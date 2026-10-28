// Lumen — media server for videomapping and interactive installations.
#include "app.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void usage() {
  std::printf(
      "Lumen %s\n"
      "uso: lumen [projeto.lumen] [opções]\n"
      "  --launch N              dispara a coluna N ao abrir\n"
      "  --outputs               abre as janelas de saída ao iniciar (instalações)\n"
      "  --no-outputs            nunca abre as janelas de saída\n"
      "  --output-controls       desenha os cantos/malha da 1a saída no projetor\n"
      "  --check-effects         compila todos os shaders ISF da pasta effects e sai\n"
      "  --diagnose              testa GPU, shaders, monitores, áudio, MIDI, serial, Spout e NDI e sai\n"
      "  --show-mode             abre com a edição travada (modo show)\n"
      "  --size LxA              tamanho da janela de controle\n"
      "  --frames N              sai depois de N quadros (testes)\n"
      "  --screenshot-canvas F   salva o canvas em F (.ppm) no último quadro\n"
      "  --screenshot-ui F       salva a interface em F (.ppm) no último quadro\n"
      "  --screenshot-output F   salva a saída 1 em F (.ppm) no último quadro\n"
      "  --screenshot-outputs P  salva todas as saídas em P1.ppm, P2.ppm... (1 px = 1 px do canvas)\n"
      "  --blend CxL:SOBR:LxA    aplica o assistente de blend, ex.: 3x1:0.15:1920x1080\n"
      "  --pattern N             padrão de teste em todas as saídas (0 nenhum, 1 card, 2 grade, 3 branco, 4 preto)\n"
      "  --black-level V         compensação de preto em todas as saídas\n",
      LUMEN_VERSION);
}

int main(int argc, char **argv) {
  lumen::platform::attachConsole();
  lumen::Options opts;
  std::vector<std::string> args = lumen::platform::utf8Args(argc, argv);
  int count = int(args.size());
  for (int i = 1; i < count; ++i) {
    std::string a = args[size_t(i)];
    auto next = [&]() -> std::string {
      if (i + 1 >= count) {
        std::fprintf(stderr, "faltou valor para %s\n", a.c_str());
        std::exit(2);
      }
      return args[size_t(++i)];
    };
    if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (a == "--launch") opts.launchColumn = std::atoi(next().c_str());
    else if (a == "--no-outputs") opts.noOutputs = true;
    else if (a == "--outputs") opts.openOutputs = true;
    else if (a == "--output-controls") opts.outputControls = true;
    else if (a == "--diagnose") opts.diagnose = true;
    else if (a == "--show-mode") opts.showMode = true;
    else if (a == "--check-effects") {
      opts.checkEffects = true;
      if (i + 1 < int(args.size()) && args[size_t(i + 1)][0] != '-') opts.checkEffectsDir = args[size_t(++i)];
    }
    else if (a == "--frames") opts.frames = std::atoi(next().c_str());
    else if (a == "--screenshot-canvas") opts.screenshotCanvas = next();
    else if (a == "--screenshot-ui") opts.screenshotUi = next();
    else if (a == "--screenshot-output") opts.screenshotOutput = next();
    else if (a == "--screenshot-outputs") opts.screenshotOutputs = next();
    else if (a == "--blend") opts.blend = next();
    else if (a == "--pattern") opts.pattern = std::atoi(next().c_str());
    else if (a == "--black-level") opts.blackLevel = float(std::atof(next().c_str()));
    else if (a == "--size") {
      std::string s = next();
      std::sscanf(s.c_str(), "%dx%d", &opts.width, &opts.height);
    } else if (!a.empty() && a[0] != '-') opts.projectPath = a;
    else { usage(); return 2; }
  }
  lumen::App app;
  return app.run(opts);
}
