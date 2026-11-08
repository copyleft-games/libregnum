/* test-nav-agent.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgent, LrgNavCrowd and LrgNavAgentComponent: path following and
 * arrival, failures, off-mesh links, ORCA (two agents crossing never
 * overlap, priorities, walls), a crowd of 100 reaching its goals,
 * determinism, the component in an LrgWorld3D (destination, teleports,
 * signals) and step timing (run under taskset for stable numbers).
 */

#include <libregnum.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define STEP (1.0f / 60.0f)

/* ------------------------------------------------------------------ */
/* Meshes                                                               */

static void
floor_rect (GArray *tris,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1,
            gfloat  y)
{
	const gfloat v[18] = {
		x0, y, z0,  x1, y, z0,  x1, y, z1,
		x0, y, z0,  x1, y, z1,  x0, y, z1,
	};

	g_array_append_vals (tris, v, 18);
}

static void
box (GArray *tris,
     gfloat  x0,
     gfloat  y0,
     gfloat  z0,
     gfloat  x1,
     gfloat  y1,
     gfloat  z1)
{
	/* the top and four sides are enough to block the floor under it */
	floor_rect (tris, x0, z0, x1, z1, y1);
	{
		const gfloat s[] = {
			x0, y0, z0,  x1, y0, z0,  x1, y1, z0,   x0, y0, z0,  x1, y1, z0,  x0, y1, z0,
			x0, y0, z1,  x1, y0, z1,  x1, y1, z1,   x0, y0, z1,  x1, y1, z1,  x0, y1, z1,
			x0, y0, z0,  x0, y0, z1,  x0, y1, z1,   x0, y0, z0,  x0, y1, z1,  x0, y1, z0,
			x1, y0, z0,  x1, y0, z1,  x1, y1, z1,   x1, y0, z0,  x1, y1, z1,  x1, y1, z0,
		};

		g_array_append_vals (tris, s, G_N_ELEMENTS (s));
	}
}

static LrgNavPolyMesh *
bake_tris (GArray *tris,
           gfloat  radius)
{
	g_autoptr(LrgNavBake) bake = lrg_nav_bake_new ();
	g_autoptr(GError)     error = NULL;
	LrgNavPolyMesh       *mesh;

	lrg_nav_bake_set_agent (bake, radius, 1.8f, 50.0f, 0.45f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	return mesh;
}

/* a 40 x 40 open floor centred on the origin */
static LrgNavPolyMesh *
open_floor (void)
{
	g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));

	floor_rect (tris, -20.0f, -20.0f, 20.0f, 20.0f, 0.0f);
	return bake_tris (tris, 0.3f);
}

