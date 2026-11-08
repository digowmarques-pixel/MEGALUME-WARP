// Lumen — media server for videomapping and interactive installations.
#include "app.h"
#include "media/convert.h"
#include "media/decoder.h"
#include "platform/platform.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// --convert-hap: one file after the other, progress on the terminal.
static int convertFiles(const std::vector<std::string> &files, lumen::HapVariant v) {
  int failed = 0;
  for (const std::string &f : files) {
    lumen::MediaInfo info;
    std::string err;
    if (!lumen::probeMedia(f, info, &err) || !info.hasVideo) {
      std::fprintf(stderr, "[hap] %s: %s\n", f.c_str(), err.empty() ? "sem vídeo" : err.c_str());
      ++failed;
      continue;
    }
    lumen::ConvertJob job;
    job.source = f;
    job.output = lumen::hapOutputPath(f, v);
    job.variant = v;
    job.width = info.width;
    job.height = info.height;
    job.duration = info.duration;
    lumen::HapConverter conv;
    conv.add(job);
    conv.waitAll();
    lumen::ConvertJob r = conv.jobs().front();
    if (r.state == lumen::ConvertJob::State::Done) std::printf("[hap] %s -> %s\n", f.c_str(), r.output.c_str());
    else {
      std::fprintf(stderr, "[hap] %s: %s\n", f.c_str(), r.error.c_str());
      ++failed;
    }
  }
  return failed ? 1 : 0;
}

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
      "  --operator              abre no Modo Operador (loja)\n"
      "  --machine-code          mostra o código desta máquina (para pedir a licença) e sai\n"
      "  --license ARQUIVO       ativa a licença (.lumenlic) neste computador e sai\n"
      "  --sync-upload           envia os quadros à GPU sem PBO (comparação / driver com problema)\n"
      "  --role main|backup      máquina principal ou reserva (sem salvar a configuração)\n"
      "  --sync-peer IP[:PORTA]  a outra máquina (vazio = broadcast na rede)\n"
      "  --sync-port N           porta UDP da sincronia (padrão 7400)\n"
      "  --size LxA              tamanho da janela de controle\n"
      "  --frames N              sai depois de N quadros (testes)\n"
      "  --quit-after S          sai depois de S segundos, em tempo real (testes)\n"
      "  --test-audio-stall S    para a placa de som depois de S segundos (testes)\n"
      "  --test-audio-record F   grava em F (WAV) o som enviado à placa (testes)\n"
      "  --test-timecode A-B@T;  timecode recebido: de A a B s de execução, T, T+1... (testes)\n"
      "  --screenshot-canvas F   salva o canvas em F (.ppm) no último quadro\n"
      "  --screenshot-ui F       salva a interface em F (.ppm) no último quadro\n"
      "  --screenshot-output F   salva a saída 1 em F (.ppm) no último quadro\n"
      "  --screenshot-outputs P  salva todas as saídas em P1.ppm, P2.ppm... (1 px = 1 px do canvas)\n"
      "  --blend CxL:SOBR:LxA    aplica o assistente de blend, ex.: 3x1:0.15:1920x1080\n"
      "  --pattern N             padrão de teste em todas as saídas (0 nenhum, 1 card, 2 grade, 3 branco, 4 preto)\n"
      "  --black-level V         compensação de preto em todas as saídas\n"
      "  --convert-hap ARQ...    converte vídeos para HAP (ao lado do original) e sai\n"
      "  --hap-variant V         hap (padrão), q ou alpha, para --convert-hap\n",
      LUMEN_VERSION);
}

#ifdef _WIN32
// Laptops with two GPUs (NVIDIA Optimus, AMD switchable graphics) give a
// program the integrated one unless the program exports these.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char **argv) {
#ifndef _WIN32
  // A device or controller that closes its TCP connection must give an error,
  // not SIGPIPE (which ends the program).
  std::signal(SIGPIPE, SIG_IGN);
