/* lrg-editor-data-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDataEditor: the base panel of the data editors (see the header).
 *
 * Layout, top to bottom:
 *
 *   toolbar   title (format icon, file name, dirty dot) | Save Revert
 *             Validate | issue chip | subclass tools
 *   banner    "<file> changed on disk"  Reload  Keep mine   (when shown)
 *   content   the subclass widget
 *   issues    a list of the validation issues                (when open)
 *
 * The toolbar, banner and issues backgrounds are painted by the base
 * itself before the children (LrgWidgetClass.paint chains up last).
 *
 * Validation runs on open, save, reload, Validate and, debounced, half a
 * second (of canvas time, so tests stay deterministic) after the last
 * edit, from the step() vfunc.
 *
 * Registry. A process-wide table maps LrgEditorDocumentFormat to the
 * GType of its editor; the built-in editors are registered lazily on the
 * first use and only when their type really derives from this class.
 *
 * Open editors. Per app, object data "lrg-editor-data-editors" holds a
 * DataSet: the editors in opening order, the focused one and whether the
 * IPC methods are installed. Panels are docked with the id
 * "data:<absolute path>" through lrg_editor_app_add_panel(), so the app's
 * panel table owns them; closing removes them from both.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-data-editor.h"
#include "lrg-editor-input-map-editor.h"
#include "lrg-editor-locale-editor.h"
#include "lrg-editor-sound-bank-editor.h"
#include "lrg-editor-theme-editor.h"
#include "lrg-editor-hud-layout-editor.h"
#include "lrg-editor-ui-layout-editor.h"
#include "lrg-editor-dialog-tree-editor.h"
#include "lrg-editor-dungeon-editor.h"
#include "lrg-editor-material-editor.h"
#include "lrg-editor-post-stack-editor.h"
#include "lrg-editor-anim-graph-editor.h"
#include "lrg-editor-bone-map-editor.h"
#include "lrg-editor-skeleton2d-editor.h"
#include "lrg-editor-physics-asset-editor.h"
#include "lrg-editor-bt-editor.h"
#include "lrg-editor-fsm-editor.h"
#include "lrg-editor-particle-editor.h"
#include "lrg-editor-weather-editor.h"
#include "lrg-editor-mixer-editor.h"
#include "lrg-editor-tileset-editor.h"
#include "lrg-editor-sprite-editor.h"
#include "lrg-editor-water-editor.h"
#include "../lrg-editor-shader-graph-editor.h"
#include "lrg-editor-vscript-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-list-view.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-dialog.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../ui/lrg-ui-actions.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <string.h>

#define SET_KEY           "lrg-editor-data-editors"
#define DIALOG_KEY        "lrg-editor-dialog"
#define DIRTY_MARK        " \xe2\x80\xa2"        /* " •" */
#define REVALIDATE_DELAY  (0.5f)
#define MAX_ISSUE_ROWS    (6)
#define MOD_MASK          (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL | \
                           LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)

/* Responses of the prompts (LrgDialog's own are <= 0) */
enum
{
	RESPONSE_SAVE = 1,
	RESPONSE_DISCARD = 2,
	RESPONSE_REVERT = 3
};

/* ==========================================================================
 * Issue rows: a tiny GObject around an LrgEditorDocumentIssue for the
 * GListStore behind the issues list.
 * ========================================================================== */

#define DATA_TYPE_ISSUE_ROW (data_issue_row_get_type ())
G_DECLARE_FINAL_TYPE (DataIssueRow, data_issue_row, DATA, ISSUE_ROW, GObject)

struct _DataIssueRow
{
	GObject                 parent_instance;
	LrgEditorDocumentIssue *issue;
	guint                   index;
};

G_DEFINE_FINAL_TYPE (DataIssueRow, data_issue_row, G_TYPE_OBJECT)

static void
data_issue_row_finalize (GObject *object)
{
	DataIssueRow *self = DATA_ISSUE_ROW (object);

	g_clear_pointer (&self->issue, lrg_editor_document_issue_free);
	G_OBJECT_CLASS (data_issue_row_parent_class)->finalize (object);
}

static void
data_issue_row_class_init (DataIssueRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = data_issue_row_finalize;
}

static void
data_issue_row_init (DataIssueRow *self)
{
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

enum
{
	PROP_0,
	PROP_APP,
	PROP_DOCUMENT,
	PROP_TITLE,
	PROP_N_ISSUES,
	N_PROPS
};

enum
{
	SIGNAL_REVEAL,
	SIGNAL_CLOSED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

typedef struct
{
	LrgEditorApp      *app;          /* weak */
	LrgEditorDocument *document;
	gchar             *panel_id;
	gchar             *title;

	/* toolbar */
	LrgHBox           *toolbar;
	LrgWidget         *title_widget;
	LrgWidget         *save_chip;
	LrgWidget         *revert_chip;
	LrgWidget         *validate_chip;
	LrgWidget         *issues_chip;

	/* banner */
	LrgHBox           *banner;
	LrgWidget         *banner_label;     /* custom: clipped text */
	gchar             *banner_text;
	gboolean           banner_visible;

	/* content and issues */
	LrgWidget         *content;
	LrgListView       *issues_view;
	GListStore        *issues_model;
	GPtrArray         *issues;       /* LrgEditorDocumentIssue* */
	gboolean           issues_open;
	guint              n_errors;
	guint              n_warnings;

	/* debounced validation */
	gboolean           stale;
	gfloat             stale_time;
	gboolean           closing;
} LrgEditorDataEditorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgEditorDataEditor, lrg_editor_data_editor, LRG_TYPE_CONTAINER)

#define PRIV(o) ((LrgEditorDataEditorPrivate *) lrg_editor_data_editor_get_instance_private (o))

/* The editors of one app */
typedef struct
{
	GPtrArray           *editors;    /* LrgEditorDataEditor* (borrowed: the app's panel table owns them) */
	LrgEditorDataEditor *focused;    /* borrowed */
	gboolean             installed;
	gulong               close_id;
	gulong               closed_id;
	LrgDockArea         *dock;       /* weak */
} DataSet;

static void update_title (LrgEditorDataEditor *self);
static void update_issue_chip (LrgEditorDataEditor *self);
static void set_banner_visible (LrgEditorDataEditor *self, gboolean visible);

/* ==========================================================================
 * Registry
 * ========================================================================== */

static GHashTable *registry;     /* GINT_TO_POINTER (format) -> GSIZE_TO_POINTER (type) */

static void
registry_add_builtin (LrgEditorDocumentFormat format,
                      GType                   type)
{
	/* Stub types of editors still being written are skipped */
	if (g_type_is_a (type, LRG_TYPE_EDITOR_DATA_EDITOR) &&
	    !g_hash_table_contains (registry, GINT_TO_POINTER (format)))
		g_hash_table_insert (registry, GINT_TO_POINTER (format), GSIZE_TO_POINTER (type));
}

static void
registry_ensure (void)
{
	if (registry != NULL)
		return;
	registry = g_hash_table_new (g_direct_hash, g_direct_equal);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP, LRG_TYPE_EDITOR_INPUT_MAP_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_LOCALE, LRG_TYPE_EDITOR_LOCALE_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK, LRG_TYPE_EDITOR_SOUND_BANK_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_THEME, LRG_TYPE_EDITOR_THEME_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT, LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT, LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE, LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON, LRG_TYPE_EDITOR_DUNGEON_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL, LRG_TYPE_EDITOR_MATERIAL_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK, LRG_TYPE_EDITOR_POST_STACK_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH, LRG_TYPE_EDITOR_SHADER_GRAPH_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH, LRG_TYPE_EDITOR_ANIM_GRAPH_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP, LRG_TYPE_EDITOR_BONE_MAP_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D, LRG_TYPE_EDITOR_SKELETON2D_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET, LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR);
	/*
	 * The 0.9.0 M6/M7 engine-backed formats.  Their editors are written
	 * in parallel; until one derives from LrgEditorDataEditor its stub
	 * GType is skipped by registry_add_builtin() and the format opens
	 * nowhere (can_open() says no), never in the wrong editor.
	 */
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_BT, LRG_TYPE_EDITOR_BT_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_FSM, LRG_TYPE_EDITOR_FSM_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES, LRG_TYPE_EDITOR_PARTICLE_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_WEATHER, LRG_TYPE_EDITOR_WEATHER_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_MIXER, LRG_TYPE_EDITOR_MIXER_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_TILESET, LRG_TYPE_EDITOR_TILESET_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_SPRITE, LRG_TYPE_EDITOR_SPRITE_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_WATER, LRG_TYPE_EDITOR_WATER_EDITOR);
	registry_add_builtin (LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT, LRG_TYPE_EDITOR_VSCRIPT_EDITOR);
	/* .rseq files are opened by the docked sequencer panel and .rnav bake
	 * settings by the Navigation panel (action nav.open-settings), not a
	 * data editor: SEQUENCE and NAV_SETTINGS have no builtin here */
}

void
lrg_editor_data_editor_register_type (LrgEditorDocumentFormat format,
                                      GType                   type)
{
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_EDITOR_DATA_EDITOR));
	g_return_if_fail (format != LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);

	registry_ensure ();
	g_hash_table_replace (registry, GINT_TO_POINTER (format), GSIZE_TO_POINTER (type));
}

GType
lrg_editor_data_editor_lookup_type (LrgEditorDocumentFormat format)
{
	registry_ensure ();
	return (GType) GPOINTER_TO_SIZE (g_hash_table_lookup (registry, GINT_TO_POINTER (format)));
}

/* Detects the format of @path from its name and its first bytes. */
static LrgEditorDocumentFormat
detect_file_format (const gchar *path)
{
	g_autofree gchar *data = NULL;
	gsize             len = 0;

	if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
		return lrg_editor_document_detect_format (path, NULL, 0);
	if (!g_file_get_contents (path, &data, &len, NULL))
		return lrg_editor_document_detect_format (path, NULL, 0);
	return lrg_editor_document_detect_format (path, data, (gssize) len);
}

gboolean
lrg_editor_data_editor_can_open (const gchar *path)
{
	LrgEditorDocumentFormat format;

	g_return_val_if_fail (path != NULL, FALSE);

	/* data files are .yaml / .yml (levels, prefabs and projects have
	 * their own extensions and editors), plus the engine assets edited
	 * on documents: every extension lrg_editor_document_format_get_extension()
	 * knows (.rmat, .rpost, .rshadergraph, .ranim, .rbonemap, .rseq,
	 * .rskel2d, .rphysasset, .rbt, .rfsm, .rparticles, .rweather,
	 * .rmixer, .rtileset, .rsprite, .rwater, .rnav) */
	if (!g_str_has_suffix (path, ".yaml") && !g_str_has_suffix (path, ".yml") &&
	    lrg_editor_document_format_get_extension (lrg_editor_document_detect_format (path, NULL, 0)) == NULL)
		return FALSE;
	format = detect_file_format (path);
	return format != LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN &&
	       lrg_editor_data_editor_lookup_type (format) != G_TYPE_INVALID;
}

/* ==========================================================================
 * The per-app set
 * ========================================================================== */

static void
data_set_free (gpointer data)
{
	DataSet *set = data;

	if (set->dock != NULL)
	{
		g_clear_signal_handler (&set->close_id, set->dock);
		g_clear_signal_handler (&set->closed_id, set->dock);
		g_clear_weak_pointer (&set->dock);
	}
	g_ptr_array_unref (set->editors);
	g_free (set);
}

static DataSet *
data_set (LrgEditorApp *app)
{
	DataSet *set = g_object_get_data (G_OBJECT (app), SET_KEY);

	if (set == NULL)
	{
		set = g_new0 (DataSet, 1);
		set->editors = g_ptr_array_new ();
		g_object_set_data_full (G_OBJECT (app), SET_KEY, set, data_set_free);
	}
	return set;
}

/* Forgets @self in its app's set (called once the tab is gone). */
static void
data_set_forget (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	DataSet                    *set;

	if (priv->app == NULL)
		return;
	set = g_object_get_data (G_OBJECT (priv->app), SET_KEY);
	if (set == NULL)
		return;
	g_ptr_array_remove (set->editors, self);
	if (set->focused == self)
		set->focused = (set->editors->len > 0) ? g_ptr_array_index (set->editors, set->editors->len - 1) : NULL;
}

