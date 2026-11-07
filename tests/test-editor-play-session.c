/* test-editor-play-session.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgEditorPlaySession: start / tick / pause / step /
 * stop with exact restoration (transforms, edits, undo history, dirty
 * flag), the node GUID <-> game object map, the live object model's
 * items-changed, apply-to-level as one undo step, and profiler sections.
 *
 * A small "test-mover" component, registered in the default engine's
 * registry, moves its object along X at "speed" units per second so the
 * simulation writes transforms back onto the authoring node.
 *
 * The /editor/play-session/3d/ cases play a level whose default view is
 * 3D: the session builds an LrgWorld3D with lrg_level_instantiate_3d(),
 * "test-mover3d" moves its LrgEntity3D, "test-turn3d" rotates it (by
 * quaternion or by Euler angles) and counts fixed steps, and the cases
 * check the field-by-field sync-back (authored Euler kept), Stop, the
 * GUID lookups, the live tree model and apply-to-level for registry and
 * visual components.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4f)

/* ==========================================================================
 * test-mover component
 * ========================================================================== */

#define TEST_TYPE_MOVER (test_mover_get_type ())
G_DECLARE_FINAL_TYPE (TestMover, test_mover, TEST, MOVER, LrgComponent)

struct _TestMover
{
	LrgComponent parent_instance;
	gfloat       speed;
};

G_DEFINE_FINAL_TYPE (TestMover, test_mover, LRG_TYPE_COMPONENT)

enum
{
	MOVER_PROP_0,
	MOVER_PROP_SPEED,
	MOVER_N_PROPS
};

static void
test_mover_update (LrgComponent *component,
                   gfloat        delta)
{
	TestMover     *self = TEST_MOVER (component);
	LrgGameObject *owner = lrg_component_get_owner (component);

	if (owner != NULL)
		grl_entity_set_position_xy (GRL_ENTITY (owner), grl_entity_get_x (GRL_ENTITY (owner)) + self->speed * delta,
		                            grl_entity_get_y (GRL_ENTITY (owner)));
}

