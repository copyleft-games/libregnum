/* lrg-editor-frame-debugger.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFrameDebugger: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_FRAME_DEBUGGER (lrg_editor_frame_debugger_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorFrameDebugger, lrg_editor_frame_debugger, LRG, EDITOR_FRAME_DEBUGGER, GObject)

/**
 * lrg_editor_frame_debugger_new:
 *
 * Returns: (transfer full): a new #LrgEditorFrameDebugger
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFrameDebugger * lrg_editor_frame_debugger_new (void);

G_END_DECLS
