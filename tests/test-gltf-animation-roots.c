/* test-gltf-animation-roots.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the raylib 6.0 multi-root animation fix:
 * LrgGltfInfo skin-joint queries (names, parents, parent-node world
 * transforms) and lrg_gltf_info_fix_animation_roots(). raylib applies the
 * armature transform only to bone 0, so bones under any other root joint
 * are posed in armature space. The fixtures put two root joints under an
 * armature with translation (1, 2, 3), rotation -90 degrees about X and
 * scale 100, and check the exact posed positions before and after the fix,
 * that a single-root rig is left alone, that the fix is idempotent and
 * that the asset manager applies it. A real Quaternius wolf from the
 * parent game repository is checked read-only when present.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#include "lrg-test-glb.h"

#define EPS        (1e-3)
#define WOLF_CLIP  "AnimalArmature|Idle"
#define WOLF_ENV   "LRG_TEST_WOLF_GLB"

/* ========================================================================== */
/*                                 Helpers                                    */
/* ========================================================================== */

/* temp_path:
 * A per-process fixture path, so parallel test runs do not collide. */
static gchar *
temp_path (const gchar *basename)
{
    g_autofree gchar *name = g_strdup_printf ("lrg-roots-%d-%s", (gint)getpid (), basename);

    return g_build_filename (g_get_tmp_dir (), name, NULL);
}

/* load_clips:
 * Loads every clip of @path with raylib (through graylib), unpatched.
 * The array owns the clips. */
static GPtrArray *
load_clips (const gchar *path)
{
    g_autoptr(GError)   error = NULL;
    GrlModelAnimation **loaded;
    GPtrArray          *clips = g_ptr_array_new_with_free_func (g_object_unref);
    gint                count = 0;
    gint                i;

    loaded = grl_model_animation_load (path, &count, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loaded);
    for (i = 0; i < count; i++)
        g_ptr_array_add (clips, loaded[i]);
    g_free (loaded);
    return clips;
}

/* pose_of:
 * Borrowed pointer to bone @bone of keyframe @frame in clip @index. */
static Transform *
pose_of (GPtrArray *clips,
         guint      index,
         gint       frame,
         gint       bone)
{
    ModelAnimation *anim = grl_model_animation_get_handle (g_ptr_array_index (clips, index));

    g_assert_cmpint (frame, <, anim->keyframeCount);
    g_assert_cmpint (bone, <, anim->boneCount);
    return &anim->keyframePoses[frame][bone];
}

/* find_clip:
 * Index of the clip named @name, or -1. */
static gint
find_clip (GPtrArray   *clips,
           const gchar *name)
{
    guint i;

    for (i = 0; i < clips->len; i++)
        if (g_strcmp0 (grl_model_animation_get_name (g_ptr_array_index (clips, i)), name) == 0)
            return (gint)i;
    return -1;
}

static void
assert_vec3 (Vector3 v,
             gdouble x,
             gdouble y,
             gdouble z)
{
    g_assert_cmpfloat_with_epsilon (v.x, x, EPS);
    g_assert_cmpfloat_with_epsilon (v.y, y, EPS);
    g_assert_cmpfloat_with_epsilon (v.z, z, EPS);
}

/* assert_same_rotation:
 * q and -q are the same rotation. */
static void
assert_same_rotation (Quaternion a,
                      Quaternion b)
{
    gdouble dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    g_assert_cmpfloat_with_epsilon (fabs (dot), 1.0, 1e-4);
}

/* assert_same_pose:
 * Two poses are equal component by component. */
static void
assert_same_pose (const Transform *a,
                  const Transform *b)
{
    assert_vec3 (a->translation, b->translation.x, b->translation.y, b->translation.z);
    assert_vec3 (a->scale, b->scale.x, b->scale.y, b->scale.z);
    assert_same_rotation (a->rotation, b->rotation);
}

