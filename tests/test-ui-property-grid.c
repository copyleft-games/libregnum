/* test-ui-property-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPropertySource, LrgObjectPropertySource, LrgPropertyGrid and its
 * editors (LrgVectorEdit, LrgCurveEdit, LrgGradientEdit). Headless:
 * input goes through lrg_canvas_process_input() frames exactly as the
 * real loop feeds it, time through lrg_canvas_update() at 60 Hz, and
 * positions come from widget rectangles, never hard-coded pixels.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * TestEnemy: a GObject with one property of every kind the grid edits
 * ========================================================================== */

#define TEST_TYPE_ENEMY (test_enemy_get_type ())
G_DECLARE_FINAL_TYPE (TestEnemy, test_enemy, TEST, ENEMY, GObject)

struct _TestEnemy
{
	GObject           parent_instance;
	gchar            *name;
	gchar            *notes;
	gchar            *kind;
	gint              health;
	gdouble           speed;
	gboolean          aggressive;
	LrgEasingType     easing;
	guint             flags;
	GrlColor          tint;
	GrlVector3        offset;
	gdouble           heading;
	LrgKeyframeCurve *falloff;
	LrgGradientStops *glow;
	gchar            *target;
	gchar            *sprite;
	gchar           **tags;
	gint              id;
	gint              level;
};

G_DEFINE_FINAL_TYPE (TestEnemy, test_enemy, G_TYPE_OBJECT)

enum
{
	E_PROP_0,
	E_PROP_NAME,
	E_PROP_NOTES,
	E_PROP_KIND,
	E_PROP_HEALTH,
	E_PROP_SPEED,
	E_PROP_AGGRESSIVE,
	E_PROP_EASING,
	E_PROP_FLAGS,
	E_PROP_TINT,
	E_PROP_OFFSET,
	E_PROP_HEADING,
	E_PROP_FALLOFF,
	E_PROP_GLOW,
	E_PROP_TARGET,
	E_PROP_SPRITE,
	E_PROP_TAGS,
	E_PROP_ID,
	E_PROP_LEVEL,
	E_N_PROPS
};

static GParamSpec *enemy_props[E_N_PROPS];

static void
test_enemy_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	TestEnemy *self = TEST_ENEMY (object);

	switch (prop_id)
	{
	case E_PROP_NAME: g_value_set_string (value, self->name); break;
	case E_PROP_NOTES: g_value_set_string (value, self->notes); break;
	case E_PROP_KIND: g_value_set_string (value, self->kind); break;
	case E_PROP_HEALTH: g_value_set_int (value, self->health); break;
	case E_PROP_SPEED: g_value_set_double (value, self->speed); break;
	case E_PROP_AGGRESSIVE: g_value_set_boolean (value, self->aggressive); break;
	case E_PROP_EASING: g_value_set_enum (value, self->easing); break;
	case E_PROP_FLAGS: g_value_set_flags (value, self->flags); break;
	case E_PROP_TINT: g_value_set_boxed (value, &self->tint); break;
	case E_PROP_OFFSET: g_value_set_boxed (value, &self->offset); break;
	case E_PROP_HEADING: g_value_set_double (value, self->heading); break;
	case E_PROP_FALLOFF: g_value_set_object (value, self->falloff); break;
	case E_PROP_GLOW: g_value_set_boxed (value, self->glow); break;
	case E_PROP_TARGET: g_value_set_string (value, self->target); break;
	case E_PROP_SPRITE: g_value_set_string (value, self->sprite); break;
	case E_PROP_TAGS: g_value_set_boxed (value, self->tags); break;
	case E_PROP_ID: g_value_set_int (value, self->id); break;
	case E_PROP_LEVEL: g_value_set_int (value, self->level); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_enemy_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	TestEnemy *self = TEST_ENEMY (object);

	switch (prop_id)
	{
	case E_PROP_NAME: g_free (self->name); self->name = g_value_dup_string (value); break;
	case E_PROP_NOTES: g_free (self->notes); self->notes = g_value_dup_string (value); break;
	case E_PROP_KIND: g_free (self->kind); self->kind = g_value_dup_string (value); break;
	case E_PROP_HEALTH: self->health = g_value_get_int (value); break;
	case E_PROP_SPEED: self->speed = g_value_get_double (value); break;
	case E_PROP_AGGRESSIVE: self->aggressive = g_value_get_boolean (value); break;
	case E_PROP_EASING: self->easing = g_value_get_enum (value); break;
	case E_PROP_FLAGS: self->flags = g_value_get_flags (value); break;
	case E_PROP_TINT:
		if (g_value_get_boxed (value) != NULL)
			self->tint = *(const GrlColor *) g_value_get_boxed (value);
		break;
	case E_PROP_OFFSET:
		if (g_value_get_boxed (value) != NULL)
			self->offset = *(const GrlVector3 *) g_value_get_boxed (value);
		break;
	case E_PROP_HEADING: self->heading = g_value_get_double (value); break;
	case E_PROP_FALLOFF: g_set_object (&self->falloff, g_value_get_object (value)); break;
	case E_PROP_GLOW:
		lrg_gradient_stops_free (self->glow);
		self->glow = g_value_dup_boxed (value);
		break;
	case E_PROP_TARGET: g_free (self->target); self->target = g_value_dup_string (value); break;
	case E_PROP_SPRITE: g_free (self->sprite); self->sprite = g_value_dup_string (value); break;
	case E_PROP_TAGS: g_strfreev (self->tags); self->tags = g_value_dup_boxed (value); break;
	case E_PROP_ID: self->id = g_value_get_int (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); return;
	}
	g_object_notify_by_pspec (object, pspec);
}

static void
test_enemy_finalize (GObject *object)
{
	TestEnemy *self = TEST_ENEMY (object);

	g_free (self->name);
	g_free (self->notes);
	g_free (self->kind);
	g_clear_object (&self->falloff);
	lrg_gradient_stops_free (self->glow);
	g_free (self->target);
	g_free (self->sprite);
	g_strfreev (self->tags);
	G_OBJECT_CLASS (test_enemy_parent_class)->finalize (object);
}

