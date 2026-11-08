#include "core/license.h"

#include "core/machine_settings.h"
#include "core/md5.h"
#include "platform/platform.h"

#include <monocypher/monocypher-ed25519.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <uuid/uuid.h>
#endif
#endif

namespace lumen {

// The public half of the author's key pair (the private half never leaves
// lumen-keygen's folder on the author's computer).
static const uint8_t kPublicKey[32] = {
#include "core/license_key.inc"
};

const uint8_t *builtinLicensePublicKey() { return kPublicKey; }

const char *licenseCheckText(LicenseCheck c) {
  switch (c) {
  case LicenseCheck::Ok: return "Licença válida";
  case LicenseCheck::Empty: return "Nenhuma licença";
  case LicenseCheck::Malformed: return "Texto de licença inválido (copie a licença inteira)";
  case LicenseCheck::BadSignature: return "Licença não reconhecida (assinatura inválida)";
  case LicenseCheck::OtherMachine: return "Esta licença é de outro computador";
  case LicenseCheck::Expired: return "Licença vencida";
  }
  return "";
}

// ---------------------------------------------------------------- base64

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string toBase64(const uint8_t *d, size_t n) { // URL-safe, no padding
  std::string out;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = uint32_t(d[i]) << 16 | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0u) | (i + 2 < n ? d[i + 2] : 0u);
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    if (i + 1 < n) out += kB64[(v >> 6) & 63];
    if (i + 2 < n) out += kB64[v & 63];
  }
  return out;
}

bool fromBase64(const std::string &s, std::vector<uint8_t> &out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '-' || c == '+') v = 62;
    else if (c == '_' || c == '/') v = 63;
    else if (c == '=') break;
    else return false;
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t((acc >> bits) & 0xFF));
    }
  }
  return true;
}

// ---------------------------------------------------------------- machine code

std::string machineCodeFrom(const std::string &osId) {
  std::string id;
  for (char c : osId)
    if (!std::isspace(static_cast<unsigned char>(c)) && c != '{' && c != '}') id += char(std::tolower(static_cast<unsigned char>(c)));
  std::string h = md5Hex("lumen-machine-v1:" + id);
  std::string out;
  for (int i = 0; i < 16; ++i) {
    if (i && i % 4 == 0) out += '-';
    out += char(std::toupper(static_cast<unsigned char>(h[size_t(i)])));
  }
  return out;
}

std::string normalizeMachineCode(const std::string &code) {
  std::string hex;
  for (char c : code)
    if (std::isxdigit(static_cast<unsigned char>(c))) hex += char(std::toupper(static_cast<unsigned char>(c)));
    else if (c != '-' && !std::isspace(static_cast<unsigned char>(c))) return {};
  if (hex.size() != 16) return {};
  return hex.substr(0, 4) + "-" + hex.substr(4, 4) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4);
}

static std::string osMachineId() {
#ifdef _WIN32
  wchar_t buf[128] = {};
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ, nullptr,
                   buf, &size) == ERROR_SUCCESS) {
    std::string s;
    for (const wchar_t *p = buf; *p; ++p) s += char(*p < 128 ? *p : '?');
    return s;
  }
  wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(name, &n)) {
    std::string s;
    for (const wchar_t *p = name; *p; ++p) s += char(*p < 128 ? *p : '?');
    return "name:" + s;
  }
  return "unknown";
#elif defined(__APPLE__)
  uuid_t id;
  timespec wait{1, 0};
  if (gethostuuid(id, &wait) == 0) {
    char s[37];
    uuid_unparse(id, s);
    return s;
  }
  char host[256] = {};
  gethostname(host, sizeof(host) - 1);
  return std::string("name:") + host;
#else
  for (const char *f : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
    std::string s;
    if (platform::readFile(f, s) && s.size() >= 16) return s;
  }
  char host[256] = {};
  gethostname(host, sizeof(host) - 1);
  return std::string("name:") + host;
#endif
}

std::string machineCode() { return machineCodeFrom(osMachineId()); }

// ---------------------------------------------------------------- dates