static void
test_mover_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	if (prop_id == MOVER_PROP_SPEED)
		g_value_set_float (value, TEST_MOVER (object)->speed);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_mover_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	if (prop_id == MOVER_PROP_SPEED)
		TEST_MOVER (object)->speed = g_value_get_float (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_mover_class_init (TestMoverClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->get_property = test_mover_get_property;
	object_class->set_property = test_mover_set_property;
	component_class->update = test_mover_update;

	g_object_class_install_property (object_class, MOVER_PROP_SPEED,
		g_param_spec_float ("speed", "Speed", "Units per second", -1000.0f, 1000.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_mover_init (TestMover *self)
{
}


/* ==========================================================================
 * test-mover3d and test-turn3d components (3D play)
 * ========================================================================== */

#define TEST_TYPE_MOVER3D (test_mover3d_get_type ())
G_DECLARE_FINAL_TYPE (TestMover3D, test_mover3d, TEST, MOVER3D, LrgComponent)

struct _TestMover3D
{
	LrgComponent parent_instance;
	gfloat       speed;
	gfloat       lift;    /* units per second on Y */
};

G_DEFINE_FINAL_TYPE (TestMover3D, test_mover3d, LRG_TYPE_COMPONENT)

enum
{
	MOVER3D_PROP_0,
	MOVER3D_PROP_SPEED,
	MOVER3D_PROP_LIFT,
	MOVER3D_N_PROPS
};

static void
test_mover3d_update (LrgComponent *component,
                     gfloat        delta)
{
	TestMover3D *self = TEST_MOVER3D (component);
	LrgEntity3D *entity = lrg_component_get_entity (component);
	gfloat       p[3];

	if (entity == NULL)
		return;
	lrg_entity3d_get_position (entity, p);
	p[0] += self->speed * delta;
	p[1] += self->lift * delta;
	lrg_entity3d_set_position (entity, p);
}

static void
test_mover3d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	if (prop_id == MOVER3D_PROP_SPEED)
		g_value_set_float (value, TEST_MOVER3D (object)->speed);
	else if (prop_id == MOVER3D_PROP_LIFT)
		g_value_set_float (value, TEST_MOVER3D (object)->lift);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_mover3d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	if (prop_id == MOVER3D_PROP_SPEED)
		TEST_MOVER3D (object)->speed = g_value_get_float (value);
	else if (prop_id == MOVER3D_PROP_LIFT)
		TEST_MOVER3D (object)->lift = g_value_get_float (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_mover3d_class_init (TestMover3DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = test_mover3d_get_property;
	object_class->set_property = test_mover3d_set_property;
	LRG_COMPONENT_CLASS (klass)->update = test_mover3d_update;

	g_object_class_install_property (object_class, MOVER3D_PROP_SPEED,
		g_param_spec_float ("speed", "Speed", "Units per second on X", -1000.0f, 1000.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, MOVER3D_PROP_LIFT,
		g_param_spec_float ("lift", "Lift", "Units per second on Y", -1000.0f, 1000.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_mover3d_init (TestMover3D *self)
{
}

#define TEST_TYPE_TURN3D (test_turn3d_get_type ())
G_DECLARE_FINAL_TYPE (TestTurn3D, test_turn3d, TEST, TURN3D, LrgComponent)

/* Turns its entity about Y in fixed steps: through a quaternion, or
 * (euler = TRUE) through Euler angles. */
struct _TestTurn3D
{
	LrgComponent parent_instance;
	gfloat       rate;        /* radians per second */
	gboolean     euler;
	gfloat       angle;
	guint        fixed_steps;
};

G_DEFINE_FINAL_TYPE (TestTurn3D, test_turn3d, LRG_TYPE_COMPONENT)

enum
{
	TURN3D_PROP_0,
	TURN3D_PROP_RATE,
	TURN3D_PROP_EULER,
	TURN3D_N_PROPS
};

static void
test_turn3d_fixed_update (LrgComponent *component,
                          gfloat        step)
{
	TestTurn3D  *self = TEST_TURN3D (component);
	LrgEntity3D *entity = lrg_component_get_entity (component);
	const gfloat axis[3] = { 0.0f, 1.0f, 0.0f };

	self->fixed_steps++;
	if (entity == NULL || self->rate == 0.0f)
		return;
	self->angle += self->rate * step;
	if (self->euler)
	{
		gfloat e[3] = { 0.0f, self->angle, 0.0f };

		lrg_entity3d_set_rotation_euler (entity, e);
	}
	else
	{
		gfloat q[4];

		lrg_math3d_quat_from_axis_angle (axis, self->angle, q);
		lrg_entity3d_set_rotation (entity, q);
	}
}

static void
test_turn3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	if (prop_id == TURN3D_PROP_RATE)
		g_value_set_float (value, TEST_TURN3D (object)->rate);
	else if (prop_id == TURN3D_PROP_EULER)
		g_value_set_boolean (value, TEST_TURN3D (object)->euler);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_turn3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	if (prop_id == TURN3D_PROP_RATE)
		TEST_TURN3D (object)->rate = g_value_get_float (value);
	else if (prop_id == TURN3D_PROP_EULER)
		TEST_TURN3D (object)->euler = g_value_get_boolean (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_turn3d_class_init (TestTurn3DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = test_turn3d_get_property;
	object_class->set_property = test_turn3d_set_property;
	LRG_COMPONENT_CLASS (klass)->fixed_update = test_turn3d_fixed_update;

	g_object_class_install_property (object_class, TURN3D_PROP_RATE,
		g_param_spec_float ("rate", "Rate", "Radians per second", -100.0f, 100.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, TURN3D_PROP_EULER,
		g_param_spec_boolean ("euler", "Euler", "Set Euler angles", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_turn3d_init (TestTurn3D *self)
{
}

/* ==========================================================================
 * Fixture
 * ========================================================================== */

typedef struct
{
	LrgEditor            *editor;
	LrgEditorPlaySession *session;
	LrgNode              *mover;     /* owned by the level */
	guint                 started;
	guint                 stopped;
	guint                 paused;
	guint                 ticked;
} Fixture;

static void on_signal (gpointer instance, gpointer user_data) { (*(guint *) user_data)++; }
static void on_paused (gpointer instance, gboolean p, gpointer user_data) { (*(guint *) user_data)++; }
static void on_ticked (gpointer instance, gfloat d, gpointer user_data) { (*(guint *) user_data)++; }

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(LrgNode)          node = lrg_node_new ("Mover");
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("test-mover");

	memset (f, 0, sizeof *f);
	f->editor = lrg_editor_new ();

	/* These cases cover the 2D path (LrgWorld / LrgGameObject) */
	lrg_level_set_default_2d (lrg_editor_get_level (f->editor), TRUE);

	/* A node at (1, 2, 3) whose component moves it +60 units/s on X. */
	lrg_node_set_location_xyz (node, 1.0f, 2.0f, 3.0f);
	lrg_component_desc_set_double (desc, "speed", 60.0);
	lrg_node_add_component (node, desc);
	lrg_editor_add_node (f->editor, node, NULL);
	f->mover = node;
	lrg_editor_mark_clean (f->editor);

	f->session = lrg_editor_play_session_new (f->editor);
	g_signal_connect (f->session, "started", G_CALLBACK (on_signal), &f->started);
	g_signal_connect (f->session, "stopped", G_CALLBACK (on_signal), &f->stopped);
	g_signal_connect (f->session, "paused", G_CALLBACK (on_paused), &f->paused);
	g_signal_connect (f->session, "ticked", G_CALLBACK (on_ticked), &f->ticked);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->session);
	g_clear_object (&f->editor);
}

static LrgComponent *
live_mover (Fixture *f)
{
	LrgGameObject *obj = lrg_editor_play_session_get_object_for_node (f->session, f->mover);

	g_assert_nonnull (obj);
	return lrg_game_object_get_component (obj, TEST_TYPE_MOVER);
}

static gdouble
desc_speed (Fixture *f)
{
	LrgComponentDesc *desc = g_ptr_array_index (lrg_node_get_components (f->mover), 0);
	const GValue     *v = lrg_component_desc_get_value (desc, "speed");

	g_assert_nonnull (v);
	return g_value_get_double (v);
}

static void
start (Fixture *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_play_session_start (f->session, &error));
	g_assert_no_error (error);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* test-editor-app "play-restores", plus the simulation write-back. */
static void
test_play_restores (Fixture       *f,
                    gconstpointer  data)
{
	guint undo = lrg_editor_get_undo_count (f->editor);

	g_assert_cmpint (lrg_editor_play_session_get_state (f->session), ==, LRG_EDITOR_PLAY_STATE_STOPPED);
	g_assert_false (lrg_editor_play_session_tick (f->session, 0.016f));
	start (f);
	start (f);   /* already playing: nothing happens */
	g_assert_cmpuint (f->started, ==, 1);
	g_assert_true (lrg_editor_play_session_is_playing (f->session));
	g_assert_nonnull (lrg_editor_play_session_get_world (f->session));

	/* 0.1 s of simulation at 60 units/s moves the node 6 units on X. */
	g_assert_true (lrg_editor_play_session_tick (f->session, 0.1f));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 7.0f, EPS);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->y, ==, 2.0f);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->z, ==, 3.0f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_play_session_get_time (f->session), 0.1, 1e-6);
	g_assert_cmpuint (f->ticked, ==, 1);

	/* Long frames are clamped to 0.1 s. */
	lrg_editor_play_session_tick (f->session, 5.0f);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 13.0f, EPS);

	/* Edits while playing are discarded on stop but stay redoable. */
	lrg_editor_rename_node (f->editor, f->mover, "Changed");
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpuint (f->stopped, ==, 1);
	g_assert_false (lrg_editor_play_session_is_playing (f->session));
	g_assert_null (lrg_editor_play_session_get_world (f->session));
	g_assert_cmpstr (lrg_node_get_name (f->mover), ==, "Mover");
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->y, ==, 2.0f);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->z, ==, 3.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);
	g_assert_cmpuint (lrg_editor_get_redo_count (f->editor), ==, 1);
	g_assert_false (lrg_editor_is_dirty (f->editor));

	/* Stopping twice is harmless. */
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpuint (f->stopped, ==, 1);
}

static void
test_play_keeps_dirty (Fixture       *f,
                       gconstpointer  data)
{
	/* A document dirty before play stays dirty after stop. */
	lrg_editor_rename_node (f->editor, f->mover, "Edited");
	g_assert_true (lrg_editor_is_dirty (f->editor));
	start (f);
	lrg_editor_play_session_tick (f->session, 0.05f);
	lrg_editor_play_session_stop (f->session);
	g_assert_true (lrg_editor_is_dirty (f->editor));
	g_assert_cmpstr (lrg_node_get_name (f->mover), ==, "Edited");
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
}

static void
test_play_pause_step (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(GError) error = NULL;

	/* Pausing does nothing while stopped. */
	lrg_editor_play_session_toggle_pause (f->session);
	g_assert_cmpuint (f->paused, ==, 0);

	start (f);
	lrg_editor_play_session_toggle_pause (f->session);
	g_assert_true (lrg_editor_play_session_is_paused (f->session));
	g_assert_cmpint (lrg_editor_play_session_get_state (f->session), ==, LRG_EDITOR_PLAY_STATE_PAUSED);
	g_assert_cmpuint (f->paused, ==, 1);
	g_assert_false (lrg_editor_play_session_tick (f->session, 0.1f));
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);

	/* Step: exactly one fixed frame, then paused again. */
	g_assert_true (lrg_editor_play_session_step (f->session, &error));
	g_assert_true (lrg_editor_play_session_tick (f->session, 0.1f));
	g_assert_cmpfloat_with_epsilon (lrg_editor_play_session_get_time (f->session), 1.0 / 60.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 2.0f, EPS);
	g_assert_true (lrg_editor_play_session_is_paused (f->session));
	g_assert_false (lrg_editor_play_session_tick (f->session, 0.1f));

	/* Resume. */
	lrg_editor_play_session_set_paused (f->session, FALSE);
	g_assert_true (lrg_editor_play_session_tick (f->session, 0.1f));
	lrg_editor_play_session_stop (f->session);

	/* Step from stopped starts paused. */
	g_assert_true (lrg_editor_play_session_step (f->session, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_play_session_is_paused (f->session));
	g_assert_true (lrg_editor_play_session_tick (f->session, 0.0f));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 2.0f, EPS);
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
}

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  guint      *log)
{
	log[0]++;
	log[1] = position;
	log[2] = removed;
	log[3] = added;
}

static void
test_play_guid_map_and_model (Fixture       *f,
                              gconstpointer  data)
{
	GListModel              *model = lrg_editor_play_session_get_objects (f->session);
	guint                    log[4] = { 0, 0, 0, 0 };
	LrgGameObject           *obj;
	g_autoptr(LrgGameObject) spawned = NULL;

	g_assert_true (g_list_model_get_item_type (model) == LRG_TYPE_GAME_OBJECT);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 0);
	g_signal_connect (model, "items-changed", G_CALLBACK (on_items_changed), log);

	start (f);
	g_assert_cmpuint (log[0], ==, 1);
	g_assert_cmpuint (log[3], ==, 1);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 1);

	/* Both directions of the map. */
	obj = lrg_editor_play_session_get_object_for_node (f->session, f->mover);
	g_assert_nonnull (obj);
	g_assert_true (lrg_editor_play_session_get_object_for_guid (f->session, lrg_node_get_guid (f->mover)) == obj);
	g_assert_true (lrg_editor_play_session_get_node_for_object (f->session, obj) == f->mover);
	g_assert_cmpstr (lrg_editor_play_session_get_guid_for_object (f->session, obj), ==,
	                 lrg_node_get_guid (f->mover));
	g_assert_null (lrg_editor_play_session_get_object_for_guid (f->session, "no-such-guid"));

	/* A tick without world changes emits nothing. */
	lrg_editor_play_session_tick (f->session, 0.016f);
	g_assert_cmpuint (log[0], ==, 1);

	/* Game code spawning an object: one insertion at the end. */
	spawned = lrg_game_object_new ();
	lrg_world_add_object (lrg_editor_play_session_get_world (f->session), spawned);
	lrg_editor_play_session_tick (f->session, 0.016f);
	g_assert_cmpuint (log[0], ==, 2);
	g_assert_cmpuint (log[1], ==, 1);
	g_assert_cmpuint (log[2], ==, 0);
	g_assert_cmpuint (log[3], ==, 1);
	g_assert_null (lrg_editor_play_session_get_node_for_object (f->session, spawned));

	/* ...and destroying it: one removal. */
	lrg_world_remove_object (lrg_editor_play_session_get_world (f->session), spawned);
	lrg_editor_play_session_sync_objects (f->session);
	g_assert_cmpuint (log[0], ==, 3);
	g_assert_cmpuint (log[2], ==, 1);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 1);

	/* Stop empties the model and the map. */
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 0);
	g_assert_cmpuint (log[0], ==, 4);
	g_assert_null (lrg_editor_play_session_get_object_for_node (f->session, f->mover));
}

