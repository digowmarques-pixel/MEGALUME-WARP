/*{
  "DESCRIPTION": "Rastro: o que se move deixa uma trilha que some aos poucos (feedback)",
  "CATEGORIES": ["Estilizar"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "decay", "LABEL": "Duração do rastro", "TYPE": "float", "DEFAULT": 0.92, "MIN": 0.0, "MAX": 0.995},
    {"NAME": "zoom", "LABEL": "Zoom do rastro", "TYPE": "float", "DEFAULT": 0.0, "MIN": -0.02, "MAX": 0.02}
  ],
  "PASSES": [
    {"TARGET": "trail", "PERSISTENT": true, "FLOAT": true},
    {}
  ]
}*/
void main() {
  if (PASSINDEX == 0) {
    vec2 uv = (isf_FragNormCoord - 0.5) * (1.0 - zoom) + 0.5;
    vec4 old = IMG_NORM_PIXEL(trail, uv) * decay;
    gl_FragColor = max(IMG_THIS_PIXEL(inputImage), old);
  } else {
    gl_FragColor = IMG_THIS_PIXEL(trail);
  }
}
