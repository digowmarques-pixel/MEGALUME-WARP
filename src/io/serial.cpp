#include "io/serial.h"

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
  speed_t sp = B9600;
  switch (baud) {
  case 19200: sp = B19200; break;
  case 38400: sp = B38400; break;
  case 57600: sp = B57600; break;
  case 115200: sp = B115200; break;
  default: break;
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

std::string SerialPort::read(int timeoutMs) {
  std::string out;
  if (!isOpen()) return out;
  char buf[256];
#ifdef _WIN32
  DWORD n = 0;
  if (ReadFile(HANDLE(handle_), buf, sizeof(buf), &n, nullptr) && n > 0) out.assign(buf, n);
  else Sleep(DWORD(timeoutMs));
#else
  fd_set set;
  FD_ZERO(&set);
  FD_SET(int(handle_), &set);
  timeval tv{0, timeoutMs * 1000};
  if (select(int(handle_) + 1, &set, nullptr, nullptr, &tv) > 0) {
    ssize_t n = ::read(int(handle_), buf, sizeof(buf));
    if (n > 0) out.assign(buf, size_t(n));
  }
#endif
  return out;
}

SerialReader::~SerialReader() { stop(); }

bool SerialReader::start(const std::string &port, int baud, std::string *error) {
  stop();
  if (!port0_.open(port, baud, error)) return false;
  port_ = port;
  running_ = true;
  thread_ = std::thread(&SerialReader::run, this);
  return true;
}

void SerialReader::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  port0_.close();
}

void SerialReader::run() {
  std::string line;
  while (running_) {
    std::string data = port0_.read(20);
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
