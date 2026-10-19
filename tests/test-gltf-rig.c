/* test-gltf-rig.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the rigging side of LrgGltfGeometry: a two-joint
 * skinned strip (bind pose ignores the mesh node's own transform, a
 * rotation clip bends it, looping and clamping), step and cubic spline
 * interpolation of a rigid node, morph targets with default and animated
 * weights, the PBR material slots and KHR_texture_transform, and the
 * refusal of broken skins and samplers.  Fixtures are generated per test.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#include "lrg-test-glb.h"

/* appends floats / ushorts / bytes to a binary buffer; returns the offset */
static gsize
put_floats (GByteArray   *bin,
            const gfloat *v,
            guint         n)
{
    gsize at = bin->len;

    g_byte_array_append (bin, (const guint8 *)v, n * sizeof (gfloat));
    return at;
}

static gsize
put_bytes (GByteArray   *bin,
           const guint8 *v,
           guint         n)
{
    gsize at = bin->len;

    g_byte_array_append (bin, v, n);
    while (bin->len % 4 != 0)
    {
        guint8 zero = 0;

        g_byte_array_append (bin, &zero, 1);
    }
    return at;
}

/*
 * A strip of three rungs along +Y (y = 0, 1, 2; x = -0.5, 0.5), six
 * vertices.  Joint 0 (node 1) sits at the origin, joint 1 (node 2) is its
 * child at (0, 1, 0); the rungs at y = 0 follow joint 0, the others joint
 * 1.  Node 0 carries the mesh and the skin with a translation that the
 * skinned mesh must ignore.  The clip "bend" rotates joint 1 by 90 degrees
 * about Z over one second.
 */
static GBytes *
skinned_strip (const gchar *tweak)
{
    static const gfloat positions[18] = { -0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 1, 0,
                                          0.5f, 1, 0, -0.5f, 2, 0, 0.5f, 2, 0 };
    static const guint8 joints[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                                       1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    static const gfloat weights[24] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                        1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    static const guint16 indices[12] = { 0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4 };
    static const gfloat ibm[32] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                                    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1 };
    static const gfloat times[2] = { 0.0f, 1.0f };
    /* rotation about Z: identity, then 90 degrees (sin 45, cos 45) */
    static const gfloat rotations[8] = { 0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f };
    GByteArray *bin = g_byte_array_new ();
    gsize o_pos;
    gsize o_joints;
    gsize o_weights;
    gsize o_idx;
    gsize o_ibm;
    gsize o_times;
    gsize o_rot;
    g_autofree gchar *json = NULL;
    GBytes *glb;

    o_pos = put_floats (bin, positions, 18);
    o_joints = put_bytes (bin, joints, 24);
    o_weights = put_floats (bin, weights, 24);
    o_idx = put_bytes (bin, (const guint8 *)indices, 24);
    o_ibm = put_floats (bin, ibm, 32);
    o_times = put_floats (bin, times, 2);
    o_rot = put_floats (bin, rotations, 8);

    json = g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5121,\"count\":6,\"type\":\"VEC4\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":6,\"type\":\"VEC4\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5123,\"count\":12,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":%s,\"type\":\"VEC4\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},"
        "\"indices\":3}]}],"
        "\"skins\":[{\"joints\":[%s,2],\"inverseBindMatrices\":4}],"
        "\"nodes\":[{\"name\":\"Body\",\"mesh\":0,\"skin\":0,\"translation\":[5,5,5]},"
        "{\"name\":\"J0\",\"children\":[2]},"
        "{\"name\":\"J1\",\"translation\":[0,1,0]}],"
        "\"animations\":[{\"name\":\"bend\",\"samplers\":[{\"input\":5,\"output\":6}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"rotation\"}}]}],"
        "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0}",
        (guint)bin->len, (guint)bin->len, o_pos, o_joints, o_weights, o_idx, o_ibm, o_times, o_rot,
        g_strcmp0 (tweak, "short-output") == 0 ? "1" : "2",
        g_strcmp0 (tweak, "bad-joint") == 0 ? "9" : "1");
    glb = test_glb_build (json, bin->data, bin->len);
    g_byte_array_free (bin, TRUE);
    return glb;
}

static LrgGltfGeometry *
load (GBytes *glb,
      GError **error)
{
    return lrg_gltf_geometry_new_from_bytes (glb, NULL, error);
}

static void
assert_near (gfloat actual,
             gfloat expected,
             const gchar *what)
{
    if (fabsf (actual - expected) > 1e-3f)
        g_error ("%s: %f != %f", what, actual, expected);
}

