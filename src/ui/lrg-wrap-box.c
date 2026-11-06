/* lrg-wrap-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWrapBox: A box that flows children into lines, wrapping when a line is full.
 *
 * Children keep their preferred size and are placed one after another
 * along the main axis (left to right, or top to bottom when vertical);
 * when the next one does not fit, a new line starts. Lines are
 * justified (start / center / end / space-between) and items aligned
 * across their line (start / center / end / fill).
 *
 * Measuring is the tricky part: the height of a horizontal wrap box
 * depends on its width, but measure() gets no width. The box measures
 * with the width it was last allocated (a single line before the first
 * allocation) and reports its natural single-line width. When an
 * allocation produces a different number of lines than the last
 * measure assumed, the next step() queues a layout so the parent
 * measures again with the new width: the height settles one frame
 * after a width change. Everything below is written along "main" and
 * "cross" axes so the vertical orientation is the same code.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-wrap-box.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Enum type
 * ========================================================================== */

GType
lrg_wrap_justify_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WRAP_JUSTIFY_START, "LRG_WRAP_JUSTIFY_START", "start" },
			{ LRG_WRAP_JUSTIFY_CENTER, "LRG_WRAP_JUSTIFY_CENTER", "center" },
			{ LRG_WRAP_JUSTIFY_END, "LRG_WRAP_JUSTIFY_END", "end" },
			{ LRG_WRAP_JUSTIFY_SPACE_BETWEEN, "LRG_WRAP_JUSTIFY_SPACE_BETWEEN", "space-between" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgWrapJustify"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

struct _LrgWrapBox
{
	LrgContainer    parent_instance;

	LrgOrientation  orientation;
	gfloat          line_spacing;
	LrgWrapJustify  justify;
	LrgAlign        item_align;

	guint           n_lines;
	gfloat          measured_cross;  /* cross length the last measure reported */
	gboolean        remeasure;       /* a layout found a different cross length */
	gboolean        in_layout;
};

G_DEFINE_FINAL_TYPE (LrgWrapBox, lrg_wrap_box, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_ORIENTATION,
	PROP_LINE_SPACING,
	PROP_JUSTIFY,
	PROP_ITEM_ALIGN,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

typedef struct
{
	LrgWidget *widget;
	gfloat     main;   /* outer preferred size along the flow */
	gfloat     cross;  /* outer preferred size across it */
} Item;

typedef struct
{
	guint  first;
	guint  count;
	gfloat main;       /* items plus spacing */
	gfloat cross;      /* tallest item */
} Line;

static gboolean
horizontal (LrgWrapBox *self)
{
	return self->orientation == LRG_ORIENTATION_HORIZONTAL;
}

/* collect_items: visible children with their outer preferred sizes */
static GArray *
collect_items (LrgWrapBox *self)
{
	GArray *items = g_array_new (FALSE, FALSE, sizeof (Item));
	GList  *l;

	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		LrgInsets  m;
		gfloat     w;
		gfloat     h;
		Item       it;

		if (!lrg_widget_get_visible (child))
			continue;
		lrg_widget_get_preferred_size (child, &w, &h);
		lrg_widget_get_margin (child, &m);
		w += m.left + m.right;
		h += m.top + m.bottom;

		it.widget = child;
		it.main = horizontal (self) ? w : h;
		it.cross = horizontal (self) ? h : w;
		g_array_append_val (items, it);
	}
	return items;
}

/*
 * break_lines:
 * @limit: main-axis room for a line
 *
 * Greedy line breaking: an item starts a new line when it would push a
 * non-empty line past @limit. An item longer than @limit gets a line of
 * its own.
 */
static GArray *
break_lines (LrgWrapBox *self,
             GArray     *items,
             gfloat      limit)
{
	GArray *lines = g_array_new (FALSE, FALSE, sizeof (Line));
	gfloat  spacing = lrg_container_get_spacing (LRG_CONTAINER (self));
	Line    cur = { 0, 0, 0.0f, 0.0f };
	guint   i;

	for (i = 0; i < items->len; i++)
	{
		Item  *it = &g_array_index (items, Item, i);
		gfloat need = cur.count > 0 ? cur.main + spacing + it->main : it->main;

		if (cur.count > 0 && need > limit + 0.01f)
		{
			g_array_append_val (lines, cur);
			cur.first = i;
			cur.count = 0;
			cur.main = 0.0f;
			cur.cross = 0.0f;
			need = it->main;
		}
		cur.main = need;
		cur.cross = MAX (cur.cross, it->cross);
		cur.count++;
	}
	if (cur.count > 0)
		g_array_append_val (lines, cur);
	return lines;
}

/* lines_cross: total cross length of @lines with the line spacing */
static gfloat
lines_cross (LrgWrapBox *self,
             GArray     *lines)
{
	gfloat total = 0.0f;
	guint  i;

	for (i = 0; i < lines->len; i++)
		total += g_array_index (lines, Line, i).cross;
	if (lines->len > 1)
		total += self->line_spacing * (gfloat) (lines->len - 1);
	return total;
}

/* main_room: allocated main-axis room inside the padding, or 0 if unknown */
static gfloat
main_room (LrgWrapBox *self)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat     len = horizontal (self) ? lrg_widget_get_width (w) : lrg_widget_get_height (w);

	return len > 0.0f ? MAX (0.0f, len - pad * 2.0f) : 0.0f;
}

