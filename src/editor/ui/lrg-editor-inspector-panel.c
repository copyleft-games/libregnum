/* lrg-editor-inspector-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInspectorPanel: the editor's inspector on the retained UI
 * toolkit.
 *
 * Structure (top to bottom):
 *
 *   toolbar      title ("Cube", "3 nodes", "Level"), a filter field that
 *                filters every grid, and the lock toggle
 *   banner       "Live values - changes are discarded on Stop" while the
 *                play world is inspected
 *   prefab bar   for nodes of a linked prefab instance: source link,
 *                Select Source, Apply All, Revert All, Unlink (Relink and
 *                Open for 0.8.0 instances)
 *   scroll box   the sections, one LrgPropertyGrid each:
 *                  level      (nothing selected) name, default view, file,
 *                             node count, project; Project Settings
 *                  Node       LRG_EDITOR_NODE_SECTION_HEADER
 *                  Transform  LRG_EDITOR_NODE_SECTION_TRANSFORM
 *                  Visual     LRG_EDITOR_NODE_SECTION_VISUAL
 *                  <Type>     one per component every inspected node has
 *                  Add Component (searchable popup of registered types)
 *                  Scripts    the scripts grid, one row per binding (open,
 *                             remove), New Script / Attach, a drop target
 *                  live       one grid per component of the live object
 *
 * Every node grid holds one LrgEditorNodeSource per inspected node
 * (lrg_editor_node_source_new_for_nodes) and is wired to the editor's
 * undo stack with lrg_editor_node_source_connect_grid: multi-edit with
 * mixed values, and ONE undo step per drag across the whole selection.
 * Sources are replaced on every selection change; grids keep their rows
 * when the schema stays the same, and component sections are pooled by
 * component key, so selecting another node of the same shape rebuilds
 * nothing.
 *
 * Rows get a context menu of the grid's own Reset / Copy / Paste plus,
 * for prefab instances, "Apply to Prefab" / "Revert to Prefab" (actions
 * insp.apply-override / insp.revert-override on the grid, parameter the
 * property name). Live rows get "Apply to Level" (live.apply).
 *
 * The panel also registers the inspector.* IPC methods; they build their
 * own node sources, so they work headless and for any node.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_EDITOR

#include "lrg-editor-inspector-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-hierarchy-panel.h"
#include "../lrg-editor-node-source.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-registry.h"
#include "../../ecs/lrg-component.h"
#include "../../ecs/lrg-game-object.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-expander.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../lrg-log.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

/* Content type of asset drags (the assets panel, the property grid) */
#define ASSET_PAYLOAD "application/x-lrg-asset"

/* Picker popup size and the most candidates it lists */
#define PICKER_WIDTH  (300.0f)
#define PICKER_HEIGHT (340.0f)
#define PICKER_MAX    (300)

/* Narrowest the toolbar's filter field gets */
#define FILTER_WIDTH  (90.0f)

/* Grids by section: the component slot is unused (components are pooled) */
#define N_FIXED (5)

/* ==========================================================================
 * Private: InspIconButton, a small rounded button with an icon
 * ========================================================================== */

#define INSP_TYPE_ICON_BUTTON (insp_icon_button_get_type ())
G_DECLARE_FINAL_TYPE (InspIconButton, insp_icon_button, INSP, ICON_BUTTON, LrgWidget)

/*
 * InspIconButton:
 *
 * A square (or, with text, a pill) button showing an LrgIcon: the lock
 * toggle, script row buttons. It looks like a flat control (surface on
 * hover, pressed shade, accent fill while checked) and calls its
 * callback on a click or ACTIVATE.
 */
struct _InspIconButton
{
	LrgWidget  parent_instance;

	LrgIcon    icon;
	gchar     *text;
	gboolean   toggle;   /* checked state shown with the accent */
	gboolean   checked;
	void     (*clicked) (InspIconButton *button, gpointer data);
	gpointer   data;
};

G_DEFINE_FINAL_TYPE (InspIconButton, insp_icon_button, LRG_TYPE_WIDGET)

static void
insp_icon_button_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
	InspIconButton *self = INSP_ICON_BUTTON (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	gfloat          h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat          tw = 0.0f;

	if (self->text != NULL)
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), self->text,
		                        lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL), 1.0f, &tw, NULL);
	*preferred_width = h + (self->text != NULL ? ceilf (tw) + lrg_theme_get_metric (theme,
	                                                                                 LRG_THEME_METRIC_PADDING_X)
	                                           : 0.0f);
	*preferred_height = h;
}

static void
insp_icon_button_style_defaults (LrgWidget      *widget,
                                 LrgUIPart       part,
                                 LrgWidgetState  state,
                                 LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	GrlColor  clear = { 0, 0, 0, 0 };

	if (part != LRG_UI_PART_BOX)
	{
		LRG_WIDGET_CLASS (insp_icon_button_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	/* Flat at rest, control surfaces on hover and press, accent when on */
	if (state & LRG_WIDGET_STATE_CHECKED)
	{
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_PRESSED) ? LRG_THEME_COLOR_ACCENT_PRESSED
		                              : (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_ACCENT_HOVER
		                              : LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
	}
	else
	{
		if (state & LRG_WIDGET_STATE_PRESSED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		else if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
		else
			lrg_part_look_set_background (look, &clear);
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
		                              ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_TEXT_MUTED));
	}
	lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

static void
insp_icon_button_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
	InspIconButton *self = INSP_ICON_BUTTON (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	LrgPartLook     look;
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          icon = MIN (h, 16.0f + h * 0.15f);
	gfloat          ix = x + (h - icon) * 0.5f;
	gboolean        faded;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	faded = look.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, look.opacity);
	look.opacity = 1.0f;
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);

	/* Icon centred in the left square; the text, if any, after it */
	if (self->text == NULL)
		ix = x + (w - icon) * 0.5f;
	lrg_paint_list_icon (list, self->icon, ix, y + (h - icon) * 0.5f, icon, 0.0f, &look.foreground);
	if (self->text != NULL)
	{
		gfloat size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
		gfloat th = 0.0f;

		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), self->text, size, 1.0f, NULL, &th);
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), self->text, roundf (x + h),
		                     roundf (y + (h - th) * 0.5f), size, 1.0f, &look.foreground);
	}
	if (faded)
		lrg_paint_list_pop_opacity (list);
}

static void
insp_icon_button_click (InspIconButton *self)
{
	if (!lrg_widget_get_enabled (LRG_WIDGET (self)))
		return;
	if (self->clicked != NULL)
		self->clicked (self, self->data);
}

static gboolean
insp_icon_button_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	InspIconButton *self = INSP_ICON_BUTTON (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			insp_icon_button_click (self);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		insp_icon_button_click (self);
		return TRUE;

	default:
		return FALSE;
	}
}

static void
insp_icon_button_finalize (GObject *object)
{
	g_free (INSP_ICON_BUTTON (object)->text);
	G_OBJECT_CLASS (insp_icon_button_parent_class)->finalize (object);
}

static void
insp_icon_button_class_init (InspIconButtonClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = insp_icon_button_finalize;
	widget_class->measure = insp_icon_button_measure;
	widget_class->style_defaults = insp_icon_button_style_defaults;
	widget_class->paint = insp_icon_button_paint;
	widget_class->handle_event = insp_icon_button_handle_event;
}

static void
insp_icon_button_init (InspIconButton *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

static InspIconButton *
insp_icon_button_new (LrgIcon      icon,
                      const gchar *text,
                      const gchar *name,
                      void       (*clicked) (InspIconButton *, gpointer),
                      gpointer     data)
{
	InspIconButton *self = g_object_new (INSP_TYPE_ICON_BUTTON, NULL);

	self->icon = icon;
	self->text = g_strdup (text);
	self->clicked = clicked;
	self->data = data;
	if (name != NULL)
		lrg_widget_set_name (LRG_WIDGET (self), name);
	return self;
}

static void
insp_icon_button_set_checked (InspIconButton *self,
                              gboolean        checked)
{
	self->checked = checked;
	if (checked)
		lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_CHECKED);
}

/* ==========================================================================
 * Private: InspStrip, a tinted bar holding a stack of rows
 * ========================================================================== */

#define INSP_TYPE_STRIP (insp_strip_get_type ())
G_DECLARE_FINAL_TYPE (InspStrip, insp_strip, INSP, STRIP, LrgContainer)

/*
 * InspStrip:
 *
 * The live banner and the prefab bar: a rounded box tinted with a theme
 * colour (WARNING for live values, ACCENT for prefabs), an accent bar
 * and an icon at the left edge, and its children stacked top to bottom
 * at full width to the right of the icon.
 */
struct _InspStrip
{
	LrgContainer    parent_instance;

	LrgThemeColor   tone;
	LrgIcon         icon;
};

G_DEFINE_FINAL_TYPE (InspStrip, insp_strip, LRG_TYPE_CONTAINER)

/* strip_gutter: room for the bar and the icon at the left */
static gfloat
strip_gutter (InspStrip *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));

	return lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) + 18.0f;
}

static void
insp_strip_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
	InspStrip *self = INSP_STRIP (widget);
	LrgTheme  *theme = lrg_widget_get_theme (widget);
	gfloat     pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y) + 2.0f;
	gfloat     spacing = 4.0f;
	gfloat     w = 0.0f;
	gfloat     h = 0.0f;
	guint      i;
	guint      shown = 0;

	for (i = 0; i < lrg_container_get_child_count (LRG_CONTAINER (self)); i++)
	{
		LrgWidget *child = lrg_container_get_child (LRG_CONTAINER (self), i);
		gfloat     cw;
		gfloat     ch;

		if (!lrg_widget_get_visible (child))
			continue;
		lrg_widget_get_preferred_size (child, &cw, &ch);
		w = MAX (w, cw);
		h += ch + (shown > 0 ? spacing : 0.0f);
		shown++;
	}
	*preferred_width = w + strip_gutter (self) + pad;
	*preferred_height = h + pad * 2.0f;
}

static void
insp_strip_layout_children (LrgContainer *container)
{
	InspStrip *self = INSP_STRIP (container);
	LrgTheme  *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat     pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y) + 2.0f;
	gfloat     x = strip_gutter (self);
	gfloat     w = MAX (0.0f, lrg_widget_get_width (LRG_WIDGET (self)) - x - pad);
	gfloat     y = pad;
	guint      i;

	for (i = 0; i < lrg_container_get_child_count (container); i++)
	{
		LrgWidget *child = lrg_container_get_child (container, i);
		gfloat     ch;

		if (!lrg_widget_get_visible (child))
			continue;
		lrg_widget_get_preferred_size (child, NULL, &ch);
		lrg_widget_place_in_slot (child, x, y, w, ch);
		y += ch + 4.0f;
	}
}

static void
insp_strip_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	InspStrip *self = INSP_STRIP (widget);
	LrgTheme  *theme = lrg_widget_get_theme (widget);
	gfloat     x = lrg_widget_get_world_x (widget);
	gfloat     y = lrg_widget_get_world_y (widget);
	gfloat     w = lrg_widget_get_width (widget);
	gfloat     h = lrg_widget_get_height (widget);
	gfloat     r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat     icon = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	const GrlColor *tone = lrg_theme_get_color (theme, self->tone);
	GrlColor   wash;
	GrlColor   edge;
	guint      i;

	/* A wash of the tone over the panel colour, a firmer outline, and a
	 * solid bar at the left edge */
	lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), tone, 0.16f, &wash);
	lrg_paint_color_with_alpha (tone, 0.55f, &edge);
	lrg_paint_list_rect (list, x, y, w, h, r, &wash);
	lrg_paint_list_border (list, x, y, w, h, r, 1.0f, &edge);
	lrg_paint_list_rect (list, x + 1.0f, y + 3.0f, 3.0f, MAX (0.0f, h - 6.0f), 1.5f, tone);
	lrg_paint_list_icon (list, self->icon, x + 9.0f, y + MIN (h * 0.5f - icon * 0.5f, 7.0f), icon, 0.0f, tone);

	for (i = 0; i < lrg_container_get_child_count (LRG_CONTAINER (self)); i++)
	{
		LrgWidget *child = lrg_container_get_child (LRG_CONTAINER (self), i);

		if (lrg_widget_get_visible (child))
			lrg_widget_paint (child, list);
	}
}

static void
insp_strip_class_init (InspStripClass *klass)
{
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	widget_class->measure = insp_strip_measure;
	widget_class->paint = insp_strip_paint;
	container_class->layout_children = insp_strip_layout_children;
}

static void
insp_strip_init (InspStrip *self)
{
	self->tone = LRG_THEME_COLOR_ACCENT;
	self->icon = LRG_ICON_INFO;
}

static InspStrip *
insp_strip_new (const gchar   *name,
                LrgThemeColor  tone,
                LrgIcon        icon)
{
	InspStrip *self = g_object_new (INSP_TYPE_STRIP, NULL);

	self->tone = tone;
	self->icon = icon;
	lrg_widget_set_name (LRG_WIDGET (self), name);
	return self;
}

/* ==========================================================================
 * Private: InspLevelSource, the level's own properties
 * ========================================================================== */

#define INSP_TYPE_LEVEL_SOURCE (insp_level_source_get_type ())
G_DECLARE_FINAL_TYPE (InspLevelSource, insp_level_source, INSP, LEVEL_SOURCE, GObject)

/*
 * InspLevelSource:
 *
 * What the inspector shows with nothing selected (the 0.8.0
 * level_section): the level name and default view (written through
 * lrg_editor_set_object_property(), so they are undoable), and
 * read-only file, node count, project name and project root. It
 * follows LrgEditor::changed and LrgEditor::level-replaced.
 */
struct _InspLevelSource
{
	GObject       parent_instance;

	LrgEditor    *editor;   /* owned */
	LrgEditorApp *app;      /* weak */
	GPtrArray    *pspecs;   /* GParamSpec*, owned refs */
};

static void insp_level_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (InspLevelSource, insp_level_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, insp_level_source_iface_init))

static GPtrArray *
insp_level_source_list (LrgPropertySource *source)
{
	InspLevelSource *self = INSP_LEVEL_SOURCE (source);
	GPtrArray       *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint            i;

	for (i = 0; i < self->pspecs->len; i++)
	{
		GParamSpec *pspec = g_ptr_array_index (self->pspecs, i);

		/* Project rows only with a project */
		if (g_str_has_prefix (pspec->name, "project") &&
		    (self->app == NULL || lrg_editor_app_get_project_dir (self->app) == NULL))
			continue;
		g_ptr_array_add (infos, lrg_property_info_new (pspec));
	}
	return infos;
}

static gboolean
insp_level_source_get (LrgPropertySource *source,
                       const gchar       *name,
                       GValue            *value)
{
	InspLevelSource *self = INSP_LEVEL_SOURCE (source);
	LrgLevel        *level = lrg_editor_get_level (self->editor);

	if (g_strcmp0 (name, "name") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_level_get_name (level));
	}
	else if (g_strcmp0 (name, "default-2d") == 0)
	{
		gboolean d2 = FALSE;

		g_object_get (level, "default-2d", &d2, NULL);
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, d2);
	}
	else if (g_strcmp0 (name, "file") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_editor_get_path (self->editor) != NULL ? lrg_editor_get_path (self->editor)
		                                                                       : "(not saved)");
	}
	else if (g_strcmp0 (name, "nodes") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_take_string (value, g_strdup_printf ("%u", lrg_level_count_nodes (level)));
	}
	else if (g_strcmp0 (name, "project") == 0 && self->app != NULL && self->app->project != NULL)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_project_get_name (self->app->project));
	}
	else if (g_strcmp0 (name, "project-root") == 0 && self->app != NULL)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_editor_app_get_project_dir (self->app));
	}
	else if (g_strcmp0 (name, "project") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
	}
	else
		return FALSE;
	return TRUE;
}

static gboolean
insp_level_source_set (LrgPropertySource *source,
                       const gchar       *name,
                       const GValue      *value)
{
	InspLevelSource *self = INSP_LEVEL_SOURCE (source);
	g_auto(GValue)   v = G_VALUE_INIT;
	GType            type;

	if (g_strcmp0 (name, "name") == 0)
		type = G_TYPE_STRING;
	else if (g_strcmp0 (name, "default-2d") == 0)
		type = G_TYPE_BOOLEAN;
	else
		return FALSE;

	/* Undoable, like every other edit */
	g_value_init (&v, type);
	if (!g_value_transform (value, &v))
		return FALSE;
	return lrg_editor_set_object_property (self->editor, G_OBJECT (lrg_editor_get_level (self->editor)), name,
	                                       &v, FALSE);
}

static LrgPropertyState
insp_level_source_get_state (LrgPropertySource *source,
                             const gchar       *name)
{
	return LRG_PROPERTY_STATE_NONE;
}

static void
insp_level_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = insp_level_source_list;
	iface->get_value = insp_level_source_get;
	iface->set_value = insp_level_source_set;
	iface->get_state = insp_level_source_get_state;
}

/* Every value may have changed: tell the grid row by row (cheap) */
static void
insp_level_source_changed (InspLevelSource *self)
{
	guint i;

	for (i = 0; i < self->pspecs->len; i++)
		lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self),
		                                  ((GParamSpec *) g_ptr_array_index (self->pspecs, i))->name);
}

/* A new document (often a new project): the project rows may come or go */
static void
insp_level_source_replaced (InspLevelSource *self)
{
	lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), NULL);
}

static void
insp_level_source_dispose (GObject *object)
{
	InspLevelSource *self = INSP_LEVEL_SOURCE (object);

	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	g_clear_object (&self->editor);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (insp_level_source_parent_class)->dispose (object);
}

static void
insp_level_source_finalize (GObject *object)
{
	g_ptr_array_unref (INSP_LEVEL_SOURCE (object)->pspecs);
	G_OBJECT_CLASS (insp_level_source_parent_class)->finalize (object);
}

static void
insp_level_source_class_init (InspLevelSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = insp_level_source_dispose;
	G_OBJECT_CLASS (klass)->finalize = insp_level_source_finalize;
}

