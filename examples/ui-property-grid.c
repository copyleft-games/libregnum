/* ui-property-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A reflected property grid editing "enemy definitions": a GObject
 * with one property of every kind LrgPropertyGrid knows (text, multi-
 * line notes, a choice list, numbers with ranges, an angle, a flag set,
 * an enum, a colour, vectors with a uniform-scale lock, a damage
 * falloff curve, a glow gradient, node and asset references and a
 * list of loot tags), grouped into sections.
 *
 * Tick the enemies on the left to edit several at once: values they
 * disagree on show an em dash until an edit writes all of them. The
 * apply mode switches between DIRECT (the grid writes the objects) and
 * DELEGATE (the grid only emits edit-begin / edit-value / edit-end and
 * this example applies them). Either way every gesture becomes ONE
 * entry on the small undo stack on the right: a whole scrub, a whole
 * colour-picker session, a typed name. Escape during a drag cancels
 * it. Right-click a row for Reset / Copy / Paste.
 *
 * Controls:
 *   Mouse                 everything (drag numbers sideways to scrub)
 *   Tab / Shift+Tab       move focus
 *   Escape                cancel a drag, close popups
 *   Ctrl+Z / Ctrl+Y       undo / redo
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <stdlib.h>

#define WINDOW_WIDTH  (1280)
#define WINDOW_HEIGHT (800)
#define SIDE_WIDTH    (240.0f)
#define N_ENEMIES     (3)

static const gchar *license_text =
	"ui-property-grid - reflected property grid example for libregnum\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ==========================================================================
 * EnemyTraits: a flags type for the "traits" property
 * ========================================================================== */

typedef enum
{
	ENEMY_TRAIT_FLYING  = 1 << 0,
	ENEMY_TRAIT_ARMORED = 1 << 1,
	ENEMY_TRAIT_BOSS    = 1 << 2
} EnemyTraits;

static GType
enemy_traits_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ ENEMY_TRAIT_FLYING, "ENEMY_TRAIT_FLYING", "flying" },
			{ ENEMY_TRAIT_ARMORED, "ENEMY_TRAIT_ARMORED", "armored" },
			{ ENEMY_TRAIT_BOSS, "ENEMY_TRAIT_BOSS", "boss" },
			{ 0, NULL, NULL }
		};

		g_once_init_leave (&type_id, g_flags_register_static ("EnemyTraits", values));
	}
	return type_id;
}

/* ==========================================================================
 * Enemy: the edited object
 * ========================================================================== */

#define ENEMY_TYPE (enemy_get_type ())
G_DECLARE_FINAL_TYPE (Enemy, enemy, EXAMPLE, ENEMY, GObject)

struct _Enemy
{
	GObject           parent_instance;
	gchar            *name;
	gchar            *role;
	gchar            *notes;
	gint              health;
	gdouble           speed;
	gdouble           attack_range;
	gboolean          aggressive;
	guint             traits;
	LrgEasingType     movement;
	GrlColor          tint;
	GrlVector3        spawn_offset;
	GrlVector3        scale;
	gdouble           heading;
	LrgKeyframeCurve *falloff;
	LrgGradientStops *glow;
	gchar            *patrol_target;
	gchar            *sprite;
	gchar           **loot;
	gint              level;
};

