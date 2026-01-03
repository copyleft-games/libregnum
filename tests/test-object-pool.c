/* test-object-pool.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgPoolable and LrgObjectPool.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Poolable Object for Testing
 *
 * A simple GObject that implements LrgPoolable for testing.
 * ========================================================================== */

#define TEST_TYPE_POOLABLE_OBJECT (test_poolable_object_get_type ())
G_DECLARE_FINAL_TYPE (TestPoolableObject, test_poolable_object,
                      TEST, POOLABLE_OBJECT, GObject)

struct _TestPoolableObject
{
    GObject parent_instance;

    gboolean       is_active;
    LrgObjectPool *pool;

    /* Test data */
    gfloat x;
    gfloat y;
    gint   value;
    gchar *name;

    /* Track reset calls */
    gint reset_count;
};

enum
{
    PROP_POOLABLE_0,
    PROP_POOLABLE_X,
    PROP_POOLABLE_Y,
    PROP_POOLABLE_VALUE,
    PROP_POOLABLE_NAME,
    N_POOLABLE_PROPS
};

static GParamSpec *poolable_properties[N_POOLABLE_PROPS];

static void test_poolable_object_poolable_init (LrgPoolableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestPoolableObject, test_poolable_object, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_POOLABLE, test_poolable_object_poolable_init))

/* LrgPoolable implementation */

static void
test_poolable_object_reset (LrgPoolable *poolable)
{
    TestPoolableObject *self = TEST_POOLABLE_OBJECT (poolable);

    self->is_active = FALSE;
    self->x = 0.0f;
    self->y = 0.0f;
    self->value = 0;
    g_clear_pointer (&self->name, g_free);
    self->reset_count++;
}

static gboolean
test_poolable_object_is_active (LrgPoolable *poolable)
{
    return TEST_POOLABLE_OBJECT (poolable)->is_active;
}

static void
test_poolable_object_set_active (LrgPoolable *poolable,
                                  gboolean     active)
{
    TEST_POOLABLE_OBJECT (poolable)->is_active = active;
}

static LrgObjectPool *
test_poolable_object_get_pool (LrgPoolable *poolable)
{
    return TEST_POOLABLE_OBJECT (poolable)->pool;
}

static void
test_poolable_object_poolable_init (LrgPoolableInterface *iface)
{
    iface->reset = test_poolable_object_reset;
    iface->is_active = test_poolable_object_is_active;
    iface->set_active = test_poolable_object_set_active;
    iface->get_pool = test_poolable_object_get_pool;
}

/* GObject implementation */

static void
test_poolable_object_finalize (GObject *object)
{
    TestPoolableObject *self = TEST_POOLABLE_OBJECT (object);

    g_clear_pointer (&self->name, g_free);

    G_OBJECT_CLASS (test_poolable_object_parent_class)->finalize (object);
}