static void
test_play_apply_to_level (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	LrgComponent     *comp;
	LrgGameObject    *obj;
	g_autoptr(LrgGameObject) keep = NULL;
	guint             undo = lrg_editor_get_undo_count (f->editor);

	start (f);
	obj = lrg_editor_play_session_get_object_for_node (f->session, f->mover);
	comp = live_mover (f);
	g_assert_nonnull (comp);
	keep = g_object_ref (obj);   /* outlives the world for the last check */

	/* Tweak the live value, then keep it. */
	g_object_set (comp, "speed", 99.0f, NULL);
	g_assert_true (lrg_editor_play_session_apply_to_level (f->session, obj, comp, "speed", &error));
	g_assert_no_error (error);
	g_assert_cmpfloat (desc_speed (f), ==, 99.0);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);

	/* Errors: unknown property. */
	g_assert_false (lrg_editor_play_session_apply_to_level (f->session, obj, comp, "nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* Stop restores everything else but keeps the applied value as one
	 * undo step; the document is dirty. */
	lrg_editor_play_session_tick (f->session, 0.05f);
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpfloat (desc_speed (f), ==, 99.0);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_true (lrg_editor_is_dirty (f->editor));

	/* That one undo takes the whole apply back. */
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat (desc_speed (f), ==, 60.0);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);

	/* Not playing: nothing to apply. */
	g_assert_false (lrg_editor_play_session_apply_to_level (f->session, obj, comp, "speed", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

static void
test_play_profiler (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();
	LrgProfilerSample     *sample;

	lrg_profiler_set_enabled (profiler, TRUE);
	lrg_editor_play_session_set_profiler (f->session, profiler);
	g_assert_true (lrg_editor_play_session_get_profiler (f->session) == profiler);

	start (f);
	lrg_profiler_begin_frame (profiler);
	lrg_editor_play_session_tick (f->session, 0.016f);
	lrg_profiler_end_frame (profiler);
	sample = lrg_profiler_get_last_sample (profiler, LRG_EDITOR_PLAY_PROFILER_SECTION);
	g_assert_nonnull (sample);
	g_assert_cmpstr (lrg_profiler_sample_get_name (sample), ==, "play.tick");
	g_assert_false (lrg_profiler_is_section_active (profiler, LRG_EDITOR_PLAY_PROFILER_SECTION));
	lrg_profiler_sample_free (sample);
	lrg_editor_play_session_stop (f->session);
}

static void
test_play_dispose_while_playing (Fixture       *f,
                                 gconstpointer  data)
{
	/* Dropping a playing session still restores the level. */
	start (f);
	lrg_editor_play_session_tick (f->session, 0.1f);
	g_clear_object (&f->session);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
	g_assert_false (lrg_editor_is_dirty (f->editor));
}


/* ==========================================================================
 * 3D play
 * ========================================================================== */

typedef struct
{
	LrgEditor            *editor;
	LrgEditorPlaySession *session;
	LrgNode              *mover;     /* (1,2,3), authored rotation, scale */
	LrgNode              *turner;    /* turns by quaternion */
	LrgNode              *eturner;   /* turns by Euler angles */
	LrgNode              *group;     /* group with one child */
	LrgNode              *child;
	LrgNode              *lamp;      /* light visual */
} Fixture3D;

static LrgNode *
add3d (Fixture3D   *f,
       const gchar *name,
       LrgNode     *parent)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	if (parent != NULL)
		lrg_editor_insert_node (f->editor, node, parent, -1);
	else
		lrg_editor_add_node (f->editor, node, NULL);
	return node;
}

static void
add_desc (LrgNode     *node,
          const gchar *type,
          const gchar *key,
          gdouble      value)
{
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new (type);

	if (key != NULL)
		lrg_component_desc_set_double (desc, key, value);
	lrg_node_add_component (node, desc);
}

static void
fixture3d_setup (Fixture3D     *f,
                 gconstpointer  data)
{
	g_autoptr(LrgNodeVisual)    light = lrg_node_visual_new (LRG_NODE_VISUAL_LIGHT);
	g_autoptr(LrgComponentDesc) turn = lrg_component_desc_new ("test-turn3d");

	memset (f, 0, sizeof *f);
	f->editor = lrg_editor_new ();
	g_assert_false (lrg_level_get_default_2d (lrg_editor_get_level (f->editor)));

	f->mover = add3d (f, "Mover", NULL);
	lrg_node_set_location_xyz (f->mover, 1.0f, 2.0f, 3.0f);
	lrg_node_set_rotation_xyz (f->mover, 0.1f, 7.0f, -0.2f);   /* past 2 pi */
	lrg_node_set_scale_xyz (f->mover, 1.0f, 2.0f, 3.0f);
	add_desc (f->mover, "test-mover3d", "speed", 60.0);

	f->turner = add3d (f, "Turner", NULL);
	add_desc (f->turner, "test-turn3d", "rate", 1.0);

	f->eturner = add3d (f, "EulerTurner", NULL);
	lrg_component_desc_set_double (turn, "rate", 1.0);
	lrg_component_desc_set_boolean (turn, "euler", TRUE);
	lrg_node_add_component (f->eturner, turn);

	f->group = add3d (f, "Group", NULL);
	f->child = add3d (f, "Child", f->group);

	f->lamp = add3d (f, "Lamp", NULL);
	lrg_node_visual_set_param_double (light, "intensity", 1.0);
	lrg_node_set_visual (f->lamp, light);

	lrg_editor_mark_clean (f->editor);
	f->session = lrg_editor_play_session_new (f->editor);
}

static void
fixture3d_teardown (Fixture3D     *f,
                    gconstpointer  data)
{
	g_clear_object (&f->session);
	g_clear_object (&f->editor);
}

static void
start3d (Fixture3D *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_play_session_start (f->session, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_play_session_get_is_3d (f->session));
}

static LrgEntity3D *
entity_of (Fixture3D *f,
           LrgNode   *node)
{
	GObject *live = lrg_editor_play_session_get_live_for_node (f->session, node);

	g_assert_true (LRG_IS_ENTITY3D (live));
	return LRG_ENTITY3D (live);
}

/* A 3D level plays in an LrgWorld3D; the tick moves the entity, the node
 * follows exactly, Stop restores everything. */
static void
test_play3d_restores (Fixture3D     *f,
                      gconstpointer  data)
{
	guint       undo = lrg_editor_get_undo_count (f->editor);
	GrlVector3 *loc, *rot, *scl;

	start3d (f);
	g_assert_null (lrg_editor_play_session_get_world (f->session));
	g_assert_nonnull (lrg_editor_play_session_get_world3d (f->session));
	g_assert_cmpuint (lrg_editor_play_session_get_n_objects (f->session), ==, 6);
	g_assert_cmpuint (g_list_model_get_n_items (lrg_editor_play_session_get_objects (f->session)), ==, 0);

	g_assert_true (lrg_editor_play_session_tick (f->session, 0.1f));
	loc = lrg_node_get_location (f->mover);
	rot = lrg_node_get_rotation (f->mover);
	scl = lrg_node_get_scale (f->mover);
	g_assert_cmpfloat_with_epsilon (loc->x, 7.0f, EPS);
	g_assert_cmpfloat (loc->y, ==, 2.0f);
	g_assert_cmpfloat (loc->z, ==, 3.0f);

	/* The rotation did not change: the authored Euler (7 rad) stays */
	g_assert_cmpfloat (rot->x, ==, 0.1f);
	g_assert_cmpfloat (rot->y, ==, 7.0f);
	g_assert_cmpfloat (rot->z, ==, -0.2f);
	g_assert_cmpfloat (scl->x, ==, 1.0f);
	g_assert_cmpfloat (scl->y, ==, 2.0f);
	g_assert_cmpfloat (scl->z, ==, 3.0f);

	/* Group nodes the simulation did not touch stay put */
	g_assert_cmpfloat (lrg_node_get_location (f->child)->x, ==, 0.0f);

	lrg_editor_play_session_stop (f->session);
	g_assert_null (lrg_editor_play_session_get_world3d (f->session));
	g_assert_false (lrg_editor_play_session_get_is_3d (f->session));
	g_assert_cmpuint (lrg_editor_play_session_get_n_objects (f->session), ==, 0);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
	g_assert_cmpfloat (lrg_node_get_rotation (f->mover)->y, ==, 7.0f);
	g_assert_cmpfloat (lrg_node_get_rotation (f->turner)->y, ==, 0.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);
	g_assert_false (lrg_editor_is_dirty (f->editor));
}

/* Only the axes the simulation moved are written. */
static void
test_play3d_per_field (Fixture3D     *f,
                       gconstpointer  data)
{
	LrgComponent *mover;

	start3d (f);

	/* An outside write to the node's Z (a timeline, a tool) is not
	 * clobbered by the entity's unchanged Z */
	lrg_node_set_location_xyz (f->mover, 1.0f, 2.0f, 9.0f);
	lrg_editor_play_session_tick (f->session, 0.05f);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 4.0f, EPS);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->y, ==, 2.0f);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->z, ==, 9.0f);

	/* Now Y moves too */
	mover = lrg_entity3d_get_component (entity_of (f, f->mover), TEST_TYPE_MOVER3D);
	g_object_set (mover, "lift", 10.0f, NULL);
	lrg_editor_play_session_tick (f->session, 0.1f);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->x, 10.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->mover)->y, 3.0f, EPS);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->z, ==, 9.0f);

	lrg_editor_play_session_stop (f->session);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->z, ==, 3.0f);
}

