/* test-entity3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgEntity3D: identity and flags, local TRS, the
 * authored-Euler round trip, hierarchy operations, cached world matrices
 * (including keep-world reparenting and lazy dirty propagation), ordered
 * components with the 0.9.0 LrgComponent entity hooks, and signals.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4f)
#define DEG(d) ((gfloat)((d) * G_PI / 180.0))

/* ==========================================================================
 * A recording test component
 * ========================================================================== */

#define TEST_TYPE_REC_COMPONENT (test_rec_component_get_type ())
G_DECLARE_FINAL_TYPE (TestRecComponent, test_rec_component, TEST, REC_COMPONENT, LrgComponent)

struct _TestRecComponent
{
	LrgComponent parent_instance;
	gchar       *tag;
	GString     *log;    /* shared, not owned */
};

G_DEFINE_FINAL_TYPE (TestRecComponent, test_rec_component, LRG_TYPE_COMPONENT)

static void
rec_attached_entity (LrgComponent *self,
                     LrgEntity3D  *entity)
{
	TestRecComponent *rc = TEST_REC_COMPONENT (self);

	/* The owner link is already set when attached_entity runs. */
	g_assert_true (lrg_component_get_entity (self) == entity);
	if (rc->log != NULL)
		g_string_append_printf (rc->log, "attach:%s;", rc->tag);
}

static void
rec_detached_entity (LrgComponent *self,
                     LrgEntity3D  *entity)
{
	TestRecComponent *rc = TEST_REC_COMPONENT (self);

	/* ... and still set when detached_entity runs. */
	g_assert_true (lrg_component_get_entity (self) == entity);
	if (rc->log != NULL)
		g_string_append_printf (rc->log, "detach:%s;", rc->tag);
}

static void
rec_finalize (GObject *object)
{
	g_free (TEST_REC_COMPONENT (object)->tag);
	G_OBJECT_CLASS (test_rec_component_parent_class)->finalize (object);
}

static void
test_rec_component_class_init (TestRecComponentClass *klass)
{
	LrgComponentClass *cc = LRG_COMPONENT_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = rec_finalize;
	cc->attached_entity = rec_attached_entity;
	cc->detached_entity = rec_detached_entity;
}

static void
test_rec_component_init (TestRecComponent *self)
{
}

