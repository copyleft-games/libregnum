/* lrg-skinned-model-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedModelComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SKINNED_MODEL_COMPONENT (lrg_skinned_model_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkinnedModelComponent, lrg_skinned_model_component, LRG, SKINNED_MODEL_COMPONENT, GObject)

/**
 * lrg_skinned_model_component_new:
 *
 * Returns: (transfer full): a new #LrgSkinnedModelComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedModelComponent * lrg_skinned_model_component_new (void);

G_END_DECLS
