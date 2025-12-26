/* lrg-input-map.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Container for input actions with YAML serialization.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-input-map.h"
#include "../lrg-log.h"
#include <yaml-glib.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    GHashTable *actions;  /* name -> LrgInputAction* (owned) */
} LrgInputMapPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgInputMap, lrg_input_map, G_TYPE_OBJECT)

/* ==========================================================================
 * Error Domain
 * ========================================================================== */

/**
 * LRG_INPUT_MAP_ERROR:
 *
 * Error domain for input map errors.
 */
#define LRG_INPUT_MAP_ERROR (lrg_input_map_error_quark ())

typedef enum
{
    LRG_INPUT_MAP_ERROR_PARSE,
    LRG_INPUT_MAP_ERROR_INVALID_FORMAT,
    LRG_INPUT_MAP_ERROR_IO
} LrgInputMapError;

GQuark lrg_input_map_error_quark (void);
G_DEFINE_QUARK (lrg-input-map-error, lrg_input_map_error)

/* ==========================================================================
 * YAML String Tables
 * ========================================================================== */

/*
 * Key name lookup table for YAML serialization.
 * These map between GrlKey enum values and YAML string names.
 */
typedef struct
{
    const gchar *name;
    GrlKey       key;
} KeyNameEntry;

static const KeyNameEntry key_name_table[] = {
    { "SPACE", GRL_KEY_SPACE },
    { "ESCAPE", GRL_KEY_ESCAPE },
    { "ENTER", GRL_KEY_ENTER },
    { "TAB", GRL_KEY_TAB },
    { "BACKSPACE", GRL_KEY_BACKSPACE },
    { "INSERT", GRL_KEY_INSERT },
    { "DELETE", GRL_KEY_DELETE },
    { "RIGHT", GRL_KEY_RIGHT },
    { "LEFT", GRL_KEY_LEFT },
    { "DOWN", GRL_KEY_DOWN },
    { "UP", GRL_KEY_UP },
    { "PAGE_UP", GRL_KEY_PAGE_UP },
    { "PAGE_DOWN", GRL_KEY_PAGE_DOWN },
    { "HOME", GRL_KEY_HOME },
    { "END", GRL_KEY_END },
    { "CAPS_LOCK", GRL_KEY_CAPS_LOCK },
    { "SCROLL_LOCK", GRL_KEY_SCROLL_LOCK },
    { "NUM_LOCK", GRL_KEY_NUM_LOCK },
    { "PRINT_SCREEN", GRL_KEY_PRINT_SCREEN },
    { "PAUSE", GRL_KEY_PAUSE },
    { "F1", GRL_KEY_F1 },
    { "F2", GRL_KEY_F2 },
    { "F3", GRL_KEY_F3 },
    { "F4", GRL_KEY_F4 },
    { "F5", GRL_KEY_F5 },
    { "F6", GRL_KEY_F6 },
    { "F7", GRL_KEY_F7 },
    { "F8", GRL_KEY_F8 },
    { "F9", GRL_KEY_F9 },
    { "F10", GRL_KEY_F10 },
    { "F11", GRL_KEY_F11 },
    { "F12", GRL_KEY_F12 },
    { "LEFT_SHIFT", GRL_KEY_LEFT_SHIFT },
    { "LEFT_CONTROL", GRL_KEY_LEFT_CONTROL },
    { "LEFT_ALT", GRL_KEY_LEFT_ALT },
    { "RIGHT_SHIFT", GRL_KEY_RIGHT_SHIFT },
    { "RIGHT_CONTROL", GRL_KEY_RIGHT_CONTROL },
    { "RIGHT_ALT", GRL_KEY_RIGHT_ALT },
    { "A", GRL_KEY_A },
    { "B", GRL_KEY_B },
    { "C", GRL_KEY_C },
    { "D", GRL_KEY_D },
    { "E", GRL_KEY_E },
    { "F", GRL_KEY_F },
    { "G", GRL_KEY_G },
    { "H", GRL_KEY_H },
    { "I", GRL_KEY_I },
    { "J", GRL_KEY_J },
    { "K", GRL_KEY_K },
    { "L", GRL_KEY_L },
    { "M", GRL_KEY_M },
    { "N", GRL_KEY_N },
    { "O", GRL_KEY_O },
    { "P", GRL_KEY_P },
    { "Q", GRL_KEY_Q },
    { "R", GRL_KEY_R },
    { "S", GRL_KEY_S },
    { "T", GRL_KEY_T },
    { "U", GRL_KEY_U },
    { "V", GRL_KEY_V },
    { "W", GRL_KEY_W },
    { "X", GRL_KEY_X },
    { "Y", GRL_KEY_Y },
    { "Z", GRL_KEY_Z },
    { "0", GRL_KEY_ZERO },
    { "1", GRL_KEY_ONE },
    { "2", GRL_KEY_TWO },
    { "3", GRL_KEY_THREE },
    { "4", GRL_KEY_FOUR },
    { "5", GRL_KEY_FIVE },
    { "6", GRL_KEY_SIX },
    { "7", GRL_KEY_SEVEN },
    { "8", GRL_KEY_EIGHT },
    { "9", GRL_KEY_NINE },
    { NULL, GRL_KEY_NULL }
};