static void
test_poolable_object_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    TestPoolableObject *self = TEST_POOLABLE_OBJECT (object);

    switch (prop_id)
    {
    case PROP_POOLABLE_X:
        g_value_set_float (value, self->x);
        break;
    case PROP_POOLABLE_Y:
        g_value_set_float (value, self->y);
        break;
    case PROP_POOLABLE_VALUE:
        g_value_set_int (value, self->value);
        break;
    case PROP_POOLABLE_NAME:
        g_value_set_string (value, self->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_poolable_object_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    TestPoolableObject *self = TEST_POOLABLE_OBJECT (object);

    switch (prop_id)
    {
    case PROP_POOLABLE_X:
        self->x = g_value_get_float (value);
        break;
    case PROP_POOLABLE_Y:
        self->y = g_value_get_float (value);
        break;
    case PROP_POOLABLE_VALUE:
        self->value = g_value_get_int (value);
        break;
    case PROP_POOLABLE_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_poolable_object_class_init (TestPoolableObjectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_poolable_object_finalize;
    object_class->get_property = test_poolable_object_get_property;
    object_class->set_property = test_poolable_object_set_property;

    poolable_properties[PROP_POOLABLE_X] =
        g_param_spec_float ("x", "X", "X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    poolable_properties[PROP_POOLABLE_Y] =
        g_param_spec_float ("y", "Y", "Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    poolable_properties[PROP_POOLABLE_VALUE] =
        g_param_spec_int ("value", "Value", "Test value",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    poolable_properties[PROP_POOLABLE_NAME] =
        g_param_spec_string ("name", "Name", "Object name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_POOLABLE_PROPS,
                                        poolable_properties);
}

static void
test_poolable_object_init (TestPoolableObject *self)
{
    self->is_active = FALSE;
    self->pool = NULL;
    self->x = 0.0f;
    self->y = 0.0f;
    self->value = 0;
    self->name = NULL;
    self->reset_count = 0;
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgObjectPool *pool;
} PoolFixture;

static void
pool_fixture_set_up (PoolFixture   *fixture,
                     gconstpointer  user_data)
{
    fixture->pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 10,
                                          LRG_POOL_GROWTH_DOUBLE);
    g_assert_nonnull (fixture->pool);
}

static void
pool_fixture_tear_down (PoolFixture   *fixture,
                        gconstpointer  user_data)
{
    g_clear_object (&fixture->pool);
}

/* ==========================================================================
 * Test Cases - LrgPoolable Interface
 * ========================================================================== */

static void
test_poolable_interface_implemented (void)
{
    g_autoptr(TestPoolableObject) obj = NULL;

    obj = g_object_new (TEST_TYPE_POOLABLE_OBJECT, NULL);
    g_assert_nonnull (obj);
    g_assert_true (LRG_IS_POOLABLE (obj));
}

static void
test_poolable_reset (void)
{
    g_autoptr(TestPoolableObject) obj = NULL;

    obj = g_object_new (TEST_TYPE_POOLABLE_OBJECT,
                        "x", 100.0f,
                        "y", 200.0f,
                        "value", 42,
                        "name", "test",
                        NULL);

    g_assert_cmpfloat_with_epsilon (obj->x, 100.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (obj->y, 200.0f, 0.01f);
    g_assert_cmpint (obj->value, ==, 42);
    g_assert_cmpstr (obj->name, ==, "test");
    g_assert_cmpint (obj->reset_count, ==, 0);

    lrg_poolable_reset (LRG_POOLABLE (obj));

    g_assert_cmpfloat_with_epsilon (obj->x, 0.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (obj->y, 0.0f, 0.01f);
    g_assert_cmpint (obj->value, ==, 0);
    g_assert_null (obj->name);
    g_assert_cmpint (obj->reset_count, ==, 1);
}

static void
test_poolable_active_state (void)
{
    g_autoptr(TestPoolableObject) obj = NULL;

    obj = g_object_new (TEST_TYPE_POOLABLE_OBJECT, NULL);

    g_assert_false (lrg_poolable_is_active (LRG_POOLABLE (obj)));

    lrg_poolable_set_active (LRG_POOLABLE (obj), TRUE);
    g_assert_true (lrg_poolable_is_active (LRG_POOLABLE (obj)));

    lrg_poolable_set_active (LRG_POOLABLE (obj), FALSE);
    g_assert_false (lrg_poolable_is_active (LRG_POOLABLE (obj)));
}

/* ==========================================================================
 * Test Cases - LrgObjectPool Construction
 * ========================================================================== */

static void
test_pool_new (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;

    pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 10,
                                 LRG_POOL_GROWTH_DOUBLE);
    g_assert_nonnull (pool);
    g_assert_true (LRG_IS_OBJECT_POOL (pool));
}

static void
test_pool_new_with_max (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;

    pool = lrg_object_pool_new_with_max (TEST_TYPE_POOLABLE_OBJECT, 5, 20,
                                          LRG_POOL_GROWTH_LINEAR);
    g_assert_nonnull (pool);
    g_assert_cmpuint (lrg_object_pool_get_max_size (pool), ==, 20);
}

static void
test_pool_initial_state (PoolFixture   *fixture,
                         gconstpointer  user_data)
{
    g_assert_cmpuint (lrg_object_pool_get_total_size (fixture->pool), ==, 10);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 10);
    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 0);
    g_assert_true (lrg_object_pool_get_object_type (fixture->pool) ==
                   TEST_TYPE_POOLABLE_OBJECT);
}

static void
test_pool_properties (PoolFixture   *fixture,
                      gconstpointer  user_data)
{
    GType type;
    guint initial_size;
    guint max_size;
    LrgPoolGrowthPolicy policy;

    g_object_get (fixture->pool,
                  "object-type", &type,
                  "initial-size", &initial_size,
                  "max-size", &max_size,
                  "growth-policy", &policy,
                  NULL);

    g_assert_true (type == TEST_TYPE_POOLABLE_OBJECT);
    g_assert_cmpuint (initial_size, ==, 10);
    g_assert_cmpuint (max_size, ==, 0);
    g_assert_cmpint (policy, ==, LRG_POOL_GROWTH_DOUBLE);
}

/* ==========================================================================
 * Test Cases - Pool Operations
 * ========================================================================== */

static void
test_pool_acquire (PoolFixture   *fixture,
                   gconstpointer  user_data)
{
    GObject *obj;

    obj = lrg_object_pool_acquire (fixture->pool);
    g_assert_nonnull (obj);
    g_assert_true (TEST_IS_POOLABLE_OBJECT (obj));
    g_assert_true (lrg_poolable_is_active (LRG_POOLABLE (obj)));

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 1);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 9);
}

static void
test_pool_acquire_multiple (PoolFixture   *fixture,
                            gconstpointer  user_data)
{
    GObject *objects[10];
    guint i;

    for (i = 0; i < 10; i++)
    {
        objects[i] = lrg_object_pool_acquire (fixture->pool);
        g_assert_nonnull (objects[i]);
    }

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 10);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 0);
}

