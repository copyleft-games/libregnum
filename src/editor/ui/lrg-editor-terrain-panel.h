/* lrg-editor-terrain-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTerrainPanel: the lrgeditor "Terrain" panel, the terrain and
 * water viewport tools, and the editor's shared terrain / water store.
 *
 * The panel edits the #LrgTerrainAsset (`.rterrain`) of the selected
 * `terrain` node: a tool strip (select, raise, lower, flatten, smooth,
 * noise, paint, hole, fill, foliage paint, foliage erase), the brush
 * settings (radius, strength, falloff and its curve, flatten target with
 * "sample", noise scale and seed), the surface layer list (add, remove;
 * name, texture, material, tiling, colour), the foliage layer list with
 * their scatter rules, the chunk grid summary, Rebake and Save.
 *
 * With a brush tool picked, the viewport projects a cursor ring onto the
 * terrain (lrg_terrain_component_raycast()), a left drag paints a stroke
 * (one undo step per stroke, from the brush's #LrgTerrainEdit), Ctrl
 * inverts the tool (raise <-> lower, hole <-> fill, foliage paint <->
 * erase), Shift smooths, and `[` / `]` shrink and grow the radius. With a
 * `water` lake or river node selected the viewport shows its outline
 * points as handles: drag moves a point, Ctrl+click on the water adds one,
 * Shift+click on a handle removes it (each one undo step).
 *
 * The store: every `.rterrain` and `.rwater` the editor shows is loaded
 * once per editor (lrg_editor_terrain_store_get_terrain(),
 * lrg_editor_terrain_store_get_water()) and shared by the scene renderer,
 * this panel, the viewport tools and the water editor, so a brush stroke
 * is seen everywhere at once. Edited files are marked dirty and written
 * when the level is saved (or with Save / terrain.save / water.save).
 *
 * The panel registers the terrain.* IPC methods and the terrain.* actions
 * from its own file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-enums.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-theme.h"
#include "../../ui/lrg-property-grid.h"
#include "../../paint/lrg-paint-list.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-terrain-asset.h"
#include "../../graphics/lrg-terrain-brush.h"
#include "../../graphics/lrg-foliage.h"
#include "../../graphics/lrg-water-body.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../lrg-editor.h"
#include "../lrg-editor-math.h"
#include "../lrg-node.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_TERRAIN_PANEL_ID:
 *
 * The dock panel id of the Terrain panel ("terrain").
 */
#define LRG_EDITOR_TERRAIN_PANEL_ID "terrain"

/**
 * LrgEditorTerrainTool:
 * @LRG_EDITOR_TERRAIN_TOOL_SELECT: no brush: the viewport selects and
 *   transforms as usual
 * @LRG_EDITOR_TERRAIN_TOOL_RAISE: raise heights
 * @LRG_EDITOR_TERRAIN_TOOL_LOWER: lower heights
 * @LRG_EDITOR_TERRAIN_TOOL_FLATTEN: pull heights towards the target height
 * @LRG_EDITOR_TERRAIN_TOOL_SMOOTH: average heights with their neighbours
 * @LRG_EDITOR_TERRAIN_TOOL_NOISE: add value noise
 * @LRG_EDITOR_TERRAIN_TOOL_PAINT: paint the selected surface layer
 * @LRG_EDITOR_TERRAIN_TOOL_HOLE: cut holes
 * @LRG_EDITOR_TERRAIN_TOOL_FILL: fill holes
 * @LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_PAINT: raise the selected foliage
 *   layer's density
 * @LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE: lower the selected foliage
 *   layer's density
 *
 * The tools of the Terrain panel's tool strip.  Every tool but SELECT is
 * an #LrgTerrainBrushOp of the same name.
 */
typedef enum
{
	LRG_EDITOR_TERRAIN_TOOL_SELECT,
	LRG_EDITOR_TERRAIN_TOOL_RAISE,
	LRG_EDITOR_TERRAIN_TOOL_LOWER,
	LRG_EDITOR_TERRAIN_TOOL_FLATTEN,
	LRG_EDITOR_TERRAIN_TOOL_SMOOTH,
	LRG_EDITOR_TERRAIN_TOOL_NOISE,
	LRG_EDITOR_TERRAIN_TOOL_PAINT,
	LRG_EDITOR_TERRAIN_TOOL_HOLE,
	LRG_EDITOR_TERRAIN_TOOL_FILL,
	LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_PAINT,
	LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE
} LrgEditorTerrainTool;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_terrain_tool_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_TERRAIN_TOOL (lrg_editor_terrain_tool_get_type ())

