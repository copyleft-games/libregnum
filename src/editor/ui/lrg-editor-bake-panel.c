/* lrg-editor-bake-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBakePanel: the lighting bake in lrgeditor (0.9.0 M3).
 *
 * Pieces, top to bottom:
 *
 *   header    title, a status pill (not baked / baked / unsaved / baking /
 *             cancelled / failed) and the Bake / Cancel, Save and Clear
 *             chips, plus the "preview baked lighting" toggle
 *   progress  an LrgProgressBar and a phase line while a bake runs
 *   summary   a custom widget: objects, lights, texels, pages, probe
 *             grids, the bake time and where the result lives
 *   atlas     a custom widget painting the selected atlas page (tone
 *             mapped, a texture with a window, coarse cells headless) with
 *             page arrows
 *   cursor    "Light at cursor": a chip that probes the light under the
 *             active viewport's centre and a swatch with the value
 *   grid      an LrgPropertyGrid over an LrgLightmapBakeSettings that
 *             mirrors `lighting: settings:`
 *
 * Undo.  Everything the panel writes into the level (a setting, the
 * references a save writes, a clear) goes through one tiny private
 * object, EditorBakeProxy, whose one property "state" is the whole
 * `lighting:` block as a string (bake_state_capture()); setting it makes
 * the level's block equal to that string (bake_state_apply()).  Each edit
 * records "state went from before to after" as one set-property command
 * (lrg_editor_execute_merged() folds a slider drag into one step).
 *
 * Bake.  bake_prepare() instantiates the level into a private LrgWorld3D
 * with the level's directory as base (the play session's rule), gives
 * mesh renderers the viewport's cached models so the baker sees exactly
 * the geometry the renderer draws, steps the world once (lights register
 * when entering), drops the world's old lightmap and grids, and hands it
 * to an LrgLightmapBaker together with one volume per visible
 * probe-volume node (lrg_node_visual_get_probe_volume()).  The baker runs
 * on its thread pool; "progress" arrives on the main context and drives
 * the bar.  The result is shown in the viewport at once
 * (lrg_editor_scene_renderer_set_baked_override()) and, with auto save
 * and a level file, saved next to the level.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-bake-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-scene-renderer.h"
#include "lrg-editor-gizmo-paint.h"
#include "../lrg-editor.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-command.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-actions.h"
#include "../lrg-editor-view-state.h"
#include "../../level/lrg-level.h"
#include "../../level/lrg-level-instantiate.h"
#include "../../level/lrg-node.h"
#include "../../level/lrg-node-visual.h"
#include "../../lighting/lrg-lightmap3d.h"
#include "../../lighting/lrg-probe-grid.h"
#include "../../graphics/lrg-hdr-image.h"
#include "../../world3d/lrg-world3d.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../../world3d/components/lrg-mesh-renderer-component.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-progress-bar.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-property-source.h"
#include "../../paint/lrg-paint-icons.h"
#include <json-glib/json-glib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define PANEL_KEY        "lrg-editor-bake-panel"
#define SUMMARY_HEIGHT   (70.0f)
#define ATLAS_HEIGHT     (150.0f)
#define ATLAS_MAX_EDGE   (256u)
#define ATLAS_CELLS      (24u)
#define CURSOR_HEIGHT    (30.0f)

/* ==========================================================================
 * The `lighting:` block as one string (the undo value)
 * ========================================================================== */

/* "L\t<lightmap>\n", "P\t<probes>\n"..., "S\t<key>\t<value>\n"... */
static gchar *
bake_state_capture (LrgLevel *level)
{
	GString             *s = g_string_new (NULL);
	g_autoptr(GPtrArray) keys = NULL;
	GPtrArray           *probes;
	const gchar         *lm;
	guint                i;

	if (level == NULL)
		return g_string_free (s, FALSE);
	lm = lrg_level_get_lightmap_ref (level);
	if (lm != NULL && *lm != '\0')
		g_string_append_printf (s, "L\t%s\n", lm);
	probes = lrg_level_get_probe_refs (level);
	for (i = 0; i < probes->len; i++)
		g_string_append_printf (s, "P\t%s\n", (const gchar *) g_ptr_array_index (probes, i));
	keys = lrg_level_get_bake_setting_keys (level);
	for (i = 0; keys != NULL && i < keys->len; i++)
	{
		const gchar *key = g_ptr_array_index (keys, i);
		gdouble      v = 0.0;
		gchar        buf[G_ASCII_DTOSTR_BUF_SIZE];

		if (lrg_level_get_bake_setting (level, key, &v))
			g_string_append_printf (s, "S\t%s\t%s\n", key, g_ascii_dtostr (buf, sizeof buf, v));
	}
	return g_string_free (s, FALSE);
}

/* Makes @level's `lighting:` block what @state says. */
static void
bake_state_apply (LrgLevel    *level,
                  const gchar *state)
{
	g_auto(GStrv) lines = NULL;
	guint         i;

	if (level == NULL)
		return;
	lrg_level_clear_lighting (level);
	if (state == NULL)
		return;
	lines = g_strsplit (state, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_auto(GStrv) f = g_strsplit (lines[i], "\t", 3);

		if (f[0] == NULL || f[1] == NULL)
			continue;
		if (g_strcmp0 (f[0], "L") == 0)
			lrg_level_set_lightmap_ref (level, f[1]);
		else if (g_strcmp0 (f[0], "P") == 0)
			lrg_level_add_probe_ref (level, f[1]);
		else if (g_strcmp0 (f[0], "S") == 0 && f[2] != NULL)
			lrg_level_set_bake_setting (level, f[1], g_ascii_strtod (f[2], NULL));
	}
}

/* ==========================================================================
 * EditorBakeProxy: the undo target
 * ========================================================================== */

#define EDITOR_TYPE_BAKE_PROXY (editor_bake_proxy_get_type ())
G_DECLARE_FINAL_TYPE (EditorBakeProxy, editor_bake_proxy, EDITOR, BAKE_PROXY, GObject)

struct _EditorBakeProxy
{
	GObject   parent_instance;
	LrgLevel *level;   /* owned: the level the edits belong to */
	gchar    *state;   /* last applied block */
};

G_DEFINE_FINAL_TYPE (EditorBakeProxy, editor_bake_proxy, G_TYPE_OBJECT)

enum { PROXY_PROP_0, PROXY_PROP_STATE, PROXY_N_PROPS };

static void
editor_bake_proxy_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	EditorBakeProxy *self = EDITOR_BAKE_PROXY (object);

	if (prop_id == PROXY_PROP_STATE)
		g_value_set_string (value, self->state);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
editor_bake_proxy_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	EditorBakeProxy  *self = EDITOR_BAKE_PROXY (object);
	g_autofree gchar *now = NULL;

	if (prop_id != PROXY_PROP_STATE)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	g_free (self->state);
	self->state = g_value_dup_string (value);
	/* only touch the level when it differs (the first execute is a no-op) */
	now = bake_state_capture (self->level);
	if (g_strcmp0 (now, self->state != NULL ? self->state : "") != 0)
		bake_state_apply (self->level, self->state);
	g_object_notify_by_pspec (object, pspec);
}

static void
editor_bake_proxy_finalize (GObject *object)
{
	EditorBakeProxy *self = EDITOR_BAKE_PROXY (object);

	g_clear_object (&self->level);
	g_free (self->state);
	G_OBJECT_CLASS (editor_bake_proxy_parent_class)->finalize (object);
}

