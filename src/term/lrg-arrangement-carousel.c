/* lrg-arrangement-carousel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include "lrg-arrangement-carousel.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* Panel height in world units: the front panel fills about 60% of the default
   camera's view, leaving its neighbours visible at the sides. */
#define LRG_CAROUSEL_HEIGHT    3.4f
/* Extra arc length between slot groups, as a fraction of the panel height. */
#define LRG_CAROUSEL_GROUP_GAP 0.6f

struct _LrgArrangementCarousel
{
	GObject parent_instance;

	gfloat radius;   /* minimum ring radius */
	gfloat spacing;  /* centre distance in panel widths */
};

static void lrg_arrangement_carousel_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementCarousel, lrg_arrangement_carousel, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
                                                      lrg_arrangement_carousel_iface_init))

static const gchar *
carousel_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "carousel";
}

/* World width of @p at the carousel height, from its source aspect. */
static gfloat
panel_width (LrgScenePanel *p)
{
	gint sw, sh;

	lrg_scene_panel_get_source_rect (p, NULL, NULL, &sw, &sh);
	return LRG_CAROUSEL_HEIGHT * ((sw > 0 && sh > 0) ? (gfloat) sw / (gfloat) sh : 1.6f);
}

/*
 * Lay the panels out along a ring centred at (0, 0, R), on the viewer's side
 * of the origin, so the ring curves around the default camera.
 *
 * 1. Sort a copy of the panels by slot (group, ordinal); the sort is stable,
 *    so equal slots keep their sync order.
 * 2. Walk them assigning arc-length positions: neighbours sit their mean
 *    width apart (times the spacing), plus a gap where the group changes.
 * 3. The radius is the configured minimum, grown so that the whole sequence
 *    (plus the gap that closes the ring) fits on one full circle.
 * 4. The front panel (ordinal 0, else the middle one) gets angle 0: the
 *    origin, facing +Z.  Every other panel's angle is its arc distance from
 *    the front divided by the radius, wrapped into (-pi, pi] so a closed ring
 *    continues behind the viewer rather than overlapping itself.
 * 5. A panel at angle a sits at (R sin a, 0, R (1 - cos a)) and turns by -a,
 *    so it faces the ring's centre.
 */
static void
carousel_layout (LrgSceneArrangement *arrangement,
                 GPtrArray           *panels,
                 gint                 frame_width,
                 gint                 frame_height,
                 gfloat               scale)
{
	LrgArrangementCarousel *self = LRG_ARRANGEMENT_CAROUSEL (arrangement);
	g_autoptr (GPtrArray) sorted = NULL;
	g_autofree gfloat *arc = NULL;
	g_autofree gfloat *width = NULL;
	gfloat total, radius, front_arc;
	guint n, i, front;

	(void) frame_width;
	(void) frame_height;
	(void) scale;

	n = panels->len;
	if (n == 0)
		return;

	/* 1. stable sort by slot */
	/* A borrowed copy: g_ptr_array_copy() would inherit the caller's element
	   free func and drop references it never took. */
	sorted = g_ptr_array_sized_new (n);
	for (i = 0; i < n; i++)
		g_ptr_array_add (sorted, g_ptr_array_index (panels, i));
	g_ptr_array_sort_values (sorted, lrg_scene_panel_compare_slot);

	/* 2. arc-length positions */
	arc = g_new0 (gfloat, n);
	width = g_new0 (gfloat, n);
	for (i = 0; i < n; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (sorted, i);

		width[i] = panel_width (p);
		if (i == 0)
			continue;
		arc[i] = arc[i - 1] + 0.5f * (width[i - 1] + width[i]) * self->spacing;
		if (lrg_scene_panel_get_group (p)
		    != lrg_scene_panel_get_group (g_ptr_array_index (sorted, i - 1)))
			arc[i] += LRG_CAROUSEL_GROUP_GAP * LRG_CAROUSEL_HEIGHT;
	}

	/* 3. radius: the minimum, or larger so the closed ring fits */
	total = arc[n - 1] + 0.5f * (width[0] + width[n - 1]) * self->spacing;
	radius = MAX (self->radius, total / (2.0f * (gfloat) G_PI));

	/* 4. the front panel */
	front = n / 2;
	for (i = 0; i < n; i++)
		if (lrg_scene_panel_get_ordinal (g_ptr_array_index (sorted, i)) == 0)
		{
			front = i;
			break;
		}
	front_arc = arc[front];

	/* 5. place */
	for (i = 0; i < n; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (sorted, i);
		gfloat a = (arc[i] - front_arc) / radius;

		while (a > (gfloat) G_PI)
			a -= 2.0f * (gfloat) G_PI;
		while (a <= -(gfloat) G_PI)
			a += 2.0f * (gfloat) G_PI;

		lrg_scene_panel_set_target (p,
		                            radius * sinf (a),
		                            0.0f,
		                            radius * (1.0f - cosf (a)),
		                            -a * (gfloat) (180.0 / G_PI),
		                            width[i], LRG_CAROUSEL_HEIGHT);
	}
}

static void
lrg_arrangement_carousel_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = carousel_get_id;
	iface->layout = carousel_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_carousel_class_init (LrgArrangementCarouselClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_carousel_init (LrgArrangementCarousel *self)
{
	self->radius = 7.0f;
	self->spacing = 1.15f;
}

LrgArrangementCarousel *
lrg_arrangement_carousel_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_CAROUSEL, NULL);
}

void
lrg_arrangement_carousel_set_radius (LrgArrangementCarousel *self,
                                     gfloat                  radius)
{
	g_return_if_fail (LRG_IS_ARRANGEMENT_CAROUSEL (self));
	self->radius = CLAMP (radius, 2.0f, 100.0f);
}

gfloat
lrg_arrangement_carousel_get_radius (LrgArrangementCarousel *self)
{
	g_return_val_if_fail (LRG_IS_ARRANGEMENT_CAROUSEL (self), 0.0f);
	return self->radius;
}

void
lrg_arrangement_carousel_set_spacing (LrgArrangementCarousel *self,
                                      gfloat                  spacing)
{
	g_return_if_fail (LRG_IS_ARRANGEMENT_CAROUSEL (self));
	self->spacing = CLAMP (spacing, 1.0f, 4.0f);
}

gfloat
lrg_arrangement_carousel_get_spacing (LrgArrangementCarousel *self)
{
	g_return_val_if_fail (LRG_IS_ARRANGEMENT_CAROUSEL (self), 0.0f);
	return self->spacing;
}
