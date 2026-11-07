/* test-decal.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgDecal, LrgDecalSet and LrgDecalComponent.  The
 * projection, coverage (angle and depth fades), selection order, limits
 * and grid binning are headless; the component is tested in an
 * LrgWorld3D.  With a display (run under the isolated Xvfb) a plane is
 * drawn with a shader that pastes the decal hook: a decal must change
 * the albedo inside its box only, with and without the grid, through
 * the atlas, in sort order, and respect the receiver layers.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-decal-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* make_decal: a decal of @size centred at (@x, @y, @z), projecting down */
static LrgDecal *
make_decal (gfloat x,
            gfloat y,
            gfloat z,
            gfloat size)
{
	LrgDecal *d = lrg_decal_new ();
	gfloat    pos[3];
	gfloat    sz[3];

	pos[0] = x;
	pos[1] = y;
	pos[2] = z;
	sz[0] = size;
	sz[1] = 1.0f;
	sz[2] = size;
	lrg_decal_set_trs (d, pos, NULL, sz);
	return d;
}

/* ==========================================================================
 * LrgDecal
 * ========================================================================== */

static void
count_notify (GObject    *obj,
              GParamSpec *pspec,
              gpointer    user_data)
{
	(void) obj;
	(void) pspec;
	(*(guint *) user_data)++;
}

static void
test_decal_defaults (void)
{
	g_autoptr(LrgDecal) d = lrg_decal_new ();
	gfloat              rgba[4];
	gfloat              rect[4];
	guint               notified = 0;
	guint               v;

	lrg_decal_get_color (d, rgba);
	g_assert_cmpfloat (rgba[0], ==, 1.0f);
	g_assert_cmpfloat (rgba[3], ==, 1.0f);
	g_assert_cmpfloat (lrg_decal_get_opacity (d), ==, 1.0f);
	g_assert_cmpfloat (lrg_decal_get_fade_angle (d), ==, 60.0f);
	g_assert_cmpfloat (lrg_decal_get_fade_width (d), ==, 20.0f);
	g_assert_cmpfloat (lrg_decal_get_depth_fade (d), ==, 0.0f);
	g_assert_cmpint (lrg_decal_get_sort_order (d), ==, 0);
	g_assert_cmpuint (lrg_decal_get_layer_mask (d), ==, G_MAXUINT32);
	g_assert_cmpint (lrg_decal_get_blend (d), ==, LRG_DECAL_BLEND_ALPHA);
	g_assert_cmpuint (lrg_decal_get_channels (d), ==,
	                  LRG_DECAL_CHANNEL_ALBEDO | LRG_DECAL_CHANNEL_NORMAL);
	g_assert_cmpfloat (lrg_decal_get_normal_strength (d), ==, 1.0f);
	g_assert_cmpfloat (lrg_decal_get_roughness (d), ==, 0.5f);
	g_assert_cmpfloat (lrg_decal_get_metallic (d), ==, 0.0f);
	g_assert_true (lrg_decal_get_enabled (d));
	g_assert_null (lrg_decal_get_albedo_texture (d));
	g_assert_null (lrg_decal_get_normal_texture (d));
	g_assert_null (lrg_decal_get_orm_texture (d));
	lrg_decal_get_uv_rect (d, rect);
	g_assert_cmpfloat (rect[2], ==, 1.0f);
	g_assert_cmpfloat (rect[3], ==, 1.0f);

	/* setters notify once on change and bump the version */
	g_signal_connect (d, "notify", G_CALLBACK (count_notify), &notified);
	v = lrg_decal_get_version (d);
	lrg_decal_set_opacity (d, 0.5f);
	lrg_decal_set_opacity (d, 0.5f);
	g_assert_cmpuint (notified, ==, 1);
	g_assert_cmpuint (lrg_decal_get_version (d), >, v);
	lrg_decal_set_color (d, 1.0f, 0.0f, 0.0f, 1.0f);
	g_assert_cmpuint (notified, ==, 3);
	g_object_set (d, "blend", LRG_DECAL_BLEND_MULTIPLY, "sort-order", 4, NULL);
	g_assert_cmpint (lrg_decal_get_blend (d), ==, LRG_DECAL_BLEND_MULTIPLY);
	g_assert_cmpint (lrg_decal_get_sort_order (d), ==, 4);
	lrg_decal_set_fade_angle (d, 400.0f);
	g_assert_cmpfloat (lrg_decal_get_fade_angle (d), ==, 180.0f);
	lrg_decal_set_uv_rect (d, 0.5f, 0.25f, 1.0f, 0.5f);
	lrg_decal_get_uv_rect (d, rect);
	g_assert_cmpfloat (rect[0], ==, 0.5f);
	g_assert_cmpfloat (rect[2], ==, 0.5f);   /* clamped to the texture */
	g_assert_cmpfloat (rect[3], ==, 0.5f);

	/* enum and flags types */
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_DECAL_BLEND));
	g_assert_true (G_TYPE_IS_FLAGS (LRG_TYPE_DECAL_CHANNELS));
	g_assert_nonnull (strstr (lrg_decal_set_get_glsl (), "void lrg_decals_apply("));
}

