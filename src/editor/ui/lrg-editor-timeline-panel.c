/* lrg-editor-timeline-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTimelinePanel: placeholder until Wave 4 fills it in. It holds a weak
 * pointer to the app and one centred, muted label that fills the panel.
 */

#include "config.h"

#include "lrg-editor-timeline-panel.h"
#include "../../ui/lrg-label.h"

struct _LrgEditorTimelinePanel
{
	LrgContainer  parent_instance;

	LrgEditorApp *app;    /* weak */
	LrgWidget    *label;  /* borrowed: owned by the container */
};

G_DEFINE_FINAL_TYPE (LrgEditorTimelinePanel, lrg_editor_timeline_panel, LRG_TYPE_CONTAINER)

/* The label fills the panel; it centres its own text. */
static void
lrg_editor_timeline_panel_layout_children (LrgContainer *container)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (container);

	lrg_widget_place_in_slot (self->label, 0.0f, 0.0f,
	                          lrg_widget_get_width (LRG_WIDGET (self)),
	                          lrg_widget_get_height (LRG_WIDGET (self)));
}

static void
lrg_editor_timeline_panel_dispose (GObject *object)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (object);

	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_timeline_panel_parent_class)->dispose (object);
}

static void
lrg_editor_timeline_panel_class_init (LrgEditorTimelinePanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_timeline_panel_dispose;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_timeline_panel_layout_children;
}

static void
lrg_editor_timeline_panel_init (LrgEditorTimelinePanel *self)
{
	g_autoptr(LrgLabel) label = lrg_label_new ("Timeline (Wave 4)");

	lrg_widget_set_name (LRG_WIDGET (label), "placeholder");
	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_label_set_alignment (label, LRG_TEXT_ALIGN_CENTER);
	self->label = LRG_WIDGET (label);
	lrg_container_add_child (LRG_CONTAINER (self), self->label);
}

LrgEditorTimelinePanel *
lrg_editor_timeline_panel_new (LrgEditorApp *app)
{
	LrgEditorTimelinePanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TIMELINE_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	return self;
}

LrgEditorApp *
lrg_editor_timeline_panel_get_app (LrgEditorTimelinePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self), NULL);
	return self->app;
}