static void
editor_bake_proxy_class_init (EditorBakeProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = editor_bake_proxy_get_property;
	object_class->set_property = editor_bake_proxy_set_property;
	object_class->finalize = editor_bake_proxy_finalize;
	g_object_class_install_property (object_class, PROXY_PROP_STATE,
		g_param_spec_string ("state", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
}

static void
editor_bake_proxy_init (EditorBakeProxy *self)
{
}

/* ==========================================================================
 * The panel
 * ========================================================================== */

typedef enum
{
	STATE_IDLE,
	STATE_PREPARE,
	STATE_LIGHTMAPS,
	STATE_POSTPROCESS,
	STATE_PROBES,
	STATE_DONE,
	STATE_CANCELLED,
	STATE_FAILED
} BakeState;

static const gchar * const state_names[] = {
	"idle", "prepare", "lightmaps", "postprocess", "probes", "done", "cancelled", "failed"
};

static const gchar * const phase_labels[] = {
	"Ready", "Collecting the scene", "Tracing lightmaps", "Denoising and stitching", "Baking probes",
	"Done", "Cancelled", "Failed"
};

struct _LrgEditorBakePanel
{
	LrgContainer             parent_instance;

	LrgEditorApp            *app;       /* weak */
	LrgEditor               *editor;    /* ref */
	LrgLevel                *level;     /* weak: the level the panel follows */

	/* Settings */
	LrgLightmapBakeSettings *settings;  /* mirrors `lighting: settings:` */
	gboolean                 applying;  /* the panel is writing: ignore echoes */
	gboolean                 in_gesture;
	guint                    gesture_id;
	EditorBakeProxy         *gesture_proxy;

	/* Bake */
	LrgLightmapBaker        *baker;     /* the last bake's (kept for evaluate) */
	LrgWorld3D              *world;     /* the scene it was collected from */
	GCancellable            *cancel;
	BakeState                state;
	gdouble                  progress;
	gchar                   *error;
	gint64                   started;
	gboolean                 auto_save;

	/* Result */
	LrgLightmap3D           *lightmap;  /* ref */
	GPtrArray               *grids;     /* LrgProbeGrid refs */
	gboolean                 unsaved;
	guint                    n_objects;
	guint                    n_lights;
	guint                    n_texels;
	gdouble                  bake_time;

	/* Atlas preview */
	guint                    page;
	GrlTexture              *atlas_tex;
	gpointer                 atlas_for;   /* lightmap the texture shows */
	guint                    atlas_page;
	guint                    atlas_w;     /* texels of the shown (cropped) region */
	guint                    atlas_h;
	guint8                   cells[ATLAS_CELLS * ATLAS_CELLS * 3];
	gboolean                 cells_valid;

	/* Light at cursor */
	gboolean                 cursor_valid;
	gfloat                   cursor_pos[3];
	gfloat                   cursor_rgb[3];

	/* Widgets */
	LrgWidget               *top;
	LrgWidget               *header_row;
	LrgWidget               *status;
	LrgWidget               *bake_chip;
	LrgWidget               *save_chip;
	LrgWidget               *clear_chip;
	LrgWidget               *preview_chip;
	LrgProgressBar          *bar;
	LrgWidget               *phase_label;
	LrgWidget               *summary;
	LrgWidget               *atlas_row;
	LrgWidget               *atlas;
	LrgWidget               *cursor;
	LrgWidget               *scroll;
	LrgPropertyGrid         *grid;
	LrgWidget               *empty;
};

G_DEFINE_FINAL_TYPE (LrgEditorBakePanel, lrg_editor_bake_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorBakePanel *self);
static void sync_settings (LrgEditorBakePanel *self);

static LrgLevel *
current_level (LrgEditorBakePanel *self)
{
	return (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
}

static LrgEditorSceneRenderer *
scene_renderer (LrgEditorBakePanel *self)
{
	return (self->app != NULL) ? self->app->renderer : NULL;
}

/* "texels-per-unit" -> "texels_per_unit" (a newly allocated string). */
static gchar *
key_from_name (const gchar *name)
{
	return g_strdelimit (g_strdup (name), "-", '_');
}

/* ==========================================================================
 * Commands
 * ========================================================================== */

/*
 * Records "the block went from @before to @after" (already applied) as
 * one command; @merge folds it into the gesture's previous step.
 */
static void
commit (LrgEditorBakePanel *self,
        LrgLevel           *level,
        const gchar        *before,
        const gchar        *after,
        const gchar        *label,
        gboolean            merge)
{
	g_auto(GValue)    vb = G_VALUE_INIT;
	g_auto(GValue)    va = G_VALUE_INIT;
	EditorBakeProxy  *proxy;
	LrgEditorCommand *cmd;

	if (g_strcmp0 (before, after) == 0)
		return;
	if (merge && self->gesture_proxy != NULL && self->gesture_proxy->level == level)
		proxy = g_object_ref (self->gesture_proxy);
	else
	{
		proxy = g_object_new (EDITOR_TYPE_BAKE_PROXY, NULL);
		proxy->level = g_object_ref (level);
		if (merge)
			g_set_object (&self->gesture_proxy, proxy);
	}

	g_value_init (&vb, G_TYPE_STRING);
	g_value_set_string (&vb, before != NULL ? before : "");
	g_value_init (&va, G_TYPE_STRING);
	g_value_set_string (&va, after != NULL ? after : "");
	cmd = lrg_editor_command_new_set_property (G_OBJECT (proxy), "state", &vb, &va);
	lrg_editor_command_set_label (cmd, label != NULL ? label : "Edit Bake Settings");

	self->applying = TRUE;
	if (merge)
		lrg_editor_execute_merged (self->editor, cmd);
	else
		lrg_editor_execute (self->editor, cmd);
	self->applying = FALSE;
	g_object_unref (proxy);
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

/* Makes self->settings the defaults plus the level's keys. */
static void
sync_settings (LrgEditorBakePanel *self)
{
	LrgLevel    *level = current_level (self);
	GParamSpec **props;
	guint        n = 0;
	guint        i;

	self->applying = TRUE;
	props = g_object_class_list_properties (G_OBJECT_GET_CLASS (self->settings), &n);
	g_object_freeze_notify (G_OBJECT (self->settings));
	for (i = 0; i < n; i++)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autofree gchar *key = key_from_name (props[i]->name);
		gdouble           d = 0.0;

		if (!(props[i]->flags & G_PARAM_WRITABLE))
			continue;
		g_value_init (&v, props[i]->value_type);
		if (level != NULL && lrg_level_get_bake_setting (level, key, &d))
		{
			g_auto(GValue) dv = G_VALUE_INIT;

			g_value_init (&dv, G_TYPE_DOUBLE);
			g_value_set_double (&dv, d);
			if (props[i]->value_type == G_TYPE_BOOLEAN)
				g_value_set_boolean (&v, d != 0.0);
			else if (!g_value_transform (&dv, &v))
				g_param_value_set_default (props[i], &v);
			g_param_value_validate (props[i], &v);
		}
		else
			g_param_value_set_default (props[i], &v);
		g_object_set_property (G_OBJECT (self->settings), props[i]->name, &v);
	}
	g_object_thaw_notify (G_OBJECT (self->settings));
	g_free (props);
	self->applying = FALSE;
}

/* A settings property as a double (booleans 0 / 1). */
static gboolean
settings_double (LrgLightmapBakeSettings *settings,
                 GParamSpec              *pspec,
                 gdouble                 *out)
{
	g_auto(GValue) v = G_VALUE_INIT;
	g_auto(GValue) d = G_VALUE_INIT;

	g_value_init (&v, pspec->value_type);
	g_object_get_property (G_OBJECT (settings), pspec->name, &v);
	if (G_VALUE_HOLDS_BOOLEAN (&v))
	{
		*out = g_value_get_boolean (&v) ? 1.0 : 0.0;
		return TRUE;
	}
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_transform (&v, &d))
		return FALSE;
	*out = g_value_get_double (&d);
	return TRUE;
}

/* The grid (or anyone) changed a property of self->settings. */
static void
on_settings_notify (GObject    *settings,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgLevel           *level = current_level (self);
	g_autofree gchar   *key = NULL;
	g_autofree gchar   *before = NULL;
	g_autofree gchar   *after = NULL;
	g_autofree gchar   *label = NULL;
	gdouble             v;

	if (self->applying || level == NULL)
		return;
	if (!settings_double (self->settings, pspec, &v))
		return;
	key = key_from_name (pspec->name);
	before = bake_state_capture (level);
	self->applying = TRUE;
	lrg_level_set_bake_setting (level, key, v);
	self->applying = FALSE;
	after = bake_state_capture (level);
	label = g_strdup_printf ("Bake Setting: %s", key);
	commit (self, level, before, after, label, self->in_gesture);
	sync_widgets (self);
}

LrgLightmapBakeSettings *
lrg_editor_bake_panel_get_settings (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), NULL);
	return self->settings;
}

LrgPropertyGrid *
lrg_editor_bake_panel_get_grid (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), NULL);
	return self->grid;
}

gboolean
lrg_editor_bake_panel_set_setting (LrgEditorBakePanel  *self,
                                   const gchar         *key,
                                   gdouble              value,
                                   GError             **error)
{
	g_autofree gchar *name = NULL;
	GParamSpec       *pspec;
	g_auto(GValue)    v = G_VALUE_INIT;
	g_auto(GValue)    d = G_VALUE_INIT;

	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (current_level (self) == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No level is open");
		return FALSE;
	}
	name = g_strdelimit (g_strdup (key), "_", '-');
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->settings), name);
	if (pspec == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown bake setting '%s'", key);
		return FALSE;
	}

	/* through the object: clamped by the pspec, then on_settings_notify()
	 * commits it (a value equal to the current one still writes the key) */
	g_value_init (&v, pspec->value_type);
	g_value_init (&d, G_TYPE_DOUBLE);
	g_value_set_double (&d, value);
	if (pspec->value_type == G_TYPE_BOOLEAN)
		g_value_set_boolean (&v, value != 0.0);
	else if (pspec->value_type == G_TYPE_UINT)
		g_value_set_uint (&v, (guint) CLAMP (value, 0.0, (gdouble) G_MAXUINT));
	else if (!g_value_transform (&d, &v))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad value for '%s'", key);
		return FALSE;
	}
	g_param_value_validate (pspec, &v);
	g_object_set_property (G_OBJECT (self->settings), pspec->name, &v);
	on_settings_notify (G_OBJECT (self->settings), pspec, self);
	lrg_property_grid_refresh (self->grid);
	return TRUE;
}

gboolean
lrg_editor_bake_panel_reset_settings (LrgEditorBakePanel *self)
{
	LrgLevel            *level;
	g_autofree gchar    *before = NULL;
	g_autofree gchar    *after = NULL;
	g_autoptr(GPtrArray) refs = NULL;
	GPtrArray           *probes;
	const gchar         *lm;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);

	level = current_level (self);
	if (level == NULL)
		return FALSE;
	before = bake_state_capture (level);

	/* the block without its settings: references kept */
	refs = g_ptr_array_new_with_free_func (g_free);
	lm = lrg_level_get_lightmap_ref (level);
	probes = lrg_level_get_probe_refs (level);
	for (i = 0; i < probes->len; i++)
		g_ptr_array_add (refs, g_strdup (g_ptr_array_index (probes, i)));
	{
		g_autofree gchar *keep = (lm != NULL) ? g_strdup (lm) : NULL;

		self->applying = TRUE;
		lrg_level_clear_lighting (level);
		if (keep != NULL)
			lrg_level_set_lightmap_ref (level, keep);
		for (i = 0; i < refs->len; i++)
			lrg_level_add_probe_ref (level, g_ptr_array_index (refs, i));
		self->applying = FALSE;
	}
	after = bake_state_capture (level);
	commit (self, level, before, after, "Reset Bake Settings", FALSE);
	sync_settings (self);
	sync_widgets (self);
	return g_strcmp0 (before, after) != 0;
}

/* ==========================================================================
 * Bake
 * ========================================================================== */

/* The directory the level's relative references resolve against. */
static gchar *
level_dir (LrgEditorBakePanel *self)
{
	const gchar *path = (self->editor != NULL) ? lrg_editor_get_path (self->editor) : NULL;

	if (path != NULL)
		return g_path_get_dirname (path);
	if (self->app != NULL && editor_app_base_dir (self->app) != NULL)
		return g_strdup (editor_app_base_dir (self->app));
	return NULL;
}

/*
 * Gives mesh renderers that have no model the one the viewport draws (so
 * the baker unwraps exactly that geometry), or, without a window, the
 * resolved file path the baker can read headless.
 */
