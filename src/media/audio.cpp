#include "media/audio.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio/miniaudio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#if defined(__linux__) && defined(__LINUX_ALSA__)
#include <alsa/asoundlib.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lumen {

// ------------------------------------------------------------------ voice

AudioVoice::AudioVoice(size_t capacityFrames)
    : ring_(capacityFrames * kAudioChannels), capacity_(capacityFrames) {}

size_t AudioVoice::space() {
  std::lock_guard<std::mutex> lock(mutex_);
  return capacity_ - count_;
}

size_t AudioVoice::write(const float *in, size_t frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t n = std::min(frames, capacity_ - count_);
  size_t writePos = (readPos_ + count_) % capacity_;
  for (size_t i = 0; i < n; ++i) {
    size_t p = (writePos + i) % capacity_;
    ring_[p * 2] = in[i * 2];
    ring_[p * 2 + 1] = in[i * 2 + 1];
  }
  count_ += n;
  return n;
}

void AudioVoice::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  readPos_ = count_ = 0;
  played_ = 0;
}

void AudioVoice::mixInto(float *out, size_t frames) {
  if (!active) { peak_ = 0; return; }
  std::lock_guard<std::mutex> lock(mutex_);
  size_t n = std::min(frames, count_);
  starving_ = n < frames;
  float target = gain.load();
  float g = currentGain_;
  // Linear ramp over the buffer avoids clicks when the gain changes.
  float step = frames ? (target - g) / float(frames) : 0.0f;
  float pk = 0;
  for (size_t i = 0; i < n; ++i) {
    size_t p = (readPos_ + i) % capacity_;
    g += step;
    float l = ring_[p * 2] * g, r = ring_[p * 2 + 1] * g;
    out[i * 2] += l;
    out[i * 2 + 1] += r;
    pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
  }
  currentGain_ = target;
  readPos_ = (readPos_ + n) % capacity_;
  count_ -= n;
  played_ += n;
  peak_ = std::max(pk, peak_.load() * 0.9f);
}

// ------------------------------------------------------------------ engine

struct AudioEngine::Impl {
  ma_context context{};
  ma_device device{};
  bool hasContext = false, hasDevice = false;
};

static void dataCallback(ma_device *d, void *out, const void *, ma_uint32 frames) {
  static_cast<AudioEngine *>(d->pUserData)->callback(static_cast<float *>(out), frames);
}

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {
  peak_[0] = 0;
  peak_[1] = 0;
}

AudioEngine::~AudioEngine() { stop(); }

std::vector<std::string> AudioEngine::devices() {
  std::vector<std::string> names;
  ma_context ctx;
  if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return names;
  ma_device_info *playback = nullptr;
  ma_uint32 count = 0;
  if (ma_context_get_devices(&ctx, &playback, &count, nullptr, nullptr) == MA_SUCCESS)
    for (ma_uint32 i = 0; i < count; ++i) names.emplace_back(playback[i].name);
  ma_context_uninit(&ctx);
  return names;
}

#if defined(__linux__) && defined(__LINUX_ALSA__)
static void silentAlsa(const char *, int, const char *, int, const char *, ...) {}
#endif

bool AudioEngine::start(const std::string &name) {
#if defined(__linux__) && defined(__LINUX_ALSA__)
  // ALSA prints a page of errors on machines without a sound card.
  snd_lib_error_set_handler(silentAlsa);
#endif
  stop();
  error_.clear();
  silent_ = false;
  auto tryInit = [&](const ma_backend *backends, ma_uint32 nb) -> bool {
    if (ma_context_init(backends, nb, nullptr, &impl_->context) != MA_SUCCESS) return false;
    impl_->hasContext = true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kAudioChannels;
    cfg.sampleRate = kAudioRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    cfg.periodSizeInMilliseconds = 10;
    ma_device_id id{};
    bool found = false;
    if (!name.empty()) {
      ma_device_info *playback = nullptr;
      ma_uint32 count = 0;
      if (ma_context_get_devices(&impl_->context, &playback, &count, nullptr, nullptr) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < count; ++i)
          if (name == playback[i].name) { id = playback[i].id; found = true; }
      if (!found) error_ = "dispositivo \"" + name + "\" não encontrado, usando o padrão";
    }
    if (found) cfg.playback.pDeviceID = &id;
    if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) {
      ma_context_uninit(&impl_->context);
      impl_->hasContext = false;
      return false;
    }
    impl_->hasDevice = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
      stop();
      return false;
    }
    deviceName_ = impl_->device.playback.name;
    return true;
  };
  if (!tryInit(nullptr, 0)) {
    // No sound card: a null device keeps the audio clock (and sync) running.
    ma_backend nullBackend[] = {ma_backend_null};
    if (!tryInit(nullBackend, 1)) {
      error_ = "não foi possível abrir nenhum dispositivo de áudio";
      return false;
    }
    silent_ = true;
    deviceName_ = "(sem saída de áudio)";
  }
  running_ = true;
  return true;
}

