uniform vec4 u_color;
uniform mat4 u_modelview;

attribute vec3 a_position;
attribute vec4 a_color;
attribute vec2 a_texcoord;

const mat4 projection = mat4(2.414213,0.000000,0.000000,0.000000,
			     0.000000,2.414213,0.000000,0.000000,
			     0.000000,0.000000,1.033898,-1.000000,
			     0.000000,0.000000,2.033898,0.000000);

varying vec2 f_tex;
varying vec4 f_col;

void main()
{
  gl_Position = projection * u_modelview * vec4(a_position, 1);
  f_tex = vec4(a_texcoord, 0, 1);
  f_col = a_color * u_color;
}
