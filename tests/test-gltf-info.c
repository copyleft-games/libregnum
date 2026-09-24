/* test-gltf-info.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless unit tests for LrgGltfInfo: raylib mesh ordering (nodes in
 * array order, triangle primitives only, instanced meshes repeated),
 * naming of multi-primitive and unnamed meshes, node names, vertex
 * counts, skins, animations, Draco detection, node masks, plain JSON
 * glTF, and malformed containers. Fixtures are generated per test.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

#include "lrg-test-glb.h"

/* ========================================================================== */
/*                                 Helpers                                    */
/* ========================================================================== */

static gchar *
temp_path (const gchar *basename)
{
    return g_build_filename (g_get_tmp_dir (), basename, NULL);
}

/* info_from_json:
 * Wraps @json in a GLB container (no BIN chunk) and inspects it. */
static LrgGltfInfo *
info_from_json (const gchar  *json,
                GError      **error)
{
    g_autoptr(GBytes) bytes = test_glb_build (json, NULL, 0);

    return lrg_gltf_info_new_from_bytes (bytes, error);
}

static void
assert_json_error (const gchar *json,
                   gint         code)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = info_from_json (json, &error);

    g_assert_null (info);
    g_assert_error (error, LRG_GLTF_ERROR, code);
}

static void
assert_bytes_error (const guint8 *data,
                    gsize         length,
                    gint          code)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(GBytes)      bytes = g_bytes_new (data, length);
    g_autoptr(LrgGltfInfo) info = lrg_gltf_info_new_from_bytes (bytes, &error);

    g_assert_null (info);
    g_assert_error (error, LRG_GLTF_ERROR, code);
}

/*
 * Scene used by several tests:
 *   node 0 "Horse"  -> mesh 0 "Horse" (prims: tri, lines, tri+material 0)
 *   node 1 unnamed  -> mesh 1 unnamed (one tri)
 *   node 2 "Hat"    -> mesh 0 again (instanced)
 *   node 3 "Empty"  -> no mesh
 * The only scene lists node 3, proving scenes are ignored.
 */
static const gchar *multi_json =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[3]}],"
    "\"nodes\":[{\"name\":\"Horse\",\"mesh\":0},{\"mesh\":1},"
    "{\"name\":\"Hat\",\"mesh\":0},{\"name\":\"Empty\"}],"
    "\"meshes\":["
    "{\"name\":\"Horse\",\"primitives\":["
    "{\"attributes\":{\"POSITION\":0}},"
    "{\"attributes\":{\"POSITION\":1},\"mode\":1},"
    "{\"attributes\":{\"POSITION\":2},\"mode\":4,\"material\":0}]},"
    "{\"primitives\":[{\"attributes\":{\"POSITION\":1,\"NORMAL\":0}}]}],"
    "\"materials\":[{\"name\":\"Fur\"}],"
    "\"accessors\":[{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
    "{\"componentType\":5126,\"count\":5,\"type\":\"VEC3\"}]}";

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

