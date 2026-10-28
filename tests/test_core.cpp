// Unit tests for the GPU-independent core: warp geometry, OSC, project files.
#include "control/midi.h"
#include "control/osc.h"
#include "media/hap.h"
#include "core/dmx.h"
#include "core/project.h"
#include "core/svg.h"
#include "core/warp.h"

#include <algorithm>
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
  // Per-seam pixel overlaps (Millumin-style soft edge fields).
  BlendLayoutParams g;
  g.cols = 3; g.rows = 1; g.gapsX = {200, 300};
  b = computeBlendLayout(g);
  CHECK(b.canvasW == 3 * 1920 - 500 && b.projectors[1].srcX == 1720 && b.projectors[2].srcX == 1720 + 1620);
  CHECK(std::fabs(b.projectors[1].left * 1920 - 200) < 0.01f && std::fabs(b.projectors[1].right * 1920 - 300) < 0.01f);
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

static void testV06() {
  // DMX input: what we send we can read back.
  DmxUniverse u{};
  u[0] = 255; u[9] = 128; u[511] = 7;
  int uni = -1, count = 0;
  DmxUniverse got{};
  auto art = encodeArtDmx(3, 1, u);
  CHECK(decodeArtDmx(art.data(), art.size(), uni, got, count));
  CHECK(uni == 3 && count == 512 && got[0] == 255 && got[9] == 128 && got[511] == 7);
  std::array<uint8_t, 16> cid{};
  auto sacn = encodeSacn(5, 1, u, cid, "test");
  got = {};
  CHECK(decodeSacn(sacn.data(), sacn.size(), uni, got, count));
  CHECK(uni == 5 && count == 512 && got[9] == 128);
  CHECK(!decodeArtDmx(sacn.data(), sacn.size(), uni, got, count));
  CHECK(!decodeSacn(art.data(), art.size(), uni, got, count));

  // Plain-text OSC.
  OscMessage m;
  CHECK(parseOscText("/millumin/action/launchColumn \"Intro\" 2 0.5", m));
  CHECK(m.address == "/millumin/action/launchColumn" && m.args.size() == 3);
  CHECK(std::get<std::string>(m.args[0]) == "Intro" && std::get<int32_t>(m.args[1]) == 2);
  CHECK(!parseOscText("no slash", m));

  // New project fields survive a save / load.
  Project p = Project::makeDefault();
  p.columns[0].cells[p.layers[0].id] = Cell{};
  p.columns[0].cells[p.layers[0].id].mediaId = 1;
  p.columns[0].cells[p.layers[0].id].setEndMode(EndMode::NextColumn);
  p.columns[1].autoGo = 12.5f;
  p.columns[1].autoGoTarget = 3;
  p.columns[1].autoGoAfterTransition = true;
  p.columns[1].states.push_back({p.layers[0].id, int(MappingTarget::Scale), 0.5f});
  p.columns[2].color[0] = 0.5f;
  Warp sw;
  sw.setRect({10, 20}, {300, 200});
  int sid = p.addSurface(sw).id;
  p.layers[0].slices[0].surfaceId = sid;
  p.schedule.push_back({true, 21, 30, 0x3E, ScheduleAction::BlackoutOn, 1});
  p.dmxInput = {true, DmxProtocol::SACN, 1, 4};
  p.startOutputs = true;
  p.startColumn = 2;
  p.outputs[0].delayFrames = 2;
  InputMapping im;
  im.transformer = Transformer::Accumulator;
  im.step = 0.25f;
  im.firstColumn = 2; im.lastColumn = 4;
  p.mappings.push_back(im);
  Project q;
  std::string err;
  CHECK(deserializeProject(serializeProject(p, ""), "", q, &err));
  const Cell &c = q.columns[0].cells[q.layers[0].id];
  CHECK(c.endMode == EndMode::NextColumn && !c.loop);
  CHECK(q.columns[1].autoGo == 12.5f && q.columns[1].autoGoTarget == 3 && q.columns[1].autoGoAfterTransition);
  CHECK(q.columns[1].states.size() == 1 && q.columns[1].states[0].value == 0.5f);
  CHECK(q.columns[2].color[0] == 0.5f);
  CHECK(q.surfaces.size() == 1 && q.layers[0].slices[0].surfaceId == sid);
  CHECK(near(q.layers[0].slices[0].warp.corners[2], {310, 220})); // resolved from the surface
  CHECK(q.schedule.size() == 1 && q.schedule[0].hour == 21 && q.schedule[0].minute == 30 && q.schedule[0].days == 0x3E);
  CHECK(q.dmxInput.enabled && q.dmxInput.protocol == DmxProtocol::SACN && q.dmxInput.count == 4);
  CHECK(q.startOutputs && q.startColumn == 2 && q.outputs[0].delayFrames == 2);
  CHECK(q.mappings[0].transformer == Transformer::Accumulator && q.mappings[0].step == 0.25f && q.mappings[0].lastColumn == 4);

  // Old files: "loop": false meant "hold the last frame".
  CHECK(deserializeProject(R"({"layers":[{"id":1}],"columns":[{"id":2,"cells":[{"layer":1,"media":3,"loop":false}]}]})", "", q, &err));
  CHECK(q.columns[0].cells[1].endMode == EndMode::Freeze);

  // Surfaces: editing one bound slice moves the surface and the other layers.
  Project s = Project::makeDefault();
  s.addLayer("B");
  int id = s.addSurface(s.layers[0].slices[0].warp).id;
  s.layers[0].slices[0].surfaceId = id;
  s.layers[1].slices[0].surfaceId = id;
  s.resolveSurfaces();
  s.layers[1].slices[0].warp.corners[0] = {111, 222};
  s.pushSurfaceEdits();
  CHECK(near(s.surface(id)->warp.corners[0], {111, 222}));
  CHECK(near(s.layers[0].slices[0].warp.corners[0], {111, 222}));
  s.surfaces.clear();
  s.resolveSurfaces();
  CHECK(s.layers[0].slices[0].surfaceId == -1 && near(s.layers[0].slices[0].warp.corners[0], {111, 222}));
  // Removing a layer removes its states.
  s.columns[0].states.push_back({s.layers[1].id, 0, 1});
  s.removeLayer(s.layers[1].id);
  CHECK(s.columns[0].states.empty());
}

