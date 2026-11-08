/* lrg-reflection-probe-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbeComponent: an #LrgReflectionProbe placed by an
 * entity.  The component owns the probe, keeps its position on the
 * entity's world position and its box on the authored extents times the
 * entity's world scale, registers it with the environment of the world
 * it is in (lrg_world3d_get_environment()) and tracks whether it needs a
 * new capture (it entered the world, moved, its box or resolution
 * changed, or lrg_reflection_probe_component_request_capture() was
 * called).  The capture itself is GPU work done by the renderer
 * (lrg_world3d_renderer_capture_probes()) or by the game through
 * lrg_reflection_probe_component_capture().
 *
 * A world without an environment gets none from the component: the
 * probe then stays unregistered (and unused by the lit shader) until the
 * world is given one, which the next sync (every late_update) picks up.
 * lrg_level_instantiate_3d() gives the world the level's environment
 * before any entity enters it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../lighting/lrg-reflection-probe.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_REFLECTION_PROBE_COMPONENT (lrg_reflection_probe_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReflectionProbeComponent, lrg_reflection_probe_component, LRG, REFLECTION_PROBE_COMPONENT, LrgComponent)

/**
 * lrg_reflection_probe_component_new:
 *
 * Creates a component owning a new #LrgReflectionProbe with the probe's
 * defaults: extents 5 x 5 x 5 (a 10 m box), blend distance 1, intensity
 * 1, box projection on, the default resolution, clip planes 0.05 .. 1000
 * and automatic recapture on.
 *
 * Returns: (transfer full): a new #LrgReflectionProbeComponent
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbeComponent * lrg_reflection_probe_component_new (void);

/**
 * lrg_reflection_probe_component_get_probe:
 * @self: an #LrgReflectionProbeComponent
 *
 * The probe the component owns.  Its position and extents are rewritten
 * by lrg_reflection_probe_component_sync(): change the component's
 * properties instead of the probe's for those two.
 *
 * Returns: (transfer none): the probe
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbe * lrg_reflection_probe_component_get_probe (LrgReflectionProbeComponent *self);

/**
 * lrg_reflection_probe_component_set_extents:
 * @self: an #LrgReflectionProbeComponent
 * @x: half size of the box along X before the entity's scale (> 0)
 * @y: half size along Y (> 0)
 * @z: half size along Z (> 0)
 *
 * Sets #LrgReflectionProbeComponent:extents-x, -y and -z.  The probe's
 * axis-aligned box is these half sizes times the entity's world scale,
 * centred on the entity's world position (the entity's rotation does not
 * turn the box).
 */
LRG_AVAILABLE_IN_ALL
void lrg_reflection_probe_component_set_extents (LrgReflectionProbeComponent *self,
                                                 gfloat                       x,
                                                 gfloat                       y,
                                                 gfloat                       z);

/**
 * lrg_reflection_probe_component_get_extents:
 * @self: an #LrgReflectionProbeComponent
 * @out_extents: (out caller-allocates) (array fixed-size=3): the authored
 *   half sizes (before the entity's scale)
 */
LRG_AVAILABLE_IN_ALL
void lrg_reflection_probe_component_get_extents (LrgReflectionProbeComponent *self,
                                                 gfloat                      *out_extents);

/**
 * lrg_reflection_probe_component_set_auto_recapture:
 * @self: an #LrgReflectionProbeComponent
 * @auto_recapture: %TRUE to request a capture whenever the entity moves
 *   or the box changes
 *
 * Sets #LrgReflectionProbeComponent:auto-recapture.  Off, the probe is
 * captured once after it enters the world and then only on
 * lrg_reflection_probe_component_request_capture() (the usual choice for
 * a probe on a moving platform, which would otherwise recapture every
 * frame).
 */
LRG_AVAILABLE_IN_ALL
void lrg_reflection_probe_component_set_auto_recapture (LrgReflectionProbeComponent *self,
                                                        gboolean                     auto_recapture);

