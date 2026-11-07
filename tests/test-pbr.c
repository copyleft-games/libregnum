/* test-pbr.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 lit shader: the GGX/Smith/Schlick metallic-roughness model
 * (CPU reference lrg_light_set_brdf() against the shader's output on a
 * lit quad), spot lights (cone curve, API, CPU sample, GPU cone falloff,
 * spot shadow maps with PCF), the material contract uniforms (pbrParams,
 * ORM map, unlit flag), render layers, the hook objects (environment,
 * SSAO, decals bound and inactive), the GLSL library for user shaders
 * and the light component's additions.  The GPU cases draw raylib
 * planes with the shader directly (no static batch), so they depend on
 * the light set alone.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static const GrlColor white = { 255, 255, 255, 255 };
static const GrlColor black = { 0, 0, 0, 255 };

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-pbr-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

/* sRGB as gamma 2.2, as the shader */
static gfloat
to_linear (gfloat c)
{
    return powf (MAX (c, 0.0f), 2.2f);
}

static gfloat
to_display (gfloat c)
{
    return powf (MAX (c, 0.0f), 1.0f / 2.2f);
}

static void
normalize3 (gfloat *v)
{
    gfloat len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

/* ------------------------------------------------------------------------ */
/* Headless: the BRDF, the cone, the API                                    */
/* ------------------------------------------------------------------------ */

/* Known values of the model: a white rough dielectric facing the light
 * and the eye reflects (1 - F0) + D G F0 pi / 4; metals have no diffuse;
 * nothing is reflected from behind; rougher surfaces have a weaker peak. */
static void
test_pbr_brdf_reference (void)
{
    gfloat n[3] = { 0, 1, 0 };
    gfloat l[3] = { 0, 1, 0 };
    gfloat back[3] = { 0, -1, 0 };
    gfloat albedo[3] = { 1, 1, 1 };
    gfloat red[3] = { 1, 0, 0 };
    gfloat out[3];
    gfloat smooth[3];
    gfloat rough[3];
    gfloat d;
    gfloat expected;

    /* roughness 0.5: alpha^2 = 0.0625, D(1) = 1 / (pi alpha^2), G = 1 */
    lrg_light_set_brdf (n, n, l, albedo, 0.0f, 0.5f, out);
    d = 1.0f / ((gfloat)G_PI * 0.0625f);
    expected = (1.0f - 0.04f) + d * 0.04f / 4.0f * (gfloat)G_PI;
    g_assert_cmpfloat_with_epsilon (out[0], expected, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[1], out[0], 1e-6);

    /* metallic red: F0 = albedo, no diffuse, so green is 0 at normal
     * incidence */
    lrg_light_set_brdf (n, n, l, red, 1.0f, 0.5f, out);
    g_assert_cmpfloat_with_epsilon (out[0], d * 1.0f / 4.0f * (gfloat)G_PI, 1e-3);
    g_assert_cmpfloat_with_epsilon (out[1], 0.0f, 1e-6);

    /* from behind: nothing */
    lrg_light_set_brdf (n, n, back, albedo, 0.0f, 0.5f, out);
    g_assert_cmpfloat (out[0], ==, 0.0f);

    /* a light at 60 degrees, the eye above: the smooth surface's peak is
     * elsewhere (less specular here) than the rough surface's spread */
    {
        gfloat l60[3] = { 0.8660254f, 0.5f, 0.0f };

        lrg_light_set_brdf (n, n, l60, albedo, 1.0f, 0.1f, smooth);
        lrg_light_set_brdf (n, n, l60, albedo, 1.0f, 0.9f, rough);
        g_assert_cmpfloat (smooth[0], <, rough[0]);
        /* Lambert part of a dielectric scales with N.L */
        lrg_light_set_brdf (n, n, l60, albedo, 0.0f, 1.0f, out);
        g_assert_cmpfloat (out[0], >, 0.4f);
        g_assert_cmpfloat (out[0], <, 0.6f);
    }
}

static void
test_pbr_spot_factor (void)
{
    gfloat c_mid;

    g_assert_cmpfloat_with_epsilon (lrg_light_set_spot_factor (20, 30, 1.0f), 1.0f, 1e-6);
    g_assert_cmpfloat_with_epsilon (lrg_light_set_spot_factor (20, 30, cosf (G_PI / 180.0 * 20.0)),
                                    1.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (lrg_light_set_spot_factor (20, 30, cosf (G_PI / 180.0 * 31.0)),
                                    0.0f, 1e-6);
    /* halfway between the cosines: smoothstep (0.5) = 0.5 */
    c_mid = 0.5f * (cosf (G_PI / 180.0 * 20.0) + cosf (G_PI / 180.0 * 30.0));
    g_assert_cmpfloat_with_epsilon (lrg_light_set_spot_factor (20, 30, c_mid), 0.5f, 1e-3);
    /* inner == outer: a hard edge, still finite */
    g_assert_cmpfloat (lrg_light_set_spot_factor (30, 30, 1.0f), ==, 1.0f);
    g_assert_cmpfloat (lrg_light_set_spot_factor (30, 30, 0.0f), ==, 0.0f);
}

static void
test_pbr_spot_api (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlVector3 down = { 0, -2, 0 };
    GrlVector3 up = { 0, 1, 0 };
    GrlVector3 below = { 0, 0, 0 };
    GrlVector3 aside = { 3, 0, 0 };
    gfloat     dir[3];
    gfloat     inner;
    gfloat     outer;
    gfloat     rgb[3];
    guint      spot;
    guint      point;

    lrg_light_set_set_ambient (lights, &black);
    spot = lrg_light_set_add_spot_light (lights, 0, 2, 0, &down, &white, 10, 20, 30);
    g_assert_cmpuint (spot, !=, LRG_LIGHT_SET_INVALID_LIGHT);
    g_assert_true (lrg_light_set_get_spot_light (lights, spot, dir, &inner, &outer));
    g_assert_cmpfloat_with_epsilon (dir[1], -1.0f, 1e-6);
    g_assert_cmpfloat (inner, ==, 20.0f);
    g_assert_cmpfloat (outer, ==, 30.0f);

    /* the outer angle never falls below the inner one */
    lrg_light_set_set_spot_light (lights, spot, &down, 40, 10);
    lrg_light_set_get_spot_light (lights, spot, NULL, &inner, &outer);
    g_assert_cmpfloat (outer, >=, inner);
    lrg_light_set_set_spot_light (lights, spot, &down, 20, 30);

    /* the CPU sample: lit under the cone, dark beside it (56 degrees off
     * axis) */
    lrg_light_set_select (lights, 0, 10, 0);
    lrg_light_set_sample (lights, &below, &up, rgb);
    g_assert_cmpfloat (rgb[0], >, 0.5f);
    lrg_light_set_sample (lights, &aside, &up, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.0f, 1e-6);

    /* back to a point light: the side is lit */
    lrg_light_set_set_spot_light (lights, spot, NULL, 0, 0);
    g_assert_false (lrg_light_set_get_spot_light (lights, spot, NULL, NULL, NULL));
    lrg_light_set_select (lights, 0, 10, 0);
    lrg_light_set_sample (lights, &aside, &up, rgb);
    g_assert_cmpfloat (rgb[0], >, 0.0f);

    /* bias and layers */
    point = lrg_light_set_add_point_light (lights, 5, 1, 5, &white, 4);
    g_assert_cmpfloat (lrg_light_set_get_light_shadow_bias (lights, point), ==, 0.0f);
    lrg_light_set_set_light_shadow_bias (lights, point, 0.2f);
    g_assert_cmpfloat (lrg_light_set_get_light_shadow_bias (lights, point), ==, 0.2f);
    g_assert_cmpuint (lrg_light_set_get_light_layers (lights, point), ==, LRG_RENDER_LAYERS_MASK_ALL);
    lrg_light_set_set_light_layers (lights, point, 0x00010002u);
    g_assert_cmpuint (lrg_light_set_get_light_layers (lights, point), ==, 0x00010002u);
    g_assert_cmpuint (lrg_light_set_get_draw_layers (lights), ==, LRG_RENDER_LAYERS_MASK_ALL);
    lrg_light_set_set_draw_layers (lights, 1);
    g_assert_cmpuint (lrg_light_set_get_draw_layers (lights), ==, 1);
    g_assert_false (lrg_light_set_get_output_linear (lights));
    lrg_light_set_set_output_linear (lights, TRUE);
    g_assert_true (lrg_light_set_get_output_linear (lights));
}

/* light descriptions and the light component */
static void
test_pbr_light_desc (void)
{
    g_autoptr(LrgLightSet)       lights = lrg_light_set_new ();
    g_autoptr(LrgLightComponent) comp = NULL;
    LrgLight3DDesc desc;
    gfloat inner;
    gfloat outer;
    gfloat dir[3];
    guint  id;

    comp = lrg_light_component_new_with_kind (LRG_LIGHT3D_SPOT);
    lrg_light_component_set_spot_angles (comp, 15, 25);
    lrg_light_component_get_spot_angles (comp, &inner, &outer);
    g_assert_cmpfloat (inner, ==, 15.0f);
    g_assert_cmpfloat (outer, ==, 25.0f);
    lrg_light_component_set_cast_shadows (comp, TRUE);
    lrg_light_component_set_shadow_bias (comp, 0.3f);
    g_assert_cmpfloat (lrg_light_component_get_shadow_bias (comp), ==, 0.3f);
    lrg_light_component_set_layer_mask (comp, 4);
    g_assert_cmpuint (lrg_light_component_get_layer_mask (comp), ==, 4);
    lrg_light_component_set_range (comp, 7.0f);
    lrg_light_component_set_intensity (comp, 2.0f);

    /* no entity: at the origin, shining down -Z */
    id = lrg_light_component_add_to_light_set (comp, lights);
    g_assert_cmpuint (id, !=, LRG_LIGHT_SET_INVALID_LIGHT);
    g_assert_true (lrg_light_set_get_spot_light (lights, id, dir, &inner, &outer));
    g_assert_cmpfloat_with_epsilon (inner, 15.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (outer, 25.0f, 1e-3);
    g_assert_cmpuint (lrg_light_set_get_point_light_shadow (lights, id), ==, 1);
    g_assert_cmpfloat_with_epsilon (lrg_light_set_get_light_shadow_bias (lights, id), 0.3f, 1e-6);
    g_assert_cmpuint (lrg_light_set_get_light_layers (lights, id), ==, 4);

    /* a directional description sets the directional light */
    lrg_light_component_fill_desc (comp, &desc);
    desc.kind = LRG_LIGHT3D_DIRECTIONAL;
    desc.direction[0] = 0;
    desc.direction[1] = -1;
    desc.direction[2] = 0;
    g_assert_cmpuint (lrg_light_set_add_light_desc (lights, &desc), ==, LRG_LIGHT_SET_INVALID_LIGHT);
    g_assert_true (lrg_light_set_get_directional (lights, dir, NULL));
    g_assert_cmpfloat_with_epsilon (dir[1], -1.0f, 1e-6);
    /* disabled: nothing */
    desc.kind = LRG_LIGHT3D_POINT;
    desc.enabled = FALSE;
    g_assert_cmpuint (lrg_light_set_add_light_desc (lights, &desc), ==, LRG_LIGHT_SET_INVALID_LIGHT);
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 1);
}

/* spot shadow planning: spots compete for spot maps only, points for
 * cubes only; passes come after the cubes */
static void
test_pbr_spot_shadow_plan (void)
{
    g_autoptr(LrgLightSet)       lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 down = { 0, -1, 0 };
    gfloat camera[3] = { 0, 10, 0.01f };
    gfloat forward[3] = { 0, -1, 0 };
    gfloat up[3] = { 0, 0, -1 };
    gfloat vp[16];
    gfloat on_axis[3] = { 0, 0, 0 };
    gfloat clip[3];
    guint  res;
    guint  max;
    guint  spot;
    guint  spot2;
    guint  point;
    guint  index;

    spot = lrg_light_set_add_spot_light (lights, 0, 4, 0, &down, &white, 12, 20, 40);
    spot2 = lrg_light_set_add_spot_light (lights, 30, 4, 0, &down, &white, 12, 20, 40);
    point = lrg_light_set_add_point_light (lights, 0, 3, 3, &white, 6);
    lrg_light_set_set_point_light_shadow (lights, spot, 1);
    lrg_light_set_set_point_light_shadow (lights, spot2, 5);
    lrg_light_set_set_point_light_shadow (lights, point, 1);
    lrg_light_set_set_light_shadow_bias (lights, spot, 0.1f);

    lrg_shadow_renderer_set_sun (shadows, 0, 1, 10, 0.5f);
    lrg_shadow_renderer_set_points (shadows, 64, 2, 0);
    lrg_shadow_renderer_set_spots (shadows, 128, 1, 0);
    lrg_shadow_renderer_get_spots (shadows, &res, &max);
    g_assert_cmpuint (res, ==, 128);
    g_assert_cmpuint (max, ==, 1);
    g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 30, 1, 0.1f),
                      ==, 6 + 1);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_count (shadows), ==, 1);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_light (shadows, 0), ==, point);
    /* the higher priority spot wins the one map */
    g_assert_cmpuint (lrg_shadow_renderer_get_spot_count (shadows), ==, 1);
    g_assert_cmpuint (lrg_shadow_renderer_get_spot_light (shadows, 0), ==, spot2);
    g_assert_cmpint (lrg_shadow_renderer_get_spot_slot (shadows, spot2), ==, 0);
    g_assert_cmpint (lrg_shadow_renderer_get_spot_slot (shadows, spot), ==, LRG_SHADOW_NO_SLOT);
    g_assert_cmpint (lrg_shadow_renderer_get_pass (shadows, 6, &index, NULL, vp), ==,
                     LRG_SHADOW_PASS_SPOT);
    g_assert_cmpuint (index, ==, 0);
    g_assert_cmpint (lrg_shadow_renderer_get_pass (shadows, 7, NULL, NULL, NULL), ==,
                     LRG_SHADOW_PASS_NONE);

    /* two spots: both, nearest-first among equal priority */
    lrg_shadow_renderer_set_spots (shadows, 128, 2, 0);
    lrg_light_set_set_point_light_shadow (lights, spot2, 1);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 30, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_spot_light (shadows, 0), ==, spot);
    g_assert_cmpuint (lrg_shadow_renderer_get_spot_light (shadows, 1), ==, spot2);

    /* the spot camera: the point on the axis lands in the map's centre */
    {
        gfloat light[3] = { 0, 4, 0 };
        gfloat dirv[3] = { 0, -1, 0 };
        gfloat view[16];
        gfloat proj[16];
        gfloat m[16];

        g_assert_true (lrg_shadow_spot_matrices (light, dirv, 40, 12, view, proj));
        lrg_shadow_multiply (proj, view, m);
        lrg_shadow_transform_point (m, on_axis, clip);
        g_assert_cmpfloat_with_epsilon (clip[0], 0.0f, 1e-4);
        g_assert_cmpfloat_with_epsilon (clip[1], 0.0f, 1e-4);
        g_assert_cmpfloat (clip[2], >, -1.0f);
        g_assert_cmpfloat (clip[2], <, 1.0f);
        dirv[1] = 0.0f;
        g_assert_false (lrg_shadow_spot_matrices (light, dirv, 40, 12, view, proj));
    }
}

