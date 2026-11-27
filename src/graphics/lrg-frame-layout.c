/* lrg-frame-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameLayout - named element boxes on a still frame, with overlap,
 * frame and safe-area checks and a debug overlay.
 *
 * Boxes keep how they were placed (an anchor on the safe area, a side
 * of another box, or an explicit rectangle), not only where, so a box
 * placed beside another follows it and anchored boxes follow the safe
 * area.  The rectangles are resolved lazily (once per change) in one
 * pass that also finds placements against missing boxes and loops.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-frame-layout.h"
#include "lrg-image-canvas.h"
#include "../hud/lrg-hud-layout.h"

/* Slack, in pixels, for rounding in percent margins and float sums:
 * two boxes overlap only when they share more than this much in both
 * directions, and a box leaves the frame only when it reaches past an
 * edge by more than this. */
#define FRAME_EPSILON (1e-3f)

/* How a box was placed. */
typedef enum
{
	PLACE_ANCHOR,
	PLACE_RELATIVE,
	PLACE_RECT
} PlaceKind;

/* Why a box has no rectangle after resolving. */
typedef enum
{
	UNRESOLVED_NONE,
	UNRESOLVED_MISSING,	/* the target box does not exist */
	UNRESOLVED_TARGET,	/* the target box has no rectangle itself */
	UNRESOLVED_LOOP		/* the placements loop back to this box */
} Unresolved;

typedef struct
{
	gchar        *name;
	PlaceKind     kind;

	/* PLACE_ANCHOR */
	LrgHudAnchor  anchor;
	gfloat        offset_x;
	gfloat        offset_y;

	/* PLACE_RELATIVE */
	gchar        *of;
	LrgFrameSide  side;
	LrgFrameAlign align;
	gfloat        gap;

	/* every kind: the size (PLACE_RECT also its position) */
	GrlRectangle  rect;

	/* filled in by resolve_all() */
	GrlRectangle  resolved;
	Unresolved    unresolved;
	guint         visit;	/* 0 unvisited, 1 on the stack, 2 done */
} Box;

/* One problem found by collect_problems(). */
typedef struct
{
	LrgFrameLayoutError code;
	gchar              *message;
	guint               box_a;	/* index into self->boxes */
	guint               box_b;	/* G_MAXUINT unless an overlap */
	GrlRectangle        shared;	/* overlaps only: the shared area */
} Problem;

struct _LrgFrameLayout
{
	GObject     parent_instance;

	gfloat      width;
	gfloat      height;
	gfloat      margin_left;
	gfloat      margin_top;
	gfloat      margin_right;
	gfloat      margin_bottom;

	GPtrArray  *boxes;		/* (element-type Box), insertion order */
	GHashTable *allowed;		/* "a\037b" (sorted names) -> itself */
	gboolean    dirty;		/* rectangles need resolving */
};

G_DEFINE_FINAL_TYPE (LrgFrameLayout, lrg_frame_layout, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-frame-layout-error-quark, lrg_frame_layout_error)

GType
lrg_frame_layout_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRAME_LAYOUT_ERROR_OVERLAP, "LRG_FRAME_LAYOUT_ERROR_OVERLAP", "overlap" },
			{ LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME, "LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME", "out-of-frame" },
			{ LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA, "LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA", "out-of-safe-area" },
			{ LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX, "LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX", "unknown-box" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFrameLayoutError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * Boxes
 * ========================================================================== */

static void
box_free (gpointer data)
{
	Box *box = data;

	g_free (box->name);
	g_free (box->of);
	g_free (box);
}

/* A finite value, or 0. */
static gfloat
finite_or_zero (gfloat v)
{
	return isfinite (v) ? v : 0.0f;
}

/* A size: finite and not negative, or 0. */
static gfloat
size_or_zero (gfloat v)
{
	return (isfinite (v) && v > 0.0f) ? v : 0.0f;
}

/* The index of the box called @name, or -1. */
static gint
find_box (LrgFrameLayout *self,
          const gchar    *name)
{
	guint i;

	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);

		if (g_strcmp0 (box->name, name) == 0)
			return (gint)i;
	}
	return -1;
}

