#include "media/decoder.h"

#include "media/hap.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace lumen {

namespace {

std::string averr(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof(buf));
  return buf;
}

void ensureDevices() {
  static bool done = false;
  if (!done) {
    avdevice_register_all();
    done = true;
  }
}

// "a=1:b=2" -> AVDictionary
AVDictionary *parseOptions(const std::string &opts) {
  AVDictionary *d = nullptr;
  std::stringstream ss(opts);
  for (std::string kv; std::getline(ss, kv, ':');) {
    auto eq = kv.find('=');
    if (eq == std::string::npos) continue;
    av_dict_set(&d, kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 0);
  }
  return d;
}

AVPixelFormat pickHwFormat(AVCodecContext *ctx, const AVPixelFormat *fmts) {
  auto want = AVPixelFormat(intptr_t(ctx->opaque));
  for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p)
    if (*p == want) return *p;
  // Hardware path refused this stream: fall back to the first software format.
  for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(*p);
    if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
  }
  return fmts[0];
}

void copyPlane(std::vector<uint8_t> &dst, int &dstStride, const uint8_t *src, int srcStride,
               int rowBytes, int rows) {
  dstStride = rowBytes;
  dst.resize(size_t(rowBytes) * size_t(rows));
  for (int y = 0; y < rows; ++y)
    std::memcpy(dst.data() + size_t(y) * size_t(rowBytes), src + ptrdiff_t(y) * srcStride, size_t(rowBytes));
}

// Owns the demuxer/decoder/scaler for one source. Used by the threaded decoder
// and the one-shot still decoder.
struct Pipeline {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *codec = nullptr;
  AVBufferRef *hwDevice = nullptr;
  AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
  SwsContext *sws = nullptr;
  AVPacket *pkt = nullptr;
  AVFrame *frame = nullptr, *swFrame = nullptr;
  int stream = -1;
  double timeBase = 0;
  double frameDuration = 1.0 / 30.0;
  double startTime = 0;
  bool nativeHap = false; // packets parsed by us, no FFmpeg decode
  bool packetPending = false;
  std::string hwName;

  ~Pipeline() {
    if (sws) sws_freeContext(sws);
    if (frame) av_frame_free(&frame);
    if (swFrame) av_frame_free(&swFrame);
    if (pkt) av_packet_free(&pkt);
    if (codec) avcodec_free_context(&codec);
    if (hwDevice) av_buffer_unref(&hwDevice);
    if (fmt) avformat_close_input(&fmt);
  }

  void tryHardware(const AVCodec *dec) {
    static const AVHWDeviceType preferred[] = {
#ifdef _WIN32
        AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2, AV_HWDEVICE_TYPE_CUDA,
#elif defined(__APPLE__)
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
        AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA,
#endif
    };
    // Probing devices that are not installed logs scary errors: keep it quiet.
    int level = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET);
    struct Restore { int l; ~Restore() { av_log_set_level(l); } } restore{level};
    for (AVHWDeviceType type : preferred) {
      for (int i = 0;; ++i) {
        const AVCodecHWConfig *cfg = avcodec_get_hw_config(dec, i);
        if (!cfg) break;
        if (cfg->device_type != type || !(cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) continue;
        if (av_hwdevice_ctx_create(&hwDevice, type, nullptr, nullptr, 0) < 0) break;
        hwFormat = cfg->pix_fmt;
        codec->hw_device_ctx = av_buffer_ref(hwDevice);
        codec->opaque = reinterpret_cast<void *>(intptr_t(hwFormat));
        codec->get_format = pickHwFormat;
        hwName = av_hwdevice_get_type_name(type);
        return;
      }
    }
  }

  bool open(const MediaSource &src, const DecoderSettings &settings, MediaInfo &info,
            std::string *error) {
    const AVInputFormat *ifmt = nullptr;
    if (!src.format.empty()) {
      ensureDevices();
      ifmt = av_find_input_format(src.format.c_str());
      if (!ifmt) { if (error) *error = "unknown input format " + src.format; return false; }
    }
    AVDictionary *opts = parseOptions(src.options);
    if (src.live) av_dict_set(&opts, "fflags", "nobuffer", 0);
    int r = avformat_open_input(&fmt, src.url.c_str(), ifmt, &opts);
    av_dict_free(&opts);
    if (r < 0) { if (error) *error = "open: " + averr(r); return false; }
    if (src.live) fmt->flags |= AVFMT_FLAG_NOBUFFER;
    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0) { if (error) *error = "stream info: " + averr(r); return false; }

