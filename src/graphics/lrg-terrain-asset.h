/* lrg-terrain-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainAsset: the `.rterrain` heightfield terrain asset.
 *
 * A terrain asset is a rectangular grid of chunks.  Every chunk has
 * `resolution` x `resolution` cells; neighbouring chunks share their
 * border samples, so the whole terrain is one grid of
 * (chunks_x * resolution + 1) x (chunks_z * resolution + 1) height
 * samples starting at the asset's local origin and growing towards +X
 * and +Z.  On top of the heights it holds up to
 * %LRG_TERRAIN_ASSET_MAX_LAYERS surface layers (texture, colour, tiling,
 * optional material) blended per sample by splat weights (or chosen per
 * cell by material ids), a hole flag per cell, level-of-detail settings
 * and the #LrgFoliage scatter layers.  The YAML document references
 * binary sidecars next to it (16-bit PNG or raw float heights, RGBA PNG
 * splat weights, gray PNG ids, holes and foliage density) so big grids
 * stay out of the text.  Everything here is headless; the
 * #LrgTerrainComponent draws an asset and gives it a physics body.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../physics/lrg-physics-shape.h"
#include "lrg-foliage.h"

G_BEGIN_DECLS

/**
 * LRG_TERRAIN_ASSET_FORMAT_VERSION:
 *
 * The `.rterrain` format version this build writes (and the newest it
 * reads).
 */
#define LRG_TERRAIN_ASSET_FORMAT_VERSION (1)

/**
 * LRG_TERRAIN_ASSET_MAX_LAYERS:
 *
 * The most surface (splat) layers one terrain holds: two RGBA weight
 * sidecars of four layers each.
 */
#define LRG_TERRAIN_ASSET_MAX_LAYERS (8)

/**
 * LRG_TERRAIN_ASSET_MAX_RESOLUTION:
 *
 * The most cells along one side of a chunk (a chunk mesh then has
 * 129 x 129 vertices plus its skirts, within 16-bit indices).
 */
#define LRG_TERRAIN_ASSET_MAX_RESOLUTION (128)

/**
 * LRG_TERRAIN_ASSET_MAX_CHUNKS:
 *
 * The most chunks along one side of a terrain asset.
 */
#define LRG_TERRAIN_ASSET_MAX_CHUNKS (256)

/**
 * LRG_TERRAIN_ASSET_MAX_LOD:
 *
 * The most level-of-detail levels (level n uses every 2^n-th sample).
 */
#define LRG_TERRAIN_ASSET_MAX_LOD (6)

/**
 * LRG_TERRAIN_ASSET_ERROR:
 *
 * Error domain of #LrgTerrainAsset.
 */
#define LRG_TERRAIN_ASSET_ERROR (lrg_terrain_asset_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_terrain_asset_error_quark (void);

/**
 * LrgTerrainAssetError:
 * @LRG_TERRAIN_ASSET_ERROR_PARSE: the document is not valid YAML or has
 *   the wrong shape (a value of the wrong kind)
 * @LRG_TERRAIN_ASSET_ERROR_INVALID: a value is out of range or the data
 *   does not fit the grid (a sidecar of the wrong size)
 * @LRG_TERRAIN_ASSET_ERROR_IO: a file could not be read or written
 *
 * Errors of #LrgTerrainAsset loading, saving and editing.
 */
typedef enum
{
	LRG_TERRAIN_ASSET_ERROR_PARSE,
	LRG_TERRAIN_ASSET_ERROR_INVALID,
	LRG_TERRAIN_ASSET_ERROR_IO
} LrgTerrainAssetError;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_asset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_ASSET_ERROR (lrg_terrain_asset_error_get_type ())

/**
 * LrgTerrainHeightFormat:
 * @LRG_TERRAIN_HEIGHT_FORMAT_PNG16: a 16-bit grayscale PNG; 0 .. 65535
 *   map linearly onto the asset's height range (lossy: 1/65535 of the
 *   range)
 * @LRG_TERRAIN_HEIGHT_FORMAT_R32: raw little-endian 32-bit floats, row
 *   by row (exact)
 *
 * How the height sidecar is stored.
 */
typedef enum
{
	LRG_TERRAIN_HEIGHT_FORMAT_PNG16,
	LRG_TERRAIN_HEIGHT_FORMAT_R32
} LrgTerrainHeightFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_height_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_HEIGHT_FORMAT (lrg_terrain_height_format_get_type ())

