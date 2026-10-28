/*{
  "DESCRIPTION": "Recorta as bordas da imagem com transição suave",
  "CATEGORIES": ["Geometria"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "left", "LABEL": "Esquerda", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "right", "LABEL": "Direita", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "top", "LABEL": "Topo", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "bottom", "LABEL": "Base", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.05, "MIN": 0.0, "MAX": 0.5}
  ]
}*/
void main() {
  vec2 uv = isf_FragNormCoord;
  float s = max(softness, 0.0001);
  float a = smoothstep(left, left + s, uv.x) * smoothstep(right, right + s, 1.0 - uv.x) *
            smoothstep(bottom, bottom + s, uv.y) * smoothstep(top, top + s, 1.0 - uv.y);
  gl_FragColor = IMG_THIS_PIXEL(inputImage) * a;
}