static void
attach_models (LrgEditorBakePanel *self,
               LrgWorld3D         *world)
{
	LrgEditorSceneRenderer *sr = scene_renderer (self);
	LrgWorld3DRenderer     *runtime = (sr != NULL) ? lrg_editor_scene_renderer_get_runtime_renderer (sr) : NULL;
	g_autoptr(GPtrArray)    comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_MESH_RENDERER_COMPONENT);
	guint                   i;

	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		LrgMeshRendererComponent *mr = g_ptr_array_index (comps, i);
		const gchar              *asset = lrg_mesh_renderer_component_get_mesh_asset (mr);

		if (asset == NULL || *asset == '\0' || lrg_mesh_renderer_component_get_model (mr) != NULL ||
		    !lrg_mesh_renderer_component_get_static_lighting (mr))
			continue;
		if (runtime == NULL)
			continue;
		if (IsWindowReady ())
		{
			GrlModel *model = lrg_world3d_renderer_get_model (runtime, asset);

			if (model != NULL)
				lrg_mesh_renderer_component_set_model (mr, model);
		}
		else
		{
			g_autofree gchar *abs = lrg_world3d_renderer_resolve_asset (runtime, asset);

			if (abs != NULL)
				lrg_mesh_renderer_component_set_mesh_asset (mr, abs);
		}
	}
}

/* One volume per visible probe-volume node under @node. */
static guint
add_probe_volumes (LrgLightmapBaker *baker,
                   LrgNode          *node,
                   gfloat            spacing)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	GPtrArray     *children = lrg_node_get_children (node);
	guint          added = 0;
	guint          i;

	if (!lrg_node_get_visible (node))
		return 0;
	if (visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PROBE_VOLUME)
	{
		gfloat world[16], mn[3], mx[3];
		guint  n[3];

		lrg_editor_node_get_world_matrix (node, world);
		if (lrg_node_visual_get_probe_volume (visual, world, spacing, mn, mx, n))
		{
			lrg_lightmap_baker_add_probe_volume (baker, mn, mx, n[0], n[1], n[2]);
			added++;
		}
	}
	for (i = 0; children != NULL && i < children->len; i++)
		added += add_probe_volumes (baker, g_ptr_array_index (children, i), spacing);
	return added;
}

/*
 * Collects the level into a fresh world and baker (see the file
 * comment).  On success self->world and self->baker are the new ones.
 */
static gboolean
bake_prepare (LrgEditorBakePanel  *self,
              GError             **error)
{
	LrgLevel                   *level = current_level (self);
	g_autoptr(LrgWorld3D)       world = NULL;
	g_autoptr(LrgLightmapBaker) baker = NULL;
	g_autofree gchar           *dir = NULL;
	gfloat                      spacing = 2.0f;

	if (level == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No level is open");
		return FALSE;
	}
	world = lrg_world3d_new ();
	dir = level_dir (self);
	if (!lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, error))
		return FALSE;
	attach_models (self, world);
	/* lights register as their entities enter; one step settles the rest */
	lrg_world3d_step (world, 0.0f);
	/* the last bake must not feed the new one */
	lrg_world3d_set_lightmap (world, NULL);
	lrg_world3d_clear_probe_grids (world);

	baker = lrg_lightmap_baker_new ();
	lrg_lightmap_baker_set_settings (baker, self->settings);
	lrg_lightmap_baker_add_world (baker, world);
	g_object_get (self->settings, "probe-spacing", &spacing, NULL);
	add_probe_volumes (baker, lrg_level_get_root (level), spacing);

	g_set_object (&self->world, world);
	g_set_object (&self->baker, baker);
	return TRUE;
}

static void
on_progress (LrgLightmapBaker     *baker,
             LrgLightmapBakePhase  phase,
             gdouble               fraction,
             gpointer              user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);

	if (baker != self->baker || self->state > STATE_PROBES)
		return;
	switch (phase)
	{
	case LRG_LIGHTMAP_BAKE_PHASE_PREPARE:     self->state = STATE_PREPARE; break;
	case LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS:   self->state = STATE_LIGHTMAPS; break;
	case LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS: self->state = STATE_POSTPROCESS; break;
	case LRG_LIGHTMAP_BAKE_PHASE_PROBES:      self->state = STATE_PROBES; break;
	case LRG_LIGHTMAP_BAKE_PHASE_DONE:
	default:
		break;
	}
	self->progress = CLAMP (fraction, 0.0, 1.0);
	sync_widgets (self);
}

/* Takes the finished bake's outputs as the panel's result. */
static void
take_result (LrgEditorBakePanel *self)
{
	GPtrArray *grids = lrg_lightmap_baker_get_probe_grids (self->baker);
	guint      i;

	g_set_object (&self->lightmap, lrg_lightmap_baker_get_lightmap (self->baker));
	g_ptr_array_set_size (self->grids, 0);
	for (i = 0; grids != NULL && i < grids->len; i++)
		g_ptr_array_add (self->grids, g_object_ref (g_ptr_array_index (grids, i)));
	self->n_objects = lrg_lightmap_baker_get_n_objects (self->baker);
	self->n_lights = lrg_lightmap_baker_get_n_lights (self->baker);
	self->n_texels = lrg_lightmap_baker_get_n_texels (self->baker);
	self->bake_time = lrg_lightmap_baker_get_bake_time (self->baker);
	self->unsaved = TRUE;
	self->page = 0;
	self->cells_valid = FALSE;
	if (scene_renderer (self) != NULL)
		lrg_editor_scene_renderer_set_baked_override (scene_renderer (self), self->lightmap, self->grids);
}

static void
on_baked (GObject      *source,
          GAsyncResult *res,
          gpointer      user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgLightmapBaker   *baker = LRG_LIGHTMAP_BAKER (source);
	g_autoptr(GError)   error = NULL;

	if (!lrg_lightmap_baker_bake_finish (baker, res, &error))
	{
		if (baker == self->baker)
		{
			gboolean cancelled = g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

			self->state = cancelled ? STATE_CANCELLED : STATE_FAILED;
			g_free (self->error);
			self->error = cancelled ? NULL : g_strdup (error->message);
			if (self->app != NULL)
			{
				if (cancelled)
					editor_app_set_status (self->app, "Lighting bake cancelled");
				else
					editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Lighting bake failed: %s", error->message);
			}
		}
		g_clear_object (&self->cancel);
		sync_widgets (self);
		g_object_unref (self);
		return;
	}
	if (baker != self->baker)
	{
		g_object_unref (self);
		return;
	}

	self->state = STATE_DONE;
	self->progress = 1.0;
	g_clear_object (&self->cancel);
	take_result (self);

	/* Saved levels keep the result next to them at once */
	if (self->auto_save && self->editor != NULL && lrg_editor_get_path (self->editor) != NULL)
	{
		g_autoptr(GError) save_error = NULL;

		if (!lrg_editor_bake_panel_save (self, NULL, &save_error) && self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Bake not saved: %s", save_error->message);
	}
	if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_SUCCESS,
		                   "Lighting baked in %.1f s: %u objects, %u lights, %u texels, %u probe grid%s%s",
		                   self->bake_time, self->n_objects, self->n_lights, self->n_texels, self->grids->len,
		                   self->grids->len == 1 ? "" : "s", self->unsaved ? " (not saved yet)" : "");
	sync_widgets (self);
	g_object_unref (self);
}

gboolean
lrg_editor_bake_panel_bake (LrgEditorBakePanel  *self,
                            GError             **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (lrg_editor_bake_panel_get_baking (self))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY, "A bake is already running");
		return FALSE;
	}
	if (!bake_prepare (self, error))
		return FALSE;
	if (lrg_lightmap_baker_get_n_objects (self->baker) == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Nothing to bake: mark meshes or primitives with static_lighting "
		                     "(Inspector > Lighting > Static lighting)");
		g_clear_object (&self->baker);
		g_clear_object (&self->world);
		return FALSE;
	}

	g_clear_pointer (&self->error, g_free);
	self->state = STATE_PREPARE;
	self->progress = 0.0;
	self->started = g_get_monotonic_time ();
	g_clear_object (&self->cancel);
	self->cancel = g_cancellable_new ();
	g_signal_connect_object (self->baker, "progress", G_CALLBACK (on_progress), self, 0);
	lrg_lightmap_baker_bake_async (self->baker, self->cancel, on_baked, g_object_ref (self));
	if (self->app != NULL)
		editor_app_set_status (self->app, "Baking lighting: %u objects, %u lights",
		                       lrg_lightmap_baker_get_n_objects (self->baker),
		                       lrg_lightmap_baker_get_n_lights (self->baker));
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_bake_panel_cancel (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);

	if (!lrg_editor_bake_panel_get_baking (self) || self->cancel == NULL)
		return FALSE;
	g_cancellable_cancel (self->cancel);
	return TRUE;
}

gboolean
lrg_editor_bake_panel_get_baking (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	return self->state >= STATE_PREPARE && self->state <= STATE_PROBES;
}

gdouble
lrg_editor_bake_panel_get_progress (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), 0.0);
	return self->progress;
}

const gchar *
lrg_editor_bake_panel_get_phase (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), "idle");
	return state_names[self->state];
}

const gchar *
lrg_editor_bake_panel_get_error (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), NULL);
	return self->error;
}

LrgLightmap3D *
lrg_editor_bake_panel_get_lightmap (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), NULL);
	return self->lightmap;
}

GPtrArray *
lrg_editor_bake_panel_get_probe_grids (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), NULL);
	return self->grids;
}

gboolean
lrg_editor_bake_panel_has_unsaved (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	return self->unsaved && (self->lightmap != NULL || self->grids->len > 0);
}

void
lrg_editor_bake_panel_set_auto_save (LrgEditorBakePanel *self,
                                     gboolean            auto_save)
{
	g_return_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self));
	self->auto_save = !!auto_save;
}

gboolean
lrg_editor_bake_panel_get_auto_save (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	return self->auto_save;
}

/* ==========================================================================
 * Save / clear
 * ========================================================================== */

/* @path as the level stores it: relative when inside @ldir. */
static gchar *
level_ref (const gchar *ldir,
           const gchar *path)
{
	if (ldir != NULL)
	{
		g_autofree gchar *prefix = g_strconcat (ldir, G_DIR_SEPARATOR_S, NULL);

		if (g_str_has_prefix (path, prefix))
			return g_strdup (path + strlen (prefix));
	}
	return g_strdup (path);
}

