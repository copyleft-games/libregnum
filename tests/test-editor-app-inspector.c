/* test-editor-app-inspector.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The inspector panel (LrgEditorInspectorPanel), driven through the
 * canvas of a headless LrgEditorApp (tests/editor-app-fixture.h) and its
 * IPC server: multi-node scrubs as one undo step, Escape, mixed values,
 * enum combos on components, the Add Component popup, script drops,
 * prefab overrides, the lock, live inspection during play with Apply to
 * Level, the inspector.* methods, and the enum-nick round trip of the
 * level serializer. Also the two toolkit additions the property grid
 * needs: LrgSpinBox scrub gestures and the LrgCheckbox inconsistent
 * state.
 */

#include "editor-app-fixture.h"

/* ==========================================================================
 * test-health: a registered component with an enum, a flags, an int and
 * a float property; update() moves its owner by "speed" on X
 * ========================================================================== */

typedef enum
{
	TEST_KIND_MELEE,
	TEST_KIND_RANGED,
	TEST_KIND_MAGIC
} TestKind;

typedef enum
{
	TEST_RESIST_FIRE = 1 << 0,
	TEST_RESIST_ICE  = 1 << 1,
	TEST_RESIST_ACID = 1 << 2
} TestResist;

static GType
test_kind_get_type (void)
{
	static gsize            type = 0;
	static const GEnumValue values[] = {
		{ TEST_KIND_MELEE, "TEST_KIND_MELEE", "melee" },
		{ TEST_KIND_RANGED, "TEST_KIND_RANGED", "ranged" },
		{ TEST_KIND_MAGIC, "TEST_KIND_MAGIC", "magic" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type))
		g_once_init_leave (&type, g_enum_register_static ("TestKind", values));
	return (GType) type;
}

static GType
test_resist_get_type (void)
{
	static gsize             type = 0;
	static const GFlagsValue values[] = {
		{ TEST_RESIST_FIRE, "TEST_RESIST_FIRE", "fire" },
		{ TEST_RESIST_ICE, "TEST_RESIST_ICE", "ice" },
		{ TEST_RESIST_ACID, "TEST_RESIST_ACID", "acid" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type))
		g_once_init_leave (&type, g_flags_register_static ("TestResist", values));
	return (GType) type;
}

#define TEST_TYPE_HEALTH (test_health_get_type ())
G_DECLARE_FINAL_TYPE (TestHealth, test_health, TEST, HEALTH, LrgComponent)

struct _TestHealth
{
	LrgComponent parent_instance;
	gint         max_hp;
	gfloat       speed;
	TestKind     kind;
	guint        resist;
};

G_DEFINE_FINAL_TYPE (TestHealth, test_health, LRG_TYPE_COMPONENT)

enum
{
	HEALTH_PROP_0,
	HEALTH_PROP_MAX_HP,
	HEALTH_PROP_SPEED,
	HEALTH_PROP_KIND,
	HEALTH_PROP_RESIST
};

static void
test_health_update (LrgComponent *component,
                    gfloat        delta)
{
	TestHealth    *self = TEST_HEALTH (component);
	LrgGameObject *owner = lrg_component_get_owner (component);

	if (owner != NULL)
		grl_entity_set_position_xy (GRL_ENTITY (owner), grl_entity_get_x (GRL_ENTITY (owner)) + self->speed * delta,
		                            grl_entity_get_y (GRL_ENTITY (owner)));
}

static void
test_health_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	TestHealth *self = TEST_HEALTH (object);

	switch (prop_id)
	{
	case HEALTH_PROP_MAX_HP:
		g_value_set_int (value, self->max_hp);
		break;
	case HEALTH_PROP_SPEED:
		g_value_set_float (value, self->speed);
		break;
	case HEALTH_PROP_KIND:
		g_value_set_enum (value, self->kind);
		break;
	case HEALTH_PROP_RESIST:
		g_value_set_flags (value, self->resist);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_health_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	TestHealth *self = TEST_HEALTH (object);

	switch (prop_id)
	{
	case HEALTH_PROP_MAX_HP:
		self->max_hp = g_value_get_int (value);
		break;
	case HEALTH_PROP_SPEED:
		self->speed = g_value_get_float (value);
		break;
	case HEALTH_PROP_KIND:
		self->kind = g_value_get_enum (value);
		break;
	case HEALTH_PROP_RESIST:
		self->resist = g_value_get_flags (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_health_class_init (TestHealthClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->get_property = test_health_get_property;
	object_class->set_property = test_health_set_property;
	LRG_COMPONENT_CLASS (klass)->update = test_health_update;

	g_object_class_install_property (object_class, HEALTH_PROP_MAX_HP,
		g_param_spec_int ("max-hp", "Max HP", "Hit points", 0, 10000, 100, rw));
	g_object_class_install_property (object_class, HEALTH_PROP_SPEED,
		g_param_spec_float ("speed", "Speed", "Units per second", -1000.0f, 1000.0f, 0.0f, rw));
	g_object_class_install_property (object_class, HEALTH_PROP_KIND,
		g_param_spec_enum ("kind", "Kind", "Attack kind", test_kind_get_type (), TEST_KIND_MELEE, rw));
	g_object_class_install_property (object_class, HEALTH_PROP_RESIST,
		g_param_spec_flags ("resist", "Resist", "Resistances", test_resist_get_type (), 0, rw));
}

static void
test_health_init (TestHealth *self)
{
	self->max_hp = 100;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgEditorInspectorPanel *
inspector (EditorAppFixture *f)
{
	LrgDockPanel *panel = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "inspector");

	g_assert_nonnull (panel);
	g_assert_true (LRG_IS_EDITOR_INSPECTOR_PANEL (lrg_dock_panel_get_child (panel)));
	return LRG_EDITOR_INSPECTOR_PANEL (lrg_dock_panel_get_child (panel));
}

/* add_node: a node under the root at (@x, @y, 0), not selected */
static LrgNode *
add_node (EditorAppFixture *f,
          const gchar      *name,
          gfloat            x,
          gfloat            y)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	lrg_node_set_location_xyz (node, x, y, 0.0f);
	lrg_editor_add_node (f->editor, node, NULL);
	return node;
}

/* add_health: a test-health component with @kind and @speed */
static void
add_health (LrgNode  *node,
            TestKind  kind,
            gdouble   speed)
{
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("test-health");
	g_auto(GValue)              v = G_VALUE_INIT;

	g_value_init (&v, test_kind_get_type ());
	g_value_set_enum (&v, kind);
	lrg_component_desc_set_value (desc, "kind", &v);
	lrg_component_desc_set_double (desc, "speed", speed);
	lrg_node_add_component (node, desc);
}

static void
select_nodes (EditorAppFixture *f,
              LrgNode          *a,
              LrgNode          *b,
              LrgNode          *c)
{
	g_autoptr(GPtrArray) nodes = g_ptr_array_new ();

	if (a != NULL)
		g_ptr_array_add (nodes, a);
	if (b != NULL)
		g_ptr_array_add (nodes, b);
	if (c != NULL)
		g_ptr_array_add (nodes, c);
	lrg_editor_select_nodes (f->editor, nodes, FALSE);
	efx_frames (f, 2);
}

static LrgWidget *
editor_of (LrgPropertyGrid *grid,
           const gchar     *name)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (grid, name);

	g_assert_nonnull (row);
	return lrg_property_row_get_editor (row);
}

/* A press, frames of drag with the button held, Escape, release */
static void
key_while_held (EditorAppFixture *f,
                GrlKey            key)
{
	efx_frame_with (f, &key, 1, NULL, 0, NULL);
	efx_frame_with (f, NULL, 0, &key, 1, NULL);
}

static void
press (EditorAppFixture *f,
       gfloat            x,
       gfloat            y)
{
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
}

static void
drag_to (EditorAppFixture *f,
         gfloat            x,
         gfloat            y)
{
	f->x = x;
	f->y = y;
	efx_frame (f);
}

static void
release (EditorAppFixture *f)
{
	efx_button (f, f->x, f->y, GRL_MOUSE_BUTTON_LEFT, FALSE);
}

/* reveal: scroll the inspector so @widget is on screen, and settle */
static void
reveal (EditorAppFixture *f,
        LrgWidget        *widget)
{
	LrgWidget *scroll = efx_find (f, "inspector-scroll");

	g_assert_nonnull (widget);
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (scroll), widget);
	efx_frames (f, 40);
}