static TestRecComponent *
rec_new (const gchar *tag,
         GString     *log)
{
	TestRecComponent *rc;

	rc = g_object_new (TEST_TYPE_REC_COMPONENT, NULL);
	rc->tag = g_strdup (tag);
	rc->log = log;
	return rc;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
assert_vec3 (const gfloat *v,
             gfloat        x,
             gfloat        y,
             gfloat        z)
{
	g_assert_cmpfloat_with_epsilon (v[0], x, EPS);
	g_assert_cmpfloat_with_epsilon (v[1], y, EPS);
	g_assert_cmpfloat_with_epsilon (v[2], z, EPS);
}

static void
assert_mat_eq (const gfloat *a,
               const gfloat *b)
{
	guint i;

	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (a[i], b[i], EPS);
}

/* Counter for signals without arguments. */
static void
count_cb (gpointer  instance,
          gpointer  user_data)
{
	(*(guint *)user_data)++;
}

/* Counter for signals with one object argument. */
static void
count1_cb (gpointer  instance,
           gpointer  arg,
           gpointer  user_data)
{
	(*(guint *)user_data)++;
}

/* ==========================================================================
 * Identity and flags
 * ========================================================================== */

static void
test_identity_defaults (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	gfloat v[4];

	g_assert_null (lrg_entity3d_get_guid (e));
	g_assert_null (lrg_entity3d_get_name (e));
	g_assert_true (lrg_entity3d_get_active (e));
	g_assert_true (lrg_entity3d_get_visible (e));
	g_assert_false (lrg_entity3d_get_static (e));
	g_assert_cmpuint (lrg_entity3d_get_layer (e), ==, LRG_RENDER_LAYERS_DEFAULT);

	lrg_entity3d_get_position (e, v);
	assert_vec3 (v, 0, 0, 0);
	lrg_entity3d_get_scale (e, v);
	assert_vec3 (v, 1, 1, 1);
	lrg_entity3d_get_rotation (e, v);
	g_assert_cmpfloat_with_epsilon (v[3], 1.0f, EPS);
}

static void
test_identity_guid_once (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new_with_name ("crate");
	g_autoptr(LrgEntity3D) f = lrg_entity3d_new ();
	const gchar *g;

	g_assert_cmpstr (lrg_entity3d_get_name (e), ==, "crate");
	g_assert_true (lrg_entity3d_set_guid (e, "guid-1"));
	g_assert_true (lrg_entity3d_set_guid (e, "guid-1"));   /* same value */

	g_test_expect_message ("Libregnum-World3D", G_LOG_LEVEL_WARNING, "*refusing*");
	g_assert_false (lrg_entity3d_set_guid (e, "guid-2"));
	g_test_assert_expected_messages ();
	g_assert_cmpstr (lrg_entity3d_get_guid (e), ==, "guid-1");

	g = lrg_entity3d_ensure_guid (f);
	g_assert_true (g_uuid_string_is_valid (g));
	g_assert_true (lrg_entity3d_ensure_guid (f) == g);
}

static void
test_identity_flags_tags_layer (void)
{
	g_autoptr(LrgEntity3D) p = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) c = lrg_entity3d_new ();
	g_auto(GStrv) tags = NULL;

	lrg_entity3d_add_child (p, c);

	lrg_entity3d_set_active (p, FALSE);
	g_assert_true (lrg_entity3d_get_active (c));
	g_assert_false (lrg_entity3d_is_active_in_hierarchy (c));
	lrg_entity3d_set_active (p, TRUE);
	g_assert_true (lrg_entity3d_is_active_in_hierarchy (c));

	lrg_entity3d_set_visible (p, FALSE);
	g_assert_false (lrg_entity3d_is_visible_in_hierarchy (c));

	lrg_entity3d_set_static (c, TRUE);
	g_assert_true (lrg_entity3d_get_static (c));

	lrg_entity3d_set_layer (c, LRG_RENDER_LAYERS_WATER);
	g_assert_cmpuint (lrg_entity3d_get_layer_mask (c), ==,
	                  LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_WATER));

	lrg_entity3d_add_tag (c, "pickup");
	lrg_entity3d_add_tag (c, "enemy");
	lrg_entity3d_add_tag (c, "enemy");
	g_assert_true (lrg_entity3d_has_tag (c, "enemy"));
	tags = lrg_entity3d_get_tags (c);
	g_assert_cmpuint (g_strv_length (tags), ==, 2);
	g_assert_cmpstr (tags[0], ==, "enemy");
	g_assert_cmpstr (tags[1], ==, "pickup");
	g_assert_true (lrg_entity3d_remove_tag (c, "enemy"));
	g_assert_false (lrg_entity3d_remove_tag (c, "enemy"));
	g_assert_false (lrg_entity3d_has_tag (c, "enemy"));
}

/* ==========================================================================
 * Local transform and Euler round trip
 * ========================================================================== */

static void
test_trs_local_matrix (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	gfloat euler[3] = { DEG (10), DEG (20), DEG (30) };
	gfloat t[3] = { 1, 2, 3 };
	gfloat s[3] = { 2, 3, 4 };
	gfloat q[4];
	gfloat expect[16];
	gfloat got[16];

	lrg_entity3d_set_position (e, t);
	lrg_entity3d_set_rotation_euler (e, euler);
	lrg_entity3d_set_scale (e, s);

	lrg_math3d_compose (t, euler, s, expect);
	lrg_entity3d_get_local_matrix (e, got);
	assert_mat_eq (got, expect);

	/* No parent: world == local. */
	lrg_entity3d_get_world_matrix (e, got);
	assert_mat_eq (got, expect);

	/* set_local_matrix decomposes back to the same TRS. */
	{
		g_autoptr(LrgEntity3D) f = lrg_entity3d_new ();
		gfloat v[3];

		lrg_entity3d_set_local_matrix (f, expect);
		lrg_entity3d_get_position (f, v);
		assert_vec3 (v, 1, 2, 3);
		lrg_entity3d_get_scale (f, v);
		assert_vec3 (v, 2, 3, 4);
		lrg_entity3d_get_rotation (f, q);
		lrg_entity3d_get_rotation (e, got);
		/* q and -q are the same rotation. */
		g_assert_cmpfloat_with_epsilon (fabsf (q[0] * got[0] + q[1] * got[1]
		                                       + q[2] * got[2] + q[3] * got[3]),
		                                1.0f, EPS);
	}
}

