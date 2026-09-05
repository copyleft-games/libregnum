/* test-data-loader.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgDataLoader.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libregnum.h>
#include <yaml-glib.h>

/* ==========================================================================
 * Mock Object for Testing
 *
 * A simple GObject that implements YamlSerializable for YAML loading.
 * ========================================================================== */

#define TEST_TYPE_ENTITY (test_entity_get_type ())
G_DECLARE_FINAL_TYPE (TestEntity, test_entity, TEST, ENTITY, GObject)

struct _TestEntity
{
    GObject parent_instance;

    gchar   *name;
    gint     health;
    gdouble  speed;
};

enum
{
    PROP_ENTITY_0,
    PROP_ENTITY_NAME,
    PROP_ENTITY_HEALTH,
    PROP_ENTITY_SPEED,
    N_ENTITY_PROPS
};

static GParamSpec *entity_properties[N_ENTITY_PROPS];

G_DEFINE_TYPE (TestEntity, test_entity, G_TYPE_OBJECT)

static void
test_entity_finalize (GObject *object)
{
    TestEntity *self = TEST_ENTITY (object);

    g_free (self->name);

    G_OBJECT_CLASS (test_entity_parent_class)->finalize (object);
}

static void
test_entity_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    TestEntity *self = TEST_ENTITY (object);

    switch (prop_id)
    {
    case PROP_ENTITY_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_ENTITY_HEALTH:
        g_value_set_int (value, self->health);
        break;
    case PROP_ENTITY_SPEED:
        g_value_set_double (value, self->speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_entity_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    TestEntity *self = TEST_ENTITY (object);

    switch (prop_id)
    {
    case PROP_ENTITY_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_ENTITY_HEALTH:
        self->health = g_value_get_int (value);
        break;
    case PROP_ENTITY_SPEED:
        self->speed = g_value_get_double (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_entity_class_init (TestEntityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_entity_finalize;
    object_class->get_property = test_entity_get_property;
    object_class->set_property = test_entity_set_property;

    entity_properties[PROP_ENTITY_NAME] =
        g_param_spec_string ("name", "Name", "Entity name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    entity_properties[PROP_ENTITY_HEALTH] =
        g_param_spec_int ("health", "Health", "Entity health",
                          0, G_MAXINT, 100,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    entity_properties[PROP_ENTITY_SPEED] =
        g_param_spec_double ("speed", "Speed", "Entity speed",
                             0.0, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_ENTITY_PROPS,
                                       entity_properties);
}

static void
test_entity_init (TestEntity *self)
{
    self->name = NULL;
    self->health = 100;
    self->speed = 1.0;
}

#define TEST_TYPE_VALIDATED (test_validated_get_type ())
G_DECLARE_FINAL_TYPE (TestValidated, test_validated, TEST, VALIDATED, GObject)

struct _TestValidated
{
    GObject parent_instance;
    GBytes *payload;
    gint level;
    guint64 total;
};

static guint validated_decode_calls;
static void test_validated_serializable_init (YamlSerializableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestValidated, test_validated, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (YAML_TYPE_SERIALIZABLE,
                                               test_validated_serializable_init))

static void
test_validated_finalize (GObject *object)
{
    TestValidated *self = TEST_VALIDATED (object);

    g_clear_pointer (&self->payload, g_bytes_unref);
    G_OBJECT_CLASS (test_validated_parent_class)->finalize (object);
}

static void
test_validated_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    TestValidated *self = TEST_VALIDATED (object);

    switch (prop_id)
    {
    case 1: g_value_set_boxed (value, self->payload); break;
    case 2: g_value_set_int (value, self->level); break;
    case 3: g_value_set_uint64 (value, self->total); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_validated_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    TestValidated *self = TEST_VALIDATED (object);

    switch (prop_id)
    {
    case 1:
        g_clear_pointer (&self->payload, g_bytes_unref);
        self->payload = g_value_dup_boxed (value);
        break;
    case 2: self->level = g_value_get_int (value); break;
    case 3: self->total = g_value_get_uint64 (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
test_validated_deserialize (YamlSerializable *serializable,
                            const gchar      *name,
                            GValue           *value,
                            GParamSpec       *pspec,
                            YamlNode         *node)
{
    validated_decode_calls++;
    if (g_str_equal (name, "payload"))
    {
        const gchar *text = yaml_node_get_scalar (node);

        if (text == NULL || g_str_equal (text, "reject"))
            return FALSE;
        g_value_take_boxed (value, g_bytes_new (text, strlen (text)));
        return TRUE;
    }
    if (g_str_equal (name, "level"))
    {
        gint level = yaml_node_get_int (node);

        if (level == 13)
            return FALSE;
        g_value_set_int (value, level == 7 ? 200 : level);
        return TRUE;
    }
    return yaml_serializable_default_deserialize_property (serializable, name, value, pspec, node);
}

static void
test_validated_serializable_init (YamlSerializableInterface *iface)
{
    iface->deserialize_property = test_validated_deserialize;
}

static void
test_validated_class_init (TestValidatedClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_validated_finalize;
    object_class->get_property = test_validated_get_property;
    object_class->set_property = test_validated_set_property;
    g_object_class_install_property (object_class, 1,
        g_param_spec_boxed ("payload", "Payload", "Custom payload", G_TYPE_BYTES,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (object_class, 2,
        g_param_spec_int ("level", "Level", "Custom level", 0, 100, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (object_class, 3,
        g_param_spec_uint64 ("total", "Total", "Unsigned total", 0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_validated_init (TestValidated *self)
{
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgDataLoader *loader;
    LrgRegistry   *registry;
    gchar         *test_dir;
} LoaderFixture;

static void
loader_fixture_set_up (LoaderFixture *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    fixture->registry = lrg_registry_new ();
    fixture->loader = lrg_data_loader_new ();

    /* Register test type */
    lrg_registry_register (fixture->registry, "entity", TEST_TYPE_ENTITY);
    lrg_registry_register (fixture->registry, "validated", TEST_TYPE_VALIDATED);

    /* Connect loader to registry */
    lrg_data_loader_set_registry (fixture->loader, fixture->registry);

    /* Create temporary directory for test files */
    fixture->test_dir = g_dir_make_tmp ("libregnum-test-XXXXXX", &error);
    g_assert_no_error (error);
}

static void
loader_fixture_tear_down (LoaderFixture *fixture,
                          gconstpointer  user_data)
{
    /* Clean up test directory */
    if (fixture->test_dir != NULL)
    {
        g_autoptr(GDir) dir = g_dir_open (fixture->test_dir, 0, NULL);

        if (dir != NULL)
        {
            const gchar *name;

            while ((name = g_dir_read_name (dir)) != NULL)
            {
                g_autofree gchar *path = g_build_filename (fixture->test_dir,
                                                           name, NULL);
                g_remove (path);
            }
        }

        g_rmdir (fixture->test_dir);
        g_free (fixture->test_dir);
    }

    g_clear_object (&fixture->loader);
    g_clear_object (&fixture->registry);
}

/* Helper to write a test YAML file */
static gchar *
write_test_file (LoaderFixture *fixture,
                 const gchar   *filename,
                 const gchar   *content)
{
    gchar            *path;
    g_autoptr(GError) error = NULL;

    path = g_build_filename (fixture->test_dir, filename, NULL);
    g_file_set_contents (path, content, -1, &error);
    g_assert_no_error (error);

    return path;
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_data_loader_new (void)
{
    g_autoptr(LrgDataLoader) loader = NULL;

    loader = lrg_data_loader_new ();

    g_assert_nonnull (loader);
    g_assert_true (LRG_IS_DATA_LOADER (loader));
}

static void
test_data_loader_default_type_field (void)
{
    g_autoptr(LrgDataLoader) loader = NULL;
    const gchar             *field_name;

    loader = lrg_data_loader_new ();
    field_name = lrg_data_loader_get_type_field_name (loader);

    g_assert_cmpstr (field_name, ==, "type");
}

static void
test_data_loader_default_extensions (void)
{
    g_autoptr(LrgDataLoader) loader = NULL;
    const gchar * const     *extensions;

    loader = lrg_data_loader_new ();
    extensions = lrg_data_loader_get_file_extensions (loader);

    g_assert_nonnull (extensions);
    g_assert_cmpstr (extensions[0], ==, ".yaml");
    g_assert_cmpstr (extensions[1], ==, ".yml");
    g_assert_null (extensions[2]);
}

/* ==========================================================================
 * Test Cases - Registry
 * ========================================================================== */

static void
test_data_loader_set_registry (LoaderFixture *fixture,
                               gconstpointer  user_data)
{
    LrgRegistry *retrieved;

    retrieved = lrg_data_loader_get_registry (fixture->loader);

    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == fixture->registry);
}

/* ==========================================================================
 * Test Cases - Load from Data
 * ========================================================================== */

static void
test_data_loader_load_data (LoaderFixture *fixture,
                            gconstpointer  user_data)
{
    const gchar       *yaml_data = "type: entity\nname: \"Hero\"\nhealth: 100\nspeed: 1.5\n";
    g_autoptr(GError)  error = NULL;
    g_autoptr(GObject) object = NULL;
    TestEntity        *entity;

    object = lrg_data_loader_load_data (fixture->loader, yaml_data, -1, &error);

    g_assert_no_error (error);
    g_assert_nonnull (object);
    g_assert_true (TEST_IS_ENTITY (object));

    entity = TEST_ENTITY (object);
    g_assert_cmpstr (entity->name, ==, "Hero");
    g_assert_cmpint (entity->health, ==, 100);
    g_assert_cmpfloat (entity->speed, ==, 1.5);
}

static void
test_data_loader_load_data_missing_type (LoaderFixture *fixture,
                                         gconstpointer  user_data)
{
    const gchar      *yaml_data = "name: \"NoType\"\nhealth: 50\n";
    g_autoptr(GError) error = NULL;
    GObject          *object;

    object = lrg_data_loader_load_data (fixture->loader, yaml_data, -1, &error);

    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE);
}

static void
test_data_loader_load_data_unknown_type (LoaderFixture *fixture,
                                         gconstpointer  user_data)
{
    const gchar      *yaml_data = "type: nonexistent\nname: \"Test\"\n";
    g_autoptr(GError) error = NULL;
    GObject          *object;

    object = lrg_data_loader_load_data (fixture->loader, yaml_data, -1, &error);

    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE);
}

/* ==========================================================================
 * Test Cases - Load from File
 * ========================================================================== */

static void
test_data_loader_load_file (LoaderFixture *fixture,
                            gconstpointer  user_data)
{
    const gchar       *yaml_content = "type: entity\nname: \"FileHero\"\nhealth: 200\n";
    g_autofree gchar  *path = NULL;
    g_autoptr(GError)  error = NULL;
    g_autoptr(GObject) object = NULL;
    TestEntity        *entity;

    path = write_test_file (fixture, "test.yaml", yaml_content);

    object = lrg_data_loader_load_file (fixture->loader, path, &error);

    g_assert_no_error (error);
    g_assert_nonnull (object);
    g_assert_true (TEST_IS_ENTITY (object));

    entity = TEST_ENTITY (object);
    g_assert_cmpstr (entity->name, ==, "FileHero");
    g_assert_cmpint (entity->health, ==, 200);
}

static void
test_data_loader_load_file_not_found (LoaderFixture *fixture,
                                      gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    GObject          *object;

    object = lrg_data_loader_load_file (fixture->loader,
                                        "/nonexistent/path/file.yaml",
                                        &error);

    g_assert_null (object);
    g_assert_nonnull (error);
}

/* ==========================================================================
 * Test Cases - Load Typed
 * ========================================================================== */

static void
test_data_loader_load_typed (LoaderFixture *fixture,
                             gconstpointer  user_data)
{
    /* YAML without type field - type is specified in code */
    const gchar       *yaml_content = "name: \"TypedHero\"\nhealth: 150\nspeed: 2.0\n";
    g_autofree gchar  *path = NULL;
    g_autoptr(GError)  error = NULL;
    g_autoptr(GObject) object = NULL;
    TestEntity        *entity;

    path = write_test_file (fixture, "typed.yaml", yaml_content);

    object = lrg_data_loader_load_typed (fixture->loader,
                                         TEST_TYPE_ENTITY,
                                         path,
                                         &error);

    g_assert_no_error (error);
    g_assert_nonnull (object);
    g_assert_true (TEST_IS_ENTITY (object));

    entity = TEST_ENTITY (object);
    g_assert_cmpstr (entity->name, ==, "TypedHero");
    g_assert_cmpint (entity->health, ==, 150);
    g_assert_cmpfloat (entity->speed, ==, 2.0);
}

/* ==========================================================================
 * Test Cases - Configuration
 * ========================================================================== */

static void
test_data_loader_set_type_field_name (LoaderFixture *fixture,
                                      gconstpointer  user_data)
{
    const gchar       *yaml_data = "kind: entity\nname: \"CustomField\"\n";
    g_autoptr(GError)  error = NULL;
    g_autoptr(GObject) object = NULL;
    TestEntity        *entity;

    /* Change type field name */
    lrg_data_loader_set_type_field_name (fixture->loader, "kind");
    g_assert_cmpstr (lrg_data_loader_get_type_field_name (fixture->loader),
                     ==, "kind");

    object = lrg_data_loader_load_data (fixture->loader, yaml_data, -1, &error);

    g_assert_no_error (error);
    g_assert_nonnull (object);

    entity = TEST_ENTITY (object);
    g_assert_cmpstr (entity->name, ==, "CustomField");
}

static void
test_data_loader_set_extensions (LoaderFixture *fixture,
                                 gconstpointer  user_data)
{
    const gchar         *new_extensions[] = { ".lrg", ".data", NULL };
    const gchar * const *retrieved;

    lrg_data_loader_set_file_extensions (fixture->loader, new_extensions);
    retrieved = lrg_data_loader_get_file_extensions (fixture->loader);

    g_assert_cmpstr (retrieved[0], ==, ".lrg");
    g_assert_cmpstr (retrieved[1], ==, ".data");
    g_assert_null (retrieved[2]);
}

/* ==========================================================================
 * Test Cases - Load Directory
 * ========================================================================== */

static void
test_data_loader_load_directory (LoaderFixture *fixture,
                                 gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    GList            *objects;
    guint             count;

    /* Create multiple test files.  write_test_file returns the built path
     * (transfer full); we only need the file on disk, so free the path. */
    g_free (write_test_file (fixture, "entity1.yaml",
                             "type: entity\nname: \"Entity1\"\nhealth: 100\n"));
    g_free (write_test_file (fixture, "entity2.yaml",
                             "type: entity\nname: \"Entity2\"\nhealth: 200\n"));
    g_free (write_test_file (fixture, "entity3.yml",
                             "type: entity\nname: \"Entity3\"\nhealth: 300\n"));
    /* Non-YAML file should be ignored */
    g_free (write_test_file (fixture, "readme.txt", "This should be ignored"));

    objects = lrg_data_loader_load_directory (fixture->loader,
                                              fixture->test_dir,
                                              FALSE,
                                              &error);

    g_assert_no_error (error);
    g_assert_nonnull (objects);

    count = g_list_length (objects);
    g_assert_cmpuint (count, ==, 3);

    /* Verify all objects are entities */
    {
        GList *l;
        for (l = objects; l != NULL; l = l->next)
        {
            g_assert_true (TEST_IS_ENTITY (l->data));
        }
    }

    g_list_free_full (objects, g_object_unref);
}

static void
test_data_loader_load_files (LoaderFixture *fixture,
                             gconstpointer  user_data)
{
    g_autofree gchar  *path1 = NULL;
    g_autofree gchar  *path2 = NULL;
    const gchar       *paths[3];
    g_autoptr(GError)  error = NULL;
    GList             *objects;

    path1 = write_test_file (fixture, "first.yaml",
                             "type: entity\nname: \"First\"\n");
    path2 = write_test_file (fixture, "second.yaml",
                             "type: entity\nname: \"Second\"\n");

    paths[0] = path1;
    paths[1] = path2;
    paths[2] = NULL;

    objects = lrg_data_loader_load_files (fixture->loader, paths, &error);

    g_assert_no_error (error);
    g_assert_nonnull (objects);
    g_assert_cmpuint (g_list_length (objects), ==, 2);

    g_list_free_full (objects, g_object_unref);
}

/* ==========================================================================
 * Test Cases - No Registry
 * ========================================================================== */

static void
test_data_loader_no_registry (void)
{
    g_autoptr(LrgDataLoader) loader = NULL;
    g_autoptr(GError)        error = NULL;
    const gchar             *yaml_data = "type: entity\nname: \"Test\"\n";
    GObject                 *object;

    loader = lrg_data_loader_new ();
    /* Don't set registry */

    object = lrg_data_loader_load_data (loader, yaml_data, -1, &error);

    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

static void
test_data_loader_validated_properties (LoaderFixture *fixture,
                                       gconstpointer  user_data)
{
    const gchar *invalid[] = {
        "type: entity\nhealth: not-a-number\n",
        "type: entity\nhealth: -1\n",
        "type: entity\nhealth: 4294967296\n",
        "type: entity\nspeed: NaN\n",
        "type: entity\nspeed: 1.0junk\n",
        "type: entity\nname: {mapping: invalid}\n",
        "type: entity\nunknown-property: value\n"
    };
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;
    g_autofree gchar *path = NULL;
    guint i;

    path = write_test_file (fixture, "validated.yaml", "type: entity\nhealth: 125\nspeed: 2.5\n");
    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_no_error (error);
    g_assert_true (TEST_IS_ENTITY (object));
    g_assert_cmpint (TEST_ENTITY (object)->health, ==, 125);
    g_assert_cmpfloat (TEST_ENTITY (object)->speed, ==, 2.5);
    g_clear_object (&object);
    for (i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
        g_assert_true (g_file_set_contents (path, invalid[i], -1, &error));
        object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
        g_assert_null (object);
        g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
        g_clear_error (&error);
    }
    g_assert_true (g_file_set_contents (path, "type: entity\nhealth: [\n", -1, &error));
    g_assert_null (lrg_data_loader_load_file_validated (fixture->loader, path, NULL));

    /* Existing permissive APIs remain compatible. */
    object = lrg_data_loader_load_data (fixture->loader,
                                      "type: entity\nextra-field: ignored\n", -1, &error);
    g_assert_no_error (error);
    g_assert_nonnull (object);
}

static void
test_data_loader_invalid_without_error (LoaderFixture *fixture,
                                        gconstpointer  user_data)
{
    const gchar *invalid = "type: entity\nhealth: [\n";
    g_autofree gchar *path = write_test_file (fixture, "invalid.yaml", invalid);
    g_autoptr(GFile) file = g_file_new_for_path (path);

    g_assert_null (lrg_data_loader_load_data (fixture->loader, invalid, -1, NULL));
    g_assert_null (lrg_data_loader_load_file (fixture->loader, path, NULL));
    g_assert_null (lrg_data_loader_load_gfile (fixture->loader, file, NULL, NULL));
    g_assert_null (lrg_data_loader_load_typed (fixture->loader, TEST_TYPE_ENTITY, path, NULL));
}

static void
test_data_loader_validated_null (LoaderFixture *fixture,
                                 gconstpointer  user_data)
{
    g_autofree gchar *path = write_test_file (fixture, "null.yaml", "type: entity\nhealth: null\n");
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;

    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
}

static void
test_data_loader_custom_once (LoaderFixture *fixture,
                              gconstpointer  user_data)
{
    g_autofree gchar *path = write_test_file (fixture, "custom.yaml", "type: validated\npayload: hello\n");
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;

    validated_decode_calls = 0;
    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (object);
    g_assert_cmpuint (validated_decode_calls, ==, 1);
    g_assert_cmpmem (g_bytes_get_data (TEST_VALIDATED (object)->payload, NULL),
                     g_bytes_get_size (TEST_VALIDATED (object)->payload), "hello", 5);
}

static void
test_data_loader_custom_reject (LoaderFixture *fixture,
                                gconstpointer  user_data)
{
    g_autofree gchar *path = write_test_file (fixture, "custom.yaml", "type: validated\nlevel: 13\n");
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;

    validated_decode_calls = 0;
    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
    g_assert_cmpuint (validated_decode_calls, ==, 1);
}

static void
test_data_loader_custom_range (LoaderFixture *fixture,
                               gconstpointer  user_data)
{
    g_autofree gchar *path = write_test_file (fixture, "custom.yaml", "type: validated\nlevel: 7\n");
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;

    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
}

static void
test_data_loader_uint64_boundaries (LoaderFixture *fixture,
                                    gconstpointer  user_data)
{
    g_autofree gchar *path = write_test_file (fixture, "unsigned.yaml",
        "type: validated\ntotal: 9223372036854775807\n");
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) object = NULL;

    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (TEST_VALIDATED (object)->total, ==, G_MAXINT64);
    g_clear_object (&object);
    g_assert_true (g_file_set_contents (path,
        "type: validated\ntotal: 9223372036854775808\n", -1, &error));
    object = lrg_data_loader_load_file_validated (fixture->loader, path, &error);
    g_assert_null (object);
    g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
}

static void
test_data_loader_custom_retention (LoaderFixture *fixture,
                                   gconstpointer  user_data)
{
    const gchar *invalid[] = {
        "type: validated\npayload: reject\n",
        "type: validated\nlevel: 13\n",
        "type: validated\nlevel: 7\n",
        "type: validated\nlevel: null\n"
    };
    g_autofree gchar *path = write_test_file (fixture, "retained.yaml",
        "type: validated\npayload: original\nlevel: 5\n");
    g_autoptr(LrgAssetManager) manager = lrg_asset_manager_new ();
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) original = NULL;
    GObject *replacement;
    guint i;

    lrg_asset_manager_set_data_loader (manager, fixture->loader);
    original = g_object_ref (lrg_asset_manager_load_object (manager, path, &error));
    g_assert_no_error (error);
    for (i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
        g_assert_true (g_file_set_contents (path, invalid[i], -1, &error));
        validated_decode_calls = 0;
        g_assert_false (lrg_asset_manager_reload_object (manager, path, &error));
        g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
        g_clear_error (&error);
        g_assert_cmpuint (validated_decode_calls, <=, 1);
        g_assert_true (lrg_asset_manager_load_object (manager, path, NULL) == original);
        g_assert_cmpint (TEST_VALIDATED (original)->level, ==, 5);
    }
    g_assert_true (g_file_set_contents (path,
        "type: validated\npayload: replacement\nlevel: 6\n", -1, &error));
    validated_decode_calls = 0;
    g_assert_true (lrg_asset_manager_reload_object (manager, path, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (validated_decode_calls, ==, 2);
    replacement = lrg_asset_manager_load_object (manager, path, NULL);
    g_assert_true (replacement != original);
    g_assert_cmpint (TEST_VALIDATED (replacement)->level, ==, 6);
    g_assert_cmpint (TEST_VALIDATED (original)->level, ==, 5);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/data-loader/new", test_data_loader_new);
    g_test_add_func ("/data-loader/default-type-field",
                     test_data_loader_default_type_field);
    g_test_add_func ("/data-loader/default-extensions",
                     test_data_loader_default_extensions);

    /* Registry */
    g_test_add ("/data-loader/registry/set",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_set_registry,
                loader_fixture_tear_down);

    /* Load from data */
    g_test_add ("/data-loader/load-data/basic",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_data,
                loader_fixture_tear_down);

    g_test_add ("/data-loader/load-data/missing-type",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_data_missing_type,
                loader_fixture_tear_down);

    g_test_add ("/data-loader/load-data/unknown-type",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_data_unknown_type,
                loader_fixture_tear_down);

    /* Load from file */
    g_test_add ("/data-loader/load-file/basic",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_file,
                loader_fixture_tear_down);

    g_test_add ("/data-loader/load-file/not-found",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_file_not_found,
                loader_fixture_tear_down);

    /* Load typed */
    g_test_add ("/data-loader/load-typed/basic",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_typed,
                loader_fixture_tear_down);

    /* Configuration */
    g_test_add ("/data-loader/config/type-field-name",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_set_type_field_name,
                loader_fixture_tear_down);

    g_test_add ("/data-loader/config/extensions",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_set_extensions,
                loader_fixture_tear_down);

    /* Load directory */
    g_test_add ("/data-loader/load-directory/basic",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_directory,
                loader_fixture_tear_down);

    g_test_add ("/data-loader/load-files/basic",
                LoaderFixture, NULL,
                loader_fixture_set_up,
                test_data_loader_load_files,
                loader_fixture_tear_down);

    /* No registry */
    g_test_add_func ("/data-loader/no-registry", test_data_loader_no_registry);

    g_test_add ("/data-loader/validated/properties", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_validated_properties, loader_fixture_tear_down);

    g_test_add ("/data-loader/invalid-without-error", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_invalid_without_error, loader_fixture_tear_down);

    g_test_add ("/data-loader/validated/null", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_validated_null, loader_fixture_tear_down);
    g_test_add ("/data-loader/validated/custom-once", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_custom_once, loader_fixture_tear_down);
    g_test_add ("/data-loader/validated/custom-reject", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_custom_reject, loader_fixture_tear_down);
    g_test_add ("/data-loader/validated/custom-range", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_custom_range, loader_fixture_tear_down);
    g_test_add ("/data-loader/validated/uint64-boundaries", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_uint64_boundaries, loader_fixture_tear_down);

    g_test_add ("/data-loader/validated/custom-retention", LoaderFixture, NULL,
                loader_fixture_set_up, test_data_loader_custom_retention, loader_fixture_tear_down);

    return g_test_run ();
}