static void
test_enemy_class_init (TestEnemyClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY;

	oc->get_property = test_enemy_get_property;
	oc->set_property = test_enemy_set_property;
	oc->finalize = test_enemy_finalize;

	enemy_props[E_PROP_NAME] = g_param_spec_string ("name", NULL, "[group:Identity] The display name.", "Grunt", rw);
	enemy_props[E_PROP_NOTES] = g_param_spec_string ("notes", NULL, "[hint:multiline][group:Identity] Notes.", "", rw);
	enemy_props[E_PROP_KIND] = g_param_spec_string ("kind", NULL, "[hint:enum-strings:melee,ranged,boss][group:Identity]",
	                                                "melee", rw);
	enemy_props[E_PROP_HEALTH] = g_param_spec_int ("health", "Health", "[group:Combat] Hit points.", 0, 1000, 100, rw);
	enemy_props[E_PROP_SPEED] = g_param_spec_double ("speed", NULL, "[group:Combat]", 0.0, 50.0, 3.5, rw);
	enemy_props[E_PROP_AGGRESSIVE] = g_param_spec_boolean ("aggressive", NULL, "[group:Combat]", FALSE, rw);
	enemy_props[E_PROP_EASING] = g_param_spec_enum ("easing", NULL, "[group:Combat]", LRG_TYPE_EASING_TYPE,
	                                                LRG_EASING_LINEAR, rw);
	enemy_props[E_PROP_FLAGS] = g_param_spec_flags ("flags", NULL, "[group:Debug]", LRG_TYPE_PROPERTY_STATE, 0, rw);
	enemy_props[E_PROP_TINT] = g_param_spec_boxed ("tint", NULL, "[group:Looks]", GRL_TYPE_COLOR, rw);
	enemy_props[E_PROP_OFFSET] = g_param_spec_boxed ("offset", NULL, "[group:Transform]", GRL_TYPE_VECTOR3, rw);
	enemy_props[E_PROP_HEADING] = g_param_spec_double ("heading", NULL, NULL, -G_PI, G_PI, 0.0, rw);
	enemy_props[E_PROP_FALLOFF] = g_param_spec_object ("falloff", NULL, "[group:Looks]", LRG_TYPE_KEYFRAME_CURVE, rw);
	enemy_props[E_PROP_GLOW] = g_param_spec_boxed ("glow", NULL, "[group:Looks]", LRG_TYPE_GRADIENT_STOPS, rw);
	enemy_props[E_PROP_TARGET] = g_param_spec_string ("target", NULL, "[hint:node-ref][group:AI]", "", rw);
	enemy_props[E_PROP_SPRITE] = g_param_spec_string ("sprite", NULL, "[hint:asset:texture][group:Looks]", "", rw);
	enemy_props[E_PROP_TAGS] = g_param_spec_boxed ("tags", NULL, "[hint:array][group:AI]", G_TYPE_STRV, rw);
	enemy_props[E_PROP_ID] = g_param_spec_int ("id", NULL, NULL, 0, G_MAXINT, 0,
	                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	enemy_props[E_PROP_LEVEL] = g_param_spec_int ("level", NULL, "[group:Combat]", 0, 99, 1, G_PARAM_READABLE);

	/* Hints can come from qdata too */
	g_param_spec_set_qdata (enemy_props[E_PROP_HEADING], lrg_property_hint_quark (), (gpointer) "angle");
	g_param_spec_set_qdata (enemy_props[E_PROP_HEADING], lrg_property_group_quark (), (gpointer) "Transform");

	g_object_class_install_properties (oc, E_N_PROPS, enemy_props);
}

static void
test_enemy_init (TestEnemy *self)
{
	GrlColor red = { 255, 0, 0, 255 };
	GrlColor blue = { 0, 0, 255, 255 };

	self->name = g_strdup ("Grunt");
	self->notes = g_strdup ("");
	self->kind = g_strdup ("melee");
	self->health = 100;
	self->speed = 3.5;
	self->tint = red;
	self->falloff = lrg_keyframe_curve_new ();
	lrg_keyframe_curve_add_key (self->falloff, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (self->falloff, 1.0f, 1.0f, LRG_EASING_LINEAR);
	self->glow = lrg_gradient_stops_new_two (&red, &blue);
	self->target = g_strdup ("");
	self->sprite = g_strdup ("");
	self->tags = g_new0 (gchar *, 1);
	self->level = 1;
}

static TestEnemy *
enemy_new (void)
{
	return g_object_new (TEST_TYPE_ENEMY, "id", 7, NULL);
}

/* ==========================================================================
 * TestSource: a hand-written LrgPropertySource with states
 * ========================================================================== */

#define TEST_TYPE_SOURCE (test_source_get_type ())
G_DECLARE_FINAL_TYPE (TestSource, test_source, TEST, SOURCE, GObject)

struct _TestSource
{
	GObject           parent_instance;
	GParamSpec       *hp;
	GParamSpec       *x;
	gint              hp_value;
	gdouble           x_value;
	LrgPropertyState  hp_state;
};

static void test_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (TestSource, test_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, test_source_iface_init))

static GPtrArray *
ts_list (LrgPropertySource *source)
{
	TestSource *self = TEST_SOURCE (source);
	GPtrArray  *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);

	g_ptr_array_add (out, lrg_property_info_new (self->hp));
	g_ptr_array_add (out, lrg_property_info_new (self->x));
	return out;
}

static gboolean
ts_get (LrgPropertySource *source,
        const gchar       *name,
        GValue            *value)
{
	TestSource *self = TEST_SOURCE (source);

	if (g_strcmp0 (name, "hp") == 0)
	{
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, self->hp_value);
		return TRUE;
	}
	if (g_strcmp0 (name, "x") == 0)
	{
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, self->x_value);
		return TRUE;
	}
	return FALSE;
}

static gboolean
ts_set (LrgPropertySource *source,
        const gchar       *name,
        const GValue      *value)
{
	TestSource *self = TEST_SOURCE (source);

	if (g_strcmp0 (name, "hp") == 0)
		self->hp_value = g_value_get_int (value);
	else if (g_strcmp0 (name, "x") == 0)
		self->x_value = g_value_get_double (value);
	else
		return FALSE;
	lrg_property_source_emit_changed (source, name);
	return TRUE;
}

static LrgPropertyState
ts_state (LrgPropertySource *source,
          const gchar       *name)
{
	TestSource *self = TEST_SOURCE (source);

	if (g_strcmp0 (name, "hp") == 0)
		return self->hp_state;
	return LRG_PROPERTY_STATE_ANIMATED;
}

static void
test_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = ts_list;
	iface->get_value = ts_get;
	iface->set_value = ts_set;
	iface->get_state = ts_state;
}

static void
test_source_finalize (GObject *object)
{
	TestSource *self = TEST_SOURCE (object);

	g_param_spec_unref (self->hp);
	g_param_spec_unref (self->x);
	G_OBJECT_CLASS (test_source_parent_class)->finalize (object);
}

static void
test_source_class_init (TestSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = test_source_finalize;
}

static void
test_source_init (TestSource *self)
{
	self->hp = g_param_spec_ref_sink (g_param_spec_int ("hp", "HP", NULL, 0, 100, 50, G_PARAM_READWRITE));
	self->x = g_param_spec_ref_sink (g_param_spec_double ("x", NULL, NULL, -10.0, 10.0, 0.0, G_PARAM_READWRITE));
	self->hp_value = 50;
}

/* ==========================================================================
 * Canvas helpers
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
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* key_held: a key press while the pointer stays at (@x, @y), the
 * left button held when @held */
static void
key_held (LrgCanvas *canvas,
          GrlKey     k,
          gfloat     x,
          gfloat     y,
          gboolean   held)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	if (held)
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

/* type_text: characters in one frame, the pointer parked at (@x, @y) */
static void
type_text (LrgCanvas   *canvas,
           const gchar *text,
           gfloat       x,
           gfloat       y)
{
	LrgCanvasInput input;
	const gchar   *p;

	/* One frame per character, as a keyboard would deliver them */
	for (p = text; *p != '\0'; p = g_utf8_next_char (p))
	{
		gchar buf[8] = { 0 };

		memcpy (buf, p, (gsize) (g_utf8_next_char (p) - p));
		frame_pointer (&input, x, y);
		lrg_canvas_input_add_text (&input, buf);
		lrg_canvas_process_input (canvas, &input);
	}
}