static void
test_gltf_mesh_order (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = info_from_json (multi_json, &error);

    g_assert_no_error (error);
    g_assert_nonnull (info);
    g_assert_true (lrg_gltf_info_get_is_binary (info));
    g_assert_cmpuint (lrg_gltf_info_get_node_count (info), ==, 4);

    /* Node 0: primitives 0 and 2 (the lines primitive is skipped). Node 1:
     * one. Node 2: mesh 0 again. */
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 5);

    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 0), ==, "Horse#0");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 1), ==, "Horse#2");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 2), ==, "mesh1");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 3), ==, "Horse#0");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 4), ==, "Horse#2");
    g_assert_null (lrg_gltf_info_get_mesh_name (info, 5));

    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 0), ==, "Horse");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 1), ==, "Horse");
    g_assert_null (lrg_gltf_info_get_mesh_node_name (info, 2));
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 3), ==, "Hat");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 4), ==, "Hat");

    g_assert_cmpstr (lrg_gltf_info_get_mesh_source_name (info, 0), ==, "Horse");
    g_assert_null (lrg_gltf_info_get_mesh_source_name (info, 2));

    g_assert_cmpint (lrg_gltf_info_get_mesh_node_index (info, 2), ==, 1);
    g_assert_cmpint (lrg_gltf_info_get_mesh_node_index (info, 4), ==, 2);
    g_assert_cmpint (lrg_gltf_info_get_mesh_source_index (info, 2), ==, 1);
    g_assert_cmpint (lrg_gltf_info_get_mesh_source_index (info, 3), ==, 0);
    g_assert_cmpint (lrg_gltf_info_get_mesh_primitive (info, 1), ==, 2);
    g_assert_cmpint (lrg_gltf_info_get_mesh_primitive (info, 2), ==, 0);

    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 0), ==, 3);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 1), ==, 5);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 2), ==, 4);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 99), ==, 0);

    g_assert_cmpint (lrg_gltf_info_get_mesh_material (info, 0), ==, -1);
    g_assert_cmpint (lrg_gltf_info_get_mesh_material (info, 1), ==, 0);
    g_assert_cmpint (lrg_gltf_info_get_mesh_material (info, 99), ==, -1);
    g_assert_cmpint (lrg_gltf_info_get_mesh_node_index (info, 99), ==, -1);

    g_assert_cmpint (lrg_gltf_info_find_mesh_by_node (info, "Hat"), ==, 3);
    g_assert_cmpint (lrg_gltf_info_find_mesh_by_node (info, "Empty"), ==, -1);

    g_assert_false (lrg_gltf_info_get_uses_draco (info));
    g_assert_cmpuint (lrg_gltf_info_get_skin_count (info), ==, 0);
    g_assert_cmpuint (lrg_gltf_info_get_animation_count (info), ==, 0);
}

static void
test_gltf_single_primitive_names (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = NULL;

    /* KayKit style: meaningful node names, editor mesh names. */
    info = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"nodes\":[{\"name\":\"Knight_Helmet\",\"mesh\":1},{\"name\":\"1H_Sword\",\"mesh\":0}],"
        "\"meshes\":[{\"name\":\"Cube.125\",\"primitives\":[{\"attributes\":{}}]},"
        "{\"name\":\"Cube.124\",\"primitives\":[{\"attributes\":{}}]}]}", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 2);
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 0), ==, "Cube.124");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 0), ==, "Knight_Helmet");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 1), ==, "Cube.125");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 1), ==, "1H_Sword");
    /* No POSITION attribute: zero vertices. */
    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 0), ==, 0);
}

static void
test_gltf_unnamed_multi (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = NULL;

    info = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{}},{\"attributes\":{}}]}]}", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 2);
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 0), ==, "mesh0#0");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 1), ==, "mesh0#1");
    g_assert_null (lrg_gltf_info_get_mesh_node_name (info, 0));
}

static void
test_gltf_animations_skins (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = NULL;
    g_autofree gchar      *truncated = NULL;
    g_autofree gchar      *short_name = NULL;

    info = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"name\":\"a\"},{\"name\":\"b\"}],"
        "\"skins\":[{\"joints\":[0]},{\"joints\":[1]}],"
        "\"animations\":[{\"name\":\"Idle\",\"channels\":[],\"samplers\":[]},"
        "{\"channels\":[],\"samplers\":[]},"
        "{\"name\":\"CharacterArmature|CharacterArmature|Run_Forward\",\"channels\":[],\"samplers\":[]}]}",
        &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_get_skin_count (info), ==, 2);
    g_assert_cmpuint (lrg_gltf_info_get_animation_count (info), ==, 3);
    g_assert_cmpstr (lrg_gltf_info_get_animation_name (info, 0), ==, "Idle");
    g_assert_cmpstr (lrg_gltf_info_get_animation_name (info, 1), ==, "");
    g_assert_cmpstr (lrg_gltf_info_get_animation_name (info, 2), ==,
                     "CharacterArmature|CharacterArmature|Run_Forward");
    g_assert_null (lrg_gltf_info_get_animation_name (info, 3));

    truncated = lrg_gltf_info_get_animation_raylib_name (info, 2);
    g_assert_cmpuint (strlen (truncated), ==, LRG_GLTF_RAYLIB_NAME_MAX);
    g_assert_cmpstr (truncated, ==, "CharacterArmature|CharacterArma");
    short_name = lrg_gltf_info_get_animation_raylib_name (info, 0);
    g_assert_cmpstr (short_name, ==, "Idle");
    g_assert_null (lrg_gltf_info_get_animation_raylib_name (info, 7));
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 0);
}

