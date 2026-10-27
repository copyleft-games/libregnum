/* lrg-test-glb.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Test-only helpers that write small binary glTF (GLB) fixtures, so model,
 * animation and glTF-inspection tests never depend on files outside the
 * repository. Header-only: every function is static and only the test
 * that includes it links it.
 */

#pragma once

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define TEST_GLB_MAGIC      (0x46546C67u)  /* "glTF" */
#define TEST_GLB_CHUNK_JSON (0x4E4F534Au)  /* "JSON" */
#define TEST_GLB_CHUNK_BIN  (0x004E4942u)  /* "BIN\0" */

/* test_glb_put_u32:
 * Appends a little-endian 32-bit word. */
G_GNUC_UNUSED static void
test_glb_put_u32 (GByteArray *out,
                  guint32     value)
{
    guint8 bytes[4];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    bytes[2] = (guint8)((value >> 16) & 0xff);
    bytes[3] = (guint8)((value >> 24) & 0xff);
    g_byte_array_append (out, bytes, 4);
}

/* test_glb_build:
 * Builds a GLB container: 12-byte header, a JSON chunk padded with spaces
 * and, when @bin is not NULL, a BIN chunk padded with zeros. */
G_GNUC_UNUSED static GBytes *
test_glb_build (const gchar  *json,
                const guint8 *bin,
                gsize         bin_len)
{
    GByteArray *out = g_byte_array_new ();
    gsize       json_len = strlen (json);
    gsize       json_padded = (json_len + 3) & ~(gsize)3;
    gsize       bin_padded = (bin_len + 3) & ~(gsize)3;
    gsize       total;
    gsize       i;
    guint8      zero = 0;
    guint8      space = ' ';

    total = 12 + 8 + json_padded + (bin != NULL ? 8 + bin_padded : 0);
    test_glb_put_u32 (out, TEST_GLB_MAGIC);
    test_glb_put_u32 (out, 2);
    test_glb_put_u32 (out, (guint32)total);

    test_glb_put_u32 (out, (guint32)json_padded);
    test_glb_put_u32 (out, TEST_GLB_CHUNK_JSON);
    g_byte_array_append (out, (const guint8 *)json, (guint)json_len);
    for (i = json_len; i < json_padded; i++)
        g_byte_array_append (out, &space, 1);

    if (bin != NULL)
    {
        test_glb_put_u32 (out, (guint32)bin_padded);
        test_glb_put_u32 (out, TEST_GLB_CHUNK_BIN);
        g_byte_array_append (out, bin, (guint)bin_len);
        for (i = bin_len; i < bin_padded; i++)
            g_byte_array_append (out, &zero, 1);
    }

    return g_byte_array_free_to_bytes (out);
}

/* test_glb_write_bytes:
 * Writes raw bytes to @path, asserting success. */
G_GNUC_UNUSED static void
test_glb_write_bytes (const gchar  *path,
                      const guint8 *data,
                      gsize         length)
{
    g_autoptr(GError) error = NULL;

    g_assert_true (g_file_set_contents (path, (const gchar *)data, (gssize)length, &error));
    g_assert_no_error (error);
}

/* test_glb_write:
 * Builds and writes a GLB fixture. */
G_GNUC_UNUSED static void
test_glb_write (const gchar  *path,
                const gchar  *json,
                const guint8 *bin,
                gsize         bin_len)
{
    g_autoptr(GBytes) bytes = test_glb_build (json, bin, bin_len);
    gsize             length = 0;
    const guint8     *data = g_bytes_get_data (bytes, &length);

    test_glb_write_bytes (path, data, length);
}

/*
 * test_glb_write_skinned_triangle:
 * @path: destination
 * @clip_names: (array length=n_clips): animation names
 * @n_clips: number of animations
 *
 * One triangle mesh ("Body") parented to a single joint ("Bone") under an
 * "Armature" root, with a one-joint skin. Every animation translates the
 * joint over 0.5 s, so raylib samples 31 keyframes at 60 fps. raylib gives
 * the joint-parented mesh full weight on bone 0.
 *
 * Binary layout: positions (36 bytes) | indices u16 (6 + 2 pad) |
 * times (8) | translations (24) = 76 bytes.
 */
