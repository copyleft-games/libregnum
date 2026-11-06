/* lrg-size-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSizeBox: one child with size overrides, limits and an aspect ratio.
 *
 * The six size constraints (width / height override, min and max) are
 * kept in one array indexed by property id so the setters, getters
 * and GObject plumbing share a single code path. Negative means unset.
 *
 * Measure: the child's preferred size, replaced by the overrides,
 * completed by the aspect ratio (an override on one axis derives the
 * other), then clamped. Layout: the box's content rectangle, replaced
 * by the overrides and clamped, then (with an aspect ratio) shrunk to
 * the largest rectangle of that ratio, positioned by the child's
 * alignment.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-size-box.h"
#include "lrg-widget-private.h"
#include "../lrg-log.h"

enum
{
	PROP_0,
	PROP_WIDTH_OVERRIDE,
	PROP_HEIGHT_OVERRIDE,
	PROP_MIN_WIDTH,
	PROP_MIN_HEIGHT,
	PROP_MAX_WIDTH,
	PROP_MAX_HEIGHT,
	PROP_ASPECT_RATIO,
	N_PROPS
};

/* Index of a size constraint in LrgSizeBox.size[] */
#define SIZE_INDEX(prop) ((prop) - PROP_WIDTH_OVERRIDE)
#define N_SIZES (PROP_MAX_HEIGHT - PROP_WIDTH_OVERRIDE + 1)

struct _LrgSizeBox
{
	LrgContainer parent_instance;

	gfloat       size[N_SIZES];   /* overrides, mins, maxes; < 0 = unset */
	gfloat       aspect_ratio;    /* width / height, 0 = unset */
};

G_DEFINE_FINAL_TYPE (LrgSizeBox, lrg_size_box, LRG_TYPE_CONTAINER)

static GParamSpec *properties[N_PROPS];

#define WIDTH_OVERRIDE(s)  ((s)->size[SIZE_INDEX (PROP_WIDTH_OVERRIDE)])
#define HEIGHT_OVERRIDE(s) ((s)->size[SIZE_INDEX (PROP_HEIGHT_OVERRIDE)])
#define MIN_WIDTH(s)       ((s)->size[SIZE_INDEX (PROP_MIN_WIDTH)])
#define MIN_HEIGHT(s)      ((s)->size[SIZE_INDEX (PROP_MIN_HEIGHT)])
#define MAX_WIDTH(s)       ((s)->size[SIZE_INDEX (PROP_MAX_WIDTH)])
#define MAX_HEIGHT(s)      ((s)->size[SIZE_INDEX (PROP_MAX_HEIGHT)])

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* clamp_set: clamp @v to whichever of @min / @max are set (>= 0) */
static gfloat
clamp_set (gfloat v,
           gfloat min,
           gfloat max)
{
	if (max >= 0.0f)
		v = MIN (v, max);
	if (min >= 0.0f)
		v = MAX (v, min);
	return v;
}

/*
 * align_fraction:
 *
 * Where a rectangle smaller than its area sits along one axis: 0 at
 * the start, 1 at the end, 0.5 centred (CENTER and FILL). Computed
 * left to right: in right-to-left lrg_widget_allocate() mirrors the
 * child's rectangle inside the box, so START ends up on the right.
 */