/* settle: frames until nothing animates */
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

/* widget_center: world centre of @w */
static void
widget_center (LrgWidget *w,
               gfloat    *x,
               gfloat    *y)
{
	*x = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) * 0.5f;
	*y = lrg_widget_get_world_y (w) + lrg_widget_get_height (w) * 0.5f;
}

/* editor_of: the editor widget of property @name */
static LrgWidget *
editor_of (LrgPropertyGrid *grid,
           const gchar     *name)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (grid, name);

	g_assert_nonnull (row);
	return lrg_property_row_get_editor (row);
}

/* ==========================================================================
 * Fixture: a canvas with a grid editing one enemy
 * ========================================================================== */

typedef struct
{
	LrgCanvas       *canvas;
	LrgPropertyGrid *grid;
	TestEnemy       *a;
	TestEnemy       *b;
	guint            begins;
	guint            values;
	guint            ends;
	guint            commits;
	guint            cancels;
	guint            first_id;
	guint            last_id;
	gboolean         ids_consistent;
	gchar           *last_name;
} Fixture;

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            id,
               gpointer         user_data)
{
	Fixture *f = user_data;

	f->begins++;
	if (f->first_id == 0)
		f->first_id = id;
	f->last_id = id;
	g_free (f->last_name);
	f->last_name = g_strdup (name);
}

static void
on_edit_value (LrgPropertyGrid *grid,
               const gchar     *name,
               const GValue    *value,
               guint            id,
               gboolean         interactive,
               gpointer         user_data)
{
	Fixture *f = user_data;

	f->values++;
	if (id != f->last_id)
		f->ids_consistent = FALSE;
	g_assert_true (G_IS_VALUE (value));
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            id,
             gboolean         commit,
             gpointer         user_data)
{
	Fixture *f = user_data;

	f->ends++;
	if (commit)
		f->commits++;
	else
		f->cancels++;
	if (id != f->last_id)
		f->ids_consistent = FALSE;
}

static void
reset_counts (Fixture *f)
{
	f->begins = f->values = f->ends = f->commits = f->cancels = 0;
	f->first_id = f->last_id = 0;
	f->ids_consistent = TRUE;
}

static void
fixture_set_up (Fixture       *f,
                gconstpointer  data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 900.0f, 2400.0f);
	f->grid = lrg_property_grid_new ();
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->grid));
	lrg_widget_allocate (LRG_WIDGET (f->grid), 10.0f, 10.0f, 640.0f, 2380.0f);
	f->a = enemy_new ();
	f->b = enemy_new ();
	lrg_property_grid_set_object (f->grid, G_OBJECT (f->a));
	g_signal_connect (f->grid, "edit-begin", G_CALLBACK (on_edit_begin), f);
	g_signal_connect (f->grid, "edit-value", G_CALLBACK (on_edit_value), f);
	g_signal_connect (f->grid, "edit-end", G_CALLBACK (on_edit_end), f);
	reset_counts (f);
	settle (f->canvas);
}

static void
fixture_tear_down (Fixture       *f,
                   gconstpointer  data)
{
	g_object_unref (f->grid);
	g_clear_object (&f->canvas);
	g_clear_object (&f->a);
	g_clear_object (&f->b);
	g_free (f->last_name);
}

/* set_both: the grid edits a and b together */
static void
set_both (Fixture *f)
{
	g_autoptr(GPtrArray)               sources = g_ptr_array_new_with_free_func (g_object_unref);

	g_ptr_array_add (sources, lrg_object_property_source_new (G_OBJECT (f->a)));
	g_ptr_array_add (sources, lrg_object_property_source_new (G_OBJECT (f->b)));
	lrg_property_grid_set_sources (f->grid, sources);
	settle (f->canvas);
}

/* ==========================================================================
 * Sources, hints and values
 * ========================================================================== */

static void
test_object_source_listing (void)
{
	g_autoptr(TestEnemy)               enemy = enemy_new ();
	g_autoptr(LrgObjectPropertySource) source = lrg_object_property_source_new (G_OBJECT (enemy));
	g_autoptr(GPtrArray)               props = lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (source));
	g_autoptr(LrgPropertyInfo)         name = NULL;
	g_autoptr(LrgPropertyInfo)         heading = NULL;
	g_autoptr(LrgPropertyInfo)         level = NULL;
	g_autoptr(LrgPropertyInfo)         health = NULL;
	g_autoptr(LrgPropertyInfo)         id = NULL;
	g_auto(GValue)                     v = G_VALUE_INIT;

	/* Every readable property but the construct-only one */
	g_assert_cmpuint (props->len, ==, E_N_PROPS - 2);
	id = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source), "id");
	g_assert_null (id);

	/* Blurb prefixes: group and hint out, the rest is the tooltip */
	name = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source), "name");
	g_assert_cmpstr (lrg_property_info_get_group (name), ==, "Identity");
	g_assert_cmpstr (lrg_property_info_get_tooltip (name), ==, "The display name.");
	g_assert_cmpstr (lrg_property_info_get_label (name), ==, "Name");
	g_assert_null (lrg_property_info_get_hint (name));
	g_assert_false (lrg_property_info_get_read_only (name));

	/* qdata hint and group; nick wins as label */
	heading = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source), "heading");
	g_assert_cmpstr (lrg_property_info_get_hint (heading), ==, "angle");
	g_assert_cmpstr (lrg_property_info_get_group (heading), ==, "Transform");
	health = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source), "health");
	g_assert_cmpstr (lrg_property_info_get_label (health), ==, "Health");
	g_assert_cmpstr (lrg_property_info_get_tooltip (health), ==, "Hit points.");

	/* Not writable: read-only */
	level = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source), "level");
	g_assert_true (lrg_property_info_get_read_only (level));

	/* get / set go to the object */
	g_assert_true (lrg_property_source_get_value (LRG_PROPERTY_SOURCE (source), "health", &v));
	g_assert_cmpint (g_value_get_int (&v), ==, 100);
	g_value_set_int (&v, 250);
	g_assert_true (lrg_property_source_set_value (LRG_PROPERTY_SOURCE (source), "health", &v));
	g_assert_cmpint (enemy->health, ==, 250);

	/* Default state when the value equals the pspec default */
	g_assert_true (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (source), "speed") &
	               LRG_PROPERTY_STATE_DEFAULT);
	g_assert_false (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (source), "health") &
	                LRG_PROPERTY_STATE_DEFAULT);
	g_assert_true (lrg_property_source_reset (LRG_PROPERTY_SOURCE (source), "health"));
	g_assert_cmpint (enemy->health, ==, 100);
}

static void
on_changed_count (LrgPropertySource *source,
                  const gchar       *name,
                  gpointer           user_data)
{
	(*(guint *) user_data)++;
}

