/*{
  "DESCRIPTION": "Transição: a imagem antiga cresce e some, a nova chega com desfoque de zoom",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "strength", "LABEL": "Força", "TYPE": "float", "DEFAULT": 0.4, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
vec4 zoomBlur(sampler2D img, float amount) {
  vec4 sum = vec4(0.0);
  for (int i = 0; i < 12; ++i) {
    float s = 1.0 - amount * float(i) / 12.0;
    sum += IMG_NORM_PIXEL(img, (isf_FragNormCoord - 0.5) * s + 0.5);
  }
  return sum / 12.0;
}
void main() {
  float a = smoothstep(0.0, 1.0, progress);
  vec4 from = zoomBlur(startImage, a * strength);
  vec4 to = zoomBlur(endImage, (1.0 - a) * strength);
  gl_FragColor = mix(from, to, a);
}
