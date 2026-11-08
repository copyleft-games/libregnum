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
#include "../core/lrg-data-loader.h"
#include "../lrg-log.h"
#include <yaml-glib.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    GHashTable *actions;  /* name -> LrgInputAction* (owned) */
    GPtrArray  *order;    /* action names, in the order added */
    YamlNode   *source;   /* root of the last loaded document: unknown keys */
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
    g_clear_pointer (&priv->order, g_ptr_array_unref);
    g_clear_pointer (&priv->source, yaml_node_unref);

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
    priv->order = g_ptr_array_new_with_free_func (g_free);
}

/* the index of @name in the declaration order, or -1 */
static gint
order_index (LrgInputMapPrivate *priv,
             const gchar        *name)
{
    guint i;

    for (i = 0; i < priv->order->len; i++)
    {
        if (g_str_equal (g_ptr_array_index (priv->order, i), name))
            return (gint)i;
    }
    return -1;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
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

/*
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

    /* a new name goes last; a replaced action keeps its place */
    if (!g_hash_table_contains (priv->actions, name))
        g_ptr_array_add (priv->order, g_strdup (name));

    g_hash_table_insert (priv->actions,
                         g_strdup (name),
                         g_object_ref (action));

    lrg_debug (LRG_LOG_DOMAIN_INPUT,
               "Added action '%s' to map (count: %u)",
               name, g_hash_table_size (priv->actions));
}

/*
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
        g_ptr_array_remove_index (priv->order, (guint)order_index (priv, name));
        lrg_debug (LRG_LOG_DOMAIN_INPUT,
                   "Removed action '%s' from map (count: %u)",
                   name, g_hash_table_size (priv->actions));
    }
}

/*
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

/*
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

/*
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
    GList              *actions = NULL;
    guint               i;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), NULL);

    priv = lrg_input_map_get_instance_private (self);
    for (i = priv->order->len; i > 0; i--)
        actions = g_list_prepend (actions, g_hash_table_lookup (priv->actions,
                                                                g_ptr_array_index (priv->order, i - 1)));
    return actions;
}

/*
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

/*
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
    g_ptr_array_set_size (priv->order, 0);

    lrg_debug (LRG_LOG_DOMAIN_INPUT, "Cleared all actions from map");
}

void
lrg_input_map_clear_overrides (LrgInputMap *self)
{
    LrgInputMapPrivate *priv;
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_INPUT_MAP (self));

    priv = lrg_input_map_get_instance_private (self);
    g_hash_table_iter_init (&iter, priv->actions);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        lrg_input_action_clear_override (LRG_INPUT_ACTION (value));
}

/* ==========================================================================
 * Convenience State Query
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

gfloat
lrg_input_map_get_axis (LrgInputMap *self,
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

    return lrg_input_action_get_axis (action);
}

/* ==========================================================================
 * YAML Serialization
 * ========================================================================== */

/*
 * Loads the actions of a parsed document. @source names the text in
 * messages (a path, or "<data>").
 */
static gboolean
load_from_parser (LrgInputMap  *self,
                  YamlParser   *parser,
                  const gchar  *source,
                  GError      **error)
{
    LrgInputMapPrivate   *priv = lrg_input_map_get_instance_private (self);
    YamlDocument         *doc;
    YamlNode             *root;
    YamlMapping          *root_map;
    YamlMapping          *actions_map;
    GList                *keys;
    GList                *l;

    doc = yaml_parser_get_document (parser, 0);

    if (doc == NULL)
    {
        g_set_error (error, LRG_INPUT_MAP_ERROR, LRG_INPUT_MAP_ERROR_PARSE,
                     "Failed to parse YAML document from %s", source);
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

    /* the document, for the keys the engine does not know */
    g_clear_pointer (&priv->source, yaml_node_unref);
    priv->source = yaml_node_copy (root);

    /* Iterate over action names (document order) */
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
              lrg_input_map_get_action_count (self), source);

    return TRUE;
}

/*
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

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, error))
    {
        return FALSE;
    }

    return load_from_parser (self, parser, path, error);
}

gboolean
lrg_input_map_load_from_data (LrgInputMap  *self,
                              const gchar  *data,
                              gssize        length,
                              GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (data != NULL || length == 0, FALSE);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, error))
    {
        return FALSE;
    }

    return load_from_parser (self, parser, "<data>", error);
}

/* ==========================================================================
 * Saving
 * ========================================================================== */

