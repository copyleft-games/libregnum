/* lrg-editor-weather-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWeatherEditor: the data editor of `.rweather` weather files
 * (#LrgWeatherAsset), with a live preview scene.
 *
 * The editor shows:
 *  - the file settings (name, default preset, default transition);
 *  - the preset list (add, remove; name and ambient light in the grid);
 *  - the selected preset's layer stack (add a layer of any registered
 *    kind, remove, reorder, enable, intensity slider);
 *  - the selected layer's parameters in a property grid generated from
 *    lrg_weather_asset_list_kind_params() (spin boxes with the spec
 *    ranges, enum combos, colour pickers, gradient editors); a reset
 *    button unsets a parameter (the effect default applies again);
 *  - the transition rules (from, to, duration; `*` and `none` allowed);
 *  - a live preview scene: an #LrgWeatherManager playing the asset, the
 *    previewed preset kept in sync after every edit
 *    (lrg_weather_asset_sync_weather()), and "Transition to" crossfading
 *    to another preset with the duration the rules give.
 *
 * Every edit is one undoable step of the `.rweather` #LrgEditorDocument,
 * written in the engine writer's canonical form.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../../lrg-version.h"
#include "../../../weather/lrg-weather-asset.h"
#include "../../../weather/lrg-weather-manager.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_WEATHER_EDITOR (lrg_editor_weather_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorWeatherEditor, lrg_editor_weather_editor, LRG,
                      EDITOR_WEATHER_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_weather_editor_new:
 * @app: (nullable): the editor application
 * @document: a weather document (`.rweather`)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_weather_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorWeatherEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorWeatherEditor * lrg_editor_weather_editor_new (LrgEditorApp      *app,
                                                        LrgEditorDocument *document);

/**
 * lrg_editor_weather_editor_install:
 * @app: the editor application
 *
 * Registers the `weather.*` IPC methods on @app. Safe to call more than
 * once; every weather editor calls it when it is constructed and the
 * assets panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_weather_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_weather_editor_open:
 * @app: the editor application
 * @path: a `.rweather` file, absolute or relative to the project directory
 * @error: return location for a #GError
 *
 * Opens @path in a weather editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorWeatherEditor * lrg_editor_weather_editor_open (LrgEditorApp  *app,
                                                         const gchar   *path,
                                                         GError       **error);

/**
 * lrg_editor_weather_editor_create:
 * @app: (nullable): the editor application (resolves relative paths
 *   against the project)
 * @path: the new `.rweather` file
 * @template_name: (nullable): "default" (or %NULL: a "clear" preset with a
 *   light wind and a "storm" preset with rain, fog, lightning and gusts,
 *   plus crossfade rules) or "empty" (no presets)
 * @error: return location for a #GError
 *
 * Writes a new weather file named after the file.
 *
 * Returns: %TRUE on success; %G_IO_ERROR_EXISTS when @path exists,
 *   %G_IO_ERROR_INVALID_ARGUMENT for an unknown template
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_create (LrgEditorApp  *app,
                                           const gchar   *path,
                                           const gchar   *template_name,
                                           GError       **error);

/**
 * lrg_editor_weather_editor_get_asset:
 * @self: an #LrgEditorWeatherEditor
 *
 * The weather parsed from the document. It is replaced when the document
 * changes from outside the editor (undo, the YAML pane, a reload), so do
 * not keep it.
 *
 * Returns: (transfer none) (nullable): the asset, %NULL while the document
 *   does not load (see lrg_editor_weather_editor_get_load_error())
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherAsset * lrg_editor_weather_editor_get_asset (LrgEditorWeatherEditor *self);

/**
 * lrg_editor_weather_editor_get_load_error:
 * @self: an #LrgEditorWeatherEditor
 *
 * Returns: (nullable): why the document does not load (the strict
 *   loader's message, starting with the key path), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_weather_editor_get_load_error (LrgEditorWeatherEditor *self);

/**
 * lrg_editor_weather_editor_get_manager:
 * @self: an #LrgEditorWeatherEditor
 *
 * Returns: (transfer none) (nullable): the preview's weather manager
 *   (every preset registered), %NULL while the document does not load
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherManager * lrg_editor_weather_editor_get_manager (LrgEditorWeatherEditor *self);

/* --- selection --- */

