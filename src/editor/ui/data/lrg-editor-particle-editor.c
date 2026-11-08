/* lrg-editor-particle-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorParticleEditor: the `.rparticles` data editor (see the header).
 *
 * Layout:
 *
 *   +-----------------------------------+--------------------------------+
 *   | (one scrolling column)            | [Play] [Restart] ====o== 1.2 s |
 *   | [Effect Settings]                 | Speed [1] Seed [x][42] [Ground]|
 *   | EMITTERS  3                       |  [GPU] [Background v]          |
 *   | [Add] [Duplicate] [Remove] [^][v] | +----------------------------+ |
 *   |  [x] sparks      [S]  120  CPU    | |                            | |
 *   |  [x] smoke       [S]   40  GPU    | |   the effect, the ground,  | |
 *   | STACK  sparks   [kind v] [Add]    | |   the selected emitter's   | |
 *   |  SPAWN    [x] burst    [^][v][x]  | |   bounds                   | |
 *   |  SHAPE    [x] shape    ...        | +----------------------------+ |
 *   |  [Renderer]                       | sparks 120 / 200 · smoke 40    |
 *   | PROPERTIES  burst                 | GPU: smoke runs on the GPU,    |
 *   |  property grid                    |  sparks: sub-emitter ...       |
 *   | WARNINGS (unknown keys)           |                                |
 *   +-----------------------------------+--------------------------------+
 *
 * The document is the truth. Its text is parsed with the engine's strict
 * loader into an LrgParticleAsset (a load error is an issue and empties
 * the views), which the preview effect plays with auto-reload.
 *
 * Edits:
 *  - property grid edits (DELEGATE mode) change the parsed asset in place
 *    through GObject properties, are validated with
 *    lrg_particle_asset_validate() (and reverted when that fails), and the
 *    changed object is written back in the writer's canonical form at its
 *    YAML path; a grid gesture (a drag, a popup) merges into one undo step
 *    (lrg_editor_document_replace_gesture()). The asset is kept, so the
 *    grid rows and the playing preview survive the edit;
 *  - structural edits (emitters, the module stack) change a fresh copy
 *    parsed from the document and write it back as one labelled undo
 *    group; the document change re-parses the asset;
 *  - lrg_editor_particle_editor_set_value() (IPC, scripts) writes the value
 *    into a scratch copy of the document text and runs the strict loader
 *    on it, so a bad value fails with the loader's key-path message and
 *    nothing changes; a valid one is written back canonically.
 *
 * The preview: an LrgParticleEffect of the parsed asset, stepped in the
 * panel's step() at the preview speed. Seeks use fixed 1/60 s steps, so a
 * timeline position always shows the same particles. With the GPU toggle,
 * emitters inside the GPU subset (lrg_gpu_particles_check_emitter()) are
 * simulated and drawn by an LrgGpuParticles each (GPU when a GL 3.3
 * context exists, its bit-identical CPU twin otherwise); the others stay
 * on the effect.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-particle-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../core/lrg-asset-manager.h"
#include "../../../particles/lrg-gpu-particles.h"
#include "../../../particles/lrg-particle-system.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-viewport.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-checkbox.h"
#include "../../../ui/lrg-slider.h"
#include "../../../ui/lrg-spin-box.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define INSTALLED_KEY   "lrg-editor-particles-installed"
#define PREVIEW_STEP    (1.0f / 60.0f)
#define VIEW_FOV        (45.0f)
#define MIN_SPEED       (0.05f)
#define MAX_SPEED       (4.0f)
#define MAX_SEEK        (30.0f)
#define FINISH_PAUSE    (0.6f)
#define STATS_INTERVAL  (0.2f)
#define SEL_EFFECT      (-1)
#define SEL_EMITTER     (-1)
#define SEL_RENDERER    (-2)

/* The default effect of lrg_editor_particle_editor_create(): a soft
 * looping fountain of warm sparks (canonicalised by the engine writer). */
static const gchar DEFAULT_EFFECT[] =
	"version: 1\n"
	"duration: 2\n"
	"emitters:\n"
	"- name: main\n"
	"  max-particles: 500\n"
	"  renderer:\n"
	"    blend: additive\n"
	"  modules:\n"
	"  - type: spawn-rate\n"
	"    rate: 40\n"
	"  - type: shape\n"
	"    shape: cone\n"
	"    radius: 0.15\n"
	"    angle: 18\n"
	"  - type: velocity\n"
	"    speed: [1.5, 3]\n"
	"  - type: lifetime\n"
	"    lifetime: [0.9, 1.5]\n"
	"  - type: size\n"
	"    size: [0.15, 0.3]\n"
	"  - type: color\n"
	"    color: '#ffd27f'\n"
	"  - type: gravity\n"
	"    gravity: [0, -1.5, 0]\n"
	"  - type: size-over-life\n"
	"    curve: 0 1; 1 0.3\n"
	"  - type: alpha-over-life\n"
	"    curve: 0 1; 1 0\n";

/* Stage names, in LrgParticleModuleStage order. */
static const gchar * const STAGE_NAMES[] = {
	"SPAWN", "SHAPE", "INIT", "FORCE", "COLLISION", "OVER LIFE", "EVENT"
};

/* The stage nicks of the IPC replies, in LrgParticleModuleStage order. */
static const gchar * const STAGE_NICKS[] = {
	"spawn", "shape", "init", "force", "collision", "over-life", "event"
};

static const gchar *
stage_nick (guint stage)
{
	return stage < G_N_ELEMENTS (STAGE_NICKS) ? STAGE_NICKS[stage] : "custom";
}

/* Renderer keys shown per renderer kind (the others are hidden). */
static const gchar RENDER_BILLBOARD_KEYS[] = " texture columns rows fps ";
static const gchar RENDER_RIBBON_KEYS[] =
	" texture ribbon-mode width width-curve color-gradient uv-mode uv-tile-length max-points min-distance "
	"trail-lifetime ";
static const gchar RENDER_MESH_KEYS[] = " mesh primitive mesh-scale align lit ";

/* ==========================================================================
 * EditorFxView: the preview viewport with a caption
 * ========================================================================== */

#define EDITOR_TYPE_FX_VIEW (editor_fx_view_get_type ())
G_DECLARE_FINAL_TYPE (EditorFxView, editor_fx_view, EDITOR, FX_VIEW, LrgContainer)

struct _EditorFxView
{
	LrgContainer             parent_instance;

	LrgEditorParticleEditor *editor;     /* weak */
	LrgViewport             *viewport;
	LrgWidget               *caption;

	gfloat                   yaw;
	gfloat                   pitch;
	gfloat                   zoom;
	gboolean                 dragging;
	gfloat                   last_x;
	gfloat                   last_y;

	/* auto-framing, eased toward the effect's bounds */
	gboolean                 framed;
	gfloat                   center[3];
	gfloat                   radius;
};

G_DEFINE_FINAL_TYPE (EditorFxView, editor_fx_view, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * LrgEditorParticleEditor
 * ========================================================================== */

struct _LrgEditorParticleEditor
{
	LrgEditorDataEditor  parent_instance;

	/* the model of the document */
	LrgParticleAsset    *asset;          /* parsed from the document */
	gchar               *load_error;
	gint                 sel_emitter;    /* -1: effect settings */
	gint                 sel_module;     /* -1: emitter, -2: renderer */
	gulong               asset_changed_id;

	/* the preview */
	LrgParticleEffect   *effect;
	gboolean             playing;
	gfloat               speed;
	gint64               seed_override;
	gboolean             show_ground;
	GrlColor             background;
	gint                 solo;
	gboolean             gpu;
	GPtrArray           *gpu_emitters;   /* LrgGpuParticles or NULL, per emitter */
	gboolean             gpu_dirty;
	gfloat               gpu_time;
	gfloat               finished_for;   /* seconds a non-looping preview sat finished */
	LrgAssetManager     *manager;
	gchar               *manager_root;

	/* widgets */
	EditorFxView        *view;
	LrgWidget           *emitters;
	LrgLabel            *emitters_title;
	LrgWidget           *stack;
	LrgLabel            *stack_title;
	LrgComboBox         *kind_combo;
	LrgButton           *settings_button;
	LrgButton           *renderer_button;
	LrgLabel            *grid_title;
	LrgPropertyGrid     *grid;
	LrgLabel            *warnings;
	LrgButton           *play_button;
	LrgSlider           *timeline;
	LrgLabel            *time_label;
	LrgSpinBox          *speed_spin;
	LrgCheckbox         *seed_check;
	LrgSpinBox          *seed_spin;
	LrgCheckbox         *ground_check;
	LrgCheckbox         *gpu_check;
	LrgComboBox         *bg_combo;
	LrgLabel            *stats;
	LrgLabel            *gpu_label;

	/* bookkeeping */
	GObject             *grid_object;    /* what the grid shows (in self->asset) */
	gchar               *grid_path;      /* its YAML path */
	gchar               *grid_hidden;    /* the hidden rows of its source */
	guint                gesture;        /* the grid's gesture, 0 outside one */
	gboolean             gesture_wrote;
	gboolean             syncing;        /* widgets are being set from the model */
	gboolean             committing;     /* the document change is our own in-place edit */
	gboolean             lists_stale;
	gboolean             grid_stale;
	gchar               *lists_sig;
	gfloat               stats_clock;
};

G_DEFINE_FINAL_TYPE (LrgEditorParticleEditor, lrg_editor_particle_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_effect (LrgEditorParticleEditor *self,
                            gboolean                 keep_time);
static void sync_preview_widgets (LrgEditorParticleEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorParticleEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorParticleEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* @path made absolute against the project (or the cwd). */
static gchar *
abs_path (LrgEditorApp *app,
          const gchar  *path)
{
	g_autofree gchar *p = NULL;

	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	p = app != NULL ? editor_app_project_path (app, path) : g_strdup (path);
	return g_canonicalize_filename (p, NULL);
}

/* The basename of the document, for loader messages. */
static gchar *
doc_source (LrgEditorParticleEditor *self)
{
	const gchar *path = lrg_editor_document_get_path (doc_of (self));

	return path != NULL ? g_path_get_basename (path) : g_strdup ("effect.rparticles");
}

/* ==========================================================================
 * YAML paths
 * ========================================================================== */

static gchar *
emitter_path (guint emitter)
{
	return g_strdup_printf ("/emitters/%u", emitter);
}

static gchar *
module_path (guint emitter,
             guint module)
{
	return g_strdup_printf ("/emitters/%u/modules/%u", emitter, module);
}

/* @path without its last segment ("" for a top-level key). */
static gchar *
path_parent (const gchar *path)
{
	const gchar *slash = strrchr (path, '/');

	if (slash == NULL || slash == path)
		return g_strdup ("");
	return g_strndup (path, (gsize) (slash - path));
}

/* ==========================================================================
 * The asset
 * ========================================================================== */

/* A fresh asset parsed from the document text (what structural edits
 * change). */
static LrgParticleAsset *
parse_doc (LrgEditorParticleEditor  *self,
           GError                  **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar *source = doc_source (self);

	if (text == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The document has no text");
		return NULL;
	}
	return lrg_particle_asset_new_from_data (text, -1, source, error);
}

static void
on_asset_changed (LrgParticleAsset *asset,
                  gpointer          user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	/* the effect follows by itself (auto-reload); the GPU copies do not */
	self->gpu_dirty = TRUE;
}

static void
set_asset (LrgEditorParticleEditor *self,
           LrgParticleAsset        *asset)
{
	if (self->asset != NULL && self->asset_changed_id != 0)
		g_signal_handler_disconnect (self->asset, self->asset_changed_id);
	self->asset_changed_id = 0;
	g_clear_object (&self->asset);
	if (asset != NULL)
	{
		self->asset = g_object_ref (asset);
		self->asset_changed_id = g_signal_connect (self->asset, "changed", G_CALLBACK (on_asset_changed), self);
	}
}

/* Parses the document into self->asset and clamps the selection. */
static void
load_asset (LrgEditorParticleEditor *self)
{
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgParticleAsset) asset = NULL;
	guint                       n;

	g_clear_pointer (&self->load_error, g_free);
	asset = parse_doc (self, &error);
	set_asset (self, asset);
	/* the grid shows an object of the old asset: rebind it */
	self->grid_object = NULL;
	self->grid_stale = TRUE;
	if (asset == NULL)
	{
		self->load_error = g_strdup (error != NULL ? error->message : "The document does not load");
		self->sel_emitter = SEL_EFFECT;
		self->sel_module = SEL_EMITTER;
		return;
	}
	n = lrg_particle_asset_get_n_emitters (asset);
	if (self->sel_emitter >= (gint) n)
	{
		self->sel_emitter = n > 0 ? (gint) n - 1 : SEL_EFFECT;
		self->sel_module = SEL_EMITTER;
	}
	if (self->sel_emitter >= 0 && self->sel_module >= 0 &&
	    self->sel_module >= (gint) lrg_particle_asset_emitter_get_n_modules (
	                            lrg_particle_asset_get_emitter (asset, (guint) self->sel_emitter)))
		self->sel_module = SEL_EMITTER;
	if (self->solo >= (gint) n)
		self->solo = -1;
}

/*
 * commit_from:
 * Writes @asset (the parsed asset edited in place, or a fresh copy) back
 * into the document at @path, in the writer's canonical form. When the
 * subtree is missing on either side (a removed module, a renderer block
 * that became all defaults) the nearest common ancestor is written. Inside
 * a gesture the writes merge into one undo step; otherwise each is a
 * group of its own labelled @label. Nothing is written when the text would
 * not change.
 */
static gboolean
commit_from (LrgEditorParticleEditor  *self,
             LrgParticleAsset         *asset,
             const gchar              *path,
             const gchar              *label,
             guint                     gesture,
             GError                  **error)
{
	LrgEditorDocument            *doc = doc_of (self);
	g_autofree gchar             *text = lrg_particle_asset_save_to_data (asset, NULL, error);
	g_autoptr(LrgEditorDocument)  tdoc = NULL;
	g_autofree gchar             *p = g_strdup (path != NULL ? path : "");
	g_autofree gchar             *sub = NULL;
	g_autofree gchar             *now = NULL;
	gboolean                      ok;

	if (text == NULL)
		return FALSE;
	if (*p != '\0')
	{
		tdoc = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, error);
		if (tdoc == NULL)
			return FALSE;
		while (*p != '\0' && (!lrg_editor_document_has_path (tdoc, p) || !lrg_editor_document_has_path (doc, p)))
		{
			gchar *up = path_parent (p);

			g_free (p);
			p = up;
		}
	}
	if (*p == '\0')
	{
		sub = g_steal_pointer (&text);
		now = lrg_editor_document_to_data (doc, NULL);
	}
	else
	{
		sub = lrg_editor_document_get_yaml (tdoc, p);
		now = lrg_editor_document_get_yaml (doc, p);
	}
	if (g_strcmp0 (now, sub) == 0)
		return TRUE;
	if (gesture != 0)
		return lrg_editor_document_replace_gesture (doc, p, sub, gesture, error);
	lrg_editor_document_begin_group (doc, label);
	ok = lrg_editor_document_replace_gesture (doc, p, sub, 0, error);
	lrg_editor_document_end_group (doc);
	return ok;
}

/* Writes the in-place edited self->asset back without re-parsing it. */
static gboolean
commit_in_place (LrgEditorParticleEditor  *self,
                 const gchar              *path,
                 const gchar              *label,
                 guint                     gesture,
                 GError                  **error)
{
	gboolean ok;

	self->committing = TRUE;
	ok = commit_from (self, self->asset, path, label, gesture, error);
	self->committing = FALSE;
	if (!ok)
	{
		/* the asset and the document disagree now: trust the document */
		load_asset (self);
		rebuild_effect (self, TRUE);
		self->lists_stale = TRUE;
		self->grid_stale = TRUE;
	}
	return ok;
}

/* A fresh copy for a structural edit, with an error when it does not load. */
static LrgParticleAsset *
edit_copy (LrgEditorParticleEditor  *self,
           GError                  **error)
{
	if (self->asset == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
		             self->load_error != NULL ? self->load_error : "The effect does not load");
		return NULL;
	}
	return parse_doc (self, error);
}

static LrgParticleAssetEmitter *
copy_emitter (LrgParticleAsset  *copy,
              guint              emitter,
              GError           **error)
{
	if (emitter >= lrg_particle_asset_get_n_emitters (copy))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No emitter %u", emitter);
		return NULL;
	}
	return lrg_particle_asset_get_emitter (copy, emitter);
}

/* A name not used by any emitter: @base, else "@base 2", "@base 3", ... */
static gchar *
unique_emitter_name (LrgParticleAsset *asset,
                     const gchar      *base)
{
	guint n;

	if (lrg_particle_asset_find_emitter (asset, base) == NULL)
		return g_strdup (base);
	for (n = 2; n < 100000; n++)
	{
		g_autofree gchar *name = g_strdup_printf ("%s %u", base, n);

		if (lrg_particle_asset_find_emitter (asset, name) == NULL)
			return g_steal_pointer (&name);
	}
	return g_strdup_printf ("%s %" G_GINT64_FORMAT, base, g_get_real_time ());
}

