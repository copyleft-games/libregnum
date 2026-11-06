/* lrg-editor-viewport-nav.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportNav: the viewport navigation state machine as pure
 * logic (no UI toolkit, no window).
 *
 * The viewport widget feeds it pointer, wheel, key and focus events in
 * viewport-local pixels plus a per-frame tick; it moves the active camera
 * of its #LrgEditorViewState, reports its mode there (ui.state "nav") and
 * tells the caller whether it consumed each event, so picking and the
 * transform gizmo only see what navigation left over.
 *
 * Schemes (the camera-navigation setting):
 *   default   right drag flies (look + WASD/QE, Shift x3, wheel = speed),
 *             middle drag pans, Alt + left drag orbits, Alt + Shift + left
 *             pans, wheel dollies
 *   blender   middle drag orbits, Shift + middle pans, Ctrl + middle
 *             zooms; everything else as default
 *   trackpad  two-finger scroll orbits, Shift + scroll pans, Ctrl + scroll
 *             zooms; everything else as default
 * In every scheme the orientation gizmo (bottom-left corner) orbits when
 * dragged and aligns the view when an axis end is clicked, a right click
 * without motion asks for the context menu, and in 2D mode right/middle
 * drags pan. A left press that navigation does not take and the transform
 * gizmo does not take arms box selection: a drag past the click slop
 * becomes a box (#LrgEditorViewportNav::box-select), otherwise a click
 * (#LrgEditorViewportNav::click).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-editor-view-state.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_NAV_CLICK_SLOP:
 *
 * Pixels the pointer may move before a press becomes a drag.
 */
#define LRG_EDITOR_NAV_CLICK_SLOP (4.0f)

/**
 * LRG_EDITOR_NAV_GIZMO_RADIUS:
 *
 * Orientation gizmo axis length in pixels.
 */
#define LRG_EDITOR_NAV_GIZMO_RADIUS (30.0f)

/**
 * LRG_EDITOR_NAV_GIZMO_MARGIN:
 *
 * Distance of the orientation gizmo centre from the viewport's left and
 * bottom edges, in pixels.
 */
#define LRG_EDITOR_NAV_GIZMO_MARGIN (46.0f)

/**
 * LRG_EDITOR_NAV_GIZMO_END_HIT:
 *
 * Hit radius of an orientation gizmo axis end, in pixels.
 */
#define LRG_EDITOR_NAV_GIZMO_END_HIT (9.0f)

/**
 * LRG_EDITOR_NAV_AXIS_HOT_DISC:
 *
 * The value of #LrgEditorViewportNav:axis-hot while the pointer is over
 * the orientation gizmo's disc but not over an axis end.
 */
#define LRG_EDITOR_NAV_AXIS_HOT_DISC (6)

/**
 * LrgEditorNavScheme:
 * @LRG_EDITOR_NAV_SCHEME_DEFAULT: Unity/Godot-like mouse navigation
 * @LRG_EDITOR_NAV_SCHEME_BLENDER: middle-button orbit / Shift pan / Ctrl zoom
 * @LRG_EDITOR_NAV_SCHEME_TRACKPAD: scroll orbits / Shift pans / Ctrl zooms
 *
 * Viewport mouse scheme; the nicks are the camera-navigation setting
 * values.
 */
typedef enum
{
	LRG_EDITOR_NAV_SCHEME_DEFAULT,
	LRG_EDITOR_NAV_SCHEME_BLENDER,
	LRG_EDITOR_NAV_SCHEME_TRACKPAD
} LrgEditorNavScheme;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_nav_scheme_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_NAV_SCHEME (lrg_editor_nav_scheme_get_type ())

#define LRG_TYPE_EDITOR_VIEWPORT_NAV (lrg_editor_viewport_nav_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewportNav, lrg_editor_viewport_nav, LRG, EDITOR_VIEWPORT_NAV, GObject)