static void
set_focused (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	if (priv->app != NULL)
		data_set (priv->app)->focused = self;
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgIcon
severity_icon (LrgEditorProblemSeverity severity)
{
	switch (severity)
	{
	case LRG_EDITOR_PROBLEM_ERROR:
		return LRG_ICON_CROSS;
	case LRG_EDITOR_PROBLEM_WARNING:
		return LRG_ICON_WARNING;
	default:
		return LRG_ICON_INFO;
	}
}

static LrgThemeColor
severity_color (LrgEditorProblemSeverity severity)
{
	switch (severity)
	{
	case LRG_EDITOR_PROBLEM_ERROR:
		return LRG_THEME_COLOR_DANGER;
	case LRG_EDITOR_PROBLEM_WARNING:
		return LRG_THEME_COLOR_WARNING;
	default:
		return LRG_THEME_COLOR_LINK;
	}
}

/* The icon of a format, used by the title. */
static LrgIcon
format_icon (LrgEditorDocumentFormat format)
{
	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP:
		return LRG_ICON_GEAR;
	case LRG_EDITOR_DOCUMENT_FORMAT_LOCALE:
		return LRG_ICON_SEARCH;
	case LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK:
		return LRG_ICON_PLAY;
	case LRG_EDITOR_DOCUMENT_FORMAT_THEME:
		return LRG_ICON_STAR;
	case LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE:
		return LRG_ICON_USER;
	case LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON:
		return LRG_ICON_HOME;
	case LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL:
		return LRG_ICON_STAR_HALF;
	case LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK:
		return LRG_ICON_EYE;
	case LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH:
		return LRG_ICON_MENU;
	case LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH:
		return LRG_ICON_REFRESH;
	case LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP:
		return LRG_ICON_ARROW_RIGHT;
	case LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE:
		return LRG_ICON_CLOCK;
	case LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D:
		return LRG_ICON_EDIT;
	case LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET:
		return LRG_ICON_USER;
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
		return LRG_ICON_MENU;
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
		return LRG_ICON_STAR;
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
		return LRG_ICON_EYE;
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
		return LRG_ICON_PLAY;
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
		return LRG_ICON_EDIT;
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
		return LRG_ICON_REFRESH;
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
		return LRG_ICON_ARROW_RIGHT;
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
		return LRG_ICON_MENU;
	default:
		return LRG_ICON_FILE;
	}
}

/* Human names of the formats for the title ("Input map") */
static const gchar *
format_label (LrgEditorDocumentFormat format)
{
	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP:
		return "Input map";
	case LRG_EDITOR_DOCUMENT_FORMAT_LOCALE:
		return "Locale";
	case LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK:
		return "Sound bank";
	case LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT:
		return "HUD layout";
	case LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT:
		return "UI layout";
	case LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE:
		return "Dialog tree";
	case LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON:
		return "Dungeon";
	case LRG_EDITOR_DOCUMENT_FORMAT_THEME:
		return "Theme";
	case LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL:
		return "Material";
	case LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK:
		return "Post stack";
	case LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH:
		return "Shader graph";
	case LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH:
		return "Animation graph";
	case LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP:
		return "Bone map";
	case LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE:
		return "Sequence";
	case LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D:
		return "2D skeleton";
	case LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET:
		return "Physics asset";
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
		return "Behaviour tree";
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
		return "State machine";
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
		return "Particle effect";
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
		return "Weather";
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
		return "Mixer";
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
		return "Tileset";
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
		return "Sprite sheet";
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
		return "Water body";
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
		return "Navigation settings";
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
		return "Visual script";
	default:
		return "Data";
	}
}

static LrgDockPanel *
dock_panel (LrgEditorDataEditor *self)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (self));

	return (parent != NULL && LRG_IS_DOCK_PANEL (parent)) ? LRG_DOCK_PANEL (parent) : NULL;
}

/* ==========================================================================
 * Title widget: format icon, file name, dirty dot, format name
 * ========================================================================== */

static void
title_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat               small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat               w1 = 0.0f, w2 = 0.0f, h = 0.0f;

	lrg_paint_measure_text (font, lrg_editor_document_get_display_name (priv->document), fs, 0.0f, &w1, &h);
	lrg_paint_measure_text (font, format_label (lrg_editor_document_get_format (priv->document)), small, 0.0f,
	                        &w2, &h);
	size->width = fs + 8.0f + w1 + 18.0f + w2 + 8.0f;
	size->height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
}

static void
title_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat               small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor      *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const gchar         *name = lrg_editor_document_get_display_name (priv->document);
	LrgEditorDocumentFormat format = lrg_editor_document_get_format (priv->document);
	gfloat               cx = x + 2.0f;
	gfloat               tw = 0.0f, th = 0.0f;
	gfloat               icon = fs + 2.0f;

	/* format icon in the accent colour */
	lrg_paint_list_icon (list, format_icon (format), cx, y + (height - icon) * 0.5f, icon, 0.0f, accent);
	cx += icon + 8.0f;

	/* file name, then the dirty dot (or a hollow ring when clean) */
	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_measure_text (font, name, fs, 0.0f, &tw, &th);
	lrg_paint_list_text (list, font, name, cx, y + (height - fs) * 0.5f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	cx += tw + 9.0f;
	if (lrg_editor_data_editor_get_dirty (self))
		lrg_paint_list_circle (list, cx, y + height * 0.5f, 4.0f, lrg_theme_get_color (theme,
		                                                                               LRG_THEME_COLOR_WARNING));
	cx += 9.0f;

	/* format name, muted */
	lrg_paint_list_text (list, font, format_label (format), cx, y + (height - small) * 0.5f + 1.0f, small, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

/* The banner message: a warning icon and the text, clipped to the slot. */
static void
banner_text_paint (LrgCustomWidget *widget,
                   LrgPaintList    *list,
                   gfloat           x,
                   gfloat           y,
                   gfloat           width,
                   gfloat           height,
                   gpointer         user_data)
{
	LrgEditorDataEditorPrivate *priv = PRIV (LRG_EDITOR_DATA_EDITOR (user_data));
	LrgTheme                   *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                      fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat                      icon = fs + 2.0f;

	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_list_icon (list, LRG_ICON_WARNING, x + 4.0f, y + (height - icon) * 0.5f, icon, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
	if (priv->banner_text != NULL)
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), priv->banner_text, x + icon + 12.0f,
		                     y + (height - fs) * 0.5f, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Issue rows
 * ========================================================================== */

static void
issue_row_paint (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	DataIssueRow   *row = g_object_get_data (G_OBJECT (widget), "lrg-data-issue");
	LrgWidget      *w = LRG_WIDGET (widget);
	LrgTheme       *theme = lrg_widget_get_theme (w);
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	GrlFont        *mono = editor_panel_mono_font (w);
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat          icon = MIN (height - 6.0f, fs + 2.0f);
	gfloat          ty = y + (height - fs) * 0.5f;
	gfloat          cx = x + 8.0f;
	gfloat          right = x + width - 8.0f;
	const GrlColor *tint;
	gfloat          tw = 0.0f, th = 0.0f;
	g_autofree gchar *where = NULL;

	if (row == NULL || row->issue == NULL)
		return;
	tint = lrg_theme_get_color (theme, severity_color (row->issue->severity));

	/* severity icon */
	lrg_paint_list_icon (list, severity_icon (row->issue->severity), cx, y + (height - icon) * 0.5f, icon, 0.0f,
	                     tint);
	cx += icon + 8.0f;

	/* "line N" chip on the right */
	if (row->issue->line > 0)
	{
		GrlColor bg;

		where = g_strdup_printf ("line %u", row->issue->line);
		lrg_paint_measure_text (mono, where, fs * 0.9f, 0.0f, &tw, &th);
		lrg_paint_color_with_alpha (tint, 0.16f, &bg);
		lrg_paint_list_rect (list, right - tw - 12.0f, y + 4.0f, tw + 12.0f, height - 8.0f, (height - 8.0f) * 0.5f,
		                     &bg);
		lrg_paint_list_text (list, mono, where, right - tw - 6.0f, y + (height - fs * 0.9f) * 0.5f, fs * 0.9f,
		                     0.0f, tint);
		right -= tw + 20.0f;
	}

	lrg_paint_list_push_clip (list, cx, y, MAX (right - cx, 0.0f), height);
	/* the YAML path in mono, then the message */
	if (row->issue->path != NULL && *row->issue->path != '\0')
	{
		lrg_paint_measure_text (mono, row->issue->path, fs, 0.0f, &tw, &th);
		tw = MIN (tw, (right - cx) * 0.4f);
		lrg_paint_list_push_clip (list, cx, y, tw, height);
		lrg_paint_list_text (list, mono, row->issue->path, cx, ty, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK));
		lrg_paint_list_pop_clip (list);
		cx += tw + 10.0f;
	}
	lrg_paint_list_text (list, font, row->issue->message != NULL ? row->issue->message : "", cx, ty, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	lrg_paint_list_pop_clip (list);
}

static LrgWidget *
issue_row_create (LrgListView *view,
                  gpointer     user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, issue_row_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *widget = lrg_custom_widget_new ();

	lrg_custom_widget_set_funcs (widget, &funcs, NULL, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (widget), TRUE);
	return LRG_WIDGET (widget);
}

static void
issue_row_bind (LrgListView *view,
                LrgWidget   *widget,
                GObject     *item,
                guint        position,
                gpointer     user_data)
{
	g_object_set_data_full (G_OBJECT (widget), "lrg-data-issue", g_object_ref (item), g_object_unref);
}

/* A click selects and reveals; a double click (row-activated) too. */
static void
on_issue_selection (LrgListView *view,
                    gpointer     user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	guint                pos = lrg_list_view_get_selected (view);

	if (pos != G_MAXUINT && pos != (guint) -1)
		lrg_editor_data_editor_activate_issue (self, pos);
}

static void
on_issue_activated (LrgListView *view,
                    guint        position,
                    gpointer     user_data)
{
	lrg_editor_data_editor_activate_issue (LRG_EDITOR_DATA_EDITOR (user_data), position);
}

/* ==========================================================================
 * Validation
 * ========================================================================== */

static void
rebuild_issue_model (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	guint                       i;

	g_list_store_remove_all (priv->issues_model);
	for (i = 0; i < priv->issues->len; i++)
	{
		DataIssueRow *row = g_object_new (DATA_TYPE_ISSUE_ROW, NULL);

		row->issue = lrg_editor_document_issue_copy (g_ptr_array_index (priv->issues, i));
		row->index = i;
		g_list_store_append (priv->issues_model, row);
		g_object_unref (row);
	}
}

guint
lrg_editor_data_editor_validate (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv;
	LrgEditorDataEditorClass   *klass;
	g_autoptr(GPtrArray)        issues = NULL;
	guint                       i;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), 0);
	priv = PRIV (self);
	klass = LRG_EDITOR_DATA_EDITOR_GET_CLASS (self);

	/* the document's checks, then the subclass's */
	lrg_editor_document_validate (priv->document, &issues);
	if (issues == NULL)
		issues = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
	if (klass->validate != NULL)
		klass->validate (self, issues);

	/* refill in place: callers may hold the array */
	g_ptr_array_set_size (priv->issues, 0);
	for (i = 0; i < issues->len; i++)
		g_ptr_array_add (priv->issues, lrg_editor_document_issue_copy (g_ptr_array_index (issues, i)));
	priv->n_errors = 0;
	priv->n_warnings = 0;
	for (i = 0; i < priv->issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (priv->issues, i);

		if (issue->severity == LRG_EDITOR_PROBLEM_ERROR)
			priv->n_errors++;
		else if (issue->severity == LRG_EDITOR_PROBLEM_WARNING)
			priv->n_warnings++;
	}
	priv->stale = FALSE;
	rebuild_issue_model (self);
	update_issue_chip (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ISSUES]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	return priv->n_errors;
}

static void
update_issue_chip (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	g_autofree gchar           *label = NULL;
	g_autofree gchar           *badge = NULL;

	if (priv->issues_chip == NULL)
		return;
	if (priv->issues->len == 0)
	{
		editor_chip_set_icon (priv->issues_chip, LRG_ICON_CHECK);
		editor_chip_set_tint (priv->issues_chip, LRG_THEME_COLOR_SUCCESS);
		editor_chip_set_label (priv->issues_chip, priv->stale ? "Checking" : "Valid");
		editor_chip_set_badge (priv->issues_chip, NULL);
		return;
	}
	editor_chip_set_icon (priv->issues_chip, priv->n_errors > 0 ? LRG_ICON_CROSS
	                                         : priv->n_warnings > 0 ? LRG_ICON_WARNING : LRG_ICON_INFO);
	editor_chip_set_tint (priv->issues_chip, priv->n_errors > 0 ? LRG_THEME_COLOR_DANGER
	                                         : priv->n_warnings > 0 ? LRG_THEME_COLOR_WARNING : LRG_THEME_COLOR_LINK);
	label = g_strdup (priv->issues->len == 1 ? "Issue" : "Issues");
	badge = g_strdup_printf ("%u", priv->issues->len);
	editor_chip_set_label (priv->issues_chip, label);
	editor_chip_set_badge (priv->issues_chip, badge);
	editor_chip_set_checked (priv->issues_chip, priv->issues_open);
}

GPtrArray *
lrg_editor_data_editor_get_issues (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->issues;
}

gboolean
lrg_editor_data_editor_activate_issue (LrgEditorDataEditor *self,
                                       guint                index)
{
	LrgEditorDataEditorPrivate *priv;
	LrgEditorDocumentIssue     *issue;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	priv = PRIV (self);
	if (index >= priv->issues->len)
		return FALSE;
	issue = g_ptr_array_index (priv->issues, index);
	lrg_editor_data_editor_reveal (self, issue->path != NULL ? issue->path : "", issue->line);
	return TRUE;
}

void
lrg_editor_data_editor_reveal (LrgEditorDataEditor *self,
                               const gchar         *path,
                               guint                line)
{
	LrgEditorDataEditorPrivate *priv;
	guint                       col = 0;

	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	priv = PRIV (self);
	if (path == NULL)
		path = "";
	if (line == 0)
		lrg_editor_document_get_line (priv->document, path, &line, &col);
	g_signal_emit (self, signals[SIGNAL_REVEAL], 0, path, line);
}

/* ==========================================================================
 * Toolbar commands
 * ========================================================================== */

void
lrg_editor_data_editor_report_error (LrgEditorDataEditor *self,
                                     const GError        *error)
{
	LrgEditorDataEditorPrivate *priv;

	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	if (error == NULL)
		return;
	priv = PRIV (self);
	if (priv->app != NULL)
		editor_app_notify (priv->app, EDITOR_NOTIFY_ERROR, "%s: %s",
		                   lrg_editor_document_get_display_name (priv->document), error->message);
	else
		g_warning ("%s: %s", lrg_editor_document_get_display_name (priv->document), error->message);
}

gboolean
lrg_editor_data_editor_save (LrgEditorDataEditor  *self,
                             GError              **error)
{
	LrgEditorDataEditorPrivate *priv;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	priv = PRIV (self);
	if (!LRG_EDITOR_DATA_EDITOR_GET_CLASS (self)->save (self, error))
		return FALSE;
	set_banner_visible (self, FALSE);
	lrg_editor_data_editor_validate (self);
	if (priv->app != NULL)
		editor_app_set_status (priv->app, "Saved %s", lrg_editor_document_get_display_name (priv->document));
	return TRUE;
}

gboolean
lrg_editor_data_editor_revert (LrgEditorDataEditor  *self,
                               GError              **error)
{
	LrgEditorDataEditorPrivate *priv;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	priv = PRIV (self);
	if (!lrg_editor_document_reload (priv->document, error))
		return FALSE;
	set_banner_visible (self, FALSE);
	lrg_editor_data_editor_validate (self);
	return TRUE;
}

gboolean
lrg_editor_data_editor_undo (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	return LRG_EDITOR_DATA_EDITOR_GET_CLASS (self)->undo (self);
}

gboolean
lrg_editor_data_editor_redo (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	return LRG_EDITOR_DATA_EDITOR_GET_CLASS (self)->redo (self);
}

/* --- prompts (the lrg-editor-dialogs.c protocol) -------------------------- */

typedef void (*PromptDone) (LrgEditorDataEditor *self, gint response);

static void
on_prompt_response (LrgDialog *dialog,
                    gint       response,
                    gpointer   user_data)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	PromptDone                  done = (PromptDone) g_object_get_data (G_OBJECT (dialog), "lrg-data-done");
	g_autoptr(LrgDialog)        keep = g_object_ref (dialog);
	g_autoptr(LrgEditorDataEditor) hold = g_object_ref (self);

	if (priv->app != NULL && g_object_get_data (G_OBJECT (priv->app), DIALOG_KEY) == dialog)
	{
		g_object_set_data (G_OBJECT (priv->app), DIALOG_KEY, NULL);
		lrg_editor_view_state_set_dialog (priv->app->view, NULL);
		editor_window_set_menus_enabled (priv->app->window, TRUE);
	}
	if (done != NULL)
		done (self, response);
}

