#include "core/svg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lumen {

namespace {

struct Affine {
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // x' = a x + c y + e ; y' = b x + d y + f
  Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  Vec2 linear(Vec2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
  Affine operator*(const Affine &o) const { // this after o
    Affine r;
    r.a = a * o.a + c * o.b;
    r.b = b * o.a + d * o.b;
    r.c = a * o.c + c * o.d;
    r.d = b * o.c + d * o.d;
    r.e = a * o.e + c * o.f + e;
    r.f = b * o.e + d * o.f + f;
    return r;
  }
};

std::vector<float> numbers(const std::string &s) {
  std::vector<float> out;
  const char *p = s.c_str();
  while (*p) {
    if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.') {
      char *end = nullptr;
      float v = std::strtof(p, &end);
      if (end == p) { ++p; continue; }
      out.push_back(v);
      p = end;
    } else {
      ++p;
    }
  }
  return out;
}

Affine parseTransform(const std::string &t) {
  Affine m;
  size_t pos = 0;
  while (pos < t.size()) {
    size_t open = t.find('(', pos);
    if (open == std::string::npos) break;
    size_t close = t.find(')', open);
    if (close == std::string::npos) break;
    std::string name = t.substr(pos, open - pos);
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == ','; }), name.end());
    auto v = numbers(t.substr(open + 1, close - open - 1));
    Affine x;
    if (name == "matrix" && v.size() >= 6) x = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (name == "translate" && !v.empty()) { x.e = v[0]; x.f = v.size() > 1 ? v[1] : 0; }
    else if (name == "scale" && !v.empty()) { x.a = v[0]; x.d = v.size() > 1 ? v[1] : v[0]; }
    else if (name == "rotate" && !v.empty()) {
      float r = v[0] * 3.14159265f / 180.0f, cs = std::cos(r), sn = std::sin(r);
      Affine rot{cs, sn, -sn, cs, 0, 0};
      if (v.size() >= 3) {
        Affine t1{1, 0, 0, 1, v[1], v[2]}, t2{1, 0, 0, 1, -v[1], -v[2]};
        x = t1 * rot * t2;
      } else {
        x = rot;
      }
    }
    m = m * x;
    pos = close + 1;
  }
  return m;
}

std::string attr(const std::string &tag, const char *name) {
  // name="value" or name='value', as a whole attribute name.
  size_t n = std::strlen(name);
  for (size_t pos = 0; (pos = tag.find(name, pos)) != std::string::npos; pos += n) {
    if (pos > 0 && !std::isspace(static_cast<unsigned char>(tag[pos - 1]))) continue;
    size_t q = pos + n;
    while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
    if (q >= tag.size() || tag[q] != '=') continue;
    ++q;
    while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
    if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) continue;
    char quote = tag[q];
    size_t end = tag.find(quote, q + 1);
    if (end == std::string::npos) return {};
    return tag.substr(q + 1, end - q - 1);
  }
  return {};
}

float num(const std::string &tag, const char *name, float def = 0) {
  std::string v = attr(tag, name);
  return v.empty() ? def : std::strtof(v.c_str(), nullptr);
}

// A shape being built: points with bezier handles (absolute control points).
struct Shape {
  std::vector<Vec2> pts, cin, cout; // cin/cout = absolute control points
  void moveTo(Vec2 p) { pts = {p}; cin = {p}; cout = {p}; }
  void lineTo(Vec2 p) { pts.push_back(p); cin.push_back(p); cout.push_back(p); }
  void cubicTo(Vec2 c1, Vec2 c2, Vec2 p) {
    cout.back() = c1;
    pts.push_back(p);
    cin.push_back(c2);
    cout.push_back(p);
  }
};

void finish(Shape &s, const Affine &m, const std::string &name, std::vector<Mask> &out) {
  if (s.pts.size() >= 2) {
    // A closing segment that ends on the start point: merge the two points.
    Vec2 d = s.pts.back() - s.pts.front();
    if (s.pts.size() > 2 && std::fabs(d.x) < 1e-3f && std::fabs(d.y) < 1e-3f) {
      s.cin.front() = s.cin.back();
      s.pts.pop_back();
      s.cin.pop_back();
      s.cout.pop_back();
    }
  }
  if (s.pts.size() >= 3) {
    Mask mk;
    mk.name = name.empty() ? "SVG " + std::to_string(out.size() + 1) : name;
    bool finite = true;
    for (size_t i = 0; i < s.pts.size(); ++i) {
      Vec2 p = m.apply(s.pts[i]);
      mk.points.push_back(p);
      mk.tanIn.push_back(m.apply(s.cin[i]) - p);
      mk.tanOut.push_back(m.apply(s.cout[i]) - p);
      for (Vec2 v : {p, mk.tanIn.back(), mk.tanOut.back()}) finite = finite && std::isfinite(v.x) && std::isfinite(v.y);
    }
    if (finite) out.push_back(mk); // "inf" / "nan" coordinates: not a usable shape
  }
  s = {};
}

