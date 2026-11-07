/* lrg-editor-weather-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWeatherEditor: the `.rweather` data editor (see the header).
 *
 * Layout:
 *
 *   +-----------------------------------+--------------------------------+
 *   | (one scrolling column)            | Show [storm v]  Transition to  |
 *   | [File Settings]                   |  [clear v] [Go]  fading 8.0 s  |
 *   | PRESETS  2   [id] [Add] [Remove]  | +----------------------------+ |
 *   |  [clear]  Clear sky   1 layer     | |  ground, pillars, the sky  | |
 *   |  [storm]  Thunderstorm 5 layers   | |  tinted by the ambient     | |
 *   | LAYERS  storm  [kind v] [Add]     | |  light, rain, snow, bolts, | |
 *   |  [x] rain (rain)  ===o== [^][v][x]| |  fog veils, flashes        | |
 *   |  [x] fog (fog)    =====o [^][v][x]| +----------------------------+ |
 *   | PROPERTIES  rain                  | wind 6.0, 0.0                  |
 *   |  property grid (kind schema)      |                                |
 *   | TRANSITIONS                       |                                |
 *   |  clear -> storm   [8]  [x]        |                                |
 *   |  [from v] [to v] [5] [Add]        |                                |
 *   +-----------------------------------+--------------------------------+
 *
 * The document is the truth: its text is parsed with the engine loader
 * into an LrgWeatherAsset. Every edit changes that asset in place through
 * the LrgWeatherAsset API (which validates), writes the changed part back
 * in the writer's canonical form (a slider or grid drag merges into one
 * undo step) and syncs the preview (lrg_weather_asset_sync_weather() for
 * the edited preset, so rain keeps falling while a slider moves). A
 * document change from outside (undo, the YAML pane, a reload) re-parses
 * the asset and rebuilds the preview manager.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-weather-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../weather/lrg-weather.h"
#include "../../../core/lrg-color-gradient.h"
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

#define INSTALLED_KEY   "lrg-editor-weather-installed"
#define PREVIEW_STEP    (1.0f / 60.0f)
#define VIEW_FOV        (55.0f)
#define SLIDER_GESTURE  (0.4)          /* seconds between slider steps of one drag */

/* The default weather of lrg_editor_weather_editor_create() (the format
 * page's example, trimmed): canonicalised by the engine writer. */
static const gchar DEFAULT_WEATHER[] =
	"version: 1\n"
	"default-preset: clear\n"
	"default-transition: 6\n"
	"presets:\n"
	"- id: clear\n"
	"  name: Clear sky\n"
	"  ambient-color: '#fff4e0'\n"
	"  ambient-brightness: 1\n"
	"  layers:\n"
	"  - kind: wind\n"
	"    intensity: 0.3\n"
	"    params:\n"
	"      speed-x: 1.5\n"
	"- id: storm\n"
	"  name: Thunderstorm\n"
	"  ambient-color: '#5a5f6e'\n"
	"  ambient-brightness: 0.55\n"
	"  layers:\n"
	"  - kind: rain\n"
	"    intensity: 0.9\n"
	"    params:\n"
	"      space: world\n"
	"      drop-count: 4000\n"
	"      drop-speed: 12\n"
	"      volume-radius: 30\n"
	"  - kind: fog\n"
	"    intensity: 1\n"
	"    params:\n"
	"      fog-type: exponential\n"
	"      density: 0.03\n"
	"      color: '#5a5f69ff'\n"
	"      veil-opacity: 0.35\n"
	"  - kind: lightning\n"
	"    intensity: 1\n"
	"    params:\n"
	"      min-interval: 3\n"
	"      max-interval: 9\n"
	"      bolts-enabled: true\n"
	"  - kind: wind\n"
	"    id: gusts\n"
	"    intensity: 1\n"
	"    params:\n"
	"      speed-x: 6\n"
	"      gust-strength: 3\n"
	"      gust-frequency: 0.4\n"
	"transitions:\n"
	"- from: clear\n"
	"  to: storm\n"
	"  duration: 8\n"
	"- from: '*'\n"
	"  to: clear\n"
	"  duration: 12\n";

/* ==========================================================================
 * EditorWeatherSource: the grid's view of the settings, a preset or a layer
 * ========================================================================== */

typedef enum
{
	SOURCE_SETTINGS,
	SOURCE_PRESET,
	SOURCE_LAYER
} SourceMode;

#define EDITOR_TYPE_WEATHER_SOURCE (editor_weather_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorWeatherSource, editor_weather_source, EDITOR, WEATHER_SOURCE, GObject)

struct _EditorWeatherSource
{
	GObject                 parent_instance;

	LrgEditorWeatherEditor *editor;       /* weak */
	SourceMode              mode;
	guint                   preset;
	guint                   layer;
	GPtrArray              *specs;        /* GParamSpec, owned */
};

static void editor_weather_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorWeatherSource, editor_weather_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_weather_source_iface_init))

/* ==========================================================================
 * EditorWeatherView: the preview scene with a caption
 * ========================================================================== */

#define EDITOR_TYPE_WEATHER_VIEW (editor_weather_view_get_type ())
G_DECLARE_FINAL_TYPE (EditorWeatherView, editor_weather_view, EDITOR, WEATHER_VIEW, LrgContainer)

struct _EditorWeatherView
{
	LrgContainer            parent_instance;

	LrgEditorWeatherEditor *editor;       /* weak */
	LrgViewport            *viewport;
	LrgWidget              *caption;
	gfloat                  yaw;
	gfloat                  pitch;
	gfloat                  zoom;
	gboolean                dragging;
	gfloat                  last_x;
	gfloat                  last_y;
};

G_DEFINE_FINAL_TYPE (EditorWeatherView, editor_weather_view, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * LrgEditorWeatherEditor
 * ========================================================================== */

struct _LrgEditorWeatherEditor
{
	LrgEditorDataEditor  parent_instance;

	/* the model of the document */
	LrgWeatherAsset     *asset;
	gchar               *load_error;
	gint                 sel_preset;     /* -1: file settings */
	gint                 sel_layer;      /* -1: the preset */

	/* the preview */
	LrgWeatherManager   *manager;
	gchar               *preview_id;     /* the preset shown (or faded to) */
	gfloat               fade;           /* the running crossfade, seconds */
	gfloat               fade_left;

	/* widgets */
	EditorWeatherView   *view;
	LrgButton           *settings_button;
	LrgLabel            *presets_title;
	LrgWidget           *presets;
	LrgLabel            *layers_title;
	LrgWidget           *layers;
	LrgComboBox         *kind_combo;
	LrgLabel            *grid_title;
	LrgPropertyGrid     *grid;
	LrgWidget           *transitions;
	LrgComboBox         *from_combo;
	LrgComboBox         *to_combo;
	LrgSpinBox          *duration_spin;
	LrgComboBox         *show_combo;
	LrgComboBox         *goto_combo;
	LrgLabel            *status;

	/* bookkeeping */
	EditorWeatherSource *source;
	guint                gesture;        /* the grid's gesture, 0 outside one */
	gboolean             gesture_wrote;
	gboolean             syncing;
	gboolean             committing;
	gboolean             lists_stale;
	gboolean             grid_stale;
	gchar               *lists_sig;
	gint64               slider_time;
	gpointer             slider_last;
	guint                slider_gesture;
};

G_DEFINE_FINAL_TYPE (LrgEditorWeatherEditor, lrg_editor_weather_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_manager (LrgEditorWeatherEditor *self,
                             gboolean                pick_default);

static LrgEditorDocument *
doc_of (LrgEditorWeatherEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorWeatherEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

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

static gchar *
doc_base_dir (LrgEditorWeatherEditor *self)
{
	const gchar *path = lrg_editor_document_get_path (doc_of (self));

	return path != NULL ? g_path_get_dirname (path) : NULL;
}

static LrgWeatherAsset *
parse_doc (LrgEditorWeatherEditor  *self,
           GError                 **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar *dir = doc_base_dir (self);

	if (text == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The document has no text");
		return NULL;
	}
	return lrg_weather_asset_new_from_data (text, -1, dir, error);
}

static void
clamp_selection (LrgEditorWeatherEditor *self)
{
	guint n;

	if (self->asset == NULL)
	{
		self->sel_preset = -1;
		self->sel_layer = -1;
		return;
	}
	n = lrg_weather_asset_get_preset_count (self->asset);
	if (self->sel_preset >= (gint) n)
	{
		self->sel_preset = n > 0 ? (gint) n - 1 : -1;
		self->sel_layer = -1;
	}
	if (self->sel_preset >= 0 &&
	    self->sel_layer >= (gint) lrg_weather_asset_get_layer_count (self->asset, (guint) self->sel_preset))
		self->sel_layer = -1;
}

static void
load_asset (LrgEditorWeatherEditor *self)
{
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->asset);
	g_clear_pointer (&self->load_error, g_free);
	self->asset = parse_doc (self, &error);
	if (self->asset == NULL)
		self->load_error = g_strdup (error != NULL ? error->message : "The document does not load");
	clamp_selection (self);
}

/*
 * commit:
 * Writes self->asset (edited in place) back at @path in the writer's
 * canonical form, or at the nearest common ancestor when the subtree is
 * missing on either side. The document change does not re-parse the
 * asset. On failure the asset is re-parsed from the document.
 */
static gboolean
commit (LrgEditorWeatherEditor  *self,
        const gchar             *path,
        const gchar             *label,
        guint                    gesture,
        GError                 **error)
{
	LrgEditorDocument            *doc = doc_of (self);
	g_autofree gchar             *text = NULL;
	g_autoptr(LrgEditorDocument)  tdoc = NULL;
	g_autofree gchar             *p = g_strdup (path != NULL ? path : "");
	g_autofree gchar             *sub = NULL;
	g_autofree gchar             *now = NULL;
	gboolean                      ok = TRUE;

	text = lrg_weather_asset_to_data (self->asset, error);
	if (text == NULL)
		goto failed;
	if (*p != '\0')
	{
		tdoc = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, error);
		if (tdoc == NULL)
			goto failed;
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
	self->committing = TRUE;
	if (gesture != 0)
		ok = lrg_editor_document_replace_gesture (doc, p, sub, gesture, error);
	else
	{
		lrg_editor_document_begin_group (doc, label);
		ok = lrg_editor_document_replace_gesture (doc, p, sub, 0, error);
		lrg_editor_document_end_group (doc);
	}
	self->committing = FALSE;
	if (ok)
		return TRUE;

failed:
	load_asset (self);
	rebuild_manager (self, FALSE);
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return FALSE;
}

static gboolean
need_asset (LrgEditorWeatherEditor  *self,
            GError                 **error)
{
	if (self->asset != NULL)
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
	             self->load_error != NULL ? self->load_error : "The weather file does not load");
	return FALSE;
}

static gboolean
need_preset (LrgEditorWeatherEditor  *self,
             guint                    preset,
             GError                 **error)
{
	if (!need_asset (self, error))
		return FALSE;
	if (preset < lrg_weather_asset_get_preset_count (self->asset))
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No preset %u", preset);
	return FALSE;
}

static gboolean
need_layer (LrgEditorWeatherEditor  *self,
            guint                    preset,
            guint                    layer,
            GError                 **error)
{
	if (!need_preset (self, preset, error))
		return FALSE;
	if (layer < lrg_weather_asset_get_layer_count (self->asset, preset))
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Preset %u has no layer %u", preset, layer);
	return FALSE;
}

static gchar *
preset_path (guint preset)
{
	return g_strdup_printf ("/presets/%u", preset);
}

/* ==========================================================================
 * The preview
 * ========================================================================== */

/* Re-syncs the live weather of @preset after an in-place edit. */
static void
sync_preset (LrgEditorWeatherEditor *self,
             guint                   preset)
{
	LrgWeather       *w;
	g_autoptr(GError) error = NULL;

	if (self->manager == NULL || self->asset == NULL || preset >= lrg_weather_asset_get_preset_count (self->asset))
		return;
	w = lrg_weather_manager_get_weather (self->manager, lrg_weather_asset_get_preset_id (self->asset, preset));
	if (w == NULL)
	{
		/* a new preset: apply registers it */
		if (!lrg_weather_asset_apply (self->asset, self->manager, &error))
			g_debug ("weather preview: %s", error->message);
		return;
	}
	if (!lrg_weather_asset_sync_weather (self->asset, preset, w, &error))
		g_debug ("weather preview: %s", error->message);
}

static void
show_now (LrgEditorWeatherEditor *self)
{
	if (self->manager == NULL)
		return;
	if (self->preview_id != NULL && self->asset != NULL &&
	    lrg_weather_asset_find_preset (self->asset, self->preview_id) >= 0)
		lrg_weather_manager_set_weather (self->manager, self->preview_id, 0.0f);
	else
	{
		g_clear_pointer (&self->preview_id, g_free);
		lrg_weather_manager_clear_weather (self->manager, 0.0f);
	}
	self->fade = 0.0f;
	self->fade_left = 0.0f;
}

/* A new manager with every preset; the preview shows the same preset
 * (with @pick_default and none yet: the default, else the first). */
static void
rebuild_manager (LrgEditorWeatherEditor *self,
                 gboolean                pick_default)
{
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->manager);
	if (self->asset == NULL)
		return;
	self->manager = lrg_weather_manager_new ();
	/* the preset's ambient light alone: no time of day in the preview */
	lrg_weather_manager_set_day_night_enabled (self->manager, FALSE);
	if (!lrg_weather_asset_apply (self->asset, self->manager, &error))
	{
		g_debug ("weather preview: %s", error->message);
		return;
	}
	if (pick_default && self->preview_id == NULL && lrg_weather_asset_get_default_preset (self->asset) != NULL)
		self->preview_id = g_strdup (lrg_weather_asset_get_default_preset (self->asset));
	if (pick_default && self->preview_id == NULL && lrg_weather_asset_get_preset_count (self->asset) > 0)
		self->preview_id = g_strdup (lrg_weather_asset_get_preset_id (self->asset, 0));
	show_now (self);
}

