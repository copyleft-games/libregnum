/* lrg-editor-locale-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorLocaleEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_LOCALE_EDITOR (lrg_editor_locale_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorLocaleEditor, lrg_editor_locale_editor, LRG, EDITOR_LOCALE_EDITOR, LrgContainer)

/**
 * lrg_editor_locale_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorLocaleEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorLocaleEditor * lrg_editor_locale_editor_new (void);

G_END_DECLS