static void
insp_level_source_init (InspLevelSource *self)
{
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;
	GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	self->pspecs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("name", "Name", "[group:Level] The level's name", NULL, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boolean ("default-2d", "2D view", "[group:Level] Open in the 2D view", FALSE, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("file", "File", "[group:Level] Where the level is saved", NULL, ro)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("nodes", "Nodes", "[group:Level] Nodes in the level", NULL, ro)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("project", "Project", "[group:Project] The project's name", NULL, ro)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("project-root", "Root", "[group:Project] The project folder", NULL, ro)));
}

static InspLevelSource *
insp_level_source_new (LrgEditorApp *app)
{
	InspLevelSource *self = g_object_new (INSP_TYPE_LEVEL_SOURCE, NULL);

	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	g_set_weak_pointer (&self->app, app);
	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (insp_level_source_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (insp_level_source_replaced), self);
	return self;
}

/* ==========================================================================
 * Private: InspPicker, a searchable list popup
 * ========================================================================== */

/* One candidate of a picker: what is shown and what is chosen */
typedef struct
{
	gchar *label;
	gchar *detail;   /* nullable: muted, right-aligned */
	gchar *value;
	gint   score;    /* filled while filtering */
} PickItem;

static void
pick_item_free (gpointer data)
{
	PickItem *item = data;

	g_free (item->label);
	g_free (item->detail);
	g_free (item->value);
	g_free (item);
}

static void
pick_items_add (GPtrArray   *items,
                const gchar *label,
                const gchar *detail,
                const gchar *value)
{
	PickItem *item = g_new0 (PickItem, 1);

	item->label = g_strdup (label);
	item->detail = g_strdup (detail);
	item->value = g_strdup (value != NULL ? value : label);
	g_ptr_array_add (items, item);
}

/* What happens with the chosen value */
typedef void (*PickDone) (LrgEditorInspectorPanel *panel,
                          const gchar             *value,
                          gpointer                 data);

#define INSP_TYPE_PICKER (insp_picker_get_type ())
G_DECLARE_FINAL_TYPE (InspPicker, insp_picker, INSP, PICKER, LrgContainer)

#define INSP_TYPE_PICK_ROW (insp_pick_row_get_type ())
G_DECLARE_FINAL_TYPE (InspPickRow, insp_pick_row, INSP, PICK_ROW, LrgWidget)

/*
 * InspPicker:
 *
 * The Add Component, Attach Script and reference pick popups: a title,
 * a search field (fuzzy-filtered with lrg_editor_fuzzy_score(), best
 * match first) and a scrolling list of InspPickRow. Up / Down move the
 * highlight, Enter chooses it, a click chooses a row. With
 * @custom_format set, typing something no candidate matches exactly
 * adds a row that chooses the typed text itself.
 */
struct _InspPicker
{
	LrgContainer             parent_instance;

	LrgEditorInspectorPanel *panel;       /* weak */
	GPtrArray               *items;       /* PickItem*, all candidates */
	gchar                   *custom_format; /* "Add \"%s\" (custom type)" or NULL */
	PickDone                 done;
	gpointer                 data;
	GDestroyNotify           destroy;
	gboolean                 chosen;

	LrgLabel                *title;
	LrgTextInput            *search;
	LrgScrollBox            *scroll;
	LrgVBox                 *list;
	GPtrArray               *rows;        /* InspPickRow*, borrowed: shown rows */
	gint                     highlight;
};

struct _InspPickRow
{
	LrgWidget   parent_instance;

	InspPicker *picker;      /* not owned: the row lives inside it */
	gchar      *label;
	gchar      *detail;
	gchar      *value;
	gboolean    custom;
	gboolean    highlighted;
};

G_DEFINE_FINAL_TYPE (InspPicker, insp_picker, LRG_TYPE_CONTAINER)
G_DEFINE_FINAL_TYPE (InspPickRow, insp_pick_row, LRG_TYPE_WIDGET)

static void insp_picker_choose (InspPicker *self, const gchar *value);

/* --- InspPickRow --------------------------------------------------------- */

static void
insp_pick_row_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	*preferred_width = PICKER_WIDTH - 24.0f;
	*preferred_height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
}

static void
insp_pick_row_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	InspPickRow *self = INSP_PICK_ROW (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	GrlFont     *font = lrg_theme_get_ui_font (theme);
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat       hover = lrg_widget_get_hover_progress (widget);
	gfloat       th = 0.0f;
	gfloat       tx = x + 8.0f;
	const GrlColor *fg = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);

	/* Highlight (keyboard) in the selection colour, hover as a wash */
	if (self->highlighted)
		lrg_paint_list_rect (list, x, y, w, h, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
	else if (hover > 0.01f)
	{
		GrlColor wash;

		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), hover, &wash);
		lrg_paint_list_rect (list, x, y, w, h, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS),
		                     &wash);
	}

	/* The custom row starts with a plus in the accent colour */
	if (self->custom)
	{
		lrg_paint_list_icon (list, LRG_ICON_PLUS, tx, y + (h - size) * 0.5f, size, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		tx += size + 4.0f;
	}

	lrg_paint_list_push_clip (list, x, y, w, h);
	if (self->detail != NULL)
	{
		gfloat dw = 0.0f;
		gfloat dh = 0.0f;

		lrg_paint_measure_text (font, self->detail, small, 1.0f, &dw, &dh);
		lrg_paint_list_text (list, font, self->detail, roundf (x + w - dw - 8.0f), roundf (y + (h - dh) * 0.5f),
		                     small, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	}
	lrg_paint_measure_text (font, self->label, size, 1.0f, NULL, &th);
	lrg_paint_list_text (list, font, self->label, roundf (tx), roundf (y + (h - th) * 0.5f), size, 1.0f, fg);
	lrg_paint_list_pop_clip (list);
}

static gboolean
insp_pick_row_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	InspPickRow *self = INSP_PICK_ROW (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) == 0 &&
		    lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)) &&
		    self->picker != NULL)
		{
			g_autofree gchar *value = g_strdup (self->value);

			insp_picker_choose (self->picker, value);
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static void
insp_pick_row_finalize (GObject *object)
{
	InspPickRow *self = INSP_PICK_ROW (object);

	g_free (self->label);
	g_free (self->detail);
	g_free (self->value);
	G_OBJECT_CLASS (insp_pick_row_parent_class)->finalize (object);
}

static void
insp_pick_row_class_init (InspPickRowClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = insp_pick_row_finalize;
	widget_class->measure = insp_pick_row_measure;
	widget_class->paint = insp_pick_row_paint;
	widget_class->handle_event = insp_pick_row_handle_event;
}

static void
insp_pick_row_init (InspPickRow *self)
{
}

/* --- InspPicker ---------------------------------------------------------- */

static void
picker_set_highlight (InspPicker *self,
                      gint        index)
{
	guint i;

	if (self->rows->len == 0)
	{
		self->highlight = -1;
		return;
	}
	self->highlight = CLAMP (index, 0, (gint) self->rows->len - 1);
	for (i = 0; i < self->rows->len; i++)
		INSP_PICK_ROW (g_ptr_array_index (self->rows, i))->highlighted = ((gint) i == self->highlight);
	lrg_scroll_box_scroll_to_widget (self->scroll, g_ptr_array_index (self->rows, self->highlight));
}

static gint
compare_items (gconstpointer a,
               gconstpointer b)
{
	const PickItem *ia = *(PickItem * const *) a;
	const PickItem *ib = *(PickItem * const *) b;

	if (ia->score != ib->score)
		return ib->score - ia->score;
	return g_utf8_collate (ia->label, ib->label);
}

/*
 * picker_refill:
 *
 * Rebuilds the visible rows for the search text: every candidate whose
 * label or value fuzzy-matches (best first, then alphabetical), at most
 * PICKER_MAX, plus the custom row when allowed and no row matches the
 * text exactly.
 */
static void
picker_refill (InspPicker *self)
{
	const gchar         *text = lrg_text_input_get_text (self->search);
	g_autoptr(GPtrArray) shown = g_ptr_array_new ();
	gboolean             exact = FALSE;
	guint                i;

	if (text == NULL)
		text = "";
	for (i = 0; i < self->items->len; i++)
	{
		PickItem *item = g_ptr_array_index (self->items, i);

		item->score = 1;
		if (*text != '\0')
		{
			item->score = MAX (lrg_editor_fuzzy_score (text, item->label),
			                   lrg_editor_fuzzy_score (text, item->value));
			if (item->score <= 0)
				continue;
			if (g_ascii_strcasecmp (text, item->label) == 0 || g_strcmp0 (text, item->value) == 0)
				exact = TRUE;
		}
		g_ptr_array_add (shown, item);
	}
	g_ptr_array_sort (shown, compare_items);

	lrg_container_remove_all (LRG_CONTAINER (self->list));
	g_ptr_array_set_size (self->rows, 0);
	for (i = 0; i < MIN (shown->len, PICKER_MAX); i++)
	{
		PickItem    *item = g_ptr_array_index (shown, i);
		InspPickRow *row = g_object_new (INSP_TYPE_PICK_ROW, NULL);
		g_autofree gchar *name = g_strdup_printf ("inspector-picker-item-%u", i);

		row->picker = self;
		row->label = g_strdup (item->label);
		row->detail = g_strdup (item->detail);
		row->value = g_strdup (item->value);
		lrg_widget_set_name (LRG_WIDGET (row), name);
		lrg_container_add_child (LRG_CONTAINER (self->list), LRG_WIDGET (row));
		g_ptr_array_add (self->rows, row);
		g_object_unref (row);
	}
	if (self->custom_format != NULL && *text != '\0' && !exact)
	{
		InspPickRow  *row = g_object_new (INSP_TYPE_PICK_ROW, NULL);
		g_auto(GStrv) parts = g_strsplit (self->custom_format, "%s", 2);

		row->picker = self;
		row->label = g_strjoinv (text, parts);
		row->value = g_strdup (text);
		row->custom = TRUE;
		lrg_widget_set_name (LRG_WIDGET (row), "inspector-picker-custom");
		lrg_container_add_child (LRG_CONTAINER (self->list), LRG_WIDGET (row));
		g_ptr_array_add (self->rows, row);
		g_object_unref (row);
	}
	if (self->rows->len == 0)
	{
		LrgLabel *none = lrg_label_new ("Nothing matches");

		lrg_widget_set_name (LRG_WIDGET (none), "inspector-picker-empty");
		lrg_widget_add_class (LRG_WIDGET (none), "dim-label");
		lrg_container_add_child (LRG_CONTAINER (self->list), LRG_WIDGET (none));
		g_object_unref (none);
	}
	self->highlight = -1;
	picker_set_highlight (self, 0);
}

static void
on_picker_search_changed (LrgTextInput *input,
                          gpointer      user_data)
{
	picker_refill (INSP_PICKER (user_data));
}

static void
on_picker_search_submitted (LrgTextInput *input,
                            gpointer      user_data)
{
	InspPicker *self = INSP_PICKER (user_data);

	if (self->highlight >= 0 && (guint) self->highlight < self->rows->len)
	{
		g_autofree gchar *value = g_strdup (INSP_PICK_ROW (g_ptr_array_index (self->rows, self->highlight))->value);

		insp_picker_choose (self, value);
	}
}

/*
 * insp_picker_choose:
 *
 * Runs the done callback once with @value, then closes the popup. The
 * picker keeps itself alive across the callback (which may rebuild the
 * inspector).
 */
static void
insp_picker_choose (InspPicker  *self,
                    const gchar *value)
{
	g_autoptr(InspPicker) hold = g_object_ref (self);
	LrgWidget            *root = lrg_widget_get_root (LRG_WIDGET (self));

	if (self->chosen)
		return;
	self->chosen = TRUE;
	if (self->done != NULL && self->panel != NULL)
		self->done (self->panel, value, self->data);
	if (LRG_IS_CANVAS (root))
		lrg_canvas_dismiss_popup (LRG_CANVAS (root), LRG_WIDGET (self));
}

static void
insp_picker_measure (LrgWidget *widget,
                     gfloat    *preferred_width,
                     gfloat    *preferred_height)
{
	*preferred_width = PICKER_WIDTH;
	*preferred_height = PICKER_HEIGHT;
}

static void
insp_picker_layout_children (LrgContainer *container)
{
	InspPicker *self = INSP_PICKER (container);
	gfloat      w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat      h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat      pad = 10.0f;
	gfloat      th;
	gfloat      sh;
	gfloat      y = pad;

	/* Children are added one by one in init */
	if (self->title == NULL || self->search == NULL || self->scroll == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->title), NULL, &th);
	lrg_widget_place_in_slot (LRG_WIDGET (self->title), pad, y, w - pad * 2.0f, th);
	y += th + 6.0f;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->search), NULL, &sh);
	lrg_widget_place_in_slot (LRG_WIDGET (self->search), pad, y, w - pad * 2.0f, sh);
	y += sh + 6.0f;
	lrg_widget_place_in_slot (LRG_WIDGET (self->scroll), pad * 0.5f, y, w - pad, MAX (0.0f, h - y - pad * 0.5f));
}

static void
insp_picker_paint (LrgWidget    *widget,
                   LrgPaintList *list)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    x = lrg_widget_get_world_x (widget);
	gfloat    y = lrg_widget_get_world_y (widget);
	gfloat    w = lrg_widget_get_width (widget);
	gfloat    h = lrg_widget_get_height (widget);
	gfloat    r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS);

	/* A floating card: soft shadow, popup fill, hairline border */
	lrg_paint_list_shadow (list, x, y, w, h, r, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR), 0.0f,
	                       0.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y),
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
	lrg_paint_list_rect (list, x, y, w, h, r, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
	lrg_paint_list_border (list, x, y, w, h, r, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	LRG_WIDGET_CLASS (insp_picker_parent_class)->paint (widget, list);
}

static gboolean
insp_picker_handle_event (LrgWidget        *widget,
                          const LrgUIEvent *event)
{
	InspPicker *self = INSP_PICKER (widget);

	/* Arrow keys from the search field move the highlight */
	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_KEY_DOWN)
	{
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_DOWN:
			picker_set_highlight (self, self->highlight + 1);
			return TRUE;
		case GRL_KEY_UP:
			picker_set_highlight (self, self->highlight - 1);
			return TRUE;
		default:
			break;
		}
	}
	return LRG_WIDGET_CLASS (insp_picker_parent_class)->handle_event (widget, event);
}

static void
insp_picker_dispose (GObject *object)
{
	InspPicker *self = INSP_PICKER (object);

	g_clear_weak_pointer (&self->panel);
	if (self->destroy != NULL && self->data != NULL)
		self->destroy (self->data);
	self->destroy = NULL;
	self->data = NULL;
	G_OBJECT_CLASS (insp_picker_parent_class)->dispose (object);
}

static void
insp_picker_finalize (GObject *object)
{
	InspPicker *self = INSP_PICKER (object);

	g_ptr_array_unref (self->items);
	g_ptr_array_unref (self->rows);
	g_free (self->custom_format);
	G_OBJECT_CLASS (insp_picker_parent_class)->finalize (object);
}

static void
insp_picker_class_init (InspPickerClass *klass)
{
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	G_OBJECT_CLASS (klass)->dispose = insp_picker_dispose;
	G_OBJECT_CLASS (klass)->finalize = insp_picker_finalize;
	widget_class->measure = insp_picker_measure;
	widget_class->paint = insp_picker_paint;
	widget_class->handle_event = insp_picker_handle_event;
	container_class->layout_children = insp_picker_layout_children;
}

static void
insp_picker_init (InspPicker *self)
{
	self->rows = g_ptr_array_new ();
	self->highlight = -1;

	self->title = lrg_label_new (NULL);
	lrg_widget_set_name (LRG_WIDGET (self->title), "inspector-picker-title");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->title));
	g_object_unref (self->title);

	self->search = lrg_text_input_new_with_placeholder ("Search");
	lrg_widget_set_name (LRG_WIDGET (self->search), "inspector-picker-search");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->search));
	g_object_unref (self->search);
	g_signal_connect (self->search, "text-changed", G_CALLBACK (on_picker_search_changed), self);
	g_signal_connect (self->search, "submitted", G_CALLBACK (on_picker_search_submitted), self);

	self->list = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->list), 1.0f);
	self->scroll = lrg_scroll_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->scroll), "inspector-picker-list");
	lrg_scroll_box_set_hscroll (self->scroll, FALSE);
	lrg_scroll_box_set_child (self->scroll, LRG_WIDGET (self->list));
	g_object_unref (self->list);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->scroll));
	g_object_unref (self->scroll);
}

/* ==========================================================================
 * LrgEditorInspectorPanel
 * ========================================================================== */

/* A pooled component section, reused by component key */
typedef struct
{
	gchar           *key;       /* "Health", "Health#2" */
	LrgExpander     *expander;  /* owned ref (in comp_box while used) */
	LrgPropertyGrid *grid;      /* borrowed: inside the expander */
} CompSection;

/* A pooled live component section */
typedef struct
{
	gchar           *key;
	LrgExpander     *expander;  /* owned ref */
	LrgPropertyGrid *grid;      /* borrowed */
	LrgComponent    *component; /* owned ref while shown */
} LiveSection;

struct _LrgEditorInspectorPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* owned */
	LrgEditorViewState   *view;         /* owned */
	LrgEditorPlaySession *play;         /* owned */

	/* Chrome */
	LrgHBox              *toolbar;
	LrgLabel             *title;
	LrgTextInput         *filter;
	InspIconButton       *lock_button;
	InspStrip            *banner;
	LrgLabel             *banner_label;
	InspStrip            *prefab_bar;
	LrgButton            *prefab_source;
	LrgLabel             *prefab_info;
	LrgHBox              *prefab_buttons;
	LrgButton            *prefab_select;
	LrgButton            *prefab_apply;
	LrgButton            *prefab_revert;
	LrgButton            *prefab_unlink;
	LrgButton            *prefab_relink;
	LrgScrollBox         *scroll;
	LrgVBox              *body;

	/* Level (nothing selected) */
	LrgVBox              *level_box;
	LrgPropertyGrid      *level_grid;
	InspLevelSource      *level_source;
	LrgButton            *project_settings;

	/* Node sections (index: LrgEditorNodeSection; COMPONENT unused) */
	LrgExpander          *sections[N_FIXED];
	LrgPropertyGrid      *grids[N_FIXED];
	LrgVBox              *comp_box;
	GPtrArray            *comp_pool;    /* CompSection* */
	GStrv                 comp_keys;    /* shown, in order */
	LrgHBox              *add_row;
	LrgButton            *add_component;
	LrgVBox              *scripts_body;
	LrgVBox              *script_rows;
	gchar                *script_sig;   /* rows were built for this */
	LrgButton            *new_script;
	LrgButton            *attach_script;

	/* Live inspection */
	LrgVBox              *live_box;
	GPtrArray            *live_pool;    /* LiveSection* */
	GStrv                 live_keys;
	GObject              *live_explicit; /* weak: chosen in the hierarchy */
	GObject              *live_object;   /* weak: shown now */
	gboolean              hierarchy_hooked;

	/* What is inspected */
	GPtrArray            *nodes;        /* LrgNode*, owned refs */
	gboolean              locked;
	GPtrArray            *lock_nodes;   /* LrgNode*, owned refs */
	gchar                *structure;    /* signature of what the widgets show */
	gboolean              prefab_rows;  /* row menus carry the prefab items */
	gboolean              refreshing;
	gboolean              syncing_lock;
};

