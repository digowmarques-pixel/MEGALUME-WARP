# Equipamentos, timecode, NDI, texto e Arduino

Este guia cobre o que o Lumen controla fora dele (projetores, mesas, outros
programas) e o que entra nele (timecode, Arduino, NDI).

## Comandos de equipamentos

Os mesmos comandos funcionam em quatro lugares:

- **Coluna** → botão direito no cabeçalho → *Comandos para equipamentos* (um por
  linha, executados em ordem quando a coluna é disparada);
- **Marcadores da timeline** (duplo clique na faixa de marcadores);
- **Agenda** (*Interação → Agenda*, ação *Comando*);
- **OSC**: `/lumen/command "pjlink Sala power on"`.

Eles rodam numa linha separada: um projetor que demora para responder não trava o show.
Linhas que começam com `#` são comentários. Para testar, use *Interação → Testar um
comando*; o resultado (e a resposta do equipamento) aparece no *Monitor OSC*.

| Comando | Exemplo | O que faz |
|---|---|---|
| `pjlink <projetor> power on\|off` | `pjlink Sala power off` | Liga / desliga (PJLink classe 1, porta 4352) |
| `pjlink <projetor> shutter close\|open` | `pjlink Sala shutter close` | Obturador / AV mute (`mute` é sinônimo) |
| `pjlink <projetor> input <código>` | `pjlink Sala input 31` | Troca a entrada (11 RGB1, 31 HDMI1, 32 HDMI2...) |
| `pjlink <projetor> status` | `pjlink Sala status` | Consulta se está ligado |
| `midi note <canal> <nota> <vel>` | `midi note 1 60 127` | Note On (canal 1–16) |
| `midi noteoff <canal> <nota>` | `midi noteoff 1 60` | Note Off |
| `midi cc <canal> <n> <valor>` | `midi cc 1 7 100` | Control Change |
| `midi pc <canal> <programa>` | `midi pc 1 5` | Program Change (troca de cena em mesas de som) |
| `midi@<porta> ...` | `midi@QLab note 1 60 127` | Escolhe a saída MIDI pelo nome (parte do nome basta) |
| `dmx [artnet\|sacn] <universo> <canal> <valor>` | `dmx 0 1 255` | Ajusta um canal DMX (0–255); o valor fica guardado e é reenviado |
| `dmx ... <canal>-<último> <valor>` | `dmx sacn 1 1-12 0` | Uma faixa de canais |
| `tcp <ip>:<porta> <texto>` | `tcp 192.168.1.20:23 PWR ON[CR]` | Envia texto por TCP e mostra a resposta |
| `udp <ip>:<porta> <texto>` | `udp 192.168.1.30:7000 [HEX_02]PLAY[HEX_03]` | Envia texto por UDP |
| `serial <porta>[@baud] <texto>` | `serial COM3@9600 rele1 on\n` | Envia texto pela porta serial (Arduino, relés); a porta fica aberta |
| `osc <ip>:<porta> /endereço args` | `osc 192.168.1.40:53000 /go` | Mensagem OSC (QLab, TouchDesigner...) |
| `wait <segundos>` | `wait 2.5` | Espera antes da próxima linha |

**Projetores**: cadastre em *Interação → Projetores (PJLink)* com nome, IP e senha
(se houver). Nos comandos use o nome (`pjlink Sala ...`, ou `pjlink Projetor 1 ...`
quando o nome tem espaço) ou direto o IP. Os botões
ao lado de cada projetor ligam, desligam e fecham o obturador, e mostram o estado.
Ative o PJLink no menu de rede do projetor (Epson: *Rede → Protocolo de controle*;
Panasonic: *Rede → PJLink*). Sem senha, deixe o campo vazio.

**DMX**: o comando usa os controladores de *Propriedades → LED* (Art-Net / sACN; a aba aparece com *Janela → LED mapping*) do
mesmo protocolo; sem nenhum, vai em broadcast (Art-Net) ou multicast (sACN).
Cada universo guarda o último valor de cada canal, então `dmx 0 1 255` seguido de
`dmx 0 2 128` mantém o canal 1 em 255. Os valores são reenviados uma vez por segundo
(um pacote perdido não perde a cena, e receptores sACN não soltam a fonte). Se o
mesmo universo também tem LEDs, os dois se juntam: vale o maior valor de cada canal.

**Texto em tcp / udp / serial**: `[CR]`, `[LF]`, `[TAB]`, `\r`, `\n`, `\t` e
`[HEX_xx]` (um byte em hexadecimal) viram os caracteres de controle.

Exemplo de coluna "Abertura":

```
pjlink Sala shutter open
midi@Behringer pc 1 3
dmx 0 1-4 255
wait 1
osc 192.168.1.40:53000 /cue/abertura/start
```

## Timecode (MTC e LTC)

Em *Janela → Timecode (MTC / LTC)* (MTC precisa do MIDI ativado: a janela avisa):

- **Receber**: *MTC* chega por qualquer entrada MIDI; *LTC* entra pela placa de som
  (escolha a entrada e o canal: 0 = esquerdo). O valor recebido aparece ao lado, com
  os quadros por segundo detectados.
- **Roda livre**: se o sinal cair (cabo com mau contato), o relógio continua sozinho
  por esse tempo.
