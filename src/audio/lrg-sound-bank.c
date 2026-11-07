/* lrg-sound-bank.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"
#include "lrg-sound-bank.h"
#include "lrg-wave-data.h"
#include "../core/lrg-asset-pack.h"
#include "../core/lrg-data-loader.h"
#include "../lrg-log.h"

#include <yaml-glib.h>

/* Private structure */
struct _LrgSoundBank
{
    GObject parent_instance;

    gchar      *name;
    gchar      *base_path;
    GHashTable *sounds;     /* gchar* -> GrlSound* */
    gfloat      volume;

    /* the manifest: what save_to_data writes */
    gchar      *base_path_text;  /* base_path as the manifest gives it */
    GHashTable *files;           /* name -> file */
    GPtrArray  *file_order;      /* names, manifest order */
};

G_DEFINE_FINAL_TYPE (LrgSoundBank, lrg_sound_bank, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_BASE_PATH,
    PROP_VOLUME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_sound_bank_finalize (GObject *object)
{
    LrgSoundBank *self = LRG_SOUND_BANK (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->base_path, g_free);
    g_clear_pointer (&self->sounds, g_hash_table_unref);
    g_clear_pointer (&self->base_path_text, g_free);
    g_clear_pointer (&self->files, g_hash_table_unref);
    g_clear_pointer (&self->file_order, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_sound_bank_parent_class)->finalize (object);
}

static void
lrg_sound_bank_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgSoundBank *self = LRG_SOUND_BANK (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_BASE_PATH:
        g_value_set_string (value, self->base_path);
        break;
    case PROP_VOLUME:
        g_value_set_float (value, self->volume);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sound_bank_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgSoundBank *self = LRG_SOUND_BANK (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_BASE_PATH:
        lrg_sound_bank_set_base_path (self, g_value_get_string (value));
        break;
    case PROP_VOLUME:
        lrg_sound_bank_set_volume (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sound_bank_class_init (LrgSoundBankClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_sound_bank_finalize;
    object_class->get_property = lrg_sound_bank_get_property;
    object_class->set_property = lrg_sound_bank_set_property;

    /**
     * LrgSoundBank:name:
     *
     * The bank name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "The bank name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSoundBank:base-path:
     *
     * The base path for loading sound files.
     */
    properties[PROP_BASE_PATH] =
        g_param_spec_string ("base-path",
                             "Base Path",
                             "The base path for loading sound files",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSoundBank:volume:
     *
     * The volume level for all sounds in the bank.
     */
    properties[PROP_VOLUME] =
        g_param_spec_float ("volume",
                            "Volume",
                            "The volume level for all sounds",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sound_bank_init (LrgSoundBank *self)
{
    self->sounds = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           g_object_unref);
    self->volume = 1.0f;
    self->files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->file_order = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
 * lrg_sound_bank_new:
 * @name: the bank name (e.g., "player", "ui")
 *
 * Creates a new empty sound bank.
 *
 * Returns: (transfer full): A new #LrgSoundBank
 */
LrgSoundBank *
lrg_sound_bank_new (const gchar *name)
{
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_SOUND_BANK,
                         "name", name,
                         NULL);
}

/*
 * Builds a bank from a parsed manifest. @source names it in messages;
 * @manifest_dir (nullable) is what relative paths start from; with
 * @manifest_only no audio is decoded.
 */
static LrgSoundBank *
bank_from_parser (YamlParser   *parser,
                  const gchar  *source,
                  const gchar  *manifest_dir,
                  gboolean      manifest_only,
                  GError      **error)
{
    g_autoptr(LrgSoundBank) self = NULL;
    YamlNode *root;
    YamlMapping *mapping;
    const gchar *name;
    const gchar *base_path_str;

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Empty manifest file: %s", source);
        return NULL;
    }

    mapping = yaml_node_get_mapping (root);
    if (mapping == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Manifest root must be a mapping: %s", source);
        return NULL;
    }

    /* Get required 'name' field */
    name = yaml_mapping_get_string_member (mapping, "name");
    if (name == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Manifest missing 'name' field: %s", source);
        return NULL;
    }

    /* Create the bank */
    self = lrg_sound_bank_new (name);

    /* Get optional 'base_path' field, relative to the manifest */
    base_path_str = yaml_mapping_get_string_member (mapping, "base_path");
    if (base_path_str != NULL)
    {
        if (g_path_is_absolute (base_path_str) || manifest_dir == NULL)
        {
            lrg_sound_bank_set_base_path (self, base_path_str);
        }
        else
        {
            g_autofree gchar *full_path = g_build_filename (manifest_dir, base_path_str, NULL);
            lrg_sound_bank_set_base_path (self, full_path);
        }
    }
    else
    {
        lrg_sound_bank_set_base_path (self, manifest_dir);
    }

    /* the manifest's own text is what gets written back */
    g_free (self->base_path_text);
    self->base_path_text = g_strdup (base_path_str);

    /* Get optional 'volume' field (0..1) */
    if (yaml_mapping_has_member (mapping, "volume"))
    {
        YamlNode *node = yaml_mapping_get_member (mapping, "volume");
        const gchar *text = node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR ?
                            yaml_node_get_scalar (node) : NULL;
        gchar *end = NULL;
        gdouble volume = text != NULL ? g_ascii_strtod (text, &end) : -1.0;

        if (text == NULL || end == text || *end != '\0' || !(volume >= 0.0 && volume <= 1.0))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Manifest 'volume' must be a number from 0 to 1: %s", source);
            return NULL;
        }
        lrg_sound_bank_set_volume (self, (gfloat)volume);
    }

    /* Load sounds from 'sounds' mapping, in manifest order */
    if (yaml_mapping_has_member (mapping, "sounds"))
    {
        YamlMapping *sounds_mapping;
        guint i;

        sounds_mapping = yaml_mapping_get_mapping_member (mapping, "sounds");
        for (i = 0; sounds_mapping != NULL && i < yaml_mapping_get_size (sounds_mapping); i++)
        {
            const gchar *sound_name = yaml_mapping_get_key (sounds_mapping, i);
            YamlNode *node = yaml_mapping_get_value (sounds_mapping, i);
            const gchar *sound_file = node != NULL ? yaml_node_get_string (node) : NULL;
            g_autoptr(GError) load_error = NULL;

            if (sound_name == NULL || sound_file == NULL)
                continue;

            /* the entry stays in the manifest even when its audio fails */
            lrg_sound_bank_set_file (self, sound_name, sound_file);
            if (manifest_only)
                continue;
            if (!lrg_sound_bank_load (self, sound_name, sound_file, &load_error))
            {
                lrg_log_warning ("Failed to load sound '%s': %s",
                                sound_name, load_error->message);
            }
        }
    }

    lrg_log_debug ("Loaded sound bank '%s' with %u sounds",
                  self->name, lrg_sound_bank_get_count (self));

    return g_steal_pointer (&self);
}

/*
 * lrg_sound_bank_new_from_file:
 * @manifest_path: path to the YAML manifest file
 * @error: return location for a #GError
 *
 * Loads a sound bank from a YAML manifest file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSoundBank, or %NULL on error
 */
LrgSoundBank *
lrg_sound_bank_new_from_file (const gchar  *manifest_path,
                               GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autofree gchar *manifest_dir = NULL;

    g_return_val_if_fail (manifest_path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Parse the manifest file */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, manifest_path, error))
        return NULL;

    /* Get manifest directory for relative paths */
    manifest_dir = g_path_get_dirname (manifest_path);
    return bank_from_parser (parser, manifest_path, manifest_dir, FALSE, error);
}

LrgSoundBank *
lrg_sound_bank_new_from_data (const gchar  *data,
                              gssize        length,
                              const gchar  *base_dir,
                              gboolean      manifest_only,
                              GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;

    g_return_val_if_fail (data != NULL || length == 0, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, error))
        return NULL;

    return bank_from_parser (parser, "<data>", base_dir, manifest_only, error);
}

/* ==========================================================================
 * Manifest
 * ========================================================================== */

void
lrg_sound_bank_set_file (LrgSoundBank *self,
                         const gchar  *name,
                         const gchar  *filename)
{
    guint i;

    g_return_if_fail (LRG_IS_SOUND_BANK (self));
    g_return_if_fail (name != NULL);

    if (filename == NULL)
    {
        if (g_hash_table_remove (self->files, name))
        {
            for (i = 0; i < self->file_order->len; i++)
            {
                if (g_str_equal (g_ptr_array_index (self->file_order, i), name))
                {
                    g_ptr_array_remove_index (self->file_order, i);
                    break;
                }
            }
        }
        return;
    }

    if (!g_hash_table_contains (self->files, name))
        g_ptr_array_add (self->file_order, g_strdup (name));
    g_hash_table_replace (self->files, g_strdup (name), g_strdup (filename));
}

const gchar *
lrg_sound_bank_get_file (LrgSoundBank *self,
                         const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->files, name);
}

gchar **
lrg_sound_bank_get_file_names (LrgSoundBank *self)
{
    GStrvBuilder *builder;
    gchar **names;
    guint i;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);

    builder = g_strv_builder_new ();
    for (i = 0; i < self->file_order->len; i++)
        g_strv_builder_add (builder, g_ptr_array_index (self->file_order, i));
    names = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return names;
}

