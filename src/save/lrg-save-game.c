/* lrg-save-game.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the save game representation.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SAVE

#include "lrg-save-game.h"
#include "lrg-save-context.h"
#include "../lrg-log.h"
#include <gio/gio.h>

struct _LrgSaveGame
{
    GObject parent_instance;

    gchar     *slot_name;
    gchar     *display_name;
    gchar     *path;
    GDateTime *timestamp;
    gdouble    playtime;
    guint      version;

    /* Custom metadata storage */
    GHashTable *custom_strings;
    GHashTable *custom_ints;
};

G_DEFINE_FINAL_TYPE (LrgSaveGame, lrg_save_game, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SLOT_NAME,
    PROP_DISPLAY_NAME,
    PROP_PATH,
    PROP_TIMESTAMP,
    PROP_PLAYTIME,
    PROP_VERSION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
lrg_save_game_finalize (GObject *object)
{
    LrgSaveGame *self = LRG_SAVE_GAME (object);

    g_clear_pointer (&self->slot_name, g_free);
    g_clear_pointer (&self->display_name, g_free);
    g_clear_pointer (&self->path, g_free);
    g_clear_pointer (&self->timestamp, g_date_time_unref);
    g_clear_pointer (&self->custom_strings, g_hash_table_unref);
    g_clear_pointer (&self->custom_ints, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_save_game_parent_class)->finalize (object);
}

static void
lrg_save_game_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgSaveGame *self = LRG_SAVE_GAME (object);

    switch (prop_id)
    {
    case PROP_SLOT_NAME:
        g_value_set_string (value, self->slot_name);
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string (value, self->display_name);
        break;
    case PROP_PATH:
        g_value_set_string (value, self->path);
        break;
    case PROP_TIMESTAMP:
        g_value_set_boxed (value, self->timestamp);
        break;
    case PROP_PLAYTIME:
        g_value_set_double (value, self->playtime);
        break;
    case PROP_VERSION:
        g_value_set_uint (value, self->version);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_save_game_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgSaveGame *self = LRG_SAVE_GAME (object);

    switch (prop_id)
    {
    case PROP_SLOT_NAME:
        g_free (self->slot_name);
        self->slot_name = g_value_dup_string (value);
        break;
    case PROP_DISPLAY_NAME:
        g_free (self->display_name);
        self->display_name = g_value_dup_string (value);
        break;
    case PROP_PATH:
        g_free (self->path);
        self->path = g_value_dup_string (value);
        break;
    case PROP_TIMESTAMP:
        g_clear_pointer (&self->timestamp, g_date_time_unref);
        self->timestamp = g_value_dup_boxed (value);
        break;
    case PROP_PLAYTIME:
        self->playtime = g_value_get_double (value);
        break;
    case PROP_VERSION:
        self->version = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_save_game_class_init (LrgSaveGameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_save_game_finalize;
    object_class->get_property = lrg_save_game_get_property;
    object_class->set_property = lrg_save_game_set_property;

    /**
     * LrgSaveGame:slot-name:
     *
     * The slot identifier for this save.
     */
    properties[PROP_SLOT_NAME] =
        g_param_spec_string ("slot-name",
                             "Slot Name",
                             "The slot identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveGame:display-name:
     *
     * The user-visible display name.
     */
    properties[PROP_DISPLAY_NAME] =
        g_param_spec_string ("display-name",
                             "Display Name",
                             "The user-visible display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveGame:path:
     *
     * The file path for the save file.
     */
    properties[PROP_PATH] =
        g_param_spec_string ("path",
                             "Path",
                             "The file path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveGame:timestamp:
     *
     * The timestamp when this save was created/modified.
     */
    properties[PROP_TIMESTAMP] =
        g_param_spec_boxed ("timestamp",
                            "Timestamp",
                            "When the save was created/modified",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveGame:playtime:
     *
     * The total playtime in seconds.
     */
    properties[PROP_PLAYTIME] =
        g_param_spec_double ("playtime",
                             "Playtime",
                             "Total playtime in seconds",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveGame:version:
     *
     * The save format version.
     */
    properties[PROP_VERSION] =
        g_param_spec_uint ("version",
                           "Version",
                           "Save format version",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_save_game_init (LrgSaveGame *self)
{
    self->slot_name = NULL;
    self->display_name = NULL;
    self->path = NULL;
    self->timestamp = NULL;
    self->playtime = 0.0;
    self->version = 1;
    self->custom_strings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, g_free);
    self->custom_ints = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_save_game_new:
 * @slot_name: the slot identifier
 *
 * Creates a new save game with the given slot name.
 *
 * Returns: (transfer full): A new #LrgSaveGame
 */
LrgSaveGame *
lrg_save_game_new (const gchar *slot_name)
{
    g_return_val_if_fail (slot_name != NULL, NULL);

    return g_object_new (LRG_TYPE_SAVE_GAME,
                         "slot-name", slot_name,
                         NULL);
}

/**
 * lrg_save_game_new_from_file:
 * @path: path to the save file
 * @error: (optional): return location for a #GError
 *
 * Loads save game metadata from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveGame
 */
LrgSaveGame *
lrg_save_game_new_from_file (const gchar  *path,
                              GError      **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    LrgSaveGame               *self;
    g_autofree gchar          *slot_name = NULL;
    const gchar               *display_name;
    const gchar               *timestamp_str;
    gdouble                    playtime;
    guint                      version;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    context = lrg_save_context_new_from_file (path, error);
    if (context == NULL)
    {
        return NULL;
    }

    /* Extract slot name from filename */
    slot_name = g_path_get_basename (path);
    if (g_str_has_suffix (slot_name, ".yaml"))
    {
        slot_name[strlen (slot_name) - 5] = '\0';
    }
    else if (g_str_has_suffix (slot_name, ".yml"))
    {
        slot_name[strlen (slot_name) - 4] = '\0';
    }

    self = lrg_save_game_new (slot_name);
    if (self == NULL)
    {
        return NULL;
    }
    self->path = g_strdup (path);

    /* Read metadata from the context */
    if (lrg_save_context_enter_section (context, "metadata"))
    {
        display_name = lrg_save_context_read_string (context, "display_name", NULL);
        if (display_name != NULL)
        {
            self->display_name = g_strdup (display_name);
        }

        timestamp_str = lrg_save_context_read_string (context, "timestamp", NULL);
        if (timestamp_str != NULL)
        {
            self->timestamp = g_date_time_new_from_iso8601 (timestamp_str, NULL);
        }

        playtime = lrg_save_context_read_double (context, "playtime", 0.0);
        self->playtime = playtime;

        lrg_save_context_leave_section (context);
    }

    version = lrg_save_context_get_version (context);
    self->version = version;

    lrg_log_debug ("Loaded save game metadata from %s", path);

    return self;
}

/* ==========================================================================
 * Metadata
 * ========================================================================== */

/**
 * lrg_save_game_get_slot_name:
 * @self: a #LrgSaveGame
 *
 * Gets the slot identifier for this save.
 *
 * Returns: (transfer none): the slot name
 */
const gchar *
lrg_save_game_get_slot_name (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), NULL);

    return self->slot_name;
}

/**
 * lrg_save_game_get_display_name:
 * @self: a #LrgSaveGame
 *
 * Gets the display name for this save.
 *
 * Returns: (transfer none) (nullable): the display name
 */
const gchar *
lrg_save_game_get_display_name (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), NULL);

    return self->display_name;
}

/**
 * lrg_save_game_set_display_name:
 * @self: a #LrgSaveGame
 * @name: (nullable): the display name to set
 *
 * Sets the display name for this save.
 */
void
lrg_save_game_set_display_name (LrgSaveGame *self,
                                const gchar *name)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));

    if (g_strcmp0 (self->display_name, name) == 0)
        return;

    g_free (self->display_name);
    self->display_name = g_strdup (name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISPLAY_NAME]);
}

/**
 * lrg_save_game_get_timestamp:
 * @self: a #LrgSaveGame
 *
 * Gets the timestamp when this save was created or last modified.
 *
 * Returns: (transfer none) (nullable): the timestamp
 */
GDateTime *
lrg_save_game_get_timestamp (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), NULL);

    return self->timestamp;
}