typedef struct
{
    const gchar    *name;
    GrlMouseButton  button;
} MouseButtonEntry;

static const MouseButtonEntry mouse_button_table[] = {
    { "LEFT", GRL_MOUSE_BUTTON_LEFT },
    { "RIGHT", GRL_MOUSE_BUTTON_RIGHT },
    { "MIDDLE", GRL_MOUSE_BUTTON_MIDDLE },
    { "SIDE", GRL_MOUSE_BUTTON_SIDE },
    { "EXTRA", GRL_MOUSE_BUTTON_EXTRA },
    { "FORWARD", GRL_MOUSE_BUTTON_FORWARD },
    { "BACK", GRL_MOUSE_BUTTON_BACK },
    { NULL, 0 }
};

typedef struct
{
    const gchar      *name;
    GrlGamepadButton  button;
} GamepadButtonEntry;

static const GamepadButtonEntry gamepad_button_table[] = {
    { "LEFT_FACE_UP", GRL_GAMEPAD_BUTTON_LEFT_FACE_UP },
    { "LEFT_FACE_RIGHT", GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT },
    { "LEFT_FACE_DOWN", GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN },
    { "LEFT_FACE_LEFT", GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT },
    { "RIGHT_FACE_UP", GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP },
    { "RIGHT_FACE_RIGHT", GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT },
    { "RIGHT_FACE_DOWN", GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN },
    { "RIGHT_FACE_LEFT", GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT },
    { "LEFT_TRIGGER_1", GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1 },
    { "LEFT_TRIGGER_2", GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2 },
    { "RIGHT_TRIGGER_1", GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1 },
    { "RIGHT_TRIGGER_2", GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2 },
    { "MIDDLE_LEFT", GRL_GAMEPAD_BUTTON_MIDDLE_LEFT },
    { "MIDDLE", GRL_GAMEPAD_BUTTON_MIDDLE },
    { "MIDDLE_RIGHT", GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT },
    { "LEFT_THUMB", GRL_GAMEPAD_BUTTON_LEFT_THUMB },
    { "RIGHT_THUMB", GRL_GAMEPAD_BUTTON_RIGHT_THUMB },
    { NULL, 0 }
};

typedef struct
{
    const gchar    *name;
    GrlGamepadAxis  axis;
} GamepadAxisEntry;

static const GamepadAxisEntry gamepad_axis_table[] = {
    { "LEFT_X", GRL_GAMEPAD_AXIS_LEFT_X },
    { "LEFT_Y", GRL_GAMEPAD_AXIS_LEFT_Y },
    { "RIGHT_X", GRL_GAMEPAD_AXIS_RIGHT_X },
    { "RIGHT_Y", GRL_GAMEPAD_AXIS_RIGHT_Y },
    { "LEFT_TRIGGER", GRL_GAMEPAD_AXIS_LEFT_TRIGGER },
    { "RIGHT_TRIGGER", GRL_GAMEPAD_AXIS_RIGHT_TRIGGER },
    { NULL, 0 }
};