static void
test_object_source_notify (void)
{
	g_autoptr(TestEnemy)               enemy = enemy_new ();
	g_autoptr(LrgObjectPropertySource) source = lrg_object_property_source_new (G_OBJECT (enemy));
	guint                              any = 0;
	guint                              speed = 0;

	g_signal_connect (source, "property-changed", G_CALLBACK (on_changed_count), &any);
	g_signal_connect (source, "property-changed::speed", G_CALLBACK (on_changed_count), &speed);
	g_object_set (enemy, "health", 5, NULL);
	g_object_set (enemy, "speed", 9.0, NULL);
	g_assert_cmpuint (any, ==, 2);
	g_assert_cmpuint (speed, ==, 1);

	/* Hidden properties are neither listed nor followed */
	lrg_object_property_source_hide_property (source, "health");
	any = 0;
	g_object_set (enemy, "health", 6, NULL);
	g_assert_cmpuint (any, ==, 0);
	{
		g_autoptr(LrgPropertyInfo) info = lrg_property_source_find_property (LRG_PROPERTY_SOURCE (source),
		                                                                     "health");

		g_assert_null (info);
	}
}

static void
test_hints_and_values (void)
{
	g_autofree gchar *kind = NULL;
	g_autofree gchar *arg = NULL;
	g_auto(GValue)    c = G_VALUE_INIT;
	g_auto(GValue)    c2 = G_VALUE_INIT;
	g_auto(GValue)    vec = G_VALUE_INIT;
	g_autofree gchar *text = NULL;
	GrlColor          red = { 255, 0, 0, 255 };
	GrlVector3        v3 = { 1.0f, 2.5f, -3.0f };
	gdouble           min;
	gdouble           max;
	gdouble           step;

	lrg_property_hint_split ("array:asset:texture", &kind, &arg);
	g_assert_cmpstr (kind, ==, "array");
	g_assert_cmpstr (arg, ==, "asset:texture");
	g_assert_true (lrg_property_hint_parse_range ("range:0,10,0.5", &min, &max, &step));
	g_assert_cmpfloat (min, ==, 0.0);
	g_assert_cmpfloat (max, ==, 10.0);
	g_assert_cmpfloat (step, ==, 0.5);
	g_assert_false (lrg_property_hint_parse_range ("range:5", NULL, NULL, NULL));
	g_assert_false (lrg_property_hint_parse_range ("color", NULL, NULL, NULL));

	/* Boxed values compare by content */
	g_value_init (&c, GRL_TYPE_COLOR);
	g_value_set_boxed (&c, &red);
	g_value_init (&c2, GRL_TYPE_COLOR);
	g_value_set_boxed (&c2, &red);
	g_assert_true (lrg_property_values_equal (&c, &c2));

	/* Text round trips */
	text = lrg_property_value_to_string (&c);
	g_assert_cmpstr (text, ==, "#ff0000ff");
	g_value_init (&vec, GRL_TYPE_VECTOR3);
	g_value_set_boxed (&vec, &v3);
	g_free (text);
	text = lrg_property_value_to_string (&vec);
	g_assert_cmpstr (text, ==, "1, 2.5, -3");
	g_assert_true (lrg_property_value_from_string (&vec, "4, 5, 6"));
	g_assert_cmpfloat (((GrlVector3 *) g_value_get_boxed (&vec))->z, ==, 6.0f);
	g_assert_false (lrg_property_value_from_string (&vec, "4, 5"));
}

static void
test_gradient_stops (void)
{
	GrlColor                    black = { 0, 0, 0, 255 };
	GrlColor                    white = { 255, 255, 255, 255 };
	GrlColor                    red = { 255, 0, 0, 255 };
	GrlColor                    out;
	g_autoptr(LrgGradientStops) stops = lrg_gradient_stops_new_two (&black, &white);
	g_autoptr(LrgGradientStops) parsed = NULL;
	g_autofree gchar           *text = NULL;
	gfloat                      offset;

	lrg_gradient_stops_evaluate (stops, 0.5f, &out);
	g_assert_cmpint (out.r, ==, 128);
	g_assert_cmpint (out.a, ==, 255);

	/* Sorted insert, ends clamp */
	g_assert_cmpuint (lrg_gradient_stops_add (stops, 0.25f, &red), ==, 1);
	lrg_gradient_stops_evaluate (stops, 0.25f, &out);
	g_assert_cmpint (out.r, ==, 255);
	g_assert_cmpint (out.g, ==, 0);
	lrg_gradient_stops_evaluate (stops, -4.0f, &out);
	g_assert_cmpint (out.r, ==, 0);

	/* Moving re-sorts */
	g_assert_cmpint (lrg_gradient_stops_set_stop (stops, 1, 0.9f, NULL), ==, 1);
	g_assert_cmpint (lrg_gradient_stops_set_stop (stops, 1, 1.0f, NULL), ==, 2);
	lrg_gradient_stops_get_stop (stops, 2, &offset, &out);
	g_assert_cmpfloat (offset, ==, 1.0f);

	/* Text round trip and equality */
	text = lrg_gradient_stops_to_string (stops);
	parsed = lrg_gradient_stops_new_from_string (text);
	g_assert_nonnull (parsed);
	g_assert_true (lrg_gradient_stops_equal (stops, parsed));
	g_assert_null (lrg_gradient_stops_new_from_string ("0 #zz"));
	g_assert_true (lrg_gradient_stops_remove (stops, 0));
	g_assert_false (lrg_gradient_stops_equal (stops, parsed));
}

/* ==========================================================================
 * Grid structure
 * ========================================================================== */

static void
test_grid_structure (Fixture       *f,
                     gconstpointer  data)
{
	LrgPropertyRow *row;

	/* Sections per group, rows with the right editors */
	g_assert_nonnull (lrg_property_grid_get_section (f->grid, "Combat"));
	g_assert_nonnull (lrg_property_grid_get_section (f->grid, "Looks"));
	g_assert_null (lrg_property_grid_get_row (f->grid, "id"));
	g_assert_true (LRG_IS_TEXT_INPUT (editor_of (f->grid, "name")));
	g_assert_true (LRG_IS_TEXT_EDITOR (editor_of (f->grid, "notes")));
	g_assert_true (LRG_IS_COMBO_BOX (editor_of (f->grid, "kind")));
	g_assert_true (LRG_IS_CHECKBOX (editor_of (f->grid, "aggressive")));
	g_assert_true (LRG_IS_COMBO_BOX (editor_of (f->grid, "easing")));
	g_assert_true (LRG_IS_CONTAINER (editor_of (f->grid, "flags")));
	g_assert_true (LRG_IS_COLOR_PICKER (editor_of (f->grid, "tint")));
	g_assert_true (LRG_IS_VECTOR_EDIT (editor_of (f->grid, "offset")));
	g_assert_true (LRG_IS_CURVE_EDIT (editor_of (f->grid, "falloff")));
	g_assert_true (LRG_IS_GRADIENT_EDIT (editor_of (f->grid, "glow")));
	g_assert_true (LRG_IS_EXPANDER (editor_of (f->grid, "tags")));
	g_assert_nonnull (lrg_widget_find (editor_of (f->grid, "target"), "pick"));

	/* Read-only rows have a disabled editor */
	row = lrg_property_grid_get_row (f->grid, "level");
	g_assert_true (lrg_property_row_get_read_only (row));
	g_assert_false (lrg_widget_get_enabled (lrg_property_row_get_editor (row)));

	/* Values reached the editors */
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (editor_of (f->grid, "name"))), ==, "Grunt");
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (editor_of (f->grid, "kind"))), ==, "melee");
}

