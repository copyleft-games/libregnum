/* lrg-ui-inspector.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUIInspector: Inspects a canvas: widget tree, geometry, state, looks and timings, with pick mode.
 *
 * The inspector attaches to one #LrgCanvas through a weak reference and
 * never adds work to the widget hot path: it reads the tree only when
 * asked (snapshots, lookups, picks) and draws its highlight from one
 * pointer-transparent widget in the canvas overlay layer. Widgets are
 * addressed by stable paths such as "LrgVBox[0]/LrgButton#ok"; the MCP
 * debug tools (lrg_ui_tree, lrg_ui_pick, ...) drive the process-wide
 * inspector returned by lrg_ui_inspector_get_default().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../paint/lrg-paint-list.h"
#include "lrg-canvas-input.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors
 * ========================================================================== */

/**
 * LRG_UI_INSPECTOR_ERROR:
 *
 * Error domain for #LrgUIInspector. Errors in this domain are from the
 * #LrgUIInspectorError enumeration.
 */
#define LRG_UI_INSPECTOR_ERROR (lrg_ui_inspector_error_quark ())

/**
 * LrgUIInspectorError:
 * @LRG_UI_INSPECTOR_ERROR_NO_CANVAS: the inspector is not attached to a canvas
 * @LRG_UI_INSPECTOR_ERROR_NOT_FOUND: no widget matches the path
 * @LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY: the widget has no such
 *   property, or it cannot be read or written
 * @LRG_UI_INSPECTOR_ERROR_INVALID_VALUE: the value does not fit the
 *   property (wrong type, unparsable text or out of range)
 *
 * Errors reported by the inspector's path and property functions.
 */
typedef enum
{
	LRG_UI_INSPECTOR_ERROR_NO_CANVAS,
	LRG_UI_INSPECTOR_ERROR_NOT_FOUND,
	LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY,
	LRG_UI_INSPECTOR_ERROR_INVALID_VALUE
} LrgUIInspectorError;

/**
 * lrg_ui_inspector_error_quark:
 *
 * Returns: the error quark of %LRG_UI_INSPECTOR_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_ui_inspector_error_quark (void);

/**
 * lrg_ui_inspector_error_get_type:
 *
 * Returns: the #GType of #LrgUIInspectorError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_ui_inspector_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UI_INSPECTOR_ERROR (lrg_ui_inspector_error_get_type ())

/* ==========================================================================
 * Timings
 * ========================================================================== */

#define LRG_TYPE_UI_INSPECTOR_TIMINGS (lrg_ui_inspector_timings_get_type ())

/**
 * LrgUIInspectorTimings:
 * @step_us: duration of the last lrg_canvas_update() (springs and
 *   widget animations), in microseconds
 * @layout_us: duration of the layout pass of the last
 *   lrg_canvas_record(), in microseconds
 * @record_us: duration of the paint recording of the last
 *   lrg_canvas_record() (layout excluded), in microseconds
 * @n_instances: paint instances the canvas recorded last frame, without
 *   the inspector's own highlight
 * @n_commands: paint commands (instance batches and callbacks) in the
 *   canvas paint list, highlight included
 * @draw_calls: instanced draw calls the default #LrgPaintRenderer issued
 *   for its last render (0 headless or when the app renders elsewhere)
 * @frames: number of frames recorded since the inspector attached
 *
 * One frame of canvas statistics. A plain value struct.
 */
typedef struct
{
	gint64  step_us;
	gint64  layout_us;
	gint64  record_us;
	guint   n_instances;
	guint   n_commands;
	guint   draw_calls;
	guint64 frames;
} LrgUIInspectorTimings;

/**
 * lrg_ui_inspector_timings_get_type:
 *
 * Returns: the boxed #GType of #LrgUIInspectorTimings
 */
LRG_AVAILABLE_IN_ALL
GType lrg_ui_inspector_timings_get_type (void) G_GNUC_CONST;

/**
 * lrg_ui_inspector_timings_copy:
 * @self: an #LrgUIInspectorTimings
 *
 * Returns: (transfer full): a heap copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgUIInspectorTimings * lrg_ui_inspector_timings_copy (const LrgUIInspectorTimings *self);

/**
 * lrg_ui_inspector_timings_free:
 * @self: (nullable): a heap-allocated #LrgUIInspectorTimings
 *
 * Frees a copy made with lrg_ui_inspector_timings_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_timings_free (LrgUIInspectorTimings *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgUIInspectorTimings, lrg_ui_inspector_timings_free)

/* ==========================================================================
 * LrgUIInspector
 * ========================================================================== */

