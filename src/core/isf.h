// ISF (Interactive Shader Format, isf.video): GLSL fragment shaders with a
// JSON header describing their parameters and render passes. Used for layer
// effects (filters), generators and transitions. This part is pure (no GL):
// header parsing, GLSL translation and pass size expressions.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace lumen {

struct IsfInput {
  std::string name, type; // float bool long color point2D image event audio audioFFT
  std::string label;
  std::vector<float> def, min, max; // 1 value (float/bool/long), 2 (point2D), 4 (color)
  std::vector<int> values;          // long: allowed values
  std::vector<std::string> labels;  // long: their names
  int size() const;                 // number of floats of the value
};

struct IsfPass {
  std::string target;               // "" = the output
  bool persistent = false, floatTarget = false;
  std::string width, height;        // expressions ("$WIDTH/2"), "" = render size
};

struct IsfInfo {
  std::string description, credit;
  std::vector<std::string> categories;
  std::vector<IsfInput> inputs;
  std::vector<IsfPass> passes;      // at least one
  std::vector<std::pair<std::string, std::string>> imported; // image name -> file (relative to the .fs)
  bool filter = false;              // has an "inputImage" image input
  bool transition = false;          // startImage + endImage + progress
  std::string body;                 // GLSL without the JSON header
  const IsfInput *input(const std::string &name) const;
};

bool parseIsf(const std::string &source, IsfInfo &out, std::string *error = nullptr);
// Complete GLSL 3.30 fragment shader (and the matching vertex shader).
std::string isfFragmentSource(const IsfInfo &info);
extern const char *kIsfVertexShader;
// Evaluates a pass size expression: numbers, $WIDTH, $HEIGHT, $<float input>,
// + - * / ( ), floor ceil round min max pow abs.
float isfEval(const std::string &expr, const std::map<std::string, float> &vars, bool *ok = nullptr);

// Parameter values by input name (floats as listed by IsfInput::size()).
using IsfValues = std::map<std::string, std::vector<float>>;
// Fills missing or wrongly sized values with the inputs' defaults.
void isfDefaults(const IsfInfo &info, IsfValues &values);

} // namespace lumen
