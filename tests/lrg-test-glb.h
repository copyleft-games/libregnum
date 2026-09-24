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
