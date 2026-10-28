#include "core/warp.h"

#include <algorithm>

namespace lumen {

Vec2 Mat3::apply(Vec2 p) const {
  float x = m[0] * p.x + m[1] * p.y + m[2];
  float y = m[3] * p.x + m[4] * p.y + m[5];
  float w = m[6] * p.x + m[7] * p.y + m[8];
  if (std::fabs(w) < 1e-9f) w = 1e-9f;
  return {x / w, y / w};
}

Mat3 Mat3::inverse() const {
  const float *a = m;
  float det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
              a[2] * (a[3] * a[7] - a[4] * a[6]);
  Mat3 r;
  if (std::fabs(det) < 1e-12f) return r;
  float id = 1.0f / det;
  r.m[0] = (a[4] * a[8] - a[5] * a[7]) * id;
  r.m[1] = (a[2] * a[7] - a[1] * a[8]) * id;
  r.m[2] = (a[1] * a[5] - a[2] * a[4]) * id;
  r.m[3] = (a[5] * a[6] - a[3] * a[8]) * id;
  r.m[4] = (a[0] * a[8] - a[2] * a[6]) * id;
  r.m[5] = (a[2] * a[3] - a[0] * a[5]) * id;
  r.m[6] = (a[3] * a[7] - a[4] * a[6]) * id;
  r.m[7] = (a[1] * a[6] - a[0] * a[7]) * id;
  r.m[8] = (a[0] * a[4] - a[1] * a[3]) * id;
  return r;
}

// Heckbert, "Fundamentals of Texture Mapping and Image Warping" (1989).
Mat3 Mat3::squareToQuad(const std::array<Vec2, 4> &q) {
  float x0 = q[0].x, y0 = q[0].y, x1 = q[1].x, y1 = q[1].y;
  float x2 = q[2].x, y2 = q[2].y, x3 = q[3].x, y3 = q[3].y;
  float sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
  Mat3 r;
  if (std::fabs(sx) < 1e-6f && std::fabs(sy) < 1e-6f) { // affine
    r.m[0] = x1 - x0; r.m[1] = x2 - x1; r.m[2] = x0;
    r.m[3] = y1 - y0; r.m[4] = y2 - y1; r.m[5] = y0;
    r.m[6] = 0; r.m[7] = 0; r.m[8] = 1;
    return r;
  }
  float dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
  float det = dx1 * dy2 - dx2 * dy1;
  if (std::fabs(det) < 1e-12f) det = 1e-12f;
  float g = (sx * dy2 - dx2 * sy) / det;
  float h = (dx1 * sy - sx * dy1) / det;
  r.m[0] = x1 - x0 + g * x1; r.m[1] = x3 - x0 + h * x3; r.m[2] = x0;
  r.m[3] = y1 - y0 + g * y1; r.m[4] = y3 - y0 + h * y3; r.m[5] = y0;
  r.m[6] = g; r.m[7] = h; r.m[8] = 1;
  return r;
}

void Warp::setRect(Vec2 tl, Vec2 size) {
  corners = {Vec2{tl.x, tl.y}, Vec2{tl.x + size.x, tl.y}, Vec2{tl.x + size.x, tl.y + size.y},
             Vec2{tl.x, tl.y + size.y}};
  if (mesh.empty()) resetMesh(meshCols, meshRows);
}

void Warp::resetMesh(int cols, int rows) {
  meshCols = std::max(2, cols);
  meshRows = std::max(2, rows);
  uvMesh.clear();
  mesh.resize(size_t(meshCols * meshRows));
  for (int r = 0; r < meshRows; ++r)
    for (int c = 0; c < meshCols; ++c)
      meshAt(c, r) = {float(c) / float(meshCols - 1), float(r) / float(meshRows - 1)};
}

bool Warp::meshIsIdentity() const {
  for (int r = 0; r < meshRows; ++r)
    for (int c = 0; c < meshCols; ++c) {
      Vec2 d = meshAt(c, r) - Vec2{float(c) / float(meshCols - 1), float(r) / float(meshRows - 1)};
      if (std::fabs(d.x) > 1e-6f || std::fabs(d.y) > 1e-6f) return false;
    }
  return true;
}

bool Warp::uvIsIdentity() const {
  if (uvMesh.size() != size_t(meshCols * meshRows)) return true;
  for (int r = 0; r < meshRows; ++r)
    for (int c = 0; c < meshCols; ++c) {
      Vec2 d = uvMesh[size_t(r * meshCols + c)] - Vec2{float(c) / float(meshCols - 1), float(r) / float(meshRows - 1)};
      if (std::fabs(d.x) > 1e-6f || std::fabs(d.y) > 1e-6f) return false;
    }
  return true;
}

void Warp::ensureUvMesh() {
  if (uvMesh.size() == size_t(meshCols * meshRows)) return;
  uvMesh.resize(size_t(meshCols * meshRows));
  for (int r = 0; r < meshRows; ++r)
    for (int c = 0; c < meshCols; ++c)
      uvAt(c, r) = {float(c) / float(meshCols - 1), float(r) / float(meshRows - 1)};
}

static float catmull(float p0, float p1, float p2, float p3, float t) {
  float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                 (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

// Smooth interpolation through the control points (Catmull-Rom in both axes,
// with clamped borders). Degenerates to bilinear for a 2x2 grid.
static Vec2 gridInterp(const std::vector<Vec2> &grid, int meshCols, int meshRows, float u, float v);

Vec2 Warp::meshInterp(float u, float v) const { return gridInterp(mesh, meshCols, meshRows, u, v); }

Vec2 Warp::uvMap(float u, float v) const {
  if (uvMesh.size() != size_t(meshCols * meshRows)) return {u, v};
  return gridInterp(uvMesh, meshCols, meshRows, u, v);
}

static Vec2 gridInterp(const std::vector<Vec2> &grid, int meshCols, int meshRows, float u, float v) {
  auto meshAt = [&](int c, int r) -> const Vec2 & { return grid[size_t(r * meshCols + c)]; };
  float fx = std::clamp(u, 0.f, 1.f) * float(meshCols - 1);
  float fy = std::clamp(v, 0.f, 1.f) * float(meshRows - 1);
  int cx = std::min(int(fx), meshCols - 2), cy = std::min(int(fy), meshRows - 2);
  float tx = fx - float(cx), ty = fy - float(cy);

  auto P = [&](int c, int r) -> const Vec2 & {
    return meshAt(std::clamp(c, 0, meshCols - 1), std::clamp(r, 0, meshRows - 1));
  };
  Vec2 rows[4];
  for (int j = 0; j < 4; ++j) {
    int r = cy - 1 + j;
    const Vec2 &a = P(cx - 1, r), &b = P(cx, r), &c = P(cx + 1, r), &d = P(cx + 2, r);
    rows[j] = {catmull(a.x, b.x, c.x, d.x, tx), catmull(a.y, b.y, c.y, d.y, tx)};
  }
  return {catmull(rows[0].x, rows[1].x, rows[2].x, rows[3].x, ty),
          catmull(rows[0].y, rows[1].y, rows[2].y, rows[3].y, ty)};
}

bool Warp::perspectiveValid() const {
  // The homography's w must keep one sign over the whole square. A concave
  // (or nearly degenerate) quad sends part of the image through infinity,
  // which shows up as wild stretched stripes.
  Mat3 h = Mat3::squareToQuad(corners);
  const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (auto &q : uv)
    if (h.m[6] * q[0] + h.m[7] * q[1] + h.m[8] < 0.02f) return false;
  return true;
}

Vec2 Warp::baseMap(Vec2 uv) const {
  if (perspectiveValid()) return Mat3::squareToQuad(corners).apply(uv);
  // Bilinear fallback: follows the four corners without any singularity.
  Vec2 top = corners[0] + (corners[1] - corners[0]) * uv.x;
  Vec2 bottom = corners[3] + (corners[2] - corners[3]) * uv.x;
  return top + (bottom - top) * uv.y;
}

Vec2 Warp::map(float u, float v) const {
  if (meshCols == 2 && meshRows == 2 && meshIsIdentity()) return baseMap({u, v});
  return baseMap(meshInterp(u, v));
}

Vec2 Warp::unmapPerspective(Vec2 p) const {
  if (perspectiveValid()) return Mat3::squareToQuad(corners).inverse().apply(p);
  // Inverse bilinear by Newton iterations (converges in a few steps).
  Vec2 uv{0.5f, 0.5f};
  for (int it = 0; it < 12; ++it) {
    Vec2 f = baseMap(uv) - p;
    const float e = 1e-3f;
    Vec2 du = (baseMap({uv.x + e, uv.y}) - baseMap(uv)) * (1.0f / e);
    Vec2 dv = (baseMap({uv.x, uv.y + e}) - baseMap(uv)) * (1.0f / e);
    float det = du.x * dv.y - du.y * dv.x;
    if (std::fabs(det) < 1e-9f) break;
    uv = {uv.x - (f.x * dv.y - f.y * dv.x) / det, uv.y - (du.x * f.y - du.y * f.x) / det};
  }
  return uv;
}

Vec2 Warp::meshPointDest(int c, int r) const { return baseMap(meshAt(c, r)); }

Vec2 Warp::center() const {
  Vec2 s;
  for (auto &c : corners) s += c;
  return s * 0.25f;
}

void Warp::translate(Vec2 d) {
  for (auto &c : corners) c += d;
}

void Warp::scaleAround(Vec2 p, float sx, float sy) {
  for (auto &c : corners) c = {p.x + (c.x - p.x) * sx, p.y + (c.y - p.y) * sy};
}

void Warp::rotateAround(Vec2 p, float a) {
  float cs = std::cos(a), sn = std::sin(a);
  for (auto &c : corners) {
    Vec2 d = c - p;
    c = {p.x + d.x * cs - d.y * sn, p.y + d.x * sn + d.y * cs};
  }
}

void Warp::tessellate(int n, float u0, float v0, float u1, float v1,
                      std::vector<float> &out) const {
  Mat3 h = Mat3::squareToQuad(corners);
  bool persp = perspectiveValid();
  bool identity = meshCols == 2 && meshRows == 2 && meshIsIdentity();
  bool uvWarp = !uvIsIdentity();
  std::vector<Vec2> pos(size_t((n + 1) * (n + 1)));
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) {
      float u = float(i) / float(n), v = float(j) / float(n);
      Vec2 q = identity ? Vec2{u, v} : meshInterp(u, v);
      pos[size_t(j * (n + 1) + i)] = persp ? h.apply(q) : baseMap(q);
    }
  auto push = [&](int i, int j) {
    const Vec2 &p = pos[size_t(j * (n + 1) + i)];
    float u = float(i) / float(n), v = float(j) / float(n);
    if (uvWarp) {
      Vec2 t = uvMap(u, v);
      u = t.x;
      v = t.y;
    }
    out.insert(out.end(), {p.x, p.y, u0 + (u1 - u0) * u, v0 + (v1 - v0) * v});
  };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      push(i, j); push(i + 1, j); push(i + 1, j + 1);
      push(i, j); push(i + 1, j + 1); push(i, j + 1);
    }
}

void Warp::resampleMesh(int cols, int rows) {
  cols = std::clamp(cols, 2, 33);
  rows = std::clamp(rows, 2, 33);
  if (cols == meshCols && rows == meshRows) return;
  bool uv = !uvIsIdentity();
  std::vector<Vec2> nm(size_t(cols * rows)), nuv(size_t(cols * rows));
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      float u = float(c) / float(cols - 1), v = float(r) / float(rows - 1);
      nm[size_t(r * cols + c)] = meshInterp(u, v);
      nuv[size_t(r * cols + c)] = uvMap(u, v);
    }
  meshCols = cols;
  meshRows = rows;
  mesh = std::move(nm);
  if (uv) uvMesh = std::move(nuv);
  else uvMesh.clear();
}