static gfloat
dist_xz (const gfloat *a,
         const gfloat *b)
{
	return sqrtf ((a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static void
count_cb (gpointer  instance,
          gpointer  user_data)
{
	(void) instance;
	(*(guint *) user_data)++;
}

/* ------------------------------------------------------------------ */
/* One agent                                                            */

static void
test_follow_and_arrive (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavAgent)    agent = lrg_nav_agent_new ();
	const gfloat              start[3] = { -8.0f, 0.0f, -8.0f };
	const gfloat              goal[3] = { -8.0f, 0.0f, 8.0f };
	const gfloat              ext[3] = { 0.05f, 0.5f, 0.05f };
	guint                     arrived = 0;
	guint                     n = 0;
	guint                     i;
	gfloat                    pos[3];

	/* a floor split by a wall along z = 0 with a gap at the far right */
	floor_rect (tris, -10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
	box (tris, -10.0f, 0.0f, -0.5f, 6.0f, 2.0f, 0.5f);
	mesh = bake_tris (tris, 0.3f);
	lrg_nav_agent_set_mesh (agent, mesh);
	g_assert_true (lrg_nav_agent_warp (agent, start));
	g_signal_connect (agent, "arrived", G_CALLBACK (count_cb), &arrived);

	g_assert_true (lrg_nav_agent_set_destination (agent, goal));
	g_assert_cmpint (lrg_nav_agent_get_state (agent), ==, LRG_NAV_AGENT_STATE_MOVING);
	g_assert_nonnull (lrg_nav_agent_get_path (agent, &n));
	g_assert_cmpuint (n, >=, 12);   /* around the wall's end: at least 4 points */
	g_assert_cmpfloat (lrg_nav_agent_get_remaining_distance (agent), >, 28.0f);

	for (i = 0; i < 60 * 30 && lrg_nav_agent_get_state (agent) == LRG_NAV_AGENT_STATE_MOVING; i++)
	{
		lrg_nav_agent_update (agent, STEP);
		lrg_nav_agent_get_position (agent, pos);
		/* never off the mesh */
		g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, pos, ext, NULL, NULL));
	}
	g_assert_cmpint (lrg_nav_agent_get_state (agent), ==, LRG_NAV_AGENT_STATE_ARRIVED);
	g_assert_cmpuint (arrived, ==, 1);
	lrg_nav_agent_get_position (agent, pos);
	g_assert_cmpfloat (dist_xz (pos, goal), <=, 0.1f + 1e-3f);
	/* about the path length at 3.5 m/s (plus braking) */
	g_assert_cmpfloat ((gfloat) i * STEP, <, 14.0f);

	lrg_nav_agent_stop (agent);
	g_assert_cmpint (lrg_nav_agent_get_state (agent), ==, LRG_NAV_AGENT_STATE_IDLE);
	g_assert_false (lrg_nav_agent_get_destination (agent, NULL));
}

static void
test_path_failed (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavAgent)    agent = lrg_nav_agent_new ();
	g_autoptr(LrgNavAgent)    lost = lrg_nav_agent_new ();
	const gfloat              start[3] = { 2.0f, 0.0f, 2.0f };
	const gfloat              island[3] = { 22.0f, 0.0f, 2.0f };
	guint                     failed = 0;

	floor_rect (tris, 0.0f, 0.0f, 5.0f, 5.0f, 0.0f);
	floor_rect (tris, 20.0f, 0.0f, 25.0f, 5.0f, 0.0f);
	mesh = bake_tris (tris, 0.3f);
	lrg_nav_agent_set_mesh (agent, mesh);
	lrg_nav_agent_warp (agent, start);
	g_signal_connect (agent, "path-failed", G_CALLBACK (count_cb), &failed);
	g_assert_false (lrg_nav_agent_set_destination (agent, island));
	g_assert_cmpint (lrg_nav_agent_get_state (agent), ==, LRG_NAV_AGENT_STATE_FAILED);
	g_assert_cmpuint (failed, ==, 1);
	lrg_nav_agent_update (agent, STEP);
	g_assert_cmpuint (failed, ==, 1);

	/* no mesh: fails too, and plans once a mesh arrives */
	failed = 0;
	g_signal_connect (lost, "path-failed", G_CALLBACK (count_cb), &failed);
	g_assert_false (lrg_nav_agent_set_destination (lost, start));
	g_assert_cmpuint (failed, ==, 1);
	lrg_nav_agent_set_mesh (lost, mesh);
	lrg_nav_agent_warp (lost, island);
	{
		const gfloat near[3] = { 23.0f, 0.0f, 3.0f };

		g_assert_true (lrg_nav_agent_set_destination (lost, near));
	}
	lrg_nav_agent_update (lost, STEP);
	g_assert_cmpint (lrg_nav_agent_get_state (lost), ==, LRG_NAV_AGENT_STATE_MOVING);
}