G_DEFINE_FINAL_TYPE (LrgEditorInspectorPanel, lrg_editor_inspector_panel, LRG_TYPE_CONTAINER)

static void refresh_all (LrgEditorInspectorPanel *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static const gchar * const section_names[] = { "header", "transform", "visual", "component", "scripts" };
static const gchar * const section_titles[] = { "Node", "Transform", "Visual", "Component", "Scripts" };

/* section_from_name: "transform" -> LRG_EDITOR_NODE_SECTION_TRANSFORM */
static gboolean
section_from_name (const gchar          *name,
                   LrgEditorNodeSection *out)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (section_names); i++)
	{
		if (g_strcmp0 (name, section_names[i]) == 0)
		{
			*out = (LrgEditorNodeSection) i;
			return TRUE;
		}
	}
	return FALSE;
}

static LrgRegistry *
default_registry (void)
{
	LrgEngine *engine = lrg_engine_get_default ();

	return (engine != NULL) ? lrg_engine_get_registry (engine) : NULL;
}

static LrgCanvas *
canvas_of (LrgEditorInspectorPanel *self)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));

	if (LRG_IS_CANVAS (root))
		return LRG_CANVAS (root);
	return self->app != NULL ? lrg_editor_app_get_canvas (self->app) : NULL;
}

static const gchar *
asset_root (LrgEditorInspectorPanel *self)
{
	return self->app != NULL ? lrg_editor_app_get_project_dir (self->app) : NULL;
}

/* status: a status-bar line (also the log) */
static void
status (LrgEditorInspectorPanel *self,
        const gchar             *text)
{
	if (self->app != NULL)
		editor_app_set_status (self->app, "%s", text);
}

static gboolean
collect_attached (LrgNode  *node,
                  gpointer  user_data)
{
	g_hash_table_add (user_data, node);
	return TRUE;
}

/*
 * attached_set:
 *
 * Every node of the current level (but the root), found by walking the
 * tree down from the root. Nodes are never tested by walking up their
 * own parent pointers: after a level is replaced, nodes we still hold
 * have parents that are already gone.
 */
static GHashTable *
attached_set (LrgEditor *editor)
{
	GHashTable *set = g_hash_table_new (g_direct_hash, g_direct_equal);
	LrgNode    *root = lrg_level_get_root (lrg_editor_get_level (editor));

	lrg_node_foreach (root, collect_attached, set);
	g_hash_table_remove (set, root);
	return set;
}

/*
 * node_component_keys:
 *
 * The component keys of @node in order: the type name, with "#2",
 * "#3" ... for further components of the same type (the convention of
 * prefab override paths and lrg_editor_node_source_new()).
 */
static GPtrArray *
node_component_keys (LrgNode *node)
{
	GPtrArray            *keys = g_ptr_array_new_with_free_func (g_free);
	GPtrArray            *comps = lrg_node_get_components (node);
	g_autoptr(GHashTable) counts = g_hash_table_new (g_str_hash, g_str_equal);
	guint                 i;

	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		const gchar *type = lrg_component_desc_get_type_name (g_ptr_array_index (comps, i));
		guint        n;

		if (type == NULL)
			type = "";
		n = GPOINTER_TO_UINT (g_hash_table_lookup (counts, type)) + 1;
		g_hash_table_insert (counts, (gpointer) type, GUINT_TO_POINTER (n));
		g_ptr_array_add (keys, n == 1 ? g_strdup (type) : g_strdup_printf ("%s#%u", type, n));
	}
	return keys;
}

/* component_index: index of the component with key @key on @node, or -1 */
static gint
component_index (LrgNode     *node,
                 const gchar *key)
{
	g_autoptr(GPtrArray) keys = node_component_keys (node);
	guint                i;

	for (i = 0; i < keys->len; i++)
		if (g_strcmp0 (g_ptr_array_index (keys, i), key) == 0)
			return (gint) i;
	return -1;
}

/* common_component_keys: keys of the first node every node has */
static GPtrArray *
common_component_keys (GPtrArray *nodes)
{
	GPtrArray *keys;
	guint      i;
	guint      j;

	if (nodes->len == 0)
		return g_ptr_array_new_with_free_func (g_free);
	keys = node_component_keys (g_ptr_array_index (nodes, 0));
	for (i = 1; i < nodes->len; i++)
	{
		g_autoptr(GPtrArray) other = node_component_keys (g_ptr_array_index (nodes, i));

		for (j = keys->len; j > 0; j--)
		{
			if (!g_ptr_array_find_with_equal_func (other, g_ptr_array_index (keys, j - 1), g_str_equal, NULL))
				g_ptr_array_remove_index (keys, j - 1);
		}
	}
	return keys;
}

/* strv_equal_array: @strv holds exactly the strings of @array */
static gboolean
strv_equal_array (GStrv      strv,
                  GPtrArray *array)
{
	guint i;

	if (strv == NULL)
		return array->len == 0;
	if (g_strv_length (strv) != array->len)
		return FALSE;
	for (i = 0; i < array->len; i++)
		if (g_strcmp0 (strv[i], g_ptr_array_index (array, i)) != 0)
			return FALSE;
	return TRUE;
}

static GStrv
array_to_strv (GPtrArray *array)
{
	GStrv strv = g_new0 (gchar *, array->len + 1);
	guint i;

	for (i = 0; i < array->len; i++)
		strv[i] = g_strdup (g_ptr_array_index (array, i));
	return strv;
}

/* script_language_for: the binding language of a script path */
static LrgScriptLanguage
script_language_for (const gchar *path)
{
	return editor_script_language_for_path (path);
}

/* inspected_prefab: any inspected node belongs to a prefab instance */
static gboolean
inspected_prefab (LrgEditorInspectorPanel *self)
{
	guint i;

	for (i = 0; i < self->nodes->len; i++)
		if (lrg_prefab_find_owner (g_ptr_array_index (self->nodes, i)) != NULL)
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Row context menus
 * ========================================================================== */

/* The grid's own items, repeated in our menus (the grid's model is private) */
static void
append_prop_items (GMenu *menu)
{
	g_autoptr(GMenu) section = g_menu_new ();

	g_menu_append (section, "Reset to default", "prop.reset");
	g_menu_append (section, "Copy value", "prop.copy");
	g_menu_append (section, "Paste value", "prop.paste");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
}

/* row_menu: Reset / Copy / Paste plus one targeted section */
static GMenuModel *
row_menu (const gchar *name,
          gboolean     prefab,
          gboolean     live)
{
	GMenu *menu = g_menu_new ();

	append_prop_items (menu);
	if (prefab || live)
	{
		g_autoptr(GMenu)     section = g_menu_new ();
		g_autoptr(GMenuItem) item = NULL;

		if (prefab)
		{
			item = g_menu_item_new ("Apply to Prefab", NULL);
			g_menu_item_set_action_and_target_value (item, "insp.apply-override", g_variant_new_string (name));
			g_menu_append_item (section, item);
			g_clear_object (&item);
			item = g_menu_item_new ("Revert to Prefab", NULL);
			g_menu_item_set_action_and_target_value (item, "insp.revert-override", g_variant_new_string (name));
			g_menu_append_item (section, item);
		}
		else
		{
			item = g_menu_item_new ("Apply to Level", NULL);
			g_menu_item_set_action_and_target_value (item, "live.apply", g_variant_new_string (name));
			g_menu_append_item (section, item);
		}
		g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	}
	return G_MENU_MODEL (menu);
}

/*
 * update_row_menus:
 *
 * Gives every top-level row of @grid our context menu: the plain one,
 * or one with the prefab (or live) items targeted at the row's
 * property. Rows that already carry the right kind are left alone.
 */
static void
update_row_menus (LrgPropertyGrid *grid,
                  gboolean         prefab,
                  gboolean         live)
{
	LrgPropertySource   *first = lrg_property_grid_get_source (grid, 0);
	g_autoptr(GPtrArray) infos = NULL;
	gint                 kind = live ? 3 : prefab ? 2 : 1;
	guint                i;

	if (first == NULL)
		return;
	infos = lrg_property_source_list_properties (first);
	for (i = 0; i < infos->len; i++)
	{
		const gchar     *name = lrg_property_info_get_name (g_ptr_array_index (infos, i));
		LrgPropertyRow  *row = lrg_property_grid_get_row (grid, name);
		g_autoptr(GMenuModel) model = NULL;

		if (row == NULL || GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "insp-menu-kind")) == kind)
			continue;
		model = row_menu (name, prefab, live);
		lrg_widget_set_context_menu (LRG_WIDGET (row), model);
		g_object_set_data (G_OBJECT (row), "insp-menu-kind", GINT_TO_POINTER (kind));
	}
}

/*
 * on_override_action:
 *
 * "Apply to Prefab" / "Revert to Prefab" of one row: every node source
 * of the grid whose property is overridden, in one undo group.
 */
static void
override_action (LrgPropertyGrid *grid,
                 const gchar     *name,
                 gboolean         apply)
{
	LrgEditorInspectorPanel *self = g_object_get_data (G_OBJECT (grid), "insp-panel");
	g_autoptr(GString)       errors = g_string_new (NULL);
	guint                    done = 0;
	guint                    i;

	if (self == NULL || self->editor == NULL)
		return;

	lrg_editor_begin_group (self->editor, apply ? "Apply to Prefab" : "Revert to Prefab");
	for (i = 0; i < lrg_property_grid_get_n_sources (grid); i++)
	{
		LrgPropertySource *src = lrg_property_grid_get_source (grid, i);
		g_autoptr(GError)  error = NULL;
		gboolean           ok;

		if (!LRG_IS_EDITOR_NODE_SOURCE (src) ||
		    !(lrg_property_source_get_state (src, name) & LRG_PROPERTY_STATE_OVERRIDDEN))
			continue;
		ok = apply ? lrg_editor_node_source_apply_override (LRG_EDITOR_NODE_SOURCE (src), name, &error)
		           : lrg_editor_node_source_revert_override (LRG_EDITOR_NODE_SOURCE (src), name, &error);
		if (ok)
			done++;
		else if (error != NULL)
			g_string_append_printf (errors, "%s%s", errors->len > 0 ? "; " : "", error->message);
	}
	lrg_editor_end_group (self->editor);

	if (errors->len > 0 && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "%s failed: %s",
		                   apply ? "Apply to prefab" : "Revert to prefab", errors->str);
	else if (done == 0)
		status (self, "That property is not overridden");
	else if (self->app != NULL)
		editor_app_set_status (self->app, "%s %s on %u node%s", apply ? "Applied" : "Reverted", name, done,
		                       done == 1 ? "" : "s");
}

static void
on_apply_override (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
	override_action (LRG_PROPERTY_GRID (user_data), g_variant_get_string (parameter, NULL), TRUE);
}

static void
on_revert_override (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
	override_action (LRG_PROPERTY_GRID (user_data), g_variant_get_string (parameter, NULL), FALSE);
}

/* ==========================================================================
 * Pickers: references, components, scripts
 * ========================================================================== */

/*
 * open_picker:
 * @items: (transfer full): the candidates (PickItem*)
 *
 * Shows an InspPicker below @anchor (centred without one), focuses its
 * search field and calls @done with the chosen value.
 */
static gboolean
open_picker (LrgEditorInspectorPanel *self,
             LrgWidget               *anchor,
             const gchar             *title,
             GPtrArray               *items,
             const gchar             *custom_format,
             PickDone                 done,
             gpointer                 data,
             GDestroyNotify           destroy)
{
	LrgCanvas  *canvas = canvas_of (self);
	InspPicker *picker;

	if (canvas == NULL)
	{
		g_ptr_array_unref (items);
		if (destroy != NULL && data != NULL)
			destroy (data);
		return FALSE;
	}

	picker = g_object_new (INSP_TYPE_PICKER, NULL);
	lrg_widget_set_name (LRG_WIDGET (picker), "inspector-picker");
	g_set_weak_pointer (&picker->panel, self);
	picker->items = items;
	picker->custom_format = g_strdup (custom_format);
	picker->done = done;
	picker->data = data;
	picker->destroy = destroy;
	lrg_label_set_text (picker->title, title);
	picker_refill (picker);

	if (anchor != NULL && lrg_widget_get_root (anchor) != LRG_WIDGET (canvas))
		anchor = NULL;
	lrg_canvas_show_popup (canvas, LRG_WIDGET (picker), anchor,
	                       anchor != NULL ? LRG_POPUP_PLACEMENT_BELOW : LRG_POPUP_PLACEMENT_CENTER, FALSE);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (picker->search));
	g_object_unref (picker);
	return TRUE;
}

/* asset_items: assets of the project, of kind @kind (an LrgAssetType nick) or all */
static GPtrArray *
asset_items (LrgEditorInspectorPanel *self,
             const gchar             *kind)
{
	GPtrArray           *items = g_ptr_array_new_with_free_func (pick_item_free);
	g_autoptr(GEnumClass) types = g_type_class_ref (LRG_TYPE_ASSET_TYPE);
	GEnumValue          *want = (kind != NULL) ? g_enum_get_value_by_nick (types, kind) : NULL;
	GPtrArray           *entries;
	guint                i;

	if (self->app == NULL || self->app->assets == NULL)
		return items;
	entries = lrg_asset_database_get_entries (self->app->assets);
	for (i = 0; entries != NULL && i < entries->len; i++)
	{
		LrgAssetEntry *entry = g_ptr_array_index (entries, i);
		GEnumValue    *type = g_enum_get_value (types, (gint) lrg_asset_entry_get_asset_type (entry));

		if (want != NULL && (gint) lrg_asset_entry_get_asset_type (entry) != want->value)
			continue;
		pick_items_add (items, lrg_asset_entry_get_path (entry), type != NULL ? type->value_nick : NULL,
		                lrg_asset_entry_get_path (entry));
	}
	return items;
}

static gboolean
collect_node_item (LrgNode  *node,
                   gpointer  user_data)
{
	GPtrArray *items = user_data;

	if (lrg_node_get_parent (node) != NULL)
	{
		g_autofree gchar *path = lrg_node_get_path (node);

		pick_items_add (items, lrg_node_get_name (node) != NULL ? lrg_node_get_name (node) : "(unnamed)", path,
		                lrg_node_get_guid (node));
	}
	return TRUE;
}

/* node_items: every node of the level: name, path as detail, guid as value */
static GPtrArray *
node_items (LrgEditorInspectorPanel *self)
{
	GPtrArray *items = g_ptr_array_new_with_free_func (pick_item_free);

	lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (self->editor)), collect_node_item, items);
	return items;
}

/* reference_items: the candidates of a reference hint */
static GPtrArray *
reference_items (LrgEditorInspectorPanel *self,
                 const gchar             *hint)
{
	g_autofree gchar *kind = NULL;
	g_autofree gchar *arg = NULL;

	lrg_property_hint_split (hint, &kind, &arg);
	if (g_strcmp0 (kind, "asset") == 0)
		return asset_items (self, arg);
	if (g_strcmp0 (kind, "node-ref") == 0 || g_strcmp0 (kind, "object-ref") == 0)
		return node_items (self);
	return g_ptr_array_new_with_free_func (pick_item_free);
}

/* The grid's drop-down candidates (LrgPropertyReferenceFunc) */
static gchar **
reference_func (LrgPropertyGrid *grid,
                const gchar     *name,
                const gchar     *hint,
                gpointer         user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autoptr(GPtrArray)     items = reference_items (self, hint);
	GStrv                    out = g_new0 (gchar *, items->len + 1);
	guint                    i;

	for (i = 0; i < items->len; i++)
		out[i] = g_strdup (((PickItem *) g_ptr_array_index (items, i))->value);
	return out;
}

/* What a reference pick writes back to */
typedef struct
{
	LrgPropertyGrid *grid;  /* owned */
	gchar           *name;
} RefTarget;

static void
ref_target_free (gpointer data)
{
	RefTarget *t = data;

	g_object_unref (t->grid);
	g_free (t->name);
	g_free (t);
}

static void
on_reference_picked (LrgEditorInspectorPanel *self,
                     const gchar             *value,
                     gpointer                 data)
{
	RefTarget      *t = data;
	LrgPropertyRow *row = lrg_property_grid_get_row (t->grid, t->name);
	g_auto(GValue)  v = G_VALUE_INIT;
	GType           type;

	if (row == NULL)
		return;
	type = lrg_property_info_get_value_type (lrg_property_row_get_info (row));

	/* Object-typed references take the node object, strings the value */
	if (G_TYPE_IS_OBJECT (type))
	{
		LrgNode *node = lrg_editor_find_node (self->editor, value);

		if (node == NULL)
			return;
		g_value_init (&v, G_TYPE_OBJECT);
		g_value_set_object (&v, node);
	}
	else
	{
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, value);
	}
	if (!lrg_property_grid_set_reference_value (t->grid, t->name, &v))
		status (self, "That value does not fit the property");
}

