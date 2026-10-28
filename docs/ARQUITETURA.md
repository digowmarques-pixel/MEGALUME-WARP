# Arquitetura

```
 arquivos / geradores        OSC (UDP, thread)
        │                          │
        ▼                          ▼
 ┌─────────────┐   texturas   ┌──────────┐   canvas    ┌──────────────┐
 │ media/      │ ───────────► │ engine/  │ ──────────► │ engine/      │ ──► projetores
 │ decoder     │              │ compositor│  (textura)  │ outputs      │
 │ (thread)    │              └──────────┘             └──────────────┘
 └─────────────┘                   ▲                          ▲
                                   │ project (core/)          │
                              ┌────┴──────────────────────────┴───┐
                              │ ui/ (ImGui) – edita o Project     │
                              └───────────────────────────────────┘
```

## Fluxo de um quadro (`App::run`)

1. `glfwPollEvents` e arquivos arrastados para a janela
2. `Engine::update(dt)`
   - drena as mensagens OSC e aplica comandos e mapeamentos
   - avança transições (crossfade) e relógios de reprodução
   - busca o quadro de vídeo certo para o relógio e envia para a textura
   - renderiza geradores
3. `Engine::render()` → `Compositor`: máscaras de cada layer (stencil + blur) e
   depois as layers de trás para frente no **canvas** (framebuffer)
4. Interface ImGui (mostra o canvas no workspace)
5. `OutputManager`: para cada janela de saída, recorta a região do canvas,
   aplica o warp da saída e o soft edge, e faz o swap
6. Desfazer: depois que o usuário solta o mouse, o projeto vira um snapshot JSON

## Decisões

- **OpenGL 3.3 core + GLFW**: roda em qualquer GPU com driver de 2010 para cá,
  multiplataforma, e permite várias janelas compartilhando texturas. Objetos que
  não são compartilhados entre contextos (VAO, FBO) são criados por janela.
- **Sincronização entre contextos**: `glFenceSync` no contexto principal e
  `glWaitSync` em cada saída antes de ler o canvas.
- **Vsync**: só a primeira janela de saída espera o vsync. Se todas esperassem,
  cada swap dividiria o FPS.
- **Saídas sem borda** em vez de tela cheia exclusiva: mais estável com vários
  projetores e não minimiza quando outra janela recebe foco.
- **Warp** = homografia (4 cantos) aplicada sobre uma malha Catmull-Rom em espaço
  unitário. Os pontos da malha ficam no espaço da mídia, então mover um canto
  depois de ajustar a malha preserva o ajuste fino.
- **Máscaras** em coordenadas do canvas (ficam presas à superfície física,
  mesmo quando a layer é movida por OSC).
- **Cores pré-multiplicadas** no compositor; blend modes via `glBlendFunc`.
- **Vídeo**: decodificado numa thread com fila de 6 quadros, mantendo o formato
  nativo (planos YUV, NV12, P010 ou blocos HAP). `FrameUploader` envia os planos para a
  GPU e um shader converte para RGBA (matriz BT.601/709/2020, faixa limitada/total). Com
  decodificação por hardware, o quadro vem da GPU como NV12/P010.
- **HAP**: o pacote é lido direto (`media/hap.cpp`, com Snappy próprio) e enviado como
  textura DXT1/DXT5/BC4 com `glCompressedTexImage2D`. A CPU não decodifica nada.
- **Áudio**: cada vídeo com som tem um `AudioDecoder` (thread, FFmpeg + swresample →
  float 48 kHz estéreo) que escreve num `AudioVoice` (buffer circular). O callback do
  miniaudio mistura as vozes com rampa de ganho. O número de amostras tocadas é o
  **relógio mestre** do vídeo, e o loop do áudio tem exatamente a duração do loop do
  vídeo (corta ou completa com silêncio), por isso não há deriva.
- **Ao vivo**: `LiveInput` (câmera/stream/Spout) fica no `MediaCache` e é compartilhado
  por todas as layers. É fechado quando nenhuma layer usa.
- **MIDI**: RtMidi converte as mensagens em endereços `/midi/...` que passam pelo mesmo
  sistema de mapeamentos do OSC.
- **Projeto em JSON** com caminhos relativos ao arquivo `.lumen`.

## Onde mexer para...

| Quero... | Arquivo |
|---|---|
| novo gerador | `media/source.cpp` (`kGenBodies`) + `GeneratorKind` em `core/project.h` |
| novo parâmetro controlável | `MappingTarget` (`core/project.h`) + `Engine::setParam` (`control/mapping.cpp`) |
| novo comando OSC | `Engine::handleOsc` (`control/mapping.cpp`) |
| novo tipo de entrada ao vivo | `LiveInput` (`media/source.cpp`) |
| novo formato de pixel | `Pipeline::toNative` (`media/decoder.cpp`) + shader em `media/upload.cpp` |
| novo efeito de cor | shader `kLayerFS` em `engine/compositor.cpp` + `LayerFx` |
| novo painel | `ui/ui.cpp` |
| nova ferramenta de mapping | `ui/workspace.cpp` |
