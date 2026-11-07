/* lrg-editor-weather-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWeatherEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-weather-editor.h"

struct _LrgEditorWeatherEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorWeatherEditor, lrg_editor_weather_editor, G_TYPE_OBJECT)

static void
lrg_editor_weather_editor_class_init (LrgEditorWeatherEditorClass *klass)
{
}

static void
lrg_editor_weather_editor_init (LrgEditorWeatherEditor *self)
{
}

LrgEditorWeatherEditor *
lrg_editor_weather_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_WEATHER_EDITOR, NULL);
}