/* LrgPropertyGrid::pick-requested: a filtered asset or node list */
static void
on_pick_requested (LrgPropertyGrid *grid,
                   const gchar     *name,
                   const gchar     *hint,
                   gpointer         user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	LrgPropertyRow          *row = lrg_property_grid_get_row (grid, name);
	RefTarget               *t = g_new0 (RefTarget, 1);
	g_autofree gchar        *kind = NULL;
	g_autofree gchar        *title = NULL;

	lrg_property_hint_split (hint, &kind, NULL);
	t->grid = g_object_ref (grid);
	t->name = g_strdup (name);
	title = g_strdup_printf ("Pick %s for %s", g_strcmp0 (kind, "asset") == 0 ? "an asset" : "a node",
	                         row != NULL ? lrg_property_info_get_label (lrg_property_row_get_info (row)) : name);
	open_picker (self, row != NULL ? LRG_WIDGET (row) : NULL, title, reference_items (self, hint),
	             g_strcmp0 (kind, "asset") == 0 ? "Use \"%s\"" : NULL, on_reference_picked, t, ref_target_free);
}

/* --- Add Component -------------------------------------------------------- */

/* Adds a component of @type to every inspected node, as one undo step */
static void
add_component_to_nodes (LrgEditorInspectorPanel *self,
                        const gchar             *type)
{
	guint i;

	if (type == NULL || *type == '\0' || self->nodes->len == 0)
		return;
	lrg_editor_begin_group (self->editor, "Add Component");
	for (i = 0; i < self->nodes->len; i++)
	{
		g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new (type);

		lrg_editor_add_component (self->editor, g_ptr_array_index (self->nodes, i), desc);
	}
	lrg_editor_end_group (self->editor);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Added %s", type);
}

static void
on_component_picked (LrgEditorInspectorPanel *self,
                     const gchar             *value,
                     gpointer                 data)
{
	add_component_to_nodes (self, value);
}

/* component_items: registered names whose type is an LrgComponent */
static GPtrArray *
component_items (void)
{
	GPtrArray   *items = g_ptr_array_new_with_free_func (pick_item_free);
	LrgRegistry *reg = default_registry ();
	GList       *names = (reg != NULL) ? lrg_registry_get_names (reg) : NULL;
	GList       *l;

	for (l = names; l != NULL; l = l->next)
	{
		GType type = lrg_registry_lookup (reg, l->data);

		if (type != G_TYPE_INVALID && g_type_is_a (type, LRG_TYPE_COMPONENT))
			pick_items_add (items, l->data, g_type_name (type), l->data);
	}
	g_list_free (names);
	return items;
}

static void
on_add_component_clicked (LrgButton *button,
                          gpointer   user_data)
{
	lrg_editor_inspector_panel_show_add_component (LRG_EDITOR_INSPECTOR_PANEL (user_data));
}

/* Removes the component @key from every inspected node, as one step */
static void
on_remove_component_clicked (LrgButton *button,
                             gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autofree gchar        *key = g_strdup (g_object_get_data (G_OBJECT (button), "insp-key"));
	g_autoptr(GPtrArray)     nodes = g_ptr_array_new_with_free_func (g_object_unref);
	guint                    i;

	/* The group's commands rebuild the inspector: work on a copy */
	for (i = 0; i < self->nodes->len; i++)
		g_ptr_array_add (nodes, g_object_ref (g_ptr_array_index (self->nodes, i)));

	lrg_editor_begin_group (self->editor, "Remove Component");
	for (i = 0; i < nodes->len; i++)
	{
		gint index = component_index (g_ptr_array_index (nodes, i), key);

		if (index >= 0)
			lrg_editor_remove_component (self->editor, g_ptr_array_index (nodes, i), (guint) index);
	}
	lrg_editor_end_group (self->editor);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Removed %s", key);
}

/* --- Scripts ------------------------------------------------------------- */

static void
on_script_picked (LrgEditorInspectorPanel *self,
                  const gchar             *value,
                  gpointer                 data)
{
	lrg_editor_inspector_panel_attach_script (self, value);
}

static void
on_attach_script_clicked (LrgButton *button,
                          gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	if (self->nodes->len == 0)
		return;
	open_picker (self, LRG_WIDGET (button), "Attach a script", asset_items (self, "script"), "Attach \"%s\"",
	             on_script_picked, NULL, NULL);
}

static void
on_new_script_clicked (LrgButton *button,
                       gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	if (self->app == NULL || self->nodes->len == 0)
		return;
	editor_dialog_new_script (self->app, lrg_node_get_guid (g_ptr_array_index (self->nodes, 0)));
}

static void
on_script_open_clicked (InspIconButton *button,
                        gpointer        user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	const gchar             *path = g_object_get_data (G_OBJECT (button), "insp-path");

	if (self->app != NULL && path != NULL)
		editor_app_open_file (self->app, path, 1, 1);
}

/* Detaches the script @path (by path) from every inspected node */
static void
on_script_remove_clicked (InspIconButton *button,
                          gpointer        user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autofree gchar        *path = g_strdup (g_object_get_data (G_OBJECT (button), "insp-path"));
	g_autoptr(GPtrArray)     nodes = g_ptr_array_new_with_free_func (g_object_unref);
	guint                    i;
	guint                    j;

	for (i = 0; i < self->nodes->len; i++)
		g_ptr_array_add (nodes, g_object_ref (g_ptr_array_index (self->nodes, i)));

	lrg_editor_begin_group (self->editor, "Detach Script");
	for (i = 0; i < nodes->len; i++)
	{
		LrgNode   *node = g_ptr_array_index (nodes, i);
		GPtrArray *scripts = lrg_node_get_scripts (node);

		for (j = 0; scripts != NULL && j < scripts->len; j++)
		{
			if (g_strcmp0 (lrg_script_binding_get_script (g_ptr_array_index (scripts, j)), path) == 0)
			{
				lrg_editor_remove_script (self->editor, node, j);
				break;
			}
		}
	}
	lrg_editor_end_group (self->editor);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Detached %s", path);
}

/* The Scripts section takes script assets dropped from the asset browser */
static gboolean
on_scripts_accept (LrgDropTarget  *target,
                   LrgDragPayload *payload,
                   gpointer        user_data)
{
	const gchar *path = lrg_drag_payload_get_string (payload);

	return path != NULL && script_language_for (path) != LRG_SCRIPT_LANGUAGE_NONE;
}

static gboolean
on_scripts_drop (LrgDropTarget  *target,
                 LrgDragPayload *payload,
                 gfloat          x,
                 gfloat          y,
                 gpointer        user_data)
{
	return lrg_editor_inspector_panel_attach_script (LRG_EDITOR_INSPECTOR_PANEL (user_data),
	                                                 lrg_drag_payload_get_string (payload));
}

/* ==========================================================================
 * Building blocks
 * ========================================================================== */

/*
 * make_grid:
 *
 * A node grid: DELEGATE edits through the editor's undo stack, the
 * reference drop-downs and pick button answered from the project, the
 * "insp" row actions, the toolbar's filter text.
 */
static LrgPropertyGrid *
make_grid (LrgEditorInspectorPanel *self,
           const gchar             *name)
{
	LrgPropertyGrid              *grid = lrg_property_grid_new ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	const GActionEntry            entries[] = {
		{ "apply-override", on_apply_override, "s", NULL, NULL, { 0 } },
		{ "revert-override", on_revert_override, "s", NULL, NULL, { 0 } },
	};

	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, self->level_grid != NULL
	                                         ? lrg_property_grid_get_label_width (self->level_grid) : 110.0f);
	lrg_editor_node_source_connect_grid (grid, self->editor);
	lrg_property_grid_set_reference_func (grid, reference_func, self, NULL);
	g_signal_connect_object (grid, "pick-requested", G_CALLBACK (on_pick_requested), self, 0);
	g_object_set_data (G_OBJECT (grid), "insp-panel", self);
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), grid);
	lrg_widget_insert_action_group (LRG_WIDGET (grid), "insp", G_ACTION_GROUP (group));
	if (self->filter != NULL)
		lrg_property_grid_set_filter_text (grid, lrg_text_input_get_text (self->filter));
	return grid;
}

/* make_section: an open expander titled @title holding @child */
static LrgExpander *
make_section (const gchar *name,
              const gchar *title,
              LrgWidget   *child)
{
	LrgExpander *expander = lrg_expander_new (title);

	lrg_widget_set_name (LRG_WIDGET (expander), name);
	lrg_expander_set_child (expander, child);
	lrg_expander_set_expanded (expander, TRUE);
	return expander;
}

static LrgButton *
make_button (const gchar *text,
             const gchar *name,
             const gchar *klass,
             GCallback    callback,
             gpointer     data)
{
	LrgButton *button = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	if (klass != NULL)
		lrg_widget_add_class (LRG_WIDGET (button), klass);
	g_signal_connect_object (button, "clicked", callback, data, 0);
	return button;
}

/* add_owned: adds @child to @container and drops the creation reference */
static gpointer
add_owned (gpointer container,
           gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (container), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

/* ==========================================================================
 * Component sections (pooled by key)
 * ========================================================================== */

static void
comp_section_free (gpointer data)
{
	CompSection *sec = data;

	g_free (sec->key);
	g_clear_object (&sec->expander);
	g_free (sec);
}

static CompSection *
comp_section_get (LrgEditorInspectorPanel *self,
                  const gchar             *key)
{
	CompSection      *sec;
	LrgVBox          *box;
	LrgHBox          *actions;
	LrgButton        *remove;
	g_autofree gchar *name = NULL;
	guint             i;

	for (i = 0; i < self->comp_pool->len; i++)
	{
		sec = g_ptr_array_index (self->comp_pool, i);
		if (g_strcmp0 (sec->key, key) == 0)
			return sec;
	}

	/* A new one: expander > vbox > (grid, actions row) */
	sec = g_new0 (CompSection, 1);
	sec->key = g_strdup (key);
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 4.0f);
	name = g_strdup_printf ("inspector-grid-component-%s", key);
	sec->grid = add_owned (box, make_grid (self, name));

	actions = add_owned (box, lrg_hbox_new ());
	lrg_widget_set_halign (LRG_WIDGET (actions), LRG_ALIGN_END);
	g_free (name);
	name = g_strdup_printf ("inspector-remove-%s", key);
	remove = add_owned (actions, make_button ("Remove Component", name, "flat",
	                                          G_CALLBACK (on_remove_component_clicked), self));
	g_object_set_data_full (G_OBJECT (remove), "insp-key", g_strdup (key), g_free);

	g_free (name);
	name = g_strdup_printf ("inspector-component-%s", key);
	sec->expander = g_object_ref_sink (make_section (name, *key != '\0' ? key : "(no type)", LRG_WIDGET (box)));
	g_object_unref (box);
	g_object_unref (sec->expander);   /* make_section's reference; the pool keeps ours */
	g_ptr_array_add (self->comp_pool, sec);
	return sec;
}

/*
 * update_components:
 *
 * One section per component key every inspected node has, in the first
 * node's order. The expanders are re-added only when the key list
 * changed; each grid gets fresh sources (rows stay when the schema
 * does). Pooled sections not shown drop their sources (and node refs).
 */
static void
update_components (LrgEditorInspectorPanel *self,
                   gboolean                 prefab)
{
	g_autoptr(GPtrArray) keys = common_component_keys (self->nodes);
	guint                i;

	if (!strv_equal_array (self->comp_keys, keys))
	{
		lrg_container_remove_all (LRG_CONTAINER (self->comp_box));
		for (i = 0; i < keys->len; i++)
		{
			CompSection *sec = comp_section_get (self, g_ptr_array_index (keys, i));

			lrg_container_add_child (LRG_CONTAINER (self->comp_box), LRG_WIDGET (sec->expander));
		}
		g_strfreev (self->comp_keys);
		self->comp_keys = array_to_strv (keys);
	}

	for (i = 0; i < self->comp_pool->len; i++)
	{
		CompSection          *sec = g_ptr_array_index (self->comp_pool, i);
		g_autoptr(GPtrArray)  sources = NULL;
		guint                 j;

		if (!g_ptr_array_find_with_equal_func (keys, sec->key, g_str_equal, NULL))
		{
			lrg_property_grid_set_sources (sec->grid, NULL);
			continue;
		}
		sources = lrg_editor_node_source_new_for_nodes (self->editor, self->nodes,
		                                                LRG_EDITOR_NODE_SECTION_COMPONENT, sec->key);
		for (j = 0; j < sources->len; j++)
			lrg_editor_node_source_set_asset_root (g_ptr_array_index (sources, j), asset_root (self));
		lrg_property_grid_set_sources (sec->grid, sources);
		update_row_menus (sec->grid, prefab, FALSE);
	}
}

/* ==========================================================================
 * Script rows
 * ========================================================================== */

/* scripts_signature: the first node's script paths, one per line */
static gchar *
scripts_signature (GPtrArray *nodes)
{
	GString   *sig = g_string_new (NULL);
	GPtrArray *scripts;
	guint      i;

	if (nodes->len == 0)
		return g_string_free (sig, FALSE);
	scripts = lrg_node_get_scripts (g_ptr_array_index (nodes, 0));
	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		const gchar *path = lrg_script_binding_get_script (g_ptr_array_index (scripts, i));

		g_string_append_printf (sig, "%s\n", path != NULL ? path : "");
	}
	return g_string_free (sig, FALSE);
}

/*
 * update_script_rows:
 *
 * One row per script binding of the first inspected node: a file icon
 * button that opens it (the external editor, Emacs or the code panel),
 * the path, and a remove button. The Scripts title counts them.
 */
static void
update_script_rows (LrgEditorInspectorPanel *self)
{
	g_autofree gchar *sig = scripts_signature (self->nodes);
	GPtrArray        *scripts;
	g_autofree gchar *title = NULL;
	guint             i;

	scripts = self->nodes->len > 0 ? lrg_node_get_scripts (g_ptr_array_index (self->nodes, 0)) : NULL;
	title = g_strdup_printf ("Scripts (%u)", scripts != NULL ? scripts->len : 0);
	lrg_expander_set_title (self->sections[LRG_EDITOR_NODE_SECTION_SCRIPTS], title);

	if (g_strcmp0 (sig, self->script_sig) == 0)
		return;
	g_free (self->script_sig);
	self->script_sig = g_steal_pointer (&sig);

	lrg_container_remove_all (LRG_CONTAINER (self->script_rows));
	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		const gchar      *path = lrg_script_binding_get_script (g_ptr_array_index (scripts, i));
		LrgHBox          *row = add_owned (self->script_rows, lrg_hbox_new ());
		InspIconButton   *open;
		InspIconButton   *remove;
		LrgLabel         *label;
		g_autofree gchar *name = NULL;

		lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
		name = g_strdup_printf ("inspector-script-open-%u", i);
		open = add_owned (row, insp_icon_button_new (LRG_ICON_FILE, NULL, name, on_script_open_clicked, self));
		g_object_set_data_full (G_OBJECT (open), "insp-path", g_strdup (path), g_free);
		label = add_owned (row, lrg_label_new (path != NULL ? path : "(no file)"));
		lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
		g_free (name);
		name = g_strdup_printf ("inspector-script-remove-%u", i);
		remove = add_owned (row, insp_icon_button_new (LRG_ICON_TRASH, NULL, name, on_script_remove_clicked, self));
		g_object_set_data_full (G_OBJECT (remove), "insp-path", g_strdup (path), g_free);
	}
}

/* ==========================================================================
 * Prefab bar
 * ========================================================================== */

/*
 * update_prefab_bar:
 *
 * For a node of a linked prefab instance (the first inspected node):
 * the source as a link button (opens the prefab), the override count,
 * Select Source (selects the instance root that carries the link),
 * Apply All, Revert All and Unlink. For an unlinked 0.8.0 instance
 * (only the old marker): Relink and the link. Hidden otherwise.
 */
static void
update_prefab_bar (LrgEditorInspectorPanel *self,
                   gboolean                 live)
{
	LrgNode          *node = self->nodes->len > 0 ? g_ptr_array_index (self->nodes, 0) : NULL;
	LrgNode          *owner = node != NULL ? lrg_prefab_find_owner (node) : NULL;
	LrgPrefabLink    *link = owner != NULL ? lrg_node_get_prefab_link (owner) : NULL;
	const gchar      *marker = (node != NULL && link == NULL) ? lrg_prefab_get_marker_source (node) : NULL;
	const gchar      *source = link != NULL ? lrg_prefab_link_get_source_path (link) : marker;
	g_autofree gchar *base = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *info = NULL;
	guint             n;

	if (live || source == NULL)
	{
		lrg_widget_set_visible (LRG_WIDGET (self->prefab_bar), FALSE);
		return;
	}
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_bar), TRUE);
	base = g_path_get_basename (source);
	text = g_strdup_printf ("%s  ↗", base);
	lrg_button_set_text (self->prefab_source, text);
	g_object_set_data_full (G_OBJECT (self->prefab_source), "insp-path", g_strdup (source), g_free);

	if (link != NULL)
	{
		n = lrg_prefab_link_get_n_overrides (link);
		info = n == 0 ? g_strdup ("Linked prefab instance, no overrides")
		              : g_strdup_printf ("Linked prefab instance, %u override%s", n, n == 1 ? "" : "s");
		lrg_widget_set_enabled (LRG_WIDGET (self->prefab_select), node != owner || self->nodes->len > 1);
		lrg_widget_set_enabled (LRG_WIDGET (self->prefab_apply), n > 0);
		lrg_widget_set_enabled (LRG_WIDGET (self->prefab_revert), n > 0);
	}
	else
		info = g_strdup ("Unlinked prefab instance (0.8.0): relink to track changes");
	lrg_label_set_text (self->prefab_info, info);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_select), link != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_apply), link != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_revert), link != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_unlink), link != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_relink), link == NULL);
}

/* prefab_owner: the instance root of the first inspected node */
static LrgNode *
prefab_owner (LrgEditorInspectorPanel *self)
{
	return self->nodes->len > 0 ? lrg_prefab_find_owner (g_ptr_array_index (self->nodes, 0)) : NULL;
}

static void
on_prefab_source_clicked (LrgButton *button,
                          gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	const gchar             *path = g_object_get_data (G_OBJECT (button), "insp-path");
	g_autofree gchar        *abs = NULL;

	if (self->app == NULL || path == NULL)
		return;
	abs = editor_app_project_path (self->app, path);
	editor_app_request (self->app, EDITOR_PENDING_OPEN_LEVEL, abs != NULL ? abs : path);
}

