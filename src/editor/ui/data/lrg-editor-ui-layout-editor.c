/* lrg-editor-ui-layout-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The UI layout editor: outline | YAML | live preview.
 *
 * Text flow. The YAML pane is the source of truth while the user types:
 * every edit restarts a debounce timer stepped by the canvas clock (the
 * preview frame's step vfunc, so tests drive it with frames). When it
 * fires, the text is loaded by a fresh LrgUILoader with source tracking;
 * on success the new root replaces the preview, on failure the old
 * preview stays and the error is shown (banner + line mark). Text that is
 * valid YAML is then written into the document with one
 * lrg_editor_document_replace() of the root, which is one undo step; the
 * resulting "changed" emission is ignored (the text already shows it).
 * Any other document change (undo, redo, IPC, reload) replaces the text
 * with the document's serialization and reloads the preview.
 *
 * Handlers. A document's `on:` entries name C handlers that only the game
 * registers; the preview registers a no-op under every name the loader
 * reports as unknown and loads again, so documents with handlers preview.
 *
 * Selection. Each preview widget knows its source pointer and line; the
 * outline holds one row per widget built from a node (repeat / for-each
 * copies share their template's pointer). The three panes select through
 * one function guarded against re-entry.
 */

#include "config.h"

#define LRG_EDITOR_DATA_INTERNAL
#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-ui-layout-editor.h"
#include "lrg-editor-hud-layout-editor.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-tree-view.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../ui/lrg-theme.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-icons.h"
#include <math.h>
#include <string.h>

#define DEBOUNCE        (0.35f)    /* seconds of quiet before the text applies */
#define MAX_HANDLERS    (64)       /* unknown handler names stubbed per load */
#define PREVIEW_PAD     (16.0f)

/* text editor geometry (lrg-text-editor.c): content inset */
#define TEXT_PAD_Y      (6.0f)

/* ==========================================================================
 * Outline rows
 * ========================================================================== */

#define UI_TYPE_OUTLINE_ITEM (ui_outline_item_get_type ())
G_DECLARE_FINAL_TYPE (UiOutlineItem, ui_outline_item, UI, OUTLINE_ITEM, GObject)

struct _UiOutlineItem
{
	GObject        parent_instance;

	gchar         *label;     /* "LrgButton  #play" */
	gchar         *path;      /* JSON pointer */
	guint          line;      /* 1-based, 0 unknown */
	guint          column;
	LrgWidget     *widget;    /* weak */
	UiOutlineItem *parent;    /* not a ref: the parent owns us */
	GListStore    *children;
};

enum { ITEM_PROP_0, ITEM_PROP_LABEL, ITEM_N_PROPS };

G_DEFINE_FINAL_TYPE (UiOutlineItem, ui_outline_item, G_TYPE_OBJECT)

static void
ui_outline_item_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	if (prop_id == ITEM_PROP_LABEL)
		g_value_set_string (value, UI_OUTLINE_ITEM (object)->label);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ui_outline_item_finalize (GObject *object)
{
	UiOutlineItem *self = UI_OUTLINE_ITEM (object);

	g_free (self->label);
	g_free (self->path);
	g_clear_weak_pointer (&self->widget);
	g_clear_object (&self->children);
	G_OBJECT_CLASS (ui_outline_item_parent_class)->finalize (object);
}

