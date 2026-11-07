// OS-specific helpers: UTF-8 paths, file dialogs, monitors.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lumen::platform {

// All strings in Lumen are UTF-8; these convert to/from native paths.
std::filesystem::path toPath(const std::string &utf8);
std::string toUtf8(const std::filesystem::path &p);

bool readFile(const std::string &path, std::string &out);
// Writes through a temp file that is flushed to the disk before it replaces
// `path`, so a crash or power cut leaves either the old or the new file, never
// a half-written one. keepBackup: the previous file stays as `path`.bak.
bool writeFile(const std::string &path, const std::string &data, bool keepBackup = false);

// Native file dialogs. Return empty when cancelled or unavailable.
// `filter` example: "Media|*.mp4;*.mov;*.png|Lumen project|*.lumen"
std::vector<std::string> openFileDialog(const std::string &title, const std::string &filter,
                                        bool multiple);
std::string saveFileDialog(const std::string &title, const std::string &filter,
                           const std::string &defaultName);
std::string folderDialog(const std::string &title);
bool hasNativeDialogs();
// Called over and over on the calling thread while a file dialog is open, so
// the outputs (and the backup machine's heartbeat) keep running behind it.
void setDialogPump(std::function<void()> pump);
// Runs `work` on another thread and keeps the show running (the same pump)
// until it is done: long file work started from the interface.
void runKeepingShow(const std::function<void()> &work);

// Opens a file or folder with its default program (Explorer, Finder, xdg-open).
void openInShell(const std::string &path);

std::string executableDir();
// Per-user folder for settings and autosaves (created if missing).
std::string settingsDir();

// Windows: reuse the parent terminal for stdout/stderr when there is one.
void attachConsole();
// Windows: no sleep, screen saver or display power-off while Lumen runs, 1 ms
// timers, no power throttling (EcoQoS) and above-normal priority.
void keepRunningSmoothly();
// Shows a message box (Windows) and logs it. Used for errors before/without the UI.
void fatalError(const std::string &message);
std::string logPath(); // where stdout/stderr go when there is no console

// Command-line arguments as UTF-8 (Windows passes them in the ANSI code page).
std::vector<std::string> utf8Args(int argc, char **argv);

} // namespace lumen::platform