G_DEFINE_FINAL_TYPE (Enemy, enemy, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_ROLE,
	PROP_NOTES,
	PROP_HEALTH,
	PROP_SPEED,
	PROP_ATTACK_RANGE,
	PROP_AGGRESSIVE,
	PROP_TRAITS,
	PROP_MOVEMENT,
	PROP_TINT,
	PROP_SPAWN_OFFSET,
	PROP_SCALE,
	PROP_HEADING,
	PROP_FALLOFF,
	PROP_GLOW,
	PROP_PATROL_TARGET,
	PROP_SPRITE,
	PROP_LOOT,
	PROP_LEVEL,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

static void
enemy_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
	Enemy *self = EXAMPLE_ENEMY (object);

	switch (prop_id)
	{
	case PROP_NAME: g_value_set_string (value, self->name); break;
	case PROP_ROLE: g_value_set_string (value, self->role); break;
	case PROP_NOTES: g_value_set_string (value, self->notes); break;
	case PROP_HEALTH: g_value_set_int (value, self->health); break;
	case PROP_SPEED: g_value_set_double (value, self->speed); break;
	case PROP_ATTACK_RANGE: g_value_set_double (value, self->attack_range); break;
	case PROP_AGGRESSIVE: g_value_set_boolean (value, self->aggressive); break;
	case PROP_TRAITS: g_value_set_flags (value, self->traits); break;
	case PROP_MOVEMENT: g_value_set_enum (value, self->movement); break;
	case PROP_TINT: g_value_set_boxed (value, &self->tint); break;
	case PROP_SPAWN_OFFSET: g_value_set_boxed (value, &self->spawn_offset); break;
	case PROP_SCALE: g_value_set_boxed (value, &self->scale); break;
	case PROP_HEADING: g_value_set_double (value, self->heading); break;
	case PROP_FALLOFF: g_value_set_object (value, self->falloff); break;
	case PROP_GLOW: g_value_set_boxed (value, self->glow); break;
	case PROP_PATROL_TARGET: g_value_set_string (value, self->patrol_target); break;
	case PROP_SPRITE: g_value_set_string (value, self->sprite); break;
	case PROP_LOOT: g_value_set_boxed (value, self->loot); break;
	case PROP_LEVEL: g_value_set_int (value, self->level); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* replace_string: store a copy, TRUE when it changed */
static gboolean
replace_string (gchar        **slot,
                const GValue  *value)
{
	if (g_strcmp0 (*slot, g_value_get_string (value)) == 0)
		return FALSE;
	g_free (*slot);
	*slot = g_value_dup_string (value);
	return TRUE;
}

static void
enemy_set_property (GObject      *object,
                    guint         prop_id,
                    const GValue *value,
                    GParamSpec   *pspec)
{
	Enemy *self = EXAMPLE_ENEMY (object);

	switch (prop_id)
	{
	case PROP_NAME: replace_string (&self->name, value); break;
	case PROP_ROLE: replace_string (&self->role, value); break;
	case PROP_NOTES: replace_string (&self->notes, value); break;
	case PROP_HEALTH: self->health = g_value_get_int (value); break;
	case PROP_SPEED: self->speed = g_value_get_double (value); break;
	case PROP_ATTACK_RANGE: self->attack_range = g_value_get_double (value); break;
	case PROP_AGGRESSIVE: self->aggressive = g_value_get_boolean (value); break;
	case PROP_TRAITS: self->traits = g_value_get_flags (value); break;
	case PROP_MOVEMENT: self->movement = (LrgEasingType) g_value_get_enum (value); break;
	case PROP_TINT:
		if (g_value_get_boxed (value) != NULL)
			self->tint = *(const GrlColor *) g_value_get_boxed (value);
		break;
	case PROP_SPAWN_OFFSET:
		if (g_value_get_boxed (value) != NULL)
			self->spawn_offset = *(const GrlVector3 *) g_value_get_boxed (value);
		break;
	case PROP_SCALE:
		if (g_value_get_boxed (value) != NULL)
			self->scale = *(const GrlVector3 *) g_value_get_boxed (value);
		break;
	case PROP_HEADING: self->heading = g_value_get_double (value); break;
	case PROP_FALLOFF: g_set_object (&self->falloff, g_value_get_object (value)); break;
	case PROP_GLOW:
		lrg_gradient_stops_free (self->glow);
		self->glow = g_value_dup_boxed (value);
		break;
	case PROP_PATROL_TARGET: replace_string (&self->patrol_target, value); break;
	case PROP_SPRITE: replace_string (&self->sprite, value); break;
	case PROP_LOOT:
		g_strfreev (self->loot);
		self->loot = g_value_dup_boxed (value);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); return;
	}
	/* Every set notifies, so the grid and undo stack follow */
	g_object_notify_by_pspec (object, pspec);
}

static void
enemy_finalize (GObject *object)
{
	Enemy *self = EXAMPLE_ENEMY (object);

	g_free (self->name);
	g_free (self->role);
	g_free (self->notes);
	g_clear_object (&self->falloff);
	lrg_gradient_stops_free (self->glow);
	g_free (self->patrol_target);
	g_free (self->sprite);
	g_strfreev (self->loot);
	G_OBJECT_CLASS (enemy_parent_class)->finalize (object);
}

static void
enemy_class_init (EnemyClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	oc->get_property = enemy_get_property;
	oc->set_property = enemy_set_property;
	oc->finalize = enemy_finalize;

	/* Hints and groups ride in the blurb: "[hint:...][group:...] tooltip" */
	props[PROP_NAME] = g_param_spec_string ("name", NULL, "[group:Identity] Shown above the enemy.", "Enemy", rw);
	props[PROP_ROLE] = g_param_spec_string ("role", NULL, "[hint:enum-strings:melee,ranged,support,boss][group:Identity]",
	                                        "melee", rw);
	props[PROP_NOTES] = g_param_spec_string ("notes", NULL, "[hint:multiline][group:Identity] Designer notes.", "", rw);
	props[PROP_LEVEL] = g_param_spec_int ("level", NULL, "[group:Identity] Derived from health (read-only).",
	                                      1, 99, 1, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_HEALTH] = g_param_spec_int ("health", NULL, "[group:Combat] Hit points.", 1, 5000, 100, rw);
	props[PROP_SPEED] = g_param_spec_double ("speed", NULL, "[hint:range:0,20,0.1][group:Combat] Units per second.",
	                                         0.0, 20.0, 3.0, rw);
	props[PROP_ATTACK_RANGE] = g_param_spec_double ("attack-range", NULL, "[group:Combat]", 0.0, 100.0, 1.5, rw);
	props[PROP_AGGRESSIVE] = g_param_spec_boolean ("aggressive", NULL, "[group:Combat] Attacks on sight.", FALSE, rw);
	props[PROP_TRAITS] = g_param_spec_flags ("traits", NULL, "[group:Combat]", enemy_traits_get_type (), 0, rw);
	props[PROP_MOVEMENT] = g_param_spec_enum ("movement", NULL, "[group:Combat] Easing of each step.",
	                                          LRG_TYPE_EASING_TYPE, LRG_EASING_LINEAR, rw);
	props[PROP_TINT] = g_param_spec_boxed ("tint", NULL, "[group:Looks]", GRL_TYPE_COLOR, rw);
	props[PROP_SPRITE] = g_param_spec_string ("sprite", NULL, "[hint:asset:texture][group:Looks]", "", rw);
	props[PROP_GLOW] = g_param_spec_boxed ("glow", NULL, "[group:Looks] Aura colour over its lifetime.",
	                                       LRG_TYPE_GRADIENT_STOPS, rw);
	props[PROP_FALLOFF] = g_param_spec_object ("falloff", "Damage falloff", "[group:Combat] Damage by distance.",
	                                           LRG_TYPE_KEYFRAME_CURVE, rw);
	props[PROP_SPAWN_OFFSET] = g_param_spec_boxed ("spawn-offset", NULL, "[group:Transform]", GRL_TYPE_VECTOR3, rw);
	props[PROP_SCALE] = g_param_spec_boxed ("scale", NULL, "[hint:vector3:uniform][group:Transform]",
	                                        GRL_TYPE_VECTOR3, rw);
	props[PROP_HEADING] = g_param_spec_double ("heading", NULL, "[hint:angle][group:Transform] Facing.",
	                                           -G_PI, G_PI, 0.0, rw);
	props[PROP_PATROL_TARGET] = g_param_spec_string ("patrol-target", NULL, "[hint:node-ref][group:AI]", "", rw);
	props[PROP_LOOT] = g_param_spec_boxed ("loot", NULL, "[hint:array:enum-strings:gold,potion,key,gem][group:AI]",
	                                       G_TYPE_STRV, rw);

	g_object_class_install_properties (oc, N_PROPS, props);
}

static void
enemy_init (Enemy *self)
{
	GrlColor gold = { 0xff, 0xc8, 0x3c, 0xff };
	GrlColor red = { 0xe0, 0x30, 0x40, 0x00 };

	self->name = g_strdup ("Enemy");
	self->role = g_strdup ("melee");
	self->notes = g_strdup ("");
	self->health = 100;
	self->speed = 3.0;
	self->attack_range = 1.5;
	self->tint.r = self->tint.g = self->tint.b = self->tint.a = 255;
	self->scale.x = self->scale.y = self->scale.z = 1.0f;
	self->falloff = lrg_keyframe_curve_new ();
	lrg_keyframe_curve_add_key (self->falloff, 0.0f, 1.0f, LRG_EASING_EASE_OUT_QUAD);
	lrg_keyframe_curve_add_key (self->falloff, 1.0f, 0.0f, LRG_EASING_LINEAR);
	self->glow = lrg_gradient_stops_new_two (&gold, &red);
	self->patrol_target = g_strdup ("");
	self->sprite = g_strdup ("");
	self->loot = g_new0 (gchar *, 1);
	self->level = 1;
}

/* enemy_new: one of the three sample enemies */
static Enemy *
enemy_new (const gchar *name,
           gint         health,
           gdouble      speed,
           guint8       r,
           guint8       g,
           guint8       b)
{
	Enemy    *self = g_object_new (ENEMY_TYPE, "name", name, "health", health, "speed", speed, NULL);
	GrlColor  tint = { r, g, b, 255 };

	g_object_set (self, "tint", &tint, NULL);
	return self;
}

/* ==========================================================================
 * A tiny undo stack: one entry per gesture
 * ========================================================================== */

typedef struct
{
	gchar     *name;     /* property */
	GPtrArray *sources;  /* LrgPropertySource */
	GPtrArray *before;   /* GValue* per source */
	GPtrArray *after;    /* GValue* per source */
} UndoEntry;

static void
gvalue_free (gpointer p)
{
	GValue *v = p;

	if (G_IS_VALUE (v))
		g_value_unset (v);
	g_free (v);
}

static void
undo_entry_free (gpointer p)
{
	UndoEntry *e = p;

	g_free (e->name);
	g_ptr_array_unref (e->sources);
	g_ptr_array_unref (e->before);
	g_ptr_array_unref (e->after);
	g_free (e);
}

typedef struct
{
	LrgCanvas       *canvas;
	LrgPropertyGrid *grid;
	Enemy           *enemies[N_ENEMIES];
	LrgCheckbox     *picks[N_ENEMIES];
	LrgLabel        *log;
	LrgLabel        *status;
	GPtrArray       *undo;      /* UndoEntry */
	GPtrArray       *redo;      /* UndoEntry */
	UndoEntry       *pending;   /* the gesture in flight */
	GString         *log_text;
	guint            n_values;  /* edit-value emissions of the gesture */
	guint            pick_turn;
} App;

/* read_all: every source's value of @name */
static GPtrArray *
read_all (GPtrArray   *sources,
          const gchar *name)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (gvalue_free);
	guint      i;

	for (i = 0; i < sources->len; i++)
	{
		GValue *v = g_new0 (GValue, 1);

		lrg_property_source_get_value (g_ptr_array_index (sources, i), name, v);
		g_ptr_array_add (out, v);
	}
	return out;
}

