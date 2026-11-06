/* lrg-widget.h
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for UI widgets.
 *
 * A widget has a rectangle relative to its parent, layout hints the
 * parent reads (margin, alignment, fill weight, min/max size),
 * interaction state flags, style classes, and two springs (hover and
 * press) that cross-fade its looks between states. Widgets record
 * their appearance into an #LrgPaintList through the paint() virtual
 * method; the older draw() method, which draws immediately with
 * graylib, keeps working for existing subclasses.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../paint/lrg-paint-list.h"
#include "lrg-ui-event.h"
#include "lrg-style.h"
#include "lrg-theme.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgInsets
 * ========================================================================== */

#define LRG_TYPE_INSETS (lrg_insets_get_type ())

/**
 * LrgInsets:
 * @left: space on the left
 * @top: space on top
 * @right: space on the right
 * @bottom: space at the bottom
 *
 * Space around a widget, in canvas units.
 */
typedef struct
{
	gfloat left;
	gfloat top;
	gfloat right;
	gfloat bottom;
} LrgInsets;

LRG_AVAILABLE_IN_ALL
GType lrg_insets_get_type (void) G_GNUC_CONST;

/**
 * lrg_insets_new:
 * @left: left
 * @top: top
 * @right: right
 * @bottom: bottom
 *
 * Returns: (transfer full): a new #LrgInsets
 */
LRG_AVAILABLE_IN_ALL
LrgInsets * lrg_insets_new (gfloat left,
                            gfloat top,
                            gfloat right,
                            gfloat bottom);

/**
 * lrg_insets_copy:
 * @self: an #LrgInsets
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgInsets * lrg_insets_copy (const LrgInsets *self);

/**
 * lrg_insets_free:
 * @self: (nullable): an #LrgInsets
 */
LRG_AVAILABLE_IN_ALL
void lrg_insets_free (LrgInsets *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgInsets, lrg_insets_free)

/* ==========================================================================
 * LrgWidget
 * ========================================================================== */

#define LRG_TYPE_WIDGET (lrg_widget_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWidget, lrg_widget, LRG, WIDGET, GObject)

/**
 * LrgWidgetClass:
 * @parent_class: The parent class
 * @draw: Legacy immediate drawing with graylib
 * @measure: Calculates the preferred size
 * @handle_event: Handles an input event
 * @paint: Records the widget into a paint list
 * @style_defaults: Built-in look of a part in a state
 * @arrange: Called after the widget's rectangle was assigned
 * @step: Advances the widget's own animations
 *
 * The class structure for #LrgWidget.
 *
 * New widgets override @paint (and usually @style_defaults); widgets
 * that only override @draw keep working and are drawn through a paint
 * callback. @measure returns the preferred size without margins.
 */
struct _LrgWidgetClass
{
	GObjectClass parent_class;

	/**
	 * LrgWidgetClass::draw:
	 * @self: the widget
	 *
	 * Draws the widget immediately at its world position with graylib.
	 * Superseded by @paint; the default implementation renders @paint
	 * output on the spot so lrg_widget_draw() works for every widget.
	 */
	void     (*draw)           (LrgWidget *self);

	/**
	 * LrgWidgetClass::measure:
	 * @self: the widget
	 * @preferred_width: (out): preferred width
	 * @preferred_height: (out): preferred height
	 *
	 * Calculates the widget's preferred size, excluding its margin.
	 */
	void     (*measure)        (LrgWidget *self,
	                            gfloat    *preferred_width,
	                            gfloat    *preferred_height);

	/**
	 * LrgWidgetClass::handle_event:
	 * @self: the widget
	 * @event: the UI event
	 *
	 * Handles an event. Return %TRUE to stop it bubbling to the parent.
	 *
	 * Returns: %TRUE if the event was consumed
	 */
	gboolean (*handle_event)   (LrgWidget        *self,
	                            const LrgUIEvent *event);

	/**
	 * LrgWidgetClass::paint:
	 * @self: the widget
	 * @list: the paint list to record into
	 *
	 * Records the widget at its world position. Opacity, appear
	 * animations and the focus ring are applied around this call by
	 * lrg_widget_paint(); containers paint their children here.
	 */
	void     (*paint)          (LrgWidget    *self,
	                            LrgPaintList *list);

