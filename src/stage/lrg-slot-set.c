/* lrg-slot-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSlotSet: seats, hand-placed and generated standing places, and the
 * sticky deterministic assignment of actors to them.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-slot-set.h"
#include "../lrg-log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	gchar  *name;
	gfloat  x;
	gfloat  y;
	gfloat  z;
	gfloat  facing;
	gchar  *occupant;     /* owned copy of the actor id, or NULL */
} Seat;

typedef struct
{
	gfloat x;
	gfloat y;
	gfloat z;
	gfloat facing;
} Place;

typedef struct
{
	LrgSlotKind kind;
	guint       index;
	guint64     arrival;  /* when the actor first stood: orders promotions */
} Placement;

struct _LrgSlotSet
{
	GObject             parent_instance;

	LrgActorMoverPlane  plane;
	GArray             *seats;      /* Seat */
	GArray             *standing;   /* Place, hand-placed */
	gfloat              centre_x;
	gfloat              centre_y;
	gfloat              centre_z;
	gfloat              radius;
	gfloat              spacing;

	GHashTable         *placements; /* actor id -> Placement */
	GHashTable         *stand_used; /* GUINT_TO_POINTER (index + 1) -> actor id */
	guint64             next_arrival;
};

G_DEFINE_FINAL_TYPE (LrgSlotSet, lrg_slot_set, G_TYPE_OBJECT)