static void
test_euler_authored_roundtrip (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	gfloat authored[3] = { DEG (370), DEG (-190), DEG (720) };
	gfloat out[3];
	gfloat q[4];
	gfloat expect[3];
	gfloat qa[4] = { 0.0f, (gfloat)sin (G_PI / 8.0), 0.0f, (gfloat)cos (G_PI / 8.0) };

	/* Authored angles come back bit-exact, even out of range. */
	lrg_entity3d_set_rotation_euler (e, authored);
	g_assert_true (lrg_entity3d_has_authored_euler (e));
	lrg_entity3d_get_rotation_euler (e, out);
	g_assert_cmpmem (out, sizeof (out), authored, sizeof (authored));

	/* A quaternion write forgets them; the Euler is then decomposed. */
	lrg_entity3d_set_rotation (e, qa);
	g_assert_false (lrg_entity3d_has_authored_euler (e));
	lrg_entity3d_get_rotation_euler (e, out);
	lrg_entity3d_get_rotation (e, q);
	lrg_math3d_quat_to_euler (q, expect);
	assert_vec3 (out, expect[0], expect[1], expect[2]);
	g_assert_cmpfloat_with_epsilon (out[1], DEG (45), EPS);

	/* Position/scale changes keep the authored Euler. */
	lrg_entity3d_set_rotation_euler (e, authored);
	lrg_entity3d_set_position_xyz (e, 5, 5, 5);
	lrg_entity3d_set_scale_xyz (e, 2, 2, 2);
	g_assert_true (lrg_entity3d_has_authored_euler (e));

	/* set_local_trs forgets it. */
	{
		gfloat t[3] = { 0, 0, 0 };
		gfloat s[3] = { 1, 1, 1 };

		lrg_entity3d_set_local_trs (e, t, qa, s);
		g_assert_false (lrg_entity3d_has_authored_euler (e));
	}

	/* A zero quaternion becomes the identity, not NaN. */
	{
		gfloat zero[4] = { 0, 0, 0, 0 };

		lrg_entity3d_set_rotation (e, zero);
		lrg_entity3d_get_rotation (e, q);
		g_assert_cmpfloat_with_epsilon (q[3], 1.0f, EPS);
	}
}

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

