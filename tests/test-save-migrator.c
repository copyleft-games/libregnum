/* test-save-migrator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSaveDocument, LrgSaveMigration and LrgSaveMigrator:
 * chained conversion over several versions, gaps, failing steps
 * (atomicity), document steps that rename and split sections, output
 * type checks and the refusal to convert newer data back.
 */

#include <glib.h>
#include <libregnum.h>
#include <string.h>

/* ==========================================================================
 * Steps used by the tests
 *
 * The test section "stats" is an a{sv}:
 *   v1 {hp: i}
 *   v2 {hp: i, mp: i}              (adds mp = 10)
 *   v3 {health: i, mp: i}          (renames hp to health)
 *   v4 {health: x, mp: x}          (widens both to int64)
 *   v5 {health: x, mp: x, tag: s}  (adds tag = user_data)
 * ========================================================================== */

static GVariant *
stats_1_to_2 (GVariant  *state,
              gpointer   user_data,
              GError   **error)
{
    GVariantDict dict;

    g_variant_dict_init (&dict, state);
    g_variant_dict_insert (&dict, "mp", "i", 10);
    return g_variant_dict_end (&dict);
}

static GVariant *
stats_2_to_3 (GVariant  *state,
              gpointer   user_data,
              GError   **error)
{
    GVariantDict dict;
    gint32 hp;

    g_variant_dict_init (&dict, state);
    if (!g_variant_dict_lookup (&dict, "hp", "i", &hp))
    {
        g_variant_dict_clear (&dict);
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no hp");
        return NULL;
    }
    g_variant_dict_remove (&dict, "hp");
    g_variant_dict_insert (&dict, "health", "i", hp);
    return g_variant_dict_end (&dict);
}

static GVariant *
stats_3_to_4 (GVariant  *state,
              gpointer   user_data,
              GError   **error)
{
    GVariantDict dict;
    gint32 health;
    gint32 mp;

    g_assert_true (g_variant_lookup (state, "health", "i", &health));
    g_assert_true (g_variant_lookup (state, "mp", "i", &mp));
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "health", "x", (gint64) health);
    g_variant_dict_insert (&dict, "mp", "x", (gint64) mp);
    return g_variant_dict_end (&dict);
}

static GVariant *
stats_4_to_5 (GVariant  *state,
              gpointer   user_data,
              GError   **error)
{
    GVariantDict dict;

    g_variant_dict_init (&dict, state);
    g_variant_dict_insert (&dict, "tag", "s", (const gchar *) user_data);
    return g_variant_dict_end (&dict);
}

/* a step that always fails */
static GVariant *
failing_section_step (GVariant  *state,
                      gpointer   user_data,
                      GError   **error)
{
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "this step refuses");
    return NULL;
}

/* a step that fails without saying why */
static GVariant *
silent_section_step (GVariant  *state,
                     gpointer   user_data,
                     GError   **error)
{
    return NULL;
}

/* a step that returns the wrong type */
static GVariant *
string_section_step (GVariant  *state,
                     gpointer   user_data,
                     GError   **error)
{
    return g_variant_new_string ("not a dictionary");
}

/* adds the four stats steps; the last one tags with @tag */
static void
add_stats_chain (LrgSaveMigrator *migrator,
                 const gchar     *tag)
{
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 1, "add mp",
                                                       stats_1_to_2, NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 2, "hp is health",
                                                       stats_2_to_3, NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 3, "64-bit",
                                                       stats_3_to_4, NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 4, "tag",
                                                       stats_4_to_5, g_strdup (tag), g_free,
                                                       &error));
    g_assert_no_error (error);
}

/*
 * Document steps. Format 1 has "player" {name: s, hp: i, gold: i}.
 * Format 2 renames "player" to "hero" (no section versions yet).
 * Format 3 introduces section versions and splits the gold out of
 * "hero" into "purse" (version 1): "hero" becomes {name: s},
 * "stats" {hp: i} at version 1 and every other section version 1.
 */
static gboolean
doc_1_to_2 (LrgSaveDocument  *document,
            gpointer          user_data,
            GError          **error)
{
    return lrg_save_document_rename_section (document, "player", "hero", error);
}