void AudioEngine::stop() {
  if (impl_->hasDevice) {
    ma_device_uninit(&impl_->device);
    impl_->hasDevice = false;
  }
  if (impl_->hasContext) {
    ma_context_uninit(&impl_->context);
    impl_->hasContext = false;
  }
  running_ = false;
}

std::shared_ptr<AudioVoice> AudioEngine::createVoice() {
  auto v = std::make_shared<AudioVoice>();
  std::lock_guard<std::mutex> lock(voicesMutex_);
  voices_.push_back(v);
  return v;
}

void AudioEngine::removeVoice(const std::shared_ptr<AudioVoice> &v) {
  std::lock_guard<std::mutex> lock(voicesMutex_);
  voices_.erase(std::remove(voices_.begin(), voices_.end(), v), voices_.end());
}

void AudioEngine::callback(float *out, size_t frames) {
  std::memset(out, 0, frames * kAudioChannels * sizeof(float));
  {
    std::lock_guard<std::mutex> lock(voicesMutex_);
    for (auto &v : voices_) v->mixInto(out, frames);
  }
  float m = master.load(), pl = 0, pr = 0;
  for (size_t i = 0; i < frames; ++i) {
    // Soft clip keeps several loud voices from wrapping around.
    float l = std::tanh(out[i * 2] * m), r = std::tanh(out[i * 2 + 1] * m);
    out[i * 2] = l;
    out[i * 2 + 1] = r;
    pl = std::max(pl, std::fabs(l));
    pr = std::max(pr, std::fabs(r));
  }
  peak_[0] = std::max(pl, peak_[0].load() * 0.92f);
  peak_[1] = std::max(pr, peak_[1].load() * 0.92f);
}

// ------------------------------------------------------------------ decoder

AudioDecoder::~AudioDecoder() { close(); }

bool AudioDecoder::open(const std::string &path, std::shared_ptr<AudioVoice> voice, bool loop,
                        double loopLength, std::string *error) {
  close();
  path_ = path;
  voice_ = std::move(voice);
  loop_ = loop;
  loopLength_ = loopLength;
  finished_ = false;
  running_ = true;
  thread_ = std::thread(&AudioDecoder::run, this);
  return true;
}

void AudioDecoder::close() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void AudioDecoder::restart() {
  restart_ = true;
  finished_ = false;
}

