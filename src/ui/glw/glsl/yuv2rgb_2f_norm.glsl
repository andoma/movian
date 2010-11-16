uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform sampler2D u_t3;
uniform sampler2D u_t4;
uniform sampler2D u_t5;
uniform mat4      u_colormtx;
uniform vec4      u_color;
uniform float     u_blend;

varying vec2 f_tex0, f_tex1;

void main()
{
  vec4 rgbA = u_colormtx * vec4(texture2D(u_t0, f_tex0).r,
				texture2D(u_t2, f_tex0).r,
				texture2D(u_t1, f_tex0).r,
				u_color.a);
  
  vec4 rgbB = u_colormtx * vec4(texture2D(u_t3, f_tex1).r,
				texture2D(u_t5, f_tex1).r,
				texture2D(u_t4, f_tex1).r,
				u_color.a);

  gl_FragColor = mix(rgbB, rgbA, u_blend);
}	
