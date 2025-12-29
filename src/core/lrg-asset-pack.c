/* lrg-asset-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "config.h"
#include "lrg-asset-pack.h"
#include "lrg-data-loader.h"
#include "../audio/lrg-wave-data.h"
#include "../lrg-log.h"

/* Private structure */
struct _LrgAssetPack
{
    GObject          parent_instance;

    GrlResourcePack *pack;          /* Underlying graylib resource pack */
    gchar           *filename;      /* Pack filename */
    GHashTable      *texture_cache; /* name -> GrlTexture */
    GHashTable      *sound_cache;   /* name -> GrlSound */
};

G_DEFINE_FINAL_TYPE (LrgAssetPack, lrg_asset_pack, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-asset-pack-error-quark, lrg_asset_pack_error)

enum
{
    PROP_0,
    PROP_FILENAME,
    PROP_RESOURCE_COUNT,
    PROP_VERSION,
    PROP_HAS_DIRECTORY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_asset_pack_finalize (GObject *object)
{
    LrgAssetPack *self = LRG_ASSET_PACK (object);

    g_clear_pointer (&self->filename, g_free);
    g_clear_pointer (&self->texture_cache, g_hash_table_unref);
    g_clear_pointer (&self->sound_cache, g_hash_table_unref);
    g_clear_object (&self->pack);

    G_OBJECT_CLASS (lrg_asset_pack_parent_class)->finalize (object);
}

static void
lrg_asset_pack_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgAssetPack *self = LRG_ASSET_PACK (object);

    switch (prop_id)
    {
    case PROP_FILENAME:
        g_value_set_string (value, self->filename);
        break;
    case PROP_RESOURCE_COUNT:
        g_value_set_uint (value, lrg_asset_pack_get_resource_count (self));
        break;
    case PROP_VERSION:
        g_value_set_uint (value, lrg_asset_pack_get_version (self));
        break;
    case PROP_HAS_DIRECTORY:
        g_value_set_boolean (value, lrg_asset_pack_has_directory (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_asset_pack_class_init (LrgAssetPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_asset_pack_finalize;
    object_class->get_property = lrg_asset_pack_get_property;

    /**
     * LrgAssetPack:filename:
     *
     * The filename of the resource pack.
     */
    properties[PROP_FILENAME] =
        g_param_spec_string ("filename",
                             "Filename",
                             "The filename of the resource pack",
                             NULL,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAssetPack:resource-count:
     *
     * The number of resources in the pack.
     */
    properties[PROP_RESOURCE_COUNT] =
        g_param_spec_uint ("resource-count",
                           "Resource Count",
                           "Number of resources in the pack",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgAssetPack:version:
     *
     * The rres format version.
     */
    properties[PROP_VERSION] =
        g_param_spec_uint ("version",
                           "Version",
                           "The rres format version",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgAssetPack:has-directory:
     *
     * Whether the pack has a central directory.
     */
    properties[PROP_HAS_DIRECTORY] =
        g_param_spec_boolean ("has-directory",
                              "Has Directory",
                              "Whether the pack has a central directory",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_asset_pack_init (LrgAssetPack *self)
{
    self->pack = NULL;
    self->filename = NULL;
    self->texture_cache = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   g_object_unref);
    self->sound_cache = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 g_object_unref);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_asset_pack_new:
 * @path: (type filename): path to the rres file
 * @error: (nullable): return location for a #GError
 *
 * Opens an rres resource pack file.
 *
 * Returns: (transfer full) (nullable): A new #LrgAssetPack, or %NULL on error
 */
LrgAssetPack *
lrg_asset_pack_new (const gchar  *path,
                     GError      **error)
{
    LrgAssetPack *self;
    g_autoptr(GrlResourcePack) pack = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    pack = grl_resource_pack_new (path, error);
    if (pack == NULL)
        return NULL;

    self = g_object_new (LRG_TYPE_ASSET_PACK, NULL);
    self->pack = g_steal_pointer (&pack);
    self->filename = g_strdup (path);

    lrg_log_debug ("Opened asset pack '%s' with %u resources",
                  path,
                  lrg_asset_pack_get_resource_count (self));

    return self;
}

/**
 * lrg_asset_pack_new_encrypted:
 * @path: (type filename): path to the rres file
 * @password: the decryption password
 * @error: (nullable): return location for a #GError
 *
 * Opens an encrypted rres resource pack file.
 *
 * Returns: (transfer full) (nullable): A new #LrgAssetPack, or %NULL on error
 */
LrgAssetPack *
lrg_asset_pack_new_encrypted (const gchar  *path,
                               const gchar  *password,
                               GError      **error)
{
    LrgAssetPack *self;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (password != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    self = lrg_asset_pack_new (path, error);
    if (self == NULL)
        return NULL;

    /* Set the cipher password for subsequent resource loads */
    grl_resource_pack_set_cipher_password (self->pack, password);

    return self;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_asset_pack_get_filename:
 * @self: a #LrgAssetPack
 *
 * Gets the filename of the resource pack.
 *
 * Returns: (transfer none): the filename
 */
const gchar *
lrg_asset_pack_get_filename (LrgAssetPack *self)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);

    return self->filename;
}

/**
 * lrg_asset_pack_get_resource_count:
 * @self: a #LrgAssetPack
 *
 * Gets the number of resource chunks in the pack.
 *
 * Returns: the resource count
 */
guint
lrg_asset_pack_get_resource_count (LrgAssetPack *self)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), 0);

    if (self->pack == NULL)
        return 0;

    return grl_resource_pack_get_chunk_count (self->pack);
}

/**
 * lrg_asset_pack_get_version:
 * @self: a #LrgAssetPack
 *
 * Gets the rres format version.
 *
 * Returns: the version number
 */
guint
lrg_asset_pack_get_version (LrgAssetPack *self)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), 0);

    if (self->pack == NULL)
        return 0;

    return grl_resource_pack_get_version (self->pack);
}

/**
 * lrg_asset_pack_has_directory:
 * @self: a #LrgAssetPack
 *
 * Checks if the pack has a central directory for name-based lookups.
 *
 * Returns: %TRUE if central directory is available
 */
gboolean
lrg_asset_pack_has_directory (LrgAssetPack *self)
{
    unsigned char raw;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), FALSE);

