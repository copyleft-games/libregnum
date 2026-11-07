/* lrg-editor-nav-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNavPanel: the lrgeditor "Navigation" panel.
 *
 * Edits a 3D level's navigation: the level's `navigation:` block (which
 * .rnav settings file, which baked .rnavmesh, bake on load) and the
 * #LrgNavBake settings themselves (agent, voxels, regions, geometry
 * sources and filters, area types with their boxes, off-mesh links).
 * Bake runs lrg_nav_bake_bake() on a private instance of the level (on a
 * worker thread from the UI, cancellable), Save writes the .rnav and the
 * baked mesh next to the level and points the level at them.  Every edit
 * is one undoable level command.  While the game runs in the editor the
 * panel lists the agents of the play world's #LrgNavCrowd (state, path,
 * remaining distance) and picks the crowd's debug agent.
 *
 * The viewport navigation overlay (polygons by region or area, boundaries,
 * off-mesh links, the settings' area boxes and links, agents, paths and
 * ORCA planes while playing) is drawn by the panel from the scene
 * renderer's 3D overlay hook; the area-box and link-endpoint handles are
 * painted and dragged through lrg_editor_nav_panel_paint_handles() and
 * the lrg_editor_nav_panel_handle_*() calls the viewport makes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "../../ui/lrg-theme.h"
#include "../../paint/lrg-paint-list.h"
#include "../../pathfinding/lrg-nav-bake.h"
#include "../../pathfinding/lrg-nav-poly-mesh.h"
#include "../../pathfinding/lrg-nav-agent.h"
#include "../../debug/lrg-debug-draw.h"
#include "../lrg-editor-math.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_NAV_PANEL_ID:
 *
 * The dock panel id of the navigation panel ("navigation").
 */
#define LRG_EDITOR_NAV_PANEL_ID "navigation"

/**
 * LrgEditorNavHandle:
 * @LRG_EDITOR_NAV_HANDLE_NONE: no handle
 * @LRG_EDITOR_NAV_HANDLE_BOX_MIN: the selected area box's minimum X/Z
 *   corner
 * @LRG_EDITOR_NAV_HANDLE_BOX_MAX: the selected area box's maximum X/Z
 *   corner
 * @LRG_EDITOR_NAV_HANDLE_BOX_CENTER: the selected area box's centre
 *   (moves the whole box)
 * @LRG_EDITOR_NAV_HANDLE_LINK_START: the selected off-mesh link's start
 * @LRG_EDITOR_NAV_HANDLE_LINK_END: the selected off-mesh link's end
 *
 * The viewport handles of the navigation panel's selection.  They drag
 * on the horizontal plane through the handle.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_EDITOR_NAV_HANDLE_NONE,
	LRG_EDITOR_NAV_HANDLE_BOX_MIN,
	LRG_EDITOR_NAV_HANDLE_BOX_MAX,
	LRG_EDITOR_NAV_HANDLE_BOX_CENTER,
	LRG_EDITOR_NAV_HANDLE_LINK_START,
	LRG_EDITOR_NAV_HANDLE_LINK_END
} LrgEditorNavHandle;

/**
 * lrg_editor_nav_handle_get_type:
 *
 * Returns: the #GType of #LrgEditorNavHandle
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_nav_handle_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_NAV_HANDLE (lrg_editor_nav_handle_get_type ())

#define LRG_TYPE_EDITOR_NAV_PANEL (lrg_editor_nav_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNavPanel, lrg_editor_nav_panel, LRG, EDITOR_NAV_PANEL, LrgContainer)

/**
 * lrg_editor_nav_panel_new:
 * @app: the editor application
 *
 * Creates the panel.  It follows the app's level, registers the nav.* IPC
 * methods on the app's server and the "nav.*" actions (nav.show,
 * nav.bake, nav.cancel, nav.save, nav.clear, nav.new-settings,
 * nav.open-settings and the checkable nav.overlay-* toggles of the
 * viewport overlays menu), and hooks the scene renderer's 3D overlay
 * pass.  The window docks it with the id %LRG_EDITOR_NAV_PANEL_ID;
 * embedders may dock it themselves with lrg_editor_app_add_panel().
 *
 * Returns: (transfer full): a new #LrgEditorNavPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavPanel * lrg_editor_nav_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_nav_panel_find:
 * @app: the editor application
 *
 * The panel created for @app (one per app).
 *
 * Returns: (transfer none) (nullable): the panel, or %NULL when none was
 *   created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavPanel * lrg_editor_nav_panel_find (LrgEditorApp *app);

/* ==========================================================================
 * Settings (.rnav) and the level's navigation: block
 * ========================================================================== */

