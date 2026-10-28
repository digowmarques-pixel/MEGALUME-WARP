// System self-test (lumen --diagnose, or Help > System diagnostics): checks
// everything a show depends on and writes a report that can be sent as is.
#include "app.h"

#include "gl/gl.h"
#include "io/ndi.h"
#include "io/serial.h"
#include "io/spout.h"
#include "media/audio.h"
#include "media/decoder.h"
#include "media/upload.h"
#include "platform/platform.h"
#include "render/isf_renderer.h"
#include "render/stream.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace lumen {

namespace {

struct Report {
  std::string text;
  int warnings = 0, failures = 0;
  void section(const char *name) { text += std::string("\n== ") + name + " ==\n"; }
  void line(const std::string &s) { text += "   " + s + "\n"; }
  void ok(const std::string &s) { text += "[OK]    " + s + "\n"; }
  void warn(const std::string &s) { text += "[AVISO] " + s + "\n", ++warnings; }
  void fail(const std::string &s) { text += "[FALHA] " + s + "\n", ++failures; }
  void list(const std::vector<std::string> &items, const char *none) {
    if (items.empty()) line(none);
    for (auto &i : items) line("- " + i);
  }
};

// Time the render thread spends handing one frame to the GPU, direct vs PBO.
// The wall time includes glFinish, i.e. when the GPU really has the frame.
struct UploadTiming {
  double directMs = 0, asyncMs = 0, directWallMs = 0, asyncWallMs = 0;
};

UploadTiming timeUploads(int w, int h, DynamicMesh &mesh) {
  VideoFrame f;
  f.layout = FrameLayout::NV12;
  f.width = w;
  f.height = h;
  f.plane[0].assign(size_t(w) * size_t(h), 100);
  f.plane[1].assign(size_t(w) * size_t(h) / 2, 128);
  f.stride[0] = w;
  f.stride[1] = w;
  UploadTiming t;
  bool was = StreamUploader::enabled();
  for (int pass = 0; pass < 2; ++pass) {
    StreamUploader::setEnabled(pass == 1);
    FrameUploader up;
    up.upload(f, mesh); // allocation outside the timing
    glFinish();
    const int n = 20;
    double callMs = 0;
    auto w0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      f.plane[0][size_t(i)] = uint8_t(i); // a "new" frame each time
      auto c0 = std::chrono::steady_clock::now();
      up.upload(f, mesh);
      callMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
    }
    glFinish();
    double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count() / n;
    (pass ? t.asyncMs : t.directMs) = callMs / n;
    (pass ? t.asyncWallMs : t.directWallMs) = wall;
  }
  StreamUploader::setEnabled(was);
  return t;
}

std::string glString(GLenum e) {
  const char *s = reinterpret_cast<const char *>(glGetString(e));
  return s ? s : "?";
}

} // namespace

