// LED pixel mapping: samples the composed canvas inside each LED mapping on
// the GPU, builds DMX universes and sends them over Art-Net / sACN.
#pragma once

#include <functional>

#include "control/osc.h"
#include "core/project.h"
#include "render/gpu.h"

#include <map>
#include <memory>
#include <vector>

namespace lumen {

struct UniverseKey {
  int deviceId, universe;
  bool operator<(const UniverseKey &o) const {
    return deviceId != o.deviceId ? deviceId < o.deviceId : universe < o.universe;
  }
};

class LedEngine {
public:
  bool init();
  // Samples the canvas and (at each device's rate) sends the universes.
  void update(const Project &p, const std::function<const Texture &(int canvasId)> &canvas,
              DynamicMesh &mesh, double now);

  // Last sampled colours of a mapping (cols*rows RGB, 0..1, row 0 = top).
  const std::vector<float> *colours(int mappingId) const;
  // Last universes sent, for the DMX monitor.
  const std::map<UniverseKey, DmxUniverse> &universes() const { return universes_; }
  // Channel overlaps between mappings, as human-readable warnings.
  const std::vector<std::string> &conflicts() const { return conflicts_; }
  int packetsSent() const { return packetsSent_; }

private:
  struct Sampler {
    Framebuffer fb;
    std::vector<float> rgb;
    std::vector<uint16_t> raw;
  };
  void send(const Project &p, double now);

  Shader shader_;
  std::map<int, std::unique_ptr<Sampler>> samplers_;
  std::map<UniverseKey, DmxUniverse> universes_;
  std::map<UniverseKey, uint8_t> sequence_;
  std::map<int, double> lastSend_;
  std::vector<std::string> conflicts_;
  OscSender socket_;
  std::array<uint8_t, 16> cid_{};
  int packetsSent_ = 0;
};

} // namespace lumen