/* The emitters a sub-emitter module of @emitter triggers (directly). */
static gboolean
triggers (LrgParticleAsset *asset,
          guint             emitter,
          guint             target)
{
	LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (asset, emitter);
	LrgParticleAssetEmitter *t = lrg_particle_asset_get_emitter (asset, target);
	guint                    i;

	if (e == NULL || t == NULL)
		return FALSE;
	for (i = 0; i < lrg_particle_asset_emitter_get_n_modules (e); i++)
	{
		LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, i);
		g_autofree gchar  *name = NULL;

		if (g_strcmp0 (lrg_particle_module_get_kind (m), "sub-emitter") != 0)
			continue;
		g_object_get (m, "emitter", &name, NULL);
		if (g_strcmp0 (name, lrg_particle_asset_emitter_get_name (t)) == 0)
			return TRUE;
	}
	return FALSE;
}

/* Whether the preview draws @emitter under the current solo (the soloed
 * emitter and everything it triggers, transitively). */
static gboolean
solo_shows (LrgEditorParticleEditor *self,
            guint                    emitter)
{
	guint    n, i, pass;
	gboolean shown[64];

	if (self->solo < 0 || self->asset == NULL)
		return TRUE;
	n = MIN (lrg_particle_asset_get_n_emitters (self->asset), G_N_ELEMENTS (shown));
	if (emitter >= n)
		return FALSE;
	memset (shown, 0, sizeof shown);
	if ((guint) self->solo < n)
		shown[self->solo] = TRUE;
	/* closure over the trigger graph (n passes are always enough) */
	for (pass = 0; pass < n; pass++)
		for (i = 0; i < n; i++)
		{
			guint k;

			if (!shown[i])
				continue;
			for (k = 0; k < n; k++)
				if (!shown[k] && triggers (self->asset, i, k))
					shown[k] = TRUE;
		}
	return shown[emitter];
}

/* ==========================================================================
 * The preview
 * ========================================================================== */

static LrgAssetManager *
preview_manager (LrgEditorParticleEditor *self)
{
	LrgEditorApp *app = app_of (self);
	const gchar  *root = app != NULL ? lrg_editor_app_get_project_dir (app) : NULL;

	if (self->manager != NULL && g_strcmp0 (root, self->manager_root) == 0)
		return self->manager;
	g_clear_object (&self->manager);
	g_free (self->manager_root);
	self->manager_root = g_strdup (root);
	self->manager = lrg_asset_manager_new ();
	if (root != NULL)
		lrg_asset_manager_set_project_root (self->manager, root);
	return self->manager;
}

/* The timeline length: a cycle and the tail of the last particles for a
 * finite effect (two cycles when it loops), 5 s for an endless one. */
static gfloat
timeline_length (LrgEditorParticleEditor *self)
{
	gfloat d;

	if (self->asset == NULL)
		return 5.0f;
	d = lrg_particle_asset_get_duration (self->asset);
	if (d <= 0.0f)
		return 5.0f;
	return CLAMP (d * (lrg_particle_asset_get_loop (self->asset) ? 2.0f : 1.0f) + 1.0f, 1.0f, 60.0f);
}

static void
clear_gpu (LrgEditorParticleEditor *self)
{
	g_ptr_array_set_size (self->gpu_emitters, 0);
	self->gpu_dirty = FALSE;
}

/*
 * build_gpu:
 * One LrgGpuParticles per emitter inside the GPU subset (NULL for the
 * others), stepped to the effect's time. Only while the toggle is on.
 */
static void
build_gpu (LrgEditorParticleEditor *self)
{
	guint  i, n;
	gfloat t;

	clear_gpu (self);
	if (!self->gpu || self->asset == NULL)
		return;
	n = lrg_particle_asset_get_n_emitters (self->asset);
	for (i = 0; i < n; i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
		LrgGpuParticles         *gp = NULL;

		if (lrg_particle_asset_emitter_get_enabled (e) && lrg_gpu_particles_check_emitter (self->asset, e, NULL))
		{
			gp = lrg_gpu_particles_new_from_asset (self->asset, i, 0, NULL);
			if (gp != NULL)
			{
				g_autoptr(GError) error = NULL;

				lrg_gpu_particles_set_prefer_gpu (gp, TRUE);
				if (!lrg_gpu_particles_prepare (gp, &error) && error != NULL)
					g_debug ("particle preview: %s", error->message);
			}
		}
		g_ptr_array_add (self->gpu_emitters, gp);
	}
	/* catch up with the effect clock in fixed steps */
	self->gpu_time = 0.0f;
	t = self->effect != NULL ? MIN (lrg_particle_effect_get_time (self->effect), MAX_SEEK) : 0.0f;
	while (self->gpu_time + PREVIEW_STEP * 0.5f < t)
	{
		for (i = 0; i < self->gpu_emitters->len; i++)
			if (g_ptr_array_index (self->gpu_emitters, i) != NULL)
				lrg_gpu_particles_update (g_ptr_array_index (self->gpu_emitters, i), PREVIEW_STEP);
		self->gpu_time += PREVIEW_STEP;
	}
	self->gpu_dirty = FALSE;
}

static LrgGpuParticles *
gpu_of (LrgEditorParticleEditor *self,
        guint                    emitter)
{
	if (!self->gpu || emitter >= self->gpu_emitters->len)
		return NULL;
	return g_ptr_array_index (self->gpu_emitters, emitter);
}

/*
 * rebuild_effect:
 * A new preview effect for self->asset. With @keep_time the preview
 * continues from the old effect's time (seeked deterministically) in the
 * same play state.
 */
static void
rebuild_effect (LrgEditorParticleEditor *self,
                gboolean                 keep_time)
{
	gfloat t = 0.0f;

	if (self->effect != NULL)
	{
		t = lrg_particle_effect_get_time (self->effect);
		if (IsWindowReady ())
			lrg_particle_effect_release_gpu (self->effect);
		g_clear_object (&self->effect);
	}
	clear_gpu (self);
	if (self->asset == NULL)
		return;
	self->effect = lrg_particle_effect_new (self->asset);
	lrg_particle_effect_set_auto_reload (self->effect, TRUE);
	lrg_particle_effect_set_asset_manager (self->effect, preview_manager (self));
	if (self->seed_override >= 0)
		lrg_particle_effect_set_seed (self->effect, (guint64) self->seed_override);
	if (!keep_time)
		t = 0.0f;
	t = MIN (t, MAX_SEEK);
	if (t > 0.0f)
		lrg_particle_effect_seek (self->effect, t, PREVIEW_STEP);
	else
		lrg_particle_effect_restart (self->effect);
	if (!self->playing)
		lrg_particle_effect_pause (self->effect);
	self->finished_for = 0.0f;
	self->gpu_dirty = TRUE;
}

/* One preview frame of @dt seconds of effect time. */
static void
preview_step (LrgEditorParticleEditor *self,
              gfloat                   dt)
{
	guint i;

	if (self->effect == NULL || !self->playing)
		return;
	if (self->gpu && self->gpu_dirty)
		build_gpu (self);
	if (!lrg_particle_effect_is_playing (self->effect))
	{
		/* a finished non-looping effect starts over after a short pause */
		self->finished_for += dt;
		if (self->finished_for >= FINISH_PAUSE)
		{
			lrg_particle_effect_restart (self->effect);
			self->finished_for = 0.0f;
			if (self->gpu)
				build_gpu (self);
		}
		return;
	}
	lrg_particle_effect_update (self->effect, dt);
	for (i = 0; self->gpu && i < self->gpu_emitters->len; i++)
		if (g_ptr_array_index (self->gpu_emitters, i) != NULL)
			lrg_gpu_particles_update (g_ptr_array_index (self->gpu_emitters, i), dt);
	self->gpu_time += dt;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/* The point the camera orbits and the radius it frames. */
static void
frame_target (LrgEditorParticleEditor *self,
              gfloat                  *center,
              gfloat                  *radius)
{
	gfloat mn[3], mx[3];
	guint  k;

	center[0] = 0.0f;
	center[1] = 1.0f;
	center[2] = 0.0f;
	*radius = 2.0f;
	if (self->effect == NULL || !lrg_particle_effect_get_bounds (self->effect, mn, mx))
		return;
	for (k = 0; k < 3; k++)
		center[k] = 0.5f * (mn[k] + mx[k]);
	*radius = 0.5f * sqrtf ((mx[0] - mn[0]) * (mx[0] - mn[0]) + (mx[1] - mn[1]) * (mx[1] - mn[1]) +
	                        (mx[2] - mn[2]) * (mx[2] - mn[2]));
	*radius = CLAMP (*radius, 1.0f, 200.0f);
}

static void
draw_scene (LrgEditorParticleEditor *self,
            EditorFxView            *view,
            gfloat                   width,
            gfloat                   height,
            gboolean                 snap)
{
	Camera3D cam;
	gfloat   c[3], r, dist, eye[3], up[3] = { 0.0f, 1.0f, 0.0f };
	guint    i, k, n;

	ClearBackground ((Color) { self->background.r, self->background.g, self->background.b, 255 });
	frame_target (self, c, &r);
	if (snap || !view->framed)
	{
		memcpy (view->center, c, sizeof c);
		view->radius = r;
		view->framed = TRUE;
	}
	else
	{
		/* ease toward the bounds: the camera does not jitter with them */
		for (k = 0; k < 3; k++)
			view->center[k] += (c[k] - view->center[k]) * 0.04f;
		view->radius += (r - view->radius) * 0.04f;
	}
	dist = view->radius / tanf (0.5f * VIEW_FOV * (gfloat) G_PI / 180.0f) * 1.2f * view->zoom;
	if (width < height && width > 0.0f)
		dist *= height / width;
	eye[0] = view->center[0] + dist * cosf (view->pitch) * sinf (view->yaw);
	eye[1] = view->center[1] + dist * sinf (view->pitch);
	eye[2] = view->center[2] + dist * cosf (view->pitch) * cosf (view->yaw);
	memset (&cam, 0, sizeof cam);
	cam.target = (Vector3) { view->center[0], view->center[1], view->center[2] };
	cam.position = (Vector3) { eye[0], eye[1], eye[2] };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = VIEW_FOV;
	cam.projection = CAMERA_PERSPECTIVE;

	BeginMode3D (cam);
	if (self->show_ground)
	{
		gfloat spacing = view->radius > 6.0f ? 1.0f : 0.5f;

		DrawGrid (24, spacing);
	}
	/* the emitter origin */
	DrawLine3D ((Vector3) { 0, 0, 0 }, (Vector3) { 0.3f, 0, 0 }, (Color) { 230, 90, 90, 255 });
	DrawLine3D ((Vector3) { 0, 0, 0 }, (Vector3) { 0, 0.3f, 0 }, (Color) { 120, 220, 120, 255 });
	DrawLine3D ((Vector3) { 0, 0, 0 }, (Vector3) { 0, 0, 0.3f }, (Color) { 110, 150, 240, 255 });
	rlDrawRenderBatchActive ();

	if (self->effect != NULL)
	{
		n = lrg_particle_effect_get_n_systems (self->effect);
		for (i = 0; i < n; i++)
		{
			LrgGpuParticles *gp = gpu_of (self, i);

			if (!solo_shows (self, i))
				continue;
			if (gp != NULL)
				lrg_gpu_particles_draw (gp);
			else
				lrg_particle_effect_draw_emitter (self->effect, i, eye, up);
		}
		rlDrawRenderBatchActive ();

		/* the selected emitter's bounds */
		if (self->sel_emitter >= 0 && (guint) self->sel_emitter < n)
		{
			gfloat mn[3], mx[3];

			if (lrg_particle_effect_get_emitter_bounds (self->effect, (guint) self->sel_emitter, mn, mx))
				DrawBoundingBox ((BoundingBox) { { mn[0], mn[1], mn[2] }, { mx[0], mx[1], mx[2] } },
				                 (Color) { 255, 200, 90, 150 });
		}
	}
	EndMode3D ();
}

static void
view_render (LrgViewport      *viewport,
             GrlRenderTexture *target,
             gfloat            width,
             gfloat            height,
             gpointer          user_data)
{
	EditorFxView *view = EDITOR_FX_VIEW (user_data);

	if (view->editor == NULL)
	{
		ClearBackground ((Color) { 20, 22, 30, 255 });
		return;
	}
	draw_scene (view->editor, view, width, height, FALSE);
}

/* The caption: time, play state, live particles over a soft scrim. */
static void
view_caption_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	EditorFxView            *view = EDITOR_FX_VIEW (user_data);
	LrgEditorParticleEditor *self = view->editor;
	LrgTheme                *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	const GrlColor           scrim = { 0, 0, 0, 120 };
	const GrlColor           ink = { 236, 238, 246, 255 };
	g_autofree gchar        *text = NULL;
	gfloat                   tw = 0.0f;

	if (self == NULL)
		return;
	if (self->asset == NULL)
		text = g_strdup ("The effect does not load");
	else if (lrg_particle_asset_get_n_emitters (self->asset) == 0)
		text = g_strdup ("No emitters yet: Add one");
	else
		text = g_strdup_printf ("%s  \xc2\xb7  %.2f s  \xc2\xb7  %u particles%s%s",
		                        self->playing ? "playing" : "paused", lrg_editor_particle_editor_get_time (self),
		                        lrg_editor_particle_editor_get_active_count (self, -1),
		                        self->solo >= 0 ? "  \xc2\xb7  solo" : "",
		                        self->gpu ? "  \xc2\xb7  GPU" : "");
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, NULL);
	tw = MIN (tw, width - 20.0f);
	lrg_paint_list_rect (list, x, y, tw + 20.0f, fs + 12.0f, (fs + 12.0f) * 0.5f, &scrim);
	lrg_paint_list_push_clip (list, x, y, tw + 20.0f, fs + 12.0f);
	lrg_paint_list_text (list, font, text, x + 10.0f, y + 6.0f, fs, 0.0f, &ink);
	lrg_paint_list_pop_clip (list);
}

static void
editor_fx_view_layout_children (LrgContainer *container)
{
	EditorFxView *view = EDITOR_FX_VIEW (container);
	gfloat        w = lrg_widget_get_width (LRG_WIDGET (view));
	gfloat        h = lrg_widget_get_height (LRG_WIDGET (view));

	if (view->viewport == NULL || view->caption == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (view->viewport), 0.0f, 0.0f, w, h);
	lrg_widget_place_in_slot (view->caption, 10.0f, 10.0f, MAX (w - 20.0f, 0.0f), 28.0f);
}

static void
editor_fx_view_measure (LrgWidget *widget,
                        gfloat    *width,
                        gfloat    *height)
{
	*width = 280.0f;
	*height = 260.0f;
}

/* Drag to orbit, wheel to zoom, double click to re-frame. */
static gboolean
editor_fx_view_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	EditorFxView *view = EDITOR_FX_VIEW (widget);
	gfloat        x = lrg_ui_event_get_x (event);
	gfloat        y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		view->dragging = TRUE;
		view->last_x = x;
		view->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!view->dragging)
			return FALSE;
		view->yaw -= (x - view->last_x) * 0.012f;
		view->pitch = CLAMP (view->pitch + (y - view->last_y) * 0.010f, -1.35f, 1.35f);
		view->last_x = x;
		view->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!view->dragging)
			return FALSE;
		view->dragging = FALSE;
		return TRUE;
	case LRG_UI_EVENT_SCROLL:
		view->zoom = CLAMP (view->zoom * (lrg_ui_event_get_scroll_y (event) > 0.0f ? 0.9f : 1.1f), 0.2f, 5.0f);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
editor_fx_view_dispose (GObject *object)
{
	EditorFxView *view = EDITOR_FX_VIEW (object);

	if (view->caption != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->caption), NULL, NULL, NULL);
	if (view->viewport != NULL)
		lrg_viewport_set_render_func (view->viewport, NULL, NULL, NULL);
	g_clear_weak_pointer (&view->editor);
	G_OBJECT_CLASS (editor_fx_view_parent_class)->dispose (object);
}

static void
editor_fx_view_class_init (EditorFxViewClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = editor_fx_view_dispose;
	LRG_WIDGET_CLASS (klass)->measure = editor_fx_view_measure;
	LRG_WIDGET_CLASS (klass)->handle_event = editor_fx_view_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = editor_fx_view_layout_children;
}

static void
editor_fx_view_init (EditorFxView *view)
{
	view->yaw = 0.7f;
	view->pitch = 0.32f;
	view->zoom = 1.0f;
	view->radius = 2.0f;
}

static EditorFxView *
editor_fx_view_new (LrgEditorParticleEditor *editor)
{
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, view_caption_paint, NULL, NULL, { NULL } };
	EditorFxView *view = g_object_new (EDITOR_TYPE_FX_VIEW, NULL);

	g_set_weak_pointer (&view->editor, editor);
	lrg_widget_set_name (LRG_WIDGET (view), "particles-preview");

	view->viewport = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (view->viewport), "particles-preview-viewport");
	lrg_viewport_set_auto_clear (view->viewport, FALSE);
	lrg_viewport_set_continuous (view->viewport, TRUE);
	lrg_viewport_set_render_func (view->viewport, view_render, view, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (view->viewport), TRUE);
	lrg_container_add_child (LRG_CONTAINER (view), LRG_WIDGET (view->viewport));
	g_object_unref (view->viewport);

	view->caption = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->caption), &caption_funcs, view, NULL);
	lrg_widget_set_pointer_transparent (view->caption, TRUE);
	lrg_container_add_child (LRG_CONTAINER (view), view->caption);
	g_object_unref (view->caption);
	return view;
}