	/**
	 * LrgWidgetClass::style_defaults:
	 * @self: the widget
	 * @part: the part being drawn
	 * @state: the state to describe
	 * @look: (inout): look to fill
	 *
	 * Fills the widget's built-in look of @part in @state from its
	 * theme. Theme style rules are merged on top afterwards. Called by
	 * lrg_widget_resolve_look() for the resting, hovered and pressed
	 * variants of the current state.
	 */
	void     (*style_defaults) (LrgWidget      *self,
	                            LrgUIPart       part,
	                            LrgWidgetState  state,
	                            LrgPartLook    *look);

	/**
	 * LrgWidgetClass::arrange:
	 * @self: the widget
	 *
	 * Called when the widget's rectangle was assigned by its parent
	 * (lrg_widget_allocate()) or it was marked for layout. Containers
	 * position their children here; the default for #LrgContainer
	 * runs its layout_children() method.
	 */
	void     (*arrange)        (LrgWidget *self);

	/**
	 * LrgWidgetClass::step:
	 * @self: the widget
	 * @delta_time: seconds since the last frame
	 *
	 * Advances springs and timers the widget owns itself (knobs,
	 * indicators, scroll offsets, toast timers). Called by
	 * lrg_widget_step() every frame, before painting, so animations
	 * follow the canvas clock (deterministic in tests and replays) and
	 * lrg_canvas_update() knows when anything is still moving.
	 *
	 * Returns: %TRUE while something is still animating
	 */
	gboolean (*step)           (LrgWidget *self,
	                            gfloat     delta_time);

	/*< private >*/
	gpointer _reserved[4];
};

/* ==========================================================================
 * Position and Size
 * ========================================================================== */

/**
 * lrg_widget_get_x:
 * @self: an #LrgWidget
 *
 * Returns: The X position relative to the parent
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_x (LrgWidget *self);

/**
 * lrg_widget_set_x:
 * @self: an #LrgWidget
 * @x: the X position relative to the parent
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_x (LrgWidget *self,
                       gfloat     x);

/**
 * lrg_widget_get_y:
 * @self: an #LrgWidget
 *
 * Returns: The Y position relative to the parent
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_y (LrgWidget *self);

/**
 * lrg_widget_set_y:
 * @self: an #LrgWidget
 * @y: the Y position relative to the parent
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_y (LrgWidget *self,
                       gfloat     y);

/**
 * lrg_widget_get_width:
 * @self: an #LrgWidget
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_width (LrgWidget *self);

/**
 * lrg_widget_set_width:
 * @self: an #LrgWidget
 * @width: the width
 *
 * Sets the width. A container whose size changes is marked for layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_width (LrgWidget *self,
                           gfloat     width);

/**
 * lrg_widget_get_height:
 * @self: an #LrgWidget
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_height (LrgWidget *self);

/**
 * lrg_widget_set_height:
 * @self: an #LrgWidget
 * @height: the height
 *
 * Sets the height. A container whose size changes is marked for layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_height (LrgWidget *self,
                            gfloat     height);

/**
 * lrg_widget_set_position:
 * @self: an #LrgWidget
 * @x: the X position
 * @y: the Y position
 *
 * Sets both the X and Y position at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_position (LrgWidget *self,
                              gfloat     x,
                              gfloat     y);

/**
 * lrg_widget_set_size:
 * @self: an #LrgWidget
 * @width: the width
 * @height: the height
 *
 * Sets both the width and height at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_size (LrgWidget *self,
                          gfloat     width,
                          gfloat     height);

/**
 * lrg_widget_allocate:
 * @self: an #LrgWidget
 * @x: X relative to the parent
 * @y: Y relative to the parent
 * @width: width
 * @height: height
 *
 * Assigns the widget's rectangle and lays it out (containers position
 * their children). Parents call this from their layout; it does the
 * work of set_position() + set_size() + arrange() in one step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_allocate (LrgWidget *self,
                          gfloat     x,
                          gfloat     y,
                          gfloat     width,
                          gfloat     height);

/* ==========================================================================
 * World Coordinates
 * ========================================================================== */

