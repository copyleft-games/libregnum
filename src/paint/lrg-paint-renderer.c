/* lrg-paint-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draws paint lists with one instanced SDF shader.
 *
 * Every instance is expanded by the vertex shader into a quad that
 * covers the shape plus an anti-aliasing margin (and the blur for
 * shadows), clamped to its clip rectangle. The fragment shader
 * evaluates the shape's signed distance and turns it into coverage, so
 * edges are anti-aliased analytically at any scale with no MSAA and no
 * tessellation. Output is premultiplied alpha.
 *
 * Blur and glass instances read what was drawn behind them. Before a
 * BACKDROP batch the renderer copies the current target (screen or
 * render texture) into an offscreen texture and builds a dual-filter
 * ("dual Kawase") blur chain from it; the batch then samples both.
 * Frames without such instances never touch the offscreen path.
 *
 * The rounded-box distance is the standard construction (see Inigo
 * Quilez's 2D SDF notes); the instance-per-shape design, the glass
 * model (frost, rim refraction, dispersion, rim light) and the effect
 * snippet contract follow the Raven UI library (Apache-2.0),
 * reimplemented here for rlgl and GLSL 330.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-paint-renderer.h"
#include "lrg-paint-private.h"
#include "../lrg-log.h"

/* Initial and minimum instance capacity of the GPU buffer */
#define MIN_CAPACITY (1024)

/* Attribute locations, matching the layout() qualifiers below */
#define ATTR_CORNER (0)
#define ATTR_RECT   (1)
#define ATTR_RADII  (2)
#define ATTR_CLIP   (3)
#define ATTR_PARAMS (4)
#define ATTR_EXTRA  (5)
#define ATTR_DATA   (6)
#define ATTR_COLOR  (7)
#define ATTR_COLOR2 (8)
#define ATTR_BORDER (9)

/* Backdrop blur chain: at most this many half-size levels */
#define MAX_BLUR_LEVELS (4)

/* The backdrop copy is padded to a multiple of this, so every blur
 * level is an exact half of the one above */
#define BACKDROP_ALIGN (1 << MAX_BLUR_LEVELS)

/* GL_COLOR_BUFFER_BIT, for rlBlitFramebuffer() */
#define COLOR_BUFFER_BIT (0x00004000)

/* Pointer position sent when the list has none: far from everything */
#define NO_POINTER (-1.0e6f)

/* Texture slots of the paint shader */
#define SLOT_BACKDROP (1)
#define SLOT_BLUR     (2)

static const gchar *vertex_source =
	"#version 330\n"
	"layout(location = 0) in vec2 a_corner;\n"
	"layout(location = 1) in vec4 i_rect;\n"
	"layout(location = 2) in vec4 i_radii;\n"
	"layout(location = 3) in vec4 i_clip;\n"
	"layout(location = 4) in vec4 i_params;\n"
	"layout(location = 5) in vec4 i_extra;\n"
	"layout(location = 6) in vec4 i_data;\n"
	"layout(location = 7) in vec4 i_color;\n"
	"layout(location = 8) in vec4 i_color2;\n"
	"layout(location = 9) in vec4 i_border;\n"
	"uniform mat4 u_mvp;\n"
	"out vec2 v_pos;\n"
	"flat out vec4 v_rect;\n"
	"flat out vec4 v_radii;\n"
	"flat out vec4 v_clip;\n"
	"flat out vec4 v_params;\n"
	"flat out vec4 v_extra;\n"
	"flat out vec4 v_data;\n"
	"flat out vec4 v_color;\n"
	"flat out vec4 v_color2;\n"
	"flat out vec4 v_border;\n"
	"void main ()\n"
	"{\n"
	"    int  prim = int (i_params.z + 0.5);\n"
	"    vec2 lo;\n"
	"    vec2 hi;\n"
	"    if (prim == 2) {\n"
	"        float pad = i_params.y * 0.5 + 1.0;\n"
	"        lo = min (i_rect.xy, i_rect.zw) - pad;\n"
	"        hi = max (i_rect.xy, i_rect.zw) + pad;\n"
	"    } else if (prim == 4) {\n"
	"        lo = min (min (i_rect.xy, i_rect.zw), i_extra.xy) - 1.0;\n"
	"        hi = max (max (i_rect.xy, i_rect.zw), i_extra.xy) + 1.0;\n"
	"    } else if (prim == 8) {\n"
	/* a Bezier lies inside the hull of its control points */
	"        float pad = i_params.y * 0.5 + max (i_data.x, 0.0) + 1.0;\n"
	"        lo = min (min (i_rect.xy, i_rect.zw), min (i_extra.xy, i_extra.zw)) - pad;\n"
	"        hi = max (max (i_rect.xy, i_rect.zw), max (i_extra.xy, i_extra.zw)) + pad;\n"
	"    } else {\n"
	"        float pad = (prim == 1) ? i_params.y + 1.0 : 1.0;\n"
	"        lo = i_rect.xy - pad;\n"
	"        hi = i_rect.zw + pad;\n"
	"    }\n"
	"    lo = max (lo, i_clip.xy);\n"
	"    hi = max (min (hi, i_clip.zw), lo);\n"
	"    vec2 p = mix (lo, hi, a_corner);\n"
	"    v_pos = p;\n"
	"    v_rect = i_rect;\n"
	"    v_radii = i_radii;\n"
	"    v_clip = i_clip;\n"
	"    v_params = i_params;\n"
	"    v_extra = i_extra;\n"
	"    v_data = i_data;\n"
	"    v_color = i_color;\n"
	"    v_color2 = i_color2;\n"
	"    v_border = i_border;\n"
	"    gl_Position = u_mvp * vec4 (p, 0.0, 1.0);\n"
	"}\n";

/*
 * Fragment shader, part 1: inputs, uniforms, distance functions and
 * the helpers effects are written with (the "effect library").
 */