GType
lrg_slot_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SLOT_NONE, "LRG_SLOT_NONE", "none" },
			{ LRG_SLOT_SEAT, "LRG_SLOT_SEAT", "seat" },
			{ LRG_SLOT_STANDING, "LRG_SLOT_STANDING", "standing" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSlotKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static void
seat_clear (gpointer data)
{
	Seat *seat = data;

	g_free (seat->name);
	g_free (seat->occupant);
}

static void
lrg_slot_set_finalize (GObject *object)
{
	LrgSlotSet *self = LRG_SLOT_SET (object);

	g_array_unref (self->seats);
	g_array_unref (self->standing);
	g_hash_table_unref (self->placements);
	g_hash_table_unref (self->stand_used);
	G_OBJECT_CLASS (lrg_slot_set_parent_class)->finalize (object);
}

static void
lrg_slot_set_class_init (LrgSlotSetClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_slot_set_finalize;
}

static void
lrg_slot_set_init (LrgSlotSet *self)
{
	self->seats = g_array_new (FALSE, TRUE, sizeof (Seat));
	g_array_set_clear_func (self->seats, seat_clear);
	self->standing = g_array_new (FALSE, FALSE, sizeof (Place));
	self->radius = 24.0f;
	self->spacing = 20.0f;
	self->placements = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->stand_used = g_hash_table_new (g_direct_hash, g_direct_equal);
}

LrgSlotSet *
lrg_slot_set_new (void)
{
	return g_object_new (LRG_TYPE_SLOT_SET, NULL);
}

void
lrg_slot_set_set_plane (LrgSlotSet         *self,
                        LrgActorMoverPlane  plane)
{
	g_return_if_fail (LRG_IS_SLOT_SET (self));
	self->plane = plane;
}

/* A point at @angle and @radius round a centre, in the ground plane. */
static void
point_round (LrgSlotSet *self,
             gfloat      cx,
             gfloat      cy,
             gfloat      cz,
             gfloat      radius,
             gfloat      angle,
             gfloat     *x,
             gfloat     *y,
             gfloat     *z)
{
	gfloat da = radius * cosf (angle);
	gfloat db = radius * sinf (angle);

	*x = cx + da;
	if (self->plane == LRG_ACTOR_MOVER_PLANE_XZ)
	{
		*y = cy;
		*z = cz + db;
	}
	else
	{
		*y = cy + db;
		*z = cz;
	}
}

/* The facing that looks from @angle back at the centre. */
static gfloat
facing_inward (gfloat angle)
{
	gfloat f = angle + (gfloat) G_PI;

	while (f > (gfloat) G_PI)
		f -= 2.0f * (gfloat) G_PI;
	return f;
}

static Seat *
find_seat (LrgSlotSet  *self,
           const gchar *name)
{
	guint i;

	for (i = 0; i < self->seats->len; i++)
	{
		Seat *seat = &g_array_index (self->seats, Seat, i);

		if (g_strcmp0 (seat->name, name) == 0)
			return seat;
	}
	return NULL;
}

gboolean
lrg_slot_set_add_seat (LrgSlotSet  *self,
                       const gchar *name,
                       gfloat       x,
                       gfloat       y,
                       gfloat       z,
                       gfloat       facing)
{
	Seat seat;

	g_return_val_if_fail (LRG_IS_SLOT_SET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (find_seat (self, name) != NULL)
		return FALSE;
	memset (&seat, 0, sizeof (seat));
	seat.name = g_strdup (name);
	seat.x = x;
	seat.y = y;
	seat.z = z;
	seat.facing = facing;
	g_array_append_val (self->seats, seat);
	return TRUE;
}

guint
lrg_slot_set_add_seats_around (LrgSlotSet  *self,
                               const gchar *prefix,
                               gfloat       x,
                               gfloat       y,
                               gfloat       z,
                               gfloat       radius,
                               guint        count,
                               gfloat       start_angle)
{
	guint added = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_SLOT_SET (self), 0);
	g_return_val_if_fail (prefix != NULL, 0);

	for (i = 0; i < count; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("%s%u", prefix, i);
		gfloat angle = start_angle + 2.0f * (gfloat) G_PI * (gfloat) i / (gfloat) count;
		gfloat sx;
		gfloat sy;
		gfloat sz;

		point_round (self, x, y, z, radius, angle, &sx, &sy, &sz);
		if (lrg_slot_set_add_seat (self, name, sx, sy, sz, facing_inward (angle)))
			added++;
	}
	return added;
}

void
lrg_slot_set_add_standing (LrgSlotSet *self,
                           gfloat      x,
                           gfloat      y,
                           gfloat      z,
                           gfloat      facing)
{
	Place place;

	g_return_if_fail (LRG_IS_SLOT_SET (self));

	place.x = x;
	place.y = y;
	place.z = z;
	place.facing = facing;
	g_array_append_val (self->standing, place);
}

void
lrg_slot_set_set_overflow (LrgSlotSet *self,
                           gfloat      x,
                           gfloat      y,
                           gfloat      z,
                           gfloat      radius,
                           gfloat      spacing)
{
	g_return_if_fail (LRG_IS_SLOT_SET (self));
	g_return_if_fail (radius > 0.0f && spacing > 0.0f);

	self->centre_x = x;
	self->centre_y = y;
	self->centre_z = z;
	self->radius = radius;
	self->spacing = spacing;
}

guint
lrg_slot_set_get_n_seats (LrgSlotSet *self)
{
	g_return_val_if_fail (LRG_IS_SLOT_SET (self), 0);
	return self->seats->len;
}

const gchar *
lrg_slot_set_get_seat_name (LrgSlotSet *self,
                            guint       index)
{
	g_return_val_if_fail (LRG_IS_SLOT_SET (self), NULL);

	if (index >= self->seats->len)
		return NULL;
	return g_array_index (self->seats, Seat, index).name;
}

void
lrg_slot_set_get_standing_place (LrgSlotSet *self,
                                 guint       index,
                                 gfloat     *x,
                                 gfloat     *y,
                                 gfloat     *z,
                                 gfloat     *facing)
{
	gfloat px;
	gfloat py;
	gfloat pz;
	gfloat pf;
	guint ring = 0;
	guint i;

	g_return_if_fail (LRG_IS_SLOT_SET (self));

	if (index < self->standing->len)
	{
		Place *p = &g_array_index (self->standing, Place, index);

		px = p->x;
		py = p->y;
		pz = p->z;
		pf = p->facing;
	}
	else
	{
		/* walk the rings until the index falls on one */
		i = index - self->standing->len;
		for (;;)
		{
			gfloat r = self->radius + (gfloat) ring * self->spacing;
			guint n = MAX ((guint) floorf (2.0f * (gfloat) G_PI * r / self->spacing), 1u);
			gfloat angle;

			if (i < n)
			{
				angle = 2.0f * (gfloat) G_PI * (gfloat) i / (gfloat) n;
				point_round (self, self->centre_x, self->centre_y, self->centre_z, r, angle,
				             &px, &py, &pz);
				pf = facing_inward (angle);
				break;
			}
			i -= n;
			ring++;
		}
	}
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	if (z != NULL)
		*z = pz;
	if (facing != NULL)
		*facing = pf;
}

/* ==========================================================================
 * Assignment
 * ========================================================================== */

typedef struct
{
	const gchar *id;
	guint64      arrival;
} Waiting;

static gint
compare_waiting (gconstpointer a,
                 gconstpointer b)
{
	const Waiting *wa = a;
	const Waiting *wb = b;

	if (wa->arrival != wb->arrival)
		return (wa->arrival < wb->arrival) ? -1 : 1;
	return strcmp (wa->id, wb->id);
}

static gint
compare_ids (gconstpointer a,
             gconstpointer b)
{
	return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

static gint
first_free_seat (LrgSlotSet *self)
{
	guint i;

	for (i = 0; i < self->seats->len; i++)
	{
		if (g_array_index (self->seats, Seat, i).occupant == NULL)
			return (gint) i;
	}
	return -1;
}

static void
free_placement (LrgSlotSet *self,
                const gchar *id,
                Placement   *p)
{
	if (p->kind == LRG_SLOT_SEAT)
		g_clear_pointer (&g_array_index (self->seats, Seat, p->index).occupant, g_free);
	else if (p->kind == LRG_SLOT_STANDING)
		g_hash_table_remove (self->stand_used, GUINT_TO_POINTER (p->index + 1));
}

static void
take_seat (LrgSlotSet *self,
           const gchar *id,
           Placement   *p,
           guint        seat)
{
	p->kind = LRG_SLOT_SEAT;
	p->index = seat;
	g_array_index (self->seats, Seat, seat).occupant = g_strdup (id);
}

void
lrg_slot_set_assign (LrgSlotSet          *self,
                     const gchar * const *actor_ids)
{
	g_autoptr(GHashTable) present = NULL;
	g_autoptr(GPtrArray) gone = NULL;
	g_autoptr(GArray) waiting = NULL;
	g_autoptr(GPtrArray) fresh = NULL;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	guint i;

	g_return_if_fail (LRG_IS_SLOT_SET (self));

	present = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; actor_ids != NULL && actor_ids[i] != NULL; i++)
		g_hash_table_add (present, (gpointer) actor_ids[i]);

	/* 1. actors that left free their places */
	gone = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, self->placements);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (!g_hash_table_contains (present, key))
		{
			free_placement (self, key, value);
			g_ptr_array_add (gone, key);
		}
	}
	for (i = 0; i < gone->len; i++)
		g_hash_table_remove (self->placements, g_ptr_array_index (gone, i));

	/* 2-3. standing actors move to free seats, longest waiting first */
	waiting = g_array_new (FALSE, FALSE, sizeof (Waiting));
	g_hash_table_iter_init (&iter, self->placements);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		Placement *p = value;

		if (p->kind == LRG_SLOT_STANDING)
		{
			Waiting w = { key, p->arrival };

			g_array_append_val (waiting, w);
		}
	}
	g_array_sort (waiting, compare_waiting);
	for (i = 0; i < waiting->len; i++)
	{
		Waiting *w = &g_array_index (waiting, Waiting, i);
		gint seat = first_free_seat (self);
		Placement *p;

		if (seat < 0)
			break;
		p = g_hash_table_lookup (self->placements, w->id);
		free_placement (self, w->id, p);
		take_seat (self, w->id, p, (guint) seat);
	}

	/* 3-4. new actors, by id: free seats, then the lowest free places */
	fresh = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, present);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		if (!g_hash_table_contains (self->placements, key))
			g_ptr_array_add (fresh, key);
	}
	g_ptr_array_sort (fresh, compare_ids);
	for (i = 0; i < fresh->len; i++)
	{
		const gchar *id = g_ptr_array_index (fresh, i);
		Placement *p = g_new0 (Placement, 1);
		gint seat = first_free_seat (self);

		if (seat >= 0)
			take_seat (self, id, p, (guint) seat);
		else
		{
			guint index = 0;

			while (g_hash_table_contains (self->stand_used, GUINT_TO_POINTER (index + 1)))
				index++;
			p->kind = LRG_SLOT_STANDING;
			p->index = index;
			p->arrival = self->next_arrival++;
			g_hash_table_insert (self->stand_used, GUINT_TO_POINTER (index + 1), (gpointer) id);
		}
		g_hash_table_insert (self->placements, g_strdup (id), p);
	}

	/* stand_used held borrowed ids from the caller: point it at our keys */
	g_hash_table_iter_init (&iter, self->placements);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		Placement *p = value;

		if (p->kind == LRG_SLOT_STANDING)
			g_hash_table_insert (self->stand_used, GUINT_TO_POINTER (p->index + 1), key);
	}
}

