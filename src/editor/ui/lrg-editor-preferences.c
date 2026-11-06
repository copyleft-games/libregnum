/* lrg-editor-preferences.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPreferences: placeholder until Wave 4 fills it in. It holds a weak
 * pointer to the app and one centred, muted label that fills the panel.
 */

#include "config.h"

#include "lrg-editor-preferences.h"
#include "../../ui/lrg-label.h"

struct _LrgEditorPreferences
{
	LrgContainer  parent_instance;

	LrgEditorApp *app;    /* weak */
	LrgWidget    *label;  /* borrowed: owned by the container */
};

G_DEFINE_FINAL_TYPE (LrgEditorPreferences, lrg_editor_preferences, LRG_TYPE_CONTAINER)

/* The label fills the panel; it centres its own text. */
static void
lrg_editor_preferences_layout_children (LrgContainer *container)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (container);

	lrg_widget_place_in_slot (self->label, 0.0f, 0.0f,
	                          lrg_widget_get_width (LRG_WIDGET (self)),
	                          lrg_widget_get_height (LRG_WIDGET (self)));
}

static void
lrg_editor_preferences_dispose (GObject *object)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (object);

	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_preferences_parent_class)->dispose (object);
}

static void
lrg_editor_preferences_class_init (LrgEditorPreferencesClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_preferences_dispose;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_preferences_layout_children;
}

static void
lrg_editor_preferences_init (LrgEditorPreferences *self)
{
	g_autoptr(LrgLabel) label = lrg_label_new ("Preferences (Wave 4)");

	lrg_widget_set_name (LRG_WIDGET (label), "placeholder");
	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_label_set_alignment (label, LRG_TEXT_ALIGN_CENTER);
	self->label = LRG_WIDGET (label);
	lrg_container_add_child (LRG_CONTAINER (self), self->label);
}

LrgEditorPreferences *
lrg_editor_preferences_new (LrgEditorApp *app)
{
	LrgEditorPreferences *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PREFERENCES, NULL);
	g_set_weak_pointer (&self->app, app);
	return self;
}

LrgEditorApp *
lrg_editor_preferences_get_app (LrgEditorPreferences *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), NULL);
	return self->app;
}