static void
present_prompt (LrgEditorDataEditor *self,
                LrgDialog           *dialog,
                const gchar         *id,
                PromptDone           done)
{
	LrgEditorApp *app = PRIV (self)->app;

	if (app == NULL || app->canvas == NULL)
		return;
	editor_dialog_close_all (app);
	g_object_set_data (G_OBJECT (dialog), "lrg-data-done", (gpointer) done);
	g_signal_connect_object (dialog, "response", G_CALLBACK (on_prompt_response), self, 0);
	g_object_set_data_full (G_OBJECT (app), DIALOG_KEY, g_object_ref_sink (dialog), g_object_unref);
	lrg_editor_view_state_set_dialog (app->view, id);
	editor_window_set_menus_enabled (app->window, FALSE);
	lrg_dialog_present (dialog, app->canvas);
}

static void
revert_prompt_done (LrgEditorDataEditor *self,
                    gint                 response)
{
	g_autoptr(GError) error = NULL;

	if (response == RESPONSE_REVERT && !lrg_editor_data_editor_revert (self, &error))
		lrg_editor_data_editor_report_error (self, error);
}

/* The Revert button: asks first when there is something to lose. */
static void
ask_revert (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *body = NULL;
	g_autoptr(LrgDialog)        dialog = NULL;

	if (!lrg_editor_data_editor_get_dirty (self) || priv->app == NULL || priv->app->canvas == NULL)
	{
		if (!lrg_editor_data_editor_revert (self, &error))
			lrg_editor_data_editor_report_error (self, error);
		return;
	}
	body = g_strdup_printf ("Throw away the unsaved changes to %s and reload it from disk?",
	                        lrg_editor_document_get_display_name (priv->document));
	dialog = lrg_dialog_new ("Revert file?", body);
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Revert", RESPONSE_REVERT, TRUE);
	present_prompt (self, dialog, "data-revert", revert_prompt_done);
}

/* --- close --------------------------------------------------------------- */

/* Takes the tab out of the dock and the app (the final unref may follow). */
static void
really_close (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	g_autoptr(LrgEditorDataEditor) hold = g_object_ref (self);
	LrgDockPanel               *panel = dock_panel (self);

	if (priv->closing)
		return;
	priv->closing = TRUE;
	data_set_forget (self);
	if (panel != NULL)
	{
		g_autoptr(LrgDockPanel) keep = g_object_ref (panel);
		LrgDockArea            *area = lrg_dock_panel_get_dock_area (panel);

		if (area != NULL)
			lrg_dock_area_remove_panel (area, panel);
		if (priv->app != NULL)
			g_hash_table_remove (priv->app->panels, priv->panel_id);
	}
	g_signal_emit (self, signals[SIGNAL_CLOSED], 0);
}

static void
close_prompt_done (LrgEditorDataEditor *self,
                   gint                 response)
{
	g_autoptr(GError) error = NULL;

	if (response == RESPONSE_SAVE)
	{
		if (!lrg_editor_data_editor_save (self, &error))
		{
			lrg_editor_data_editor_report_error (self, error);
			return;
		}
		really_close (self);
	}
	else if (response == RESPONSE_DISCARD)
		really_close (self);
}

gboolean
lrg_editor_data_editor_close (LrgEditorDataEditor *self,
                              gboolean             force)
{
	LrgEditorDataEditorPrivate *priv;
	g_autofree gchar           *body = NULL;
	g_autoptr(LrgDialog)        dialog = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	priv = PRIV (self);
	if (force || !lrg_editor_data_editor_get_dirty (self) || priv->app == NULL ||
	    priv->app->canvas == NULL)
	{
		really_close (self);
		return TRUE;
	}
	body = g_strdup_printf ("%s has unsaved changes. Save them before closing?",
	                        lrg_editor_document_get_display_name (priv->document));
	dialog = lrg_dialog_new ("Save changes?", body);
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Don't Save", RESPONSE_DISCARD, FALSE);
	lrg_dialog_add_button (dialog, "Save", RESPONSE_SAVE, TRUE);
	present_prompt (self, dialog, "data-close", close_prompt_done);
	return FALSE;
}

/* The dock's close button on a data tab: veto and ask when dirty. */
static gboolean
on_dock_close_request (LrgDockArea  *dock,
                       LrgDockPanel *panel,
                       gpointer      user_data)
{
	LrgWidget *child = lrg_dock_panel_get_child (panel);

	if (child == NULL || !LRG_IS_EDITOR_DATA_EDITOR (child))
		return FALSE;
	if (lrg_editor_data_editor_close (LRG_EDITOR_DATA_EDITOR (child), FALSE))
		return TRUE;   /* already removed by really_close() */
	return TRUE;       /* the prompt decides */
}

/* ==========================================================================
 * External changes
 * ========================================================================== */

static void
set_banner_visible (LrgEditorDataEditor *self,
                    gboolean             visible)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	g_autofree gchar           *text = NULL;

	if (priv->banner_visible == visible)
		return;
	priv->banner_visible = visible;
	if (visible)
	{
		text = g_strdup_printf ("%s changed on disk. Reload it, or keep your edits and overwrite it on save.",
		                        lrg_editor_document_get_display_name (priv->document));
		g_free (priv->banner_text);
		priv->banner_text = g_steal_pointer (&text);
		lrg_widget_queue_layout (priv->banner_label);
	}
	lrg_widget_set_visible (LRG_WIDGET (priv->banner), visible);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_editor_data_editor_get_banner_visible (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	return PRIV (self)->banner_visible;
}

void
lrg_editor_data_editor_keep_mine (LrgEditorDataEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	set_banner_visible (self, FALSE);
}

/* The document noticed the change (monitor or check): reload a clean
 * document right away, ask through the banner when there are edits. */
static void
on_file_changed (LrgEditorDocument *doc,
                 gpointer           user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;

	if (!lrg_editor_document_get_dirty (doc) && g_file_test (lrg_editor_document_get_path (doc),
	                                                          G_FILE_TEST_IS_REGULAR))
	{
		if (!lrg_editor_data_editor_revert (self, &error))
			set_banner_visible (self, TRUE);
		return;
	}
	set_banner_visible (self, TRUE);
}

gboolean
lrg_editor_data_editor_check_disk (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv;
	gboolean                    was_shown;
	gboolean                    changed;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	priv = PRIV (self);
	was_shown = priv->banner_visible;
	changed = lrg_editor_document_check_external_change (priv->document);
	/* A change the user chose to keep stays quiet until the next save */
	if (changed && !was_shown && !priv->banner_visible && lrg_editor_data_editor_get_dirty (self))
		return TRUE;
	return changed;
}

/* ==========================================================================
 * Document signals
 * ========================================================================== */

static void
update_title (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgDockPanel               *panel = dock_panel (self);

	g_free (priv->title);
	priv->title = g_strconcat (lrg_editor_document_get_display_name (priv->document),
	                           lrg_editor_data_editor_get_dirty (self) ? DIRTY_MARK : "", NULL);
	if (panel != NULL)
		lrg_dock_panel_set_title (panel, priv->title);
	if (priv->save_chip != NULL)
		lrg_widget_set_enabled (priv->save_chip, lrg_editor_data_editor_get_dirty (self) ||
		                                        lrg_editor_document_get_path (priv->document) == NULL);
	if (priv->title_widget != NULL)
		lrg_widget_queue_layout (priv->title_widget);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
on_doc_changed (LrgEditorDocument *doc,
                const gchar       *path,
                gpointer           user_data)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgEditorDataEditorClass   *klass = LRG_EDITOR_DATA_EDITOR_GET_CLASS (self);

	if (klass->refresh != NULL)
		klass->refresh (self, path != NULL ? path : "");
	/* validate again once the edits pause */
	priv->stale = TRUE;
	priv->stale_time = 0.0f;
	update_title (self);
}