/**
 * lrg_editor_weather_editor_select:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index, or -1 for the file settings
 * @layer: a layer index of that preset, or -1 for the preset itself
 *
 * Shows the selection in the property grid and the layer stack. Selecting
 * a preset also previews it.
 *
 * Returns: %FALSE when an index is out of range (nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_select (LrgEditorWeatherEditor *self,
                                           gint                    preset,
                                           gint                    layer);

/**
 * lrg_editor_weather_editor_get_selected_preset:
 * @self: an #LrgEditorWeatherEditor
 *
 * Returns: the selected preset index, or -1 (the file settings)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_weather_editor_get_selected_preset (LrgEditorWeatherEditor *self);

/**
 * lrg_editor_weather_editor_get_selected_layer:
 * @self: an #LrgEditorWeatherEditor
 *
 * Returns: the selected layer index, or -1 (the preset)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_weather_editor_get_selected_layer (LrgEditorWeatherEditor *self);

/* --- edits (each one undo step) --- */

/**
 * lrg_editor_weather_editor_set_setting:
 * @self: an #LrgEditorWeatherEditor
 * @key: `name`, `default-preset` or `default-transition`
 * @value: (nullable): the value in the file syntax; %NULL clears `name` or
 *   `default-preset` and resets `default-transition`
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_set_setting (LrgEditorWeatherEditor  *self,
                                                const gchar             *key,
                                                const gchar             *value,
                                                GError                 **error);

/**
 * lrg_editor_weather_editor_add_preset:
 * @self: an #LrgEditorWeatherEditor
 * @id: (nullable): the preset id; %NULL picks "preset", numbered when
 *   taken
 * @name: (nullable): its display name
 * @error: return location for a #GError
 *
 * Appends an empty preset and selects it.
 *
 * Returns: the new preset index, or -1 with @error set (a taken or
 *   reserved id)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_weather_editor_add_preset (LrgEditorWeatherEditor  *self,
                                           const gchar             *id,
                                           const gchar             *name,
                                           GError                 **error);

/**
 * lrg_editor_weather_editor_remove_preset:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @error: return location for a #GError
 *
 * Removes a preset, the transition rules naming it and, when it was the
 * default preset, the default.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_remove_preset (LrgEditorWeatherEditor  *self,
                                                  guint                    preset,
                                                  GError                 **error);

/**
 * lrg_editor_weather_editor_set_preset:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @name: (nullable): the new display name, or %NULL to keep it
 * @ambient: (nullable): the ambient tint, or %NULL to keep it
 * @brightness: the ambient brightness 0 .. 2, or a negative value to keep
 *   it
 * @gesture: 0, or a gesture id merging consecutive edits into one step
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_set_preset (LrgEditorWeatherEditor  *self,
                                               guint                    preset,
                                               const gchar             *name,
                                               const GrlColor          *ambient,
                                               gfloat                   brightness,
                                               guint                    gesture,
                                               GError                 **error);

/**
 * lrg_editor_weather_editor_add_layer:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @kind: a layer kind (lrg_weather_asset_list_kinds())
 * @id: (nullable): the layer id; %NULL uses the kind, numbered when taken
 * @error: return location for a #GError
 *
 * Appends a layer with the kind's defaults and selects it.
 *
 * Returns: the new layer index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_weather_editor_add_layer (LrgEditorWeatherEditor  *self,
                                          guint                    preset,
                                          const gchar             *kind,
                                          const gchar             *id,
                                          GError                 **error);

/**
 * lrg_editor_weather_editor_remove_layer:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @layer: a layer index
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_remove_layer (LrgEditorWeatherEditor  *self,
                                                 guint                    preset,
                                                 guint                    layer,
                                                 GError                 **error);

/**
 * lrg_editor_weather_editor_move_layer:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @from: a layer index
 * @to: its index after the move (effect order inside a render layer)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_move_layer (LrgEditorWeatherEditor  *self,
                                               guint                    preset,
                                               guint                    from,
                                               guint                    to,
                                               GError                 **error);

/**
 * lrg_editor_weather_editor_set_layer:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @layer: a layer index
 * @intensity: the layer level 0 .. 1, or a negative value to keep it
 * @enabled: 1 or 0 to enable or disable the layer, -1 to keep it
 * @gesture: 0, or a gesture id (a slider drag is one step)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_set_layer (LrgEditorWeatherEditor  *self,
                                              guint                    preset,
                                              guint                    layer,
                                              gfloat                   intensity,
                                              gint                     enabled,
                                              guint                    gesture,
                                              GError                 **error);

/**
 * lrg_editor_weather_editor_set_param:
 * @self: an #LrgEditorWeatherEditor
 * @preset: a preset index
 * @layer: a layer index
 * @name: a parameter of the layer's kind
 * @value: (nullable): the value in the file syntax (a number, `true`, an
 *   enum nick, `#rrggbbaa`, a gradient string), or %NULL to unset it
 * @gesture: 0, or a gesture id merging consecutive edits into one step
 * @error: return location for a #GError
 *
 * Sets a parameter through lrg_weather_asset_set_layer_param() (numbers
 * clamp to the parameter's range) and syncs the preview.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_set_param (LrgEditorWeatherEditor  *self,
                                              guint                    preset,
                                              guint                    layer,
                                              const gchar             *name,
                                              const gchar             *value,
                                              guint                    gesture,
                                              GError                 **error);

/**
 * lrg_editor_weather_editor_set_transition:
 * @self: an #LrgEditorWeatherEditor
 * @from: a preset id, `*` or `none`
 * @to: a preset id, `*` or `none`
 * @seconds: the crossfade (>= 0)
 * @error: return location for a #GError
 *
 * Adds or replaces the rule for @from to @to.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_set_transition (LrgEditorWeatherEditor  *self,
                                                   const gchar             *from,
                                                   const gchar             *to,
                                                   gfloat                   seconds,
                                                   GError                 **error);

/**
 * lrg_editor_weather_editor_remove_transition:
 * @self: an #LrgEditorWeatherEditor
 * @from: the rule's `from`
 * @to: the rule's `to`
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; %G_IO_ERROR_NOT_FOUND without such a rule
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_remove_transition (LrgEditorWeatherEditor  *self,
                                                      const gchar             *from,
                                                      const gchar             *to,
                                                      GError                 **error);

/* --- preview --- */

