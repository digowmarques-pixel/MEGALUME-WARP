/*{
  "DESCRIPTION": "Glow: as partes claras ganham um halo",
  "CATEGORIES": ["Desfoque"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "threshold", "LABEL": "A partir de", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "radius", "LABEL": "Raio (px)", "TYPE": "float", "DEFAULT": 16.0, "MIN": 1.0, "MAX": 64.0},
    {"NAME": "intensity", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0}
  ],
  "PASSES": [
    {"TARGET": "brightH", "WIDTH": "floor($WIDTH / 2.0)", "HEIGHT": "floor($HEIGHT / 2.0)"},
    {"TARGET": "brightV", "WIDTH": "floor($WIDTH / 2.0)", "HEIGHT": "floor($HEIGHT / 2.0)"},
    {}
  ]
}*/
vec4 blur(sampler2D img, vec2 dir, bool bright) {
  float sigma = radius / 4.0;
  vec4 sum = vec4(0.0);
  float wsum = 0.0;
  for (int i = -10; i <= 10; ++i) {
    float x = float(i) * radius / 20.0;
    float w = exp(-0.5 * x * x / (sigma * sigma));
    vec4 c = IMG_NORM_PIXEL(img, isf_FragNormCoord + dir * x / RENDERSIZE);
    if (bright) c.rgb *= smoothstep(threshold, threshold + 0.1, dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)));
    sum += c * w;
    wsum += w;
  }
  return sum / wsum;
}
void main() {
  if (PASSINDEX == 0) gl_FragColor = blur(inputImage, vec2(1.0, 0.0), true);
  else if (PASSINDEX == 1) gl_FragColor = blur(brightH, vec2(0.0, 1.0), false);
  else {
    vec4 c = IMG_THIS_PIXEL(inputImage);
    vec4 g = IMG_NORM_PIXEL(brightV, isf_FragNormCoord);
    gl_FragColor = vec4(c.rgb + g.rgb * intensity, max(c.a, g.a));
  }
}
