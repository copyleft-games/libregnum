/* lrg-editor-nav-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNavPanel: the lrgeditor "Navigation" panel (0.9.0 M6/M7).
 *
 * Pieces, top to bottom:
 *
 *   header    title, a status pill (not baked / baked / unsaved / baking /
 *             failed / playing) and the Bake / Cancel, Save, overlay and
 *             Clear chips
 *   progress  an indeterminate LrgProgressBar and a phase line while an
 *             asynchronous bake runs (the voxel bake has no progress)
 *   summary   polygons / regions / links / milliseconds of the shown mesh
 *   agents    (playing) the play crowd's agents: state, remaining distance,
 *             speed; a row click makes it the crowd's debug agent
 *   settings  the .rnav picker (every .rnav under the project) and New
 *   grid      the settings form (an EditorNavForm through an
 *             LrgObjectPropertySource): Level, Agent, Voxels, Regions,
 *             Sources, Output
 *   areas     the area types (swatch, name, tag, cost, boxes) with the
 *             selected area's form and its box list / box form
 *   links     the off-mesh links with the selected link's form
 *
 * Undo.  The panel's whole state is one string (nav_state_capture()): the
 * level's `navigation:` block (settings ref, data ref, bake on load) and
 * the .rnav text of the in-memory settings (lrg_nav_bake_to_data()).
 * Every edit changes the live objects, then records "state went from
 * before to after" as one set-property command on a tiny private object,
 * EditorNavProxy, whose "state" property applies such a string
 * (nav_state_apply(): the level block is rewritten, the settings are
 * re-parsed when their text differs).  A property-grid gesture merges into
 * one step (lrg_editor_execute_merged()), a viewport handle drag commits
 * once at the release.  The baked mesh is not part of the undo state (it
 * is a result, like the lighting bake); its `data:` reference is.
 *
 * Bake.  bake_prepare() instantiates the level into a private LrgWorld3D
 * with the level's directory as base and every mesh asset resolved to an
 * absolute path (the nav bake reads glTF headless, so the worker never
 * touches the renderer's GPU models), the level's own navigation block
 * switched off for the instantiation (no bake on load inside the bake).
 * A copy of the settings bakes it, on the calling thread
 * (lrg_editor_nav_panel_bake(): IPC, tests) or a GTask worker (the Bake
 * button); cancel just drops the worker's result.
 *
 * Overlay.  The panel connects to the shared runtime renderer's
 * "draw-overlay" signal, which fires inside the 3D pass of every viewport
 * render (authoring and play), and draws an LrgDebugDraw batch it records
 * right there (record_overlay()).  The handles of the selected area box
 * and link are 2D: the viewport calls lrg_editor_nav_panel_paint_handles()
 * and the lrg_editor_nav_panel_handle_*() functions from its own paint and
 * pointer code.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-nav-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-scene-renderer.h"
#include "lrg-editor-viewport-split.h"
#include "lrg-editor-viewport-panel.h"
#include "lrg-editor-gizmo-paint.h"
#include "../lrg-editor.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-command.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-actions.h"
#include "../lrg-editor-play-session.h"
#include "../lrg-editor-view-state.h"
#include "../lrg-asset-database.h"
#include "../../level/lrg-level.h"
#include "../../level/lrg-level-instantiate.h"
#include "../../world3d/lrg-world3d.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../../world3d/lrg-entity3d.h"
#include "../../world3d/components/lrg-mesh-renderer-component.h"
#include "../../world3d/components/lrg-nav-agent-component.h"
#include "../../ecs/lrg-component.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-expander.h"
#include "../../ui/lrg-progress-bar.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-property-source.h"
#include "../../paint/lrg-paint-icons.h"
#include <json-glib/json-glib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define PANEL_KEY        "lrg-editor-nav-panel"
#define STATE_SEPARATOR  "\n--rnav--\n"
#define LIST_ROW_H       (24.0f)
#define LIST_BTN_W       (22.0f)
#define SUMMARY_HEIGHT   (58.0f)
#define HANDLE_RADIUS    (6.0f)
#define HANDLE_HIT       (10.0f)
#define SNAP_STEP        (0.25f)
#define SCAN_DEPTH       (5)
#define NO_AREA          "(none)"

/* the overlay flags a non-empty overlay starts with */
#define OVERLAY_DEFAULT  (LRG_NAV_DEBUG_DEFAULT)
/* the flags lrg_nav_poly_mesh_debug_draw() understands */
#define MESH_FLAGS       (LRG_NAV_DEBUG_POLYGONS | LRG_NAV_DEBUG_POLYGON_EDGES | LRG_NAV_DEBUG_BOUNDARIES | \
                          LRG_NAV_DEBUG_PORTALS | LRG_NAV_DEBUG_LINKS | LRG_NAV_DEBUG_AREAS)

/* ==========================================================================
 * LrgEditorNavHandle
 * ========================================================================== */

GType
lrg_editor_nav_handle_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_NAV_HANDLE_NONE, "LRG_EDITOR_NAV_HANDLE_NONE", "none" },
			{ LRG_EDITOR_NAV_HANDLE_BOX_MIN, "LRG_EDITOR_NAV_HANDLE_BOX_MIN", "box-min" },
			{ LRG_EDITOR_NAV_HANDLE_BOX_MAX, "LRG_EDITOR_NAV_HANDLE_BOX_MAX", "box-max" },
			{ LRG_EDITOR_NAV_HANDLE_BOX_CENTER, "LRG_EDITOR_NAV_HANDLE_BOX_CENTER", "box-center" },
			{ LRG_EDITOR_NAV_HANDLE_LINK_START, "LRG_EDITOR_NAV_HANDLE_LINK_START", "link-start" },
			{ LRG_EDITOR_NAV_HANDLE_LINK_END, "LRG_EDITOR_NAV_HANDLE_LINK_END", "link-end" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorNavHandle"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

/* "agent_radius" -> "agent-radius" (newly allocated). */
static gchar *
canon_key (const gchar *key)
{
	return g_strdelimit (g_strdup (key), "_", '-');
}

/*
 * The colour the overlay gives area @tag with LRG_NAV_DEBUG_AREAS (the
 * engine's golden-ratio palette of lrg_nav_poly_mesh_debug_draw(), at
 * index tag + 7), so the list swatches match the viewport.
 */
static GrlColor
area_color (guint32 tag,
            guint8  alpha)
{
	gfloat   h = fmodf ((gfloat) (tag + 7u) * 0.618034f, 1.0f) * 6.0f;
	gfloat   s = 0.55f;
	gfloat   v = 0.95f;
	gint     sector = (gint) h;
	gfloat   f = h - (gfloat) sector;
	gfloat   p = v * (1.0f - s);
	gfloat   q = v * (1.0f - s * f);
	gfloat   t = v * (1.0f - s * (1.0f - f));
	gfloat   r, g, b;
	GrlColor c;

	switch (sector)
	{
	case 0:  r = v; g = t; b = p; break;
	case 1:  r = q; g = v; b = p; break;
	case 2:  r = p; g = v; b = t; break;
	case 3:  r = p; g = q; b = v; break;
	case 4:  r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	c.r = (guint8) (r * 255.0f);
	c.g = (guint8) (g * 255.0f);
	c.b = (guint8) (b * 255.0f);
	c.a = alpha;
	return c;
}

static GrlVector3
v3 (const gfloat *p)
{
	GrlVector3 v;

	v.x = p[0];
	v.y = p[1];
	v.z = p[2];
	return v;
}

static GrlColor
rgba (guint8 r,
      guint8 g,
      guint8 b,
      guint8 a)
{
	GrlColor c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

/* An agent state's nick ("on-link"). */
static const gchar *
agent_state_nick (LrgNavAgentState state)
{
	switch (state)
	{
	case LRG_NAV_AGENT_STATE_IDLE:    return "idle";
	case LRG_NAV_AGENT_STATE_MOVING:  return "moving";
	case LRG_NAV_AGENT_STATE_ON_LINK: return "on-link";
	case LRG_NAV_AGENT_STATE_ARRIVED: return "arrived";
	case LRG_NAV_AGENT_STATE_FAILED:  return "failed";
	default:                          return "unknown";
	}
}

/* The colour lrg_nav_crowd_debug_draw() gives an agent in @state. */
static GrlColor
agent_state_color (LrgNavAgentState state)
{
	switch (state)
	{
	case LRG_NAV_AGENT_STATE_MOVING:  return rgba (80, 150, 255, 255);
	case LRG_NAV_AGENT_STATE_ON_LINK: return rgba (60, 220, 230, 255);
	case LRG_NAV_AGENT_STATE_ARRIVED: return rgba (90, 210, 110, 255);
	case LRG_NAV_AGENT_STATE_FAILED:  return rgba (235, 80, 80, 255);
	case LRG_NAV_AGENT_STATE_IDLE:
	default:                          return rgba (160, 160, 170, 255);
	}
}

/* ==========================================================================
 * Values (shared by the form, set_field and IPC)
 * ========================================================================== */

/* A number from any numeric, boolean or numeric-string value. */
static gboolean
value_as_double (const GValue *value,
                 gdouble      *out)
{
	g_auto(GValue) d = G_VALUE_INIT;

	if (value == NULL)
		return FALSE;
	if (G_VALUE_HOLDS_STRING (value))
	{
		const gchar *s = g_value_get_string (value);
		gchar       *end = NULL;

		if (s == NULL || *s == '\0')
			return FALSE;
		*out = g_ascii_strtod (s, &end);
		return end != NULL && end != s;
	}
	if (G_VALUE_HOLDS_BOOLEAN (value))
	{
		*out = g_value_get_boolean (value) ? 1.0 : 0.0;
		return TRUE;
	}
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE) || !g_value_transform (value, &d))
		return FALSE;
	*out = g_value_get_double (&d);
	return TRUE;
}

static gboolean
value_as_bool (const GValue *value,
               gboolean     *out)
{
	gdouble d;

	if (value != NULL && G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		const gchar *s = g_value_get_string (value);

		if (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0 || g_strcmp0 (s, "1") == 0)
		{
			*out = TRUE;
			return TRUE;
		}
		if (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0 || g_strcmp0 (s, "0") == 0)
		{
			*out = FALSE;
			return TRUE;
		}
		return FALSE;
	}
	if (!value_as_double (value, &d))
		return FALSE;
	*out = d != 0.0;
	return TRUE;
}

/* A string array from a GStrv or a comma / newline separated string;
 * empty entries are dropped. */
static GStrv
value_as_strv (const GValue *value)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i;

	if (value != NULL && G_VALUE_HOLDS (value, G_TYPE_STRV) && g_value_get_boxed (value) != NULL)
	{
		const gchar * const *v = g_value_get_boxed (value);

		for (i = 0; v[i] != NULL; i++)
		{
			g_autofree gchar *t = g_strstrip (g_strdup (v[i]));

			if (*t != '\0')
				g_ptr_array_add (out, g_steal_pointer (&t));
		}
	}
	else if (value != NULL && G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GStrv) parts = g_strsplit_set (g_value_get_string (value), ",\n", -1);

		for (i = 0; parts[i] != NULL; i++)
		{
			g_autofree gchar *t = g_strstrip (g_strdup (parts[i]));

			if (*t != '\0')
				g_ptr_array_add (out, g_steal_pointer (&t));
		}
	}
	g_ptr_array_add (out, NULL);
	return (GStrv) g_ptr_array_free (out, FALSE);
}

/* A GrlVector3 or "x, y, z" (the physics panel's rule). */
static gboolean
value_as_vec3 (const GValue *value,
               gfloat       *out)
{
	if (value == NULL)
		return FALSE;
	if (G_VALUE_HOLDS (value, GRL_TYPE_VECTOR3))
	{
		const GrlVector3 *v = g_value_get_boxed (value);

		if (v == NULL)
			return FALSE;
		out[0] = v->x;
		out[1] = v->y;
		out[2] = v->z;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GStrv) parts = g_strsplit_set (g_value_get_string (value), ", ", -1);
		guint         n = 0, i;

		for (i = 0; parts[i] != NULL && n < 3; i++)
			if (*parts[i] != '\0')
				out[n++] = (gfloat) g_ascii_strtod (parts[i], NULL);
		return n == 3;
	}
	return FALSE;
}

/* A nullable string ("" counts as none). */
static const gchar *
value_as_string (const GValue *value)
{
	const gchar *s;

	if (value == NULL || !G_VALUE_HOLDS_STRING (value))
		return NULL;
	s = g_value_get_string (value);
	return (s != NULL && *s != '\0') ? s : NULL;
}

/* ==========================================================================
 * The panel's state as one string (the undo value)
 * ========================================================================== */

/*
 * "S\t<settings>\nD\t<data>\nB\t<0|1>\n" + STATE_SEPARATOR + the .rnav
 * text.  Empty references are written as empty fields.
 */
static gchar *
nav_state_capture (LrgLevel   *level,
                   LrgNavBake *bake)
{
	GString          *s = g_string_new (NULL);
	g_autofree gchar *yaml = (bake != NULL) ? lrg_nav_bake_to_data (bake) : NULL;
	const gchar      *settings = (level != NULL) ? lrg_level_get_nav_settings_ref (level) : NULL;
	const gchar      *data = (level != NULL) ? lrg_level_get_nav_data_ref (level) : NULL;

	g_string_append_printf (s, "S\t%s\nD\t%s\nB\t%d\n", settings != NULL ? settings : "", data != NULL ? data : "",
	                        (level != NULL && lrg_level_get_nav_bake_on_load (level)) ? 1 : 0);
	g_string_append (s, STATE_SEPARATOR);
	if (yaml != NULL)
		g_string_append (s, yaml);
	return g_string_free (s, FALSE);
}

/* Splits a state string into its level part and its .rnav text. */
static void
nav_state_split (const gchar  *state,
                 gchar       **level_part,
                 const gchar **yaml)
{
	const gchar *sep = (state != NULL) ? strstr (state, STATE_SEPARATOR) : NULL;

	if (sep == NULL)
	{
		*level_part = g_strdup (state != NULL ? state : "");
		*yaml = NULL;
		return;
	}
	*level_part = g_strndup (state, (gsize) (sep - state));
	*yaml = sep + strlen (STATE_SEPARATOR);
}

/* Makes @level's `navigation:` block what @state says. */
static void
nav_state_apply_level (LrgLevel    *level,
                       const gchar *state)
{
	g_autofree gchar *head = NULL;
	const gchar      *yaml = NULL;
	g_auto(GStrv)     lines = NULL;
	guint             i;

	if (level == NULL)
		return;
	nav_state_split (state, &head, &yaml);
	lrg_level_clear_navigation (level);
	lines = g_strsplit (head, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_auto(GStrv) f = g_strsplit (lines[i], "\t", 2);

		if (f[0] == NULL || f[1] == NULL)
			continue;
		if (g_strcmp0 (f[0], "S") == 0 && *f[1] != '\0')
			lrg_level_set_nav_settings_ref (level, f[1]);
		else if (g_strcmp0 (f[0], "D") == 0 && *f[1] != '\0')
			lrg_level_set_nav_data_ref (level, f[1]);
		else if (g_strcmp0 (f[0], "B") == 0 && g_strcmp0 (f[1], "1") == 0)
			lrg_level_set_nav_bake_on_load (level, TRUE);
	}
}

/* ==========================================================================
 * EditorNavForm: the property grid's object
 * ========================================================================== */

enum
{
	F_0,
	/* Level */
	F_DATA,
	F_BAKE_ON_LOAD,
	/* Agent */
	F_AGENT_RADIUS,
	F_AGENT_HEIGHT,
	F_MAX_SLOPE,
	F_MAX_STEP,
	F_MAX_DROP,
	/* Voxels */
	F_CELL_SIZE,
	F_CELL_HEIGHT,
	/* Regions */
	F_MIN_REGION_AREA,
	F_CORNER_MARGIN,
	/* Sources */
	F_INCLUDE_MESHES,
	F_INCLUDE_COLLIDERS,
	F_STATIC_ONLY,
	F_LAYER_MASK,
	F_INCLUDE_TAGS,
	F_EXCLUDE_TAGS,
	F_PHYSICS_LAYERS,
	F_USE_BOUNDS,
	F_BOUNDS_MIN,
	F_BOUNDS_MAX,
	/* Output */
	F_NAME,
	F_OUTPUT,
	/* The selected area */
	F_AREA_NAME,
	F_AREA_TAG,
	F_AREA_COST,
	/* The selected box */
	F_BOX_MIN,
	F_BOX_MAX,
	/* The selected link */
	F_LINK_START,
	F_LINK_END,
	F_LINK_BIDIRECTIONAL,
	F_LINK_COST,
	F_LINK_AREA,
	F_N
};

#define F_SETTINGS_LAST (F_OUTPUT)

#define EDITOR_TYPE_NAV_FORM (editor_nav_form_get_type ())
G_DECLARE_FINAL_TYPE (EditorNavForm, editor_nav_form, EDITOR, NAV_FORM, GObject)

struct _EditorNavForm
{
	GObject parent_instance;
	GValue  values[F_N];
};

G_DEFINE_FINAL_TYPE (EditorNavForm, editor_nav_form, G_TYPE_OBJECT)

static GParamSpec *form_props[F_N];

static void
editor_nav_form_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	EditorNavForm *self = EDITOR_NAV_FORM (object);

	if (prop_id == 0 || prop_id >= F_N)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	g_value_copy (&self->values[prop_id], value);
}

static void
editor_nav_form_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	EditorNavForm *self = EDITOR_NAV_FORM (object);

	if (prop_id == 0 || prop_id >= F_N)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	if (lrg_property_values_equal (&self->values[prop_id], value))
		return;
	g_value_copy (value, &self->values[prop_id]);
	g_object_notify_by_pspec (object, pspec);
}

static void
editor_nav_form_finalize (GObject *object)
{
	EditorNavForm *self = EDITOR_NAV_FORM (object);
	guint          i;

	for (i = 1; i < F_N; i++)
		g_value_unset (&self->values[i]);
	G_OBJECT_CLASS (editor_nav_form_parent_class)->finalize (object);
}

/* Group and hint as pspec qdata (static strings). */
static GParamSpec *
form_prop (GParamSpec  *pspec,
           const gchar *group,
           const gchar *hint)
{
	if (group != NULL)
		g_param_spec_set_qdata (pspec, lrg_property_group_quark (), (gpointer) group);
	if (hint != NULL)
		g_param_spec_set_qdata (pspec, lrg_property_hint_quark (), (gpointer) hint);
	return pspec;
}

#define FORM_FLAGS (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY)

static GParamSpec *
form_float (const gchar *name,
            const gchar *nick,
            const gchar *blurb,
            const gchar *group,
            gfloat       min,
            gfloat       max,
            gfloat       def)
{
	return form_prop (g_param_spec_float (name, nick, blurb, min, max, def, FORM_FLAGS), group, NULL);
}

static GParamSpec *
form_bool (const gchar *name,
           const gchar *nick,
           const gchar *blurb,
           const gchar *group,
           gboolean     def)
{
	return form_prop (g_param_spec_boolean (name, nick, blurb, def, FORM_FLAGS), group, NULL);
}

static GParamSpec *
form_string (const gchar *name,
             const gchar *nick,
             const gchar *blurb,
             const gchar *group,
             const gchar *hint)
{
	return form_prop (g_param_spec_string (name, nick, blurb, NULL, FORM_FLAGS), group, hint);
}

static GParamSpec *
form_strv (const gchar *name,
           const gchar *nick,
           const gchar *blurb,
           const gchar *group)
{
	return form_prop (g_param_spec_boxed (name, nick, blurb, G_TYPE_STRV, FORM_FLAGS), group, NULL);
}

static GParamSpec *
form_vec3 (const gchar *name,
           const gchar *nick,
           const gchar *blurb,
           const gchar *group)
{
	return form_prop (g_param_spec_boxed (name, nick, blurb, GRL_TYPE_VECTOR3, FORM_FLAGS), group, "vector3");
}

static void
editor_nav_form_class_init (EditorNavFormClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = editor_nav_form_get_property;
	object_class->set_property = editor_nav_form_set_property;
	object_class->finalize = editor_nav_form_finalize;

	form_props[F_DATA] = form_string ("data", "Baked mesh", "The level's navigation: data: (the baked .rnavmesh); "
	                                  "Save writes it", "Level", NULL);
	form_props[F_BAKE_ON_LOAD] = form_bool ("bake-on-load", "Bake on load", "Bake when the level loads when it has "
	                                        "no data (prototypes, procedural levels)", "Level", FALSE);
	form_props[F_AGENT_RADIUS] = form_float ("agent-radius", "Radius", "Agent radius (m): the walkable surface is "
	                                         "eroded by it", "Agent", 0.0f, 100.0f, 0.3f);
	form_props[F_AGENT_HEIGHT] = form_float ("agent-height", "Height", "Head room an agent needs above a floor (m)",
	                                         "Agent", 0.01f, 100.0f, 1.8f);
	form_props[F_MAX_SLOPE] = form_float ("max-slope", "Max slope", "Steepest walkable slope (degrees)", "Agent", 0.0f,
	                                      89.0f, 50.0f);
	form_props[F_MAX_STEP] = form_float ("max-step", "Max step", "Highest step climbed (m)", "Agent", 0.0f, 100.0f,
	                                     0.45f);
	form_props[F_MAX_DROP] = form_float ("max-drop", "Max drop", "Highest ledge to drop down (m); above max step it "
	                                     "adds one-way drop links", "Agent", 0.0f, 100.0f, 0.0f);
	form_props[F_CELL_SIZE] = form_float ("cell-size", "Cell size", "Horizontal voxel size (m); memory grows with "
	                                      "1 / cell size squared", "Voxels", 0.01f, 100.0f, 0.25f);
	form_props[F_CELL_HEIGHT] = form_float ("cell-height", "Cell height", "Vertical voxel size (m)", "Voxels", 0.01f,
	                                        100.0f, 0.1f);
	form_props[F_MIN_REGION_AREA] = form_float ("min-region-area", "Min region", "Connected regions smaller than "
	                                            "this (m^2) are disabled after the bake", "Regions", 0.0f, 1.0e6f,
	                                            0.0f);
	form_props[F_CORNER_MARGIN] = form_float ("corner-margin", "Corner margin", "Paths keep this far from the portal "
	                                          "ends they turn round (m)", "Regions", 0.0f, 100.0f, 0.0f);
	form_props[F_INCLUDE_MESHES] = form_bool ("include-meshes", "Meshes", "Bake mesh renderer geometry", "Sources",
	                                          TRUE);
	form_props[F_INCLUDE_COLLIDERS] = form_bool ("include-colliders", "Colliders", "Bake static collider geometry",
	                                             "Sources", TRUE);
	form_props[F_STATIC_ONLY] = form_bool ("static-only", "Static only", "Only entities flagged static", "Sources",
	                                       FALSE);
	form_props[F_LAYER_MASK] = form_prop (g_param_spec_uint ("layer-mask", "Layer mask", "Render layers that count "
	                                                         "(a bit per layer)", 0, G_MAXUINT32, G_MAXUINT32,
	                                                         FORM_FLAGS), "Sources", NULL);
	form_props[F_INCLUDE_TAGS] = form_strv ("include-tags", "Include tags", "Only entities with one of these tags "
	                                        "(empty: all)", "Sources");
	form_props[F_EXCLUDE_TAGS] = form_strv ("exclude-tags", "Exclude tags", "Never entities with one of these tags",
	                                        "Sources");
	form_props[F_PHYSICS_LAYERS] = form_strv ("physics-layers", "Physics layers", "Only colliders on these layers "
	                                          "(empty: all)", "Sources");
	form_props[F_USE_BOUNDS] = form_bool ("use-bounds", "Clip to bounds", "Restrict the bake to a box", "Sources",
	                                      FALSE);
	form_props[F_BOUNDS_MIN] = form_vec3 ("bounds-min", "Bounds min", "Lowest corner of the bake box", "Sources");
	form_props[F_BOUNDS_MAX] = form_vec3 ("bounds-max", "Bounds max", "Highest corner of the bake box", "Sources");
	form_props[F_NAME] = form_string ("name", "Name", "Display name of the settings", "Output", NULL);
	form_props[F_OUTPUT] = form_string ("output", "Output", "Where Save writes the baked mesh, relative to the "
	                                    ".rnav (empty: <level>.rnavmesh next to the level)", "Output", NULL);

	form_props[F_AREA_NAME] = form_string ("area-name", "Name", "The area's unique name (links name it)", NULL, NULL);
	form_props[F_AREA_TAG] = form_prop (g_param_spec_uint ("area-tag", "Tag", "Unique nonzero tag the polygons "
	                                                       "carry", 1, G_MAXUINT32, 1, FORM_FLAGS), NULL, NULL);
	form_props[F_AREA_COST] = form_float ("area-cost", "Cost", "Traversal cost multiplier (1 = normal ground)", NULL,
	                                      0.001f, 1.0e4f, 1.0f);
	form_props[F_BOX_MIN] = form_vec3 ("box-min", "Min", "Lowest corner (the floor height must lie within y)", NULL);
	form_props[F_BOX_MAX] = form_vec3 ("box-max", "Max", "Highest corner", NULL);
	form_props[F_LINK_START] = form_vec3 ("link-start", "Start", "Where agents enter it (snapped to the mesh)", NULL);
	form_props[F_LINK_END] = form_vec3 ("link-end", "End", "Where it leads", NULL);
	form_props[F_LINK_BIDIRECTIONAL] = form_bool ("link-bidirectional", "Both ways", "Usable from end to start too",
	                                              NULL, TRUE);
	form_props[F_LINK_COST] = form_float ("link-cost", "Extra cost", "Cost on top of its length", NULL, 0.0f, 1.0e6f,
	                                      0.0f);
	form_props[F_LINK_AREA] = form_string ("link-area", "Area", "An area whose tag the link carries", NULL,
	                                       "enum-strings:" NO_AREA);
	g_object_class_install_properties (object_class, F_N, form_props);
}