/**
 * lrg_widget_get_world_x:
 * @self: an #LrgWidget
 *
 * Returns: The absolute X position (all parent offsets added)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_world_x (LrgWidget *self);

/**
 * lrg_widget_get_world_y:
 * @self: an #LrgWidget
 *
 * Returns: The absolute Y position (all parent offsets added)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_world_y (LrgWidget *self);

/* ==========================================================================
 * Visibility and sensitivity
 * ========================================================================== */

/**
 * lrg_widget_get_visible:
 * @self: an #LrgWidget
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_visible (LrgWidget *self);

/**
 * lrg_widget_set_visible:
 * @self: an #LrgWidget
 * @visible: whether to show the widget
 *
 * Shows or hides the widget. Showing a widget with an appear
 * animation plays it again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_visible (LrgWidget *self,
                             gboolean   visible);

/**
 * lrg_widget_get_enabled:
 * @self: an #LrgWidget
 *
 * Returns: %TRUE if the widget responds to input
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_enabled (LrgWidget *self);

/**
 * lrg_widget_set_enabled:
 * @self: an #LrgWidget
 * @enabled: whether to enable the widget
 *
 * Disabled widgets ignore input, cannot take focus and carry
 * %LRG_WIDGET_STATE_DISABLED.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_enabled (LrgWidget *self,
                             gboolean   enabled);

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

/**
 * lrg_widget_get_parent:
 * @self: an #LrgWidget
 *
 * Returns: (transfer none) (nullable): The parent #LrgContainer, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgContainer * lrg_widget_get_parent (LrgWidget *self);

/**
 * lrg_widget_get_root:
 * @self: an #LrgWidget
 *
 * Returns: (transfer none): the top-most ancestor (usually an #LrgCanvas),
 *   or @self when it has no parent
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_widget_get_root (LrgWidget *self);

/**
 * lrg_widget_is_ancestor:
 * @self: an #LrgWidget
 * @ancestor: a possible ancestor
 *
 * Returns: %TRUE if @ancestor contains @self at any depth
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_is_ancestor (LrgWidget *self,
                                 LrgWidget *ancestor);

/* ==========================================================================
 * Hit Testing
 * ========================================================================== */

/**
 * lrg_widget_contains_point:
 * @self: an #LrgWidget
 * @x: the X coordinate in world space
 * @y: the Y coordinate in world space
 *
 * Returns: %TRUE if the point is inside the widget
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_contains_point (LrgWidget *self,
                                    gfloat     x,
                                    gfloat     y);

/* ==========================================================================
 * Layout hints
 * ========================================================================== */

/**
 * lrg_widget_get_name:
 * @self: an #LrgWidget
 *
 * Returns: (nullable): the widget's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_widget_get_name (LrgWidget *self);

/**
 * lrg_widget_set_name:
 * @self: an #LrgWidget
 * @name: (nullable): a name, used by lrg_widget_find() and the inspector
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_name (LrgWidget   *self,
                          const gchar *name);

/**
 * lrg_widget_find:
 * @self: an #LrgWidget
 * @name: name to look for
 *
 * Searches @self and its descendants depth-first.
 *
 * Returns: (transfer none) (nullable): the first widget called @name
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_widget_find (LrgWidget   *self,
                             const gchar *name);

/**
 * lrg_widget_get_margin:
 * @self: an #LrgWidget
 * @margin: (out caller-allocates): the margin
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_get_margin (LrgWidget *self,
                            LrgInsets *margin);

/**
 * lrg_widget_set_margin:
 * @self: an #LrgWidget
 * @margin: space the parent leaves around the widget
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_margin (LrgWidget       *self,
                            const LrgInsets *margin);

/**
 * lrg_widget_set_margin_all:
 * @self: an #LrgWidget
 * @margin: the same margin on every side
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_margin_all (LrgWidget *self,
                                gfloat     margin);

/**
 * lrg_widget_get_halign:
 * @self: an #LrgWidget
 *
 * Returns: horizontal alignment inside the parent's slot
 */
LRG_AVAILABLE_IN_ALL
LrgAlign lrg_widget_get_halign (LrgWidget *self);

/**
 * lrg_widget_set_halign:
 * @self: an #LrgWidget
 * @align: horizontal alignment (default %LRG_ALIGN_FILL)
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_halign (LrgWidget *self,
                            LrgAlign   align);

/**
 * lrg_widget_get_valign:
 * @self: an #LrgWidget
 *
 * Returns: vertical alignment inside the parent's slot
 */
