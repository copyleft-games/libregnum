/* lrg-hdr-image.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrImage: (stub, being written)
 */

#include "config.h"

#include "lrg-hdr-image.h"

struct _LrgHdrImage
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgHdrImage, lrg_hdr_image, G_TYPE_OBJECT)

static void
lrg_hdr_image_class_init (LrgHdrImageClass *klass)
{
}

static void
lrg_hdr_image_init (LrgHdrImage *self)
{
}

LrgHdrImage *
lrg_hdr_image_new (void)
{
	return g_object_new (LRG_TYPE_HDR_IMAGE, NULL);
}