static void
click_revealed (EditorAppFixture *f,
                const gchar      *name)
{
	LrgWidget *widget = efx_find (f, name);

	reveal (f, widget);
	efx_click_widget (f, widget);
}

static const GValue *
desc_value (LrgNode     *node,
            guint        index,
            const gchar *key)
{
	return lrg_component_desc_get_value (g_ptr_array_index (lrg_node_get_components (node), index), key);
}

static JsonObject *
result_object (JsonNode *node)
{
	g_assert_true (JSON_NODE_HOLDS_OBJECT (node));
	return json_node_get_object (node);
}

/* ==========================================================================
 * Level and structure
 * ========================================================================== */

static void
test_level_section (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgPropertyGrid         *grid = lrg_editor_inspector_panel_get_level_grid (insp);
	LrgPropertyRow          *row;
	g_auto(GValue)           v = G_VALUE_INIT;

	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_inspector_panel_get_nodes (insp)->len, ==, 0);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-level")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-section-transform")));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (efx_find (f, "inspector-title"))), ==, "Level");

	/* Renaming the level is undoable */
	row = lrg_property_grid_get_row (grid, "name");
	g_assert_nonnull (row);
	g_assert_true (lrg_property_row_get_read_only (lrg_property_grid_get_row (grid, "file")));
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "Dungeon");
	lrg_property_row_apply_value (row, &v);
	g_assert_cmpstr (lrg_level_get_name (lrg_editor_get_level (f->editor)), ==, "Dungeon");
	lrg_editor_undo (f->editor);
	g_assert_cmpstr (lrg_level_get_name (lrg_editor_get_level (f->editor)), !=, "Dungeon");

	/* Selecting a node swaps the level for the node sections */
	select_nodes (f, add_node (f, "Cube", 0, 0), NULL, NULL);
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-level")));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-section-transform")));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (efx_find (f, "inspector-title"))), ==, "Cube");
}

/* Rows are reused when another node of the same shape is selected */
static void
test_rows_reused (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgNode                 *a = add_node (f, "A", 1, 0);
	LrgNode                 *b = add_node (f, "B", 2, 0);
	LrgPropertyGrid         *grid;
	LrgPropertyRow          *row;

	select_nodes (f, a, NULL, NULL);
	grid = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	row = lrg_property_grid_get_row (grid, "location");
	g_assert_nonnull (row);
	select_nodes (f, b, NULL, NULL);
	g_assert_true (lrg_property_grid_get_row (grid, "location") == row);
	g_assert_true (lrg_editor_inspector_panel_get_nodes (insp)->len == 1);
	g_assert_true (g_ptr_array_index (lrg_editor_inspector_panel_get_nodes (insp), 0) == b);
}

/* ==========================================================================
 * Multi-edit through the canvas
 * ========================================================================== */

static void
scrub_location_x (EditorAppFixture *f,
                  gfloat            dx,
                  gboolean          cancel)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgPropertyGrid         *grid = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_TRANSFORM,
	                                                                     NULL);
	LrgVectorEdit           *edit = LRG_VECTOR_EDIT (editor_of (grid, "location"));
	GrlRectangle             r;
	gfloat                   x;
	gfloat                   y;
	gint                     i;

	g_assert_true (lrg_vector_edit_get_field_rect (edit, 0, &r));
	x = r.x + r.width * 0.5f;
	y = r.y + r.height * 0.5f;
	press (f, x, y);
	for (i = 1; i <= 5; i++)
		drag_to (f, x + dx * (gfloat) i, y);
	if (cancel)
		key_while_held (f, GRL_KEY_ESCAPE);
	release (f);
}

/* Three nodes, one scrub of X: one undo step, all get the same X, each
 * keeps its own Y */