#define LRG_TYPE_UI_INSPECTOR (lrg_ui_inspector_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUIInspector, lrg_ui_inspector, LRG, UI_INSPECTOR, GObject)

/**
 * lrg_ui_inspector_new:
 *
 * Creates a detached inspector. Most applications use the shared one
 * from lrg_ui_inspector_get_default() instead, because that is the
 * one the MCP tools talk to.
 *
 * Returns: (transfer full): a new #LrgUIInspector
 */
LRG_AVAILABLE_IN_ALL
LrgUIInspector * lrg_ui_inspector_new (void);

/**
 * lrg_ui_inspector_get_default:
 *
 * The process-wide inspector. The MCP UI tools (lrg_ui_tree,
 * lrg_ui_pick, ...) use it; attach the application's canvas to it with
 * lrg_ui_inspector_register_canvas().
 *
 * Returns: (transfer none): the default #LrgUIInspector
 */
LRG_AVAILABLE_IN_ALL
LrgUIInspector * lrg_ui_inspector_get_default (void);

/**
 * lrg_ui_inspector_register_canvas:
 * @canvas: (nullable): the application's main canvas, or %NULL to detach
 *
 * Attaches @canvas to the default inspector, which is how an MCP server
 * (or any other tool) finds the app's UI. Same as
 * lrg_ui_inspector_set_canvas() on lrg_ui_inspector_get_default().
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_register_canvas (LrgCanvas *canvas);

/**
 * lrg_ui_inspector_get_canvas:
 * @self: an #LrgUIInspector
 *
 * Returns: (transfer none) (nullable): the inspected canvas, or %NULL
 *   when detached or after the canvas was finalized
 */
LRG_AVAILABLE_IN_ALL
LrgCanvas * lrg_ui_inspector_get_canvas (LrgUIInspector *self);

/**
 * lrg_ui_inspector_set_canvas:
 * @self: an #LrgUIInspector
 * @canvas: (nullable): canvas to inspect, or %NULL to detach
 *
 * Attaches the inspector to @canvas. The inspector keeps only a weak
 * reference: when the canvas is finalized the inspector detaches
 * itself. Attaching adds one pointer-transparent widget to the canvas
 * overlay layer (it draws the highlight and is invisible to hit-testing,
 * snapshots and paths) and switches on the canvas phase timings.
 * Detaching removes both and clears the selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_set_canvas (LrgUIInspector *self,
                                  LrgCanvas      *canvas);

/* --------------------------------------------------------------------------
 * Paths and snapshots
 * -------------------------------------------------------------------------- */

