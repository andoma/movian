uniform sampler2D u_t0;

varying vec4 f_col;
varying vec2 f_tex;

void main()
{
  gl_FragColor = f_col * texture2D(u_t0, f_tex);
}