static void
test_multi_scrub (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgNode *a = add_node (f, "A", 1, 10);
	LrgNode *b = add_node (f, "B", 2, 20);
	LrgNode *c = add_node (f, "C", 3, 30);
	guint    undo;

	select_nodes (f, a, b, c);
	g_assert_cmpuint (lrg_editor_inspector_panel_get_nodes (inspector (f))->len, ==, 3);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (efx_find (f, "inspector-title"))), ==, "3 nodes");
	undo = lrg_editor_get_undo_count (f->editor);

	/* X is mixed, so the scrub starts from 0: after the drag threshold,
	 * 4 frames of 13 units at 0.05 per 2 units give 1.3 */
	scrub_location_x (f, 13.0f, FALSE);

	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 1.3f, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, lrg_node_get_location (b)->x, 1e-5);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (b)->x, lrg_node_get_location (c)->x, 1e-5);
	g_assert_cmpfloat (lrg_node_get_location (a)->y, ==, 10.0f);
	g_assert_cmpfloat (lrg_node_get_location (b)->y, ==, 20.0f);
	g_assert_cmpfloat (lrg_node_get_location (c)->y, ==, 30.0f);

	/* One undo puts all three back */
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat (lrg_node_get_location (a)->x, ==, 1.0f);
	g_assert_cmpfloat (lrg_node_get_location (b)->x, ==, 2.0f);
	g_assert_cmpfloat (lrg_node_get_location (c)->x, ==, 3.0f);
}

/* Escape in the middle of a scrub restores every node and records nothing */
static void
test_scrub_escape (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgNode *a = add_node (f, "A", 1, 0);
	LrgNode *b = add_node (f, "B", 2, 0);
	guint    undo;

	select_nodes (f, a, b, NULL);
	undo = lrg_editor_get_undo_count (f->editor);
	scrub_location_x (f, 10.0f, TRUE);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);
	g_assert_cmpfloat (lrg_node_get_location (a)->x, ==, 1.0f);
	g_assert_cmpfloat (lrg_node_get_location (b)->x, ==, 2.0f);
}

/* Values the nodes disagree on show as mixed */
static void
test_mixed (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgNode                 *a = add_node (f, "A", 1, 5);
	LrgNode                 *b = add_node (f, "B", 2, 5);
	LrgPropertyGrid         *transform;
	LrgPropertyGrid         *header;
	LrgVectorEdit           *edit;

	lrg_node_set_visible (b, FALSE);
	select_nodes (f, a, b, NULL);
	transform = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	header = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_HEADER, NULL);
	edit = LRG_VECTOR_EDIT (editor_of (transform, "location"));
	g_assert_true (lrg_property_row_get_mixed (lrg_property_grid_get_row (transform, "location")));
	g_assert_true (lrg_vector_edit_get_axis_mixed (edit, 0));
	g_assert_false (lrg_vector_edit_get_axis_mixed (edit, 1));
	g_assert_true (lrg_property_row_get_mixed (lrg_property_grid_get_row (header, "visible")));
	g_assert_false (lrg_property_row_get_mixed (lrg_property_grid_get_row (transform, "scale")));

	/* Components every node has; the others are left out */
	add_health (a, TEST_KIND_MELEE, 0.0);
	select_nodes (f, a, b, NULL);
	g_assert_null (lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_COMPONENT, "test-health"));
	add_health (b, TEST_KIND_RANGED, 0.0);
	select_nodes (f, b, a, NULL);
	select_nodes (f, a, b, NULL);
	g_assert_nonnull (lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_COMPONENT, "test-health"));
	g_assert_true (lrg_property_row_get_mixed (
		lrg_property_grid_get_row (lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_COMPONENT,
		                                                                "test-health"), "kind")));
}

/* An enum property of a component, edited with its combo box */
static void
test_enum_combo (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgNode         *node = add_node (f, "Orc", 0, 0);
	LrgPropertyGrid *grid;
	LrgWidget       *combo;
	const GValue    *v;
	guint            undo;

	add_health (node, TEST_KIND_MELEE, 0.0);
	select_nodes (f, node, NULL, NULL);
	grid = lrg_editor_inspector_panel_get_grid (inspector (f), LRG_EDITOR_NODE_SECTION_COMPONENT, "test-health");
	g_assert_nonnull (grid);
	combo = editor_of (grid, "kind");
	g_assert_true (LRG_IS_COMBO_BOX (combo));
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (combo)), ==, "melee");
	undo = lrg_editor_get_undo_count (f->editor);

	/* Focus the combo and press Down: the next value, one undo step */
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), combo);
	efx_key (f, GRL_KEY_DOWN, 0);
	v = desc_value (node, 0, "kind");
	g_assert_nonnull (v);
	g_assert_true (G_VALUE_HOLDS (v, test_kind_get_type ()));
	g_assert_cmpint (g_value_get_enum (v), ==, TEST_KIND_RANGED);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	lrg_editor_undo (f->editor);
	efx_frame (f);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (editor_of (grid, "kind"))), ==, "melee");
}

/* ==========================================================================
 * Add Component, scripts
 * ========================================================================== */

static void
test_add_component (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgNode   *node = add_node (f, "Hero", 0, 0);
	LrgCanvas *canvas = lrg_editor_app_get_canvas (f->app);
	LrgWidget *item;
	LrgWidget *popup;

	select_nodes (f, node, NULL, NULL);
	click_revealed (f, "inspector-add-component");
	efx_frames (f, 2);
	popup = lrg_canvas_get_top_popup (canvas);
	g_assert_nonnull (popup);
	g_assert_cmpstr (lrg_widget_get_name (popup), ==, "inspector-picker");

	/* Typing filters the registered component types */
	efx_type (f, "test-hea");
	efx_frames (f, 2);
	item = lrg_widget_find (popup, "inspector-picker-item-0");
	g_assert_nonnull (item);
	efx_click_widget (f, item);
	efx_frames (f, 30);

	g_assert_cmpuint (lrg_node_get_components (node)->len, ==, 1);
	g_assert_cmpstr (lrg_component_desc_get_type_name (g_ptr_array_index (lrg_node_get_components (node), 0)), ==,
	                 "test-health");
	g_assert_nonnull (efx_find (f, "inspector-component-test-health"));
	g_assert_nonnull (lrg_editor_inspector_panel_get_grid (inspector (f), LRG_EDITOR_NODE_SECTION_COMPONENT,
	                                                       "test-health"));

	/* Removing it is one click and one undo step */
	click_revealed (f, "inspector-remove-test-health");
	g_assert_cmpuint (lrg_node_get_components (node)->len, ==, 0);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_node_get_components (node)->len, ==, 1);

	/* A name nobody registered can still be typed in */
	g_assert_true (lrg_editor_inspector_panel_show_add_component (inspector (f)));
	efx_frames (f, 2);
	efx_type (f, "my-custom");
	efx_frames (f, 2);
	g_assert_nonnull (lrg_widget_find (lrg_canvas_get_top_popup (canvas), "inspector-picker-custom"));
	efx_key (f, GRL_KEY_ENTER, 0);
	g_assert_cmpuint (lrg_node_get_components (node)->len, ==, 2);
}

