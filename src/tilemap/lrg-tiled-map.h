/* lrg-tiled-map.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTiledMap: a map made in the Tiled editor (mapeditor.org), read from
 * its JSON format (.tmj, with .tsj tilesets) or its XML format (.tmx,
 * with .tsx tilesets).  The importer keeps everything an application
 * reads out of a map -- tile layers with their flip flags, tilesets
 * with per-tile properties, animations and collision shapes, object
 * layers with named rectangles, points, ellipses and polygons, and the
 * custom properties on all of them -- and turns tile layers into
 * LrgTilemap layers drawn by the existing tilemap renderer.
 *
 * The XML reader is GLib's GMarkup and the JSON reader json-glib, so the
 * importer adds no dependency; zlib and gzip layer data is inflated with
 * GIO's GZlibDecompressor.  zstd-compressed data, infinite (chunked)
 * maps and object templates are refused with LRG_TILED_ERROR_UNSUPPORTED.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-tilemap.h"
#include "lrg-tileset.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors and enumerations
 * ========================================================================== */

/**
 * LRG_TILED_ERROR:
 *
 * Error domain of the Tiled importer.
 *
 * Since: 0.11.0
 */
#define LRG_TILED_ERROR (lrg_tiled_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_tiled_error_quark (void);

/**
 * LrgTiledError:
 * @LRG_TILED_ERROR_IO: a file could not be read
 * @LRG_TILED_ERROR_PARSE: the document is not well-formed JSON or XML
 * @LRG_TILED_ERROR_INVALID: the document is well-formed but is not a valid
 *   Tiled map or tileset (a missing size, a layer whose data does not fill
 *   it, a bad encoding)
 * @LRG_TILED_ERROR_UNSUPPORTED: a valid Tiled feature this importer does
 *   not read: infinite maps, zstd compression, object templates
 *
 * Errors from the Tiled importer.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_TILED_ERROR_IO,
	LRG_TILED_ERROR_PARSE,
	LRG_TILED_ERROR_INVALID,
	LRG_TILED_ERROR_UNSUPPORTED
} LrgTiledError;

LRG_AVAILABLE_IN_ALL
GType lrg_tiled_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILED_ERROR (lrg_tiled_error_get_type ())

/**
 * LrgTiledLayerKind:
 * @LRG_TILED_LAYER_TILES: a tile layer (a grid of global tile ids)
 * @LRG_TILED_LAYER_OBJECTS: an object layer ("object group")
 * @LRG_TILED_LAYER_IMAGE: an image layer
 *
 * What a #LrgTiledLayer holds.  Group layers are not kept as layers:
 * their children are listed in their place, with the group's name as a
 * path prefix and its offset, opacity, visibility and parallax folded in.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_TILED_LAYER_TILES,
	LRG_TILED_LAYER_OBJECTS,
	LRG_TILED_LAYER_IMAGE
} LrgTiledLayerKind;

LRG_AVAILABLE_IN_ALL
GType lrg_tiled_layer_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILED_LAYER_KIND (lrg_tiled_layer_kind_get_type ())

/**
 * LrgTiledObjectShape:
 * @LRG_TILED_OBJECT_RECTANGLE: a rectangle (x, y is its top-left corner)
 * @LRG_TILED_OBJECT_POINT: a point (width and height are 0)
 * @LRG_TILED_OBJECT_ELLIPSE: an ellipse inscribed in the rectangle
 * @LRG_TILED_OBJECT_POLYGON: a closed polygon; points relative to x, y
 * @LRG_TILED_OBJECT_POLYLINE: an open polyline; points relative to x, y
 * @LRG_TILED_OBJECT_TILE: a tile object (it has a gid; x, y is its
 *   bottom-left corner on orthogonal maps, as in Tiled)
 * @LRG_TILED_OBJECT_TEXT: a text object
 *
 * The shape of a #LrgTiledObject.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_TILED_OBJECT_RECTANGLE,
	LRG_TILED_OBJECT_POINT,
	LRG_TILED_OBJECT_ELLIPSE,
	LRG_TILED_OBJECT_POLYGON,
	LRG_TILED_OBJECT_POLYLINE,
	LRG_TILED_OBJECT_TILE,
	LRG_TILED_OBJECT_TEXT
} LrgTiledObjectShape;

LRG_AVAILABLE_IN_ALL
GType lrg_tiled_object_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILED_OBJECT_SHAPE (lrg_tiled_object_shape_get_type ())

/**
 * LrgTiledPropertyType:
 * @LRG_TILED_PROPERTY_STRING: text
 * @LRG_TILED_PROPERTY_INT: an integer
 * @LRG_TILED_PROPERTY_FLOAT: a floating point number
 * @LRG_TILED_PROPERTY_BOOL: a boolean
 * @LRG_TILED_PROPERTY_COLOR: a colour, as Tiled writes it ("#AARRGGBB")
 * @LRG_TILED_PROPERTY_FILE: a path, resolved against the file it is in
 * @LRG_TILED_PROPERTY_OBJECT: an object id (0 = none)
 *
 * The type of a custom property.  A class property is not a type of its
 * own: its members are flattened into "name.member" keys.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_TILED_PROPERTY_STRING,
	LRG_TILED_PROPERTY_INT,
	LRG_TILED_PROPERTY_FLOAT,
	LRG_TILED_PROPERTY_BOOL,
	LRG_TILED_PROPERTY_COLOR,
	LRG_TILED_PROPERTY_FILE,
	LRG_TILED_PROPERTY_OBJECT
} LrgTiledPropertyType;

LRG_AVAILABLE_IN_ALL
GType lrg_tiled_property_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILED_PROPERTY_TYPE (lrg_tiled_property_type_get_type ())

/**
 * LRG_TILED_GID_FLIP_HEXAGONAL:
 *
 * Tiled's fourth flag bit: a hexagonal tile rotated by 120 degrees.  It
 * is kept in raw gids (lrg_tiled_layer_get_gid()) but has no LrgTilemap
 * equivalent, so lrg_tiled_map_build_tilemap() drops it.
 *
 * Since: 0.11.0
 */