static void
ui_outline_item_class_init (UiOutlineItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = ui_outline_item_get_property;
	object_class->finalize = ui_outline_item_finalize;
	g_object_class_install_property (object_class, ITEM_PROP_LABEL,
	                                 g_param_spec_string ("label", NULL, NULL, NULL,
	                                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
ui_outline_item_init (UiOutlineItem *self)
{
	self->children = g_list_store_new (UI_TYPE_OUTLINE_ITEM);
}

/* ==========================================================================
 * The editor
 * ========================================================================== */

typedef struct _UiFrame UiFrame;
typedef struct _UiYamlPane UiYamlPane;

struct _LrgEditorUiLayoutEditor
{
	LrgEditorDataEditor  parent_instance;

	/* Widgets */
	LrgSplitter         *outer;          /* outline | inner */
	LrgSplitter         *inner;          /* yaml | preview */
	LrgTreeView         *tree;
	LrgTextEditor       *text;
	LrgWidget           *yaml_pane;      /* UiYamlPane holding text */
	LrgWidget           *frame;          /* UiFrame holding the preview */
	LrgWidget           *shield;         /* pick overlay inside the frame */
	LrgWidget           *pick_chip;

	/* Preview */
	LrgUILoader         *loader;         /* the loader of the current preview */
	LrgWidget           *root;           /* preview root (child of frame) */
	GPtrArray           *handlers;       /* stubbed handler names */
	gchar               *error;
	guint                error_line;

	/* Outline */
	GListStore          *outline;        /* top-level UiOutlineItem */
	GPtrArray           *items;          /* every UiOutlineItem, DFS order (refs) */

	/* Selection */
	gchar               *selected_path;
	LrgWidget           *selected;       /* weak */
	LrgWidget           *hovered;        /* weak, pick mode hover */
	gboolean             selecting;      /* re-entry guard */

	/* Text flow */
	gboolean             pending;
	gfloat               pending_time;
	gboolean             syncing;        /* setting the text from the document */
	gboolean             writing;        /* writing the text into the document */
	gboolean             pick_mode;
};

G_DEFINE_FINAL_TYPE (LrgEditorUiLayoutEditor, lrg_editor_ui_layout_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static gboolean ui_step (LrgEditorUiLayoutEditor *self, gfloat dt);

static LrgEditorDocument *
doc_of (LrgEditorUiLayoutEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

/* ==========================================================================
 * UiFrame: the preview's container (backdrop, highlight, error banner,
 * debounce clock)
 * ========================================================================== */

#define UI_TYPE_FRAME (ui_frame_get_type ())
G_DECLARE_FINAL_TYPE (UiFrame, ui_frame, UI, FRAME, LrgContainer)

struct _UiFrame
{
	LrgContainer             parent_instance;
	LrgEditorUiLayoutEditor *owner;   /* not a ref: the owner holds us */
};

G_DEFINE_FINAL_TYPE (UiFrame, ui_frame, LRG_TYPE_CONTAINER)

/* Wraps @text to @max_w using the measured width, one line per entry. */
static GPtrArray *
wrap_text (GrlFont     *font,
           const gchar *text,
           gfloat       size,
           gfloat       max_w)
{
	GPtrArray       *lines = g_ptr_array_new_with_free_func (g_free);
	g_auto(GStrv)    words = g_strsplit (text != NULL ? text : "", " ", -1);
	g_autoptr(GString) cur = g_string_new (NULL);
	guint            i;

	for (i = 0; words[i] != NULL; i++)
	{
		g_autofree gchar *probe = cur->len > 0 ? g_strconcat (cur->str, " ", words[i], NULL) : g_strdup (words[i]);
		gfloat            w = 0.0f;

		lrg_paint_measure_text (font, probe, size, 0.0f, &w, NULL);
		if (w > max_w && cur->len > 0)
		{
			g_ptr_array_add (lines, g_strdup (cur->str));
			g_string_assign (cur, words[i]);
		}
		else
			g_string_assign (cur, probe);
	}
	if (cur->len > 0)
		g_ptr_array_add (lines, g_strdup (cur->str));
	return lines;
}

static void
ui_frame_paint (LrgWidget    *widget,
                LrgPaintList *list)
{
	UiFrame                 *frame = UI_FRAME (widget);
	LrgEditorUiLayoutEditor *self = frame->owner;
	LrgTheme                *theme = lrg_widget_get_theme (widget);
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                   radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat                   x = lrg_widget_get_world_x (widget);
	gfloat                   y = lrg_widget_get_world_y (widget);
	gfloat                   w = lrg_widget_get_width (widget);
	gfloat                   h = lrg_widget_get_height (widget);
	const GrlColor          *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor          *border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);

	/* --- backdrop: the panel colour with a framed "screen" --- */
	lrg_paint_list_rect (list, x, y, w, h, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	lrg_paint_list_rect (list, x + PREVIEW_PAD * 0.5f, y + PREVIEW_PAD * 0.5f, w - PREVIEW_PAD, h - PREVIEW_PAD,
	                     radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	lrg_paint_list_border (list, x + PREVIEW_PAD * 0.5f, y + PREVIEW_PAD * 0.5f, w - PREVIEW_PAD, h - PREVIEW_PAD,
	                       radius, 1.0f, border);

	/* --- the preview widgets --- */
	lrg_paint_list_push_clip (list, x + PREVIEW_PAD * 0.5f, y + PREVIEW_PAD * 0.5f, w - PREVIEW_PAD, h - PREVIEW_PAD);
	LRG_WIDGET_CLASS (ui_frame_parent_class)->paint (widget, list);
	lrg_paint_list_pop_clip (list);
	if (self == NULL)
		return;

	/* --- hover and selection outlines --- */
	if (self->pick_mode && self->hovered != NULL && self->hovered != self->selected)
	{
		GrlColor soft = editor_data_color_alpha (accent, 0.55f);

		lrg_paint_list_border (list, lrg_widget_get_world_x (self->hovered), lrg_widget_get_world_y (self->hovered),
		                       lrg_widget_get_width (self->hovered), lrg_widget_get_height (self->hovered), 3.0f,
		                       1.0f, &soft);
	}
	if (self->selected != NULL)
	{
		gfloat   sx = lrg_widget_get_world_x (self->selected);
		gfloat   sy = lrg_widget_get_world_y (self->selected);
		gfloat   sw = lrg_widget_get_width (self->selected);
		gfloat   sh = lrg_widget_get_height (self->selected);
		GrlColor tint = editor_data_color_alpha (accent, 0.12f);
		const gchar *type = G_OBJECT_TYPE_NAME (self->selected);
		gfloat   tw = 0.0f;

		lrg_paint_list_rect (list, sx, sy, sw, sh, 3.0f, &tint);
		lrg_paint_list_border (list, sx, sy, sw, sh, 3.0f, 2.0f, accent);
		lrg_paint_measure_text (font, type, fs, 0.0f, &tw, NULL);
		lrg_paint_list_rect (list, sx, MAX (y, sy - fs - 6.0f), tw + 10.0f, fs + 6.0f, 3.0f, accent);
		lrg_paint_list_text (list, font, type, sx + 5.0f, MAX (y, sy - fs - 6.0f) + 3.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
	}

	/* --- the load error, over the bottom of the preview --- */
	if (self->error != NULL)
	{
		const GrlColor     *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
		GrlColor            fill = editor_data_color_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP), 0.96f);
		g_autoptr(GPtrArray) lines = wrap_text (font, self->error, fs, MAX (40.0f, w - PREVIEW_PAD * 2.0f - 40.0f));
		gfloat              lh = fs * 1.35f;
		gfloat              bh = MIN (h * 0.6f, (gfloat) lines->len * lh + 16.0f);
		gfloat              by = y + h - PREVIEW_PAD - bh;
		guint               i;

		lrg_paint_list_rect (list, x + PREVIEW_PAD, by, w - PREVIEW_PAD * 2.0f, bh, radius, &fill);
		lrg_paint_list_rect (list, x + PREVIEW_PAD, by, 4.0f, bh, 2.0f, danger);
		lrg_paint_list_icon (list, LRG_ICON_WARNING, x + PREVIEW_PAD + 12.0f, by + 8.0f, fs + 2.0f, 1.5f, danger);
		lrg_paint_list_push_clip (list, x + PREVIEW_PAD, by, w - PREVIEW_PAD * 2.0f, bh);
		for (i = 0; i < lines->len; i++)
			lrg_paint_list_text (list, font, g_ptr_array_index (lines, i), x + PREVIEW_PAD + 34.0f,
			                     by + 8.0f + (gfloat) i * lh, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_pop_clip (list);
	}
	else if (self->root == NULL)
		lrg_paint_list_text (list, font, "Nothing to preview yet", x + PREVIEW_PAD * 1.5f, y + PREVIEW_PAD * 1.5f, fs,
		                     0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

/* The preview root fills the screen area; the pick shield covers it. */
static void
ui_frame_layout_children (LrgContainer *container)
{
	UiFrame *frame = UI_FRAME (container);
	gfloat   w = lrg_widget_get_width (LRG_WIDGET (container));
	gfloat   h = lrg_widget_get_height (LRG_WIDGET (container));
	GList   *l;

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = l->data;

		if (frame->owner != NULL && child == frame->owner->shield)
			lrg_widget_place_in_slot (child, 0.0f, 0.0f, w, h);
		else
			lrg_widget_place_in_slot (child, PREVIEW_PAD, PREVIEW_PAD, MAX (0.0f, w - PREVIEW_PAD * 2.0f),
			                          MAX (0.0f, h - PREVIEW_PAD * 2.0f));
	}
}

static gboolean
ui_frame_step (LrgWidget *widget,
               gfloat     delta_time)
{
	UiFrame *frame = UI_FRAME (widget);
	gboolean moving = LRG_WIDGET_CLASS (ui_frame_parent_class)->step != NULL &&
	                  LRG_WIDGET_CLASS (ui_frame_parent_class)->step (widget, delta_time);

	if (frame->owner != NULL && ui_step (frame->owner, delta_time))
		moving = TRUE;
	return moving;
}

static void
ui_frame_measure (LrgWidget *widget,
                  gfloat    *preferred_width,
                  gfloat    *preferred_height)
{
	*preferred_width = 240.0f;
	*preferred_height = 200.0f;
}

static void
ui_frame_class_init (UiFrameClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->paint = ui_frame_paint;
	widget_class->step = ui_frame_step;
	widget_class->measure = ui_frame_measure;
	LRG_CONTAINER_CLASS (klass)->layout_children = ui_frame_layout_children;
}

static void
ui_frame_init (UiFrame *self)
{
}

/* ==========================================================================
 * UiYamlPane: the text editor plus line marks
 * ========================================================================== */

#define UI_TYPE_YAML_PANE (ui_yaml_pane_get_type ())
G_DECLARE_FINAL_TYPE (UiYamlPane, ui_yaml_pane, UI, YAML_PANE, LrgContainer)

struct _UiYamlPane
{
	LrgContainer             parent_instance;
	LrgEditorUiLayoutEditor *owner;   /* not a ref */
};

G_DEFINE_FINAL_TYPE (UiYamlPane, ui_yaml_pane, LRG_TYPE_CONTAINER)

/* A full-width band behind / a bar beside 1-based @line of the editor. */
static void
mark_line (LrgEditorUiLayoutEditor *self,
           LrgPaintList            *list,
           guint                    line,
           const GrlColor          *color,
           gfloat                   alpha,
           gboolean                 bar)
{
	LrgWidget *w = LRG_WIDGET (self->text);
	gfloat     lh = lrg_text_editor_get_line_height (self->text);
	gfloat     sy = 0.0f;
	gfloat     top;
	GrlColor   band = editor_data_color_alpha (color, alpha);

	if (line == 0 || lh <= 0.0f)
		return;
	lrg_text_editor_get_scroll (self->text, NULL, &sy);
	top = lrg_widget_get_world_y (w) + TEXT_PAD_Y + (gfloat) (line - 1) * lh - sy;
	if (top + lh < lrg_widget_get_world_y (w) || top > lrg_widget_get_world_y (w) + lrg_widget_get_height (w))
		return;
	lrg_paint_list_push_clip (list, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                          lrg_widget_get_height (w));
	lrg_paint_list_rect (list, lrg_widget_get_world_x (w), top, lrg_widget_get_width (w), lh, 0.0f, &band);
	if (bar)
		lrg_paint_list_rect (list, lrg_widget_get_world_x (w), top, 3.0f, lh, 0.0f, color);
	lrg_paint_list_pop_clip (list);
}

static void
ui_yaml_pane_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgEditorUiLayoutEditor *self = UI_YAML_PANE (widget)->owner;
	LrgTheme                *theme = lrg_widget_get_theme (widget);
	GPtrArray               *issues;
	guint                    i;

	LRG_WIDGET_CLASS (ui_yaml_pane_parent_class)->paint (widget, list);
	if (self == NULL || self->text == NULL)
		return;

	/* the selected node's line */
	if (self->selected_path != NULL && self->items != NULL)
	{
		for (i = 0; i < self->items->len; i++)
		{
			UiOutlineItem *item = g_ptr_array_index (self->items, i);

			if (item->widget == self->selected && item->line > 0)
			{
				mark_line (self, list, item->line, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.10f, TRUE);
				break;
			}
		}
	}

	/* the document's validation issues, while the text is the document's */
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (self));
	for (i = 0; issues != NULL && !self->pending && i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		mark_line (self, list, issue->line,
		           lrg_theme_get_color (theme, issue->severity == LRG_EDITOR_PROBLEM_ERROR ? LRG_THEME_COLOR_DANGER
		                                                                                   : LRG_THEME_COLOR_WARNING),
		           0.12f, TRUE);
	}

	/* the preview's load error */
	if (self->error != NULL)
		mark_line (self, list, self->error_line, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER), 0.22f, TRUE);
}

static void
ui_yaml_pane_layout_children (LrgContainer *container)
{
	GList *l;

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
		lrg_widget_place_in_slot (l->data, 0.0f, 0.0f, lrg_widget_get_width (LRG_WIDGET (container)),
		                          lrg_widget_get_height (LRG_WIDGET (container)));
}

static void
ui_yaml_pane_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	*preferred_width = 300.0f;
	*preferred_height = 200.0f;
}

static void
ui_yaml_pane_class_init (UiYamlPaneClass *klass)
{
	LRG_WIDGET_CLASS (klass)->paint = ui_yaml_pane_paint;
	LRG_WIDGET_CLASS (klass)->measure = ui_yaml_pane_measure;
	LRG_CONTAINER_CLASS (klass)->layout_children = ui_yaml_pane_layout_children;
}

static void
ui_yaml_pane_init (UiYamlPane *self)
{
}

/* ==========================================================================
 * Outline
 * ========================================================================== */

/* Adds an item for every widget with a source pointer under @widget;
 * widgets without one (a container's internals) are flattened. */
static void
outline_add (LrgEditorUiLayoutEditor *self,
             LrgWidget               *widget,
             UiOutlineItem           *parent,
             GListStore              *store)
{
	const gchar   *path = lrg_ui_loader_get_source_path (self->loader, widget);
	UiOutlineItem *item = NULL;
	GListStore    *child_store = store;
	GList         *l;

	if (path != NULL)
	{
		const gchar *name = lrg_widget_get_name (widget);

		item = g_object_new (UI_TYPE_OUTLINE_ITEM, NULL);
		item->path = g_strdup (path);
		item->parent = parent;
		g_set_weak_pointer (&item->widget, widget);
		lrg_ui_loader_get_source_location (self->loader, widget, &item->line, &item->column);
		item->label = name != NULL && *name != '\0'
		              ? g_strdup_printf ("%s  #%s", G_OBJECT_TYPE_NAME (widget), name)
		              : g_strdup (G_OBJECT_TYPE_NAME (widget));
		g_list_store_append (store, item);
		g_ptr_array_add (self->items, item);   /* takes the creation ref */
		child_store = item->children;
		parent = item;
	}
	if (LRG_IS_CONTAINER (widget))
		for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
			outline_add (self, l->data, parent, child_store);
}

static GListModel *
outline_children (LrgTreeView *tree,
                  GObject     *item,
                  gpointer     user_data)
{
	UiOutlineItem *it = UI_OUTLINE_ITEM (item);

	/* leaves are known when the outline is built (a fresh model each time) */
	return g_list_model_get_n_items (G_LIST_MODEL (it->children)) > 0
	       ? G_LIST_MODEL (g_object_ref (it->children)) : NULL;
}

static GObject *
outline_parent (LrgTreeView *tree,
                GObject     *item,
                gpointer     user_data)
{
	return (GObject *) UI_OUTLINE_ITEM (item)->parent;
}

static UiOutlineItem *
item_for_widget (LrgEditorUiLayoutEditor *self,
                 LrgWidget               *widget)
{
	guint i;

	for (i = 0; widget != NULL && i < self->items->len; i++)
		if (((UiOutlineItem *) g_ptr_array_index (self->items, i))->widget == widget)
			return g_ptr_array_index (self->items, i);
	return NULL;
}

static UiOutlineItem *
item_for_path (LrgEditorUiLayoutEditor *self,
               const gchar             *path)
{
	g_autofree gchar *norm = NULL;
	guint             i;

	if (path == NULL)
		return NULL;
	norm = g_strcmp0 (path, "/") == 0 ? g_strdup ("") : g_strdup (path);
	for (i = 0; i < self->items->len; i++)
		if (g_strcmp0 (((UiOutlineItem *) g_ptr_array_index (self->items, i))->path, norm) == 0)
			return g_ptr_array_index (self->items, i);
	return NULL;
}

/* The deepest node starting at or before 1-based @line. */
static UiOutlineItem *
item_for_line (LrgEditorUiLayoutEditor *self,
               guint                    line)
{
	UiOutlineItem *best = NULL;
	guint          i;

	for (i = 0; i < self->items->len; i++)
	{
		UiOutlineItem *it = g_ptr_array_index (self->items, i);

		if (it->line > 0 && it->line <= line && (best == NULL || it->line >= best->line))
			best = it;
	}
	return best;
}

static void
rebuild_outline (LrgEditorUiLayoutEditor *self)
{
	/* a fresh model, filled before the tree sees it (the tree caches each
	 * item's children the first time it shows the item) */
	g_ptr_array_set_size (self->items, 0);
	g_clear_object (&self->outline);
	self->outline = g_list_store_new (UI_TYPE_OUTLINE_ITEM);
	if (self->root != NULL && self->loader != NULL)
		outline_add (self, self->root, NULL, self->outline);
	/* a new root model: the tree drops expansion; open the first level */
	lrg_tree_view_set_model (self->tree, G_LIST_MODEL (self->outline));
	if (g_list_model_get_n_items (G_LIST_MODEL (self->outline)) > 0)
	{
		g_autoptr(GObject) first = g_list_model_get_item (G_LIST_MODEL (self->outline), 0);

		lrg_tree_view_expand (self->tree, first);
	}
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

/* Selects @item in every pane except the one the selection came from. */
static void
select_item (LrgEditorUiLayoutEditor *self,
             UiOutlineItem           *item,
             gboolean                 move_caret,
             gboolean                 update_tree)
{
	if (self->selecting)
		return;
	self->selecting = TRUE;

	g_clear_pointer (&self->selected_path, g_free);
	g_clear_weak_pointer (&self->selected);
	if (item != NULL)
	{
		self->selected_path = g_strdup (item->path);
		g_set_weak_pointer (&self->selected, item->widget);
	}
	if (update_tree)
	{
		if (item != NULL)
		{
			lrg_tree_view_reveal (self->tree, G_OBJECT (item));
			lrg_tree_view_select_item (self->tree, G_OBJECT (item));
		}
		else
			lrg_tree_view_unselect_all (self->tree);
	}
	if (move_caret && item != NULL && item->line > 0)
	{
		self->syncing = TRUE;
		lrg_text_editor_set_cursor (self->text, item->line - 1, item->column > 0 ? item->column - 1 : 0);
		self->syncing = FALSE;
	}
	self->selecting = FALSE;
}

static void
on_tree_selection (LrgTreeView *tree,
                   gpointer     user_data)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (user_data);
	GObject                 *item = lrg_tree_view_get_selected_item (tree);

	if (self->selecting || item == NULL)
		return;
	select_item (self, UI_OUTLINE_ITEM (item), TRUE, FALSE);
}

static void
on_cursor_moved (LrgTextEditor *text,
                 guint          line,
                 guint          column,
                 gpointer       user_data)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (user_data);
	UiOutlineItem           *item;

	if (self->syncing || self->selecting || self->pending)
		return;
	item = item_for_line (self, line + 1);
	if (item != NULL && item->widget != self->selected)
		select_item (self, item, FALSE, TRUE);
}

/* The deepest preview widget with a source pointer under a point. */
static LrgWidget *
pick_widget (LrgEditorUiLayoutEditor *self,
             LrgWidget               *widget,
             gfloat                   x,
             gfloat                   y)
{
	GList *l;

	if (widget == NULL || !lrg_widget_get_visible (widget) || !lrg_widget_contains_point (widget, x, y))
		return NULL;
	if (LRG_IS_CONTAINER (widget))
		for (l = g_list_last (lrg_container_get_children (LRG_CONTAINER (widget))); l != NULL; l = l->prev)
		{
			LrgWidget *hit = pick_widget (self, l->data, x, y);

			if (hit != NULL)
				return hit;
		}
	return lrg_ui_loader_get_source_path (self->loader, widget) != NULL ? widget : NULL;
}

static gboolean
shield_event (LrgCustomWidget  *widget,
              const LrgUIEvent *event,
              gpointer          user_data)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (user_data);
	gfloat                   x = lrg_ui_event_get_x (event);
	gfloat                   y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		g_set_weak_pointer (&self->hovered, pick_widget (self, self->root, x, y));
		return TRUE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		g_clear_weak_pointer (&self->hovered);
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) == 0)
		{
			LrgWidget *hit = pick_widget (self, self->root, x, y);

			select_item (self, item_for_widget (self, hit), TRUE, TRUE);
		}
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
	case LRG_UI_EVENT_SCROLL:
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Loading the preview
 * ========================================================================== */

