#include "io/ndi.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace lumen {

namespace {

// The NDI SDK's C structures (layout of Processing.NDI.Lib.h, v5/v6).
struct NdiSource {
  const char *name;
  const char *url;
};
struct NdiFindCreate {
  bool showLocal;
  const char *groups;
  const char *extraIps;
};
struct NdiRecvCreate {
  NdiSource source;
  int colorFormat; // 2 = RGBX_RGBA
  int bandwidth;   // 100 = highest
  bool allowFields;
  const char *recvName;
};
struct NdiVideoFrame {
  int xres, yres;
  int fourcc;
  int fpsN, fpsD;
  float aspect;
  int frameFormat; // 1 = progressive
  int64_t timecode;
  uint8_t *data;
  int stride;
  const char *metadata;
  int64_t timestamp;
};
struct NdiSendCreate {
  const char *name;
  const char *groups;
  bool clockVideo, clockAudio;
};

constexpr int fourcc(char a, char b, char c, char d) {
  return int(uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24);
}
constexpr int kRGBA = fourcc('R', 'G', 'B', 'A'), kRGBX = fourcc('R', 'G', 'B', 'X');
constexpr int kBGRA = fourcc('B', 'G', 'R', 'A'), kBGRX = fourcc('B', 'G', 'R', 'X');

struct Api {
  bool (*initialize)() = nullptr;
  void *(*findCreate)(const NdiFindCreate *) = nullptr;
  void (*findDestroy)(void *) = nullptr;
  const NdiSource *(*findSources)(void *, uint32_t *) = nullptr;
  void *(*recvCreate)(const NdiRecvCreate *) = nullptr;
  void (*recvDestroy)(void *) = nullptr;
  int (*recvCapture)(void *, NdiVideoFrame *, void *, void *, uint32_t) = nullptr;
  void (*recvFreeVideo)(void *, const NdiVideoFrame *) = nullptr;
  void *(*sendCreate)(const NdiSendCreate *) = nullptr;
  void (*sendDestroy)(void *) = nullptr;
  void (*sendVideo)(void *, const NdiVideoFrame *) = nullptr;
  void (*sendVideoAsync)(void *, const NdiVideoFrame *) = nullptr; // optional
  bool ok = false;
  std::string status = "NDI não carregado";
};

Api &api() {
  static Api a = [] {
    Api x;
    void *lib = nullptr;
    std::vector<std::string> names;
#ifdef _WIN32
    for (const char *env : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"})
      if (const char *d = std::getenv(env)) names.push_back(std::string(d) + "\\Processing.NDI.Lib.x64.dll");
    names.push_back("Processing.NDI.Lib.x64.dll");
    for (auto &n : names)
      if (!lib) lib = reinterpret_cast<void *>(LoadLibraryA(n.c_str()));
    auto sym = [&](const char *s) { return lib ? reinterpret_cast<void *>(GetProcAddress(HMODULE(lib), s)) : nullptr; };
#else
    names = {"libndi.so.6", "libndi.so.5", "libndi.so", "/usr/local/lib/libndi.dylib", "libndi.dylib"};
    for (auto &n : names)
      if (!lib) lib = dlopen(n.c_str(), RTLD_NOW);
    auto sym = [&](const char *s) { return lib ? dlsym(lib, s) : nullptr; };
#endif
    if (!lib) {
      x.status = "runtime NDI não encontrado (instale o NDI Tools / NDI Runtime)";
      return x;
    }
    x.initialize = reinterpret_cast<bool (*)()>(sym("NDIlib_initialize"));
    x.findCreate = reinterpret_cast<void *(*)(const NdiFindCreate *)>(sym("NDIlib_find_create_v2"));
    x.findDestroy = reinterpret_cast<void (*)(void *)>(sym("NDIlib_find_destroy"));
    x.findSources = reinterpret_cast<const NdiSource *(*)(void *, uint32_t *)>(sym("NDIlib_find_get_current_sources"));
    x.recvCreate = reinterpret_cast<void *(*)(const NdiRecvCreate *)>(sym("NDIlib_recv_create_v3"));
    x.recvDestroy = reinterpret_cast<void (*)(void *)>(sym("NDIlib_recv_destroy"));
    x.recvCapture = reinterpret_cast<int (*)(void *, NdiVideoFrame *, void *, void *, uint32_t)>(sym("NDIlib_recv_capture_v2"));
    x.recvFreeVideo = reinterpret_cast<void (*)(void *, const NdiVideoFrame *)>(sym("NDIlib_recv_free_video_v2"));
    x.sendCreate = reinterpret_cast<void *(*)(const NdiSendCreate *)>(sym("NDIlib_send_create"));
    x.sendDestroy = reinterpret_cast<void (*)(void *)>(sym("NDIlib_send_destroy"));
    x.sendVideo = reinterpret_cast<void (*)(void *, const NdiVideoFrame *)>(sym("NDIlib_send_send_video_v2"));
    // Optional: the asynchronous send converts and compresses on NDI's own thread.
    x.sendVideoAsync = reinterpret_cast<void (*)(void *, const NdiVideoFrame *)>(sym("NDIlib_send_send_video_async_v2"));
    bool all = x.initialize && x.findCreate && x.findDestroy && x.findSources && x.recvCreate && x.recvDestroy &&
               x.recvCapture && x.recvFreeVideo && x.sendCreate && x.sendDestroy && x.sendVideo;
    if (!all) {
      x.status = "runtime NDI incompleto";
      return x;
    }
    if (!x.initialize()) {
      x.status = "o NDI não iniciou (processador sem suporte?)";
      return x;
    }
    x.ok = true;
    x.status = "NDI pronto";
    return x;
  }();
  return a;
}

// One finder for the whole program, polled in the background.
struct Finder {
  std::mutex mutex;
  std::vector<std::string> names;
  std::thread thread;
  std::atomic<bool> running{true};
  Finder() {
    thread = std::thread([this] {
      Api &a = api();
      if (!a.ok) return;
      NdiFindCreate fc{true, nullptr, nullptr};
      void *f = a.findCreate(&fc);
      while (running) {
        uint32_t n = 0;
        const NdiSource *src = f ? a.findSources(f, &n) : nullptr;
        std::vector<std::string> list;
        for (uint32_t i = 0; i < n; ++i)
          if (src[i].name) list.push_back(src[i].name);
        {
          std::lock_guard<std::mutex> lock(mutex);
          names = list;
        }
        for (int k = 0; k < 20 && running; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (f) a.findDestroy(f);
    });
  }
  ~Finder() {
    running = false;
    if (thread.joinable()) thread.join();
  }
};

} // namespace

bool ndiAvailable() { return api().ok; }
std::string ndiStatus() { return api().status; }

std::vector<std::string> ndiSources() {
  if (!api().ok) return {};
  static Finder finder;
  std::lock_guard<std::mutex> lock(finder.mutex);
  return finder.names;
}

NdiReceiver::NdiReceiver(const std::string &source) : source_(source) {
  if (api().ok) thread_ = std::thread(&NdiReceiver::run, this);
}

NdiReceiver::~NdiReceiver() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void NdiReceiver::run() {
  Api &a = api();
  NdiRecvCreate rc{};
  rc.source = {source_.c_str(), nullptr};
  rc.colorFormat = 2; // RGBX / RGBA
  rc.bandwidth = 100;
  rc.allowFields = false;
  rc.recvName = "Lumen";
  void *recv = a.recvCreate(&rc);
  if (!recv) return;
  while (running_) {
    NdiVideoFrame v{};
    int type = a.recvCapture(recv, &v, nullptr, nullptr, 100);
    if (type != 1) continue; // 1 = video
    connected_ = true;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      w_ = v.xres;
      h_ = v.yres;
      frame_.resize(size_t(w_) * size_t(h_) * 4);
      bool bgr = v.fourcc == kBGRA || v.fourcc == kBGRX;
      bool opaque = v.fourcc == kRGBX || v.fourcc == kBGRX;
      for (int y = 0; y < h_; ++y) {
        const uint8_t *src = v.data + size_t(y) * size_t(v.stride);
        uint8_t *dst = &frame_[size_t(y) * size_t(w_) * 4];
        if (!bgr && !opaque) { std::memcpy(dst, src, size_t(w_) * 4); continue; }
        for (int x = 0; x < w_; ++x) {
          dst[x * 4 + 0] = src[x * 4 + (bgr ? 2 : 0)];
          dst[x * 4 + 1] = src[x * 4 + 1];
          dst[x * 4 + 2] = src[x * 4 + (bgr ? 0 : 2)];
          dst[x * 4 + 3] = opaque ? 255 : src[x * 4 + 3];
        }
      }
      fresh_ = true;
    }
    a.recvFreeVideo(recv, &v);
  }
  a.recvDestroy(recv);
}

bool NdiReceiver::latest(std::vector<uint8_t> &rgba, int &w, int &h) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!fresh_) return false;
  fresh_ = false;
  rgba = frame_;
  w = w_;
  h = h_;
  return true;
}