static void
test_gltf_draco (void)
{
    g_autoptr(LrgGltfInfo) by_primitive = NULL;
    g_autoptr(LrgGltfInfo) by_used = NULL;
    g_autoptr(LrgGltfInfo) by_required = NULL;
    g_autoptr(GError)      error = NULL;

    by_primitive = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"name\":\"D\",\"primitives\":[{\"attributes\":{},"
        "\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0,\"attributes\":{}}}}]}]}",
        &error);
    g_assert_no_error (error);
    g_assert_true (lrg_gltf_info_get_uses_draco (by_primitive));
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (by_primitive), ==, 1);

    by_used = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_materials_unlit\","
        "\"KHR_draco_mesh_compression\"]}", &error);
    g_assert_no_error (error);
    g_assert_true (lrg_gltf_info_get_uses_draco (by_used));

    by_required = info_from_json (
        "{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]}",
        &error);
    g_assert_no_error (error);
    g_assert_true (lrg_gltf_info_get_uses_draco (by_required));
}

static void
test_gltf_node_mask (void)
{
    g_autoptr(LrgGltfInfo) info = info_from_json (multi_json, NULL);
    const gchar *hidden[] = { "Hat", "NoSuchNode", NULL };
    g_autoptr(GBytes) mask = NULL;
    g_autoptr(GBytes) all = NULL;
    const guint8 *bytes;
    gsize length = 0;

    mask = lrg_gltf_info_build_node_mask (info, hidden);
    bytes = g_bytes_get_data (mask, &length);
    g_assert_cmpuint (length, ==, 5);
    g_assert_cmpuint (bytes[0], ==, 1);
    g_assert_cmpuint (bytes[1], ==, 1);
    g_assert_cmpuint (bytes[2], ==, 1);  /* unnamed node never matches */
    g_assert_cmpuint (bytes[3], ==, 0);
    g_assert_cmpuint (bytes[4], ==, 0);

    all = lrg_gltf_info_build_node_mask (info, NULL);
    bytes = g_bytes_get_data (all, &length);
    g_assert_cmpuint (length, ==, 5);
    g_assert_cmpuint (bytes[3], ==, 1);
}

