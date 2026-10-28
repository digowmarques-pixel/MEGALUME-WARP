// Output windows (projectors / screens). Each output is a borderless window
// on a monitor (or a normal window for testing) sharing the main GL context.
#pragma once

#include "core/project.h"
#include "render/gpu.h"

#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lumen {

struct MonitorInfo {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;
  int refresh = 60;
};

class OutputManager {
public:
  bool init(GLFWwindow *mainWindow);
  void shutdown();

  // Opens / closes / moves windows so they match the project.
  void sync(Project &p);
  // Draws every output from the canvas texture. Restores the main context.
  void render(const Project &p, const Texture &canvas, double time, bool blackout);
  // Draws an output into a framebuffer of the main context (workspace preview).
  void renderPreview(const Project &p, const OutputConfig &o, int outputIndex,
                     const Texture &canvas, Framebuffer &target, DynamicMesh &mesh,
                     double time, bool blackout);

  static std::vector<MonitorInfo> monitors();
  int openCount() const { return int(windows_.size()); }
  bool windowFor(int outputId) const;

private:
  struct Window {
    int outputId = 0;
    GLFWwindow *handle = nullptr;
    std::unique_ptr<DynamicMesh> mesh;
    int monitor = -1;
    int x = 0, y = 0, w = 0, h = 0;
    std::string title;
  };
  void destroy(Window &w);
  void draw(const Project &p, const OutputConfig &o, int index, const Texture &canvas,
            DynamicMesh &mesh, int w, int h, double time, bool blackout);
  void updateSwapIntervals();

  GLFWwindow *main_ = nullptr;
  Shader shader_;
  std::vector<std::unique_ptr<Window>> windows_;
  bool swapDirty_ = true;
};

} // namespace lumen
