#include "media/decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace lumen {

namespace {

std::string averr(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof(buf));
  return buf;
}

// Owns the demuxer/decoder/scaler for one file. Used by both the threaded
// decoder and the one-shot still decoder.
struct Pipeline {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *codec = nullptr;
  SwsContext *sws = nullptr;
  AVPacket *pkt = nullptr;
  AVFrame *frame = nullptr;
  int stream = -1;
  double timeBase = 0;
  double frameDuration = 1.0 / 30.0;

  ~Pipeline() {
    if (sws) sws_freeContext(sws);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (codec) avcodec_free_context(&codec);
    if (fmt) avformat_close_input(&fmt);
  }

  bool open(const std::string &path, MediaInfo &info, std::string *error) {
    int r = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (r < 0) { if (error) *error = "open: " + averr(r); return false; }
    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0) { if (error) *error = "stream info: " + averr(r); return false; }

    for (unsigned i = 0; i < fmt->nb_streams; ++i)
      if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) info.hasAudio = true;

    stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) { if (error) *error = "no video stream"; return false; }
    AVStream *st = fmt->streams[stream];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { if (error) *error = "unsupported codec"; return false; }
    codec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codec, st->codecpar);
    codec->thread_count = 0; // auto
    r = avcodec_open2(codec, dec, nullptr);
    if (r < 0) { if (error) *error = "codec: " + averr(r); return false; }

    timeBase = av_q2d(st->time_base);
    AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
    if (fr.num > 0 && fr.den > 0) frameDuration = 1.0 / av_q2d(fr);

    info.hasVideo = true;
    info.width = codec->width;
    info.height = codec->height;
    info.fps = 1.0 / frameDuration;
    info.codec = dec->name;
    if (st->duration != AV_NOPTS_VALUE) info.duration = double(st->duration) * timeBase;
    else if (fmt->duration != AV_NOPTS_VALUE) info.duration = double(fmt->duration) / AV_TIME_BASE;

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    return true;
  }

  // Decodes the next frame into `frame`. Returns 1 on frame, 0 on EOF, <0 on error.
  int next() {
    while (true) {
      int r = avcodec_receive_frame(codec, frame);
      if (r == 0) return 1;
      if (r == AVERROR_EOF) return 0;
      if (r != AVERROR(EAGAIN)) return r;
      r = av_read_frame(fmt, pkt);
      if (r < 0) {
        avcodec_send_packet(codec, nullptr); // flush remaining frames
        continue;
      }
      if (pkt->stream_index == stream) avcodec_send_packet(codec, pkt);
      av_packet_unref(pkt);
    }
  }

  void rewind() {
    av_seek_frame(fmt, stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec);
  }

  double framePts() const {
    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    if (ts == AV_NOPTS_VALUE) return 0;
    double start = fmt->streams[stream]->start_time != AV_NOPTS_VALUE
                       ? double(fmt->streams[stream]->start_time) * timeBase
                       : 0.0;
    return double(ts) * timeBase - start;
  }

  void toRGBA(VideoFrame &out) {
    int w = frame->width, h = frame->height;
    sws = sws_getCachedContext(sws, w, h, AVPixelFormat(frame->format), w, h, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    uint8_t *dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {w * 4, 0, 0, 0};
    sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
  }
};

} // namespace

bool probeMedia(const std::string &path, MediaInfo &info, std::string *error) {
  Pipeline p;
  return p.open(path, info, error);
}

bool decodeStill(const std::string &path, VideoFrame &out, std::string *error) {
  Pipeline p;
  MediaInfo info;
  if (!p.open(path, info, error)) return false;
  if (p.next() != 1) { if (error) *error = "could not decode a frame"; return false; }
  p.toRGBA(out);
  out.pts = 0;
  return true;
}

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(const std::string &path, bool loop, std::string *error) {
  close();
  if (!probeMedia(path, info_, error)) return false;
  path_ = path;
  loop_ = loop;
  eof_ = false;
  running_ = true;
  thread_ = std::thread(&VideoDecoder::run, this);
  return true;
}

void VideoDecoder::close() {
  if (!running_ && !thread_.joinable()) return;
  running_ = false;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  queue_.clear();
}

void VideoDecoder::restart() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &f : queue_) pool_.push_back(std::move(f));
    queue_.clear();
    eof_ = false;
    restart_ = true;
  }
  cv_.notify_all();
}

bool VideoDecoder::finished() {
  std::lock_guard<std::mutex> lock(mutex_);
  return eof_ && queue_.empty();
}

bool VideoDecoder::fetch(double time, VideoFrame &out, bool force) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (queue_.empty()) return false;
  // Skip frames that are already late, keeping the newest one that is due.
  while (queue_.size() >= 2 && queue_[1].pts <= time) {
    pool_.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }
  if (queue_.front().pts > time && !force) return false;
  if (!out.rgba.empty()) pool_.push_back(std::move(out));
  out = std::move(queue_.front());
  queue_.pop_front();
  lock.unlock();
  cv_.notify_all();
  return true;
}

void VideoDecoder::run() {
  Pipeline p;
  MediaInfo info;
  std::string err;
  if (!p.open(path_, info, &err)) {
    std::fprintf(stderr, "[decoder] %s: %s\n", path_.c_str(), err.c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    eof_ = true;
    return;
  }

  double loopOffset = 0; // added to pts so time keeps increasing across loops
  double maxPts = 0;

  while (running_) {
    if (restart_.exchange(false)) {
      p.rewind();
      loopOffset = 0;
      maxPts = 0;
    }
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(20), [&] {
        return !running_ || restart_ || (queue_.size() < kQueueSize && !eof_);
      });
      if (!running_) break;
      if (restart_ || eof_ || queue_.size() >= kQueueSize) continue;
    }

    int r = p.next();
    if (r == 1) {
      double pts = p.framePts();
      maxPts = std::max(maxPts, pts);
      VideoFrame f;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pool_.empty()) { f = std::move(pool_.back()); pool_.pop_back(); }
      }
      p.toRGBA(f);
      f.pts = loopOffset + pts;
      std::lock_guard<std::mutex> lock(mutex_);
      if (!restart_) queue_.push_back(std::move(f));
    } else if (r == 0 && loop_) {
      loopOffset += maxPts + p.frameDuration;
      maxPts = 0;
      p.rewind();
    } else {
      if (r < 0) std::fprintf(stderr, "[decoder] %s: %s\n", path_.c_str(), averr(r).c_str());
      std::lock_guard<std::mutex> lock(mutex_);
      eof_ = true;
    }
  }
}

} // namespace lumen
