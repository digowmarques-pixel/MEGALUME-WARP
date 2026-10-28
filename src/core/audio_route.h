// Audio routing: which source channel of a media goes to which output
// channel of the sound card (per layer), as a list of matrix connections.
#pragma once

#include <vector>

namespace lumen {

constexpr int kMaxVoiceChannels = 8;   // 7.1
constexpr int kMaxOutputChannels = 32; // multichannel interfaces, Dante, MADI...

// One connection of a routing matrix: source channel -> output channel (0-based).
struct AudioPatch {
  int src = 0, dst = 0;
  float gain = 1;
  bool operator==(const AudioPatch &o) const { return src == o.src && dst == o.dst && gain == o.gain; }
};

// The connections actually used for a voice. `custom` (when not empty) is the
// layer's own matrix; otherwise mono goes to the pair starting at `firstOutput`,
// stereo to that pair, and multichannel files to consecutive outputs, or
// downmixed to the pair when the device has fewer channels. A first output
// beyond the device falls back to 1-2, so a show made for an 8-channel
// interface is still heard on a laptop.
std::vector<AudioPatch> resolveAudioRoute(int srcChannels, int outChannels, int firstOutput,
                                          const std::vector<AudioPatch> &custom);

} // namespace lumen
