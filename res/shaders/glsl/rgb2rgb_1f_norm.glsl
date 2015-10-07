#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;
uniform vec4      u_color;

varying vec2 f_tex0;

void main()
{
  gl_FragColor = vec4(texture2D(u_t0, f_tex0).rgb, u_color.a);
}