static void
editor_nav_form_init (EditorNavForm *self)
{
	guint i;

	for (i = 1; i < F_N; i++)
	{
		g_value_init (&self->values[i], form_props[i]->value_type);
		g_param_value_set_default (form_props[i], &self->values[i]);
		/* boxed rows never hold NULL: the grid's editors read them */
		if (form_props[i]->value_type == GRL_TYPE_VECTOR3)
			g_value_take_boxed (&self->values[i], grl_vector3_new (0.0f, 0.0f, 0.0f));
		else if (form_props[i]->value_type == G_TYPE_STRV)
			g_value_take_boxed (&self->values[i], g_new0 (gchar *, 1));
	}
}

/* Sets a form value without the notify reaching the panel (the caller
 * guards with self->applying). */
static void
form_set (EditorNavForm *form,
          guint          id,
          const GValue  *value)
{
	g_object_set_property (G_OBJECT (form), form_props[id]->name, value);
}

static void
form_set_float (EditorNavForm *form,
                guint          id,
                gfloat         f)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_FLOAT);
	g_value_set_float (&v, f);
	g_param_value_validate (form_props[id], &v);
	form_set (form, id, &v);
}

static void
form_set_bool (EditorNavForm *form,
               guint          id,
               gboolean       b)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, b);
	form_set (form, id, &v);
}

static void
form_set_uint (EditorNavForm *form,
               guint          id,
               guint          u)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_UINT);
	g_value_set_uint (&v, u);
	g_param_value_validate (form_props[id], &v);
	form_set (form, id, &v);
}

static void
form_set_string (EditorNavForm *form,
                 guint          id,
                 const gchar   *s)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, s);
	form_set (form, id, &v);
}

static void
form_set_strv (EditorNavForm       *form,
               guint                id,
               const gchar * const *strv)
{
	g_auto(GValue)       v = G_VALUE_INIT;
	static const gchar  *empty[] = { NULL };

	g_value_init (&v, G_TYPE_STRV);
	g_value_set_boxed (&v, strv != NULL ? strv : empty);
	form_set (form, id, &v);
}

static void
form_set_vec3 (EditorNavForm *form,
               guint          id,
               const gfloat  *p)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, GRL_TYPE_VECTOR3);
	g_value_take_boxed (&v, grl_vector3_new (p[0], p[1], p[2]));
	form_set (form, id, &v);
}

static const GValue *
form_value (EditorNavForm *form,
            guint          id)
{
	return &form->values[id];
}

/* ==========================================================================
 * EditorNavProxy: the undo target
 * ========================================================================== */

static void nav_proxy_applied (LrgEditorNavPanel *panel,
                               LrgLevel          *level,
                               const gchar       *state);

#define EDITOR_TYPE_NAV_PROXY (editor_nav_proxy_get_type ())
G_DECLARE_FINAL_TYPE (EditorNavProxy, editor_nav_proxy, EDITOR, NAV_PROXY, GObject)

struct _EditorNavProxy
{
	GObject            parent_instance;
	LrgLevel          *level;   /* owned: the level the edits belong to */
	LrgEditorNavPanel *panel;   /* weak */
	gchar             *state;   /* last applied state */
};

G_DEFINE_FINAL_TYPE (EditorNavProxy, editor_nav_proxy, G_TYPE_OBJECT)

enum { PROXY_PROP_0, PROXY_PROP_STATE, PROXY_N_PROPS };

static void
editor_nav_proxy_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	EditorNavProxy *self = EDITOR_NAV_PROXY (object);

	if (prop_id == PROXY_PROP_STATE)
		g_value_set_string (value, self->state);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

/* Setting "state" (execute, undo, redo) applies it through the panel,
 * which owns the in-memory settings; without the panel only the level
 * block can follow. */
static void
editor_nav_proxy_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	EditorNavProxy *self = EDITOR_NAV_PROXY (object);

	if (prop_id != PROXY_PROP_STATE)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	g_free (self->state);
	self->state = g_value_dup_string (value);
	if (self->panel != NULL)
		nav_proxy_applied (self->panel, self->level, self->state);
	else
		nav_state_apply_level (self->level, self->state);
	g_object_notify_by_pspec (object, pspec);
}

static void
editor_nav_proxy_finalize (GObject *object)
{
	EditorNavProxy *self = EDITOR_NAV_PROXY (object);

	g_clear_object (&self->level);
	g_clear_weak_pointer (&self->panel);
	g_free (self->state);
	G_OBJECT_CLASS (editor_nav_proxy_parent_class)->finalize (object);
}

static void
editor_nav_proxy_class_init (EditorNavProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = editor_nav_proxy_get_property;
	object_class->set_property = editor_nav_proxy_set_property;
	object_class->finalize = editor_nav_proxy_finalize;
	g_object_class_install_property (object_class, PROXY_PROP_STATE,
		g_param_spec_string ("state", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
}

static void
editor_nav_proxy_init (EditorNavProxy *self)
{
}

/* ==========================================================================
 * The panel
 * ========================================================================== */

typedef enum
{
	BAKE_IDLE,
	BAKE_RUNNING,
	BAKE_DONE,
	BAKE_CANCELLED,
	BAKE_FAILED
} BakeState;

struct _LrgEditorNavPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* ref */
	LrgEditorPlaySession *play;         /* ref, nullable */
	LrgLevel             *level;        /* weak: the level the panel follows */

	/* Settings and the undo state */
	LrgNavBake           *bake;         /* the in-memory settings (owned) */
	gchar                *state;        /* the last committed / applied state */
	gchar                *settings_path;/* absolute, of the level's settings ref */
	gchar                *disk_yaml;    /* what settings_path holds (NULL: unread / missing) */
	gchar                *disk_for;     /* the path disk_yaml was read from */
	gchar                *default_yaml; /* lrg_nav_bake_new()'s text */
	gfloat                bounds_min[3];/* remembered while bounds are off */
	gfloat                bounds_max[3];
	gboolean              applying;     /* the panel is writing: ignore echoes */
	gboolean              in_gesture;
	guint                 gesture_id;
	EditorNavProxy       *gesture_proxy;

	/* Selection (areas, boxes, links) */
	gint                  sel_area;
	gint                  sel_box;
	gint                  sel_link;
	gchar                *area_sig;     /* the area names the link-area hint lists */

	/* Mesh and bake */
	LrgNavPolyMesh       *mesh;         /* the shown mesh (owned) */
	gboolean              mesh_unsaved; /* a bake not written yet */
	gchar                *mesh_from;    /* absolute path the mesh was loaded from */
	BakeState             bake_state;
	guint                 bake_serial;  /* bumps on start / cancel / level change */
	gint64                bake_started;
	gchar                *error;
	guint                 last_triangles;
	gdouble               last_seconds;
	gboolean              auto_save;

	/* Overlay */
	LrgNavDebugFlags      overlay;
	LrgNavDebugFlags      overlay_last; /* what the eye chip turns back on */
	gboolean              show_handles;
	LrgDebugDraw         *draw;         /* recorded inside the 3D pass */
	GObject              *runtime;      /* weak: the renderer hooked */
	gulong                overlay_id;

	/* Handles */
	LrgEditorNavHandle    drag;         /* the handle being dragged */
	LrgEditorNavHandle    hot;          /* the hovered handle */
	gchar                *drag_before;  /* the state at the press */
	gfloat                drag_grab[3]; /* centre drag: grab offset */
	gfloat                drag_y;       /* the drag plane's height */

	/* Play */
	gint                  debug_agent;

	/* Widgets (owned by the container tree) */
	LrgWidget            *top;
	LrgWidget            *header_row;
	LrgWidget            *status;
	LrgWidget            *bake_chip;
	LrgWidget            *save_chip;
	LrgWidget            *overlay_chip;
	LrgWidget            *clear_chip;
	LrgProgressBar       *bar;
	LrgWidget            *phase_label;
	LrgWidget            *summary;
	LrgWidget            *scroll;
	LrgWidget            *content;
	LrgExpander          *agents_section;
	LrgWidget            *agent_list;
	LrgWidget            *agent_footer;
	LrgComboBox          *settings_combo;
	LrgWidget            *settings_path_label;
	EditorNavForm        *form;
	LrgPropertyGrid      *grid;
	LrgWidget            *area_list;
	LrgPropertyGrid      *area_grid;
	LrgWidget            *box_heading;
	LrgWidget            *box_list;
	LrgPropertyGrid      *box_grid;
	LrgWidget            *link_list;
	LrgPropertyGrid      *link_grid;
	LrgWidget            *empty;
	GPtrArray            *settings_files;  /* relative paths in the combo order (after "(none)") */
	gchar                *combo_ref;       /* the settings ref the picker was filled for */
};

G_DEFINE_FINAL_TYPE (LrgEditorNavPanel, lrg_editor_nav_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorNavPanel *self);
static void sync_form (LrgEditorNavPanel *self);
static void queue_viewports (LrgEditorNavPanel *self);
static void refresh_mesh (LrgEditorNavPanel *self);

static LrgLevel *
current_level (LrgEditorNavPanel *self)
{
	return (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
}

static gboolean
is_playing (LrgEditorNavPanel *self)
{
	return self->play != NULL && lrg_editor_play_session_is_playing (self->play);
}

/* The directory the level's relative references resolve against: the
 * level file's, else the project's. */
static gchar *
level_dir (LrgEditorNavPanel *self)
{
	const gchar *path = (self->editor != NULL) ? lrg_editor_get_path (self->editor) : NULL;

	if (path != NULL)
		return g_path_get_dirname (path);
	if (self->app != NULL && editor_app_base_dir (self->app) != NULL)
		return g_strdup (editor_app_base_dir (self->app));
	return NULL;
}

/* The level file's name without its extension ("castle"). */
static gchar *
level_stem (LrgEditorNavPanel *self)
{
	const gchar      *path = (self->editor != NULL) ? lrg_editor_get_path (self->editor) : NULL;
	g_autofree gchar *base = NULL;
	gchar            *dot;

	if (path == NULL)
		return NULL;
	base = g_path_get_basename (path);
	dot = strrchr (base, '.');
	if (dot != NULL && dot != base)
		*dot = '\0';
	return g_steal_pointer (&base);
}

/* A level reference as an absolute path: absolute as is, `guid:` through
 * the asset database, relative against the level's directory (then the
 * project's).  NULL for NULL. */
static gchar *
resolve_ref (LrgEditorNavPanel *self,
             const gchar       *ref)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;

	if (ref == NULL || *ref == '\0')
		return NULL;
	if (g_path_is_absolute (ref))
		return g_strdup (ref);
	if (g_str_has_prefix (ref, "guid:") && self->app != NULL && self->app->assets != NULL)
	{
		LrgAssetEntry *entry = lrg_asset_database_lookup_guid (self->app->assets, ref + 5);

		if (entry != NULL && lrg_asset_entry_get_path (entry) != NULL)
			return editor_app_project_path (self->app, lrg_asset_entry_get_path (entry));
	}
	dir = level_dir (self);
	if (dir != NULL)
	{
		path = g_build_filename (dir, ref, NULL);
		if (g_file_test (path, G_FILE_TEST_EXISTS) || self->app == NULL)
			return g_steal_pointer (&path);
	}
	if (self->app != NULL)
	{
		g_autofree gchar *proj = editor_app_project_path (self->app, ref);

		if (proj != NULL && g_file_test (proj, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&proj);
	}
	return (path != NULL) ? g_steal_pointer (&path) : g_strdup (ref);
}

/* @path as the level stores it: relative when inside the level's
 * directory. */
static gchar *
level_ref (LrgEditorNavPanel *self,
           const gchar       *path)
{
	g_autofree gchar *dir = level_dir (self);

	if (dir != NULL)
	{
		g_autofree gchar *prefix = g_strconcat (dir, G_DIR_SEPARATOR_S, NULL);

		if (g_str_has_prefix (path, prefix))
			return g_strdup (path + strlen (prefix));
	}
	return g_strdup (path);
}

/* A user path (absolute, or relative to the level's directory). */
static gchar *
user_path (LrgEditorNavPanel *self,
           const gchar       *path)
{
	g_autofree gchar *dir = NULL;

	if (g_path_is_absolute (path))
		return g_strdup (path);
	dir = level_dir (self);
	return (dir != NULL) ? g_build_filename (dir, path, NULL) : NULL;
}

/* Re-resolves settings_path and re-reads the file it names when the path
 * changed (the dirty check compares against it). */
static void
refresh_disk (LrgEditorNavPanel *self)
{
	LrgLevel    *level = current_level (self);
	const gchar *ref = (level != NULL) ? lrg_level_get_nav_settings_ref (level) : NULL;

	g_free (self->settings_path);
	self->settings_path = resolve_ref (self, ref);
	if (g_strcmp0 (self->settings_path, self->disk_for) == 0)
		return;
	g_free (self->disk_for);
	self->disk_for = g_strdup (self->settings_path);
	g_clear_pointer (&self->disk_yaml, g_free);
	if (self->settings_path != NULL)
	{
		g_autoptr(LrgNavBake) disk = lrg_nav_bake_new_from_file (self->settings_path, NULL);

		/* normalised through the writer, like the in-memory text */
		if (disk != NULL)
			self->disk_yaml = lrg_nav_bake_to_data (disk);
	}
}

/* ==========================================================================
 * Commands
 * ========================================================================== */

/*
 * Records "the state went from @before to @after" (already applied) as one
 * command; @merge folds it into the running gesture's previous step.
 */
static void
commit (LrgEditorNavPanel *self,
        const gchar       *before,
        const gchar       *after,
        const gchar       *label,
        gboolean           merge)
{
	LrgLevel         *level = current_level (self);
	g_auto(GValue)    vb = G_VALUE_INIT;
	g_auto(GValue)    va = G_VALUE_INIT;
	EditorNavProxy   *proxy;
	LrgEditorCommand *cmd;

	g_free (self->state);
	self->state = g_strdup (after);
	if (level == NULL || g_strcmp0 (before, after) == 0)
		return;
	if (merge && self->gesture_proxy != NULL && self->gesture_proxy->level == level)
		proxy = g_object_ref (self->gesture_proxy);
	else
	{
		proxy = g_object_new (EDITOR_TYPE_NAV_PROXY, NULL);
		proxy->level = g_object_ref (level);
		g_set_weak_pointer (&proxy->panel, self);
		if (merge)
			g_set_object (&self->gesture_proxy, proxy);
	}

	g_value_init (&vb, G_TYPE_STRING);
	g_value_set_string (&vb, before);
	g_value_init (&va, G_TYPE_STRING);
	g_value_set_string (&va, after);
	cmd = lrg_editor_command_new_set_property (G_OBJECT (proxy), "state", &vb, &va);
	lrg_editor_command_set_label (cmd, label != NULL ? label : "Edit Navigation");

	self->applying = TRUE;
	if (merge)
		lrg_editor_execute_merged (self->editor, cmd);
	else
		lrg_editor_execute (self->editor, cmd);
	self->applying = FALSE;
	g_object_unref (proxy);
}

/* The current state (level block + settings text). */
static gchar *
capture (LrgEditorNavPanel *self)
{
	return nav_state_capture (current_level (self), self->bake);
}

/* Commits whatever changed since the last commit as one step. */
static gboolean
commit_now (LrgEditorNavPanel *self,
            const gchar       *before,
            const gchar       *label,
            gboolean           merge)
{
	g_autofree gchar *after = capture (self);
	gboolean          changed = g_strcmp0 (before, after) != 0;

	commit (self, before, after, label, merge);
	refresh_disk (self);
	if (!self->in_gesture)
		sync_form (self);
	sync_widgets (self);
	queue_viewports (self);
	return changed;
}

/*
 * The proxy applied @state (an execute, undo or redo).  The level block
 * is rewritten; the settings are re-parsed when their text differs.
 */
static void
nav_proxy_applied (LrgEditorNavPanel *self,
                   LrgLevel          *level,
                   const gchar       *state)
{
	g_autofree gchar *head = NULL;
	g_autofree gchar *now = NULL;
	const gchar      *yaml = NULL;

	if (level != current_level (self))
	{
		nav_state_apply_level (level, state);
		return;
	}
	now = capture (self);
	if (g_strcmp0 (now, state) == 0)
		return;
	self->applying = TRUE;
	nav_state_apply_level (level, state);
	nav_state_split (state, &head, &yaml);
	if (yaml != NULL)
	{
		g_autofree gchar *mine = lrg_nav_bake_to_data (self->bake);

		if (g_strcmp0 (mine, yaml) != 0)
		{
			g_autoptr(GError) error = NULL;
			LrgNavBake       *parsed = lrg_nav_bake_new_from_data (yaml, -1, &error);

			if (parsed != NULL)
			{
				g_clear_object (&self->bake);
				self->bake = parsed;
			}
			else
				g_warning ("navigation panel: undo state did not parse: %s", error->message);
		}
	}
	self->applying = FALSE;
	g_free (self->state);
	self->state = capture (self);
	refresh_disk (self);
	refresh_mesh (self);
	sync_form (self);
	sync_widgets (self);
	queue_viewports (self);
}

/* ==========================================================================
 * Areas and links as plain lists (the "set" operations rebuild them)
 * ========================================================================== */

typedef struct
{
	gchar   *name;
	guint32  tag;
	gfloat   cost;
	GArray  *boxes;   /* gfloat[6] per box: min, max */
} AreaRec;

typedef struct
{
	gfloat   start[3];
	gfloat   end[3];
	gboolean bidirectional;
	gfloat   cost;
	gchar   *area;
} LinkRec;

static void
area_rec_free (gpointer data)
{
	AreaRec *a = data;

	g_free (a->name);
	g_array_unref (a->boxes);
	g_free (a);
}

static void
link_rec_free (gpointer data)
{
	LinkRec *l = data;

	g_free (l->area);
	g_free (l);
}

/* Reads the areas (with boxes) and links of @bake. */
static void
lists_read (LrgNavBake  *bake,
            GPtrArray  **areas,
            GPtrArray  **links)
{
	guint i, k;

	*areas = g_ptr_array_new_with_free_func (area_rec_free);
	*links = g_ptr_array_new_with_free_func (link_rec_free);
	for (i = 0; i < lrg_nav_bake_get_n_areas (bake); i++)
	{
		AreaRec     *a = g_new0 (AreaRec, 1);
		const gchar *name = NULL;

		lrg_nav_bake_get_area (bake, i, &name, &a->tag, &a->cost);
		a->name = g_strdup (name);
		a->boxes = g_array_new (FALSE, FALSE, sizeof (gfloat) * 6);
		for (k = 0; k < lrg_nav_bake_get_n_area_boxes (bake, i); k++)
		{
			gfloat b[6];

			lrg_nav_bake_get_area_box (bake, i, k, b, b + 3);
			g_array_append_vals (a->boxes, b, 1);
		}
		g_ptr_array_add (*areas, a);
	}
	for (i = 0; i < lrg_nav_bake_get_n_links (bake); i++)
	{
		LinkRec     *l = g_new0 (LinkRec, 1);
		const gchar *area = NULL;

		lrg_nav_bake_get_link (bake, i, l->start, l->end, &l->bidirectional, &l->cost, &area);
		l->area = g_strdup (area);
		g_ptr_array_add (*links, l);
	}
}

/*
 * Replaces the areas and links of a COPY of @bake with the lists; on
 * success the copy is returned (the caller installs it), on failure NULL
 * with @error naming the first entry the engine refused.  Unknown keys
 * inside the rewritten area and link entries are not kept.
 */
static LrgNavBake *
lists_write (LrgNavBake  *bake,
             GPtrArray   *areas,
             GPtrArray   *links,
             GError     **error)
{
	g_autoptr(LrgNavBake) copy = lrg_nav_bake_copy (bake);
	guint                 i, k;

	while (lrg_nav_bake_get_n_links (copy) > 0)
		lrg_nav_bake_remove_link (copy, lrg_nav_bake_get_n_links (copy) - 1);
	while (lrg_nav_bake_get_n_areas (copy) > 0)
		lrg_nav_bake_remove_area (copy, lrg_nav_bake_get_n_areas (copy) - 1);
	for (i = 0; i < areas->len; i++)
	{
		AreaRec *a = g_ptr_array_index (areas, i);
		gint     index = lrg_nav_bake_add_area (copy, a->name, a->tag, a->cost);

		if (index < 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Area '%s' (tag %u, cost %g): the name or tag is taken, or a value is invalid",
			             a->name != NULL ? a->name : "", a->tag, (gdouble) a->cost);
			return NULL;
		}
		for (k = 0; k < a->boxes->len; k++)
		{
			const gfloat *b = (const gfloat *) (gpointer) (a->boxes->data + k * sizeof (gfloat) * 6);

			if (lrg_nav_bake_add_area_box (copy, (guint) index, b, b + 3) < 0)
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Area '%s' box %u is empty", a->name, k);
				return NULL;
			}
		}
	}
	for (i = 0; i < links->len; i++)
	{
		LinkRec *l = g_ptr_array_index (links, i);

		if (lrg_nav_bake_add_link (copy, l->start, l->end, l->bidirectional, l->cost, l->area) < 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Link %u: a negative cost or an unknown area '%s'", i, l->area != NULL ? l->area : "");
			return NULL;
		}
	}
	return g_steal_pointer (&copy);
}

/* Installs @bake (owned) as the panel's settings. */
static void
install_bake (LrgEditorNavPanel *self,
              LrgNavBake        *bake)
{
	g_clear_object (&self->bake);
	self->bake = bake;
}

static gfloat *
box_at (AreaRec *a,
        guint    box)
{
	return (gfloat *) (gpointer) (a->boxes->data + box * sizeof (gfloat) * 6);
}

/* Sorts the corners per axis. */
static void
sort_box (gfloat *mn,
          gfloat *mx)
{
	gint k;

	for (k = 0; k < 3; k++)
	{
		if (mn[k] > mx[k])
		{
			gfloat t = mn[k];

			mn[k] = mx[k];
			mx[k] = t;
		}
	}
}

/* ==========================================================================
 * The form
 * ========================================================================== */

/* The area names as an "enum-strings:" hint (the link's area picker). */
static void
update_area_hint (LrgEditorNavPanel *self)
{
	GString *s = g_string_new ("enum-strings:" NO_AREA);
	guint    i;

	for (i = 0; i < lrg_nav_bake_get_n_areas (self->bake); i++)
	{
		const gchar *name = NULL;

		lrg_nav_bake_get_area (self->bake, i, &name, NULL, NULL);
		if (name != NULL && strchr (name, ',') == NULL)
			g_string_append_printf (s, ",%s", name);
	}
	if (g_strcmp0 (s->str, self->area_sig) == 0)
	{
		g_string_free (s, TRUE);
		return;
	}
	g_free (self->area_sig);
	self->area_sig = g_string_free (s, FALSE);
	g_param_spec_set_qdata_full (form_props[F_LINK_AREA], lrg_property_hint_quark (), g_strdup (self->area_sig),
	                             g_free);
	if (self->link_grid != NULL)
		lrg_property_grid_rebuild (self->link_grid);
}

/* Clamps the selection to what exists. */
static void
clamp_selection (LrgEditorNavPanel *self)
{
	guint n_areas = lrg_nav_bake_get_n_areas (self->bake);
	guint n_links = lrg_nav_bake_get_n_links (self->bake);

	if (self->sel_area >= (gint) n_areas)
		self->sel_area = (gint) n_areas - 1;
	if (self->sel_area < 0)
		self->sel_box = -1;
	else if (self->sel_box >= (gint) lrg_nav_bake_get_n_area_boxes (self->bake, (guint) self->sel_area))
		self->sel_box = (gint) lrg_nav_bake_get_n_area_boxes (self->bake, (guint) self->sel_area) - 1;
	if (self->sel_link >= (gint) n_links)
		self->sel_link = (gint) n_links - 1;
}

