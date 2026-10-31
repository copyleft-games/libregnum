/* lrg-editor-log.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ring-buffered log capture. See lrg-editor-log.h.
 */

#include "lrg-editor-log.h"
#include <string.h>

struct _LrgEditorLog
{
	GObject parent_instance;

	GMutex               lock;
	LrgEditorLogRecord **ring;      /* capacity slots */
	guint                capacity;
	guint                head;      /* index of the oldest record */
	guint                count;
	guint64              next_serial;
	GMainContext        *context;   /* where signals are emitted */

	/* Running per-level tallies of the held records, so
	 * lrg_editor_log_count_level() (status bar badges, every frame)
	 * does not walk the ring. One entry per distinct level value seen;
	 * records normally carry a single level bit, so this stays tiny. */
	GArray              *level_counts;   /* LevelCount */
};

typedef struct
{
	GLogLevelFlags level;
	guint          count;
} LevelCount;

G_DEFINE_FINAL_TYPE (LrgEditorLog, lrg_editor_log, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (LrgEditorLogRecord, lrg_editor_log_record,
                     lrg_editor_log_record_copy, lrg_editor_log_record_free)

enum
{
	SIGNAL_MESSAGE,
	SIGNAL_CLEARED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Records
 * ========================================================================== */

LrgEditorLogRecord *
lrg_editor_log_record_copy (const LrgEditorLogRecord *record)
{
	LrgEditorLogRecord *copy;

	g_return_val_if_fail (record != NULL, NULL);

	copy = g_new0 (LrgEditorLogRecord, 1);
	*copy = *record;
	copy->domain = g_strdup (record->domain);
	copy->message = g_strdup (record->message);
	copy->file = g_strdup (record->file);

	return copy;
}

void
lrg_editor_log_record_free (LrgEditorLogRecord *record)
{
	if (record == NULL)
		return;

	g_free (record->domain);
	g_free (record->message);
	g_free (record->file);
	g_free (record);
}

const gchar *
lrg_editor_log_level_to_string (GLogLevelFlags level)
{
	if (level & G_LOG_LEVEL_ERROR)
		return "error";
	if (level & G_LOG_LEVEL_CRITICAL)
		return "critical";
	if (level & G_LOG_LEVEL_WARNING)
		return "warning";
	if (level & G_LOG_LEVEL_MESSAGE)
		return "message";
	if (level & G_LOG_LEVEL_INFO)
		return "info";
	return "debug";
}

/* ==========================================================================
 * Level tallies (call with the lock held)
 * ========================================================================== */

/* Adds @delta (+1 or -1) to the tally of records whose level is exactly
 * @level, creating the entry on first use. */
static void
level_count_adjust (LrgEditorLog   *self,
                    GLogLevelFlags  level,
                    gint            delta)
{
	LevelCount entry;
	guint      i;

	for (i = 0; i < self->level_counts->len; i++)
	{
		LevelCount *c = &g_array_index (self->level_counts, LevelCount, i);

		if (c->level == level)
		{
			c->count = (guint) ((gint) c->count + delta);
			return;
		}
	}

	if (delta > 0)
	{
		entry.level = level;
		entry.count = (guint) delta;
		g_array_append_val (self->level_counts, entry);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_log_finalize (GObject *object)
{
	LrgEditorLog *self = LRG_EDITOR_LOG (object);
	guint         i;

	for (i = 0; i < self->count; i++)
		lrg_editor_log_record_free (self->ring[(self->head + i) % self->capacity]);
	g_free (self->ring);
	g_array_unref (self->level_counts);
	g_main_context_unref (self->context);
	g_mutex_clear (&self->lock);

	G_OBJECT_CLASS (lrg_editor_log_parent_class)->finalize (object);
}

static void
lrg_editor_log_class_init (LrgEditorLogClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_log_finalize;

	/**
	 * LrgEditorLog::message:
	 * @self: the log
	 * @record: (transfer none): the new record
	 *
	 * Emitted on the main context for every appended record.
	 */
	signals[SIGNAL_MESSAGE] =
		g_signal_new ("message",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1,
		              LRG_TYPE_EDITOR_LOG_RECORD | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgEditorLog::cleared:
	 * @self: the log
	 *
	 * Emitted after lrg_editor_log_clear().
	 */
	signals[SIGNAL_CLEARED] =
		g_signal_new ("cleared",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_editor_log_init (LrgEditorLog *self)
{
	g_mutex_init (&self->lock);
	self->context = g_main_context_ref (g_main_context_default ());
	self->level_counts = g_array_new (FALSE, FALSE, sizeof (LevelCount));
}

LrgEditorLog *
lrg_editor_log_new (guint capacity)
{
	LrgEditorLog *self = g_object_new (LRG_TYPE_EDITOR_LOG, NULL);

	self->capacity = (capacity > 0) ? capacity : LRG_EDITOR_LOG_DEFAULT_CAPACITY;
	self->ring = g_new0 (LrgEditorLogRecord *, self->capacity);
	return self;
}

/* ==========================================================================
 * Appending
 * ========================================================================== */

typedef struct
{
	LrgEditorLog       *self;    /* ref */
	LrgEditorLogRecord *record;  /* owned copy */
} EmitData;

static gboolean
emit_idle (gpointer user_data)
{
	EmitData *data = user_data;

	g_signal_emit (data->self, signals[SIGNAL_MESSAGE], 0, data->record);
	lrg_editor_log_record_free (data->record);
	g_object_unref (data->self);
	g_free (data);

	return G_SOURCE_REMOVE;
}

static void
append_record (LrgEditorLog       *self,
               LrgEditorLogRecord *record)   /* transfer full */
{
	LrgEditorLogRecord *copy;
	EmitData           *data;

	g_mutex_lock (&self->lock);
	record->serial = ++self->next_serial;
	if (self->count == self->capacity)
	{
		/* Full: overwrite the oldest. */
		level_count_adjust (self, self->ring[self->head]->level, -1);
		lrg_editor_log_record_free (self->ring[self->head]);
		self->ring[self->head] = record;
		self->head = (self->head + 1) % self->capacity;
	}
	else
	{
		self->ring[(self->head + self->count) % self->capacity] = record;
		self->count++;
	}
	level_count_adjust (self, record->level, 1);
	copy = lrg_editor_log_record_copy (record);
	g_mutex_unlock (&self->lock);

	/* Always emit from an idle on the owning context: the caller may be
	 * inside the GLib log machinery or on another thread. */
	data = g_new0 (EmitData, 1);
	data->self = g_object_ref (self);
	data->record = copy;
	g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT_IDLE,
	                            emit_idle, data, NULL);
}

void
lrg_editor_log_append (LrgEditorLog   *self,
                       GLogLevelFlags  level,
                       const gchar    *domain,
                       const gchar    *message)
{
	LrgEditorLogRecord *record;

	g_return_if_fail (LRG_IS_EDITOR_LOG (self));

	record = g_new0 (LrgEditorLogRecord, 1);
	record->timestamp = g_get_real_time ();
	record->level = level;
	record->domain = g_strdup (domain);
	record->message = g_strdup (message != NULL ? message : "");
	append_record (self, record);
}

guint
lrg_editor_log_get_n_records (LrgEditorLog *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_LOG (self), 0);

	return self->count;
}

const LrgEditorLogRecord *
lrg_editor_log_get_record (LrgEditorLog *self,
                           guint         index)
{
	const LrgEditorLogRecord *record = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_LOG (self), NULL);

	g_mutex_lock (&self->lock);
	if (index < self->count)
		record = self->ring[(self->head + index) % self->capacity];
	g_mutex_unlock (&self->lock);

	return record;
}

guint
lrg_editor_log_count_level (LrgEditorLog   *self,
                            GLogLevelFlags  levels)
{
	guint i, n = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_LOG (self), 0);

	/* Sum the running tallies of every level value that matches; same
	 * result as testing each held record against @levels. */
	g_mutex_lock (&self->lock);
	for (i = 0; i < self->level_counts->len; i++)
	{
		const LevelCount *c = &g_array_index (self->level_counts, LevelCount, i);

		if (c->level & levels)
			n += c->count;
	}
	g_mutex_unlock (&self->lock);

	return n;
}

void
lrg_editor_log_clear (LrgEditorLog *self)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_LOG (self));

	g_mutex_lock (&self->lock);
	for (i = 0; i < self->count; i++)
		lrg_editor_log_record_free (self->ring[(self->head + i) % self->capacity]);
	self->head = 0;
	self->count = 0;
	g_array_set_size (self->level_counts, 0);
	g_mutex_unlock (&self->lock);

	g_signal_emit (self, signals[SIGNAL_CLEARED], 0);
}

/* ==========================================================================
 * Process-wide writer
 * ========================================================================== */

static LrgEditorLog *default_log = NULL;
static GPrivate      in_writer;

static GLogWriterOutput
editor_log_writer (GLogLevelFlags   level,
                   const GLogField *fields,
                   gsize            n_fields,
                   gpointer         user_data)
{
	/* Capture what the default writer would show (debug and info only
	 * with G_MESSAGES_DEBUG), never recursively. */
	if (default_log != NULL && g_private_get (&in_writer) == NULL &&
	    !g_log_writer_default_would_drop (level, NULL))
	{
		LrgEditorLogRecord *record;
		gsize               i;

		g_private_set (&in_writer, GINT_TO_POINTER (1));

		record = g_new0 (LrgEditorLogRecord, 1);
		record->timestamp = g_get_real_time ();
		record->level = level & G_LOG_LEVEL_MASK;
		for (i = 0; i < n_fields; i++)
		{
			const gchar *key = fields[i].key;
			gssize       len = fields[i].length;
			const gchar *val = fields[i].value;

			if (g_strcmp0 (key, "MESSAGE") == 0)
				record->message = (len < 0) ? g_strdup (val) : g_strndup (val, len);
			else if (g_strcmp0 (key, "GLIB_DOMAIN") == 0)
				record->domain = (len < 0) ? g_strdup (val) : g_strndup (val, len);
			else if (g_strcmp0 (key, "CODE_FILE") == 0)
				record->file = (len < 0) ? g_strdup (val) : g_strndup (val, len);
			else if (g_strcmp0 (key, "CODE_LINE") == 0)
			{
				g_autofree gchar *s = (len < 0) ? g_strdup (val) : g_strndup (val, len);

				record->line = (gint) g_ascii_strtoll (s, NULL, 10);
			}
		}
		if (record->message == NULL)
			record->message = g_strdup ("");

		append_record (default_log, record);
		g_private_set (&in_writer, NULL);
	}

	return g_log_writer_default (level, fields, n_fields, user_data);
}

LrgEditorLog *
lrg_editor_log_install (void)
{
	static gsize once = 0;

	if (g_once_init_enter (&once))
	{
		default_log = lrg_editor_log_new (0);
		g_log_set_writer_func (editor_log_writer, NULL, NULL);
		g_once_init_leave (&once, 1);
	}

	return default_log;
}