G_GNUC_UNUSED static void
test_glb_write_skinned_triangle (const gchar        *path,
                                 const gchar *const *clip_names,
                                 guint               n_clips)
{
    static const gfloat positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const guint16 indices[4] = { 0, 1, 2, 0 };
    static const gfloat times[2] = { 0.0f, 0.5f };
    static const gfloat moves[6] = { 0, 0, 0, 0, 1, 0 };
    guint8   bin[76];
    GString *json = g_string_new (NULL);
    guint    i;

    memcpy (bin, positions, 36);
    memcpy (bin + 36, indices, 8);
    memcpy (bin + 44, times, 8);
    memcpy (bin + 52, moves, 24);

    g_string_append (json,
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"Armature\",\"children\":[1]},"
        "{\"name\":\"Bone\",\"children\":[2]},"
        "{\"name\":\"Body\",\"mesh\":0,\"skin\":0}],"
        "\"meshes\":[{\"name\":\"Body\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"skins\":[{\"joints\":[1]}],"
        "\"animations\":[");
    for (i = 0; i < n_clips; i++)
        g_string_append_printf (json,
            "%s{\"name\":\"%s\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
            "\"samplers\":[{\"input\":2,\"output\":3,\"interpolation\":\"LINEAR\"}]}",
            i > 0 ? "," : "", clip_names[i]);
    g_string_append (json,
        "],\"buffers\":[{\"byteLength\":76}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":52,\"byteLength\":24}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[0.5]},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}]}");

    test_glb_write (path, json->str, bin, sizeof bin);
    g_string_free (json, TRUE);
}

/*
 * test_glb_write_static_triangle:
 * @path: destination
 *
 * One named triangle mesh with no skin and no animations.
 */
G_GNUC_UNUSED static void
test_glb_write_static_triangle (const gchar *path)
{
    static const gfloat positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };

    test_glb_write (path,
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"nodes\":[{\"name\":\"Rock\",\"mesh\":0}],"
        "\"meshes\":[{\"name\":\"Rock\",\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"buffers\":[{\"byteLength\":36}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]}]}",
        (const guint8 *)positions, sizeof positions);
}

/*
 * test_glb_write_multi_root_rig:
 * @path: destination
 * @two_roots: %TRUE for two root joints, %FALSE to parent the second
 *   root under the first (a single-root rig)
 *
 * A skinned rig under an "Armature" node with translation (1, 2, 3),
 * rotation -90 degrees about X and scale 100 (a typical Blender export):
 *
 *   node 0 Armature  children [1, 2, 4] (or [1, 4] for one root)
 *   node 1 Root0     t (0, 0.01, 0)     children [3] (+ [2] for one root)
 *   node 2 Root1     t (0.02, 0, 0.01)  children [5]
 *   node 3 Child0    t (0, 0.01, 0)
 *   node 4 Body      mesh 0, skin 0
 *   node 5 Child1    t (0, 0, 0.01)
 *
 * Skin joints, in raylib bone order, are [Root0, Child0, Root1, Child1].
 * Two animations: "MoveRoot1" translates Root1 by +0.01 Y over 0.5 s and
 * "MoveRoot0" translates Root0 by +0.01 Y over 0.5 s (31 keyframes each).
 *
 * Binary layout: positions (36) | indices u16 (6 + 2 pad) | times (8) |
 * Root1 translations (24) | Root0 translations (24) = 100 bytes.
 */
G_GNUC_UNUSED static void
test_glb_write_multi_root_rig (const gchar *path,
                               gboolean     two_roots)
{
    static const gfloat  positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const guint16 indices[4] = { 0, 1, 2, 0 };
    static const gfloat  times[2] = { 0.0f, 0.5f };
    static const gfloat  root1_moves[6] = { 0.02f, 0, 0.01f, 0.02f, 0.01f, 0.01f };
    static const gfloat  root0_moves[6] = { 0, 0.01f, 0, 0, 0.02f, 0 };
    guint8           bin[100];
    g_autofree gchar *json = NULL;

    memcpy (bin, positions, 36);
    memcpy (bin + 36, indices, 8);
    memcpy (bin + 44, times, 8);
    memcpy (bin + 52, root1_moves, 24);
    memcpy (bin + 76, root0_moves, 24);

    json = g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":["
        "{\"name\":\"Armature\",\"children\":%s,\"translation\":[1,2,3],"
        "\"rotation\":[-0.70710678,0,0,0.70710678],\"scale\":[100,100,100]},"
        "{\"name\":\"Root0\",\"translation\":[0,0.01,0],\"children\":%s},"
        "{\"name\":\"Root1\",\"translation\":[0.02,0,0.01],\"children\":[5]},"
        "{\"name\":\"Child0\",\"translation\":[0,0.01,0]},"
        "{\"name\":\"Body\",\"mesh\":0,\"skin\":0},"
        "{\"name\":\"Child1\",\"translation\":[0,0,0.01]}],"
        "\"meshes\":[{\"name\":\"Body\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"skins\":[{\"joints\":[1,3,2,5]}],"
        "\"animations\":["
        "{\"name\":\"MoveRoot1\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}}],"
        "\"samplers\":[{\"input\":2,\"output\":3,\"interpolation\":\"LINEAR\"}]},"
        "{\"name\":\"MoveRoot0\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
        "\"samplers\":[{\"input\":2,\"output\":4,\"interpolation\":\"LINEAR\"}]}],"
        "\"buffers\":[{\"byteLength\":100}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":52,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":76,\"byteLength\":24}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[0.5]},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}]}",
        two_roots ? "[1,2,4]" : "[1,4]",
        two_roots ? "[3]" : "[3,2]");

    test_glb_write (path, json, bin, sizeof bin);
}