/* ==========================================================================
 * YAML Helper Functions
 * ========================================================================== */

static GrlKey
key_from_string (const gchar *name)
{
    const KeyNameEntry *entry;

    if (name == NULL)
    {
        return GRL_KEY_NULL;
    }

    for (entry = key_name_table; entry->name != NULL; entry++)
    {
        if (g_ascii_strcasecmp (entry->name, name) == 0)
        {
            return entry->key;
        }
    }

    return GRL_KEY_NULL;
}

static const gchar *
key_to_yaml_string (GrlKey key)
{
    const KeyNameEntry *entry;

    for (entry = key_name_table; entry->name != NULL; entry++)
    {
        if (entry->key == key)
        {
            return entry->name;
        }
    }

    return "UNKNOWN";
}

static GrlMouseButton
mouse_button_from_string (const gchar *name)
{
    const MouseButtonEntry *entry;

    if (name == NULL)
    {
        return GRL_MOUSE_BUTTON_LEFT;
    }

    for (entry = mouse_button_table; entry->name != NULL; entry++)
    {
        if (g_ascii_strcasecmp (entry->name, name) == 0)
        {
            return entry->button;
        }
    }

    return GRL_MOUSE_BUTTON_LEFT;
}

static const gchar *
mouse_button_to_yaml_string (GrlMouseButton button)
{
    const MouseButtonEntry *entry;

    for (entry = mouse_button_table; entry->name != NULL; entry++)
    {
        if (entry->button == button)
        {
            return entry->name;
        }
    }

    return "LEFT";
}

static GrlGamepadButton
gamepad_button_from_string (const gchar *name)
{
    const GamepadButtonEntry *entry;

    if (name == NULL)
    {
        return GRL_GAMEPAD_BUTTON_UNKNOWN;
    }

    for (entry = gamepad_button_table; entry->name != NULL; entry++)
    {
        if (g_ascii_strcasecmp (entry->name, name) == 0)
        {
            return entry->button;
        }
    }

    return GRL_GAMEPAD_BUTTON_UNKNOWN;
}

static const gchar *
gamepad_button_to_yaml_string (GrlGamepadButton button)
{
    const GamepadButtonEntry *entry;

    for (entry = gamepad_button_table; entry->name != NULL; entry++)
    {
        if (entry->button == button)
        {
            return entry->name;
        }
    }

    return "UNKNOWN";
}

static GrlGamepadAxis
gamepad_axis_from_string (const gchar *name)
{
    const GamepadAxisEntry *entry;

    if (name == NULL)
    {
        return GRL_GAMEPAD_AXIS_LEFT_X;
    }

    for (entry = gamepad_axis_table; entry->name != NULL; entry++)
    {
        if (g_ascii_strcasecmp (entry->name, name) == 0)
        {
            return entry->axis;
        }
    }

    return GRL_GAMEPAD_AXIS_LEFT_X;
}

static const gchar *
gamepad_axis_to_yaml_string (GrlGamepadAxis axis)
{
    const GamepadAxisEntry *entry;

    for (entry = gamepad_axis_table; entry->name != NULL; entry++)
    {
        if (entry->axis == axis)
        {
            return entry->name;
        }
    }

    return "LEFT_X";
}

static LrgInputModifiers
modifiers_from_sequence (YamlSequence *seq)
{
    LrgInputModifiers mods;
    guint             i;
    guint             len;

    mods = LRG_INPUT_MODIFIER_NONE;

    if (seq == NULL)
    {
        return mods;
    }

    len = yaml_sequence_get_length (seq);

    for (i = 0; i < len; i++)
    {
        YamlNode    *node = yaml_sequence_get_element (seq, i);
        const gchar *str = yaml_node_get_string (node);

        if (str != NULL)
        {
            if (g_ascii_strcasecmp (str, "SHIFT") == 0)
            {
                mods |= LRG_INPUT_MODIFIER_SHIFT;
            }
            else if (g_ascii_strcasecmp (str, "CTRL") == 0 ||
                     g_ascii_strcasecmp (str, "CONTROL") == 0)
            {
                mods |= LRG_INPUT_MODIFIER_CTRL;
            }
            else if (g_ascii_strcasecmp (str, "ALT") == 0)
            {
                mods |= LRG_INPUT_MODIFIER_ALT;
            }
        }
    }

    return mods;
}

