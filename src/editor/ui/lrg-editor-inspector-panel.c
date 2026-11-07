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
#include "../../world3d/lrg-entity3d.h"
#include "../../world3d/lrg-world3d.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-expander.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-ui-event.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../graphics/lrg-lod-group.h"
#include "../../animation/lrg-anim-graph.h"
#include "../../animation/lrg-animation-layer.h"
#include "../../animation/lrg-animation-state-machine.h"
#include "../../animation/lrg-ik-chain.h"
#include "../../world3d/components/lrg-animator-component3d.h"
#include "data/lrg-editor-anim-graph-editor.h"
#include "lrg-editor-scene-renderer.h"
#include "lrg-editor-gizmo-paint.h"
#include "lrg-editor-viewport-panel.h"
#include "lrg-editor-viewport-split.h"
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
 * Private: InspEntitySource, a live 3D entity's own state
 * ========================================================================== */

#define INSP_TYPE_ENTITY_SOURCE (insp_entity_source_get_type ())
G_DECLARE_FINAL_TYPE (InspEntitySource, insp_entity_source, INSP, ENTITY_SOURCE, GObject)

/*
 * InspEntitySource:
 *
 * The "Entity" section of a live #LrgEntity3D during 3D play: name,
 * active and visible flags, the local transform (position, Euler
 * rotation in degrees, scale) and the read-only world position. Edits
 * go straight into the play world like every live row (a DIRECT grid):
 * they are not undoable and are dropped on Stop unless the simulation
 * sync-back writes them into the node. The entity is held weakly, so a
 * destroyed entity just reads as nothing.
 */
struct _InspEntitySource
{
	GObject      parent_instance;

	LrgEntity3D *entity;   /* weak */
	GPtrArray   *pspecs;   /* GParamSpec*, owned refs */
};

static void insp_entity_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (InspEntitySource, insp_entity_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, insp_entity_source_iface_init))

#define INSP_RAD2DEG (180.0 / G_PI)
#define INSP_DEG2RAD (G_PI / 180.0)

static GPtrArray *
insp_entity_source_list (LrgPropertySource *source)
{
	InspEntitySource *self = INSP_ENTITY_SOURCE (source);
	GPtrArray        *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint             i;

	for (i = 0; i < self->pspecs->len; i++)
		g_ptr_array_add (infos, lrg_property_info_new (g_ptr_array_index (self->pspecs, i)));
	return infos;
}

/* A boxed GrlVector3 value from three floats (scaled by @k). */
static void
insp_vec3_value (GValue       *value,
                 const gfloat *v,
                 gdouble       k)
{
	g_value_init (value, GRL_TYPE_VECTOR3);
	g_value_take_boxed (value, grl_vector3_new ((gfloat) (v[0] * k), (gfloat) (v[1] * k), (gfloat) (v[2] * k)));
}

static gboolean
insp_entity_source_get (LrgPropertySource *source,
                        const gchar       *name,
                        GValue            *value)
{
	InspEntitySource *self = INSP_ENTITY_SOURCE (source);
	gfloat            v[3];

	if (self->entity == NULL)
		return FALSE;

	if (g_strcmp0 (name, "name") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_entity3d_get_name (self->entity));
	}
	else if (g_strcmp0 (name, "active") == 0)
	{
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_entity3d_get_active (self->entity));
	}
	else if (g_strcmp0 (name, "visible") == 0)
	{
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_entity3d_get_visible (self->entity));
	}
	else if (g_strcmp0 (name, "position") == 0)
	{
		lrg_entity3d_get_position (self->entity, v);
		insp_vec3_value (value, v, 1.0);
	}
	else if (g_strcmp0 (name, "rotation") == 0)
	{
		lrg_entity3d_get_rotation_euler (self->entity, v);
		insp_vec3_value (value, v, INSP_RAD2DEG);
	}
	else if (g_strcmp0 (name, "scale") == 0)
	{
		lrg_entity3d_get_scale (self->entity, v);
		insp_vec3_value (value, v, 1.0);
	}
	else if (g_strcmp0 (name, "world") == 0)
	{
		lrg_entity3d_get_world_position (self->entity, v);
		insp_vec3_value (value, v, 1.0);
	}
	else
		return FALSE;
	return TRUE;
}

static gboolean
insp_entity_source_set (LrgPropertySource *source,
                        const gchar       *name,
                        const GValue      *value)
{
	InspEntitySource *self = INSP_ENTITY_SOURCE (source);
	GrlVector3       *vec;
	gfloat            v[3];

	if (self->entity == NULL)
		return FALSE;

	if (g_strcmp0 (name, "name") == 0 && G_VALUE_HOLDS_STRING (value))
		lrg_entity3d_set_name (self->entity, g_value_get_string (value));
	else if (g_strcmp0 (name, "active") == 0 && G_VALUE_HOLDS_BOOLEAN (value))
		lrg_entity3d_set_active (self->entity, g_value_get_boolean (value));
	else if (g_strcmp0 (name, "visible") == 0 && G_VALUE_HOLDS_BOOLEAN (value))
		lrg_entity3d_set_visible (self->entity, g_value_get_boolean (value));
	else if (G_VALUE_HOLDS (value, GRL_TYPE_VECTOR3) && (vec = g_value_get_boxed (value)) != NULL)
	{
		v[0] = vec->x;
		v[1] = vec->y;
		v[2] = vec->z;
		if (g_strcmp0 (name, "position") == 0)
			lrg_entity3d_set_position (self->entity, v);
		else if (g_strcmp0 (name, "scale") == 0)
			lrg_entity3d_set_scale (self->entity, v);
		else if (g_strcmp0 (name, "rotation") == 0)
		{
			v[0] = (gfloat) (v[0] * INSP_DEG2RAD);
			v[1] = (gfloat) (v[1] * INSP_DEG2RAD);
			v[2] = (gfloat) (v[2] * INSP_DEG2RAD);
			lrg_entity3d_set_rotation_euler (self->entity, v);
		}
		else
			return FALSE;
	}
	else
		return FALSE;

	lrg_property_source_emit_changed (source, name);
	return TRUE;
}

static LrgPropertyState
insp_entity_source_get_state (LrgPropertySource *source,
                              const gchar       *name)
{
	return LRG_PROPERTY_STATE_NONE;
}

static void
insp_entity_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = insp_entity_source_list;
	iface->get_value = insp_entity_source_get;
	iface->set_value = insp_entity_source_set;
	iface->get_state = insp_entity_source_get_state;
}

static void
insp_entity_source_dispose (GObject *object)
{
	g_clear_weak_pointer (&INSP_ENTITY_SOURCE (object)->entity);
	G_OBJECT_CLASS (insp_entity_source_parent_class)->dispose (object);
}

static void
insp_entity_source_finalize (GObject *object)
{
	g_ptr_array_unref (INSP_ENTITY_SOURCE (object)->pspecs);
	G_OBJECT_CLASS (insp_entity_source_parent_class)->finalize (object);
}

static void
insp_entity_source_class_init (InspEntitySourceClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = insp_entity_source_dispose;
	G_OBJECT_CLASS (klass)->finalize = insp_entity_source_finalize;
}

