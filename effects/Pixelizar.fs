/*{
  "DESCRIPTION": "Blocos grandes de cor (mosaico)",
  "CATEGORIES": ["Estilizar"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "size", "LABEL": "Tamanho do bloco (px)", "TYPE": "float", "DEFAULT": 24.0, "MIN": 1.0, "MAX": 200.0}
  ]
}*/
void main() {
  vec2 cell = vec2(max(size, 1.0)) / RENDERSIZE;
  vec2 uv = (floor(isf_FragNormCoord / cell) + 0.5) * cell;
  gl_FragColor = IMG_NORM_PIXEL(inputImage, uv);
}
