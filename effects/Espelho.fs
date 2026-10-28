/*{
  "DESCRIPTION": "Espelha a imagem na horizontal e/ou na vertical",
  "CATEGORIES": ["Geometria"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "horizontal", "LABEL": "Horizontal", "TYPE": "bool", "DEFAULT": true},
    {"NAME": "vertical", "LABEL": "Vertical", "TYPE": "bool", "DEFAULT": false},
    {"NAME": "mode", "LABEL": "Modo", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1], "LABELS": ["Inverter", "Simétrico (metade espelhada)"]}
  ]
}*/
void main() {
  vec2 uv = isf_FragNormCoord;
  if (mode == 0) {
    if (horizontal) uv.x = 1.0 - uv.x;
    if (vertical) uv.y = 1.0 - uv.y;
  } else {
    if (horizontal) uv.x = 0.5 - abs(uv.x - 0.5);
    if (vertical) uv.y = 1.0 - (0.5 - abs(uv.y - 0.5));
  }
  gl_FragColor = IMG_NORM_PIXEL(inputImage, uv);
}
