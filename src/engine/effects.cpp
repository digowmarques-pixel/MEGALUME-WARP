#include "engine/effects.h"

#include "platform/platform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace lumen {

static double nowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static long long mtimeOf(const std::string &path) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(platform::toPath(path), ec);
  return ec ? 0 : (long long)t.time_since_epoch().count();
}

const IsfInfo &IsfCatalog::blobTrackerInfo() {
  static IsfInfo info = [] {
    IsfInfo i;
    parseIsf(R"isf(/*{
  "DESCRIPTION": "Rastreia regiões claras (ou o que mudou em relação ao fundo gravado) e envia /blob/<layer>/... para os mapeamentos",
  "INPUTS": [
    {"NAME": "threshold", "LABEL": "Limiar", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1},
    {"NAME": "invert", "LABEL": "Objetos escuros sobre fundo claro", "TYPE": "bool", "DEFAULT": false},
    {"NAME": "minArea", "LABEL": "Tamanho mínimo", "TYPE": "float", "DEFAULT": 0.002, "MIN": 0, "MAX": 0.1},
    {"NAME": "maxArea", "LABEL": "Tamanho máximo", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.01, "MAX": 1},
    {"NAME": "maxBlobs", "LABEL": "Máximo de blobs", "TYPE": "long", "DEFAULT": 4, "VALUES": [1, 2, 3, 4, 6, 8, 12, 16], "LABELS": ["1", "2", "3", "4", "6", "8", "12", "16"]},
    {"NAME": "merge", "LABEL": "Juntar blobs a menos de", "TYPE": "float", "DEFAULT": 0.04, "MIN": 0, "MAX": 0.3},
    {"NAME": "smoothing", "LABEL": "Estabilizar", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 0.95},
    {"NAME": "resolution", "LABEL": "Resolução da análise", "TYPE": "long", "DEFAULT": 160, "VALUES": [80, 160, 320], "LABELS": ["80 px (rápido)", "160 px", "320 px (preciso)"]},
    {"NAME": "view", "LABEL": "Mostrar", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1], "LABELS": ["Imagem original", "Limiar (preto e branco)"]},
    {"NAME": "sendOsc", "LABEL": "Enviar também às saídas OSC", "TYPE": "bool", "DEFAULT": false}
  ]
}*/
void main() { gl_FragColor = vec4(0.0); }
)isf", i);
    return i;
  }();
  return info;
}

const std::vector<IsfCatalog::Entry> &IsfCatalog::builtins() {
  if (scanned_) return builtins_;
  scanned_ = true;
  builtins_.clear();
  namespace fs = std::filesystem;
  std::error_code ec;
  for (auto &f : fs::directory_iterator(platform::toPath(builtinEffectsDir()), ec)) {
    if (!f.is_regular_file(ec) || f.path().extension() != ".fs") continue;
    Entry e;
    e.path = platform::toUtf8(f.path());
    e.name = platform::toUtf8(f.path().stem());
    std::string src;
    if (!platform::readFile(e.path, src) || !parseIsf(src, e.info, &e.error)) continue;
    builtins_.push_back(e);
  }
  std::sort(builtins_.begin(), builtins_.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
  return builtins_;
}

const IsfInfo *IsfCatalog::info(const std::string &path) {
  if (path == kBlobTrackerId) return &blobTrackerInfo();
  Cached &c = cache_[path];
  double now = nowSeconds();
  if (now - c.checkedAt > 1.0) {
    c.checkedAt = now;
    long long t = mtimeOf(path);
    if (t != c.mtime) {
      c.mtime = t;
      std::string src;
      c.ok = platform::readFile(path, src) && parseIsf(src, c.info, &c.error);
      if (!c.ok && c.error.empty()) c.error = "arquivo não encontrado";
    }
  }
  return c.ok ? &c.info : nullptr;
}

std::string IsfCatalog::error(const std::string &path) {
  info(path);
  return cache_[path].error;
}

static const char *kLumaFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
  float l = dot(texture(uTex, vUV).rgb, vec3(0.2126, 0.7152, 0.0722));
  fragColor = vec4(l, l, l, 1.0);
}
)";