static void
test_gltf_files (void)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgGltfInfo) info = NULL;
    g_autofree gchar      *glb = temp_path ("lrg-gltf-info-skinned.glb");
    g_autofree gchar      *gltf = temp_path ("lrg-gltf-info-plain.gltf");
    g_autofree gchar      *missing = temp_path ("lrg-gltf-info-missing.glb");
    g_auto(GStrv)          names = NULL;
    const gchar *const     clips[] = { "Walk" };

    /* A GLB with a BIN chunk from the shared fixture writer. */
    test_glb_write_skinned_triangle (glb, clips, 1);
    info = lrg_gltf_info_new_from_file (glb, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 1);
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 0), ==, "Body");
    g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 0), ==, "Body");
    g_assert_cmpuint (lrg_gltf_info_get_mesh_vertex_count (info, 0), ==, 3);
    g_assert_cmpuint (lrg_gltf_info_get_skin_count (info), ==, 1);
    g_assert_cmpstr (lrg_gltf_info_get_animation_name (info, 0), ==, "Walk");
    g_clear_object (&info);

    names = lrg_gltf_read_mesh_names (glb, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_strv_length (names), ==, 1);
    g_assert_cmpstr (names[0], ==, "Body");

    /* Plain JSON glTF (with a BOM and leading whitespace). */
    g_assert_true (g_file_set_contents (gltf, "\xEF\xBB\xBF \n{\"asset\":{\"version\":\"2.0\"},"
                                        "\"nodes\":[{\"name\":\"N\",\"mesh\":0}],"
                                        "\"meshes\":[{\"name\":\"M\",\"primitives\":[{\"attributes\":{}}]}]}",
                                        -1, NULL));
    info = lrg_gltf_info_new_from_file (gltf, &error);
    g_assert_no_error (error);
    g_assert_false (lrg_gltf_info_get_is_binary (info));
    g_assert_cmpstr (lrg_gltf_info_get_mesh_name (info, 0), ==, "M");

    g_assert_null (lrg_gltf_info_new_from_file (missing, &error));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_clear_error (&error);
    g_assert_null (lrg_gltf_read_mesh_names (missing, &error));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

    g_remove (glb);
    g_remove (gltf);
}

static void
test_gltf_bad_container (void)
{
    g_autoptr(GBytes) good = test_glb_build (multi_json, NULL, 0);
    const guint8     *data;
    gsize             length = 0;
    guint8           *copy;
    guint8            junk[32];
    guint32           value;

    data = g_bytes_get_data (good, &length);

    /* Bad magic and non-JSON text. */
    memset (junk, 'x', sizeof junk);
    assert_bytes_error (junk, sizeof junk, LRG_GLTF_ERROR_INVALID_CONTAINER);
    assert_bytes_error ((const guint8 *)"", 0, LRG_GLTF_ERROR_INVALID_CONTAINER);

    /* Truncated: shorter than the headers, and shorter than declared. */
    assert_bytes_error (data, 16, LRG_GLTF_ERROR_INVALID_CONTAINER);
    assert_bytes_error (data, 40, LRG_GLTF_ERROR_INVALID_CONTAINER);
    assert_bytes_error (data, length - 4, LRG_GLTF_ERROR_INVALID_CONTAINER);

    copy = g_memdup2 (data, length);

    /* Version 1. */
    value = 1;
    memcpy (copy + 4, &value, 4);
    assert_bytes_error (copy, length, LRG_GLTF_ERROR_UNSUPPORTED_VERSION);
    value = 2;
    memcpy (copy + 4, &value, 4);

    /* Declared length below the minimum. */
    value = 12;
    memcpy (copy + 8, &value, 4);
    assert_bytes_error (copy, length, LRG_GLTF_ERROR_INVALID_CONTAINER);
    value = (guint32)length;
    memcpy (copy + 8, &value, 4);

    /* JSON chunk longer than the container. */
    value = (guint32)length;
    memcpy (copy + 12, &value, 4);
    assert_bytes_error (copy, length, LRG_GLTF_ERROR_INVALID_CONTAINER);
    value = (guint32)(length - 20);
    memcpy (copy + 12, &value, 4);

    /* First chunk not JSON. */
    memcpy (copy + 16, "BIN", 4);
    assert_bytes_error (copy, length, LRG_GLTF_ERROR_INVALID_CONTAINER);

    g_free (copy);
}

static void
test_gltf_bad_json (void)
{
    assert_json_error ("{\"asset\":", LRG_GLTF_ERROR_INVALID_JSON);
    assert_json_error ("[1,2,3]", LRG_GLTF_ERROR_INVALID_JSON);
    assert_json_error ("{\"nodes\":{}}", LRG_GLTF_ERROR_INVALID_JSON);
    assert_json_error ("{\"nodes\":[1]}", LRG_GLTF_ERROR_INVALID_JSON);
    assert_json_error ("{\"nodes\":[{\"name\":7,\"mesh\":0}],\"meshes\":[{\"primitives\":[]}]}",
                       LRG_GLTF_ERROR_INVALID_JSON);
    assert_json_error ("{\"animations\":[{\"name\":[]}]}", LRG_GLTF_ERROR_INVALID_JSON);
}

