/* test-editor-node-source.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgEditorNodeSource: section listings and hints, undoable
 * get/set, a multi-node property-grid scrub in DELEGATE mode driven
 * through the canvas (one undo step, Escape restores), mixed values,
 * component pspec mapping, prefab override state and revert, timeline
 * animated state, missing-asset invalid state and coalesced change
 * emission.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define DT  (1.0f / 60.0f)
#define EPS (1e-4)

/* ==========================================================================
 * A test component type: one property of each kind
 * ========================================================================== */

typedef enum
{
	TEST_MODE_IDLE,
	TEST_MODE_PATROL,
	TEST_MODE_CHASE
} TestMode;

typedef enum
{
	TEST_TAG_NONE  = 0,
	TEST_TAG_BOSS  = 1 << 0,
	TEST_TAG_FLYER = 1 << 1
} TestTags;

static GType
test_mode_get_type (void)
{
	static gsize id = 0;

	if (g_once_init_enter (&id))
	{
		static const GEnumValue values[] = {
			{ TEST_MODE_IDLE, "TEST_MODE_IDLE", "idle" },
			{ TEST_MODE_PATROL, "TEST_MODE_PATROL", "patrol" },
			{ TEST_MODE_CHASE, "TEST_MODE_CHASE", "chase" },
			{ 0, NULL, NULL }
		};
		g_once_init_leave (&id, g_enum_register_static ("TestNodeSourceMode", values));
	}
	return (GType) id;
}

static GType
test_tags_get_type (void)
{
	static gsize id = 0;

	if (g_once_init_enter (&id))
	{
		static const GFlagsValue values[] = {
			{ TEST_TAG_BOSS, "TEST_TAG_BOSS", "boss" },
			{ TEST_TAG_FLYER, "TEST_TAG_FLYER", "flyer" },
			{ 0, NULL, NULL }
		};
		g_once_init_leave (&id, g_flags_register_static ("TestNodeSourceTags", values));
	}
	return (GType) id;
}

#define TEST_TYPE_BRAIN (test_brain_get_type ())
G_DECLARE_FINAL_TYPE (TestBrain, test_brain, TEST, BRAIN, LrgComponent)

struct _TestBrain
{
	LrgComponent parent_instance;
	gint         mode;
	guint        tags;
	gchar       *label;
	gboolean     awake;
	gint         hp;
	gdouble      speed;
};

enum
{
	B_PROP_0,
	B_PROP_MODE,
	B_PROP_TAGS,
	B_PROP_LABEL,
	B_PROP_AWAKE,
	B_PROP_HP,
	B_PROP_SPEED,
	B_N_PROPS
};

G_DEFINE_FINAL_TYPE (TestBrain, test_brain, LRG_TYPE_COMPONENT)