static void
test_links (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavAgent)    agent = lrg_nav_agent_new ();
	g_autoptr(GError)         error = NULL;
	const gfloat              ls[3] = { 4.5f, 0.0f, 2.0f };
	const gfloat              le[3] = { 7.5f, 3.0f, 2.0f };
	const gfloat              start[3] = { 1.0f, 0.0f, 2.0f };
	const gfloat              goal[3] = { 10.0f, 3.0f, 2.0f };
	guint                     entered = 0;
	guint                     exited = 0;
	guint                     arrived = 0;
	gboolean                  saw_on_link = FALSE;
	guint                     i;
	gfloat                    pos[3];

	floor_rect (tris, 0.0f, 0.0f, 5.0f, 4.0f, 0.0f);
	floor_rect (tris, 7.0f, 0.0f, 12.0f, 4.0f, 3.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	lrg_nav_bake_add_link (bake, ls, le, TRUE, 0.0f, NULL);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);

	lrg_nav_agent_set_mesh (agent, mesh);
	lrg_nav_agent_warp (agent, start);
	g_signal_connect (agent, "link-entered", G_CALLBACK (count_cb), &entered);
	g_signal_connect (agent, "link-exited", G_CALLBACK (count_cb), &exited);
	g_signal_connect (agent, "arrived", G_CALLBACK (count_cb), &arrived);
	g_assert_true (lrg_nav_agent_set_destination (agent, goal));
	for (i = 0; i < 60 * 20 && lrg_nav_agent_get_state (agent) != LRG_NAV_AGENT_STATE_ARRIVED; i++)
	{
		lrg_nav_agent_update (agent, STEP);
		if (lrg_nav_agent_get_state (agent) == LRG_NAV_AGENT_STATE_ON_LINK)
			saw_on_link = TRUE;
	}
	g_assert_true (saw_on_link);
	g_assert_cmpuint (entered, ==, 1);
	g_assert_cmpuint (exited, ==, 1);
	g_assert_cmpuint (arrived, ==, 1);
	lrg_nav_agent_get_position (agent, pos);
	g_assert_cmpfloat (fabsf (pos[1] - 3.0f), <, 0.2f);
	g_assert_cmpfloat (dist_xz (pos, goal), <, 0.2f);
}

/* ------------------------------------------------------------------ */
/* Crowds                                                               */

static LrgNavAgent *
add_agent (LrgNavCrowd *crowd,
           gfloat       x,
           gfloat       z,
           gfloat       gx,
           gfloat       gz)
{
	g_autoptr(LrgNavAgent) agent = lrg_nav_agent_new ();
	const gfloat           p[3] = { x, 0.0f, z };
	const gfloat           g[3] = { gx, 0.0f, gz };

	g_assert_true (lrg_nav_crowd_add_agent (crowd, agent));
	lrg_nav_agent_warp (agent, p);
	lrg_nav_agent_set_destination (agent, g);
	return agent;
}

static gfloat
min_pair_distance (LrgNavCrowd *crowd)
{
	guint  n = lrg_nav_crowd_get_n_agents (crowd);
	gfloat best = G_MAXFLOAT;
	guint  i;
	guint  j;

	for (i = 0; i < n; i++)
	{
		gfloat a[3];

		lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (crowd, i), a);
		for (j = i + 1; j < n; j++)
		{
			gfloat b[3];
			gfloat d;

			lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (crowd, j), b);
			d = dist_xz (a, b) - lrg_nav_agent_get_radius (lrg_nav_crowd_get_agent (crowd, i)) -
			    lrg_nav_agent_get_radius (lrg_nav_crowd_get_agent (crowd, j));
			best = MIN (best, d);
		}
	}
	return best;
}

