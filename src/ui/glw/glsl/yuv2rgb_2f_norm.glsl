uniform sampler2D yA;
uniform sampler2D uA;
uniform sampler2D vA;
uniform sampler2D yB;
uniform sampler2D uB;
uniform sampler2D vB;
uniform mat3      colormtx;
uniform float     alpha;
uniform float     blend;

void main()
{
  vec3 yuvA;
  vec3 yuvB;

  yuvA = vec3(texture2D(yA, gl_TexCoord[0].st).r - 0.0625,
	      texture2D(vA, gl_TexCoord[0].st).r - 0.5,
	      texture2D(uA, gl_TexCoord[0].st).r - 0.5);

  yuvB = vec3(texture2D(yB, gl_TexCoord[1].st).r - 0.0625,
	      texture2D(vB, gl_TexCoord[1].st).r - 0.5,
	      texture2D(uB, gl_TexCoord[1].st).r - 0.5);

  vec3 rgbA;
  vec3 rgbB;
  
  rgbA = colormtx * yuvA;
  rgbB = colormtx * yuvB;
  
  vec3 rgb = rgbA * blend + rgbB * (1.0 - blend);

  gl_FragColor = vec4(rgb, alpha);
}	