    if (self->pack == NULL)
        return FALSE;

    raw = grl_resource_pack_has_central_directory (self->pack);
    return raw != 0;
}

/* ==========================================================================
 * Directory Access
 * ========================================================================== */

/**
 * lrg_asset_pack_list_resources:
 * @self: a #LrgAssetPack
 *
 * Gets a list of all resource names in the pack.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): list of resource names
 */
GList *
lrg_asset_pack_list_resources (LrgAssetPack *self)
{
    GList *result = NULL;
    guint entry_count;
    guint i;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);

    if (!lrg_asset_pack_has_directory (self))
        return NULL;

    entry_count = grl_resource_pack_get_entry_count (self->pack);
    for (i = 0; i < entry_count; i++)
    {
        gchar *name = grl_resource_pack_get_entry_filename (self->pack, i);
        if (name != NULL)
        {
            result = g_list_prepend (result, name);
        }
    }

    return g_list_reverse (result);
}

/**
 * lrg_asset_pack_get_id:
 * @self: a #LrgAssetPack
 * @name: the resource name
 *
 * Gets the resource ID for a name.
 *
 * Returns: the resource ID, or 0 if not found
 */
guint32
lrg_asset_pack_get_id (LrgAssetPack *self,
                        const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    if (!lrg_asset_pack_has_directory (self))
        return 0;

    return grl_resource_pack_get_resource_id (self->pack, name);
}

/**
 * lrg_asset_pack_get_name:
 * @self: a #LrgAssetPack
 * @id: the resource ID
 *
 * Gets the resource name for an ID.
 *
 * Returns: (transfer full) (nullable): the resource name, or %NULL
 */
gchar *
lrg_asset_pack_get_name (LrgAssetPack *self,
                          guint32       id)
{
    guint entry_count;
    guint i;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);

    if (!lrg_asset_pack_has_directory (self))
        return NULL;

    /* Search through entries to find matching ID */
    entry_count = grl_resource_pack_get_entry_count (self->pack);
    for (i = 0; i < entry_count; i++)
    {
        guint32 entry_id = grl_resource_pack_get_entry_id (self->pack, i);
        if (entry_id == id)
        {
            return grl_resource_pack_get_entry_filename (self->pack, i);
        }
    }

    return NULL;
}

/**
 * lrg_asset_pack_contains:
 * @self: a #LrgAssetPack
 * @name: the resource name
 *
 * Checks if the pack contains a resource with the given name.
 *
 * Returns: %TRUE if the resource exists
 */
gboolean
lrg_asset_pack_contains (LrgAssetPack *self,
                          const gchar  *name)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return lrg_asset_pack_get_id (self, name) != 0;
}