static void
test_pbr_library_text (void)
{
    const gchar *lib = lrg_light_set_get_glsl_library ();
    const gchar *vs = lrg_light_set_get_vertex_source ();

    g_assert_nonnull (strstr (lib, "vec3 lrg_light_pbr(vec3 p, vec3 n, vec3 v, vec4 albedo"));
    g_assert_nonnull (strstr (lib, "LrgSurface lrg_surface("));
    g_assert_nonnull (strstr (lib, "vec4 lrg_light_shade("));
    g_assert_nonnull (strstr (lib, "uniform vec4 pbrParams;"));
    g_assert_nonnull (strstr (lib, "uniform sampler2D ormMap;"));
    g_assert_nonnull (strstr (lib, "lrg_env_active()"));
    g_assert_nonnull (strstr (lib, "lrg_ssao()"));
    g_assert_nonnull (strstr (lib, "lrg_decals_apply("));
    g_assert_null (strstr (lib, "#version"));
    g_assert_null (strstr (lib, "void main"));
    g_assert_true (g_str_has_prefix (vs, "#version 330\n"));
    g_assert_nonnull (strstr (vs, "out vec3 fragNormal;"));
}

/* ------------------------------------------------------------------------ */
/* GPU                                                                      */
/* ------------------------------------------------------------------------ */

