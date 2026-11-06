#version 330
/* lrgeditor normals mode: world-space normals as colours
 * (x -> red, y -> green, z -> blue, remapped from -1..1 to 0..1). */
in vec2 fragTexCoord;
in vec3 fragNormal;
out vec4 finalColor;
void main ()
{
    vec3 n = normalize (fragNormal);
    finalColor = vec4 (n * 0.5 + 0.5, 1.0);
}
