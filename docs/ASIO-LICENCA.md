# ASIO®: licença da Steinberg

O Lumen usa o **Steinberg ASIO SDK** pela **licença proprietária** da Steinberg (a outra opção,
GPLv3, obrigaria o Lumen a ser GPL). Ela é gratuita, mas tem regras:

## Antes de distribuir uma versão com ASIO

1. Baixe o SDK em steinberg.net/asiosdk; o contrato vem dentro dele
   (`Steinberg ASIO Licensing Agreement.pdf`).
2. Preencha a 1ª página (empresa ou pessoa, endereço) e a última (assinatura e contato
   técnico), assine e envie para **reception@steinberg.de**.
3. Espere a cópia **assinada pela Steinberg**. Só depois disso publique instaladores com ASIO.
   Até lá, as versões públicas saem sem ASIO (o programa funciona igual, com WASAPI).

## Regras que o Lumen já cumpre

- O SDK **não está no repositório** e não é distribuído: entra só na compilação
  (`ASIO_SDK=...` / `LUMEN_ASIO_SDK=...`). O `.gitignore` bloqueia pastas `ASIOSDK/`.
- O **logo "ASIO compatible"** aparece onde o ASIO é configurado (*Projeto → Áudio*) e em
  *Ajuda → Sobre o Lumen*, junto da frase de marca registrada.
- A documentação traz o ® no primeiro "ASIO" e a frase abaixo.

## Regras para o site, catálogos e anúncios

- Toda página do site que fale do Lumen com ASIO precisa mostrar o **logo ASIO compatible**
  (não basta no rodapé) e a frase de marca registrada.
- Não use "ASIO" no nome da empresa nem no logo do produto; "Lumen for ASIO" só em texto
  simples e com o logo ao lado.
- Não altere o logo (arquivos oficiais na pasta `Steinberg ASIO Logo Artwork` do SDK).

ASIO is a registered trademark of Steinberg Media Technologies GmbH.