/*
 * parse_binding:
 *
 * Parses a single binding from a YAML mapping.
 */
static LrgInputBinding *
parse_binding (YamlMapping *binding_map)
{
    const gchar       *type_str;
    const gchar       *key_str;
    const gchar       *button_str;
    const gchar       *axis_str;
    gint64             gamepad;
    gdouble            threshold;
    gboolean           positive;
    YamlSequence      *mods_seq;
    LrgInputModifiers  modifiers;

    type_str = yaml_mapping_get_string_member (binding_map, "type");

    if (type_str == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_INPUT, "Binding missing 'type' field");
        return NULL;
    }

    if (g_ascii_strcasecmp (type_str, "keyboard") == 0)
    {
        key_str = yaml_mapping_get_string_member (binding_map, "key");
        mods_seq = yaml_mapping_get_sequence_member (binding_map, "modifiers");
        modifiers = modifiers_from_sequence (mods_seq);

        return lrg_input_binding_new_keyboard (key_from_string (key_str),
                                               modifiers);
    }
    else if (g_ascii_strcasecmp (type_str, "mouse_button") == 0)
    {
        button_str = yaml_mapping_get_string_member (binding_map, "button");
        mods_seq = yaml_mapping_get_sequence_member (binding_map, "modifiers");
        modifiers = modifiers_from_sequence (mods_seq);

        return lrg_input_binding_new_mouse_button (mouse_button_from_string (button_str),
                                                   modifiers);
    }
    else if (g_ascii_strcasecmp (type_str, "gamepad_button") == 0)
    {
        gamepad = yaml_mapping_get_int_member (binding_map, "gamepad");
        button_str = yaml_mapping_get_string_member (binding_map, "button");

        return lrg_input_binding_new_gamepad_button ((gint)gamepad,
                                                     gamepad_button_from_string (button_str));
    }
    else if (g_ascii_strcasecmp (type_str, "gamepad_axis") == 0)
    {
        gamepad = yaml_mapping_get_int_member (binding_map, "gamepad");
        axis_str = yaml_mapping_get_string_member (binding_map, "axis");
        threshold = yaml_mapping_get_double_member (binding_map, "threshold");
        positive = yaml_mapping_get_boolean_member (binding_map, "positive");

        if (threshold <= 0.0)
        {
            threshold = 0.5;  /* Default threshold */
        }

        return lrg_input_binding_new_gamepad_axis ((gint)gamepad,
                                                   gamepad_axis_from_string (axis_str),
                                                   (gfloat)threshold,
                                                   positive);
    }

    lrg_warning (LRG_LOG_DOMAIN_INPUT, "Unknown binding type: %s", type_str);
    return NULL;
}

/*
 * parse_action:
 *
 * Parses an action from a YAML mapping with a bindings sequence.
 */
static LrgInputAction *
parse_action (const gchar *name,
              YamlMapping *action_map)
{
    LrgInputAction *action;
    YamlSequence   *bindings_seq;
    guint           i;
    guint           len;

    action = lrg_input_action_new (name);
    bindings_seq = yaml_mapping_get_sequence_member (action_map, "bindings");

    if (bindings_seq == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_INPUT,
                     "Action '%s' has no bindings", name);
        return action;
    }

    len = yaml_sequence_get_length (bindings_seq);

    for (i = 0; i < len; i++)
    {
        YamlNode        *node = yaml_sequence_get_element (bindings_seq, i);
        YamlMapping     *binding_map = yaml_node_get_mapping (node);
        LrgInputBinding *binding;

        if (binding_map == NULL)
        {
            lrg_warning (LRG_LOG_DOMAIN_INPUT,
                         "Action '%s' binding %u is not a mapping", name, i);
            continue;
        }

        binding = parse_binding (binding_map);

        if (binding != NULL)
        {
            lrg_input_action_add_binding (action, binding);
            lrg_input_binding_free (binding);
        }
    }

    return action;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_map_finalize (GObject *object)
{
    LrgInputMap        *self = LRG_INPUT_MAP (object);
    LrgInputMapPrivate *priv = lrg_input_map_get_instance_private (self);

    g_clear_pointer (&priv->actions, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_input_map_parent_class)->finalize (object);
}

