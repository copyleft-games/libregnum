#version 330
/* lrgeditor preview post-processing, one pass over the scene image:
 * FXAA, soft-knee bloom, a gentle highlight shoulder and a vignette.
 * The parameters come from the viewport's LrgFxaa, LrgBloom and
 * LrgVignette effects; colour grading runs after this pass through
 * LrgColorGrade. Uses raylib's default vertex shader. */
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform int fxaaOn;
uniform float fxaaEdge;
uniform float fxaaEdgeMin;
uniform int bloomOn;
uniform float bloomThreshold;
uniform float bloomKnee;
uniform float bloomIntensity;
uniform float bloomSize;
uniform vec3 bloomTint;
uniform int vignetteOn;
uniform float vignetteIntensity;
uniform float vignetteRadius;
uniform float vignetteSmoothness;
uniform vec3 vignetteColor;
out vec4 finalColor;

float luma (vec3 c)
{
    return dot (c, vec3 (0.299, 0.587, 0.114));
}

/* FXAA: blend along the local edge direction where contrast is high. */
vec3 fxaa (vec2 uv)
{
    vec2 px = 1.0 / resolution;
    vec3 rgb_nw = texture (texture0, uv + vec2 (-1.0, -1.0) * px).rgb;
    vec3 rgb_ne = texture (texture0, uv + vec2 (1.0, -1.0) * px).rgb;
    vec3 rgb_sw = texture (texture0, uv + vec2 (-1.0, 1.0) * px).rgb;
    vec3 rgb_se = texture (texture0, uv + vec2 (1.0, 1.0) * px).rgb;
    vec3 rgb_m = texture (texture0, uv).rgb;
    float l_nw = luma (rgb_nw);
    float l_ne = luma (rgb_ne);
    float l_sw = luma (rgb_sw);
    float l_se = luma (rgb_se);
    float l_m = luma (rgb_m);
    float l_min = min (l_m, min (min (l_nw, l_ne), min (l_sw, l_se)));
    float l_max = max (l_m, max (max (l_nw, l_ne), max (l_sw, l_se)));
    vec2 dir;
    float reduce;
    float rcp;
    vec3 a;
    vec3 b;
    float l_b;

    if (l_max - l_min < max (fxaaEdgeMin, l_max * fxaaEdge))
        return rgb_m;
    dir.x = -((l_nw + l_ne) - (l_sw + l_se));
    dir.y = ((l_nw + l_sw) - (l_ne + l_se));
    reduce = max ((l_nw + l_ne + l_sw + l_se) * 0.03125, 1.0 / 128.0);
    rcp = 1.0 / (min (abs (dir.x), abs (dir.y)) + reduce);
    dir = clamp (dir * rcp, vec2 (-8.0), vec2 (8.0)) * px;
    a = 0.5 * (texture (texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
               texture (texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    b = a * 0.5 + 0.25 * (texture (texture0, uv - dir * 0.5).rgb +
                          texture (texture0, uv + dir * 0.5).rgb);
    l_b = luma (b);
    return (l_b < l_min || l_b > l_max) ? a : b;
}

/* The part of a colour above the bloom threshold, with a soft knee. */
vec3 bright (vec3 c)
{
    float l = max (c.r, max (c.g, c.b));
    float knee = max (bloomThreshold * bloomKnee, 0.0001);
    float soft = clamp (l - bloomThreshold + knee, 0.0, 2.0 * knee);
    float w;

    soft = soft * soft / (4.0 * knee);
    w = max (soft, l - bloomThreshold) / max (l, 0.0001);
    return c * w;
}

/* Three rings of eight taps, weighted towards the centre. */
vec3 bloom (vec2 uv)
{
    vec2 px = bloomSize / resolution;
    vec3 sum = vec3 (0.0);
    float total = 0.0;
    int ring;
    int i;

    for (ring = 1; ring <= 3; ring++)
    {
        for (i = 0; i < 8; i++)
        {
            float a = 6.2831853 * (float (i) + 0.5 * float (ring)) / 8.0;
            float w = 1.0 / float (ring);

            sum += bright (texture (texture0, uv + vec2 (cos (a), sin (a)) * px * float (ring)).rgb) * w;
            total += w;
        }
    }
    return sum / total;
}

void main ()
{
    vec2 uv = fragTexCoord;
    vec3 color = (fxaaOn != 0) ? fxaa (uv) : texture (texture0, uv).rgb;

    if (bloomOn != 0)
    {
        color += bloom (uv) * bloomIntensity * bloomTint;
        /* Shoulder: values above 0.8 roll off instead of clipping */
        color = mix (color, 0.8 + 0.2 * (1.0 - exp (-(color - 0.8) * 5.0)), step (0.8, color));
    }
    if (vignetteOn != 0)
    {
        vec2 d = uv - 0.5;
        float dist;
        float v;

        d.x *= resolution.x / max (resolution.y, 1.0);
        dist = length (d) * 1.41421;
        v = smoothstep (vignetteRadius, vignetteRadius + max (vignetteSmoothness, 0.001), dist) * vignetteIntensity;
        color = mix (color, vignetteColor, v);
    }
    finalColor = vec4 (color, 1.0);
}