/* ==========================================================================
 * Widgets: helpers
 * ========================================================================== */

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static LrgWidget *
heading (const gchar *text)
{
	LrgLabel *l = lrg_label_new (text);

	lrg_widget_add_class (LRG_WIDGET (l), "dim-label");
	return LRG_WIDGET (l);
}

static LrgWidget *
hbox (gfloat spacing)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_container_set_spacing (LRG_CONTAINER (row), spacing);
	return row;
}

static LrgButton *
tool_button (const gchar *label,
             const gchar *name,
             GCallback    func,
             gpointer     data)
{
	LrgButton *b = lrg_button_new (label);

	lrg_widget_set_name (LRG_WIDGET (b), name);
	g_signal_connect (b, "clicked", func, data);
	return b;
}

/* A small flat button carrying an emitter and a module index. */
static LrgButton *
index_button (const gchar *label,
              const gchar *name,
              gint         emitter,
              gint         module,
              GCallback    func,
              gpointer     data)
{
	LrgButton *b = tool_button (label, name, func, data);

	lrg_widget_add_class (LRG_WIDGET (b), "flat");
	g_object_set_data (G_OBJECT (b), "emitter", GINT_TO_POINTER (emitter));
	g_object_set_data (G_OBJECT (b), "module", GINT_TO_POINTER (module));
	return b;
}

static gint
data_index (gpointer     object,
            const gchar *key)
{
	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (object), key));
}

/* ==========================================================================
 * The emitter list and the module stack
 * ========================================================================== */

static void
report (LrgEditorParticleEditor *self,
        GError                  *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_emitter_clicked (LrgButton *button,
                    gpointer   user_data)
{
	lrg_editor_particle_editor_select (LRG_EDITOR_PARTICLE_EDITOR (user_data), data_index (button, "emitter"),
	                                   SEL_EMITTER);
}

static void
on_emitter_mute (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *path = NULL;

	if (self->syncing)
		return;
	path = emitter_path ((guint) data_index (check, "emitter"));
	if (!lrg_editor_particle_editor_set_value (self, path, "enabled", checked ? NULL : "false", 0, &error))
		report (self, error);
}

static void
on_emitter_solo (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	gint                     e = data_index (button, "emitter");

	lrg_editor_particle_editor_set_solo (self, self->solo == e ? -1 : e);
}

static void
on_settings_clicked (LrgButton *button,
                     gpointer   user_data)
{
	lrg_editor_particle_editor_select (LRG_EDITOR_PARTICLE_EDITOR (user_data), SEL_EFFECT, SEL_EMITTER);
}

static void
on_renderer_clicked (LrgButton *button,
                     gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (self->sel_emitter >= 0)
		lrg_editor_particle_editor_select (self, self->sel_emitter, SEL_RENDERER);
}

static void
on_add_emitter (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;

	if (lrg_editor_particle_editor_add_emitter (self, NULL, &error) < 0)
		report (self, error);
}

static void
on_duplicate_emitter (LrgButton *button,
                      gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;

	if (self->sel_emitter >= 0 &&
	    lrg_editor_particle_editor_duplicate_emitter (self, (guint) self->sel_emitter, &error) < 0)
		report (self, error);
}

static void
on_remove_emitter (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;

	if (self->sel_emitter >= 0 &&
	    !lrg_editor_particle_editor_remove_emitter (self, (guint) self->sel_emitter, &error))
		report (self, error);
}

static void
on_move_emitter (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	gint                     delta = data_index (button, "module");   /* -1 up, +1 down */
	gint                     to;

	if (self->sel_emitter < 0 || self->asset == NULL)
		return;
	to = self->sel_emitter + delta;
	if (to < 0 || to >= (gint) lrg_particle_asset_get_n_emitters (self->asset))
		return;
	if (!lrg_editor_particle_editor_move_emitter (self, (guint) self->sel_emitter, (guint) to, &error))
		report (self, error);
}

static void
on_module_clicked (LrgButton *button,
                   gpointer   user_data)
{
	lrg_editor_particle_editor_select (LRG_EDITOR_PARTICLE_EDITOR (user_data), data_index (button, "emitter"),
	                                   data_index (button, "module"));
}

static void
on_module_enable (LrgCheckbox *check,
                  gboolean     checked,
                  gpointer     user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *path = NULL;

	if (self->syncing)
		return;
	path = module_path ((guint) data_index (check, "emitter"), (guint) data_index (check, "module"));
	if (!lrg_editor_particle_editor_set_value (self, path, "enabled", checked ? NULL : "false", 0, &error))
		report (self, error);
}

static void
on_module_remove (LrgButton *button,
                  gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;

	if (!lrg_editor_particle_editor_remove_module (self, (guint) data_index (button, "emitter"),
	                                               (guint) data_index (button, "module"), &error))
		report (self, error);
}

/* Moves a module past its neighbour of the same stage (data "delta"). */
static void
on_module_move (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	guint                    e = (guint) data_index (button, "emitter");
	gint                     m = data_index (button, "module");
	gint                     delta = data_index (button, "delta");
	LrgParticleAssetEmitter *em;
	LrgParticleModuleStage   stage;
	gint                     k;

	if (self->asset == NULL || (em = lrg_particle_asset_get_emitter (self->asset, e)) == NULL)
		return;
	stage = lrg_particle_module_get_stage (lrg_particle_asset_emitter_get_module (em, (guint) m));
	for (k = m + delta; k >= 0 && k < (gint) lrg_particle_asset_emitter_get_n_modules (em); k += delta)
		if (lrg_particle_module_get_stage (lrg_particle_asset_emitter_get_module (em, (guint) k)) == stage)
		{
			if (!lrg_editor_particle_editor_move_module (self, e, (guint) m, (guint) k, &error))
				report (self, error);
			return;
		}
}

static void
on_add_module (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	const gchar             *text = lrg_combo_box_get_selected_text (self->kind_combo);
	g_autofree gchar        *kind = NULL;

	if (self->sel_emitter < 0 || text == NULL)
		return;
	/* the combo shows "kind  (stage)" */
	kind = g_strdup (text);
	if (strchr (kind, ' ') != NULL)
		*strchr (kind, ' ') = '\0';
	if (lrg_editor_particle_editor_add_module (self, (guint) self->sel_emitter, kind, -1, &error) < 0)
		report (self, error);
}

/* A signature of what the lists show, so they rebuild only on change. */
static gchar *
lists_signature (LrgEditorParticleEditor *self)
{
	GString *s = g_string_new (self->load_error);
	guint    i, k;

	g_string_append_printf (s, "|%d|%d|%d|%d|", self->sel_emitter, self->sel_module, self->solo, self->gpu);
	for (i = 0; self->asset != NULL && i < lrg_particle_asset_get_n_emitters (self->asset); i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);

		g_string_append_printf (s, "%s:%d:%d[", lrg_particle_asset_emitter_get_name (e),
		                        lrg_particle_asset_emitter_get_enabled (e),
		                        (gint) lrg_particle_render_settings_get_kind (lrg_particle_asset_emitter_get_renderer (e)));
		for (k = 0; k < lrg_particle_asset_emitter_get_n_modules (e); k++)
		{
			LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, k);

			g_string_append_printf (s, "%s%c,", lrg_particle_module_get_kind (m),
			                        lrg_particle_module_get_enabled (m) ? '+' : '-');
		}
		g_string_append (s, "]");
	}
	return g_string_free (s, FALSE);
}

static void
rebuild_emitters (LrgEditorParticleEditor *self,
                  LrgTheme                *theme)
{
	guint i, n;

	lrg_container_remove_all (LRG_CONTAINER (self->emitters));
	if (self->asset == NULL)
	{
		LrgLabel *hint = lrg_label_new (self->load_error != NULL ? self->load_error : "The effect does not load");

		lrg_label_set_color (hint, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		add_to (self->emitters, LRG_WIDGET (hint));
		lrg_label_set_text (self->emitters_title, "EMITTERS");
		return;
	}
	n = lrg_particle_asset_get_n_emitters (self->asset);
	{
		g_autofree gchar *t = g_strdup_printf ("EMITTERS  \xc2\xb7  %u", n);

		lrg_label_set_text (self->emitters_title, t);
	}
	if (n == 0)
		add_to (self->emitters, heading ("No emitters yet: Add one"));
	for (i = 0; i < n; i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
		LrgWidget               *row = hbox (6.0f);
		LrgCheckbox             *mute = lrg_checkbox_new (NULL);
		g_autofree gchar        *mname = g_strdup_printf ("particles-emitter-enabled-%u", i);
		g_autofree gchar        *bname = g_strdup_printf ("particles-emitter-%u", i);
		g_autofree gchar        *sname = g_strdup_printf ("particles-emitter-solo-%u", i);
		g_autofree gchar        *cname = g_strdup_printf ("particles-emitter-count-%u", i);
		LrgButton               *pick = index_button (lrg_particle_asset_emitter_get_name (e), bname, (gint) i,
		                                              SEL_EMITTER, G_CALLBACK (on_emitter_clicked), self);
		LrgButton               *solo = index_button ("S", sname, (gint) i, 0, G_CALLBACK (on_emitter_solo), self);
		LrgLabel                *count = lrg_label_new ("");
		LrgLabel                *backend = lrg_label_new ("");

		lrg_widget_set_name (LRG_WIDGET (mute), mname);
		lrg_checkbox_set_checked (mute, lrg_particle_asset_emitter_get_enabled (e));
		g_object_set_data (G_OBJECT (mute), "emitter", GINT_TO_POINTER ((gint) i));
		g_signal_connect (mute, "toggled", G_CALLBACK (on_emitter_mute), self);
		lrg_widget_set_valign (LRG_WIDGET (mute), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (mute));

		lrg_widget_remove_class (LRG_WIDGET (pick), "flat");
		lrg_widget_add_class (LRG_WIDGET (pick), (gint) i == self->sel_emitter ? "primary" : "flat");
		lrg_widget_set_fill_weight (LRG_WIDGET (pick), 1.0f);
		add_to (row, LRG_WIDGET (pick));

		if (self->solo == (gint) i)
		{
			lrg_widget_remove_class (LRG_WIDGET (solo), "flat");
			lrg_widget_add_class (LRG_WIDGET (solo), "primary");
		}
		add_to (row, LRG_WIDGET (solo));

		lrg_widget_set_name (LRG_WIDGET (count), cname);
		lrg_widget_set_min_size (LRG_WIDGET (count), 64.0f, 0.0f);
		lrg_widget_set_valign (LRG_WIDGET (count), LRG_ALIGN_CENTER);
		lrg_label_set_color (count, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		g_object_set_data (G_OBJECT (count), "emitter", GINT_TO_POINTER ((gint) i));
		add_to (row, LRG_WIDGET (count));

		lrg_widget_set_min_size (LRG_WIDGET (backend), 36.0f, 0.0f);
		lrg_widget_set_valign (LRG_WIDGET (backend), LRG_ALIGN_CENTER);
		if (self->gpu)
		{
			gboolean ok = lrg_editor_particle_editor_get_gpu_status (self, i, NULL);

			lrg_label_set_text (backend, ok ? "GPU" : "CPU");
			lrg_label_set_color (backend, lrg_theme_get_color (theme, ok ? LRG_THEME_COLOR_SUCCESS
			                                                             : LRG_THEME_COLOR_WARNING));
		}
		add_to (row, LRG_WIDGET (backend));
		add_to (self->emitters, row);
	}
}

static void
rebuild_stack (LrgEditorParticleEditor *self,
               LrgTheme                *theme)
{
	LrgParticleAssetEmitter *e;
	guint                    stage, k, n;

	lrg_container_remove_all (LRG_CONTAINER (self->stack));
	lrg_widget_set_visible (LRG_WIDGET (self->renderer_button), self->sel_emitter >= 0 && self->asset != NULL);
	if (self->asset == NULL || self->sel_emitter < 0)
	{
		lrg_label_set_text (self->stack_title, "MODULE STACK");
		add_to (self->stack, heading (self->asset != NULL ? "Select an emitter to edit its modules" : ""));
		return;
	}
	e = lrg_particle_asset_get_emitter (self->asset, (guint) self->sel_emitter);
	{
		g_autofree gchar *t = g_strdup_printf ("MODULE STACK  \xc2\xb7  %s", lrg_particle_asset_emitter_get_name (e));

		lrg_label_set_text (self->stack_title, t);
	}
	{
		g_autoptr(GEnumClass) kinds = g_type_class_ref (LRG_TYPE_PARTICLE_RENDERER_KIND);
		GEnumValue           *kv = g_enum_get_value (kinds, (gint) lrg_particle_render_settings_get_kind (
		                                                    lrg_particle_asset_emitter_get_renderer (e)));
		g_autofree gchar     *rl = g_strdup_printf ("Renderer: %s", kv != NULL ? kv->value_nick : "?");

		lrg_button_set_text (self->renderer_button, rl);
		lrg_widget_remove_class (LRG_WIDGET (self->renderer_button), "primary");
		if (self->sel_module == SEL_RENDERER)
			lrg_widget_add_class (LRG_WIDGET (self->renderer_button), "primary");
	}
	n = lrg_particle_asset_emitter_get_n_modules (e);
	if (n == 0)
		add_to (self->stack, heading ("Empty stack: add a spawn module first"));
	/* grouped by stage, file order inside a stage */
	for (stage = 0; stage < G_N_ELEMENTS (STAGE_NAMES); stage++)
	{
		gboolean any = FALSE;

		for (k = 0; k < n; k++)
		{
			LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, k);
			LrgWidget         *row;
			LrgCheckbox       *en;
			LrgButton         *pick, *up, *down, *rm;
			g_autofree gchar  *ename = NULL, *pname = NULL, *uname = NULL, *dname = NULL, *rname = NULL;
			const gchar       *kind;

			if ((guint) lrg_particle_module_get_stage (m) != stage)
				continue;
			if (!any)
			{
				LrgLabel *h = LRG_LABEL (heading (STAGE_NAMES[stage]));

				lrg_label_set_color (h, lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK));
				add_to (self->stack, LRG_WIDGET (h));
				any = TRUE;
			}
			kind = lrg_particle_module_get_kind (m);
			ename = g_strdup_printf ("particles-module-enabled-%u", k);
			pname = g_strdup_printf ("particles-module-%u", k);
			uname = g_strdup_printf ("particles-module-up-%u", k);
			dname = g_strdup_printf ("particles-module-down-%u", k);
			rname = g_strdup_printf ("particles-module-remove-%u", k);
			row = hbox (4.0f);
			en = lrg_checkbox_new (NULL);
			lrg_widget_set_name (LRG_WIDGET (en), ename);
			lrg_checkbox_set_checked (en, lrg_particle_module_get_enabled (m));
			g_object_set_data (G_OBJECT (en), "emitter", GINT_TO_POINTER (self->sel_emitter));
			g_object_set_data (G_OBJECT (en), "module", GINT_TO_POINTER ((gint) k));
			g_signal_connect (en, "toggled", G_CALLBACK (on_module_enable), self);
			lrg_widget_set_valign (LRG_WIDGET (en), LRG_ALIGN_CENTER);
			add_to (row, LRG_WIDGET (en));
			pick = index_button (kind != NULL ? kind : "?", pname, self->sel_emitter, (gint) k,
			                     G_CALLBACK (on_module_clicked), self);
			lrg_widget_remove_class (LRG_WIDGET (pick), "flat");
			lrg_widget_add_class (LRG_WIDGET (pick), (gint) k == self->sel_module ? "primary" : "flat");
			lrg_widget_set_fill_weight (LRG_WIDGET (pick), 1.0f);
			add_to (row, LRG_WIDGET (pick));
			if (self->gpu && !lrg_particle_module_get_gpu_capable (m))
			{
				LrgLabel *cpu = lrg_label_new ("CPU only");

				lrg_label_set_color (cpu, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
				lrg_widget_set_valign (LRG_WIDGET (cpu), LRG_ALIGN_CENTER);
				add_to (row, LRG_WIDGET (cpu));
			}
			up = index_button ("\xe2\x96\xb2", uname, self->sel_emitter, (gint) k, G_CALLBACK (on_module_move),
			                   self);
			g_object_set_data (G_OBJECT (up), "delta", GINT_TO_POINTER (-1));
			add_to (row, LRG_WIDGET (up));
			down = index_button ("\xe2\x96\xbc", dname, self->sel_emitter, (gint) k, G_CALLBACK (on_module_move),
			                     self);
			g_object_set_data (G_OBJECT (down), "delta", GINT_TO_POINTER (1));
			add_to (row, LRG_WIDGET (down));
			rm = index_button ("\xc3\x97", rname, self->sel_emitter, (gint) k, G_CALLBACK (on_module_remove), self);
			add_to (row, LRG_WIDGET (rm));
			add_to (self->stack, row);
		}
	}
}

static void
rebuild_warnings (LrgEditorParticleEditor *self,
                  LrgTheme                *theme)
{
	g_auto(GStrv)     keys = NULL;
	g_autoptr(GString) s = g_string_new (NULL);
	guint             i;

	if (self->asset != NULL)
		keys = lrg_particle_asset_get_unknown_keys (self->asset);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		g_string_append_printf (s, "%sunknown key %s (kept on save; a typo?)", i > 0 ? "\n" : "", keys[i]);
	lrg_label_set_text (self->warnings, s->str);
	lrg_label_set_color (self->warnings, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
	lrg_widget_set_visible (LRG_WIDGET (self->warnings), s->len > 0);
}

static void
rebuild_lists (LrgEditorParticleEditor *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));

	self->lists_stale = FALSE;
	g_free (self->lists_sig);
	self->lists_sig = lists_signature (self);
	self->syncing = TRUE;
	lrg_widget_remove_class (LRG_WIDGET (self->settings_button), "primary");
	if (self->sel_emitter < 0)
		lrg_widget_add_class (LRG_WIDGET (self->settings_button), "primary");
	rebuild_emitters (self, theme);
	rebuild_stack (self, theme);
	rebuild_warnings (self, theme);
	self->syncing = FALSE;
}

