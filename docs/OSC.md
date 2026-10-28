# Controle por OSC

O Lumen escuta mensagens **OSC 1.0 via UDP** na porta configurada em
*Propriedades → Interação* (padrão **8000**). Mensagens simples e *bundles* são
aceitos. Tipos de argumento: `i`, `f`, `d`, `h`, `s`, `T`, `F`.

Funciona com TouchOSC, Open Stage Control, TouchDesigner, Max/MSP, Pure Data,
QLab, Bitfocus Companion, Arduino/ESP32 (biblioteca OSC) e qualquer script.

## Comandos

Layers são endereçadas pela **posição a partir do topo (1, 2, 3...)** ou pelo **nome**
(`/lumen/layer/Cubo/...`). Colunas são numeradas a partir de 1.

| Endereço | Argumentos | Efeito |
|---|---|---|
| `/lumen/go` | `[1]` | Dispara a próxima coluna (com argumento, só se > 0.5) |
| `/lumen/back` | `[1]` | Coluna anterior |
| `/lumen/stop` | `[fade]` | Para tudo (fade em segundos, opcional) |
| `/lumen/blackout` | `0/1` | Liga/desliga o blackout |
| `/lumen/master` | `0..1` | Opacidade master |
| `/lumen/volume` | `0..2` | Volume master do áudio |
| `/lumen/dmx/master` | `0..1` | Intensidade de todos os LEDs |
| `/lumen/led/<n>/enabled` | `0/1` | Liga/desliga uma fita de LED (`<n>` = posição ou nome) |
| `/lumen/led/<n>/brightness` | `0..1` | Brilho de uma fita de LED |
| `/lumen/column/launch` | `n` | Dispara a coluna `n` |
| `/lumen/column/<n>/launch` | `[1]` | Dispara a coluna `n` (bom para botões) |
| `/lumen/layer/<l>/opacity` | `0..1` | Opacidade |
| `/lumen/layer/<l>/visible` | `0/1` | Visibilidade |
| `/lumen/layer/<l>/position` | `x y` | Deslocamento em pixels do canvas |
| `/lumen/layer/<l>/x`, `/y` | `px` | Deslocamento em um eixo |
| `/lumen/layer/<l>/scale` | `s` | Escala (1 = original) |
| `/lumen/layer/<l>/rotation` | `graus` | Rotação |
| `/lumen/layer/<l>/brightness` | `-1..1` | Brilho |
| `/lumen/layer/<l>/contrast` | `0..2` | Contraste |
| `/lumen/layer/<l>/saturation` | `0..2` | Saturação |
| `/lumen/layer/<l>/hue` | `graus` | Matiz |
| `/lumen/layer/<l>/tint` | `r g b` | Tinta (0..1) |
| `/lumen/layer/<l>/speed` | `x` | Velocidade do vídeo em reprodução (≠ 1 desliga o som) |
| `/lumen/layer/<l>/volume` | `0..2` | Volume da layer |
| `/lumen/layer/<l>/mute` | `0/1` | Mudo |
| `/lumen/layer/<l>/launch` | `n` | Dispara a célula da coluna `n` nesta layer |
| `/lumen/layer/<l>/stop` | `[fade]` | Para a layer |
| `/lumen/layer/<l>/restart` | | Reinicia a mídia da layer |

## MIDI

Controladores MIDI conectados são abertos automaticamente (e reconectados se forem
plugados depois). As mensagens entram como endereços OSC e podem ser usadas nos
mapeamentos e no **Aprender**:

| MIDI | Endereço | Valor |
|---|---|---|
| Control Change | `/midi/cc/<canal>/<número>` | 0..127 |
| Note On / Off | `/midi/note/<canal>/<nota>` | velocidade (off = 0) |
| Program Change | `/midi/pc/<canal>` | programa |
| Pitch Bend | `/midi/bend/<canal>` | 0..16383 |

Exemplo: o fader 1 de um controlador costuma ser `/midi/cc/1/7`; com um mapeamento
*Opacidade* (entrada 0–127, saída 0–1) ele vira o fader de uma layer.

## Saída OSC

Em *Interação → Saída OSC* adicione destinos (IP e porta). Eles recebem:

- `/lumen/status/column <n> "<nome>"` a cada cue (0 quando tudo para). Útil para
  mostrar o cue atual no TouchOSC ou no Companion (Stream Deck).
- As **ações OSC da coluna**: botão direito no cabeçalho da coluna, uma mensagem por
  linha, ex.: `/luz/cena 3` ou `/som/play "abertura" 0.8`.

## Mapeamentos (sensor → parâmetro)

Para sensores que enviam endereços próprios (ex.: `/sensor/distancia 512`), crie
um **mapeamento** em *Interação*:

- **Endereço**: aceita curingas OSC (`/sensor/*`, `/pad/[1-4]`, `/{a,b}/x`)
- **Argumento #**: qual argumento usar (0 = primeiro)
- **Entrada min/max**: faixa do sensor (ex.: 0–1023 para um Arduino)
- **Alvo**: opacidade, posição X/Y, escala, rotação, brilho, contraste, saturação,
  matiz, velocidade ou volume de uma layer; opacidade/volume master; ou um gatilho
  (**Disparar coluna**, **GO**, **Parar tudo**, **Blackout**), que dispara quando o
  valor cruza a metade da faixa
- **Saída min/max**: faixa do parâmetro
- **Suavização**: 0 = imediato; perto de 1 = movimento lento e suave (filtra ruído)

O botão **Aprender** preenche o endereço com a próxima mensagem recebida.

## Testar rapidamente

Python, sem bibliotecas extras:

```python
import socket, struct

def osc(addr, *args):
    pad = lambda b: b + b"\0" * (4 - len(b) % 4)
    tags = "," + "".join("f" if isinstance(a, float) else "i" for a in args)
    data = b"".join(struct.pack(">f" if isinstance(a, float) else ">i", a) for a in args)
    return pad(addr.encode()) + pad(tags.encode()) + data

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(osc("/lumen/go"), ("127.0.0.1", 8000))
s.sendto(osc("/lumen/layer/1/opacity", 0.5), ("127.0.0.1", 8000))
```

Arduino/ESP32 com a biblioteca *CNMAT OSC*:

```cpp
OSCMessage msg("/sensor/1");
msg.add((float)analogRead(A0) / 1023.0);
udp.beginPacket(ipDoComputador, 8000);
msg.send(udp);
udp.endPacket();
```

No Windows, libere a porta UDP no firewall na primeira execução (o Windows pergunta).
