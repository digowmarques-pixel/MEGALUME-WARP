// UI language. The source strings are Portuguese; T() returns the English text
// when English is selected (or the same pointer when there is no translation).
#pragma once

namespace lumen {

enum class Language { Portuguese, English };

Language language();
void setLanguage(Language l);
// Language saved in the settings file, or the system's when none was chosen.
void loadLanguage();
void saveLanguage();

// Translates a label. "Texto##id" keeps the ImGui id suffix.
const char *T(const char *pt);
// Translates a zero-separated list ("a\0b\0c\0") for ImGui::Combo.
const char *TZ(const char *pt);

} // namespace lumen
