/* lrg-camera-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCameraComponent: owns an #LrgCamera3D that follows the entity's world
 * transform (eye at the entity, looking down its local -Z) and registers it
 * with the #LrgWorld3D camera registry by priority.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-camera3d.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA_COMPONENT (lrg_camera_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCameraComponent, lrg_camera_component, LRG, CAMERA_COMPONENT, LrgComponent)

/**
 * lrg_camera_component_new:
 *
 * Creates an active perspective camera: 60 degree vertical field of view,
 * near 0.1, far 1000, priority 0, every render layer visible.
 *
 * Returns: (transfer full): a new #LrgCameraComponent
 */
LRG_AVAILABLE_IN_ALL
LrgCameraComponent * lrg_camera_component_new (void);

/**
 * lrg_camera_component_get_camera:
 * @self: an #LrgCameraComponent
 *
 * Gets the camera renderers draw with.  Its pose, projection, field of
 * view and cull mask are written by the component; change the
 * component's properties, not the camera's.
 *
 * Returns: (transfer none): the camera
 */
LRG_AVAILABLE_IN_ALL
LrgCamera3D * lrg_camera_component_get_camera (LrgCameraComponent *self);

/**
 * lrg_camera_component_from_camera:
 * @camera: a camera
 *
 * Finds the component that owns @camera, e.g. to read the near and far
 * planes of the world's active camera (#LrgCamera3D has no clip planes).
 *
 * Returns: (transfer none) (nullable): the owning component, or %NULL
 *   when @camera does not belong to one
 */
LRG_AVAILABLE_IN_ALL
LrgCameraComponent * lrg_camera_component_from_camera (LrgCamera *camera);

/**
 * lrg_camera_component_get_fov:
 * @self: an #LrgCameraComponent
 *
 * Returns: the vertical field of view in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_component_get_fov (LrgCameraComponent *self);

/**
 * lrg_camera_component_set_fov:
 * @self: an #LrgCameraComponent
 * @fov: vertical field of view in degrees, 1 .. 179
 *
 * Sets the perspective field of view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_set_fov (LrgCameraComponent *self,
                                   gfloat              fov);

/**
 * lrg_camera_component_get_near:
 * @self: an #LrgCameraComponent
 *
 * Returns: the near clip distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_component_get_near (LrgCameraComponent *self);

/**
 * lrg_camera_component_get_far:
 * @self: an #LrgCameraComponent
 *
 * Returns: the far clip distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_component_get_far (LrgCameraComponent *self);

/**
 * lrg_camera_component_set_clip:
 * @self: an #LrgCameraComponent
 * @near_plane: near clip distance (> 0)
 * @far_plane: far clip distance (> @near_plane)
 *
 * Sets the clip planes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_set_clip (LrgCameraComponent *self,
                                    gfloat              near_plane,
                                    gfloat              far_plane);

/**
 * lrg_camera_component_get_orthographic:
 * @self: an #LrgCameraComponent
 *
 * Returns: %TRUE for an orthographic projection
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_component_get_orthographic (LrgCameraComponent *self);

/**
 * lrg_camera_component_set_orthographic:
 * @self: an #LrgCameraComponent
 * @orthographic: whether to project orthographically
 *
 * Switches the projection.  An orthographic camera shows
 * #LrgCameraComponent:ortho-size world units vertically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_set_orthographic (LrgCameraComponent *self,
                                            gboolean            orthographic);

/**
 * lrg_camera_component_get_priority:
 * @self: an #LrgCameraComponent
 *
 * Returns: the registry priority (higher wins)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_camera_component_get_priority (LrgCameraComponent *self);

/**
 * lrg_camera_component_set_priority:
 * @self: an #LrgCameraComponent
 * @priority: higher wins lrg_world3d_get_active_camera()
 *
 * Sets the priority (updates the world registry at once).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_set_priority (LrgCameraComponent *self,
                                        gint                priority);

/**
 * lrg_camera_component_get_active:
 * @self: an #LrgCameraComponent
 *
 * Returns: %TRUE if the camera takes part in the world's camera selection
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_component_get_active (LrgCameraComponent *self);

/**
 * lrg_camera_component_set_active:
 * @self: an #LrgCameraComponent
 * @active: whether the camera is registered with the world
 *
 * An inactive camera (or a disabled component) is unregistered from the
 * world, so it can never become the active camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_set_active (LrgCameraComponent *self,
                                      gboolean            active);

/**
 * lrg_camera_component_is_current:
 * @self: an #LrgCameraComponent
 *
 * Returns: %TRUE if the world's active camera is this component's
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_component_is_current (LrgCameraComponent *self);

/**
 * lrg_camera_component_make_current:
 * @self: an #LrgCameraComponent
 *
 * Forces this camera as the world's active camera
 * (lrg_world3d_set_active_camera()), ignoring priorities.  Does nothing
 * outside a world.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_make_current (LrgCameraComponent *self);

/**
 * lrg_camera_component_sync:
 * @self: an #LrgCameraComponent
 *
 * Writes the entity's world pose and the projection properties into the
 * camera now (late_update() does this every frame).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_component_sync (LrgCameraComponent *self);

G_END_DECLS