#define LRG_TILED_GID_FLIP_HEXAGONAL ((guint) 0x10000000)

/**
 * LRG_TILED_GID_MASK:
 *
 * The bits of a raw Tiled gid that hold the global tile id (all four
 * flag bits cleared).  The three flip bits are the same bits as
 * %LRG_TILE_FLIP_HORIZONTAL, %LRG_TILE_FLIP_VERTICAL and
 * %LRG_TILE_FLIP_DIAGONAL.
 *
 * Since: 0.11.0
 */
#define LRG_TILED_GID_MASK ((guint) 0x0FFFFFFF)

/* ==========================================================================
 * LrgTiledProperties
 * ========================================================================== */

#define LRG_TYPE_TILED_PROPERTIES (lrg_tiled_properties_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledProperties, lrg_tiled_properties, LRG, TILED_PROPERTIES, GObject)

/**
 * lrg_tiled_properties_get_n_properties:
 * @self: an #LrgTiledProperties
 *
 * Returns: how many properties there are
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_properties_get_n_properties (LrgTiledProperties *self);

/**
 * lrg_tiled_properties_get_keys:
 * @self: an #LrgTiledProperties
 *
 * Returns: (transfer full) (array zero-terminated=1): the property names,
 *   sorted
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_tiled_properties_get_keys (LrgTiledProperties *self);

/**
 * lrg_tiled_properties_has:
 * @self: an #LrgTiledProperties
 * @name: a property name
 *
 * Returns: %TRUE if the property exists
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_properties_has (LrgTiledProperties *self,
                                   const gchar        *name);

/**
 * lrg_tiled_properties_get_value_type:
 * @self: an #LrgTiledProperties
 * @name: a property name
 *
 * Returns: the property's type (%LRG_TILED_PROPERTY_STRING when absent)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledPropertyType lrg_tiled_properties_get_value_type (LrgTiledProperties *self,
                                                          const gchar        *name);

/**
 * lrg_tiled_properties_get_string:
 * @self: an #LrgTiledProperties
 * @name: a property name
 *
 * Every property has a string form: the text Tiled wrote ("true",
 * "12", "#ff00ff00", a resolved path).
 *
 * Returns: (transfer none) (nullable): the value as text, or %NULL when
 *   absent
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_properties_get_string (LrgTiledProperties *self,
                                               const gchar        *name);

/**
 * lrg_tiled_properties_get_int:
 * @self: an #LrgTiledProperties
 * @name: a property name
 * @fallback: returned when the property is absent or not a number
 *
 * Reads an int, float (truncated), bool (0/1) or numeric string property.
 *
 * Returns: the value
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_tiled_properties_get_int (LrgTiledProperties *self,
                                     const gchar        *name,
                                     gint64              fallback);

/**
 * lrg_tiled_properties_get_double:
 * @self: an #LrgTiledProperties
 * @name: a property name
 * @fallback: returned when the property is absent or not a number
 *
 * Returns: the value as a double
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_tiled_properties_get_double (LrgTiledProperties *self,
                                         const gchar        *name,
                                         gdouble             fallback);

/**
 * lrg_tiled_properties_get_boolean:
 * @self: an #LrgTiledProperties
 * @name: a property name
 * @fallback: returned when the property is absent or not a boolean
 *
 * Reads a bool property, a number (non-zero is %TRUE) or the strings
 * "true"/"false"/"yes"/"no"/"1"/"0".
 *
 * Returns: the value
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_properties_get_boolean (LrgTiledProperties *self,
                                           const gchar        *name,
                                           gboolean            fallback);

/* ==========================================================================
 * LrgTiledObject
 * ========================================================================== */