static gboolean
doc_2_to_3 (LrgSaveDocument  *document,
            gpointer          user_data,
            GError          **error)
{
    g_auto(GStrv) names = NULL;
    GVariant *hero;
    const gchar *name;
    gint32 hp;
    gint32 gold;
    guint i;

    hero = lrg_save_document_get_section (document, "hero");
    if (hero == NULL || !g_variant_lookup (hero, "name", "&s", &name) ||
        !g_variant_lookup (hero, "hp", "i", &hp) || !g_variant_lookup (hero, "gold", "i", &gold))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the hero is incomplete");
        return FALSE;
    }
    lrg_save_document_set_section (document, "purse",
                                   g_variant_new_parsed ("{'gold': <%i>}", gold), 1);
    lrg_save_document_set_section (document, "stats", g_variant_new_parsed ("{'hp': <%i>}", hp), 1);
    lrg_save_document_set_section (document, "hero", g_variant_new_parsed ("{'name': <%s>}", name),
                                   1);

    /* every remaining unversioned section was at its first layout */
    names = lrg_save_document_get_section_names (document);
    for (i = 0; names[i] != NULL; i++)
    {
        if (lrg_save_document_get_section_version (document, names[i]) == 0)
            lrg_save_document_set_section_version (document, names[i], 1);
    }
    return TRUE;
}

static gboolean
failing_document_step (LrgSaveDocument  *document,
                       gpointer          user_data,
                       GError          **error)
{
    /* changes the working copy first: none of it may reach the caller */
    lrg_save_document_remove_section (document, "hero");
    lrg_save_document_set_section (document, "junk", g_variant_new_int32 (1), 9);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "the document step refuses");
    return FALSE;
}

/* a format 1 document, as the oldest release wrote it */
static LrgSaveDocument *
format_1_document (void)
{
    LrgSaveDocument *document;

    document = lrg_save_document_new (1);
    lrg_save_document_set_section (document, "player",
                                   g_variant_new_parsed ("{'name': <'Ada'>, 'hp': <%i>, "
                                                         "'gold': <%i>}", 42, 300), 0);
    lrg_save_document_set_section (document, "world", g_variant_new_parsed ("{'day': <%i>}", 7),
                                   0);
    lrg_save_document_set_section (document, "mod-notes", g_variant_new_string ("hello"), 0);
    return document;
}

