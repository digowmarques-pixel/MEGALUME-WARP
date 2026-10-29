# Roteiro de teste no Windows

O Lumen é compilado e testado automaticamente (Linux e Windows no GitHub Actions),
mas várias partes só podem ser conferidas numa máquina de verdade, com projetor,
placa de vídeo, câmera e equipamentos. Este roteiro leva cerca de 2 horas.
Marque cada item; quando algo falhar, anote o número do item e mande junto o
relatório do diagnóstico (passo 0).

**Material ideal**: PC com placa de vídeo dedicada e 2 saídas (ou 1 monitor + 1
projetor), um vídeo H.264 e um HAP, uma webcam, um controlador MIDI USB, um
celular com TouchOSC (ou qualquer app OSC), e — se tiver — um projetor com PJLink,
um Arduino e o NDI Tools.

## 0. Instalação e diagnóstico

- [ ] 0.1 Instale com `Lumen-0.10.12-setup.exe` (ou descompacte o .zip). O atalho
      aparece no Menu Iniciar, e arquivos `.lumen` abrem com o Lumen.
- [ ] 0.2 Menu Iniciar → *Lumen (console de diagnóstico)*. O relatório abre no Bloco
      de Notas (também fica em `%APPDATA%\Lumen\diagnostico.txt`).
      **Não pode ter [FALHA]**. Confira: GPU correta (não "GDI Generic" / "Microsoft
      Basic Render"), monitores, saída de áudio, controlador MIDI, câmera.
- [ ] 0.3 Abra o Lumen normal. A janela abre maximizada, preta e amarela, sem erro.
      Se fechar sozinho: rode `lumen-console.exe` e envie o texto do console
      (e `%APPDATA%\Lumen\lumen.log`, se existir).

## 1. Mídia e reprodução

- [ ] 1.1 Arraste um vídeo H.264, um HAP e uma imagem PNG para a biblioteca. As
      miniaturas aparecem.
- [ ] 1.2 Arraste o vídeo para uma célula e clique no ▶ da coluna: toca com som, sem engasgar.
      A barra de baixo mostra ~60 fps.
- [ ] 1.3 *Projeto → Decodificação por hardware* ligada: um vídeo 4K H.264/HEVC
      toca liso. Em *Mídia* aparece "Decodificação por hardware: d3d11va".
- [ ] 1.4 Espaço (GO), Shift+Espaço, B (blackout). Esc fecha um menu aberto e **não** para o vídeo.
- [ ] 1.5 Transições: crossfade, corte, pelo preto e uma *Shader (ISF)* (ex.:
      T_Circulo) entre duas colunas.

## 2. Saídas, mapping e blend

- [ ] 2.1 *Saídas...*: escolha o monitor do projetor como destino e abra as saídas
      (Ctrl+P). Tela cheia, sem bordas, no monitor certo.
- [ ] 2.2 Perspectiva (M) e Malha (G) numa layer: a imagem deforma ao arrastar.
- [ ] 2.3 Warp da saída inteira (*Editar no workspace*) e Ctrl+Shift+D (controles
      desenhados no projetor).
- [ ] 2.4 Com dois projetores: *Assistente de blend* 2×1, padrão *Grade de blend*.
      As linhas coincidem na emenda; com *Branco* a emenda some ajustando gamma.
- [ ] 2.5 Fechar e abrir as saídas várias vezes; desligar e ligar o cabo do
      projetor com o Lumen aberto (não pode travar).

## 3. Efeitos e texto

- [ ] 3.1 *+ Efeito* numa layer: Desfoque, ChromaKey, Caleidoscópio. Parâmetros
      respondem na hora.
- [ ] 3.2 Edite um `.fs` da pasta `effects` no Bloco de Notas e salve: o efeito
      recarrega sozinho.
- [ ] 3.3 *+ Gerador → Texto (dinâmico)*: acentos (ç, ã, é) aparecem certos.
      Mande `/layer:<nome>/text "Olá"` por OSC.

## 4. Entradas ao vivo

- [ ] 4.1 *+ Ao vivo*: a webcam aparece na lista e mostra imagem.
- [ ] 4.2 *Blob tracker* com a webcam: *Gravar fundo*, passe a mão na frente — os
      blobs aparecem.
- [ ] 4.3 Spout: abra um programa que envia Spout (ex.: o demo do Spout, Resolume,
      TouchDesigner). Ele aparece em *+ Ao vivo → Spout*.
- [ ] 4.4 Envie a saída por Spout e receba no outro programa.
- [ ] 4.5 NDI (com o NDI Tools instalado): *+ Ao vivo → NDI* mostra as fontes;
      *NDI desta saída* aparece no *NDI Studio Monitor*.

## 5. Controle

- [ ] 5.1 OSC: TouchOSC no celular para o IP do PC, porta 8000. `/lumen/go` funciona.
      O Windows pergunta sobre o firewall na primeira vez: permita.
- [ ] 5.2 MIDI: *Aprender* num mapeamento e mexa um fader do controlador.
- [ ] 5.3 Teclado como entrada (`/key/a`) e DMX de entrada (se tiver mesa de luz).
- [ ] 5.4 Fader de transição manual: coluna com *Manual (fader)*, mova o fader da
      barra de cima (e por MIDI).

## 6. Timeline e timecode

- [ ] 6.1 Crie uma timeline, faixa *Opacidade*, duas chaves; ligue à coluna. Ao
      disparar a coluna a opacidade anima.
- [ ] 6.2 Marcador com `/lumen/go`: a timeline dispara a próxima coluna.
- [ ] 6.3 MTC: gere MTC a partir de uma timeline numa saída MIDI (loopMIDI cria uma
      porta virtual) e receba em outro programa (ou no próprio Lumen).
- [ ] 6.4 LTC: toque um arquivo de áudio LTC (ou um gerador) na entrada de linha;
      *Receber → LTC* mostra o timecode correndo; a timeline com *timecode* segue.

## 7. Equipamentos

- [ ] 7.1 PJLink: cadastre o projetor, use *Ligar*, *Fechar obturador*. O estado
      aparece ao lado.
- [ ] 7.2 Comandos numa coluna (`pjlink`, `midi`, `dmx`, `tcp`, `wait`) — veja o
      resultado no Monitor OSC.
- [ ] 7.3 Arduino: carregue o exemplo de `docs/DISPOSITIVOS.md`, ligue a serial
      e use *Aprender* com o potenciômetro.
- [ ] 7.4 LED: uma fita por Art-Net/sACN (ou o *Monitor DMX* se não tiver fita).

## 8. Acabamento

- [ ] 8.1 Edite um projeto sem salvar e mate o processo pelo Gerenciador de Tarefas
      (depois de 1 minuto). Ao abrir o projeto, o Lumen oferece *Recuperar*.
- [ ] 8.2 Mova a pasta do projeto (com as mídias) para outro lugar e abra:
      as mídias são encontradas sozinhas. Renomeie uma mídia: ela fica vermelha
      na biblioteca e *Procurar mídias ausentes...* resolve.
- [ ] 8.3 Modo show (Ctrl+Shift+L ou botão *Editar/SHOW*): não dá para editar, mas
      GO, B e masters funcionam.
- [ ] 8.4 *Idioma / Language → English*: a interface muda para inglês; reabra o
      programa e continua em inglês.
- [ ] 8.5 Desinstale pelo *Configurações → Aplicativos*: os projetos e mídias do
      usuário continuam onde estavam.

## 9. Desempenho, áudio multicanal e reserva (v0.10)

- [ ] 9.1 Três ou quatro vídeos 4K H.264 ao mesmo tempo: a barra de baixo mostra
      *GPU: ... MB/s* e o fps fica estável. Compare com *Envio assíncrono à GPU*
      desligado (anote os dois valores de fps e de ms por quadro).
- [ ] 9.2 Um vídeo 8K (H.265 ou HAP): toca sem engasgar? Anote a placa de vídeo.
- [ ] 9.3 Interface de áudio multicanal: *Canais* mostra todos; uma layer em *Saídas 3-4*
      sai nas saídas 3 e 4; a matriz manda um canal para uma saída escolhida.
- [ ] 9.4 Um arquivo 5.1: com placa de 6+ canais sai em 6 saídas; com placa estéreo
      sai mixado (centro nos dois lados).
- [ ] 9.5 Modo exclusivo ligado: *Em uso* diz "exclusivo"; o som não estala com buffer
      de 5 ms.
- [ ] 9.6 Duas máquinas na rede (veja docs/REDUNDANCIA.md): GO na principal muda a
      reserva; tirar o cabo de rede da principal faz a reserva assumir em ~1 s e rodar
      o comando de troca.
- [ ] 9.7 Envio NDI de uma saída 4K: o fps do Lumen não cai em relação ao NDI desligado.

## 10. Resistência (show longo)

- [ ] 10.1 Deixe um projeto com auto-GO em loop rodando por 8 horas com as saídas
      abertas. Anote o uso de memória no começo e no fim (não pode crescer sem
      parar) e se houve algum engasgo.
- [ ] 10.2 Abra o projeto pela pasta *Inicializar* do Windows (`shell:startup`)
      com *Ao abrir o projeto → Abrir as saídas* e reinicie o PC: o show volta sozinho.

## 11. Correções da revisão (v0.10.1) que só o Windows confirma

- [ ] 11.1 Num PC (ou máquina virtual) **sem** o *Visual C++ Redistributable*: o Lumen
      abre sem erro de `VCRUNTIME140.dll` ou `MSVCP140.dll`, e o atalho *Lumen (console
      de diagnóstico)* abre o console.
- [ ] 11.2 Instale uma versão antiga numa pasta escolhida (ex.: `D:\Lumen`), coloque um
      `.fs` seu em `effects` e instale a 0.10.1 por cima: o instalador já sugere
      `D:\Lumen`, o seu `.fs` continua lá e o atalho do Menu Iniciar aparece também
      para outro usuário do PC.
- [ ] 11.3 *Executar o Lumen* no fim do instalador: arrastar um vídeo do Explorer para
      a biblioteca funciona (o Lumen não abriu como administrador).
- [ ] 11.4 Com um vídeo tocando **com som**, renomeie uma mídia do projeto e use
      *Arquivo → Procurar mídias ausentes...*: a janela de escolher pasta abre.
- [ ] 11.5 PJLink: o estado aparece (ligado / desligado / ligando). Com o projetor fora
      da tomada, um comando `pjlink` dá erro em ~3 s e as próximas linhas da coluna
      não ficam esperando.
- [ ] 11.6 Arduino: o comando `serial COM3@9600 ...` disparado três vezes seguidas
      não reinicia a placa (o LED *L* não pisca de novo e os relés não voltam ao
      início). Tire o cabo USB da porta que está **recebendo** e ponha de volta: a
      seção Serial mostra "desconectada: tentando..." e volta a ler sozinha.
- [ ] 11.7 Com um vídeo com som tocando, desconecte a interface de áudio USB: o vídeo
      continua andando; ao reconectar (ou escolher outra saída), o som volta.
- [ ] 11.8 Interface com mais de 32 canais (ex.: Dante Virtual Soundcard, MADI): uma
      layer em *Saídas 33-34* sai nas saídas 33 e 34. Com *Canais* diferente do que a
      interface tem, o som sai nas saídas certas (não dobrado nem trocado).

## 12. Dashboard e tempo (v0.10.2)

- [ ] 12.1 Clicar numa célula só a seleciona (borda branca, aba *Célula*); nada vai ao ar.
      O ▶ da coluna, Espaço ou botão direito → *Disparar só esta célula* põem no ar.
- [ ] 12.2 Painel *Tempo* (à direita do dashboard): com um vídeo tocando, o relógio conta
      o tempo restante; clicar nele mostra o decorrido. Nos últimos 10 s fica laranja.
- [ ] 12.3 ⏸ pausa imagem e som; ▶ continua do mesmo ponto, com o som em sincronia e sem estalo.
- [ ] 12.4 Esc com vídeo tocando: nada para. Com um menu aberto, Esc só fecha o menu.
- [ ] 12.5 Aba *Projeto*: abre normalmente; mudar a resolução e apertar Enter aplica.

## 13. Interface estilo Millumin (v0.10.3)

- [ ] 13.1 Propriedades de uma layer: seções Layer, Mapping, Efeitos, Máscaras e Mídia atual.
      Em Mapping, ligar *Deformação (warping)* mostra a malha; a prévia acompanha.
- [ ] 13.2 *+ Efeito*: seções Cor, Padrão, Especial e Vários.
- [ ] 13.3 *Saídas...* com 1×1: soft edge nos quatro lados; 200 px à esquerda escurece a
      borda esquerda do projetor até o preto.
- [ ] 13.4 Menus Lista, Quadro, Saídas e Janela: cada item faz o que diz.
- [ ] 13.5 *Saídas... → Mapping → Slice*: ligar *warping* e arrastar um canto deforma a
      imagem do projetor; desligar volta ao retângulo.
- [ ] 13.7 Janela → Propriedades das saídas / Interação / Projeto abrem como janelas; o painel
      da direita fica com Layer, Célula e Mídia.
- [ ] 13.8 Painel Tempo: arrastar a divisa à esquerda dele o alarga; clicar ou arrastar na
      linha do tempo leva o vídeo (com som) para aquele ponto.
- [ ] 13.9 Com um vídeo em loop tocando, mudar *No fim* para *Congelar*: ele para no último quadro.
- [ ] 13.10 Máscaras: + → Máscara redonda cria a máscara; o modo mostrar / esconder muda na
      própria linha; − remove.
- [ ] 13.11 Mapping → Editor de fatias...: arrastar um lado muda a parte da mídia usada;
      *Livre (4 cantos)* deixa mover cada canto.
- [ ] 13.12 Várias saídas sem tela (monitor virtual) + Ctrl+P: uma só janela "Monitores
      virtuais" com todas; nenhuma janela extra do Windows.
- [ ] 13.13 Máscara: com *Adicionar pontos* desligado, clicar no workspace não cria ponto;
      ligado, cria.
- [ ] 13.14 Mudar a velocidade da célula com o vídeo tocando muda na hora.
- [ ] 13.15 Vídeo com som a 1,5x: o som continua (mais agudo) e em sincronia com a imagem.
- [ ] 13.16 Saídas → Mapping → Slice: clicar na área de outro projetor o seleciona.
- [ ] 13.17 Arrastar a aba "Biblioteca" para baixo das Propriedades: ela fica lá; fechar e
      abrir o Lumen mantém; *Janela → Restaurar layout dos painéis* volta ao original.
- [ ] 13.18 Dashboard: no seletor, + cria outro dashboard; GO num dashboard não pula para
      as colunas do outro.
- [ ] 13.19 Saídas → Mapping → Malha: as linhas da malha aparecem entre os pontos.
- [ ] 13.20 Arrastar 2 vídeos do Explorer para uma célula: o 1º entra nela, o 2º na coluna
      seguinte; os dois aparecem na biblioteca.
- [ ] 13.6 LED escondido: sem botão LED, sem aba LED; *Janela → LED mapping* mostra tudo de volta.

## Como relatar

Para cada falha: número do item, o que aconteceu, o relatório do diagnóstico
(*Ajuda → Diagnóstico do sistema → Copiar*) e, se possível, uma foto ou vídeo.
