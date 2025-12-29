/* test-crash.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgCrashReporter and LrgCrashDialog.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Crash Dialog for Testing
 *
 * Tracks show/hide calls for verification.
 * ========================================================================== */

#define TEST_TYPE_CRASH_DIALOG (test_crash_dialog_get_type ())
G_DECLARE_FINAL_TYPE (TestCrashDialog, test_crash_dialog, TEST, CRASH_DIALOG, LrgCrashDialog)

struct _TestCrashDialog
{
    LrgCrashDialog parent_instance;

    gboolean shown;
    gboolean hidden;
    gchar   *last_crash_info;
};

G_DEFINE_TYPE (TestCrashDialog, test_crash_dialog, LRG_TYPE_CRASH_DIALOG)

static void
test_crash_dialog_show (LrgCrashDialog *dialog,
                        const gchar    *crash_info)
{
    TestCrashDialog *self = TEST_CRASH_DIALOG (dialog);

    self->shown = TRUE;
    g_free (self->last_crash_info);
    self->last_crash_info = g_strdup (crash_info);
}

static void
test_crash_dialog_hide (LrgCrashDialog *dialog)
{
    TestCrashDialog *self = TEST_CRASH_DIALOG (dialog);
    self->hidden = TRUE;
}

static void
test_crash_dialog_finalize (GObject *object)
{
    TestCrashDialog *self = TEST_CRASH_DIALOG (object);

    g_free (self->last_crash_info);

    G_OBJECT_CLASS (test_crash_dialog_parent_class)->finalize (object);
}

static void
test_crash_dialog_class_init (TestCrashDialogClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgCrashDialogClass *dialog_class = LRG_CRASH_DIALOG_CLASS (klass);

    object_class->finalize = test_crash_dialog_finalize;

    dialog_class->show = test_crash_dialog_show;
    dialog_class->hide = test_crash_dialog_hide;
}

static void
test_crash_dialog_init (TestCrashDialog *self)
{
    self->shown = FALSE;
    self->hidden = FALSE;
    self->last_crash_info = NULL;
}

static TestCrashDialog *
test_crash_dialog_new (void)
{
    return g_object_new (TEST_TYPE_CRASH_DIALOG, NULL);
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgCrashReporter *reporter;
    gchar            *temp_dir;
    gchar            *log_file;
} CrashFixture;

