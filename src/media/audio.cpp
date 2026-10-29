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

static long long steadyNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ voice

AudioVoice::AudioVoice(size_t capacityFrames)
    : ring_(capacityFrames * 2), capacity_(capacityFrames) {}

void AudioVoice::setChannels(int channels) {
  channels = std::clamp(channels, 1, kMaxVoiceChannels);
  std::lock_guard<std::mutex> lock(mutex_);
  if (channels == channels_.load() && ring_.size() == capacity_ * size_t(channels)) return;
  channels_ = channels;
  ring_.assign(capacity_ * size_t(channels), 0.0f);
  readPos_ = count_ = 0;
  routeOut_ = -1; // re-resolve the routing
}

size_t AudioVoice::space() {
  std::lock_guard<std::mutex> lock(mutex_);
  return capacity_ - count_;
}

size_t AudioVoice::buffered() {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

size_t AudioVoice::write(const float *in, size_t frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t ch = size_t(channels_.load());
  size_t n = std::min(frames, capacity_ - count_);
  size_t writePos = (readPos_ + count_) % capacity_;
  for (size_t i = 0; i < n; ++i) {
    size_t p = (writePos + i) % capacity_;
    std::memcpy(&ring_[p * ch], &in[i * ch], ch * sizeof(float));
  }
  count_ += n;
  return n;
}

void AudioVoice::reset(double atSeconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  readPos_ = count_ = 0;
  pos_ = 0;
  played_ = uint64_t(std::llround(std::max(0.0, atSeconds) * kAudioRate));
}

void AudioVoice::setRoute(int firstOutput, const std::vector<AudioPatch> &custom) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (firstOutput == firstOutput_ && custom == custom_) return;
  firstOutput_ = firstOutput;
  custom_ = custom;
  routeOut_ = -1;
}

