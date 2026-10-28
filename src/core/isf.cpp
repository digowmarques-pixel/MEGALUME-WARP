#include "core/isf.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace lumen {

using json = nlohmann::json;

int IsfInput::size() const {
  if (type == "color") return 4;
  if (type == "point2D") return 2;
  if (type == "image" || type == "audio" || type == "audioFFT") return 0;
  return 1;
}

const IsfInput *IsfInfo::input(const std::string &name) const {
  for (auto &i : inputs)
    if (i.name == name) return &i;
  return nullptr;
}

static std::vector<float> floats(const json &j) {
  std::vector<float> v;
  if (j.is_array()) {
    for (auto &x : j)
      if (x.is_number()) v.push_back(x.get<float>());
  } else if (j.is_number()) {
    v.push_back(j.get<float>());
  } else if (j.is_boolean()) {
    v.push_back(j.get<bool>() ? 1.0f : 0.0f);
  }
  return v;
}

static bool parseIsfHeader(const std::string &src, IsfInfo &out, std::string *error);

// A header with a value of the wrong type ("LABEL": null, a number as NAME)
// is an invalid file, not a crash: effects folders hold downloaded shaders.
bool parseIsf(const std::string &src, IsfInfo &out, std::string *error) {
  try {
    return parseIsfHeader(src, out, error);
  } catch (const std::exception &e) {
    out = {};
    if (error) *error = std::string("cabeçalho ISF com valor de tipo errado: ") + e.what();
    return false;
  }
}

