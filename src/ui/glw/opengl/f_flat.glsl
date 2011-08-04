varying vec4 f_col_off;

void main()
{
  gl_FragColor = gl_Color + f_col_off;
}