static void
test_decal_projection (void)
{
	g_autoptr(LrgDecal) d = make_decal (2.0f, 0.0f, 0.0f, 2.0f);
	gfloat              p[3] = { 2.5f, 0.0f, -0.5f };
	gfloat              l[3];
	gfloat              lo[3], hi[3];
	gfloat              q[4];
	gfloat              pos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat              size[3] = { 2.0f, 1.0f, 2.0f };
	gfloat              axis[3] = { 1.0f, 0.0f, 0.0f };

	g_assert_true (lrg_decal_project_point (d, p, l));
	g_assert_cmpfloat_with_epsilon (l[0], 0.25f, 1e-5);
	g_assert_cmpfloat_with_epsilon (l[1], 0.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (l[2], -0.25f, 1e-5);
	p[0] = 3.2f;
	g_assert_false (lrg_decal_project_point (d, p, NULL));
	p[0] = 2.0f;
	p[1] = 0.6f;     /* above the half depth */
	g_assert_false (lrg_decal_project_point (d, p, NULL));

	lrg_decal_get_world_bounds (d, lo, hi);
	g_assert_cmpfloat_with_epsilon (lo[0], 1.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (hi[0], 3.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (lo[1], -0.5f, 1e-5);
	g_assert_cmpfloat_with_epsilon (hi[2], 1.0f, 1e-5);

	/* rotated +90 degrees about X: local +Y becomes world +Z, so the
	 * decal projects along -Z onto a wall facing +Z */
	lrg_math3d_quat_from_axis_angle (axis, (gfloat) (G_PI / 2.0), q);
	lrg_decal_set_trs (d, pos, q, size);
	{
		gfloat wall[3] = { 0.0f, 0.5f, 0.0f };
		gfloat facing[3] = { 0.0f, 0.0f, 1.0f };
		gfloat floor_n[3] = { 0.0f, 1.0f, 0.0f };

		g_assert_true (lrg_decal_project_point (d, wall, l));
		g_assert_cmpfloat_with_epsilon (l[2], -0.25f, 1e-5);
		g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, wall, facing), 1.0f, 1e-5);
		g_assert_cmpfloat (lrg_decal_get_coverage (d, wall, floor_n), ==, 0.0f);
	}
}

static void
test_decal_coverage (void)
{
	g_autoptr(LrgDecal) d = make_decal (0.0f, 0.0f, 0.0f, 2.0f);
	gfloat              p[3] = { 0.0f, 0.0f, 0.0f };
	gfloat              up[3] = { 0.0f, 2.0f, 0.0f };
	gfloat              side[3] = { 1.0f, 0.0f, 0.0f };
	gfloat              tilted[3];
	gfloat              c;
	gdouble             a;

	g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, p, up), 1.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, p, NULL), 1.0f, 1e-6);
	g_assert_cmpfloat (lrg_decal_get_coverage (d, p, side), ==, 0.0f);

	/* within the fade angle: full; halfway through the fade: partial;
	 * past angle + width: none */
	a = 50.0 * G_PI / 180.0;
	tilted[0] = (gfloat) sin (a);
	tilted[1] = (gfloat) cos (a);
	tilted[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, p, tilted), 1.0f, 1e-6);
	a = 70.0 * G_PI / 180.0;
	tilted[0] = (gfloat) sin (a);
	tilted[1] = (gfloat) cos (a);
	c = lrg_decal_get_coverage (d, p, tilted);
	g_assert_cmpfloat (c, >, 0.2f);
	g_assert_cmpfloat (c, <, 0.8f);
	a = 85.0 * G_PI / 180.0;
	tilted[0] = (gfloat) sin (a);
	tilted[1] = (gfloat) cos (a);
	g_assert_cmpfloat (lrg_decal_get_coverage (d, p, tilted), ==, 0.0f);

	/* opacity and colour alpha scale it; disabled: none */
	lrg_decal_set_opacity (d, 0.5f);
	lrg_decal_set_color (d, 1.0f, 1.0f, 1.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, p, up), 0.25f, 1e-6);
	lrg_decal_set_enabled (d, FALSE);
	g_assert_cmpfloat (lrg_decal_get_coverage (d, p, up), ==, 0.0f);
	lrg_decal_set_enabled (d, TRUE);
	lrg_decal_set_opacity (d, 1.0f);
	lrg_decal_set_color (d, 1.0f, 1.0f, 1.0f, 1.0f);

	/* depth fade: full at the centre, gone at the faces */
	lrg_decal_set_depth_fade (d, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_decal_get_coverage (d, p, up), 1.0f, 1e-6);
	p[1] = 0.49f;
	c = lrg_decal_get_coverage (d, p, up);
	g_assert_cmpfloat (c, <, 0.05f);
	p[1] = 0.375f;
	c = lrg_decal_get_coverage (d, p, up);
	g_assert_cmpfloat (c, >, 0.3f);
	g_assert_cmpfloat (c, <, 0.7f);
}

