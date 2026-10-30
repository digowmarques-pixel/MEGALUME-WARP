#include "media/asio_out.h"

#if defined(LUMEN_ASIO) && defined(_WIN32)

#include <windows.h>
#include <objbase.h>

#include "asiosys.h" // platform defines; asio.h needs them first
#include "asio.h"
#include "asiodrivers.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

extern AsioDrivers *asioDrivers;
bool loadAsioDriver(char *name);

namespace lumen {

// ASIO has one driver per process: the callbacks reach the open output through this.
static std::atomic<AsioOutput::Impl *> gActive{nullptr};

struct AsioOutput::Impl {
  std::thread thread; // owns the driver: COM (single-threaded apartment) lives here
  std::mutex mutex;
  std::condition_variable wake;
  bool quit = false, panel = false;
  std::atomic<bool> running{false}, reset{false};
  Render render;
  int channels = 0, latencyMs = 0;
  long bufferSize = 0;
  bool postOutput = false;
  std::vector<ASIOBufferInfo> buffers;
  std::vector<ASIOSampleType> types;
  std::vector<float> mix; // interleaved, channels per frame

  void fill(long index) {
    const size_t frames = size_t(bufferSize);
    const int ch = channels;
    mix.assign(frames * size_t(ch), 0.0f);
    if (render) render(mix.data(), frames, ch);
    for (int c = 0; c < ch; ++c) {
      void *dst = buffers[size_t(c)].buffers[index];
      const float *src = mix.data() + c;
      auto at = [&](size_t i) { return std::clamp(src[i * size_t(ch)], -1.0f, 1.0f); };
      switch (types[size_t(c)]) {
      case ASIOSTFloat32LSB: {
        auto *d = static_cast<float *>(dst);
        for (size_t i = 0; i < frames; ++i) d[i] = at(i);
        break;
      }
      case ASIOSTFloat64LSB: {
        auto *d = static_cast<double *>(dst);
        for (size_t i = 0; i < frames; ++i) d[i] = at(i);
        break;
      }
      case ASIOSTInt16LSB: {
        auto *d = static_cast<int16_t *>(dst);
        for (size_t i = 0; i < frames; ++i) d[i] = int16_t(std::lrint(at(i) * 32767.0f));
        break;
      }
      case ASIOSTInt24LSB: { // packed, 3 bytes per sample
        auto *d = static_cast<uint8_t *>(dst);
        for (size_t i = 0; i < frames; ++i) {
          int32_t v = int32_t(std::lrint(double(at(i)) * 8388607.0));
          d[i * 3] = uint8_t(v);
          d[i * 3 + 1] = uint8_t(v >> 8);
          d[i * 3 + 2] = uint8_t(v >> 16);
        }
        break;
      }
      case ASIOSTInt32LSB: {
        auto *d = static_cast<int32_t *>(dst);
        for (size_t i = 0; i < frames; ++i) d[i] = int32_t(std::lrint(double(at(i)) * 2147483647.0));
        break;
      }
      // 32-bit containers holding fewer bits, aligned to the low bits.
      case ASIOSTInt32LSB16:
      case ASIOSTInt32LSB18:
      case ASIOSTInt32LSB20:
      case ASIOSTInt32LSB24: {
        const int bits = types[size_t(c)] == ASIOSTInt32LSB16 ? 16 : types[size_t(c)] == ASIOSTInt32LSB18 ? 18
                         : types[size_t(c)] == ASIOSTInt32LSB20 ? 20 : 24;
        const double full = double((1 << (bits - 1)) - 1);
        auto *d = static_cast<int32_t *>(dst);
        for (size_t i = 0; i < frames; ++i) d[i] = int32_t(std::lrint(double(at(i)) * full));
        break;
      }
      default: // big-endian formats (not used by Windows drivers): silence
        std::memset(dst, 0, frames * 4);
        break;
      }
    }
    if (postOutput) ASIOOutputReady();
  }
};

static void onBufferSwitch(long index, ASIOBool) {
  if (AsioOutput::Impl *a = gActive.load()) a->fill(index);
}

static ASIOTime *onBufferSwitchTimeInfo(ASIOTime *t, long index, ASIOBool direct) {
  onBufferSwitch(index, direct);
  return t;
}

static void onSampleRateChanged(ASIOSampleRate) {
  if (AsioOutput::Impl *a = gActive.load()) a->reset = true; // reopened at our rate
}

static long onMessage(long selector, long value, void *, double *) {
  switch (selector) {
  case kAsioSelectorSupported:
    return value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest ||
           value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo;
  case kAsioEngineVersion: return 2;
  case kAsioResetRequest: // the user changed the buffer size (or more) in the driver's panel
    if (AsioOutput::Impl *a = gActive.load()) a->reset = true;
    return 1;
  case kAsioResyncRequest:
  case kAsioLatenciesChanged:
  case kAsioSupportsTimeInfo: return 1;
  default: return 0;
  }
}

static ASIOCallbacks gCallbacks = {onBufferSwitch, onSampleRateChanged, onMessage, onBufferSwitchTimeInfo};

bool AsioOutput::available() { return true; }

std::vector<std::string> AsioOutput::drivers() {
  std::vector<std::string> out;
  // The driver list only reads the registry; COM is not needed for it.
  AsioDriverList list;
  char name[64][32];
  char *names[64];
  for (int i = 0; i < 64; ++i) names[i] = name[i];
  long n = list.asioGetNumDev();
  for (long i = 0; i < n && i < 64; ++i)
    if (list.asioGetDriverName(int(i), names[i], 32) == 0) out.emplace_back(names[i]);
  return out;
}

AsioOutput::AsioOutput() : impl_(new Impl) {}
AsioOutput::~AsioOutput() {
  stop();
  delete impl_;
}

bool AsioOutput::start(const std::string &driver, int sampleRate, int maxChannels, Render render,
                       std::string &error) {
  stop();
  Impl &a = *impl_;
  a.render = std::move(render);
  a.quit = a.panel = false;
  a.reset = false;
  std::promise<std::string> opened;
  auto result = opened.get_future();
  a.thread = std::thread([&a, driver, sampleRate, maxChannels, &opened] {
    CoInitialize(nullptr); // drivers are in-process COM objects for this apartment
    auto fail = [&](const std::string &why, bool init) {
      if (init) ASIOExit();
      if (asioDrivers) asioDrivers->removeCurrentDriver();
      opened.set_value(why);
      CoUninitialize();
    };
    if (!asioDrivers) asioDrivers = new AsioDrivers;
    std::vector<char> name(driver.begin(), driver.end());
    name.push_back(0);
    if (!loadAsioDriver(name.data())) return fail("driver ASIO \"" + driver + "\" não encontrado", false);
    ASIODriverInfo info{};
    info.asioVersion = 2;
    info.sysRef = GetDesktopWindow();
    if (ASIOInit(&info) != ASE_OK) return fail(std::string("o driver ASIO não iniciou: ") + info.errorMessage, false);
    if (ASIOCanSampleRate(sampleRate) != ASE_OK || ASIOSetSampleRate(sampleRate) != ASE_OK)
      return fail("o driver ASIO não aceita " + std::to_string(sampleRate) + " Hz", true);
    long ins = 0, outs = 0;
    ASIOGetChannels(&ins, &outs);
    long minSize = 0, maxSize = 0, pref = 0, gran = 0;
    ASIOGetBufferSize(&minSize, &maxSize, &pref, &gran);
    a.channels = int(std::clamp<long>(outs, 0, maxChannels));
    if (a.channels <= 0) return fail("o driver ASIO não tem saídas", true);
    a.bufferSize = pref > 0 ? pref : std::max(64L, minSize);
    a.postOutput = ASIOOutputReady() == ASE_OK;
    a.buffers.assign(size_t(a.channels), ASIOBufferInfo{});
    for (int c = 0; c < a.channels; ++c) {
      a.buffers[size_t(c)].isInput = ASIOFalse;
      a.buffers[size_t(c)].channelNum = c;
    }
    gActive = &a; // callbacks may come as soon as the buffers exist
    if (ASIOCreateBuffers(a.buffers.data(), a.channels, a.bufferSize, &gCallbacks) != ASE_OK) {
      gActive = nullptr;
      return fail("o driver ASIO recusou os buffers", true);
    }
    a.types.assign(size_t(a.channels), ASIOSTFloat32LSB);
    for (int c = 0; c < a.channels; ++c) {
      ASIOChannelInfo ci{};
      ci.channel = c;
      ci.isInput = ASIOFalse;
      if (ASIOGetChannelInfo(&ci) == ASE_OK) a.types[size_t(c)] = ci.type;
    }
    long inLat = 0, outLat = 0;
    if (ASIOGetLatencies(&inLat, &outLat) == ASE_OK) a.latencyMs = int(outLat * 1000 / std::max(1, sampleRate));
    else a.latencyMs = int(a.bufferSize * 1000 / std::max(1, sampleRate));
    if (ASIOStart() != ASE_OK) {
      gActive = nullptr;
      ASIODisposeBuffers();
      return fail("o driver ASIO não começou a tocar", true);
    }
    a.running = true;
    opened.set_value("");
    // The driver stays on this thread until stop(); its settings panel opens here too.
    std::unique_lock<std::mutex> lock(a.mutex);
    while (!a.quit) {
      a.wake.wait(lock, [&] { return a.quit || a.panel; });
      if (a.panel) {
        a.panel = false;
        lock.unlock();
        ASIOControlPanel();
        lock.lock();
      }
    }
    lock.unlock();
    ASIOStop();
    gActive = nullptr;
    ASIODisposeBuffers();
    ASIOExit();
    asioDrivers->removeCurrentDriver();
    a.running = false;
    CoUninitialize();
  });
  error = result.get();
  if (!error.empty()) {
    a.thread.join();
    return false;
  }
  return true;
}

void AsioOutput::stop() {
  Impl &a = *impl_;
  if (!a.thread.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    a.quit = true;
  }
  a.wake.notify_all();
  a.thread.join();
}

bool AsioOutput::running() const { return impl_->running; }
int AsioOutput::channels() const { return impl_->channels; }
int AsioOutput::latencyMs() const { return impl_->latencyMs; }
bool AsioOutput::needsReset() const { return impl_->reset; }

void AsioOutput::controlPanel() {
  Impl &a = *impl_;
  if (!a.running) return;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    a.panel = true;
  }
  a.wake.notify_all();
}

} // namespace lumen

#else // built without the ASIO SDK

namespace lumen {

struct AsioOutput::Impl {};

bool AsioOutput::available() { return false; }
std::vector<std::string> AsioOutput::drivers() { return {}; }
AsioOutput::AsioOutput() : impl_(nullptr) {}
AsioOutput::~AsioOutput() = default;
bool AsioOutput::start(const std::string &, int, int, Render, std::string &error) {
  error = "esta versão do Lumen foi gerada sem ASIO";
  return false;
}
void AsioOutput::stop() {}
bool AsioOutput::running() const { return false; }
int AsioOutput::channels() const { return 0; }
int AsioOutput::latencyMs() const { return 0; }
bool AsioOutput::needsReset() const { return false; }
void AsioOutput::controlPanel() {}

} // namespace lumen

#endif