bool EffectTools::init() { return luma.build(kPixelVertexShader, kLumaFS); }

static float param(const IsfValues &v, const char *name, float def) {
  auto it = v.find(name);
  return it == v.end() || it->second.empty() ? def : it->second[0];
}

const BlobTracker *EffectChain::tracker() const {
  for (auto &b : blob_)
    if (b) return &b->tracker;
  return nullptr;
}

const Texture *EffectChain::apply(const std::vector<Effect> &effects, const Texture *src, double time,
                                  EffectTools &tools, DynamicMesh &mesh, BlobReport *report, bool learn) {
  if (!src || src->empty()) return src;
  bool any = std::any_of(effects.begin(), effects.end(), [](const Effect &e) { return e.enabled; });
  if (!any) return src;
  isf_.resize(effects.size());
  blob_.resize(effects.size());
  int w = src->width(), h = src->height();

  // ISF works with GL-native textures (row 0 = bottom).
  tools.generators->copyFlipped(*src, in_, mesh);
  const Texture *cur = &in_.texture();
  for (size_t i = 0; i < effects.size(); ++i) {
    const Effect &e = effects[i];
    if (!e.enabled) continue;
    if (e.isBlobTracker()) {
      auto &b = blob_[i];
      if (!b) b = std::make_unique<BlobState>();
      int dw = std::clamp(int(param(e.params, "resolution", 160)), 32, 640);
      int dh = std::max(8, int(std::lround(float(dw) * float(h) / float(std::max(1, w)))));
      b->small.resize(dw, dh);
      b->small.bind();
      glDisable(GL_BLEND);
      tools.luma.use();
      tools.luma.set("uTarget", float(dw), float(dh));
      tools.luma.set("uTex", 0);
      cur->bind(0);
      mesh.drawQuad(0, 0, float(dw), float(dh), 0, 1, 1, 0); // stays bottom-up
      b->small.readPixels(b->rgba);
      Framebuffer::unbind();
      b->luma.resize(size_t(dw) * size_t(dh));
      for (int y = 0; y < dh; ++y) // rows come bottom-up: the tracker wants row 0 = top
        for (int x = 0; x < dw; ++x) b->luma[size_t(y * dw + x)] = b->rgba[(size_t(dh - 1 - y) * size_t(dw) + size_t(x)) * 4];
      if (learn) b->tracker.setBackground(b->luma.data(), dw, dh);
      BlobParams bp;
      bp.threshold = param(e.params, "threshold", 0.5f);
      bp.invert = param(e.params, "invert", 0) > 0.5f;
      bp.minArea = param(e.params, "minArea", 0.002f);
      bp.maxArea = param(e.params, "maxArea", 0.5f);
      bp.maxBlobs = int(param(e.params, "maxBlobs", 4));
      bp.mergeDistance = param(e.params, "merge", 0.04f);
      bp.smoothing = param(e.params, "smoothing", 0.5f);
      const auto &found = b->tracker.process(b->luma.data(), dw, dh, bp);
      if (report) {
        report->blobs = found;
        report->sendOsc = param(e.params, "sendOsc", 0) > 0.5f;
      }
      if (int(param(e.params, "view", 0)) == 1) { // show the black & white image
        const auto &mask = b->tracker.mask();
        std::vector<unsigned char> img(mask.size() * 4);
        for (size_t k = 0; k < mask.size(); ++k) {
          img[k * 4] = img[k * 4 + 1] = img[k * 4 + 2] = mask[k];
          img[k * 4 + 3] = 255;
        }
        b->maskTex.upload(img.data(), dw, dh);
        tools.generators->copyFlipped(b->maskTex, b->view, mesh); // row 0 = top -> GL-native
        cur = &b->view.texture();
      }
      continue;
    }
    auto &r = isf_[i];
    if (!r) r = std::make_unique<IsfRenderer>();
    if (!r->sync(e.path) || !r->info().filter) continue;
    IsfValues values = e.params;
    isfDefaults(r->info(), values);
    if (const Texture *t = r->render(w, h, time, {{"inputImage", cur}}, values, mesh)) cur = t;
  }
  tools.generators->copyFlipped(*cur, out_, mesh);
  return &out_.texture();
}

} // namespace lumen
