// A child program whose output (stdout and stderr together) is read line by
// line: used to run ffmpeg for conversions outside Lumen's own process, so a
// failing conversion can never take the show down.
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace lumen::platform {

class ChildProcess {
public:
  ChildProcess() = default;
  ~ChildProcess();
  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;

  // args[0] is the program (a path, or a name looked up in PATH). The child
  // runs at low priority and, on Windows, without a console window.
  bool start(const std::vector<std::string> &args, std::string *error = nullptr);
  // Blocks until a whole line (without the end of line) or the end of output.
  bool readLine(std::string &line);
  void kill();
  // Waits for the child to end; its exit code (-1 if it could not be known).
  int wait();
  bool running() const { return started_; }

private:
  bool fill();
  std::string buffer_;
  bool eof_ = false, started_ = false;
  // kill() may come from another thread (cancel) while wait() ends the child:
  // the handle (pid) is released under this lock, never while kill() uses it.
  std::mutex handleMutex_;
#ifdef _WIN32
  void *process_ = nullptr, *pipe_ = nullptr;
#else
  int pid_ = -1, pipe_ = -1;
#endif
};

} // namespace lumen::platform
