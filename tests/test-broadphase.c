/* test-broadphase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgBroadphase (dynamic AABB tree) and LrgPhysicsLayers.
 * Every tree result is compared against a brute-force reference over the
 * same boxes, kept by the test in a mirror array.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Mirror of the proxies for brute force checks                        */
/* ------------------------------------------------------------------ */

typedef struct
{
	gboolean live;
	gfloat   min[3];
	gfloat   max[3];
	gfloat   vel[3];
	guint    layer;
	guint32  mask;
	guint    flags;
} Mirror;

typedef struct
{
	LrgBroadphase    *bp;
	LrgPhysicsLayers *layers;
	GArray           *mirror;   /* Mirror, indexed by proxy id */
	GRand            *rand;
	gfloat            extent;
} Scene;

static void
random_box (GRand  *r,
            gfloat  extent,
            gfloat *min,
            gfloat *max)
{
	guint k;

	for (k = 0; k < 3; k++)
	{
		gfloat c = (gfloat) g_rand_double_range (r, -extent, extent);
		gfloat h = (gfloat) g_rand_double_range (r, 0.1, 1.5);

		min[k] = c - h;
		max[k] = c + h;
	}
}

static Mirror *
mirror_at (Scene *s,
           guint  id)
{
	if (id >= s->mirror->len)
		g_array_set_size (s->mirror, id + 1);
	return &g_array_index (s->mirror, Mirror, id);
}

static void
scene_init (Scene  *s,
            guint32 seed,
            gfloat  extent,
            gboolean with_layers)
{
	s->bp = lrg_broadphase_new ();
	s->layers = with_layers ? lrg_physics_layers_new_default () : NULL;
	if (s->layers != NULL)
		lrg_broadphase_set_layers (s->bp, s->layers);
	s->mirror = g_array_new (FALSE, TRUE, sizeof (Mirror));
	s->rand = g_rand_new_with_seed (seed);
	s->extent = extent;
}

static void
scene_clear (Scene *s)
{
	g_clear_object (&s->bp);
	g_clear_object (&s->layers);
	g_clear_pointer (&s->mirror, g_array_unref);
	g_clear_pointer (&s->rand, g_rand_free);
}

/* scene_add: a random box with a random layer, mask and flags */
static guint
scene_add (Scene *s)
{
	gfloat  min[3];
	gfloat  max[3];
	guint   layer = (guint) g_rand_int_range (s->rand, 0, 9);
	guint32 mask = LRG_PHYSICS_LAYERS_ALL;
	guint   roll = (guint) g_rand_int_range (s->rand, 0, 10);
	guint   flags = LRG_BROADPHASE_PROXY_NONE;
	guint   id;
	Mirror *m;
	guint   k;

	if (g_rand_int_range (s->rand, 0, 4) == 0)
		mask &= ~LRG_PHYSICS_LAYER_BIT (g_rand_int_range (s->rand, 0, 9));
	if (roll == 0)
		flags = LRG_BROADPHASE_PROXY_STATIC;
	else if (roll == 1)
		flags = LRG_BROADPHASE_PROXY_SLEEPING;

	random_box (s->rand, s->extent, min, max);
	id = lrg_broadphase_create_proxy (s->bp, min, max, layer, mask,
	                                  (LrgBroadphaseProxyFlags) flags, 0);
	g_assert_cmpuint (id, !=, LRG_BROADPHASE_NULL_PROXY);
	g_assert_true (lrg_broadphase_set_proxy_user_id (s->bp, id, id + 1000u));

	m = mirror_at (s, id);
	g_assert_false (m->live);
	m->live = TRUE;
	memcpy (m->min, min, sizeof (min));
	memcpy (m->max, max, sizeof (max));
	for (k = 0; k < 3; k++)
		m->vel[k] = (gfloat) g_rand_double_range (s->rand, -0.3, 0.3);
	m->layer = layer;
	m->mask = mask;
	m->flags = flags;
	return id;
}

/* scene_step: moves every non-static box by its velocity, bouncing off
 * the extent walls */
static void
scene_step (Scene *s)
{
	guint i;
	guint k;

	for (i = 0; i < s->mirror->len; i++)
	{
		Mirror *m = &g_array_index (s->mirror, Mirror, i);
		gfloat  d[3];

		if (!m->live || (m->flags & LRG_BROADPHASE_PROXY_STATIC) != 0)
			continue;
		for (k = 0; k < 3; k++)
		{
			if (m->min[k] < -s->extent || m->max[k] > s->extent)
				m->vel[k] = m->min[k] < -s->extent ? fabsf (m->vel[k]) : -fabsf (m->vel[k]);
			d[k] = m->vel[k];
			m->min[k] += d[k];
			m->max[k] += d[k];
		}
		lrg_broadphase_move_proxy (s->bp, i, m->min, m->max, d);
	}
}

static gboolean
overlap (const gfloat *amin, const gfloat *amax,
         const gfloat *bmin, const gfloat *bmax)
{
	guint k;

	for (k = 0; k < 3; k++)
		if (amin[k] > bmax[k] || bmin[k] > amax[k])
			return FALSE;
	return TRUE;
}

static gboolean
mirror_active (const Mirror *m)
{
	return (m->flags & (LRG_BROADPHASE_PROXY_STATIC | LRG_BROADPHASE_PROXY_SLEEPING)) == 0;
}

/* brute_pairs: the reference pair list, sorted as (a << 32 | b) */
static GArray *
brute_pairs (Scene *s)
{
	GArray *out = g_array_new (FALSE, FALSE, sizeof (guint64));
	guint   i;
	guint   j;

	for (i = 0; i < s->mirror->len; i++)
	{
		const Mirror *a = &g_array_index (s->mirror, Mirror, i);

		if (!a->live)
			continue;
		for (j = i + 1; j < s->mirror->len; j++)
		{
			const Mirror *b = &g_array_index (s->mirror, Mirror, j);
			guint64       key;

			if (!b->live || (!mirror_active (a) && !mirror_active (b)))
				continue;
			if (!overlap (a->min, a->max, b->min, b->max))
				continue;
			if (!lrg_physics_layers_should_collide (s->layers, a->layer, a->mask, b->layer, b->mask))
				continue;
			key = ((guint64) i << 32) | j;
			g_array_append_val (out, key);
		}
	}
	return out;
}

typedef struct
{
	GArray *pairs;
	gboolean bad_user;
} PairCollect;

static void
collect_pair (guint    proxy_a,
              guint    user_a,
              guint    proxy_b,
              guint    user_b,
              gpointer user_data)
{
	PairCollect *c = user_data;
	guint64      key = ((guint64) proxy_a << 32) | proxy_b;

	if (user_a != proxy_a + 1000u || user_b != proxy_b + 1000u)
		c->bad_user = TRUE;
	g_array_append_val (c->pairs, key);
}

