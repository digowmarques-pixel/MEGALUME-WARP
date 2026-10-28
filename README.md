# Lumen

Media server para **videomapping** e **instalações interativas**, inspirado no
Millumin, feito para **Windows** (também compila no Linux).

## O que já funciona (v0.7)

| Área | Recursos |
|---|---|
| **Show** | Dashboard em colunas (cues) × layers, GO pela barra de espaço, crossfade por coluna, disparo de célula individual, blackout, master |
| **Mídia** | Vídeo via FFmpeg (H.264, HEVC, ProRes, HAP, VP9, AV1...) em thread separada, imagens (PNG, JPG, TIFF, WebP...), geradores: test card, cor sólida, degradê, grade, plasma, listras |
| **Áudio** | Som dos vídeos (WASAPI no Windows), volume e mudo por layer, o fade do áudio acompanha o fade da imagem, volume master, escolha da placa de som, medidores. O relógio do áudio comanda o vídeo, então imagem e som não se perdem nem depois de horas em loop |
| **Ao vivo** | Câmeras e placas de captura (DirectShow), streams de rede (RTSP, SRT, UDP, HTTP) e **Spout** (receber de TouchDesigner, Resolume, OBS, Unreal...). Uma câmera usada por várias layers abre uma vez só. Reconecta sozinho se o sinal cair |
| **Desempenho** | Conversão de cor YUV → RGB na GPU (8, 10 e 12 bits, BT.601/709/2020), **decodificação por hardware** (D3D11VA/DXVA2), **HAP / HAP Alpha / HAP Q / HAP Q Alpha direto na GPU** como textura comprimida, sem decodificar na CPU |
| **Videomapping** | Fatias (slices) com recorte UV, perspectiva de 4 cantos (homografia, com troca automática para bilinear quando o quadrilátero fica côncavo), malha deformável (mesh warp) até 33×33, máscaras poligonais com borda suave, bordas com anti-aliasing |
| **Composição** | Blend normal / adição / multiplicar / tela, opacidade, brilho, contraste, saturação, matiz, inverter, tinta, transformação (posição, escala, rotação) por layer |
| **Saídas / blend** | Várias saídas (abertas só quando você pede: **Abrir saídas** / Ctrl+P), cada uma num monitor sem bordas. **Assistente de blend** para grades de projetores, soft edge com curva complementar, **compensação de nível de preto**, **cor por projetor**, **máscaras por saída**, warp por saída (keystone e malha), padrões de teste (card, **grade contínua**, branco, preto) e identificação das saídas. Veja [docs/BLEND.md](docs/BLEND.md) |
| **Interação** | Servidor **OSC** (UDP) com comandos `/lumen/...`, **MIDI** (controladores USB, com reconexão automática), **mapeamentos** sensor/MIDI → parâmetro com faixa, suavização e botão **Aprender**, monitor de mensagens |
| **LED mapping** | Pixel mapping de fitas e matrizes de LED por **Art-Net** e **sACN**: RGB/BGR/GRB/RGBW/mono, 8/16 bits, zigzag, inversão, curva de brilho, vários universos automáticos, monitor DMX e DMX master. Veja [docs/LED.md](docs/LED.md) |
| **Saída de controle** | **OSC de saída**: status do cue para TouchOSC/Companion e **ações OSC por coluna** (disparar luz, som, outros computadores). **Spout de saída**: o canvas inteiro vai para OBS, TouchDesigner, Resolume... |
| **Show automático** | Fim da mídia por célula (loop, congelar, parar, **próxima coluna**), **auto-GO** com contagem regressiva, **estados por coluna** (parâmetros animados na transição), **agenda** por horário, projeto que abre as saídas e dispara uma coluna ao abrir |
| **Surfaces** | Mapping compartilhado por várias layers: ajuste uma vez quando o cenário muda |
| **Máscaras e warp avançados** | Máscaras com curvas bezier, pilha mostrar/esconder, invertida, opacidade e SVG; malha de entrada (pontos sobre os detalhes da imagem) e duplo clique para adicionar pontos |
| **Video routing** | Vários canvases, saída escolhe o canvas, canvas como mídia de outro, blend automático para projetores fora de grade, Spout por saída (com ou sem janela) |
| **Compatível com Millumin** | API OSC do Millumin (`/millumin/action/launchColumn`, `/layer:Nome/opacity`...), feedback `launchedColumn`; teclado e **DMX de entrada** como interação; transformações chave / alternar / acumular. Veja [docs/MILLUMIN.md](docs/MILLUMIN.md) |
| **Projeto** | Arquivo `.lumen` (JSON legível), caminhos relativos, desfazer/refazer, arrastar arquivos do Explorer |

