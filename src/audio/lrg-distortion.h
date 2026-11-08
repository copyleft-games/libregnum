/* lrg-distortion.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Waveshaping distortion and bit reduction: warm tape-like saturation
 * (tanh), soft and hard clipping, wave folding and a bit crusher, with
 * a tone low-pass after the shaper and a dry/wet mix.
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

/**
 * LrgDistortionMode:
 * @LRG_DISTORTION_MODE_TANH: smooth saturation, `tanh (drive x)`
 * @LRG_DISTORTION_MODE_SOFT_CLIP: a cubic soft clipper, flat above full scale
 * @LRG_DISTORTION_MODE_HARD_CLIP: clamps at full scale (harsh, buzzy)
 * @LRG_DISTORTION_MODE_FOLD: folds peaks back down (metallic overtones)
 * @LRG_DISTORTION_MODE_BITCRUSH: fewer bits and a lower sample rate
 *
 * The shaping curve of an #LrgDistortion.
 */
typedef enum
{
	LRG_DISTORTION_MODE_TANH,
	LRG_DISTORTION_MODE_SOFT_CLIP,
	LRG_DISTORTION_MODE_HARD_CLIP,
	LRG_DISTORTION_MODE_FOLD,
	LRG_DISTORTION_MODE_BITCRUSH
} LrgDistortionMode;

LRG_AVAILABLE_IN_ALL
GType lrg_distortion_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DISTORTION_MODE (lrg_distortion_mode_get_type ())

#define LRG_TYPE_DISTORTION (lrg_distortion_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDistortion, lrg_distortion, LRG, DISTORTION, LrgAudioEffect)

/**
 * lrg_distortion_new:
 * @mode: the shaping curve
 * @drive_db: input gain into the shaper in dB (0 .. 48)
 *
 * Creates the effect; every parameter is also a GObject property (#LrgDistortion:drive, #LrgDistortion:tone, #LrgDistortion:output, #LrgDistortion:mix, #LrgDistortion:bits, #LrgDistortion:downsample).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgDistortion
 */
LRG_AVAILABLE_IN_ALL
LrgDistortion * lrg_distortion_new (LrgDistortionMode mode,
                                    gdouble drive_db);

G_END_DECLS
