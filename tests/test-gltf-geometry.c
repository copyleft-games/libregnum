/* test-gltf-geometry.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless unit tests for LrgGltfGeometry: a GLB quad with normals,
 * texture coordinates, colours and u16 indices placed by a node tree,
 * strips and fans, mirrored nodes, materials and an embedded PNG,
 * buffers in data: URIs and external files (and the refusal of paths
 * leaving the model's folder), the scene choice, and malformed data:
 * overrunning accessors, indices past the vertex count, Draco, sparse
 * accessors, cycles, and every truncation of a valid file. Fixtures are
 * generated per test.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#include "lrg-test-glb.h"

/*
 * Quad binary layout (byte offsets):
 *   0  positions  4 x VEC3 float (48)
 *  48  normals    4 x VEC3 float (48)
 *  96  texcoords  4 x VEC2 float (32)
 * 128  colours    4 x VEC4 u8 normalized (16)
 * 144  indices    6 x u16 (12)
 * = 156 bytes
 */
#define QUAD_BIN_SIZE (156)

static void
quad_bin (guint8 *bin)
{
    static const gfloat positions[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    static const gfloat normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    static const gfloat uvs[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    static const guint8 colors[16] = { 255, 0, 0, 255, 0, 255, 0, 255,
                                       0, 0, 255, 255, 255, 255, 255, 128 };
    static const guint16 indices[6] = { 0, 1, 2, 0, 2, 3 };

    memcpy (bin, positions, 48);
    memcpy (bin + 48, normals, 48);
    memcpy (bin + 96, uvs, 32);
    memcpy (bin + 128, colors, 16);
    memcpy (bin + 144, indices, 12);
}

/* quad_json:
 * @buffer: the "buffers" entry (with or without uri)
 * @nodes: the "nodes" array contents
 * @extra: extra top-level members (leading comma) or "" */
static gchar *
quad_json (const gchar *buffer,
           const gchar *nodes,
           const gchar *extra)
{
    return g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},%s"
        "\"buffers\":[%s],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":144},"
        "{\"buffer\":0,\"byteOffset\":144,\"byteLength\":12}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":96,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
        "{\"bufferView\":0,\"byteOffset\":128,\"componentType\":5121,\"normalized\":true,"
        "\"count\":4,\"type\":\"VEC4\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"name\":\"Quad\",\"primitives\":[{\"attributes\":{\"POSITION\":0,"
        "\"NORMAL\":1,\"TEXCOORD_0\":2,\"COLOR_0\":3},\"indices\":4,\"material\":0}]}],"
        "\"materials\":[{\"name\":\"TEXTURE.210 [Index=3]\","
        "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1]},"
        "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,\"doubleSided\":true}],"
        "\"nodes\":[%s]}",
        extra, buffer, nodes);
}

static GBytes *
quad_glb (const gchar *nodes,
          const gchar *extra)
{
    guint8            bin[QUAD_BIN_SIZE];
    g_autofree gchar *json = quad_json ("{\"byteLength\":156}", nodes, extra);

    quad_bin (bin);
    return test_glb_build (json, bin, sizeof (bin));
}

static LrgGltfGeometry *
geometry_from_json_glb (const gchar *json,
                        const guint8 *bin,
                        gsize         bin_len,
                        GError      **error)
{
    g_autoptr(GBytes) bytes = test_glb_build (json, bin, bin_len);

    return lrg_gltf_geometry_new_from_bytes (bytes, NULL, error);
}

static void
assert_vec3 (const gfloat *v,
             gfloat        x,
             gfloat        y,
             gfloat        z)
{
    g_assert_cmpfloat_with_epsilon (v[0], x, 1e-5);
    g_assert_cmpfloat_with_epsilon (v[1], y, 1e-5);
    g_assert_cmpfloat_with_epsilon (v[2], z, 1e-5);
}

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

/* The quad placed by a child node: parent translates by (10, 0, 0), child
 * scales by 2 and rotates 90 degrees about Y. */