/**
 * lrg_editor_weather_editor_preview_preset:
 * @self: an #LrgEditorWeatherEditor
 * @preset_id: (nullable): a preset id, or %NULL for no weather
 * @error: return location for a #GError
 *
 * Switches the preview to a preset instantly (every layer at its level).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_weather_editor_preview_preset (LrgEditorWeatherEditor  *self,
                                                   const gchar             *preset_id,
                                                   GError                 **error);

/**
 * lrg_editor_weather_editor_transition_to:
 * @self: an #LrgEditorWeatherEditor
 * @preset_id: (nullable): a preset id, or %NULL to clear the weather
 * @error: return location for a #GError
 *
 * Crossfades the preview from its preset to @preset_id over the duration
 * the transition rules give (lrg_weather_asset_transition()), to check
 * the rules.
 *
 * Returns: the crossfade in seconds, or a negative value with @error set
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_weather_editor_transition_to (LrgEditorWeatherEditor  *self,
                                                const gchar             *preset_id,
                                                GError                 **error);

/**
 * lrg_editor_weather_editor_get_preview_preset:
 * @self: an #LrgEditorWeatherEditor
 *
 * Returns: (nullable): the preset the preview shows (or fades to), %NULL
 *   for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_weather_editor_get_preview_preset (LrgEditorWeatherEditor *self);

/**
 * lrg_editor_weather_editor_advance:
 * @self: an #LrgEditorWeatherEditor
 * @seconds: preview time to run now, in 1/60 s frames
 *
 * Steps the preview weather as the panel does every frame (tests, IPC).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_weather_editor_advance (LrgEditorWeatherEditor *self,
                                        gfloat                  seconds);

/**
 * lrg_editor_weather_editor_render_preview:
 * @self: an #LrgEditorWeatherEditor
 * @width: image width in pixels
 * @height: image height in pixels
 *
 * Renders the preview scene into an image, as the panel draws it. Needs a
 * graphics context.
 *
 * Returns: (transfer full) (nullable): the image, %NULL without a graphics
 *   context
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_weather_editor_render_preview (LrgEditorWeatherEditor *self,
                                                     guint                   width,
                                                     guint                   height);

G_END_DECLS