/**
 * lrg_ui_inspector_get_path:
 * @self: an #LrgUIInspector
 * @widget: a widget in the inspected canvas
 *
 * Builds the stable path of @widget. Segments are separated by "/" and
 * start below the canvas; each is the widget's type name followed by
 * "#name" for named widgets and "[n]" for the n-th sibling of the same
 * type (and name), e.g. "LrgVBox[0]/LrgButton#ok". Widgets in a popup
 * start with ":popup[i]" and widgets in the overlay layer with
 * ":overlay[i]" (i counts from the oldest / bottom one). The canvas
 * itself is the empty path "".
 *
 * Returns: (transfer full) (nullable): the path, or %NULL if @widget is
 *   not in the inspected canvas
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_ui_inspector_get_path (LrgUIInspector *self,
                                   LrgWidget      *widget);

/**
 * lrg_ui_inspector_lookup:
 * @self: an #LrgUIInspector
 * @path: a path as built by lrg_ui_inspector_get_path()
 *
 * Resolves a path. A segment may omit the type ("#ok" matches any type
 * named "ok") and the index ("[0]" is the default). "" and "/" are the
 * canvas.
 *
 * Returns: (transfer none) (nullable): the widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_inspector_lookup (LrgUIInspector *self,
                                     const gchar    *path);

/**
 * lrg_ui_inspector_snapshot_json:
 * @self: an #LrgUIInspector
 * @pretty: indent the output
 *
 * Serialises the whole tree: an object with "canvas" (size), "root"
 * (the canvas node with nested "children"), "popups" and "overlays"
 * (arrays of nodes), "selected" and "hovered" (paths or null). Each node
 * has "path", "type", "name", "classes", "state", "rect" (world x, y,
 * width, height), "margin", "halign", "valign", "fill_weight",
 * "visible", "enabled", "focusable", "pointer_transparent", "opacity",
 * "look" (the resolved %LRG_UI_PART_BOX look), "n_children" and
 * "children". The inspector's own overlay widget is left out.
 *
 * Returns: (transfer full): the JSON text; "{}" with a "error" member
 *   when detached
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_ui_inspector_snapshot_json (LrgUIInspector *self,
                                        gboolean        pretty);

/**
 * lrg_ui_inspector_snapshot:
 * @self: an #LrgUIInspector
 *
 * The tree as a flat, depth-first node list for C callers: a #GVariant
 * of type "aa{sv}" (canvas first, then its subtree, then popups and
 * overlays). Each dictionary has "path" (s), "parent" (s, "" for layer
 * roots), "depth" (u), "layer" (s: "canvas", "popup" or "overlay"),
 * "type" (s), "name" (s, "" when unnamed), "classes" (as), "state" (u,
 * #LrgWidgetState bits), "x", "y", "width", "height" (d, world rect),
 * "margin" ((dddd) left, top, right, bottom), "halign" and "valign"
 * (s, nicks), "fill-weight" (d), "visible", "enabled", "focusable",
 * "pointer-transparent" (b), "opacity" (d), "n-children" (u) and "look"
 * (a{sv}: "background", "foreground", "border" as (yyyy) RGBA,
 * "border-width", "opacity", "shadow-blur", "size" (d), "radii" ((dddd))).
 *
 * Returns: (transfer full): a floating-free "aa{sv}" variant (empty
 *   when detached)
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_ui_inspector_snapshot (LrgUIInspector *self);

/**
 * lrg_ui_inspector_describe_widget:
 * @self: an #LrgUIInspector
 * @widget: a widget in the inspected canvas
 * @pretty: indent the output
 *
 * One node as JSON (the fields of a snapshot node), with "children" as
 * an array of child paths instead of nested nodes, plus "properties":
 * every readable GObject property of @widget by name. Enums are nicks,
 * flags arrays of nicks, colours "#rrggbbaa" and objects their type.
 *
 * Returns: (transfer full) (nullable): the JSON text, or %NULL if
 *   @widget is not in the inspected canvas
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_ui_inspector_describe_widget (LrgUIInspector *self,
                                          LrgWidget      *widget,
                                          gboolean        pretty);

/* --------------------------------------------------------------------------
 * Picking and selection
 * -------------------------------------------------------------------------- */