static void
preview_step (LrgEditorWeatherEditor *self,
              gfloat                  dt)
{
	if (self->manager == NULL)
		return;
	lrg_weather_manager_update (self->manager, dt);
	if (self->fade_left > 0.0f)
		self->fade_left = MAX (self->fade_left - dt, 0.0f);
}

/* The sky and ground colours from the combined ambient light. */
static void
scene_colors (LrgEditorWeatherEditor *self,
              Color                  *sky,
              Color                  *ground)
{
	guint8 r = 255, g = 255, b = 255;
	gfloat br = 1.0f;

	if (self->manager != NULL)
		lrg_weather_manager_get_combined_ambient (self->manager, &r, &g, &b, &br);
	br = CLAMP (br, 0.05f, 2.0f);
	sky->r = (guint8) CLAMP (128.0f * r / 255.0f * br + 10.0f, 0.0f, 255.0f);
	sky->g = (guint8) CLAMP (168.0f * g / 255.0f * br + 12.0f, 0.0f, 255.0f);
	sky->b = (guint8) CLAMP (214.0f * b / 255.0f * br + 18.0f, 0.0f, 255.0f);
	sky->a = 255;
	ground->r = (guint8) CLAMP (86.0f * r / 255.0f * br, 0.0f, 255.0f);
	ground->g = (guint8) CLAMP (104.0f * g / 255.0f * br, 0.0f, 255.0f);
	ground->b = (guint8) CLAMP (78.0f * b / 255.0f * br, 0.0f, 255.0f);
	ground->a = 255;
}

static void
draw_scene (LrgEditorWeatherEditor *self,
            EditorWeatherView      *view,
            gfloat                  width,
            gfloat                  height)
{
	Camera3D   cam;
	Color      sky, ground, stone;
	GrlVector3 eye;
	gfloat     dist = 16.0f * view->zoom;
	gint       i;

	scene_colors (self, &sky, &ground);
	ClearBackground (sky);
	memset (&cam, 0, sizeof cam);
	cam.target = (Vector3) { 0.0f, 2.0f, 0.0f };
	cam.position = (Vector3) { dist * cosf (view->pitch) * sinf (view->yaw), 2.0f + dist * sinf (view->pitch),
	                           dist * cosf (view->pitch) * cosf (view->yaw) };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = VIEW_FOV;
	cam.projection = CAMERA_PERSPECTIVE;
	eye.x = cam.position.x;
	eye.y = cam.position.y;
	eye.z = cam.position.z;
	stone = (Color) { (guint8) MIN (ground.r * 1.6f, 255.0f), (guint8) MIN (ground.g * 1.5f, 255.0f),
	                  (guint8) MIN (ground.b * 1.6f, 255.0f), 255 };

	BeginMode3D (cam);
	/* a small landscape: a ground plane, a ring of pillars, a house */
	DrawPlane ((Vector3) { 0, 0, 0 }, (Vector2) { 80, 80 }, ground);
	for (i = 0; i < 8; i++)
	{
		gfloat a = (gfloat) i * (gfloat) G_PI / 4.0f;
		gfloat h = 2.0f + (gfloat) (i % 3);

		DrawCube ((Vector3) { 9.0f * cosf (a), h * 0.5f, 9.0f * sinf (a) }, 0.8f, h, 0.8f, stone);
	}
	DrawCube ((Vector3) { 0, 1.2f, 0 }, 3.0f, 2.4f, 3.0f, stone);
	DrawCube ((Vector3) { 0, 2.9f, 0 }, 3.4f, 1.0f, 3.4f, (Color) { (guint8) (stone.r * 0.7f),
	                                                                 (guint8) (stone.g * 0.55f),
	                                                                 (guint8) (stone.b * 0.5f), 255 });
	rlDrawRenderBatchActive ();
	if (self->manager != NULL)
		lrg_weather_manager_render_world (self->manager, &eye);
	rlDrawRenderBatchActive ();
	EndMode3D ();
	/* screen-space effects, fog veils and lightning flashes */
	if (self->manager != NULL)
	{
		lrg_weather_manager_render (self->manager);
		lrg_weather_manager_render_overlay (self->manager, (gint) width, (gint) height);
	}
}

static void
view_render (LrgViewport      *viewport,
             GrlRenderTexture *target,
             gfloat            width,
             gfloat            height,
             gpointer          user_data)
{
	EditorWeatherView *view = EDITOR_WEATHER_VIEW (user_data);

	if (view->editor == NULL)
	{
		ClearBackground ((Color) { 20, 22, 30, 255 });
		return;
	}
	draw_scene (view->editor, view, width, height);
}

static void
view_caption_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	EditorWeatherView      *view = EDITOR_WEATHER_VIEW (user_data);
	LrgEditorWeatherEditor *self = view->editor;
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	const GrlColor          scrim = { 0, 0, 0, 120 };
	const GrlColor          ink = { 236, 238, 246, 255 };
	g_autofree gchar       *text = NULL;
	gfloat                  tw = 0.0f, wx = 0.0f, wy = 0.0f;
	LrgWeather             *active;

	if (self == NULL)
		return;
	if (self->asset == NULL)
		text = g_strdup ("The weather file does not load");
	else
	{
		active = self->manager != NULL ? lrg_weather_manager_get_active_weather (self->manager) : NULL;
		if (active != NULL)
			lrg_weather_get_effective_wind (active, &wx, &wy);
		if (self->fade_left > 0.0f)
			text = g_strdup_printf ("fading to %s  \xc2\xb7  %.1f / %.1f s  \xc2\xb7  wind %.1f, %.1f",
			                        self->preview_id != NULL ? self->preview_id : "none",
			                        self->fade - self->fade_left, self->fade, wx, wy);
		else
			text = g_strdup_printf ("%s  \xc2\xb7  wind %.1f, %.1f",
			                        self->preview_id != NULL ? self->preview_id : "no weather", wx, wy);
	}
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, NULL);
	tw = MIN (tw, width - 20.0f);
	lrg_paint_list_rect (list, x, y, tw + 20.0f, fs + 12.0f, (fs + 12.0f) * 0.5f, &scrim);
	lrg_paint_list_push_clip (list, x, y, tw + 20.0f, fs + 12.0f);
	lrg_paint_list_text (list, font, text, x + 10.0f, y + 6.0f, fs, 0.0f, &ink);
	lrg_paint_list_pop_clip (list);
}

static void
editor_weather_view_layout_children (LrgContainer *container)
{
	EditorWeatherView *view = EDITOR_WEATHER_VIEW (container);
	gfloat             w = lrg_widget_get_width (LRG_WIDGET (view));
	gfloat             h = lrg_widget_get_height (LRG_WIDGET (view));

	if (view->viewport == NULL || view->caption == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (view->viewport), 0.0f, 0.0f, w, h);
	lrg_widget_place_in_slot (view->caption, 10.0f, 10.0f, MAX (w - 20.0f, 0.0f), 28.0f);
}

static void
editor_weather_view_measure (LrgWidget *widget,
                             gfloat    *width,
                             gfloat    *height)
{
	*width = 280.0f;
	*height = 260.0f;
}

static gboolean
editor_weather_view_handle_event (LrgWidget        *widget,
                                  const LrgUIEvent *event)
{
	EditorWeatherView *view = EDITOR_WEATHER_VIEW (widget);
	gfloat             x = lrg_ui_event_get_x (event);
	gfloat             y = lrg_ui_event_get_y (event);

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
		view->yaw -= (x - view->last_x) * 0.010f;
		view->pitch = CLAMP (view->pitch + (y - view->last_y) * 0.008f, 0.02f, 1.3f);
		view->last_x = x;
		view->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!view->dragging)
			return FALSE;
		view->dragging = FALSE;
		return TRUE;
	case LRG_UI_EVENT_SCROLL:
		view->zoom = CLAMP (view->zoom * (lrg_ui_event_get_scroll_y (event) > 0.0f ? 0.9f : 1.1f), 0.3f, 3.0f);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
editor_weather_view_dispose (GObject *object)
{
	EditorWeatherView *view = EDITOR_WEATHER_VIEW (object);

	if (view->caption != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->caption), NULL, NULL, NULL);
	if (view->viewport != NULL)
		lrg_viewport_set_render_func (view->viewport, NULL, NULL, NULL);
	g_clear_weak_pointer (&view->editor);
	G_OBJECT_CLASS (editor_weather_view_parent_class)->dispose (object);
}

static void
editor_weather_view_class_init (EditorWeatherViewClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = editor_weather_view_dispose;
	LRG_WIDGET_CLASS (klass)->measure = editor_weather_view_measure;
	LRG_WIDGET_CLASS (klass)->handle_event = editor_weather_view_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = editor_weather_view_layout_children;
}

static void
editor_weather_view_init (EditorWeatherView *view)
{
	view->yaw = 0.6f;
	view->pitch = 0.22f;
	view->zoom = 1.0f;
}

static EditorWeatherView *
editor_weather_view_new (LrgEditorWeatherEditor *editor)
{
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, view_caption_paint, NULL, NULL, { NULL } };
	EditorWeatherView *view = g_object_new (EDITOR_TYPE_WEATHER_VIEW, NULL);

	g_set_weak_pointer (&view->editor, editor);
	lrg_widget_set_name (LRG_WIDGET (view), "weather-preview");
	view->viewport = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (view->viewport), "weather-preview-viewport");
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
 * EditorWeatherSource implementation
 * ========================================================================== */

/* The synthesized specs of the file settings and of a preset. */
static GParamSpec *
settings_spec (guint index)
{
	static GParamSpec *specs[3];

	if (specs[0] == NULL)
	{
		specs[0] = g_param_spec_ref_sink (g_param_spec_string ("name", "Name", "Display name of the file", NULL,
		                                                       G_PARAM_READWRITE));
		specs[1] = g_param_spec_ref_sink (g_param_spec_string ("default-preset", "Default preset",
		                                                       "The preset lrg_weather_asset_apply() starts",
		                                                       NULL, G_PARAM_READWRITE));
		specs[2] = g_param_spec_ref_sink (g_param_spec_float ("default-transition", "Default transition",
		                                                      "Crossfade in seconds when no rule matches", 0.0f,
		                                                      3600.0f, 5.0f, G_PARAM_READWRITE));
	}
	return index < G_N_ELEMENTS (specs) ? specs[index] : NULL;
}

static GParamSpec *
preset_spec (guint index)
{
	static GParamSpec *specs[3];

	if (specs[0] == NULL)
	{
		specs[0] = g_param_spec_ref_sink (g_param_spec_string ("name", "Name", "Display name of the preset", NULL,
		                                                       G_PARAM_READWRITE));
		specs[1] = g_param_spec_ref_sink (g_param_spec_boxed ("ambient-color", "Ambient colour",
		                                                      "Ambient tint of the built weather", GRL_TYPE_COLOR,
		                                                      G_PARAM_READWRITE));
		specs[2] = g_param_spec_ref_sink (g_param_spec_float ("ambient-brightness", "Ambient brightness",
		                                                      "Ambient brightness of the built weather", 0.0f, 2.0f,
		                                                      1.0f, G_PARAM_READWRITE));
	}
	return index < G_N_ELEMENTS (specs) ? specs[index] : NULL;
}

static GParamSpec *
source_find (EditorWeatherSource *src,
             const gchar         *name)
{
	guint i;

	for (i = 0; i < src->specs->len; i++)
	{
		GParamSpec *s = g_ptr_array_index (src->specs, i);

		if (g_strcmp0 (s->name, name) == 0)
			return s;
	}
	return NULL;
}

static GPtrArray *
source_list_properties (LrgPropertySource *source)
{
	EditorWeatherSource *src = EDITOR_WEATHER_SOURCE (source);
	GPtrArray           *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint                i;

	for (i = 0; i < src->specs->len; i++)
		g_ptr_array_add (out, lrg_property_info_new (g_ptr_array_index (src->specs, i)));
	return out;
}