static void
insp_entity_source_init (InspEntitySource *self)
{
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;
	GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	self->pspecs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_string ("name", "Name", "The entity's name", NULL, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boolean ("active", "Active", "Takes part in updates", TRUE, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boolean ("visible", "Visible", "Collected for rendering", TRUE, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boxed ("position", "Position", "[hint:vector3] Local position", GRL_TYPE_VECTOR3, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boxed ("rotation", "Rotation", "[hint:vector3] Local Euler rotation, in degrees",
		                    GRL_TYPE_VECTOR3, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boxed ("scale", "Scale", "[hint:vector3] Local scale", GRL_TYPE_VECTOR3, rw)));
	g_ptr_array_add (self->pspecs, g_param_spec_ref_sink (
		g_param_spec_boxed ("world", "World", "[hint:vector3] World-space position (read-only)",
		                    GRL_TYPE_VECTOR3, ro)));
}

static InspEntitySource *
insp_entity_source_new (LrgEntity3D *entity)
{
	InspEntitySource *self = g_object_new (INSP_TYPE_ENTITY_SOURCE, NULL);

	g_set_weak_pointer (&self->entity, entity);
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
	GObject         *target;    /* owned ref while shown: a component, or the
	                             * entity for the "entity" section */
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
	LrgButton            *prefab_variant;   /* Create Variant (0.9.0) */
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

	/* Physics summary (0.9.0 M5): one line about the node's body, a
	 * button to the Physics panel */
	LrgExpander          *phys_section;
	LrgLabel             *phys_summary;
	LrgButton            *phys_add;

	/* LOD preview (0.9.0): mesh-asset nodes whose model has LOD levels */
	LrgExpander          *lod_section;
	LrgWidget            *lod_levels;   /* custom: screen-size meter and level rows */
	LrgComboBox          *lod_force;
	LrgLodGroup          *lod_group;    /* weak: the group shown (owned by the scene renderer) */
	LrgNode              *lod_node;     /* weak: its node */
	guint                 lod_n;        /* levels the force combo lists */
	gboolean              lod_syncing;

	/* Animator (0.9.0): nodes with an animator3d component */
	LrgExpander          *anim_section;
	LrgLabel             *anim_label;
	LrgButton            *anim_open;
	LrgWidget            *anim_list;     /* custom: parameters, layers, IK goals */
	gchar                *anim_path;     /* absolute graph path shown */
	LrgAnimGraph         *anim_file_graph; /* loaded from anim_path (authoring view) */
	gchar                *anim_error;
	GPtrArray            *anim_hits;     /* AnimHit* of the last paint */
	gchar                *anim_drag;     /* parameter being dragged */
	gfloat                anim_drag_x;
	gfloat                anim_drag_value;

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
 * prefab_chain_text:
 *
 * "base.rprefab > variant.rprefab" for a source that is a prefab variant
 * (base first, then each variant down to @source), or NULL for a plain
 * prefab or an unreadable chain. The chain is read from the files once
 * per source and cached on the info label until the source changes.
 */
static const gchar *
prefab_chain_text (LrgEditorInspectorPanel *self,
                   const gchar             *source)
{
	const gchar         *cached_src = g_object_get_data (G_OBJECT (self->prefab_info), "insp-chain-src");
	g_autofree gchar    *abs = NULL;
	g_auto(GStrv)        chain = NULL;
	g_autoptr(GString)   text = NULL;
	gint                 i;

	if (g_strcmp0 (cached_src, source) == 0)
		return g_object_get_data (G_OBJECT (self->prefab_info), "insp-chain-text");

	abs = (self->app != NULL) ? editor_app_project_path (self->app, source) : NULL;
	chain = lrg_prefab_get_variant_chain (abs != NULL ? abs : source, NULL);
	if (chain != NULL && g_strv_length (chain) > 1)
	{
		text = g_string_new (NULL);
		for (i = (gint) g_strv_length (chain) - 1; i >= 0; i--)
		{
			g_autofree gchar *base = g_path_get_basename (chain[i]);

			g_string_append (text, base);
			if (i > 0)
				g_string_append (text, " > ");
		}
	}

	g_object_set_data_full (G_OBJECT (self->prefab_info), "insp-chain-src", g_strdup (source), g_free);
	g_object_set_data_full (G_OBJECT (self->prefab_info), "insp-chain-text",
	                        text != NULL ? g_string_free (g_steal_pointer (&text), FALSE) : NULL, g_free);
	return g_object_get_data (G_OBJECT (self->prefab_info), "insp-chain-text");
}

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
		const gchar *chain = prefab_chain_text (self, source);

		/* Overrides are always relative to the nearest source: the
		 * variant when the source is one, its base chain shown first. */
		n = lrg_prefab_link_get_n_overrides (link);
		if (chain != NULL)
			info = n == 0 ? g_strdup_printf ("Variant chain %s, no overrides", chain)
			              : g_strdup_printf ("Variant chain %s, %u override%s", chain, n, n == 1 ? "" : "s");
		else
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
	lrg_widget_set_visible (LRG_WIDGET (self->prefab_variant), link != NULL);
	lrg_widget_set_enabled (LRG_WIDGET (self->prefab_variant), link != NULL && node == owner);
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

/*
 * on_prefab_variant_clicked:
 *
 * Create Variant from the inspected instance root: the variant goes next
 * to the source as `<stem>_variant.rprefab` (resolved against the project
 * root), the instance's overrides move into it and the instance is
 * relinked to it; one undo step (lrg_editor_prefab_create_variant()).
 */
static void
on_prefab_variant_clicked (LrgButton *button,
                           gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autoptr(LrgNode)       owner = prefab_owner (self) != NULL ? g_object_ref (prefab_owner (self)) : NULL;
	g_autoptr(GError)        error = NULL;
	LrgPrefabLink           *link;
	const gchar             *source;
	g_autofree gchar        *abs = NULL;
	g_autofree gchar        *variant = NULL;

	if (owner == NULL || (link = lrg_node_get_prefab_link (owner)) == NULL)
		return;
	source = lrg_prefab_link_get_source_path (link);
	if (source == NULL)
		return;

	abs = (self->app != NULL) ? editor_app_project_path (self->app, source) : NULL;
	variant = lrg_editor_prefab_suggest_variant_path (abs != NULL ? abs : source);
	if (variant == NULL)
		return;

	if (lrg_editor_prefab_create_variant (self->editor, owner, variant, &error))
		status (self, "Created prefab variant");
	else if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Create variant failed: %s",
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
	g_clear_object (&sec->target);
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

	if (self == NULL || sec == NULL || !LRG_IS_COMPONENT (sec->target) || self->live_object == NULL)
		return;
	if (lrg_editor_play_session_apply_live (self->play, self->live_object, LRG_COMPONENT (sec->target),
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

/* The components of a live object (2D game object or 3D entity), in
 * attach order; the array does not own them. */
static GPtrArray *
live_object_components (GObject *object)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i;

	if (LRG_IS_GAME_OBJECT (object))
	{
		GList *list = lrg_game_object_get_components (LRG_GAME_OBJECT (object));
		GList *l;

		for (l = list; l != NULL; l = l->next)
			g_ptr_array_add (out, l->data);
		g_list_free (list);
	}
	else if (LRG_IS_ENTITY3D (object))
	{
		GPtrArray *comps = lrg_entity3d_get_components (LRG_ENTITY3D (object));

		for (i = 0; comps != NULL && i < comps->len; i++)
			g_ptr_array_add (out, g_ptr_array_index (comps, i));
	}
	return out;
}

/* The key of the live section showing a 3D entity's own state. */
#define LIVE_ENTITY_KEY "entity"

/*
 * update_live:
 *
 * One DIRECT grid per component of @object over an
 * LrgObjectPropertySource; the LrgComponent base properties other than
 * "enabled" are hidden. A 3D entity gets an "entity" section first (its
 * name, flags and transform, InspEntitySource). A grid's source is
 * replaced only when its target object changed.
 */
static void
update_live (LrgEditorInspectorPanel *self,
             GObject                 *object)
{
	g_autoptr(GPtrArray)  keys = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray)  targets = g_ptr_array_new ();
	g_autoptr(GHashTable) counts = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GPtrArray)  list = live_object_components (object);
	guint                 i;

	if (LRG_IS_ENTITY3D (object))
	{
		g_ptr_array_add (keys, g_strdup (LIVE_ENTITY_KEY));
		g_ptr_array_add (targets, object);
	}
	for (i = 0; i < list->len; i++)
	{
		LrgComponent *comp = g_ptr_array_index (list, i);
		const gchar  *type = live_type_name (comp);
		guint         n = GPOINTER_TO_UINT (g_hash_table_lookup (counts, type)) + 1;

		g_hash_table_insert (counts, (gpointer) type, GUINT_TO_POINTER (n));
		g_ptr_array_add (keys, n == 1 ? g_strdup (type) : g_strdup_printf ("%s#%u", type, n));
		g_ptr_array_add (targets, comp);
	}

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
			if (sec->target != NULL)
			{
				lrg_property_grid_set_sources (sec->grid, NULL);
				g_clear_object (&sec->target);
			}
			continue;
		}
		if (sec->target == g_ptr_array_index (targets, at))
			continue;

		g_set_object (&sec->target, g_ptr_array_index (targets, at));
		if (LRG_IS_ENTITY3D (sec->target))
		{
			/* The entity's own state: no "Apply to Level" (the
			 * sync-back already writes simulated transforms) */
			g_autoptr(InspEntitySource) source = insp_entity_source_new (LRG_ENTITY3D (sec->target));

			lrg_property_grid_set_source (sec->grid, LRG_PROPERTY_SOURCE (source));
			update_row_menus (sec->grid, FALSE, FALSE);
		}
		else
		{
			g_autoptr(LrgObjectPropertySource) source = NULL;
			GObjectClass                      *base = g_type_class_ref (LRG_TYPE_COMPONENT);
			GParamSpec                       **pspecs;
			guint                              n = 0;
			guint                              k;

			source = lrg_object_property_source_new (sec->target);
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

		if (sec->target != NULL)
		{
			lrg_property_grid_set_sources (sec->grid, NULL);
			g_clear_object (&sec->target);
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
	LrgWorld3D *world3d;

	if (self->play == NULL || !lrg_editor_play_session_is_playing (self->play))
		return NULL;

	/* The Live tab's pick: a game object made from a node, or any entity
	 * still in the 3D play world (spawned ones too) */
	world3d = lrg_editor_play_session_get_world3d (self->play);
	if (self->live_explicit != NULL &&
	    (lrg_editor_play_session_get_guid_for_live (self->play, self->live_explicit) != NULL ||
	     (world3d != NULL && LRG_IS_ENTITY3D (self->live_explicit) &&
	      lrg_entity3d_get_world (LRG_ENTITY3D (self->live_explicit)) == world3d)))
		return self->live_explicit;
	if (self->nodes->len > 0)
		return lrg_editor_play_session_get_live_for_node (self->play, g_ptr_array_index (self->nodes, 0));
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
	if (LRG_IS_GAME_OBJECT (live))
		g_string_append_printf (sig, "c%u|", lrg_game_object_get_component_count (LRG_GAME_OBJECT (live)));
	else if (LRG_IS_ENTITY3D (live))
		g_string_append_printf (sig, "e%u|", lrg_entity3d_get_n_components (LRG_ENTITY3D (live)));
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
		const gchar *guid = lrg_editor_play_session_get_guid_for_live (self->play, live);
		LrgNode     *node = guid != NULL ? lrg_editor_find_node (self->editor, guid) : NULL;
		const gchar *name = (node != NULL) ? lrg_node_get_name (node)
		                  : LRG_IS_ENTITY3D (live) ? lrg_entity3d_get_name (LRG_ENTITY3D (live)) : NULL;

		text = g_strdup_printf ("%s (live)", name != NULL ? name : G_OBJECT_TYPE_NAME (live));
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

/* ==========================================================================
 * LOD preview (0.9.0)
 *
 * For one selected mesh-asset node whose model has LOD levels (the scene
 * renderer's per-asset LrgLodGroup, built from the asset's .meta): a meter
 * of the node's screen size in the active viewport against every level's
 * threshold, one row per level (triangles, simplification error, the
 * threshold) with the level drawn now highlighted, and a "Force level"
 * picker. Forcing sets the shared group's force level, so every instance
 * of the asset shows it; it is a preview, reset when the node is no
 * longer inspected (lrg_lod_group_set_force_level (group, -1)).
 * ========================================================================== */

/* The group of @node, or NULL (no window, model not loaded, one level). */
static LrgLodGroup *
lod_group_for (LrgEditorInspectorPanel *self,
               LrgNode                 *node)
{
	LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

	if (self->app == NULL || self->app->renderer == NULL || visual == NULL ||
	    lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_MESH_ASSET || lrg_node_visual_get_asset (visual) == NULL)
		return NULL;
	return lrg_editor_scene_renderer_get_lod_group (self->app->renderer, lrg_node_visual_get_asset (visual));
}

static void
lod_queue_viewports (LrgEditorInspectorPanel *self)
{
	guint i;

	if (self->app == NULL || self->app->viewport_split == NULL)
		return;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewportPanel *p = lrg_editor_viewport_split_get_panel (self->app->viewport_split, i);

		if (p != NULL)
			lrg_editor_viewport_panel_queue_render (p);
	}
}

/* The current screen size of the inspected LOD node. */
static gfloat
lod_screen_size (LrgEditorInspectorPanel *self)
{
	LrgEditorView view;

	if (self->lod_group == NULL || self->lod_node == NULL || self->view == NULL)
		return 0.0f;
	lrg_editor_view_state_get_view (self->view, -1, &view);
	return lrg_editor_lod_node_screen_size (&view, self->lod_node, self->lod_group);
}

#define LOD_METER_H (34.0f)
#define LOD_ROW_H   (22.0f)

static void
lod_levels_measure (LrgCustomWidget     *widget,
                    gfloat               avail_width,
                    gfloat               avail_height,
                    LrgCustomWidgetSize *size,
                    gpointer             user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	guint                    n = (self->lod_group != NULL) ? lrg_lod_group_get_n_levels (self->lod_group) : 0;

	size->width = 160.0f;
	size->height = LOD_METER_H + (gfloat) n * LOD_ROW_H + 4.0f;
}

/* Screen size 0..1 -> meter x (square root: small sizes get room). */
static gfloat
lod_meter_x (gfloat size,
             gfloat x,
             gfloat width)
{
	return x + width * sqrtf (CLAMP (size, 0.0f, 1.0f));
}

static void
lod_levels_paint (LrgCustomWidget *widget,
                  LrgPaintList    *list,
                  gfloat           x,
                  gfloat           y,
                  gfloat           width,
                  gfloat           height,
                  gpointer         user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	LrgTheme                *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                   radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor                 track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	GrlColor                 accent = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	GrlColor                 text = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	GrlColor                 muted = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor                 sel = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
	GrlColor                 warn = *lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	gfloat                   size, mx;
	gint                     current, forced;
	guint                    n, i;
	g_autofree gchar        *caption = NULL;

	if (self->lod_group == NULL)
		return;
	n = lrg_lod_group_get_n_levels (self->lod_group);
	size = lod_screen_size (self);
	forced = lrg_lod_group_get_force_level (self->lod_group);
	current = lrg_lod_group_select_level (self->lod_group, size, LRG_LOD_LEVEL_NONE);

	/* The meter: track, threshold ticks, the node's size as a marker */
	caption = g_strdup_printf ("Screen size %.1f%%", (gdouble) size * 100.0);
	lrg_paint_list_text (list, font, caption, x, y, fs, 0.0f, &muted);
	lrg_paint_list_rect (list, x, y + 18.0f, width, 8.0f, 4.0f, &track);
	mx = lod_meter_x (size, x, width);
	{
		GrlColor fill = accent;

		fill.a = 0x70;
		lrg_paint_list_rect (list, x, y + 18.0f, MAX (mx - x, 8.0f), 8.0f, 4.0f, &fill);
	}
	for (i = 0; i < n; i++)
	{
		gfloat tx = lod_meter_x (lrg_lod_group_get_level_screen_size (self->lod_group, i), x, width);

		lrg_paint_list_line (list, tx, y + 15.0f, tx, y + 29.0f, 1.5f, &text);
	}
	lrg_paint_list_circle (list, mx, y + 22.0f, 6.0f, &accent);

	/* One row per level */
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + LOD_METER_H + (gfloat) i * LOD_ROW_H;
		gboolean          on = (gint) i == current;
		g_autofree gchar *row = g_strdup_printf ("LOD %u   %d tris   >= %.0f%%   err %.4f", i,
		                                         lrg_lod_group_get_level_triangle_count (self->lod_group, i),
		                                         (gdouble) lrg_lod_group_get_level_screen_size (self->lod_group, i) *
		                                         100.0,
		                                         (gdouble) lrg_lod_group_get_level_error (self->lod_group, i));

		if (on)
			lrg_paint_list_rect (list, x - 2.0f, ry, width + 4.0f, LOD_ROW_H - 2.0f, radius, &sel);
		lrg_paint_list_circle (list, x + 6.0f, ry + LOD_ROW_H * 0.5f - 1.0f, 3.5f,
		                       on ? (forced >= 0 ? &warn : &accent) : &track);
		lrg_paint_list_text (list, font, row, x + 16.0f, roundf (ry + (LOD_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     on ? &text : &muted);
	}
}

/* Force picker: item 0 is "Auto", item k is level k - 1. */
static void
on_lod_force_changed (LrgComboBox *combo,
                      gpointer     user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	gint                     i = lrg_combo_box_get_selected (combo);

	if (self->lod_syncing || self->lod_group == NULL || i < 0)
		return;
	lrg_lod_group_set_force_level (self->lod_group, i - 1);
	lod_queue_viewports (self);
}

/* Shows / hides the LOD section for what is inspected now. */
static void
update_lod (LrgEditorInspectorPanel *self)
{
	LrgNode     *node = (self->nodes->len == 1) ? g_ptr_array_index (self->nodes, 0) : NULL;
	LrgLodGroup *group = lod_group_for (self, node);
	guint        n, i;

	/* A preview force ends when its node is no longer inspected */
	if (self->lod_group != NULL && self->lod_group != group)
		lrg_lod_group_set_force_level (self->lod_group, -1);
	g_set_weak_pointer (&self->lod_group, group);
	g_set_weak_pointer (&self->lod_node, group != NULL ? node : NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->lod_section), group != NULL && self->live_object == NULL);
	if (group == NULL)
		return;

	n = lrg_lod_group_get_n_levels (group);
	self->lod_syncing = TRUE;
	if (n != self->lod_n)
	{
		lrg_combo_box_remove_all (self->lod_force);
		lrg_combo_box_append (self->lod_force, "Auto (screen size)");
		for (i = 0; i < n; i++)
		{
			g_autofree gchar *label = g_strdup_printf ("LOD %u", i);

			lrg_combo_box_append (self->lod_force, label);
		}
		self->lod_n = n;
	}
	lrg_combo_box_set_selected (self->lod_force, lrg_lod_group_get_force_level (group) + 1);
	self->lod_syncing = FALSE;
	lrg_widget_queue_layout (self->lod_levels);
}

static void
on_lod_camera_changed (LrgEditorViewState *view,
                       guint               index,
                       gpointer            user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);

	/* the meter follows the camera; the group itself appears once the
	 * viewport has loaded the model */
	if (self->lod_group == NULL && self->nodes->len == 1 && !self->refreshing)
		update_lod (self);
	else if (self->lod_group != NULL)
		lrg_widget_queue_layout (self->lod_levels);
}

static LrgExpander *
build_lod_section (LrgEditorInspectorPanel *self)
{
	static const LrgCustomWidgetFuncs funcs = { lod_levels_measure, lod_levels_paint, NULL, NULL, { NULL } };
	LrgVBox                          *box = lrg_vbox_new ();
	LrgHBox                          *row = lrg_hbox_new ();
	LrgLabel                         *label = lrg_label_new ("Force level");
	LrgExpander                      *section;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	self->lod_levels = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->lod_levels, "inspector-lod-levels");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->lod_levels), &funcs, self, NULL);
	add_owned (box, self->lod_levels);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	add_owned (row, label);
	self->lod_force = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->lod_force), "inspector-lod-force");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->lod_force), 1.0f);
	g_signal_connect (self->lod_force, "changed", G_CALLBACK (on_lod_force_changed), self);
	add_owned (row, self->lod_force);
	add_owned (box, row);
	section = make_section ("inspector-section-lod", "LOD", LRG_WIDGET (box));
	g_object_unref (box);
	lrg_widget_set_visible (LRG_WIDGET (section), FALSE);
	return section;
}