/* Every `on:` handler of the preview does nothing. */
static void
noop_handler (void)
{
}

/* "unknown handler 'NAME' for signal" -> NAME, or NULL. */
static gchar *
unknown_handler_name (const GError *error)
{
	const gchar *start;
	const gchar *end;

	if (!g_error_matches (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_HANDLER))
		return NULL;
	start = strstr (error->message, "unknown handler '");
	if (start == NULL)
		return NULL;
	start += strlen ("unknown handler '");
	end = strchr (start, '\'');
	return end != NULL && end > start ? g_strndup (start, (gsize) (end - start)) : NULL;
}

/* The 1-based line an error message names ("line 3", ":3:5"), or 0. */
static guint
message_line (const gchar *message)
{
	const gchar *p;

	if (message == NULL)
		return 0;
	p = strstr (message, "line ");
	if (p != NULL && g_ascii_isdigit (p[5]))
		return (guint) g_ascii_strtoull (p + 5, NULL, 10);
	p = strstr (message, "(at ");
	if (p != NULL)
	{
		const gchar *colon = strchr (p, ':');

		if (colon != NULL && g_ascii_isdigit (colon[1]))
			return (guint) g_ascii_strtoull (colon + 1, NULL, 10);
	}
	return 0;
}

static void
set_error (LrgEditorUiLayoutEditor *self,
           const gchar             *message,
           guint                    line)
{
	g_free (self->error);
	self->error = g_strdup (message);
	self->error_line = message != NULL ? (line > 0 ? line : message_line (message)) : 0;
}