/* ==========================================================================
 * Measure and layout
 * ========================================================================== */

/*
 * lrg_wrap_box_measure:
 *
 * Main axis: the natural single-line length. Cross axis: the lines
 * needed at the currently allocated main length (single line while it
 * is unknown). See the file comment for why this is two-pass.
 */
static void
lrg_wrap_box_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgWrapBox *self = LRG_WRAP_BOX (widget);
	gfloat      pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat      room = main_room (self);
	GArray     *items = collect_items (self);
	GArray     *single = break_lines (self, items, G_MAXFLOAT);
	GArray     *lines = break_lines (self, items, room > 0.0f ? room : G_MAXFLOAT);
	gfloat      main = single->len > 0 ? g_array_index (single, Line, 0).main : 0.0f;
	gfloat      cross = lines_cross (self, lines);

	self->measured_cross = cross;
	if (horizontal (self))
	{
		*preferred_width = main + pad * 2.0f;
		*preferred_height = cross + pad * 2.0f;
	}
	else
	{
		*preferred_width = cross + pad * 2.0f;
		*preferred_height = main + pad * 2.0f;
	}

	g_array_unref (lines);
	g_array_unref (single);
	g_array_unref (items);
}

/*
 * lrg_wrap_box_layout_children:
 *
 * Breaks the items into lines for the allocated main length, then for
 * each line: the start offset and gap from the justify mode, and each
 * item's cross position / length from the item alignment.
 */
