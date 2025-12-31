/* test-input.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for Input module (Binding, Action, Map).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures - Action
 * ========================================================================== */

typedef struct
{
    LrgInputAction *action;
} ActionFixture;

static void
action_fixture_set_up (ActionFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->action = lrg_input_action_new ("test_action");
    g_assert_nonnull (fixture->action);
}

static void
action_fixture_tear_down (ActionFixture *fixture,
                          gconstpointer  user_data)
{
    g_clear_object (&fixture->action);
}

/* ==========================================================================
 * Test Fixtures - Map
 * ========================================================================== */

typedef struct
{
    LrgInputMap *map;
} MapFixture;

static void
map_fixture_set_up (MapFixture    *fixture,
                    gconstpointer  user_data)
{
    fixture->map = lrg_input_map_new ();
    g_assert_nonnull (fixture->map);
}

static void
map_fixture_tear_down (MapFixture    *fixture,
                       gconstpointer  user_data)
{
    g_clear_object (&fixture->map);
}

/* ==========================================================================
 * Test Cases - Binding
 * ========================================================================== */

static void
test_binding_new_keyboard (void)
{
    LrgInputBinding *binding;

    binding = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_binding_type (binding), ==,
                     LRG_INPUT_BINDING_KEYBOARD);
    g_assert_cmpint (lrg_input_binding_get_key (binding), ==, GRL_KEY_SPACE);
    g_assert_cmpint (lrg_input_binding_get_modifiers (binding), ==,
                     LRG_INPUT_MODIFIER_NONE);

    lrg_input_binding_free (binding);
}

static void
test_binding_new_keyboard_with_modifiers (void)
{
    LrgInputBinding  *binding;
    LrgInputModifiers mods;

    mods = LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL;
    binding = lrg_input_binding_new_keyboard (GRL_KEY_A, mods);

    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_key (binding), ==, GRL_KEY_A);
    g_assert_cmpint (lrg_input_binding_get_modifiers (binding), ==, mods);

    lrg_input_binding_free (binding);
}

static void
test_binding_new_mouse_button (void)
{
    LrgInputBinding *binding;

    binding = lrg_input_binding_new_mouse_button (GRL_MOUSE_BUTTON_LEFT,
                                                  LRG_INPUT_MODIFIER_NONE);
    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_binding_type (binding), ==,
                     LRG_INPUT_BINDING_MOUSE_BUTTON);
    g_assert_cmpint (lrg_input_binding_get_mouse_button (binding), ==,
                     GRL_MOUSE_BUTTON_LEFT);

    lrg_input_binding_free (binding);
}

static void
test_binding_new_gamepad_button (void)
{
    LrgInputBinding *binding;

    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_binding_type (binding), ==,
                     LRG_INPUT_BINDING_GAMEPAD_BUTTON);
    g_assert_cmpint (lrg_input_binding_get_gamepad (binding), ==, 0);
    g_assert_cmpint (lrg_input_binding_get_gamepad_button (binding), ==,
                     GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

    lrg_input_binding_free (binding);
}

static void
test_binding_new_gamepad_axis (void)
{
    LrgInputBinding *binding;

    binding = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_X, 0.5f, TRUE);
    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_binding_type (binding), ==,
                     LRG_INPUT_BINDING_GAMEPAD_AXIS);
    g_assert_cmpint (lrg_input_binding_get_gamepad (binding), ==, 0);
    g_assert_cmpint (lrg_input_binding_get_gamepad_axis (binding), ==,
                     GRL_GAMEPAD_AXIS_LEFT_X);
    g_assert_cmpfloat_with_epsilon (lrg_input_binding_get_threshold (binding),
                                    0.5f, 0.0001f);
    g_assert_true (lrg_input_binding_get_positive (binding));

    lrg_input_binding_free (binding);
}

static void
test_binding_new_gamepad_axis_negative (void)
{
    LrgInputBinding *binding;

    binding = lrg_input_binding_new_gamepad_axis (1, GRL_GAMEPAD_AXIS_LEFT_Y, 0.3f, FALSE);
    g_assert_nonnull (binding);
    g_assert_cmpint (lrg_input_binding_get_gamepad (binding), ==, 1);
    g_assert_cmpfloat_with_epsilon (lrg_input_binding_get_threshold (binding),
                                    0.3f, 0.0001f);
    g_assert_false (lrg_input_binding_get_positive (binding));

    lrg_input_binding_free (binding);
}

