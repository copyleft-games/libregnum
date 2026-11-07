/* lrg-editor-environment-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorEnvironmentPanel: live, undoable editing of the level's
 * `environment:` block.
 *
 * Pieces, top to bottom:
 *
 *   header    the title, a status chip (baked / needs bake / no maps),
 *             Bake and Remove chips
 *   progress  an LrgProgressBar shown while a bake runs
 *   preview   a custom widget painting a small equirectangular panorama of
 *             the sky as lrg_environment_sample_sky() sees it, with the
 *             exposure applied and a soft tone map
 *   grid      an LrgPropertyGrid over EditorEnvModel, a private GObject
 *             whose properties are the environment's fields (grouped Sky,
 *             Image Based Lighting, Ambient, Fog, Volumetric Fog, Post
 *             Processing); rows that do not apply to the current sky
 *             source or to disabled fog are hidden
 *   empty     a hint and an "Add Environment" chip when the level has no
 *             block
 *
 * Undo. The editor's command system has no "custom" command, so the panel
 * uses LRG_EDITOR_CMD_SET_PROPERTY on a tiny private GObject,
 * EditorEnvProxy, whose one property "yaml" holds a complete environment
 * block: setting it makes the level's environment equal to that block
 * (env_apply_data()). Every edit captures the block before and after
 * (lrg_environment_to_data()) and executes a set-property command
 * proxy.yaml = after with before as the undo value. A new proxy object is
 * made for every user gesture, so lrg_editor_execute_merged() folds the
 * values of one slider drag into a single step while separate edits stay
 * separate. env_apply_data() copies field by field into the live object
 * (only what differs), so a fog tweak does not mark the GPU maps dirty
 * and image sources reload only when their paths changed.
 *
 * Bake. lrg_environment_bake() needs a GL context outside BeginDrawing();
 * the panel runs it from a main-loop timer (the editor iterates the main
 * context between frames) in phases, one per tick, so the progress bar is
 * painted between them: starting -> environment (GPU maps) -> probes
 * (lrg_editor_scene_renderer_recapture_probes(); while playing also
 * lrg_world3d_renderer_capture_probes() on the play world) -> done.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-environment-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-scene-renderer.h"
#include "lrg-editor-gizmo-paint.h"
#include "data/lrg-editor-post-stack-editor.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-progress-bar.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-property-source.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include <json-glib/json-glib.h>
#include <yaml-glib.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define PANEL_KEY       "lrg-editor-environment-panel"
#define PREVIEW_COLS    (72)
#define PREVIEW_ROWS    (18)
#define PREVIEW_HEIGHT  (78.0f)
#define BAKE_TICK_MS    (16)

/* ==========================================================================
 * Fields
 * ========================================================================== */

typedef enum
{
	F_0,
	F_SKY_SOURCE,
	F_SKY_COLOR,
	F_ZENITH,
	F_HORIZON,
	F_GROUND,
	F_SKY_PATH,
	F_EXPOSURE,
	F_ROTATION,
	F_IBL_ENABLED,
	F_DIFFUSE,
	F_SPECULAR,
	F_RESOLUTION,
	F_AMBIENT_COLOR,
	F_AMBIENT_INTENSITY,
	F_FOG_ENABLED,
	F_FOG_TYPE,
	F_FOG_COLOR,
	F_FOG_DENSITY,
	F_FOG_START,
	F_FOG_END,
	F_FOG_BASE,
	F_FOG_FALLOFF,
	F_VOL_ENABLED,
	F_VOL_DENSITY,
	F_VOL_ANISOTROPY,
	F_VOL_STEPS,
	F_VOL_DISTANCE,
	F_VOL_INTENSITY,
	F_POST_STACK,
	N_FIELDS
} Field;

/* ==========================================================================
 * EditorEnvProxy: the undo target
 * ========================================================================== */

#define EDITOR_TYPE_ENV_PROXY (editor_env_proxy_get_type ())
G_DECLARE_FINAL_TYPE (EditorEnvProxy, editor_env_proxy, EDITOR, ENV_PROXY, GObject)

struct _EditorEnvProxy
{
	GObject       parent_instance;
	LrgLevel     *level;     /* owned: the level the edits belong to */
	gchar        *base_dir;  /* image references resolve against it */
	gchar        *yaml;      /* last applied block */
};

G_DEFINE_FINAL_TYPE (EditorEnvProxy, editor_env_proxy, G_TYPE_OBJECT)

enum { PROXY_PROP_0, PROXY_PROP_YAML, PROXY_N_PROPS };

static gboolean env_apply_data (LrgLevel *level, const gchar *data, const gchar *base_dir, GError **error);

static void
editor_env_proxy_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	EditorEnvProxy *self = EDITOR_ENV_PROXY (object);

	if (prop_id == PROXY_PROP_YAML)
		g_value_set_string (value, self->yaml);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

/* Undo and redo land here: make the level's environment equal @value */
static void
editor_env_proxy_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	EditorEnvProxy    *self = EDITOR_ENV_PROXY (object);
	g_autoptr(GError)  error = NULL;

	if (prop_id != PROXY_PROP_YAML)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	g_free (self->yaml);
	self->yaml = g_value_dup_string (value);
	if (self->level != NULL && !env_apply_data (self->level, self->yaml, self->base_dir, &error))
		g_warning ("environment: %s", error->message);
}

static void
editor_env_proxy_finalize (GObject *object)
{
	EditorEnvProxy *self = EDITOR_ENV_PROXY (object);

	g_clear_object (&self->level);
	g_free (self->base_dir);
	g_free (self->yaml);
	G_OBJECT_CLASS (editor_env_proxy_parent_class)->finalize (object);
}

static void
editor_env_proxy_class_init (EditorEnvProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = editor_env_proxy_get_property;
	object_class->set_property = editor_env_proxy_set_property;
	object_class->finalize = editor_env_proxy_finalize;
	g_object_class_install_property (object_class, PROXY_PROP_YAML,
		g_param_spec_string ("yaml", "Environment", "The environment block", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
}

static void
editor_env_proxy_init (EditorEnvProxy *self)
{
}

/* ==========================================================================
 * EditorEnvModel: the environment as GObject properties for the grid
 * ========================================================================== */

#define EDITOR_TYPE_ENV_MODEL (editor_env_model_get_type ())
G_DECLARE_FINAL_TYPE (EditorEnvModel, editor_env_model, EDITOR, ENV_MODEL, GObject)

struct _EditorEnvModel
{
	GObject                    parent_instance;
	LrgEditorEnvironmentPanel *panel;   /* weak */
};

G_DEFINE_FINAL_TYPE (EditorEnvModel, editor_env_model, G_TYPE_OBJECT)

static GParamSpec *model_props[N_FIELDS];

/* ==========================================================================
 * The panel
 * ========================================================================== */

struct _LrgEditorEnvironmentPanel
{
	LrgContainer         parent_instance;

	LrgEditorApp        *app;          /* weak */
	LrgEditor           *editor;       /* owned */
	LrgLevel            *level;        /* weak: the level whose environment we watch */
	LrgEnvironment      *env;          /* weak: its environment, signals held */
	LrgEnvironment      *defaults;     /* owned: shown greyed when there is no block */
	gulong               env_changed_id;

	/* Widgets */
	LrgWidget           *top;          /* header + progress + preview */
	LrgWidget           *title;
	LrgWidget           *header_row;
	LrgWidget           *status;       /* custom pill */
	LrgWidget           *bake_chip;
	LrgWidget           *remove_chip;
	LrgProgressBar      *progress;
	LrgWidget           *preview;
	LrgWidget           *scroll;
	LrgPropertyGrid     *grid;
	LrgWidget           *empty;
	EditorEnvModel      *model;
	gchar               *hidden_sig;   /* which rows the current source hides */

	/* Undo gestures */
	EditorEnvProxy      *gesture_proxy;
	guint                gesture_id;   /* the grid gesture the proxy belongs to */
	gboolean             in_gesture;
	gboolean             applying;

	/* Preview (sRGB 0..255, PREVIEW_COLS x PREVIEW_ROWS) */
	guint8               preview_rgb[PREVIEW_COLS * PREVIEW_ROWS * 3];
	gboolean             preview_dirty;

	/* Bake */
	gboolean             baking;
	gint                 bake_phase;   /* 0 idle 1 starting 2 environment 3 probes 4 done 5 failed */
	gdouble              bake_progress;
	gchar               *bake_error;
	guint                bake_source;
	guint                bake_probes;
	gint64               bake_started;
};

G_DEFINE_FINAL_TYPE (LrgEditorEnvironmentPanel, lrg_editor_environment_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorEnvironmentPanel *self);
static void watch_level (LrgEditorEnvironmentPanel *self);

/* ==========================================================================
 * Colour helpers
 * ========================================================================== */

static guint8
to_byte (gfloat v)
{
	return (guint8) lroundf (CLAMP (v, 0.0f, 1.0f) * 255.0f);
}

static void
color_to_value (const gfloat *rgb,
                gfloat        alpha,
                GValue       *value)
{
	g_value_init (value, GRL_TYPE_COLOR);
	g_value_take_boxed (value, grl_color_new (to_byte (rgb[0]), to_byte (rgb[1]), to_byte (rgb[2]), to_byte (alpha)));
}

/* A colour value (GrlColor, or "#rrggbb[aa]" text) as floats. */
static gboolean
value_to_color (const GValue *value,
                gfloat       *rgba)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_COLOR) && g_value_get_boxed (value) != NULL)
	{
		const GrlColor *c = g_value_get_boxed (value);

		rgba[0] = (gfloat) c->r / 255.0f;
		rgba[1] = (gfloat) c->g / 255.0f;
		rgba[2] = (gfloat) c->b / 255.0f;
		rgba[3] = (gfloat) c->a / 255.0f;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GValue) tmp = G_VALUE_INIT;

		g_value_init (&tmp, GRL_TYPE_COLOR);
		if (lrg_property_value_from_string (&tmp, g_value_get_string (value)))
			return value_to_color (&tmp, rgba);
	}
	return FALSE;
}

