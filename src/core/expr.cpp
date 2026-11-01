#include "core/expr.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace lumen {

namespace {

// Recursive descent: sum := product (('+'|'-') product)*; product := unary
// (('*'|'/') unary)*; unary := ('-'|'+') unary | number | '(' sum ')'.
struct Parser {
  const char *p;
  bool ok = true;
  void skip() { while (*p == ' ' || *p == '\t') ++p; }
  double sum() {
    double v = product();
    for (;;) {
      skip();
      if (*p == '+') { ++p; v += product(); }
      else if (*p == '-') { ++p; v -= product(); }
      else return v;
    }
  }
  double product() {
    double v = unary();
    for (;;) {
      skip();
      if (*p == '*' || *p == 'x' || *p == 'X') { ++p; v *= unary(); }
      else if (*p == '/') {
        ++p;
        double d = unary();
        if (d == 0) { ok = false; return 0; }
        v /= d;
      } else return v;
    }
  }
  double unary() {
    skip();
    if (*p == '-') { ++p; return -unary(); }
    if (*p == '+') { ++p; return unary(); }
    if (*p == '(') {
      ++p;
      double v = sum();
      skip();
      if (*p != ')') { ok = false; return 0; }
      ++p;
      return v;
    }
    // Numbers with a dot or a comma as decimal separator.
    std::string num;
    while (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' || *p == ',') {
      num += *p == ',' ? '.' : *p;
      ++p;
    }
    if (num.empty()) { ok = false; return 0; }
    char *end = nullptr;
    double v = std::strtod(num.c_str(), &end);
    if (end == num.c_str() || *end) ok = false;
    return v;
  }
};

} // namespace

bool evalNumber(const std::string &text, double current, double &out) {
  std::string t = text;
  size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
  if (a == std::string::npos) return false;
  t = t.substr(a, b - a + 1);
  // "/3", "*2", "+100": an operation on the current value. "-10" stays a number.
  char op = t[0];
  bool relative = op == '*' || op == '/' || op == 'x' || op == 'X' || (op == '+' && t.size() > 1);
  Parser ps{relative ? t.c_str() + 1 : t.c_str()};
  double v = ps.sum();
  ps.skip();
  if (!ps.ok || *ps.p || !std::isfinite(v)) return false;
  if (!relative) {
    out = v;
    return true;
  }
  if (op == '/' && v == 0) return false;
  out = op == '+' ? current + v : op == '/' ? current / v : current * v;
  return std::isfinite(out);
}

} // namespace lumen
