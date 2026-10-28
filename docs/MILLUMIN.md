# Estudo do Millumin → Lumen

Toda a central de ajuda do Millumin (help.millumin.com) foi lida, junto com a
documentação OSC do kit de desenvolvimento. Esta página resume o que o Millumin faz,
o que o Lumen já tem e o que vem depois.

Legenda: ✅ o Lumen tem · 🟡 o Lumen tem parte · ⬜ ainda não tem.

## Reprodução (dashboard, layers, mídia)

| Millumin | Lumen |
|---|---|
| Dashboard de colunas × layers, GO no espaço, título, notas e cor da coluna | ✅ |
| Transição por coluna (crossfade, corte, pelo preto, ISF, stinger, manual) | ✅ v0.9 |
| Modos de loop: infinito, uma vez e congelar, uma vez e parar, auto-follow | ✅ v0.6 |
| Auto-go (espera, depois da transição, próxima / coluna específica) | ✅ v0.6 |
| **States**: valores de propriedades por coluna, interpolados na transição, herança ➔ | ✅ v0.6 (layer: opacidade, posição, escala, rotação, cor, volume) |
| Auto States (gravar automaticamente ao editar) | ⬜ |
| Media layer independente, copy layer, grupos | 🟡 (fatias cobrem parte do copy layer) |
| Transição por layer (crossfade, fade in/out) | ⬜ |
| Timeline com keyframes, curvas, segmentos, intro, assíncrona, flags | ✅ v0.9 (intro + loop, flags com comandos; timeline como mídia ⬜) |
| Media Editor: corte (crop), in/out, efeitos por mídia | 🟡 (recorte UV por fatia) |
| Efeitos em pilha: RGBA, HSLA, níveis, LUT, limiar, espelho, borda, chroma key, soft crop, blur, ISF | ✅ v0.8 (ISF; LUT ⬜) |
| Mattes: alpha, alpha invertido, luma, luma invertido | ✅ v0.8 |
| Máscaras: mostrar/esconder em pilha, invertida, opacidade, feather, escala, bezier, SVG | ✅ v0.7 |
| Mídias: texto dinâmico, navegador, captura de tela, slideshow, PDF, color card com 10 cores | 🟡 (texto dinâmico v0.9, geradores, câmera, rede, Spout, NDI) |
| Áudio: volume por mídia, roteamento por layer e matriz de canais | ✅ v0.10 (multicanal, par ou matriz por layer, 5.1/7.1) |

## Saídas e mapping

| Millumin | Lumen |
|---|---|
| Layout H × V de W × H, soft edge em px por emenda, botão Fit, test card | ✅ |
| Mapping do canvas por saída (slice, perspectiva, warping, máscaras) | ✅ |
| Cor por saída (RGB, gamma), **atraso em quadros** | ✅ (atraso v0.6) |
| Modo "só alpha" (para keyer) | ⬜ |
| **Surfaces**: mapping compartilhado entre layers | ✅ v0.6 (triângulo, cantos soldados e SVG ⬜) |
| Mostrar controles na saída (⌘⇧D) | ✅ v0.6 (Ctrl+Shift+D) |
| Warp com pontos de entrada (lado da mídia) e pontos por duplo clique | ✅ v0.7 |
| `division` muda a grade mantendo o warp | ✅ v0.7 |
| Vários canvases, canvas como mídia (vários → 1 saída) | ✅ v0.7 |
| 1 canvas → várias saídas com blend livre (fora de grade) | ✅ v0.7 (blend automático) |
| Saída sem tela (NDI / Syphon) | ✅ (Spout v0.7, NDI v0.9) |
| Structure: anamorfose 3D (OBJ, câmera do público, 6 pontos e auto-place) | ⬜ |
| Syphon / NDI / Blackmagic / AJA / RTMP | 🟡 (Spout no lugar de Syphon, NDI v0.9) |
| Compensação de nível de preto | ✅ (o Millumin não tem) |

## Luz

| Millumin | Lumen |
|---|---|
| LED mapping (camada DMX): RGB/BGR/mono, 8/16 bits, zigzag, flip, curva | ✅ |
| Editor de fixture próprio, curva por canal | ⬜ |
| Plano de luz, fixtures da Open Fixture Library, states de luz, HTP | ⬜ |
| Laser (ILDA, EtherDream, Helios...) | ⬜ |
| USB DMX (Enttec) | ⬜ |

## Interação

| Millumin | Lumen |
|---|---|
| Interações: entrada → transformação → ação, com "learn" | ✅ |
| Entradas: teclado, MIDI, OSC, DMX, Art-Net, sACN | ✅ v0.6 |
| Entradas: timecode, gamepad, Kinect, Leap Motion, áudio, blob tracker, scheduler | 🟡 (agenda v0.6, blob tracker v0.8, timecode e serial v0.9) |
| Transformações: nenhuma, chave, alternar, linear, acumular; calibrar; estabilizar | ✅ v0.6 |
| Grupos de interações limitados a colunas | ✅ v0.6 (por mapeamento) |
| Data-tracks: MIDI, OSC, DMX, string, MSC, PJLink, ATEM por coluna | ✅ v0.9 (MIDI, OSC, DMX, TCP/UDP, serial, PJLink; MSC e ATEM ⬜) |
| API OSC completa, feedback, consulta `/?` | ✅ v0.6 (a consulta `/?` ⬜) |
| Scripts JavaScript | ⬜ |
| Timecode LTC/MTC/NTC (entrada e geração) | ✅ v0.9 (LTC e MTC; NTC ⬜) |
| Máquina reserva (ColumnSync) | ✅ v0.10 (segue a principal, corrige o tempo dos vídeos e assume sozinha) |
| Arduino (auto-programação) | 🟡 (serial como entrada v0.9; sem auto-programação) |

## Atalhos do Millumin e equivalentes

| Millumin (Mac) | Lumen (Windows) |
|---|---|
| Espaço / ⌥Espaço | Espaço / Shift+Espaço |
| ⌘; (Output) | Ctrl+Shift+P (Saídas) |
| ⌘⇧D (controles na saída) | Ctrl+Shift+D |
| ⌘F (tela cheia) | Ctrl+P (abrir / fechar saídas) |
| M (mapping), V (mover) | M, V (e G malha, K máscara, L LED) |
| TAB (próximo canto), setas, ⇧ | Tab, setas, Shift ×10 |
| ⌘Z / ⌘⇧Z | Ctrl+Z / Ctrl+Y |
| Modo show (lock) | Ctrl+Shift+L |