/* ==========================================================================
 * Editors round-trip values through the canvas
 * ========================================================================== */

static void
test_edit_text (Fixture       *f,
                gconstpointer  data)
{
	LrgWidget *input;
	gfloat     x;
	gfloat     y;

	g_object_set (f->a, "name", "", NULL);
	settle (f->canvas);
	input = editor_of (f->grid, "name");
	widget_center (input, &x, &y);
	click_at (f->canvas, x, y);
	type_text (f->canvas, "Orc", x, y);
	g_assert_cmpstr (f->a->name, ==, "Orc");
	key_held (f->canvas, GRL_KEY_ENTER, x, y, FALSE);

	/* One gesture: begin, a value per character, one end */
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->values, ==, 3);
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);
	g_assert_true (f->ids_consistent);
}

static void
test_edit_checkbox (Fixture       *f,
                    gconstpointer  data)
{
	LrgWidget *check = editor_of (f->grid, "aggressive");

	click_at (f->canvas, lrg_widget_get_world_x (check) + 8.0f,
	          lrg_widget_get_world_y (check) + lrg_widget_get_height (check) * 0.5f);
	g_assert_true (f->a->aggressive);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->ends, ==, 1);
}

static void
test_edit_combo (Fixture       *f,
                 gconstpointer  data)
{
	LrgWidget *combo = editor_of (f->grid, "easing");

	/* Focus the combo; Down picks the next item */
	lrg_canvas_set_focused_widget (f->canvas, combo);
	key_held (f->canvas, GRL_KEY_DOWN, -10.0f, -10.0f, FALSE);
	g_assert_cmpint (f->a->easing, ==, LRG_EASING_EASE_IN_QUAD);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);

	/* enum-strings on a string */
	combo = editor_of (f->grid, "kind");
	lrg_canvas_set_focused_widget (f->canvas, combo);
	key_held (f->canvas, GRL_KEY_END, -10.0f, -10.0f, FALSE);
	g_assert_cmpstr (f->a->kind, ==, "boss");
}

static void
test_edit_flags_and_color (Fixture       *f,
                           gconstpointer  data)
{
	LrgWidget     *flags = editor_of (f->grid, "flags");
	LrgWidget     *second = lrg_container_get_child (LRG_CONTAINER (flags), 1);
	LrgColorPicker *picker = LRG_COLOR_PICKER (editor_of (f->grid, "tint"));
	GrlColor       green = { 0, 255, 0, 255 };

	click_at (f->canvas, lrg_widget_get_world_x (second) + 8.0f,
	          lrg_widget_get_world_y (second) + lrg_widget_get_height (second) * 0.5f);
	g_assert_cmpuint (f->a->flags, ==, LRG_PROPERTY_STATE_OVERRIDDEN);

	/* A colour set while the popup is closed is a one-shot edit */
	reset_counts (f);
	lrg_color_picker_set_color (picker, &green);
	g_assert_cmpint (f->a->tint.g, ==, 255);
	g_assert_cmpint (f->a->tint.r, ==, 0);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);
}

/* scrub: press the editor's centre, drag @steps frames of @dx, release */
static void
scrub (Fixture   *f,
       LrgWidget *w,
       guint      steps,
       gfloat     dx)
{
	gfloat x;
	gfloat y;
	guint  i;

	widget_center (w, &x, &y);
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	for (i = 1; i <= steps; i++)
		drag_to (f->canvas, x + dx * (gfloat) i, y);
	release_at (f->canvas, x + dx * (gfloat) steps, y);
}

static void
test_scrub_spin (Fixture       *f,
                 gconstpointer  data)
{
	LrgWidget *frame = editor_of (f->grid, "health");

	/* Five frames of 12 units: two steps each */
	scrub (f, frame, 5, 12.0f);
	g_assert_cmpint (f->a->health, ==, 110);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->values, ==, 5);
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);
	g_assert_true (f->ids_consistent);
	g_assert_cmpstr (f->last_name, ==, "health");

	/* The angle row shows degrees, stores radians */
	reset_counts (f);
	frame = editor_of (f->grid, "heading");
	lrg_canvas_set_focused_widget (f->canvas, frame);
	key_held (f->canvas, GRL_KEY_UP, -10.0f, -10.0f, FALSE);
	g_assert_cmpfloat_with_epsilon (f->a->heading, G_PI / 180.0, 1e-6);
	g_assert_cmpuint (f->commits, ==, 1);
}

static void
test_scrub_cancel (Fixture       *f,
                   gconstpointer  data)
{
	LrgWidget *frame = editor_of (f->grid, "health");
	gfloat     x;
	gfloat     y;

	widget_center (frame, &x, &y);
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	drag_to (f->canvas, x + 30.0f, y);
	drag_to (f->canvas, x + 60.0f, y);
	g_assert_cmpint (f->a->health, ==, 110);

	/* Escape with the button still down: everything goes back */
	key_held (f->canvas, GRL_KEY_ESCAPE, x + 60.0f, y, TRUE);
	g_assert_cmpint (f->a->health, ==, 100);
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpuint (f->cancels, ==, 1);

	/* The rest of the drag does nothing */
	drag_to (f->canvas, x + 120.0f, y);
	release_at (f->canvas, x + 120.0f, y);
	g_assert_cmpint (f->a->health, ==, 100);
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpfloat (lrg_spin_box_get_value (LRG_SPIN_BOX (lrg_container_get_child (LRG_CONTAINER (frame), 0))),
	                   ==, 100.0);
}

static void
test_scrub_vector (Fixture       *f,
                   gconstpointer  data)
{
	LrgVectorEdit *edit = LRG_VECTOR_EDIT (editor_of (f->grid, "offset"));
	GrlRectangle   r;
	gfloat         y;
	guint          i;

	f->b->offset.y = 7.0f;
	set_both (f);
	reset_counts (f);

	/* Y differs between the two: that axis shows mixed */
	edit = LRG_VECTOR_EDIT (editor_of (f->grid, "offset"));
	g_assert_true (lrg_vector_edit_get_axis_mixed (edit, 1));
	g_assert_false (lrg_vector_edit_get_axis_mixed (edit, 0));

	/* Scrub X: 4 frames of 10 units (0.05 per 2 units) */
	lrg_vector_edit_get_field_rect (edit, 0, &r);
	y = r.y + r.height * 0.5f;
	move_to (f->canvas, r.x + r.width * 0.5f, y);
	press_at (f->canvas, r.x + r.width * 0.5f, y);
	for (i = 1; i <= 5; i++)
		drag_to (f->canvas, r.x + r.width * 0.5f + 10.0f * (gfloat) i, y);
	release_at (f->canvas, r.x + r.width * 0.5f + 50.0f, y);

	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpfloat_with_epsilon (f->a->offset.x, f->b->offset.x, 1e-6);
	g_assert_cmpfloat (f->a->offset.x, >, 0.5f);

	/* Each keeps its own Y */
	g_assert_cmpfloat (f->a->offset.y, ==, 0.0f);
	g_assert_cmpfloat (f->b->offset.y, ==, 7.0f);
}

