#version 330
/* lrgeditor unlit mode: flat albedo (texture times material colour). */
in vec2 fragTexCoord;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main ()
{
    vec4 base = texture (texture0, fragTexCoord) * colDiffuse;
    if (base.a < 0.01) discard;
    finalColor = base;
}