#define LRG_TYPE_TILED_OBJECT (lrg_tiled_object_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledObject, lrg_tiled_object, LRG, TILED_OBJECT, GObject)

/**
 * lrg_tiled_object_get_id:
 * @self: an #LrgTiledObject
 *
 * Returns: the object's id, unique within its map (0 for collision shapes
 *   inside a tileset that were saved without one)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_object_get_id (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_name:
 * @self: an #LrgTiledObject
 *
 * Returns: (transfer none): the object's name ("" when unnamed)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_object_get_name (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_object_type:
 * @self: an #LrgTiledObject
 *
 * The object's class (Tiled 1.9 and later) or type (earlier versions);
 * both are read into this one field.
 *
 * Returns: (transfer none): the type ("" when none)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_object_get_object_type (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_shape:
 * @self: an #LrgTiledObject
 *
 * Returns: the object's shape
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledObjectShape lrg_tiled_object_get_shape (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_bounds:
 * @self: an #LrgTiledObject
 * @x: (out) (optional): x in map pixels, layer offset included
 * @y: (out) (optional): y in map pixels, layer offset included
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 *
 * Gets the object's position and size as Tiled stores them (unrotated).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_object_get_bounds (LrgTiledObject *self,
                                  gdouble        *x,
                                  gdouble        *y,
                                  gdouble        *width,
                                  gdouble        *height);

/**
 * lrg_tiled_object_get_rotation:
 * @self: an #LrgTiledObject
 *
 * Returns: the clockwise rotation in degrees around (x, y)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_tiled_object_get_rotation (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_visible:
 * @self: an #LrgTiledObject
 *
 * Returns: %TRUE when the object is visible
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_object_get_visible (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_gid:
 * @self: an #LrgTiledObject
 *
 * Returns: the raw gid of a tile object (flip flags included), 0 otherwise
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_object_get_gid (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_points:
 * @self: an #LrgTiledObject
 * @n_coords: (out): number of floats (two per point)
 *
 * The vertices of a polygon or polyline, relative to the object's
 * position, as x, y pairs.
 *
 * Returns: (transfer none) (array length=n_coords) (nullable): the
 *   coordinates, %NULL for other shapes
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_tiled_object_get_points (LrgTiledObject *self,
                                            guint          *n_coords);

/**
 * lrg_tiled_object_get_text:
 * @self: an #LrgTiledObject
 *
 * Returns: (transfer none) (nullable): the text of a text object
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_object_get_text (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_properties:
 * @self: an #LrgTiledObject
 *
 * Returns: (transfer none): the object's custom properties
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledProperties * lrg_tiled_object_get_properties (LrgTiledObject *self);

/**
 * lrg_tiled_object_get_center:
 * @self: an #LrgTiledObject
 * @x: (out): centre x in map pixels
 * @y: (out): centre y in map pixels
 *
 * The point an application puts something at: a point object's position;
 * the centre of a rectangle or ellipse, with its rotation applied; the
 * average of a polygon's or polyline's vertices; the centre of a tile
 * object (whose position is its bottom-left corner).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_object_get_center (LrgTiledObject *self,
                                  gdouble        *x,
                                  gdouble        *y);

/**
 * lrg_tiled_object_contains_point:
 * @self: an #LrgTiledObject
 * @x: x in map pixels
 * @y: y in map pixels
 *
 * Whether a point lies inside a rectangle, ellipse, polygon or tile
 * object (rotation included); edges count as inside.  Points and
 * polylines contain nothing.  This is what makes an object a zone.
 *
 * Returns: %TRUE if (@x, @y) is inside
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_object_contains_point (LrgTiledObject *self,
                                          gdouble         x,
                                          gdouble         y);

/* ==========================================================================
 * LrgTiledLayer
 * ========================================================================== */

