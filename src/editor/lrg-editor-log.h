/* lrg-editor-log.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Log capture for the editor's Output/Console panel and IPC clients.
 *
 * LrgEditorLog keeps a ring buffer of recent log records. The process-wide
 * instance from lrg_editor_log_install() hooks the GLib structured log
 * writer, so every g_log/g_message/lrg_* message from the engine, the game
 * and the editor itself is captured and still forwarded to the default
 * writer (stderr / journald). Records arriving on other threads are
 * delivered on the main context so #LrgEditorLog::message handlers always
 * run on the UI thread.
 *
 * Records can also be appended directly (e.g. the stdout of a game launched
 * with Play) with lrg_editor_log_append().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_LOG_DEFAULT_CAPACITY:
 *
 * Default number of records kept.
 */
#define LRG_EDITOR_LOG_DEFAULT_CAPACITY (4000)

#define LRG_TYPE_EDITOR_LOG_RECORD (lrg_editor_log_record_get_type ())

/**
 * LrgEditorLogRecord:
 * @serial: monotonically increasing record number
 * @timestamp: wall-clock time in microseconds since the Unix epoch
 * @level: the GLib log level (a single #GLogLevelFlags bit)
 * @domain: (nullable): the log domain, or a source such as "game"
 * @message: the message text
 * @file: (nullable): source file from the structured fields, if any
 * @line: source line, or 0
 *
 * One captured log message.
 */
typedef struct
{
	guint64        serial;
	gint64         timestamp;
	GLogLevelFlags level;
	gchar         *domain;
	gchar         *message;
	gchar         *file;
	gint           line;
} LrgEditorLogRecord;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_log_record_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_log_record_copy:
 * @record: a record
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgEditorLogRecord * lrg_editor_log_record_copy (const LrgEditorLogRecord *record);

/**
 * lrg_editor_log_record_free:
 * @record: a record from lrg_editor_log_record_copy()
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_log_record_free (LrgEditorLogRecord *record);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorLogRecord, lrg_editor_log_record_free)

/**
 * lrg_editor_log_level_to_string:
 * @level: a log level
 *
 * Returns: (transfer none): "error", "critical", "warning", "message",
 *   "info" or "debug"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_log_level_to_string (GLogLevelFlags level);

#define LRG_TYPE_EDITOR_LOG (lrg_editor_log_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorLog, lrg_editor_log, LRG, EDITOR_LOG, GObject)

/**
 * lrg_editor_log_new:
 * @capacity: number of records to keep (0 = the default)
 *
 * Creates a standalone log buffer that captures nothing by itself.
 *
 * Returns: (transfer full): a new #LrgEditorLog
 */
LRG_AVAILABLE_IN_ALL
LrgEditorLog * lrg_editor_log_new (guint capacity);

/**
 * lrg_editor_log_install:
 *
 * Returns the process-wide log, installing the structured log writer on
 * the first call. GLib allows only one writer per process, so if the
 * application already set one this only returns the buffer (fed through
 * lrg_editor_log_append()).
 *
 * Returns: (transfer none): the process-wide #LrgEditorLog
 */
LRG_AVAILABLE_IN_ALL
LrgEditorLog * lrg_editor_log_install (void);

/**
 * lrg_editor_log_append:
 * @self: an #LrgEditorLog
 * @level: the log level
 * @domain: (nullable): the domain or source name
 * @message: the message
 *
 * Appends a record and emits #LrgEditorLog::message. Safe from any thread.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_log_append (LrgEditorLog   *self,
                            GLogLevelFlags  level,
                            const gchar    *domain,
                            const gchar    *message);

/**
 * lrg_editor_log_get_n_records:
 * @self: an #LrgEditorLog
 *
 * Returns: the number of records held
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_log_get_n_records (LrgEditorLog *self);

/**
 * lrg_editor_log_get_record:
 * @self: an #LrgEditorLog
 * @index: 0 is the oldest record held
 *
 * Returns: (transfer none) (nullable): the record
 */
LRG_AVAILABLE_IN_ALL
const LrgEditorLogRecord * lrg_editor_log_get_record (LrgEditorLog *self,
                                                      guint         index);

/**
 * lrg_editor_log_count_level:
 * @self: an #LrgEditorLog
 * @levels: mask of levels to count
 *
 * Returns: how many held records match @levels (for the warning/error
 *   badges in the status bar)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_log_count_level (LrgEditorLog   *self,
                                  GLogLevelFlags  levels);

/**
 * lrg_editor_log_clear:
 * @self: an #LrgEditorLog
 *
 * Drops every record and emits #LrgEditorLog::cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_log_clear (LrgEditorLog *self);

G_END_DECLS
