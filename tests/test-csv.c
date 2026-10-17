/* test-csv.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the CSV reader, CSV string tables merged into LrgLocale and
 * the CLDR plural rules.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* row @r, field @f of a parse result */
#define FIELD(rows, r, f) (((gchar **) g_ptr_array_index ((rows), (r)))[(f)])

static void
test_csv_basic (void)
{
    g_autoptr(GPtrArray) rows = NULL;
    GError *error = NULL;

    rows = lrg_csv_parse ("a,b,c\n1,,3\n", -1, ',', &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows->len, ==, 2);
    g_assert_cmpuint (g_strv_length (g_ptr_array_index (rows, 0)), ==, 3);
    g_assert_cmpstr (FIELD (rows, 0, 2), ==, "c");
    g_assert_cmpstr (FIELD (rows, 1, 1), ==, "");
    g_assert_cmpstr (FIELD (rows, 1, 2), ==, "3");
}

static void
test_csv_quoted (void)
{
    g_autoptr(GPtrArray) rows = NULL;
    GError *error = NULL;
    const gchar *text =
        "\xEF\xBB\xBFKey,Value\r\n"
        "multi,\"line one\r\nline \"\"two\"\", with comma\"\r\n"
        "\r\n"
        "accent,\"caf\xC3\xA9\"\r"
        "last,no newline";

    rows = lrg_csv_parse (text, -1, ',', &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows->len, ==, 4);
    /* the byte order mark is not part of the first key */
    g_assert_cmpstr (FIELD (rows, 0, 0), ==, "Key");
    /* line breaks inside quotes are kept as written, quotes unescaped */
    g_assert_cmpstr (FIELD (rows, 1, 1), ==, "line one\r\nline \"two\", with comma");
    g_assert_cmpstr (FIELD (rows, 2, 1), ==, "caf\xC3\xA9");
    g_assert_cmpstr (FIELD (rows, 3, 1), ==, "no newline");

    /* an unquoted field keeps a quote as an ordinary character */
    g_clear_pointer (&rows, g_ptr_array_unref);
    rows = lrg_csv_parse ("say,he said \"hi\"\n", -1, ',', &error);
    g_assert_no_error (error);
    g_assert_cmpstr (FIELD (rows, 0, 1), ==, "he said \"hi\"");

    /* another separator, an empty quoted field */
    g_clear_pointer (&rows, g_ptr_array_unref);
    rows = lrg_csv_parse ("x;\"\";\"a;b\"", -1, ';', &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_strv_length (g_ptr_array_index (rows, 0)), ==, 3);
    g_assert_cmpstr (FIELD (rows, 0, 1), ==, "");
    g_assert_cmpstr (FIELD (rows, 0, 2), ==, "a;b");
}

static void
test_csv_errors (void)
{
    GPtrArray *rows;
    GError *error = NULL;

    rows = lrg_csv_parse ("k,\"never closed\nmore", -1, ',', &error);
    g_assert_null (rows);
    g_assert_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_UNTERMINATED_QUOTE);
    g_clear_error (&error);

    rows = lrg_csv_parse ("k,\"closed\"junk\n", -1, ',', &error);
    g_assert_null (rows);
    g_assert_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_MALFORMED);
    g_clear_error (&error);

    rows = lrg_csv_parse ("k,\xC3\x28\n", -1, ',', &error);
    g_assert_null (rows);
    g_assert_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_INVALID_UTF8);
    g_clear_error (&error);

    /* empty input is zero rows, not an error */
    rows = lrg_csv_parse ("", 0, ',', &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows->len, ==, 0);
    g_ptr_array_unref (rows);
}

static void
test_locale_merge_csv (void)
{
    g_autoptr(LrgLocale) locale = NULL;
    GError *error = NULL;
    gint n;

    locale = lrg_locale_new ("fr", "Fran\xC3\xA7" "ais");
    lrg_locale_set_string (locale, "keep", "kept");
    lrg_locale_set_string (locale, "replace", "old");

    n = lrg_locale_merge_csv (locale,
                              "Key,Value\n"
                              "replace,nouveau\n"
                              "multi,\"ligne 1\nligne 2\n\"\n"
                              ",skipped\n"
                              "%d day#one,%d jour\n"
                              "%d day#other,%d jours\n", -1, &error);
    g_assert_no_error (error);
    g_assert_cmpint (n, ==, 4);
    g_assert_cmpstr (lrg_locale_get_string (locale, "keep"), ==, "kept");
    g_assert_cmpstr (lrg_locale_get_string (locale, "replace"), ==, "nouveau");
    /* the trailing line break of a value goes, inner ones stay */
    g_assert_cmpstr (lrg_locale_get_string (locale, "multi"), ==, "ligne 1\nligne 2");
    g_assert_false (lrg_locale_has_string (locale, "Key"));
    /* French counts 0 as singular */
    g_assert_cmpstr (lrg_locale_get_plural (locale, "%d day", 0), ==, "%d jour");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "%d day", 1), ==, "%d jour");
    g_assert_cmpstr (lrg_locale_get_plural (locale, "%d day", 2), ==, "%d jours");

    /* a broken table changes nothing */
    n = lrg_locale_merge_csv (locale, "replace,broken\nx,\"open\n", -1, &error);
    g_assert_cmpint (n, ==, -1);
    g_assert_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE);
    g_clear_error (&error);
    g_assert_cmpstr (lrg_locale_get_string (locale, "replace"), ==, "nouveau");

    /* missing files report the file error */
    n = lrg_locale_merge_csv_file (locale, "/nonexistent/table.csv", &error);
    g_assert_cmpint (n, ==, -1);
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_clear_error (&error);
}