void AudioVoice::mixInto(float *out, size_t frames, int stride, int outChannels) {
  if (!active) {
    peak_ = 0;
    if (retiring) retired = true;
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  int src = channels_.load();
  if (routeOut_ != outChannels || routeSrc_ != src) { // rare: device or file changed
    patches_ = resolveAudioRoute(src, outChannels, firstOutput_, custom_);
    routeOut_ = outChannels;
    routeSrc_ = src;
  }
  bool held = hold.load();
  if (held && currentGain_ == 0) { // paused, already faded out
    peak_ = 0;
    starving_ = false;
    if (retiring) retired = true;
    return;
  }
  float target = retiring || held ? 0.0f : gain.load(); // a removed or paused voice fades out: no click
  float g = currentGain_;
  // Linear ramp over the buffer avoids clicks when the gain changes.
  float step = frames ? (target - g) / float(frames) : 0.0f;
  float pk = 0;
  size_t oc = size_t(stride);
  double r = std::clamp(double(rate.load()), 0.0, 8.0);
  if (r != 1.0 || pos_ != 0) {
    // Another speed: read the source at `r` frames per output frame, linear interpolation.
    size_t i = 0;
    for (; i < frames; ++i) {
      size_t k = size_t(pos_);
      if (k + 1 >= count_) break; // not enough decoded sound yet
      float t = float(pos_ - double(k));
      const float *a = &ring_[((readPos_ + k) % capacity_) * size_t(src)];
      const float *b = &ring_[((readPos_ + k + 1) % capacity_) * size_t(src)];
      g += step;
      float *o = &out[i * oc];
      for (const AudioPatch &pa : patches_) {
        float v = (a[pa.src] + (b[pa.src] - a[pa.src]) * t) * pa.gain * g;
        o[pa.dst] += v;
        pk = std::max(pk, std::fabs(v));
      }
      pos_ += r;
    }
    starving_ = i < frames;
    size_t used = std::min(size_t(pos_), count_);
    pos_ -= double(used);
    if (r == 1.0 && pos_ < 1e-9) pos_ = 0; // back at normal speed: the exact path again
    currentGain_ = target;
    readPos_ = (readPos_ + used) % capacity_;
    count_ -= used;
    played_ += used;
    peak_ = std::max(pk, peak_.load() * 0.9f);
    if (retiring) retired = true;
    return;
  }
  size_t n = std::min(frames, count_);
  starving_ = n < frames;
  for (size_t i = 0; i < n; ++i) {
    size_t p = (readPos_ + i) % capacity_;
    g += step;
    const float *frame = &ring_[p * size_t(src)];
    float *o = &out[i * oc];
    for (const AudioPatch &pa : patches_) {
      float v = frame[pa.src] * pa.gain * g;
      o[pa.dst] += v;
      pk = std::max(pk, std::fabs(v));
    }
  }
  currentGain_ = target;
  readPos_ = (readPos_ + n) % capacity_;
  count_ -= n;
  played_ += n;
  peak_ = std::max(pk, peak_.load() * 0.9f);
  if (retiring) retired = true; // ramped to silence in this buffer
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
  for (auto &p : peak_) p = 0;
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

bool AudioEngine::start(const std::string &name, const AudioDeviceOptions &opt) {
#if defined(__linux__) && defined(__LINUX_ALSA__)
  // ALSA prints a page of errors on machines without a sound card.
  snd_lib_error_set_handler(silentAlsa);
#endif
  stop();
  error_.clear();
  silent_ = false;
  auto tryInit = [&](const ma_backend *backends, ma_uint32 nb, bool exclusive) -> bool {
    if (ma_context_init(backends, nb, nullptr, &impl_->context) != MA_SUCCESS) return false;
    impl_->hasContext = true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    // 0 = the device's own channel count (every output of the interface).
    cfg.playback.channels = ma_uint32(std::clamp(opt.channels, 0, kMaxOutputChannels));
    cfg.playback.shareMode = exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    cfg.sampleRate = kAudioRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    cfg.periodSizeInMilliseconds = ma_uint32(std::clamp(opt.bufferMs, 2, 100));
    cfg.wasapi.noAutoConvertSRC = exclusive ? MA_TRUE : MA_FALSE;
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
    // More channels asked than the device has: miniaudio would fold the extra
    // ones into the real outputs. Reopen with the device's own count instead.
    ma_uint32 native = impl_->device.playback.internalChannels;
    if (cfg.playback.channels > 0 && native > 0 && cfg.playback.channels > native) {
      ma_device_uninit(&impl_->device);
      impl_->hasDevice = false;
      cfg.playback.channels = native;
      if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) {
        ma_context_uninit(&impl_->context);
        impl_->hasContext = false;
        return false;
      }
      impl_->hasDevice = true;
    }
    // The callback mixes with this layout: it must be right before the device
    // starts calling it (a stale, larger count overflows the buffer). Devices
    // with more than kMaxOutputChannels outputs keep their own frame layout
    // (stride); only the first kMaxOutputChannels are used.
    stride_ = std::max(1, int(impl_->device.playback.channels));
    channels_ = std::min(stride_.load(), kMaxOutputChannels);
    lastCallbackNs_ = steadyNs(); // a device that never calls back counts as stalled
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
      stop();
      return false;
    }
    deviceName_ = impl_->device.playback.name;
    return true;
  };
  exclusive_ = false;
  bool ok = false;
  if (opt.exclusive) { // the device may refuse (in use, format): then shared
    ok = tryInit(nullptr, 0, true);
    exclusive_ = ok;
    if (!ok) error_ = "modo exclusivo recusado pelo dispositivo, usando o compartilhado";
  }
  if (!ok) ok = tryInit(nullptr, 0, false);
  if (!ok) {
    // No sound card: a null device keeps the audio clock (and sync) running.
    ma_backend nullBackend[] = {ma_backend_null};
    if (!tryInit(nullBackend, 1, false)) {
      error_ = "não foi possível abrir nenhum dispositivo de áudio";
      return false;
    }
    silent_ = true;
    deviceName_ = "(sem saída de áudio)";
  }
  latencyMs_ = int(impl_->device.playback.internalPeriodSizeInFrames * 1000 /
                   std::max<ma_uint32>(1, impl_->device.playback.internalSampleRate));
  for (auto &p : peak_) p = 0;
  running_ = true;
  ++generation_;
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
  if (v) v->retiring = true; // the callback ramps it to silence (no click on a cut)
  purge();
}

void AudioEngine::purge() {
  // Without a running callback nothing fades: drop the removed voices at once.
  bool now = !running_ || stalled();
  std::lock_guard<std::mutex> lock(voicesMutex_);
  std::erase_if(voices_, [&](const std::shared_ptr<AudioVoice> &v) { return v->retired || (now && v->retiring); });
}

bool AudioEngine::stalled() const {
  return running_ && steadyNs() - lastCallbackNs_.load() > 500'000'000LL;
}

void AudioEngine::debugStall() {
  if (impl_->hasDevice) ma_device_stop(&impl_->device);
}

void AudioEngine::debugRecord(double seconds) {
  recording_ = false;
  recChannels_ = stride_;
  rec_.assign(size_t(seconds * kAudioRate) * size_t(recChannels_), 0.0f);
  recPos_ = 0;
  recording_.store(true, std::memory_order_release);
}