static void
test_skin_bind_and_clip (void)
{
    g_autoptr(GBytes) glb = skinned_strip (NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGltfGeometry) g = load (glb, &error);
    const gfloat *stat;
    gfloat out[18];
    guint n = 0;

    g_assert_no_error (error);
    g_assert_nonnull (g);
    g_assert_true (lrg_gltf_geometry_is_animated (g));
    g_assert_cmpuint (lrg_gltf_geometry_get_skin_count (g), ==, 1);
    g_assert_cmpint (lrg_gltf_geometry_get_primitive_skin (g, 0), ==, 0);
    g_assert_cmpuint (lrg_gltf_geometry_get_animation_count (g), ==, 1);
    g_assert_cmpstr (lrg_gltf_geometry_get_animation_name (g, 0), ==, "bend");
    g_assert_cmpfloat_with_epsilon (lrg_gltf_geometry_get_animation_duration (g, 0), 1.0, 1e-6);

    /* the static getters are the bind pose: the skinned mesh's own node
     * translation (5,5,5) is ignored, the strip stands at the origin */
    stat = lrg_gltf_geometry_get_positions (g, 0, &n);
    g_assert_cmpuint (n, ==, 6);
    assert_near (stat[0], -0.5f, "bind x0");
    assert_near (stat[1], 0.0f, "bind y0");
    assert_near (stat[16], 2.0f, "bind y5");

    /* t = 1: joint 1 turned 90 degrees about (0,1,0) */
    lrg_gltf_geometry_pose (g, 0, 1.0, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[0], -0.5f, "j0 follower x");
    assert_near (out[1], 0.0f, "j0 follower y");
    assert_near (out[6], 0.0f, "rung1 left x");
    assert_near (out[7], 0.5f, "rung1 left y");
    assert_near (out[9], 0.0f, "rung1 right x");
    assert_near (out[10], 1.5f, "rung1 right y");
    assert_near (out[12], -1.0f, "rung2 left x");
    assert_near (out[13], 0.5f, "rung2 left y");
    assert_near (out[15], -1.0f, "rung2 right x");
    assert_near (out[16], 1.5f, "rung2 right y");

    /* t = 0.5: 45 degrees (rung 1 right corner (0.5,1) -> pivot + R(0.5,0)) */
    lrg_gltf_geometry_pose (g, 0, 0.5, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[9], 0.5f * cosf ((gfloat)G_PI / 4), "45 deg x");
    assert_near (out[10], 1.0f + 0.5f * sinf ((gfloat)G_PI / 4), "45 deg y");

    /* looping wraps, clamping stops at the end */
    lrg_gltf_geometry_pose (g, 0, 1.5, TRUE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[9], 0.5f * cosf ((gfloat)G_PI / 4), "looped x");
    lrg_gltf_geometry_pose (g, 0, 9.0, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[9], 0.0f, "clamped x");
    assert_near (out[10], 1.5f, "clamped y");

    /* back to the rest pose */
    lrg_gltf_geometry_pose (g, -1, 0.0, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[16], 2.0f, "rest y5");
    g_assert_false (lrg_gltf_geometry_get_posed (g, 3, out, NULL));
}

static void
test_skin_errors (void)
{
    g_autoptr(GBytes) bad_joint = skinned_strip ("bad-joint");
    g_autoptr(GBytes) bad_out = skinned_strip ("short-output");
    g_autoptr(GError) error = NULL;
    LrgGltfGeometry *g;

    g = load (bad_joint, &error);
    g_assert_null (g);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
    g_clear_error (&error);

    /* two keys but a single output value: refused, not read past the end */
    g = load (bad_out, &error);
    g_assert_null (g);
    g_assert_nonnull (error);
    g_clear_error (&error);
}

/*
 * A rigid triangle on node 0 with one morph target (vertex 0 moves +1 in x),
 * default mesh weight 0.5; node 1 (no mesh) is animated: STEP then CUBIC.
 * Animation 0 drives the weights 0 -> 1, animation 1 translates node 1 with
 * step interpolation, animation 2 with a cubic spline of zero tangents.
 */
