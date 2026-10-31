#include "platform/platform.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <csignal>
#include <exception>
#include <functional>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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

// Shell dialogs need a single-threaded COM apartment, but the main thread is
// already multithreaded (miniaudio's WASAPI initialises COM so, and a second
// CoInitializeEx fails): the folder picker then silently did nothing. They run
// on a thread of their own while the caller waits (they are modal anyway).
template <class R> static R onStaThread(const std::function<R()> &f) {
  R result{};
  std::thread([&] {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    result = f();
    if (SUCCEEDED(hr)) CoUninitialize();
  }).join();
  return result;
}

static std::vector<std::string> openFileDialogSta(const std::string &title, const std::string &filter,
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

std::vector<std::string> openFileDialog(const std::string &title, const std::string &filter,
                                        bool multiple) {
  return onStaThread<std::vector<std::string>>([&] { return openFileDialogSta(title, filter, multiple); });
}

static std::string saveFileDialogSta(const std::string &title, const std::string &filter,
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

std::string saveFileDialog(const std::string &title, const std::string &filter,
                           const std::string &defaultName) {
  return onStaThread<std::string>([&] { return saveFileDialogSta(title, filter, defaultName); });
}

bool hasNativeDialogs() { return true; }

void openInShell(const std::string &path) {
  ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::string folderDialogSta(const std::string &title) {
  std::wstring wtitle = widen(title);
  BROWSEINFOW bi{};
  bi.lpszTitle = wtitle.c_str();
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return {};
  wchar_t path[MAX_PATH * 4] = {};
  bool ok = SHGetPathFromIDListW(pidl, path);
  CoTaskMemFree(pidl);
  return ok ? narrow(path) : std::string();
}

std::string folderDialog(const std::string &title) {
  return onStaThread<std::string>([&] { return folderDialogSta(title); });
}

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
  // %APPDATA%\Lumen (the program folder is read-only when installed),
  // otherwise %TEMP%.
  std::wstring path = (toPath(settingsDir()) / L"lumen.log").wstring();
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
  // One box: an error already shown here is followed by abort(), whose
  // handler would show a second one.
  static std::atomic<bool> shown{false};
  if (shown.exchange(true)) return;
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
  // A minidump lets the crash be located after the fact (dbghelp is loaded
  // here, not linked: the crash path must not depend on it).
  std::wstring dump = widen(settingsDir()) + L"\\lumen-crash.dmp";
  if (HMODULE dbg = LoadLibraryW(L"dbghelp.dll")) {
    using WriteDump = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);
    auto write = reinterpret_cast<WriteDump>(reinterpret_cast<void *>(GetProcAddress(dbg, "MiniDumpWriteDump")));
    HANDLE f = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (write && f != INVALID_HANDLE_VALUE) {
      struct { DWORD thread; EXCEPTION_POINTERS *info; BOOL client; } ex{GetCurrentThreadId(), info, FALSE};
      if (write(GetCurrentProcess(), GetCurrentProcessId(), f, 0 /* MiniDumpNormal */, &ex, nullptr, nullptr))
        std::snprintf(msg + std::strlen(msg), sizeof(msg) - std::strlen(msg), "\nRelatório técnico: %s",
                      narrow(dump).c_str());
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  }
  fatalError(msg);
  return EXCEPTION_EXECUTE_HANDLER;
}

void keepRunningSmoothly() {
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
  // timeBeginPeriod from winmm, loaded so the core library needs no new import.
  if (HMODULE mm = LoadLibraryW(L"winmm.dll"))
    if (auto begin = reinterpret_cast<UINT(WINAPI *)(UINT)>(reinterpret_cast<void *>(GetProcAddress(mm, "timeBeginPeriod"))))
      begin(1);
  // Windows 11 slows down ("EcoQoS") processes whose window is not in front;
  // the outputs must keep their frame rate whatever window has the focus.
  struct { ULONG version, controlMask, stateMask; } throttle{1, 0x1 /* EXECUTION_SPEED */, 0};
  using SetInfo = BOOL(WINAPI *)(HANDLE, int, void *, DWORD);
  if (auto set = reinterpret_cast<SetInfo>(
          reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"))))
    set(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &throttle, sizeof(throttle));
  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
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

#else // macOS (osascript) and Linux (zenity)

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

#ifdef __APPLE__
// AppleScript string literal (titles and names are short UI strings).
static std::string asQuote(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\'') { out += "'\\''"; continue; } // we are inside a single-quoted shell arg
    out += c;
  }
  return out + "\"";
}
#endif

#ifndef __APPLE__
// A single-quoted shell argument: titles and default names may hold quotes.
static std::string shQuote(const std::string &s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}
#endif

bool hasNativeDialogs() {
#ifdef __APPLE__
  return true;
#else
  static int has = -1;
  if (has < 0) has = std::system("command -v zenity >/dev/null 2>&1") == 0 ? 1 : 0;
  return has == 1;
#endif
}

std::vector<std::string> openFileDialog(const std::string &title, const std::string &,
                                        bool multiple) {
  if (!hasNativeDialogs()) return {};
#ifdef __APPLE__
  std::string cmd = multiple
      ? "osascript -e 'set fs to choose file with prompt " + asQuote(title) +
            " with multiple selections allowed' -e 'set out to \"\"' -e 'repeat with f in fs' "
            "-e 'set out to out & POSIX path of f & linefeed' -e 'end repeat' -e 'return out'"
      : "osascript -e 'POSIX path of (choose file with prompt " + asQuote(title) + ")'";
#else
  std::string cmd = "zenity --file-selection --separator='\n' --title=" + shQuote(title);
  if (multiple) cmd += " --multiple";
#endif
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
#ifdef __APPLE__
  return runCapture("osascript -e 'POSIX path of (choose file name with prompt " + asQuote(title) +
                    " default name " + asQuote(defaultName) + ")' 2>/dev/null");
#else
  return runCapture("zenity --file-selection --save --confirm-overwrite --title=" + shQuote(title) +
                    " --filename=" + shQuote(defaultName) + " 2>/dev/null");
#endif
}

std::string folderDialog(const std::string &title) {
  if (!hasNativeDialogs()) return {};
#ifdef __APPLE__
  return runCapture("osascript -e 'POSIX path of (choose folder with prompt " + asQuote(title) + ")' 2>/dev/null");
#else
  return runCapture("zenity --file-selection --directory --title=" + shQuote(title) + " 2>/dev/null");
#endif
}

void openInShell(const std::string &path) {
  std::string quoted = "'";
  for (char c : path) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
  quoted += "'";
#ifdef __APPLE__
  runCapture("open " + quoted + " >/dev/null 2>&1");
#else
  runCapture("xdg-open " + quoted + " >/dev/null 2>&1 &");
#endif
}

void attachConsole() {}
void keepRunningSmoothly() {}

std::string logPath() { return ""; }

void fatalError(const std::string &message) {
  std::fprintf(stderr, "[fatal] %s\n", message.c_str());
#ifdef __APPLE__
  // Apps opened from Finder have no terminal: show the error.
  runCapture("osascript -e 'display alert \"Lumen\" message " + asQuote(message) + " as critical' 2>/dev/null");
#endif
}

std::vector<std::string> utf8Args(int argc, char **argv) {
  return std::vector<std::string>(argv, argv + argc);
}

std::string executableDir() {
#ifdef __APPLE__
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) return toUtf8(std::filesystem::path(buf).parent_path());
  return ".";
#else
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::string(".") : toUtf8(p.parent_path());
#endif
}

#endif

std::string settingsDir() {
  namespace fs = std::filesystem;
  fs::path dir;
#ifdef _WIN32
  if (const wchar_t *app = _wgetenv(L"APPDATA")) dir = fs::path(app) / L"Lumen";
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) dir = fs::path(home) / "Library/Application Support/Lumen";
#else
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) dir = fs::path(xdg) / "lumen";
  else if (const char *home = std::getenv("HOME")) dir = fs::path(home) / ".config/lumen";
#endif
  if (dir.empty()) dir = toPath(executableDir());
  std::error_code ec;
  fs::create_directories(dir, ec);
  return toUtf8(dir);
}

} // namespace lumen::platform

// Called by IM_ASSERT (see lumen_imconfig.h).
void lumenAssertFail(const char *expr, const char *file, int line) {
  char msg[1024];
  std::snprintf(msg, sizeof(msg), "Falha interna da interface: %s\n(%s:%d)", expr, file, line);
  lumen::platform::fatalError(msg);
  std::abort();
}
