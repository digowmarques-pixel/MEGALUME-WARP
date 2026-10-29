#include "media/source.h"

#include "io/ndi.h"
#include "media/text.h"

#include "io/spout.h"

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
  ok &= copy_.build(kPixelVertexShader, R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() { fragColor = texture(uTex, vUV); }
)");
  // Cross-dissolve with premultiplied alpha, back to straight alpha.
  ok &= mix_.build(kPixelVertexShader, R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uA;
uniform sampler2D uB;
uniform float uT;
void main() {
  vec4 a = texture(uA, vUV), b = texture(uB, vUV);
  vec4 m = mix(vec4(a.rgb * a.a, a.a), vec4(b.rgb * b.a, b.a), uT);
  fragColor = m.a > 0.0 ? vec4(m.rgb / m.a, m.a) : vec4(0.0);
}
)");
  return ok;
}

void Generators::crossfade(const Texture &a, const Texture &b, float t, Framebuffer &fb, DynamicMesh &mesh) {
  int w = b.width(), h = b.height();
  fb.resize(w, h);
  fb.bind();
  glDisable(GL_BLEND);
  mix_.use();
  mix_.set("uTarget", float(w), float(h));
  mix_.set("uA", 0);
  mix_.set("uB", 1);
  mix_.set("uT", std::clamp(t, 0.0f, 1.0f));
  a.bind(0);
  b.bind(1);
  // Both images and the result keep row 0 = top.
  mesh.drawQuad(0, 0, float(w), float(h), 0, 1, 1, 0);
  Framebuffer::unbind();
  glActiveTexture(GL_TEXTURE0);
}

void Generators::place(const Texture &src, int W, int H, const std::array<float, 4> &r, Framebuffer &fb,
                       DynamicMesh &mesh) {
  fb.resize(W, H);
  fb.bind();
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  copy_.use();
  copy_.set("uTarget", float(W), float(H));
  copy_.set("uTex", 0);
  src.bind(0);
  // Same orientation as crossfade: the result keeps row 0 = top.
  mesh.drawQuad(r[0], r[1], r[0] + r[2], r[1] + r[3], 0, 1, 1, 0);
  Framebuffer::unbind();
}