void
lrg_slot_set_clear (LrgSlotSet *self)
{
	guint i;

	g_return_if_fail (LRG_IS_SLOT_SET (self));

	for (i = 0; i < self->seats->len; i++)
		g_clear_pointer (&g_array_index (self->seats, Seat, i).occupant, g_free);
	g_hash_table_remove_all (self->placements);
	g_hash_table_remove_all (self->stand_used);
	self->next_arrival = 0;
}

LrgSlotKind
lrg_slot_set_get_placement (LrgSlotSet  *self,
                            const gchar *actor_id,
                            guint       *index,
                            gfloat      *x,
                            gfloat      *y,
                            gfloat      *z,
                            gfloat      *facing)
{
	Placement *p;

	g_return_val_if_fail (LRG_IS_SLOT_SET (self), LRG_SLOT_NONE);
	g_return_val_if_fail (actor_id != NULL, LRG_SLOT_NONE);

	p = g_hash_table_lookup (self->placements, actor_id);
	if (p == NULL)
		return LRG_SLOT_NONE;
	if (index != NULL)
		*index = p->index;
	if (p->kind == LRG_SLOT_SEAT)
	{
		Seat *seat = &g_array_index (self->seats, Seat, p->index);

		if (x != NULL)
			*x = seat->x;
		if (y != NULL)
			*y = seat->y;
		if (z != NULL)
			*z = seat->z;
		if (facing != NULL)
			*facing = seat->facing;
	}
	else
		lrg_slot_set_get_standing_place (self, p->index, x, y, z, facing);
	return p->kind;
}

const gchar *
lrg_slot_set_get_seat_occupant (LrgSlotSet *self,
                                guint       index)
{
	g_return_val_if_fail (LRG_IS_SLOT_SET (self), NULL);

	if (index >= self->seats->len)
		return NULL;
	return g_array_index (self->seats, Seat, index).occupant;
}

guint
lrg_slot_set_get_n_seated (LrgSlotSet *self)
{
	guint n = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_SLOT_SET (self), 0);

	for (i = 0; i < self->seats->len; i++)
	{
		if (g_array_index (self->seats, Seat, i).occupant != NULL)
			n++;
	}
	return n;
}

guint
lrg_slot_set_get_n_standing (LrgSlotSet *self)
{
	g_return_val_if_fail (LRG_IS_SLOT_SET (self), 0);
	return g_hash_table_size (self->stand_used);
}
