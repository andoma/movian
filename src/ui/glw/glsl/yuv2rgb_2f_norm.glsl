uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform sampler2D u_t3;
uniform sampler2D u_t4;
uniform sampler2D u_t5;
uniform mat3      u_colormtx;
uniform vec4      u_color;
uniform float     u_blend;

varying vec2 f_tex0, f_tex1;

void main()
{
  vec3 yuvA;
  vec3 yuvB;

  yuvA = vec3(texture2D(u_t0, f_tex0).r - 0.0625,
	      texture2D(u_t2, f_tex0).r - 0.5,
	      texture2D(u_t1, f_tex0).r - 0.5);

  yuvB = vec3(texture2D(u_t3, f_tex1).r - 0.0625,
	      texture2D(u_t5, f_tex1).r - 0.5,
	      texture2D(u_t4, f_tex1).r - 0.5);

  vec3 rgbA;
  vec3 rgbB;
  
  rgbA = u_colormtx * yuvA;
  rgbB = u_colormtx * yuvB;
  
  vec3 rgb = rgbA * u_blend + rgbB * (1.0 - u_blend);

  gl_FragColor = vec4(rgb, u_color.a);
}	