static void
test_brain_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	TestBrain *self = TEST_BRAIN (object);

	switch (prop_id)
	{
	case B_PROP_MODE:  g_value_set_enum (value, self->mode); break;
	case B_PROP_TAGS:  g_value_set_flags (value, self->tags); break;
	case B_PROP_LABEL: g_value_set_string (value, self->label); break;
	case B_PROP_AWAKE: g_value_set_boolean (value, self->awake); break;
	case B_PROP_HP:    g_value_set_int (value, self->hp); break;
	case B_PROP_SPEED: g_value_set_double (value, self->speed); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_brain_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	TestBrain *self = TEST_BRAIN (object);

	switch (prop_id)
	{
	case B_PROP_MODE:  self->mode = g_value_get_enum (value); break;
	case B_PROP_TAGS:  self->tags = g_value_get_flags (value); break;
	case B_PROP_LABEL: g_free (self->label); self->label = g_value_dup_string (value); break;
	case B_PROP_AWAKE: self->awake = g_value_get_boolean (value); break;
	case B_PROP_HP:    self->hp = g_value_get_int (value); break;
	case B_PROP_SPEED: self->speed = g_value_get_double (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_brain_finalize (GObject *object)
{
	g_free (TEST_BRAIN (object)->label);
	G_OBJECT_CLASS (test_brain_parent_class)->finalize (object);
}

static void
test_brain_class_init (TestBrainClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->get_property = test_brain_get_property;
	object_class->set_property = test_brain_set_property;
	object_class->finalize = test_brain_finalize;

	g_object_class_install_property (object_class, B_PROP_MODE,
		g_param_spec_enum ("mode", NULL, NULL, test_mode_get_type (), TEST_MODE_IDLE, flags));
	g_object_class_install_property (object_class, B_PROP_TAGS,
		g_param_spec_flags ("tags", NULL, NULL, test_tags_get_type (), 0, flags));
	g_object_class_install_property (object_class, B_PROP_LABEL,
		g_param_spec_string ("label", NULL, NULL, "grunt", flags));
	g_object_class_install_property (object_class, B_PROP_AWAKE,
		g_param_spec_boolean ("awake", NULL, NULL, TRUE, flags));
	g_object_class_install_property (object_class, B_PROP_HP,
		g_param_spec_int ("hp", NULL, NULL, 0, 1000, 100, flags));
	g_object_class_install_property (object_class, B_PROP_SPEED,
		g_param_spec_double ("speed", NULL, NULL, 0.0, 50.0, 2.5, flags));
}

static void
test_brain_init (TestBrain *self)
{
	self->label = g_strdup ("grunt");
	self->awake = TRUE;
	self->hp = 100;
	self->speed = 2.5;
}

/* Registers TestBrain as "brain" in the default engine's registry */
static gboolean
register_brain (void)
{
	LrgEngine         *engine = lrg_engine_get_default ();
	LrgRegistry       *registry;
	g_autoptr(GError)  error = NULL;

	if (engine == NULL)
		return FALSE;
	registry = lrg_engine_get_registry (engine);
	if (registry == NULL)
	{
		lrg_engine_startup (engine, &error);
		g_assert_no_error (error);
		registry = lrg_engine_get_registry (engine);
	}
	if (registry == NULL)
		return FALSE;
	if (!lrg_registry_is_registered (registry, "brain"))
		lrg_registry_register (registry, "brain", TEST_TYPE_BRAIN);
	return TRUE;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A node added to the editor's level (not undoable), history cleared */
static LrgNode *
add_node (LrgEditor   *editor,
          const gchar *name,
          gfloat       x,
          gfloat       y,
          gfloat       z)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	lrg_node_set_location_xyz (node, x, y, z);
	lrg_level_add_node (lrg_editor_get_level (editor), node, NULL);
	return node;  /* the level keeps it alive */
}

static gboolean
has_property (LrgPropertySource *src,
              const gchar       *name)
{
	g_autoptr(LrgPropertyInfo) info = lrg_property_source_find_property (src, name);

	return info != NULL;
}

static const gchar *
hint_of (LrgPropertySource *src,
         const gchar       *name)
{
	g_autoptr(LrgPropertyInfo) info = lrg_property_source_find_property (src, name);
	static gchar              *last = NULL;

	g_assert_nonnull (info);
	g_free (last);
	last = g_strdup (lrg_property_info_get_hint (info));
	return last;
}

static void
set_vec3 (LrgPropertySource *src,
          const gchar       *name,
          gfloat             x,
          gfloat             y,
          gfloat             z)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, GRL_TYPE_VECTOR3);
	g_value_take_boxed (&v, grl_vector3_new (x, y, z));
	g_assert_true (lrg_property_source_set_value (src, name, &v));
}

static GrlVector3
get_vec3 (LrgPropertySource *src,
          const gchar       *name)
{
	g_auto(GValue) v = G_VALUE_INIT;
	GrlVector3     out;

	g_assert_true (lrg_property_source_get_value (src, name, &v));
	out = *(GrlVector3 *) g_value_get_boxed (&v);
	return out;
}

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR))
			remove_tree (p);
		else
			g_unlink (p);
	}
	g_rmdir (dir);
}

/* ==========================================================================
 * Canvas helpers (as tests/test-ui-property-grid.c)
 * ========================================================================== */

static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
drag_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
	lrg_canvas_update (canvas, DT);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