static void
test_binding_copy (void)
{
    LrgInputBinding *original;
    LrgInputBinding *copy;

    original = lrg_input_binding_new_keyboard (GRL_KEY_W, LRG_INPUT_MODIFIER_SHIFT);
    copy = lrg_input_binding_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (original != copy);
    g_assert_cmpint (lrg_input_binding_get_binding_type (copy), ==,
                     lrg_input_binding_get_binding_type (original));
    g_assert_cmpint (lrg_input_binding_get_key (copy), ==,
                     lrg_input_binding_get_key (original));
    g_assert_cmpint (lrg_input_binding_get_modifiers (copy), ==,
                     lrg_input_binding_get_modifiers (original));

    lrg_input_binding_free (original);
    lrg_input_binding_free (copy);
}

static void
test_binding_to_string_keyboard (void)
{
    LrgInputBinding *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    str = lrg_input_binding_to_string (binding);

    g_assert_nonnull (str);
    /* The function returns human-readable format */
    g_assert_cmpint (g_ascii_strcasecmp (str, "SPACE"), ==, 0);

    lrg_input_binding_free (binding);
}

static void
test_binding_to_string_keyboard_with_modifiers (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_keyboard (GRL_KEY_A,
                                              LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
    str = lrg_input_binding_to_string (binding);

    g_assert_nonnull (str);
    /* Should contain modifier names and key */
    g_assert_true (g_str_has_suffix (str, "A"));

    lrg_input_binding_free (binding);
}

static void
test_binding_to_string_mouse (void)
{
    LrgInputBinding *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_mouse_button (GRL_MOUSE_BUTTON_RIGHT,
                                                  LRG_INPUT_MODIFIER_NONE);
    str = lrg_input_binding_to_string (binding);

    g_assert_nonnull (str);
    /* The function returns human-readable format like "RightMouse" */
    g_assert_true (g_str_has_prefix (str, "Right") || strstr (str, "Mouse") != NULL);

    lrg_input_binding_free (binding);
}

static void
test_binding_to_string_gamepad_button (void)
{
    LrgInputBinding *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    str = lrg_input_binding_to_string (binding);

    g_assert_nonnull (str);
    /* Should contain gamepad info - format is "Gamepad0 A" */
    g_assert_true (strstr (str, "Gamepad") != NULL);

    lrg_input_binding_free (binding);
}

static void
test_binding_to_string_gamepad_axis (void)
{
    LrgInputBinding *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_X, 0.5f, TRUE);
    str = lrg_input_binding_to_string (binding);

    g_assert_nonnull (str);
    /* Should contain gamepad and direction info - format is "Gamepad0 LeftX+" */
    g_assert_true (strstr (str, "Gamepad") != NULL);
    g_assert_true (strstr (str, "+") != NULL);

    lrg_input_binding_free (binding);
}

/* ==========================================================================
 * Test Cases - Action
 * ========================================================================== */

static void
test_action_new (void)
{
    g_autoptr(LrgInputAction) action = NULL;

    action = lrg_input_action_new ("jump");

    g_assert_nonnull (action);
    g_assert_true (LRG_IS_INPUT_ACTION (action));
    g_assert_cmpstr (lrg_input_action_get_name (action), ==, "jump");
    g_assert_cmpuint (lrg_input_action_get_binding_count (action), ==, 0);
}

static void
test_action_add_binding (ActionFixture *fixture,
                         gconstpointer  user_data)
{
    LrgInputBinding       *binding;
    const LrgInputBinding *retrieved;

    binding = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    lrg_input_action_add_binding (fixture->action, binding);
    lrg_input_binding_free (binding); /* Action copies the binding */

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 1);

    retrieved = lrg_input_action_get_binding (fixture->action, 0);
    g_assert_nonnull (retrieved);
    g_assert_cmpint (lrg_input_binding_get_key (retrieved), ==, GRL_KEY_SPACE);
}

