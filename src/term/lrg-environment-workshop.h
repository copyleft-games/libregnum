/* lrg-environment-workshop.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "workshop" environment: a spatial-editor room with a dim grid floor
 * beneath the panels.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENVIRONMENT_WORKSHOP (lrg_environment_workshop_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironmentWorkshop, lrg_environment_workshop, LRG, ENVIRONMENT_WORKSHOP, GObject)

LRG_AVAILABLE_IN_ALL
LrgEnvironmentWorkshop * lrg_environment_workshop_new (void);

G_END_DECLS