## Baixar pronto (sem compilar)

Cada envio para o GitHub dispara o workflow [`build`](.github/workflows/build.yml),
que compila no Windows e publica o artefato **Lumen-windows-x64**
(aba *Actions* → execução mais recente → *Artifacts*). Descompacte e rode `lumen.exe`.

## Compilar no Windows

Requisitos:

- **Visual Studio 2022** com a carga de trabalho *Desenvolvimento para desktop com C++*
- **CMake 3.20+** (já vem com o Visual Studio)
- **vcpkg**, que instala FFmpeg e GLFW automaticamente:

```bash
git clone https://github.com/microsoft/vcpkg C:\vcpkg
```

```bash
C:\vcpkg\bootstrap-vcpkg.bat
```

Na pasta do projeto (*Developer PowerShell for VS 2022*):

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

```bash
cmake --build build --config Release
```

```bash
build\Release\lumen.exe examples\demo-mapping.lumen
```

A primeira compilação demora (o vcpkg compila o FFmpeg); as seguintes são rápidas.
As DLLs necessárias são copiadas para `build\Release` automaticamente.

Também dá para abrir a pasta direto no Visual Studio (*Arquivo → Abrir → Pasta*),
desde que a variável `VCPKG_ROOT` aponte para o vcpkg.

## Compilar para Windows a partir do Linux (MinGW)