static void
on_doc_dirty_changed (LrgEditorDocument *doc,
                      gboolean           dirty,
                      gpointer           user_data)
{
	update_title (LRG_EDITOR_DATA_EDITOR (user_data));
}

/* ==========================================================================
 * Toolbar callbacks
 * ========================================================================== */

static void
on_save_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;

	if (!lrg_editor_data_editor_save (self, &error))
		lrg_editor_data_editor_report_error (self, error);
}

static void
on_revert_chip (LrgWidget *chip,
                gpointer   user_data)
{
	ask_revert (LRG_EDITOR_DATA_EDITOR (user_data));
}

static void
on_validate_chip (LrgWidget *chip,
                  gpointer   user_data)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	lrg_editor_data_editor_validate (self);
	priv->issues_open = priv->issues->len > 0;
	update_issue_chip (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (priv->app != NULL)
		editor_app_set_status (priv->app, "%s: %u error(s), %u warning(s)",
		                       lrg_editor_document_get_display_name (priv->document), priv->n_errors,
		                       priv->n_warnings);
}

static void
on_issues_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (user_data);
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	priv->issues_open = !priv->issues_open && priv->issues->len > 0;
	update_issue_chip (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_reload_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;

	if (!lrg_editor_data_editor_revert (self, &error))
		lrg_editor_data_editor_report_error (self, error);
}

static void
on_keep_chip (LrgWidget *chip,
              gpointer   user_data)
{
	lrg_editor_data_editor_keep_mine (LRG_EDITOR_DATA_EDITOR (user_data));
}

/* ==========================================================================
 * LrgWidget / LrgContainer
 * ========================================================================== */

/* Height of the issues area for the current issues, 0 when closed. */
static gfloat
issues_height (LrgEditorDataEditor *self,
               gfloat               avail)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	gfloat                      row = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                                        LRG_THEME_METRIC_ROW_HEIGHT);
	guint                       n = MIN (priv->issues->len, MAX_ISSUE_ROWS);

	if (!priv->issues_open || priv->issues->len == 0)
		return 0.0f;
	return MIN ((gfloat) n * row + 6.0f, avail * 0.4f);
}

static void
lrg_editor_data_editor_layout_children (LrgContainer *container)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (container);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	gfloat                      w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                      h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                      tw = 0.0f, th = 0.0f, bh = 0.0f, ih;
	gfloat                      y = 0.0f;

	if (priv->toolbar == NULL || priv->banner == NULL || priv->issues_view == NULL)
		return;

	/* toolbar */
	lrg_widget_get_preferred_size (LRG_WIDGET (priv->toolbar), &tw, &th);
	editor_panel_fit_toolbar (LRG_WIDGET (priv->toolbar), w);
	lrg_widget_place_in_slot (LRG_WIDGET (priv->toolbar), 0.0f, y, w, th);
	y += th;

	/* banner */
	if (priv->banner_visible)
	{
		lrg_widget_get_preferred_size (LRG_WIDGET (priv->banner), &tw, &bh);
		lrg_widget_place_in_slot (LRG_WIDGET (priv->banner), 0.0f, y, w, bh);
		y += bh;
	}

	/* issues at the bottom, content in between */
	ih = issues_height (self, MAX (h - y, 0.0f));
	lrg_list_view_set_row_height (priv->issues_view,
	                              lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                                    LRG_THEME_METRIC_ROW_HEIGHT));
	lrg_widget_set_visible (LRG_WIDGET (priv->issues_view), ih > 0.0f);
	if (ih > 0.0f)
		lrg_widget_place_in_slot (LRG_WIDGET (priv->issues_view), 0.0f, h - ih + 1.0f, w, ih - 1.0f);
	if (priv->content != NULL)
		lrg_widget_place_in_slot (priv->content, 0.0f, y, w, MAX (h - y - ih, 0.0f));
}

static void
lrg_editor_data_editor_measure (LrgWidget *widget,
                                gfloat    *width,
                                gfloat    *height)
{
	*width = 320.0f;
	*height = 200.0f;
}

static void
lrg_editor_data_editor_paint (LrgWidget    *widget,
                              LrgPaintList *list)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (widget);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgTheme                   *theme = lrg_widget_get_theme (widget);
	gfloat                      x = lrg_widget_get_world_x (widget);
	gfloat                      y = lrg_widget_get_world_y (widget);
	gfloat                      w = lrg_widget_get_width (widget);
	const GrlColor             *border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
	GrlColor                    c;

	/* toolbar strip with a hairline under it */
	if (priv->toolbar != NULL)
	{
		gfloat th = lrg_widget_get_height (LRG_WIDGET (priv->toolbar));

		lrg_paint_list_rect (list, x, y, w, th, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_paint_list_rect (list, x, y + th - 1.0f, w, 1.0f, 0.0f, border);
	}

	/* banner: a warning tint with an accent bar on the left */
	if (priv->banner_visible && priv->banner != NULL)
	{
		gfloat by = lrg_widget_get_world_y (LRG_WIDGET (priv->banner));
		gfloat bh = lrg_widget_get_height (LRG_WIDGET (priv->banner));
		const GrlColor *warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);

		lrg_paint_color_with_alpha (warn, 0.16f, &c);
		lrg_paint_list_rect (list, x, by, w, bh, 0.0f, &c);
		lrg_paint_list_rect (list, x, by, 3.0f, bh, 0.0f, warn);
		lrg_paint_color_with_alpha (warn, 0.45f, &c);
		lrg_paint_list_rect (list, x, by + bh - 1.0f, w, 1.0f, 0.0f, &c);
	}

	/* issues: panel background and a separator above */
	if (priv->issues_view != NULL && lrg_widget_get_visible (LRG_WIDGET (priv->issues_view)))
	{
		gfloat iy = lrg_widget_get_world_y (LRG_WIDGET (priv->issues_view));
		gfloat ih = lrg_widget_get_height (LRG_WIDGET (priv->issues_view));

		lrg_paint_list_rect (list, x, iy - 1.0f, w, ih + 1.0f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_paint_list_rect (list, x, iy - 1.0f, w, 1.0f, 0.0f, border);
	}

	LRG_WIDGET_CLASS (lrg_editor_data_editor_parent_class)->paint (widget, list);
}

/* Whether @key + @mods is one of @action's accelerators (or @fallback). */
static gboolean
matches_action (LrgEditorDataEditor *self,
                const gchar         *action,
                const gchar         *fallback,
                GrlKey               key,
                LrgInputModifiers    mods)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	g_auto(GStrv)               accels = NULL;
	guint                       i;

	if (priv->app != NULL)
		accels = lrg_editor_actions_get_accels (priv->app->actions, action);
	if ((accels == NULL || accels[0] == NULL) && fallback != NULL)
	{
		g_strfreev (accels);
		accels = g_strsplit (fallback, "|", -1);
	}
	for (i = 0; accels != NULL && accels[i] != NULL; i++)
	{
		GrlKey            k;
		LrgInputModifiers m;

		if (lrg_accel_parse (accels[i], &k, &m) && k == key && (m & MOD_MASK) == (mods & MOD_MASK))
			return TRUE;
	}
	return FALSE;
}

/*
 * Events bubble up from the focused child. Inside the panel the level's
 * undo/redo/save accelerators act on the document instead, and any click
 * makes this the focused data editor (for the IPC defaults).
 */
static gboolean
lrg_editor_data_editor_handle_event (LrgWidget        *widget,
                                     const LrgUIEvent *event)
{
	LrgEditorDataEditor *self = LRG_EDITOR_DATA_EDITOR (widget);
	LrgUIEventType       type = lrg_ui_event_get_event_type (event);
	GrlKey               key;
	LrgInputModifiers    mods;
	g_autoptr(GError)    error = NULL;

	if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
	{
		set_focused (self);
		return FALSE;
	}
	if (type != LRG_UI_EVENT_KEY_DOWN)
		return FALSE;
	set_focused (self);
	key = lrg_ui_event_get_key (event);
	mods = lrg_ui_event_get_modifiers (event);

	if (matches_action (self, "edit.redo", "<Ctrl><Shift>z|<Ctrl>y", key, mods))
	{
		lrg_editor_data_editor_redo (self);
		return TRUE;
	}
	if (matches_action (self, "edit.undo", "<Ctrl>z", key, mods))
	{
		lrg_editor_data_editor_undo (self);
		return TRUE;
	}
	if (lrg_ui_event_get_repeat (event))
		return FALSE;
	if (matches_action (self, "file.save", "<Ctrl>s", key, mods))
	{
		if (!lrg_editor_data_editor_save (self, &error))
			lrg_editor_data_editor_report_error (self, error);
		return TRUE;
	}
	if (matches_action (self, "code.close-tab", "<Ctrl>w", key, mods))
	{
		lrg_editor_data_editor_close (self, FALSE);
		return TRUE;
	}
	return FALSE;
}

/* Debounced validation after edits */
static gboolean
lrg_editor_data_editor_step (LrgWidget *widget,
                             gfloat     delta_time)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (widget);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	gboolean                    busy;

	busy = LRG_WIDGET_CLASS (lrg_editor_data_editor_parent_class)->step != NULL &&
	       LRG_WIDGET_CLASS (lrg_editor_data_editor_parent_class)->step (widget, delta_time);
	if (!priv->stale)
		return busy;
	priv->stale_time += delta_time;
	if (priv->stale_time >= REVALIDATE_DELAY)
		lrg_editor_data_editor_validate (self);
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static LrgWidget *
chip (const gchar    *name,
      LrgIcon         icon,
      const gchar    *label,
      EditorChipFunc  func,
      gpointer        data)
{
	return editor_chip_new (name, icon, label, func, data);
}

