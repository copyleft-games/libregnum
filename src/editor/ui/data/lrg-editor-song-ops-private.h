/* lrg-editor-song-ops-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The song editing operations shared by the song editor's IPC methods
 * (song.<op>), its batch (song.apply) and its UI: each takes JSON
 * arguments, changes an LrgSong and fills a JSON result.  Also the song
 * as JSON and the discovery catalogue (song.instruments).  Not installed.
 */

#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>
#include "../../../music/lrg-song.h"

G_BEGIN_DECLS

/*
 * LrgSongOp:
 * Changes @song from @args; adds its results to @result.  On failure
 * sets @error (G_IO_ERROR_INVALID_ARGUMENT for bad arguments,
 * G_IO_ERROR_NOT_FOUND for unknown names) and may leave @song partly
 * changed (callers work on a copy).
 */
typedef gboolean (*LrgSongOp) (LrgSong     *song,
                               JsonObject  *args,
                               JsonObject  *result,
                               GError     **error);

/* the operation called @name ("add_clip"), or NULL */
G_GNUC_INTERNAL LrgSongOp lrg_editor_song_op_find (const gchar *name);

/* the names of every operation, NULL-terminated, static */
G_GNUC_INTERNAL const gchar * const *lrg_editor_song_op_names (void);

/*
 * lrg_editor_song_to_json:
 * The song as JSON; @section limits it to one part ("settings",
 * "instruments", "patterns", "tracks", "mixer", "automation") or NULL for
 * everything.
 */
G_GNUC_INTERNAL JsonObject *lrg_editor_song_to_json (LrgSong     *song,
                                                     const gchar *section);

/* every instrument type, preset, kit, piece, parameter and effect type */
G_GNUC_INTERNAL JsonObject *lrg_editor_song_catalogue (void);

/* every audio file format and what writes it here */
G_GNUC_INTERNAL JsonObject *lrg_editor_song_formats (void);

G_END_DECLS