/* ==========================================================================
 * LrgDecalSet
 * ========================================================================== */

static void
test_decal_set_selection (void)
{
	g_autoptr(LrgDecalSet) set = lrg_decal_set_new ();
	g_autoptr(LrgDecal)    a = make_decal (0.0f, 0.0f, 0.0f, 1.0f);
	g_autoptr(LrgDecal)    b = make_decal (1.0f, 0.0f, 0.0f, 1.0f);
	g_autoptr(LrgDecal)    c = make_decal (2.0f, 0.0f, 0.0f, 1.0f);
	g_autoptr(LrgDecal)    far = make_decal (100.0f, 0.0f, 0.0f, 1.0f);
	gdouble                ox, oy, oz;
	guint                  i;

	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 0);
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 0);

	lrg_decal_set_add (set, a);
	lrg_decal_set_add (set, b);
	lrg_decal_set_add (set, a);    /* no duplicate */
	lrg_decal_set_add (set, c);
	lrg_decal_set_add (set, far);
	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 4);
	g_assert_true (lrg_decal_set_get_decal (set, 1) == b);
	g_assert_null (lrg_decal_set_get_decal (set, 9));

	/* insertion order for equal sort orders; higher orders last */
	lrg_decal_set_sort_order (a, 5);
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 4);
	g_assert_true (lrg_decal_set_get_active_decal (set, 0) == b);
	g_assert_true (lrg_decal_set_get_active_decal (set, 1) == c);
	g_assert_true (lrg_decal_set_get_active_decal (set, 2) == far);
	g_assert_true (lrg_decal_set_get_active_decal (set, 3) == a);
	g_assert_null (lrg_decal_set_get_active_decal (set, 4));

	/* disabled, transparent and channel-less decals are skipped */
	lrg_decal_set_enabled (b, FALSE);
	lrg_decal_set_opacity (c, 0.0f);
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 2);
	lrg_decal_set_enabled (b, TRUE);
	lrg_decal_set_opacity (c, 1.0f);
	lrg_decal_set_channels (c, LRG_DECAL_CHANNEL_NONE);
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 3);
	lrg_decal_set_channels (c, LRG_DECAL_CHANNEL_ALL);

	/* the distance limit */
	lrg_decal_set_set_max_distance (set, 10.0f);
	g_assert_cmpfloat (lrg_decal_set_get_max_distance (set), ==, 10.0f);
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 3);
	lrg_decal_set_set_max_distance (set, 0.0f);

	/* remove keeps the others' order */
	g_assert_true (lrg_decal_set_remove (set, b));
	g_assert_false (lrg_decal_set_remove (set, b));
	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 3);

	/* origin, receiver */
	lrg_decal_set_set_origin (set, 1.0, 2.0, 3.0);
	lrg_decal_set_get_origin (set, &ox, &oy, &oz);
	g_assert_cmpfloat (oy, ==, 2.0);
	lrg_decal_set_set_receiver_layers (set, 0x5u);
	g_assert_cmpuint (lrg_decal_set_get_receiver_layers (set), ==, 0x5u);

	/* without a grid at most LRG_DECAL_SET_MAX_UNGRIDDED, the nearest */
	lrg_decal_set_clear (set);
	for (i = 0; i < LRG_DECAL_SET_MAX_UNGRIDDED + 10; i++)
	{
		g_autoptr(LrgDecal) d = make_decal ((gfloat) i * 2.0f, 0.0f, 0.0f, 1.0f);

		lrg_decal_set_add (set, d);
	}
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, LRG_DECAL_SET_MAX_UNGRIDDED);
	for (i = 0; i < LRG_DECAL_SET_MAX_UNGRIDDED; i++)
		g_assert_true (lrg_decal_set_get_active_decal (set, i) == lrg_decal_set_get_decal (set, i));
}