/* ==========================================================================
 * Multi-edit, DELEGATE, lock, reuse
 * ========================================================================== */

static void
test_mixed (Fixture       *f,
            gconstpointer  data)
{
	LrgPropertyRow *row;

	g_object_set (f->b, "health", 200, "aggressive", TRUE, NULL);
	set_both (f);
	row = lrg_property_grid_get_row (f->grid, "health");
	g_assert_true (lrg_property_row_get_mixed (row));
	g_assert_false (lrg_property_row_get_mixed (lrg_property_grid_get_row (f->grid, "speed")));

	/* Mixed booleans: unchecked with an em dash */
	g_assert_cmpstr (lrg_checkbox_get_label (LRG_CHECKBOX (editor_of (f->grid, "aggressive"))), ==,
	                 "\xe2\x80\x94");

	/* The first edit writes the new value to both */
	lrg_canvas_set_focused_widget (f->canvas, lrg_property_row_get_editor (row));
	key_held (f->canvas, GRL_KEY_UP, -10.0f, -10.0f, FALSE);
	g_assert_cmpint (f->a->health, ==, 101);
	g_assert_cmpint (f->b->health, ==, 101);
	g_assert_false (lrg_property_row_get_mixed (row));

	/* Properties not shared with the same type are left out */
	g_assert_cmpuint (lrg_property_grid_get_n_sources (f->grid), ==, 2);
}

static void
test_delegate (Fixture       *f,
               gconstpointer  data)
{
	guint first;

	lrg_property_grid_set_apply_mode (f->grid, LRG_PROPERTY_APPLY_DELEGATE);
	scrub (f, editor_of (f->grid, "health"), 3, 12.0f);

	/* Signals only: the object is untouched */
	g_assert_cmpint (f->a->health, ==, 100);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->values, ==, 3);
	g_assert_cmpuint (f->commits, ==, 1);
	g_assert_true (f->ids_consistent);
	first = f->first_id;
	g_assert_cmpuint (first, >, 0);

	/* The next gesture gets a new id */
	reset_counts (f);
	scrub (f, editor_of (f->grid, "speed"), 2, 12.0f);
	g_assert_cmpuint (f->first_id, ==, first + 1);
	g_assert_cmpfloat (f->a->speed, ==, 3.5);
}

static void
test_lock_and_reuse (Fixture       *f,
                     gconstpointer  data)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (f->grid, "health");
	LrgWidget      *editor = lrg_property_row_get_editor (row);

	/* Same schema: rows and editors are kept, focus stays */
	lrg_canvas_set_focused_widget (f->canvas, editor);
	f->b->health = 42;
	lrg_property_grid_set_object (f->grid, G_OBJECT (f->b));
	settle (f->canvas);
	g_assert_true (lrg_property_grid_get_row (f->grid, "health") == row);
	g_assert_true (lrg_property_row_get_editor (row) == editor);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == editor);
	g_assert_cmpfloat (lrg_spin_box_get_value (LRG_SPIN_BOX (lrg_container_get_child (LRG_CONTAINER (editor), 0))),
	                   ==, 42.0);

	/* Locked: new sources are ignored */
	lrg_property_grid_set_locked (f->grid, TRUE);
	lrg_property_grid_set_object (f->grid, G_OBJECT (f->a));
	g_assert_true (lrg_object_property_source_get_object (
		LRG_OBJECT_PROPERTY_SOURCE (lrg_property_grid_get_source (f->grid, 0))) == G_OBJECT (f->b));
	lrg_property_grid_set_locked (f->grid, FALSE);

	/* Another schema: rows are rebuilt */
	{
		g_autoptr(TestSource) ts = g_object_new (TEST_TYPE_SOURCE, NULL);

		lrg_property_grid_set_source (f->grid, LRG_PROPERTY_SOURCE (ts));
		g_assert_null (lrg_property_grid_get_row (f->grid, "health"));
		g_assert_nonnull (lrg_property_grid_get_row (f->grid, "hp"));
		lrg_property_grid_set_source (f->grid, NULL);
	}
}

static void
test_groups_and_filter (Fixture       *f,
                        gconstpointer  data)
{
	LrgExpander *combat = lrg_property_grid_get_section (f->grid, "Combat");
	LrgExpander *identity = lrg_property_grid_get_section (f->grid, "Identity");

	/* The collapsed state is remembered across sources */
	lrg_property_grid_set_group_expanded (f->grid, "Combat", FALSE);
	g_assert_false (lrg_expander_get_expanded (combat));
	lrg_property_grid_set_object (f->grid, G_OBJECT (f->b));
	g_assert_false (lrg_expander_get_expanded (lrg_property_grid_get_section (f->grid, "Combat")));
	lrg_property_grid_set_group_expanded (f->grid, "Combat", TRUE);

	/* Filter: matching rows only, empty sections hidden */
	lrg_property_grid_set_show_filter (f->grid, TRUE);
	g_assert_true (lrg_widget_get_parent (lrg_property_grid_get_filter_input (f->grid)) ==
	               LRG_CONTAINER (f->grid));
	lrg_text_input_set_text (LRG_TEXT_INPUT (lrg_property_grid_get_filter_input (f->grid)), "HEAL");
	g_assert_cmpstr (lrg_property_grid_get_filter_text (f->grid), ==, "HEAL");
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (lrg_property_grid_get_row (f->grid, "health"))));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (lrg_property_grid_get_row (f->grid, "speed"))));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (identity)));
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (combat)));
	lrg_property_grid_set_filter_text (f->grid, NULL);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (identity)));
}

static void
test_reset_copy_paste (Fixture       *f,
                       gconstpointer  data)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (f->grid, "health");

	f->a->health = 500;
	g_object_notify (G_OBJECT (f->a), "health");

	/* The context menu's actions */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (row), "prop.reset", NULL));
	g_assert_cmpint (f->a->health, ==, 100);
	g_assert_true (lrg_widget_get_context_menu (LRG_WIDGET (row)) != NULL);

	/* Copy from a, paste into b */
	f->a->speed = 12.25;
	g_object_notify (G_OBJECT (f->a), "speed");
	g_assert_true (lrg_property_grid_copy_value (f->grid, "speed"));
	lrg_property_grid_set_object (f->grid, G_OBJECT (f->b));
	reset_counts (f);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (lrg_property_grid_get_row (f->grid, "speed")),
	                                           "prop.paste", NULL));
	g_assert_cmpfloat (f->b->speed, ==, 12.25);
	g_assert_cmpuint (f->commits, ==, 1);

	/* Text from another type is parsed when it fits */
	g_assert_true (lrg_property_grid_paste_value (f->grid, "health"));
	g_assert_cmpint (f->b->health, ==, 12);
}

