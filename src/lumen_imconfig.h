// Dear ImGui user config (IMGUI_USER_CONFIG): failed ImGui checks are reported
// with file and line through Lumen's error handler instead of a silent abort.
#pragma once

void lumenAssertFail(const char *expr, const char *file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : lumenAssertFail(#_EXPR, __FILE__, __LINE__))
