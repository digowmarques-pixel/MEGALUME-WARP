#include "core/machine_settings.h"

#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <mutex>

namespace lumen {

namespace {

std::mutex g_mutex;

std::string file() { return platform::toUtf8(platform::toPath(platform::settingsDir()) / "settings.json"); }

nlohmann::json load() {
  std::string text;
  if (!platform::readFile(file(), text)) return nlohmann::json::object();
  auto j = nlohmann::json::parse(text, nullptr, false);
  return j.is_object() ? j : nlohmann::json::object();
}

template <class T> void store(const std::string &key, const T &value) {
  std::lock_guard<std::mutex> lock(g_mutex);
  nlohmann::json j = load();
  j[key] = value;
  platform::writeFile(file(), j.dump(2));
}

} // namespace

std::string machineSetting(const std::string &key, const std::string &fallback) {
  std::lock_guard<std::mutex> lock(g_mutex);
  nlohmann::json j = load();
  return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}

int machineSettingInt(const std::string &key, int fallback) {
  std::lock_guard<std::mutex> lock(g_mutex);
  nlohmann::json j = load();
  return j.contains(key) && j[key].is_number_integer() ? j[key].get<int>() : fallback;
}

void setMachineSetting(const std::string &key, const std::string &value) { store(key, value); }
void setMachineSetting(const std::string &key, int value) { store(key, value); }

} // namespace lumen
