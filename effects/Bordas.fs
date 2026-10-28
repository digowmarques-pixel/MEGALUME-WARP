/*{
  "DESCRIPTION": "Desenha só os contornos da imagem (detecção de bordas)",
  "CATEGORIES": ["Estilizar"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "strength", "LABEL": "Força", "TYPE": "float", "DEFAULT": 2.0, "MIN": 0.0, "MAX": 10.0},
    {"NAME": "color", "LABEL": "Cor", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0]},
    {"NAME": "keepImage", "LABEL": "Manter a imagem", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
float lum(vec2 o) { return dot(IMG_NORM_PIXEL(inputImage, isf_FragNormCoord + o / RENDERSIZE).rgb, vec3(0.299, 0.587, 0.114)); }
void main() {
  float gx = -lum(vec2(-1, 1)) - 2.0 * lum(vec2(-1, 0)) - lum(vec2(-1, -1)) + lum(vec2(1, 1)) + 2.0 * lum(vec2(1, 0)) + lum(vec2(1, -1));
  float gy = -lum(vec2(-1, -1)) - 2.0 * lum(vec2(0, -1)) - lum(vec2(1, -1)) + lum(vec2(-1, 1)) + 2.0 * lum(vec2(0, 1)) + lum(vec2(1, 1));
  float e = clamp(length(vec2(gx, gy)) * strength, 0.0, 1.0);
  vec4 c = IMG_THIS_PIXEL(inputImage);
  // The edge colour over the (kept) image, back to straight alpha.
  float ka = c.a * keepImage;
  float a = e + ka * (1.0 - e);
  vec3 pm = color.rgb * e + c.rgb * ka * (1.0 - e);
  gl_FragColor = vec4(a > 0.0 ? pm / a : vec3(0.0), a);
}