NdiSender::~NdiSender() {
  if (inst_ && api().ok) {
    if (api().sendVideoAsync) api().sendVideoAsync(inst_, nullptr); // waits until NDI is done with our buffer
    api().sendDestroy(inst_);
  }
}

bool NdiSender::send(const std::string &name, const uint8_t *rgba, int w, int h, int fpsN, int fpsD) {
  Api &a = api();
  if (!a.ok) return false;
  if (!inst_ || name != name_) {
    if (inst_) {
      if (a.sendVideoAsync) a.sendVideoAsync(inst_, nullptr);
      a.sendDestroy(inst_);
    }
    NdiSendCreate sc{name.c_str(), nullptr, false, false};
    inst_ = a.sendCreate(&sc);
    name_ = name;
  }
  if (!inst_) return false;
  NdiVideoFrame v{};
  v.xres = w;
  v.yres = h;
  v.fourcc = kRGBA;
  v.fpsN = fpsN;
  v.fpsD = fpsD;
  v.aspect = float(w) / float(std::max(1, h));
  v.frameFormat = 1;
  v.timecode = INT64_MAX; // synthesize
  v.stride = w * 4;
  if (a.sendVideoAsync) {
    // Asynchronous: NDI keeps reading the buffer until the next call, so the
    // frames alternate between two copies of our own. The render thread only
    // copies; the colour conversion and compression run on NDI's thread.
    std::vector<uint8_t> &b = buf_[cur_];
    cur_ ^= 1;
    b.assign(rgba, rgba + size_t(w) * size_t(h) * 4);
    v.data = b.data();
    a.sendVideoAsync(inst_, &v);
    return true;
  }
  v.data = const_cast<uint8_t *>(rgba);
  a.sendVideo(inst_, &v); // synchronous: the data may be reused afterwards
  return true;
}

} // namespace lumen