/* the migrator of the latest release: format 3, stats v5, the rest v1 */
static LrgSaveMigrator *
latest_migrator (void)
{
    LrgSaveMigrator *migrator;
    g_autoptr(GError) error = NULL;

    migrator = lrg_save_migrator_new ();
    lrg_save_migrator_set_current_format (migrator, 3);
    lrg_save_migrator_set_section_version (migrator, "hero", 1);
    lrg_save_migrator_set_section_version (migrator, "purse", 1);
    lrg_save_migrator_set_section_version (migrator, "world", 1);
    lrg_save_migrator_set_section_version (migrator, "stats", 5);
    g_assert_true (lrg_save_migrator_add_document_step (migrator, 1, "player is hero", doc_1_to_2,
                                                        NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_document_step (migrator, 2, "versions, split gold",
                                                        doc_2_to_3, NULL, NULL, &error));
    g_assert_no_error (error);
    add_stats_chain (migrator, "converted");
    return migrator;
}

/* ==========================================================================
 * LrgSaveDocument
 * ========================================================================== */

static void
test_document_basics (void)
{
    g_autoptr(LrgSaveDocument) document = NULL;
    g_autoptr(LrgSaveDocument) copy = NULL;
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) names = NULL;

    document = lrg_save_document_new (2);
    g_assert_cmpuint (lrg_save_document_get_format_version (document), ==, 2);
    g_assert_cmpuint (lrg_save_document_get_n_sections (document), ==, 0);

    lrg_save_document_set_section (document, "b", g_variant_new_int32 (2), 3);
    lrg_save_document_set_section (document, "a", g_variant_new_string ("x"), 0);
    g_assert_true (lrg_save_document_has_section (document, "a"));
    g_assert_false (lrg_save_document_has_section (document, "c"));
    g_assert_null (lrg_save_document_get_section (document, "c"));
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "b"), ==, 3);
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "a"), ==, 0);
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "c"), ==, 0);
    names = lrg_save_document_get_section_names (document);
    g_assert_cmpuint (g_strv_length (names), ==, 2);
    g_assert_cmpstr (names[0], ==, "a");
    g_assert_cmpstr (names[1], ==, "b");

    /* a copy is independent */
    copy = lrg_save_document_copy (document);
    g_assert_true (lrg_save_document_equal (document, copy));
    g_assert_true (lrg_save_document_set_section_version (copy, "a", 1));
    g_assert_false (lrg_save_document_set_section_version (copy, "zzz", 1));
    g_assert_false (lrg_save_document_equal (document, copy));
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "a"), ==, 0);

    /* rename keeps state and version; refuses a missing or taken name */
    g_assert_true (lrg_save_document_rename_section (document, "b", "c", &error));
    g_assert_no_error (error);
    g_assert_false (lrg_save_document_has_section (document, "b"));
    g_assert_cmpint (g_variant_get_int32 (lrg_save_document_get_section (document, "c")), ==, 2);
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "c"), ==, 3);
    g_assert_false (lrg_save_document_rename_section (document, "b", "d", &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_false (lrg_save_document_rename_section (document, "a", "c", &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_FAILED);
    g_clear_error (&error);
    g_assert_true (lrg_save_document_rename_section (document, "a", "a", &error));

    g_assert_true (lrg_save_document_remove_section (document, "a"));
    g_assert_false (lrg_save_document_remove_section (document, "a"));
    g_assert_cmpuint (lrg_save_document_get_n_sections (document), ==, 1);
}

static void
test_document_variant (void)
{
    g_autoptr(LrgSaveDocument) document = NULL;
    g_autoptr(LrgSaveDocument) back = NULL;
    g_autoptr(GVariant) variant = NULL;
    g_autoptr(GVariant) bad = NULL;
    g_autoptr(GError) error = NULL;

    document = format_1_document ();
    lrg_save_document_set_section_version (document, "world", 4);
    variant = lrg_save_document_to_variant (document);
    g_assert_cmpstr (g_variant_get_type_string (variant), ==, LRG_SAVE_DOCUMENT_VARIANT_TYPE);
    back = lrg_save_document_new_from_variant (variant, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_save_document_equal (document, back));
    g_assert_cmpuint (lrg_save_document_get_section_version (back, "world"), ==, 4);

    /* malformed documents */
    bad = g_variant_ref_sink (g_variant_new_string ("nope"));
    g_assert_null (lrg_save_document_new_from_variant (bad, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
    g_clear_error (&error);
    g_clear_pointer (&bad, g_variant_unref);

    bad = g_variant_ref_sink (g_variant_new_parsed ("(uint32 0, @a{sv} {}, @a{su} {})"));
    g_assert_null (lrg_save_document_new_from_variant (bad, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
    g_clear_error (&error);
    g_clear_pointer (&bad, g_variant_unref);

    bad = g_variant_ref_sink (g_variant_new_parsed ("(uint32 1, {'a': <1>}, {'b': uint32 1})"));
    g_assert_null (lrg_save_document_new_from_variant (bad, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
    g_assert_nonnull (strstr (error->message, "\"b\""));
    g_clear_error (&error);
    g_clear_pointer (&bad, g_variant_unref);

    bad = g_variant_ref_sink (g_variant_new_parsed ("(uint32 1, {'': <1>}, @a{su} {})"));
    g_assert_null (lrg_save_document_new_from_variant (bad, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
}

/* ==========================================================================
 * Steps and plans
 * ========================================================================== */

static void
count_destroy (gpointer data)
{
    (*(guint *) data)++;
}

static GVariant *
identity_step (GVariant  *state,
               gpointer   user_data,
               GError   **error)
{
    return g_variant_ref (state);
}

static void
test_migration_accessors (void)
{
    g_autoptr(LrgSaveMigration) step = NULL;
    g_autoptr(LrgSaveMigration) doc = NULL;
    g_autofree gchar *text = NULL;
    g_autofree gchar *doc_text = NULL;
    GEnumClass *scope_class;
    guint destroyed = 0;

    step = lrg_save_migration_new_section ("stats", 3, "widen", identity_step, &destroyed,
                                           count_destroy);
    g_assert_cmpint (lrg_save_migration_get_scope (step), ==, LRG_SAVE_MIGRATION_SCOPE_SECTION);
    g_assert_cmpstr (lrg_save_migration_get_section (step), ==, "stats");
    g_assert_cmpuint (lrg_save_migration_get_from_version (step), ==, 3);
    g_assert_cmpuint (lrg_save_migration_get_to_version (step), ==, 4);
    g_assert_cmpstr (lrg_save_migration_get_description (step), ==, "widen");
    g_assert_null (lrg_save_migration_get_output_type (step));
    lrg_save_migration_set_output_type (step, G_VARIANT_TYPE_VARDICT);
    g_assert_true (g_variant_type_equal (lrg_save_migration_get_output_type (step),
                                         G_VARIANT_TYPE_VARDICT));
    text = lrg_save_migration_to_string (step);
    g_assert_cmpstr (text, ==, "section \"stats\" 3 -> 4: widen");

    doc = lrg_save_migration_new_document (1, NULL, doc_1_to_2, NULL, NULL);
    g_assert_null (lrg_save_migration_get_section (doc));
    g_assert_cmpstr (lrg_save_migration_get_description (doc), ==, "");
    doc_text = lrg_save_migration_to_string (doc);
    g_assert_cmpstr (doc_text, ==, "format 1 -> 2");

    /* the enum nicks are stable names */
    scope_class = (GEnumClass *) g_type_class_ref (LRG_TYPE_SAVE_MIGRATION_SCOPE);
    g_assert_cmpstr (g_enum_get_value (scope_class, LRG_SAVE_MIGRATION_SCOPE_SECTION)->value_nick,
                     ==, "section");
    g_assert_cmpstr (g_enum_get_value (scope_class, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT)->value_nick,
                     ==, "document");
    g_type_class_unref (scope_class);

    g_assert_cmpuint (destroyed, ==, 0);
    g_clear_pointer (&step, lrg_save_migration_unref);
    g_assert_cmpuint (destroyed, ==, 1);
}

static void
test_duplicates (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(GError) error = NULL;
    guint destroyed = 0;

    migrator = lrg_save_migrator_new ();
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 1, NULL, identity_step,
                                                       NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "other", 1, NULL, identity_step,
                                                       NULL, NULL, &error));
    g_assert_true (lrg_save_migrator_add_document_step (migrator, 1, NULL, doc_1_to_2, NULL, NULL,
                                                        &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_save_migrator_get_n_steps (migrator), ==, 3);

    /* the same step again is refused, and its data freed */
    g_assert_false (lrg_save_migrator_add_section_step (migrator, "stats", 1, "again",
                                                        identity_step, &destroyed, count_destroy,
                                                        &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_DUPLICATE_STEP);
    g_assert_nonnull (strstr (error->message, "\"stats\" 1 -> 2"));
    g_clear_error (&error);
    g_assert_cmpuint (destroyed, ==, 1);
    g_assert_false (lrg_save_migrator_add_document_step (migrator, 1, NULL, doc_2_to_3, NULL,
                                                         NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_DUPLICATE_STEP);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_save_migrator_get_n_steps (migrator), ==, 3);

    /* lookup, remove */
    g_assert_nonnull (lrg_save_migrator_lookup (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION,
                                                "stats", 1));
    g_assert_null (lrg_save_migrator_lookup (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION,
                                             "stats", 2));
    g_assert_null (lrg_save_migrator_lookup (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION,
                                             "nothing", 1));
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL, 1));
    g_assert_false (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL,
                                              1));
    g_assert_cmpuint (lrg_save_migrator_get_n_steps (migrator), ==, 2);

    /* forgetting a section drops its version and its steps */
    lrg_save_migrator_set_section_version (migrator, "stats", 2);
    g_assert_true (lrg_save_migrator_forget_section (migrator, "stats"));
    g_assert_cmpuint (lrg_save_migrator_get_section_version (migrator, "stats"), ==, 0);
    g_assert_cmpuint (lrg_save_migrator_get_n_steps (migrator), ==, 1);
    g_assert_false (lrg_save_migrator_forget_section (migrator, "stats"));
}

static void
test_plan (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(GPtrArray) chain = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    migrator = lrg_save_migrator_new ();
    add_stats_chain (migrator, "x");

    /* the four steps in order */
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 1, 5,
                                    &error);
    g_assert_no_error (error);
    g_assert_cmpuint (chain->len, ==, 4);
    for (i = 0; i < 4; i++)
        g_assert_cmpuint (lrg_save_migration_get_from_version (g_ptr_array_index (chain, i)), ==,
                          i + 1);
    g_clear_pointer (&chain, g_ptr_array_unref);

    /* part of it, and nothing */
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 3, 5,
                                    &error);
    g_assert_cmpuint (chain->len, ==, 2);
    g_clear_pointer (&chain, g_ptr_array_unref);
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 5, 5,
                                    &error);
    g_assert_cmpuint (chain->len, ==, 0);
    g_clear_pointer (&chain, g_ptr_array_unref);

    /* beyond the last step: the gap is named */
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 2, 7,
                                    &error);
    g_assert_null (chain);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP);
    g_assert_nonnull (strstr (error->message, "from version 5 to 6"));
    g_clear_error (&error);

    /* a hole in the middle */
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats",
                                             3));
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 1, 5,
                                    &error);
    g_assert_null (chain);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP);
    g_assert_nonnull (strstr (error->message, "\"stats\" from version 3 to 4"));
    g_clear_error (&error);

    /* no steps at all for a section, or for the format */
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "nothing", 1, 2,
                                    &error);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP);
    g_clear_error (&error);
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL, 1, 2,
                                    &error);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP);
    g_assert_nonnull (strstr (error->message, "format from version 1 to 2"));
    g_clear_error (&error);

    /* downgrades and version 0 */
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 5, 4,
                                    &error);
    g_assert_null (chain);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH);
    g_clear_error (&error);
    chain = lrg_save_migrator_plan (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats", 0, 4,
                                    &error);
    g_assert_null (chain);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
}

