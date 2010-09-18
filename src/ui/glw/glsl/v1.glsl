uniform vec4 ucolor;
attribute vec3 position;
attribute vec4 color;
attribute vec2 texcoord;
uniform mat4 modelview;

const mat4 projection = mat4(2.414213,0.000000,0.000000,0.000000,
			     0.000000,2.414213,0.000000,0.000000,
			     0.000000,0.000000,1.033898,-1.000000,
			     0.000000,0.000000,2.033898,0.000000);

void main()
{
  gl_Position = projection * modelview * vec4(position, 1);
  gl_TexCoord[0] = vec4(texcoord, 0, 1);
  gl_FrontColor = color * ucolor;
}
