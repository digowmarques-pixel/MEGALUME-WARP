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

## ⚠️ Pendente de teste em Windows real
Tudo foi compilado para Windows, mas **ainda não rodou numa máquina Windows**:
Spout, WASAPI, DirectShow, D3D11VA e WinMM precisam de um teste de verdade.

## 🔜 Próximas tarefas (por prioridade)

1. **Teste no Windows** com projetor, placa de vídeo dedicada, câmera, Spout e controlador MIDI
2. **NDI** (vídeo pela rede): receber e enviar
3. **Timeline** com keyframes (opacidade, posição, efeitos) e cues com tempo
4. **Efeitos em shader** carregados de arquivo (GLSL estilo ISF/Shadertoy) com parâmetros
5. **DMX de entrada** (ser controlado por mesa de luz via Art-Net/sACN), USB DMX (Enttec),
   plano de luz com fixtures (moving heads, PAR) e estados por coluna
6. Áudio: roteamento multicanal por layer, ASIO, áudio das câmeras/streams,
   time-stretch para velocidade ≠ 1
7. Upload assíncrono de textura (PBO) e decodificação D3D11 sem cópia para 8K
8. **Kinect / câmera de profundidade / MediaPipe** para detecção de pessoas
9. **Scripts** (Lua ou JavaScript) para lógica de instalações
10. Autosave, modo "show" travado, monitor de desempenho, relink de mídias ausentes
11. Instalador (.msi) e auto-atualização
