/* test-i18n.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the I18N (localization) module.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <yaml-glib.h>

/* ========================================================================== */
/* Test Fixtures                                                              */
/* ========================================================================== */

typedef struct
{
    gchar *test_dir;
} I18nFixture;

static void
i18n_fixture_setup (I18nFixture *fixture,
                    gconstpointer user_data)
{
    GError *error = NULL;

    (void)user_data;

    /* Create a temp directory for test files */
    fixture->test_dir = g_dir_make_tmp ("libregnum-i18n-XXXXXX", &error);
    g_assert_no_error (error);
}

static void
i18n_fixture_teardown (I18nFixture *fixture,
                       gconstpointer user_data)
{
    (void)user_data;

    if (fixture->test_dir != NULL)
    {
        /* Clean up test files */
        g_autoptr(GDir) dir = NULL;
        const gchar *filename;

        dir = g_dir_open (fixture->test_dir, 0, NULL);
        if (dir != NULL)
        {
            while ((filename = g_dir_read_name (dir)) != NULL)
            {
                g_autofree gchar *path = g_build_filename (fixture->test_dir, filename, NULL);
                g_unlink (path);
            }
        }

        g_rmdir (fixture->test_dir);
        g_free (fixture->test_dir);
    }
}

/* Helper function to write a test locale file */
static void
write_locale_file (I18nFixture *fixture,
                   const gchar *filename,
                   const gchar *contents)
{
    g_autofree gchar *path = NULL;
    GError *error = NULL;

    path = g_build_filename (fixture->test_dir, filename, NULL);
    g_file_set_contents (path, contents, -1, &error);
    g_assert_no_error (error);
}

/* ========================================================================== */
/* LrgLocale Tests                                                            */
/* ========================================================================== */

static void
test_locale_new (void)
{
    g_autoptr(LrgLocale) locale = NULL;

    locale = lrg_locale_new ("en", "English");
    g_assert_nonnull (locale);
    g_assert_true (LRG_IS_LOCALE (locale));
    g_assert_cmpstr (lrg_locale_get_code (locale), ==, "en");
    g_assert_cmpstr (lrg_locale_get_name (locale), ==, "English");
    g_assert_cmpuint (lrg_locale_get_string_count (locale), ==, 0);
}

static void
test_locale_set_get_string (void)
{
    g_autoptr(LrgLocale) locale = NULL;

    locale = lrg_locale_new ("en", "English");

    lrg_locale_set_string (locale, "greeting", "Hello");
    lrg_locale_set_string (locale, "farewell", "Goodbye");

    g_assert_cmpstr (lrg_locale_get_string (locale, "greeting"), ==, "Hello");
    g_assert_cmpstr (lrg_locale_get_string (locale, "farewell"), ==, "Goodbye");
    g_assert_null (lrg_locale_get_string (locale, "nonexistent"));

    g_assert_true (lrg_locale_has_string (locale, "greeting"));
    g_assert_false (lrg_locale_has_string (locale, "nonexistent"));
}

static void
test_locale_plurals (void)
{
    g_autoptr(LrgLocale) locale = NULL;

    locale = lrg_locale_new ("en", "English");

    /* Set plural forms */
    lrg_locale_set_plural (locale, "items", LRG_PLURAL_ONE, "%d item");
    lrg_locale_set_plural (locale, "items", LRG_PLURAL_OTHER, "%d items");

    /* Test plural selection */
    g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 1), ==, "%d item");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 0), ==, "%d items");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 2), ==, "%d items");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 100), ==, "%d items");
}

static void
test_locale_plural_form (void)
{
    g_autoptr(LrgLocale) locale = NULL;

    locale = lrg_locale_new ("en", "English");

    /* English plural rules: 1 = one, everything else = other */
    g_assert_cmpint (lrg_locale_get_plural_form (locale, 0), ==, LRG_PLURAL_OTHER);
    g_assert_cmpint (lrg_locale_get_plural_form (locale, 1), ==, LRG_PLURAL_ONE);
    g_assert_cmpint (lrg_locale_get_plural_form (locale, 2), ==, LRG_PLURAL_OTHER);
    g_assert_cmpint (lrg_locale_get_plural_form (locale, -1), ==, LRG_PLURAL_ONE);
}