/* ==========================================================================
 * Raw Data Loading
 * ========================================================================== */

/**
 * lrg_asset_pack_load_raw:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @size: (out): return location for data size
 * @error: (nullable): return location for a #GError
 *
 * Loads raw resource data by name.
 *
 * Returns: (transfer full) (array length=size) (nullable): the raw data bytes
 */
guint8 *
lrg_asset_pack_load_raw (LrgAssetPack  *self,
                          const gchar   *name,
                          gsize         *size,
                          GError       **error)
{
    guint32 id;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (size != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!lrg_asset_pack_has_directory (self))
    {
        g_set_error (error,
                     LRG_ASSET_PACK_ERROR,
                     LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND,
                     "Pack has no central directory, use load_raw_by_id instead");
        return NULL;
    }

    id = lrg_asset_pack_get_id (self, name);
    if (id == 0)
    {
        g_set_error (error,
                     LRG_ASSET_PACK_ERROR,
                     LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND,
                     "Resource '%s' not found in pack", name);
        return NULL;
    }

    return lrg_asset_pack_load_raw_by_id (self, id, size, error);
}

/**
 * lrg_asset_pack_load_raw_by_id:
 * @self: a #LrgAssetPack
 * @id: the resource ID
 * @size: (out): return location for data size
 * @error: (nullable): return location for a #GError
 *
 * Loads raw resource data by ID.
 *
 * Returns: (transfer full) (array length=size) (nullable): the raw data bytes
 */
guint8 *
lrg_asset_pack_load_raw_by_id (LrgAssetPack  *self,
                                guint32        id,
                                gsize         *size,
                                GError       **error)
{
    guint8 *data;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (size != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    data = grl_resource_pack_load_raw (self->pack, id, size, error);
    if (data == NULL)
    {
        return NULL;
    }

    return data;
}

/* ==========================================================================
 * Typed Asset Loading
 * ========================================================================== */

/* Helper to detect file type from resource name */
static const gchar *
get_file_type_from_name (const gchar *name)
{
    const gchar *ext;

    if (name == NULL)
        return NULL;

    ext = strrchr (name, '.');
    if (ext == NULL)
        return NULL;

    return ext;  /* Returns ".wav", ".ogg", etc. */
}

/**
 * lrg_asset_pack_load_texture:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads a texture from the pack.
 *
 * Returns: (transfer full) (nullable): A new #GrlTexture, or %NULL on error
 */
GrlTexture *
lrg_asset_pack_load_texture (LrgAssetPack  *self,
                              const gchar   *name,
                              GError       **error)
{
    GrlTexture *texture;
    gsize size;
    g_autofree guint8 *data = NULL;
    const gchar *file_type;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Check cache first */
    texture = g_hash_table_lookup (self->texture_cache, name);
    if (texture != NULL)
    {
        return g_object_ref (texture);
    }

    /* Load raw data */
    data = lrg_asset_pack_load_raw (self, name, &size, error);
    if (data == NULL)
        return NULL;

    /* Detect file type from extension */
    file_type = get_file_type_from_name (name);

    /* Load image from memory and create texture */
    {
        g_autoptr(GrlImage) image = grl_image_new_from_memory (file_type, data, size);
        if (image == NULL)
        {
            g_set_error (error,
                         LRG_ASSET_PACK_ERROR,
                         LRG_ASSET_PACK_ERROR_LOAD_FAILED,
                         "Failed to load image '%s' from pack", name);
            return NULL;
        }

        texture = grl_texture_new_from_image (image);
        if (texture == NULL)
        {
            g_set_error (error,
                         LRG_ASSET_PACK_ERROR,
                         LRG_ASSET_PACK_ERROR_LOAD_FAILED,
                         "Failed to create texture from image '%s'", name);
            return NULL;
        }
    }

    /* Cache and return */
    g_hash_table_insert (self->texture_cache, g_strdup (name), g_object_ref (texture));
    lrg_log_debug ("Loaded texture '%s' from pack", name);

    return texture;
}

/**
 * lrg_asset_pack_load_sound:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads a sound from the pack.
 *
 * Returns: (transfer full) (nullable): A new #GrlSound, or %NULL on error
 */
GrlSound *
lrg_asset_pack_load_sound (LrgAssetPack  *self,
                            const gchar   *name,
                            GError       **error)
{
    GrlSound *sound;
    guint32 id;
    const gchar *file_type;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Check cache first */
    sound = g_hash_table_lookup (self->sound_cache, name);
    if (sound != NULL)
    {
        return g_object_ref (sound);
    }

    /* Get resource ID */
    id = lrg_asset_pack_get_id (self, name);
    if (id == 0)
    {
        g_set_error (error,
                     LRG_ASSET_PACK_ERROR,
                     LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND,
                     "Sound '%s' not found in pack", name);
        return NULL;
    }

    /* Detect file type */
    file_type = get_file_type_from_name (name);

    /* Load sound from resource pack */
    sound = grl_sound_new_from_resource (self->pack, id, file_type, error);
    if (sound == NULL)
        return NULL;

    /* Cache and return */
    g_hash_table_insert (self->sound_cache, g_strdup (name), g_object_ref (sound));
    lrg_log_debug ("Loaded sound '%s' from pack", name);

    return sound;
}

/**
 * lrg_asset_pack_load_wave:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from the pack.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LrgWaveData *
lrg_asset_pack_load_wave (LrgAssetPack  *self,
                           const gchar   *name,
                           GError       **error)
{
    LrgWaveData *wave_data;
    gsize size;
    g_autofree guint8 *data = NULL;
    const gchar *file_type;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Load raw data */
    data = lrg_asset_pack_load_raw (self, name, &size, error);
    if (data == NULL)
        return NULL;

    /* Detect file type from extension */
    file_type = get_file_type_from_name (name);

    /* Create wave data from memory */
    wave_data = lrg_wave_data_new_from_memory (file_type, data, size, error);
    if (wave_data == NULL)
        return NULL;

    lrg_wave_data_set_name (wave_data, name);
    lrg_log_debug ("Loaded wave '%s' from pack", name);

    return wave_data;
}

