# Roadmap

Ordem pensada para **videomapping** e **instalações interativas**.

## ✅ v0.1
- Motor OpenGL, janelas de saída, vídeo/imagem/geradores, projeto JSON, desfazer
- Dashboard colunas × layers, GO, crossfade
- Mapping: fatias, perspectiva, malha, máscaras com feather
- Saídas: região do canvas, warp por saída, soft edge, test card
- OSC de entrada e mapeamentos com "Aprender"

## ✅ v0.2
- **Áudio**: som dos vídeos, volume/mudo por layer, fade junto com a imagem,
  escolha da placa de som, relógio do áudio sincronizando o vídeo
- **Desempenho**: conversão YUV → RGB na GPU (8–16 bits), decodificação por
  hardware (D3D11VA/DXVA2), HAP / HAP Alpha / HAP Q / HAP Q Alpha direto na GPU
- **Ao vivo**: câmeras e placas de captura (DirectShow), RTSP/SRT/UDP/HTTP,
  receptor **Spout**, entrada compartilhada entre layers e reconexão automática
- **MIDI** de entrada com "Aprender" (via mapeamentos)
- **OSC de saída**: status do cue e ações OSC por coluna
- **Spout de saída** do canvas
- Build para Windows a partir do Linux (MinGW) e verificação no CI

## ✅ v0.3
- **LED mapping** por Art-Net e sACN, com monitor DMX e DMX master

## ✅ v0.4
- **Blend multi-projetor**: assistente de layout, compensação de nível de preto, cor por
  projetor, grade de blend contínua, branco/preto de teste, máscaras por saída,
  identificar saídas, sincronia por DwmFlush no Windows. Validado numericamente
- Saídas abrem só quando solicitado; perspectiva côncava sem distorção

## ✅ v0.5
- Janela **Saídas** no estilo do Millumin (layout, telas, soft edge em px por emenda,
  Tela cheia / Test Card / Grade / Identificar) e página de mapping por saída
  (Slice, Mapping, Máscaras, Cor e blend)
- Coluna com título, notas e tipo de transição (crossfade, corte, fade pelo preto)
- Dashboard: colunas 01, 02… com ▶, linha de transição, coluna de parar e zoom

## ✅ v0.6 (estudo da documentação do Millumin, veja [MILLUMIN.md](MILLUMIN.md))
- **Fim da mídia** por célula: loop, congelar, parar, **disparar a próxima coluna** (auto-follow)
- **Auto-GO** por coluna (espera, contar depois da transição, coluna de destino) com contagem
  regressiva no dashboard; na última coluna volta para a primeira
- **Estados por coluna**: parâmetros de layer gravados na coluna e animados durante a
  transição, com herança das colunas anteriores (◆ na célula)
- **Surfaces**: mapping compartilhado por várias layers
- **Entradas**: teclado (`/key/...`), **DMX de entrada** (Art-Net/sACN), calibrar,
  transformações (linear, chave, alternar, acumular), mapeamento ativo só em algumas colunas
- **Agenda** por horário e dia da semana; projeto que abre as saídas e dispara uma coluna
- **API OSC do Millumin** (`/millumin/action/...`, `/layer:Nome/...`, `/selectedLayer`,
  `/+` `/-`), feedback `launchedColumn`, OSC em texto puro
- Saídas: **atraso em quadros** por projetor e **controles desenhados no projetor**
  (Ctrl+Shift+D); cor por coluna

## ⚠️ Pendente de teste em Windows real
Tudo foi compilado para Windows, mas **ainda não rodou numa máquina Windows**:
Spout, WASAPI, DirectShow, D3D11VA e WinMM precisam de um teste de verdade.

## 🔜 Próximas tarefas (por prioridade)

1. **Teste no Windows** com projetor, placa de vídeo dedicada, câmera, Spout e controlador MIDI
2. **Timeline** com keyframes, curvas, segmentos (loop / intro) e flags que disparam ações;
   timeline como mídia dentro de uma célula
3. **Efeitos em pilha** por layer, incluindo **shaders ISF** (efeitos, geradores e transições),
   LUT, níveis, chroma key, espelho, blur, soft crop
4. **Blob tracking** na câmera (posição das pessoas → parâmetros) e modos de **matte**
   (alpha / luma) para usar uma layer como máscara da de baixo
5. **NDI** (receber e enviar), **texto dinâmico** (OSC `/layer/media/text`, arquivo .txt)
6. **Video routing**: vários canvases, saídas sem tela (NDI/Spout), blend entre fatias livres
7. **Máscaras** com pontos bezier, modo mostrar/esconder em pilha, opacidade, SVG;
   **warp com pontos de entrada** (malha do lado da mídia) e malha bezier
8. Data-tracks: comandos MIDI / DMX / PJLink (ligar projetor, obturador) / string TCP por coluna
9. Timecode LTC/MTC (entrada e geração), cues de ensaio, transição manual por fader
10. Estrutura 3D (anamorfose): importar OBJ, câmera do público, calibração por 6 pontos (DLT)
11. Scripts (JavaScript/QuickJS), Arduino por serial, gamepad
12. Áudio multicanal por layer, ASIO; upload assíncrono (PBO) para 8K; instalador
