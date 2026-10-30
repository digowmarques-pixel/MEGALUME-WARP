// Application shell: windows, main loop, file operations and undo history.
#pragma once

#include "engine/engine.h"
#include "engine/outputs.h"

#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lumen {

class Ui;

struct Options {
  std::string projectPath;
  std::string screenshotCanvas; // write the canvas to a PPM and exit
  std::string screenshotUi;     // write the control window to a PPM and exit
  std::string screenshotOutput; // write output #1 preview to a PPM and exit
  int frames = 0;               // exit after N frames (0 = run forever)
  int launchColumn = -1;        // 1-based column to launch at start
  bool noOutputs = false;       // never open output windows
  bool openOutputs = false;     // open output windows at start (installations)
  bool outputControls = false;  // calibration overlay on the first output
  bool checkEffects = false;    // compile every built-in ISF shader and exit
  std::string checkEffectsDir;  // another folder of .fs files
  bool diagnose = false;        // print the system self-test report and exit
  bool showMode = false;        // start with editing locked
  bool syncUpload = false;      // never use the asynchronous (PBO) upload
  std::string role;             // "main" / "backup": overrides the machine setting
  std::string syncPeer;
  int syncPort = 0;
  double quitAfter = 0;         // tests in real time: exit after this many seconds
  double testAudioStall = 0;    // tests: the sound card stops after this many seconds
  // tests: timecode received, "A-B@T;..." (from A to B seconds of running: T, T+1...; none outside)
  std::string testTimecode;
  // tests: "X,Y,file[,file...]" dropped at window point X,Y after 3 s (as from the file manager)
  std::string testDrop;
  std::string testAudioRecord;  // tests: WAV file with what was sent to the sound card
  int width = 1600, height = 900;
  std::string blend;             // "3x1:0.15:1920x1080" applies the blend assistant at start
  int pattern = -1;              // test pattern for every output (TestPattern value)
  float blackLevel = -1;         // black level compensation for every output
  std::string screenshotOutputs; // prefix: writes <prefix>1.ppm... (1 px = 1 canvas px)
};

enum class Inspector { Layer, Cell, Media, Output, Led, Interaction, Project, Timecode };

struct Selection {
  int layerId = -1;
  int slice = 0;
  int mask = -1;
  int maskPoint = -1;
  int corner = -1;    // 0..3, perspective handles
  int meshPoint = -1; // index into the mesh
  int cellColumn = -1, cellLayer = -1;
  int mediaId = -1;
  int outputId = -1;
  int ledId = -1, dmxDeviceId = -1;
  int outMask = -1, outMaskPoint = -1;
  Inspector inspector = Inspector::Layer;
};

class History {
public:
  void reset(const std::string &snapshot);
  bool push(const std::string &snapshot); // returns true if it was a real change
  bool canUndo() const { return index_ > 0; }
  bool canRedo() const { return index_ + 1 < int(stack_.size()); }
  const std::string &undo() { return stack_[size_t(--index_)]; }
  const std::string &redo() { return stack_[size_t(++index_)]; }

private:
  std::vector<std::string> stack_;
  int index_ = -1;
};

class App {
public:
  App();
  ~App();
  int run(const Options &opts);

  // Owned through pointers so their GPU objects can be released while the GL
  // context still exists (end of run()); the references keep call sites simple.
  std::unique_ptr<Engine> enginePtr_ = std::make_unique<Engine>();
  std::unique_ptr<OutputManager> outputsPtr_ = std::make_unique<OutputManager>();
  Engine &engine = *enginePtr_;
  OutputManager &outputs = *outputsPtr_;
  Selection sel;
  GLFWwindow *window = nullptr;

  // File operations.
  void newProject();
  bool openProject(const std::string &path);
  bool saveProject(bool saveAs);
  void openDialog();
  void importDialog();
  void importFiles(const std::vector<std::string> &paths, int column = -1, int layerId = -1);

  // Undo: widgets call edited(); a snapshot is taken once the interaction ends.
  void edited() { pendingCommit_ = true; dirty = true; }
  void undo();
  void redo();
  bool dirty = false;

  void status(const std::string &msg, bool error = false);
  const std::string &statusText() const { return status_; }
  bool statusIsError() const { return statusError_; }
  double statusAge() const;

  float fps() const { return fps_; }
  bool requestQuit = false;
  // Output windows open only when the user asks (toolbar / Ctrl+P / --outputs).
  bool outputsOpen = false;
  // Draws the selected output's warp handles on the projector itself.
  bool showOutputControls = false;
  // Show mode: editing is locked, only show control (GO, stop, blackout...).
  bool showLocked = false;
  // Missing media: relinks every missing file found (by name) under `dir`.
  int relinkMissing(const std::string &dir);
  int missingMedia() const;
  bool mediaMissing(const MediaItem &m) const;
  static bool checkMissing(const MediaItem &m);
  mutable std::map<std::pair<int, std::string>, bool> missingCache_;
  mutable double missingCheckedAt_ = -10;
  // Crash recovery (autosave of the last session that did not close cleanly).
  bool recoveryPending() const { return !recoveryFile_.empty(); }
  const std::string &recoveryInfo() const { return recoveryInfo_; }
  void recover(bool yes);
  // Runs `action` now, or after asking what to do with unsaved changes
  // (save / discard / cancel): quit, new, open, dropping a project.
  void unlessUnsaved(std::function<void()> action);
  // Redundancy role of this computer, saved in the machine settings.
  void applySync(SyncRole role, const std::string &peer, int port, bool save);
  // System self-test (GPU, shaders, monitors, audio, MIDI, serial, Spout, NDI).
  std::string diagnosticReport();

private:
  void commitIfNeeded();
  std::function<void()> afterUnsaved_;
  bool askUnsaved_ = false;
  bool popupWasOpen_ = false; // a menu or dialog was open in the last frame
  void autosave();
  void clearAutosave() { clearAutosave(engine.project.filePath); }
  void clearAutosave(const std::string &projectPath);
  void checkRecovery();
  double lastAutosave_ = 0;
  std::string recoveryFile_, recoveryInfo_;
  void pollKeyboard(bool mainWindowKeys);
  std::vector<bool> keyDown_;
  void updateTitle();
  bool writePpm(const std::string &path, int w, int h, const std::vector<unsigned char> &rgba,
                bool flipY);

  std::unique_ptr<Ui> ui_;
  History history_;
  bool pendingCommit_ = false;
  std::string status_;
  bool statusError_ = false;
  double statusTime_ = 0;
  float fps_ = 0;
  std::string lastTitle_;
};

} // namespace lumen