/**
 * lrg_editor_nav_panel_get_settings:
 * @self: an #LrgEditorNavPanel
 *
 * The bake settings the panel edits: the level's `.rnav` as loaded, plus
 * every edit since (in memory until lrg_editor_nav_panel_save() or the
 * level is saved).  Read it; change it through the panel's functions so
 * the edits are undoable.  Undo replaces the object, so do not keep it.
 *
 * Returns: (transfer none): the settings
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_editor_nav_panel_get_settings (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_settings_path:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: (transfer none) (nullable): the absolute path of the level's
 *   `navigation: settings:` file, %NULL when the level references none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_nav_panel_get_settings_path (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_dirty:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: %TRUE when the settings differ from the `.rnav` file on disk
 *   (or from the defaults when the level references no file)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_get_dirty (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_grid:
 * @self: an #LrgEditorNavPanel
 *
 * The settings form.  Its rows, by group: Level ("data",
 * "bake-on-load"), Agent ("agent-radius", "agent-height", "max-slope",
 * "max-step", "max-drop"), Voxels ("cell-size", "cell-height"), Regions
 * ("min-region-area", "corner-margin"), Sources ("include-meshes",
 * "include-colliders", "static-only", "layer-mask", "include-tags",
 * "exclude-tags", "physics-layers", "use-bounds", "bounds-min",
 * "bounds-max") and Output ("name", "output").
 *
 * Returns: (transfer none): the grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_nav_panel_get_grid (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_set_field:
 * @self: an #LrgEditorNavPanel
 * @key: a form row name ('_' works for '-'), or "settings" (the path of
 *   an existing `.rnav` to load as the level's settings, %NULL value or
 *   "" to drop the reference)
 * @value: the value: numbers for the numeric rows, booleans, strings,
 *   string arrays or comma separated strings for the tag lists, a
 *   #GrlVector3 or "x, y, z" for the bounds
 * @error: return location for a #GError
 *
 * Sets one field of the settings or of the level's `navigation:` block
 * as one undoable command.  Numbers are clamped to the field's range.
 *
 * Returns: %TRUE when the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_set_field (LrgEditorNavPanel  *self,
                                         const gchar        *key,
                                         const GValue       *value,
                                         GError            **error);

/**
 * lrg_editor_nav_panel_new_settings:
 * @self: an #LrgEditorNavPanel
 * @path: (nullable): where to write the new `.rnav` (absolute, or
 *   relative to the level's directory); %NULL for `<level-stem>.rnav`
 *   next to the level
 * @error: return location for a #GError
 *
 * Writes default settings to @path and makes it the level's
 * `navigation: settings:` (the reference change is undoable, the file
 * stays).  The level must have been saved once unless @path is absolute.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_new_settings (LrgEditorNavPanel  *self,
                                            const gchar        *path,
                                            GError            **error);

/**
 * lrg_editor_nav_panel_open_settings:
 * @self: an #LrgEditorNavPanel
 * @path: an existing `.rnav` file (absolute, or relative to the level's
 *   directory)
 * @error: return location for a #GError
 *
 * Loads @path as the level's navigation settings (one undoable command:
 * the reference and the settings change together).  This is what the
 * data editor calls for a `.rnav` document.
 *
 * Returns: %TRUE when the file loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_open_settings (LrgEditorNavPanel  *self,
                                             const gchar        *path,
                                             GError            **error);

/* ==========================================================================
 * Areas and off-mesh links
 * ========================================================================== */

