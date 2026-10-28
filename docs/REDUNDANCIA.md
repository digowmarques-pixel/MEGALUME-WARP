# Máquina reserva (redundância)

Dois computadores rodam o mesmo show. A **principal** comanda; a **reserva** faz
exatamente o mesmo, em sincronia, e assume sozinha se a principal travar, desligar
ou perder a rede. É o que se usa em eventos ao vivo, museus e instalações que não
podem apagar.

## Como funciona

- A principal envia o estado do show pela rede **10 vezes por segundo** e na hora de
  cada mudança: coluna no ar, mídia e tempo de cada layer, opacidade, timelines,
  blackout, masters e fader de transição.
- A reserva dispara as mesmas colunas, com as mesmas transições, e **corrige o tempo
  dos vídeos**: se um vídeo se afastar mais que a *tolerância* (padrão 70 ms), ele
  salta para o tempo da principal. Isso no máximo uma vez por segundo por layer.
- Enquanto segue, a reserva **ignora os comandos locais**: OSC, MIDI, teclado, DMX,
  serial, agenda, auto-GO, cues de timecode, marcadores e os comandos de equipamentos
  das colunas. Só a principal manda nos projetores, na luz e no som, então nada é
  enviado em dobro. GO e Parar ficam desativados na tela da reserva.
- Se a principal ficar em silêncio pelo *tempo para assumir* (padrão 1 s), a reserva
  **assume**: passa a aceitar os comandos, segue o show de onde está e roda os
  *comandos ao assumir*.
- Quando a principal volta, a reserva **continua no comando** até alguém clicar em
  *Voltar a seguir*. Assim o show não fica alternando entre as máquinas.

## Montagem

1. Copie a pasta do projeto, com as mídias, para os dois computadores. O projeto tem
   que ser o mesmo: se for diferente, a principal mostra "projeto DIFERENTE".
2. Ligue as duas máquinas na mesma rede, de preferência num switch só delas e com
   IP fixo (ex.: 192.168.10.1 e 192.168.10.2).
3. Na principal, em *Interação → Máquina reserva*: **Principal**, *Outra máquina* =
   IP da reserva. Clique em **Aplicar nesta máquina**.
4. Na reserva: **Reserva**, *Outra máquina* = IP da principal, **Aplicar nesta máquina**.
5. Libere a **porta UDP 7400** no firewall do Windows nas duas (o Windows pergunta na
   primeira vez).

A função de cada computador fica salva **no computador**, não no projeto. Por isso o
mesmo `.lumen` serve para os dois. Com *Outra máquina* vazio, a principal envia
para a rede toda (broadcast).

Na barra de cima aparece **PRINCIPAL**, **RESERVA** ou, em vermelho,
**RESERVA NO COMANDO**. Passe o mouse para ver os detalhes. A principal mostra se a
reserva está seguindo.

Para instalações que ligam sozinhas, o papel também pode ir na linha de comando:

```
lumen.exe show.lumen --outputs --show-mode --role backup --sync-peer 192.168.10.1
```

## Saída de vídeo

Há duas formas de ligar as máquinas aos projetores:

- **Switcher / matriz de vídeo** (Blackmagic Videohub, ATEM, Kramer, Barco E2...): as
  duas máquinas entram no switcher, que normalmente mostra a principal. Nos *comandos
  ao assumir* da reserva, coloque o comando que troca a entrada. Exemplos:

  ```
  tcp 192.168.10.50:9990 VIDEO OUTPUT ROUTING:[LF]0 1[LF][LF]
  osc 192.168.10.60:9000 /atem/program 2
  ```

  O primeiro é o protocolo do Videohub: saída 0 recebe a entrada 1. O segundo é um
  exemplo para um ATEM controlado por um software que recebe OSC, como o Companion.

- **Projetores com duas entradas**: a principal na HDMI1, a reserva na HDMI2. Ao
  assumir, a reserva troca a entrada pelo PJLink:

  ```
  pjlink Esquerdo input 32
  pjlink Direito input 32
  ```

Os comandos são os mesmos das colunas; veja [DISPOSITIVOS.md](DISPOSITIVOS.md).

## Som

Se as duas máquinas tocam som, ligue as duas na mesa: a reserva num canal fechado,
que um comando abre ao assumir (ex.: `midi cc 1 7 127` para a mesa). Ou deixe só a
principal com som e use o comando de troca também para o áudio.

## Ajustes

| Ajuste | Padrão | Para que serve |
|---|---|---|
| Tempo para assumir | 1 s | Quanto silêncio da principal antes de a reserva assumir. Em redes instáveis, 2–3 s. |
| Tolerância de sincronia | 70 ms | Quanto o vídeo da reserva pode se afastar antes de saltar. Cerca de 2 quadros. |
| Porta | 7400 | Porta UDP da sincronia (configuração da máquina). |

## Testar antes do show

1. Com as duas máquinas seguindo, dê GO na principal: a reserva muda junto.
2. Tire o cabo de rede da principal: em cerca de 1 s a reserva mostra
   **RESERVA NO COMANDO** e roda os comandos de troca.
3. Recoloque o cabo, clique em *Voltar a seguir* na reserva e confira que as duas
   voltam a andar juntas.

O teste automático `scripts/test-integration.sh` faz isso com duas instâncias no
mesmo computador.

## Detalhes técnicos

- As mensagens são OSC sobre UDP: `/lumen/sync/state` (principal → reserva) e
  `/lumen/sync/backup` (reserva → principal, 2 vezes por segundo). O argumento é um
  texto JSON.
- A comparação de projeto usa as mídias (nome do arquivo), layers, colunas e timelines.
- A correção de tempo usa o *seek* preciso do Lumen (decodifica a partir do quadro-chave
  e descarta até o quadro exato), no vídeo e no áudio.