static const gchar *fragment_head =
	"#version 330\n"
	"in vec2 v_pos;\n"
	"flat in vec4 v_rect;\n"
	"flat in vec4 v_radii;\n"
	"flat in vec4 v_clip;\n"
	"flat in vec4 v_params;\n"
	"flat in vec4 v_extra;\n"
	"flat in vec4 v_data;\n"
	"flat in vec4 v_color;\n"
	"flat in vec4 v_color2;\n"
	"flat in vec4 v_border;\n"
	"uniform sampler2D u_tex;\n"
	"uniform sampler2D u_backdrop;\n"
	"uniform sampler2D u_blur;\n"
	"uniform vec2 u_backdrop_inv;\n"
	"uniform float u_time;\n"
	"uniform vec2 u_pointer;\n"
	"out vec4 o_color;\n"
	"\n"
	"const float LRG_PI = 3.14159265358979;\n"
	"const float LRG_TAU = 6.28318530717959;\n"
	"\n"
	/* what an effect receives; see docs/modules/paint/index.org */
	"struct LrgEffect {\n"
	"    vec2  uv;\n"
	"    vec2  pixel;\n"
	"    vec2  size;\n"
	"    float edge;\n"
	"    float time;\n"
	"    vec2  pointer;\n"
	"    float hover;\n"
	"    float press;\n"
	"    vec4  color1;\n"
	"    vec4  color2;\n"
	"    vec4  color3;\n"
	"    float intensity;\n"
	"    float scale;\n"
	"};\n"
	"\n"
	/* cheap hash and value noise (fract-of-dot construction) */
	"float lrg_hash (vec2 p)\n"
	"{\n"
	"    vec3 q = fract (vec3 (p.xyx) * 0.1031);\n"
	"    q += dot (q, q.yzx + 33.33);\n"
	"    return fract ((q.x + q.y) * q.z);\n"
	"}\n"
	"\n"
	"float lrg_noise (vec2 p)\n"
	"{\n"
	"    vec2 i = floor (p);\n"
	"    vec2 f = fract (p);\n"
	"    vec2 u = f * f * (3.0 - 2.0 * f);\n"
	"    return mix (mix (lrg_hash (i), lrg_hash (i + vec2 (1.0, 0.0)), u.x),\n"
	"                mix (lrg_hash (i + vec2 (0.0, 1.0)), lrg_hash (i + vec2 (1.0, 1.0)), u.x), u.y);\n"
	"}\n"
	"\n"
	"float lrg_fbm (vec2 p)\n"
	"{\n"
	"    float sum = 0.0;\n"
	"    float amp = 0.5;\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        sum += amp * lrg_noise (p);\n"
	"        p = p * 2.03 + vec2 (17.1, 9.2);\n"
	"        amp *= 0.5;\n"
	"    }\n"
	"    return sum;\n"
	"}\n"
	"\n"
	/* three-stop palette: a at 0, b at 0.5, c at 1 */
	"vec3 lrg_palette (vec3 a, vec3 b, vec3 c, float t)\n"
	"{\n"
	"    t = clamp (t, 0.0, 1.0);\n"
	"    return t < 0.5 ? mix (a, b, t * 2.0) : mix (b, c, t * 2.0 - 1.0);\n"
	"}\n"
	"\n"
	/* signed distance to a rounded box centred at the origin; radii are
	 * (top-left, top-right, bottom-right, bottom-left), y points down */
	"float sd_round_box (vec2 p, vec2 b, vec4 r)\n"
	"{\n"
	"    float rad = (p.x < 0.0) ? ((p.y < 0.0) ? r.x : r.w)\n"
	"                            : ((p.y < 0.0) ? r.y : r.z);\n"
	"    vec2 q = abs (p) - b + rad;\n"
	"    return min (max (q.x, q.y), 0.0) + length (max (q, 0.0)) - rad;\n"
	"}\n"
	"\n"
	/* outward unit normal of the same box: radial in the corner arcs,
	 * axis-aligned along the straight sides */
	"vec2 round_box_normal (vec2 p, vec2 b, vec4 r)\n"
	"{\n"
	"    float rad = (p.x < 0.0) ? ((p.y < 0.0) ? r.x : r.w)\n"
	"                            : ((p.y < 0.0) ? r.y : r.z);\n"
	"    vec2 s = vec2 (p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);\n"
	"    vec2 q = abs (p) - b + rad;\n"
	"    if (q.x > 0.0 && q.y > 0.0)\n"
	"        return s * normalize (q);\n"
	"    return q.x > q.y ? vec2 (s.x, 0.0) : vec2 (0.0, s.y);\n"
	"}\n"
	"\n"
	"float sd_segment (vec2 p, vec2 a, vec2 b)\n"
	"{\n"
	"    vec2  pa = p - a;\n"
	"    vec2  ba = b - a;\n"
	"    float h = clamp (dot (pa, ba) / max (dot (ba, ba), 1e-6), 0.0, 1.0);\n"
	"    return length (pa - ba * h);\n"
	"}\n"
	"\n"
	/* exact signed distance to a triangle, negative inside, any winding */
	"float sd_triangle (vec2 p, vec2 p0, vec2 p1, vec2 p2)\n"
	"{\n"
	"    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;\n"
	"    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;\n"
	"    vec2 q0 = v0 - e0 * clamp (dot (v0, e0) / dot (e0, e0), 0.0, 1.0);\n"
	"    vec2 q1 = v1 - e1 * clamp (dot (v1, e1) / dot (e1, e1), 0.0, 1.0);\n"
	"    vec2 q2 = v2 - e2 * clamp (dot (v2, e2) / dot (e2, e2), 0.0, 1.0);\n"
	"    float s = sign (e0.x * e2.y - e0.y * e2.x);\n"
	"    vec2 d = min (min (vec2 (dot (q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),\n"
	"                       vec2 (dot (q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),\n"
	"                  vec2 (dot (q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));\n"
	"    return -sqrt (d.x) * sign (d.y);\n"
	"}\n"
	"\n"
	"vec4 premul (vec4 c) { return vec4 (c.rgb * c.a, c.a); }\n"
	"\n";

/*
 * Fragment shader, part 2: the built-in effects. Each returns a
 * straight-alpha colour; the caller applies the rounded shape,
 * anti-aliasing, clip and opacity.
 */
static const gchar *fragment_effects =
	/* shimmer: a two-colour sweep with a highlight band crossing it */
	"vec4 fx_shimmer (LrgEffect fx)\n"
	"{\n"
	"    vec4  base = mix (fx.color1, fx.color2, fx.uv.x);\n"
	"    float x = fx.uv.x + fx.uv.y * 0.35;\n"
	"    float pos = fract (fx.time * 0.4) * 2.2 - 0.6;\n"
	"    float w = 0.12 / fx.scale;\n"
	"    float band = exp (-pow ((x - pos) / w, 2.0)) * fx.intensity * (0.6 + 0.4 * fx.hover);\n"
	"    band = clamp (band, 0.0, 1.0) * fx.color3.a;\n"
	"    return vec4 (mix (base.rgb, fx.color3.rgb, band), max (base.a, band));\n"
	"}\n"
	"\n"
	/* aurora: drifting curtains of light over the base colour */
	"vec4 fx_aurora (LrgEffect fx)\n"
	"{\n"
	"    vec2  q = fx.uv * vec2 (3.0, 1.5) * fx.scale;\n"
	"    float t = fx.time * 0.15;\n"
	"    float n = lrg_fbm (q + vec2 (t, -t * 0.5));\n"
	"    float curtain = 0.5 + 0.5 * sin (q.x * 2.2 + n * 5.0 + fx.time * 0.6);\n"
	"    float band = smoothstep (0.2, 1.0, curtain) * (1.0 - fx.uv.y * 0.6);\n"
	"    vec3  glow = mix (fx.color2.rgb, fx.color3.rgb, lrg_fbm (q * 1.7 - vec2 (t * 1.3, 0.0)));\n"
	"    vec3  col = fx.color1.rgb + glow * band * fx.intensity * (0.8 + 0.4 * fx.hover);\n"
	"    return vec4 (min (col, 1.0), fx.color1.a);\n"
	"}\n"
	"\n"
	/* neon: a glowing tube along the rim whose colour runs around it */
	"vec4 fx_neon (LrgEffect fx)\n"
	"{\n"
	"    float w = (2.0 + 4.0 * fx.intensity) * (1.0 + 0.6 * fx.hover + 0.4 * fx.press);\n"
	"    float glow = exp (min (fx.edge, 0.0) / w);\n"
	"    float core = smoothstep (-2.0, -0.5, fx.edge);\n"
	"    vec2  c = fx.pixel - fx.size * 0.5;\n"
	"    float ang = atan (c.y, c.x) / LRG_TAU;\n"
	"    float k = 0.5 + 0.5 * sin ((ang + fx.time * 0.25) * LRG_TAU * 2.0);\n"
	"    vec3  tube = mix (fx.color2.rgb, fx.color3.rgb, k);\n"
	"    float pulse = 0.85 + 0.15 * sin (fx.time * 3.0);\n"
	"    float a = clamp (glow * pulse + core, 0.0, 1.0);\n"
	"    return vec4 (mix (fx.color1.rgb, tube, a), max (fx.color1.a, a));\n"
	"}\n"
	"\n"
	/* spotlight: a soft light that follows the pointer while hovered */
	"vec4 fx_spotlight (LrgEffect fx)\n"
	"{\n"
	"    float r = 0.35 * length (fx.size) * fx.scale;\n"
	"    float d = length (fx.pixel - fx.pointer);\n"
	"    float s = 1.0 - smoothstep (0.0, r, d);\n"
	"    float k = s * s * fx.hover * fx.intensity;\n"
	"    vec3  col = fx.color1.rgb + fx.color2.rgb * k * 0.8 + fx.color3.rgb * fx.press * k * 0.3;\n"
	"    return vec4 (min (col, 1.0), fx.color1.a);\n"
	"}\n"
	"\n"
	/* ripple: rings travelling out from the pointer, stronger on press */
	"vec4 fx_ripple (LrgEffect fx)\n"
	"{\n"
	"    float d = length (fx.pixel - fx.pointer);\n"
	"    float wave = 0.5 + 0.5 * sin (d * 0.25 / fx.scale - fx.time * 8.0);\n"
	"    float fall = exp (-d * 0.02 / fx.scale);\n"
	"    float k = (0.25 * fx.hover + fx.press) * fx.intensity * fall * wave;\n"
	"    return vec4 (mix (fx.color1.rgb, fx.color2.rgb, clamp (k, 0.0, 1.0)), fx.color1.a);\n"
	"}\n"
	"\n"
	/* grain: a vertical gradient with animated film grain */
	"vec4 fx_grain (LrgEffect fx)\n"
	"{\n"
	"    vec3  base = mix (fx.color1.rgb, fx.color2.rgb, fx.uv.y);\n"
	"    float g = lrg_hash (floor (fx.pixel / fx.scale) + floor (fx.time * 24.0) * vec2 (17.0, 31.0)) - 0.5;\n"
	"    return vec4 (clamp (base + g * 0.18 * fx.intensity, 0.0, 1.0), fx.color1.a);\n"
	"}\n"
	"\n"
	/* plasma: summed sine fields through the three-colour palette */
	"vec4 fx_plasma (LrgEffect fx)\n"
	"{\n"
	"    vec2  p = fx.uv * 4.0 * fx.scale;\n"
	"    float t = fx.time * 0.8;\n"
	"    float v = sin (p.x + t) + sin (p.y * 1.3 - t * 0.7) + sin ((p.x + p.y) * 0.7 + t * 0.5)\n"
	"            + sin (length (p - 2.0) * 1.5 - t);\n"
	"    float k = 0.5 + 0.5 * sin (v * 1.2);\n"
	"    vec3  c = lrg_palette (fx.color1.rgb, fx.color2.rgb, fx.color3.rgb, k);\n"
	"    return vec4 (mix (fx.color1.rgb, c, clamp (fx.intensity, 0.0, 1.0)), fx.color1.a);\n"
	"}\n"
	"\n"
	/* mesh-gradient: three colour points drifting, blended by distance */
	"vec4 fx_mesh (LrgEffect fx)\n"
	"{\n"
	"    float t = fx.time * 0.2;\n"
	"    float aspect = fx.size.x / max (fx.size.y, 1.0);\n"
	"    vec2  uv = vec2 (fx.uv.x * aspect, fx.uv.y);\n"
	"    vec2  p1 = vec2 ((0.2 + 0.15 * sin (t * 1.1)) * aspect, 0.25 + 0.2 * cos (t * 0.9));\n"
	"    vec2  p2 = vec2 ((0.8 + 0.15 * cos (t * 0.7)) * aspect, 0.3 + 0.2 * sin (t * 1.3));\n"
	"    vec2  p3 = vec2 ((0.5 + 0.25 * sin (t * 0.5)) * aspect, 0.8 + 0.15 * cos (t * 1.7));\n"
	"    float s = 1.0 / fx.scale;\n"
	"    float w1 = 1.0 / (pow (length (uv - p1) * s, 2.0) + 0.02);\n"
	"    float w2 = 1.0 / (pow (length (uv - p2) * s, 2.0) + 0.02);\n"
	"    float w3 = 1.0 / (pow (length (uv - p3) * s, 2.0) + 0.02);\n"
	"    vec3  c = (fx.color1.rgb * w1 + fx.color2.rgb * w2 + fx.color3.rgb * w3) / (w1 + w2 + w3);\n"
	"    return vec4 (mix (fx.color1.rgb, c, clamp (fx.intensity, 0.0, 1.0)), fx.color1.a);\n"
	"}\n"
	"\n"
	/* ids match builtin_effects[] in lrg-paint-fx.c */
	"vec4 lrg_builtin_effect (int id, LrgEffect fx)\n"
	"{\n"
	"    if (id == 1) return fx_shimmer (fx);\n"
	"    if (id == 2) return fx_aurora (fx);\n"
	"    if (id == 3) return fx_neon (fx);\n"
	"    if (id == 4) return fx_spotlight (fx);\n"
	"    if (id == 5) return fx_ripple (fx);\n"
	"    if (id == 6) return fx_grain (fx);\n"
	"    if (id == 7) return fx_plasma (fx);\n"
	"    if (id == 8) return fx_mesh (fx);\n"
	"    return fx.color1;\n"
	"}\n"
	"\n";