bool AudioEngine::saveRecording(const std::string &path) const {
  if (!recording_) return false;
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  uint32_t data = uint32_t(recPos_.load() * sizeof(float)), ch = uint32_t(recChannels_);
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(3); u16(uint16_t(ch)); u32(kAudioRate); u32(kAudioRate * ch * 4); u16(uint16_t(ch * 4)); u16(32);
  std::fwrite("data", 1, 4, f); u32(data);
  std::fwrite(rec_.data(), 1, data, f);
  std::fclose(f);
  return true;
}

void AudioEngine::callback(float *out, size_t frames) {
  lastCallbackNs_ = steadyNs();
  const int ch = channels_, stride = stride_;
  std::memset(out, 0, frames * size_t(stride) * sizeof(float));
  {
    std::lock_guard<std::mutex> lock(voicesMutex_);
    for (auto &v : voices_)
      if (!v->bypassMaster) v->mixInto(out, frames, stride, ch);
  }
  float m = master.load();
  for (size_t i = 0; i < frames; ++i) {
    float *f = &out[i * size_t(stride)];
    // Soft clip keeps several loud voices from wrapping around.
    for (int c = 0; c < ch; ++c) f[c] = std::tanh(f[c] * m);
  }
  {
    // Timecode goes out after the master: a master at zero must not cut it.
    std::lock_guard<std::mutex> lock(voicesMutex_);
    for (auto &v : voices_)
      if (v->bypassMaster) v->mixInto(out, frames, stride, ch);
  }
  if (recording_.load(std::memory_order_acquire) && stride == recChannels_) {
    size_t pos = recPos_, n = std::min(frames * size_t(stride), rec_.size() - pos);
    std::memcpy(rec_.data() + pos, out, n * sizeof(float));
    recPos_ = pos + n;
  }
  float pk[kMaxOutputChannels] = {};
  for (size_t i = 0; i < frames; ++i) {
    const float *f = &out[i * size_t(stride)];
    for (int c = 0; c < ch; ++c) pk[c] = std::max(pk[c], std::fabs(f[c]));
  }
  for (int c = 0; c < ch; ++c) peak_[c] = std::max(pk[c], peak_[c].load() * 0.92f);
}

// ------------------------------------------------------------------ decoder

AudioDecoder::~AudioDecoder() { close(); }

bool AudioDecoder::open(const std::string &path, std::shared_ptr<AudioVoice> voice, bool loop,
                        double loopLength, double videoStart, std::string *error) {
  close();
  path_ = path;
  voice_ = std::move(voice);
  loop_ = loop;
  loopLength_ = loopLength;
  videoStart_ = std::isfinite(videoStart) ? videoStart : 0.0;
  finished_ = false;
  running_ = true;
  thread_ = std::thread(&AudioDecoder::run, this);
  return true;
}

