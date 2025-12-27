/* test-scene.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for Scene module (LrgScene, LrgSceneEntity, LrgSceneObject,
 * LrgMaterial3D, LrgSceneSerializer, LrgSceneSerializerYaml).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgScene       *scene;
    LrgSceneEntity *entity;
    LrgSceneObject *object;
    LrgMaterial3D  *material;
} SceneFixture;

static void
scene_fixture_set_up (SceneFixture *fixture,
                      gconstpointer user_data)
{
    fixture->scene    = lrg_scene_new ("test-scene");
    fixture->entity   = lrg_scene_entity_new ("test-entity");
    fixture->object   = lrg_scene_object_new ("test-object", LRG_PRIMITIVE_CUBE);
    fixture->material = lrg_material3d_new ();

    g_assert_nonnull (fixture->scene);
    g_assert_nonnull (fixture->entity);
    g_assert_nonnull (fixture->object);
    g_assert_nonnull (fixture->material);
}

static void
scene_fixture_tear_down (SceneFixture *fixture,
                         gconstpointer user_data)
{
    g_clear_object (&fixture->material);
    g_clear_object (&fixture->object);
    g_clear_object (&fixture->entity);
    g_clear_object (&fixture->scene);
}

/* ==========================================================================
 * Test Cases - LrgMaterial3D
 * ========================================================================== */

static void
test_material3d_new (void)
{
    g_autoptr(LrgMaterial3D) material = NULL;

    material = lrg_material3d_new ();

    g_assert_nonnull (material);
    g_assert_true (LRG_IS_MATERIAL3D (material));
}