static void
test_two_cross (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    crowd = lrg_nav_crowd_new (mesh);
	LrgNavAgent              *a;
	LrgNavAgent              *b;
	gfloat                    closest = G_MAXFLOAT;
	gfloat                    max_side = 0.0f;
	guint                     i;

	/* head on, exactly opposite: the worst case for symmetric avoidance */
	a = add_agent (crowd, -8.0f, 0.0f, 8.0f, 0.0f);
	b = add_agent (crowd, 8.0f, 0.0f, -8.0f, 0.0f);
	for (i = 0; i < 60 * 15; i++)
	{
		gfloat pa[3];

		lrg_nav_crowd_step (crowd, STEP);
		closest = MIN (closest, min_pair_distance (crowd));
		lrg_nav_agent_get_position (a, pa);
		max_side = MAX (max_side, fabsf (pa[2]));
		if (lrg_nav_agent_get_state (a) == LRG_NAV_AGENT_STATE_ARRIVED &&
		    lrg_nav_agent_get_state (b) == LRG_NAV_AGENT_STATE_ARRIVED)
			break;
	}
	g_test_message ("two agents: closest gap %.4f m, sidestep %.3f m, %u steps", (gdouble) closest,
	                (gdouble) max_side, i);
	/* never overlapping, and they really stepped aside */
	g_assert_cmpfloat (closest, >=, -1e-3f);
	g_assert_cmpfloat (max_side, >, 0.1f);
	g_assert_cmpint (lrg_nav_agent_get_state (a), ==, LRG_NAV_AGENT_STATE_ARRIVED);
	g_assert_cmpint (lrg_nav_agent_get_state (b), ==, LRG_NAV_AGENT_STATE_ARRIVED);
	g_assert_cmpuint (lrg_nav_crowd_get_step_count (crowd), ==, i + 1);
}

static void
test_priority (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    crowd = lrg_nav_crowd_new (mesh);
	LrgNavAgent              *vip;
	LrgNavAgent              *crowdie;
	gfloat                    vip_side = 0.0f;
	gfloat                    other_side = 0.0f;
	guint                     i;

	vip = add_agent (crowd, -8.0f, 0.05f, 8.0f, 0.05f);
	crowdie = add_agent (crowd, 8.0f, -0.05f, -8.0f, -0.05f);
	lrg_nav_agent_set_avoidance_priority (vip, 1.0f);
	lrg_nav_agent_set_avoidance_priority (crowdie, 0.0f);
	for (i = 0; i < 60 * 10; i++)
	{
		gfloat p[3];

		lrg_nav_crowd_step (crowd, STEP);
		lrg_nav_agent_get_position (vip, p);
		vip_side = MAX (vip_side, fabsf (p[2] - 0.05f));
		lrg_nav_agent_get_position (crowdie, p);
		other_side = MAX (other_side, fabsf (p[2] + 0.05f));
	}
	g_test_message ("priority: vip sidestep %.3f, other %.3f", (gdouble) vip_side, (gdouble) other_side);
	g_assert_cmpfloat (vip_side, <, other_side * 0.5f);
	g_assert_cmpfloat (min_pair_distance (crowd), >=, -1e-3f);
}

static void
test_walls (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavCrowd)    crowd = NULL;
	const gfloat              ext[3] = { 0.05f, 0.5f, 0.05f };
	guint                     i;
	guint                     k;

	/* a corridor 5 m wide with a pillar in the middle: two 1 m lanes after erosion */
	floor_rect (tris, -10.0f, -2.5f, 10.0f, 2.5f, 0.0f);
	box (tris, -0.5f, 0.0f, -0.5f, 0.5f, 2.0f, 0.5f);
	mesh = bake_tris (tris, 0.3f);
	crowd = lrg_nav_crowd_new (mesh);
	for (k = 0; k < 6; k++)
		add_agent (crowd, -8.0f + (gfloat) (k % 2), -0.6f + 0.6f * (gfloat) (k / 2), 8.0f, 0.0f);
	for (i = 0; i < 60 * 20; i++)
	{
		lrg_nav_crowd_step (crowd, STEP);
		for (k = 0; k < lrg_nav_crowd_get_n_agents (crowd); k++)
		{
			gfloat p[3];

			lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (crowd, k), p);
			g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, p, ext, NULL, NULL));
		}
		g_assert_cmpfloat (min_pair_distance (crowd), >=, -1e-3f);
	}
	/* most made it through the corridor (they crowd around one goal) */
	for (k = 0; k < lrg_nav_crowd_get_n_agents (crowd); k++)
	{
		gfloat p[3];

		lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (crowd, k), p);
		g_test_message ("agent %u at %.2f %.2f state %d", k, (gdouble) p[0], (gdouble) p[2],
		                (gint) lrg_nav_agent_get_state (lrg_nav_crowd_get_agent (crowd, k)));
	}
	for (k = 0; k < lrg_nav_crowd_get_n_agents (crowd); k++)
	{
		gfloat p[3];

		lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (crowd, k), p);
		g_assert_cmpfloat (p[0], >, 5.0f);
	}
}