/* write_all: put @values back into the sources */
static void
write_all (GPtrArray   *sources,
           const gchar *name,
           GPtrArray   *values)
{
	guint i;

	for (i = 0; i < sources->len && i < values->len; i++)
		if (G_IS_VALUE ((GValue *) g_ptr_array_index (values, i)))
			lrg_property_source_set_value (g_ptr_array_index (sources, i), name, g_ptr_array_index (values, i));
}

/* log_line: newest first, at most 18 lines */
static void
log_line (App         *app,
          const gchar *line)
{
	g_auto(GStrv) lines = NULL;

	g_string_prepend (app->log_text, "\n");
	g_string_prepend (app->log_text, line);
	lines = g_strsplit (app->log_text->str, "\n", 19);
	if (g_strv_length (lines) == 19)
	{
		g_free (lines[18]);
		lines[18] = NULL;
	}
	{
		g_autofree gchar *joined = g_strjoinv ("\n", lines);

		g_string_assign (app->log_text, joined);
	}
	lrg_label_set_text (app->log, app->log_text->str);
}

static void
update_status (App *app)
{
	g_autofree gchar *text = g_strdup_printf ("%s mode, %u undo / %u redo",
	                                          lrg_property_grid_get_apply_mode (app->grid) ==
	                                          LRG_PROPERTY_APPLY_DIRECT ? "DIRECT" : "DELEGATE",
	                                          app->undo->len, app->redo->len);

	lrg_label_set_text (app->status, text);
}