/**
 * lrg_save_game_set_timestamp:
 * @self: a #LrgSaveGame
 * @timestamp: (nullable): the timestamp to set
 *
 * Sets the timestamp for this save.
 */
void
lrg_save_game_set_timestamp (LrgSaveGame *self,
                             GDateTime   *timestamp)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));

    g_clear_pointer (&self->timestamp, g_date_time_unref);
    self->timestamp = timestamp != NULL ? g_date_time_ref (timestamp) : NULL;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMESTAMP]);
}

/**
 * lrg_save_game_update_timestamp:
 * @self: a #LrgSaveGame
 *
 * Updates the timestamp to the current time.
 */
void
lrg_save_game_update_timestamp (LrgSaveGame *self)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));

    g_clear_pointer (&self->timestamp, g_date_time_unref);
    self->timestamp = g_date_time_new_now_utc ();

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMESTAMP]);
}

/**
 * lrg_save_game_get_playtime:
 * @self: a #LrgSaveGame
 *
 * Gets the total playtime in seconds.
 *
 * Returns: the playtime in seconds
 */
gdouble
lrg_save_game_get_playtime (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), 0.0);

    return self->playtime;
}

/**
 * lrg_save_game_set_playtime:
 * @self: a #LrgSaveGame
 * @playtime: the playtime in seconds
 *
 * Sets the total playtime.
 */
