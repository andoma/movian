attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texcoord;

uniform vec4 u_color;
uniform vec4 u_color_offset;
uniform mat4 u_modelview;
uniform float u_blur_amount;


const mat4 projection = mat4(2.414213,0.000000,0.000000,0.000000,
			     0.000000,2.414213,0.000000,0.000000,
			     0.000000,0.000000,1.033898,-1.000000,
			     0.000000,0.000000,2.033898,0.000000);

// The ordering of these are important to match the varying variables
// in the fragment shaders.
varying vec4 f_col_mul;
varying vec4 f_col_mul2;
varying vec4 f_col_off;
varying vec2 f_tex;
varying float f_blur_amount;

void main()
{
  gl_Position = projection * u_modelview * vec4(a_position.xyz, 1);
  f_col_mul = a_color;
  f_col_off = u_color_offset;
  f_col_mul2 =  clamp(u_color, 0.0, 1.0);
  f_tex = a_texcoord;
  f_blur_amount = u_blur_amount + (1.0 - a_position.w);
}