static void
test_action_add_multiple_bindings (ActionFixture *fixture,
                                   gconstpointer  user_data)
{
    LrgInputBinding *binding1;
    LrgInputBinding *binding2;
    LrgInputBinding *binding3;

    binding1 = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    binding2 = lrg_input_binding_new_keyboard (GRL_KEY_W, LRG_INPUT_MODIFIER_NONE);
    binding3 = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

    lrg_input_action_add_binding (fixture->action, binding1);
    lrg_input_action_add_binding (fixture->action, binding2);
    lrg_input_action_add_binding (fixture->action, binding3);

    lrg_input_binding_free (binding1);
    lrg_input_binding_free (binding2);
    lrg_input_binding_free (binding3);

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 3);

    g_assert_cmpint (lrg_input_binding_get_key (lrg_input_action_get_binding (fixture->action, 0)),
                     ==, GRL_KEY_SPACE);
    g_assert_cmpint (lrg_input_binding_get_key (lrg_input_action_get_binding (fixture->action, 1)),
                     ==, GRL_KEY_W);
    g_assert_cmpint (lrg_input_binding_get_binding_type (lrg_input_action_get_binding (fixture->action, 2)),
                     ==, LRG_INPUT_BINDING_GAMEPAD_BUTTON);
}

static void
test_action_remove_binding (ActionFixture *fixture,
                            gconstpointer  user_data)
{
    LrgInputBinding *binding1;
    LrgInputBinding *binding2;

    binding1 = lrg_input_binding_new_keyboard (GRL_KEY_A, LRG_INPUT_MODIFIER_NONE);
    binding2 = lrg_input_binding_new_keyboard (GRL_KEY_B, LRG_INPUT_MODIFIER_NONE);

    lrg_input_action_add_binding (fixture->action, binding1);
    lrg_input_action_add_binding (fixture->action, binding2);
    lrg_input_binding_free (binding1);
    lrg_input_binding_free (binding2);

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 2);

    lrg_input_action_remove_binding (fixture->action, 0);

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 1);
    /* The second binding should now be at index 0 */
    g_assert_cmpint (lrg_input_binding_get_key (lrg_input_action_get_binding (fixture->action, 0)),
                     ==, GRL_KEY_B);
}

static void
test_action_clear_bindings (ActionFixture *fixture,
                            gconstpointer  user_data)
{
    LrgInputBinding *binding1;
    LrgInputBinding *binding2;

    binding1 = lrg_input_binding_new_keyboard (GRL_KEY_X, LRG_INPUT_MODIFIER_NONE);
    binding2 = lrg_input_binding_new_keyboard (GRL_KEY_Y, LRG_INPUT_MODIFIER_NONE);

    lrg_input_action_add_binding (fixture->action, binding1);
    lrg_input_action_add_binding (fixture->action, binding2);
    lrg_input_binding_free (binding1);
    lrg_input_binding_free (binding2);

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 2);

    lrg_input_action_clear_bindings (fixture->action);

    g_assert_cmpuint (lrg_input_action_get_binding_count (fixture->action), ==, 0);
}

static void
test_action_get_binding_out_of_range (ActionFixture *fixture,
                                      gconstpointer  user_data)
{
    const LrgInputBinding *binding;

    /* No bindings added */
    binding = lrg_input_action_get_binding (fixture->action, 0);
    g_assert_null (binding);

    binding = lrg_input_action_get_binding (fixture->action, 100);
    g_assert_null (binding);
}

/* ==========================================================================
 * Test Cases - Map
 * ========================================================================== */

static void
test_map_new (void)
{
    g_autoptr(LrgInputMap) map = NULL;

    map = lrg_input_map_new ();

    g_assert_nonnull (map);
    g_assert_true (LRG_IS_INPUT_MAP (map));
    g_assert_cmpuint (lrg_input_map_get_action_count (map), ==, 0);
}

static void
test_map_add_action (MapFixture    *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action = NULL;

    action = lrg_input_action_new ("jump");
    lrg_input_map_add_action (fixture->map, action);

    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 1);
    g_assert_true (lrg_input_map_has_action (fixture->map, "jump"));
}

static void
test_map_get_action (MapFixture    *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action = NULL;
    LrgInputAction           *found;

    action = lrg_input_action_new ("attack");
    lrg_input_map_add_action (fixture->map, action);

    found = lrg_input_map_get_action (fixture->map, "attack");
    g_assert_nonnull (found);
    g_assert_true (found == action);

    found = lrg_input_map_get_action (fixture->map, "nonexistent");
    g_assert_null (found);
}

