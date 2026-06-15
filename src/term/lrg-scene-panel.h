/* lrg-scene-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One textured quad in an #Lrg3DSurface scene: a slice of the captured frame
 * (an Emacs window, or the whole frame in single-panel mode) mapped onto a plane
 * with an eased transform and a depth-of-field focus weight.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_PANEL (lrg_scene_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScenePanel, lrg_scene_panel, LRG, SCENE_PANEL, GObject)

/**
 * lrg_scene_panel_new:
 * @key: a stable identity for the source window (0 for the whole-frame panel)
 *
 * Returns: (transfer full): a new #LrgScenePanel (with a unit plane mesh/model; no
 *   texture until lrg_scene_panel_update_texture() is called)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScenePanel * lrg_scene_panel_new (guint64 key);

LRG_AVAILABLE_IN_ALL
guint64 lrg_scene_panel_get_key (LrgScenePanel *self);

/**
 * lrg_scene_panel_set_source_rect:
 * @self: a #LrgScenePanel
 * @x: left of the source window within the captured frame, in pixels
 * @y: top of the source window, in pixels
 * @width: source width in pixels
 * @height: source height in pixels
 *
 * Records which sub-rectangle of the captured frame this panel shows. The next
 * lrg_scene_panel_update_texture() crops to it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_source_rect (LrgScenePanel *self,
                                gint      x,
                                gint      y,
                                gint      width,
                                gint      height);

LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_get_source_rect (LrgScenePanel *self,
                                gint     *x,
                                gint     *y,
                                gint     *width,
                                gint     *height);

/**
 * lrg_scene_panel_set_target:
 * @self: a #LrgScenePanel
 * @px: target world x of the panel centre
 * @py: target world y
 * @pz: target world z
 * @yaw: target yaw in degrees (rotation about world Y; 0 faces +Z)
 * @width: target panel width in world units
 * @height: target panel height in world units
 *
 * Sets the transform the panel eases toward.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_target (LrgScenePanel *self,
                           gfloat    px,
                           gfloat    py,
                           gfloat    pz,
                           gfloat    yaw,
                           gfloat    width,
                           gfloat    height);

/**
 * lrg_scene_panel_set_immediate:
 * @self: a #LrgScenePanel
 * @px: world x
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @width: width in world units
 * @height: height in world units
 *
 * Sets the current transform (and target) immediately, with no animation. Used
 * when a panel first appears so it does not fly in from the origin.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_immediate (LrgScenePanel *self,
                              gfloat    px,
                              gfloat    py,
                              gfloat    pz,
                              gfloat    yaw,
                              gfloat    width,
                              gfloat    height);

/**
 * lrg_scene_panel_set_target_focus:
 * @self: a #LrgScenePanel
 * @focus: 1.0 = fully focused (crisp/forward), 0.0 = fully unfocused (dim)
 *
 * Sets the depth-of-field focus weight the panel eases toward.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_target_focus (LrgScenePanel *self,
                                 gfloat    focus);

/**
 * lrg_scene_panel_pin:
 * @self: a #LrgScenePanel
 * @px: world x of the panel centre
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @width: width in world units
 * @height: height in world units
 *
 * Pins the panel to the given transform (applied immediately) and marks it
 * manually placed: while pinned, lrg_scene_panel_set_target() and
 * lrg_scene_panel_set_immediate() are ignored, so an #LrgSceneArrangement's
 * layout no longer moves it.  This is how a user-dragged ("grabbed") panel keeps
 * its place across arrangement re-layouts.  Focus (DOF) is unaffected.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_pin (LrgScenePanel *self,
                          gfloat         px,
                          gfloat         py,
                          gfloat         pz,
                          gfloat         yaw,
                          gfloat         width,
                          gfloat         height);

/**
 * lrg_scene_panel_repin:
 * @self: a #LrgScenePanel
 * @px: world x of the panel centre
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @width: width in world units
 * @height: height in world units
 *
 * Like lrg_scene_panel_pin(), but instead of snapping, *eases* the current
 * transform toward the new pinned target (the very first placement still snaps,
 * so a panel appears in place rather than flying in from the origin).  Used for
 * animated re-layout — e.g. a workspace carousel shifting smoothly on switch.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_repin (LrgScenePanel *self,
                            gfloat         px,
                            gfloat         py,
                            gfloat         pz,
                            gfloat         yaw,
                            gfloat         width,
                            gfloat         height);

/**
 * lrg_scene_panel_unpin:
 * @self: a #LrgScenePanel
 *
 * Clears the pinned flag so the arrangement governs the panel again (it eases
 * back to the layout position on the next layout pass).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_unpin (LrgScenePanel *self);

LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_panel_is_pinned (LrgScenePanel *self);

/**
 * lrg_scene_panel_update_texture:
 * @self: a #LrgScenePanel
 * @frame: the full captured frame image
 *
 * Crops @frame to the panel's source rectangle and uploads it to the panel's
 * GPU texture (creating it on first use, with mipmaps + anisotropic filtering
 * for legible minification at an angle).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_update_texture (LrgScenePanel *self,
                               GrlImage *frame);

/**
 * lrg_scene_panel_set_image:
 * @self: a #LrgScenePanel
 * @image: an image to display on the whole panel
 *
 * Uploads @image to the panel's GPU texture verbatim (no source-rect crop) and
 * marks the panel as carrying a *static texture*: it is then no longer overwritten
 * by the per-frame live capture in lrg_3d_surface_end_content().  This is how a
 * panel shows an independent image — e.g. an off-screen workspace rendered to its
 * own texture — rather than a slice of the current frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_image (LrgScenePanel *self,
                                GrlImage      *image);

/**
 * lrg_scene_panel_set_static_texture:
 * @self: a #LrgScenePanel
 * @static_texture: %TRUE to mark the panel as carrying a caller-supplied texture
 *
 * Marks (or clears) the static-texture flag.  A static panel keeps its own texture
 * (set via lrg_scene_panel_set_image()) and is excluded from the live per-frame
 * capture and from arrangement layout.  lrg_scene_panel_set_image() sets this
 * implicitly; the explicit setter lets a panel be reserved as static before its
 * first image arrives (so layout never moves it).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_set_static_texture (LrgScenePanel *self,
                                         gboolean       static_texture);

LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_panel_has_static_texture (LrgScenePanel *self);

/**
 * lrg_scene_panel_step:
 * @self: a #LrgScenePanel
 * @dt: elapsed seconds since the last step
 *
 * Eases the current transform + focus toward their targets.
 *
 * Returns: %TRUE while still animating, %FALSE once converged
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_panel_step (LrgScenePanel *self,
                         gfloat    dt);

LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_panel_is_animating (LrgScenePanel *self);

/**
 * lrg_scene_panel_draw:
 * @self: a #LrgScenePanel
 * @dim: (nullable): the colour an unfocused panel fades toward (NULL = black)
 *
 * Draws the panel inside the current 3D camera scope. The tint is interpolated
 * between @dim and white by the focus weight, giving the depth-of-field dimming.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_draw (LrgScenePanel       *self,
                     const GrlColor *dim);

/**
 * lrg_scene_panel_get_geometry:
 * @self: a #LrgScenePanel
 * @px: (out) (optional): current world x
 * @py: (out) (optional): current world y
 * @pz: (out) (optional): current world z
 * @yaw: (out) (optional): current yaw in degrees
 * @width: (out) (optional): current width in world units
 * @height: (out) (optional): current height in world units
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_get_geometry (LrgScenePanel *self,
                             gfloat   *px,
                             gfloat   *py,
                             gfloat   *pz,
                             gfloat   *yaw,
                             gfloat   *width,
                             gfloat   *height);

/**
 * lrg_scene_panel_get_corners:
 * @self: a #LrgScenePanel
 * @out_xyz12: (out caller-allocates) (array fixed-size=12): 4 world-space
 *   corners (top-left, top-right, bottom-right, bottom-left), x,y,z each
 *
 * Fills the panel's current quad corners in world space, for ray-cast picking.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_panel_get_corners (LrgScenePanel *self,
                            gfloat   *out_xyz12);

G_END_DECLS