static void
test_locale_get_keys (void)
{
    g_autoptr(LrgLocale) locale = NULL;
    g_autoptr(GPtrArray) keys = NULL;

    locale = lrg_locale_new ("en", "English");
    lrg_locale_set_string (locale, "a", "A");
    lrg_locale_set_string (locale, "b", "B");
    lrg_locale_set_plural (locale, "items", LRG_PLURAL_ONE, "item");

    keys = lrg_locale_get_keys (locale);
    g_assert_nonnull (keys);
    g_assert_cmpuint (keys->len, ==, 3);
}

static void
test_locale_from_file (I18nFixture *fixture,
                       gconstpointer user_data)
{
    g_autoptr(LrgLocale) locale = NULL;
    g_autofree gchar *path = NULL;
    GError *error = NULL;
    const gchar *yaml =
        "code: de\n"
        "name: Deutsch\n"
        "strings:\n"
        "  greeting: Hallo\n"
        "  farewell: Auf Wiedersehen\n"
        "  items:\n"
        "    one: '%d Artikel'\n"
        "    other: '%d Artikel'\n";

    (void)user_data;

    write_locale_file (fixture, "de.yaml", yaml);
    path = g_build_filename (fixture->test_dir, "de.yaml", NULL);

    locale = lrg_locale_new_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (locale);
    g_assert_cmpstr (lrg_locale_get_code (locale), ==, "de");
    g_assert_cmpstr (lrg_locale_get_name (locale), ==, "Deutsch");
    g_assert_cmpstr (lrg_locale_get_string (locale, "greeting"), ==, "Hallo");
    g_assert_cmpstr (lrg_locale_get_string (locale, "farewell"), ==, "Auf Wiedersehen");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 1), ==, "%d Artikel");
}

static void
test_locale_from_file_missing (I18nFixture *fixture,
                               gconstpointer user_data)
{
    g_autoptr(LrgLocale) locale = NULL;
    g_autofree gchar *path = NULL;
    GError *error = NULL;

    (void)user_data;

    path = g_build_filename (fixture->test_dir, "nonexistent.yaml", NULL);

    locale = lrg_locale_new_from_file (path, &error);
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_null (locale);
    g_clear_error (&error);
}

/* ========================================================================== */
/* LrgLocalization Tests                                                      */
/* ========================================================================== */

static void
test_localization_singleton (void)
{
    LrgLocalization *loc1;
    LrgLocalization *loc2;

    loc1 = lrg_localization_get_default ();
    loc2 = lrg_localization_get_default ();

    g_assert_nonnull (loc1);
    g_assert_true (loc1 == loc2);
}

static void
test_localization_add_locale (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) locale = NULL;
    LrgLocale *retrieved;

    loc = lrg_localization_get_default ();

    locale = lrg_locale_new ("fr", "Français");
    lrg_locale_set_string (locale, "hello", "Bonjour");

    lrg_localization_add_locale (loc, g_steal_pointer (&locale));

    g_assert_true (lrg_localization_has_locale (loc, "fr"));
    g_assert_false (lrg_localization_has_locale (loc, "xx"));

    retrieved = lrg_localization_get_locale (loc, "fr");
    g_assert_nonnull (retrieved);
    g_assert_cmpstr (lrg_locale_get_name (retrieved), ==, "Français");
}

static void
test_localization_set_current (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) en = NULL;
    g_autoptr(LrgLocale) es = NULL;
    gboolean result;

    loc = lrg_localization_get_default ();

    en = lrg_locale_new ("en_test", "English Test");
    lrg_locale_set_string (en, "hello", "Hello");
    lrg_localization_add_locale (loc, g_steal_pointer (&en));

    es = lrg_locale_new ("es_test", "Spanish Test");
    lrg_locale_set_string (es, "hello", "Hola");
    lrg_localization_add_locale (loc, g_steal_pointer (&es));

    result = lrg_localization_set_current (loc, "en_test");
    g_assert_true (result);
    g_assert_cmpstr (lrg_localization_get_current_code (loc), ==, "en_test");

    result = lrg_localization_set_current (loc, "es_test");
    g_assert_true (result);
    g_assert_cmpstr (lrg_localization_get_current_code (loc), ==, "es_test");

    result = lrg_localization_set_current (loc, "nonexistent");
    g_assert_false (result);
}

