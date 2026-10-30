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

## ✅ v0.10.22
- **Janela própria do Timecode** (*Janela → Timecode (MTC / LTC)*, em seção separada no menu):
  relógio grande com fps e atraso, Receber (MTC/LTC, roda livre, atraso em quadros) e Gerar.
  Avisa (com botão) quando o MTC está escolhido e o MIDI desligado. Interação e a coluna têm
  um atalho para ela

## ✅ v0.10.21
- **Atraso (quadros)** do timecode recebido (*Interação → Timecode*): acerta o vídeo com o
  som do Reaper/mesa; positivo atrasa o vídeo, negativo adianta (até ±100 quadros)
- Vídeo que segue o timecode fica no quadro: diferenças pequenas (até 0,5 s) são corrigidas
  acelerando ou freando até 10%, sem pular; acima disso, pula

## ✅ v0.10.20
- **Vídeo segue o timecode** (por coluna, ligado por padrão): pausa quando o Reaper para,
  pula quando ele pula (para frente ou para trás); dar play no meio entra na coluna da última
  cue antes daquele ponto, com o vídeo na posição certa
- Opção de teste `--test-timecode` (timecode simulado) e teste de integração da perseguição

## ✅ v0.10.19
- **Coluna no timecode dispara no início**: com o horário em 00:00:00:00 (ou em qualquer ponto
  de onde se dá play no Reaper/mesa), a coluna dispara. Antes o primeiro quadro de MTC já chegava
  ~2 quadros adiante e o horário nunca era "atravessado". Voltar e dar play de novo dispara de novo
- MTC "full frame" (enviado ao posicionar com o transporte parado) só move a posição: não faz o
  relógio andar sozinho nem dispara colunas

## ✅ v0.10.18
- Leitura mais fácil: fonte maior (17 px), texto mais claro, **botões em relevo** (cinza claro
  com contorno), **campos escuros** com contorno, títulos de seção em amarelo claro, Parar e ■
  como botões vermelhos, Remover coluna em vermelho; Mover para a esquerda/direita viraram botões
- **Remover várias colunas de uma vez**: *Lista → Remover colunas...* (ou "Remover várias..." no
  menu da coluna): marque as colunas, com atalhos *Todas*, *Só as vazias* e *Nenhuma*

## ✅ v0.10.17
- **ASIO®** (Windows): saída direta pelos drivers ASIO, todas as saídas da interface, botão
  para o painel do driver, reabre sozinho quando o buffer muda e volta ao WASAPI se o driver
  falhar. Entra só nas versões geradas com o SDK da Steinberg (`ASIO_SDK=` / `LUMEN_ASIO_SDK=`);
  testado com o driver de exemplo do SDK (16 saídas, 48 kHz, parar e reabrir)
- Logo "ASIO compatible" na configuração de áudio e nova janela *Ajuda → Sobre o Lumen*
- `docs/ASIO-LICENCA.md`: o que fazer com o contrato da Steinberg antes de distribuir

## ✅ v0.10.16
- Ferramenta **Escala** (tecla E): caixa amarela com 8 alças em volta da fatia; os cantos mudam
  o tamanho mantendo a proporção (Shift: livre), os lados esticam, o meio move
- **Ímã** (botão na barra): ao mover ou mudar o tamanho, a fatia encaixa nas bordas e no centro
  do canvas, das saídas e das outras fatias, com uma linha-guia amarela. Ctrl inverte na hora
- Workspace abre **enquadrado** e continua enquadrado enquanto os painéis mudam de tamanho
  (até você dar zoom ou mover a vista)
- Fundo do programa **mais claro** (cinza médio-escuro em vez de preto)

## ✅ v0.10.15
- Ordem das layers: **arraste o nome da layer** no dashboard para cima ou para baixo (uma
  linha amarela mostra onde ela entra). A de cima fica na frente; as células vão junto

