/*{
  "DESCRIPTION": "Níveis: ponto preto, ponto branco e gamma (como no Photoshop)",
  "CATEGORIES": ["Cor"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "inBlack", "LABEL": "Entrada preto", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "inWhite", "LABEL": "Entrada branco", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "gamma", "LABEL": "Gamma", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.1, "MAX": 4.0},
    {"NAME": "outBlack", "LABEL": "Saída preto", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "outWhite", "LABEL": "Saída branco", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
void main() {
  vec4 c = IMG_THIS_PIXEL(inputImage);
  vec3 rgb = c.rgb; // straight alpha in and out
  rgb = clamp((rgb - inBlack) / max(inWhite - inBlack, 0.001), 0.0, 1.0);
  rgb = pow(rgb, vec3(1.0 / gamma));
  rgb = mix(vec3(outBlack), vec3(outWhite), rgb);
  gl_FragColor = vec4(rgb, c.a);
}