/* ==========================================================================
 * The property grid
 * ========================================================================== */

/* The object the selection names in self->asset, its YAML path, a title
 * and the rows to hide. */
static GObject *
selected_object (LrgEditorParticleEditor  *self,
                 gchar                   **path,
                 gchar                   **title,
                 gchar                   **hidden)
{
	LrgParticleAssetEmitter   *e;
	LrgParticleRenderSettings *r;
	const gchar               *keep;

	*path = NULL;
	*title = NULL;
	*hidden = NULL;
	if (self->asset == NULL)
		return NULL;
	if (self->sel_emitter < 0)
	{
		*path = g_strdup ("");
		*title = g_strdup ("EFFECT SETTINGS");
		return G_OBJECT (self->asset);
	}
	e = lrg_particle_asset_get_emitter (self->asset, (guint) self->sel_emitter);
	if (e == NULL)
		return NULL;
	if (self->sel_module == SEL_EMITTER)
	{
		*path = emitter_path ((guint) self->sel_emitter);
		*title = g_strdup_printf ("EMITTER  \xc2\xb7  %s", lrg_particle_asset_emitter_get_name (e));
		*hidden = g_strdup ("renderer");
		return G_OBJECT (e);
	}
	if (self->sel_module == SEL_RENDERER)
	{
		GString      *h = g_string_new (NULL);
		GParamSpec  **specs;
		guint         n, i;

		r = lrg_particle_asset_emitter_get_renderer (e);
		switch (lrg_particle_render_settings_get_kind (r))
		{
		case LRG_PARTICLE_RENDERER_BILLBOARD:
			keep = RENDER_BILLBOARD_KEYS;
			break;
		case LRG_PARTICLE_RENDERER_RIBBON:
			keep = RENDER_RIBBON_KEYS;
			break;
		case LRG_PARTICLE_RENDERER_MESH:
			keep = RENDER_MESH_KEYS;
			break;
		default:
			keep = "";
			break;
		}
		/* kind, blend and layer always; the rest by renderer kind */
		specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (r), &n);
		for (i = 0; i < n; i++)
		{
			const gchar      *name = specs[i]->name;
			g_autofree gchar *probe = g_strdup_printf (" %s ", name);

			if (g_strcmp0 (name, "kind") == 0 || g_strcmp0 (name, "blend") == 0 || g_strcmp0 (name, "layer") == 0)
				continue;
			if (strstr (keep, probe) == NULL)
				g_string_append_printf (h, "%s ", name);
		}
		g_free (specs);
		*path = g_strdup_printf ("/emitters/%d/renderer", self->sel_emitter);
		*title = g_strdup_printf ("RENDERER  \xc2\xb7  %s", lrg_particle_asset_emitter_get_name (e));
		*hidden = g_string_free (h, FALSE);
		return G_OBJECT (r);
	}
	if (self->sel_module >= 0 && (guint) self->sel_module < lrg_particle_asset_emitter_get_n_modules (e))
	{
		LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, (guint) self->sel_module);

		*path = module_path ((guint) self->sel_emitter, (guint) self->sel_module);
		*title = g_strdup_printf ("MODULE  \xc2\xb7  %s", lrg_particle_module_get_kind (m));
		return G_OBJECT (m);
	}
	return NULL;
}

static void
rebuild_grid (LrgEditorParticleEditor *self)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *title = NULL;
	g_autofree gchar *hidden = NULL;
	GObject          *obj;

	self->grid_stale = FALSE;
	obj = selected_object (self, &path, &title, &hidden);
	lrg_label_set_text (self->grid_title, title != NULL ? title : "PROPERTIES");
	if (obj == self->grid_object && g_strcmp0 (hidden, self->grid_hidden) == 0 && obj != NULL)
	{
		lrg_property_grid_refresh (self->grid);
		return;
	}
	self->grid_object = obj;
	g_free (self->grid_path);
	self->grid_path = g_strdup (path);
	g_free (self->grid_hidden);
	self->grid_hidden = g_strdup (hidden);
	if (obj == NULL)
	{
		lrg_property_grid_set_source (self->grid, NULL);
		return;
	}
	{
		g_autoptr(LrgObjectPropertySource) src = lrg_object_property_source_new (obj);
		g_auto(GStrv)                      names = g_strsplit (hidden != NULL ? hidden : "", " ", -1);
		guint                              i;

		for (i = 0; names[i] != NULL; i++)
			if (*names[i] != '\0')
				lrg_object_property_source_hide_property (src, names[i]);
		lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (src));
	}
}

/*
 * apply_property:
 * A grid edit: sets @name on the shown object in place, validates the
 * asset (reverting the value when it fails) and writes the object back.
 */
static gboolean
apply_property (LrgEditorParticleEditor  *self,
                const gchar              *name,
                const GValue             *value,
                guint                     gesture,
                GError                  **error)
{
	GObject          *obj = self->grid_object;
	GParamSpec       *spec;
	g_auto(GValue)    old = G_VALUE_INIT;
	g_autofree gchar *old_name = NULL;
	g_autofree gchar *label = NULL;
	gboolean          renamed = FALSE;

	if (obj == NULL || self->asset == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Nothing is selected");
		return FALSE;
	}
	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (obj), name);
	if (spec == NULL || !(spec->flags & G_PARAM_WRITABLE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No writable property '%s'", name);
		return FALSE;
	}
	g_value_init (&old, spec->value_type);
	g_object_get_property (obj, name, &old);

	/* an emitter rename carries the sub-emitter references along */
	if (LRG_IS_PARTICLE_ASSET_EMITTER (obj) && g_strcmp0 (name, "name") == 0)
	{
		old_name = g_strdup (lrg_particle_asset_emitter_get_name (LRG_PARTICLE_ASSET_EMITTER (obj)));
		if (G_VALUE_HOLDS_STRING (value) &&
		    (g_value_get_string (value) == NULL || *g_value_get_string (value) == '\0'))
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "An emitter needs a name");
			return FALSE;
		}
		if (G_VALUE_HOLDS_STRING (value) && g_strcmp0 (g_value_get_string (value), old_name) != 0 &&
		    lrg_particle_asset_find_emitter (self->asset, g_value_get_string (value)) != NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "There is already an emitter '%s'",
			             g_value_get_string (value));
			return FALSE;
		}
		renamed = TRUE;
	}
	g_object_set_property (obj, name, value);
	if (renamed)
	{
		const gchar *new_name = lrg_particle_asset_emitter_get_name (LRG_PARTICLE_ASSET_EMITTER (obj));
		guint        i, k;

		for (i = 0; i < lrg_particle_asset_get_n_emitters (self->asset); i++)
		{
			LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);

			for (k = 0; k < lrg_particle_asset_emitter_get_n_modules (e); k++)
			{
				LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, k);
				g_autofree gchar  *target = NULL;

				if (g_strcmp0 (lrg_particle_module_get_kind (m), "sub-emitter") != 0)
					continue;
				g_object_get (m, "emitter", &target, NULL);
				if (g_strcmp0 (target, old_name) == 0)
					g_object_set (m, "emitter", new_name, NULL);
			}
		}
	}
	if (!lrg_particle_asset_validate (self->asset, error))
	{
		g_object_set_property (obj, name, &old);
		return FALSE;
	}
	label = g_strdup_printf ("Set %s", name);
	return commit_in_place (self, renamed ? "" : self->grid_path, label, gesture, error);
}

static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
	self->gesture_wrote = FALSE;
}

static void
on_grid_edit_value (LrgPropertyGrid *grid,
                    const gchar     *name,
                    const GValue    *value,
                    guint            gesture_id,
                    gboolean         interactive,
                    gpointer         user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	guint                    gesture = self->gesture;

	if (gesture == 0)
		gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
	if (apply_property (self, name, value, gesture, &error))
		self->gesture_wrote = TRUE;
	else
	{
		report (self, error);
		lrg_property_grid_refresh (self->grid);
	}
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit,
                  gpointer         user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	/* Escape: the gesture's writes are one undo step; take it back */
	if (!commit && self->gesture_wrote)
		lrg_editor_document_undo (doc_of (self));
	self->gesture = 0;
	self->gesture_wrote = FALSE;
}

static void
on_grid_reset (LrgPropertyGrid *grid,
               const gchar     *name,
               gpointer         user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;

	if (self->grid_path != NULL && !lrg_editor_particle_editor_set_value (self, self->grid_path, name, NULL, 0, &error))
		report (self, error);
}

/* ==========================================================================
 * The preview toolbar
 * ========================================================================== */

static void
on_play (LrgButton *button,
         gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	lrg_editor_particle_editor_set_playing (self, !self->playing);
}

static void
on_restart (LrgButton *button,
            gpointer   user_data)
{
	lrg_editor_particle_editor_restart (LRG_EDITOR_PARTICLE_EDITOR (user_data));
}

static void
on_timeline (LrgSlider *slider,
             gpointer   user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (self->syncing)
		return;
	lrg_editor_particle_editor_seek (self, (gfloat) lrg_slider_get_value (slider));
}

static void
on_speed (LrgSpinBox *spin,
          gpointer    user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_particle_editor_set_speed (self, (gfloat) lrg_spin_box_get_value (spin));
}

static void
on_seed (gpointer  widget,
         gpointer  user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (self->syncing)
		return;
	lrg_editor_particle_editor_set_seed_override (self, lrg_checkbox_get_checked (self->seed_check)
	                                              ? (gint64) lrg_spin_box_get_value (self->seed_spin) : -1);
}

static void
on_seed_check (LrgCheckbox *check,
               gboolean     checked,
               gpointer     user_data)
{
	on_seed (check, user_data);
}

static void
on_ground (LrgCheckbox *check,
           gboolean     checked,
           gpointer     user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_particle_editor_set_show_ground (self, checked);
}

static void
on_gpu (LrgCheckbox *check,
        gboolean     checked,
        gpointer     user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_particle_editor_set_gpu (self, checked);
}

/* Background presets of the combo (the IPC takes any colour). */
static const struct { const gchar *label; guint8 r, g, b; } BACKGROUNDS[] = {
	{ "Night", 18, 20, 28 },
	{ "Dusk", 46, 52, 72 },
	{ "Grey", 110, 112, 118 },
	{ "Sky", 150, 186, 222 },
	{ "White", 236, 236, 240 },
};

static void
on_background (LrgComboBox *combo,
               gpointer     user_data)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (user_data);
	gint                     i = lrg_combo_box_get_selected (combo);
	GrlColor                 c;

	if (self->syncing || i < 0 || (guint) i >= G_N_ELEMENTS (BACKGROUNDS))
		return;
	c.r = BACKGROUNDS[i].r;
	c.g = BACKGROUNDS[i].g;
	c.b = BACKGROUNDS[i].b;
	c.a = 255;
	lrg_editor_particle_editor_set_background (self, &c);
}

/* The statistics line and the GPU messages. */
static void
update_stats (LrgEditorParticleEditor *self)
{
	g_autoptr(GString) s = g_string_new (NULL);
	g_autoptr(GString) g = g_string_new (NULL);
	guint              i, n;
	gfloat             mn[3], mx[3];

	if (self->asset == NULL || self->effect == NULL)
	{
		lrg_label_set_text (self->stats, "");
		lrg_label_set_text (self->gpu_label, "");
		return;
	}
	n = lrg_particle_asset_get_n_emitters (self->asset);
	for (i = 0; i < n; i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
		guint                    c = lrg_editor_particle_editor_get_active_count (self, (gint) i);
		g_autofree gchar        *cname = g_strdup_printf ("particles-emitter-count-%u", i);
		LrgWidget               *cl = lrg_widget_find (LRG_WIDGET (self->emitters), cname);

		g_string_append_printf (s, "%s%s %u / %u", i > 0 ? "   " : "", lrg_particle_asset_emitter_get_name (e), c,
		                        lrg_particle_asset_emitter_get_max_particles (e));
		if (cl != NULL)
		{
			g_autofree gchar *t = g_strdup_printf ("%u", c);

			lrg_label_set_text (LRG_LABEL (cl), t);
		}
		if (self->gpu)
		{
			g_autofree gchar *msg = NULL;
			LrgGpuParticles  *gp = gpu_of (self, i);

			if (lrg_editor_particle_editor_get_gpu_status (self, i, &msg))
				g_string_append_printf (g, "%s%s: %s", g->len > 0 ? "\n" : "", lrg_particle_asset_emitter_get_name (e),
				                        gp != NULL && lrg_gpu_particles_get_backend (gp) == LRG_PARTICLE_BACKEND_GPU
				                        ? "runs on the GPU" : "GPU subset (CPU twin: no GL 3.3 context)");
			else
				g_string_append_printf (g, "%s%s: CPU (%s)", g->len > 0 ? "\n" : "",
				                        lrg_particle_asset_emitter_get_name (e), msg != NULL ? msg : "disabled");
		}
	}
	if (lrg_particle_effect_get_bounds (self->effect, mn, mx))
		g_string_append_printf (s, "   \xc2\xb7   bounds %.2f x %.2f x %.2f", mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]);
	lrg_label_set_text (self->stats, s->str);
	lrg_label_set_text (self->gpu_label, g->str);
	lrg_widget_set_visible (LRG_WIDGET (self->gpu_label), g->len > 0);
}

/* The toolbar from the preview state (no signals act on it). */
static void
sync_preview_widgets (LrgEditorParticleEditor *self)
{
	gfloat           len = timeline_length (self);
	gfloat           t = lrg_editor_particle_editor_get_time (self);
	g_autofree gchar *tl = NULL;

	if (self->timeline == NULL)
		return;
	self->syncing = TRUE;
	lrg_button_set_text (self->play_button, self->playing ? "Pause" : "Play");
	if (lrg_slider_get_max (self->timeline) != len)
		lrg_slider_set_range (self->timeline, 0.0, len);
	lrg_slider_set_value (self->timeline, fmodf (t, len));
	tl = g_strdup_printf ("%.2f s", t);
	lrg_label_set_text (self->time_label, tl);
	if (fabs (lrg_spin_box_get_value (self->speed_spin) - self->speed) > 1e-4)
		lrg_spin_box_set_value (self->speed_spin, self->speed);
	if (lrg_checkbox_get_checked (self->seed_check) != (self->seed_override >= 0))
		lrg_checkbox_set_checked (self->seed_check, self->seed_override >= 0);
	if (self->seed_override >= 0 && (gint64) lrg_spin_box_get_value (self->seed_spin) != self->seed_override)
		lrg_spin_box_set_value (self->seed_spin, (gdouble) self->seed_override);
	if (lrg_checkbox_get_checked (self->ground_check) != self->show_ground)
		lrg_checkbox_set_checked (self->ground_check, self->show_ground);
	if (lrg_checkbox_get_checked (self->gpu_check) != self->gpu)
		lrg_checkbox_set_checked (self->gpu_check, self->gpu);
	self->syncing = FALSE;
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
particles_refresh (LrgEditorDataEditor *editor,
                   const gchar         *path)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (editor);
	g_autofree gchar        *sig = NULL;

	if (self->grid == NULL)
		return;
	if (!self->committing)
	{
		/* an outside change (undo, the YAML pane, a reload): re-parse */
		load_asset (self);
		rebuild_effect (self, TRUE);
		self->grid_object = NULL;
		self->grid_stale = TRUE;
	}
	sig = lists_signature (self);
	if (g_strcmp0 (sig, self->lists_sig) != 0)
		self->lists_stale = TRUE;
	if (self->committing)
		lrg_property_grid_refresh (self->grid);
}

/* The YAML path at the start of a loader message ("/emitters/0/...: "). */
static gchar *
message_path (const gchar *message)
{
	const gchar *colon;

	if (message == NULL || message[0] != '/')
		return NULL;
	colon = strstr (message, ": ");
	return colon != NULL ? g_strndup (message, (gsize) (colon - message)) : NULL;
}

/* Removes the error issues whose message is (or contains) @message. */
static void
remove_same_error (GPtrArray   *issues,
                   const gchar *message)
{
	guint k = 0;

	while (k < issues->len)
	{
		LrgEditorDocumentIssue *is = g_ptr_array_index (issues, k);

		if (is->severity == LRG_EDITOR_PROBLEM_ERROR && is->message != NULL &&
		    (strstr (is->message, message) != NULL || strstr (message, is->message) != NULL))
			g_ptr_array_remove_index (issues, k);
		else
			k++;
	}
}