/* Rotations: quaternions decompose with the node's convention, Euler
 * angles set by the simulation come back exactly; step runs one fixed
 * step. */
static void
test_play3d_rotation (Fixture3D     *f,
                      gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	LrgEntity3D      *e;
	TestTurn3D       *turn;
	gfloat            q[4], euler[3];
	GrlVector3       *rot;

	g_assert_true (lrg_editor_play_session_step (f->session, &error));
	g_assert_true (lrg_editor_play_session_get_is_3d (f->session));
	g_assert_true (lrg_editor_play_session_tick (f->session, 0.5f));

	/* Exactly one fixed step of 1/60 s */
	e = entity_of (f, f->turner);
	turn = TEST_TURN3D (lrg_entity3d_get_component (e, TEST_TYPE_TURN3D));
	g_assert_cmpuint (turn->fixed_steps, ==, 1);

	/* Quaternion -> lrg_math3d_quat_to_euler() */
	lrg_entity3d_get_rotation (e, q);
	lrg_math3d_quat_to_euler (q, euler);
	rot = lrg_node_get_rotation (f->turner);
	g_assert_cmpfloat (rot->x, ==, euler[0]);
	g_assert_cmpfloat (rot->y, ==, euler[1]);
	g_assert_cmpfloat (rot->z, ==, euler[2]);
	g_assert_cmpfloat_with_epsilon (rot->y, 1.0f / 60.0f, 1e-5f);

	/* Euler set by the simulation: exact */
	rot = lrg_node_get_rotation (f->eturner);
	turn = TEST_TURN3D (lrg_entity3d_get_component (entity_of (f, f->eturner), TEST_TYPE_TURN3D));
	g_assert_cmpfloat (rot->x, ==, 0.0f);
	g_assert_cmpfloat (rot->y, ==, turn->angle);
	g_assert_cmpfloat (rot->z, ==, 0.0f);

	/* The mover's rotation was never touched: still the authored 7 rad */
	g_assert_cmpfloat (lrg_node_get_rotation (f->mover)->y, ==, 7.0f);

	lrg_editor_play_session_stop (f->session);
	g_assert_cmpfloat (lrg_node_get_rotation (f->turner)->y, ==, 0.0f);
	g_assert_cmpfloat (lrg_node_get_rotation (f->eturner)->y, ==, 0.0f);
}

