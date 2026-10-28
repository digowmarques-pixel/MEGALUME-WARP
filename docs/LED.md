# LED mapping (pixel mapping por DMX)

O Lumen lê os pixels do canvas e os converte em DMX. Assim, a fita ou matriz de LED
"toca" o mesmo conteúdo do vídeo, seguindo as colunas, os fades e a interação.

## Hardware

```
PC (Lumen) → rede → controlador Art-Net / sACN → fita de LED
```

O Lumen não liga direto na fita. Use um controlador de pixels pela rede, por exemplo
**DMXking LeDMX4 Pro** (WS2811/WS2812/SK6812), **Enttec ODE / Octo**, **Advatek PixLite**
ou **VisualProductions QuadCore**. Para fitas DMX "de verdade" (com decoder DMX), qualquer
nó Art-Net/sACN com saída DMX serve.

> Primeiro universo: **Art-Net = 0**, **sACN = 1**. É o erro clássico quando a fita não acende.

## Passo a passo

1. **Propriedades → LED → + Art-Net** (ou **+ sACN**). Em *IP do controlador*, coloque o IP
   do nó (unicast, recomendado) ou deixe vazio para broadcast/multicast.
2. **+ Fita de LED**. A ferramenta **LED (L)** é ativada: arraste os cantos ou o meio da
   fita no workspace para colocá-la sobre a área da imagem que ela deve mostrar. Uma fita
   reta é um retângulo fino; a área pode ser inclinada ou em perspectiva.
3. Ajuste as propriedades:
   - **LEDs (col × lin)**: fita de 60 LEDs = 60 × 1; matriz 16×16 = 16 × 16.
   - **Formato**: RGB, BGR, GRB (WS2812/SK6812), RGBW ou Mono. Cores trocadas indicam
     formato errado.
   - **16 bits**: para fixtures DMX de 16 bits (dois canais por cor).
   - **Zigzag**: as linhas vão e voltam (matriz feita com uma fita dobrada).
   - **Vertical**: a fiação segue as colunas.
   - **Inverter X/Y**: fita instalada "ao contrário".
   - **Curva (gamma)**: 2.2 deixa os tons médios naturais. LEDs não são lineares.
   - **Universo / Canal inicial**: onde o primeiro LED começa.
4. O painel mostra onde a fita termina, por exemplo
   `300 LEDs · universo 0 canal 1 → universo 1 canal 390`.

## Conta de canais

- RGB = 3 canais por LED, RGBW = 4, Mono = 1. Em 16 bits, o dobro.
- Um universo tem 512 canais, então cabem **170 LEDs RGB** por universo. Uma fita maior
  continua sozinha no universo seguinte, porque um LED nunca fica dividido entre dois
  universos. Esse é o mesmo padrão dos controladores de pixel.
- Ao criar uma nova fita no mesmo controlador, ela já começa onde a anterior terminou.
- Duas fitas usando o mesmo canal geram um aviso de **conflito** no painel.

## Testar

- **Monitor DMX** (fim da aba LED): mostra os 512 canais do universo escolhido ao vivo.
  Passe o mouse para ver o valor.
- Os pontos da fita no workspace mostram a cor que está sendo enviada a cada LED.
- **DMX master**: intensidade de todos os LEDs (blackout das luzes). Também funciona por
  OSC (`/lumen/dmx/master 0.5`) e por mapeamento MIDI (alvo *DMX master*).
- **Olho** (checkbox) desliga uma fita, e ela envia preto. **Cadeado** trava a posição.
- Por OSC: `/lumen/led/1/enabled 0`, `/lumen/led/Fita/brightness 0.3`.

## Dicas

- **Conteúdo**: gradientes, cores chapadas animadas e shaders ficam ótimos em LED.
  Detalhes finos se perdem por causa da baixa resolução.
- **LED separado do projetor**: aumente o canvas (por exemplo 3840×1080), deixe a saída do
  projetor usando só a metade esquerda e coloque o conteúdo e as fitas na metade
  direita.
- A fita lê o canvas final, com todas as layers: master e blackout também a escurecem.
- Firewall do Windows: permita o Lumen em redes privadas na primeira execução, senão os
  pacotes não saem.