#define LRG_TYPE_TILED_LAYER (lrg_tiled_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledLayer, lrg_tiled_layer, LRG, TILED_LAYER, GObject)

/**
 * lrg_tiled_layer_get_kind:
 * @self: an #LrgTiledLayer
 *
 * Returns: what the layer holds
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledLayerKind lrg_tiled_layer_get_kind (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_id:
 * @self: an #LrgTiledLayer
 *
 * Returns: the layer id Tiled assigned (0 in files older than Tiled 1.2)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_layer_get_id (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_name:
 * @self: an #LrgTiledLayer
 *
 * The layer's name, prefixed with the names of the groups it is in
 * ("upstairs/furniture").
 *
 * Returns: (transfer none): the name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_layer_get_name (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_visible:
 * @self: an #LrgTiledLayer
 *
 * Returns: %TRUE when the layer and every group around it are visible
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_layer_get_visible (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_opacity:
 * @self: an #LrgTiledLayer
 *
 * Returns: the opacity, multiplied through enclosing groups (0 to 1)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_tiled_layer_get_opacity (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_offset:
 * @self: an #LrgTiledLayer
 * @x: (out) (optional): horizontal offset in pixels
 * @y: (out) (optional): vertical offset in pixels
 *
 * Gets the layer's drawing offset, enclosing groups included.  Object
 * positions already include it.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_layer_get_offset (LrgTiledLayer *self,
                                 gdouble       *x,
                                 gdouble       *y);

/**
 * lrg_tiled_layer_get_parallax:
 * @self: an #LrgTiledLayer
 * @x: (out) (optional): horizontal parallax factor
 * @y: (out) (optional): vertical parallax factor
 *
 * Gets the layer's parallax factors, multiplied through enclosing groups.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_layer_get_parallax (LrgTiledLayer *self,
                                   gdouble       *x,
                                   gdouble       *y);

/**
 * lrg_tiled_layer_get_properties:
 * @self: an #LrgTiledLayer
 *
 * Returns: (transfer none): the layer's custom properties
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledProperties * lrg_tiled_layer_get_properties (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_width:
 * @self: an #LrgTiledLayer
 *
 * Returns: the width in cells of a tile layer, 0 for other layers
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_layer_get_width (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_height:
 * @self: an #LrgTiledLayer
 *
 * Returns: the height in cells of a tile layer, 0 for other layers
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_layer_get_height (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_gid:
 * @self: an #LrgTiledLayer
 * @x: column
 * @y: row
 *
 * Reads one cell of a tile layer: the raw gid, flip flags included
 * (mask with %LRG_TILED_GID_MASK for the global tile id).
 *
 * Returns: the gid, 0 for an empty cell or outside the layer
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_layer_get_gid (LrgTiledLayer *self,
                               guint          x,
                               guint          y);

/**
 * lrg_tiled_layer_get_gids:
 * @self: an #LrgTiledLayer
 * @n_gids: (out): width * height
 *
 * Returns: (transfer none) (array length=n_gids) (nullable): every cell
 *   of a tile layer in row order, %NULL for other layers
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_tiled_layer_get_gids (LrgTiledLayer *self,
                                        guint         *n_gids);

/**
 * lrg_tiled_layer_get_n_objects:
 * @self: an #LrgTiledLayer
 *
 * Returns: the number of objects in an object layer (0 for other layers)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_layer_get_n_objects (LrgTiledLayer *self);

/**
 * lrg_tiled_layer_get_object:
 * @self: an #LrgTiledLayer
 * @index: object index, in file order
 *
 * Returns: (transfer none) (nullable): the object, %NULL when out of range
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledObject * lrg_tiled_layer_get_object (LrgTiledLayer *self,
                                             guint          index);

/**
 * lrg_tiled_layer_get_image_path:
 * @self: an #LrgTiledLayer
 *
 * Returns: (transfer none) (nullable): an image layer's image, resolved
 *   against the map's directory
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_layer_get_image_path (LrgTiledLayer *self);

/* ==========================================================================
 * LrgTiledTileset
 * ========================================================================== */