void
lrg_save_game_set_playtime (LrgSaveGame *self,
                            gdouble      playtime)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));
    g_return_if_fail (playtime >= 0.0);

    if (self->playtime == playtime)
        return;

    self->playtime = playtime;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYTIME]);
}

/**
 * lrg_save_game_add_playtime:
 * @self: a #LrgSaveGame
 * @seconds: seconds to add
 *
 * Adds to the total playtime.
 */
void
lrg_save_game_add_playtime (LrgSaveGame *self,
                            gdouble      seconds)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));
    g_return_if_fail (seconds >= 0.0);

    self->playtime += seconds;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYTIME]);
}

/* ==========================================================================
 * File Path
 * ========================================================================== */

/**
 * lrg_save_game_get_path:
 * @self: a #LrgSaveGame
 *
 * Gets the file path for this save.
 *
 * Returns: (transfer none) (nullable): the file path
 */
const gchar *
lrg_save_game_get_path (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), NULL);

    return self->path;
}

/**
 * lrg_save_game_set_path:
 * @self: a #LrgSaveGame
 * @path: (nullable): the file path to set
 *
 * Sets the file path for this save.
 */
void
lrg_save_game_set_path (LrgSaveGame *self,
                        const gchar *path)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));

    if (g_strcmp0 (self->path, path) == 0)
        return;

    g_free (self->path);
    self->path = g_strdup (path);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PATH]);
}

/**
 * lrg_save_game_exists:
 * @self: a #LrgSaveGame
 *
 * Checks if the save file exists on disk.
 *
 * Returns: %TRUE if the file exists
 */
gboolean
lrg_save_game_exists (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), FALSE);

    if (self->path == NULL)
        return FALSE;

    return g_file_test (self->path, G_FILE_TEST_EXISTS);
}

/* ==========================================================================
 * Version
 * ========================================================================== */

/**
 * lrg_save_game_get_version:
 * @self: a #LrgSaveGame
 *
 * Gets the save format version.
 *
 * Returns: the version number
 */
guint
lrg_save_game_get_version (LrgSaveGame *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), 0);

    return self->version;
}

/**
 * lrg_save_game_set_version:
 * @self: a #LrgSaveGame
 * @version: the version number
 *
 * Sets the save format version.
 */
void
lrg_save_game_set_version (LrgSaveGame *self,
                           guint        version)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));

    if (self->version == version)
        return;

    self->version = version;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERSION]);
}

/* ==========================================================================
 * Custom Data
 * ========================================================================== */

/**
 * lrg_save_game_set_custom_string:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @value: (nullable): the string value
 *
 * Sets a custom string value in the save metadata.
 */
void
lrg_save_game_set_custom_string (LrgSaveGame *self,
                                 const gchar *key,
                                 const gchar *value)
{
    g_return_if_fail (LRG_IS_SAVE_GAME (self));
    g_return_if_fail (key != NULL);

    if (value != NULL)
    {
        g_hash_table_insert (self->custom_strings,
                             g_strdup (key),
                             g_strdup (value));
    }
    else
    {
        g_hash_table_remove (self->custom_strings, key);
    }
}

/**
 * lrg_save_game_get_custom_string:
 * @self: a #LrgSaveGame
 * @key: the key name
 *
 * Gets a custom string value from the save metadata.
 *
 * Returns: (transfer none) (nullable): the string value
 */
const gchar *
lrg_save_game_get_custom_string (LrgSaveGame *self,
                                 const gchar *key)
{
    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->custom_strings, key);
}

/**
 * lrg_save_game_set_custom_int:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @value: the integer value
 *
 * Sets a custom integer value in the save metadata.
 */
void
lrg_save_game_set_custom_int (LrgSaveGame *self,
                              const gchar *key,
                              gint64       value)
{
    gint64 *stored_value;

    g_return_if_fail (LRG_IS_SAVE_GAME (self));
    g_return_if_fail (key != NULL);

    stored_value = g_new (gint64, 1);
    *stored_value = value;

    g_hash_table_insert (self->custom_ints,
                         g_strdup (key),
                         stored_value);
}

/**
 * lrg_save_game_get_custom_int:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @default_value: default if not found
 *
 * Gets a custom integer value from the save metadata.
 *
 * Returns: the integer value, or @default_value
 */
gint64
lrg_save_game_get_custom_int (LrgSaveGame *self,
                              const gchar *key,
                              gint64       default_value)
{
    gint64 *stored_value;

    g_return_val_if_fail (LRG_IS_SAVE_GAME (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    stored_value = g_hash_table_lookup (self->custom_ints, key);
    if (stored_value == NULL)
        return default_value;

    return *stored_value;
}