static gboolean
source_get_value (LrgPropertySource *source,
                  const gchar       *name,
                  GValue            *value)
{
	EditorWeatherSource    *src = EDITOR_WEATHER_SOURCE (source);
	LrgEditorWeatherEditor *ed = src->editor;
	GParamSpec             *spec = source_find (src, name);
	LrgWeatherAsset        *a;

	if (spec == NULL || ed == NULL || (a = ed->asset) == NULL)
		return FALSE;
	switch (src->mode)
	{
	case SOURCE_SETTINGS:
		g_value_init (value, spec->value_type);
		if (g_strcmp0 (name, "name") == 0)
			g_value_set_string (value, lrg_weather_asset_get_name (a));
		else if (g_strcmp0 (name, "default-preset") == 0)
			g_value_set_string (value, lrg_weather_asset_get_default_preset (a));
		else
			g_value_set_float (value, lrg_weather_asset_get_default_transition (a));
		return TRUE;
	case SOURCE_PRESET:
		if (src->preset >= lrg_weather_asset_get_preset_count (a))
			return FALSE;
		g_value_init (value, spec->value_type);
		if (g_strcmp0 (name, "name") == 0)
			g_value_set_string (value, lrg_weather_asset_get_preset_name (a, src->preset));
		else
		{
			GrlColor c = { 255, 255, 255, 255 };
			gfloat   br = 1.0f;

			lrg_weather_asset_get_preset_ambient (a, src->preset, &c, &br);
			if (g_strcmp0 (name, "ambient-color") == 0)
				g_value_set_boxed (value, &c);
			else
				g_value_set_float (value, br);
		}
		return TRUE;
	case SOURCE_LAYER:
	default:
		if (src->preset >= lrg_weather_asset_get_preset_count (a) ||
		    src->layer >= lrg_weather_asset_get_layer_count (a, src->preset))
			return FALSE;
		if (lrg_weather_asset_get_layer_param (a, src->preset, src->layer, name, value))
		{
			/* the stored value may be of another (transformable) type */
			if (G_VALUE_TYPE (value) != spec->value_type)
			{
				g_auto(GValue) conv = G_VALUE_INIT;

				g_value_init (&conv, spec->value_type);
				if (g_value_type_transformable (G_VALUE_TYPE (value), spec->value_type) &&
				    g_value_transform (value, &conv))
				{
					g_value_unset (value);
					g_value_init (value, spec->value_type);
					g_value_copy (&conv, value);
				}
			}
			return TRUE;
		}
		g_value_init (value, spec->value_type);
		g_param_value_set_default (spec, value);
		return TRUE;
	}
}

static LrgPropertyState
source_get_state (LrgPropertySource *source,
                  const gchar       *name)
{
	EditorWeatherSource    *src = EDITOR_WEATHER_SOURCE (source);
	LrgEditorWeatherEditor *ed = src->editor;
	g_auto(GValue)          v = G_VALUE_INIT;

	if (src->mode != SOURCE_LAYER || ed == NULL || ed->asset == NULL ||
	    src->preset >= lrg_weather_asset_get_preset_count (ed->asset) ||
	    src->layer >= lrg_weather_asset_get_layer_count (ed->asset, src->preset))
		return LRG_PROPERTY_STATE_NONE;
	/* an unset parameter shows as "default" (the effect's own value) */
	return lrg_weather_asset_get_layer_param (ed->asset, src->preset, src->layer, name, &v)
	       ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
}

/* defined below: the edit path shared with the grid */
static gboolean apply_grid_value (LrgEditorWeatherEditor  *self,
                                  const gchar             *name,
                                  const GValue            *value,
                                  guint                    gesture,
                                  GError                 **error);

static gboolean
source_set_value (LrgPropertySource *source,
                  const gchar       *name,
                  const GValue      *value)
{
	EditorWeatherSource *src = EDITOR_WEATHER_SOURCE (source);

	return src->editor != NULL && apply_grid_value (src->editor, name, value, 0, NULL);
}

static gboolean
source_reset (LrgPropertySource *source,
              const gchar       *name)
{
	EditorWeatherSource *src = EDITOR_WEATHER_SOURCE (source);

	if (src->editor == NULL || src->mode != SOURCE_LAYER)
		return FALSE;
	return lrg_editor_weather_editor_set_param (src->editor, src->preset, src->layer, name, NULL, 0, NULL);
}

static void
editor_weather_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = source_list_properties;
	iface->get_value = source_get_value;
	iface->set_value = source_set_value;
	iface->get_state = source_get_state;
	iface->reset = source_reset;
}

static void
editor_weather_source_finalize (GObject *object)
{
	EditorWeatherSource *src = EDITOR_WEATHER_SOURCE (object);

	g_clear_weak_pointer (&src->editor);
	g_ptr_array_unref (src->specs);
	G_OBJECT_CLASS (editor_weather_source_parent_class)->finalize (object);
}

static void
editor_weather_source_class_init (EditorWeatherSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_weather_source_finalize;
}

static void
editor_weather_source_init (EditorWeatherSource *src)
{
	src->specs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
}

static EditorWeatherSource *
editor_weather_source_new (LrgEditorWeatherEditor *editor,
                           SourceMode              mode,
                           guint                   preset,
                           guint                   layer)
{
	EditorWeatherSource *src = g_object_new (EDITOR_TYPE_WEATHER_SOURCE, NULL);
	guint                i;

	g_set_weak_pointer (&src->editor, editor);
	src->mode = mode;
	src->preset = preset;
	src->layer = layer;
	if (mode == SOURCE_SETTINGS)
		for (i = 0; settings_spec (i) != NULL; i++)
			g_ptr_array_add (src->specs, g_param_spec_ref (settings_spec (i)));
	else if (mode == SOURCE_PRESET)
		for (i = 0; preset_spec (i) != NULL; i++)
			g_ptr_array_add (src->specs, g_param_spec_ref (preset_spec (i)));
	else
	{
		const gchar *kind = lrg_weather_asset_get_layer_kind (editor->asset, preset, layer);
		guint        n = 0;
		g_autofree GParamSpec **specs = lrg_weather_asset_list_kind_params (kind, &n);

		for (i = 0; specs != NULL && i < n; i++)
			g_ptr_array_add (src->specs, g_param_spec_ref (specs[i]));
	}
	return src;
}

/* ==========================================================================
 * Widgets
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

static LrgButton *
index_button (const gchar *label,
              const gchar *name,
              gint         preset,
              gint         layer,
              GCallback    func,
              gpointer     data)
{
	LrgButton *b = tool_button (label, name, func, data);

	lrg_widget_add_class (LRG_WIDGET (b), "flat");
	g_object_set_data (G_OBJECT (b), "preset", GINT_TO_POINTER (preset));
	g_object_set_data (G_OBJECT (b), "layer", GINT_TO_POINTER (layer));
	return b;
}

static gint
data_index (gpointer     object,
            const gchar *key)
{
	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (object), key));
}

static void
report (LrgEditorWeatherEditor *self,
        GError                 *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_settings_clicked (LrgButton *button,
                     gpointer   user_data)
{
	lrg_editor_weather_editor_select (LRG_EDITOR_WEATHER_EDITOR (user_data), -1, -1);
}

static void
on_preset_clicked (LrgButton *button,
                   gpointer   user_data)
{
	lrg_editor_weather_editor_select (LRG_EDITOR_WEATHER_EDITOR (user_data), data_index (button, "preset"), -1);
}

static void
on_add_preset (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (lrg_editor_weather_editor_add_preset (self, NULL, NULL, &error) < 0)
		report (self, error);
}

static void
on_remove_preset (LrgButton *button,
                  gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->sel_preset >= 0 && !lrg_editor_weather_editor_remove_preset (self, (guint) self->sel_preset, &error))
		report (self, error);
}

static void
on_layer_clicked (LrgButton *button,
                  gpointer   user_data)
{
	lrg_editor_weather_editor_select (LRG_EDITOR_WEATHER_EDITOR (user_data), data_index (button, "preset"),
	                                  data_index (button, "layer"));
}

static void
on_layer_enable (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing)
		return;
	if (!lrg_editor_weather_editor_set_layer (self, (guint) data_index (check, "preset"),
	                                          (guint) data_index (check, "layer"), -1.0f, checked ? 1 : 0, 0, &error))
		report (self, error);
}

/* A slider's steps within SLIDER_GESTURE of each other are one drag. */
static guint
slider_gesture (LrgEditorWeatherEditor *self,
                gpointer                slider)
{
	gint64 now = g_get_monotonic_time ();

	if (slider != self->slider_last || self->slider_gesture == 0 ||
	    (gdouble) (now - self->slider_time) / G_USEC_PER_SEC > SLIDER_GESTURE)
		self->slider_gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
	self->slider_last = slider;
	self->slider_time = now;
	return self->slider_gesture;
}

static void
on_layer_intensity (LrgSlider *slider,
                    gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing)
		return;
	if (!lrg_editor_weather_editor_set_layer (self, (guint) data_index (slider, "preset"),
	                                          (guint) data_index (slider, "layer"),
	                                          (gfloat) lrg_slider_get_value (slider), -1,
	                                          slider_gesture (self, slider), &error))
		report (self, error);
}

static void
on_layer_move (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	gint                    p = data_index (button, "preset");
	gint                    l = data_index (button, "layer");
	gint                    to = l + data_index (button, "delta");

	if (self->asset == NULL || to < 0 || to >= (gint) lrg_weather_asset_get_layer_count (self->asset, (guint) p))
		return;
	if (!lrg_editor_weather_editor_move_layer (self, (guint) p, (guint) l, (guint) to, &error))
		report (self, error);
}

static void
on_layer_remove (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (!lrg_editor_weather_editor_remove_layer (self, (guint) data_index (button, "preset"),
	                                             (guint) data_index (button, "layer"), &error))
		report (self, error);
}

static void
on_add_layer (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	const gchar            *kind = lrg_combo_box_get_selected_text (self->kind_combo);

	if (self->sel_preset < 0 || kind == NULL)
		return;
	if (lrg_editor_weather_editor_add_layer (self, (guint) self->sel_preset, kind, NULL, &error) < 0)
		report (self, error);
}

static void
on_transition_duration (LrgSpinBox *spin,
                        gpointer    user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing)
		return;
	if (!lrg_editor_weather_editor_set_transition (self, g_object_get_data (G_OBJECT (spin), "from"),
	                                               g_object_get_data (G_OBJECT (spin), "to"),
	                                               (gfloat) lrg_spin_box_get_value (spin), &error))
		report (self, error);
}

static void
on_transition_remove (LrgButton *button,
                      gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (!lrg_editor_weather_editor_remove_transition (self, g_object_get_data (G_OBJECT (button), "from"),
	                                                  g_object_get_data (G_OBJECT (button), "to"), &error))
		report (self, error);
}

static void
on_add_transition (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	const gchar            *from = lrg_combo_box_get_selected_text (self->from_combo);
	const gchar            *to = lrg_combo_box_get_selected_text (self->to_combo);

	if (from == NULL || to == NULL)
		return;
	if (!lrg_editor_weather_editor_set_transition (self, from, to, (gfloat) lrg_spin_box_get_value (self->duration_spin),
	                                               &error))
		report (self, error);
}

static void
on_show (LrgComboBox *combo,
         gpointer     user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	const gchar            *id = lrg_combo_box_get_selected_text (combo);
	g_autoptr(GError)       error = NULL;

	if (self->syncing || id == NULL)
		return;
	if (!lrg_editor_weather_editor_preview_preset (self, g_strcmp0 (id, "none") == 0 ? NULL : id, &error))
		report (self, error);
}

static void
on_goto (LrgButton *button,
         gpointer   user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	const gchar            *id = lrg_combo_box_get_selected_text (self->goto_combo);
	g_autoptr(GError)       error = NULL;

	if (id == NULL)
		return;
	if (lrg_editor_weather_editor_transition_to (self, g_strcmp0 (id, "none") == 0 ? NULL : id, &error) < 0.0f)
		report (self, error);
}

static gchar *
lists_signature (LrgEditorWeatherEditor *self)
{
	GString *s = g_string_new (self->load_error);
	guint    p, l;

	g_string_append_printf (s, "|%d|%d|", self->sel_preset, self->sel_layer);
	if (self->asset == NULL)
		return g_string_free (s, FALSE);
	for (p = 0; p < lrg_weather_asset_get_preset_count (self->asset); p++)
	{
		g_string_append_printf (s, "%s:%s[", lrg_weather_asset_get_preset_id (self->asset, p),
		                        lrg_weather_asset_get_preset_name (self->asset, p));
		for (l = 0; l < lrg_weather_asset_get_layer_count (self->asset, p); l++)
			g_string_append_printf (s, "%s/%s/%d/%.3f,", lrg_weather_asset_get_layer_kind (self->asset, p, l),
			                        lrg_weather_asset_get_layer_id (self->asset, p, l),
			                        lrg_weather_asset_get_layer_enabled (self->asset, p, l),
			                        lrg_weather_asset_get_layer_intensity (self->asset, p, l));
		g_string_append (s, "]");
	}
	for (p = 0; p < lrg_weather_asset_get_transition_count (self->asset); p++)
	{
		const gchar *from = NULL, *to = NULL;
		gfloat       sec = 0.0f;

		lrg_weather_asset_get_transition (self->asset, p, &from, &to, &sec);
		g_string_append_printf (s, "t%s>%s=%.3f,", from, to, sec);
	}
	return g_string_free (s, FALSE);
}

static void
fill_preset_combo (LrgEditorWeatherEditor *self,
                   LrgComboBox            *combo,
                   gboolean                rules,
                   const gchar            *selected)
{
	g_autofree gchar *keep = g_strdup (selected);   /* @selected may be the combo's own text */
	guint             p;
	gint              sel = 0, n = 0;

	lrg_combo_box_remove_all (combo);
	if (rules)
	{
		lrg_combo_box_append (combo, "*");
		n++;
	}
	if (g_strcmp0 (keep, "none") == 0)
		sel = n;
	lrg_combo_box_append (combo, "none");
	n++;
	for (p = 0; self->asset != NULL && p < lrg_weather_asset_get_preset_count (self->asset); p++)
	{
		const gchar *id = lrg_weather_asset_get_preset_id (self->asset, p);

		if (g_strcmp0 (id, keep) == 0)
			sel = n;
		lrg_combo_box_append (combo, id);
		n++;
	}
	lrg_combo_box_set_selected (combo, sel);
}