static void
test_gltf_bad_references (void)
{
    assert_json_error ("{\"nodes\":[{\"mesh\":0}]}", LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"mesh\":-1}],\"meshes\":[{\"primitives\":[]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"mesh\":0.5}],\"meshes\":[{\"primitives\":[]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"mesh\":\"0\"}],\"meshes\":[{\"primitives\":[]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":"
                       "[{\"attributes\":{\"POSITION\":3}}]}],\"accessors\":[{\"count\":1}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":"
                       "[{\"attributes\":{},\"material\":0}]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);

    /* Structural rules cgltf enforces (raylib then loads nothing). */
    assert_json_error ("{\"nodes\":[{\"children\":[1]},{}],\"scenes\":[{\"nodes\":[1]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"children\":[2]},{\"children\":[2]},{}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"children\":[5]}]}", LRG_GLTF_ERROR_INVALID_REFERENCE);
    /* Regression: a node listing itself as a child hung the joint root
     * fix-up (its parent walk never advanced). */
    assert_json_error ("{\"nodes\":[{},{\"children\":[1]}]}", LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{}],\"skins\":[{\"joints\":[0,1]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"nodes\":[{\"skin\":0}]}", LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"bufferViews\":[{\"byteLength\":4}]}", LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"accessors\":[{\"bufferView\":0,\"count\":1}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"meshes\":[{\"primitives\":[{\"attributes\":{\"NORMAL\":0}}]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"meshes\":[{\"primitives\":[{\"attributes\":{},\"indices\":0}]}]}",
                       LRG_GLTF_ERROR_INVALID_REFERENCE);
    assert_json_error ("{\"scenes\":[],\"scene\":0}", LRG_GLTF_ERROR_INVALID_REFERENCE);

    /* A parented node that is not a scene root is fine. */
    {
        g_autoptr(GError)      error = NULL;
        g_autoptr(LrgGltfInfo) info = info_from_json (
            "{\"nodes\":[{\"children\":[1]},{\"name\":\"Child\",\"mesh\":0}],"
            "\"scenes\":[{\"nodes\":[0]}],\"scene\":0,"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}]}", &error);

        g_assert_no_error (error);
        g_assert_cmpstr (lrg_gltf_info_get_mesh_node_name (info, 0), ==, "Child");
    }

    /* An integral float index is accepted, like cgltf. */
    {
        g_autoptr(GError)      error = NULL;
        g_autoptr(LrgGltfInfo) info = info_from_json (
            "{\"nodes\":[{\"mesh\":0.0}],\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}]}", &error);

        g_assert_no_error (error);
        g_assert_cmpuint (lrg_gltf_info_get_mesh_count (info), ==, 1);
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/gltf-info/mesh-order", test_gltf_mesh_order);
    g_test_add_func ("/gltf-info/single-primitive-names", test_gltf_single_primitive_names);
    g_test_add_func ("/gltf-info/unnamed-multi", test_gltf_unnamed_multi);
    g_test_add_func ("/gltf-info/animations-skins", test_gltf_animations_skins);
    g_test_add_func ("/gltf-info/draco", test_gltf_draco);
    g_test_add_func ("/gltf-info/node-mask", test_gltf_node_mask);
    g_test_add_func ("/gltf-info/files", test_gltf_files);
    g_test_add_func ("/gltf-info/bad-container", test_gltf_bad_container);
    g_test_add_func ("/gltf-info/bad-json", test_gltf_bad_json);
    g_test_add_func ("/gltf-info/bad-references", test_gltf_bad_references);

    return g_test_run ();
}
