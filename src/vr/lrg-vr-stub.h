/* lrg-vr-stub.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Stub VR implementation for when no VR runtime is available.
 *
 * This provides a no-op implementation of #LrgVRService that
 * returns appropriate failure states for all operations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-vr-service.h"

G_BEGIN_DECLS

#define LRG_TYPE_VR_STUB (lrg_vr_stub_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRStub, lrg_vr_stub, LRG, VR_STUB, GObject)

/**
 * lrg_vr_stub_new:
 *
 * Creates a new VR stub instance.
 *
 * This is a no-op implementation that returns failure for all
 * VR operations. Use this as a fallback when no VR runtime is
 * available.
 *
 * Returns: (transfer full): a new #LrgVRStub
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRStub * lrg_vr_stub_new (void);

/**
 * lrg_vr_stub_get_default:
 *
 * Gets the default VR stub instance.
 *
 * Returns: (transfer none): the default #LrgVRStub
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRStub * lrg_vr_stub_get_default (void);

G_END_DECLS
