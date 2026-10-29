// Unit tests for the GPU-independent core: warp geometry, OSC, project files.
#include "control/midi.h"
#include "control/osc.h"
#include "media/hap.h"
#include "core/dmx.h"
#include "core/project.h"
#include "core/blob.h"
#include "core/isf.h"
#include "core/md5.h"
#include "core/timecode.h"
#include "control/devices.h"
#include "core/svg.h"
#include "core/warp.h"
#include "platform/platform.h"
#include "ui/i18n.h"
#include "core/parallel.h"
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

  // A regular grid is exactly linear up to the borders (repeating the border
  // point bent the outer cells: up to 47 px on a 1000 px projector).
  {
    Warp g;
    g.setRect({0, 0}, {1000, 1000});
    g.resetMesh(5, 5);
    g.meshAt(2, 2).x += 1e-5f; // no longer an identity mesh: the spline is used
    for (float u : {0.05f, 0.125f, 0.25f, 0.6f, 0.95f}) CHECK(near(g.map(u, 0.1f), {u * 1000, 100}, 0.05f));
    Warp h;
    h.setRect({0, 0}, {1000, 1000});
    h.resampleMesh(4, 4);
    CHECK(near(h.map(0.25f, 0.5f), {250, 500}, 0.05f));
    Warp k;
    k.setRect({0, 0}, {1000, 1000});
    k.resetMesh(2, 2);
    k.meshAt(1, 1).x += 1e-5f; // 2x2: bilinear
    CHECK(near(k.map(0.25f, 0.25f), {250, 250}, 0.05f));
  }

  // Editing a scaled / rotated layer in the workspace: the dragged corner
  // lands under the mouse in one step and stays there (the pivot moves with
  // the corner; re-solving against it drifted and ran away below scale 0.2),
  // and the rest of the layer does not move on screen.
  for (float sc : {0.15f, 0.5f, 2.0f}) {
    Layer l;
    Slice a, b;
    a.warp.setRect({0, 0}, {1000, 600});
    b.warp.setRect({1200, 0}, {400, 300});
    l.slices = {a, b};
    l.transform.scale = sc;
    l.transform.rotation = 30;
    l.transform.position = {50, -20};
    Warp otherBefore = transformedWarp(l, l.slices[1]);
    Vec2 c0 = transformedWarp(l, l.slices[0]).corners[0];
    Vec2 mouse{700, 400};
    for (int frame = 0; frame < 8; ++frame) // mouse held still while dragging
      keepInPlace(l, [&] { l.slices[0].warp.corners[2] = untransform(l, mouse); });
    CHECK(near(transformedWarp(l, l.slices[0]).corners[2], mouse, 0.05f));
    CHECK(near(transformedWarp(l, l.slices[0]).corners[0], c0, 0.05f));
    for (int k = 0; k < 4; ++k) CHECK(near(transformedWarp(l, l.slices[1]).corners[size_t(k)], otherBefore.corners[size_t(k)], 0.05f));
    // Moving a slice by 100 px on the canvas moves it 100 px, whatever the scale.
    Vec2 before = transformedWarp(l, l.slices[1]).corners[1];
    Vec2 d = layerDelta(l, {100, 0});
    keepInPlace(l, [&] { l.slices[1].warp.translate(d); });
    CHECK(near(transformedWarp(l, l.slices[1]).corners[1], before + Vec2{100, 0}, 0.05f));
    CHECK(near(transformedWarp(l, l.slices[0]).corners[0], c0, 0.05f));
  }

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
  // Every standard type tag is read (an unread type made the packet fall to the
  // text reader, which kept only the address: a "fire" with no argument).
  {
    const uint8_t pk[] = {'/', 'x', 0, 0, ',', 'r', 'c', '[', 'm', 't', ']', 'i', 0, 0, 0, 0,
                          0xff, 0, 0, 0xff, 0, 0, 0, 'A', 0x90, 60, 100, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 5};
    out.clear();
    CHECK(parseOsc(pk, sizeof(pk), out) && out.size() == 1 && out[0].args.size() == 5);
    CHECK(out.size() == 1 && std::get<int32_t>(out[0].args[1]) == 'A' && std::get<int32_t>(out[0].args[4]) == 5);
  }

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
  {
    // A slice cut as a free quad (slice editor): the canvas corners follow the media
    // corners by the same mapping, so the picture lands without bending.
    Warp w;
    w.resetMesh(2, 2);
    w.uvMesh = {{0, 0}, {0.7f, 0.3f}, {0, 1}, {1, 1}}; // row-major: TL TR BL BR
    auto A = [](Vec2 uv) { return Vec2{uv.x * 1280, uv.y * 720}; };
    w.corners = {A(w.uvMesh[0]), A(w.uvMesh[1]), A(w.uvMesh[3]), A(w.uvMesh[2])};
    float worst = 0;
    for (float s = 0; s <= 1.001f; s += 0.125f)
      for (float t = 0; t <= 1.001f; t += 0.125f) {
        Vec2 a = w.map(s, t), b = A(w.uvMap(s, t));
        worst = std::max(worst, std::hypot(a.x - b.x, a.y - b.y));
      }
    CHECK(worst < 0.05f);
  }
  {
    // Several dashboards: the columns keep their board, a bad index is clamped.
    Project bp = Project::makeDefault();
    bp.boards = {"Show", "Ensaio"};
    bp.currentBoard = 1;
    bp.columns[2].board = 1;
    Project back;
    std::string err;
    CHECK(deserializeProject(serializeProject(bp, ""), "", back, &err));
    CHECK(back.boards.size() == 2 && back.boards[1] == "Ensaio" && back.currentBoard == 1);
    CHECK(back.columns[2].board == 1 && back.columns[0].board == 0);
    std::string text = serializeProject(bp, "");
    size_t at = text.find("\"board\"");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text[text.find('1', at)] = '7'; // a board that does not exist
    Project bad;
    CHECK(deserializeProject(text, "", bad, &err) && bad.columns[2].board == 1);
  }
  // Outer soft edges (the border of the whole picture), saved with the project.
  g.edgeLeft = 192; g.edgeRight = 96; g.edgeTop = 108; g.edgeBottom = 0;
  b = computeBlendLayout(g);
  CHECK(std::fabs(b.projectors[0].left - 0.1f) < 1e-5f && std::fabs(b.projectors[2].right - 0.05f) < 1e-5f);
  CHECK(std::fabs(b.projectors[1].top - 0.1f) < 1e-5f && b.projectors[1].bottom == 0);
  CHECK(std::fabs(b.projectors[1].left * 1920 - 200) < 0.01f); // inner seams unchanged
  {
    Project ep = Project::makeDefault();
    ep.outputLayout = g;
    Project back;
    std::string err;
    CHECK(deserializeProject(serializeProject(ep, ""), "", back, &err));
    CHECK(back.outputLayout.edgeLeft == 192 && back.outputLayout.edgeRight == 96 && back.outputLayout.edgeTop == 108);
  }
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
  {
    // Priority is read; preview (blind) and stream-terminated packets are not live data.
    int prio = -1;
    CHECK(decodeSacn(sacn.data(), sacn.size(), uni, got, count, &prio) && prio == sacn[108]);
    auto preview = sacn;
    preview[112] |= 0x80;
    CHECK(!decodeSacn(preview.data(), preview.size(), uni, got, count));
    auto ended = sacn;
    ended[112] |= 0x40;
    CHECK(!decodeSacn(ended.data(), ended.size(), uni, got, count));
  }

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
  // An output warp is in 0..1: a point outside it is not "inside" (an absolute
  // tolerance of 1 accepted the whole output, and double clicks outside added lines).
  {
    Warp ow;
    ow.setRect({0, 0}, {1, 1});
    ow.resetMesh(3, 3);
    ow.meshAt(1, 1).x += 0.01f;
    CHECK(ow.unmap(ow.map(0.4f, 0.7f), uv) && near(uv, {0.4f, 0.7f}, 1e-3f));
    CHECK(!ow.unmap({1.6f, 0.5f}, uv));
    CHECK(!ow.unmap({0.5f, -0.3f}, uv));
  }

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
  // Shapes in <defs> / <clipPath> / <mask> are not masks (an artboard clip
  // would show the whole layer); "nan" / "inf" coordinates are dropped.
  CHECK(masksFromSvg(R"svg(<svg viewBox="0 0 100 100"><defs><clipPath id="c"><rect x="0" y="0" width="100" height="100"/></clipPath></defs>
                     <g clip-path="url(#c)"><rect x="10" y="10" width="20" height="20"/></g>
                     <mask id="m"><circle cx="50" cy="50" r="10"/></mask></svg>)svg", 100, 100).size() == 1);
  CHECK(masksFromSvg(R"svg(<svg><polygon points="0,0 nan,0 10,10"/><polygon points="0,0 -inf,0 10,10"/></svg>)svg", 100, 100).empty());

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
  // Only the part of an edge that the neighbour overlaps is blended (above the
  // lower projector the first one keeps its full light); the 2x2 grid blends
  // whole edges.
  {
    float sp[4][2];
    softEdgeSpans(f, f.outputs[0], sp);
    CHECK(std::fabs(sp[1][0] - 100.0f / 600.0f) < 1e-4f && sp[1][1] == 1.0f);
    CHECK(sp[0][0] == 0 && sp[0][1] == 1); // no neighbour on the left: whole edge
    softEdgeSpans(f, f.outputs[1], sp);
    CHECK(sp[0][0] == 0 && std::fabs(sp[0][1] - 500.0f / 600.0f) < 1e-4f);
    for (auto &o : g.outputs) {
      softEdgeSpans(g, o, sp);
      for (auto &e : sp) CHECK(e[0] == 0 && e[1] == 1);
    }
  }
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

static void testV08() {
  // ISF header parsing and translation.
  const char *src = R"isf(/*{
    "DESCRIPTION": "test", "CATEGORIES": ["Cor"],
    "INPUTS": [
      {"NAME": "inputImage", "TYPE": "image"},
      {"NAME": "amount", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 2},
      {"NAME": "tint", "TYPE": "color", "DEFAULT": [1, 0, 0, 1]},
      {"NAME": "mode", "TYPE": "long", "VALUES": [0, 1, 2], "LABELS": ["a", "b", "c"], "DEFAULT": 1},
      {"NAME": "pos", "TYPE": "point2D"},
      {"NAME": "on", "TYPE": "bool", "DEFAULT": true}
    ],
    "PASSES": [{"TARGET": "half", "WIDTH": "floor($WIDTH/2)", "HEIGHT": "$HEIGHT / 2", "PERSISTENT": true}, {}],
    "IMPORTED": {"noise": {"PATH": "noise.png"}}
  }*/
  #version 120
  void main() { gl_FragColor = IMG_THIS_PIXEL(inputImage) * amount; }
  )isf";
  IsfInfo info;
  std::string err;
  CHECK(parseIsf(src, info, &err));
  CHECK(info.filter && !info.transition && info.inputs.size() == 6 && info.passes.size() == 2);
  CHECK(info.passes[0].persistent && info.passes[0].target == "half" && info.imported.size() == 1);
  CHECK(info.categories.size() == 1 && info.categories[0] == "Cor");
  std::string glsl = isfFragmentSource(info);
  CHECK(glsl.find("#version 330") == 0 && glsl.find("#version 120") == std::string::npos);
  CHECK(glsl.find("uniform float amount;") != std::string::npos && glsl.find("uniform sampler2D half;") != std::string::npos);
  CHECK(glsl.find("uniform sampler2D noise;") != std::string::npos && glsl.find("_inputImage_imgSize") != std::string::npos);
  IsfValues v;
  isfDefaults(info, v);
  CHECK(v["amount"].size() == 1 && v["amount"][0] == 0.5f && v["tint"].size() == 4 && v["mode"][0] == 1.0f);
  CHECK(v["pos"].size() == 2 && v["on"][0] == 1.0f && v.count("inputImage") == 0);
  bool ok = false;
  CHECK(isfEval("floor($WIDTH/2)", {{"WIDTH", 1001}}, &ok) == 500 && ok);
  CHECK(std::fabs(isfEval("max($WIDTH, $HEIGHT) * 0.5 + (2 - 1)", {{"WIDTH", 100}, {"HEIGHT", 300}}, &ok) - 151) < 1e-4f && ok);
  // Deep nesting is refused, not a stack overflow.
  isfEval(std::string(200000, '(') + "1", {}, &ok);
  CHECK(!ok);
  // A header value of the wrong type is an invalid file, not a crash.
  {
    IsfInfo bad;
    std::string err;
    CHECK(!parseIsf("/*{\"INPUTS\":[{\"NAME\":\"a\",\"TYPE\":\"float\",\"LABEL\":null}]}*/\nvoid main(){}", bad, &err));
    CHECK(!err.empty());
    CHECK(!parseIsf("/*{\"INPUTS\":[{\"NAME\":5,\"TYPE\":\"float\"}]}*/\nvoid main(){}", bad, &err));
  }
  isfEval("$NOPE + 1", {}, &ok);
  CHECK(!ok);
  CHECK(!parseIsf("void main() {}", info, &err) && !err.empty());

  // Blob tracker: two bright squares on black.
  int W = 100, H = 50;
  std::vector<uint8_t> img(size_t(W * H), 0);
  auto square = [&](int x0, int y0, int s, uint8_t v) {
    for (int y = y0; y < y0 + s; ++y)
      for (int x = x0; x < x0 + s; ++x) img[size_t(y * W + x)] = v;
  };
  square(10, 10, 10, 255);
  square(70, 20, 14, 255);
  BlobTracker t;
  BlobParams bp;
  bp.smoothing = 0;
  auto b = t.process(img.data(), W, H, bp);
  CHECK(b.size() == 2);
  if (b.size() == 2) {
    const Blob &a = b[0].x < b[1].x ? b[0] : b[1], &c = b[0].x < b[1].x ? b[1] : b[0];
    CHECK(std::fabs(a.x - 0.15f) < 0.01f && std::fabs(a.y - 0.3f) < 0.02f && std::fabs(a.w - 0.1f) < 0.01f);
    CHECK(std::fabs(c.x - 0.77f) < 0.01f && c.area > a.area);
    int idA = a.id;
    // Move the left square a little: it keeps its id.
    std::fill(img.begin(), img.end(), 0);
    square(13, 11, 10, 255);
    square(70, 20, 14, 255);
    auto b2 = t.process(img.data(), W, H, bp);
    CHECK(b2.size() == 2 && std::any_of(b2.begin(), b2.end(), [&](const Blob &x) { return x.id == idA && x.x > 0.17f; }));
  }
  // Too small blobs are ignored; close blobs merge.
  std::fill(img.begin(), img.end(), 0);
  square(40, 20, 2, 255); // 4 px = 0.0008 of the image
  CHECK(t.process(img.data(), W, H, bp).empty());
  std::fill(img.begin(), img.end(), 0);
  square(10, 10, 8, 255);
  square(20, 10, 8, 255); // 2 px apart
  bp.mergeDistance = 0.05f;
  CHECK(t.process(img.data(), W, H, bp).size() == 1);
  bp.mergeDistance = 0;
  CHECK(t.process(img.data(), W, H, bp).size() == 2);
  // Background subtraction: a mid-grey person on a mid-grey wall with a bright lamp.
  BlobTracker bg;
  std::vector<uint8_t> wall(size_t(W * H), 120);
  for (int x = 0; x < 20; ++x) wall[size_t(5 * W + x)] = 250; // a lamp in the empty scene
  bg.setBackground(wall.data(), W, H);
  std::vector<uint8_t> scene = wall;
  for (int y = 20; y < 40; ++y)
    for (int x = 50; x < 60; ++x) scene[size_t(y * W + x)] = 40; // darker person
  BlobParams bq;
  bq.threshold = 0.2f;
  auto pb = bg.process(scene.data(), W, H, bq);
  CHECK(pb.size() == 1 && pb.size() == 1 && std::fabs(pb[0].x - 0.55f) < 0.01f);

  // Effects and mattes survive a save / load; built-ins use a portable path.
  Project p = Project::makeDefault();
  Effect fx;
  fx.path = builtinEffectsDir() + "/Desfoque.fs";
  fx.name = "Desfoque";
  fx.params["radius"] = {12};
  p.layers[0].effects.push_back(fx);
  Effect bt;
  bt.path = kBlobTrackerId;
  p.layers[0].effects.push_back(bt);
  p.layers[0].blend = BlendMode::LumaMatteInverted;
  MediaItem &sh = p.addShader(builtinEffectsDir() + "/Ruido.fs");
  sh.shaderParams["scale"] = {7};
  std::string json = serializeProject(p, "/tmp/somewhere");
  CHECK(json.find("@lumen/Desfoque.fs") != std::string::npos && json.find("@lumen/Ruido.fs") != std::string::npos);
  Project q;
  CHECK(deserializeProject(json, "/tmp/elsewhere", q));
  CHECK(q.layers[0].effects.size() == 2 && q.layers[0].effects[0].params["radius"][0] == 12);
  CHECK(platform::toPath(q.layers[0].effects[0].path) == platform::toPath(builtinEffectsDir()) / "Desfoque.fs");
  CHECK(q.layers[0].effects[1].isBlobTracker() && q.layers[0].blend == BlendMode::LumaMatteInverted);
  CHECK(q.library.back().type == MediaType::Shader && q.library.back().shaderParams["scale"][0] == 7);
}

