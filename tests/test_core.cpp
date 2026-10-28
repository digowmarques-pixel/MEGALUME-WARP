// Unit tests for the GPU-independent core: warp geometry, OSC, project files.
#include "control/midi.h"
#include "control/osc.h"
#include "media/hap.h"
#include "core/project.h"
#include "core/warp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace lumen;

static int g_failures = 0, g_checks = 0;

#define CHECK(cond)                                                                    \
  do {                                                                                 \
    ++g_checks;                                                                        \
    if (!(cond)) {                                                                     \
      ++g_failures;                                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                                  \
  } while (0)

static bool near(Vec2 a, Vec2 b, float eps = 1e-3f) { return (a - b).length() < eps; }

static void testWarp() {
  Warp w;
  w.corners = {Vec2{100, 50}, Vec2{900, 120}, Vec2{1000, 700}, Vec2{50, 600}};
  // Corners of the unit square land exactly on the quad corners.
  CHECK(near(w.map(0, 0), w.corners[0]));
  CHECK(near(w.map(1, 0), w.corners[1]));
  CHECK(near(w.map(1, 1), w.corners[2]));
  CHECK(near(w.map(0, 1), w.corners[3]));
  // unmap is the inverse of map.
  for (float u : {0.1f, 0.5f, 0.83f})
    for (float v : {0.2f, 0.5f, 0.97f}) CHECK(near(w.unmapPerspective(w.map(u, v)), {u, v}, 1e-3f));

  // An identity mesh of any density does not change the mapping.
  Warp m = w;
  m.resetMesh(5, 4);
  CHECK(m.meshIsIdentity());
  CHECK(near(m.map(0.3f, 0.6f), w.map(0.3f, 0.6f), 0.05f));
  // Moving a mesh point moves the surface through it (interpolating spline).
  m.meshAt(2, 1) = {0.5f, 0.2f};
  CHECK(!m.meshIsIdentity());
  CHECK(near(m.map(0.5f, 1.0f / 3.0f), m.meshPointDest(2, 1), 0.5f));

  // Tessellation produces whole triangles of (x, y, u, v).
  std::vector<float> verts;
  w.tessellate(8, 0, 0, 1, 1, verts);
  CHECK(verts.size() == size_t(8 * 8 * 6 * 4));

  // Transform helpers.
  Warp r;
  r.setRect({0, 0}, {100, 100});
  r.rotateAround({50, 50}, 3.14159265f / 2);
  CHECK(near(r.corners[0], {100, 0}, 1e-3f));
  r.translate({10, 0});
  CHECK(near(r.center(), {60, 50}, 1e-3f));

  CHECK(pointInPolygon({5, 5}, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
  CHECK(!pointInPolygon({15, 5}, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
}

static void testOsc() {
  OscMessage m;
  m.address = "/lumen/layer/1/opacity";
  m.args = {0.25f, int32_t(7), std::string("abc"), true};
  auto bytes = encodeOsc(m);
  CHECK(bytes.size() % 4 == 0);
  std::vector<OscMessage> out;
  CHECK(parseOsc(bytes.data(), bytes.size(), out));
  CHECK(out.size() == 1);
  if (out.size() == 1) {
    CHECK(out[0].address == m.address);
    CHECK(out[0].args.size() == 4);
    CHECK(std::fabs(out[0].number(0) - 0.25f) < 1e-6f);
    CHECK(out[0].number(1) == 7.0f);
    CHECK(std::get<std::string>(out[0].args[2]) == "abc");
    CHECK(out[0].number(3) == 1.0f);
  }

  // Bundle with two messages.
  OscMessage a{"/a", {1.0f}, ""}, b{"/b", {int32_t(2)}, ""};
  auto ea = encodeOsc(a), eb = encodeOsc(b);
  std::vector<uint8_t> bundle = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
  auto put = [&](const std::vector<uint8_t> &x) {
    uint32_t n = uint32_t(x.size());
    for (int s = 24; s >= 0; s -= 8) bundle.push_back(uint8_t(n >> s));
    bundle.insert(bundle.end(), x.begin(), x.end());
  };
  put(ea);
  put(eb);
  out.clear();
  CHECK(parseOsc(bundle.data(), bundle.size(), out));
  CHECK(out.size() == 2 && out[0].address == "/a" && out[1].address == "/b");

  // Garbage is rejected without crashing.
  uint8_t junk[] = {'/', 'x', 0, 0, ',', 'f', 0, 0, 1};
  out.clear();
  CHECK(!parseOsc(junk, sizeof(junk), out));

  // Address patterns.
  CHECK(oscMatch("/sensor/*", "/sensor/dist"));
  CHECK(!oscMatch("/sensor/*", "/sensor/a/b"));
  CHECK(oscMatch("/pad/[1-4]", "/pad/3"));
  CHECK(!oscMatch("/pad/[1-4]", "/pad/5"));
  CHECK(oscMatch("/{foo,bar}/x", "/bar/x"));
  CHECK(oscMatch("/a?c", "/abc"));
  CHECK(!oscMatch("/a", "/a/b"));
}

static void testProject() {
  Project p = Project::makeDefault();
  MediaItem &g = p.addGenerator(GeneratorKind::Plasma);
  int gid = g.id;
  p.layers[0].slices[0].warp.resetMesh(3, 3);
  p.layers[0].slices[0].warp.meshAt(1, 1) = {0.4f, 0.6f};
  Mask mask;
  mask.points = {{1, 2}, {3, 4}, {5, 6}};
  mask.feather = 12;
  p.layers[0].masks.push_back(mask);
  p.columns[0].cells[p.layers[0].id].mediaId = gid;
  p.outputs[0].softEdge.right = 0.15f;
  InputMapping im;
  im.address = "/sensor/x";
  im.target = MappingTarget::PositionX;
  p.mappings.push_back(im);

  std::string json = serializeProject(p, "");
  Project q;
  std::string err;
  CHECK(deserializeProject(json, "", q, &err));
  CHECK(q.layers.size() == 1);
  CHECK(q.library.size() == 1 && q.library[0].generator == GeneratorKind::Plasma);
  CHECK(q.layers[0].slices[0].warp.meshCols == 3);
  CHECK(near(q.layers[0].slices[0].warp.meshAt(1, 1), {0.4f, 0.6f}));
  CHECK(q.layers[0].masks.size() == 1 && q.layers[0].masks[0].feather == 12);
  CHECK(q.columns[0].cells[q.layers[0].id].mediaId == gid);
  CHECK(std::fabs(q.outputs[0].softEdge.right - 0.15f) < 1e-6f);
  CHECK(q.mappings.size() == 1 && q.mappings[0].target == MappingTarget::PositionX);
  CHECK(q.nextId > gid); // ids never collide after loading
  // Serialising again is stable.
  CHECK(serializeProject(q, "") == json);

  CHECK(!deserializeProject("{ not json", "", q, &err));
  CHECK(isImagePath("foto.JPG"));
  CHECK(isMediaPath("show.mov") && !isMediaPath("notes.txt"));

  // Removing a layer also clears its cells.
  int lid = p.layers[0].id;
  p.removeLayer(lid);
  CHECK(p.layers.empty() && p.columns[0].cells.count(lid) == 0);
}

static void testMidi() {
  OscMessage m;
  unsigned char cc[] = {0xB2, 7, 100}; // CC 7 on channel 3
  CHECK(midiToOsc(cc, 3, m) && m.address == "/midi/cc/3/7" && m.number(0) == 100);
  unsigned char on[] = {0x90, 60, 127};
  CHECK(midiToOsc(on, 3, m) && m.address == "/midi/note/1/60" && m.number(0) == 127);
  unsigned char off[] = {0x80, 60, 64};
  CHECK(midiToOsc(off, 3, m) && m.number(0) == 0); // note off always reads as 0
  unsigned char bend[] = {0xE0, 0x00, 0x40};
  CHECK(midiToOsc(bend, 3, m) && m.address == "/midi/bend/1" && m.number(0) == 8192);
  unsigned char clock[] = {0xF8};
  CHECK(!midiToOsc(clock, 1, m));
  CHECK(oscMatch("/midi/cc/*/7", "/midi/cc/16/7"));
}

static void testSnappyHap() {
  // Snappy: literal "abcd" then a copy of 8 bytes at offset 4 -> "abcdabcdabcd".
  const uint8_t snap[] = {12, (3 << 2) | 0, 'a', 'b', 'c', 'd', uint8_t(((8 - 4) << 2) | 1), 4};
  std::vector<uint8_t> out;
  CHECK(snappyDecompress(snap, sizeof(snap), out));
  CHECK(std::string(out.begin(), out.end()) == "abcdabcdabcd");
  const uint8_t bad[] = {12, (3 << 2) | 0, 'a', 'b', 'c', 'd', uint8_t((4 << 2) | 1), 9};
  CHECK(!snappyDecompress(bad, sizeof(bad), out)); // offset before start of output

  // HAP frame: uncompressed DXT1 (type 0xAB), 8 bytes = one 4x4 block.
  std::vector<uint8_t> frame = {8, 0, 0, 0xAB, 1, 2, 3, 4, 5, 6, 7, 8};
  HapFrame hf;
  CHECK(decodeHapFrame(frame.data(), frame.size(), hf));
  CHECK(hf.format == HapFormat::DXT1 && hf.color.size() == 8 && hf.color[7] == 8);
  // Same block, snappy-compressed (type 0xBB).
  std::vector<uint8_t> sframe = {10, 0, 0, 0xBB, 8, (7 << 2), 1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(decodeHapFrame(sframe.data(), sframe.size(), hf) && hf.color.size() == 8 && hf.color[0] == 1);
  // Truncated frames are rejected.
  CHECK(!decodeHapFrame(frame.data(), 6, hf));
}

int main() {
  testWarp();
  testOsc();
  testProject();
  testMidi();
  testSnappyHap();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