static void
lrg_input_map_class_init (LrgInputMapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_input_map_finalize;
}

static void
lrg_input_map_init (LrgInputMap *self)
{
    LrgInputMapPrivate *priv = lrg_input_map_get_instance_private (self);

    priv->actions = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           g_object_unref);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_map_new:
 *
 * Creates a new empty input map.
 *
 * Returns: (transfer full): A new #LrgInputMap
 */
LrgInputMap *
lrg_input_map_new (void)
{
    return g_object_new (LRG_TYPE_INPUT_MAP, NULL);
}

/* ==========================================================================
 * Action Management
 * ========================================================================== */

/**
 * lrg_input_map_add_action:
 * @self: an #LrgInputMap
 * @action: the action to add
 *
 * Adds an action to this map.
 *
 * The map takes ownership of the action.
 */
void
lrg_input_map_add_action (LrgInputMap    *self,
                          LrgInputAction *action)
{
    LrgInputMapPrivate *priv;
    const gchar        *name;

    g_return_if_fail (LRG_IS_INPUT_MAP (self));
    g_return_if_fail (LRG_IS_INPUT_ACTION (action));

    priv = lrg_input_map_get_instance_private (self);
    name = lrg_input_action_get_name (action);

    g_hash_table_insert (priv->actions,
                         g_strdup (name),
                         g_object_ref (action));

    lrg_debug (LRG_LOG_DOMAIN_INPUT,
               "Added action '%s' to map (count: %u)",
               name, g_hash_table_size (priv->actions));
}

/**
 * lrg_input_map_remove_action:
 * @self: an #LrgInputMap
 * @name: the action name to remove
 *
 * Removes an action from this map by name.
 */
void
lrg_input_map_remove_action (LrgInputMap *self,
                             const gchar *name)
{
    LrgInputMapPrivate *priv;

    g_return_if_fail (LRG_IS_INPUT_MAP (self));
    g_return_if_fail (name != NULL);

    priv = lrg_input_map_get_instance_private (self);

    if (g_hash_table_remove (priv->actions, name))
    {
        lrg_debug (LRG_LOG_DOMAIN_INPUT,
                   "Removed action '%s' from map (count: %u)",
                   name, g_hash_table_size (priv->actions));
    }
}

/**
 * lrg_input_map_get_action:
 * @self: an #LrgInputMap
 * @name: the action name
 *
 * Gets an action by name.
 *
 * Returns: (transfer none) (nullable): The action, or %NULL if not found
 */
LrgInputAction *
lrg_input_map_get_action (LrgInputMap *self,
                          const gchar *name)
{
    LrgInputMapPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_input_map_get_instance_private (self);
    return g_hash_table_lookup (priv->actions, name);
}

/**
 * lrg_input_map_has_action:
 * @self: an #LrgInputMap
 * @name: the action name
 *
 * Checks if an action exists in this map.
 *
 * Returns: %TRUE if the action exists
 */
gboolean
lrg_input_map_has_action (LrgInputMap *self,
                          const gchar *name)
{
    LrgInputMapPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_input_map_get_instance_private (self);
    return g_hash_table_contains (priv->actions, name);
}

/**
 * lrg_input_map_get_actions:
 * @self: an #LrgInputMap
 *
 * Gets a list of all actions in this map.
 *
 * Returns: (transfer container) (element-type LrgInputAction): A list of actions
 */