static gfloat
align_fraction (LrgAlign align)
{
	switch (align)
	{
	case LRG_ALIGN_START:
		return 0.0f;
	case LRG_ALIGN_END:
		return 1.0f;
	case LRG_ALIGN_CENTER:
	case LRG_ALIGN_FILL:
	default:
		return 0.5f;
	}
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_size_box_layout_children (LrgContainer *container)
{
	LrgSizeBox *self = LRG_SIZE_BOX (container);
	LrgWidget  *widget = LRG_WIDGET (container);
	LrgWidget  *child = lrg_size_box_get_child (self);
	gfloat      pad = lrg_container_get_padding (container);
	LrgInsets   m;
	gfloat      ax;
	gfloat      ay;
	gfloat      aw;
	gfloat      ah;
	gfloat      w;
	gfloat      h;
	gfloat      x;
	gfloat      y;

	if (child == NULL || !lrg_widget_get_visible (child))
		return;

	/* The area: content rectangle minus the child's margin */
	lrg_widget_get_margin (child, &m);
	ax = pad + m.left;
	ay = pad + m.top;
	aw = MAX (0.0f, lrg_widget_get_width (widget) - pad * 2.0f - m.left - m.right);
	ah = MAX (0.0f, lrg_widget_get_height (widget) - pad * 2.0f - m.top - m.bottom);

	/* Overrides replace the available size, then the limits apply */
	w = WIDTH_OVERRIDE (self) >= 0.0f ? WIDTH_OVERRIDE (self) : aw;
	h = HEIGHT_OVERRIDE (self) >= 0.0f ? HEIGHT_OVERRIDE (self) : ah;
	w = clamp_set (w, MIN_WIDTH (self), MAX_WIDTH (self));
	h = clamp_set (h, MIN_HEIGHT (self), MAX_HEIGHT (self));

	/* Unconstrained: an ordinary bin, the child's alignment applies */
	if (self->aspect_ratio <= 0.0f && w == aw && h == ah)
	{
		lrg_widget_place_in_slot (child, pad, pad,
		                          MAX (0.0f, lrg_widget_get_width (widget) - pad * 2.0f),
		                          MAX (0.0f, lrg_widget_get_height (widget) - pad * 2.0f));
		return;
	}

	/* Aspect ratio: the largest rectangle of that ratio inside (w, h);
	 * width-limited first, height-limited if that is too tall */
	if (self->aspect_ratio > 0.0f && w > 0.0f && h > 0.0f)
	{
		gfloat fw = w;
		gfloat fh = w / self->aspect_ratio;

		if (fh > h)
		{
			fh = h;
			fw = h * self->aspect_ratio;
		}
		w = fw;
		h = fh;
	}

	/* Position the constrained rectangle in the area by the child's
	 * alignment (FILL centres) */
	x = ax + (aw - w) * align_fraction (lrg_widget_get_halign (child));
	y = ay + (ah - h) * align_fraction (lrg_widget_get_valign (child));

	if (self->aspect_ratio > 0.0f)
	{
		/* The ratio is exact: allocate the rectangle as is */
		lrg_widget_allocate (child, roundf (x), roundf (y), roundf (w), roundf (h));
	}
	else
	{
		/* Fixed / limited size: the rectangle is the child's slot (with
		 * its margin back), so a CENTER child still centres in it */
		lrg_widget_place_in_slot (child, x - m.left, y - m.top,
		                          w + m.left + m.right, h + m.top + m.bottom);
	}
}

static void
lrg_size_box_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgSizeBox *self = LRG_SIZE_BOX (widget);
	LrgWidget  *child = lrg_size_box_get_child (self);
	gfloat      pad = lrg_container_get_padding (LRG_CONTAINER (widget));
	gboolean    has_w = WIDTH_OVERRIDE (self) >= 0.0f;
	gboolean    has_h = HEIGHT_OVERRIDE (self) >= 0.0f;
	LrgInsets   m = { 0.0f, 0.0f, 0.0f, 0.0f };
	gfloat      pw = 0.0f;
	gfloat      ph = 0.0f;
	gfloat      w;
	gfloat      h;

	if (child != NULL && lrg_widget_get_visible (child))
	{
		lrg_widget_get_preferred_size (child, &pw, &ph);
		lrg_widget_get_margin (child, &m);
	}

	/* Overrides replace the preferred size */
	w = has_w ? WIDTH_OVERRIDE (self) : pw;
	h = has_h ? HEIGHT_OVERRIDE (self) : ph;

	/* The ratio derives the axis that has no override; with neither,
	 * the width leads (unless the child reports no width) */
	if (self->aspect_ratio > 0.0f)
	{
		if (has_w && !has_h)
			h = w / self->aspect_ratio;
		else if (has_h && !has_w)
			w = h * self->aspect_ratio;
		else if (!has_w && !has_h)
		{
			if (w > 0.0f)
				h = w / self->aspect_ratio;
			else
				w = h * self->aspect_ratio;
		}
	}

	w = clamp_set (w, MIN_WIDTH (self), MAX_WIDTH (self));
	h = clamp_set (h, MIN_HEIGHT (self), MAX_HEIGHT (self));

	*preferred_width = ceilf (w) + m.left + m.right + pad * 2.0f;
	*preferred_height = ceilf (h) + m.top + m.bottom + pad * 2.0f;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

/* set_size: shared setter of the six size constraints */
static void
set_size (LrgSizeBox *self,
          guint       prop,
          gfloat      value)
{
	gfloat *field = &self->size[SIZE_INDEX (prop)];

	if (value < 0.0f)
		value = -1.0f;
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
	lrg_container_layout_children (LRG_CONTAINER (self));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
lrg_size_box_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgSizeBox *self = LRG_SIZE_BOX (object);

	if (prop_id >= PROP_WIDTH_OVERRIDE && prop_id <= PROP_MAX_HEIGHT)
		g_value_set_float (value, self->size[SIZE_INDEX (prop_id)]);
	else if (prop_id == PROP_ASPECT_RATIO)
		g_value_set_float (value, self->aspect_ratio);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_size_box_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgSizeBox *self = LRG_SIZE_BOX (object);

	if (prop_id >= PROP_WIDTH_OVERRIDE && prop_id <= PROP_MAX_HEIGHT)
		set_size (self, prop_id, g_value_get_float (value));
	else if (prop_id == PROP_ASPECT_RATIO)
		lrg_size_box_set_aspect_ratio (self, g_value_get_float (value));
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

/* size_pspec: the float spec every size constraint shares */
static GParamSpec *
size_pspec (const gchar *name,
            const gchar *nick,
            const gchar *blurb)
{
	return g_param_spec_float (name, nick, blurb, -1.0f, G_MAXFLOAT, -1.0f,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_size_box_class_init (LrgSizeBoxClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->get_property = lrg_size_box_get_property;
	object_class->set_property = lrg_size_box_set_property;

	widget_class->measure = lrg_size_box_measure;
	container_class->layout_children = lrg_size_box_layout_children;

	/**
	 * LrgSizeBox:width-override:
	 *
	 * Fixed width of the child (excluding its margin), -1 for none.
	 */
	properties[PROP_WIDTH_OVERRIDE] =
		size_pspec ("width-override", "Width override", "Fixed child width, -1 for none");

	/**
	 * LrgSizeBox:height-override:
	 *
	 * Fixed height of the child (excluding its margin), -1 for none.
	 */
	properties[PROP_HEIGHT_OVERRIDE] =
		size_pspec ("height-override", "Height override", "Fixed child height, -1 for none");

	/**
	 * LrgSizeBox:min-width:
	 *
	 * Minimum width of the child, -1 for none.
	 */
	properties[PROP_MIN_WIDTH] = size_pspec ("min-width", "Min width", "Minimum child width");

	/**
	 * LrgSizeBox:min-height:
	 *
	 * Minimum height of the child, -1 for none.
	 */
	properties[PROP_MIN_HEIGHT] = size_pspec ("min-height", "Min height", "Minimum child height");

	/**
	 * LrgSizeBox:max-width:
	 *
	 * Maximum width of the child, -1 for none.
	 */
	properties[PROP_MAX_WIDTH] = size_pspec ("max-width", "Max width", "Maximum child width");

	/**
	 * LrgSizeBox:max-height:
	 *
	 * Maximum height of the child, -1 for none.
	 */
	properties[PROP_MAX_HEIGHT] = size_pspec ("max-height", "Max height", "Maximum child height");

	/**
	 * LrgSizeBox:aspect-ratio:
	 *
	 * Width / height ratio the child is kept at, 0 for none.
	 */
	properties[PROP_ASPECT_RATIO] =
		g_param_spec_float ("aspect-ratio", "Aspect ratio", "Width / height of the child, 0 for none",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_size_box_init (LrgSizeBox *self)
{
	guint i;

	for (i = 0; i < N_SIZES; i++)
		self->size[i] = -1.0f;
	self->aspect_ratio = 0.0f;

	/* Letterbox bars let clicks through */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSizeBox *
lrg_size_box_new (void)
{
	return g_object_new (LRG_TYPE_SIZE_BOX, NULL);
}

LrgWidget *
lrg_size_box_get_child (LrgSizeBox *self)
{
	g_return_val_if_fail (LRG_IS_SIZE_BOX (self), NULL);
	return lrg_container_get_child (LRG_CONTAINER (self), 0);
}

void
lrg_size_box_set_child (LrgSizeBox *self,
                        LrgWidget  *child)
{
	g_return_if_fail (LRG_IS_SIZE_BOX (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	if (child != NULL && lrg_size_box_get_child (self) == child)
		return;
	lrg_container_remove_all (LRG_CONTAINER (self));
	if (child != NULL)
		lrg_container_add_child (LRG_CONTAINER (self), child);
}

#define SIZE_ACCESSORS(name, PROP)                                       \
	gfloat                                                               \
	lrg_size_box_get_##name (LrgSizeBox *self)                           \
	{                                                                    \
		g_return_val_if_fail (LRG_IS_SIZE_BOX (self), -1.0f);            \
		return self->size[SIZE_INDEX (PROP)];                            \
	}                                                                    \
	void                                                                 \
	lrg_size_box_set_##name (LrgSizeBox *self,                           \
	                         gfloat      value)                          \
	{                                                                    \
		g_return_if_fail (LRG_IS_SIZE_BOX (self));                       \
		set_size (self, PROP, value);                                    \
	}

SIZE_ACCESSORS (width_override, PROP_WIDTH_OVERRIDE)
SIZE_ACCESSORS (height_override, PROP_HEIGHT_OVERRIDE)
SIZE_ACCESSORS (min_width, PROP_MIN_WIDTH)
SIZE_ACCESSORS (min_height, PROP_MIN_HEIGHT)
SIZE_ACCESSORS (max_width, PROP_MAX_WIDTH)
SIZE_ACCESSORS (max_height, PROP_MAX_HEIGHT)

gfloat
lrg_size_box_get_aspect_ratio (LrgSizeBox *self)
{
	g_return_val_if_fail (LRG_IS_SIZE_BOX (self), 0.0f);
	return self->aspect_ratio;
}

void
lrg_size_box_set_aspect_ratio (LrgSizeBox *self,
                               gfloat      ratio)
{
	g_return_if_fail (LRG_IS_SIZE_BOX (self));

	if (!(ratio > 0.0f) || ratio > G_MAXFLOAT)
		ratio = 0.0f;
	if (self->aspect_ratio == ratio)
		return;
	self->aspect_ratio = ratio;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASPECT_RATIO]);
	lrg_container_layout_children (LRG_CONTAINER (self));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}