static void
test_quad_placed (void)
{
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    g_autoptr(GBytes)          bytes = NULL;
    const gfloat              *positions;
    const gfloat              *normals;
    const gfloat              *uvs;
    const guint8              *colors;
    const guint32             *indices;
    guint                      n_vertices = 0;
    guint                      n_indices = 0;
    gfloat                     color[4];
    gfloat                     cutoff = 0.0f;
    gfloat                     min[3];
    gfloat                     max[3];
    const gfloat               h = (gfloat)(G_SQRT2 / 2.0);
    gchar                     *nodes;

    nodes = g_strdup_printf ("{\"name\":\"Root\",\"translation\":[10,0,0],\"children\":[1]},"
                             "{\"name\":\"Child\",\"mesh\":0,\"scale\":[2,2,2],"
                             "\"rotation\":[0,%f,0,%f]}", h, h);
    bytes = quad_glb (nodes, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],");
    g_free (nodes);
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (geometry);

    g_assert_cmpuint (lrg_gltf_geometry_get_primitive_count (geometry), ==, 1);
    g_assert_cmpuint (lrg_gltf_geometry_get_triangle_count (geometry), ==, 2);
    g_assert_cmpuint (lrg_gltf_geometry_get_skipped_count (geometry), ==, 0);
    g_assert_cmpstr (lrg_gltf_geometry_get_primitive_node_name (geometry, 0), ==, "Child");

    /* rotation +90 about Y maps (x, y, z) to (z, y, -x) */
    positions = lrg_gltf_geometry_get_positions (geometry, 0, &n_vertices);
    g_assert_cmpuint (n_vertices, ==, 4);
    assert_vec3 (&positions[0], 10, 0, 0);
    assert_vec3 (&positions[3], 10, 0, -2);
    assert_vec3 (&positions[6], 10, 2, -2);
    normals = lrg_gltf_geometry_get_normals (geometry, 0);
    g_assert_nonnull (normals);
    assert_vec3 (&normals[0], 1, 0, 0);

    uvs = lrg_gltf_geometry_get_texcoords (geometry, 0);
    g_assert_cmpfloat_with_epsilon (uvs[0], 0.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (uvs[1], 1.0, 1e-6);
    colors = lrg_gltf_geometry_get_colors (geometry, 0);
    g_assert_cmpuint (colors[0], ==, 255);
    g_assert_cmpuint (colors[1], ==, 0);
    g_assert_cmpuint (colors[15], ==, 128);

    indices = lrg_gltf_geometry_get_indices (geometry, 0, &n_indices);
    g_assert_cmpuint (n_indices, ==, 6);
    g_assert_cmpuint (indices[0], ==, 0);
    g_assert_cmpuint (indices[1], ==, 1);
    g_assert_cmpuint (indices[2], ==, 2);
    g_assert_cmpuint (indices[5], ==, 3);

    g_assert_true (lrg_gltf_geometry_get_bounds (geometry, min, max));
    assert_vec3 (min, 10, 0, -2);
    assert_vec3 (max, 10, 2, 0);

    g_assert_cmpuint (lrg_gltf_geometry_get_material_count (geometry), ==, 1);
    g_assert_cmpint (lrg_gltf_geometry_get_primitive_material (geometry, 0), ==, 0);
    g_assert_cmpstr (lrg_gltf_geometry_get_material_name (geometry, 0), ==,
                     "TEXTURE.210 [Index=3]");
    g_assert_true (lrg_gltf_geometry_get_material_color (geometry, 0, color));
    g_assert_cmpfloat_with_epsilon (color[1], 0.5, 1e-6);
    g_assert_cmpint (lrg_gltf_geometry_get_material_image (geometry, 0), ==, -1);
    g_assert_cmpint (lrg_gltf_geometry_get_material_alpha_mode (geometry, 0, &cutoff), ==,
                     LRG_GLTF_ALPHA_MODE_MASK);
    g_assert_cmpfloat_with_epsilon (cutoff, 0.25, 1e-6);
    g_assert_true (lrg_gltf_geometry_get_material_double_sided (geometry, 0));

    /* out of range lookups */
    g_assert_null (lrg_gltf_geometry_get_positions (geometry, 1, &n_vertices));
    g_assert_cmpuint (n_vertices, ==, 0);
    g_assert_cmpint (lrg_gltf_geometry_get_primitive_material (geometry, 9), ==, -1);
    g_assert_false (lrg_gltf_geometry_get_material_color (geometry, 3, color));
}

/* Without scenes every root node is placed; an instanced mesh appears
 * once per node; a mirrored node reverses the winding. */
static void
test_roots_and_mirror (void)
{
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    g_autoptr(GBytes)          bytes = NULL;
    const guint32             *indices;
    const gfloat              *normals;
    guint                      n = 0;

    bytes = quad_glb ("{\"mesh\":0},{\"mesh\":0,\"scale\":[-1,1,1]}", "");
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_geometry_get_primitive_count (geometry), ==, 2);
    g_assert_cmpuint (lrg_gltf_geometry_get_triangle_count (geometry), ==, 4);

    indices = lrg_gltf_geometry_get_indices (geometry, 1, &n);
    g_assert_cmpuint (indices[0], ==, 0);
    g_assert_cmpuint (indices[1], ==, 2);
    g_assert_cmpuint (indices[2], ==, 1);
    /* a mirror in X keeps a +Z normal */
    normals = lrg_gltf_geometry_get_normals (geometry, 1);
    assert_vec3 (normals, 0, 0, 1);
}