/* ==========================================================================
 * Physics summary (0.9.0 M5)
 *
 * For one inspected node (not while a live object is inspected): a short
 * summary of its `physics:` block ("Dynamic body, 20 kg, layer debris /
 * 2 shapes: box, sphere (trigger) / 1 joint: hinge") with "Edit in
 * Physics Panel", or "No physics body" with "Add Body". The full editor is
 * the Physics panel (lrg-editor-physics-panel.c); both buttons go through
 * its actions, so the inspector does not depend on the panel being
 * docked.
 * ========================================================================== */

static gchar *
physics_summary (LrgNode *node)
{
	LrgNodePhysics       *phys = lrg_node_get_physics (node);
	GString              *s;
	g_autoptr(GEnumClass) kinds = NULL;
	g_autoptr(GEnumClass) shape_kinds = NULL;
	g_autoptr(GEnumClass) joint_kinds = NULL;
	GPtrArray            *shapes, *joints;
	guint                 i;

	if (phys == NULL)
		return g_strdup ("No physics body");
	kinds = g_type_class_ref (LRG_TYPE_DYNAMICS_BODY_KIND);
	shape_kinds = g_type_class_ref (LRG_TYPE_COLLIDER3D_KIND);
	joint_kinds = g_type_class_ref (LRG_TYPE_JOINT_KIND);
	s = g_string_new (NULL);
	g_string_append_printf (s, "%s body", g_enum_get_value (kinds, (gint) lrg_node_physics_get_body_kind (phys))->value_nick);
	if (lrg_node_physics_get_body_kind (phys) == LRG_DYNAMICS_BODY_DYNAMIC)
		g_string_append_printf (s, ", %.3g kg", (gdouble) lrg_node_physics_get_mass (phys));
	if (lrg_node_physics_get_layer (phys) != NULL)
		g_string_append_printf (s, ", layer %s", lrg_node_physics_get_layer (phys));
	shapes = lrg_node_physics_get_shapes (phys);
	g_string_append_printf (s, "\n%u shape%s", shapes->len, shapes->len == 1 ? "" : "s");
	for (i = 0; i < shapes->len; i++)
	{
		LrgNodePhysicsShape *shape = g_ptr_array_index (shapes, i);

		g_string_append_printf (s, "%s%s%s", i == 0 ? ": " : ", ",
		                        g_enum_get_value (shape_kinds, (gint) lrg_node_physics_shape_get_kind (shape))->value_nick,
		                        lrg_node_physics_shape_get_trigger (shape) ? " (trigger)" : "");
	}
	joints = lrg_node_physics_get_joints (phys);
	if (joints->len > 0)
	{
		g_string_append_printf (s, "\n%u joint%s", joints->len, joints->len == 1 ? "" : "s");
		for (i = 0; i < joints->len; i++)
			g_string_append_printf (s, "%s%s", i == 0 ? ": " : ", ",
			                        g_enum_get_value (joint_kinds, (gint) lrg_node_physics_joint_get_kind (
			                                              g_ptr_array_index (joints, i)))->value_nick);
	}
	return g_string_free (s, FALSE);
}