LRG_AVAILABLE_IN_ALL
LrgAlign lrg_widget_get_valign (LrgWidget *self);

/**
 * lrg_widget_set_valign:
 * @self: an #LrgWidget
 * @align: vertical alignment (default %LRG_ALIGN_FILL)
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_valign (LrgWidget *self,
                            LrgAlign   align);

/**
 * lrg_widget_get_fill_weight:
 * @self: an #LrgWidget
 *
 * Returns: share of the parent box's spare space this widget takes
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_fill_weight (LrgWidget *self);

/**
 * lrg_widget_set_fill_weight:
 * @self: an #LrgWidget
 * @weight: 0 (default) keeps the preferred size along the box's axis;
 *   positive weights split the leftover space in proportion
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_fill_weight (LrgWidget *self,
                                 gfloat     weight);

/**
 * lrg_widget_set_min_size:
 * @self: an #LrgWidget
 * @width: minimum width, 0 for none
 * @height: minimum height, 0 for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_min_size (LrgWidget *self,
                              gfloat     width,
                              gfloat     height);

/**
 * lrg_widget_set_max_size:
 * @self: an #LrgWidget
 * @width: maximum width, 0 for none
 * @height: maximum height, 0 for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_max_size (LrgWidget *self,
                              gfloat     width,
                              gfloat     height);

/**
 * lrg_widget_get_min_size:
 * @self: an #LrgWidget
 * @width: (out) (optional): minimum width
 * @height: (out) (optional): minimum height
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_get_min_size (LrgWidget *self,
                              gfloat    *width,
                              gfloat    *height);

/**
 * lrg_widget_get_max_size:
 * @self: an #LrgWidget
 * @width: (out) (optional): maximum width (0 = none)
 * @height: (out) (optional): maximum height (0 = none)
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_get_max_size (LrgWidget *self,
                              gfloat    *width,
                              gfloat    *height);

/**
 * lrg_widget_get_preferred_size:
 * @self: an #LrgWidget
 * @width: (out) (optional): preferred width clamped to min/max
 * @height: (out) (optional): preferred height clamped to min/max
 *
 * The size layouts should give the widget: measure() clamped to the
 * min and max size. The margin is not included.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_get_preferred_size (LrgWidget *self,
                                    gfloat    *width,
                                    gfloat    *height);

/**
 * lrg_widget_place_in_slot:
 * @self: an #LrgWidget
 * @x: slot left, relative to the parent
 * @y: slot top
 * @width: slot width
 * @height: slot height
 *
 * Allocates the widget inside a slot its parent computed, honouring
 * the widget's margin, alignment and min/max size. Container layouts
 * call this for each child.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_place_in_slot (LrgWidget *self,
                               gfloat     x,
                               gfloat     y,
                               gfloat     width,
                               gfloat     height);

/**
 * lrg_widget_queue_layout:
 * @self: an #LrgWidget
 *
 * Marks the widget and its ancestors as needing layout; the canvas
 * lays them out before the next paint. Propagation stops at the first
 * ancestor already marked. Call it when something that affects the
 * preferred size changes (text, font, children).
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_queue_layout (LrgWidget *self);

/**
 * lrg_widget_get_needs_layout:
 * @self: an #LrgWidget
 *
 * Returns: %TRUE if the widget is waiting for layout
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_needs_layout (LrgWidget *self);

/**
 * lrg_widget_layout:
 * @self: an #LrgWidget
 *
 * Lays out @self and any marked descendants now, skipping subtrees
 * that are clean. The canvas calls this every frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_layout (LrgWidget *self);

/* ==========================================================================
 * Opacity, appearance, theme
 * ========================================================================== */

/**
 * lrg_widget_get_opacity:
 * @self: an #LrgWidget
 *
 * Returns: the widget's opacity (0..1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_opacity (LrgWidget *self);

/**
 * lrg_widget_set_opacity:
 * @self: an #LrgWidget
 * @opacity: 0..1, applied to the widget and its children
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_opacity (LrgWidget *self,
                             gfloat     opacity);

/**
 * lrg_widget_get_appear:
 * @self: an #LrgWidget
 *
 * Returns: the entrance animation
 */
