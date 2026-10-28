#include "control/devices.h"

#include "control/osc.h"
#include "core/md5.h"
#include "io/serial.h"

#include <rtmidi/RtMidi.h>

#include <algorithm>
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

// Connect with a timeout, send, then read until a line ends or the timeout.
static bool tcpExchange(const std::string &host, int port, const std::string &send, std::string *reply,
                        int timeoutMs, std::string *error) {
  netInit();
  sockaddr_in addr;
  if (!resolve(host, port, addr)) { if (error) *error = "endereço inválido"; return false; }
  Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == LUMEN_BADSOCK) { if (error) *error = "socket"; return false; }
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
#endif
  connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  fd_set w;
  FD_ZERO(&w);
  FD_SET(s, &w);
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  if (select(int(s + 1), nullptr, &w, nullptr, &tv) <= 0) {
    LUMEN_CLOSESOCK(s);
    if (error) *error = "sem resposta";
    return false;
  }
  auto readLine = [&](std::string &out) {
    out.clear();
    char c;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < until) {
      fd_set r;
      FD_ZERO(&r);
      FD_SET(s, &r);
      timeval t2{0, 100000};
      if (select(int(s + 1), &r, nullptr, nullptr, &t2) <= 0) continue;
      int n = int(recv(s, &c, 1, 0));
      if (n <= 0) break;
      if (c == '\r' || c == '\n') { if (!out.empty()) return true; continue; }
      out += c;
    }
    return !out.empty();
  };
  const std::string &data = send;
  size_t pos = 0;
  while (pos < data.size()) {
    int n = int(::send(s, data.data() + pos, int(data.size() - pos), 0));
    if (n <= 0) {
      fd_set w2;
      FD_ZERO(&w2);
      FD_SET(s, &w2);
      timeval t3{1, 0};
      if (select(int(s + 1), nullptr, &w2, nullptr, &t3) <= 0) break;
      continue;
    }
    pos += size_t(n);
  }
  if (reply) readLine(*reply);
  LUMEN_CLOSESOCK(s);
  return pos == data.size();
}

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

std::string DeviceCommands::pjlink(const std::string &host, const std::string &password, const std::string &cmd) {
  // Class 1: the projector greets "PJLINK 0" (no password) or "PJLINK 1 <seed>";
  // with a password every command is prefixed by md5(seed + password).
  netInit();
  sockaddr_in addr;
  if (!resolve(host, 4352, addr)) return "erro: endereço inválido";
  Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == LUMEN_BADSOCK) return "erro: socket";
  timeval tv{3, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
  if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    LUMEN_CLOSESOCK(s);
    return "erro: sem conexão";
  }
  auto readLine = [&]() {
    std::string out;
    char c;
    while (recv(s, &c, 1, 0) == 1) {
      if (c == '\r') break;
      out += c;
    }
    return out;
  };
  std::string greet = readLine();
  std::string prefix;
  if (greet.rfind("PJLINK 1 ", 0) == 0) prefix = md5Hex(greet.substr(9) + password);
  else if (greet.rfind("PJLINK 0", 0) != 0) {
    LUMEN_CLOSESOCK(s);
    return "erro: não é PJLink (" + greet + ")";
  }
  std::string msg = prefix + cmd + "\r";
  ::send(s, msg.data(), int(msg.size()), 0);
  std::string reply = readLine();
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
    std::string host = w[1], password;
    for (auto &pj : job.projectors)
      if (pj.name == w[1] || pj.host == w[1]) { host = pj.host; password = pj.password; }
    std::string a = w[2], b = w.size() > 3 ? w[3] : "";
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
    if (r.rfind("%1POWR=", 0) == 0 && r.size() > 7) {
      static const char *names[] = {"desligado", "ligado", "esfriando", "aquecendo"};
      int v = r[7] - '0';
      state = (v >= 0 && v <= 3) ? names[v] : r;
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
    DmxUniverse &u = dmx_[{int(proto), uni}];
    for (int c = std::max(1, c0); c <= std::min(512, c1); ++c) u[size_t(c - 1)] = uint8_t(v);
    std::vector<uint8_t> pkt = proto == DmxProtocol::ArtNet ? encodeArtDmx(uni, ++seq_, u)
                                                             : encodeSacn(uni, ++seq_, u, cid_, "Lumen");
    OscSender sender;
    bool sent = false;
    for (auto &d : job.dmx)
      if (d.enabled && d.protocol == proto) {
        std::string host = d.host.empty() ? (proto == DmxProtocol::ArtNet ? "255.255.255.255" : sacnMulticastAddress(uni)) : d.host;
        sender.sendRaw(host, proto == DmxProtocol::ArtNet ? 6454 : 5568, pkt.data(), pkt.size());
        sent = true;
      }
    if (!sent)
      sender.sendRaw(proto == DmxProtocol::ArtNet ? "255.255.255.255" : sacnMulticastAddress(uni),
                     proto == DmxProtocol::ArtNet ? 6454 : 5568, pkt.data(), pkt.size());
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
    SerialPort sp;
    std::string err;
    if (!sp.open(port, baud, &err)) { log("[serial] " + err); return; }
    sp.write(expandDeviceText(rest(line, 2)));
    log("[serial] " + line);
    return;
  }
  log("[dispositivo] não entendi: " + line);
}

} // namespace lumen