/* A script asset dropped on the Scripts section is attached */
static void
test_script_drop (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgNode                  *node = add_node (f, "Hero", 0, 0);
	LrgCanvas                *canvas = lrg_editor_app_get_canvas (f->app);
	LrgWidget                *section;
	g_autoptr(LrgDragPayload) payload = lrg_drag_payload_new_string ("application/x-lrg-asset", "scripts/ai.lua",
	                                                                         LRG_DRAG_ACTION_COPY);
	gfloat                    x;
	gfloat                    y;
	gfloat                    w;
	gfloat                    h;
	GPtrArray                *scripts;

	select_nodes (f, node, NULL, NULL);
	section = efx_find (f, "inspector-section-scripts");
	reveal (f, section);
	efx_rect_of (section, &x, &y, &w, &h);
	g_assert_cmpfloat (h, >, 0.0f);

	/* Click-to-drop: begin, hover the section, click */
	g_assert_true (lrg_canvas_begin_drag (canvas, NULL, payload, x + 4.0f, y + 4.0f));
	efx_move (f, x + w * 0.5f, y + 8.0f);
	efx_move (f, x + w * 0.5f, y + 10.0f);
	efx_click (f, x + w * 0.5f, y + 10.0f);
	efx_frames (f, 2);
	g_assert_false (lrg_canvas_is_dragging (canvas));

	scripts = lrg_node_get_scripts (node);
	g_assert_cmpuint (scripts->len, ==, 1);
	g_assert_cmpstr (lrg_script_binding_get_script (g_ptr_array_index (scripts, 0)), ==, "scripts/ai.lua");
	g_assert_cmpint (lrg_script_binding_get_language (g_ptr_array_index (scripts, 0)), ==, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_nonnull (efx_find (f, "inspector-script-remove-0"));

	/* The row's remove button detaches it again */
	click_revealed (f, "inspector-script-remove-0");
	g_assert_cmpuint (lrg_node_get_scripts (node)->len, ==, 0);
}

/* ==========================================================================
 * Prefabs
 * ========================================================================== */

static void
test_prefab_override (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *path = g_build_filename (f->dir, "crate.rprefab", NULL);
	LrgNode                 *src = add_node (f, "Crate", 4, 0);
	LrgNode                 *inst;
	LrgPropertyGrid         *grid;
	LrgPropertyRow          *row;
	g_autoptr(JsonNode)      r = NULL;
	g_autofree gchar        *req = NULL;

	g_assert_true (lrg_editor_save_prefab (f->editor, src, path, &error));
	g_assert_no_error (error);
	inst = lrg_editor_instantiate_prefab (f->editor, path, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (inst);
	select_nodes (f, inst, NULL, NULL);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-prefab-bar")));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-prefab-unlink")));

	/* Edit the scale over IPC: the row gets the override marker (the
	 * root's location and rotation belong to the instance, never
	 * overridden) */
	req = g_strdup_printf ("{'id':1,'method':'inspector.set','params':{'node':'%s','section':'transform',"
	                       "'name':'scale','value':[2,2,2]}}", lrg_node_get_guid (inst));
	r = efx_ipc_result (f, req);
	efx_frames (f, 2);
	grid = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_TRANSFORM, NULL);
	row = lrg_property_grid_get_row (grid, "scale");
	g_assert_true (lrg_property_row_get_state (row) & LRG_PROPERTY_STATE_OVERRIDDEN);
	g_assert_cmpfloat (lrg_node_get_scale (inst)->x, ==, 2.0f);
	g_assert_nonnull (lrg_widget_get_context_menu (LRG_WIDGET (row)));

	/* The row's "Revert to Prefab" */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (row), "insp.revert-override",
	                                           g_variant_new_string ("scale")));
	efx_frames (f, 2);
	g_assert_cmpfloat (lrg_node_get_scale (inst)->x, ==, 1.0f);
	g_assert_false (lrg_property_row_get_state (lrg_property_grid_get_row (grid, "scale")) &
	                LRG_PROPERTY_STATE_OVERRIDDEN);

	/* Revert over IPC: nothing left to revert is an error */
	g_free (req);
	req = g_strdup_printf ("{'id':2,'method':'inspector.revert_override','params':{'node':'%s',"
	                       "'section':'transform','name':'scale'}}", lrg_node_get_guid (inst));
	{
		g_autofree gchar *reply = efx_ipc (f, req);

		g_assert_nonnull (strstr (reply, "\"error\""));
	}

	/* Apply over IPC: the prefab takes the value, the marker goes */
	g_free (req);
	req = g_strdup_printf ("{'id':3,'method':'inspector.set','params':{'node':'%s','section':'transform',"
	                       "'name':'scale','value':[3,3,3]}}", lrg_node_get_guid (inst));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_true (lrg_property_row_get_state (lrg_property_grid_get_row (grid, "scale")) &
	               LRG_PROPERTY_STATE_OVERRIDDEN);
	g_free (req);
	req = g_strdup_printf ("{'id':4,'method':'inspector.apply_override','params':{'node':'%s',"
	                       "'section':'transform','name':'scale'}}", lrg_node_get_guid (inst));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_cmpint (json_object_get_int_member (result_object (r), "count"), ==, 1);
	efx_frames (f, 2);
	g_assert_cmpfloat (lrg_node_get_scale (inst)->x, ==, 3.0f);
	g_assert_false (lrg_property_row_get_state (lrg_property_grid_get_row (grid, "scale")) &
	                LRG_PROPERTY_STATE_OVERRIDDEN);
	{
		g_autofree gchar *text = NULL;

		g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
		g_assert_nonnull (strstr (text, "3"));
	}

	/* Unlink from the bar */
	efx_click_widget (f, efx_find (f, "inspector-prefab-unlink"));
	efx_frames (f, 2);
	g_assert_null (lrg_node_get_prefab_link (inst));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-prefab-unlink")));

	/* The 0.8.0 marker stays: the bar now offers Relink */
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-prefab-relink")));
	efx_click_widget (f, efx_find (f, "inspector-prefab-relink"));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_node_get_prefab_link (inst));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-prefab-unlink")));
}