#endif
  lumen::platform::attachConsole();
  lumen::platform::keepRunningSmoothly();
  lumen::Options opts;
  std::vector<std::string> args = lumen::platform::utf8Args(argc, argv);
  int count = int(args.size());
  std::vector<std::string> convertList;
  lumen::HapVariant variant = lumen::HapVariant::Hap;
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
    else if (a == "--machine-code") { std::printf("%s\n", lumen::machineCode().c_str()); return 0; }
    else if (a == "--license") { // installation by a script, or support over the phone
      std::string text;
      if (!lumen::platform::readFile(next(), text)) { std::fprintf(stderr, "não foi possível ler o arquivo\n"); return 2; }
      lumen::License lic;
      lumen::LicenseCheck c = lumen::installLicense(text, lic);
      if (c != lumen::LicenseCheck::Ok) {
        std::fprintf(stderr, "%s (código desta máquina: %s)\n", lumen::licenseCheckText(c), lumen::machineCode().c_str());
        return 1;
      }
      std::printf("Licença ativada: %s (%s)\n", lic.name.c_str(), lic.id.c_str());
      return 0;
    }
    else if (a == "--launch") opts.launchColumn = std::atoi(next().c_str());
    else if (a == "--no-outputs") opts.noOutputs = true;
    else if (a == "--outputs") opts.openOutputs = true;
    else if (a == "--output-controls") opts.outputControls = true;
    else if (a == "--diagnose") opts.diagnose = true;
    else if (a == "--show-mode") opts.showMode = true;
    else if (a == "--operator") opts.operatorMode = true;
    else if (a == "--sync-upload") opts.syncUpload = true;
    else if (a == "--role") opts.role = next();
    else if (a == "--sync-peer") opts.syncPeer = next();
    else if (a == "--sync-port") opts.syncPort = std::atoi(next().c_str());
    else if (a == "--check-effects") {
      opts.checkEffects = true;
      if (i + 1 < int(args.size()) && args[size_t(i + 1)][0] != '-') opts.checkEffectsDir = args[size_t(++i)];
    }
    else if (a == "--frames") opts.frames = std::atoi(next().c_str());
    else if (a == "--quit-after") opts.quitAfter = std::atof(next().c_str());
    else if (a == "--test-audio-stall") opts.testAudioStall = std::atof(next().c_str());
    else if (a == "--test-drop") opts.testDrop = next();
    else if (a == "--test-timecode") opts.testTimecode = next();
    else if (a == "--test-audio-record") opts.testAudioRecord = next();
    else if (a == "--screenshot-canvas") opts.screenshotCanvas = next();
    else if (a == "--screenshot-ui") opts.screenshotUi = next();
    else if (a == "--screenshot-output") opts.screenshotOutput = next();
    else if (a == "--screenshot-outputs") opts.screenshotOutputs = next();
    else if (a == "--blend") opts.blend = next();
    else if (a == "--pattern") opts.pattern = std::atoi(next().c_str());
    else if (a == "--convert-hap") {
      convertList.push_back(next());
      while (i + 1 < count && args[size_t(i + 1)][0] != '-') convertList.push_back(args[size_t(++i)]);
    } else if (a == "--hap-variant") {
      std::string v = next();
      variant = v == "q" ? lumen::HapVariant::HapQ : v == "alpha" ? lumen::HapVariant::HapAlpha : lumen::HapVariant::Hap;
    }
    else if (a == "--black-level") opts.blackLevel = float(std::atof(next().c_str()));
    else if (a == "--size") {
      std::string s = next();
      std::sscanf(s.c_str(), "%dx%d", &opts.width, &opts.height);
    } else if (!a.empty() && a[0] != '-') opts.projectPath = a;
    else { usage(); return 2; }
  }
  if (!convertList.empty()) return convertFiles(convertList, variant);
  lumen::App app;
  return app.run(opts);
}
