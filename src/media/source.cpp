#include "media/source.h"

#include <algorithm>
#include <cstdio>

namespace lumen {

// ------------------------------------------------------------------ generators

static const char *kGenHeader = R"(#version 330 core
in vec2 vUV;              // (0,0) = top-left of the generated image
out vec4 fragColor;
uniform float uTime;
uniform vec2 uRes;
uniform vec4 uColor1;
uniform vec4 uColor2;
uniform float uP1;
uniform float uP2;
const float PI = 3.14159265;
)";

static const char *kGenBodies[] = {
    // Test card: grid, circles, diagonals, colour bars, moving marker.
    R"(
float line(float d, float w) { return 1.0 - smoothstep(0.0, w, abs(d)); }
void main() {
  vec2 px = vUV * uRes;
  float cell = uRes.y / 9.0;
  vec2 g = mod(px + cell * 0.5, cell) - cell * 0.5;
  float grid = max(line(g.x, 1.5), line(g.y, 1.5));
  vec3 col = mix(vec3(0.08), vec3(0.18), mod(floor(px.x / cell) + floor(px.y / cell), 2.0));
  col = mix(col, vec3(0.85), grid);
  vec2 c = px - uRes * 0.5;
  float r = length(c);
  float R = uRes.y * 0.45;
  col = mix(col, vec3(1.0), line(r - R, 2.0));
  col = mix(col, vec3(1.0), line(r - R * 0.5, 2.0));
  col = mix(col, vec3(1.0, 0.8, 0.0), line(c.x, 2.0) * step(r, R));
  col = mix(col, vec3(1.0, 0.8, 0.0), line(c.y, 2.0) * step(r, R));
  vec2 n = vUV * 2.0 - 1.0;
  col = mix(col, vec3(0.3, 0.8, 1.0), max(line(n.x * uRes.x - n.y * uRes.x, 3.0), line(n.x * uRes.x + n.y * uRes.x, 3.0)) * 0.6);
  // colour bars in the middle band
  if (abs(c.y) < cell * 0.5 && abs(c.x) < R * 0.8) {
    float i = floor((c.x / (R * 1.6) + 0.5) * 8.0);
    vec3 bars[8] = vec3[8](vec3(1), vec3(1,1,0), vec3(0,1,1), vec3(0,1,0), vec3(1,0,1), vec3(1,0,0), vec3(0,0,1), vec3(0));
    col = bars[int(clamp(i, 0.0, 7.0))];
  }
  // border
  float b = min(min(px.x, uRes.x - px.x), min(px.y, uRes.y - px.y));
  col = mix(col, vec3(1.0, 0.2, 0.2), 1.0 - smoothstep(4.0, 6.0, b));
  // moving dot proves the output is live
  vec2 dotPos = uRes * 0.5 + vec2(cos(uTime), sin(uTime)) * R * 0.75;
  col = mix(col, vec3(1.0, 0.3, 0.3), 1.0 - smoothstep(10.0, 12.0, length(px - dotPos)));
  fragColor = vec4(col, 1.0);
}
)",
    // Solid colour.
    R"(
void main() { fragColor = vec4(uColor1.rgb, 1.0); }
)",
    // Linear gradient, uP1 = angle in degrees.
    R"(
void main() {
  float a = radians(uP1);
  vec2 d = vec2(cos(a), sin(a));
  vec2 p = (vUV - 0.5) * vec2(uRes.x / uRes.y, 1.0);
  float t = clamp(dot(p, d) / (0.5 * (abs(d.x) * uRes.x / uRes.y + abs(d.y))) * 0.5 + 0.5, 0.0, 1.0);
  fragColor = vec4(mix(uColor1.rgb, uColor2.rgb, t), 1.0);
}
)",
    // Grid, uP1 = cell size px, uP2 = line width px.
    R"(
void main() {
  vec2 px = vUV * uRes;
  vec2 g = abs(mod(px + uP1 * 0.5, uP1) - uP1 * 0.5);
  float l = 1.0 - smoothstep(uP2 * 0.5, uP2 * 0.5 + 1.0, min(g.x, g.y));
  fragColor = vec4(mix(uColor2.rgb, uColor1.rgb, l), 1.0);
}
)",
    // Plasma, uP1 = speed, uP2 = scale.
    R"(
void main() {
  vec2 p = (vUV - 0.5) * vec2(uRes.x / uRes.y, 1.0) * uP2;
  float t = uTime * uP1;
  float v = sin(p.x * 2.0 + t) + sin(p.y * 3.0 - t * 1.3) + sin((p.x + p.y) * 1.7 + t * 0.7)
          + sin(length(p + vec2(sin(t * 0.3), cos(t * 0.4))) * 3.0);
  float k = 0.5 + 0.5 * sin(v * PI * 0.5);
  vec3 col = mix(uColor1.rgb, uColor2.rgb, k) * (0.6 + 0.4 * sin(v * 2.0 + t));
  fragColor = vec4(col, 1.0);
}
)",
    // Moving stripes, uP1 = speed, uP2 = count.
    R"(
void main() {
  float s = fract(vUV.x * uP2 - uTime * uP1);
  float m = smoothstep(0.48, 0.5, s) - smoothstep(0.98, 1.0, s);
  fragColor = vec4(mix(uColor2.rgb, uColor1.rgb, m), 1.0);
}
)",
};

bool Generators::init() {
  bool ok = true;
  for (int i = 0; i < int(GeneratorKind::COUNT); ++i) {
    std::string fs = std::string(kGenHeader) + kGenBodies[i];
    ok &= shaders_[i].build(kPixelVertexShader, fs.c_str());
  }
  return ok;
}

