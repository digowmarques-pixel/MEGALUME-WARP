/*{
  "DESCRIPTION": "Transição: um círculo se abre a partir do centro (íris)",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "center", "LABEL": "Centro", "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0]},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.05, "MIN": 0.0, "MAX": 0.5}
  ]
}*/
void main() {
  vec2 p = (isf_FragNormCoord - center) * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0);
  // Radius that reaches the farthest corner from the centre at progress 1.
  vec2 a = vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0);
  float far = max(max(length(-center * a), length((vec2(1.0) - center) * a)),
                  max(length(vec2(-center.x, 1.0 - center.y) * a), length(vec2(1.0 - center.x, -center.y) * a)));
  float r = progress * (far + softness);
  float m = smoothstep(r - softness - 0.0001, r, length(p));
  gl_FragColor = mix(IMG_THIS_PIXEL(endImage), IMG_THIS_PIXEL(startImage), m);
}