static void
set_root (LrgEditorUiLayoutEditor *self,
          LrgWidget               *root)
{
	if (self->root != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self->frame), self->root);
	self->root = root;
	g_clear_weak_pointer (&self->hovered);
	if (root != NULL)
	{
		/* below the pick shield */
		lrg_container_insert_child_at (LRG_CONTAINER (self->frame), root, 0);
	}
}

/*
 * Loads @text into a new preview. Returns FALSE (and sets the error) when
 * it does not load; @parse_error tells whether the text is not even YAML.
 */
static gboolean
load_preview (LrgEditorUiLayoutEditor *self,
              const gchar             *text,
              gboolean                *parse_error)
{
	g_autoptr(LrgUILoader) loader = NULL;
	g_autoptr(GError)      error = NULL;
	LrgWidget             *root = NULL;
	g_autofree gchar      *path = g_strdup (self->selected_path);
	guint                  attempt, i;

	*parse_error = FALSE;
	loader = lrg_ui_loader_new ();
	lrg_ui_loader_set_track_sources (loader, TRUE);
	for (i = 0; i < self->handlers->len; i++)
		lrg_ui_loader_add_handler (loader, g_ptr_array_index (self->handlers, i), G_CALLBACK (noop_handler), NULL);

	/* load, stubbing each unknown handler the loader names */
	for (attempt = 0; attempt < MAX_HANDLERS; attempt++)
	{
		g_autofree gchar *handler = NULL;

		g_clear_error (&error);
		root = lrg_ui_loader_load_from_data (loader, text, -1, &error);
		if (root != NULL)
			break;
		handler = unknown_handler_name (error);
		if (handler == NULL)
			break;
		lrg_ui_loader_add_handler (loader, handler, G_CALLBACK (noop_handler), NULL);
		g_ptr_array_add (self->handlers, g_steal_pointer (&handler));
	}

	if (root == NULL)
	{
		*parse_error = g_error_matches (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_PARSE);
		set_error (self, error != NULL ? error->message : "the document did not load", 0);
		return FALSE;
	}

	/* swap in the new preview, keep the selection by path */
	set_error (self, NULL, 0);
	set_root (self, NULL);
	g_set_object (&self->loader, loader);
	set_root (self, root);
	g_object_unref (root);
	rebuild_outline (self);
	g_clear_weak_pointer (&self->selected);
	g_clear_pointer (&self->selected_path, g_free);
	if (path != NULL)
		select_item (self, item_for_path (self, path), FALSE, TRUE);
	return TRUE;
}