/* ==========================================================================
 * Section conversion
 * ========================================================================== */

/* step-applied handler: records "<from>" or "f<from>" per step */
static void
on_step_applied (LrgSaveMigrator  *migrator,
                 LrgSaveMigration *migration,
                 gpointer          user_data)
{
    GString *log = (GString *) user_data;

    if (log->len > 0)
        g_string_append_c (log, ',');
    if (lrg_save_migration_get_scope (migration) == LRG_SAVE_MIGRATION_SCOPE_DOCUMENT)
        g_string_append_printf (log, "f%u", lrg_save_migration_get_from_version (migration));
    else
        g_string_append_printf (log, "%s%u", lrg_save_migration_get_section (migration),
                                lrg_save_migration_get_from_version (migration));
}

static void
test_section_chain (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GVariant) result = NULL;
    g_autoptr(GVariant) same = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GString) log = NULL;
    gint64 health;
    gint64 mp;
    const gchar *tag;

    migrator = lrg_save_migrator_new ();
    add_stats_chain (migrator, "four steps");
    log = g_string_new (NULL);
    g_signal_connect (migrator, "step-applied", G_CALLBACK (on_step_applied), log);

    /* v1 -> v5 in one call */
    state = g_variant_ref_sink (g_variant_new_parsed ("{'hp': <%i>}", 33));
    result = lrg_save_migrator_migrate_section (migrator, "stats", state, 1, 5, &error);
    g_assert_no_error (error);
    g_assert_nonnull (result);
    g_assert_true (g_variant_lookup (result, "health", "x", &health));
    g_assert_true (g_variant_lookup (result, "mp", "x", &mp));
    g_assert_true (g_variant_lookup (result, "tag", "&s", &tag));
    g_assert_false (g_variant_lookup (result, "hp", "i", NULL));
    g_assert_cmpint (health, ==, 33);
    g_assert_cmpint (mp, ==, 10);
    g_assert_cmpstr (tag, ==, "four steps");
    g_assert_cmpstr (log->str, ==, "stats1,stats2,stats3,stats4");

    /* the input is untouched; an empty chain returns it */
    g_assert_true (g_variant_lookup (state, "hp", "i", NULL));
    same = lrg_save_migrator_migrate_section (migrator, "stats", state, 3, 3, &error);
    g_assert_true (same == state);
}