long long dayNumber(const std::string &ymd) {
  int y, m, d;
  if (ymd.size() != 10 || std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
  if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  long long yy = y - (m <= 2);
  const long long era = yy / 400;
  const unsigned yoe = unsigned(yy - era * 400);
  const unsigned doy = unsigned((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

std::string todayLocal() {
  std::time_t now = std::time(nullptr);
  std::tm lt{};
#ifdef _WIN32
  localtime_s(&lt, &now);
#else
  localtime_r(&now, &lt);
#endif
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  return buf;
}

// ---------------------------------------------------------------- licences

void licenseKeyPair(const uint8_t seed[32], uint8_t secretKey[64], uint8_t publicKey[32]) {
  uint8_t s[32];
  std::copy(seed, seed + 32, s);
  crypto_ed25519_key_pair(secretKey, publicKey, s); // wipes its copy of the seed
}

std::string makeLicense(const License &l, const uint8_t secretKey[64]) {
  nlohmann::json j = {{"v", 1}, {"n", l.name}, {"id", l.id}, {"i", l.issued}, {"e", l.expires}};
  j["m"] = nlohmann::json::array();
  for (auto &m : l.machines) j["m"].push_back(normalizeMachineCode(m));
  std::string payload = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  uint8_t sig[64];
  crypto_ed25519_sign(sig, secretKey, reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
  return "LUMEN1." + toBase64(reinterpret_cast<const uint8_t *>(payload.data()), payload.size()) + "." +
         toBase64(sig, sizeof(sig));
}

LicenseCheck checkLicense(const std::string &text, const std::string &machine, const std::string &today,
                          License &out, const uint8_t *publicKey) {
  out = {};
  // Only what lies between the marks (a pasted e-mail keeps its greeting).
  std::string compact;
  size_t at = text.find("LUMEN1.");
  if (at == std::string::npos) {
    for (char c : text)
      if (!std::isspace(static_cast<unsigned char>(c))) return LicenseCheck::Malformed;
    return LicenseCheck::Empty;
  }
  for (size_t i = at; i < text.size(); ++i) {
    char c = text[i];
    if (c == '-' && text.compare(i, 5, "-----") == 0) break; // -----END...
    if (!std::isspace(static_cast<unsigned char>(c))) compact += c;
  }
  size_t a = compact.find('.'), b = compact.find('.', a + 1);
  if (b == std::string::npos || compact.find('.', b + 1) != std::string::npos) return LicenseCheck::Malformed;
  std::vector<uint8_t> payload, sig;
  if (!fromBase64(compact.substr(a + 1, b - a - 1), payload) || !fromBase64(compact.substr(b + 1), sig) ||
      sig.size() != 64 || payload.empty())
    return LicenseCheck::Malformed;
  if (crypto_ed25519_check(sig.data(), publicKey ? publicKey : kPublicKey, payload.data(), payload.size()) != 0)
    return LicenseCheck::BadSignature;
  auto j = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
  if (!j.is_object() || j.value("v", 0) != 1) return LicenseCheck::Malformed;
  try {
    out.name = j.value("n", "");
    out.id = j.value("id", "");
    out.issued = j.value("i", "");
    out.expires = j.value("e", "");
    for (auto &m : j.value("m", nlohmann::json::array()))
      if (m.is_string()) out.machines.push_back(m.get<std::string>());
  } catch (const std::exception &) {
    return LicenseCheck::Malformed;
  }
  const std::string mine = normalizeMachineCode(machine);
  if (std::none_of(out.machines.begin(), out.machines.end(), [&](const std::string &m) { return normalizeMachineCode(m) == mine; }))
    return LicenseCheck::OtherMachine;
  if (!out.expires.empty()) {
    long long e = dayNumber(out.expires), t = dayNumber(today);
    if (e < 0 || (t >= 0 && t > e)) return LicenseCheck::Expired;
  }
  return LicenseCheck::Ok;
}

static std::string licenseFile() {
  return platform::toUtf8(platform::toPath(platform::settingsDir()) / "licenca.lumenlic");
}

std::string storedLicense() {
  std::string text = machineSetting("license");
  if (text.empty()) platform::readFile(licenseFile(), text); // the copy, if the settings were lost
  return text;
}

LicenseCheck installLicense(const std::string &text, License &out) {
  LicenseCheck c = checkLicense(text, machineCode(), todayLocal(), out);
  if (c != LicenseCheck::Ok) return c;
  std::string line;
  for (size_t i = text.find("LUMEN1."); i < text.size() && text.compare(i, 5, "-----") != 0; ++i)
    if (!std::isspace(static_cast<unsigned char>(text[i]))) line += text[i];
  setMachineSetting("license", line);
  platform::writeFile(licenseFile(), "-----BEGIN LUMEN LICENSE-----\n" + line + "\n-----END LUMEN LICENSE-----\n", true);
  return c;
}

// ---------------------------------------------------------------- trial

TrialInfo trialFrom(long long first, long long seen, long long today, bool tampered) {
  TrialInfo t;
  if (tampered || today < 0) {
    t.daysLeft = 0;
    t.expired = true;
    return t;
  }
  if (first < 0) first = today; // the first run
  // The clock set back (to stretch the trial): more than a day before the
  // latest day this computer has already seen.
  if (seen >= 0 && today < seen - 1) {
    t.daysLeft = 0;
    t.expired = true;
    return t;
  }
  long long used = std::max(seen, today) - first;
  t.daysLeft = int(std::clamp<long long>(kTrialDays - used, 0, kTrialDays));
  t.expired = t.daysLeft <= 0;
  return t;
}

// A record is "YYYY-MM-DD.tag": the tag ties it to the date and this computer,
// so a record edited by hand (a later first day) is noticed.
static std::string trialTag(const std::string &date, const std::string &machine) {
  return md5Hex("lumen-trial-v1|" + date + "|" + machine + "|b7c41e").substr(0, 16);
}
static long long readRecord(const std::string &rec, const std::string &machine, bool &tampered) {
  if (rec.empty()) return -1;
  size_t dot = rec.find('.');
  std::string date = rec.substr(0, dot);
  if (dot == std::string::npos || dayNumber(date) < 0 || rec.substr(dot + 1) != trialTag(date, machine)) {
    tampered = true;
    return -1;
  }
  return dayNumber(date);
}
static std::string record(const std::string &date, const std::string &machine) { return date + "." + trialTag(date, machine); }

#ifdef _WIN32
// A second copy in the registry: deleting the settings folder does not
// restart the trial.
static std::string regGet(const wchar_t *name) {
  wchar_t buf[64] = {};
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Lumen\\Avaliacao", name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
    return {};
  std::string s;
  for (const wchar_t *p = buf; *p; ++p) s += char(*p < 128 ? *p : '?');
  return s;
}
static void regSet(const wchar_t *name, const std::string &v) {
  std::wstring w(v.begin(), v.end());
  RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Lumen\\Avaliacao", name, REG_SZ, w.c_str(), DWORD((w.size() + 1) * sizeof(wchar_t)));
}
#else
static std::string hiddenFile() { return platform::toUtf8(platform::toPath(platform::settingsDir()) / ".avaliacao"); }
static std::string fileGet(int line) {
  std::string s;
  if (!platform::readFile(hiddenFile(), s)) return {};
  size_t nl = s.find('\n');
  std::string a = s.substr(0, nl), b = nl == std::string::npos ? std::string() : s.substr(nl + 1);
  while (!b.empty() && (b.back() == '\n' || b.back() == '\r')) b.pop_back();
  return line == 0 ? a : b;
}
#endif

TrialInfo trialCheck(const std::string &machine, const std::string &today) {
  bool tampered = false;
  std::vector<std::string> firsts = {machineSetting("trialFirst")}, seens = {machineSetting("trialSeen")};
#ifdef _WIN32
  firsts.push_back(regGet(L"a"));
  seens.push_back(regGet(L"b"));
#else
  firsts.push_back(fileGet(0));
  seens.push_back(fileGet(1));
#endif
  long long first = -1, seen = -1;
  for (auto &r : firsts) {
    long long d = readRecord(r, machine, tampered);
    if (d >= 0 && (first < 0 || d < first)) first = d; // the earliest one counts
  }
  for (auto &r : seens) seen = std::max(seen, readRecord(r, machine, tampered));
  const long long now = dayNumber(today);
  TrialInfo info = trialFrom(first, seen, now, tampered);
  if (!tampered && now >= 0) {
    // First day kept as it was; latest day moved forward.
    auto dateOf = [](long long day) {
      // Back from a day number to "YYYY-MM-DD" (civil_from_days).
      day += 719468;
      const long long era = (day >= 0 ? day : day - 146096) / 146097;
      const unsigned doe = unsigned(day - era * 146097);
      const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      const long long y = (long long)yoe + era * 400;
      const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      const unsigned mp = (5 * doy + 2) / 153;
      const unsigned d = doy - (153 * mp + 2) / 5 + 1;
      const unsigned m = mp < 10 ? mp + 3 : mp - 9;
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u", y + (m <= 2), m, d);
      return std::string(buf);
    };
    const std::string f = record(dateOf(first >= 0 ? first : now), machine);
    const std::string s = record(dateOf(std::max(seen, now)), machine);
    if (machineSetting("trialFirst") != f) setMachineSetting("trialFirst", f);
    if (machineSetting("trialSeen") != s) setMachineSetting("trialSeen", s);
#ifdef _WIN32
    if (regGet(L"a") != f) regSet(L"a", f);
    if (regGet(L"b") != s) regSet(L"b", s);
#else
    if (fileGet(0) != f || fileGet(1) != s) platform::writeFile(hiddenFile(), f + "\n" + s + "\n");
#endif
  }
  return info;
}

} // namespace lumen
