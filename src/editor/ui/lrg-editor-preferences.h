/* lrg-editor-preferences.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPreferences: the preferences editor (Wave 4).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Preferences (Wave 4)"
 * label. The constructor signature is final.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PREFERENCES (lrg_editor_preferences_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPreferences, lrg_editor_preferences, LRG, EDITOR_PREFERENCES, LrgContainer)

/**
 * lrg_editor_preferences_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorPreferences
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPreferences * lrg_editor_preferences_new (LrgEditorApp *app);

/**
 * lrg_editor_preferences_get_app:
 * @self: an #LrgEditorPreferences
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_preferences_get_app (LrgEditorPreferences *self);

G_END_DECLS