/* sets @key of @mapping to @node (consumed) */
static void
put (YamlMapping *mapping,
     const gchar *key,
     YamlNode    *node)
{
    yaml_mapping_set_member (mapping, key, node);
    yaml_node_unref (node);
}

/* the modifiers sequence, or %NULL for none */
static YamlNode *
modifiers_node (LrgInputModifiers mods)
{
    g_autoptr(YamlSequence) seq = NULL;

    if (mods == LRG_INPUT_MODIFIER_NONE)
        return NULL;
    seq = yaml_sequence_new ();
    if (mods & LRG_INPUT_MODIFIER_SHIFT)
        yaml_sequence_add_string_element (seq, "SHIFT");
    if (mods & LRG_INPUT_MODIFIER_CTRL)
        yaml_sequence_add_string_element (seq, "CTRL");
    if (mods & LRG_INPUT_MODIFIER_ALT)
        yaml_sequence_add_string_element (seq, "ALT");
    return yaml_node_new_sequence (seq);
}

/* the binding keys the loader reads, by binding type */
static const gchar * const keyboard_keys[] = { "type", "key", "modifiers", NULL };
static const gchar * const mouse_keys[] = { "type", "button", "modifiers", NULL };
static const gchar * const pad_button_keys[] = { "type", "gamepad", "button", NULL };
static const gchar * const pad_axis_keys[] = { "type", "gamepad", "axis", "threshold", "positive", NULL };

/*
 * One binding: its known keys, then the unknown keys of @old (the binding
 * at the same index of the loaded document) when it has the same type.
 */
static YamlNode *
binding_node (const LrgInputBinding *binding,
              YamlMapping           *old)
{
    g_autoptr(YamlMapping) map = yaml_mapping_new ();
    const gchar * const *known = NULL;
    const gchar *type_name = NULL;
    YamlNode *mods;
    guint i;

    switch (lrg_input_binding_get_binding_type (binding))
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        type_name = "keyboard";
        known = keyboard_keys;
        put (map, "type", lrg_data_yaml_string (type_name));
        put (map, "key", lrg_data_yaml_string (key_to_yaml_string (lrg_input_binding_get_key (binding))));
        mods = modifiers_node (lrg_input_binding_get_modifiers (binding));
        if (mods != NULL)
            put (map, "modifiers", mods);
        break;

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        type_name = "mouse_button";
        known = mouse_keys;
        put (map, "type", lrg_data_yaml_string (type_name));
        put (map, "button", lrg_data_yaml_string (
            mouse_button_to_yaml_string (lrg_input_binding_get_mouse_button (binding))));
        mods = modifiers_node (lrg_input_binding_get_modifiers (binding));
        if (mods != NULL)
            put (map, "modifiers", mods);
        break;

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        type_name = "gamepad_button";
        known = pad_button_keys;
        put (map, "type", lrg_data_yaml_string (type_name));
        put (map, "gamepad", lrg_data_yaml_int (lrg_input_binding_get_gamepad (binding)));
        put (map, "button", lrg_data_yaml_string (
            gamepad_button_to_yaml_string (lrg_input_binding_get_gamepad_button (binding))));
        break;

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        type_name = "gamepad_axis";
        known = pad_axis_keys;
        put (map, "type", lrg_data_yaml_string (type_name));
        put (map, "gamepad", lrg_data_yaml_int (lrg_input_binding_get_gamepad (binding)));
        put (map, "axis", lrg_data_yaml_string (
            gamepad_axis_to_yaml_string (lrg_input_binding_get_gamepad_axis (binding))));
        put (map, "threshold", lrg_data_yaml_float (lrg_input_binding_get_threshold (binding)));
        put (map, "positive", lrg_data_yaml_boolean (lrg_input_binding_get_positive (binding)));
        break;

    default:
        break;
    }

    /* the old binding's extra keys, when it was the same kind of binding */
    if (old != NULL && type_name != NULL &&
        g_strcmp0 (yaml_mapping_get_string_member (old, "type"), type_name) == 0)
    {
        for (i = 0; i < yaml_mapping_get_size (old); i++)
        {
            const gchar *key = yaml_mapping_get_key (old, i);

            if (!g_strv_contains (known, key))
                yaml_mapping_set_member (map, key, yaml_mapping_get_value (old, i));
        }
    }

    return yaml_node_new_mapping (map);
}

