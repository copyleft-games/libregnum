/* test-save.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the save module.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <gio/gio.h>

/* ==========================================================================
 * Test Saveable Implementation
 * ========================================================================== */

#define TEST_TYPE_SAVEABLE_OBJECT (test_saveable_object_get_type ())
G_DECLARE_FINAL_TYPE (TestSaveableObject, test_saveable_object, TEST, SAVEABLE_OBJECT, GObject)

struct _TestSaveableObject
{
    GObject  parent_instance;
    gchar   *name;
    gint     score;
    gdouble  health;
    gboolean alive;
};

static void test_saveable_iface_init (LrgSaveableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (TestSaveableObject, test_saveable_object, G_TYPE_OBJECT,
                                G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                       test_saveable_iface_init))

static void
test_saveable_object_finalize (GObject *object)
{
    TestSaveableObject *self = TEST_SAVEABLE_OBJECT (object);

    g_free (self->name);

    G_OBJECT_CLASS (test_saveable_object_parent_class)->finalize (object);
}

static void
test_saveable_object_class_init (TestSaveableObjectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_saveable_object_finalize;
}

static void
test_saveable_object_init (TestSaveableObject *self)
{
    self->name = g_strdup ("Default");
    self->score = 0;
    self->health = 100.0;
    self->alive = TRUE;
}

static const gchar *
test_saveable_get_save_id (LrgSaveable *saveable)
{
    return "test-object";
}

static gboolean
test_saveable_save (LrgSaveable     *saveable,
                    LrgSaveContext  *context,
                    GError         **error)
{
    TestSaveableObject *self = TEST_SAVEABLE_OBJECT (saveable);

    lrg_save_context_write_string (context, "name", self->name);
    lrg_save_context_write_int (context, "score", self->score);
    lrg_save_context_write_double (context, "health", self->health);
    lrg_save_context_write_boolean (context, "alive", self->alive);

    return TRUE;
}

static gboolean
test_saveable_load (LrgSaveable     *saveable,
                    LrgSaveContext  *context,
                    GError         **error)
{
    TestSaveableObject *self = TEST_SAVEABLE_OBJECT (saveable);
    const gchar        *name;

    name = lrg_save_context_read_string (context, "name", "Unknown");
    g_free (self->name);
    self->name = g_strdup (name);

    self->score = (gint) lrg_save_context_read_int (context, "score", 0);
    self->health = lrg_save_context_read_double (context, "health", 100.0);
    self->alive = lrg_save_context_read_boolean (context, "alive", TRUE);

    return TRUE;
}

static void
test_saveable_iface_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = test_saveable_get_save_id;
    iface->save = test_saveable_save;
    iface->load = test_saveable_load;
}

static TestSaveableObject *
test_saveable_object_new (void)
{
    return g_object_new (TEST_TYPE_SAVEABLE_OBJECT, NULL);
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgSaveContext *context;
} SaveContextFixture;

typedef struct
{
    LrgSaveManager     *manager;
    TestSaveableObject *object;
    gchar              *temp_dir;
} SaveManagerFixture;

static void
save_context_fixture_setup (SaveContextFixture *fixture,
                            gconstpointer       user_data)
{
    fixture->context = lrg_save_context_new_for_save ();
}

static void
save_context_fixture_teardown (SaveContextFixture *fixture,
                               gconstpointer       user_data)
{
    g_clear_object (&fixture->context);
}

static void
save_manager_fixture_setup (SaveManagerFixture *fixture,
                            gconstpointer       user_data)
{
    fixture->manager = lrg_save_manager_new ();
    fixture->object = test_saveable_object_new ();
    fixture->temp_dir = g_dir_make_tmp ("libregnum-save-test-XXXXXX", NULL);

    lrg_save_manager_set_save_directory (fixture->manager, fixture->temp_dir);
}

static void
save_manager_fixture_teardown (SaveManagerFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(GDir) dir = NULL;
    const gchar    *filename;

    /* Clean up temp files */
    if (fixture->temp_dir != NULL)
    {
        dir = g_dir_open (fixture->temp_dir, 0, NULL);
        if (dir != NULL)
        {
            while ((filename = g_dir_read_name (dir)) != NULL)
            {
                g_autofree gchar *path = g_build_filename (fixture->temp_dir,
                                                            filename, NULL);
                g_remove (path);
            }
        }
        g_rmdir (fixture->temp_dir);
        g_free (fixture->temp_dir);
    }

    g_clear_object (&fixture->object);
    g_clear_object (&fixture->manager);
}

/* ==========================================================================
 * Test Cases - Save Context
 * ========================================================================== */