static void
particles_validate (LrgEditorDataEditor *editor,
                    GPtrArray           *issues)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (editor);
	g_auto(GStrv)            keys = NULL;
	guint                    i, line, col;

	if (self->load_error != NULL)
	{
		g_autofree gchar *p = message_path (strchr (self->load_error, '/') != NULL
		                                    ? strchr (self->load_error, '/') : NULL);

		line = 1;
		col = 1;
		if (p != NULL)
			lrg_editor_document_get_line (doc_of (self), p, &line, &col);
		/* the document's own validation ran the loader too: replace its
		 * issue of the same error by this one, which names the key path */
		remove_same_error (issues, self->load_error);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, p != NULL ? p : "", line,
		                                                        col, self->load_error));
		return;
	}
	if (self->asset == NULL)
		return;
	if (lrg_particle_asset_get_n_emitters (self->asset) == 0)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_INFO, "/emitters", 0, 0,
		                                                        "The effect has no emitters"));
	keys = lrg_particle_asset_get_unknown_keys (self->asset);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *msg = g_strdup_printf ("unknown key %s: kept on save, but nothing reads it (a typo?)",
		                                         keys[i]);

		line = col = 0;
		lrg_editor_document_get_line (doc_of (self), keys[i], &line, &col);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, keys[i], line, col, msg));
	}
	for (i = 0; i < lrg_particle_asset_get_n_emitters (self->asset); i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
		guint                    k;
		gboolean                 spawns = FALSE;

		for (k = 0; k < lrg_particle_asset_emitter_get_n_modules (e); k++)
			if (lrg_particle_module_get_stage (lrg_particle_asset_emitter_get_module (e, k)) ==
			    LRG_PARTICLE_MODULE_STAGE_SPAWN)
				spawns = TRUE;
		/* an emitter without spawn modules only makes sense as a target */
		if (!spawns)
		{
			gboolean target = FALSE;
			guint    j;

			for (j = 0; j < lrg_particle_asset_get_n_emitters (self->asset) && !target; j++)
				target = j != i && triggers (self->asset, j, i);
			if (!target)
			{
				g_autofree gchar *path = emitter_path (i);
				g_autofree gchar *msg = g_strdup_printf ("emitter '%s' has no spawn module and no emitter "
				                                         "triggers it: it never emits",
				                                         lrg_particle_asset_emitter_get_name (e));

				line = col = 0;
				lrg_editor_document_get_line (doc_of (self), path, &line, &col);
				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col,
				                                                        msg));
			}
		}
	}
}

static void
particles_reveal (LrgEditorDataEditor *editor,
                  const gchar         *path,
                  guint                line)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (editor);
	guint                    e, m;

	if (path == NULL)
		return;
	if (sscanf (path, "/emitters/%u/modules/%u", &e, &m) == 2)
		lrg_editor_particle_editor_select (self, (gint) e, (gint) m);
	else if (sscanf (path, "/emitters/%u", &e) == 1)
		lrg_editor_particle_editor_select (self, (gint) e, g_str_has_prefix (strchr (path + 10, '/') != NULL
		                                                                    ? strchr (path + 10, '/') : "",
		                                                                    "/renderer")
		                                                   ? SEL_RENDERER : SEL_EMITTER);
	else
		lrg_editor_particle_editor_select (self, SEL_EFFECT, SEL_EMITTER);
}

static gchar *
particles_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (editor);

	return g_strdup_printf ("emitters=%u emitter=%d module=%d playing=%s time=%.2f solo=%d gpu=%s",
	                        self->asset != NULL ? lrg_particle_asset_get_n_emitters (self->asset) : 0u,
	                        self->sel_emitter, self->sel_module, self->playing ? "true" : "false",
	                        lrg_editor_particle_editor_get_time (self), self->solo, self->gpu ? "true" : "false");
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_particle_editor_step (LrgWidget *widget,
                                 gfloat     delta_time)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (widget);
	gboolean                 busy;

	/* simulate before the viewport (a child) renders in its own step */
	preview_step (self, delta_time * self->speed);
	busy = LRG_WIDGET_CLASS (lrg_editor_particle_editor_parent_class)->step (widget, delta_time);
	if (self->lists_stale)
		rebuild_lists (self);
	if (self->grid_stale)
		rebuild_grid (self);
	self->stats_clock += delta_time;
	if (self->stats_clock >= STATS_INTERVAL)
	{
		self->stats_clock = 0.0f;
		update_stats (self);
	}
	sync_preview_widgets (self);
	return busy || self->playing;
}

static void
lrg_editor_particle_editor_constructed (GObject *object)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (object);
	LrgEditorDataEditor     *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp            *app;
	LrgWidget               *column, *row, *right, *section, *scroll;
	LrgSplitter             *outer;
	g_auto(GStrv)            kinds = NULL;
	guint                    i;

	G_OBJECT_CLASS (lrg_editor_particle_editor_parent_class)->constructed (object);
	app = app_of (self);

	/* --- left: one scrolling column --- */
	column = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (column, "particles-column");
	lrg_container_set_spacing (LRG_CONTAINER (column), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (column), 8.0f);

	self->settings_button = tool_button ("Effect Settings", "particles-settings", G_CALLBACK (on_settings_clicked),
	                                     self);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->settings_button));
	g_object_unref (self->settings_button);

	/* emitters */
	self->emitters_title = LRG_LABEL (heading ("EMITTERS"));
	lrg_widget_set_name (LRG_WIDGET (self->emitters_title), "particles-emitters-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->emitters_title));
	g_object_unref (self->emitters_title);
	row = hbox (6.0f);
	add_to (row, LRG_WIDGET (tool_button ("Add", "particles-add-emitter", G_CALLBACK (on_add_emitter), self)));
	add_to (row, LRG_WIDGET (tool_button ("Duplicate", "particles-duplicate-emitter",
	                                      G_CALLBACK (on_duplicate_emitter), self)));
	add_to (row, LRG_WIDGET (tool_button ("Remove", "particles-remove-emitter", G_CALLBACK (on_remove_emitter),
	                                      self)));
	add_to (row, LRG_WIDGET (index_button ("\xe2\x96\xb2", "particles-emitter-up", 0, -1,
	                                       G_CALLBACK (on_move_emitter), self)));
	add_to (row, LRG_WIDGET (index_button ("\xe2\x96\xbc", "particles-emitter-down", 0, 1,
	                                       G_CALLBACK (on_move_emitter), self)));
	add_to (column, row);
	self->emitters = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->emitters, "particles-emitters");
	lrg_container_set_spacing (LRG_CONTAINER (self->emitters), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (column), self->emitters);
	g_object_unref (self->emitters);

	/* the module stack */
	self->stack_title = LRG_LABEL (heading ("MODULE STACK"));
	lrg_widget_set_name (LRG_WIDGET (self->stack_title), "particles-stack-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->stack_title));
	g_object_unref (self->stack_title);
	row = hbox (6.0f);
	self->kind_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->kind_combo), "particles-module-kind");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->kind_combo), 1.0f);
	kinds = lrg_particle_module_list_kinds ();
	for (i = 0; kinds != NULL && kinds[i] != NULL; i++)
	{
		g_autoptr(LrgParticleModule) probe = lrg_particle_module_new (kinds[i]);
		g_autofree gchar            *text = NULL;
		guint                        stage = probe != NULL ? (guint) lrg_particle_module_get_stage (probe) : 0u;

		text = g_strdup_printf ("%s  (%s)", kinds[i],
		                        stage < G_N_ELEMENTS (STAGE_NAMES) ? STAGE_NAMES[stage] : "custom");
		lrg_combo_box_append (self->kind_combo, text);
	}
	lrg_combo_box_set_selected (self->kind_combo, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->kind_combo));
	g_object_unref (self->kind_combo);
	add_to (row, LRG_WIDGET (tool_button ("Add Module", "particles-add-module", G_CALLBACK (on_add_module), self)));
	add_to (column, row);
	self->renderer_button = tool_button ("Renderer", "particles-renderer", G_CALLBACK (on_renderer_clicked), self);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->renderer_button));
	g_object_unref (self->renderer_button);
	self->stack = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->stack, "particles-stack");
	lrg_container_set_spacing (LRG_CONTAINER (self->stack), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (column), self->stack);
	g_object_unref (self->stack);

	/* the property grid */
	self->grid_title = LRG_LABEL (heading ("PROPERTIES"));
	lrg_widget_set_name (LRG_WIDGET (self->grid_title), "particles-grid-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->grid_title));
	g_object_unref (self->grid_title);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "particles-grid");
	lrg_property_grid_set_label_width (self->grid, 140.0f);
	lrg_property_grid_set_apply_mode (self->grid, LRG_PROPERTY_APPLY_DELEGATE);
	g_signal_connect_object (self->grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self, 0);
	g_signal_connect_object (self->grid, "edit-value", G_CALLBACK (on_grid_edit_value), self, 0);
	g_signal_connect_object (self->grid, "edit-end", G_CALLBACK (on_grid_edit_end), self, 0);
	g_signal_connect_object (self->grid, "reset-requested", G_CALLBACK (on_grid_reset), self, 0);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	self->warnings = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->warnings), "particles-warnings");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->warnings));
	g_object_unref (self->warnings);

	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "particles-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), column);
	g_object_unref (column);

	/* --- right: the preview --- */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (right), 6.0f);

	row = hbox (6.0f);
	self->play_button = tool_button ("Pause", "particles-play", G_CALLBACK (on_play), self);
	lrg_widget_add_class (LRG_WIDGET (self->play_button), "primary");
	lrg_widget_set_min_size (LRG_WIDGET (self->play_button), 72.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->play_button));
	g_object_unref (self->play_button);
	add_to (row, LRG_WIDGET (tool_button ("Restart", "particles-restart", G_CALLBACK (on_restart), self)));
	self->timeline = lrg_slider_new_with_range (0.0, 5.0, 0.0);
	lrg_widget_set_name (LRG_WIDGET (self->timeline), "particles-timeline");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->timeline), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (self->timeline), LRG_ALIGN_CENTER);
	g_signal_connect_object (self->timeline, "value-changed", G_CALLBACK (on_timeline), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->timeline));
	g_object_unref (self->timeline);
	self->time_label = lrg_label_new ("0.00 s");
	lrg_widget_set_name (LRG_WIDGET (self->time_label), "particles-time");
	lrg_widget_set_min_size (LRG_WIDGET (self->time_label), 64.0f, 0.0f);
	lrg_widget_set_valign (LRG_WIDGET (self->time_label), LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->time_label));
	g_object_unref (self->time_label);
	add_to (right, row);

	row = hbox (8.0f);
	add_to (row, heading ("Speed"));
	self->speed_spin = lrg_spin_box_new_with_range (MIN_SPEED, MAX_SPEED, 0.05);
	lrg_spin_box_set_digits (self->speed_spin, 2);
	lrg_spin_box_set_value (self->speed_spin, 1.0);
	lrg_widget_set_name (LRG_WIDGET (self->speed_spin), "particles-speed");
	lrg_widget_set_min_size (LRG_WIDGET (self->speed_spin), 80.0f, 0.0f);
	g_signal_connect_object (self->speed_spin, "value-changed", G_CALLBACK (on_speed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->speed_spin));
	g_object_unref (self->speed_spin);
	self->seed_check = lrg_checkbox_new ("Seed");
	lrg_widget_set_name (LRG_WIDGET (self->seed_check), "particles-seed-override");
	lrg_widget_set_valign (LRG_WIDGET (self->seed_check), LRG_ALIGN_CENTER);
	g_signal_connect_object (self->seed_check, "toggled", G_CALLBACK (on_seed_check), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->seed_check));
	g_object_unref (self->seed_check);
	self->seed_spin = lrg_spin_box_new_with_range (0.0, 4294967295.0, 1.0);
	lrg_widget_set_name (LRG_WIDGET (self->seed_spin), "particles-seed");
	lrg_widget_set_min_size (LRG_WIDGET (self->seed_spin), 90.0f, 0.0f);
	g_signal_connect_object (self->seed_spin, "value-changed", G_CALLBACK (on_seed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->seed_spin));
	g_object_unref (self->seed_spin);
	self->ground_check = lrg_checkbox_new ("Ground");
	lrg_widget_set_name (LRG_WIDGET (self->ground_check), "particles-ground");
	lrg_widget_set_valign (LRG_WIDGET (self->ground_check), LRG_ALIGN_CENTER);
	g_signal_connect_object (self->ground_check, "toggled", G_CALLBACK (on_ground), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->ground_check));
	g_object_unref (self->ground_check);
	self->gpu_check = lrg_checkbox_new ("GPU");
	lrg_widget_set_name (LRG_WIDGET (self->gpu_check), "particles-gpu");
	lrg_widget_set_valign (LRG_WIDGET (self->gpu_check), LRG_ALIGN_CENTER);
	g_signal_connect_object (self->gpu_check, "toggled", G_CALLBACK (on_gpu), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->gpu_check));
	g_object_unref (self->gpu_check);
	self->bg_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->bg_combo), "particles-background");
	lrg_widget_set_min_size (LRG_WIDGET (self->bg_combo), 96.0f, 0.0f);
	for (i = 0; i < G_N_ELEMENTS (BACKGROUNDS); i++)
		lrg_combo_box_append (self->bg_combo, BACKGROUNDS[i].label);
	lrg_combo_box_set_selected (self->bg_combo, 0);
	g_signal_connect_object (self->bg_combo, "changed", G_CALLBACK (on_background), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->bg_combo));
	g_object_unref (self->bg_combo);
	add_to (right, row);

	self->view = editor_fx_view_new (self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->view), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (self->view));
	g_object_unref (self->view);

	section = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (section), 2.0f);
	self->stats = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->stats), "particles-stats");
	lrg_container_add_child (LRG_CONTAINER (section), LRG_WIDGET (self->stats));
	g_object_unref (self->stats);
	self->gpu_label = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->gpu_label), "particles-gpu-status");
	lrg_widget_set_visible (LRG_WIDGET (self->gpu_label), FALSE);
	lrg_container_add_child (LRG_CONTAINER (section), LRG_WIDGET (self->gpu_label));
	g_object_unref (self->gpu_label);
	add_to (right, section);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "particles-split");
	lrg_splitter_set_start_child (outer, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_end_child (outer, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (outer, 0.40f);
	lrg_splitter_set_fraction (outer, 0.40f, FALSE);
	lrg_splitter_set_min_sizes (outer, 380.0f, 320.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	/* the model and the preview */
	load_asset (self);
	if (self->asset != NULL && lrg_particle_asset_get_n_emitters (self->asset) > 0)
		self->sel_emitter = 0;
	rebuild_effect (self, FALSE);
	rebuild_lists (self);
	rebuild_grid (self);
	update_stats (self);
	sync_preview_widgets (self);
	if (app != NULL)
		lrg_editor_particle_editor_install (app);
}

static void
lrg_editor_particle_editor_dispose (GObject *object)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (object);

	if (self->effect != NULL && IsWindowReady ())
		lrg_particle_effect_release_gpu (self->effect);
	g_clear_object (&self->effect);
	if (self->gpu_emitters != NULL)
		g_ptr_array_set_size (self->gpu_emitters, 0);
	set_asset (self, NULL);
	self->grid_object = NULL;
	G_OBJECT_CLASS (lrg_editor_particle_editor_parent_class)->dispose (object);
}

static void
lrg_editor_particle_editor_finalize (GObject *object)
{
	LrgEditorParticleEditor *self = LRG_EDITOR_PARTICLE_EDITOR (object);

	g_clear_pointer (&self->gpu_emitters, g_ptr_array_unref);
	g_clear_object (&self->manager);
	g_free (self->manager_root);
	g_free (self->load_error);
	g_free (self->lists_sig);
	g_free (self->grid_path);
	g_free (self->grid_hidden);
	G_OBJECT_CLASS (lrg_editor_particle_editor_parent_class)->finalize (object);
}

static void
lrg_editor_particle_editor_class_init (LrgEditorParticleEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_particle_editor_constructed;
	object_class->dispose = lrg_editor_particle_editor_dispose;
	object_class->finalize = lrg_editor_particle_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_particle_editor_step;
	data_class->refresh = particles_refresh;
	data_class->validate = particles_validate;
	data_class->reveal = particles_reveal;
	data_class->get_state = particles_get_state;
}

/* The free function of gpu_emitters: its slots may be NULL. */
static void
unref_nullable (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

static void
lrg_editor_particle_editor_init (LrgEditorParticleEditor *self)
{
	self->sel_emitter = SEL_EFFECT;
	self->sel_module = SEL_EMITTER;
	self->playing = TRUE;
	self->speed = 1.0f;
	self->seed_override = -1;
	self->show_ground = TRUE;
	self->background.r = BACKGROUNDS[0].r;
	self->background.g = BACKGROUNDS[0].g;
	self->background.b = BACKGROUNDS[0].b;
	self->background.a = 255;
	self->solo = -1;
	self->gpu_emitters = g_ptr_array_new_with_free_func (unref_nullable);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorParticleEditor *
lrg_editor_particle_editor_new (LrgEditorApp      *app,
                                LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_PARTICLE_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorParticleEditor *
lrg_editor_particle_editor_open (LrgEditorApp  *app,
                                 const gchar   *path,
                                 GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_PARTICLE_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a particle effect", path);
		return NULL;
	}
	return LRG_EDITOR_PARTICLE_EDITOR (e);
}

gboolean
lrg_editor_particle_editor_create (LrgEditorApp  *app,
                                   const gchar   *path,
                                   const gchar   *template_name,
                                   GError       **error)
{
	g_autofree gchar            *abs = NULL;
	g_autofree gchar            *dir = NULL;
	g_autofree gchar            *base = NULL;
	g_autofree gchar            *text = NULL;
	g_autoptr(LrgParticleAsset)  asset = NULL;
	gchar                       *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (template_name == NULL || g_strcmp0 (template_name, "default") == 0)
		asset = lrg_particle_asset_new_from_data (DEFAULT_EFFECT, -1, "default.rparticles", error);
	else if (g_strcmp0 (template_name, "empty") == 0)
		asset = lrg_particle_asset_new ();
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown template '%s' (default, empty)", template_name);
		return FALSE;
	}
	if (asset == NULL)
		return FALSE;
	abs = abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	base = g_path_get_basename (abs);
	dot = strrchr (base, '.');
	if (dot != NULL)
		*dot = '\0';
	lrg_particle_asset_set_name (asset, base);
	text = lrg_particle_asset_save_to_data (asset, NULL, error);
	return text != NULL && g_file_set_contents (abs, text, -1, error);
}

LrgParticleAsset *
lrg_editor_particle_editor_get_asset (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), NULL);
	return self->asset;
}