static void
test_hierarchy_ops (void)
{
	g_autoptr(LrgEntity3D) root = lrg_entity3d_new_with_name ("root");
	LrgEntity3D *a = lrg_entity3d_new_with_name ("a");
	LrgEntity3D *b = lrg_entity3d_new_with_name ("b");
	LrgEntity3D *c = lrg_entity3d_new_with_name ("c");
	LrgEntity3D *d = lrg_entity3d_new_with_name ("d");
	g_autoptr(GPtrArray) kids = NULL;
	g_autofree gchar *path = NULL;

	/* Parent takes a ref; drop ours after linking (b keeps ours). */
	lrg_entity3d_add_child (root, a);
	lrg_entity3d_add_child (root, c);
	lrg_entity3d_insert_child (root, b, 1);
	lrg_entity3d_add_child (b, d);
	g_object_unref (a);
	g_object_unref (c);
	g_object_unref (d);

	g_assert_cmpuint (lrg_entity3d_get_n_children (root), ==, 3);
	g_assert_true (lrg_entity3d_get_child (root, 0) == a);
	g_assert_true (lrg_entity3d_get_child (root, 1) == b);
	g_assert_true (lrg_entity3d_get_child (root, 2) == c);
	g_assert_null (lrg_entity3d_get_child (root, 3));
	g_assert_cmpint (lrg_entity3d_get_index (c), ==, 2);
	g_assert_cmpint (lrg_entity3d_get_index (root), ==, -1);
	g_assert_true (lrg_entity3d_get_parent (d) == b);
	g_assert_true (lrg_entity3d_get_root (d) == root);
	g_assert_true (lrg_entity3d_is_ancestor_of (root, d));
	g_assert_false (lrg_entity3d_is_ancestor_of (d, root));
	g_assert_false (lrg_entity3d_is_ancestor_of (d, d));

	kids = lrg_entity3d_get_children (root);
	g_assert_cmpuint (kids->len, ==, 3);

	/* Sibling order. */
	lrg_entity3d_set_sibling_index (c, 0);
	g_assert_true (lrg_entity3d_get_child (root, 0) == c);
	g_assert_true (lrg_entity3d_get_child (root, 1) == a);
	lrg_entity3d_set_sibling_index (c, 99);
	g_assert_true (lrg_entity3d_get_child (root, 2) == c);

	/* Lookups. */
	g_assert_null (lrg_entity3d_find_child (root, "d", FALSE));
	g_assert_true (lrg_entity3d_find_child (root, "d", TRUE) == d);
	g_assert_true (lrg_entity3d_find_by_path (root, "b/d") == d);
	g_assert_true (lrg_entity3d_find_by_path (d, "../../a") == a);
	g_assert_true (lrg_entity3d_find_by_path (root, "./b/./d") == d);
	g_assert_null (lrg_entity3d_find_by_path (root, "b/x"));
	path = lrg_entity3d_get_path (d);
	g_assert_cmpstr (path, ==, "b/d");

	/* Unnamed entities get "#index" segments that resolve. */
	{
		LrgEntity3D *anon = lrg_entity3d_new ();
		g_autofree gchar *apath = NULL;

		lrg_entity3d_add_child (d, anon);
		g_object_unref (anon);
		apath = lrg_entity3d_get_path (anon);
		g_assert_cmpstr (apath, ==, "b/d/#0");
		g_assert_true (lrg_entity3d_find_by_path (root, apath) == anon);
	}

	/* Cycles are refused. */
	g_test_expect_message ("Libregnum-World3D", G_LOG_LEVEL_WARNING, "*descendant*");
	g_assert_false (lrg_entity3d_set_parent (b, d, FALSE));
	g_test_assert_expected_messages ();

	/* remove_child drops the parent's ref; b is still ours. */
	g_assert_true (lrg_entity3d_remove_child (root, b));
	g_assert_null (lrg_entity3d_get_parent (b));
	g_assert_false (lrg_entity3d_remove_child (root, b));
	g_assert_cmpuint (lrg_entity3d_get_n_children (root), ==, 2);
	g_assert_true (lrg_entity3d_get_parent (d) == b);
	g_object_unref (b);
}

static void
test_hierarchy_signals (void)
{
	g_autoptr(LrgEntity3D) p = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) q = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) c = lrg_entity3d_new ();
	guint added = 0;
	guint removed = 0;

	g_signal_connect (p, "child-added", G_CALLBACK (count1_cb), &added);
	g_signal_connect (p, "child-removed", G_CALLBACK (count1_cb), &removed);

	lrg_entity3d_add_child (p, c);
	g_assert_cmpuint (added, ==, 1);

	/* Reparent keeps the entity alive (we hold c too) and signals both. */
	g_assert_true (lrg_entity3d_set_parent (c, q, FALSE));
	g_assert_cmpuint (removed, ==, 1);
	g_assert_true (lrg_entity3d_get_parent (c) == q);
	g_assert_cmpuint (lrg_entity3d_get_n_children (p), ==, 0);

	g_assert_true (lrg_entity3d_set_parent (c, NULL, FALSE));
	g_assert_null (lrg_entity3d_get_parent (c));
	g_assert_cmpint (G_OBJECT (c)->ref_count, ==, 1);
}

/* ==========================================================================
 * World matrices
 * ========================================================================== */