static void
on_prefab_select_clicked (LrgButton *button,
                          gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	LrgNode                 *owner = prefab_owner (self);

	if (owner != NULL)
		lrg_editor_select (self->editor, owner, FALSE);
}

static void
prefab_apply_or_revert (LrgEditorInspectorPanel *self,
                        gboolean                 apply)
{
	g_autoptr(LrgNode) owner = prefab_owner (self) != NULL ? g_object_ref (prefab_owner (self)) : NULL;
	g_autoptr(GError)  error = NULL;
	gboolean           ok;

	if (owner == NULL)
		return;
	ok = apply ? lrg_editor_prefab_apply (self->editor, owner, NULL, &error)
	           : lrg_editor_prefab_revert (self->editor, owner, NULL, &error);
	if (!ok && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "%s failed: %s", apply ? "Apply all" : "Revert all",
		                   error != NULL ? error->message : "unknown error");
	else if (ok)
		status (self, apply ? "Applied all overrides to the prefab" : "Reverted all overrides");
}

static void
on_prefab_apply_clicked (LrgButton *button,
                         gpointer   user_data)
{
	prefab_apply_or_revert (LRG_EDITOR_INSPECTOR_PANEL (user_data), TRUE);
}

static void
on_prefab_revert_clicked (LrgButton *button,
                          gpointer   user_data)
{
	prefab_apply_or_revert (LRG_EDITOR_INSPECTOR_PANEL (user_data), FALSE);
}

static void
on_prefab_unlink_clicked (LrgButton *button,
                          gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autoptr(LrgNode)       owner = prefab_owner (self) != NULL ? g_object_ref (prefab_owner (self)) : NULL;

	if (owner != NULL && lrg_editor_prefab_unlink (self->editor, owner))
		status (self, "Unlinked from the prefab");
}

static void
on_prefab_relink_clicked (LrgButton *button,
                          gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autoptr(GError)        error = NULL;
	LrgNode                 *node = self->nodes->len > 0 ? g_ptr_array_index (self->nodes, 0) : NULL;

	if (node == NULL)
		return;
	if (lrg_editor_prefab_relink (self->editor, node, NULL, &error))
		status (self, "Relinked to the prefab");
	else if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Relink failed: %s",
		                   error != NULL ? error->message : "unknown error");
}

/* ==========================================================================
 * Live inspection
 * ========================================================================== */

static void
live_section_free (gpointer data)
{
	LiveSection *sec = data;

	g_free (sec->key);
	g_clear_object (&sec->component);
	g_clear_object (&sec->expander);
	g_free (sec);
}

/* "Apply to Level" of a live row */
static void
on_live_apply (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	LrgPropertyGrid         *grid = LRG_PROPERTY_GRID (user_data);
	LrgEditorInspectorPanel *self = g_object_get_data (G_OBJECT (grid), "insp-panel");
	LiveSection             *sec = g_object_get_data (G_OBJECT (grid), "insp-live-section");
	const gchar             *name = g_variant_get_string (parameter, NULL);
	g_autoptr(GError)        error = NULL;

	if (self == NULL || sec == NULL || sec->component == NULL || !LRG_IS_GAME_OBJECT (self->live_object))
		return;
	if (lrg_editor_play_session_apply_to_level (self->play, LRG_GAME_OBJECT (self->live_object), sec->component,
	                                            name, &error))
	{
		if (self->app != NULL)
			editor_app_set_status (self->app, "Applied %s to the level", name);
	}
	else if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Apply to level failed: %s",
		                   error != NULL ? error->message : "unknown error");
}

static LiveSection *
live_section_get (LrgEditorInspectorPanel *self,
                  const gchar             *key)
{
	LiveSection                  *sec;
	g_autoptr(GSimpleActionGroup) group = NULL;
	g_autofree gchar             *name = NULL;
	const GActionEntry            entries[] = {
		{ "apply", on_live_apply, "s", NULL, NULL, { 0 } },
	};
	guint                         i;

	for (i = 0; i < self->live_pool->len; i++)
	{
		sec = g_ptr_array_index (self->live_pool, i);
		if (g_strcmp0 (sec->key, key) == 0)
			return sec;
	}

	/* A DIRECT grid: live values are written straight into the play
	 * world (not undoable, discarded on Stop) */
	sec = g_new0 (LiveSection, 1);
	sec->key = g_strdup (key);
	name = g_strdup_printf ("inspector-live-grid-%s", key);
	sec->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (sec->grid), name);
	lrg_property_grid_set_label_width (sec->grid, lrg_property_grid_get_label_width (self->level_grid));
	lrg_property_grid_set_apply_mode (sec->grid, LRG_PROPERTY_APPLY_DIRECT);
	if (self->filter != NULL)
		lrg_property_grid_set_filter_text (sec->grid, lrg_text_input_get_text (self->filter));
	g_object_set_data (G_OBJECT (sec->grid), "insp-panel", self);
	g_object_set_data (G_OBJECT (sec->grid), "insp-live-section", sec);
	group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), sec->grid);
	lrg_widget_insert_action_group (LRG_WIDGET (sec->grid), "live", G_ACTION_GROUP (group));

	g_free (name);
	name = g_strdup_printf ("inspector-live-component-%s", key);
	sec->expander = g_object_ref_sink (make_section (name, key, LRG_WIDGET (sec->grid)));
	g_object_unref (sec->grid);
	g_object_unref (sec->expander);
	g_ptr_array_add (self->live_pool, sec);
	return sec;
}

/* live_component_key: registry name of a live component (type name otherwise) */
static const gchar *
live_type_name (LrgComponent *component)
{
	LrgRegistry *reg = default_registry ();
	const gchar *name = (reg != NULL) ? lrg_registry_lookup_name (reg, G_OBJECT_TYPE (component)) : NULL;

	return name != NULL ? name : G_OBJECT_TYPE_NAME (component);
}

/*
 * update_live:
 *
 * One DIRECT grid per component of @object over an
 * LrgObjectPropertySource; the LrgComponent base properties other than
 * "enabled" are hidden. A grid's source is replaced only when its
 * component object changed.
 */
static void
update_live (LrgEditorInspectorPanel *self,
             GObject                 *object)
{
	g_autoptr(GPtrArray)  keys = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray)  comps = g_ptr_array_new ();
	g_autoptr(GHashTable) counts = g_hash_table_new (g_str_hash, g_str_equal);
	GList                *list = lrg_game_object_get_components (LRG_GAME_OBJECT (object));
	GList                *l;
	guint                 i;

	for (l = list; l != NULL; l = l->next)
	{
		const gchar *type = live_type_name (l->data);
		guint        n = GPOINTER_TO_UINT (g_hash_table_lookup (counts, type)) + 1;

		g_hash_table_insert (counts, (gpointer) type, GUINT_TO_POINTER (n));
		g_ptr_array_add (keys, n == 1 ? g_strdup (type) : g_strdup_printf ("%s#%u", type, n));
		g_ptr_array_add (comps, l->data);
	}
	g_list_free (list);

	if (!strv_equal_array (self->live_keys, keys))
	{
		lrg_container_remove_all (LRG_CONTAINER (self->live_box));
		for (i = 0; i < keys->len; i++)
			lrg_container_add_child (LRG_CONTAINER (self->live_box),
			                         LRG_WIDGET (live_section_get (self, g_ptr_array_index (keys, i))->expander));
		g_strfreev (self->live_keys);
		self->live_keys = array_to_strv (keys);
	}

	for (i = 0; i < self->live_pool->len; i++)
	{
		LiveSection *sec = g_ptr_array_index (self->live_pool, i);
		guint        at;

		if (!g_ptr_array_find_with_equal_func (keys, sec->key, g_str_equal, &at))
		{
			if (sec->component != NULL)
			{
				lrg_property_grid_set_sources (sec->grid, NULL);
				g_clear_object (&sec->component);
			}
			continue;
		}
		if (sec->component != g_ptr_array_index (comps, at))
		{
			g_autoptr(LrgObjectPropertySource) source = NULL;
			GObjectClass                      *base = g_type_class_ref (LRG_TYPE_COMPONENT);
			GParamSpec                       **pspecs;
			guint                              n = 0;
			guint                              k;

			g_set_object (&sec->component, g_ptr_array_index (comps, at));
			source = lrg_object_property_source_new (G_OBJECT (sec->component));
			pspecs = g_object_class_list_properties (base, &n);
			for (k = 0; k < n; k++)
				if (g_strcmp0 (pspecs[k]->name, "enabled") != 0)
					lrg_object_property_source_hide_property (source, pspecs[k]->name);
			g_free (pspecs);
			g_type_class_unref (base);
			lrg_property_grid_set_source (sec->grid, LRG_PROPERTY_SOURCE (source));
			update_row_menus (sec->grid, FALSE, TRUE);
		}
	}
}

/* clear_live: forget the live grids' components (play stopped) */
static void
clear_live (LrgEditorInspectorPanel *self)
{
	guint i;

	for (i = 0; i < self->live_pool->len; i++)
	{
		LiveSection *sec = g_ptr_array_index (self->live_pool, i);

		if (sec->component != NULL)
		{
			lrg_property_grid_set_sources (sec->grid, NULL);
			g_clear_object (&sec->component);
		}
	}
	lrg_container_remove_all (LRG_CONTAINER (self->live_box));
	g_clear_pointer (&self->live_keys, g_strfreev);
	g_clear_weak_pointer (&self->live_object);
}

/*
 * compute_live_object:
 *
 * While play runs: the object picked in the hierarchy's Live tab (as
 * long as the session still knows it), else the live object made from
 * the first inspected node. %NULL when stopped.
 */
static GObject *
compute_live_object (LrgEditorInspectorPanel *self)
{
	if (self->play == NULL || !lrg_editor_play_session_is_playing (self->play))
		return NULL;
	if (LRG_IS_GAME_OBJECT (self->live_explicit) &&
	    lrg_editor_play_session_get_guid_for_object (self->play, LRG_GAME_OBJECT (self->live_explicit)) != NULL)
		return self->live_explicit;
	if (self->nodes->len > 0)
		return (GObject *) lrg_editor_play_session_get_object_for_node (self->play,
		                                                               g_ptr_array_index (self->nodes, 0));
	return NULL;
}

/* ==========================================================================
 * Refresh
 * ========================================================================== */

/*
 * collect_nodes:
 *
 * The nodes to inspect: the locked ones still in the level (unlocking
 * when none is left), else the selection with its primary node first.
 */
static void
collect_nodes (LrgEditorInspectorPanel *self)
{
	g_autoptr(GHashTable) attached = attached_set (self->editor);
	GPtrArray            *sel;
	LrgNode              *primary;
	guint                 i;

	g_ptr_array_set_size (self->nodes, 0);
	if (self->locked)
	{
		for (i = 0; i < self->lock_nodes->len; i++)
		{
			LrgNode *node = g_ptr_array_index (self->lock_nodes, i);

			if (g_hash_table_contains (attached, node))
				g_ptr_array_add (self->nodes, g_object_ref (node));
		}
		if (self->nodes->len > 0)
			return;

		/* Every locked node is gone (deleted, new level): unlock */
		self->locked = FALSE;
		g_ptr_array_set_size (self->lock_nodes, 0);
		insp_icon_button_set_checked (self->lock_button, FALSE);
		self->syncing_lock = TRUE;
		lrg_editor_view_state_set_inspector_lock (self->view, NULL);
		self->syncing_lock = FALSE;
	}
	sel = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	primary = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

	/* The primary node first: its order, scripts and prefab lead */
	if (primary != NULL && g_hash_table_contains (attached, primary))
		g_ptr_array_add (self->nodes, g_object_ref (primary));
	for (i = 0; sel != NULL && i < sel->len; i++)
	{
		LrgNode *node = g_ptr_array_index (sel, i);

		if (node != primary && g_hash_table_contains (attached, node))
			g_ptr_array_add (self->nodes, g_object_ref (node));
	}
}

/*
 * structure_signature:
 *
 * Everything the widget structure depends on (not the values): the
 * inspected nodes, their component keys and script paths, prefab link
 * and override count, the live object and its component count, the
 * lock. Editor changes that leave it alone (value edits, drag
 * previews) need no rebuild: the sources refresh the rows themselves.
 */
static gchar *
structure_signature (LrgEditorInspectorPanel *self,
                     GObject                 *live)
{
	GString *sig = g_string_new (NULL);
	guint    i;

	g_string_append_printf (sig, "L%d P%p|", self->locked, (gpointer) live);
	if (live != NULL)
		g_string_append_printf (sig, "c%u|", lrg_game_object_get_component_count (LRG_GAME_OBJECT (live)));
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgNode              *node = g_ptr_array_index (self->nodes, i);
		g_autoptr(GPtrArray)  keys = node_component_keys (node);
		LrgNode              *owner = lrg_prefab_find_owner (node);
		LrgPrefabLink        *link = owner != NULL ? lrg_node_get_prefab_link (owner) : NULL;
		g_autofree gchar     *scripts = NULL;
		guint                 j;

		g_string_append_printf (sig, "%p[", (gpointer) node);
		for (j = 0; j < keys->len; j++)
			g_string_append_printf (sig, "%s,", (const gchar *) g_ptr_array_index (keys, j));
		if (i == 0)
		{
			g_autoptr(GPtrArray) one = g_ptr_array_new ();

			g_ptr_array_add (one, node);
			scripts = scripts_signature (one);
		}
		else
			scripts = g_strdup_printf ("%u", lrg_node_get_scripts (node) != NULL ? lrg_node_get_scripts (node)->len
			                                                                      : 0);
		g_string_append_printf (sig, "]s%s o%p:%u m%d|", scripts, (gpointer) owner,
		                        link != NULL ? lrg_prefab_link_get_n_overrides (link) : 0,
		                        lrg_prefab_get_marker_source (node) != NULL);
	}
	return g_string_free (sig, FALSE);
}

static void
update_title (LrgEditorInspectorPanel *self,
              GObject                 *live)
{
	g_autofree gchar *text = NULL;

	if (live != NULL)
	{
		const gchar *guid = lrg_editor_play_session_get_guid_for_object (self->play, LRG_GAME_OBJECT (live));
		LrgNode     *node = guid != NULL ? lrg_editor_find_node (self->editor, guid) : NULL;

		text = g_strdup_printf ("%s (live)", node != NULL && lrg_node_get_name (node) != NULL
		                                     ? lrg_node_get_name (node) : G_OBJECT_TYPE_NAME (live));
	}
	else if (self->nodes->len == 0)
		text = g_strdup ("Level");
	else if (self->nodes->len == 1)
	{
		const gchar *name = lrg_node_get_name (g_ptr_array_index (self->nodes, 0));

		text = g_strdup (name != NULL && *name != '\0' ? name : "(unnamed)");
	}
	else
		text = g_strdup_printf ("%u nodes", self->nodes->len);
	lrg_label_set_text (self->title, text);
}

/* set_mode_visibility: which blocks of the body are shown */
static void
set_mode_visibility (LrgEditorInspectorPanel *self,
                     gboolean                 live,
                     gboolean                 have_nodes)
{
	gboolean nodes_mode = !live && have_nodes;
	guint    i;

	lrg_widget_set_visible (LRG_WIDGET (self->banner), live);
	lrg_widget_set_visible (LRG_WIDGET (self->live_box), live);
	lrg_widget_set_visible (LRG_WIDGET (self->level_box), !live && !have_nodes);
	for (i = 0; i < N_FIXED; i++)
		if (self->sections[i] != NULL)
			lrg_widget_set_visible (LRG_WIDGET (self->sections[i]), nodes_mode);
	lrg_widget_set_visible (LRG_WIDGET (self->comp_box), nodes_mode);
	lrg_widget_set_visible (LRG_WIDGET (self->add_row), nodes_mode);
}

/*
 * refresh_all:
 *
 * Works out what to show (live object, nodes or the level) and gives
 * every grid its sources. Cheap to call: rows are reused by the grids,
 * component and live sections by key.
 */
static void
refresh_all (LrgEditorInspectorPanel *self)
{
	GObject *live;
	gboolean prefab;
	guint    i;

	if (self->refreshing || self->editor == NULL)
		return;
	self->refreshing = TRUE;

	collect_nodes (self);
	live = compute_live_object (self);
	prefab = inspected_prefab (self);
	set_mode_visibility (self, live != NULL, self->nodes->len > 0);

	/* 1. Live inspection replaces the authoring sections */
	if (live != NULL)
	{
		g_set_weak_pointer (&self->live_object, live);
		update_live (self, live);
	}
	else if (self->live_object != NULL || self->live_keys != NULL)
		clear_live (self);

	/* 2. The node sections (kept up to date while live, hidden) */
	if (self->nodes->len > 0)
	{
		static const LrgEditorNodeSection fixed[] = {
			LRG_EDITOR_NODE_SECTION_HEADER, LRG_EDITOR_NODE_SECTION_TRANSFORM,
			LRG_EDITOR_NODE_SECTION_VISUAL, LRG_EDITOR_NODE_SECTION_SCRIPTS
		};

		for (i = 0; i < G_N_ELEMENTS (fixed); i++)
		{
			g_autoptr(GPtrArray) sources = lrg_editor_node_source_new_for_nodes (self->editor, self->nodes,
			                                                                     fixed[i], NULL);
			guint                j;

			for (j = 0; j < sources->len; j++)
				lrg_editor_node_source_set_asset_root (g_ptr_array_index (sources, j), asset_root (self));
			lrg_property_grid_set_sources (self->grids[fixed[i]], sources);
			update_row_menus (self->grids[fixed[i]], prefab, FALSE);
		}
		update_components (self, prefab);
	}
	/* Nothing inspected: the hidden grids keep their last sources, so
	 * the rows survive a selection that is cleared and set again */
	if (self->nodes->len > 0)
		update_script_rows (self);
	self->prefab_rows = prefab;

	/* 3. Chrome */
	update_prefab_bar (self, live != NULL);
	update_title (self, live);
	lrg_widget_set_visible (LRG_WIDGET (self->project_settings),
	                        self->app != NULL && lrg_editor_app_get_project_dir (self->app) != NULL);

	g_free (self->structure);
	self->structure = structure_signature (self, live);
	self->refreshing = FALSE;
}

