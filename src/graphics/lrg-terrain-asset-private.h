/* lrg-terrain-asset-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal helpers shared by the terrain asset, the terrain brush, the
 * foliage scatter layers, the water body and the three world3d
 * components (terrain, foliage, water).  Not installed.
 *
 *  - TerrainReader: strict YAML readers that report "source: /path:
 *    message" errors (every terrain-family format uses them).
 *  - A minimal PNG codec built on GIO's zlib converters: 8-bit gray,
 *    16-bit gray and 8-bit RGBA are written (filter "Up" on every row,
 *    deterministic bytes), and every non-interlaced 8/16-bit gray, gray
 *    alpha, RGB and RGBA file is read.  The sidecars of a .rterrain
 *    (heights, splat weights, material ids, holes, foliage density) use
 *    it so the engine needs no image library for them and the files
 *    load headless.
 *  - Direct access to the asset's sample and cell arrays for the brush
 *    and the foliage scatter, and the chunk mesh builder the terrain
 *    component uploads.
 */

#pragma once

#include <glib-object.h>
#include <yaml-glib.h>
#include "lrg-terrain-asset.h"
#include "lrg-foliage.h"
#include "../world3d/lrg-world3d.h"
#include "../world3d/lrg-entity3d.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Strict YAML reading
 * ========================================================================== */

/*
 * TerrainReader:
 * @source: the file name or "<data>" used as the message prefix
 * @domain: the error domain of every failure
 * @code: the error code of every failure
 * @error: where the first failure goes
 */