/* The document's text -> the YAML pane (keeping the caret line). */
static void
sync_text_from_document (LrgEditorUiLayoutEditor *self)
{
	g_autofree gchar *data = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar *current = lrg_text_editor_get_text (self->text);
	guint             line = 0, column = 0;

	if (g_strcmp0 (data, current) == 0)
		return;
	lrg_text_editor_get_cursor (self->text, &line, &column);
	self->syncing = TRUE;
	lrg_text_editor_set_text (self->text, data);
	lrg_text_editor_set_cursor (self->text, line, column);
	self->syncing = FALSE;
}

static gboolean
apply_text (LrgEditorUiLayoutEditor *self)
{
	g_autofree gchar *text = lrg_text_editor_get_text (self->text);
	g_autofree gchar *data = NULL;
	gboolean          parse_error = FALSE;
	gboolean          loaded;

	self->pending = FALSE;
	self->pending_time = 0.0f;
	loaded = load_preview (self, text, &parse_error);
	if (parse_error)
		return FALSE;

	/* valid YAML: it becomes the document (one undo step) */
	data = lrg_editor_document_to_data (doc_of (self), NULL);
	if (g_strcmp0 (data, text) != 0)
	{
		g_autoptr(GError) error = NULL;

		self->writing = TRUE;
		if (!lrg_editor_document_replace (doc_of (self), "", text, &error) && loaded)
			set_error (self, error->message, 0);
		self->writing = FALSE;
	}
	return loaded && self->error == NULL;
}

