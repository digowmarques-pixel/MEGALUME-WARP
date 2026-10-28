/*{
  "DESCRIPTION": "Recorta uma cor (fundo verde ou azul)",
  "CATEGORIES": ["Cor"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "keyColor", "LABEL": "Cor", "TYPE": "color", "DEFAULT": [0.0, 1.0, 0.0, 1.0]},
    {"NAME": "sensitivity", "LABEL": "Sensibilidade", "TYPE": "float", "DEFAULT": 0.3, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "smoothing", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.1, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "spill", "LABEL": "Remover reflexo", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
vec2 chroma(vec3 c) { // YCbCr chroma: robust to lighting changes
  return vec2(-0.1687 * c.r - 0.3313 * c.g + 0.5 * c.b, 0.5 * c.r - 0.4187 * c.g - 0.0813 * c.b);
}
void main() {
  vec4 c = IMG_THIS_PIXEL(inputImage);
  float d = distance(chroma(c.rgb), chroma(keyColor.rgb));
  float a = smoothstep(sensitivity * 0.5, sensitivity * 0.5 + smoothing, d);
  vec3 rgb = c.rgb;
  // Spill: pull the key channel down towards the others.
  float gray = dot(rgb, vec3(0.3333));
  rgb = mix(rgb, mix(rgb, vec3(gray), keyColor.rgb), spill * (1.0 - a));
  gl_FragColor = vec4(rgb * a, c.a * a);
}
