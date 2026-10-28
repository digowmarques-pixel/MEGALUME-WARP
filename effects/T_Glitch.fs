/*{
  "DESCRIPTION": "Transição: faixas deslocadas e cores separadas, como um sinal com defeito",
  "CATEGORIES": ["Transição"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "amount", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 0.08, "MIN": 0.0, "MAX": 0.3}
  ]
}*/
float hash(float x) { return fract(sin(x * 91.3458) * 47453.5453); }
void main() {
  float k = sin(progress * 3.14159265); // strongest in the middle
  float band = floor(isf_FragNormCoord.y * 24.0);
  float jitter = (hash(band + floor(TIME * 30.0)) - 0.5) * amount * k * step(0.5, hash(band * 1.7 + floor(TIME * 12.0)));
  vec2 uv = isf_FragNormCoord + vec2(jitter, 0.0);
  float sel = step(hash(band * 3.1 + 7.0) * 0.6 + 0.2, progress);
  vec2 off = vec2(amount * 0.2 * k, 0.0);
  vec4 a = vec4(IMG_NORM_PIXEL(startImage, uv + off).r, IMG_NORM_PIXEL(startImage, uv).g, IMG_NORM_PIXEL(startImage, uv - off).b, IMG_NORM_PIXEL(startImage, uv).a);
  vec4 b = vec4(IMG_NORM_PIXEL(endImage, uv + off).r, IMG_NORM_PIXEL(endImage, uv).g, IMG_NORM_PIXEL(endImage, uv - off).b, IMG_NORM_PIXEL(endImage, uv).a);
  gl_FragColor = mix(a, b, progress < 0.02 ? 0.0 : progress > 0.98 ? 1.0 : sel);
}
