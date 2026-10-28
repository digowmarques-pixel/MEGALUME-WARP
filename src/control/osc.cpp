#include "control/osc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
using Sock = SOCKET;
#define LUMEN_BADSOCK INVALID_SOCKET
#define LUMEN_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Sock = int;
#define LUMEN_BADSOCK (-1)
#define LUMEN_CLOSESOCK close
#endif

namespace lumen {

namespace {

void ensureNetwork() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    done = true;
  }
#endif
}

uint32_t readU32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void writeU32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

size_t pad4(size_t n) { return (n + 3) & ~size_t(3); }

// Reads a null-terminated, 4-byte padded OSC string.
bool readString(const uint8_t *data, size_t size, size_t &pos, std::string &out) {
  size_t start = pos;
  while (pos < size && data[pos] != 0) ++pos;
  if (pos >= size) return false;
  out.assign(reinterpret_cast<const char *>(data + start), pos - start);
  pos = start + pad4(pos - start + 1);
  return pos <= size;
}

void writeString(std::vector<uint8_t> &v, const std::string &s) {
  v.insert(v.end(), s.begin(), s.end());
  size_t n = pad4(s.size() + 1) - s.size();
  v.insert(v.end(), n, 0);
}

bool parseMessage(const uint8_t *data, size_t size, OscMessage &m) {
  size_t pos = 0;
  if (!readString(data, size, pos, m.address) || m.address.empty() || m.address[0] != '/')
    return false;
  if (pos >= size) return true; // no type tags: allowed by old senders
  std::string tags;
  if (!readString(data, size, pos, tags) || tags.empty() || tags[0] != ',') return false;
  for (size_t i = 1; i < tags.size(); ++i) {
    char t = tags[i];
    switch (t) {
    case 'i': {
      if (pos + 4 > size) return false;
      m.args.emplace_back(int32_t(readU32(data + pos)));
      pos += 4;
      break;
    }
    case 'f': {
      if (pos + 4 > size) return false;
      uint32_t u = readU32(data + pos);
      float f;
      std::memcpy(&f, &u, 4);
      m.args.emplace_back(f);
      pos += 4;
      break;
    }
    case 'd': {
      if (pos + 8 > size) return false;
      uint64_t u = (uint64_t(readU32(data + pos)) << 32) | readU32(data + pos + 4);
      double d;
      std::memcpy(&d, &u, 8);
      m.args.emplace_back(float(d));
      pos += 8;
      break;
    }
    case 'h': {
      if (pos + 8 > size) return false;
      int64_t v = int64_t((uint64_t(readU32(data + pos)) << 32) | readU32(data + pos + 4));
      m.args.emplace_back(int32_t(v));
      pos += 8;
      break;
    }
    case 's':
    case 'S': {
      std::string s;
      if (!readString(data, size, pos, s)) return false;
      m.args.emplace_back(s);
      break;
    }
    case 'T': m.args.emplace_back(true); break;
    case 'F': m.args.emplace_back(false); break;
    case 'N':
    case 'I': break;
    case 'b': {
      if (pos + 4 > size) return false;
      size_t n = readU32(data + pos);
      pos += 4 + pad4(n);
      if (pos > size) return false;
      break;
    }
    default: return false; // unknown type: can't know its size
    }
  }
  return true;
}

bool parseAny(const uint8_t *data, size_t size, std::vector<OscMessage> &out, int depth) {
  if (size < 4 || depth > 8) return false;
  if (size >= 16 && std::memcmp(data, "#bundle", 8) == 0) {
    size_t pos = 16; // "#bundle\0" + 8-byte timetag (executed immediately)
    while (pos + 4 <= size) {
      size_t n = readU32(data + pos);
      pos += 4;
      if (n == 0 || pos + n > size) return false;
      if (!parseAny(data + pos, n, out, depth + 1)) return false;
      pos += n;
    }
    return true;
  }
  OscMessage m;
  if (!parseMessage(data, size, m)) return false;
  out.push_back(std::move(m));
  return true;
}