/* ==========================================================================
 * Lock
 * ========================================================================== */

static void
test_lock (EditorAppFixture *f,
           gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgEditorViewState      *view = lrg_editor_app_get_view_state (f->app);
	LrgNode                 *a = add_node (f, "A", 0, 0);
	LrgNode                 *b = add_node (f, "B", 0, 0);

	select_nodes (f, a, NULL, NULL);
	efx_click_widget (f, efx_find (f, "inspector-lock"));
	g_assert_true (lrg_editor_inspector_panel_get_locked (insp));
	g_assert_cmpstr (lrg_editor_view_state_get_inspector_lock (view), ==, lrg_node_get_guid (a));

	/* Another selection does not move a locked inspector */
	select_nodes (f, b, NULL, NULL);
	g_assert_true (g_ptr_array_index (lrg_editor_inspector_panel_get_nodes (insp), 0) == a);

	/* The view state (IPC) can point the lock elsewhere */
	lrg_editor_view_state_set_inspector_lock (view, lrg_node_get_guid (b));
	efx_frame (f);
	g_assert_true (g_ptr_array_index (lrg_editor_inspector_panel_get_nodes (insp), 0) == b);

	/* Unlocking follows the selection again */
	efx_click_widget (f, efx_find (f, "inspector-lock"));
	g_assert_false (lrg_editor_inspector_panel_get_locked (insp));
	g_assert_null (lrg_editor_view_state_get_inspector_lock (view));
	select_nodes (f, a, NULL, NULL);
	g_assert_true (g_ptr_array_index (lrg_editor_inspector_panel_get_nodes (insp), 0) == a);

	/* Deleting the locked node unlocks */
	lrg_editor_inspector_panel_set_locked (insp, TRUE);
	lrg_editor_delete_node (f->editor, a);
	efx_frame (f);
	g_assert_false (lrg_editor_inspector_panel_get_locked (insp));
}

/* ==========================================================================
 * Live inspection during play
 * ========================================================================== */

static void
test_live (EditorAppFixture *f,
           gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgEditorPlaySession    *play = lrg_editor_app_get_play_session (f->app);
	LrgNode                 *node = add_node (f, "Mover", 0, 0);
	LrgNode                 *other_node = add_node (f, "Plain", 5, 0);
	LrgPropertyGrid         *grid;
	LrgPropertyRow          *row;
	LrgGameObject           *obj;
	LrgComponent            *comp;
	g_auto(GValue)           v = G_VALUE_INIT;
	g_auto(GValue)           shown = G_VALUE_INIT;

	/* LrgGameObject live inspection: play this level in 2D */
	lrg_level_set_default_2d (lrg_editor_get_level (f->editor), TRUE);
	add_health (node, TEST_KIND_MAGIC, 30.0);
	select_nodes (f, node, NULL, NULL);
	g_assert_true (lrg_editor_app_activate_action (f->app, "play.start"));
	efx_frames (f, 3);
	g_assert_true (lrg_editor_play_session_is_playing (play));

	/* The live object of the selected node, with the banner */
	obj = lrg_editor_play_session_get_object_for_node (play, node);
	g_assert_nonnull (obj);
	g_assert_true (lrg_editor_inspector_panel_get_live_object (insp) == G_OBJECT (obj));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-live-banner")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-section-transform")));
	grid = lrg_editor_inspector_panel_get_live_grid (insp, "test-health");
	g_assert_nonnull (grid);
	comp = lrg_game_object_get_component (obj, TEST_TYPE_HEALTH);
	g_assert_nonnull (comp);

	/* Values follow the world every frame */
	TEST_HEALTH (comp)->max_hp = 42;
	efx_frames (f, 2);
	row = lrg_property_grid_get_row (grid, "max-hp");
	g_assert_true (lrg_property_row_get_value (row, &shown));
	g_assert_cmpint (g_value_get_int (&shown), ==, 42);

	/* An edit goes straight into the play world */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 7);
	lrg_property_row_apply_value (row, &v);
	g_assert_cmpint (TEST_HEALTH (comp)->max_hp, ==, 7);
	g_assert_null (desc_value (node, 0, "max-hp"));

	/* "Apply to Level" copies it into the component description */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (row), "live.apply", g_variant_new_string ("max-hp")));
	g_assert_nonnull (desc_value (node, 0, "max-hp"));

	/* Picking another object on the hierarchy's Live tab shows that one */
	{
		LrgGameObject *other = lrg_editor_play_session_get_object_for_node (play, other_node);

		g_assert_nonnull (other);
		g_signal_emit_by_name (lrg_editor_hierarchy_panel_find (f->app), "live-selected", other);
		efx_frame (f);
		g_assert_true (lrg_editor_inspector_panel_get_live_object (insp) == G_OBJECT (other));
		g_assert_null (lrg_editor_inspector_panel_get_live_grid (insp, "test-health"));
		lrg_editor_inspector_panel_set_live_object (insp, NULL);
		efx_frame (f);
		g_assert_true (lrg_editor_inspector_panel_get_live_object (insp) == G_OBJECT (obj));
	}

	/* Stop: back to the authoring sections; the applied value stays */
	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_inspector_panel_get_live_object (insp));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-live-banner")));
	g_assert_nonnull (desc_value (node, 0, "max-hp"));
	{
		g_auto(GValue) n = G_VALUE_INIT;

		g_value_init (&n, G_TYPE_INT64);
		g_assert_true (g_value_transform (desc_value (node, 0, "max-hp"), &n));
		g_assert_cmpint (g_value_get_int64 (&n), ==, 7);
	}
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgNode            *a = add_node (f, "A", 1, 0);
	LrgNode            *b = add_node (f, "B", 2, 0);
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *req = NULL;
	JsonArray          *rows;
	JsonObject         *row = NULL;
	guint               undo;
	guint               i;

	add_health (a, TEST_KIND_MELEE, 1.0);
	add_health (b, TEST_KIND_RANGED, 1.0);

	/* list one section */
	req = g_strdup_printf ("{'id':1,'method':'inspector.list','params':{'nodes':['%s','%s'],"
	                       "'section':'component','component':'test-health'}}",
	                       lrg_node_get_guid (a), lrg_node_get_guid (b));
	r = efx_ipc_result (f, req);
	rows = json_object_get_array_member (result_object (r), "rows");
	for (i = 0; i < json_array_get_length (rows); i++)
		if (g_strcmp0 (json_object_get_string_member (json_array_get_object_element (rows, i), "name"), "kind") == 0)
			row = json_array_get_object_element (rows, i);
	g_assert_nonnull (row);
	g_assert_cmpstr (json_object_get_string_member (row, "type"), ==, "TestKind");
	g_assert_cmpstr (json_object_get_string_member (row, "value"), ==, "melee");
	g_assert_true (json_object_get_boolean_member (row, "mixed"));
	g_assert_true (json_object_has_member (row, "state"));
	g_assert_true (json_object_has_member (row, "label"));
	g_assert_true (json_object_has_member (row, "group"));
	g_assert_true (json_object_has_member (row, "hint"));

	/* list everything */
	g_clear_pointer (&r, json_node_unref);
	g_free (req);
	req = g_strdup_printf ("{'id':2,'method':'inspector.list','params':{'node':'%s'}}", lrg_node_get_guid (a));
	r = efx_ipc_result (f, req);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (r), "sections")), ==, 5);

	/* set on two nodes: one undo step */
	undo = lrg_editor_get_undo_count (f->editor);
	g_clear_pointer (&r, json_node_unref);
	g_free (req);
	req = g_strdup_printf ("{'id':3,'method':'inspector.set','params':{'nodes':['%s','%s'],"
	                       "'section':'component','component':'test-health','name':'kind','value':'magic'}}",
	                       lrg_node_get_guid (a), lrg_node_get_guid (b));
	r = efx_ipc_result (f, req);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_cmpint (g_value_get_enum (desc_value (a, 0, "kind")), ==, TEST_KIND_MAGIC);
	g_assert_cmpint (g_value_get_enum (desc_value (b, 0, "kind")), ==, TEST_KIND_MAGIC);
	g_assert_false (json_object_get_boolean_member (result_object (r), "mixed"));

	/* get */
	g_clear_pointer (&r, json_node_unref);
	g_free (req);
	req = g_strdup_printf ("{'id':4,'method':'inspector.get','params':{'node':'%s','section':'transform',"
	                       "'name':'location'}}", lrg_node_get_guid (b));
	r = efx_ipc_result (f, req);
	g_assert_cmpfloat (json_array_get_double_element (json_object_get_array_member (result_object (r), "value"), 0),
	                   ==, 2.0);

	/* reset: the component key goes, the type's default applies */
	g_clear_pointer (&r, json_node_unref);
	g_free (req);
	req = g_strdup_printf ("{'id':5,'method':'inspector.reset','params':{'node':'%s','section':'component',"
	                       "'component':'test-health','name':'kind'}}", lrg_node_get_guid (a));
	r = efx_ipc_result (f, req);
	g_assert_null (desc_value (a, 0, "kind"));
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "value"), ==, "melee");

	/* the selection is the default target; state reports it */
	select_nodes (f, b, NULL, NULL);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':6,'method':'inspector.state'}");
	g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (result_object (r), "nodes"), 0),
	                 ==, lrg_node_get_guid (b));
	g_assert_false (json_object_get_boolean_member (result_object (r), "locked"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'inspector.get','params':{'section':'header','name':'name'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "value"), ==, "B");

	/* errors */
	{
		g_autofree gchar *e1 = efx_ipc (f, "{'id':8,'method':'inspector.get','params':{'section':'nope','name':'x'}}");
		g_autofree gchar *e2 = efx_ipc (f, "{'id':9,'method':'inspector.set','params':{'section':'header',"
		                                   "'name':'guid','value':'x'}}");

		g_assert_nonnull (strstr (e1, "\"error\""));
		g_assert_nonnull (strstr (e2, "read-only"));
	}
}