/* Settings + level + selection -> the form (no commits). */
static void
sync_form (LrgEditorNavPanel *self)
{
	LrgLevel *level = current_level (self);
	gfloat    radius, height, slope, step, drop = 0.0f;
	gfloat    mn[3], mx[3];
	gboolean  b = FALSE;
	guint     mask = G_MAXUINT32;
	gfloat    f;
	gboolean  saved = self->applying;

	if (self->form == NULL || self->bake == NULL)
		return;
	clamp_selection (self);
	update_area_hint (self);
	self->applying = TRUE;
	g_object_freeze_notify (G_OBJECT (self->form));

	form_set_string (self->form, F_DATA, level != NULL ? lrg_level_get_nav_data_ref (level) : NULL);
	form_set_bool (self->form, F_BAKE_ON_LOAD, level != NULL && lrg_level_get_nav_bake_on_load (level));
	lrg_nav_bake_get_agent (self->bake, &radius, &height, &slope, &step);
	g_object_get (self->bake, "max-drop", &drop, NULL);
	form_set_float (self->form, F_AGENT_RADIUS, radius);
	form_set_float (self->form, F_AGENT_HEIGHT, height);
	form_set_float (self->form, F_MAX_SLOPE, slope);
	form_set_float (self->form, F_MAX_STEP, step);
	form_set_float (self->form, F_MAX_DROP, drop);
	form_set_float (self->form, F_CELL_SIZE, lrg_nav_bake_get_cell_size (self->bake));
	form_set_float (self->form, F_CELL_HEIGHT, lrg_nav_bake_get_cell_height (self->bake));
	g_object_get (self->bake, "min-region-area", &f, NULL);
	form_set_float (self->form, F_MIN_REGION_AREA, f);
	g_object_get (self->bake, "corner-margin", &f, NULL);
	form_set_float (self->form, F_CORNER_MARGIN, f);
	g_object_get (self->bake, "include-meshes", &b, NULL);
	form_set_bool (self->form, F_INCLUDE_MESHES, b);
	g_object_get (self->bake, "include-colliders", &b, NULL);
	form_set_bool (self->form, F_INCLUDE_COLLIDERS, b);
	g_object_get (self->bake, "static-only", &b, NULL);
	form_set_bool (self->form, F_STATIC_ONLY, b);
	g_object_get (self->bake, "layer-mask", &mask, NULL);
	form_set_uint (self->form, F_LAYER_MASK, mask);
	form_set_strv (self->form, F_INCLUDE_TAGS, lrg_nav_bake_get_include_tags (self->bake));
	form_set_strv (self->form, F_EXCLUDE_TAGS, lrg_nav_bake_get_exclude_tags (self->bake));
	form_set_strv (self->form, F_PHYSICS_LAYERS, lrg_nav_bake_get_physics_layers (self->bake));
	if (lrg_nav_bake_get_bounds (self->bake, mn, mx))
	{
		memcpy (self->bounds_min, mn, sizeof mn);
		memcpy (self->bounds_max, mx, sizeof mx);
		form_set_bool (self->form, F_USE_BOUNDS, TRUE);
	}
	else
		form_set_bool (self->form, F_USE_BOUNDS, FALSE);
	form_set_vec3 (self->form, F_BOUNDS_MIN, self->bounds_min);
	form_set_vec3 (self->form, F_BOUNDS_MAX, self->bounds_max);
	form_set_string (self->form, F_NAME, lrg_nav_bake_get_name (self->bake));
	{
		g_autofree gchar *output = NULL;

		g_object_get (self->bake, "output", &output, NULL);
		form_set_string (self->form, F_OUTPUT, output);
	}

	/* the selected area, box, link */
	if (self->sel_area >= 0)
	{
		const gchar *name = NULL;
		guint32      tag = 1;
		gfloat       cost = 1.0f;

		lrg_nav_bake_get_area (self->bake, (guint) self->sel_area, &name, &tag, &cost);
		form_set_string (self->form, F_AREA_NAME, name);
		form_set_uint (self->form, F_AREA_TAG, tag);
		form_set_float (self->form, F_AREA_COST, cost);
		if (self->sel_box >= 0 &&
		    lrg_nav_bake_get_area_box (self->bake, (guint) self->sel_area, (guint) self->sel_box, mn, mx))
		{
			form_set_vec3 (self->form, F_BOX_MIN, mn);
			form_set_vec3 (self->form, F_BOX_MAX, mx);
		}
	}
	if (self->sel_link >= 0)
	{
		gfloat       s[3], e[3], cost = 0.0f;
		gboolean     bidir = TRUE;
		const gchar *area = NULL;

		lrg_nav_bake_get_link (self->bake, (guint) self->sel_link, s, e, &bidir, &cost, &area);
		form_set_vec3 (self->form, F_LINK_START, s);
		form_set_vec3 (self->form, F_LINK_END, e);
		form_set_bool (self->form, F_LINK_BIDIRECTIONAL, bidir);
		form_set_float (self->form, F_LINK_COST, cost);
		form_set_string (self->form, F_LINK_AREA, area != NULL ? area : NO_AREA);
	}

	g_object_thaw_notify (G_OBJECT (self->form));
	self->applying = saved;
	if (self->grid != NULL && !self->in_gesture)
	{
		lrg_property_grid_refresh (self->grid);
		lrg_property_grid_refresh (self->area_grid);
		lrg_property_grid_refresh (self->box_grid);
		lrg_property_grid_refresh (self->link_grid);
	}
}

/* ==========================================================================
 * Fields (the settings form, set_field, nav.set)
 * ========================================================================== */

static gboolean
level_required (LrgEditorNavPanel  *self,
                GError            **error)
{
	if (current_level (self) != NULL)
		return TRUE;
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No level is open");
	return FALSE;
}

static gboolean load_settings_file (LrgEditorNavPanel  *self,
                                    const gchar        *path,
                                    GError            **error);

/*
 * Applies one field to the live settings / level (no commit).  The
 * caller captured the state before and commits after.
 */
static gboolean
apply_field (LrgEditorNavPanel  *self,
             const gchar        *raw_key,
             const GValue       *value,
             GError            **error)
{
	g_autofree gchar *key = canon_key (raw_key);
	LrgLevel         *level = current_level (self);
	GParamSpec       *pspec;

	/* The level's block */
	if (g_strcmp0 (key, "settings") == 0)
	{
		const gchar *path = value_as_string (value);

		if (path == NULL)
		{
			lrg_level_set_nav_settings_ref (level, NULL);
			return TRUE;
		}
		return load_settings_file (self, path, error);
	}
	if (g_strcmp0 (key, "data") == 0)
	{
		lrg_level_set_nav_data_ref (level, value_as_string (value));
		return TRUE;
	}
	if (g_strcmp0 (key, "bake-on-load") == 0)
	{
		gboolean b;

		if (!value_as_bool (value, &b))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes a boolean", key);
			return FALSE;
		}
		lrg_level_set_nav_bake_on_load (level, b);
		return TRUE;
	}

	/* Lists */
	if (g_strcmp0 (key, "include-tags") == 0 || g_strcmp0 (key, "exclude-tags") == 0 ||
	    g_strcmp0 (key, "physics-layers") == 0)
	{
		g_auto(GStrv) tags = value_as_strv (value);

		if (key[0] == 'i')
			lrg_nav_bake_set_include_tags (self->bake, (const gchar * const *) tags);
		else if (key[0] == 'e')
			lrg_nav_bake_set_exclude_tags (self->bake, (const gchar * const *) tags);
		else
			lrg_nav_bake_set_physics_layers (self->bake, (const gchar * const *) tags);
		return TRUE;
	}

	/* Bounds: the box is remembered while switched off */
	if (g_strcmp0 (key, "use-bounds") == 0)
	{
		gboolean b;

		if (!value_as_bool (value, &b))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes a boolean", key);
			return FALSE;
		}
		if (b)
		{
			sort_box (self->bounds_min, self->bounds_max);
			lrg_nav_bake_set_bounds (self->bake, self->bounds_min, self->bounds_max);
		}
		else
			lrg_nav_bake_set_bounds (self->bake, NULL, NULL);
		return TRUE;
	}
	if (g_strcmp0 (key, "bounds-min") == 0 || g_strcmp0 (key, "bounds-max") == 0)
	{
		gfloat p[3], mn[3], mx[3];

		if (!value_as_vec3 (value, p))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes three numbers", key);
			return FALSE;
		}
		memcpy (g_strcmp0 (key, "bounds-min") == 0 ? self->bounds_min : self->bounds_max, p, sizeof p);
		if (lrg_nav_bake_get_bounds (self->bake, mn, mx))
		{
			memcpy (mn, self->bounds_min, sizeof mn);
			memcpy (mx, self->bounds_max, sizeof mx);
			sort_box (mn, mx);
			lrg_nav_bake_set_bounds (self->bake, mn, mx);
		}
		return TRUE;
	}

	/* Everything else is an LrgNavBake property of the same name */
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->bake), key);
	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No navigation field '%s'", raw_key);
		return FALSE;
	}
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_value_init (&v, pspec->value_type);
		if (pspec->value_type == G_TYPE_STRING)
			g_value_set_string (&v, value_as_string (value));
		else if (pspec->value_type == G_TYPE_BOOLEAN)
		{
			gboolean b;

			if (!value_as_bool (value, &b))
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes a boolean", key);
				return FALSE;
			}
			g_value_set_boolean (&v, b);
		}
		else
		{
			gdouble d;

			if (!value_as_double (value, &d))
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes a number", key);
				return FALSE;
			}
			if (pspec->value_type == G_TYPE_FLOAT)
				g_value_set_float (&v, (gfloat) d);
			else if (pspec->value_type == G_TYPE_UINT)
				g_value_set_uint (&v, (guint) CLAMP (d, 0.0, (gdouble) G_MAXUINT32));
			else if (pspec->value_type == G_TYPE_INT)
				g_value_set_int (&v, (gint) CLAMP (d, (gdouble) G_MININT, (gdouble) G_MAXINT));
			else if (pspec->value_type == G_TYPE_DOUBLE)
				g_value_set_double (&v, d);
			else
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unsupported field '%s'", key);
				return FALSE;
			}
		}
		g_param_value_validate (pspec, &v);
		g_object_set_property (G_OBJECT (self->bake), pspec->name, &v);
	}
	return TRUE;
}

gboolean
lrg_editor_nav_panel_set_field (LrgEditorNavPanel  *self,
                                const gchar        *key,
                                const GValue       *value,
                                GError            **error)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *label = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	before = capture (self);
	self->applying = TRUE;
	ok = apply_field (self, key, value, error);
	self->applying = FALSE;
	label = g_strdup_printf ("Navigation: %s", key);
	commit_now (self, before, label, FALSE);
	return ok;
}

/* The grid changed a form value: route it to the settings, the selected
 * area, box or link. */
static void
on_form_notify (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *before = NULL;
	g_autofree gchar  *label = NULL;
	guint              id = pspec->param_id;
	gboolean           ok = TRUE;

	if (self->applying || current_level (self) == NULL)
		return;
	before = g_strdup (self->state);
	self->applying = TRUE;
	if (id <= F_SETTINGS_LAST)
	{
		ok = apply_field (self, pspec->name, form_value (self->form, id), &error);
		label = g_strdup_printf ("Navigation: %s", g_param_spec_get_nick (pspec));
	}
	else if (id >= F_AREA_NAME && id <= F_AREA_COST && self->sel_area >= 0)
	{
		const gchar *name = value_as_string (form_value (self->form, F_AREA_NAME));

		self->applying = FALSE;
		ok = lrg_editor_nav_panel_set_area (self, (guint) self->sel_area, name,
		                                    g_value_get_uint (form_value (self->form, F_AREA_TAG)),
		                                    g_value_get_float (form_value (self->form, F_AREA_COST)), &error);
		self->applying = TRUE;
		label = NULL;
	}
	else if ((id == F_BOX_MIN || id == F_BOX_MAX) && self->sel_area >= 0 && self->sel_box >= 0)
	{
		gfloat mn[3], mx[3];

		value_as_vec3 (form_value (self->form, F_BOX_MIN), mn);
		value_as_vec3 (form_value (self->form, F_BOX_MAX), mx);
		self->applying = FALSE;
		ok = lrg_editor_nav_panel_set_area_box (self, (guint) self->sel_area, (guint) self->sel_box, mn, mx, &error);
		self->applying = TRUE;
	}
	else if (id >= F_LINK_START && id <= F_LINK_AREA && self->sel_link >= 0)
	{
		gfloat       s[3], e[3];
		const gchar *area = value_as_string (form_value (self->form, F_LINK_AREA));

		value_as_vec3 (form_value (self->form, F_LINK_START), s);
		value_as_vec3 (form_value (self->form, F_LINK_END), e);
		if (g_strcmp0 (area, NO_AREA) == 0)
			area = NULL;
		self->applying = FALSE;
		ok = lrg_editor_nav_panel_set_link (self, (guint) self->sel_link, s, e,
		                                    g_value_get_boolean (form_value (self->form, F_LINK_BIDIRECTIONAL)),
		                                    g_value_get_float (form_value (self->form, F_LINK_COST)), area, &error);
		self->applying = TRUE;
	}
	self->applying = FALSE;
	if (!ok && self->app != NULL && error != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Navigation: %s", error->message);
	if (label != NULL)
		commit_now (self, before, label, self->in_gesture);
	else if (!ok)
		sync_form (self);
}

/* ==========================================================================
 * Areas and links
 * ========================================================================== */

/* The lowest tag no area uses. */
static guint32
free_tag (LrgNavBake *bake)
{
	guint32 tag = 1;
	gboolean taken = TRUE;

	while (taken)
	{
		guint i;

		taken = FALSE;
		for (i = 0; i < lrg_nav_bake_get_n_areas (bake); i++)
		{
			guint32 t = 0;

			lrg_nav_bake_get_area (bake, i, NULL, &t, NULL);
			if (t == tag)
			{
				taken = TRUE;
				tag++;
				break;
			}
		}
	}
	return tag;
}

gint
lrg_editor_nav_panel_add_area (LrgEditorNavPanel  *self,
                               const gchar        *name,
                               guint32             tag,
                               gfloat              cost,
                               GError            **error)
{
	g_autofree gchar *before = NULL;
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	if (!level_required (self, error))
		return -1;
	if (name == NULL || *name == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "An area needs a name");
		return -1;
	}
	before = capture (self);
	index = lrg_nav_bake_add_area (self->bake, name, tag != 0 ? tag : free_tag (self->bake), cost);
	if (index < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Cannot add area '%s': the name or tag is taken, or the cost is not positive", name);
		return -1;
	}
	self->sel_area = index;
	self->sel_box = -1;
	commit_now (self, before, "Add Navigation Area", FALSE);
	return index;
}

gboolean
lrg_editor_nav_panel_remove_area (LrgEditorNavPanel *self,
                                  guint              area)
{
	g_autofree gchar *before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	if (current_level (self) == NULL || area >= lrg_nav_bake_get_n_areas (self->bake))
		return FALSE;
	before = capture (self);
	lrg_nav_bake_remove_area (self->bake, area);
	commit_now (self, before, "Remove Navigation Area", FALSE);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_set_area (LrgEditorNavPanel  *self,
                               guint               area,
                               const gchar        *name,
                               guint32             tag,
                               gfloat              cost,
                               GError            **error)
{
	g_autoptr(GPtrArray) areas = NULL;
	g_autoptr(GPtrArray) links = NULL;
	g_autofree gchar    *before = NULL;
	LrgNavBake          *rebuilt;
	AreaRec             *a;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	if (area >= lrg_nav_bake_get_n_areas (self->bake))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No area %u", area);
		return FALSE;
	}
	lists_read (self->bake, &areas, &links);
	a = g_ptr_array_index (areas, area);
	if (name != NULL && *name != '\0' && g_strcmp0 (name, a->name) != 0)
	{
		/* links naming the area follow the rename */
		for (i = 0; i < links->len; i++)
		{
			LinkRec *l = g_ptr_array_index (links, i);

			if (g_strcmp0 (l->area, a->name) == 0)
			{
				g_free (l->area);
				l->area = g_strdup (name);
			}
		}
		g_free (a->name);
		a->name = g_strdup (name);
	}
	if (tag != 0)
		a->tag = tag;
	if (cost > 0.0f)
		a->cost = cost;
	rebuilt = lists_write (self->bake, areas, links, error);
	if (rebuilt == NULL)
	{
		sync_form (self);
		return FALSE;
	}
	before = capture (self);
	install_bake (self, rebuilt);
	commit_now (self, before, "Edit Navigation Area", self->in_gesture);
	return TRUE;
}

gint
lrg_editor_nav_panel_add_area_box (LrgEditorNavPanel  *self,
                                   guint               area,
                                   const gfloat       *min,
                                   const gfloat       *max,
                                   GError            **error)
{
	g_autofree gchar *before = NULL;
	gfloat            mn[3], mx[3];
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), -1);
	g_return_val_if_fail (min != NULL && max != NULL, -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	if (!level_required (self, error))
		return -1;
	if (area >= lrg_nav_bake_get_n_areas (self->bake))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No area %u", area);
		return -1;
	}
	memcpy (mn, min, sizeof mn);
	memcpy (mx, max, sizeof mx);
	sort_box (mn, mx);
	before = capture (self);
	index = lrg_nav_bake_add_area_box (self->bake, area, mn, mx);
	if (index < 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The box is empty");
		return -1;
	}
	self->sel_area = (gint) area;
	self->sel_box = index;
	commit_now (self, before, "Add Area Box", FALSE);
	return index;
}

gboolean
lrg_editor_nav_panel_remove_area_box (LrgEditorNavPanel *self,
                                      guint              area,
                                      guint              box)
{
	g_autofree gchar *before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	if (current_level (self) == NULL || area >= lrg_nav_bake_get_n_areas (self->bake) ||
	    box >= lrg_nav_bake_get_n_area_boxes (self->bake, area))
		return FALSE;
	before = capture (self);
	lrg_nav_bake_remove_area_box (self->bake, area, box);
	commit_now (self, before, "Remove Area Box", FALSE);
	return TRUE;
}

/* Rewrites one box in the lists and installs the rebuilt settings (no
 * commit: the handles call it live). */
static gboolean
write_box (LrgEditorNavPanel  *self,
           guint               area,
           guint               box,
           const gfloat       *min,
           const gfloat       *max,
           GError            **error)
{
	g_autoptr(GPtrArray) areas = NULL;
	g_autoptr(GPtrArray) links = NULL;
	LrgNavBake          *rebuilt;
	AreaRec             *a;
	gfloat              *b;

	if (area >= lrg_nav_bake_get_n_areas (self->bake) || box >= lrg_nav_bake_get_n_area_boxes (self->bake, area))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No box %u of area %u", box, area);
		return FALSE;
	}
	lists_read (self->bake, &areas, &links);
	a = g_ptr_array_index (areas, area);
	b = box_at (a, box);
	memcpy (b, min, sizeof (gfloat) * 3);
	memcpy (b + 3, max, sizeof (gfloat) * 3);
	sort_box (b, b + 3);
	rebuilt = lists_write (self->bake, areas, links, error);
	if (rebuilt == NULL)
		return FALSE;
	install_bake (self, rebuilt);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_set_area_box (LrgEditorNavPanel  *self,
                                   guint               area,
                                   guint               box,
                                   const gfloat       *min,
                                   const gfloat       *max,
                                   GError            **error)
{
	g_autofree gchar *before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (min != NULL && max != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	before = capture (self);
	if (!write_box (self, area, box, min, max, error))
	{
		sync_form (self);
		return FALSE;
	}
	commit_now (self, before, "Edit Area Box", self->in_gesture);
	return TRUE;
}

gint
lrg_editor_nav_panel_add_link (LrgEditorNavPanel  *self,
                               const gfloat       *start,
                               const gfloat       *end,
                               gboolean            bidirectional,
                               gfloat              cost,
                               const gchar        *area,
                               GError            **error)
{
	g_autofree gchar *before = NULL;
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), -1);
	g_return_val_if_fail (start != NULL && end != NULL, -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	if (!level_required (self, error))
		return -1;
	if (area != NULL && *area == '\0')
		area = NULL;
	before = capture (self);
	index = lrg_nav_bake_add_link (self->bake, start, end, bidirectional, cost, area);
	if (index < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot add the link: a negative cost or an "
		             "unknown area '%s'", area != NULL ? area : "");
		return -1;
	}
	self->sel_link = index;
	commit_now (self, before, "Add Off-mesh Link", FALSE);
	return index;
}

gboolean
lrg_editor_nav_panel_remove_link (LrgEditorNavPanel *self,
                                  guint              index)
{
	g_autofree gchar *before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	if (current_level (self) == NULL || index >= lrg_nav_bake_get_n_links (self->bake))
		return FALSE;
	before = capture (self);
	lrg_nav_bake_remove_link (self->bake, index);
	commit_now (self, before, "Remove Off-mesh Link", FALSE);
	return TRUE;
}

/* Rewrites one link (no commit: the handles call it live). */
static gboolean
write_link (LrgEditorNavPanel  *self,
            guint               index,
            const gfloat       *start,
            const gfloat       *end,
            gboolean            bidirectional,
            gfloat              cost,
            const gchar        *area,
            GError            **error)
{
	g_autoptr(GPtrArray) areas = NULL;
	g_autoptr(GPtrArray) links = NULL;
	LrgNavBake          *rebuilt;
	LinkRec             *l;

	if (index >= lrg_nav_bake_get_n_links (self->bake))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No link %u", index);
		return FALSE;
	}
	lists_read (self->bake, &areas, &links);
	l = g_ptr_array_index (links, index);
	memcpy (l->start, start, sizeof l->start);
	memcpy (l->end, end, sizeof l->end);
	l->bidirectional = bidirectional;
	l->cost = cost;
	g_free (l->area);
	l->area = (area != NULL && *area != '\0') ? g_strdup (area) : NULL;
	rebuilt = lists_write (self->bake, areas, links, error);
	if (rebuilt == NULL)
		return FALSE;
	install_bake (self, rebuilt);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_set_link (LrgEditorNavPanel  *self,
                               guint               index,
                               const gfloat       *start,
                               const gfloat       *end,
                               gboolean            bidirectional,
                               gfloat              cost,
                               const gchar        *area,
                               GError            **error)
{
	g_autofree gchar *before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (start != NULL && end != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	before = capture (self);
	if (!write_link (self, index, start, end, bidirectional, cost, area, error))
	{
		sync_form (self);
		return FALSE;
	}
	commit_now (self, before, "Edit Off-mesh Link", self->in_gesture);
	return TRUE;
}

void
lrg_editor_nav_panel_select (LrgEditorNavPanel *self,
                             gint               area,
                             gint               box,
                             gint               link)
{
	g_return_if_fail (LRG_IS_EDITOR_NAV_PANEL (self));

	self->sel_area = (area >= 0 && (guint) area < lrg_nav_bake_get_n_areas (self->bake)) ? area : -1;
	self->sel_box = (self->sel_area >= 0 && box >= 0 &&
	                 (guint) box < lrg_nav_bake_get_n_area_boxes (self->bake, (guint) self->sel_area)) ? box : -1;
	self->sel_link = (link >= 0 && (guint) link < lrg_nav_bake_get_n_links (self->bake)) ? link : -1;
	self->hot = LRG_EDITOR_NAV_HANDLE_NONE;
	sync_form (self);
	sync_widgets (self);
	queue_viewports (self);
}

void
lrg_editor_nav_panel_get_selection (LrgEditorNavPanel *self,
                                    gint              *area,
                                    gint              *box,
                                    gint              *link)
{
	g_return_if_fail (LRG_IS_EDITOR_NAV_PANEL (self));

	if (area != NULL)
		*area = self->sel_area;
	if (box != NULL)
		*box = self->sel_box;
	if (link != NULL)
		*link = self->sel_link;
}

/* ==========================================================================
 * Settings files
 * ========================================================================== */

LrgNavBake *
lrg_editor_nav_panel_get_settings (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);
	return self->bake;
}

const gchar *
lrg_editor_nav_panel_get_settings_path (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);
	return self->settings_path;
}

gboolean
lrg_editor_nav_panel_get_dirty (LrgEditorNavPanel *self)
{
	g_autofree gchar *mine = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	mine = lrg_nav_bake_to_data (self->bake);
	if (self->settings_path == NULL)
		return g_strcmp0 (mine, self->default_yaml) != 0;
	return self->disk_yaml == NULL || g_strcmp0 (mine, self->disk_yaml) != 0;
}

LrgPropertyGrid *
lrg_editor_nav_panel_get_grid (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);
	return self->grid;
}

/* Loads @path into the live settings and points the level at it (no
 * commit). */
static gboolean
load_settings_file (LrgEditorNavPanel  *self,
                    const gchar        *path,
                    GError            **error)
{
	g_autofree gchar *abs = user_path (self, path);
	g_autofree gchar *ref = NULL;
	LrgNavBake       *loaded;

	if (abs == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "'%s' is relative and the level has no directory yet",
		             path);
		return FALSE;
	}
	loaded = lrg_nav_bake_new_from_file (abs, error);
	if (loaded == NULL)
		return FALSE;
	install_bake (self, loaded);
	ref = level_ref (self, abs);
	lrg_level_set_nav_settings_ref (current_level (self), ref);
	self->sel_area = self->sel_box = self->sel_link = -1;
	return TRUE;
}