/* the manifest of @self as a document tree */
static YamlNode *
bank_to_yaml (LrgSoundBank *self)
{
    g_autoptr(YamlMapping) root = yaml_mapping_new ();
    g_autoptr(YamlNode) name = NULL;
    guint i;

    name = lrg_data_yaml_string (self->name != NULL ? self->name : "");
    yaml_mapping_set_member (root, "name", name);
    if (self->base_path_text != NULL)
    {
        g_autoptr(YamlNode) base = lrg_data_yaml_string (self->base_path_text);

        yaml_mapping_set_member (root, "base_path", base);
    }
    if (self->volume != 1.0f)
    {
        g_autoptr(YamlNode) volume = lrg_data_yaml_float (self->volume);

        yaml_mapping_set_member (root, "volume", volume);
    }
    if (self->file_order->len > 0)
    {
        g_autoptr(YamlMapping) sounds = yaml_mapping_new ();
        g_autoptr(YamlNode) sounds_node = NULL;

        for (i = 0; i < self->file_order->len; i++)
        {
            const gchar *sound = g_ptr_array_index (self->file_order, i);
            g_autoptr(YamlNode) file = lrg_data_yaml_string (g_hash_table_lookup (self->files, sound));

            yaml_mapping_set_member (sounds, sound, file);
        }
        sounds_node = yaml_node_new_mapping (sounds);
        yaml_mapping_set_member (root, "sounds", sounds_node);
    }
    return yaml_node_new_mapping (root);
}