/* @value converted to a double (numbers and booleans). */
static gboolean
value_to_double (const GValue *value,
                 gdouble      *out)
{
	g_auto(GValue) d = G_VALUE_INIT;

	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE) || !g_value_transform (value, &d))
		return FALSE;
	*out = g_value_get_double (&d);
	return TRUE;
}

static gboolean
value_to_bool (const GValue *value,
               gboolean     *out)
{
	gdouble d;

	if (G_VALUE_HOLDS_BOOLEAN (value))
	{
		*out = g_value_get_boolean (value);
		return TRUE;
	}
	if (!value_to_double (value, &d))
		return FALSE;
	*out = d != 0.0;
	return TRUE;
}

/* An enum value by number or nick ("equirect", "linear"). */
static gboolean
value_to_enum (const GValue *value,
               GType         enum_type,
               gint         *out)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (enum_type);
	GEnumValue           *ev = NULL;
	gdouble               d;

	if (G_VALUE_HOLDS (value, enum_type))
		ev = g_enum_get_value (klass, g_value_get_enum (value));
	else if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_autofree gchar *nick = g_strdelimit (g_strdup (g_value_get_string (value)), "_", '-');

		ev = g_enum_get_value_by_nick (klass, nick);
		if (ev == NULL)
			ev = g_enum_get_value_by_name (klass, g_value_get_string (value));
	}
	else if (value_to_double (value, &d))
		ev = g_enum_get_value (klass, (gint) d);
	if (ev == NULL)
		return FALSE;
	*out = ev->value;
	return TRUE;
}

/* ==========================================================================
 * Reading and writing one field
 * ========================================================================== */

/* The value of @field in @env, in the type of the field's pspec. */
static void
field_get (LrgEnvironment *env,
           Field           field,
           GValue         *value)
{
	gfloat     rgb[3], z[3], h[3], g[3], rgba[4], d, s, e, a, b;
	guint      steps;
	LrgFogType type;
	gboolean   on;

	switch (field)
	{
	case F_SKY_SOURCE:
		g_value_init (value, LRG_TYPE_ENVIRONMENT_SKY_SOURCE);
		g_value_set_enum (value, (gint) lrg_environment_get_sky_source (env));
		break;
	case F_SKY_COLOR:
		lrg_environment_get_sky_color (env, rgb);
		color_to_value (rgb, 1.0f, value);
		break;
	case F_ZENITH:
	case F_HORIZON:
	case F_GROUND:
		lrg_environment_get_gradient (env, z, h, g);
		color_to_value (field == F_ZENITH ? z : field == F_HORIZON ? h : g, 1.0f, value);
		break;
	case F_SKY_PATH:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_environment_get_sky_path (env));
		break;
	case F_EXPOSURE:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_environment_get_exposure (env));
		break;
	case F_ROTATION:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_environment_get_rotation (env));
		break;
	case F_IBL_ENABLED:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_environment_get_ibl_enabled (env));
		break;
	case F_DIFFUSE:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_environment_get_diffuse_intensity (env));
		break;
	case F_SPECULAR:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_environment_get_specular_intensity (env));
		break;
	case F_RESOLUTION:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, (gint) lrg_environment_get_resolution (env));
		break;
	case F_AMBIENT_COLOR:
		lrg_environment_get_ambient (env, rgb, NULL);
		color_to_value (rgb, 1.0f, value);
		break;
	case F_AMBIENT_INTENSITY:
		lrg_environment_get_ambient (env, NULL, &a);
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, a);
		break;
	case F_FOG_ENABLED:
	case F_FOG_TYPE:
	case F_FOG_COLOR:
	case F_FOG_DENSITY:
	case F_FOG_START:
	case F_FOG_END:
		on = lrg_environment_get_fog (env, &type, rgba, &d, &s, &e);
		if (field == F_FOG_ENABLED)
		{
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, on);
		}
		else if (field == F_FOG_TYPE)
		{
			g_value_init (value, LRG_TYPE_FOG_TYPE);
			g_value_set_enum (value, (gint) type);
		}
		else if (field == F_FOG_COLOR)
			color_to_value (rgba, rgba[3], value);
		else
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, field == F_FOG_DENSITY ? d : field == F_FOG_START ? s : e);
		}
		break;
	case F_FOG_BASE:
	case F_FOG_FALLOFF:
		lrg_environment_get_fog_height (env, &a, &b);
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, field == F_FOG_BASE ? a : b);
		break;
	case F_VOL_ENABLED:
	case F_VOL_DENSITY:
	case F_VOL_ANISOTROPY:
	case F_VOL_STEPS:
	case F_VOL_DISTANCE:
	case F_VOL_INTENSITY:
		on = lrg_environment_get_volumetric (env, &d, &a, &steps, &e, &s);
		if (field == F_VOL_ENABLED)
		{
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, on);
		}
		else if (field == F_VOL_STEPS)
		{
			g_value_init (value, G_TYPE_INT);
			g_value_set_int (value, (gint) steps);
		}
		else
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, field == F_VOL_DENSITY ? d : field == F_VOL_ANISOTROPY ? a :
			                           field == F_VOL_DISTANCE ? e : s);
		}
		break;
	case F_POST_STACK:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_environment_get_post_stack (env));
		break;
	case F_0:
	case N_FIELDS:
	default:
		g_value_init (value, G_TYPE_INT);
		break;
	}
}

/* Writes @value into @field of @env. Fields that share a setter (fog,
 * volumetric, ambient, gradient) read the others back first. */