/* ==========================================================================
 * Enum and flags values survive save, load and instantiate
 * ========================================================================== */

static void
test_enum_roundtrip (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(LrgWorld)           world = lrg_world_new ();
	g_autofree gchar             *yaml = NULL;
	LrgNode                      *node = add_node (f, "Orc", 0, 0);
	LrgGameObject                *obj;
	TestHealth                   *health;
	g_auto(GValue)                flags = G_VALUE_INIT;
	g_autofree gchar             *guid = NULL;

	add_health (node, TEST_KIND_RANGED, 2.0);
	g_value_init (&flags, test_resist_get_type ());
	g_value_set_flags (&flags, TEST_RESIST_FIRE | TEST_RESIST_ACID);
	lrg_component_desc_set_value (g_ptr_array_index (lrg_node_get_components (node), 0), "resist", &flags);

	/* Saved as nicks */
	yaml = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	g_assert_nonnull (yaml);
	g_assert_nonnull (strstr (yaml, "kind: ranged"));
	g_assert_nonnull (strstr (yaml, "fire|acid"));
	g_assert_null (strstr (yaml, "TEST_KIND_RANGED"));

	/* Loaded back and instantiated with the real values */
	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_level_instantiate (loaded, world, NULL, &error));
	g_assert_no_error (error);
	obj = lrg_world_find_by_tag (world, lrg_node_get_guid (node));
	g_assert_nonnull (obj);
	health = TEST_HEALTH (lrg_game_object_get_component (obj, TEST_TYPE_HEALTH));
	g_assert_nonnull (health);
	g_assert_cmpint (health->kind, ==, TEST_KIND_RANGED);
	g_assert_cmpuint (health->resist, ==, TEST_RESIST_FIRE | TEST_RESIST_ACID);
	g_assert_cmpfloat (health->speed, ==, 2.0f);

	/* The inspector reads the loaded string as the enum (the old level,
	 * and @node with it, is gone after set_level) */
	guid = g_strdup (lrg_node_get_guid (node));
	lrg_editor_set_level (f->editor, loaded);
	efx_frame (f);
	select_nodes (f, lrg_editor_find_node (f->editor, guid), NULL, NULL);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (
		editor_of (lrg_editor_inspector_panel_get_grid (inspector (f), LRG_EDITOR_NODE_SECTION_COMPONENT,
		                                                "test-health"), "kind"))), ==, "ranged");
}