/*
 * Returns the box called @name ready to be (re)placed: a new one at the
 * end of the order, or the existing one with its old placement cleared
 * so it keeps its position in the order.
 */
static Box *
take_box (LrgFrameLayout *self,
          const gchar    *name)
{
	gint index = find_box (self, name);
	Box *box;

	self->dirty = TRUE;
	if (index >= 0)
	{
		box = g_ptr_array_index (self->boxes, index);
		g_clear_pointer (&box->of, g_free);
		return box;
	}
	box = g_new0 (Box, 1);
	box->name = g_strdup (name);
	g_ptr_array_add (self->boxes, box);
	return box;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_frame_layout_finalize (GObject *object)
{
	LrgFrameLayout *self = LRG_FRAME_LAYOUT (object);

	g_clear_pointer (&self->boxes, g_ptr_array_unref);
	g_clear_pointer (&self->allowed, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_frame_layout_parent_class)->finalize (object);
}

static void
lrg_frame_layout_class_init (LrgFrameLayoutClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_frame_layout_finalize;
}

static void
lrg_frame_layout_init (LrgFrameLayout *self)
{
	self->width = 1.0f;
	self->height = 1.0f;
	self->boxes = g_ptr_array_new_with_free_func (box_free);
	self->allowed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

LrgFrameLayout *
lrg_frame_layout_new (gfloat width,
                      gfloat height)
{
	LrgFrameLayout *self = g_object_new (LRG_TYPE_FRAME_LAYOUT, NULL);

	self->width = (isfinite (width) && width >= 1.0f) ? width : 1.0f;
	self->height = (isfinite (height) && height >= 1.0f) ? height : 1.0f;
	return self;
}

gfloat
lrg_frame_layout_get_width (LrgFrameLayout *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), 0.0f);
	return self->width;
}

gfloat
lrg_frame_layout_get_height (LrgFrameLayout *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), 0.0f);
	return self->height;
}

/* ==========================================================================
 * Safe area
 * ========================================================================== */

void
lrg_frame_layout_set_safe_area_margins (LrgFrameLayout *self,
                                        gfloat          left,
                                        gfloat          top,
                                        gfloat          right,
                                        gfloat          bottom)
{
	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));

	self->margin_left = size_or_zero (left);
	self->margin_top = size_or_zero (top);
	self->margin_right = size_or_zero (right);
	self->margin_bottom = size_or_zero (bottom);
	self->dirty = TRUE;
}

void
lrg_frame_layout_set_safe_area_percent (LrgFrameLayout *self,
                                        gfloat          fraction)
{
	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));

	fraction = isfinite (fraction) ? CLAMP (fraction, 0.0f, 0.5f) : 0.0f;
	lrg_frame_layout_set_safe_area_margins (self,
	                                        self->width * fraction,
	                                        self->height * fraction,
	                                        self->width * fraction,
	                                        self->height * fraction);
}

/*
 * The frame minus the margins.  When the margins of one axis together
 * exceed the frame, the safe area collapses to zero size at the point
 * where they meet (shared in proportion to the two margins).
 */
static void
safe_area (LrgFrameLayout *self,
           GrlRectangle   *out)
{
	gfloat horizontal = self->margin_left + self->margin_right;
	gfloat vertical = self->margin_top + self->margin_bottom;

	if (horizontal <= self->width)
	{
		out->x = self->margin_left;
		out->width = self->width - horizontal;
	}
	else
	{
		out->x = self->width * self->margin_left / horizontal;
		out->width = 0.0f;
	}

	if (vertical <= self->height)
	{
		out->y = self->margin_top;
		out->height = self->height - vertical;
	}
	else
	{
		out->y = self->height * self->margin_top / vertical;
		out->height = 0.0f;
	}
}

void
lrg_frame_layout_get_safe_area (LrgFrameLayout *self,
                                GrlRectangle   *out)
{
	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (out != NULL);

	safe_area (self, out);
}

