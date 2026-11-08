/* lrg-stereo-widener.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mid/side stereo width: 0 folds the image to mono, 1 leaves it, 2
 * doubles the side signal.  The bass-mono corner keeps the side signal
 * out of the low end so a wide pad never smears the bass.  Mono buffers
 * pass untouched.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-audio-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEREO_WIDENER (lrg_stereo_widener_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgStereoWidener, lrg_stereo_widener, LRG, STEREO_WIDENER, LrgAudioEffect)

/**
 * lrg_stereo_widener_new:
 * @width: the side gain (0 .. 2)
 *
 * Creates the effect; every parameter is also a GObject property (#LrgStereoWidener:width, #LrgStereoWidener:bass-mono).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgStereoWidener
 */
LRG_AVAILABLE_IN_ALL
LrgStereoWidener * lrg_stereo_widener_new (gdouble width);

G_END_DECLS
