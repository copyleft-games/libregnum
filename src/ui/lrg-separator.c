/* lrg-separator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSeparator: A thin horizontal or vertical divider line.
 *
 * The line is the BOX part: its background is the line colour (theme
 * BORDER) and its size the thickness (theme BORDER_WIDTH), so theme
 * rules can recolour or thicken separators.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-separator.h"
#include "../lrg-log.h"

struct _LrgSeparator
{
	LrgWidget      parent_instance;

	LrgOrientation orientation;
};

G_DEFINE_FINAL_TYPE (LrgSeparator, lrg_separator, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_ORIENTATION,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* thickness: the line's thickness from its (themed) look */
static gfloat
thickness (LrgSeparator *self)
{
	LrgPartLook look;

	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_BOX,
	                                   LRG_WIDGET_STATE_NONE, &look);
	return MAX (look.size, 0.0f);
}

static void
lrg_separator_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgSeparator *self = LRG_SEPARATOR (widget);
	gfloat        t = thickness (self);

	/* Thick on the cross axis, nothing on the main axis: boxes stretch
	 * it along their cross axis with the default FILL alignment */
	if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
	{
		*preferred_width = 0.0f;
		*preferred_height = t;
	}
	else
	{
		*preferred_width = t;
		*preferred_height = 0.0f;
	}
}

static void
lrg_separator_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part == LRG_UI_PART_BOX)
	{
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;
	}

	LRG_WIDGET_CLASS (lrg_separator_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_separator_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgSeparator *self = LRG_SEPARATOR (widget);
	LrgPartLook   look;
	gfloat        x = lrg_widget_get_world_x (widget);
	gfloat        y = lrg_widget_get_world_y (widget);
	gfloat        w = lrg_widget_get_width (widget);
	gfloat        h = lrg_widget_get_height (widget);
	gfloat        t;
	gboolean      faded;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	t = MAX (look.size, 0.0f);
	if (t <= 0.0f || look.background.a == 0)
		return;

	faded = look.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, look.opacity);

	/* A crisp bar centred in the allocation, on whole units */
	if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
		lrg_paint_list_rect (list, x, roundf (y + (h - t) * 0.5f), w, t, 0.0f, &look.background);
	else
		lrg_paint_list_rect (list, roundf (x + (w - t) * 0.5f), y, t, h, 0.0f, &look.background);

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

static void
lrg_separator_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgSeparator *self = LRG_SEPARATOR (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		g_value_set_enum (value, self->orientation);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_separator_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgSeparator *self = LRG_SEPARATOR (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		lrg_separator_set_orientation (self, (LrgOrientation) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_separator_class_init (LrgSeparatorClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->get_property = lrg_separator_get_property;
	object_class->set_property = lrg_separator_set_property;

	widget_class->measure = lrg_separator_measure;
	widget_class->paint = lrg_separator_paint;
	widget_class->style_defaults = lrg_separator_style_defaults;

	/**
	 * LrgSeparator:orientation:
	 *
	 * Direction of the line: horizontal (a line across, the default)
	 * or vertical (a line down).
	 */
	properties[PROP_ORIENTATION] =
		g_param_spec_enum ("orientation", "Orientation", "Direction of the line",
		                   LRG_TYPE_ORIENTATION, LRG_ORIENTATION_HORIZONTAL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_separator_init (LrgSeparator *self)
{
	self->orientation = LRG_ORIENTATION_HORIZONTAL;
}

LrgSeparator *
lrg_separator_new (void)
{
	return g_object_new (LRG_TYPE_SEPARATOR, NULL);
}

LrgSeparator *
lrg_separator_new_with_orientation (LrgOrientation orientation)
{
	return g_object_new (LRG_TYPE_SEPARATOR, "orientation", orientation, NULL);
}

LrgOrientation
lrg_separator_get_orientation (LrgSeparator *self)
{
	g_return_val_if_fail (LRG_IS_SEPARATOR (self), LRG_ORIENTATION_HORIZONTAL);
	return self->orientation;
}

void
lrg_separator_set_orientation (LrgSeparator   *self,
                               LrgOrientation  orientation)
{
	g_return_if_fail (LRG_IS_SEPARATOR (self));

	if (self->orientation == orientation)
		return;
	self->orientation = orientation;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}