static void
test_map_remove_action (MapFixture    *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action = NULL;

    action = lrg_input_action_new ("dodge");
    lrg_input_map_add_action (fixture->map, action);

    g_assert_true (lrg_input_map_has_action (fixture->map, "dodge"));

    lrg_input_map_remove_action (fixture->map, "dodge");

    g_assert_false (lrg_input_map_has_action (fixture->map, "dodge"));
    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 0);
}

static void
test_map_multiple_actions (MapFixture    *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action1 = NULL;
    g_autoptr(LrgInputAction) action2 = NULL;
    g_autoptr(LrgInputAction) action3 = NULL;

    action1 = lrg_input_action_new ("jump");
    action2 = lrg_input_action_new ("attack");
    action3 = lrg_input_action_new ("dodge");

    lrg_input_map_add_action (fixture->map, action1);
    lrg_input_map_add_action (fixture->map, action2);
    lrg_input_map_add_action (fixture->map, action3);

    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 3);
    g_assert_true (lrg_input_map_has_action (fixture->map, "jump"));
    g_assert_true (lrg_input_map_has_action (fixture->map, "attack"));
    g_assert_true (lrg_input_map_has_action (fixture->map, "dodge"));
}

static void
test_map_get_actions (MapFixture    *fixture,
                      gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action1 = NULL;
    g_autoptr(LrgInputAction) action2 = NULL;
    GList                    *actions;

    action1 = lrg_input_action_new ("action1");
    action2 = lrg_input_action_new ("action2");

    lrg_input_map_add_action (fixture->map, action1);
    lrg_input_map_add_action (fixture->map, action2);

    actions = lrg_input_map_get_actions (fixture->map);
    g_assert_nonnull (actions);
    g_assert_cmpuint (g_list_length (actions), ==, 2);

    g_list_free (actions);
}

static void
test_map_clear (MapFixture    *fixture,
                gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action1 = NULL;
    g_autoptr(LrgInputAction) action2 = NULL;

    action1 = lrg_input_action_new ("action1");
    action2 = lrg_input_action_new ("action2");

    lrg_input_map_add_action (fixture->map, action1);
    lrg_input_map_add_action (fixture->map, action2);

    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 2);

    lrg_input_map_clear (fixture->map);

    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 0);
}

/* ==========================================================================
 * Test Cases - Map YAML Serialization
 * ========================================================================== */

