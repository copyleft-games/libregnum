#version 330
/* lrgeditor scene vertex shader (lit, unlit and normals modes).
 * raylib attribute and uniform names, so DrawMesh() feeds it. */
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec3 fragNormal;
void main ()
{
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize (vec3 (matNormal * vec4 (vertexNormal, 0.0)));
    gl_Position = mvp * vec4 (vertexPosition, 1.0);
}