static GArray *
tree_pairs (Scene *s)
{
	PairCollect c = { NULL, FALSE };
	guint       n;
	guint       i;

	c.pairs = g_array_new (FALSE, FALSE, sizeof (guint64));
	n = lrg_broadphase_find_pairs (s->bp, collect_pair, &c);
	g_assert_cmpuint (n, ==, c.pairs->len);
	g_assert_false (c.bad_user);
	/* strictly increasing: sorted, a < b, no duplicates */
	for (i = 0; i < c.pairs->len; i++)
	{
		guint64 k = g_array_index (c.pairs, guint64, i);

		g_assert_cmpuint ((guint) (k >> 32), <, (guint) (k & 0xFFFFFFFFu));
		if (i > 0)
			g_assert_cmpuint (k, >, g_array_index (c.pairs, guint64, i - 1));
	}
	return c.pairs;
}

static void
assert_same_pairs (GArray *a,
                   GArray *b)
{
	g_assert_cmpuint (a->len, ==, b->len);
	g_assert_cmpmem (a->data, a->len * sizeof (guint64), b->data, b->len * sizeof (guint64));
}

/* ------------------------------------------------------------------ */
/* Pairs                                                               */
/* ------------------------------------------------------------------ */

static void
test_pairs_brute_force (void)
{
	Scene s;
	guint frame;
	guint i;

	scene_init (&s, 1234u, 30.0f, TRUE);
	for (i = 0; i < 600; i++)
		scene_add (&s);
	g_assert_true (lrg_broadphase_validate (s.bp));

	for (frame = 0; frame < 120; frame++)
	{
		g_autoptr(GArray) want = NULL;
		g_autoptr(GArray) got = NULL;

		scene_step (&s);

		/* churn: destroy and create a few proxies every frame, and
		 * flip some sleeping flags */
		for (i = 0; i < 5; i++)
		{
			guint   victim = (guint) g_rand_int_range (s.rand, 0, (gint32) s.mirror->len);
			Mirror *m = &g_array_index (s.mirror, Mirror, victim);

			if (m->live)
			{
				g_assert_true (lrg_broadphase_destroy_proxy (s.bp, victim));
				m->live = FALSE;
			}
			scene_add (&s);
		}
		for (i = 0; i < 5; i++)
		{
			guint   who = (guint) g_rand_int_range (s.rand, 0, (gint32) s.mirror->len);
			Mirror *m = &g_array_index (s.mirror, Mirror, who);

			if (!m->live || (m->flags & LRG_BROADPHASE_PROXY_STATIC) != 0)
				continue;
			m->flags ^= LRG_BROADPHASE_PROXY_SLEEPING;
			g_assert_true (lrg_broadphase_set_proxy_flags (s.bp, who, (LrgBroadphaseProxyFlags) m->flags));
		}

		want = brute_pairs (&s);
		got = tree_pairs (&s);
		assert_same_pairs (want, got);
		if (frame % 20 == 0)
			g_assert_true (lrg_broadphase_validate (s.bp));
	}
	g_assert_true (lrg_broadphase_validate (s.bp));
	scene_clear (&s);
}

static void
test_pairs_flags (void)
{
	g_autoptr(LrgBroadphase) bp = lrg_broadphase_new ();
	const gfloat             min[3] = { 0, 0, 0 };
	const gfloat             max[3] = { 1, 1, 1 };
	guint                    a;
	guint                    b;
	guint                    c;

	/* three identical boxes: two static and one dynamic */
	a = lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, LRG_BROADPHASE_PROXY_STATIC, 1);
	b = lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, LRG_BROADPHASE_PROXY_STATIC, 2);
	c = lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, LRG_BROADPHASE_PROXY_NONE, 3);
	g_assert_cmpuint (a, ==, 0);
	g_assert_cmpuint (b, ==, 1);
	g_assert_cmpuint (c, ==, 2);
	/* static-static never pairs: (0,2) and (1,2) */
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 2);

	/* asleep: nothing is active */
	lrg_broadphase_set_proxy_flags (bp, c, LRG_BROADPHASE_PROXY_SLEEPING);
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 0);
	lrg_broadphase_set_proxy_flags (bp, c, LRG_BROADPHASE_PROXY_NONE);
	g_assert_cmpuint (lrg_broadphase_get_proxy_flags (bp, c), ==, LRG_BROADPHASE_PROXY_NONE);

	/* touching faces count as overlap */
	{
		const gfloat tmin[3] = { 1, 0, 0 };
		const gfloat tmax[3] = { 2, 1, 1 };

		lrg_broadphase_move_proxy (bp, c, tmin, tmax, NULL);
		g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 2);
	}

	/* ids: the lowest free slot is reused */
	g_assert_true (lrg_broadphase_destroy_proxy (bp, a));
	g_assert_false (lrg_broadphase_destroy_proxy (bp, a));
	g_assert_false (lrg_broadphase_is_proxy_valid (bp, a));
	g_assert_cmpuint (lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, 0, 9), ==, 0);
	g_assert_cmpuint (lrg_broadphase_get_proxy_user_id (bp, 0), ==, 9);

	/* invalid input */
	{
		const gfloat bad[3] = { 2, 2, 2 };
		const gfloat nan3[3] = { NAN, 0, 0 };

		g_assert_cmpuint (lrg_broadphase_create_proxy (bp, bad, max, 0, 1, 0, 0), ==, LRG_BROADPHASE_NULL_PROXY);
		g_assert_cmpuint (lrg_broadphase_create_proxy (bp, nan3, max, 0, 1, 0, 0), ==, LRG_BROADPHASE_NULL_PROXY);
		g_assert_cmpuint (lrg_broadphase_create_proxy (bp, min, max, 32, 1, 0, 0), ==, LRG_BROADPHASE_NULL_PROXY);
		g_assert_false (lrg_broadphase_move_proxy (bp, 77, min, max, NULL));
	}
	g_assert_true (lrg_broadphase_validate (bp));

	lrg_broadphase_clear (bp);
	g_assert_cmpuint (lrg_broadphase_get_proxy_count (bp), ==, 0);
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 0);
	g_assert_true (lrg_broadphase_validate (bp));
	g_assert_cmpuint (lrg_broadphase_create_proxy (bp, min, max, 0, 1, 0, 0), ==, 0);
}

