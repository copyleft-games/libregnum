/* test-configurable.c - Tests for the LrgConfigurable interface
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Exercises (headlessly) the LrgConfigurable interface dispatch, error
 * propagation, and the LrgGameTemplate bridge: the apply_args class vfunc, the
 * args-applied signal, the applied-args getter, and the default no-op.
 */

#include <glib.h>
#include <libregnum.h>

/* ===================================================================== */
/* A plain GObject implementing LrgConfigurable directly                 */
/* ===================================================================== */

#define TEST_TYPE_THING (test_thing_get_type ())
G_DECLARE_FINAL_TYPE (TestThing, test_thing, TEST, THING, GObject)

struct _TestThing
{
    GObject  parent_instance;
    GStrv    last;
    gboolean fail;
};

static void test_thing_configurable_init (LrgConfigurableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestThing, test_thing, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_CONFIGURABLE,
                                                test_thing_configurable_init))

static gboolean
test_thing_apply_args (LrgConfigurable    *configurable,
                       const gchar *const *argv,
                       GError            **error)
{
    TestThing *self = TEST_THING (configurable);

    if (self->fail)
    {
        g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                     "thing refused");
        return FALSE;
    }

    g_clear_pointer (&self->last, g_strfreev);
    self->last = g_strdupv ((gchar **) argv);
    return TRUE;
}

static void
test_thing_configurable_init (LrgConfigurableInterface *iface)
{
    iface->apply_args = test_thing_apply_args;
}

static void
test_thing_finalize (GObject *object)
{
    TestThing *self = TEST_THING (object);

    g_clear_pointer (&self->last, g_strfreev);
    G_OBJECT_CLASS (test_thing_parent_class)->finalize (object);
}

static void
test_thing_class_init (TestThingClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = test_thing_finalize;
}

static void
test_thing_init (TestThing *self)
{
    self->last = NULL;
    self->fail = FALSE;
}

/* ===================================================================== */
/* An LrgGameTemplate subclass that overrides the apply_args class vfunc */
/* ===================================================================== */

#define TEST_TYPE_TMPL (test_tmpl_get_type ())
G_DECLARE_FINAL_TYPE (TestTmpl, test_tmpl, TEST, TMPL, LrgGameTemplate)

struct _TestTmpl
{
    LrgGameTemplate parent_instance;
    gint            calls;
    gboolean        fail;
};

G_DEFINE_FINAL_TYPE (TestTmpl, test_tmpl, LRG_TYPE_GAME_TEMPLATE)

static gboolean
test_tmpl_apply_args (LrgGameTemplate    *self,
                      const gchar *const *argv,
                      GError            **error)
{
    TestTmpl *t = TEST_TMPL (self);

    (void) argv;
    t->calls++;

    if (t->fail)
    {
        g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "tmpl refused");
        return FALSE;
    }

    return TRUE;
}

static void
test_tmpl_class_init (TestTmplClass *klass)
{
    LRG_GAME_TEMPLATE_CLASS (klass)->apply_args = test_tmpl_apply_args;
}

static void
test_tmpl_init (TestTmpl *self)
{
    self->calls = 0;
    self->fail = FALSE;
}

/* A small signal-capture record. */
typedef struct
{
    guint  count;
    GStrv  argv;
} SignalCapture;

static void
on_args_applied (LrgGameTemplate *self,
                 GStrv            argv,
                 gpointer         user_data)
{
    SignalCapture *cap = user_data;

    (void) self;
    cap->count++;
    g_clear_pointer (&cap->argv, g_strfreev);
    cap->argv = g_strdupv (argv);
}

/* ===================================================================== */
/* Tests                                                                 */
/* ===================================================================== */