gboolean
lrg_editor_bake_panel_save (LrgEditorBakePanel  *self,
                            const gchar         *dir,
                            GError             **error)
{
	LrgLevel         *level;
	const gchar      *level_path;
	g_autofree gchar *ldir = NULL;
	g_autofree gchar *out_dir = NULL;
	g_autofree gchar *stem = NULL;
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	level = current_level (self);
	if (level == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No level is open");
		return FALSE;
	}
	if (self->lightmap == NULL && self->grids->len == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Nothing baked yet");
		return FALSE;
	}
	level_path = lrg_editor_get_path (self->editor);
	ldir = (level_path != NULL) ? g_path_get_dirname (level_path) : NULL;
	out_dir = g_strdup (dir != NULL ? dir : ldir);
	if (out_dir == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Save the level first: the bake is written next to it");
		return FALSE;
	}
	if (g_mkdir_with_parents (out_dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create %s", out_dir);
		return FALSE;
	}

	/* <level>.rlightmap and <level>-<n>.rprobes */
	if (level_path != NULL)
	{
		g_autofree gchar *base = g_path_get_basename (level_path);
		gchar            *dot = strrchr (base, '.');

		if (dot != NULL && dot != base)
			*dot = '\0';
		stem = g_strdup (base);
	}
	else
		stem = g_strdup ("level");

	before = bake_state_capture (level);
	self->applying = TRUE;
	{
		/* the settings stay, the references are replaced */
		g_autofree gchar *keep = bake_state_capture (level);
		g_auto(GStrv)     lines = g_strsplit (keep, "\n", -1);
		GString          *settings_only = g_string_new (NULL);
		guint             k;

		for (k = 0; lines[k] != NULL; k++)
			if (g_str_has_prefix (lines[k], "S\t"))
				g_string_append_printf (settings_only, "%s\n", lines[k]);
		bake_state_apply (level, settings_only->str);
		g_string_free (settings_only, TRUE);
	}
	if (self->lightmap != NULL && lrg_lightmap3d_get_n_entries (self->lightmap) > 0)
	{
		g_autofree gchar *name = g_strdup_printf ("%s.rlightmap", stem);
		g_autofree gchar *path = g_build_filename (out_dir, name, NULL);
		g_autofree gchar *ref = NULL;

		if (!lrg_lightmap3d_save (self->lightmap, path, error))
		{
			bake_state_apply (level, before);
			self->applying = FALSE;
			return FALSE;
		}
		ref = level_ref (ldir, path);
		lrg_level_set_lightmap_ref (level, ref);
	}
	for (i = 0; i < self->grids->len; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("%s-%u.rprobes", stem, i);
		g_autofree gchar *path = g_build_filename (out_dir, name, NULL);
		g_autofree gchar *ref = NULL;

		if (!lrg_probe_grid_save (g_ptr_array_index (self->grids, i), path, error))
		{
			bake_state_apply (level, before);
			self->applying = FALSE;
			return FALSE;
		}
		ref = level_ref (ldir, path);
		lrg_level_add_probe_ref (level, ref);
	}
	self->applying = FALSE;
	after = bake_state_capture (level);
	commit (self, level, before, after, "Bake Lighting", FALSE);

	/* the viewport reads the files from now on (same data, and undo
	 * then really takes the bake away) */
	self->unsaved = FALSE;
	if (scene_renderer (self) != NULL)
		lrg_editor_scene_renderer_set_baked_override (scene_renderer (self), NULL, NULL);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Saved the bake as %s.rlightmap + %u probe grid%s", stem,
		                       self->grids->len, self->grids->len == 1 ? "" : "s");
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_bake_panel_clear (LrgEditorBakePanel *self)
{
	LrgLevel            *level;
	g_autofree gchar    *before = NULL;
	g_autofree gchar    *after = NULL;
	gboolean             had_result;

	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);

	level = current_level (self);
	had_result = lrg_editor_bake_panel_has_unsaved (self);
	g_clear_object (&self->lightmap);
	g_ptr_array_set_size (self->grids, 0);
	self->unsaved = FALSE;
	self->cells_valid = FALSE;
	if (scene_renderer (self) != NULL)
		lrg_editor_scene_renderer_set_baked_override (scene_renderer (self), NULL, NULL);
	if (level == NULL)
	{
		sync_widgets (self);
		return had_result;
	}

	before = bake_state_capture (level);
	self->applying = TRUE;
	lrg_level_set_lightmap_ref (level, NULL);
	while (lrg_level_get_probe_refs (level)->len > 0)
	{
		g_autofree gchar *ref = g_strdup (g_ptr_array_index (lrg_level_get_probe_refs (level), 0));

		if (!lrg_level_remove_probe_ref (level, ref))
			break;
	}
	self->applying = FALSE;
	after = bake_state_capture (level);
	commit (self, level, before, after, "Clear Baked Lighting", FALSE);
	sync_widgets (self);
	return had_result || g_strcmp0 (before, after) != 0;
}

/* ==========================================================================
 * Light at a point
 * ========================================================================== */

gboolean
lrg_editor_bake_panel_evaluate (LrgEditorBakePanel *self,
                                const gfloat       *position,
                                const gfloat       *normal,
                                gfloat             *out_rgb)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	g_return_val_if_fail (position != NULL && normal != NULL && out_rgb != NULL, FALSE);

	if (lrg_editor_bake_panel_get_baking (self))
		return FALSE;
	/* without a bake this session, collect the scene now (not baked) */
	if (self->baker == NULL && !bake_prepare (self, NULL))
		return FALSE;
	return lrg_lightmap_baker_evaluate (self->baker, position, normal, out_rgb);
}

/*
 * The normal of @node's bounds face the ray (@o, @d) enters at distance
 * @t: the local box face nearest the hit point, through the world matrix.
 */
static void
hit_normal (LrgNode      *node,
            const gfloat *o,
            const gfloat *d,
            gfloat        t,
            gfloat       *out)
{
	gfloat world[16], inv[16], p[3], lp[3], mn[3], mx[3], ln[3] = { 0.0f, 0.0f, 0.0f };
	gfloat best = G_MAXFLOAT, len;
	gint   k;

	p[0] = o[0] + d[0] * t;
	p[1] = o[1] + d[1] * t;
	p[2] = o[2] + d[2] * t;
	out[0] = -d[0];
	out[1] = -d[1];
	out[2] = -d[2];
	lrg_editor_node_get_world_matrix (node, world);
	if (!lrg_editor_node_get_local_bounds (node, mn, mx) || !lrg_editor_math_invert (world, inv))
		return;
	lrg_editor_math_transform_point (inv, p, lp);
	for (k = 0; k < 3; k++)
	{
		gfloat dl = fabsf (lp[k] - mn[k]);
		gfloat dh = fabsf (lp[k] - mx[k]);

		if (dl < best)
		{
			best = dl;
			memset (ln, 0, sizeof ln);
			ln[k] = -1.0f;
		}
		if (dh < best)
		{
			best = dh;
			memset (ln, 0, sizeof ln);
			ln[k] = 1.0f;
		}
	}
	/* normals go through the inverse transpose */
	out[0] = inv[0] * ln[0] + inv[1] * ln[1] + inv[2] * ln[2];
	out[1] = inv[4] * ln[0] + inv[5] * ln[1] + inv[6] * ln[2];
	out[2] = inv[8] * ln[0] + inv[9] * ln[1] + inv[10] * ln[2];
	len = sqrtf (out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (len > 1e-6f)
	{
		out[0] /= len;
		out[1] /= len;
		out[2] /= len;
	}
}

gboolean
lrg_editor_bake_panel_sample_cursor (LrgEditorBakePanel *self,
                                     gfloat             *out_position,
                                     gfloat             *out_normal,
                                     gfloat             *out_rgb)
{
	LrgEditorView  view;
	LrgLevel      *level;
	LrgNode       *hit;
	gfloat         o[3], d[3], t = 0.0f, p[3];
	gint           k;

	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), FALSE);
	g_return_val_if_fail (out_position != NULL && out_normal != NULL && out_rgb != NULL, FALSE);

	level = current_level (self);
	if (level == NULL || self->app == NULL || self->app->view == NULL)
		return FALSE;
	lrg_editor_view_state_get_view (self->app->view, -1, &view);
	if (view.width < 1.0f || view.height < 1.0f)
	{
		/* a viewport that was never laid out: any size, the centre ray
		 * is the same */
		view.width = 640.0f;
		view.height = 480.0f;
	}
	lrg_editor_view_get_ray (&view, view.width * 0.5f, view.height * 0.5f, o, d);
	hit = lrg_editor_pick (level, o, d, FALSE, &t);
	if (hit == NULL)
		return FALSE;
	hit_normal (hit, o, d, t, out_normal);
	/* a hair above the surface, like a texel centre */
	for (k = 0; k < 3; k++)
	{
		p[k] = o[k] + d[k] * t + out_normal[k] * 1e-3f;
		out_position[k] = p[k];
	}
	if (!lrg_editor_bake_panel_evaluate (self, p, out_normal, out_rgb))
		return FALSE;
	memcpy (self->cursor_pos, p, sizeof self->cursor_pos);
	memcpy (self->cursor_rgb, out_rgb, sizeof self->cursor_rgb);
	self->cursor_valid = TRUE;
	return TRUE;
}

/* ==========================================================================
 * Atlas pages
 * ========================================================================== */

/* The lightmap the preview shows: the last bake's, else the viewport's. */
static LrgLightmap3D *
shown_lightmap (LrgEditorBakePanel *self)
{
	if (self->lightmap != NULL)
		return self->lightmap;
	if (scene_renderer (self) != NULL)
		return lrg_editor_scene_renderer_get_baked_lightmap (scene_renderer (self));
	return NULL;
}

void
lrg_editor_bake_panel_set_page (LrgEditorBakePanel *self,
                                guint               page)
{
	LrgLightmap3D *lm;
	guint          n;

	g_return_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self));

	lm = shown_lightmap (self);
	n = (lm != NULL) ? lrg_lightmap3d_get_n_pages (lm) : 0;
	self->page = (n > 0) ? MIN (page, n - 1) : 0;
	self->cells_valid = FALSE;
	sync_widgets (self);
}

