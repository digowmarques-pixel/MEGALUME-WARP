/*{
  "DESCRIPTION": "Degradê linear ou radial com três cores, que pode girar",
  "CATEGORIES": ["Gerador"],
  "INPUTS": [
    {"NAME": "mode", "LABEL": "Tipo", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1], "LABELS": ["Linear", "Radial"]},
    {"NAME": "c1", "LABEL": "Cor 1", "TYPE": "color", "DEFAULT": [0.9, 0.2, 0.3, 1.0]},
    {"NAME": "c2", "LABEL": "Cor 2", "TYPE": "color", "DEFAULT": [0.95, 0.75, 0.2, 1.0]},
    {"NAME": "c3", "LABEL": "Cor 3", "TYPE": "color", "DEFAULT": [0.2, 0.3, 0.9, 1.0]},
    {"NAME": "angle", "LABEL": "Ângulo", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "spin", "LABEL": "Girar", "TYPE": "float", "DEFAULT": 0.0, "MIN": -1.0, "MAX": 1.0},
    {"NAME": "center", "LABEL": "Centro", "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0]}
  ]
}*/
void main() {
  vec2 p = isf_FragNormCoord - center;
  float t;
  if (mode == 0) {
    float a = (angle + spin * TIME * 0.1) * 6.2831853;
    t = dot(p, vec2(cos(a), sin(a))) + 0.5;
  } else {
    t = length(p * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0)) * 1.4;
  }
  t = clamp(t, 0.0, 1.0);
  vec4 c = t < 0.5 ? mix(c1, c2, t * 2.0) : mix(c2, c3, t * 2.0 - 1.0);
  gl_FragColor = vec4(c.rgb, 1.0);
}