static void
test_pool_acquire_with_init (PoolFixture   *fixture,
                             gconstpointer  user_data)
{
    GObject *obj;
    TestPoolableObject *poolable;

    obj = lrg_object_pool_acquire_with_init (fixture->pool,
                                              "x", 10.0f,
                                              "y", 20.0f,
                                              "value", 42,
                                              NULL);
    g_assert_nonnull (obj);

    poolable = TEST_POOLABLE_OBJECT (obj);
    g_assert_cmpfloat_with_epsilon (poolable->x, 10.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (poolable->y, 20.0f, 0.01f);
    g_assert_cmpint (poolable->value, ==, 42);
}

static void
test_pool_release (PoolFixture   *fixture,
                   gconstpointer  user_data)
{
    GObject *obj;
    TestPoolableObject *poolable;

    obj = lrg_object_pool_acquire (fixture->pool);
    poolable = TEST_POOLABLE_OBJECT (obj);

    /* Set some values */
    poolable->x = 100.0f;
    poolable->y = 200.0f;
    poolable->value = 999;

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 1);
    g_assert_cmpint (poolable->reset_count, ==, 0);

    lrg_object_pool_release (fixture->pool, LRG_POOLABLE (obj));

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 0);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 10);
    g_assert_false (lrg_poolable_is_active (LRG_POOLABLE (obj)));
    g_assert_cmpint (poolable->reset_count, ==, 1);

    /* Values should be reset */
    g_assert_cmpfloat_with_epsilon (poolable->x, 0.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (poolable->y, 0.0f, 0.01f);
    g_assert_cmpint (poolable->value, ==, 0);
}

static void
test_pool_release_all_active (PoolFixture   *fixture,
                              gconstpointer  user_data)
{
    guint i;

    /* Acquire all objects */
    for (i = 0; i < 10; i++)
        lrg_object_pool_acquire (fixture->pool);

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 10);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 0);

    /* Release all */
    lrg_object_pool_release_all_active (fixture->pool);

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 0);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 10);
}

/* ==========================================================================
 * Test Cases - Pool Growth
 * ========================================================================== */

static void
test_pool_growth_double (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;
    GObject *obj;
    guint i;

    pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 5,
                                 LRG_POOL_GROWTH_DOUBLE);

    /* Exhaust initial pool */
    for (i = 0; i < 5; i++)
        lrg_object_pool_acquire (pool);

    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 5);

    /* Trigger growth */
    obj = lrg_object_pool_acquire (pool);
    g_assert_nonnull (obj);

    /* Should have doubled (5 + 5 = 10) */
    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 10);
}

static void
test_pool_growth_linear (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;
    GObject *obj;
    guint i;

    pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 5,
                                 LRG_POOL_GROWTH_LINEAR);

    /* Exhaust initial pool */
    for (i = 0; i < 5; i++)
        lrg_object_pool_acquire (pool);

    /* Trigger growth */
    obj = lrg_object_pool_acquire (pool);
    g_assert_nonnull (obj);

    /* Should have added initial_size (5 + 5 = 10) */
    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 10);
}