static GBytes *
morph_triangle (void)
{
    static const gfloat positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const gfloat delta[9] = { 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const guint16 indices[3] = { 0, 1, 2 };
    static const gfloat times[2] = { 0.0f, 1.0f };
    static const gfloat wvals[2] = { 0.0f, 1.0f };
    static const gfloat tvals[6] = { 0, 0, 0, 10, 0, 0 };
    /* cubic: per key in-tangent, value, out-tangent */
    static const gfloat cvals[18] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 10, 0, 0, 0, 0, 0 };
    GByteArray *bin = g_byte_array_new ();
    gsize o_pos = put_floats (bin, positions, 9);
    gsize o_delta = put_floats (bin, delta, 9);
    gsize o_idx = put_bytes (bin, (const guint8 *)indices, 6);
    gsize o_times = put_floats (bin, times, 2);
    gsize o_w = put_floats (bin, wvals, 2);
    gsize o_t = put_floats (bin, tvals, 6);
    gsize o_c = put_floats (bin, cvals, 18);
    g_autofree gchar *json = NULL;
    GBytes *glb;

    json = g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"weights\":[0.5],\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":2,\"targets\":[{\"POSITION\":1}]}]}],"
        "\"nodes\":[{\"name\":\"Tri\",\"mesh\":0},{\"name\":\"Mover\"}],"
        "\"animations\":["
        "{\"name\":\"morph\",\"samplers\":[{\"input\":3,\"output\":4}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}}]},"
        "{\"name\":\"step\",\"samplers\":[{\"input\":3,\"output\":5,\"interpolation\":\"STEP\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]},"
        "{\"name\":\"cubic\",\"samplers\":[{\"input\":3,\"output\":6,\"interpolation\":\"CUBICSPLINE\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}],"
        "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0}",
        (guint)bin->len, (guint)bin->len, o_pos, o_delta, o_idx, o_times, o_w, o_t, o_c);
    glb = test_glb_build (json, bin->data, bin->len);
    g_byte_array_free (bin, TRUE);
    return glb;
}

static void
test_morph (void)
{
    g_autoptr(GBytes) glb = morph_triangle ();
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGltfGeometry) g = load (glb, &error);
    const gfloat *stat;
    gfloat out[9];
    guint n = 0;

    g_assert_no_error (error);
    g_assert_nonnull (g);
    g_assert_true (lrg_gltf_geometry_is_animated (g));
    g_assert_cmpuint (lrg_gltf_geometry_get_primitive_morph_target_count (g, 0), ==, 1);
    g_assert_cmpint (lrg_gltf_geometry_get_primitive_skin (g, 0), ==, -1);

    /* the rest pose carries the mesh's default weight 0.5 */
    stat = lrg_gltf_geometry_get_positions (g, 0, &n);
    assert_near (stat[0], 0.5f, "default weight");
    assert_near (stat[3], 1.0f, "unmoved vertex");

    lrg_gltf_geometry_pose (g, 0, 0.25, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[0], 0.25f, "weight 0.25");
    lrg_gltf_geometry_pose (g, 0, 1.0, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[0], 1.0f, "weight 1");
    lrg_gltf_geometry_pose (g, -1, 0.0, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[0], 0.5f, "back to the default");
}

static void
test_interpolation (void)
{
    g_autoptr(GBytes) glb = morph_triangle ();
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGltfGeometry) g = load (glb, &error);
    gfloat out[9];

    g_assert_no_error (error);
    g_assert_nonnull (g);
    /* node 1 has no mesh: the triangle on node 0 does not move; check
     * the node-less data through a pose that must not crash, and the
     * clip lengths */
    g_assert_cmpuint (lrg_gltf_geometry_get_animation_count (g), ==, 3);
    g_assert_cmpstr (lrg_gltf_geometry_get_animation_name (g, 1), ==, "step");
    g_assert_cmpstr (lrg_gltf_geometry_get_animation_name (g, 2), ==, "cubic");
    lrg_gltf_geometry_pose (g, 1, 0.5, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    lrg_gltf_geometry_pose (g, 2, 0.5, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
    assert_near (out[0], 0.5f, "the morph keeps its default weight");
    assert_near (out[3], 1.0f, "the unmoved vertex");
}

/* the same file with the triangle under the animated node: a rigid
 * primitive follows its node's translation (linear, step, cubic) */
static GBytes *
rigid_follower (const gchar *interp,
                gboolean     cubic)
{
    static const gfloat positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const guint16 indices[3] = { 0, 1, 2 };
    static const gfloat times[2] = { 0.0f, 1.0f };
    static const gfloat lin[6] = { 0, 0, 0, 10, 0, 0 };
    static const gfloat cub[18] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 10, 0, 0, 0, 0, 0 };
    GByteArray *bin = g_byte_array_new ();
    gsize o_pos = put_floats (bin, positions, 9);
    gsize o_idx = put_bytes (bin, (const guint8 *)indices, 6);
    gsize o_times = put_floats (bin, times, 2);
    gsize o_val = cubic ? put_floats (bin, cub, 18) : put_floats (bin, lin, 6);
    g_autofree gchar *json = NULL;
    GBytes *glb;

    json = g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":0,\"byteOffset\":%zu,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"nodes\":[{\"name\":\"Tri\",\"mesh\":0}],"
        "\"animations\":[{\"samplers\":[{\"input\":2,\"output\":3,\"interpolation\":\"%s\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}",
        (guint)bin->len, (guint)bin->len, o_pos, o_idx, o_times, o_val, cubic ? 6 : 2, interp);
    glb = test_glb_build (json, bin->data, bin->len);
    g_byte_array_free (bin, TRUE);
    return glb;
}

