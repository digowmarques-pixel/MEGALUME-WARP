/*{
  "DESCRIPTION": "Caleidoscópio de N fatias girando em torno do centro",
  "CATEGORIES": ["Geometria"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "slices", "LABEL": "Fatias", "TYPE": "float", "DEFAULT": 6.0, "MIN": 2.0, "MAX": 24.0},
    {"NAME": "angle", "LABEL": "Ângulo", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0},
    {"NAME": "spin", "LABEL": "Girar (voltas/min)", "TYPE": "float", "DEFAULT": 0.0, "MIN": -30.0, "MAX": 30.0},
    {"NAME": "center", "LABEL": "Centro", "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0]}
  ]
}*/
void main() {
  vec2 aspect = vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0);
  vec2 p = (isf_FragNormCoord - center) * aspect;
  float r = length(p);
  float a = atan(p.y, p.x) + 6.2831853 * (angle + spin * TIME / 60.0);
  float seg = 6.2831853 / floor(slices);
  a = mod(a, seg);
  a = abs(a - seg * 0.5);
  vec2 q = vec2(cos(a), sin(a)) * r / aspect + center;
  gl_FragColor = IMG_NORM_PIXEL(inputImage, clamp(q, 0.0, 1.0));
}
