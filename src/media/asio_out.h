// ASIO output (Windows, Steinberg ASIO SDK). Built only when the SDK is given at
// build time (LUMEN_ASIO): the SDK is licensed separately and never lives in this
// repository. Without it every function here reports "not available".
//
// ASIO is a registered trademark of Steinberg Media Technologies GmbH.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lumen {

// Device names in the audio device list carry this prefix ("ASIO: Focusrite USB ASIO").
inline constexpr const char *kAsioPrefix = "ASIO: ";

class AsioOutput {
public:
  // Mixes `frames` frames into `out`, `channels` interleaved floats per frame.
  using Render = std::function<void(float *out, size_t frames, int channels)>;
  struct Impl;

  static bool available();                 // built with the SDK (Windows only)
  static std::vector<std::string> drivers(); // installed ASIO drivers

  AsioOutput();
  ~AsioOutput();
  // Opens the driver at `sampleRate` with its preferred buffer size and every
  // output (at most maxChannels). `render` runs on the driver's thread.
  bool start(const std::string &driver, int sampleRate, int maxChannels, Render render, std::string &error);
  void stop();
  bool running() const;
  int channels() const;  // outputs in use
  int latencyMs() const; // output latency reported by the driver
  // The driver asked to be reopened (buffer size or sample rate changed in its panel).
  bool needsReset() const;
  void controlPanel(); // the driver's own settings window

private:
  Impl *impl_;
};

} // namespace lumen
