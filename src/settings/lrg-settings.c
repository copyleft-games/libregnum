/* lrg-settings.c - Main settings container implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-settings.h"
#include <json-glib/json-glib.h>

/**
 * SECTION:lrg-settings
 * @title: LrgSettings
 * @short_description: Main settings container
 *
 * #LrgSettings is the main container for all game settings.
 * It manages multiple #LrgSettingsGroup instances (graphics, audio, etc.)
 * and provides serialization to/from YAML files.
 *
 * ## Default Groups
 *
 * The following groups are created by default:
 * - graphics: #LrgGraphicsSettings
 * - audio: #LrgAudioSettings
 *
 * ## Custom Groups
 *
 * Games can add custom settings groups with lrg_settings_add_group().
 *
 * ## File Format
 *
 * Settings are stored as YAML (via JSON intermediate):
 *
 * ```yaml
 * graphics:
 *   width: 1920
 *   height: 1080
 *   fullscreen_mode: 0
 *   vsync: true
 *
 * audio:
 *   master_volume: 0.8
 *   music_volume: 0.6
 * ```
 */

G_DEFINE_QUARK (lrg-settings-error-quark, lrg_settings_error)

struct _LrgSettings
{
    GObject parent_instance;

    /* Built-in settings groups */
    LrgGraphicsSettings *graphics;
    LrgAudioSettings *audio;

    /* All groups including custom ones */
    GHashTable *groups;
};

G_DEFINE_TYPE (LrgSettings, lrg_settings, G_TYPE_OBJECT)

/* Default singleton */
static LrgSettings *default_settings = NULL;

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * Forward settings group changes to our own changed signal.
 */
static void
on_group_changed (LrgSettingsGroup *group,
                  const gchar      *property_name,
                  LrgSettings      *self)
{
    const gchar *group_name;

    group_name = lrg_settings_group_get_group_name (group);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, group_name, property_name);
}

/*
 * Connect to a group's changed signal.
 */
static void
connect_group (LrgSettings      *self,
               LrgSettingsGroup *group)
{
    g_signal_connect_object (group, "changed",
                             G_CALLBACK (on_group_changed),
                             self, 0);
}

static void
lrg_settings_dispose (GObject *object)
{
    LrgSettings *self = LRG_SETTINGS (object);

    g_clear_object (&self->graphics);
    g_clear_object (&self->audio);
    g_clear_pointer (&self->groups, g_hash_table_unref);

    if (default_settings == self)
        default_settings = NULL;

    G_OBJECT_CLASS (lrg_settings_parent_class)->dispose (object);
}

static void
lrg_settings_class_init (LrgSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_settings_dispose;

    /**
     * LrgSettings::changed:
     * @self: the #LrgSettings
     * @group_name: name of the group that changed
     * @property_name: (nullable): name of the property that changed
     *
     * Emitted when any setting changes.
     */
    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_settings_init (LrgSettings *self)
{
    /* Create hash table for groups */
    self->groups = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_object_unref);

    /* Create default groups */
    self->graphics = lrg_graphics_settings_new ();
    self->audio = lrg_audio_settings_new ();

    /* Add to hash table */
    g_hash_table_insert (self->groups, g_strdup ("graphics"),
                         g_object_ref (self->graphics));
    g_hash_table_insert (self->groups, g_strdup ("audio"),
                         g_object_ref (self->audio));

    /* Connect change signals */
    connect_group (self, LRG_SETTINGS_GROUP (self->graphics));
    connect_group (self, LRG_SETTINGS_GROUP (self->audio));
}

/* Public API */

/**
 * lrg_settings_new:
 *
 * Creates a new #LrgSettings with default values for all groups.
 *
 * Returns: (transfer full): A new #LrgSettings
 */
LrgSettings *
lrg_settings_new (void)
{
    return g_object_new (LRG_TYPE_SETTINGS, NULL);
}

/**
 * lrg_settings_get_default:
 *
 * Gets the default settings singleton instance.
 *
 * Returns: (transfer none): The default #LrgSettings
 */
LrgSettings *
lrg_settings_get_default (void)
{
    if (default_settings == NULL)
        default_settings = lrg_settings_new ();

    return default_settings;
}

LrgGraphicsSettings *
lrg_settings_get_graphics (LrgSettings *self)
{
    g_return_val_if_fail (LRG_IS_SETTINGS (self), NULL);
    return self->graphics;
}

LrgAudioSettings *
lrg_settings_get_audio (LrgSettings *self)
{
    g_return_val_if_fail (LRG_IS_SETTINGS (self), NULL);
    return self->audio;
}

LrgSettingsGroup *
lrg_settings_get_group (LrgSettings *self,
                        const gchar *name)
{
    g_return_val_if_fail (LRG_IS_SETTINGS (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->groups, name);
}

gboolean
lrg_settings_add_group (LrgSettings      *self,
                        LrgSettingsGroup *group)
{
    const gchar *name;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);
    g_return_val_if_fail (LRG_IS_SETTINGS_GROUP (group), FALSE);

    name = lrg_settings_group_get_group_name (group);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Check if already exists */
    if (g_hash_table_contains (self->groups, name))
    {
        g_warning ("Settings group '%s' already exists", name);
        return FALSE;
    }

    g_hash_table_insert (self->groups, g_strdup (name), g_object_ref (group));
    connect_group (self, group);

    return TRUE;
}