static void
lrg_wrap_box_layout_children (LrgContainer *container)
{
	LrgWrapBox *self = LRG_WRAP_BOX (container);
	gfloat      pad = lrg_container_get_padding (container);
	gfloat      spacing = lrg_container_get_spacing (container);
	gfloat      room = main_room (self);
	GArray     *items;
	GArray     *lines;
	gfloat      cross_pos = pad;
	guint       li;

	if (self->in_layout)
		return;
	self->in_layout = TRUE;

	items = collect_items (self);
	lines = break_lines (self, items, room);
	self->n_lines = lines->len;

	/* The parent sized us for another line count: measure again soon */
	if (fabsf (lines_cross (self, lines) - self->measured_cross) > 0.5f)
		self->remeasure = TRUE;

	for (li = 0; li < lines->len; li++)
	{
		Line  *line = &g_array_index (lines, Line, li);
		gfloat free_len = MAX (0.0f, room - line->main);
		gfloat main_pos = pad;
		gfloat gap = spacing;
		guint  k;

		/* 1. Justification of this line */
		switch (self->justify)
		{
		case LRG_WRAP_JUSTIFY_CENTER:
			main_pos += free_len * 0.5f;
			break;
		case LRG_WRAP_JUSTIFY_END:
			main_pos += free_len;
			break;
		case LRG_WRAP_JUSTIFY_SPACE_BETWEEN:
			if (line->count > 1)
				gap += free_len / (gfloat) (line->count - 1);
			break;
		case LRG_WRAP_JUSTIFY_START:
		default:
			break;
		}

		/* 2. Items along the line, aligned across it */
		for (k = 0; k < line->count; k++)
		{
			Item  *it = &g_array_index (items, Item, line->first + k);
			gfloat c = cross_pos;
			gfloat cl = it->cross;

			switch (self->item_align)
			{
			case LRG_ALIGN_FILL:
				cl = line->cross;
				break;
			case LRG_ALIGN_CENTER:
				c += (line->cross - it->cross) * 0.5f;
				break;
			case LRG_ALIGN_END:
				c += line->cross - it->cross;
				break;
			case LRG_ALIGN_START:
			default:
				break;
			}

			if (horizontal (self))
				lrg_widget_place_in_slot (it->widget, roundf (main_pos), roundf (c), it->main, cl);
			else
				lrg_widget_place_in_slot (it->widget, roundf (c), roundf (main_pos), cl, it->main);
			main_pos += it->main + gap;
		}

		cross_pos += line->cross + self->line_spacing;
	}

	g_array_unref (lines);
	g_array_unref (items);
	self->in_layout = FALSE;
}

/*
 * lrg_wrap_box_step:
 *
 * The deferred half of the two-pass measure: after a layout found a
 * different line count, mark the box (and its ancestors) for layout
 * so the parent measures it again with the width it now has.
 *
 * Returns: %FALSE (nothing animates)
 */
