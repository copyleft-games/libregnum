/* test-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for demo mode support.
 */

#include <glib.h>
#include <glib-object.h>

#include "../src/lrg-types.h"
#include "../src/lrg-enums.h"
#include "../src/demo/lrg-demo-gatable.h"
#include "../src/demo/lrg-demo-manager.h"

/* ==========================================================================
 * Test Mock Gatable Object
 * ========================================================================== */

#define TEST_TYPE_GATABLE (test_gatable_get_type ())
G_DECLARE_FINAL_TYPE (TestGatable, test_gatable, TEST, GATABLE, GObject)

struct _TestGatable
{
    GObject parent_instance;

    gchar *content_id;
    gboolean is_demo_content;
    gchar *unlock_message;
};

static void test_gatable_demo_gatable_init (LrgDemoGatableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestGatable, test_gatable, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DEMO_GATABLE,
                                                test_gatable_demo_gatable_init))

static const gchar *
test_gatable_get_content_id (LrgDemoGatable *gatable)
{
    TestGatable *self = TEST_GATABLE (gatable);
    return self->content_id;
}

static gboolean
test_gatable_is_demo_content (LrgDemoGatable *gatable)
{
    TestGatable *self = TEST_GATABLE (gatable);
    return self->is_demo_content;
}

static const gchar *
test_gatable_get_unlock_message (LrgDemoGatable *gatable)
{
    TestGatable *self = TEST_GATABLE (gatable);
    return self->unlock_message;
}

static void
test_gatable_demo_gatable_init (LrgDemoGatableInterface *iface)
{
    iface->get_content_id = test_gatable_get_content_id;
    iface->is_demo_content = test_gatable_is_demo_content;
    iface->get_unlock_message = test_gatable_get_unlock_message;
}

static void
test_gatable_finalize (GObject *object)
{
    TestGatable *self = TEST_GATABLE (object);

    g_free (self->content_id);
    g_free (self->unlock_message);

    G_OBJECT_CLASS (test_gatable_parent_class)->finalize (object);
}

static void
test_gatable_class_init (TestGatableClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = test_gatable_finalize;
}

static void
test_gatable_init (TestGatable *self)
{
    self->content_id = NULL;
    self->is_demo_content = TRUE;
    self->unlock_message = NULL;
}

static TestGatable *
test_gatable_new (const gchar *content_id,
                  gboolean     is_demo_content)
{
    TestGatable *gatable;

    gatable = g_object_new (TEST_TYPE_GATABLE, NULL);
    gatable->content_id = g_strdup (content_id);
    gatable->is_demo_content = is_demo_content;

    return gatable;
}

/* ==========================================================================
 * Demo Gatable Interface Tests
 * ========================================================================== */

static void
test_demo_gatable_interface (void)
{
    g_autoptr(TestGatable) gatable = NULL;

    gatable = test_gatable_new ("level-1", TRUE);
    g_assert_nonnull (gatable);
    g_assert_true (LRG_IS_DEMO_GATABLE (gatable));

    g_assert_cmpstr (lrg_demo_gatable_get_content_id (LRG_DEMO_GATABLE (gatable)),
                     ==, "level-1");
    g_assert_true (lrg_demo_gatable_is_demo_content (LRG_DEMO_GATABLE (gatable)));
}

static void
test_demo_gatable_default_message (void)
{
    g_autoptr(TestGatable) gatable = NULL;
    const gchar *message;

    gatable = test_gatable_new ("boss-final", FALSE);
    gatable->unlock_message = g_strdup ("Upgrade to fight the final boss!");

    message = lrg_demo_gatable_get_unlock_message (LRG_DEMO_GATABLE (gatable));
    g_assert_cmpstr (message, ==, "Upgrade to fight the final boss!");
}

/* ==========================================================================
 * Demo Manager Tests
 * ========================================================================== */

static void
test_demo_manager_new (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();
    g_assert_nonnull (manager);
    g_assert_false (lrg_demo_manager_get_demo_mode (manager));
}

static void
test_demo_manager_singleton (void)
{
    LrgDemoManager *manager1;
    LrgDemoManager *manager2;

    manager1 = lrg_demo_manager_get_default ();
    manager2 = lrg_demo_manager_get_default ();

    g_assert_true (manager1 == manager2);
}