static void
test_map_save_load_roundtrip (MapFixture    *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action1 = NULL;
    g_autoptr(LrgInputAction) action2 = NULL;
    LrgInputBinding          *binding;
    g_autoptr(GError)         error = NULL;
    g_autofree gchar         *path = NULL;
    gboolean                  success;

    /* Create actions with bindings */
    action1 = lrg_input_action_new ("jump");
    binding = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    lrg_input_action_add_binding (action1, binding);
    lrg_input_binding_free (binding);
    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    lrg_input_action_add_binding (action1, binding);
    lrg_input_binding_free (binding);

    action2 = lrg_input_action_new ("move_right");
    binding = lrg_input_binding_new_keyboard (GRL_KEY_D, LRG_INPUT_MODIFIER_NONE);
    lrg_input_action_add_binding (action2, binding);
    lrg_input_binding_free (binding);
    binding = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_X, 0.2f, TRUE);
    lrg_input_action_add_binding (action2, binding);
    lrg_input_binding_free (binding);

    lrg_input_map_add_action (fixture->map, action1);
    lrg_input_map_add_action (fixture->map, action2);

    /* Save to temp file */
    path = g_build_filename (g_get_tmp_dir (), "test-input-map.yaml", NULL);
    success = lrg_input_map_save_to_file (fixture->map, path, &error);
    g_assert_no_error (error);
    g_assert_true (success);

    /* Create new map and load */
    {
        g_autoptr(LrgInputMap) loaded_map = NULL;
        LrgInputAction        *loaded_action;
        const LrgInputBinding *loaded_binding;

        loaded_map = lrg_input_map_new ();
        success = lrg_input_map_load_from_file (loaded_map, path, &error);
        g_assert_no_error (error);
        g_assert_true (success);

        /* Verify actions were loaded */
        g_assert_cmpuint (lrg_input_map_get_action_count (loaded_map), ==, 2);
        g_assert_true (lrg_input_map_has_action (loaded_map, "jump"));
        g_assert_true (lrg_input_map_has_action (loaded_map, "move_right"));

        /* Verify jump action bindings */
        loaded_action = lrg_input_map_get_action (loaded_map, "jump");
        g_assert_nonnull (loaded_action);
        g_assert_cmpuint (lrg_input_action_get_binding_count (loaded_action), ==, 2);

        loaded_binding = lrg_input_action_get_binding (loaded_action, 0);
        g_assert_cmpint (lrg_input_binding_get_binding_type (loaded_binding), ==,
                         LRG_INPUT_BINDING_KEYBOARD);
        g_assert_cmpint (lrg_input_binding_get_key (loaded_binding), ==, GRL_KEY_SPACE);

        loaded_binding = lrg_input_action_get_binding (loaded_action, 1);
        g_assert_cmpint (lrg_input_binding_get_binding_type (loaded_binding), ==,
                         LRG_INPUT_BINDING_GAMEPAD_BUTTON);
        g_assert_cmpint (lrg_input_binding_get_gamepad_button (loaded_binding), ==,
                         GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

        /* Verify move_right action bindings */
        loaded_action = lrg_input_map_get_action (loaded_map, "move_right");
        g_assert_nonnull (loaded_action);
        g_assert_cmpuint (lrg_input_action_get_binding_count (loaded_action), ==, 2);

        loaded_binding = lrg_input_action_get_binding (loaded_action, 0);
        g_assert_cmpint (lrg_input_binding_get_binding_type (loaded_binding), ==,
                         LRG_INPUT_BINDING_KEYBOARD);
        g_assert_cmpint (lrg_input_binding_get_key (loaded_binding), ==, GRL_KEY_D);

        loaded_binding = lrg_input_action_get_binding (loaded_action, 1);
        g_assert_cmpint (lrg_input_binding_get_binding_type (loaded_binding), ==,
                         LRG_INPUT_BINDING_GAMEPAD_AXIS);
        g_assert_cmpint (lrg_input_binding_get_gamepad_axis (loaded_binding), ==,
                         GRL_GAMEPAD_AXIS_LEFT_X);
        g_assert_cmpfloat_with_epsilon (lrg_input_binding_get_threshold (loaded_binding),
                                        0.2f, 0.0001f);
        g_assert_true (lrg_input_binding_get_positive (loaded_binding));
    }

    /* Cleanup temp file */
    g_unlink (path);
}

static void
test_map_load_nonexistent_file (MapFixture    *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          success;

    success = lrg_input_map_load_from_file (fixture->map,
                                            "/nonexistent/path/file.yaml",
                                            &error);
    g_assert_false (success);
    g_assert_nonnull (error);
}

static void
test_map_load_clears_existing (MapFixture    *fixture,
                               gconstpointer  user_data)
{
    g_autoptr(LrgInputAction) action = NULL;
    g_autofree gchar         *path = NULL;
    g_autoptr(GError)         error = NULL;
    gboolean                  success;

    /* Add an action to the map */
    action = lrg_input_action_new ("existing_action");
    lrg_input_map_add_action (fixture->map, action);
    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 1);

    /* Save empty map to file */
    {
        g_autoptr(LrgInputMap) empty_map = lrg_input_map_new ();
        path = g_build_filename (g_get_tmp_dir (), "test-empty-map.yaml", NULL);
        success = lrg_input_map_save_to_file (empty_map, path, &error);
        g_assert_no_error (error);
        g_assert_true (success);
    }

    /* Load the empty map - should clear existing actions */
    success = lrg_input_map_load_from_file (fixture->map, path, &error);
    g_assert_no_error (error);
    g_assert_true (success);

    /* Existing action should be gone */
    g_assert_cmpuint (lrg_input_map_get_action_count (fixture->map), ==, 0);
    g_assert_false (lrg_input_map_has_action (fixture->map, "existing_action"));

    g_unlink (path);
}

/* ==========================================================================
 * Test Fixtures - Gamepad
 * ========================================================================== */

typedef struct
{
    LrgInputGamepad *gamepad;
} GamepadFixture;

static void
gamepad_fixture_set_up (GamepadFixture *fixture,
                        gconstpointer   user_data)
{
    fixture->gamepad = LRG_INPUT_GAMEPAD (lrg_input_gamepad_new ());
    g_assert_nonnull (fixture->gamepad);
}