static void
rebuild_lists (LrgEditorWeatherEditor *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	guint     p, l, n;

	self->lists_stale = FALSE;
	g_free (self->lists_sig);
	self->lists_sig = lists_signature (self);
	self->syncing = TRUE;
	lrg_container_remove_all (LRG_CONTAINER (self->presets));
	lrg_container_remove_all (LRG_CONTAINER (self->layers));
	lrg_container_remove_all (LRG_CONTAINER (self->transitions));
	lrg_widget_remove_class (LRG_WIDGET (self->settings_button), "primary");
	if (self->sel_preset < 0)
		lrg_widget_add_class (LRG_WIDGET (self->settings_button), "primary");
	if (self->asset == NULL)
	{
		LrgLabel *hint = lrg_label_new (self->load_error != NULL ? self->load_error : "The file does not load");

		lrg_label_set_color (hint, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		add_to (self->presets, LRG_WIDGET (hint));
		lrg_label_set_text (self->presets_title, "PRESETS");
		self->syncing = FALSE;
		return;
	}

	/* presets */
	n = lrg_weather_asset_get_preset_count (self->asset);
	{
		g_autofree gchar *t = g_strdup_printf ("PRESETS  \xc2\xb7  %u", n);

		lrg_label_set_text (self->presets_title, t);
	}
	if (n == 0)
		add_to (self->presets, heading ("No presets yet: Add one"));
	for (p = 0; p < n; p++)
	{
		LrgWidget        *row = hbox (6.0f);
		const gchar      *id = lrg_weather_asset_get_preset_id (self->asset, p);
		const gchar      *name = lrg_weather_asset_get_preset_name (self->asset, p);
		g_autofree gchar *bname = g_strdup_printf ("weather-preset-%u", p);
		g_autofree gchar *info = g_strdup_printf ("%s%s%u layer%s", name != NULL && g_strcmp0 (name, id) != 0 ? name : "",
		                                          name != NULL && g_strcmp0 (name, id) != 0 ? "  \xc2\xb7  " : "",
		                                          lrg_weather_asset_get_layer_count (self->asset, p),
		                                          lrg_weather_asset_get_layer_count (self->asset, p) == 1 ? "" : "s");
		LrgButton        *pick = index_button (id, bname, (gint) p, -1, G_CALLBACK (on_preset_clicked), self);
		LrgLabel         *il = lrg_label_new (info);
		gboolean          is_default = g_strcmp0 (id, lrg_weather_asset_get_default_preset (self->asset)) == 0;

		lrg_widget_remove_class (LRG_WIDGET (pick), "flat");
		lrg_widget_add_class (LRG_WIDGET (pick), (gint) p == self->sel_preset ? "primary" : "flat");
		lrg_widget_set_min_size (LRG_WIDGET (pick), 120.0f, 0.0f);
		add_to (row, LRG_WIDGET (pick));
		lrg_label_set_color (il, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_widget_set_fill_weight (LRG_WIDGET (il), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (il), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (il));
		if (is_default)
		{
			LrgLabel *d = lrg_label_new ("default");

			lrg_label_set_color (d, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
			lrg_widget_set_valign (LRG_WIDGET (d), LRG_ALIGN_CENTER);
			add_to (row, LRG_WIDGET (d));
		}
		add_to (self->presets, row);
	}

	/* layers of the selected preset */
	if (self->sel_preset < 0)
	{
		lrg_label_set_text (self->layers_title, "LAYERS");
		add_to (self->layers, heading ("Select a preset to edit its layers"));
	}
	else
	{
		guint             sp = (guint) self->sel_preset;
		g_autofree gchar *t = g_strdup_printf ("LAYERS  \xc2\xb7  %s", lrg_weather_asset_get_preset_id (self->asset, sp));

		lrg_label_set_text (self->layers_title, t);
		n = lrg_weather_asset_get_layer_count (self->asset, sp);
		if (n == 0)
			add_to (self->layers, heading ("No layers: a clear sky. Add rain, fog, wind ..."));
		for (l = 0; l < n; l++)
		{
			LrgWidget        *row = hbox (4.0f);
			const gchar      *kind = lrg_weather_asset_get_layer_kind (self->asset, sp, l);
			const gchar      *lid = lrg_weather_asset_get_layer_id (self->asset, sp, l);
			g_autofree gchar *text = g_strcmp0 (kind, lid) == 0 ? g_strdup (lid)
			                                                    : g_strdup_printf ("%s (%s)", lid, kind);
			g_autofree gchar *ename = g_strdup_printf ("weather-layer-enabled-%u", l);
			g_autofree gchar *pname = g_strdup_printf ("weather-layer-%u", l);
			g_autofree gchar *iname = g_strdup_printf ("weather-layer-intensity-%u", l);
			g_autofree gchar *uname = g_strdup_printf ("weather-layer-up-%u", l);
			g_autofree gchar *dname = g_strdup_printf ("weather-layer-down-%u", l);
			g_autofree gchar *rname = g_strdup_printf ("weather-layer-remove-%u", l);
			LrgCheckbox      *en = lrg_checkbox_new (NULL);
			LrgButton        *pick, *b;
			LrgSlider        *slider = lrg_slider_new_with_range (0.0, 1.0, 0.0);

			lrg_widget_set_name (LRG_WIDGET (en), ename);
			lrg_checkbox_set_checked (en, lrg_weather_asset_get_layer_enabled (self->asset, sp, l));
			g_object_set_data (G_OBJECT (en), "preset", GINT_TO_POINTER ((gint) sp));
			g_object_set_data (G_OBJECT (en), "layer", GINT_TO_POINTER ((gint) l));
			g_signal_connect (en, "toggled", G_CALLBACK (on_layer_enable), self);
			lrg_widget_set_valign (LRG_WIDGET (en), LRG_ALIGN_CENTER);
			add_to (row, LRG_WIDGET (en));
			pick = index_button (text, pname, (gint) sp, (gint) l, G_CALLBACK (on_layer_clicked), self);
			lrg_widget_remove_class (LRG_WIDGET (pick), "flat");
			lrg_widget_add_class (LRG_WIDGET (pick), (gint) l == self->sel_layer ? "primary" : "flat");
			lrg_widget_set_fill_weight (LRG_WIDGET (pick), 1.0f);
			add_to (row, LRG_WIDGET (pick));
			lrg_widget_set_name (LRG_WIDGET (slider), iname);
			lrg_slider_set_value (slider, lrg_weather_asset_get_layer_intensity (self->asset, sp, l));
			lrg_widget_set_min_size (LRG_WIDGET (slider), 110.0f, 0.0f);
			lrg_widget_set_valign (LRG_WIDGET (slider), LRG_ALIGN_CENTER);
			g_object_set_data (G_OBJECT (slider), "preset", GINT_TO_POINTER ((gint) sp));
			g_object_set_data (G_OBJECT (slider), "layer", GINT_TO_POINTER ((gint) l));
			g_signal_connect (slider, "value-changed", G_CALLBACK (on_layer_intensity), self);
			add_to (row, LRG_WIDGET (slider));
			b = index_button ("\xe2\x96\xb2", uname, (gint) sp, (gint) l, G_CALLBACK (on_layer_move), self);
			g_object_set_data (G_OBJECT (b), "delta", GINT_TO_POINTER (-1));
			add_to (row, LRG_WIDGET (b));
			b = index_button ("\xe2\x96\xbc", dname, (gint) sp, (gint) l, G_CALLBACK (on_layer_move), self);
			g_object_set_data (G_OBJECT (b), "delta", GINT_TO_POINTER (1));
			add_to (row, LRG_WIDGET (b));
			b = index_button ("\xc3\x97", rname, (gint) sp, (gint) l, G_CALLBACK (on_layer_remove), self);
			add_to (row, LRG_WIDGET (b));
			add_to (self->layers, row);
		}
	}

	/* transitions */
	n = lrg_weather_asset_get_transition_count (self->asset);
	if (n == 0)
		add_to (self->transitions, heading ("No rules: every crossfade takes the default transition"));
	for (l = 0; l < n; l++)
	{
		const gchar      *from = NULL, *to = NULL;
		gfloat            sec = 0.0f;
		LrgWidget        *row = hbox (6.0f);
		g_autofree gchar *text = NULL;
		g_autofree gchar *sname = g_strdup_printf ("weather-transition-duration-%u", l);
		g_autofree gchar *rname = g_strdup_printf ("weather-transition-remove-%u", l);
		LrgLabel         *tl;
		LrgSpinBox       *spin = lrg_spin_box_new_with_range (0.0, 3600.0, 0.5);
		LrgButton        *rm;

		lrg_weather_asset_get_transition (self->asset, l, &from, &to, &sec);
		text = g_strdup_printf ("%s  \xe2\x86\x92  %s", from, to);
		tl = lrg_label_new (text);
		lrg_widget_set_fill_weight (LRG_WIDGET (tl), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (tl), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (tl));
		lrg_widget_set_name (LRG_WIDGET (spin), sname);
		lrg_spin_box_set_digits (spin, 1);
		lrg_spin_box_set_value (spin, sec);
		lrg_widget_set_min_size (LRG_WIDGET (spin), 90.0f, 0.0f);
		g_object_set_data_full (G_OBJECT (spin), "from", g_strdup (from), g_free);
		g_object_set_data_full (G_OBJECT (spin), "to", g_strdup (to), g_free);
		g_signal_connect (spin, "value-changed", G_CALLBACK (on_transition_duration), self);
		add_to (row, LRG_WIDGET (spin));
		rm = index_button ("\xc3\x97", rname, -1, -1, G_CALLBACK (on_transition_remove), self);
		g_object_set_data_full (G_OBJECT (rm), "from", g_strdup (from), g_free);
		g_object_set_data_full (G_OBJECT (rm), "to", g_strdup (to), g_free);
		add_to (row, LRG_WIDGET (rm));
		add_to (self->transitions, row);
	}
	fill_preset_combo (self, self->from_combo, TRUE, lrg_combo_box_get_selected_text (self->from_combo));
	fill_preset_combo (self, self->to_combo, TRUE, lrg_combo_box_get_selected_text (self->to_combo));
	fill_preset_combo (self, self->show_combo, FALSE, self->preview_id);
	fill_preset_combo (self, self->goto_combo, FALSE, lrg_combo_box_get_selected_text (self->goto_combo));
	self->syncing = FALSE;
}

/* ==========================================================================
 * The property grid
 * ========================================================================== */

static void
rebuild_grid (LrgEditorWeatherEditor *self)
{
	SourceMode        mode;
	g_autofree gchar *title = NULL;

	self->grid_stale = FALSE;
	g_clear_object (&self->source);
	if (self->asset == NULL)
	{
		lrg_label_set_text (self->grid_title, "PROPERTIES");
		lrg_property_grid_set_source (self->grid, NULL);
		return;
	}
	if (self->sel_preset < 0)
	{
		mode = SOURCE_SETTINGS;
		title = g_strdup ("FILE SETTINGS");
	}
	else if (self->sel_layer < 0)
	{
		mode = SOURCE_PRESET;
		title = g_strdup_printf ("PRESET  \xc2\xb7  %s",
		                         lrg_weather_asset_get_preset_id (self->asset, (guint) self->sel_preset));
	}
	else
	{
		mode = SOURCE_LAYER;
		title = g_strdup_printf ("LAYER  \xc2\xb7  %s  (%s)",
		                         lrg_weather_asset_get_layer_id (self->asset, (guint) self->sel_preset,
		                                                         (guint) self->sel_layer),
		                         lrg_weather_asset_get_layer_kind (self->asset, (guint) self->sel_preset,
		                                                           (guint) self->sel_layer));
	}
	lrg_label_set_text (self->grid_title, title);
	self->source = editor_weather_source_new (self, mode, (guint) MAX (self->sel_preset, 0),
	                                          (guint) MAX (self->sel_layer, 0));
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
}

/* A value of the grid as file-syntax text (what set_param takes). */
static gchar *
value_text (const GValue *value)
{
	if (G_VALUE_HOLDS_STRING (value))
		return g_value_dup_string (value);
	if (G_VALUE_HOLDS_FLOAT (value) || G_VALUE_HOLDS_DOUBLE (value))
	{
		gchar  buf[G_ASCII_DTOSTR_BUF_SIZE];
		gdouble d = G_VALUE_HOLDS_FLOAT (value) ? (gdouble) g_value_get_float (value) : g_value_get_double (value);

		g_ascii_formatd (buf, sizeof buf, "%.7g", d);
		return g_strdup (buf);
	}
	if (G_VALUE_HOLDS_BOXED (value) && G_VALUE_TYPE (value) == GRL_TYPE_COLOR && g_value_get_boxed (value) != NULL)
	{
		const GrlColor *c = g_value_get_boxed (value);

		return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
	}
	return lrg_property_value_to_string (value);
}

static gboolean
apply_grid_value (LrgEditorWeatherEditor  *self,
                  const gchar             *name,
                  const GValue            *value,
                  guint                    gesture,
                  GError                 **error)
{
	EditorWeatherSource *src = self->source;
	g_autofree gchar    *text = NULL;

	if (src == NULL || self->asset == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Nothing is selected");
		return FALSE;
	}
	switch (src->mode)
	{
	case SOURCE_SETTINGS:
		text = value_text (value);
		return lrg_editor_weather_editor_set_setting (self, name, text != NULL && *text != '\0' ? text : NULL, error);
	case SOURCE_PRESET:
		if (g_strcmp0 (name, "name") == 0)
		{
			text = value_text (value);
			return lrg_editor_weather_editor_set_preset (self, src->preset, text != NULL ? text : "", NULL, -1.0f,
			                                             gesture, error);
		}
		if (g_strcmp0 (name, "ambient-color") == 0 && G_VALUE_HOLDS (value, GRL_TYPE_COLOR) &&
		    g_value_get_boxed (value) != NULL)
			return lrg_editor_weather_editor_set_preset (self, src->preset, NULL, g_value_get_boxed (value), -1.0f,
			                                             gesture, error);
		if (g_strcmp0 (name, "ambient-brightness") == 0 && G_VALUE_HOLDS_FLOAT (value))
			return lrg_editor_weather_editor_set_preset (self, src->preset, NULL, NULL, g_value_get_float (value),
			                                             gesture, error);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot set '%s' on a preset", name);
		return FALSE;
	case SOURCE_LAYER:
	default:
		{
			/* typed: set_layer_param takes the GValue as it is */
			g_autoptr(GError) err = NULL;

			if (!need_layer (self, src->preset, src->layer, error))
				return FALSE;
			if (!lrg_weather_asset_set_layer_param (self->asset, src->preset, src->layer, name, value, &err))
			{
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
				return FALSE;
			}
			sync_preset (self, src->preset);
			{
				g_autofree gchar *path = g_strdup_printf ("/presets/%u/layers/%u", src->preset, src->layer);
				g_autofree gchar *label = g_strdup_printf ("Set %s", name);

				return commit (self, path, label, gesture, error);
			}
		}
	}
}

static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);

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
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	guint                   gesture = self->gesture;

	if (gesture == 0)
		gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
	if (apply_grid_value (self, name, value, gesture, &error))
		self->gesture_wrote = TRUE;
	else
		report (self, error);
	lrg_property_grid_refresh (self->grid);
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit_edit,
                  gpointer         user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);

	if (!commit_edit && self->gesture_wrote)
		lrg_editor_document_undo (doc_of (self));
	self->gesture = 0;
	self->gesture_wrote = FALSE;
}