/* Shows / updates the Physics section for what is inspected now. */
static void
update_physics (LrgEditorInspectorPanel *self)
{
	LrgNode          *node = (self->nodes->len == 1) ? g_ptr_array_index (self->nodes, 0) : NULL;
	g_autofree gchar *text = NULL;
	gboolean          show = node != NULL && self->live_object == NULL && !lrg_node_get_is_2d (node);

	lrg_widget_set_visible (LRG_WIDGET (self->phys_section), show);
	if (!show)
		return;
	text = physics_summary (node);
	lrg_label_set_text (self->phys_summary, text);
	lrg_button_set_text (self->phys_add, lrg_node_get_physics (node) != NULL ? "Edit in Physics Panel" : "Add Body");
}

static void
on_physics_button (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	LrgNode                 *node = (self->nodes->len == 1) ? g_ptr_array_index (self->nodes, 0) : NULL;
	LrgEditorActions        *actions;

	if (self->app == NULL || node == NULL)
		return;
	actions = lrg_editor_app_get_actions (self->app);
	if (lrg_node_get_physics (node) == NULL && lrg_editor_actions_has (actions, "physics.add-body"))
		lrg_editor_actions_activate (actions, "physics.add-body");
	if (lrg_editor_actions_has (actions, "physics.show"))
		lrg_editor_actions_activate (actions, "physics.show");
	update_physics (self);
}

