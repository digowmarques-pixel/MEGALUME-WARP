// Layer effects: a stack of ISF filters (and the blob tracker) applied to the
// media of a layer before it is mapped. Also the catalog of ISF files.
#pragma once

#include "core/blob.h"
#include "core/project.h"
#include "media/source.h"
#include "render/isf_renderer.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

// ISF files known to the program: the built-in folder plus files used by the
// project. Headers are parsed without a GL context (for the inspector).
class IsfCatalog {
public:
  struct Entry {
    std::string path, name;
    IsfInfo info;
    std::string error;
  };
  // Built-in shaders (scanned once; rescan() after adding files).
  const std::vector<Entry> &builtins();
  void rescan() { scanned_ = false; }
  // Header of any .fs file (re-read when the file changes). Null if unreadable.
  const IsfInfo *info(const std::string &path);
  std::string error(const std::string &path);
  static const IsfInfo &blobTrackerInfo();

private:
  struct Cached {
    IsfInfo info;
    std::string error;
    long long mtime = -1;
    double checkedAt = -10;
    bool ok = false;
  };
  std::vector<Entry> builtins_;
  bool scanned_ = false;
  std::map<std::string, Cached> cache_;
};

// Shared GPU helpers of the effect chains (main context).
struct EffectTools {
  bool init();
  Shader luma; // downsample to grayscale for the blob tracker
  Generators *generators = nullptr;
};

struct BlobReport {
  std::vector<Blob> blobs;
  bool sendOsc = false;
};

class EffectChain {
public:
  // `src` follows the image convention (row 0 = top); so does the result.
  // Returns `src` when no effect is enabled.
  const Texture *apply(const std::vector<Effect> &effects, const Texture *src, double time, EffectTools &tools,
                       DynamicMesh &mesh, BlobReport *blobs, bool learnBackground);
  // Grayscale preview of the blob tracker's input (for the inspector).
  const BlobTracker *tracker() const;

private:
  struct BlobState {
    BlobTracker tracker;
    Framebuffer small;
    Texture maskTex;
    Framebuffer view;
    std::vector<unsigned char> rgba;
    std::vector<uint8_t> luma;
  };
  // Runtime state per effect, keyed by its file and how many effects of the
  // same file come before it: reordering or removing other effects keeps a
  // blob tracker's background and an ISF effect's trails (by position they
  // were reset, or handed to another effect).
  std::map<std::string, std::unique_ptr<IsfRenderer>> isf_;
  std::map<std::string, std::unique_ptr<BlobState>> blob_;
  Framebuffer in_, out_;
};

} // namespace lumen