/* A scene that lists only one root ignores the others. */
static void
test_scene_selection (void)
{
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    g_autoptr(GBytes)          bytes = NULL;
    const gfloat              *positions;

    bytes = quad_glb ("{\"mesh\":0},{\"mesh\":0,\"translation\":[0,5,0]}",
                      "\"scene\":1,\"scenes\":[{\"nodes\":[0]},{\"nodes\":[1]}],");
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_geometry_get_primitive_count (geometry), ==, 1);
    positions = lrg_gltf_geometry_get_positions (geometry, 0, NULL);
    assert_vec3 (positions, 0, 5, 0);
}

/* Strips and fans unroll with the specification's order; lines and
 * points are skipped; non-indexed primitives get sequential indices. */
static void
test_strip_fan_lines (void)
{
    static const gfloat positions[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 };
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    const guint32             *indices;
    guint                      n = 0;
    const gchar               *json =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":48}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":5},"
        "{\"attributes\":{\"POSITION\":0},\"mode\":6},"
        "{\"attributes\":{\"POSITION\":0},\"mode\":1},"
        "{\"attributes\":{\"POSITION\":0}}]}],"
        "\"nodes\":[{\"mesh\":0}]}";

    geometry = geometry_from_json_glb (json, (const guint8 *)positions, 48, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_geometry_get_primitive_count (geometry), ==, 3);
    g_assert_cmpuint (lrg_gltf_geometry_get_skipped_count (geometry), ==, 1);

    /* strip: (0 1 2) (1 3 2) */
    indices = lrg_gltf_geometry_get_indices (geometry, 0, &n);
    g_assert_cmpuint (n, ==, 6);
    g_assert_cmpuint (indices[3], ==, 1);
    g_assert_cmpuint (indices[4], ==, 3);
    g_assert_cmpuint (indices[5], ==, 2);
    /* fan: (1 2 0) (2 3 0) */
    indices = lrg_gltf_geometry_get_indices (geometry, 1, &n);
    g_assert_cmpuint (n, ==, 6);
    g_assert_cmpuint (indices[0], ==, 1);
    g_assert_cmpuint (indices[2], ==, 0);
    g_assert_cmpuint (indices[3], ==, 2);
    /* list without indices: 4 vertices make one triangle */
    indices = lrg_gltf_geometry_get_indices (geometry, 2, &n);
    g_assert_cmpuint (n, ==, 3);
    g_assert_cmpuint (indices[2], ==, 2);
    g_assert_null (lrg_gltf_geometry_get_normals (geometry, 2));
    g_assert_null (lrg_gltf_geometry_get_texcoords (geometry, 2));
    g_assert_null (lrg_gltf_geometry_get_colors (geometry, 2));
}