gchar *
lrg_sound_bank_save_to_data (LrgSoundBank  *self,
                             gsize         *length,
                             GError       **error)
{
    g_autoptr(YamlNode) root = NULL;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    root = bank_to_yaml (self);
    return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_sound_bank_save_to_file (LrgSoundBank  *self,
                             const gchar   *path,
                             GError       **error)
{
    g_autoptr(YamlNode) root = NULL;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    root = bank_to_yaml (self);
    return lrg_data_yaml_to_file (root, path, error);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/*
 * lrg_sound_bank_get_name:
 * @self: a #LrgSoundBank
 *
 * Gets the bank name.
 *
 * Returns: (transfer none): the bank name
 */
const gchar *
lrg_sound_bank_get_name (LrgSoundBank *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);

    return self->name;
}

/*
 * lrg_sound_bank_get_base_path:
 * @self: a #LrgSoundBank
 *
 * Gets the base path for sound files.
 *
 * Returns: (transfer none) (nullable): the base path, or %NULL if not set
 */
const gchar *
lrg_sound_bank_get_base_path (LrgSoundBank *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);

    return self->base_path;
}

/*
 * lrg_sound_bank_set_base_path:
 * @self: a #LrgSoundBank
 * @path: (nullable): the base path for sound files
 *
 * Sets the base path for loading sound files.
 */
