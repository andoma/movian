uniform sampler2D u_t0;

uniform vec4 u_color_offset;

varying vec4 f_col_mul;
varying vec2 f_tex;

void main()
{
  gl_FragColor = f_col_mul * texture2D(u_t0, f_tex) + u_color_offset;
}