static void
test_demo_manager_demo_mode (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();

    g_assert_false (lrg_demo_manager_get_demo_mode (manager));

    lrg_demo_manager_set_demo_mode (manager, TRUE);
    g_assert_true (lrg_demo_manager_get_demo_mode (manager));

    lrg_demo_manager_set_demo_mode (manager, FALSE);
    g_assert_false (lrg_demo_manager_get_demo_mode (manager));
}

static void
test_demo_manager_time_limit (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();

    g_assert_cmpfloat (lrg_demo_manager_get_time_limit (manager), ==, 0.0f);
    g_assert_cmpfloat (lrg_demo_manager_get_time_remaining (manager), ==, -1.0f);

    lrg_demo_manager_set_time_limit (manager, 1800.0f);
    g_assert_cmpfloat (lrg_demo_manager_get_time_limit (manager), ==, 1800.0f);
}

static void
test_demo_manager_time_tracking (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    gfloat remaining;

    manager = lrg_demo_manager_new ();

    lrg_demo_manager_set_demo_mode (manager, TRUE);
    lrg_demo_manager_set_time_limit (manager, 100.0f);
    lrg_demo_manager_start (manager);

    g_assert_cmpfloat (lrg_demo_manager_get_time_elapsed (manager), ==, 0.0f);

    /* Simulate some updates */
    lrg_demo_manager_update (manager, 10.0f);
    g_assert_cmpfloat (lrg_demo_manager_get_time_elapsed (manager), ==, 10.0f);

    remaining = lrg_demo_manager_get_time_remaining (manager);
    g_assert_cmpfloat (remaining, ==, 90.0f);

    lrg_demo_manager_update (manager, 50.0f);
    g_assert_cmpfloat (lrg_demo_manager_get_time_elapsed (manager), ==, 60.0f);
    g_assert_cmpfloat (lrg_demo_manager_get_time_remaining (manager), ==, 40.0f);
}

static void
test_demo_manager_content_gating (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();

    g_assert_false (lrg_demo_manager_is_content_gated (manager, "level-1"));

    lrg_demo_manager_gate_content (manager, "level-5");
    lrg_demo_manager_gate_content (manager, "level-6");

    g_assert_true (lrg_demo_manager_is_content_gated (manager, "level-5"));
    g_assert_true (lrg_demo_manager_is_content_gated (manager, "level-6"));
    g_assert_false (lrg_demo_manager_is_content_gated (manager, "level-1"));

    lrg_demo_manager_ungate_content (manager, "level-5");
    g_assert_false (lrg_demo_manager_is_content_gated (manager, "level-5"));
}

static void
test_demo_manager_check_access (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    g_autoptr(TestGatable) demo_content = NULL;
    g_autoptr(TestGatable) gated_content = NULL;
    g_autoptr(GError) error = NULL;

    manager = lrg_demo_manager_new ();

    /* Create test objects */
    demo_content = test_gatable_new ("level-1", TRUE);
    gated_content = test_gatable_new ("level-5", FALSE);

    /* Gate level-5 */
    lrg_demo_manager_gate_content (manager, "level-5");

    /* Not in demo mode - all access allowed */
    g_assert_true (lrg_demo_manager_check_access (manager,
                                                   LRG_DEMO_GATABLE (gated_content),
                                                   &error));
    g_assert_no_error (error);

    /* Enable demo mode */
    lrg_demo_manager_set_demo_mode (manager, TRUE);

    /* Demo content should be accessible */
    g_assert_true (lrg_demo_manager_check_access (manager,
                                                   LRG_DEMO_GATABLE (demo_content),
                                                   &error));
    g_assert_no_error (error);

    /* Gated content should be blocked */
    g_assert_false (lrg_demo_manager_check_access (manager,
                                                    LRG_DEMO_GATABLE (gated_content),
                                                    &error));
    g_assert_error (error, LRG_DEMO_ERROR, LRG_DEMO_ERROR_CONTENT_GATED);
}

static void
test_demo_manager_gated_content_list (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    g_autoptr(GPtrArray) gated = NULL;

    manager = lrg_demo_manager_new ();

    lrg_demo_manager_gate_content (manager, "a");
    lrg_demo_manager_gate_content (manager, "b");
    lrg_demo_manager_gate_content (manager, "c");

    gated = lrg_demo_manager_get_gated_content (manager);
    g_assert_cmpuint (gated->len, ==, 3);

    lrg_demo_manager_clear_gated_content (manager);
    g_ptr_array_unref (gated);

    gated = lrg_demo_manager_get_gated_content (manager);
    g_assert_cmpuint (gated->len, ==, 0);
}

