// Arithmetic typed into number fields (as in Resolume): "1920/3", "(1080-40)*2",
// or an operation on the current value: "/3", "+100", "*2".
#pragma once

#include <string>

namespace lumen {

// Evaluates `text`; a leading + - * / applies to `current` ("-10" is a plain
// negative number). False when it is not a valid expression (or divides by 0).
bool evalNumber(const std::string &text, double current, double &out);

} // namespace lumen