static void
test_pairs_layers (void)
{
	g_autoptr(LrgBroadphase)    bp = lrg_broadphase_new ();
	g_autoptr(LrgPhysicsLayers) layers = lrg_physics_layers_new_default ();
	const gfloat                min[3] = { 0, 0, 0 };
	const gfloat                max[3] = { 1, 1, 1 };
	guint                       debris_a;
	guint                       debris_b;
	guint                       character;
	guint                       dyn;

	lrg_broadphase_set_layers (bp, layers);
	g_assert_true (lrg_broadphase_get_layers (bp) == layers);

	debris_a = lrg_broadphase_create_proxy (bp, min, max, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYERS_ALL, 0, 0);
	debris_b = lrg_broadphase_create_proxy (bp, min, max, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYERS_ALL, 0, 1);
	character = lrg_broadphase_create_proxy (bp, min, max, LRG_PHYSICS_LAYER_CHARACTER, LRG_PHYSICS_LAYERS_ALL, 0, 2);
	dyn = lrg_broadphase_create_proxy (bp, min, max, LRG_PHYSICS_LAYER_DYNAMIC, LRG_PHYSICS_LAYERS_ALL, 0, 3);

	/* debris-debris and debris-character are off by default; the rest
	 * pair: debris_a-dyn, debris_b-dyn, character-dyn */
	g_assert_false (lrg_broadphase_layers_allow (bp, debris_a, debris_b));
	g_assert_false (lrg_broadphase_layers_allow (bp, debris_a, character));
	g_assert_true (lrg_broadphase_layers_allow (bp, character, dyn));
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 3);

	/* editing the matrix applies live */
	lrg_physics_layers_set_collides (layers, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_DEBRIS, TRUE);
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 4);

	/* a per-proxy mask removes the dynamic layer from the character */
	g_assert_true (lrg_broadphase_set_proxy_layer (bp, character, LRG_PHYSICS_LAYER_CHARACTER,
	                                               LRG_PHYSICS_LAYERS_ALL & ~LRG_PHYSICS_LAYER_BIT (LRG_PHYSICS_LAYER_DYNAMIC)));
	g_assert_cmpuint (lrg_broadphase_get_proxy_layer (bp, character), ==, LRG_PHYSICS_LAYER_CHARACTER);
	g_assert_false (lrg_broadphase_layers_allow (bp, character, dyn));
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 3);

	/* masks work without a matrix too */
	lrg_broadphase_set_layers (bp, NULL);
	g_assert_cmpuint (lrg_broadphase_find_pairs (bp, NULL, NULL), ==, 5);
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

static gboolean
collect_query (guint    proxy,
               guint    user_id,
               gpointer user_data)
{
	GArray *hits = user_data;

	(void) user_id;
	g_array_append_val (hits, proxy);
	return TRUE;
}