// Elliptical arc (SVG endpoint form) as cubic segments.
void arcTo(Shape &s, Vec2 p0, float rx, float ry, float phiDeg, bool large, bool sweep, Vec2 p1) {
  if (rx == 0 || ry == 0) { s.lineTo(p1); return; }
  rx = std::fabs(rx); ry = std::fabs(ry);
  float phi = phiDeg * 3.14159265f / 180.0f, cs = std::cos(phi), sn = std::sin(phi);
  float dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
  float x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
  float lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lam > 1) { rx *= std::sqrt(lam); ry *= std::sqrt(lam); }
  float num_ = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  float k = std::sqrt(std::max(0.0f, num_ / std::max(den, 1e-12f))) * (large == sweep ? -1.0f : 1.0f);
  float cxp = k * rx * y1 / ry, cyp = -k * ry * x1 / rx;
  float cx = cs * cxp - sn * cyp + (p0.x + p1.x) / 2, cy = sn * cxp + cs * cyp + (p0.y + p1.y) / 2;
  auto ang = [](float ux, float uy, float vx, float vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  float t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
  float dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
  if (!sweep && dt > 0) dt -= 2 * 3.14159265f;
  if (sweep && dt < 0) dt += 2 * 3.14159265f;
  int segs = std::max(1, int(std::ceil(std::fabs(dt) / (3.14159265f / 2))));
  float step = dt / float(segs), alpha = 4.0f / 3.0f * std::tan(step / 4);
  auto pt = [&](float t) {
    return Vec2{cx + rx * std::cos(t) * cs - ry * std::sin(t) * sn, cy + rx * std::cos(t) * sn + ry * std::sin(t) * cs};
  };
  auto der = [&](float t) {
    return Vec2{-rx * std::sin(t) * cs - ry * std::cos(t) * sn, -rx * std::sin(t) * sn + ry * std::cos(t) * cs};
  };
  for (int i = 0; i < segs; ++i) {
    float a = t1 + step * float(i), b = a + step;
    Vec2 pa = pt(a), pb = i + 1 == segs ? p1 : pt(b);
    s.cubicTo(pa + der(a) * alpha, pb - der(b) * alpha, pb);
  }
}