void
lrg_sound_bank_set_base_path (LrgSoundBank *self,
                               const gchar  *path)
{
    g_return_if_fail (LRG_IS_SOUND_BANK (self));

    /* what the manifest writer writes from now on */
    g_free (self->base_path_text);
    self->base_path_text = g_strdup (path);

    if (g_strcmp0 (self->base_path, path) != 0)
    {
        g_free (self->base_path);
        self->base_path = g_strdup (path);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_PATH]);
    }
}

/* ==========================================================================
 * Sound Management
 * ========================================================================== */

/*
 * lrg_sound_bank_add:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @sound: the sound to add
 *
 * Adds a sound to the bank.
 */
void
lrg_sound_bank_add (LrgSoundBank *self,
                     const gchar  *name,
                     GrlSound     *sound)
{
    g_return_if_fail (LRG_IS_SOUND_BANK (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (GRL_IS_SOUND (sound));

    /* Apply current bank volume */
    grl_sound_set_volume (sound, self->volume);

    g_hash_table_insert (self->sounds,
                         g_strdup (name),
                         g_object_ref (sound));

    lrg_log_debug ("Added sound '%s' to bank '%s'", name, self->name);
}

/*
 * lrg_sound_bank_load:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @filename: path to the sound file (relative to base_path)
 * @error: return location for a #GError
 *
 * Loads a sound from a file and adds it to the bank.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_sound_bank_load (LrgSoundBank  *self,
                      const gchar   *name,
                      const gchar   *filename,
                      GError       **error)
{
    g_autoptr(GrlSound) sound = NULL;
    g_autofree gchar *full_path = NULL;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Build full path */
    if (g_path_is_absolute (filename))
    {
        full_path = g_strdup (filename);
    }
    else if (self->base_path != NULL)
    {
        full_path = g_build_filename (self->base_path, filename, NULL);
    }
    else
    {
        full_path = g_strdup (filename);
    }

    /* Load the sound */
    sound = grl_sound_new_from_file (full_path, error);
    if (sound == NULL)
        return FALSE;

    lrg_sound_bank_add (self, name, sound);
    lrg_sound_bank_set_file (self, name, filename);
    return TRUE;
}

/*
 * lrg_sound_bank_add_from_wave:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @wave: the wave data to convert to a sound
 *
 * Adds a sound created from wave data.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_sound_bank_add_from_wave (LrgSoundBank *self,
                               const gchar  *name,
                               LrgWaveData  *wave)
{
    GrlSound *sound;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), FALSE);

    if (!lrg_wave_data_is_valid (wave))
    {
        lrg_log_debug ("Cannot add invalid wave data as sound '%s'", name);
        return FALSE;
    }

    sound = lrg_wave_data_to_sound (wave);
    if (sound == NULL)
    {
        lrg_log_debug ("Failed to convert wave data to sound '%s'", name);
        return FALSE;
    }

    lrg_sound_bank_add (self, name, sound);
    g_object_unref (sound);

    return TRUE;
}

/*
 * lrg_sound_bank_load_from_memory:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @file_type: file type extension (e.g., ".wav", ".ogg")
 * @data: (array length=data_size): audio file data in memory
 * @data_size: size of @data in bytes
 * @error: return location for a #GError
 *
 * Loads a sound from a memory buffer containing audio file data.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_sound_bank_load_from_memory (LrgSoundBank  *self,
                                  const gchar   *name,
                                  const gchar   *file_type,
                                  const guint8  *data,
                                  gsize          data_size,
                                  GError       **error)
{
    g_autoptr(GrlSound) sound = NULL;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (file_type != NULL, FALSE);
    g_return_val_if_fail (data != NULL, FALSE);
    g_return_val_if_fail (data_size > 0, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    sound = grl_sound_new_from_memory (file_type, data, data_size, error);
    if (sound == NULL)
        return FALSE;

    lrg_sound_bank_add (self, name, sound);
    return TRUE;
}

/*
 * lrg_sound_bank_load_from_resource:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @pack: the resource pack to load from
 * @resource_name: the name of the resource in the pack
 * @error: return location for a #GError
 *
 * Loads a sound from a resource pack (rres file).
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_sound_bank_load_from_resource (LrgSoundBank  *self,
                                    const gchar   *name,
                                    LrgAssetPack  *pack,
                                    const gchar   *resource_name,
                                    GError       **error)
{
    g_autoptr(GrlSound) sound = NULL;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (LRG_IS_ASSET_PACK (pack), FALSE);
    g_return_val_if_fail (resource_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    sound = lrg_asset_pack_load_sound (pack, resource_name, error);
    if (sound == NULL)
        return FALSE;

    lrg_sound_bank_add (self, name, sound);
    return TRUE;
}

/*
 * lrg_sound_bank_add_alias:
 * @self: a #LrgSoundBank
 * @alias: the alias name
 * @source: the source sound name to alias
 *
 * Creates an alias for an existing sound in the bank.
 *
 * Returns: %TRUE if the source sound exists and alias was created
 */
gboolean
lrg_sound_bank_add_alias (LrgSoundBank *self,
                           const gchar  *alias,
                           const gchar  *source)
{
    GrlSound *sound;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (alias != NULL, FALSE);
    g_return_val_if_fail (source != NULL, FALSE);

    sound = lrg_sound_bank_get (self, source);
    if (sound == NULL)
    {
        lrg_log_debug ("Cannot create alias '%s': source sound '%s' not found",
                       alias, source);
        return FALSE;
    }

    /* Add the same sound under a different name */
    lrg_sound_bank_add (self, alias, sound);

    lrg_log_debug ("Created alias '%s' -> '%s' in bank '%s'",
                  alias, source, self->name);
    return TRUE;
}

/*
 * lrg_sound_bank_remove:
 * @self: a #LrgSoundBank
 * @name: the sound name to remove
 *
 * Removes a sound from the bank.
 *
 * Returns: %TRUE if the sound was found and removed
 */
gboolean
lrg_sound_bank_remove (LrgSoundBank *self,
                        const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* the manifest entry goes too (an entry alone also counts) */
    if (g_hash_table_contains (self->files, name))
    {
        lrg_sound_bank_set_file (self, name, NULL);
        g_hash_table_remove (self->sounds, name);
        return TRUE;
    }
    return g_hash_table_remove (self->sounds, name);
}

/*
 * lrg_sound_bank_get:
 * @self: a #LrgSoundBank
 * @name: the sound name
 *
 * Gets a sound from the bank by name.
 *
 * Returns: (transfer none) (nullable): the sound, or %NULL if not found
 */
GrlSound *
lrg_sound_bank_get (LrgSoundBank *self,
                     const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->sounds, name);
}