static void
test_demo_manager_demo_saves (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    g_autoptr(GPtrArray) saves = NULL;

    manager = lrg_demo_manager_new ();

    g_assert_false (lrg_demo_manager_is_demo_save (manager, "save1"));

    lrg_demo_manager_mark_save_as_demo (manager, "save1");
    lrg_demo_manager_mark_save_as_demo (manager, "save2");

    g_assert_true (lrg_demo_manager_is_demo_save (manager, "save1"));
    g_assert_true (lrg_demo_manager_is_demo_save (manager, "save2"));

    saves = lrg_demo_manager_get_demo_saves (manager);
    g_assert_cmpuint (saves->len, ==, 2);

    lrg_demo_manager_convert_demo_save (manager, "save1");
    g_assert_false (lrg_demo_manager_is_demo_save (manager, "save1"));
}

static void
test_demo_manager_purchase_url (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();

    g_assert_null (lrg_demo_manager_get_purchase_url (manager));

    lrg_demo_manager_set_purchase_url (manager, "https://store.example.com/game");
    g_assert_cmpstr (lrg_demo_manager_get_purchase_url (manager),
                     ==, "https://store.example.com/game");

    lrg_demo_manager_set_purchase_url (manager, NULL);
    g_assert_null (lrg_demo_manager_get_purchase_url (manager));
}

static void
test_demo_manager_properties (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    gboolean demo_mode;
    gfloat time_limit;
    gchar *purchase_url;

    manager = lrg_demo_manager_new ();

    g_object_set (manager,
                  "demo-mode", TRUE,
                  "time-limit", 600.0f,
                  "purchase-url", "https://example.com",
                  NULL);

    g_object_get (manager,
                  "demo-mode", &demo_mode,
                  "time-limit", &time_limit,
                  "purchase-url", &purchase_url,
                  NULL);

    g_assert_true (demo_mode);
    g_assert_cmpfloat (time_limit, ==, 600.0f);
    g_assert_cmpstr (purchase_url, ==, "https://example.com");

    g_free (purchase_url);
}

/* ==========================================================================
 * Signal Tests
 * ========================================================================== */

static gboolean demo_ended_called = FALSE;
static LrgDemoEndReason last_end_reason = 0;

static void
on_demo_ended (LrgDemoManager   *manager,
               LrgDemoEndReason  reason,
               gpointer          user_data)
{
    demo_ended_called = TRUE;
    last_end_reason = reason;
}

static void
test_demo_manager_demo_ended_signal (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;

    manager = lrg_demo_manager_new ();
    demo_ended_called = FALSE;

    g_signal_connect (manager, "demo-ended", G_CALLBACK (on_demo_ended), NULL);

    lrg_demo_manager_set_demo_mode (manager, TRUE);
    lrg_demo_manager_start (manager);

    lrg_demo_manager_stop (manager, LRG_DEMO_END_REASON_MANUAL);

    g_assert_true (demo_ended_called);
    g_assert_cmpint (last_end_reason, ==, LRG_DEMO_END_REASON_MANUAL);
}

static gboolean time_warning_called = FALSE;
static gfloat last_warning_time = 0.0f;

static void
on_time_warning (LrgDemoManager *manager,
                 gfloat          seconds_remaining,
                 gpointer        user_data)
{
    time_warning_called = TRUE;
    last_warning_time = seconds_remaining;
}

static void
test_demo_manager_time_warning_signal (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    const gfloat warnings[] = { 60.0f, 30.0f };

    manager = lrg_demo_manager_new ();
    time_warning_called = FALSE;

    g_signal_connect (manager, "time-warning", G_CALLBACK (on_time_warning), NULL);

    lrg_demo_manager_set_demo_mode (manager, TRUE);
    lrg_demo_manager_set_time_limit (manager, 100.0f);
    lrg_demo_manager_set_warning_times (manager, warnings, 2);
    lrg_demo_manager_start (manager);

    /* Update to 45 seconds remaining (should trigger 60 warning) */
    lrg_demo_manager_update (manager, 55.0f);
    g_assert_true (time_warning_called);
    g_assert_cmpfloat (last_warning_time, <=, 60.0f);
}

