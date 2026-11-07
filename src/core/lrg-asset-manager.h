/* lrg-asset-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Centralized asset loading and caching.
 *
 * The asset manager provides a unified interface for loading game
 * assets (textures, fonts, sounds, music) with caching and mod
 * overlay support through prioritized search paths.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#ifdef LRG_HAS_LIBDEX
#include <libdex.h>
#endif
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-asset-meta.h"
#include "../graphics/lrg-material.h"

G_BEGIN_DECLS

/**
 * LRG_ASSET_GUID_PREFIX:
 *
 * The prefix of an asset reference by guid: `guid:<uuid>`. Anywhere a
 * loader of #LrgAssetManager takes an asset name (and in `.rlevel`,
 * `.rprefab` and YAML definitions that reach it) the name may be
 * `guid:` followed by the asset guid from its `.meta` sidecar instead of
 * a path; lrg_asset_manager_resolve_reference() turns it into a path.
 *
 * Since: 0.9.0
 */
#define LRG_ASSET_GUID_PREFIX "guid:"

/**
 * LRG_ASSET_GUID_INDEX_VERSION:
 *
 * The `version:` written to a cached guid index file (see
 * lrg_asset_manager_save_guid_index()).
 *
 * Since: 0.9.0
 */
#define LRG_ASSET_GUID_INDEX_VERSION (1)

#define LRG_TYPE_ASSET_MANAGER (lrg_asset_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAssetManager, lrg_asset_manager, LRG, ASSET_MANAGER, GObject)

/**
 * LrgAssetManagerClass:
 * @parent_class: The parent class
 * @load_texture: Virtual method to load a texture
 * @load_font: Virtual method to load a font
 * @load_sound: Virtual method to load a sound
 * @load_music: Virtual method to load music
 *
 * The class structure for #LrgAssetManager.
 *
 * Subclasses can override the virtual methods to customize
 * asset loading behavior.
 */
struct _LrgAssetManagerClass
{
    GObjectClass parent_class;

