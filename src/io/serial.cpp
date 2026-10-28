#include "io/serial.h"

#include <cerrno>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace lumen {

std::vector<std::string> serialPorts() {
  std::vector<std::string> out;
#ifdef _WIN32
  char target[512];
  for (int i = 1; i <= 64; ++i) {
    std::string name = "COM" + std::to_string(i);
    if (QueryDosDeviceA(name.c_str(), target, sizeof(target))) out.push_back(name);
  }
#else
  std::error_code ec;
  for (auto &e : std::filesystem::directory_iterator("/dev", ec)) {
    std::string n = e.path().filename().string();
    if (n.rfind("ttyUSB", 0) == 0 || n.rfind("ttyACM", 0) == 0 || n.rfind("cu.usb", 0) == 0 || n.rfind("tty.usb", 0) == 0)
      out.push_back(e.path().string());
  }
#endif
  return out;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string &name, int baud, std::string *error) {
  close();
#ifdef _WIN32
  std::string path = "\\\\.\\" + name; // COM10 and above need this form
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (error) *error = "não foi possível abrir " + name;
    return false;
  }
  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  GetCommState(h, &dcb);
  dcb.BaudRate = DWORD(baud);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fDtrControl = DTR_CONTROL_ENABLE; // Arduino Leonardo / Teensy need DTR
  SetCommState(h, &dcb);
  COMMTIMEOUTS to{};
  to.ReadIntervalTimeout = MAXDWORD;
  to.ReadTotalTimeoutConstant = 0;
  to.WriteTotalTimeoutConstant = 500;
  SetCommTimeouts(h, &to);
  handle_ = intptr_t(h);
#else
  int fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (error) *error = "não foi possível abrir " + name;
    return false;
  }
  termios t{};
  tcgetattr(fd, &t);
  cfmakeraw(&t);
  speed_t sp = 0;
  switch (baud) {
  case 1200: sp = B1200; break;
  case 2400: sp = B2400; break;
  case 4800: sp = B4800; break;
  case 9600: sp = B9600; break;
  case 19200: sp = B19200; break;
  case 38400: sp = B38400; break;
  case 57600: sp = B57600; break;
  case 115200: sp = B115200; break;
  case 230400: sp = B230400; break;
#ifdef B460800
  case 460800: sp = B460800; break;
#endif
#ifdef B500000
  case 500000: sp = B500000; break;
#endif
#ifdef B921600
  case 921600: sp = B921600; break;
#endif
#ifdef B1000000
  case 1000000: sp = B1000000; break;
#endif
  default: break;
  }
#ifdef __APPLE__
  if (!sp) sp = speed_t(baud); // macOS takes the number itself
#endif
  if (!sp) { // silently using 9600 would only give garbage
    ::close(fd);
    if (error) *error = "velocidade " + std::to_string(baud) + " não suportada";
    return false;
  }
  cfsetispeed(&t, sp);
  cfsetospeed(&t, sp);
  t.c_cflag |= CLOCAL | CREAD;
  tcsetattr(fd, TCSANOW, &t);
  handle_ = fd;
#endif
  name_ = name;
  return true;
}

void SerialPort::close() {
  if (handle_ == invalid()) return;
#ifdef _WIN32
  CloseHandle(HANDLE(handle_));
#else
  ::close(int(handle_));
#endif
  handle_ = invalid();
}

bool SerialPort::write(const std::string &data) {
  if (!isOpen()) return false;
#ifdef _WIN32
  DWORD n = 0;
  return WriteFile(HANDLE(handle_), data.data(), DWORD(data.size()), &n, nullptr) && n == data.size();
#else
  return ::write(int(handle_), data.data(), data.size()) == ssize_t(data.size());
#endif
}

std::string SerialPort::read(int timeoutMs, bool *lost) {
  std::string out;
  if (lost) *lost = false;
  if (!isOpen()) return out;
  char buf[256];
#ifdef _WIN32
  DWORD n = 0;
  if (ReadFile(HANDLE(handle_), buf, sizeof(buf), &n, nullptr)) {
    if (n > 0) out.assign(buf, n);
    else Sleep(DWORD(timeoutMs));
  } else { // unplugged: ERROR_ACCESS_DENIED, ERROR_BAD_COMMAND...
    if (lost) *lost = true;
    Sleep(DWORD(timeoutMs));
  }
#else
  fd_set set;
  FD_ZERO(&set);
  FD_SET(int(handle_), &set);
  timeval tv{0, timeoutMs * 1000};
  if (select(int(handle_) + 1, &set, nullptr, nullptr, &tv) > 0) {
    // Readable with nothing to read (0) or an error: the device hung up. Without
    // this the loop spun at 100% CPU and never noticed the replug.
    ssize_t n = ::read(int(handle_), buf, sizeof(buf));
    if (n > 0) out.assign(buf, size_t(n));
    else if (lost && (n == 0 || (errno != EAGAIN && errno != EINTR))) *lost = true;
  }
#endif
  return out;
}

SerialReader::~SerialReader() { stop(); }

bool SerialReader::start(const std::string &port, int baud, std::string *error) {
  stop();
  bool ok = port0_.open(port, baud, error);
  connected_ = ok;
  port_ = port;
  baud_ = baud;
  running_ = true;
  thread_ = std::thread(&SerialReader::run, this);
  return ok;
}

void SerialReader::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  port0_.close();
  connected_ = false;
}

void SerialReader::run() {
  std::string line;
  while (running_) {
    if (!port0_.isOpen()) { // not there yet, or unplugged: try again every second
      for (int i = 0; i < 20 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (running_ && port0_.open(port_, baud_)) connected_ = true;
      line.clear();
      continue;
    }
    bool lost = false;
    std::string data = port0_.read(20, &lost);
    if (lost) {
      port0_.close();
      connected_ = false;
      continue;
    }
    for (char c : data) {
      if (c == '\n' || c == '\r') {
        if (!line.empty()) {
          std::lock_guard<std::mutex> lock(mutex_);
          lines_.push_back(line);
          while (lines_.size() > 1000) lines_.pop_front();
        }
        line.clear();
      } else if (line.size() < 256) {
        line += c;
      }
    }
  }
}

void SerialReader::drain(std::vector<std::string> &lines) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!lines_.empty()) {
    lines.push_back(std::move(lines_.front()));
    lines_.pop_front();
  }
}

} // namespace lumen