gboolean
lrg_editor_nav_panel_open_settings (LrgEditorNavPanel  *self,
                                    const gchar        *path,
                                    GError            **error)
{
	g_autofree gchar *before = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	before = capture (self);
	self->applying = TRUE;
	ok = load_settings_file (self, path, error);
	self->applying = FALSE;
	if (!ok)
		return FALSE;
	commit_now (self, before, "Open Navigation Settings", FALSE);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_new_settings (LrgEditorNavPanel  *self,
                                   const gchar        *path,
                                   GError            **error)
{
	g_autofree gchar     *abs = NULL;
	g_autofree gchar     *dir = NULL;
	g_autofree gchar     *before = NULL;
	g_autofree gchar     *ref = NULL;
	g_autoptr(LrgNavBake) fresh = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	if (path != NULL)
		abs = user_path (self, path);
	else
	{
		g_autofree gchar *stem = level_stem (self);
		g_autofree gchar *ldir = level_dir (self);
		g_autofree gchar *name = NULL;

		if (stem != NULL && lrg_editor_get_path (self->editor) != NULL)
		{
			name = g_strdup_printf ("%s.rnav", stem);
			abs = g_build_filename (ldir, name, NULL);
		}
	}
	if (abs == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Save the level first: navigation settings are written next to it");
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create %s", dir);
		return FALSE;
	}
	fresh = lrg_nav_bake_new ();
	{
		g_autofree gchar *stem = level_stem (self);

		if (stem != NULL)
			lrg_nav_bake_set_name (fresh, stem);
	}
	if (!lrg_nav_bake_save_to_file (fresh, abs, error))
		return FALSE;

	before = capture (self);
	install_bake (self, g_steal_pointer (&fresh));
	ref = level_ref (self, abs);
	self->applying = TRUE;
	lrg_level_set_nav_settings_ref (current_level (self), ref);
	self->applying = FALSE;
	self->sel_area = self->sel_box = self->sel_link = -1;
	commit_now (self, before, "New Navigation Settings", FALSE);
	if (self->settings_combo != NULL)
		sync_widgets (self);
	return TRUE;
}

/* Writes the settings where the level points (creating <stem>.rnav and the
 * reference when it points nowhere).  The reference change, when there is
 * one, is left for the caller's commit. */
static gboolean
write_settings (LrgEditorNavPanel  *self,
                GError            **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *dir = NULL;

	if (self->settings_path != NULL)
		path = g_strdup (self->settings_path);
	else
	{
		g_autofree gchar *stem = level_stem (self);
		g_autofree gchar *ldir = level_dir (self);
		g_autofree gchar *name = NULL;

		if (stem == NULL || ldir == NULL)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                     "Save the level first: navigation is written next to it");
			return FALSE;
		}
		name = g_strdup_printf ("%s.rnav", stem);
		path = g_build_filename (ldir, name, NULL);
	}
	dir = g_path_get_dirname (path);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create %s", dir);
		return FALSE;
	}
	if (!lrg_nav_bake_save_to_file (self->bake, path, error))
		return FALSE;
	if (self->settings_path == NULL)
	{
		g_autofree gchar *ref = level_ref (self, path);

		lrg_level_set_nav_settings_ref (current_level (self), ref);
	}
	/* the file now holds the in-memory text */
	g_free (self->disk_for);
	self->disk_for = g_strdup (path);
	g_free (self->disk_yaml);
	self->disk_yaml = lrg_nav_bake_to_data (self->bake);
	g_free (self->settings_path);
	self->settings_path = g_steal_pointer (&path);
	return TRUE;
}

/* ==========================================================================
 * The mesh
 * ========================================================================== */

LrgNavPolyMesh *
lrg_editor_nav_panel_get_mesh (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);
	return self->mesh;
}

/* Follows the level's data reference: (re)loads it when it changed, drops
 * the mesh when the reference went away (undo of a save, clear) unless a
 * bake is unsaved. */
static void
refresh_mesh (LrgEditorNavPanel *self)
{
	LrgLevel         *level = current_level (self);
	const gchar      *ref = (level != NULL) ? lrg_level_get_nav_data_ref (level) : NULL;
	g_autofree gchar *path = resolve_ref (self, ref);
	g_autofree gchar *bytes = NULL;
	gsize             len = 0;

	if (self->mesh_unsaved)
		return;
	if (path == NULL)
	{
		if (self->mesh != NULL)
		{
			g_clear_object (&self->mesh);
			g_clear_pointer (&self->mesh_from, g_free);
		}
		return;
	}
	if (self->mesh != NULL && g_strcmp0 (path, self->mesh_from) == 0)
	{
		/* costs are not stored with the mesh: keep them current */
		lrg_nav_bake_apply_costs (self->bake, self->mesh);
		return;
	}
	g_clear_object (&self->mesh);
	g_free (self->mesh_from);
	self->mesh_from = g_strdup (path);
	if (g_file_get_contents (path, &bytes, &len, NULL))
	{
		g_autoptr(GBytes)         data = g_bytes_new_take (g_steal_pointer (&bytes), len);
		g_autoptr(LrgNavPolyMesh) mesh = lrg_nav_poly_mesh_new ();

		if (lrg_nav_poly_mesh_load (mesh, data, NULL))
		{
			lrg_nav_bake_apply_costs (self->bake, mesh);
			self->mesh = g_steal_pointer (&mesh);
		}
	}
}

/*
 * The level in a private world, ready for a bake: mesh assets as absolute
 * paths (the bake reads glTF headless), the level's own navigation block
 * off while instantiating (no bake on load inside the bake).
 */
static LrgWorld3D *
bake_prepare (LrgEditorNavPanel  *self,
              GError            **error)
{
	LrgLevel              *level = current_level (self);
	g_autoptr(LrgWorld3D)  world = NULL;
	g_autoptr(GPtrArray)   comps = NULL;
	g_autofree gchar      *dir = NULL;
	g_autofree gchar      *keep = NULL;
	LrgWorld3DRenderer    *runtime = NULL;
	gboolean               ok;
	guint                  i;

	if (level == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No level is open");
		return NULL;
	}
	world = lrg_world3d_new ();
	dir = level_dir (self);
	self->applying = TRUE;
	keep = nav_state_capture (level, NULL);
	lrg_level_clear_navigation (level);
	ok = lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, error);
	nav_state_apply_level (level, keep);
	self->applying = FALSE;
	if (!ok)
		return NULL;

	if (self->app != NULL && self->app->renderer != NULL)
		runtime = lrg_editor_scene_renderer_get_runtime_renderer (self->app->renderer);
	comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_MESH_RENDERER_COMPONENT);
	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		LrgMeshRendererComponent *mr = g_ptr_array_index (comps, i);
		const gchar              *asset = lrg_mesh_renderer_component_get_mesh_asset (mr);
		g_autofree gchar         *abs = NULL;

		if (asset == NULL || *asset == '\0' || g_path_is_absolute (asset))
			continue;
		abs = resolve_ref (self, asset);
		if ((abs == NULL || !g_file_test (abs, G_FILE_TEST_EXISTS)) && runtime != NULL)
		{
			g_free (abs);
			abs = lrg_world3d_renderer_resolve_asset (runtime, asset);
		}
		if (abs != NULL && g_file_test (abs, G_FILE_TEST_EXISTS))
			lrg_mesh_renderer_component_set_mesh_asset (mr, abs);
	}
	/* entities register as they enter; one step settles the rest */
	lrg_world3d_step (world, 0.0f);
	return g_steal_pointer (&world);
}

/* A new bake result: shown at once, saved when asked. */
static void
take_mesh (LrgEditorNavPanel *self,
           LrgNavPolyMesh    *mesh,
           guint              triangles,
           gdouble            seconds)
{
	g_set_object (&self->mesh, mesh);
	g_clear_pointer (&self->mesh_from, g_free);
	self->mesh_unsaved = TRUE;
	self->last_triangles = triangles;
	self->last_seconds = seconds;
	self->bake_state = BAKE_DONE;
	g_clear_pointer (&self->error, g_free);
	if (self->overlay == LRG_NAV_DEBUG_NONE)
		lrg_editor_nav_panel_set_overlay (self, self->overlay_last != 0 ? self->overlay_last : OVERLAY_DEFAULT);
	if (self->auto_save && self->editor != NULL && lrg_editor_get_path (self->editor) != NULL)
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_nav_panel_save (self, &error) && self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Navigation not saved: %s", error->message);
	}
	if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_SUCCESS,
		                   "Navigation baked in %.0f ms: %u polygons, %u regions, %u triangles%s", seconds * 1000.0,
		                   lrg_nav_poly_mesh_get_polygon_count (mesh), lrg_nav_poly_mesh_get_region_count (mesh),
		                   triangles, self->mesh_unsaved ? " (not saved yet)" : "");
	sync_widgets (self);
	queue_viewports (self);
}

static void
bake_failed (LrgEditorNavPanel *self,
             const gchar       *message)
{
	self->bake_state = BAKE_FAILED;
	g_free (self->error);
	self->error = g_strdup (message);
	if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Navigation bake failed: %s", message);
	sync_widgets (self);
}

gboolean
lrg_editor_nav_panel_bake (LrgEditorNavPanel  *self,
                           GError            **error)
{
	g_autoptr(LrgWorld3D)     world = NULL;
	g_autoptr(LrgNavBake)     copy = NULL;
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         local = NULL;
	gint64                    t0;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (lrg_editor_nav_panel_get_baking (self))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY, "A navigation bake is already running");
		return FALSE;
	}
	t0 = g_get_monotonic_time ();
	world = bake_prepare (self, &local);
	if (world != NULL)
	{
		copy = lrg_nav_bake_copy (self->bake);
		mesh = lrg_nav_bake_bake (copy, world, NULL, &local);
	}
	if (mesh == NULL)
	{
		bake_failed (self, local->message);
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}
	take_mesh (self, mesh, lrg_nav_bake_get_last_triangle_count (copy),
	           (gdouble) (g_get_monotonic_time () - t0) / (gdouble) G_USEC_PER_SEC);
	return TRUE;
}

/* The worker's input and output. */
typedef struct
{
	LrgNavBake     *bake;      /* a copy */
	LrgWorld3D     *world;
	guint           serial;
	gint64          started;
	guint           triangles;
} BakeJob;

static void
bake_job_free (gpointer data)
{
	BakeJob *job = data;

	g_clear_object (&job->bake);
	g_clear_object (&job->world);
	g_free (job);
}

static void
bake_thread (GTask        *task,
             gpointer      source,
             gpointer      task_data,
             GCancellable *cancellable)
{
	BakeJob        *job = task_data;
	GError         *error = NULL;
	LrgNavPolyMesh *mesh = lrg_nav_bake_bake (job->bake, job->world, NULL, &error);

	if (mesh == NULL)
	{
		g_task_return_error (task, error);
		return;
	}
	job->triangles = lrg_nav_bake_get_last_triangle_count (job->bake);
	g_task_return_pointer (task, mesh, g_object_unref);
}

static void
on_bake_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	LrgEditorNavPanel        *self = LRG_EDITOR_NAV_PANEL (source);
	BakeJob                  *job = g_task_get_task_data (G_TASK (result));
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgNavPolyMesh) mesh = g_task_propagate_pointer (G_TASK (result), &error);

	/* a cancelled or superseded bake: drop it */
	if (job->serial != self->bake_serial || self->bake_state != BAKE_RUNNING)
		return;
	if (mesh == NULL)
	{
		bake_failed (self, error != NULL ? error->message : "unknown error");
		return;
	}
	take_mesh (self, mesh, job->triangles,
	           (gdouble) (g_get_monotonic_time () - job->started) / (gdouble) G_USEC_PER_SEC);
}

gboolean
lrg_editor_nav_panel_bake_async (LrgEditorNavPanel  *self,
                                 GError            **error)
{
	g_autoptr(GTask) task = NULL;
	BakeJob         *job;
	LrgWorld3D      *world;
	gint64           t0;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (lrg_editor_nav_panel_get_baking (self))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY, "A navigation bake is already running");
		return FALSE;
	}
	t0 = g_get_monotonic_time ();
	world = bake_prepare (self, error);
	if (world == NULL)
		return FALSE;
	job = g_new0 (BakeJob, 1);
	job->bake = lrg_nav_bake_copy (self->bake);
	job->world = world;
	job->serial = ++self->bake_serial;
	job->started = t0;
	self->bake_state = BAKE_RUNNING;
	self->bake_started = t0;
	g_clear_pointer (&self->error, g_free);
	task = g_task_new (self, NULL, on_bake_done, NULL);
	g_task_set_task_data (task, job, bake_job_free);
	g_task_run_in_thread (task, bake_thread);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Baking navigation...");
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_cancel (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	if (self->bake_state != BAKE_RUNNING)
		return FALSE;
	self->bake_serial++;
	self->bake_state = BAKE_CANCELLED;
	if (self->app != NULL)
		editor_app_set_status (self->app, "Navigation bake cancelled");
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_get_baking (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	return self->bake_state == BAKE_RUNNING;
}

guint
lrg_editor_nav_panel_get_last_triangle_count (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), 0);
	return self->last_triangles;
}

gdouble
lrg_editor_nav_panel_get_last_bake_seconds (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), 0.0);
	return self->last_seconds;
}

const gchar *
lrg_editor_nav_panel_get_error (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);
	return self->error;
}

gboolean
lrg_editor_nav_panel_has_unsaved (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	return self->mesh != NULL && self->mesh_unsaved;
}

void
lrg_editor_nav_panel_set_auto_save (LrgEditorNavPanel *self,
                                    gboolean           auto_save)
{
	g_return_if_fail (LRG_IS_EDITOR_NAV_PANEL (self));
	self->auto_save = !!auto_save;
}

gboolean
lrg_editor_nav_panel_get_auto_save (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	return self->auto_save;
}

gboolean
lrg_editor_nav_panel_save (LrgEditorNavPanel  *self,
                           GError            **error)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *mesh_path = NULL;
	g_autofree gchar *output = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!level_required (self, error))
		return FALSE;
	if (lrg_editor_get_path (self->editor) == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Save the level first: navigation is written next to it");
		return FALSE;
	}
	before = capture (self);
	self->applying = TRUE;
	if (!write_settings (self, error))
	{
		nav_state_apply_level (current_level (self), before);
		self->applying = FALSE;
		return FALSE;
	}

	/* the mesh: the settings' output (relative to the .rnav), else
	 * <stem>.rnavmesh next to the level */
	if (self->mesh != NULL && lrg_nav_poly_mesh_get_is_baked (self->mesh))
	{
		g_autoptr(GBytes) bytes = lrg_nav_poly_mesh_save (self->mesh);
		g_autofree gchar *dir = NULL;
		g_autofree gchar *ref = NULL;

		g_object_get (self->bake, "output", &output, NULL);
		if (output != NULL && *output != '\0')
		{
			g_autofree gchar *sdir = g_path_get_dirname (self->settings_path);

			mesh_path = g_path_is_absolute (output) ? g_strdup (output) : g_build_filename (sdir, output, NULL);
		}
		else
		{
			g_autofree gchar *stem = level_stem (self);
			g_autofree gchar *ldir = level_dir (self);
			g_autofree gchar *name = g_strdup_printf ("%s.rnavmesh", stem);

			mesh_path = g_build_filename (ldir, name, NULL);
		}
		dir = g_path_get_dirname (mesh_path);
		if (g_mkdir_with_parents (dir, 0755) != 0)
		{
			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create %s", dir);
			nav_state_apply_level (current_level (self), before);
			self->applying = FALSE;
			return FALSE;
		}
		if (!g_file_set_contents (mesh_path, g_bytes_get_data (bytes, NULL), (gssize) g_bytes_get_size (bytes),
		                          error))
		{
			nav_state_apply_level (current_level (self), before);
			self->applying = FALSE;
			return FALSE;
		}
		ref = level_ref (self, mesh_path);
		lrg_level_set_nav_data_ref (current_level (self), ref);
		self->mesh_unsaved = FALSE;
		g_free (self->mesh_from);
		self->mesh_from = g_strdup (mesh_path);
	}
	self->applying = FALSE;
	commit_now (self, before, "Save Navigation", FALSE);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Saved navigation to %s%s%s", self->settings_path,
		                       mesh_path != NULL ? " and " : "", mesh_path != NULL ? mesh_path : "");
	return TRUE;
}

gboolean
lrg_editor_nav_panel_clear (LrgEditorNavPanel *self)
{
	g_autofree gchar *before = NULL;
	gboolean          had;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	had = self->mesh != NULL;
	g_clear_object (&self->mesh);
	g_clear_pointer (&self->mesh_from, g_free);
	self->mesh_unsaved = FALSE;
	if (current_level (self) != NULL)
	{
		before = capture (self);
		self->applying = TRUE;
		lrg_level_set_nav_data_ref (current_level (self), NULL);
		self->applying = FALSE;
		had = commit_now (self, before, "Clear Baked Navigation", FALSE) || had;
	}
	sync_widgets (self);
	queue_viewports (self);
	return had;
}

/* Play starts: an unsaved (or never referenced) bake goes into the play
 * world, so the game walks what the panel shows. */
static void
push_mesh_to_play (LrgEditorNavPanel *self)
{
	LrgWorld3D               *world;
	g_autoptr(GBytes)         bytes = NULL;
	g_autoptr(LrgNavPolyMesh) copy = NULL;

	if (self->mesh == NULL || !self->mesh_unsaved || self->play == NULL)
		return;
	world = lrg_editor_play_session_get_world3d (self->play);
	if (world == NULL)
		return;
	bytes = lrg_nav_poly_mesh_save (self->mesh);
	copy = lrg_nav_poly_mesh_new ();
	if (!lrg_nav_poly_mesh_load (copy, bytes, NULL))
		return;
	lrg_nav_bake_apply_costs (self->bake, copy);
	lrg_world3d_set_navigation (world, copy);
}

/* ==========================================================================
 * The overlay
 * ========================================================================== */

/* Every viewport draws the next frame again. */
static void
queue_viewports (LrgEditorNavPanel *self)
{
	guint i;

	if (self->app == NULL || self->app->viewport_split == NULL)
		return;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewportPanel *vp = lrg_editor_viewport_split_get_panel (self->app->viewport_split, i);

		if (vp != NULL)
			lrg_editor_viewport_panel_queue_render (vp);
	}
}

void
lrg_editor_nav_panel_set_overlay (LrgEditorNavPanel *self,
                                  LrgNavDebugFlags   flags)
{
	g_return_if_fail (LRG_IS_EDITOR_NAV_PANEL (self));

	self->overlay = flags & LRG_NAV_DEBUG_ALL;
	if (self->overlay != LRG_NAV_DEBUG_NONE)
		self->overlay_last = self->overlay;
	if (self->overlay_chip != NULL)
		editor_chip_set_checked (self->overlay_chip, self->overlay != LRG_NAV_DEBUG_NONE);
	queue_viewports (self);
}

LrgNavDebugFlags
lrg_editor_nav_panel_get_overlay (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), LRG_NAV_DEBUG_NONE);
	return self->overlay;
}

void
lrg_editor_nav_panel_set_show_handles (LrgEditorNavPanel *self,
                                       gboolean           show)
{
	g_return_if_fail (LRG_IS_EDITOR_NAV_PANEL (self));
	self->show_handles = !!show;
	queue_viewports (self);
}

gboolean
lrg_editor_nav_panel_get_show_handles (LrgEditorNavPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	return self->show_handles;
}

/* The settings' area boxes (wire, area colour; the selected one solid
 * and brighter) and links (arcs with arrows, cyan; area links in the
 * area colour). */
static void
record_settings (LrgEditorNavPanel *self,
                 LrgDebugDraw      *draw)
{
	const GrlVector3 up = { 0.0f, 1.0f, 0.0f };
	guint            i, k;

	for (i = 0; i < lrg_nav_bake_get_n_areas (self->bake); i++)
	{
		guint32 tag = 0;

		lrg_nav_bake_get_area (self->bake, i, NULL, &tag, NULL);
		for (k = 0; k < lrg_nav_bake_get_n_area_boxes (self->bake, i); k++)
		{
			gfloat     mn[3], mx[3];
			GrlVector3 a, b;
			gboolean   sel = (gint) i == self->sel_area && (gint) k == self->sel_box;
			GrlColor   c = area_color (tag, sel ? 255 : 200);

			lrg_nav_bake_get_area_box (self->bake, i, k, mn, mx);
			a = v3 (mn);
			b = v3 (mx);
			lrg_debug_draw_box (draw, &a, &b, &c, 0.0f);
			if (sel)
			{
				GrlColor fill = area_color (tag, 50);

				lrg_debug_draw_set_solid (draw, TRUE);
				lrg_debug_draw_box (draw, &a, &b, &fill, 0.0f);
				lrg_debug_draw_set_solid (draw, FALSE);
			}
		}
	}
	for (i = 0; i < lrg_nav_bake_get_n_links (self->bake); i++)
	{
		gfloat       s[3], e[3], mid[3];
		gboolean     bidir = TRUE;
		const gchar *area = NULL;
		GrlColor     c = rgba (60, 220, 230, (gint) i == self->sel_link ? 255 : 210);
		GrlVector3   pts[9];
		guint        n;
		gfloat       len;

		lrg_nav_bake_get_link (self->bake, i, s, e, &bidir, NULL, &area);
		if (area != NULL && lrg_nav_bake_find_area (self->bake, area) >= 0)
		{
			guint32 tag = 0;

			lrg_nav_bake_get_area (self->bake, (guint) lrg_nav_bake_find_area (self->bake, area), NULL, &tag, NULL);
			c = area_color (tag, 255);
		}
		/* a parabola-ish arc over the midpoint */
		len = sqrtf ((e[0] - s[0]) * (e[0] - s[0]) + (e[1] - s[1]) * (e[1] - s[1]) + (e[2] - s[2]) * (e[2] - s[2]));
		for (n = 0; n < G_N_ELEMENTS (pts); n++)
		{
			gfloat t = (gfloat) n / (gfloat) (G_N_ELEMENTS (pts) - 1);

			mid[0] = s[0] + (e[0] - s[0]) * t;
			mid[1] = s[1] + (e[1] - s[1]) * t + 4.0f * t * (1.0f - t) * MAX (len * 0.25f, 0.3f);
			mid[2] = s[2] + (e[2] - s[2]) * t;
			pts[n] = v3 (mid);
		}
		lrg_debug_draw_polyline (draw, pts, G_N_ELEMENTS (pts), FALSE, &c, 0.0f);
		lrg_debug_draw_arrow (draw, &pts[G_N_ELEMENTS (pts) - 2], &pts[G_N_ELEMENTS (pts) - 1], 0.2f, &c, 0.0f);
		if (bidir)
			lrg_debug_draw_arrow (draw, &pts[1], &pts[0], 0.2f, &c, 0.0f);
		lrg_debug_draw_circle (draw, &pts[0], &up, 0.25f, &c, 0.0f);
		lrg_debug_draw_circle (draw, &pts[G_N_ELEMENTS (pts) - 1], &up, 0.25f, &c, 0.0f);
	}
}

guint
lrg_editor_nav_panel_record_overlay (LrgEditorNavPanel *self,
                                     LrgDebugDraw      *draw)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), 0);
	g_return_val_if_fail (LRG_IS_DEBUG_DRAW (draw), 0);

	lrg_debug_draw_clear (draw);
	if (self->overlay == LRG_NAV_DEBUG_NONE)
		return 0;
	if (is_playing (self))
	{
		LrgWorld3D *world = lrg_editor_play_session_get_world3d (self->play);

		if (world != NULL)
			lrg_world3d_debug_draw_navigation (world, draw, self->overlay);
		return lrg_debug_draw_get_count (draw);
	}
	if (current_level (self) == NULL)
		return 0;
	if (self->mesh != NULL)
		lrg_nav_poly_mesh_debug_draw (self->mesh, draw, self->overlay & MESH_FLAGS);
	lrg_debug_draw_push (draw);
	lrg_debug_draw_set_category (draw, LRG_DEBUG_CATEGORY_NAVIGATION);
	record_settings (self, draw);
	lrg_debug_draw_pop (draw);
	return lrg_debug_draw_get_count (draw);
}

/* The runtime renderer's "draw-overlay": inside the 3D pass of a viewport
 * render (authoring and play), record and draw the overlay. */
static void
on_runtime_draw_overlay (GObject  *runtime,
                         gpointer  view,
                         gpointer  user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	if (self->overlay == LRG_NAV_DEBUG_NONE)
		return;
	if (lrg_editor_nav_panel_record_overlay (self, self->draw) > 0)
		lrg_debug_draw_draw_3d (self->draw);
	lrg_debug_draw_end_frame (self->draw);
}

/* ==========================================================================
 * Handles (the selected area box and link in the viewport)
 * ========================================================================== */

gboolean
lrg_editor_nav_panel_get_handle_position (LrgEditorNavPanel  *self,
                                          LrgEditorNavHandle  handle,
                                          gfloat             *out)
{
	gfloat mn[3], mx[3], s[3], e[3];

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	switch (handle)
	{
	case LRG_EDITOR_NAV_HANDLE_BOX_MIN:
	case LRG_EDITOR_NAV_HANDLE_BOX_MAX:
	case LRG_EDITOR_NAV_HANDLE_BOX_CENTER:
		if (self->sel_area < 0 || self->sel_box < 0 ||
		    !lrg_nav_bake_get_area_box (self->bake, (guint) self->sel_area, (guint) self->sel_box, mn, mx))
			return FALSE;
		/* on the box's mid height, where the floor usually is */
		out[1] = (mn[1] + mx[1]) * 0.5f;
		if (handle == LRG_EDITOR_NAV_HANDLE_BOX_MIN)
		{
			out[0] = mn[0];
			out[2] = mn[2];
		}
		else if (handle == LRG_EDITOR_NAV_HANDLE_BOX_MAX)
		{
			out[0] = mx[0];
			out[2] = mx[2];
		}
		else
		{
			out[0] = (mn[0] + mx[0]) * 0.5f;
			out[2] = (mn[2] + mx[2]) * 0.5f;
		}
		return TRUE;
	case LRG_EDITOR_NAV_HANDLE_LINK_START:
	case LRG_EDITOR_NAV_HANDLE_LINK_END:
		if (self->sel_link < 0 || !lrg_nav_bake_get_link (self->bake, (guint) self->sel_link, s, e, NULL, NULL, NULL))
			return FALSE;
		memcpy (out, handle == LRG_EDITOR_NAV_HANDLE_LINK_START ? s : e, sizeof (gfloat) * 3);
		return TRUE;
	case LRG_EDITOR_NAV_HANDLE_NONE:
	default:
		return FALSE;
	}
}