/* ==========================================================================
 * Placing boxes
 * ========================================================================== */

void
lrg_frame_layout_add_box (LrgFrameLayout *self,
                          const gchar    *name,
                          LrgHudAnchor    anchor,
                          gfloat          offset_x,
                          gfloat          offset_y,
                          gfloat          width,
                          gfloat          height)
{
	Box *box;

	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (name != NULL && name[0] != '\0');

	box = take_box (self, name);
	box->kind = PLACE_ANCHOR;
	box->anchor = anchor;
	box->offset_x = finite_or_zero (offset_x);
	box->offset_y = finite_or_zero (offset_y);
	box->rect.x = 0.0f;
	box->rect.y = 0.0f;
	box->rect.width = size_or_zero (width);
	box->rect.height = size_or_zero (height);
}

void
lrg_frame_layout_add_box_relative (LrgFrameLayout *self,
                                   const gchar    *name,
                                   const gchar    *of,
                                   LrgFrameSide    side,
                                   gfloat          gap,
                                   gfloat          width,
                                   gfloat          height,
                                   LrgFrameAlign   align)
{
	Box *box;

	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (name != NULL && name[0] != '\0');
	g_return_if_fail (of != NULL);

	box = take_box (self, name);
	box->kind = PLACE_RELATIVE;
	box->of = g_strdup (of);
	box->side = side;
	box->align = align;
	box->gap = finite_or_zero (gap);
	box->rect.x = 0.0f;
	box->rect.y = 0.0f;
	box->rect.width = size_or_zero (width);
	box->rect.height = size_or_zero (height);
}

void
lrg_frame_layout_set_box_rect (LrgFrameLayout     *self,
                               const gchar        *name,
                               const GrlRectangle *rect)
{
	Box *box;

	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (name != NULL && name[0] != '\0');
	g_return_if_fail (rect != NULL);

	box = take_box (self, name);
	box->kind = PLACE_RECT;
	box->rect.x = finite_or_zero (rect->x);
	box->rect.y = finite_or_zero (rect->y);
	box->rect.width = size_or_zero (rect->width);
	box->rect.height = size_or_zero (rect->height);
}

gboolean
lrg_frame_layout_remove_box (LrgFrameLayout *self,
                             const gchar    *name)
{
	gint index;

	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	index = find_box (self, name);
	if (index < 0)
		return FALSE;
	g_ptr_array_remove_index (self->boxes, (guint)index);
	self->dirty = TRUE;
	return TRUE;
}

GPtrArray *
lrg_frame_layout_get_names (LrgFrameLayout *self)
{
	GPtrArray *names;
	guint i;

	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), NULL);

	names = g_ptr_array_sized_new (self->boxes->len);
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);

		g_ptr_array_add (names, box->name);
	}
	return names;
}

/* The key of an allowed pair: the two names sorted, joined by a unit
 * separator (a byte names are unlikely to hold). */
static gchar *
pair_key (const gchar *a,
          const gchar *b)
{
	if (strcmp (a, b) > 0)
	{
		const gchar *t = a;

		a = b;
		b = t;
	}
	return g_strconcat (a, "\037", b, NULL);
}

void
lrg_frame_layout_allow_overlap (LrgFrameLayout *self,
                                const gchar    *a,
                                const gchar    *b)
{
	gchar *key;

	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (a != NULL && b != NULL);

	key = pair_key (a, b);
	g_hash_table_add (self->allowed, key);
}

/* ==========================================================================
 * Resolving rectangles
 * ========================================================================== */

/*
 * Puts @box on @side of @target and lines it up by its alignment.  Along
 * the shared side START/CENTER/END pick the left, centre or right (above
 * and below) or the top, centre or bottom (left and right) edges.
 */