static void
on_text_changed (LrgTextEditor *text,
                 gpointer       user_data)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (user_data);

	if (self->syncing)
		return;
	self->pending = TRUE;
	self->pending_time = 0.0f;
}

/* The debounce, on the canvas clock. */
static gboolean
ui_step (LrgEditorUiLayoutEditor *self,
         gfloat                   dt)
{
	if (!self->pending)
		return FALSE;
	self->pending_time += dt;
	if (self->pending_time >= DEBOUNCE)
		apply_text (self);
	return self->pending;
}

/* ==========================================================================
 * Toolbar
 * ========================================================================== */

static void
on_pick_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (user_data);

	lrg_editor_ui_layout_editor_set_pick_mode (self, !self->pick_mode);
}

static void
on_apply_chip (LrgWidget *chip,
               gpointer   user_data)
{
	lrg_editor_ui_layout_editor_apply (LRG_EDITOR_UI_LAYOUT_EDITOR (user_data));
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

/* A change that did not come from the text: show the document's text. */
static void
ui_refresh (LrgEditorDataEditor *editor,
            const gchar         *path)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (editor);
	g_autofree gchar        *text = NULL;
	gboolean                 parse_error;

	if (self->writing || self->text == NULL)
		return;
	sync_text_from_document (self);
	self->pending = FALSE;
	text = lrg_text_editor_get_text (self->text);
	load_preview (self, text, &parse_error);
}

