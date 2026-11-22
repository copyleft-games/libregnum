/* test-slot-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgSlotSet: seat order, overflow rings, independence from
 * input order, stickiness as actors come and go, promotion from standing
 * to a freed seat, and identical results from identical calls.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>

static LrgSlotSet *
meeting_room (void)
{
	LrgSlotSet *set = lrg_slot_set_new ();

	/* four seats round a table at (100, 100), radius 20, first at angle 0 */
	g_assert_cmpuint (lrg_slot_set_add_seats_around (set, "chair-", 100.0f, 100.0f, 0.0f,
	                                                 20.0f, 4, 0.0f), ==, 4);
	lrg_slot_set_set_overflow (set, 100.0f, 100.0f, 0.0f, 40.0f, 20.0f);
	return set;
}

static void
test_seats_and_overflow (void)
{
	g_autoptr(LrgSlotSet) set = meeting_room ();
	const gchar *ids[] = { "eve", "bob", "dan", "amy", "cat", "fay", NULL };
	gfloat x;
	gfloat y;
	gfloat facing;
	guint index;

	g_assert_cmpuint (lrg_slot_set_get_n_seats (set), ==, 4);
	g_assert_cmpstr (lrg_slot_set_get_seat_name (set, 2), ==, "chair-2");
	g_assert_null (lrg_slot_set_get_seat_name (set, 4));
	g_assert_false (lrg_slot_set_add_seat (set, "chair-0", 0.0f, 0.0f, 0.0f, 0.0f));
	g_assert_cmpuint (lrg_slot_set_add_seats_around (set, "chair-", 0.0f, 0.0f, 0.0f,
	                                                 1.0f, 2, 0.0f), ==, 0);

	lrg_slot_set_assign (set, ids);
	g_assert_cmpuint (lrg_slot_set_get_n_seated (set), ==, 4);
	g_assert_cmpuint (lrg_slot_set_get_n_standing (set), ==, 2);

	/* seats go by id: amy, bob, cat, dan; eve and fay stand */
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 0), ==, "amy");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 3), ==, "dan");
	g_assert_cmpint (lrg_slot_set_get_placement (set, "amy", &index, &x, &y, NULL, &facing),
	                 ==, LRG_SLOT_SEAT);
	g_assert_cmpuint (index, ==, 0);
	g_assert_cmpfloat_with_epsilon (x, 120.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (y, 100.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (fabsf (facing), (gfloat) G_PI, 1e-5f);  /* facing the table */
	g_assert_cmpint (lrg_slot_set_get_placement (set, "bob", NULL, &x, &y, NULL, &facing),
	                 ==, LRG_SLOT_SEAT);
	g_assert_cmpfloat_with_epsilon (y, 120.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (facing, -(gfloat) G_PI_2, 1e-5f);

	g_assert_cmpint (lrg_slot_set_get_placement (set, "eve", &index, &x, &y, NULL, NULL),
	                 ==, LRG_SLOT_STANDING);
	g_assert_cmpuint (index, ==, 0);
	g_assert_cmpfloat_with_epsilon (x, 140.0f, 1e-4f);    /* ring 1 at angle 0 */
	g_assert_cmpfloat_with_epsilon (y, 100.0f, 1e-4f);
	g_assert_cmpint (lrg_slot_set_get_placement (set, "fay", &index, NULL, NULL, NULL, NULL),
	                 ==, LRG_SLOT_STANDING);
	g_assert_cmpuint (index, ==, 1);
	g_assert_cmpint (lrg_slot_set_get_placement (set, "zed", NULL, NULL, NULL, NULL, NULL),
	                 ==, LRG_SLOT_NONE);
}

static void
test_rings (void)
{
	g_autoptr(LrgSlotSet) set = lrg_slot_set_new ();
	gfloat x;
	gfloat y;
	gfloat z;
	gfloat f;
	guint n_first;

	lrg_slot_set_set_overflow (set, 0.0f, 0.0f, 0.0f, 10.0f, 10.0f);
	/* ring 0: floor (2 pi 10 / 10) = 6 places, ring 1 (r = 20): 12 */
	n_first = (guint) floorf (2.0f * (gfloat) G_PI);
	g_assert_cmpuint (n_first, ==, 6);
	lrg_slot_set_get_standing_place (set, 6, &x, &y, NULL, &f);
	g_assert_cmpfloat_with_epsilon (x, 20.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (y, 0.0f, 1e-4f);
	lrg_slot_set_get_standing_place (set, 3, &x, &y, NULL, &f);
	g_assert_cmpfloat_with_epsilon (x, -10.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (f, 0.0f, 1e-5f);   /* facing back to the centre */

	/* hand-placed places come first and shift the rings */
	lrg_slot_set_add_standing (set, 5.0f, 6.0f, 7.0f, 1.0f);
	lrg_slot_set_get_standing_place (set, 0, &x, &y, &z, &f);
	g_assert_cmpfloat (z, ==, 7.0f);
	g_assert_cmpfloat (f, ==, 1.0f);
	lrg_slot_set_get_standing_place (set, 7, &x, &y, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (x, 20.0f, 1e-4f);

	/* the x/z plane keeps y and spreads in z */
	lrg_slot_set_set_plane (set, LRG_ACTOR_MOVER_PLANE_XZ);
	lrg_slot_set_set_overflow (set, 0.0f, 3.0f, 0.0f, 10.0f, 10.0f);
	lrg_slot_set_get_standing_place (set, 1 + 6 / 4, &x, &y, &z, NULL);
	g_assert_cmpfloat (y, ==, 3.0f);
	g_assert_cmpfloat (fabsf (z), >, 1.0f);
}

static void
test_order_independent (void)
{
	g_autoptr(LrgSlotSet) a = meeting_room ();
	g_autoptr(LrgSlotSet) b = meeting_room ();
	const gchar *forward[] = { "a1", "b2", "c3", "d4", "e5", "f6", "g7", NULL };
	const gchar *backward[] = { "g7", "f6", "e5", "d4", "c3", "b2", "a1", "a1", NULL };
	guint i;

	lrg_slot_set_assign (a, forward);
	lrg_slot_set_assign (b, backward);
	for (i = 0; forward[i] != NULL; i++)
	{
		guint ia;
		guint ib;
		LrgSlotKind ka = lrg_slot_set_get_placement (a, forward[i], &ia, NULL, NULL, NULL, NULL);
		LrgSlotKind kb = lrg_slot_set_get_placement (b, forward[i], &ib, NULL, NULL, NULL, NULL);

		g_assert_cmpint (ka, ==, kb);
		g_assert_cmpuint (ia, ==, ib);
	}
	g_assert_cmpuint (lrg_slot_set_get_n_standing (b), ==, 3);
}

static void
test_sticky_and_promotion (void)
{
	g_autoptr(LrgSlotSet) set = meeting_room ();
	const gchar *first[] = { "dan", "cat", "bob", "amy", "eve", "fay", NULL };
	const gchar *bob_left[] = { "dan", "cat", "amy", "eve", "fay", "abe", NULL };
	const gchar *later[] = { "dan", "abe", "zoe", NULL };
	guint index;

	lrg_slot_set_assign (set, first);
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 1), ==, "bob");

	/* bob leaves; eve, standing longest (first by id among the first
	 * standers), takes his seat rather than newcomer abe */
	lrg_slot_set_assign (set, bob_left);
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 1), ==, "eve");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 0), ==, "amy");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 3), ==, "dan");
	/* fay keeps her place; abe takes the freed standing place 0 */
	g_assert_cmpint (lrg_slot_set_get_placement (set, "fay", &index, NULL, NULL, NULL, NULL),
	                 ==, LRG_SLOT_STANDING);
	g_assert_cmpuint (index, ==, 1);
	g_assert_cmpint (lrg_slot_set_get_placement (set, "abe", &index, NULL, NULL, NULL, NULL),
	                 ==, LRG_SLOT_STANDING);
	g_assert_cmpuint (index, ==, 0);

	/* most leave: dan keeps seat 3, abe (who stood) is seated first */
	lrg_slot_set_assign (set, later);
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 3), ==, "dan");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 0), ==, "abe");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (set, 1), ==, "zoe");
	g_assert_null (lrg_slot_set_get_seat_occupant (set, 2));
	g_assert_cmpuint (lrg_slot_set_get_n_standing (set), ==, 0);

	/* nobody left */
	lrg_slot_set_assign (set, NULL);
	g_assert_cmpuint (lrg_slot_set_get_n_seated (set), ==, 0);
}