/* GUID lookups over the world's GUID map. */
static void
test_play3d_lookups (Fixture3D     *f,
                     gconstpointer  data)
{
	g_autoptr(LrgEntity3D) spawned = lrg_entity3d_new_with_name ("Spawned");
	LrgEntity3D           *e;

	start3d (f);
	e = entity_of (f, f->mover);
	g_assert_cmpstr (lrg_entity3d_get_guid (e), ==, lrg_node_get_guid (f->mover));
	g_assert_true (lrg_editor_play_session_get_live_for_guid (f->session, lrg_node_get_guid (f->mover)) ==
	               G_OBJECT (e));
	g_assert_true (lrg_editor_play_session_get_node_for_live (f->session, G_OBJECT (e)) == f->mover);
	g_assert_cmpstr (lrg_editor_play_session_get_guid_for_live (f->session, G_OBJECT (e)), ==,
	                 lrg_node_get_guid (f->mover));
	g_assert_null (lrg_editor_play_session_get_live_for_guid (f->session, "no-such-guid"));
	g_assert_true (lrg_editor_play_session_get_node_for_live (f->session,
	                                                          G_OBJECT (entity_of (f, f->child))) == f->child);

	/* The 2D-only lookups find nothing in 3D */
	g_assert_null (lrg_editor_play_session_get_object_for_node (f->session, f->mover));

	/* An entity the simulation spawned has a GUID but no node */
	lrg_world3d_add_entity (lrg_editor_play_session_get_world3d (f->session), spawned);
	lrg_entity3d_ensure_guid (spawned);
	g_assert_null (lrg_editor_play_session_get_guid_for_live (f->session, G_OBJECT (spawned)));
	g_assert_null (lrg_editor_play_session_get_node_for_live (f->session, G_OBJECT (spawned)));
	g_assert_null (lrg_editor_play_session_get_live_for_guid (f->session, lrg_entity3d_get_guid (spawned)));

	lrg_editor_play_session_stop (f->session);
	g_assert_null (lrg_editor_play_session_get_live_for_node (f->session, f->mover));
}

