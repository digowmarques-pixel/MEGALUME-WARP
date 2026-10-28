// Geometry used by the videomapping tools: a 4-corner perspective quad plus an
// optional mesh (grid of control points) for curved / irregular surfaces.
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace lumen {

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2 &operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
  bool operator!=(Vec2 o) const { return !(*this == o); }
  float length() const { return std::sqrt(x * x + y * y); }
};

// 3x3 projective transform, row-major.
struct Mat3 {
  float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  Vec2 apply(Vec2 p) const;
  Mat3 inverse() const;
  // Maps the unit square (0,0)(1,0)(1,1)(0,1) onto quad q (same order).
  static Mat3 squareToQuad(const std::array<Vec2, 4> &q);
};

// Corner order: top-left, top-right, bottom-right, bottom-left.
struct Warp {
  std::array<Vec2, 4> corners;
  // Mesh control points in *unit* space (before perspective), row-major,
  // meshCols x meshRows. A 2x2 mesh is the identity (no deformation).
  int meshCols = 2, meshRows = 2;
  std::vector<Vec2> mesh;
  // Input side of the mesh: which point of the content (unit space) each
  // control point shows. Empty = the regular grid (no input warp). Lets the
  // grid follow features of the image (windows of a façade...).
  std::vector<Vec2> uvMesh;

  Warp() { setRect({0, 0}, {1, 1}); }
  void setRect(Vec2 topLeft, Vec2 size);
  void resetMesh(int cols, int rows);
  bool hasMesh() const { return meshCols > 2 || meshRows > 2 || !meshIsIdentity() || !uvIsIdentity(); }
  bool meshIsIdentity() const;
  bool uvIsIdentity() const;
  void ensureUvMesh(); // fills uvMesh with the regular grid
  Vec2 &uvAt(int c, int r) { return uvMesh[size_t(r * meshCols + c)]; }
  // Content point shown at grid parameter (u, v).
  Vec2 uvMap(float u, float v) const;
  // Changes the grid size keeping the current deformation (Millumin's "division").
  void resampleMesh(int cols, int rows);
  // Adds a column of control points at grid parameter u and a row at v
  // (0..1), without moving the image. Returns false if the grid is full.
  bool insertLines(float u, float v);
  // Grid parameter (u, v) of a destination point (inverse of map, mesh included).
  bool unmap(Vec2 p, Vec2 &uv) const;

  Vec2 &meshAt(int c, int r) { return mesh[r * meshCols + c]; }
  const Vec2 &meshAt(int c, int r) const { return mesh[r * meshCols + c]; }

  // Unit (u,v) of the content -> destination space (canvas px or output 0..1).
  Vec2 map(float u, float v) const;
  // Destination point -> unit space of the perspective quad (mesh ignored).
  Vec2 unmapPerspective(Vec2 p) const;
  // Position of mesh control point (c,r) in destination space.
  Vec2 meshPointDest(int c, int r) const;
  // False when the corners make a concave / degenerate quad: then the warp
  // uses bilinear interpolation instead of a (singular) perspective.
  bool perspectiveValid() const;

  Vec2 center() const;
  bool operator==(const Warp &o) const;
  bool operator!=(const Warp &o) const { return !(*this == o); }
  // Bounding-box helpers used by the transform tools.
  void translate(Vec2 d);
  void scaleAround(Vec2 pivot, float sx, float sy);
  void rotateAround(Vec2 pivot, float radians);

  // Tessellates into triangles: (x, y, u, v) per vertex.
  void tessellate(int subdiv, float u0, float v0, float u1, float v1,
                  std::vector<float> &out) const;

private:
  Vec2 meshInterp(float u, float v) const;
  Vec2 baseMap(Vec2 uv) const; // corners only: perspective, or bilinear fallback
};

inline bool pointInPolygon(Vec2 p, const std::vector<Vec2> &poly) {
  bool inside = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Vec2 &a = poly[i], &b = poly[j];
    if (((a.y > p.y) != (b.y > p.y)) &&
        (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12f) + a.x))
      inside = !inside;
  }
  return inside;
}

} // namespace lumen