static LrgExpander *
build_physics_section (LrgEditorInspectorPanel *self)
{
	LrgVBox     *box = lrg_vbox_new ();
	LrgExpander *section;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	self->phys_summary = add_owned (box, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->phys_summary), "inspector-physics-summary");
	lrg_widget_add_class (LRG_WIDGET (self->phys_summary), "dim");
	self->phys_add = add_owned (box, make_button ("Add Body", "inspector-physics-open", NULL,
	                                              G_CALLBACK (on_physics_button), self));
	lrg_widget_set_halign (LRG_WIDGET (self->phys_add), LRG_ALIGN_START);
	section = make_section ("inspector-section-physics", "Physics", LRG_WIDGET (box));
	g_object_unref (box);
	lrg_widget_set_visible (LRG_WIDGET (section), FALSE);
	return section;
}

/* ==========================================================================
 * Animator (0.9.0)
 *
 * For one inspected node with an `animator3d` component (or the live
 * entity of such a node while playing): the graph file with an Open
 * button, then a painted list of the graph's parameters (defaults from the
 * file, live values while playing: click a bool to toggle it, click a
 * trigger to fire it, drag a number sideways to change it), its layers
 * (current state, a weight bar you click to set the weight while playing)
 * and its IK goals (kind, chain, weight; the last solved target while
 * playing). The viewport draws the goals' targets (IK Targets overlay).
 * ========================================================================== */

#define ANIM_ROW_H (22.0f)

typedef enum
{
	ANIM_HIT_NONE,
	ANIM_HIT_PARAM,
	ANIM_HIT_LAYER
} AnimHitKind;

typedef struct
{
	AnimHitKind kind;
	gchar      *name;
	gfloat      x, y, w, h;
} AnimHit;

static void
anim_hit_free (gpointer data)
{
	AnimHit *h = data;

	g_free (h->name);
	g_free (h);
}

/* The animator3d description of @node, or NULL. */
static LrgComponentDesc *
animator_desc (LrgNode *node)
{
	GPtrArray *comps = node != NULL ? lrg_node_get_components (node) : NULL;
	guint      i;

	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (comps, i);
		const gchar      *type = lrg_component_desc_get_type_name (desc);
		GType             t = type != NULL ? g_type_from_name (type) : G_TYPE_INVALID;

		if (g_strcmp0 (type, "animator3d") == 0 ||
		    (t != G_TYPE_INVALID && g_type_is_a (t, LRG_TYPE_ANIMATOR_COMPONENT3D)))
			return desc;
	}
	return NULL;
}

static const gchar *
desc_graph_path (LrgComponentDesc *desc)
{
	static const gchar * const keys[] = { "graph-path", "graph_path" };
	guint                      i;

	for (i = 0; desc != NULL && i < G_N_ELEMENTS (keys); i++)
	{
		const GValue *v = lrg_component_desc_get_value (desc, keys[i]);

		if (v != NULL && G_VALUE_HOLDS_STRING (v))
			return g_value_get_string (v);
	}
	return NULL;
}

/* The live animator of what is inspected, or NULL. */
static LrgAnimatorComponent3D *
live_animator (LrgEditorInspectorPanel *self)
{
	GObject *live = self->live_object;

	if (live == NULL && self->nodes->len == 1 && self->play != NULL && lrg_editor_play_session_is_playing (self->play))
		live = lrg_editor_play_session_get_live_for_node (self->play, g_ptr_array_index (self->nodes, 0));
	if (live == NULL || !LRG_IS_ENTITY3D (live))
		return NULL;
	return (LrgAnimatorComponent3D *) lrg_entity3d_get_component (LRG_ENTITY3D (live), LRG_TYPE_ANIMATOR_COMPONENT3D);
}

/* The graph the section describes: the live one, else the file's. */
static LrgAnimGraph *
anim_section_graph (LrgEditorInspectorPanel *self)
{
	LrgAnimatorComponent3D *anim = live_animator (self);

	if (anim != NULL && lrg_animator_component3d_get_graph (anim) != NULL)
		return lrg_animator_component3d_get_graph (anim);
	return self->anim_file_graph;
}

static const gchar *
param_type_name (LrgAnimParameterType type)
{
	switch (type)
	{
	case LRG_ANIM_PARAMETER_FLOAT:   return "float";
	case LRG_ANIM_PARAMETER_INT:     return "int";
	case LRG_ANIM_PARAMETER_BOOL:    return "bool";
	case LRG_ANIM_PARAMETER_TRIGGER: return "trigger";
	default:                         return "?";
	}
}

/* The rows as text (painted, and returned by the public getter). */
static GPtrArray *
anim_rows (LrgEditorInspectorPanel *self)
{
	GPtrArray              *rows = g_ptr_array_new_with_free_func (g_free);
	LrgAnimGraph           *graph = anim_section_graph (self);
	LrgAnimatorComponent3D *anim = live_animator (self);
	guint                   i, n;

	if (graph == NULL)
	{
		g_ptr_array_add (rows, g_strdup (self->anim_error != NULL ? self->anim_error : "no graph"));
		return rows;
	}
	n = lrg_anim_graph_get_parameter_count (graph);
	for (i = 0; i < n; i++)
	{
		const gchar         *name = lrg_anim_graph_get_parameter_name (graph, i);
		LrgAnimParameterType type = lrg_anim_graph_get_parameter_type (graph, name);
		g_autofree gchar    *value = NULL;

		if (type == LRG_ANIM_PARAMETER_FLOAT)
			value = g_strdup_printf ("%.3g", (gdouble) lrg_anim_graph_get_float (graph, name));
		else if (type == LRG_ANIM_PARAMETER_INT)
			value = g_strdup_printf ("%d", lrg_anim_graph_get_int (graph, name));
		else
			value = g_strdup (lrg_anim_graph_get_bool (graph, name) ? "true" : "false");
		g_ptr_array_add (rows, g_strdup_printf ("param %s %s %s", name, param_type_name (type), value));
	}
	n = lrg_anim_graph_get_layer_count (graph);
	for (i = 0; i < n; i++)
	{
		LrgAnimationLayer        *layer = lrg_anim_graph_get_layer (graph, i);
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);
		const gchar              *state = anim != NULL && m != NULL ? lrg_animation_state_machine_get_current_state_name (m)
		                                  : m != NULL ? lrg_animation_state_machine_get_default_state (m) : NULL;

		g_ptr_array_add (rows, g_strdup_printf ("layer %s %.2f %s", lrg_animation_layer_get_name (layer),
		                                        (gdouble) lrg_animation_layer_get_weight (layer),
		                                        state != NULL ? state : "-"));
	}
	n = lrg_anim_graph_get_ik_goal_count (graph);
	for (i = 0; i < n; i++)
	{
		LrgIKGoal        *goal = lrg_anim_graph_get_ik_goal (graph, i);
		g_auto(GStrv)     bones = lrg_ik_goal_get_bones (goal);
		g_autofree gchar *chain = bones != NULL ? g_strjoinv (">", bones) : g_strdup ("");
		GEnumClass       *klass = g_type_class_ref (LRG_TYPE_IK_GOAL_KIND);
		GEnumValue       *ev = g_enum_get_value (klass, (gint) lrg_ik_goal_get_kind (goal));
		gfloat            t[3];
		g_autofree gchar *target = (anim != NULL && lrg_ik_goal_get_last_target (goal, t))
		                           ? g_strdup_printf (" @ %.2f %.2f %.2f", t[0], t[1], t[2]) : g_strdup ("");

		g_ptr_array_add (rows, g_strdup_printf ("ik %s %s %s %.2f%s%s", lrg_ik_goal_get_name (goal),
		                                        ev != NULL ? ev->value_nick : "?", chain,
		                                        (gdouble) lrg_ik_goal_get_weight (goal),
		                                        lrg_ik_goal_get_enabled (goal) ? "" : " off", target));
		g_type_class_unref (klass);
	}
	return rows;
}