const gchar *
lrg_editor_particle_editor_get_load_error (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), NULL);
	return self->load_error;
}

LrgParticleEffect *
lrg_editor_particle_editor_get_effect (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), NULL);
	return self->effect;
}

gboolean
lrg_editor_particle_editor_select (LrgEditorParticleEditor *self,
                                   gint                     emitter,
                                   gint                     module)
{
	LrgParticleAssetEmitter *e;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	if (emitter < 0)
	{
		emitter = SEL_EFFECT;
		module = SEL_EMITTER;
	}
	else
	{
		if (self->asset == NULL || (guint) emitter >= lrg_particle_asset_get_n_emitters (self->asset))
			return FALSE;
		e = lrg_particle_asset_get_emitter (self->asset, (guint) emitter);
		if (module < SEL_RENDERER || (module >= 0 && (guint) module >= lrg_particle_asset_emitter_get_n_modules (e)))
			return FALSE;
	}
	if (self->sel_emitter == emitter && self->sel_module == module)
		return TRUE;
	self->sel_emitter = emitter;
	self->sel_module = module;
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return TRUE;
}

gint
lrg_editor_particle_editor_get_selected_emitter (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);
	return self->sel_emitter;
}

gint
lrg_editor_particle_editor_get_selected_module (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);
	return self->sel_module;
}

/* After a structural commit: what to select once the document re-parsed. */
static void
select_after (LrgEditorParticleEditor *self,
              gint                     emitter,
              gint                     module)
{
	if (!lrg_editor_particle_editor_select (self, emitter, module))
		lrg_editor_particle_editor_select (self, SEL_EFFECT, SEL_EMITTER);
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
}

gint
lrg_editor_particle_editor_add_emitter (LrgEditorParticleEditor  *self,
                                        const gchar              *name,
                                        GError                  **error)
{
	g_autoptr(LrgParticleAsset)        copy = NULL;
	g_autoptr(LrgParticleAssetEmitter) e = NULL;
	g_autofree gchar                  *unique = NULL;
	LrgParticleModule                 *m;
	guint                              index;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);

	copy = edit_copy (self, error);
	if (copy == NULL)
		return -1;
	if (name != NULL && (*name == '\0' || lrg_particle_asset_find_emitter (copy, name) != NULL))
	{
		g_set_error (error, G_IO_ERROR, *name == '\0' ? G_IO_ERROR_INVALID_ARGUMENT : G_IO_ERROR_EXISTS,
		             *name == '\0' ? "An emitter needs a name" : "There is already an emitter '%s'", name);
		return -1;
	}
	unique = name != NULL ? g_strdup (name) : unique_emitter_name (copy, "emitter");
	e = lrg_particle_asset_emitter_new (unique);
	m = lrg_particle_asset_emitter_add_module (e, "spawn-rate");
	(void) m;
	lrg_particle_asset_emitter_add_module (e, "velocity");
	lrg_particle_asset_emitter_add_module (e, "lifetime");
	lrg_particle_asset_add_emitter (copy, e);
	index = lrg_particle_asset_get_n_emitters (copy) - 1;
	{
		g_autofree gchar *label = g_strdup_printf ("Add Emitter %s", unique);

		if (!commit_from (self, copy, "/emitters", label, 0, error))
			return -1;
	}
	select_after (self, (gint) index, SEL_EMITTER);
	return (gint) index;
}

gint
lrg_editor_particle_editor_duplicate_emitter (LrgEditorParticleEditor  *self,
                                              guint                     emitter,
                                              GError                  **error)
{
	g_autoptr(LrgParticleAsset)  copy = NULL;
	g_autoptr(LrgParticleAsset)  twin = NULL;
	LrgParticleAssetEmitter     *src, *dup;
	g_autofree gchar            *base = NULL;
	g_autofree gchar            *unique = NULL;
	g_autofree gchar            *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);

	copy = edit_copy (self, error);
	if (copy == NULL || (src = copy_emitter (copy, emitter, error)) == NULL)
		return -1;
	/* a second parse gives a deep copy of the emitter, unknown keys too */
	twin = parse_doc (self, error);
	if (twin == NULL)
		return -1;
	dup = g_object_ref (lrg_particle_asset_get_emitter (twin, emitter));
	lrg_particle_asset_remove_emitter (twin, emitter);
	base = g_strdup_printf ("%s copy", lrg_particle_asset_emitter_get_name (src));
	unique = unique_emitter_name (copy, base);
	lrg_particle_asset_emitter_set_name (dup, unique);
	lrg_particle_asset_add_emitter (copy, dup);
	g_object_unref (dup);
	lrg_particle_asset_move_emitter (copy, lrg_particle_asset_get_n_emitters (copy) - 1, emitter + 1);
	label = g_strdup_printf ("Duplicate Emitter %s", lrg_particle_asset_emitter_get_name (src));
	if (!commit_from (self, copy, "/emitters", label, 0, error))
		return -1;
	select_after (self, (gint) emitter + 1, SEL_EMITTER);
	return (gint) emitter + 1;
}

gboolean
lrg_editor_particle_editor_remove_emitter (LrgEditorParticleEditor  *self,
                                           guint                     emitter,
                                           GError                  **error)
{
	g_autoptr(LrgParticleAsset)  copy = NULL;
	LrgParticleAssetEmitter     *e;
	g_autofree gchar            *label = NULL;
	guint                        i;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	copy = edit_copy (self, error);
	if (copy == NULL || (e = copy_emitter (copy, emitter, error)) == NULL)
		return FALSE;
	for (i = 0; i < lrg_particle_asset_get_n_emitters (copy); i++)
		if (i != emitter && triggers (copy, i, emitter))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Emitter '%s' is triggered by '%s': remove that sub-emitter module first",
			             lrg_particle_asset_emitter_get_name (e),
			             lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (copy, i)));
			return FALSE;
		}
	label = g_strdup_printf ("Remove Emitter %s", lrg_particle_asset_emitter_get_name (e));
	lrg_particle_asset_remove_emitter (copy, emitter);
	if (!commit_from (self, copy, "/emitters", label, 0, error))
		return FALSE;
	if (self->solo == (gint) emitter)
		self->solo = -1;
	else if (self->solo > (gint) emitter)
		self->solo--;
	select_after (self, emitter > 0 ? (gint) emitter - 1 : (lrg_particle_asset_get_n_emitters (copy) > 0 ? 0 : -1),
	              SEL_EMITTER);
	return TRUE;
}

gboolean
lrg_editor_particle_editor_move_emitter (LrgEditorParticleEditor  *self,
                                         guint                     from,
                                         guint                     to,
                                         GError                  **error)
{
	g_autoptr(LrgParticleAsset) copy = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	copy = edit_copy (self, error);
	if (copy == NULL || copy_emitter (copy, from, error) == NULL)
		return FALSE;
	if (to >= lrg_particle_asset_get_n_emitters (copy))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No emitter %u", to);
		return FALSE;
	}
	if (from == to)
		return TRUE;
	lrg_particle_asset_move_emitter (copy, from, to);
	if (!commit_from (self, copy, "/emitters", "Move Emitter", 0, error))
		return FALSE;
	select_after (self, (gint) to, SEL_EMITTER);
	return TRUE;
}

gint
lrg_editor_particle_editor_add_module (LrgEditorParticleEditor  *self,
                                       guint                     emitter,
                                       const gchar              *kind,
                                       gint                      index,
                                       GError                  **error)
{
	g_autoptr(LrgParticleAsset)  copy = NULL;
	g_autoptr(LrgParticleModule) m = NULL;
	LrgParticleAssetEmitter     *e;
	g_autofree gchar            *path = NULL;
	g_autofree gchar            *label = NULL;
	guint                        n;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);
	g_return_val_if_fail (kind != NULL, -1);

	copy = edit_copy (self, error);
	if (copy == NULL || (e = copy_emitter (copy, emitter, error)) == NULL)
		return -1;
	m = lrg_particle_module_new (kind);
	if (m == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown module kind '%s'", kind);
		return -1;
	}
	n = lrg_particle_asset_emitter_get_n_modules (e);
	if (index < 0 || (guint) index > n)
	{
		guint k;

		/* after the last module of the same or an earlier stage, so the
		 * file reads in stage order */
		index = 0;
		for (k = 0; k < n; k++)
			if (lrg_particle_module_get_stage (lrg_particle_asset_emitter_get_module (e, k)) <=
			    lrg_particle_module_get_stage (m))
				index = (gint) k + 1;
	}
	/* a sub-emitter needs a target to validate: the next emitter, if any */
	if (g_strcmp0 (kind, "sub-emitter") == 0)
	{
		guint i;

		for (i = 0; i < lrg_particle_asset_get_n_emitters (copy); i++)
			if (i != emitter)
			{
				g_object_set (m, "emitter",
				              lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (copy, i)), NULL);
				break;
			}
	}
	lrg_particle_asset_emitter_insert_module (e, m, index);
	if (!lrg_particle_asset_validate (copy, error))
		return -1;
	path = g_strdup_printf ("/emitters/%u/modules", emitter);
	label = g_strdup_printf ("Add Module %s", kind);
	if (!commit_from (self, copy, path, label, 0, error))
		return -1;
	select_after (self, (gint) emitter, index);
	return index;
}

gboolean
lrg_editor_particle_editor_remove_module (LrgEditorParticleEditor  *self,
                                          guint                     emitter,
                                          guint                     module,
                                          GError                  **error)
{
	g_autoptr(LrgParticleAsset)  copy = NULL;
	LrgParticleAssetEmitter     *e;
	g_autofree gchar            *path = NULL;
	g_autofree gchar            *label = NULL;
	guint                        n;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	copy = edit_copy (self, error);
	if (copy == NULL || (e = copy_emitter (copy, emitter, error)) == NULL)
		return FALSE;
	n = lrg_particle_asset_emitter_get_n_modules (e);
	if (module >= n)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Emitter %u has no module %u", emitter, module);
		return FALSE;
	}
	label = g_strdup_printf ("Remove Module %s",
	                         lrg_particle_module_get_kind (lrg_particle_asset_emitter_get_module (e, module)));
	lrg_particle_asset_emitter_remove_module (e, module);
	path = g_strdup_printf ("/emitters/%u/modules", emitter);
	if (!commit_from (self, copy, path, label, 0, error))
		return FALSE;
	select_after (self, (gint) emitter, n > 1 ? (gint) MIN (module, n - 2) : SEL_EMITTER);
	return TRUE;
}

gboolean
lrg_editor_particle_editor_move_module (LrgEditorParticleEditor  *self,
                                        guint                     emitter,
                                        guint                     from,
                                        guint                     to,
                                        GError                  **error)
{
	g_autoptr(LrgParticleAsset)  copy = NULL;
	LrgParticleAssetEmitter     *e;
	g_autofree gchar            *path = NULL;
	guint                        n;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	copy = edit_copy (self, error);
	if (copy == NULL || (e = copy_emitter (copy, emitter, error)) == NULL)
		return FALSE;
	n = lrg_particle_asset_emitter_get_n_modules (e);
	if (from >= n || to >= n)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Emitter %u has %u modules", emitter, n);
		return FALSE;
	}
	if (from == to)
		return TRUE;
	lrg_particle_asset_emitter_move_module (e, from, to);
	path = g_strdup_printf ("/emitters/%u/modules", emitter);
	if (!commit_from (self, copy, path, "Move Module", 0, error))
		return FALSE;
	select_after (self, (gint) emitter, (gint) to);
	return TRUE;
}

/* Whether @value would be misread as plain YAML (a comment, an alias, a
 * tag), so it needs single quotes. */
static gboolean
needs_quotes (const gchar *value)
{
	return value[0] == '#' || value[0] == '*' || value[0] == '&' || value[0] == '!' || value[0] == '%' ||
	       value[0] == '@' || value[0] == '`' || strstr (value, " #") != NULL || strstr (value, ": ") != NULL;
}

static gchar *
yaml_scalar (const gchar *value)
{
	GString     *s;
	const gchar *p;

	if (!needs_quotes (value))
		return g_strdup (value);
	s = g_string_new ("'");
	for (p = value; *p != '\0'; p++)
	{
		if (*p == '\'')
			g_string_append_c (s, '\'');
		g_string_append_c (s, *p);
	}
	g_string_append_c (s, '\'');
	return g_string_free (s, FALSE);
}

gboolean
lrg_editor_particle_editor_set_value (LrgEditorParticleEditor  *self,
                                      const gchar              *object_path,
                                      const gchar              *key,
                                      const gchar              *value,
                                      guint                     gesture,
                                      GError                  **error)
{
	g_autofree gchar             *text = NULL;
	g_autofree gchar             *key_path = NULL;
	g_autofree gchar             *yaml = NULL;
	g_autofree gchar             *ttext = NULL;
	g_autofree gchar             *source = NULL;
	g_autofree gchar             *label = NULL;
	g_autofree gchar             *commit_path = NULL;
	g_autoptr(LrgEditorDocument)  tdoc = NULL;
	g_autoptr(LrgParticleAsset)   parsed = NULL;
	g_autoptr(GError)             err = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);
	g_return_val_if_fail (object_path != NULL, FALSE);
	g_return_val_if_fail (key != NULL && *key != '\0', FALSE);

	if (self->asset == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
		             self->load_error != NULL ? self->load_error : "The effect does not load");
		return FALSE;
	}
	if (g_strcmp0 (key, "version") == 0 || g_strcmp0 (key, "type") == 0 || g_strcmp0 (key, "emitters") == 0 ||
	    g_strcmp0 (key, "modules") == 0 || g_strcmp0 (key, "renderer") == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "'%s' is structural: use the emitter and module edits", key);
		return FALSE;
	}

	/* an emitter rename: through the API on a fresh copy, carrying the
	 * sub-emitter references along (the loader would refuse the dangling
	 * reference of a text edit) */
	if (g_strcmp0 (key, "name") == 0 && g_str_has_prefix (object_path, "/emitters/") &&
	    strchr (object_path + 10, '/') == NULL)
	{
		g_autoptr(LrgParticleAsset) copy = edit_copy (self, error);
		LrgParticleAssetEmitter    *em;
		g_autofree gchar           *old_name = NULL;
		guint                       i, k;

		if (copy == NULL ||
		    (em = copy_emitter (copy, (guint) g_ascii_strtoull (object_path + 10, NULL, 10), error)) == NULL)
			return FALSE;
		if (value == NULL || *value == '\0')
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "An emitter needs a name");
			return FALSE;
		}
		old_name = g_strdup (lrg_particle_asset_emitter_get_name (em));
		if (g_strcmp0 (old_name, value) == 0)
			return TRUE;
		if (lrg_particle_asset_find_emitter (copy, value) != NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "There is already an emitter '%s'", value);
			return FALSE;
		}
		lrg_particle_asset_emitter_set_name (em, value);
		for (i = 0; i < lrg_particle_asset_get_n_emitters (copy); i++)
		{
			LrgParticleAssetEmitter *e2 = lrg_particle_asset_get_emitter (copy, i);

			for (k = 0; k < lrg_particle_asset_emitter_get_n_modules (e2); k++)
			{
				LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e2, k);
				g_autofree gchar  *target = NULL;

				if (g_strcmp0 (lrg_particle_module_get_kind (m), "sub-emitter") != 0)
					continue;
				g_object_get (m, "emitter", &target, NULL);
				if (g_strcmp0 (target, old_name) == 0)
					g_object_set (m, "emitter", value, NULL);
			}
		}
		if (!lrg_particle_asset_validate (copy, error))
			return FALSE;
		return commit_from (self, copy, "", "Rename Emitter", gesture, error);
	}

	/* the edit, on a scratch copy of the document text */
	text = lrg_editor_document_to_data (doc_of (self), NULL);
	tdoc = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, error);
	if (tdoc == NULL)
		return FALSE;
	if (*object_path != '\0' && !lrg_editor_document_has_path (tdoc, object_path))
	{
		g_autofree gchar *parent = path_parent (object_path);
		const gchar      *last = strrchr (object_path, '/');

		/* a missing renderer block is created; anything else is unknown */
		if (last == NULL || g_strcmp0 (last, "/renderer") != 0 || !lrg_editor_document_has_path (tdoc, parent))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No object at %s", object_path);
			return FALSE;
		}
		if (value == NULL)
			return TRUE;
		yaml = yaml_scalar (value);
		{
			g_autofree gchar *block = g_strdup_printf ("%s: %s", key, yaml);

			if (!lrg_editor_document_add_key (tdoc, parent, "renderer", block, -1, error))
				return FALSE;
		}
	}
	else
	{
		key_path = g_strdup_printf ("%s/%s", object_path, key);
		if (value == NULL)
		{
			if (!lrg_editor_document_has_path (tdoc, key_path))
				return TRUE;
			if (!lrg_editor_document_remove (tdoc, key_path, error))
				return FALSE;
		}
		else
		{
			yaml = yaml_scalar (value);
			if (lrg_editor_document_has_path (tdoc, key_path))
			{
				if (!lrg_editor_document_replace (tdoc, key_path, yaml, error))
					return FALSE;
			}
			else if (!lrg_editor_document_add_key (tdoc, object_path, key, yaml, -1, error))
				return FALSE;
		}
	}

	/* the strict loader decides */
	ttext = lrg_editor_document_to_data (tdoc, NULL);
	source = doc_source (self);
	parsed = lrg_particle_asset_new_from_data (ttext, -1, source, &err);
	if (parsed == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
		return FALSE;
	}

	/* the loader keeps keys it does not know (typos survive a save); a
	 * scripted edit of one is a mistake */
	if (value != NULL)
	{
		g_auto(GStrv)     unknown = lrg_particle_asset_get_unknown_keys (parsed);
		g_autofree gchar *probe = g_strdup_printf ("%s/%s", object_path, key);

		if (unknown != NULL && g_strv_contains ((const gchar * const *) unknown, probe))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s: not a key of this object", probe);
			return FALSE;
		}
	}

	if (!lrg_particle_asset_validate (parsed, &err))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
		return FALSE;
	}
	label = g_strdup_printf ("Set %s", key);
	commit_path = g_strdup (object_path);
	return commit_from (self, parsed, commit_path, label, gesture, error);
}

