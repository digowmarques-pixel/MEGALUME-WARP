// Small icons drawn with ImGui shapes (no icon font needed: they look the
// same on every Windows): hand and magnet buttons of the toolbars.
#pragma once

#include <imgui.h>

namespace lumen {

enum class Icon { Hand, Magnet, Lock };

// A button showing `icon` in the current text colour (the caller pushes the
// button colours, e.g. for the active state). `id` is an ImGui id ("##hand").
bool iconButton(const char *id, Icon icon, ImVec2 size);
// Draws `icon` of size s centred at c.
void drawIcon(ImDrawList *dl, Icon icon, ImVec2 c, float s, ImU32 col);

} // namespace lumen