static void
test_section_failures (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(LrgSaveMigration) typed = NULL;
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GString) log = NULL;
    GVariant *result;

    migrator = lrg_save_migrator_new ();
    log = g_string_new (NULL);
    g_signal_connect (migrator, "step-applied", G_CALLBACK (on_step_applied), log);
    state = g_variant_ref_sink (g_variant_new_parsed ("{'hp': <%i>}", 1));

    /* a failing step mid-chain names itself; nothing is signalled */
    add_stats_chain (migrator, "x");
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats",
                                             3));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 3, "explode",
                                                       failing_section_step, NULL, NULL, &error));
    result = lrg_save_migrator_migrate_section (migrator, "stats", state, 1, 5, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED);
    g_assert_nonnull (strstr (error->message, "\"stats\" 3 -> 4: explode"));
    g_assert_nonnull (strstr (error->message, "this step refuses"));
    g_clear_error (&error);
    g_assert_cmpstr (log->str, ==, "");

    /* a step that fails silently still gives an error */
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "quiet", 1, NULL,
                                                       silent_section_step, NULL, NULL, &error));
    result = lrg_save_migrator_migrate_section (migrator, "quiet", state, 1, 2, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED);
    g_clear_error (&error);

    /* the output type is checked */
    typed = lrg_save_migration_new_section ("typed", 1, "must stay a dictionary",
                                            string_section_step, NULL, NULL);
    lrg_save_migration_set_output_type (typed, G_VARIANT_TYPE_VARDICT);
    g_assert_true (lrg_save_migrator_add (migrator, typed, &error));
    result = lrg_save_migrator_migrate_section (migrator, "typed", state, 1, 2, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED);
    g_assert_nonnull (strstr (error->message, "expected a{sv}"));
    g_clear_error (&error);

    /* downgrade */
    result = lrg_save_migrator_migrate_section (migrator, "typed", state, 3, 2, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH);
}