static gint
cmp_uint (gconstpointer a,
          gconstpointer b)
{
	guint x = *(const guint *) a;
	guint y = *(const guint *) b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

static void
assert_same_ids (GArray *got,
                 GArray *want)
{
	g_array_sort (got, cmp_uint);
	g_array_sort (want, cmp_uint);
	g_assert_cmpuint (got->len, ==, want->len);
	g_assert_cmpmem (got->data, got->len * sizeof (guint), want->data, want->len * sizeof (guint));
}

typedef struct
{
	GArray *ids;
	GArray *ts;
} RayCollect;

static gfloat
collect_ray (guint    proxy,
             guint    user_id,
             gfloat   t_enter,
             gpointer user_data)
{
	RayCollect *c = user_data;

	(void) user_id;
	g_array_append_val (c->ids, proxy);
	g_array_append_val (c->ts, t_enter);
	return 1000.0f;   /* keep the whole ray */
}

/* ref_ray_box: an independent slab test (double precision) */
static gboolean
ref_ray_box (const gfloat *o,
             const gfloat *d,
             const gfloat *min,
             const gfloat *max,
             gdouble       t_max,
             gdouble      *t_enter)
{
	gdouble t0 = 0.0;
	gdouble t1 = t_max;
	guint   k;

	for (k = 0; k < 3; k++)
	{
		if (d[k] == 0.0f)
		{
			if (o[k] < min[k] || o[k] > max[k])
				return FALSE;
			continue;
		}
		{
			gdouble a = (min[k] - o[k]) / (gdouble) d[k];
			gdouble b = (max[k] - o[k]) / (gdouble) d[k];

			if (a > b)
			{
				gdouble t = a;

				a = b;
				b = t;
			}
			t0 = MAX (t0, a);
			t1 = MIN (t1, b);
		}
	}
	*t_enter = t0;
	return t0 <= t1;
}

typedef struct
{
	gfloat best;
	guint  best_id;
	guint  calls;
} Closest;

static gfloat
closest_ray (guint    proxy,
             guint    user_id,
             gfloat   t_enter,
             gpointer user_data)
{
	Closest *c = user_data;

	(void) user_id;
	c->calls++;
	if (t_enter < c->best)
	{
		c->best = t_enter;
		c->best_id = proxy;
	}
	return c->best;
}

static void
test_queries_brute_force (void)
{
	Scene  s;
	guint  q;
	guint  i;
	guint  k;

	scene_init (&s, 99u, 40.0f, FALSE);
	for (i = 0; i < 2000; i++)
		scene_add (&s);
	for (i = 0; i < 10; i++)
		scene_step (&s);

	for (q = 0; q < 50; q++)
	{
		g_autoptr(GArray) got = g_array_new (FALSE, FALSE, sizeof (guint));
		g_autoptr(GArray) want = g_array_new (FALSE, FALSE, sizeof (guint));
		gfloat            qmin[3];
		gfloat            qmax[3];
		gfloat            center[3];
		gfloat            radius = (gfloat) g_rand_double_range (s.rand, 0.0, 8.0);
		guint32           layer_mask = q % 2 == 0 ? LRG_PHYSICS_LAYERS_ALL : 0x0Fu;
		guint             n;

		/* box query */
		random_box (s.rand, 40.0f, qmin, qmax);
		for (k = 0; k < 3; k++)
		{
			qmin[k] -= 4.0f;
			qmax[k] += 4.0f;
		}
		n = lrg_broadphase_query_aabb (s.bp, qmin, qmax, layer_mask, collect_query, got);
		g_assert_cmpuint (n, ==, got->len);
		for (i = 0; i < s.mirror->len; i++)
		{
			const Mirror *m = &g_array_index (s.mirror, Mirror, i);

			if (m->live && (layer_mask & LRG_PHYSICS_LAYER_BIT (m->layer)) != 0
			    && overlap (qmin, qmax, m->min, m->max))
				g_array_append_val (want, i);
		}
		assert_same_ids (got, want);

		/* sphere query */
		g_array_set_size (got, 0);
		g_array_set_size (want, 0);
		for (k = 0; k < 3; k++)
			center[k] = (gfloat) g_rand_double_range (s.rand, -40.0, 40.0);
		lrg_broadphase_query_sphere (s.bp, center, radius, layer_mask, collect_query, got);
		for (i = 0; i < s.mirror->len; i++)
		{
			const Mirror *m = &g_array_index (s.mirror, Mirror, i);
			gdouble       d2 = 0.0;

			if (!m->live || (layer_mask & LRG_PHYSICS_LAYER_BIT (m->layer)) == 0)
				continue;
			for (k = 0; k < 3; k++)
			{
				gdouble e = MAX (0.0, MAX ((gdouble) m->min[k] - center[k], (gdouble) center[k] - m->max[k]));

				d2 += e * e;
			}
			if (d2 <= (gdouble) radius * radius)
				g_array_append_val (want, i);
		}
		assert_same_ids (got, want);

		/* ray: all candidates with their entry distances */
		{
			RayCollect rc;
			gfloat     origin[3];
			gfloat     dir[3];
			gfloat     len;

			g_array_set_size (want, 0);
			rc.ids = g_array_new (FALSE, FALSE, sizeof (guint));
			rc.ts = g_array_new (FALSE, FALSE, sizeof (gfloat));
			for (k = 0; k < 3; k++)
			{
				origin[k] = (gfloat) g_rand_double_range (s.rand, -40.0, 40.0);
				dir[k] = (gfloat) g_rand_double_range (s.rand, -1.0, 1.0);
			}
			if (q % 5 == 0)
				dir[1] = 0.0f;   /* axis-parallel slab path */
			len = sqrtf (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
			for (k = 0; k < 3; k++)
				dir[k] /= len;

			lrg_broadphase_cast_ray (s.bp, origin, dir, 60.0f, LRG_PHYSICS_LAYERS_ALL, collect_ray, &rc);
			for (i = 0; i < rc.ids->len; i++)
			{
				const Mirror *m = &g_array_index (s.mirror, Mirror, g_array_index (rc.ids, guint, i));
				gdouble       t;

				g_assert_true (ref_ray_box (origin, dir, m->min, m->max, 60.0 + 1e-3, &t));
				g_assert_cmpfloat_with_epsilon (t, g_array_index (rc.ts, gfloat, i), 1e-3);
			}
			for (i = 0; i < s.mirror->len; i++)
			{
				const Mirror *m = &g_array_index (s.mirror, Mirror, i);
				gdouble       t;

				/* skip grazing cases within float noise */
				if (m->live && ref_ray_box (origin, dir, m->min, m->max, 60.0 - 1e-3, &t))
				{
					gboolean found = FALSE;
					guint    j;

					for (j = 0; j < rc.ids->len; j++)
						found = found || g_array_index (rc.ids, guint, j) == i;
					g_assert_true (found);
				}
			}

			/* closest hit with clipping returns the smallest entry */
			{
				Closest c = { 1000.0f, G_MAXUINT, 0 };
				gfloat  best = 1000.0f;

				for (i = 0; i < rc.ts->len; i++)
					best = MIN (best, g_array_index (rc.ts, gfloat, i));
				c.best = 60.0f;
				lrg_broadphase_cast_ray (s.bp, origin, dir, 60.0f, LRG_PHYSICS_LAYERS_ALL, closest_ray, &c);
				if (rc.ts->len > 0)
					g_assert_cmpfloat (c.best, ==, best);
				g_assert_cmpuint (c.calls, <=, rc.ids->len);
			}

			g_array_unref (rc.ids);
			g_array_unref (rc.ts);
		}

		/* swept box vs Minkowski brute force */
		{
			RayCollect rc;
			gfloat     bmin[3];
			gfloat     bmax[3];
			gfloat     dir[3] = { 1.0f, 0.25f, -0.5f };
			gfloat     c[3];
			guint      expect = 0;

			rc.ids = g_array_new (FALSE, FALSE, sizeof (guint));
			rc.ts = g_array_new (FALSE, FALSE, sizeof (gfloat));
			random_box (s.rand, 40.0f, bmin, bmax);
			lrg_broadphase_cast_aabb (s.bp, bmin, bmax, dir, 30.0f, LRG_PHYSICS_LAYERS_ALL, collect_ray, &rc);
			for (k = 0; k < 3; k++)
				c[k] = 0.5f * (bmin[k] + bmax[k]);
			for (i = 0; i < s.mirror->len; i++)
			{
				const Mirror *m = &g_array_index (s.mirror, Mirror, i);
				gfloat        emin[3];
				gfloat        emax[3];
				gdouble       t;

				if (!m->live)
					continue;
				for (k = 0; k < 3; k++)
				{
					gfloat h = 0.5f * (bmax[k] - bmin[k]);

					emin[k] = m->min[k] - h;
					emax[k] = m->max[k] + h;
				}
				if (ref_ray_box (c, dir, emin, emax, 30.0 - 1e-3, &t))
					expect++;
			}
			g_assert_cmpuint (rc.ids->len, >=, expect);
			g_assert_cmpuint (rc.ids->len, <=, expect + 2);
			g_array_unref (rc.ids);
			g_array_unref (rc.ts);
		}
	}
	scene_clear (&s);
}

static void
test_query_frustum (void)
{
	g_autoptr(LrgBroadphase) bp = lrg_broadphase_new ();
	g_autoptr(GArray)        got = g_array_new (FALSE, FALSE, sizeof (guint));
	g_autoptr(GArray)        want = g_array_new (FALSE, FALSE, sizeof (guint));
	g_autoptr(GRand)         r = g_rand_new_with_seed (7);
	/* the box x,y,z in [-10, 10] as six inward planes */
	const gfloat planes[24] = {
		 1, 0, 0, 10,   -1, 0, 0, 10,
		 0, 1, 0, 10,    0, -1, 0, 10,
		 0, 0, 1, 10,    0, 0, -1, 10
	};
	const gfloat qmin[3] = { -10, -10, -10 };
	const gfloat qmax[3] = { 10, 10, 10 };
	guint i;

	for (i = 0; i < 1000; i++)
	{
		gfloat min[3];
		gfloat max[3];

		random_box (r, 30.0f, min, max);
		lrg_broadphase_create_proxy (bp, min, max, i % 4, LRG_PHYSICS_LAYERS_ALL, 0, i);
	}
	/* for an axis-aligned frustum the plane test is exact: it must equal
	 * the box query */
	lrg_broadphase_query_frustum (bp, planes, LRG_PHYSICS_LAYERS_ALL, collect_query, got);
	lrg_broadphase_query_aabb (bp, qmin, qmax, LRG_PHYSICS_LAYERS_ALL, collect_query, want);
	g_assert_cmpuint (got->len, >, 0);
	assert_same_ids (got, want);

	/* the layer mask filters, and a stopping callback stops */
	g_array_set_size (got, 0);
	g_array_set_size (want, 0);
	lrg_broadphase_query_frustum (bp, planes, LRG_PHYSICS_LAYER_BIT (1), collect_query, got);
	lrg_broadphase_query_aabb (bp, qmin, qmax, LRG_PHYSICS_LAYER_BIT (1), collect_query, want);
	assert_same_ids (got, want);
	for (i = 0; i < got->len; i++)
		g_assert_cmpuint (lrg_broadphase_get_proxy_layer (bp, g_array_index (got, guint, i)), ==, 1);
}

static gboolean
stop_first (guint proxy, guint user_id, gpointer user_data)
{
	(void) proxy;
	(void) user_id;
	(*(guint *) user_data)++;
	return FALSE;
}

static void
test_query_stop (void)
{
	g_autoptr(LrgBroadphase) bp = lrg_broadphase_new ();
	const gfloat             min[3] = { 0, 0, 0 };
	const gfloat             max[3] = { 1, 1, 1 };
	guint                    calls = 0;
	guint                    i;

	for (i = 0; i < 10; i++)
		lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, 0, i);
	g_assert_cmpuint (lrg_broadphase_query_aabb (bp, min, max, LRG_PHYSICS_LAYERS_ALL, stop_first, &calls), ==, 1);
	g_assert_cmpuint (calls, ==, 1);
}

/* ------------------------------------------------------------------ */
/* Balance, incremental moves, rebuild                                 */
/* ------------------------------------------------------------------ */

static void
test_balance (void)
{
	g_autoptr(LrgBroadphase) bp = lrg_broadphase_new ();
	LrgBroadphaseStats       st;
	guint                    n = 10000;
	guint                    i;
	guint                    bound;

	/* worst case for a naive tree: boxes inserted in sorted order */
	for (i = 0; i < n; i++)
	{
		gfloat min[3] = { (gfloat) i, 0.0f, 0.0f };
		gfloat max[3] = { (gfloat) i + 0.5f, 1.0f, 1.0f };

		lrg_broadphase_create_proxy (bp, min, max, 0, LRG_PHYSICS_LAYERS_ALL, LRG_BROADPHASE_PROXY_STATIC, i);
	}
	g_assert_true (lrg_broadphase_validate (bp));
	lrg_broadphase_get_stats (bp, &st);
	/* AVL-style rotations keep the height O(log n): log2(10000) = 13.3 */
	bound = 2u * (guint) ceil (log2 ((gdouble) n));
	g_test_message ("sorted insert: height %u (bound %u), max balance %u, rotations %" G_GUINT64_FORMAT ", area ratio %.2f",
	                st.height, bound, st.max_balance, st.rotations, (gdouble) st.area_ratio);
	g_assert_cmpuint (st.height, <=, bound);
	g_assert_cmpuint (st.proxy_count, ==, n);
	g_assert_cmpuint (st.node_count, ==, 2 * n - 1);
	g_assert_cmpuint (st.rotations, >, 0);

	/* removing every other proxy keeps it balanced */
	for (i = 0; i < n; i += 2)
		g_assert_true (lrg_broadphase_destroy_proxy (bp, i));
	g_assert_true (lrg_broadphase_validate (bp));
	g_assert_cmpuint (lrg_broadphase_get_height (bp), <=, bound);

	/* rebuild gives the minimum height */
	lrg_broadphase_rebuild (bp);
	g_assert_true (lrg_broadphase_validate (bp));
	g_assert_cmpuint (lrg_broadphase_get_height (bp), ==, (guint) ceil (log2 ((gdouble) (n / 2))));
}

static void
test_incremental_move (void)
{
	g_autoptr(LrgBroadphase) bp = lrg_broadphase_new ();
	LrgBroadphaseStats       st;
	gfloat                   min[3] = { 0, 0, 0 };
	gfloat                   max[3] = { 1, 1, 1 };
	gfloat                   fmin[3];
	gfloat                   fmax[3];
	guint                    id;
	guint                    i;

	g_assert_cmpfloat (lrg_broadphase_get_margin (bp), ==, LRG_BROADPHASE_DEFAULT_MARGIN);
	lrg_broadphase_set_margin (bp, 0.5f);
	lrg_broadphase_set_velocity_factor (bp, 4.0f);
	g_assert_cmpfloat (lrg_broadphase_get_velocity_factor (bp), ==, 4.0f);

	for (i = 0; i < 100; i++)
	{
		gfloat a[3] = { (gfloat) (i * 3), 0, 0 };
		gfloat b[3] = { (gfloat) (i * 3) + 1.0f, 1, 1 };

		lrg_broadphase_create_proxy (bp, a, b, 0, LRG_PHYSICS_LAYERS_ALL, 0, i);
	}
	id = 0;
	g_assert_true (lrg_broadphase_get_fat_aabb (bp, id, fmin, fmax));
	g_assert_cmpfloat (fmin[0], ==, -0.5f);
	g_assert_cmpfloat (fmax[0], ==, 1.5f);
	g_assert_true (lrg_broadphase_get_proxy_aabb (bp, id, fmin, fmax));
	g_assert_cmpfloat (fmax[0], ==, 1.0f);

	/* jitter inside the margin: O(1), no re-insert */
	lrg_broadphase_reset_counters (bp);
	for (i = 0; i < 1000; i++)
	{
		gfloat j = (i % 2 == 0) ? 0.2f : -0.2f;
		gfloat a[3] = { min[0] + j, min[1], min[2] };
		gfloat b[3] = { max[0] + j, max[1], max[2] };

		g_assert_false (lrg_broadphase_move_proxy (bp, id, a, b, NULL));
	}
	lrg_broadphase_get_stats (bp, &st);
	g_assert_cmpuint (st.moves, ==, 1000);
	g_assert_cmpuint (st.reinserts, ==, 0);
	g_assert_cmpuint (st.rotations, ==, 0);

	/* steady motion with a displacement: the fat box is stretched
	 * ahead, so only about one in (4 * 0.1 + 0.5) / 0.1 steps re-inserts */
	lrg_broadphase_reset_counters (bp);
	for (i = 0; i < 1000; i++)
	{
		const gfloat d[3] = { 0.0f, 0.1f, 0.0f };

		min[1] += 0.1f;
		max[1] += 0.1f;
		lrg_broadphase_move_proxy (bp, id, min, max, d);
	}
	lrg_broadphase_get_stats (bp, &st);
	g_test_message ("steady move: %" G_GUINT64_FORMAT " re-inserts in 1000 moves", st.reinserts);
	g_assert_cmpuint (st.reinserts, <=, 1000 / 8);
	g_assert_cmpuint (st.reinserts, >, 0);
	g_assert_true (lrg_broadphase_validate (bp));

	/* a teleport always re-inserts */
	{
		const gfloat a[3] = { 500, 500, 500 };
		const gfloat b[3] = { 501, 501, 501 };

		g_assert_true (lrg_broadphase_move_proxy (bp, id, a, b, NULL));
	}
	g_assert_true (lrg_broadphase_validate (bp));
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void
hash_node (guint         depth,
           const gfloat *min,
           const gfloat *max,
           guint         proxy,
           gpointer      user_data)
{
	GChecksum *sum = user_data;

	g_checksum_update (sum, (const guchar *) &depth, sizeof (depth));
	g_checksum_update (sum, (const guchar *) min, 3 * sizeof (gfloat));
	g_checksum_update (sum, (const guchar *) max, 3 * sizeof (gfloat));
	g_checksum_update (sum, (const guchar *) &proxy, sizeof (proxy));
}

static gchar *
run_scene_digest (void)
{
	Scene                 s;
	g_autoptr(GChecksum)  sum = g_checksum_new (G_CHECKSUM_SHA256);
	guint                 frame;
	guint                 i;

	scene_init (&s, 4242u, 25.0f, TRUE);
	for (i = 0; i < 400; i++)
		scene_add (&s);
	for (frame = 0; frame < 60; frame++)
	{
		g_autoptr(GArray) pairs = NULL;

		scene_step (&s);
		if (frame % 10 == 0)
		{
			lrg_broadphase_destroy_proxy (s.bp, frame);
			g_array_index (s.mirror, Mirror, frame).live = FALSE;
			scene_add (&s);
		}
		pairs = tree_pairs (&s);
		g_checksum_update (sum, (const guchar *) pairs->data, pairs->len * sizeof (guint64));
	}
	lrg_broadphase_foreach_node (s.bp, hash_node, sum);
	scene_clear (&s);
	return g_strdup (g_checksum_get_string (sum));
}

static void
test_determinism (void)
{
	g_autofree gchar *a = run_scene_digest ();
	g_autofree gchar *b = run_scene_digest ();

	g_assert_cmpstr (a, ==, b);
}

static void
count_nodes (guint depth, const gfloat *min, const gfloat *max, guint proxy, gpointer user_data)
{
	guint *counts = user_data;

	(void) depth;
	(void) min;
	(void) max;
	if (proxy == LRG_BROADPHASE_NULL_PROXY)
		counts[0]++;
	else
		counts[1]++;
}

static void
test_foreach_and_stats (void)
{
	g_autoptr(LrgBroadphase)      bp = lrg_broadphase_new ();
	g_autoptr(LrgBroadphaseStats) copy = NULL;
	LrgBroadphaseStats            st;
	guint                         counts[2] = { 0, 0 };
	guint                         i;

	for (i = 0; i < 37; i++)
	{
		gfloat a[3] = { (gfloat) i, 0, 0 };
		gfloat b[3] = { (gfloat) i + 2.0f, 1, 1 };

		lrg_broadphase_create_proxy (bp, a, b, 0, LRG_PHYSICS_LAYERS_ALL,
		                             i % 3 == 0 ? LRG_BROADPHASE_PROXY_STATIC : 0, i);
	}
	lrg_broadphase_foreach_node (bp, count_nodes, counts);
	g_assert_cmpuint (counts[0], ==, 36);
	g_assert_cmpuint (counts[1], ==, 37);
	lrg_broadphase_find_pairs (bp, NULL, NULL);
	lrg_broadphase_get_stats (bp, &st);
	g_assert_cmpuint (st.proxy_count, ==, 37);
	g_assert_cmpuint (st.active_count, ==, 24);
	g_assert_cmpuint (st.node_count, ==, 73);
	g_assert_cmpuint (st.pair_count, >, 0);
	g_assert_cmpfloat (st.area_ratio, >, 0.0f);
	copy = lrg_broadphase_stats_copy (&st);
	g_assert_cmpuint (copy->pair_count, ==, st.pair_count);
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_BROADPHASE_STATS));
	g_assert_true (G_TYPE_IS_FLAGS (LRG_TYPE_BROADPHASE_PROXY_FLAGS));
}

