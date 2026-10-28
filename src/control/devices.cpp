#include "control/devices.h"

#include "control/osc.h"
#include "core/md5.h"
#include "io/serial.h"

#include <rtmidi/RtMidi.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock = SOCKET;
#define LUMEN_BADSOCK INVALID_SOCKET
#define LUMEN_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
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

std::string expandDeviceText(const std::string &t) {
  std::string out;
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '\\' && i + 1 < t.size()) {
      char c = t[i + 1];
      if (c == 'r') { out += '\r'; ++i; continue; }
      if (c == 'n') { out += '\n'; ++i; continue; }
      if (c == 't') { out += '\t'; ++i; continue; }
    }
    if (t[i] == '[') {
      size_t e = t.find(']', i);
      if (e != std::string::npos) {
        std::string tok = t.substr(i + 1, e - i - 1);
        if (tok == "CR") { out += '\r'; i = e; continue; }
        if (tok == "LF" || tok == "NEW_LINE") { out += '\n'; i = e; continue; }
        if (tok == "RETURN") { out += "\r"; i = e; continue; }
        if (tok == "TAB") { out += '\t'; i = e; continue; }
        if (tok.rfind("HEX_", 0) == 0 && tok.size() == 6) {
          out += char(std::strtol(tok.substr(4).c_str(), nullptr, 16));
          i = e;
          continue;
        }
      }
    }
    out += t[i];
  }
  return out;
}

static void netInit() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    done = true;
  }
#endif
}

static bool resolve(const std::string &host, int port, sockaddr_in &addr) {
  addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) return true;
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  addr.sin_addr = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr;
  freeaddrinfo(res);
  return true;
}

#ifdef _WIN32
static bool wouldBlock() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
#endif

// A connected, non-blocking TCP socket, or LUMEN_BADSOCK. The connection
// result is checked (SO_ERROR): a refused port is an error, not a socket that
// looks writable and then fails.
static Sock tcpConnect(const std::string &host, int port, int timeoutMs, std::string *error) {
  netInit();
  sockaddr_in addr;
  if (!resolve(host, port, addr)) { if (error) *error = "endereço inválido"; return LUMEN_BADSOCK; }
  Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == LUMEN_BADSOCK) { if (error) *error = "socket"; return LUMEN_BADSOCK; }
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE // macOS: no SIGPIPE from this socket
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
  if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 && !wouldBlock()) {
    LUMEN_CLOSESOCK(s);
    if (error) *error = "sem conexão";
    return LUMEN_BADSOCK;
  }
  fd_set w, x;
  FD_ZERO(&w);
  FD_SET(s, &w);
  FD_ZERO(&x);
  FD_SET(s, &x); // Windows reports a failed connect here
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  int soErr = 0;
  socklen_t len = sizeof(soErr);
  if (select(int(s + 1), nullptr, &w, &x, &tv) <= 0 || FD_ISSET(s, &x) ||
      getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soErr), &len) != 0 || soErr != 0) {
    LUMEN_CLOSESOCK(s);
    if (error) *error = soErr ? "conexão recusada" : "sem resposta";
    return LUMEN_BADSOCK;
  }
  return s;
}

// Sends everything within the time limit; stops at the first real error.
static bool tcpSendAll(Sock s, const std::string &data, int timeoutMs) {
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL; // Linux: a closed peer is an error, not SIGPIPE
#else
  const int flags = 0;
#endif
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  size_t pos = 0;
  while (pos < data.size()) {
    int n = int(::send(s, data.data() + pos, int(data.size() - pos), flags));
    if (n > 0) { pos += size_t(n); continue; }
    if (!wouldBlock() || std::chrono::steady_clock::now() >= until) return false;
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval t{0, 100000};
    select(int(s + 1), nullptr, &w, nullptr, &t);
  }
  return true;
}

// Reads one line (ended by CR or LF, at most maxLen characters) within the time limit.
static bool tcpReadLine(Sock s, std::string &out, int timeoutMs, size_t maxLen = 1024) {
  out.clear();
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < until && out.size() < maxLen) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    timeval t{0, 100000};
    if (select(int(s + 1), &r, nullptr, nullptr, &t) <= 0) continue;
    char c;
    int n = int(recv(s, &c, 1, 0));
    if (n == 0) break; // closed
    if (n < 0) { if (wouldBlock()) continue; break; }
    if (c == '\r' || c == '\n') { if (!out.empty()) return true; continue; }
    out += c;
  }
  return !out.empty();
}