static void
test_save_context_new_for_save (SaveContextFixture *fixture,
                                gconstpointer       user_data)
{
    g_assert_nonnull (fixture->context);
    g_assert_cmpint (lrg_save_context_get_mode (fixture->context), ==,
                     LRG_SAVE_CONTEXT_MODE_SAVE);
}

static void
test_save_context_version (SaveContextFixture *fixture,
                           gconstpointer       user_data)
{
    g_assert_cmpuint (lrg_save_context_get_version (fixture->context), ==, 1);

    lrg_save_context_set_version (fixture->context, 5);
    g_assert_cmpuint (lrg_save_context_get_version (fixture->context), ==, 5);
}

static void
test_save_context_write_read_roundtrip (void)
{
    g_autoptr(LrgSaveContext) save_ctx = NULL;
    g_autoptr(LrgSaveContext) load_ctx = NULL;
    g_autofree gchar         *yaml_str = NULL;
    g_autoptr(GError)         error = NULL;

    /* Create save context and write values */
    save_ctx = lrg_save_context_new_for_save ();
    lrg_save_context_begin_section (save_ctx, "player");
    lrg_save_context_write_string (save_ctx, "name", "Hero");
    lrg_save_context_write_int (save_ctx, "level", 42);
    lrg_save_context_write_double (save_ctx, "experience", 1234.56);
    lrg_save_context_write_boolean (save_ctx, "is_active", TRUE);
    lrg_save_context_end_section (save_ctx);

    /* Generate YAML string */
    yaml_str = lrg_save_context_to_string (save_ctx, &error);
    g_assert_no_error (error);
    g_assert_nonnull (yaml_str);

    /* Load from the generated YAML */
    load_ctx = lrg_save_context_new_for_load (yaml_str, &error);
    g_assert_no_error (error);
    g_assert_nonnull (load_ctx);

    /* Verify we can enter the section and read values back */
    g_assert_true (lrg_save_context_has_section (load_ctx, "player"));
    g_assert_true (lrg_save_context_enter_section (load_ctx, "player"));

    g_assert_cmpstr (lrg_save_context_read_string (load_ctx, "name", NULL), ==, "Hero");
    g_assert_cmpint (lrg_save_context_read_int (load_ctx, "level", 0), ==, 42);
    g_assert_cmpfloat_with_epsilon (lrg_save_context_read_double (load_ctx, "experience", 0.0),
                                    1234.56, 0.01);
    g_assert_true (lrg_save_context_read_boolean (load_ctx, "is_active", FALSE));

    lrg_save_context_leave_section (load_ctx);
}

static void
test_save_context_default_values (void)
{
    g_autoptr(LrgSaveContext) save_ctx = NULL;
    g_autoptr(LrgSaveContext) load_ctx = NULL;
    g_autofree gchar         *yaml_str = NULL;
    g_autoptr(GError)         error = NULL;

    /* Create minimal save context */
    save_ctx = lrg_save_context_new_for_save ();
    lrg_save_context_begin_section (save_ctx, "empty");
    lrg_save_context_end_section (save_ctx);

    yaml_str = lrg_save_context_to_string (save_ctx, &error);
    g_assert_no_error (error);

    load_ctx = lrg_save_context_new_for_load (yaml_str, &error);
    g_assert_no_error (error);

    g_assert_true (lrg_save_context_enter_section (load_ctx, "empty"));

    /* Check that missing keys return defaults */
    g_assert_cmpstr (lrg_save_context_read_string (load_ctx, "missing", "default"), ==, "default");
    g_assert_cmpint (lrg_save_context_read_int (load_ctx, "missing", 99), ==, 99);
    g_assert_cmpfloat (lrg_save_context_read_double (load_ctx, "missing", 3.14), ==, 3.14);
    g_assert_false (lrg_save_context_read_boolean (load_ctx, "missing", FALSE));

    lrg_save_context_leave_section (load_ctx);
}

static void
test_save_context_has_key (void)
{
    g_autoptr(LrgSaveContext) save_ctx = NULL;
    g_autoptr(LrgSaveContext) load_ctx = NULL;
    g_autofree gchar         *yaml_str = NULL;
    g_autoptr(GError)         error = NULL;

    save_ctx = lrg_save_context_new_for_save ();
    lrg_save_context_begin_section (save_ctx, "data");
    lrg_save_context_write_string (save_ctx, "exists", "value");
    lrg_save_context_end_section (save_ctx);

    yaml_str = lrg_save_context_to_string (save_ctx, &error);
    g_assert_no_error (error);

    load_ctx = lrg_save_context_new_for_load (yaml_str, &error);
    g_assert_no_error (error);

    g_assert_true (lrg_save_context_enter_section (load_ctx, "data"));
    g_assert_true (lrg_save_context_has_key (load_ctx, "exists"));
    g_assert_false (lrg_save_context_has_key (load_ctx, "not_exists"));
    lrg_save_context_leave_section (load_ctx);
}