/* A plain .gltf with a base64 buffer, and one with an external .bin next
 * to it; "../" and absolute URIs are refused. */
static void
test_uris (void)
{
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    g_autofree gchar          *dir = NULL;
    g_autofree gchar          *b64 = NULL;
    g_autofree gchar          *buffer = NULL;
    g_autofree gchar          *json = NULL;
    g_autofree gchar          *gltf_path = NULL;
    g_autofree gchar          *bin_path = NULL;
    g_autoptr(GBytes)          bytes = NULL;
    guint8                     bin[QUAD_BIN_SIZE];

    quad_bin (bin);

    /* data: URI, no base directory needed */
    b64 = g_base64_encode (bin, sizeof (bin));
    buffer = g_strdup_printf ("{\"byteLength\":156,"
                              "\"uri\":\"data:application/octet-stream;base64,%s\"}", b64);
    json = quad_json (buffer, "{\"mesh\":0}", "");
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_geometry_get_triangle_count (geometry), ==, 2);
    g_clear_object (&geometry);
    g_clear_pointer (&bytes, g_bytes_unref);
    g_clear_pointer (&json, g_free);
    g_clear_pointer (&buffer, g_free);

    /* external file, found next to the .gltf */
    dir = g_dir_make_tmp ("lrg-gltf-geometry-XXXXXX", &error);
    g_assert_no_error (error);
    bin_path = g_build_filename (dir, "quad data.bin", NULL);
    test_glb_write_bytes (bin_path, bin, sizeof (bin));
    json = quad_json ("{\"byteLength\":156,\"uri\":\"quad%20data.bin\"}", "{\"mesh\":0}", "");
    gltf_path = g_build_filename (dir, "quad.gltf", NULL);
    test_glb_write_bytes (gltf_path, (const guint8 *)json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_file (gltf_path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_geometry_get_triangle_count (geometry), ==, 2);
    g_clear_object (&geometry);

    /* the same bytes without a base directory: unsupported */
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, NULL, &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);
    g_clear_pointer (&json, g_free);

    /* leaving the directory, absolute paths and remote URIs */
    json = quad_json ("{\"byteLength\":156,\"uri\":\"../quad%20data.bin\"}", "{\"mesh\":0}", "");
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, dir, &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);
    g_clear_pointer (&json, g_free);

    json = quad_json ("{\"byteLength\":156,\"uri\":\"/etc/passwd\"}", "{\"mesh\":0}", "");
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, dir, &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);
    g_clear_pointer (&json, g_free);

    json = quad_json ("{\"byteLength\":156,\"uri\":\"https://example.invalid/q.bin\"}",
                      "{\"mesh\":0}", "");
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, dir, &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_clear_error (&error);

    /* a missing external file is a file error */
    g_clear_pointer (&bytes, g_bytes_unref);
    g_clear_pointer (&json, g_free);
    json = quad_json ("{\"byteLength\":156,\"uri\":\"missing.bin\"}", "{\"mesh\":0}", "");
    bytes = g_bytes_new (json, strlen (json));
    geometry = lrg_gltf_geometry_new_from_bytes (bytes, dir, &error);
    g_assert_null (geometry);
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

    g_unlink (bin_path);
    g_unlink (gltf_path);
    g_rmdir (dir);
}

/* A material whose base colour texture is a PNG embedded in the BIN
 * chunk decodes without a graphics context. */