/* Fragment shader, part 3 for the base program: no user effects */
static const gchar *fragment_user_stub =
	"vec4 lrg_user_effect (int id, LrgEffect fx)\n"
	"{\n"
	"    return fx.color1;\n"
	"}\n"
	"\n";

/* Fragment shader, part 4: glass, curves and the per-primitive switch */
static const gchar *fragment_main =
	/*
	 * Glass (and blur, which is glass with full frost and no rim): the
	 * backdrop seen through a rounded slab. The rim band bends the
	 * backdrop inward along the SDF normal (content looks pulled to the
	 * edge, like a lens), optionally splitting R and B; the middle mixes
	 * the sharp and the blurred copy by the frost; colours are
	 * re-saturated, tinted, and a specular rim lit from the top-left.
	 * Offsets are worked out in canvas units and turned into target
	 * pixels with the inverse of the pixel->canvas Jacobian, so it works
	 * at any transform and on screen or in a render texture alike.
	 */
	"vec4 shade_glass (vec2 c, vec2 hb, float d, vec2 dpx, vec2 dpy)\n"
	"{\n"
	"    float frost = v_extra.x;\n"
	"    float refr = v_extra.y;\n"
	"    float sat = v_extra.z;\n"
	"    float rim = v_extra.w;\n"
	"    float disp = v_data.x;\n"
	"    float bevel = max (v_data.y, 1.0);\n"
	"    vec2  n = round_box_normal (v_pos - c, hb, v_radii);\n"
	"\n"
	"    float t = clamp (1.0 + d / bevel, 0.0, 1.0);\n"
	"    float bend = t * t;\n"
	"    vec2  off_c = -n * bend * refr;\n"
	"    mat2  jac = mat2 (dpx, dpy);\n"
	"    vec2  off_px = abs (determinant (jac)) > 1e-10 ? inverse (jac) * off_c : vec2 (0.0);\n"
	"\n"
	"    vec2 fc = gl_FragCoord.xy + off_px;\n"
	"    vec4 sharp = texture (u_backdrop, fc * u_backdrop_inv);\n"
	"    vec4 soft = texture (u_blur, fc * u_backdrop_inv);\n"
	"    if (disp > 0.0 && refr > 0.0 && bend > 0.001) {\n"
	"        vec2 spread = off_px * disp * 0.6;\n"
	"        sharp.r = texture (u_backdrop, (fc - spread) * u_backdrop_inv).r;\n"
	"        sharp.b = texture (u_backdrop, (fc + spread) * u_backdrop_inv).b;\n"
	"        soft.r = texture (u_blur, (fc - spread) * u_backdrop_inv).r;\n"
	"        soft.b = texture (u_blur, (fc + spread) * u_backdrop_inv).b;\n"
	"    }\n"
	"    vec4 col = mix (sharp, soft, frost);\n"
	"\n"
	"    float luma = dot (col.rgb, vec3 (0.2126, 0.7152, 0.0722));\n"
	"    col.rgb = clamp (mix (vec3 (luma), col.rgb, sat), 0.0, 1.0);\n"
	"\n"
	"    vec4 tint = premul (v_color);\n"
	"    col = col * (1.0 - tint.a) + tint;\n"
	"\n"
	"    vec2  light = normalize (vec2 (-1.0, -1.4));\n"
	"    float facing = dot (n, light);\n"
	"    float lit = 0.25 + (facing > 0.0 ? pow (facing, 1.5) : 0.4 * pow (-facing, 1.5));\n"
	"    float lw = max (1.5 * length (dpx), 0.75);\n"
	"    float line = clamp (1.0 + d / lw, 0.0, 1.0);\n"
	"    float spec = rim * (line * line * lit + bend * 0.12 * max (facing, 0.0));\n"
	"    float k = (v_pos.y - v_rect.y) / max (v_rect.w - v_rect.y, 1.0);\n"
	"    float sheen = rim * 0.06 * (0.5 - k);\n"
	"    col.a = clamp (col.a + spec, 0.0, 1.0);\n"
	"    col.rgb = clamp (col.rgb + vec3 (spec + sheen), 0.0, col.a);\n"
	"    return col;\n"
	"}\n"
	"\n"
	/*
	 * Curve: a cubic Bezier flattened into 24 segments in the shader.
	 * The closest segment gives both the distance (stroke coverage and
	 * glow) and the arc length at that point (gradient position and
	 * pulse placement).
	 */
	"vec4 shade_curve ()\n"
	"{\n"
	"    vec2  p0 = v_rect.xy;\n"
	"    vec2  p1 = v_extra.xy;\n"
	"    vec2  p2 = v_extra.zw;\n"
	"    vec2  p3 = v_rect.zw;\n"
	"    vec2  prev = p0;\n"
	"    float best = 1e9;\n"
	"    float best_s = 0.0;\n"
	"    float acc = 0.0;\n"
	"    for (int i = 1; i <= 24; i++) {\n"
	"        float t = float (i) / 24.0;\n"
	"        float u = 1.0 - t;\n"
	"        vec2  q = u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;\n"
	"        vec2  pa = v_pos - prev;\n"
	"        vec2  ba = q - prev;\n"
	"        float len = length (ba);\n"
	"        float h = clamp (dot (pa, ba) / max (dot (ba, ba), 1e-6), 0.0, 1.0);\n"
	"        float dd = length (pa - ba * h);\n"
	"        if (dd < best) {\n"
	"            best = dd;\n"
	"            best_s = acc + h * len;\n"
	"        }\n"
	"        acc += len;\n"
	"        prev = q;\n"
	"    }\n"
	"    float total = max (acc, 1e-4);\n"
	"    float d = best - v_params.y * 0.5;\n"
	"    float core = clamp (0.5 - d, 0.0, 1.0);\n"
	"    vec4  stroke = mix (v_color, v_color2, best_s / total);\n"
	"\n"
	"    int   count = int (v_data.y + 0.5);\n"
	"    float pulse = 0.0;\n"
	"    float plen = max (v_params.y * 4.0, 6.0);\n"
	"    for (int k = 0; k < 8; k++) {\n"
	"        if (k >= count)\n"
	"            break;\n"
	"        float at = fract (v_data.w + u_time * v_data.z + float (k) / float (count)) * total;\n"
	"        float ds = (best_s - at) / plen;\n"
	"        pulse = max (pulse, exp (-ds * ds));\n"
	"    }\n"
	"    vec4  pc = v_radii;\n"
	"    float pk = pulse * pc.a;\n"
	"    vec4  body = premul (vec4 (mix (stroke.rgb, pc.rgb, pk), max (stroke.a, pk))) * core;\n"
	"\n"
	"    vec4 halo = vec4 (0.0);\n"
	"    if (v_data.x > 0.0) {\n"
	"        float g = 1.0 - clamp (max (d, 0.0) / v_data.x, 0.0, 1.0);\n"
	"        vec4  hc = mix (v_border, vec4 (pc.rgb, max (v_border.a, pc.a)), pk * 0.7);\n"
	"        halo = premul (hc) * g * g;\n"
	"    }\n"
	"    return body + halo * (1.0 - body.a);\n"
	"}\n"
	"\n"
	"void main ()\n"
	"{\n"
	/* derivatives first, while every fragment of the quad is active */
	"    vec2 dpx = dFdx (v_pos);\n"
	"    vec2 dpy = dFdy (v_pos);\n"
	"\n"
	"    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y ||\n"
	"        v_pos.x > v_clip.z || v_pos.y > v_clip.w)\n"
	"        discard;\n"
	"\n"
	"    int  prim = int (v_params.z + 0.5);\n"
	"    int  gk = int (v_params.w + 0.5);\n"
	"    vec2 c = (v_rect.xy + v_rect.zw) * 0.5;\n"
	"    vec2 hb = abs (v_rect.zw - v_rect.xy) * 0.5;\n"
	"    vec4 fill = v_color;\n"
	"\n"
	/* gradients: linear spans the box along the direction, radial runs
	 * from a centre given as a fraction of the box out to a radius */
	"    if (gk == 1) {\n"
	"        vec2  dir = v_extra.xy;\n"
	"        float ext = abs (dir.x) * hb.x + abs (dir.y) * hb.y;\n"
	"        float t = clamp (dot (v_pos - c, dir) / (2.0 * max (ext, 1e-4)) + 0.5, 0.0, 1.0);\n"
	"        fill = mix (v_color, v_color2, t);\n"
	"    } else if (gk == 2) {\n"
	"        vec2  ctr = mix (v_rect.xy, v_rect.zw, v_extra.xy);\n"
	"        float t = clamp (length (v_pos - ctr) / max (v_extra.z, 1e-4), 0.0, 1.0);\n"
	"        fill = mix (v_color, v_color2, t);\n"
	"    }\n"
	"\n"
	"    vec4 outc;\n"
	"    if (prim == 2) {\n"
	"        float d = sd_segment (v_pos, v_rect.xy, v_rect.zw) - v_params.y * 0.5;\n"
	"        outc = premul (fill) * clamp (0.5 - d, 0.0, 1.0);\n"
	"    } else if (prim == 4) {\n"
	"        float d = sd_triangle (v_pos, v_rect.xy, v_rect.zw, v_extra.xy);\n"
	"        outc = premul (v_color) * clamp (0.5 - d, 0.0, 1.0);\n"
	"    } else if (prim == 8) {\n"
	"        outc = shade_curve ();\n"
	"    } else if (prim == 5 || prim == 6) {\n"
	"        float d = sd_round_box (v_pos - c, hb, v_radii);\n"
	"        outc = shade_glass (c, hb, d, dpx, dpy) * (clamp (0.5 - d, 0.0, 1.0) * v_data.z);\n"
	"    } else if (prim == 7) {\n"
	"        float     d = sd_round_box (v_pos - c, hb, v_radii);\n"
	"        LrgEffect fx;\n"
	"        fx.size = max (v_rect.zw - v_rect.xy, vec2 (1e-4));\n"
	"        fx.pixel = v_pos - v_rect.xy;\n"
	"        fx.uv = fx.pixel / fx.size;\n"
	"        fx.edge = d;\n"
	"        fx.time = u_time * v_data.x;\n"
	"        fx.pointer = u_pointer - v_rect.xy;\n"
	"        fx.hover = v_extra.x;\n"
	"        fx.press = v_extra.y;\n"
	"        fx.intensity = v_extra.z;\n"
	"        fx.scale = max (v_data.y, 1e-3);\n"
	"        fx.color1 = v_color;\n"
	"        fx.color2 = v_color2;\n"
	"        fx.color3 = v_border;\n"
	"        int  id = int (v_extra.w + 0.5);\n"
	"        vec4 e = id >= 64 ? lrg_user_effect (id, fx) : lrg_builtin_effect (id, fx);\n"
	"        outc = premul (clamp (e, 0.0, 1.0)) * (clamp (0.5 - d, 0.0, 1.0) * v_data.z);\n"
	"    } else {\n"
	"        float d = sd_round_box (v_pos - c, hb, v_radii);\n"
	"        if (prim == 1) {\n"
	"            float blur = max (v_params.y, 0.5);\n"
	"            outc = premul (fill) * (1.0 - smoothstep (-blur, blur, d));\n"
	"        } else {\n"
	"            vec4 body = premul (fill);\n"
	"            if (prim == 3) {\n"
	"                vec2 f = (v_pos - v_rect.xy) / max (v_rect.zw - v_rect.xy, vec2 (1e-4));\n"
	/* textures (images, font atlases) are straight alpha */
	"                vec4 t = texture (u_tex, mix (v_extra.xy, v_extra.zw, f));\n"
	"                body *= vec4 (t.rgb * t.a, t.a);\n"
	"            }\n"
	"            float bw = v_params.x;\n"
	"            if (bw > 0.0)\n"
	"                body = mix (premul (v_border), body, clamp (0.5 - (d + bw), 0.0, 1.0));\n"
	"            outc = body * clamp (0.5 - d, 0.0, 1.0);\n"
	"        }\n"
	"    }\n"
	"    if (outc.a <= 0.0)\n"
	"        discard;\n"
	"    o_color = outc;\n"
	"}\n";