/*
 * test_glb_write_two_mesh_model:
 * @path: destination
 *
 * A static model with two meshes, the second not indexed:
 *
 *   node 0 "Crate"  mesh 0: an indexed unit quad (4 vertices, 2 triangles)
 *                   translated (0.5, 0, 0) and rotated 90 degrees about Y
 *   node 1 "Plank"  mesh 1: two triangles as a plain list (6 vertices, no
 *                   "indices"), scaled 2 on X
 *
 * raylib bakes node transforms into the vertices and loads the second mesh
 * with Mesh.indices == NULL.
 *
 * Binary layout: quad positions (48) | quad indices u16 (12) |
 * plank positions (72) = 132 bytes.
 */
G_GNUC_UNUSED static void
test_glb_write_two_mesh_model (const gchar *path)
{
    static const gfloat  quad[12] = { 0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0 };
    static const guint16 quad_indices[6] = { 0, 1, 2,  0, 2, 3 };
    static const gfloat  plank[18] = { 0, 0, 0,  1, 0, 1,  1, 0, 0,
                                       0, 0, 0,  0, 0, 1,  1, 0, 1 };
    guint8 bin[132];

    memcpy (bin, quad, 48);
    memcpy (bin + 48, quad_indices, 12);
    memcpy (bin + 60, plank, 72);

    test_glb_write (path,
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
        "\"nodes\":["
        "{\"name\":\"Crate\",\"mesh\":0,\"translation\":[0.5,0,0],"
        "\"rotation\":[0,0.70710678,0,0.70710678]},"
        "{\"name\":\"Plank\",\"mesh\":1,\"scale\":[2,1,1]}],"
        "\"meshes\":["
        "{\"name\":\"Crate\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]},"
        "{\"name\":\"Plank\",\"primitives\":[{\"attributes\":{\"POSITION\":2}}]}],"
        "\"buffers\":[{\"byteLength\":132}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":60,\"byteLength\":72}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,0,1]}]}",
        bin, sizeof bin);
}

/* test_glb_put_floats / test_glb_put_bytes:
 * Append raw little-endian payload to a BIN chunk under construction. */
G_GNUC_UNUSED static void
test_glb_put_floats (GByteArray   *out,
                     const gfloat *values,
                     guint         n)
{
    g_byte_array_append (out, (const guint8 *)values, n * (guint)sizeof (gfloat));
}

G_GNUC_UNUSED static void
test_glb_pad4 (GByteArray *out)
{
    guint8 zero = 0;

    while (out->len % 4 != 0)
        g_byte_array_append (out, &zero, 1);
}

/*
 * test_glb_write_skinned_tube:
 * @path: destination
 * @rings: segments along the tube (rings + 1 vertex rings)
 * @sides: vertices around each ring
 * @bones: joints in the chain, 1..255
 * @clip_names: (array length=n_clips): animation names
 * @n_clips: number of animations
 *
 * A vertical tube 2 units tall and 0.25 in radius, standing on the origin,
 * skinned to a chain of @bones joints ("Bone0" at the origin, each next
 * one 2 / @bones higher) under an "Armature" root. Vertices blend
 * linearly between the two nearest joints, so most carry two influences.
 * Vertex colours alternate in a checker of rings and sides, normals point
 * outwards. Every clip lasts 1 s (61 raylib keyframes) and rotates each
 * joint from rest to a clip- and joint-dependent angle and back, about Z
 * for even clips and X for odd ones, so the tube curls and sways.
 *
 * Binary layout: positions | normals | colours (u8 RGBA) | joints (u8) |
 * weights (f32) | indices (u16, padded) | key times | one rotation
 * output (3 quaternions) per clip and joint.
 */
