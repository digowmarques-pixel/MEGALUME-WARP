/*{
  "DESCRIPTION": "Separa os canais vermelho, verde e azul (aberração cromática / glitch)",
  "CATEGORIES": ["Estilizar"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "amount", "LABEL": "Deslocamento (px)", "TYPE": "float", "DEFAULT": 6.0, "MIN": 0.0, "MAX": 60.0},
    {"NAME": "angle", "LABEL": "Direção", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "wobble", "LABEL": "Tremer", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
void main() {
  float a = 6.2831853 * angle + wobble * sin(TIME * 13.0) * 3.0;
  vec2 d = vec2(cos(a), sin(a)) * amount * (1.0 + wobble * sin(TIME * 7.0)) / RENDERSIZE;
  vec4 r = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord + d);
  vec4 g = IMG_THIS_PIXEL(inputImage);
  vec4 b = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord - d);
  gl_FragColor = vec4(r.r, g.g, b.b, max(max(r.a, g.a), b.a));
}