/* ==========================================================================
 * Signal handlers
 * ========================================================================== */

static void
on_selection_changed (LrgEditorSelection *selection,
                      gpointer            user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	if (!self->locked)
		refresh_all (self);
}

/*
 * on_editor_changed:
 *
 * Fired by every edit, including each drag preview. Only a change of
 * the structure (components, scripts, prefab state, deleted nodes)
 * rebuilds; values are refreshed by the sources on their own.
 */
static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autofree gchar         *sig = NULL;
	guint                     i;
	gboolean                  gone = FALSE;

	if (self->refreshing)
		return;

	/* A node we show left the level (deleted, undo of an add) */
	if (self->nodes->len > 0)
	{
		g_autoptr(GHashTable) attached = attached_set (self->editor);

		for (i = 0; i < self->nodes->len && !gone; i++)
			gone = !g_hash_table_contains (attached, g_ptr_array_index (self->nodes, i));
	}
	if (gone)
	{
		refresh_all (self);
		return;
	}

	sig = structure_signature (self, self->live_object);
	if (g_strcmp0 (sig, self->structure) != 0)
	{
		refresh_all (self);
		return;
	}
	update_title (self, self->live_object);

	/* A grid rebuilds its own rows when a source's schema changes (a new
	 * visual kind): give new rows of prefab instances their menus */
	if (self->prefab_rows)
	{
		for (i = 0; i < N_FIXED; i++)
			if (self->grids[i] != NULL)
				update_row_menus (self->grids[i], TRUE, FALSE);
	}
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	guint                    i;

	/* The old level is gone: nodes we hold have dangling parents, so
	 * drop them and every source over them now, before those sources
	 * (connected after us) re-read their nodes */
	g_ptr_array_set_size (self->nodes, 0);
	g_ptr_array_set_size (self->lock_nodes, 0);
	for (i = 0; i < N_FIXED; i++)
		if (self->grids[i] != NULL)
			lrg_property_grid_set_sources (self->grids[i], NULL);
	for (i = 0; i < self->comp_pool->len; i++)
		lrg_property_grid_set_sources (((CompSection *) g_ptr_array_index (self->comp_pool, i))->grid, NULL);

	/* Locks do not survive a new document */
	if (self->locked)
	{
		self->locked = FALSE;
		insp_icon_button_set_checked (self->lock_button, FALSE);
		self->syncing_lock = TRUE;
		lrg_editor_view_state_set_inspector_lock (self->view, NULL);
		self->syncing_lock = FALSE;
	}
	refresh_all (self);
}

/* The view state's inspector-lock changed (IPC, settings) */
static void
on_view_lock_changed (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	const gchar             *guid = lrg_editor_view_state_get_inspector_lock (self->view);
	LrgNode                 *node;

	if (self->syncing_lock)
		return;
	if (guid == NULL)
	{
		if (self->locked)
			lrg_editor_inspector_panel_set_locked (self, FALSE);
		return;
	}
	node = lrg_editor_find_node (self->editor, guid);
	if (node == NULL)
		return;
	if (self->locked && self->lock_nodes->len > 0 && g_ptr_array_index (self->lock_nodes, 0) == node)
		return;
	g_ptr_array_set_size (self->lock_nodes, 0);
	g_ptr_array_add (self->lock_nodes, g_object_ref (node));
	self->locked = TRUE;
	insp_icon_button_set_checked (self->lock_button, TRUE);
	refresh_all (self);
}

/* The hierarchy's Live tab picked an object */
static void
on_live_selected (GObject  *hierarchy,
                  GObject  *object,
                  gpointer  user_data)
{
	lrg_editor_inspector_panel_set_live_object (LRG_EDITOR_INSPECTOR_PANEL (user_data), object);
}

/*
 * hook_hierarchy:
 *
 * Connects to the hierarchy panel's #LrgEditorHierarchyPanel::live-selected
 * (its Live tab) and picks up an object already selected there. Retried
 * when play starts, in case the hierarchy was created after us.
 */
static void
hook_hierarchy (LrgEditorInspectorPanel *self)
{
	LrgEditorHierarchyPanel *hierarchy;
	GObject                 *picked;

	if (self->hierarchy_hooked || self->app == NULL)
		return;
	hierarchy = lrg_editor_hierarchy_panel_find (self->app);
	if (hierarchy == NULL)
		return;
	g_signal_connect_object (hierarchy, "live-selected", G_CALLBACK (on_live_selected), self, 0);
	self->hierarchy_hooked = TRUE;
	picked = lrg_editor_hierarchy_panel_get_live_selection (hierarchy);
	if (LRG_IS_GAME_OBJECT (picked))
		g_set_weak_pointer (&self->live_explicit, picked);
}

static void
on_play_started (LrgEditorPlaySession *session,
                 gpointer              user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	hook_hierarchy (self);
	refresh_all (self);
}

static void
on_play_stopped (LrgEditorPlaySession *session,
                 gpointer              user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	g_clear_weak_pointer (&self->live_explicit);
	refresh_all (self);
}

/* Every simulated frame: live values move, so the live grids re-read */
static void
on_play_ticked (LrgEditorPlaySession *session,
                gfloat                delta,
                gpointer              user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	GObject                 *live = compute_live_object (self);
	g_autofree gchar        *sig = NULL;
	guint                    i;

	sig = structure_signature (self, live);
	if (live != self->live_object || g_strcmp0 (sig, self->structure) != 0)
	{
		refresh_all (self);
		return;
	}
	for (i = 0; live != NULL && i < self->live_pool->len; i++)
	{
		LiveSection *sec = g_ptr_array_index (self->live_pool, i);

		if (sec->component != NULL)
			lrg_property_grid_refresh (sec->grid);
	}
}

/* The toolbar filter applies to every grid */
static void
on_filter_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	const gchar             *text = lrg_text_input_get_text (input);
	guint                    i;

	for (i = 0; i < N_FIXED; i++)
		if (self->grids[i] != NULL)
			lrg_property_grid_set_filter_text (self->grids[i], text);
	for (i = 0; i < self->comp_pool->len; i++)
		lrg_property_grid_set_filter_text (((CompSection *) g_ptr_array_index (self->comp_pool, i))->grid, text);
	for (i = 0; i < self->live_pool->len; i++)
		lrg_property_grid_set_filter_text (((LiveSection *) g_ptr_array_index (self->live_pool, i))->grid, text);
	lrg_property_grid_set_filter_text (self->level_grid, text);
}

static void
on_lock_clicked (InspIconButton *button,
                 gpointer        user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	lrg_editor_inspector_panel_set_locked (self, !self->locked);
}

static void
on_project_settings_clicked (LrgButton *button,
                             gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	if (self->app != NULL)
		lrg_editor_app_activate_action (self->app, "file.project-settings");
}

/* ==========================================================================
 * Widget
 * ========================================================================== */

/* set_label_width: every grid's label column (only when it changed) */
static void
set_label_width (LrgEditorInspectorPanel *self,
                 gfloat                   width)
{
	guint i;

	if (self->level_grid == NULL || lrg_property_grid_get_label_width (self->level_grid) == width)
		return;
	lrg_property_grid_set_label_width (self->level_grid, width);
	for (i = 0; i < N_FIXED; i++)
		if (self->grids[i] != NULL)
			lrg_property_grid_set_label_width (self->grids[i], width);
	for (i = 0; i < self->comp_pool->len; i++)
		lrg_property_grid_set_label_width (((CompSection *) g_ptr_array_index (self->comp_pool, i))->grid, width);
	for (i = 0; i < self->live_pool->len; i++)
		lrg_property_grid_set_label_width (((LiveSection *) g_ptr_array_index (self->live_pool, i))->grid, width);
}

/*
 * Toolbar, banner and prefab bar at their preferred heights across the
 * top; the scroll box takes the rest.
 */
static void
lrg_editor_inspector_panel_layout_children (LrgContainer *container)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (container);
	gfloat                   w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                   h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                   pad = 6.0f;
	gfloat                   y = pad;
	LrgWidget               *top[3];
	guint                    i;

	/* Not built yet (children are added one by one during construction) */
	if (self->scroll == NULL || self->prefab_bar == NULL)
		return;

	/* The title takes what it needs up to 40 % of the toolbar, the
	 * filter the rest; the label column follows the panel width */
	lrg_widget_set_max_size (LRG_WIDGET (self->title), MAX (40.0f, w * 0.4f), 0.0f);
	set_label_width (self, CLAMP (roundf (w * 0.3f), 72.0f, 140.0f));
	top[0] = LRG_WIDGET (self->toolbar);
	top[1] = LRG_WIDGET (self->banner);
	top[2] = LRG_WIDGET (self->prefab_bar);
	for (i = 0; i < G_N_ELEMENTS (top); i++)
	{
		gfloat ch;

		if (!lrg_widget_get_visible (top[i]))
			continue;
		lrg_widget_get_preferred_size (top[i], NULL, &ch);
		lrg_widget_place_in_slot (top[i], pad, y, MAX (0.0f, w - pad * 2.0f), ch);
		y += ch + 6.0f;
	}
	lrg_widget_place_in_slot (LRG_WIDGET (self->scroll), 0.0f, y, w, MAX (0.0f, h - y));
}

static void
lrg_editor_inspector_panel_dispose (GObject *object)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (object);
	guint                    i;

	/* Grids hold node sources; drop them before the pools go */
	for (i = 0; i < N_FIXED; i++)
		if (self->grids[i] != NULL)
			lrg_editor_node_source_disconnect_grid (self->grids[i]);
	if (self->comp_pool != NULL)
	{
		for (i = 0; i < self->comp_pool->len; i++)
			lrg_editor_node_source_disconnect_grid (((CompSection *) g_ptr_array_index (self->comp_pool, i))->grid);
		g_ptr_array_set_size (self->comp_pool, 0);
	}
	if (self->live_pool != NULL)
		g_ptr_array_set_size (self->live_pool, 0);

	if (self->editor != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->editor, self);
		g_signal_handlers_disconnect_by_data (lrg_editor_get_selection (self->editor), self);
	}
	if (self->view != NULL)
		g_signal_handlers_disconnect_by_data (self->view, self);
	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	g_clear_object (&self->level_source);
	g_clear_object (&self->editor);
	g_clear_object (&self->view);
	g_clear_object (&self->play);
	g_clear_weak_pointer (&self->app);
	g_clear_weak_pointer (&self->live_explicit);
	g_clear_weak_pointer (&self->live_object);
	if (self->nodes != NULL)
		g_ptr_array_set_size (self->nodes, 0);
	if (self->lock_nodes != NULL)
		g_ptr_array_set_size (self->lock_nodes, 0);

	G_OBJECT_CLASS (lrg_editor_inspector_panel_parent_class)->dispose (object);
}

static void
lrg_editor_inspector_panel_finalize (GObject *object)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (object);

	g_ptr_array_unref (self->comp_pool);
	g_ptr_array_unref (self->live_pool);
	g_ptr_array_unref (self->nodes);
	g_ptr_array_unref (self->lock_nodes);
	g_strfreev (self->comp_keys);
	g_strfreev (self->live_keys);
	g_free (self->script_sig);
	g_free (self->structure);
	G_OBJECT_CLASS (lrg_editor_inspector_panel_parent_class)->finalize (object);
}

static void
lrg_editor_inspector_panel_class_init (LrgEditorInspectorPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_inspector_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_inspector_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_inspector_panel_layout_children;
}

static void
lrg_editor_inspector_panel_init (LrgEditorInspectorPanel *self)
{
	self->nodes = g_ptr_array_new_with_free_func (g_object_unref);
	self->lock_nodes = g_ptr_array_new_with_free_func (g_object_unref);
	self->comp_pool = g_ptr_array_new_with_free_func (comp_section_free);
	self->live_pool = g_ptr_array_new_with_free_func (live_section_free);
}

/*
 * build_chrome:
 *
 * Toolbar (title, filter, lock), live banner and prefab bar.
 */
static void
build_chrome (LrgEditorInspectorPanel *self)
{
	LrgHBox *row2;

	/* Toolbar */
	self->toolbar = add_owned (self, lrg_hbox_new ());
	lrg_widget_set_name (LRG_WIDGET (self->toolbar), "inspector-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	self->title = add_owned (self->toolbar, lrg_label_new ("Level"));
	lrg_widget_set_name (LRG_WIDGET (self->title), "inspector-title");
	lrg_widget_set_valign (LRG_WIDGET (self->title), LRG_ALIGN_CENTER);
	self->filter = add_owned (self->toolbar, lrg_text_input_new_with_placeholder ("Filter properties"));
	lrg_widget_set_name (LRG_WIDGET (self->filter), "inspector-filter");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->filter), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (self->filter), FILTER_WIDTH, 0.0f);
	g_signal_connect (self->filter, "text-changed", G_CALLBACK (on_filter_changed), self);
	self->lock_button = add_owned (self->toolbar, insp_icon_button_new (LRG_ICON_LOCK, NULL, "inspector-lock",
	                                                                    on_lock_clicked, self));

	/* Live banner */
	self->banner = add_owned (self, insp_strip_new ("inspector-live-banner", LRG_THEME_COLOR_WARNING,
	                                                LRG_ICON_PLAY));
	self->banner_label = add_owned (self->banner, lrg_label_new ("Live values"));
	lrg_widget_set_name (LRG_WIDGET (self->banner_label), "inspector-live-title");
	{
		LrgLabel *note = add_owned (self->banner, lrg_label_new ("Changes are discarded on Stop"));

		lrg_widget_add_class (LRG_WIDGET (note), "dim-label");
	}
	lrg_widget_set_visible (LRG_WIDGET (self->banner), FALSE);

	/* Prefab bar: link + info line, then the buttons */
	self->prefab_bar = add_owned (self, insp_strip_new ("inspector-prefab-bar", LRG_THEME_COLOR_ACCENT,
	                                                    LRG_ICON_STAR));
	self->prefab_source = add_owned (self->prefab_bar,
	                                 make_button ("prefab", "inspector-prefab-source", "flat",
	                                              G_CALLBACK (on_prefab_source_clicked), self));
	lrg_widget_set_halign (LRG_WIDGET (self->prefab_source), LRG_ALIGN_START);
	self->prefab_info = add_owned (self->prefab_bar, lrg_label_new (NULL));
	lrg_widget_set_name (LRG_WIDGET (self->prefab_info), "inspector-prefab-info");
	lrg_widget_add_class (LRG_WIDGET (self->prefab_info), "dim-label");
	/* Two rows of equally wide buttons, so the bar fits a narrow dock */
	self->prefab_buttons = add_owned (self->prefab_bar, lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->prefab_buttons), 4.0f);
	row2 = add_owned (self->prefab_bar, lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row2), 4.0f);
	self->prefab_select = add_owned (self->prefab_buttons, make_button ("Select Source", "inspector-prefab-select",
	                                                                    NULL, G_CALLBACK (on_prefab_select_clicked),
	                                                                    self));
	self->prefab_unlink = add_owned (self->prefab_buttons, make_button ("Unlink", "inspector-prefab-unlink",
	                                                                    "flat", G_CALLBACK (on_prefab_unlink_clicked),
	                                                                    self));
	self->prefab_relink = add_owned (self->prefab_buttons, make_button ("Relink", "inspector-prefab-relink",
	                                                                    "primary",
	                                                                    G_CALLBACK (on_prefab_relink_clicked), self));
	self->prefab_apply = add_owned (row2, make_button ("Apply All", "inspector-prefab-apply", "primary",
	                                                   G_CALLBACK (on_prefab_apply_clicked), self));
	self->prefab_revert = add_owned (row2, make_button ("Revert All", "inspector-prefab-revert", NULL,
	                                                    G_CALLBACK (on_prefab_revert_clicked), self));
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_select), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_unlink), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_relink), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_apply), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_revert), 1.0f);
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_bar), FALSE);
}

/*
 * build_body:
 *
 * The scroll box and its sections, in display order: level, Node,
 * Transform, Visual, components, Add Component, Scripts, live.
 */