static void
place_beside (Box                *box,
              const GrlRectangle *target)
{
	gfloat w = box->rect.width;
	gfloat h = box->rect.height;
	gfloat along;

	switch (box->side)
	{
	case LRG_FRAME_SIDE_ABOVE:
	case LRG_FRAME_SIDE_BELOW:
	default:
		/* --- horizontal alignment along the top or bottom edge --- */
		if (box->align == LRG_FRAME_ALIGN_END)
			along = target->x + target->width - w;
		else if (box->align == LRG_FRAME_ALIGN_CENTER)
			along = target->x + (target->width - w) * 0.5f;
		else
			along = target->x;
		box->resolved.x = along;
		if (box->side == LRG_FRAME_SIDE_BELOW)
			box->resolved.y = target->y + target->height + box->gap;
		else
			box->resolved.y = target->y - box->gap - h;
		break;

	case LRG_FRAME_SIDE_LEFT:
	case LRG_FRAME_SIDE_RIGHT:
		/* --- vertical alignment along the left or right edge --- */
		if (box->align == LRG_FRAME_ALIGN_END)
			along = target->y + target->height - h;
		else if (box->align == LRG_FRAME_ALIGN_CENTER)
			along = target->y + (target->height - h) * 0.5f;
		else
			along = target->y;
		box->resolved.y = along;
		if (box->side == LRG_FRAME_SIDE_RIGHT)
			box->resolved.x = target->x + target->width + box->gap;
		else
			box->resolved.x = target->x - box->gap - w;
		break;
	}
	box->resolved.width = w;
	box->resolved.height = h;
}

/*
 * Resolves one box, first resolving the box it is placed against.  The
 * visit marks find loops: meeting a box that is still on the stack means
 * the placements lead back to it.
 */
static void
resolve_box (LrgFrameLayout     *self,
             Box                *box,
             const GrlRectangle *safe)
{
	gint target_index;
	Box *target;

	if (box->visit == 2)
		return;
	box->visit = 1;
	box->unresolved = UNRESOLVED_NONE;

	switch (box->kind)
	{
	case PLACE_RECT:
		box->resolved = box->rect;
		break;

	case PLACE_ANCHOR:
		/* --- the HUD's nine-point placement, on the safe area --- */
		lrg_hud_layout_resolve (box->anchor, box->offset_x, box->offset_y,
		                        box->rect.width, box->rect.height,
		                        safe->width, safe->height,
		                        &box->resolved.x, &box->resolved.y);
		box->resolved.x += safe->x;
		box->resolved.y += safe->y;
		box->resolved.width = box->rect.width;
		box->resolved.height = box->rect.height;
		break;

	case PLACE_RELATIVE:
	default:
		/* --- beside another box, which must be placed first --- */
		target_index = find_box (self, box->of);
		if (target_index < 0)
		{
			box->unresolved = UNRESOLVED_MISSING;
			break;
		}
		target = g_ptr_array_index (self->boxes, target_index);
		if (target->visit == 1)
		{
			box->unresolved = UNRESOLVED_LOOP;
			break;
		}
		resolve_box (self, target, safe);
		if (target->unresolved != UNRESOLVED_NONE)
		{
			box->unresolved = UNRESOLVED_TARGET;
			break;
		}
		place_beside (box, &target->resolved);
		break;
	}

	if (box->unresolved != UNRESOLVED_NONE)
		memset (&box->resolved, 0, sizeof (box->resolved));
	box->visit = 2;
}

/* Resolves every box when anything changed since the last time. */
static void
resolve_all (LrgFrameLayout *self)
{
	GrlRectangle safe;
	guint i;

	if (!self->dirty)
		return;

	safe_area (self, &safe);
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);

		box->visit = 0;
	}
	for (i = 0; i < self->boxes->len; i++)
		resolve_box (self, g_ptr_array_index (self->boxes, i), &safe);
	self->dirty = FALSE;
}

gboolean
lrg_frame_layout_get_box (LrgFrameLayout *self,
                          const gchar    *name,
                          GrlRectangle   *out)
{
	gint index;
	Box *box;

	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), FALSE);

	if (out != NULL)
		memset (out, 0, sizeof (*out));
	if (name == NULL)
		return FALSE;
	index = find_box (self, name);
	if (index < 0)
		return FALSE;

	resolve_all (self);
	box = g_ptr_array_index (self->boxes, index);
	if (box->unresolved != UNRESOLVED_NONE)
		return FALSE;
	if (out != NULL)
		*out = box->resolved;
	return TRUE;
}