#define SHOT_SIZE (32)

/* A 20 x 20 plane at y = 0 facing up (and an optional 1.6 x 1.6 one at
 * @occluder_y, -1 for none), seen from straight above. */
typedef struct
{
    Model floor;
    Model occluder;
} Scene;

static void
scene_init (Scene *scene)
{
    scene->floor = LoadModelFromMesh (GenMeshPlane (20, 20, 1, 1));
    scene->occluder = LoadModelFromMesh (GenMeshPlane (1.6f, 1.6f, 1, 1));
}

static void
scene_clear (Scene *scene)
{
    /* the models do not own the light set's shaders */
    scene->floor.materials[0].shader.id = rlGetShaderIdDefault ();
    scene->floor.materials[0].shader.locs = rlGetShaderLocsDefault ();
    scene->occluder.materials[0].shader.id = rlGetShaderIdDefault ();
    scene->occluder.materials[0].shader.locs = rlGetShaderLocsDefault ();
    UnloadModel (scene->floor);
    UnloadModel (scene->occluder);
}

static void
set_shader (Scene     *scene,
            GrlShader *shader)
{
    Shader *handle = grl_shader_get_handle (shader);

    scene->floor.materials[0].shader = *handle;
    scene->occluder.materials[0].shader = *handle;
}

