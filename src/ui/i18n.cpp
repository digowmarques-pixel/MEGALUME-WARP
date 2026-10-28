#include "ui/i18n.h"

#include "core/machine_settings.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lumen {

namespace {

Language g_lang = Language::Portuguese;

const std::unordered_map<std::string, std::string> &english() {
  static const std::unordered_map<std::string, std::string> dict = {
#include "ui/lang_en.inc"
  };
  return dict;
}

// Literal pointers are stable: cache the result per pointer, so a frame costs
// one pointer lookup per label.
std::unordered_map<const char *, const char *> g_cache, g_cacheZ;
std::deque<std::string> g_storage;

std::string translateOne(const std::string &s) {
  size_t id = s.find("##");
  std::string text = id == std::string::npos ? s : s.substr(0, id);
  auto it = english().find(text);
  if (it == english().end()) return s;
  return id == std::string::npos ? it->second : it->second + s.substr(id);
}

Language systemLanguage() {
#ifdef _WIN32
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
    return (name[0] == L'p' && name[1] == L't') ? Language::Portuguese : Language::English;
  return Language::Portuguese;
#else
  for (const char *var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char *v = std::getenv(var);
    if (v && *v && std::strcmp(v, "C") != 0 && std::strcmp(v, "POSIX") != 0)
      return std::strncmp(v, "pt", 2) == 0 ? Language::Portuguese : Language::English;
  }
  return Language::Portuguese;
#endif
}

} // namespace

Language language() { return g_lang; }

void setLanguage(Language l) { g_lang = l; }

void loadLanguage() {
  std::string l = machineSetting("language");
  g_lang = l.empty() ? systemLanguage() : l == "en" ? Language::English : Language::Portuguese;
}

void saveLanguage() { setMachineSetting("language", g_lang == Language::English ? "en" : "pt"); }

const char *T(const char *pt) {
  if (g_lang == Language::Portuguese || !pt) return pt;
  auto it = g_cache.find(pt);
  if (it != g_cache.end()) return it->second;
  std::string s = translateOne(pt);
  const char *out = pt;
  if (s != pt) out = g_storage.emplace_back(std::move(s)).c_str();
  g_cache.emplace(pt, out);
  return out;
}

const char *TZ(const char *pt) {
  if (g_lang == Language::Portuguese || !pt) return pt;
  auto it = g_cacheZ.find(pt);
  if (it != g_cacheZ.end()) return it->second;
  std::string out;
  for (const char *p = pt; *p; p += std::strlen(p) + 1) {
    out += translateOne(p);
    out.push_back('\0');
  }
  out.push_back('\0');
  const char *r = g_storage.emplace_back(std::move(out)).data();
  g_cacheZ.emplace(pt, r);
  return r;
}

} // namespace lumen