/* ==========================================================================
 * Checks
 * ========================================================================== */

/* Appends a rectangle as "(x=.., y=.., w=.., h=..)". */
static void
append_rect (GString            *s,
             const GrlRectangle *r)
{
	g_string_append_printf (s, "(x=%g, y=%g, w=%g, h=%g)",
	                        (gdouble)r->x, (gdouble)r->y,
	                        (gdouble)r->width, (gdouble)r->height);
}

/*
 * How far @r reaches past each edge of @area, appended as
 * "left 5, bottom 12"; returns whether it reaches past any edge.
 */
static gboolean
append_overhang (GString            *s,
                 const GrlRectangle *r,
                 const GrlRectangle *area)
{
	gfloat over[4];
	const gchar *edge[4] = { "left", "top", "right", "bottom" };
	gboolean any = FALSE;
	guint i;

	over[0] = area->x - r->x;
	over[1] = area->y - r->y;
	over[2] = (r->x + r->width) - (area->x + area->width);
	over[3] = (r->y + r->height) - (area->y + area->height);
	for (i = 0; i < 4; i++)
	{
		if (over[i] <= FRAME_EPSILON)
			continue;
		if (s != NULL)
			g_string_append_printf (s, "%s%s %g", any ? ", " : "", edge[i], (gdouble)over[i]);
		any = TRUE;
	}
	return any;
}

/* The area two rectangles share; %FALSE when they only touch or miss. */
static gboolean
shared_area (const GrlRectangle *a,
             const GrlRectangle *b,
             GrlRectangle       *out)
{
	gfloat left = MAX (a->x, b->x);
	gfloat top = MAX (a->y, b->y);
	gfloat right = MIN (a->x + a->width, b->x + b->width);
	gfloat bottom = MIN (a->y + a->height, b->y + b->height);

	if (right - left <= FRAME_EPSILON || bottom - top <= FRAME_EPSILON)
		return FALSE;
	out->x = left;
	out->y = top;
	out->width = right - left;
	out->height = bottom - top;
	return TRUE;
}

static void
problem_clear (gpointer data)
{
	Problem *problem = data;

	g_free (problem->message);
}

static void
add_problem (GArray              *problems,
             LrgFrameLayoutError  code,
             GString             *message,
             guint                box_a,
             guint                box_b,
             const GrlRectangle  *shared)
{
	Problem problem;

	memset (&problem, 0, sizeof (problem));
	problem.code = code;
	problem.message = g_string_free (message, FALSE);
	problem.box_a = box_a;
	problem.box_b = box_b;
	if (shared != NULL)
		problem.shared = *shared;
	g_array_append_val (problems, problem);
}

/*
 * Every problem of the layout, in a fixed order: first one per box in
 * insertion order (no rectangle, else out of the frame, else out of the
 * safe area: a box outside the frame is outside the safe area too, and
 * only the worse is reported), then the overlapping pairs in insertion
 * order of their first and then second box.
 */
