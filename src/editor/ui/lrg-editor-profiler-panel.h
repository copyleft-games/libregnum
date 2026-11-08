/* lrg-editor-profiler-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorProfilerPanel: frame times and #LrgProfiler sections.
 *
 * The panel turns the default #LrgProfiler on while it is shown or while
 * play-in-editor runs (and hands it to the play session, which times
 * every tick under "play.tick"). It keeps a 300-frame ring of frame
 * times, drawn as a bar graph with 16.6 ms (60 fps) and 33.3 ms
 * (30 fps) guides and a hover readout, above a sortable table of the
 * profiler's sections (last, average and maximum time, calls). The IPC
 * method "profiler.sample" returns the same data.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../debug/lrg-profiler.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_PROFILER_FRAMES:
 *
 * Frames the profiler panel's graph remembers.
 */
#define LRG_EDITOR_PROFILER_FRAMES (300)

#define LRG_TYPE_EDITOR_PROFILER_PANEL (lrg_editor_profiler_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorProfilerPanel, lrg_editor_profiler_panel, LRG, EDITOR_PROFILER_PANEL, LrgContainer)

/**
 * lrg_editor_profiler_panel_new:
 * @app: the editor application it presents
 *
 * Creates the profiler panel. It registers the "profiler.sample" IPC
 * method and the "profiler.pause" / "profiler.clear" actions, and gives
 * the play session the default profiler.
 *
 * Returns: (transfer full): a new #LrgEditorProfilerPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorProfilerPanel * lrg_editor_profiler_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_profiler_panel_get_profiler:
 * @self: an #LrgEditorProfilerPanel
 *
 * Returns: (transfer none): the profiler the panel shows (the default one)
 */
LRG_AVAILABLE_IN_ALL
LrgProfiler * lrg_editor_profiler_panel_get_profiler (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_get_paused:
 * @self: an #LrgEditorProfilerPanel
 *
 * Returns: %TRUE while recording is paused (the graph and the table
 *   freeze)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_profiler_panel_get_paused (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_set_paused:
 * @self: an #LrgEditorProfilerPanel
 * @paused: freeze the graph and the table
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_profiler_panel_set_paused (LrgEditorProfilerPanel *self,
                                           gboolean                paused);

/**
 * lrg_editor_profiler_panel_clear:
 * @self: an #LrgEditorProfilerPanel
 *
 * Empties the frame ring and the table, and clears the profiler's
 * sections before the next frame starts.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_profiler_panel_clear (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_get_frames:
 * @self: an #LrgEditorProfilerPanel
 * @n_frames: (out): the number of frame times returned
 *
 * The recorded frame times, oldest first, at most
 * %LRG_EDITOR_PROFILER_FRAMES. A frame is recorded every canvas update
 * (lrg_editor_app_process_frame()) while the panel is shown or play
 * runs, unless paused; its time is the frame's delta time.
 *
 * Returns: (transfer full) (array length=n_frames): frame times in
 *   milliseconds
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_editor_profiler_panel_get_frames (LrgEditorProfilerPanel *self,
                                               guint                  *n_frames);

/**
 * lrg_editor_profiler_panel_get_n_sections:
 * @self: an #LrgEditorProfilerPanel
 *
 * Refreshes the table from the profiler.
 *
 * Returns: the number of sections the table lists
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_profiler_panel_get_n_sections (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_get_hovered_frame:
 * @self: an #LrgEditorProfilerPanel
 *
 * Returns: the index (into lrg_editor_profiler_panel_get_frames()) of
 *   the frame under the pointer in the graph, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_profiler_panel_get_hovered_frame (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_get_capture:
 * @self: an #LrgEditorProfilerPanel
 *
 * The capture held by the last profiler.capture or profiler.load.
 *
 * Returns: (transfer none) (nullable): the capture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgProfilerCapture * lrg_editor_profiler_panel_get_capture (LrgEditorProfilerPanel *self);

/**
 * lrg_editor_profiler_panel_get_view_mode:
 * @self: an #LrgEditorProfilerPanel
 *
 * Returns: (transfer none): "timeline", "flame", "table" or "compare"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_profiler_panel_get_view_mode (LrgEditorProfilerPanel *self);

G_END_DECLS