static gboolean
field_set (LrgEnvironment *env,
           Field           field,
           const GValue   *value)
{
	gfloat     rgb[3], z[3], h[3], g[3], rgba[4], c[4], d, s, e, a, b;
	gdouble    num = 0.0;
	gboolean   flag = FALSE;
	gint       ev = 0;
	guint      steps;
	LrgFogType type;
	gboolean   on;

	switch (field)
	{
	case F_SKY_SOURCE:
		if (!value_to_enum (value, LRG_TYPE_ENVIRONMENT_SKY_SOURCE, &ev))
			return FALSE;
		lrg_environment_set_sky_source (env, (LrgEnvironmentSkySource) ev);
		return TRUE;
	case F_SKY_COLOR:
		if (!value_to_color (value, c))
			return FALSE;
		lrg_environment_set_sky_color (env, c);
		return TRUE;
	case F_ZENITH:
	case F_HORIZON:
	case F_GROUND:
		if (!value_to_color (value, c))
			return FALSE;
		lrg_environment_get_gradient (env, z, h, g);
		memcpy (field == F_ZENITH ? z : field == F_HORIZON ? h : g, c, sizeof z);
		lrg_environment_set_gradient (env, z, h, g);
		return TRUE;
	case F_SKY_PATH:
	case F_POST_STACK:
		{
			const gchar *str = G_VALUE_HOLDS_STRING (value) ? g_value_get_string (value) : NULL;

			if (!G_VALUE_HOLDS_STRING (value))
				return FALSE;
			if (str != NULL && *str == '\0')
				str = NULL;
			if (field == F_SKY_PATH)
				lrg_environment_set_sky_path (env, str);
			else
				lrg_environment_set_post_stack (env, str);
			return TRUE;
		}
	case F_IBL_ENABLED:
		if (!value_to_bool (value, &flag))
			return FALSE;
		lrg_environment_set_ibl_enabled (env, flag);
		return TRUE;
	case F_FOG_ENABLED:
	case F_VOL_ENABLED:
		if (!value_to_bool (value, &flag))
			return FALSE;
		break;
	case F_FOG_TYPE:
		if (!value_to_enum (value, LRG_TYPE_FOG_TYPE, &ev))
			return FALSE;
		break;
	case F_AMBIENT_COLOR:
	case F_FOG_COLOR:
		if (!value_to_color (value, c))
			return FALSE;
		break;
	default:
		if (!value_to_double (value, &num))
			return FALSE;
		break;
	}

	/* The numeric fields and the shared setters */
	switch (field)
	{
	case F_EXPOSURE:
		lrg_environment_set_exposure (env, (gfloat) num);
		break;
	case F_ROTATION:
		lrg_environment_set_rotation (env, (gfloat) num);
		break;
	case F_DIFFUSE:
		lrg_environment_set_diffuse_intensity (env, (gfloat) MAX (num, 0.0));
		break;
	case F_SPECULAR:
		lrg_environment_set_specular_intensity (env, (gfloat) MAX (num, 0.0));
		break;
	case F_RESOLUTION:
		lrg_environment_set_resolution (env, (guint) CLAMP (num, 1.0, (gdouble) LRG_ENVIRONMENT_MAX_RESOLUTION));
		break;
	case F_AMBIENT_COLOR:
	case F_AMBIENT_INTENSITY:
		lrg_environment_get_ambient (env, rgb, &a);
		if (field == F_AMBIENT_COLOR)
			memcpy (rgb, c, sizeof rgb);
		else
			a = (gfloat) MAX (num, 0.0);
		lrg_environment_set_ambient (env, rgb, a);
		break;
	case F_FOG_ENABLED:
	case F_FOG_TYPE:
	case F_FOG_COLOR:
	case F_FOG_DENSITY:
	case F_FOG_START:
	case F_FOG_END:
		on = lrg_environment_get_fog (env, &type, rgba, &d, &s, &e);
		if (field == F_FOG_ENABLED)
			on = flag;
		else if (field == F_FOG_TYPE)
			type = (LrgFogType) ev;
		else if (field == F_FOG_COLOR)
			memcpy (rgba, c, sizeof rgba);
		else if (field == F_FOG_DENSITY)
			d = (gfloat) MAX (num, 0.0);
		else if (field == F_FOG_START)
			s = (gfloat) num;
		else
			e = (gfloat) num;
		lrg_environment_set_fog (env, on, type, rgba, d, s, e);
		break;
	case F_FOG_BASE:
	case F_FOG_FALLOFF:
		lrg_environment_get_fog_height (env, &a, &b);
		if (field == F_FOG_BASE)
			a = (gfloat) num;
		else
			b = (gfloat) MAX (num, 0.0001);
		lrg_environment_set_fog_height (env, a, b);
		break;
	case F_VOL_ENABLED:
	case F_VOL_DENSITY:
	case F_VOL_ANISOTROPY:
	case F_VOL_STEPS:
	case F_VOL_DISTANCE:
	case F_VOL_INTENSITY:
		on = lrg_environment_get_volumetric (env, &d, &a, &steps, &e, &s);
		if (field == F_VOL_ENABLED)
			on = flag;
		else if (field == F_VOL_DENSITY)
			d = (gfloat) MAX (num, 0.00001);
		else if (field == F_VOL_ANISOTROPY)
			a = (gfloat) CLAMP (num, -0.95, 0.95);
		else if (field == F_VOL_STEPS)
			steps = (guint) CLAMP (num, 4.0, 128.0);
		else if (field == F_VOL_DISTANCE)
			e = (gfloat) MAX (num, 0.01);
		else
			s = (gfloat) MAX (num, 0.0);
		lrg_environment_set_volumetric (env, on, d, a, steps, e, s);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static Field
field_from_name (const gchar *name)
{
	guint i;

	/* the model class fills model_props[] */
	g_type_class_unref (g_type_class_ref (EDITOR_TYPE_ENV_MODEL));
	if (name == NULL)
		return F_0;
	for (i = 1; i < N_FIELDS; i++)
		if (model_props[i] != NULL && g_strcmp0 (model_props[i]->name, name) == 0)
			return (Field) i;
	return F_0;
}

/* ==========================================================================
 * Applying a whole block (undo, redo, IPC)
 * ========================================================================== */

static gboolean
colors_equal (const gfloat *a,
              const gfloat *b,
              guint         n)
{
	guint i;

	for (i = 0; i < n; i++)
		if (fabsf (a[i] - b[i]) > 1e-6f)
			return FALSE;
	return TRUE;
}

/* Copies every editable field of @src into @dst, touching only what
 * differs (so unrelated GPU maps stay valid). Returns whether an image
 * reference changed (the sources must be loaded again). */
static gboolean
env_copy_fields (LrgEnvironment *dst,
                 LrgEnvironment *src)
{
	gboolean reload = FALSE;
	guint    i;
	Field    f;

	if (lrg_environment_get_sky_source (dst) != lrg_environment_get_sky_source (src))
	{
		lrg_environment_set_sky_source (dst, lrg_environment_get_sky_source (src));
		reload = TRUE;
	}
	if (g_strcmp0 (lrg_environment_get_sky_path (dst), lrg_environment_get_sky_path (src)) != 0)
	{
		lrg_environment_set_sky_path (dst, lrg_environment_get_sky_path (src));
		reload = TRUE;
	}
	for (i = 0; i < 6; i++)
	{
		if (g_strcmp0 (lrg_environment_get_sky_face_path (dst, i), lrg_environment_get_sky_face_path (src, i)) != 0)
		{
			lrg_environment_set_sky_face_path (dst, i, lrg_environment_get_sky_face_path (src, i));
			reload = TRUE;
		}
	}
	{
		gfloat a[3], b[3], za[3], ha[3], ga[3], zb[3], hb[3], gb[3];

		lrg_environment_get_sky_color (dst, a);
		lrg_environment_get_sky_color (src, b);
		if (!colors_equal (a, b, 3))
			lrg_environment_set_sky_color (dst, b);
		lrg_environment_get_gradient (dst, za, ha, ga);
		lrg_environment_get_gradient (src, zb, hb, gb);
		if (!colors_equal (za, zb, 3) || !colors_equal (ha, hb, 3) || !colors_equal (ga, gb, 3))
			lrg_environment_set_gradient (dst, zb, hb, gb);
	}
	if (lrg_environment_get_resolution (dst) != lrg_environment_get_resolution (src))
		lrg_environment_set_resolution (dst, lrg_environment_get_resolution (src));

	/* The rest compares as values through the field table */
	for (f = F_EXPOSURE; f < N_FIELDS; f++)
	{
		g_auto(GValue) va = G_VALUE_INIT;
		g_auto(GValue) vb = G_VALUE_INIT;

		if (f == F_RESOLUTION)
			continue;
		field_get (dst, f, &va);
		field_get (src, f, &vb);
		if (!lrg_property_values_equal (&va, &vb))
			field_set (dst, f, &vb);
	}
	return reload;
}

/* Makes @level's environment equal the block @data ("" or NULL removes
 * it). The live object is kept and updated in place. */
static gboolean
env_apply_data (LrgLevel     *level,
                const gchar  *data,
                const gchar  *base_dir,
                GError      **error)
{
	g_autoptr(LrgEnvironment) parsed = NULL;
	LrgEnvironment           *live;
	gboolean                  reload;

	if (data == NULL || *data == '\0')
	{
		lrg_level_set_environment (level, NULL);
		return TRUE;
	}
	parsed = lrg_environment_new ();
	if (!lrg_environment_load_from_data (parsed, data, -1, error))
		return FALSE;

	live = lrg_level_get_environment (level);
	if (live == NULL)
	{
		lrg_level_set_environment (level, parsed);
		live = parsed;
		reload = lrg_environment_get_sky_path (live) != NULL;
	}
	else
	{
		g_autofree gchar *current = lrg_environment_to_data (live);

		if (g_strcmp0 (current, data) == 0)
			return TRUE;
		reload = env_copy_fields (live, parsed);
	}

	/* Image skies load their pixels (a missing image is a warning, the
	 * bake falls back to the gradient) */
	if (reload && (lrg_environment_get_sky_source (live) == LRG_ENVIRONMENT_SKY_EQUIRECT ||
	               lrg_environment_get_sky_source (live) == LRG_ENVIRONMENT_SKY_CUBEMAP))
	{
		g_autoptr(GError) load_error = NULL;

		if (!lrg_environment_load_sources (live, NULL, base_dir, &load_error))
			g_message ("environment: %s", load_error->message);
	}
	return TRUE;
}

/* ==========================================================================
 * Model properties
 * ========================================================================== */

static LrgEnvironment *
shown_env (LrgEditorEnvironmentPanel *self)
{
	LrgEnvironment *env = lrg_editor_environment_panel_get_environment (self);

	return env != NULL ? env : self->defaults;
}

static void
editor_env_model_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	EditorEnvModel *self = EDITOR_ENV_MODEL (object);
	g_auto(GValue)  v = G_VALUE_INIT;

	if (prop_id == F_0 || prop_id >= N_FIELDS || self->panel == NULL)
	{
		g_param_value_set_default (pspec, value);
		return;
	}
	field_get (shown_env (self->panel), (Field) prop_id, &v);
	if (!g_value_transform (&v, value))
		g_param_value_set_default (pspec, value);
}

static void
editor_env_model_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	EditorEnvModel *self = EDITOR_ENV_MODEL (object);

	if (self->panel != NULL && !self->panel->applying)
		lrg_editor_environment_panel_set_field (self->panel, pspec->name, value);
}

static void
editor_env_model_finalize (GObject *object)
{
	EditorEnvModel *self = EDITOR_ENV_MODEL (object);

	g_clear_weak_pointer (&self->panel);
	G_OBJECT_CLASS (editor_env_model_parent_class)->finalize (object);
}