static void
test_pool_growth_fixed (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;
    GObject *obj;
    guint i;

    pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 5,
                                 LRG_POOL_GROWTH_FIXED);

    /* Exhaust initial pool */
    for (i = 0; i < 5; i++)
    {
        obj = lrg_object_pool_acquire (pool);
        g_assert_nonnull (obj);
    }

    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 5);

    /* Should return NULL when exhausted */
    obj = lrg_object_pool_acquire (pool);
    g_assert_null (obj);
}

static void
test_pool_max_size_respected (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;
    GObject *obj;
    guint i;

    pool = lrg_object_pool_new_with_max (TEST_TYPE_POOLABLE_OBJECT, 5, 10,
                                          LRG_POOL_GROWTH_DOUBLE);

    /* Exhaust all possible objects */
    for (i = 0; i < 10; i++)
    {
        obj = lrg_object_pool_acquire (pool);
        g_assert_nonnull (obj);
    }

    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 10);

    /* Should return NULL - max size reached */
    obj = lrg_object_pool_acquire (pool);
    g_assert_null (obj);
}

/* ==========================================================================
 * Test Cases - Pool Utilities
 * ========================================================================== */

static void
test_pool_prewarm (void)
{
    g_autoptr(LrgObjectPool) pool = NULL;

    pool = lrg_object_pool_new (TEST_TYPE_POOLABLE_OBJECT, 0,
                                 LRG_POOL_GROWTH_DOUBLE);

    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 0);

    lrg_object_pool_prewarm (pool, 50);

    g_assert_cmpuint (lrg_object_pool_get_total_size (pool), ==, 50);
    g_assert_cmpuint (lrg_object_pool_get_available_count (pool), ==, 50);
    g_assert_cmpuint (lrg_object_pool_get_active_count (pool), ==, 0);
}

static void
test_pool_shrink_to_fit (PoolFixture   *fixture,
                         gconstpointer  user_data)
{
    guint i;

    /* Trigger growth */
    for (i = 0; i < 15; i++)
        lrg_object_pool_acquire (fixture->pool);

    g_assert_cmpuint (lrg_object_pool_get_total_size (fixture->pool), >=, 15);

    /* Release all */
    lrg_object_pool_release_all_active (fixture->pool);

    /* Shrink */
    lrg_object_pool_shrink_to_fit (fixture->pool);

    /* Should be back to initial size */
    g_assert_cmpuint (lrg_object_pool_get_total_size (fixture->pool), ==, 10);
}

static void
test_pool_clear (PoolFixture   *fixture,
                 gconstpointer  user_data)
{
    guint i;

    /* Acquire some objects */
    for (i = 0; i < 5; i++)
        lrg_object_pool_acquire (fixture->pool);

    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 5);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 5);

    lrg_object_pool_clear (fixture->pool);

    g_assert_cmpuint (lrg_object_pool_get_total_size (fixture->pool), ==, 0);
    g_assert_cmpuint (lrg_object_pool_get_active_count (fixture->pool), ==, 0);
    g_assert_cmpuint (lrg_object_pool_get_available_count (fixture->pool), ==, 0);
}

/* ==========================================================================
 * Test Cases - Iteration
 * ========================================================================== */

static gint foreach_count = 0;
static gint foreach_value_sum = 0;

static gboolean
foreach_callback (LrgPoolable *object,
                  gpointer     user_data)
{
    TestPoolableObject *poolable = TEST_POOLABLE_OBJECT (object);

    foreach_count++;
    foreach_value_sum += poolable->value;

    return TRUE;
}

static void
test_pool_foreach_active (PoolFixture   *fixture,
                          gconstpointer  user_data)
{
    GObject *obj1, *obj2, *obj3;

    foreach_count = 0;
    foreach_value_sum = 0;

    obj1 = lrg_object_pool_acquire (fixture->pool);
    obj2 = lrg_object_pool_acquire (fixture->pool);
    obj3 = lrg_object_pool_acquire (fixture->pool);

    TEST_POOLABLE_OBJECT (obj1)->value = 10;
    TEST_POOLABLE_OBJECT (obj2)->value = 20;
    TEST_POOLABLE_OBJECT (obj3)->value = 30;

    lrg_object_pool_foreach_active (fixture->pool, foreach_callback, NULL);

    g_assert_cmpint (foreach_count, ==, 3);
    g_assert_cmpint (foreach_value_sum, ==, 60);
}

