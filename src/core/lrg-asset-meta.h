/* lrg-asset-meta.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAssetMeta: the "<file>.meta" YAML sidecar of an asset.
 *
 * Every asset in a project may have a sidecar named after it with ".meta"
 * appended ("hero.png" -> "hero.png.meta"). The sidecar holds:
 *
 *  - guid:     the asset's stable identifier. New assets get a random UUID
 *              v4; assets that predate sidecars get the old path-derived
 *              identifier (sha256 of the path relative to the asset
 *              directory) so existing references keep resolving;
 *  - importer: which importer reads the asset ("texture", "model",
 *              "audio", "font", "script", "level", "prefab", ...);
 *  - settings: a mapping of importer settings (texture filter, model
 *              scale, audio mode, font size, ...).
 *
 * Keys the editor does not know (at the top level and inside settings) are
 * preserved verbatim on load/save, so a newer editor's sidecars survive a
 * round trip through an older one.
 *
 * Settings are reachable three ways: typed accessors for the well-known
 * keys (lrg_asset_meta_get_texture_filter() ...), scalar accessors by key
 * (lrg_asset_meta_get_setting_string() ...), and GVariant access
 * (lrg_asset_meta_get_setting_variant(), lrg_asset_meta_to_variant())
 * meant for IPC (asset.meta.get / asset.meta.set).
 *
 * Since 0.9.0 this reader is part of the always-built core module: the
 * runtime #LrgAssetManager reads the same sidecars to resolve `guid:`
 * references and to apply importer settings at load time, so a game built
 * with BUILD_EDITOR=0 sees what the editor shows.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_ASSET_META_SUFFIX:
 *
 * The suffix appended to an asset's file name to name its sidecar.
 */
#define LRG_ASSET_META_SUFFIX ".meta"

/**
 * LRG_ASSET_META_VERSION:
 *
 * The sidecar format version written to the "version" key.
 */
#define LRG_ASSET_META_VERSION (1)

/* ==========================================================================
 * Setting enums
 * ========================================================================== */

/**
 * LrgAssetTextureFilter:
 * @LRG_ASSET_TEXTURE_FILTER_POINT: nearest-neighbour sampling ("point")
 * @LRG_ASSET_TEXTURE_FILTER_BILINEAR: bilinear sampling ("bilinear")
 * @LRG_ASSET_TEXTURE_FILTER_TRILINEAR: trilinear sampling, needs mipmaps
 *   ("trilinear")
 *
 * The texture importer's "filter" setting.
 */
typedef enum
{
	LRG_ASSET_TEXTURE_FILTER_POINT,
	LRG_ASSET_TEXTURE_FILTER_BILINEAR,
	LRG_ASSET_TEXTURE_FILTER_TRILINEAR
} LrgAssetTextureFilter;

LRG_AVAILABLE_IN_ALL
GType lrg_asset_texture_filter_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASSET_TEXTURE_FILTER (lrg_asset_texture_filter_get_type ())

/**
 * LrgAssetTextureWrap:
 * @LRG_ASSET_TEXTURE_WRAP_REPEAT: tile the texture ("repeat")
 * @LRG_ASSET_TEXTURE_WRAP_CLAMP: clamp to the edge texels ("clamp")
 * @LRG_ASSET_TEXTURE_WRAP_MIRROR: tile with mirroring ("mirror")
 *
 * The texture importer's "wrap" setting.
 */
typedef enum
{
	LRG_ASSET_TEXTURE_WRAP_REPEAT,
	LRG_ASSET_TEXTURE_WRAP_CLAMP,
	LRG_ASSET_TEXTURE_WRAP_MIRROR
} LrgAssetTextureWrap;

LRG_AVAILABLE_IN_ALL
GType lrg_asset_texture_wrap_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASSET_TEXTURE_WRAP (lrg_asset_texture_wrap_get_type ())

/**
 * LrgAssetUpAxis:
 * @LRG_ASSET_UP_AXIS_Y: the source file is Y-up ("y")
 * @LRG_ASSET_UP_AXIS_Z: the source file is Z-up ("z"), rotated on import
 *
 * The model importer's "up-axis" setting.
 */
