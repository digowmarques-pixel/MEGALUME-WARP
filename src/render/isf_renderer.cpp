#include "render/isf_renderer.h"

#include "media/decoder.h"
#include "platform/platform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace lumen {

bool IsfRenderer::load(const std::string &path, std::string *error) {
  std::string src;
  if (!platform::readFile(path, src)) {
    if (error) *error = "não foi possível ler " + path;
    return false;
  }
  path_ = path;
  return loadSource(src, error);
}

bool IsfRenderer::loadSource(const std::string &source, std::string *error) {
  if (!parseIsf(source, info_, error)) return false;
  std::string fs = isfFragmentSource(info_);
  std::string err;
  if (!shader_.build(kIsfVertexShader, fs.c_str(), &err)) {
    if (error) *error = "erro no shader: " + err;
    return false;
  }
  targets_.clear();
  frame_ = 0;
  // Images the shader brings with it ("IMPORTED"), next to the .fs file.
  imported_.clear();
  for (auto &[name, file] : info_.imported) {
    VideoFrame f;
    std::string full = platform::toUtf8(platform::toPath(path_).parent_path() / platform::toPath(file));
    if (!decodeStill(full, f)) {
      std::fprintf(stderr, "[isf] imagem %s não encontrada\n", full.c_str());
      continue;
    }
    // GL-native: row 0 = bottom.
    std::vector<uint8_t> flipped(f.rgba().size());
    size_t row = size_t(f.width) * 4;
    for (int y = 0; y < f.height; ++y)
      std::copy_n(&f.rgba()[size_t(y) * row], row, &flipped[size_t(f.height - 1 - y) * row]);
    auto t = std::make_unique<Texture>();
    t->upload(flipped.data(), f.width, f.height);
    imported_[name] = std::move(t);
  }
  return true;
}