/*
 * The blur passes. Dual filtering: each "down" pass halves the image
 * reading the centre and four diagonal neighbours (bilinear taps, so
 * 16 texels for 5 fetches); each "up" pass doubles it reading a ring
 * of eight. The cost per pass is constant whatever the radius; the
 * radius comes from the number of levels and the tap spread.
 */
static const gchar *blur_vertex_source =
	"#version 330\n"
	"layout(location = 0) in vec2 a_corner;\n"
	"out vec2 v_uv;\n"
	"void main ()\n"
	"{\n"
	/* y flipped so the quad keeps the front-facing winding rlgl's
	 * back-face culling expects; uv follows the position */
	"    v_uv = vec2 (a_corner.x, 1.0 - a_corner.y);\n"
	"    gl_Position = vec4 (v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const gchar *blur_fragment_source =
	"#version 330\n"
	"in vec2 v_uv;\n"
	"uniform sampler2D u_src;\n"
	"uniform vec2 u_step;\n"
	"uniform vec2 u_uv_max;\n"
	"uniform int u_mode;\n"
	"out vec4 o_color;\n"
	"vec4 tap (vec2 uv) { return texture (u_src, clamp (uv, vec2 (0.0), u_uv_max)); }\n"
	"void main ()\n"
	"{\n"
	"    vec2 s = u_step;\n"
	"    if (u_mode == 0) {\n"
	"        vec4 sum = tap (v_uv) * 4.0;\n"
	"        sum += tap (v_uv + vec2 (-s.x, -s.y));\n"
	"        sum += tap (v_uv + vec2 ( s.x, -s.y));\n"
	"        sum += tap (v_uv + vec2 (-s.x,  s.y));\n"
	"        sum += tap (v_uv + vec2 ( s.x,  s.y));\n"
	"        o_color = sum * 0.125;\n"
	"    } else {\n"
	"        vec4 sum = tap (v_uv + vec2 (-2.0 * s.x, 0.0));\n"
	"        sum += tap (v_uv + vec2 (2.0 * s.x, 0.0));\n"
	"        sum += tap (v_uv + vec2 (0.0, -2.0 * s.y));\n"
	"        sum += tap (v_uv + vec2 (0.0, 2.0 * s.y));\n"
	"        sum += tap (v_uv + vec2 (-s.x, -s.y)) * 2.0;\n"
	"        sum += tap (v_uv + vec2 ( s.x, -s.y)) * 2.0;\n"
	"        sum += tap (v_uv + vec2 (-s.x,  s.y)) * 2.0;\n"
	"        sum += tap (v_uv + vec2 ( s.x,  s.y)) * 2.0;\n"
	"        o_color = sum / 12.0;\n"
	"    }\n"
	"}\n";