static void
test_embedded_image (void)
{
    static const guint8 pixels[16] = { 255, 0, 0, 255, 0, 255, 0, 255,
                                       0, 0, 255, 255, 255, 255, 255, 255 };
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    g_autoptr(GrlImage)        source = NULL;
    g_autoptr(GrlImage)        image = NULL;
    g_autoptr(GBytes)          data = NULL;
    g_autofree guint8         *png = NULL;
    g_autofree gchar          *json = NULL;
    GByteArray                *bin;
    const gchar               *mime = NULL;
    gsize                      png_size = 0;
    static const gfloat        positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };

    source = grl_image_new_from_pixels (2, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels);
    png = grl_image_export_to_memory (source, ".png", &png_size);
    g_assert_nonnull (png);

    bin = g_byte_array_new ();
    g_byte_array_append (bin, (const guint8 *)positions, 36);
    g_byte_array_append (bin, png, (guint)png_size);
    json = g_strdup_printf (
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":%" G_GSIZE_FORMAT "}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
        "\"images\":[{\"bufferView\":1,\"mimeType\":\"image/png\"},{\"uri\":\"data:,notanimage\"}],"
        "\"textures\":[{\"source\":0},{\"source\":1}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}},"
        "{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":1}}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
        "\"nodes\":[{\"mesh\":0}]}", bin->len, png_size);
    geometry = geometry_from_json_glb (json, bin->data, bin->len, &error);
    g_byte_array_unref (bin);
    g_assert_no_error (error);

    g_assert_cmpuint (lrg_gltf_geometry_get_image_count (geometry), ==, 2);
    g_assert_cmpint (lrg_gltf_geometry_get_material_image (geometry, 0), ==, 0);
    g_assert_cmpint (lrg_gltf_geometry_get_material_image (geometry, 1), ==, 1);
    data = lrg_gltf_geometry_get_image_data (geometry, 0, &mime, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (data), ==, png_size);
    g_assert_cmpstr (mime, ==, "image/png");

    image = lrg_gltf_geometry_load_image (geometry, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpint (grl_image_get_width (image), ==, 2);
    g_assert_cmpint (grl_image_get_height (image), ==, 2);

    /* garbage bytes, and an index out of range */
    g_assert_null (lrg_gltf_geometry_load_image (geometry, 1, &error));
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_null (lrg_gltf_geometry_load_image (geometry, 2, &error));
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
}

static void
assert_quad_error (const gchar *json,
                   GQuark       domain,
                   gint         code)
{
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;
    guint8                     bin[QUAD_BIN_SIZE];

    quad_bin (bin);
    geometry = geometry_from_json_glb (json, bin, sizeof (bin), &error);
    g_assert_null (geometry);
    g_assert_error (error, domain, code);
}

static gchar *
replace (const gchar *text,
         const gchar *from,
         const gchar *to)
{
    g_auto(GStrv) parts = g_strsplit (text, from, 2);

    g_assert_nonnull (parts[1]);
    return g_strjoin (to, parts[0], parts[1], NULL);
}

