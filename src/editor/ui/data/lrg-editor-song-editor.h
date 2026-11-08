/* lrg-editor-song-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSongEditor: the song editor (a small DAW), the data editor of
 * .rsong files (#LrgSong).  An arrangement of tracks and clips on a bar
 * ruler with markers and the loop range; a step grid (drum patterns) or a
 * piano roll (note patterns) for the selected clip's pattern; the
 * selected track's instrument parameters; a transport playing the song
 * live on the audio device while it is edited; mixdown and stem export
 * to any audio format.  Every change is one undo step of the document;
 * the song.* IPC methods (also a batch, song.apply) make the same edits,
 * so a person and an agent can compose together.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../lrg-types.h"
#include "../../../music/lrg-song.h"
#include "../../lrg-editor-document.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SONG_EDITOR (lrg_editor_song_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSongEditor, lrg_editor_song_editor, LRG, EDITOR_SONG_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_song_editor_new:
 * @app: (nullable): the editor application
 * @document: a .rsong document
 *
 * Returns: (transfer full): a new song editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSongEditor * lrg_editor_song_editor_new (LrgEditorApp      *app,
                                                  LrgEditorDocument *document);

/**
 * lrg_editor_song_editor_install:
 * @app: the editor application
 *
 * Registers the song.* and audio.formats IPC methods (once).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_song_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_song_editor_open:
 * @app: the editor application
 * @path: a .rsong file, relative to the project or absolute
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer none) (nullable): the open (or newly opened) editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSongEditor * lrg_editor_song_editor_open (LrgEditorApp  *app,
                                                   const gchar   *path,
                                                   GError       **error);

/**
 * lrg_editor_song_editor_create:
 * @app: (nullable): the editor application
 * @path: the new .rsong file
 * @empty: %TRUE for a song with no tracks, %FALSE for the starter loop
 *   (drums and a bass line)
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_song_editor_create (LrgEditorApp  *app,
                                        const gchar   *path,
                                        gboolean       empty,
                                        GError       **error);

/**
 * lrg_editor_song_editor_get_song:
 * @self: an #LrgEditorSongEditor
 *
 * Returns: (transfer none) (nullable): the song the document holds
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_editor_song_editor_get_song (LrgEditorSongEditor *self);

/**
 * lrg_editor_song_editor_apply:
 * @self: an #LrgEditorSongEditor
 * @ops: a JSON array of {"op": name, ...arguments} (the song.* method
 *   names without "song.", for example {"op": "add_clip", "track":
 *   "bass", "source": "bass-a", "start": 0})
 * @label: (nullable): the undo label
 * @error: (nullable): return location for an error
 *
 * Applies every edit to a copy of the song and commits it as one undo
 * step; nothing changes when one fails.
 *
 * Returns: (transfer full) (nullable): a JSON array of the results
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_song_editor_apply (LrgEditorSongEditor  *self,
                                      const gchar          *ops,
                                      const gchar          *label,
                                      GError              **error);

/**
 * lrg_editor_song_editor_select:
 * @self: an #LrgEditorSongEditor
 * @track: (nullable): a track id
 * @clip: a clip index on it, or -1
 *
 * Selects a track and clip; the pattern editor shows the clip's pattern.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_song_editor_select (LrgEditorSongEditor *self,
                                    const gchar         *track,
                                    gint                 clip);

/**
 * lrg_editor_song_editor_get_selected_track:
 * @self: an #LrgEditorSongEditor
 *
 * Returns: (transfer none) (nullable): the selected track id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_song_editor_get_selected_track (LrgEditorSongEditor *self);

/**
 * lrg_editor_song_editor_get_selected_clip:
 * @self: an #LrgEditorSongEditor
 *
 * Returns: the selected clip index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_song_editor_get_selected_clip (LrgEditorSongEditor *self);

/**
 * lrg_editor_song_editor_play:
 * @self: an #LrgEditorSongEditor
 * @from_beat: where to start (below 0: where the playhead is)
 * @error: (nullable): return location for an error
 *
 * Plays the song on the audio device; later edits are heard at once.
 *
 * Returns: %TRUE when it plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_song_editor_play (LrgEditorSongEditor  *self,
                                      gdouble               from_beat,
                                      GError              **error);

/**
 * lrg_editor_song_editor_stop:
 * @self: an #LrgEditorSongEditor
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_song_editor_stop (LrgEditorSongEditor *self);

/**
 * lrg_editor_song_editor_get_beat:
 * @self: an #LrgEditorSongEditor
 *
 * Returns: the playhead in beats
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_song_editor_get_beat (LrgEditorSongEditor *self);

/**
 * lrg_editor_song_editor_preview_render:
 * @self: an #LrgEditorSongEditor
 * @frames: frames to render
 * @n_samples: (out): samples returned (frames x 2)
 * @error: (nullable): return location for an error
 *
 * Renders the next @frames frames of the live preview headless (no
 * device), from the playhead, advancing it (tests, captures).
 *
 * Returns: (transfer full) (array length=n_samples) (nullable): stereo
 *   interleaved float samples
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_editor_song_editor_preview_render (LrgEditorSongEditor  *self,
                                                guint                 frames,
                                                gsize                *n_samples,
                                                GError              **error);

/**
 * lrg_editor_song_editor_export:
 * @self: an #LrgEditorSongEditor
 * @path: the audio file to write (its extension picks the format)
 * @options: (nullable): render options (%NULL: -14 LUFS, -1 dBTP)
 * @error: (nullable): return location for an error
 *
 * Renders the mixdown and writes it, tagged with the song's title and
 * artist.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_song_editor_export (LrgEditorSongEditor         *self,
                                        const gchar                 *path,
                                        const LrgSongRenderOptions  *options,
                                        GError                     **error);

G_END_DECLS
