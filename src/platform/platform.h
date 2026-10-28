// OS-specific helpers: UTF-8 paths, file dialogs, monitors.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lumen::platform {

// All strings in Lumen are UTF-8; these convert to/from native paths.
std::filesystem::path toPath(const std::string &utf8);
std::string toUtf8(const std::filesystem::path &p);

bool readFile(const std::string &path, std::string &out);
bool writeFile(const std::string &path, const std::string &data);

// Native file dialogs. Return empty when cancelled or unavailable.
// `filter` example: "Media|*.mp4;*.mov;*.png|Lumen project|*.lumen"
std::vector<std::string> openFileDialog(const std::string &title, const std::string &filter,
                                        bool multiple);
std::string saveFileDialog(const std::string &title, const std::string &filter,
                           const std::string &defaultName);
bool hasNativeDialogs();

std::string executableDir();

// Windows: reuse the parent terminal for stdout/stderr when there is one.
void attachConsole();
// Shows a message box (Windows) and logs it. Used for errors before/without the UI.
void fatalError(const std::string &message);
std::string logPath(); // where stdout/stderr go when there is no console

// Command-line arguments as UTF-8 (Windows passes them in the ANSI code page).
std::vector<std::string> utf8Args(int argc, char **argv);

} // namespace lumen::platform