static gboolean
handles_active (LrgEditorNavPanel *self)
{
	return self->show_handles && !is_playing (self) && current_level (self) != NULL;
}

LrgEditorNavHandle
lrg_editor_nav_panel_hit_test (LrgEditorNavPanel   *self,
                               const LrgEditorView *view,
                               gfloat               u,
                               gfloat               v)
{
	static const LrgEditorNavHandle order[] = {
		LRG_EDITOR_NAV_HANDLE_LINK_START, LRG_EDITOR_NAV_HANDLE_LINK_END, LRG_EDITOR_NAV_HANDLE_BOX_MIN,
		LRG_EDITOR_NAV_HANDLE_BOX_MAX, LRG_EDITOR_NAV_HANDLE_BOX_CENTER
	};
	LrgEditorNavHandle best = LRG_EDITOR_NAV_HANDLE_NONE;
	gfloat             best_d = HANDLE_HIT;
	guint              i;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), LRG_EDITOR_NAV_HANDLE_NONE);
	g_return_val_if_fail (view != NULL, LRG_EDITOR_NAV_HANDLE_NONE);

	if (!handles_active (self))
		return LRG_EDITOR_NAV_HANDLE_NONE;
	for (i = 0; i < G_N_ELEMENTS (order); i++)
	{
		gfloat p[3], sx, sy, d;

		if (!lrg_editor_nav_panel_get_handle_position (self, order[i], p) ||
		    !lrg_editor_view_project (view, p, &sx, &sy, NULL))
			continue;
		d = sqrtf ((sx - u) * (sx - u) + (sy - v) * (sy - v));
		if (d <= best_d)
		{
			best_d = d;
			best = order[i];
		}
	}
	return best;
}

/* The point under (@u, @v) on the horizontal plane at @y. */
static gboolean
plane_point (const LrgEditorView *view,
             gfloat               u,
             gfloat               v,
             gfloat               y,
             gfloat              *out)
{
	gfloat o[3], d[3], t = 0.0f;
	gfloat p[3] = { 0.0f, 0.0f, 0.0f };
	gfloat n[3] = { 0.0f, 1.0f, 0.0f };

	p[1] = y;
	lrg_editor_view_get_ray (view, u, v, o, d);
	if (!lrg_editor_ray_plane (o, d, p, n, &t))
		return FALSE;
	out[0] = o[0] + d[0] * t;
	out[1] = y;
	out[2] = o[2] + d[2] * t;
	return TRUE;
}

/* Writes the dragged handle's new position into the settings (live, no
 * commit). */
static void
drag_to (LrgEditorNavPanel   *self,
         const LrgEditorView *view,
         gfloat               u,
         gfloat               v,
         gboolean             snap)
{
	gfloat p[3];
	gint   k;

	if (!plane_point (view, u, v, self->drag_y, p))
		return;
	if (self->drag == LRG_EDITOR_NAV_HANDLE_BOX_CENTER)
	{
		p[0] -= self->drag_grab[0];
		p[2] -= self->drag_grab[2];
	}
	if (snap)
		for (k = 0; k < 3; k += 2)
			p[k] = roundf (p[k] / SNAP_STEP) * SNAP_STEP;

	self->applying = TRUE;
	switch (self->drag)
	{
	case LRG_EDITOR_NAV_HANDLE_BOX_MIN:
	case LRG_EDITOR_NAV_HANDLE_BOX_MAX:
	case LRG_EDITOR_NAV_HANDLE_BOX_CENTER:
		{
			gfloat mn[3], mx[3];

			if (!lrg_nav_bake_get_area_box (self->bake, (guint) self->sel_area, (guint) self->sel_box, mn, mx))
				break;
			if (self->drag == LRG_EDITOR_NAV_HANDLE_BOX_MIN)
			{
				/* a corner never passes the other one */
				mn[0] = MIN (p[0], mx[0] - 0.05f);
				mn[2] = MIN (p[2], mx[2] - 0.05f);
			}
			else if (self->drag == LRG_EDITOR_NAV_HANDLE_BOX_MAX)
			{
				mx[0] = MAX (p[0], mn[0] + 0.05f);
				mx[2] = MAX (p[2], mn[2] + 0.05f);
			}
			else
			{
				gfloat hx = (mx[0] - mn[0]) * 0.5f;
				gfloat hz = (mx[2] - mn[2]) * 0.5f;

				mn[0] = p[0] - hx;
				mx[0] = p[0] + hx;
				mn[2] = p[2] - hz;
				mx[2] = p[2] + hz;
			}
			write_box (self, (guint) self->sel_area, (guint) self->sel_box, mn, mx, NULL);
		}
		break;
	case LRG_EDITOR_NAV_HANDLE_LINK_START:
	case LRG_EDITOR_NAV_HANDLE_LINK_END:
		{
			gfloat       s[3], e[3], cost = 0.0f;
			gboolean     bidir = TRUE;
			const gchar *area = NULL;
			g_autofree gchar *area_copy = NULL;

			if (!lrg_nav_bake_get_link (self->bake, (guint) self->sel_link, s, e, &bidir, &cost, &area))
				break;
			area_copy = g_strdup (area);
			if (self->drag == LRG_EDITOR_NAV_HANDLE_LINK_START)
			{
				s[0] = p[0];
				s[2] = p[2];
			}
			else
			{
				e[0] = p[0];
				e[2] = p[2];
			}
			write_link (self, (guint) self->sel_link, s, e, bidir, cost, area_copy, NULL);
		}
		break;
	case LRG_EDITOR_NAV_HANDLE_NONE:
	default:
		break;
	}
	self->applying = FALSE;
	sync_form (self);
	queue_viewports (self);
}

gboolean
lrg_editor_nav_panel_handle_press (LrgEditorNavPanel   *self,
                                   const LrgEditorView *view,
                                   gint                 button,
                                   gfloat               u,
                                   gfloat               v)
{
	LrgEditorNavHandle h;
	gfloat             p[3], hit[3];

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	/* another button during a drag puts the value back */
	if (self->drag != LRG_EDITOR_NAV_HANDLE_NONE)
	{
		if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
		{
			self->drag = LRG_EDITOR_NAV_HANDLE_NONE;
			if (self->drag_before != NULL)
				nav_proxy_applied (self, current_level (self), self->drag_before);
			g_clear_pointer (&self->drag_before, g_free);
			if (self->app != NULL)
				editor_app_set_status (self->app, "Handle drag cancelled");
		}
		return TRUE;
	}
	if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
		return FALSE;
	h = lrg_editor_nav_panel_hit_test (self, view, u, v);
	if (h == LRG_EDITOR_NAV_HANDLE_NONE || !lrg_editor_nav_panel_get_handle_position (self, h, p))
		return FALSE;
	self->drag = h;
	self->hot = h;
	self->drag_y = p[1];
	memset (self->drag_grab, 0, sizeof self->drag_grab);
	if (h == LRG_EDITOR_NAV_HANDLE_BOX_CENTER && plane_point (view, u, v, p[1], hit))
	{
		self->drag_grab[0] = hit[0] - p[0];
		self->drag_grab[2] = hit[2] - p[2];
	}
	g_free (self->drag_before);
	self->drag_before = capture (self);
	return TRUE;
}

gboolean
lrg_editor_nav_panel_handle_motion (LrgEditorNavPanel   *self,
                                    const LrgEditorView *view,
                                    gfloat               u,
                                    gfloat               v,
                                    gboolean             snap,
                                    gboolean            *repaint)
{
	LrgEditorNavHandle hot;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	if (repaint != NULL)
		*repaint = FALSE;
	if (self->drag != LRG_EDITOR_NAV_HANDLE_NONE)
	{
		drag_to (self, view, u, v, snap);
		return TRUE;
	}
	hot = lrg_editor_nav_panel_hit_test (self, view, u, v);
	if (hot != self->hot)
	{
		self->hot = hot;
		if (repaint != NULL)
			*repaint = TRUE;
	}
	return FALSE;
}

gboolean
lrg_editor_nav_panel_handle_release (LrgEditorNavPanel   *self,
                                     const LrgEditorView *view,
                                     gfloat               u,
                                     gfloat               v,
                                     gboolean             snap)
{
	static const gchar * const labels[] = {
		NULL, "Resize Area Box", "Resize Area Box", "Move Area Box", "Move Link Start", "Move Link End"
	};
	g_autofree gchar *before = NULL;
	LrgEditorNavHandle h;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	if (self->drag == LRG_EDITOR_NAV_HANDLE_NONE)
		return FALSE;
	drag_to (self, view, u, v, snap);
	h = self->drag;
	self->drag = LRG_EDITOR_NAV_HANDLE_NONE;
	before = g_steal_pointer (&self->drag_before);
	commit_now (self, before, labels[h], FALSE);
	return TRUE;
}

/* A round handle: filled when hot, a ring otherwise. */
static void
paint_handle (LrgPaintList   *list,
              LrgTheme       *theme,
              gfloat          x,
              gfloat          y,
              gboolean        hot,
              const GrlColor *color)
{
	gfloat   r = hot ? HANDLE_RADIUS + 2.0f : HANDLE_RADIUS;
	GrlColor shadow = rgba (0, 0, 0, 140);

	lrg_paint_list_circle (list, x, y + 1.0f, r + 1.5f, &shadow);
	lrg_paint_list_circle (list, x, y, r, hot ? color : lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x - r, y - r, r * 2.0f, r * 2.0f, r, 2.0f, color);
}

guint
lrg_editor_nav_panel_paint_handles (LrgEditorNavPanel   *self,
                                    LrgPaintList        *list,
                                    LrgTheme            *theme,
                                    const LrgEditorView *view,
                                    gfloat               ox,
                                    gfloat               oy,
                                    gfloat               scale)
{
	LrgEditorNavHandle shown = (self->drag != LRG_EDITOR_NAV_HANDLE_NONE) ? self->drag : self->hot;
	guint              n = 0;
	gfloat             mn[3], mx[3];

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), 0);
	g_return_val_if_fail (list != NULL && theme != NULL && view != NULL, 0);

	if (!handles_active (self))
		return 0;

	/* The selected box: its 12 edges, then the corner and centre handles */
	if (self->sel_area >= 0 && self->sel_box >= 0 &&
	    lrg_nav_bake_get_area_box (self->bake, (guint) self->sel_area, (guint) self->sel_box, mn, mx))
	{
		static const guint8 edges[12][2] = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};
		guint32  tag = 0;
		GrlColor c;
		gfloat   sx[8], sy[8];
		gboolean ok[8];
		guint    i;
		static const LrgEditorNavHandle hs[] = {
			LRG_EDITOR_NAV_HANDLE_BOX_MIN, LRG_EDITOR_NAV_HANDLE_BOX_MAX, LRG_EDITOR_NAV_HANDLE_BOX_CENTER
		};

		lrg_nav_bake_get_area (self->bake, (guint) self->sel_area, NULL, &tag, NULL);
		c = area_color (tag, 255);
		for (i = 0; i < 8; i++)
		{
			gfloat p[3];

			p[0] = (i & 1) ? mx[0] : mn[0];
			p[1] = (i & 4) ? mx[1] : mn[1];
			p[2] = (i & 2) ? mx[2] : mn[2];
			ok[i] = lrg_editor_view_project (view, p, &sx[i], &sy[i], NULL);
		}
		for (i = 0; i < 12; i++)
			if (ok[edges[i][0]] && ok[edges[i][1]])
				lrg_paint_list_line (list, ox + sx[edges[i][0]] * scale, oy + sy[edges[i][0]] * scale,
				                     ox + sx[edges[i][1]] * scale, oy + sy[edges[i][1]] * scale, 1.5f, &c);
		for (i = 0; i < G_N_ELEMENTS (hs); i++)
		{
			gfloat p[3], hx, hy;

			if (!lrg_editor_nav_panel_get_handle_position (self, hs[i], p) ||
			    !lrg_editor_view_project (view, p, &hx, &hy, NULL))
				continue;
			paint_handle (list, theme, ox + hx * scale, oy + hy * scale, shown == hs[i], &c);
			n++;
		}
	}

	/* The selected link: a straight guide with its two handles */
	if (self->sel_link >= 0)
	{
		gfloat   s[3], e[3], ssx, ssy, esx, esy;
		gboolean sok, eok;
		GrlColor c = rgba (60, 220, 230, 255);

		if (lrg_nav_bake_get_link (self->bake, (guint) self->sel_link, s, e, NULL, NULL, NULL))
		{
			sok = lrg_editor_view_project (view, s, &ssx, &ssy, NULL);
			eok = lrg_editor_view_project (view, e, &esx, &esy, NULL);
			if (sok && eok)
				lrg_paint_list_line (list, ox + ssx * scale, oy + ssy * scale, ox + esx * scale, oy + esy * scale, 1.5f,
				                     &c);
			if (sok)
			{
				paint_handle (list, theme, ox + ssx * scale, oy + ssy * scale,
				              shown == LRG_EDITOR_NAV_HANDLE_LINK_START, &c);
				lrg_editor_gizmo_paint_chip (list, theme, "start", ox + ssx * scale + 10.0f, oy + ssy * scale - 22.0f,
				                             FALSE, NULL);
				n++;
			}
			if (eok)
			{
				paint_handle (list, theme, ox + esx * scale, oy + esy * scale,
				              shown == LRG_EDITOR_NAV_HANDLE_LINK_END, &c);
				lrg_editor_gizmo_paint_chip (list, theme, "end", ox + esx * scale + 10.0f, oy + esy * scale - 22.0f,
				                             FALSE, NULL);
				n++;
			}
		}
	}
	return n;
}

/* ==========================================================================
 * Play: the crowd and its agents
 * ========================================================================== */

LrgNavCrowd *
lrg_editor_nav_panel_get_crowd (LrgEditorNavPanel *self)
{
	LrgWorld3D *world;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), NULL);

	if (!is_playing (self))
		return NULL;
	world = lrg_editor_play_session_get_world3d (self->play);
	if (world == NULL || lrg_world3d_get_navigation (world) == NULL)
		return NULL;
	return lrg_world3d_get_nav_crowd (world);
}

/* The component (and so the entity) behind each agent of the play world. */
static LrgNavAgentComponent *
component_for_agent (LrgEditorNavPanel *self,
                     LrgNavAgent       *agent)
{
	LrgWorld3D          *world = lrg_editor_play_session_get_world3d (self->play);
	g_autoptr(GPtrArray) comps = (world != NULL) ? lrg_world3d_get_components_of_type (world,
	                                                                                   LRG_TYPE_NAV_AGENT_COMPONENT)
	                                             : NULL;
	guint                i;

	for (i = 0; comps != NULL && i < comps->len; i++)
		if (lrg_nav_agent_component_get_agent (g_ptr_array_index (comps, i)) == agent)
			return g_ptr_array_index (comps, i);
	return NULL;
}

/* An agent's display name: its entity's, else "agent N". */
static gchar *
agent_name (LrgEditorNavPanel *self,
            LrgNavAgent       *agent,
            guint              index,
            LrgNode          **out_node)
{
	LrgNavAgentComponent *comp = component_for_agent (self, agent);
	LrgEntity3D          *entity = (comp != NULL) ? lrg_component_get_entity (LRG_COMPONENT (comp)) : NULL;
	LrgNode              *node = (entity != NULL) ? lrg_editor_play_session_get_node_for_live (self->play,
	                                                                                           G_OBJECT (entity))
	                                              : NULL;

	if (out_node != NULL)
		*out_node = node;
	if (node != NULL)
		return g_strdup (lrg_node_get_name (node));
	if (entity != NULL && lrg_entity3d_get_name (entity) != NULL)
		return g_strdup (lrg_entity3d_get_name (entity));
	return g_strdup_printf ("agent %u", index);
}

gint
lrg_editor_nav_panel_get_debug_agent (LrgEditorNavPanel *self)
{
	LrgNavCrowd *crowd;
	LrgNavAgent *agent;
	guint        i;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), -1);

	crowd = lrg_editor_nav_panel_get_crowd (self);
	agent = (crowd != NULL) ? lrg_nav_crowd_get_debug_agent (crowd) : NULL;
	for (i = 0; agent != NULL && i < lrg_nav_crowd_get_n_agents (crowd); i++)
		if (lrg_nav_crowd_get_agent (crowd, i) == agent)
			return (gint) i;
	return -1;
}

gboolean
lrg_editor_nav_panel_set_debug_agent (LrgEditorNavPanel *self,
                                      gint               index)
{
	LrgNavCrowd *crowd;

	g_return_val_if_fail (LRG_IS_EDITOR_NAV_PANEL (self), FALSE);

	crowd = lrg_editor_nav_panel_get_crowd (self);
	if (crowd == NULL)
		return FALSE;
	if (index < 0)
	{
		lrg_nav_crowd_set_debug_agent (crowd, NULL);
		self->debug_agent = -1;
		return TRUE;
	}
	if ((guint) index >= lrg_nav_crowd_get_n_agents (crowd))
		return FALSE;
	lrg_nav_crowd_set_debug_agent (crowd, lrg_nav_crowd_get_agent (crowd, (guint) index));
	self->debug_agent = index;
	/* the detail views need these flags */
	if (self->overlay != LRG_NAV_DEBUG_NONE)
		lrg_editor_nav_panel_set_overlay (self, self->overlay | LRG_NAV_DEBUG_ORCA | LRG_NAV_DEBUG_NEIGHBOURS);
	return TRUE;
}

/* ==========================================================================
 * Widgets: status pill, summary, lists
 * ========================================================================== */

static const gchar *
status_text (LrgEditorNavPanel *self,
             LrgThemeColor     *role)
{
	if (is_playing (self))
	{
		*role = LRG_THEME_COLOR_ACCENT;
		return lrg_editor_nav_panel_get_crowd (self) != NULL ? "Playing" : "Playing, no mesh";
	}
	if (self->bake_state == BAKE_RUNNING)
	{
		*role = LRG_THEME_COLOR_ACCENT;
		return "Baking";
	}
	if (self->bake_state == BAKE_FAILED)
	{
		*role = LRG_THEME_COLOR_DANGER;
		return "Bake failed";
	}
	if (lrg_editor_nav_panel_has_unsaved (self))
	{
		*role = LRG_THEME_COLOR_WARNING;
		return "Unsaved";
	}
	if (self->mesh != NULL)
	{
		*role = LRG_THEME_COLOR_SUCCESS;
		return "Baked";
	}
	*role = LRG_THEME_COLOR_TEXT_MUTED;
	return "Not baked";
}

static void
pill_measure (LrgCustomWidget     *widget,
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
pill_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgThemeColor      role = LRG_THEME_COLOR_TEXT_MUTED;
	const gchar       *text = status_text (self, &role);
	GrlColor           fill = *lrg_theme_get_color (theme, role);
	GrlColor           ink = fill;
	gfloat             fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat             tw = 0.0f, th = 0.0f;

	fill.a = 46;
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, fs, 0.0f, &tw, &th);
	width = MIN (width, tw + height + 14.0f);
	lrg_paint_list_rect (list, x, y, width, height, height * 0.5f, &fill);
	lrg_paint_list_circle (list, x + height * 0.5f + 2.0f, y + height * 0.5f, 3.0f, &ink);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, roundf (x + height * 0.5f + 9.0f),
	                     roundf (y + (height - th) * 0.5f), fs, 0.0f, &ink);
}

/* Keeps the pill (and the elapsed time) moving while a bake runs or the
 * game plays. */
static gboolean
pill_step (LrgCustomWidget *widget,
           gfloat           delta_time,
           gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	return self->bake_state == BAKE_RUNNING || is_playing (self);
}

static void
summary_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	size->width = 120.0f;
	size->height = SUMMARY_HEIGHT;
}

