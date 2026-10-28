# Roadmap

Ordem pensada para **videomapping** e **instalações interativas**.

## ✅ Etapa 1 — Motor básico
- Janela de controle + janelas de saída (monitor sem bordas ou janela)
- Vídeo via FFmpeg em thread, imagens, geradores em shader
- Projeto em JSON, desfazer/refazer

## ✅ Etapa 2 — Layers e dashboard
- Grade colunas × layers, GO, crossfade, disparo por célula, arrastar e soltar

## ✅ Etapa 3 — Mapping
- Fatias com recorte, perspectiva de 4 cantos, malha (mesh warp), máscaras com feather

## ✅ Etapa 4 (base) — Várias saídas
- Região do canvas por saída, warp por saída, soft edge, test card

## ✅ Etapa 6 (base) — Interação
- OSC de entrada, mapeamentos sensor → parâmetro com "Aprender" e suavização

## 🔜 Próximas tarefas (por prioridade)

1. **Áudio dos vídeos** (miniaudio, WASAPI/ASIO, volume por layer, saída multicanal)
2. **HAP nativo na GPU** (DXT/BC direto para a textura) e decodificação por
   hardware (D3D11VA/NVDEC), para vários vídeos 4K simultâneos
3. **Spout** (receber/enviar textura de/para TouchDesigner, Resolume, Unreal, Notch)
   e **NDI** (vídeo pela rede)
4. **Entrada de câmera / placa de captura** (Media Foundation) como mídia
5. **MIDI** de entrada (controladores) com "MIDI learn" usando o mesmo sistema de mapeamentos
6. **Timeline** com keyframes (opacidade, posição, efeitos) e cues com tempo
7. **OSC de saída** (feedback para TouchOSC; disparar luz/som em outros sistemas)
8. **DMX / Art-Net / sACN** de entrada (mesas de luz) e de saída (pixel mapping de LED)
9. **Efeitos em shader** carregados de arquivo (GLSL estilo ISF/Shadertoy) com parâmetros
10. **Kinect / câmera de profundidade / MediaPipe** para detecção de pessoas
11. **Scripts** (Lua ou JavaScript) para lógica de instalações
12. Autosave, modo "show" travado, monitor de desempenho, relink de mídias ausentes
13. Instalador (.msi) e auto-atualização

## Ideias de arquitetura para depois
- Trocar OpenGL por Direct3D 11 no Windows se for preciso zero-cópia com
  Spout/NDI/captura (o OpenGL atende bem via `WGL_NV_DX_interop`).
- Separar o renderizador num processo "nó" (como o MilluNode) para controlar
  vários computadores pela rede.
