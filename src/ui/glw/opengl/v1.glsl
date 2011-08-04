uniform vec4 u_color;
uniform vec4 u_color_offset;
uniform vec2 u_blur_amount;

varying vec4 f_col_mul;
varying vec4 f_col_off;
varying vec2 f_blur_amount;

void main()
{
  gl_Position = ftransform();
  gl_TexCoord[0] = gl_MultiTexCoord0;
  gl_FrontColor = gl_Color * clamp(u_color, 0.0, 1.0);
  f_col_off = u_color_offset;
  f_blur_amount = u_blur_amount;
}