static void
on_grid_reset (LrgPropertyGrid *grid,
               const gchar     *name,
               gpointer         user_data)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->source == NULL)
		return;
	if (self->source->mode == SOURCE_LAYER)
	{
		if (!lrg_editor_weather_editor_set_param (self, self->source->preset, self->source->layer, name, NULL, 0,
		                                          &error))
			report (self, error);
	}
	else if (self->source->mode == SOURCE_SETTINGS)
	{
		if (!lrg_editor_weather_editor_set_setting (self, name, NULL, &error))
			report (self, error);
	}
	lrg_property_grid_refresh (self->grid);
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
weather_refresh (LrgEditorDataEditor *editor,
                 const gchar         *path)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (editor);
	g_autofree gchar       *sig = NULL;

	if (self->grid == NULL)
		return;
	if (!self->committing)
	{
		load_asset (self);
		rebuild_manager (self, FALSE);
		self->grid_stale = TRUE;
	}
	else
		clamp_selection (self);
	sig = lists_signature (self);
	if (g_strcmp0 (sig, self->lists_sig) != 0)
		self->lists_stale = TRUE;
	if (self->committing)
		lrg_property_grid_refresh (self->grid);
}

/* "presets[0].layers[1].params.drop-count: ..." -> "/presets/0/layers/1/params/drop-count" */
static gchar *
message_path (const gchar *message)
{
	GString     *s;
	const gchar *p;

	if (message == NULL)
		return NULL;
	s = g_string_new (NULL);
	for (p = message; *p != '\0' && *p != ':' && *p != ' '; p++)
	{
		if (*p == '.' || *p == '[')
			g_string_append_c (s, '/');
		else if (*p != ']')
			g_string_append_c (s, *p);
	}
	if (*p != ':' || s->len == 0)
	{
		g_string_free (s, TRUE);
		return NULL;
	}
	g_string_prepend_c (s, '/');
	return g_string_free (s, FALSE);
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
weather_validate (LrgEditorDataEditor *editor,
                  GPtrArray           *issues)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (editor);
	guint                   p, l, line, col;

	if (self->load_error != NULL)
	{
		g_autofree gchar *path = message_path (self->load_error);

		line = 1;
		col = 1;
		if (path != NULL && !lrg_editor_document_get_line (doc_of (self), path, &line, &col))
		{
			/* the deepest existing prefix of the path */
			g_autofree gchar *up = path_parent (path);

			while (*up != '\0' && !lrg_editor_document_get_line (doc_of (self), up, &line, &col))
			{
				gchar *next = path_parent (up);

				g_free (up);
				up = next;
			}
		}
		/* the document's own validation ran the loader too: replace its
		 * issue of the same error by this one, which names the key path */
		remove_same_error (issues, self->load_error);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path != NULL ? path : "",
		                                                        line, col, self->load_error));
		return;
	}
	if (self->asset == NULL)
		return;
	if (lrg_weather_asset_get_preset_count (self->asset) == 0)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_INFO, "/presets", 0, 0,
		                                                        "The file has no presets"));
	else if (lrg_weather_asset_get_default_preset (self->asset) == NULL)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_INFO, "", 0, 0,
		                                                        "No default-preset: lrg_weather_asset_apply() "
		                                                        "starts with a clear sky"));
	for (p = 0; p < lrg_weather_asset_get_preset_count (self->asset); p++)
	{
		gboolean any = FALSE;

		for (l = 0; l < lrg_weather_asset_get_layer_count (self->asset, p); l++)
			any = any || lrg_weather_asset_get_layer_enabled (self->asset, p, l);
		if (lrg_weather_asset_get_layer_count (self->asset, p) > 0 && !any)
		{
			g_autofree gchar *path = preset_path (p);
			g_autofree gchar *msg = g_strdup_printf ("every layer of preset '%s' is disabled",
			                                         lrg_weather_asset_get_preset_id (self->asset, p));

			line = col = 0;
			lrg_editor_document_get_line (doc_of (self), path, &line, &col);
			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col, msg));
		}
	}
}

static void
weather_reveal (LrgEditorDataEditor *editor,
                const gchar         *path,
                guint                line)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (editor);
	guint                   p, l;

	if (path == NULL)
		return;
	if (sscanf (path, "/presets/%u/layers/%u", &p, &l) == 2)
		lrg_editor_weather_editor_select (self, (gint) p, (gint) l);
	else if (sscanf (path, "/presets/%u", &p) == 1)
		lrg_editor_weather_editor_select (self, (gint) p, -1);
	else
		lrg_editor_weather_editor_select (self, -1, -1);
}

static gchar *
weather_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (editor);

	return g_strdup_printf ("presets=%u preset=%d layer=%d preview=%s",
	                        self->asset != NULL ? lrg_weather_asset_get_preset_count (self->asset) : 0u,
	                        self->sel_preset, self->sel_layer, self->preview_id != NULL ? self->preview_id : "none");
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_weather_editor_step (LrgWidget *widget,
                                gfloat     delta_time)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (widget);
	gboolean                busy;

	preview_step (self, delta_time);
	busy = LRG_WIDGET_CLASS (lrg_editor_weather_editor_parent_class)->step (widget, delta_time);
	if (self->lists_stale)
		rebuild_lists (self);
	if (self->grid_stale)
		rebuild_grid (self);
	return busy || self->manager != NULL;
}

static void
lrg_editor_weather_editor_constructed (GObject *object)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (object);
	LrgEditorDataEditor    *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp           *app;
	LrgWidget              *column, *row, *right, *scroll;
	LrgSplitter            *outer;
	g_auto(GStrv)           kinds = NULL;
	LrgButton              *b;
	guint                   i;

	G_OBJECT_CLASS (lrg_editor_weather_editor_parent_class)->constructed (object);
	app = app_of (self);

	column = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (column, "weather-column");
	lrg_container_set_spacing (LRG_CONTAINER (column), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (column), 8.0f);

	self->settings_button = tool_button ("File Settings", "weather-settings", G_CALLBACK (on_settings_clicked), self);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->settings_button));
	g_object_unref (self->settings_button);

	/* presets */
	self->presets_title = LRG_LABEL (heading ("PRESETS"));
	lrg_widget_set_name (LRG_WIDGET (self->presets_title), "weather-presets-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->presets_title));
	g_object_unref (self->presets_title);
	row = hbox (6.0f);
	add_to (row, LRG_WIDGET (tool_button ("Add Preset", "weather-add-preset", G_CALLBACK (on_add_preset), self)));
	add_to (row, LRG_WIDGET (tool_button ("Remove", "weather-remove-preset", G_CALLBACK (on_remove_preset), self)));
	add_to (column, row);
	self->presets = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->presets, "weather-presets");
	lrg_container_set_spacing (LRG_CONTAINER (self->presets), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (column), self->presets);
	g_object_unref (self->presets);

	/* layers */
	self->layers_title = LRG_LABEL (heading ("LAYERS"));
	lrg_widget_set_name (LRG_WIDGET (self->layers_title), "weather-layers-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->layers_title));
	g_object_unref (self->layers_title);
	row = hbox (6.0f);
	self->kind_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->kind_combo), "weather-layer-kind");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->kind_combo), 1.0f);
	kinds = lrg_weather_asset_list_kinds ();
	for (i = 0; kinds != NULL && kinds[i] != NULL; i++)
		lrg_combo_box_append (self->kind_combo, kinds[i]);
	lrg_combo_box_set_selected (self->kind_combo, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->kind_combo));
	g_object_unref (self->kind_combo);
	add_to (row, LRG_WIDGET (tool_button ("Add Layer", "weather-add-layer", G_CALLBACK (on_add_layer), self)));
	add_to (column, row);
	self->layers = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->layers, "weather-layers");
	lrg_container_set_spacing (LRG_CONTAINER (self->layers), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (column), self->layers);
	g_object_unref (self->layers);

	/* the property grid */
	self->grid_title = LRG_LABEL (heading ("PROPERTIES"));
	lrg_widget_set_name (LRG_WIDGET (self->grid_title), "weather-grid-title");
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->grid_title));
	g_object_unref (self->grid_title);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "weather-grid");
	lrg_property_grid_set_label_width (self->grid, 150.0f);
	lrg_property_grid_set_apply_mode (self->grid, LRG_PROPERTY_APPLY_DELEGATE);
	g_signal_connect_object (self->grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self, 0);
	g_signal_connect_object (self->grid, "edit-value", G_CALLBACK (on_grid_edit_value), self, 0);
	g_signal_connect_object (self->grid, "edit-end", G_CALLBACK (on_grid_edit_end), self, 0);
	g_signal_connect_object (self->grid, "reset-requested", G_CALLBACK (on_grid_reset), self, 0);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);

	/* transitions */
	add_to (column, heading ("TRANSITIONS"));
	self->transitions = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->transitions, "weather-transitions");
	lrg_container_set_spacing (LRG_CONTAINER (self->transitions), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (column), self->transitions);
	g_object_unref (self->transitions);
	row = hbox (6.0f);
	self->from_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->from_combo), "weather-transition-from");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->from_combo), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->from_combo));
	g_object_unref (self->from_combo);
	add_to (row, heading ("\xe2\x86\x92"));
	self->to_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->to_combo), "weather-transition-to");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->to_combo), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->to_combo));
	g_object_unref (self->to_combo);
	self->duration_spin = lrg_spin_box_new_with_range (0.0, 3600.0, 0.5);
	lrg_spin_box_set_digits (self->duration_spin, 1);
	lrg_spin_box_set_value (self->duration_spin, 5.0);
	lrg_widget_set_name (LRG_WIDGET (self->duration_spin), "weather-transition-duration");
	lrg_widget_set_min_size (LRG_WIDGET (self->duration_spin), 80.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->duration_spin));
	g_object_unref (self->duration_spin);
	add_to (row, LRG_WIDGET (tool_button ("Add Rule", "weather-add-transition", G_CALLBACK (on_add_transition),
	                                      self)));
	add_to (column, row);

	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "weather-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), column);
	g_object_unref (column);

	/* --- right: the preview --- */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (right), 6.0f);
	row = hbox (8.0f);
	add_to (row, heading ("Show"));
	self->show_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->show_combo), "weather-show");
	lrg_widget_set_min_size (LRG_WIDGET (self->show_combo), 140.0f, 0.0f);
	g_signal_connect_object (self->show_combo, "changed", G_CALLBACK (on_show), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->show_combo));
	g_object_unref (self->show_combo);
	add_to (row, heading ("Transition to"));
	self->goto_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->goto_combo), "weather-goto");
	lrg_widget_set_min_size (LRG_WIDGET (self->goto_combo), 140.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->goto_combo));
	g_object_unref (self->goto_combo);
	b = tool_button ("Crossfade", "weather-goto-run", G_CALLBACK (on_goto), self);
	lrg_widget_add_class (LRG_WIDGET (b), "primary");
	add_to (row, LRG_WIDGET (b));
	add_to (right, row);
	self->view = editor_weather_view_new (self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->view), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (self->view));
	g_object_unref (self->view);
	self->status = lrg_label_new ("Drag to orbit, wheel to zoom. Fog shows as a screen veil here.");
	lrg_widget_add_class (LRG_WIDGET (self->status), "dim-label");
	lrg_widget_set_name (LRG_WIDGET (self->status), "weather-status");
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (self->status));
	g_object_unref (self->status);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "weather-split");
	lrg_splitter_set_start_child (outer, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_end_child (outer, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (outer, 0.42f);
	lrg_splitter_set_fraction (outer, 0.42f, FALSE);
	lrg_splitter_set_min_sizes (outer, 400.0f, 320.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	load_asset (self);
	rebuild_manager (self, TRUE);
	if (self->asset != NULL && self->preview_id != NULL)
		self->sel_preset = lrg_weather_asset_find_preset (self->asset, self->preview_id);
	rebuild_lists (self);
	rebuild_grid (self);
	if (app != NULL)
		lrg_editor_weather_editor_install (app);
}

static void
lrg_editor_weather_editor_dispose (GObject *object)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (object);

	g_clear_object (&self->manager);
	g_clear_object (&self->source);
	G_OBJECT_CLASS (lrg_editor_weather_editor_parent_class)->dispose (object);
}

static void
lrg_editor_weather_editor_finalize (GObject *object)
{
	LrgEditorWeatherEditor *self = LRG_EDITOR_WEATHER_EDITOR (object);

	g_clear_object (&self->asset);
	g_free (self->load_error);
	g_free (self->preview_id);
	g_free (self->lists_sig);
	G_OBJECT_CLASS (lrg_editor_weather_editor_parent_class)->finalize (object);
}

static void
lrg_editor_weather_editor_class_init (LrgEditorWeatherEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_weather_editor_constructed;
	object_class->dispose = lrg_editor_weather_editor_dispose;
	object_class->finalize = lrg_editor_weather_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_weather_editor_step;
	data_class->refresh = weather_refresh;
	data_class->validate = weather_validate;
	data_class->reveal = weather_reveal;
	data_class->get_state = weather_get_state;
}

static void
lrg_editor_weather_editor_init (LrgEditorWeatherEditor *self)
{
	self->sel_preset = -1;
	self->sel_layer = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorWeatherEditor *
lrg_editor_weather_editor_new (LrgEditorApp      *app,
                               LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_WEATHER_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorWeatherEditor *
lrg_editor_weather_editor_open (LrgEditorApp  *app,
                                const gchar   *path,
                                GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_WEATHER_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a weather file", path);
		return NULL;
	}
	return LRG_EDITOR_WEATHER_EDITOR (e);
}

gboolean
lrg_editor_weather_editor_create (LrgEditorApp  *app,
                                  const gchar   *path,
                                  const gchar   *template_name,
                                  GError       **error)
{
	g_autofree gchar           *abs = NULL;
	g_autofree gchar           *dir = NULL;
	g_autofree gchar           *base = NULL;
	g_autofree gchar           *text = NULL;
	g_autoptr(LrgWeatherAsset)  asset = NULL;
	gchar                      *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (template_name == NULL || g_strcmp0 (template_name, "default") == 0)
		asset = lrg_weather_asset_new_from_data (DEFAULT_WEATHER, -1, NULL, error);
	else if (g_strcmp0 (template_name, "empty") == 0)
		asset = lrg_weather_asset_new ();
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
	lrg_weather_asset_set_name (asset, base);
	text = lrg_weather_asset_to_data (asset, error);
	return text != NULL && g_file_set_contents (abs, text, -1, error);
}

LrgWeatherAsset *
lrg_editor_weather_editor_get_asset (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), NULL);
	return self->asset;
}

