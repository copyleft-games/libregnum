/* lrg-environment-solid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "void" environment: a plain dark backdrop, no ambient geometry. The
 * cheapest #LrgPanelEnvironment.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENVIRONMENT_SOLID (lrg_environment_solid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironmentSolid, lrg_environment_solid, LRG, ENVIRONMENT_SOLID, GObject)

LRG_AVAILABLE_IN_ALL
LrgEnvironmentSolid * lrg_environment_solid_new (void);

G_END_DECLS