/* One model property: label, group, hint and tooltip. */
static void
add_prop (GObjectClass *klass,
          Field         field,
          GParamSpec   *pspec,
          const gchar  *group,
          const gchar  *hint)
{
	if (group != NULL)
		g_param_spec_set_qdata (pspec, lrg_property_group_quark (), (gpointer) group);
	if (hint != NULL)
		g_param_spec_set_qdata (pspec, lrg_property_hint_quark (), (gpointer) hint);
	model_props[field] = pspec;
	g_object_class_install_property (klass, field, pspec);
}

static void
editor_env_model_class_init (EditorEnvModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->get_property = editor_env_model_get_property;
	object_class->set_property = editor_env_model_set_property;
	object_class->finalize = editor_env_model_finalize;

	/* Sky */
	add_prop (object_class, F_SKY_SOURCE,
	          g_param_spec_enum ("sky-source", "Source", "Where the sky light comes from",
	                             LRG_TYPE_ENVIRONMENT_SKY_SOURCE, LRG_ENVIRONMENT_SKY_GRADIENT, rw), "Sky", NULL);
	add_prop (object_class, F_SKY_COLOR,
	          g_param_spec_boxed ("sky-color", "Color", "The one colour of a colour sky", GRL_TYPE_COLOR, rw),
	          "Sky", "color");
	add_prop (object_class, F_ZENITH,
	          g_param_spec_boxed ("zenith", "Zenith", "Colour straight up", GRL_TYPE_COLOR, rw), "Sky", "color");
	add_prop (object_class, F_HORIZON,
	          g_param_spec_boxed ("horizon", "Horizon", "Colour at the horizon", GRL_TYPE_COLOR, rw), "Sky", "color");
	add_prop (object_class, F_GROUND,
	          g_param_spec_boxed ("ground", "Ground", "Colour straight down", GRL_TYPE_COLOR, rw), "Sky", "color");
	add_prop (object_class, F_SKY_PATH,
	          g_param_spec_string ("sky-path", "Image", "Equirect panorama (.hdr) or cubemap cross, path or guid:",
	                               NULL, rw), "Sky", "asset:texture");
	add_prop (object_class, F_EXPOSURE,
	          g_param_spec_double ("exposure", "Exposure", "Exposure compensation in stops (EV)",
	                               -16.0, 16.0, 0.0, rw), "Sky", "range:-8,8,0.05");
	add_prop (object_class, F_ROTATION,
	          g_param_spec_double ("rotation", "Rotation", "Turn of the sky about +Y, in degrees",
	                               -360.0, 360.0, 0.0, rw), "Sky", "angle:degrees");

	/* Image based lighting */
	add_prop (object_class, F_IBL_ENABLED,
	          g_param_spec_boolean ("ibl-enabled", "Enabled", "Light the scene with the sky (else flat ambient)",
	                                TRUE, rw), "Image Based Lighting", NULL);
	add_prop (object_class, F_DIFFUSE,
	          g_param_spec_double ("diffuse-intensity", "Diffuse", "Multiplier of the irradiance (diffuse) light",
	                               0.0, 100.0, 1.0, rw), "Image Based Lighting", "range:0,4,0.01");
	add_prop (object_class, F_SPECULAR,
	          g_param_spec_double ("specular-intensity", "Specular", "Multiplier of the reflections",
	                               0.0, 100.0, 1.0, rw), "Image Based Lighting", "range:0,4,0.01");
	add_prop (object_class, F_RESOLUTION,
	          g_param_spec_int ("resolution", "Resolution", "Cubemap edge of the baked maps, in pixels",
	                            16, LRG_ENVIRONMENT_MAX_RESOLUTION, LRG_ENVIRONMENT_DEFAULT_RESOLUTION, rw),
	          "Image Based Lighting", "range:16,1024,16");

	/* Ambient */
	add_prop (object_class, F_AMBIENT_COLOR,
	          g_param_spec_boxed ("ambient-color", "Color", "Flat ambient, used when IBL is off or not baked",
	                              GRL_TYPE_COLOR, rw), "Ambient", "color");
	add_prop (object_class, F_AMBIENT_INTENSITY,
	          g_param_spec_double ("ambient-intensity", "Intensity", "Multiplier of the flat ambient",
	                               0.0, 100.0, 1.0, rw), "Ambient", "range:0,4,0.01");

	/* Fog */
	add_prop (object_class, F_FOG_ENABLED,
	          g_param_spec_boolean ("fog-enabled", "Enabled", "Distance fog", FALSE, rw), "Fog", NULL);
	add_prop (object_class, F_FOG_TYPE,
	          g_param_spec_enum ("fog-type", "Model", "The fog model", LRG_TYPE_FOG_TYPE, 0, rw), "Fog", NULL);
	add_prop (object_class, F_FOG_COLOR,
	          g_param_spec_boxed ("fog-color", "Color", "Fog colour; alpha caps how opaque it gets",
	                              GRL_TYPE_COLOR, rw), "Fog", "color");
	add_prop (object_class, F_FOG_DENSITY,
	          g_param_spec_double ("fog-density", "Density", "Exponential / height density",
	                               0.0, 10.0, 0.02, rw), "Fog", "range:0,0.5,0.001");
	add_prop (object_class, F_FOG_START,
	          g_param_spec_double ("fog-start", "Start", "Linear fog start distance",
	                               0.0, 100000.0, 10.0, rw), "Fog", "range:0,1000,0.5");
	add_prop (object_class, F_FOG_END,
	          g_param_spec_double ("fog-end", "End", "Linear fog end distance",
	                               0.0, 100000.0, 100.0, rw), "Fog", "range:0,5000,1");
	add_prop (object_class, F_FOG_BASE,
	          g_param_spec_double ("fog-height-base", "Height", "World height below which height fog is full",
	                               -100000.0, 100000.0, 0.0, rw), "Fog", "range:-100,100,0.1");
	add_prop (object_class, F_FOG_FALLOFF,
	          g_param_spec_double ("fog-height-falloff", "Falloff", "How fast height fog thins, per unit",
	                               0.0001, 100.0, 0.1, rw), "Fog", "range:0.001,2,0.001");

	/* Volumetric fog */
	add_prop (object_class, F_VOL_ENABLED,
	          g_param_spec_boolean ("volumetric-enabled", "Enabled", "Ray-marched light shafts", FALSE, rw),
	          "Volumetric Fog", NULL);
	add_prop (object_class, F_VOL_DENSITY,
	          g_param_spec_double ("volumetric-density", "Density", "Extinction per unit at the base height",
	                               0.00001, 10.0, 0.02, rw), "Volumetric Fog", "range:0.0001,0.5,0.0005");
	add_prop (object_class, F_VOL_ANISOTROPY,
	          g_param_spec_double ("volumetric-anisotropy", "Anisotropy", "Forward scattering (Henyey-Greenstein g)",
	                               -0.95, 0.95, 0.6, rw), "Volumetric Fog", "range:-0.95,0.95,0.01");
	add_prop (object_class, F_VOL_STEPS,
	          g_param_spec_int ("volumetric-steps", "Steps", "Ray-march steps per pixel", 4, 128, 32, rw),
	          "Volumetric Fog", "range:4,128,1");
	add_prop (object_class, F_VOL_DISTANCE,
	          g_param_spec_double ("volumetric-max-distance", "Distance", "How far the march reaches",
	                               0.01, 100000.0, 100.0, rw), "Volumetric Fog", "range:1,1000,1");
	add_prop (object_class, F_VOL_INTENSITY,
	          g_param_spec_double ("volumetric-intensity", "Intensity", "Multiplier of the in-scattered light",
	                               0.0, 100.0, 1.0, rw), "Volumetric Fog", "range:0,8,0.01");

	/* Post processing */
	add_prop (object_class, F_POST_STACK,
	          g_param_spec_string ("post-stack", "Post Stack", "The level's .rpost look, path or guid:", NULL, rw),
	          "Post Processing", "asset:post-stack");
}

static void
editor_env_model_init (EditorEnvModel *self)
{
}

/* ==========================================================================
 * Commands
 * ========================================================================== */