static void
crash_fixture_set_up (CrashFixture  *fixture,
                      gconstpointer  user_data)
{
    GError *error = NULL;

    fixture->reporter = lrg_crash_reporter_new ();
    g_assert_nonnull (fixture->reporter);

    fixture->temp_dir = g_dir_make_tmp ("test-crash-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (fixture->temp_dir);

    fixture->log_file = g_build_filename (fixture->temp_dir, "crash.log", NULL);
}

static void
crash_fixture_tear_down (CrashFixture  *fixture,
                         gconstpointer  user_data)
{
    /* Uninstall handlers if installed */
    if (lrg_crash_reporter_is_installed (fixture->reporter))
        lrg_crash_reporter_uninstall (fixture->reporter);

    g_clear_object (&fixture->reporter);

    if (fixture->log_file != NULL)
    {
        g_unlink (fixture->log_file);
        g_free (fixture->log_file);
    }

    if (fixture->temp_dir != NULL)
    {
        g_rmdir (fixture->temp_dir);
        g_free (fixture->temp_dir);
    }
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_crash_reporter_new (void)
{
    g_autoptr(LrgCrashReporter) reporter = NULL;

    reporter = lrg_crash_reporter_new ();

    g_assert_nonnull (reporter);
    g_assert_true (LRG_IS_CRASH_REPORTER (reporter));
    g_assert_false (lrg_crash_reporter_is_installed (reporter));
}

static void
test_crash_reporter_singleton (void)
{
    LrgCrashReporter *reporter1;
    LrgCrashReporter *reporter2;

    reporter1 = lrg_crash_reporter_get_default ();
    g_assert_nonnull (reporter1);

    reporter2 = lrg_crash_reporter_get_default ();
    g_assert_nonnull (reporter2);

    /* Should be the same instance */
    g_assert_true (reporter1 == reporter2);
}

/* ==========================================================================
 * Test Cases - App Info
 * ========================================================================== */

static void
test_crash_reporter_app_name (CrashFixture  *fixture,
                              gconstpointer  user_data)
{
    const gchar *name;

    lrg_crash_reporter_set_app_name (fixture->reporter, "TestGame");
    name = lrg_crash_reporter_get_app_name (fixture->reporter);

    g_assert_cmpstr (name, ==, "TestGame");
}

static void
test_crash_reporter_app_version (CrashFixture  *fixture,
                                 gconstpointer  user_data)
{
    const gchar *version;

    lrg_crash_reporter_set_app_version (fixture->reporter, "1.2.3");
    version = lrg_crash_reporter_get_app_version (fixture->reporter);

    g_assert_cmpstr (version, ==, "1.2.3");
}

static void
test_crash_reporter_log_path (CrashFixture  *fixture,
                              gconstpointer  user_data)
{
    const gchar *path;

    lrg_crash_reporter_set_log_path (fixture->reporter, "/tmp/test.log");
    path = lrg_crash_reporter_get_log_path (fixture->reporter);

    g_assert_cmpstr (path, ==, "/tmp/test.log");

    /* Test NULL */
    lrg_crash_reporter_set_log_path (fixture->reporter, NULL);
    path = lrg_crash_reporter_get_log_path (fixture->reporter);

    g_assert_null (path);
}

/* ==========================================================================
 * Test Cases - Metadata
 * ========================================================================== */

static void
test_crash_reporter_metadata (CrashFixture  *fixture,
                              gconstpointer  user_data)
{
    /* Add some metadata */
    lrg_crash_reporter_add_metadata (fixture->reporter, "level", "5");
    lrg_crash_reporter_add_metadata (fixture->reporter, "player", "TestPlayer");
    lrg_crash_reporter_add_metadata (fixture->reporter, "score", "12345");

    /* Remove one */
    lrg_crash_reporter_remove_metadata (fixture->reporter, "score");

    /* Clear all */
    lrg_crash_reporter_clear_metadata (fixture->reporter);

    /* Should not crash after clearing */
    lrg_crash_reporter_add_metadata (fixture->reporter, "new_key", "new_value");
}

/* ==========================================================================
 * Test Cases - Dialog
 * ========================================================================== */

static void
test_crash_reporter_set_dialog (CrashFixture  *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(TestCrashDialog) dialog = NULL;
    LrgCrashDialog            *retrieved;

    dialog = test_crash_dialog_new ();

    lrg_crash_reporter_set_dialog (fixture->reporter, LRG_CRASH_DIALOG (dialog));
    retrieved = lrg_crash_reporter_get_dialog (fixture->reporter);

    g_assert_true (retrieved == LRG_CRASH_DIALOG (dialog));

    /* Set to NULL */
    lrg_crash_reporter_set_dialog (fixture->reporter, NULL);
    retrieved = lrg_crash_reporter_get_dialog (fixture->reporter);

    g_assert_null (retrieved);
}

/* ==========================================================================
 * Test Cases - Installation
 * ========================================================================== */

static void
test_crash_reporter_install (CrashFixture  *fixture,
                             gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    g_assert_false (lrg_crash_reporter_is_installed (fixture->reporter));

    result = lrg_crash_reporter_install (fixture->reporter, &error);
    g_assert_no_error (error);
    g_assert_true (result);

    g_assert_true (lrg_crash_reporter_is_installed (fixture->reporter));
}

static void
test_crash_reporter_uninstall (CrashFixture  *fixture,
                               gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    lrg_crash_reporter_install (fixture->reporter, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_crash_reporter_is_installed (fixture->reporter));

    lrg_crash_reporter_uninstall (fixture->reporter);

    g_assert_false (lrg_crash_reporter_is_installed (fixture->reporter));
}

static void
test_crash_reporter_install_twice (CrashFixture  *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    result = lrg_crash_reporter_install (fixture->reporter, &error);
    g_assert_no_error (error);
    g_assert_true (result);

    /* Second install should fail */
    result = lrg_crash_reporter_install (fixture->reporter, &error);
    g_assert_false (result);
    g_assert_error (error, LRG_CRASH_REPORTER_ERROR,
                    LRG_CRASH_REPORTER_ERROR_ALREADY_INSTALLED);
}

/* ==========================================================================
 * Test Cases - Crash Dialog Base Class
 * ========================================================================== */

static void
test_crash_dialog_terminal_new (void)
{
    g_autoptr(LrgCrashDialogTerminal) dialog = NULL;

    dialog = lrg_crash_dialog_terminal_new ();

    g_assert_nonnull (dialog);
    g_assert_true (LRG_IS_CRASH_DIALOG (dialog));
    g_assert_true (LRG_IS_CRASH_DIALOG_TERMINAL (dialog));
}

static void
test_crash_dialog_show_hide (void)
{
    g_autoptr(TestCrashDialog) dialog = NULL;

    dialog = test_crash_dialog_new ();

    g_assert_false (dialog->shown);
    g_assert_false (dialog->hidden);

    lrg_crash_dialog_show (LRG_CRASH_DIALOG (dialog), "Test crash info");

    g_assert_true (dialog->shown);
    g_assert_cmpstr (dialog->last_crash_info, ==, "Test crash info");

    lrg_crash_dialog_hide (LRG_CRASH_DIALOG (dialog));

    g_assert_true (dialog->hidden);
}

/* ==========================================================================
 * Test Cases - Error Quark
 * ========================================================================== */

static void
test_crash_reporter_error_quark (void)
{
    GQuark quark;

    quark = lrg_crash_reporter_error_quark ();
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-crash-reporter-error-quark");
}

/* ==========================================================================
 * Test Cases - Manual Report (without actually crashing)
 * ========================================================================== */

static void
test_crash_reporter_report_crash (CrashFixture  *fixture,
                                  gconstpointer  user_data)
{
    g_autoptr(TestCrashDialog) dialog = NULL;

    dialog = test_crash_dialog_new ();

    lrg_crash_reporter_set_app_name (fixture->reporter, "TestApp");
    lrg_crash_reporter_set_app_version (fixture->reporter, "1.0.0");
    lrg_crash_reporter_set_dialog (fixture->reporter, LRG_CRASH_DIALOG (dialog));
    lrg_crash_reporter_set_log_path (fixture->reporter, fixture->log_file);
    lrg_crash_reporter_add_metadata (fixture->reporter, "test_key", "test_value");

    /* Manually trigger a crash report (with fake signal) */
    lrg_crash_reporter_report_crash (fixture->reporter, 11, NULL, NULL);  /* SIGSEGV */

    /* Dialog should have been shown */
    g_assert_true (dialog->shown);
    g_assert_nonnull (dialog->last_crash_info);

    /* Crash info should contain relevant information */
    g_assert_nonnull (strstr (dialog->last_crash_info, "TestApp"));
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
    g_test_add_func ("/crash/reporter/new", test_crash_reporter_new);
    g_test_add_func ("/crash/reporter/singleton", test_crash_reporter_singleton);

    /* App Info */
    g_test_add ("/crash/reporter/app-name",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_app_name,
                crash_fixture_tear_down);

    g_test_add ("/crash/reporter/app-version",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_app_version,
                crash_fixture_tear_down);

    g_test_add ("/crash/reporter/log-path",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_log_path,
                crash_fixture_tear_down);

    /* Metadata */
    g_test_add ("/crash/reporter/metadata",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_metadata,
                crash_fixture_tear_down);

    /* Dialog */
    g_test_add ("/crash/reporter/set-dialog",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_set_dialog,
                crash_fixture_tear_down);

    /* Installation */
    g_test_add ("/crash/reporter/install",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_install,
                crash_fixture_tear_down);

    g_test_add ("/crash/reporter/uninstall",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_uninstall,
                crash_fixture_tear_down);

    g_test_add ("/crash/reporter/install-twice",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_install_twice,
                crash_fixture_tear_down);

    /* Crash Dialog */
    g_test_add_func ("/crash/dialog/terminal-new", test_crash_dialog_terminal_new);
    g_test_add_func ("/crash/dialog/show-hide", test_crash_dialog_show_hide);

    /* Error Quark */
    g_test_add_func ("/crash/error-quark", test_crash_reporter_error_quark);

    /* Manual Report */
    g_test_add ("/crash/reporter/report-crash",
                CrashFixture, NULL,
                crash_fixture_set_up,
                test_crash_reporter_report_crash,
                crash_fixture_tear_down);

    return g_test_run ();
}