/**
 * lrg_editor_nav_panel_add_area:
 * @self: an #LrgEditorNavPanel
 * @name: a unique area name
 * @tag: its nonzero tag, 0 for the lowest unused one
 * @cost: its traversal cost multiplier (> 0)
 * @error: return location for a #GError
 *
 * Declares an area type (undoable) and selects it.
 *
 * Returns: the new area's index, -1 on failure (name or tag taken, bad
 *   cost)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_nav_panel_add_area (LrgEditorNavPanel  *self,
                                    const gchar        *name,
                                    guint32             tag,
                                    gfloat              cost,
                                    GError            **error);

/**
 * lrg_editor_nav_panel_remove_area:
 * @self: an #LrgEditorNavPanel
 * @area: an area index
 *
 * Removes an area and its boxes (undoable); links naming it lose their
 * area.
 *
 * Returns: %TRUE when the area existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_remove_area (LrgEditorNavPanel *self,
                                           guint              area);

/**
 * lrg_editor_nav_panel_set_area:
 * @self: an #LrgEditorNavPanel
 * @area: an area index
 * @name: (nullable): a new unique name, %NULL to keep it (links naming
 *   the area follow a rename)
 * @tag: a new unique nonzero tag, 0 to keep it
 * @cost: a new cost (> 0), 0 or less to keep it
 * @error: return location for a #GError
 *
 * Changes an area type as one undoable command.
 *
 * Returns: %TRUE when the change was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_set_area (LrgEditorNavPanel  *self,
                                        guint               area,
                                        const gchar        *name,
                                        guint32             tag,
                                        gfloat              cost,
                                        GError            **error);

/**
 * lrg_editor_nav_panel_add_area_box:
 * @self: an #LrgEditorNavPanel
 * @area: an area index
 * @min: (array fixed-size=3): the box's lowest corner
 * @max: (array fixed-size=3): its highest corner
 * @error: return location for a #GError
 *
 * Adds a box marking the floor inside it as @area (undoable) and selects
 * it, so its viewport handles appear.
 *
 * Returns: the box's index within the area, -1 on failure
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_nav_panel_add_area_box (LrgEditorNavPanel  *self,
                                        guint               area,
                                        const gfloat       *min,
                                        const gfloat       *max,
                                        GError            **error);

/**
 * lrg_editor_nav_panel_remove_area_box:
 * @self: an #LrgEditorNavPanel
 * @area: an area index
 * @box: a box index of the area
 *
 * Removes one box (undoable).
 *
 * Returns: %TRUE when the box existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_remove_area_box (LrgEditorNavPanel *self,
                                               guint              area,
                                               guint              box);

/**
 * lrg_editor_nav_panel_set_area_box:
 * @self: an #LrgEditorNavPanel
 * @area: an area index
 * @box: a box index of the area
 * @min: (array fixed-size=3): the new lowest corner
 * @max: (array fixed-size=3): the new highest corner
 * @error: return location for a #GError
 *
 * Moves or resizes one box (undoable); the corners are sorted per axis.
 *
 * Returns: %TRUE when the change was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_set_area_box (LrgEditorNavPanel  *self,
                                            guint               area,
                                            guint               box,
                                            const gfloat       *min,
                                            const gfloat       *max,
                                            GError            **error);

/**
 * lrg_editor_nav_panel_add_link:
 * @self: an #LrgEditorNavPanel
 * @start: (array fixed-size=3): where agents enter it
 * @end: (array fixed-size=3): where it leads
 * @bidirectional: whether it works from @end to @start too
 * @cost: extra cost on top of its length (>= 0)
 * @area: (nullable): the name of an area whose tag the link carries
 * @error: return location for a #GError
 *
 * Adds an off-mesh link (undoable) and selects it.
 *
 * Returns: the link's index, -1 on failure
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_nav_panel_add_link (LrgEditorNavPanel  *self,
                                    const gfloat       *start,
                                    const gfloat       *end,
                                    gboolean            bidirectional,
                                    gfloat              cost,
                                    const gchar        *area,
                                    GError            **error);

/**
 * lrg_editor_nav_panel_remove_link:
 * @self: an #LrgEditorNavPanel
 * @index: a link index (later links move down one)
 *
 * Removes one link (undoable).
 *
 * Returns: %TRUE when the link existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_remove_link (LrgEditorNavPanel *self,
                                           guint              index);

/**
 * lrg_editor_nav_panel_set_link:
 * @self: an #LrgEditorNavPanel
 * @index: a link index
 * @start: (array fixed-size=3): its start
 * @end: (array fixed-size=3): its end
 * @bidirectional: whether it works both ways
 * @cost: its extra cost (>= 0)
 * @area: (nullable): an area name, %NULL for none
 * @error: return location for a #GError
 *
 * Replaces one link (undoable); it keeps its index.
 *
 * Returns: %TRUE when the change was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_set_link (LrgEditorNavPanel  *self,
                                        guint               index,
                                        const gfloat       *start,
                                        const gfloat       *end,
                                        gboolean            bidirectional,
                                        gfloat              cost,
                                        const gchar        *area,
                                        GError            **error);

/**
 * lrg_editor_nav_panel_select:
 * @self: an #LrgEditorNavPanel
 * @area: an area index, -1 for none
 * @box: a box index of @area, -1 for none
 * @link: a link index, -1 for none
 *
 * Selects what the panel's area / box / link forms edit and what the
 * viewport handles drag.  Out of range values select nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_nav_panel_select (LrgEditorNavPanel *self,
                                  gint               area,
                                  gint               box,
                                  gint               link);

/**
 * lrg_editor_nav_panel_get_selection:
 * @self: an #LrgEditorNavPanel
 * @area: (out) (optional): the selected area, -1 for none
 * @box: (out) (optional): the selected box of that area, -1 for none
 * @link: (out) (optional): the selected link, -1 for none
 *
 * The panel's selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_nav_panel_get_selection (LrgEditorNavPanel *self,
                                         gint              *area,
                                         gint              *box,
                                         gint              *link);

/* ==========================================================================
 * Bake, save, clear
 * ========================================================================== */