key_held (LrgCanvas *canvas,
          GrlKey     k,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
settle (LrgCanvas *canvas)
{
	guint n = 0;

	while (n < 600 && lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		n++;
	}
	lrg_canvas_record (canvas);
}

/* ==========================================================================
 * Listing per section
 * ========================================================================== */

static void
test_listing (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "lamp", 0, 0, 0);
	g_autoptr(LrgNodeVisual)       light = lrg_node_visual_new (LRG_NODE_VISUAL_LIGHT);
	g_autoptr(LrgScriptBinding)    script = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "scripts/flicker.lua");
	g_autoptr(LrgEditorNodeSource) header = NULL;
	g_autoptr(LrgEditorNodeSource) xform = NULL;
	g_autoptr(LrgEditorNodeSource) visual = NULL;
	g_autoptr(LrgEditorNodeSource) scripts = NULL;
	g_autoptr(GPtrArray)           props = NULL;
	g_autoptr(LrgPropertyInfo)     info = NULL;

	lrg_node_visual_set_param_double (light, "cad:thickness", 2.0);
	lrg_node_set_visual (node, light);
	lrg_node_add_script (node, script);

	/* Header */
	header = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_HEADER, NULL);
	props = lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (header));
	g_assert_cmpuint (props->len, ==, 6);
	g_assert_cmpstr (lrg_property_info_get_name (g_ptr_array_index (props, 0)), ==, "name");
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (header), "is-2d"));
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (header), "guid");
	g_assert_true (lrg_property_info_get_read_only (info));
	g_clear_pointer (&props, g_ptr_array_unref);

	/* Transform: vector3 hints, uniform scale, read-only world */
	xform = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (xform), "location"), ==, "vector3");
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (xform), "rotation"), ==, "vector3");
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (xform), "scale"), ==, "vector3:uniform");
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (xform), "world");
	g_assert_true (lrg_property_info_get_read_only (info));
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, GRL_TYPE_VECTOR3);

	/* Visual of a light: colour, the light params with hints (listed
	 * even when unset), a custom key with a sanitized name */
	visual = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (visual), "kind"));
	g_assert_false (has_property (LRG_PROPERTY_SOURCE (visual), "primitive"));
	g_assert_false (has_property (LRG_PROPERTY_SOURCE (visual), "roughness"));
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (visual), "color"), ==, "color");
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (visual), "param-intensity"), ==, "range:0,100,0.01");
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (visual), "param-spot-angle"), ==, "angle:degrees");
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (visual), "param-range"));
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (visual), "param-cad-thickness");
	g_assert_nonnull (info);
	g_assert_cmpstr (lrg_property_info_get_label (info), ==, "cad:thickness");
	g_assert_cmpstr (lrg_property_info_get_group (info), ==, "Parameters");
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (visual), "param-intensity") &
	               LRG_PROPERTY_STATE_DEFAULT);

	/* Visual of a camera and of a mesh */
	{
		g_autoptr(LrgNodeVisual)       cam = lrg_node_visual_new (LRG_NODE_VISUAL_CAMERA);
		g_autoptr(LrgNodeVisual)       mesh = lrg_node_visual_new (LRG_NODE_VISUAL_MESH_ASSET);
		LrgNode                       *n2 = add_node (editor, "cam", 0, 0, 0);
		LrgNode                       *n3 = add_node (editor, "mesh", 0, 0, 0);
		g_autoptr(LrgEditorNodeSource) s2 = NULL;
		g_autoptr(LrgEditorNodeSource) s3 = NULL;

		lrg_node_set_visual (n2, cam);
		lrg_node_set_visual (n3, mesh);
		s2 = lrg_editor_node_source_new (editor, n2, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
		s3 = lrg_editor_node_source_new (editor, n3, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
		g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (s2), "param-fov"), ==, "range:1,179,0.5");
		g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (s3), "asset"), ==, "asset:model");
		g_assert_true (has_property (LRG_PROPERTY_SOURCE (s3), "roughness"));
	}

	/* Scripts: language, path with asset:script, enabled */
	scripts = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_SCRIPTS, NULL);
	g_assert_cmpstr (hint_of (LRG_PROPERTY_SOURCE (scripts), "script-0-path"), ==, "asset:script");
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (scripts), "script-0-language"));
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (scripts), "script-0-enabled"));
	{
		g_autofree gchar *path = lrg_editor_node_source_get_override_path (scripts, "script-0-enabled");

		g_assert_cmpstr (path, ==, "scripts/scripts/flicker.lua/enabled");
	}
}

/* ==========================================================================
 * Get / set through undo and redo
 * ========================================================================== */

static void
test_undo_redo (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "box", 1, 2, 3);
	g_autoptr(LrgNodeVisual)       vis = lrg_node_visual_new (LRG_NODE_VISUAL_LIGHT);
	g_autoptr(LrgEditorNodeSource) header = NULL;
	g_autoptr(LrgEditorNodeSource) xform = NULL;
	g_autoptr(LrgEditorNodeSource) visual = NULL;
	g_auto(GValue)                 v = G_VALUE_INIT;
	GrlVector3                     r;

	lrg_node_set_visual (node, vis);
	header = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_HEADER, NULL);
	xform = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	visual = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);

	/* Location: one undo step, undo and redo */
	set_vec3 (LRG_PROPERTY_SOURCE (xform), "location", 5, 6, 7);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 1);
	g_assert_cmpfloat (lrg_node_get_location (node)->x, ==, 5.0f);
	lrg_editor_undo (editor);
	g_assert_cmpfloat (lrg_node_get_location (node)->x, ==, 1.0f);
	g_assert_cmpfloat (get_vec3 (LRG_PROPERTY_SOURCE (xform), "location").z, ==, 3.0f);
	lrg_editor_redo (editor);
	g_assert_cmpfloat (lrg_node_get_location (node)->y, ==, 6.0f);

	/* Rotation is shown in degrees, stored in radians */
	set_vec3 (LRG_PROPERTY_SOURCE (xform), "rotation", 0, 90, 0);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_rotation (node)->y, G_PI / 2.0, EPS);
	r = get_vec3 (LRG_PROPERTY_SOURCE (xform), "rotation");
	g_assert_cmpfloat_with_epsilon (r.y, 90.0, EPS);
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (xform), "scale") &
	               LRG_PROPERTY_STATE_DEFAULT);

	/* Name and a flag */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "crate");
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (header), "name", &v));
	g_value_unset (&v);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "crate");
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (header), "visible", &v));
	g_value_unset (&v);
	g_assert_false (lrg_node_get_visible (node));
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (header), "visible") &
	                LRG_PROPERTY_STATE_DEFAULT);

	/* A visual param (a double, transformed from an int) */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 7);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (visual), "param-intensity", &v));
	g_value_unset (&v);
	g_assert_cmpfloat (lrg_node_visual_get_param_double (lrg_node_get_visual (node), "intensity", 0.0), ==, 7.0);

	/* Undo everything back, one step per edit */
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 5);
	lrg_editor_undo (editor);
	g_assert_null (lrg_node_visual_get_param (lrg_node_get_visual (node), "intensity"));
	lrg_editor_undo (editor);
	g_assert_true (lrg_node_get_visible (node));
	lrg_editor_undo (editor);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "box");
	lrg_editor_undo (editor);
	g_assert_cmpfloat (lrg_node_get_rotation (node)->y, ==, 0.0f);

	/* Reset puts the default back, undoably */
	g_assert_true (lrg_property_source_reset (LRG_PROPERTY_SOURCE (xform), "location"));
	g_assert_cmpfloat (lrg_node_get_location (node)->x, ==, 0.0f);
	lrg_editor_undo (editor);
	g_assert_cmpfloat (lrg_node_get_location (node)->x, ==, 5.0f);

	/* Read-only properties refuse writes */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "x");
	g_assert_false (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (header), "guid", &v));
}

