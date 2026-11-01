/* test-terrain-render.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrain's shared-page drawing (chunk slots in shared buffers, one
 * base-vertex draw per mesh) renders exactly what the raylib mesh path
 * (one Mesh per terrain mesh, DrawMesh(), forced with
 * LRG_TERRAIN_DRAW=meshes) renders: the same pixels with raylib's
 * default shader and with a light set's shader, the same shadow atlas
 * texels in every shadow pass drawn with the caster shader, and the
 * same again after chunks are replaced, removed and added, the texture
 * repeat changes, and the terrain is unloaded and uploaded again. The
 * tests open a hidden window and skip without a display.
 */

#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define TARGET_W (320)
#define TARGET_H (240)
#define RES      (128)
#define STRIDE   (RES + 1)

/* GL entry points the atlas read-back needs, through raylib's loader */
#define TEST_GL_VIEWPORT (0x0BA2)
#define TEST_GL_RED      (0x1903)
#define TEST_GL_FLOAT    (0x1406)
typedef void (*TestGetIntegervFn) (guint name, gint *data);
typedef void (*TestReadPixelsFn) (gint x, gint y, gint w, gint h, guint format, guint type,
                                  void *pixels);

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetTraceLogLevel (LOG_WARNING);
    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-terrain-render-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GL() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("Graphics context not available"); \
            return; \
        } \
    } while (0)

/* set_chunk:
 * A rolling chunk at (@cx, @cz). @kind 0 is one material everywhere
 * (16384 cells: 65536 vertices, split into two meshes); other kinds mix
 * three materials and every UV variant. */
static void
set_chunk (LrgTerrain *terrain,
           gint        cx,
           gint        cz,
           guint       kind,
           gfloat      phase)
{
    g_autofree gfloat *heights = g_new (gfloat, STRIDE * STRIDE);
    g_autofree guint  *materials = g_new (guint, RES * RES);
    g_autofree guint8 *variants = g_new (guint8, RES * RES);
    guint              k;

    for (k = 0; k < STRIDE * STRIDE; k++)
    {
        gfloat x = (gfloat)cx * 32.0f + (gfloat)(k % STRIDE) * 0.25f;
        gfloat z = (gfloat)cz * 32.0f + (gfloat)(k / STRIDE) * 0.25f;

        heights[k] = 2.5f * sinf (x * 0.21f + phase) * cosf (z * 0.17f) + 0.3f * sinf (x * z * 0.01f);
    }
    for (k = 0; k < RES * RES; k++)
    {
        materials[k] = kind == 0 ? 0 : ((k / 7 + k % RES / 5 + kind) % 3);
        variants[k] = (guint8)((k * kind) & (LRG_TERRAIN_UV_ROTATE_90 | LRG_TERRAIN_UV_FLIP_U
                                             | LRG_TERRAIN_UV_FLIP_V));
    }
    g_assert_true (lrg_terrain_set_chunk (terrain, cx, cz, heights, STRIDE * STRIDE, materials,
                                          variants, NULL));
}

/* make_terrain:
 * 3 x 3 chunks minus one corner, two textured materials and an untextured
 * one, a tint and an origin; uploaded through the shared path, or
 * through raylib meshes when @meshes. */
static LrgTerrain *
make_terrain (gboolean    meshes,
              GrlTexture *a,
              GrlTexture *b)
{
    LrgTerrain *terrain = lrg_terrain_new (RES, 32.0f);
    GrlColor    tint = { 230, 240, 210, 255 };
    gint        cx;
    gint        cz;

    for (cz = -1; cz <= 1; cz++)
        for (cx = -1; cx <= 1; cx++)
            if (cx != 1 || cz != 1)
                set_chunk (terrain, cx, cz, (guint)(cx + 1 + (cz + 1) * 3) % 4, 0.0f);
    lrg_terrain_set_material_texture (terrain, 1, a);
    lrg_terrain_set_material_texture (terrain, 2, b);
    lrg_terrain_set_tint (terrain, &tint);
    lrg_terrain_set_origin (terrain, 3.5, 0.25, -1.75);
    lrg_terrain_set_texture_repeat (terrain, 3);
    if (meshes)
        g_setenv ("LRG_TERRAIN_DRAW", "meshes", TRUE);
    else
        g_unsetenv ("LRG_TERRAIN_DRAW");
    g_assert_true (lrg_terrain_upload (terrain, NULL));
    g_unsetenv ("LRG_TERRAIN_DRAW");
    return terrain;
}