static void
test_localization_get_string (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) locale = NULL;
    const gchar *str;

    loc = lrg_localization_get_default ();

    locale = lrg_locale_new ("test_get", "Test Get");
    lrg_locale_set_string (locale, "message", "Test Message");
    lrg_localization_add_locale (loc, g_steal_pointer (&locale));

    lrg_localization_set_current (loc, "test_get");

    str = lrg_localization_get (loc, "message");
    g_assert_cmpstr (str, ==, "Test Message");

    str = lrg_localization_get (loc, "nonexistent");
    g_assert_null (str);
}

static void
test_localization_format (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) locale = NULL;
    g_autofree gchar *result = NULL;

    loc = lrg_localization_get_default ();

    locale = lrg_locale_new ("test_fmt", "Test Format");
    lrg_locale_set_string (locale, "welcome", "Welcome, %s!");
    lrg_localization_add_locale (loc, g_steal_pointer (&locale));

    lrg_localization_set_current (loc, "test_fmt");

    result = lrg_localization_format (loc, "welcome", "User");
    g_assert_cmpstr (result, ==, "Welcome, User!");
}

static void
test_localization_plural (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) locale = NULL;
    const gchar *str;

    loc = lrg_localization_get_default ();

    locale = lrg_locale_new ("test_pl", "Test Plural");
    lrg_locale_set_plural (locale, "files", LRG_PLURAL_ONE, "1 file");
    lrg_locale_set_plural (locale, "files", LRG_PLURAL_OTHER, "%d files");
    lrg_localization_add_locale (loc, g_steal_pointer (&locale));

    lrg_localization_set_current (loc, "test_pl");

    str = lrg_localization_get_plural (loc, "files", 1);
    g_assert_cmpstr (str, ==, "1 file");

    str = lrg_localization_get_plural (loc, "files", 5);
    g_assert_cmpstr (str, ==, "%d files");
}

/* ==========================================================================
 * Checked formats: a translation must read the fallback's argument types
 * ========================================================================== */

/* fmt_setup: fallback "<prefix>_src" with @source for "k", current
 * "<prefix>_tr" with @translation for "k" */
static LrgLocalization *
fmt_setup (const gchar *prefix,
           const gchar *source,
           const gchar *translation)
{
	LrgLocalization *loc = lrg_localization_get_default ();
	g_autofree gchar *src_code = g_strdup_printf ("%s_src", prefix);
	g_autofree gchar *tr_code = g_strdup_printf ("%s_tr", prefix);
	LrgLocale *src = lrg_locale_new (src_code, "Source");
	LrgLocale *tr = lrg_locale_new (tr_code, "Translation");

	lrg_locale_set_string (src, "k", source);
	lrg_locale_set_string (tr, "k", translation);
	lrg_localization_add_locale (loc, src);
	lrg_localization_add_locale (loc, tr);
	g_assert_true (lrg_localization_set_fallback (loc, src_code));
	g_assert_true (lrg_localization_set_current (loc, tr_code));
	return loc;
}

/* a translation with the source's argument types is used */
static void
test_format_checked_ok (void)
{
	LrgLocalization *loc = fmt_setup ("fmtok", "%s has %d coins", "%s a %d pièces");
	g_autofree gchar *out = lrg_localization_format (loc, "k", "Ana", 3);

	g_assert_cmpstr (out, ==, "Ana a 3 pièces");
}

/* reordered positional arguments of the same types are fine */
static void
test_format_checked_positional (void)
{
	LrgLocalization *loc = fmt_setup ("fmtpos", "%s has %d coins", "%2$d Münzen hat %1$s");
	g_autofree gchar *out = lrg_localization_format (loc, "k", "Ana", 3);

	g_assert_cmpstr (out, ==, "3 Münzen hat Ana");
}

/* fewer arguments than the source is fine (a prefix of them) */
static void
test_format_checked_fewer (void)
{
	LrgLocalization *loc = fmt_setup ("fmtfew", "%s has %d coins", "%s is rich");
	g_autofree gchar *out = lrg_localization_format (loc, "k", "Ana", 3);

	g_assert_cmpstr (out, ==, "Ana is rich");
}

/* a type mismatch (%s where the source has %d) falls back, warning once */
static void
test_format_checked_mismatch (void)
{
	LrgLocalization *loc = fmt_setup ("fmtbad", "%d coins", "%s pièces");
	g_autofree gchar *first = NULL;
	g_autofree gchar *second = NULL;

	g_test_expect_message ("Libregnum-I18N", G_LOG_LEVEL_WARNING, "*fmtbad_tr*does not match*");
	first = lrg_localization_format (loc, "k", 7);
	g_test_assert_expected_messages ();
	g_assert_cmpstr (first, ==, "7 coins");

	/* reported once: no second warning */
	second = lrg_localization_format (loc, "k", 8);
	g_assert_cmpstr (second, ==, "8 coins");
}