/* ==========================================================================
 * Test Cases - Save Game
 * ========================================================================== */

static void
test_save_game_new (void)
{
    g_autoptr(LrgSaveGame) save = NULL;

    save = lrg_save_game_new ("slot1");
    g_assert_nonnull (save);
    g_assert_cmpstr (lrg_save_game_get_slot_name (save), ==, "slot1");
}

static void
test_save_game_display_name (void)
{
    g_autoptr(LrgSaveGame) save = NULL;

    save = lrg_save_game_new ("slot1");

    g_assert_null (lrg_save_game_get_display_name (save));

    lrg_save_game_set_display_name (save, "My Save Game");
    g_assert_cmpstr (lrg_save_game_get_display_name (save), ==, "My Save Game");
}

static void
test_save_game_timestamp (void)
{
    g_autoptr(LrgSaveGame) save = NULL;
    GDateTime             *ts;

    save = lrg_save_game_new ("slot1");

    g_assert_null (lrg_save_game_get_timestamp (save));

    lrg_save_game_update_timestamp (save);
    ts = lrg_save_game_get_timestamp (save);
    g_assert_nonnull (ts);
}

static void
test_save_game_playtime (void)
{
    g_autoptr(LrgSaveGame) save = NULL;

    save = lrg_save_game_new ("slot1");

    g_assert_cmpfloat (lrg_save_game_get_playtime (save), ==, 0.0);

    lrg_save_game_set_playtime (save, 3600.0);
    g_assert_cmpfloat (lrg_save_game_get_playtime (save), ==, 3600.0);

    lrg_save_game_add_playtime (save, 60.0);
    g_assert_cmpfloat (lrg_save_game_get_playtime (save), ==, 3660.0);
}

static void
test_save_game_custom_data (void)
{
    g_autoptr(LrgSaveGame) save = NULL;

    save = lrg_save_game_new ("slot1");

    /* String custom data */
    g_assert_null (lrg_save_game_get_custom_string (save, "level"));
    lrg_save_game_set_custom_string (save, "level", "Dungeon 5");
    g_assert_cmpstr (lrg_save_game_get_custom_string (save, "level"), ==, "Dungeon 5");

    /* Integer custom data */
    g_assert_cmpint (lrg_save_game_get_custom_int (save, "coins", 0), ==, 0);
    lrg_save_game_set_custom_int (save, "coins", 500);
    g_assert_cmpint (lrg_save_game_get_custom_int (save, "coins", 0), ==, 500);
}

/* ==========================================================================
 * Test Cases - Save Manager
 * ========================================================================== */

static void
test_save_manager_new (SaveManagerFixture *fixture,
                       gconstpointer       user_data)
{
    g_assert_nonnull (fixture->manager);
}

static void
test_save_manager_save_directory (SaveManagerFixture *fixture,
                                  gconstpointer       user_data)
{
    g_assert_cmpstr (lrg_save_manager_get_save_directory (fixture->manager),
                     ==, fixture->temp_dir);
}

static void
test_save_manager_register (SaveManagerFixture *fixture,
                            gconstpointer       user_data)
{
    /* Register should not crash */
    lrg_save_manager_register (fixture->manager, LRG_SAVEABLE (fixture->object));

    /* Unregister should work */
    lrg_save_manager_unregister (fixture->manager, LRG_SAVEABLE (fixture->object));
}

static void
test_save_manager_save_load (SaveManagerFixture *fixture,
                             gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    /* Set up test data */
    g_free (fixture->object->name);
    fixture->object->name = g_strdup ("TestPlayer");
    fixture->object->score = 9999;
    fixture->object->health = 75.5;
    fixture->object->alive = TRUE;

    /* Register and save */
    lrg_save_manager_register (fixture->manager, LRG_SAVEABLE (fixture->object));

    result = lrg_save_manager_save (fixture->manager, "test-slot", &error);
    g_assert_no_error (error);
    g_assert_true (result);

    /* Verify slot exists */
    g_assert_true (lrg_save_manager_slot_exists (fixture->manager, "test-slot"));

    /* Modify the object */
    g_free (fixture->object->name);
    fixture->object->name = g_strdup ("Modified");
    fixture->object->score = 0;
    fixture->object->health = 0.0;
    fixture->object->alive = FALSE;

    /* Load and verify restoration */
    result = lrg_save_manager_load (fixture->manager, "test-slot", &error);
    g_assert_no_error (error);
    g_assert_true (result);

    g_assert_cmpstr (fixture->object->name, ==, "TestPlayer");
    g_assert_cmpint (fixture->object->score, ==, 9999);
    g_assert_cmpfloat_with_epsilon (fixture->object->health, 75.5, 0.01);
    g_assert_true (fixture->object->alive);
}