void AudioDecoder::close() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void AudioDecoder::seek(double time) {
  seekTo_ = std::max(0.0, time);
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

  // Keep the file's channels (up to 7.1): the voice routes them to the outputs.
  const int ch = std::clamp(codec->ch_layout.nb_channels, 1, kMaxVoiceChannels);
  AVChannelLayout outLayout;
  if (codec->ch_layout.nb_channels == ch && codec->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
    av_channel_layout_copy(&outLayout, &codec->ch_layout);
  else
    av_channel_layout_default(&outLayout, ch);
  voice_->setChannels(ch);
  if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate, &codec->ch_layout,
                          codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0 ||
      swr_init(swr) < 0) {
    finished_ = true;
    cleanup();
    return;
  }

  // Exact frame count of each loop: the video's loop length, with the
  // fraction of a sample carried from one loop to the next, so the sound never
  // drifts from the picture in installations that loop for days.
  const double loopSamples = loopLength_ > 0 ? loopLength_ * kAudioRate : 0;
  auto loopSize = [&](int64_t k) -> int64_t {
    if (loopSamples <= 0) return -1;
    return std::llround(double(k + 1) * loopSamples) - std::llround(double(k) * loopSamples);
  };
  int64_t loopIndex = 0, framesPerLoop = loopSize(0), loopFrames = 0;
  std::vector<float> buf;
  std::vector<float> pending; // converted audio not yet accepted by the voice
  size_t pendingPos = 0;
  bool eof = false;

  // After a seek: samples to drop (the audio starts before the target) or
  // silence to add (it starts after it), from the first decoded frame. Times
  // are on the video's clock: file time minus the video stream's start.
  int64_t skipFrames = 0, padFrames = 0;
  bool measureSkip = true; // also from the start of the file
  double seekInside = 0;
  auto seekInFile = [&](double inside) {
    double fileTime = inside + videoStart_;
    int64_t ts = av_rescale_q(int64_t(fileTime * AV_TIME_BASE), AV_TIME_BASE_Q, st->time_base);
    if (av_seek_frame(fmt, stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
      av_seek_frame(fmt, stream, st->start_time != AV_NOPTS_VALUE ? st->start_time : 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec);
    swr_init(swr);
    loopFrames = int64_t(std::llround(inside * kAudioRate));
    pending.clear();
    pendingPos = 0;
    eof = false;
    seekInside = inside;
    measureSkip = true;
    skipFrames = padFrames = 0;
  };
  auto rewind = [&] { seekInFile(0); };

  while (running_) {
    if (restart_.exchange(false)) {
      double target = seekTo_.load();
      double len = loopLength_ > 0 ? loopLength_ : 0;
      int64_t loops = len > 0 && loop_ ? int64_t(std::floor(target / len)) : 0;
      double inside = std::max(0.0, target - double(loops) * len);
      seekInFile(inside);
      loopIndex = loops;
      framesPerLoop = loopSize(loops);
      if (loopSamples > 0) // frames already played in this loop, on the exact loop grid
        loopFrames = std::max<int64_t>(0, std::llround(target * kAudioRate) - std::llround(double(loops) * loopSamples));
      voice_->reset(target);
    }
    // Feed what we already have first.
    if (pendingPos < pending.size()) {
      size_t frames = (pending.size() - pendingPos) / size_t(ch);
      size_t n = voice_->write(pending.data() + pendingPos, frames);
      pendingPos += n * size_t(ch);
      if (n < frames) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    pending.clear();
    pendingPos = 0;

    if (eof) {
      // Pad with silence up to the exact loop length, then loop or stop.
      if (framesPerLoop > 0 && loopFrames < framesPerLoop && loop_) {
        int64_t n = std::min<int64_t>(framesPerLoop - loopFrames, 2048);
        pending.assign(size_t(n) * size_t(ch), 0.0f);
        loopFrames += n;
        continue;
      }
      if (loop_) {
        framesPerLoop = loopSize(++loopIndex);
        rewind();
        continue;
      }
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
        buf.resize(size_t(out) * size_t(ch));
        uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
        int64_t got = std::max(0, swr_convert(swr, &dst, out, nullptr, 0));
        // Never past the loop's length (the resampler's tail made every loop
        // a little longer than the video's).
        if (framesPerLoop > 0 && loop_) got = std::min(got, std::max<int64_t>(0, framesPerLoop - loopFrames));
        if (got > 0) pending.assign(buf.begin(), buf.begin() + got * ch);
        loopFrames += got;
      }
      eof = true;
      continue;
    }
    if (r < 0) { eof = true; continue; }

    if (measureSkip) { // first frame after a seek: how far before (or after) the target it starts
      int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
      if (ts != AV_NOPTS_VALUE) {
        double at = double(ts) * av_q2d(st->time_base) - videoStart_;
        int64_t d = std::llround((seekInside - at) * kAudioRate);
        if (d > 0) skipFrames = d;
        else padFrames = std::min<int64_t>(-d, 30 * kAudioRate); // sound starts later than the picture
      }
      measureSkip = false;
    }
    int out = swr_get_out_samples(swr, frame->nb_samples);
    buf.resize(size_t(std::max(out, 1)) * size_t(ch));
    uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
    int got = swr_convert(swr, &dst, out, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    av_frame_unref(frame);
    if (got <= 0) continue;
    int64_t start = 0;
    if (skipFrames > 0) { // still before the seek target
      start = std::min<int64_t>(skipFrames, got);
      skipFrames -= start;
      if (start == got) continue;
    }
    int64_t keep = got - start, pad = padFrames;
    padFrames = 0;
    if (framesPerLoop > 0 && loop_) {
      int64_t room = std::max<int64_t>(0, framesPerLoop - loopFrames);
      pad = std::min(pad, room);
      keep = std::min(keep, room - pad);
    }
    if (keep <= 0 && pad <= 0) { eof = true; continue; } // audio longer than the video: cut
    pending.assign(size_t(pad) * size_t(ch), 0.0f);
    if (keep > 0) pending.insert(pending.end(), buf.begin() + start * ch, buf.begin() + (start + keep) * ch);
    loopFrames += pad + std::max<int64_t>(keep, 0);
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