/**
 * LrgTerrainSplatMode:
 * @LRG_TERRAIN_SPLAT_MODE_WEIGHTS: every sample has a weight per layer
 *   (normalised to sum 1); layers blend smoothly
 * @LRG_TERRAIN_SPLAT_MODE_IDS: every cell names one layer (hard edges,
 *   tile-like)
 *
 * How surface layers are assigned.
 */
typedef enum
{
	LRG_TERRAIN_SPLAT_MODE_WEIGHTS,
	LRG_TERRAIN_SPLAT_MODE_IDS
} LrgTerrainSplatMode;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_splat_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_SPLAT_MODE (lrg_terrain_splat_mode_get_type ())

/**
 * LrgTerrainEditKind:
 * @LRG_TERRAIN_EDIT_HEIGHTS: height samples (region in samples)
 * @LRG_TERRAIN_EDIT_SPLAT: splat weights (region in samples)
 * @LRG_TERRAIN_EDIT_MATERIAL_IDS: per-cell material ids (region in cells)
 * @LRG_TERRAIN_EDIT_HOLES: per-cell hole flags (region in cells)
 * @LRG_TERRAIN_EDIT_FOLIAGE: one foliage layer's density map (region in
 *   cells)
 *
 * What an edit (and a #LrgTerrainAsset::region-changed emission) touches.
 */
typedef enum
{
	LRG_TERRAIN_EDIT_HEIGHTS,
	LRG_TERRAIN_EDIT_SPLAT,
	LRG_TERRAIN_EDIT_MATERIAL_IDS,
	LRG_TERRAIN_EDIT_HOLES,
	LRG_TERRAIN_EDIT_FOLIAGE
} LrgTerrainEditKind;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_edit_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_EDIT_KIND (lrg_terrain_edit_kind_get_type ())

/* ==========================================================================
 * LrgTerrainEdit
 * ========================================================================== */

/**
 * LrgTerrainEdit:
 *
 * An opaque, undo-friendly record of a rectangular region of one terrain
 * data set (heights, splat weights, material ids, holes or one foliage
 * density map) as it was before and after a change.  Brushes return one
 * per dab and per stroke; lrg_terrain_edit_apply() puts either state
 * back.
 */
typedef struct _LrgTerrainEdit LrgTerrainEdit;

#define LRG_TYPE_TERRAIN_EDIT (lrg_terrain_edit_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_edit_get_type (void) G_GNUC_CONST;

/**
 * lrg_terrain_edit_copy:
 * @edit: an #LrgTerrainEdit
 *
 * Returns: (transfer full): a deep copy of @edit
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEdit * lrg_terrain_edit_copy (const LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_free:
 * @edit: (nullable): an #LrgTerrainEdit
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_edit_free (LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_get_kind:
 * @edit: an #LrgTerrainEdit
 *
 * Returns: the data set the edit covers
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEditKind lrg_terrain_edit_get_kind (const LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_get_layer:
 * @edit: an #LrgTerrainEdit
 *
 * Returns: the foliage layer of a %LRG_TERRAIN_EDIT_FOLIAGE edit (0
 *   otherwise)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_edit_get_layer (const LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_get_region:
 * @edit: an #LrgTerrainEdit
 * @x: (out) (optional): first column (sample or cell, see
 *   #LrgTerrainEditKind)
 * @z: (out) (optional): first row
 * @width: (out) (optional): columns
 * @height: (out) (optional): rows
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_edit_get_region (const LrgTerrainEdit *edit,
                                  guint                *x,
                                  guint                *z,
                                  guint                *width,
                                  guint                *height);

/**
 * lrg_terrain_edit_is_empty:
 * @edit: an #LrgTerrainEdit
 *
 * Returns: %TRUE when the region is empty or before and after are equal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_edit_is_empty (const LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_get_before:
 * @edit: an #LrgTerrainEdit
 *
 * The region's data before the change, row by row: 4-byte native floats
 * for heights, %LRG_TERRAIN_ASSET_MAX_LAYERS bytes per sample for splat
 * weights, one byte per cell otherwise.
 *
 * Returns: (transfer none): the old data
 */
LRG_AVAILABLE_IN_ALL
GBytes * lrg_terrain_edit_get_before (const LrgTerrainEdit *edit);

/**
 * lrg_terrain_edit_get_after:
 * @edit: an #LrgTerrainEdit
 *
 * Returns: (transfer none): the region's data after the change (same
 *   layout as lrg_terrain_edit_get_before())
 */
LRG_AVAILABLE_IN_ALL
GBytes * lrg_terrain_edit_get_after (const LrgTerrainEdit *edit);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTerrainEdit, lrg_terrain_edit_free)

/* ==========================================================================
 * LrgTerrainLayer
 * ========================================================================== */