/* two groups of 50 walking head on through each other, lane for lane */
static LrgNavCrowd *
make_crowd_100 (LrgNavPolyMesh *mesh)
{
	LrgNavCrowd *crowd = lrg_nav_crowd_new (mesh);
	guint        i;

	for (i = 0; i < 50; i++)
	{
		gfloat row = (gfloat) (i / 5);
		gfloat col = (gfloat) (i % 5);

		add_agent (crowd, -16.0f + col * 1.0f, -9.0f + row * 2.0f, 12.0f + col * 1.0f, -9.0f + row * 2.0f);
		add_agent (crowd, 16.0f - col * 1.0f, -9.0f + row * 2.0f, -12.0f - col * 1.0f, -9.0f + row * 2.0f);
	}
	return crowd;
}

static void
test_crowd_100 (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    crowd = make_crowd_100 (mesh);
	gfloat                    closest = G_MAXFLOAT;
	guint                     arrived = 0;
	guint                     i;
	guint                     k;

	g_assert_cmpuint (lrg_nav_crowd_get_n_agents (crowd), ==, 100);
	for (i = 0; i < 60 * 60; i++)
	{
		lrg_nav_crowd_step (crowd, STEP);
		closest = MIN (closest, min_pair_distance (crowd));
		arrived = 0;
		for (k = 0; k < 100; k++)
			if (lrg_nav_agent_get_state (lrg_nav_crowd_get_agent (crowd, k)) == LRG_NAV_AGENT_STATE_ARRIVED)
				arrived++;
		if (arrived == 100)
			break;
	}
	g_test_message ("crowd of 100: %u arrived after %.2f s, closest gap %.4f m", arrived,
	                (gdouble) ((gfloat) i * STEP), (gdouble) closest);
	g_assert_cmpuint (arrived, ==, 100);
	g_assert_cmpfloat (closest, >=, -0.01f);
}

static void
test_determinism (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    a = make_crowd_100 (mesh);
	g_autoptr(LrgNavCrowd)    b = make_crowd_100 (mesh);
	guint                     i;
	guint                     k;

	for (i = 0; i < 600; i++)
	{
		lrg_nav_crowd_step (a, STEP);
		lrg_nav_crowd_step (b, STEP);
	}
	for (k = 0; k < 100; k++)
	{
		gfloat pa[3];
		gfloat pb[3];
		gfloat va[3];
		gfloat vb[3];

		lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (a, k), pa);
		lrg_nav_agent_get_position (lrg_nav_crowd_get_agent (b, k), pb);
		lrg_nav_agent_get_velocity (lrg_nav_crowd_get_agent (a, k), va);
		lrg_nav_agent_get_velocity (lrg_nav_crowd_get_agent (b, k), vb);
		g_assert_cmpmem (pa, sizeof (pa), pb, sizeof (pb));
		g_assert_cmpmem (va, sizeof (va), vb, sizeof (vb));
	}
}

static void
test_perf (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    crowd = make_crowd_100 (mesh);
	gdouble                   total = 0.0;
	gdouble                   worst = 0.0;
	guint                     i;

	for (i = 0; i < 600; i++)
	{
		lrg_nav_crowd_step (crowd, STEP);
		total += lrg_nav_crowd_get_last_step_seconds (crowd);
		worst = MAX (worst, lrg_nav_crowd_get_last_step_seconds (crowd));
	}
	g_test_message ("perf: 100 agents, 600 steps: mean %.3f ms, worst %.3f ms per step",
	                total / 600.0 * 1000.0, worst * 1000.0);
	g_assert_cmpfloat (total / 600.0, <, 0.05);
}