static void
test_references (Fixture       *f,
                 gconstpointer  data)
{
	g_auto(GValue) v = G_VALUE_INIT;
	gchar         *picked = NULL;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "guid-1234");

	/* Pick button asks the application */
	g_signal_connect_swapped (f->grid, "pick-requested", G_CALLBACK (g_set_str), &picked);
	{
		LrgWidget *pick = lrg_widget_find (editor_of (f->grid, "target"), "pick");
		gfloat     x;
		gfloat     y;

		widget_center (pick, &x, &y);
		click_at (f->canvas, x, y);
	}
	g_assert_cmpstr (picked, ==, "target");
	g_free (picked);

	/* ... which hands the value back */
	g_assert_true (lrg_property_grid_set_reference_value (f->grid, "target", &v));
	g_assert_cmpstr (f->a->target, ==, "guid-1234");

	/* A real drag: an asset path dropped on the sprite row */
	{
		g_autoptr(LrgDragPayload) payload = lrg_drag_payload_new_string ("application/x-lrg-asset",
		                                                                 "textures/orc.png",
		                                                                 LRG_DRAG_ACTION_COPY);
		LrgWidget                *sprite = editor_of (f->grid, "sprite");
		gfloat                    x;
		gfloat                    y;

		widget_center (sprite, &x, &y);
		move_to (f->canvas, x, y - 200.0f);
		g_assert_true (lrg_canvas_begin_drag (f->canvas, NULL, payload, x, y - 200.0f));
		move_to (f->canvas, x, y);
		move_to (f->canvas, x + 1.0f, y);
		click_at (f->canvas, x + 1.0f, y);
		g_assert_false (lrg_canvas_is_dragging (f->canvas));
		g_assert_cmpstr (f->a->sprite, ==, "textures/orc.png");
	}

	/* Drops are matched to the hint */
	g_assert_false (lrg_property_grid_drop_reference (f->grid, "target", "application/x-lrg-asset", &v));
	g_assert_true (lrg_property_grid_drop_reference (f->grid, "sprite", "application/x-lrg-asset", &v));
	g_assert_cmpstr (f->a->sprite, ==, "guid-1234");
}

/* ==========================================================================
 * Arrays
 * ========================================================================== */

/* element_button: a named button of element @index's item */
static LrgWidget *
element_button (LrgPropertyRow *array,
                guint           index,
                const gchar    *name)
{
	LrgPropertyRow *er = lrg_property_row_get_element_row (array, index);

	g_assert_nonnull (er);
	return lrg_widget_find (LRG_WIDGET (lrg_widget_get_parent (LRG_WIDGET (er))), name);
}

static void
click_widget (Fixture   *f,
              LrgWidget *w)
{
	gfloat x;
	gfloat y;

	settle (f->canvas);
	widget_center (w, &x, &y);
	click_at (f->canvas, x, y);
}

/* type_into_element: type @text into element @index and press Enter */
static void
type_into_element (Fixture        *f,
                   LrgPropertyRow *array,
                   guint           index,
                   const gchar    *text)
{
	LrgWidget *input = lrg_property_row_get_editor (lrg_property_row_get_element_row (array, index));
	gfloat     x;
	gfloat     y;

	settle (f->canvas);
	widget_center (input, &x, &y);
	click_at (f->canvas, x, y);
	type_text (f->canvas, text, x, y);
	key_held (f->canvas, GRL_KEY_ENTER, x, y, FALSE);
}

static void
test_array (Fixture       *f,
            gconstpointer  data)
{
	LrgPropertyRow *tags = lrg_property_grid_get_row (f->grid, "tags");

	g_assert_cmpuint (lrg_property_row_get_n_elements (tags), ==, 0);

	/* Add twice through the + button, type into each element */
	click_widget (f, lrg_widget_find (lrg_property_row_get_editor (tags), "add"));
	g_assert_cmpuint (g_strv_length (f->a->tags), ==, 1);
	type_into_element (f, tags, 0, "boss");
	g_assert_cmpstr (f->a->tags[0], ==, "boss");
	click_widget (f, lrg_widget_find (lrg_property_row_get_editor (tags), "add"));
	type_into_element (f, tags, 1, "elite");
	g_assert_cmpuint (g_strv_length (f->a->tags), ==, 2);
	g_assert_cmpstr (f->a->tags[1], ==, "elite");

	/* Move the second up, remove the (new) second */
	reset_counts (f);
	click_widget (f, element_button (tags, 1, "up"));
	g_assert_cmpstr (f->a->tags[0], ==, "elite");
	g_assert_cmpstr (f->a->tags[1], ==, "boss");
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);
	click_widget (f, element_button (tags, 1, "remove"));
	g_assert_cmpuint (g_strv_length (f->a->tags), ==, 1);
	g_assert_cmpstr (f->a->tags[0], ==, "elite");
	g_assert_cmpuint (lrg_property_row_get_n_elements (tags), ==, 1);
}

/* ==========================================================================
 * Curve and gradient editors
 * ========================================================================== */

static void
test_curve_popup (Fixture       *f,
                  gconstpointer  data)
{
	LrgCurveEdit *edit = LRG_CURVE_EDIT (editor_of (f->grid, "falloff"));
	LrgCurveEdit *popup;
	gfloat        kx;
	gfloat        ky;
	gfloat        value;
	gfloat        x;
	gfloat        y;

	widget_center (LRG_WIDGET (edit), &x, &y);
	click_at (f->canvas, x, y);
	popup = lrg_curve_edit_get_popup_editor (edit);
	g_assert_nonnull (popup);
	settle (f->canvas);

	/* Drag the last key down in the popup editor */
	g_assert_true (lrg_curve_edit_get_key_position (popup, 1, &kx, &ky));
	move_to (f->canvas, kx, ky);
	press_at (f->canvas, kx, ky);
	drag_to (f->canvas, kx, ky + 10.0f);
	drag_to (f->canvas, kx, ky + 40.0f);
	release_at (f->canvas, kx, ky + 40.0f);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->ends, ==, 0);

	/* Closing the popup ends the gesture */
	lrg_canvas_dismiss_popup (f->canvas, LRG_WIDGET (popup));
	settle (f->canvas);
	g_assert_null (lrg_curve_edit_get_popup_editor (edit));
	g_assert_cmpuint (f->ends, ==, 1);
	g_assert_cmpuint (f->commits, ==, 1);
	lrg_keyframe_curve_get_key (f->a->falloff, 1, NULL, &value, NULL);
	g_assert_cmpfloat (value, <, 0.9f);
}