/* ==========================================================================
 * Multi-node gesture through a real grid
 * ========================================================================== */

typedef struct
{
	LrgEditor       *editor;
	LrgCanvas       *canvas;
	LrgPropertyGrid *grid;
	LrgNode         *nodes[3];
} GridFixture;

static void
grid_fixture_set_up (GridFixture   *f,
                     gconstpointer  data)
{
	g_autoptr(GPtrArray) nodes = g_ptr_array_new ();
	g_autoptr(GPtrArray) sources = NULL;
	guint                i;

	f->editor = lrg_editor_new ();
	for (i = 0; i < 3; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("n%u", i);

		f->nodes[i] = add_node (f->editor, name, 0.0f, (gfloat) i, 10.0f * (gfloat) i);
		g_ptr_array_add (nodes, f->nodes[i]);
	}

	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 900.0f, 1200.0f);
	f->grid = lrg_property_grid_new ();
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->grid));
	lrg_widget_allocate (LRG_WIDGET (f->grid), 10.0f, 10.0f, 640.0f, 1180.0f);

	sources = lrg_editor_node_source_new_for_nodes (f->editor, nodes, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	g_assert_cmpuint (sources->len, ==, 3);
	lrg_property_grid_set_sources (f->grid, sources);
	lrg_editor_node_source_connect_grid (f->grid, f->editor);
	settle (f->canvas);
}

static void
grid_fixture_tear_down (GridFixture   *f,
                        gconstpointer  data)
{
	g_object_unref (f->grid);
	g_clear_object (&f->canvas);
	g_clear_object (&f->editor);
}

/* The centre of axis @axis of the vector editor of @name */
static void
axis_center (GridFixture *f,
             const gchar *name,
             guint        axis,
             gfloat      *x,
             gfloat      *y)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (f->grid, name);
	GrlRectangle    r;

	g_assert_nonnull (row);
	g_assert_true (LRG_IS_VECTOR_EDIT (lrg_property_row_get_editor (row)));
	g_assert_true (lrg_vector_edit_get_field_rect (LRG_VECTOR_EDIT (lrg_property_row_get_editor (row)), axis, &r));
	*x = r.x + r.width * 0.5f;
	*y = r.y + r.height * 0.5f;
}

static void
test_grid_gesture (GridFixture   *f,
                   gconstpointer  data)
{
	gfloat x;
	gfloat y;
	guint  i;
	guint  undo_before = lrg_editor_get_undo_count (f->editor);
	guint  redo_before;

	g_assert_cmpint (lrg_property_grid_get_apply_mode (f->grid), ==, LRG_PROPERTY_APPLY_DELEGATE);

	/* Scrub X of location over several frames on 3 nodes */
	axis_center (f, "location", 0, &x, &y);
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	for (i = 1; i <= 6; i++)
		drag_to (f->canvas, x + 10.0f * (gfloat) i, y);

	/* Live preview while dragging, nothing recorded yet */
	g_assert_cmpfloat (lrg_node_get_location (f->nodes[0])->x, >, 0.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before);
	release_at (f->canvas, x + 60.0f, y);

	/* One undo step; every node moved in X and kept its own Y and Z */
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);
	for (i = 0; i < 3; i++)
	{
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->x, >, 0.0f);
		g_assert_cmpfloat_with_epsilon (lrg_node_get_location (f->nodes[i])->x,
		                                lrg_node_get_location (f->nodes[0])->x, EPS);
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->y, ==, (gfloat) i);
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->z, ==, 10.0f * (gfloat) i);
	}

	/* Undo puts all three back */
	lrg_editor_undo (f->editor);
	for (i = 0; i < 3; i++)
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->x, ==, 0.0f);
	lrg_editor_redo (f->editor);
	g_assert_cmpfloat (lrg_node_get_location (f->nodes[2])->x, >, 0.0f);
	lrg_editor_undo (f->editor);
	settle (f->canvas);

	/* Escape mid-drag restores exactly and records nothing */
	undo_before = lrg_editor_get_undo_count (f->editor);
	redo_before = lrg_editor_get_redo_count (f->editor);
	axis_center (f, "location", 1, &x, &y);
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	for (i = 1; i <= 4; i++)
		drag_to (f->canvas, x + 12.0f * (gfloat) i, y);
	g_assert_cmpfloat (lrg_node_get_location (f->nodes[1])->y, !=, 1.0f);
	key_held (f->canvas, GRL_KEY_ESCAPE, x + 48.0f, y);
	release_at (f->canvas, x + 48.0f, y);
	for (i = 0; i < 3; i++)
	{
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->x, ==, 0.0f);
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->y, ==, (gfloat) i);
		g_assert_cmpfloat (lrg_node_get_location (f->nodes[i])->z, ==, 10.0f * (gfloat) i);
	}
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before);
	g_assert_cmpuint (lrg_editor_get_redo_count (f->editor), ==, redo_before);
	g_assert_true (lrg_vector_edit_get_axis_mixed (
		LRG_VECTOR_EDIT (lrg_property_row_get_editor (lrg_property_grid_get_row (f->grid, "location"))), 1));

	/* Reset from the grid: one undo step for all three */
	for (i = 0; i < 3; i++)
		lrg_node_set_scale_xyz (f->nodes[i], 2.0f, 2.0f, 2.0f);
	undo_before = lrg_editor_get_undo_count (f->editor);
	lrg_property_grid_reset_property (f->grid, "scale");
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);
	for (i = 0; i < 3; i++)
		g_assert_cmpfloat (lrg_node_get_scale (f->nodes[i])->x, ==, 1.0f);

	lrg_editor_node_source_disconnect_grid (f->grid);
}