// Connect with a timeout, send, then read until a line ends or the timeout.
static bool tcpExchange(const std::string &host, int port, const std::string &send, std::string *reply,
                        int timeoutMs, std::string *error) {
  Sock s = tcpConnect(host, port, timeoutMs, error);
  if (s == LUMEN_BADSOCK) return false;
  bool ok = tcpSendAll(s, send, timeoutMs);
  if (!ok && error) *error = "conexão caiu";
  if (ok && reply) tcpReadLine(s, *reply, timeoutMs);
  LUMEN_CLOSESOCK(s);
  return ok;
}

struct DeviceCommands::OpenSerial {
  SerialPort port;
  int baud = 9600;
};

DeviceCommands::DeviceCommands() {
  std::random_device rd;
  for (auto &b : cid_) b = uint8_t(rd());
  thread_ = std::thread(&DeviceCommands::worker, this);
}

DeviceCommands::~DeviceCommands() {
  running_ = false;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void DeviceCommands::runBatch(const std::vector<std::string> &lines, const Project &p) {
  Job j;
  for (auto &l : lines)
    if (!l.empty()) j.lines.push_back(l);
  if (j.lines.empty()) return;
  j.dmx = p.dmxDevices;
  j.projectors = p.projectors;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(j));
  }
  cv_.notify_one();
}

void DeviceCommands::log(const std::string &s) {
  std::lock_guard<std::mutex> lock(mutex_);
  log_.push_back(s);
  if (log_.size() > 200) log_.erase(log_.begin());
}

void DeviceCommands::drainLog(std::vector<std::string> &out) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &l : log_) out.push_back(l);
  log_.clear();
}

std::string DeviceCommands::projectorStatus(const std::string &host) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = status_.find(host);
  return it == status_.end() ? std::string() : it->second;
}

std::vector<std::string> DeviceCommands::midiOutPorts() {
  std::vector<std::string> out;
  try {
    RtMidiOut probe;
    for (unsigned i = 0; i < probe.getPortCount(); ++i) out.push_back(probe.getPortName(i));
  } catch (...) {
  }
  return out;
}

void DeviceCommands::releaseSerial(const std::string &port) {
  auto lower = [](std::string s) {
    for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  std::lock_guard<std::mutex> lock(serialMutex_);
  for (auto it = serial_.begin(); it != serial_.end();)
    it = lower(it->first) == lower(port) ? serial_.erase(it) : std::next(it);
}

void DeviceCommands::worker() {
  while (running_) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(200), [&] { return !jobs_.empty() || !running_; });
      if (jobs_.empty()) continue;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    for (auto &line : job.lines) {
      if (!running_) return;
      try {
        exec(line, job);
      } catch (const std::exception &e) {
        log("[dispositivo] " + line + ": " + e.what());
      }
    }
  }
}

RtMidiOut *DeviceCommands::midiPort(const std::string &match) {
  auto it = midi_.find(match);
  if (it != midi_.end()) return it->second.get();
  auto out = std::make_unique<RtMidiOut>();
  unsigned n = out->getPortCount();
  int pick = -1;
  for (unsigned i = 0; i < n; ++i) {
    std::string name = out->getPortName(i);
    std::string a = name, b = match;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    if (b.empty() || a.find(b) != std::string::npos) { pick = int(i); break; }
  }
  if (pick < 0) return nullptr;
  out->openPort(unsigned(pick), "Lumen");
  RtMidiOut *raw = out.get();
  midi_[match] = std::move(out);
  return raw;
}

bool DeviceCommands::heldDmx(HeldDmx &out, uint64_t &version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (version == dmxVersion_) return false;
  version = dmxVersion_;
  out = dmx_;
  return true;
}