/* ------------------------------------------------------------------ */
/* Performance                                                         */
/* ------------------------------------------------------------------ */

static void
test_perf (void)
{
	Scene    s;
	gint64   t0;
	gint64   t_update = 0;
	gint64   t_pairs = 0;
	guint    frames = 60;
	guint    pairs = 0;
	guint    frame;
	guint    i;
	LrgBroadphaseStats st;

	/* 10k proxies in a 100 m cube, density close to a busy level */
	scene_init (&s, 777u, 100.0f, TRUE);
	t0 = g_get_monotonic_time ();
	for (i = 0; i < 10000; i++)
		scene_add (&s);
	g_test_message ("perf: build 10k proxies %.2f ms", (g_get_monotonic_time () - t0) / 1000.0);

	lrg_broadphase_reset_counters (s.bp);
	for (frame = 0; frame < frames; frame++)
	{
		t0 = g_get_monotonic_time ();
		scene_step (&s);
		t_update += g_get_monotonic_time () - t0;
		t0 = g_get_monotonic_time ();
		pairs = lrg_broadphase_find_pairs (s.bp, NULL, NULL);
		t_pairs += g_get_monotonic_time () - t0;
	}
	lrg_broadphase_get_stats (s.bp, &st);
	g_test_message ("perf: 10k proxies (%u active): update %.3f ms/frame, pairs %.3f ms/frame (%u pairs, %" G_GUINT64_FORMAT " tests), "
	                "re-inserts %.1f%%, height %u, area ratio %.1f",
	                st.active_count,
	                t_update / 1000.0 / frames, t_pairs / 1000.0 / frames, pairs, st.pair_tests,
	                100.0 * (gdouble) st.reinserts / (gdouble) MAX (st.moves, 1), st.height, (gdouble) st.area_ratio);
	if (g_test_perf ())
		g_printerr ("\n# broadphase 10k: update %.3f ms/frame, pairs %.3f ms/frame, %u pairs\n",
		            t_update / 1000.0 / frames, t_pairs / 1000.0 / frames, pairs);
	/* generous ceiling so a loaded CI box does not flake; the documented
	 * numbers come from a taskset run */
	g_assert_cmpfloat ((t_update + t_pairs) / 1000.0 / frames, <, 100.0);
	g_assert_true (lrg_broadphase_validate (s.bp));
	scene_clear (&s);
}