/* The live tree: top-level entities plus per-entity child models. */
static void
test_play3d_live_tree (Fixture3D     *f,
                       gconstpointer  data)
{
	GListModel             *model = lrg_editor_play_session_get_live_model (f->session);
	g_autoptr(GListModel)   children = NULL;
	g_autoptr(GListModel)   leaf = NULL;
	g_autoptr(LrgEntity3D)  extra = lrg_entity3d_new_with_name ("Extra");
	g_autoptr(GObject)      first = NULL;
	guint                   log[4] = { 0, 0, 0, 0 };
	LrgEntity3D            *group;

	g_assert_true (g_list_model_get_item_type (model) == G_TYPE_OBJECT);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 0);

	start3d (f);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 5);   /* the top-level nodes */
	first = g_list_model_get_item (model, 0);
	g_assert_true (first == G_OBJECT (entity_of (f, f->mover)));

	group = g_object_ref (entity_of (f, f->group));   /* outlives the world for the last check */
	children = lrg_editor_play_session_get_live_children (f->session, G_OBJECT (group));
	g_assert_nonnull (children);
	g_assert_cmpuint (g_list_model_get_n_items (children), ==, 1);
	{
		g_autoptr(GObject) c = g_list_model_get_item (children, 0);

		g_assert_true (c == G_OBJECT (entity_of (f, f->child)));
	}
	leaf = lrg_editor_play_session_get_live_children (f->session, G_OBJECT (entity_of (f, f->child)));
	g_assert_cmpuint (g_list_model_get_n_items (leaf), ==, 0);

	/* The simulation adds a child: one insertion on the next sync */
	g_signal_connect (children, "items-changed", G_CALLBACK (on_items_changed), log);
	lrg_entity3d_add_child (group, extra);
	lrg_editor_play_session_tick (f->session, 0.016f);
	g_assert_cmpuint (log[0], ==, 1);
	g_assert_cmpuint (log[1], ==, 1);
	g_assert_cmpuint (log[3], ==, 1);
	g_assert_cmpuint (g_list_model_get_n_items (children), ==, 2);

	/* 2D game objects have no children */
	{
		g_autoptr(LrgGameObject) go = lrg_game_object_new ();

		g_assert_null (lrg_editor_play_session_get_live_children (f->session, G_OBJECT (go)));
	}

	/* Stop empties both */
	lrg_editor_play_session_stop (f->session);
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 0);
	g_assert_cmpuint (g_list_model_get_n_items (children), ==, 0);
	g_assert_null (lrg_editor_play_session_get_live_children (f->session, G_OBJECT (group)));
	g_object_unref (group);
}