static LrgLevel *
current_level (LrgEditorEnvironmentPanel *self)
{
	LrgLevel *level = (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;

	return level;
}

static EditorEnvProxy *
new_proxy (LrgEditorEnvironmentPanel *self,
           LrgLevel                  *level)
{
	EditorEnvProxy *proxy = g_object_new (EDITOR_TYPE_ENV_PROXY, NULL);

	proxy->level = g_object_ref (level);
	proxy->base_dir = (self->app != NULL) ? g_strdup (editor_app_base_dir (self->app)) : NULL;
	return proxy;
}

/* Records "the environment went from @before to @after" (already applied)
 * as one command; @merge folds it into the gesture's previous step. */
static void
commit (LrgEditorEnvironmentPanel *self,
        LrgLevel                  *level,
        const gchar               *before,
        const gchar               *after,
        const gchar               *label,
        gboolean                   merge)
{
	g_auto(GValue)     vb = G_VALUE_INIT;
	g_auto(GValue)     va = G_VALUE_INIT;
	EditorEnvProxy    *proxy;
	LrgEditorCommand  *cmd;

	if (g_strcmp0 (before, after) == 0)
		return;
	if (merge && self->gesture_proxy != NULL && self->gesture_proxy->level == level)
		proxy = g_object_ref (self->gesture_proxy);
	else
	{
		proxy = new_proxy (self, level);
		if (merge)
			g_set_object (&self->gesture_proxy, proxy);
	}

	g_value_init (&vb, G_TYPE_STRING);
	g_value_set_string (&vb, before != NULL ? before : "");
	g_value_init (&va, G_TYPE_STRING);
	g_value_set_string (&va, after != NULL ? after : "");
	cmd = lrg_editor_command_new_set_property (G_OBJECT (proxy), "yaml", &vb, &va);
	lrg_editor_command_set_label (cmd, label != NULL ? label : "Edit Environment");

	/* The level already holds @after; applying again is a no-op */
	self->applying = TRUE;
	if (merge)
		lrg_editor_execute_merged (self->editor, cmd);
	else
		lrg_editor_execute (self->editor, cmd);
	self->applying = FALSE;
	g_object_unref (proxy);
}

static gchar *
env_data (LrgLevel *level)
{
	LrgEnvironment *env = (level != NULL) ? lrg_level_get_environment (level) : NULL;

	return (env != NULL) ? lrg_environment_to_data (env) : g_strdup ("");
}

/* ==========================================================================
 * Public API: editing
 * ========================================================================== */

LrgEnvironment *
lrg_editor_environment_panel_get_environment (LrgEditorEnvironmentPanel *self)
{
	LrgLevel *level;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), NULL);

	level = current_level (self);
	return (level != NULL) ? lrg_level_get_environment (level) : NULL;
}

LrgPropertyGrid *
lrg_editor_environment_panel_get_grid (LrgEditorEnvironmentPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), NULL);
	return self->grid;
}

gboolean
lrg_editor_environment_panel_add_environment (LrgEditorEnvironmentPanel *self)
{
	g_autoptr(LrgEnvironment) env = NULL;
	g_autofree gchar         *after = NULL;
	LrgLevel                 *level;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);

	level = current_level (self);
	if (level == NULL || lrg_level_get_environment (level) != NULL)
		return FALSE;
	env = lrg_environment_new ();
	after = lrg_environment_to_data (env);
	lrg_level_set_environment (level, env);
	commit (self, level, "", after, "Add Environment", FALSE);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Added an environment to the level");
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_environment_panel_remove_environment (LrgEditorEnvironmentPanel *self)
{
	g_autofree gchar *before = NULL;
	LrgLevel         *level;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);

	level = current_level (self);
	if (level == NULL || lrg_level_get_environment (level) == NULL)
		return FALSE;
	before = env_data (level);
	lrg_level_set_environment (level, NULL);
	commit (self, level, before, "", "Remove Environment", FALSE);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Removed the level's environment");
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_environment_panel_set_field (LrgEditorEnvironmentPanel *self,
                                        const gchar               *name,
                                        const GValue              *value)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	g_autofree gchar *label = NULL;
	LrgLevel         *level;
	LrgEnvironment   *env;
	Field             field;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	/* the model class owns the field names */
	g_type_class_unref (g_type_class_ref (EDITOR_TYPE_ENV_MODEL));
	field = field_from_name (name);
	level = current_level (self);
	if (field == F_0 || level == NULL)
		return FALSE;

	/* No block yet: the first edit creates it in the same undo step */
	env = lrg_level_get_environment (level);
	before = env_data (level);
	if (env == NULL)
		env = lrg_level_ensure_environment (level);
	if (!field_set (env, field, value))
	{
		if (*before == '\0')
			lrg_level_set_environment (level, NULL);
		return FALSE;
	}
	after = lrg_environment_to_data (env);

	/* An image path change loads the image now */
	if (field == F_SKY_PATH || field == F_SKY_SOURCE)
	{
		g_autoptr(GError) error = NULL;
		LrgEnvironmentSkySource src = lrg_environment_get_sky_source (env);

		if ((src == LRG_ENVIRONMENT_SKY_EQUIRECT || src == LRG_ENVIRONMENT_SKY_CUBEMAP) &&
		    lrg_environment_get_sky_path (env) != NULL &&
		    !lrg_environment_load_sources (env, NULL, self->app != NULL ? editor_app_base_dir (self->app) : NULL,
		                                   &error) && self->app != NULL)
			editor_app_set_status (self->app, "Sky image: %s", error->message);
	}

	label = g_strdup_printf ("Environment: %s", g_param_spec_get_nick (model_props[field]));
	commit (self, level, before, after, label, self->in_gesture);
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_environment_panel_apply_data (LrgEditorEnvironmentPanel  *self,
                                         const gchar                *data,
                                         const gchar                *label,
                                         GError                    **error)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	LrgLevel         *level;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	level = current_level (self);
	if (level == NULL)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_FAILED, "No level is open");
		return FALSE;
	}
	before = env_data (level);
	if (!env_apply_data (level, data, self->app != NULL ? editor_app_base_dir (self->app) : NULL, error))
		return FALSE;
	after = env_data (level);
	commit (self, level, before, after, label, FALSE);
	sync_widgets (self);
	return TRUE;
}

/* ==========================================================================
 * Grid gestures
 * ========================================================================== */

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);

	/* a fresh undo target per gesture: its values merge into one step */
	g_clear_object (&self->gesture_proxy);
	self->gesture_id = gesture_id;
	self->in_gesture = TRUE;
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);

	if (gesture_id == self->gesture_id)
	{
		self->in_gesture = FALSE;
		g_clear_object (&self->gesture_proxy);
	}
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

void
lrg_editor_environment_panel_sample_preview (LrgEditorEnvironmentPanel *self,
                                             gfloat                     u,
                                             gfloat                     v,
                                             gfloat                    *out_rgba)
{
	LrgEnvironment *env;
	gfloat          dir[3], rgb[3], azimuth, elevation, scale;
	guint           i;

	g_return_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self));
	g_return_if_fail (out_rgba != NULL);

	env = lrg_editor_environment_panel_get_environment (self);
	if (env == NULL)
		env = self->defaults;

	/* (u, v) -> a direction: u turns about +Y from -Z, v from up to down */
	azimuth = (u - 0.5f) * 2.0f * (gfloat) G_PI;
	elevation = (0.5f - v) * (gfloat) G_PI;
	dir[0] = sinf (azimuth) * cosf (elevation);
	dir[1] = sinf (elevation);
	dir[2] = -cosf (azimuth) * cosf (elevation);
	lrg_environment_sample_sky (env, dir, rgb);

	/* exposure in stops, a gentle Reinhard and the sRGB curve */
	scale = powf (2.0f, lrg_environment_get_exposure (env));
	for (i = 0; i < 3; i++)
	{
		gfloat c = MAX (rgb[i], 0.0f) * scale;

		c = c / (1.0f + c * 0.25f);
		out_rgba[i] = CLAMP (powf (c, 1.0f / 2.2f), 0.0f, 1.0f);
	}
	out_rgba[3] = 1.0f;
}

static void
update_preview (LrgEditorEnvironmentPanel *self)
{
	guint x, y;

	for (y = 0; y < PREVIEW_ROWS; y++)
	{
		for (x = 0; x < PREVIEW_COLS; x++)
		{
			gfloat  rgba[4];
			guint8 *p = &self->preview_rgb[(y * PREVIEW_COLS + x) * 3];

			lrg_editor_environment_panel_sample_preview (self, ((gfloat) x + 0.5f) / (gfloat) PREVIEW_COLS,
			                                             ((gfloat) y + 0.5f) / (gfloat) PREVIEW_ROWS, rgba);
			p[0] = to_byte (rgba[0]);
			p[1] = to_byte (rgba[1]);
			p[2] = to_byte (rgba[2]);
		}
	}
	self->preview_dirty = FALSE;
}

static void
preview_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	size->width = 120.0f;
	size->height = PREVIEW_HEIGHT;
}

/* The panorama: rounded, one rect per cell, a horizon line, compass
 * ticks and an exposure chip; dimmed when the level has no block. */