guint
lrg_editor_bake_panel_get_page (LrgEditorBakePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BAKE_PANEL (self), 0);
	return self->page;
}

/* Linear irradiance / pi -> display byte (soft tone map, sRGB). */
static guint8
tone_byte (gfloat v)
{
	v = MAX (v, 0.0f) * 1.5f;
	v = v / (1.0f + v);
	return (guint8) CLAMP (powf (v, 1.0f / 2.2f) * 255.0f + 0.5f, 0.0f, 255.0f);
}

/* The shown page as coarse cells (headless) and, with a window, a texture. */
static void
update_atlas (LrgEditorBakePanel *self)
{
	LrgLightmap3D *lm = shown_lightmap (self);
	LrgHdrImage   *img = (lm != NULL) ? lrg_lightmap3d_get_page (lm, self->page) : NULL;
	guint          pw, ph, w, h, x, y, ox = 0, oy = 0;

	if (self->cells_valid && self->atlas_for == (gpointer) img && self->atlas_page == self->page)
		return;
	self->cells_valid = TRUE;
	self->atlas_for = img;
	self->atlas_page = self->page;
	g_clear_object (&self->atlas_tex);
	memset (self->cells, 0, sizeof self->cells);
	if (img == NULL)
		return;
	pw = lrg_hdr_image_get_width (img);
	ph = lrg_hdr_image_get_height (img);
	if (pw == 0 || ph == 0)
		return;

	/* The used part of the page (charts fill it from one corner): a
	 * square around every texel with light, so the charts are readable
	 * instead of a dot in a black page */
	{
		gsize         nf = 0;
		const gfloat *d = lrg_hdr_image_get_data (img, &nf);
		guint         x0 = pw, y0 = ph, x1 = 0, y1 = 0, edge;

		for (y = 0; d != NULL && y < ph; y++)
			for (x = 0; x < pw; x++)
			{
				const gfloat *t = d + ((gsize) y * pw + x) * 3u;

				if (t[0] + t[1] + t[2] > 1e-6f)
				{
					x0 = MIN (x0, x);
					y0 = MIN (y0, y);
					x1 = MAX (x1, x + 1);
					y1 = MAX (y1, y + 1);
				}
			}
		if (x1 <= x0 || y1 <= y0)
		{
			x0 = y0 = 0;
			x1 = pw;
			y1 = ph;
		}
		/* a little margin, and at least a quarter of the page per side so
		 * a single tiny chart is not blown up */
		edge = MAX (MAX (x1 - x0, y1 - y0) / 16u, 2u);
		x0 = (x0 > edge) ? x0 - edge : 0;
		y0 = (y0 > edge) ? y0 - edge : 0;
		x1 = MIN (x1 + edge, pw);
		y1 = MIN (y1 + edge, ph);
		w = MAX (x1 - x0, MIN (pw / 4u, pw - x0));
		h = MAX (y1 - y0, MIN (ph / 4u, ph - y0));
		ox = x0;
		oy = y0;
	}
	self->atlas_w = w;
	self->atlas_h = h;

	for (y = 0; y < ATLAS_CELLS; y++)
	{
		for (x = 0; x < ATLAS_CELLS; x++)
		{
			gfloat rgb[3];
			guint8 *c = &self->cells[(y * ATLAS_CELLS + x) * 3];

			lrg_hdr_image_get_pixel (img, ox + (x * w + w / 2) / ATLAS_CELLS, oy + (y * h + h / 2) / ATLAS_CELLS, rgb);
			c[0] = tone_byte (rgb[0]);
			c[1] = tone_byte (rgb[1]);
			c[2] = tone_byte (rgb[2]);
		}
	}

	if (IsWindowReady ())
	{
		guint                tw = (w >= h) ? MIN (w, ATLAS_MAX_EDGE) : MAX (MIN (w, ATLAS_MAX_EDGE) * w / MAX (h, 1u), 1u);
		guint                th = (h >= w) ? MIN (h, ATLAS_MAX_EDGE) : MAX (MIN (h, ATLAS_MAX_EDGE) * h / MAX (w, 1u), 1u);
		g_autofree guint8   *px = g_new (guint8, (gsize) tw * th * 4u);
		g_autoptr(GrlImage)  gi = NULL;

		/* nearest texel per preview pixel: charts keep hard edges */
		for (y = 0; y < th; y++)
		{
			for (x = 0; x < tw; x++)
			{
				gfloat  rgb[3];
				guint8 *p = &px[((gsize) y * tw + x) * 4u];

				lrg_hdr_image_get_pixel (img, ox + x * w / tw, oy + y * h / th, rgb);
				p[0] = tone_byte (rgb[0]);
				p[1] = tone_byte (rgb[1]);
				p[2] = tone_byte (rgb[2]);
				p[3] = 255;
			}
		}
		gi = grl_image_new_from_pixels ((gint) tw, (gint) th, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, px);
		if (gi != NULL)
			self->atlas_tex = grl_texture_new_from_image (gi);
	}
}

static void
atlas_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	size->width = 120.0f;
	size->height = ATLAS_HEIGHT;
}

/* The page, square and centred, on a checker-free dark well; a chip names
 * the page and its size. */