static void
test_decal_set_grid (void)
{
	g_autoptr(LrgDecalSet) set = lrg_decal_set_new ();
	g_autoptr(LrgDecal)    here = make_decal (1.0f, 0.0f, 1.0f, 1.0f);
	g_autoptr(LrgDecal)    there = make_decal (-5.0f, 0.0f, -5.0f, 1.0f);
	g_autoptr(LrgDecal)    big = make_decal (0.0f, 0.0f, 0.0f, 6.0f);
	g_autoptr(LrgDecal)    outside = make_decal (50.0f, 0.0f, 0.0f, 1.0f);
	guint                  i;

	/* 8 cells of 2 around the camera: the grid spans -8 .. 8 */
	lrg_decal_set_set_grid (set, 8, 2.0f);
	g_assert_cmpuint (lrg_decal_set_get_grid_cells (set), ==, 8);
	g_assert_cmpuint (lrg_decal_set_get_grid_slots (set), ==, 8);
	lrg_decal_set_add (set, here);
	lrg_decal_set_add (set, there);
	lrg_decal_set_add (set, big);
	lrg_decal_set_add (set, outside);
	lrg_decal_set_sort_order (big, -1);
	g_assert_cmpuint (lrg_decal_set_select (set, 0.5, 0.0, 0.5), ==, 3);

	/* the cell at (1, 1) lists big then here (sort order) */
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 1.0, 0.0, 1.0), ==, 2);
	g_assert_true (lrg_decal_set_get_cluster_decal (set, 1.0, 0.0, 1.0, 0) == big);
	g_assert_true (lrg_decal_set_get_cluster_decal (set, 1.0, 0.0, 1.0, 1) == here);
	g_assert_null (lrg_decal_set_get_cluster_decal (set, 1.0, 0.0, 1.0, 2));
	/* (-5, -5) only lists there; a far empty cell lists none */
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, -5.0, 0.0, -5.0), ==, 1);
	g_assert_true (lrg_decal_set_get_cluster_decal (set, -5.0, 0.0, -5.0, 0) == there);
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 7.0, 0.0, -7.0), ==, 0);
	/* outside the grid */
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 50.0, 0.0, 0.0), ==, 0);

	/* overflow: with 4 slots the highest sort orders stay, in order */
	lrg_decal_set_clear (set);
	lrg_decal_set_set_grid_slots (set, 4);
	for (i = 0; i < 6; i++)
	{
		g_autoptr(LrgDecal) d = make_decal (1.0f, 0.0f, 1.0f, 0.5f);

		lrg_decal_set_sort_order (d, (gint) (5 - i));
		lrg_decal_set_add (set, d);
	}
	g_assert_cmpuint (lrg_decal_set_select (set, 0.0, 0.0, 0.0), ==, 6);
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 1.0, 0.0, 1.0), ==, 4);
	for (i = 0; i < 4; i++)
		g_assert_cmpint (lrg_decal_get_sort_order (
		                     lrg_decal_set_get_cluster_decal (set, 1.0, 0.0, 1.0, i)),
		                 ==, (gint) i + 2);

	/* vertical layers: a decal at height 10 is listed in its layer only */
	lrg_decal_set_clear (set);
	lrg_decal_set_set_grid_slots (set, 8);
	lrg_decal_set_set_grid_layers (set, 4, 4.0f);
	lrg_decal_set_add (set, here);
	{
		g_autoptr(LrgDecal) high = make_decal (1.0f, 10.0f, 1.0f, 1.0f);

		lrg_decal_set_add (set, high);
		g_assert_cmpuint (lrg_decal_set_select (set, 0.0, 1.0, 0.0), ==, 2);
		g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 1.0, 0.0, 1.0), ==, 1);
		g_assert_true (lrg_decal_set_get_cluster_decal (set, 1.0, 0.0, 1.0, 0) == here);
		g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 1.0, 10.0, 1.0), ==, 1);
		g_assert_true (lrg_decal_set_get_cluster_decal (set, 1.0, 10.0, 1.0, 0) == high);
	}

	/* the grid off: no clusters */
	lrg_decal_set_set_grid (set, 0, 0.0f);
	lrg_decal_set_select (set, 0.0, 0.0, 0.0);
	g_assert_cmpuint (lrg_decal_set_get_cluster_decal_count (set, 1.0, 0.0, 1.0), ==, 0);

	/* atlas configuration */
	lrg_decal_set_set_atlas (set, 512, 2);
	g_assert_cmpuint (lrg_decal_set_get_atlas_size (set), ==, 512);
	g_assert_cmpuint (lrg_decal_set_get_atlas_tiles (set), ==, 2);

	/* no context: nothing uploads */
	if (!graphics_available)
		g_assert_false (lrg_decal_set_upload (set));
}

/* ==========================================================================
 * LrgDecalComponent
 * ========================================================================== */

