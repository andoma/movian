#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform mat4      u_colormtx;
uniform vec4      u_color;

varying vec2 f_tex0;

void main()
{
  vec3 rgb;

  rgb = vec3(u_colormtx * vec4(texture2D(u_t0, f_tex0).r,
			       texture2D(u_t1, f_tex0).r,
			       texture2D(u_t2, f_tex0).r,
			       1));
  
  gl_FragColor = vec4(rgb, u_color.a);
}