/**
 * LrgEditorTerrainStrokeFlags:
 * @LRG_EDITOR_TERRAIN_STROKE_NONE: the tool as picked
 * @LRG_EDITOR_TERRAIN_STROKE_INVERT: the inverse tool (Ctrl): raise <->
 *   lower, hole <-> fill, foliage paint <-> foliage erase; other tools are
 *   unchanged
 * @LRG_EDITOR_TERRAIN_STROKE_SMOOTH: smooth instead of the tool (Shift)
 *
 * Modifiers of one stroke.
 */
typedef enum /*< flags >*/
{
	LRG_EDITOR_TERRAIN_STROKE_NONE   = 0,
	LRG_EDITOR_TERRAIN_STROKE_INVERT = 1 << 0,
	LRG_EDITOR_TERRAIN_STROKE_SMOOTH = 1 << 1
} LrgEditorTerrainStrokeFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_terrain_stroke_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_TERRAIN_STROKE_FLAGS (lrg_editor_terrain_stroke_flags_get_type ())

/**
 * lrg_editor_terrain_tool_get_op:
 * @tool: a tool other than %LRG_EDITOR_TERRAIN_TOOL_SELECT
 *
 * Returns: the brush operation of @tool
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrushOp lrg_editor_terrain_tool_get_op (LrgEditorTerrainTool tool);

/**
 * lrg_editor_terrain_tool_get_label:
 * @tool: a tool
 *
 * Returns: (transfer none): the tool's display name ("Raise", ...)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_terrain_tool_get_label (LrgEditorTerrainTool tool);

/* ==========================================================================
 * The store: shared terrain and water assets
 * ========================================================================== */

/**
 * lrg_editor_terrain_store_get_terrain:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute `.rterrain` path
 * @error: return location for a #GError
 *
 * The terrain asset of @path, loaded on first use and shared by everything
 * in the editor that shows or edits it until the editor is finalized (or
 * lrg_editor_terrain_store_forget() drops it).  A failed load is not
 * cached: the next call tries again.
 *
 * Returns: (transfer none) (nullable): the asset, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_editor_terrain_store_get_terrain (LrgEditor    *editor,
                                                        const gchar  *path,
                                                        GError      **error);

/**
 * lrg_editor_terrain_store_get_water:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute `.rwater` path
 * @error: return location for a #GError
 *
 * Like lrg_editor_terrain_store_get_terrain() for water bodies.
 *
 * Returns: (transfer none) (nullable): the body, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_editor_terrain_store_get_water (LrgEditor    *editor,
                                                   const gchar  *path,
                                                   GError      **error);

/**
 * lrg_editor_terrain_store_set_water:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute `.rwater` path
 * @body: (transfer none): the body to show for @path from now on
 *
 * Makes @body's state the shared body of @path: the first body given is
 * stored, later ones are copied into it, so everything holding the stored
 * body keeps seeing it (the water editor hands its unsaved document here
 * so the level's water nodes follow it).  Water nodes showing @path
 * rebuild their surface on the next draw.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_store_set_water (LrgEditor    *editor,
                                         const gchar  *path,
                                         LrgWaterBody *body);

/**
 * lrg_editor_terrain_store_set_dirty:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute path of a stored asset
 * @dirty: whether it has unsaved changes
 *
 * Marks a stored terrain or water file as changed (or saved).  Dirty
 * files are written when the level is saved.  Water nodes showing @path
 * rebuild their surface.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_store_set_dirty (LrgEditor   *editor,
                                         const gchar *path,
                                         gboolean     dirty);

/**
 * lrg_editor_terrain_store_is_dirty:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute path
 *
 * Returns: whether @path is stored with unsaved changes
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_store_is_dirty (LrgEditor   *editor,
                                            const gchar *path);

/**
 * lrg_editor_terrain_store_save:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute path of a stored asset
 * @error: return location for a #GError
 *
 * Writes the stored asset of @path (a terrain with its sidecars, or a
 * water body) and clears its dirty mark.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_store_save (LrgEditor    *editor,
                                        const gchar  *path,
                                        GError      **error);

/**
 * lrg_editor_terrain_store_save_all:
 * @editor: an #LrgEditor
 * @error: return location for the first #GError
 *
 * Writes every dirty stored asset (also run when the level is saved).
 *
 * Returns: the number of files written; on an error the rest are still
 *   tried and @error holds the first failure
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_terrain_store_save_all (LrgEditor  *editor,
                                         GError    **error);

/**
 * lrg_editor_terrain_store_forget:
 * @editor: an #LrgEditor
 * @path: (type filename): an absolute path
 *
 * Drops @path from the store (unsaved changes are lost); the next use
 * loads it from disk again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_store_forget (LrgEditor   *editor,
                                      const gchar *path);

/**
 * lrg_editor_terrain_node_path:
 * @editor: an #LrgEditor
 * @resolver: (nullable): the renderer whose asset resolver turns the
 *   node's asset string into a file (GUIDs, project-relative paths); %NULL
 *   resolves relative paths against the level's directory only
 * @node: a `terrain` or `water` node
 *
 * Returns: (transfer full) (nullable) (type filename): the absolute file
 *   of @node's asset, %NULL for another kind or no asset
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_terrain_node_path (LrgEditor          *editor,
                                      LrgWorld3DRenderer *resolver,
                                      LrgNode            *node);

/**
 * lrg_editor_terrain_node_component:
 * @editor: an #LrgEditor
 * @resolver: (nullable): see lrg_editor_terrain_node_path()
 * @node: a `terrain` or `water` node
 *
 * The authoring stand-in of @node: an #LrgTerrainComponent or
 * #LrgWaterComponent on a private #LrgEntity3D placed at @node's world
 * transform, showing the stored asset.  The scene renderer draws it with
 * lrg_component_collect_render(), the brush picks with
 * lrg_terrain_component_raycast().  The entity follows @node on every
 * call; the node's `cast_shadows` and `foliage` params are applied.
 *
 * Returns: (transfer none) (nullable): the component, %NULL for another
 *   kind or an asset that does not load
 */
