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
 */

#include <glib.h>
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

	return g_test_run ();
}