std::string DeviceCommands::pjlink(const std::string &host, const std::string &password, const std::string &cmd) {
  // Class 1: the projector greets "PJLINK 0" (no password) or "PJLINK 1 <seed>";
  // with a password every command is prefixed by md5(seed + password).
  // Non-blocking with 3 s limits: a projector that is off (or on eco standby)
  // must not hold the device worker for the OS's 21 s connect timeout.
  std::string err;
  Sock s = tcpConnect(host, 4352, 3000, &err);
  if (s == LUMEN_BADSOCK) return "erro: " + err;
  std::string greet;
  tcpReadLine(s, greet, 3000, 256);
  std::string prefix;
  if (greet.rfind("PJLINK 1 ", 0) == 0) prefix = md5Hex(greet.substr(9) + password);
  else if (greet.rfind("PJLINK 0", 0) != 0) {
    LUMEN_CLOSESOCK(s);
    return "erro: não é PJLink (" + greet + ")";
  }
  std::string reply;
  if (!tcpSendAll(s, prefix + cmd + "\r", 3000)) {
    LUMEN_CLOSESOCK(s);
    return "erro: conexão caiu";
  }
  tcpReadLine(s, reply, 3000, 256);
  LUMEN_CLOSESOCK(s);
  if (reply.find("ERRA") != std::string::npos) return "erro: senha";
  return reply;
}

static std::vector<std::string> words(const std::string &s) {
  std::vector<std::string> w;
  std::istringstream ss(s);
  for (std::string x; ss >> x;) w.push_back(x);
  return w;
}

static bool hostPort(const std::string &hp, std::string &host, int &port) {
  size_t c = hp.rfind(':');
  if (c == std::string::npos) return false;
  host = hp.substr(0, c);
  port = std::atoi(hp.substr(c + 1).c_str());
  return port > 0 && port < 65536;
}

// The text after the first `n` words of the line (keeps spaces).
static std::string rest(const std::string &line, int n) {
  size_t pos = 0;
  for (int i = 0; i < n; ++i) {
    pos = line.find_first_not_of(" \t", pos);
    if (pos == std::string::npos) return {};
    pos = line.find_first_of(" \t", pos);
    if (pos == std::string::npos) return {};
  }
  return line.substr(pos + 1);
}

