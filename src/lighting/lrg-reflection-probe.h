/* lrg-reflection-probe.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbe: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_REFLECTION_PROBE (lrg_reflection_probe_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReflectionProbe, lrg_reflection_probe, LRG, REFLECTION_PROBE, GObject)

/**
 * lrg_reflection_probe_new:
 *
 * Returns: (transfer full): a new #LrgReflectionProbe
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbe * lrg_reflection_probe_new (void);

G_END_DECLS