// Matches one path segment (no '/').
bool matchSegment(const char *p, const char *pe, const char *a, const char *ae) {
  while (p < pe) {
    char c = *p;
    if (c == '*') {
      ++p;
      for (const char *t = a; t <= ae; ++t)
        if (matchSegment(p, pe, t, ae)) return true;
      return false;
    }
    if (a >= ae) return false;
    if (c == '?') { ++p; ++a; continue; }
    if (c == '[') {
      const char *close = p + 1;
      while (close < pe && *close != ']') ++close;
      if (close >= pe) return false;
      bool negate = p[1] == '!';
      bool found = false;
      for (const char *q = p + 1 + (negate ? 1 : 0); q < close; ++q) {
        if (q + 2 < close && q[1] == '-') {
          if (*a >= q[0] && *a <= q[2]) found = true;
          q += 2;
        } else if (*q == *a) {
          found = true;
        }
      }
      if (found == negate) return false;
      p = close + 1;
      ++a;
      continue;
    }
    if (c == '{') {
      const char *close = p + 1;
      while (close < pe && *close != '}') ++close;
      if (close >= pe) return false;
      const char *opt = p + 1;
      while (opt <= close) {
        const char *end = opt;
        while (end < close && *end != ',') ++end;
        size_t n = size_t(end - opt);
        if (size_t(ae - a) >= n && std::strncmp(opt, a, n) == 0 &&
            matchSegment(close + 1, pe, a + n, ae))
          return true;
        opt = end + 1;
      }
      return false;
    }
    if (c != *a) return false;
    ++p;
    ++a;
  }
  return a == ae;
}

} // namespace

float OscMessage::number(size_t i, float def) const {
  if (i >= args.size()) return def;
  const OscArg &a = args[i];
  if (auto v = std::get_if<int32_t>(&a)) return float(*v);
  if (auto v = std::get_if<float>(&a)) return *v;
  if (auto v = std::get_if<bool>(&a)) return *v ? 1.0f : 0.0f;
  if (auto v = std::get_if<std::string>(&a)) {
    try { return std::stof(*v); } catch (...) { return def; }
  }
  return def;
}

std::string OscMessage::toString() const {
  std::string s = address;
  char buf[64];
  for (auto &a : args) {
    s += ' ';
    if (auto v = std::get_if<int32_t>(&a)) { std::snprintf(buf, sizeof(buf), "%d", *v); s += buf; }
    else if (auto f = std::get_if<float>(&a)) { std::snprintf(buf, sizeof(buf), "%.4g", *f); s += buf; }
    else if (auto b = std::get_if<bool>(&a)) s += *b ? "true" : "false";
    else if (auto str = std::get_if<std::string>(&a)) s += '"' + *str + '"';
  }
  return s;
}

bool parseOsc(const uint8_t *data, size_t size, std::vector<OscMessage> &out) {
  return parseAny(data, size, out, 0);
}

std::vector<uint8_t> encodeOsc(const OscMessage &m) {
  std::vector<uint8_t> v;
  writeString(v, m.address);
  std::string tags = ",";
  for (auto &a : m.args) {
    if (std::holds_alternative<int32_t>(a)) tags += 'i';
    else if (std::holds_alternative<float>(a)) tags += 'f';
    else if (std::holds_alternative<std::string>(a)) tags += 's';
    else tags += std::get<bool>(a) ? 'T' : 'F';
  }
  writeString(v, tags);
  for (auto &a : m.args) {
    if (auto i = std::get_if<int32_t>(&a)) writeU32(v, uint32_t(*i));
    else if (auto f = std::get_if<float>(&a)) {
      uint32_t u;
      std::memcpy(&u, f, 4);
      writeU32(v, u);
    } else if (auto s = std::get_if<std::string>(&a)) writeString(v, *s);
  }
  return v;
}

bool oscMatch(const char *pattern, const char *address) {
  // Compare segment by segment so '*' never crosses a '/'.
  while (*pattern && *address) {
    if (*pattern != '/' || *address != '/') return false;
    ++pattern;
    ++address;
    const char *pe = pattern, *ae = address;
    while (*pe && *pe != '/') ++pe;
    while (*ae && *ae != '/') ++ae;
    if (!matchSegment(pattern, pe, address, ae)) return false;
    pattern = pe;
    address = ae;
  }
  return *pattern == 0 && *address == 0;
}