static void
summary_tile (LrgPaintList *list,
              LrgTheme     *theme,
              gfloat        x,
              gfloat        y,
              gfloat        w,
              gfloat        h,
              const gchar  *value,
              const gchar  *label)
{
	GrlFont *font = lrg_theme_get_ui_font (theme);
	gfloat   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat   fss = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlColor surface = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE);

	lrg_paint_list_rect (list, x, y, w, h, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS), &surface);
	lrg_paint_list_push_clip (list, x, y, w, h);
	lrg_paint_list_text (list, font, value, roundf (x + 8.0f), roundf (y + 5.0f), fs + 2.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	lrg_paint_list_text (list, font, label, roundf (x + 8.0f), roundf (y + h - fss - 5.0f), fss, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

/* Polygons / regions / links / bake time of the shown mesh (the play
 * world's while playing: crowd agents and step time instead). */
static void
summary_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgNavCrowd       *crowd = lrg_editor_nav_panel_get_crowd (self);
	LrgNavPolyMesh    *mesh = self->mesh;
	gchar              v[4][32];
	const gchar       *labels[4] = { "polygons", "regions", "links", "ms bake" };
	gfloat             gap = 6.0f;
	gfloat             tw;
	guint              i;

	if (crowd != NULL)
		mesh = lrg_nav_crowd_get_mesh (crowd);
	if (mesh != NULL)
	{
		g_snprintf (v[0], sizeof v[0], "%u", lrg_nav_poly_mesh_get_polygon_count (mesh));
		g_snprintf (v[1], sizeof v[1], "%u", lrg_nav_poly_mesh_get_region_count (mesh));
		g_snprintf (v[2], sizeof v[2], "%u", lrg_nav_poly_mesh_get_link_count (mesh));
	}
	else
	{
		g_snprintf (v[0], sizeof v[0], "-");
		g_snprintf (v[1], sizeof v[1], "-");
		g_snprintf (v[2], sizeof v[2], "%u", lrg_nav_bake_get_n_links (self->bake));
	}
	if (crowd != NULL)
	{
		labels[2] = "agents";
		labels[3] = "ms step";
		g_snprintf (v[2], sizeof v[2], "%u", lrg_nav_crowd_get_n_agents (crowd));
		g_snprintf (v[3], sizeof v[3], "%.2f", lrg_nav_crowd_get_last_step_seconds (crowd) * 1000.0);
	}
	else if (self->bake_state == BAKE_RUNNING)
		g_snprintf (v[3], sizeof v[3], "%.0f",
		            (gdouble) (g_get_monotonic_time () - self->bake_started) / 1000.0);
	else if (self->last_seconds > 0.0)
		g_snprintf (v[3], sizeof v[3], "%.0f", self->last_seconds * 1000.0);
	else
		g_snprintf (v[3], sizeof v[3], "-");

	if (width < 260.0f)
	{
		gfloat hh = (height - gap) * 0.5f;

		tw = (width - gap) * 0.5f;
		for (i = 0; i < 4; i++)
			summary_tile (list, theme, x + (gfloat) (i % 2) * (tw + gap), y + (gfloat) (i / 2) * (hh + gap), tw, hh,
			              v[i], labels[i]);
		return;
	}
	tw = (width - gap * 3.0f) / 4.0f;
	for (i = 0; i < 4; i++)
		summary_tile (list, theme, x + (gfloat) i * (tw + gap), y, tw, height, v[i], labels[i]);
}

/* --- the area / box / link lists ----------------------------------------- */

typedef enum
{
	LIST_AREAS,
	LIST_BOXES,
	LIST_LINKS
} ListKind;

static ListKind
list_kind (LrgEditorNavPanel *self,
           LrgCustomWidget   *widget)
{
	if (LRG_WIDGET (widget) == self->box_list)
		return LIST_BOXES;
	if (LRG_WIDGET (widget) == self->link_list)
		return LIST_LINKS;
	return LIST_AREAS;
}

static guint
list_count (LrgEditorNavPanel *self,
            ListKind           kind)
{
	switch (kind)
	{
	case LIST_BOXES:
		return self->sel_area >= 0 ? lrg_nav_bake_get_n_area_boxes (self->bake, (guint) self->sel_area) : 0;
	case LIST_LINKS:
		return lrg_nav_bake_get_n_links (self->bake);
	case LIST_AREAS:
	default:
		return lrg_nav_bake_get_n_areas (self->bake);
	}
}

/* The buttons of a row: areas get "add box" and remove, the rest remove. */
static guint
row_buttons (ListKind kind)
{
	return kind == LIST_AREAS ? 2u : 1u;
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	size->width = 160.0f;
	size->height = (gfloat) MAX (list_count (self, list_kind (self, widget)), 1u) * LIST_ROW_H + 2.0f;
}

static gchar *
row_text (LrgEditorNavPanel *self,
          ListKind           kind,
          guint              i,
          GrlColor          *dot)
{
	switch (kind)
	{
	case LIST_BOXES:
		{
			gfloat mn[3], mx[3];

			lrg_nav_bake_get_area_box (self->bake, (guint) self->sel_area, i, mn, mx);
			*dot = rgba (200, 200, 210, 255);
			return g_strdup_printf ("%u  %.2f x %.2f m  at (%.2f, %.2f, %.2f)", i, (gdouble) (mx[0] - mn[0]),
			                        (gdouble) (mx[2] - mn[2]), (gdouble) ((mn[0] + mx[0]) * 0.5f),
			                        (gdouble) ((mn[1] + mx[1]) * 0.5f), (gdouble) ((mn[2] + mx[2]) * 0.5f));
		}
	case LIST_LINKS:
		{
			gfloat       s[3], e[3], cost = 0.0f;
			gboolean     bidir = TRUE;
			const gchar *area = NULL;
			GString     *t = g_string_new (NULL);

			lrg_nav_bake_get_link (self->bake, i, s, e, &bidir, &cost, &area);
			*dot = rgba (60, 220, 230, 255);
			if (area != NULL && lrg_nav_bake_find_area (self->bake, area) >= 0)
			{
				guint32 tag = 0;

				lrg_nav_bake_get_area (self->bake, (guint) lrg_nav_bake_find_area (self->bake, area), NULL, &tag, NULL);
				*dot = area_color (tag, 255);
			}
			g_string_append_printf (t, "%u  (%.1f, %.1f, %.1f) %s (%.1f, %.1f, %.1f)", i, (gdouble) s[0], (gdouble) s[1],
			                        (gdouble) s[2], bidir ? "<->" : "->", (gdouble) e[0], (gdouble) e[1],
			                        (gdouble) e[2]);
			if (cost > 0.0f)
				g_string_append_printf (t, "  +%.1f", (gdouble) cost);
			if (area != NULL)
				g_string_append_printf (t, "  [%s]", area);
			return g_string_free (t, FALSE);
		}
	case LIST_AREAS:
	default:
		{
			const gchar *name = NULL;
			guint32      tag = 0;
			gfloat       cost = 1.0f;
			guint        nb = lrg_nav_bake_get_n_area_boxes (self->bake, i);

			lrg_nav_bake_get_area (self->bake, i, &name, &tag, &cost);
			*dot = area_color (tag, 255);
			return g_strdup_printf ("%s  tag %u  cost %.2f  %u box%s", name != NULL ? name : "?", tag, (gdouble) cost,
			                        nb, nb == 1 ? "" : "es");
		}
	}
}

static void
list_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	ListKind           kind = list_kind (self, widget);
	guint              n = list_count (self, kind), i, b;
	gint               sel = kind == LIST_AREAS ? self->sel_area : kind == LIST_BOXES ? self->sel_box : self->sel_link;
	gfloat             fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat             radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont           *font = lrg_theme_get_ui_font (theme);
	const GrlColor    *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor    *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor           track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	static const gchar * const empty_text[] = {
		"No area types: everything is normal ground (cost 1)",
		"No boxes: add one to mark the floor as this area",
		"No off-mesh links (ladders, jumps, teleporters)"
	};

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	if (n == 0)
	{
		lrg_paint_list_push_clip (list, x, y, width, height);
		lrg_paint_list_text (list, font, empty_text[kind], x + 10.0f, roundf (y + (LIST_ROW_H - fs) * 0.5f), fs, 0.0f,
		                     muted);
		lrg_paint_list_pop_clip (list);
		return;
	}
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * LIST_ROW_H;
		GrlColor          dot;
		g_autofree gchar *label = row_text (self, kind, i, &dot);

		if ((gint) i == sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, LIST_ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		if (kind == LIST_AREAS)
			lrg_paint_list_rect (list, x + 6.0f, ry + LIST_ROW_H * 0.5f - 5.0f, 10.0f, 10.0f, 3.0f, &dot);
		else
			lrg_paint_list_circle (list, x + 11.0f, ry + LIST_ROW_H * 0.5f, 3.5f, &dot);
		lrg_paint_list_push_clip (list, x + 22.0f, ry,
		                          MAX (width - 22.0f - LIST_BTN_W * (gfloat) row_buttons (kind) - 4.0f, 0.0f), LIST_ROW_H);
		lrg_paint_list_text (list, font, label, x + 22.0f, roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     text);
		lrg_paint_list_pop_clip (list);
		for (b = 0; b < row_buttons (kind); b++)
		{
			gfloat  bx = x + width - LIST_BTN_W * (gfloat) (row_buttons (kind) - b) - 2.0f;
			gfloat  is = 12.0f;
			gboolean add = kind == LIST_AREAS && b == 0;

			lrg_paint_list_icon (list, add ? LRG_ICON_PLUS : LRG_ICON_TRASH, bx + (LIST_BTN_W - is) * 0.5f,
			                     ry + (LIST_ROW_H - is) * 0.5f, is, 0.0f,
			                     add ? text : lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		}
	}
}

/* A box for a new area or the "Add box" button: 4 x 4 m around the
 * active viewport's target, one metre above and below it. */
static void
default_box (LrgEditorNavPanel *self,
             gfloat            *mn,
             gfloat            *mx)
{
	LrgEditorView view;
	gfloat        c[3] = { 0.0f, 0.0f, 0.0f };

	if (self->app != NULL && self->app->view != NULL)
	{
		lrg_editor_view_state_get_view (self->app->view, -1, &view);
		memcpy (c, view.target, sizeof c);
	}
	mn[0] = roundf (c[0]) - 2.0f;
	mn[1] = roundf (c[1]) - 1.0f;
	mn[2] = roundf (c[2]) - 2.0f;
	mx[0] = roundf (c[0]) + 2.0f;
	mx[1] = roundf (c[1]) + 1.0f;
	mx[2] = roundf (c[2]) + 2.0f;
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	ListKind           kind = list_kind (self, widget);
	gfloat             x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat             y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat             w = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat             px, py;
	guint              n, row, b;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	px = lrg_ui_event_get_x (event);
	py = lrg_ui_event_get_y (event);
	n = list_count (self, kind);
	if (py < y + 1.0f || n == 0)
		return FALSE;
	row = (guint) ((py - y - 1.0f) / LIST_ROW_H);
	if (row >= n)
		return TRUE;

	for (b = 0; b < row_buttons (kind); b++)
	{
		gfloat bx = x + w - LIST_BTN_W * (gfloat) (row_buttons (kind) - b) - 2.0f;

		if (px < bx || px >= bx + LIST_BTN_W)
			continue;
		if (kind == LIST_AREAS && b == 0)
		{
			gfloat mn[3], mx[3];

			default_box (self, mn, mx);
			lrg_editor_nav_panel_add_area_box (self, row, mn, mx, NULL);
		}
		else if (kind == LIST_AREAS)
			lrg_editor_nav_panel_remove_area (self, row);
		else if (kind == LIST_BOXES)
			lrg_editor_nav_panel_remove_area_box (self, (guint) self->sel_area, row);
		else
			lrg_editor_nav_panel_remove_link (self, row);
		return TRUE;
	}
	if (kind == LIST_AREAS)
		lrg_editor_nav_panel_select (self, (gint) row,
		                             lrg_nav_bake_get_n_area_boxes (self->bake, row) > 0 ? 0 : -1, self->sel_link);
	else if (kind == LIST_BOXES)
		lrg_editor_nav_panel_select (self, self->sel_area, (gint) row, self->sel_link);
	else
		lrg_editor_nav_panel_select (self, self->sel_area, self->sel_box, (gint) row);
	return TRUE;
}

/* --- the agent list (playing) -------------------------------------------- */

static guint
agent_count (LrgEditorNavPanel *self)
{
	LrgNavCrowd *crowd = lrg_editor_nav_panel_get_crowd (self);

	return (crowd != NULL) ? lrg_nav_crowd_get_n_agents (crowd) : 0;
}

static void
agents_measure (LrgCustomWidget     *widget,
                gfloat               avail_width,
                gfloat               avail_height,
                LrgCustomWidgetSize *size,
                gpointer             user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	size->width = 160.0f;
	size->height = (gfloat) CLAMP (agent_count (self), 1u, 12u) * LIST_ROW_H + 2.0f;
}

static void
agents_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgNavCrowd       *crowd = lrg_editor_nav_panel_get_crowd (self);
	guint              n = agent_count (self), i;
	gint               dbg = lrg_editor_nav_panel_get_debug_agent (self);
	gfloat             fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat             radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont           *font = lrg_theme_get_ui_font (theme);
	GrlColor           track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	lrg_paint_list_push_clip (list, x, y, width, height);
	if (n == 0)
	{
		lrg_paint_list_text (list, font, crowd != NULL ? "No agents: give entities a nav-agent component"
		                                               : "No navigation mesh on the play world",
		                     x + 10.0f, roundf (y + (LIST_ROW_H - fs) * 0.5f), fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_paint_list_pop_clip (list);
		return;
	}
	for (i = 0; i < MIN (n, 12u); i++)
	{
		LrgNavAgent      *agent = lrg_nav_crowd_get_agent (crowd, i);
		LrgNavAgentState  state = lrg_nav_agent_get_state (agent);
		gfloat            ry = y + 1.0f + (gfloat) i * LIST_ROW_H;
		GrlColor          dot = agent_state_color (state);
		gfloat            vel[3];
		g_autofree gchar *name = agent_name (self, agent, i, NULL);
		g_autofree gchar *right = NULL;
		gfloat            tw = 0.0f, th = 0.0f;

		lrg_nav_agent_get_velocity (agent, vel);
		if ((gint) i == dbg)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, LIST_ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		lrg_paint_list_circle (list, x + 11.0f, ry + LIST_ROW_H * 0.5f, 4.0f, &dot);
		right = g_strdup_printf ("%s  %.1f m  %.1f m/s", agent_state_nick (state),
		                         (gdouble) lrg_nav_agent_get_remaining_distance (agent),
		                         (gdouble) sqrtf (vel[0] * vel[0] + vel[2] * vel[2]));
		lrg_paint_measure_text (font, right, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, name, x + 22.0f, roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_text (list, font, right, roundf (x + width - tw - 8.0f), roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f),
		                     fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
agents_step (LrgCustomWidget *widget,
             gfloat           delta_time,
             gpointer         user_data)
{
	return is_playing (LRG_EDITOR_NAV_PANEL (user_data));
}

static gboolean
agents_event (LrgCustomWidget  *widget,
              const LrgUIEvent *event,
              gpointer          user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	gfloat             y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	guint              row;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	if (lrg_ui_event_get_y (event) < y + 1.0f)
		return FALSE;
	row = (guint) ((lrg_ui_event_get_y (event) - y - 1.0f) / LIST_ROW_H);
	if (row >= agent_count (self))
		return TRUE;
	/* a second click on the debug agent clears it */
	lrg_editor_nav_panel_set_debug_agent (self, lrg_editor_nav_panel_get_debug_agent (self) == (gint) row ? -1 : (gint) row);
	return TRUE;
}

static void
footer_measure (LrgCustomWidget     *widget,
                gfloat               avail_width,
                gfloat               avail_height,
                LrgCustomWidgetSize *size,
                gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));

	size->width = 120.0f;
	size->height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL) + 8.0f;
}

/* "debug: Guard 2  path 6 corners  step #1234" under the agent list. */
static void
footer_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgNavCrowd       *crowd = lrg_editor_nav_panel_get_crowd (self);
	gint               dbg = lrg_editor_nav_panel_get_debug_agent (self);
	gfloat             fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	g_autofree gchar  *text = NULL;

	if (crowd == NULL)
		return;
	if (dbg >= 0)
	{
		LrgNavAgent      *agent = lrg_nav_crowd_get_agent (crowd, (guint) dbg);
		g_autofree gchar *name = agent_name (self, agent, (guint) dbg, NULL);
		guint             nf = 0;

		lrg_nav_agent_get_path (agent, &nf);
		text = g_strdup_printf ("Debug agent: %s, path %u corners (at %u), step %" G_GUINT64_FORMAT
		                        "  (click again to clear)", name, nf / 3, lrg_nav_agent_get_corner_index (agent),
		                        lrg_nav_crowd_get_step_count (crowd));
	}
	else
		text = g_strdup_printf ("Click an agent to see its ORCA planes and neighbours; step %" G_GUINT64_FORMAT,
		                        lrg_nav_crowd_get_step_count (crowd));
	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, x + 2.0f, roundf (y + (height - fs) * 0.5f), fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * The settings picker
 * ========================================================================== */

/* Every .rnav under @dir (relative paths, sorted), a few levels deep. */
static void
scan_rnav (const gchar *root,
           const gchar *rel,
           guint        depth,
           GPtrArray   *out)
{
	g_autofree gchar *dir = (rel != NULL) ? g_build_filename (root, rel, NULL) : g_strdup (root);
	g_autoptr(GDir)   d = g_dir_open (dir, 0, NULL);
	const gchar      *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child_rel = (rel != NULL) ? g_build_filename (rel, name, NULL) : g_strdup (name);
		g_autofree gchar *child = g_build_filename (root, child_rel, NULL);

		if (name[0] == '.' || g_strcmp0 (name, "build") == 0)
			continue;
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
		{
			if (depth > 0)
				scan_rnav (root, child_rel, depth - 1, out);
		}
		else if (g_str_has_suffix (name, ".rnav"))
			g_ptr_array_add (out, g_steal_pointer (&child_rel));
	}
}

static gint
compare_str (gconstpointer a,
             gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* Refills the picker: "(none)", every .rnav under the level's directory
 * (relative to it, as the level stores them), and the current reference
 * when it is elsewhere. */
static void
sync_settings_combo (LrgEditorNavPanel *self)
{
	LrgLevel         *level = current_level (self);
	const gchar      *ref = (level != NULL) ? lrg_level_get_nav_settings_ref (level) : NULL;
	g_autofree gchar *dir = level_dir (self);
	gint              sel = 0;
	guint             i;

	if (self->settings_combo == NULL)
		return;
	g_free (self->combo_ref);
	self->combo_ref = g_strdup (ref);
	g_ptr_array_set_size (self->settings_files, 0);
	if (dir != NULL)
		scan_rnav (dir, NULL, SCAN_DEPTH, self->settings_files);
	g_ptr_array_sort (self->settings_files, compare_str);
	if (ref != NULL)
	{
		gboolean found = FALSE;

		for (i = 0; i < self->settings_files->len; i++)
			found = found || g_strcmp0 (g_ptr_array_index (self->settings_files, i), ref) == 0;
		if (!found)
			g_ptr_array_add (self->settings_files, g_strdup (ref));
	}
	self->applying = TRUE;
	lrg_combo_box_remove_all (self->settings_combo);
	lrg_combo_box_append (self->settings_combo, NO_AREA);
	for (i = 0; i < self->settings_files->len; i++)
	{
		lrg_combo_box_append (self->settings_combo, g_ptr_array_index (self->settings_files, i));
		if (g_strcmp0 (g_ptr_array_index (self->settings_files, i), ref) == 0)
			sel = (gint) i + 1;
	}
	lrg_combo_box_set_selected (self->settings_combo, sel);
	self->applying = FALSE;
}

static void
on_settings_combo (LrgComboBox *combo,
                   gpointer     user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	gint               sel = lrg_combo_box_get_selected (combo);
	g_autoptr(GError)  error = NULL;

	if (self->applying || current_level (self) == NULL)
		return;
	if (sel <= 0)
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_STRING);
		lrg_editor_nav_panel_set_field (self, "settings", &v, NULL);
		return;
	}
	if ((guint) sel - 1 < self->settings_files->len &&
	    !lrg_editor_nav_panel_open_settings (self, g_ptr_array_index (self->settings_files, (guint) sel - 1), &error))
	{
		if (self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Navigation settings: %s", error->message);
		sync_settings_combo (self);
	}
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static void
sync_widgets (LrgEditorNavPanel *self)
{
	LrgLevel *level = current_level (self);
	gboolean  has = level != NULL;
	gboolean  baking = self->bake_state == BAKE_RUNNING;
	gboolean  playing = is_playing (self);

	if (self->bake_chip == NULL)
		return;
	editor_chip_set_label (self->bake_chip, baking ? "Cancel" : "Bake");
	editor_chip_set_icon (self->bake_chip, baking ? LRG_ICON_STOP : LRG_ICON_REFRESH);
	editor_chip_set_tint (self->bake_chip, baking ? LRG_THEME_COLOR_DANGER : LRG_THEME_COLOR_ACCENT);
	lrg_widget_set_enabled (self->bake_chip, has && !playing);
	lrg_widget_set_enabled (self->save_chip, has && !baking && lrg_editor_get_path (self->editor) != NULL);
	lrg_widget_set_enabled (self->clear_chip, has && !baking &&
	                        (self->mesh != NULL || (level != NULL && lrg_level_get_nav_data_ref (level) != NULL)));
	editor_chip_set_checked (self->overlay_chip, self->overlay != LRG_NAV_DEBUG_NONE);
	editor_chip_set_badge (self->save_chip, (has && (lrg_editor_nav_panel_get_dirty (self) ||
	                                                 lrg_editor_nav_panel_has_unsaved (self))) ? "*" : NULL);

	lrg_widget_set_visible (LRG_WIDGET (self->bar), baking);
	lrg_progress_bar_set_indeterminate (self->bar, baking);
	lrg_widget_set_visible (self->phase_label, baking || self->bake_state == BAKE_FAILED);
	if (self->bake_state == BAKE_FAILED && self->error != NULL)
	{
		g_autofree gchar *t = g_strdup_printf ("Failed: %s", self->error);

		lrg_label_set_text (LRG_LABEL (self->phase_label), t);
	}
	else
		lrg_label_set_text (LRG_LABEL (self->phase_label), "Voxelising the level and building polygons");

	/* the picker follows reference changes made elsewhere (IPC, undo) */
	if (g_strcmp0 (level != NULL ? lrg_level_get_nav_settings_ref (level) : NULL, self->combo_ref) != 0)
		sync_settings_combo (self);

	/* the settings path line (project relative) */
	{
		g_autofree gchar *t = NULL;

		if (self->settings_path != NULL)
		{
			g_autofree gchar *rel = (self->app != NULL) ? editor_app_relative_path (self->app, self->settings_path)
			                                            : g_strdup (self->settings_path);

			t = g_strdup_printf ("%s%s", rel, lrg_editor_nav_panel_get_dirty (self) ? "  (modified)" : "");
		}
		else
			t = g_strdup (lrg_editor_nav_panel_get_dirty (self) ? "Not saved to a .rnav yet (Save writes one)"
			                                                   : "No .rnav: the defaults (Save or New writes one)");
		lrg_label_set_text (LRG_LABEL (self->settings_path_label), t);
	}

	lrg_widget_set_visible (LRG_WIDGET (self->agents_section), playing);
	lrg_widget_set_visible (LRG_WIDGET (self->area_grid), self->sel_area >= 0);
	lrg_widget_set_visible (self->box_heading, self->sel_area >= 0);
	lrg_widget_set_visible (self->box_list, self->sel_area >= 0);
	lrg_widget_set_visible (LRG_WIDGET (self->box_grid), self->sel_area >= 0 && self->sel_box >= 0);
	lrg_widget_set_visible (LRG_WIDGET (self->link_grid), self->sel_link >= 0);
	lrg_widget_queue_layout (self->area_list);
	lrg_widget_queue_layout (self->box_list);
	lrg_widget_queue_layout (self->link_list);
	lrg_widget_queue_layout (self->agent_list);
	lrg_widget_set_visible (self->scroll, has);
	lrg_widget_set_visible (self->empty, !has);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* The panel follows a new (or reloaded) level: its settings file, its
 * baked data. */
static void
follow_level (LrgEditorNavPanel *self)
{
	LrgLevel *level = current_level (self);

	g_set_weak_pointer (&self->level, level);
	g_clear_object (&self->gesture_proxy);
	if (self->bake_state == BAKE_RUNNING)
		lrg_editor_nav_panel_cancel (self);
	self->bake_state = BAKE_IDLE;
	self->bake_serial++;
	g_clear_pointer (&self->error, g_free);
	g_clear_object (&self->mesh);
	g_clear_pointer (&self->mesh_from, g_free);
	self->mesh_unsaved = FALSE;
	self->last_triangles = 0;
	self->last_seconds = 0.0;
	self->sel_area = self->sel_box = self->sel_link = -1;
	self->drag = self->hot = LRG_EDITOR_NAV_HANDLE_NONE;
	g_clear_pointer (&self->drag_before, g_free);
	g_clear_pointer (&self->disk_for, g_free);
	refresh_disk (self);
	install_bake (self, NULL);
	if (self->settings_path != NULL)
	{
		g_autoptr(GError) error = NULL;

		self->bake = lrg_nav_bake_new_from_file (self->settings_path, &error);
		if (self->bake == NULL && self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Navigation settings did not load: %s",
			                   error->message);
	}
	if (self->bake == NULL)
		self->bake = lrg_nav_bake_new ();
	refresh_mesh (self);
	g_free (self->state);
	self->state = capture (self);
	sync_settings_combo (self);
	sync_form (self);
	sync_widgets (self);
	queue_viewports (self);
}

static void
on_editor_changed (LrgEditorNavPanel *self)
{
	g_autofree gchar *now = NULL;

	if (self->applying)
		return;
	if (current_level (self) != self->level)
	{
		follow_level (self);
		return;
	}
	/* someone else changed the level's block (a script, a reload): follow */
	now = capture (self);
	if (g_strcmp0 (now, self->state) != 0)
	{
		g_free (self->state);
		self->state = g_steal_pointer (&now);
		refresh_disk (self);
		refresh_mesh (self);
		sync_form (self);
	}
	sync_widgets (self);
}

static void
on_level_replaced (LrgEditorNavPanel *self)
{
	follow_level (self);
}

/* The level was saved: dirty settings that have a file go with it. */
static void
on_editor_saved (LrgEditorNavPanel *self,
                 const gchar       *path)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_editor_nav_panel_get_dirty (self))
		return;
	if (self->settings_path == NULL)
	{
		if (self->app != NULL)
			editor_app_set_status (self->app, "Navigation settings are not saved: use Save in the Navigation panel");
		return;
	}
	if (!lrg_nav_bake_save_to_file (self->bake, self->settings_path, &error))
	{
		if (self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Navigation settings not saved: %s", error->message);
		return;
	}
	g_free (self->disk_yaml);
	self->disk_yaml = lrg_nav_bake_to_data (self->bake);
	sync_widgets (self);
}

static void
on_play_started (LrgEditorNavPanel *self)
{
	push_mesh_to_play (self);
	self->debug_agent = -1;
	sync_widgets (self);
	queue_viewports (self);
}

static void
on_play_stopped (LrgEditorNavPanel *self)
{
	self->debug_agent = -1;
	sync_widgets (self);
	queue_viewports (self);
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

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
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	if (gesture_id != self->gesture_id)
		return;
	self->in_gesture = FALSE;
	g_clear_object (&self->gesture_proxy);
	sync_form (self);
}

/* ==========================================================================
 * Chips
 * ========================================================================== */

static void
on_bake_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	g_autoptr(GError)  error = NULL;

	if (lrg_editor_nav_panel_get_baking (self))
	{
		lrg_editor_nav_panel_cancel (self);
		return;
	}
	if (!lrg_editor_nav_panel_bake_async (self, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
}

static void
on_save_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	g_autoptr(GError)  error = NULL;

	if (!lrg_editor_nav_panel_save (self, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
	sync_settings_combo (self);
}

static void
on_clear_chip (LrgWidget *chip,
               gpointer   user_data)
{
	lrg_editor_nav_panel_clear (LRG_EDITOR_NAV_PANEL (user_data));
}

static void
on_overlay_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);

	lrg_editor_nav_panel_set_overlay (self, self->overlay != LRG_NAV_DEBUG_NONE
	                                       ? LRG_NAV_DEBUG_NONE
	                                       : (self->overlay_last != 0 ? self->overlay_last : OVERLAY_DEFAULT));
}

static void
on_new_chip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	g_autoptr(GError)  error = NULL;

	if (!lrg_editor_nav_panel_new_settings (self, NULL, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
	sync_settings_combo (self);
}

/* A new area: "area", "area-2"..., at the view target with one box. */
static void
on_add_area (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	g_autofree gchar  *name = g_strdup ("area");
	g_autofree gchar  *before = NULL;
	g_autoptr(GError)  error = NULL;
	guint              n = 1;
	gint               index;
	gfloat             mn[3], mx[3];

	if (current_level (self) == NULL)
		return;
	while (lrg_nav_bake_find_area (self->bake, name) >= 0)
	{
		g_free (name);
		name = g_strdup_printf ("area-%u", ++n);
	}
	/* one step for the area and its first box */
	before = capture (self);
	index = lrg_nav_bake_add_area (self->bake, name, free_tag (self->bake), 2.0f);
	if (index < 0)
		return;
	default_box (self, mn, mx);
	self->sel_area = index;
	self->sel_box = lrg_nav_bake_add_area_box (self->bake, (guint) index, mn, mx);
	commit_now (self, before, "Add Navigation Area", FALSE);
}

/* A new link: from the view target 2 m along X, one way up a metre. */
static void
on_add_link (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (user_data);
	gfloat             mn[3], mx[3], s[3], e[3];

	default_box (self, mn, mx);
	s[0] = (mn[0] + mx[0]) * 0.5f - 1.0f;
	s[1] = (mn[1] + mx[1]) * 0.5f;
	s[2] = (mn[2] + mx[2]) * 0.5f;
	e[0] = s[0] + 2.0f;
	e[1] = s[1];
	e[2] = s[2];
	lrg_editor_nav_panel_add_link (self, s, e, TRUE, 0.0f, NULL, NULL);
}

/* ==========================================================================
 * IPC: nav.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static JsonArray *
vec3_array (const gfloat *v)
{
	JsonArray *a = json_array_new ();
	gint       k;

	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, (gdouble) v[k]);
	return a;
}

static JsonArray *
strv_array (const gchar * const *v)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; v != NULL && v[i] != NULL; i++)
		json_array_add_string_element (a, v[i]);
	return a;
}

static void
set_string_or_null (JsonObject  *o,
                    const gchar *name,
                    const gchar *value)
{
	if (value != NULL)
		json_object_set_string_member (o, name, value);
	else
		json_object_set_null_member (o, name);
}

static gboolean
json_vec3 (JsonNode *n,
           gfloat   *out)
{
	JsonArray *a;
	guint      k;

	if (n == NULL || !JSON_NODE_HOLDS_ARRAY (n))
		return FALSE;
	a = json_node_get_array (n);
	if (json_array_get_length (a) < 3)
		return FALSE;
	for (k = 0; k < 3; k++)
	{
		JsonNode *e = json_array_get_element (a, k);

		if (!JSON_NODE_HOLDS_VALUE (e))
			return FALSE;
		out[k] = (gfloat) json_node_get_double (e);
	}
	return TRUE;
}

static gboolean
params_vec3 (JsonObject  *params,
             const gchar *name,
             gfloat      *out)
{
	return params != NULL && json_vec3 (json_object_get_member (params, name), out);
}

/* The overlay flag nicks, plus "handles". */
static JsonArray *
overlay_array (LrgEditorNavPanel *self)
{
	g_autoptr(GFlagsClass) klass = g_type_class_ref (LRG_TYPE_NAV_DEBUG_FLAGS);
	JsonArray             *a = json_array_new ();
	guint                  i;

	for (i = 0; i < klass->n_values; i++)
	{
		GFlagsValue *fv = &klass->values[i];

		/* single bits only */
		if (fv->value != 0 && (fv->value & (fv->value - 1)) == 0 && (self->overlay & fv->value) != 0)
			json_array_add_string_element (a, fv->value_nick);
	}
	if (self->show_handles)
		json_array_add_string_element (a, "handles");
	return a;
}

static JsonNode *
state_json (LrgEditorNavPanel *self)
{
	LrgLevel   *level = current_level (self);
	JsonObject *o = json_object_new ();
	JsonObject *bake = json_object_new ();
	JsonObject *agent = json_object_new ();
	JsonObject *voxel = json_object_new ();
	JsonObject *regions = json_object_new ();
	JsonObject *sources = json_object_new ();
	JsonObject *mesh = json_object_new ();
	JsonObject *sel = json_object_new ();
	JsonArray  *areas = json_array_new ();
	JsonArray  *links = json_array_new ();
	gfloat      radius, height, slope, step, drop = 0.0f, f = 0.0f;
	gfloat      mn[3], mx[3];
	gboolean    b = FALSE;
	guint       mask = 0;
	guint       i, k;

	set_string_or_null (o, "settings", level != NULL ? lrg_level_get_nav_settings_ref (level) : NULL);
	set_string_or_null (o, "settings_path", self->settings_path);
	set_string_or_null (o, "data", level != NULL ? lrg_level_get_nav_data_ref (level) : NULL);
	json_object_set_boolean_member (o, "bake_on_load", level != NULL && lrg_level_get_nav_bake_on_load (level));
	json_object_set_boolean_member (o, "dirty", lrg_editor_nav_panel_get_dirty (self));

	set_string_or_null (bake, "name", lrg_nav_bake_get_name (self->bake));
	lrg_nav_bake_get_agent (self->bake, &radius, &height, &slope, &step);
	g_object_get (self->bake, "max-drop", &drop, NULL);
	json_object_set_double_member (agent, "radius", radius);
	json_object_set_double_member (agent, "height", height);
	json_object_set_double_member (agent, "max_slope", slope);
	json_object_set_double_member (agent, "max_step", step);
	json_object_set_double_member (agent, "max_drop", drop);
	json_object_set_object_member (bake, "agent", agent);
	json_object_set_double_member (voxel, "cell_size", lrg_nav_bake_get_cell_size (self->bake));
	json_object_set_double_member (voxel, "cell_height", lrg_nav_bake_get_cell_height (self->bake));
	json_object_set_object_member (bake, "voxel", voxel);
	g_object_get (self->bake, "min-region-area", &f, NULL);
	json_object_set_double_member (regions, "min_area", f);
	g_object_get (self->bake, "corner-margin", &f, NULL);
	json_object_set_double_member (regions, "corner_margin", f);
	json_object_set_object_member (bake, "regions", regions);
	g_object_get (self->bake, "include-meshes", &b, NULL);
	json_object_set_boolean_member (sources, "meshes", b);
	g_object_get (self->bake, "include-colliders", &b, NULL);
	json_object_set_boolean_member (sources, "colliders", b);
	g_object_get (self->bake, "static-only", &b, NULL);
	json_object_set_boolean_member (sources, "static_only", b);
	g_object_get (self->bake, "layer-mask", &mask, NULL);
	json_object_set_int_member (sources, "layer_mask", (gint64) mask);
	json_object_set_array_member (sources, "include_tags", strv_array (lrg_nav_bake_get_include_tags (self->bake)));
	json_object_set_array_member (sources, "exclude_tags", strv_array (lrg_nav_bake_get_exclude_tags (self->bake)));
	json_object_set_array_member (sources, "physics_layers",
	                              strv_array (lrg_nav_bake_get_physics_layers (self->bake)));
	if (lrg_nav_bake_get_bounds (self->bake, mn, mx))
	{
		JsonObject *bounds = json_object_new ();

		json_object_set_array_member (bounds, "min", vec3_array (mn));
		json_object_set_array_member (bounds, "max", vec3_array (mx));
		json_object_set_object_member (sources, "bounds", bounds);
	}
	else
		json_object_set_null_member (sources, "bounds");
	json_object_set_object_member (bake, "sources", sources);
	{
		g_autofree gchar *output = NULL;

		g_object_get (self->bake, "output", &output, NULL);
		set_string_or_null (bake, "output", output);
	}
	json_object_set_object_member (o, "bake", bake);

	for (i = 0; i < lrg_nav_bake_get_n_areas (self->bake); i++)
	{
		JsonObject      *a = json_object_new ();
		JsonArray       *boxes = json_array_new ();
		const gchar     *name = NULL;
		guint32          tag = 0;
		gfloat           cost = 1.0f;
		GrlColor         c;
		g_autofree gchar *hex = NULL;

		lrg_nav_bake_get_area (self->bake, i, &name, &tag, &cost);
		c = area_color (tag, 255);
		hex = g_strdup_printf ("#%02x%02x%02x", c.r, c.g, c.b);
		json_object_set_int_member (a, "index", i);
		json_object_set_string_member (a, "name", name);
		json_object_set_int_member (a, "tag", (gint64) tag);
		json_object_set_double_member (a, "cost", cost);
		json_object_set_string_member (a, "color", hex);
		for (k = 0; k < lrg_nav_bake_get_n_area_boxes (self->bake, i); k++)
		{
			JsonObject *box = json_object_new ();

			lrg_nav_bake_get_area_box (self->bake, i, k, mn, mx);
			json_object_set_array_member (box, "min", vec3_array (mn));
			json_object_set_array_member (box, "max", vec3_array (mx));
			json_array_add_object_element (boxes, box);
		}
		json_object_set_array_member (a, "boxes", boxes);
		json_array_add_object_element (areas, a);
	}
	json_object_set_array_member (o, "areas", areas);
	for (i = 0; i < lrg_nav_bake_get_n_links (self->bake); i++)
	{
		JsonObject  *l = json_object_new ();
		gfloat       s[3], e[3], cost = 0.0f;
		gboolean     bidir = TRUE;
		const gchar *area = NULL;

		lrg_nav_bake_get_link (self->bake, i, s, e, &bidir, &cost, &area);
		json_object_set_int_member (l, "index", i);
		json_object_set_array_member (l, "start", vec3_array (s));
		json_object_set_array_member (l, "end", vec3_array (e));
		json_object_set_boolean_member (l, "bidirectional", bidir);
		json_object_set_double_member (l, "cost", cost);
		set_string_or_null (l, "area", area);
		json_array_add_object_element (links, l);
	}
	json_object_set_array_member (o, "links", links);

	json_object_set_boolean_member (mesh, "present", self->mesh != NULL);
	json_object_set_boolean_member (mesh, "unsaved", lrg_editor_nav_panel_has_unsaved (self));
	json_object_set_int_member (mesh, "polygons", self->mesh != NULL ? lrg_nav_poly_mesh_get_polygon_count (self->mesh) : 0);
	json_object_set_int_member (mesh, "regions", self->mesh != NULL ? lrg_nav_poly_mesh_get_region_count (self->mesh) : 0);
	json_object_set_int_member (mesh, "links", self->mesh != NULL ? lrg_nav_poly_mesh_get_link_count (self->mesh) : 0);
	json_object_set_int_member (mesh, "boundaries",
	                            self->mesh != NULL ? lrg_nav_poly_mesh_get_boundary_count (self->mesh) : 0);
	json_object_set_int_member (mesh, "triangles", self->last_triangles);
	json_object_set_double_member (mesh, "seconds", self->last_seconds);
	json_object_set_object_member (o, "mesh", mesh);

	json_object_set_boolean_member (o, "baking", lrg_editor_nav_panel_get_baking (self));
	json_object_set_boolean_member (o, "playing", is_playing (self));
	set_string_or_null (o, "error", self->error);
	json_object_set_array_member (o, "overlay", overlay_array (self));
	json_object_set_int_member (sel, "area", self->sel_area);
	json_object_set_int_member (sel, "box", self->sel_box);
	json_object_set_int_member (sel, "link", self->sel_link);
	json_object_set_object_member (o, "selection", sel);
	return obj_node (o);
}

static LrgEditorNavPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorNavPanel *self = lrg_editor_nav_panel_find (LRG_EDITOR_APP (user_data));

	if (self == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No navigation panel");
	return self;
}

/* The panel with a level, or an error. */
static LrgEditorNavPanel *
ipc_level_panel (gpointer   user_data,
                 GError   **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);

	if (self != NULL && current_level (self) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level is open");
		return NULL;
	}
	return self;
}

/* A local error as a server error of @code. */
static void
ipc_error (GError  **error,
           gint      code,
           GError   *local)
{
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, code, "%s", local != NULL ? local->message : "failed");
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);

	return (self != NULL) ? state_json (self) : NULL;
}