/* Malformed geometry is refused with a precise code. */
static void
test_malformed (void)
{
    g_autofree gchar *base = quad_json ("{\"byteLength\":156}", "{\"mesh\":0}", "");
    gchar            *json;
    guint8            bin[QUAD_BIN_SIZE];
    guint8            bad[QUAD_BIN_SIZE];
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGltfGeometry) geometry = NULL;

    /* accessor overruns its view */
    json = replace (base, "\"count\":4,\"type\":\"VEC3\"}", "\"count\":40,\"type\":\"VEC3\"}");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_free (json);
    /* view overruns its buffer */
    json = replace (base, "\"byteOffset\":144,\"byteLength\":12", "\"byteOffset\":150,\"byteLength\":12");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_free (json);
    /* buffer declares more than the BIN chunk holds */
    json = replace (base, "{\"byteLength\":156}", "{\"byteLength\":4096}");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_free (json);
    /* attribute counts differ */
    json = replace (base, "\"byteOffset\":48,\"componentType\":5126,\"count\":4",
                    "\"byteOffset\":48,\"componentType\":5126,\"count\":3");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_free (json);
    /* sparse accessor */
    json = replace (base, "\"type\":\"SCALAR\"}", "\"type\":\"SCALAR\",\"sparse\":{}}");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_free (json);
    /* Draco */
    json = replace (base, "\"indices\":4,", "\"indices\":4,\"extensions\":"
                    "{\"KHR_draco_mesh_compression\":{}},");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_free (json);
    /* Draco required by the file (meshopt is decoded: test-meshopt.c) */
    json = replace (base, "\"asset\":", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],"
                    "\"asset\":");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_free (json);
    /* missing POSITION, bad references */
    json = replace (base, "\"POSITION\":0,", "");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_free (json);
    json = replace (base, "\"material\":0", "\"material\":7");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
    g_free (json);
    json = replace (base, "\"nodes\":[{\"mesh\":0}]", "\"nodes\":[{\"mesh\":3}]");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
    g_free (json);
    /* cycles and doubly-parented nodes */
    json = replace (base, "\"nodes\":[{\"mesh\":0}]",
                    "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"children\":[1]},"
                    "{\"children\":[0]}]");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
    g_free (json);
    json = replace (base, "\"nodes\":[{\"mesh\":0}]",
                    "\"scenes\":[{\"nodes\":[0,1]}],\"nodes\":[{\"children\":[2]},"
                    "{\"children\":[2]},{\"mesh\":0}]");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
    g_free (json);
    /* wrong JSON types */
    json = replace (base, "\"nodes\":[{\"mesh\":0}]", "\"nodes\":{}");
    assert_quad_error (json, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON);
    g_free (json);

    /* an index past the vertex count */
    quad_bin (bin);
    memcpy (bad, bin, sizeof (bad));
    bad[144 + 10] = 9;
    geometry = geometry_from_json_glb (base, bad, sizeof (bad), &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a NaN position */
    memcpy (bad, bin, sizeof (bad));
    bad[0] = 0x00; bad[1] = 0x00; bad[2] = 0xC0; bad[3] = 0x7F;
    geometry = geometry_from_json_glb (base, bad, sizeof (bad), &error);
    g_assert_null (geometry);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* not glTF at all */
    {
        g_autoptr(GBytes) junk = g_bytes_new_static ("hello", 5);

        geometry = lrg_gltf_geometry_new_from_bytes (junk, NULL, &error);
        g_assert_null (geometry);
        g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER);
    }
}

/* Every truncation of a valid GLB is refused or read, never a crash. */
static void
test_truncations (void)
{
    g_autoptr(GBytes) full = quad_glb ("{\"mesh\":0}", "");
    gsize             size = 0;
    const guint8     *data = g_bytes_get_data (full, &size);
    gsize             cut;

    for (cut = 0; cut < size; cut++)
    {
        g_autoptr(GError)          error = NULL;
        g_autoptr(GBytes)          part = g_bytes_new (data, cut);
        g_autoptr(LrgGltfGeometry) geometry = NULL;

        geometry = lrg_gltf_geometry_new_from_bytes (part, NULL, &error);
        if (geometry == NULL)
            g_assert_nonnull (error);
    }

    /* and random byte flips in the binary chunk and JSON */
    {
        GRand *rand = g_rand_new_with_seed (7);
        guint  round;

        for (round = 0; round < 2000; round++)
        {
            g_autoptr(GError)          error = NULL;
            g_autoptr(LrgGltfGeometry) geometry = NULL;
            g_autoptr(GBytes)          mutated = NULL;
            guint8                    *copy = g_memdup2 (data, size);

            copy[g_rand_int_range (rand, 0, (gint32)size)] = (guint8)g_rand_int_range (rand, 0, 256);
            mutated = g_bytes_new_take (copy, size);
            geometry = lrg_gltf_geometry_new_from_bytes (mutated, NULL, &error);
            if (geometry == NULL)
                g_assert_nonnull (error);
        }
        g_rand_free (rand);
    }
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/gltf-geometry/quad-placed", test_quad_placed);
    g_test_add_func ("/gltf-geometry/roots-and-mirror", test_roots_and_mirror);
    g_test_add_func ("/gltf-geometry/scene-selection", test_scene_selection);
    g_test_add_func ("/gltf-geometry/strip-fan-lines", test_strip_fan_lines);
    g_test_add_func ("/gltf-geometry/uris", test_uris);
    g_test_add_func ("/gltf-geometry/embedded-image", test_embedded_image);
    g_test_add_func ("/gltf-geometry/malformed", test_malformed);
    g_test_add_func ("/gltf-geometry/truncations", test_truncations);

    return g_test_run ();
}