/* sources_snapshot: a strong copy of the grid's sources */
static GPtrArray *
sources_snapshot (LrgPropertyGrid *grid)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (g_object_unref);
	guint      i;

	for (i = 0; i < lrg_property_grid_get_n_sources (grid); i++)
		g_ptr_array_add (out, g_object_ref (lrg_property_grid_get_source (grid, i)));
	return out;
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture,
               gpointer         user_data)
{
	App *app = user_data;

	/* Capture "before" for every edited object */
	g_clear_pointer (&app->pending, undo_entry_free);
	app->pending = g_new0 (UndoEntry, 1);
	app->pending->name = g_strdup (name);
	app->pending->sources = sources_snapshot (grid);
	app->pending->before = read_all (app->pending->sources, name);
	app->pending->after = g_ptr_array_new_with_free_func (gvalue_free);
	app->n_values = 0;
}

static void
on_edit_value (LrgPropertyGrid *grid,
               const gchar     *name,
               const GValue    *value,
               guint            gesture,
               gboolean         interactive,
               gpointer         user_data)
{
	App  *app = user_data;
	guint i;

	app->n_values++;

	/* DELEGATE: the application applies (here: live, per object, with
	 * only the vector axes the gesture touched) */
	if (lrg_property_grid_get_apply_mode (grid) != LRG_PROPERTY_APPLY_DELEGATE)
		return;
	for (i = 0; i < lrg_property_grid_get_n_sources (grid); i++)
	{
		g_auto(GValue) per = G_VALUE_INIT;

		if (lrg_property_grid_get_value_for_source (grid, name, i, value, &per))
			lrg_property_source_set_value (lrg_property_grid_get_source (grid, i), name, &per);
	}
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture,
             gboolean         commit,
             gpointer         user_data)
{
	App              *app = user_data;
	g_autofree gchar *line = NULL;
	UndoEntry        *e = g_steal_pointer (&app->pending);

	if (e == NULL)
		return;

	/* Cancelled: DELEGATE puts the values back itself */
	if (!commit)
	{
		if (lrg_property_grid_get_apply_mode (grid) == LRG_PROPERTY_APPLY_DELEGATE)
			write_all (e->sources, e->name, e->before);
		line = g_strdup_printf ("#%u %s cancelled", gesture, name);
		log_line (app, line);
		undo_entry_free (e);
		return;
	}

	/* One undo entry for the whole gesture */
	g_ptr_array_unref (e->after);
	e->after = read_all (e->sources, name);
	g_ptr_array_add (app->undo, e);
	g_ptr_array_set_size (app->redo, 0);
	line = g_strdup_printf ("#%u %s: %u value%s x %u object%s", gesture, name, app->n_values,
	                        app->n_values == 1 ? "" : "s", e->sources->len, e->sources->len == 1 ? "" : "s");
	log_line (app, line);
	update_status (app);
}