GList *
lrg_input_map_get_actions (LrgInputMap *self)
{
    LrgInputMapPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), NULL);

    priv = lrg_input_map_get_instance_private (self);
    return g_hash_table_get_values (priv->actions);
}

/**
 * lrg_input_map_get_action_count:
 * @self: an #LrgInputMap
 *
 * Gets the number of actions in this map.
 *
 * Returns: The action count
 */
guint
lrg_input_map_get_action_count (LrgInputMap *self)
{
    LrgInputMapPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), 0);

    priv = lrg_input_map_get_instance_private (self);
    return g_hash_table_size (priv->actions);
}

/**
 * lrg_input_map_clear:
 * @self: an #LrgInputMap
 *
 * Removes all actions from this map.
 */
void
lrg_input_map_clear (LrgInputMap *self)
{
    LrgInputMapPrivate *priv;

    g_return_if_fail (LRG_IS_INPUT_MAP (self));

    priv = lrg_input_map_get_instance_private (self);
    g_hash_table_remove_all (priv->actions);

    lrg_debug (LRG_LOG_DOMAIN_INPUT, "Cleared all actions from map");
}

/* ==========================================================================
 * Convenience State Query
 * ========================================================================== */

/**
 * lrg_input_map_is_pressed:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action was just pressed this frame.
 *
 * Returns: %TRUE if the action was just pressed, %FALSE otherwise or if not found
 */
gboolean
lrg_input_map_is_pressed (LrgInputMap *self,
                          const gchar *action_name)
{
    LrgInputAction *action;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (action_name != NULL, FALSE);

    action = lrg_input_map_get_action (self, action_name);

    if (action == NULL)
    {
        return FALSE;
    }

    return lrg_input_action_is_pressed (action);
}

/**
 * lrg_input_map_is_down:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action is currently held down.
 *
 * Returns: %TRUE if the action is held down, %FALSE otherwise or if not found
 */
gboolean
lrg_input_map_is_down (LrgInputMap *self,
                       const gchar *action_name)
{
    LrgInputAction *action;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (action_name != NULL, FALSE);

    action = lrg_input_map_get_action (self, action_name);

    if (action == NULL)
    {
        return FALSE;
    }

    return lrg_input_action_is_down (action);
}

/**
 * lrg_input_map_is_released:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action was just released this frame.
 *
 * Returns: %TRUE if the action was just released, %FALSE otherwise or if not found
 */
gboolean
lrg_input_map_is_released (LrgInputMap *self,
                           const gchar *action_name)
{
    LrgInputAction *action;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (action_name != NULL, FALSE);

    action = lrg_input_map_get_action (self, action_name);

    if (action == NULL)
    {
        return FALSE;
    }

    return lrg_input_action_is_released (action);
}

/**
 * lrg_input_map_get_value:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Gets the axis value for an action.
 *
 * Returns: The axis value (0.0 to 1.0), or 0.0 if not found
 */
gfloat
lrg_input_map_get_value (LrgInputMap *self,
                         const gchar *action_name)
{
    LrgInputAction *action;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), 0.0f);
    g_return_val_if_fail (action_name != NULL, 0.0f);

    action = lrg_input_map_get_action (self, action_name);

    if (action == NULL)
    {
        return 0.0f;
    }

    return lrg_input_action_get_value (action);
}

/* ==========================================================================
 * YAML Serialization
 * ========================================================================== */