static void
test_crowd_membership_and_debug (void)
{
	g_autoptr(LrgNavPolyMesh) mesh = open_floor ();
	g_autoptr(LrgNavCrowd)    crowd = lrg_nav_crowd_new (NULL);
	g_autoptr(LrgNavCrowd)    other = lrg_nav_crowd_new (NULL);
	g_autoptr(LrgDebugDraw)   draw = lrg_debug_draw_new ();
	g_autoptr(LrgNavAgent)    agent = lrg_nav_agent_new ();
	guint                     stepped = 0;
	guint                     lines_mesh;

	g_assert_true (lrg_nav_crowd_add_agent (crowd, agent));
	g_assert_false (lrg_nav_crowd_add_agent (other, agent));
	g_assert_true (lrg_nav_agent_get_crowd (agent) == crowd);
	lrg_nav_crowd_set_mesh (crowd, mesh);
	g_assert_true (lrg_nav_agent_get_mesh (agent) == mesh);
	add_agent (crowd, 5.0f, 0.0f, -5.0f, 0.0f);
	{
		const gfloat p[3] = { -5.0f, 0.0f, 0.0f };
		const gfloat g[3] = { 5.0f, 0.0f, 0.0f };

		lrg_nav_agent_warp (agent, p);
		lrg_nav_agent_set_destination (agent, g);
	}
	g_signal_connect (crowd, "stepped", G_CALLBACK (count_cb), &stepped);
	lrg_nav_crowd_step (crowd, STEP);
	lrg_nav_crowd_step (crowd, STEP);
	g_assert_cmpuint (stepped, ==, 2);

	lrg_nav_crowd_debug_draw (crowd, draw, LRG_NAV_DEBUG_POLYGONS);
	lines_mesh = lrg_debug_draw_get_line_count (draw, TRUE) + lrg_debug_draw_get_triangle_count (draw, TRUE);
	lrg_debug_draw_end_frame (draw);
	lrg_nav_crowd_debug_draw (crowd, draw, LRG_NAV_DEBUG_ALL);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (draw, TRUE) + lrg_debug_draw_get_triangle_count (draw, TRUE),
	                  >, lines_mesh);
	lrg_debug_draw_end_frame (draw);

	g_assert_true (lrg_nav_crowd_remove_agent (crowd, agent));
	g_assert_null (lrg_nav_agent_get_crowd (agent));
	g_assert_false (lrg_nav_crowd_remove_agent (crowd, agent));
	g_assert_cmpuint (lrg_nav_crowd_get_n_agents (crowd), ==, 1);
	lrg_nav_crowd_clear (crowd);
	g_assert_cmpuint (lrg_nav_crowd_get_n_agents (crowd), ==, 0);
	g_assert_null (lrg_nav_crowd_get_agent (crowd, 0));
	g_assert_true (g_type_is_a (LRG_TYPE_NAV_AGENT_STATE, G_TYPE_ENUM));
}

/* ------------------------------------------------------------------ */
/* The component in a world                                             */