/**
 * lrg_editor_nav_panel_bake:
 * @self: an #LrgEditorNavPanel
 * @error: return location for a #GError
 *
 * Bakes now, on the calling thread: the level is instantiated into a
 * private #LrgWorld3D (mesh assets resolved against the level's
 * directory) and lrg_nav_bake_bake() runs on a copy of the settings.
 * The result becomes the panel's mesh (the viewport shows it at once);
 * with auto save and a saved level it is saved right away
 * (lrg_editor_nav_panel_save()).
 *
 * Returns: %TRUE on success (%LRG_NAV_BAKE_ERROR_NO_GEOMETRY and the
 *   voxel bake's errors are passed on)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_bake (LrgEditorNavPanel  *self,
                                    GError            **error);

/**
 * lrg_editor_nav_panel_bake_async:
 * @self: an #LrgEditorNavPanel
 * @error: return location for a #GError
 *
 * Starts a bake on a worker thread (what the Bake button does); the
 * level is instantiated on the calling thread first.  The result arrives
 * on the main context; lrg_editor_nav_panel_cancel() drops it.
 *
 * Returns: %TRUE when the bake started (%G_IO_ERROR_BUSY while one runs)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_bake_async (LrgEditorNavPanel  *self,
                                          GError            **error);

/**
 * lrg_editor_nav_panel_cancel:
 * @self: an #LrgEditorNavPanel
 *
 * Cancels the running asynchronous bake: the worker finishes in the
 * background and its result is dropped.
 *
 * Returns: %TRUE when a bake was running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_cancel (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_baking:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: %TRUE while an asynchronous bake runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_get_baking (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_mesh:
 * @self: an #LrgEditorNavPanel
 *
 * The mesh the panel shows while authoring: the last bake, else the
 * level's `navigation: data:` file.
 *
 * Returns: (transfer none) (nullable): the mesh
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh * lrg_editor_nav_panel_get_mesh (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_last_triangle_count:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: the triangles the last bake of this session staged
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_nav_panel_get_last_triangle_count (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_last_bake_seconds:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: how long the last bake of this session took (the voxel bake
 *   and the level instantiation), 0 without one
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_nav_panel_get_last_bake_seconds (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_error:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: (transfer none) (nullable): why the last bake failed
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_nav_panel_get_error (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_has_unsaved:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: %TRUE when a baked mesh was not written to disk yet
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_has_unsaved (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_set_auto_save:
 * @self: an #LrgEditorNavPanel
 * @auto_save: save every successful bake of a saved level at once
 *
 * Default %TRUE (like the lighting bake).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_nav_panel_set_auto_save (LrgEditorNavPanel *self,
                                         gboolean           auto_save);

/**
 * lrg_editor_nav_panel_get_auto_save:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: whether successful bakes are saved at once
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_get_auto_save (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_save:
 * @self: an #LrgEditorNavPanel
 * @error: return location for a #GError
 *
 * Writes the settings to the level's `.rnav` (creating `<level-stem>.rnav`
 * next to the level when it references none) and the baked mesh, when
 * there is one, to the settings' `output` (relative to the `.rnav`), else
 * `<level-stem>.rnavmesh` next to the level; then points the level's
 * `navigation:` block at both as one undoable command.  The level itself
 * still needs saving for the references.
 *
 * Returns: %TRUE on success (the level must have a file)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_save (LrgEditorNavPanel  *self,
                                    GError            **error);

/**
 * lrg_editor_nav_panel_clear:
 * @self: an #LrgEditorNavPanel
 *
 * Drops the baked mesh and the level's `navigation: data:` reference
 * (undoable; the file stays).
 *
 * Returns: %TRUE when there was something to clear
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_clear (LrgEditorNavPanel *self);

/* ==========================================================================
 * The viewport overlay
 * ========================================================================== */