#define LRG_TYPE_TILED_TILESET (lrg_tiled_tileset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledTileset, lrg_tiled_tileset, LRG, TILED_TILESET, GObject)

/**
 * lrg_tiled_tileset_get_name:
 * @self: an #LrgTiledTileset
 *
 * Returns: (transfer none): the tileset's name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_tileset_get_name (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_first_gid:
 * @self: an #LrgTiledTileset
 *
 * Returns: the global id of the tileset's first tile in this map
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_tileset_get_first_gid (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_tile_count:
 * @self: an #LrgTiledTileset
 *
 * Returns: the number of tiles
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_tileset_get_tile_count (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_columns:
 * @self: an #LrgTiledTileset
 *
 * Returns: tiles per row of the tileset image
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_tileset_get_columns (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_tile_size:
 * @self: an #LrgTiledTileset
 * @width: (out) (optional): tile width in pixels
 * @height: (out) (optional): tile height in pixels
 *
 * Gets the size of one tile.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_tileset_get_tile_size (LrgTiledTileset *self,
                                      guint           *width,
                                      guint           *height);

/**
 * lrg_tiled_tileset_get_image_path:
 * @self: an #LrgTiledTileset
 *
 * Returns: (transfer none) (nullable): the tileset image, resolved against
 *   the file that names it; %NULL for an image-collection tileset
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_tileset_get_image_path (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_properties:
 * @self: an #LrgTiledTileset
 *
 * Returns: (transfer none): the tileset's own custom properties
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledProperties * lrg_tiled_tileset_get_properties (LrgTiledTileset *self);

/**
 * lrg_tiled_tileset_get_tile_properties:
 * @self: an #LrgTiledTileset
 * @local_id: the tile's id within the tileset (0-based)
 *
 * Returns: (transfer none) (nullable): the tile's custom properties, %NULL
 *   when the tile has none
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledProperties * lrg_tiled_tileset_get_tile_properties (LrgTiledTileset *self,
                                                            guint            local_id);

/**
 * lrg_tiled_tileset_get_tile_type:
 * @self: an #LrgTiledTileset
 * @local_id: the tile's id within the tileset (0-based)
 *
 * Returns: (transfer none) (nullable): the tile's class or type
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_tileset_get_tile_type (LrgTiledTileset *self,
                                               guint            local_id);

/**
 * lrg_tiled_tileset_get_tileset:
 * @self: an #LrgTiledTileset
 *
 * The tileset as an #LrgTileset: tile size, columns, margin, spacing,
 * tile offset and image path, every tile property as tile data (its
 * string form; the tile's class under the key "type"), tile animations,
 * and rectangle and polygon collision shapes.  The texture is not
 * loaded; call lrg_tiled_map_load_textures() with a GL context.
 *
 * Returns: (transfer none) (nullable): the tileset, %NULL for an
 *   image-collection tileset (one image per tile), which LrgTileset
 *   cannot hold
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tiled_tileset_get_tileset (LrgTiledTileset *self);

/* ==========================================================================
 * LrgTiledMap
 * ========================================================================== */