static GArray *
collect_problems (LrgFrameLayout *self)
{
	GArray *problems = g_array_new (FALSE, TRUE, sizeof (Problem));
	GrlRectangle frame;
	GrlRectangle safe;
	guint i;
	guint j;

	frame.x = 0.0f;
	frame.y = 0.0f;
	frame.width = self->width;
	frame.height = self->height;
	g_array_set_clear_func (problems, problem_clear);
	resolve_all (self);
	safe_area (self, &safe);

	/* --- per box: placement, frame, safe area --- */
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);
		GString *s;

		if (box->unresolved != UNRESOLVED_NONE)
		{
			s = g_string_new (NULL);
			g_string_append_printf (s, "unknown box: \"%s\" is placed relative to \"%s\", ",
			                        box->name, box->of);
			if (box->unresolved == UNRESOLVED_MISSING)
				g_string_append (s, "which does not exist");
			else if (box->unresolved == UNRESOLVED_LOOP)
				g_string_append (s, "and the placements loop back to it");
			else
				g_string_append (s, "which has no rectangle itself");
			add_problem (problems, LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX, s, i, G_MAXUINT, NULL);
		}
		else if (append_overhang (NULL, &box->resolved, &frame))
		{
			s = g_string_new (NULL);
			g_string_append_printf (s, "out of frame: \"%s\" ", box->name);
			append_rect (s, &box->resolved);
			g_string_append_printf (s, " leaves the %gx%g frame by ",
			                        (gdouble)self->width, (gdouble)self->height);
			append_overhang (s, &box->resolved, &frame);
			add_problem (problems, LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME, s, i, G_MAXUINT, NULL);
		}
		else if (append_overhang (NULL, &box->resolved, &safe))
		{
			s = g_string_new (NULL);
			g_string_append_printf (s, "out of safe area: \"%s\" ", box->name);
			append_rect (s, &box->resolved);
			g_string_append (s, " leaves the safe area ");
			append_rect (s, &safe);
			g_string_append (s, " by ");
			append_overhang (s, &box->resolved, &safe);
			add_problem (problems, LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA, s, i, G_MAXUINT, NULL);
		}
	}

	/* --- per pair: overlaps that were not allowed --- */
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *a = g_ptr_array_index (self->boxes, i);

		if (a->unresolved != UNRESOLVED_NONE)
			continue;
		for (j = i + 1; j < self->boxes->len; j++)
		{
			Box *b = g_ptr_array_index (self->boxes, j);
			GrlRectangle shared;
			g_autofree gchar *key = NULL;
			GString *s;

			if (b->unresolved != UNRESOLVED_NONE ||
			    !shared_area (&a->resolved, &b->resolved, &shared))
				continue;
			key = pair_key (a->name, b->name);
			if (g_hash_table_contains (self->allowed, key))
				continue;

			s = g_string_new (NULL);
			g_string_append_printf (s, "overlap: \"%s\" ", a->name);
			append_rect (s, &a->resolved);
			g_string_append_printf (s, " and \"%s\" ", b->name);
			append_rect (s, &b->resolved);
			g_string_append (s, " share ");
			append_rect (s, &shared);
			add_problem (problems, LRG_FRAME_LAYOUT_ERROR_OVERLAP, s, i, j, &shared);
		}
	}
	return problems;
}

GPtrArray *
lrg_frame_layout_get_problems (LrgFrameLayout *self)
{
	g_autoptr(GArray) problems = NULL;
	GPtrArray *lines;
	guint i;

	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), NULL);

	problems = collect_problems (self);
	lines = g_ptr_array_new_full (problems->len, g_free);
	for (i = 0; i < problems->len; i++)
		g_ptr_array_add (lines, g_strdup (g_array_index (problems, Problem, i).message));
	return lines;
}