/* ==========================================================================
 * Document conversion
 * ========================================================================== */

static void
test_document_chain (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(LrgSaveDocument) document = NULL;
    g_autoptr(GPtrArray) applied = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GString) log = NULL;
    g_auto(GStrv) names = NULL;
    GVariant *section;
    const gchar *text;
    gint32 value;
    gint64 health;

    migrator = latest_migrator ();
    log = g_string_new (NULL);
    g_signal_connect (migrator, "step-applied", G_CALLBACK (on_step_applied), log);
    document = format_1_document ();
    g_assert_true (lrg_save_migrator_needs_migration (migrator, document));

    /* format 1 -> 3, then stats 1 -> 5: six steps in one call */
    g_assert_true (lrg_save_migrator_migrate_document (migrator, document, &applied, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (applied->len, ==, 6);
    g_assert_cmpstr (log->str, ==, "f1,f2,stats1,stats2,stats3,stats4");
    g_assert_cmpuint (lrg_save_document_get_format_version (document), ==, 3);
    g_assert_false (lrg_save_migrator_needs_migration (migrator, document));

    /* the renamed and split sections */
    names = lrg_save_document_get_section_names (document);
    g_assert_cmpuint (g_strv_length (names), ==, 5);
    g_assert_false (lrg_save_document_has_section (document, "player"));
    section = lrg_save_document_get_section (document, "hero");
    g_assert_true (g_variant_lookup (section, "name", "&s", &text));
    g_assert_cmpstr (text, ==, "Ada");
    g_assert_false (g_variant_lookup (section, "gold", "i", NULL));
    section = lrg_save_document_get_section (document, "purse");
    g_assert_true (g_variant_lookup (section, "gold", "i", &value));
    g_assert_cmpint (value, ==, 300);
    section = lrg_save_document_get_section (document, "stats");
    g_assert_true (g_variant_lookup (section, "health", "x", &health));
    g_assert_cmpint (health, ==, 42);
    g_assert_true (g_variant_lookup (section, "tag", "&s", &text));
    g_assert_cmpstr (text, ==, "converted");
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "stats"), ==, 5);
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "hero"), ==, 1);
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "world"), ==, 1);

    /* the unknown (mod) section is kept as it is */
    g_assert_cmpstr (g_variant_get_string (lrg_save_document_get_section (document, "mod-notes"),
                                           NULL), ==, "hello");
    g_assert_cmpuint (lrg_save_document_get_section_version (document, "mod-notes"), ==, 1);

    /* converting a current document applies nothing */
    g_clear_pointer (&applied, g_ptr_array_unref);
    g_string_truncate (log, 0);
    g_assert_true (lrg_save_migrator_migrate_document (migrator, document, &applied, &error));
    g_assert_cmpuint (applied->len, ==, 0);
    g_assert_cmpstr (log->str, ==, "");
}