/* Apply to level: a registry component into its description, the light
 * component into the visual; both survive Stop as one undo step. */
static void
test_play3d_apply (Fixture3D     *f,
                   gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	guint             undo = lrg_editor_get_undo_count (f->editor);
	LrgEntity3D      *mover, *lamp;
	LrgComponent     *comp, *light;
	const GValue     *v;

	start3d (f);
	mover = entity_of (f, f->mover);
	lamp = entity_of (f, f->lamp);
	comp = lrg_entity3d_get_component (mover, TEST_TYPE_MOVER3D);
	light = lrg_entity3d_get_component (lamp, LRG_TYPE_LIGHT_COMPONENT);
	g_assert_nonnull (light);

	g_object_set (comp, "speed", 33.0f, NULL);
	g_assert_true (lrg_editor_play_session_apply_live (f->session, G_OBJECT (mover), comp, "speed", &error));
	g_assert_no_error (error);
	lrg_light_component_set_intensity (LRG_LIGHT_COMPONENT (light), 3.5f);
	g_assert_true (lrg_editor_play_session_apply_live (f->session, G_OBJECT (lamp), light, "intensity", &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 2);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (f->lamp), "intensity", 0.0),
	                                3.5, 1e-6);

	/* Errors: a component of another entity, an unknown property */
	g_assert_false (lrg_editor_play_session_apply_live (f->session, G_OBJECT (lamp), comp, "speed", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_editor_play_session_apply_live (f->session, G_OBJECT (mover), comp, "nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* Stop: both values survive, as one undo step */
	lrg_editor_play_session_tick (f->session, 0.05f);
	lrg_editor_play_session_stop (f->session);
	v = lrg_component_desc_get_value (g_ptr_array_index (lrg_node_get_components (f->mover), 0), "speed");
	g_assert_nonnull (v);
	g_assert_cmpfloat (g_value_get_double (v), ==, 33.0);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (f->lamp), "intensity", 0.0),
	                                3.5, 1e-6);
	g_assert_cmpfloat (lrg_node_get_location (f->mover)->x, ==, 1.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_true (lrg_editor_is_dirty (f->editor));

	lrg_editor_undo (f->editor);
	v = lrg_component_desc_get_value (g_ptr_array_index (lrg_node_get_components (f->mover), 0), "speed");
	g_assert_cmpfloat (g_value_get_double (v), ==, 60.0);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (f->lamp), "intensity", 0.0),
	                                1.0, 1e-6);
}

/* ==========================================================================
 * Project physics: play uses the open project's physics: block
 * ========================================================================== */

static const gchar *PLAY_PROJECT_YAML =
	"project:\n"
	"  name: mars\n"
	"physics:\n"
	"  layers: [default, ground, crate, ghost]\n"
	"  no_collide: [[ground, ghost]]\n"
	"  gravity: [0, -3.7, 0]\n"
	"  iterations: 6\n"
	"  extra_tool_key: 1\n";

/* a dynamic box on @layer at the origin */
static LrgNode *
add_physics_box (Fixture3D   *f,
                 const gchar *name,
                 const gchar *layer)
{
	LrgNode                        *node = add3d (f, name, NULL);
	g_autoptr(LrgNodePhysics)       phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	g_autoptr(LrgNodePhysicsShape)  box = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);

	lrg_node_physics_shape_set_half_extents (box, 0.5f, 0.5f, 0.5f);
	lrg_node_physics_add_shape (phys, box);
	lrg_node_physics_set_layer (phys, layer);
	lrg_node_physics_set_mass (phys, 1.0f);
	lrg_node_set_physics (node, phys);
	return node;
}

