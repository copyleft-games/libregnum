/* lrg-anchor-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnchorLayout: children placed freely by anchors, offsets and a pivot.
 *
 * Per-child placement lives in an #LrgAnchorData attached to the child
 * as object data (keyed by a quark), so children are added with the
 * plain lrg_container_add_child() and configured afterwards, which
 * keeps the layout friendly to the declarative UI loader.
 *
 * Stacking: the canvas hit-tests a container's children from the last
 * to the first and the container paints them first to last, so the
 * child list itself is the stacking order. The layout keeps that list
 * sorted by z-order (stably, so equal z-orders keep their insertion
 * order) by taking children out and putting them back in order.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-anchor-layout.h"
#include "lrg-widget-private.h"
#include "../lrg-log.h"

struct _LrgAnchorLayout
{
	LrgContainer parent_instance;

	/* TRUE while the child list is being re-sorted: the container calls
	 * layout_children() on every remove / add, which must not recurse */
	gboolean     sorting;
};

G_DEFINE_FINAL_TYPE (LrgAnchorLayout, lrg_anchor_layout, LRG_TYPE_CONTAINER)

G_DEFINE_BOXED_TYPE (LrgAnchorData, lrg_anchor_data, lrg_anchor_data_copy, lrg_anchor_data_free)

/* Object-data key holding a child's LrgAnchorData */
static GQuark anchor_data_quark;

/* ==========================================================================
 * LrgAnchorPreset
 * ========================================================================== */

GType
lrg_anchor_preset_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ANCHOR_PRESET_TOP_LEFT, "LRG_ANCHOR_PRESET_TOP_LEFT", "top-left" },
			{ LRG_ANCHOR_PRESET_TOP, "LRG_ANCHOR_PRESET_TOP", "top" },
			{ LRG_ANCHOR_PRESET_TOP_RIGHT, "LRG_ANCHOR_PRESET_TOP_RIGHT", "top-right" },
			{ LRG_ANCHOR_PRESET_LEFT, "LRG_ANCHOR_PRESET_LEFT", "left" },
			{ LRG_ANCHOR_PRESET_CENTER, "LRG_ANCHOR_PRESET_CENTER", "center" },
			{ LRG_ANCHOR_PRESET_RIGHT, "LRG_ANCHOR_PRESET_RIGHT", "right" },
			{ LRG_ANCHOR_PRESET_BOTTOM_LEFT, "LRG_ANCHOR_PRESET_BOTTOM_LEFT", "bottom-left" },
			{ LRG_ANCHOR_PRESET_BOTTOM, "LRG_ANCHOR_PRESET_BOTTOM", "bottom" },
			{ LRG_ANCHOR_PRESET_BOTTOM_RIGHT, "LRG_ANCHOR_PRESET_BOTTOM_RIGHT", "bottom-right" },
			{ LRG_ANCHOR_PRESET_TOP_STRETCH, "LRG_ANCHOR_PRESET_TOP_STRETCH", "top-stretch" },
			{ LRG_ANCHOR_PRESET_MIDDLE_STRETCH, "LRG_ANCHOR_PRESET_MIDDLE_STRETCH", "middle-stretch" },
			{ LRG_ANCHOR_PRESET_BOTTOM_STRETCH, "LRG_ANCHOR_PRESET_BOTTOM_STRETCH", "bottom-stretch" },
			{ LRG_ANCHOR_PRESET_LEFT_STRETCH, "LRG_ANCHOR_PRESET_LEFT_STRETCH", "left-stretch" },
			{ LRG_ANCHOR_PRESET_CENTER_STRETCH, "LRG_ANCHOR_PRESET_CENTER_STRETCH", "center-stretch" },
			{ LRG_ANCHOR_PRESET_RIGHT_STRETCH, "LRG_ANCHOR_PRESET_RIGHT_STRETCH", "right-stretch" },
			{ LRG_ANCHOR_PRESET_FILL, "LRG_ANCHOR_PRESET_FILL", "fill" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgAnchorPreset"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * LrgAnchorData
 * ========================================================================== */

void
lrg_anchor_data_init (LrgAnchorData *self)
{
	g_return_if_fail (self != NULL);

	memset (self, 0, sizeof (LrgAnchorData));
	self->auto_size = TRUE;
}

LrgAnchorData *
lrg_anchor_data_copy (const LrgAnchorData *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgAnchorData *) g_memdup2 (self, sizeof (LrgAnchorData));
}