void Generators::copyFlipped(const Texture &src, Framebuffer &fb, DynamicMesh &mesh) {
  fb.resize(src.width(), src.height());
  fb.bind();
  glDisable(GL_BLEND);
  copy_.use();
  copy_.set("uTarget", float(src.width()), float(src.height()));
  copy_.set("uTex", 0);
  src.bind(0);
  // Pixel y = h (stored in row 0) samples v = 1, the source's top row.
  mesh.drawQuad(0, 0, float(src.width()), float(src.height()), 0, 0, 1, 1);
  Framebuffer::unbind();
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

// ------------------------------------------------------------------ live

LiveInput::LiveInput(const MediaItem &m, const DecoderSettings &s) : type_(m.type), url_(m.path) {
  if (type_ == MediaType::Spout) {
    spout_ = std::make_unique<SpoutIn>(m.path);
    return;
  }
  if (type_ == MediaType::NDI) {
    ndi_ = std::make_unique<NdiReceiver>(m.path);
    return;
  }
  MediaSource src{m.path, m.liveFormat, m.liveOptions, true};
  std::string err;
  opened_ = decoder_.open(src, false, s, &err);
  if (!opened_) std::fprintf(stderr, "[live] %s: %s\n", m.path.c_str(), err.c_str());
}

LiveInput::~LiveInput() = default;

const Texture *LiveInput::update(uint64_t frame, DynamicMesh &mesh) {
  if (frame == lastFrame_) return tex_; // several layers share one input
  lastFrame_ = frame;
  if (spout_) {
    if (spout_->receive() || (spout_->connected() && spout_->texture())) {
      wrapped_.wrap(spout_->texture(), spout_->width(), spout_->height());
      tex_ = &wrapped_;
    }
    return tex_;
  }
  if (ndi_) {
    int w, h;
    if (ndi_->latest(ndiFrame_, w, h)) {
      ndiTex_.upload(ndiFrame_.data(), w, h);
      tex_ = &ndiTex_;
    }
    return tex_;
  }
  if (decoder_.fetch(0, frame_, true)) tex_ = uploader_.upload(frame_, mesh);
  return tex_;
}

std::string LiveInput::status() const {
  if (spout_) return spout_->connected() ? "Spout: " + spout_->senderName() : "Spout: aguardando sender";
  if (ndi_) return !ndiAvailable() ? ndiStatus() : ndi_->connected() ? "NDI conectado" : "NDI: procurando a fonte";
  if (!opened_) return "não abriu";
  return tex_ ? "ao vivo" : "aguardando sinal";
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
  if (m.type != MediaType::Video && m.type != MediaType::Image) return nullptr;
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
  e.still->upload(f.rgba().data(), f.width, f.height);
  return e.still.get();
}

std::shared_ptr<LiveInput> MediaCache::live(const MediaItem &m, const DecoderSettings &s) {
  Entry &e = entry(m);
  if (!e.live) e.live = std::make_shared<LiveInput>(m, s);
  return e.live;
}

void MediaCache::setLiveTexture(int id, const Texture *t) {
  auto it = entries_.find(id);
  if (it != entries_.end()) it->second.liveTex = t;
}

void MediaCache::pruneLive() {
  for (auto &[id, e] : entries_)
    if (e.live && e.live.use_count() == 1) {
      e.live.reset(); // nobody shows it: release the camera
      e.liveTex = nullptr;
    }
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
  if (m.type == MediaType::Live || m.type == MediaType::Spout || m.type == MediaType::NDI) return e.liveTex;
  if (m.type == MediaType::Text) {
    if (!e.text) e.text = std::make_unique<TextRenderer>();
    return e.text->render(m);
  }
  if (m.type == MediaType::Canvas) return nullptr; // shown by name
  if (m.type == MediaType::Shader) {
    if (!e.isf) e.isf = std::make_unique<IsfRenderer>();
    if (!e.isf->sync(m.path)) return nullptr;
    IsfValues v = m.shaderParams;
    isfDefaults(e.isf->info(), v);
    int th = std::max(16, int(192.0f * float(m.height) / float(std::max(1, m.width))));
    const Texture *t = e.isf->render(192, th, 1.0, {}, v, mesh);
    if (!t) return nullptr;
    if (!e.thumbFb) e.thumbFb = std::make_unique<Framebuffer>();
    gen.copyFlipped(*t, *e.thumbFb, mesh);
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
      std::copy_n(&f.rgba()[size_t((sy * f.width + sx) * 4)], 4, &small[size_t((y * tw + x) * 4)]);
    }
  e.thumb = std::make_unique<Texture>();
  e.thumb->upload(small.data(), tw, th);
  return e.thumb.get();
}

void MediaCache::invalidate(int id) { entries_.erase(id); }
void MediaCache::clear() { entries_.clear(); }

// ------------------------------------------------------------------ playback

static bool audibleSpeed(float s);

Playback::Playback(const MediaItem &m, const Cell &cell, MediaContext &ctx)
    : speed(cell.speed), loop(cell.loop), mediaId_(m.id), type_(m.type), audio_(ctx.audio) {
  if (type_ == MediaType::Live || type_ == MediaType::Spout || type_ == MediaType::NDI) {
    live_ = ctx.cache.live(m, ctx.decoder);
    return;
  }
  if (type_ != MediaType::Video) return;
  decoder_ = std::make_unique<VideoDecoder>();
  std::string err;
  if (!decoder_->open(MediaSource{m.path, "", "", false}, loop, ctx.decoder, &err)) {
    std::fprintf(stderr, "[playback] %s: %s\n", m.path.c_str(), err.c_str());
    decoder_.reset();
    return;
  }
  // Sound: at any speed the card can follow (like a tape: the pitch follows the speed).
  if (audio_ && audio_->running() && decoder_->info().hasAudio && audibleSpeed(speed)) {
    voice_ = audio_->createVoice();
    voice_->gain = 0; // the engine sets the real gain on the first update
    voice_->rate = speed;
    audioDecoder_ = std::make_unique<AudioDecoder>();
    audioDecoder_->open(m.path, voice_, loop, decoder_->info().duration, decoder_->info().startTime);
    audioClock_ = true;
    audioGen_ = audio_->generation();
  }
}

Playback::~Playback() {
  audioDecoder_.reset();
  if (voice_ && audio_) audio_->removeVoice(voice_);
}

void Playback::setGain(float g) {
  if (voice_) voice_->gain = g;
}

void Playback::setRoute(int firstOutput, const std::vector<AudioPatch> &matrix) {
  if (voice_) voice_->setRoute(firstOutput, matrix);
}

void Playback::restart() { seek(0); }

void Playback::seek(double time) {
  time = std::max(0.0, time);
  clock_ = time;
  // hasFrame_ stays: the last picture remains until the decoder reaches the
  // new time (otherwise the layer vanishes for the 0.2-2 s of the seek).
  if (decoder_) decoder_->seek(time);
  if (voice_) voice_->reset(time); // the audio clock must jump immediately
  if (audioDecoder_) {
    audioDecoder_->seek(time);
    audioClock_ = voice_ && voice_->active;
    if (audioClock_) { // an ended sound track plays again from here
      audioOff_ = AudioOff::No;
      lastPlayed_ = -1;
      stallFor_ = 0;
    }
  }
}

// Sound follows speeds from 0.25x to 4x (resampled, like a tape); outside that
// the picture plays alone.
static bool audibleSpeed(float s) { return s >= 0.25f && s <= 4.0f; }

// The sound card paces the picture while the clip plays at normal speed. A
// speed change, or a card that stopped taking sound (unplugged, driver), hands
// the picture to the frame clock; the sound comes back in step when the cause
// goes away.
void Playback::watchAudio(double dt) {
  if (audioClock_) {
    voice_->rate = speed;
    if (!audibleSpeed(speed)) { // too fast / slow for the sound: the picture goes on alone
      voice_->active = false;
      audioClock_ = false;
      audioOff_ = AudioOff::Speed;
      return;
    }
    if (paused) { // held on purpose: the clock stops, which is not a stalled card
      lastPlayed_ = -1;
      stallFor_ = 0;
      return;
    }
    if (audioDecoder_->finished() && voice_->starving()) { // the sound track ended (or never started)
      audioClock_ = false;
      audioOff_ = AudioOff::Ended;
      return;
    }
    // Sound waiting in the voice that the card does not take: a stalled device.
    double a = voice_->playedSeconds();
    if (a != lastPlayed_ || voice_->buffered() == 0) {
      lastPlayed_ = a;
      stallFor_ = 0;
    } else if ((stallFor_ += dt) > 0.3) {
      voice_->active = false;
      audioClock_ = false;
      audioOff_ = AudioOff::Stall;
      audioGen_ = audio_ ? audio_->generation() : 0;
    }
    return;
  }
  bool back = audibleSpeed(speed) && (audioOff_ == AudioOff::Speed ||
                                (audioOff_ == AudioOff::Stall && audio_ && audio_->generation() != audioGen_ && !audio_->stalled()));
  if (!back) return;
  voice_->reset(clock_); // sound again from where the picture is now
  audioDecoder_->seek(clock_);
  voice_->active = true;
  audioClock_ = true;
  audioOff_ = AudioOff::No;
  lastPlayed_ = -1;
  stallFor_ = 0;
}

bool Playback::finished() {
  if (loop) return false;
  if (decoder_) return decoder_->finished();
  return false;
}

double Playback::duration() const {
  return decoder_ ? decoder_->info().duration : 0.0;
}

const Texture *Playback::update(double dt, const MediaItem &m, MediaContext &ctx) {
  switch (type_) {
  case MediaType::Image:
    if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
    started_ = true;
    return ctx.cache.still(m);
  case MediaType::Generator:
    if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
    started_ = true;
    if (!genFb_) genFb_ = std::make_unique<Framebuffer>();
    ctx.generators.render(m, clock_, *genFb_, ctx.mesh);
    return &genFb_->texture();
  case MediaType::Shader: {
    if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
    started_ = true;
    if (!isf_) isf_ = std::make_unique<IsfRenderer>();
    if (!isf_->sync(m.path)) return nullptr;
    IsfValues v = m.shaderParams;
    isfDefaults(isf_->info(), v);
    const Texture *t = isf_->render(std::clamp(m.width, 16, 8192), std::clamp(m.height, 16, 8192), clock_, {}, v, ctx.mesh);
    if (!t) return nullptr;
    if (!genFb_) genFb_ = std::make_unique<Framebuffer>();
    ctx.generators.copyFlipped(*t, *genFb_, ctx.mesh); // ISF is GL-native: back to row 0 = top
    return &genFb_->texture();
  }
  case MediaType::Canvas: {
    if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
    started_ = true;
    const Texture *src = ctx.canvasTexture ? ctx.canvasTexture(m.canvasRef) : nullptr;
    if (!src || src->empty()) return nullptr;
    if (!genFb_) genFb_ = std::make_unique<Framebuffer>();
    ctx.generators.copyFlipped(*src, *genFb_, ctx.mesh);
    return &genFb_->texture();
  }
  case MediaType::Text: {
    if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
    started_ = true;
    if (!text_) text_ = std::make_unique<TextRenderer>();
    return text_->render(m);
  }
  case MediaType::Live:
  case MediaType::NDI:
  case MediaType::Spout: {
    clock_ += dt;
    const Texture *t = live_ ? live_->update(ctx.frame, ctx.mesh) : nullptr;
    ctx.cache.setLiveTexture(mediaId_, t);
    return t;
  }
  case MediaType::Video: {
    if (!decoder_) return nullptr;
    decoder_->setLoop(loop);
    if (audioDecoder_) audioDecoder_->setLoop(loop);
    if (voice_) {
      voice_->hold = paused;
      watchAudio(dt);
    }
    if (audioClock_) {
      clock_ = std::max(clock_, voice_->playedSeconds());
    } else {
      if (started_) clock_ += dt * (paused ? 0.0 : double(speed));
      started_ = true;
    }
    if (decoder_->fetch(clock_, frame_, !hasFrame_)) {
      videoTex_ = uploader_.upload(frame_, ctx.mesh);
      hasFrame_ = videoTex_ != nullptr;
      // If decoding fell far behind (e.g. after a stall), resync the clock.
      if (!audioClock_ && clock_ - frame_.pts > 0.5) clock_ = frame_.pts;
    }
    return hasFrame_ ? videoTex_ : nullptr;
  }
  }
  return nullptr;
}

} // namespace lumen
