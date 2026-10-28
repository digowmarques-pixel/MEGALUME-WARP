/*{
  "DESCRIPTION": "Transição: a imagem se desfaz em manchas de ruído",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "scale", "LABEL": "Tamanho das manchas", "TYPE": "float", "DEFAULT": 8.0, "MIN": 1.0, "MAX": 60.0},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.15, "MIN": 0.0, "MAX": 0.5}
  ]
}*/
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
void main() {
  vec2 p = isf_FragNormCoord * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0) * scale;
  float n = 0.6 * noise(p) + 0.3 * noise(p * 2.1) + 0.1 * noise(p * 4.3);
  float e = progress * (1.0 + softness);
  float m = smoothstep(e - softness - 0.0001, e, n);
  gl_FragColor = mix(IMG_THIS_PIXEL(endImage), IMG_THIS_PIXEL(startImage), m);
}
