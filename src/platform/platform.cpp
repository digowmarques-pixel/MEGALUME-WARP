#include "platform/platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <csignal>
#include <exception>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace lumen::platform {

std::filesystem::path toPath(const std::string &utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string toUtf8(const std::filesystem::path &p) {
  auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

bool readFile(const std::string &path, std::string &out) {
  std::ifstream f(toPath(path), std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool writeFile(const std::string &path, const std::string &data) {
  // Write to a temp file then rename so a crash never leaves a half-written project.
  auto target = toPath(path);
  auto tmp = target;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    if (!f) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  return !ec;
}

#ifdef _WIN32

static std::wstring widen(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

static std::string narrow(const std::wstring &w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// "Name|*.a;*.b|Other|*.c" -> double-null-terminated Win32 filter.
static std::wstring win32Filter(const std::string &filter) {
  std::wstring f = widen(filter);
  for (auto &c : f)
    if (c == L'|') c = L'\0';
  f.push_back(L'\0');
  f.push_back(L'\0');
  return f;
}

std::vector<std::string> openFileDialog(const std::string &title, const std::string &filter,
                                        bool multiple) {
  std::wstring buffer(64 * 1024, L'\0');
  std::wstring wfilter = win32Filter(filter), wtitle = widen(title);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = wfilter.c_str();
  ofn.lpstrFile = buffer.data();
  ofn.nMaxFile = DWORD(buffer.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
  if (multiple) ofn.Flags |= OFN_ALLOWMULTISELECT;
  if (!GetOpenFileNameW(&ofn)) return {};

  std::vector<std::string> result;
  const wchar_t *p = buffer.c_str();
  std::wstring first = p;
  p += first.size() + 1;
  if (*p == L'\0') { // single selection: full path
    result.push_back(narrow(first));
  } else { // directory followed by file names
    while (*p) {
      std::wstring name = p;
      result.push_back(narrow(first + L"\\" + name));
      p += name.size() + 1;
    }
  }
  return result;
}

std::string saveFileDialog(const std::string &title, const std::string &filter,
                           const std::string &defaultName) {
  std::wstring buffer(4096, L'\0');
  std::wstring def = widen(defaultName);
  std::copy(def.begin(), def.end(), buffer.begin());
  std::wstring wfilter = win32Filter(filter), wtitle = widen(title);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = wfilter.c_str();
  ofn.lpstrFile = buffer.data();
  ofn.nMaxFile = DWORD(buffer.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.lpstrDefExt = L"lumen";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn)) return {};
  return narrow(buffer.c_str());
}

bool hasNativeDialogs() { return true; }

std::vector<std::string> utf8Args(int, char **) {
  std::vector<std::string> out;
  int n = 0;
  LPWSTR *w = CommandLineToArgvW(GetCommandLineW(), &n);
  for (int i = 0; i < n; ++i) out.push_back(narrow(w[i]));
  LocalFree(w);
  return out;
}

static std::wstring g_logPath;

static std::wstring logPathW() {
  if (!g_logPath.empty()) return g_logPath;
  // Next to the exe when writable, otherwise in %TEMP%.
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring dir = std::filesystem::path(std::wstring(buf, n)).parent_path().wstring();
  std::wstring path = dir + L"\\lumen.log";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    path = std::wstring(tmp) + L"lumen.log";
  } else {
    CloseHandle(h);
  }
  g_logPath = path;
  return path;
}

std::string logPath() { return narrow(logPathW()); }

void fatalError(const std::string &message) {
  std::fprintf(stderr, "[fatal] %s\n", message.c_str());
  std::fflush(stderr);
  std::wstring text = widen(message + "\n\nDetalhes em: " + logPath());
  MessageBoxW(nullptr, text.c_str(), L"Lumen", MB_OK | MB_ICONERROR);
}

static LONG WINAPI onCrash(EXCEPTION_POINTERS *info) {
  const EXCEPTION_RECORD *r = info->ExceptionRecord;
  // Which module (exe or DLL) contains the faulting address.
  HMODULE mod = nullptr;
  wchar_t modName[MAX_PATH] = L"?";
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(r->ExceptionAddress), &mod))
    GetModuleFileNameW(mod, modName, MAX_PATH);
  char msg[1024];
  std::snprintf(msg, sizeof(msg), "O Lumen travou (exceção 0x%08lX em %p, módulo %s + 0x%llx).",
                static_cast<unsigned long>(r->ExceptionCode), r->ExceptionAddress,
                narrow(std::filesystem::path(modName).filename().wstring()).c_str(),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r->ExceptionAddress) -
                                                reinterpret_cast<uintptr_t>(mod)));
  fatalError(msg);
  return EXCEPTION_EXECUTE_HANDLER;
}

void attachConsole() {
  // Lumen is a GUI-subsystem program: when started from a terminal, print logs
  // there; otherwise write them to lumen.log so failures can be diagnosed.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
  } else {
    // One file, both streams in append mode so neither overwrites the other.
    std::wstring path = logPathW();
    _wfreopen(path.c_str(), L"a", stdout);
    _wfreopen(path.c_str(), L"a", stderr);
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  SetUnhandledExceptionFilter(onCrash);
  std::signal(SIGABRT, [](int) {
    fatalError("O Lumen foi interrompido (abort). Veja o final do log.");
    _exit(3);
  });
  std::set_terminate([] {
    std::string what = "erro interno";
    if (auto e = std::current_exception()) {
      try { std::rethrow_exception(e); }
      catch (const std::exception &ex) { what = ex.what(); }
      catch (...) {}
    }
    fatalError("O Lumen encontrou um erro e precisa fechar: " + what);
    std::abort();
  });
}

std::string executableDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return toUtf8(std::filesystem::path(std::wstring(buf, n)).parent_path());
}