void AudioDecoder::run() {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *codec = nullptr;
  SwrContext *swr = nullptr;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  int stream = -1;
  auto cleanup = [&] {
    if (swr) swr_free(&swr);
    if (codec) avcodec_free_context(&codec);
    if (fmt) avformat_close_input(&fmt);
    av_frame_free(&frame);
    av_packet_free(&pkt);
  };

  if (avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr) < 0 ||
      avformat_find_stream_info(fmt, nullptr) < 0 ||
      (stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0)) < 0) {
    finished_ = true;
    cleanup();
    return;
  }
  AVStream *st = fmt->streams[stream];
  const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!dec) { finished_ = true; cleanup(); return; }
  codec = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(codec, st->codecpar);
  if (avcodec_open2(codec, dec, nullptr) < 0) { finished_ = true; cleanup(); return; }

  AVChannelLayout outLayout;
  av_channel_layout_default(&outLayout, kAudioChannels);
  if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate, &codec->ch_layout,
                          codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0 ||
      swr_init(swr) < 0) {
    finished_ = true;
    cleanup();
    return;
  }

  // Exact frame count per loop: identical to the video loop length.
  const int64_t framesPerLoop = loopLength_ > 0 ? int64_t(std::llround(loopLength_ * kAudioRate)) : -1;
  int64_t loopFrames = 0;
  std::vector<float> buf;
  std::vector<float> pending; // converted audio not yet accepted by the voice
  size_t pendingPos = 0;
  bool eof = false;

  auto rewind = [&] {
    av_seek_frame(fmt, stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec);
    swr_init(swr);
    loopFrames = 0;
    pending.clear();
    pendingPos = 0;
    eof = false;
  };

  while (running_) {
    if (restart_.exchange(false)) {
      rewind();
      voice_->reset();
    }
    // Feed what we already have first.
    if (pendingPos < pending.size()) {
      size_t frames = (pending.size() - pendingPos) / 2;
      size_t n = voice_->write(pending.data() + pendingPos, frames);
      pendingPos += n * 2;
      if (n < frames) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    pending.clear();
    pendingPos = 0;

    if (eof) {
      // Pad with silence up to the exact loop length, then loop or stop.
      if (framesPerLoop > 0 && loopFrames < framesPerLoop && loop_) {
        int64_t n = std::min<int64_t>(framesPerLoop - loopFrames, 2048);
        pending.assign(size_t(n) * 2, 0.0f);
        loopFrames += n;
        continue;
      }
      if (loop_) { rewind(); continue; }
      finished_ = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }

    int r = avcodec_receive_frame(codec, frame);
    if (r == AVERROR(EAGAIN)) {
      int rr = av_read_frame(fmt, pkt);
      if (rr < 0) avcodec_send_packet(codec, nullptr);
      else {
        if (pkt->stream_index == stream) avcodec_send_packet(codec, pkt);
        av_packet_unref(pkt);
      }
      continue;
    }
    if (r == AVERROR_EOF) {
      // Drain the resampler.
      int out = swr_get_out_samples(swr, 0);
      if (out > 0) {
        buf.resize(size_t(out) * 2);
        uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
        int got = swr_convert(swr, &dst, out, nullptr, 0);
        if (got > 0) pending.assign(buf.begin(), buf.begin() + got * 2);
        loopFrames += got > 0 ? got : 0;
      }
      eof = true;
      continue;
    }
    if (r < 0) { eof = true; continue; }

    int out = swr_get_out_samples(swr, frame->nb_samples);
    buf.resize(size_t(std::max(out, 1)) * 2);
    uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
    int got = swr_convert(swr, &dst, out, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    av_frame_unref(frame);
    if (got <= 0) continue;
    int64_t keep = got;
    if (framesPerLoop > 0 && loop_) keep = std::min<int64_t>(keep, framesPerLoop - loopFrames);
    if (keep <= 0) { eof = true; continue; } // audio longer than the video: cut
    pending.assign(buf.begin(), buf.begin() + keep * 2);
    loopFrames += keep;
  }
  av_channel_layout_uninit(&outLayout);
  cleanup();
}

// ------------------------------------------------------------------ LTC input

struct LtcCapture::Impl {
  ma_context context{};
  ma_device device{};
  bool hasContext = false, hasDevice = false;
};

static void captureCallback(ma_device *d, void *, const void *in, ma_uint32 frames) {
  static_cast<LtcCapture *>(d->pUserData)->callback(static_cast<const float *>(in), frames, int(d->capture.channels));
}

LtcCapture::LtcCapture() : impl_(std::make_unique<Impl>()) {}
LtcCapture::~LtcCapture() { stop(); }

std::vector<std::string> LtcCapture::devices() {
  std::vector<std::string> names;
  ma_context ctx;
  if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return names;
  ma_device_info *capture = nullptr;
  ma_uint32 count = 0;
  if (ma_context_get_devices(&ctx, nullptr, nullptr, &capture, &count) == MA_SUCCESS)
    for (ma_uint32 i = 0; i < count; ++i) names.emplace_back(capture[i].name);
  ma_context_uninit(&ctx);
  return names;
}

bool LtcCapture::start(const std::string &name, int channel, std::string *error) {
  stop();
  channel_ = std::clamp(channel, 0, 7);
  if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
    if (error) *error = "sem sistema de áudio";
    return false;
  }
  impl_->hasContext = true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 0; // native channel count
  cfg.sampleRate = kAudioRate;
  cfg.dataCallback = captureCallback;
  cfg.pUserData = this;
  ma_device_id id{};
  if (!name.empty()) {
    ma_device_info *capture = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&impl_->context, nullptr, nullptr, &capture, &count) == MA_SUCCESS)
      for (ma_uint32 i = 0; i < count; ++i)
        if (name == capture[i].name) { id = capture[i].id; cfg.capture.pDeviceID = &id; }
  }
  if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) {
    if (error) *error = "não foi possível abrir a entrada de áudio";
    stop();
    return false;
  }
  impl_->hasDevice = true;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    if (error) *error = "a entrada de áudio não iniciou";
    stop();
    return false;
  }
  running_ = true;
  return true;
}

void LtcCapture::stop() {
  if (impl_->hasDevice) {
    ma_device_uninit(&impl_->device);
    impl_->hasDevice = false;
  }
  if (impl_->hasContext) {
    ma_context_uninit(&impl_->context);
    impl_->hasContext = false;
  }
  running_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  have_ = false;
}

void LtcCapture::callback(const float *in, size_t frames, int channels) {
  if (!in || channels <= 0) return;
  int ch = std::min(channel_, channels - 1);
  float pk = 0;
  for (size_t i = 0; i < frames; ++i) pk = std::max(pk, std::fabs(in[i * size_t(channels) + size_t(ch)]));
  level_ = std::max(pk, level_.load() * 0.9f);
  decoder_.feed(in + ch, frames, size_t(channels));
  if (decoder_.frames() != lastFrames_) {
    lastFrames_ = decoder_.frames();
    Timecode tc;
    decoder_.latest(tc);
    std::lock_guard<std::mutex> lock(mutex_);
    last_ = tc;
    at_ = std::chrono::steady_clock::now();
    have_ = true;
  }
}

bool LtcCapture::latest(Timecode &tc, double &age) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!have_) return false;
  tc = last_;
  age = std::chrono::duration<double>(std::chrono::steady_clock::now() - at_).count();
  return true;
}

} // namespace lumen
