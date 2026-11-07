// Control window user interface (Dear ImGui).
#pragma once

#include "core/project.h"
#include "media/decoder.h"
#include "media/source.h"
#include "render/gpu.h"
#include "ui/i18n.h"

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

struct ImGuiWindow;

namespace lumen {

class App;
struct OutputConfig;

enum class Tool { Select, Perspective, Mesh, Mask, Led, Scale, Hand }; // values index the hints: only append

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
  // The Workspace panel was hovered or focused last frame: only then do the
  // arrows, Tab and Delete act on its selection (pressed while working in the
  // Outputs window or the dashboard they moved the canvas mapping too).
  bool keysActive = false;
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
  int dragLayerId_ = -1; // the layer dragWarps_ were taken from
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
  // Operator mode: the window's close button (or Alt+F4) asks for the PIN.
  void requestOperatorPin() { askOperatorPin_ = true; operatorPin_.clear(); operatorPinWrong_ = false; }
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
  // "Remover colunas...": several columns of the current dashboard at once.
  bool showRemoveColumns_ = false;
  std::vector<int> removeColumnIds_; // ticked column ids
  void removeColumnsWindow(App &app);
  // "ASIO compatible" logo (asio-logo.png next to the exe, only in builds with ASIO):
  // Steinberg's terms ask for it wherever ASIO is set up, and in the About box.
  Texture asioLogo_;
  bool asioLogoTried_ = false;
  void asioNotice();
  // Setup panels in their own windows (Janela menu): Saídas, Interação, Projeto, LED, Timecode,
  // Conversor HAP.
  bool panelOpen_[6] = {};
  void converterWindow(App &app);
  // Operator mode (shop): big cue buttons of one dashboard, Stop, Blackout,
  // volume; the lock asks for the PIN. Its setup lives in Janela.
  void operatorScreen(App &app);
  void operatorSetupWindow(App &app);
  bool showOperatorSetup_ = false;
  bool askOperatorPin_ = false;
  std::string operatorPin_;
  bool operatorPinWrong_ = false;
  int operatorPinFails_ = 0;        // wrong PINs in a row
  double operatorPinLockUntil_ = 0; // after 5 wrong: wait (glfw time)
  int operatorSel_ = -1; // column id selected (Play puts it on air)
  // Preview (double click on a cell or a library item, or its menu): plays one
  // media item in its own window, silent and outside the outputs, to check it
  // before giving GO.
  std::unique_ptr<Playback> preview_;
  int previewMediaId_ = -1;
  bool previewOpen_ = false;
  void openPreview(App &app, int mediaId);
  void previewWindow(App &app);
  // "Converter para HAP...": the library items it applies to, asked in a popup.
  std::vector<int> convertIds_;
  std::vector<std::string> convertFiles_; // files not in the library
  bool openConvert_ = false;
  int convertVariant_ = 0;   // HapVariant
  bool convertSwap_ = true;  // the project uses the new files
  void convertPopup(App &app);
  void panelWindows(App &app);
  std::string diagReport_;
  void relinkDialog(App &app);
  void toolbar(App &app);
  void library(App &app);
  void dashboard(App &app);
  void mappingSection(App &app, Layer &layer, float CW, float CH);
  struct CellRect { float x0, y0, x1, y1; int column, layerId; };
  std::vector<CellRect> dashCells_;
  void *dashWindow_ = nullptr; // the ImGui window the cells were drawn in
  void inspector(App &app);
  void statusBar(App &app);

  void layerInspector(App &app);
  void cellInspector(App &app);
  void mediaInspector(App &app);
  void outputInspector(App &app);
  void interactionInspector(App &app);
  void timecodeInspector(App &app);
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
  // Slice being edited in the Slice and Mapping tabs: 0 = the output's own
  // (Slice 1), k = its extra slice k-1. While the page is drawn that slice is
  // swapped into the output's source and warp, so every editor works on it.
  int outputSlice_ = 0;
  int outputSliceFor_ = -1;
  void outputSliceBar(App &app, OutputConfig &o);
  // Slice editor (Input Selection / Output Transformation, as Resolume's
  // Advanced Output): stage with every slice, toolbar and properties panel.
  struct StageView {
    float zoom = 0; // 0 = fit
    ImVec2 pan{0, 0};
  };
  StageView stageView_[2];
  bool stageHand_ = false;
  // Undo / redo asked from inside a window: done after every window is drawn
  // (they replace the project, which the windows hold references into).
  int pendingHistory_ = 0; // -1 undo, +1 redo
  bool stageEditPoints_[2] = {false, false};
  int stageDrag_ = 0, stageHandle_ = -1, stagePoint_ = -1;
  ImVec2 stageStartMouse_{0, 0};
  float stageStart_[6] = {};
  Warp stageStartWarp_;
  Framebuffer stagePreview_;
  std::string guideLoaded_; // output guide picture currently in guideTex_
  std::unique_ptr<Texture> guideTex_;
  std::future<VideoFrame> guideJob_; // decoding (another thread) the picture named guideJobPath_
  std::string guideJobPath_;
  void screenPanel(App &app, OutputConfig &o, float outW, float outH);
  // Resolume's "Match Output Shape": slice k takes from the canvas the same
  // place and size it has on the projector (pixel-perfect pixel maps).
  void matchOutputShape(App &app, OutputConfig &o, int k);
  void sliceEditor(App &app, OutputConfig &o, bool output);
  void slicePanel(App &app, OutputConfig &o, bool output, float outW, float outH);
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