/**
 * lrg_asset_pack_load_music:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads music from the pack.
 *
 * Returns: (transfer full) (nullable): A new #GrlMusic, or %NULL on error
 */
GrlMusic *
lrg_asset_pack_load_music (LrgAssetPack  *self,
                            const gchar   *name,
                            GError       **error)
{
    GrlMusic *music;
    gsize size;
    g_autofree guint8 *data = NULL;
    const gchar *file_type;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Load raw data - for music we need to load fully into memory
     * since streaming from rres is not supported */
    data = lrg_asset_pack_load_raw (self, name, &size, error);
    if (data == NULL)
        return NULL;

    /* Detect file type from extension */
    file_type = get_file_type_from_name (name);

    /* Load music from memory */
    music = grl_music_new_from_memory (file_type, data, size, error);
    if (music == NULL)
        return NULL;

    lrg_log_debug ("Loaded music '%s' from pack", name);

    return music;
}

/**
 * lrg_asset_pack_load_object:
 * @self: a #LrgAssetPack
 * @name: the resource name (should be a YAML file)
 * @loader: the data loader to use
 * @error: (nullable): return location for a #GError
 *
 * Loads a GObject from a YAML resource in the pack.
 *
 * Returns: (transfer full) (nullable): A new #GObject, or %NULL on error
 */
GObject *
lrg_asset_pack_load_object (LrgAssetPack   *self,
                             const gchar    *name,
                             LrgDataLoader  *loader,
                             GError        **error)
{
    GObject *object;
    gsize size;
    g_autofree guint8 *data = NULL;
    g_autofree gchar *yaml_str = NULL;

    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (LRG_IS_DATA_LOADER (loader), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Load raw data */
    data = lrg_asset_pack_load_raw (self, name, &size, error);
    if (data == NULL)
        return NULL;

    /* Convert to null-terminated string */
    yaml_str = g_strndup ((const gchar *)data, size);

    /* Parse YAML and create object */
    object = lrg_data_loader_load_data (loader, yaml_str, (gssize)size, error);
    if (object == NULL)
        return NULL;

    lrg_log_debug ("Loaded object '%s' from pack", name);

    return object;
}

/* ==========================================================================
 * Access Underlying
 * ========================================================================== */

/**
 * lrg_asset_pack_get_resource_pack:
 * @self: a #LrgAssetPack
 *
 * Gets the underlying #GrlResourcePack.
 *
 * Returns: (transfer none): the underlying #GrlResourcePack
 */
GrlResourcePack *
lrg_asset_pack_get_resource_pack (LrgAssetPack *self)
{
    g_return_val_if_fail (LRG_IS_ASSET_PACK (self), NULL);

    return self->pack;
}