static void
preview_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);
	LrgTheme                  *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgEnvironment            *env = lrg_editor_environment_panel_get_environment (self);
	gfloat                     radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) + 2.0f;
	gfloat                     cw = width / (gfloat) PREVIEW_COLS;
	gfloat                     ch = height / (gfloat) PREVIEW_ROWS;
	GrlColor                   ink;
	GrlColor                   line;
	g_autofree gchar          *chip = NULL;
	static const gchar * const compass[] = { "S", "W", "N", "E" };
	guint                      cx, cy, i;

	if (self->preview_dirty)
		update_preview (self);

	lrg_paint_list_shadow (list, x, y, width, height, radius, 10.0f, 0.0f, 0.0f, 3.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
	lrg_paint_list_push_clip (list, x, y, width, height);
	if (env == NULL)
		lrg_paint_list_push_opacity (list, 0.35f);
	for (cy = 0; cy < PREVIEW_ROWS; cy++)
	{
		for (cx = 0; cx < PREVIEW_COLS; cx++)
		{
			const guint8 *p = &self->preview_rgb[(cy * PREVIEW_COLS + cx) * 3];
			GrlColor      c = { p[0], p[1], p[2], 255 };

			/* cells overlap by a pixel so no seams show */
			lrg_paint_list_rect (list, x + (gfloat) cx * cw, y + (gfloat) cy * ch, cw + 1.0f, ch + 1.0f, 0.0f, &c);
		}
	}
	if (env == NULL)
		lrg_paint_list_pop_opacity (list);

	/* Horizon line and compass ticks */
	line.r = 255; line.g = 255; line.b = 255; line.a = 70;
	lrg_paint_list_line (list, x, y + height * 0.5f, x + width, y + height * 0.5f, 1.0f, &line);
	ink.r = 255; ink.g = 255; ink.b = 255; ink.a = 190;
	for (i = 0; i < 4; i++)
	{
		gfloat tx = x + width * ((gfloat) i * 0.25f + 0.125f);

		lrg_paint_list_line (list, tx, y + height * 0.5f - 4.0f, tx, y + height * 0.5f + 4.0f, 1.0f, &line);
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), compass[i], roundf (tx - 3.0f),
		                     roundf (y + height * 0.5f + 5.0f), 10.0f, 0.0f, &ink);
	}
	lrg_paint_list_pop_clip (list);
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	/* What the strip shows */
	if (env != NULL)
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_ENVIRONMENT_SKY_SOURCE);
		GEnumValue           *ev = g_enum_get_value (klass, (gint) lrg_environment_get_sky_source (env));

		chip = g_strdup_printf ("%s  %+.1f EV", ev != NULL ? ev->value_nick : "?",
		                        (gdouble) lrg_environment_get_exposure (env));
	}
	else
		chip = g_strdup ("no environment");
	lrg_editor_gizmo_paint_chip (list, theme, chip, x + 6.0f, y + 6.0f, FALSE, NULL);
}

/* ==========================================================================
 * Status pill
 * ========================================================================== */

static const gchar *
status_text (LrgEditorEnvironmentPanel *self,
             LrgThemeColor             *role)
{
	LrgEnvironment *env = lrg_editor_environment_panel_get_environment (self);

	if (self->baking)
	{
		*role = LRG_THEME_COLOR_ACCENT;
		return "Baking";
	}
	if (env == NULL)
	{
		*role = LRG_THEME_COLOR_TEXT_MUTED;
		return "None";
	}
	if (self->bake_phase == 5)
	{
		*role = LRG_THEME_COLOR_DANGER;
		return "Bake failed";
	}
	if (lrg_environment_is_baked (env) && !lrg_environment_needs_bake (env))
	{
		*role = LRG_THEME_COLOR_SUCCESS;
		return "Baked";
	}
	*role = LRG_THEME_COLOR_WARNING;
	return lrg_environment_is_baked (env) ? "Needs bake" : "Not baked";
}

static void
status_measure (LrgCustomWidget     *widget,
                gfloat               avail_width,
                gfloat               avail_height,
                LrgCustomWidgetSize *size,
                gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));

	size->width = 64.0f;
	size->height = MAX (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT) - 6.0f, 18.0f);
}

static void
status_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);
	LrgTheme                  *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgThemeColor              role = LRG_THEME_COLOR_TEXT_MUTED;
	const gchar               *text = status_text (self, &role);
	GrlColor                   fill = *lrg_theme_get_color (theme, role);
	GrlColor                   ink = fill;
	gfloat                     fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                     tw = 0.0f, th = 0.0f;

	fill.a = 46;
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, fs, 0.0f, &tw, &th);
	width = MIN (width, tw + height + 14.0f);
	lrg_paint_list_rect (list, x, y, width, height, height * 0.5f, &fill);
	lrg_paint_list_circle (list, x + height * 0.5f + 2.0f, y + height * 0.5f, 3.0f, &ink);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, roundf (x + height * 0.5f + 9.0f),
	                     roundf (y + (height - th) * 0.5f), fs, 0.0f, &ink);
}

/* ==========================================================================
 * Bake
 * ========================================================================== */

static const gchar * const phase_names[] = { "idle", "starting", "environment", "probes", "done", "failed" };

static void
bake_finish (LrgEditorEnvironmentPanel *self,
             const gchar               *error)
{
	self->baking = FALSE;
	g_free (self->bake_error);
	self->bake_error = g_strdup (error);
	self->bake_phase = (error != NULL) ? 5 : 4;
	self->bake_progress = (error != NULL) ? self->bake_progress : 1.0;
	if (self->app != NULL)
	{
		if (error != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Bake failed: %s", error);
		else
			editor_app_notify (self->app, EDITOR_NOTIFY_SUCCESS,
			                   "Environment baked in %.0f ms; reflection probes recapture with the next frame",
			                   (gdouble) (g_get_monotonic_time () - self->bake_started) / 1000.0);
	}
	sync_widgets (self);
}

gboolean
lrg_editor_environment_panel_step_bake (LrgEditorEnvironmentPanel *self)
{
	LrgEnvironment *env;

	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);

	if (!self->baking)
		return FALSE;
	env = lrg_editor_environment_panel_get_environment (self);
	if (env == NULL)
	{
		bake_finish (self, "the level has no environment");
		return FALSE;
	}

	switch (self->bake_phase)
	{
	case 1:
		/* the bar was painted at "starting"; the GPU maps next */
		self->bake_phase = 2;
		self->bake_progress = 0.15;
		break;
	case 2:
		{
			g_autoptr(GError) error = NULL;

			if (!lrg_environment_bake (env, &error))
			{
				bake_finish (self, error->message);
				return FALSE;
			}
			self->bake_phase = 3;
			self->bake_progress = 0.7;
		}
		break;
	case 3:
		{
			LrgWorld3DRenderer *rt = NULL;

			/* While playing the runtime world's probes are captured
			 * now; in edit mode the viewport recaptures the probe
			 * nodes before its next frame */
			self->bake_probes = lrg_environment_get_n_probes (env);
			if (self->app != NULL && self->app->renderer != NULL)
			{
				lrg_editor_scene_renderer_recapture_probes (self->app->renderer);
				rt = lrg_editor_scene_renderer_get_runtime_renderer (self->app->renderer);
			}
			if (rt != NULL && self->app != NULL && lrg_editor_play_session_is_playing (self->app->play))
				lrg_world3d_renderer_capture_probes (rt, lrg_editor_play_session_get_world3d (self->app->play));
			bake_finish (self, NULL);
			return FALSE;
		}
	default:
		bake_finish (self, NULL);
		return FALSE;
	}
	sync_widgets (self);
	return TRUE;
}

static gboolean
bake_timer (gpointer user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);

	if (lrg_editor_environment_panel_step_bake (self))
		return G_SOURCE_CONTINUE;
	self->bake_source = 0;
	return G_SOURCE_REMOVE;
}

gboolean
lrg_editor_environment_panel_bake (LrgEditorEnvironmentPanel  *self,
                                   GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->baking)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_FAILED, "A bake is already running");
		return FALSE;
	}
	if (lrg_editor_environment_panel_get_environment (self) == NULL)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_FAILED,
		             "The level has no environment (add one first)");
		return FALSE;
	}
	if (self->app == NULL || !IsWindowReady ())
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "Baking needs the editor window (GPU); headless editors cannot bake");
		return FALSE;
	}

	self->baking = TRUE;
	self->bake_phase = 1;
	self->bake_progress = 0.05;
	self->bake_probes = 0;
	self->bake_started = g_get_monotonic_time ();
	g_clear_pointer (&self->bake_error, g_free);
	if (self->bake_source == 0)
		self->bake_source = g_timeout_add (BAKE_TICK_MS, bake_timer, self);
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_environment_panel_get_baking (LrgEditorEnvironmentPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), FALSE);
	return self->baking;
}

gdouble
lrg_editor_environment_panel_get_bake_progress (LrgEditorEnvironmentPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), 0.0);
	return self->bake_progress;
}

const gchar *
lrg_editor_environment_panel_get_bake_phase (LrgEditorEnvironmentPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), "idle");
	return phase_names[CLAMP (self->bake_phase, 0, 5)];
}

const gchar *
lrg_editor_environment_panel_get_bake_error (LrgEditorEnvironmentPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ENVIRONMENT_PANEL (self), NULL);
	return self->bake_error;
}

/* ==========================================================================
 * Widgets <- model
 * ========================================================================== */

/* The rows the current settings hide, as a signature string. */
static gchar *
hidden_rows (LrgEnvironment *env)
{
	GString                *s = g_string_new ("");
	LrgEnvironmentSkySource src = lrg_environment_get_sky_source (env);
	LrgFogType              type;
	gboolean                fog = lrg_environment_get_fog (env, &type, NULL, NULL, NULL, NULL);
	gboolean                vol = lrg_environment_get_volumetric (env, NULL, NULL, NULL, NULL, NULL);

	if (src != LRG_ENVIRONMENT_SKY_COLOR)
		g_string_append (s, "sky-color ");
	if (src != LRG_ENVIRONMENT_SKY_GRADIENT)
		g_string_append (s, "zenith horizon ground ");
	if (src != LRG_ENVIRONMENT_SKY_EQUIRECT && src != LRG_ENVIRONMENT_SKY_CUBEMAP)
		g_string_append (s, "sky-path ");
	if (!fog)
		g_string_append (s, "fog-type fog-color fog-density fog-start fog-end fog-height-base fog-height-falloff ");
	else if (type == LRG_FOG_TYPE_LINEAR)
		g_string_append (s, "fog-density fog-height-base fog-height-falloff ");
	else
		g_string_append (s, "fog-start fog-end ");
	if (!vol)
		g_string_append (s, "volumetric-density volumetric-anisotropy volumetric-steps volumetric-max-distance "
		                    "volumetric-intensity ");
	return g_string_free (s, FALSE);
}

