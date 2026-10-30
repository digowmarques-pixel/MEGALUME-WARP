// Control window user interface (Dear ImGui).
#pragma once

#include "core/project.h"
#include "media/decoder.h"
#include "render/gpu.h"
#include "ui/i18n.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

struct ImGuiWindow;

namespace lumen {

class App;
struct OutputConfig;

enum class Tool { Select, Perspective, Mesh, Mask, Led, Scale }; // values index the hints: only append

class Workspace {
public:
  void draw(App &app, ImVec2 size);
  Tool tool = Tool::Select;
  bool showLed = false; // LED mapping tools shown (Janela > LED mapping)
  // Magnet: moved / scaled slices snap to the canvas, the outputs and the other
  // slices (edges and centres). Ctrl held inverts it for the drag.
  bool snap = true;
  // Masks: a click adds a point only when this is on (button in the Masks
  // section); off, clicks select and drag points. A mask with fewer than 3
  // points always takes new ones (it has no shape yet).
  static inline bool maskAddPoints = false;
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
  bool autoFit_ = true; // keeps refitting when the panel changes size, until you zoom or pan
  ImVec2 fitAvail_{0, 0};
  int lastCanvas_ = 0, lastView_ = -2; // per workspace: the main one and the Outputs window's
  Vec2 dragLast_;                      // mask move: last mouse position in mask units
  bool dragging_ = false;
  ImVec2 dragStart_{0, 0};
  int dragKind_ = 0; // 1 corner, 2 mesh, 3 slice move, 4 mask point, 5 mask move, 6 output corner, 7 output mesh, 8 scale
  // Move / scale drags start from these (restored each frame, then the whole
  // movement applied): snapping never accumulates rounding.
  std::vector<Warp> dragWarps_;
  Vec2 dragPos_, dragMouse_;
  int scaleHandle_ = -1; // 0-3 corners (TL TR BR BL), 4-7 sides (T R B L)
  void beginWarpDrag(const Layer &l, Vec2 mouseCanvas);
  void restoreWarpDrag(Layer &l) const;
  Framebuffer preview_;
};

class Ui {
public:
  // The dashboard cell under a screen point (last frame's layout): files dropped
  // there from the file manager go into it. False (column / layer -1) elsewhere.
  bool dashboardCellAt(float x, float y, int &column, int &layerId) const;
  void setup();
  void openOutputs() { showOutputs_ = true; outputsPage_ = 0; }
  // Output shown in the mapping page of the Outputs window (-1: none).
  int editedOutput() const { return showOutputs_ && outputsPage_ == 1 ? outputsSel_ : -1; }
  void draw(App &app);
  Workspace workspace;

private:
  void menuBar(App &app);
  void recoveryPopup(App &app);
  void redundancyUi(App &app);
  bool syncEditInit_ = false;
  int syncRoleEdit_ = 0, syncPortEdit_ = 7400;
  char syncPeerEdit_[128] = {};
  bool showDiag_ = false;
  bool showAbout_ = false;
  // "ASIO compatible" logo (asio-logo.png next to the exe, only in builds with ASIO):
  // Steinberg's terms ask for it wherever ASIO is set up, and in the About box.
  Texture asioLogo_;
  bool asioLogoTried_ = false;
  void asioNotice();
  // Setup panels in their own windows (Janela menu): Saídas, Interação, Projeto, LED.
  bool panelOpen_[4] = {};
  void panelWindows(App &app);
  std::string diagReport_;
  void relinkDialog(App &app);
  void toolbar(App &app);
  void library(App &app);
  void dashboard(App &app);
  void mappingSection(App &app, Layer &layer, float CW, float CH);
  struct CellRect { float x0, y0, x1, y1; int column, layerId; };
  std::vector<CellRect> dashCells_;
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
  void audioRouteUi(App &app, Layer &l);
  void timelineEditor(App &app);
  void showTimeline(int id) { timelineSel_ = id; bottomTab_ = 1; forceBottom_ = true; }
  void blobUi(App &app, Layer &l);
  void openInputMesh(int layerId, int slice, int outputId);
  void inputMeshWindow(App &app);
  void sliceEditorWindow(App &app);
  void virtualMonitorsWindow(App &app);
  std::map<int, std::unique_ptr<Framebuffer>> virtualFbs_; // by output id
  int sliceEditorLayer_ = -1; // layer whose slice editor is open
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
  bool resetLayout_ = false; // Janela > Restaurar layout dos painéis
  int bottomTab_ = 0;     // 0 dashboard, 1 timeline
  bool forceBottom_ = false;
  int timelineSel_ = -1;  // timeline shown in the editor
  float timelineZoom_ = 40; // pixels per second
  float timelineView_ = 0, tlStart_ = 0;
  int tlViewFor_ = -1, tlFlag_ = -1, tlKeyTrack_ = -1, tlKey_ = -1, tlDragTrack_ = -1;
  bool tlDragFlag_ = false;
  float tlDragMin_ = 0, tlDragMax_ = 1;
  int dashZoom_ = 1;      // 0 small, 1 medium, 2 large cells
  float toolbarRowA_ = 700, toolbarRowB_ = 800; // widths of the two halves of the toolbar (last frame)
  bool toolbarTwoRows_ = false; // narrow window: the toolbar wraps into two rows
  float timeW_ = 380;   // Tempo panel width (drag its left edge); machine setting
  int timeCounter_ = 2; // Tempo panel clocks: 0 none, 1 elapsed, 2 remaining (like Millumin)
  // Tempo panel beside the dashboard: one row per layer, at the screen rows of the table.
  struct TimeRow { int layerId; float y1, y2; };
  void timePanel(App &app, const std::vector<TimeRow> &rows, float bodyTop, float bodyBottom, float height,
                 ImGuiWindow *tableWindow, float rowStep);
  bool showDemo_ = false;
  bool showHelp_ = false;
  char filterText_[128] = "";
  char liveUrl_[512] = "";
  std::vector<CaptureDevice> captureDevices_;
  std::vector<std::string> spoutSenders_;
};

} // namespace lumen