static void
lrg_editor_data_editor_constructed (GObject *object)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	static const LrgCustomWidgetFuncs title_funcs = { title_measure, title_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs banner_funcs = { NULL, banner_text_paint, NULL, NULL, { NULL } };
	LrgCustomWidget            *title;
	LrgWidget                  *sep;
	g_autofree gchar           *abs = NULL;

	G_OBJECT_CLASS (lrg_editor_data_editor_parent_class)->constructed (object);

	/* A bare construction (introspection, tests) gets an empty document */
	if (priv->document == NULL)
		priv->document = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);

	if (lrg_editor_document_get_path (priv->document) != NULL)
		priv->panel_id = g_strconcat (LRG_EDITOR_DATA_EDITOR_PANEL_PREFIX,
		                              lrg_editor_document_get_path (priv->document), NULL);
	else
		priv->panel_id = g_strdup_printf ("%s%p", LRG_EDITOR_DATA_EDITOR_PANEL_PREFIX, (gpointer) self);

	/* toolbar: title | Save Revert Validate | issues chip | tools */
	priv->toolbar = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (priv->toolbar), "data-toolbar");
	lrg_container_set_padding (LRG_CONTAINER (priv->toolbar), 4.0f);
	lrg_container_set_spacing (LRG_CONTAINER (priv->toolbar), 4.0f);
	title = lrg_custom_widget_new ();
	lrg_custom_widget_set_funcs (title, &title_funcs, self, NULL);
	lrg_widget_set_name (LRG_WIDGET (title), "data-title");
	lrg_widget_set_pointer_transparent (LRG_WIDGET (title), TRUE);
	priv->title_widget = LRG_WIDGET (title);
	lrg_widget_set_fill_weight (priv->title_widget, 1.0f);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), priv->title_widget);
	sep = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (sep, "data-gap");
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (sep), 10.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (sep), 4.0f);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), sep);
	priv->save_chip = chip ("data-save", LRG_ICON_DOWNLOAD, "Save", on_save_chip, self);
	priv->revert_chip = chip ("data-revert", LRG_ICON_REFRESH, "Revert", on_revert_chip, self);
	priv->validate_chip = chip ("data-validate", LRG_ICON_CHECK, "Validate", on_validate_chip, self);
	priv->issues_chip = chip ("data-issues", LRG_ICON_CHECK, "Valid", on_issues_chip, self);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), priv->save_chip);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), priv->revert_chip);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), priv->validate_chip);
	lrg_container_add_child (LRG_CONTAINER (priv->toolbar), priv->issues_chip);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (priv->toolbar));

	/* banner: message | Reload | Keep mine */
	priv->banner = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (priv->banner), "data-banner");
	lrg_container_set_padding (LRG_CONTAINER (priv->banner), 6.0f);
	lrg_container_set_spacing (LRG_CONTAINER (priv->banner), 6.0f);
	priv->banner_label = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (priv->banner_label), &banner_funcs, self, NULL);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (priv->banner_label), 40.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (priv->banner_label), 24.0f);
	lrg_widget_set_name (priv->banner_label, "data-banner-text");
	lrg_widget_set_fill_weight (priv->banner_label, 1.0f);
	lrg_widget_set_pointer_transparent (priv->banner_label, TRUE);
	lrg_container_add_child (LRG_CONTAINER (priv->banner), priv->banner_label);
	lrg_container_add_child (LRG_CONTAINER (priv->banner),
	                         chip ("data-banner-reload", LRG_ICON_REFRESH, "Reload", on_reload_chip, self));
	lrg_container_add_child (LRG_CONTAINER (priv->banner),
	                         chip ("data-banner-keep", LRG_ICON_CHECK, "Keep mine", on_keep_chip, self));
	lrg_widget_set_visible (LRG_WIDGET (priv->banner), FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (priv->banner));

	/* issues list */
	priv->issues_model = g_list_store_new (DATA_TYPE_ISSUE_ROW);
	priv->issues_view = lrg_list_view_new ();
	lrg_widget_set_name (LRG_WIDGET (priv->issues_view), "data-issues-list");
	lrg_list_view_set_factory (priv->issues_view, issue_row_create, issue_row_bind, NULL, NULL);
	lrg_list_view_set_selection_mode (priv->issues_view, LRG_SELECTION_MODE_SINGLE);
	lrg_list_view_set_model (priv->issues_view, G_LIST_MODEL (priv->issues_model));
	g_signal_connect_object (priv->issues_view, "selection-changed", G_CALLBACK (on_issue_selection), self, 0);
	g_signal_connect_object (priv->issues_view, "row-activated", G_CALLBACK (on_issue_activated), self, 0);
	lrg_widget_set_visible (LRG_WIDGET (priv->issues_view), FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (priv->issues_view));

	/* the document */
	g_signal_connect_object (priv->document, "changed", G_CALLBACK (on_doc_changed), self, 0);
	g_signal_connect_object (priv->document, "dirty-changed", G_CALLBACK (on_doc_dirty_changed), self, 0);
	g_signal_connect_object (priv->document, "file-changed", G_CALLBACK (on_file_changed), self, 0);

	update_title (self);
	(void) abs;
}

static void
lrg_editor_data_editor_dispose (GObject *object)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	data_set_forget (self);
	if (priv->document != NULL)
		g_signal_handlers_disconnect_by_data (priv->document, self);
	if (priv->issues_model != NULL)
		g_list_store_remove_all (priv->issues_model);
	g_clear_weak_pointer (&priv->app);
	G_OBJECT_CLASS (lrg_editor_data_editor_parent_class)->dispose (object);
}

static void
lrg_editor_data_editor_finalize (GObject *object)
{
	LrgEditorDataEditorPrivate *priv = PRIV (LRG_EDITOR_DATA_EDITOR (object));

	g_clear_object (&priv->document);
	g_clear_object (&priv->issues_model);
	g_clear_pointer (&priv->issues, g_ptr_array_unref);
	g_free (priv->panel_id);
	g_free (priv->title);
	g_free (priv->banner_text);
	G_OBJECT_CLASS (lrg_editor_data_editor_parent_class)->finalize (object);
}

static void
lrg_editor_data_editor_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	switch (prop_id)
	{
	case PROP_APP:
		g_value_set_object (value, priv->app);
		break;
	case PROP_DOCUMENT:
		g_value_set_object (value, priv->document);
		break;
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	case PROP_N_ISSUES:
		g_value_set_uint (value, priv->issues->len);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_data_editor_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgEditorDataEditor        *self = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	switch (prop_id)
	{
	case PROP_APP:
		g_set_weak_pointer (&priv->app, g_value_get_object (value));
		break;
	case PROP_DOCUMENT:
		priv->document = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static gboolean
real_save (LrgEditorDataEditor  *self,
           GError              **error)
{
	return lrg_editor_document_save (PRIV (self)->document, error);
}

static gboolean
real_undo (LrgEditorDataEditor *self)
{
	return lrg_editor_document_undo (PRIV (self)->document);
}

static gboolean
real_redo (LrgEditorDataEditor *self)
{
	return lrg_editor_document_redo (PRIV (self)->document);
}

static gboolean
real_get_dirty (LrgEditorDataEditor *self)
{
	return lrg_editor_document_get_dirty (PRIV (self)->document);
}

static void
lrg_editor_data_editor_class_init (LrgEditorDataEditorClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_editor_data_editor_constructed;
	object_class->dispose = lrg_editor_data_editor_dispose;
	object_class->finalize = lrg_editor_data_editor_finalize;
	object_class->get_property = lrg_editor_data_editor_get_property;
	object_class->set_property = lrg_editor_data_editor_set_property;
	widget_class->measure = lrg_editor_data_editor_measure;
	widget_class->paint = lrg_editor_data_editor_paint;
	widget_class->handle_event = lrg_editor_data_editor_handle_event;
	widget_class->step = lrg_editor_data_editor_step;
	container_class->layout_children = lrg_editor_data_editor_layout_children;
	klass->save = real_save;
	klass->undo = real_undo;
	klass->redo = real_redo;
	klass->get_dirty = real_get_dirty;

	/**
	 * LrgEditorDataEditor:app:
	 *
	 * The editor application (weak; %NULL for a panel built without one,
	 * e.g. in a unit test).
	 */
	properties[PROP_APP] =
		g_param_spec_object ("app", "App", "The editor application", LRG_TYPE_EDITOR_APP,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDataEditor:document:
	 *
	 * The edited document. Construct-only; a panel built without one gets
	 * an empty document of unknown format.
	 */
	properties[PROP_DOCUMENT] =
		g_param_spec_object ("document", "Document", "The edited document", LRG_TYPE_EDITOR_DOCUMENT,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDataEditor:title:
	 *
	 * The tab title: the file name plus " •" while the document is dirty.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", "Title", "The tab title", NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorDataEditor:n-issues:
	 *
	 * The number of issues found by the last validation.
	 */
	properties[PROP_N_ISSUES] =
		g_param_spec_uint ("n-issues", "Issues", "Issues of the last validation", 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorDataEditor::reveal:
	 * @self: the data editor
	 * @path: the YAML path to show ("" for the root)
	 * @line: the 1-based line of @path, 0 when unknown
	 *
	 * Emitted when an issue row is clicked (or
	 * lrg_editor_data_editor_reveal() is called). The class handler is
	 * #LrgEditorDataEditorClass.reveal.
	 */
	signals[SIGNAL_REVEAL] =
		g_signal_new ("reveal", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgEditorDataEditorClass, reveal), NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	/**
	 * LrgEditorDataEditor::closed:
	 * @self: the data editor
	 *
	 * Emitted after the tab was closed and removed from the dock.
	 */
	signals[SIGNAL_CLOSED] =
		g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_editor_data_editor_init (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);

	priv->issues = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Panel API
 * ========================================================================== */

LrgEditorApp *
lrg_editor_data_editor_get_app (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->app;
}

LrgEditorDocument *
lrg_editor_data_editor_get_document (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->document;
}

const gchar *
lrg_editor_data_editor_get_path (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return lrg_editor_document_get_path (PRIV (self)->document);
}

const gchar *
lrg_editor_data_editor_get_panel_id (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->panel_id;
}

const gchar *
lrg_editor_data_editor_get_title (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->title;
}

void
lrg_editor_data_editor_set_content (LrgEditorDataEditor *self,
                                    LrgWidget           *content)
{
	LrgEditorDataEditorPrivate *priv;

	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	g_return_if_fail (content == NULL || LRG_IS_WIDGET (content));
	priv = PRIV (self);
	if (priv->content == content)
		return;
	if (priv->content != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self), priv->content);
	priv->content = content;
	if (content != NULL)
		lrg_container_add_child (LRG_CONTAINER (self), content);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_editor_data_editor_get_dirty (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), FALSE);
	return LRG_EDITOR_DATA_EDITOR_GET_CLASS (self)->get_dirty (self);
}

void
lrg_editor_data_editor_sync_title (LrgEditorDataEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	update_title (self);
}

LrgWidget *
lrg_editor_data_editor_get_content (LrgEditorDataEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	return PRIV (self)->content;
}

void
lrg_editor_data_editor_add_tool (LrgEditorDataEditor *self,
                                 LrgWidget           *tool)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self));
	g_return_if_fail (LRG_IS_WIDGET (tool));
	lrg_container_add_child (LRG_CONTAINER (PRIV (self)->toolbar), tool);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

guint
lrg_editor_data_editor_new_gesture (LrgEditorDataEditor *self)
{
	static guint next = 0x40000000u;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), 0);
	if (++next == 0)
		next = 1;
	return next;
}

gchar *
lrg_editor_data_editor_get_state (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorClass *klass;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (self), NULL);
	klass = LRG_EDITOR_DATA_EDITOR_GET_CLASS (self);
	return (klass->get_state != NULL) ? klass->get_state (self) : NULL;
}

/* ==========================================================================
 * Opening
 * ========================================================================== */