/* --- preview --- */

void
lrg_editor_particle_editor_set_playing (LrgEditorParticleEditor *self,
                                        gboolean                 playing)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	self->playing = playing;
	if (self->effect == NULL)
		return;
	if (playing)
	{
		if (!lrg_particle_effect_is_alive (self->effect) && lrg_particle_effect_get_time (self->effect) > 0.0f &&
		    !lrg_particle_asset_get_loop (self->asset))
		{
			lrg_particle_effect_restart (self->effect);
			self->gpu_dirty = TRUE;
		}
		else
			lrg_particle_effect_play (self->effect);
	}
	else
		lrg_particle_effect_pause (self->effect);
	self->finished_for = 0.0f;
}

gboolean
lrg_editor_particle_editor_get_playing (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);
	return self->playing;
}

void
lrg_editor_particle_editor_restart (LrgEditorParticleEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	self->playing = TRUE;
	if (self->effect == NULL)
		return;
	lrg_particle_effect_restart (self->effect);
	self->finished_for = 0.0f;
	self->gpu_dirty = TRUE;
}

void
lrg_editor_particle_editor_seek (LrgEditorParticleEditor *self,
                                 gfloat                   time)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	if (self->effect == NULL)
		return;
	time = CLAMP (time, 0.0f, MAX_SEEK);
	lrg_particle_effect_seek (self->effect, time, PREVIEW_STEP);
	if (!self->playing)
		lrg_particle_effect_pause (self->effect);
	self->finished_for = 0.0f;
	self->gpu_dirty = TRUE;
	if (self->gpu)
		build_gpu (self);
}

gfloat
lrg_editor_particle_editor_get_time (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), 0.0f);
	return self->effect != NULL ? lrg_particle_effect_get_time (self->effect) : 0.0f;
}

void
lrg_editor_particle_editor_advance (LrgEditorParticleEditor *self,
                                    gfloat                   seconds)
{
	gfloat t;

	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	for (t = 0.0f; t + PREVIEW_STEP * 0.5f < seconds; t += PREVIEW_STEP)
		preview_step (self, PREVIEW_STEP * self->speed);
}

void
lrg_editor_particle_editor_set_speed (LrgEditorParticleEditor *self,
                                      gfloat                   speed)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));
	self->speed = CLAMP (speed, MIN_SPEED, MAX_SPEED);
}

gfloat
lrg_editor_particle_editor_get_speed (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), 1.0f);
	return self->speed;
}

void
lrg_editor_particle_editor_set_seed_override (LrgEditorParticleEditor *self,
                                              gint64                   seed)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	self->seed_override = seed < 0 ? -1 : seed;
	if (self->effect == NULL)
		return;
	lrg_particle_effect_set_seed (self->effect, self->seed_override >= 0 ? (guint64) self->seed_override
	                                                                     : lrg_particle_asset_get_seed (self->asset));
	lrg_editor_particle_editor_restart (self);
}

gint64
lrg_editor_particle_editor_get_seed_override (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);
	return self->seed_override;
}

void
lrg_editor_particle_editor_set_show_ground (LrgEditorParticleEditor *self,
                                            gboolean                 show)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));
	self->show_ground = show;
}

gboolean
lrg_editor_particle_editor_get_show_ground (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);
	return self->show_ground;
}

void
lrg_editor_particle_editor_set_background (LrgEditorParticleEditor *self,
                                           const GrlColor          *color)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));
	g_return_if_fail (color != NULL);

	self->background = *color;
	self->background.a = 255;
}

void
lrg_editor_particle_editor_get_background (LrgEditorParticleEditor *self,
                                           GrlColor                *color)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));
	g_return_if_fail (color != NULL);
	*color = self->background;
}

gboolean
lrg_editor_particle_editor_set_solo (LrgEditorParticleEditor *self,
                                     gint                     emitter)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	if (emitter >= 0 && (self->asset == NULL || (guint) emitter >= lrg_particle_asset_get_n_emitters (self->asset)))
		return FALSE;
	self->solo = emitter < 0 ? -1 : emitter;
	self->lists_stale = TRUE;
	return TRUE;
}

gint
lrg_editor_particle_editor_get_solo (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), -1);
	return self->solo;
}

void
lrg_editor_particle_editor_set_gpu (LrgEditorParticleEditor *self,
                                    gboolean                 gpu)
{
	g_return_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self));

	if (self->gpu == gpu)
		return;
	self->gpu = gpu;
	if (gpu)
		build_gpu (self);
	else
		clear_gpu (self);
	self->lists_stale = TRUE;
	self->stats_clock = STATS_INTERVAL;
}

gboolean
lrg_editor_particle_editor_get_gpu (LrgEditorParticleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);
	return self->gpu;
}

gboolean
lrg_editor_particle_editor_get_gpu_status (LrgEditorParticleEditor  *self,
                                           guint                     emitter,
                                           gchar                   **message)
{
	LrgParticleAssetEmitter *e;
	g_autoptr(GError)        error = NULL;
	gboolean                 ok;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	if (message != NULL)
		*message = NULL;
	if (self->asset == NULL || (e = lrg_particle_asset_get_emitter (self->asset, emitter)) == NULL)
	{
		if (message != NULL)
			*message = g_strdup ("no such emitter");
		return FALSE;
	}
	if (!lrg_particle_asset_emitter_get_enabled (e))
	{
		if (message != NULL)
			*message = g_strdup ("the emitter is muted");
		return FALSE;
	}
	ok = lrg_gpu_particles_check_emitter (self->asset, e, &error);
	if (!ok && message != NULL)
		*message = g_strdup (error != NULL ? error->message : "outside the GPU subset");
	return ok;
}

gboolean
lrg_editor_particle_editor_get_emitter_on_gpu (LrgEditorParticleEditor *self,
                                               guint                    emitter)
{
	LrgGpuParticles *gp;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), FALSE);

	if (self->gpu && self->gpu_dirty)
		build_gpu (self);
	gp = gpu_of (self, emitter);
	return gp != NULL && lrg_gpu_particles_get_backend (gp) == LRG_PARTICLE_BACKEND_GPU;
}

guint
lrg_editor_particle_editor_get_active_count (LrgEditorParticleEditor *self,
                                             gint                     emitter)
{
	guint i, n, sum = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), 0);

	if (self->effect == NULL)
		return 0;
	n = lrg_particle_effect_get_n_systems (self->effect);
	for (i = 0; i < n; i++)
	{
		LrgGpuParticles *gp;

		if (emitter >= 0 && (guint) emitter != i)
			continue;
		gp = gpu_of (self, i);
		if (gp != NULL)
			sum += lrg_gpu_particles_count_alive (gp);
		else
			sum += lrg_particle_system_get_active_count (lrg_particle_effect_get_system (self->effect, i));
	}
	return sum;
}

GrlImage *
lrg_editor_particle_editor_render_preview (LrgEditorParticleEditor *self,
                                           guint                    size)
{
	RenderTexture2D rt;
	Image           img;
	GrlImage       *out;

	g_return_val_if_fail (LRG_IS_EDITOR_PARTICLE_EDITOR (self), NULL);

	if (size == 0 || !IsWindowReady () || self->view == NULL)
		return NULL;
	if (self->gpu && self->gpu_dirty)
		build_gpu (self);
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (rt.id == 0)
		return NULL;
	BeginTextureMode (rt);
	draw_scene (self, self->view, (gfloat) size, (gfloat) size, TRUE);
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	UnloadRenderTexture (rt);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	out = grl_image_new_from_pixels (img.width, img.height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
	UnloadImage (img);
	return out;
}

/* ==========================================================================
 * IPC: particles.*
 * ========================================================================== */

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

/* A number (JSON int or double, or a numeric string). */
static gboolean
node_number (JsonNode *n,
             gdouble  *out)
{
	GType t;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	t = json_node_get_value_type (n);
	if (t == G_TYPE_INT64 || t == G_TYPE_INT)
		*out = (gdouble) json_node_get_int (n);
	else if (t == G_TYPE_DOUBLE || t == G_TYPE_FLOAT)
		*out = json_node_get_double (n);
	else if (t == G_TYPE_STRING)
	{
		const gchar *s = json_node_get_string (n);
		gchar       *end = NULL;

		*out = g_ascii_strtod (s, &end);
		return end != s && *end == '\0';
	}
	else
		return FALSE;
	return TRUE;
}

static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	return params != NULL && json_object_has_member (params, name) &&
	       node_number (json_object_get_member (params, name), out);
}

static const gchar *
require_string (JsonObject   *params,
                const gchar  *name,
                GError      **error)
{
	const gchar *s = param_string (params, name);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
	                           g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_EXISTS)))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static JsonNode *
ok_node (void)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "ok", TRUE);
	return object_node (o);
}

/* A float as the double with the same shortest text. */
static gdouble
clean (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.7g", (gdouble) v);
	return g_ascii_strtod (buf, NULL);
}

/*
 * json_value_yaml:
 * The file-syntax YAML text of a JSON value: strings as written (quoted
 * when YAML would misread them), numbers, booleans, arrays of numbers as
 * a flow list. %NULL for JSON null (remove the key); @ok is FALSE for
 * anything else.
 */
static gchar *
json_value_yaml (JsonNode *n,
                 gboolean *ok)
{
	*ok = TRUE;
	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
		return NULL;
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		GString   *s = g_string_new ("[");
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
		{
			gdouble v;
			gchar   buf[G_ASCII_DTOSTR_BUF_SIZE];

			if (!node_number (json_array_get_element (a, i), &v))
			{
				*ok = FALSE;
				g_string_free (s, TRUE);
				return NULL;
			}
			g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", g_ascii_dtostr (buf, sizeof buf, v));
		}
		g_string_append_c (s, ']');
		return g_string_free (s, FALSE);
	}
	if (JSON_NODE_HOLDS_VALUE (n))
	{
		GType t = json_node_get_value_type (n);
		gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

		if (t == G_TYPE_BOOLEAN)
			return g_strdup (json_node_get_boolean (n) ? "true" : "false");
		if (t == G_TYPE_INT64 || t == G_TYPE_INT)
			return g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (n));
		if (t == G_TYPE_DOUBLE || t == G_TYPE_FLOAT)
			return g_strdup (g_ascii_dtostr (buf, sizeof buf, json_node_get_double (n)));
		if (t == G_TYPE_STRING)
			return g_strdup (json_node_get_string (n));
	}
	*ok = FALSE;
	return NULL;
}

static LrgEditorParticleEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar             *path = require_string (params, "path", error);
	LrgEditorParticleEditor *self;
	g_autoptr(GError)        err = NULL;

	if (path == NULL)
		return NULL;
	{
		g_autofree gchar *abs = abs_path (app, path);

		if (!g_file_test (abs, G_FILE_TEST_EXISTS))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No file %s", abs);
			return NULL;
		}
	}
	self = lrg_editor_particle_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

static gboolean
ipc_loaded (LrgEditorParticleEditor  *self,
            GError                  **error)
{
	if (self->asset != NULL)
		return TRUE;
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s",
	             self->load_error != NULL ? self->load_error : "The effect does not load");
	return FALSE;
}

/* "emitter" (or @member): an index or an emitter name. */
static gint
ipc_emitter (LrgEditorParticleEditor  *self,
             JsonObject               *params,
             const gchar              *member,
             GError                  **error)
{
	const gchar *name = param_string (params, member);
	gdouble      v;

	if (!ipc_loaded (self, error))
		return -1;
	if (name != NULL)
	{
		guint i;

		for (i = 0; i < lrg_particle_asset_get_n_emitters (self->asset); i++)
			if (g_strcmp0 (lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (self->asset, i)),
			               name) == 0)
				return (gint) i;
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No emitter '%s'", name);
		return -1;
	}
	if (!param_number (params, member, &v))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing parameter '%s' (an index or an emitter name)", member);
		return -1;
	}
	if (v < 0 || v >= lrg_particle_asset_get_n_emitters (self->asset))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No emitter %g", v);
		return -1;
	}
	return (gint) v;
}

static gint
ipc_index (JsonObject   *params,
           const gchar  *member,
           guint         count,
           GError      **error)
{
	gdouble v;

	if (!param_number (params, member, &v))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing number parameter '%s'", member);
		return -1;
	}
	if (v < 0 || v >= count)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "'%s' %g is out of range (%u)",
		             member, v, count);
		return -1;
	}
	return (gint) v;
}

static guint
n_modules (LrgEditorParticleEditor *self,
           gint                     emitter)
{
	return lrg_particle_asset_emitter_get_n_modules (lrg_particle_asset_get_emitter (self->asset, (guint) emitter));
}

/* The writable properties of @obj as {name: file-syntax text}. */
static JsonObject *
values_json (GObject *obj)
{
	JsonObject  *o = json_object_new ();
	GParamSpec **specs;
	guint        n, i;

	specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (obj), &n);
	for (i = 0; i < n; i++)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autofree gchar *text = NULL;

		/* the keys of the file are the readable and writable properties */
		if (!(specs[i]->flags & G_PARAM_READABLE) || !(specs[i]->flags & G_PARAM_WRITABLE))
			continue;
		g_value_init (&v, specs[i]->value_type);
		g_object_get_property (obj, specs[i]->name, &v);
		if (G_VALUE_HOLDS_OBJECT (&v) && g_value_get_object (&v) == NULL)
		{
			json_object_set_null_member (o, specs[i]->name);
			continue;
		}
		if (G_VALUE_HOLDS_BOXED (&v) && g_value_get_boxed (&v) == NULL)
		{
			json_object_set_null_member (o, specs[i]->name);
			continue;
		}
		if (G_VALUE_HOLDS_STRING (&v) && g_value_get_string (&v) == NULL)
		{
			json_object_set_null_member (o, specs[i]->name);
			continue;
		}
		text = lrg_property_value_to_string (&v);
		json_object_set_string_member (o, specs[i]->name, text != NULL ? text : "");
	}
	g_free (specs);
	return o;
}