static bool parseIsfHeader(const std::string &src, IsfInfo &out, std::string *error) {
  out = {};
  size_t start = src.find("/*");
  size_t end = start == std::string::npos ? std::string::npos : src.find("*/", start + 2);
  // The header must be the first thing in the file (only whitespace before).
  bool atTop = start != std::string::npos &&
               std::all_of(src.begin(), src.begin() + long(start), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
  if (!atTop || end == std::string::npos) {
    if (error) *error = "sem cabeçalho ISF (comentário JSON no início do arquivo)";
    return false;
  }
  json j = json::parse(src.begin() + long(start) + 2, src.begin() + long(end), nullptr, false, true);
  if (j.is_discarded() || !j.is_object()) {
    if (error) *error = "cabeçalho ISF com JSON inválido";
    return false;
  }
  out.description = j.value("DESCRIPTION", "");
  out.credit = j.value("CREDIT", "");
  if (j.contains("CATEGORIES") && j["CATEGORIES"].is_array())
    for (auto &c : j["CATEGORIES"])
      if (c.is_string()) out.categories.push_back(c.get<std::string>());
  if (j.contains("INPUTS") && j["INPUTS"].is_array())
    for (auto &x : j["INPUTS"]) {
      if (!x.is_object() || !x.contains("NAME") || !x.contains("TYPE")) continue;
      IsfInput in;
      in.name = x["NAME"].get<std::string>();
      in.type = x["TYPE"].get<std::string>();
      in.label = x.value("LABEL", in.name);
      if (x.contains("DEFAULT")) in.def = floats(x["DEFAULT"]);
      if (x.contains("MIN")) in.min = floats(x["MIN"]);
      if (x.contains("MAX")) in.max = floats(x["MAX"]);
      if (x.contains("VALUES") && x["VALUES"].is_array())
        for (auto &v : x["VALUES"])
          if (v.is_number()) in.values.push_back(int(std::clamp(v.get<double>(), -1e9, 1e9)));
      if (x.contains("LABELS") && x["LABELS"].is_array())
        for (auto &v : x["LABELS"])
          if (v.is_string()) in.labels.push_back(v.get<std::string>());
      out.inputs.push_back(in);
    }
  if (j.contains("PASSES") && j["PASSES"].is_array())
    for (auto &x : j["PASSES"]) {
      if (!x.is_object()) continue;
      IsfPass p;
      p.target = x.value("TARGET", "");
      p.persistent = x.contains("PERSISTENT") && (x["PERSISTENT"].is_boolean() ? x["PERSISTENT"].get<bool>()
                                                  : x["PERSISTENT"].is_number() && x["PERSISTENT"].get<float>() != 0);
      p.floatTarget = x.contains("FLOAT") && (x["FLOAT"].is_boolean() ? x["FLOAT"].get<bool>()
                                              : x["FLOAT"].is_number() && x["FLOAT"].get<float>() != 0);
      auto dim = [](const json &v) { return v.is_string() ? v.get<std::string>() : v.is_number() ? std::to_string(v.get<float>()) : std::string(); };
      if (x.contains("WIDTH")) p.width = dim(x["WIDTH"]);
      if (x.contains("HEIGHT")) p.height = dim(x["HEIGHT"]);
      out.passes.push_back(p);
    }
  // ISF v1 "PERSISTENT_BUFFERS": names of targets that persist.
  if (j.contains("PERSISTENT_BUFFERS")) {
    auto &pb = j["PERSISTENT_BUFFERS"];
    auto mark = [&](const std::string &n) {
      for (auto &p : out.passes)
        if (p.target == n) p.persistent = true;
    };
    if (pb.is_array()) for (auto &n : pb) { if (n.is_string()) mark(n.get<std::string>()); }
    else if (pb.is_object()) for (auto &[n, v] : pb.items()) mark(n);
  }
  if (j.contains("IMPORTED")) {
    auto &im = j["IMPORTED"];
    if (im.is_object()) {
      for (auto &[n, v] : im.items())
        if (v.is_object() && v.contains("PATH") && v["PATH"].is_string()) out.imported.push_back({n, v["PATH"].get<std::string>()});
    } else if (im.is_array()) {
      for (auto &v : im)
        if (v.is_object() && v.contains("NAME") && v.contains("PATH") && v["PATH"].is_string())
          out.imported.push_back({v["NAME"].get<std::string>(), v["PATH"].get<std::string>()});
    }
  }
  if (out.passes.empty()) out.passes.push_back({});
  const IsfInput *img = out.input("inputImage");
  out.filter = img && img->type == "image";
  out.transition = out.input("startImage") && out.input("endImage") && out.input("progress");
  // Body: the file without the header; "#version" lines are ours to write.
  std::istringstream lines(src.substr(end + 2));
  for (std::string line; std::getline(lines, line);) {
    std::string t = line;
    t.erase(0, t.find_first_not_of(" \t"));
    if (t.rfind("#version", 0) == 0) continue;
    out.body += line + "\n";
  }
  return true;
}

const char *kIsfVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform vec2 uTarget;
out vec2 isf_FragNormCoord;
void main() {
  isf_FragNormCoord = aUV;
  vec2 ndc = aPos / uTarget * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

std::string isfFragmentSource(const IsfInfo &info) {
  std::string s = R"(#version 330 core
out vec4 isf_FragColor;
in vec2 isf_FragNormCoord;
uniform int PASSINDEX;
uniform vec2 RENDERSIZE;
uniform float TIME;
uniform float TIMEDELTA;
uniform int FRAMEINDEX;
uniform vec4 DATE;
#define gl_FragColor isf_FragColor
#define texture2D texture
#define texture2DRect texture
#define varying in
#define vv_FragNormCoord isf_FragNormCoord
#define IMG_NORM_PIXEL(img, c) texture(img, c)
#define IMG_PIXEL(img, c) texture(img, (c) / vec2(textureSize(img, 0)))
#define IMG_THIS_PIXEL(img) texture(img, isf_FragNormCoord)
#define IMG_THIS_NORM_PIXEL(img) texture(img, isf_FragNormCoord)
#define IMG_SIZE(img) vec2(textureSize(img, 0))
)";
  for (auto &in : info.inputs) {
    const std::string &t = in.type;
    if (t == "float") s += "uniform float " + in.name + ";\n";
    else if (t == "bool" || t == "event") s += "uniform bool " + in.name + ";\n";
    else if (t == "long") s += "uniform int " + in.name + ";\n";
    else if (t == "color") s += "uniform vec4 " + in.name + ";\n";
    else if (t == "point2D") s += "uniform vec2 " + in.name + ";\n";
    else if (t == "image" || t == "audio" || t == "audioFFT") s += "uniform sampler2D " + in.name + ";\n";
  }
  std::vector<std::string> images;
  for (auto &in : info.inputs)
    if (in.type == "image" || in.type == "audio" || in.type == "audioFFT") images.push_back(in.name);
  for (auto &[n, path] : info.imported) {
    s += "uniform sampler2D " + n + ";\n";
    images.push_back(n);
  }
  for (auto &p : info.passes)
    if (!p.target.empty() && std::find(images.begin(), images.end(), p.target) == images.end()) {
      s += "uniform sampler2D " + p.target + ";\n";
      images.push_back(p.target);
    }
  // Per-image helpers some shaders use directly.
  for (auto &n : images)
    s += "uniform vec4 _" + n + "_imgRect;\nuniform vec2 _" + n + "_imgSize;\nuniform bool _" + n + "_flip;\n";
  s += "#line 1\n";
  s += info.body;
  return s;
}

namespace {

struct Parser {
  const std::string &s;
  const std::map<std::string, float> &vars;
  size_t i = 0;
  bool ok = true;

  void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
  }
  float expr() {
    float v = term();
    while (true) {
      if (eat('+')) v += term();
      else if (eat('-')) v -= term();
      else return v;
    }
  }
  float term() {
    float v = factor();
    while (true) {
      if (eat('*')) v *= factor();
      else if (eat('/')) { float d = factor(); v = d != 0 ? v / d : 0; }
      else return v;
    }
  }
  int depth = 0;
  float factor() {
    // Bounded nesting: a WIDTH full of "(" must not overflow the stack.
    if (depth >= 64) { ok = false; i = s.size(); return 0; }
    ++depth;
    float v = primary();
    --depth;
    return v;
  }
  float primary() {
    ws();
    if (eat('-')) return -factor();
    if (eat('+')) return factor();
    if (eat('(')) { float v = expr(); if (!eat(')')) ok = false; return v; }
    if (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
      size_t n = 0;
      float v = std::stof(s.substr(i), &n);
      i += n;
      return v;
    }
    std::string name;
    bool var = eat('$');
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) name += s[i++];
    if (name.empty()) { ok = false; return 0; }
    if (var) {
      auto it = vars.find(name);
      if (it == vars.end()) { ok = false; return 0; }
      return it->second;
    }
    // Function call.
    std::vector<float> args;
    if (!eat('(')) { ok = false; return 0; }
    if (!eat(')')) {
      do args.push_back(expr()); while (eat(','));
      if (!eat(')')) ok = false;
    }
    auto a = [&](size_t k) { return k < args.size() ? args[k] : 0.0f; };
    if (name == "floor") return std::floor(a(0));
    if (name == "ceil") return std::ceil(a(0));
    if (name == "round") return std::round(a(0));
    if (name == "abs") return std::fabs(a(0));
    if (name == "min") return std::min(a(0), a(1));
    if (name == "max") return std::max(a(0), a(1));
    if (name == "pow") return std::pow(a(0), a(1));
    if (name == "sqrt") return std::sqrt(std::max(0.0f, a(0)));
    ok = false;
    return 0;
  }
};

} // namespace

float isfEval(const std::string &expr, const std::map<std::string, float> &vars, bool *ok) {
  Parser p{expr, vars};
  float v = 0;
  try {
    v = p.expr();
    p.ws();
    if (p.i != expr.size()) p.ok = false;
  } catch (...) {
    p.ok = false;
  }
  if (ok) *ok = p.ok;
  return p.ok ? v : 0;
}

void isfDefaults(const IsfInfo &info, IsfValues &values) {
  for (auto &in : info.inputs) {
    int n = in.size();
    if (n == 0) continue;
    auto &v = values[in.name];
    if (int(v.size()) == n) continue;
    v = in.def;
    if (v.empty()) {
      if (in.type == "color") v = {1, 1, 1, 1};
      else if (in.type == "point2D") v = {0, 0};
      else if (in.type == "float" && !in.min.empty() && !in.max.empty()) v = {(in.min[0] + in.max[0]) * 0.5f};
      else if (in.type == "long" && !in.values.empty()) v = {float(in.values[0])};
      else v = {0};
    }
    v.resize(size_t(n), v.empty() ? 0.0f : v.back());
  }
}

} // namespace lumen
