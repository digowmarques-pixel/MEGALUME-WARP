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
      "  --no-outputs            não abre as janelas de saída\n"
      "  --size LxA              tamanho da janela de controle\n"
      "  --frames N              sai depois de N quadros (testes)\n"
      "  --screenshot-canvas F   salva o canvas em F (.ppm) no último quadro\n"
      "  --screenshot-ui F       salva a interface em F (.ppm) no último quadro\n"
      "  --screenshot-output F   salva a saída 1 em F (.ppm) no último quadro\n",
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
    else if (a == "--frames") opts.frames = std::atoi(next().c_str());
    else if (a == "--screenshot-canvas") opts.screenshotCanvas = next();
    else if (a == "--screenshot-ui") opts.screenshotUi = next();
    else if (a == "--screenshot-output") opts.screenshotOutput = next();
    else if (a == "--size") {
      std::string s = next();
      std::sscanf(s.c_str(), "%dx%d", &opts.width, &opts.height);
    } else if (!a.empty() && a[0] != '-') opts.projectPath = a;
    else { usage(); return 2; }
  }
  lumen::App app;
  return app.run(opts);
}