/*
 * lrg_sound_bank_contains:
 * @self: a #LrgSoundBank
 * @name: the sound name
 *
 * Checks if the bank contains a sound with the given name.
 *
 * Returns: %TRUE if the sound exists
 */
gboolean
lrg_sound_bank_contains (LrgSoundBank *self,
                          const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->sounds, name);
}

/*
 * lrg_sound_bank_get_count:
 * @self: a #LrgSoundBank
 *
 * Gets the number of sounds in the bank.
 *
 * Returns: the sound count
 */
guint
lrg_sound_bank_get_count (LrgSoundBank *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), 0);

    return g_hash_table_size (self->sounds);
}

/*
 * lrg_sound_bank_get_names:
 * @self: a #LrgSoundBank
 *
 * Gets a list of all sound names in the bank.
 *
 * Returns: (transfer container) (element-type utf8): list of sound names
 */
GList *
lrg_sound_bank_get_names (LrgSoundBank *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), NULL);

    return g_hash_table_get_keys (self->sounds);
}

/*
 * lrg_sound_bank_clear:
 * @self: a #LrgSoundBank
 *
 * Removes all sounds from the bank.
 */
void
lrg_sound_bank_clear (LrgSoundBank *self)
{
    g_return_if_fail (LRG_IS_SOUND_BANK (self));

    g_hash_table_remove_all (self->sounds);
    g_hash_table_remove_all (self->files);
    g_ptr_array_set_size (self->file_order, 0);
}