    for (unsigned i = 0; i < fmt->nb_streams; ++i)
      if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) info.hasAudio = true;

    stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) { if (error) *error = "no video stream"; return false; }
    AVStream *st = fmt->streams[stream];
    timeBase = av_q2d(st->time_base);
    startTime = st->start_time != AV_NOPTS_VALUE ? double(st->start_time) * timeBase : 0.0;
    AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
    if (fr.num > 0 && fr.den > 0) frameDuration = 1.0 / av_q2d(fr);

    info.hasVideo = true;
    info.width = st->codecpar->width;
    info.height = st->codecpar->height;
    info.fps = 1.0 / frameDuration;
    if (!src.live) {
      if (st->duration != AV_NOPTS_VALUE) info.duration = double(st->duration) * timeBase;
      else if (fmt->duration != AV_NOPTS_VALUE) info.duration = double(fmt->duration) / AV_TIME_BASE;
    }
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    swFrame = av_frame_alloc();

    if (st->codecpar->codec_id == AV_CODEC_ID_HAP && settings.nativeHap) {
      nativeHap = true;
      info.codec = "hap (GPU)";
      return true;
    }

    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { if (error) *error = "unsupported codec"; return false; }
    codec = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codec, st->codecpar);
    codec->thread_count = 0; // auto
    if (src.live) codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (settings.hardware) tryHardware(dec);
    r = avcodec_open2(codec, dec, nullptr);
    if (r < 0 && hwDevice) { // retry in software
      avcodec_free_context(&codec);
      av_buffer_unref(&hwDevice);
      hwName.clear();
      codec = avcodec_alloc_context3(dec);
      avcodec_parameters_to_context(codec, st->codecpar);
      codec->thread_count = 0;
      r = avcodec_open2(codec, dec, nullptr);
    }
    if (r < 0) { if (error) *error = "codec: " + averr(r); return false; }
    info.codec = dec->name;
    info.hwaccel = hwName;
    return true;
  }

  // Reads the next packet of our stream. Returns 1, 0 on EOF, <0 on error.
  int readPacket() {
    while (true) {
      int r = av_read_frame(fmt, pkt);
      if (r < 0) return r == AVERROR_EOF ? 0 : r;
      if (pkt->stream_index == stream) return 1;
      av_packet_unref(pkt);
    }
  }

  // Decodes the next frame into `frame`. Returns 1 on frame, 0 on EOF, <0 on error.
  int next() {
    while (true) {
      int r = avcodec_receive_frame(codec, frame);
      if (r == 0) {
        if (hwDevice && frame->format == hwFormat) {
          av_frame_unref(swFrame);
          int t = av_hwframe_transfer_data(swFrame, frame, 0);
          if (t < 0) return t;
          swFrame->pts = frame->pts;
          swFrame->best_effort_timestamp = frame->best_effort_timestamp;
          swFrame->colorspace = frame->colorspace;
          swFrame->color_range = frame->color_range;
          av_frame_unref(frame);
          av_frame_move_ref(frame, swFrame);
        }
        return 1;
      }
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
    if (codec) avcodec_flush_buffers(codec);
  }

  double tsToSeconds(int64_t ts) const {
    if (ts == AV_NOPTS_VALUE) return 0;
    return double(ts) * timeBase - startTime;
  }

  double framePts() const {
    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    return tsToSeconds(ts);
  }

  void toRGBA(VideoFrame &out) {
    int w = frame->width, h = frame->height;
    sws = sws_getCachedContext(sws, w, h, AVPixelFormat(frame->format), w, h, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    out.layout = FrameLayout::RGBA;
    out.width = w;
    out.height = h;
    out.stride[0] = w * 4;
    out.plane[0].resize(size_t(w) * size_t(h) * 4);
    uint8_t *dst[4] = {out.plane[0].data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {w * 4, 0, 0, 0};
    sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
  }

  // Copies the decoded frame keeping YUV layouts for GPU conversion.
  void toNative(VideoFrame &out) {
    auto pf = AVPixelFormat(frame->format);
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(pf);
    int w = frame->width, h = frame->height;
    out.width = w;
    out.height = h;
    out.fullRange = frame->color_range == AVCOL_RANGE_JPEG || pf == AV_PIX_FMT_YUVJ420P ||
                    pf == AV_PIX_FMT_YUVJ422P || pf == AV_PIX_FMT_YUVJ444P;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT709: out.matrix = 1; break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: out.matrix = 0; break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: out.matrix = 2; break;
    default: out.matrix = h >= 720 ? 1 : 0; break;
    }
    bool planarYuv = d && !(d->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA | AV_PIX_FMT_FLAG_HWACCEL |
                                        AV_PIX_FMT_FLAG_BE)) &&
                     d->nb_components == 3 && (d->flags & AV_PIX_FMT_FLAG_PLANAR);
    int depth = d ? d->comp[0].depth : 8;
    if (planarYuv && d->comp[1].plane == 1 && d->comp[2].plane == 2 && depth <= 16) {
      out.layout = FrameLayout::Planar;
      out.wide = depth > 8;
      out.valueScale = out.wide ? 65535.0f / float((1 << depth) - 1) : 1.0f;
      out.chromaShiftX = d->log2_chroma_w;
      out.chromaShiftY = d->log2_chroma_h;
      int bpp = out.wide ? 2 : 1;
      int cw = AV_CEIL_RSHIFT(w, d->log2_chroma_w), ch = AV_CEIL_RSHIFT(h, d->log2_chroma_h);
      copyPlane(out.plane[0], out.stride[0], frame->data[0], frame->linesize[0], w * bpp, h);
      copyPlane(out.plane[1], out.stride[1], frame->data[1], frame->linesize[1], cw * bpp, ch);
      copyPlane(out.plane[2], out.stride[2], frame->data[2], frame->linesize[2], cw * bpp, ch);
      return;
    }
    if (pf == AV_PIX_FMT_NV12 || pf == AV_PIX_FMT_P010LE || pf == AV_PIX_FMT_P016LE) {
      out.layout = FrameLayout::NV12;
      out.wide = pf != AV_PIX_FMT_NV12;
      out.valueScale = 1.0f; // P010 keeps samples in the high bits
      out.chromaShiftX = out.chromaShiftY = 1;
      int bpp = out.wide ? 2 : 1;
      copyPlane(out.plane[0], out.stride[0], frame->data[0], frame->linesize[0], w * bpp, h);
      copyPlane(out.plane[1], out.stride[1], frame->data[1], frame->linesize[1], ((w + 1) / 2) * 2 * bpp, (h + 1) / 2);
      return;
    }
    toRGBA(out); // anything else (RGB, alpha, palettes...) via swscale
  }

  // Native HAP: parse the packet into compressed texture data.
  int nextHap(VideoFrame &out, double &pts) {
    int r = readPacket();
    if (r <= 0) return r;
    HapFrame hf;
    std::string err;
    bool ok = decodeHapFrame(pkt->data, size_t(pkt->size), hf, &err);
    pts = tsToSeconds(pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts);
    av_packet_unref(pkt);
    if (!ok) {
      std::fprintf(stderr, "[hap] %s\n", err.c_str());
      return AVERROR_INVALIDDATA;
    }
    out.width = fmt->streams[stream]->codecpar->width;
    out.height = fmt->streams[stream]->codecpar->height;
    switch (hf.format) {
    case HapFormat::DXT1: out.layout = FrameLayout::DXT1; break;
    case HapFormat::DXT5: out.layout = FrameLayout::DXT5; break;
    case HapFormat::YCoCgDXT5: out.layout = hf.alpha.empty() ? FrameLayout::YCoCgDXT5 : FrameLayout::YCoCgDXT5A; break;
    }
    out.plane[0] = std::move(hf.color);
    out.plane[1] = std::move(hf.alpha);
    return 1;
  }
};

} // namespace

