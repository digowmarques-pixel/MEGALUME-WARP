/*{
  "DESCRIPTION": "Limiar: tudo acima do valor fica branco, o resto preto (bom para máscaras e silhuetas)",
  "CATEGORIES": ["Cor"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "threshold", "LABEL": "Limiar", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "softness", "LABEL": "Suavidade", "TYPE": "float", "DEFAULT": 0.02, "MIN": 0.0, "MAX": 0.5},
    {"NAME": "invert", "LABEL": "Inverter", "TYPE": "bool", "DEFAULT": false}
  ]
}*/
void main() {
  vec4 c = IMG_THIS_PIXEL(inputImage);
  float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
  float s = max(softness, 0.001);
  float v = smoothstep(threshold - s, threshold + s, l);
  if (invert) v = 1.0 - v;
  gl_FragColor = vec4(vec3(v) * c.a, c.a);
}