LRG_AVAILABLE_IN_ALL
LrgComponent * lrg_editor_terrain_node_component (LrgEditor          *editor,
                                                  LrgWorld3DRenderer *resolver,
                                                  LrgNode            *node);

/* ==========================================================================
 * The panel
 * ========================================================================== */

#define LRG_TYPE_EDITOR_TERRAIN_PANEL (lrg_editor_terrain_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTerrainPanel, lrg_editor_terrain_panel, LRG, EDITOR_TERRAIN_PANEL, LrgContainer)

/**
 * lrg_editor_terrain_panel_new:
 * @app: the #LrgEditorApp
 *
 * Creates the Terrain panel, registers the terrain.* and water.* IPC
 * methods and the terrain.* actions and remembers itself on @app
 * (lrg_editor_terrain_panel_find()).
 *
 * Returns: (transfer full): a new #LrgEditorTerrainPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTerrainPanel * lrg_editor_terrain_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_terrain_panel_find:
 * @app: an #LrgEditorApp
 *
 * Returns: (transfer none) (nullable): the app's Terrain panel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTerrainPanel * lrg_editor_terrain_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_terrain_panel_get_node:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer none) (nullable): the terrain node shown (the primary
 *   selection when it is a `terrain` node)
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_terrain_panel_get_node (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_asset:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer none) (nullable): the stored asset of the node shown
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_editor_terrain_panel_get_asset (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_brush:
 * @self: an #LrgEditorTerrainPanel
 *
 * The panel's brush: radius, strength, falloff, target height, noise and
 * seed live here (the operation and layer are set from the tool and the
 * layer selection at each stroke).
 *
 * Returns: (transfer none): the brush
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrush * lrg_editor_terrain_panel_get_brush (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_brush_grid:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer none): the brush settings grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_terrain_panel_get_brush_grid (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_layer_grid:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer none): the selected surface layer's grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_terrain_panel_get_layer_grid (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_foliage_grid:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer none): the selected foliage layer's grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_terrain_panel_get_foliage_grid (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_tool:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: the picked tool
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTerrainTool lrg_editor_terrain_panel_get_tool (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_set_tool:
 * @self: an #LrgEditorTerrainPanel
 * @tool: the tool to pick
 *
 * Picks a tool.  Any tool but %LRG_EDITOR_TERRAIN_TOOL_SELECT turns the
 * viewport's left button into the brush while a terrain node is selected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_panel_set_tool (LrgEditorTerrainPanel *self,
                                        LrgEditorTerrainTool   tool);

/**
 * lrg_editor_terrain_panel_get_brush_active:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: whether the viewport brush is live: a brush tool is picked, a
 *   terrain node with a loaded asset is selected, nothing is playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_get_brush_active (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_get_selected_layer:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: the selected surface layer (painted by the paint tool), -1 for
 *   none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_terrain_panel_get_selected_layer (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_set_selected_layer:
 * @self: an #LrgEditorTerrainPanel
 * @index: a surface layer index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_panel_set_selected_layer (LrgEditorTerrainPanel *self,
                                                  gint                   index);

/**
 * lrg_editor_terrain_panel_get_selected_foliage:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: the selected foliage layer (the foliage brushes' layer), -1
 *   for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_terrain_panel_get_selected_foliage (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_set_selected_foliage:
 * @self: an #LrgEditorTerrainPanel
 * @index: a foliage layer index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_panel_set_selected_foliage (LrgEditorTerrainPanel *self,
                                                    gint                   index);

/* --- strokes ------------------------------------------------------------ */