/* Mixed values across nodes */
static void
test_grid_mixed (GridFixture   *f,
                 gconstpointer  data)
{
	LrgVectorEdit *edit = LRG_VECTOR_EDIT (lrg_property_row_get_editor (lrg_property_grid_get_row (f->grid, "location")));
	LrgPropertyRow *row = lrg_property_grid_get_row (f->grid, "location");

	/* X agrees (0), Y and Z differ */
	g_assert_true (lrg_property_row_get_mixed (row));
	g_assert_false (lrg_vector_edit_get_axis_mixed (edit, 0));
	g_assert_true (lrg_vector_edit_get_axis_mixed (edit, 1));
	g_assert_true (lrg_vector_edit_get_axis_mixed (edit, 2));
	g_assert_false (lrg_property_row_get_mixed (lrg_property_grid_get_row (f->grid, "scale")));

	/* An undoable change behind the grid's back reaches it */
	{
		gfloat loc[3] = { 0.0f, 5.0f, 0.0f };
		guint  i;

		for (i = 0; i < 3; i++)
			lrg_editor_transform_node (f->editor, f->nodes[i], loc, NULL, NULL, FALSE);
	}
	settle (f->canvas);
	g_assert_false (lrg_vector_edit_get_axis_mixed (edit, 1));
	g_assert_false (lrg_property_row_get_mixed (row));
}

/* ==========================================================================
 * Components
 * ========================================================================== */