static void
on_reset_requested (LrgPropertyGrid *grid,
                    const gchar     *name,
                    gpointer         user_data)
{
	App       *app = user_data;
	UndoEntry *e = g_new0 (UndoEntry, 1);
	guint      i;

	/*
	 * Reset is undoable too. The example resets the objects itself in
	 * both modes so it can record the values after; in DIRECT mode the
	 * grid's own reset that follows this signal changes nothing more.
	 */
	e->name = g_strdup (name);
	e->sources = sources_snapshot (grid);
	e->before = read_all (e->sources, name);
	for (i = 0; i < e->sources->len; i++)
		lrg_property_source_reset (g_ptr_array_index (e->sources, i), name);
	e->after = read_all (e->sources, name);
	g_ptr_array_add (app->undo, e);
	g_ptr_array_set_size (app->redo, 0);
	{
		g_autofree gchar *line = g_strdup_printf ("reset %s", name);

		log_line (app, line);
	}
	update_status (app);
}

static void
undo (App *app)
{
	UndoEntry        *e;
	g_autofree gchar *line = NULL;

	if (app->undo->len == 0)
		return;
	e = g_ptr_array_steal_index (app->undo, app->undo->len - 1);
	write_all (e->sources, e->name, e->before);
	line = g_strdup_printf ("undo %s", e->name);
	g_ptr_array_add (app->redo, e);
	log_line (app, line);
	update_status (app);
}