GPtrArray *
lrg_settings_list_groups (LrgSettings *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), NULL);

    result = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (result, g_strdup (key));
    }

    return result;
}

/*
 * Internal: Convert settings to JSON object.
 */
static JsonObject *
settings_to_json (LrgSettings *self,
                  GError     **error)
{
    JsonObject *root;
    GHashTableIter iter;
    gpointer key, value;

    root = json_object_new ();

    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *group_name = key;
        LrgSettingsGroup *group = value;
        g_autoptr(GVariant) variant = NULL;
        JsonNode *node;

        variant = lrg_settings_group_serialize (group, error);
        if (!variant)
        {
            json_object_unref (root);
            return NULL;
        }

        node = json_gvariant_serialize (variant);
        json_object_set_member (root, group_name, node);
    }

    return root;
}

/*
 * Internal: Load settings from JSON object.
 */
static gboolean
settings_from_json (LrgSettings *self,
                    JsonObject  *root,
                    GError     **error)
{
    JsonObjectIter iter;
    const gchar *group_name;
    JsonNode *node;

    json_object_iter_init (&iter, root);
    while (json_object_iter_next (&iter, &group_name, &node))
    {
        LrgSettingsGroup *group;
        GVariant *variant;

        group = g_hash_table_lookup (self->groups, group_name);
        if (!group)
        {
            /* Unknown group - skip it */
            g_debug ("Skipping unknown settings group: %s", group_name);
            continue;
        }

        variant = json_gvariant_deserialize (node, "a{sv}", error);
        if (!variant)
        {
            return FALSE;
        }

        if (!lrg_settings_group_deserialize (group, variant, error))
        {
            g_variant_unref (variant);
            return FALSE;
        }

        g_variant_unref (variant);
    }

    return TRUE;
}

gboolean
lrg_settings_load (LrgSettings  *self,
                   const gchar  *path,
                   GError      **error)
{
    g_autoptr(JsonParser) parser = NULL;
    JsonNode *root_node;
    JsonObject *root;
    g_autoptr(GError) local_error = NULL;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /* Check if file exists - if not, that's okay, use defaults */
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_debug ("Settings file does not exist, using defaults: %s", path);
        return TRUE;
    }

    parser = json_parser_new ();

    if (!json_parser_load_from_file (parser, path, &local_error))
    {
        g_set_error (error, LRG_SETTINGS_ERROR, LRG_SETTINGS_ERROR_PARSE,
                     "Failed to parse settings file: %s", local_error->message);
        return FALSE;
    }

    root_node = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root_node))
    {
        g_set_error (error, LRG_SETTINGS_ERROR, LRG_SETTINGS_ERROR_INVALID,
                     "Settings file root must be an object");
        return FALSE;
    }

    root = json_node_get_object (root_node);
    return settings_from_json (self, root, error);
}

gboolean
lrg_settings_save (LrgSettings  *self,
                   const gchar  *path,
                   GError      **error)
{
    g_autoptr(JsonGenerator) generator = NULL;
    g_autoptr(JsonNode) root_node = NULL;
    JsonObject *root;
    g_autofree gchar *dir = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /* Ensure directory exists */
    dir = g_path_get_dirname (path);
    if (g_mkdir_with_parents (dir, 0755) != 0)
    {
        g_set_error (error, LRG_SETTINGS_ERROR, LRG_SETTINGS_ERROR_IO,
                     "Failed to create settings directory: %s", dir);
        return FALSE;
    }

    root = settings_to_json (self, error);
    if (!root)
        return FALSE;

    root_node = json_node_new (JSON_NODE_OBJECT);
    json_node_take_object (root_node, root);

    generator = json_generator_new ();
    json_generator_set_pretty (generator, TRUE);
    json_generator_set_indent (generator, 2);
    json_generator_set_root (generator, root_node);

    if (!json_generator_to_file (generator, path, error))
    {
        return FALSE;
    }

    /* Mark all groups as clean after save */
    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        lrg_settings_group_mark_clean (value);
    }

    return TRUE;
}

gboolean
lrg_settings_load_default_path (LrgSettings  *self,
                                const gchar  *app_id,
                                GError      **error)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);
    g_return_val_if_fail (app_id != NULL, FALSE);

    path = g_build_filename (g_get_user_config_dir (), app_id,
                             "settings.json", NULL);

    return lrg_settings_load (self, path, error);
}

gboolean
lrg_settings_save_default_path (LrgSettings  *self,
                                const gchar  *app_id,
                                GError      **error)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);
    g_return_val_if_fail (app_id != NULL, FALSE);

    path = g_build_filename (g_get_user_config_dir (), app_id,
                             "settings.json", NULL);

    return lrg_settings_save (self, path, error);
}

void
lrg_settings_apply_all (LrgSettings *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_SETTINGS (self));

    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        lrg_settings_group_apply (value);
    }
}

void
lrg_settings_reset_all (LrgSettings *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_SETTINGS (self));

    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        lrg_settings_group_reset (value);
    }
}

gboolean
lrg_settings_is_dirty (LrgSettings *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_SETTINGS (self), FALSE);

    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        if (lrg_settings_group_is_dirty (value))
            return TRUE;
    }

    return FALSE;
}