static void
test_component (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "orc", 0, 0, 0);
	g_autoptr(LrgComponentDesc)    desc = lrg_component_desc_new ("brain");
	g_autoptr(LrgComponentDesc)    alien = lrg_component_desc_new ("no-such-type");
	g_autoptr(LrgEditorNodeSource) src = NULL;
	g_autoptr(LrgEditorNodeSource) raw = NULL;
	g_autoptr(LrgPropertyInfo)     info = NULL;
	g_auto(GValue)                 v = G_VALUE_INIT;
	LrgComponentDesc              *live;

	if (!register_brain ())
	{
		g_test_skip ("No engine registry");
		return;
	}

	/* "mode" stored as a nick string, as a YAML load keeps it */
	lrg_component_desc_set_string (desc, "mode", "patrol");
	lrg_component_desc_set_int (desc, "hp", 40);
	lrg_node_add_component (node, desc);
	lrg_component_desc_set_double (alien, "weight", 3.5);
	lrg_component_desc_set_string (alien, "odd key!", "x");
	lrg_node_add_component (node, alien);

	src = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_COMPONENT, "brain");

	/* The pspecs of the registered type, by type */
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "mode");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, test_mode_get_type ());
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "tags");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, test_tags_get_type ());
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "label");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, G_TYPE_STRING);
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "awake");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, G_TYPE_BOOLEAN);
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "hp");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, G_TYPE_INT);
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (src), "speed");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, G_TYPE_DOUBLE);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "component-type") &
	                LRG_PROPERTY_STATE_INVALID);

	/* Values: stored ones converted to the pspec type, unset ones are the
	 * pspec default (DEFAULT state) */
	g_assert_true (lrg_property_source_get_value (LRG_PROPERTY_SOURCE (src), "mode", &v));
	g_assert_cmpint (g_value_get_enum (&v), ==, TEST_MODE_PATROL);
	g_value_unset (&v);
	g_assert_true (lrg_property_source_get_value (LRG_PROPERTY_SOURCE (src), "hp", &v));
	g_assert_cmpint (g_value_get_int (&v), ==, 40);
	g_value_unset (&v);
	g_assert_true (lrg_property_source_get_value (LRG_PROPERTY_SOURCE (src), "speed", &v));
	g_assert_cmpfloat (g_value_get_double (&v), ==, 2.5);
	g_value_unset (&v);
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "speed") & LRG_PROPERTY_STATE_DEFAULT);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "hp") & LRG_PROPERTY_STATE_DEFAULT);

	/* Writes: enum, flags, string, bool, double; each one undo step */
	g_value_init (&v, test_mode_get_type ());
	g_value_set_enum (&v, TEST_MODE_CHASE);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "mode", &v));
	g_value_unset (&v);
	g_value_init (&v, test_tags_get_type ());
	g_value_set_flags (&v, TEST_TAG_BOSS | TEST_TAG_FLYER);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "tags", &v));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "warlord");
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "label", &v));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "awake", &v));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 99.0);    /* clamped by the pspec to 50 */
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "speed", &v));
	g_value_unset (&v);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 5);

	live = lrg_node_find_component (node, "brain");
	g_assert_cmpint (g_value_get_enum (lrg_component_desc_get_value (live, "mode")), ==, TEST_MODE_CHASE);
	g_assert_cmpuint (g_value_get_flags (lrg_component_desc_get_value (live, "tags")), ==,
	                  TEST_TAG_BOSS | TEST_TAG_FLYER);
	g_assert_cmpstr (g_value_get_string (lrg_component_desc_get_value (live, "label")), ==, "warlord");
	g_assert_false (g_value_get_boolean (lrg_component_desc_get_value (live, "awake")));
	g_assert_cmpfloat (g_value_get_double (lrg_component_desc_get_value (live, "speed")), ==, 50.0);

	/* Undo the speed edit; reset removes the stored key (back to default) */
	lrg_editor_undo (editor);
	live = lrg_node_find_component (node, "brain");
	g_assert_false (lrg_component_desc_has (live, "speed"));
	g_assert_true (lrg_property_source_reset (LRG_PROPERTY_SOURCE (src), "hp"));
	live = lrg_node_find_component (node, "brain");
	g_assert_false (lrg_component_desc_has (live, "hp"));
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "hp") & LRG_PROPERTY_STATE_DEFAULT);
	lrg_editor_undo (editor);
	live = lrg_node_find_component (node, "brain");
	g_assert_cmpint (g_value_get_int (lrg_component_desc_get_value (live, "hp")), ==, 40);

	/* An unregistered type: invalid type row, raw typed values */
	raw = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_COMPONENT, "no-such-type");
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (raw), "component-type") &
	               LRG_PROPERTY_STATE_INVALID);
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (raw), "weight");
	g_assert_cmpint (lrg_property_info_get_value_type (info), ==, G_TYPE_DOUBLE);
	g_clear_pointer (&info, lrg_property_info_unref);
	info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (raw), "odd-key-");
	g_assert_nonnull (info);
	g_assert_cmpstr (lrg_property_info_get_label (info), ==, "odd key!");
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 4.0);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (raw), "weight", &v));
	g_assert_cmpfloat (g_value_get_double (lrg_component_desc_get_value (
		lrg_node_find_component (node, "no-such-type"), "weight")), ==, 4.0);

	/* Sources for nodes without the component are left out */
	{
		g_autoptr(GPtrArray) nodes = g_ptr_array_new ();
		g_autoptr(GPtrArray) sources = NULL;

		g_ptr_array_add (nodes, node);
		g_ptr_array_add (nodes, add_node (editor, "plain", 0, 0, 0));
		sources = lrg_editor_node_source_new_for_nodes (editor, nodes, LRG_EDITOR_NODE_SECTION_COMPONENT, "brain");
		g_assert_cmpuint (sources->len, ==, 1);
	}
}

/* ==========================================================================
 * Prefab overrides, animation, invalid assets
 * ========================================================================== */

static void
test_prefab_override (void)
{
	g_autoptr(GError)              error = NULL;
	g_autofree gchar              *dir = g_dir_make_tmp ("lrg-node-source-XXXXXX", &error);
	g_autofree gchar              *path = NULL;
	g_autoptr(LrgNode)             crate = lrg_node_new ("Crate");
	g_autoptr(LrgNode)             lid = lrg_node_new ("Lid");
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	g_autoptr(LrgEditorNodeSource) src = NULL;
	g_autoptr(LrgEditorNodeSource) root_src = NULL;
	LrgNode                       *inst;
	LrgNode                       *inst_lid;
	g_autofree gchar              *opath = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "crate.rprefab", NULL);
	lrg_node_set_location_xyz (lid, 0.0f, 1.0f, 0.0f);
	lrg_node_add_child (crate, lid);
	g_assert_true (lrg_prefab_save (crate, path, &error));
	g_assert_no_error (error);

	inst = lrg_editor_instantiate_prefab (editor, path, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (inst);
	inst_lid = g_ptr_array_index (lrg_node_get_children (inst), 0);

	src = lrg_editor_node_source_new (editor, inst_lid, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	opath = lrg_editor_node_source_get_override_path (src, "location");
	g_assert_cmpstr (opath, ==, "transform/location");
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "location") &
	                LRG_PROPERTY_STATE_OVERRIDDEN);

	/* An edit marks the override */
	set_vec3 (LRG_PROPERTY_SOURCE (src), "location", 0.0f, 3.0f, 0.0f);
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "location") &
	               LRG_PROPERTY_STATE_OVERRIDDEN);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "scale") &
	                LRG_PROPERTY_STATE_OVERRIDDEN);

	/* Undo clears it again */
	lrg_editor_undo (editor);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "location") &
	                LRG_PROPERTY_STATE_OVERRIDDEN);
	lrg_editor_redo (editor);
	g_assert_cmpfloat (lrg_node_get_location (inst_lid)->y, ==, 3.0f);

	/* Revert: back to the prefab's value, override gone, one undo step */
	g_assert_true (lrg_editor_node_source_revert_override (src, "location", &error));
	g_assert_no_error (error);
	g_assert_cmpfloat (lrg_node_get_location (inst_lid)->y, ==, 1.0f);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "location") &
	                LRG_PROPERTY_STATE_OVERRIDDEN);
	g_assert_false (lrg_editor_node_source_revert_override (src, "location", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	lrg_editor_undo (editor);
	g_assert_cmpfloat (lrg_node_get_location (inst_lid)->y, ==, 3.0f);

	/* Non-instance nodes are never overridden */
	root_src = lrg_editor_node_source_new (editor, add_node (editor, "free", 0, 0, 0),
	                                       LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	set_vec3 (LRG_PROPERTY_SOURCE (root_src), "scale", 2.0f, 2.0f, 2.0f);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (root_src), "scale") &
	                LRG_PROPERTY_STATE_OVERRIDDEN);

	g_clear_object (&src);
	g_clear_object (&root_src);
	g_clear_object (&editor);
	remove_tree (dir);
}