static void
redo (App *app)
{
	UndoEntry        *e;
	g_autofree gchar *line = NULL;

	if (app->redo->len == 0)
		return;
	e = g_ptr_array_steal_index (app->redo, app->redo->len - 1);
	write_all (e->sources, e->name, e->after);
	line = g_strdup_printf ("redo %s", e->name);
	g_ptr_array_add (app->undo, e);
	log_line (app, line);
	update_status (app);
}

/* ==========================================================================
 * Selection, references, controls
 * ========================================================================== */

/* sync_selection: the grid edits the ticked enemies */
static void
sync_selection (App *app)
{
	g_autoptr(GPtrArray) sources = g_ptr_array_new_with_free_func (g_object_unref);
	guint                i;

	for (i = 0; i < N_ENEMIES; i++)
		if (lrg_checkbox_get_checked (app->picks[i]))
			g_ptr_array_add (sources, lrg_object_property_source_new (G_OBJECT (app->enemies[i])));
	lrg_property_grid_set_sources (app->grid, sources);
}

static void
on_pick_toggled (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	sync_selection (user_data);
}

/* list_references: what the node-ref and asset drop-downs offer */
static gchar **
list_references (LrgPropertyGrid *grid,
                 const gchar     *name,
                 const gchar     *hint,
                 gpointer         user_data)
{
	static const gchar *nodes[] = { "Waypoint A", "Waypoint B", "Tower", "Gate", NULL };
	static const gchar *assets[] = { "textures/grunt.png", "textures/archer.png", "textures/ogre.png", NULL };

	return g_strdupv ((gchar **) (g_str_has_prefix (hint, "asset") ? assets : nodes));
}

/* on_pick_requested: a stand-in for an eyedropper: take the next node */
static void
on_pick_requested (LrgPropertyGrid *grid,
                   const gchar     *name,
                   const gchar     *hint,
                   gpointer         user_data)
{
	App              *app = user_data;
	g_auto(GStrv)     items = list_references (grid, name, hint, NULL);
	g_auto(GValue)    v = G_VALUE_INIT;
	g_autofree gchar *line = NULL;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, items[app->pick_turn++ % g_strv_length (items)]);
	lrg_property_grid_set_reference_value (grid, name, &v);
	line = g_strdup_printf ("picked %s for %s", g_value_get_string (&v), name);
	log_line (app, line);
}

static void
on_mode_changed (LrgComboBox *combo,
                 gpointer     user_data)
{
	App *app = user_data;

	lrg_property_grid_set_apply_mode (app->grid, lrg_combo_box_get_selected (combo) == 1
	                                  ? LRG_PROPERTY_APPLY_DELEGATE : LRG_PROPERTY_APPLY_DIRECT);
	update_status (app);
}

static void
on_undo_clicked (LrgButton *button,
                 gpointer   user_data)
{
	undo (user_data);
}

static void
on_redo_clicked (LrgButton *button,
                 gpointer   user_data)
{
	redo (user_data);
}

static void
on_filter_toggled (LrgCheckbox *check,
                   gboolean     checked,
                   gpointer     user_data)
{
	lrg_property_grid_set_show_filter (LRG_PROPERTY_GRID (user_data), checked);
}

static void
on_lock_toggled (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	lrg_property_grid_set_locked (LRG_PROPERTY_GRID (user_data), checked);
}

/* add: append a child, dropping our reference */
static gpointer
add (gpointer container,
     gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (container), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

/* side_panel: a framed vertical panel */
static LrgVBox *
side_panel (LrgHBox *root)
{
	LrgPanel *panel = lrg_panel_new ();
	LrgVBox  *box = lrg_vbox_new ();

	lrg_panel_set_fill_children (panel, TRUE);
	lrg_widget_set_min_size (LRG_WIDGET (panel), SIDE_WIDTH, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (panel), SIDE_WIDTH, G_MAXFLOAT);
	lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 12.0f);
	add (panel, box);
	add (root, panel);
	return box;
}

