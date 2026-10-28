/*{
  "DESCRIPTION": "Desfoque gaussiano (duas passadas)",
  "CATEGORIES": ["Desfoque"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "radius", "LABEL": "Raio (px)", "TYPE": "float", "DEFAULT": 8.0, "MIN": 0.0, "MAX": 64.0}
  ],
  "PASSES": [
    {"TARGET": "horizontal"},
    {}
  ]
}*/
vec4 blur(sampler2D img, vec2 dir) {
  float sigma = max(radius, 0.001) / 2.0;
  vec4 sum = vec4(0.0);
  float wsum = 0.0;
  for (int i = -12; i <= 12; ++i) {
    float x = float(i) * max(radius, 0.001) / 12.0;
    float w = exp(-0.5 * x * x / (sigma * sigma));
    sum += IMG_NORM_PIXEL(img, isf_FragNormCoord + dir * x / RENDERSIZE) * w;
    wsum += w;
  }
  return sum / wsum;
}
void main() {
  if (PASSINDEX == 0) gl_FragColor = blur(inputImage, vec2(1.0, 0.0));
  else gl_FragColor = blur(horizontal, vec2(0.0, 1.0));
}
