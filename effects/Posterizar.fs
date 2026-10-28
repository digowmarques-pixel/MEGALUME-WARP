/*{
  "DESCRIPTION": "Reduz o número de tons de cada cor",
  "CATEGORIES": ["Cor"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "levels", "LABEL": "Níveis", "TYPE": "float", "DEFAULT": 4.0, "MIN": 2.0, "MAX": 16.0}
  ]
}*/
void main() {
  vec4 c = IMG_THIS_PIXEL(inputImage);
  float n = floor(levels);
  gl_FragColor = vec4(floor(c.rgb * n + 0.5) / n, c.a);
}
