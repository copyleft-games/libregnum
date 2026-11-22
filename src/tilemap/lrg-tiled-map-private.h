/* lrg-tiled-map-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Tiled importer's model, shared by the map (lrg-tiled-map.c) and
 * its two readers (lrg-tiled-tmj.c, lrg-tiled-tmx.c).  Both readers
 * fill exactly these structures, so the two formats cannot disagree
 * about anything past parsing.
 */

#pragma once

#include "lrg-tiled-map.h"

G_BEGIN_DECLS

/* One custom property; string is the canonical text form. */
typedef struct
{
	LrgTiledPropertyType type;
	gchar               *string;
} LrgTiledValue;

struct _LrgTiledProperties
{
	GObject     parent_instance;
	GHashTable *values;     /* name -> LrgTiledValue */
};

struct _LrgTiledObject
{
	GObject              parent_instance;
	guint                id;
	gchar               *name;
	gchar               *type;
	LrgTiledObjectShape  shape;
	gdouble              x;
	gdouble              y;
	gdouble              width;
	gdouble              height;
	gdouble              rotation;
	gboolean             visible;
	guint                gid;
	GArray              *points;      /* gfloat x, y pairs; NULL unless poly */
	gchar               *text;
	LrgTiledProperties  *properties;
};

struct _LrgTiledLayer
{
	GObject              parent_instance;
	LrgTiledLayerKind    kind;
	guint                id;
	gchar               *name;
	gboolean             visible;
	gdouble              opacity;
	gdouble              offset_x;
	gdouble              offset_y;
	gdouble              parallax_x;
	gdouble              parallax_y;
	LrgTiledProperties  *properties;
	guint                width;
	guint                height;
	guint               *gids;        /* width * height, tile layers only */
	GPtrArray           *objects;     /* LrgTiledObject, object layers only */
	gchar               *image_path;
};

/* Per-tile data of a tileset (only tiles that have any). */
typedef struct
{
	gchar              *type;
	LrgTiledProperties *properties;
	GArray             *anim_frames;  /* guint local ids */
	GArray             *anim_ms;      /* guint durations */
	GPtrArray          *shapes;       /* LrgTiledObject collision shapes */
} LrgTiledTile;

struct _LrgTiledTileset
{
	GObject              parent_instance;
	gchar               *name;
	guint                first_gid;
	guint                tile_width;
	guint                tile_height;
	guint                tile_count;
	guint                columns;
	guint                margin;
	guint                spacing;
	gint                 offset_x;
	gint                 offset_y;
	gchar               *image_path;
	guint                image_width;
	guint                image_height;
	LrgTiledProperties  *properties;
	GHashTable          *tiles;       /* GUINT_TO_POINTER (local id) -> LrgTiledTile */
	LrgTileset          *built;       /* lazily made by get_tileset() */
};

struct _LrgTiledMap
{
	GObject                parent_instance;
	gchar                 *base_dir;
	guint                  width;
	guint                  height;
	guint                  tile_width;
	guint                  tile_height;
	LrgTilemapOrientation  orientation;
	LrgTilemapStaggerAxis  stagger_axis;
	LrgTilemapStaggerIndex stagger_index;
	guint                  hex_side;
	gchar                 *background;
	LrgTiledProperties    *properties;
	GPtrArray             *layers;    /* LrgTiledLayer */
	GPtrArray             *tilesets;  /* LrgTiledTileset, by first gid */
};

/* What group layers pass down to the layers inside them. */
typedef struct
{
	const gchar *prefix;
	gboolean     visible;
	gdouble      opacity;
	gdouble      offset_x;
	gdouble      offset_y;
	gdouble      parallax_x;
	gdouble      parallax_y;
} LrgTiledGroupState;

/* ---- model helpers (lrg-tiled-map.c) ---------------------------------- */

G_GNUC_INTERNAL LrgTiledProperties * lrg_tiled_properties_new_empty (void);
G_GNUC_INTERNAL void lrg_tiled_properties_take (LrgTiledProperties   *self,
                                                const gchar          *name,
                                                LrgTiledPropertyType  type,
                                                gchar                *string);
G_GNUC_INTERNAL gboolean lrg_tiled_property_type_parse (const gchar          *name,
                                                        LrgTiledPropertyType *out);
G_GNUC_INTERNAL LrgTiledObject * lrg_tiled_object_new_empty (void);
G_GNUC_INTERNAL LrgTiledLayer * lrg_tiled_layer_new_empty (LrgTiledLayerKind kind);
G_GNUC_INTERNAL LrgTiledTileset * lrg_tiled_tileset_new_empty (void);
G_GNUC_INTERNAL LrgTiledTile * lrg_tiled_tileset_ensure_tile (LrgTiledTileset *self,
                                                             guint            local_id);
G_GNUC_INTERNAL LrgTiledMap * lrg_tiled_map_new_empty (const gchar *base_dir);
G_GNUC_INTERNAL gchar * lrg_tiled_resolve_path (const gchar *base_dir,
                                                const gchar *path);
G_GNUC_INTERNAL void lrg_tiled_layer_apply_group (LrgTiledLayer            *layer,
                                                  const gchar              *own_name,
                                                  const LrgTiledGroupState *group);
G_GNUC_INTERNAL gboolean lrg_tiled_decode_data (const gchar  *text,
                                                const gchar  *encoding,
                                                const gchar  *compression,
                                                guint        *out,
                                                guint         n_cells,
                                                GError      **error);
G_GNUC_INTERNAL gboolean lrg_tiled_map_finish (LrgTiledMap  *self,
                                               GError      **error);
G_GNUC_INTERNAL gchar * lrg_tiled_read_file (const gchar  *path,
                                             gsize        *length,
                                             GError      **error);

/* ---- readers ---------------------------------------------------------- */

G_GNUC_INTERNAL LrgTiledMap * lrg_tiled_parse_tmj (const gchar  *data,
                                                   gsize         length,
                                                   const gchar  *base_dir,
                                                   GError      **error);
G_GNUC_INTERNAL LrgTiledMap * lrg_tiled_parse_tmx (const gchar  *data,
                                                   gsize         length,
                                                   const gchar  *base_dir,
                                                   GError      **error);
/* External tilesets, read by either reader whatever the map's format. */
G_GNUC_INTERNAL gboolean lrg_tiled_load_external_tileset (LrgTiledTileset  *tileset,
                                                          const gchar      *path,
                                                          GError          **error);
G_GNUC_INTERNAL gboolean lrg_tiled_parse_tsj (LrgTiledTileset  *tileset,
                                              const gchar      *data,
                                              gsize             length,
                                              const gchar      *base_dir,
                                              GError          **error);
G_GNUC_INTERNAL gboolean lrg_tiled_parse_tsx (LrgTiledTileset  *tileset,
                                              const gchar      *data,
                                              gsize             length,
                                              const gchar      *base_dir,
                                              GError          **error);

G_END_DECLS
