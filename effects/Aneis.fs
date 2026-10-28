/*{
  "DESCRIPTION": "Anéis que se expandem a partir de um ponto (pulso)",
  "CATEGORIES": ["Gerador"],
  "INPUTS": [
    {"NAME": "center", "LABEL": "Centro", "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0]},
    {"NAME": "spacing", "LABEL": "Espaçamento", "TYPE": "float", "DEFAULT": 0.12, "MIN": 0.01, "MAX": 1.0},
    {"NAME": "width", "LABEL": "Espessura", "TYPE": "float", "DEFAULT": 0.3, "MIN": 0.01, "MAX": 1.0},
    {"NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 0.15, "MIN": -2.0, "MAX": 2.0},
    {"NAME": "color", "LABEL": "Cor", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0]}
  ]
}*/
void main() {
  vec2 p = (isf_FragNormCoord - center) * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0);
  float d = length(p) - TIME * speed;
  float f = fract(d / spacing);
  float v = smoothstep(0.0, 0.05, f) * (1.0 - smoothstep(width, width + 0.05, f));
  gl_FragColor = vec4(color.rgb * v, 1.0);
}
