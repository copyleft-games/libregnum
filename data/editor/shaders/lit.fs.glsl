#version 330
/* lrgeditor fallback lit shader: one two-sided directional light plus
 * ambient. Used when the engine light set's shader is unavailable. */
in vec2 fragTexCoord;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform float ambient;
out vec4 finalColor;
void main ()
{
    vec4 base = texture (texture0, fragTexCoord) * colDiffuse;
    float d = abs (dot (normalize (fragNormal), -lightDir));
    float l = ambient + (1.0 - ambient) * d;
    if (base.a < 0.01) discard;
    finalColor = vec4 (base.rgb * l, base.a);
}