bool Warp::insertLines(float u, float v) {
  // New knots: the old (uniform) ones plus u / v. The new grid is uniform
  // again, so the input side records which content point each node shows.
  auto knots = [](int n, float x, bool &added) {
    std::vector<float> k;
    for (int i = 0; i < n; ++i) k.push_back(float(i) / float(n - 1));
    added = false;
    float minGap = 0.25f / float(n - 1);
    bool close = std::any_of(k.begin(), k.end(), [&](float q) { return std::fabs(q - x) < minGap; });
    if (!close && n < 33 && x > 0 && x < 1) {
      k.insert(std::upper_bound(k.begin(), k.end(), x), x);
      added = true;
    }
    return k;
  };
  bool addC, addR;
  auto ku = knots(meshCols, std::clamp(u, 0.0f, 1.0f), addC);
  auto kv = knots(meshRows, std::clamp(v, 0.0f, 1.0f), addR);
  if (!addC && !addR) return false;
  int cols = int(ku.size()), rows = int(kv.size());
  std::vector<Vec2> nm(size_t(cols * rows)), nuv(size_t(cols * rows));
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      nm[size_t(r * cols + c)] = meshInterp(ku[size_t(c)], kv[size_t(r)]);
      nuv[size_t(r * cols + c)] = uvMap(ku[size_t(c)], kv[size_t(r)]);
    }
  meshCols = cols;
  meshRows = rows;
  mesh = std::move(nm);
  uvMesh = std::move(nuv);
  return true;
}