/* a document written at an intermediate release converts from there */
static void
test_document_partial (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(LrgSaveDocument) document = NULL;
    g_autoptr(GPtrArray) applied = NULL;
    g_autoptr(GError) error = NULL;
    gint64 health;

    migrator = latest_migrator ();
    document = lrg_save_document_new (3);
    lrg_save_document_set_section (document, "hero", g_variant_new_parsed ("{'name': <'Bo'>}"), 1);
    lrg_save_document_set_section (document, "stats",
                                   g_variant_new_parsed ("{'health': <%i>, 'mp': <%i>}", 5, 6), 3);
    g_assert_true (lrg_save_migrator_migrate_document (migrator, document, &applied, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (applied->len, ==, 2);
    g_assert_true (g_variant_lookup (lrg_save_document_get_section (document, "stats"), "health",
                                     "x", &health));
    g_assert_cmpint (health, ==, 5);
}

static void
test_document_atomicity (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(LrgSaveDocument) document = NULL;
    g_autoptr(LrgSaveDocument) before = NULL;
    g_autoptr(GPtrArray) applied = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GString) log = NULL;

    migrator = latest_migrator ();
    log = g_string_new (NULL);
    g_signal_connect (migrator, "step-applied", G_CALLBACK (on_step_applied), log);
    document = format_1_document ();
    before = lrg_save_document_copy (document);

    /* a section step fails after both document steps succeeded */
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_SECTION, "stats",
                                             4));
    g_assert_true (lrg_save_migrator_add_section_step (migrator, "stats", 4, "explode",
                                                       failing_section_step, NULL, NULL, &error));
    applied = (GPtrArray *) 0x1;
    g_assert_false (lrg_save_migrator_migrate_document (migrator, document, &applied, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED);
    g_assert_nonnull (strstr (error->message, "\"stats\" 4 -> 5: explode"));
    g_clear_error (&error);
    g_assert_null (applied);
    g_assert_true (lrg_save_document_equal (document, before));
    g_assert_cmpstr (log->str, ==, "");

    /* a document step fails after changing its working copy */
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL, 2));
    g_assert_true (lrg_save_migrator_add_document_step (migrator, 2, "explode",
                                                        failing_document_step, NULL, NULL,
                                                        &error));
    g_assert_false (lrg_save_migrator_migrate_document (migrator, document, NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED);
    g_assert_nonnull (strstr (error->message, "format 2 -> 3: explode"));
    g_assert_nonnull (strstr (error->message, "the document step refuses"));
    g_clear_error (&error);
    g_assert_true (lrg_save_document_equal (document, before));

    /* a gap in the format chain */
    g_assert_true (lrg_save_migrator_remove (migrator, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL, 2));
    g_assert_false (lrg_save_migrator_migrate_document (migrator, document, NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP);
    g_clear_error (&error);
    g_assert_true (lrg_save_document_equal (document, before));
    g_assert_cmpstr (log->str, ==, "");
}

static void
test_document_refusals (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_autoptr(LrgSaveDocument) newer = NULL;
    g_autoptr(LrgSaveDocument) newer_section = NULL;
    g_autoptr(LrgSaveDocument) unversioned = NULL;
    g_autoptr(LrgSaveDocument) before = NULL;
    g_autoptr(GError) error = NULL;

    migrator = latest_migrator ();

    /* a file of a later release (format 4) */
    newer = lrg_save_document_new (4);
    lrg_save_document_set_section (newer, "hero", g_variant_new_parsed ("{'name': <'Cy'>}"), 1);
    before = lrg_save_document_copy (newer);
    g_assert_false (lrg_save_migrator_migrate_document (migrator, newer, NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH);
    g_assert_nonnull (strstr (error->message, "format is at version 4"));
    g_clear_error (&error);
    g_assert_true (lrg_save_document_equal (newer, before));
    g_clear_pointer (&before, lrg_save_document_unref);

    /* a section of a later release */
    newer_section = lrg_save_document_new (3);
    lrg_save_document_set_section (newer_section, "stats", g_variant_new_parsed ("@a{sv} {}"), 6);
    before = lrg_save_document_copy (newer_section);
    g_assert_false (lrg_save_migrator_migrate_document (migrator, newer_section, NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH);
    g_assert_nonnull (strstr (error->message, "\"stats\""));
    g_clear_error (&error);
    g_assert_true (lrg_save_document_equal (newer_section, before));

    /* a known section without a version after the document steps */
    unversioned = lrg_save_document_new (3);
    lrg_save_document_set_section (unversioned, "world", g_variant_new_parsed ("@a{sv} {}"), 0);
    g_assert_false (lrg_save_migrator_migrate_document (migrator, unversioned, NULL, &error));
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT);
    g_assert_nonnull (strstr (error->message, "\"world\" has no version"));
}

static void
test_current_format_property (void)
{
    g_autoptr(LrgSaveMigrator) migrator = NULL;
    g_auto(GStrv) sections = NULL;
    guint format;

    migrator = lrg_save_migrator_new ();
    g_assert_cmpuint (lrg_save_migrator_get_current_format (migrator), ==, 1);
    g_object_set (migrator, "current-format", 7, NULL);
    g_object_get (migrator, "current-format", &format, NULL);
    g_assert_cmpuint (format, ==, 7);

    lrg_save_migrator_set_section_version (migrator, "b", 2);
    lrg_save_migrator_set_section_version (migrator, "a", 1);
    sections = lrg_save_migrator_get_sections (migrator);
    g_assert_cmpuint (g_strv_length (sections), ==, 2);
    g_assert_cmpstr (sections[0], ==, "a");
    g_assert_cmpuint (lrg_save_migrator_get_section_version (migrator, "b"), ==, 2);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/save-migrator/document/basics", test_document_basics);
    g_test_add_func ("/save-migrator/document/variant", test_document_variant);
    g_test_add_func ("/save-migrator/migration/accessors", test_migration_accessors);
    g_test_add_func ("/save-migrator/duplicates", test_duplicates);
    g_test_add_func ("/save-migrator/plan", test_plan);
    g_test_add_func ("/save-migrator/section/chain", test_section_chain);
    g_test_add_func ("/save-migrator/section/failures", test_section_failures);
    g_test_add_func ("/save-migrator/document/chain", test_document_chain);
    g_test_add_func ("/save-migrator/document/partial", test_document_partial);
    g_test_add_func ("/save-migrator/document/atomicity", test_document_atomicity);
    g_test_add_func ("/save-migrator/document/refusals", test_document_refusals);
    g_test_add_func ("/save-migrator/current-format", test_current_format_property);

    return g_test_run ();
}