/* the `bindings` list of @action; @old is the loaded action's list */
static YamlNode *
bindings_node (LrgInputAction *action,
               YamlSequence   *old)
{
    g_autoptr(YamlSequence) seq = yaml_sequence_new ();
    guint count = lrg_input_action_get_binding_count (action);
    guint i;

    for (i = 0; i < count; i++)
    {
        const LrgInputBinding *binding = lrg_input_action_get_binding (action, i);
        YamlMapping *old_map = NULL;
        g_autoptr(YamlNode) node = NULL;

        if (binding == NULL)
            continue;
        if (old != NULL && i < yaml_sequence_get_length (old))
            old_map = yaml_sequence_get_mapping_element (old, i);
        node = binding_node (binding, old_map);
        yaml_sequence_add_element (seq, node);
    }
    return yaml_node_new_sequence (seq);
}

/* one action: the loaded action's keys in place, `bindings` regenerated */
static YamlNode *
action_node (LrgInputAction *action,
             YamlMapping    *old)
{
    g_autoptr(YamlMapping) map = yaml_mapping_new ();
    YamlSequence *old_bindings = NULL;
    gboolean wrote_bindings = FALSE;
    guint i;

    if (old != NULL)
        old_bindings = yaml_mapping_get_sequence_member (old, "bindings");

    for (i = 0; old != NULL && i < yaml_mapping_get_size (old); i++)
    {
        const gchar *key = yaml_mapping_get_key (old, i);

        if (g_str_equal (key, "bindings"))
        {
            put (map, "bindings", bindings_node (action, old_bindings));
            wrote_bindings = TRUE;
        }
        else
        {
            yaml_mapping_set_member (map, key, yaml_mapping_get_value (old, i));
        }
    }
    if (!wrote_bindings)
        put (map, "bindings", bindings_node (action, old_bindings));

    return yaml_node_new_mapping (map);
}

/* the document: the loaded root's keys in place, `actions` regenerated */
static YamlNode *
map_to_yaml (LrgInputMap *self)
{
    LrgInputMapPrivate *priv = lrg_input_map_get_instance_private (self);
    g_autoptr(YamlMapping) root = yaml_mapping_new ();
    g_autoptr(YamlMapping) actions = yaml_mapping_new ();
    g_autoptr(YamlNode) actions_node = NULL;
    YamlMapping *old_root = NULL;
    YamlMapping *old_actions = NULL;
    gboolean wrote_actions = FALSE;
    guint i;

    if (priv->source != NULL && yaml_node_get_node_type (priv->source) == YAML_NODE_MAPPING)
    {
        old_root = yaml_node_get_mapping (priv->source);
        old_actions = yaml_mapping_get_mapping_member (old_root, "actions");
    }

    for (i = 0; i < priv->order->len; i++)
    {
        const gchar *name = g_ptr_array_index (priv->order, i);
        YamlMapping *old = old_actions != NULL ? yaml_mapping_get_mapping_member (old_actions, name) : NULL;

        put (actions, name, action_node (g_hash_table_lookup (priv->actions, name), old));
    }
    actions_node = yaml_node_new_mapping (actions);

    for (i = 0; old_root != NULL && i < yaml_mapping_get_size (old_root); i++)
    {
        const gchar *key = yaml_mapping_get_key (old_root, i);

        if (g_str_equal (key, "actions"))
        {
            yaml_mapping_set_member (root, "actions", actions_node);
            wrote_actions = TRUE;
        }
        else
        {
            yaml_mapping_set_member (root, key, yaml_mapping_get_value (old_root, i));
        }
    }
    if (!wrote_actions)
        yaml_mapping_set_member (root, "actions", actions_node);

    return yaml_node_new_mapping (root);
}

gchar *
lrg_input_map_save_to_data (LrgInputMap  *self,
                            gsize        *length,
                            GError      **error)
{
    g_autoptr(YamlNode) root = NULL;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    root = map_to_yaml (self);
    return lrg_data_yaml_to_data (root, length, error);
}

/*
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
    g_autoptr(YamlNode) root = NULL;
    gboolean ret;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    root = map_to_yaml (self);
    ret = lrg_data_yaml_to_file (root, path, error);

    if (ret)
    {
        lrg_info (LRG_LOG_DOMAIN_INPUT,
                  "Saved %u actions to %s",
                  lrg_input_map_get_action_count (self), path);
    }

    return ret;
}
