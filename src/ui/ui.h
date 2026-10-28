// Control window user interface (Dear ImGui).
#pragma once

#include "media/decoder.h"
#include "render/gpu.h"

#include <string>
#include <vector>

#include <imgui.h>

namespace lumen {

class App;

enum class Tool { Select, Perspective, Mesh, Mask };

class Workspace {
public:
  void draw(App &app, ImVec2 size);
  Tool tool = Tool::Select;
  int viewOutputId = -1; // -1: canvas view, otherwise preview of that output
  bool fitRequested = false;
  void handleShortcuts(App &app);

private:
  void drawCanvasOverlay(App &app, ImDrawList *dl, ImVec2 origin, float scale);
  void drawOutputOverlay(App &app, ImDrawList *dl, ImVec2 origin, ImVec2 size);
  void fit(ImVec2 avail, float contentW, float contentH);

  float zoom_ = 1;
  ImVec2 pan_{0, 0};
  bool fitted_ = false;
  bool dragging_ = false;
  ImVec2 dragStart_{0, 0};
  int dragKind_ = 0; // 1 corner, 2 mesh, 3 slice move, 4 mask point, 5 mask move, 6 output corner, 7 output mesh
  Framebuffer preview_;
};

class Ui {
public:
  void setup();
  void draw(App &app);
  Workspace workspace;

private:
  void menuBar(App &app);
  void toolbar(App &app);
  void library(App &app);
  void dashboard(App &app);
  void inspector(App &app);
  void statusBar(App &app);

  void layerInspector(App &app);
  void cellInspector(App &app);
  void mediaInspector(App &app);
  void outputInspector(App &app);
  void interactionInspector(App &app);
  void projectInspector(App &app);

  bool showDemo_ = false;
  bool showHelp_ = false;
  char filterText_[128] = "";
  char liveUrl_[512] = "";
  std::vector<CaptureDevice> captureDevices_;
  std::vector<std::string> spoutSenders_;
};

} // namespace lumen
