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
