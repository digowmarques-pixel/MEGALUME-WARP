# Efeitos, shaders, matte e rastreamento

## Efeitos (shaders ISF)

*Propriedades → Layer → Efeitos → + Efeito*. Os efeitos são aplicados em ordem sobre a mídia
da layer, antes do mapping. Botão direito no efeito: subir, descer, valores padrão, remover.

O formato é o **ISF** (Interactive Shader Format), o mesmo do Millumin, VDMX, Resolume e
MadMapper. Os parâmetros aparecem sozinhos no painel.

| Categoria | Efeitos incluídos |
|---|---|
| Cor | Níveis, Limiar, Posterizar, Chroma key, Duotone |
| Geometria | Espelho, Recorte suave, Caleidoscópio, Onda |
| Desfoque | Desfoque (gaussiano), Brilho (glow) |
| Estilizar | Bordas, Vinheta, Deslocar RGB, Pixelizar, Rastro (feedback) |

**Mais efeitos:** baixe arquivos `.fs` em [editor.isf.video](https://editor.isf.video) ou na
coleção da Vidvox, e use *+ Efeito → Abrir arquivo ISF*, ou coloque-os na pasta `effects` ao
lado do `lumen.exe` (*Recarregar a pasta de efeitos*). Dos 327 shaders da coleção oficial, 324
funcionam sem mudança (multi-passes, buffers persistentes e imagens importadas incluídos).

**Editar ao vivo:** abra o `.fs` em qualquer editor de texto e salve. O Lumen recarrega em
cerca de 1 segundo. Se o shader tiver erro, a versão anterior continua no ar e o erro aparece
no painel.

## Geradores ISF

*Biblioteca → + Gerador → Shaders ISF*: Ruído, Anéis, Varredura (faixa de luz que percorre a
superfície), Degradê. Qualquer `.fs` sem `inputImage` funciona como gerador.

## Matte

No *Blend* da layer: **Matte alpha**, **Matte alpha invertido**, **Matte luma**, **Matte luma
invertido**. A layer com matte não aparece: ela recorta a layer logo abaixo. Ex.: um vídeo em
preto e branco (ou uma câmera com o efeito Limiar) revela outro vídeo só onde é claro.

## Rastreamento de blobs (instalações interativas)

1. Ponha a câmera numa layer (*+ Ao vivo*) e adicione o efeito **Rastreamento de blobs**.
2. Com a cena vazia, clique em **Gravar fundo**. Depois, o que mudar vira blob (pessoas,
   objetos), mesmo com luz irregular. Sem fundo gravado, vale o limiar (regiões claras, ou
   escuras com *Objetos escuros*).
3. Ajuste *Limiar*, *Tamanho mínimo / máximo* e *Juntar blobs* olhando a prévia com os
   retângulos. *Mostrar → Limiar* mostra a imagem preto e branco na própria layer.
4. Use os atalhos: **segue o blob 1** (uma layer acompanha a pessoa) ou **Presença liga a
   layer** (aparece quando alguém entra), ou crie mapeamentos com os endereços abaixo.

Endereços (posições de 0 a 1, origem no canto superior esquerdo):

| Endereço | Valor |
|---|---|
| `/blob/<layer>/count` | Número de blobs |
| `/blob/<layer>/1` | x y largura altura do blob 1 |
| `/blob/<layer>/1/x`, `/y`, `/size` | Uma coordenada só |
| `/blob/<layer>/1/active` | 1 enquanto o blob 1 existe, 0 quando ele some |
| `/blob/<layer>/centroid` | Média de todos os blobs |

O blob 1 continua sendo a mesma pessoa enquanto ela estiver na cena. Com *Enviar também às
saídas OSC*, os mesmos dados vão para TouchDesigner, Unity etc.

Dicas: luz uniforme e contraste ajudam mais que resolução; 160 px de análise bastam; câmera
fixa; luz infravermelha + câmera sem filtro IR separa as pessoas da projeção.