gboolean
lrg_frame_layout_validate (LrgFrameLayout  *self,
                           GError         **error)
{
	g_autoptr(GArray) problems = NULL;
	GString *message;
	guint i;

	g_return_val_if_fail (LRG_IS_FRAME_LAYOUT (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	problems = collect_problems (self);
	if (problems->len == 0)
		return TRUE;

	/* every problem, one per line; the code is the first one's */
	message = g_string_new (NULL);
	for (i = 0; i < problems->len; i++)
	{
		if (i > 0)
			g_string_append_c (message, '\n');
		g_string_append (message, g_array_index (problems, Problem, i).message);
	}
	g_set_error_literal (error, LRG_FRAME_LAYOUT_ERROR,
	                     (gint)g_array_index (problems, Problem, 0).code, message->str);
	g_string_free (message, TRUE);
	return FALSE;
}

/* ==========================================================================
 * Debug overlay
 * ========================================================================== */

#define DEBUG_LABEL_SIZE (10)	/* the bitmap font's own height: crisp */

/* A rectangle rounded out to whole pixels. */
static GrlRectangle
pixel_rect (const GrlRectangle *r)
{
	GrlRectangle out;

	out.x = floorf (r->x);
	out.y = floorf (r->y);
	out.width = ceilf (r->x + r->width) - out.x;
	out.height = ceilf (r->y + r->height) - out.y;
	return out;
}

/* One box: its outline and a name label inside its top-left corner. */
static void
draw_box (GrlImage           *image,
          const Box          *box,
          gboolean            problem)
{
	static const GrlColor ok = { 60, 220, 120, 255 };
	static const GrlColor bad = { 255, 40, 40, 255 };
	static const GrlColor ink = { 0, 0, 0, 255 };
	static const GrlColor paper = { 255, 255, 255, 255 };
	GrlRectangle r = pixel_rect (&box->resolved);
	GrlRectangle label;
	g_autoptr(GrlVector2) size = NULL;

	if (r.width >= 1.0f && r.height >= 1.0f)
		grl_image_draw_rectangle_lines (image, &r, problem ? 3 : 2, problem ? &bad : &ok);

	/* --- the name on a filled label, so it reads over any picture --- */
	size = grl_image_measure_text_bitmap (box->name, DEBUG_LABEL_SIZE);
	label.x = r.x;
	label.y = r.y;
	label.width = size->x + 4.0f;
	label.height = size->y + 4.0f;
	grl_image_draw_rectangle (image, &label, problem ? &bad : &ok);
	grl_image_draw_text_bitmap (image, box->name, (gint)label.x + 2, (gint)label.y + 2,
	                            DEBUG_LABEL_SIZE, problem ? &paper : &ink);
}

void
lrg_frame_layout_draw_debug (LrgFrameLayout *self,
                             LrgImageCanvas *canvas)
{
	static const GrlColor safe_color = { 255, 210, 0, 255 };
	static const GrlColor shared_color = { 255, 40, 40, 128 };
	g_autoptr(GArray) problems = NULL;
	g_autofree gboolean *flagged = NULL;
	GrlImage *image;
	GrlImageBlendMode old_mode;
	GrlRectangle safe;
	GrlRectangle r;
	guint i;

	g_return_if_fail (LRG_IS_FRAME_LAYOUT (self));
	g_return_if_fail (LRG_IS_IMAGE_CANVAS (canvas));

	image = lrg_image_canvas_get_image (canvas);
	g_return_if_fail (image != NULL);

	/* --- which boxes have a problem --- */
	problems = collect_problems (self);
	flagged = g_new0 (gboolean, self->boxes->len + 1);
	for (i = 0; i < problems->len; i++)
	{
		const Problem *p = &g_array_index (problems, Problem, i);

		flagged[p->box_a] = TRUE;
		if (p->box_b != G_MAXUINT)
			flagged[p->box_b] = TRUE;
	}

	/* the shading is translucent: blend it over, then restore the mode */
	old_mode = grl_image_get_blend_mode (image);
	grl_image_set_blend_mode (image, GRL_IMAGE_BLEND_OVER);

	/* --- overlaps: shade the shared area --- */
	for (i = 0; i < problems->len; i++)
	{
		const Problem *p = &g_array_index (problems, Problem, i);

		if (p->code != LRG_FRAME_LAYOUT_ERROR_OVERLAP)
			continue;
		r = pixel_rect (&p->shared);
		grl_image_draw_rectangle (image, &r, &shared_color);
	}

	/* --- the safe area --- */
	safe_area (self, &safe);
	r = pixel_rect (&safe);
	if (r.width >= 1.0f && r.height >= 1.0f)
		grl_image_draw_rectangle_lines (image, &r, 1, &safe_color);

	/* --- the boxes, clean ones first so problem outlines stay on top --- */
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);

		if (box->unresolved == UNRESOLVED_NONE && !flagged[i])
			draw_box (image, box, FALSE);
	}
	for (i = 0; i < self->boxes->len; i++)
	{
		Box *box = g_ptr_array_index (self->boxes, i);

		if (box->unresolved == UNRESOLVED_NONE && flagged[i])
			draw_box (image, box, TRUE);
	}

	grl_image_set_blend_mode (image, old_mode);
}