/* ==========================================================================
 * Playback
 * ========================================================================== */

/*
 * lrg_sound_bank_play:
 * @self: a #LrgSoundBank
 * @name: the sound name to play
 *
 * Plays a sound from the bank by name.
 *
 * Returns: %TRUE if the sound was found and played
 */
gboolean
lrg_sound_bank_play (LrgSoundBank *self,
                      const gchar  *name)
{
    GrlSound *sound;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    sound = lrg_sound_bank_get (self, name);
    if (sound == NULL)
    {
        lrg_log_warning ("Sound '%s' not found in bank '%s'",
                        name, self->name);
        return FALSE;
    }

    grl_sound_play (sound);
    return TRUE;
}

/*
 * lrg_sound_bank_play_multi:
 * @self: a #LrgSoundBank
 * @name: the sound name to play
 *
 * Plays a sound allowing multiple overlapping instances.
 *
 * Returns: %TRUE if the sound was found and played
 */
gboolean
lrg_sound_bank_play_multi (LrgSoundBank *self,
                            const gchar  *name)
{
    GrlSound *sound;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    sound = lrg_sound_bank_get (self, name);
    if (sound == NULL)
    {
        lrg_log_warning ("Sound '%s' not found in bank '%s'",
                        name, self->name);
        return FALSE;
    }

    grl_sound_play_multi (sound);
    return TRUE;
}

/*
 * lrg_sound_bank_stop:
 * @self: a #LrgSoundBank
 * @name: the sound name to stop
 *
 * Stops a playing sound.
 *
 * Returns: %TRUE if the sound was found and stopped
 */
gboolean
lrg_sound_bank_stop (LrgSoundBank *self,
                      const gchar  *name)
{
    GrlSound *sound;

    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    sound = lrg_sound_bank_get (self, name);
    if (sound == NULL)
        return FALSE;

    grl_sound_stop (sound);
    grl_sound_stop_multi (sound);
    return TRUE;
}

static void
stop_sound_foreach (gpointer key     G_GNUC_UNUSED,
                    gpointer value,
                    gpointer user_data G_GNUC_UNUSED)
{
    GrlSound *sound = GRL_SOUND (value);
    grl_sound_stop (sound);
    grl_sound_stop_multi (sound);
}

/*
 * lrg_sound_bank_stop_all:
 * @self: a #LrgSoundBank
 *
 * Stops all playing sounds in the bank.
 */
void
lrg_sound_bank_stop_all (LrgSoundBank *self)
{
    g_return_if_fail (LRG_IS_SOUND_BANK (self));

    g_hash_table_foreach (self->sounds, stop_sound_foreach, NULL);
}

/* ==========================================================================
 * Volume Control
 * ========================================================================== */

static void
set_volume_foreach (gpointer key     G_GNUC_UNUSED,
                    gpointer value,
                    gpointer user_data)
{
    GrlSound *sound = GRL_SOUND (value);
    gfloat volume = *(gfloat *)user_data;
    grl_sound_set_volume (sound, volume);
}

/*
 * lrg_sound_bank_set_volume:
 * @self: a #LrgSoundBank
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the volume for all sounds in the bank.
 */
void
lrg_sound_bank_set_volume (LrgSoundBank *self,
                            gfloat        volume)
{
    g_return_if_fail (LRG_IS_SOUND_BANK (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume != volume)
    {
        self->volume = volume;
        g_hash_table_foreach (self->sounds, set_volume_foreach, &volume);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLUME]);
    }
}

/*
 * lrg_sound_bank_get_volume:
 * @self: a #LrgSoundBank
 *
 * Gets the current volume level for the bank.
 *
 * Returns: the volume level (0.0 to 1.0)
 */
gfloat
lrg_sound_bank_get_volume (LrgSoundBank *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BANK (self), 1.0f);

    return self->volume;
}