static LrgWidget *
build_ui (App          *app,
          LrgHBox     **root_out)
{
	static const gchar *names[N_ENEMIES] = { "Grunt", "Archer", "Ogre" };
	LrgHBox            *root = lrg_hbox_new ();
	LrgVBox            *left;
	LrgVBox            *right;
	LrgScrollBox       *scroll;
	LrgComboBox        *mode;
	LrgCheckbox        *check;
	LrgButton          *button;
	guint               i;

	lrg_container_set_spacing (LRG_CONTAINER (root), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (root), 8.0f);

	/* Left: what to edit and how */
	left = side_panel (root);
	add (left, lrg_label_new ("Enemies (tick several\nto multi-edit)"));
	for (i = 0; i < N_ENEMIES; i++)
	{
		app->picks[i] = add (left, lrg_checkbox_new (names[i]));
		g_signal_connect (app->picks[i], "toggled", G_CALLBACK (on_pick_toggled), app);
	}
	add (left, lrg_label_new ("Apply mode"));
	mode = add (left, lrg_combo_box_new ());
	lrg_combo_box_append (mode, "DIRECT");
	lrg_combo_box_append (mode, "DELEGATE");
	lrg_combo_box_set_selected (mode, lrg_property_grid_get_apply_mode (app->grid) ==
	                                  LRG_PROPERTY_APPLY_DELEGATE ? 1 : 0);
	g_signal_connect (mode, "changed", G_CALLBACK (on_mode_changed), app);
	check = add (left, lrg_checkbox_new ("Show filter"));
	lrg_checkbox_set_checked (check, lrg_property_grid_get_show_filter (app->grid));
	g_signal_connect (check, "toggled", G_CALLBACK (on_filter_toggled), app->grid);
	check = add (left, lrg_checkbox_new ("Lock selection"));
	g_signal_connect (check, "toggled", G_CALLBACK (on_lock_toggled), app->grid);

	/* Centre: the grid, scrolling */
	scroll = add (root, lrg_scroll_box_new ());
	lrg_widget_set_fill_weight (LRG_WIDGET (scroll), 1.0f);
	lrg_scroll_box_set_child (scroll, LRG_WIDGET (app->grid));

	/* Right: the undo stack */
	right = side_panel (root);
	app->status = add (right, lrg_label_new (""));
	button = add (right, lrg_button_new ("Undo (Ctrl+Z)"));
	g_signal_connect (button, "clicked", G_CALLBACK (on_undo_clicked), app);
	button = add (right, lrg_button_new ("Redo (Ctrl+Y)"));
	g_signal_connect (button, "clicked", G_CALLBACK (on_redo_clicked), app);
	add (right, lrg_label_new ("Gestures, newest first:"));
	app->log = add (right, lrg_label_new (""));

	*root_out = root;
	return LRG_WIDGET (root);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlWindow)      window = NULL;
	g_autofree gchar         *opt_mode = NULL;
	g_autofree gchar         *opt_preset = NULL;
	g_autofree gchar         *opt_filter = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_select = 3;
	gint                      opt_frames = 60;
	gboolean                  opt_license = FALSE;
	App                       app = { 0 };
	LrgHBox                  *root = NULL;
	gint                      frame = 0;
	guint                     i;

	GOptionEntry entries[] = {
		{ "mode", 'm', 0, G_OPTION_ARG_STRING, &opt_mode, "Apply mode: direct (default) or delegate", "MODE" },
		{ "select", 'n', 0, G_OPTION_ARG_INT, &opt_select, "How many enemies start selected, 1-3 (default 3)", "N" },
		{ "preset", 'p', 0, G_OPTION_ARG_STRING, &opt_preset,
		  "Theme preset (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "filter", 0, 0, G_OPTION_ARG_STRING, &opt_filter, "Show the filter field with this text", "TEXT" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 60)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- edit enemy definitions with a reflected property grid");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  ui-property-grid\n"
		"  ui-property-grid --select 1\n"
		"  ui-property-grid --mode delegate --preset catppuccin-latte\n"
		"  ui-property-grid --filter speed\n"
		"  ui-property-grid --screenshot grid.png --frames 90\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("ui-property-grid: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_mode != NULL && g_strcmp0 (opt_mode, "direct") != 0 && g_strcmp0 (opt_mode, "delegate") != 0)
	{
		g_printerr ("ui-property-grid: --mode must be direct or delegate\n");
		return 2;
	}
	opt_select = CLAMP (opt_select, 1, N_ENEMIES);

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "libregnum property grid");
	grl_window_set_target_fps (window, 60);

	if (opt_preset != NULL)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (opt_preset);

		if (theme == NULL)
		{
			g_printerr ("ui-property-grid: unknown preset '%s'\n", opt_preset);
			return 2;
		}
		lrg_theme_set_default (theme);
	}

	/* The data: three enemies that differ in a few properties */
	app.enemies[0] = enemy_new ("Grunt", 100, 3.0, 0x8f, 0xd0, 0x6a);
	app.enemies[1] = enemy_new ("Archer", 70, 4.5, 0x6a, 0xa8, 0xf0);
	app.enemies[2] = enemy_new ("Ogre", 400, 1.8, 0xd0, 0x7a, 0x50);
	g_object_set (app.enemies[1], "role", "ranged", "attack-range", 12.0, NULL);
	g_object_set (app.enemies[2], "role", "boss", "aggressive", TRUE, NULL);
	app.undo = g_ptr_array_new_with_free_func (undo_entry_free);
	app.redo = g_ptr_array_new_with_free_func (undo_entry_free);
	app.log_text = g_string_new (NULL);

	/* The grid, its hooks and the window */
	app.grid = lrg_property_grid_new ();
	lrg_property_grid_set_label_width (app.grid, 130.0f);
	if (g_strcmp0 (opt_mode, "delegate") == 0)
		lrg_property_grid_set_apply_mode (app.grid, LRG_PROPERTY_APPLY_DELEGATE);
	if (opt_filter != NULL)
	{
		lrg_property_grid_set_show_filter (app.grid, TRUE);
		lrg_property_grid_set_filter_text (app.grid, opt_filter);
	}
	lrg_property_grid_set_reference_func (app.grid, list_references, NULL, NULL);
	g_signal_connect (app.grid, "edit-begin", G_CALLBACK (on_edit_begin), &app);
	g_signal_connect (app.grid, "edit-value", G_CALLBACK (on_edit_value), &app);
	g_signal_connect (app.grid, "edit-end", G_CALLBACK (on_edit_end), &app);
	g_signal_connect (app.grid, "reset-requested", G_CALLBACK (on_reset_requested), &app);
	g_signal_connect (app.grid, "pick-requested", G_CALLBACK (on_pick_requested), &app);

	app.canvas = lrg_canvas_new ();
	lrg_canvas_set_paint_background (app.canvas, TRUE);
	lrg_container_add_child (LRG_CONTAINER (app.canvas), build_ui (&app, &root));
	for (i = 0; i < N_ENEMIES; i++)
		lrg_checkbox_set_checked (app.picks[i], (gint) i < opt_select);
	sync_selection (&app);
	update_status (&app);

	while (!grl_window_should_close (window))
	{
		gfloat w = (gfloat) GetScreenWidth ();
		gfloat h = (gfloat) GetScreenHeight ();

		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (LRG_WIDGET (root), w, h);

		/* Undo / redo keys (the canvas has no focus-independent hook
		 * for these in this small example) */
		if (IsKeyDown (KEY_LEFT_CONTROL) || IsKeyDown (KEY_RIGHT_CONTROL))
		{
			if (IsKeyPressed (KEY_Z))
				undo (&app);
			else if (IsKeyPressed (KEY_Y))
				redo (&app);
		}

		lrg_canvas_handle_input (app.canvas);

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);
		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromScreen ();

			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	/* Tear down: the grid lets go of the sources first */
	lrg_property_grid_set_locked (app.grid, FALSE);
	lrg_property_grid_set_sources (app.grid, NULL);
	g_clear_pointer (&app.pending, undo_entry_free);
	g_ptr_array_unref (app.undo);
	g_ptr_array_unref (app.redo);
	g_clear_object (&app.canvas);
	g_clear_object (&app.grid);
	for (i = 0; i < N_ENEMIES; i++)
		g_clear_object (&app.enemies[i]);
	g_string_free (app.log_text, TRUE);
	lrg_theme_set_default (NULL);
	return 0;
}
