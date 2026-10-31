#include "platform/process.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lumen::platform {

ChildProcess::~ChildProcess() {
  if (started_) {
    kill();
    wait();
  }
}

bool ChildProcess::readLine(std::string &line) {
  for (;;) {
    size_t nl = buffer_.find('\n');
    if (nl != std::string::npos) {
      line = buffer_.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      buffer_.erase(0, nl + 1);
      return true;
    }
    if (eof_ || !fill()) {
      eof_ = true;
      if (buffer_.empty()) return false;
      line.swap(buffer_);
      buffer_.clear();
      return true;
    }
  }
}

#ifdef _WIN32

static std::wstring widen(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

// Quoting that CommandLineToArgvW (and the C runtime of the child) undoes.
static std::wstring quote(const std::wstring &a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
  std::wstring q = L"\"";
  size_t slashes = 0;
  for (wchar_t c : a) {
    if (c == L'\\') { ++slashes; continue; }
    q.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    q += c;
  }
  q.append(slashes * 2, L'\\');
  return q + L"\"";
}

bool ChildProcess::start(const std::vector<std::string> &args, std::string *error) {
  if (started_ || args.empty()) return false;
  std::wstring cmd;
  for (const auto &a : args) cmd += (cmd.empty() ? L"" : L" ") + quote(widen(a));
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE readEnd = nullptr, writeEnd = nullptr;
  if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
    if (error) *error = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
  CloseHandle(writeEnd);
  if (!ok) {
    CloseHandle(readEnd);
    if (error) *error = "could not start " + args[0] + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  pipe_ = readEnd;
  started_ = true;
  eof_ = false;
  return true;
}

bool ChildProcess::fill() {
  char buf[4096];
  DWORD n = 0;
  if (!pipe_ || !ReadFile(pipe_, buf, sizeof(buf), &n, nullptr) || n == 0) return false;
  buffer_.append(buf, n);
  return true;
}

void ChildProcess::kill() {
  if (process_) TerminateProcess(process_, 1);
}

int ChildProcess::wait() {
  if (!started_) return -1;
  WaitForSingleObject(process_, INFINITE);
  DWORD code = DWORD(-1);
  GetExitCodeProcess(process_, &code);
  CloseHandle(process_);
  if (pipe_) CloseHandle(pipe_);
  process_ = pipe_ = nullptr;
  started_ = false;
  return int(code);
}

#else

bool ChildProcess::start(const std::vector<std::string> &args, std::string *error) {
  if (started_ || args.empty()) return false;
  int fds[2];
  if (pipe(fds) != 0) {
    if (error) *error = "pipe failed";
    return false;
  }
  std::vector<char *> argv;
  for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    if (error) *error = "fork failed";
    return false;
  }
  if (pid == 0) {
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[0]);
    close(fds[1]);
    setpriority(PRIO_PROCESS, 0, 10);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(fds[1]);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  pid_ = pid;
  pipe_ = fds[0];
  started_ = true;
  eof_ = false;
  return true;
}

bool ChildProcess::fill() {
  char buf[4096];
  ssize_t n;
  do n = read(pipe_, buf, sizeof(buf));
  while (n < 0 && errno == EINTR);
  if (n <= 0) return false;
  buffer_.append(buf, size_t(n));
  return true;
}

void ChildProcess::kill() {
  if (pid_ > 0) ::kill(pid_, SIGKILL);
}

int ChildProcess::wait() {
  if (!started_) return -1;
  int status = 0;
  while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
  close(pipe_);
  pid_ = pipe_ = -1;
  started_ = false;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#endif

} // namespace lumen::platform