static gboolean
lrg_wrap_box_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgWrapBox *self = LRG_WRAP_BOX (widget);

	if (self->remeasure)
	{
		self->remeasure = FALSE;
		lrg_widget_queue_layout (widget);
	}
	return FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_wrap_box_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgWrapBox *self = LRG_WRAP_BOX (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		g_value_set_enum (value, self->orientation);
		break;
	case PROP_LINE_SPACING:
		g_value_set_float (value, self->line_spacing);
		break;
	case PROP_JUSTIFY:
		g_value_set_enum (value, self->justify);
		break;
	case PROP_ITEM_ALIGN:
		g_value_set_enum (value, self->item_align);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_wrap_box_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgWrapBox *self = LRG_WRAP_BOX (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		lrg_wrap_box_set_orientation (self, (LrgOrientation) g_value_get_enum (value));
		break;
	case PROP_LINE_SPACING:
		lrg_wrap_box_set_line_spacing (self, g_value_get_float (value));
		break;
	case PROP_JUSTIFY:
		lrg_wrap_box_set_justify (self, (LrgWrapJustify) g_value_get_enum (value));
		break;
	case PROP_ITEM_ALIGN:
		lrg_wrap_box_set_item_align (self, (LrgAlign) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_wrap_box_class_init (LrgWrapBoxClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->get_property = lrg_wrap_box_get_property;
	object_class->set_property = lrg_wrap_box_set_property;

	widget_class->measure = lrg_wrap_box_measure;
	widget_class->step = lrg_wrap_box_step;

	container_class->layout_children = lrg_wrap_box_layout_children;

	/**
	 * LrgWrapBox:orientation:
	 *
	 * Horizontal: rows that wrap at the width; vertical: columns that
	 * wrap at the height.
	 */
	properties[PROP_ORIENTATION] =
		g_param_spec_enum ("orientation", "Orientation", "Flow direction",
		                   LRG_TYPE_ORIENTATION, LRG_ORIENTATION_HORIZONTAL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWrapBox:line-spacing:
	 *
	 * Gap between lines. The gap between items of a line is the
	 * container's #LrgContainer:spacing.
	 */
	properties[PROP_LINE_SPACING] =
		g_param_spec_float ("line-spacing", "Line spacing", "Gap between lines",
		                    0.0f, G_MAXFLOAT, 8.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWrapBox:justify:
	 *
	 * How each line shares its free length.
	 */
	properties[PROP_JUSTIFY] =
		g_param_spec_enum ("justify", "Justify", "Distribution of free space in a line",
		                   LRG_TYPE_WRAP_JUSTIFY, LRG_WRAP_JUSTIFY_START,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWrapBox:item-align:
	 *
	 * Alignment of items across their line.
	 */
	properties[PROP_ITEM_ALIGN] =
		g_param_spec_enum ("item-align", "Item align", "Alignment across a line",
		                   LRG_TYPE_ALIGN, LRG_ALIGN_START,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_wrap_box_init (LrgWrapBox *self)
{
	self->orientation = LRG_ORIENTATION_HORIZONTAL;
	self->line_spacing = 8.0f;
	self->justify = LRG_WRAP_JUSTIFY_START;
	self->item_align = LRG_ALIGN_START;
	lrg_container_set_spacing (LRG_CONTAINER (self), 8.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgWrapBox *
lrg_wrap_box_new (void)
{
	return g_object_new (LRG_TYPE_WRAP_BOX, NULL);
}

LrgOrientation
lrg_wrap_box_get_orientation (LrgWrapBox *self)
{
	g_return_val_if_fail (LRG_IS_WRAP_BOX (self), LRG_ORIENTATION_HORIZONTAL);
	return self->orientation;
}

void
lrg_wrap_box_set_orientation (LrgWrapBox     *self,
                              LrgOrientation  orientation)
{
	g_return_if_fail (LRG_IS_WRAP_BOX (self));

	if (self->orientation == orientation)
		return;
	self->orientation = orientation;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

gfloat
lrg_wrap_box_get_line_spacing (LrgWrapBox *self)
{
	g_return_val_if_fail (LRG_IS_WRAP_BOX (self), 0.0f);
	return self->line_spacing;
}

void
lrg_wrap_box_set_line_spacing (LrgWrapBox *self,
                               gfloat      spacing)
{
	g_return_if_fail (LRG_IS_WRAP_BOX (self));

	spacing = MAX (0.0f, spacing);
	if (self->line_spacing == spacing)
		return;
	self->line_spacing = spacing;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_SPACING]);
}

LrgWrapJustify
lrg_wrap_box_get_justify (LrgWrapBox *self)
{
	g_return_val_if_fail (LRG_IS_WRAP_BOX (self), LRG_WRAP_JUSTIFY_START);
	return self->justify;
}

void
lrg_wrap_box_set_justify (LrgWrapBox     *self,
                          LrgWrapJustify  justify)
{
	g_return_if_fail (LRG_IS_WRAP_BOX (self));

	if (self->justify == justify)
		return;
	self->justify = justify;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_JUSTIFY]);
}

LrgAlign
lrg_wrap_box_get_item_align (LrgWrapBox *self)
{
	g_return_val_if_fail (LRG_IS_WRAP_BOX (self), LRG_ALIGN_START);
	return self->item_align;
}

void
lrg_wrap_box_set_item_align (LrgWrapBox *self,
                             LrgAlign    align)
{
	g_return_if_fail (LRG_IS_WRAP_BOX (self));

	if (self->item_align == align)
		return;
	self->item_align = align;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM_ALIGN]);
}

guint
lrg_wrap_box_get_n_lines (LrgWrapBox *self)
{
	g_return_val_if_fail (LRG_IS_WRAP_BOX (self), 0);
	return self->n_lines;
}