#define LRG_TYPE_TERRAIN_LAYER (lrg_terrain_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainLayer, lrg_terrain_layer, LRG, TERRAIN_LAYER, GObject)

/**
 * lrg_terrain_layer_new:
 * @name: (nullable): the layer's name
 *
 * Creates a white, untextured layer with a tiling of 4 world units.
 *
 * Returns: (transfer full): a new #LrgTerrainLayer
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainLayer * lrg_terrain_layer_new (const gchar *name);

/**
 * lrg_terrain_layer_get_name:
 * @self: an #LrgTerrainLayer
 *
 * Returns: (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_layer_get_name (LrgTerrainLayer *self);

/**
 * lrg_terrain_layer_set_name:
 * @self: an #LrgTerrainLayer
 * @name: (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_name (LrgTerrainLayer *self,
                                 const gchar     *name);

/**
 * lrg_terrain_layer_get_texture:
 * @self: an #LrgTerrainLayer
 *
 * Returns: (nullable): the albedo texture path (relative to the asset's
 *   directory, absolute, or a `guid:` reference)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_layer_get_texture (LrgTerrainLayer *self);

/**
 * lrg_terrain_layer_set_texture:
 * @self: an #LrgTerrainLayer
 * @texture: (nullable): the albedo texture path
 *
 * Also forgets pixels loaded from the old path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_texture (LrgTerrainLayer *self,
                                    const gchar     *texture);

/**
 * lrg_terrain_layer_get_material:
 * @self: an #LrgTerrainLayer
 *
 * Returns: (nullable): the `.rmat` reference kept for the layer (tools
 *   and future shading; the baked albedo uses texture and colour)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_layer_get_material (LrgTerrainLayer *self);

/**
 * lrg_terrain_layer_set_material:
 * @self: an #LrgTerrainLayer
 * @material: (nullable): a `.rmat` path or `guid:` reference
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_material (LrgTerrainLayer *self,
                                     const gchar     *material);

/**
 * lrg_terrain_layer_get_tiling:
 * @self: an #LrgTerrainLayer
 *
 * Returns: the world size one repeat of the texture covers
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_layer_get_tiling (LrgTerrainLayer *self);

/**
 * lrg_terrain_layer_set_tiling:
 * @self: an #LrgTerrainLayer
 * @tiling: world units per texture repeat (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_tiling (LrgTerrainLayer *self,
                                   gfloat           tiling);

/**
 * lrg_terrain_layer_get_color:
 * @self: an #LrgTerrainLayer
 * @out: (out caller-allocates) (array fixed-size=4): RGBA, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_get_color (LrgTerrainLayer *self,
                                  gfloat          *out);

/**
 * lrg_terrain_layer_set_color:
 * @self: an #LrgTerrainLayer
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1 (unused by the bake, kept)
 *
 * The colour the layer's texture is multiplied by (the whole albedo
 * without a texture).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_color (LrgTerrainLayer *self,
                                  gfloat           r,
                                  gfloat           g,
                                  gfloat           b,
                                  gfloat           a);

/**
 * lrg_terrain_layer_set_pixels:
 * @self: an #LrgTerrainLayer
 * @width: image width (> 0)
 * @height: image height (> 0)
 * @rgba: (array) (nullable): @width * @height RGBA8 pixels, or %NULL to
 *   go back to the texture file
 *
 * Gives the layer in-memory albedo pixels (procedural textures, tests,
 * editor previews) that the splat bake samples instead of loading the
 * texture file.  The pixels are copied; they are not saved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_layer_set_pixels (LrgTerrainLayer *self,
                                   guint            width,
                                   guint            height,
                                   const guint8    *rgba);

/**
 * lrg_terrain_layer_has_pixels:
 * @self: an #LrgTerrainLayer
 *
 * Returns: %TRUE when the layer has albedo pixels (set or loaded)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_layer_has_pixels (LrgTerrainLayer *self);

/* ==========================================================================
 * LrgTerrainAsset
 * ========================================================================== */

#define LRG_TYPE_TERRAIN_ASSET (lrg_terrain_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainAsset, lrg_terrain_asset, LRG, TERRAIN_ASSET, GObject)

