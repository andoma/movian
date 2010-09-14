uniform sampler2D y;
uniform sampler2D u;
uniform sampler2D v;
uniform mat3      colormtx;
uniform float     alpha;

void main()
{
  vec3 yuv;

  yuv = vec3(texture2D(y, gl_TexCoord[0].st).r - 0.0625,
	     texture2D(v, gl_TexCoord[0].st).r - 0.5,
	     texture2D(u, gl_TexCoord[0].st).r - 0.5);
  
  gl_FragColor = vec4(colormtx * yuv, alpha);
}	