static void
test_world_matrices (void)
{
	g_autoptr(LrgEntity3D) p = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) c = lrg_entity3d_new ();
	gfloat yaw[3] = { 0, DEG (90), 0 };
	gfloat v[3];
	gfloat local[3] = { 1, 0, 0 };
	gfloat q[4];
	gfloat pw[16];
	gfloat cl[16];
	gfloat expect[16];
	gfloat got[16];

	lrg_entity3d_add_child (p, c);
	lrg_entity3d_set_position_xyz (p, 10, 0, 0);
	lrg_entity3d_set_rotation_euler (p, yaw);
	lrg_entity3d_set_scale_xyz (p, 2, 2, 2);
	lrg_entity3d_set_position_xyz (c, 1, 0, 0);

	/* world = parent world * local */
	lrg_entity3d_get_world_matrix (p, pw);
	lrg_entity3d_get_local_matrix (c, cl);
	lrg_math3d_multiply (pw, cl, expect);
	lrg_entity3d_get_world_matrix (c, got);
	assert_mat_eq (got, expect);

	/* +X rotated 90 deg about Y is -Z, scaled by 2, offset by 10. */
	lrg_entity3d_get_world_position (c, v);
	assert_vec3 (v, 10, 0, -2);
	lrg_entity3d_get_world_scale (c, v);
	assert_vec3 (v, 2, 2, 2);
	lrg_entity3d_transform_point (p, local, v);
	assert_vec3 (v, 10, 0, -2);
	lrg_entity3d_inverse_transform_point (p, v, v);
	assert_vec3 (v, 1, 0, 0);

	/* Directions: forward is local -Z. */
	lrg_entity3d_get_forward (p, v);
	assert_vec3 (v, -1, 0, 0);
	lrg_entity3d_get_right (p, v);
	assert_vec3 (v, 0, 0, -1);
	lrg_entity3d_get_up (p, v);
	assert_vec3 (v, 0, 1, 0);

	/* World setters solve through the parent. */
	{
		gfloat target[3] = { 4, 6, 8 };

		lrg_entity3d_set_world_position (c, target);
		lrg_entity3d_get_world_position (c, v);
		assert_vec3 (v, 4, 6, 8);
	}
	{
		gfloat wq[4] = { 0, 0, 0, 1 };

		lrg_entity3d_set_world_rotation (c, wq);
		lrg_entity3d_get_world_rotation (c, q);
		g_assert_cmpfloat_with_epsilon (fabsf (q[3]), 1.0f, EPS);
	}
	{
		gfloat t[3] = { 1, 2, 3 };
		gfloat r[3] = { DEG (15), DEG (25), DEG (35) };
		gfloat s[3] = { 1, 1, 1 };
		gfloat m[16];

		lrg_math3d_compose (t, r, s, m);
		lrg_entity3d_set_world_matrix (c, m);
		lrg_entity3d_get_world_matrix (c, got);
		assert_mat_eq (got, m);
	}
}

static void
test_world_keep_world_reparent (void)
{
	g_autoptr(LrgEntity3D) a = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) b = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) c = lrg_entity3d_new ();
	gfloat ra[3] = { 0, DEG (45), 0 };
	gfloat rb[3] = { DEG (30), 0, DEG (10) };
	gfloat before[16];
	gfloat after[16];
	gfloat lp_before[3];
	gfloat lp_after[3];

	lrg_entity3d_set_position_xyz (a, 5, 1, -3);
	lrg_entity3d_set_rotation_euler (a, ra);
	lrg_entity3d_set_position_xyz (b, -7, 2, 9);
	lrg_entity3d_set_rotation_euler (b, rb);
	lrg_entity3d_set_scale_xyz (b, 1.5f, 1.5f, 1.5f);

	lrg_entity3d_add_child (a, c);
	lrg_entity3d_set_position_xyz (c, 1, 2, 3);

	/* keep_world = TRUE: the world matrix survives the move. */
	lrg_entity3d_get_world_matrix (c, before);
	g_assert_true (lrg_entity3d_set_parent (c, b, TRUE));
	lrg_entity3d_get_world_matrix (c, after);
	assert_mat_eq (after, before);

	/* keep_world = FALSE: the local position survives instead. */
	lrg_entity3d_get_position (c, lp_before);
	g_assert_true (lrg_entity3d_set_parent (c, a, FALSE));
	lrg_entity3d_get_position (c, lp_after);
	assert_vec3 (lp_after, lp_before[0], lp_before[1], lp_before[2]);

	/* Detach with keep_world: becomes a root at the same place. */
	lrg_entity3d_get_world_matrix (c, before);
	g_assert_true (lrg_entity3d_set_parent (c, NULL, TRUE));
	lrg_entity3d_get_world_matrix (c, after);
	assert_mat_eq (after, before);
}

