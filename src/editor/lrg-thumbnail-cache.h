/* lrg-thumbnail-cache.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * On-disk cache of asset thumbnails for the editor's assets panel.
 *
 * Thumbnails are PNG files named "<sha1 of the absolute path>-<mtime in
 * microseconds>-<size in bytes>.png" inside the cache directory, so an
 * edited file simply misses the cache and old versions are swept when the
 * new one is stored or when the path is invalidated.
 *
 * Textures and fonts with a ".meta" sidecar append the first 12 hex
 * digits of the sidecar's settings hash ("<sha1>-<mtime>-<size>-<hash>.png"),
 * so changing an import setting that affects the preview (a texture's
 * max-size, a font's size or code points) misses the cache too.
 *
 * Generation is incremental and kept off the frame path:
 *
 *  - CPU kinds render on a worker thread: textures and sprites
 *    (png/jpg/tga/bmp/pep, downscaled to the sidecar's max-size first),
 *    levels and prefabs (a top-down schematic of node positions) and
 *    audio (a min/max waveform envelope decoded with raylib's
 *    LoadWave/LoadWaveSamples, which need no audio device and no GL);
 *  - GPU kinds need the GL context, so they run on the main thread
 *    through a per-kind renderer callback, at most N per
 *    lrg_thumbnail_cache_process() call (one per frame in the editor):
 *    models (lrg_thumbnail_cache_set_renderer()) and font samples
 *    (lrg_thumbnail_cache_set_kind_renderer() with
 *    %LRG_THUMBNAIL_KIND_FONT). The font hook is implemented by the UI:
 *    load the font at the sidecar's size and code points, draw a sample
 *    such as "Aa Bb 123" into a render texture of the requested size and
 *    read it back into a #GrlImage.
 *
 * Results are delivered on the main thread by
 * lrg_thumbnail_cache_process() through the #LrgThumbnailCache::ready
 * signal.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgThumbnailKind:
 * @LRG_THUMBNAIL_KIND_NONE: no thumbnail for this file
 * @LRG_THUMBNAIL_KIND_IMAGE: a texture, scaled (CPU)
 * @LRG_THUMBNAIL_KIND_SCHEMATIC: a level or prefab node schematic (CPU)
 * @LRG_THUMBNAIL_KIND_WAVEFORM: an audio waveform envelope (CPU)
 * @LRG_THUMBNAIL_KIND_MODEL: a rendered model (GPU renderer hook)
 * @LRG_THUMBNAIL_KIND_FONT: a font sample (GPU renderer hook)
 *
 * How a thumbnail is produced for a file, from its extension.
 */
typedef enum
{
	LRG_THUMBNAIL_KIND_NONE,
	LRG_THUMBNAIL_KIND_IMAGE,
	LRG_THUMBNAIL_KIND_SCHEMATIC,
	LRG_THUMBNAIL_KIND_WAVEFORM,
	LRG_THUMBNAIL_KIND_MODEL,
	LRG_THUMBNAIL_KIND_FONT
} LrgThumbnailKind;

LRG_AVAILABLE_IN_ALL
GType lrg_thumbnail_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_THUMBNAIL_KIND (lrg_thumbnail_kind_get_type ())

#define LRG_TYPE_THUMBNAIL_CACHE (lrg_thumbnail_cache_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgThumbnailCache, lrg_thumbnail_cache, LRG, THUMBNAIL_CACHE, GObject)

/**
 * LRG_THUMBNAIL_DEFAULT_SIZE:
 *
 * Default longest edge of a thumbnail, in pixels.
 */
#define LRG_THUMBNAIL_DEFAULT_SIZE (96)

/**
 * LrgThumbnailRenderFunc:
 * @path: absolute path of the asset
 * @size: requested longest edge in pixels
 * @user_data: data passed to lrg_thumbnail_cache_set_renderer()
 *
 * Renders a GPU-only asset (a model or a font sample) to an image.
 * Called on the thread that calls lrg_thumbnail_cache_process(), which
 * must own the GL context. The asset's sidecar, if any, is
 * "@path.meta" (see lrg_asset_meta_load_for_asset()).
 *
 * Returns: (transfer full) (nullable): the rendered image, or %NULL
 */
typedef GrlImage * (*LrgThumbnailRenderFunc) (const gchar *path,
                                              guint        size,
                                              gpointer     user_data);