static void
test_animated (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "door", 0, 0, 0);
	g_autoptr(LrgNodeVisual)       vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgEditorNodeSource) xform = NULL;
	g_autoptr(LrgEditorNodeSource) visual = NULL;

	lrg_node_set_visual (node, vis);
	xform = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	visual = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (xform), "location") &
	                LRG_PROPERTY_STATE_ANIMATED);

	/* Key location.x and roughness */
	g_assert_true (lrg_editor_timeline_set_key (editor, "open", node, "location.x", 0.0f, 1.0f,
	                                            LRG_EASING_LINEAR, FALSE));
	g_assert_true (lrg_editor_timeline_set_key (editor, "open", node, "roughness", 0.0f, 0.2f,
	                                            LRG_EASING_LINEAR, FALSE));
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (xform), "location") &
	               LRG_PROPERTY_STATE_ANIMATED);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (xform), "scale") &
	                LRG_PROPERTY_STATE_ANIMATED);
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (visual), "roughness") &
	               LRG_PROPERTY_STATE_ANIMATED);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (visual), "metallic") &
	                LRG_PROPERTY_STATE_ANIMATED);
}

static void
test_invalid_asset (void)
{
	g_autoptr(GError)              error = NULL;
	g_autofree gchar              *dir = g_dir_make_tmp ("lrg-node-source-XXXXXX", &error);
	g_autofree gchar              *models = NULL;
	g_autofree gchar              *file = NULL;
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "rock", 0, 0, 0);
	g_autoptr(LrgNodeVisual)       vis = lrg_node_visual_new (LRG_NODE_VISUAL_MESH_ASSET);
	g_autoptr(LrgEditorNodeSource) src = NULL;
	g_auto(GValue)                 v = G_VALUE_INIT;

	g_assert_no_error (error);
	models = g_build_filename (dir, "models", NULL);
	g_assert_cmpint (g_mkdir (models, 0755), ==, 0);
	file = g_build_filename (models, "rock.glb", NULL);
	g_assert_true (g_file_set_contents (file, "glTF", -1, &error));

	lrg_node_visual_set_asset (vis, "models/missing.glb");
	lrg_node_set_visual (node, vis);
	src = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	lrg_editor_node_source_set_asset_root (src, dir);
	g_assert_cmpstr (lrg_editor_node_source_get_asset_root (src), ==, dir);
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "asset") &
	               LRG_PROPERTY_STATE_INVALID);

	/* An existing file under the root, and a guid, are fine */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "models/rock.glb");
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "asset", &v));
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "asset") &
	                LRG_PROPERTY_STATE_INVALID);
	g_value_set_string (&v, "3f2a9c0d6b1e4f5a8c7d2e1f0a9b8c7d");
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), "asset", &v));
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), "asset") &
	                LRG_PROPERTY_STATE_INVALID);

	g_clear_object (&src);
	g_clear_object (&editor);
	remove_tree (dir);
}

/* ==========================================================================
 * Coalesced change emission
 * ========================================================================== */

typedef struct
{
	guint  total;
	guint  null_count;
	gchar *last;
} ChangeLog;

static void
on_property_changed (LrgPropertySource *src,
                     const gchar       *name,
                     gpointer           user_data)
{
	ChangeLog *log = user_data;

	log->total++;
	if (name == NULL)
		log->null_count++;
	g_free (log->last);
	log->last = g_strdup (name);
}