static void
atlas_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgTheme           *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgLightmap3D      *lm = shown_lightmap (self);
	LrgHdrImage        *img;
	gfloat              radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) + 2.0f;
	gfloat              aspect, ew, eh, ax;
	GrlColor            well = { 8, 8, 12, 255 };
	g_autofree gchar   *chip = NULL;

	update_atlas (self);
	img = (lm != NULL) ? lrg_lightmap3d_get_page (lm, self->page) : NULL;
	/* the shown region keeps its aspect, centred */
	aspect = (self->atlas_h > 0) ? (gfloat) self->atlas_w / (gfloat) self->atlas_h : 1.0f;
	ew = MIN (width, height * aspect);
	eh = ew / MAX (aspect, 1e-3f);
	ax = x + (width - ew) * 0.5f;
	y += (height - eh) * 0.5f;
	lrg_paint_list_shadow (list, ax, y, ew, eh, radius, 10.0f, 0.0f, 0.0f, 3.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
	lrg_paint_list_rect (list, ax, y, ew, eh, radius, &well);
	if (img != NULL && self->atlas_tex != NULL)
		lrg_paint_list_image (list, self->atlas_tex, NULL, ax, y, ew, eh, radius, NULL);
	else if (img != NULL)
	{
		gfloat cw = ew / (gfloat) ATLAS_CELLS;
		gfloat ch = eh / (gfloat) ATLAS_CELLS;
		guint  cx, cy;

		lrg_paint_list_push_clip (list, ax, y, ew, eh);
		for (cy = 0; cy < ATLAS_CELLS; cy++)
		{
			for (cx = 0; cx < ATLAS_CELLS; cx++)
			{
				const guint8 *p = &self->cells[(cy * ATLAS_CELLS + cx) * 3];
				GrlColor      c = { p[0], p[1], p[2], 255 };

				lrg_paint_list_rect (list, ax + (gfloat) cx * cw, y + (gfloat) cy * ch, cw + 1.0f, ch + 1.0f, 0.0f, &c);
			}
		}
		lrg_paint_list_pop_clip (list);
	}
	lrg_paint_list_border (list, ax, y, ew, eh, radius, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	if (img != NULL)
		chip = g_strdup_printf ("page %u / %u  %u x %u", self->page + 1, lrg_lightmap3d_get_n_pages (lm),
		                        lrg_hdr_image_get_width (img), lrg_hdr_image_get_height (img));
	else
		chip = g_strdup ("no lightmap");
	lrg_editor_gizmo_paint_chip (list, theme, chip, ax + 6.0f, y + 6.0f, FALSE, NULL);
}

/* ==========================================================================
 * Status pill, summary, cursor readout
 * ========================================================================== */

static gboolean
level_has_bake (LrgEditorBakePanel *self)
{
	LrgLevel *level = current_level (self);

	return level != NULL && (lrg_level_get_lightmap_ref (level) != NULL || lrg_level_get_probe_refs (level)->len > 0);
}

static const gchar *
status_text (LrgEditorBakePanel *self,
             LrgThemeColor      *role)
{
	if (lrg_editor_bake_panel_get_baking (self))
	{
		*role = LRG_THEME_COLOR_ACCENT;
		return "Baking";
	}
	if (self->state == STATE_FAILED)
	{
		*role = LRG_THEME_COLOR_DANGER;
		return "Bake failed";
	}
	if (lrg_editor_bake_panel_has_unsaved (self))
	{
		*role = LRG_THEME_COLOR_WARNING;
		return "Unsaved";
	}
	if (level_has_bake (self))
	{
		*role = LRG_THEME_COLOR_SUCCESS;
		return "Baked";
	}
	if (self->state == STATE_CANCELLED)
	{
		*role = LRG_THEME_COLOR_TEXT_MUTED;
		return "Cancelled";
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
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgTheme           *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgThemeColor       role = LRG_THEME_COLOR_TEXT_MUTED;
	const gchar        *text = status_text (self, &role);
	GrlColor            fill = *lrg_theme_get_color (theme, role);
	GrlColor            ink = fill;
	gfloat              fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat              tw = 0.0f, th = 0.0f;

	fill.a = 46;
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, fs, 0.0f, &tw, &th);
	width = MIN (width, tw + height + 14.0f);
	lrg_paint_list_rect (list, x, y, width, height, height * 0.5f, &fill);
	lrg_paint_list_circle (list, x + height * 0.5f + 2.0f, y + height * 0.5f, 3.0f, &ink);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, roundf (x + height * 0.5f + 9.0f),
	                     roundf (y + (height - th) * 0.5f), fs, 0.0f, &ink);
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

/* One "value / label" tile of the summary. */
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
	lrg_paint_list_text (list, font, value, roundf (x + 8.0f), roundf (y + 5.0f), fs + 2.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	lrg_paint_list_text (list, font, label, roundf (x + 8.0f), roundf (y + h - fss - 5.0f), fss, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

/* Objects / texels / probes / time tiles of the last bake (or of the
 * level's saved bake). */
static void
summary_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgTheme           *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgLightmap3D      *lm = shown_lightmap (self);
	GPtrArray          *grids = (self->grids->len > 0 || scene_renderer (self) == NULL) ? self->grids
	                            : lrg_editor_scene_renderer_get_baked_probe_grids (scene_renderer (self));
	gchar               v[4][32];
	const gchar        *labels[4] = { "objects", "texels", "probes", "seconds" };
	guint               probes = 0;
	guint               i;
	gfloat              gap = 6.0f;
	gfloat              tw = (width - gap * 3.0f) / 4.0f;

	for (i = 0; grids != NULL && i < grids->len; i++)
		probes += lrg_probe_grid_get_n_probes (g_ptr_array_index (grids, i));
	if (self->baker != NULL && self->state == STATE_DONE)
	{
		g_snprintf (v[0], sizeof v[0], "%u", self->n_objects);
		g_snprintf (v[1], sizeof v[1], self->n_texels >= 10000 ? "%.0fk" : "%.0f",
		            self->n_texels >= 10000 ? (gdouble) self->n_texels / 1000.0 : (gdouble) self->n_texels);
		g_snprintf (v[3], sizeof v[3], "%.1f", self->bake_time);
	}
	else
	{
		g_snprintf (v[0], sizeof v[0], "%u", lm != NULL ? lrg_lightmap3d_get_n_entries (lm) : 0u);
		g_snprintf (v[1], sizeof v[1], "%s", "-");
		g_snprintf (v[3], sizeof v[3], "%s", lrg_editor_bake_panel_get_baking (self) ? "..." : "-");
	}
	g_snprintf (v[2], sizeof v[2], "%u", probes);
	if (lrg_editor_bake_panel_get_baking (self))
		g_snprintf (v[3], sizeof v[3], "%.1f",
		            (gdouble) (g_get_monotonic_time () - self->started) / (gdouble) G_USEC_PER_SEC);

	if (width < 220.0f)
	{
		/* narrow docks: two rows of two */
		gfloat hh = (height - gap) * 0.5f;

		tw = (width - gap) * 0.5f;
		for (i = 0; i < 4; i++)
			summary_tile (list, theme, x + (gfloat) (i % 2) * (tw + gap), y + (gfloat) (i / 2) * (hh + gap), tw, hh,
			              v[i], labels[i]);
		return;
	}
	for (i = 0; i < 4; i++)
		summary_tile (list, theme, x + (gfloat) i * (tw + gap), y, tw, height, v[i], labels[i]);
}

static void
cursor_measure (LrgCustomWidget     *widget,
                gfloat               avail_width,
                gfloat               avail_height,
                LrgCustomWidgetSize *size,
                gpointer             user_data)
{
	size->width = 120.0f;
	size->height = CURSOR_HEIGHT;
}

/* The swatch and numbers of the last "light at cursor" sample. */
static void
cursor_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgTheme           *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont            *font = lrg_theme_get_ui_font (theme);
	gfloat              fss = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat              r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	g_autofree gchar   *text = NULL;
	GrlColor            sw = { 0, 0, 0, 255 };

	if (!self->cursor_valid)
	{
		lrg_paint_list_text (list, font, "Light at cursor: aim the viewport centre at a surface and press Sample",
		                     roundf (x), roundf (y + (height - fss) * 0.5f), fss, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;
	}
	sw.r = tone_byte (self->cursor_rgb[0]);
	sw.g = tone_byte (self->cursor_rgb[1]);
	sw.b = tone_byte (self->cursor_rgb[2]);
	lrg_paint_list_rect (list, x, y + 2.0f, height - 4.0f, height - 4.0f, r, &sw);
	lrg_paint_list_border (list, x, y + 2.0f, height - 4.0f, height - 4.0f, r, 1.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	text = g_strdup_printf ("%.3f %.3f %.3f  at (%.2f, %.2f, %.2f)", (gdouble) self->cursor_rgb[0],
	                        (gdouble) self->cursor_rgb[1], (gdouble) self->cursor_rgb[2],
	                        (gdouble) self->cursor_pos[0], (gdouble) self->cursor_pos[1],
	                        (gdouble) self->cursor_pos[2]);
	lrg_paint_list_text (list, font, text, roundf (x + height + 4.0f), roundf (y + (height - fss) * 0.5f), fss, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
}

/* ==========================================================================
 * Widgets
 * ========================================================================== */

static void
sync_widgets (LrgEditorBakePanel *self)
{
	LrgLevel      *level = current_level (self);
	LrgLightmap3D *lm = shown_lightmap (self);
	gboolean       baking = lrg_editor_bake_panel_get_baking (self);
	gboolean       has = level != NULL;
	guint          pages = (lm != NULL) ? lrg_lightmap3d_get_n_pages (lm) : 0;

	if (level != self->level)
	{
		g_set_weak_pointer (&self->level, level);
		sync_settings (self);
	}
	if (self->bake_chip == NULL)
		return;
	editor_chip_set_label (self->bake_chip, baking ? "Cancel" : "Bake");
	editor_chip_set_icon (self->bake_chip, baking ? LRG_ICON_STOP : LRG_ICON_REFRESH);
	editor_chip_set_tint (self->bake_chip, baking ? LRG_THEME_COLOR_DANGER : LRG_THEME_COLOR_ACCENT);
	lrg_widget_set_enabled (self->bake_chip, has);
	lrg_widget_set_enabled (self->save_chip, !baking && lrg_editor_bake_panel_has_unsaved (self));
	lrg_widget_set_enabled (self->clear_chip, !baking && (level_has_bake (self) || lrg_editor_bake_panel_has_unsaved (self)));
	if (scene_renderer (self) != NULL)
		editor_chip_set_checked (self->preview_chip, lrg_editor_scene_renderer_get_show_baked (scene_renderer (self)));

	lrg_widget_set_visible (LRG_WIDGET (self->bar), baking);
	lrg_widget_set_visible (self->phase_label, baking || self->state == STATE_FAILED);
	lrg_progress_bar_set_value (self->bar, self->progress);
	if (self->state == STATE_FAILED && self->error != NULL)
	{
		g_autofree gchar *t = g_strdup_printf ("Failed: %s", self->error);

		lrg_label_set_text (LRG_LABEL (self->phase_label), t);
	}
	else
	{
		g_autofree gchar *t = g_strdup_printf ("%s  %.0f%%", phase_labels[self->state], self->progress * 100.0);

		lrg_label_set_text (LRG_LABEL (self->phase_label), t);
	}
	lrg_widget_set_visible (self->atlas_row, pages > 0);
	lrg_widget_set_visible (self->scroll, has);
	lrg_widget_set_visible (self->empty, !has);
	if (!self->applying)
		lrg_property_grid_refresh (self->grid);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_editor_changed (LrgEditorBakePanel *self)
{
	if (self->applying)
		return;
	/* undo / redo of a settings step: follow the level */
	sync_settings (self);
	sync_widgets (self);
}

static void
on_level_replaced (LrgEditorBakePanel *self)
{
	g_clear_object (&self->gesture_proxy);
	if (self->cancel != NULL)
		g_cancellable_cancel (self->cancel);
	g_clear_object (&self->baker);
	g_clear_object (&self->world);
	g_clear_object (&self->lightmap);
	g_ptr_array_set_size (self->grids, 0);
	self->unsaved = FALSE;
	self->state = STATE_IDLE;
	self->progress = 0.0;
	self->cursor_valid = FALSE;
	self->cells_valid = FALSE;
	self->page = 0;
	if (scene_renderer (self) != NULL)
		lrg_editor_scene_renderer_set_baked_override (scene_renderer (self), NULL, NULL);
	sync_settings (self);
	sync_widgets (self);
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);

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
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);

	if (gesture_id == self->gesture_id)
	{
		self->in_gesture = FALSE;
		g_clear_object (&self->gesture_proxy);
	}
}

static void
on_bake_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	g_autoptr(GError)   error = NULL;

	if (lrg_editor_bake_panel_get_baking (self))
	{
		lrg_editor_bake_panel_cancel (self);
		return;
	}
	if (!lrg_editor_bake_panel_bake (self, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
}

static void
on_save_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	g_autoptr(GError)   error = NULL;

	if (!lrg_editor_bake_panel_save (self, NULL, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "%s", error->message);
}

static void
on_clear_chip (LrgWidget *chip,
               gpointer   user_data)
{
	lrg_editor_bake_panel_clear (LRG_EDITOR_BAKE_PANEL (user_data));
}

static void
on_preview_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorBakePanel     *self = LRG_EDITOR_BAKE_PANEL (user_data);
	LrgEditorSceneRenderer *sr = scene_renderer (self);

	if (sr != NULL)
		lrg_editor_scene_renderer_set_show_baked (sr, !lrg_editor_scene_renderer_get_show_baked (sr));
	sync_widgets (self);
}

static void
on_sample_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);
	gfloat              p[3], n[3], rgb[3];

	if (!lrg_editor_bake_panel_sample_cursor (self, p, n, rgb) && self->app != NULL)
		editor_app_set_status (self->app, "Light at cursor: nothing under the viewport centre");
	sync_widgets (self);
}

static void
on_page_prev (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);

	if (self->page > 0)
		lrg_editor_bake_panel_set_page (self, self->page - 1);
}

static void
on_page_next (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (user_data);

	lrg_editor_bake_panel_set_page (self, self->page + 1);
}