/* more arguments than the source, and %n, are rejected */
static void
test_format_checked_extra_and_n (void)
{
	LrgLocalization *loc = fmt_setup ("fmtmore", "%d coins", "%d coins from %s");
	g_autofree gchar *more = NULL;
	g_autofree gchar *n = NULL;
	LrgLocalization *loc2;

	g_test_expect_message ("Libregnum-I18N", G_LOG_LEVEL_WARNING, "*fmtmore_tr*");
	more = lrg_localization_format (loc, "k", 7);
	g_test_assert_expected_messages ();
	g_assert_cmpstr (more, ==, "7 coins");

	loc2 = fmt_setup ("fmtn", "%d coins", "%d coins%n");
	g_test_expect_message ("Libregnum-I18N", G_LOG_LEVEL_WARNING, "*fmtn_tr*");
	n = lrg_localization_format (loc2, "k", 7);
	g_test_assert_expected_messages ();
	g_assert_cmpstr (n, ==, "7 coins");
}

/* width/precision stars and length modifiers are arguments with types */
static void
test_format_checked_widths (void)
{
	LrgLocalization *loc = fmt_setup ("fmtwid", "%*d|%.2f|%" G_GINT64_FORMAT,
	                                  "%*d;%.3f;%" G_GINT64_FORMAT);
	g_autofree gchar *out = lrg_localization_format (loc, "k", 4, 42, 1.5, (gint64) 9);
	LrgLocalization *loc2;
	g_autofree gchar *bad = NULL;

	g_assert_cmpstr (out, ==, "  42;1.500;9");

	/* dropping the star shifts every type: rejected */
	loc2 = fmt_setup ("fmtwid2", "%*d|%.2f", "%d|%.2f");
	g_test_expect_message ("Libregnum-I18N", G_LOG_LEVEL_WARNING, "*fmtwid2_tr*");
	bad = lrg_localization_format (loc2, "k", 4, 42, 1.5);
	g_test_assert_expected_messages ();
	g_assert_cmpstr (bad, ==, "  42|1.50");
}

/* plurals: a singular translation may leave the number out */
static void
test_format_checked_plural (void)
{
	LrgLocalization *loc = lrg_localization_get_default ();
	LrgLocale *src = lrg_locale_new ("fmtpl_src", "Source");
	LrgLocale *tr = lrg_locale_new ("fmtpl_tr", "Translation");
	g_autofree gchar *one = NULL;
	g_autofree gchar *many = NULL;

	lrg_locale_set_plural (src, "k", LRG_PLURAL_ONE, "%d file");
	lrg_locale_set_plural (src, "k", LRG_PLURAL_OTHER, "%d files");
	lrg_locale_set_plural (tr, "k", LRG_PLURAL_ONE, "un fichier");
	lrg_locale_set_plural (tr, "k", LRG_PLURAL_OTHER, "%d fichiers");
	lrg_localization_add_locale (loc, src);
	lrg_localization_add_locale (loc, tr);
	g_assert_true (lrg_localization_set_fallback (loc, "fmtpl_src"));
	g_assert_true (lrg_localization_set_current (loc, "fmtpl_tr"));

	one = lrg_localization_format_plural (loc, "k", 1, 1);
	many = lrg_localization_format_plural (loc, "k", 5, 5);
	g_assert_cmpstr (one, ==, "un fichier");
	g_assert_cmpstr (many, ==, "5 fichiers");
}

/* a format with nothing to compare against must still not use %n */
static void
test_format_checked_no_fallback (void)
{
	LrgLocalization *loc = lrg_localization_get_default ();
	LrgLocale *only = lrg_locale_new ("fmtonly", "Only");
	g_autofree gchar *ok = NULL;
	g_autofree gchar *bad = NULL;

	lrg_locale_set_string (only, "plain", "%s!");
	lrg_locale_set_string (only, "evil", "%s%n");
	lrg_localization_add_locale (loc, only);
	g_assert_true (lrg_localization_set_fallback (loc, "fmtonly"));
	g_assert_true (lrg_localization_set_current (loc, "fmtonly"));

	ok = lrg_localization_format (loc, "plain", "hi");
	g_assert_cmpstr (ok, ==, "hi!");
	bad = lrg_localization_format (loc, "evil", "hi");
	g_assert_null (bad);
}