const Texture *IsfRenderer::render(int w, int h, double time, const std::map<std::string, const Texture *> &images,
                                   const IsfValues &values, DynamicMesh &mesh) {
  if (!shader_.valid()) return nullptr;
  w = std::clamp(w, 1, 8192);
  h = std::clamp(h, 1, 8192);
  shader_.use();
  std::map<std::string, float> vars{{"WIDTH", float(w)}, {"HEIGHT", float(h)}};

  // Parameters.
  for (auto &in : info_.inputs) {
    GLint loc = shader_.loc(in.name.c_str());
    if (loc < 0) continue;
    auto it = values.find(in.name);
    std::vector<float> v = it != values.end() ? it->second : in.def;
    v.resize(std::max<size_t>(v.size(), 4), 0.0f);
    if (in.type == "float") { glUniform1f(loc, v[0]); vars[in.name] = v[0]; }
    else if (in.type == "bool" || in.type == "event" || in.type == "long") glUniform1i(loc, int(std::lround(v[0])));
    else if (in.type == "color") glUniform4f(loc, v[0], v[1], v[2], v[3]);
    else if (in.type == "point2D") glUniform2f(loc, v[0], v[1]);
  }
  double dt = lastTime_ < 0 ? 0.0 : time - lastTime_;
  lastTime_ = time;
  shader_.set("TIME", float(time));
  shader_.set("TIMEDELTA", float(std::max(0.0, dt)));
  shader_.set("FRAMEINDEX", frame_++);
  {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    shader_.set("DATE", float(lt.tm_year + 1900), float(lt.tm_mon + 1), float(lt.tm_mday),
                float(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec));
  }

  // Image inputs on the first units, then the pass targets.
  int unit = 0;
  for (auto &in : info_.inputs) {
    if (in.type != "image") continue;
    auto it = images.find(in.name);
    GLint loc = shader_.loc(in.name.c_str());
    if (loc < 0) continue;
    if (it != images.end() && it->second) it->second->bind(unit);
    else { glActiveTexture(GLenum(GL_TEXTURE0 + unit)); glBindTexture(GL_TEXTURE_2D, 0); }
    glUniform1i(loc, unit++);
  }
  for (auto &[name, tex] : imported_) {
    GLint loc = shader_.loc(name.c_str());
    if (loc < 0) continue;
    tex->bind(unit);
    glUniform1i(loc, unit++);
  }
  // _<image>_imgRect / _imgSize for shaders that use them.
  auto imgInfo = [&](const std::string &name, const Texture *t) {
    if (!t) return;
    shader_.set(("_" + name + "_imgRect").c_str(), 0.0f, 0.0f, 1.0f, 1.0f);
    shader_.set(("_" + name + "_imgSize").c_str(), float(t->width()), float(t->height()));
    shader_.set(("_" + name + "_flip").c_str(), 0);
  };
  for (auto &[name, t] : images) imgInfo(name, t);
  for (auto &[name, t] : imported_) imgInfo(name, t.get());
  int targetBase = unit;

  glDisable(GL_BLEND);
  const Texture *result = nullptr;
  for (size_t pi = 0; pi < info_.passes.size(); ++pi) {
    const IsfPass &pass = info_.passes[pi];
    bool last = pi + 1 == info_.passes.size();
    int pw = w, ph = h;
    if (!pass.width.empty()) pw = std::max(1, int(isfEval(pass.width, vars)));
    if (!pass.height.empty()) ph = std::max(1, int(isfEval(pass.height, vars)));
    // Every target is readable by name; a pass writes the other buffer of
    // its target so it can read its own previous frame (feedback).
    unit = targetBase;
    for (auto &[name, t] : targets_) {
      GLint loc = shader_.loc(name.c_str());
      if (loc < 0 || !t.fb[t.current]) continue;
      t.fb[t.current]->texture().bind(unit);
      glUniform1i(loc, unit++);
      imgInfo(name, &t.fb[t.current]->texture());
    }
    Framebuffer *fb = &out_;
    Target *tgt = nullptr;
    int write = 0;
    if (!pass.target.empty()) {
      tgt = &targets_[pass.target];
      write = tgt->fb[tgt->current] ? 1 - tgt->current : tgt->current;
      auto &f = tgt->fb[write];
      if (!f) f = std::make_unique<Framebuffer>();
      bool fresh = f->width() != pw || f->height() != ph;
      f->resize(pw, ph, false, pass.floatTarget ? GL_RGBA16F : GL_RGBA8);
      fb = f.get();
      if (fresh) { // new buffers start transparent black
        fb->bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
      }
    } else {
      out_.resize(pw, ph);
    }
    fb->bind();
    shader_.use();
    shader_.set("PASSINDEX", int(pi));
    shader_.set("RENDERSIZE", float(pw), float(ph));
    shader_.set("uTarget", float(pw), float(ph));
    // Pixel space is Y down; v = 1 at the top keeps row 0 = bottom (GL-native).
    mesh.drawQuad(0, 0, float(pw), float(ph), 0, 1, 1, 0);
    if (tgt) {
      tgt->current = write;
      if (last) result = &fb->texture();
    } else {
      result = &out_.texture();
    }
  }
  Framebuffer::unbind();
  glActiveTexture(GL_TEXTURE0);
  return result;
}

static long long fileTime(const std::string &path) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(platform::toPath(path), ec);
  return ec ? 0 : (long long)t.time_since_epoch().count();
}

bool IsfRenderer::sync(const std::string &path) {
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (path == path_ && now - checkedAt_ < 1.0) return valid();
  checkedAt_ = now;
  long long t = fileTime(path);
  if (path == path_ && t == mtime_) return valid();
  mtime_ = t;
  IsfRenderer fresh;
  std::string err;
  if (fresh.load(path, &err)) {
    info_ = fresh.info_;
    shader_.swap(fresh.shader_);
    targets_.clear();
    frame_ = 0;
    error_.clear();
  } else {
    error_ = err;
    std::fprintf(stderr, "[isf] %s: %s\n", path.c_str(), err.c_str());
    if (path != path_) { info_ = {}; Shader none; shader_.swap(none); }
  }
  path_ = path;
  return valid();
}

} // namespace lumen
