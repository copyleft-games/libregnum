/* lrg-rigid-body-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRigidBodyComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_RIGID_BODY_COMPONENT (lrg_rigid_body_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRigidBodyComponent, lrg_rigid_body_component, LRG, RIGID_BODY_COMPONENT, LrgComponent)

/**
 * lrg_rigid_body_component_new:
 *
 * Returns: (transfer full): a new #LrgRigidBodyComponent
 */
LRG_AVAILABLE_IN_ALL
LrgRigidBodyComponent * lrg_rigid_body_component_new (void);

G_END_DECLS
