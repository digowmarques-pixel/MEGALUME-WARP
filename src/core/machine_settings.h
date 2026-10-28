// Settings of this computer (not of the project): language, redundancy role...
// Stored as JSON in platform::settingsDir()/settings.json.
#pragma once

#include <string>

namespace lumen {

std::string machineSetting(const std::string &key, const std::string &fallback = "");
int machineSettingInt(const std::string &key, int fallback);
void setMachineSetting(const std::string &key, const std::string &value);
void setMachineSetting(const std::string &key, int value);

} // namespace lumen
