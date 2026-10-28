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

## ✅ v0.7
- **Máscaras**: curvas bezier (Alt+clique), pilha Mostrar / Esconder aplicada em ordem,
  invertida, opacidade, suavização e escala por máscara, formas prontas (quadrado, círculo,
  triângulo), **importar / exportar SVG** (Illustrator, Inkscape)
- **Warp**: **malha de entrada** (pontos sobre os detalhes da mídia), mudar a divisão sem
  perder a deformação, **duplo clique adiciona linha e coluna** de pontos no lugar clicado
- **Vários canvases** com layers próprias, saída escolhe o canvas, **canvas como mídia**
  (video routing), **blend automático** para projetores fora de grade, **Spout por saída**
  com ou sem janela, LED mapping por canvas

## ✅ v0.8
- **Efeitos ISF** em pilha por layer (20 shaders incluídos, 324 de 327 da coleção oficial
  funcionando, recarga ao vivo), **geradores ISF** na biblioteca, parâmetros por OSC
- **Matte** alpha / luma (e invertidos): uma layer recorta a de baixo
- **Rastreamento de blobs** na câmera, com fundo gravado, prévia e atalhos de mapeamento
- **Design novo**: preto e amarelo, painéis arredondados

## ✅ v0.9
- **Timeline** com keyframes e curvas (opacidade, posição, escala, rotação, cor, volume,
  parâmetros de efeitos, masters), **marcadores** que disparam comandos, loop com
  introdução, GO no fim, ligada a colunas ou por OSC
- **Transições** em shader ISF (6 incluídas), **stinger** e **fader de transição manual**
- **Comandos de equipamentos** por coluna, marcador, agenda e OSC: PJLink (com senha),
  MIDI, DMX (Art-Net/sACN), TCP, UDP, serial, OSC, espera
- **Timecode**: recebe MTC e LTC (roda livre), colunas e timelines no timecode, gera MTC e LTC
- **NDI** (receber; enviar por saída e o canvas), **texto dinâmico** (OSC / arquivo),
  **Arduino pela serial**
- **Acabamento**: salvamento automático com recuperação, mídias ausentes (busca automática
  e por pasta), **modo show**, **instalador** (NSIS), interface em **inglês**, ícone,
  **diagnóstico do sistema** (`--diagnose`) e roteiro de teste ([TESTE-WINDOWS.md](TESTE-WINDOWS.md))

## ⚠️ Pendente de teste em Windows real
Tudo foi compilado para Windows, mas **ainda não rodou numa máquina Windows**:
Spout, NDI, WASAPI, DirectShow, D3D11VA, WinMM, saída MIDI, portas COM e o instalador
precisam de um teste de verdade. Siga [TESTE-WINDOWS.md](TESTE-WINDOWS.md).
MTC/LTC e PJLink foram testados com simuladores, não com equipamentos reais.

## 🔜 Próximas tarefas (por prioridade)

1. **Teste no Windows** com o roteiro, corrigindo o que aparecer
2. Detecção de pessoas por IA (esqueleto) e câmeras de profundidade (Kinect, Orbbec)
3. LUT (.cube) por layer e por saída; transição por layer (fade in/out)
4. Timeline como mídia dentro de uma célula; Auto States
5. Estrutura 3D (anamorfose): importar OBJ, câmera do público, calibração por 6 pontos (DLT)
6. Scripts (JavaScript/QuickJS), gamepad, MSC e ATEM nos comandos
7. Áudio multicanal por layer, ASIO; upload assíncrono (PBO) para 8K
8. Triângulo e cantos soldados nas surfaces; mídias navegador, PDF e captura de tela
