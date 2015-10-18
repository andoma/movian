#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D u_t0;

varying vec4 f_col_mul;
varying vec4 f_col_off;
varying vec4 f_tex;
varying vec3 f_blur;

void main()
{
  vec2 t = clamp(f_blur.x, 0.0, 1.0) * f_blur.yz;

  vec4 col =
    texture2D(u_t0, f_tex.xy + vec2(t.x, 0)) +
    texture2D(u_t0, f_tex.xy - vec2(t.x, 0)) +
    texture2D(u_t0, f_tex.xy + vec2(0,  t.y)) +
    texture2D(u_t0, f_tex.xy - vec2(0,  t.y)) +
    texture2D(u_t0, f_tex.xy + vec2(t.x, t.y)) +
    texture2D(u_t0, f_tex.xy - vec2(t.x, t.y)) +
    texture2D(u_t0, f_tex.xy + vec2(-t.x, t.y)) +
    texture2D(u_t0, f_tex.xy - vec2(-t.x, t.y));

  col = col * 0.125;

  gl_FragColor = f_col_mul * (col + f_col_off);
}