#else // Linux / macOS fallback via zenity, mostly for development.

static std::string runCapture(const std::string &cmd) {
  std::string out;
  FILE *f = popen(cmd.c_str(), "r");
  if (!f) return out;
  char buf[4096];
  while (fgets(buf, sizeof(buf), f)) out += buf;
  pclose(f);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

bool hasNativeDialogs() {
  static int has = -1;
  if (has < 0) has = std::system("command -v zenity >/dev/null 2>&1") == 0 ? 1 : 0;
  return has == 1;
}

std::vector<std::string> openFileDialog(const std::string &title, const std::string &,
                                        bool multiple) {
  if (!hasNativeDialogs()) return {};
  std::string cmd = "zenity --file-selection --separator='\n' --title='" + title + "'";
  if (multiple) cmd += " --multiple";
  std::string out = runCapture(cmd + " 2>/dev/null");
  std::vector<std::string> result;
  std::stringstream ss(out);
  for (std::string line; std::getline(ss, line);)
    if (!line.empty()) result.push_back(line);
  return result;
}

std::string saveFileDialog(const std::string &title, const std::string &,
                           const std::string &defaultName) {
  if (!hasNativeDialogs()) return {};
  return runCapture("zenity --file-selection --save --confirm-overwrite --title='" + title +
                    "' --filename='" + defaultName + "' 2>/dev/null");
}

void attachConsole() {}

std::string logPath() { return ""; }

void fatalError(const std::string &message) {
  std::fprintf(stderr, "[fatal] %s\n", message.c_str());
}

std::vector<std::string> utf8Args(int argc, char **argv) {
  return std::vector<std::string>(argv, argv + argc);
}

std::string executableDir() {
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::string(".") : toUtf8(p.parent_path());
}

#endif

} // namespace lumen::platform

// Called by IM_ASSERT (see lumen_imconfig.h).
void lumenAssertFail(const char *expr, const char *file, int line) {
  char msg[1024];
  std::snprintf(msg, sizeof(msg), "Falha interna da interface: %s\n(%s:%d)", expr, file, line);
  lumen::platform::fatalError(msg);
  std::abort();
}
