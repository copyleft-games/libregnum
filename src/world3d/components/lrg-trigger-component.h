/* lrg-trigger-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_COMPONENT (lrg_trigger_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerComponent, lrg_trigger_component, LRG, TRIGGER_COMPONENT, GObject)

/**
 * lrg_trigger_component_new:
 *
 * Returns: (transfer full): a new #LrgTriggerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerComponent * lrg_trigger_component_new (void);

G_END_DECLS
