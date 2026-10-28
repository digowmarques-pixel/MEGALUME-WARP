/*{
  "DESCRIPTION": "Nuvens de ruído animadas (fumaça, névoa, textura orgânica)",
  "CATEGORIES": ["Gerador"],
  "INPUTS": [
    {"NAME": "scale", "LABEL": "Escala", "TYPE": "float", "DEFAULT": 3.0, "MIN": 0.5, "MAX": 20.0},
    {"NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 0.2, "MIN": 0.0, "MAX": 3.0},
    {"NAME": "color1", "LABEL": "Cor 1", "TYPE": "color", "DEFAULT": [0.02, 0.02, 0.08, 1.0]},
    {"NAME": "color2", "LABEL": "Cor 2", "TYPE": "color", "DEFAULT": [0.3, 0.6, 1.0, 1.0]}
  ]
}*/
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.03; a *= 0.5; }
  return v;
}
void main() {
  vec2 p = isf_FragNormCoord * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0) * scale;
  float t = TIME * speed;
  float n = fbm(p + vec2(fbm(p + t), fbm(p - t)));
  gl_FragColor = mix(color1, color2, smoothstep(0.2, 0.9, n));
}
