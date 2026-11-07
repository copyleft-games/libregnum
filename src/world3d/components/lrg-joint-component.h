/* lrg-joint-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJointComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_JOINT_COMPONENT (lrg_joint_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgJointComponent, lrg_joint_component, LRG, JOINT_COMPONENT, LrgComponent)

/**
 * lrg_joint_component_new:
 *
 * Returns: (transfer full): a new #LrgJointComponent
 */
LRG_AVAILABLE_IN_ALL
LrgJointComponent * lrg_joint_component_new (void);

G_END_DECLS