static gchar *
absolute_path (LrgEditorApp *app,
               const gchar  *path)
{
	g_autoptr(GFile) file = NULL;

	if (g_path_is_absolute (path))
		file = g_file_new_for_path (path);
	else if (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
		return editor_app_project_path (app, path);
	else
		file = g_file_new_for_commandline_arg (path);
	return g_file_get_path (file);
}

LrgEditorDataEditor *
lrg_editor_data_editor_find (LrgEditorApp *app,
                             const gchar  *path)
{
	DataSet          *set;
	g_autofree gchar *abs = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	set = g_object_get_data (G_OBJECT (app), SET_KEY);
	if (set == NULL)
		return NULL;
	if (path == NULL || *path == '\0')
		return set->focused;
	abs = absolute_path (app, path);
	for (i = 0; i < set->editors->len; i++)
	{
		LrgEditorDataEditor *e = g_ptr_array_index (set->editors, i);

		if (g_strcmp0 (lrg_editor_data_editor_get_path (e), abs) == 0)
			return e;
	}
	return NULL;
}

GList *
lrg_editor_data_editor_list (LrgEditorApp *app)
{
	DataSet *set;
	GList   *out = NULL;
	guint    i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	set = g_object_get_data (G_OBJECT (app), SET_KEY);
	for (i = 0; set != NULL && i < set->editors->len; i++)
		out = g_list_prepend (out, g_ptr_array_index (set->editors, i));
	return g_list_reverse (out);
}

/* Brings the tab of @self to the front and focuses it. */
static void
present (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgDockPanel               *panel = dock_panel (self);

	set_focused (self);
	if (priv->app == NULL || panel == NULL || lrg_dock_panel_get_dock_area (panel) == NULL)
		return;
	lrg_dock_area_present_panel (lrg_dock_panel_get_dock_area (panel), panel);
	if (priv->app->canvas != NULL && lrg_editor_view_state_get_dialog (priv->app->view) == NULL)
	{
		LrgWidget *focus = lrg_canvas_get_focused_widget (priv->app->canvas);

		if (focus == NULL || !lrg_widget_is_ancestor (focus, LRG_WIDGET (self)))
			lrg_canvas_set_focused_widget (priv->app->canvas, LRG_WIDGET (self));
	}
}

/*
 * Where a new data tab goes (an lrg_editor_app_add_panel() position): next
 * to an open data tab (so they stay together wherever the user moved
 * them), else next to the code panel when that sits in the viewport's
 * stack, else the center stack (a tab beside the viewport). The default
 * layout docks the code panel in the short bottom stack, too cramped for
 * a table editor.
 */
static gchar *
dock_target (LrgEditorApp *app,
             LrgDockArea  *dock)
{
	DataSet      *set = data_set (app);
	LrgDockPanel *code;
	LrgDockPanel *viewport;
	guint         i;

	for (i = 0; dock != NULL && i < set->editors->len; i++)
	{
		LrgDockPanel *p = dock_panel (g_ptr_array_index (set->editors, i));

		if (p != NULL && lrg_dock_panel_get_dock_area (p) == dock && !lrg_dock_area_is_panel_floating (dock, p))
			return g_strconcat ("tab:", lrg_dock_panel_get_id (p), NULL);
	}
	code = (dock != NULL) ? lrg_dock_area_get_panel (dock, "code") : NULL;
	viewport = (dock != NULL) ? lrg_dock_area_get_panel (dock, "viewport") : NULL;
	if (code != NULL && viewport != NULL && lrg_dock_panel_get_dock_area (code) == dock)
	{
		g_autoptr(GList) stack = lrg_dock_area_get_stack_panels (dock, viewport);

		if (g_list_find (stack, code) != NULL)
			return g_strdup ("tab:code");
	}
	return g_strdup ("center");
}

LrgEditorDataEditor *
lrg_editor_data_editor_open (LrgEditorApp  *app,
                             const gchar   *path,
                             GError       **error)
{
	DataSet                       *set;
	LrgEditorDataEditor           *self;
	LrgEditorDocumentFormat        format;
	GType                          type;
	g_autofree gchar              *abs = NULL;
	g_autoptr(LrgEditorDocument)   doc = NULL;
	LrgDockPanel                  *panel;
	LrgDockArea                   *dock;
	g_autofree gchar              *where = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	lrg_editor_data_editor_install (app);
	set = data_set (app);

	/* an open tab is reused */
	self = lrg_editor_data_editor_find (app, path);
	if (self != NULL)
	{
		present (self);
		return self;
	}

	/* the document and the editor of its format */
	abs = absolute_path (app, path);
	doc = lrg_editor_document_new_from_file (abs, error);
	if (doc == NULL)
		return NULL;
	format = lrg_editor_document_get_format (doc);
	type = lrg_editor_data_editor_lookup_type (format);
	if (format == LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN || type == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a data format with an editor (%s)", abs,
		             lrg_editor_document_format_to_string (format));
		return NULL;
	}
	lrg_editor_document_set_monitor_file (doc, TRUE);
	self = g_object_new (type, "app", app, "document", doc, NULL);

	/* dock it next to the code panel, else in the center stack */
	panel = lrg_dock_panel_new (PRIV (self)->panel_id, PRIV (self)->title);
	lrg_dock_panel_set_closable (panel, TRUE);
	lrg_dock_panel_set_child (panel, LRG_WIDGET (self));
	dock = lrg_editor_app_get_dock (app);
	where = dock_target (app, dock);
	g_ptr_array_add (set->editors, self);
	lrg_editor_app_add_panel (app, panel, where);
	update_title (self);
	lrg_editor_data_editor_validate (self);
	present (self);
	editor_app_set_status (app, "Opened %s", lrg_editor_document_get_display_name (doc));
	return self;
}

/* ==========================================================================
 * IPC
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
param_int (JsonObject  *params,
           const gchar *name,
           gint64      *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || (json_node_get_value_type (n) != G_TYPE_INT64 &&
	                                   json_node_get_value_type (n) != G_TYPE_DOUBLE))
		return FALSE;
	*out = json_node_get_int (n);
	return TRUE;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	return JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_BOOLEAN && json_node_get_boolean (n);
}

static void
set_ipc_error (GError       **error,
               const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause->domain == G_FILE_ERROR && cause->code == G_FILE_ERROR_NOENT)
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause->domain == LRG_EDITOR_DOCUMENT_ERROR &&
	         (cause->code == LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND ||
	          cause->code == LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause->message);
}

/* The editor named by "path" (or the focused one); NULL + error if none. */
static LrgEditorDataEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar         *path = param_string (params, "path");
	LrgEditorDataEditor *e = lrg_editor_data_editor_find (app, path);

	if (e == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             path != NULL ? "No data editor is open for %s (use data.open)" : "No data editor is open%s",
		             path != NULL ? path : "");
	return e;
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static JsonObject *
editor_status (LrgEditorDataEditor *self)
{
	LrgEditorDataEditorPrivate *priv = PRIV (self);
	LrgEditorDocument          *doc = priv->document;
	JsonObject                 *o = json_object_new ();
	g_autofree gchar           *state = lrg_editor_data_editor_get_state (self);
	DataSet                    *set = priv->app != NULL ? g_object_get_data (G_OBJECT (priv->app), SET_KEY) : NULL;

	json_object_set_string_member (o, "path", lrg_editor_document_get_path (doc));
	json_object_set_string_member (o, "panel", priv->panel_id);
	json_object_set_string_member (o, "format", lrg_editor_document_format_to_string (
	                                   lrg_editor_document_get_format (doc)));
	json_object_set_string_member (o, "editor", G_OBJECT_TYPE_NAME (self));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (self));
	json_object_set_boolean_member (o, "read_only", lrg_editor_document_get_read_only (doc));
	json_object_set_boolean_member (o, "focused", set != NULL && set->focused == self);
	json_object_set_boolean_member (o, "changed_on_disk", priv->banner_visible);
	json_object_set_int_member (o, "issues", priv->issues->len);
	json_object_set_int_member (o, "errors", priv->n_errors);
	if (lrg_editor_document_get_undo_label (doc) != NULL)
		json_object_set_string_member (o, "undo_label", lrg_editor_document_get_undo_label (doc));
	else
		json_object_set_null_member (o, "undo_label");
	if (lrg_editor_document_get_redo_label (doc) != NULL)
		json_object_set_string_member (o, "redo_label", lrg_editor_document_get_redo_label (doc));
	else
		json_object_set_null_member (o, "redo_label");
	if (state != NULL)
		json_object_set_string_member (o, "state", state);
	return o;
}

/* data.open {path} */
static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	const gchar         *path = param_string (params, "path");
	LrgEditorDataEditor *e;
	g_autoptr(GError)    err = NULL;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	e = lrg_editor_data_editor_open (LRG_EDITOR_APP (user_data), path, &err);
	if (e == NULL)
	{
		set_ipc_error (error, err);
		return NULL;
	}
	return object_node (editor_status (e));
}

/* data.get {path?, at?, as?} */
static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorDataEditor      *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	LrgEditorDocument        *doc;
	const gchar              *at = param_string (params, "at");
	const gchar              *as = param_string (params, "as");
	LrgEditorDocumentNodeKind kind;
	JsonObject               *o;
	guint                     line = 0, col = 0;

	if (e == NULL)
		return NULL;
	doc = lrg_editor_data_editor_get_document (e);
	if (at == NULL)
		at = "";
	kind = lrg_editor_document_get_kind (doc, at);
	if (kind == LRG_EDITOR_DOCUMENT_NODE_MISSING)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node at '%s'", at);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "at", at);
	json_object_set_string_member (o, "kind", kind == LRG_EDITOR_DOCUMENT_NODE_NULL ? "null"
	                               : kind == LRG_EDITOR_DOCUMENT_NODE_SCALAR ? "scalar"
	                               : kind == LRG_EDITOR_DOCUMENT_NODE_MAPPING ? "mapping" : "sequence");
	if (g_strcmp0 (as, "yaml") == 0 || kind == LRG_EDITOR_DOCUMENT_NODE_MAPPING ||
	    kind == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		g_autofree gchar *yaml = lrg_editor_document_get_yaml (doc, at);

		json_object_set_string_member (o, "yaml", yaml);
	}
	if (kind == LRG_EDITOR_DOCUMENT_NODE_SCALAR)
	{
		g_auto(GValue) v = G_VALUE_INIT;

		if (lrg_editor_document_get_value (doc, at, &v))
		{
			JsonNode *n = json_node_new (JSON_NODE_VALUE);

			if (G_VALUE_HOLDS_BOOLEAN (&v))
				json_node_set_boolean (n, g_value_get_boolean (&v));
			else if (G_VALUE_HOLDS_INT64 (&v))
				json_node_set_int (n, g_value_get_int64 (&v));
			else if (G_VALUE_HOLDS_DOUBLE (&v))
				json_node_set_double (n, g_value_get_double (&v));
			else
				json_node_set_string (n, lrg_editor_document_get_scalar (doc, at));
			json_object_set_member (o, "value", n);
		}
		else
			json_object_set_string_member (o, "value", lrg_editor_document_get_scalar (doc, at));
	}
	else if (kind == LRG_EDITOR_DOCUMENT_NODE_NULL)
		json_object_set_null_member (o, "value");
	else if (kind == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc, at);
		JsonArray    *a = json_array_new ();
		guint         i;

		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			json_array_add_string_element (a, keys[i]);
		json_object_set_array_member (o, "keys", a);
	}
	else
		json_object_set_int_member (o, "length", lrg_editor_document_get_length (doc, at));
	if (lrg_editor_document_get_line (doc, at, &line, &col))
		json_object_set_int_member (o, "line", line);
	return object_node (o);
}

/* Replies {ok, dirty, undo_label} after an edit */
static JsonNode *
edit_reply (LrgEditorDataEditor *e)
{
	JsonObject *o = editor_status (e);

	json_object_set_boolean_member (o, "ok", TRUE);
	return object_node (o);
}

/* data.set {path?, at, value | yaml, gesture?} */
static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	LrgEditorDocument   *doc;
	const gchar         *at = param_string (params, "at");
	const gchar         *yaml = param_string (params, "yaml");
	gint64               gesture = 0;
	g_autoptr(GError)    err = NULL;
	gboolean             ok;

	if (e == NULL)
		return NULL;
	doc = lrg_editor_data_editor_get_document (e);
	if (at == NULL || (yaml == NULL && !json_object_has_member (params, "value")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "data.set needs 'at' and one of 'value' / 'yaml'");
		return NULL;
	}
	param_int (params, "gesture", &gesture);
	if (yaml != NULL)
		ok = lrg_editor_document_replace (doc, at, yaml, &err);
	else
	{
		JsonNode *v = json_object_get_member (params, "value");

		if (JSON_NODE_HOLDS_NULL (v))
			ok = lrg_editor_document_set_null (doc, at, (guint) gesture, &err);
		else if (!JSON_NODE_HOLDS_VALUE (v))
		{
			g_autoptr(JsonGenerator) gen = json_generator_new ();
			g_autofree gchar        *text = NULL;

			/* JSON is YAML: an object or array value replaces the subtree */
			json_generator_set_root (gen, v);
			text = json_generator_to_data (gen, NULL);
			ok = lrg_editor_document_replace (doc, at, text, &err);
		}
		else if (json_node_get_value_type (v) == G_TYPE_BOOLEAN)
			ok = lrg_editor_document_set_boolean (doc, at, json_node_get_boolean (v), (guint) gesture, &err);
		else if (json_node_get_value_type (v) == G_TYPE_INT64)
			ok = lrg_editor_document_set_int (doc, at, json_node_get_int (v), (guint) gesture, &err);
		else if (json_node_get_value_type (v) == G_TYPE_DOUBLE)
			ok = lrg_editor_document_set_double (doc, at, json_node_get_double (v), (guint) gesture, &err);
		else
			ok = lrg_editor_document_set_string (doc, at, json_node_get_string (v), (guint) gesture, &err);
	}
	if (!ok)
	{
		set_ipc_error (error, err);
		return NULL;
	}
	return edit_reply (e);
}