/**
 * lrg_input_map_load_from_file:
 * @self: an #LrgInputMap
 * @path: (type filename): path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads input mappings from a YAML file.
 *
 * This clears any existing actions before loading.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_input_map_load_from_file (LrgInputMap  *self,
                              const gchar  *path,
                              GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlDocument         *doc;
    YamlNode             *root;
    YamlMapping          *root_map;
    YamlMapping          *actions_map;
    GList                *keys;
    GList                *l;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, error))
    {
        return FALSE;
    }

    doc = yaml_parser_get_document (parser, 0);

    if (doc == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_PARSE,
                     "Failed to parse YAML document from %s", path);
        return FALSE;
    }

    root = yaml_document_get_root (doc);

    if (root == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_INVALID_FORMAT,
                     "YAML document has no root node");
        return FALSE;
    }

    root_map = yaml_node_get_mapping (root);

    if (root_map == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_INVALID_FORMAT,
                     "Root node is not a mapping");
        return FALSE;
    }

    /* Clear existing actions */
    lrg_input_map_clear (self);

    /* Look for "actions" mapping */
    actions_map = yaml_mapping_get_mapping_member (root_map, "actions");

    if (actions_map == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_INVALID_FORMAT,
                     "Missing 'actions' mapping");
        return FALSE;
    }

    /* Iterate over action names */
    keys = yaml_mapping_get_members (actions_map);

    for (l = keys; l != NULL; l = l->next)
    {
        const gchar    *action_name = l->data;
        YamlMapping    *action_map;
        LrgInputAction *action;

        action_map = yaml_mapping_get_mapping_member (actions_map, action_name);

        if (action_map == NULL)
        {
            lrg_warning (LRG_LOG_DOMAIN_INPUT,
                         "Action '%s' is not a mapping, skipping", action_name);
            continue;
        }

        action = parse_action (action_name, action_map);

        if (action != NULL)
        {
            lrg_input_map_add_action (self, action);
            g_object_unref (action);
        }
    }

    g_list_free (keys);

    lrg_info (LRG_LOG_DOMAIN_INPUT,
              "Loaded %u actions from %s",
              lrg_input_map_get_action_count (self), path);

    return TRUE;
}