static void testV09() {
  // MD5: RFC 1321 vectors and the PJLink specification example.
  CHECK(md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
  CHECK(md5Hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
  CHECK(md5Hex("498e4a67JBMIAProjectorLink") == "5d8409bc1c3fa39749434aa3a5c38682");
  // Device text tokens.
  CHECK(expandDeviceText("run cue 1[CR][LF]") == "run cue 1\r\n");
  CHECK(expandDeviceText("[HEX_02]PON[HEX_03]") == std::string("\x02PON\x03"));
  CHECK(expandDeviceText("a\\tb\\n") == "a\tb\n");
  // Keyframe curves.
  TimelineTrack tr;
  tr.keys = {{0, 0, Ease::Linear}, {2, 10, Ease::InOut}, {4, 0, Ease::Hold}, {5, 5, Ease::Linear}};
  CHECK(tr.valueAt(-1) == 0 && tr.valueAt(1) == 5 && tr.valueAt(3) == 5 && tr.valueAt(4.5f) == 0);
  CHECK(tr.valueAt(2.5f) < 10 && tr.valueAt(2.5f) > 8); // ease in-out: slow start (linear would be 7.5)
  CHECK(tr.valueAt(9) == 5);
  // Timelines, column options and projectors round trip.
  Project p = Project::makeDefault();
  Timeline &tl = p.addTimeline();
  tl.duration = 12; tl.loop = true; tl.loopStart = 3;
  tl.tracks.push_back({p.layers[0].id, "effect1/radius", true, {{1, 2, Ease::Out}, {5, 8, Ease::Hold}}});
  tl.flags.push_back({4, "luz", "pjlink Sala shutter close"});
  Column &c = p.columns[0];
  c.timelineId = tl.id;
  c.transitionType = TransitionType::Shader;
  c.transitionShader = builtinEffectsDir() + "/T_Circulo.fs";
  c.manualTransition = true;
  c.stingerMediaId = 7; c.stingerCut = 0.3f;
  c.commands = {"pjlink Sala power on", "wait 2", "midi note 1 60 127"};
  c.timecodeCue = 3661.5f;
  p.projectors.push_back({"Sala", "10.0.0.9", "segredo"});
  ScheduleEntry se;
  se.action = ScheduleAction::Command;
  se.command = "pjlink Sala power off";
  p.schedule.push_back(se);
  Project q;
  CHECK(deserializeProject(serializeProject(p, ""), "", q));
  CHECK(q.timelines.size() == 1 && q.timelines[0].loopStart == 3 && q.timelines[0].tracks[0].keys[1].ease == Ease::Hold);
  CHECK(q.timelines[0].flags[0].command == "pjlink Sala shutter close");
  const Column &qc = q.columns[0];
  CHECK(qc.timelineId == tl.id && qc.transitionType == TransitionType::Shader && qc.manualTransition);
  CHECK(qc.stingerMediaId == 7 && qc.stingerCut == 0.3f && qc.commands.size() == 3 && qc.timecodeCue == 3661.5f);
  CHECK(platform::toPath(qc.transitionShader) == platform::toPath(builtinEffectsDir()) / "T_Circulo.fs");
  CHECK(q.projectors.size() == 1 && q.projectors[0].password == "segredo");
  CHECK(q.schedule[0].action == ScheduleAction::Command && q.schedule[0].command == "pjlink Sala power off");
  q.removeLayer(q.layers[0].id);
  CHECK(q.timelines[0].tracks.empty());

  // Timecode: labels <-> seconds (with drop frame).
  Timecode a = Timecode::fromSeconds(3723.48, 25);
  CHECK(a.h == 1 && a.m == 2 && a.s == 3 && a.f == 12 && std::fabs(a.seconds() - 3723.48) < 0.001);
  Timecode df = Timecode::fromSeconds(60.0 * 10 + 0.5, 29.97);
  CHECK(df.dropFrame && std::fabs(df.seconds() - 600.5) < 0.04);
  Timecode d1 = Timecode::fromSeconds(60.0, 29.97); // 1 minute of real time: 00:00:59;28 or 00:01:00;02
  CHECK(d1.m == 0 || d1.f >= 2);
  double sec = 0;
  CHECK(parseTimecode("01:00:10:12", 25, sec) && std::fabs(sec - 3610.48) < 1e-6);
  CHECK(parseTimecode("00:01:02.5", 25, sec) && std::fabs(sec - 62.5) < 1e-6);
  CHECK(!parseTimecode("abc", 25, sec));
  // 29.97 drop frame: labels ;00 and ;01 do not exist at most minutes, so the
  // label is not h:m:s + f/fps (00:01:00;02 is real frame 1800).
  CHECK(parseTimecode("00:01:00;02", 29.97, sec) && std::fabs(sec - 1800 / 29.97) < 1e-6);
  CHECK(parseTimecode("00:10:00;00", 29.97, sec) && std::fabs(sec - 17982 / 29.97) < 1e-6);
  CHECK(Timecode::fromSeconds(1800 / 29.97 + 0.5 / 29.97, 29.97).str() == "00:01:00;02");
  // MTC: 8 quarter frames give the time (2 frames late, compensated); full frame.
  Timecode src = Timecode::fromSeconds(3725.2, 25);
  MtcDecoder mtc;
  Timecode got;
  bool ok = false;
  for (int piece = 0; piece < 8; ++piece) {
    auto qf = mtcQuarterFrame(src, piece);
    ok = mtc.feed(qf.data(), qf.size(), got);
  }
  CHECK(ok && got.fps == 25 && std::fabs(got.seconds() - (src.seconds() + 2.0 / 25)) < 1e-6);
  auto ff = mtcFullFrame(Timecode::fromSeconds(10.0, 30));
  CHECK(mtc.feed(ff.data(), ff.size(), got) && got.s == 10 && got.fps == 30);
  // LTC: encode a few frames, decode them back (also at another sample rate and level).
  for (double fps : {25.0, 30.0, 24.0}) {
    for (int rate : {48000, 44100}) {
      LtcEncoder enc(rate);
      std::vector<float> audio;
      for (int k = 0; k < 6; ++k) enc.encode(Timecode::fromSeconds(5025.0 + k / fps, fps), audio, 0.3f);
      LtcDecoder dec(rate);
      dec.feed(audio.data(), audio.size());
      Timecode last;
      CHECK(dec.latest(last) && dec.frames() >= 4);
      if (!(last.h == 1 && last.m == 23 && last.s == 45 && last.f >= 4 && std::fabs(last.fps - fps) < 0.5)) std::printf("ltc %s fps %.2f frames %d\n", last.str().c_str(), last.fps, int(dec.frames()));
      CHECK(last.h == 1 && last.m == 23 && last.s == 45 && last.f >= 4 && std::fabs(last.fps - fps) < 0.5);
    }
  }
}

// printf conversions of a format string, in order ("%d", "%.2f" -> "d", "f").
static std::string conversions(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') continue;
    if (i + 1 < s.size() && s[i + 1] == '%') { ++i; continue; }
    if (i + 1 >= s.size() || s[i + 1] == ' ') continue; // "20% of": plain text, not a format
    size_t j = i + 1;
    while (j < s.size() && std::strchr("-+#0123456789.zlh", s[j])) ++j;
    if (j < s.size() && std::strchr("diufFeEgGxXoscp", s[j])) out += s.substr(i + 1, j - i);
    else out += '?';
    out += ',';
    i = j;
  }
  return out;
}

static void testInstaller() {
#ifdef LUMEN_SOURCE_DIR
  // The uninstaller removes the shipped effects by name (users keep their own
  // .fs files in the same folder): every file of effects/ must be listed.
  namespace fs = std::filesystem;
  std::ifstream in(fs::path(LUMEN_SOURCE_DIR) / "installer" / "lumen.nsi");
  std::string nsi((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(!nsi.empty());
  int n = 0;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(fs::path(LUMEN_SOURCE_DIR) / "effects", ec)) {
    if (e.path().extension() != ".fs") continue;
    ++n;
    std::string line = "Delete \"$INSTDIR\\effects\\" + e.path().filename().string() + "\"";
    if (nsi.find(line) == std::string::npos) std::printf("installer/lumen.nsi: falta %s\n", line.c_str());
    CHECK(nsi.find(line) != std::string::npos);
  }
  CHECK(n > 20);
#endif
}

static void testI18n() {
  using namespace lumen;
  // Every translation keeps the format specifiers of the original, in order:
  // the labels are passed to ImGui::Text / snprintf as formats.
  static const std::vector<std::pair<std::string, std::string>> dict = {
#include "ui/lang_en.inc"
  };
  for (auto &[pt, en] : dict) {
    if (conversions(pt) != conversions(en)) std::printf("i18n: formatos diferentes em \"%s\"\n", pt.c_str());
    CHECK(conversions(pt) == conversions(en));
  }
  CHECK(dict.size() > 600);
  Language before = language();
  setLanguage(Language::Portuguese);
  const char *salvar = "Salvar";
  CHECK(T(salvar) == salvar);
  setLanguage(Language::English);
  CHECK(std::string(T("Salvar")) == "Save");
  CHECK(std::string(T("Salvar##menu")) == "Save##menu");
  const char *unknown = "xyz sem tradução";
  CHECK(T(unknown) == unknown);
  const char *z = TZ("Branco\0Preto\0Outra coisa\0");
  CHECK(std::string(z) == "White");
  CHECK(std::string(z + 6) == "Black");
  CHECK(std::string(z + 12) == "Outra coisa");
  CHECK(z[12 + 11 + 1] == '\0');
  setLanguage(before);
}

static void testParallel() {
  using namespace lumen;
  std::atomic<int> sum{0};
  std::vector<int> hits(1000, 0);
  parallelFor(1000, [&](int i) { ++hits[size_t(i)]; sum += i; });
  CHECK(sum == 999 * 1000 / 2);
  CHECK(std::all_of(hits.begin(), hits.end(), [](int h) { return h == 1; }));
  // Big copy with padding in the source rows (split across threads).
  int w = 4000, h = 1200;
  size_t row = size_t(w), stride = row + 64;
  std::vector<unsigned char> src(stride * size_t(h)), dst(row * size_t(h)), flip(row * size_t(h));
  for (size_t i = 0; i < src.size(); ++i) src[i] = (unsigned char)(i * 31 + i / 7);
  parallelCopyRows(dst.data(), ptrdiff_t(row), src.data(), ptrdiff_t(stride), row, h);
  bool ok = true;
  for (int y = 0; y < h && ok; ++y) ok = std::memcmp(&dst[size_t(y) * row], &src[size_t(y) * stride], row) == 0;
  CHECK(ok);
  // Vertical flip with a negative source stride.
  parallelCopyRows(flip.data(), ptrdiff_t(row), dst.data() + row * size_t(h - 1), -ptrdiff_t(row), row, h);
  ok = true;
  for (int y = 0; y < h && ok; ++y) ok = std::memcmp(&flip[size_t(y) * row], &dst[size_t(h - 1 - y) * row], row) == 0;
  CHECK(ok);
  // Many concurrent callers are serialized safely.
  std::atomic<int> total{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&] { for (int k = 0; k < 50; ++k) parallelFor(8, [&](int) { ++total; }); });
  for (auto &t : ts) t.join();
  CHECK(total == 4 * 50 * 8);
}

static bool hasPatch(const std::vector<lumen::AudioPatch> &r, int src, int dst) {
  return std::any_of(r.begin(), r.end(), [&](const lumen::AudioPatch &p) { return p.src == src && p.dst == dst; });
}

static void testAudioRoute() {
  using namespace lumen;
  // Stereo on a stereo card: L->1, R->2.
  auto r = resolveAudioRoute(2, 2, 0, {});
  CHECK(r.size() == 2 && hasPatch(r, 0, 0) && hasPatch(r, 1, 1));
  // Stereo to outputs 3-4 of an 8-channel interface.
  r = resolveAudioRoute(2, 8, 2, {});
  CHECK(r.size() == 2 && hasPatch(r, 0, 2) && hasPatch(r, 1, 3));
  // Mono goes to both outputs of the pair.
  r = resolveAudioRoute(1, 8, 4, {});
  CHECK(r.size() == 2 && hasPatch(r, 0, 4) && hasPatch(r, 0, 5));
  // 5.1 on an 8-channel card: one output per channel.
  r = resolveAudioRoute(6, 8, 0, {});
  CHECK(r.size() == 6 && hasPatch(r, 3, 3) && hasPatch(r, 5, 5));
  // 5.1 on a stereo card: downmix (centre in both, surrounds to their side, no LFE).
  r = resolveAudioRoute(6, 2, 0, {});
  CHECK(hasPatch(r, 2, 0) && hasPatch(r, 2, 1) && hasPatch(r, 4, 0) && hasPatch(r, 5, 1) && !hasPatch(r, 3, 0) &&
        !hasPatch(r, 3, 1));
  // A pair beyond the device (show made for 8 channels, laptop): falls back to 1-2.
  r = resolveAudioRoute(2, 2, 6, {});
  CHECK(hasPatch(r, 0, 0) && hasPatch(r, 1, 1));
  // Custom matrix: invalid connections are dropped.
  r = resolveAudioRoute(2, 4, 0, {{0, 3, 1}, {1, 9, 1}, {5, 0, 1}, {1, 2, 0.5f}});
  CHECK(r.size() == 2 && hasPatch(r, 0, 3) && hasPatch(r, 1, 2));
  // Single-output device: everything summed at half gain.
  r = resolveAudioRoute(2, 1, 0, {});
  CHECK(r.size() == 2 && hasPatch(r, 0, 0) && hasPatch(r, 1, 0) && std::fabs(r[0].gain - 0.5f) < 1e-6f);

  // The layer routing survives saving and loading.
  Project p;
  Layer &l = p.addLayer("Som");
  l.audioOutput = 4;
  l.audioMatrix = {{0, 6, 1}, {1, 7, 0.5f}};
  p.audioChannels = 16;
  p.audioExclusive = true;
  p.audioBufferMs = 5;
  Project q;
  CHECK(deserializeProject(serializeProject(p, ""), "", q));
  CHECK(q.layers.size() == 1 && q.layers[0].audioOutput == 4 && q.layers[0].audioMatrix.size() == 2 &&
        q.layers[0].audioMatrix[1].dst == 7 && std::fabs(q.layers[0].audioMatrix[1].gain - 0.5f) < 1e-6f);
  CHECK(q.audioChannels == 16 && q.audioExclusive && q.audioBufferMs == 5);
}

int main() {
  testAudioRoute();
  testParallel();
  testInstaller();
  testI18n();
  testV09();
  testV08();
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
