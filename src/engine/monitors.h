// Screens (monitors / projectors) and which one an output belongs to. No GL:
// the tests use it as it is.
#pragma once

#include "core/project.h"

#include <string>
#include <vector>

namespace lumen {

struct MonitorInfo {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;
  int refresh = 60;
};

// Finds the screen an output belongs to. Returns false when it is not
// connected now: the output stays closed (never on another screen).
inline bool resolveMonitor(OutputConfig &o, const std::vector<MonitorInfo> &mons) {
  if (o.monitor < 0) {
    o.monitorRecorded = -1;
    return true;
  }
  if (o.monitorName.empty() || o.monitor != o.monitorRecorded) { // just chosen (or an older file)
    if (o.monitor >= int(mons.size())) return false;
    const MonitorInfo &m = mons[size_t(o.monitor)];
    o.monitorName = m.name;
    o.monitorX = m.x;
    o.monitorY = m.y;
    o.monitorRecorded = o.monitor;
    return true;
  }
  // The same screen means the same name AND the same place on the desktop.
  // Windows calls most screens "Generic PnP Monitor": matching by the name
  // alone, a projector switched off found the notebook's own screen (the only
  // one left with that name) and covered it, above every other window.
  int at = -1;
  for (size_t i = 0; i < mons.size(); ++i)
    if (mons[i].name == o.monitorName && mons[i].x == o.monitorX && mons[i].y == o.monitorY) at = int(i);
  if (at < 0) return false; // not connected (or moved in Windows' display settings: choose it again)
  o.monitor = o.monitorRecorded = at;
  return true;
}

} // namespace lumen
