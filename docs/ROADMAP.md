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

## ✅ v0.10 (desempenho para shows grandes)
- **Envio assíncrono à GPU** (anel de PBOs com fences, cópia em várias threads, HAP
  atualizado no lugar): mesma imagem bit a bit, sem travar o render; **leitura
  assíncrona** para o NDI; medição no diagnóstico e na barra de status
- **Áudio multicanal**: todos os canais da interface, par de saídas ou matriz por layer,
  arquivos 5.1/7.1 nativos ou mixados, modo exclusivo WASAPI, buffer ajustável, medidores
  por canal, LTC em qualquer saída ([AUDIO.md](AUDIO.md))
- **Máquina reserva**: segue a principal pela rede, corrige o tempo dos vídeos com *seek*
  preciso e assume sozinha rodando comandos de troca ([REDUNDANCIA.md](REDUNDANCIA.md))
- *Seek* preciso de vídeo e áudio (também por OSC: `/lumen/layer/<l>/time`)
- Testes de integração no CI (`scripts/test-integration.sh`)

## ✅ v0.10.4
- Saídas → Slice: **warping** da fatia (os pontos andam sobre a imagem do canvas), como
  no Millumin
- LED mapping escondido por padrão (*Janela → LED mapping* mostra a ferramenta, a aba e
  as fitas)

## ✅ v0.10.3 (mais parecido com o Millumin)
- **Propriedades da layer em seções**: Layer, Mapping, Efeitos, Máscaras e Mídia atual
- **Mapping**: liga/desliga a deformação (*warping*) com a divisão da malha e uma prévia
  de onde a fatia cai no canvas
- **Menu de efeitos** em seções: Cor, Padrão, Especial e Vários (por categoria, com os
  efeitos baixados)
- **Saídas**: soft edge também nas bordas externas (escurece até o preto, para emendar
  com o projetor de outro computador)
- **Menus** como os do Millumin: Lista (layers: subir, descer, solo), Quadro (colunas:
  anterior, próxima, duplicar, remover vazias, nova timeline), Saídas (padrão de teste em
  todas, controles na saída, identificar) e Janela (painéis)

## ✅ v0.10.2
- Clique na célula **só seleciona** (para editar); vai ao ar pelo ▶ da coluna, GO ou
  botão direito → *Disparar só esta célula*
- Painel **Tempo** ao lado do dashboard: mídia no ar em cada layer, tempo restante ou
  decorrido ou sem contador (clique no relógio troca), barra de progresso e **pausa** (imagem e som param e voltam
  juntos, sem estalo; também por OSC e na máquina reserva)
- **Esc** nunca para o show: só fecha menus
- Corrigido: a aba *Projeto* fechava o programa (campo de resolução)

## ✅ v0.10.1 (revisão geral)
Revisão de tudo o que foi feito da v0.1 à v0.10 (código lido por área, sanitizers,
análise estática, testes de cenário e de interface). Corrigidos, entre outros:
- **Travamentos**: comando `tcp` para porta fechada (Linux/macOS), arquivo que não é
  fonte escolhido como fonte, cabeçalho ISF inválido, tabela de cues com muitas colunas
- **Áudio**: deriva do loop, som atrasado em relação ao vídeo, interface de som que
  para (o vídeo congelava), velocidade ≠ 1 que deixava a layer muda, LTC cortado pelo
  master, estalo nos cortes
- **Vídeo**: fechar um stream travado, HAP Alpha-Only, *seek* em .ts, *seek* além do fim,
  fonte lavfi rápida demais
- **Imagem**: malha do warp nas bordas, primeiro quadro com efeito, imagens IMPORTED do
  ISF, efeitos com alpha, master dobrado com matte, queda de brilho no crossfade, blend
  com projetores desalinhados, recorte espelhado, buffers FLOAT
- **Equipamentos**: PJLink no Windows, OSC com tipos raros, DMX por comando somado aos
  LEDs, DNS lento, serial que gastava 100% de CPU depois de desconectar, Arduino
  reiniciando a cada comando
- **Instalador**: não apaga mais os efeitos do usuário, lembra a pasta, inclui o runtime
  do Visual C++ e o `lumen-console.exe`

## ⚠️ Pendente de teste em Windows real
Tudo foi compilado para Windows, mas **ainda não rodou numa máquina Windows**:
Spout, NDI, WASAPI (multicanal e exclusivo), DirectShow, D3D11VA, PBO, WinMM, saída MIDI,
portas COM, a máquina reserva entre dois PCs e o instalador
precisam de um teste de verdade. Siga [TESTE-WINDOWS.md](TESTE-WINDOWS.md).
MTC/LTC e PJLink foram testados com simuladores, não com equipamentos reais.

## 🔜 Próximas tarefas (por prioridade)

1. **Teste no Windows** com o roteiro, corrigindo o que aparecer
2. ASIO (depende da decisão de licença: GPLv3 ou acordo com a Steinberg)
3. Detecção de pessoas por IA (esqueleto) e câmeras de profundidade (Kinect, Orbbec)
4. LUT (.cube) por layer e por saída; transição por layer (fade in/out)
5. Timeline como mídia dentro de uma célula; Auto States
6. Estrutura 3D (anamorfose): importar OBJ, câmera do público, calibração por 6 pontos (DLT)
7. Scripts (JavaScript/QuickJS), gamepad, MSC e ATEM nos comandos
8. Triângulo e cantos soldados nas surfaces; mídias navegador, PDF e captura de tela