static void
test_change_emission (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *node = add_node (editor, "cube", 0, 0, 0);
	g_autoptr(LrgEditorNodeSource) xform = NULL;
	g_autoptr(LrgEditorNodeSource) visual = NULL;
	g_auto(GValue)                 v = G_VALUE_INIT;
	ChangeLog                      log = { 0, 0, NULL };
	ChangeLog                      vlog = { 0, 0, NULL };
	gfloat                         loc[3] = { 4.0f, 0.0f, 0.0f };

	xform = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	visual = lrg_editor_node_source_new (editor, node, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	g_ptr_array_unref (lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (xform)));
	g_ptr_array_unref (lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (visual)));
	g_signal_connect (xform, "property-changed", G_CALLBACK (on_property_changed), &log);
	g_signal_connect (visual, "property-changed", G_CALLBACK (on_property_changed), &vlog);

	/* An editor command on the node: location and world, once each */
	lrg_editor_transform_node (editor, node, loc, NULL, NULL, FALSE);
	g_assert_cmpuint (log.total, ==, 2);
	g_assert_cmpuint (vlog.total, ==, 0);

	/* A notification without a real change emits nothing */
	g_signal_emit_by_name (editor, "changed");
	g_assert_cmpuint (log.total, ==, 2);

	/* Undo comes back the same way */
	lrg_editor_undo (editor);
	g_assert_cmpuint (log.total, ==, 4);

	/* A visual kind change changes the visual schema: one NULL emission */
	g_value_init (&v, LRG_TYPE_NODE_VISUAL_KIND);
	g_value_set_enum (&v, LRG_NODE_VISUAL_PRIMITIVE);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (visual), "kind", &v));
	g_assert_cmpuint (vlog.null_count, ==, 1);
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (visual), "primitive"));
	g_assert_true (has_property (LRG_PROPERTY_SOURCE (visual), "roughness"));
	lrg_editor_undo (editor);
	g_assert_cmpuint (vlog.null_count, ==, 2);
	g_assert_false (has_property (LRG_PROPERTY_SOURCE (visual), "primitive"));

	g_free (log.last);
	g_free (vlog.last);
}

/* Gesture API without a grid: N nodes in one group */
static void
test_gesture_api (void)
{
	g_autoptr(LrgEditor)           editor = lrg_editor_new ();
	LrgNode                       *a = add_node (editor, "a", 0, 0, 0);
	LrgNode                       *b = add_node (editor, "b", 1, 0, 0);
	g_autoptr(LrgEditorNodeSource) sa = lrg_editor_node_source_new (editor, a, LRG_EDITOR_NODE_SECTION_HEADER, NULL);
	g_autoptr(LrgEditorNodeSource) sb = lrg_editor_node_source_new (editor, b, LRG_EDITOR_NODE_SECTION_HEADER, NULL);
	g_auto(GValue)                 v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);

	lrg_editor_node_source_begin_gesture (sa);
	lrg_editor_node_source_begin_gesture (sb);
	g_assert_true (lrg_editor_node_source_in_gesture (sa));
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (sa), "locked", &v));
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (sb), "locked", &v));
	g_assert_true (lrg_node_get_locked (a));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 0);
	lrg_editor_begin_group (editor, "Lock");
	g_assert_true (lrg_editor_node_source_end_gesture (sa, TRUE, "Lock"));
	g_assert_true (lrg_editor_node_source_end_gesture (sb, TRUE, "Lock"));
	lrg_editor_end_group (editor);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 1);
	lrg_editor_undo (editor);
	g_assert_false (lrg_node_get_locked (a));
	g_assert_false (lrg_node_get_locked (b));

	/* Cancel restores */
	lrg_editor_node_source_begin_gesture (sa);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (sa), "locked", &v));
	g_assert_false (lrg_editor_node_source_end_gesture (sa, FALSE, NULL));
	g_assert_false (lrg_node_get_locked (a));
	g_assert_false (lrg_editor_node_source_in_gesture (sa));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

/* The audio emitter's suggested radius is the audible distance (50). */
static void
test_suggested_audio_radius (void)
{
	gdouble radius = 0.0;

	g_assert_true (lrg_editor_node_source_get_suggested_double (LRG_NODE_VISUAL_AUDIO_EMITTER,
	                                                           "radius", &radius));
	g_assert_cmpfloat (radius, ==, 50.0);
	g_assert_false (lrg_editor_node_source_get_suggested_double (LRG_NODE_VISUAL_AUDIO_EMITTER,
	                                                            "no-such", &radius));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/node-source/listing", test_listing);
	g_test_add_func ("/editor/node-source/undo-redo", test_undo_redo);
	g_test_add ("/editor/node-source/grid/gesture", GridFixture, NULL,
	            grid_fixture_set_up, test_grid_gesture, grid_fixture_tear_down);
	g_test_add ("/editor/node-source/grid/mixed", GridFixture, NULL,
	            grid_fixture_set_up, test_grid_mixed, grid_fixture_tear_down);
	g_test_add_func ("/editor/node-source/component", test_component);
	g_test_add_func ("/editor/node-source/prefab-override", test_prefab_override);
	g_test_add_func ("/editor/node-source/animated", test_animated);
	g_test_add_func ("/editor/node-source/invalid-asset", test_invalid_asset);
	g_test_add_func ("/editor/node-source/change-emission", test_change_emission);
	g_test_add_func ("/editor/node-source/gesture-api", test_gesture_api);
	g_test_add_func ("/editor/node-source/suggested-audio-radius", test_suggested_audio_radius);

	return g_test_run ();
}
