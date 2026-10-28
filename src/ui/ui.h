// Control window user interface (Dear ImGui).
#pragma once

#include "core/project.h"
#include "media/decoder.h"
#include "render/gpu.h"
#include "ui/i18n.h"

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

namespace lumen {

class App;
struct OutputConfig;

enum class Tool { Select, Perspective, Mesh, Mask, Led };

class Workspace {
public:
  void draw(App &app, ImVec2 size);
  Tool tool = Tool::Select;
  int viewOutputId = -1; // -1: canvas view, otherwise preview of that output
  int viewCanvas = 0;    // canvas shown when viewOutputId < 0
  bool fitRequested = false;
  bool lockView = false; // embedded use: no canvas/output view selector
  void handleShortcuts(App &app);

private:
  void drawCanvasOverlay(App &app, ImDrawList *dl, ImVec2 origin, float scale);
  void drawOutputOverlay(App &app, ImDrawList *dl, ImVec2 origin, ImVec2 size);
  // Draws LED mappings; with the LED tool also edits them. Returns true if it handled the input.
  bool drawLedOverlay(App &app, ImDrawList *dl, ImVec2 origin, float scale);
  void fit(ImVec2 avail, float contentW, float contentH);
  // Point editing shared by layer masks (canvas px) and output masks (0..1).
  // Returns true when the mask changed. dragBase: first of 4 drag kinds.
  bool editMask(Mask &m, int &point, const std::function<ImVec2(Vec2)> &toScreen, Vec2 mouse,
                float pixelsPerUnit, ImDrawList *dl, ImU32 color, int dragBase);

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
  void openOutputs() { showOutputs_ = true; outputsPage_ = 0; }
  // Output shown in the mapping page of the Outputs window (-1: none).
  int editedOutput() const { return showOutputs_ && outputsPage_ == 1 ? outputsSel_ : -1; }
  void draw(App &app);
  Workspace workspace;

private:
  void menuBar(App &app);
  void recoveryPopup(App &app);
  bool showDiag_ = false;
  std::string diagReport_;
  void relinkDialog(App &app);
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
  void softEdgeControls(App &app, OutputConfig *o);
  void blackAndColorControls(App &app, OutputConfig *o);
  void effectsUi(App &app, Layer &l);
  void timelineEditor(App &app);
  void showTimeline(int id) { timelineSel_ = id; bottomTab_ = 1; forceBottom_ = true; }
  void blobUi(App &app, Layer &l);
  void openInputMesh(int layerId, int slice, int outputId);
  void inputMeshWindow(App &app);
  struct InputMeshEdit { bool open = false; int layerId = -1, slice = 0, outputId = -1, point = -1; } inputMesh_;
  void maskList(App &app, std::vector<Mask> &masks, int &sel, int &selPoint, Vec2 unit, Vec2 area,
                bool outputSpace);
  void outputMaskControls(App &app, OutputConfig *o, Workspace &ws);
  void outputsWindow(App &app);   // Millumin-style output setup (layout + mapping pages)
  void columnPopup(App &app, int columnIndex);
  void ledInspector(App &app);
  void projectInspector(App &app);

  bool showOutputs_ = false;
  int outputsPage_ = 0;   // 0 layout, 1 mapping of one output
  int outputsSel_ = -1;   // output id shown in the mapping page
  int mappingTab_ = 0;    // slice, mapping, masks, colour
  Workspace outputsWs_;   // workspace embedded in the mapping page
  int calibrating_ = -1; // mapping whose input range is being learnt
  float calMin_ = 0, calMax_ = 0;
  ImFont *bigFont_ = nullptr;
  int bottomTab_ = 0;     // 0 dashboard, 1 timeline
  bool forceBottom_ = false;
  int timelineSel_ = -1;  // timeline shown in the editor
  float timelineZoom_ = 40; // pixels per second
  float timelineView_ = 0, tlStart_ = 0;
  int tlViewFor_ = -1, tlFlag_ = -1, tlKeyTrack_ = -1, tlKey_ = -1, tlDragTrack_ = -1;
  bool tlDragFlag_ = false;
  float tlDragMin_ = 0, tlDragMax_ = 1;
  int dashZoom_ = 1;      // 0 small, 1 medium, 2 large cells
  bool showDemo_ = false;
  bool showHelp_ = false;
  char filterText_[128] = "";
  char liveUrl_[512] = "";
  std::vector<CaptureDevice> captureDevices_;
  std::vector<std::string> spoutSenders_;
};

} // namespace lumen
