#ifdef GL_ES
precision highp float;
#endif

varying vec4 f_col_mul;
varying vec4 f_col_off;

void main()
{
  gl_FragColor = f_col_mul + f_col_off;
}