/**
 * lrg_editor_terrain_panel_begin_stroke:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @local_x: asset-local X of the first dab
 * @local_z: asset-local Z of the first dab
 * @flags: Ctrl / Shift modifiers of the stroke
 *
 * Starts a stroke of the picked tool (which must not be SELECT) and dabs
 * once with the amount 1/60.  Further dabs come from
 * lrg_editor_terrain_panel_stroke_to(); lrg_editor_terrain_panel_end_stroke()
 * records the whole stroke as one undo step.
 *
 * Returns: %TRUE when a stroke started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_begin_stroke (LrgEditorTerrainPanel       *self,
                                                LrgNode                     *node,
                                                gfloat                       local_x,
                                                gfloat                       local_z,
                                                LrgEditorTerrainStrokeFlags  flags);

/**
 * lrg_editor_terrain_panel_stroke_to:
 * @self: an #LrgEditorTerrainPanel
 * @local_x: asset-local X
 * @local_z: asset-local Z
 * @amount: the dab amount (the frame time while a brush is held, 1 for a
 *   scripted dab)
 *
 * One more dab of the open stroke.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_panel_stroke_to (LrgEditorTerrainPanel *self,
                                         gfloat                 local_x,
                                         gfloat                 local_z,
                                         gfloat                 amount);

/**
 * lrg_editor_terrain_panel_end_stroke:
 * @self: an #LrgEditorTerrainPanel
 * @commit: %TRUE to record the stroke, %FALSE to put the data back
 *
 * Ends the open stroke.
 *
 * Returns: %TRUE when a change was recorded (an undo step was added)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_end_stroke (LrgEditorTerrainPanel *self,
                                              gboolean               commit);

/**
 * lrg_editor_terrain_panel_is_stroking:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: whether a stroke is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_is_stroking (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_stroke:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @tool: the tool (not SELECT)
 * @points: (array length=n_points): asset-local positions as x, z pairs
 * @n_points: the number of floats in @points (twice the dab count)
 * @amount: the amount of every dab
 * @flags: Ctrl / Shift modifiers
 *
 * A scripted stroke: one dab per point, one undo step (what terrain.stroke
 * runs).
 *
 * Returns: %TRUE when the terrain changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_stroke (LrgEditorTerrainPanel       *self,
                                          LrgNode                     *node,
                                          LrgEditorTerrainTool         tool,
                                          const gfloat                *points,
                                          guint                        n_points,
                                          gfloat                       amount,
                                          LrgEditorTerrainStrokeFlags  flags);

/* --- layers ------------------------------------------------------------- */