static void
test_locale_merge_csv_file (void)
{
    g_autoptr(LrgLocale) locale = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    GError *error = NULL;

    dir = g_dir_make_tmp ("lrg-csv-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "Table.csv", NULL);
    g_assert_true (g_file_set_contents (path, "Key,Value\nhello,\"Gr\xC3\xBC\xC3\x9F" "e\"\n", -1, &error));

    locale = lrg_locale_new ("de", "Deutsch");
    g_assert_cmpint (lrg_locale_merge_csv_file (locale, path, &error), ==, 1);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_locale_get_string (locale, "hello"), ==, "Gr\xC3\xBC\xC3\x9F" "e");

    g_unlink (path);
    g_rmdir (dir);
}

static void
test_plural_rules (void)
{
    g_autoptr(LrgLocale) ru = NULL;

    g_assert_cmpint (lrg_plural_rule_for_language ("en"), ==, LRG_PLURAL_RULE_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language ("de_DE.UTF-8"), ==, LRG_PLURAL_RULE_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language ("FR"), ==, LRG_PLURAL_RULE_ZERO_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language ("pt_BR"), ==, LRG_PLURAL_RULE_ZERO_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language ("pt-PT"), ==, LRG_PLURAL_RULE_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language ("ru-RU"), ==, LRG_PLURAL_RULE_EAST_SLAVIC);
    g_assert_cmpint (lrg_plural_rule_for_language ("pl"), ==, LRG_PLURAL_RULE_POLISH);
    g_assert_cmpint (lrg_plural_rule_for_language ("cs"), ==, LRG_PLURAL_RULE_CZECH);
    g_assert_cmpint (lrg_plural_rule_for_language ("ja"), ==, LRG_PLURAL_RULE_NONE);
    g_assert_cmpint (lrg_plural_rule_for_language ("xx"), ==, LRG_PLURAL_RULE_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language (""), ==, LRG_PLURAL_RULE_ONE_OTHER);
    g_assert_cmpint (lrg_plural_rule_for_language (NULL), ==, LRG_PLURAL_RULE_ONE_OTHER);

    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_ONE_OTHER, -1), ==, LRG_PLURAL_ONE);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_ONE_OTHER, 0), ==, LRG_PLURAL_OTHER);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_NONE, 1), ==, LRG_PLURAL_OTHER);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_EAST_SLAVIC, 21), ==, LRG_PLURAL_ONE);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_EAST_SLAVIC, 11), ==, LRG_PLURAL_MANY);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_EAST_SLAVIC, 23), ==, LRG_PLURAL_FEW);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_EAST_SLAVIC, 13), ==, LRG_PLURAL_MANY);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_POLISH, 21), ==, LRG_PLURAL_MANY);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_POLISH, 22), ==, LRG_PLURAL_FEW);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_CZECH, 4), ==, LRG_PLURAL_FEW);
    g_assert_cmpint (lrg_plural_rule_select (LRG_PLURAL_RULE_CZECH, 5), ==, LRG_PLURAL_OTHER);

    /* a locale takes its language's rule */
    ru = lrg_locale_new ("ru", "Russian");
    g_assert_cmpint (lrg_locale_get_plural_rule (ru), ==, LRG_PLURAL_RULE_EAST_SLAVIC);
    lrg_locale_set_plural (ru, "file", LRG_PLURAL_ONE, "1");
    lrg_locale_set_plural (ru, "file", LRG_PLURAL_FEW, "2-4");
    lrg_locale_set_plural (ru, "file", LRG_PLURAL_MANY, "5+");
    g_assert_cmpstr (lrg_locale_get_plural (ru, "file", 31), ==, "1");
    g_assert_cmpstr (lrg_locale_get_plural (ru, "file", 102), ==, "2-4");
    g_assert_cmpstr (lrg_locale_get_plural (ru, "file", 112), ==, "5+");
    lrg_locale_set_plural_rule (ru, LRG_PLURAL_RULE_NONE);
    g_assert_cmpint (lrg_locale_get_plural_form (ru, 1), ==, LRG_PLURAL_OTHER);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/csv/basic", test_csv_basic);
    g_test_add_func ("/csv/quoted", test_csv_quoted);
    g_test_add_func ("/csv/errors", test_csv_errors);
    g_test_add_func ("/csv/locale-merge", test_locale_merge_csv);
    g_test_add_func ("/csv/locale-merge-file", test_locale_merge_csv_file);
    g_test_add_func ("/csv/plural-rules", test_plural_rules);

    return g_test_run ();
}