static Camera3D
make_camera (guint which)
{
    Camera3D camera;

    memset (&camera, 0, sizeof camera);
    if (which == 0)
    {
        camera.position = (Vector3){ 10.0f, 22.0f, 46.0f };
        camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    }
    else
    {
        camera.position = (Vector3){ -30.0f, 6.0f, -8.0f };
        camera.target = (Vector3){ 20.0f, -2.0f, 12.0f };
    }
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

/* render:
 * @terrain drawn whole (and chunk (0, 0) again with draw_chunk() when
 * @single) into a fresh render texture; returns the pixels. */
static Image
render (LrgTerrain *terrain,
        Camera3D    camera,
        gboolean    single)
{
    RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
    Image           image;

    BeginTextureMode (target);
    ClearBackground ((Color){ 10, 20, 30, 255 });
    BeginMode3D (camera);
    if (single)
    {
        rlPushMatrix ();
        rlTranslatef (0.0f, 4.0f, 0.0f);
        lrg_terrain_draw_chunk (terrain, 0, 0);
        rlPopMatrix ();
    }
    lrg_terrain_draw (terrain);
    EndMode3D ();
    EndTextureMode ();
    image = LoadImageFromTexture (target.texture);
    UnloadRenderTexture (target);
    return image;
}

/* assert_same_image:
 * Both images are pixel for pixel equal, and not blank. */
static void
assert_same_image (Image a,
                   Image b)
{
    gsize    bytes = (gsize)a.width * a.height * 4;
    guint8  *pa = a.data;
    gsize    i;
    gboolean drawn = FALSE;

    g_assert_cmpint (a.width, ==, b.width);
    g_assert_cmpint (a.height, ==, b.height);
    g_assert_cmpint (a.format, ==, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    g_assert_cmpint (b.format, ==, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    for (i = 0; i < bytes; i += 4)
        if (pa[i] != 10 || pa[i + 1] != 20 || pa[i + 2] != 30)
            drawn = TRUE;
    g_assert_true (drawn);
    g_assert_cmpmem (a.data, bytes, b.data, bytes);
}

/* compare:
 * Renders both terrains from both cameras (and with draw_chunk()) and
 * asserts identical pixels. */
static void
compare (LrgTerrain *meshes,
         LrgTerrain *shared)
{
    guint which;

    for (which = 0; which < 4; which++)
    {
        Camera3D camera = make_camera (which % 2);
        Image    a = render (meshes, camera, which >= 2);
        Image    b = render (shared, camera, which >= 2);

        assert_same_image (a, b);
        UnloadImage (a);
        UnloadImage (b);
    }
}

/* edit:
 * The same edits on a terrain: replace a chunk, drop one, add one next
 * to another (its edge normals change), change the texture repeat (every
 * mesh rebuilds); then upload. */
static void
edit (LrgTerrain *terrain)
{
    set_chunk (terrain, 1, 0, 2, 1.3f);
    g_assert_true (lrg_terrain_remove_chunk (terrain, -1, 1));
    set_chunk (terrain, 1, 1, 1, 0.4f);
    g_assert_true (lrg_terrain_upload (terrain, NULL));
    set_chunk (terrain, 2, 0, 3, 0.9f);
    g_assert_true (lrg_terrain_upload (terrain, NULL));
    lrg_terrain_set_texture_repeat (terrain, 5);
    g_assert_true (lrg_terrain_upload (terrain, NULL));
    g_assert_true (lrg_terrain_is_uploaded (terrain));
}

static void
make_textures (GrlTexture **a,
               GrlTexture **b)
{
    g_autoptr(GrlImage) ia = grl_image_new_color (8, 8, &(GrlColor){ 60, 170, 50, 255 });
    g_autoptr(GrlImage) ib = grl_image_new_color (8, 8, &(GrlColor){ 150, 120, 90, 255 });

    grl_image_draw_rectangle (ia, &(GrlRectangle){ 0, 0, 4, 4 }, &(GrlColor){ 200, 220, 90, 255 });
    grl_image_draw_rectangle (ib, &(GrlRectangle){ 4, 0, 4, 8 }, &(GrlColor){ 90, 70, 40, 255 });
    *a = grl_texture_new_from_image (ia);
    *b = grl_texture_new_from_image (ib);
}

/* raylib's default shader: draw, edit, unload + upload again */
static void
test_terrain_render_default (void)
{
    g_autoptr(GrlTexture) ta = NULL;
    g_autoptr(GrlTexture) tb = NULL;
    g_autoptr(LrgTerrain) meshes = NULL;
    g_autoptr(LrgTerrain) shared = NULL;

    SKIP_WITHOUT_GL ();

    make_textures (&ta, &tb);
    meshes = make_terrain (TRUE, ta, tb);
    shared = make_terrain (FALSE, ta, tb);
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_count (shared, 0, 0), ==, 2);
    compare (meshes, shared);

    edit (meshes);
    edit (shared);
    compare (meshes, shared);

    lrg_terrain_unload (meshes);
    lrg_terrain_unload (shared);
    g_assert_false (lrg_terrain_is_uploaded (shared));
    g_assert_true (lrg_terrain_upload (meshes, NULL));
    g_assert_true (lrg_terrain_upload (shared, NULL));
    compare (meshes, shared);
}

/* read_pass:
 * The open shadow pass's viewport of the atlas as floats, appended. */
static void
read_pass (GArray *out)
{
    static TestGetIntegervFn get_integerv = NULL;
    static TestReadPixelsFn  read_pixels = NULL;
    gint                     viewport[4];
    guint                    at = out->len;

    if (get_integerv == NULL)
    {
        get_integerv = (TestGetIntegervFn)rlGetProcAddress ("glGetIntegerv");
        read_pixels = (TestReadPixelsFn)rlGetProcAddress ("glReadPixels");
    }
    g_assert_nonnull (get_integerv);
    g_assert_nonnull (read_pixels);
    rlDrawRenderBatchActive ();
    get_integerv (TEST_GL_VIEWPORT, viewport);
    g_assert_cmpint (viewport[2], >, 0);
    g_array_set_size (out, at + (guint)(viewport[2] * viewport[3]));
    read_pixels (viewport[0], viewport[1], viewport[2], viewport[3], TEST_GL_RED, TEST_GL_FLOAT,
                 &g_array_index (out, gfloat, at));
}

/* render_lit:
 * The shadow passes (terrain drawn with the caster shader, each pass's
 * atlas read into @atlas), then the terrain lit and shadowed. */
static Image
render_lit (LrgTerrain  *terrain,
            LrgLightSet *lights,
            GArray      *atlas)
{
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    RenderTexture2D              target;
    Camera3D                     camera = make_camera (0);
    gfloat                       eye[3] = { 10.0f, 22.0f, 46.0f };
    gfloat                       forward[3];
    gfloat                       up[3] = { 0.0f, 1.0f, 0.0f };
    gfloat                       length;
    guint                        n;
    guint                        p;
    Image                        image;

    forward[0] = -eye[0];
    forward[1] = -eye[1];
    forward[2] = -eye[2];
    length = sqrtf (forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    forward[0] /= length;
    forward[1] /= length;
    forward[2] /= length;

    lrg_shadow_renderer_set_sun (shadows, 512, 2, 60.0f, 0.5f);
    lrg_shadow_renderer_set_points (shadows, 128, 1, 0.0f);
    lrg_light_set_set_shadow_renderer (lights, NULL);
    n = lrg_shadow_renderer_update (shadows, lights, eye, forward, up, 55.0f,
                                    (gfloat)TARGET_W / TARGET_H, 0.1f);
    g_assert_cmpuint (n, >=, 2);
    lrg_terrain_set_shader (terrain, lrg_shadow_renderer_get_caster_shader (shadows));
    for (p = 0; p < n; p++)
    {
        if (!lrg_shadow_renderer_begin_pass (shadows, p))
            continue;
        lrg_terrain_draw (terrain);
        read_pass (atlas);
        lrg_shadow_renderer_end_pass (shadows);
    }
    g_assert_cmpuint (atlas->len, >, 0);
    lrg_light_set_set_shadow_renderer (lights, shadows);

    target = LoadRenderTexture (TARGET_W, TARGET_H);
    BeginTextureMode (target);
    ClearBackground ((Color){ 10, 20, 30, 255 });
    BeginMode3D (camera);
    lrg_light_set_apply (lights, eye[0], eye[1], eye[2]);
    lrg_light_set_bind (lights);
    lrg_terrain_set_shader (terrain, lrg_light_set_get_shader (lights));
    lrg_terrain_draw (terrain);
    EndMode3D ();
    EndTextureMode ();
    lrg_light_set_unbind ();
    lrg_light_set_set_shadow_renderer (lights, NULL);
    lrg_terrain_set_shader (terrain, NULL);
    lrg_shadow_renderer_release_gpu (shadows);
    image = LoadImageFromTexture (target.texture);
    UnloadRenderTexture (target);
    return image;
}

/* a light set's shader with a sun and a shadowed point light: the same
 * shadow atlas texels and the same lit pixels */
static void
test_terrain_render_lit_shadows (void)
{
    g_autoptr(GrlTexture)  ta = NULL;
    g_autoptr(GrlTexture)  tb = NULL;
    g_autoptr(LrgTerrain)  meshes = NULL;
    g_autoptr(LrgTerrain)  shared = NULL;
    g_autoptr(LrgLightSet) lights = NULL;
    g_autoptr(GArray)      atlas_a = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray)      atlas_b = g_array_new (FALSE, FALSE, sizeof (gfloat));
    GrlColor               ambient = { 50, 50, 60, 255 };
    GrlColor               sun = { 230, 220, 200, 255 };
    GrlColor               torch = { 255, 160, 60, 255 };
    GrlVector3             direction = { -0.4f, -1.0f, -0.3f };
    guint                  id;
    Image                  a;
    Image                  b;

    SKIP_WITHOUT_GL ();

    lights = lrg_light_set_new ();
    if (lrg_light_set_get_shader (lights) == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    lrg_light_set_set_ambient (lights, &ambient);
    lrg_light_set_set_directional (lights, &direction, &sun);
    lrg_light_set_set_origin (lights, 3.5, 0.25, -1.75);
    id = lrg_light_set_add_point_light (lights, 6.0, 5.0, 2.0, &torch, 18.0f);
    lrg_light_set_set_point_light_shadow (lights, id, 1);

    make_textures (&ta, &tb);
    meshes = make_terrain (TRUE, ta, tb);
    shared = make_terrain (FALSE, ta, tb);

    a = render_lit (meshes, lights, atlas_a);
    b = render_lit (shared, lights, atlas_b);
    g_assert_cmpuint (atlas_a->len, ==, atlas_b->len);
    g_assert_cmpmem (atlas_a->data, atlas_a->len * sizeof (gfloat),
                     atlas_b->data, atlas_b->len * sizeof (gfloat));
    assert_same_image (a, b);
    UnloadImage (a);
    UnloadImage (b);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/terrain-render/default", test_terrain_render_default);
    g_test_add_func ("/terrain-render/lit-shadows", test_terrain_render_lit_shadows);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
