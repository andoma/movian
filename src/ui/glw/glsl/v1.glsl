uniform vec4 ucolor;
attribute vec3 position;
attribute vec4 color;
attribute vec2 texcoord;

void main()
{
  gl_Position = gl_ModelViewProjectionMatrix * vec4(position, 1);
  gl_TexCoord[0] = vec4(texcoord, 0, 1);
  gl_FrontColor = color * ucolor;
}