void Generators::render(const MediaItem &m, double time, Framebuffer &fb, DynamicMesh &mesh) {
  int w = std::clamp(m.width, 16, 8192), h = std::clamp(m.height, 16, 8192);
  fb.resize(w, h);
  fb.bind();
  glDisable(GL_BLEND);
  const Shader &s = shaders_[int(m.generator)];
  s.use();
  s.set("uTarget", float(w), float(h));
  s.set("uRes", float(w), float(h));
  s.set("uTime", float(time));
  s.set("uColor1", m.color1.r, m.color1.g, m.color1.b, m.color1.a);
  s.set("uColor2", m.color2.r, m.color2.g, m.color2.b, m.color2.a);
  s.set("uP1", m.param1);
  s.set("uP2", m.param2);
  // Flip V so that the texture follows the "row 0 = top" convention of decoded images.
  mesh.drawQuad(0, 0, float(w), float(h), 0, 1, 1, 0);
  Framebuffer::unbind();
}

// ------------------------------------------------------------------ cache

MediaCache::Entry &MediaCache::entry(const MediaItem &m) {
  Entry &e = entries_[m.id];
  if (e.path != m.path) { // media relinked: drop stale data
    e = Entry{};
    e.path = m.path;
  }
  return e;
}

const MediaInfo *MediaCache::info(const MediaItem &m) {
  if (m.type == MediaType::Generator) return nullptr;
  Entry &e = entry(m);
  if (!e.probed) {
    e.probed = true;
    std::string err;
    if (!probeMedia(m.path, e.info, &err)) {
      e.failed = true;
      std::fprintf(stderr, "[media] %s: %s\n", m.path.c_str(), err.c_str());
    }
  }
  return e.failed ? nullptr : &e.info;
}

const Texture *MediaCache::still(const MediaItem &m) {
  Entry &e = entry(m);
  if (e.still) return e.still.get();
  if (e.failed) return nullptr;
  VideoFrame f;
  std::string err;
  if (!decodeStill(m.path, f, &err)) {
    std::fprintf(stderr, "[media] %s: %s\n", m.path.c_str(), err.c_str());
    e.failed = true;
    return nullptr;
  }
  e.still = std::make_unique<Texture>();
  e.still->upload(f.rgba.data(), f.width, f.height);
  return e.still.get();
}

const Texture *MediaCache::thumbnail(const MediaItem &m, Generators &gen, DynamicMesh &mesh) {
  Entry &e = entry(m);
  if (m.type == MediaType::Generator) {
    // Re-rendered every call so edits to the generator show up immediately.
    if (!e.thumbFb) e.thumbFb = std::make_unique<Framebuffer>();
    MediaItem small = m;
    small.width = 192;
    small.height = std::max(16, int(192.0f * float(m.height) / float(std::max(1, m.width))));
    gen.render(small, 1.0, *e.thumbFb, mesh);
    return &e.thumbFb->texture();
  }
  if (e.thumb) return e.thumb.get();
  if (e.failed) return nullptr;
  VideoFrame f;
  if (!decodeStill(m.path, f)) {
    e.failed = true;
    return nullptr;
  }
  // Downscale on the CPU (nearest) to keep thumbnails cheap.
  int tw = 192, th = std::max(1, int(192.0f * float(f.height) / float(std::max(1, f.width))));
  std::vector<uint8_t> small(size_t(tw * th * 4));
  for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x) {
      int sx = x * f.width / tw, sy = y * f.height / th;
      std::copy_n(&f.rgba[size_t((sy * f.width + sx) * 4)], 4, &small[size_t((y * tw + x) * 4)]);
    }
  e.thumb = std::make_unique<Texture>();
  e.thumb->upload(small.data(), tw, th);
  return e.thumb.get();
}

void MediaCache::invalidate(int id) { entries_.erase(id); }
void MediaCache::clear() { entries_.clear(); }

// ------------------------------------------------------------------ playback

Playback::Playback(const MediaItem &m, const Cell &cell)
    : speed(cell.speed), loop(cell.loop), mediaId_(m.id), type_(m.type) {
  if (type_ == MediaType::Video) {
    decoder_ = std::make_unique<VideoDecoder>();
    std::string err;
    if (!decoder_->open(m.path, loop, &err)) {
      std::fprintf(stderr, "[playback] %s: %s\n", m.path.c_str(), err.c_str());
      decoder_.reset();
    }
  }
}

void Playback::restart() {
  clock_ = 0;
  hasFrame_ = false;
  if (decoder_) decoder_->restart();
}

bool Playback::finished() {
  if (loop) return false;
  if (decoder_) return decoder_->finished();
  return false;
}

double Playback::duration() const {
  return decoder_ ? decoder_->info().duration : 0.0;
}

const Texture *Playback::update(double dt, const MediaItem &m, MediaCache &cache,
                                Generators &gen, DynamicMesh &mesh) {
  if (started_) clock_ += dt * double(speed);
  started_ = true;
  switch (type_) {
  case MediaType::Image: return cache.still(m);
  case MediaType::Generator:
    if (!genFb_) genFb_ = std::make_unique<Framebuffer>();
    gen.render(m, clock_, *genFb_, mesh);
    return &genFb_->texture();
  case MediaType::Video:
    if (!decoder_) return nullptr;
    decoder_->setLoop(loop);
    if (decoder_->fetch(clock_, frame_, !hasFrame_)) {
      videoTex_.upload(frame_.rgba.data(), frame_.width, frame_.height);
      hasFrame_ = true;
      // If decoding fell far behind (e.g. after a stall), resync the clock.
      if (clock_ - frame_.pts > 0.5) clock_ = frame_.pts;
    }
    return hasFrame_ ? &videoTex_ : nullptr;
  }
  return nullptr;
}

} // namespace lumen
