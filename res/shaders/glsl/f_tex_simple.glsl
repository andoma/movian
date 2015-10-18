#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;

varying vec4 f_col_mul;
varying vec4 f_tex;

void main()
{
  gl_FragColor = texture2D(u_t0, f_tex.xy) * f_col_mul2;
}