static void testV07() {
  // Input mesh: inserting a row/column keeps the image where it was.
  Warp w;
  w.corners = {Vec2{100, 50}, Vec2{900, 120}, Vec2{1000, 700}, Vec2{50, 600}};
  w.resetMesh(3, 3);
  w.meshAt(1, 1) = {0.55f, 0.45f}; // a bulge
  Warp before = w;
  CHECK(w.insertLines(0.3f, 0.8f));
  CHECK(w.meshCols == 4 && w.meshRows == 4 && !w.uvIsIdentity());
  // New node (1, 2) sits at the old parameter (0.3, 0.8) and shows that content point.
  CHECK(near(w.meshPointDest(1, 2), before.map(0.3f, 0.8f), 0.01f));
  CHECK(near(w.uvAt(1, 2), {0.3f, 0.8f}));
  // Old nodes did not move.
  CHECK(near(w.meshPointDest(2, 1), before.meshPointDest(1, 1), 0.01f));
  CHECK(!w.insertLines(1.0f / 3.0f, 0.68f)); // on existing lines: nothing to add
  // Serialisation keeps the input side.
  Project p = Project::makeDefault();
  p.layers[0].slices[0].warp = w;
  Project q;
  CHECK(deserializeProject(serializeProject(p, ""), "", q));
  CHECK(q.layers[0].slices[0].warp == w);
  // Changing the division keeps the deformation.
  Warp r = before;
  r.resampleMesh(5, 5);
  CHECK(r.meshCols == 5 && near(r.map(0.5f, 0.5f), before.map(0.5f, 0.5f), 0.5f));
  CHECK(near(r.meshPointDest(2, 2), before.meshPointDest(1, 1), 0.01f));
  // unmap is the inverse of map, mesh included.
  Vec2 uv;
  CHECK(before.unmap(before.map(0.37f, 0.61f), uv) && near(uv, {0.37f, 0.61f}, 1e-3f));

  // Masks: bezier outline and scale.
  Mask m;
  m.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  CHECK(maskOutline(m).size() == 4);
  m.ensureTangents();
  m.tanOut[0] = {30, -30};
  CHECK(m.isBezier(0) && maskOutline(m, 16).size() == 4 + 15);
  m.scale = 0.5f;
  CHECK(near(maskOutline(m)[0], {25, 25}));

  // SVG import: viewBox scaling, transforms, curves, shapes.
  std::string err;
  auto a = masksFromSvg(R"svg(<svg viewBox="0 0 100 50"><g transform="translate(10 5)">
      <path id="porta" d="M0 0 L20 0 L20 20 Z"/></g>
      <rect x="50" y="10" width="10" height="20"/><circle cx="80" cy="25" r="10"/>
      <polygon points="0,40 10,40 5,49"/><path d="m 0 0 h 10 v 10 h -10 z m 20 0 h 5 v 5 z"/></svg>)svg",
                        1000, 500, &err);
  CHECK(a.size() == 6);
  if (a.size() == 6) {
    CHECK(a[0].name == "porta" && a[0].points.size() == 3 && near(a[0].points[1], {300, 50}));
    CHECK(a[1].points.size() == 4 && near(a[1].points[2], {600, 300}));
    CHECK(a[2].points.size() == 4 && a[2].isBezier(0)); // circle: 4 curves
    CHECK(near(a[2].points[0], {900, 250}));
    CHECK(a[4].points.size() == 4 && a[5].points.size() == 3); // two subpaths
  }
  // Round trip through our own export.
  Mask c;
  c.points = {{10, 10}, {200, 20}, {150, 180}};
  c.ensureTangents();
  c.tanOut[1] = {20, 40};
  c.tanIn[2] = {30, -10};
  auto b = masksFromSvg(masksToSvg({c}, 1920, 1080), 1920, 1080);
  CHECK(b.size() == 1 && b[0].points.size() == 3);
  if (b.size() == 1 && b[0].points.size() == 3) {
    CHECK(near(b[0].points[1], c.points[1]) && near(b[0].tanOut[1], c.tanOut[1]) && near(b[0].tanIn[2], c.tanIn[2]));
  }
  CHECK(masksFromSvg("<svg></svg>", 1, 1, &err).empty() && !err.empty());

  // Free blend: the soft edges computed from overlaps match the grid assistant.
  Project g = Project::makeDefault();
  BlendLayoutParams bl;
  bl.cols = 2; bl.rows = 2; bl.overlapX = 0.15f; bl.overlapY = 0.1f;
  applyBlendLayout(g, bl, -1, false);
  std::vector<SoftEdge> want;
  for (auto &o : g.outputs) want.push_back(o.softEdge);
  CHECK(autoBlendOverlaps(g, 0) == 4);
  for (size_t i = 0; i < g.outputs.size(); ++i) {
    CHECK(std::fabs(g.outputs[i].softEdge.left - want[i].left) < 1e-4f);
    CHECK(std::fabs(g.outputs[i].softEdge.right - want[i].right) < 1e-4f);
    CHECK(std::fabs(g.outputs[i].softEdge.top - want[i].top) < 1e-4f);
    CHECK(std::fabs(g.outputs[i].softEdge.bottom - want[i].bottom) < 1e-4f);
  }
  // Two projectors of different sizes, offset vertically.
  Project f = Project::makeDefault();
  f.outputs[0].srcX = 0; f.outputs[0].srcY = 0; f.outputs[0].srcW = 1000; f.outputs[0].srcH = 600;
  OutputConfig &o2 = f.addOutput();
  o2.srcX = 900; o2.srcY = 100; o2.srcW = 800; o2.srcH = 600;
  CHECK(autoBlendOverlaps(f, 0) == 1);
  CHECK(std::fabs(f.outputs[0].softEdge.right - 0.1f) < 1e-4f && std::fabs(f.outputs[1].softEdge.left - 0.125f) < 1e-4f);
  CHECK(f.outputs[0].softEdge.left == 0 && f.outputs[1].softEdge.top == 0);
  // Outputs of another canvas are left alone.
  o2.canvasId = 99;
  CHECK(autoBlendOverlaps(f, 0) == 0 && f.outputs[1].softEdge.left > 0);

  // Several canvases.
  Project mc = Project::makeDefault();
  CanvasConfig &led = mc.addCanvas(3840, 2160);
  int ledId = led.id;
  Layer &ll = mc.addLayer("LED", ledId);
  CHECK(ll.canvasId == ledId && std::fabs(ll.slices[0].warp.corners[2].x - 3840) < 0.5f);
  MediaItem &cm = mc.addCanvasMedia(0);
  CHECK(cm.type == MediaType::Canvas && cm.canvasRef == 0);
  mc.outputs[0].canvasId = ledId;
  mc.outputs[0].spoutName = "LED processor";
  mc.outputs[0].windowless = true;
  Project mc2;
  CHECK(deserializeProject(serializeProject(mc, ""), "", mc2));
  CHECK(mc2.canvases.size() == 1 && mc2.canvasWidth(ledId) == 3840 && mc2.canvasHeight(ledId) == 2160);
  CHECK(mc2.layers.front().canvasId == ledId && mc2.library.back().type == MediaType::Canvas);
  CHECK(mc2.outputs[0].canvasId == ledId && mc2.outputs[0].spoutName == "LED processor" && mc2.outputs[0].windowless);
  CHECK(mc2.canvasWidth(12345) == mc2.canvasW); // unknown: main canvas
  // The blend assistant keeps outputs of other canvases.
  applyBlendLayout(mc2, bl, -1, true);
  CHECK(mc2.outputs.size() == 5 && mc2.outputs.back().canvasId == ledId);
  CHECK(mc2.layers.front().slices[0].warp.corners[2].x == 3840); // other canvas not rescaled
  mc2.removeCanvas(ledId);
  CHECK(mc2.canvases.empty() && mc2.outputs.back().canvasId == 0 && mc2.library.back().canvasRef == 0);
  CHECK(std::none_of(mc2.layers.begin(), mc2.layers.end(), [&](auto &l) { return l.canvasId == ledId; }));
}

int main() {
  testV07();
  testV06();
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