LRG_AVAILABLE_IN_ALL
LrgAppear lrg_widget_get_appear (LrgWidget *self);

/**
 * lrg_widget_set_appear:
 * @self: an #LrgWidget
 * @appear: entrance animation played the first time the widget is
 *   painted and whenever it is shown again
 * @delay: seconds to wait before starting; stagger siblings for a
 *   cascade
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_appear (LrgWidget *self,
                            LrgAppear  appear,
                            gfloat     delay);

/**
 * lrg_widget_play_appear:
 * @self: an #LrgWidget
 *
 * Restarts the entrance animation now.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_play_appear (LrgWidget *self);

/**
 * lrg_widget_get_appear_progress:
 * @self: an #LrgWidget
 *
 * Returns: 0 before the entrance animation starts, 1 once it has
 *   finished (can overshoot 1 briefly with bouncy motion)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_appear_progress (LrgWidget *self);

/**
 * lrg_widget_get_theme:
 * @self: an #LrgWidget
 *
 * The theme the widget draws with: its own (see lrg_widget_set_theme()),
 * else the nearest ancestor's, else the default theme.
 *
 * Returns: (transfer none): the effective theme
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_widget_get_theme (LrgWidget *self);

/**
 * lrg_widget_set_theme:
 * @self: an #LrgWidget
 * @theme: (nullable): a theme for this widget and its subtree, or
 *   %NULL to inherit
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_theme (LrgWidget *self,
                           LrgTheme  *theme);

/**
 * lrg_widget_get_motion:
 * @self: an #LrgWidget
 *
 * Returns: the spring preset for this widget's transitions: its own
 *   override, else the theme's
 */
LRG_AVAILABLE_IN_ALL
LrgMotion lrg_widget_get_motion (LrgWidget *self);

/**
 * lrg_widget_set_motion:
 * @self: an #LrgWidget
 * @motion: a preset, or -1 to follow the theme
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_motion (LrgWidget *self,
                            gint       motion);

/* ==========================================================================
 * Style classes and state
 * ========================================================================== */

/**
 * lrg_widget_add_class:
 * @self: an #LrgWidget
 * @style_class: a class name such as "primary" or "flat"
 *
 * Adds a style class; theme rules can target it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_add_class (LrgWidget   *self,
                           const gchar *style_class);

/**
 * lrg_widget_remove_class:
 * @self: an #LrgWidget
 * @style_class: a class name
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_remove_class (LrgWidget   *self,
                              const gchar *style_class);

/**
 * lrg_widget_has_class:
 * @self: an #LrgWidget
 * @style_class: a class name
 *
 * Returns: %TRUE if the widget carries @style_class
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_has_class (LrgWidget   *self,
                               const gchar *style_class);

/**
 * lrg_widget_get_classes:
 * @self: an #LrgWidget
 * @n_classes: (out): number of classes
 *
 * Returns: (transfer none) (array length=n_classes): the classes as quarks
 */
LRG_AVAILABLE_IN_ALL
const GQuark * lrg_widget_get_classes (LrgWidget *self,
                                       guint     *n_classes);

/**
 * lrg_widget_get_state_flags:
 * @self: an #LrgWidget
 *
 * Returns: the current state flags
 */
LRG_AVAILABLE_IN_ALL
LrgWidgetState lrg_widget_get_state_flags (LrgWidget *self);

