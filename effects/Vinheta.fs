/*{
  "DESCRIPTION": "Escurece as bordas",
  "CATEGORIES": ["Estilizar"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "radius", "LABEL": "Raio", "TYPE": "float", "DEFAULT": 0.75, "MIN": 0.0, "MAX": 1.5},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.45, "MIN": 0.01, "MAX": 1.0}
  ]
}*/
void main() {
  vec2 p = (isf_FragNormCoord - 0.5) * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0);
  float v = 1.0 - smoothstep(radius - softness, radius, length(p));
  gl_FragColor = IMG_THIS_PIXEL(inputImage) * vec4(vec3(v), 1.0);
}
