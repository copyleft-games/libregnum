/* lrg-frame-capture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameCapture: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_FRAME_CAPTURE (lrg_frame_capture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFrameCapture, lrg_frame_capture, LRG, FRAME_CAPTURE, GObject)

/**
 * lrg_frame_capture_new:
 *
 * Returns: (transfer full): a new #LrgFrameCapture
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_frame_capture_new (void);

G_END_DECLS