/**
 * lrg_editor_viewport_nav_new:
 * @view_state: (nullable): the view state whose active camera navigation
 *   moves; %NULL creates a private one
 *
 * Returns: (transfer full): a new #LrgEditorViewportNav
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportNav * lrg_editor_viewport_nav_new (LrgEditorViewState *view_state);

/**
 * lrg_editor_viewport_nav_get_view_state:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: (transfer none): the view state navigation acts on
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewState * lrg_editor_viewport_nav_get_view_state (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_get_camera:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: (transfer none): the camera navigation moves (the view state's
 *   active camera)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewCamera * lrg_editor_viewport_nav_get_camera (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_bind_settings:
 * @self: an #LrgEditorViewportNav
 * @settings: (nullable): editor settings, or %NULL to unbind
 *
 * Follows camera-navigation (#LrgEditorViewportNav:scheme),
 * camera-invert-y and camera-orbit-speed from now on.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_bind_settings (LrgEditorViewportNav *self,
                                            LrgEditorSettings    *settings);

/**
 * lrg_editor_viewport_nav_get_scheme:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: the navigation scheme
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavScheme lrg_editor_viewport_nav_get_scheme (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_set_scheme:
 * @self: an #LrgEditorViewportNav
 * @scheme: the navigation scheme
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_set_scheme (LrgEditorViewportNav *self,
                                         LrgEditorNavScheme    scheme);

/**
 * lrg_editor_viewport_nav_get_invert_y:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: whether vertical look and orbit are inverted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_get_invert_y (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_set_invert_y:
 * @self: an #LrgEditorViewportNav
 * @invert_y: whether to invert vertical look and orbit
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_set_invert_y (LrgEditorViewportNav *self,
                                           gboolean              invert_y);

/**
 * lrg_editor_viewport_nav_get_orbit_speed:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: look and orbit sensitivity in degrees per pixel
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_viewport_nav_get_orbit_speed (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_set_orbit_speed:
 * @self: an #LrgEditorViewportNav
 * @speed: degrees per pixel (0.01..5)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_set_orbit_speed (LrgEditorViewportNav *self,
                                              gdouble               speed);

/**
 * lrg_editor_viewport_nav_get_mode:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: the navigation in progress
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavMode lrg_editor_viewport_nav_get_mode (LrgEditorViewportNav *self);

/* --- Input ------------------------------------------------------------------- */

/**
 * lrg_editor_viewport_nav_pointer_press:
 * @self: an #LrgEditorViewportNav
 * @button: the button pressed
 * @x: viewport-local x in pixels
 * @y: viewport-local y in pixels
 * @mods: modifiers held
 *
 * Starts navigation: a left press on the orientation gizmo, Alt + left
 * (orbit, or pan with Shift or in 2D), right (fly, or pan in 2D), middle
 * (pan; in the blender scheme orbit / Shift pan / Ctrl zoom). Presses
 * while navigating are swallowed.
 *
 * Returns: %TRUE if navigation took the press; on %FALSE the caller tries
 *   the transform gizmo and then lrg_editor_viewport_nav_press_empty()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_pointer_press (LrgEditorViewportNav *self,
                                                GrlMouseButton        button,
                                                gfloat                x,
                                                gfloat                y,
                                                LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_press_empty:
 * @self: an #LrgEditorViewportNav
 * @x: viewport-local x of the left press
 * @y: viewport-local y of the left press
 *
 * Arms box selection after a left press that hit neither navigation nor
 * the transform gizmo. The release then emits
 * #LrgEditorViewportNav::click or #LrgEditorViewportNav::box-select.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_press_empty (LrgEditorViewportNav *self,
                                          gfloat                x,
                                          gfloat                y);

/**
 * lrg_editor_viewport_nav_pointer_move:
 * @self: an #LrgEditorViewportNav
 * @x: viewport-local x in pixels
 * @y: viewport-local y in pixels
 * @dx: relative x motion since the last move (unclamped, also while the
 *   cursor is locked)
 * @dy: relative y motion since the last move
 * @mods: modifiers held
 *
 * Applies look, orbit, pan or zoom drags, turns an armed left press into
 * a box past the click slop and tracks the hovered orientation gizmo end.
 *
 * Returns: %TRUE if navigation (or box selection) owns the pointer
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_pointer_move (LrgEditorViewportNav *self,
                                               gfloat                x,
                                               gfloat                y,
                                               gfloat                dx,
                                               gfloat                dy,
                                               LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_pointer_release:
 * @self: an #LrgEditorViewportNav
 * @button: the button released
 * @x: viewport-local x in pixels
 * @y: viewport-local y in pixels
 * @mods: modifiers held
 *
 * Ends navigation. A right press without motion emits
 * #LrgEditorViewportNav::context-menu, a click on an orientation gizmo
 * axis end aligns the view, an armed left press emits click or
 * box-select.
 *
 * Returns: %TRUE if navigation (or box selection) consumed the release
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_pointer_release (LrgEditorViewportNav *self,
                                                  GrlMouseButton        button,
                                                  gfloat                x,
                                                  gfloat                y,
                                                  LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_scroll:
 * @self: an #LrgEditorViewportNav
 * @dx: horizontal wheel / two-finger scroll
 * @dy: vertical wheel / two-finger scroll (positive = away from the user)
 * @mods: modifiers held
 *
 * While flying the wheel changes the fly speed (x1.2 per notch, 0.1..500).
 * Otherwise it dollies (x0.87 per notch); in the trackpad scheme without
 * Ctrl it orbits (4 degrees per unit) or with Shift / in 2D pans (12 px
 * per unit).
 *
 * Returns: %TRUE if the scroll was used
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_scroll (LrgEditorViewportNav *self,
                                         gfloat                dx,
                                         gfloat                dy,
                                         LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_key_down:
 * @self: an #LrgEditorViewportNav
 * @key: the key pressed
 * @mods: modifiers held
 *
 * Records a held key (WASD/QE and Shift drive flying in
 * lrg_editor_viewport_nav_tick()).
 *
 * Returns: %TRUE while flying for the fly keys, so they do not also fire
 *   shortcuts (W would otherwise switch to the move tool)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_key_down (LrgEditorViewportNav *self,
                                           GrlKey                key,
                                           LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_key_up:
 * @self: an #LrgEditorViewportNav
 * @key: the key released
 * @mods: modifiers held
 *
 * Returns: %TRUE while flying for the fly keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_key_up (LrgEditorViewportNav *self,
                                         GrlKey                key,
                                         LrgInputModifiers     mods);

/**
 * lrg_editor_viewport_nav_is_key_held:
 * @self: an #LrgEditorViewportNav
 * @key: a key
 *
 * Returns: whether @key is held as far as navigation knows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_is_key_held (LrgEditorViewportNav *self,
                                              GrlKey                key);

/**
 * lrg_editor_viewport_nav_focus_lost:
 * @self: an #LrgEditorViewportNav
 *
 * The window or viewport lost keyboard focus: forgets held keys and ends
 * any navigation or armed box selection without emitting click,
 * box-select or context-menu.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_focus_lost (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_cancel:
 * @self: an #LrgEditorViewportNav
 *
 * Ends navigation and box selection without side effects; held keys are
 * kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_cancel (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_tick:
 * @self: an #LrgEditorViewportNav
 * @dt: seconds since the last tick
 *
 * Applies the held fly keys while flying: W/S forward, D/A right, E/Q up,
 * at #LrgEditorViewState:fly-speed units per second, three times faster
 * with Shift.
 *
 * Returns: %TRUE if the camera moved (the viewport should redraw)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_tick (LrgEditorViewportNav *self,
                                       gfloat                dt);

/* --- Painting helpers --------------------------------------------------------- */