static void
test_world_dirty_propagation (void)
{
	g_autoptr(LrgEntity3D) root = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) mid = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) leaf = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) other = lrg_entity3d_new ();
	guint64 v_leaf;
	guint64 v_other;
	gfloat v[3];

	lrg_entity3d_add_child (root, mid);
	lrg_entity3d_add_child (mid, leaf);
	lrg_entity3d_set_position_xyz (leaf, 0, 0, 1);

	v_leaf = lrg_entity3d_get_world_version (leaf);
	v_other = lrg_entity3d_get_world_version (other);

	/* Reads without writes keep the version. */
	g_assert_cmpuint (lrg_entity3d_get_world_version (leaf), ==, v_leaf);

	/* Moving the root changes the leaf's world matrix lazily. */
	lrg_entity3d_set_position_xyz (root, 100, 0, 0);
	lrg_entity3d_get_world_position (leaf, v);
	assert_vec3 (v, 100, 0, 1);
	g_assert_cmpuint (lrg_entity3d_get_world_version (leaf), !=, v_leaf);

	/* An unrelated entity is not recomputed by someone else's move. */
	g_assert_cmpuint (lrg_entity3d_get_world_version (other), ==, v_other);

	/* Setting the same value is a no-op for the version. */
	v_leaf = lrg_entity3d_get_world_version (leaf);
	lrg_entity3d_set_position_xyz (root, 100, 0, 0);
	g_assert_cmpuint (lrg_entity3d_get_world_version (leaf), ==, v_leaf);

	/* Reparenting is a change too. */
	lrg_entity3d_set_parent (leaf, root, FALSE);
	lrg_entity3d_get_world_position (leaf, v);
	assert_vec3 (v, 100, 0, 1);
	g_assert_cmpuint (lrg_entity3d_get_world_version (leaf), !=, v_leaf);
}

static void
test_world_transform_signal_outside_world (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	guint n = 0;

	g_signal_connect (e, "transform-changed", G_CALLBACK (count_cb), &n);

	/* Outside a world: synchronous, one per effective change. */
	lrg_entity3d_set_position_xyz (e, 1, 0, 0);
	lrg_entity3d_set_position_xyz (e, 1, 0, 0);
	lrg_entity3d_set_scale_xyz (e, 2, 2, 2);
	g_assert_cmpuint (n, ==, 2);
}

/* ==========================================================================
 * Components
 * ========================================================================== */

