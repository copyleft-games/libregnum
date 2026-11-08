/* lrg-editor-frame-debugger.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFrameDebugger: one captured frame's draw calls.
 *
 * The panel lists the calls recorded by an #LrgFrameCapture (pass, kind,
 * entity, triangles), shows the selected call's state and scrubs the
 * replay to that call.  It is constructed with the profiler panel and
 * stays undocked until frame.show (or the View menu, once the shell
 * patch is applied).  Recording the list needs no GL; replaying into
 * an image does.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../debug/lrg-frame-capture.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_FRAME_DEBUGGER (lrg_editor_frame_debugger_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorFrameDebugger, lrg_editor_frame_debugger, LRG, EDITOR_FRAME_DEBUGGER, LrgContainer)

/**
 * lrg_editor_frame_debugger_new:
 * @app: the editor application it registers with
 *
 * Creates the frame debugger.  It owns an #LrgFrameCapture, registers
 * the frame.* IPC methods and the frame.capture / frame.show /
 * frame.scrub-full actions, and does not dock itself.
 *
 * Returns: (transfer full): a new #LrgEditorFrameDebugger
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFrameDebugger * lrg_editor_frame_debugger_new (LrgEditorApp *app);

/**
 * lrg_editor_frame_debugger_get_capture:
 * @self: an #LrgEditorFrameDebugger
 *
 * Returns: (transfer none): the capture the panel shows
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_editor_frame_debugger_get_capture (LrgEditorFrameDebugger *self);

/**
 * lrg_editor_frame_debugger_get_scrub:
 * @self: an #LrgEditorFrameDebugger
 *
 * Returns: the scrub index, or -1 for the whole frame
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_frame_debugger_get_scrub (LrgEditorFrameDebugger *self);

G_END_DECLS