/* A JSON value as a GValue for apply_field(): numbers, booleans, strings,
 * null (an unset string), arrays of 3 numbers (GrlVector3) or strings. */
static gboolean
json_to_value (JsonNode *n,
               GValue   *out)
{
	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
	{
		g_value_init (out, G_TYPE_STRING);
		return TRUE;
	}
	if (JSON_NODE_HOLDS_VALUE (n))
	{
		switch (json_node_get_value_type (n))
		{
		case G_TYPE_BOOLEAN:
			g_value_init (out, G_TYPE_BOOLEAN);
			g_value_set_boolean (out, json_node_get_boolean (n));
			return TRUE;
		case G_TYPE_INT64:
			g_value_init (out, G_TYPE_DOUBLE);
			g_value_set_double (out, (gdouble) json_node_get_int (n));
			return TRUE;
		case G_TYPE_DOUBLE:
			g_value_init (out, G_TYPE_DOUBLE);
			g_value_set_double (out, json_node_get_double (n));
			return TRUE;
		case G_TYPE_STRING:
			g_value_init (out, G_TYPE_STRING);
			g_value_set_string (out, json_node_get_string (n));
			return TRUE;
		default:
			return FALSE;
		}
	}
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		guint      len = json_array_get_length (a), i;
		gboolean   numbers = TRUE;
		GPtrArray *strs;

		for (i = 0; i < len; i++)
		{
			JsonNode *e = json_array_get_element (a, i);

			numbers = numbers && JSON_NODE_HOLDS_VALUE (e) && json_node_get_value_type (e) != G_TYPE_STRING &&
			          json_node_get_value_type (e) != G_TYPE_BOOLEAN;
		}
		if (numbers && len == 3)
		{
			gfloat p[3];

			json_vec3 (n, p);
			g_value_init (out, GRL_TYPE_VECTOR3);
			g_value_take_boxed (out, grl_vector3_new (p[0], p[1], p[2]));
			return TRUE;
		}
		strs = g_ptr_array_new_with_free_func (g_free);
		for (i = 0; i < len; i++)
		{
			JsonNode *e = json_array_get_element (a, i);

			if (!JSON_NODE_HOLDS_VALUE (e) || json_node_get_value_type (e) != G_TYPE_STRING)
			{
				g_ptr_array_unref (strs);
				return FALSE;
			}
			g_ptr_array_add (strs, g_strdup (json_node_get_string (e)));
		}
		g_ptr_array_add (strs, NULL);
		g_value_init (out, G_TYPE_STRV);
		g_value_take_boxed (out, g_ptr_array_free (strs, FALSE));
		return TRUE;
	}
	return FALSE;
}

/* One key: "bounds" {min,max} | null expands to the three bounds rows. */
static gboolean
ipc_apply_key (LrgEditorNavPanel  *self,
               const gchar        *key,
               JsonNode           *node,
               GError            **error)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (g_strcmp0 (key, "bounds") == 0)
	{
		g_auto(GValue) on = G_VALUE_INIT;

		g_value_init (&on, G_TYPE_BOOLEAN);
		if (node != NULL && JSON_NODE_HOLDS_OBJECT (node))
		{
			JsonObject *b = json_node_get_object (node);
			gfloat      mn[3], mx[3];

			if (!params_vec3 (b, "min", mn) || !params_vec3 (b, "max", mx))
			{
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bounds takes {min, max}");
				return FALSE;
			}
			memcpy (self->bounds_min, mn, sizeof mn);
			memcpy (self->bounds_max, mx, sizeof mx);
			g_value_set_boolean (&on, TRUE);
		}
		return apply_field (self, "use-bounds", &on, error);
	}
	if (!json_to_value (node, &v))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad value for '%s'", key);
		return FALSE;
	}
	return apply_field (self, key, &v, error);
}

/* {key, value} | {values: {key: value}}: one undo step */
static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *before = NULL;
	gboolean           ok = TRUE;
	guint              n = 0;

	if (self == NULL)
		return NULL;
	before = capture (self);
	self->applying = TRUE;
	if (params != NULL && json_object_has_member (params, "key"))
	{
		ok = ipc_apply_key (self, json_object_get_string_member (params, "key"),
		                    json_object_get_member (params, "value"), &local);
		n++;
	}
	if (ok && params != NULL && json_object_has_member (params, "values") &&
	    JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		JsonObject *values = json_object_get_object_member (params, "values");
		GList      *keys = json_object_get_members (values);
		GList      *l;

		for (l = keys; ok && l != NULL; l = l->next, n++)
			ok = ipc_apply_key (self, l->data, json_object_get_member (values, l->data), &local);
		g_list_free (keys);
	}
	self->applying = FALSE;
	if (!ok)
	{
		/* nothing of a failed call stays */
		nav_proxy_applied (self, current_level (self), before);
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	if (n == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Give \"key\" and \"value\", or \"values\": {key: value}");
		return NULL;
	}
	commit_now (self, before, "Edit Navigation", FALSE);
	return state_json (self);
}

static JsonNode *
m_new (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	const gchar       *path = NULL;

	if (self == NULL)
		return NULL;
	if (params != NULL && json_object_has_member (params, "path"))
		path = json_object_get_string_member (params, "path");
	if (!lrg_editor_nav_panel_new_settings (self, path, &local))
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_FAILED, local);
		return NULL;
	}
	sync_settings_combo (self);
	return state_json (self);
}

static JsonNode *
m_bake (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gboolean           saved_auto;
	gboolean           ok;
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	saved_auto = self->auto_save;
	if (params != NULL && json_object_has_member (params, "save"))
		self->auto_save = json_object_get_boolean_member_with_default (params, "save", TRUE);
	if (params != NULL && json_object_get_boolean_member_with_default (params, "async", FALSE))
	{
		ok = lrg_editor_nav_panel_bake_async (self, &local);
		/* the async result reads auto_save when it lands */
		if (!ok)
			self->auto_save = saved_auto;
		if (!ok)
		{
			ipc_error (error, LRG_EDITOR_SERVER_ERROR_FAILED, local);
			return NULL;
		}
		o = json_object_new ();
		json_object_set_boolean_member (o, "started", TRUE);
		return obj_node (o);
	}
	ok = lrg_editor_nav_panel_bake (self, &local);
	self->auto_save = saved_auto;
	if (!ok)
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_FAILED, local);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "polygons", lrg_nav_poly_mesh_get_polygon_count (self->mesh));
	json_object_set_int_member (o, "regions", lrg_nav_poly_mesh_get_region_count (self->mesh));
	json_object_set_int_member (o, "links", lrg_nav_poly_mesh_get_link_count (self->mesh));
	json_object_set_int_member (o, "boundaries", lrg_nav_poly_mesh_get_boundary_count (self->mesh));
	json_object_set_int_member (o, "triangles", self->last_triangles);
	json_object_set_double_member (o, "seconds", self->last_seconds);
	json_object_set_boolean_member (o, "saved", !self->mesh_unsaved);
	return obj_node (o);
}

static JsonNode *
m_cancel (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "cancelled", lrg_editor_nav_panel_cancel (self));
	return obj_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_nav_panel_save (self, &local))
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_FAILED, local);
		return NULL;
	}
	sync_settings_combo (self);
	return state_json (self);
}

static JsonNode *
m_clear (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "cleared", lrg_editor_nav_panel_clear (self));
	return obj_node (o);
}

/* "area": an index or a name -> an index, -1 with @error. */
static gint
ipc_area (LrgEditorNavPanel  *self,
          JsonObject         *params,
          const gchar        *member,
          GError            **error)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, member) : NULL;
	gint      index = -1;

	if (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
		index = lrg_nav_bake_find_area (self->bake, json_node_get_string (n));
	else if (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_INT64)
		index = (gint) json_node_get_int (n);
	else
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "\"%s\" (an area index or name) is required", member);
		return -1;
	}
	if (index < 0 || (guint) index >= lrg_nav_bake_get_n_areas (self->bake))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such area");
		return -1;
	}
	return index;
}

static gint
ipc_index (JsonObject  *params,
           const gchar *member,
           guint        n,
           const gchar *what,
           GError     **error)
{
	gint64 i;

	if (params == NULL || !json_object_has_member (params, member))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "\"%s\" is required",
		             member);
		return -1;
	}
	i = json_object_get_int_member (params, member);
	if (i < 0 || (guint64) i >= n)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No %s %" G_GINT64_FORMAT,
		             what, i);
		return -1;
	}
	return (gint) i;
}

static JsonNode *
index_json (const gchar *member,
            gint         index)
{
	JsonObject *o = json_object_new ();

	json_object_set_int_member (o, member, index);
	return obj_node (o);
}

static JsonNode *
removed_json (void)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "removed", TRUE);
	return obj_node (o);
}

static JsonNode *
m_area_add (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gint               index;

	if (self == NULL)
		return NULL;
	if (params == NULL || !json_object_has_member (params, "name"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "\"name\" is required");
		return NULL;
	}
	index = lrg_editor_nav_panel_add_area (self, json_object_get_string_member (params, "name"),
	                                       (guint32) json_object_get_int_member_with_default (params, "tag", 0),
	                                       (gfloat) json_object_get_double_member_with_default (params, "cost", 1.0),
	                                       &local);
	if (index < 0)
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", index);
}

static JsonNode *
m_area_remove (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	gint               area;

	if (self == NULL || (area = ipc_area (self, params, "area", error)) < 0)
		return NULL;
	lrg_editor_nav_panel_remove_area (self, (guint) area);
	return removed_json ();
}

static JsonNode *
m_area_set (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gint               area;

	if (self == NULL || (area = ipc_area (self, params, "area", error)) < 0)
		return NULL;
	if (!lrg_editor_nav_panel_set_area (self, (guint) area,
	                                    json_object_has_member (params, "name")
	                                    ? json_object_get_string_member (params, "name") : NULL,
	                                    (guint32) json_object_get_int_member_with_default (params, "tag", 0),
	                                    (gfloat) json_object_get_double_member_with_default (params, "cost", 0.0),
	                                    &local))
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", area);
}

static JsonNode *
m_box_add (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gfloat             mn[3], mx[3];
	gint               area, index;

	if (self == NULL || (area = ipc_area (self, params, "area", error)) < 0)
		return NULL;
	if (!params_vec3 (params, "min", mn) || !params_vec3 (params, "max", mx))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "\"min\" and \"max\" ([x, y, z]) are required");
		return NULL;
	}
	index = lrg_editor_nav_panel_add_area_box (self, (guint) area, mn, mx, &local);
	if (index < 0)
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", index);
}

static JsonNode *
m_box_remove (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	gint               area, box;

	if (self == NULL || (area = ipc_area (self, params, "area", error)) < 0)
		return NULL;
	box = ipc_index (params, "box", lrg_nav_bake_get_n_area_boxes (self->bake, (guint) area), "box", error);
	if (box < 0)
		return NULL;
	lrg_editor_nav_panel_remove_area_box (self, (guint) area, (guint) box);
	return removed_json ();
}

static JsonNode *
m_box_set (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gfloat             mn[3], mx[3];
	gint               area, box;

	if (self == NULL || (area = ipc_area (self, params, "area", error)) < 0)
		return NULL;
	box = ipc_index (params, "box", lrg_nav_bake_get_n_area_boxes (self->bake, (guint) area), "box", error);
	if (box < 0)
		return NULL;
	lrg_nav_bake_get_area_box (self->bake, (guint) area, (guint) box, mn, mx);
	params_vec3 (params, "min", mn);
	params_vec3 (params, "max", mx);
	if (!lrg_editor_nav_panel_set_area_box (self, (guint) area, (guint) box, mn, mx, &local))
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", box);
}

/* "area" of a link: a string, null or absent. */
static const gchar *
link_area_param (JsonObject  *params,
                 const gchar *fallback)
{
	JsonNode *n = json_object_get_member (params, "area");

	if (n == NULL)
		return fallback;
	if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
		return json_node_get_string (n);
	return NULL;
}

static JsonNode *
m_link_add (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	gfloat             s[3], e[3];
	gint               index;

	if (self == NULL)
		return NULL;
	if (!params_vec3 (params, "start", s) || !params_vec3 (params, "end", e))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "\"start\" and \"end\" ([x, y, z]) are required");
		return NULL;
	}
	index = lrg_editor_nav_panel_add_link (self, s, e,
	                                       json_object_get_boolean_member_with_default (params, "bidirectional", TRUE),
	                                       (gfloat) json_object_get_double_member_with_default (params, "cost", 0.0),
	                                       link_area_param (params, NULL), &local);
	if (index < 0)
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", index);
}

static JsonNode *
m_link_remove (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	gint               index;

	if (self == NULL)
		return NULL;
	index = ipc_index (params, "index", lrg_nav_bake_get_n_links (self->bake), "link", error);
	if (index < 0)
		return NULL;
	lrg_editor_nav_panel_remove_link (self, (guint) index);
	return removed_json ();
}

static JsonNode *
m_link_set (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorNavPanel *self = ipc_level_panel (user_data, error);
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *old_area = NULL;
	gfloat             s[3], e[3], cost = 0.0f;
	gboolean           bidir = TRUE;
	const gchar       *area = NULL;
	gint               index;

	if (self == NULL)
		return NULL;
	index = ipc_index (params, "index", lrg_nav_bake_get_n_links (self->bake), "link", error);
	if (index < 0)
		return NULL;
	lrg_nav_bake_get_link (self->bake, (guint) index, s, e, &bidir, &cost, &area);
	old_area = g_strdup (area);
	params_vec3 (params, "start", s);
	params_vec3 (params, "end", e);
	bidir = json_object_get_boolean_member_with_default (params, "bidirectional", bidir);
	cost = (gfloat) json_object_get_double_member_with_default (params, "cost", cost);
	if (!lrg_editor_nav_panel_set_link (self, (guint) index, s, e, bidir, cost, link_area_param (params, old_area),
	                                    &local))
	{
		ipc_error (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, local);
		return NULL;
	}
	return index_json ("index", index);
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	gint               area, box, link;
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	area = self->sel_area;
	box = self->sel_box;
	link = self->sel_link;
	if (params != NULL && json_object_has_member (params, "area"))
	{
		JsonNode *n = json_object_get_member (params, "area");

		if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
			area = lrg_nav_bake_find_area (self->bake, json_node_get_string (n));
		else
			area = (gint) json_object_get_int_member (params, "area");
		box = (area >= 0 && lrg_nav_bake_get_n_area_boxes (self->bake, (guint) area) > 0) ? 0 : -1;
	}
	if (params != NULL && json_object_has_member (params, "box"))
		box = (gint) json_object_get_int_member (params, "box");
	if (params != NULL && json_object_has_member (params, "link"))
		link = (gint) json_object_get_int_member (params, "link");
	lrg_editor_nav_panel_select (self, area, box, link);
	o = json_object_new ();
	json_object_set_int_member (o, "area", self->sel_area);
	json_object_set_int_member (o, "box", self->sel_box);
	json_object_set_int_member (o, "link", self->sel_link);
	return obj_node (o);
}