static void
test_configurable_dispatch (void)
{
    g_autoptr(GError)   error = NULL;
    g_autoptr(TestThing) thing = g_object_new (TEST_TYPE_THING, NULL);
    const gchar *const  argv[] = { "prog", "--alpha", "beta", NULL };
    gboolean            ok;

    g_assert_true (LRG_IS_CONFIGURABLE (thing));

    ok = lrg_configurable_apply_args (LRG_CONFIGURABLE (thing), argv, &error);
    g_assert_no_error (error);
    g_assert_true (ok);

    g_assert_nonnull (thing->last);
    g_assert_cmpstr (thing->last[0], ==, "prog");
    g_assert_cmpstr (thing->last[1], ==, "--alpha");
    g_assert_cmpstr (thing->last[2], ==, "beta");
    g_assert_null (thing->last[3]);
}

static void
test_configurable_error (void)
{
    g_autoptr(GError)   error = NULL;
    g_autoptr(TestThing) thing = g_object_new (TEST_TYPE_THING, NULL);
    const gchar *const  argv[] = { "prog", NULL };
    gboolean            ok;

    thing->fail = TRUE;
    ok = lrg_configurable_apply_args (LRG_CONFIGURABLE (thing), argv, &error);
    g_assert_false (ok);
    g_assert_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED);
    g_assert_null (thing->last);
}

static void
test_template_apply_and_signal (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestTmpl) tmpl = g_object_new (TEST_TYPE_TMPL, NULL);
    const gchar *const  argv[] = { "prog", "--x", "1", NULL };
    const gchar * const *applied;
    SignalCapture      cap = { 0, NULL };
    gboolean           ok;

    g_assert_true (LRG_IS_CONFIGURABLE (tmpl));
    g_signal_connect (tmpl, "args-applied", G_CALLBACK (on_args_applied), &cap);

    ok = lrg_game_template_apply_args (LRG_GAME_TEMPLATE (tmpl), argv, &error);
    g_assert_no_error (error);
    g_assert_true (ok);
    g_assert_cmpint (tmpl->calls, ==, 1);

    /* The signal fired once, carrying the applied vector. */
    g_assert_cmpuint (cap.count, ==, 1);
    g_assert_nonnull (cap.argv);
    g_assert_cmpstr (cap.argv[1], ==, "--x");

    /* The getter returns the same vector. */
    applied = lrg_game_template_get_applied_args (LRG_GAME_TEMPLATE (tmpl));
    g_assert_nonnull (applied);
    g_assert_cmpstr (applied[2], ==, "1");

    g_clear_pointer (&cap.argv, g_strfreev);
}

static void
test_template_error_no_signal (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestTmpl) tmpl = g_object_new (TEST_TYPE_TMPL, NULL);
    const gchar *const  argv[] = { "prog", "--x", NULL };
    SignalCapture      cap = { 0, NULL };
    gboolean           ok;

    tmpl->fail = TRUE;
    g_signal_connect (tmpl, "args-applied", G_CALLBACK (on_args_applied), &cap);

    ok = lrg_game_template_apply_args (LRG_GAME_TEMPLATE (tmpl), argv, &error);
    g_assert_false (ok);
    g_assert_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED);
    g_assert_cmpint (tmpl->calls, ==, 1);

    /* On failure, no signal and no stored vector. */
    g_assert_cmpuint (cap.count, ==, 0);
    g_assert_null (lrg_game_template_get_applied_args (LRG_GAME_TEMPLATE (tmpl)));
}

static void
test_template_default_noop (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGameTemplate) tmpl = g_object_new (LRG_TYPE_GAME_TEMPLATE, NULL);
    const gchar *const  argv[] = { "prog", "--anything", NULL };
    gboolean           ok;

    /* The base class accepts any arguments as a no-op. */
    ok = lrg_game_template_apply_args (tmpl, argv, &error);
    g_assert_no_error (error);
    g_assert_true (ok);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/configurable/dispatch", test_configurable_dispatch);
    g_test_add_func ("/configurable/error", test_configurable_error);
    g_test_add_func ("/configurable/template-apply-and-signal",
                     test_template_apply_and_signal);
    g_test_add_func ("/configurable/template-error-no-signal",
                     test_template_error_no_signal);
    g_test_add_func ("/configurable/template-default-noop",
                     test_template_default_noop);

    return g_test_run ();
}