/* ------------------------------------------------------------------ */
/* Layers                                                              */
/* ------------------------------------------------------------------ */

static void
on_changed (LrgPhysicsLayers *layers,
            gpointer          user_data)
{
	(void) layers;
	(*(guint *) user_data)++;
}

static void
test_layers_basic (void)
{
	g_autoptr(LrgPhysicsLayers) l = lrg_physics_layers_new ();
	g_autoptr(GError)           error = NULL;
	guint                       changes = 0;
	guint                       i;

	g_signal_connect (l, "changed", G_CALLBACK (on_changed), &changes);

	/* empty table: nothing named, all collide */
	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 0);
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		g_assert_cmpuint (lrg_physics_layers_get_collide_mask (l, i), ==, LRG_PHYSICS_LAYERS_ALL);
	g_assert_null (lrg_physics_layers_get_name (l, 0));
	g_assert_null (lrg_physics_layers_get_name (l, 99));

	/* names */
	g_assert_cmpint (lrg_physics_layers_add (l, "world", &error), ==, 0);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_physics_layers_add (l, "player", &error), ==, 1);
	g_assert_true (lrg_physics_layers_set_name (l, 5, "water", &error));
	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 6);
	g_assert_cmpint (lrg_physics_layers_find (l, "water"), ==, 5);
	g_assert_cmpint (lrg_physics_layers_find (l, "nope"), ==, -1);
	g_assert_cmpint (lrg_physics_layers_add (l, "world", &error), ==, -1);
	g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_DUPLICATE);
	g_clear_error (&error);
	g_assert_false (lrg_physics_layers_set_name (l, 3, "12", &error));
	g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT);
	g_clear_error (&error);
	g_assert_false (lrg_physics_layers_set_name (l, 3, "all", &error));
	g_clear_error (&error);
	g_assert_false (lrg_physics_layers_set_name (l, 32, "x", &error));
	g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER);
	g_clear_error (&error);
	g_assert_cmpint (lrg_physics_layers_resolve (l, "player", &error), ==, 1);
	g_assert_cmpint (lrg_physics_layers_resolve (l, "17", &error), ==, 17);
	g_assert_cmpint (lrg_physics_layers_resolve (l, "40", &error), ==, -1);
	g_clear_error (&error);
	g_assert_true (lrg_physics_layers_set_name (l, 5, NULL, &error));
	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 2);

	/* the matrix stays symmetric */
	lrg_physics_layers_set_collides (l, 0, 1, FALSE);
	g_assert_false (lrg_physics_layers_get_collides (l, 0, 1));
	g_assert_false (lrg_physics_layers_get_collides (l, 1, 0));
	lrg_physics_layers_set_collide_mask (l, 3, LRG_PHYSICS_LAYER_BIT (0));
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		guint j;

		g_assert_cmpint (lrg_physics_layers_get_collides (l, 3, i), ==, i == 0);
		for (j = 0; j < LRG_PHYSICS_LAYERS_MAX; j++)
			g_assert_cmpint (lrg_physics_layers_get_collides (l, i, j), ==, lrg_physics_layers_get_collides (l, j, i));
	}

	/* the pair rule */
	g_assert_false (lrg_physics_layers_should_collide (l, 0, LRG_PHYSICS_LAYERS_ALL, 1, LRG_PHYSICS_LAYERS_ALL));
	g_assert_true (lrg_physics_layers_should_collide (l, 0, LRG_PHYSICS_LAYERS_ALL, 2, LRG_PHYSICS_LAYERS_ALL));
	g_assert_false (lrg_physics_layers_should_collide (l, 0, LRG_PHYSICS_LAYER_BIT (1), 2, LRG_PHYSICS_LAYERS_ALL));
	g_assert_true (lrg_physics_layers_should_collide (NULL, 0, LRG_PHYSICS_LAYER_BIT (2), 2, LRG_PHYSICS_LAYER_BIT (0)));
	g_assert_false (lrg_physics_layers_should_collide (NULL, 0, LRG_PHYSICS_LAYERS_ALL, 32, LRG_PHYSICS_LAYERS_ALL));

	/* change signals: one per effective edit, none for no-ops */
	{
		guint before = changes;

		lrg_physics_layers_set_collides (l, 0, 1, FALSE);
		g_assert_cmpuint (changes, ==, before);
		lrg_physics_layers_set_collides (l, 0, 1, TRUE);
		g_assert_cmpuint (changes, ==, before + 1);
	}
	g_assert_cmpuint (changes, >=, 6);

	/* masks by name */
	{
		const gchar * const names[] = { "world", "player", "7", NULL };
		const gchar * const bad[] = { "world", "ghost", NULL };
		const gchar * const all[] = { "all", NULL };
		guint32             mask = 0;
		g_auto(GStrv)       back = NULL;

		g_assert_true (lrg_physics_layers_mask_from_names (l, names, &mask, &error));
		g_assert_cmpuint (mask, ==, 0x83u);
		back = lrg_physics_layers_mask_to_names (l, mask);
		g_assert_cmpuint (g_strv_length (back), ==, 3);
		g_assert_cmpstr (back[0], ==, "world");
		g_assert_cmpstr (back[2], ==, "7");
		g_assert_false (lrg_physics_layers_mask_from_names (l, bad, &mask, &error));
		g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER);
		g_clear_error (&error);
		g_assert_true (lrg_physics_layers_mask_from_names (l, all, &mask, &error));
		g_assert_cmpuint (mask, ==, LRG_PHYSICS_LAYERS_ALL);
		g_strfreev (back);
		back = lrg_physics_layers_mask_to_names (l, LRG_PHYSICS_LAYERS_ALL);
		g_assert_cmpstr (back[0], ==, "all");
	}

	/* reset */
	lrg_physics_layers_reset (l);
	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 0);
	g_assert_true (lrg_physics_layers_get_collides (l, 0, 1));
}