/* copy_poses:
 * Snapshot of every pose of every clip, so later mutations can be
 * compared with the raylib originals. Free with g_ptr_array_unref(). */
static GPtrArray *
copy_poses (GPtrArray *clips)
{
    GPtrArray *copy = g_ptr_array_new_with_free_func (g_free);
    guint      c;

    for (c = 0; c < clips->len; c++)
    {
        ModelAnimation *anim = grl_model_animation_get_handle (g_ptr_array_index (clips, c));
        Transform      *poses = g_new (Transform, (gsize)anim->keyframeCount * (gsize)anim->boneCount);
        gint            f;

        for (f = 0; f < anim->keyframeCount; f++)
            memcpy (poses + (gsize)f * (gsize)anim->boneCount, anim->keyframePoses[f],
                    sizeof (Transform) * (gsize)anim->boneCount);
        g_ptr_array_add (copy, poses);
    }
    return copy;
}

/* original_pose:
 * Pose from a copy_poses() snapshot. */
static const Transform *
original_pose (GPtrArray *snapshot,
               GPtrArray *clips,
               guint      clip,
               gint       frame,
               gint       bone)
{
    ModelAnimation *anim = grl_model_animation_get_handle (g_ptr_array_index (clips, clip));
    Transform      *poses = g_ptr_array_index (snapshot, clip);

    return &poses[(gsize)frame * (gsize)anim->boneCount + (gsize)bone];
}

/* expected_fixed:
 * Independent reference for the fix: W composed onto @pose with W taken
 * from lrg_gltf_info_get_joint_root_transform() and quaternion algebra
 * written out here (not the library's code path). */
static Transform
expected_fixed (LrgGltfInfo     *info,
                guint            root,
                const Transform *pose)
{
    gfloat     t[3], q[4], s[3];
    Transform  out;
    Vector3    v;
    Vector3    u;
    Vector3    c;
    gfloat     w;

    g_assert_true (lrg_gltf_info_get_joint_root_transform (info, root, t, q, s));

    /* Rotation: q * pose.rotation (Hamilton product). */
    out.rotation.x = q[3] * pose->rotation.x + q[0] * pose->rotation.w + q[1] * pose->rotation.z - q[2] * pose->rotation.y;
    out.rotation.y = q[3] * pose->rotation.y + q[1] * pose->rotation.w + q[2] * pose->rotation.x - q[0] * pose->rotation.z;
    out.rotation.z = q[3] * pose->rotation.z + q[2] * pose->rotation.w + q[0] * pose->rotation.y - q[1] * pose->rotation.x;
    out.rotation.w = q[3] * pose->rotation.w - q[0] * pose->rotation.x - q[1] * pose->rotation.y - q[2] * pose->rotation.z;

    /* Translation: rotate (s . t) by q via v' = v + 2w(u x v) + 2u x (u x v). */
    v = (Vector3){ pose->translation.x * s[0], pose->translation.y * s[1], pose->translation.z * s[2] };
    u = (Vector3){ q[0], q[1], q[2] };
    w = q[3];
    c = (Vector3){ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
    out.translation.x = v.x + 2 * w * c.x + 2 * (u.y * c.z - u.z * c.y) + t[0];
    out.translation.y = v.y + 2 * w * c.y + 2 * (u.z * c.x - u.x * c.z) + t[1];
    out.translation.z = v.z + 2 * w * c.z + 2 * (u.x * c.y - u.y * c.x) + t[2];

    out.scale = (Vector3){ pose->scale.x * s[0], pose->scale.y * s[1], pose->scale.z * s[2] };
    return out;
}

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

/* Joint queries on the two-root fixture. */
static void
test_roots_joint_queries (void)
{
    g_autofree gchar       *path = temp_path ("joints.glb");
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    gfloat                  t[3], q[4], s[3];

    test_glb_write_multi_root_rig (path, TRUE);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);

    g_assert_cmpuint (lrg_gltf_info_get_joint_count (info), ==, 4);
    g_assert_cmpstr (lrg_gltf_info_get_joint_name (info, 0), ==, "Root0");
    g_assert_cmpstr (lrg_gltf_info_get_joint_name (info, 1), ==, "Child0");
    g_assert_cmpstr (lrg_gltf_info_get_joint_name (info, 2), ==, "Root1");
    g_assert_cmpstr (lrg_gltf_info_get_joint_name (info, 3), ==, "Child1");
    g_assert_null (lrg_gltf_info_get_joint_name (info, 4));
    g_assert_cmpint (lrg_gltf_info_get_joint_node_index (info, 2), ==, 2);
    g_assert_cmpint (lrg_gltf_info_get_joint_node_index (info, 3), ==, 5);
    g_assert_cmpint (lrg_gltf_info_get_joint_node_index (info, 9), ==, -1);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 0), ==, -1);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 1), ==, 0);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 2), ==, -1);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 3), ==, 2);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 4), ==, -1);

    /* Root1's parent node is the armature: W exactly. */
    g_assert_true (lrg_gltf_info_get_joint_root_transform (info, 2, t, q, s));
    assert_vec3 ((Vector3){ t[0], t[1], t[2] }, 1, 2, 3);
    assert_same_rotation ((Quaternion){ q[0], q[1], q[2], q[3] },
                          (Quaternion){ -0.70710678f, 0, 0, 0.70710678f });
    assert_vec3 ((Vector3){ s[0], s[1], s[2] }, 100, 100, 100);

    /* Child0's parent node is Root0: W * Root0 = t (1, 2, 2). */
    g_assert_true (lrg_gltf_info_get_joint_root_transform (info, 1, t, NULL, s));
    assert_vec3 ((Vector3){ t[0], t[1], t[2] }, 1, 2, 2);
    assert_vec3 ((Vector3){ s[0], s[1], s[2] }, 100, 100, 100);

    g_assert_false (lrg_gltf_info_get_joint_root_transform (info, 4, t, q, s));
    g_unlink (path);
}