/**
 * lrg_thumbnail_cache_new:
 * @dir: (nullable): cache directory; %NULL uses
 *   $XDG_CACHE_HOME/libregnum/thumbnails
 *
 * Creates a cache. The directory is created on the first store.
 *
 * Returns: (transfer full): a new #LrgThumbnailCache
 */
LRG_AVAILABLE_IN_ALL
LrgThumbnailCache * lrg_thumbnail_cache_new (const gchar *dir);

/**
 * lrg_thumbnail_cache_get_dir:
 * @self: an #LrgThumbnailCache
 *
 * Returns: the cache directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_thumbnail_cache_get_dir (LrgThumbnailCache *self);

/**
 * lrg_thumbnail_cache_get_size:
 * @self: an #LrgThumbnailCache
 *
 * Returns: the longest thumbnail edge in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_thumbnail_cache_get_size (LrgThumbnailCache *self);

/**
 * lrg_thumbnail_cache_set_size:
 * @self: an #LrgThumbnailCache
 * @size: longest edge in pixels (16 .. 512)
 *
 * Sets the thumbnail size for future generations.
 */
LRG_AVAILABLE_IN_ALL
void lrg_thumbnail_cache_set_size (LrgThumbnailCache *self,
                                   guint              size);

/**
 * lrg_thumbnail_cache_make_key:
 * @path: an asset path
 * @mtime_usec: modification time in microseconds
 * @size: file size in bytes
 *
 * Builds the cache file name for one version of a file.
 *
 * Returns: (transfer full): "<sha1(path)>-<mtime>-<size>.png"
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_make_key (const gchar *path,
                                      gint64       mtime_usec,
                                      goffset      size);

/**
 * lrg_thumbnail_cache_make_key_full:
 * @path: an asset path
 * @mtime_usec: modification time in microseconds
 * @size: file size in bytes
 * @settings_hash: (nullable): the sidecar settings hash, or %NULL
 *
 * Like lrg_thumbnail_cache_make_key() with the first 12 characters of
 * @settings_hash appended before the extension.
 *
 * Returns: (transfer full): "<sha1(path)>-<mtime>-<size>[-<hash>].png"
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_make_key_full (const gchar *path,
                                           gint64       mtime_usec,
                                           goffset      size,
                                           const gchar *settings_hash);

/**
 * lrg_thumbnail_cache_get_kind:
 * @path: an asset path or file name
 *
 * Returns: how a thumbnail for @path is produced
 */
LRG_AVAILABLE_IN_ALL
LrgThumbnailKind lrg_thumbnail_cache_get_kind (const gchar *path);

