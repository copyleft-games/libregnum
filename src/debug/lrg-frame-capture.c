/* lrg-frame-capture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameCapture: (stub, being written)
 */

#include "config.h"

#include "lrg-frame-capture.h"

struct _LrgFrameCapture
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgFrameCapture, lrg_frame_capture, G_TYPE_OBJECT)

static void
lrg_frame_capture_class_init (LrgFrameCaptureClass *klass)
{
}

static void
lrg_frame_capture_init (LrgFrameCapture *self)
{
}

LrgFrameCapture *
lrg_frame_capture_new (void)
{
	return g_object_new (LRG_TYPE_FRAME_CAPTURE, NULL);
}
