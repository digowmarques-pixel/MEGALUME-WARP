/*{
  "DESCRIPTION": "Uma faixa de luz que varre a superfície (clássico de videomapping)",
  "CATEGORIES": ["Gerador"],
  "INPUTS": [
    {"NAME": "angle", "LABEL": "Direção", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "width", "LABEL": "Largura", "TYPE": "float", "DEFAULT": 0.08, "MIN": 0.005, "MAX": 1.0},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.05, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "speed", "LABEL": "Velocidade (passadas/s)", "TYPE": "float", "DEFAULT": 0.25, "MIN": -4.0, "MAX": 4.0},
    {"NAME": "position", "LABEL": "Posição (com velocidade 0)", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "color", "LABEL": "Cor", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0]}
  ]
}*/
void main() {
  float a = angle * 6.2831853;
  vec2 dir = vec2(cos(a), sin(a));
  vec2 p = isf_FragNormCoord - 0.5;
  float x = dot(p, dir) / (abs(dir.x) + abs(dir.y)) + 0.5;           // 0..1 across the surface
  float pos = speed != 0.0 ? fract(TIME * speed) * (1.0 + 2.0 * width) - width : position;
  float d = abs(x - pos);
  float v = 1.0 - smoothstep(width * 0.5, width * 0.5 + softness + 0.0001, d);
  gl_FragColor = vec4(color.rgb * v, 1.0);
}