/**
 * lrg_thumbnail_cache_get_entry_path:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 *
 * Returns: (transfer full) (nullable): where the thumbnail of the file's
 *   current version lives (whether or not it exists yet), or %NULL if the
 *   file cannot be read
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_get_entry_path (LrgThumbnailCache *self,
                                            const gchar       *path);

/**
 * lrg_thumbnail_cache_lookup:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 *
 * Returns: (transfer full) (nullable): the cached thumbnail of the file's
 *   current version, or %NULL on a miss
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_lookup (LrgThumbnailCache *self,
                                    const gchar       *path);

/**
 * lrg_thumbnail_cache_is_supported:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 *
 * Returns: whether a thumbnail can be made for @path (textures, levels,
 *   prefabs and audio always; models and fonts only with a renderer set
 *   for their kind)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_thumbnail_cache_is_supported (LrgThumbnailCache *self,
                                           const gchar       *path);

/**
 * lrg_thumbnail_cache_render_cpu:
 * @path: an asset path (texture, level, prefab or audio)
 * @size: longest edge in pixels
 * @error: return location for a #GError
 *
 * Renders a thumbnail without a GL context (safe on any thread):
 * textures are downscaled to their sidecar's max-size (when set) and
 * returned for fitting, levels and prefabs become a top-down schematic of
 * their nodes, audio becomes a waveform (see
 * lrg_thumbnail_cache_render_waveform()).
 *
 * Returns: (transfer full) (nullable): the image, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_thumbnail_cache_render_cpu (const gchar  *path,
                                           guint         size,
                                           GError      **error);

/**
 * lrg_thumbnail_cache_render_waveform:
 * @path: an audio file (wav, ogg, flac, mp3)
 * @width: image width in pixels (16 .. 512)
 * @height: image height in pixels (16 .. 512)
 * @error: return location for a #GError
 *
 * Decodes @path on the calling thread (no audio device needed) and draws
 * a centred min/max envelope of all channels, one column per pixel, in a
 * neutral grey on a dark background with a faint centre line. Long files
 * are downmixed to mono and resampled first so memory stays bounded.
 *
 * Returns: (transfer full) (nullable): an RGBA image, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_thumbnail_cache_render_waveform (const gchar  *path,
                                                guint         width,
                                                guint         height,
                                                GError      **error);

/**
 * lrg_thumbnail_cache_store:
 * @self: an #LrgThumbnailCache
 * @path: the asset the image belongs to
 * @image: the thumbnail image (scaled down to the cache size if larger)
 * @error: return location for a #GError
 *
 * Writes @image as the thumbnail of @path's current version (atomically)
 * and removes thumbnails of older versions.
 *
 * Returns: (transfer full) (nullable): the thumbnail path, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_store (LrgThumbnailCache  *self,
                                   const gchar        *path,
                                   GrlImage           *image,
                                   GError            **error);

/**
 * lrg_thumbnail_cache_generate:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 * @error: return location for a #GError
 *
 * Returns the cached thumbnail, generating it synchronously on a miss
 * (GPU kinds call the renderer, so call this on the GL thread for them).
 *
 * Returns: (transfer full) (nullable): the thumbnail path, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_thumbnail_cache_generate (LrgThumbnailCache  *self,
                                      const gchar        *path,
                                      GError            **error);

/**
 * lrg_thumbnail_cache_invalidate:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 *
 * Deletes every cached thumbnail of @path (all versions), e.g. when the
 * file watcher reports a change.
 *
 * Returns: the number of files removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_thumbnail_cache_invalidate (LrgThumbnailCache *self,
                                      const gchar       *path);

/**
 * lrg_thumbnail_cache_set_renderer:
 * @self: an #LrgThumbnailCache
 * @func: (nullable) (scope notified): renderer for GPU-only assets
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets the main-thread renderer used for models. Same as
 * lrg_thumbnail_cache_set_kind_renderer() with %LRG_THUMBNAIL_KIND_MODEL.
 */
LRG_AVAILABLE_IN_ALL
void lrg_thumbnail_cache_set_renderer (LrgThumbnailCache      *self,
                                       LrgThumbnailRenderFunc  func,
                                       gpointer                user_data,
                                       GDestroyNotify          destroy);

/**
 * lrg_thumbnail_cache_set_kind_renderer:
 * @self: an #LrgThumbnailCache
 * @kind: %LRG_THUMBNAIL_KIND_MODEL or %LRG_THUMBNAIL_KIND_FONT
 * @func: (nullable) (scope notified): renderer for that kind
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets the main-thread (GL) renderer for a GPU kind. Without one, files
 * of that kind are unsupported.
 */
LRG_AVAILABLE_IN_ALL
void lrg_thumbnail_cache_set_kind_renderer (LrgThumbnailCache      *self,
                                            LrgThumbnailKind        kind,
                                            LrgThumbnailRenderFunc  func,
                                            gpointer                user_data,
                                            GDestroyNotify          destroy);

/**
 * lrg_thumbnail_cache_request:
 * @self: an #LrgThumbnailCache
 * @path: an asset path
 *
 * Asks for a thumbnail asynchronously. Cache hits and finished
 * generations are reported by #LrgThumbnailCache::ready from
 * lrg_thumbnail_cache_process(). Repeated requests for a path that is
 * already pending are ignored.
 *
 * Returns: %FALSE if @path is unsupported
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_thumbnail_cache_request (LrgThumbnailCache *self,
                                      const gchar       *path);

/**
 * lrg_thumbnail_cache_process:
 * @self: an #LrgThumbnailCache
 * @max_gpu_jobs: how many GPU renders to run in this call
 *
 * Main-thread pump: runs up to @max_gpu_jobs queued GPU renders and emits
 * #LrgThumbnailCache::ready for every finished request.
 *
 * Returns: the number of ready notifications emitted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_thumbnail_cache_process (LrgThumbnailCache *self,
                                   guint              max_gpu_jobs);

/**
 * lrg_thumbnail_cache_get_n_pending:
 * @self: an #LrgThumbnailCache
 *
 * Returns: the number of requests not yet reported
 */
LRG_AVAILABLE_IN_ALL
guint lrg_thumbnail_cache_get_n_pending (LrgThumbnailCache *self);

G_END_DECLS