static void
anim_list_measure (LrgCustomWidget     *widget,
                   gfloat               avail_width,
                   gfloat               avail_height,
                   LrgCustomWidgetSize *size,
                   gpointer             user_data)
{
	LrgEditorInspectorPanel *self = user_data;
	LrgAnimGraph            *graph = anim_section_graph (self);
	guint                    rows = 1;

	if (graph != NULL)
		rows = 3 + lrg_anim_graph_get_parameter_count (graph) + lrg_anim_graph_get_layer_count (graph) +
		       lrg_anim_graph_get_ik_goal_count (graph);
	size->width = MAX (avail_width, 160.0f);
	size->height = (gfloat) rows * ANIM_ROW_H + 4.0f;
}

static void
anim_heading (LrgPaintList *list,
              LrgTheme     *theme,
              const gchar  *text,
              gfloat        x,
              gfloat       *y)
{
	gfloat fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);

	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, x, *y + 4.0f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	*y += ANIM_ROW_H;
}

static void
anim_list_paint (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	LrgEditorInspectorPanel *self = user_data;
	LrgTheme                *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	const GrlColor          *ink = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor          *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor          *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor          *success = lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS);
	LrgAnimGraph            *graph = anim_section_graph (self);
	gboolean                 live = live_animator (self) != NULL;
	gfloat                   cy = y + 2.0f;
	gfloat                   vx = x + width * 0.45f;
	GrlColor                 track;
	guint                    i, n;

	g_ptr_array_set_size (self->anim_hits, 0);
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK), 0.8f, &track);
	if (graph == NULL)
	{
		lrg_paint_list_text (list, font, self->anim_error != NULL ? self->anim_error : "No graph: set graph-path",
		                     x + 4.0f, cy + 3.0f, fs, 0.0f, muted);
		return;
	}

	/* --- parameters --- */
	anim_heading (list, theme, live ? "PARAMETERS  (live)" : "PARAMETERS  (defaults)", x + 4.0f, &cy);
	n = lrg_anim_graph_get_parameter_count (graph);
	for (i = 0; i < n; i++)
	{
		const gchar          *name = lrg_anim_graph_get_parameter_name (graph, i);
		LrgAnimParameterType  type = lrg_anim_graph_get_parameter_type (graph, name);
		g_autofree gchar     *value = NULL;
		AnimHit              *hit;

		lrg_paint_list_text (list, font, name, x + 12.0f, cy + 3.0f, fs, 0.0f, ink);
		if (type == LRG_ANIM_PARAMETER_BOOL || type == LRG_ANIM_PARAMETER_TRIGGER)
		{
			gboolean on = lrg_anim_graph_get_bool (graph, name);

			/* a pill: filled when true (a raised trigger) */
			lrg_paint_list_rect (list, vx, cy + 4.0f, 30.0f, ANIM_ROW_H - 8.0f, (ANIM_ROW_H - 8.0f) * 0.5f,
			                     on ? success : &track);
			lrg_paint_list_text (list, font, type == LRG_ANIM_PARAMETER_TRIGGER ? "fire" : (on ? "on" : "off"),
			                     vx + 38.0f, cy + 3.0f, fs, 0.0f, muted);
		}
		else
		{
			value = type == LRG_ANIM_PARAMETER_FLOAT
			        ? g_strdup_printf ("%.3f", (gdouble) lrg_anim_graph_get_float (graph, name))
			        : g_strdup_printf ("%d", lrg_anim_graph_get_int (graph, name));
			lrg_paint_list_rect (list, vx, cy + 2.0f, x + width - vx - 8.0f, ANIM_ROW_H - 4.0f, 4.0f, &track);
			lrg_paint_list_text (list, font, value, vx + 6.0f, cy + 3.0f, fs, 0.0f, live ? accent : ink);
		}
		hit = g_new0 (AnimHit, 1);
		hit->kind = ANIM_HIT_PARAM;
		hit->name = g_strdup (name);
		hit->x = vx;
		hit->y = cy;
		hit->w = x + width - vx;
		hit->h = ANIM_ROW_H;
		g_ptr_array_add (self->anim_hits, hit);
		cy += ANIM_ROW_H;
	}

	/* --- layers --- */
	anim_heading (list, theme, "LAYERS", x + 4.0f, &cy);
	n = lrg_anim_graph_get_layer_count (graph);
	for (i = 0; i < n; i++)
	{
		LrgAnimationLayer        *layer = lrg_anim_graph_get_layer (graph, i);
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);
		const gchar              *state = m != NULL ? (live ? lrg_animation_state_machine_get_current_state_name (m)
		                                                    : lrg_animation_state_machine_get_default_state (m))
		                                            : NULL;
		g_autofree gchar         *label = g_strdup_printf ("%s  \xc2\xb7  %s", lrg_animation_layer_get_name (layer),
		                                                   state != NULL ? state : "-");
		gfloat                    bw = x + width - vx - 8.0f;
		AnimHit                  *hit;

		lrg_paint_list_text (list, font, label, x + 12.0f, cy + 3.0f, fs, 0.0f, ink);
		lrg_paint_list_rect (list, vx, cy + 7.0f, bw, ANIM_ROW_H - 14.0f, 3.0f, &track);
		lrg_paint_list_rect (list, vx, cy + 7.0f, bw * CLAMP (lrg_animation_layer_get_weight (layer), 0.0f, 1.0f),
		                     ANIM_ROW_H - 14.0f, 3.0f, accent);
		hit = g_new0 (AnimHit, 1);
		hit->kind = ANIM_HIT_LAYER;
		hit->name = g_strdup (lrg_animation_layer_get_name (layer));
		hit->x = vx;
		hit->y = cy;
		hit->w = bw;
		hit->h = ANIM_ROW_H;
		g_ptr_array_add (self->anim_hits, hit);
		cy += ANIM_ROW_H;
	}

	/* --- IK goals --- */
	anim_heading (list, theme, "IK GOALS", x + 4.0f, &cy);
	n = lrg_anim_graph_get_ik_goal_count (graph);
	if (n == 0)
		lrg_paint_list_text (list, font, "none", x + 12.0f, cy + 3.0f, fs, 0.0f, muted);
	for (i = 0; i < n; i++)
	{
		LrgIKGoal        *goal = lrg_anim_graph_get_ik_goal (graph, i);
		g_auto(GStrv)     bones = lrg_ik_goal_get_bones (goal);
		g_autofree gchar *chain = bones != NULL ? g_strjoinv (" \xe2\x80\xba ", bones) : g_strdup ("");
		g_autofree gchar *label = g_strdup_printf ("%s  %s", lrg_ik_goal_get_name (goal), chain);
		gfloat            t[3];
		g_autofree gchar *right = (live && lrg_ik_goal_get_last_target (goal, t))
		                          ? g_strdup_printf ("\xe2\x97\x8e %.2f %.2f %.2f", t[0], t[1], t[2])
		                          : g_strdup_printf ("w %.2f", (gdouble) lrg_ik_goal_get_weight (goal));

		lrg_paint_list_push_clip (list, x, cy, vx - x - 4.0f, ANIM_ROW_H);
		lrg_paint_list_text (list, font, label, x + 12.0f, cy + 3.0f, fs, 0.0f,
		                     lrg_ik_goal_get_enabled (goal) ? ink : muted);
		lrg_paint_list_pop_clip (list);
		lrg_paint_list_text (list, font, right, vx, cy + 3.0f, fs, 0.0f, live ? accent : muted);
		cy += ANIM_ROW_H;
	}
}