void parsePath(const std::string &d, const Affine &m, const std::string &name, std::vector<Mask> &out) {
  Shape s;
  Vec2 cur, start, lastCtrl;
  char cmd = 0, prevCmd = 0;
  const char *p = d.c_str();
  auto skip = [&] { while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p; };
  auto readNum = [&](float &v) {
    skip();
    char *end = nullptr;
    v = std::strtof(p, &end);
    if (end == p) return false;
    p = end;
    return true;
  };
  auto readFlag = [&](bool &f) { // flags may be written without separators ("a1 1 0 01 ...")
    skip();
    if (*p != '0' && *p != '1') return false;
    f = *p == '1';
    ++p;
    return true;
  };
  while (true) {
    skip();
    if (!*p) break;
    if (std::isalpha(static_cast<unsigned char>(*p))) cmd = *p++;
    else if (!cmd) break;
    bool rel = std::islower(static_cast<unsigned char>(cmd));
    Vec2 o = rel ? cur : Vec2{};
    char up = char(std::toupper(static_cast<unsigned char>(cmd)));
    if (s.pts.empty() && up != 'M' && up != 'Z') s.moveTo(cur);
    float x, y, x1, y1, x2, y2;
    switch (std::toupper(static_cast<unsigned char>(cmd))) {
    case 'M':
      if (!readNum(x) || !readNum(y)) return;
      if (!s.pts.empty()) finish(s, m, name, out);
      cur = start = o + Vec2{x, y};
      s.moveTo(cur);
      cmd = rel ? 'l' : 'L'; // following pairs are line-to
      break;
    case 'L':
      if (!readNum(x) || !readNum(y)) return;
      cur = o + Vec2{x, y};
      s.lineTo(cur);
      break;
    case 'H':
      if (!readNum(x)) return;
      cur = {rel ? cur.x + x : x, cur.y};
      s.lineTo(cur);
      break;
    case 'V':
      if (!readNum(y)) return;
      cur = {cur.x, rel ? cur.y + y : y};
      s.lineTo(cur);
      break;
    case 'C':
      if (!readNum(x1) || !readNum(y1) || !readNum(x2) || !readNum(y2) || !readNum(x) || !readNum(y)) return;
      lastCtrl = o + Vec2{x2, y2};
      s.cubicTo(o + Vec2{x1, y1}, lastCtrl, o + Vec2{x, y});
      cur = o + Vec2{x, y};
      break;
    case 'S': {
      if (!readNum(x2) || !readNum(y2) || !readNum(x) || !readNum(y)) return;
      char pc = char(std::toupper(static_cast<unsigned char>(prevCmd)));
      Vec2 c1 = (pc == 'C' || pc == 'S') ? cur * 2.0f - lastCtrl : cur;
      lastCtrl = o + Vec2{x2, y2};
      s.cubicTo(c1, lastCtrl, o + Vec2{x, y});
      cur = o + Vec2{x, y};
      break;
    }
    case 'Q':
    case 'T': {
      Vec2 q;
      bool smooth = std::toupper(static_cast<unsigned char>(cmd)) == 'T';
      if (smooth) {
        char pc = char(std::toupper(static_cast<unsigned char>(prevCmd)));
        q = (pc == 'Q' || pc == 'T') ? cur * 2.0f - lastCtrl : cur;
      } else {
        if (!readNum(x1) || !readNum(y1)) return;
        q = o + Vec2{x1, y1};
      }
      if (!readNum(x) || !readNum(y)) return;
      Vec2 end = o + Vec2{x, y};
      // Quadratic -> cubic.
      s.cubicTo(cur + (q - cur) * (2.0f / 3.0f), end + (q - end) * (2.0f / 3.0f), end);
      lastCtrl = q;
      cur = end;
      break;
    }
    case 'A': {
      float rx, ry, rot;
      bool large, sweep;
      if (!readNum(rx) || !readNum(ry) || !readNum(rot) || !readFlag(large) || !readFlag(sweep) ||
          !readNum(x) || !readNum(y))
        return;
      Vec2 end = o + Vec2{x, y};
      arcTo(s, cur, rx, ry, rot, large, sweep, end);
      cur = end;
      break;
    }
    case 'Z':
      finish(s, m, name, out); // a following command starts a new subpath at `start`
      cur = start;
      cmd = 0;
      break;
    default:
      return;
    }
    prevCmd = cmd ? cmd : 'Z';
  }
  finish(s, m, name, out);
}

void ellipse(float cx, float cy, float rx, float ry, const Affine &m, const std::string &name,
             std::vector<Mask> &out) {
  const float k = 0.5522847f;
  Shape s;
  s.moveTo({cx + rx, cy});
  s.cubicTo({cx + rx, cy + ry * k}, {cx + rx * k, cy + ry}, {cx, cy + ry});
  s.cubicTo({cx - rx * k, cy + ry}, {cx - rx, cy + ry * k}, {cx - rx, cy});
  s.cubicTo({cx - rx, cy - ry * k}, {cx - rx * k, cy - ry}, {cx, cy - ry});
  s.cubicTo({cx + rx * k, cy - ry}, {cx + rx, cy - ry * k}, {cx + rx, cy});
  finish(s, m, name, out);
}

} // namespace

std::string masksToSvg(const std::vector<Mask> &masks, float w, float h) {
  std::string out;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\">\n",
                w, h, w, h);
  out += buf;
  for (auto &m : masks) {
    if (!m.enabled || m.points.size() < 3) continue;
    std::string name;
    for (char c : m.name) name += (c == '"' || c == '<' || c == '&') ? '_' : c;
    out += "  <path id=\"" + name + "\" fill=\"" + (m.subtract ? "black" : "white") + "\" d=\"";
    size_t n = m.points.size();
    Vec2 c = maskCenter(m);
    auto P = [&](Vec2 p) { return c + (p - c) * m.scale; };
    std::snprintf(buf, sizeof(buf), "M%g %g", P(m.points[0]).x, P(m.points[0]).y);
    out += buf;
    for (size_t i = 0; i < n; ++i) {
      size_t j = (i + 1) % n;
      Vec2 to = i < m.tanOut.size() ? m.tanOut[i] : Vec2{}, ti = j < m.tanIn.size() ? m.tanIn[j] : Vec2{};
      bool curve = to.x != 0 || to.y != 0 || ti.x != 0 || ti.y != 0;
      Vec2 a = P(m.points[i] + to), b = P(m.points[j] + ti), e = P(m.points[j]);
      if (curve) std::snprintf(buf, sizeof(buf), " C%g %g %g %g %g %g", a.x, a.y, b.x, b.y, e.x, e.y);
      else if (j != 0) std::snprintf(buf, sizeof(buf), " L%g %g", e.x, e.y);
      else buf[0] = 0;
      out += buf;
    }
    out += " Z\"/>\n";
  }
  out += "</svg>\n";
  return out;
}