/* ==========================================================================
 * Toolkit: LrgSpinBox scrub gestures, LrgCheckbox inconsistent
 * ========================================================================== */

typedef struct
{
	guint    begins;
	guint    ends;
	guint    commits;
	guint    changes;
	gboolean value_at_end_restored;
	gdouble  start;
} SpinCounts;

static void on_scrub_begin (LrgSpinBox *s, SpinCounts *c) { c->begins++; c->start = lrg_spin_box_get_value (s); }
static void on_value_changed (LrgSpinBox *s, SpinCounts *c) { c->changes++; }

static void
on_scrub_end (LrgSpinBox *s,
              gboolean    commit,
              SpinCounts *c)
{
	c->ends++;
	if (commit)
		c->commits++;
	c->value_at_end_restored = lrg_spin_box_get_value (s) == c->start;
}

static void
canvas_frame (LrgCanvas *canvas,
              gfloat     x,
              gfloat     y,
              gboolean   down,
              gboolean   was_down,
              GrlKey     key)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.delta_time = EFX_DT;
	if (down)
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	if (down && !was_down)
		input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	if (!down && was_down)
		input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	if (key != GRL_KEY_NULL)
		lrg_canvas_input_add_key (&input, key);
	lrg_canvas_process_input (canvas, &input);
}

static void
test_spin_box_scrub (void)
{
	g_autoptr(LrgCanvas) canvas = g_object_ref_sink (lrg_canvas_new ());
	LrgSpinBox          *spin = lrg_spin_box_new_with_range (0.0, 1000.0, 1.0);
	SpinCounts           c = { 0 };
	gfloat               x;
	gfloat               y;

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 200.0f);
	lrg_widget_set_position (LRG_WIDGET (spin), 20.0f, 20.0f);
	lrg_widget_set_size (LRG_WIDGET (spin), 160.0f, 28.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (spin));
	g_object_unref (spin);
	lrg_spin_box_set_value (spin, 100.0);
	g_signal_connect (spin, "scrub-begin", G_CALLBACK (on_scrub_begin), &c);
	g_signal_connect (spin, "scrub-end", G_CALLBACK (on_scrub_end), &c);
	g_signal_connect (spin, "value-changed", G_CALLBACK (on_value_changed), &c);
	lrg_canvas_record (canvas);
	x = lrg_widget_get_world_x (LRG_WIDGET (spin)) + 80.0f;
	y = lrg_widget_get_world_y (LRG_WIDGET (spin)) + 14.0f;

	/* A scrub: one begin, one committed end */
	canvas_frame (canvas, x, y, FALSE, FALSE, GRL_KEY_NULL);
	canvas_frame (canvas, x, y, TRUE, FALSE, GRL_KEY_NULL);
	g_assert_true (lrg_spin_box_get_scrubbing (spin));
	canvas_frame (canvas, x + 30.0f, y, TRUE, TRUE, GRL_KEY_NULL);
	canvas_frame (canvas, x + 30.0f, y, FALSE, TRUE, GRL_KEY_NULL);
	g_assert_cmpuint (c.begins, ==, 1);
	g_assert_cmpuint (c.ends, ==, 1);
	g_assert_cmpuint (c.commits, ==, 1);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 105.0);
	g_assert_false (lrg_spin_box_get_scrubbing (spin));

	/* Escape mid-scrub: back to 105, ended without commit; the rest of
	 * the drag does nothing */
	canvas_frame (canvas, x, y, TRUE, FALSE, GRL_KEY_NULL);
	canvas_frame (canvas, x + 60.0f, y, TRUE, TRUE, GRL_KEY_NULL);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 115.0);
	canvas_frame (canvas, x + 60.0f, y, TRUE, TRUE, GRL_KEY_ESCAPE);
	g_assert_cmpuint (c.ends, ==, 2);
	g_assert_cmpuint (c.commits, ==, 1);
	g_assert_true (c.value_at_end_restored);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 105.0);
	canvas_frame (canvas, x + 120.0f, y, TRUE, TRUE, GRL_KEY_NULL);
	canvas_frame (canvas, x + 120.0f, y, FALSE, TRUE, GRL_KEY_NULL);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 105.0);
	g_assert_cmpuint (c.ends, ==, 2);

	/* A step button press is a gesture too; cancel_scrub ends it */
	canvas_frame (canvas, lrg_widget_get_world_x (LRG_WIDGET (spin)) + 150.0f, y, TRUE, FALSE, GRL_KEY_NULL);
	g_assert_cmpuint (c.begins, ==, 3);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 106.0);
	lrg_spin_box_cancel_scrub (spin);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 105.0);
	g_assert_cmpuint (c.ends, ==, 3);
	canvas_frame (canvas, lrg_widget_get_world_x (LRG_WIDGET (spin)) + 150.0f, y, FALSE, TRUE, GRL_KEY_NULL);
	g_assert_cmpuint (c.ends, ==, 3);

	/* Wheel and keys are no gestures */
	lrg_spin_box_spin (spin, 1);
	g_assert_cmpuint (c.begins, ==, 3);
}

static void
on_toggled (LrgCheckbox *box,
            gboolean     checked,
            guint       *count)
{
	(*count)++;
}

static guint
recorded_instances (LrgCanvas *canvas)
{
	return lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
}