const gchar *
lrg_editor_weather_editor_get_load_error (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), NULL);
	return self->load_error;
}

LrgWeatherManager *
lrg_editor_weather_editor_get_manager (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), NULL);
	return self->manager;
}

gboolean
lrg_editor_weather_editor_select (LrgEditorWeatherEditor *self,
                                  gint                    preset,
                                  gint                    layer)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (preset < 0)
	{
		preset = -1;
		layer = -1;
	}
	else
	{
		if (self->asset == NULL || (guint) preset >= lrg_weather_asset_get_preset_count (self->asset))
			return FALSE;
		if (layer >= (gint) lrg_weather_asset_get_layer_count (self->asset, (guint) preset))
			return FALSE;
		if (layer < 0)
			layer = -1;
	}
	if (preset >= 0 && preset != self->sel_preset)
	{
		/* selecting a preset previews it */
		g_free (self->preview_id);
		self->preview_id = g_strdup (lrg_weather_asset_get_preset_id (self->asset, (guint) preset));
		rebuild_manager (self, FALSE);
	}
	self->sel_preset = preset;
	self->sel_layer = layer;
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return TRUE;
}

gint
lrg_editor_weather_editor_get_selected_preset (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), -1);
	return self->sel_preset;
}

gint
lrg_editor_weather_editor_get_selected_layer (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), -1);
	return self->sel_layer;
}

gboolean
lrg_editor_weather_editor_set_setting (LrgEditorWeatherEditor  *self,
                                       const gchar             *key,
                                       const gchar             *value,
                                       GError                 **error)
{
	g_autofree gchar *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (!need_asset (self, error))
		return FALSE;
	if (g_strcmp0 (key, "name") == 0)
		lrg_weather_asset_set_name (self->asset, value);
	else if (g_strcmp0 (key, "default-preset") == 0)
	{
		if (value != NULL && lrg_weather_asset_find_preset (self->asset, value) < 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No preset '%s'", value);
			return FALSE;
		}
		lrg_weather_asset_set_default_preset (self->asset, value);
	}
	else if (g_strcmp0 (key, "default-transition") == 0)
	{
		gchar  *end = NULL;
		gdouble v = value != NULL ? g_ascii_strtod (value, &end) : 5.0;

		if (value != NULL && (end == value || *end != '\0' || v < 0.0 || !isfinite (v)))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "default-transition: '%s' is not a number of seconds >= 0", value);
			return FALSE;
		}
		lrg_weather_asset_set_default_transition (self->asset, (gfloat) v);
	}
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown setting '%s' (name, default-preset, default-transition)", key);
		return FALSE;
	}
	label = g_strdup_printf ("Set %s", key);
	return commit (self, "", label, 0, error);
}

/* An id no preset uses: @base, else "@base-2", ... */
static gchar *
unique_preset_id (LrgWeatherAsset *asset,
                  const gchar     *base)
{
	guint n;

	if (lrg_weather_asset_find_preset (asset, base) < 0)
		return g_strdup (base);
	for (n = 2; n < 100000; n++)
	{
		g_autofree gchar *id = g_strdup_printf ("%s-%u", base, n);

		if (lrg_weather_asset_find_preset (asset, id) < 0)
			return g_steal_pointer (&id);
	}
	return g_strdup_printf ("%s-%" G_GINT64_FORMAT, base, g_get_real_time ());
}

gint
lrg_editor_weather_editor_add_preset (LrgEditorWeatherEditor  *self,
                                      const gchar             *id,
                                      const gchar             *name,
                                      GError                 **error)
{
	g_autofree gchar *unique = NULL;
	g_autofree gchar *label = NULL;
	g_autoptr(GError) err = NULL;
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), -1);

	if (!need_asset (self, error))
		return -1;
	unique = id != NULL ? g_strdup (id) : unique_preset_id (self->asset, "preset");
	index = lrg_weather_asset_add_preset (self->asset, unique, name, &err);
	if (index < 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
		return -1;
	}
	label = g_strdup_printf ("Add Preset %s", unique);
	if (!commit (self, "/presets", label, 0, error))
		return -1;
	sync_preset (self, (guint) index);
	lrg_editor_weather_editor_select (self, index, -1);
	return index;
}

gboolean
lrg_editor_weather_editor_remove_preset (LrgEditorWeatherEditor  *self,
                                         guint                    preset,
                                         GError                 **error)
{
	g_autofree gchar *id = NULL;
	g_autofree gchar *label = NULL;
	guint             t;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_preset (self, preset, error))
		return FALSE;
	id = g_strdup (lrg_weather_asset_get_preset_id (self->asset, preset));
	/* the rules and the default naming it go with it */
	for (t = lrg_weather_asset_get_transition_count (self->asset); t > 0; t--)
	{
		const gchar      *from = NULL, *to = NULL;
		g_autofree gchar *f = NULL, *o = NULL;

		lrg_weather_asset_get_transition (self->asset, t - 1, &from, &to, NULL);
		if (g_strcmp0 (from, id) != 0 && g_strcmp0 (to, id) != 0)
			continue;
		f = g_strdup (from);
		o = g_strdup (to);
		lrg_weather_asset_remove_transition (self->asset, f, o);
	}
	if (g_strcmp0 (lrg_weather_asset_get_default_preset (self->asset), id) == 0)
		lrg_weather_asset_set_default_preset (self->asset, NULL);
	lrg_weather_asset_remove_preset (self->asset, preset);
	label = g_strdup_printf ("Remove Preset %s", id);
	if (!commit (self, "", label, 0, error))
		return FALSE;
	if (self->manager != NULL)
	{
		if (g_strcmp0 (self->preview_id, id) == 0)
		{
			g_clear_pointer (&self->preview_id, g_free);
			lrg_weather_manager_clear_weather (self->manager, 0.0f);
		}
		lrg_weather_manager_unregister_weather (self->manager, id);
	}
	clamp_selection (self);
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_weather_editor_set_preset (LrgEditorWeatherEditor  *self,
                                      guint                    preset,
                                      const gchar             *name,
                                      const GrlColor          *ambient,
                                      gfloat                   brightness,
                                      guint                    gesture,
                                      GError                 **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_preset (self, preset, error))
		return FALSE;
	if (name != NULL)
		lrg_weather_asset_set_preset_name (self->asset, preset, *name != '\0' ? name : NULL);
	if (ambient != NULL || brightness >= 0.0f)
	{
		GrlColor c = { 255, 255, 255, 255 };
		gfloat   br = 1.0f;

		lrg_weather_asset_get_preset_ambient (self->asset, preset, &c, &br);
		if (ambient != NULL)
			c = *ambient;
		if (brightness >= 0.0f)
			br = CLAMP (brightness, 0.0f, 2.0f);
		lrg_weather_asset_set_preset_ambient (self->asset, preset, &c, br);
	}
	sync_preset (self, preset);
	path = preset_path (preset);
	return commit (self, path, "Edit Preset", gesture, error);
}

/* A layer id unused in @preset: the kind, else "kind-2", ... */
static gchar *
unique_layer_id (LrgWeatherAsset *asset,
                 guint            preset,
                 const gchar     *base)
{
	guint n;

	if (lrg_weather_asset_find_layer (asset, preset, base) < 0)
		return g_strdup (base);
	for (n = 2; n < 100000; n++)
	{
		g_autofree gchar *id = g_strdup_printf ("%s-%u", base, n);

		if (lrg_weather_asset_find_layer (asset, preset, id) < 0)
			return g_steal_pointer (&id);
	}
	return g_strdup_printf ("%s-%" G_GINT64_FORMAT, base, g_get_real_time ());
}

gint
lrg_editor_weather_editor_add_layer (LrgEditorWeatherEditor  *self,
                                     guint                    preset,
                                     const gchar             *kind,
                                     const gchar             *id,
                                     GError                 **error)
{
	g_autofree gchar *unique = NULL;
	g_autofree gchar *label = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GError) err = NULL;
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), -1);
	g_return_val_if_fail (kind != NULL, -1);

	if (!need_preset (self, preset, error))
		return -1;
	if (lrg_weather_asset_lookup_kind (kind) == G_TYPE_INVALID)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown layer kind '%s'", kind);
		return -1;
	}
	unique = id != NULL ? g_strdup (id) : unique_layer_id (self->asset, preset, kind);
	index = lrg_weather_asset_add_layer (self->asset, preset, kind, unique, &err);
	if (index < 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
		return -1;
	}
	sync_preset (self, preset);
	path = preset_path (preset);
	label = g_strdup_printf ("Add Layer %s", unique);
	if (!commit (self, path, label, 0, error))
		return -1;
	lrg_editor_weather_editor_select (self, (gint) preset, index);
	return index;
}

