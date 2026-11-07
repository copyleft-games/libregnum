/* lrg-color-gradient.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorGradient: (stub, being written)
 */

#include "config.h"

#include "lrg-color-gradient.h"

struct _LrgColorGradient
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgColorGradient, lrg_color_gradient, G_TYPE_OBJECT)

static void
lrg_color_gradient_class_init (LrgColorGradientClass *klass)
{
}

static void
lrg_color_gradient_init (LrgColorGradient *self)
{
}

LrgColorGradient *
lrg_color_gradient_new (void)
{
	return g_object_new (LRG_TYPE_COLOR_GRADIENT, NULL);
}