/**
 * lrg_editor_nav_panel_set_overlay:
 * @self: an #LrgEditorNavPanel
 * @flags: what the viewport navigation overlay draws, %LRG_NAV_DEBUG_NONE
 *   to hide it
 *
 * The navigation overlay of every viewport (a view setting, not saved,
 * not undoable).  While authoring it draws the panel's mesh with the
 * mesh flags plus the settings' area boxes and off-mesh links; while
 * playing it draws the play world's mesh and crowd
 * (lrg_world3d_debug_draw_navigation()) with every flag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_nav_panel_set_overlay (LrgEditorNavPanel *self,
                                       LrgNavDebugFlags   flags);

/**
 * lrg_editor_nav_panel_get_overlay:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: the overlay flags, %LRG_NAV_DEBUG_NONE when hidden
 */
LRG_AVAILABLE_IN_ALL
LrgNavDebugFlags lrg_editor_nav_panel_get_overlay (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_set_show_handles:
 * @self: an #LrgEditorNavPanel
 * @show: whether the viewport shows (and lets you drag) the handles of
 *   the selected area box and link
 *
 * Default %TRUE.  The handles are never shown while playing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_nav_panel_set_show_handles (LrgEditorNavPanel *self,
                                            gboolean           show);

/**
 * lrg_editor_nav_panel_get_show_handles:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: whether the handles are shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_get_show_handles (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_record_overlay:
 * @self: an #LrgEditorNavPanel
 * @draw: the batch to record into (cleared first)
 *
 * Records what the overlay draws this frame (headless too): what the 3D
 * pass of every viewport draws from the scene renderer's overlay hook.
 *
 * Returns: the number of primitives recorded
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_nav_panel_record_overlay (LrgEditorNavPanel *self,
                                           LrgDebugDraw      *draw);

/**
 * lrg_editor_nav_panel_paint_handles:
 * @self: an #LrgEditorNavPanel
 * @list: the viewport overlay's paint list
 * @theme: the theme
 * @view: the viewport's view
 * @ox: canvas x of the view's origin
 * @oy: canvas y of the view's origin
 * @scale: canvas pixels per view pixel
 *
 * Paints the selected area box (outline) and link (arrow) with their
 * drag handles; the viewport calls it from its overlay paint.
 *
 * Returns: the number of handles painted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_nav_panel_paint_handles (LrgEditorNavPanel   *self,
                                          LrgPaintList        *list,
                                          LrgTheme            *theme,
                                          const LrgEditorView *view,
                                          gfloat               ox,
                                          gfloat               oy,
                                          gfloat               scale);

/**
 * lrg_editor_nav_panel_hit_test:
 * @self: an #LrgEditorNavPanel
 * @view: the viewport's view
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 *
 * Returns: the handle under (@u, @v), %LRG_EDITOR_NAV_HANDLE_NONE for
 *   none (or while handles are hidden)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavHandle lrg_editor_nav_panel_hit_test (LrgEditorNavPanel   *self,
                                                  const LrgEditorView *view,
                                                  gfloat               u,
                                                  gfloat               v);

/**
 * lrg_editor_nav_panel_get_handle_position:
 * @self: an #LrgEditorNavPanel
 * @handle: a handle
 * @out: (out caller-allocates) (array fixed-size=3): its world position
 *
 * Returns: %FALSE when @handle does not exist (nothing selected)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_get_handle_position (LrgEditorNavPanel  *self,
                                                   LrgEditorNavHandle  handle,
                                                   gfloat             *out);

/**
 * lrg_editor_nav_panel_handle_press:
 * @self: an #LrgEditorNavPanel
 * @view: the viewport's view
 * @button: the #GrlMouseButton pressed
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 *
 * A viewport button press.  A left press on a handle starts its drag;
 * any other button during a drag cancels it (the value goes back).
 *
 * Returns: %TRUE when the press was the panel's (the viewport stops)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_handle_press (LrgEditorNavPanel   *self,
                                            const LrgEditorView *view,
                                            gint                 button,
                                            gfloat               u,
                                            gfloat               v);

/**
 * lrg_editor_nav_panel_handle_motion:
 * @self: an #LrgEditorNavPanel
 * @view: the viewport's view
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 * @snap: snap to 0.25 m (Ctrl)
 * @repaint: (out) (optional): set when the hovered handle changed
 *
 * A viewport pointer motion: drags the grabbed handle live, or tracks
 * the hovered one.
 *
 * Returns: %TRUE while a handle is being dragged (the viewport stops)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_handle_motion (LrgEditorNavPanel   *self,
                                             const LrgEditorView *view,
                                             gfloat               u,
                                             gfloat               v,
                                             gboolean             snap,
                                             gboolean            *repaint);

/**
 * lrg_editor_nav_panel_handle_release:
 * @self: an #LrgEditorNavPanel
 * @view: the viewport's view
 * @u: viewport x in pixels
 * @v: viewport y in pixels
 * @snap: snap to 0.25 m (Ctrl)
 *
 * Ends a handle drag: the whole drag is one undoable command.
 *
 * Returns: %TRUE when a drag ended (the viewport stops)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_handle_release (LrgEditorNavPanel   *self,
                                              const LrgEditorView *view,
                                              gfloat               u,
                                              gfloat               v,
                                              gboolean             snap);

/* ==========================================================================
 * Play mode
 * ========================================================================== */

/**
 * lrg_editor_nav_panel_get_crowd:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: (transfer none) (nullable): the play world's crowd while the
 *   game runs in the editor, else %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNavCrowd * lrg_editor_nav_panel_get_crowd (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_get_debug_agent:
 * @self: an #LrgEditorNavPanel
 *
 * Returns: the crowd index of the debug agent (its ORCA planes and
 *   neighbours are drawn), -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_nav_panel_get_debug_agent (LrgEditorNavPanel *self);

/**
 * lrg_editor_nav_panel_set_debug_agent:
 * @self: an #LrgEditorNavPanel
 * @index: a crowd index, -1 to clear
 *
 * Sets the play crowd's debug agent (lrg_nav_crowd_set_debug_agent()),
 * what selecting a row of the agent list does.
 *
 * Returns: %TRUE when the agent exists (or @index is -1) while playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_nav_panel_set_debug_agent (LrgEditorNavPanel *self,
                                               gint               index);

G_END_DECLS