static void
test_save_manager_slot_not_found (SaveManagerFixture *fixture,
                                  gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    result = lrg_save_manager_load (fixture->manager, "nonexistent", &error);
    g_assert_false (result);
    g_assert_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND);
}

static void
test_save_manager_delete_save (SaveManagerFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    /* Register and save first */
    lrg_save_manager_register (fixture->manager, LRG_SAVEABLE (fixture->object));
    result = lrg_save_manager_save (fixture->manager, "to-delete", &error);
    g_assert_no_error (error);
    g_assert_true (result);

    g_assert_true (lrg_save_manager_slot_exists (fixture->manager, "to-delete"));

    /* Delete */
    result = lrg_save_manager_delete_save (fixture->manager, "to-delete", &error);
    g_assert_no_error (error);
    g_assert_true (result);

    g_assert_false (lrg_save_manager_slot_exists (fixture->manager, "to-delete"));
}

static void
test_save_manager_list_saves (SaveManagerFixture *fixture,
                              gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    GList            *saves;

    lrg_save_manager_register (fixture->manager, LRG_SAVEABLE (fixture->object));

    /* Save to multiple slots */
    lrg_save_manager_save (fixture->manager, "slot1", &error);
    lrg_save_manager_save (fixture->manager, "slot2", &error);
    lrg_save_manager_save (fixture->manager, "slot3", &error);

    saves = lrg_save_manager_list_saves (fixture->manager);
    g_assert_cmpuint (g_list_length (saves), ==, 3);

    g_list_free_full (saves, g_object_unref);
}

static void
test_save_manager_get_save (SaveManagerFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(GError)     error = NULL;
    g_autoptr(LrgSaveGame) save = NULL;

    lrg_save_manager_register (fixture->manager, LRG_SAVEABLE (fixture->object));
    lrg_save_manager_save (fixture->manager, "my-save", &error);

    save = lrg_save_manager_get_save (fixture->manager, "my-save");
    g_assert_nonnull (save);
    g_assert_cmpstr (lrg_save_game_get_slot_name (save), ==, "my-save");
}

/* ==========================================================================
 * Test Cases - Saveable Interface
 * ========================================================================== */

static void
test_saveable_interface (void)
{
    g_autoptr(TestSaveableObject) obj = NULL;

    obj = test_saveable_object_new ();

    g_assert_true (LRG_IS_SAVEABLE (obj));
    g_assert_cmpstr (lrg_saveable_get_save_id (LRG_SAVEABLE (obj)), ==, "test-object");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Save Context tests */
    g_test_add ("/save/context/new-for-save",
                SaveContextFixture, NULL,
                save_context_fixture_setup,
                test_save_context_new_for_save,
                save_context_fixture_teardown);

    g_test_add ("/save/context/version",
                SaveContextFixture, NULL,
                save_context_fixture_setup,
                test_save_context_version,
                save_context_fixture_teardown);

    g_test_add_func ("/save/context/write-read-roundtrip",
                     test_save_context_write_read_roundtrip);

    g_test_add_func ("/save/context/default-values",
                     test_save_context_default_values);

    g_test_add_func ("/save/context/has-key",
                     test_save_context_has_key);

    /* Save Game tests */
    g_test_add_func ("/save/game/new", test_save_game_new);
    g_test_add_func ("/save/game/display-name", test_save_game_display_name);
    g_test_add_func ("/save/game/timestamp", test_save_game_timestamp);
    g_test_add_func ("/save/game/playtime", test_save_game_playtime);
    g_test_add_func ("/save/game/custom-data", test_save_game_custom_data);

    /* Save Manager tests */
    g_test_add ("/save/manager/new",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_new,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/save-directory",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_save_directory,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/register",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_register,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/save-load",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_save_load,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/slot-not-found",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_slot_not_found,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/delete-save",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_delete_save,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/list-saves",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_list_saves,
                save_manager_fixture_teardown);

    g_test_add ("/save/manager/get-save",
                SaveManagerFixture, NULL,
                save_manager_fixture_setup,
                test_save_manager_get_save,
                save_manager_fixture_teardown);

    /* Saveable Interface tests */
    g_test_add_func ("/save/interface/basic", test_saveable_interface);

    return g_test_run ();
}