static gboolean content_blocked_called = FALSE;
static gchar *last_blocked_id = NULL;

static void
on_content_blocked (LrgDemoManager *manager,
                    const gchar    *content_id,
                    const gchar    *unlock_message,
                    gpointer        user_data)
{
    content_blocked_called = TRUE;
    g_free (last_blocked_id);
    last_blocked_id = g_strdup (content_id);
}

static void
test_demo_manager_content_blocked_signal (void)
{
    g_autoptr(LrgDemoManager) manager = NULL;
    g_autoptr(TestGatable) gatable = NULL;
    g_autoptr(GError) error = NULL;

    manager = lrg_demo_manager_new ();
    content_blocked_called = FALSE;

    g_signal_connect (manager, "content-blocked", G_CALLBACK (on_content_blocked), NULL);

    gatable = test_gatable_new ("secret-level", FALSE);
    lrg_demo_manager_set_demo_mode (manager, TRUE);
    lrg_demo_manager_gate_content (manager, "secret-level");

    lrg_demo_manager_check_access (manager, LRG_DEMO_GATABLE (gatable), &error);

    g_assert_true (content_blocked_called);
    g_assert_cmpstr (last_blocked_id, ==, "secret-level");

    g_free (last_blocked_id);
    last_blocked_id = NULL;
}

/* ==========================================================================
 * Error Domain Tests
 * ========================================================================== */

static void
test_demo_error_quark (void)
{
    GQuark quark;

    quark = LRG_DEMO_ERROR;
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-demo-error-quark");
}

static void
test_demo_error_type (void)
{
    GType type;
    GEnumClass *enum_class;
    GEnumValue *value;

    type = LRG_TYPE_DEMO_ERROR;
    g_assert_true (G_TYPE_IS_ENUM (type));

    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    value = g_enum_get_value (enum_class, LRG_DEMO_ERROR_CONTENT_GATED);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "content-gated");

    g_type_class_unref (enum_class);
}

static void
test_demo_end_reason_type (void)
{
    GType type;
    GEnumClass *enum_class;
    GEnumValue *value;

    type = LRG_TYPE_DEMO_END_REASON;
    g_assert_true (G_TYPE_IS_ENUM (type));

    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    value = g_enum_get_value (enum_class, LRG_DEMO_END_REASON_TIME_LIMIT);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "time-limit");

    value = g_enum_get_value (enum_class, LRG_DEMO_END_REASON_UPGRADED);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "upgraded");

    g_type_class_unref (enum_class);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Demo Gatable interface tests */
    g_test_add_func ("/demo/gatable/interface", test_demo_gatable_interface);
    g_test_add_func ("/demo/gatable/default-message", test_demo_gatable_default_message);

    /* Demo Manager tests */
    g_test_add_func ("/demo/manager/new", test_demo_manager_new);
    g_test_add_func ("/demo/manager/singleton", test_demo_manager_singleton);
    g_test_add_func ("/demo/manager/demo-mode", test_demo_manager_demo_mode);
    g_test_add_func ("/demo/manager/time-limit", test_demo_manager_time_limit);
    g_test_add_func ("/demo/manager/time-tracking", test_demo_manager_time_tracking);
    g_test_add_func ("/demo/manager/content-gating", test_demo_manager_content_gating);
    g_test_add_func ("/demo/manager/check-access", test_demo_manager_check_access);
    g_test_add_func ("/demo/manager/gated-content-list", test_demo_manager_gated_content_list);
    g_test_add_func ("/demo/manager/demo-saves", test_demo_manager_demo_saves);
    g_test_add_func ("/demo/manager/purchase-url", test_demo_manager_purchase_url);
    g_test_add_func ("/demo/manager/properties", test_demo_manager_properties);

    /* Signal tests */
    g_test_add_func ("/demo/signals/demo-ended", test_demo_manager_demo_ended_signal);
    g_test_add_func ("/demo/signals/time-warning", test_demo_manager_time_warning_signal);
    g_test_add_func ("/demo/signals/content-blocked", test_demo_manager_content_blocked_signal);

    /* Error domain tests */
    g_test_add_func ("/demo/error/quark", test_demo_error_quark);
    g_test_add_func ("/demo/error/type", test_demo_error_type);
    g_test_add_func ("/demo/error/end-reason-type", test_demo_end_reason_type);

    return g_test_run ();
}
