/* lrg-overlay.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgOverlay: A stack of children drawn on top of each other, each placed by its own alignment and margin.
 *
 * Every visible child gets the whole content rectangle as its slot;
 * lrg_widget_place_in_slot() then applies the child's margin, halign
 * and valign, so a badge with END/START alignment sits in the top
 * right corner over a FILL child. The container's default paint and
 * the canvas's hit-testing already treat later children as being on
 * top.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-overlay.h"
#include "lrg-widget-private.h"
#include "../lrg-log.h"

struct _LrgOverlay
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgOverlay, lrg_overlay, LRG_TYPE_CONTAINER)

static void
lrg_overlay_layout_children (LrgContainer *container)
{
	LrgWidget *widget = LRG_WIDGET (container);
	gfloat     pad = lrg_container_get_padding (container);
	gfloat     w = MAX (0.0f, lrg_widget_get_width (widget) - pad * 2.0f);
	gfloat     h = MAX (0.0f, lrg_widget_get_height (widget) - pad * 2.0f);
	GList     *l;

	/* One shared slot: the content rectangle */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);

		if (lrg_widget_get_visible (child))
			lrg_widget_place_in_slot (child, pad, pad, w, h);
	}
}

static void
lrg_overlay_measure (LrgWidget *widget,
                     gfloat    *preferred_width,
                     gfloat    *preferred_height)
{
	LrgContainer *container = LRG_CONTAINER (widget);
	gfloat        pad = lrg_container_get_padding (container);
	gfloat        w = 0.0f;
	gfloat        h = 0.0f;
	GList        *l;

	/* Big enough for the largest child, margins included */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		gfloat     cw;
		gfloat     ch;

		if (!lrg_widget_get_visible (child))
			continue;
		_lrg_widget_outer_size (child, &cw, &ch);
		w = MAX (w, cw);
		h = MAX (h, ch);
	}

	*preferred_width = w + pad * 2.0f;
	*preferred_height = h + pad * 2.0f;
}

static void
lrg_overlay_class_init (LrgOverlayClass *klass)
{
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	widget_class->measure = lrg_overlay_measure;
	container_class->layout_children = lrg_overlay_layout_children;
}

static void
lrg_overlay_init (LrgOverlay *self)
{
}

LrgOverlay *
lrg_overlay_new (void)
{
	return g_object_new (LRG_TYPE_OVERLAY, NULL);
}
