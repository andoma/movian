uniform vec4 u_color_offset;

varying vec4 f_col_mul;

void main()
{
  gl_FragColor = f_col_mul + u_color_offset;
}