static void
test_gradient_drag (Fixture       *f,
                    gconstpointer  data)
{
	LrgGradientEdit *edit = LRG_GRADIENT_EDIT (editor_of (f->grid, "glow"));
	GrlRectangle     h;
	GrlRectangle     bar;
	gfloat           offset;
	gfloat           x;
	gfloat           y;

	g_assert_true (lrg_gradient_edit_get_handle_rect (edit, 1, &h));
	lrg_gradient_edit_get_bar_rect (edit, &bar);
	x = h.x + h.width * 0.5f;
	y = h.y + h.height * 0.5f;

	/* Drag the end stop to the middle */
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	drag_to (f->canvas, x - bar.width * 0.25f, y);
	drag_to (f->canvas, x - bar.width * 0.5f, y);
	release_at (f->canvas, x - bar.width * 0.5f, y);
	lrg_gradient_stops_get_stop (f->a->glow, 1, &offset, NULL);
	g_assert_cmpfloat_with_epsilon (offset, 0.5f, 0.02f);
	g_assert_cmpuint (f->begins, ==, 1);
	g_assert_cmpuint (f->values, ==, 2);
	g_assert_cmpuint (f->commits, ==, 1);

	/* Escape mid-drag restores */
	reset_counts (f);
	settle (f->canvas);
	g_assert_true (lrg_gradient_edit_get_handle_rect (edit, 1, &h));
	x = h.x + h.width * 0.5f;
	move_to (f->canvas, x, y);
	press_at (f->canvas, x, y);
	drag_to (f->canvas, x - 40.0f, y);
	key_held (f->canvas, GRL_KEY_ESCAPE, x - 40.0f, y, TRUE);
	release_at (f->canvas, x - 40.0f, y);
	lrg_gradient_stops_get_stop (f->a->glow, 1, &offset, NULL);
	g_assert_cmpfloat_with_epsilon (offset, 0.5f, 0.02f);
	g_assert_cmpuint (f->cancels, ==, 1);

	/* A click on the bar adds a stop */
	reset_counts (f);
	click_at (f->canvas, bar.x + bar.width * 0.9f, bar.y + bar.height * 0.5f);
	g_assert_cmpuint (lrg_gradient_stops_get_n_stops (f->a->glow), ==, 3);
	g_assert_cmpuint (f->commits, ==, 1);
}

/* ==========================================================================
 * State markers and painting
 * ========================================================================== */

static void
test_state_markers (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(TestSource) ts = g_object_new (TEST_TYPE_SOURCE, NULL);
	LrgPropertyRow       *hp;
	guint                 plain;
	guint                 marked;

	lrg_property_grid_set_source (f->grid, LRG_PROPERTY_SOURCE (ts));
	settle (f->canvas);
	hp = lrg_property_grid_get_row (f->grid, "hp");
	g_assert_cmpint (lrg_property_row_get_state (hp), ==, LRG_PROPERTY_STATE_NONE);
	g_assert_true (lrg_property_row_get_state (lrg_property_grid_get_row (f->grid, "x")) &
	               LRG_PROPERTY_STATE_ANIMATED);
	plain = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));

	/* Overridden: an accent bar and a bolder label */
	ts->hp_state = LRG_PROPERTY_STATE_OVERRIDDEN;
	lrg_property_grid_refresh (f->grid);
	g_assert_true (lrg_property_row_get_state (hp) & LRG_PROPERTY_STATE_OVERRIDDEN);
	marked = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));
	g_assert_cmpuint (marked, >, plain);
	lrg_property_grid_set_source (f->grid, NULL);
}

static void
test_paint_presets (Fixture       *f,
                    gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	set_both (f);
	g_object_set (f->b, "health", 7, NULL);
	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		LrgPaintList       *list;

		lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
		settle (f->canvas);
		list = lrg_canvas_record (f->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 100);
	}
	g_assert_cmpuint (i, ==, 5);
}

/* ==========================================================================
 * Standalone editors
 * ========================================================================== */

static void
on_widget_count (gpointer user_data)
{
	(*(guint *) user_data)++;
}

static void
test_vector_edit_typing (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgVectorEdit       *edit = lrg_vector_edit_new (3);
	GrlRectangle         r;
	guint                begins = 0;
	guint                ends = 0;
	gfloat               x;
	gfloat               y;

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 200.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (edit));
	g_object_unref (edit);
	lrg_widget_allocate (LRG_WIDGET (edit), 10.0f, 10.0f, 300.0f, 28.0f);
	lrg_vector_edit_set_show_uniform (edit, TRUE);
	lrg_canvas_record (canvas);
	g_signal_connect_swapped (edit, "edit-begin", G_CALLBACK (on_widget_count), &begins);
	g_signal_connect_swapped (edit, "edit-end", G_CALLBACK (on_widget_count), &ends);

	/* A click without moving types into the axis */
	g_assert_true (lrg_vector_edit_get_field_rect (edit, 2, &r));
	x = r.x + r.width * 0.5f;
	y = r.y + r.height * 0.5f;
	click_at (canvas, x, y);
	g_assert_cmpint (lrg_vector_edit_get_editing_axis (edit), ==, 2);
	type_text (canvas, "2.5", x, y);
	key_held (canvas, GRL_KEY_ENTER, x, y, FALSE);
	g_assert_cmpfloat (lrg_vector_edit_get_component (edit, 2), ==, 2.5f);
	g_assert_cmpuint (begins, ==, 1);
	g_assert_cmpuint (ends, ==, 1);

	/* Uniform lock: typing one axis scales the others */
	lrg_vector_edit_set_component (edit, 0, 1.0f);
	g_assert_true (lrg_vector_edit_get_lock_rect (edit, &r));
	click_at (canvas, r.x + r.width * 0.5f, r.y + r.height * 0.5f);
	g_assert_true (lrg_vector_edit_get_uniform (edit));
	lrg_vector_edit_get_field_rect (edit, 2, &r);
	click_at (canvas, r.x + r.width * 0.5f, y);
	type_text (canvas, "5", x, y);
	key_held (canvas, GRL_KEY_ENTER, x, y, FALSE);
	g_assert_cmpfloat (lrg_vector_edit_get_component (edit, 2), ==, 5.0f);
	g_assert_cmpfloat (lrg_vector_edit_get_component (edit, 0), ==, 2.0f);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/property-grid/source/listing", test_object_source_listing);
	g_test_add_func ("/ui/property-grid/source/notify", test_object_source_notify);
	g_test_add_func ("/ui/property-grid/source/hints-and-values", test_hints_and_values);
	g_test_add_func ("/ui/property-grid/gradient-stops", test_gradient_stops);
	g_test_add_func ("/ui/property-grid/vector-edit/typing", test_vector_edit_typing);

#define ADD(path, func) \
	g_test_add ("/ui/property-grid/" path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

	ADD ("structure", test_grid_structure);
	ADD ("edit/text", test_edit_text);
	ADD ("edit/checkbox", test_edit_checkbox);
	ADD ("edit/combo", test_edit_combo);
	ADD ("edit/flags-and-color", test_edit_flags_and_color);
	ADD ("edit/scrub-spin", test_scrub_spin);
	ADD ("edit/scrub-cancel", test_scrub_cancel);
	ADD ("edit/scrub-vector", test_scrub_vector);
	ADD ("edit/references", test_references);
	ADD ("multi/mixed", test_mixed);
	ADD ("delegate", test_delegate);
	ADD ("lock-and-reuse", test_lock_and_reuse);
	ADD ("groups-and-filter", test_groups_and_filter);
	ADD ("reset-copy-paste", test_reset_copy_paste);
	ADD ("array", test_array);
	ADD ("curve-popup", test_curve_popup);
	ADD ("gradient-drag", test_gradient_drag);
	ADD ("state-markers", test_state_markers);
	ADD ("paint-presets", test_paint_presets);

#undef ADD

	return g_test_run ();
}