## ✅ v0.10.14
- Painéis estreitos não sobrepõem mais nada: os botões da Biblioteca, do Dashboard e do
  Workspace descem para a linha de baixo, as dicas são cortadas com "..." (texto inteiro ao
  passar o mouse) e a barra de cima vira duas linhas em telas pequenas (1366 px)
- Mídia com proporção diferente do canvas (ex.: vídeo em pé 1080×1920) **não estica mais**:
  é encaixada inteira com sobras transparentes. Opção por layer em Propriedades → Layer →
  Proporção: Encaixar (padrão), Preencher (corta as bordas) ou Esticar
- Relógio do painel Tempo e barra de tempo ficam **vermelhos nos últimos 10 s**, também com
  o vídeo em loop

## ✅ v0.10.13
- Nova paleta: fundo preto de verdade, painéis cinza bem escuro, menos bordas; o **amarelo**
  continua sendo a marca e o que você aciona. Uma cor por tipo de coisa, só em detalhes finos
  (faixa no título do painel, barra na seção, contornos): **verde** = o que está tocando
  (célula no ar, relógio, barra de tempo, medidores), **ciano** = mapping e saídas,
  **magenta** = máscaras, **roxo** = efeitos e timeline, **laranja** = avisos,
  **vermelho** = parar e blackout

## ✅ v0.10.12
- Arrastar arquivos do Explorer **direto para uma célula** do dashboard: a mídia é
  importada e posta nela; vários arquivos vão para as colunas seguintes (criadas se faltar)

## ✅ v0.10.11
- **Painéis móveis**: arraste qualquer painel (Biblioteca, Workspace, Dashboard, Timeline,
  Propriedades) pela aba para cima, para baixo ou para os lados; o layout fica salvo
  (*Janela → Restaurar layout dos painéis* volta ao original)
- **Vários dashboards** (boards do Millumin): seletor no Dashboard com +, − e nome; cada
  coluna pertence a um; GO / auto-GO andam dentro do dashboard da coluna no ar
- Linhas da malha (warping) desenhadas nas saídas e nas layers
- Projeto novo começa só com o Test card na biblioteca

## ✅ v0.10.10
- **Som com velocidade ≠ 1**: o áudio acompanha de 0,25x a 4x (como fita: o tom muda) e
  continua comandando a imagem; antes ficava mudo
- Saídas → Slice: clicar na área de outro projetor seleciona ele (os nomes aparecem nas áreas)

## ✅ v0.10.9
- **Monitores virtuais**: saídas sem tela aparecem juntas numa janela do Lumen em vez de
  abrir uma janela cada (*Saídas → Saídas sem tela em monitores virtuais*)
- Máscaras: botão **Adicionar pontos** — desligado, clicar não cria ponto por acidente
- Velocidade da célula mudada com o vídeo tocando vale na hora

## ✅ v0.10.8
- Editor de fatias, forma livre: mover um canto corta a imagem nessa forma também no
  canvas (sem esticar), como no Millumin; a malha de entrada de 4 cantos é em perspectiva
- Janelas só se movem pela barra de título (clicar perto de uma alça não arrasta a janela)

## ✅ v0.10.7
- As seções das propriedades da layer começam fechadas

## ✅ v0.10.6
- **Máscaras** como no Millumin: lista com mostrar / esconder em cada uma e os botões
  + (quadrada, redonda, triangular, vazia), ••• (ordem, duplicar, SVG) e −
- **Editor de fatias** (Mapping → *Editor de fatias...*): a mídia com a parte de cada
  fatia; arraste cantos, lados ou o meio; forma retângulo ou livre (4 cantos); + Fatia

## ✅ v0.10.5
- Propriedades só com Layer, Célula e Mídia; **Saídas, Interação, Projeto e LED viram
  janelas** (menu *Janela*)
- Painel **Tempo** mais largo (arraste a divisa) com **linha do tempo**: clique ou arraste
  para levar o vídeo para frente ou para trás
- Corrigido: mudar o *No fim* (ex.: Congelar) com o vídeo tocando agora vale na hora

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