/* Keeps redrawing while the play world runs (live values). */
static gboolean
anim_list_step (LrgCustomWidget *widget,
                gfloat           delta_time,
                gpointer         user_data)
{
	LrgEditorInspectorPanel *self = user_data;

	return lrg_widget_get_visible (LRG_WIDGET (self->anim_section)) && live_animator (self) != NULL;
}

static AnimHit *
anim_hit_at (LrgEditorInspectorPanel *self,
             gfloat                   x,
             gfloat                   y)
{
	guint i;

	for (i = 0; i < self->anim_hits->len; i++)
	{
		AnimHit *h = g_ptr_array_index (self->anim_hits, i);

		if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h)
			return h;
	}
	return NULL;
}

/* Live edits: bool toggle, trigger fire, number drag, layer weight. */
static gboolean
anim_list_event (LrgCustomWidget  *widget,
                 const LrgUIEvent *event,
                 gpointer          user_data)
{
	LrgEditorInspectorPanel *self = user_data;
	LrgAnimatorComponent3D  *anim = live_animator (self);
	LrgAnimGraph            *graph = anim != NULL ? lrg_animator_component3d_get_graph (anim) : NULL;
	LrgUIEventType           type = lrg_ui_event_get_event_type (event);
	gfloat                   x = lrg_ui_event_get_x (event);
	gfloat                   y = lrg_ui_event_get_y (event);
	AnimHit                 *hit;

	if (graph == NULL)
		return FALSE;
	if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP)
	{
		g_clear_pointer (&self->anim_drag, g_free);
		return FALSE;
	}
	if (type == LRG_UI_EVENT_MOUSE_MOVE && self->anim_drag != NULL)
	{
		/* numbers follow the pointer: 0.01 per canvas unit */
		gfloat d = (x - self->anim_drag_x) * 0.01f;

		if (lrg_anim_graph_get_parameter_type (graph, self->anim_drag) == LRG_ANIM_PARAMETER_INT)
			lrg_animator_component3d_set_int (anim, self->anim_drag, (gint) rint (self->anim_drag_value + d * 10.0f));
		else
			lrg_animator_component3d_set_float (anim, self->anim_drag, self->anim_drag_value + d);
		return TRUE;
	}
	if (type != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || (hit = anim_hit_at (self, x, y)) == NULL)
		return FALSE;
	if (hit->kind == ANIM_HIT_LAYER)
	{
		lrg_animator_component3d_set_layer_weight (anim, hit->name, CLAMP ((x - hit->x) / MAX (hit->w, 1.0f), 0.0f, 1.0f));
		return TRUE;
	}
	switch (lrg_anim_graph_get_parameter_type (graph, hit->name))
	{
	case LRG_ANIM_PARAMETER_BOOL:
		lrg_animator_component3d_set_bool (anim, hit->name, !lrg_anim_graph_get_bool (graph, hit->name));
		break;
	case LRG_ANIM_PARAMETER_TRIGGER:
		lrg_animator_component3d_set_trigger (anim, hit->name);
		break;
	case LRG_ANIM_PARAMETER_INT:
		self->anim_drag = g_strdup (hit->name);
		self->anim_drag_x = x;
		self->anim_drag_value = (gfloat) lrg_anim_graph_get_int (graph, hit->name);
		break;
	default:
		self->anim_drag = g_strdup (hit->name);
		self->anim_drag_x = x;
		self->anim_drag_value = lrg_anim_graph_get_float (graph, hit->name);
		break;
	}
	return TRUE;
}

static void
on_anim_open_clicked (LrgButton *button,
                      gpointer   user_data)
{
	LrgEditorInspectorPanel *self = LRG_EDITOR_INSPECTOR_PANEL (user_data);
	g_autoptr(GError)        error = NULL;

	if (self->app == NULL || self->anim_path == NULL)
		return;
	if (lrg_editor_anim_graph_editor_open (self->app, self->anim_path, &error) == NULL)
		status (self, error->message);
}

/* Shows / hides and refills the animator section. */
static void
update_animator (LrgEditorInspectorPanel *self)
{
	LrgNode          *node = (self->nodes->len == 1) ? g_ptr_array_index (self->nodes, 0) : NULL;
	LrgComponentDesc *desc = animator_desc (node);
	const gchar      *rel = desc_graph_path (desc);
	g_autofree gchar *abs = NULL;
	gboolean          show;

	if (rel != NULL && self->app != NULL)
		abs = g_path_is_absolute (rel) ? g_strdup (rel) : editor_app_project_path (self->app, rel);
	show = desc != NULL || (self->live_object != NULL && live_animator (self) != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->anim_section), show);
	if (!show)
		return;
	if (g_strcmp0 (abs, self->anim_path) != 0)
	{
		g_autoptr(GError) error = NULL;

		g_free (self->anim_path);
		self->anim_path = g_steal_pointer (&abs);
		g_clear_object (&self->anim_file_graph);
		g_clear_pointer (&self->anim_error, g_free);
		if (self->anim_path != NULL)
		{
			self->anim_file_graph = lrg_anim_graph_new_from_file (self->anim_path, &error);
			if (self->anim_file_graph == NULL)
				self->anim_error = g_strdup (error->message);
		}
	}
	{
		g_autofree gchar *text = g_strdup_printf ("Graph  %s", rel != NULL ? rel : "(none: set graph-path)");

		lrg_label_set_text (self->anim_label, text);
	}
	lrg_widget_set_enabled (LRG_WIDGET (self->anim_open), self->anim_path != NULL);
	lrg_widget_queue_layout (self->anim_list);
}

static LrgExpander *
build_anim_section (LrgEditorInspectorPanel *self)
{
	static const LrgCustomWidgetFuncs funcs = { anim_list_measure, anim_list_paint, anim_list_step, anim_list_event,
	                                            { NULL } };
	LrgVBox                          *box = lrg_vbox_new ();
	LrgHBox                          *row = lrg_hbox_new ();
	LrgExpander                      *section;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->anim_label = add_owned (row, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->anim_label), "inspector-animator-graph");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->anim_label), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (self->anim_label), LRG_ALIGN_CENTER);
	self->anim_open = add_owned (row, make_button ("Open Graph", "inspector-animator-open", NULL,
	                                               G_CALLBACK (on_anim_open_clicked), self));
	add_owned (box, row);
	self->anim_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->anim_list, "inspector-animator-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->anim_list), &funcs, self, NULL);
	add_owned (box, self->anim_list);
	section = make_section ("inspector-section-animator", "Animator", LRG_WIDGET (box));
	g_object_unref (box);
	lrg_widget_set_visible (LRG_WIDGET (section), FALSE);
	self->anim_hits = g_ptr_array_new_with_free_func (anim_hit_free);
	return section;
}

