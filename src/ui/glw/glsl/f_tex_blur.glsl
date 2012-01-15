#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;

varying vec4 f_col_mul;
varying vec4 f_col_off;
varying vec2 f_tex;
varying vec2 f_blur_amount;

void main()
{
  vec4 col = 
    texture2D(u_t0, f_tex + vec2(f_blur_amount.x, 0)) +
    texture2D(u_t0, f_tex - vec2(f_blur_amount.x, 0)) +
    texture2D(u_t0, f_tex + vec2(0, f_blur_amount.y)) +
    texture2D(u_t0, f_tex - vec2(0, f_blur_amount.y)) +
    texture2D(u_t0, f_tex + vec2(f_blur_amount.x, f_blur_amount.y)) +
    texture2D(u_t0, f_tex - vec2(f_blur_amount.x, f_blur_amount.y)) +
    texture2D(u_t0, f_tex + vec2(-f_blur_amount.x, f_blur_amount.y)) +
    texture2D(u_t0, f_tex - vec2(-f_blur_amount.x, f_blur_amount.y));

  col = col * 0.12;

  gl_FragColor = f_col_mul * col + f_col_off;
}
