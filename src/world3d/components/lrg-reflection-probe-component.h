/* lrg-reflection-probe-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbeComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_REFLECTION_PROBE_COMPONENT (lrg_reflection_probe_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReflectionProbeComponent, lrg_reflection_probe_component, LRG, REFLECTION_PROBE_COMPONENT, LrgComponent)

/**
 * lrg_reflection_probe_component_new:
 *
 * Returns: (transfer full): a new #LrgReflectionProbeComponent
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbeComponent * lrg_reflection_probe_component_new (void);

G_END_DECLS