/* render: the floor lit by the active shader; returns the image */
static Image
render (Scene *scene)
{
    RenderTexture2D rt = LoadRenderTexture (SHOT_SIZE, SHOT_SIZE);
    Camera3D        camera;
    Image           shot;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 10, 0.01f };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 0, -1 };
    camera.fovy = 30.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    /* the plane's winding faces it away from raylib's culling here; the
     * normal (up) is what lighting reads */
    rlDisableBackfaceCulling ();
    DrawModel (scene->floor, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);

    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
    EndMode3D ();
    EndTextureMode ();
    shot = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    return shot;
}

static Color
render_centre (Scene *scene)
{
    Image shot = render (scene);
    Color c = GetImageColor (shot, SHOT_SIZE / 2, SHOT_SIZE / 2);

    UnloadImage (shot);
    return c;
}

static void
set_pbr (GrlShader *shader,
         gfloat     metallic,
         gfloat     roughness,
         gfloat     occlusion,
         gint       flags)
{
    Shader *handle = grl_shader_get_handle (shader);
    gfloat  params[4] = { metallic, roughness, occlusion, 1.0f };

    SetShaderValue (*handle, GetShaderLocation (*handle, "pbrParams"), params, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, GetShaderLocation (*handle, "mapFlags"), &flags, SHADER_UNIFORM_INT);
}