/* An issue's line (or the line of its path) gets the caret. */
static void
ui_reveal (LrgEditorDataEditor *editor,
           const gchar         *path,
           guint                line)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (editor);
	UiOutlineItem           *item = NULL;

	if (line == 0 && path != NULL)
		lrg_editor_document_get_line (doc_of (self), path, &line, NULL);
	if (path != NULL)
	{
		g_autofree gchar *probe = g_strdup (path);

		/* the widget node holding the path: strip keys until one matches */
		while (item == NULL && probe != NULL)
		{
			gchar *slash;

			item = item_for_path (self, probe);
			slash = strrchr (probe, '/');
			if (item != NULL || slash == NULL)
				break;
			*slash = '\0';
		}
	}
	if (item != NULL)
		select_item (self, item, FALSE, TRUE);
	if (line > 0)
	{
		self->syncing = TRUE;
		lrg_text_editor_set_cursor (self->text, line - 1, 0);
		self->syncing = FALSE;
	}
}

static gchar *
ui_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (editor);

	return g_strdup_printf ("widgets=%u selected=%s pick=%s error=%s", self->items->len,
	                        self->selected_path != NULL ? (*self->selected_path != '\0' ? self->selected_path : "/")
	                                                    : "",
	                        self->pick_mode ? "true" : "false", self->error != NULL ? self->error : "");
}

static void
lrg_editor_ui_layout_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs shield_funcs = { NULL, NULL, NULL, shield_event, { NULL } };
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (object);
	LrgEditorDataEditor     *editor = LRG_EDITOR_DATA_EDITOR (object);
	LrgWidget               *chip;
	g_autofree gchar        *data = NULL;
	gboolean                 parse_error;

	G_OBJECT_CLASS (lrg_editor_ui_layout_editor_parent_class)->constructed (object);

	/* --- toolbar --- */
	self->pick_chip = editor_chip_new ("ui-pick", LRG_ICON_SEARCH, "Pick", on_pick_chip, self);
	editor_chip_set_checked (self->pick_chip, TRUE);
	lrg_editor_data_editor_add_tool (editor, self->pick_chip);
	g_object_unref (self->pick_chip);
	chip = editor_chip_new ("ui-apply", LRG_ICON_PLAY, "Apply", on_apply_chip, self);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);

	/* --- outline --- */
	self->tree = lrg_tree_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tree), "ui-outline");
	lrg_tree_view_set_children_func (self->tree, outline_children, NULL, NULL);
	lrg_tree_view_set_parent_func (self->tree, outline_parent, NULL, NULL);
	lrg_tree_view_set_model (self->tree, G_LIST_MODEL (self->outline));
	g_signal_connect_object (self->tree, "selection-changed", G_CALLBACK (on_tree_selection), self, 0);

	/* --- YAML --- */
	self->text = lrg_text_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (self->text), "ui-yaml");
	lrg_text_editor_set_language (self->text, "yaml");
	lrg_text_editor_set_show_line_numbers (self->text, TRUE);
	lrg_text_editor_set_wrap (self->text, FALSE);
	lrg_text_editor_set_insert_spaces (self->text, TRUE);
	lrg_text_editor_set_tab_width (self->text, 2);
	self->yaml_pane = g_object_new (UI_TYPE_YAML_PANE, NULL);
	UI_YAML_PANE (self->yaml_pane)->owner = self;
	lrg_widget_set_name (self->yaml_pane, "ui-yaml-pane");
	lrg_container_add_child (LRG_CONTAINER (self->yaml_pane), LRG_WIDGET (self->text));
	g_object_unref (self->text);
	data = lrg_editor_document_to_data (doc_of (self), NULL);
	lrg_text_editor_set_text (self->text, data);
	lrg_text_editor_clear_history (self->text);
	lrg_text_editor_set_cursor (self->text, 0, 0);
	g_signal_connect_object (self->text, "changed", G_CALLBACK (on_text_changed), self, 0);
	g_signal_connect_object (self->text, "cursor-moved", G_CALLBACK (on_cursor_moved), self, 0);

	/* --- preview --- */
	self->frame = g_object_new (UI_TYPE_FRAME, NULL);
	UI_FRAME (self->frame)->owner = self;
	lrg_widget_set_name (self->frame, "ui-preview");
	self->shield = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->shield, "ui-pick-shield");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->shield), &shield_funcs, self, NULL);
	lrg_container_add_child (LRG_CONTAINER (self->frame), self->shield);
	g_object_unref (self->shield);

	/* --- splits: outline | (yaml | preview) --- */
	self->inner = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->inner), "ui-split-inner");
	lrg_splitter_set_start_child (self->inner, self->yaml_pane);
	g_object_unref (self->yaml_pane);
	lrg_splitter_set_end_child (self->inner, self->frame);
	g_object_unref (self->frame);
	lrg_splitter_set_fraction (self->inner, 0.5f, FALSE);
	lrg_splitter_set_min_sizes (self->inner, 160.0f, 160.0f);
	self->outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->outer), "ui-split");
	lrg_splitter_set_start_child (self->outer, LRG_WIDGET (self->tree));
	g_object_unref (self->tree);
	lrg_splitter_set_end_child (self->outer, LRG_WIDGET (self->inner));
	g_object_unref (self->inner);
	lrg_splitter_set_fraction (self->outer, 0.2f, FALSE);
	lrg_splitter_set_min_sizes (self->outer, 120.0f, 320.0f);
	lrg_editor_data_editor_set_content (editor, LRG_WIDGET (self->outer));
	g_object_unref (self->outer);

	load_preview (self, data, &parse_error);
}