GStrv
lrg_editor_inspector_panel_get_animator_rows (LrgEditorInspectorPanel *self)
{
	g_autoptr(GPtrArray) rows = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_INSPECTOR_PANEL (self), NULL);
	if (!lrg_widget_get_visible (LRG_WIDGET (self->anim_section)))
		return g_new0 (gchar *, 1);
	rows = anim_rows (self);
	g_ptr_array_add (rows, NULL);
	return (GStrv) g_ptr_array_steal (rows, NULL);
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
	update_lod (self);
	update_physics (self);
	update_animator (self);

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
	update_physics (self);

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

		if (sec->target != NULL)
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

	if (self->anim_list != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->anim_list), NULL, NULL, NULL);

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
	if (self->lod_group != NULL)
		lrg_lod_group_set_force_level (self->lod_group, -1);
	g_clear_weak_pointer (&self->lod_group);
	g_clear_weak_pointer (&self->lod_node);
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
	g_clear_object (&self->anim_file_graph);
	g_clear_pointer (&self->anim_hits, g_ptr_array_unref);
	g_free (self->anim_path);
	g_free (self->anim_error);
	g_free (self->anim_drag);
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
	self->prefab_variant = add_owned (row2, make_button ("Create Variant", "inspector-prefab-variant", NULL,
	                                                     G_CALLBACK (on_prefab_variant_clicked), self));
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_select), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_unlink), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_relink), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_apply), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_revert), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->prefab_variant), 1.0f);
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

	/* Physics summary, linking to the Physics panel */
	self->phys_section = add_owned (self->body, build_physics_section (self));

	/* LOD preview, shown for mesh assets with levels */
	self->lod_section = add_owned (self->body, build_lod_section (self));

	/* Animator: parameters, layers and IK goals of an animator3d node */
	self->anim_section = add_owned (self->body, build_anim_section (self));

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

/* --- lod.* (0.9.0) --- */

/* The node of params.node (guid or name path), selected and inspected so
 * the panel's LOD section follows it. */
static LrgEditorInspectorPanel *
lod_target (LrgEditorApp  *app,
            JsonObject    *params,
            LrgNode      **out_node,
            GError       **error)
{
	LrgEditorInspectorPanel *self = inspector_of (app);
	const gchar             *ref = (params != NULL && json_object_has_member (params, "node"))
	                               ? json_object_get_string_member (params, "node") : NULL;
	LrgNode                 *node;

	if (self == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No inspector panel");
		return NULL;
	}
	if (ref == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing \"node\"");
		return NULL;
	}
	node = lrg_editor_find_node (lrg_editor_app_get_editor (app), ref);
	if (node == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'", ref);
		return NULL;
	}
	if (self->nodes->len != 1 || g_ptr_array_index (self->nodes, 0) != node)
		lrg_editor_select (lrg_editor_app_get_editor (app), node, FALSE);
	update_lod (self);
	*out_node = node;
	return self;
}

static JsonNode *
lod_json (LrgEditorInspectorPanel *self,
          LrgNode                 *node)
{
	JsonObject    *o = json_object_new ();
	JsonArray     *levels = json_array_new ();
	JsonNode      *n = json_node_new (JSON_NODE_OBJECT);
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	guint          i;

	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	if (visual != NULL && lrg_node_visual_get_asset (visual) != NULL)
		json_object_set_string_member (o, "asset", lrg_node_visual_get_asset (visual));
	else
		json_object_set_null_member (o, "asset");
	json_object_set_boolean_member (o, "available", self->lod_group != NULL);
	if (self->lod_group != NULL)
	{
		gfloat size = lod_screen_size (self);

		for (i = 0; i < lrg_lod_group_get_n_levels (self->lod_group); i++)
		{
			JsonObject *l = json_object_new ();

			json_object_set_int_member (l, "level", i);
			json_object_set_int_member (l, "triangles", lrg_lod_group_get_level_triangle_count (self->lod_group, i));
			json_object_set_double_member (l, "screen_size", lrg_lod_group_get_level_screen_size (self->lod_group, i));
			json_object_set_double_member (l, "error", lrg_lod_group_get_level_error (self->lod_group, i));
			json_array_add_object_element (levels, l);
		}
		json_object_set_double_member (o, "screen_size", size);
		json_object_set_int_member (o, "current",
		                            lrg_lod_group_select_level (self->lod_group, size, LRG_LOD_LEVEL_NONE));
		json_object_set_int_member (o, "forced", lrg_lod_group_get_force_level (self->lod_group));
	}
	else
	{
		json_object_set_null_member (o, "current");
		json_object_set_int_member (o, "forced", -1);
	}
	json_object_set_array_member (o, "levels", levels);
	json_node_take_object (n, o);
	return n;
}

/* {"node": guid} -> levels, current, forced, screen_size */
static JsonNode *
m_lod_get (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgNode                 *node = NULL;
	LrgEditorInspectorPanel *self = lod_target (LRG_EDITOR_APP (user_data), params, &node, error);

	return (self != NULL) ? lod_json (self, node) : NULL;
}

/* {"node": guid, "level": 1} (-1 = automatic) */
static JsonNode *
m_lod_force (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgNode                 *node = NULL;
	LrgEditorInspectorPanel *self = lod_target (LRG_EDITOR_APP (user_data), params, &node, error);
	gint64                   level;

	if (self == NULL)
		return NULL;
	if (!json_object_has_member (params, "level"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing \"level\"");
		return NULL;
	}
	if (self->lod_group == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "The node has no LOD levels here (a mesh asset with lod.levels > 1, drawn by a viewport)");
		return NULL;
	}
	level = json_object_get_int_member (params, "level");
	if (level < -1 || level >= (gint64) lrg_lod_group_get_n_levels (self->lod_group))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Level %" G_GINT64_FORMAT " out of range (-1 .. %u)", level,
		             lrg_lod_group_get_n_levels (self->lod_group) - 1);
		return NULL;
	}
	lrg_lod_group_set_force_level (self->lod_group, (gint) level);
	lod_queue_viewports (self);
	update_lod (self);
	return lod_json (self, node);
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
	lrg_editor_server_register_method (server, "lod.get", m_lod_get, app, NULL);
	lrg_editor_server_register_method (server, "lod.force", m_lod_force, app, NULL);
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

/* inspector.show (0.9.0): brings the inspector tab to the front (it
 * shares its dock stack with the environment and tiles panels) */
static void
a_show_inspector (gpointer data)
{
	editor_app_show_panel (LRG_EDITOR_APP (data), "inspector");
}

static void
register_actions (LrgEditorApp *app)
{
	LrgEditorActions *actions = lrg_editor_app_get_actions (app);

	lrg_editor_actions_add (actions, "inspector.toggle-lock", "Lock Inspector", "View", NULL, a_toggle_lock, NULL,
	                        a_lock_checked, app);
	lrg_editor_actions_add (actions, "node.add-component", "Add Component…", "Node", NULL, a_add_component,
	                        a_has_selection, NULL, app);
	lrg_editor_actions_add (actions, "inspector.show", "Inspector Panel", "View", NULL, a_show_inspector, NULL, NULL,
	                        app);
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
	g_signal_connect (self->view, "camera-changed", G_CALLBACK (on_lod_camera_changed), self);
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
	g_return_if_fail (object == NULL || LRG_IS_GAME_OBJECT (object) || LRG_IS_ENTITY3D (object));

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
