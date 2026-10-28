/*{
  "DESCRIPTION": "Transição: a imagem vira blocos grandes e volta já trocada",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "maxSize", "LABEL": "Bloco máximo (px)", "TYPE": "float", "DEFAULT": 60.0, "MIN": 4.0, "MAX": 300.0}
  ]
}*/
void main() {
  float k = 1.0 - abs(progress * 2.0 - 1.0); // 0 -> 1 -> 0
  vec2 cell = vec2(max(1.0, maxSize * k * k)) / RENDERSIZE;
  vec2 uv = (floor(isf_FragNormCoord / cell) + 0.5) * cell;
  gl_FragColor = progress < 0.5 ? IMG_NORM_PIXEL(startImage, uv) : IMG_NORM_PIXEL(endImage, uv);
}