static void
rebuild_source (LrgEditorEnvironmentPanel *self,
                const gchar               *hidden)
{
	g_autoptr(LrgObjectPropertySource) src = lrg_object_property_source_new (G_OBJECT (self->model));
	g_auto(GStrv)                      names = g_strsplit (hidden, " ", -1);
	guint                              i;

	for (i = 0; names[i] != NULL; i++)
		if (*names[i] != '\0')
			lrg_object_property_source_hide_property (src, names[i]);
	g_free (self->hidden_sig);
	self->hidden_sig = g_strdup (hidden);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (src));
}

static void
sync_widgets (LrgEditorEnvironmentPanel *self)
{
	LrgEnvironment   *env = lrg_editor_environment_panel_get_environment (self);
	g_autofree gchar *hidden = hidden_rows (env != NULL ? env : self->defaults);
	gboolean          has = env != NULL;

	watch_level (self);
	self->preview_dirty = TRUE;

	/* rows hidden by the source / fog switches need a new source */
	if (g_strcmp0 (hidden, self->hidden_sig) != 0)
		rebuild_source (self, hidden);
	else
		lrg_property_grid_refresh (self->grid);

	lrg_widget_set_visible (self->scroll, has);
	lrg_widget_set_visible (self->empty, !has && current_level (self) != NULL);
	lrg_widget_set_visible (self->remove_chip, has);
	lrg_widget_set_enabled (self->bake_chip, has && !self->baking);
	lrg_widget_set_visible (LRG_WIDGET (self->progress), self->baking || self->bake_phase == 5);
	lrg_progress_bar_set_value (self->progress, self->bake_progress);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Level and environment signals
 * ========================================================================== */

static void
on_env_changed (LrgEnvironment *env,
                gpointer        user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);

	self->preview_dirty = TRUE;
	if (!self->applying)
		lrg_property_grid_refresh (self->grid);
}

/* Follows the current level's environment object for change signals. */
static void
watch_level (LrgEditorEnvironmentPanel *self)
{
	LrgLevel       *level = current_level (self);
	LrgEnvironment *env = (level != NULL) ? lrg_level_get_environment (level) : NULL;

	if (level != self->level)
		g_set_weak_pointer (&self->level, level);
	if (env == self->env)
		return;
	if (self->env != NULL)
		g_clear_signal_handler (&self->env_changed_id, self->env);
	g_set_weak_pointer (&self->env, env);
	if (env != NULL)
		self->env_changed_id = g_signal_connect (env, "changed", G_CALLBACK (on_env_changed), self);
}

static void
on_editor_changed (LrgEditorEnvironmentPanel *self)
{
	if (!self->applying)
		sync_widgets (self);
}

static void
on_level_replaced (LrgEditorEnvironmentPanel *self)
{
	g_clear_object (&self->gesture_proxy);
	if (self->baking)
		bake_finish (self, "the level was replaced");
	sync_widgets (self);
}

/* ==========================================================================
 * Chips
 * ========================================================================== */

static void
on_bake_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (user_data);
	g_autoptr(GError)          error = NULL;

	if (!lrg_editor_environment_panel_bake (self, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
}

static void
on_remove_chip (LrgWidget *chip,
                gpointer   user_data)
{
	lrg_editor_environment_panel_remove_environment (LRG_EDITOR_ENVIRONMENT_PANEL (user_data));
}

static void
on_add_chip (LrgWidget *chip,
             gpointer   user_data)
{
	lrg_editor_environment_panel_add_environment (LRG_EDITOR_ENVIRONMENT_PANEL (user_data));
}

/* ==========================================================================
 * IPC: environment.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* The environment block as JSON (YAML -> document -> JSON). */
static JsonNode *
env_json (LrgEnvironment *env)
{
	g_autofree gchar     *yaml = lrg_environment_to_data (env);
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	YamlDocument         *doc;

	if (!yaml_parser_load_from_data (parser, yaml, -1, NULL))
		return json_node_new (JSON_NODE_NULL);
	doc = yaml_parser_get_document (parser, 0);
	return (doc != NULL) ? yaml_document_to_json_node (doc) : json_node_new (JSON_NODE_NULL);
}

static JsonNode *
env_state (LrgEditorEnvironmentPanel *self)
{
	LrgEnvironment *env = lrg_editor_environment_panel_get_environment (self);
	JsonObject     *o = json_object_new ();

	json_object_set_boolean_member (o, "present", env != NULL);
	if (env != NULL)
	{
		json_object_set_member (o, "environment", env_json (env));
		json_object_set_boolean_member (o, "baked", lrg_environment_is_baked (env));
		json_object_set_boolean_member (o, "needs_bake", lrg_environment_needs_bake (env));
	}
	else
	{
		json_object_set_null_member (o, "environment");
		json_object_set_boolean_member (o, "baked", FALSE);
		json_object_set_boolean_member (o, "needs_bake", FALSE);
	}
	return obj_node (o);
}

static LrgEditorEnvironmentPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorEnvironmentPanel *self = lrg_editor_environment_panel_find (LRG_EDITOR_APP (user_data));

	if (self == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No environment panel");
	return self;
}

static JsonNode *
m_env_get (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorEnvironmentPanel *self = ipc_panel (user_data, error);

	return (self != NULL) ? env_state (self) : NULL;
}

/* Sets @node at the dotted @key inside @root (objects created on the way). */
static gboolean
json_set_path (JsonObject  *root,
               const gchar *key,
               JsonNode    *node)
{
	g_auto(GStrv) parts = g_strsplit (key, ".", -1);
	JsonObject   *o = root;
	guint         i;

	for (i = 0; parts[i] != NULL && parts[i + 1] != NULL; i++)
	{
		JsonNode *child = json_object_get_member (o, parts[i]);

		if (child == NULL || !JSON_NODE_HOLDS_OBJECT (child))
		{
			json_object_set_object_member (o, parts[i], json_object_new ());
			child = json_object_get_member (o, parts[i]);
		}
		o = json_node_get_object (child);
	}
	if (parts[i] == NULL || *parts[i] == '\0')
		return FALSE;
	json_object_set_member (o, parts[i], node);
	return TRUE;
}

/* {"key": "fog.density", "value": 0.05} or {"values": {"fog.enabled": true, ...}} */
static JsonNode *
m_env_set (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorEnvironmentPanel *self = ipc_panel (user_data, error);
	LrgEnvironment            *env;
	g_autoptr(LrgEnvironment)  fresh = NULL;
	g_autoptr(JsonNode)        root = NULL;
	g_autoptr(JsonGenerator)   gen = NULL;
	g_autofree gchar          *text = NULL;
	g_autoptr(GError)          local = NULL;
	JsonObject                *obj;
	guint                      n = 0;

	if (self == NULL)
		return NULL;
	if (current_level (self) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level is open");
		return NULL;
	}

	/* A panel field name ("exposure", "fog-density", "sky-color") sets
	 * that field like the grid does; colours as [r, g, b(, a)] 0..1 */
	if (params != NULL && json_object_has_member (params, "key") && json_object_has_member (params, "value") &&
	    field_from_name (json_object_get_string_member (params, "key")) != F_0)
	{
		const gchar   *key = json_object_get_string_member (params, "key");
		JsonNode      *jv = json_object_get_member (params, "value");
		g_auto(GValue) v = G_VALUE_INIT;

		if (JSON_NODE_HOLDS_ARRAY (jv))
		{
			JsonArray *a = json_node_get_array (jv);
			gfloat     c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			guint      i;

			for (i = 0; i < MIN (json_array_get_length (a), 4u); i++)
				c[i] = (gfloat) json_array_get_double_element (a, i);
			color_to_value (c, c[3], &v);
		}
		else if (JSON_NODE_HOLDS_VALUE (jv))
			json_node_get_value (jv, &v);
		if (!G_IS_VALUE (&v) || !lrg_editor_environment_panel_set_field (self, key, &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Bad value for %s", key);
			return NULL;
		}
		return env_state (self);
	}

	env = lrg_editor_environment_panel_get_environment (self);
	if (env == NULL)
		env = fresh = lrg_environment_new ();
	root = env_json (env);
	if (!JSON_NODE_HOLDS_OBJECT (root))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "Environment not readable");
		return NULL;
	}
	obj = json_node_get_object (root);

	/* Patch the JSON form of the block */
	if (params != NULL && json_object_has_member (params, "values") &&
	    JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		JsonObject *values = json_object_get_object_member (params, "values");
		GList      *keys = json_object_get_members (values);
		GList      *l;

		for (l = keys; l != NULL; l = l->next)
			if (json_set_path (obj, l->data, json_node_copy (json_object_get_member (values, l->data))))
				n++;
		g_list_free (keys);
	}
	else if (params != NULL && json_object_has_member (params, "key") && json_object_has_member (params, "value"))
	{
		const gchar *key = json_object_get_string_member (params, "key");

		if (key != NULL && json_set_path (obj, key, json_node_copy (json_object_get_member (params, "value"))))
			n++;
	}
	if (n == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Give \"key\" and \"value\", or \"values\": {key: value}");
		return NULL;
	}

	/* JSON is YAML: hand the patched block to the panel as one step */
	gen = json_generator_new ();
	json_generator_set_root (gen, root);
	text = json_generator_to_data (gen, NULL);
	if (!lrg_editor_environment_panel_apply_data (self, text, "Edit Environment", &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
		return NULL;
	}
	return env_state (self);
}

