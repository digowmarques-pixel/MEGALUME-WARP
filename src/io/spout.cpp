#include "io/spout.h"

#ifdef _WIN32

#include <SpoutReceiver.h>
#include <SpoutSender.h>

namespace lumen {

bool spoutAvailable() { return true; }

std::vector<std::string> spoutSenders() {
  std::vector<std::string> out;
  ::SpoutReceiver r;
  int n = r.GetSenderCount();
  char name[256];
  for (int i = 0; i < n; ++i)
    if (r.GetSender(i, name, 256)) out.emplace_back(name);
  return out;
}

struct SpoutIn::Impl {
  ::SpoutReceiver receiver;
};

static void allocTexture(unsigned &tex, int w, int h) {
  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

SpoutIn::SpoutIn(const std::string &sender) : impl_(std::make_unique<Impl>()), name_(sender) {
  if (!sender.empty()) impl_->receiver.SetReceiverName(sender.c_str());
}

SpoutIn::~SpoutIn() {
  impl_->receiver.ReleaseReceiver();
  if (tex_) glDeleteTextures(1, &tex_);
}

bool SpoutIn::receive() {
  if (!tex_) {
    w_ = h_ = 16;
    allocTexture(tex_, w_, h_);
  }
  // Not inverted: Spout (DirectX) textures are top-down, matching Lumen media.
  bool ok = impl_->receiver.ReceiveTexture(tex_, GL_TEXTURE_2D, false, 0);
  connected_ = impl_->receiver.IsConnected();
  if (ok && impl_->receiver.IsUpdated()) {
    // Sender started or changed size: resize and receive on the next frame.
    w_ = int(impl_->receiver.GetSenderWidth());
    h_ = int(impl_->receiver.GetSenderHeight());
    if (w_ > 0 && h_ > 0) allocTexture(tex_, w_, h_);
    if (const char *n = impl_->receiver.GetSenderName()) name_ = n;
    return false;
  }
  return ok && connected_;
}

struct SpoutOut::Impl {
  ::SpoutSender sender;
};

SpoutOut::SpoutOut() : impl_(std::make_unique<Impl>()) {}
SpoutOut::~SpoutOut() { release(); }

bool SpoutOut::send(unsigned texture, int w, int h, const std::string &name) {
  if (name != name_) {
    impl_->sender.ReleaseSender();
    impl_->sender.SetSenderName(name.c_str());
    name_ = name;
  }
  // Invert: OpenGL framebuffers are bottom-up, receivers expect top-down.
  return impl_->sender.SendTexture(texture, GL_TEXTURE_2D, unsigned(w), unsigned(h), true, 0);
}

void SpoutOut::release() {
  if (!name_.empty()) impl_->sender.ReleaseSender();
  name_.clear();
}

} // namespace lumen

#else // not Windows

namespace lumen {

bool spoutAvailable() { return false; }
std::vector<std::string> spoutSenders() { return {}; }

struct SpoutIn::Impl {};
SpoutIn::SpoutIn(const std::string &sender) : name_(sender) {}
SpoutIn::~SpoutIn() = default;
bool SpoutIn::receive() { return false; }

struct SpoutOut::Impl {};
SpoutOut::SpoutOut() = default;
SpoutOut::~SpoutOut() = default;
bool SpoutOut::send(unsigned, int, int, const std::string &) { return false; }
void SpoutOut::release() {}

} // namespace lumen

#endif
