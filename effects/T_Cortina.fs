/*{
  "DESCRIPTION": "Transição: uma cortina com borda suave atravessa a imagem",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "angle", "LABEL": "Direção", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.1, "MIN": 0.0, "MAX": 0.5}
  ]
}*/
void main() {
  float a = angle * 6.2831853;
  vec2 d = vec2(cos(a), sin(a));
  float x = dot(isf_FragNormCoord - 0.5, d) / (abs(d.x) + abs(d.y)) + 0.5;
  float edge = progress * (1.0 + softness) - softness;
  float m = smoothstep(edge, edge + softness + 0.0001, x);
  gl_FragColor = mix(IMG_THIS_PIXEL(endImage), IMG_THIS_PIXEL(startImage), m);
}
