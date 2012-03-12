#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;
uniform vec2 u_texture_blur_scale;

varying vec4 f_col_mul;
varying vec4 f_col_off;
varying vec2 f_tex;
varying float f_blur_amount;

void main()
{
  vec2 t = clamp(f_blur_amount, 0.0, 1.0) * u_texture_blur_scale;
  
  vec4 col = 
    texture2D(u_t0, f_tex + vec2(t.x, 0)) +
    texture2D(u_t0, f_tex - vec2(t.x, 0)) +
    texture2D(u_t0, f_tex + vec2(0,  t.y)) +
    texture2D(u_t0, f_tex - vec2(0,  t.y)) +
    texture2D(u_t0, f_tex + vec2(t.x, t.y)) +
    texture2D(u_t0, f_tex - vec2(t.x, t.y)) +
    texture2D(u_t0, f_tex + vec2(-t.x, t.y)) +
    texture2D(u_t0, f_tex - vec2(-t.x, t.y));
  
  col = col * 0.125;

  gl_FragColor = f_col_mul * col + f_col_off;
}