/**
 * lrg_editor_viewport_nav_get_box_rect:
 * @self: an #LrgEditorViewportNav
 * @x: (out) (optional): left edge, viewport-local
 * @y: (out) (optional): top edge
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Returns: %TRUE while a box selection is being dragged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_get_box_rect (LrgEditorViewportNav *self,
                                               gfloat               *x,
                                               gfloat               *y,
                                               gfloat               *width,
                                               gfloat               *height);

/**
 * lrg_editor_viewport_nav_get_axis_hot:
 * @self: an #LrgEditorViewportNav
 *
 * Returns: the hovered orientation gizmo end (0..2 = +X +Y +Z, 3..5 =
 *   -X -Y -Z), %LRG_EDITOR_NAV_AXIS_HOT_DISC over its disc, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_viewport_nav_get_axis_hot (LrgEditorViewportNav *self);

/**
 * lrg_editor_viewport_nav_get_gizmo_center:
 * @self: an #LrgEditorViewportNav
 * @x: (out): centre x, viewport-local
 * @y: (out): centre y
 *
 * Returns: %FALSE when the active camera is 2D (no orientation gizmo)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_nav_get_gizmo_center (LrgEditorViewportNav *self,
                                                   gfloat               *x,
                                                   gfloat               *y);

/**
 * lrg_editor_viewport_nav_get_gizmo_end:
 * @self: an #LrgEditorViewportNav
 * @index: axis end, 0..2 = +X +Y +Z, 3..5 = -X -Y -Z
 * @dx: (out): screen x offset from the centre
 * @dy: (out): screen y offset from the centre
 * @depth: (out) (optional): camera-space depth (draw far ends first)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_nav_get_gizmo_end (LrgEditorViewportNav *self,
                                            gint                  index,
                                            gfloat               *dx,
                                            gfloat               *dy,
                                            gfloat               *depth);

/**
 * lrg_editor_viewport_nav_gizmo_hit:
 * @self: an #LrgEditorViewportNav
 * @x: viewport-local x
 * @y: viewport-local y
 *
 * Returns: 0..5 for an axis end, %LRG_EDITOR_NAV_AXIS_HOT_DISC for the
 *   disc, -1 outside (always -1 in 2D)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_viewport_nav_gizmo_hit (LrgEditorViewportNav *self,
                                        gfloat                x,
                                        gfloat                y);

G_END_DECLS
