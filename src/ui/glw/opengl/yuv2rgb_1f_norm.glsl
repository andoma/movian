uniform sampler2D u_t0;
uniform sampler2D u_t1;
uniform sampler2D u_t2;
uniform mat4      u_colormtx;
uniform vec4      u_color;

void main()
{
  vec3 rgb;

  rgb = vec3(u_colormtx * vec4(texture2D(u_t0, gl_TexCoord[0].xy).r,
			       texture2D(u_t1, gl_TexCoord[0].xy).r,
			       texture2D(u_t2, gl_TexCoord[0].xy).r,
			       1));
  
  gl_FragColor = vec4(rgb, u_color.a);
}