static void
test_decal_component (void)
{
	g_autoptr(LrgWorld3D)        world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)       entity = lrg_entity3d_new_with_name ("decal");
	g_autoptr(LrgDecalComponent) comp = lrg_decal_component_new ();
	LrgDecalSet                 *set;
	LrgDecal                    *decal;
	LrgBoundingBox3D             box;
	gfloat                       size[3];
	gfloat                       rgba[4];
	gfloat                       lo[3], hi[3];
	guint                        notified = 0;

	decal = lrg_decal_component_get_decal (comp);
	g_assert_true (LRG_IS_DECAL (decal));
	lrg_decal_component_get_size (comp, size);
	g_assert_cmpfloat (size[0], ==, 1.0f);

	/* mirrored properties go to the decal and notify the component */
	g_signal_connect (comp, "notify::color-r", G_CALLBACK (count_notify), &notified);
	g_object_set (comp, "color-r", 0.25f, "sort-order", 3, "blend", LRG_DECAL_BLEND_ADD, NULL);
	lrg_decal_get_color (decal, rgba);
	g_assert_cmpfloat (rgba[0], ==, 0.25f);
	g_assert_cmpint (lrg_decal_get_sort_order (decal), ==, 3);
	g_assert_cmpint (lrg_decal_get_blend (decal), ==, LRG_DECAL_BLEND_ADD);
	g_assert_cmpuint (notified, ==, 1);
	lrg_decal_set_color (decal, 0.5f, 1.0f, 1.0f, 1.0f);
	g_assert_cmpuint (notified, ==, 2);
	{
		gfloat r = 0.0f;

		g_object_get (comp, "color-r", &r, NULL);
		g_assert_cmpfloat (r, ==, 0.5f);
	}

	/* size and bounds */
	g_object_set (comp, "size-x", 4.0f, "size-z", 2.0f, NULL);
	g_assert_true (lrg_entity3d_add_component (entity, LRG_COMPONENT (comp)));
	g_assert_true (lrg_entity3d_get_local_bounds (entity, &box));
	g_assert_cmpfloat (box.max.x, ==, 2.0f);
	g_assert_cmpfloat (box.max.z, ==, 1.0f);

	/* entering the world registers the decal with the world's set */
	set = lrg_decal_component_get_world_set (world);
	g_assert_true (set == lrg_decal_component_get_world_set (world));
	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 0);
	lrg_entity3d_set_position_xyz (entity, 10.0f, 1.0f, -3.0f);
	lrg_world3d_add_entity (world, entity);
	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 1);
	g_assert_true (lrg_decal_set_get_decal (set, 0) == decal);

	/* the transform follows the entity and the size */
	lrg_decal_get_world_bounds (decal, lo, hi);
	g_assert_cmpfloat_with_epsilon (lo[0], 8.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (hi[0], 12.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (lo[2], -4.0f, 1e-4);
	lrg_entity3d_set_position_xyz (entity, 0.0f, 0.0f, 0.0f);
	lrg_world3d_step (world, 1.0f / 60.0f);
	lrg_decal_get_world_bounds (decal, lo, hi);
	g_assert_cmpfloat_with_epsilon (lo[0], -2.0f, 1e-4);

	/* disabling the component or hiding the entity turns the decal off
	 * at the next sync */
	lrg_component_set_enabled (LRG_COMPONENT (comp), FALSE);
	g_assert_cmpuint (lrg_decal_component_sync_world (world, NULL, NULL), ==, 1);
	g_assert_false (lrg_decal_get_enabled (decal));
	lrg_component_set_enabled (LRG_COMPONENT (comp), TRUE);
	lrg_entity3d_set_visible (entity, FALSE);
	lrg_decal_component_sync_world (world, NULL, NULL);
	g_assert_false (lrg_decal_get_enabled (decal));
	lrg_entity3d_set_visible (entity, TRUE);
	lrg_decal_component_sync_world (world, NULL, NULL);
	g_assert_true (lrg_decal_get_enabled (decal));
	g_assert_cmpuint (lrg_decal_set_select (set, 0, 0, 0), ==, 1);

	/* paths: set, read back, cleared texture */
	lrg_decal_component_set_albedo_path (comp, "textures/missing.png");
	g_assert_cmpstr (lrg_decal_component_get_albedo_path (comp), ==, "textures/missing.png");
	lrg_decal_component_set_normal_path (comp, "n.png");
	g_assert_cmpstr (lrg_decal_component_get_normal_path (comp), ==, "n.png");
	lrg_decal_component_set_orm_path (comp, "orm.png");
	g_assert_cmpstr (lrg_decal_component_get_orm_path (comp), ==, "orm.png");
	g_assert_null (lrg_decal_component_get_asset_manager (comp));
	lrg_decal_component_set_asset_manager (comp, NULL);
	g_assert_null (lrg_decal_get_albedo_texture (decal));

	/* leaving the world unregisters */
	g_assert_true (lrg_world3d_remove_entity (world, entity));
	g_assert_cmpuint (lrg_decal_set_get_count (set), ==, 0);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define SIZE (64)

static const gchar plane_vs[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matModel;\n"
	"uniform mat4 matNormal;\n"
	"out vec3 fragPosition;\n"
	"out vec3 fragNormal;\n"
	"void main() {\n"
	"    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
	"    fragNormal = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
	"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
	"}\n";

/* the albedo after the decals: white material, lit nothing */
static const gchar plane_fs_head[] =
	"#version 330\n"
	"in vec3 fragPosition;\n"
	"in vec3 fragNormal;\n"
	"out vec4 finalColor;\n"
	"vec3 lrg_to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n";

static const gchar plane_fs_main[] =
	"void main() {\n"
	"    vec3 ng = normalize(fragNormal);\n"
	"    vec3 n = ng;\n"
	"    vec4 albedo = vec4(1.0);\n"
	"    float rough = 0.5;\n"
	"    float metal = 0.0;\n"
	"    lrg_decals_apply(fragPosition, ng, albedo, n, rough, metal);\n"
	"    finalColor = vec4(pow(clamp(albedo.rgb, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);\n"
	"}\n";

typedef struct
{
	Shader          shader;
	GrlModel       *plane;
	RenderTexture2D rt;
	Camera3D        camera;
} Gpu;

static gboolean
gpu_init (Gpu *g)
{
	g_autofree gchar   *fs = NULL;
	g_autoptr(GrlMesh)  mesh = NULL;

	fs = g_strconcat (plane_fs_head, lrg_decal_set_get_glsl (), plane_fs_main, NULL);
	g->shader = LoadShaderFromMemory (plane_vs, fs);
	if (g->shader.id == 0 || g->shader.id == rlGetShaderIdDefault ())
		return FALSE;
	mesh = grl_mesh_new_plane (10.0f, 10.0f, 1, 1);
	g->plane = grl_model_new_from_mesh (mesh);
	g->rt = LoadRenderTexture (SIZE, SIZE);

	/* straight down, orthographic, 10 units across: -Z at the top */
	memset (&g->camera, 0, sizeof g->camera);
	g->camera.position = (Vector3){ 0.0f, 10.0f, 0.0f };
	g->camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
	g->camera.up = (Vector3){ 0.0f, 0.0f, -1.0f };
	g->camera.fovy = 10.0f;
	g->camera.projection = CAMERA_ORTHOGRAPHIC;
	return TRUE;
}

static void
gpu_clear (Gpu *g)
{
	UnloadRenderTexture (g->rt);
	UnloadShader (g->shader);
	g_clear_object (&g->plane);
}

/* gpu_draw: binds @set (or the inactive state) and draws the plane */
static gint
gpu_draw (Gpu         *g,
          LrgDecalSet *set)
{
	Model   *m = grl_model_get_handle (g->plane);
	Material mat = m->materials[0];
	Matrix   t;
	gint     next = LRG_LIGHT_SET_HOOK_UNIT;

	if (set != NULL)
		next = lrg_decal_set_bind (set, g->shader.id, LRG_LIGHT_SET_HOOK_UNIT);
	else
		lrg_decal_set_bind_inactive (g->shader.id);
	memset (&t, 0, sizeof t);
	t.m0 = t.m5 = t.m10 = t.m15 = 1.0f;
	mat.shader = g->shader;
	BeginTextureMode (g->rt);
	ClearBackground (BLACK);
	BeginMode3D (g->camera);
	DrawMesh (m->meshes[0], mat, t);
	EndMode3D ();
	EndTextureMode ();
	return next;
}

/* gpu_pixel: the colour at world (@x, 0, @z); world X runs right and
 * world -Z up the picture, 6.4 pixels per unit around the centre */
static Color
gpu_pixel (Gpu   *g,
           gfloat x,
           gfloat z)
{
	Image  shot = LoadImageFromTexture (g->rt.texture);
	Color  c;
	gint   px = (gint) ((x / 10.0f + 0.5f) * SIZE);
	gint   py = (gint) ((0.5f - z / 10.0f) * SIZE);

	/* raylib renders into textures upside down and reads them back
	 * bottom row first, so image rows match screen rows here */
	c = GetImageColor (shot, CLAMP (px, 0, SIZE - 1), CLAMP (py, 0, SIZE - 1));
	UnloadImage (shot);
	return c;
}

static gboolean
is_red (Color c)
{
	return c.r > 200 && c.g < 60 && c.b < 60;
}

static gboolean
is_white (Color c)
{
	return c.r > 240 && c.g > 240 && c.b > 240;
}

static void
run_gpu_case (gboolean grid)
{
	g_autoptr(LrgDecalSet) set = lrg_decal_set_new ();
	g_autoptr(LrgDecal)    d = make_decal (1.0f, 0.0f, 1.0f, 2.0f);
	Gpu                    g;
	gint                   next;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	if (!gpu_init (&g))
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	if (grid)
		lrg_decal_set_set_grid (set, 8, 2.0f);
	lrg_decal_set_color (d, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_decal_set_add (set, d);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);

	/* inactive: all white */
	gpu_draw (&g, NULL);
	g_assert_true (is_white (gpu_pixel (&g, 1.0f, 1.0f)));

	next = gpu_draw (&g, set);
	if (next == LRG_LIGHT_SET_HOOK_UNIT)
	{
		g_test_skip ("no hook texture unit on this driver");
		gpu_clear (&g);
		return;
	}
	g_assert_cmpint (next, ==, LRG_LIGHT_SET_HOOK_UNIT + 1);   /* no atlas needed */

	/* red inside the box (0 .. 2 in X and Z), white outside */
	g_assert_true (is_red (gpu_pixel (&g, 1.0f, 1.0f)));
	g_assert_true (is_red (gpu_pixel (&g, 0.3f, 1.7f)));
	g_assert_true (is_white (gpu_pixel (&g, -1.0f, 1.0f)));
	g_assert_true (is_white (gpu_pixel (&g, 1.0f, -1.0f)));
	g_assert_true (is_white (gpu_pixel (&g, 3.0f, 3.0f)));
	g_assert_true (is_white (gpu_pixel (&g, -3.0f, -3.0f)));

	/* a receiver mask the decal does not share: white */
	lrg_decal_set_layer_mask (d, 0x2u);
	lrg_decal_set_set_receiver_layers (set, 0x1u);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	gpu_draw (&g, set);
	g_assert_true (is_white (gpu_pixel (&g, 1.0f, 1.0f)));
	lrg_decal_set_set_receiver_layers (set, 0x3u);
	gpu_draw (&g, set);
	g_assert_true (is_red (gpu_pixel (&g, 1.0f, 1.0f)));

	/* sort order: a blue decal over the red one wins only above it */
	{
		g_autoptr(LrgDecal) blue = make_decal (1.0f, 0.0f, 1.0f, 1.0f);
		Color               c;

		lrg_decal_set_color (blue, 0.0f, 0.0f, 1.0f, 1.0f);
		lrg_decal_set_sort_order (blue, -1);
		lrg_decal_set_add (set, blue);
		lrg_decal_set_select (set, 0.0, 10.0, 0.0);
		gpu_draw (&g, set);
		g_assert_true (is_red (gpu_pixel (&g, 1.0f, 1.0f)));
		lrg_decal_set_sort_order (blue, 1);
		lrg_decal_set_select (set, 0.0, 10.0, 0.0);
		gpu_draw (&g, set);
		c = gpu_pixel (&g, 1.0f, 1.0f);
		g_assert_true (c.b > 200 && c.r < 60);
		g_assert_true (is_red (gpu_pixel (&g, 0.2f, 0.2f)));
	}

	/* multiply blend: white * red = red; half opacity: pink */
	lrg_decal_set_clear (set);
	lrg_decal_set_add (set, d);
	lrg_decal_set_blend (d, LRG_DECAL_BLEND_MULTIPLY);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	gpu_draw (&g, set);
	g_assert_true (is_red (gpu_pixel (&g, 1.0f, 1.0f)));
	lrg_decal_set_opacity (d, 0.5f);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	gpu_draw (&g, set);
	{
		Color c = gpu_pixel (&g, 1.0f, 1.0f);

		g_assert_cmpuint (c.r, >, 240);
		g_assert_cmpuint (c.g, >, 100);
		g_assert_cmpuint (c.g, <, 240);
	}

	lrg_decal_set_release (set);
	gpu_clear (&g);
}

static void
test_decal_gpu_plain (void)
{
	run_gpu_case (FALSE);
}

static void
test_decal_gpu_grid (void)
{
	run_gpu_case (TRUE);
}

static void
test_decal_gpu_atlas (void)
{
	g_autoptr(LrgDecalSet) set = lrg_decal_set_new ();
	g_autoptr(LrgDecal)    d = make_decal (0.0f, 0.0f, 0.0f, 4.0f);
	g_autoptr(GrlTexture)  tex = NULL;
	Gpu                    g;
	gint                   next;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	if (!gpu_init (&g))
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}

	/* a 2 x 2 texture: top row green, bottom row blue (image rows) */
	{
		static const guint8 pixels[16] = {
			0, 255, 0, 255,   0, 255, 0, 255,
			0, 0, 255, 255,   0, 0, 255, 255
		};
		g_autoptr(GrlImage) gimg = grl_image_new_from_pixels (2, 2,
		                                                      GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
		                                                      pixels);

		tex = grl_texture_new_from_image (gimg);
	}
	g_assert_nonnull (tex);

	lrg_decal_set_set_atlas (set, 256, 2);
	lrg_decal_set_albedo_texture (d, tex);
	lrg_decal_set_add (set, d);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	g_assert_true (lrg_decal_set_upload (set));
	next = gpu_draw (&g, set);
	if (next == LRG_LIGHT_SET_HOOK_UNIT)
	{
		g_test_skip ("no hook texture unit on this driver");
		gpu_clear (&g);
		return;
	}
	g_assert_cmpint (next, ==, LRG_LIGHT_SET_HOOK_UNIT + 2);

	/* the image's top edge at local -Z: green towards -Z (up the
	 * picture), blue towards +Z */
	{
		Color top = gpu_pixel (&g, 0.0f, -1.5f);
		Color bottom = gpu_pixel (&g, 0.0f, 1.5f);

		g_test_message ("top %u %u %u, bottom %u %u %u", top.r, top.g, top.b,
		                bottom.r, bottom.g, bottom.b);
		g_assert_true (top.g > 150 && top.b < 100);
		g_assert_true (bottom.b > 150 && bottom.g < 100);
		g_assert_true (is_white (gpu_pixel (&g, 3.0f, 3.0f)));
	}

	/* a uv rect of the bottom half: all blue */
	lrg_decal_set_uv_rect (d, 0.0f, 0.5f, 1.0f, 0.5f);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	gpu_draw (&g, set);
	{
		Color top = gpu_pixel (&g, 0.0f, -1.5f);

		g_assert_true (top.b > 150 && top.g < 100);
	}

	lrg_decal_set_release (set);
	gpu_clear (&g);
}

/* the real lit shader: the light set binds the decal set itself */
static void
test_decal_gpu_light_set (void)
{
	g_autoptr(LrgDecalSet) set = lrg_decal_set_new ();
	g_autoptr(LrgDecal)    d = make_decal (1.0f, 0.0f, 1.0f, 2.0f);
	g_autoptr(LrgLightSet) lights = NULL;
	GrlColor               grey = { 200, 200, 200, 255 };
	GrlShader             *shader;
	Gpu                    g;
	Model                 *m;
	Material               mat;
	Matrix                 t;
	Color                  in, out;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	if (!gpu_init (&g))
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	lights = lrg_light_set_new ();
	shader = lrg_light_set_get_shader (lights);
	if (shader == NULL)
	{
		g_test_skip ("light set shader unavailable");
		gpu_clear (&g);
		return;
	}
	lrg_light_set_set_ambient (lights, &grey);
	lrg_decal_set_color (d, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_decal_set_add (set, d);
	lrg_decal_set_select (set, 0.0, 10.0, 0.0);
	lrg_light_set_set_decals (lights, set);
	g_assert_true (lrg_light_set_get_decals (lights) == set);

	m = grl_model_get_handle (g.plane);
	mat = m->materials[0];
	mat.shader = *(Shader *) grl_shader_get_handle (shader);
	memset (&t, 0, sizeof t);
	t.m0 = t.m5 = t.m10 = t.m15 = 1.0f;
	BeginTextureMode (g.rt);
	ClearBackground (BLACK);
	BeginMode3D (g.camera);
	g_assert_true (lrg_light_set_apply (lights, 0.0, 10.0, 0.0));
	DrawMesh (m->meshes[0], mat, t);
	EndMode3D ();
	EndTextureMode ();

	in = gpu_pixel (&g, 1.0f, 1.0f);
	out = gpu_pixel (&g, -2.0f, -2.0f);
	g_test_message ("lit: inside %u %u %u, outside %u %u %u", in.r, in.g, in.b, out.r, out.g, out.b);
	g_assert_cmpuint (out.r, >, 20);
	g_assert_cmpuint ((guint) out.r, <, (guint) out.g + 20u);   /* grey outside */
	g_assert_cmpuint (in.r, >, 20);
	g_assert_cmpuint (in.g * 4u, <, in.r);                        /* red inside */

	lrg_light_set_set_decals (lights, NULL);
	lrg_decal_set_release (set);
	gpu_clear (&g);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/decal/defaults", test_decal_defaults);
	g_test_add_func ("/decal/projection", test_decal_projection);
	g_test_add_func ("/decal/coverage", test_decal_coverage);
	g_test_add_func ("/decal-set/selection", test_decal_set_selection);
	g_test_add_func ("/decal-set/grid", test_decal_set_grid);
	g_test_add_func ("/decal-component/world", test_decal_component);
	g_test_add_func ("/decal/gpu-plain", test_decal_gpu_plain);
	g_test_add_func ("/decal/gpu-grid", test_decal_gpu_grid);
	g_test_add_func ("/decal/gpu-atlas", test_decal_gpu_atlas);
	g_test_add_func ("/decal/gpu-light-set", test_decal_gpu_light_set);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
