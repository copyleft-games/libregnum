/* lrg-environment-studio.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "studio" environment: a calm, neutral photo-studio look -- a soft grey
 * backdrop and a floor marked with concentric rings under the panels.  Meant
 * for long working sessions where the room should disappear.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENVIRONMENT_STUDIO (lrg_environment_studio_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironmentStudio, lrg_environment_studio, LRG, ENVIRONMENT_STUDIO, GObject)

LRG_AVAILABLE_IN_ALL
LrgEnvironmentStudio * lrg_environment_studio_new (void);

G_END_DECLS