/* data.edit {path?, op, at, ...} */
static JsonNode *
m_edit (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	LrgEditorDocument   *doc;
	const gchar         *op = param_string (params, "op");
	const gchar         *at = param_string (params, "at");
	const gchar         *yaml = param_string (params, "yaml");
	gint64               a = -1, b = -1;
	g_autoptr(GError)    err = NULL;
	gboolean             ok = FALSE;

	if (e == NULL)
		return NULL;
	doc = lrg_editor_data_editor_get_document (e);
	if (op == NULL || at == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "data.edit needs 'op' and 'at'");
		return NULL;
	}
	if (g_strcmp0 (op, "insert") == 0)
	{
		if (!param_int (params, "index", &a))
			a = -1;
		ok = lrg_editor_document_insert_item (doc, at, (gint) a, yaml != NULL ? yaml : "null", &err);
	}
	else if (g_strcmp0 (op, "move") == 0)
	{
		if (!param_int (params, "from", &a) || !param_int (params, "to", &b))
			g_set_error_literal (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
			                     "move needs 'from' and 'to'");
		else
			ok = lrg_editor_document_move_item (doc, at, (guint) a, (guint) b, &err);
	}
	else if (g_strcmp0 (op, "add_key") == 0)
	{
		const gchar *key = param_string (params, "key");

		if (!param_int (params, "position", &a))
			a = -1;
		if (key == NULL)
			g_set_error_literal (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
			                     "add_key needs 'key'");
		else
			ok = lrg_editor_document_add_key (doc, at, key, yaml != NULL ? yaml : "null", (gint) a, &err);
	}
	else if (g_strcmp0 (op, "rename_key") == 0)
	{
		const gchar *from = param_string (params, "old");
		const gchar *to = param_string (params, "new");

		if (from == NULL || to == NULL)
			g_set_error_literal (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
			                     "rename_key needs 'old' and 'new'");
		else
			ok = lrg_editor_document_rename_key (doc, at, from, to, &err);
	}
	else if (g_strcmp0 (op, "remove") == 0)
		ok = lrg_editor_document_remove (doc, at, &err);
	else
		g_set_error (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		             "Unknown op '%s' (insert, move, add_key, rename_key, remove)", op);
	if (!ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     err != NULL ? err->message : "Edit failed");
		return NULL;
	}
	return edit_reply (e);
}

/* data.undo / data.redo {path?} */
static JsonNode *
m_undo_redo (LrgEditorDataEditor *e,
             gboolean             redo)
{
	gboolean    ok = redo ? lrg_editor_data_editor_redo (e) : lrg_editor_data_editor_undo (e);
	JsonObject *o = editor_status (e);

	json_object_set_boolean_member (o, "ok", ok);
	return object_node (o);
}

static JsonNode *
m_undo (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return (e != NULL) ? m_undo_redo (e, FALSE) : NULL;
}

static JsonNode *
m_redo (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return (e != NULL) ? m_undo_redo (e, TRUE) : NULL;
}

/* data.validate {path?} */
static JsonNode *
m_validate (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject          *o;
	JsonArray           *a;
	GPtrArray           *issues;
	guint                errors, i;

	if (e == NULL)
		return NULL;
	errors = lrg_editor_data_editor_validate (e);
	issues = PRIV (e)->issues;
	o = json_object_new ();
	a = json_array_new ();
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);
		JsonObject             *io = json_object_new ();

		json_object_set_string_member (io, "severity", issue->severity == LRG_EDITOR_PROBLEM_ERROR ? "error"
		                               : issue->severity == LRG_EDITOR_PROBLEM_WARNING ? "warning" : "info");
		json_object_set_string_member (io, "path", issue->path != NULL ? issue->path : "");
		json_object_set_int_member (io, "line", issue->line);
		json_object_set_int_member (io, "column", issue->column);
		json_object_set_string_member (io, "message", issue->message);
		json_array_add_object_element (a, io);
	}
	json_object_set_boolean_member (o, "ok", errors == 0);
	json_object_set_int_member (o, "errors", errors);
	json_object_set_array_member (o, "issues", a);
	return object_node (o);
}

/* data.save {path?, save_as?} */
static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar         *save_as = param_string (params, "save_as");
	g_autoptr(GError)    err = NULL;
	gboolean             ok;

	if (e == NULL)
		return NULL;
	if (save_as != NULL)
	{
		g_autofree gchar *abs = absolute_path (LRG_EDITOR_APP (user_data), save_as);

		ok = lrg_editor_document_save_as (lrg_editor_data_editor_get_document (e), abs, &err);
		if (ok)
			lrg_editor_data_editor_validate (e);
	}
	else
		ok = lrg_editor_data_editor_save (e, &err);
	if (!ok)
	{
		set_ipc_error (error, err);
		return NULL;
	}
	return edit_reply (e);
}

/* data.reload {path?} */
static JsonNode *
m_reload (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)    err = NULL;

	if (e == NULL)
		return NULL;
	if (!lrg_editor_data_editor_revert (e, &err))
	{
		set_ipc_error (error, err);
		return NULL;
	}
	return edit_reply (e);
}

/* data.list */
static JsonNode *
m_list (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	g_autoptr(GList) editors = lrg_editor_data_editor_list (LRG_EDITOR_APP (user_data));
	JsonArray       *a = json_array_new ();
	JsonNode        *n = json_node_new (JSON_NODE_ARRAY);
	GList           *l;

	for (l = editors; l != NULL; l = l->next)
		json_array_add_object_element (a, editor_status (l->data));
	json_node_take_array (n, a);
	return n;
}

/* data.close {path?, force?} */
static JsonNode *
m_close (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject          *o;

	if (e == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "closed", lrg_editor_data_editor_close (e, param_bool (params, "force")));
	return object_node (o);
}

/* data.reveal {path?, at} : what clicking an issue with that path does */
static JsonNode *
m_reveal (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorDataEditor *e = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gint64               line = 0;

	if (e == NULL)
		return NULL;
	param_int (params, "line", &line);
	lrg_editor_data_editor_reveal (e, param_string (params, "at"), (guint) MAX (line, 0));
	return object_node (editor_status (e));
}

/* ==========================================================================
 * Install
 * ========================================================================== */

static gboolean
has_editor (gpointer data)
{
	return lrg_editor_data_editor_find (LRG_EDITOR_APP (data), NULL) != NULL;
}

static void
a_save (gpointer data)
{
	LrgEditorDataEditor *e = lrg_editor_data_editor_find (LRG_EDITOR_APP (data), NULL);
	g_autoptr(GError)    error = NULL;

	if (e != NULL && !lrg_editor_data_editor_save (e, &error))
		lrg_editor_data_editor_report_error (e, error);
}

static void
a_validate (gpointer data)
{
	LrgEditorDataEditor *e = lrg_editor_data_editor_find (LRG_EDITOR_APP (data), NULL);

	if (e != NULL)
		on_validate_chip (NULL, e);
}

static void
a_revert (gpointer data)
{
	LrgEditorDataEditor *e = lrg_editor_data_editor_find (LRG_EDITOR_APP (data), NULL);

	if (e != NULL)
		ask_revert (e);
}

static void
a_close (gpointer data)
{
	LrgEditorDataEditor *e = lrg_editor_data_editor_find (LRG_EDITOR_APP (data), NULL);

	if (e != NULL)
		lrg_editor_data_editor_close (e, FALSE);
}

/* the "data" canvas action group: data.open(s) for the assets panel */
static void
g_open (GSimpleAction *action,
        GVariant      *parameter,
        gpointer       user_data)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	const gchar      *path = g_variant_get_string (parameter, NULL);
	g_autoptr(GError) error = NULL;

	if (lrg_editor_data_editor_open (app, path, &error) == NULL)
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "Cannot open %s: %s", path, error->message);
}

void
lrg_editor_data_editor_install (LrgEditorApp *app)
{
	static const GActionEntry entries[] = {
		{ "open", g_open, "s", NULL, NULL, { 0 } },
	};
	DataSet                  *set;
	LrgEditorActions         *actions;
	LrgDockArea              *dock;
	g_autoptr(GSimpleActionGroup) group = NULL;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	set = data_set (app);
	if (set->installed)
		return;
	set->installed = TRUE;
	registry_ensure ();

	/* the dock's close button asks for dirty data tabs */
	dock = lrg_editor_app_get_dock (app);
	if (dock != NULL)
	{
		g_set_weak_pointer (&set->dock, dock);
		set->close_id = g_signal_connect (dock, "close-request", G_CALLBACK (on_dock_close_request), NULL);
	}

	/* canvas action group "data" */
	if (app->canvas != NULL)
	{
		group = g_simple_action_group_new ();
		g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), app);
		lrg_widget_insert_action_group (LRG_WIDGET (app->canvas), "data", G_ACTION_GROUP (group));
	}

	/* palette / menu actions on the focused data editor */
	actions = app->actions;
	lrg_editor_actions_add (actions, "data.save", "Save Data File", "Data", NULL, a_save, has_editor, NULL, app);
	lrg_editor_actions_add (actions, "data.revert", "Revert Data File", "Data", NULL, a_revert, has_editor, NULL,
	                        app);
	lrg_editor_actions_add (actions, "data.validate", "Validate Data File", "Data", NULL, a_validate, has_editor,
	                        NULL, app);
	lrg_editor_actions_add (actions, "data.close", "Close Data File", "Data", NULL, a_close, has_editor, NULL, app);

	/* IPC */
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "data.open", m_open, app, NULL);
		lrg_editor_server_register_method (app->server, "data.get", m_get, app, NULL);
		lrg_editor_server_register_method (app->server, "data.set", m_set, app, NULL);
		lrg_editor_server_register_method (app->server, "data.edit", m_edit, app, NULL);
		lrg_editor_server_register_method (app->server, "data.undo", m_undo, app, NULL);
		lrg_editor_server_register_method (app->server, "data.redo", m_redo, app, NULL);
		lrg_editor_server_register_method (app->server, "data.validate", m_validate, app, NULL);
		lrg_editor_server_register_method (app->server, "data.save", m_save, app, NULL);
		lrg_editor_server_register_method (app->server, "data.reload", m_reload, app, NULL);
		lrg_editor_server_register_method (app->server, "data.list", m_list, app, NULL);
		lrg_editor_server_register_method (app->server, "data.close", m_close, app, NULL);
		lrg_editor_server_register_method (app->server, "data.reveal", m_reveal, app, NULL);
	}
}

/* ==========================================================================
 * LrgEditorDataSource
 * ========================================================================== */

typedef struct
{
	LrgPropertyInfo *info;
	gchar           *path;
	gchar           *default_text;
	GType            type;
} DataField;

struct _LrgEditorDataSource
{
	GObject                       parent_instance;
	LrgEditorDataEditor          *editor;     /* weak */
	GPtrArray                    *fields;     /* DataField* */
	guint                         gesture;    /* non-zero inside a grid gesture */
	guint                         grid_gesture;
	gboolean                      wrote;      /* the gesture changed something */
	LrgEditorDataSourceWriteFunc  write_func;
	gpointer                      write_data;
	GDestroyNotify                write_destroy;
	LrgEditorDataSourceReadFunc   read_func;
	gpointer                      read_data;
	GDestroyNotify                read_destroy;
};

static void data_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEditorDataSource, lrg_editor_data_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, data_source_iface_init))

static void
data_field_free (gpointer data)
{
	DataField *f = data;

	lrg_property_info_unref (f->info);
	g_free (f->path);
	g_free (f->default_text);
	g_free (f);
}

static DataField *
source_field (LrgEditorDataSource *self,
              const gchar         *name)
{
	guint i;

	for (i = 0; i < self->fields->len; i++)
	{
		DataField *f = g_ptr_array_index (self->fields, i);

		if (g_strcmp0 (lrg_property_info_get_name (f->info), name) == 0)
			return f;
	}
	return NULL;
}

static LrgEditorDocument *
source_document (LrgEditorDataSource *self)
{
	return (self->editor != NULL) ? lrg_editor_data_editor_get_document (self->editor) : NULL;
}

static GPtrArray *
source_list_properties (LrgPropertySource *source)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (source);
	GPtrArray           *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint                i;

	for (i = 0; i < self->fields->len; i++)
		g_ptr_array_add (out, lrg_property_info_ref (((DataField *) g_ptr_array_index (self->fields, i))->info));
	return out;
}

/* Converts the YAML text @text to a value of @type in @value (initialised
 * here); unparsable text gives the type's zero value. */
