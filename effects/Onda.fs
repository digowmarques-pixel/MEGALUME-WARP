/*{
  "DESCRIPTION": "Ondula a imagem (água, bandeira)",
  "CATEGORIES": ["Geometria"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "amplitude", "LABEL": "Amplitude", "TYPE": "float", "DEFAULT": 0.02, "MIN": 0.0, "MAX": 0.2},
    {"NAME": "frequency", "LABEL": "Frequência", "TYPE": "float", "DEFAULT": 6.0, "MIN": 0.0, "MAX": 40.0},
    {"NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 1.0, "MIN": -10.0, "MAX": 10.0},
    {"NAME": "vertical", "LABEL": "Ondas verticais", "TYPE": "bool", "DEFAULT": false}
  ]
}*/
void main() {
  vec2 uv = isf_FragNormCoord;
  float t = TIME * speed;
  if (vertical) uv.y += sin(uv.x * frequency * 6.2831853 + t) * amplitude;
  else uv.x += sin(uv.y * frequency * 6.2831853 + t) * amplitude;
  gl_FragColor = IMG_NORM_PIXEL(inputImage, uv);
}