bool probeMedia(const MediaSource &src, MediaInfo &info, std::string *error) {
  Pipeline p;
  DecoderSettings s;
  s.hardware = false;
  return p.open(src, s, info, error);
}

bool decodeStill(const std::string &path, VideoFrame &out, std::string *error) {
  Pipeline p;
  MediaInfo info;
  DecoderSettings s;
  s.hardware = false;
  s.nativeHap = false;
  if (!p.open(MediaSource{path, "", "", false}, s, info, error)) return false;
  if (p.next() != 1) { if (error) *error = "could not decode a frame"; return false; }
  p.toRGBA(out);
  out.pts = 0;
  return true;
}

std::vector<CaptureDevice> listCaptureDevices() {
  ensureDevices();
  std::vector<CaptureDevice> out;
#ifdef _WIN32
  const char *formats[] = {"dshow"};
#elif defined(__APPLE__)
  const char *formats[] = {"avfoundation"};
#else
  const char *formats[] = {"v4l2"};
#endif
  for (const char *name : formats) {
    const AVInputFormat *f = av_find_input_format(name);
    if (!f) continue;
    AVDeviceInfoList *list = nullptr;
    if (avdevice_list_input_sources(f, nullptr, nullptr, &list) < 0 || !list) continue;
    for (int i = 0; i < list->nb_devices; ++i) {
      const AVDeviceInfo *d = list->devices[i];
      bool video = d->nb_media_types == 0;
      for (int k = 0; k < d->nb_media_types; ++k)
        if (d->media_types[k] == AVMEDIA_TYPE_VIDEO) video = true;
      if (!video) continue;
      CaptureDevice c;
      c.name = d->device_description ? d->device_description : d->device_name;
      c.format = name;
#ifdef _WIN32
      c.url = std::string("video=") + (d->device_description ? d->device_description : d->device_name);
#else
      c.url = d->device_name;
#endif
      out.push_back(c);
    }
    avdevice_free_list_devices(&list);
  }
  return out;
}

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(const MediaSource &src, bool loop, const DecoderSettings &settings,
                        std::string *error) {
  close();
  // Probe synchronously so errors are reported to the caller.
  if (!src.live && !probeMedia(src, info_, error)) return false;
  src_ = src;
  settings_ = settings;
  loop_ = loop && !src.live;
  queueSize_ = src.live ? 2 : 6;
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
  if (src_.live) return;
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

std::string VideoDecoder::hwaccel() {
  std::lock_guard<std::mutex> lock(mutex_);
  return hwaccel_;
}

bool VideoDecoder::fetch(double time, VideoFrame &out, bool force) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (queue_.empty()) return false;
  if (src_.live) {
    while (queue_.size() > 1) {
      pool_.push_back(std::move(queue_.front()));
      queue_.pop_front();
    }
  } else {
    // Skip frames that are already late, keeping the newest one that is due.
    while (queue_.size() >= 2 && queue_[1].pts <= time) {
      pool_.push_back(std::move(queue_.front()));
      queue_.pop_front();
    }
    if (queue_.front().pts > time && !force) return false;
  }
  if (!out.plane[0].empty()) pool_.push_back(std::move(out));
  out = std::move(queue_.front());
  queue_.pop_front();
  lock.unlock();
  cv_.notify_all();
  return true;
}