static void
test_same_calls_same_result (void)
{
	g_autoptr(LrgSlotSet) a = meeting_room ();
	g_autoptr(LrgSlotSet) b = meeting_room ();
	const gchar *s1[] = { "k", "j", "i", "h", "g", "f", NULL };
	const gchar *s2[] = { "k", "h", "x", "y", "z", NULL };
	const gchar *all[] = { "f", "g", "h", "i", "j", "k", "x", "y", "z", NULL };
	guint i;

	lrg_slot_set_assign (a, s1);
	lrg_slot_set_assign (a, s2);
	lrg_slot_set_assign (b, s1);
	lrg_slot_set_assign (b, s2);
	for (i = 0; all[i] != NULL; i++)
	{
		guint ia = 99;
		guint ib = 99;

		g_assert_cmpint (lrg_slot_set_get_placement (a, all[i], &ia, NULL, NULL, NULL, NULL), ==,
		                 lrg_slot_set_get_placement (b, all[i], &ib, NULL, NULL, NULL, NULL));
		g_assert_cmpuint (ia, ==, ib);
	}

	/* after clear, the result depends on the ids alone */
	lrg_slot_set_clear (a);
	g_assert_cmpuint (lrg_slot_set_get_n_seated (a), ==, 0);
	lrg_slot_set_assign (a, s2);
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (a, 0), ==, "h");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (a, 1), ==, "k");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (a, 2), ==, "x");
	g_assert_cmpstr (lrg_slot_set_get_seat_occupant (a, 3), ==, "y");
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/slot-set/seats-and-overflow", test_seats_and_overflow);
	g_test_add_func ("/slot-set/rings", test_rings);
	g_test_add_func ("/slot-set/order-independent", test_order_independent);
	g_test_add_func ("/slot-set/sticky-and-promotion", test_sticky_and_promotion);
	g_test_add_func ("/slot-set/same-calls-same-result", test_same_calls_same_result);

	return g_test_run ();
}