void
lrg_anchor_data_free (LrgAnchorData *self)
{
	g_free (self);
}

void
lrg_anchor_data_set_preset (LrgAnchorData   *self,
                            LrgAnchorPreset  preset)
{
	/*
	 * Table: min_x, min_y, max_x, max_y, align_x, align_y. Pinned presets
	 * put the pivot on the same point as the anchor; stretched ones only
	 * have a meaningful pivot on the unstretched axis.
	 */
	static const gfloat table[][6] = {
		/* TOP_LEFT       */ { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		/* TOP            */ { 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f },
		/* TOP_RIGHT      */ { 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f },
		/* LEFT           */ { 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f },
		/* CENTER         */ { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },
		/* RIGHT          */ { 1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f },
		/* BOTTOM_LEFT    */ { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
		/* BOTTOM         */ { 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f },
		/* BOTTOM_RIGHT   */ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
		/* TOP_STRETCH    */ { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
		/* MIDDLE_STRETCH */ { 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f },
		/* BOTTOM_STRETCH */ { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f },
		/* LEFT_STRETCH   */ { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		/* CENTER_STRETCH */ { 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f },
		/* RIGHT_STRETCH  */ { 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f },
		/* FILL           */ { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }
	};
	const gfloat *row;

	g_return_if_fail (self != NULL);
	g_return_if_fail ((guint) preset < G_N_ELEMENTS (table));

	row = table[preset];
	self->min_x = row[0];
	self->min_y = row[1];
	self->max_x = row[2];
	self->max_y = row[3];
	self->align_x = row[4];
	self->align_y = row[5];
}

/*
 * anchor_axis:
 *
 * The anchor math for one axis (Slate SConstraintCanvas):
 *  - stretched (amax > amin): the near edge sits @near past the near
 *    anchor and the far edge @far before the far anchor;
 *  - pinned: the pivot point (@align of the length) sits @near past the
 *    anchor; the length is @far when positive and not auto-sizing,
 *    otherwise the preferred length.
 */
static void
anchor_axis (gfloat    extent,
             gfloat    amin,
             gfloat    amax,
             gfloat    near,
             gfloat    far,
             gfloat    align,
             gboolean  auto_size,
             gfloat    preferred,
             gfloat   *pos,
             gfloat   *len)
{
	if (amax > amin)
	{
		gfloat start = extent * amin + near;
		gfloat end = extent * amax - far;

		*pos = start;
		*len = MAX (0.0f, end - start);
	}
	else
	{
		gfloat l = (!auto_size && far > 0.0f) ? far : preferred;

		*pos = extent * amin + near - align * l;
		*len = MAX (0.0f, l);
	}
}

void
lrg_anchor_compute_rect (const LrgAnchorData *data,
                         gfloat               width,
                         gfloat               height,
                         gfloat               preferred_width,
                         gfloat               preferred_height,
                         gfloat              *x,
                         gfloat              *y,
                         gfloat              *out_width,
                         gfloat              *out_height)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (x != NULL && y != NULL && out_width != NULL && out_height != NULL);

	anchor_axis (width, data->min_x, data->max_x, data->left, data->right, data->align_x,
	             data->auto_size, preferred_width, x, out_width);
	anchor_axis (height, data->min_y, data->max_y, data->top, data->bottom, data->align_y,
	             data->auto_size, preferred_height, y, out_height);
}

/* ==========================================================================
 * Per-child data
 * ========================================================================== */

/*
 * child_data:
 *
 * The child's placement, created with the defaults on first use so
 * every child of the layout always has one.
 */
static LrgAnchorData *
child_data (LrgWidget *child)
{
	LrgAnchorData *data = g_object_get_qdata (G_OBJECT (child), anchor_data_quark);

	if (data == NULL)
	{
		data = g_new (LrgAnchorData, 1);
		lrg_anchor_data_init (data);
		g_object_set_qdata_full (G_OBJECT (child), anchor_data_quark, data,
		                         (GDestroyNotify) lrg_anchor_data_free);
	}
	return data;
}

/* z_order_of: the child's z-order without creating data for it */
static gint
z_order_of (LrgWidget *child)
{
	LrgAnchorData *data = g_object_get_qdata (G_OBJECT (child), anchor_data_quark);

	return data != NULL ? data->z_order : 0;
}

/* ==========================================================================
 * Stacking order
 * ========================================================================== */

/* compare_z: GCompareFunc for g_list_sort (a stable merge sort) */
static gint
compare_z (gconstpointer a,
           gconstpointer b)
{
	gint za = z_order_of (LRG_WIDGET ((gpointer) a));
	gint zb = z_order_of (LRG_WIDGET ((gpointer) b));

	return za < zb ? -1 : (za > zb ? 1 : 0);
}

/*
 * sort_children:
 *
 * Makes the container's child list follow the z-order. Nothing happens
 * when it already does (the common case: every child at 0). Otherwise
 * every child is taken out and added back in sorted order through the
 * public container API, holding a reference so none is finalised.
 */
static void
sort_children (LrgAnchorLayout *self)
{
	LrgContainer *container = LRG_CONTAINER (self);
	GList        *children = lrg_container_get_children (container);
	GList        *sorted;
	GList        *a;
	GList        *b;
	gboolean      same = TRUE;

	if (self->sorting || children == NULL)
		return;

	/* g_list_sort is a stable merge sort: equal z-orders keep their
	 * current (insertion) order */
	sorted = g_list_sort (g_list_copy (children), compare_z);
	for (a = children, b = sorted; a != NULL && b != NULL; a = a->next, b = b->next)
		if (a->data != b->data)
		{
			same = FALSE;
			break;
		}

	if (!same)
	{
		GList *l;

		self->sorting = TRUE;
		g_list_foreach (sorted, (GFunc) (void (*) (void)) g_object_ref, NULL);
		lrg_container_remove_all (container);
		for (l = sorted; l != NULL; l = l->next)
			lrg_container_add_child (container, LRG_WIDGET (l->data));
		g_list_foreach (sorted, (GFunc) (void (*) (void)) g_object_unref, NULL);
		self->sorting = FALSE;
	}
	g_list_free (sorted);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_anchor_layout_layout_children (LrgContainer *container)
{
	LrgAnchorLayout *self = LRG_ANCHOR_LAYOUT (container);
	LrgWidget       *widget = LRG_WIDGET (container);
	gfloat           pad;
	gfloat           w;
	gfloat           h;
	GList           *l;

	/* Mid-sort the list is incomplete; the pass after the sort lays out */
	if (self->sorting)
		return;

	/* A newly added child (z 0) may sit after higher z-orders */
	sort_children (self);

	pad = lrg_container_get_padding (container);
	w = MAX (0.0f, lrg_widget_get_width (widget) - pad * 2.0f);
	h = MAX (0.0f, lrg_widget_get_height (widget) - pad * 2.0f);

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget     *child = LRG_WIDGET (l->data);
		LrgAnchorData *data;
		gfloat         pw;
		gfloat         ph;
		gfloat         cx;
		gfloat         cy;
		gfloat         cw;
		gfloat         ch;

		if (!lrg_widget_get_visible (child))
			continue;
		data = child_data (child);

		/* The anchor rectangle is the child's outer box: its margin is
		 * part of the preferred size and place_in_slot() takes it off */
		_lrg_widget_outer_size (child, &pw, &ph);
		lrg_anchor_compute_rect (data, w, h, pw, ph, &cx, &cy, &cw, &ch);

		/* Right to left needs nothing here: lrg_widget_allocate()
		 * mirrors the rectangle inside the layout, which mirrors the
		 * anchors, offsets and pivot together */

		lrg_widget_place_in_slot (child, pad + cx, pad + cy, cw, ch);
	}
}

/*
 * required_extent:
 *
 * How long an axis must be so a pinned child lies fully inside: with
 * the anchor at fraction a, the child spans
 * [a * E + p - align * l, a * E + p + (1 - align) * l], which must sit
 * within [0, E]. Stretched children need their margins plus their
 * preferred length between the anchors.
 */
static gfloat
required_extent (gfloat   amin,
                 gfloat   amax,
                 gfloat   near,
                 gfloat   far,
                 gfloat   align,
                 gboolean auto_size,
                 gfloat   preferred)
{
	gfloat need = 0.0f;
	gfloat l;

	if (amax > amin)
	{
		gfloat span = amax - amin;

		/* (amax - amin) * E - near - far >= preferred */
		return MAX (0.0f, (preferred + near + far) / span);
	}

	l = (!auto_size && far > 0.0f) ? far : preferred;

	/* Far side: (1 - a) * E >= p + (1 - align) * l */
	if (amin < 1.0f)
		need = MAX (need, (near + (1.0f - align) * l) / (1.0f - amin));
	/* Near side: a * E >= align * l - p */
	if (amin > 0.0f)
		need = MAX (need, (align * l - near) / amin);
	return need;
}

static void
lrg_anchor_layout_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
	LrgContainer *container = LRG_CONTAINER (widget);
	gfloat        pad = lrg_container_get_padding (container);
	gfloat        w = 0.0f;
	gfloat        h = 0.0f;
	GList        *l;

	/* Big enough that every visible child lies inside */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget     *child = LRG_WIDGET (l->data);
		LrgAnchorData *data;
		gfloat         pw;
		gfloat         ph;

		if (!lrg_widget_get_visible (child))
			continue;
		data = child_data (child);
		_lrg_widget_outer_size (child, &pw, &ph);
		w = MAX (w, required_extent (data->min_x, data->max_x, data->left, data->right,
		                             data->align_x, data->auto_size, pw));
		h = MAX (h, required_extent (data->min_y, data->max_y, data->top, data->bottom,
		                             data->align_y, data->auto_size, ph));
	}

	*preferred_width = ceilf (w) + pad * 2.0f;
	*preferred_height = ceilf (h) + pad * 2.0f;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_anchor_layout_class_init (LrgAnchorLayoutClass *klass)
{
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	anchor_data_quark = g_quark_from_static_string ("lrg-anchor-layout-data");

	/* Painting is the container default: the child list is already in
	 * z-order, children outside the bounds are not clipped */
	widget_class->measure = lrg_anchor_layout_measure;
	container_class->layout_children = lrg_anchor_layout_layout_children;
}

static void
lrg_anchor_layout_init (LrgAnchorLayout *self)
{
	/* Empty HUD areas let clicks through to the game */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnchorLayout *
lrg_anchor_layout_new (void)
{
	return g_object_new (LRG_TYPE_ANCHOR_LAYOUT, NULL);
}

/* relayout: apply a placement change */
static void
relayout (LrgAnchorLayout *self)
{
	lrg_container_layout_children (LRG_CONTAINER (self));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

#define CHECK_CHILD(self, child)                                                   \
	g_return_if_fail (LRG_IS_ANCHOR_LAYOUT (self));                                \
	g_return_if_fail (LRG_IS_WIDGET (child));                                      \
	g_return_if_fail (lrg_widget_get_parent (child) == LRG_CONTAINER (self))

void
lrg_anchor_layout_add (LrgAnchorLayout *self,
                       LrgWidget       *child,
                       LrgAnchorPreset  preset,
                       gfloat           offset_x,
                       gfloat           offset_y)
{
	LrgAnchorData *data;

	g_return_if_fail (LRG_IS_ANCHOR_LAYOUT (self));
	g_return_if_fail (LRG_IS_WIDGET (child));
	g_return_if_fail (lrg_widget_get_parent (child) == NULL);

	/* Configure before adding so the first layout is already right */
	data = child_data (child);
	lrg_anchor_data_set_preset (data, preset);
	data->auto_size = TRUE;

	/* Stretched axes: equal margins on both sides; pinned axes: a
	 * positive offset moves inwards from the right / bottom */
	if (data->max_x > data->min_x)
	{
		data->left = offset_x;
		data->right = offset_x;
	}
	else
	{
		data->left = data->min_x >= 1.0f ? -offset_x : offset_x;
		data->right = 0.0f;
	}
	if (data->max_y > data->min_y)
	{
		data->top = offset_y;
		data->bottom = offset_y;
	}
	else
	{
		data->top = data->min_y >= 1.0f ? -offset_y : offset_y;
		data->bottom = 0.0f;
	}

	lrg_container_add_child (LRG_CONTAINER (self), child);
}

void
lrg_anchor_layout_get_child_data (LrgAnchorLayout *self,
                                  LrgWidget       *child,
                                  LrgAnchorData   *data)
{
	g_return_if_fail (data != NULL);
	CHECK_CHILD (self, child);

	*data = *child_data (child);
}

void
lrg_anchor_layout_set_child_data (LrgAnchorLayout     *self,
                                  LrgWidget           *child,
                                  const LrgAnchorData *data)
{
	g_return_if_fail (data != NULL);
	CHECK_CHILD (self, child);

	*child_data (child) = *data;
	relayout (self);
}

void
lrg_anchor_layout_set_child_preset (LrgAnchorLayout *self,
                                    LrgWidget       *child,
                                    LrgAnchorPreset  preset)
{
	CHECK_CHILD (self, child);

	lrg_anchor_data_set_preset (child_data (child), preset);
	relayout (self);
}

void
lrg_anchor_layout_set_child_anchors (LrgAnchorLayout *self,
                                     LrgWidget       *child,
                                     gfloat           min_x,
                                     gfloat           min_y,
                                     gfloat           max_x,
                                     gfloat           max_y)
{
	LrgAnchorData *data;

	CHECK_CHILD (self, child);

	data = child_data (child);
	data->min_x = min_x;
	data->min_y = min_y;
	data->max_x = MAX (min_x, max_x);
	data->max_y = MAX (min_y, max_y);
	relayout (self);
}

void
lrg_anchor_layout_set_child_offsets (LrgAnchorLayout *self,
                                     LrgWidget       *child,
                                     gfloat           left,
                                     gfloat           top,
                                     gfloat           right,
                                     gfloat           bottom)
{
	LrgAnchorData *data;

	CHECK_CHILD (self, child);

	data = child_data (child);
	data->left = left;
	data->top = top;
	data->right = right;
	data->bottom = bottom;
	relayout (self);
}

void
lrg_anchor_layout_set_child_alignment (LrgAnchorLayout *self,
                                       LrgWidget       *child,
                                       gfloat           align_x,
                                       gfloat           align_y)
{
	LrgAnchorData *data;

	CHECK_CHILD (self, child);

	data = child_data (child);
	data->align_x = align_x;
	data->align_y = align_y;
	relayout (self);
}

void
lrg_anchor_layout_set_child_auto_size (LrgAnchorLayout *self,
                                       LrgWidget       *child,
                                       gboolean         auto_size)
{
	CHECK_CHILD (self, child);

	child_data (child)->auto_size = !!auto_size;
	relayout (self);
}

void
lrg_anchor_layout_set_child_z_order (LrgAnchorLayout *self,
                                     LrgWidget       *child,
                                     gint             z_order)
{
	LrgAnchorData *data;

	CHECK_CHILD (self, child);

	data = child_data (child);
	if (data->z_order == z_order)
		return;
	data->z_order = z_order;
	sort_children (self);
	relayout (self);
}

gint
lrg_anchor_layout_get_child_z_order (LrgAnchorLayout *self,
                                     LrgWidget       *child)
{
	g_return_val_if_fail (LRG_IS_ANCHOR_LAYOUT (self), 0);
	g_return_val_if_fail (LRG_IS_WIDGET (child), 0);
	g_return_val_if_fail (lrg_widget_get_parent (child) == LRG_CONTAINER (self), 0);

	return z_order_of (child);
}