/* ==========================================================================
 * IPC: bake.*
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

static gboolean
read_vec3 (JsonObject  *params,
           const gchar *name,
           gfloat      *out)
{
	JsonNode  *n = (params != NULL) ? json_object_get_member (params, name) : NULL;
	JsonArray *a;
	guint      k;

	if (n == NULL || !JSON_NODE_HOLDS_ARRAY (n))
		return FALSE;
	a = json_node_get_array (n);
	if (json_array_get_length (a) < 3)
		return FALSE;
	for (k = 0; k < 3; k++)
		out[k] = (gfloat) json_array_get_double_element (a, k);
	return TRUE;
}

static JsonNode *
progress_json (LrgEditorBakePanel *self)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "baking", lrg_editor_bake_panel_get_baking (self));
	json_object_set_string_member (o, "phase", lrg_editor_bake_panel_get_phase (self));
	json_object_set_double_member (o, "progress", self->progress);
	if (self->error != NULL)
		json_object_set_string_member (o, "error", self->error);
	else
		json_object_set_null_member (o, "error");
	json_object_set_boolean_member (o, "unsaved", lrg_editor_bake_panel_has_unsaved (self));
	return obj_node (o);
}

/* bake.get: settings, references, state and the last result. */
static JsonNode *
state_json (LrgEditorBakePanel *self)
{
	LrgLevel    *level = current_level (self);
	JsonObject  *o = json_object_new ();
	JsonObject  *settings = json_object_new ();
	JsonArray   *set_keys = json_array_new ();
	JsonArray   *probes = json_array_new ();
	JsonObject  *result = json_object_new ();
	GParamSpec **props;
	guint        n = 0;
	guint        i;

	props = g_object_class_list_properties (G_OBJECT_GET_CLASS (self->settings), &n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *key = key_from_name (props[i]->name);
		gdouble           v = 0.0;

		if (props[i]->value_type == G_TYPE_BOOLEAN)
		{
			gboolean b = FALSE;

			g_object_get (self->settings, props[i]->name, &b, NULL);
			json_object_set_boolean_member (settings, key, b);
		}
		else if (settings_double (self->settings, props[i], &v))
			json_object_set_double_member (settings, key, v);
		if (level != NULL && lrg_level_get_bake_setting (level, key, NULL))
			json_array_add_string_element (set_keys, key);
	}
	g_free (props);
	json_object_set_object_member (o, "settings", settings);
	json_object_set_array_member (o, "set_keys", set_keys);

	if (level != NULL && lrg_level_get_lightmap_ref (level) != NULL)
		json_object_set_string_member (o, "lightmap", lrg_level_get_lightmap_ref (level));
	else
		json_object_set_null_member (o, "lightmap");
	for (i = 0; level != NULL && i < lrg_level_get_probe_refs (level)->len; i++)
		json_array_add_string_element (probes, g_ptr_array_index (lrg_level_get_probe_refs (level), i));
	json_object_set_array_member (o, "probes", probes);

	json_object_set_boolean_member (o, "baking", lrg_editor_bake_panel_get_baking (self));
	json_object_set_string_member (o, "phase", lrg_editor_bake_panel_get_phase (self));
	json_object_set_double_member (o, "progress", self->progress);
	json_object_set_boolean_member (o, "auto_save", self->auto_save);
	json_object_set_boolean_member (o, "preview",
	                                scene_renderer (self) != NULL &&
	                                lrg_editor_scene_renderer_get_show_baked (scene_renderer (self)));

	json_object_set_boolean_member (result, "present", self->lightmap != NULL || self->grids->len > 0);
	json_object_set_boolean_member (result, "unsaved", lrg_editor_bake_panel_has_unsaved (self));
	json_object_set_int_member (result, "objects", self->n_objects);
	json_object_set_int_member (result, "lights", self->n_lights);
	json_object_set_int_member (result, "texels", self->n_texels);
	json_object_set_int_member (result, "entries", self->lightmap != NULL ? lrg_lightmap3d_get_n_entries (self->lightmap) : 0);
	json_object_set_int_member (result, "pages", self->lightmap != NULL ? lrg_lightmap3d_get_n_pages (self->lightmap) : 0);
	json_object_set_int_member (result, "grids", self->grids->len);
	{
		guint np = 0;

		for (i = 0; i < self->grids->len; i++)
			np += lrg_probe_grid_get_n_probes (g_ptr_array_index (self->grids, i));
		json_object_set_int_member (result, "probes", np);
	}
	json_object_set_double_member (result, "seconds", self->bake_time);
	json_object_set_object_member (o, "result", result);
	return obj_node (o);
}

static LrgEditorBakePanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorBakePanel *self = lrg_editor_bake_panel_find (LRG_EDITOR_APP (user_data));

	if (self == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No bake panel");
	return self;
}

static JsonNode *
m_bake_get (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);

	return (self != NULL) ? state_json (self) : NULL;
}

/* A JSON value as a setting double (booleans 0 / 1). */
static gboolean
json_double (JsonNode *n,
             gdouble  *out)
{
	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_BOOLEAN:
		*out = json_node_get_boolean (n) ? 1.0 : 0.0;
		return TRUE;
	case G_TYPE_INT64:
		*out = (gdouble) json_node_get_int (n);
		return TRUE;
	case G_TYPE_DOUBLE:
		*out = json_node_get_double (n);
		return TRUE;
	default:
		return FALSE;
	}
}

/* {key, value} | {values: {key: value}} | {reset: true} | {auto_save} */
static JsonNode *
m_bake_set (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	g_autoptr(GError)   local = NULL;
	guint               n = 0;

	if (self == NULL)
		return NULL;
	if (current_level (self) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level is open");
		return NULL;
	}
	if (params != NULL && json_object_get_boolean_member_with_default (params, "reset", FALSE))
	{
		lrg_editor_bake_panel_reset_settings (self);
		n++;
	}
	if (params != NULL && json_object_has_member (params, "auto_save"))
	{
		self->auto_save = json_object_get_boolean_member_with_default (params, "auto_save", TRUE);
		n++;
	}
	if (params != NULL && json_object_has_member (params, "key"))
	{
		gdouble v = 0.0;

		if (!json_double (json_object_get_member (params, "value"), &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "\"value\" must be a number or a boolean");
			return NULL;
		}
		if (!lrg_editor_bake_panel_set_setting (self, json_object_get_string_member (params, "key"), v, &local))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
			return NULL;
		}
		n++;
	}
	if (params != NULL && json_object_has_member (params, "values") &&
	    JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		JsonObject *values = json_object_get_object_member (params, "values");
		GList      *keys = json_object_get_members (values);
		GList      *l;

		/* one undo step for the batch */
		lrg_editor_begin_group (self->editor, "Edit Bake Settings");
		for (l = keys; l != NULL; l = l->next)
		{
			gdouble v = 0.0;

			if (!json_double (json_object_get_member (values, l->data), &v) ||
			    !lrg_editor_bake_panel_set_setting (self, l->data, v, &local))
			{
				lrg_editor_end_group (self->editor);
				g_list_free (keys);
				g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
				             "%s", local != NULL ? local->message : "bad value");
				return NULL;
			}
			n++;
		}
		lrg_editor_end_group (self->editor);
		g_list_free (keys);
	}
	if (n == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Give \"key\" and \"value\", \"values\": {key: value}, \"reset\": true or \"auto_save\"");
		return NULL;
	}
	return state_json (self);
}

static JsonNode *
m_bake_start (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	g_autoptr(GError)   local = NULL;
	JsonObject         *o;

	if (self == NULL)
		return NULL;
	if (params != NULL && json_object_has_member (params, "save"))
		self->auto_save = json_object_get_boolean_member_with_default (params, "save", TRUE);
	if (!lrg_editor_bake_panel_bake (self, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "started", TRUE);
	json_object_set_int_member (o, "objects", lrg_lightmap_baker_get_n_objects (self->baker));
	json_object_set_int_member (o, "lights", lrg_lightmap_baker_get_n_lights (self->baker));
	return obj_node (o);
}

static JsonNode *
m_bake_progress (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);

	return (self != NULL) ? progress_json (self) : NULL;
}

static JsonNode *
m_bake_cancel (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	JsonObject         *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "cancelled", lrg_editor_bake_panel_cancel (self));
	return obj_node (o);
}

static JsonNode *
m_bake_save (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	g_autoptr(GError)   local = NULL;
	const gchar        *dir = NULL;

	if (self == NULL)
		return NULL;
	if (params != NULL && json_object_has_member (params, "dir"))
		dir = json_object_get_string_member (params, "dir");
	if (!lrg_editor_bake_panel_save (self, dir, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s", local->message);
		return NULL;
	}
	return state_json (self);
}

static JsonNode *
m_bake_clear (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	JsonObject         *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "cleared", lrg_editor_bake_panel_clear (self));
	return obj_node (o);
}

/* {position, normal?} or {} for the viewport centre */
static JsonNode *
m_bake_evaluate (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorBakePanel *self = ipc_panel (user_data, error);
	gfloat              p[3], n[3] = { 0.0f, 1.0f, 0.0f }, rgb[3];
	JsonObject         *o;

	if (self == NULL)
		return NULL;
	if (current_level (self) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level is open");
		return NULL;
	}
	if (read_vec3 (params, "position", p))
	{
		gfloat len;

		read_vec3 (params, "normal", n);
		len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len < 1e-6f)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Zero normal");
			return NULL;
		}
		n[0] /= len;
		n[1] /= len;
		n[2] /= len;
		if (!lrg_editor_bake_panel_evaluate (self, p, n, rgb))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
			             "Nothing to trace (no static_lighting geometry, or a bake is running)");
			return NULL;
		}
		self->cursor_valid = TRUE;
		memcpy (self->cursor_pos, p, sizeof self->cursor_pos);
		memcpy (self->cursor_rgb, rgb, sizeof self->cursor_rgb);
	}
	else if (!lrg_editor_bake_panel_sample_cursor (self, p, n, rgb))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "Nothing under the viewport centre to sample (or nothing to trace)");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_array_member (o, "position", vec3_array (p));
	json_object_set_array_member (o, "normal", vec3_array (n));
	json_object_set_array_member (o, "rgb", vec3_array (rgb));
	json_object_set_double_member (o, "luminance", 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]);
	return obj_node (o);
}

static JsonNode *
m_bake_preview (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorBakePanel     *self = ipc_panel (user_data, error);
	LrgEditorSceneRenderer *sr;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	sr = scene_renderer (self);
	if (sr != NULL && params != NULL && json_object_has_member (params, "show"))
		lrg_editor_scene_renderer_set_show_baked (sr, json_object_get_boolean_member (params, "show"));
	if (params != NULL && json_object_has_member (params, "page"))
		lrg_editor_bake_panel_set_page (self, (guint) MAX (json_object_get_int_member (params, "page"), 0));
	sync_widgets (self);
	o = json_object_new ();
	json_object_set_boolean_member (o, "show", sr != NULL && lrg_editor_scene_renderer_get_show_baked (sr));
	json_object_set_int_member (o, "page", self->page);
	json_object_set_int_member (o, "pages", shown_lightmap (self) != NULL ? lrg_lightmap3d_get_n_pages (shown_lightmap (self)) : 0);
	return obj_node (o);
}