static gboolean
gpu_ready (LrgLightSet *lights)
{
    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return FALSE;
    }
    if (lrg_light_set_get_shader (lights) == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return FALSE;
    }
    return TRUE;
}

/* The shader's output on a quad lit by a slanted sun matches the CPU
 * reference within rounding, for several metallic / roughness pairs. */
static void
test_pbr_gpu_brdf (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor   sun = { 230, 200, 160, 255 };
    GrlColor   ambient = { 40, 50, 60, 255 };
    GrlVector3 dir = { -0.5f, -1.0f, -0.3f };
    gfloat     n[3] = { 0, 1, 0 };
    gfloat     v[3] = { 0, 10, 0.01f };
    gfloat     l[3];
    gfloat     albedo[3] = { 1, 1, 1 };
    gfloat     cases[4][2] = { { 0.0f, 0.5f }, { 0.0f, 0.15f }, { 1.0f, 0.3f }, { 0.5f, 0.8f } };
    GrlShader *shader;
    Scene      scene;
    guint      i;

    if (!gpu_ready (lights))
        return;
    shader = lrg_light_set_get_shader (lights);
    scene_init (&scene);
    set_shader (&scene, shader);

    l[0] = -dir.x;
    l[1] = -dir.y;
    l[2] = -dir.z;
    normalize3 (l);
    normalize3 (v);
    lrg_light_set_set_ambient (lights, &ambient);
    lrg_light_set_set_directional (lights, &dir, &sun);
    for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        gfloat ref[3];
        Color  px;
        guint8 expected[3];
        guint  c;

        g_assert_true (lrg_light_set_apply (lights, 0, 10, 0.01));
        set_pbr (shader, cases[i][0], cases[i][1], 1.0f, 0);
        px = render_centre (&scene);
        lrg_light_set_brdf (n, v, l, albedo, cases[i][0], cases[i][1], ref);
        expected[0] = (guint8)CLAMP (to_display (ref[0] * to_linear (sun.r / 255.0f)
                                                 + to_linear (ambient.r / 255.0f)) * 255.0f + 0.5f, 0, 255);
        expected[1] = (guint8)CLAMP (to_display (ref[1] * to_linear (sun.g / 255.0f)
                                                 + to_linear (ambient.g / 255.0f)) * 255.0f + 0.5f, 0, 255);
        expected[2] = (guint8)CLAMP (to_display (ref[2] * to_linear (sun.b / 255.0f)
                                                 + to_linear (ambient.b / 255.0f)) * 255.0f + 0.5f, 0, 255);
        for (c = 0; c < 3; c++)
        {
            guint8 got = c == 0 ? px.r : c == 1 ? px.g : px.b;

            g_test_message ("case %u channel %u: shader %u, reference %u", i, c, got, expected[c]);
            g_assert_cmpint (ABS ((gint)got - (gint)expected[c]), <=, 4);
        }
    }
    set_pbr (shader, LRG_LIGHT_SET_DEFAULT_METALLIC, LRG_LIGHT_SET_DEFAULT_ROUGHNESS, 1.0f, 0);
    scene_clear (&scene);
}

/* A spot shining down: lit under its cone, dark outside; a point light
 * at the same place lights both; the draw's layers drop a light. */
