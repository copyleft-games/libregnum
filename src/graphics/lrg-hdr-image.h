/* lrg-hdr-image.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrImage: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_HDR_IMAGE (lrg_hdr_image_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHdrImage, lrg_hdr_image, LRG, HDR_IMAGE, GObject)

/**
 * lrg_hdr_image_new:
 *
 * Returns: (transfer full): a new #LrgHdrImage
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage * lrg_hdr_image_new (void);

G_END_DECLS