/**
 * lrg_editor_terrain_panel_add_layer:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @name: the new layer's name
 *
 * Adds a surface layer (one undo step) and selects it.
 *
 * Returns: the new layer's index, -1 when the asset is full (8 layers) or
 *   there is no terrain
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_terrain_panel_add_layer (LrgEditorTerrainPanel *self,
                                         LrgNode               *node,
                                         const gchar           *name);

/**
 * lrg_editor_terrain_panel_remove_layer:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @index: the layer
 *
 * Removes a surface layer (its weight goes to the others; one undo step
 * that brings the layer and the weights back).
 *
 * Returns: %TRUE when removed (the last layer stays)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_remove_layer (LrgEditorTerrainPanel *self,
                                                LrgNode               *node,
                                                guint                  index);

/**
 * lrg_editor_terrain_panel_set_layer_field:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @index: the layer
 * @name: "name", "texture", "material", "tiling" or "color"
 * @value: the value (a string, a number, or a #GrlColor / string colour)
 *
 * Sets one field of a surface layer (one undo step; values of a grid drag
 * merge into one).
 *
 * Returns: %TRUE when set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_set_layer_field (LrgEditorTerrainPanel *self,
                                                   LrgNode               *node,
                                                   guint                  index,
                                                   const gchar           *name,
                                                   const GValue          *value);

/**
 * lrg_editor_terrain_panel_add_foliage:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @name: the layer name
 * @kind: what it scatters
 *
 * Adds a foliage layer (one undo step) and selects it.
 *
 * Returns: the new layer's index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_terrain_panel_add_foliage (LrgEditorTerrainPanel *self,
                                           LrgNode               *node,
                                           const gchar           *name,
                                           LrgFoliageKind         kind);

/**
 * lrg_editor_terrain_panel_remove_foliage:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @index: the foliage layer
 *
 * Removes a foliage layer (one undo step that brings it back with its
 * density map).
 *
 * Returns: %TRUE when removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_remove_foliage (LrgEditorTerrainPanel *self,
                                                  LrgNode               *node,
                                                  guint                  index);

/**
 * lrg_editor_terrain_panel_set_foliage_field:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @index: the foliage layer
 * @name: "mesh", "lod-mesh", "lod-distance", "texture", "color",
 *   "density", "scale" (min, max), "slope" (degrees min, max), "height"
 *   (min, max), "align", "size" (width, height), "fade" (start, end) or
 *   "seed"
 * @value: the value (pairs are a #GrlVector2)
 *
 * Sets one scatter rule of a foliage layer (one undo step; values of a
 * grid drag merge into one).  The layer re-scatters on the next draw.
 *
 * Returns: %TRUE when set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_set_foliage_field (LrgEditorTerrainPanel *self,
                                                     LrgNode               *node,
                                                     guint                  index,
                                                     const gchar           *name,
                                                     const GValue          *value);

/**
 * lrg_editor_terrain_panel_rebake:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 *
 * Re-scatters every foliage layer and drops the chunk meshes and splat
 * bakes so they are made again on the next draw.  Not an undo step (no
 * data changes).
 *
 * Returns: the foliage instance count after scattering
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_terrain_panel_rebake (LrgEditorTerrainPanel *self,
                                       LrgNode               *node);

/**
 * lrg_editor_terrain_panel_save:
 * @self: an #LrgEditorTerrainPanel
 * @node: (nullable): the terrain node, %NULL for the one shown
 * @error: return location for a #GError
 *
 * Writes the node's `.rterrain` and its sidecars.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_save (LrgEditorTerrainPanel  *self,
                                        LrgNode                *node,
                                        GError                **error);

/**
 * lrg_editor_terrain_panel_describe:
 * @self: an #LrgEditorTerrainPanel
 *
 * Returns: (transfer full): one line about the terrain shown (chunk grid,
 *   cells, samples, size, layers, foliage, holes, dirty), "" without one
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_terrain_panel_describe (LrgEditorTerrainPanel *self);

/* --- the viewport tools (called by LrgEditorViewportPanel) -------------- */