static void
test_component (void)
{
	g_autoptr(LrgWorld3D)           world = lrg_world3d_new ();
	g_autoptr(LrgNavPolyMesh)       mesh = open_floor ();
	g_autoptr(LrgNavAgentComponent) comp = lrg_nav_agent_component_new ();
	g_autoptr(LrgDebugDraw)         draw = lrg_debug_draw_new ();
	LrgEntity3D                    *e = lrg_entity3d_new_with_name ("walker");
	const gfloat                    goal[3] = { 6.0f, 0.0f, 0.0f };
	gfloat                          pos[3];
	gfloat                          q[4];
	guint                           arrived = 0;
	guint                           i;

	/* a destination before the world (and its mesh): kept */
	g_object_set (comp, "speed", 4.0f, "base-offset", 0.9f, NULL);
	g_assert_cmpfloat (lrg_nav_agent_get_max_speed (lrg_nav_agent_component_get_agent (comp)), ==, 4.0f);
	g_assert_true (lrg_nav_agent_component_set_destination (comp, goal));
	g_assert_true (lrg_nav_agent_component_get_destination (comp, pos));
	g_signal_connect (comp, "arrived", G_CALLBACK (count_cb), &arrived);
	lrg_entity3d_add_component (e, LRG_COMPONENT (comp));
	lrg_entity3d_set_position_xyz (e, -6.0f, 0.9f, 0.0f);
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	g_assert_cmpuint (lrg_nav_crowd_get_n_agents (lrg_world3d_get_nav_crowd (world)), ==, 1);

	/* the mesh arrives: the agent plans on the next fixed update */
	lrg_world3d_set_navigation (world, mesh);
	g_assert_true (lrg_world3d_get_navigation (world) == mesh);
	for (i = 0; i < 60 * 8 && arrived == 0; i++)
		lrg_world3d_fixed_update (world, lrg_world3d_get_fixed_step (world));
	g_assert_cmpuint (arrived, ==, 1);
	g_assert_true (lrg_nav_agent_component_has_arrived (comp));
	g_assert_cmpint (lrg_nav_agent_component_get_state (comp), ==, LRG_NAV_AGENT_STATE_ARRIVED);
	lrg_entity3d_get_world_position (e, pos);
	g_assert_cmpfloat (fabsf (pos[0] - 6.0f), <, 0.15f);
	g_assert_cmpfloat (fabsf (pos[1] - 0.9f), <, 1e-3f);   /* base offset */
	/* facing +X (yaw 90 degrees) */
	lrg_entity3d_get_rotation (e, q);
	g_assert_cmpfloat (fabsf (fabsf (q[1]) - sinf ((gfloat) G_PI / 4.0f)), <, 0.05f);

	/* game code teleports the entity: the agent follows */
	lrg_entity3d_set_position_xyz (e, 0.0f, 0.9f, 10.0f);
	lrg_world3d_fixed_update (world, lrg_world3d_get_fixed_step (world));
	lrg_nav_agent_get_position (lrg_nav_agent_component_get_agent (comp), pos);
	g_assert_cmpfloat (fabsf (pos[2] - 10.0f), <, 0.05f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_remaining_distance (comp), ==, 0.0f);

	/* debug draw through the world */
	lrg_world3d_debug_draw_navigation (world, draw, LRG_NAV_DEBUG_DEFAULT);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (draw, TRUE), >, 0);

	/* accessors mirror the agent */
	lrg_nav_agent_component_set_radius (comp, 0.5f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_radius (comp), ==, 0.5f);
	g_assert_cmpfloat (lrg_nav_agent_get_radius (lrg_nav_agent_component_get_agent (comp)), ==, 0.5f);
	lrg_nav_agent_component_set_height (comp, 2.0f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_height (comp), ==, 2.0f);
	lrg_nav_agent_component_set_acceleration (comp, 20.0f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_acceleration (comp), ==, 20.0f);
	lrg_nav_agent_component_set_avoidance_priority (comp, 2.0f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_avoidance_priority (comp), ==, 1.0f);
	lrg_nav_agent_component_set_speed (comp, 1.0f);
	g_assert_cmpfloat (lrg_nav_agent_component_get_speed (comp), ==, 1.0f);
	lrg_nav_agent_component_stop (comp);
	g_assert_cmpint (lrg_nav_agent_component_get_state (comp), ==, LRG_NAV_AGENT_STATE_IDLE);

	/* leaving the world leaves the crowd */
	lrg_entity3d_remove_component (e, LRG_COMPONENT (comp));
	g_assert_cmpuint (lrg_nav_crowd_get_n_agents (lrg_world3d_get_nav_crowd (world)), ==, 0);
}

/* destination-x/y/z merge one axis at a time. target-node, loaded from
 * a level, sends the agent to that entity and wins over destination-x. */
