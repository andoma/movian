uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform mat3      u_colormtx;
uniform vec4      u_color;

varying vec2 f_tex0;

void main()
{
  vec3 yuv;

  yuv = vec3(texture2D(u_t0, f_tex0).r - 0.0625,
	     texture2D(u_t2, f_tex0).r - 0.5,
	     texture2D(u_t1, f_tex0).r - 0.5);
  
  gl_FragColor = vec4(u_colormtx * yuv, u_color.a);
}

