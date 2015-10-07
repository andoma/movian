#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform vec4      u_color;
uniform float     u_blend;

varying vec2 f_tex0, f_tex1;

void main()
{
  vec3 rgb1 = texture2D(u_t0, f_tex0).rgb;
  vec3 rgb2 = texture2D(u_t1, f_tex1).rgb;

  gl_FragColor = vec4(mix(rgb2, rgb1, u_blend), u_color.a);
}