static void
test_destination_properties (void)
{
	static const gchar *yaml =
		"type: level\n"
		"name: nav-goals\n"
		"default_view: 3d\n"
		"nodes:\n"
		"  - name: Marker\n"
		"    transform:\n"
		"      location: [3.0, 1.0, 5.0]\n"
		"  - name: Walker\n"
		"    components:\n"
		"      - type: nav-agent\n"
		"        props:\n"
		"          destination-x: 8.0\n"
		"          destination-z: 2.0\n"
		"  - name: Follower\n"
		"    components:\n"
		"      - type: nav-agent\n"
		"        props:\n"
		"          destination-x: 1.0\n"
		"          target-node: Marker\n";
	g_autoptr(LrgNavAgentComponent) alone = lrg_nav_agent_component_new ();
	g_autoptr(LrgWorld3D)           world = lrg_world3d_new ();
	g_autoptr(LrgLevel)             level = NULL;
	g_autoptr(GError)               error = NULL;
	g_autofree gchar               *dir = NULL;
	g_autofree gchar               *path = NULL;
	LrgEntity3D                    *walker;
	LrgEntity3D                    *follower;
	LrgNavAgentComponent           *wcomp;
	LrgNavAgentComponent           *fcomp;
	gfloat                          dest[3];

	g_object_set (alone, "destination-x", 8.0f, NULL);
	g_object_set (alone, "destination-z", 2.0f, NULL);
	g_assert_true (lrg_nav_agent_component_get_destination (alone, dest));
	g_assert_cmpfloat (dest[0], ==, 8.0f);
	g_assert_cmpfloat (dest[1], ==, 0.0f);
	g_assert_cmpfloat (dest[2], ==, 2.0f);

	/* builtins, including nav-agent, register during startup */
	g_assert_true (lrg_engine_startup (lrg_engine_get_default (), &error));
	g_assert_no_error (error);
	dir = g_dir_make_tmp ("lrg-nav-agent-XXXXXX", NULL);
	g_assert_nonnull (dir);
	path = g_build_filename (dir, "goals.rlevel", NULL);
	g_assert_true (g_file_set_contents (path, yaml, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_load_world3d (path, world, &error);
	g_assert_no_error (error);
	g_assert_nonnull (level);

	walker = lrg_world3d_find_by_name (world, "Walker");
	follower = lrg_world3d_find_by_name (world, "Follower");
	g_assert_nonnull (walker);
	g_assert_nonnull (follower);
	wcomp = LRG_NAV_AGENT_COMPONENT (lrg_entity3d_get_component (walker, LRG_TYPE_NAV_AGENT_COMPONENT));
	fcomp = LRG_NAV_AGENT_COMPONENT (lrg_entity3d_get_component (follower, LRG_TYPE_NAV_AGENT_COMPONENT));
	g_assert_nonnull (wcomp);
	g_assert_nonnull (fcomp);
	g_assert_true (lrg_nav_agent_component_get_destination (wcomp, dest));
	g_assert_cmpfloat (dest[0], ==, 8.0f);
	g_assert_cmpfloat (dest[1], ==, 0.0f);
	g_assert_cmpfloat (dest[2], ==, 2.0f);
	g_assert_true (lrg_nav_agent_component_get_destination (fcomp, dest));
	g_assert_cmpfloat (dest[0], ==, 3.0f);
	g_assert_cmpfloat (dest[1], ==, 1.0f);
	g_assert_cmpfloat (dest[2], ==, 5.0f);

	g_unlink (path);
	g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/nav-agent/follow-and-arrive", test_follow_and_arrive);
	g_test_add_func ("/nav-agent/path-failed", test_path_failed);
	g_test_add_func ("/nav-agent/links", test_links);
	g_test_add_func ("/nav-agent/orca/two-cross", test_two_cross);
	g_test_add_func ("/nav-agent/orca/priority", test_priority);
	g_test_add_func ("/nav-agent/orca/walls", test_walls);
	g_test_add_func ("/nav-agent/crowd/100-reach-goals", test_crowd_100);
	g_test_add_func ("/nav-agent/crowd/determinism", test_determinism);
	g_test_add_func ("/nav-agent/crowd/perf", test_perf);
	g_test_add_func ("/nav-agent/crowd/membership-and-debug", test_crowd_membership_and_debug);
	g_test_add_func ("/nav-agent/component", test_component);
	g_test_add_func ("/nav-agent/destination-properties", test_destination_properties);

	return g_test_run ();
}