static JsonObject *
asset_json (LrgEditorParticleEditor *self)
{
	JsonObject   *o = json_object_new ();
	JsonArray    *emitters = json_array_new ();
	JsonArray    *unknown = json_array_new ();
	JsonObject   *sel = json_object_new ();
	g_auto(GStrv) keys = NULL;
	guint         i, k;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	else
		json_object_set_null_member (o, "load_error");
	if (self->asset != NULL)
	{
		gboolean prewarm = FALSE, local = FALSE;

		g_object_get (self->asset, "prewarm", &prewarm, "local-space", &local, NULL);
		if (lrg_particle_asset_get_name (self->asset) != NULL)
			json_object_set_string_member (o, "name", lrg_particle_asset_get_name (self->asset));
		else
			json_object_set_null_member (o, "name");
		json_object_set_int_member (o, "seed", (gint64) lrg_particle_asset_get_seed (self->asset));
		json_object_set_double_member (o, "duration", clean (lrg_particle_asset_get_duration (self->asset)));
		json_object_set_boolean_member (o, "loop", lrg_particle_asset_get_loop (self->asset));
		json_object_set_boolean_member (o, "prewarm", prewarm);
		json_object_set_boolean_member (o, "local_space", local);
		for (i = 0; i < lrg_particle_asset_get_n_emitters (self->asset); i++)
		{
			LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
			JsonObject              *eo = json_object_new ();
			JsonArray               *mods = json_array_new ();
			JsonObject              *ro = values_json (G_OBJECT (lrg_particle_asset_emitter_get_renderer (e)));

			json_object_set_int_member (eo, "index", i);
			json_object_set_string_member (eo, "name", lrg_particle_asset_emitter_get_name (e));
			json_object_set_boolean_member (eo, "enabled", lrg_particle_asset_emitter_get_enabled (e));
			json_object_set_int_member (eo, "max_particles", lrg_particle_asset_emitter_get_max_particles (e));
			json_object_set_double_member (eo, "start_delay", clean (lrg_particle_asset_emitter_get_start_delay (e)));
			json_object_set_object_member (eo, "renderer", ro);
			for (k = 0; k < lrg_particle_asset_emitter_get_n_modules (e); k++)
			{
				LrgParticleModule *m = lrg_particle_asset_emitter_get_module (e, k);
				JsonObject        *mo = json_object_new ();
				guint              stage = (guint) lrg_particle_module_get_stage (m);

				json_object_set_int_member (mo, "index", k);
				json_object_set_string_member (mo, "kind", lrg_particle_module_get_kind (m));
				json_object_set_string_member (mo, "stage", stage_nick (stage));
				json_object_set_boolean_member (mo, "enabled", lrg_particle_module_get_enabled (m));
				json_object_set_boolean_member (mo, "gpu_capable", lrg_particle_module_get_gpu_capable (m));
				json_object_set_object_member (mo, "values", values_json (G_OBJECT (m)));
				json_array_add_object_element (mods, mo);
			}
			json_object_set_array_member (eo, "modules", mods);
			json_array_add_object_element (emitters, eo);
		}
		keys = lrg_particle_asset_get_unknown_keys (self->asset);
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			json_array_add_string_element (unknown, keys[i]);
	}
	json_object_set_array_member (o, "emitters", emitters);
	json_object_set_array_member (o, "unknown_keys", unknown);
	json_object_set_int_member (sel, "emitter", self->sel_emitter);
	json_object_set_int_member (sel, "module", self->sel_module);
	json_object_set_object_member (o, "selected", sel);
	return o;
}

static JsonObject *
preview_json (LrgEditorParticleEditor *self)
{
	JsonObject       *o = json_object_new ();
	g_autofree gchar *bg = g_strdup_printf ("#%02x%02x%02x", self->background.r, self->background.g,
	                                        self->background.b);

	json_object_set_double_member (o, "time", clean (lrg_editor_particle_editor_get_time (self)));
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_double_member (o, "speed", clean (self->speed));
	if (self->seed_override >= 0)
		json_object_set_int_member (o, "seed", self->seed_override);
	else
		json_object_set_null_member (o, "seed");
	json_object_set_boolean_member (o, "ground", self->show_ground);
	json_object_set_string_member (o, "background", bg);
	json_object_set_boolean_member (o, "gpu", self->gpu);
	json_object_set_int_member (o, "solo", self->solo);
	return o;
}

static JsonArray *
bounds_json (const gfloat *mn,
             const gfloat *mx)
{
	JsonArray *a = json_array_new ();
	guint      k;

	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, clean (mn[k]));
	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, clean (mx[k]));
	return a;
}

static JsonObject *
stats_json (LrgEditorParticleEditor *self)
{
	JsonObject *o = preview_json (self);
	JsonArray  *emitters = json_array_new ();
	gfloat      mn[3], mx[3];
	guint       i;

	if (self->gpu && self->gpu_dirty)
		build_gpu (self);
	json_object_set_int_member (o, "active", lrg_editor_particle_editor_get_active_count (self, -1));
	if (self->effect != NULL && lrg_particle_effect_get_bounds (self->effect, mn, mx))
		json_object_set_array_member (o, "bounds", bounds_json (mn, mx));
	else
		json_object_set_null_member (o, "bounds");
	for (i = 0; self->asset != NULL && i < lrg_particle_asset_get_n_emitters (self->asset); i++)
	{
		LrgParticleAssetEmitter *e = lrg_particle_asset_get_emitter (self->asset, i);
		JsonObject              *eo = json_object_new ();
		g_autofree gchar        *msg = NULL;
		gboolean                 ok = lrg_editor_particle_editor_get_gpu_status (self, i, &msg);
		LrgGpuParticles         *gp = gpu_of (self, i);

		json_object_set_string_member (eo, "name", lrg_particle_asset_emitter_get_name (e));
		json_object_set_boolean_member (eo, "enabled", lrg_particle_asset_emitter_get_enabled (e));
		json_object_set_int_member (eo, "active", lrg_editor_particle_editor_get_active_count (self, (gint) i));
		json_object_set_int_member (eo, "capacity", lrg_particle_asset_emitter_get_max_particles (e));
		if (self->effect != NULL && lrg_particle_effect_get_emitter_bounds (self->effect, i, mn, mx))
			json_object_set_array_member (eo, "bounds", bounds_json (mn, mx));
		else
			json_object_set_null_member (eo, "bounds");
		json_object_set_boolean_member (eo, "gpu_ok", ok);
		if (msg != NULL)
			json_object_set_string_member (eo, "gpu_message", msg);
		else
			json_object_set_null_member (eo, "gpu_message");
		json_object_set_string_member (eo, "simulation", gp == NULL ? "effect" : "gpu-emitter");
		json_object_set_string_member (eo, "backend", gp != NULL && lrg_gpu_particles_get_backend (gp) ==
		                                              LRG_PARTICLE_BACKEND_GPU ? "gpu" : "cpu");
		json_object_set_boolean_member (eo, "drawn", solo_shows (self, i));
		json_array_add_object_element (emitters, eo);
	}
	json_object_set_array_member (o, "emitters", emitters);
	return o;
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autoptr(GError)  err = NULL;
	g_autofree gchar  *abs = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	if (!lrg_editor_particle_editor_create (app, path, param_string (params, "template"), &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = abs_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (asset_json (self)) : NULL;
}

static JsonNode *
m_kinds (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	g_auto(GStrv) kinds = lrg_particle_module_list_kinds ();
	JsonObject   *o = json_object_new ();
	JsonArray    *a = json_array_new ();
	guint         i;

	for (i = 0; kinds != NULL && kinds[i] != NULL; i++)
	{
		g_autoptr(LrgParticleModule) m = lrg_particle_module_new (kinds[i]);
		JsonObject                  *k = json_object_new ();

		json_object_set_string_member (k, "kind", kinds[i]);
		json_object_set_string_member (k, "stage",
		                               m != NULL ? stage_nick ((guint) lrg_particle_module_get_stage (m)) : "unknown");
		json_object_set_boolean_member (k, "gpu_capable", m != NULL && lrg_particle_module_get_gpu_capable (m));
		json_array_add_object_element (a, k);
	}
	json_object_set_array_member (o, "kinds", a);
	return object_node (o);
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar             *key;
	g_autofree gchar        *object_path = NULL;
	g_autofree gchar        *value = NULL;
	g_autoptr(GError)        err = NULL;
	gboolean                 renderer = FALSE, ok;
	gint                     e = -1, m = -1;

	if (self == NULL || (key = require_string (params, "key", error)) == NULL || !ipc_loaded (self, error))
		return NULL;
	if (!json_object_has_member (params, "value"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing parameter 'value' (null resets the key)");
		return NULL;
	}
	value = json_value_yaml (json_object_get_member (params, "value"), &ok);
	if (!ok)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'value' must be a string in file syntax, a number, a boolean, an array of numbers or null");
		return NULL;
	}
	param_bool (params, "renderer", &renderer);
	if (json_object_has_member (params, "emitter"))
	{
		e = ipc_emitter (self, params, "emitter", error);
		if (e < 0)
			return NULL;
		if (json_object_has_member (params, "module"))
		{
			m = ipc_index (params, "module", n_modules (self, e), error);
			if (m < 0)
				return NULL;
			object_path = module_path ((guint) e, (guint) m);
		}
		else if (renderer)
			object_path = g_strdup_printf ("/emitters/%d/renderer", e);
		else
			object_path = emitter_path ((guint) e);
	}
	else
		object_path = g_strdup ("");
	if (!lrg_editor_particle_editor_set_value (self, object_path, key, value, 0, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_add_emitter (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gdouble                  to;
	gint                     index;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	index = lrg_editor_particle_editor_add_emitter (self, param_string (params, "name"), &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	if (param_number (params, "index", &to) && to >= 0 && (gint) to != index &&
	    lrg_editor_particle_editor_move_emitter (self, (guint) index, (guint) MIN ((gint) to, index), NULL))
		index = MIN ((gint) to, index);
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	json_object_set_string_member (o, "name", lrg_particle_asset_emitter_get_name (
	                                              lrg_particle_asset_get_emitter (self->asset, (guint) index)));
	return object_node (o);
}

static JsonNode *
m_remove_emitter (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gint                     e;

	if (self == NULL || (e = ipc_emitter (self, params, "emitter", error)) < 0)
		return NULL;
	if (!lrg_editor_particle_editor_remove_emitter (self, (guint) e, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_move_emitter (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gint                     from, to;

	if (self == NULL || (from = ipc_emitter (self, params, "from", error)) < 0 ||
	    (to = ipc_index (params, "to", lrg_particle_asset_get_n_emitters (self->asset), error)) < 0)
		return NULL;
	if (!lrg_editor_particle_editor_move_emitter (self, (guint) from, (guint) to, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_duplicate_emitter (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gint                     e, index;
	JsonObject              *o;

	if (self == NULL || (e = ipc_emitter (self, params, "emitter", error)) < 0)
		return NULL;
	index = lrg_editor_particle_editor_duplicate_emitter (self, (guint) e, &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	json_object_set_string_member (o, "name", lrg_particle_asset_emitter_get_name (
	                                              lrg_particle_asset_get_emitter (self->asset, (guint) index)));
	return object_node (o);
}

static JsonNode *
m_add_module (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	const gchar             *kind;
	gdouble                  at = -1.0;
	gint                     e, index;
	JsonObject              *o;

	if (self == NULL || (e = ipc_emitter (self, params, "emitter", error)) < 0 ||
	    (kind = require_string (params, "kind", error)) == NULL)
		return NULL;
	param_number (params, "index", &at);
	index = lrg_editor_particle_editor_add_module (self, (guint) e, kind, (gint) at, &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	return object_node (o);
}

static JsonNode *
m_remove_module (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gint                     e, m;

	if (self == NULL || (e = ipc_emitter (self, params, "emitter", error)) < 0 ||
	    (m = ipc_index (params, "module", n_modules (self, e), error)) < 0)
		return NULL;
	if (!lrg_editor_particle_editor_remove_module (self, (guint) e, (guint) m, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_move_module (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	gint                     e, from, to;

	if (self == NULL || (e = ipc_emitter (self, params, "emitter", error)) < 0 ||
	    (from = ipc_index (params, "from", n_modules (self, e), error)) < 0 ||
	    (to = ipc_index (params, "to", n_modules (self, e), error)) < 0)
		return NULL;
	if (!lrg_editor_particle_editor_move_module (self, (guint) e, (guint) from, (guint) to, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gdouble                  v;
	gint                     e = SEL_EFFECT, m = SEL_EMITTER;
	gboolean                 renderer = FALSE;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	if (json_object_has_member (params, "emitter") && !JSON_NODE_HOLDS_NULL (json_object_get_member (params,
	                                                                                                "emitter")))
	{
		if (!(param_number (params, "emitter", &v) && v < 0) &&
		    (e = ipc_emitter (self, params, "emitter", error)) < 0)
			return NULL;
		if (param_number (params, "module", &v))
			m = (gint) v;
		if (param_bool (params, "renderer", &renderer) && renderer)
			m = SEL_RENDERER;
	}
	if (!lrg_editor_particle_editor_select (self, e, m))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No module %d", m);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "emitter", self->sel_emitter);
	json_object_set_int_member (o, "module", self->sel_module);
	return object_node (o);
}

static JsonNode *
m_solo (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gint                     e = -1;
	gdouble                  v;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	if (json_object_has_member (params, "emitter") &&
	    !JSON_NODE_HOLDS_NULL (json_object_get_member (params, "emitter")) &&
	    !(param_number (params, "emitter", &v) && v < 0))
	{
		e = ipc_emitter (self, params, "emitter", error);
		if (e < 0)
			return NULL;
	}
	lrg_editor_particle_editor_set_solo (self, e);
	o = json_object_new ();
	json_object_set_int_member (o, "solo", self->solo);
	return object_node (o);
}

static JsonNode *
m_play (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gboolean                 playing = TRUE;

	if (self == NULL)
		return NULL;
	param_bool (params, "playing", &playing);
	lrg_editor_particle_editor_set_playing (self, playing);
	return object_node (preview_json (self));
}

static JsonNode *
m_restart (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	if (self == NULL)
		return NULL;
	lrg_editor_particle_editor_restart (self);
	return object_node (preview_json (self));
}

static JsonNode *
m_seek (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gdouble                  t;

	if (self == NULL)
		return NULL;
	if (!param_number (params, "time", &t) || t < 0.0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'time' must be a number of seconds >= 0");
		return NULL;
	}
	lrg_editor_particle_editor_seek (self, (gfloat) t);
	return object_node (stats_json (self));
}

static gboolean
parse_hex_color (const gchar *s,
                 GrlColor    *out)
{
	guint  r, g, b;
	gsize  len;

	if (s == NULL || s[0] != '#')
		return FALSE;
	len = strlen (s);
	if (len != 7 && len != 9)
		return FALSE;
	if (sscanf (s + 1, "%02x%02x%02x", &r, &g, &b) != 3)
		return FALSE;
	out->r = (guint8) r;
	out->g = (guint8) g;
	out->b = (guint8) b;
	out->a = 255;
	return TRUE;
}

static JsonNode *
m_preview (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gdouble                  v;
	gboolean                 b;
	const gchar             *bg;

	if (self == NULL)
		return NULL;
	bg = param_string (params, "background");
	if (bg != NULL)
	{
		GrlColor c;

		if (!parse_hex_color (bg, &c))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "'background' must be \"#rrggbb\"");
			return NULL;
		}
		lrg_editor_particle_editor_set_background (self, &c);
	}
	if (param_number (params, "speed", &v))
		lrg_editor_particle_editor_set_speed (self, (gfloat) v);
	if (json_object_has_member (params, "seed"))
	{
		if (JSON_NODE_HOLDS_NULL (json_object_get_member (params, "seed")))
			lrg_editor_particle_editor_set_seed_override (self, -1);
		else if (param_number (params, "seed", &v))
			lrg_editor_particle_editor_set_seed_override (self, v < 0 ? -1 : (gint64) v);
	}
	if (param_bool (params, "ground", &b))
		lrg_editor_particle_editor_set_show_ground (self, b);
	if (param_bool (params, "gpu", &b))
		lrg_editor_particle_editor_set_gpu (self, b);
	return object_node (preview_json (self));
}

static JsonNode *
m_stats (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (stats_json (self)) : NULL;
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorParticleEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_render (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	LrgEditorParticleEditor *self = ipc_editor (app, params, error);
	const gchar             *file;
	g_autoptr(GrlImage)      image = NULL;
	g_autofree gchar        *abs = NULL;
	gdouble                  size = 512.0;
	JsonObject              *o;

	if (self == NULL || (file = require_string (params, "file", error)) == NULL)
		return NULL;
	param_number (params, "size", &size);
	image = lrg_editor_particle_editor_render_preview (self, (guint) CLAMP (size, 16.0, 4096.0));
	if (image == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "No graphics context to render the preview");
		return NULL;
	}
	abs = abs_path (app, file);
	if (!grl_image_export (image, abs))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "Could not write %s", abs);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "file", abs);
	json_object_set_int_member (o, "width", grl_image_get_width (image));
	json_object_set_int_member (o, "height", grl_image_get_height (image));
	return object_node (o);
}

void
lrg_editor_particle_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "particles.open", m_open },
		{ "particles.create", m_create },
		{ "particles.get", m_get },
		{ "particles.kinds", m_kinds },
		{ "particles.set", m_set },
		{ "particles.add_emitter", m_add_emitter },
		{ "particles.remove_emitter", m_remove_emitter },
		{ "particles.move_emitter", m_move_emitter },
		{ "particles.duplicate_emitter", m_duplicate_emitter },
		{ "particles.add_module", m_add_module },
		{ "particles.remove_module", m_remove_module },
		{ "particles.move_module", m_move_module },
		{ "particles.select", m_select },
		{ "particles.solo", m_solo },
		{ "particles.play", m_play },
		{ "particles.restart", m_restart },
		{ "particles.seek", m_seek },
		{ "particles.preview", m_preview },
		{ "particles.stats", m_stats },
		{ "particles.save", m_save },
		{ "particles.render", m_render }
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALLED_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALLED_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