G_GNUC_UNUSED static void
test_glb_write_skinned_tube (const gchar        *path,
                             guint               rings,
                             guint               sides,
                             guint               bones,
                             const gchar *const *clip_names,
                             guint               n_clips)
{
    static const gfloat times[3] = { 0.0f, 0.5f, 1.0f };
    GByteArray *bin = g_byte_array_new ();
    GString    *json = g_string_new (NULL);
    guint       n_vertices = (rings + 1) * sides;
    guint       n_indices = rings * sides * 6;
    gfloat      height = 2.0f;
    gfloat      radius = 0.25f;
    gfloat      segment;
    gsize       off_normals, off_colors, off_joints, off_weights, off_indices;
    gsize       off_times, off_rotations;
    guint       r, k, c, b;

    g_assert_cmpuint (bones, >=, 1);
    g_assert_cmpuint (bones, <=, 255);
    g_assert_cmpuint (sides, >=, 3);
    g_assert_cmpuint (n_vertices, <=, 65535);
    segment = height / (gfloat)bones;

    /* positions */
    for (r = 0; r <= rings; r++)
        for (k = 0; k < sides; k++)
        {
            gfloat a = 2.0f * (gfloat)G_PI * (gfloat)k / (gfloat)sides;
            gfloat v[3];

            v[0] = radius * cosf (a);
            v[1] = height * (gfloat)r / (gfloat)rings;
            v[2] = radius * sinf (a);
            test_glb_put_floats (bin, v, 3);
        }
    /* normals */
    off_normals = bin->len;
    for (r = 0; r <= rings; r++)
        for (k = 0; k < sides; k++)
        {
            gfloat a = 2.0f * (gfloat)G_PI * (gfloat)k / (gfloat)sides;
            gfloat n[3];

            n[0] = cosf (a);
            n[1] = 0.0f;
            n[2] = sinf (a);
            test_glb_put_floats (bin, n, 3);
        }
    /* colours: a checker of rings and sides */
    off_colors = bin->len;
    for (r = 0; r <= rings; r++)
        for (k = 0; k < sides; k++)
        {
            guint8 rgba[4];

            rgba[0] = ((r / 2 + k / 2) % 2 == 0) ? 255 : 60;
            rgba[1] = (guint8)(80 + (175 * r) / MAX (rings, 1));
            rgba[2] = (guint8)(255 - (200 * k) / sides);
            rgba[3] = 255;
            g_byte_array_append (bin, rgba, 4);
        }
    /* joints and weights: linear between the two nearest joint centres */
    off_joints = bin->len;
    for (r = 0; r <= rings; r++)
        for (k = 0; k < sides; k++)
        {
            gfloat f = (height * (gfloat)r / (gfloat)rings) / segment;
            guint  j = MIN ((guint)floorf (f), bones - 1);
            guint8 ids[4] = { 0, 0, 0, 0 };

            ids[0] = (guint8)j;
            if (f - (gfloat)j > 0.5f && j + 1 < bones)
                ids[1] = (guint8)(j + 1);
            else if (f - (gfloat)j < 0.5f && j > 0)
                ids[1] = (guint8)(j - 1);
            g_byte_array_append (bin, ids, 4);
        }
    off_weights = bin->len;
    for (r = 0; r <= rings; r++)
        for (k = 0; k < sides; k++)
        {
            gfloat f = (height * (gfloat)r / (gfloat)rings) / segment;
            guint  j = MIN ((guint)floorf (f), bones - 1);
            gfloat frac = f - (gfloat)j;
            gfloat w[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

            if (frac > 0.5f && j + 1 < bones)
            {
                w[0] = 1.5f - frac;
                w[1] = frac - 0.5f;
            }
            else if (frac < 0.5f && j > 0)
            {
                w[0] = 0.5f + frac;
                w[1] = 0.5f - frac;
            }
            test_glb_put_floats (bin, w, 4);
        }
    /* indices */
    off_indices = bin->len;
    for (r = 0; r < rings; r++)
        for (k = 0; k < sides; k++)
        {
            guint16 a = (guint16)(r * sides + k);
            guint16 b2 = (guint16)(r * sides + (k + 1) % sides);
            guint16 c2 = (guint16)((r + 1) * sides + k);
            guint16 d = (guint16)((r + 1) * sides + (k + 1) % sides);
            guint16 tri[6];

            tri[0] = a;  tri[1] = c2; tri[2] = b2;
            tri[3] = b2; tri[4] = c2; tri[5] = d;
            g_byte_array_append (bin, (const guint8 *)tri, sizeof tri);
        }
    test_glb_pad4 (bin);
    off_times = bin->len;
    test_glb_put_floats (bin, times, 3);
    /* rotations: rest, bent, rest for every clip and joint */
    off_rotations = bin->len;
    for (c = 0; c < n_clips; c++)
        for (b = 0; b < bones; b++)
        {
            gfloat angle = 0.6f * sinf ((gfloat)b * 0.7f + (gfloat)c * 1.3f + 0.5f);
            gfloat s = sinf (angle * 0.5f);
            gfloat q[12] = { 0, 0, 0, 1,  0, 0, 0, 1,  0, 0, 0, 1 };

            q[7] = cosf (angle * 0.5f);
            if (c % 2 == 0)
                q[6] = s;
            else
                q[4] = s;
            test_glb_put_floats (bin, q, 12);
        }

    /* nodes: Armature, the joint chain, then the body */
    g_string_append_printf (json,
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"Armature\",\"children\":[1,%u]}", bones + 1);
    for (b = 0; b < bones; b++)
    {
        gchar y[G_ASCII_DTOSTR_BUF_SIZE];

        /* locale-independent: JSON wants a decimal point */
        g_ascii_dtostr (y, sizeof y, b == 0 ? 0.0 : (gdouble)segment);
        g_string_append_printf (json, ",{\"name\":\"Bone%u\",\"translation\":[0,%s,0]", b, y);
        if (b + 1 < bones)
            g_string_append_printf (json, ",\"children\":[%u]", b + 2);
        g_string_append (json, "}");
    }
    g_string_append (json,
        ",{\"name\":\"Body\",\"mesh\":0,\"skin\":0}],"
        "\"meshes\":[{\"name\":\"Body\",\"primitives\":[{\"attributes\":{"
        "\"POSITION\":0,\"NORMAL\":1,\"COLOR_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4},"
        "\"indices\":5}]}],"
        "\"skins\":[{\"joints\":[");
    for (b = 0; b < bones; b++)
        g_string_append_printf (json, "%s%u", b > 0 ? "," : "", b + 1);
    g_string_append (json, "]}],\"animations\":[");
    for (c = 0; c < n_clips; c++)
    {
        g_string_append_printf (json, "%s{\"name\":\"%s\",\"channels\":[",
                                c > 0 ? "," : "", clip_names[c]);
        for (b = 0; b < bones; b++)
            g_string_append_printf (json,
                "%s{\"sampler\":%u,\"target\":{\"node\":%u,\"path\":\"rotation\"}}",
                b > 0 ? "," : "", b, b + 1);
        g_string_append (json, "],\"samplers\":[");
        for (b = 0; b < bones; b++)
            g_string_append_printf (json,
                "%s{\"input\":6,\"output\":%u,\"interpolation\":\"LINEAR\"}",
                b > 0 ? "," : "", 7 + c * bones + b);
        g_string_append (json, "]}");
    }

    /* buffer views 0..6, then one per rotation output */
    g_string_append_printf (json,
        "],\"buffers\":[{\"byteLength\":%u}],\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":12}",
        bin->len, n_vertices * 12,
        (guint)off_normals, n_vertices * 12,
        (guint)off_colors, n_vertices * 4,
        (guint)off_joints, n_vertices * 4,
        (guint)off_weights, n_vertices * 16,
        (guint)off_indices, n_indices * 2,
        (guint)off_times);
    for (c = 0; c < n_clips * bones; c++)
        g_string_append_printf (json, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":48}",
                                (guint)off_rotations + c * 48);
    g_string_append_printf (json,
        "],\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
        "\"min\":[-0.25,0,-0.25],\"max\":[0.25,2,0.25]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5121,\"normalized\":true,\"count\":%u,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5121,\"count\":%u,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
        "{\"bufferView\":5,\"componentType\":5123,\"count\":%u,\"type\":\"SCALAR\"},"
        "{\"bufferView\":6,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]}",
        n_vertices,
        n_vertices, n_vertices, n_vertices, n_vertices, n_indices);
    for (c = 0; c < n_clips * bones; c++)
        g_string_append_printf (json,
            ",{\"bufferView\":%u,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}",
            7 + c);
    g_string_append (json, "]}");

    test_glb_write (path, json->str, bin->data, bin->len);
    g_string_free (json, TRUE);
    g_byte_array_unref (bin);
}