static void
test_localization_fallback (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) primary = NULL;
    g_autoptr(LrgLocale) fallback = NULL;
    const gchar *str;

    loc = lrg_localization_get_default ();

    /* Create fallback locale with more strings */
    fallback = lrg_locale_new ("test_fb", "Test Fallback");
    lrg_locale_set_string (fallback, "common", "Common String");
    lrg_locale_set_string (fallback, "fallback_only", "Fallback Only");
    lrg_localization_add_locale (loc, g_steal_pointer (&fallback));

    /* Create primary locale with fewer strings */
    primary = lrg_locale_new ("test_pr", "Test Primary");
    lrg_locale_set_string (primary, "common", "Primary Common");
    lrg_localization_add_locale (loc, g_steal_pointer (&primary));

    lrg_localization_set_fallback (loc, "test_fb");
    lrg_localization_set_current (loc, "test_pr");

    /* String in both - should use primary */
    str = lrg_localization_get (loc, "common");
    g_assert_cmpstr (str, ==, "Primary Common");

    /* String only in fallback - should use fallback */
    str = lrg_localization_get (loc, "fallback_only");
    g_assert_cmpstr (str, ==, "Fallback Only");
}

static void
test_localization_remove_locale (void)
{
    LrgLocalization *loc;
    g_autoptr(LrgLocale) locale = NULL;
    gboolean result;

    loc = lrg_localization_get_default ();

    locale = lrg_locale_new ("test_rm", "Test Remove");
    lrg_localization_add_locale (loc, g_steal_pointer (&locale));

    g_assert_true (lrg_localization_has_locale (loc, "test_rm"));

    result = lrg_localization_remove_locale (loc, "test_rm");
    g_assert_true (result);
    g_assert_false (lrg_localization_has_locale (loc, "test_rm"));

    result = lrg_localization_remove_locale (loc, "test_rm");
    g_assert_false (result);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgLocale tests */
    g_test_add_func ("/i18n/locale/new", test_locale_new);
    g_test_add_func ("/i18n/locale/set-get-string", test_locale_set_get_string);
    g_test_add_func ("/i18n/locale/plurals", test_locale_plurals);
    g_test_add_func ("/i18n/locale/plural-form", test_locale_plural_form);
    g_test_add_func ("/i18n/locale/get-keys", test_locale_get_keys);
    g_test_add ("/i18n/locale/from-file", I18nFixture, NULL,
                i18n_fixture_setup, test_locale_from_file, i18n_fixture_teardown);
    g_test_add ("/i18n/locale/from-file-missing", I18nFixture, NULL,
                i18n_fixture_setup, test_locale_from_file_missing, i18n_fixture_teardown);

    /* LrgLocalization tests */
    g_test_add_func ("/i18n/localization/singleton", test_localization_singleton);
    g_test_add_func ("/i18n/localization/add-locale", test_localization_add_locale);
    g_test_add_func ("/i18n/localization/set-current", test_localization_set_current);
    g_test_add_func ("/i18n/localization/get-string", test_localization_get_string);
    g_test_add_func ("/i18n/localization/format", test_localization_format);
    g_test_add_func ("/i18n/localization/plural", test_localization_plural);
    g_test_add_func ("/i18n/localization/fallback", test_localization_fallback);
    g_test_add_func ("/i18n/localization/format-checked/ok", test_format_checked_ok);
    g_test_add_func ("/i18n/localization/format-checked/positional", test_format_checked_positional);
    g_test_add_func ("/i18n/localization/format-checked/fewer", test_format_checked_fewer);
    g_test_add_func ("/i18n/localization/format-checked/mismatch", test_format_checked_mismatch);
    g_test_add_func ("/i18n/localization/format-checked/extra-and-n", test_format_checked_extra_and_n);
    g_test_add_func ("/i18n/localization/format-checked/widths", test_format_checked_widths);
    g_test_add_func ("/i18n/localization/format-checked/plural", test_format_checked_plural);
    g_test_add_func ("/i18n/localization/format-checked/no-fallback", test_format_checked_no_fallback);
    g_test_add_func ("/i18n/localization/remove-locale", test_localization_remove_locale);

    return g_test_run ();
}