void DeviceCommands::exec(const std::string &line, const Job &job) {
  auto w = words(line);
  if (w.empty() || w[0][0] == '#') return;
  std::string cmd = w[0];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

  if (cmd == "wait" && w.size() >= 2) {
    double sec = std::atof(w[1].c_str());
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(sec * 1000));
    while (running_ && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return;
  }
  if (cmd == "pjlink" && w.size() >= 3) {
    // The projector: its name, which may have spaces ("Projetor 1"), or its address.
    std::string host = w[1], password;
    size_t used = 1;
    for (auto &pj : job.projectors) {
      auto nw = words(pj.name);
      if (!nw.empty() && w.size() >= nw.size() + 2 && std::equal(nw.begin(), nw.end(), w.begin() + 1)) {
        if (nw.size() >= used) { host = pj.host; password = pj.password; used = nw.size(); }
      } else if (pj.host == w[1] && used == 1) {
        host = pj.host;
        password = pj.password;
      }
    }
    std::string a = w[1 + used], b = w.size() > 2 + used ? w[2 + used] : "";
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    std::string pj;
    if (a == "power") pj = b == "off" ? "%1POWR 0" : "%1POWR 1";
    else if (a == "shutter" || a == "mute") pj = (b == "open" || b == "off") ? "%1AVMT 30" : "%1AVMT 31";
    else if (a == "input") pj = "%1INPT " + b;
    else if (a == "status") pj = "%1POWR ?";
    else { log("[pjlink] comando desconhecido: " + line); return; }
    std::string r = pjlink(host, password, pj);
    std::string state;
    static const char *pjErrors[] = {"", "comando não suportado", "parâmetro inválido", "indisponível agora",
                                     "falha no projetor"};
    if (r.size() > 7 && r.compare(r.size() - 4, 3, "ERR") == 0 && r.back() >= '1' && r.back() <= '4') {
      state = std::string("erro: ") + pjErrors[r.back() - '0']; // "%1POWR=ERR3"...
    } else if (r.rfind("%1POWR=", 0) == 0 && r.size() == 8 && r[7] >= '0' && r[7] <= '3') {
      static const char *names[] = {"desligado", "ligado", "esfriando", "aquecendo"};
      state = names[r[7] - '0'];
    } else if (r.rfind("erro", 0) == 0) {
      state = r;
    } else if (a == "power") {
      state = b == "off" ? "desligando" : "ligando";
    }
    if (!state.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      status_[host] = state;
    }
    log("[pjlink] " + host + " " + a + " " + b + ": " + r);
    return;
  }
  if (cmd.rfind("midi", 0) == 0 && w.size() >= 2) {
    std::string port;
    size_t at = w[0].find('@');
    if (at != std::string::npos) port = w[0].substr(at + 1);
    std::string type = w[1];
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    auto num = [&](size_t i) { return i < w.size() ? std::clamp(std::atoi(w[i].c_str()), 0, 127) : 0; };
    int ch = std::clamp((w.size() > 2 ? std::atoi(w[2].c_str()) : 1) - 1, 0, 15);
    std::vector<unsigned char> msg;
    if (type == "note") msg = {(unsigned char)(0x90 | ch), (unsigned char)num(3), (unsigned char)num(4)};
    else if (type == "noteoff") msg = {(unsigned char)(0x80 | ch), (unsigned char)num(3), 0};
    else if (type == "cc") msg = {(unsigned char)(0xB0 | ch), (unsigned char)num(3), (unsigned char)num(4)};
    else if (type == "pc") msg = {(unsigned char)(0xC0 | ch), (unsigned char)num(3)};
    else { log("[midi] comando desconhecido: " + line); return; }
    RtMidiOut *out = midiPort(port);
    if (!out) { log("[midi] nenhuma saída MIDI" + (port.empty() ? std::string() : " com \"" + port + "\"")); return; }
    out->sendMessage(&msg);
    log("[midi] " + line);
    return;
  }
  if (cmd == "dmx" && w.size() >= 4) {
    size_t i = 1;
    DmxProtocol proto = DmxProtocol::ArtNet;
    std::string p1 = w[1];
    std::transform(p1.begin(), p1.end(), p1.begin(), ::tolower);
    if (p1 == "artnet" || p1 == "sacn") { proto = p1 == "sacn" ? DmxProtocol::SACN : DmxProtocol::ArtNet; ++i; }
    if (w.size() < i + 3) { log("[dmx] use: dmx <universo> <canal> <valor>"); return; }
    int uni = std::atoi(w[i].c_str());
    std::string chs = w[i + 1];
    int c0 = std::atoi(chs.c_str()), c1 = c0;
    if (size_t d = chs.find('-'); d != std::string::npos) c1 = std::atoi(chs.substr(d + 1).c_str());
    int v = std::clamp(std::atoi(w[i + 2].c_str()), 0, 255);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      DmxUniverse &u = dmx_[{int(proto), uni}];
      for (int c = std::max(1, c0); c <= std::min(512, c1); ++c) u[size_t(c - 1)] = uint8_t(v);
      ++dmxVersion_;
    }
    log("[dmx] " + line);
    return;
  }
  if ((cmd == "tcp" || cmd == "udp" || cmd == "osc") && w.size() >= 3) {
    std::string host;
    int port;
    if (!hostPort(w[1], host, port)) { log("[" + cmd + "] use host:porta"); return; }
    if (cmd == "osc") {
      OscMessage m;
      if (!parseOscText(rest(line, 2), m)) { log("[osc] mensagem inválida: " + line); return; }
      OscSender().send(host, port, m);
    } else {
      std::string text = expandDeviceText(rest(line, 2));
      if (cmd == "udp") {
        OscSender().sendRaw(host, port, reinterpret_cast<const uint8_t *>(text.data()), text.size());
      } else {
        std::string err, reply;
        if (!tcpExchange(host, port, text, &reply, 1500, &err)) { log("[tcp] " + w[1] + ": " + err); return; }
        if (!reply.empty()) log("[tcp] resposta: " + reply);
      }
    }
    log("[" + cmd + "] " + line);
    return;
  }
  if (cmd == "serial" && w.size() >= 3) {
    std::string port = w[1];
    int baud = 9600;
    if (size_t a = port.find('@'); a != std::string::npos) {
      baud = std::atoi(port.substr(a + 1).c_str());
      port = port.substr(0, a);
    }
    std::lock_guard<std::mutex> lock(serialMutex_);
    auto &open = serial_[port];
    if (open && open->baud != baud) open.reset();
    if (!open) {
      auto o = std::make_unique<OpenSerial>();
      std::string err;
      if (!o->port.open(port, baud, &err)) { log("[serial] " + err); return; }
      o->baud = baud;
      open = std::move(o);
    }
    if (!open->port.write(expandDeviceText(rest(line, 2)))) {
      open.reset(); // unplugged: opened again by the next command
      log("[serial] falhou ao enviar: " + line);
      return;
    }
    log("[serial] " + line);
    return;
  }
  log("[dispositivo] não entendi: " + line);
}

} // namespace lumen