/* One compiled paint program and its uniform locations */
typedef struct
{
	guint id;
	gint  loc_mvp;
	gint  loc_tex;
	gint  loc_backdrop;
	gint  loc_blur;
	gint  loc_backdrop_inv;
	gint  loc_time;
	gint  loc_pointer;
} PaintProgram;

/* One offscreen colour target: level 0 is the copy of the frame */
typedef struct
{
	guint fbo;
	guint tex;
	gint  width;
	gint  height;
} BlurLevel;

struct _LrgPaintRenderer
{
	GObject      parent_instance;

	gboolean     gl_ready;
	gboolean     gl_failed;
	PaintProgram base;
	PaintProgram variant;        /* base + user effects, id 0 if none */
	guint        variant_serial; /* registry serial the variant was built for */
	guint        vao;
	guint        quad_vbo;
	guint        instance_vbo;
	guint        capacity;
	guint        draw_calls;
	guint        backdrop_passes;

	/* Backdrop resources, created on the first blur/glass instance */
	gboolean     fx_ready;
	gboolean     fx_failed;
	guint        blur_shader;
	gint         loc_blur_src;
	gint         loc_blur_step;
	gint         loc_blur_uv_max;
	gint         loc_blur_mode;
	guint        blur_vao;
	guint        blur_vbo;
	BlurLevel    levels[MAX_BLUR_LEVELS + 1];
};

G_DEFINE_FINAL_TYPE (LrgPaintRenderer, lrg_paint_renderer, G_TYPE_OBJECT)

static LrgPaintRenderer *default_renderer = NULL;

/* Two triangles in raylib's quad winding: TL, BL, BR / TL, BR, TR */
static const gfloat quad_corners[] = {
	0.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f,
	0.0f, 0.0f,  1.0f, 1.0f,  1.0f, 0.0f
};

/* ==========================================================================
 * GL resources
 * ========================================================================== */

/*
 * point_instance_attributes:
 *
 * Points the per-instance attributes at instance @first of the bound
 * instance buffer. glDrawArraysInstanced has no base-instance argument
 * on GL 3.3, so each batch re-points the attribute offsets instead.
 */
static void
point_instance_attributes (guint first)
{
	gint stride = (gint) sizeof (LrgPaintInstance);
	gint base = (gint) first * stride;

	rlSetVertexAttribute (ATTR_RECT, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, rect));
	rlSetVertexAttribute (ATTR_RADII, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, radii));
	rlSetVertexAttribute (ATTR_CLIP, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, clip));
	rlSetVertexAttribute (ATTR_PARAMS, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, params));
	rlSetVertexAttribute (ATTR_EXTRA, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, extra));
	rlSetVertexAttribute (ATTR_DATA, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, data));
	rlSetVertexAttribute (ATTR_COLOR, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, color));
	rlSetVertexAttribute (ATTR_COLOR2, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, color2));
	rlSetVertexAttribute (ATTR_BORDER, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, border));
}

/* ensure_capacity: grow the instance buffer to hold @needed instances */
static void
ensure_capacity (LrgPaintRenderer *self,
                 guint             needed)
{
	guint cap;

	if (needed <= self->capacity && self->instance_vbo != 0)
		return;

	cap = MAX (self->capacity, (guint) MIN_CAPACITY);
	while (cap < needed)
		cap *= 2;

	if (self->instance_vbo != 0)
		rlUnloadVertexBuffer (self->instance_vbo);

	self->instance_vbo = rlLoadVertexBuffer (NULL,
	                                         (gint) (cap * sizeof (LrgPaintInstance)),
	                                         true);
	self->capacity = cap;
}

/*
 * build_program:
 *
 * Links the paint program from its parts; @user_source (the user
 * effects and their dispatcher) replaces the stub when given. Returns
 * FALSE, with @out zeroed, when the driver rejects it.
 */
static gboolean
build_program (const gchar  *user_source,
               PaintProgram *out)
{
	g_autofree gchar *fragment = NULL;
	guint             id;

	memset (out, 0, sizeof (PaintProgram));

	fragment = g_strconcat (fragment_head, fragment_effects,
	                        user_source != NULL ? user_source : fragment_user_stub,
	                        fragment_main, NULL);
	id = rlLoadShaderProgram (vertex_source, fragment);
	if (id == 0 || id == rlGetShaderIdDefault ())
		return FALSE;

	out->id = id;
	out->loc_mvp = rlGetLocationUniform (id, "u_mvp");
	out->loc_tex = rlGetLocationUniform (id, "u_tex");
	out->loc_backdrop = rlGetLocationUniform (id, "u_backdrop");
	out->loc_blur = rlGetLocationUniform (id, "u_blur");
	out->loc_backdrop_inv = rlGetLocationUniform (id, "u_backdrop_inv");
	out->loc_time = rlGetLocationUniform (id, "u_time");
	out->loc_pointer = rlGetLocationUniform (id, "u_pointer");
	return TRUE;
}

/*
 * ensure_gl:
 *
 * Compiles the shader and builds the vertex array on first use. On
 * failure the renderer stays disabled for the rest of the process
 * instead of retrying every frame.
 */
static gboolean
ensure_gl (LrgPaintRenderer *self)
{
	guint attr;

	if (self->gl_ready)
		return TRUE;
	if (self->gl_failed)
		return FALSE;

	if (!build_program (NULL, &self->base))
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT,
		             "SDF paint shader failed to compile; UI shapes will not be drawn");
		self->gl_failed = TRUE;
		return FALSE;
	}

	self->vao = rlLoadVertexArray ();
	rlEnableVertexArray (self->vao);

	/* Static unit quad, one corner per vertex */
	self->quad_vbo = rlLoadVertexBuffer (quad_corners, (gint) sizeof (quad_corners), false);
	rlSetVertexAttribute (ATTR_CORNER, 2, RL_FLOAT, false, 0, 0);
	rlEnableVertexAttribute (ATTR_CORNER);

	/* Per-instance stream */
	ensure_capacity (self, MIN_CAPACITY);
	rlEnableVertexBuffer (self->instance_vbo);
	point_instance_attributes (0);
	for (attr = ATTR_RECT; attr <= ATTR_BORDER; attr++)
	{
		rlEnableVertexAttribute (attr);
		rlSetVertexAttributeDivisor (attr, 1);
	}

	rlDisableVertexArray ();
	rlDisableVertexBuffer ();

	self->gl_ready = TRUE;
	lrg_debug (LRG_LOG_DOMAIN_PAINT, "SDF paint renderer ready (shader %u)", self->base.id);
	return TRUE;
}

/*
 * active_program:
 *
 * The program to draw with: the user-effect variant when effects are
 * registered and it linked, else the base program. The variant is
 * (re)built once per registry change; a failed build is not retried
 * until the registry changes again.
 */
static const PaintProgram *
active_program (LrgPaintRenderer *self)
{
	guint serial = _lrg_paint_effect_get_serial ();

	if (serial == 0)
		return &self->base;

	if (serial != self->variant_serial)
	{
		g_autofree gchar *user = _lrg_paint_effect_build_user_source ();

		if (self->variant.id != 0)
			rlUnloadShaderProgram (self->variant.id);
		memset (&self->variant, 0, sizeof (PaintProgram));
		self->variant_serial = serial;

		if (user != NULL && !build_program (user, &self->variant))
			lrg_warning (LRG_LOG_DOMAIN_PAINT,
			             "user paint effects failed to compile (see the GLSL log above); "
			             "drawing them with their first colour");
	}

	return self->variant.id != 0 ? &self->variant : &self->base;
}