/* A file without a skin has no joints and fixes nothing. */
static void
test_roots_no_skin (void)
{
    g_autofree gchar       *path = temp_path ("static.glb");
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    g_autoptr(GPtrArray)    empty = g_ptr_array_new ();

    test_glb_write_static_triangle (path);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_get_joint_count (info), ==, 0);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 0), ==, -1);
    g_assert_false (lrg_gltf_info_get_joint_root_transform (info, 0, NULL, NULL, NULL));
    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, empty), ==, 0);
    g_unlink (path);
}

/* Node "matrix" transforms are honoured and malformed ones rejected. */
static void
test_roots_matrix_nodes (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(GBytes)      bytes = NULL;
    g_autoptr(LrgGltfInfo) info = NULL;
    gfloat                 t[3], s[3];

    /* Armature as a column-major matrix: scale 2, translation (5, 6, 7). */
    bytes = test_glb_build (
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"nodes\":[{\"name\":\"Armature\",\"children\":[1],"
        "\"matrix\":[2,0,0,0, 0,2,0,0, 0,0,2,0, 5,6,7,1]},"
        "{\"name\":\"Root\",\"translation\":[1,0,0]}],"
        "\"skins\":[{\"joints\":[1]}]}", NULL, 0);
    info = lrg_gltf_info_new_from_bytes (bytes, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_gltf_info_get_joint_root_transform (info, 0, t, NULL, s));
    assert_vec3 ((Vector3){ t[0], t[1], t[2] }, 5, 6, 7);
    assert_vec3 ((Vector3){ s[0], s[1], s[2] }, 2, 2, 2);
    g_clear_object (&info);
    g_clear_pointer (&bytes, g_bytes_unref);

    /* cgltf rejects a transform array of the wrong length. */
    bytes = test_glb_build (
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"translation\":[1,2]}]}", NULL, 0);
    info = lrg_gltf_info_new_from_bytes (bytes, &error);
    g_assert_null (info);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);

    bytes = test_glb_build (
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"scale\":[1,\"x\",1]}]}", NULL, 0);
    info = lrg_gltf_info_new_from_bytes (bytes, &error);
    g_assert_null (info);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON);
}

