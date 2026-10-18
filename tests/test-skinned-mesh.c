/* test-skinned-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedMesh: geometry validation, weight renormalisation, the bone
 * palette (world * inverse bind), CPU reference skinning on a synthetic
 * two-bone arm, palettes from an LrgSkeleton (with the hierarchical pose
 * composition), bone pose matrices, the CPU fallback past the GPU
 * palette, and GPU readbacks that the shader skins like the CPU does.
 */

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

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

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-skinned-mesh-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GRAPHICS() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

static void
identity (gfloat *m)
{
    memset (m, 0, 16 * sizeof (gfloat));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void
translation (gfloat *m,
             gfloat  x,
             gfloat  y,
             gfloat  z)
{
    identity (m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

/* rotation about Z by @degrees around the point (px, py, 0) */
static void
rotation_z_about (gfloat *m,
                  gfloat  degrees,
                  gfloat  px,
                  gfloat  py)
{
    gfloat a = degrees * (gfloat)G_PI / 180.0f;
    gfloat c = cosf (a);
    gfloat s = sinf (a);

    identity (m);
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
    m[12] = px - c * px + s * py;
    m[13] = py - s * px - c * py;
}

#define ASSERT_NEAR(a, b) g_assert_cmpfloat_with_epsilon ((a), (b), 1e-4)

/* the arm: four vertices along +Y, bone 0 (upper arm) owns y = 0, bone
 * 1 (forearm, joint at y = 1) owns y = 2, the joint vertex is shared */
static const gfloat arm_positions[] = {
    0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 2.0f, 0.0f,
    1.0f, 2.0f, 0.0f
};
static const gfloat arm_normals[] = {
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f
};
static const guint8 arm_bones[] = {
    0, 1, 0, 0,
    0, 1, 0, 0,
    1, 0, 0, 0,
    1, 0, 0, 0
};
static const gfloat arm_weights[] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f,   /* renormalised to 0.5 / 0.5 */
    1.0f, 0.0f, 0.0f, 0.0f,
    2.0f, 0.0f, 0.0f, 0.0f    /* renormalised to 1 */
};
static const guint32 arm_indices[] = { 0, 1, 2, 1, 3, 2 };

static LrgSkinnedMesh *
make_arm (void)
{
    g_autoptr(GError) error = NULL;
    LrgSkinnedMesh *mesh;

    mesh = lrg_skinned_mesh_new (2);
    g_assert_nonnull (mesh);
    g_assert_true (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, arm_normals, NULL, NULL,
                                                  arm_bones, arm_weights, 6, arm_indices, &error));
    g_assert_no_error (error);
    return mesh;
}

static void
test_new_limits (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = NULL;

    g_assert_null (lrg_skinned_mesh_new (0));
    g_assert_null (lrg_skinned_mesh_new (256));
    mesh = lrg_skinned_mesh_new (3);
    g_assert_nonnull (mesh);
    g_assert_cmpuint (lrg_skinned_mesh_get_bone_count (mesh), ==, 3);
    g_assert_cmpuint (lrg_skinned_mesh_get_vertex_count (mesh), ==, 0);
    g_assert_null (lrg_skinned_mesh_get_bind_positions (mesh));
    g_assert_nonnull (lrg_skinned_mesh_get_inverse_bind (mesh, 2));
    g_assert_null (lrg_skinned_mesh_get_inverse_bind (mesh, 3));
    ASSERT_NEAR (lrg_skinned_mesh_get_palette (mesh)[0], 1.0f);
    g_assert_true (lrg_skinned_mesh_uses_gpu (mesh));
}

static void
test_geometry_validation (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    g_autoptr(GError) error = NULL;
    static const guint32 bad_indices[] = { 0, 1, 7 };
    guint8 bad_bones[16];
    gfloat bad_weights[16];

    /* an index past the vertices */
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, NULL, NULL, NULL,
                                                   arm_bones, arm_weights, 3, bad_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a weighted bone past the bone count */
    memcpy (bad_bones, arm_bones, sizeof bad_bones);
    bad_bones[8] = 5;
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, NULL, NULL, NULL,
                                                   bad_bones, arm_weights, 6, arm_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a negative or NaN weight */
    memcpy (bad_weights, arm_weights, sizeof bad_weights);
    bad_weights[1] = -0.5f;
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, NULL, NULL, NULL,
                                                   arm_bones, bad_weights, 6, arm_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    bad_weights[1] = NAN;
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, NULL, NULL, NULL,
                                                   arm_bones, bad_weights, 6, arm_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a ragged triangle list, no vertices */
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 4, arm_positions, NULL, NULL, NULL,
                                                   arm_bones, arm_weights, 4, arm_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_false (lrg_skinned_mesh_set_geometry (mesh, 0, arm_positions, NULL, NULL, NULL,
                                                   arm_bones, arm_weights, 6, arm_indices, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* every failure left the arm in place */
    g_assert_cmpuint (lrg_skinned_mesh_get_vertex_count (mesh), ==, 4);
    g_assert_cmpuint (lrg_skinned_mesh_get_triangle_count (mesh), ==, 2);
}

static void
test_weights_renormalised (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    g_autoptr(LrgSkinnedMesh) unweighted = lrg_skinned_mesh_new (2);
    g_autoptr(GError) error = NULL;
    static const gfloat zero[16] = { 0 };
    const guint8 *bones = NULL;
    const gfloat *weights;

    weights = lrg_skinned_mesh_get_weights (mesh, &bones);
    ASSERT_NEAR (weights[4], 0.5f);
    ASSERT_NEAR (weights[5], 0.5f);
    ASSERT_NEAR (weights[12], 1.0f);
    g_assert_cmpuint (bones[8], ==, 1);

    /* no weight at all: the vertex follows bone 0 */
    g_assert_true (lrg_skinned_mesh_set_geometry (unweighted, 4, arm_positions, NULL, NULL, NULL,
                                                  arm_bones, zero, 6, arm_indices, &error));
    weights = lrg_skinned_mesh_get_weights (unweighted, &bones);
    ASSERT_NEAR (weights[0], 1.0f);
    ASSERT_NEAR (weights[1], 0.0f);
    g_assert_cmpuint (bones[0], ==, 0);
}

static void
test_palette_math (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    gfloat world[32];
    gfloat inverse[16];
    gfloat palette[32];

    /* the forearm's bind is at (0, 1, 0): its inverse bind subtracts it */
    translation (inverse, 0.0f, -1.0f, 0.0f);
    g_assert_true (lrg_skinned_mesh_set_inverse_bind (mesh, 1, inverse));
    g_assert_false (lrg_skinned_mesh_set_inverse_bind (mesh, 2, inverse));
    ASSERT_NEAR (lrg_skinned_mesh_get_inverse_bind (mesh, 1)[13], -1.0f);

    /* in the bind pose the palette is the identity */
    identity (world);
    translation (world + 16, 0.0f, 1.0f, 0.0f);
    lrg_skinned_mesh_compute_palette (mesh, world, palette);
    ASSERT_NEAR (palette[16 + 13], 0.0f);
    ASSERT_NEAR (palette[16 + 0], 1.0f);

    /* moving the forearm bone moves its palette entry the same way */
    translation (world + 16, 3.0f, 1.0f, 0.0f);
    lrg_skinned_mesh_compute_palette (mesh, world, palette);
    ASSERT_NEAR (palette[16 + 12], 3.0f);
    ASSERT_NEAR (palette[16 + 13], 0.0f);
    /* computing does not pose the mesh */
    ASSERT_NEAR (lrg_skinned_mesh_get_palette (mesh)[16 + 12], 0.0f);
}

static void
test_cpu_skinning (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    gfloat inverse[16];
    gfloat world[32];
    gfloat palette[32];
    gfloat out[12];
    gfloat normals[12];
    gfloat lo[3];
    gfloat hi[3];

    translation (inverse, 0.0f, -1.0f, 0.0f);
    lrg_skinned_mesh_set_inverse_bind (mesh, 1, inverse);

    /* the bind pose gives the bind positions back */
    lrg_skinned_mesh_skin (mesh, out, normals);
    ASSERT_NEAR (out[7], 2.0f);
    ASSERT_NEAR (out[9], 1.0f);
    ASSERT_NEAR (normals[0], 1.0f);

    /* bend the elbow 90 degrees about Z: the forearm's world is the
     * rotation about the joint times its bind translation */
    identity (world);
    {
        gfloat rot[16];
        gfloat bind[16];
        guint  c;
        guint  r;

        rotation_z_about (rot, 90.0f, 0.0f, 1.0f);
        translation (bind, 0.0f, 1.0f, 0.0f);
        for (c = 0; c < 4; c++)
            for (r = 0; r < 4; r++)
                world[16 + c * 4 + r] = rot[0 * 4 + r] * bind[c * 4 + 0] + rot[1 * 4 + r] * bind[c * 4 + 1] +
                                        rot[2 * 4 + r] * bind[c * 4 + 2] + rot[3 * 4 + r] * bind[c * 4 + 3];
    }
    lrg_skinned_mesh_compute_palette (mesh, world, palette);
    lrg_skinned_mesh_set_palette (mesh, palette);
    lrg_skinned_mesh_skin (mesh, out, normals);

    /* the shoulder stays */
    ASSERT_NEAR (out[0], 0.0f);
    ASSERT_NEAR (out[1], 0.0f);
    /* the joint is on both bones and does not move */
    ASSERT_NEAR (out[3], 0.0f);
    ASSERT_NEAR (out[4], 1.0f);
    /* the hand (0, 2) swings to (-1, 1) */
    ASSERT_NEAR (out[6], -1.0f);
    ASSERT_NEAR (out[7], 1.0f);
    /* (1, 2) swings to (-1, 2) */
    ASSERT_NEAR (out[9], -1.0f);
    ASSERT_NEAR (out[10], 2.0f);
    /* the forearm's normals turn with it; the blended joint's is
     * renormalised */
    ASSERT_NEAR (normals[6], 0.0f);
    ASSERT_NEAR (normals[7], 1.0f);
    ASSERT_NEAR (normals[3] * normals[3] + normals[4] * normals[4], 1.0f);
    ASSERT_NEAR (normals[3], (gfloat)G_SQRT2 / 2.0f);

    g_assert_true (lrg_skinned_mesh_get_skinned_bounds (mesh, lo, hi));
    ASSERT_NEAR (lo[0], -1.0f);
    ASSERT_NEAR (hi[0], 0.0f);
    ASSERT_NEAR (hi[1], 2.0f);
}

static void
test_bone_pose_matrix (void)
{
    g_autoptr(LrgBonePose) pose = lrg_bone_pose_new ();
    gfloat m[16];
    gfloat s = (gfloat)G_SQRT2 / 2.0f;

    /* 90 degrees about Z, scale 2, at (1, 2, 3) */
    lrg_bone_pose_set_position (pose, 1.0f, 2.0f, 3.0f);
    lrg_bone_pose_set_rotation (pose, 0.0f, 0.0f, s, s);
    lrg_bone_pose_set_uniform_scale (pose, 2.0f);
    lrg_bone_pose_to_matrix (pose, m);
    /* X maps to 2 Y */
    ASSERT_NEAR (m[0], 0.0f);
    ASSERT_NEAR (m[1], 2.0f);
    /* Y maps to -2 X */
    ASSERT_NEAR (m[4], -2.0f);
    ASSERT_NEAR (m[10], 2.0f);
    ASSERT_NEAR (m[12], 1.0f);
    ASSERT_NEAR (m[14], 3.0f);
    ASSERT_NEAR (m[15], 1.0f);
}

static void
test_palette_from_skeleton (void)
{
    g_autoptr(LrgSkeleton) skeleton = lrg_skeleton_new ();
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    g_autoptr(LrgSkinnedMesh) big = lrg_skinned_mesh_new (3);
    g_autoptr(LrgBone) upper = lrg_bone_new ("upper", 0);
    g_autoptr(LrgBone) fore = lrg_bone_new ("fore", 1);
    g_autoptr(LrgBonePose) root_pose = lrg_bone_pose_new ();
    g_autoptr(LrgBonePose) fore_pose = lrg_bone_pose_new ();
    gfloat inverse[16];
    gfloat palette[32];
    gfloat out[12];
    gfloat s = (gfloat)G_SQRT2 / 2.0f;

    /* the shoulder turns 90 degrees about Z; the forearm sits one unit
     * up the upper arm, so the composition must carry it to (-1, 0) */
    lrg_bone_pose_set_rotation (root_pose, 0.0f, 0.0f, s, s);
    lrg_bone_set_local_pose (upper, root_pose);
    lrg_bone_set_world_pose (upper, root_pose);
    lrg_bone_pose_set_position (fore_pose, 0.0f, 1.0f, 0.0f);
    lrg_bone_set_local_pose (fore, fore_pose);
    lrg_bone_set_parent_index (fore, 0);
    lrg_skeleton_add_bone (skeleton, upper);
    lrg_skeleton_add_bone (skeleton, fore);
    lrg_skeleton_calculate_world_poses (skeleton);
    ASSERT_NEAR (lrg_bone_get_world_pose (fore)->position_x, -1.0f);
    ASSERT_NEAR (lrg_bone_get_world_pose (fore)->position_y, 0.0f);

    translation (inverse, 0.0f, -1.0f, 0.0f);
    lrg_skinned_mesh_set_inverse_bind (mesh, 1, inverse);
    g_assert_true (lrg_skinned_mesh_compute_palette_from_skeleton (mesh, skeleton, palette));
    lrg_skinned_mesh_set_palette (mesh, palette);
    lrg_skinned_mesh_skin (mesh, out, NULL);
    /* the whole arm turned: the hand (0, 2) lies at (-2, 0) */
    ASSERT_NEAR (out[6], -2.0f);
    ASSERT_NEAR (out[7], 0.0f);

    /* a mesh with more bones than the skeleton has */
    g_assert_false (lrg_skinned_mesh_compute_palette_from_skeleton (big, skeleton, palette));
}

static void
test_cpu_fallback_limit (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = lrg_skinned_mesh_new (LRG_SKINNED_MESH_MAX_BONES + 1);
    g_autoptr(LrgSkinnedMesh) fits = lrg_skinned_mesh_new (LRG_SKINNED_MESH_MAX_BONES);
    g_autoptr(GError) error = NULL;

    g_assert_false (lrg_skinned_mesh_uses_gpu (mesh));
    g_assert_true (lrg_skinned_mesh_uses_gpu (fits));
    g_assert_nonnull (strstr (lrg_skinned_mesh_get_vertex_shader_source (), "boneMatrices[64]"));

    /* no geometry: nothing to upload, headless or not */
    g_assert_false (lrg_skinned_mesh_upload (fits, &error));
    g_assert_nonnull (error);
}

static void
test_upload_headless (void)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_arm ();
    g_autoptr(GError) error = NULL;

    if (graphics_available)
    {
        g_test_skip ("A display is available");
        return;
    }
    g_assert_false (lrg_skinned_mesh_upload (mesh, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
    g_assert_false (lrg_skinned_mesh_is_uploaded (mesh));
    /* drawing without a context draws nothing and does not crash */
    lrg_skinned_mesh_draw (mesh, NULL);
}

/* a red square from x -1 .. 0, y -0.5 .. 0.5, all on bone @bone of a
 * mesh with @n_bones bones */
static LrgSkinnedMesh *
make_square (guint n_bones,
             guint bone)
{
    static const gfloat positions[] = {
        -1.0f, -0.5f, 0.0f,
         0.0f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
        -1.0f,  0.5f, 0.0f
    };
    static const guint32 indices[] = { 0, 1, 2, 0, 2, 3 };
    static const gfloat weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f
    };
    guint8          bones[16];
    LrgSkinnedMesh *mesh;
    guint           i;

    memset (bones, 0, sizeof bones);
    for (i = 0; i < 4; i++)
        bones[i * 4] = (guint8)bone;
    mesh = lrg_skinned_mesh_new (n_bones);
    g_assert_true (lrg_skinned_mesh_set_geometry (mesh, 4, positions, NULL, NULL, NULL,
                                                  bones, weights, 6, indices, NULL));
    lrg_skinned_mesh_set_tint (mesh, 255, 0, 0, 255);
    return mesh;
}

/* renders @mesh orthographically (2 units tall) and counts red pixels
 * left and right of the centre */
static void
render_halves (LrgSkinnedMesh *mesh,
               guint          *out_left,
               guint          *out_right)
{
    RenderTexture2D rt = LoadRenderTexture (32, 32);
    Camera3D        camera;
    Image           shot;
    Color          *pixels;
    gint            x;
    gint            y;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 0, 5 };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 2.0f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    rlDisableBackfaceCulling ();
    lrg_skinned_mesh_draw (mesh, NULL);
    rlEnableBackfaceCulling ();
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    pixels = LoadImageColors (shot);
    *out_left = 0;
    *out_right = 0;
    for (y = 0; y < shot.height; y++)
        for (x = 0; x < shot.width; x++)
        {
            Color c = pixels[y * shot.width + x];

            if (c.r > 150 && c.b < 100)
            {
                if (x < shot.width / 2)
                    (*out_left)++;
                else
                    (*out_right)++;
            }
        }
    UnloadImageColors (pixels);
    UnloadImage (shot);
}

static void
check_square_moves (guint n_bones,
                    guint bone)
{
    g_autoptr(LrgSkinnedMesh) mesh = make_square (n_bones, bone);
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *palette = NULL;
    guint left;
    guint right;
    guint b;

    g_assert_true (lrg_skinned_mesh_upload (mesh, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_skinned_mesh_is_uploaded (mesh));

    /* the bind pose: the square is on the left */
    render_halves (mesh, &left, &right);
    g_assert_cmpuint (left, >, 100);
    g_assert_cmpuint (right, <, 10);

    /* the bone moves one unit right: so does the square */
    palette = g_new (gfloat, 16 * n_bones);
    for (b = 0; b < n_bones; b++)
        identity (palette + 16 * b);
    palette[16 * bone + 12] = 1.0f;
    lrg_skinned_mesh_set_palette (mesh, palette);
    render_halves (mesh, &left, &right);
    g_assert_cmpuint (right, >, 100);
    g_assert_cmpuint (left, <, 10);

    lrg_skinned_mesh_unload (mesh);
    g_assert_false (lrg_skinned_mesh_is_uploaded (mesh));
}

static void
test_gpu_skinning (void)
{
    SKIP_WITHOUT_GRAPHICS ();
    /* bone 3 of 4: the palette index reaches the shader */
    check_square_moves (4, 3);
}

static void
test_gpu_cpu_fallback (void)
{
    SKIP_WITHOUT_GRAPHICS ();
    /* past the GPU palette: skinned on the CPU, the same picture */
    check_square_moves (LRG_SKINNED_MESH_MAX_BONES + 6, 69);
}

int
main (int    argc,
      char **argv)
{
    gint result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/skinned-mesh/new-limits", test_new_limits);
    g_test_add_func ("/skinned-mesh/geometry-validation", test_geometry_validation);
    g_test_add_func ("/skinned-mesh/weights-renormalised", test_weights_renormalised);
    g_test_add_func ("/skinned-mesh/palette-math", test_palette_math);
    g_test_add_func ("/skinned-mesh/cpu-skinning", test_cpu_skinning);
    g_test_add_func ("/skinned-mesh/bone-pose-matrix", test_bone_pose_matrix);
    g_test_add_func ("/skinned-mesh/palette-from-skeleton", test_palette_from_skeleton);
    g_test_add_func ("/skinned-mesh/cpu-fallback-limit", test_cpu_fallback_limit);
    g_test_add_func ("/skinned-mesh/upload-headless", test_upload_headless);
    g_test_add_func ("/skinned-mesh/gpu-skinning", test_gpu_skinning);
    g_test_add_func ("/skinned-mesh/gpu-cpu-fallback", test_gpu_cpu_fallback);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