static void
test_layers_default (void)
{
	g_autoptr(LrgPhysicsLayers) l = lrg_physics_layers_new_default ();
	GEnumClass                 *klass;
	guint                       i;

	klass = g_type_class_ref (LRG_TYPE_PHYSICS_LAYER);
	for (i = 0; i <= LRG_PHYSICS_LAYER_PROJECTILE; i++)
		g_assert_cmpstr (lrg_physics_layers_get_name (l, i), ==, g_enum_get_value (klass, (gint) i)->value_nick);
	g_type_class_unref (klass);

	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 9);
	g_assert_false (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_STATIC, LRG_PHYSICS_LAYER_STATIC));
	g_assert_false (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_CHARACTER));
	g_assert_false (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_TRIGGER, LRG_PHYSICS_LAYER_TRIGGER));
	g_assert_true (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_DYNAMIC, LRG_PHYSICS_LAYER_STATIC));
	g_assert_true (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_CHARACTER, LRG_PHYSICS_LAYER_TRIGGER));
	g_assert_true (lrg_physics_layers_get_collides (l, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_STATIC));
	g_assert_true (lrg_physics_layers_get_collides (l, 20, 20));
}

static void
test_layers_round_trip (void)
{
	g_autoptr(LrgPhysicsLayers) a = lrg_physics_layers_new_default ();
	g_autoptr(LrgPhysicsLayers) b = lrg_physics_layers_new ();
	g_autoptr(LrgPhysicsLayers) c = NULL;
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *text = NULL;
	g_autofree gchar           *text2 = NULL;

	/* custom edits, an unnamed gap, and an unnamed cell */
	g_assert_true (lrg_physics_layers_set_name (a, 12, "water", &error));
	lrg_physics_layers_set_collides (a, 12, LRG_PHYSICS_LAYER_DEBRIS, FALSE);
	lrg_physics_layers_set_collides (a, 20, 31, FALSE);
	lrg_physics_layers_set_collides (a, 25, 25, FALSE);

	text = lrg_physics_layers_to_data (a, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (text, "physics:"));
	g_assert_nonnull (strstr (text, "no_collide:"));
	g_test_message ("%s", text);

	g_assert_true (lrg_physics_layers_load_from_data (b, text, -1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_physics_layers_equal (a, b));
	g_assert_cmpstr (lrg_physics_layers_get_name (b, 12), ==, "water");
	g_assert_null (lrg_physics_layers_get_name (b, 10));
	g_assert_false (lrg_physics_layers_get_collides (b, 31, 20));

	/* writing the loaded table gives the same text */
	text2 = lrg_physics_layers_to_data (b, &error);
	g_assert_cmpstr (text, ==, text2);

	/* copy and assign */
	c = lrg_physics_layers_copy (a);
	g_assert_true (lrg_physics_layers_equal (a, c));
	lrg_physics_layers_reset (c);
	g_assert_false (lrg_physics_layers_equal (a, c));
	lrg_physics_layers_assign (c, a);
	g_assert_true (lrg_physics_layers_equal (a, c));
}

static void
test_layers_project_file (void)
{
	g_autoptr(LrgPhysicsLayers) l = lrg_physics_layers_new ();
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *dir = NULL;
	g_autofree gchar           *path = NULL;
	const gchar                *project =
		"project:\n"
		"  name: Demo\n"
		"  default_level: levels/a.rlevel\n"
		"physics:\n"
		"  gravity: [0, -9.81, 0]\n"
		"  layers: [default, world, '', player, enemy]\n"
		"  no_collide:\n"
		"    - [player, player]\n"
		"    - [enemy, 2]\n"
		"    - [world, world]\n";
	const gchar *nested =
		"project:\n"
		"  name: Demo\n"
		"  physics:\n"
		"    layers: [a, b]\n"
		"    no_collide: [[a, b]]\n";
	const gchar *block_only =
		"layers: [x]\n"
		"no_collide: [[x, x]]\n";
	const gchar *no_block = "project:\n  name: Demo\n";

	/* root-level physics: next to project:, unknown keys ignored */
	g_assert_true (lrg_physics_layers_load_from_data (l, project, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_physics_layers_get_name (l, 3), ==, "player");
	g_assert_null (lrg_physics_layers_get_name (l, 2));
	g_assert_false (lrg_physics_layers_get_collides (l, 3, 3));
	g_assert_false (lrg_physics_layers_get_collides (l, 2, 4));
	g_assert_false (lrg_physics_layers_get_collides (l, 1, 1));
	g_assert_true (lrg_physics_layers_get_collides (l, 3, 4));

	/* from a file */
	dir = g_dir_make_tmp ("lrg-physics-layers-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "project.ryaml", NULL);
	g_assert_true (g_file_set_contents (path, project, -1, &error));
	lrg_physics_layers_reset (l);
	g_assert_true (lrg_physics_layers_load_from_file (l, path, &error));
	g_assert_cmpint (lrg_physics_layers_find (l, "enemy"), ==, 4);
	g_assert_false (lrg_physics_layers_load_from_file (l, "/nonexistent/project.ryaml", &error));
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_clear_error (&error);
	g_unlink (path);
	g_rmdir (dir);

	/* physics: inside project: */
	g_assert_true (lrg_physics_layers_load_from_data (l, nested, -1, &error));
	g_assert_cmpint (lrg_physics_layers_find (l, "b"), ==, 1);
	g_assert_false (lrg_physics_layers_get_collides (l, 0, 1));

	/* the block alone */
	g_assert_true (lrg_physics_layers_load_from_data (l, block_only, -1, &error));
	g_assert_false (lrg_physics_layers_get_collides (l, 0, 0));

	/* no block: back to an empty, all-colliding table */
	g_assert_true (lrg_physics_layers_load_from_data (l, no_block, -1, &error));
	g_assert_cmpuint (lrg_physics_layers_get_count (l), ==, 0);
	g_assert_true (lrg_physics_layers_get_collides (l, 0, 0));
}

static void
test_layers_load_errors (void)
{
	g_autoptr(LrgPhysicsLayers) l = lrg_physics_layers_new_default ();
	g_autoptr(LrgPhysicsLayers) before = lrg_physics_layers_copy (l);
	g_autoptr(GError)           error = NULL;
	struct {
		const gchar *text;
		gint         code;
	} cases[] = {
		{ "physics: [1, 2]\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics:\n  layers: default\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics:\n  layers: [a, a]\n", LRG_PHYSICS_LAYERS_ERROR_DUPLICATE },
		{ "physics:\n  layers: [a, '5']\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics:\n  layers: [a, [b]]\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics:\n  layers: [a]\n  no_collide: [[a]]\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics:\n  layers: [a]\n  no_collide: [[a, ghost]]\n", LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER },
		{ "physics:\n  no_collide: [[0, 32]]\n", LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER },
		{ "physics:\n  no_collide: {a: b}\n", LRG_PHYSICS_LAYERS_ERROR_FORMAT },
		{ "physics: [unclosed\n", LRG_PHYSICS_LAYERS_ERROR_PARSE },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_assert_false (lrg_physics_layers_load_from_data (l, cases[i].text, -1, &error));
		g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, cases[i].code);
		g_clear_error (&error);
		/* a failed load leaves the table untouched */
		g_assert_true (lrg_physics_layers_equal (l, before));
	}

	/* 33 layers is too many */
	{
		g_autoptr(GString) s = g_string_new ("physics:\n  layers: [");

		for (i = 0; i < 33; i++)
			g_string_append_printf (s, "%sl%u", i > 0 ? ", " : "", i);
		g_string_append (s, "]\n");
		g_assert_false (lrg_physics_layers_load_from_data (l, s->str, -1, &error));
		g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT);
	}
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_PHYSICS_LAYERS_ERROR));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/broadphase/pairs/brute-force", test_pairs_brute_force);
	g_test_add_func ("/broadphase/pairs/flags", test_pairs_flags);
	g_test_add_func ("/broadphase/pairs/layers", test_pairs_layers);
	g_test_add_func ("/broadphase/query/brute-force", test_queries_brute_force);
	g_test_add_func ("/broadphase/query/frustum", test_query_frustum);
	g_test_add_func ("/broadphase/query/stop", test_query_stop);
	g_test_add_func ("/broadphase/tree/balance", test_balance);
	g_test_add_func ("/broadphase/tree/incremental-move", test_incremental_move);
	g_test_add_func ("/broadphase/tree/foreach-stats", test_foreach_and_stats);
	g_test_add_func ("/broadphase/determinism", test_determinism);
	g_test_add_func ("/broadphase/perf/10k", test_perf);
	g_test_add_func ("/physics-layers/basic", test_layers_basic);
	g_test_add_func ("/physics-layers/default", test_layers_default);
	g_test_add_func ("/physics-layers/round-trip", test_layers_round_trip);
	g_test_add_func ("/physics-layers/project-file", test_layers_project_file);
	g_test_add_func ("/physics-layers/load-errors", test_layers_load_errors);

	return g_test_run ();
}