/**
 * lrg_terrain_asset_new:
 * @resolution: cells along one chunk side, 1 ..
 *   %LRG_TERRAIN_ASSET_MAX_RESOLUTION (a power of two lets every LOD
 *   level divide it)
 * @chunk_size: world size of one chunk side (> 0)
 * @chunks_x: chunks along X, 1 .. %LRG_TERRAIN_ASSET_MAX_CHUNKS
 * @chunks_z: chunks along Z, 1 .. %LRG_TERRAIN_ASSET_MAX_CHUNKS
 *
 * Creates a flat terrain (every height 0) with one layer ("base") at
 * full weight, no holes, 3 LOD levels switching at 2 and 4 chunk sizes,
 * a skirt of a tenth of a chunk and a 256-texel splat bake per chunk.
 * Out-of-range values are clamped.
 *
 * Returns: (transfer full): a new #LrgTerrainAsset
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_terrain_asset_new (guint  resolution,
                                         gfloat chunk_size,
                                         guint  chunks_x,
                                         guint  chunks_z);

/**
 * lrg_terrain_asset_new_from_file:
 * @path: a `.rterrain` file
 * @error: return location for a #GError
 *
 * Loads a terrain and its sidecars (resolved next to @path).
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 *   set (%LRG_TERRAIN_ASSET_ERROR; messages read "file: /key/path:
 *   problem")
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_terrain_asset_new_from_file (const gchar  *path,
                                                   GError      **error);

/**
 * lrg_terrain_asset_new_from_data:
 * @data: the YAML text
 * @length: its length, or -1 when nul-terminated
 * @base_dir: (nullable): the directory sidecars resolve against; %NULL
 *   reads the document only (heights 0, first layer at full weight, no
 *   holes, full foliage density) while keeping the sidecar names, which
 *   is what editor validation wants
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_terrain_asset_new_from_data (const gchar  *data,
                                                   gssize        length,
                                                   const gchar  *base_dir,
                                                   GError      **error);

/**
 * lrg_terrain_asset_save_to_file:
 * @self: an #LrgTerrainAsset
 * @path: where to write the `.rterrain`
 * @error: return location for a #GError
 *
 * Writes the YAML document and its sidecars into @path's directory.  The
 * sidecars are named after @path's stem (`island.rterrain` ->
 * `island.heights.png`, `island.splat0.png`, `island.ids.png`,
 * `island.holes.png`, `island.foliage0.png`, ...); a sidecar holding only
 * default data (no hole, full foliage density) is not written.  The
 * base directory becomes @path's directory.  Same data, same bytes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_save_to_file (LrgTerrainAsset  *self,
                                         const gchar      *path,
                                         GError          **error);

/**
 * lrg_terrain_asset_to_data:
 * @self: an #LrgTerrainAsset
 * @length: (out) (optional): the text length
 * @error: return location for a #GError
 *
 * The YAML document lrg_terrain_asset_save_to_file() would write for the
 * last file the asset was loaded from or saved to (sidecar names from
 * that file's stem, or from the name, or "terrain"); no sidecar is
 * written.
 *
 * Returns: (transfer full) (nullable): the document
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_terrain_asset_to_data (LrgTerrainAsset  *self,
                                   gsize            *length,
                                   GError          **error);

/**
 * lrg_terrain_asset_get_name:
 * @self: an #LrgTerrainAsset
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_asset_get_name (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_name:
 * @self: an #LrgTerrainAsset
 * @name: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_name (LrgTerrainAsset *self,
                                 const gchar     *name);

/**
 * lrg_terrain_asset_get_base_dir:
 * @self: an #LrgTerrainAsset
 *
 * Returns: (nullable): the directory relative layer textures resolve
 *   against (the file's directory after a load or save)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_asset_get_base_dir (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_base_dir:
 * @self: an #LrgTerrainAsset
 * @base_dir: (nullable): the directory
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_base_dir (LrgTerrainAsset *self,
                                     const gchar     *base_dir);

/**
 * lrg_terrain_asset_get_resolution:
 * @self: an #LrgTerrainAsset
 *
 * Returns: cells along one chunk side
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_resolution (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_get_chunk_size:
 * @self: an #LrgTerrainAsset
 *
 * Returns: world size of one chunk side
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_chunk_size (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_get_cell_size:
 * @self: an #LrgTerrainAsset
 *
 * Returns: world size of one cell side (chunk size / resolution)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_cell_size (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_get_chunks:
 * @self: an #LrgTerrainAsset
 * @chunks_x: (out) (optional): chunks along X
 * @chunks_z: (out) (optional): chunks along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_get_chunks (LrgTerrainAsset *self,
                                   guint           *chunks_x,
                                   guint           *chunks_z);

/**
 * lrg_terrain_asset_get_samples:
 * @self: an #LrgTerrainAsset
 * @samples_x: (out) (optional): height samples along X
 *   (chunks_x * resolution + 1)
 * @samples_z: (out) (optional): height samples along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_get_samples (LrgTerrainAsset *self,
                                    guint           *samples_x,
                                    guint           *samples_z);

/**
 * lrg_terrain_asset_get_cells:
 * @self: an #LrgTerrainAsset
 * @cells_x: (out) (optional): cells along X (samples_x - 1)
 * @cells_z: (out) (optional): cells along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_get_cells (LrgTerrainAsset *self,
                                  guint           *cells_x,
                                  guint           *cells_z);

/**
 * lrg_terrain_asset_get_size:
 * @self: an #LrgTerrainAsset
 * @width: (out) (optional): world extent along X
 * @depth: (out) (optional): world extent along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_get_size (LrgTerrainAsset *self,
                                 gfloat          *width,
                                 gfloat          *depth);

/**
 * lrg_terrain_asset_resize:
 * @self: an #LrgTerrainAsset
 * @chunks_x: new chunks along X (1 .. %LRG_TERRAIN_ASSET_MAX_CHUNKS)
 * @chunks_z: new chunks along Z
 *
 * Grows or shrinks the grid at its +X / +Z edges.  Kept samples and
 * cells keep their data; new samples copy the nearest old edge sample
 * (height and weights), new cells have no hole, material id 0 and full
 * foliage density.  Emits #LrgTerrainAsset::region-changed over the
 * whole grid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_resize (LrgTerrainAsset *self,
                               guint            chunks_x,
                               guint            chunks_z);

/**
 * lrg_terrain_asset_get_height:
 * @self: an #LrgTerrainAsset
 * @x: sample column (clamped)
 * @z: sample row (clamped)
 *
 * Returns: the height of sample (@x, @z)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_height (LrgTerrainAsset *self,
                                     guint            x,
                                     guint            z);

/**
 * lrg_terrain_asset_set_height:
 * @self: an #LrgTerrainAsset
 * @x: sample column
 * @z: sample row
 * @height: the new height (finite)
 *
 * Sets one sample and emits #LrgTerrainAsset::region-changed (prefer
 * lrg_terrain_asset_set_heights() or a brush for many samples).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_height (LrgTerrainAsset *self,
                                   guint            x,
                                   guint            z,
                                   gfloat           height);

/**
 * lrg_terrain_asset_get_heights:
 * @self: an #LrgTerrainAsset
 * @n_heights: (out) (optional): samples_x * samples_z
 *
 * Returns: (transfer none) (array length=n_heights): every sample, row
 *   major (index = z * samples_x + x)
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_terrain_asset_get_heights (LrgTerrainAsset *self,
                                              guint           *n_heights);

/**
 * lrg_terrain_asset_set_heights:
 * @self: an #LrgTerrainAsset
 * @heights: (array length=n_heights): samples_x * samples_z heights,
 *   row major
 * @n_heights: the number of heights
 * @error: return location for a #GError
 *
 * Replaces every height.  Refused (%LRG_TERRAIN_ASSET_ERROR_INVALID)
 * for a wrong count or a non-finite height.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_set_heights (LrgTerrainAsset  *self,
                                        const gfloat     *heights,
                                        guint             n_heights,
                                        GError          **error);

/**
 * lrg_terrain_asset_sample_height:
 * @self: an #LrgTerrainAsset
 * @x: local X (world units from the asset origin)
 * @z: local Z
 * @height: (out) (optional): the height there, bilinear between the four
 *   samples around the point (the two triangles of a cell agree with it
 *   at the samples and along the cell edges)
 *
 * Returns: %TRUE when (@x, @z) lies on the grid and not in a hole
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_sample_height (LrgTerrainAsset *self,
                                          gfloat           x,
                                          gfloat           z,
                                          gfloat          *height);

/**
 * lrg_terrain_asset_sample_normal:
 * @self: an #LrgTerrainAsset
 * @x: local X
 * @z: local Z
 * @out: (out caller-allocates) (array fixed-size=3): the unit normal
 *   (central differences, bilinear between samples; up off the grid)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_sample_normal (LrgTerrainAsset *self,
                                      gfloat           x,
                                      gfloat           z,
                                      gfloat          *out);

/**
 * lrg_terrain_asset_get_height_range:
 * @self: an #LrgTerrainAsset
 * @min: (out) (optional): the lowest height a 16-bit sidecar encodes
 * @max: (out) (optional): the highest
 *
 * The range a %LRG_TERRAIN_HEIGHT_FORMAT_PNG16 sidecar maps 0 .. 65535
 * onto.  Saving widens it to the data when a height lies outside.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_get_height_range (LrgTerrainAsset *self,
                                         gfloat          *min,
                                         gfloat          *max);

/**
 * lrg_terrain_asset_set_height_range:
 * @self: an #LrgTerrainAsset
 * @min: lowest height
 * @max: highest height (> @min)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_height_range (LrgTerrainAsset *self,
                                         gfloat           min,
                                         gfloat           max);

/**
 * lrg_terrain_asset_get_height_format:
 * @self: an #LrgTerrainAsset
 *
 * Returns: how the height sidecar is written
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainHeightFormat lrg_terrain_asset_get_height_format (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_height_format:
 * @self: an #LrgTerrainAsset
 * @format: the sidecar format for the next save
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_height_format (LrgTerrainAsset        *self,
                                          LrgTerrainHeightFormat  format);

/**
 * lrg_terrain_asset_get_n_layers:
 * @self: an #LrgTerrainAsset
 *
 * Returns: the number of surface layers (at least 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_n_layers (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_get_layer:
 * @self: an #LrgTerrainAsset
 * @index: the layer index
 *
 * Returns: (transfer none) (nullable): the layer, %NULL past the last
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainLayer * lrg_terrain_asset_get_layer (LrgTerrainAsset *self,
                                               guint            index);

/**
 * lrg_terrain_asset_add_layer:
 * @self: an #LrgTerrainAsset
 * @name: (nullable): the new layer's name
 *
 * Appends a layer with weight 0 everywhere.  Refused (returns %NULL) at
 * %LRG_TERRAIN_ASSET_MAX_LAYERS.
 *
 * Returns: (transfer none) (nullable): the new layer
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainLayer * lrg_terrain_asset_add_layer (LrgTerrainAsset *self,
                                               const gchar     *name);

/**
 * lrg_terrain_asset_remove_layer:
 * @self: an #LrgTerrainAsset
 * @index: the layer to remove (the last one cannot be removed)
 *
 * Removes a layer; its weight goes to the remaining layers (renormalised)
 * and material ids above it shift down.
 *
 * Returns: %TRUE when a layer was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_remove_layer (LrgTerrainAsset *self,
                                         guint            index);

/**
 * lrg_terrain_asset_get_splat_mode:
 * @self: an #LrgTerrainAsset
 *
 * Returns: whether layers blend by weights or are picked per cell
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainSplatMode lrg_terrain_asset_get_splat_mode (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_splat_mode:
 * @self: an #LrgTerrainAsset
 * @mode: the mode
 *
 * Both data sets are kept; the mode picks the one that is shown and
 * saved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_splat_mode (LrgTerrainAsset     *self,
                                       LrgTerrainSplatMode  mode);

/**
 * lrg_terrain_asset_get_weight:
 * @self: an #LrgTerrainAsset
 * @x: sample column (clamped)
 * @z: sample row (clamped)
 * @layer: the layer
 *
 * Returns: the layer's weight at the sample, 0 .. 1 (the weights of a
 *   sample sum to 1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_weight (LrgTerrainAsset *self,
                                     guint            x,
                                     guint            z,
                                     guint            layer);

/**
 * lrg_terrain_asset_set_weights:
 * @self: an #LrgTerrainAsset
 * @x: sample column
 * @z: sample row
 * @weights: (array length=n_weights): one weight per layer (>= 0;
 *   missing layers are 0)
 * @n_weights: the number of weights
 *
 * Sets a sample's weights, normalised to sum 1 and quantised to bytes
 * (all zero means the first layer).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_weights (LrgTerrainAsset *self,
                                    guint            x,
                                    guint            z,
                                    const gfloat    *weights,
                                    guint            n_weights);

/**
 * lrg_terrain_asset_sample_weights:
 * @self: an #LrgTerrainAsset
 * @x: local X
 * @z: local Z
 * @out: (out caller-allocates) (array fixed-size=8): the bilinear
 *   weight of every layer slot at the point (in material-ids mode, 1 for
 *   the cell's layer); slots past the last layer are 0
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_sample_weights (LrgTerrainAsset *self,
                                       gfloat           x,
                                       gfloat           z,
                                       gfloat          *out);

/**
 * lrg_terrain_asset_get_cell_material:
 * @self: an #LrgTerrainAsset
 * @x: cell column (clamped)
 * @z: cell row (clamped)
 *
 * Returns: the cell's layer in material-ids mode
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_cell_material (LrgTerrainAsset *self,
                                           guint            x,
                                           guint            z);

/**
 * lrg_terrain_asset_set_cell_material:
 * @self: an #LrgTerrainAsset
 * @x: cell column
 * @z: cell row
 * @layer: a layer index (clamped to the last layer)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_cell_material (LrgTerrainAsset *self,
                                          guint            x,
                                          guint            z,
                                          guint            layer);

/**
 * lrg_terrain_asset_get_hole:
 * @self: an #LrgTerrainAsset
 * @x: cell column
 * @z: cell row
 *
 * Returns: %TRUE when the cell is a hole (not drawn, no collision); %FALSE
 *   off the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_get_hole (LrgTerrainAsset *self,
                                     guint            x,
                                     guint            z);

/**
 * lrg_terrain_asset_set_hole:
 * @self: an #LrgTerrainAsset
 * @x: cell column
 * @z: cell row
 * @hole: whether the cell is a hole
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_hole (LrgTerrainAsset *self,
                                 guint            x,
                                 guint            z,
                                 gboolean         hole);

/**
 * lrg_terrain_asset_get_hole_count:
 * @self: an #LrgTerrainAsset
 *
 * Returns: the number of hole cells
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_hole_count (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_get_lod_levels:
 * @self: an #LrgTerrainAsset
 *
 * Returns: the number of LOD levels (1 .. %LRG_TERRAIN_ASSET_MAX_LOD)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_lod_levels (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_lod_levels:
 * @self: an #LrgTerrainAsset
 * @levels: 1 .. %LRG_TERRAIN_ASSET_MAX_LOD (clamped)
 *
 * New levels switch at twice the previous level's distance.  Levels
 * whose step does not divide the resolution draw at the finest step
 * that does.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_lod_levels (LrgTerrainAsset *self,
                                       guint            levels);

/**
 * lrg_terrain_asset_get_lod_distance:
 * @self: an #LrgTerrainAsset
 * @level: 1 .. levels - 1
 *
 * Returns: the distance from the eye to a chunk's bounds at which @level
 *   takes over from @level - 1 (0 for level 0 or out of range)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_lod_distance (LrgTerrainAsset *self,
                                           guint            level);

/**
 * lrg_terrain_asset_set_lod_distance:
 * @self: an #LrgTerrainAsset
 * @level: 1 .. levels - 1
 * @distance: the switch distance (> 0); later levels are pushed out so
 *   distances keep increasing
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_lod_distance (LrgTerrainAsset *self,
                                         guint            level,
                                         gfloat           distance);

/**
 * lrg_terrain_asset_select_lod:
 * @self: an #LrgTerrainAsset
 * @distance: the distance from the eye to a chunk
 *
 * Returns: the LOD level for that distance
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_select_lod (LrgTerrainAsset *self,
                                    gfloat           distance);

/**
 * lrg_terrain_asset_get_skirt_depth:
 * @self: an #LrgTerrainAsset
 *
 * Returns: how far the chunk skirts hang below the border (hides cracks
 *   between chunks drawn at different LOD levels; 0 = no skirts)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_asset_get_skirt_depth (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_skirt_depth:
 * @self: an #LrgTerrainAsset
 * @depth: skirt depth (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_skirt_depth (LrgTerrainAsset *self,
                                        gfloat           depth);

/**
 * lrg_terrain_asset_get_bake_resolution:
 * @self: an #LrgTerrainAsset
 *
 * Returns: texels along one side of a chunk's baked splat texture
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_get_bake_resolution (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_set_bake_resolution:
 * @self: an #LrgTerrainAsset
 * @texels: 16 .. 2048 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_asset_set_bake_resolution (LrgTerrainAsset *self,
                                            guint            texels);

/**
 * lrg_terrain_asset_bake_chunk:
 * @self: an #LrgTerrainAsset
 * @cx: chunk column
 * @cz: chunk row
 * @out_rgba: (out caller-allocates) (array): bake_resolution² RGBA8
 *   texels, row by row along +Z
 *
 * Bakes a chunk's albedo: at every texel centre the layer weights
 * (bilinear, or the cell's id) mix each layer's colour times its
 * texture, sampled with the layer's world tiling (texture pixels from
 * lrg_terrain_layer_set_pixels() or the texture file, loaded once).
 * Headless.
 *
 * Returns: %TRUE when the chunk exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_bake_chunk (LrgTerrainAsset *self,
                                       guint            cx,
                                       guint            cz,
                                       guint8          *out_rgba);

/**
 * lrg_terrain_asset_get_chunk_mesh_counts:
 * @self: an #LrgTerrainAsset
 * @cx: chunk column
 * @cz: chunk row
 * @lod: LOD level
 * @n_vertices: (out) (optional): vertices (skirts included)
 * @n_triangles: (out) (optional): triangles (holes skipped)
 *
 * The size of the mesh the terrain component draws for a chunk.
 *
 * Returns: %TRUE when the chunk exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_get_chunk_mesh_counts (LrgTerrainAsset *self,
                                                  guint            cx,
                                                  guint            cz,
                                                  guint            lod,
                                                  guint           *n_vertices,
                                                  guint           *n_triangles);

/**
 * lrg_terrain_asset_build_physics_shape:
 * @self: an #LrgTerrainAsset
 * @scale_x: world scale along X (1 for none, > 0)
 * @scale_y: world scale of the heights (> 0)
 * @scale_z: world scale along Z (> 0)
 * @error: return location for a #GError
 *
 * A heightfield #LrgPhysicsShape of the whole grid, sample (x, z) at
 * local (x * cell_size * @scale_x, height * @scale_y, z * cell_size *
 * @scale_z).  A sample becomes a %LRG_PHYSICS_HEIGHTFIELD_HOLE when every
 * cell around it is a hole, so hole regions at least two cells wide lose
 * their collision exactly.
 *
 * Returns: (transfer full) (nullable): the shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_terrain_asset_build_physics_shape (LrgTerrainAsset  *self,
                                                         gfloat            scale_x,
                                                         gfloat            scale_y,
                                                         gfloat            scale_z,
                                                         GError          **error);

/**
 * lrg_terrain_asset_raycast:
 * @self: an #LrgTerrainAsset
 * @origin: (array fixed-size=3): ray origin in local space
 * @direction: (array fixed-size=3): ray direction (need not be unit)
 * @max_distance: the longest hit distance, in units of @direction's
 *   length
 * @out_point: (out caller-allocates) (array fixed-size=3) (optional):
 *   the local hit point
 *
 * Marches the ray over the grid (half-cell steps, then bisection to a
 * thousandth of a cell); holes are not hit.  This is the brush cursor's
 * pick.
 *
 * Returns: %TRUE on a hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_asset_raycast (LrgTerrainAsset *self,
                                    const gfloat    *origin,
                                    const gfloat    *direction,
                                    gfloat           max_distance,
                                    gfloat          *out_point);

/**
 * lrg_terrain_asset_get_foliage:
 * @self: an #LrgTerrainAsset
 *
 * Returns: (transfer none): the foliage scatter layers saved with the
 *   terrain (always present, maybe without layers)
 */