/* Two roots: raylib poses Root1's subtree in armature space; the fix
 * moves it into model space and leaves Root0's subtree alone. */
static void
test_roots_two_root_fix (void)
{
    g_autofree gchar       *path = temp_path ("two-roots.glb");
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    g_autoptr(GPtrArray)    clips = NULL;
    g_autoptr(GPtrArray)    before = NULL;
    Quaternion              w_rot = { -0.70710678f, 0, 0, 0.70710678f };
    guint                   c;
    gint                    f;
    gint                    b;
    gint                    last;

    test_glb_write_multi_root_rig (path, TRUE);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    clips = load_clips (path);
    g_assert_cmpuint (clips->len, ==, 2);
    g_assert_cmpint (grl_model_animation_get_bone_count (g_ptr_array_index (clips, 0)), ==, 4);
    g_assert_cmpint (grl_model_animation_get_frame_count (g_ptr_array_index (clips, 0)), ==, 31);
    last = grl_model_animation_get_frame_count (g_ptr_array_index (clips, 0)) - 1;

    /* raylib's own result: bone 0 carries W, bone 2 does not. */
    assert_vec3 (pose_of (clips, 0, 0, 0)->translation, 1, 2, 2);
    assert_vec3 (pose_of (clips, 0, 0, 1)->translation, 1, 2, 1);
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 0.02, 0, 0.01);
    assert_vec3 (pose_of (clips, 0, 0, 2)->scale, 1, 1, 1);
    assert_vec3 (pose_of (clips, 0, 0, 3)->translation, 0.02, 0, 0.02);

    before = copy_poses (clips);
    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), ==, 2);

    /* Hand-computed model-space positions: scale 100, then -90 degrees
     * about X ((x, y, z) -> (x, z, -y)), then + (1, 2, 3). */
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 3, 3, 3);
    assert_vec3 (pose_of (clips, 0, 0, 3)->translation, 3, 4, 3);
    assert_vec3 (pose_of (clips, 0, last, 2)->translation, 3, 3, 2);
    assert_vec3 (pose_of (clips, 0, last, 3)->translation, 3, 4, 2);
    assert_vec3 (pose_of (clips, 0, 0, 2)->scale, 100, 100, 100);
    assert_same_rotation (pose_of (clips, 0, 0, 2)->rotation, w_rot);
    assert_same_rotation (pose_of (clips, 0, 0, 3)->rotation, w_rot);

    /* Every frame of every clip: Root0's subtree unchanged, Root1's
     * subtree equals W * original. */
    for (c = 0; c < clips->len; c++)
    {
        ModelAnimation *anim = grl_model_animation_get_handle (g_ptr_array_index (clips, c));

        for (f = 0; f < anim->keyframeCount; f++)
            for (b = 0; b < anim->boneCount; b++)
            {
                const Transform *orig = original_pose (before, clips, c, f, b);

                if (b < 2)
                    assert_same_pose (pose_of (clips, c, f, b), orig);
                else
                {
                    Transform want = expected_fixed (info, 2, orig);

                    assert_same_pose (pose_of (clips, c, f, b), &want);
                }
            }
    }

    /* Root0 still moves in MoveRoot0 (+0.01 Y local -> -1 Z model). */
    assert_vec3 (pose_of (clips, 1, last, 0)->translation, 1, 2, 1);

    /* Idempotent: a second call is a no-op. */
    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), ==, 0);
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 3, 3, 3);
    g_unlink (path);
}

/* One root (Root1 parented to Root0): raylib is already right and the
 * fix must not touch anything. */
