#ifdef GL_ES
precision highp float;
#endif

varying vec4 f_col_mul;
varying vec4 f_col_mul2;
varying vec4 f_col_off;

void main()
{
  gl_FragColor = clamp(f_col_mul, 0.0, 1.0) * f_col_mul2 + f_col_off;
}
