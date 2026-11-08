/* lrg-audio-yaml-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared reading and writing helpers for the 0.10.0 audio formats
 * (.rfoley, .rsong).  Readers report the first problem as
 * "<source>: <path>: <message>" in the format's own error domain, where
 * <path> is a YAML path such as "tracks[2].clips[0].start".  Writers emit
 * numbers with the shortest text that parses back to the same value, so a
 * file written and read again writes the same text.
 *
 * Not installed: these expose yaml-glib types.
 */

#pragma once

#include <glib-object.h>
#include <yaml-glib.h>

G_BEGIN_DECLS

typedef struct
{
	const gchar *source;       /* file name or "<data>" */
	GQuark       domain;
	gint         code_parse;   /* code for unreadable YAML */
	gint         code_value;   /* code for a wrong value */
	GError     **error;
} LrgAudioYamlCtx;

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_fail (LrgAudioYamlCtx *ctx,
                              const gchar     *path,
                              const gchar     *format,
                              ...) G_GNUC_PRINTF (3, 4);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_parse (LrgAudioYamlCtx  *ctx,
                                 const gchar      *data,
                                 gssize            length,
                                 YamlParser      **out_parser);

G_GNUC_INTERNAL
YamlMapping * lrg_audio_yaml_map (LrgAudioYamlCtx *ctx,
                                  YamlNode        *node,
                                  const gchar     *path);

G_GNUC_INTERNAL
YamlSequence * lrg_audio_yaml_seq (LrgAudioYamlCtx *ctx,
                                   YamlNode        *node,
                                   const gchar     *path);

G_GNUC_INTERNAL
const gchar * lrg_audio_yaml_scalar (YamlNode *node);

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_parse_double (const gchar *text,
                                      gdouble     *out);

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_number (LrgAudioYamlCtx *ctx,
                                YamlNode        *node,
                                const gchar     *path,
                                gdouble          min,
                                gdouble          max,
                                gdouble         *out);

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_int (LrgAudioYamlCtx *ctx,
                             YamlNode        *node,
                             const gchar     *path,
                             gint64           min,
                             gint64           max,
                             gint64          *out);

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_bool (LrgAudioYamlCtx *ctx,
                              YamlNode        *node,
                              const gchar     *path,
                              gboolean        *out);

G_GNUC_INTERNAL
gboolean lrg_audio_yaml_string (LrgAudioYamlCtx  *ctx,
                                YamlNode         *node,
                                const gchar      *path,
                                const gchar     **out);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_new_map (YamlMapping **out_map);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_new_seq (YamlSequence **out_seq);

G_GNUC_INTERNAL
void lrg_audio_yaml_set (YamlMapping *map,
                         const gchar *key,
                         YamlNode    *node);

G_GNUC_INTERNAL
void lrg_audio_yaml_append (YamlSequence *seq,
                            YamlNode     *node);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_num (gdouble value);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_str (const gchar *text);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_int_node (gint64 value);

G_GNUC_INTERNAL
YamlNode * lrg_audio_yaml_bool_node (gboolean value);

G_GNUC_INTERNAL
gchar * lrg_audio_yaml_format_number (gdouble value);

G_GNUC_INTERNAL
gchar * lrg_audio_yaml_to_data (YamlNode  *root,
                                gsize     *length,
                                GError   **error);

G_END_DECLS