typedef enum
{
	LRG_ASSET_UP_AXIS_Y,
	LRG_ASSET_UP_AXIS_Z
} LrgAssetUpAxis;

LRG_AVAILABLE_IN_ALL
GType lrg_asset_up_axis_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASSET_UP_AXIS (lrg_asset_up_axis_get_type ())

/**
 * LrgAssetAudioMode:
 * @LRG_ASSET_AUDIO_MODE_STREAM: decode while playing ("stream"), for music
 * @LRG_ASSET_AUDIO_MODE_SAMPLE: decode fully into memory ("sample"), for
 *   short effects
 *
 * The audio importer's "mode" setting.
 */
typedef enum
{
	LRG_ASSET_AUDIO_MODE_STREAM,
	LRG_ASSET_AUDIO_MODE_SAMPLE
} LrgAssetAudioMode;

LRG_AVAILABLE_IN_ALL
GType lrg_asset_audio_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ASSET_AUDIO_MODE (lrg_asset_audio_mode_get_type ())

/* ==========================================================================
 * LrgAssetMeta
 * ========================================================================== */

#define LRG_TYPE_ASSET_META (lrg_asset_meta_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetMeta, lrg_asset_meta, LRG, ASSET_META, GObject)

/**
 * lrg_asset_meta_new:
 *
 * Creates an empty sidecar with a fresh random guid (UUID v4), the
 * "generic" importer and no settings.
 *
 * Returns: (transfer full): a new #LrgAssetMeta
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_new (void);

/**
 * lrg_asset_meta_new_for_asset:
 * @asset_path: an asset file name or path (only the extension is used)
 *
 * Creates a sidecar for a new asset: a fresh random guid and the importer
 * that matches the asset's type (see lrg_asset_meta_importer_for_type()).
 * Settings are left empty, so every setting reads as its default.
 *
 * Returns: (transfer full): a new #LrgAssetMeta
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_new_for_asset (const gchar *asset_path);

/**
 * lrg_asset_meta_new_from_file:
 * @path: path of a ".meta" file
 * @error: return location for a #GError
 *
 * Loads a sidecar. A missing file fails with %G_IO_ERROR_NOT_FOUND; an
 * empty file loads as an empty sidecar without a guid.
 *
 * Returns: (transfer full) (nullable): the sidecar, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_new_from_file (const gchar  *path,
                                             GError      **error);

/**
 * lrg_asset_meta_new_from_data:
 * @data: YAML text
 * @length: length of @data in bytes, or -1 if NUL-terminated
 * @error: return location for a #GError
 *
 * Parses a sidecar from memory.
 *
 * Returns: (transfer full) (nullable): the sidecar, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_new_from_data (const gchar  *data,
                                             gssize        length,
                                             GError      **error);

/**
 * lrg_asset_meta_load_for_asset:
 * @asset_path: path of the asset (not of the sidecar)
 * @error: return location for a #GError
 *
 * Loads "@asset_path.meta". Fails with %G_IO_ERROR_NOT_FOUND when the
 * asset has no sidecar yet.
 *
 * Returns: (transfer full) (nullable): the sidecar, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_load_for_asset (const gchar  *asset_path,
                                              GError      **error);

/**
 * lrg_asset_meta_copy:
 * @self: an #LrgAssetMeta
 *
 * Deep-copies the sidecar, unknown keys included. The guid is copied too:
 * call lrg_asset_meta_regenerate_guid() for a duplicated asset.
 *
 * Returns: (transfer full): a new #LrgAssetMeta
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_meta_copy (LrgAssetMeta *self);

/**
 * lrg_asset_meta_path_for_asset:
 * @asset_path: path of an asset
 *
 * Returns: (transfer full): "@asset_path.meta"
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_path_for_asset (const gchar *asset_path);

/**
 * lrg_asset_meta_is_meta_path:
 * @path: a file name or path
 *
 * Returns: whether @path names a sidecar (ends in ".meta")
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_is_meta_path (const gchar *path);

/**
 * lrg_asset_meta_classify:
 * @path: (nullable): an asset file name or path
 *
 * Classifies a file by its (case-insensitive) extension. This is the one
 * classification shared by the editor's #LrgAssetDatabase (its
 * lrg_asset_database_classify() forwards here) and the runtime
 * #LrgAssetManager, so both pick the same importer for a file.
 *
 * Returns: the #LrgAssetType of @path (%LRG_ASSET_TYPE_UNKNOWN for %NULL
 *   or an unknown extension)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAssetType lrg_asset_meta_classify (const gchar *path);

/**
 * lrg_asset_meta_importer_for_type:
 * @type: an asset type
 *
 * Maps an asset type to its importer name: "texture", "model", "audio",
 * "font", "script", "level", "prefab", "tileset", "scene", "cad", or
 * "generic" for unknown files.
 *
 * Returns: (transfer none): the importer name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_meta_importer_for_type (LrgAssetType type);

/**
 * lrg_asset_meta_compute_legacy_guid:
 * @relative_path: the asset path relative to its asset directory
 *
 * Computes the identifier assets had before sidecars existed:
 * the lowercase hex SHA-256 of @relative_path. Sidecars created for
 * existing assets use it so references keep resolving.
 *
 * Returns: (transfer full): the legacy guid
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_compute_legacy_guid (const gchar *relative_path);

/**
 * lrg_asset_meta_save_to_file:
 * @self: an #LrgAssetMeta
 * @path: destination ".meta" path
 * @error: return location for a #GError
 *
 * Writes the sidecar atomically and clears the dirty flag.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_save_to_file (LrgAssetMeta  *self,
                                      const gchar   *path,
                                      GError       **error);

/**
 * lrg_asset_meta_to_data:
 * @self: an #LrgAssetMeta
 * @error: return location for a #GError
 *
 * Serializes the sidecar as YAML text (unknown keys included).
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_to_data (LrgAssetMeta  *self,
                                GError       **error);

/**
 * lrg_asset_meta_is_dirty:
 * @self: an #LrgAssetMeta
 *
 * Returns: whether the sidecar changed since it was loaded or saved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_is_dirty (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_dirty:
 * @self: an #LrgAssetMeta
 * @dirty: the new dirty state
 *
 * Marks the sidecar as needing (or not needing) a write.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_dirty (LrgAssetMeta *self,
                               gboolean      dirty);

/**
 * lrg_asset_meta_get_guid:
 * @self: an #LrgAssetMeta
 *
 * Returns: (transfer none) (nullable): the guid, or %NULL if the file had
 *   none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_meta_get_guid (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_guid:
 * @self: an #LrgAssetMeta
 * @guid: (nullable): the new guid, or %NULL to remove it
 *
 * Sets the guid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_guid (LrgAssetMeta *self,
                              const gchar  *guid);

/**
 * lrg_asset_meta_regenerate_guid:
 * @self: an #LrgAssetMeta
 *
 * Replaces the guid with a fresh random UUID v4 (used for duplicates).
 *
 * Returns: (transfer none): the new guid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_meta_regenerate_guid (LrgAssetMeta *self);

/**
 * lrg_asset_meta_get_importer:
 * @self: an #LrgAssetMeta
 *
 * Returns: (transfer none): the importer name ("generic" when unset)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_meta_get_importer (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_importer:
 * @self: an #LrgAssetMeta
 * @importer: an importer name such as "texture"
 *
 * Sets the importer. Existing settings are kept (a later switch back
 * finds them again).
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_importer (LrgAssetMeta *self,
                                  const gchar  *importer);

/* --------------------------------------------------------------------------
 * Settings by key
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_has_setting:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 *
 * Returns: whether @key is stored explicitly (as opposed to defaulted)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_has_setting (LrgAssetMeta *self,
                                     const gchar  *key);

/**
 * lrg_asset_meta_remove_setting:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 *
 * Removes @key so it reads as its default again.
 *
 * Returns: whether @key was stored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_remove_setting (LrgAssetMeta *self,
                                        const gchar  *key);

/**
 * lrg_asset_meta_get_setting_keys:
 * @self: an #LrgAssetMeta
 *
 * Lists the keys stored in "settings" (known and unknown), in file order.
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_asset_meta_get_setting_keys (LrgAssetMeta *self);

/**
 * lrg_asset_meta_get_setting_string:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 *
 * Returns the stored scalar text of @key, or the importer default for a
 * known key that is not stored.
 *
 * Returns: (transfer full) (nullable): the value text, or %NULL for an
 *   unknown key that is not stored (or is not a scalar)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_get_setting_string (LrgAssetMeta *self,
                                           const gchar  *key);

/**
 * lrg_asset_meta_get_setting_int:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @fallback: returned when @key is neither stored nor a known setting
 *
 * Returns: the integer value of @key
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_asset_meta_get_setting_int (LrgAssetMeta *self,
                                       const gchar  *key,
                                       gint64        fallback);

/**
 * lrg_asset_meta_get_setting_double:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @fallback: returned when @key is neither stored nor a known setting
 *
 * Returns: the floating-point value of @key
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_asset_meta_get_setting_double (LrgAssetMeta *self,
                                           const gchar  *key,
                                           gdouble       fallback);

/**
 * lrg_asset_meta_get_setting_boolean:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @fallback: returned when @key is neither stored nor a known setting
 *
 * Returns: the boolean value of @key ("true"/"yes"/"on"/"1" are true)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_setting_boolean (LrgAssetMeta *self,
                                             const gchar  *key,
                                             gboolean      fallback);

/**
 * lrg_asset_meta_set_setting_string:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @value: the value
 *
 * Stores a string setting (no validation; see
 * lrg_asset_meta_set_setting_variant() for the validating path).
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_setting_string (LrgAssetMeta *self,
                                        const gchar  *key,
                                        const gchar  *value);

/**
 * lrg_asset_meta_set_setting_int:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @value: the value
 *
 * Stores an integer setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_setting_int (LrgAssetMeta *self,
                                     const gchar  *key,
                                     gint64        value);

/**
 * lrg_asset_meta_set_setting_double:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @value: the value
 *
 * Stores a floating-point setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_setting_double (LrgAssetMeta *self,
                                        const gchar  *key,
                                        gdouble       value);

/**
 * lrg_asset_meta_set_setting_boolean:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @value: the value
 *
 * Stores a boolean setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_setting_boolean (LrgAssetMeta *self,
                                         const gchar  *key,
                                         gboolean      value);

/* --------------------------------------------------------------------------
 * Generic (GVariant) access, for IPC and property grids
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_get_setting_variant:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 *
 * Returns @key as a typed variant: known settings use their declared type
 * ("b", "x", "d" or "s") and fall back to their default; unknown stored
 * scalars are typed by their text (boolean, integer, double, else
 * string), nested mappings become "a{sv}" and sequences "av".
 *
 * Returns: (transfer full) (nullable): a non-floating variant, or %NULL
 *   for an unknown key that is not stored
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_asset_meta_get_setting_variant (LrgAssetMeta *self,
                                               const gchar  *key);

/**
 * lrg_asset_meta_set_setting_variant:
 * @self: an #LrgAssetMeta
 * @key: a setting key
 * @value: (nullable): the value, or %NULL to remove @key (back to its
 *   default)
 * @error: return location for a #GError
 *
 * Stores @key from a variant, validating known settings: booleans accept
 * "b" (or a "true"/"false" string), integers any integral number type,
 * doubles any number type, choices (filter, wrap, up-axis, mode) a string
 * that is one of the allowed values, and numeric ranges are checked
 * (e.g. "max-size" 0..16384). Unknown keys store any scalar, "a{sv}" or
 * "av" value as is. Fails with %G_IO_ERROR_INVALID_ARGUMENT.
 *
 * Returns: %TRUE if @value was stored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_set_setting_variant (LrgAssetMeta  *self,
                                             const gchar   *key,
                                             GVariant      *value,
                                             GError       **error);

/**
 * lrg_asset_meta_get_settings_variant:
 * @self: an #LrgAssetMeta
 * @with_defaults: also include known settings that are not stored, at
 *   their default values
 *
 * Returns: (transfer full): an "a{sv}" of the settings, keys sorted
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_asset_meta_get_settings_variant (LrgAssetMeta *self,
                                                gboolean      with_defaults);

/**
 * lrg_asset_meta_to_variant:
 * @self: an #LrgAssetMeta
 *
 * Describes the sidecar for IPC as an "a{sv}" with "guid" (s, empty when
 * unset), "importer" (s) and "settings" (a{sv}, defaults included).
 *
 * Returns: (transfer full): the description
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_asset_meta_to_variant (LrgAssetMeta *self);

/**
 * lrg_asset_meta_get_settings_hash:
 * @self: an #LrgAssetMeta
 *
 * Hashes the effective settings (stored values over defaults, unknown
 * keys included) so caches can key on them (the thumbnail cache does for
 * textures). Two sidecars with equal settings hash equally whatever the
 * key order or guid.
 *
 * Returns: (transfer full): a 40-character hex SHA-1
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_get_settings_hash (LrgAssetMeta *self);

/**
 * lrg_asset_meta_list_known_settings:
 * @importer: an importer name
 *
 * Lists the settings the editor knows for @importer, in display order
 * (e.g. "filter", "mipmaps", "wrap", "srgb", "max-size" for "texture").
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys (empty for
 *   importers without settings)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_asset_meta_list_known_settings (const gchar *importer);

/**
 * lrg_asset_meta_get_setting_choices:
 * @importer: an importer name
 * @key: a setting key
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): the
 *   allowed values of a choice setting, or %NULL if @key is not one
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_asset_meta_get_setting_choices (const gchar *importer,
                                          const gchar *key);

/**
 * lrg_asset_meta_get_default_setting:
 * @importer: an importer name
 * @key: a setting key
 *
 * Returns: (transfer full) (nullable): the default of a known setting as a
 *   typed variant, or %NULL if @key is not known for @importer
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_asset_meta_get_default_setting (const gchar *importer,
                                               const gchar *key);

/* --------------------------------------------------------------------------
 * Typed accessors: texture
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_get_texture_filter:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "filter" setting (default bilinear)
 */