std::vector<Mask> masksFromSvg(const std::string &svg, float w, float h, std::string *error) {
  std::vector<Mask> out;
  std::vector<Affine> stack{Affine{}};
  bool rootSeen = false;
  size_t pos = 0;
  int skipDepth = 0;
  while ((pos = svg.find('<', pos)) != std::string::npos) {
    if (svg.compare(pos, 4, "<!--") == 0) {
      size_t end = svg.find("-->", pos);
      pos = end == std::string::npos ? svg.size() : end + 3;
      continue;
    }
    size_t end = svg.find('>', pos);
    if (end == std::string::npos) break;
    std::string tag = svg.substr(pos + 1, end - pos - 1);
    pos = end + 1;
    if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;
    bool closing = tag[0] == '/';
    bool selfClosing = !tag.empty() && tag.back() == '/';
    std::string name;
    for (size_t i = closing ? 1 : 0; i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '/'; ++i)
      name += tag[i];
    // Shapes inside <defs>, <clipPath>, <mask>... are not drawn (an artboard's
    // clipPath would become a mask covering the whole layer).
    bool hidden = name == "defs" || name == "clipPath" || name == "mask" || name == "symbol" ||
                  name == "pattern" || name == "marker";
    if (closing) {
      if ((name == "g" || name == "svg") && stack.size() > 1) stack.pop_back();
      if (hidden && skipDepth > 0) --skipDepth;
      continue;
    }
    if (hidden) {
      if (!selfClosing) ++skipDepth;
      continue;
    }
    Affine m = stack.back() * parseTransform(attr(tag, "transform"));
    if (name == "svg") {
      Affine root;
      auto vb = numbers(attr(tag, "viewBox"));
      if (!rootSeen) {
        if (vb.size() == 4 && vb[2] > 0 && vb[3] > 0) {
          root = {w / vb[2], 0, 0, h / vb[3], -vb[0] * w / vb[2], -vb[1] * h / vb[3]};
        } else {
          float sw = num(tag, "width"), sh = num(tag, "height");
          if (sw > 0 && sh > 0) root = {w / sw, 0, 0, h / sh, 0, 0};
        }
        rootSeen = true;
      }
      if (!selfClosing) stack.push_back(root * m);
      continue;
    }
    if (name == "g") {
      if (!selfClosing) stack.push_back(m);
      continue;
    }
    if (skipDepth > 0) continue;
    std::string label = attr(tag, "inkscape:label");
    if (label.empty()) label = attr(tag, "id");
    if (name == "path") {
      parsePath(attr(tag, "d"), m, label, out);
    } else if (name == "polygon" || name == "polyline") {
      auto v = numbers(attr(tag, "points"));
      Shape s;
      for (size_t i = 0; i + 1 < v.size(); i += 2) {
        if (i == 0) s.moveTo({v[i], v[i + 1]});
        else s.lineTo({v[i], v[i + 1]});
      }
      finish(s, m, label, out);
    } else if (name == "rect") {
      float x = num(tag, "x"), y = num(tag, "y"), rw = num(tag, "width"), rh = num(tag, "height");
      if (rw > 0 && rh > 0) {
        Shape s;
        s.moveTo({x, y});
        s.lineTo({x + rw, y});
        s.lineTo({x + rw, y + rh});
        s.lineTo({x, y + rh});
        finish(s, m, label, out);
      }
    } else if (name == "circle") {
      float r = num(tag, "r");
      if (r > 0) ellipse(num(tag, "cx"), num(tag, "cy"), r, r, m, label, out);
    } else if (name == "ellipse") {
      float rx = num(tag, "rx"), ry = num(tag, "ry");
      if (rx > 0 && ry > 0) ellipse(num(tag, "cx"), num(tag, "cy"), rx, ry, m, label, out);
    }
  }
  if (out.empty() && error) *error = "nenhuma forma fechada no SVG";
  return out;
}

} // namespace lumen
