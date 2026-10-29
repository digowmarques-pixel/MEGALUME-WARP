# Áudio multicanal

O Lumen toca o som dos vídeos sincronizado com a imagem (o relógio da placa de som
comanda o vídeo). Desde a v0.10 ele usa **todos os canais** da interface de áudio e
cada layer escolhe para onde vai o seu som.

## Placa de som

Em *Propriedades → Projeto → Áudio*:

- **Saída de áudio**: o dispositivo. Interfaces multicanal (Focusrite, RME, MOTU,
  Behringer, Dante Virtual Soundcard, MADI...) aparecem com todos os canais.
- **Canais**: *Todos do dispositivo* (padrão) ou um número fixo (2 a 64). Um número
  maior que o da placa usa os canais que ela tem. Placas com mais de 64 saídas
  funcionam, com as 64 primeiras.
- **Modo exclusivo (WASAPI)** — só no Windows: o Lumen usa a placa sozinho, sem o
  mixer do Windows. Tem menos latência, não reamostra e entrega os canais do jeito
  que estão. Se o dispositivo recusar, volta ao modo compartilhado e avisa.
- **Buffer (ms)**: 10 ms é seguro. Diminua para menos atraso entre imagem e som, ou
  aumente se estalar.

A linha *Em uso* mostra o dispositivo, os canais e a latência de fato. Os medidores
mostram cada canal de saída, numerados.

**Se a placa parar** (USB desconectado, driver travado): os vídeos continuam no
relógio de quadros, sem congelar; o Lumen tenta reabrir a placa (ou a padrão, ou o
relógio silencioso) a cada 3 s, e o som volta sincronizado com a imagem. O log mostra
`[audio] a placa de som parou; reabrindo`.

**Velocidade diferente de 1**: o clipe fica sem som enquanto isso; ao voltar para 1, o
som volta no ponto certo da imagem.

## Para onde vai o som de cada layer

Em *Propriedades → Layer*, logo abaixo do volume, fica a **Saída de áudio**:

- **Saídas 1-2, 3-4, 5-6...**: o par de saídas desta layer. Exemplo: a layer
  "Vídeo principal" nas caixas do salão (1-2) e a layer "Cabine" no fone (3-4).
- Os arquivos são tratados assim:
  - **mono**: vai para as duas saídas do par;
  - **estéreo**: L e R vão para o par;
  - **5.1 / 7.1**: ocupam saídas seguidas a partir do par (FL, FR, C, LFE, BL, BR...)
    ou, se não couberem, são mixados no par (centro e surround entram nos lados, sem o LFE).
- **Matriz personalizada**: uma grade com os canais do arquivo nas linhas e as saídas
  da placa nas colunas. Marque onde cada canal deve sair; o mesmo canal pode ir para
  várias saídas. *Voltar ao automático* desfaz.

Se o projeto foi feito para uma placa de 8 canais e abrir num notebook de 2, as layers
que iam para 3-4, 5-6... tocam em 1-2. Assim dá para ensaiar em qualquer lugar.

## Timecode LTC numa saída própria

Em *Interação → Timecode → Gerar*, a saída do LTC pode ser esquerda, direita, os dois
ou **qualquer saída da interface** (ex.: saída 8, ligada direto na mesa de luz).
O LTC não passa pelo volume master (master em zero não corta o timecode) e segue o
relógio da placa: depois de um engasgo do computador ele se realinha à timeline em vez
de ficar atrasado.

## ASIO

O Lumen ainda **não fala ASIO**. O SDK da Steinberg só pode ser usado sob a licença
GPLv3 ou com um acordo com a Steinberg; isso precisa ser decidido antes de incluí-lo.
Enquanto isso, o **modo exclusivo do WASAPI** dá latência parecida, e quase todas as
interfaces profissionais têm driver WASAPI multicanal (Dante Virtual Soundcard
também, no modo WDM).

## Velocidade

Um vídeo com velocidade diferente de 1x continua com som, como numa fita: o som acelera ou
desacelera junto e o tom sobe ou desce (1,03x quase não se nota; 2x fica uma oitava acima).
De 0,25x a 4x o som acompanha e continua comandando a imagem; fora disso o vídeo toca sem som.