static void
test_pbr_gpu_spot_cone (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlVector3 down = { 0, -1, 0 };
    Scene      scene;
    Image      shot;
    Color      centre;
    Color      side;
    guint      spot;

    if (!gpu_ready (lights))
        return;
    scene_init (&scene);
    set_shader (&scene, lrg_light_set_get_shader (lights));
    lrg_light_set_set_ambient (lights, &black);

    /* the floor's view is about 5.4 units across 32 pixels: pixel 28 is
     * 2 units off the axis, 45 degrees from a light 2 units up */
    spot = lrg_light_set_add_spot_light (lights, 0, 2, 0, &down, &white, 8, 15, 25);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    shot = render (&scene);
    centre = GetImageColor (shot, SHOT_SIZE / 2, SHOT_SIZE / 2);
    side = GetImageColor (shot, 28, SHOT_SIZE / 2);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, >, 120);
    g_assert_cmpuint (side.r, <, 5);
    g_assert_cmpuint (side.b, <, 5);     /* the floor, not the clear colour */

    lrg_light_set_set_spot_light (lights, spot, NULL, 0, 0);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    shot = render (&scene);
    side = GetImageColor (shot, 28, SHOT_SIZE / 2);
    UnloadImage (shot);
    g_assert_cmpuint (side.r, >, 40);

    /* the same through the grid's light texture */
    lrg_light_set_set_spot_light (lights, spot, &down, 15, 25);
    lrg_light_set_set_grid (lights, 4.0f, 16);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    shot = render (&scene);
    centre = GetImageColor (shot, SHOT_SIZE / 2, SHOT_SIZE / 2);
    side = GetImageColor (shot, 28, SHOT_SIZE / 2);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, >, 120);
    g_assert_cmpuint (side.r, <, 5);

    /* layers: a light on layer 1 only does not light a layer-0 draw */
    lrg_light_set_set_light_layers (lights, spot, 1u << 1);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    lrg_light_set_set_draw_layers (lights, 1u << 0);
    centre = render_centre (&scene);
    g_assert_cmpuint (centre.r, <, 5);
    lrg_light_set_set_draw_layers (lights, LRG_RENDER_LAYERS_MASK_ALL);
    centre = render_centre (&scene);
    g_assert_cmpuint (centre.r, >, 120);

    scene_clear (&scene);
}

/* render_casters: every pass with the floor and the occluder (2 up) */
static void
render_casters (LrgShadowRenderer *shadows,
                Scene             *scene)
{
    GrlShader *caster = lrg_shadow_renderer_get_caster_shader (shadows);
    guint      pass;

    g_assert_nonnull (caster);
    set_shader (scene, caster);
    for (pass = 0; pass < lrg_shadow_renderer_get_pass_count (shadows); pass++)
    {
        g_assert_true (lrg_shadow_renderer_begin_pass (shadows, pass));
        DrawModel (scene->floor, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
        DrawModel (scene->occluder, (Vector3){ 0, 2, 0 }, 1.0f, WHITE);
        lrg_shadow_renderer_end_pass (shadows);
    }
    g_assert_true (lrg_shadow_renderer_is_ready (shadows));
}

/* A shadowed spot above an occluder: the floor under it is dark, the
 * floor beside it lit, with the filter's soft edge in between. */
static void
test_pbr_gpu_spot_shadow (void)
{
    g_autoptr(LrgLightSet)       lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 down = { 0, -1, 0.001f };
    gfloat camera[3] = { 0, 10, 0.01f };
    gfloat forward[3] = { 0, -1, -0.001f };
    gfloat up[3] = { 0, 0, -1 };
    Scene  scene;
    Image  shot;
    Color  centre;
    Color  side;
    guint  spot;

    if (!gpu_ready (lights))
        return;
    scene_init (&scene);
    lrg_light_set_set_ambient (lights, &black);
    spot = lrg_light_set_add_spot_light (lights, 0, 4, 0, &down, &white, 12, 50, 60);
    lrg_light_set_set_point_light_shadow (lights, spot, 1);
    lrg_light_set_set_shadow_renderer (lights, shadows);
    lrg_shadow_renderer_set_sun (shadows, 0, 1, 10, 0.5f);
    lrg_shadow_renderer_set_spots (shadows, 256, 1, 0);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 30, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass_count (shadows), ==, 1);

    /* without the pass: no shadow */
    set_shader (&scene, lrg_light_set_get_shader (lights));
    lrg_light_set_apply (lights, 0, 10, 0.01);
    centre = render_centre (&scene);
    g_assert_cmpuint (centre.r, >, 100);

    render_casters (shadows, &scene);
    set_shader (&scene, lrg_light_set_get_shader (lights));
    lrg_light_set_apply (lights, 0, 10, 0.01);
    shot = render (&scene);
    centre = GetImageColor (shot, SHOT_SIZE / 2, SHOT_SIZE / 2);
    side = GetImageColor (shot, 30, SHOT_SIZE / 2);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, <, 10);     /* under the occluder */
    g_assert_cmpuint (side.r, >, 30);       /* outside its shadow, inside the cone */
    scene_clear (&scene);
}