static void
test_roots_single_root_unchanged (void)
{
    g_autofree gchar       *path = temp_path ("one-root.glb");
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    g_autoptr(GPtrArray)    clips = NULL;
    g_autoptr(GPtrArray)    before = NULL;
    guint                   c;
    gint                    f;
    gint                    b;

    test_glb_write_multi_root_rig (path, FALSE);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (lrg_gltf_info_get_joint_parent (info, 2), ==, 0);
    clips = load_clips (path);
    before = copy_poses (clips);

    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), ==, 0);
    for (c = 0; c < clips->len; c++)
    {
        ModelAnimation *anim = grl_model_animation_get_handle (g_ptr_array_index (clips, c));

        for (f = 0; f < anim->keyframeCount; f++)
            for (b = 0; b < anim->boneCount; b++)
                g_assert_true (memcmp (pose_of (clips, c, f, b),
                                       original_pose (before, clips, c, f, b),
                                       sizeof (Transform)) == 0);
    }

    /* Root1 under Root0 under W: (1, 2, 2) + W.rot * (2, 0, 1) = (3, 3, 2). */
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 3, 3, 2);
    g_unlink (path);
}

/* Clips from another rig (bone count mismatch) are skipped, not marked. */
static void
test_roots_bone_count_mismatch (void)
{
    g_autofree gchar       *rig = temp_path ("rig.glb");
    g_autofree gchar       *other = temp_path ("other.glb");
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    g_autoptr(LrgGltfInfo)  rig_info = NULL;
    g_autoptr(GPtrArray)    clips = NULL;
    static const gchar     *names[] = { "Walk" };

    test_glb_write_multi_root_rig (rig, TRUE);
    test_glb_write_skinned_triangle (other, names, 1);
    info = lrg_gltf_info_new_from_file (rig, &error);
    g_assert_no_error (error);
    clips = load_clips (other);

    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), ==, 0);

    /* Not marked: the matching info still processes them (one bone, one
     * root, so nothing changes). */
    rig_info = lrg_gltf_info_new_from_file (other, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (rig_info, clips), ==, 0);
    g_unlink (rig);
    g_unlink (other);
}

/* The asset manager applies the fix before caching. */
static void
test_roots_asset_manager (void)
{
    g_autofree gchar           *path = temp_path ("managed.glb");
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgAssetManager)  manager = lrg_asset_manager_new ();
    g_autoptr(LrgGltfInfo)      info = NULL;
    GPtrArray                  *clips;

    test_glb_write_multi_root_rig (path, TRUE);
    clips = lrg_asset_manager_load_model_animations (manager, path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (clips);
    g_assert_cmpuint (clips->len, ==, 2);
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 3, 3, 3);
    assert_vec3 (pose_of (clips, 0, 0, 0)->translation, 1, 2, 2);

    /* Already fixed: a second fix and a cached reload change nothing. */
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), ==, 0);
    g_assert_true (lrg_asset_manager_load_model_animations (manager, path, &error) == clips);
    assert_vec3 (pose_of (clips, 0, 0, 2)->translation, 3, 3, 3);
    g_unlink (path);
}

/* find_wolf:
 * The Quaternius wolf of the parent game repository, or NULL. Override
 * with $LRG_TEST_WOLF_GLB. */
static gchar *
find_wolf (void)
{
    static const gchar *const candidates[] = {
        "../../../data/assets/quaternius-wolf/models/wolf.glb",   /* from tests/ */
        "../../data/assets/quaternius-wolf/models/wolf.glb",      /* from the engine root */
        NULL
    };
    const gchar *env = g_getenv (WOLF_ENV);
    guint        i;

    if (env != NULL && g_file_test (env, G_FILE_TEST_IS_REGULAR))
        return g_strdup (env);
    for (i = 0; candidates[i] != NULL; i++)
        if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
            return g_strdup (candidates[i]);
    return NULL;
}

