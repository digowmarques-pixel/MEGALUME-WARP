// Unit tests for the GPU-independent core: warp geometry, OSC, project files.
#include "control/midi.h"
#include "control/osc.h"
#include "media/hap.h"
#include "core/dmx.h"
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

  // Concave quad (a corner dragged inside): no perspective blow-up, corners
  // still exact, the map stays inside the quad's bounds and unmap inverts it.
  Warp cc;
  cc.corners = {Vec2{1100, 700}, Vec2{1920, 0}, Vec2{1920, 1080}, Vec2{0, 1080}};
  CHECK(!cc.perspectiveValid());
  CHECK(w.perspectiveValid());
  CHECK(near(cc.map(0, 0), cc.corners[0]) && near(cc.map(1, 1), cc.corners[2]));
  bool bounded = true;
  for (float u = 0; u <= 1.0f; u += 0.1f)
    for (float v = 0; v <= 1.0f; v += 0.1f) {
      Vec2 q = cc.map(u, v);
      bounded &= q.x >= -1 && q.x <= 1921 && q.y >= -1 && q.y <= 1081;
    }
  CHECK(bounded);
  CHECK(near(cc.unmapPerspective(cc.map(0.7f, 0.4f)), {0.7f, 0.4f}, 1e-2f));

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

static void testDmx() {
  // Straight strip of 171 RGB pixels from channel 1: pixel 171 goes to the next universe.
  LedLayoutParams p;
  p.cols = 171;
  p.rows = 1;
  p.universe = 0;
  auto slots = computeLedLayout(p);
  CHECK(slots.size() == 171);
  CHECK(slots[0].universe == 0 && slots[0].channel == 0 && slots[0].col == 0);
  CHECK(slots[169].universe == 0 && slots[169].channel == 507);
  CHECK(slots[170].universe == 1 && slots[170].channel == 0);

  // 3x2 zigzag matrix: second row runs backwards.
  p.cols = 3; p.rows = 2; p.zigzag = true;
  slots = computeLedLayout(p);
  CHECK(slots[3].col == 2 && slots[3].row == 1 && slots[5].col == 0 && slots[5].row == 1);
  // Flip X reverses the first row.
  p.zigzag = false; p.flipX = true;
  slots = computeLedLayout(p);
  CHECK(slots[0].col == 2 && slots[2].col == 0);
  // Vertical wiring walks down the first column first.
  p.flipX = false; p.vertical = true;
  slots = computeLedLayout(p);
  CHECK(slots[0].col == 0 && slots[0].row == 0 && slots[1].col == 0 && slots[1].row == 1 && slots[2].col == 1);
  // Start channel 510: an RGB pixel does not fit (510,511,512) -> fits exactly; 511 does not.
  LedLayoutParams q;
  q.cols = 2; q.channel = 510;
  slots = computeLedLayout(q);
  CHECK(slots[0].universe == 0 && slots[0].channel == 509 && slots[1].universe == 1 && slots[1].channel == 0);
  // 16-bit RGBW uses 8 channels per pixel.
  CHECK(ledChannelsPerPixel(LedFormat::RGBW, true) == 8 && ledChannelsPerPixel(LedFormat::Mono, false) == 1);

  DmxUniverse u{};
  writeLedPixel(u, 0, LedFormat::BGR, false, 1.0f, 0.5f, 0.0f);
  CHECK(u[0] == 0 && u[1] == 128 && u[2] == 255);
  writeLedPixel(u, 3, LedFormat::GRB, false, 1.0f, 0.0f, 0.0f);
  CHECK(u[3] == 0 && u[4] == 255 && u[5] == 0);
  writeLedPixel(u, 6, LedFormat::RGBW, false, 1.0f, 1.0f, 0.5f);
  CHECK(u[6] == 128 && u[7] == 128 && u[8] == 0 && u[9] == 128);
  writeLedPixel(u, 10, LedFormat::RGB, true, 1.0f, 0.0f, 0.0f);
  CHECK(u[10] == 255 && u[11] == 255 && u[12] == 0);

  // Art-Net ArtDmx header.
  u.fill(0);
  u[0] = 7;
  auto art = encodeArtDmx(0x123, 5, u);
  CHECK(art.size() == 18 + 512);
  CHECK(std::memcmp(art.data(), "Art-Net\0", 8) == 0);
  CHECK(art[8] == 0x00 && art[9] == 0x50 && art[11] == 14 && art[12] == 5);
  CHECK(art[14] == 0x23 && art[15] == 0x01 && art[16] == 0x02 && art[17] == 0x00 && art[18] == 7);

  // sACN E1.31 data packet.
  std::array<uint8_t, 16> cid{};
  cid[0] = 0xAB;
  auto sacn = encodeSacn(1, 9, u, cid, "Lumen");
  CHECK(sacn.size() == 638);
  CHECK(sacn[1] == 0x10 && std::memcmp(&sacn[4], "ASC-E1.17", 9) == 0);
  CHECK(((sacn[16] & 0x0F) << 8 | sacn[17]) == 622 && (sacn[16] & 0xF0) == 0x70);
  CHECK(((sacn[38] & 0x0F) << 8 | sacn[39]) == 600 && sacn[43] == 0x02 && sacn[22] == 0xAB);
  CHECK(sacn[108] == 100 && sacn[111] == 9 && sacn[113] == 0 && sacn[114] == 1);
  CHECK(((sacn[115] & 0x0F) << 8 | sacn[116]) == 523 && sacn[117] == 0x02 && sacn[118] == 0xA1);
  CHECK(sacn[123] == 0x02 && sacn[124] == 0x01 && sacn[125] == 0 && sacn[126] == 7);
  CHECK(sacnMulticastAddress(1) == "239.255.0.1" && sacnMulticastAddress(258) == "239.255.1.2");

  // Project round trip.
  Project pr = Project::makeDefault();
  pr.addDmxDevice(DmxProtocol::SACN).host = "10.0.0.5";
  LedMapping &a = pr.addLedMapping();
  a.cols = 170; a.format = LedFormat::GRB; a.zigzag = true;
  LedMapping &b = pr.addLedMapping(); // continues after the first: next universe
  CHECK(b.universe == 2 && b.channel == 1);
  Project back;
  CHECK(deserializeProject(serializeProject(pr, ""), "", back));
  CHECK(back.dmxDevices.size() == 1 && back.dmxDevices[0].protocol == DmxProtocol::SACN && back.dmxDevices[0].host == "10.0.0.5");
  CHECK(back.ledMappings.size() == 2 && back.ledMappings[0].cols == 170 && back.ledMappings[0].format == LedFormat::GRB);
  CHECK(back.ledMappings[0].zigzag && back.ledMappings[1].universe == 2);
}