/**
 * lrg_reflection_probe_component_get_auto_recapture:
 * @self: an #LrgReflectionProbeComponent
 *
 * Returns: whether moves and box changes request a capture
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_reflection_probe_component_get_auto_recapture (LrgReflectionProbeComponent *self);

/**
 * lrg_reflection_probe_component_request_capture:
 * @self: an #LrgReflectionProbeComponent
 *
 * Marks the probe as needing a capture (the scene around it changed:
 * a door opened, the lights were switched).  The next
 * lrg_world3d_renderer_capture_probes() or
 * lrg_reflection_probe_component_capture_world() captures it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_reflection_probe_component_request_capture (LrgReflectionProbeComponent *self);

/**
 * lrg_reflection_probe_component_get_needs_capture:
 * @self: an #LrgReflectionProbeComponent
 *
 * Whether a capture is pending.  Any successful capture of the probe
 * after the request clears it, whoever made it (the component, the
 * renderer capturing the environment's probes, or a direct
 * lrg_reflection_probe_capture()).
 *
 * Returns: %TRUE when the probe should be captured
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_reflection_probe_component_get_needs_capture (LrgReflectionProbeComponent *self);

/**
 * lrg_reflection_probe_component_sync:
 * @self: an #LrgReflectionProbeComponent
 *
 * Copies the entity's world position and the scaled extents into the
 * probe (requesting a capture when they changed and
 * #LrgReflectionProbeComponent:auto-recapture is on) and moves the probe
 * into the environment the world has now: it is registered only while
 * the component is enabled and its entity is active in the hierarchy.
 * Runs in late_update and when the entity enters the world; call it
 * after replacing the world's environment if no frame runs in between.
 */
LRG_AVAILABLE_IN_ALL
void lrg_reflection_probe_component_sync (LrgReflectionProbeComponent *self);

/**
 * lrg_reflection_probe_component_capture:
 * @self: an #LrgReflectionProbeComponent
 * @draw_func: (scope call) (closure user_data): draws the scene for each
 *   face, see #LrgReflectionProbeDrawFunc
 * @user_data: data for @draw_func
 * @error: return location for a #GError
 *
 * Syncs, then captures the probe (lrg_reflection_probe_capture()) and
 * clears the pending request.  Needs a window; call outside
 * BeginDrawing().
 *
 * Returns: %TRUE on success, else %FALSE with an
 *   %LRG_ENVIRONMENT_ERROR_GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_reflection_probe_component_capture (LrgReflectionProbeComponent  *self,
                                                 LrgReflectionProbeDrawFunc    draw_func,
                                                 gpointer                      user_data,
                                                 GError                      **error);

/**
 * lrg_reflection_probe_component_capture_world:
 * @world: an #LrgWorld3D
 * @force: %TRUE to capture every enabled probe, %FALSE for only those
 *   whose capture is pending
 * @draw_func: (scope call) (closure user_data): draws the scene for each
 *   face
 * @user_data: data for @draw_func
 * @error: return location for the first #GError
 *
 * lrg_reflection_probe_component_capture() on the reflection probe
 * components of @world (enabled, entity active) that need it.  Probes
 * are captured in hierarchy order; a later probe sees the earlier ones'
 * fresh captures when @draw_func draws with the environment bound.
 * Stops at the first failure.
 *
 * Returns: the number of probes captured, or -1 on failure
 */
LRG_AVAILABLE_IN_ALL
gint lrg_reflection_probe_component_capture_world (LrgWorld3D                  *world,
                                                   gboolean                     force,
                                                   LrgReflectionProbeDrawFunc   draw_func,
                                                   gpointer                     user_data,
                                                   GError                     **error);

/**
 * lrg_reflection_probe_component_count_pending:
 * @world: an #LrgWorld3D
 *
 * Returns: how many enabled reflection probe components of @world on
 *   active entities have a capture pending
 */
LRG_AVAILABLE_IN_ALL
guint lrg_reflection_probe_component_count_pending (LrgWorld3D *world);

G_END_DECLS