void VideoDecoder::run() {
  while (running_) {
    Pipeline p;
    MediaInfo info;
    std::string err;
    if (!p.open(src_, settings_, info, &err)) {
      std::fprintf(stderr, "[decoder] %s: %s\n", src_.url.c_str(), err.c_str());
      if (!src_.live) {
        std::lock_guard<std::mutex> lock(mutex_);
        eof_ = true;
        return;
      }
      // Live source unavailable (camera unplugged, stream down): retry.
      for (int i = 0; i < 20 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hwaccel_ = info.hwaccel;
      if (src_.live) info_ = info;
    }
    // Same loop length as the audio decoder so both clocks stay locked.
    loopLength_ = info.duration > 0 ? info.duration : 0;

    double loopOffset = 0; // added to pts so time keeps increasing across loops
    double maxPts = 0;
    bool reopen = false;

    while (running_ && !reopen) {
      if (restart_.exchange(false)) {
        p.rewind();
        loopOffset = 0;
        maxPts = 0;
      }
      if (!src_.live) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(20), [&] {
          return !running_ || restart_ || (queue_.size() < queueSize_ && !eof_);
        });
        if (!running_) break;
        if (restart_ || eof_ || queue_.size() >= queueSize_) continue;
      }

      VideoFrame f;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pool_.empty()) { f = std::move(pool_.back()); pool_.pop_back(); }
      }
      double pts = 0;
      int r;
      if (p.nativeHap) {
        r = p.nextHap(f, pts);
      } else {
        r = p.next();
        if (r == 1) {
          pts = p.framePts();
          p.toNative(f);
        }
      }

      if (r == 1) {
        maxPts = std::max(maxPts, pts);
        f.pts = loopOffset + pts;
        std::lock_guard<std::mutex> lock(mutex_);
        if (restart_) continue;
        if (src_.live) // never block a live source: keep only the newest frames
          while (queue_.size() >= queueSize_) { pool_.push_back(std::move(queue_.front())); queue_.pop_front(); }
        queue_.push_back(std::move(f));
      } else if (r == 0 && src_.live) {
        reopen = true; // stream ended: reconnect
      } else if (r == 0 && loop_) {
        double len = loopLength_ > 0 ? loopLength_ : maxPts + p.frameDuration;
        loopOffset += len;
        maxPts = 0;
        p.rewind();
      } else if (r == AVERROR_INVALIDDATA) {
        continue; // skip a damaged frame
      } else {
        if (r < 0) std::fprintf(stderr, "[decoder] %s: %s\n", src_.url.c_str(), averr(r).c_str());
        if (src_.live) { reopen = true; continue; }
        std::lock_guard<std::mutex> lock(mutex_);
        eof_ = true;
      }
    }
    if (!src_.live) break;
  }
}

} // namespace lumen