Útil para gerar um `.exe` sem ter Windows. Baixe o FFmpeg *win64-lgpl-shared* de
[BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases) e o GLFW
*bin.WIN64* de [glfw/releases](https://github.com/glfw/glfw/releases), descompacte, e:

```bash
sudo apt install mingw-w64
```

```bash
./scripts/build-windows-mingw.sh ~/ffmpeg-win64-lgpl-shared ~/glfw-3.4.bin.WIN64
```

O pacote fica em `build-mingw/Lumen/` (exe + DLLs + exemplos). Para só verificar se o
código compila para Windows: `./scripts/check-windows-mingw.sh`.

## Compilar no Linux (desenvolvimento)

```bash
sudo apt install build-essential cmake libglfw3-dev libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavdevice-dev libavutil-dev libasound2-dev libgl1-mesa-dev
```

```bash
cmake -S . -B build && cmake --build build -j
```

```bash
./build/lumen examples/demo-mapping.lumen
```

## Primeiros passos

1. Abra `examples/demo-mapping.lumen`: um cubo mapeado com 3 faces.
2. Pressione **Espaço** para avançar as colunas (Alinhamento → Plasma → Só o cubo → Fim).
3. Selecione a layer **Cubo**, pressione **M** (Perspectiva) e arraste os cantos no workspace.
4. Pressione **G** (Malha), aumente a malha em *Propriedades → Fatias* (ex.: 5 × 5) e curve a imagem.
5. Clique em **Saídas...** (Ctrl+Shift+P), como o painel *Output* do Millumin: defina o
   *layout* (ex.: 3 × 1 de 1920 × 1080), escolha a **tela** de cada projetor e o **soft edge**
   (px) entre eles. **Mapping** em cada cartão abre as abas Slice, Mapping (cantos/malha),
   Máscaras e Cor. **Tela cheia** abre os projetores (Ctrl+P). Use Win+P → *Estender*.
6. Em *Interação*, envie OSC para a porta 8000 (ex.: `/sensor/1 0.5` gira o matiz do cubo).
7. Ligue um controlador MIDI, crie um mapeamento, clique em **Aprender** e mexa num knob.
8. **+ Ao vivo** na biblioteca adiciona câmera, stream de rede ou Spout.

### Dicas de desempenho

- Para vários vídeos 4K ao mesmo tempo, exporte em **HAP** (ou HAP Q para mais qualidade,
  HAP Alpha com transparência). O After Effects/Media Encoder exporta HAP com o plugin
  gratuito da Vidvox, e o FFmpeg também: `ffmpeg -i in.mov -c:v hap -format hap_q out.mov`.
- H.264/HEVC usam a decodificação por hardware da placa de vídeo (*Projeto → Vídeo*).
- Projetor em monitor: use **Estender** (Win+P), nunca espelhar.

### Atalhos

| Tecla | Ação |
|---|---|
| Espaço / Shift+Espaço | GO (próxima coluna) / coluna anterior |
| Esc | Para tudo com fade |
| B | Blackout |
| V / M / G / K / L | Ferramentas: selecionar, perspectiva, malha, máscara, LED |
| Tab | Próximo canto / ponto |
| Setas (Shift = ×10) | Ajuste fino do ponto selecionado |
| F | Enquadrar a vista |
| Roda / botão do meio / Alt+arrastar | Zoom e mover a vista |
| Ctrl+Z / Ctrl+Y | Desfazer / refazer |
| Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+N / Ctrl+I | Salvar / salvar como / abrir / novo / importar |
| Ctrl+P | Abrir / fechar as janelas dos projetores |
| Ctrl+Shift+P | Janela Saídas (layout dos projetores) |
| Ctrl+Shift+D | Mostrar os cantos e a malha da saída no próprio projetor |

## Controle por OSC

Veja [docs/OSC.md](docs/OSC.md). Exemplos:

```
/lumen/go
/lumen/column/launch 2
/lumen/layer/1/opacity 0.5
/lumen/layer/Cubo/position 120 -40
/lumen/blackout 1
```

## Estrutura do código

```
src/
  app.*            janela principal, loop, arquivos, desfazer
  core/            modelo do projeto (JSON) e geometria do warp (homografia + malha)
  media/           decodificação FFmpeg (thread, hardware), HAP, conversão na GPU, áudio, ao vivo
  io/              Spout (Windows)
  engine/          motor do show (colunas, transições), compositor, janelas de saída
  control/         OSC (UDP), MIDI e mapeamentos sensor → parâmetro
  ui/              interface (Dear ImGui): biblioteca, dashboard, workspace, propriedades
  gl/ render/      carregador OpenGL 3.3 e wrappers (shader, textura, framebuffer)
tests/             testes unitários (warp, OSC, projeto)
```

Mais detalhes em [docs/ARQUITETURA.md](docs/ARQUITETURA.md). Próximos passos em
[docs/ROADMAP.md](docs/ROADMAP.md).

## Linha de comando

```
lumen [projeto.lumen] [--launch N] [--outputs | --no-outputs] [--size LxA]
      [--blend 3x1:0.15:1920x1080] [--pattern N] [--black-level V]
      [--frames N --screenshot-canvas F.ppm --screenshot-ui F.ppm --screenshot-output F.ppm]
```

As opções `--frames`/`--screenshot-*` servem para testes automáticos (o CI usa).

## Licenças de terceiros

Dear ImGui (MIT), nlohmann/json (MIT), miniaudio (MIT-0/domínio público),
RtMidi (MIT), Spout2 SDK (BSD-2), cabeçalhos Khronos (MIT/Apache-2.0),
GLFW (zlib) e FFmpeg (LGPL 2.1+, link dinâmico).