static void
test_material3d_color (SceneFixture *fixture,
                       gconstpointer user_data)
{
    gfloat r, g, b, a;

    /* Set color */
    lrg_material3d_set_color (fixture->material, 0.5f, 0.25f, 0.75f, 1.0f);

    /* Get color */
    lrg_material3d_get_color (fixture->material, &r, &g, &b, &a);

    g_assert_cmpfloat_with_epsilon (r, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (g, 0.25f, 0.001f);
    g_assert_cmpfloat_with_epsilon (b, 0.75f, 0.001f);
    g_assert_cmpfloat_with_epsilon (a, 1.0f, 0.001f);
}

static void
test_material3d_roughness (SceneFixture *fixture,
                           gconstpointer user_data)
{
    lrg_material3d_set_roughness (fixture->material, 0.7f);

    g_assert_cmpfloat_with_epsilon (lrg_material3d_get_roughness (fixture->material),
                                    0.7f, 0.001f);
}

static void
test_material3d_metallic (SceneFixture *fixture,
                          gconstpointer user_data)
{
    lrg_material3d_set_metallic (fixture->material, 0.9f);

    g_assert_cmpfloat_with_epsilon (lrg_material3d_get_metallic (fixture->material),
                                    0.9f, 0.001f);
}

static void
test_material3d_emission (SceneFixture *fixture,
                          gconstpointer user_data)
{
    gfloat r, g, b, a;

    lrg_material3d_set_emission_color (fixture->material, 1.0f, 0.0f, 0.0f, 1.0f);
    lrg_material3d_set_emission_strength (fixture->material, 5.0f);

    lrg_material3d_get_emission_color (fixture->material, &r, &g, &b, &a);

    g_assert_cmpfloat_with_epsilon (r, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (g, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (b, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (a, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_material3d_get_emission_strength (fixture->material),
                                    5.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgSceneObject
 * ========================================================================== */

static void
test_scene_object_new (void)
{
    g_autoptr(LrgSceneObject) object = NULL;

    object = lrg_scene_object_new ("my-object", LRG_PRIMITIVE_UV_SPHERE);

    g_assert_nonnull (object);
    g_assert_true (LRG_IS_SCENE_OBJECT (object));
    g_assert_cmpstr (lrg_scene_object_get_name (object), ==, "my-object");
    g_assert_cmpint (lrg_scene_object_get_primitive (object), ==, LRG_PRIMITIVE_UV_SPHERE);
}

static void
test_scene_object_transform (SceneFixture *fixture,
                             gconstpointer user_data)
{
    GrlVector3 *loc;
    GrlVector3 *rot;
    GrlVector3 *scl;

    /* Set transforms */
    lrg_scene_object_set_location_xyz (fixture->object, 1.0f, 2.0f, 3.0f);
    lrg_scene_object_set_rotation_xyz (fixture->object, 0.1f, 0.2f, 0.3f);
    lrg_scene_object_set_scale_xyz (fixture->object, 2.0f, 2.0f, 2.0f);

    /* Get transforms (transfer none - do not free) */
    loc = lrg_scene_object_get_location (fixture->object);
    rot = lrg_scene_object_get_rotation (fixture->object);
    scl = lrg_scene_object_get_scale (fixture->object);

    g_assert_nonnull (loc);
    g_assert_nonnull (rot);
    g_assert_nonnull (scl);

    g_assert_cmpfloat_with_epsilon (loc->x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->y, 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->z, 3.0f, 0.001f);

    g_assert_cmpfloat_with_epsilon (rot->x, 0.1f, 0.001f);
    g_assert_cmpfloat_with_epsilon (rot->y, 0.2f, 0.001f);
    g_assert_cmpfloat_with_epsilon (rot->z, 0.3f, 0.001f);

    g_assert_cmpfloat_with_epsilon (scl->x, 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (scl->y, 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (scl->z, 2.0f, 0.001f);
}

static void
test_scene_object_material (SceneFixture *fixture,
                            gconstpointer user_data)
{
    LrgMaterial3D *retrieved;

    lrg_scene_object_set_material (fixture->object, fixture->material);

    retrieved = lrg_scene_object_get_material (fixture->object);

    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == fixture->material);
}

static void
test_scene_object_params_float (SceneFixture *fixture,
                                gconstpointer user_data)
{
    lrg_scene_object_set_param_float (fixture->object, "radius", 2.5f);

    g_assert_cmpfloat_with_epsilon (
        lrg_scene_object_get_param_float (fixture->object, "radius", 0.0f),
        2.5f, 0.001f);
}

static void
test_scene_object_params_int (SceneFixture *fixture,
                              gconstpointer user_data)
{
    lrg_scene_object_set_param_int (fixture->object, "vertices", 32);

    g_assert_cmpint (
        lrg_scene_object_get_param_int (fixture->object, "vertices", 0),
        ==, 32);
}

static void
test_scene_object_params_bool (SceneFixture *fixture,
                               gconstpointer user_data)
{
    lrg_scene_object_set_param_bool (fixture->object, "cap_ends", TRUE);

    g_assert_true (
        lrg_scene_object_get_param_bool (fixture->object, "cap_ends", FALSE));
}

/* ==========================================================================
 * Test Cases - LrgSceneEntity
 * ========================================================================== */

static void
test_scene_entity_new (void)
{
    g_autoptr(LrgSceneEntity) entity = NULL;

    entity = lrg_scene_entity_new ("my-entity");

    g_assert_nonnull (entity);
    g_assert_true (LRG_IS_SCENE_ENTITY (entity));
    g_assert_cmpstr (lrg_scene_entity_get_name (entity), ==, "my-entity");
}

static void
test_scene_entity_transform (SceneFixture *fixture,
                             gconstpointer user_data)
{
    GrlVector3 *loc;
    GrlVector3 *rot;
    GrlVector3 *scl;

    lrg_scene_entity_set_location_xyz (fixture->entity, 10.0f, 20.0f, 30.0f);
    lrg_scene_entity_set_rotation_xyz (fixture->entity, 1.0f, 2.0f, 3.0f);
    lrg_scene_entity_set_scale_xyz (fixture->entity, 0.5f, 0.5f, 0.5f);

    /* Get transforms (transfer none - do not free) */
    loc = lrg_scene_entity_get_location (fixture->entity);
    rot = lrg_scene_entity_get_rotation (fixture->entity);
    scl = lrg_scene_entity_get_scale (fixture->entity);

    g_assert_cmpfloat_with_epsilon (loc->x, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->y, 20.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->z, 30.0f, 0.001f);

    g_assert_cmpfloat_with_epsilon (rot->x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (rot->y, 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (rot->z, 3.0f, 0.001f);

    g_assert_cmpfloat_with_epsilon (scl->x, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (scl->y, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (scl->z, 0.5f, 0.001f);
}

static void
test_scene_entity_add_object (SceneFixture *fixture,
                              gconstpointer user_data)
{
    GPtrArray *objects;

    lrg_scene_entity_add_object (fixture->entity, fixture->object);

    objects = lrg_scene_entity_get_objects (fixture->entity);

    g_assert_nonnull (objects);
    g_assert_cmpuint (objects->len, ==, 1);
    g_assert_true (g_ptr_array_index (objects, 0) == fixture->object);
}

static void
test_scene_entity_find_object (SceneFixture *fixture,
                               gconstpointer user_data)
{
    LrgSceneObject *found;
    g_autoptr(LrgSceneObject) obj1 = NULL;
    g_autoptr(LrgSceneObject) obj2 = NULL;

    obj1 = lrg_scene_object_new ("part1", LRG_PRIMITIVE_CUBE);
    obj2 = lrg_scene_object_new ("part2", LRG_PRIMITIVE_UV_SPHERE);

    lrg_scene_entity_add_object (fixture->entity, obj1);
    lrg_scene_entity_add_object (fixture->entity, obj2);

    found = lrg_scene_entity_find_object (fixture->entity, "part2");

    g_assert_nonnull (found);
    g_assert_true (found == obj2);
    g_assert_cmpstr (lrg_scene_object_get_name (found), ==, "part2");
}

static void
test_scene_entity_find_object_not_found (SceneFixture *fixture,
                                         gconstpointer user_data)
{
    LrgSceneObject *found;

    found = lrg_scene_entity_find_object (fixture->entity, "nonexistent");

    g_assert_null (found);
}

/* ==========================================================================
 * Test Cases - LrgScene
 * ========================================================================== */

static void
test_scene_new (void)
{
    g_autoptr(LrgScene) scene = NULL;

    scene = lrg_scene_new ("my-scene");

    g_assert_nonnull (scene);
    g_assert_true (LRG_IS_SCENE (scene));
    g_assert_cmpstr (lrg_scene_get_name (scene), ==, "my-scene");
}

static void
test_scene_metadata (SceneFixture *fixture,
                     gconstpointer user_data)
{
    g_autoptr(GDateTime) now = NULL;
    GDateTime           *retrieved;

    now = g_date_time_new_now_utc ();

    lrg_scene_set_exported_from (fixture->scene, "Blender 5.0");
    lrg_scene_set_export_date (fixture->scene, now);

    g_assert_cmpstr (lrg_scene_get_exported_from (fixture->scene), ==, "Blender 5.0");

    retrieved = lrg_scene_get_export_date (fixture->scene);
    g_assert_nonnull (retrieved);
}

static void
test_scene_add_entity (SceneFixture *fixture,
                       gconstpointer user_data)
{
    LrgSceneEntity *retrieved;

    lrg_scene_add_entity (fixture->scene, fixture->entity);

    retrieved = lrg_scene_get_entity (fixture->scene, "test-entity");

    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == fixture->entity);
}

static void
test_scene_get_entities (SceneFixture *fixture,
                         gconstpointer user_data)
{
    GList *names;
    g_autoptr(LrgSceneEntity) entity2 = NULL;

    entity2 = lrg_scene_entity_new ("entity2");

    lrg_scene_add_entity (fixture->scene, fixture->entity);
    lrg_scene_add_entity (fixture->scene, entity2);

    names = lrg_scene_get_entity_names (fixture->scene);

    g_assert_nonnull (names);
    g_assert_cmpuint (g_list_length (names), ==, 2);

    g_list_free (names);
}

static guint foreach_entity_count = 0;

static void
count_entity_foreach (const gchar    *name,
                      LrgSceneEntity *entity,
                      gpointer        user_data)
{
    foreach_entity_count++;
}

static void
test_scene_foreach_entity (SceneFixture *fixture,
                           gconstpointer user_data)
{
    g_autoptr(LrgSceneEntity) entity2 = NULL;

    foreach_entity_count = 0;

    entity2 = lrg_scene_entity_new ("entity2");

    lrg_scene_add_entity (fixture->scene, fixture->entity);
    lrg_scene_add_entity (fixture->scene, entity2);

    lrg_scene_foreach_entity (fixture->scene, count_entity_foreach, NULL);

    g_assert_cmpuint (foreach_entity_count, ==, 2);
}

/* ==========================================================================
 * Test Cases - LrgSceneSerializerYaml
 * ========================================================================== */

static void
test_serializer_yaml_new (void)
{
    g_autoptr(LrgSceneSerializerYaml) serializer = NULL;

    serializer = lrg_scene_serializer_yaml_new ();

    g_assert_nonnull (serializer);
    g_assert_true (LRG_IS_SCENE_SERIALIZER_YAML (serializer));
    g_assert_true (LRG_IS_SCENE_SERIALIZER (serializer));
}

static void
test_serializer_yaml_roundtrip (void)
{
    g_autoptr(LrgSceneSerializerYaml) serializer = NULL;
    g_autoptr(LrgScene)               scene = NULL;
    g_autoptr(LrgScene)               loaded = NULL;
    g_autoptr(LrgSceneEntity)         entity = NULL;
    g_autoptr(LrgSceneObject)         object = NULL;
    g_autoptr(LrgMaterial3D)          material = NULL;
    g_autoptr(GError)                 error = NULL;
    g_autofree gchar                 *yaml = NULL;
    gsize                             length;
    LrgSceneEntity                   *loaded_entity;
    LrgSceneObject                   *loaded_object;
    LrgMaterial3D                    *loaded_material;
    GrlVector3                       *loc;
    gfloat                            r, g, b, a;

    /* Create scene with entity and object */
    scene = lrg_scene_new ("roundtrip-test");
    lrg_scene_set_exported_from (scene, "Test Suite");

    entity = lrg_scene_entity_new ("test-entity");
    lrg_scene_entity_set_location_xyz (entity, 1.0f, 2.0f, 3.0f);

    object = lrg_scene_object_new ("test-part", LRG_PRIMITIVE_CYLINDER);
    lrg_scene_object_set_location_xyz (object, 0.5f, 0.5f, 0.5f);
    lrg_scene_object_set_param_float (object, "radius", 1.5f);
    lrg_scene_object_set_param_float (object, "depth", 3.0f);
    lrg_scene_object_set_param_int (object, "vertices", 32);

    material = lrg_material3d_new ();
    lrg_material3d_set_color (material, 0.8f, 0.2f, 0.1f, 1.0f);
    lrg_material3d_set_roughness (material, 0.5f);
    lrg_material3d_set_metallic (material, 0.0f);

    lrg_scene_object_set_material (object, material);
    lrg_scene_entity_add_object (entity, object);
    lrg_scene_add_entity (scene, entity);

    /* Serialize to YAML */
    serializer = lrg_scene_serializer_yaml_new ();
    yaml = lrg_scene_serializer_save_to_data (LRG_SCENE_SERIALIZER (serializer),
                                              scene, &length);

    g_assert_nonnull (yaml);
    g_assert_cmpuint (length, >, 0);

    /* Load from YAML */
    loaded = lrg_scene_serializer_load_from_data (LRG_SCENE_SERIALIZER (serializer),
                                                  yaml, length, &error);

    g_assert_no_error (error);
    g_assert_nonnull (loaded);

    /* Verify scene */
    g_assert_cmpstr (lrg_scene_get_name (loaded), ==, "roundtrip-test");
    g_assert_cmpstr (lrg_scene_get_exported_from (loaded), ==, "Test Suite");

    /* Verify entity */
    loaded_entity = lrg_scene_get_entity (loaded, "test-entity");
    g_assert_nonnull (loaded_entity);

    /* Verify object */
    loaded_object = lrg_scene_entity_find_object (loaded_entity, "test-part");
    g_assert_nonnull (loaded_object);
    g_assert_cmpint (lrg_scene_object_get_primitive (loaded_object),
                     ==, LRG_PRIMITIVE_CYLINDER);

    /* Verify object transform (entity transforms not serialized in current format) */
    loc = lrg_scene_object_get_location (loaded_object);
    g_assert_cmpfloat_with_epsilon (loc->x, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->y, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (loc->z, 0.5f, 0.001f);

    /* Verify params */
    g_assert_cmpfloat_with_epsilon (
        lrg_scene_object_get_param_float (loaded_object, "radius", 0.0f),
        1.5f, 0.001f);

    /* Verify material */
    loaded_material = lrg_scene_object_get_material (loaded_object);
    g_assert_nonnull (loaded_material);
    lrg_material3d_get_color (loaded_material, &r, &g, &b, &a);
    g_assert_cmpfloat_with_epsilon (r, 0.8f, 0.001f);
    g_assert_cmpfloat_with_epsilon (g, 0.2f, 0.001f);
    g_assert_cmpfloat_with_epsilon (b, 0.1f, 0.001f);
}

static void
test_serializer_yaml_load_invalid (void)
{
    g_autoptr(LrgSceneSerializerYaml) serializer = NULL;
    g_autoptr(LrgScene)               loaded = NULL;
    g_autoptr(GError)                 error = NULL;
    const gchar                      *invalid_yaml = "not: valid: yaml: [";

    serializer = lrg_scene_serializer_yaml_new ();

    loaded = lrg_scene_serializer_load_from_data (LRG_SCENE_SERIALIZER (serializer),
                                                  invalid_yaml, -1, &error);

    /* Invalid YAML should fail and return an error */
    g_assert_null (loaded);
    g_assert_nonnull (error);
}

/* ==========================================================================
 * Test Cases - Primitive Types Enum
 * ========================================================================== */

static void
test_primitive_type_enum (void)
{
    /* Test that all primitive types are defined */
    g_assert_cmpint (LRG_PRIMITIVE_PLANE, ==, 0);
    g_assert_cmpint (LRG_PRIMITIVE_CUBE, ==, 1);
    g_assert_cmpint (LRG_PRIMITIVE_CIRCLE, ==, 2);
    g_assert_cmpint (LRG_PRIMITIVE_UV_SPHERE, ==, 3);
    g_assert_cmpint (LRG_PRIMITIVE_ICO_SPHERE, ==, 4);
    g_assert_cmpint (LRG_PRIMITIVE_CYLINDER, ==, 5);
    g_assert_cmpint (LRG_PRIMITIVE_CONE, ==, 6);
    g_assert_cmpint (LRG_PRIMITIVE_TORUS, ==, 7);
    g_assert_cmpint (LRG_PRIMITIVE_GRID, ==, 8);
}

static void
test_circle_fill_type_enum (void)
{
    g_assert_cmpint (LRG_CIRCLE_FILL_NOTHING, ==, 0);
    g_assert_cmpint (LRG_CIRCLE_FILL_NGON, ==, 1);
    g_assert_cmpint (LRG_CIRCLE_FILL_TRIFAN, ==, 2);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgMaterial3D */
    g_test_add_func ("/scene/material3d/new", test_material3d_new);

    g_test_add ("/scene/material3d/color",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_material3d_color,
                scene_fixture_tear_down);

    g_test_add ("/scene/material3d/roughness",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_material3d_roughness,
                scene_fixture_tear_down);

    g_test_add ("/scene/material3d/metallic",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_material3d_metallic,
                scene_fixture_tear_down);

    g_test_add ("/scene/material3d/emission",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_material3d_emission,
                scene_fixture_tear_down);

    /* LrgSceneObject */
    g_test_add_func ("/scene/object/new", test_scene_object_new);

    g_test_add ("/scene/object/transform",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_object_transform,
                scene_fixture_tear_down);

    g_test_add ("/scene/object/material",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_object_material,
                scene_fixture_tear_down);

    g_test_add ("/scene/object/params/float",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_object_params_float,
                scene_fixture_tear_down);

    g_test_add ("/scene/object/params/int",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_object_params_int,
                scene_fixture_tear_down);

    g_test_add ("/scene/object/params/bool",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_object_params_bool,
                scene_fixture_tear_down);

    /* LrgSceneEntity */
    g_test_add_func ("/scene/entity/new", test_scene_entity_new);

    g_test_add ("/scene/entity/transform",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_entity_transform,
                scene_fixture_tear_down);

    g_test_add ("/scene/entity/add-object",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_entity_add_object,
                scene_fixture_tear_down);

    g_test_add ("/scene/entity/find-object",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_entity_find_object,
                scene_fixture_tear_down);

    g_test_add ("/scene/entity/find-object-not-found",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_entity_find_object_not_found,
                scene_fixture_tear_down);

    /* LrgScene */
    g_test_add_func ("/scene/scene/new", test_scene_new);

    g_test_add ("/scene/scene/metadata",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_metadata,
                scene_fixture_tear_down);

    g_test_add ("/scene/scene/add-entity",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_add_entity,
                scene_fixture_tear_down);

    g_test_add ("/scene/scene/get-entities",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_get_entities,
                scene_fixture_tear_down);

    g_test_add ("/scene/scene/foreach-entity",
                SceneFixture, NULL,
                scene_fixture_set_up,
                test_scene_foreach_entity,
                scene_fixture_tear_down);

    /* LrgSceneSerializerYaml */
    g_test_add_func ("/scene/serializer-yaml/new", test_serializer_yaml_new);
    g_test_add_func ("/scene/serializer-yaml/roundtrip", test_serializer_yaml_roundtrip);
    g_test_add_func ("/scene/serializer-yaml/load-invalid", test_serializer_yaml_load_invalid);

    /* Enums */
    g_test_add_func ("/scene/enums/primitive-type", test_primitive_type_enum);
    g_test_add_func ("/scene/enums/circle-fill-type", test_circle_fill_type_enum);

    return g_test_run ();
}