static void
test_checkbox_inconsistent (void)
{
	g_autoptr(LrgCanvas) canvas = g_object_ref_sink (lrg_canvas_new ());
	LrgCheckbox         *box = lrg_checkbox_new ("Visible");
	guint                toggled = 0;
	guint                plain;
	guint                dashed;

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 200.0f);
	lrg_widget_set_position (LRG_WIDGET (box), 10.0f, 10.0f);
	lrg_widget_set_size (LRG_WIDGET (box), 120.0f, 24.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	g_object_unref (box);
	g_signal_connect (box, "toggled", G_CALLBACK (on_toggled), &toggled);
	lrg_canvas_update (canvas, EFX_DT);
	plain = recorded_instances (canvas);

	/* Inconsistent: the property, and a dash drawn in the box */
	g_assert_false (lrg_checkbox_get_inconsistent (box));
	lrg_checkbox_set_inconsistent (box, TRUE);
	g_object_get (box, "inconsistent", &toggled, NULL);
	g_assert_true (toggled);
	toggled = 0;
	while (lrg_canvas_update (canvas, EFX_DT))
		;
	dashed = recorded_instances (canvas);
	g_assert_cmpuint (dashed, >, plain);

	/* A user toggle settles on checked and clears the state */
	lrg_checkbox_toggle (box);
	g_assert_false (lrg_checkbox_get_inconsistent (box));
	g_assert_true (lrg_checkbox_get_checked (box));
	g_assert_cmpuint (toggled, ==, 1);

	/* Checked underneath and inconsistent: toggled is still emitted */
	lrg_checkbox_set_inconsistent (box, TRUE);
	lrg_checkbox_toggle (box);
	g_assert_true (lrg_checkbox_get_checked (box));
	g_assert_cmpuint (toggled, ==, 2);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

/* 0.9.0 (ED2): light rows by kind, decal and probe param hints, the
 * LOD section and lod.* without a GPU. */
static LrgNode *
add_kind (EditorAppFixture  *f,
          const gchar       *name,
          LrgNodeVisualKind  kind)
{
	LrgNode                 *node = add_node (f, name, 0.0f, 0.0f);
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (kind);

	lrg_node_set_visual (node, visual);
	return node;
}

static void
test_render_kinds (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorInspectorPanel *insp = inspector (f);
	LrgPropertyGrid         *grid;
	LrgNode                 *light, *decal, *probe, *mesh;
	g_auto(GValue)           v = G_VALUE_INIT;
	g_autofree gchar        *reply = NULL;
	g_autofree gchar        *req = NULL;

	lrg_editor_new_level (f->editor, "kinds");
	light = add_kind (f, "lamp", LRG_NODE_VISUAL_LIGHT);
	decal = add_kind (f, "stain", LRG_NODE_VISUAL_DECAL);
	probe = add_kind (f, "mirror", LRG_NODE_VISUAL_REFLECTION_PROBE);
	mesh = add_kind (f, "rock", LRG_NODE_VISUAL_MESH_ASSET);
	efx_frames (f, 2);
	grid = lrg_editor_inspector_panel_get_grid (insp, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);

	/* A point light: range, the light type picker, no inner cone */
	select_nodes (f, light, NULL, NULL);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-range"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-light-type"));
	g_assert_null (lrg_property_grid_get_row (grid, "param-spot-inner-angle"));

	/* A spot shows its inner cone */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "spot");
	lrg_node_visual_set_param (lrg_node_get_visual (light), "light_type", &v);
	select_nodes (f, decal, NULL, NULL);
	select_nodes (f, light, NULL, NULL);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-spot-inner-angle"));

	/* A directional light has no range or cone */
	g_value_set_string (&v, "directional");
	lrg_node_visual_set_param (lrg_node_get_visual (light), "light_type", &v);
	select_nodes (f, decal, NULL, NULL);
	select_nodes (f, light, NULL, NULL);
	g_assert_null (lrg_property_grid_get_row (grid, "param-range"));
	g_assert_null (lrg_property_grid_get_row (grid, "param-spot-angle"));

	/* A decal: box size, fade, an albedo asset row; no roughness material row */
	select_nodes (f, decal, NULL, NULL);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-size-x"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-fade-angle"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "asset"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "color"));
	g_assert_null (lrg_property_grid_get_row (grid, "roughness"));

	/* A probe: extents (default 5), blend band, resolution */
	select_nodes (f, probe, NULL, NULL);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-extents-x"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-blend-distance"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "param-resolution"));
	req = g_strdup_printf ("{'id':1,'method':'inspector.get','params':{'node':'%s','section':'visual',"
	                       "'name':'param-extents-y'}}", lrg_node_get_guid (probe));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, req);
		JsonObject         *o = json_node_get_object (r);

		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "value"), 5.0, 1e-6);
	}

	/* The LOD section stays hidden without levels (no GPU, no meta) */
	select_nodes (f, mesh, NULL, NULL);
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-section-lod")));
	g_clear_pointer (&req, g_free);
	req = g_strdup_printf ("{'id':2,'method':'lod.get','params':{'node':'%s'}}", lrg_node_get_guid (mesh));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, req);
		JsonObject         *o = json_node_get_object (r);

		g_assert_false (json_object_get_boolean_member (o, "available"));
		g_assert_cmpint (json_object_get_int_member (o, "forced"), ==, -1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "levels")), ==, 0);
	}
	g_clear_pointer (&req, g_free);
	req = g_strdup_printf ("{'id':3,'method':'lod.force','params':{'node':'%s','level':1}}", lrg_node_get_guid (mesh));
	reply = efx_ipc (f, req);
	g_assert_nonnull (strstr (reply, "no LOD levels"));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':4,'method':'lod.get','params':{'node':'nope'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
}

int
main (int argc, char *argv[])
{
	g_autoptr(GError) error = NULL;
	LrgEngine        *engine;

	g_test_init (&argc, &argv, NULL);

	/* The registry the inspector, instantiation and play mode use */
	engine = lrg_engine_get_default ();
	lrg_engine_startup (engine, &error);
	g_assert_nonnull (lrg_engine_get_registry (engine));
	lrg_registry_register (lrg_engine_get_registry (engine), "test-health", TEST_TYPE_HEALTH);

#define ADD(path, fn) g_test_add ("/editor-app/inspector/" path, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("level", test_level_section);
	ADD ("rows-reused", test_rows_reused);
	ADD ("multi-scrub", test_multi_scrub);
	ADD ("scrub-escape", test_scrub_escape);
	ADD ("mixed", test_mixed);
	ADD ("enum-combo", test_enum_combo);
	ADD ("add-component", test_add_component);
	ADD ("script-drop", test_script_drop);
	ADD ("prefab-override", test_prefab_override);
	ADD ("lock", test_lock);
	ADD ("live", test_live);
	ADD ("ipc", test_ipc);
	ADD ("enum-roundtrip", test_enum_roundtrip);
	ADD ("render-kinds", test_render_kinds);
#undef ADD
	g_test_add_func ("/editor-app/inspector/spin-box-scrub", test_spin_box_scrub);
	g_test_add_func ("/editor-app/inspector/checkbox-inconsistent", test_checkbox_inconsistent);
	return g_test_run ();
}