static void
release_level (BlurLevel *level)
{
	if (level->fbo != 0)
		rlUnloadFramebuffer (level->fbo);
	if (level->tex != 0)
		rlUnloadTexture (level->tex);
	memset (level, 0, sizeof (BlurLevel));
}

static void
release_gl (LrgPaintRenderer *self)
{
	guint i;

	if (!IsWindowReady ())
		return;

	if (self->fx_ready)
	{
		for (i = 0; i <= MAX_BLUR_LEVELS; i++)
			release_level (&self->levels[i]);
		rlUnloadVertexBuffer (self->blur_vbo);
		rlUnloadVertexArray (self->blur_vao);
		rlUnloadShaderProgram (self->blur_shader);
		self->fx_ready = FALSE;
	}

	if (!self->gl_ready)
		return;

	rlUnloadVertexBuffer (self->instance_vbo);
	rlUnloadVertexBuffer (self->quad_vbo);
	rlUnloadVertexArray (self->vao);
	rlUnloadShaderProgram (self->base.id);
	if (self->variant.id != 0)
		rlUnloadShaderProgram (self->variant.id);
	self->gl_ready = FALSE;
}

/*
 * ensure_fx:
 *
 * Builds the blur program and its full-target quad on the first
 * backdrop batch. Frames without blur or glass never get here.
 */
static gboolean
ensure_fx (LrgPaintRenderer *self)
{
	if (self->fx_ready)
		return TRUE;
	if (self->fx_failed)
		return FALSE;

	self->blur_shader = rlLoadShaderProgram (blur_vertex_source, blur_fragment_source);
	if (self->blur_shader == 0 || self->blur_shader == rlGetShaderIdDefault ())
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT,
		             "backdrop blur shader failed to compile; glass and blur will not be drawn");
		self->fx_failed = TRUE;
		return FALSE;
	}
	self->loc_blur_src = rlGetLocationUniform (self->blur_shader, "u_src");
	self->loc_blur_step = rlGetLocationUniform (self->blur_shader, "u_step");
	self->loc_blur_uv_max = rlGetLocationUniform (self->blur_shader, "u_uv_max");
	self->loc_blur_mode = rlGetLocationUniform (self->blur_shader, "u_mode");

	self->blur_vao = rlLoadVertexArray ();
	rlEnableVertexArray (self->blur_vao);
	self->blur_vbo = rlLoadVertexBuffer (quad_corners, (gint) sizeof (quad_corners), false);
	rlSetVertexAttribute (ATTR_CORNER, 2, RL_FLOAT, false, 0, 0);
	rlEnableVertexAttribute (ATTR_CORNER);
	rlDisableVertexArray ();
	rlDisableVertexBuffer ();

	self->fx_ready = TRUE;
	return TRUE;
}

/*
 * ensure_level:
 *
 * (Re)creates one offscreen colour target at @width x @height:
 * RGBA8, bilinear, clamped to the edge. Note that rlgl's framebuffer
 * helpers leave framebuffer 0 bound; the caller rebinds its target.
 */