- **Timeline seguindo o timecode**: na timeline marque *timecode* e diga em que
  timecode ela está no tempo 0 (*início*). Ela anda junto com o timecode recebido,
  inclusive para trás e em saltos.
- **Coluna no timecode**: no menu da coluna, *Disparar no timecode* com o valor
  `hh mm ss qq`. A coluna dispara quando o timecode passa por ali.
  Com *Vídeo segue o timecode* (ligado por padrão) o vídeo anda preso ao timecode:
  pausa quando o Reaper/mesa para (no último quadro recebido), pula quando ele pula, e
  dar play em qualquer ponto já entra na coluna certa (a última cue antes daquele
  ponto), com o vídeo na posição certa. Antes da cue, o vídeo espera no primeiro quadro.
- **Atraso (quadros)**: se o vídeo estiver adiantado ou atrasado em relação ao som,
  ajuste aqui (positivo atrasa o vídeo, negativo adianta). Dá para mudar com o show
  rodando: o vídeo se acerta sozinho em um ou dois segundos.
- **Gerar**: escolha uma timeline, o timecode inicial (padrão 01:00:00:00) e o fps
  (24, 25, 29.97 drop, 30). O Lumen gera MTC numa saída MIDI e/ou LTC num canal da
  saída de áudio enquanto a timeline estiver tocando — para sincronizar luz (grandMA,
  Chamsys) ou som (QLab, Reaper) com o vídeo.

## Arduino e sensores pela serial

*Interação → Serial (Arduino)*: escolha a porta e o baud. Cada linha que chega vira
uma entrada, igual a uma mensagem OSC, e pode ser usada nos mapeamentos (o
**Aprender** funciona):

| Linha enviada pelo Arduino | Chega como |
|---|---|
| `A0 512` | `/serial/A0 512` |
| `botao 1` | `/serial/botao 1` |
| `12 34` | `/serial 12 34` |

Exemplo (potenciômetro em A0 e botão no pino 2):

```cpp
void setup() {
  Serial.begin(115200);
  pinMode(2, INPUT_PULLUP);
}

int last = -1;
void loop() {
  Serial.print("A0 ");
  Serial.println(analogRead(A0));      // 0..1023
  int b = digitalRead(2) == LOW;
  if (b != last) {                    // só quando muda
    Serial.print("botao ");
    Serial.println(b);
    last = b;
  }
  delay(20);
}
```

No Lumen: mapeamento `/serial/A0`, entrada 0–1023, alvo *Opacidade* de uma layer; e
`/serial/botao`, transformação *Chave*, alvo *Disparar coluna*.

Se o Arduino for desligado ou desconectado (ou ainda não estiver ligado quando o
Lumen abrir), o Lumen tenta abrir a porta de novo a cada segundo; a seção Serial
mostra "Lendo COM3" ou "COM3 desconectada: tentando de novo a cada segundo".

Para enviar ao Arduino (relés, motores), use o comando `serial COM3@115200 rele 1\n`
nas colunas. A porta fica aberta depois do primeiro comando: abrir a porta reinicia a
maioria dos Arduinos, e reabrir a cada linha perdia o que foi enviado. Não use a mesma
porta para receber e enviar (o Windows só deixa abrir a porta uma vez); ao ligar a
recepção numa porta na seção Serial, o comando solta essa porta.

## NDI

Precisa do **NDI Runtime** (vem com o NDI Tools, gratuito em ndi.video). O Lumen
procura `Processing.NDI.Lib.x64.dll` na pasta do runtime (`NDI_RUNTIME_DIR_V6`/`V5`)
e ao lado do `lumen.exe`. Sem o runtime, o resto funciona normalmente.

- **Receber**: *Biblioteca → + Ao vivo → NDI (rede)*: as fontes da rede aparecem
  sozinhas (câmeras NDI, OBS, outro Lumen, vMix, celular com NDI HX Camera).
- **Enviar uma saída**: na saída, *NDI desta saída*. A imagem vai com warp, blend e
  cor; com *Sem janela* ela existe só na rede (ex.: para um processador de LED).
- **Enviar o canvas**: *Propriedades → Projeto → NDI (enviar o canvas)*.

## Texto dinâmico

*Biblioteca → + Gerador → Texto (dinâmico)*. O texto é desenhado na fonte escolhida
(Segoe UI por padrão), com tamanho, cor, fundo, alinhamento e quebra de linha.

- Mude ao vivo por OSC: `/layer:Placar/text "Casa 2 x 1 Visitante"`.
- Ou leia de um arquivo `.txt`, que é lido de novo sempre que muda — outro programa
  (planilha, script, sistema de senha) só precisa salvar o arquivo.

## Endereços OSC novos da v0.9

| Endereço | Efeito |
|---|---|
| `/lumen/timeline/<n ou nome>/play` | Toca a timeline do início (`play 0` continua de onde parou) |
| `/lumen/timeline/<n>/pause`, `/stop` | Pausa / para |
| `/lumen/timeline/<n>/time <s>` | Vai para o tempo |
| `/lumen/transition <0..1>` | Fader de transição manual |
| `/lumen/command "<comando>"` | Executa um comando de equipamento |
| `/layer:<nome>/text "<texto>"` | Troca o texto de uma mídia de texto |
| `/layer:<nome>/effect1/<parâmetro> <valor>` | Parâmetro de um efeito ISF |