bool Warp::unmap(Vec2 p, Vec2 &uv) const {
  // Coarse search, then Newton steps on map().
  float best = 1e30f;
  const int N = 24;
  for (int j = 0; j <= N; ++j)
    for (int i = 0; i <= N; ++i) {
      Vec2 q{float(i) / N, float(j) / N};
      float d = (map(q.x, q.y) - p).length();
      if (d < best) { best = d; uv = q; }
    }
  for (int it = 0; it < 10; ++it) {
    Vec2 f = map(uv.x, uv.y) - p;
    const float e = 1e-3f;
    Vec2 du = (map(uv.x + e, uv.y) - map(uv.x, uv.y)) * (1.0f / e);
    Vec2 dv = (map(uv.x, uv.y + e) - map(uv.x, uv.y)) * (1.0f / e);
    float det = du.x * dv.y - du.y * dv.x;
    if (std::fabs(det) < 1e-12f) break;
    uv = {std::clamp(uv.x - (f.x * dv.y - f.y * dv.x) / det, 0.0f, 1.0f),
          std::clamp(uv.y - (du.x * f.y - du.y * f.x) / det, 0.0f, 1.0f)};
  }
  return (map(uv.x, uv.y) - p).length() < 1.0f + 1e-3f * (corners[2] - corners[0]).length();
}

bool Warp::operator==(const Warp &o) const {
  if (uvMesh != o.uvMesh) return false;
  if (meshCols != o.meshCols || meshRows != o.meshRows || mesh.size() != o.mesh.size()) return false;
  for (size_t i = 0; i < 4; ++i)
    if (corners[i].x != o.corners[i].x || corners[i].y != o.corners[i].y) return false;
  for (size_t i = 0; i < mesh.size(); ++i)
    if (mesh[i].x != o.mesh[i].x || mesh[i].y != o.mesh[i].y) return false;
  return true;
}

} // namespace lumen