static void
gamepad_fixture_tear_down (GamepadFixture *fixture,
                           gconstpointer   user_data)
{
    g_clear_object (&fixture->gamepad);
}

/* ==========================================================================
 * Test Cases - Gamepad Type Detection
 * ========================================================================== */

static void
test_gamepad_button_name_xbox (void)
{
    const gchar *name;

    /* Test Xbox button names */
    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "A");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "B");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "X");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "Y");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "LB");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_MIDDLE, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "Guide");
}

static void
test_gamepad_button_name_playstation (void)
{
    const gchar *name;

    /* Test PlayStation button names */
    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "Cross");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "Circle");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "Square");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "Triangle");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "L1");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_MIDDLE, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "PS");
}

static void
test_gamepad_button_name_switch (void)
{
    const gchar *name;

    /* Test Nintendo Switch button names (note: A/B and X/Y are swapped) */
    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "B");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "A");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "ZL");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_MIDDLE, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "Home");
}

static void
test_gamepad_button_name_steam_deck (void)
{
    const gchar *name;

    /* Test Steam Deck button names */
    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_STEAM_DECK);
    g_assert_cmpstr (name, ==, "A");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_MIDDLE, LRG_GAMEPAD_TYPE_STEAM_DECK);
    g_assert_cmpstr (name, ==, "Steam");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1, LRG_GAMEPAD_TYPE_STEAM_DECK);
    g_assert_cmpstr (name, ==, "L1");
}

static void
test_gamepad_button_name_generic (void)
{
    const gchar *name;

    /* Generic should use Xbox names */
    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_GENERIC);
    g_assert_cmpstr (name, ==, "A");

    name = lrg_input_gamepad_get_button_display_name_for_type (
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_GAMEPAD_TYPE_UNKNOWN);
    g_assert_cmpstr (name, ==, "A");
}

static void
test_gamepad_axis_name_xbox (void)
{
    const gchar *name;

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_LEFT_X, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "Left Stick X");

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_LEFT_TRIGGER, LRG_GAMEPAD_TYPE_XBOX);
    g_assert_cmpstr (name, ==, "LT");
}

static void
test_gamepad_axis_name_playstation (void)
{
    const gchar *name;

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_LEFT_X, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "Left Stick X");

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_LEFT_TRIGGER, LRG_GAMEPAD_TYPE_PLAYSTATION);
    g_assert_cmpstr (name, ==, "L2");
}

static void
test_gamepad_axis_name_switch (void)
{
    const gchar *name;

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_LEFT_TRIGGER, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "ZL");

    name = lrg_input_gamepad_get_axis_display_name_for_type (
        GRL_GAMEPAD_AXIS_RIGHT_TRIGGER, LRG_GAMEPAD_TYPE_SWITCH);
    g_assert_cmpstr (name, ==, "ZR");
}

/* ==========================================================================
 * Test Cases - Gamepad Dead Zone
 * ========================================================================== */

static void
test_gamepad_dead_zone_default (GamepadFixture *fixture,
                                gconstpointer   user_data)
{
    gfloat dead_zone;

    dead_zone = lrg_input_gamepad_get_dead_zone (fixture->gamepad);
    g_assert_cmpfloat_with_epsilon (dead_zone, 0.1f, 0.0001f);
}

static void
test_gamepad_dead_zone_set_get (GamepadFixture *fixture,
                                gconstpointer   user_data)
{
    lrg_input_gamepad_set_dead_zone (fixture->gamepad, 0.25f);
    g_assert_cmpfloat_with_epsilon (
        lrg_input_gamepad_get_dead_zone (fixture->gamepad), 0.25f, 0.0001f);

    lrg_input_gamepad_set_dead_zone (fixture->gamepad, 0.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_input_gamepad_get_dead_zone (fixture->gamepad), 0.0f, 0.0001f);
}

static void
test_gamepad_dead_zone_clamp (GamepadFixture *fixture,
                              gconstpointer   user_data)
{
    /* Values should be clamped to 0.0-1.0 */
    lrg_input_gamepad_set_dead_zone (fixture->gamepad, -0.5f);
    g_assert_cmpfloat_with_epsilon (
        lrg_input_gamepad_get_dead_zone (fixture->gamepad), 0.0f, 0.0001f);

    lrg_input_gamepad_set_dead_zone (fixture->gamepad, 1.5f);
    g_assert_cmpfloat_with_epsilon (
        lrg_input_gamepad_get_dead_zone (fixture->gamepad), 1.0f, 0.0001f);
}

