// Spout (Windows): share textures with TouchDesigner, Resolume, Unreal, OBS...
// On other platforms these classes compile to no-ops and spoutAvailable()
// returns false. No GL headers here: the Spout SDK brings its own.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace lumen {

bool spoutAvailable();
std::vector<std::string> spoutSenders();

// Receives a sender into a GL texture owned by this object (row 0 = top).
class SpoutIn {
public:
  explicit SpoutIn(const std::string &sender); // "" = active sender
  ~SpoutIn();
  // Must be called with the main GL context current. Returns false when no
  // frame is available; texture()/width()/height() keep the last frame.
  bool receive();
  unsigned texture() const { return tex_; }
  int width() const { return w_; }
  int height() const { return h_; }
  bool connected() const { return connected_; }
  const std::string &senderName() const { return name_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  unsigned tex_ = 0;
  int w_ = 0, h_ = 0;
  bool connected_ = false;
  std::string name_;
};

// Publishes a GL texture (bottom-up, as rendered by OpenGL) as a sender.
class SpoutOut {
public:
  SpoutOut();
  ~SpoutOut();
  bool send(unsigned texture, int width, int height, const std::string &name);
  void release();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string name_;
};

} // namespace lumen