gboolean
lrg_editor_weather_editor_remove_layer (LrgEditorWeatherEditor  *self,
                                        guint                    preset,
                                        guint                    layer,
                                        GError                 **error)
{
	g_autofree gchar *label = NULL;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_layer (self, preset, layer, error))
		return FALSE;
	label = g_strdup_printf ("Remove Layer %s", lrg_weather_asset_get_layer_id (self->asset, preset, layer));
	lrg_weather_asset_remove_layer (self->asset, preset, layer);
	sync_preset (self, preset);
	path = preset_path (preset);
	if (!commit (self, path, label, 0, error))
		return FALSE;
	clamp_selection (self);
	if (self->sel_preset == (gint) preset && self->sel_layer >= (gint) layer)
		self->sel_layer = self->sel_layer > 0 ? self->sel_layer - 1 : -1;
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_weather_editor_move_layer (LrgEditorWeatherEditor  *self,
                                      guint                    preset,
                                      guint                    from,
                                      guint                    to,
                                      GError                 **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_layer (self, preset, from, error) || !need_layer (self, preset, to, error))
		return FALSE;
	if (from == to)
		return TRUE;
	lrg_weather_asset_move_layer (self->asset, preset, from, to);
	sync_preset (self, preset);
	path = preset_path (preset);
	if (!commit (self, path, "Move Layer", 0, error))
		return FALSE;
	if (self->sel_preset == (gint) preset && self->sel_layer == (gint) from)
		self->sel_layer = (gint) to;
	self->lists_stale = TRUE;
	self->grid_stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_weather_editor_set_layer (LrgEditorWeatherEditor  *self,
                                     guint                    preset,
                                     guint                    layer,
                                     gfloat                   intensity,
                                     gint                     enabled,
                                     guint                    gesture,
                                     GError                 **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_layer (self, preset, layer, error))
		return FALSE;
	if (intensity >= 0.0f)
		lrg_weather_asset_set_layer_intensity (self->asset, preset, layer, CLAMP (intensity, 0.0f, 1.0f));
	if (enabled >= 0)
		lrg_weather_asset_set_layer_enabled (self->asset, preset, layer, enabled != 0);
	sync_preset (self, preset);
	path = g_strdup_printf ("/presets/%u/layers/%u", preset, layer);
	return commit (self, path, enabled >= 0 ? (enabled ? "Enable Layer" : "Disable Layer") : "Set Intensity",
	               gesture, error);
}

gboolean
lrg_editor_weather_editor_set_param (LrgEditorWeatherEditor  *self,
                                     guint                    preset,
                                     guint                    layer,
                                     const gchar             *name,
                                     const gchar             *value,
                                     guint                    gesture,
                                     GError                 **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *label = NULL;
	g_autoptr(GError) err = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (!need_layer (self, preset, layer, error))
		return FALSE;
	if (value == NULL)
	{
		if (!lrg_weather_asset_unset_layer_param (self->asset, preset, layer, name))
			return TRUE;
	}
	else
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, value);
		if (!lrg_weather_asset_set_layer_param (self->asset, preset, layer, name, &v, &err))
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, err->message);
			return FALSE;
		}
	}
	sync_preset (self, preset);
	path = g_strdup_printf ("/presets/%u/layers/%u", preset, layer);
	label = g_strdup_printf (value != NULL ? "Set %s" : "Reset %s", name);
	return commit (self, path, label, gesture, error);
}

/* A rule end: a preset id, "*" or "none". */
static gboolean
check_rule_end (LrgEditorWeatherEditor  *self,
                const gchar             *end,
                GError                 **error)
{
	if (g_strcmp0 (end, "*") == 0 || g_strcmp0 (end, "none") == 0 ||
	    lrg_weather_asset_find_preset (self->asset, end) >= 0)
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "'%s' is not a preset, '*' or 'none'", end);
	return FALSE;
}

gboolean
lrg_editor_weather_editor_set_transition (LrgEditorWeatherEditor  *self,
                                          const gchar             *from,
                                          const gchar             *to,
                                          gfloat                   seconds,
                                          GError                 **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);
	g_return_val_if_fail (from != NULL && to != NULL, FALSE);

	if (!need_asset (self, error) || !check_rule_end (self, from, error) || !check_rule_end (self, to, error))
		return FALSE;
	if (seconds < 0.0f || !isfinite (seconds))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A transition lasts 0 s or more");
		return FALSE;
	}
	lrg_weather_asset_set_transition (self->asset, from, to, seconds);
	return commit (self, "/transitions", "Set Transition", 0, error);
}

gboolean
lrg_editor_weather_editor_remove_transition (LrgEditorWeatherEditor  *self,
                                             const gchar             *from,
                                             const gchar             *to,
                                             GError                 **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);
	g_return_val_if_fail (from != NULL && to != NULL, FALSE);

	if (!need_asset (self, error))
		return FALSE;
	if (!lrg_weather_asset_remove_transition (self->asset, from, to))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No transition %s -> %s", from, to);
		return FALSE;
	}
	return commit (self, "/transitions", "Remove Transition", 0, error);
}

gboolean
lrg_editor_weather_editor_preview_preset (LrgEditorWeatherEditor  *self,
                                          const gchar             *preset_id,
                                          GError                 **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), FALSE);

	if (!need_asset (self, error))
		return FALSE;
	if (preset_id != NULL && lrg_weather_asset_find_preset (self->asset, preset_id) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No preset '%s'", preset_id);
		return FALSE;
	}
	g_free (self->preview_id);
	self->preview_id = g_strdup (preset_id);
	/* a fresh manager: a crossfade leaves the faded weather's effects
	 * scaled down, and an instant switch back would not restore them */
	rebuild_manager (self, FALSE);
	self->lists_stale = TRUE;
	return TRUE;
}

gfloat
lrg_editor_weather_editor_transition_to (LrgEditorWeatherEditor  *self,
                                         const gchar             *preset_id,
                                         GError                 **error)
{
	gfloat            seconds;
	g_autoptr(GError) err = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), -1.0f);

	if (!need_asset (self, error))
		return -1.0f;
	if (preset_id != NULL && lrg_weather_asset_find_preset (self->asset, preset_id) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No preset '%s'", preset_id);
		return -1.0f;
	}
	if (self->manager == NULL)
		rebuild_manager (self, FALSE);
	seconds = lrg_weather_asset_resolve_transition (self->asset, self->preview_id, preset_id);
	if (!lrg_weather_asset_transition (self->asset, self->manager, preset_id, &err))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, err->message);
		return -1.0f;
	}
	g_free (self->preview_id);
	self->preview_id = g_strdup (preset_id);
	self->fade = seconds;
	self->fade_left = seconds;
	self->lists_stale = TRUE;
	return seconds;
}

const gchar *
lrg_editor_weather_editor_get_preview_preset (LrgEditorWeatherEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), NULL);
	return self->preview_id;
}

void
lrg_editor_weather_editor_advance (LrgEditorWeatherEditor *self,
                                   gfloat                  seconds)
{
	gfloat t;

	g_return_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self));

	for (t = 0.0f; t + PREVIEW_STEP * 0.5f < seconds; t += PREVIEW_STEP)
		preview_step (self, PREVIEW_STEP);
}

GrlImage *
lrg_editor_weather_editor_render_preview (LrgEditorWeatherEditor *self,
                                          guint                   width,
                                          guint                   height)
{
	RenderTexture2D rt;
	Image           img;
	GrlImage       *out;

	g_return_val_if_fail (LRG_IS_EDITOR_WEATHER_EDITOR (self), NULL);

	if (width == 0 || height == 0 || !IsWindowReady () || self->view == NULL)
		return NULL;
	rt = LoadRenderTexture ((gint) width, (gint) height);
	if (rt.id == 0)
		return NULL;
	BeginTextureMode (rt);
	draw_scene (self, self->view, (gfloat) width, (gfloat) height);
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
 * IPC: weather.*
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

static gdouble
clean (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.7g", (gdouble) v);
	return g_ascii_strtod (buf, NULL);
}

/* A JSON scalar as file-syntax text; %NULL for null. */
static gchar *
json_value_text (JsonNode *n,
                 gboolean *ok)
{
	*ok = TRUE;
	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
		return NULL;
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

static LrgEditorWeatherEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar            *path = require_string (params, "path", error);
	LrgEditorWeatherEditor *self;
	g_autoptr(GError)       err = NULL;

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
	self = lrg_editor_weather_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

static gboolean
ipc_loaded (LrgEditorWeatherEditor  *self,
            GError                 **error)
{
	if (self->asset != NULL)
		return TRUE;
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s",
	             self->load_error != NULL ? self->load_error : "The weather file does not load");
	return FALSE;
}

/* "preset": an index or a preset id. */
static gint
ipc_preset (LrgEditorWeatherEditor  *self,
            JsonObject              *params,
            GError                 **error)
{
	const gchar *id = param_string (params, "preset");
	gdouble      v;
	gint         p;

	if (!ipc_loaded (self, error))
		return -1;
	if (id != NULL)
	{
		p = lrg_weather_asset_find_preset (self->asset, id);
		if (p < 0)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No preset '%s'", id);
		return p;
	}
	if (!param_number (params, "preset", &v))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing parameter 'preset' (an index or a preset id)");
		return -1;
	}
	if (v < 0 || v >= lrg_weather_asset_get_preset_count (self->asset))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No preset %g", v);
		return -1;
	}
	return (gint) v;
}

/* @member ("layer", "from", "to"): an index or a layer id of @preset. */
static gint
ipc_layer (LrgEditorWeatherEditor  *self,
           guint                    preset,
           JsonObject              *params,
           const gchar             *member,
           GError                 **error)
{
	const gchar *id = param_string (params, member);
	gdouble      v;
	gint         l;

	if (id != NULL)
	{
		l = lrg_weather_asset_find_layer (self->asset, preset, id);
		if (l < 0)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No layer '%s'", id);
		return l;
	}
	if (!param_number (params, member, &v))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing parameter '%s' (an index or a layer id)", member);
		return -1;
	}
	if (v < 0 || v >= lrg_weather_asset_get_layer_count (self->asset, preset))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No layer %g", v);
		return -1;
	}
	return (gint) v;
}

static gchar *
param_text (LrgWeatherAsset *asset,
            guint            preset,
            guint            layer,
            const gchar     *name)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (!lrg_weather_asset_get_layer_param (asset, preset, layer, name, &v))
		return NULL;
	return value_text (&v);
}

static JsonObject *
asset_json (LrgEditorWeatherEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonArray  *presets = json_array_new ();
	JsonArray  *rules = json_array_new ();
	JsonObject *sel = json_object_new ();
	guint       p, l, t;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	else
		json_object_set_null_member (o, "load_error");
	if (self->asset != NULL)
	{
		const gchar *name = lrg_weather_asset_get_name (self->asset);
		const gchar *def = lrg_weather_asset_get_default_preset (self->asset);

		if (name != NULL)
			json_object_set_string_member (o, "name", name);
		else
			json_object_set_null_member (o, "name");
		if (def != NULL)
			json_object_set_string_member (o, "default_preset", def);
		else
			json_object_set_null_member (o, "default_preset");
		json_object_set_double_member (o, "default_transition",
		                               clean (lrg_weather_asset_get_default_transition (self->asset)));
		for (p = 0; p < lrg_weather_asset_get_preset_count (self->asset); p++)
		{
			JsonObject       *po = json_object_new ();
			JsonArray        *layers = json_array_new ();
			GrlColor          c = { 255, 255, 255, 255 };
			gfloat            br = 1.0f;
			g_autofree gchar *hex = NULL;

			lrg_weather_asset_get_preset_ambient (self->asset, p, &c, &br);
			hex = g_strdup_printf ("#%02x%02x%02x", c.r, c.g, c.b);
			json_object_set_int_member (po, "index", p);
			json_object_set_string_member (po, "id", lrg_weather_asset_get_preset_id (self->asset, p));
			if (lrg_weather_asset_get_preset_name (self->asset, p) != NULL)
				json_object_set_string_member (po, "name", lrg_weather_asset_get_preset_name (self->asset, p));
			else
				json_object_set_null_member (po, "name");
			json_object_set_string_member (po, "ambient_color", hex);
			json_object_set_double_member (po, "ambient_brightness", clean (br));
			for (l = 0; l < lrg_weather_asset_get_layer_count (self->asset, p); l++)
			{
				JsonObject   *lo = json_object_new ();
				JsonObject   *params = json_object_new ();
				g_auto(GStrv) names = lrg_weather_asset_get_layer_param_names (self->asset, p, l);
				guint         k;

				json_object_set_int_member (lo, "index", l);
				json_object_set_string_member (lo, "kind", lrg_weather_asset_get_layer_kind (self->asset, p, l));
				json_object_set_string_member (lo, "id", lrg_weather_asset_get_layer_id (self->asset, p, l));
				json_object_set_double_member (lo, "intensity",
				                               clean (lrg_weather_asset_get_layer_intensity (self->asset, p, l)));
				json_object_set_boolean_member (lo, "enabled", lrg_weather_asset_get_layer_enabled (self->asset, p, l));
				for (k = 0; names != NULL && names[k] != NULL; k++)
				{
					g_autofree gchar *text = param_text (self->asset, p, l, names[k]);

					json_object_set_string_member (params, names[k], text != NULL ? text : "");
				}
				json_object_set_object_member (lo, "params", params);
				json_array_add_object_element (layers, lo);
			}
			json_object_set_array_member (po, "layers", layers);
			json_array_add_object_element (presets, po);
		}
		for (t = 0; t < lrg_weather_asset_get_transition_count (self->asset); t++)
		{
			const gchar *from = NULL, *to = NULL;
			gfloat       sec = 0.0f;
			JsonObject  *ro = json_object_new ();

			lrg_weather_asset_get_transition (self->asset, t, &from, &to, &sec);
			json_object_set_string_member (ro, "from", from);
			json_object_set_string_member (ro, "to", to);
			json_object_set_double_member (ro, "duration", clean (sec));
			json_array_add_object_element (rules, ro);
		}
	}
	json_object_set_array_member (o, "presets", presets);
	json_object_set_array_member (o, "transitions", rules);
	json_object_set_int_member (sel, "preset", self->sel_preset);
	json_object_set_int_member (sel, "layer", self->sel_layer);
	json_object_set_object_member (o, "selected", sel);
	if (self->preview_id != NULL)
		json_object_set_string_member (o, "preview", self->preview_id);
	else
		json_object_set_null_member (o, "preview");
	return o;
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject             *o;

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
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	const gchar      *path = require_string (params, "path", error);
	g_autoptr(GError) err = NULL;
	g_autofree gchar *abs = NULL;
	JsonObject       *o;

	if (path == NULL)
		return NULL;
	if (!lrg_editor_weather_editor_create (app, path, param_string (params, "template"), &err))
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
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (asset_json (self)) : NULL;
}