static void
test_play3d_project_physics (Fixture3D     *f,
                             gconstpointer  data)
{
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-play-physics-XXXXXX", &error);
	g_autofree gchar      *manifest = NULL;
	g_autofree gchar      *level_path = NULL;
	g_autoptr(LrgProject)  project = NULL;
	LrgNode               *ghost;
	LrgWorld3D            *world;
	LrgDynamicsWorld      *physics;
	LrgRigidBodyComponent *rb;
	LrgPhysicsSettings     got;
	guint                  layer = 0;

	(void) data;
	g_assert_no_error (error);
	manifest = g_build_filename (dir, "project.ryaml", NULL);
	g_assert_true (g_file_set_contents (manifest, PLAY_PROJECT_YAML, -1, &error));
	project = lrg_project_open (dir, &error);
	g_assert_no_error (error);
	g_assert_cmpfloat (lrg_project_get_physics_settings (project)->gravity[1], ==, -3.7f);
	ghost = add_physics_box (f, "Ghost", "ghost");

	/* with the project: its settings and its (shared) layer table */
	lrg_editor_play_session_set_project (f->session, project);
	g_assert_true (lrg_editor_play_session_get_project (f->session) == project);
	start3d (f);
	world = lrg_editor_play_session_get_world3d (f->session);
	physics = lrg_world3d_get_physics (world);
	g_assert_nonnull (physics);
	lrg_dynamics_world_get_settings (physics, &got);
	g_assert_cmpfloat (got.gravity[1], ==, -3.7f);
	g_assert_cmpuint (got.iterations, ==, 6);
	g_assert_true (lrg_dynamics_world_get_layers (physics) == lrg_project_get_physics_layers (project));
	g_assert_true (lrg_editor_play_session_tick (f->session, 1.0f / 60.0f));
	rb = LRG_RIGID_BODY_COMPONENT (lrg_entity3d_get_component (LRG_ENTITY3D (lrg_editor_play_session_get_live_for_node (f->session, ghost)),
	                                                           LRG_TYPE_RIGID_BODY_COMPONENT));
	g_assert_nonnull (rb);
	g_assert_true (lrg_dynamics_world_get_body_layer (physics, lrg_rigid_body_component_get_body_id (rb), &layer, NULL, NULL));
	g_assert_cmpuint (layer, ==, 3);
	lrg_editor_play_session_stop (f->session);

	/* an in-memory edit (Project Settings before saving) applies next play */
	lrg_project_get_physics_settings (project)->gravity[1] = -1.62f;
	start3d (f);
	lrg_dynamics_world_get_settings (lrg_world3d_get_physics (lrg_editor_play_session_get_world3d (f->session)), &got);
	g_assert_cmpfloat (got.gravity[1], ==, -1.62f);
	lrg_editor_play_session_stop (f->session);

	/* saving keeps the block's other keys and writes the new value */
	g_assert_true (lrg_project_save (project, &error));
	g_assert_no_error (error);
	{
		g_autoptr(LrgPhysicsSettings) back = lrg_physics_settings_new ();
		g_autofree gchar             *text = NULL;

		g_assert_true (lrg_physics_settings_load_from_file (back, manifest, &error));
		g_assert_cmpfloat (back->gravity[1], ==, -1.62f);
		g_assert_cmpuint (back->iterations, ==, 6);
		g_assert_true (g_file_get_contents (manifest, &text, NULL, &error));
		g_assert_nonnull (strstr (text, "extra_tool_key"));
	}

	/* no project on the session: the project.ryaml above the level file */
	lrg_editor_play_session_set_project (f->session, NULL);
	level_path = g_build_filename (dir, "arena.rlevel", NULL);
	lrg_editor_set_path (f->editor, level_path);
	start3d (f);
	lrg_dynamics_world_get_settings (lrg_world3d_get_physics (lrg_editor_play_session_get_world3d (f->session)), &got);
	g_assert_cmpfloat (got.gravity[1], ==, -1.62f);
	g_assert_cmpint (lrg_physics_layers_find (lrg_dynamics_world_get_layers (lrg_world3d_get_physics (
	                     lrg_editor_play_session_get_world3d (f->session))), "ghost"), ==, 3);
	lrg_editor_play_session_stop (f->session);

	g_unlink (manifest);
	g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError) error = NULL;
	LrgEngine        *engine;
	LrgRegistry      *registry;

	g_test_init (&argc, &argv, NULL);

	/* The registry is created during startup (headless-safe). */
	engine = lrg_engine_get_default ();
	lrg_engine_startup (engine, &error);
	g_assert_no_error (error);
	registry = lrg_engine_get_registry (engine);
	g_assert_nonnull (registry);
	lrg_registry_register (registry, "test-mover", TEST_TYPE_MOVER);
	lrg_registry_register (registry, "test-mover3d", TEST_TYPE_MOVER3D);
	lrg_registry_register (registry, "test-turn3d", TEST_TYPE_TURN3D);

#define ADD(path, func) \
	g_test_add ("/editor/play-session/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	ADD ("restores", test_play_restores);
	ADD ("keeps-dirty", test_play_keeps_dirty);
	ADD ("pause-step", test_play_pause_step);
	ADD ("guid-map-and-model", test_play_guid_map_and_model);
	ADD ("apply-to-level", test_play_apply_to_level);
	ADD ("profiler", test_play_profiler);
	ADD ("dispose-while-playing", test_play_dispose_while_playing);

#undef ADD
#define ADD3D(path, func) \
	g_test_add ("/editor/play-session/3d/" path, Fixture3D, NULL, fixture3d_setup, func, fixture3d_teardown)

	ADD3D ("restores", test_play3d_restores);
	ADD3D ("per-field", test_play3d_per_field);
	ADD3D ("rotation", test_play3d_rotation);
	ADD3D ("lookups", test_play3d_lookups);
	ADD3D ("live-tree", test_play3d_live_tree);
	ADD3D ("apply", test_play3d_apply);
	ADD3D ("project-physics", test_play3d_project_physics);

#undef ADD3D

	return g_test_run ();
}