static void
test_components_order_and_hooks (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	g_autoptr(GString) log = g_string_new (NULL);
	g_autoptr(TestRecComponent) c1 = rec_new ("1", log);
	g_autoptr(TestRecComponent) c2 = rec_new ("2", log);
	g_autoptr(LrgTransformComponent) t = lrg_transform_component_new ();
	g_autoptr(GPtrArray) all = NULL;
	g_autoptr(GPtrArray) recs = NULL;
	guint added = 0;
	guint removed = 0;

	g_signal_connect (e, "component-added", G_CALLBACK (count1_cb), &added);
	g_signal_connect (e, "component-removed", G_CALLBACK (count1_cb), &removed);

	g_assert_true (lrg_entity3d_add_component (e, LRG_COMPONENT (c1)));
	g_assert_true (lrg_entity3d_add_component (e, LRG_COMPONENT (t)));
	g_assert_true (lrg_entity3d_add_component (e, LRG_COMPONENT (c2)));
	g_assert_cmpuint (added, ==, 3);
	g_assert_cmpstr (log->str, ==, "attach:1;attach:2;");

	/* Owner accessors. */
	g_assert_true (lrg_component_get_entity (LRG_COMPONENT (c1)) == e);
	g_assert_true (lrg_component_get_owner_object (LRG_COMPONENT (c1)) == G_OBJECT (e));
	g_assert_null (lrg_component_get_owner (LRG_COMPONENT (c1)));
	g_assert_null (lrg_component_get_world3d (LRG_COMPONENT (c1)));

	/* Order and type queries. */
	all = lrg_entity3d_get_components (e);
	g_assert_cmpuint (all->len, ==, 3);
	g_assert_true (g_ptr_array_index (all, 0) == c1);
	g_assert_true (g_ptr_array_index (all, 1) == t);
	g_assert_true (g_ptr_array_index (all, 2) == c2);
	g_assert_true (lrg_entity3d_get_component (e, TEST_TYPE_REC_COMPONENT) == LRG_COMPONENT (c1));
	recs = lrg_entity3d_get_components_of_type (e, TEST_TYPE_REC_COMPONENT);
	g_assert_cmpuint (recs->len, ==, 2);
	g_assert_true (lrg_entity3d_has_component (e, LRG_TYPE_TRANSFORM_COMPONENT));
	g_assert_cmpuint (lrg_entity3d_get_n_components (e), ==, 3);

	/* A component has one owner at a time. */
	{
		g_autoptr(LrgEntity3D) f = lrg_entity3d_new ();

		g_test_expect_message ("Libregnum-World3D", G_LOG_LEVEL_WARNING, "*already has an owner*");
		g_assert_false (lrg_entity3d_add_component (f, LRG_COMPONENT (c1)));
		g_test_assert_expected_messages ();
	}

	/* Removal runs detached_entity with the link still set. */
	g_assert_true (lrg_entity3d_remove_component (e, LRG_COMPONENT (c1)));
	g_assert_false (lrg_entity3d_remove_component (e, LRG_COMPONENT (c1)));
	g_assert_cmpuint (removed, ==, 1);
	g_assert_null (lrg_component_get_entity (LRG_COMPONENT (c1)));
	g_assert_cmpstr (log->str, ==, "attach:1;attach:2;detach:1;");
	g_assert_true (lrg_entity3d_get_component (e, TEST_TYPE_REC_COMPONENT) == LRG_COMPONENT (c2));

	/* Disposing the entity detaches the rest. */
	g_clear_object (&e);
	g_assert_cmpstr (log->str, ==, "attach:1;attach:2;detach:1;detach:2;");
	g_assert_null (lrg_component_get_entity (LRG_COMPONENT (c2)));
}

static void
test_components_no_bounds (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	LrgBoundingBox3D b;

	lrg_entity3d_set_position_xyz (e, 3, 4, 5);
	g_assert_false (lrg_entity3d_get_local_bounds (e, &b));
	g_assert_false (lrg_entity3d_get_world_bounds (e, &b));
	g_assert_cmpfloat (b.min.x, ==, 3.0f);
	g_assert_cmpfloat (b.max.z, ==, 5.0f);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/entity3d/identity/defaults", test_identity_defaults);
	g_test_add_func ("/entity3d/identity/guid-once", test_identity_guid_once);
	g_test_add_func ("/entity3d/identity/flags-tags-layer", test_identity_flags_tags_layer);
	g_test_add_func ("/entity3d/trs/local-matrix", test_trs_local_matrix);
	g_test_add_func ("/entity3d/trs/euler-authored-roundtrip", test_euler_authored_roundtrip);
	g_test_add_func ("/entity3d/hierarchy/ops", test_hierarchy_ops);
	g_test_add_func ("/entity3d/hierarchy/signals", test_hierarchy_signals);
	g_test_add_func ("/entity3d/world/matrices", test_world_matrices);
	g_test_add_func ("/entity3d/world/keep-world-reparent", test_world_keep_world_reparent);
	g_test_add_func ("/entity3d/world/dirty-propagation", test_world_dirty_propagation);
	g_test_add_func ("/entity3d/world/transform-signal", test_world_transform_signal_outside_world);
	g_test_add_func ("/entity3d/components/order-and-hooks", test_components_order_and_hooks);
	g_test_add_func ("/entity3d/components/no-bounds", test_components_no_bounds);

	return g_test_run ();
}