static void
build_body (LrgEditorInspectorPanel *self)
{
	LrgVBox                  *box;
	LrgHBox                  *buttons;
	LrgLabel                 *hint;
	g_autoptr(LrgDropTarget)  drop = NULL;
	const gchar              *types[] = { ASSET_PAYLOAD, NULL };
	guint                     i;

	self->scroll = add_owned (self, lrg_scroll_box_new ());
	lrg_widget_set_name (LRG_WIDGET (self->scroll), "inspector-scroll");
	lrg_scroll_box_set_hscroll (self->scroll, FALSE);
	self->body = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->body), "inspector-body");
	lrg_container_set_spacing (LRG_CONTAINER (self->body), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (self->body), 6.0f);
	lrg_scroll_box_set_child (self->scroll, LRG_WIDGET (self->body));
	g_object_unref (self->body);

	/* Level */
	self->level_box = add_owned (self->body, lrg_vbox_new ());
	lrg_widget_set_name (LRG_WIDGET (self->level_box), "inspector-level");
	lrg_container_set_spacing (LRG_CONTAINER (self->level_box), 8.0f);
	self->level_grid = add_owned (self->level_box, lrg_property_grid_new ());
	lrg_widget_set_name (LRG_WIDGET (self->level_grid), "inspector-grid-level");
	lrg_property_grid_set_label_width (self->level_grid, 110.0f);
	self->level_source = insp_level_source_new (self->app);
	lrg_property_grid_set_source (self->level_grid, LRG_PROPERTY_SOURCE (self->level_source));
	self->project_settings = add_owned (self->level_box, make_button ("Project Settings…",
	                                                                  "inspector-project-settings", NULL,
	                                                                  G_CALLBACK (on_project_settings_clicked),
	                                                                  self));
	lrg_widget_set_halign (LRG_WIDGET (self->project_settings), LRG_ALIGN_START);
	hint = add_owned (self->level_box, lrg_label_new ("Select a node to inspect it."));
	lrg_widget_add_class (LRG_WIDGET (hint), "dim-label");

	/* Node, Transform, Visual */
	for (i = 0; i < LRG_EDITOR_NODE_SECTION_COMPONENT; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("inspector-grid-%s", section_names[i]);
		g_autofree gchar *sname = g_strdup_printf ("inspector-section-%s", section_names[i]);

		self->grids[i] = make_grid (self, name);
		self->sections[i] = add_owned (self->body, make_section (sname, section_titles[i],
		                                                         LRG_WIDGET (self->grids[i])));
		g_object_unref (self->grids[i]);
	}

	/* Components, then the Add Component button */
	self->comp_box = add_owned (self->body, lrg_vbox_new ());
	lrg_widget_set_name (LRG_WIDGET (self->comp_box), "inspector-components");
	lrg_container_set_spacing (LRG_CONTAINER (self->comp_box), 6.0f);
	self->add_row = add_owned (self->body, lrg_hbox_new ());
	self->add_component = add_owned (self->add_row, make_button ("Add Component…", "inspector-add-component",
	                                                             "primary",
	                                                             G_CALLBACK (on_add_component_clicked), self));
	lrg_widget_set_fill_weight (LRG_WIDGET (self->add_component), 1.0f);

	/* Scripts: grid, rows, buttons; the whole section takes script drops */
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 4.0f);
	self->scripts_body = box;
	self->grids[LRG_EDITOR_NODE_SECTION_SCRIPTS] = add_owned (box, make_grid (self, "inspector-grid-scripts"));
	self->script_rows = add_owned (box, lrg_vbox_new ());
	lrg_widget_set_name (LRG_WIDGET (self->script_rows), "inspector-script-rows");
	lrg_container_set_spacing (LRG_CONTAINER (self->script_rows), 2.0f);
	buttons = add_owned (box, lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (buttons), 4.0f);
	self->new_script = add_owned (buttons, make_button ("New Script…", "inspector-new-script", NULL,
	                                                    G_CALLBACK (on_new_script_clicked), self));
	self->attach_script = add_owned (buttons, make_button ("Attach…", "inspector-attach-script", NULL,
	                                                       G_CALLBACK (on_attach_script_clicked), self));
	self->sections[LRG_EDITOR_NODE_SECTION_SCRIPTS] = add_owned (self->body,
	                                                             make_section ("inspector-section-scripts",
	                                                                           "Scripts", LRG_WIDGET (box)));
	g_object_unref (box);
	drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	g_signal_connect_object (drop, "accept", G_CALLBACK (on_scripts_accept), self, 0);
	g_signal_connect_object (drop, "drop", G_CALLBACK (on_scripts_drop), self, 0);
	lrg_widget_add_drop_target (LRG_WIDGET (self->sections[LRG_EDITOR_NODE_SECTION_SCRIPTS]),
	                            g_steal_pointer (&drop));

	/* Live components */
	self->live_box = add_owned (self->body, lrg_vbox_new ());
	lrg_widget_set_name (LRG_WIDGET (self->live_box), "inspector-live");
	lrg_container_set_spacing (LRG_CONTAINER (self->live_box), 6.0f);
}

/* ==========================================================================
 * IPC: inspector.*
 * ========================================================================== */

/* inspector_of: the app's inspector panel, if it has one */
static LrgEditorInspectorPanel *
inspector_of (LrgEditorApp *app)
{
	LrgDockPanel *dock;
	LrgWidget    *child;

	if (app == NULL || app->panels == NULL)
		return NULL;
	dock = g_hash_table_lookup (app->panels, "inspector");
	child = dock != NULL ? lrg_dock_panel_get_child (dock) : NULL;
	return LRG_IS_EDITOR_INSPECTOR_PANEL (child) ? LRG_EDITOR_INSPECTOR_PANEL (child) : NULL;
}

/*
 * value_to_json:
 *
 * Booleans, numbers and strings as JSON values; vectors as arrays of
 * numbers; everything else (enums and flags as nicks, colours as
 * "#rrggbbaa", ...) as the text lrg_property_value_to_string() writes.
 */
static JsonNode *
value_to_json (const GValue *value)
{
	JsonNode *node = json_node_new (JSON_NODE_VALUE);
	GType     type = G_VALUE_TYPE (value);
	GType     fundamental = G_TYPE_FUNDAMENTAL (type);

	switch (fundamental)
	{
	case G_TYPE_BOOLEAN:
		json_node_set_boolean (node, g_value_get_boolean (value));
		return node;

	case G_TYPE_CHAR:
	case G_TYPE_UCHAR:
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_LONG:
	case G_TYPE_ULONG:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
		{
			g_auto(GValue) n = G_VALUE_INIT;

			g_value_init (&n, G_TYPE_INT64);
			g_value_transform (value, &n);
			json_node_set_int (node, g_value_get_int64 (&n));
			return node;
		}

	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		{
			g_auto(GValue) d = G_VALUE_INIT;

			g_value_init (&d, G_TYPE_DOUBLE);
			g_value_transform (value, &d);
			json_node_set_double (node, g_value_get_double (&d));
			return node;
		}

	case G_TYPE_STRING:
		if (g_value_get_string (value) == NULL)
		{
			json_node_unref (node);
			return json_node_new (JSON_NODE_NULL);
		}
		json_node_set_string (node, g_value_get_string (value));
		return node;

	default:
		break;
	}

	/* Vectors: "x, y, z" -> [x, y, z] */
	if (g_str_has_prefix (g_type_name (type), "GrlVector"))
	{
		g_autofree gchar *text = lrg_property_value_to_string (value);
		g_auto(GStrv)     parts = g_strsplit (text, ",", -1);
		JsonArray        *array = json_array_new ();
		guint             i;

		for (i = 0; parts[i] != NULL; i++)
			json_array_add_double_element (array, g_ascii_strtod (g_strstrip (parts[i]), NULL));
		json_node_unref (node);
		node = json_node_new (JSON_NODE_ARRAY);
		json_node_take_array (node, array);
		return node;
	}

	{
		g_autofree gchar *text = lrg_property_value_to_string (value);

		json_node_set_string (node, text);
	}
	return node;
}

/*
 * json_to_value:
 *
 * The reverse for a property of @type: numbers and booleans by GValue
 * transforms (onto enum and flags values too), strings parsed with
 * lrg_property_value_from_string() (nicks, "#rrggbb", "x, y, z"),
 * arrays of numbers as vectors, null as a %NULL string.
 */
static gboolean
json_to_value (JsonNode  *json,
               GType      type,
               GValue    *out,
               GError   **error)
{
	g_value_init (out, type);

	if (JSON_NODE_HOLDS_NULL (json))
	{
		if (G_TYPE_FUNDAMENTAL (type) == G_TYPE_STRING)
			return TRUE;
	}
	else if (JSON_NODE_HOLDS_ARRAY (json))
	{
		JsonArray        *array = json_node_get_array (json);
		g_autoptr(GString) text = g_string_new (NULL);
		guint             i;

		for (i = 0; i < json_array_get_length (array); i++)
		{
			gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

			g_string_append_printf (text, "%s%s", i > 0 ? ", " : "",
			                        g_ascii_dtostr (buf, sizeof buf, json_array_get_double_element (array, i)));
		}
		if (lrg_property_value_from_string (out, text->str))
			return TRUE;
	}
	else if (JSON_NODE_HOLDS_VALUE (json))
	{
		g_auto(GValue) raw = G_VALUE_INIT;

		json_node_get_value (json, &raw);
		if (G_VALUE_HOLDS_STRING (&raw))
		{
			if (G_TYPE_FUNDAMENTAL (type) == G_TYPE_STRING)
			{
				g_value_set_string (out, g_value_get_string (&raw));
				return TRUE;
			}
			if (lrg_property_value_from_string (out, g_value_get_string (&raw)))
				return TRUE;
		}
		else if (G_TYPE_IS_ENUM (type) && G_VALUE_HOLDS_INT64 (&raw))
		{
			g_value_set_enum (out, (gint) g_value_get_int64 (&raw));
			return TRUE;
		}
		else if (G_TYPE_IS_FLAGS (type) && G_VALUE_HOLDS_INT64 (&raw))
		{
			g_value_set_flags (out, (guint) g_value_get_int64 (&raw));
			return TRUE;
		}
		else if (g_value_transform (&raw, out))
			return TRUE;
	}

	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
	             "The value does not fit a property of type %s", g_type_name (type));
	g_value_unset (out);
	return FALSE;
}

/* state_to_json: ["default", "overridden", ...] */
static JsonNode *
state_to_json (LrgPropertyState state)
{
	JsonArray *array = json_array_new ();
	JsonNode  *node = json_node_new (JSON_NODE_ARRAY);

	if (state & LRG_PROPERTY_STATE_DEFAULT)
		json_array_add_string_element (array, "default");
	if (state & LRG_PROPERTY_STATE_OVERRIDDEN)
		json_array_add_string_element (array, "overridden");
	if (state & LRG_PROPERTY_STATE_ANIMATED)
		json_array_add_string_element (array, "animated");
	if (state & LRG_PROPERTY_STATE_INVALID)
		json_array_add_string_element (array, "invalid");
	json_node_take_array (node, array);
	return node;
}

/* An IPC request's target: nodes, section, component */
typedef struct
{
	GPtrArray            *nodes;    /* LrgNode*, owned refs */
	LrgEditorNodeSection  section;
	gboolean              has_section;
	const gchar          *component;
	GPtrArray            *sources;  /* LrgEditorNodeSource*, when a section was given */
} IpcTarget;

static void
ipc_target_clear (IpcTarget *t)
{
	g_clear_pointer (&t->nodes, g_ptr_array_unref);
	g_clear_pointer (&t->sources, g_ptr_array_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (IpcTarget, ipc_target_clear)

/*
 * ipc_target:
 *
 * Reads "nodes" (array of GUIDs or paths), "node" (one), or neither
 * (the inspector's nodes); "section" ("header", "transform", "visual",
 * "component", "scripts"); "component" (key, for the component
 * section). Builds the sources when a section is given.
 */
static gboolean
ipc_target (LrgEditorApp  *app,
            JsonObject    *params,
            gboolean       need_section,
            IpcTarget     *t,
            GError       **error)
{
	LrgEditor *editor = lrg_editor_app_get_editor (app);
	guint      i;

	memset (t, 0, sizeof *t);
	t->nodes = g_ptr_array_new_with_free_func (g_object_unref);

	/* Nodes */
	if (params != NULL && json_object_has_member (params, "nodes"))
	{
		JsonArray *array = json_object_get_array_member (params, "nodes");

		for (i = 0; array != NULL && i < json_array_get_length (array); i++)
		{
			const gchar *ref = json_array_get_string_element (array, i);
			LrgNode     *node = ref != NULL ? lrg_editor_find_node (editor, ref) : NULL;

			if (node == NULL)
			{
				g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'",
				             ref != NULL ? ref : "(not a string)");
				return FALSE;
			}
			g_ptr_array_add (t->nodes, g_object_ref (node));
		}
	}
	else if (params != NULL && json_object_has_member (params, "node"))
	{
		const gchar *ref = json_object_get_string_member (params, "node");
		LrgNode     *node = ref != NULL ? lrg_editor_find_node (editor, ref) : NULL;

		if (node == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'",
			             ref != NULL ? ref : "(not a string)");
			return FALSE;
		}
		g_ptr_array_add (t->nodes, g_object_ref (node));
	}
	else
	{
		LrgEditorInspectorPanel *panel = inspector_of (app);
		GPtrArray               *nodes = panel != NULL ? panel->nodes
		                                 : lrg_editor_selection_get_nodes (lrg_editor_get_selection (editor));

		for (i = 0; nodes != NULL && i < nodes->len; i++)
			g_ptr_array_add (t->nodes, g_object_ref (g_ptr_array_index (nodes, i)));
	}
	if (t->nodes->len == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "No nodes: give \"node\" or \"nodes\", or select something");
		return FALSE;
	}

	/* Section and component */
	if (params != NULL && json_object_has_member (params, "section"))
	{
		if (!section_from_name (json_object_get_string_member (params, "section"), &t->section))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown section (header, transform, visual, component, scripts)");
			return FALSE;
		}
		t->has_section = TRUE;
	}
	else if (need_section)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing \"section\"");
		return FALSE;
	}
	if (params != NULL && json_object_has_member (params, "component"))
		t->component = json_object_get_string_member (params, "component");
	if (t->has_section && t->section == LRG_EDITOR_NODE_SECTION_COMPONENT && t->component == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "The component section needs \"component\"");
		return FALSE;
	}

	if (t->has_section)
	{
		t->sources = lrg_editor_node_source_new_for_nodes (editor, t->nodes, t->section, t->component);
		for (i = 0; i < t->sources->len; i++)
			lrg_editor_node_source_set_asset_root (g_ptr_array_index (t->sources, i),
			                                       lrg_editor_app_get_project_dir (app));
		if (t->sources->len != t->nodes->len)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "Not every node has a component '%s'", t->component);
			return FALSE;
		}
	}
	return TRUE;
}

/* source_info: @name of the first source when every source has it */
static LrgPropertyInfo *
source_info (GPtrArray    *sources,
             const gchar  *name,
             GError      **error)
{
	LrgPropertyInfo *info = lrg_property_source_find_property (g_ptr_array_index (sources, 0), name);
	guint            i;

	for (i = 1; info != NULL && i < sources->len; i++)
	{
		g_autoptr(LrgPropertyInfo) other = lrg_property_source_find_property (g_ptr_array_index (sources, i), name);

		if (other == NULL || lrg_property_info_get_value_type (other) != lrg_property_info_get_value_type (info))
			g_clear_pointer (&info, lrg_property_info_unref);
	}
	if (info == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No property '%s' common to the nodes", name);
	return info;
}

/* row_json: one row as the grid would show it, across all sources */
static JsonNode *
row_json (GPtrArray       *sources,
          LrgPropertyInfo *info)
{
	JsonObject       *o = json_object_new ();
	JsonNode         *node = json_node_new (JSON_NODE_OBJECT);
	const gchar      *name = lrg_property_info_get_name (info);
	g_auto(GValue)    first = G_VALUE_INIT;
	LrgPropertyState  state = LRG_PROPERTY_STATE_NONE;
	gboolean          mixed = FALSE;
	gboolean          all_default = TRUE;
	g_autofree gchar *path = NULL;
	guint             i;

	json_object_set_string_member (o, "name", name);
	json_object_set_string_member (o, "label", lrg_property_info_get_label (info));
	if (lrg_property_info_get_group (info) != NULL)
		json_object_set_string_member (o, "group", lrg_property_info_get_group (info));
	else
		json_object_set_null_member (o, "group");
	if (lrg_property_info_get_hint (info) != NULL)
		json_object_set_string_member (o, "hint", lrg_property_info_get_hint (info));
	else
		json_object_set_null_member (o, "hint");
	json_object_set_string_member (o, "type", g_type_name (lrg_property_info_get_value_type (info)));
	json_object_set_boolean_member (o, "read_only", lrg_property_info_get_read_only (info));

	/* Value of the first source; mixed and state across all */
	if (lrg_property_source_get_value (g_ptr_array_index (sources, 0), name, &first))
		json_object_set_member (o, "value", value_to_json (&first));
	else
		json_object_set_null_member (o, "value");
	for (i = 0; i < sources->len; i++)
	{
		LrgPropertySource *src = g_ptr_array_index (sources, i);
		LrgPropertyState   s = lrg_property_source_get_state (src, name);

		if (i > 0 && G_IS_VALUE (&first))
		{
			g_auto(GValue) other = G_VALUE_INIT;

			if (lrg_property_source_get_value (src, name, &other) && !lrg_property_values_equal (&first, &other))
				mixed = TRUE;
		}
		state |= s & (LRG_PROPERTY_STATE_OVERRIDDEN | LRG_PROPERTY_STATE_ANIMATED | LRG_PROPERTY_STATE_INVALID);
		if (!(s & LRG_PROPERTY_STATE_DEFAULT))
			all_default = FALSE;
	}
	if (all_default)
		state |= LRG_PROPERTY_STATE_DEFAULT;
	json_object_set_boolean_member (o, "mixed", mixed);
	json_object_set_member (o, "state", state_to_json (state));
	path = lrg_editor_node_source_get_override_path (g_ptr_array_index (sources, 0), name);
	if (path != NULL)
		json_object_set_string_member (o, "override_path", path);
	json_node_take_object (node, o);
	return node;
}

/* rows_json: every property all sources share, in the first one's order */
static JsonNode *
rows_json (GPtrArray *sources)
{
	g_autoptr(GPtrArray) infos = lrg_property_source_list_properties (g_ptr_array_index (sources, 0));
	JsonArray           *array = json_array_new ();
	JsonNode            *node = json_node_new (JSON_NODE_ARRAY);
	guint                i;

	for (i = 0; i < infos->len; i++)
	{
		LrgPropertyInfo           *info = g_ptr_array_index (infos, i);
		g_autoptr(LrgPropertyInfo) common = source_info (sources, lrg_property_info_get_name (info), NULL);

		if (common != NULL)
			json_array_add_element (array, row_json (sources, common));
	}
	json_node_take_array (node, array);
	return node;
}

/* section_json: {section, component?, rows} */
static JsonNode *
section_json (GPtrArray            *sources,
              LrgEditorNodeSection  section,
              const gchar          *component)
{
	JsonObject *o = json_object_new ();
	JsonNode   *node = json_node_new (JSON_NODE_OBJECT);

	json_object_set_string_member (o, "section", section_names[section]);
	if (component != NULL)
		json_object_set_string_member (o, "component", component);
	json_object_set_member (o, "rows", rows_json (sources));
	json_node_take_object (node, o);
	return node;
}

static JsonNode *
nodes_json (GPtrArray *nodes)
{
	JsonArray *array = json_array_new ();
	JsonNode  *node = json_node_new (JSON_NODE_ARRAY);
	guint      i;

	for (i = 0; i < nodes->len; i++)
		json_array_add_string_element (array, lrg_node_get_guid (g_ptr_array_index (nodes, i)));
	json_node_take_array (node, array);
	return node;
}

/*
 * inspector.list {node|nodes?, section?, component?}
 *   -> {nodes, section, component?, rows} for one section, or
 *      {nodes, sections: [{section, component?, rows}]} for all of them
 */
static JsonNode *
m_inspector_list (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	g_auto(IpcTarget)        t = { 0 };
	JsonObject              *o;
	JsonNode                *result;

	if (!ipc_target (app, params, FALSE, &t, error))
		return NULL;

	if (t.has_section)
	{
		result = section_json (t.sources, t.section, t.component);
		o = json_node_get_object (result);
	}
	else
	{
		static const LrgEditorNodeSection order[] = {
			LRG_EDITOR_NODE_SECTION_HEADER, LRG_EDITOR_NODE_SECTION_TRANSFORM, LRG_EDITOR_NODE_SECTION_VISUAL
		};
		LrgEditor            *editor = lrg_editor_app_get_editor (app);
		JsonArray            *sections = json_array_new ();
		g_autoptr(GPtrArray)  keys = common_component_keys (t.nodes);
		g_autoptr(GPtrArray)  scripts = NULL;
		guint                 i;

		result = json_node_new (JSON_NODE_OBJECT);
		o = json_object_new ();
		json_node_take_object (result, o);
		for (i = 0; i < G_N_ELEMENTS (order); i++)
		{
			g_autoptr(GPtrArray) sources = lrg_editor_node_source_new_for_nodes (editor, t.nodes, order[i], NULL);

			json_array_add_element (sections, section_json (sources, order[i], NULL));
		}
		for (i = 0; i < keys->len; i++)
		{
			g_autoptr(GPtrArray) sources = lrg_editor_node_source_new_for_nodes (editor, t.nodes,
			                                                                     LRG_EDITOR_NODE_SECTION_COMPONENT,
			                                                                     g_ptr_array_index (keys, i));

			json_array_add_element (sections, section_json (sources, LRG_EDITOR_NODE_SECTION_COMPONENT,
			                                                g_ptr_array_index (keys, i)));
		}
		scripts = lrg_editor_node_source_new_for_nodes (editor, t.nodes, LRG_EDITOR_NODE_SECTION_SCRIPTS, NULL);
		json_array_add_element (sections, section_json (scripts, LRG_EDITOR_NODE_SECTION_SCRIPTS, NULL));
		json_object_set_array_member (o, "sections", sections);
	}
	json_object_set_member (o, "nodes", nodes_json (t.nodes));
	return result;
}

/* inspector.get {node|nodes?, section, component?, name} -> the row */
static JsonNode *
m_inspector_get (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	g_auto(IpcTarget)          t = { 0 };
	g_autoptr(LrgPropertyInfo) info = NULL;
	const gchar               *name;

	if (!ipc_target (LRG_EDITOR_APP (user_data), params, TRUE, &t, error))
		return NULL;
	name = json_object_has_member (params, "name") ? json_object_get_string_member (params, "name") : NULL;
	if (name == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing \"name\"");
		return NULL;
	}
	info = source_info (t.sources, name, error);
	return info != NULL ? row_json (t.sources, info) : NULL;
}