static void
test_gamepad_dead_zone_property (GamepadFixture *fixture,
                                 gconstpointer   user_data)
{
    gfloat dead_zone;

    /* Test GObject property access */
    g_object_set (fixture->gamepad, "dead-zone", 0.3f, NULL);
    g_object_get (fixture->gamepad, "dead-zone", &dead_zone, NULL);

    g_assert_cmpfloat_with_epsilon (dead_zone, 0.3f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Binding Display String
 * ========================================================================== */

static void
test_binding_display_string_xbox (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    str = lrg_input_binding_to_display_string (binding, LRG_GAMEPAD_TYPE_XBOX);

    g_assert_nonnull (str);
    g_assert_true (strstr (str, "Gamepad0") != NULL);
    g_assert_true (strstr (str, "A") != NULL);

    lrg_input_binding_free (binding);
}

static void
test_binding_display_string_playstation (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    str = lrg_input_binding_to_display_string (binding, LRG_GAMEPAD_TYPE_PLAYSTATION);

    g_assert_nonnull (str);
    g_assert_true (strstr (str, "Gamepad0") != NULL);
    g_assert_true (strstr (str, "Cross") != NULL);

    lrg_input_binding_free (binding);
}

static void
test_binding_display_string_switch (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_button (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    str = lrg_input_binding_to_display_string (binding, LRG_GAMEPAD_TYPE_SWITCH);

    g_assert_nonnull (str);
    g_assert_true (strstr (str, "Gamepad0") != NULL);
    g_assert_true (strstr (str, "B") != NULL);

    lrg_input_binding_free (binding);
}

static void
test_binding_display_string_keyboard_unchanged (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str1 = NULL;
    g_autofree gchar *str2 = NULL;

    binding = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);

    str1 = lrg_input_binding_to_string (binding);
    str2 = lrg_input_binding_to_display_string (binding, LRG_GAMEPAD_TYPE_PLAYSTATION);

    /* For keyboard, both should be identical */
    g_assert_cmpstr (str1, ==, str2);

    lrg_input_binding_free (binding);
}

static void
test_binding_display_string_axis (void)
{
    LrgInputBinding  *binding;
    g_autofree gchar *str = NULL;

    binding = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_TRIGGER, 0.5f, TRUE);
    str = lrg_input_binding_to_display_string (binding, LRG_GAMEPAD_TYPE_PLAYSTATION);

    g_assert_nonnull (str);
    g_assert_true (strstr (str, "L2") != NULL);
    g_assert_true (strstr (str, "+") != NULL);

    lrg_input_binding_free (binding);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Binding Tests */
    g_test_add_func ("/input/binding/new-keyboard", test_binding_new_keyboard);
    g_test_add_func ("/input/binding/new-keyboard-with-modifiers",
                     test_binding_new_keyboard_with_modifiers);
    g_test_add_func ("/input/binding/new-mouse-button", test_binding_new_mouse_button);
    g_test_add_func ("/input/binding/new-gamepad-button", test_binding_new_gamepad_button);
    g_test_add_func ("/input/binding/new-gamepad-axis", test_binding_new_gamepad_axis);
    g_test_add_func ("/input/binding/new-gamepad-axis-negative",
                     test_binding_new_gamepad_axis_negative);
    g_test_add_func ("/input/binding/copy", test_binding_copy);
    g_test_add_func ("/input/binding/to-string-keyboard", test_binding_to_string_keyboard);
    g_test_add_func ("/input/binding/to-string-keyboard-with-modifiers",
                     test_binding_to_string_keyboard_with_modifiers);
    g_test_add_func ("/input/binding/to-string-mouse", test_binding_to_string_mouse);
    g_test_add_func ("/input/binding/to-string-gamepad-button",
                     test_binding_to_string_gamepad_button);
    g_test_add_func ("/input/binding/to-string-gamepad-axis",
                     test_binding_to_string_gamepad_axis);

    /* Action Tests */
    g_test_add_func ("/input/action/new", test_action_new);

    g_test_add ("/input/action/add-binding",
                ActionFixture, NULL,
                action_fixture_set_up,
                test_action_add_binding,
                action_fixture_tear_down);

    g_test_add ("/input/action/add-multiple-bindings",
                ActionFixture, NULL,
                action_fixture_set_up,
                test_action_add_multiple_bindings,
                action_fixture_tear_down);

    g_test_add ("/input/action/remove-binding",
                ActionFixture, NULL,
                action_fixture_set_up,
                test_action_remove_binding,
                action_fixture_tear_down);

    g_test_add ("/input/action/clear-bindings",
                ActionFixture, NULL,
                action_fixture_set_up,
                test_action_clear_bindings,
                action_fixture_tear_down);

    g_test_add ("/input/action/get-binding-out-of-range",
                ActionFixture, NULL,
                action_fixture_set_up,
                test_action_get_binding_out_of_range,
                action_fixture_tear_down);

    /* Map Tests */
    g_test_add_func ("/input/map/new", test_map_new);

    g_test_add ("/input/map/add-action",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_add_action,
                map_fixture_tear_down);

    g_test_add ("/input/map/get-action",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_get_action,
                map_fixture_tear_down);

    g_test_add ("/input/map/remove-action",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_remove_action,
                map_fixture_tear_down);

    g_test_add ("/input/map/multiple-actions",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_multiple_actions,
                map_fixture_tear_down);

    g_test_add ("/input/map/get-actions",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_get_actions,
                map_fixture_tear_down);

    g_test_add ("/input/map/clear",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_clear,
                map_fixture_tear_down);

    /* YAML Serialization Tests */
    g_test_add ("/input/map/save-load-roundtrip",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_save_load_roundtrip,
                map_fixture_tear_down);

    g_test_add ("/input/map/load-nonexistent-file",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_load_nonexistent_file,
                map_fixture_tear_down);

    g_test_add ("/input/map/load-clears-existing",
                MapFixture, NULL,
                map_fixture_set_up,
                test_map_load_clears_existing,
                map_fixture_tear_down);

    /* Gamepad Button Name Tests */
    g_test_add_func ("/input/gamepad/button-name-xbox", test_gamepad_button_name_xbox);
    g_test_add_func ("/input/gamepad/button-name-playstation", test_gamepad_button_name_playstation);
    g_test_add_func ("/input/gamepad/button-name-switch", test_gamepad_button_name_switch);
    g_test_add_func ("/input/gamepad/button-name-steam-deck", test_gamepad_button_name_steam_deck);
    g_test_add_func ("/input/gamepad/button-name-generic", test_gamepad_button_name_generic);

    /* Gamepad Axis Name Tests */
    g_test_add_func ("/input/gamepad/axis-name-xbox", test_gamepad_axis_name_xbox);
    g_test_add_func ("/input/gamepad/axis-name-playstation", test_gamepad_axis_name_playstation);
    g_test_add_func ("/input/gamepad/axis-name-switch", test_gamepad_axis_name_switch);

    /* Gamepad Dead Zone Tests */
    g_test_add ("/input/gamepad/dead-zone-default",
                GamepadFixture, NULL,
                gamepad_fixture_set_up,
                test_gamepad_dead_zone_default,
                gamepad_fixture_tear_down);

    g_test_add ("/input/gamepad/dead-zone-set-get",
                GamepadFixture, NULL,
                gamepad_fixture_set_up,
                test_gamepad_dead_zone_set_get,
                gamepad_fixture_tear_down);

    g_test_add ("/input/gamepad/dead-zone-clamp",
                GamepadFixture, NULL,
                gamepad_fixture_set_up,
                test_gamepad_dead_zone_clamp,
                gamepad_fixture_tear_down);

    g_test_add ("/input/gamepad/dead-zone-property",
                GamepadFixture, NULL,
                gamepad_fixture_set_up,
                test_gamepad_dead_zone_property,
                gamepad_fixture_tear_down);

    /* Binding Display String Tests */
    g_test_add_func ("/input/binding/display-string-xbox", test_binding_display_string_xbox);
    g_test_add_func ("/input/binding/display-string-playstation", test_binding_display_string_playstation);
    g_test_add_func ("/input/binding/display-string-switch", test_binding_display_string_switch);
    g_test_add_func ("/input/binding/display-string-keyboard-unchanged", test_binding_display_string_keyboard_unchanged);
    g_test_add_func ("/input/binding/display-string-axis", test_binding_display_string_axis);

    return g_test_run ();
}