static gboolean
foreach_early_stop_callback (LrgPoolable *object,
                             gpointer     user_data)
{
    gint *count = (gint *)user_data;
    (*count)++;

    /* Stop after 2 iterations */
    return (*count < 2);
}

static void
test_pool_foreach_early_stop (PoolFixture   *fixture,
                              gconstpointer  user_data)
{
    gint count = 0;
    guint i;

    for (i = 0; i < 5; i++)
        lrg_object_pool_acquire (fixture->pool);

    lrg_object_pool_foreach_active (fixture->pool, foreach_early_stop_callback,
                                     &count);

    g_assert_cmpint (count, ==, 2);
}

/* ==========================================================================
 * Test Cases - Object Reuse
 * ========================================================================== */

static void
test_pool_object_reuse (PoolFixture   *fixture,
                        gconstpointer  user_data)
{
    GObject *obj1, *obj2;
    TestPoolableObject *poolable1, *poolable2;

    /* Acquire and modify */
    obj1 = lrg_object_pool_acquire (fixture->pool);
    poolable1 = TEST_POOLABLE_OBJECT (obj1);
    poolable1->x = 100.0f;
    poolable1->value = 42;

    /* Release */
    lrg_object_pool_release (fixture->pool, LRG_POOLABLE (obj1));

    /* Acquire again - should get same object back */
    obj2 = lrg_object_pool_acquire (fixture->pool);
    poolable2 = TEST_POOLABLE_OBJECT (obj2);

    g_assert_true (poolable1 == poolable2);

    /* Values should be reset */
    g_assert_cmpfloat_with_epsilon (poolable2->x, 0.0f, 0.01f);
    g_assert_cmpint (poolable2->value, ==, 0);
    g_assert_cmpint (poolable2->reset_count, ==, 1);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    /* LrgPoolable interface tests */
    g_test_add_func ("/object-pool/poolable/interface-implemented",
                     test_poolable_interface_implemented);
    g_test_add_func ("/object-pool/poolable/reset",
                     test_poolable_reset);
    g_test_add_func ("/object-pool/poolable/active-state",
                     test_poolable_active_state);

    /* LrgObjectPool construction tests */
    g_test_add_func ("/object-pool/pool/new",
                     test_pool_new);
    g_test_add_func ("/object-pool/pool/new-with-max",
                     test_pool_new_with_max);
    g_test_add ("/object-pool/pool/initial-state",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_initial_state,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/properties",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_properties,
                pool_fixture_tear_down);

    /* Pool operation tests */
    g_test_add ("/object-pool/pool/acquire",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_acquire,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/acquire-multiple",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_acquire_multiple,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/acquire-with-init",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_acquire_with_init,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/release",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_release,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/release-all-active",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_release_all_active,
                pool_fixture_tear_down);

    /* Growth policy tests */
    g_test_add_func ("/object-pool/pool/growth-double",
                     test_pool_growth_double);
    g_test_add_func ("/object-pool/pool/growth-linear",
                     test_pool_growth_linear);
    g_test_add_func ("/object-pool/pool/growth-fixed",
                     test_pool_growth_fixed);
    g_test_add_func ("/object-pool/pool/max-size-respected",
                     test_pool_max_size_respected);

    /* Utility tests */
    g_test_add_func ("/object-pool/pool/prewarm",
                     test_pool_prewarm);
    g_test_add ("/object-pool/pool/shrink-to-fit",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_shrink_to_fit,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/clear",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_clear,
                pool_fixture_tear_down);

    /* Iteration tests */
    g_test_add ("/object-pool/pool/foreach-active",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_foreach_active,
                pool_fixture_tear_down);
    g_test_add ("/object-pool/pool/foreach-early-stop",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_foreach_early_stop,
                pool_fixture_tear_down);

    /* Reuse tests */
    g_test_add ("/object-pool/pool/object-reuse",
                PoolFixture, NULL,
                pool_fixture_set_up, test_pool_object_reuse,
                pool_fixture_tear_down);

    return g_test_run ();
}