static void testBlendLayout() {
  BlendLayoutParams p;
  p.cols = 3; p.rows = 1; p.projW = 1920; p.projH = 1080; p.overlapX = 0.15f;
  BlendLayout b = computeBlendLayout(p);
  CHECK(b.overlapPxX == 288 && b.canvasW == 3 * 1920 - 2 * 288 && b.canvasH == 1080);
  CHECK(b.projectors.size() == 3);
  CHECK(b.projectors[0].left == 0 && b.projectors[0].right > 0.149f && b.projectors[1].left > 0.149f);
  CHECK(b.projectors[2].right == 0 && b.projectors[1].srcX == 1920 - 288);
  // Neighbour regions overlap by exactly the blend width.
  CHECK(b.projectors[0].srcX + b.projectors[0].srcW - b.projectors[1].srcX == 288);
  // 2x2 grid has vertical blends too.
  p.cols = 2; p.rows = 2; p.overlapY = 0.1f;
  b = computeBlendLayout(p);
  CHECK(b.projectors.size() == 4 && b.projectors[0].bottom > 0 && b.projectors[3].top > 0 && b.projectors[3].bottom == 0);
  // The curve is complementary: two projectors always add to one.
  for (float t = 0; t <= 1.0f; t += 0.05f)
    for (float pw : {1.0f, 2.0f, 3.5f}) CHECK(std::fabs(blendCurve(t, pw) + blendCurve(1 - t, pw) - 1) < 1e-5f);
  // Applying it to a project.
  Project pr = Project::makeDefault();
  BlendLayoutParams q;
  q.cols = 2; q.rows = 1; q.overlapX = 0.2f;
  applyBlendLayout(pr, q, 1, true);
  CHECK(pr.outputs.size() == 2 && pr.outputs[1].monitor == 2 && pr.canvasW == 2 * 1920 - 384);
  CHECK(std::fabs(pr.layers[0].slices[0].warp.corners[1].x - float(pr.canvasW)) < 0.5f); // full-canvas slice rescaled
}

int main() {
  testBlendLayout();
  testDmx();
  testWarp();
  testOsc();
  testProject();
  testMidi();
  testSnappyHap();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