LRG_AVAILABLE_IN_ALL
LrgAssetTextureFilter lrg_asset_meta_get_texture_filter (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_texture_filter:
 * @self: an #LrgAssetMeta
 * @filter: the filter
 *
 * Sets the "filter" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_texture_filter (LrgAssetMeta          *self,
                                        LrgAssetTextureFilter  filter);

/**
 * lrg_asset_meta_get_texture_mipmaps:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "mipmaps" setting (default %FALSE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_texture_mipmaps (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_texture_mipmaps:
 * @self: an #LrgAssetMeta
 * @mipmaps: whether to generate mipmaps
 *
 * Sets the "mipmaps" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_texture_mipmaps (LrgAssetMeta *self,
                                         gboolean      mipmaps);

/**
 * lrg_asset_meta_get_texture_wrap:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "wrap" setting (default repeat)
 */
LRG_AVAILABLE_IN_ALL
LrgAssetTextureWrap lrg_asset_meta_get_texture_wrap (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_texture_wrap:
 * @self: an #LrgAssetMeta
 * @wrap: the wrap mode
 *
 * Sets the "wrap" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_texture_wrap (LrgAssetMeta        *self,
                                      LrgAssetTextureWrap  wrap);

/**
 * lrg_asset_meta_get_texture_srgb:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "srgb" setting (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_texture_srgb (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_texture_srgb:
 * @self: an #LrgAssetMeta
 * @srgb: whether the texels are sRGB encoded
 *
 * Sets the "srgb" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_texture_srgb (LrgAssetMeta *self,
                                      gboolean      srgb);

/**
 * lrg_asset_meta_get_texture_max_size:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "max-size" setting: the longest edge after import in
 *   pixels, 0 for no limit (default 0)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_meta_get_texture_max_size (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_texture_max_size:
 * @self: an #LrgAssetMeta
 * @max_size: longest edge in pixels (0 .. 16384), 0 for no limit
 *
 * Sets the "max-size" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_texture_max_size (LrgAssetMeta *self,
                                          guint         max_size);

/* --------------------------------------------------------------------------
 * Typed accessors: model
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_get_model_scale:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "scale" setting (default 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_asset_meta_get_model_scale (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_model_scale:
 * @self: an #LrgAssetMeta
 * @scale: uniform import scale (> 0)
 *
 * Sets the "scale" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_model_scale (LrgAssetMeta *self,
                                     gdouble       scale);

/**
 * lrg_asset_meta_get_model_up_axis:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "up-axis" setting (default y)
 */
LRG_AVAILABLE_IN_ALL
LrgAssetUpAxis lrg_asset_meta_get_model_up_axis (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_model_up_axis:
 * @self: an #LrgAssetMeta
 * @axis: the source file's up axis
 *
 * Sets the "up-axis" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_model_up_axis (LrgAssetMeta   *self,
                                       LrgAssetUpAxis  axis);

/**
 * lrg_asset_meta_get_model_generate_normals:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "generate-normals" setting (default %FALSE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_model_generate_normals (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_model_generate_normals:
 * @self: an #LrgAssetMeta
 * @generate: whether to recompute normals on import
 *
 * Sets the "generate-normals" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_model_generate_normals (LrgAssetMeta *self,
                                                gboolean      generate);

/**
 * lrg_asset_meta_get_model_import_animations:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "import-animations" setting (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_model_import_animations (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_model_import_animations:
 * @self: an #LrgAssetMeta
 * @import: whether to import animation clips
 *
 * Sets the "import-animations" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_model_import_animations (LrgAssetMeta *self,
                                                 gboolean      import);

/* --------------------------------------------------------------------------
 * Typed accessors: audio
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_get_audio_mode:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "mode" setting (default sample)
 */
LRG_AVAILABLE_IN_ALL
LrgAssetAudioMode lrg_asset_meta_get_audio_mode (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_audio_mode:
 * @self: an #LrgAssetMeta
 * @mode: stream or sample
 *
 * Sets the "mode" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_audio_mode (LrgAssetMeta      *self,
                                    LrgAssetAudioMode  mode);

/**
 * lrg_asset_meta_get_audio_loop:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "loop" setting (default %FALSE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_meta_get_audio_loop (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_audio_loop:
 * @self: an #LrgAssetMeta
 * @loop: whether playback loops
 *
 * Sets the "loop" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_audio_loop (LrgAssetMeta *self,
                                    gboolean      loop);

/**
 * lrg_asset_meta_get_audio_volume:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "volume" setting (default 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_asset_meta_get_audio_volume (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_audio_volume:
 * @self: an #LrgAssetMeta
 * @volume: linear gain (0 .. 4)
 *
 * Sets the "volume" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_audio_volume (LrgAssetMeta *self,
                                      gdouble       volume);

/* --------------------------------------------------------------------------
 * Typed accessors: font
 * -------------------------------------------------------------------------- */

/**
 * lrg_asset_meta_get_font_size:
 * @self: an #LrgAssetMeta
 *
 * Returns: the "size" setting: the rasterized size in pixels (default 32)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_meta_get_font_size (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_font_size:
 * @self: an #LrgAssetMeta
 * @size: rasterized size in pixels (1 .. 1024)
 *
 * Sets the "size" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_font_size (LrgAssetMeta *self,
                                   guint         size);

/**
 * lrg_asset_meta_get_font_codepoints:
 * @self: an #LrgAssetMeta
 *
 * Returns: (transfer full): the "codepoints" setting: comma-separated
 *   code points and inclusive ranges, decimal or 0x-hex, e.g.
 *   "32-126,0xA0-0xFF" (default "32-126")
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_meta_get_font_codepoints (LrgAssetMeta *self);

/**
 * lrg_asset_meta_set_font_codepoints:
 * @self: an #LrgAssetMeta
 * @ranges: code points and ranges, see lrg_asset_meta_get_font_codepoints()
 *
 * Sets the "codepoints" setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_meta_set_font_codepoints (LrgAssetMeta *self,
                                         const gchar  *ranges);

/**
 * lrg_asset_meta_parse_codepoints:
 * @ranges: code points and ranges such as "32-126,0x4E00-0x4E0F,9731"
 * @error: return location for a #GError
 *
 * Expands a "codepoints" setting into a sorted list without duplicates.
 * At most 0x110000 entries; malformed input fails with
 * %G_IO_ERROR_INVALID_ARGUMENT.
 *
 * Returns: (transfer full) (nullable) (element-type guint32): the code
 *   points, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_asset_meta_parse_codepoints (const gchar  *ranges,
                                          GError      **error);

G_END_DECLS
