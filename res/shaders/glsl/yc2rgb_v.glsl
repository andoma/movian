attribute vec4 a_position;
attribute vec4 a_texcoord;
uniform mat4 u_modelview;

const mat4 projection = mat4(2.414213,0.000000,0.000000,0.000000,
			     0.000000,2.414213,0.000000,0.000000,
			     0.000000,0.000000,1.033898,-1.000000,
			     0.000000,0.000000,2.033898,0.000000);

varying vec2 f_tex0;
varying vec2 f_tex1;

void main()
{
  gl_Position = projection * u_modelview * vec4(a_position.xyz, 1);
  f_tex0 = vec2(a_texcoord[0], a_texcoord[1]);
  f_tex1 = vec2(a_texcoord[2], a_texcoord[3]);
}