typedef struct
{
	const gchar  *source;
	GQuark        domain;
	gint          code;
	GError      **error;
} TerrainReader;

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_fail (TerrainReader *rd,
                               const gchar   *path,
                               const gchar   *format,
                               ...) G_GNUC_PRINTF (3, 4);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_mapping (TerrainReader  *rd,
                                  YamlNode       *node,
                                  const gchar    *path,
                                  YamlMapping   **out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_sequence (TerrainReader  *rd,
                                   YamlNode       *node,
                                   const gchar    *path,
                                   YamlSequence  **out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_number (TerrainReader *rd,
                                 YamlNode      *node,
                                 const gchar   *path,
                                 gdouble        min,
                                 gdouble        max,
                                 gfloat        *out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_uint (TerrainReader *rd,
                               YamlNode      *node,
                               const gchar   *path,
                               guint          min,
                               guint          max,
                               guint         *out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_string (TerrainReader  *rd,
                                 YamlNode       *node,
                                 const gchar    *path,
                                 const gchar   **out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_boolean (TerrainReader *rd,
                                  YamlNode      *node,
                                  const gchar   *path,
                                  gboolean      *out);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_numbers (TerrainReader *rd,
                                  YamlNode      *node,
                                  const gchar   *path,
                                  guint          min_count,
                                  guint          max_count,
                                  gfloat        *out,
                                  guint         *out_count);

G_GNUC_INTERNAL
gboolean _lrg_terrain_rd_enum (TerrainReader *rd,
                               YamlNode      *node,
                               const gchar   *path,
                               GType          enum_type,
                               gint          *out);

/* Stores @member under @key in *@extra (created on demand). */
G_GNUC_INTERNAL
void     _lrg_terrain_keep_extra (YamlMapping **extra,
                                  const gchar  *key,
                                  YamlNode     *member);

/* Appends every member of @extra (nullable) to @map. */
G_GNUC_INTERNAL
void     _lrg_terrain_write_extra (YamlMapping *map,
                                   YamlMapping *extra);

/* Sets @key to @node in @map and drops the caller's reference. */
G_GNUC_INTERNAL
void     _lrg_terrain_set_member (YamlMapping *map,
                                  const gchar *key,
                                  YamlNode    *node);

/* A flow-free list of @n floats written with lrg_data_yaml_float(). */
G_GNUC_INTERNAL
YamlNode *_lrg_terrain_float_list (const gfloat *values,
                                   guint         n);

/* The nick of @value in @enum_type. */
G_GNUC_INTERNAL
const gchar *_lrg_terrain_enum_nick (GType enum_type,
                                     gint  value);

/* ==========================================================================
 * PNG sidecars
 * ========================================================================== */

/*
 * TerrainImage:
 * @width: pixels per row
 * @height: rows
 * @channels: 1 (gray), 2 (gray alpha), 3 (RGB) or 4 (RGBA)
 * @depth: 8 or 16 bits per channel
 * @data: rows of @width * @channels samples; 16-bit samples are stored
 *   as native guint16 values (not big endian)
 */
typedef struct
{
	guint   width;
	guint   height;
	guint   channels;
	guint   depth;
	guint8 *data;
} TerrainImage;

G_GNUC_INTERNAL
void      _lrg_terrain_image_clear (TerrainImage *image);

/* The sample (@x, @y, channel @c) widened to 16 bits (8-bit * 257). */
G_GNUC_INTERNAL
guint16   _lrg_terrain_image_get16 (const TerrainImage *image,
                                    guint               x,
                                    guint               y,
                                    guint               c);

/* Encodes a PNG: @depth 8 or 16, @channels 1 or 4; @samples as above. */
G_GNUC_INTERNAL
GBytes   *_lrg_terrain_png_encode (guint          width,
                                   guint          height,
                                   guint          channels,
                                   guint          depth,
                                   gconstpointer  samples,
                                   GError       **error);

G_GNUC_INTERNAL
gboolean  _lrg_terrain_png_decode (const guint8  *bytes,
                                   gsize          length,
                                   TerrainImage  *out,
                                   GError       **error);

G_GNUC_INTERNAL
gboolean  _lrg_terrain_png_load (const gchar   *path,
                                 TerrainImage  *out,
                                 GError       **error);

G_GNUC_INTERNAL
gboolean  _lrg_terrain_png_save (const gchar   *path,
                                 guint          width,
                                 guint          height,
                                 guint          channels,
                                 guint          depth,
                                 gconstpointer  samples,
                                 GError       **error);

/* ==========================================================================
 * Asset internals
 * ========================================================================== */

/* The stride of the weights array: one byte per layer slot per sample. */
#define LRG_TERRAIN_WEIGHT_STRIDE (LRG_TERRAIN_ASSET_MAX_LAYERS)

/* Samples (nx * nz floats), mutable; callers emit the change themselves. */
G_GNUC_INTERNAL
gfloat   *_lrg_terrain_asset_heights (LrgTerrainAsset *self);

/* Weights (nx * nz * LRG_TERRAIN_WEIGHT_STRIDE bytes), mutable. */
G_GNUC_INTERNAL
guint8   *_lrg_terrain_asset_weights (LrgTerrainAsset *self);

/* Per-cell material ids and hole flags ((nx - 1) * (nz - 1) bytes). */
G_GNUC_INTERNAL
guint8   *_lrg_terrain_asset_ids (LrgTerrainAsset *self);
G_GNUC_INTERNAL
guint8   *_lrg_terrain_asset_holes (LrgTerrainAsset *self);

/* Emits LrgTerrainAsset::region-changed (and keeps the foliage in sync). */
G_GNUC_INTERNAL
void      _lrg_terrain_asset_emit_changed (LrgTerrainAsset    *self,
                                           LrgTerrainEditKind  kind,
                                           guint               layer,
                                           guint               x,
                                           guint               z,
                                           guint               width,
                                           guint               height);

/* An edit from parts (takes references on @before and @after). */
G_GNUC_INTERNAL
LrgTerrainEdit *_lrg_terrain_edit_new (LrgTerrainEditKind  kind,
                                       guint               layer,
                                       guint               x,
                                       guint               z,
                                       guint               width,
                                       guint               height,
                                       GBytes             *before,
                                       GBytes             *after);

/* Bytes per element of an edit kind's data. */
G_GNUC_INTERNAL
gsize     _lrg_terrain_edit_elem_size (LrgTerrainEditKind kind);

/* Renormalises sample @s's weights so they sum to 255 (largest gets the
 * rounding rest); all-zero weights become layer 0 = 255. */
G_GNUC_INTERNAL
void      _lrg_terrain_normalize_weights (guint8 *w,
                                          guint   n_layers);

/*
 * TerrainChunkMesh:
 * The CPU geometry of one chunk at one LOD, in asset-local space (the
 * entity's model space): positions, normals, texcoords (0 .. 1 across
 * the chunk, for the baked splat texture), 16-bit indices; skirt
 * vertices hang below the four borders.
 */
typedef struct
{
	gfloat  *positions;
	gfloat  *normals;
	gfloat  *texcoords;
	guint16 *indices;
	guint    n_vertices;
	guint    n_indices;
	gfloat   min[3];
	gfloat   max[3];
} TerrainChunkMesh;

G_GNUC_INTERNAL
void      _lrg_terrain_chunk_mesh_clear (TerrainChunkMesh *mesh);

G_GNUC_INTERNAL
gboolean  _lrg_terrain_asset_build_chunk (LrgTerrainAsset  *self,
                                          guint             cx,
                                          guint             cz,
                                          guint             lod,
                                          TerrainChunkMesh *out);

/* ==========================================================================
 * Foliage internals (lrg-foliage.c)
 * ========================================================================== */

G_GNUC_INTERNAL
gboolean  _lrg_foliage_read (LrgFoliage    *self,
                             TerrainReader *rd,
                             YamlNode      *node,
                             const gchar   *path,
                             const gchar   *base_dir);

/* The foliage block; density sidecars are written into @dir (nullable:
 * names only), named from @stem when missing or when @renew. */
G_GNUC_INTERNAL
YamlNode *_lrg_foliage_write (LrgFoliage  *self,
                              const gchar *dir,
                              const gchar *stem,
                              gboolean     renew,
                              GError     **error);

/* Re-scatters every dirty patch of every layer on @terrain. */
G_GNUC_INTERNAL
void      _lrg_foliage_scatter (LrgFoliage      *self,
                                LrgTerrainAsset *terrain);

/* TRUE when the foliage has nothing worth writing (no layers). */
G_GNUC_INTERNAL
gboolean  _lrg_foliage_is_empty (LrgFoliage *self);

/* Density bytes of layer @layer (cells_x * cells_z), mutable. */
G_GNUC_INTERNAL
guint8   *_lrg_foliage_density (LrgFoliage *self,
                                guint       layer);

/* The asset resized: density maps follow (new cells start at 255). */
G_GNUC_INTERNAL
void      _lrg_foliage_set_grid (LrgFoliage *self,
                                 guint       cells_x,
                                 guint       cells_z,
                                 gfloat      cell_size);

/*
 * _lrg_foliage_collect_render:
 * Adds the foliage's render items (one per layer and visible patch) to
 * @list, placed by @entity's world matrix; @terrain (nullable) is the
 * ground the instances were scattered on (re-scattered when dirty).
 */
G_GNUC_INTERNAL
void      _lrg_foliage_collect_render (LrgFoliage      *self,
                                       LrgTerrainAsset *terrain,
                                       LrgRenderList   *list,
                                       LrgEntity3D     *entity,
                                       LrgComponent    *component,
                                       gint             layer_override,
                                       gfloat           time);

G_END_DECLS