#define LRG_TYPE_TILED_MAP (lrg_tiled_map_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledMap, lrg_tiled_map, LRG, TILED_MAP, GObject)

/**
 * lrg_tiled_map_new_from_file:
 * @path: a .tmj (or .json) or .tmx (or .xml) file
 * @error: (nullable): return location for an error
 *
 * Loads a Tiled map.  The format is picked from the first character of
 * the file ('{' is JSON, '<' is XML), so the extension does not matter.
 * External tilesets (.tsj, .tsx) are read relative to the map, and every
 * image and file property is resolved against the file it appears in.
 *
 * Returns: (transfer full) (nullable): the map, or %NULL on error
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledMap * lrg_tiled_map_new_from_file (const gchar  *path,
                                           GError      **error);

/**
 * lrg_tiled_map_new_from_data:
 * @data: (array length=length) (element-type guint8): the document
 * @length: its length in bytes, or -1 when nul-terminated
 * @base_dir: (nullable): directory relative paths are resolved against
 *   (external tilesets, images); %NULL is the current directory
 * @error: (nullable): return location for an error
 *
 * Loads a Tiled map from memory; see lrg_tiled_map_new_from_file().
 *
 * Returns: (transfer full) (nullable): the map, or %NULL on error
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledMap * lrg_tiled_map_new_from_data (const gchar  *data,
                                           gssize        length,
                                           const gchar  *base_dir,
                                           GError      **error);

/**
 * lrg_tiled_map_get_width:
 * @self: an #LrgTiledMap
 *
 * Returns: the map width in cells
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_map_get_width (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_height:
 * @self: an #LrgTiledMap
 *
 * Returns: the map height in cells
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_map_get_height (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_tile_size:
 * @self: an #LrgTiledMap
 * @width: (out) (optional): cell width in pixels
 * @height: (out) (optional): cell height in pixels
 *
 * Gets the map's grid cell size.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_map_get_tile_size (LrgTiledMap *self,
                                  guint       *width,
                                  guint       *height);

/**
 * lrg_tiled_map_get_orientation:
 * @self: an #LrgTiledMap
 *
 * Returns: the map's orientation
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapOrientation lrg_tiled_map_get_orientation (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_background_color:
 * @self: an #LrgTiledMap
 *
 * Returns: (transfer none) (nullable): the background colour as Tiled
 *   wrote it ("#RRGGBB" or "#AARRGGBB"), %NULL when none
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tiled_map_get_background_color (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_properties:
 * @self: an #LrgTiledMap
 *
 * Returns: (transfer none): the map's custom properties
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledProperties * lrg_tiled_map_get_properties (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_n_layers:
 * @self: an #LrgTiledMap
 *
 * Returns: the number of layers (groups flattened)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_map_get_n_layers (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_layer:
 * @self: an #LrgTiledMap
 * @index: layer index, bottom first (Tiled's drawing order)
 *
 * Returns: (transfer none) (nullable): the layer
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledLayer * lrg_tiled_map_get_layer (LrgTiledMap *self,
                                         guint        index);

/**
 * lrg_tiled_map_get_layer_by_name:
 * @self: an #LrgTiledMap
 * @name: a layer name, with group prefixes ("upstairs/furniture")
 *
 * Returns: (transfer none) (nullable): the first layer with that name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledLayer * lrg_tiled_map_get_layer_by_name (LrgTiledMap *self,
                                                 const gchar *name);

/**
 * lrg_tiled_map_get_n_tilesets:
 * @self: an #LrgTiledMap
 *
 * Returns: the number of tilesets
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_map_get_n_tilesets (LrgTiledMap *self);

/**
 * lrg_tiled_map_get_tileset:
 * @self: an #LrgTiledMap
 * @index: tileset index, in first-gid order
 *
 * Returns: (transfer none) (nullable): the tileset
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledTileset * lrg_tiled_map_get_tileset (LrgTiledMap *self,
                                             guint        index);

/**
 * lrg_tiled_map_get_tileset_for_gid:
 * @self: an #LrgTiledMap
 * @gid: a raw gid (flags are ignored)
 * @local_id: (out) (optional): the tile's id within the tileset
 *
 * Finds the tileset a global tile id belongs to.
 *
 * Returns: (transfer none) (nullable): the tileset, %NULL for 0 or a gid
 *   past every tileset
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledTileset * lrg_tiled_map_get_tileset_for_gid (LrgTiledMap *self,
                                                     guint        gid,
                                                     guint       *local_id);

/**
 * lrg_tiled_map_find_object:
 * @self: an #LrgTiledMap
 * @name: an object name
 *
 * Finds an object by name in every object layer, bottom layer first.
 *
 * Returns: (transfer none) (nullable): the first object with that name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledObject * lrg_tiled_map_find_object (LrgTiledMap *self,
                                            const gchar *name);

/**
 * lrg_tiled_map_find_object_by_id:
 * @self: an #LrgTiledMap
 * @id: an object id
 *
 * Resolves an object id, for example the value of an object property.
 *
 * Returns: (transfer none) (nullable): the object
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledObject * lrg_tiled_map_find_object_by_id (LrgTiledMap *self,
                                                  guint        id);

/**
 * lrg_tiled_map_get_objects:
 * @self: an #LrgTiledMap
 * @object_type: (nullable): only objects of this class/type; %NULL for all
 *
 * Collects objects from every object layer, bottom layer first and in
 * file order within a layer.
 *
 * Returns: (transfer container) (element-type LrgTiledObject): the objects
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_tiled_map_get_objects (LrgTiledMap *self,
                                       const gchar *object_type);

/**
 * lrg_tiled_map_find_zone_at:
 * @self: an #LrgTiledMap
 * @x: x in map pixels
 * @y: y in map pixels
 * @object_type: (nullable): only zones of this class/type
 *
 * Finds the topmost area object (rectangle, ellipse, polygon or tile
 * object) containing a point: the last one in drawing order.
 *
 * Returns: (transfer none) (nullable): the object, %NULL when none
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledObject * lrg_tiled_map_find_zone_at (LrgTiledMap *self,
                                             gdouble      x,
                                             gdouble      y,
                                             const gchar *object_type);

/**
 * lrg_tiled_map_build_tilemap:
 * @self: an #LrgTiledMap
 * @tileset_index: the tileset the tilemap draws with
 * @error: (nullable): return location for an error
 *
 * Builds an #LrgTilemap from the map's tile layers.  An LrgTilemap
 * draws with one tileset, so each cell keeps only tiles of
 * @tileset_index (renumbered to that tileset's 1-based ids, flip flags
 * kept); other tilesets' cells are empty.  A map with several tilesets
 * builds one tilemap per tileset and draws them in turn.  Layer names,
 * visibility, opacity, parallax, orientation, stagger and hex side are
 * copied; a tile layer with a true "collision" (or "solid") property has
 * collision enabled.
 *
 * Returns: (transfer full) (nullable): the tilemap, %NULL on error
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_tiled_map_build_tilemap (LrgTiledMap  *self,
                                          guint         tileset_index,
                                          GError      **error);

/**
 * lrg_tiled_map_load_textures:
 * @self: an #LrgTiledMap
 * @error: (nullable): return location for an error
 *
 * Loads every tileset's image as a texture (needs a GL context).
 * Tilemaps built earlier share the same #LrgTileset objects, so they
 * draw with the textures from now on.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tiled_map_load_textures (LrgTiledMap  *self,
                                      GError      **error);

G_END_DECLS