LRG_AVAILABLE_IN_ALL
LrgFoliage * lrg_terrain_asset_get_foliage (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_scatter_foliage:
 * @self: an #LrgTerrainAsset
 *
 * Places the instances of every layer of lrg_terrain_asset_get_foliage()
 * on the terrain now (the terrain component also does it lazily for the
 * patches a change touched).  Deterministic: the same terrain, density
 * maps and seeds always give the same instances.
 *
 * Returns: the total number of instances
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_asset_scatter_foliage (LrgTerrainAsset *self);

/**
 * lrg_terrain_asset_capture_edit:
 * @self: an #LrgTerrainAsset
 * @kind: the data set
 * @layer: the foliage layer (%LRG_TERRAIN_EDIT_FOLIAGE only)
 * @x: first column (samples or cells, see #LrgTerrainEditKind)
 * @z: first row
 * @width: columns (clipped to the grid)
 * @height: rows (clipped)
 *
 * Snapshots a region into a new edit whose before and after are both the
 * current data; change the data, then lrg_terrain_edit_capture_after().
 *
 * Returns: (transfer full) (nullable): the edit, %NULL for an empty region
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEdit * lrg_terrain_asset_capture_edit (LrgTerrainAsset    *self,
                                                 LrgTerrainEditKind  kind,
                                                 guint               layer,
                                                 guint               x,
                                                 guint               z,
                                                 guint               width,
                                                 guint               height);

/**
 * lrg_terrain_edit_apply:
 * @edit: an #LrgTerrainEdit
 * @asset: the #LrgTerrainAsset the edit was made on
 * @after: %TRUE to write the "after" state (redo), %FALSE for "before"
 *   (undo)
 *
 * Writes one state of the region back into @asset and emits
 * #LrgTerrainAsset::region-changed.  Nothing happens (and %FALSE is
 * returned) when the region no longer fits the asset.
 *
 * Returns: %TRUE when the data was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_edit_apply (const LrgTerrainEdit *edit,
                                 LrgTerrainAsset      *asset,
                                 gboolean              after);

/**
 * lrg_terrain_edit_capture_after:
 * @edit: an #LrgTerrainEdit made by lrg_terrain_asset_capture_edit()
 * @asset: the asset, changed since the capture
 *
 * Refreshes the "after" state from @asset's current data (for tools
 * that change the data themselves between a capture and the commit).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_edit_capture_after (LrgTerrainEdit  *edit,
                                     LrgTerrainAsset *asset);

G_END_DECLS