    /* Virtual methods for custom loading behavior */
    GrlTexture * (*load_texture) (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);
    GrlFont *    (*load_font)    (LrgAssetManager  *self,
                                  const gchar      *name,
                                  gint              size,
                                  GError          **error);
    GrlSound *   (*load_sound)   (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);
    GrlMusic *   (*load_music)   (LrgAssetManager  *self,
                                  const gchar      *name,
                                  GError          **error);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_asset_manager_new:
 *
 * Creates a new asset manager.
 *
 * Returns: (transfer full): A new #LrgAssetManager
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_asset_manager_new (void);

/* ==========================================================================
 * Search Path Management
 * ========================================================================== */

/**
 * lrg_asset_manager_add_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to add
 *
 * Adds a directory to the search path.
 *
 * Later paths have higher priority and will override assets from
 * earlier paths, enabling mod overlay support. Add the base game
 * assets path first, then mod paths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_add_search_path (LrgAssetManager *self,
                                        const gchar     *path);

/**
 * lrg_asset_manager_remove_search_path:
 * @self: an #LrgAssetManager
 * @path: directory path to remove
 *
 * Removes a directory from the search path.
 *
 * Returns: %TRUE if the path was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_remove_search_path (LrgAssetManager *self,
                                               const gchar     *path);

/**
 * lrg_asset_manager_clear_search_paths:
 * @self: an #LrgAssetManager
 *
 * Removes all search paths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_clear_search_paths (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_search_paths:
 * @self: an #LrgAssetManager
 *
 * Gets the list of search paths in priority order (lowest to highest).
 *
 * Returns: (transfer none) (element-type utf8): The search paths array
 */
LRG_AVAILABLE_IN_ALL
const GPtrArray * lrg_asset_manager_get_search_paths (LrgAssetManager *self);

/* ==========================================================================
 * Synchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file (e.g., "sprites/player.png")
 * @error: (nullable): return location for error
 *
 * Loads a texture from the search paths.
 *
 * If the texture is already cached, returns the cached instance.
 * Search paths are checked in reverse order (last added has priority).
 * Since 0.9.0 @name may be a `guid:<uuid>` reference, and the texture
 * importer settings of the file's `.meta` sidecar (max-size, mipmaps,
 * filter, wrap) are applied while loading (see
 * #LrgAssetManager:apply-import-settings).
 *
 * Returns: (transfer none) (nullable): The #GrlTexture, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_asset_manager_load_texture (LrgAssetManager  *self,
                                             const gchar      *name,
                                             GError          **error);

/**
 * lrg_asset_manager_load_font:
 * @self: an #LrgAssetManager
 * @name: relative path to font file (e.g., "fonts/main.ttf")
 * @size: font size in pixels; since 0.9.0 0 or less means the `size`
 *   setting of the font's `.meta` sidecar (32 without one)
 * @error: (nullable): return location for error
 *
 * Loads a font from the search paths.
 *
 * The cache key includes the size, so the same font file at different
 * sizes creates separate cache entries. Since 0.9.0 @name may be a
 * `guid:<uuid>` reference and the sidecar's `codepoints` setting chooses
 * the rasterized glyphs.
 *
 * Returns: (transfer none) (nullable): The #GrlFont, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_asset_manager_load_font (LrgAssetManager  *self,
                                       const gchar      *name,
                                       gint              size,
                                       GError          **error);

/**
 * lrg_asset_manager_load_sound:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file (e.g., "sounds/jump.wav")
 * @error: (nullable): return location for error
 *
 * Loads a sound effect from the search paths.
 *
 * Sound files are fully loaded into memory for low-latency playback.
 * Since 0.9.0 @name may be a `guid:<uuid>` reference and the sidecar's
 * `volume` is applied (see lrg_asset_manager_load_audio() to honour the
 * sidecar's stream/sample mode).
 *
 * Returns: (transfer none) (nullable): The #GrlSound, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlSound * lrg_asset_manager_load_sound (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

/**
 * lrg_asset_manager_load_music:
 * @self: an #LrgAssetManager
 * @name: relative path to music file (e.g., "music/theme.ogg")
 * @error: (nullable): return location for error
 *
 * Loads a streaming music track from the search paths.
 *
 * Unlike sounds, music is streamed from disk during playback.
 * Since 0.9.0 @name may be a `guid:<uuid>` reference and the sidecar's
 * `volume` and `loop` are applied.
 *
 * Returns: (transfer none) (nullable): The #GrlMusic, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlMusic * lrg_asset_manager_load_music (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

/**
 * lrg_asset_manager_set_data_loader:
 * @self: an #LrgAssetManager
 * @loader: (nullable): loader for YAML definitions
 *
 * Retains @loader. Changing it clears cached definitions and their watches.
 * A new manager has no loader; the engine supplies its registered loader.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_set_data_loader (LrgAssetManager *self,
                                        LrgDataLoader   *loader);

/**
 * lrg_asset_manager_get_data_loader:
 * @self: an #LrgAssetManager
 *
 * Returns: (transfer none) (nullable): the loader for YAML definitions
 */
LRG_AVAILABLE_IN_ALL
LrgDataLoader * lrg_asset_manager_get_data_loader (LrgAssetManager *self);

/**
 * lrg_asset_manager_load_asset:
 * @self: an #LrgAssetManager
 * @name: asset name or absolute file path
 * @error: (nullable): return location for error
 *
 * Loads and caches an asset based on its case-insensitive extension:
 * png/jpg/jpeg/bmp/tga/gif/qoi/dds/ktx/pkm/pvr/astc become textures;
 * ttf/otf/fnt become fonts at size 32; wav becomes a sound;
 * ogg/mp3/flac/xm/mod become streaming music; glb/gltf/obj/iqm/m3d/vox
 * become cached models (lrg_asset_manager_load_model()); rmat becomes a
 * cached #LrgMaterial (lrg_asset_manager_load_material()); yaml/yml become
 * validated GObject definitions. Other extensions return %G_IO_ERROR_NOT_SUPPORTED.
 * Call the specific load functions to choose another font size/audio mode.
 * Texture/font loading requires a graphics context, audio an audio device;
 * YAML definitions work headlessly. Calls must run on the owning thread.
 *
 * Returns: (transfer none) (nullable): the cached asset, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_asset_manager_load_asset (LrgAssetManager  *self,
                                        const gchar      *name,
                                        GError          **error);

/**
 * lrg_asset_manager_load_object:
 * @self: an #LrgAssetManager
 * @name: YAML asset name or absolute file path
 * @error: (nullable): return location for error
 *
 * Loads a definition using lrg_data_loader_load_file_validated(). A successful
 * load is cached until unloaded or reloaded. The resolved path is pinned for
 * reloads; unload it to apply search path changes. Hold your own reference
 * across reloads and use ::object-reloaded to replace consumer references.
 *
 * Returns: (transfer none) (nullable): the cached definition
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_asset_manager_load_object (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

/**
 * lrg_asset_manager_add_object_dependency:
 * @self: the manager
 * @name: cached dependent definition
 * @dependency: cached source definition
 * @error: (nullable): error location
 *
 * Adds an explicit dependency. Both endpoints must already be loaded.
 * Duplicate links are harmless; cycles are rejected. Unloading either endpoint
 * removes the link. Reloading a source also reloads all transitive dependents.
 *
 * Returns: whether the link was accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_add_object_dependency (LrgAssetManager *self,
                                                  const gchar *name,
                                                  const gchar *dependency,
                                                  GError **error);

/**
 * lrg_asset_manager_remove_object_dependency:
 * @self: the manager
 * @name: dependent definition
 * @dependency: source definition
 *
 * Returns: whether the link existed and was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_remove_object_dependency (LrgAssetManager *self,
                                                     const gchar *name,
                                                     const gchar *dependency);

/**
 * lrg_asset_manager_reload_object:
 * @self: an #LrgAssetManager
 * @name: name of a cached YAML definition
 * @error: (nullable): return location for error
 *
 * Validates a replacement before swapping the cache and emitting
 * ::object-reloaded. A failure emits ::object-reload-failed, returns an
 * error and keeps the previous object. Changing the definition's GType
 * is rejected. Unload explicitly to change type.
 *
 * All transitive dependents are validated before any cached object is replaced.
 * Failure leaves the whole batch unchanged and emits one failure for @name.
 * Success emits one replacement signal per affected object in dependency order,
 * after committing the entire batch. Recursive reloads return G_IO_ERROR_PENDING.
 * Signal arguments remain valid even if a handler unloads the cache.
 *
 * Returns: %TRUE if a new definition replaced the cached object
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_reload_object (LrgAssetManager  *self,
                                           const gchar      *name,
                                           GError          **error);

/**
 * lrg_asset_manager_watch_object:
 * @self: an #LrgAssetManager
 * @name: YAML asset name or absolute file path
 * @error: (nullable): return location for error
 *
 * Loads a definition if needed and watches its parent directory. Changes,
 * atomic file replacements, deletion and recreation are debounced for 100ms
 * on the calling thread's thread-default #GMainContext. Iterate that context
 * to receive reloads. Watching is opt-in. All manager access must occur on
 * that thread. Failed edits retain the previous object and emit an error.
 * Unloading, changing the loader or destroying the manager stops watches and
 * cancels pending reloads. Watches do not keep the manager alive.
 *
 * Returns: %TRUE if the definition is loaded and its watch is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_watch_object (LrgAssetManager  *self,
                                          const gchar      *name,
                                          GError          **error);

/**
 * lrg_asset_manager_unwatch_object:
 * @self: an #LrgAssetManager
 * @name: watched asset name
 *
 * Stops monitoring and cancels pending reloads, retaining the cached object.
 *
 * Returns: %TRUE if a watch was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_unwatch_object (LrgAssetManager *self,
                                           const gchar     *name);

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Asynchronous Loading
 * ========================================================================== */

/**
 * lrg_asset_manager_load_texture_async:
 * @self: an #LrgAssetManager
 * @name: relative path to texture file
 *
 * Asynchronously loads a texture.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlTexture
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_texture_async (LrgAssetManager *self,
                                                  const gchar     *name);

/**
 * lrg_asset_manager_load_font_async:
 * @self: an #LrgAssetManager
 * @name: relative path to font file
 * @size: font size in pixels
 *
 * Asynchronously loads a font.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlFont
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_font_async (LrgAssetManager *self,
                                               const gchar     *name,
                                               gint             size);

/**
 * lrg_asset_manager_load_sound_async:
 * @self: an #LrgAssetManager
 * @name: relative path to sound file
 *
 * Asynchronously loads a sound effect.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlSound
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_sound_async (LrgAssetManager *self,
                                                const gchar     *name);

/**
 * lrg_asset_manager_load_music_async:
 * @self: an #LrgAssetManager
 * @name: relative path to music file
 *
 * Asynchronously loads a music track.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GrlMusic
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_asset_manager_load_music_async (LrgAssetManager *self,
                                                const gchar     *name);
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Cache Management
 * ========================================================================== */

/**
 * lrg_asset_manager_unload:
 * @self: an #LrgAssetManager
 * @name: the asset name to unload
 *
 * Removes an asset from all caches.
 *
 * The asset may still be in use if other code holds a reference
 * to it. This removes the cache's reference, allowing the asset
 * to be freed when all other references are released.
 *
 * Returns: %TRUE if the asset was in a cache and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_unload (LrgAssetManager *self,
                                   const gchar     *name);

/**
 * lrg_asset_manager_unload_all:
 * @self: an #LrgAssetManager
 *
 * Clears all cached assets.
 *
 * Assets currently in use elsewhere remain valid until their
 * references are released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_unload_all (LrgAssetManager *self);

/**
 * lrg_asset_manager_is_cached:
 * @self: an #LrgAssetManager
 * @name: the asset name to check
 *
 * Checks if an asset is currently in any cache.
 *
 * Returns: %TRUE if the asset is cached
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_is_cached (LrgAssetManager *self,
                                      const gchar     *name);

/**
 * lrg_asset_manager_get_texture_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached textures.
 *
 * Returns: The texture cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_texture_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_font_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached fonts.
 *
 * Returns: The font cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_font_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_sound_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached sounds.
 *
 * Returns: The sound cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_sound_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_music_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached music tracks.
 *
 * Returns: The music cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_music_cache_size (LrgAssetManager *self);

/* ==========================================================================
 * Models
 * ========================================================================== */

/**
 * lrg_asset_manager_resolve_path:
 * @self: an #LrgAssetManager
 * @name: asset name (searched in the search paths, last added first) or
 *   absolute path
 *
 * Resolves @name the same way the loaders do and canonicalizes the result
 * (absolute, with "." and ".." segments and duplicate separators removed).
 * This canonical path is the key of the model and animation caches.
 *
 * Returns: (transfer full) (nullable) (type filename): the canonical path,
 *   or %NULL if no search path contains @name
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_manager_resolve_path (LrgAssetManager *self,
                                        const gchar     *name);

/**
 * lrg_asset_manager_load_model:
 * @self: an #LrgAssetManager
 * @name: model name (e.g. "models/bear.glb") or absolute path
 * @error: (nullable): return location for error
 *
 * Loads a 3D model (glTF/GLB, OBJ, IQM, M3D, VOX) and caches it under its
 * canonical resolved path (see lrg_asset_manager_resolve_path()), so
 * different spellings of one file share a single #GrlModel. Since 0.9.0
 * @name may be a `guid:<uuid>` reference, and the model importer settings
 * of the `.meta` sidecar are applied: `scale` and `up-axis` into the model
 * transform, `generate-normals` by recomputing vertex normals.
 *
 * The manager owns the model and unloads it on lrg_asset_manager_unload(),
 * lrg_asset_manager_unload_all() or finalization, together with the
 * material textures its loader created and the skinning pose caches.
 * Textures a caller later assigns to a material stay the caller's.
 * Materials may be given a custom shader; the manager never unloads
 * shaders. Release the manager (or unload the model) before closing the
 * window.
 *
 * Errors: %LRG_ASSET_MANAGER_ERROR_NOT_FOUND when no file matches,
 * %G_IO_ERROR_NOT_INITIALIZED without a graphics context, and
 * %LRG_ASSET_MANAGER_ERROR_LOAD_FAILED when the loader produced no meshes
 * (for example a Draco-compressed glTF).
 *
 * Returns: (transfer none) (nullable): the cached #GrlModel, or %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
GrlModel * lrg_asset_manager_load_model (LrgAssetManager  *self,
                                         const gchar      *name,
                                         GError          **error);

/**
 * lrg_asset_manager_load_model_animations:
 * @self: an #LrgAssetManager
 * @name: model name or absolute path
 * @error: (nullable): return location for error
 *
 * Loads the skeletal animation clips of a model file, cached under the
 * same canonical key as lrg_asset_manager_load_model(). Clips are sampled
 * at 60 frames per second by raylib and keep the file's clip order.
 * Loading needs no graphics context. A file without a skin or animations
 * yields an empty array (also cached).
 *
 * For .glb and .gltf files the clips are passed through
 * lrg_gltf_info_fix_animation_roots() before caching, correcting raylib
 * 6.0's posing of skeletons with more than one root joint.
 *
 * Errors: %LRG_ASSET_MANAGER_ERROR_NOT_FOUND when no file matches.
 *
 * Returns: (transfer none) (nullable) (element-type GrlModelAnimation):
 *   the cached clips, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_asset_manager_load_model_animations (LrgAssetManager  *self,
                                                     const gchar      *name,
                                                     GError          **error);

/**
 * lrg_asset_manager_get_model_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached models.
 *
 * Returns: The model cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_model_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_animation_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached animation sets (one per model file).
 *
 * Returns: The animation cache size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_animation_cache_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_load_material:
 * @self: an #LrgAssetManager
 * @name: a `.rmat` reference: `guid:<uuid>`, a name relative to a search
 *   path, or an absolute path
 * @error: (nullable): return location for error
 *
 * Loads a material asset (lrg_material_new_from_file() with @self as its
 * asset manager) and caches it under the canonical path, so every
 * reference to one file (path spellings and guid) shares one
 * #LrgMaterial, and instances naming it as `parent:` share it too.
 * Loading is headless: textures load on first use through
 * lrg_asset_manager_load_texture() (import settings apply).
 *
 * Errors: %LRG_ASSET_MANAGER_ERROR_NOT_FOUND when nothing matches @name,
 * otherwise the #LrgMaterialError of the loader.
 *
 * Returns: (transfer none) (nullable): the cached #LrgMaterial, or %NULL
 *   on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_asset_manager_load_material (LrgAssetManager  *self,
                                               const gchar      *name,
                                               GError          **error);

/**
 * lrg_asset_manager_get_material_cache_size:
 * @self: an #LrgAssetManager
 *
 * Gets the number of cached materials.
 *
 * Returns: The material cache size
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_material_cache_size (LrgAssetManager *self);

/* ==========================================================================
 * Projects and guid references (since 0.9.0)
 * ========================================================================== */

/**
 * lrg_asset_manager_set_project_root:
 * @self: an #LrgAssetManager
 * @root: (nullable) (type filename): the project directory (the one holding
 *   `project.ryaml`), or %NULL to forget the project
 *
 * Makes the manager project-aware. @root is added as a search path (asset
 * paths in levels and prefabs are project-relative), and when
 * `<root>/project.ryaml` exists its `asset_dirs` become the guid index
 * directories (lrg_asset_manager_add_asset_dir()) and its
 * `apply_import_settings` key (default true) sets
 * #LrgAssetManager:apply-import-settings. Without a manifest @root itself
 * is the only asset directory. The guid index is rebuilt lazily on the
 * next guid lookup.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_set_project_root (LrgAssetManager *self,
                                         const gchar     *root);

/**
 * lrg_asset_manager_get_project_root:
 * @self: an #LrgAssetManager
 *
 * Returns: (transfer none) (nullable) (type filename): the project root
 *   set with lrg_asset_manager_set_project_root()
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_manager_get_project_root (LrgAssetManager *self);

/**
 * lrg_asset_manager_add_asset_dir:
 * @self: an #LrgAssetManager
 * @dir: (type filename): a directory to index; relative paths are taken
 *   relative to the project root when one is set
 *
 * Adds a directory to the guid index. The index covers every file below
 * the asset directories (version-control directories skipped), exactly
 * like the editor's #LrgAssetDatabase: an asset's guid is the `guid:` of
 * its `.meta` sidecar, or, without a sidecar, the legacy guid computed
 * from its path relative to the directory
 * (lrg_asset_meta_compute_legacy_guid()). When the same guid appears
 * twice the first file (directory order, then sorted path order) wins.
 * When no asset directory is set, the search paths are indexed instead.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_add_asset_dir (LrgAssetManager *self,
                                      const gchar     *dir);

/**
 * lrg_asset_manager_get_asset_dirs:
 * @self: an #LrgAssetManager
 *
 * Returns: (transfer none) (element-type filename): the absolute guid
 *   index directories, in the order they were added
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const GPtrArray * lrg_asset_manager_get_asset_dirs (LrgAssetManager *self);

/**
 * lrg_asset_manager_clear_asset_dirs:
 * @self: an #LrgAssetManager
 *
 * Removes every guid index directory and empties the index.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_clear_asset_dirs (LrgAssetManager *self);

/**
 * lrg_asset_manager_rebuild_guid_index:
 * @self: an #LrgAssetManager
 *
 * Scans the asset directories (see lrg_asset_manager_add_asset_dir()) and
 * rebuilds the guid index now. Lookups build the index on first use, and
 * rebuild it once when an indexed file has disappeared, so calling this
 * is only needed after assets were added while the game runs.
 *
 * Returns: the number of indexed assets
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_rebuild_guid_index (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_guid_index_size:
 * @self: an #LrgAssetManager
 *
 * Returns: the number of assets in the guid index (0 before it is built)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_manager_get_guid_index_size (LrgAssetManager *self);

/**
 * lrg_asset_manager_resolve_guid:
 * @self: an #LrgAssetManager
 * @guid: an asset guid, with or without the `guid:` prefix
 *
 * Looks @guid up in the guid index (built on first use).
 *
 * Returns: (transfer full) (nullable) (type filename): the absolute,
 *   canonical path of the asset, or %NULL when no indexed asset has @guid
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_manager_resolve_guid (LrgAssetManager *self,
                                        const gchar     *guid);

/**
 * lrg_asset_manager_lookup_guid:
 * @self: an #LrgAssetManager
 * @name: an asset name (searched like the loaders do) or absolute path
 *
 * The reverse of lrg_asset_manager_resolve_guid(): the guid the index
 * holds for the file @name resolves to.
 *
 * Returns: (transfer full) (nullable): the guid, or %NULL when the file is
 *   not inside an asset directory
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_manager_lookup_guid (LrgAssetManager *self,
                                       const gchar     *name);

/**
 * lrg_asset_manager_resolve_reference:
 * @self: an #LrgAssetManager
 * @ref: an asset reference: `guid:<uuid>`, a name relative to a search
 *   path, or an absolute path
 *
 * Resolves any asset reference to an existing file: `guid:` references go
 * through the guid index, everything else through the search paths
 * (last added first).
 *
 * Returns: (transfer full) (nullable) (type filename): the absolute,
 *   canonical path, or %NULL when nothing matches
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_manager_resolve_reference (LrgAssetManager *self,
                                             const gchar     *ref);

/**
 * lrg_asset_manager_save_guid_index:
 * @self: an #LrgAssetManager
 * @path: (type filename): the index file to write
 * @error: (nullable): return location for an error
 *
 * Writes the guid index (building it first if needed) as YAML:
 *
 * |[
 * version: 1
 * assets:
 *   2f1c...: textures/crate.png     # relative to the project root
 * ]|
 *
 * Paths are written relative to the project root when one is set (and the
 * asset is inside it), absolute otherwise. A shipped game can load this
 * file with lrg_asset_manager_load_guid_index() instead of scanning.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_save_guid_index (LrgAssetManager  *self,
                                            const gchar      *path,
                                            GError          **error);

/**
 * lrg_asset_manager_load_guid_index:
 * @self: an #LrgAssetManager
 * @path: (type filename): an index file written by
 *   lrg_asset_manager_save_guid_index()
 * @error: (nullable): return location for an error
 *
 * Replaces the guid index with the cached one in @path (relative entries
 * resolve against the project root, else against the directory of
 * @path). The cache is trusted until a lookup finds that an indexed file
 * no longer exists; the index is then rebuilt from the asset directories
 * once.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_load_guid_index (LrgAssetManager  *self,
                                            const gchar      *path,
                                            GError          **error);

/* ==========================================================================
 * Runtime import settings (since 0.9.0)
 * ========================================================================== */

/**
 * lrg_asset_manager_set_apply_import_settings:
 * @self: an #LrgAssetManager
 * @apply: whether loaders apply `.meta` importer settings
 *
 * Sets #LrgAssetManager:apply-import-settings. Changing it does not touch
 * assets already in the caches; unload them to reload with the new
 * policy.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_manager_set_apply_import_settings (LrgAssetManager *self,
                                                  gboolean         apply);

/**
 * lrg_asset_manager_get_apply_import_settings:
 * @self: an #LrgAssetManager
 *
 * Returns: whether loaders apply `.meta` importer settings (default %TRUE)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_manager_get_apply_import_settings (LrgAssetManager *self);

/**
 * lrg_asset_manager_get_asset_meta:
 * @self: an #LrgAssetManager
 * @name: an asset reference (`guid:`, name or absolute path)
 *
 * Reads the `.meta` sidecar of the file @name resolves to. Use it for
 * settings the runtime records but cannot apply to the GPU object itself
 * (for example the texture `srgb` flag, or `loop` of a sample).
 *
 * Returns: (transfer full) (nullable): the sidecar, or %NULL when the
 *   asset or its sidecar does not exist
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_manager_get_asset_meta (LrgAssetManager *self,
                                                 const gchar     *name);

/**
 * lrg_asset_manager_load_audio:
 * @self: an #LrgAssetManager
 * @name: an audio asset reference (`guid:`, name or absolute path)
 * @error: (nullable): return location for error
 *
 * Loads an audio file the way its `.meta` sidecar asks: `mode: stream`
 * gives a cached #GrlMusic (lrg_asset_manager_load_music()), `mode:
 * sample` a cached #GrlSound (lrg_asset_manager_load_sound()), with
 * `volume` (and, for music, `loop`) applied. Without a sidecar, or with
 * #LrgAssetManager:apply-import-settings off, `.wav` files load as sounds
 * and other formats stream as music, like lrg_asset_manager_load_asset().
 *
 * Returns: (transfer none) (nullable): the cached #GrlSound or #GrlMusic,
 *   or %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_asset_manager_load_audio (LrgAssetManager  *self,
                                        const gchar      *name,
                                        GError          **error);

G_END_DECLS