std::string App::diagnosticReport() {
  Report r;
  r.text = "Lumen " LUMEN_VERSION " — diagnóstico do sistema\n";
#ifdef _WIN32
  r.text += "Sistema: Windows\n";
#elif defined(__APPLE__)
  r.text += "Sistema: macOS\n";
#else
  r.text += "Sistema: Linux\n";
#endif
  r.text += "Programa: " + platform::executableDir() + "\n";
  r.text += "Configurações: " + platform::settingsDir() + "\n";

  r.section("Placa de vídeo");
  std::string renderer = glString(GL_RENDERER);
  r.line("GPU: " + renderer + " (" + glString(GL_VENDOR) + ")");
  r.line("OpenGL " + glString(GL_VERSION) + ", GLSL " + glString(GL_SHADING_LANGUAGE_VERSION));
  GLint maxTex = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
  if (maxTex >= 8192) r.ok("Textura máxima " + std::to_string(maxTex) + " px");
  else r.warn("Textura máxima só " + std::to_string(maxTex) + " px: canvases grandes não funcionam");
  std::string lower = renderer;
  for (auto &c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (lower.find("llvmpipe") != std::string::npos || lower.find("gdi generic") != std::string::npos ||
      lower.find("software") != std::string::npos || lower.find("basic render") != std::string::npos)
    r.fail("Renderização por software: instale o driver da placa de vídeo (NVIDIA, AMD ou Intel)");
  else r.ok("Aceleração de hardware");
  if (FrameUploader::supportsS3tc()) r.ok("HAP comprimido direto na GPU (S3TC)");
  else r.warn("A GPU não aceita S3TC: vídeos HAP serão decodificados na CPU");
  r.ok("Shaders do renderizador compilados");
  {
    UploadTiming t = timeUploads(3840, 2160, engine.mesh);
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Envio de quadro 4K para a GPU: direto %.1f ms, assíncrono (PBO) %.1f ms (completo: %.1f / %.1f ms)",
                  t.directMs, t.asyncMs, t.directWallMs, t.asyncWallMs);
    if (t.asyncMs < 8) r.ok(buf);
    else r.warn(std::string(buf) + " — lento para vários vídeos 4K: prefira HAP");
  }
  {
    // Asynchronous read back (NDI) must give the same pixels as glReadPixels:
    // a white strip on the top rows of a black 64×32 image.
    std::vector<unsigned char> img(64 * 32 * 4, 0);
    for (size_t i = 0; i < img.size(); ++i)
      if (i < 64 * 4 * 8 || i % 4 == 3) img[i] = 255;
    Texture src;
    src.upload(img.data(), 64, 32);
    Framebuffer fb;
    engine.generators.copyFlipped(src, fb, engine.mesh); // GL bottom-up
    std::vector<unsigned char> direct, async;
    fb.readPixels(direct);
    AsyncReadback rb;
    rb.request(fb);
    glFinish();
    int w = 0, h = 0;
    bool same = rb.collect(async, w, h) && w == 64 && h == 32 && async.size() == direct.size();
    for (int y = 0; same && y < h; ++y) // async is top-down, direct bottom-up
      same = std::equal(&async[size_t(y) * 256], &async[size_t(y) * 256] + 256, &direct[size_t(h - 1 - y) * 256]);
    same = same && async[0] == 255 && async[async.size() - 4] == 0;
    if (same) r.ok("Leitura assíncrona da GPU (envio NDI sem travar o render)");
    else r.fail("Leitura assíncrona da GPU deu pixels diferentes");
  }

  // Every built-in effect, generator and transition, compiled and drawn once.
  int bad = 0, n = 0;
  for (auto &e : engine.isfCatalog.builtins()) {
    IsfRenderer isf;
    std::string err;
    ++n;
    Texture src;
    std::vector<unsigned char> px(64 * 36 * 4, 128);
    src.upload(px.data(), 64, 36);
    IsfValues v;
    if (isf.load(e.path, &err)) {
      isfDefaults(isf.info(), v);
      v["progress"] = {0.5f};
      if (isf.render(64, 36, 1.0, {{"inputImage", &src}, {"startImage", &src}, {"endImage", &src}}, v, engine.mesh)) continue;
      err = "não desenhou";
    }
    ++bad;
    r.fail("Efeito " + e.name + ": " + err);
  }
  if (n == 0) r.fail("Pasta de efeitos não encontrada (effects ao lado do lumen.exe)");
  else if (bad == 0) r.ok(std::to_string(n) + " efeitos/transições ISF compilados");

  r.section("Monitores");
  auto mons = OutputManager::monitors();
  for (size_t i = 0; i < mons.size(); ++i)
    r.line(std::to_string(i + 1) + ": " + mons[i].name + "  " + std::to_string(mons[i].w) + "x" +
           std::to_string(mons[i].h) + " @" + std::to_string(mons[i].refresh) + "Hz");
  if (mons.size() < 2) r.warn("Só um monitor: ligue os projetores (Win+P → Estender) para as saídas em tela cheia");
  else r.ok(std::to_string(mons.size()) + " monitores");

  r.section("Vídeo (FFmpeg)");
  r.line(ffmpegInfo());
  auto caps = listCaptureDevices();
  std::vector<std::string> capNames;
  for (auto &c : caps) capNames.push_back(c.name + " (" + c.format + ")");
  r.line("Câmeras / placas de captura:");
  r.list(capNames, "  (nenhuma)");

  r.section("Áudio");
  auto outs = AudioEngine::devices();
  if (engine.audio.deviceName().empty()) r.warn("Nenhuma saída de áudio aberta: vídeos tocam sem som");
  else r.ok("Saída em uso: " + engine.audio.deviceName() + " (" + std::to_string(engine.audio.channels()) + " canais, " +
            std::to_string(engine.audio.latencyMs()) + " ms" + (engine.audio.exclusive() ? ", exclusivo" : "") + ")");
  r.list(outs, "  (nenhuma saída)");
  r.line("Entradas (LTC):");
  r.list(LtcCapture::devices(), "  (nenhuma entrada)");

  r.section("MIDI");
  auto midiIn = engine.midi().ports(), midiOut = engine.devices.midiOutPorts();
  r.line("Entradas:");
  r.list(midiIn, "  (nenhuma)");
  r.line("Saídas:");
  r.list(midiOut, "  (nenhuma)");

  r.section("Serial (Arduino)");
  r.list(serialPorts(), "  (nenhuma porta)");

  r.section("Spout / NDI");
#ifdef _WIN32
  if (spoutAvailable()) r.ok("Spout disponível");
  else r.warn("Spout indisponível (a GPU não compartilha texturas D3D11/OpenGL)");
#else
  r.line("Spout: só no Windows");
#endif
  if (ndiAvailable()) {
    r.ok("NDI Runtime carregado");
    ndiSources();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500)); // the finder needs a moment
    r.line("Fontes NDI na rede:");
    r.list(ndiSources(), "  (nenhuma encontrada)");
  } else {
    r.line("NDI: " + ndiStatus());
  }

  r.section("Arquivos");
  std::string probe = platform::toUtf8(platform::toPath(platform::settingsDir()) / "write-test.tmp");
  if (platform::writeFile(probe, "ok")) {
    std::error_code ec;
    std::filesystem::remove(platform::toPath(probe), ec);
    r.ok("Pasta de configurações gravável (autosave)");
  } else {
    r.fail("Não foi possível gravar em " + platform::settingsDir() + ": não haverá autosave");
  }

  char summary[160];
  std::snprintf(summary, sizeof(summary), "\nResultado: %d falha(s), %d aviso(s)\n", r.failures, r.warnings);
  r.text += summary;
  return r.text;
}

} // namespace lumen