/**
 * lrg_widget_set_state_flags:
 * @self: an #LrgWidget
 * @flags: flags to turn on
 *
 * Turns on state flags; emits #LrgWidget::state-changed when that
 * changes anything. The canvas manages HOVERED, PRESSED, FOCUSED and
 * FOCUS_VISIBLE; widgets manage CHECKED, SELECTED and OPEN.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_state_flags (LrgWidget      *self,
                                 LrgWidgetState  flags);

/**
 * lrg_widget_unset_state_flags:
 * @self: an #LrgWidget
 * @flags: flags to turn off
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_unset_state_flags (LrgWidget      *self,
                                   LrgWidgetState  flags);

/**
 * lrg_widget_get_focusable:
 * @self: an #LrgWidget
 *
 * Returns: %TRUE if the widget can take keyboard / gamepad focus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_focusable (LrgWidget *self);

/**
 * lrg_widget_set_focusable:
 * @self: an #LrgWidget
 * @focusable: whether Tab, gamepad navigation and clicks can focus it
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_focusable (LrgWidget *self,
                               gboolean   focusable);

/**
 * lrg_widget_get_pointer_transparent:
 * @self: an #LrgWidget
 *
 * Returns: %TRUE if pointer hit-testing passes through the widget
 *   itself (its children can still be hit)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_pointer_transparent (LrgWidget *self);

/**
 * lrg_widget_set_pointer_transparent:
 * @self: an #LrgWidget
 * @transparent: let clicks on empty areas of the widget reach what is
 *   underneath; used by full-screen layers such as toast queues and
 *   overlays
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_pointer_transparent (LrgWidget *self,
                                         gboolean   transparent);

/**
 * lrg_widget_get_hover_progress:
 * @self: an #LrgWidget
 *
 * Returns: the hover spring, 0 (resting) to 1 (hovered)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_hover_progress (LrgWidget *self);

/**
 * lrg_widget_get_press_progress:
 * @self: an #LrgWidget
 *
 * Returns: the press spring, 0 (released) to 1 (pressed)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_press_progress (LrgWidget *self);

/**
 * lrg_widget_step:
 * @self: an #LrgWidget
 * @delta_time: seconds since the last frame
 *
 * Advances the widget's springs and appear animation, and those of
 * its descendants. The canvas calls this every frame.
 *
 * Returns: %TRUE if anything is still animating
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_step (LrgWidget *self,
                          gfloat     delta_time);

/* ==========================================================================
 * Looks
 * ========================================================================== */

/**
 * lrg_widget_resolve_look:
 * @self: an #LrgWidget
 * @part: the part to draw
 * @look: (out caller-allocates): the look to draw it with
 *
 * The look of @part right now: built-in defaults and theme rules for
 * the resting, hovered and pressed variants of the widget's state,
 * blended by its hover and press springs. This is what makes colours,
 * shadows and sizes glide between states.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_resolve_look (LrgWidget   *self,
                              LrgUIPart    part,
                              LrgPartLook *look);

/**
 * lrg_widget_resolve_look_for_state:
 * @self: an #LrgWidget
 * @part: the part to draw
 * @state: the state to describe
 * @look: (out caller-allocates): defaults plus matching theme rules
 *
 * The look of @part in one exact state, without spring blending.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_resolve_look_for_state (LrgWidget      *self,
                                        LrgUIPart       part,
                                        LrgWidgetState  state,
                                        LrgPartLook    *look);

/**
 * lrg_widget_paint_box:
 * @self: an #LrgWidget
 * @list: a paint list
 * @look: a resolved look
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Draws the standard surface for a look: glow, drop shadow, fill or
 * gradient and border, honouring the look's opacity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_paint_box (LrgWidget         *self,
                           LrgPaintList      *list,
                           const LrgPartLook *look,
                           gfloat             x,
                           gfloat             y,
                           gfloat             width,
                           gfloat             height);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_widget_draw:
 * @self: an #LrgWidget
 *
 * Draws the widget immediately (legacy path). Only draws if visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_draw (LrgWidget *self);

/**
 * lrg_widget_paint:
 * @self: an #LrgWidget
 * @list: the paint list
 *
 * Records the widget: applies opacity and the appear animation, calls
 * the paint() virtual method (or the legacy draw() through a
 * callback), and adds the focus ring when focus is visible. Does
 * nothing for invisible widgets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_paint (LrgWidget    *self,
                       LrgPaintList *list);

/**
 * lrg_widget_measure:
 * @self: an #LrgWidget
 * @preferred_width: (out): location to store preferred width
 * @preferred_height: (out): location to store preferred height
 *
 * Calls the measure() virtual method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_measure (LrgWidget *self,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height);

/**
 * lrg_widget_handle_event:
 * @self: an #LrgWidget
 * @event: the UI event to handle
 *
 * Calls handle_event() if the widget is visible and enabled.
 *
 * Returns: %TRUE if the event was consumed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_handle_event (LrgWidget        *self,
                                  const LrgUIEvent *event);

G_END_DECLS
