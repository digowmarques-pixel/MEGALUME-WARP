/*{
  "DESCRIPTION": "Pinta as sombras com uma cor e as luzes com outra",
  "CATEGORIES": ["Cor"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "dark", "LABEL": "Sombras", "TYPE": "color", "DEFAULT": [0.1, 0.0, 0.35, 1.0]},
    {"NAME": "light", "LABEL": "Luzes", "TYPE": "color", "DEFAULT": [1.0, 0.75, 0.2, 1.0]},
    {"NAME": "amount", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0}
  ]
}*/
void main() {
  vec4 c = IMG_THIS_PIXEL(inputImage);
  float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
  vec3 d = mix(dark.rgb, light.rgb, l);
  gl_FragColor = vec4(mix(c.rgb, d, amount), c.a);
}