static JsonNode *
m_env_reset (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorEnvironmentPanel *self = ipc_panel (user_data, error);

	if (self == NULL)
		return NULL;
	lrg_editor_environment_panel_remove_environment (self);
	return env_state (self);
}

static JsonNode *
m_env_bake (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorEnvironmentPanel *self = ipc_panel (user_data, error);
	g_autoptr(GError)          local = NULL;
	JsonObject                *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_environment_panel_bake (self, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "started", TRUE);
	return obj_node (o);
}

static JsonNode *
m_env_progress (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorEnvironmentPanel *self = ipc_panel (user_data, error);
	LrgEnvironment            *env;
	JsonObject                *o;

	if (self == NULL)
		return NULL;
	env = lrg_editor_environment_panel_get_environment (self);
	o = json_object_new ();
	json_object_set_boolean_member (o, "baking", self->baking);
	json_object_set_string_member (o, "phase", lrg_editor_environment_panel_get_bake_phase (self));
	json_object_set_double_member (o, "progress", self->bake_progress);
	if (self->bake_error != NULL)
		json_object_set_string_member (o, "error", self->bake_error);
	else
		json_object_set_null_member (o, "error");
	json_object_set_boolean_member (o, "baked", env != NULL && lrg_environment_is_baked (env));
	json_object_set_int_member (o, "probes", self->bake_probes);
	return obj_node (o);
}

static JsonNode *
m_env_show (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	editor_app_show_panel (app, LRG_EDITOR_ENVIRONMENT_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "panel", LRG_EDITOR_ENVIRONMENT_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_ENVIRONMENT_PANEL_ID));
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_ENVIRONMENT_PANEL_ID);
}

static void
a_add (gpointer data)
{
	lrg_editor_environment_panel_add_environment (LRG_EDITOR_ENVIRONMENT_PANEL (data));
}

static void
a_remove (gpointer data)
{
	lrg_editor_environment_panel_remove_environment (LRG_EDITOR_ENVIRONMENT_PANEL (data));
}

static void
a_bake (gpointer data)
{
	on_bake_chip (NULL, data);
}

static gboolean
can_add (gpointer data)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (data);

	return current_level (self) != NULL && lrg_editor_environment_panel_get_environment (self) == NULL;
}

static gboolean
has_env (gpointer data)
{
	return lrg_editor_environment_panel_get_environment (LRG_EDITOR_ENVIRONMENT_PANEL (data)) != NULL;
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static void
lrg_editor_environment_panel_layout_children (LrgContainer *container)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (container);

	/* narrow docks: the chips drop their labels before anything clips */
	editor_panel_fit_toolbar (self->header_row, lrg_widget_get_width (LRG_WIDGET (self)) - 20.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_environment_panel_dispose (GObject *object)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (object);

	g_clear_handle_id (&self->bake_source, g_source_remove);
	if (self->env != NULL)
		g_clear_signal_handler (&self->env_changed_id, self->env);
	g_clear_weak_pointer (&self->env);
	g_clear_weak_pointer (&self->level);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->editor);
	g_clear_weak_pointer (&self->app);
	g_clear_object (&self->gesture_proxy);
	g_clear_object (&self->model);
	g_clear_object (&self->defaults);
	G_OBJECT_CLASS (lrg_editor_environment_panel_parent_class)->dispose (object);
}

static void
lrg_editor_environment_panel_finalize (GObject *object)
{
	LrgEditorEnvironmentPanel *self = LRG_EDITOR_ENVIRONMENT_PANEL (object);

	g_free (self->hidden_sig);
	g_free (self->bake_error);
	G_OBJECT_CLASS (lrg_editor_environment_panel_parent_class)->finalize (object);
}

static void
lrg_editor_environment_panel_class_init (LrgEditorEnvironmentPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_environment_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_environment_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_environment_panel_layout_children;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_environment_panel_init (LrgEditorEnvironmentPanel *self)
{
	static const LrgCustomWidgetFuncs preview_funcs = { preview_measure, preview_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs status_funcs = { status_measure, status_paint, NULL, NULL, { NULL } };
	const LrgInsets                   pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	LrgWidget                        *row;
	LrgWidget                        *hint;

	self->defaults = lrg_environment_new ();
	self->preview_dirty = TRUE;
	self->model = g_object_new (EDITOR_TYPE_ENV_MODEL, NULL);
	g_set_weak_pointer (&self->model->panel, self);

	/* Header: title, status, Bake, Remove */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "environment-top");
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 6.0f);
	lrg_widget_set_margin (self->top, &pad);

	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "environment-header");
	self->header_row = row;
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->title = LRG_WIDGET (lrg_label_new ("Environment"));
	lrg_widget_set_name (self->title, "environment-title");
	lrg_widget_add_class (self->title, "heading");
	lrg_widget_set_valign (self->title, LRG_ALIGN_CENTER);
	add_to (row, self->title);
	self->status = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->status, "environment-status");
	lrg_widget_set_valign (self->status, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (self->status, 1.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->status), &status_funcs, self, NULL);
	add_to (row, self->status);
	self->bake_chip = editor_chip_new ("environment-bake", LRG_ICON_REFRESH, "Bake", on_bake_chip, self);
	editor_chip_set_tint (self->bake_chip, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->bake_chip);
	self->remove_chip = editor_chip_new ("environment-remove", LRG_ICON_TRASH, NULL, on_remove_chip, self);
	editor_chip_set_tint (self->remove_chip, LRG_THEME_COLOR_DANGER);
	add_to (row, self->remove_chip);
	add_to (self->top, row);

	self->progress = lrg_progress_bar_new ();
	lrg_widget_set_name (LRG_WIDGET (self->progress), "environment-progress");
	lrg_progress_bar_set_max (self->progress, 1.0);
	lrg_widget_set_visible (LRG_WIDGET (self->progress), FALSE);
	add_to (self->top, LRG_WIDGET (self->progress));

	self->preview = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->preview, "environment-preview");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), &preview_funcs, self, NULL);
	add_to (self->top, self->preview);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* The fields */
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "environment-grid");
	lrg_property_grid_set_label_width (self->grid, 104.0f);
	g_signal_connect (self->grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (self->grid, "edit-end", G_CALLBACK (on_edit_end), self);
	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "environment-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "environment-empty");
	lrg_container_set_spacing (LRG_CONTAINER (self->empty), 10.0f);
	lrg_widget_set_margin (self->empty, &pad);
	hint = editor_panel_hint_new ("environment-hint",
	                              "This level has no environment block. Add one to give it a sky, image based "
	                              "lighting, fog and a post-processing look that the game uses too.");
	add_to (self->empty, hint);
	row = editor_chip_new ("environment-add", LRG_ICON_PLUS, "Add Environment", on_add_chip, self);
	editor_chip_set_tint (row, LRG_THEME_COLOR_ACCENT);
	lrg_widget_set_halign (row, LRG_ALIGN_CENTER);
	add_to (self->empty, row);
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);
}

LrgEditorEnvironmentPanel *
lrg_editor_environment_panel_new (LrgEditorApp *app)
{
	LrgEditorEnvironmentPanel *self;
	LrgEditorActions          *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_ENVIRONMENT_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "environment-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "environment.show", "Environment Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "environment.add", "Add Environment", "Level", NULL, a_add, can_add, NULL, self);
	lrg_editor_actions_add (actions, "environment.remove", "Remove Environment", "Level", NULL, a_remove, has_env,
	                        NULL, self);
	lrg_editor_actions_add (actions, "environment.bake", "Bake Environment", "Level", NULL, a_bake, has_env, NULL,
	                        self);
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "environment.get", m_env_get, app, NULL);
		lrg_editor_server_register_method (app->server, "environment.set", m_env_set, app, NULL);
		lrg_editor_server_register_method (app->server, "environment.reset", m_env_reset, app, NULL);
		lrg_editor_server_register_method (app->server, "environment.bake", m_env_bake, app, NULL);
		lrg_editor_server_register_method (app->server, "environment.progress", m_env_progress, app, NULL);
		lrg_editor_server_register_method (app->server, "environment.show", m_env_show, app, NULL);
	}
	lrg_editor_post_stack_editor_install (app);

	sync_widgets (self);
	return self;
}

LrgEditorEnvironmentPanel *
lrg_editor_environment_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