/*
 * inspector.set {node|nodes?, section, component?, name, value}
 *
 * Writes @value to every node as ONE undo step: each source previews
 * inside a gesture, then all gestures are committed in one editor group
 * labelled "Set <label>" (what a drag in the grid does).
 */
static JsonNode *
m_inspector_set (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp              *app = LRG_EDITOR_APP (user_data);
	LrgEditor                 *editor = lrg_editor_app_get_editor (app);
	g_auto(IpcTarget)          t = { 0 };
	g_autoptr(LrgPropertyInfo) info = NULL;
	g_auto(GValue)             value = G_VALUE_INIT;
	g_autofree gchar          *label = NULL;
	const gchar               *name;
	guint                      i;
	gboolean                   ok = TRUE;

	if (!ipc_target (app, params, TRUE, &t, error))
		return NULL;
	name = json_object_has_member (params, "name") ? json_object_get_string_member (params, "name") : NULL;
	if (name == NULL || !json_object_has_member (params, "value"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Needs \"name\" and \"value\"");
		return NULL;
	}
	info = source_info (t.sources, name, error);
	if (info == NULL)
		return NULL;
	if (lrg_property_info_get_read_only (info))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'%s' is read-only", name);
		return NULL;
	}
	if (!json_to_value (json_object_get_member (params, "value"), lrg_property_info_get_value_type (info), &value,
	                    error))
		return NULL;

	/* Preview on every node, then one undo step for all of them */
	label = g_strdup_printf ("Set %s", lrg_property_info_get_label (info));
	for (i = 0; i < t.sources->len; i++)
		lrg_editor_node_source_begin_gesture (g_ptr_array_index (t.sources, i));
	for (i = 0; i < t.sources->len; i++)
		ok &= lrg_property_source_set_value (g_ptr_array_index (t.sources, i), name, &value);
	lrg_editor_begin_group (editor, label);
	for (i = 0; i < t.sources->len; i++)
		lrg_editor_node_source_end_gesture (g_ptr_array_index (t.sources, i), ok, label);
	lrg_editor_end_group (editor);
	if (!ok)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "The value was refused for '%s'", name);
		return NULL;
	}
	return row_json (t.sources, info);
}

/*
 * inspector.reset {..., name}: what the row's "Reset to default" does:
 * overridden prefab properties go back to the prefab's value, the
 * others to their default, all as one undo step.
 */
static JsonNode *
m_inspector_reset (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp              *app = LRG_EDITOR_APP (user_data);
	LrgEditor                 *editor = lrg_editor_app_get_editor (app);
	g_auto(IpcTarget)          t = { 0 };
	g_autoptr(LrgPropertyInfo) info = NULL;
	const gchar               *name;
	guint                      i;

	if (!ipc_target (app, params, TRUE, &t, error))
		return NULL;
	name = json_object_has_member (params, "name") ? json_object_get_string_member (params, "name") : NULL;
	if (name == NULL || (info = source_info (t.sources, name, error)) == NULL)
	{
		if (name == NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Missing \"name\"");
		return NULL;
	}

	lrg_editor_begin_group (editor, "Reset to Default");
	for (i = 0; i < t.sources->len; i++)
	{
		LrgEditorNodeSource *src = g_ptr_array_index (t.sources, i);

		if (lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), name) & LRG_PROPERTY_STATE_OVERRIDDEN)
			lrg_editor_node_source_revert_override (src, name, NULL);
		else
			lrg_property_source_reset (LRG_PROPERTY_SOURCE (src), name);
	}
	lrg_editor_end_group (editor);
	return row_json (t.sources, info);
}

/* inspector.revert_override / inspector.apply_override {..., name} */
static JsonNode *
override_method (LrgEditorApp  *app,
                 JsonObject    *params,
                 gboolean       apply,
                 GError       **error)
{
	LrgEditor                 *editor = lrg_editor_app_get_editor (app);
	g_auto(IpcTarget)          t = { 0 };
	g_autoptr(LrgPropertyInfo) info = NULL;
	g_autoptr(GError)          first_error = NULL;
	const gchar               *name;
	guint                      done = 0;
	guint                      i;
	JsonNode                  *row;

	if (!ipc_target (app, params, TRUE, &t, error))
		return NULL;
	name = json_object_has_member (params, "name") ? json_object_get_string_member (params, "name") : NULL;
	if (name == NULL || (info = source_info (t.sources, name, error)) == NULL)
	{
		if (name == NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Missing \"name\"");
		return NULL;
	}

	lrg_editor_begin_group (editor, apply ? "Apply to Prefab" : "Revert to Prefab");
	for (i = 0; i < t.sources->len; i++)
	{
		LrgEditorNodeSource *src = g_ptr_array_index (t.sources, i);
		g_autoptr(GError)    e = NULL;
		gboolean             ok;

		if (!(lrg_property_source_get_state (LRG_PROPERTY_SOURCE (src), name) & LRG_PROPERTY_STATE_OVERRIDDEN))
			continue;
		ok = apply ? lrg_editor_node_source_apply_override (src, name, &e)
		           : lrg_editor_node_source_revert_override (src, name, &e);
		if (ok)
			done++;
		else if (first_error == NULL)
			first_error = g_steal_pointer (&e);
	}
	lrg_editor_end_group (editor);

	if (done == 0)
	{
		if (first_error != NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s",
			             first_error->message);
		else
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "'%s' is not overridden on these nodes", name);
		return NULL;
	}
	row = row_json (t.sources, info);
	json_object_set_int_member (json_node_get_object (row), "count", done);
	return row;
}

static JsonNode *
m_inspector_revert_override (LrgEditorServer  *server,
                             JsonObject       *params,
                             gpointer          user_data,
                             GError          **error)
{
	return override_method (LRG_EDITOR_APP (user_data), params, FALSE, error);
}

static JsonNode *
m_inspector_apply_override (LrgEditorServer  *server,
                            JsonObject       *params,
                            gpointer          user_data,
                            GError          **error)
{
	return override_method (LRG_EDITOR_APP (user_data), params, TRUE, error);
}

/*
 * inspector.state {} -> {nodes, locked, lock, live, live_type,
 *   components, prefab}: what the inspector panel shows (an error
 *   when the app has no inspector panel)
 */
static JsonNode *
m_inspector_state (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	LrgEditorInspectorPanel *self = inspector_of (app);
	JsonObject              *o;
	JsonNode                *node;
	JsonArray               *comps;
	guint                    i;

	if (self == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No inspector panel");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_member (o, "nodes", nodes_json (self->nodes));
	json_object_set_boolean_member (o, "locked", self->locked);
	if (lrg_editor_view_state_get_inspector_lock (self->view) != NULL)
		json_object_set_string_member (o, "lock", lrg_editor_view_state_get_inspector_lock (self->view));
	else
		json_object_set_null_member (o, "lock");
	json_object_set_boolean_member (o, "live", self->live_object != NULL);
	if (self->live_object != NULL)
		json_object_set_string_member (o, "live_type", G_OBJECT_TYPE_NAME (self->live_object));
	comps = json_array_new ();
	for (i = 0; self->live_object == NULL && self->comp_keys != NULL && self->comp_keys[i] != NULL; i++)
		json_array_add_string_element (comps, self->comp_keys[i]);
	for (i = 0; self->live_object != NULL && self->live_keys != NULL && self->live_keys[i] != NULL; i++)
		json_array_add_string_element (comps, self->live_keys[i]);
	json_object_set_array_member (o, "components", comps);
	json_object_set_boolean_member (o, "prefab", lrg_widget_get_visible (LRG_WIDGET (self->prefab_bar)));
	node = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (node, o);
	return node;
}

static void
register_methods (LrgEditorApp *app)
{
	LrgEditorServer *server = lrg_editor_app_get_server (app);

	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "inspector.list", m_inspector_list, app, NULL);
	lrg_editor_server_register_method (server, "inspector.get", m_inspector_get, app, NULL);
	lrg_editor_server_register_method (server, "inspector.set", m_inspector_set, app, NULL);
	lrg_editor_server_register_method (server, "inspector.reset", m_inspector_reset, app, NULL);
	lrg_editor_server_register_method (server, "inspector.revert_override", m_inspector_revert_override, app, NULL);
	lrg_editor_server_register_method (server, "inspector.apply_override", m_inspector_apply_override, app, NULL);
	lrg_editor_server_register_method (server, "inspector.state", m_inspector_state, app, NULL);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_toggle_lock (gpointer data)
{
	LrgEditorInspectorPanel *self = inspector_of (LRG_EDITOR_APP (data));

	if (self != NULL)
		lrg_editor_inspector_panel_set_locked (self, !self->locked);
}

static gboolean
a_lock_checked (gpointer data)
{
	LrgEditorInspectorPanel *self = inspector_of (LRG_EDITOR_APP (data));

	return self != NULL && self->locked;
}

static void
a_add_component (gpointer data)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (data);
	LrgEditorInspectorPanel *self = inspector_of (app);

	if (self == NULL)
		return;
	editor_app_show_panel (app, "inspector");
	lrg_editor_inspector_panel_show_add_component (self);
}

static gboolean
a_has_selection (gpointer data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (data);

	return lrg_editor_selection_get_count (lrg_editor_get_selection (lrg_editor_app_get_editor (app))) > 0;
}

static void
register_actions (LrgEditorApp *app)
{
	LrgEditorActions *actions = lrg_editor_app_get_actions (app);

	lrg_editor_actions_add (actions, "inspector.toggle-lock", "Lock Inspector", "View", NULL, a_toggle_lock, NULL,
	                        a_lock_checked, app);
	lrg_editor_actions_add (actions, "node.add-component", "Add Component…", "Node", NULL, a_add_component,
	                        a_has_selection, NULL, app);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorInspectorPanel *
lrg_editor_inspector_panel_new (LrgEditorApp *app)
{
	LrgEditorInspectorPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_INSPECTOR_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "inspector");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->view = g_object_ref (lrg_editor_app_get_view_state (app));
	self->play = g_object_ref (lrg_editor_app_get_play_session (app));

	build_chrome (self);
	build_body (self);

	g_signal_connect (lrg_editor_get_selection (self->editor), "changed", G_CALLBACK (on_selection_changed), self);
	g_signal_connect (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect (self->view, "notify::inspector-lock", G_CALLBACK (on_view_lock_changed), self);
	g_signal_connect (self->play, "started", G_CALLBACK (on_play_started), self);
	g_signal_connect (self->play, "stopped", G_CALLBACK (on_play_stopped), self);
	g_signal_connect (self->play, "ticked", G_CALLBACK (on_play_ticked), self);

	register_methods (app);
	register_actions (app);
	hook_hierarchy (self);
	refresh_all (self);
	return self;
}

LrgEditorApp *
lrg_editor_inspector_panel_get_app (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	return self->app;
}

GPtrArray *
lrg_editor_inspector_panel_get_nodes (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	return self->nodes;
}

LrgPropertyGrid *
lrg_editor_inspector_panel_get_grid (LrgEditorInspectorPanel *self,
                                     LrgEditorNodeSection     section,
                                     const gchar             *component)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);

	if (section != LRG_EDITOR_NODE_SECTION_COMPONENT)
		return (guint) section < N_FIXED ? self->grids[section] : NULL;
	if (component == NULL || self->comp_keys == NULL || !g_strv_contains ((const gchar * const *) self->comp_keys,
	                                                                      component))
		return NULL;
	for (i = 0; i < self->comp_pool->len; i++)
	{
		CompSection *sec = g_ptr_array_index (self->comp_pool, i);

		if (g_strcmp0 (sec->key, component) == 0)
			return sec->grid;
	}
	return NULL;
}

LrgPropertyGrid *
lrg_editor_inspector_panel_get_level_grid (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	return self->level_grid;
}

gboolean
lrg_editor_inspector_panel_get_locked (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), FALSE);
	return self->locked;
}

void
lrg_editor_inspector_panel_set_locked (LrgEditorInspectorPanel *self,
                                       gboolean                 locked)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self));

	locked = !!locked;
	if (locked == self->locked)
		return;

	/* Locking keeps what is shown now; nothing shown, nothing to lock */
	g_ptr_array_set_size (self->lock_nodes, 0);
	if (locked)
	{
		for (i = 0; i < self->nodes->len; i++)
			g_ptr_array_add (self->lock_nodes, g_object_ref (g_ptr_array_index (self->nodes, i)));
		if (self->lock_nodes->len == 0)
		{
			insp_icon_button_set_checked (self->lock_button, FALSE);
			status (self, "Select something to lock the inspector to");
			return;
		}
	}
	self->locked = locked;
	insp_icon_button_set_checked (self->lock_button, locked);

	/* The view state remembers the (first) locked node */
	self->syncing_lock = TRUE;
	lrg_editor_view_state_set_inspector_lock (self->view,
	                                          locked ? lrg_node_get_guid (g_ptr_array_index (self->lock_nodes, 0))
	                                                 : NULL);
	self->syncing_lock = FALSE;
	status (self, locked ? "Inspector locked" : "Inspector follows the selection");
	refresh_all (self);
}

GObject *
lrg_editor_inspector_panel_get_live_object (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	return self->live_object;
}

void
lrg_editor_inspector_panel_set_live_object (LrgEditorInspectorPanel *self,
                                            GObject                 *object)
{
	g_return_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self));
	g_return_if_fail (object == NULL || LRG_IS_GAME_OBJECT (object));

	g_set_weak_pointer (&self->live_explicit, object);
	refresh_all (self);
}

LrgPropertyGrid *
lrg_editor_inspector_panel_get_live_grid (LrgEditorInspectorPanel *self,
                                          const gchar             *component)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	g_return_val_if_fail (component != NULL, NULL);

	if (self->live_keys == NULL || !g_strv_contains ((const gchar * const *) self->live_keys, component))
		return NULL;
	for (i = 0; i < self->live_pool->len; i++)
	{
		LiveSection *sec = g_ptr_array_index (self->live_pool, i);

		if (g_strcmp0 (sec->key, component) == 0)
			return sec->grid;
	}
	return NULL;
}

gboolean
lrg_editor_inspector_panel_show_add_component (LrgEditorInspectorPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), FALSE);

	if (self->nodes->len == 0 || self->live_object != NULL)
		return FALSE;
	return open_picker (self, LRG_WIDGET (self->add_component), "Add Component", component_items (),
	                    "Add \"%s\" (custom type)", on_component_picked, NULL, NULL);
}

gboolean
lrg_editor_inspector_panel_attach_script (LrgEditorInspectorPanel *self,
                                          const gchar             *path)
{
	g_autofree gchar *rel = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (self->nodes->len == 0)
		return FALSE;

	/* Project-relative when the file is inside the project */
	rel = (g_path_is_absolute (path) && self->app != NULL) ? editor_app_relative_path (self->app, path)
	                                                       : g_strdup (path);
	if (rel == NULL)
		rel = g_strdup (path);

	lrg_editor_begin_group (self->editor, "Attach Script");
	for (i = 0; i < self->nodes->len; i++)
	{
		g_autoptr(LrgScriptBinding) binding = lrg_script_binding_new (script_language_for (rel), rel);

		lrg_editor_add_script (self->editor, g_ptr_array_index (self->nodes, i), binding);
	}
	lrg_editor_end_group (self->editor);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Attached %s", rel);
	return TRUE;
}