/* Nicks -> flags ("handles" is the handles toggle); FALSE on an unknown
 * nick. */
static gboolean
parse_overlay (JsonArray  *a,
               guint      *flags,
               gboolean   *handles,
               GError    **error)
{
	g_autoptr(GFlagsClass) klass = g_type_class_ref (LRG_TYPE_NAV_DEBUG_FLAGS);
	guint                  i;

	*flags = 0;
	*handles = FALSE;
	for (i = 0; a != NULL && i < json_array_get_length (a); i++)
	{
		const gchar *nick = json_array_get_string_element (a, i);
		GFlagsValue *fv;

		if (g_strcmp0 (nick, "handles") == 0)
		{
			*handles = TRUE;
			continue;
		}
		/* the two combinations have no nick of their own */
		if (g_strcmp0 (nick, "default") == 0 || g_strcmp0 (nick, "all") == 0)
		{
			*flags |= (nick[0] == 'd') ? LRG_NAV_DEBUG_DEFAULT : LRG_NAV_DEBUG_ALL;
			continue;
		}
		fv = (nick != NULL) ? g_flags_get_value_by_nick (klass, nick) : NULL;
		if (fv == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown overlay flag '%s'", nick != NULL ? nick : "(null)");
			return FALSE;
		}
		*flags |= fv->value;
	}
	return TRUE;
}

static JsonNode *
m_overlay (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	guint              flags, set, on = 0, off = 0;
	gboolean           h_set = FALSE, h_on = FALSE, h_off = FALSE;
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	flags = self->overlay;
	if (params != NULL && json_object_has_member (params, "flags"))
	{
		if (!parse_overlay (json_object_get_array_member (params, "flags"), &set, &h_set, error))
			return NULL;
		flags = set;
		lrg_editor_nav_panel_set_show_handles (self, h_set);
	}
	if (params != NULL && json_object_has_member (params, "on") &&
	    !parse_overlay (json_object_get_array_member (params, "on"), &on, &h_on, error))
		return NULL;
	if (params != NULL && json_object_has_member (params, "off") &&
	    !parse_overlay (json_object_get_array_member (params, "off"), &off, &h_off, error))
		return NULL;
	if (h_on)
		lrg_editor_nav_panel_set_show_handles (self, TRUE);
	if (h_off)
		lrg_editor_nav_panel_set_show_handles (self, FALSE);
	lrg_editor_nav_panel_set_overlay (self, (LrgNavDebugFlags) ((flags | on) & ~off));
	sync_widgets (self);
	o = json_object_new ();
	json_object_set_array_member (o, "flags", overlay_array (self));
	return obj_node (o);
}

static JsonNode *
m_agents (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	LrgNavCrowd       *crowd;
	JsonObject        *o;
	JsonArray         *agents = json_array_new ();
	gint               dbg;
	guint              i, k;

	if (self == NULL)
	{
		json_array_unref (agents);
		return NULL;
	}
	crowd = lrg_editor_nav_panel_get_crowd (self);
	dbg = lrg_editor_nav_panel_get_debug_agent (self);
	o = json_object_new ();
	json_object_set_boolean_member (o, "playing", is_playing (self));
	json_object_set_double_member (o, "step_seconds", crowd != NULL ? lrg_nav_crowd_get_last_step_seconds (crowd) : 0.0);
	json_object_set_int_member (o, "step_count", crowd != NULL ? (gint64) lrg_nav_crowd_get_step_count (crowd) : 0);
	if (dbg >= 0)
		json_object_set_int_member (o, "debug", dbg);
	else
		json_object_set_null_member (o, "debug");
	for (i = 0; crowd != NULL && i < lrg_nav_crowd_get_n_agents (crowd); i++)
	{
		LrgNavAgent      *agent = lrg_nav_crowd_get_agent (crowd, i);
		JsonObject       *a = json_object_new ();
		JsonArray        *path = json_array_new ();
		LrgNode          *node = NULL;
		g_autofree gchar *name = agent_name (self, agent, i, &node);
		gfloat            p[3], vel[3], dest[3];
		const gfloat     *pts;
		guint             nf = 0;

		lrg_nav_agent_get_position (agent, p);
		lrg_nav_agent_get_velocity (agent, vel);
		json_object_set_int_member (a, "index", i);
		json_object_set_string_member (a, "entity", name);
		set_string_or_null (a, "guid", node != NULL ? lrg_node_get_guid (node) : NULL);
		if (node != NULL)
		{
			g_autofree gchar *np = lrg_node_get_path (node);

			json_object_set_string_member (a, "node", np);
		}
		else
			json_object_set_null_member (a, "node");
		json_object_set_string_member (a, "state", agent_state_nick (lrg_nav_agent_get_state (agent)));
		json_object_set_array_member (a, "position", vec3_array (p));
		json_object_set_array_member (a, "velocity", vec3_array (vel));
		json_object_set_double_member (a, "speed", sqrtf (vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]));
		json_object_set_double_member (a, "remaining", lrg_nav_agent_get_remaining_distance (agent));
		if (lrg_nav_agent_get_destination (agent, dest))
			json_object_set_array_member (a, "destination", vec3_array (dest));
		else
			json_object_set_null_member (a, "destination");
		pts = lrg_nav_agent_get_path (agent, &nf);
		for (k = 0; pts != NULL && k + 2 < nf; k += 3)
			json_array_add_array_element (path, vec3_array (pts + k));
		json_object_set_array_member (a, "path", path);
		json_object_set_int_member (a, "corner", lrg_nav_agent_get_corner_index (agent));
		json_array_add_object_element (agents, a);
	}
	json_object_set_array_member (o, "agents", agents);
	return obj_node (o);
}

static JsonNode *
m_debug_agent (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorNavPanel *self = ipc_panel (user_data, error);
	LrgNavCrowd       *crowd;
	gint               index = -1;
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	crowd = lrg_editor_nav_panel_get_crowd (self);
	if (crowd == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "Not playing, or the play world has no navigation mesh");
		return NULL;
	}
	if (params != NULL && json_object_has_member (params, "index"))
		index = (gint) json_object_get_int_member (params, "index");
	else if (params != NULL && (json_object_has_member (params, "guid") || json_object_has_member (params, "node")))
	{
		const gchar *ref = json_object_has_member (params, "guid") ? json_object_get_string_member (params, "guid")
		                                                           : json_object_get_string_member (params, "node");
		LrgNode     *want = (ref != NULL) ? lrg_editor_find_node (self->editor, ref) : NULL;
		guint        i;

		for (i = 0; want != NULL && i < lrg_nav_crowd_get_n_agents (crowd); i++)
		{
			LrgNode          *node = NULL;
			g_autofree gchar *name = agent_name (self, lrg_nav_crowd_get_agent (crowd, i), i, &node);

			if (node != NULL && g_strcmp0 (lrg_node_get_guid (node), lrg_node_get_guid (want)) == 0)
				index = (gint) i;
		}
		if (index < 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No agent for '%s'",
			             ref != NULL ? ref : "");
			return NULL;
		}
	}
	else if (params == NULL || !json_object_get_boolean_member_with_default (params, "clear", FALSE))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Give \"index\", \"guid\", \"node\" or \"clear\": true");
		return NULL;
	}
	if (!lrg_editor_nav_panel_set_debug_agent (self, index))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No agent %d", index);
		return NULL;
	}
	o = json_object_new ();
	if (lrg_editor_nav_panel_get_debug_agent (self) >= 0)
		json_object_set_int_member (o, "debug", lrg_editor_nav_panel_get_debug_agent (self));
	else
		json_object_set_null_member (o, "debug");
	return obj_node (o);
}

static JsonNode *
m_show (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	editor_app_show_panel (app, LRG_EDITOR_NAV_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "panel", LRG_EDITOR_NAV_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_NAV_PANEL_ID));
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_NAV_PANEL_ID);
}

static void
a_bake (gpointer data)
{
	if (!lrg_editor_nav_panel_get_baking (LRG_EDITOR_NAV_PANEL (data)))
		on_bake_chip (NULL, data);
}

static void
a_cancel (gpointer data)
{
	lrg_editor_nav_panel_cancel (LRG_EDITOR_NAV_PANEL (data));
}

static void
a_save (gpointer data)
{
	on_save_chip (NULL, data);
}

static void
a_clear (gpointer data)
{
	lrg_editor_nav_panel_clear (LRG_EDITOR_NAV_PANEL (data));
}

static void
a_new (gpointer data)
{
	on_new_chip (NULL, data);
}

static gboolean
has_level (gpointer data)
{
	return current_level (LRG_EDITOR_NAV_PANEL (data)) != NULL;
}

static gboolean
can_bake (gpointer data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (data);

	return current_level (self) != NULL && !is_playing (self) && !lrg_editor_nav_panel_get_baking (self);
}

static gboolean
is_baking (gpointer data)
{
	return lrg_editor_nav_panel_get_baking (LRG_EDITOR_NAV_PANEL (data));
}

/* The checkable overlay toggles of the viewport overlays menu. */
static const struct
{
	const gchar      *id;
	const gchar      *label;
	LrgNavDebugFlags  flag;
} overlay_actions[] = {
	{ "nav.overlay-polygons", "Navigation Mesh", LRG_NAV_DEBUG_POLYGONS },
	{ "nav.overlay-areas", "Colour by Area", LRG_NAV_DEBUG_AREAS },
	{ "nav.overlay-edges", "Polygon Edges", LRG_NAV_DEBUG_POLYGON_EDGES },
	{ "nav.overlay-boundaries", "Boundaries", LRG_NAV_DEBUG_BOUNDARIES },
	{ "nav.overlay-links", "Off-mesh Links", LRG_NAV_DEBUG_LINKS },
	{ "nav.overlay-agents", "Agents (playing)", LRG_NAV_DEBUG_AGENTS },
	{ "nav.overlay-paths", "Agent Paths (playing)", LRG_NAV_DEBUG_PATHS },
	{ "nav.overlay-orca", "ORCA, Debug Agent (playing)", LRG_NAV_DEBUG_ORCA | LRG_NAV_DEBUG_NEIGHBOURS },
};

/* Each toggle's data is the panel; the flag is found by the id the
 * action system passes back through a small per-toggle record. */
typedef struct
{
	LrgEditorNavPanel *panel;   /* weak by construction: lives as long as the panel */
	LrgNavDebugFlags   flag;
} OverlayToggle;

static void
a_overlay (gpointer data)
{
	OverlayToggle     *t = data;
	LrgEditorNavPanel *self = t->panel;
	LrgNavDebugFlags   flags = self->overlay;

	/* turning one on from "off" starts from nothing but it */
	if ((flags & t->flag) == t->flag)
		flags &= ~t->flag;
	else
		flags |= t->flag;
	lrg_editor_nav_panel_set_overlay (self, flags);
}

static gboolean
overlay_checked (gpointer data)
{
	OverlayToggle *t = data;

	return (t->panel->overlay & t->flag) == t->flag;
}

static void
a_handles (gpointer data)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (data);

	lrg_editor_nav_panel_set_show_handles (self, !self->show_handles);
}

static gboolean
handles_checked (gpointer data)
{
	return LRG_EDITOR_NAV_PANEL (data)->show_handles;
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static void
lrg_editor_nav_panel_layout_children (LrgContainer *container)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (container);

	editor_panel_fit_toolbar (self->header_row, lrg_widget_get_width (LRG_WIDGET (self)) - 20.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_nav_panel_dispose (GObject *object)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (object);

	self->bake_serial++;
	if (self->runtime != NULL && self->overlay_id != 0)
		g_clear_signal_handler (&self->overlay_id, self->runtime);
	g_clear_weak_pointer (&self->runtime);
	if (self->form != NULL)
		g_signal_handlers_disconnect_by_data (self->form, self);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->editor);
	g_clear_object (&self->play);
	g_clear_weak_pointer (&self->app);
	g_clear_weak_pointer (&self->level);
	g_clear_object (&self->gesture_proxy);
	g_clear_object (&self->bake);
	g_clear_object (&self->mesh);
	g_clear_object (&self->draw);
	g_clear_object (&self->form);
	G_OBJECT_CLASS (lrg_editor_nav_panel_parent_class)->dispose (object);
}

static void
lrg_editor_nav_panel_finalize (GObject *object)
{
	LrgEditorNavPanel *self = LRG_EDITOR_NAV_PANEL (object);

	g_free (self->state);
	g_free (self->settings_path);
	g_free (self->disk_yaml);
	g_free (self->disk_for);
	g_free (self->default_yaml);
	g_free (self->area_sig);
	g_free (self->mesh_from);
	g_free (self->error);
	g_free (self->drag_before);
	g_free (self->combo_ref);
	g_ptr_array_unref (self->settings_files);
	G_OBJECT_CLASS (lrg_editor_nav_panel_parent_class)->finalize (object);
}

static void
lrg_editor_nav_panel_class_init (LrgEditorNavPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_nav_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_nav_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_nav_panel_layout_children;
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgWidget *
custom (const gchar                *name,
        const LrgCustomWidgetFuncs *funcs,
        gpointer                    data)
{
	LrgWidget *w = LRG_WIDGET (lrg_custom_widget_new ());

	lrg_widget_set_name (w, name);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (w), funcs, data, NULL);
	return w;
}

/* A grid over the form showing only the rows in [first, last]. */
static LrgPropertyGrid *
make_grid (LrgEditorNavPanel *self,
           const gchar       *name,
           guint              first,
           guint              last,
           gfloat             label_width)
{
	g_autoptr(LrgObjectPropertySource) src = lrg_object_property_source_new (G_OBJECT (self->form));
	LrgPropertyGrid                   *grid = lrg_property_grid_new ();
	guint                              i;

	for (i = 1; i < F_N; i++)
		if (i < first || i > last)
			lrg_object_property_source_hide_property (src, form_props[i]->name);
	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, label_width);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	return grid;
}

/* A sub heading row: a label, then the given widgets right aligned. */
static LrgWidget *
heading_row (const gchar *name,
             const gchar *text)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_add_class (label, "heading");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (label, 1.0f);
	add_to (row, label);
	return row;
}

static void
lrg_editor_nav_panel_init (LrgEditorNavPanel *self)
{
	static const LrgCustomWidgetFuncs pill_funcs = { pill_measure, pill_paint, pill_step, NULL, { NULL } };
	static const LrgCustomWidgetFuncs summary_funcs = { summary_measure, summary_paint, pill_step, NULL, { NULL } };
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	static const LrgCustomWidgetFuncs agent_funcs = { agents_measure, agents_paint, agents_step, agents_event, { NULL } };
	static const LrgCustomWidgetFuncs footer_funcs = { footer_measure, footer_paint, agents_step, NULL, { NULL } };
	const LrgInsets                   pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	LrgWidget                        *row;
	LrgWidget                        *w;
	LrgWidget                        *box;

	self->sel_area = self->sel_box = self->sel_link = -1;
	self->debug_agent = -1;
	self->auto_save = TRUE;
	self->show_handles = TRUE;
	self->overlay_last = OVERLAY_DEFAULT;
	self->bounds_min[0] = -50.0f;
	self->bounds_min[1] = -10.0f;
	self->bounds_min[2] = -50.0f;
	self->bounds_max[0] = 50.0f;
	self->bounds_max[1] = 30.0f;
	self->bounds_max[2] = 50.0f;
	self->settings_files = g_ptr_array_new_with_free_func (g_free);
	self->bake = lrg_nav_bake_new ();
	self->default_yaml = lrg_nav_bake_to_data (self->bake);
	self->draw = lrg_debug_draw_new ();
	self->form = g_object_new (EDITOR_TYPE_NAV_FORM, NULL);
	g_signal_connect (self->form, "notify", G_CALLBACK (on_form_notify), self);

	/* Header: title, status, Bake / Cancel, Save, overlay, Clear */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "nav-top");
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 6.0f);
	lrg_widget_set_margin (self->top, &pad);

	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "nav-header");
	self->header_row = row;
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	w = LRG_WIDGET (lrg_label_new ("Navigation"));
	lrg_widget_set_name (w, "nav-title");
	lrg_widget_add_class (w, "heading");
	lrg_widget_set_valign (w, LRG_ALIGN_CENTER);
	add_to (row, w);
	self->status = custom ("nav-status", &pill_funcs, self);
	lrg_widget_set_valign (self->status, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (self->status, 1.0f);
	add_to (row, self->status);
	self->bake_chip = editor_chip_new ("nav-bake", LRG_ICON_REFRESH, "Bake", on_bake_chip, self);
	editor_chip_set_tint (self->bake_chip, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->bake_chip);
	self->save_chip = editor_chip_new ("nav-save", LRG_ICON_DOWNLOAD, "Save", on_save_chip, self);
	add_to (row, self->save_chip);
	self->overlay_chip = editor_chip_new ("nav-overlay", LRG_ICON_EYE, NULL, on_overlay_chip, self);
	add_to (row, self->overlay_chip);
	self->clear_chip = editor_chip_new ("nav-clear", LRG_ICON_TRASH, NULL, on_clear_chip, self);
	editor_chip_set_tint (self->clear_chip, LRG_THEME_COLOR_DANGER);
	add_to (row, self->clear_chip);
	add_to (self->top, row);

	/* Progress */
	self->bar = lrg_progress_bar_new ();
	lrg_widget_set_name (LRG_WIDGET (self->bar), "nav-progress");
	lrg_progress_bar_set_max (self->bar, 1.0);
	lrg_widget_set_visible (LRG_WIDGET (self->bar), FALSE);
	add_to (self->top, LRG_WIDGET (self->bar));
	self->phase_label = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->phase_label, "nav-phase");
	lrg_widget_add_class (self->phase_label, "dim");
	lrg_widget_set_visible (self->phase_label, FALSE);
	add_to (self->top, self->phase_label);

	/* Summary */
	self->summary = custom ("nav-summary", &summary_funcs, self);
	add_to (self->top, self->summary);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* Scrolled body */
	box = LRG_WIDGET (lrg_vbox_new ());
	self->content = box;
	lrg_widget_set_name (box, "nav-body");
	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	lrg_widget_set_margin (box, &pad);

	/* Agents (playing), on top where they are seen */
	{
		LrgWidget *inner = LRG_WIDGET (lrg_vbox_new ());

		lrg_container_set_spacing (LRG_CONTAINER (inner), 4.0f);
		self->agent_list = add_to (inner, custom ("nav-agent-list", &agent_funcs, self));
		self->agent_footer = add_to (inner, custom ("nav-agent-footer", &footer_funcs, self));
		self->agents_section = lrg_expander_new ("Agents");
		lrg_widget_set_name (LRG_WIDGET (self->agents_section), "nav-agents-section");
		lrg_expander_set_child (self->agents_section, inner);
		g_object_unref (inner);
		lrg_expander_set_expanded (self->agents_section, TRUE);
		lrg_widget_set_visible (LRG_WIDGET (self->agents_section), FALSE);
		add_to (box, self->agents_section);
	}

	/* Settings file: picker and New */
	row = heading_row ("nav-settings-heading", "Settings");
	self->settings_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->settings_combo), "nav-settings-file");
	lrg_widget_set_valign (LRG_WIDGET (self->settings_combo), LRG_ALIGN_CENTER);
	lrg_widget_set_max_size (LRG_WIDGET (self->settings_combo), 200.0f, 0.0f);
	g_signal_connect (self->settings_combo, "changed", G_CALLBACK (on_settings_combo), self);
	add_to (row, self->settings_combo);
	add_to (row, editor_chip_new ("nav-new", LRG_ICON_PLUS, "New", on_new_chip, self));
	add_to (box, row);
	self->settings_path_label = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->settings_path_label, "nav-settings-path");
	lrg_widget_add_class (self->settings_path_label, "dim");
	add_to (box, self->settings_path_label);

	/* The form */
	self->grid = add_to (box, make_grid (self, "nav-grid", F_DATA, F_SETTINGS_LAST, 120.0f));

	/* Areas */
	row = heading_row ("nav-areas-heading", "Areas");
	add_to (row, editor_chip_new ("nav-add-area", LRG_ICON_PLUS, "Add", on_add_area, self));
	add_to (box, row);
	self->area_list = add_to (box, custom ("nav-area-list", &list_funcs, self));
	self->area_grid = add_to (box, make_grid (self, "nav-area-grid", F_AREA_NAME, F_AREA_COST, 100.0f));
	self->box_heading = add_to (box, heading_row ("nav-boxes-heading", "Boxes"));
	self->box_list = add_to (box, custom ("nav-box-list", &list_funcs, self));
	self->box_grid = add_to (box, make_grid (self, "nav-box-grid", F_BOX_MIN, F_BOX_MAX, 100.0f));

	/* Links */
	row = heading_row ("nav-links-heading", "Off-mesh links");
	add_to (row, editor_chip_new ("nav-add-link", LRG_ICON_PLUS, "Add", on_add_link, self));
	add_to (box, row);
	self->link_list = add_to (box, custom ("nav-link-list", &list_funcs, self));
	self->link_grid = add_to (box, make_grid (self, "nav-link-grid", F_LINK_START, F_LINK_AREA, 100.0f));

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "nav-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), box);
	g_object_unref (box);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "nav-empty");
	lrg_widget_set_margin (self->empty, &pad);
	add_to (self->empty, editor_panel_hint_new ("nav-hint",
	                                            "Open a 3D level to bake its navigation mesh: set the agent size, "
	                                            "mark areas (mud, doors) and off-mesh links (ladders, jumps), then "
	                                            "Bake. Entities with a nav-agent component walk it when you play."));
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);
}

LrgEditorNavPanel *
lrg_editor_nav_panel_new (LrgEditorApp *app)
{
	LrgEditorNavPanel *self;
	LrgEditorActions  *actions;
	guint              i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_NAV_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "nav-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->play = (app->play != NULL) ? g_object_ref (app->play) : NULL;
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect_swapped (self->editor, "saved", G_CALLBACK (on_editor_saved), self);
	if (self->play != NULL)
	{
		g_signal_connect_swapped (self->play, "started", G_CALLBACK (on_play_started), self);
		g_signal_connect_swapped (self->play, "stopped", G_CALLBACK (on_play_stopped), self);
	}

	/* The 3D overlay: drawn inside every viewport's 3D pass */
	if (app->renderer != NULL)
	{
		LrgWorld3DRenderer *runtime = lrg_editor_scene_renderer_get_runtime_renderer (app->renderer);

		if (runtime != NULL)
		{
			g_set_weak_pointer (&self->runtime, G_OBJECT (runtime));
			self->overlay_id = g_signal_connect (runtime, "draw-overlay", G_CALLBACK (on_runtime_draw_overlay), self);
		}
	}

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "nav.show", "Navigation Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "nav.open-settings", "Open Navigation Settings", "View", NULL, a_show, NULL, NULL,
	                        self);
	lrg_editor_actions_add (actions, "nav.bake", "Bake Navigation", "Level", NULL, a_bake, can_bake, NULL, self);
	lrg_editor_actions_add (actions, "nav.cancel", "Cancel Navigation Bake", "Level", NULL, a_cancel, is_baking, NULL,
	                        self);
	lrg_editor_actions_add (actions, "nav.save", "Save Navigation", "Level", NULL, a_save, has_level, NULL, self);
	lrg_editor_actions_add (actions, "nav.clear", "Clear Baked Navigation", "Level", NULL, a_clear, has_level, NULL,
	                        self);
	lrg_editor_actions_add (actions, "nav.new-settings", "New Navigation Settings", "Level", NULL, a_new, has_level,
	                        NULL, self);
	for (i = 0; i < G_N_ELEMENTS (overlay_actions); i++)
	{
		OverlayToggle *t = g_new0 (OverlayToggle, 1);

		t->panel = self;
		t->flag = overlay_actions[i].flag;
		/* owned by the panel (freed with it) */
		g_object_set_data_full (G_OBJECT (self), overlay_actions[i].id, t, g_free);
		lrg_editor_actions_add (actions, overlay_actions[i].id, overlay_actions[i].label, "View", NULL, a_overlay, NULL,
		                        overlay_checked, t);
	}
	lrg_editor_actions_add (actions, "nav.overlay-handles", "Area and Link Handles", "View", NULL, a_handles, NULL,
	                        handles_checked, self);
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "nav.get", m_get, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.set", m_set, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.new", m_new, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.bake", m_bake, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.cancel", m_cancel, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.save", m_save, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.clear", m_clear, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.add", m_area_add, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.remove", m_area_remove, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.set", m_area_set, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.box.add", m_box_add, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.box.remove", m_box_remove, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.area.box.set", m_box_set, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.link.add", m_link_add, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.link.remove", m_link_remove, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.link.set", m_link_set, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.select", m_select, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.overlay", m_overlay, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.agents", m_agents, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.debug_agent", m_debug_agent, app, NULL);
		lrg_editor_server_register_method (app->server, "nav.show", m_show, app, NULL);
	}

	follow_level (self);
	return self;
}

LrgEditorNavPanel *
lrg_editor_nav_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
