#include "control/dmxin.h"

#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
using Sock = SOCKET;
#define LUMEN_BADSOCK INVALID_SOCKET
#define LUMEN_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Sock = int;
#define LUMEN_BADSOCK (-1)
#define LUMEN_CLOSESOCK close
#endif

namespace lumen {

DmxReceiver::~DmxReceiver() { stop(); }

bool DmxReceiver::start(DmxProtocol protocol, int first, int count, std::string *error) {
  stop();
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  protocol_ = protocol;
  first_ = first;
  count_ = std::max(1, count);
  int port = protocol == DmxProtocol::ArtNet ? 6454 : 5568;
  auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == LUMEN_BADSOCK) { if (error) *error = "socket() falhou"; return false; }
  int yes = 1;
  // Art-Net shares port 6454 with other software on the same machine.
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(uint16_t(port));
  if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    LUMEN_CLOSESOCK(s);
    if (error) *error = "porta " + std::to_string(port) + " em uso";
    return false;
  }
  if (protocol == DmxProtocol::SACN) {
    for (int u = first_; u < first_ + count_; ++u) {
      ip_mreq mreq{};
      inet_pton(AF_INET, sacnMulticastAddress(u).c_str(), &mreq.imr_multiaddr);
      mreq.imr_interface.s_addr = htonl(INADDR_ANY);
      setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char *>(&mreq), sizeof(mreq));
    }
  }
  socket_ = intptr_t(s);
  running_ = true;
  thread_ = std::thread(&DmxReceiver::run, this);
  return true;
}

void DmxReceiver::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (socket_ >= 0) {
    LUMEN_CLOSESOCK(Sock(socket_));
    socket_ = -1;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  live_.clear();
  seen_.clear();
}

void DmxReceiver::run() {
  auto s = Sock(socket_);
  std::vector<uint8_t> buf(2048);
  while (running_) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{0, 100000};
    if (select(int(s + 1), &set, nullptr, nullptr, &tv) <= 0) continue;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = int(recvfrom(s, reinterpret_cast<char *>(buf.data()), int(buf.size()), 0,
                         reinterpret_cast<sockaddr *>(&from), &fromLen));
    if (n <= 0) continue;
    int universe = 0, count = 0;
    DmxUniverse data{};
    bool ok = protocol_ == DmxProtocol::ArtNet ? decodeArtDmx(buf.data(), size_t(n), universe, data, count)
                                               : decodeSacn(buf.data(), size_t(n), universe, data, count);
    if (!ok || universe < first_ || universe >= first_ + count_) continue;
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    std::lock_guard<std::mutex> lock(mutex_);
    auto &u = live_[universe];
    std::copy(data.begin(), data.begin() + count, u.begin());
    source_[universe] = ip;
    ++packets_;
  }
}

void DmxReceiver::drain(std::vector<OscMessage> &out) {
  std::lock_guard<std::mutex> lock(mutex_);
  const char *prefix = protocol_ == DmxProtocol::ArtNet ? "/artnet/" : "/sacn/";
  for (auto &[u, data] : live_) {
    auto it = seen_.find(u);
    bool first = it == seen_.end();
    DmxUniverse &prev = seen_[u];
    for (int c = 0; c < 512; ++c) {
      // The first packet reports only channels that are not zero.
      if (first ? data[size_t(c)] == 0 : data[size_t(c)] == prev[size_t(c)]) continue;
      out.push_back(OscMessage{prefix + std::to_string(u) + "/" + std::to_string(c + 1),
                               {int32_t(data[size_t(c)])}, source_[u]});
    }
    prev = data;
  }
}

bool DmxReceiver::universe(int u, DmxUniverse &out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = live_.find(u);
  if (it == live_.end()) return false;
  out = it->second;
  return true;
}

} // namespace lumen