/**
 * lrg_input_map_save_to_file:
 * @self: an #LrgInputMap
 * @path: (type filename): path to save the YAML file
 * @error: (nullable): return location for error
 *
 * Saves input mappings to a YAML file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_input_map_save_to_file (LrgInputMap  *self,
                            const gchar  *path,
                            GError      **error)
{
    LrgInputMapPrivate    *priv;
    g_autoptr(YamlBuilder) builder = NULL;
    g_autoptr(YamlGenerator) generator = NULL;
    YamlDocument          *doc;
    GHashTableIter         iter;
    gpointer               key;
    gpointer               value;
    gchar                 *yaml_str;
    gboolean               ret;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_input_map_get_instance_private (self);

    builder = yaml_builder_new ();

    yaml_builder_begin_mapping (builder);  /* root */
    yaml_builder_set_member_name (builder, "actions");
    yaml_builder_begin_mapping (builder);  /* actions */

    /* Iterate over actions */
    g_hash_table_iter_init (&iter, priv->actions);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar    *action_name = key;
        LrgInputAction *action = value;
        guint           binding_count;
        guint           i;

        yaml_builder_set_member_name (builder, action_name);
        yaml_builder_begin_mapping (builder);  /* action */

        yaml_builder_set_member_name (builder, "bindings");
        yaml_builder_begin_sequence (builder);  /* bindings */

        binding_count = lrg_input_action_get_binding_count (action);

        for (i = 0; i < binding_count; i++)
        {
            const LrgInputBinding *binding;
            LrgInputBindingType    type;
            LrgInputModifiers      mods;

            binding = lrg_input_action_get_binding (action, i);

            if (binding == NULL)
            {
                continue;
            }

            type = lrg_input_binding_get_binding_type (binding);

            yaml_builder_begin_mapping (builder);  /* binding */

            switch (type)
            {
            case LRG_INPUT_BINDING_KEYBOARD:
                yaml_builder_set_member_name (builder, "type");
                yaml_builder_add_string_value (builder, "keyboard");
                yaml_builder_set_member_name (builder, "key");
                yaml_builder_add_string_value (builder,
                    key_to_yaml_string (lrg_input_binding_get_key (binding)));

                mods = lrg_input_binding_get_modifiers (binding);

                if (mods != LRG_INPUT_MODIFIER_NONE)
                {
                    yaml_builder_set_member_name (builder, "modifiers");
                    yaml_builder_begin_sequence (builder);

                    if (mods & LRG_INPUT_MODIFIER_SHIFT)
                    {
                        yaml_builder_add_string_value (builder, "SHIFT");
                    }
                    if (mods & LRG_INPUT_MODIFIER_CTRL)
                    {
                        yaml_builder_add_string_value (builder, "CTRL");
                    }
                    if (mods & LRG_INPUT_MODIFIER_ALT)
                    {
                        yaml_builder_add_string_value (builder, "ALT");
                    }

                    yaml_builder_end_sequence (builder);
                }
                break;

            case LRG_INPUT_BINDING_MOUSE_BUTTON:
                yaml_builder_set_member_name (builder, "type");
                yaml_builder_add_string_value (builder, "mouse_button");
                yaml_builder_set_member_name (builder, "button");
                yaml_builder_add_string_value (builder,
                    mouse_button_to_yaml_string (lrg_input_binding_get_mouse_button (binding)));

                mods = lrg_input_binding_get_modifiers (binding);

                if (mods != LRG_INPUT_MODIFIER_NONE)
                {
                    yaml_builder_set_member_name (builder, "modifiers");
                    yaml_builder_begin_sequence (builder);

                    if (mods & LRG_INPUT_MODIFIER_SHIFT)
                    {
                        yaml_builder_add_string_value (builder, "SHIFT");
                    }
                    if (mods & LRG_INPUT_MODIFIER_CTRL)
                    {
                        yaml_builder_add_string_value (builder, "CTRL");
                    }
                    if (mods & LRG_INPUT_MODIFIER_ALT)
                    {
                        yaml_builder_add_string_value (builder, "ALT");
                    }

                    yaml_builder_end_sequence (builder);
                }
                break;

            case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
                yaml_builder_set_member_name (builder, "type");
                yaml_builder_add_string_value (builder, "gamepad_button");
                yaml_builder_set_member_name (builder, "gamepad");
                yaml_builder_add_int_value (builder,
                    lrg_input_binding_get_gamepad (binding));
                yaml_builder_set_member_name (builder, "button");
                yaml_builder_add_string_value (builder,
                    gamepad_button_to_yaml_string (lrg_input_binding_get_gamepad_button (binding)));
                break;

            case LRG_INPUT_BINDING_GAMEPAD_AXIS:
                yaml_builder_set_member_name (builder, "type");
                yaml_builder_add_string_value (builder, "gamepad_axis");
                yaml_builder_set_member_name (builder, "gamepad");
                yaml_builder_add_int_value (builder,
                    lrg_input_binding_get_gamepad (binding));
                yaml_builder_set_member_name (builder, "axis");
                yaml_builder_add_string_value (builder,
                    gamepad_axis_to_yaml_string (lrg_input_binding_get_gamepad_axis (binding)));
                yaml_builder_set_member_name (builder, "threshold");
                yaml_builder_add_double_value (builder,
                    lrg_input_binding_get_threshold (binding));
                yaml_builder_set_member_name (builder, "positive");
                yaml_builder_add_boolean_value (builder,
                    lrg_input_binding_get_positive (binding));
                break;
            }

            yaml_builder_end_mapping (builder);  /* binding */
        }

        yaml_builder_end_sequence (builder);  /* bindings */
        yaml_builder_end_mapping (builder);  /* action */
    }

    yaml_builder_end_mapping (builder);  /* actions */
    yaml_builder_end_mapping (builder);  /* root */

    doc = yaml_builder_get_document (builder);

    if (doc == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_IO,
                     "Failed to build YAML document");
        return FALSE;
    }

    generator = yaml_generator_new ();
    yaml_generator_set_document (generator, doc);
    yaml_str = yaml_generator_to_data (generator, NULL, error);

    if (yaml_str == NULL)
    {
        return FALSE;
    }

    ret = g_file_set_contents (path, yaml_str, -1, error);
    g_free (yaml_str);

    if (ret)
    {
        lrg_info (LRG_LOG_DOMAIN_INPUT,
                  "Saved %u actions to %s",
                  g_hash_table_size (priv->actions), path);
    }

    return ret;
}