/* The material contract: ORM occlusion, the unlit flag. */
static void
test_pbr_gpu_material_flags (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor   grey = { 128, 128, 128, 255 };
    GrlShader *shader;
    Scene      scene;
    Color      px;
    guint      orm;
    guint8     texel[4] = { 0, 255, 0, 255 };   /* occlusion 0, rough 1, metal 0 */

    if (!gpu_ready (lights))
        return;
    shader = lrg_light_set_get_shader (lights);
    scene_init (&scene);
    set_shader (&scene, shader);
    lrg_light_set_set_ambient (lights, &grey);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    px = render_centre (&scene);
    g_assert_cmpint (ABS ((gint)px.r - 128), <=, 3);

    /* ORM occlusion 0 at full strength: no ambient */
    orm = rlLoadTexture (texel, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    rlActiveTextureSlot (LRG_LIGHT_SET_ORM_UNIT);
    rlEnableTexture (orm);
    rlActiveTextureSlot (0);
    set_pbr (shader, 0.0f, 1.0f, 1.0f, LRG_LIGHT_SET_MAP_ORM);
    px = render_centre (&scene);
    g_assert_cmpuint (px.r, <, 5);
    g_assert_cmpuint (px.b, <, 5);
    /* strength 0: the occlusion is ignored */
    set_pbr (shader, 0.0f, 1.0f, 0.0f, LRG_LIGHT_SET_MAP_ORM);
    px = render_centre (&scene);
    g_assert_cmpint (ABS ((gint)px.r - 128), <=, 3);
    rlActiveTextureSlot (LRG_LIGHT_SET_ORM_UNIT);
    rlDisableTexture ();
    rlActiveTextureSlot (0);
    rlUnloadTexture (orm);

    /* unlit: the albedo whatever the lights */
    lrg_light_set_set_ambient (lights, &black);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    set_pbr (shader, 0.0f, 0.5f, 1.0f, LRG_LIGHT_SET_MAP_UNLIT);
    px = render_centre (&scene);
    g_assert_cmpuint (px.r, >, 250);
    set_pbr (shader, LRG_LIGHT_SET_DEFAULT_METALLIC, LRG_LIGHT_SET_DEFAULT_ROUGHNESS, 1.0f, 0);
    px = render_centre (&scene);
    g_assert_cmpuint (px.r, <, 5);

    /* linear output: grey ambient on white comes out as its linear value */
    lrg_light_set_set_ambient (lights, &grey);
    lrg_light_set_set_output_linear (lights, TRUE);
    lrg_light_set_apply (lights, 0, 10, 0.01);
    px = render_centre (&scene);
    g_assert_cmpint (ABS ((gint)px.r - (gint)(to_linear (128.0f / 255.0f) * 255.0f + 0.5f)), <=, 3);
    lrg_light_set_set_output_linear (lights, FALSE);
    scene_clear (&scene);
}

/* The hook objects: bound by apply, their getters, a draw still works
 * with them set and after they are cleared (inactive state). */
static void
test_pbr_gpu_hooks (void)
{
    g_autoptr(LrgLightSet)    lights = lrg_light_set_new ();
    g_autoptr(LrgEnvironment) env = lrg_environment_new ();
    g_autoptr(LrgSsao)        ssao = lrg_ssao_new ();
    g_autoptr(LrgDecalSet)    decals = lrg_decal_set_new ();
    GrlColor grey = { 128, 128, 128, 255 };
    Scene    scene;
    Color    px;

    /* headless part: references and getters */
    g_assert_null (lrg_light_set_get_environment (lights));
    lrg_light_set_set_environment (lights, env);
    lrg_light_set_set_ssao (lights, ssao);
    lrg_light_set_set_decals (lights, decals);
    g_assert_true (lrg_light_set_get_environment (lights) == env);
    g_assert_true (lrg_light_set_get_ssao (lights) == ssao);
    g_assert_true (lrg_light_set_get_decals (lights) == decals);

    if (!gpu_ready (lights))
        return;
    scene_init (&scene);
    set_shader (&scene, lrg_light_set_get_shader (lights));
    lrg_light_set_set_ambient (lights, &grey);
    g_assert_true (lrg_light_set_apply (lights, 0, 10, 0.01));
    px = render_centre (&scene);
    g_assert_cmpuint (px.b, <, 250);     /* the floor drew (not the clear colour) */
    g_assert_cmpuint (px.a, ==, 255);

    /* inactive hooks: the flat ambient again */
    lrg_light_set_set_environment (lights, NULL);
    lrg_light_set_set_ssao (lights, NULL);
    lrg_light_set_set_decals (lights, NULL);
    g_assert_true (lrg_light_set_apply (lights, 0, 10, 0.01));
    px = render_centre (&scene);
    g_assert_cmpint (ABS ((gint)px.r - 128), <=, 3);
    scene_clear (&scene);
}

/* A user shader built from the library compiles and its lrg_light_pbr()
 * lights like the built-in shader. */
static void
test_pbr_gpu_library (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(GrlShader)   user = NULL;
    g_autoptr(GError)      error = NULL;
    g_autofree gchar      *fs = NULL;
    GrlColor   sun = { 200, 200, 200, 255 };
    GrlVector3 dir = { 0, -1, 0 };
    Scene      scene;
    Color      built_in;
    Color      own;
    Shader    *handle;


    if (!gpu_ready (lights))
        return;
    fs = g_strconcat ("#version 330\n"
                      "in vec3 fragPosition;\n"
                      "in vec2 fragTexCoord;\n"
                      "in vec4 fragColor;\n"
                      "in vec3 fragNormal;\n"
                      "out vec4 finalColor;\n",
                      lrg_light_set_get_glsl_library (),
                      "void main() {\n"
                      "    vec3 n = normalize(fragNormal);\n"
                      "    vec3 v = normalize(mapCamera - fragPosition);\n"
                      "    vec4 albedo = vec4(lrg_to_linear(colDiffuse.rgb), 1.0);\n"
                      "    vec3 c = lrg_light_pbr(fragPosition, n, v, albedo, pbrParams.x, pbrParams.y, 1.0);\n"
                      "    finalColor = vec4(lrg_to_display(c), 1.0);\n"
                      "}\n", NULL);
    user = grl_shader_new_from_memory (lrg_light_set_get_vertex_source (), fs, &error);
    g_assert_no_error (error);
    g_assert_nonnull (user);
    handle = grl_shader_get_handle (user);
    g_assert_cmpuint (handle->id, !=, rlGetShaderIdDefault ());

    scene_init (&scene);
    lrg_light_set_set_ambient (lights, &black);
    lrg_light_set_set_directional (lights, &dir, &sun);
    set_shader (&scene, lrg_light_set_get_shader (lights));
    lrg_light_set_apply (lights, 0, 10, 0.01);
    built_in = render_centre (&scene);

    /* the user program gets every uniform from the light set */
    g_assert_true (lrg_light_set_bind_shader (lights, user));
    g_assert_true (lrg_light_set_bind_shader (lights, user));      /* cached locations */
    set_shader (&scene, user);
    own = render_centre (&scene);
    g_assert_cmpint (ABS ((gint)own.r - (gint)built_in.r), <=, 2);
    g_assert_cmpuint (own.r, >, 150);
    lrg_light_set_forget_shader (lights, user);
    /* the light set's own program through the same call */
    g_assert_true (lrg_light_set_bind_shader (lights, lrg_light_set_get_shader (lights)));
    scene_clear (&scene);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/pbr/brdf-reference", test_pbr_brdf_reference);
    g_test_add_func ("/pbr/spot-factor", test_pbr_spot_factor);
    g_test_add_func ("/pbr/spot-api", test_pbr_spot_api);
    g_test_add_func ("/pbr/light-desc", test_pbr_light_desc);
    g_test_add_func ("/pbr/spot-shadow-plan", test_pbr_spot_shadow_plan);
    g_test_add_func ("/pbr/library-text", test_pbr_library_text);
    g_test_add_func ("/pbr/gpu/brdf", test_pbr_gpu_brdf);
    g_test_add_func ("/pbr/gpu/spot-cone", test_pbr_gpu_spot_cone);
    g_test_add_func ("/pbr/gpu/spot-shadow", test_pbr_gpu_spot_shadow);
    g_test_add_func ("/pbr/gpu/material-flags", test_pbr_gpu_material_flags);
    g_test_add_func ("/pbr/gpu/hooks", test_pbr_gpu_hooks);
    g_test_add_func ("/pbr/gpu/library", test_pbr_gpu_library);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