static void
value_from_text (GType        type,
                 const gchar *text,
                 GValue      *value)
{
	g_value_init (value, type);
	if (text == NULL)
		return;
	if (type == G_TYPE_STRING)
		g_value_set_string (value, text);
	else if (type == G_TYPE_BOOLEAN)
		g_value_set_boolean (value, g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
		                            g_ascii_strcasecmp (text, "on") == 0);
	else if (type == G_TYPE_INT64)
		g_value_set_int64 (value, g_ascii_strtoll (text, NULL, 0));
	else if (type == G_TYPE_DOUBLE)
		g_value_set_double (value, g_ascii_strtod (text, NULL));
	else
		lrg_property_value_from_string (value, text);
}

static gboolean
source_get_value (LrgPropertySource *source,
                  const gchar       *name,
                  GValue            *value)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (source);
	DataField           *f = source_field (self, name);
	LrgEditorDocument   *doc = source_document (self);
	g_auto(GValue)       raw = G_VALUE_INIT;

	if (f == NULL || doc == NULL)
		return FALSE;
	if (self->read_func != NULL)
	{
		g_value_init (value, f->type);
		if (self->read_func (self, name, value, self->read_data))
			return TRUE;
		g_value_unset (value);
	}
	if (!lrg_editor_document_has_path (doc, f->path) ||
	    lrg_editor_document_get_kind (doc, f->path) != LRG_EDITOR_DOCUMENT_NODE_SCALAR)
	{
		value_from_text (f->type, f->default_text, value);
		return TRUE;
	}
	if (f->type == G_TYPE_STRING)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_editor_document_get_scalar (doc, f->path));
		return TRUE;
	}
	if (lrg_editor_document_get_value (doc, f->path, &raw) && G_VALUE_TYPE (&raw) != G_TYPE_POINTER)
	{
		g_value_init (value, f->type);
		if (g_value_transform (&raw, value))
			return TRUE;
		g_value_unset (value);
	}
	value_from_text (f->type, lrg_editor_document_get_scalar (doc, f->path), value);
	return TRUE;
}

static gboolean
source_set_value (LrgPropertySource *source,
                  const gchar       *name,
                  const GValue      *value)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (source);
	DataField           *f = source_field (self, name);
	LrgEditorDocument   *doc = source_document (self);
	g_auto(GValue)       v = G_VALUE_INIT;
	g_autoptr(GError)    error = NULL;
	gboolean             ok;

	if (f == NULL || doc == NULL)
		return FALSE;
	g_value_init (&v, f->type);
	if (!g_value_transform (value, &v))
		return FALSE;
	if (self->write_func != NULL && !self->write_func (self, name, &v, self->write_data))
		return FALSE;

	/* an unchanged value is no command (the grid re-applies on commit) */
	{
		g_auto(GValue) cur = G_VALUE_INIT;

		if ((self->read_func != NULL || lrg_editor_document_has_path (doc, f->path)) &&
		    source_get_value (source, name, &cur) &&
		    lrg_property_values_equal (&cur, &v))
			return TRUE;
	}
	/* missing parent mappings ("/colors" of "/colors/text") are created
	 * first, in one undo group with the write */
	{
		g_auto(GStrv) parts = lrg_editor_document_path_split (f->path, NULL);
		g_autofree gchar *parent = g_strdup ("");
		gboolean          grouped = FALSE;
		guint             i;

		ok = TRUE;
		for (i = 0; ok && parts != NULL && parts[i] != NULL && parts[i + 1] != NULL; i++)
		{
			g_autofree gchar *next = lrg_editor_document_path_join (parent, parts[i]);

			if (!lrg_editor_document_has_path (doc, next))
			{
				if (!grouped)
					lrg_editor_document_begin_group (doc, lrg_property_info_get_label (f->info));
				grouped = TRUE;
				ok = lrg_editor_document_add_key (doc, parent, parts[i], "{}", -1, &error);
			}
			g_free (parent);
			parent = g_steal_pointer (&next);
		}
		if (ok)
			ok = lrg_editor_document_set_value (doc, f->path, &v, grouped ? 0 : self->gesture, &error);
		if (grouped)
			lrg_editor_document_end_group (doc);
	}
	if (!ok)
	{
		if (self->editor != NULL)
			lrg_editor_data_editor_report_error (self->editor, error);
		return FALSE;
	}
	if (self->gesture != 0)
		self->wrote = TRUE;
	return TRUE;
}

static LrgPropertyState
source_get_state (LrgPropertySource *source,
                  const gchar       *name)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (source);
	DataField           *f = source_field (self, name);
	LrgEditorDocument   *doc = source_document (self);

	if (f == NULL || doc == NULL)
		return LRG_PROPERTY_STATE_NONE;
	if (self->read_func != NULL)
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_value_init (&v, f->type);
		if (self->read_func (self, name, &v, self->read_data))
			return LRG_PROPERTY_STATE_NONE;
	}
	return lrg_editor_document_has_path (doc, f->path) ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
}

static gboolean
source_reset (LrgPropertySource *source,
              const gchar       *name)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (source);
	DataField           *f = source_field (self, name);
	LrgEditorDocument   *doc = source_document (self);
	g_autoptr(GError)    error = NULL;

	if (f == NULL || doc == NULL)
		return FALSE;
	if (!lrg_editor_document_has_path (doc, f->path))
		return TRUE;
	if (!lrg_editor_document_remove (doc, f->path, &error))
	{
		lrg_editor_data_editor_report_error (self->editor, error);
		return FALSE;
	}
	return TRUE;
}

static void
data_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = source_list_properties;
	iface->get_value = source_get_value;
	iface->set_value = source_set_value;
	iface->get_state = source_get_state;
	iface->reset = source_reset;
}

static void
lrg_editor_data_source_finalize (GObject *object)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (object);

	if (self->write_destroy != NULL)
		self->write_destroy (self->write_data);
	if (self->read_destroy != NULL)
		self->read_destroy (self->read_data);
	g_clear_weak_pointer (&self->editor);
	g_ptr_array_unref (self->fields);
	G_OBJECT_CLASS (lrg_editor_data_source_parent_class)->finalize (object);
}

static void
lrg_editor_data_source_class_init (LrgEditorDataSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_data_source_finalize;
}

static void
lrg_editor_data_source_init (LrgEditorDataSource *self)
{
	self->fields = g_ptr_array_new_with_free_func (data_field_free);
}

LrgEditorDataSource *
lrg_editor_data_source_new (LrgEditorDataEditor *editor)
{
	LrgEditorDataSource *self;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_EDITOR (editor), NULL);
	self = g_object_new (LRG_TYPE_EDITOR_DATA_SOURCE, NULL);
	g_set_weak_pointer (&self->editor, editor);
	return self;
}

void
lrg_editor_data_source_add_field (LrgEditorDataSource *self,
                                  const gchar         *name,
                                  const gchar         *path,
                                  GType                type,
                                  const gchar         *label,
                                  const gchar         *group,
                                  const gchar         *hint,
                                  const gchar         *default_text)
{
	DataField  *f;
	GParamSpec *pspec;

	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	g_return_if_fail (name != NULL && g_param_spec_is_valid_name (name));
	g_return_if_fail (path != NULL);
	g_return_if_fail (type == G_TYPE_STRING || type == G_TYPE_BOOLEAN || type == G_TYPE_INT64 ||
	                  type == G_TYPE_DOUBLE);

	if (type == G_TYPE_STRING)
		pspec = g_param_spec_string (name, label, NULL, default_text, G_PARAM_READWRITE);
	else if (type == G_TYPE_BOOLEAN)
		pspec = g_param_spec_boolean (name, label, NULL, FALSE, G_PARAM_READWRITE);
	else if (type == G_TYPE_INT64)
		pspec = g_param_spec_int64 (name, label, NULL, G_MININT64, G_MAXINT64, 0, G_PARAM_READWRITE);
	else
		pspec = g_param_spec_double (name, label, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);
	g_param_spec_ref_sink (pspec);

	f = g_new0 (DataField, 1);
	f->info = lrg_property_info_new (pspec);
	f->path = g_strdup (path);
	f->default_text = g_strdup (default_text);
	f->type = type;
	if (label != NULL)
		lrg_property_info_set_label (f->info, label);
	if (group != NULL)
		lrg_property_info_set_group (f->info, group);
	if (hint != NULL)
		lrg_property_info_set_hint (f->info, hint);
	g_param_spec_unref (pspec);
	g_ptr_array_add (self->fields, f);
}

void
lrg_editor_data_source_clear (LrgEditorDataSource *self)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	g_ptr_array_set_size (self->fields, 0);
}

guint
lrg_editor_data_source_get_n_fields (LrgEditorDataSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self), 0);
	return self->fields->len;
}

const gchar *
lrg_editor_data_source_get_field_path (LrgEditorDataSource *self,
                                       const gchar         *name)
{
	DataField *f;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self), NULL);
	f = source_field (self, name);
	return f != NULL ? f->path : NULL;
}

/* Whether @path is @prefix or lies below it ("/a" contains "/a/b"). */
static gboolean
path_within (const gchar *path,
             const gchar *prefix)
{
	gsize n = strlen (prefix);

	if (n == 0)
		return TRUE;
	return strncmp (path, prefix, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

const gchar *
lrg_editor_data_source_find_field (LrgEditorDataSource *self,
                                   const gchar         *path)
{
	const gchar *best = NULL;
	gsize        best_len = 0;
	guint        i;

	g_return_val_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	for (i = 0; i < self->fields->len; i++)
	{
		DataField *f = g_ptr_array_index (self->fields, i);

		/* exact or an ancestor of the field; else the field below @path */
		if (path_within (path, f->path) && (best == NULL || strlen (f->path) > best_len))
		{
			best = lrg_property_info_get_name (f->info);
			best_len = strlen (f->path);
		}
	}
	for (i = 0; best == NULL && i < self->fields->len; i++)
	{
		DataField *f = g_ptr_array_index (self->fields, i);

		if (path_within (f->path, path))
			best = lrg_property_info_get_name (f->info);
	}
	return best;
}

void
lrg_editor_data_source_document_changed (LrgEditorDataSource *self,
                                         const gchar         *path)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	if (path == NULL)
		path = "";
	for (i = 0; i < self->fields->len; i++)
	{
		DataField *f = g_ptr_array_index (self->fields, i);

		if (path_within (f->path, path) || path_within (path, f->path))
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), lrg_property_info_get_name (f->info));
	}
}

void
lrg_editor_data_source_set_write_func (LrgEditorDataSource          *self,
                                       LrgEditorDataSourceWriteFunc  func,
                                       gpointer                      user_data,
                                       GDestroyNotify                destroy)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	if (self->write_destroy != NULL)
		self->write_destroy (self->write_data);
	self->write_func = func;
	self->write_data = user_data;
	self->write_destroy = destroy;
}

void
lrg_editor_data_source_set_read_func (LrgEditorDataSource         *self,
                                      LrgEditorDataSourceReadFunc  func,
                                      gpointer                     user_data,
                                      GDestroyNotify               destroy)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	if (self->read_destroy != NULL)
		self->read_destroy (self->read_data);
	self->read_func = func;
	self->read_data = user_data;
	self->read_destroy = destroy;
}

static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (user_data);

	if (self->editor == NULL || source_field (self, name) == NULL)
		return;
	self->gesture = lrg_editor_data_editor_new_gesture (self->editor);
	self->grid_gesture = gesture_id;
	self->wrote = FALSE;
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit,
                  gpointer         user_data)
{
	LrgEditorDataSource *self = LRG_EDITOR_DATA_SOURCE (user_data);
	LrgEditorDocument   *doc = source_document (self);

	if (self->gesture == 0 || gesture_id != self->grid_gesture)
		return;
	/* a cancelled gesture leaves no undo step behind */
	if (!commit && self->wrote && doc != NULL)
		lrg_editor_document_undo (doc);
	self->gesture = 0;
	self->grid_gesture = 0;
	self->wrote = FALSE;
}

void
lrg_editor_data_source_bind_grid (LrgEditorDataSource *self,
                                  LrgPropertyGrid     *grid)
{
	g_return_if_fail (LRG_IS_EDITOR_DATA_SOURCE (self));
	g_return_if_fail (LRG_IS_PROPERTY_GRID (grid));

	lrg_property_grid_set_apply_mode (grid, LRG_PROPERTY_APPLY_DIRECT);
	g_signal_connect_object (grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self, 0);
	g_signal_connect_object (grid, "edit-end", G_CALLBACK (on_grid_edit_end), self, 0);
}