static void
test_rigid_follows_node (void)
{
    const gchar *modes[3] = { "LINEAR", "STEP", "CUBICSPLINE" };
    gfloat expected[3] = { 5.0f, 0.0f, 5.0f };
    guint m;

    for (m = 0; m < 3; m++)
    {
        g_autoptr(GBytes) glb = rigid_follower (modes[m], m == 2);
        g_autoptr(GError) error = NULL;
        g_autoptr(LrgGltfGeometry) g = load (glb, &error);
        gfloat out[9];

        g_assert_no_error (error);
        g_assert_nonnull (g);
        lrg_gltf_geometry_pose (g, 0, 0.5, FALSE);
        g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
        assert_near (out[0], expected[m], modes[m]);
        assert_near (out[3], expected[m] + 1.0f, modes[m]);
        lrg_gltf_geometry_pose (g, 0, 1.0, FALSE);
        g_assert_true (lrg_gltf_geometry_get_posed (g, 0, out, NULL));
        assert_near (out[0], 10.0f, modes[m]);
    }
}

static void
test_material_slots (void)
{
    static const gchar json[] =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"images\":[{\"uri\":\"a.png\"},{\"uri\":\"b.png\"},{\"uri\":\"c.png\"},"
        "{\"uri\":\"d.png\"},{\"uri\":\"e.png\"}],"
        "\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2},{\"source\":3},{\"source\":4}],"
        "\"materials\":[{\"name\":\"Full\","
        "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,\"extensions\":"
        "{\"KHR_texture_transform\":{\"offset\":[0.25,0.5],\"scale\":[2,3],\"rotation\":0}}},"
        "\"metallicFactor\":0.4,\"roughnessFactor\":0.7,"
        "\"metallicRoughnessTexture\":{\"index\":1}},"
        "\"normalTexture\":{\"index\":2,\"scale\":0.5},"
        "\"occlusionTexture\":{\"index\":3,\"strength\":0.8},"
        "\"emissiveTexture\":{\"index\":4},\"emissiveFactor\":[1,0.5,0.25]},"
        "{\"name\":\"Plain\"}]}";
    g_autoptr(GBytes) bytes = g_bytes_new_static (json, sizeof json - 1);
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGltfGeometry) g = load (bytes, &error);
    gfloat a = 0;
    gfloat b = 0;
    gfloat rgb[3];
    gfloat m[6];

    g_assert_no_error (error);
    g_assert_nonnull (g);
    g_assert_cmpint (lrg_gltf_geometry_get_material_image (g, 0), ==, 0);
    g_assert_cmpint (lrg_gltf_geometry_get_material_metallic_image (g, 0, &a, &b), ==, 1);
    assert_near (a, 0.4f, "metallic");
    assert_near (b, 0.7f, "roughness");
    g_assert_cmpint (lrg_gltf_geometry_get_material_normal_image (g, 0, &a), ==, 2);
    assert_near (a, 0.5f, "normal scale");
    g_assert_cmpint (lrg_gltf_geometry_get_material_occlusion_image (g, 0, &a), ==, 3);
    assert_near (a, 0.8f, "occlusion strength");
    g_assert_cmpint (lrg_gltf_geometry_get_material_emissive_image (g, 0, rgb), ==, 4);
    assert_near (rgb[1], 0.5f, "emissive g");
    g_assert_true (lrg_gltf_geometry_get_material_uv_transform (g, 0, m));
    assert_near (m[0], 2.0f, "uv m0");
    assert_near (m[2], 0.25f, "uv offset u");
    assert_near (m[4], 3.0f, "uv m4");
    assert_near (m[5], 0.5f, "uv offset v");

    /* the plain material has no slots and the identity transform */
    g_assert_cmpint (lrg_gltf_geometry_get_material_normal_image (g, 1, NULL), ==, -1);
    g_assert_cmpint (lrg_gltf_geometry_get_material_metallic_image (g, 1, NULL, NULL), ==, -1);
    g_assert_false (lrg_gltf_geometry_get_material_uv_transform (g, 1, m));
    assert_near (m[0], 1.0f, "identity m0");
    assert_near (m[4], 1.0f, "identity m4");
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/gltf-rig/skin", test_skin_bind_and_clip);
    g_test_add_func ("/gltf-rig/skin-errors", test_skin_errors);
    g_test_add_func ("/gltf-rig/morph", test_morph);
    g_test_add_func ("/gltf-rig/interpolation", test_interpolation);
    g_test_add_func ("/gltf-rig/rigid-follows-node", test_rigid_follows_node);
    g_test_add_func ("/gltf-rig/material-slots", test_material_slots);
    return g_test_run ();
}