// Splits `/addr 1 2.5 "some text" word` into an OSC message.
bool parseOscText(const std::string &line, OscMessage &m) {
  std::vector<std::string> tokens;
  std::string cur;
  bool quoted = false, any = false;
  for (char c : line) {
    if (c == '"') { quoted = !quoted; any = true; continue; }
    if (!quoted && (c == ' ' || c == '\t')) {
      if (!cur.empty() || any) tokens.push_back(cur);
      cur.clear();
      any = false;
      continue;
    }
    cur += c;
  }
  if (!cur.empty() || any) tokens.push_back(cur);
  if (tokens.empty() || tokens[0].empty() || tokens[0][0] != '/') return false;
  m.address = tokens[0];
  m.args.clear();
  for (size_t i = 1; i < tokens.size(); ++i) {
    const std::string &t = tokens[i];
    char *end = nullptr;
    long iv = std::strtol(t.c_str(), &end, 10);
    if (!t.empty() && *end == 0) { m.args.emplace_back(int32_t(iv)); continue; }
    float fv = std::strtof(t.c_str(), &end);
    if (!t.empty() && *end == 0) { m.args.emplace_back(fv); continue; }
    m.args.emplace_back(t);
  }
  return true;
}

OscServer::~OscServer() { stop(); }

bool OscServer::start(int port, std::string *error) {
  stop();
  ensureNetwork();
  auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == LUMEN_BADSOCK) { if (error) *error = "socket() failed"; return false; }
  // No SO_REUSEADDR: a second Lumen on the same port must fail visibly
  // instead of silently taking the messages meant for the first one.
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(uint16_t(port));
  if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    LUMEN_CLOSESOCK(s);
    if (error) *error = "port " + std::to_string(port) + " is already in use";
    return false;
  }
  socket_ = intptr_t(s);
  port_ = port;
  running_ = true;
  thread_ = std::thread(&OscServer::run, this);
  return true;
}

void OscServer::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (socket_ >= 0) {
    LUMEN_CLOSESOCK(Sock(socket_));
    socket_ = -1;
  }
}

void OscServer::drain(std::vector<OscMessage> &out) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!queue_.empty()) {
    out.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }
}

void OscServer::run() {
  auto s = Sock(socket_);
  std::vector<uint8_t> buf(65536);
  while (running_) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{0, 100000}; // wake up regularly to notice stop()
    if (select(int(s + 1), &set, nullptr, nullptr, &tv) <= 0) continue;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = int(recvfrom(s, reinterpret_cast<char *>(buf.data()), int(buf.size()), 0,
                         reinterpret_cast<sockaddr *>(&from), &fromLen));
    if (n <= 0) continue;
    std::vector<OscMessage> msgs;
    if (!parseOsc(buf.data(), size_t(n), msgs)) {
      // Plain text is accepted too, one message per line:
      //   echo "/lumen/go" | nc -u 127.0.0.1 8000
      msgs.clear();
      std::string text(reinterpret_cast<const char *>(buf.data()), size_t(n)), line;
      for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n' || text[i] == '\r' || text[i] == 0) {
          OscMessage m;
          if (parseOscText(line, m)) msgs.push_back(m);
          line.clear();
        } else {
          line += text[i];
        }
      }
      if (msgs.empty()) continue;
    }
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    std::string sender = std::string(ip) + ":" + std::to_string(ntohs(from.sin_port));
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &m : msgs) {
      m.sender = sender;
      queue_.push_back(std::move(m));
    }
    while (queue_.size() > 4096) queue_.pop_front(); // never grow without bound
  }
}

OscSender::~OscSender() {
  if (socket_ >= 0) LUMEN_CLOSESOCK(Sock(socket_));
}

bool OscSender::ensureSocket() {
  ensureNetwork();
  if (socket_ >= 0) return true;
  auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == LUMEN_BADSOCK) return false;
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof(yes));
  socket_ = intptr_t(s);
  return true;
}

bool OscSender::send(const std::string &host, int port, const OscMessage &m) {
  auto data = encodeOsc(m);
  return sendRaw(host, port, data.data(), data.size());
}

bool OscSender::sendRaw(const std::string &host, int port, const uint8_t *data, size_t size) {
  if (!ensureSocket()) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    // Host name: resolve (cached by the OS resolver).
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    addr.sin_addr = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  int r = int(sendto(Sock(socket_), reinterpret_cast<const char *>(data), int(size), 0,
                     reinterpret_cast<sockaddr *>(&addr), sizeof(addr)));
  return r == int(size);
}

} // namespace lumen