static gboolean
ensure_level (BlurLevel *level,
              gint       width,
              gint       height)
{
	if (level->fbo != 0 && level->width == width && level->height == height)
		return TRUE;

	release_level (level);

	level->tex = rlLoadTexture (NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
	if (level->tex == 0)
		return FALSE;
	rlTextureParameters (level->tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (level->tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (level->tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (level->tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);

	level->fbo = rlLoadFramebuffer ();
	rlFramebufferAttach (level->fbo, level->tex, RL_ATTACHMENT_COLOR_CHANNEL0,
	                     RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (level->fbo))
	{
		release_level (level);
		return FALSE;
	}

	level->width = width;
	level->height = height;
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_paint_renderer_finalize (GObject *object)
{
	LrgPaintRenderer *self = LRG_PAINT_RENDERER (object);

	release_gl (self);

	if (default_renderer == self)
		default_renderer = NULL;

	G_OBJECT_CLASS (lrg_paint_renderer_parent_class)->finalize (object);
}

static void
lrg_paint_renderer_class_init (LrgPaintRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_paint_renderer_finalize;
}

static void
lrg_paint_renderer_init (LrgPaintRenderer *self)
{
	self->base.loc_mvp = -1;
	self->base.loc_tex = -1;
}

LrgPaintRenderer *
lrg_paint_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PAINT_RENDERER, NULL);
}

LrgPaintRenderer *
lrg_paint_renderer_get_default (void)
{
	if (default_renderer == NULL)
		default_renderer = lrg_paint_renderer_new ();

	return default_renderer;
}

gboolean
lrg_paint_renderer_is_available (void)
{
	return IsWindowReady ();
}

guint
lrg_paint_renderer_get_draw_calls (LrgPaintRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_RENDERER (self), 0);

	return self->draw_calls;
}

guint
lrg_paint_renderer_get_backdrop_passes (LrgPaintRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_RENDERER (self), 0);

	return self->backdrop_passes;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/*
 * mat_mul:
 *
 * The product raymath's MatrixMultiply() computes (raymath itself
 * needs C99 and this library builds as gnu89).
 */
static Matrix
mat_mul (const Matrix *l,
         const Matrix *r)
{
	Matrix out;

	out.m0 = l->m0 * r->m0 + l->m1 * r->m4 + l->m2 * r->m8 + l->m3 * r->m12;
	out.m1 = l->m0 * r->m1 + l->m1 * r->m5 + l->m2 * r->m9 + l->m3 * r->m13;
	out.m2 = l->m0 * r->m2 + l->m1 * r->m6 + l->m2 * r->m10 + l->m3 * r->m14;
	out.m3 = l->m0 * r->m3 + l->m1 * r->m7 + l->m2 * r->m11 + l->m3 * r->m15;
	out.m4 = l->m4 * r->m0 + l->m5 * r->m4 + l->m6 * r->m8 + l->m7 * r->m12;
	out.m5 = l->m4 * r->m1 + l->m5 * r->m5 + l->m6 * r->m9 + l->m7 * r->m13;
	out.m6 = l->m4 * r->m2 + l->m5 * r->m6 + l->m6 * r->m10 + l->m7 * r->m14;
	out.m7 = l->m4 * r->m3 + l->m5 * r->m7 + l->m6 * r->m11 + l->m7 * r->m15;
	out.m8 = l->m8 * r->m0 + l->m9 * r->m4 + l->m10 * r->m8 + l->m11 * r->m12;
	out.m9 = l->m8 * r->m1 + l->m9 * r->m5 + l->m10 * r->m9 + l->m11 * r->m13;
	out.m10 = l->m8 * r->m2 + l->m9 * r->m6 + l->m10 * r->m10 + l->m11 * r->m14;
	out.m11 = l->m8 * r->m3 + l->m9 * r->m7 + l->m10 * r->m11 + l->m11 * r->m15;
	out.m12 = l->m12 * r->m0 + l->m13 * r->m4 + l->m14 * r->m8 + l->m15 * r->m12;
	out.m13 = l->m12 * r->m1 + l->m13 * r->m5 + l->m14 * r->m9 + l->m15 * r->m13;
	out.m14 = l->m12 * r->m2 + l->m13 * r->m6 + l->m14 * r->m10 + l->m15 * r->m14;
	out.m15 = l->m12 * r->m3 + l->m13 * r->m7 + l->m14 * r->m11 + l->m15 * r->m15;

	return out;
}

/* current_mvp: the matrix raylib would apply to its own batch now */
static Matrix
current_mvp (void)
{
	Matrix transform;
	Matrix modelview;
	Matrix projection;
	Matrix tmp;

	/* Same accumulation raylib applies to its own batch: the pushed
	 * transform, then the camera/model-view, then the projection */
	transform = rlGetMatrixTransform ();
	modelview = rlGetMatrixModelview ();
	projection = rlGetMatrixProjection ();
	tmp = mat_mul (&transform, &modelview);
	return mat_mul (&tmp, &projection);
}

/* begin_pipeline: bind shader, VAO and premultiplied blending */
static void
begin_pipeline (LrgPaintRenderer   *self,
                const PaintProgram *prog,
                LrgPaintList       *list)
{
	Matrix mvp;
	gint   slot = 0;
	gint   slot_backdrop = SLOT_BACKDROP;
	gint   slot_blur = SLOT_BLUR;
	gfloat time;
	gfloat pointer[2];

	mvp = current_mvp ();

	/* Animation inputs: the list's clock and pointer, never a clock of
	 * our own */
	time = (gfloat) lrg_paint_list_get_time (list);
	if (!lrg_paint_list_get_pointer (list, &pointer[0], &pointer[1]))
		pointer[0] = pointer[1] = NO_POINTER;

	/* Blend state first: rlSetBlendMode() flushes raylib's batch, and
	 * that flush unbinds whatever program and VAO are current */
	rlSetBlendFactors (RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
	rlSetBlendMode (RL_BLEND_CUSTOM);

	rlEnableShader (prog->id);
	rlSetUniformMatrix (prog->loc_mvp, mvp);
	rlSetUniform (prog->loc_tex, &slot, RL_SHADER_UNIFORM_INT, 1);
	rlSetUniform (prog->loc_backdrop, &slot_backdrop, RL_SHADER_UNIFORM_INT, 1);
	rlSetUniform (prog->loc_blur, &slot_blur, RL_SHADER_UNIFORM_INT, 1);
	rlSetUniform (prog->loc_time, &time, RL_SHADER_UNIFORM_FLOAT, 1);
	rlSetUniform (prog->loc_pointer, pointer, RL_SHADER_UNIFORM_VEC2, 1);

	rlEnableVertexArray (self->vao);
	rlEnableVertexBuffer (self->instance_vbo);
}

static void
end_pipeline (void)
{
	rlDisableVertexArray ();
	rlDisableVertexBuffer ();
	rlActiveTextureSlot (SLOT_BLUR);
	rlDisableTexture ();
	rlActiveTextureSlot (SLOT_BACKDROP);
	rlDisableTexture ();
	rlActiveTextureSlot (0);
	rlDisableTexture ();
	rlDisableShader ();
	rlSetBlendMode (RL_BLEND_ALPHA);
}

/*
 * run_callback:
 *
 * Runs a recorded immediate-mode callback with the clip that was
 * active when it was recorded applied as a scissor box, then flushes
 * raylib's batch so its output lands before the next instances.
 */
static void
run_callback (LrgPaintList *list,
              guint         index)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;

	if (lrg_paint_list_get_command_clip (list, index, &x0, &y0, &x1, &y1))
	{
		gint sx = (gint) floorf (x0);
		gint sy = (gint) floorf (y0);

		BeginScissorMode (sx, sy,
		                  MAX ((gint) ceilf (x1) - sx, 0),
		                  MAX ((gint) ceilf (y1) - sy, 0));
		lrg_paint_list_run_callback (list, index);
		EndScissorMode ();
		return;
	}

	lrg_paint_list_run_callback (list, index);
	rlDrawRenderBatchActive ();
}

/*
 * blur_levels_for:
 *
 * How many half-size levels and what tap spread approximate a blur of
 * @radius target pixels. Each level doubles the reach; the spread
 * fine-tunes within a level. Radii below half a pixel need no blur.
 */
static gint
blur_levels_for (gfloat  radius,
                 gfloat *spread)
{
	gint levels;

	if (radius < 0.5f)
	{
		*spread = 0.0f;
		return 0;
	}

	if (radius <= 4.0f)
		levels = 1;
	else if (radius <= 10.0f)
		levels = 2;
	else if (radius <= 24.0f)
		levels = 3;
	else
		levels = 4;

	*spread = CLAMP (radius / (gfloat) (1 << levels), 0.75f, 3.0f);
	return levels;
}

/*
 * canvas_box_to_pixels:
 *
 * Maps a canvas-space box through @mvp into target pixels in GL
 * window coordinates (origin bottom-left), as the box of its four
 * transformed corners.
 */
static void
canvas_box_to_pixels (const Matrix *mvp,
                      gfloat        x0,
                      gfloat        y0,
                      gfloat        x1,
                      gfloat        y1,
                      gint          width,
                      gint          height,
                      gfloat       *out)
{
	gfloat xs[4];
	gfloat ys[4];
	guint  i;

	xs[0] = x0; ys[0] = y0;
	xs[1] = x1; ys[1] = y0;
	xs[2] = x0; ys[2] = y1;
	xs[3] = x1; ys[3] = y1;

	out[0] = out[1] = G_MAXFLOAT;
	out[2] = out[3] = -G_MAXFLOAT;

	for (i = 0; i < 4; i++)
	{
		/* u_mvp * vec4 (x, y, 0, 1) with raylib's column-major matrix */
		gfloat cx = mvp->m0 * xs[i] + mvp->m4 * ys[i] + mvp->m12;
		gfloat cy = mvp->m1 * xs[i] + mvp->m5 * ys[i] + mvp->m13;
		gfloat cw = mvp->m3 * xs[i] + mvp->m7 * ys[i] + mvp->m15;
		gfloat px;
		gfloat py;

		if (fabsf (cw) < 1e-6f)
			cw = 1e-6f;
		px = (cx / cw * 0.5f + 0.5f) * (gfloat) width;
		py = (cy / cw * 0.5f + 0.5f) * (gfloat) height;
		out[0] = MIN (out[0], px);
		out[1] = MIN (out[1], py);
		out[2] = MAX (out[2], px);
		out[3] = MAX (out[3], py);
	}
}

/* blur_pass: one down (mode 0) or up (mode 1) pass from @src into @dst */
static void
blur_pass (LrgPaintRenderer *self,
           const BlurLevel  *src,
           const BlurLevel  *dst,
           gint              mode,
           gfloat            spread,
           const gint       *region,
           gint              shift,
           const gfloat     *uv_max)
{
	gfloat step[2];
	gfloat clamp_uv[2];
	gint   slot = 0;
	gint   x0;
	gint   y0;
	gint   x1;
	gint   y1;

	/* The up pass reads at half the spacing of the down pass (its
	 * taps are in units of the bigger target's texels) */
	step[0] = spread / (gfloat) src->width * (mode == 0 ? 1.0f : 0.5f);
	step[1] = spread / (gfloat) src->height * (mode == 0 ? 1.0f : 0.5f);

	/* Never read outside the part of the copy that holds the frame */
	clamp_uv[0] = uv_max[0] - 0.5f / (gfloat) src->width;
	clamp_uv[1] = uv_max[1] - 0.5f / (gfloat) src->height;

	rlEnableFramebuffer (dst->fbo);
	rlViewport (0, 0, dst->width, dst->height);

	/* Only the region the batch reads, at this level's scale */
	x0 = region[0] >> shift;
	y0 = region[1] >> shift;
	x1 = (region[2] + (1 << shift) - 1) >> shift;
	y1 = (region[3] + (1 << shift) - 1) >> shift;
	rlScissor (x0, y0, MAX (x1 - x0, 0), MAX (y1 - y0, 0));

	rlSetUniform (self->loc_blur_src, &slot, RL_SHADER_UNIFORM_INT, 1);
	rlSetUniform (self->loc_blur_step, step, RL_SHADER_UNIFORM_VEC2, 1);
	rlSetUniform (self->loc_blur_uv_max, clamp_uv, RL_SHADER_UNIFORM_VEC2, 1);
	rlSetUniform (self->loc_blur_mode, &mode, RL_SHADER_UNIFORM_INT, 1);

	rlActiveTextureSlot (0);
	rlEnableTexture (src->tex);
	rlDrawVertexArray (0, 6);
}

/*
 * capture_backdrop:
 *
 * Copies the part of the current target that the backdrop batch
 * (instances @first .. @first + @count) reads into level 0, then
 * blurs it down and back up into level 1. Returns the textures the
 * glass shader samples (sharp copy and blurred copy) and the inverse
 * padded size used to turn gl_FragCoord into texture coordinates.
 *
 * Everything outside the batch's reach is left alone, so the cost
 * scales with the area of the glass, not the target.
 */
static gboolean
capture_backdrop (LrgPaintRenderer       *self,
                  const LrgPaintInstance *instances,
                  guint                   first,
                  guint                   count,
                  guint                  *sharp_tex,
                  guint                  *blur_tex,
                  gfloat                 *inv_size)
{
	Matrix mvp;
	guint  target;
	gint   width;
	gint   height;
	gint   padded_w;
	gint   padded_h;
	gint   levels;
	gint   region[4];
	gint   margin;
	gint   align;
	gint   k;
	gfloat spread;
	gfloat max_blur = 0.0f;
	gfloat box[4];
	gfloat uv_max[2];
	gfloat bx0 = G_MAXFLOAT;
	gfloat by0 = G_MAXFLOAT;
	gfloat bx1 = -G_MAXFLOAT;
	gfloat by1 = -G_MAXFLOAT;
	guint  i;

	if (!ensure_fx (self))
		return FALSE;

	/* What we draw into: a render texture inside BeginTextureMode(),
	 * else the window. GetRenderWidth() follows BeginTextureMode() */
	target = rlGetActiveFramebuffer ();
	width = GetRenderWidth ();
	height = GetRenderHeight ();
	if (width <= 0 || height <= 0)
		return FALSE;

	/* 1. The canvas box the batch reads, and its strongest blur */
	for (i = first; i < first + count; i++)
	{
		const LrgPaintInstance *inst = &instances[i];
		gint                    prim = (gint) (inst->params[2] + 0.5f);
		gfloat                  x0;
		gfloat                  y0;
		gfloat                  x1;
		gfloat                  y1;

		if (prim != LRG_PAINT_PRIM_BLUR && prim != LRG_PAINT_PRIM_GLASS)
			continue;

		_lrg_paint_instance_get_backdrop_reach (inst, &x0, &y0, &x1, &y1);
		bx0 = MIN (bx0, x0);
		by0 = MIN (by0, y0);
		bx1 = MAX (bx1, x1);
		by1 = MAX (by1, y1);
		max_blur = MAX (max_blur, inst->params[1]);
	}
	if (bx0 > bx1)
		return FALSE;

	/* 2. Into target pixels, grown by the reach of the blur taps and
	 * snapped to the coarsest level so each level's scissor is exact.
	 * The blur radius is in canvas units: scale it like the box */
	mvp = current_mvp ();
	canvas_box_to_pixels (&mvp, bx0, by0, bx1, by1, width, height, box);
	{
		gfloat sx = (box[2] - box[0]) / MAX (bx1 - bx0, 1e-3f);
		gfloat sy = (box[3] - box[1]) / MAX (by1 - by0, 1e-3f);

		max_blur *= MAX (sx, sy);
	}
	levels = blur_levels_for (max_blur, &spread);
	align = BACKDROP_ALIGN;
	margin = (gint) ceilf (4.0f * spread * (gfloat) (1 << levels)) + 4;
	region[0] = (((gint) floorf (box[0]) - margin) / align) * align;
	region[1] = (((gint) floorf (box[1]) - margin) / align) * align;
	region[2] = (((gint) ceilf (box[2]) + margin + align - 1) / align) * align;
	region[3] = (((gint) ceilf (box[3]) + margin + align - 1) / align) * align;

	/* 3. Offscreen copy padded to a multiple of the alignment so every
	 * level halves exactly; levels are kept and only resized */
	padded_w = ((width + align - 1) / align) * align;
	padded_h = ((height + align - 1) / align) * align;
	region[0] = CLAMP (region[0], 0, padded_w);
	region[1] = CLAMP (region[1], 0, padded_h);
	region[2] = CLAMP (region[2], 0, padded_w);
	region[3] = CLAMP (region[3], 0, padded_h);
	if (region[2] <= region[0] || region[3] <= region[1])
		return FALSE;

	for (k = 0; k <= levels; k++)
	{
		if (!ensure_level (&self->levels[k], padded_w >> k, padded_h >> k))
		{
			rlEnableFramebuffer (target);
			lrg_warning (LRG_LOG_DOMAIN_PAINT, "could not create the backdrop blur targets");
			self->fx_failed = TRUE;
			return FALSE;
		}
	}

	/* 4. Copy the region of the frame drawn so far */
	rlBindFramebuffer (RL_READ_FRAMEBUFFER, target);
	rlBindFramebuffer (RL_DRAW_FRAMEBUFFER, self->levels[0].fbo);
	rlBlitFramebuffer (region[0], region[1], MIN (region[2], width), MIN (region[3], height),
	                   region[0], region[1], MIN (region[2], width), MIN (region[3], height),
	                   COLOR_BUFFER_BIT);

	/* 5. Dual-filter chain: down to the smallest level, back up to
	 * level 1 (half size), overwriting (no blending), scissored */
	uv_max[0] = (gfloat) width / (gfloat) padded_w;
	uv_max[1] = (gfloat) height / (gfloat) padded_h;
	if (levels > 0)
	{
		rlDisableColorBlend ();
		rlEnableScissorTest ();
		rlEnableShader (self->blur_shader);
		rlEnableVertexArray (self->blur_vao);

		for (k = 1; k <= levels; k++)
			blur_pass (self, &self->levels[k - 1], &self->levels[k], 0, spread,
			           region, k, uv_max);
		for (k = levels - 1; k >= 1; k--)
			blur_pass (self, &self->levels[k + 1], &self->levels[k], 1, spread,
			           region, k, uv_max);

		rlDisableVertexArray ();
		rlDisableTexture ();
		rlDisableShader ();
		rlDisableScissorTest ();
		rlEnableColorBlend ();
	}

	/* 6. Back to the caller's target and viewport */
	rlEnableFramebuffer (target);
	rlViewport (0, 0, width, height);

	*sharp_tex = self->levels[0].tex;
	*blur_tex = levels > 0 ? self->levels[1].tex : self->levels[0].tex;
	inv_size[0] = 1.0f / (gfloat) padded_w;
	inv_size[1] = 1.0f / (gfloat) padded_h;
	self->backdrop_passes++;
	return TRUE;
}

gboolean
lrg_paint_renderer_render (LrgPaintRenderer *self,
                           LrgPaintList     *list)
{
	const LrgPaintInstance *instances;
	const PaintProgram     *prog;
	guint                   n_instances;
	guint                   n_commands;
	guint                   i;
	gboolean                pipeline_bound = FALSE;

	g_return_val_if_fail (LRG_IS_PAINT_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_PAINT_LIST (list), FALSE);

	self->draw_calls = 0;
	self->backdrop_passes = 0;

	if (!IsWindowReady ())
		return FALSE;

	n_commands = lrg_paint_list_get_n_commands (list);
	if (n_commands == 0)
		return TRUE;

	if (!ensure_gl (self))
		return FALSE;
	prog = active_program (self);

	/* Anything raylib has queued must land underneath us */
	rlDrawRenderBatchActive ();

	/* Upload the whole frame once; batches re-point into it */
	instances = lrg_paint_list_get_instances (list, &n_instances);
	if (n_instances > 0)
	{
		ensure_capacity (self, n_instances);
		rlUpdateVertexBuffer (self->instance_vbo, instances,
		                      (gint) (n_instances * sizeof (LrgPaintInstance)), 0);
	}

	for (i = 0; i < n_commands; i++)
	{
		guint               first;
		guint               count;
		guint               texture;
		LrgPaintCommandKind kind;

		kind = lrg_paint_list_get_command (list, i, &first, &count, &texture);

		if (kind == LRG_PAINT_COMMAND_CALLBACK)
		{
			if (pipeline_bound)
			{
				end_pipeline ();
				pipeline_bound = FALSE;
			}
			run_callback (list, i);
			continue;
		}

		if (count == 0)
			continue;

		if (kind == LRG_PAINT_COMMAND_BACKDROP)
		{
			guint  sharp_tex = 0;
			guint  blur_tex = 0;
			gfloat inv_size[2];

			/* The capture runs its own passes: leave our pipeline */
			if (pipeline_bound)
			{
				end_pipeline ();
				pipeline_bound = FALSE;
			}
			if (!capture_backdrop (self, instances, first, count,
			                       &sharp_tex, &blur_tex, inv_size))
				continue;

			begin_pipeline (self, prog, list);
			pipeline_bound = TRUE;
			rlSetUniform (prog->loc_backdrop_inv, inv_size, RL_SHADER_UNIFORM_VEC2, 1);
			rlActiveTextureSlot (SLOT_BACKDROP);
			rlEnableTexture (sharp_tex);
			rlActiveTextureSlot (SLOT_BLUR);
			rlEnableTexture (blur_tex);
		}
		else if (!pipeline_bound)
		{
			begin_pipeline (self, prog, list);
			pipeline_bound = TRUE;
		}

		rlActiveTextureSlot (0);
		rlEnableTexture (texture != 0 ? texture : rlGetTextureIdDefault ());
		point_instance_attributes (first);
		rlDrawVertexArrayInstanced (0, 6, (gint) count);
		self->draw_calls++;
	}

	if (pipeline_bound)
		end_pipeline ();

	return TRUE;
}