/* One parameter spec as JSON: name, type, blurb, default, range, nicks. */
static JsonObject *
spec_json (GParamSpec *spec)
{
	JsonObject       *o = json_object_new ();
	g_auto(GValue)    def = G_VALUE_INIT;
	g_autofree gchar *dtext = NULL;

	json_object_set_string_member (o, "name", spec->name);
	json_object_set_string_member (o, "type", g_type_name (spec->value_type));
	json_object_set_string_member (o, "blurb", g_param_spec_get_blurb (spec) != NULL ? g_param_spec_get_blurb (spec)
	                                                                                 : "");
	g_value_init (&def, spec->value_type);
	g_param_value_set_default (spec, &def);
	dtext = value_text (&def);
	if (dtext != NULL)
		json_object_set_string_member (o, "default", dtext);
	else
		json_object_set_null_member (o, "default");
	if (G_IS_PARAM_SPEC_FLOAT (spec))
	{
		json_object_set_double_member (o, "min", clean (G_PARAM_SPEC_FLOAT (spec)->minimum));
		json_object_set_double_member (o, "max", clean (G_PARAM_SPEC_FLOAT (spec)->maximum));
	}
	else if (G_IS_PARAM_SPEC_DOUBLE (spec))
	{
		json_object_set_double_member (o, "min", G_PARAM_SPEC_DOUBLE (spec)->minimum);
		json_object_set_double_member (o, "max", G_PARAM_SPEC_DOUBLE (spec)->maximum);
	}
	else if (G_IS_PARAM_SPEC_INT (spec))
	{
		json_object_set_int_member (o, "min", G_PARAM_SPEC_INT (spec)->minimum);
		json_object_set_int_member (o, "max", G_PARAM_SPEC_INT (spec)->maximum);
	}
	else if (G_IS_PARAM_SPEC_UINT (spec))
	{
		json_object_set_int_member (o, "min", G_PARAM_SPEC_UINT (spec)->minimum);
		json_object_set_int_member (o, "max", G_PARAM_SPEC_UINT (spec)->maximum);
	}
	else if (G_IS_PARAM_SPEC_ENUM (spec))
	{
		GEnumClass *ec = G_PARAM_SPEC_ENUM (spec)->enum_class;
		JsonArray  *values = json_array_new ();
		guint       k;

		for (k = 0; k < ec->n_values; k++)
			json_array_add_string_element (values, ec->values[k].value_nick);
		json_object_set_array_member (o, "values", values);
	}
	return o;
}

static JsonNode *
m_kinds (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	const gchar  *only = param_string (params, "kind");
	g_auto(GStrv) kinds = lrg_weather_asset_list_kinds ();
	JsonObject   *o = json_object_new ();
	JsonArray    *a = json_array_new ();
	guint         i;

	if (only != NULL && lrg_weather_asset_lookup_kind (only) == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "Unknown layer kind '%s'",
		             only);
		json_array_unref (a);
		json_object_unref (o);
		return NULL;
	}
	for (i = 0; kinds != NULL && kinds[i] != NULL; i++)
	{
		JsonObject             *k;
		JsonArray              *ps;
		guint                   n = 0, j;
		g_autofree GParamSpec **specs = NULL;

		if (only != NULL && g_strcmp0 (only, kinds[i]) != 0)
			continue;
		k = json_object_new ();
		ps = json_array_new ();
		specs = lrg_weather_asset_list_kind_params (kinds[i], &n);
		for (j = 0; specs != NULL && j < n; j++)
			json_array_add_object_element (ps, spec_json (specs[j]));
		json_object_set_string_member (k, "kind", kinds[i]);
		json_object_set_array_member (k, "params", ps);
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
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar            *key;
	g_autofree gchar       *value = NULL;
	g_autoptr(GError)       err = NULL;
	gboolean                ok;

	if (self == NULL || (key = require_string (params, "key", error)) == NULL || !ipc_loaded (self, error))
		return NULL;
	value = json_value_text (json_object_get_member (params, "value"), &ok);
	if (!ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'value' must be a string, a number or null");
		return NULL;
	}
	if (!lrg_editor_weather_editor_set_setting (self, key, value, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_add_preset (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gint                    index;
	JsonObject             *o;

	if (self == NULL || !ipc_loaded (self, error))
		return NULL;
	index = lrg_editor_weather_editor_add_preset (self, param_string (params, "id"), param_string (params, "name"),
	                                              &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	json_object_set_string_member (o, "id", lrg_weather_asset_get_preset_id (self->asset, (guint) index));
	return object_node (o);
}

static JsonNode *
m_remove_preset (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gint                    p;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0)
		return NULL;
	if (!lrg_editor_weather_editor_remove_preset (self, (guint) p, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_set_preset (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *hex;
	GrlColor                c;
	gboolean                has_color = FALSE;
	gdouble                 br = -1.0;
	gint                    p;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0)
		return NULL;
	hex = param_string (params, "ambient_color");
	if (hex != NULL)
	{
		guint r, g, b;

		if (hex[0] != '#' || (strlen (hex) != 7 && strlen (hex) != 9) || sscanf (hex + 1, "%02x%02x%02x", &r, &g, &b) != 3)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "'ambient_color' must be \"#rrggbb\"");
			return NULL;
		}
		c.r = (guint8) r;
		c.g = (guint8) g;
		c.b = (guint8) b;
		c.a = 255;
		has_color = TRUE;
	}
	param_number (params, "ambient_brightness", &br);
	if (!lrg_editor_weather_editor_set_preset (self, (guint) p, param_string (params, "name"),
	                                           has_color ? &c : NULL, (gfloat) br, 0, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_add_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *kind;
	gint                    p, index;
	JsonObject             *o;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0 ||
	    (kind = require_string (params, "kind", error)) == NULL)
		return NULL;
	index = lrg_editor_weather_editor_add_layer (self, (guint) p, kind, param_string (params, "id"), &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	json_object_set_string_member (o, "id", lrg_weather_asset_get_layer_id (self->asset, (guint) p, (guint) index));
	return object_node (o);
}

static JsonNode *
m_remove_layer (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gint                    p, l;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0 ||
	    (l = ipc_layer (self, (guint) p, params, "layer", error)) < 0)
		return NULL;
	if (!lrg_editor_weather_editor_remove_layer (self, (guint) p, (guint) l, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_move_layer (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gint                    p, from, to;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0 ||
	    (from = ipc_layer (self, (guint) p, params, "from", error)) < 0 ||
	    (to = ipc_layer (self, (guint) p, params, "to", error)) < 0)
		return NULL;
	if (!lrg_editor_weather_editor_move_layer (self, (guint) p, (guint) from, (guint) to, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_set_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gdouble                 intensity = -1.0;
	gboolean                enabled = TRUE, has_enabled;
	gint                    p, l;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0 ||
	    (l = ipc_layer (self, (guint) p, params, "layer", error)) < 0)
		return NULL;
	param_number (params, "intensity", &intensity);
	has_enabled = param_bool (params, "enabled", &enabled);
	if (!lrg_editor_weather_editor_set_layer (self, (guint) p, (guint) l, (gfloat) intensity,
	                                          has_enabled ? (enabled ? 1 : 0) : -1, 0, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_set_param (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	g_autofree gchar       *value = NULL;
	const gchar            *name;
	gboolean                ok;
	gint                    p, l;

	if (self == NULL || (p = ipc_preset (self, params, error)) < 0 ||
	    (l = ipc_layer (self, (guint) p, params, "layer", error)) < 0 ||
	    (name = require_string (params, "name", error)) == NULL)
		return NULL;
	if (!json_object_has_member (params, "value"))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing parameter 'value' (null unsets it)");
		return NULL;
	}
	value = json_value_text (json_object_get_member (params, "value"), &ok);
	if (!ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'value' must be a string, a number, a boolean or null");
		return NULL;
	}
	if (!lrg_editor_weather_editor_set_param (self, (guint) p, (guint) l, name, value, 0, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_set_transition (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *from, *to;
	gdouble                 sec;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL)
		return NULL;
	if (!param_number (params, "duration", &sec))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing number parameter 'duration'");
		return NULL;
	}
	if (!lrg_editor_weather_editor_set_transition (self, from, to, (gfloat) sec, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node ();
}

static JsonNode *
m_remove_transition (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *from, *to;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL)
		return NULL;
	if (!lrg_editor_weather_editor_remove_transition (self, from, to, &err))
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
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gint                    p = -1, l = -1;
	gdouble                 v;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if (json_object_has_member (params, "preset") && !(param_number (params, "preset", &v) && v < 0) &&
	    !JSON_NODE_HOLDS_NULL (json_object_get_member (params, "preset")))
	{
		if ((p = ipc_preset (self, params, error)) < 0)
			return NULL;
		if (json_object_has_member (params, "layer") && !(param_number (params, "layer", &v) && v < 0) &&
		    (l = ipc_layer (self, (guint) p, params, "layer", error)) < 0)
			return NULL;
	}
	lrg_editor_weather_editor_select (self, p, l);
	o = json_object_new ();
	json_object_set_int_member (o, "preset", self->sel_preset);
	json_object_set_int_member (o, "layer", self->sel_layer);
	return object_node (o);
}

static JsonObject *
preview_json (LrgEditorWeatherEditor *self)
{
	JsonObject *o = json_object_new ();
	LrgWeather *w = self->manager != NULL ? lrg_weather_manager_get_active_weather (self->manager) : NULL;
	gfloat      wx = 0.0f, wy = 0.0f;

	if (self->preview_id != NULL)
		json_object_set_string_member (o, "active", self->preview_id);
	else
		json_object_set_null_member (o, "active");
	json_object_set_boolean_member (o, "transitioning",
	                                self->manager != NULL && lrg_weather_manager_is_transitioning (self->manager));
	json_object_set_double_member (o, "duration", clean (self->fade));
	json_object_set_double_member (o, "remaining", clean (self->fade_left));
	if (w != NULL)
		lrg_weather_get_effective_wind (w, &wx, &wy);
	json_object_set_double_member (o, "wind_x", clean (wx));
	json_object_set_double_member (o, "wind_y", clean (wy));
	return o;
}

static JsonNode *
m_preview (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gdouble                 adv;

	if (self == NULL || !ipc_loaded (self, error))
		return NULL;
	if (json_object_has_member (params, "preset"))
	{
		const gchar *id = param_string (params, "preset");

		if (!lrg_editor_weather_editor_preview_preset (self, g_strcmp0 (id, "none") == 0 ? NULL : id, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	if (json_object_has_member (params, "transition_to"))
	{
		const gchar *id = param_string (params, "transition_to");

		if (lrg_editor_weather_editor_transition_to (self, g_strcmp0 (id, "none") == 0 ? NULL : id, &err) < 0.0f)
		{
			fail (error, err);
			return NULL;
		}
	}
	if (param_number (params, "advance", &adv) && adv > 0.0)
		lrg_editor_weather_editor_advance (self, (gfloat) MIN (adv, 600.0));
	return object_node (preview_json (self));
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorWeatherEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

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
	LrgEditorApp           *app = LRG_EDITOR_APP (user_data);
	LrgEditorWeatherEditor *self = ipc_editor (app, params, error);
	const gchar            *file;
	g_autoptr(GrlImage)     image = NULL;
	g_autofree gchar       *abs = NULL;
	gdouble                 w = 640.0, h = 360.0;
	JsonObject             *o;

	if (self == NULL || (file = require_string (params, "file", error)) == NULL)
		return NULL;
	param_number (params, "width", &w);
	param_number (params, "height", &h);
	image = lrg_editor_weather_editor_render_preview (self, (guint) CLAMP (w, 16.0, 4096.0),
	                                                  (guint) CLAMP (h, 16.0, 4096.0));
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
lrg_editor_weather_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "weather.open", m_open },
		{ "weather.create", m_create },
		{ "weather.get", m_get },
		{ "weather.kinds", m_kinds },
		{ "weather.set", m_set },
		{ "weather.add_preset", m_add_preset },
		{ "weather.remove_preset", m_remove_preset },
		{ "weather.set_preset", m_set_preset },
		{ "weather.add_layer", m_add_layer },
		{ "weather.remove_layer", m_remove_layer },
		{ "weather.move_layer", m_move_layer },
		{ "weather.set_layer", m_set_layer },
		{ "weather.set_param", m_set_param },
		{ "weather.set_transition", m_set_transition },
		{ "weather.remove_transition", m_remove_transition },
		{ "weather.select", m_select },
		{ "weather.preview", m_preview },
		{ "weather.save", m_save },
		{ "weather.render", m_render }
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