/**
 * lrg_editor_terrain_panel_viewport_press:
 * @self: an #LrgEditorTerrainPanel
 * @view: the pressed viewport's camera
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 * @mods: the modifiers held
 *
 * A left press in a viewport: starts a brush stroke on the terrain under
 * the pointer, or grabs / adds / removes a water outline point.
 *
 * Returns: %TRUE when a tool took the press
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_viewport_press (LrgEditorTerrainPanel *self,
                                                  const LrgEditorView   *view,
                                                  gfloat                 u,
                                                  gfloat                 v,
                                                  LrgInputModifiers      mods);

/**
 * lrg_editor_terrain_panel_viewport_move:
 * @self: an #LrgEditorTerrainPanel
 * @view: the viewport's camera
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 * @mods: the modifiers held
 *
 * Pointer motion: moves the cursor ring (and the open stroke's position)
 * or the dragged water point, and updates the hovered handle.
 *
 * Returns: %TRUE when a tool owns the motion (a stroke or a point drag)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_viewport_move (LrgEditorTerrainPanel *self,
                                                 const LrgEditorView   *view,
                                                 gfloat                 u,
                                                 gfloat                 v,
                                                 LrgInputModifiers      mods);

/**
 * lrg_editor_terrain_panel_viewport_release:
 * @self: an #LrgEditorTerrainPanel
 * @commit: %FALSE to cancel (Escape, another button)
 *
 * Ends a stroke or a water point drag (one undo step when committed).
 *
 * Returns: %TRUE when a tool was active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_viewport_release (LrgEditorTerrainPanel *self,
                                                    gboolean               commit);

/**
 * lrg_editor_terrain_panel_viewport_tick:
 * @self: an #LrgEditorTerrainPanel
 * @delta_time: the frame time
 *
 * While a stroke is held, dabs at the cursor with @delta_time as the
 * amount (a held brush keeps working when the pointer rests).
 *
 * Returns: %TRUE when it dabbed (the viewport should render)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_viewport_tick (LrgEditorTerrainPanel *self,
                                                 gfloat                 delta_time);

/**
 * lrg_editor_terrain_panel_viewport_key:
 * @self: an #LrgEditorTerrainPanel
 * @key: the pressed key
 * @mods: the modifiers
 *
 * Brush keys while the brush is active: `[` / `]` shrink / grow the radius
 * by 10 % (Shift: the strength), Escape cancels an open stroke or drag.
 *
 * Returns: %TRUE when the key was used
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_viewport_key (LrgEditorTerrainPanel *self,
                                                GrlKey                 key,
                                                LrgInputModifiers      mods);

/**
 * lrg_editor_terrain_panel_viewport_leave:
 * @self: an #LrgEditorTerrainPanel
 *
 * The pointer left the viewport: hides the cursor ring.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_terrain_panel_viewport_leave (LrgEditorTerrainPanel *self);

/**
 * lrg_editor_terrain_panel_paint_overlay:
 * @self: an #LrgEditorTerrainPanel
 * @list: the viewport's overlay paint list
 * @theme: the theme
 * @view: the viewport's camera
 * @ox: canvas x of the viewport content
 * @oy: canvas y of the viewport content
 * @scale: canvas pixels per viewport pixel
 *
 * Paints the cursor ring projected on the terrain (outer radius, falloff
 * half-weight ring, centre) and the selected water node's outline and
 * point handles.
 *
 * Returns: the number of shapes painted (rings plus handles)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_terrain_panel_paint_overlay (LrgEditorTerrainPanel *self,
                                              LrgPaintList          *list,
                                              LrgTheme              *theme,
                                              const LrgEditorView   *view,
                                              gfloat                 ox,
                                              gfloat                 oy,
                                              gfloat                 scale);

/**
 * lrg_editor_terrain_panel_get_cursor:
 * @self: an #LrgEditorTerrainPanel
 * @out_local: (out caller-allocates) (array fixed-size=3) (optional): the
 *   cursor in the asset's local space
 * @out_world: (out caller-allocates) (array fixed-size=3) (optional): the
 *   cursor in world space
 *
 * Returns: whether the cursor is on the terrain
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_panel_get_cursor (LrgEditorTerrainPanel *self,
                                              gfloat                *out_local,
                                              gfloat                *out_world);

/* --- creating nodes ------------------------------------------------------ */

/**
 * lrg_editor_terrain_init_node:
 * @app: an #LrgEditorApp
 * @node: a freshly created `terrain` or `water` node (Create menu)
 * @error: return location for a #GError
 *
 * Gives a new node its asset: writes a flat 4 x 4 chunk `.rterrain` (two
 * surface layers) or a 20 x 20 lake `.rwater` under the project's
 * `terrain/` folder, named after the node and unique, and points the node
 * at it.  Terrain nodes sit at (-64, 0, -64) so the 128 x 128 terrain is
 * centred on the origin.  The shell's Create > Terrain / Water entries
 * call this.
 *
 * Returns: %TRUE when the asset was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_terrain_init_node (LrgEditorApp  *app,
                                       LrgNode       *node,
                                       GError       **error);

G_END_DECLS

/*
 * Shared with the water editor (lrg-editor-water-editor.c); not part of
 * the public API.
 */
#ifdef LRG_EDITOR_PANEL_INTERNAL
typedef struct _EditorAssetUndo EditorAssetUndo;
typedef void (*EditorTerrainRestoreFunc) (gpointer data, gboolean after);

G_GNUC_INTERNAL EditorAssetUndo * editor_terrain_push_undo (LrgEditor               *editor,
                                                            const gchar             *label,
                                                            EditorTerrainRestoreFunc func,
                                                            gpointer                 data,
                                                            GDestroyNotify           destroy);
G_GNUC_INTERNAL void              editor_water_copy_into   (LrgWaterBody *dst,
                                                            LrgWaterBody *src);
#endif