/* Real asset (read-only): the wolf's IK leg roots sit in armature space
 * (centimetre-sized, unrotated) before the fix and at plausible metre
 * positions inside the model's bounds after it. */
static void
test_roots_wolf (void)
{
    g_autofree gchar       *path = find_wolf ();
    g_autoptr(GError)       error = NULL;
    g_autoptr(LrgGltfInfo)  info = NULL;
    g_autoptr(GPtrArray)    clips = NULL;
    g_autoptr(GPtrArray)    before = NULL;
    guint                   n_ik = 0;
    guint                   b;
    gint                    idle;

    if (path == NULL)
    {
        g_test_skip ("wolf.glb not found (set " WOLF_ENV ")");
        return;
    }

    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    clips = load_clips (path);
    idle = find_clip (clips, WOLF_CLIP);
    g_assert_cmpint (idle, >=, 0);
    g_assert_cmpint (grl_model_animation_get_bone_count (g_ptr_array_index (clips, idle)), ==,
                     (gint)lrg_gltf_info_get_joint_count (info));
    before = copy_poses (clips);

    g_assert_cmpuint (lrg_gltf_info_fix_animation_roots (info, clips), >, 0);

    for (b = 1; b < lrg_gltf_info_get_joint_count (info); b++)
    {
        const Transform *orig;
        const Transform *fixed;
        gdouble          orig_len;
        gdouble          fixed_len;

        if (lrg_gltf_info_get_joint_parent (info, b) != -1)
            continue;
        g_assert_true (g_str_has_prefix (lrg_gltf_info_get_joint_name (info, b), "IK"));
        n_ik++;

        orig = original_pose (before, clips, (guint)idle, 0, (gint)b);
        fixed = pose_of (clips, (guint)idle, 0, (gint)b);
        orig_len = sqrt (orig->translation.x * orig->translation.x +
                         orig->translation.y * orig->translation.y +
                         orig->translation.z * orig->translation.z);
        fixed_len = sqrt (fixed->translation.x * fixed->translation.x +
                          fixed->translation.y * fixed->translation.y +
                          fixed->translation.z * fixed->translation.z);

        /* Before: armature units (~1 cm). After: ~100x, inside the bind
         * pose bounds, with scale 100 like bone 0. */
        g_assert_cmpfloat (orig_len, <, 0.05);
        g_assert_cmpfloat (fixed_len, >, 0.1);
        g_assert_cmpfloat_with_epsilon (fixed_len / orig_len, 100.0, 1.0);
        g_assert_cmpfloat (fabs (fixed->translation.x), <, 1.0);
        g_assert_cmpfloat (fixed->translation.y, >=, 0.0);
        g_assert_cmpfloat (fixed->translation.y, <, 3.0);
        g_assert_cmpfloat (fabs (fixed->translation.z), <, 4.0);
        assert_vec3 (fixed->scale, 100, 100, 100);
    }
    g_assert_cmpuint (n_ik, ==, 4);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* raylib logs every loaded clip at INFO; keep test output readable. */
    SetTraceLogLevel (LOG_WARNING);

    g_test_add_func ("/gltf-animation-roots/joint-queries", test_roots_joint_queries);
    g_test_add_func ("/gltf-animation-roots/no-skin", test_roots_no_skin);
    g_test_add_func ("/gltf-animation-roots/matrix-nodes", test_roots_matrix_nodes);
    g_test_add_func ("/gltf-animation-roots/two-root-fix", test_roots_two_root_fix);
    g_test_add_func ("/gltf-animation-roots/single-root-unchanged", test_roots_single_root_unchanged);
    g_test_add_func ("/gltf-animation-roots/bone-count-mismatch", test_roots_bone_count_mismatch);
    g_test_add_func ("/gltf-animation-roots/asset-manager", test_roots_asset_manager);
    g_test_add_func ("/gltf-animation-roots/wolf", test_roots_wolf);

    return g_test_run ();
}
