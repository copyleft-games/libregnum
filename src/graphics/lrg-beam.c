/* lrg-beam.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBeam: a quadratic curve cached as a polyline with cumulative
 * lengths; dashes are spans along that length, clipped to the drawn
 * progress, and drawn as lines (2D) or cylinders (3D).
 */

#include "config.h"

#include "lrg-beam.h"

#include <math.h>
#include <raylib.h>

typedef struct
{
	gfloat x;
	gfloat y;
	gfloat z;
} Vec3;

struct _LrgBeam
{
	GObject      parent_instance;

	Vec3         start;
	Vec3         end;
	GrlColor     color;
	gfloat       width;
	gfloat       arc_height;
	LrgBeamBend  bend;
	Vec3         axis;
	gfloat       dash;
	gfloat       gap;
	gfloat       speed;
	gfloat       phase;
	guint        segments;
	gfloat       progress;

	GArray      *points;    /* Vec3, segments + 1 */
	GArray      *lengths;   /* gfloat cumulative length at each point */
	GArray      *pieces;    /* gfloat pairs (from, to) */
	gboolean     dirty;
};

G_DEFINE_FINAL_TYPE (LrgBeam, lrg_beam, G_TYPE_OBJECT)

GType
lrg_beam_bend_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BEAM_BEND_PERPENDICULAR, "LRG_BEAM_BEND_PERPENDICULAR", "perpendicular" },
			{ LRG_BEAM_BEND_AXIS, "LRG_BEAM_BEND_AXIS", "axis" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgBeamBend"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static void
lrg_beam_finalize (GObject *object)
{
	LrgBeam *self = LRG_BEAM (object);

	g_array_unref (self->points);
	g_array_unref (self->lengths);
	g_array_unref (self->pieces);
	G_OBJECT_CLASS (lrg_beam_parent_class)->finalize (object);
}

static void
lrg_beam_class_init (LrgBeamClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_beam_finalize;
}

static void
lrg_beam_init (LrgBeam *self)
{
	self->color = (GrlColor) { 255, 255, 255, 255 };
	self->width = 2.0f;
	self->axis.y = 1.0f;
	self->segments = 24;
	self->progress = 1.0f;
	self->dash = 1.0f;
	self->points = g_array_new (FALSE, FALSE, sizeof (Vec3));
	self->lengths = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->pieces = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->dirty = TRUE;
}

LrgBeam *
lrg_beam_new (void)
{
	return g_object_new (LRG_TYPE_BEAM, NULL);
}

void
lrg_beam_set_endpoints (LrgBeam *self,
                        gfloat   start_x,
                        gfloat   start_y,
                        gfloat   start_z,
                        gfloat   end_x,
                        gfloat   end_y,
                        gfloat   end_z)
{
	g_return_if_fail (LRG_IS_BEAM (self));

	self->start.x = start_x;
	self->start.y = start_y;
	self->start.z = start_z;
	self->end.x = end_x;
	self->end.y = end_y;
	self->end.z = end_z;
	self->dirty = TRUE;
}

void
lrg_beam_set_color (LrgBeam        *self,
                    const GrlColor *color)
{
	g_return_if_fail (LRG_IS_BEAM (self));
	g_return_if_fail (color != NULL);
	self->color = *color;
}

void
lrg_beam_set_width (LrgBeam *self,
                    gfloat   width)
{
	g_return_if_fail (LRG_IS_BEAM (self));
	self->width = MAX (width, 0.0f);
}

void
lrg_beam_set_arc (LrgBeam     *self,
                  gfloat       height,
                  LrgBeamBend  bend)
{
	g_return_if_fail (LRG_IS_BEAM (self));

	self->arc_height = height;
	self->bend = bend;
	self->dirty = TRUE;
}

void
lrg_beam_set_arc_axis (LrgBeam *self,
                       gfloat   x,
                       gfloat   y,
                       gfloat   z)
{
	gfloat len;

	g_return_if_fail (LRG_IS_BEAM (self));

	len = sqrtf (x * x + y * y + z * z);
	if (len < 1e-6f)
		return;
	self->axis.x = x / len;
	self->axis.y = y / len;
	self->axis.z = z / len;
	self->dirty = TRUE;
}

void
lrg_beam_set_dash (LrgBeam *self,
                   gfloat   dash_length,
                   gfloat   gap_length,
                   gfloat   speed)
{
	g_return_if_fail (LRG_IS_BEAM (self));
	g_return_if_fail (dash_length > 0.0f);

	self->dash = dash_length;
	self->gap = MAX (gap_length, 0.0f);
	self->speed = speed;
	self->phase = 0.0f;
}

void
lrg_beam_set_segments (LrgBeam *self,
                       guint    segments)
{
	g_return_if_fail (LRG_IS_BEAM (self));
	g_return_if_fail (segments >= 1);

	self->segments = segments;
	self->dirty = TRUE;
}

void
lrg_beam_set_progress (LrgBeam *self,
                       gfloat   progress)
{
	g_return_if_fail (LRG_IS_BEAM (self));
	self->progress = CLAMP (progress, 0.0f, 1.0f);
}

gfloat
lrg_beam_get_progress (LrgBeam *self)
{
	g_return_val_if_fail (LRG_IS_BEAM (self), 0.0f);
	return self->progress;
}

void
lrg_beam_update (LrgBeam *self,
                 gfloat   delta)
{
	gfloat period;

	g_return_if_fail (LRG_IS_BEAM (self));

	period = self->dash + self->gap;
	if (self->gap <= 0.0f || period <= 0.0f)
		return;
	/* the pattern repeats every period: keep the phase inside one */
	self->phase = fmodf (self->phase + self->speed * delta, period);
	if (self->phase < 0.0f)
		self->phase += period;
}

/* The quadratic control point: mid + 2 x height x bend direction. */
static Vec3
control_point (LrgBeam *self)
{
	Vec3 c;
	Vec3 dir = { 0.0f, 0.0f, 0.0f };

	c.x = (self->start.x + self->end.x) * 0.5f;
	c.y = (self->start.y + self->end.y) * 0.5f;
	c.z = (self->start.z + self->end.z) * 0.5f;
	if (self->arc_height == 0.0f)
		return c;

	if (self->bend == LRG_BEAM_BEND_AXIS)
		dir = self->axis;
	else
	{
		gfloat dx = self->end.x - self->start.x;
		gfloat dy = self->end.y - self->start.y;
		gfloat len = sqrtf (dx * dx + dy * dy);

		/* left of travel on a y-down screen */
		if (len > 1e-6f)
		{
			dir.x = dy / len;
			dir.y = -dx / len;
		}
	}
	c.x += 2.0f * self->arc_height * dir.x;
	c.y += 2.0f * self->arc_height * dir.y;
	c.z += 2.0f * self->arc_height * dir.z;
	return c;
}

void
lrg_beam_sample (LrgBeam *self,
                 gfloat   t,
                 gfloat  *x,
                 gfloat  *y,
                 gfloat  *z)
{
	Vec3 c;
	gfloat u;

	g_return_if_fail (LRG_IS_BEAM (self));

	t = CLAMP (t, 0.0f, 1.0f);
	u = 1.0f - t;
	c = control_point (self);
	if (x != NULL)
		*x = u * u * self->start.x + 2.0f * u * t * c.x + t * t * self->end.x;
	if (y != NULL)
		*y = u * u * self->start.y + 2.0f * u * t * c.y + t * t * self->end.y;
	if (z != NULL)
		*z = u * u * self->start.z + 2.0f * u * t * c.z + t * t * self->end.z;
}

static void
ensure_polyline (LrgBeam *self)
{
	gfloat total = 0.0f;
	guint i;

	if (!self->dirty)
		return;
	self->dirty = FALSE;
	g_array_set_size (self->points, self->segments + 1);
	g_array_set_size (self->lengths, self->segments + 1);
	for (i = 0; i <= self->segments; i++)
	{
		Vec3 *p = &g_array_index (self->points, Vec3, i);

		lrg_beam_sample (self, (gfloat) i / (gfloat) self->segments, &p->x, &p->y, &p->z);
		if (i > 0)
		{
			Vec3 *q = &g_array_index (self->points, Vec3, i - 1);
			gfloat dx = p->x - q->x;
			gfloat dy = p->y - q->y;
			gfloat dz = p->z - q->z;

			total += sqrtf (dx * dx + dy * dy + dz * dz);
		}
		g_array_index (self->lengths, gfloat, i) = total;
	}
}

gfloat
lrg_beam_get_length (LrgBeam *self)
{
	g_return_val_if_fail (LRG_IS_BEAM (self), 0.0f);

	ensure_polyline (self);
	return g_array_index (self->lengths, gfloat, self->segments);
}

/* Recomputes the visible (from, to) spans along the beam. */
static void
compute_pieces (LrgBeam *self)
{
	gfloat visible = lrg_beam_get_length (self) * self->progress;
	gfloat period = self->dash + self->gap;
	gfloat s;

	g_array_set_size (self->pieces, 0);
	if (visible <= 0.0f)
		return;
	if (self->gap <= 0.0f)
	{
		gfloat from = 0.0f;

		g_array_append_val (self->pieces, from);
		g_array_append_val (self->pieces, visible);
		return;
	}
	/* dashes start at phase + k * period; begin with the one that may
	 * already be under way at distance 0 */
	for (s = self->phase - period; s < visible; s += period)
	{
		gfloat from = MAX (s, 0.0f);
		gfloat to = MIN (s + self->dash, visible);

		if (to > from)
		{
			g_array_append_val (self->pieces, from);
			g_array_append_val (self->pieces, to);
		}
	}
}

guint
lrg_beam_get_n_dashes (LrgBeam *self)
{
	g_return_val_if_fail (LRG_IS_BEAM (self), 0);

	compute_pieces (self);
	return self->pieces->len / 2;
}

gboolean
lrg_beam_get_dash (LrgBeam *self,
                   guint    index,
                   gfloat  *from,
                   gfloat  *to)
{
	g_return_val_if_fail (LRG_IS_BEAM (self), FALSE);

	compute_pieces (self);
	if (index >= self->pieces->len / 2)
		return FALSE;
	if (from != NULL)
		*from = g_array_index (self->pieces, gfloat, index * 2);
	if (to != NULL)
		*to = g_array_index (self->pieces, gfloat, index * 2 + 1);
	return TRUE;
}

/* Point at a distance along the polyline. */
static Vec3
point_at (LrgBeam *self,
          gfloat   distance,
          guint   *segment)
{
	guint i;

	for (i = 1; i <= self->segments; i++)
	{
		gfloat l0 = g_array_index (self->lengths, gfloat, i - 1);
		gfloat l1 = g_array_index (self->lengths, gfloat, i);

		if (distance <= l1 || i == self->segments)
		{
			Vec3 *a = &g_array_index (self->points, Vec3, i - 1);
			Vec3 *b = &g_array_index (self->points, Vec3, i);
			gfloat f = (l1 > l0) ? CLAMP ((distance - l0) / (l1 - l0), 0.0f, 1.0f) : 0.0f;
			Vec3 p;

			p.x = a->x + (b->x - a->x) * f;
			p.y = a->y + (b->y - a->y) * f;
			p.z = a->z + (b->z - a->z) * f;
			*segment = i;
			return p;
		}
	}
	*segment = self->segments;
	return g_array_index (self->points, Vec3, self->segments);
}

static Color
beam_color (LrgBeam *self)
{
	Color c;

	c.r = self->color.r;
	c.g = self->color.g;
	c.b = self->color.b;
	c.a = self->color.a;
	return c;
}

/* Calls @emit for each straight run of one visible piece. */
typedef void (*PieceFunc) (LrgBeam *self, Vec3 a, Vec3 b);

static void
for_each_run (LrgBeam   *self,
              PieceFunc  emit)
{
	guint k;

	compute_pieces (self);
	for (k = 0; k < self->pieces->len / 2; k++)
	{
		gfloat from = g_array_index (self->pieces, gfloat, k * 2);
		gfloat to = g_array_index (self->pieces, gfloat, k * 2 + 1);
		guint seg_from;
		guint seg_to;
		Vec3 prev = point_at (self, from, &seg_from);
		Vec3 last = point_at (self, to, &seg_to);
		guint i;

		/* through every polyline vertex strictly inside the piece */
		for (i = seg_from; i < seg_to; i++)
		{
			Vec3 v = g_array_index (self->points, Vec3, i);

			emit (self, prev, v);
			prev = v;
		}
		emit (self, prev, last);
	}
}

static void
emit_2d (LrgBeam *self,
         Vec3     a,
         Vec3     b)
{
	Vector2 pa = { a.x, a.y };
	Vector2 pb = { b.x, b.y };

	if (self->width <= 0.0f)
		DrawLineV (pa, pb, beam_color (self));
	else
	{
		DrawLineEx (pa, pb, self->width, beam_color (self));
		/* round joints, so the bends of an arc do not crack */
		DrawCircleV (pb, self->width * 0.5f, beam_color (self));
	}
}

static void
emit_3d (LrgBeam *self,
         Vec3     a,
         Vec3     b)
{
	Vector3 pa = { a.x, a.y, a.z };
	Vector3 pb = { b.x, b.y, b.z };

	if (self->width <= 0.0f)
		DrawLine3D (pa, pb, beam_color (self));
	else
		DrawCylinderEx (pa, pb, self->width * 0.5f, self->width * 0.5f, 6, beam_color (self));
}

void
lrg_beam_draw_2d (LrgBeam *self)
{
	g_return_if_fail (LRG_IS_BEAM (self));

	if (!IsWindowReady ())
		return;
	for_each_run (self, emit_2d);
}

void
lrg_beam_draw_3d (LrgBeam *self)
{
	g_return_if_fail (LRG_IS_BEAM (self));

	if (!IsWindowReady ())
		return;
	for_each_run (self, emit_3d);
}