/**
 * lrg_ui_inspector_pick:
 * @self: an #LrgUIInspector
 * @x: X in canvas coordinates
 * @y: Y in canvas coordinates
 *
 * The topmost widget under (@x, @y), exactly as the canvas hit-tests a
 * click: the overlay layer first, then popups newest first (a modal
 * popup's veil hides everything below it), then the tree; hidden,
 * disabled and pointer-transparent widgets are skipped.
 *
 * Returns: (transfer none) (nullable): the widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_inspector_pick (LrgUIInspector *self,
                                   gfloat          x,
                                   gfloat          y);

/**
 * lrg_ui_inspector_get_pick_mode:
 * @self: an #LrgUIInspector
 *
 * Returns: %TRUE while pick mode is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_get_pick_mode (LrgUIInspector *self);

/**
 * lrg_ui_inspector_set_pick_mode:
 * @self: an #LrgUIInspector
 * @pick_mode: whether hovering highlights and clicking selects
 *
 * In pick mode lrg_ui_inspector_filter_input() highlights the widget
 * under the pointer and turns a left click (or the first touch) into a
 * selection, which emits #LrgUIInspector::picked. The click, the wheel
 * and touches never reach the application. Escape leaves pick mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_set_pick_mode (LrgUIInspector *self,
                                     gboolean        pick_mode);

/**
 * lrg_ui_inspector_filter_input:
 * @self: an #LrgUIInspector
 * @input: (inout): one input frame, about to go to lrg_canvas_process_input()
 *
 * The pick-mode input hook. Call it on every frame before handing the
 * frame to the canvas (or use lrg_ui_inspector_handle_input()). Outside
 * pick mode it only tracks which buttons the application holds. In pick
 * mode it updates the hovered widget, selects on a left press or touch,
 * then strips presses, wheel and touches from @input, and releases of
 * buttons the application did not see go down, so the application
 * sees no click. Escape is consumed and leaves pick mode. The pointer
 * position is left alone, so hover looks keep working.
 *
 * Returns: %TRUE if the inspector consumed pointer input this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_filter_input (LrgUIInspector *self,
                                        LrgCanvasInput *input);

/**
 * lrg_ui_inspector_handle_input:
 * @self: an #LrgUIInspector
 *
 * Drop-in replacement for lrg_canvas_handle_input() on the inspected
 * canvas: polls graylib, runs lrg_ui_inspector_filter_input() and
 * processes the frame. Does nothing when detached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_handle_input (LrgUIInspector *self);

/**
 * lrg_ui_inspector_get_selected:
 * @self: an #LrgUIInspector
 *
 * Returns: (transfer none) (nullable): the selected widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_inspector_get_selected (LrgUIInspector *self);

/**
 * lrg_ui_inspector_set_selected:
 * @self: an #LrgUIInspector
 * @widget: (nullable): widget to select (held weakly), or %NULL
 *
 * Selects @widget; the highlight overlay frames it with its margin and
 * a "Type#name w×h" label.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_set_selected (LrgUIInspector *self,
                                    LrgWidget      *widget);

/**
 * lrg_ui_inspector_get_hovered:
 * @self: an #LrgUIInspector
 *
 * Returns: (transfer none) (nullable): the widget under the pointer in
 *   pick mode
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_inspector_get_hovered (LrgUIInspector *self);

/**
 * lrg_ui_inspector_highlight:
 * @self: an #LrgUIInspector
 * @path: (nullable): path of the widget to select, or %NULL to clear
 * @error: return location for a #GError
 *
 * Selects the widget at @path (see lrg_ui_inspector_lookup()) so the
 * overlay highlights it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_highlight (LrgUIInspector  *self,
                                     const gchar     *path,
                                     GError         **error);

/**
 * lrg_ui_inspector_get_highlight_visible:
 * @self: an #LrgUIInspector
 *
 * Returns: %TRUE if the selected / hovered widgets are drawn highlighted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_get_highlight_visible (LrgUIInspector *self);

/**
 * lrg_ui_inspector_set_highlight_visible:
 * @self: an #LrgUIInspector
 * @visible: whether to draw the highlight (on by default)
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_set_highlight_visible (LrgUIInspector *self,
                                             gboolean        visible);

/**
 * lrg_ui_inspector_paint_highlight:
 * @self: an #LrgUIInspector
 * @list: paint list to record into
 *
 * Records the highlight (hovered widget outline, selected widget's
 * bounds, margin and type label) at world coordinates. The inspector's
 * overlay widget calls this during lrg_canvas_record(), so most apps
 * never call it; use it to draw the highlight into a paint list of your
 * own, after the canvas has recorded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_paint_highlight (LrgUIInspector *self,
                                       LrgPaintList   *list);

/* --------------------------------------------------------------------------
 * Timings
 * -------------------------------------------------------------------------- */

/**
 * lrg_ui_inspector_get_timings:
 * @self: an #LrgUIInspector
 * @timings: (out caller-allocates): the last frame's statistics
 *
 * Fills @timings from the canvas phase timers and its paint list. All
 * zero when detached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_inspector_get_timings (LrgUIInspector        *self,
                                   LrgUIInspectorTimings *timings);

/* --------------------------------------------------------------------------
 * Properties
 * -------------------------------------------------------------------------- */

/**
 * lrg_ui_inspector_get_property:
 * @self: an #LrgUIInspector
 * @path: widget path
 * @name: GObject property name
 * @value: (out caller-allocates): an uninitialised #GValue, initialised
 *   to the property type on success
 * @error: return location for a #GError
 *
 * Reads a property of the widget at @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_get_property (LrgUIInspector  *self,
                                        const gchar     *path,
                                        const gchar     *name,
                                        GValue          *value,
                                        GError         **error);

/**
 * lrg_ui_inspector_set_property:
 * @self: an #LrgUIInspector
 * @path: widget path
 * @name: GObject property name
 * @value: new value; converted with g_value_transform() when its type
 *   differs from the property's
 * @error: return location for a #GError
 *
 * Writes a property of the widget at @path. Values outside the
 * property's range are rejected rather than clamped.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_set_property (LrgUIInspector  *self,
                                        const gchar     *path,
                                        const gchar     *name,
                                        const GValue    *value,
                                        GError         **error);

/**
 * lrg_ui_inspector_set_property_from_string:
 * @self: an #LrgUIInspector
 * @path: widget path
 * @name: GObject property name
 * @text: the value as text: numbers, "true"/"false", enum nicks or
 *   names, flags as "a|b", colours as "#rrggbb" or "#rrggbbaa"
 * @error: return location for a #GError
 *
 * Parses @text for the property's type and writes it, for consoles and
 * remote tools.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_inspector_set_property_from_string (LrgUIInspector  *self,
                                                    const gchar     *path,
                                                    const gchar     *name,
                                                    const gchar     *text,
                                                    GError         **error);

G_END_DECLS