static JsonNode *
m_bake_show (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	editor_app_show_panel (app, LRG_EDITOR_BAKE_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "panel", LRG_EDITOR_BAKE_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_BAKE_PANEL_ID));
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_BAKE_PANEL_ID);
}

static void
a_start (gpointer data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (data);

	if (!lrg_editor_bake_panel_get_baking (self))
		on_bake_chip (NULL, data);
}

static void
a_cancel (gpointer data)
{
	lrg_editor_bake_panel_cancel (LRG_EDITOR_BAKE_PANEL (data));
}

static void
a_save (gpointer data)
{
	on_save_chip (NULL, data);
}

static void
a_clear (gpointer data)
{
	lrg_editor_bake_panel_clear (LRG_EDITOR_BAKE_PANEL (data));
}

static void
a_preview (gpointer data)
{
	on_preview_chip (NULL, data);
}

static gboolean
has_level (gpointer data)
{
	return current_level (LRG_EDITOR_BAKE_PANEL (data)) != NULL;
}

static gboolean
is_baking (gpointer data)
{
	return lrg_editor_bake_panel_get_baking (LRG_EDITOR_BAKE_PANEL (data));
}

static gboolean
can_save (gpointer data)
{
	return lrg_editor_bake_panel_has_unsaved (LRG_EDITOR_BAKE_PANEL (data));
}

static gboolean
preview_checked (gpointer data)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (data);

	return scene_renderer (self) != NULL && lrg_editor_scene_renderer_get_show_baked (scene_renderer (self));
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static void
lrg_editor_bake_panel_layout_children (LrgContainer *container)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (container);

	editor_panel_fit_toolbar (self->header_row, lrg_widget_get_width (LRG_WIDGET (self)) - 20.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_bake_panel_dispose (GObject *object)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (object);

	if (self->cancel != NULL)
		g_cancellable_cancel (self->cancel);
	g_clear_object (&self->cancel);
	if (self->settings != NULL)
		g_signal_handlers_disconnect_by_data (self->settings, self);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->editor);
	g_clear_weak_pointer (&self->app);
	g_clear_weak_pointer (&self->level);
	g_clear_object (&self->gesture_proxy);
	g_clear_object (&self->baker);
	g_clear_object (&self->world);
	g_clear_object (&self->lightmap);
	g_clear_object (&self->settings);
	if (IsWindowReady ())
		g_clear_object (&self->atlas_tex);
	else if (self->atlas_tex != NULL)
	{
		/* no context: the texture's finalizer would call GL */
		self->atlas_tex = NULL;
	}
	G_OBJECT_CLASS (lrg_editor_bake_panel_parent_class)->dispose (object);
}

static void
lrg_editor_bake_panel_finalize (GObject *object)
{
	LrgEditorBakePanel *self = LRG_EDITOR_BAKE_PANEL (object);

	g_ptr_array_unref (self->grids);
	g_free (self->error);
	G_OBJECT_CLASS (lrg_editor_bake_panel_parent_class)->finalize (object);
}

static void
lrg_editor_bake_panel_class_init (LrgEditorBakePanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_bake_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_bake_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_bake_panel_layout_children;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
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

static void
lrg_editor_bake_panel_init (LrgEditorBakePanel *self)
{
	static const LrgCustomWidgetFuncs pill_funcs = { pill_measure, pill_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs summary_funcs = { summary_measure, summary_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs atlas_funcs = { atlas_measure, atlas_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs cursor_funcs = { cursor_measure, cursor_paint, NULL, NULL, { NULL } };
	const LrgInsets                   pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	g_autoptr(LrgObjectPropertySource) src = NULL;
	LrgWidget                        *row;
	LrgWidget                        *w;
	LrgWidget                        *box;

	self->grids = g_ptr_array_new_with_free_func (g_object_unref);
	self->auto_save = TRUE;
	self->settings = lrg_lightmap_bake_settings_new ();
	g_signal_connect (self->settings, "notify", G_CALLBACK (on_settings_notify), self);

	/* Header: title, status, Bake / Cancel, Save, Clear, preview toggle */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "bake-top");
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 6.0f);
	lrg_widget_set_margin (self->top, &pad);

	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "bake-header");
	self->header_row = row;
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	w = LRG_WIDGET (lrg_label_new ("Lighting"));
	lrg_widget_set_name (w, "bake-title");
	lrg_widget_add_class (w, "heading");
	lrg_widget_set_valign (w, LRG_ALIGN_CENTER);
	add_to (row, w);
	self->status = custom ("bake-status", &pill_funcs, self);
	lrg_widget_set_valign (self->status, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (self->status, 1.0f);
	add_to (row, self->status);
	self->bake_chip = editor_chip_new ("bake-bake", LRG_ICON_REFRESH, "Bake", on_bake_chip, self);
	editor_chip_set_tint (self->bake_chip, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->bake_chip);
	self->save_chip = editor_chip_new ("bake-save", LRG_ICON_DOWNLOAD, "Save", on_save_chip, self);
	add_to (row, self->save_chip);
	self->preview_chip = editor_chip_new ("bake-preview", LRG_ICON_EYE, NULL, on_preview_chip, self);
	add_to (row, self->preview_chip);
	self->clear_chip = editor_chip_new ("bake-clear", LRG_ICON_TRASH, NULL, on_clear_chip, self);
	editor_chip_set_tint (self->clear_chip, LRG_THEME_COLOR_DANGER);
	add_to (row, self->clear_chip);
	add_to (self->top, row);

	/* Progress */
	self->bar = lrg_progress_bar_new ();
	lrg_widget_set_name (LRG_WIDGET (self->bar), "bake-progress");
	lrg_progress_bar_set_max (self->bar, 1.0);
	lrg_widget_set_visible (LRG_WIDGET (self->bar), FALSE);
	add_to (self->top, LRG_WIDGET (self->bar));
	self->phase_label = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->phase_label, "bake-phase");
	lrg_widget_add_class (self->phase_label, "dim");
	lrg_widget_set_visible (self->phase_label, FALSE);
	add_to (self->top, self->phase_label);

	/* Summary */
	self->summary = custom ("bake-summary", &summary_funcs, self);
	add_to (self->top, self->summary);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* Scrolled body: atlas, cursor readout, settings */
	box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (box, "bake-body");
	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	lrg_widget_set_margin (box, &pad);

	self->atlas_row = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->atlas_row, "bake-atlas-row");
	lrg_container_set_spacing (LRG_CONTAINER (self->atlas_row), 4.0f);
	self->atlas = custom ("bake-atlas", &atlas_funcs, self);
	add_to (self->atlas_row, self->atlas);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "bake-pages");
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_set_halign (row, LRG_ALIGN_CENTER);
	add_to (row, editor_chip_new ("bake-page-prev", LRG_ICON_CHEVRON_LEFT, NULL, on_page_prev, self));
	add_to (row, editor_chip_new ("bake-page-next", LRG_ICON_CHEVRON_RIGHT, NULL, on_page_next, self));
	add_to (self->atlas_row, row);
	add_to (box, self->atlas_row);

	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "bake-cursor-row");
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	w = editor_chip_new ("bake-sample", LRG_ICON_SEARCH, "Sample", on_sample_chip, self);
	lrg_widget_set_valign (w, LRG_ALIGN_CENTER);
	add_to (row, w);
	self->cursor = custom ("bake-cursor", &cursor_funcs, self);
	lrg_widget_set_fill_weight (self->cursor, 1.0f);
	add_to (row, self->cursor);
	add_to (box, row);

	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "bake-grid");
	lrg_property_grid_set_label_width (self->grid, 120.0f);
	g_signal_connect (self->grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (self->grid, "edit-end", G_CALLBACK (on_edit_end), self);
	src = lrg_object_property_source_new (G_OBJECT (self->settings));
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (src));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "bake-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), box);
	g_object_unref (box);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "bake-empty");
	lrg_widget_set_margin (self->empty, &pad);
	add_to (self->empty, editor_panel_hint_new ("bake-hint",
	                                            "Open a 3D level to bake its lighting: mark static geometry with "
	                                            "Static lighting in the inspector, set the lights' bake mode, add "
	                                            "probe volumes, then Bake."));
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);
}

LrgEditorBakePanel *
lrg_editor_bake_panel_new (LrgEditorApp *app)
{
	LrgEditorBakePanel *self;
	LrgEditorActions   *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_BAKE_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "bake-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "bake.show", "Lighting Bake Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "bake.start", "Bake Lighting", "Level", NULL, a_start, has_level, NULL, self);
	lrg_editor_actions_add (actions, "bake.cancel", "Cancel Lighting Bake", "Level", NULL, a_cancel, is_baking, NULL,
	                        self);
	lrg_editor_actions_add (actions, "bake.save", "Save Baked Lighting", "Level", NULL, a_save, can_save, NULL, self);
	lrg_editor_actions_add (actions, "bake.clear", "Clear Baked Lighting", "Level", NULL, a_clear, has_level, NULL,
	                        self);
	lrg_editor_actions_add (actions, "bake.toggle-preview", "Show Baked Lighting", "View", NULL, a_preview, NULL,
	                        preview_checked, self);
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "bake.get", m_bake_get, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.set", m_bake_set, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.start", m_bake_start, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.progress", m_bake_progress, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.cancel", m_bake_cancel, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.save", m_bake_save, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.clear", m_bake_clear, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.evaluate", m_bake_evaluate, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.preview", m_bake_preview, app, NULL);
		lrg_editor_server_register_method (app->server, "bake.show", m_bake_show, app, NULL);
	}

	sync_settings (self);
	g_set_weak_pointer (&self->level, current_level (self));
	sync_widgets (self);
	return self;
}

LrgEditorBakePanel *
lrg_editor_bake_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
