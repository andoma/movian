uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform sampler2D u_t3;
uniform sampler2D u_t4;
uniform sampler2D u_t5;
uniform mat4      u_colormtx;
uniform vec4      u_color;
uniform float     u_blend;


void main()
{
  vec3 rgb1 = vec3(u_colormtx * vec4(texture2D(u_t0, gl_TexCoord[0].xy).r,
				     texture2D(u_t1, gl_TexCoord[0].xy).r,
				     texture2D(u_t2, gl_TexCoord[0].xy).r,
				     1));
  
  vec3 rgb2 = vec3(u_colormtx * vec4(texture2D(u_t3, gl_TexCoord[0].xz).r,
				     texture2D(u_t4, gl_TexCoord[0].xz).r,
				     texture2D(u_t5, gl_TexCoord[0].xz).r,
				     1));

  gl_FragColor = vec4(mix(rgb2, rgb1, u_blend), u_color.a);
}	