static void
lrg_editor_ui_layout_editor_dispose (GObject *object)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (object);

	if (self->frame != NULL)
		UI_FRAME (self->frame)->owner = NULL;
	if (self->yaml_pane != NULL)
		UI_YAML_PANE (self->yaml_pane)->owner = NULL;
	if (self->shield != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->shield), NULL, NULL, NULL);
	self->frame = self->yaml_pane = self->shield = self->pick_chip = NULL;
	self->tree = NULL;
	self->text = NULL;
	self->outer = self->inner = NULL;
	self->root = NULL;
	g_clear_weak_pointer (&self->selected);
	g_clear_weak_pointer (&self->hovered);
	if (self->items != NULL)
		g_ptr_array_set_size (self->items, 0);
	if (self->outline != NULL)
		g_list_store_remove_all (self->outline);
	g_clear_object (&self->loader);
	G_OBJECT_CLASS (lrg_editor_ui_layout_editor_parent_class)->dispose (object);
}

static void
lrg_editor_ui_layout_editor_finalize (GObject *object)
{
	LrgEditorUiLayoutEditor *self = LRG_EDITOR_UI_LAYOUT_EDITOR (object);

	g_ptr_array_unref (self->handlers);
	g_ptr_array_unref (self->items);
	g_clear_object (&self->outline);
	g_free (self->error);
	g_free (self->selected_path);
	G_OBJECT_CLASS (lrg_editor_ui_layout_editor_parent_class)->finalize (object);
}

static void
lrg_editor_ui_layout_editor_class_init (LrgEditorUiLayoutEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *editor_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_ui_layout_editor_constructed;
	object_class->dispose = lrg_editor_ui_layout_editor_dispose;
	object_class->finalize = lrg_editor_ui_layout_editor_finalize;
	editor_class->refresh = ui_refresh;
	editor_class->reveal = ui_reveal;
	editor_class->get_state = ui_get_state;
}

static void
lrg_editor_ui_layout_editor_init (LrgEditorUiLayoutEditor *self)
{
	self->handlers = g_ptr_array_new_with_free_func (g_free);
	self->items = g_ptr_array_new_with_free_func (g_object_unref);
	self->outline = g_list_store_new (UI_TYPE_OUTLINE_ITEM);
	self->pick_mode = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorUiLayoutEditor *
lrg_editor_ui_layout_editor_new (LrgEditorApp      *app,
                                 LrgEditorDocument *document)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);

	return g_object_new (LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR, "app", app, "document", document, NULL);
}

LrgTextEditor *
lrg_editor_ui_layout_editor_get_text_editor (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), NULL);
	return self->text;
}

LrgUILoader *
lrg_editor_ui_layout_editor_get_loader (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), NULL);

	/* before the first successful load there is still a loader to hand out */
	if (self->loader == NULL)
	{
		self->loader = lrg_ui_loader_new ();
		lrg_ui_loader_set_track_sources (self->loader, TRUE);
	}
	return self->loader;
}

LrgWidget *
lrg_editor_ui_layout_editor_get_preview (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), NULL);
	return self->root;
}

gboolean
lrg_editor_ui_layout_editor_apply (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), FALSE);
	return apply_text (self);
}

gboolean
lrg_editor_ui_layout_editor_get_pending (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), FALSE);
	return self->pending;
}

const gchar *
lrg_editor_ui_layout_editor_get_error (LrgEditorUiLayoutEditor *self,
                                       guint                   *line)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), NULL);

	if (line != NULL)
		*line = self->error_line;
	return self->error;
}

const gchar *
lrg_editor_ui_layout_editor_get_selected_path (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), NULL);
	return self->selected_path;
}

gboolean
lrg_editor_ui_layout_editor_select_path (LrgEditorUiLayoutEditor *self,
                                         const gchar             *path)
{
	UiOutlineItem *item;

	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), FALSE);

	item = item_for_path (self, path);
	if (path != NULL && item == NULL)
		return FALSE;
	select_item (self, item, TRUE, TRUE);
	return TRUE;
}

gboolean
lrg_editor_ui_layout_editor_get_pick_mode (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), FALSE);
	return self->pick_mode;
}

void
lrg_editor_ui_layout_editor_set_pick_mode (LrgEditorUiLayoutEditor *self,
                                           gboolean                 pick)
{
	g_return_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self));

	self->pick_mode = pick;
	if (self->shield != NULL)
		lrg_widget_set_visible (self->shield, pick);
	if (self->pick_chip != NULL)
		editor_chip_set_checked (self->pick_chip, pick);
	if (!pick)
		g_clear_weak_pointer (&self->hovered);
}

guint
lrg_editor_ui_layout_editor_get_n_outline_rows (LrgEditorUiLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_UI_LAYOUT_EDITOR (self), 0);
	return self->items->len;
}
