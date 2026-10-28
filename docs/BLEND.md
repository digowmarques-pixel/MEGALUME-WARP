# Blend de vários projetores

Vários projetores sobrepostos formam uma imagem única e sem emendas.

## Ligar os projetores no Windows

1. Ligue cada projetor numa saída da placa de vídeo.
2. Use **Win+P → Estender**. Em *Configurações → Sistema → Tela*, deixe cada projetor na
   resolução nativa (por exemplo 1920×1080) e em escala 100%.
3. Organize as telas na ordem física (projetor da esquerda à esquerda...).

## Passo a passo no Lumen

**Atalho: janela Saídas (Ctrl+Shift+P)**, no mesmo formato do Millumin: `layout 3 × 1 de 1920 × 1080`,
um cartão por projetor (tela e botão *Mapping*) e o campo **soft edge (px)** em cada emenda. Cada
emenda pode ter a sua sobreposição. O botão *Mapping* abre as abas **Slice** (parte do canvas),
**Mapping** (cantos ou malha do projetor), **Máscaras** e **Cor e blend**. Embaixo: **Tela cheia**,
**Test Card**, **Grade** e **Identificar**.

O mesmo também está em *Propriedades → Saídas → Assistente de blend*:

1. **Propriedades → Saídas → Assistente de blend**:
   - Colunas × linhas (ex.: 3 × 1 para três lado a lado; 2 × 2 em grade)
   - Resolução do projetor
   - Sobreposição (10–20% é o comum; precisa ser igual à sobreposição física)
   - Monitores: *a partir do monitor 2* (o 1 costuma ser a tela de controle)
   - **Aplicar**: cria as saídas, ajusta o canvas (ex.: 3 × 1920 com 15% → 5184 × 1080),
     as regiões e as bordas suaves. Com *Escalar o mapping atual*, as fatias, máscaras e
     LEDs existentes acompanham o novo canvas.
2. **Abrir saídas** (barra de ferramentas ou Ctrl+P). Use **Identificar saídas** para ver
   qual projetor é qual.
3. **Grade em todas**: o padrão *Grade de blend* é desenhado no canvas. Por isso as linhas
   continuam de um projetor para o outro. Ajuste a posição física e o zoom dos projetores
   e, para o resto, o **warp da saída** (*Editar no workspace*, ferramenta Perspectiva M ou
   Malha G), até as linhas coincidirem nas sobreposições.
4. **Branco** em todas: a emenda deve sumir. Se ficar uma faixa clara ou escura, ajuste
   *Gamma* e *Curva* do soft edge (gamma 2.2 é o padrão para a maioria dos projetores;
   faixa clara: aumente o gamma; faixa escura: diminua).
5. **Preto** em todas, com a sala escura: a sobreposição aparece como uma faixa cinza.
   Aumente o **Nível de preto** de cada projetor até o resto da imagem ficar igual à faixa.
   Valores típicos: 0.002 a 0.02. Com *Ajustar por cor* dá para corrigir um preto azulado.
6. **Cor do projetor**: com Branco em todos, iguale brilho e cor (ganho R G B) entre
   projetores diferentes ou com lâmpadas de idades diferentes.
7. **Máscaras da saída**: recortam a imagem de um projetor (vigas, cortinas, bordas
   irregulares, cúpulas). Com a saída em vista no workspace e a ferramenta Máscara (K).

## Projetores fora de grade (blend livre)

Quando os projetores têm tamanhos diferentes ou estão em posições livres (um mais alto, outro
inclinado, cenário com vários planos):

1. Em cada saída, ajuste a **região do canvas** (aba *Slice* da janela Saídas, ou
   *Propriedades → Saídas → Região do canvas*) para a parte que aquele projetor cobre,
   sobrepondo os vizinhos.
2. Clique em **Blend automático** (ou *Blend automático pelas sobreposições*): o Lumen mede a
   sobreposição de cada par de saídas e calcula as bordas suaves de cada lado.
3. Continue com grade, branco e preto como acima.

## Vários canvases e roteamento

*Propriedades → Projeto → Outros canvases*: cada canvas tem tamanho, fundo e layers próprios.

- **Cada saída escolhe o canvas** que mostra. Ex.: um canvas 5760×1080 para três projetores
  em blend e um canvas 3840×2160 para o processador de LED, no mesmo show.
- **Um canvas dentro de outro**: *Biblioteca → + Ao vivo → Canvas*. Numa layer de outro canvas,
  a posição, escala, rotação e o mapping da layer posicionam o canvas inteiro. Assim vários
  canvases são montados num único sinal 4K para um processador de LED.
- **Spout por saída**: a imagem final da saída (warp, blend, cor) vai para outro programa.
  Com **Sem janela (só Spout)** a saída não abre janela: serve para mandar ao processador de
  LED, para gravação, ou para NDI (com o *Spout to NDI*).
- O LED mapping escolhe de qual canvas lê os pixels.

## Como o blend é calculado

- A borda usa uma curva complementar: c(t) + c(1 − t) = 1. Em qualquer ponto da
  sobreposição, os dois projetores somam exatamente 100% da luz.
- O resultado passa pela correção de gamma do projetor, então quem soma é a *luz* e não o
  valor do pixel.
- A compensação de preto calcula quantos projetores se sobrepõem em cada ponto (1, 2 ou 4
  numa grade) e levanta o preto das outras áreas até o nível do ponto mais claro.

Validação numérica (somando a luz das saídas renderizadas):

| Caso | Variação da luz no canvas inteiro |
|---|---|
| 3 × 1, 15%, branco | ±0,5% (limite do 8 bits) |
| 3 × 1, 15%, preto com compensação 0,01 | ±0,8% |
| 2 × 2, 15%, branco | ±0,6% |
| 2 × 2, 15%, preto com compensação 0,01 | ±1,1% |

## Sincronia

No Windows, as saídas não esperam o vsync uma de cada vez. Todas desenham e o Lumen
aguarda o compositor do Windows (DwmFlush), então todos os projetores mostram o mesmo
quadro no mesmo refresh. A barra de status mostra o tempo gasto nas saídas (ms).
