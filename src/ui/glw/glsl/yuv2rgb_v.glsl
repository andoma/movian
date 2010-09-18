attribute vec2 position;
attribute vec3 texcoord;

void main()
{
  gl_Position = gl_ModelViewProjectionMatrix * vec4(position, 0, 1);
  gl_TexCoord[0] = vec4(texcoord[0], texcoord[1], 0, 1);
  gl_TexCoord[1] = vec4(texcoord[0], texcoord[2], 0, 1);
}
