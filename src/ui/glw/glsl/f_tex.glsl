uniform sampler2D u_t0;

varying vec2 f_tex;
varying vec4 f_col;

void main()
{
  gl_FragColor = f_col * texture2D(u_t0, f_tex);
}
