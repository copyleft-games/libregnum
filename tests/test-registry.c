/* test-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgRegistry.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Object for Testing
 *
 * A simple GObject for testing registry creation.
 * ========================================================================== */

#define TEST_TYPE_OBJECT (test_object_get_type ())
G_DECLARE_FINAL_TYPE (TestObject, test_object, TEST, OBJECT, GObject)

struct _TestObject
{
    GObject parent_instance;

    gchar  *name;
    gint    value;
};

enum
{
    PROP_TEST_0,
    PROP_TEST_NAME,
    PROP_TEST_VALUE,
    N_TEST_PROPS
};

static GParamSpec *test_properties[N_TEST_PROPS];

G_DEFINE_TYPE (TestObject, test_object, G_TYPE_OBJECT)

static void
test_object_finalize (GObject *object)
{
    TestObject *self = TEST_OBJECT (object);

    g_free (self->name);

    G_OBJECT_CLASS (test_object_parent_class)->finalize (object);
}

static void
test_object_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    TestObject *self = TEST_OBJECT (object);

    switch (prop_id)
    {
    case PROP_TEST_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_TEST_VALUE:
        g_value_set_int (value, self->value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_object_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    TestObject *self = TEST_OBJECT (object);

    switch (prop_id)
    {
    case PROP_TEST_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_TEST_VALUE:
        self->value = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_object_class_init (TestObjectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_object_finalize;
    object_class->get_property = test_object_get_property;
    object_class->set_property = test_object_set_property;

    test_properties[PROP_TEST_NAME] =
        g_param_spec_string ("name", "Name", "Object name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    test_properties[PROP_TEST_VALUE] =
        g_param_spec_int ("value", "Value", "Object value",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_TEST_PROPS, test_properties);
}

static void
test_object_init (TestObject *self)
{
    self->name = NULL;
    self->value = 0;
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgRegistry *registry;
} RegistryFixture;

static void
registry_fixture_set_up (RegistryFixture *fixture,
                         gconstpointer    user_data)
{
    fixture->registry = lrg_registry_new ();
    g_assert_nonnull (fixture->registry);
}

static void
registry_fixture_tear_down (RegistryFixture *fixture,
                            gconstpointer    user_data)
{
    g_clear_object (&fixture->registry);
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_registry_new (void)
{
    g_autoptr(LrgRegistry) registry = NULL;

    registry = lrg_registry_new ();

    g_assert_nonnull (registry);
    g_assert_true (LRG_IS_REGISTRY (registry));
    g_assert_cmpuint (lrg_registry_get_count (registry), ==, 0);
}

/* ==========================================================================
 * Test Cases - Registration
 * ========================================================================== */

static void
test_registry_register (RegistryFixture *fixture,
                        gconstpointer    user_data)
{
    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    g_assert_true (lrg_registry_is_registered (fixture->registry, "test-object"));
    g_assert_cmpuint (lrg_registry_get_count (fixture->registry), ==, 1);
}

static void
test_registry_register_multiple (RegistryFixture *fixture,
                                 gconstpointer    user_data)
{
    lrg_registry_register (fixture->registry, "object1", TEST_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "object2", G_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "object3", TEST_TYPE_OBJECT);

    g_assert_cmpuint (lrg_registry_get_count (fixture->registry), ==, 3);
    g_assert_true (lrg_registry_is_registered (fixture->registry, "object1"));
    g_assert_true (lrg_registry_is_registered (fixture->registry, "object2"));
    g_assert_true (lrg_registry_is_registered (fixture->registry, "object3"));
}

static void
test_registry_register_overwrite (RegistryFixture *fixture,
                                  gconstpointer    user_data)
{
    GType type;

    /* Register with one type */
    lrg_registry_register (fixture->registry, "test", G_TYPE_OBJECT);
    type = lrg_registry_lookup (fixture->registry, "test");
    g_assert_cmpuint (type, ==, G_TYPE_OBJECT);

    /* Overwrite with another type */
    lrg_registry_register (fixture->registry, "test", TEST_TYPE_OBJECT);
    type = lrg_registry_lookup (fixture->registry, "test");
    g_assert_cmpuint (type, ==, TEST_TYPE_OBJECT);

    /* Count should still be 1 */
    g_assert_cmpuint (lrg_registry_get_count (fixture->registry), ==, 1);
}

static void
test_registry_unregister (RegistryFixture *fixture,
                          gconstpointer    user_data)
{
    gboolean result;

    lrg_registry_register (fixture->registry, "test", TEST_TYPE_OBJECT);
    g_assert_true (lrg_registry_is_registered (fixture->registry, "test"));

    result = lrg_registry_unregister (fixture->registry, "test");
    g_assert_true (result);
    g_assert_false (lrg_registry_is_registered (fixture->registry, "test"));

    /* Unregistering again should return FALSE */
    result = lrg_registry_unregister (fixture->registry, "test");
    g_assert_false (result);
}

/* ==========================================================================
 * Test Cases - Lookup
 * ========================================================================== */

static void
test_registry_lookup (RegistryFixture *fixture,
                      gconstpointer    user_data)
{
    GType type;

    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    type = lrg_registry_lookup (fixture->registry, "test-object");
    g_assert_cmpuint (type, ==, TEST_TYPE_OBJECT);
}

static void
test_registry_lookup_not_found (RegistryFixture *fixture,
                                gconstpointer    user_data)
{
    GType type;

    type = lrg_registry_lookup (fixture->registry, "nonexistent");
    g_assert_cmpuint (type, ==, G_TYPE_INVALID);
}

static void
test_registry_lookup_name (RegistryFixture *fixture,
                           gconstpointer    user_data)
{
    const gchar *name;

    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    name = lrg_registry_lookup_name (fixture->registry, TEST_TYPE_OBJECT);
    g_assert_nonnull (name);
    g_assert_cmpstr (name, ==, "test-object");
}

static void
test_registry_lookup_name_not_found (RegistryFixture *fixture,
                                     gconstpointer    user_data)
{
    const gchar *name;

    name = lrg_registry_lookup_name (fixture->registry, TEST_TYPE_OBJECT);
    g_assert_null (name);
}

/* ==========================================================================
 * Test Cases - Object Creation
 * ========================================================================== */

static void
test_registry_create (RegistryFixture *fixture,
                      gconstpointer    user_data)
{
    g_autoptr(GObject) object = NULL;

    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    object = lrg_registry_create (fixture->registry, "test-object", NULL);
    g_assert_nonnull (object);
    g_assert_true (TEST_IS_OBJECT (object));
}

static void
test_registry_create_with_properties (RegistryFixture *fixture,
                                      gconstpointer    user_data)
{
    g_autoptr(GObject) object = NULL;
    TestObject        *test_obj;

    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    object = lrg_registry_create (fixture->registry, "test-object",
                                  "name", "test-name",
                                  "value", 42,
                                  NULL);
    g_assert_nonnull (object);
    g_assert_true (TEST_IS_OBJECT (object));

    test_obj = TEST_OBJECT (object);
    g_assert_cmpstr (test_obj->name, ==, "test-name");
    g_assert_cmpint (test_obj->value, ==, 42);
}

static void
test_registry_create_not_registered (RegistryFixture *fixture,
                                     gconstpointer    user_data)
{
    GObject *object;

    /* Expect the warning message when creating an unregistered type */
    g_test_expect_message ("Libregnum-Core",
                           G_LOG_LEVEL_WARNING,
                           "*type 'nonexistent' not registered*");

    object = lrg_registry_create (fixture->registry, "nonexistent", NULL);
    g_assert_null (object);

    /* Verify the warning was emitted */
    g_test_assert_expected_messages ();
}

static void
test_registry_create_with_properties_array (RegistryFixture *fixture,
                                            gconstpointer    user_data)
{
    g_autoptr(GObject) object = NULL;
    const gchar       *names[] = { "name", "value" };
    GValue             values[2] = { G_VALUE_INIT, G_VALUE_INIT };
    TestObject        *test_obj;

    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    g_value_init (&values[0], G_TYPE_STRING);
    g_value_set_string (&values[0], "array-test");

    g_value_init (&values[1], G_TYPE_INT);
    g_value_set_int (&values[1], 123);

    object = lrg_registry_create_with_properties (fixture->registry,
                                                  "test-object",
                                                  2, names, values);
    g_value_unset (&values[0]);
    g_value_unset (&values[1]);

    g_assert_nonnull (object);
    g_assert_true (TEST_IS_OBJECT (object));

    test_obj = TEST_OBJECT (object);
    g_assert_cmpstr (test_obj->name, ==, "array-test");
    g_assert_cmpint (test_obj->value, ==, 123);
}

/* ==========================================================================
 * Test Cases - Enumeration
 * ========================================================================== */

static void
test_registry_get_names (RegistryFixture *fixture,
                         gconstpointer    user_data)
{
    GList *names;

    lrg_registry_register (fixture->registry, "alpha", TEST_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "beta", G_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "gamma", TEST_TYPE_OBJECT);

    names = lrg_registry_get_names (fixture->registry);
    g_assert_nonnull (names);
    g_assert_cmpuint (g_list_length (names), ==, 3);

    /* Check all names are in the list */
    g_assert_nonnull (g_list_find_custom (names, "alpha", (GCompareFunc)g_strcmp0));
    g_assert_nonnull (g_list_find_custom (names, "beta", (GCompareFunc)g_strcmp0));
    g_assert_nonnull (g_list_find_custom (names, "gamma", (GCompareFunc)g_strcmp0));

    g_list_free (names);
}

static guint foreach_count = 0;

static void
count_foreach (const gchar *name,
               GType        type,
               gpointer     user_data)
{
    foreach_count++;
}

static void
test_registry_foreach (RegistryFixture *fixture,
                       gconstpointer    user_data)
{
    foreach_count = 0;

    lrg_registry_register (fixture->registry, "one", TEST_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "two", G_TYPE_OBJECT);

    lrg_registry_foreach (fixture->registry, count_foreach, NULL);

    g_assert_cmpuint (foreach_count, ==, 2);
}

/* ==========================================================================
 * Test Cases - Clear
 * ========================================================================== */

static void
test_registry_clear (RegistryFixture *fixture,
                     gconstpointer    user_data)
{
    lrg_registry_register (fixture->registry, "one", TEST_TYPE_OBJECT);
    lrg_registry_register (fixture->registry, "two", G_TYPE_OBJECT);
    g_assert_cmpuint (lrg_registry_get_count (fixture->registry), ==, 2);

    lrg_registry_clear (fixture->registry);

    g_assert_cmpuint (lrg_registry_get_count (fixture->registry), ==, 0);
    g_assert_false (lrg_registry_is_registered (fixture->registry, "one"));
    g_assert_false (lrg_registry_is_registered (fixture->registry, "two"));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/registry/new", test_registry_new);

    /* Registration */
    g_test_add ("/registry/register/basic",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_register,
                registry_fixture_tear_down);

    g_test_add ("/registry/register/multiple",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_register_multiple,
                registry_fixture_tear_down);

    g_test_add ("/registry/register/overwrite",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_register_overwrite,
                registry_fixture_tear_down);

    g_test_add ("/registry/unregister",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_unregister,
                registry_fixture_tear_down);

    /* Lookup */
    g_test_add ("/registry/lookup/basic",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_lookup,
                registry_fixture_tear_down);

    g_test_add ("/registry/lookup/not-found",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_lookup_not_found,
                registry_fixture_tear_down);

    g_test_add ("/registry/lookup-name/basic",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_lookup_name,
                registry_fixture_tear_down);

    g_test_add ("/registry/lookup-name/not-found",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_lookup_name_not_found,
                registry_fixture_tear_down);

    /* Creation */
    g_test_add ("/registry/create/basic",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_create,
                registry_fixture_tear_down);

    g_test_add ("/registry/create/with-properties",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_create_with_properties,
                registry_fixture_tear_down);

    g_test_add ("/registry/create/not-registered",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_create_not_registered,
                registry_fixture_tear_down);

    g_test_add ("/registry/create/properties-array",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_create_with_properties_array,
                registry_fixture_tear_down);

    /* Enumeration */
    g_test_add ("/registry/get-names",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_get_names,
                registry_fixture_tear_down);

    g_test_add ("/registry/foreach",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_foreach,
                registry_fixture_tear_down);

    /* Clear */
    g_test_add ("/registry/clear",
                RegistryFixture, NULL,
                registry_fixture_set_up,
                test_registry_clear,
                registry_fixture_tear_down);

    return g_test_run ();
}
