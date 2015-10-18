attribute vec4 a_position;
attribute vec4 a_color;
attribute vec4 a_texcoord;

uniform vec4 u_color;
uniform vec4 u_color_offset;
uniform mat4 u_modelview;
uniform vec3 u_blur;


const mat4 projection = mat4(2.414213,0.000000,0.000000,0.000000,
			     0.000000,2.414213,0.000000,0.000000,
			     0.000000,0.000000,1.033898,-1.000000,
			     0.000000,0.000000,2.033898,0.000000);

// The ordering of these are important to match the varying variables
// in the fragment shaders.
varying vec4 f_col_mul;
varying vec4 f_col_off;
varying vec4 f_tex;
varying vec3 f_blur;

void main()
{
  gl_Position = projection * u_modelview * vec4(a_position.xyz, 1);
  f_col_off = u_color_offset;
  f_col_mul =  a_color * u_color;
  f_tex = a_texcoord;
  f_blur = vec3(u_blur.x + (1.0 - a_position.w), u_blur.yz);
}
