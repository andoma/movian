uniform sampler2D u_t0;
varying vec4 f_col_off;

void main()
{
  gl_FragColor = gl_Color * texture2D(u_t0, gl_TexCoord[0].st) + f_col_off;
}
