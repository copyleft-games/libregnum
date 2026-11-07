/* lrg-lsp-client.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLspClient: a Language Server Protocol client over stdio.
 *
 * Threads. The server is a GSubprocess with stdin and stdout pipes. Two
 * helper threads move bytes so the main thread never blocks on a pipe:
 *
 *  - the WRITER pops framed messages ("Content-Length: N\r\n\r\n{...}")
 *    off a GAsyncQueue and writes them to the server's stdin, in order;
 *  - the READER reads framed messages from the server's stdout, parses
 *    them and dispatches: answers complete their pending request (a
 *    GTask returned from the reader, which GIO delivers in the caller's
 *    main context, or a sync slot woken through the condition), server
 *    requests are answered on the spot (workspace/configuration gets
 *    nulls, progress/registration requests get null, anything else
 *    MethodNotFound), notifications are stored (diagnostics) and
 *    re-emitted as signals in the main context the client started in.
 *
 * Holding no lock while writing matters: a server like clangd writes
 * its answers synchronously, so a client that blocked its reader while
 * writing a large didOpen could deadlock with it.
 *
 * Start-up. initialize goes out first; everything sent before its
 * answer waits in `held` and is queued, after the `initialized`
 * notification, by the reader when the answer arrives. So callers may
 * didOpen and request right after lrg_lsp_client_start().
 *
 * Positions. The public API speaks characters (code points) like
 * LrgTextEditor. The client offers ["utf-32", "utf-16"] position
 * encodings; when the server keeps the default UTF-16 the column is
 * converted with the line's text (the open document's, else the file's
 * on disk).
 *
 * Lock order: `lock` protects the state, pending table, held queue,
 * documents, diagnostics and capabilities. Nothing blocks while holding
 * it, except the waits of the sync API on `cond`.
 */

#include "config.h"

#include "lrg-lsp-client.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define INIT_STOP_WAIT_MS  (500)
#define EXIT_WAIT_MS       (1000)

/* JSON-RPC error codes we send */
#define RPC_METHOD_NOT_FOUND (-32601)

/* ==========================================================================
 * Enums, errors and boxed types
 * ========================================================================== */

G_DEFINE_QUARK (lrg-lsp-client-error-quark, lrg_lsp_client_error)

GType
lrg_lsp_severity_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LSP_SEVERITY_ERROR, "LRG_LSP_SEVERITY_ERROR", "error" },
			{ LRG_LSP_SEVERITY_WARNING, "LRG_LSP_SEVERITY_WARNING", "warning" },
			{ LRG_LSP_SEVERITY_INFORMATION, "LRG_LSP_SEVERITY_INFORMATION", "information" },
			{ LRG_LSP_SEVERITY_HINT, "LRG_LSP_SEVERITY_HINT", "hint" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgLspSeverity"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_lsp_client_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LSP_CLIENT_STATE_STOPPED, "LRG_LSP_CLIENT_STATE_STOPPED", "stopped" },
			{ LRG_LSP_CLIENT_STATE_STARTING, "LRG_LSP_CLIENT_STATE_STARTING", "starting" },
			{ LRG_LSP_CLIENT_STATE_READY, "LRG_LSP_CLIENT_STATE_READY", "ready" },
			{ LRG_LSP_CLIENT_STATE_EXITED, "LRG_LSP_CLIENT_STATE_EXITED", "exited" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgLspClientState"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

LrgLspDiagnostic *
lrg_lsp_diagnostic_copy (const LrgLspDiagnostic *diagnostic)
{
	LrgLspDiagnostic *copy;

	g_return_val_if_fail (diagnostic != NULL, NULL);
	copy = g_new0 (LrgLspDiagnostic, 1);
	*copy = *diagnostic;
	copy->message = g_strdup (diagnostic->message);
	copy->source = g_strdup (diagnostic->source);
	copy->code = g_strdup (diagnostic->code);
	return copy;
}

void
lrg_lsp_diagnostic_free (LrgLspDiagnostic *diagnostic)
{
	if (diagnostic == NULL)
		return;
	g_free (diagnostic->message);
	g_free (diagnostic->source);
	g_free (diagnostic->code);
	g_free (diagnostic);
}

G_DEFINE_BOXED_TYPE (LrgLspDiagnostic, lrg_lsp_diagnostic, lrg_lsp_diagnostic_copy, lrg_lsp_diagnostic_free)

LrgLspLocation *
lrg_lsp_location_copy (const LrgLspLocation *location)
{
	LrgLspLocation *copy;

	g_return_val_if_fail (location != NULL, NULL);
	copy = g_new0 (LrgLspLocation, 1);
	*copy = *location;
	copy->path = g_strdup (location->path);
	return copy;
}

void
lrg_lsp_location_free (LrgLspLocation *location)
{
	if (location == NULL)
		return;
	g_free (location->path);
	g_free (location);
}

G_DEFINE_BOXED_TYPE (LrgLspLocation, lrg_lsp_location, lrg_lsp_location_copy, lrg_lsp_location_free)

LrgLspCompletionItem *
lrg_lsp_completion_item_copy (const LrgLspCompletionItem *item)
{
	LrgLspCompletionItem *copy;

	g_return_val_if_fail (item != NULL, NULL);
	copy = g_new0 (LrgLspCompletionItem, 1);
	copy->label = g_strdup (item->label);
	copy->kind = item->kind;
	copy->detail = g_strdup (item->detail);
	copy->insert_text = g_strdup (item->insert_text);
	copy->documentation = g_strdup (item->documentation);
	return copy;
}

void
lrg_lsp_completion_item_free (LrgLspCompletionItem *item)
{
	if (item == NULL)
		return;
	g_free (item->label);
	g_free (item->detail);
	g_free (item->insert_text);
	g_free (item->documentation);
	g_free (item);
}

G_DEFINE_BOXED_TYPE (LrgLspCompletionItem, lrg_lsp_completion_item, lrg_lsp_completion_item_copy,
                     lrg_lsp_completion_item_free)

LrgLspTextEdit *
lrg_lsp_text_edit_copy (const LrgLspTextEdit *edit)
{
	LrgLspTextEdit *copy;

	g_return_val_if_fail (edit != NULL, NULL);
	copy = g_new0 (LrgLspTextEdit, 1);
	*copy = *edit;
	copy->path = g_strdup (edit->path);
	copy->new_text = g_strdup (edit->new_text);
	return copy;
}

void
lrg_lsp_text_edit_free (LrgLspTextEdit *edit)
{
	if (edit == NULL)
		return;
	g_free (edit->path);
	g_free (edit->new_text);
	g_free (edit);
}

G_DEFINE_BOXED_TYPE (LrgLspTextEdit, lrg_lsp_text_edit, lrg_lsp_text_edit_copy, lrg_lsp_text_edit_free)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

const gchar *
lrg_lsp_language_for_path (const gchar *path)
{
	const gchar *dot;
	g_autofree gchar *ext = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	dot = strrchr (path, '.');
	if (dot == NULL || strchr (dot, G_DIR_SEPARATOR) != NULL)
		return NULL;
	ext = g_ascii_strdown (dot + 1, -1);
	if (g_strcmp0 (ext, "c") == 0 || g_strcmp0 (ext, "h") == 0)
		return "c";
	if (g_strcmp0 (ext, "lua") == 0)
		return "lua";
	if (g_strcmp0 (ext, "py") == 0 || g_strcmp0 (ext, "pyw") == 0)
		return "python";
	if (g_strcmp0 (ext, "js") == 0 || g_strcmp0 (ext, "mjs") == 0)
		return "javascript";
	if (g_strcmp0 (ext, "ts") == 0)
		return "typescript";
	return NULL;
}

/* The default servers per language, tried in order. */
typedef struct
{
	const gchar *language;
	const gchar *program;
	const gchar *arg;      /* one optional argument */
} ServerCandidate;

static const ServerCandidate server_candidates[] = {
	{ "c", "clangd", NULL },
	{ "lua", "lua-language-server", NULL },
	{ "python", "pylsp", NULL },
	{ "python", "pyright-langserver", "--stdio" },
	{ "python", "basedpyright-langserver", "--stdio" },
	{ "python", "jedi-language-server", NULL },
	{ "javascript", "typescript-language-server", "--stdio" },
	{ "typescript", "typescript-language-server", "--stdio" },
};

GStrv
lrg_lsp_find_server (const gchar *language)
{
	guint i;

	g_return_val_if_fail (language != NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (server_candidates); i++)
	{
		g_autofree gchar *found = NULL;
		GStrvBuilder     *b;
		GStrv             argv;

		if (g_strcmp0 (server_candidates[i].language, language) != 0)
			continue;
		found = g_find_program_in_path (server_candidates[i].program);
		if (found == NULL)
			continue;
		b = g_strv_builder_new ();
		g_strv_builder_add (b, found);
		if (server_candidates[i].arg != NULL)
			g_strv_builder_add (b, server_candidates[i].arg);
		argv = g_strv_builder_end (b);
		g_strv_builder_unref (b);
		return argv;
	}
	return NULL;
}

/* Byte offset of the start of every line of @text. */
static GArray *
line_starts (const gchar *text)
{
	GArray      *starts = g_array_new (FALSE, FALSE, sizeof (gsize));
	gsize        zero = 0;
	const gchar *p;

	g_array_append_val (starts, zero);
	for (p = text; *p != '\0'; p++)
	{
		if (*p == '\n')
		{
			gsize next = (gsize) (p - text) + 1;

			g_array_append_val (starts, next);
		}
	}
	return starts;
}

/* Byte offset of (line, column in characters) in @text, clamped. */
static gsize
offset_of (const gchar *text,
           gsize        len,
           GArray      *starts,
           guint        line,
           guint        column)
{
	gsize        start;
	gsize        end;
	const gchar *p;
	guint        c;

	if (line >= starts->len)
		return len;
	start = g_array_index (starts, gsize, line);
	end = (line + 1 < starts->len) ? g_array_index (starts, gsize, line + 1) - 1 : len;
	p = text + start;
	for (c = 0; c < column && p < text + end; c++)
		p = g_utf8_next_char (p);
	return (gsize) (MIN (p, text + end) - text);
}

/* Sort helper: last edit first (by start, then by end). */
static gint
edit_cmp_desc (gconstpointer a,
               gconstpointer b)
{
	const LrgLspTextEdit *ea = *(LrgLspTextEdit * const *) a;
	const LrgLspTextEdit *eb = *(LrgLspTextEdit * const *) b;

	if (ea->line != eb->line)
		return ea->line < eb->line ? 1 : -1;
	if (ea->column != eb->column)
		return ea->column < eb->column ? 1 : -1;
	return 0;
}

gchar *
lrg_lsp_apply_edits (const gchar *text,
                     GPtrArray   *edits)
{
	g_autoptr(GArray)    starts = NULL;
	g_autoptr(GPtrArray) sorted = NULL;
	GString             *out;
	gsize                len;
	guint                i;

	g_return_val_if_fail (text != NULL, NULL);
	if (edits == NULL || edits->len == 0)
		return g_strdup (text);

	len = strlen (text);
	starts = line_starts (text);

	/* Positions refer to the original text: apply from the end. */
	sorted = g_ptr_array_new ();
	for (i = 0; i < edits->len; i++)
		g_ptr_array_add (sorted, g_ptr_array_index (edits, i));
	g_ptr_array_sort (sorted, edit_cmp_desc);

	out = g_string_new_len (text, (gssize) len);
	for (i = 0; i < sorted->len; i++)
	{
		LrgLspTextEdit *e = g_ptr_array_index (sorted, i);
		gsize           a = offset_of (text, len, starts, e->line, e->column);
		gsize           b = offset_of (text, len, starts, e->end_line, e->end_column);

		if (b < a)
			b = a;
		g_string_erase (out, (gssize) a, (gssize) (b - a));
		g_string_insert (out, (gssize) a, e->new_text != NULL ? e->new_text : "");
	}
	return g_string_free (out, FALSE);
}

/* Columns: characters <-> UTF-16 code units of @line_text. */
static guint
chars_to_utf16 (const gchar *line_text,
                guint        column)
{
	const gchar *p = line_text;
	guint        units = 0;
	guint        c;

	for (c = 0; c < column && *p != '\0' && *p != '\n'; c++)
	{
		gunichar ch = g_utf8_get_char (p);

		units += ch >= 0x10000 ? 2 : 1;
		p = g_utf8_next_char (p);
	}
	return units + (column - c);
}

static guint
utf16_to_chars (const gchar *line_text,
                guint        units)
{
	const gchar *p = line_text;
	guint        u = 0;
	guint        c = 0;

	while (u < units && *p != '\0' && *p != '\n')
	{
		gunichar ch = g_utf8_get_char (p);

		u += ch >= 0x10000 ? 2 : 1;
		c++;
		p = g_utf8_next_char (p);
	}
	return c + (units > u ? units - u : 0);
}

/* Pointer to line @line of @text (NULL past the end). */
static const gchar *
nth_line (const gchar *text,
          guint        line)
{
	const gchar *p = text;
	guint        l;

	for (l = 0; l < line; l++)
	{
		p = strchr (p, '\n');
		if (p == NULL)
			return NULL;
		p++;
	}
	return p;
}

/* Turns snippet syntax into its default text: ${1:x} -> x, $1 -> "". */
static gchar *
strip_snippet (const gchar *s)
{
	GString     *out = g_string_new (NULL);
	const gchar *p = s;

	while (*p != '\0')
	{
		if (*p == '\\' && (p[1] == '$' || p[1] == '}' || p[1] == '\\'))
		{
			g_string_append_c (out, p[1]);
			p += 2;
		}
		else if (*p == '$' && g_ascii_isdigit (p[1]))
		{
			p++;
			while (g_ascii_isdigit (*p))
				p++;
		}
		else if (*p == '$' && p[1] == '{')
		{
			const gchar *q = p + 2;
			gint         depth = 1;
			const gchar *colon = NULL;
			const gchar *body;

			while (g_ascii_isdigit (*q))
				q++;
			if (*q == ':')
				colon = q + 1;
			body = q;
			while (*body != '\0' && depth > 0)
			{
				if (*body == '{')
					depth++;
				else if (*body == '}')
					depth--;
				if (depth > 0)
					body++;
			}
			if (colon != NULL && body > colon)
			{
				g_autofree gchar *inner = g_strndup (colon, (gsize) (body - colon));
				g_autofree gchar *flat = strip_snippet (inner);

				g_string_append (out, flat);
			}
			p = (*body == '}') ? body + 1 : body;
		}
		else
		{
			g_string_append_c (out, *p);
			p++;
		}
	}
	return g_string_free (out, FALSE);
}

/* JSON helpers that tolerate missing or mistyped members. */
static const gchar *
obj_string (JsonObject  *o,
            const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gint64
obj_int (JsonObject  *o,
         const gchar *name,
         gint64       fallback)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return fallback;
	n = json_object_get_member (o, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return fallback;
	if (json_node_get_value_type (n) == G_TYPE_INT64)
		return json_node_get_int (n);
	if (json_node_get_value_type (n) == G_TYPE_DOUBLE)
		return (gint64) json_node_get_double (n);
	return fallback;
}

static JsonObject *
obj_object (JsonObject  *o,
            const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
}

static JsonArray *
obj_array (JsonObject  *o,
           const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return JSON_NODE_HOLDS_ARRAY (n) ? json_node_get_array (n) : NULL;
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef enum
{
	PARSE_RAW,
	PARSE_COMPLETION,
	PARSE_HOVER,
	PARSE_LOCATIONS
} ParseKind;

/* One request waiting for its answer. */
typedef struct
{
	gint64    id;
	GTask    *task;      /* async: owned; NULL for sync */
	gboolean  done;      /* sync: answered (or failed) */
	JsonNode *result;    /* sync: owned */
	GError   *error;     /* sync: owned */
} Pending;

/* An open document. */
typedef struct
{
	gchar *text;
	gint   version;
} Doc;

static void
doc_free (gpointer data)
{
	Doc *d = data;

	g_free (d->text);
	g_free (d);
}

struct _LrgLspClient
{
	GObject            parent_instance;

	gchar             *language_id;
	GStrv              argv;
	gchar             *root_path;

	/* process and threads */
	GSubprocess       *proc;
	GOutputStream     *out;          /* server stdin */
	GDataInputStream  *in;           /* server stdout */
	GThread           *reader;
	GThread           *writer;
	GAsyncQueue       *outbox;       /* gchar* framed messages; QUIT sentinel */
	GCancellable      *cancel;       /* wakes the reader */
	GMainContext      *context;      /* where signals are emitted */
	guint              generation;   /* bumped per start */
	gboolean           reader_done;  /* stdout reached EOF (atomic) */

	/* protected by lock */
	GMutex             lock;
	GCond              cond;
	LrgLspClientState  state;
	gint64             next_id;
	gint64             init_id;
	GHashTable        *pending;      /* gint64* -> Pending* (not owned) */
	GPtrArray         *held;         /* framed messages before initialized */
	GHashTable        *docs;         /* path -> Doc* */
	GHashTable        *diags;        /* path -> GPtrArray of LrgLspDiagnostic */
	JsonNode          *capabilities;
	gchar             *server_name;
	gboolean           utf32;
	GError            *init_error;
};

G_DEFINE_FINAL_TYPE (LrgLspClient, lrg_lsp_client, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_LANGUAGE_ID,
	PROP_ARGV,
	PROP_ROOT_PATH,
	PROP_STATE,
	N_PROPS
};

enum
{
	SIGNAL_READY,
	SIGNAL_EXITED,
	SIGNAL_DIAGNOSTICS_CHANGED,
	SIGNAL_LOG_MESSAGE,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* The writer's stop marker (never a real message). */
static gchar quit_marker[] = "quit";
#define QUIT ((gpointer) quit_marker)

/* ==========================================================================
 * Main-context posts from the threads
 * ========================================================================== */

typedef enum
{
	POST_STATE,
	POST_READY,
	POST_DIAGNOSTICS,
	POST_LOG
} PostKind;

typedef struct
{
	GWeakRef  ref;
	PostKind  kind;
	gchar    *text;
	gint      type;
} Post;

static void
post_free (gpointer data)
{
	Post *p = data;

	g_weak_ref_clear (&p->ref);
	g_free (p->text);
	g_free (p);
}

static gboolean
post_dispatch (gpointer data)
{
	Post                    *p = data;
	g_autoptr(LrgLspClient)  self = g_weak_ref_get (&p->ref);

	if (self == NULL)
		return G_SOURCE_REMOVE;
	switch (p->kind)
	{
	case POST_STATE:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
		break;
	case POST_READY:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
		g_signal_emit (self, signals[SIGNAL_READY], 0);
		break;
	case POST_DIAGNOSTICS:
		g_signal_emit (self, signals[SIGNAL_DIAGNOSTICS_CHANGED], 0, p->text);
		break;
	case POST_LOG:
		g_signal_emit (self, signals[SIGNAL_LOG_MESSAGE], 0, p->type, p->text);
		break;
	default:
		break;
	}
	return G_SOURCE_REMOVE;
}

/* Queues a signal emission in the client's main context (any thread). */
static void
post (LrgLspClient *self,
      PostKind      kind,
      const gchar  *text,
      gint          type)
{
	Post *p = g_new0 (Post, 1);

	g_weak_ref_init (&p->ref, self);
	p->kind = kind;
	p->text = g_strdup (text);
	p->type = type;
	g_main_context_invoke_full (self->context != NULL ? self->context : g_main_context_default (),
	                            G_PRIORITY_DEFAULT, post_dispatch, p, post_free);
}

/* ==========================================================================
 * Writing
 * ========================================================================== */

static gpointer
writer_main (gpointer data)
{
	LrgLspClient *self = data;
	gboolean      broken = FALSE;

	for (;;)
	{
		gchar *msg = g_async_queue_pop (self->outbox);

		if (msg == QUIT)
			break;
		if (!broken)
		{
			g_autoptr(GError) error = NULL;

			if (!g_output_stream_write_all (self->out, msg, strlen (msg), NULL, NULL, &error) ||
			    !g_output_stream_flush (self->out, NULL, &error))
			{
				/* The server is gone; keep draining until QUIT */
				g_debug ("lsp: write failed: %s", error->message);
				broken = TRUE;
			}
		}
		g_free (msg);
	}
	return NULL;
}

/* Frames a JSON-RPC message. */
static gchar *
frame (JsonObject *msg)
{
	g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);
	g_autofree gchar   *body = NULL;

	json_object_set_string_member (msg, "jsonrpc", "2.0");
	json_node_set_object (node, msg);
	body = json_to_string (node, FALSE);
	return g_strdup_printf ("Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n%s", strlen (body), body);
}

/*
 * enqueue_locked:
 *
 * Sends a framed message (taking it): straight to the writer once the
 * server is initialized, held until then. @force skips the hold (for
 * initialize itself and the replies to server requests). Caller holds
 * the lock.
 */
static gboolean
enqueue_locked (LrgLspClient  *self,
                gchar         *framed,
                gboolean       force,
                GError       **error)
{
	if (self->state != LRG_LSP_CLIENT_STATE_STARTING && self->state != LRG_LSP_CLIENT_STATE_READY)
	{
		g_free (framed);
		g_set_error (error, LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_NOT_RUNNING,
		             "The %s language server is not running", self->language_id);
		return FALSE;
	}
	if (self->state == LRG_LSP_CLIENT_STATE_STARTING && !force)
		g_ptr_array_add (self->held, framed);
	else
		g_async_queue_push (self->outbox, framed);
	return TRUE;
}

/* ==========================================================================
 * Pending requests
 * ========================================================================== */

static void
pending_finish (Pending  *p,
                JsonNode *result,
                GError   *error)
{
	/* async: hand the outcome to the task (any thread is fine) */
	if (error != NULL)
		g_task_return_error (p->task, error);
	else
		g_task_return_pointer (p->task, result, (GDestroyNotify) json_node_unref);
	g_object_unref (p->task);
	g_free (p);
}

/* Fails every pending request (the server went away). */
static void
fail_all (LrgLspClient *self,
          const gchar  *why)
{
	g_autoptr(GPtrArray) tasks = g_ptr_array_new ();
	GHashTableIter       iter;
	gpointer             value;
	guint                i;

	g_mutex_lock (&self->lock);
	g_hash_table_iter_init (&iter, self->pending);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		Pending *p = value;

		if (p->task != NULL)
			g_ptr_array_add (tasks, p);
		else
		{
			p->done = TRUE;
			p->error = g_error_new (LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_EXITED, "%s", why);
		}
		g_hash_table_iter_remove (&iter);
	}
	g_cond_broadcast (&self->cond);
	g_mutex_unlock (&self->lock);

	for (i = 0; i < tasks->len; i++)
		pending_finish (g_ptr_array_index (tasks, i), NULL,
		                g_error_new (LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_EXITED, "%s", why));
}

/* ==========================================================================
 * Reading
 * ========================================================================== */

/* Copy of line @line of @path's text: the open document, else the disk. */
static gchar *
line_text_locked (LrgLspClient *self,
                  const gchar  *path,
                  guint         line)
{
	Doc              *doc = g_hash_table_lookup (self->docs, path);
	g_autofree gchar *disk = NULL;
	const gchar      *text;
	const gchar      *start;
	const gchar      *end;

	if (doc != NULL)
		text = doc->text;
	else
	{
		if (!g_file_get_contents (path, &disk, NULL, NULL))
			return NULL;
		text = disk;
	}
	start = nth_line (text, line);
	if (start == NULL)
		return NULL;
	end = strchr (start, '\n');
	return end != NULL ? g_strndup (start, (gsize) (end - start)) : g_strdup (start);
}

/* Server column -> characters (caller holds the lock). */
static guint
from_server_locked (LrgLspClient *self,
                    const gchar  *path,
                    guint         line,
                    guint         units)
{
	g_autofree gchar *text = NULL;

	if (self->utf32 || units == 0)
		return units;
	text = line_text_locked (self, path, line);
	return text != NULL ? utf16_to_chars (text, units) : units;
}

/* Characters -> server column. */
static guint
to_server (LrgLspClient *self,
           const gchar  *path,
           guint         line,
           guint         column)
{
	g_autofree gchar *text = NULL;
	guint             units;

	g_mutex_lock (&self->lock);
	if (self->utf32 || column == 0)
	{
		g_mutex_unlock (&self->lock);
		return column;
	}
	text = line_text_locked (self, path, line);
	units = text != NULL ? chars_to_utf16 (text, column) : column;
	g_mutex_unlock (&self->lock);
	return units;
}

/* Reads an LSP Range into character positions. */
static void
read_range_locked (LrgLspClient *self,
                   const gchar  *path,
                   JsonObject   *range,
                   guint        *line,
                   guint        *column,
                   guint        *end_line,
                   guint        *end_column)
{
	JsonObject *start = obj_object (range, "start");
	JsonObject *end = obj_object (range, "end");

	*line = (guint) MAX (obj_int (start, "line", 0), 0);
	*column = from_server_locked (self, path, *line, (guint) MAX (obj_int (start, "character", 0), 0));
	*end_line = (guint) MAX (obj_int (end, "line", (gint64) *line), 0);
	*end_column = from_server_locked (self, path, *end_line,
	                                  (guint) MAX (obj_int (end, "character", 0), 0));
}

static gchar *
uri_to_path (const gchar *uri)
{
	if (uri == NULL)
		return NULL;
	if (g_str_has_prefix (uri, "file:"))
		return g_filename_from_uri (uri, NULL, NULL);
	return g_strdup (uri);
}

static void
handle_diagnostics (LrgLspClient *self,
                    JsonObject   *params)
{
	g_autofree gchar *path = uri_to_path (obj_string (params, "uri"));
	JsonArray        *list = obj_array (params, "diagnostics");
	GPtrArray        *out;
	guint             i;

	if (path == NULL)
		return;
	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_diagnostic_free);
	g_mutex_lock (&self->lock);
	for (i = 0; list != NULL && i < json_array_get_length (list); i++)
	{
		JsonNode         *n = json_array_get_element (list, i);
		JsonObject       *o;
		LrgLspDiagnostic *d;
		gint64            severity;

		if (!JSON_NODE_HOLDS_OBJECT (n))
			continue;
		o = json_node_get_object (n);
		d = g_new0 (LrgLspDiagnostic, 1);
		read_range_locked (self, path, obj_object (o, "range"), &d->line, &d->column, &d->end_line,
		                   &d->end_column);
		severity = obj_int (o, "severity", LRG_LSP_SEVERITY_ERROR);
		d->severity = (LrgLspSeverity) CLAMP (severity, LRG_LSP_SEVERITY_ERROR, LRG_LSP_SEVERITY_HINT);
		d->message = g_strdup (obj_string (o, "message") != NULL ? obj_string (o, "message") : "");
		d->source = g_strdup (obj_string (o, "source"));
		if (json_object_has_member (o, "code"))
		{
			JsonNode *c = json_object_get_member (o, "code");

			if (JSON_NODE_HOLDS_VALUE (c) && json_node_get_value_type (c) == G_TYPE_STRING)
				d->code = g_strdup (json_node_get_string (c));
			else if (JSON_NODE_HOLDS_VALUE (c))
				d->code = g_strdup_printf ("%" G_GINT64_FORMAT, obj_int (o, "code", 0));
		}
		g_ptr_array_add (out, d);
	}
	g_hash_table_replace (self->diags, g_strdup (path), out);
	g_mutex_unlock (&self->lock);
	post (self, POST_DIAGNOSTICS, path, 0);
}

/* Replies to a request the server sent us. */
static void
answer_server_request (LrgLspClient *self,
                       JsonNode     *id,
                       const gchar  *method,
                       JsonObject   *params)
{
	JsonObject *reply = json_object_new ();

	json_object_set_member (reply, "id", json_node_copy (id));
	if (g_strcmp0 (method, "workspace/configuration") == 0)
	{
		/* one null per item: "use your defaults" */
		JsonArray *items = obj_array (params, "items");
		JsonArray *result = json_array_new ();
		guint      i;

		for (i = 0; items != NULL && i < json_array_get_length (items); i++)
			json_array_add_null_element (result);
		json_object_set_array_member (reply, "result", result);
	}
	else if (g_strcmp0 (method, "workspace/workspaceFolders") == 0)
	{
		JsonArray *result = json_array_new ();

		if (self->root_path != NULL)
		{
			JsonObject       *folder = json_object_new ();
			g_autofree gchar *uri = g_filename_to_uri (self->root_path, NULL, NULL);
			g_autofree gchar *name = g_path_get_basename (self->root_path);

			json_object_set_string_member (folder, "uri", uri);
			json_object_set_string_member (folder, "name", name);
			json_array_add_object_element (result, folder);
		}
		json_object_set_array_member (reply, "result", result);
	}
	else if (g_strcmp0 (method, "window/workDoneProgress/create") == 0 ||
	         g_strcmp0 (method, "client/registerCapability") == 0 ||
	         g_strcmp0 (method, "client/unregisterCapability") == 0 ||
	         g_strcmp0 (method, "window/showMessageRequest") == 0)
	{
		json_object_set_null_member (reply, "result");
	}
	else
	{
		JsonObject *err = json_object_new ();

		json_object_set_int_member (err, "code", RPC_METHOD_NOT_FOUND);
		json_object_set_string_member (err, "message", "Method not supported by lrgeditor");
		json_object_set_object_member (reply, "error", err);
	}
	g_mutex_lock (&self->lock);
	enqueue_locked (self, frame (reply), TRUE, NULL);
	g_mutex_unlock (&self->lock);
	json_object_unref (reply);
}

/* The answer to initialize: capabilities, then release held messages. */
static void
handle_init_answer (LrgLspClient *self,
                    JsonNode     *result,
                    GError       *error)
{
	g_mutex_lock (&self->lock);
	if (error != NULL)
	{
		g_clear_error (&self->init_error);
		self->init_error = error;
		self->state = LRG_LSP_CLIENT_STATE_EXITED;
		g_cond_broadcast (&self->cond);
		g_mutex_unlock (&self->lock);
		post (self, POST_STATE, NULL, 0);
		return;
	}
	if (result != NULL && JSON_NODE_HOLDS_OBJECT (result))
	{
		JsonObject  *o = json_node_get_object (result);
		JsonObject  *caps = obj_object (o, "capabilities");
		JsonObject  *info = obj_object (o, "serverInfo");
		const gchar *encoding = obj_string (caps, "positionEncoding");

		g_clear_pointer (&self->capabilities, json_node_unref);
		if (caps != NULL)
		{
			self->capabilities = json_node_new (JSON_NODE_OBJECT);
			json_node_set_object (self->capabilities, caps);
		}
		if (obj_string (info, "name") != NULL)
		{
			g_free (self->server_name);
			self->server_name = g_strdup (obj_string (info, "name"));
		}
		self->utf32 = g_strcmp0 (encoding, "utf-32") == 0;
	}
	self->state = LRG_LSP_CLIENT_STATE_READY;
	{
		JsonObject *note = json_object_new ();
		guint       i;

		json_object_set_string_member (note, "method", "initialized");
		json_object_set_object_member (note, "params", json_object_new ());
		g_async_queue_push (self->outbox, frame (note));
		json_object_unref (note);
		for (i = 0; i < self->held->len; i++)
			g_async_queue_push (self->outbox, g_ptr_array_index (self->held, i));
		g_ptr_array_set_free_func (self->held, NULL);
		g_ptr_array_set_size (self->held, 0);
		g_ptr_array_set_free_func (self->held, g_free);
	}
	g_cond_broadcast (&self->cond);
	g_mutex_unlock (&self->lock);
	if (result != NULL)
		json_node_unref (result);
	post (self, POST_READY, NULL, 0);
}

static void
handle_answer (LrgLspClient *self,
               JsonObject   *msg)
{
	gint64    id = obj_int (msg, "id", -1);
	Pending  *p;
	JsonNode *result = NULL;
	GError   *error = NULL;

	/* outcome */
	if (json_object_has_member (msg, "error"))
	{
		JsonObject  *err = obj_object (msg, "error");
		const gchar *text = obj_string (err, "message");

		error = g_error_new (LRG_LSP_CLIENT_ERROR, (gint) obj_int (err, "code", -32603), "%s",
		                     text != NULL ? text : "The language server reported an error");
	}
	else if (json_object_has_member (msg, "result"))
		result = json_node_copy (json_object_get_member (msg, "result"));
	else
		result = json_node_new (JSON_NODE_NULL);

	if (id == self->init_id)
	{
		handle_init_answer (self, result, error);
		return;
	}

	g_mutex_lock (&self->lock);
	p = g_hash_table_lookup (self->pending, &id);
	if (p != NULL)
		g_hash_table_remove (self->pending, &id);
	if (p != NULL && p->task == NULL)
	{
		/* sync: the waiter frees it */
		p->done = TRUE;
		p->result = result;
		p->error = error;
		g_cond_broadcast (&self->cond);
		g_mutex_unlock (&self->lock);
		return;
	}
	g_mutex_unlock (&self->lock);

	if (p != NULL)
		pending_finish (p, result, error);
	else
	{
		/* an answer nobody waits for (timed out) */
		if (result != NULL)
			json_node_unref (result);
		g_clear_error (&error);
	}
}

static void
handle_message (LrgLspClient *self,
                const gchar  *body,
                gsize         len)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)     error = NULL;
	JsonNode             *root;
	JsonObject           *msg;
	const gchar          *method;

	if (!json_parser_load_from_data (parser, body, (gssize) len, &error))
	{
		g_debug ("lsp: unparsable message: %s", error->message);
		return;
	}
	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
		return;
	msg = json_node_get_object (root);
	method = obj_string (msg, "method");

	if (method == NULL)
	{
		handle_answer (self, msg);
		return;
	}
	if (json_object_has_member (msg, "id"))
	{
		answer_server_request (self, json_object_get_member (msg, "id"), method, obj_object (msg, "params"));
		return;
	}
	if (g_strcmp0 (method, "textDocument/publishDiagnostics") == 0)
		handle_diagnostics (self, obj_object (msg, "params"));
	else if (g_strcmp0 (method, "window/logMessage") == 0 || g_strcmp0 (method, "window/showMessage") == 0)
	{
		JsonObject *params = obj_object (msg, "params");

		post (self, POST_LOG, obj_string (params, "message"), (gint) obj_int (params, "type", 4));
	}
}

static gpointer
reader_main (gpointer data)
{
	LrgLspClient *self = data;

	for (;;)
	{
		gssize   length = -1;
		gchar   *body;
		gsize    got = 0;

		/* headers up to the blank line */
		for (;;)
		{
			g_autofree gchar *line = NULL;
			gsize             n = 0;

			line = g_data_input_stream_read_line (self->in, &n, self->cancel, NULL);
			if (line == NULL)
				goto eof;
			if (n > 0 && line[n - 1] == '\r')
				line[--n] = '\0';
			if (n == 0)
			{
				if (length >= 0)
					break;
				continue;
			}
			if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
				length = (gssize) g_ascii_strtoull (line + 15, NULL, 10);
		}

		/* the body */
		body = g_malloc ((gsize) length + 1);
		if (!g_input_stream_read_all (G_INPUT_STREAM (self->in), body, (gsize) length, &got, self->cancel, NULL) ||
		    got != (gsize) length)
		{
			g_free (body);
			goto eof;
		}
		body[length] = '\0';
		handle_message (self, body, (gsize) length);
		g_free (body);
	}

eof:
	g_atomic_int_set (&self->reader_done, TRUE);
	fail_all (self, "The language server exited");
	return NULL;
}

/* ==========================================================================
 * Process life cycle
 * ========================================================================== */

typedef struct
{
	GWeakRef ref;
	guint    generation;
} WaitData;

static void
on_process_exited (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	WaitData                *wd = user_data;
	g_autoptr(LrgLspClient)  self = g_weak_ref_get (&wd->ref);
	GSubprocess             *proc = G_SUBPROCESS (source);
	gint                     status = 0;

	g_subprocess_wait_finish (proc, result, NULL);
	if (self != NULL && wd->generation == self->generation && self->proc == proc)
	{
		gboolean changed;

		if (g_subprocess_get_if_exited (proc))
			status = g_subprocess_get_exit_status (proc);
		else if (g_subprocess_get_if_signaled (proc))
			status = 128 + g_subprocess_get_term_sig (proc);
		g_mutex_lock (&self->lock);
		changed = self->state != LRG_LSP_CLIENT_STATE_STOPPED && self->state != LRG_LSP_CLIENT_STATE_EXITED;
		if (changed)
			self->state = LRG_LSP_CLIENT_STATE_EXITED;
		g_cond_broadcast (&self->cond);
		g_mutex_unlock (&self->lock);
		if (changed)
		{
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
			g_signal_emit (self, signals[SIGNAL_EXITED], 0, status);
		}
	}
	g_weak_ref_clear (&wd->ref);
	g_free (wd);
}

static JsonObject *
client_capabilities (void)
{
	JsonObject *caps = json_object_new ();
	JsonObject *general = json_object_new ();
	JsonArray  *encodings = json_array_new ();
	JsonObject *td = json_object_new ();
	JsonObject *sync = json_object_new ();
	JsonObject *completion = json_object_new ();
	JsonObject *item = json_object_new ();
	JsonObject *hover = json_object_new ();
	JsonArray  *formats = json_array_new ();
	JsonObject *diags = json_object_new ();
	JsonObject *ws = json_object_new ();
	JsonObject *window = json_object_new ();

	json_array_add_string_element (encodings, "utf-32");
	json_array_add_string_element (encodings, "utf-16");
	json_object_set_array_member (general, "positionEncodings", encodings);
	json_object_set_object_member (caps, "general", general);

	json_object_set_boolean_member (sync, "didSave", TRUE);
	json_object_set_boolean_member (sync, "dynamicRegistration", FALSE);
	json_object_set_object_member (td, "synchronization", sync);
	json_object_set_boolean_member (item, "snippetSupport", FALSE);
	json_object_set_object_member (completion, "completionItem", item);
	json_object_set_object_member (td, "completion", completion);
	json_array_add_string_element (formats, "plaintext");
	json_array_add_string_element (formats, "markdown");
	json_object_set_array_member (hover, "contentFormat", formats);
	json_object_set_object_member (td, "hover", hover);
	json_object_set_object_member (td, "definition", json_object_new ());
	json_object_set_object_member (td, "references", json_object_new ());
	json_object_set_object_member (td, "rename", json_object_new ());
	json_object_set_object_member (td, "formatting", json_object_new ());
	json_object_set_boolean_member (diags, "relatedInformation", FALSE);
	json_object_set_object_member (td, "publishDiagnostics", diags);
	json_object_set_object_member (caps, "textDocument", td);

	json_object_set_boolean_member (ws, "configuration", TRUE);
	json_object_set_boolean_member (ws, "workspaceFolders", TRUE);
	json_object_set_object_member (caps, "workspace", ws);
	json_object_set_boolean_member (window, "workDoneProgress", FALSE);
	json_object_set_object_member (caps, "window", window);
	return caps;
}

gboolean
lrg_lsp_client_start (LrgLspClient  *self,
                      GError       **error)
{
	g_autoptr(GSubprocessLauncher) launcher = NULL;
	JsonObject                    *msg;
	JsonObject                    *params;
	WaitData                      *wd;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), FALSE);

	if (self->state == LRG_LSP_CLIENT_STATE_STARTING || self->state == LRG_LSP_CLIENT_STATE_READY)
		return TRUE;
	if (self->proc != NULL)
		lrg_lsp_client_stop (self);
	if (self->argv == NULL || self->argv[0] == NULL)
	{
		g_set_error (error, LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_NOT_RUNNING,
		             "No command line for the %s language server", self->language_id);
		return FALSE;
	}

	/*
	 * A dead server turns our next write into SIGPIPE, which kills the
	 * process by default. Ignore it (only when nobody installed a
	 * handler) so the write fails with EPIPE instead.
	 */
	{
		struct sigaction old;

		if (sigaction (SIGPIPE, NULL, &old) == 0 && old.sa_handler == SIG_DFL)
			signal (SIGPIPE, SIG_IGN);
	}

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	if (self->root_path != NULL && g_file_test (self->root_path, G_FILE_TEST_IS_DIR))
		g_subprocess_launcher_set_cwd (launcher, self->root_path);
	self->proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) self->argv, error);
	if (self->proc == NULL)
		return FALSE;

	self->generation++;
	self->out = g_object_ref (g_subprocess_get_stdin_pipe (self->proc));
	self->in = g_data_input_stream_new (g_subprocess_get_stdout_pipe (self->proc));
	g_data_input_stream_set_newline_type (self->in, G_DATA_STREAM_NEWLINE_TYPE_LF);
	g_buffered_input_stream_set_buffer_size (G_BUFFERED_INPUT_STREAM (self->in), 64 * 1024);
	self->cancel = g_cancellable_new ();
	self->outbox = g_async_queue_new ();
	g_clear_pointer (&self->context, g_main_context_unref);
	self->context = g_main_context_ref_thread_default ();
	g_atomic_int_set (&self->reader_done, FALSE);

	wd = g_new0 (WaitData, 1);
	g_weak_ref_init (&wd->ref, self);
	wd->generation = self->generation;
	g_subprocess_wait_async (self->proc, NULL, on_process_exited, wd);

	/* initialize goes out first, the rest waits for its answer */
	params = json_object_new ();
	json_object_set_int_member (params, "processId", (gint64) getpid ());
	{
		JsonObject *info = json_object_new ();

		json_object_set_string_member (info, "name", "lrgeditor");
		json_object_set_string_member (info, "version", LRG_VERSION_STRING);
		json_object_set_object_member (params, "clientInfo", info);
	}
	if (self->root_path != NULL)
	{
		g_autofree gchar *uri = g_filename_to_uri (self->root_path, NULL, NULL);
		g_autofree gchar *name = g_path_get_basename (self->root_path);
		JsonArray        *folders = json_array_new ();
		JsonObject       *folder = json_object_new ();

		json_object_set_string_member (params, "rootUri", uri);
		json_object_set_string_member (params, "rootPath", self->root_path);
		json_object_set_string_member (folder, "uri", uri);
		json_object_set_string_member (folder, "name", name);
		json_array_add_object_element (folders, folder);
		json_object_set_array_member (params, "workspaceFolders", folders);
	}
	else
		json_object_set_null_member (params, "rootUri");
	json_object_set_object_member (params, "capabilities", client_capabilities ());

	g_mutex_lock (&self->lock);
	g_clear_error (&self->init_error);
	g_clear_pointer (&self->capabilities, json_node_unref);
	self->utf32 = FALSE;
	self->state = LRG_LSP_CLIENT_STATE_STARTING;
	self->init_id = ++self->next_id;
	msg = json_object_new ();
	json_object_set_int_member (msg, "id", self->init_id);
	json_object_set_string_member (msg, "method", "initialize");
	json_object_set_object_member (msg, "params", params);
	enqueue_locked (self, frame (msg), TRUE, NULL);
	g_mutex_unlock (&self->lock);
	json_object_unref (msg);

	self->writer = g_thread_new ("lrg-lsp-writer", writer_main, self);
	self->reader = g_thread_new ("lrg-lsp-reader", reader_main, self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
	return TRUE;
}

gboolean
lrg_lsp_client_wait_ready (LrgLspClient  *self,
                           guint          timeout_ms,
                           GError       **error)
{
	gint64   deadline;
	gboolean ok;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), FALSE);

	deadline = g_get_monotonic_time () + (gint64) timeout_ms * G_TIME_SPAN_MILLISECOND;
	g_mutex_lock (&self->lock);
	while (self->state == LRG_LSP_CLIENT_STATE_STARTING && !g_atomic_int_get (&self->reader_done))
		if (!g_cond_wait_until (&self->cond, &self->lock, deadline))
			break;
	ok = self->state == LRG_LSP_CLIENT_STATE_READY;
	if (!ok)
	{
		if (self->init_error != NULL)
			g_propagate_error (error, g_error_copy (self->init_error));
		else if (self->state == LRG_LSP_CLIENT_STATE_STARTING && !g_atomic_int_get (&self->reader_done))
			g_set_error (error, LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_TIMED_OUT,
			             "The %s language server did not answer initialize", self->language_id);
		else
			g_set_error (error, LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_EXITED,
			             "The %s language server is not running", self->language_id);
	}
	g_mutex_unlock (&self->lock);
	return ok;
}

void
lrg_lsp_client_stop (LrgLspClient *self)
{
	LrgLspClientState state;
	gint              waited;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));

	if (self->proc == NULL)
		return;

	g_mutex_lock (&self->lock);
	state = self->state;
	g_mutex_unlock (&self->lock);

	/* 1. Polite: shutdown + exit while the server still listens */
	if (state == LRG_LSP_CLIENT_STATE_READY && !g_atomic_int_get (&self->reader_done))
	{
		g_autoptr(JsonNode) r = lrg_lsp_client_request_sync (self, "shutdown", NULL, INIT_STOP_WAIT_MS, NULL);

		lrg_lsp_client_notify (self, "exit", NULL);
	}

	/* 2. Stop sending, close stdin */
	g_mutex_lock (&self->lock);
	self->state = LRG_LSP_CLIENT_STATE_STOPPED;
	g_ptr_array_set_size (self->held, 0);
	g_mutex_unlock (&self->lock);
	g_async_queue_push (self->outbox, QUIT);
	g_thread_join (self->writer);
	self->writer = NULL;
	g_output_stream_close (self->out, NULL, NULL);

	/* 3. Give it a second to leave, then kill */
	for (waited = 0; waited < EXIT_WAIT_MS && !g_atomic_int_get (&self->reader_done); waited += 5)
		g_usleep (5 * G_TIME_SPAN_MILLISECOND);
	g_subprocess_force_exit (self->proc);
	g_cancellable_cancel (self->cancel);
	g_thread_join (self->reader);
	self->reader = NULL;
	fail_all (self, "The language server was stopped");

	g_clear_object (&self->in);
	g_clear_object (&self->out);
	g_clear_object (&self->cancel);
	g_clear_pointer (&self->outbox, g_async_queue_unref);
	g_clear_object (&self->proc);

	g_mutex_lock (&self->lock);
	g_hash_table_remove_all (self->docs);
	g_hash_table_remove_all (self->diags);
	g_mutex_unlock (&self->lock);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_lsp_client_dispose (GObject *object)
{
	LrgLspClient *self = LRG_LSP_CLIENT (object);

	lrg_lsp_client_stop (self);
	G_OBJECT_CLASS (lrg_lsp_client_parent_class)->dispose (object);
}

static void
lrg_lsp_client_finalize (GObject *object)
{
	LrgLspClient *self = LRG_LSP_CLIENT (object);

	g_free (self->language_id);
	g_strfreev (self->argv);
	g_free (self->root_path);
	g_free (self->server_name);
	g_clear_pointer (&self->capabilities, json_node_unref);
	g_clear_error (&self->init_error);
	g_clear_pointer (&self->context, g_main_context_unref);
	g_hash_table_unref (self->pending);
	g_ptr_array_unref (self->held);
	g_hash_table_unref (self->docs);
	g_hash_table_unref (self->diags);
	g_mutex_clear (&self->lock);
	g_cond_clear (&self->cond);
	G_OBJECT_CLASS (lrg_lsp_client_parent_class)->finalize (object);
}

static void
lrg_lsp_client_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgLspClient *self = LRG_LSP_CLIENT (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE_ID:
		g_value_set_string (value, self->language_id);
		break;
	case PROP_ARGV:
		g_value_set_boxed (value, self->argv);
		break;
	case PROP_ROOT_PATH:
		g_value_set_string (value, self->root_path);
		break;
	case PROP_STATE:
		g_value_set_enum (value, lrg_lsp_client_get_state (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lsp_client_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgLspClient *self = LRG_LSP_CLIENT (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE_ID:
		g_free (self->language_id);
		self->language_id = g_value_dup_string (value);
		break;
	case PROP_ARGV:
		g_strfreev (self->argv);
		self->argv = g_value_dup_boxed (value);
		break;
	case PROP_ROOT_PATH:
		lrg_lsp_client_set_root_path (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lsp_client_class_init (LrgLspClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_lsp_client_dispose;
	object_class->finalize = lrg_lsp_client_finalize;
	object_class->get_property = lrg_lsp_client_get_property;
	object_class->set_property = lrg_lsp_client_set_property;

	/**
	 * LrgLspClient:language-id:
	 *
	 * The LSP language id sent with didOpen ("c", "lua", "python" ...).
	 */
	properties[PROP_LANGUAGE_ID] =
		g_param_spec_string ("language-id", NULL, NULL, "plaintext",
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLspClient:argv:
	 *
	 * The server's command line.
	 */
	properties[PROP_ARGV] =
		g_param_spec_boxed ("argv", NULL, NULL, G_TYPE_STRV,
		                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLspClient:root-path:
	 *
	 * The workspace folder (used at the next start).
	 */
	properties[PROP_ROOT_PATH] =
		g_param_spec_string ("root-path", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLspClient:state:
	 *
	 * The life-cycle state; notified in the client's main context.
	 */
	properties[PROP_STATE] =
		g_param_spec_enum ("state", NULL, NULL, LRG_TYPE_LSP_CLIENT_STATE, LRG_LSP_CLIENT_STATE_STOPPED,
		                   G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgLspClient::ready:
	 * @self: the client
	 *
	 * The server answered `initialize`: capabilities are known and the
	 * held messages went out.
	 */
	signals[SIGNAL_READY] =
		g_signal_new ("ready", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgLspClient::exited:
	 * @self: the client
	 * @status: the exit status (128 + signal number when killed)
	 *
	 * The server process ended on its own (not through
	 * lrg_lsp_client_stop()).
	 */
	signals[SIGNAL_EXITED] =
		g_signal_new ("exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_INT);

	/**
	 * LrgLspClient::diagnostics-changed:
	 * @self: the client
	 * @path: the file whose diagnostics were published
	 *
	 * New diagnostics for @path; read them with
	 * lrg_lsp_client_get_diagnostics().
	 */
	signals[SIGNAL_DIAGNOSTICS_CHANGED] =
		g_signal_new ("diagnostics-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		              NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgLspClient::log-message:
	 * @self: the client
	 * @type: the LSP MessageType (1 error, 2 warning, 3 info, 4 log)
	 * @message: the text
	 *
	 * `window/logMessage` or `window/showMessage` from the server.
	 */
	signals[SIGNAL_LOG_MESSAGE] =
		g_signal_new ("log-message", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
}

static void
lrg_lsp_client_init (LrgLspClient *self)
{
	g_mutex_init (&self->lock);
	g_cond_init (&self->cond);
	self->state = LRG_LSP_CLIENT_STATE_STOPPED;
	self->pending = g_hash_table_new (g_int64_hash, g_int64_equal);
	self->held = g_ptr_array_new_with_free_func (g_free);
	self->docs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, doc_free);
	self->diags = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
}

LrgLspClient *
lrg_lsp_client_new (const gchar        *language_id,
                    const gchar * const *argv)
{
	g_return_val_if_fail (language_id != NULL, NULL);
	g_return_val_if_fail (argv != NULL, NULL);

	return g_object_new (LRG_TYPE_LSP_CLIENT, "language-id", language_id, "argv", argv, NULL);
}

const gchar *
lrg_lsp_client_get_language_id (LrgLspClient *self)
{
	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	return self->language_id;
}

const gchar * const *
lrg_lsp_client_get_argv (LrgLspClient *self)
{
	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	return (const gchar * const *) self->argv;
}

void
lrg_lsp_client_set_root_path (LrgLspClient *self,
                              const gchar  *path)
{
	g_return_if_fail (LRG_IS_LSP_CLIENT (self));

	if (g_strcmp0 (self->root_path, path) == 0)
		return;
	g_free (self->root_path);
	self->root_path = g_strdup (path);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROOT_PATH]);
}

const gchar *
lrg_lsp_client_get_root_path (LrgLspClient *self)
{
	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	return self->root_path;
}

LrgLspClientState
lrg_lsp_client_get_state (LrgLspClient *self)
{
	LrgLspClientState state;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), LRG_LSP_CLIENT_STATE_STOPPED);
	g_mutex_lock (&self->lock);
	state = self->state;
	g_mutex_unlock (&self->lock);
	return state;
}

const gchar *
lrg_lsp_client_get_server_name (LrgLspClient *self)
{
	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);

	if (self->server_name == NULL && self->argv != NULL && self->argv[0] != NULL)
		self->server_name = g_path_get_basename (self->argv[0]);
	return self->server_name;
}

gboolean
lrg_lsp_client_has_capability (LrgLspClient *self,
                               const gchar  *name)
{
	gboolean has = FALSE;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	g_mutex_lock (&self->lock);
	if (self->capabilities != NULL)
	{
		JsonObject *caps = json_node_get_object (self->capabilities);

		if (json_object_has_member (caps, name))
		{
			JsonNode *n = json_object_get_member (caps, name);

			has = !JSON_NODE_HOLDS_NULL (n) &&
			      !(JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_BOOLEAN &&
			        !json_node_get_boolean (n));
		}
	}
	g_mutex_unlock (&self->lock);
	return has;
}

gchar *
lrg_lsp_client_get_trigger_characters (LrgLspClient *self)
{
	GString *out = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);

	g_mutex_lock (&self->lock);
	if (self->capabilities != NULL)
	{
		JsonObject *provider = obj_object (json_node_get_object (self->capabilities), "completionProvider");
		JsonArray  *chars = obj_array (provider, "triggerCharacters");
		guint       i;

		for (i = 0; chars != NULL && i < json_array_get_length (chars); i++)
		{
			const gchar *c = json_array_get_string_element (chars, i);

			if (c == NULL)
				continue;
			if (out == NULL)
				out = g_string_new (NULL);
			g_string_append (out, c);
		}
	}
	g_mutex_unlock (&self->lock);
	return out != NULL ? g_string_free (out, FALSE) : NULL;
}

/* ==========================================================================
 * Document sync
 * ========================================================================== */

static JsonObject *
text_document (const gchar *path)
{
	JsonObject       *td = json_object_new ();
	g_autofree gchar *uri = g_filename_to_uri (path, NULL, NULL);

	json_object_set_string_member (td, "uri", uri != NULL ? uri : path);
	return td;
}

/* Sends a notification built by the caller (takes @params). */
static void
send_note (LrgLspClient *self,
           const gchar  *method,
           JsonObject   *params)
{
	JsonObject *msg = json_object_new ();

	json_object_set_string_member (msg, "method", method);
	if (params != NULL)
		json_object_set_object_member (msg, "params", params);
	g_mutex_lock (&self->lock);
	enqueue_locked (self, frame (msg), FALSE, NULL);
	g_mutex_unlock (&self->lock);
	json_object_unref (msg);
}

void
lrg_lsp_client_did_open (LrgLspClient *self,
                         const gchar  *path,
                         const gchar  *text)
{
	JsonObject *params;
	JsonObject *td;
	Doc        *doc;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);
	g_return_if_fail (text != NULL);

	g_mutex_lock (&self->lock);
	doc = g_hash_table_lookup (self->docs, path);
	g_mutex_unlock (&self->lock);
	if (doc != NULL)
	{
		lrg_lsp_client_did_change (self, path, text);
		return;
	}

	doc = g_new0 (Doc, 1);
	doc->text = g_strdup (text);
	doc->version = 1;
	g_mutex_lock (&self->lock);
	g_hash_table_replace (self->docs, g_strdup (path), doc);
	g_mutex_unlock (&self->lock);

	params = json_object_new ();
	td = text_document (path);
	json_object_set_string_member (td, "languageId", self->language_id);
	json_object_set_int_member (td, "version", 1);
	json_object_set_string_member (td, "text", text);
	json_object_set_object_member (params, "textDocument", td);
	send_note (self, "textDocument/didOpen", params);
}

void
lrg_lsp_client_did_change (LrgLspClient *self,
                           const gchar  *path,
                           const gchar  *text)
{
	JsonObject *params;
	JsonObject *td;
	JsonArray  *changes;
	JsonObject *change;
	Doc        *doc;
	gint        version;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);
	g_return_if_fail (text != NULL);

	g_mutex_lock (&self->lock);
	doc = g_hash_table_lookup (self->docs, path);
	if (doc != NULL)
	{
		g_free (doc->text);
		doc->text = g_strdup (text);
		version = ++doc->version;
	}
	g_mutex_unlock (&self->lock);
	if (doc == NULL)
	{
		lrg_lsp_client_did_open (self, path, text);
		return;
	}

	params = json_object_new ();
	td = text_document (path);
	json_object_set_int_member (td, "version", version);
	json_object_set_object_member (params, "textDocument", td);
	changes = json_array_new ();
	change = json_object_new ();
	json_object_set_string_member (change, "text", text);
	json_array_add_object_element (changes, change);
	json_object_set_array_member (params, "contentChanges", changes);
	send_note (self, "textDocument/didChange", params);
}

void
lrg_lsp_client_did_save (LrgLspClient *self,
                         const gchar  *path,
                         const gchar  *text)
{
	JsonObject *params;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);

	params = json_object_new ();
	json_object_set_object_member (params, "textDocument", text_document (path));
	if (text != NULL)
		json_object_set_string_member (params, "text", text);
	send_note (self, "textDocument/didSave", params);
}

void
lrg_lsp_client_did_close (LrgLspClient *self,
                          const gchar  *path)
{
	JsonObject *params;
	gboolean    was_open;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);

	g_mutex_lock (&self->lock);
	was_open = g_hash_table_remove (self->docs, path);
	g_hash_table_remove (self->diags, path);
	g_mutex_unlock (&self->lock);
	if (!was_open)
		return;
	params = json_object_new ();
	json_object_set_object_member (params, "textDocument", text_document (path));
	send_note (self, "textDocument/didClose", params);
}

gboolean
lrg_lsp_client_is_open (LrgLspClient *self,
                        const gchar  *path)
{
	gboolean open;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_mutex_lock (&self->lock);
	open = g_hash_table_contains (self->docs, path);
	g_mutex_unlock (&self->lock);
	return open;
}

gint
lrg_lsp_client_get_version (LrgLspClient *self,
                            const gchar  *path)
{
	Doc  *doc;
	gint  version;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), 0);
	g_return_val_if_fail (path != NULL, 0);
	g_mutex_lock (&self->lock);
	doc = g_hash_table_lookup (self->docs, path);
	version = doc != NULL ? doc->version : 0;
	g_mutex_unlock (&self->lock);
	return version;
}

GPtrArray *
lrg_lsp_client_get_diagnostics (LrgLspClient *self,
                                const gchar  *path)
{
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_diagnostic_free);
	GPtrArray *list;
	guint      i;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), out);
	g_return_val_if_fail (path != NULL, out);

	g_mutex_lock (&self->lock);
	list = g_hash_table_lookup (self->diags, path);
	for (i = 0; list != NULL && i < list->len; i++)
		g_ptr_array_add (out, lrg_lsp_diagnostic_copy (g_ptr_array_index (list, i)));
	g_mutex_unlock (&self->lock);
	return out;
}

/* ==========================================================================
 * Requests
 * ========================================================================== */

void
lrg_lsp_client_notify (LrgLspClient *self,
                       const gchar  *method,
                       JsonNode     *params)
{
	JsonObject *msg;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (method != NULL);

	msg = json_object_new ();
	json_object_set_string_member (msg, "method", method);
	if (params != NULL)
		json_object_set_member (msg, "params", json_node_copy (params));
	g_mutex_lock (&self->lock);
	enqueue_locked (self, frame (msg), FALSE, NULL);
	g_mutex_unlock (&self->lock);
	json_object_unref (msg);
}

/* Builds and queues a request; registers @p under a new id. */
static gboolean
send_request (LrgLspClient  *self,
              const gchar   *method,
              JsonNode      *params,
              Pending       *p,
              GError       **error)
{
	JsonObject *msg = json_object_new ();
	gboolean    ok;

	json_object_set_string_member (msg, "method", method);
	if (params != NULL)
		json_object_set_member (msg, "params", json_node_copy (params));
	g_mutex_lock (&self->lock);
	p->id = ++self->next_id;
	json_object_set_int_member (msg, "id", p->id);
	ok = enqueue_locked (self, frame (msg), FALSE, error);
	if (ok)
		g_hash_table_insert (self->pending, &p->id, p);
	g_mutex_unlock (&self->lock);
	json_object_unref (msg);
	return ok;
}

void
lrg_lsp_client_request_async (LrgLspClient        *self,
                              const gchar         *method,
                              JsonNode            *params,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
	GTask   *task;
	Pending *p;
	GError  *error = NULL;

	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (method != NULL);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, lrg_lsp_client_request_async);
	g_task_set_check_cancellable (task, TRUE);
	p = g_new0 (Pending, 1);
	p->task = task;
	if (!send_request (self, method, params, p, &error))
		pending_finish (p, NULL, error);
}

JsonNode *
lrg_lsp_client_request_finish (LrgLspClient  *self,
                               GAsyncResult  *result,
                               GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

JsonNode *
lrg_lsp_client_request_sync (LrgLspClient  *self,
                             const gchar   *method,
                             JsonNode      *params,
                             guint          timeout_ms,
                             GError       **error)
{
	Pending  *p;
	JsonNode *result = NULL;
	gint64    deadline;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (method != NULL, NULL);

	p = g_new0 (Pending, 1);
	if (!send_request (self, method, params, p, error))
	{
		g_free (p);
		return NULL;
	}

	deadline = g_get_monotonic_time () + (gint64) timeout_ms * G_TIME_SPAN_MILLISECOND;
	g_mutex_lock (&self->lock);
	while (!p->done)
		if (!g_cond_wait_until (&self->cond, &self->lock, deadline))
			break;
	if (!p->done)
	{
		/* timed out: unregister so a late answer is dropped */
		g_hash_table_remove (self->pending, &p->id);
		g_mutex_unlock (&self->lock);
		g_set_error (error, LRG_LSP_CLIENT_ERROR, LRG_LSP_CLIENT_ERROR_TIMED_OUT,
		             "%s timed out after %u ms", method, timeout_ms);
		g_free (p);
		return NULL;
	}
	g_mutex_unlock (&self->lock);
	if (p->error != NULL)
		g_propagate_error (error, p->error);
	else
		result = p->result;
	g_free (p);
	return result;
}

/* --- typed requests ------------------------------------------------------ */

/* {textDocument, position} params with the column converted. */
static JsonNode *
position_params (LrgLspClient *self,
                 const gchar  *path,
                 guint         line,
                 guint         column)
{
	JsonObject *params = json_object_new ();
	JsonObject *pos = json_object_new ();
	JsonNode   *node = json_node_new (JSON_NODE_OBJECT);

	json_object_set_object_member (params, "textDocument", text_document (path));
	json_object_set_int_member (pos, "line", line);
	json_object_set_int_member (pos, "character", to_server (self, path, line, column));
	json_object_set_object_member (params, "position", pos);
	json_node_take_object (node, params);
	return node;
}

/* MarkupContent / MarkedString / string -> text */
static void
append_markup (GString  *out,
               JsonNode *n)
{
	const gchar *value = NULL;
	gboolean     markdown = FALSE;

	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
		return;
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
			append_markup (out, json_array_get_element (a, i));
		return;
	}
	if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	{
		value = json_node_get_string (n);
		markdown = TRUE;
	}
	else if (JSON_NODE_HOLDS_OBJECT (n))
	{
		JsonObject *o = json_node_get_object (n);

		value = obj_string (o, "value");
		markdown = g_strcmp0 (obj_string (o, "kind"), "markdown") == 0;
	}
	if (value == NULL || *value == '\0')
		return;
	if (out->len > 0)
		g_string_append (out, "\n\n");
	if (markdown)
	{
		/* drop ``` fence lines, keep the code */
		g_auto(GStrv) lines = g_strsplit (value, "\n", -1);
		gboolean      first = TRUE;
		guint         i;

		for (i = 0; lines[i] != NULL; i++)
		{
			const gchar *l = lines[i];

			while (*l == ' ')
				l++;
			if (g_str_has_prefix (l, "```"))
				continue;
			if (!first)
				g_string_append_c (out, '\n');
			g_string_append (out, lines[i]);
			first = FALSE;
		}
	}
	else
		g_string_append (out, value);
}

static gchar *
parse_hover (JsonNode *result)
{
	GString *out = g_string_new (NULL);

	if (result != NULL && JSON_NODE_HOLDS_OBJECT (result))
	{
		JsonObject *o = json_node_get_object (result);

		if (json_object_has_member (o, "contents"))
			append_markup (out, json_object_get_member (o, "contents"));
	}
	g_strstrip (out->str);
	out->len = strlen (out->str);
	if (out->len == 0)
	{
		g_string_free (out, TRUE);
		return NULL;
	}
	return g_string_free (out, FALSE);
}

typedef struct
{
	LrgLspCompletionItem *item;
	gchar                *sort;
	guint                 index;
} SortedItem;

static gint
sorted_item_cmp (gconstpointer a,
                 gconstpointer b)
{
	const SortedItem *x = a;
	const SortedItem *y = b;
	gint              c = g_strcmp0 (x->sort, y->sort);

	if (c != 0)
		return c;
	return x->index < y->index ? -1 : (x->index > y->index ? 1 : 0);
}

static GPtrArray *
parse_completion (JsonNode *result)
{
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_completion_item_free);
	JsonArray *items = NULL;
	GArray    *sorted;
	guint      i;

	if (result == NULL)
		return out;
	if (JSON_NODE_HOLDS_ARRAY (result))
		items = json_node_get_array (result);
	else if (JSON_NODE_HOLDS_OBJECT (result))
		items = obj_array (json_node_get_object (result), "items");
	if (items == NULL)
		return out;

	sorted = g_array_new (FALSE, FALSE, sizeof (SortedItem));
	for (i = 0; i < json_array_get_length (items); i++)
	{
		JsonNode             *n = json_array_get_element (items, i);
		JsonObject           *o;
		JsonObject           *edit;
		LrgLspCompletionItem *item;
		const gchar          *text;
		SortedItem            s;

		if (!JSON_NODE_HOLDS_OBJECT (n))
			continue;
		o = json_node_get_object (n);
		if (obj_string (o, "label") == NULL)
			continue;
		item = g_new0 (LrgLspCompletionItem, 1);
		item->label = g_strdup (obj_string (o, "label"));
		item->kind = (gint) obj_int (o, "kind", 0);
		item->detail = g_strdup (obj_string (o, "detail"));
		if (json_object_has_member (o, "documentation"))
		{
			GString *doc = g_string_new (NULL);

			append_markup (doc, json_object_get_member (o, "documentation"));
			item->documentation = doc->len > 0 ? g_string_free (doc, FALSE) : (g_string_free (doc, TRUE), NULL);
		}
		edit = obj_object (o, "textEdit");
		text = obj_string (edit, "newText");
		if (text == NULL)
			text = obj_string (o, "insertText");
		if (text == NULL)
			text = item->label;
		item->insert_text = obj_int (o, "insertTextFormat", 1) == 2 ? strip_snippet (text) : g_strdup (text);
		s.item = item;
		s.sort = g_strdup (obj_string (o, "sortText") != NULL ? obj_string (o, "sortText") : "");
		s.index = i;
		g_array_append_val (sorted, s);
	}
	g_array_sort (sorted, sorted_item_cmp);
	for (i = 0; i < sorted->len; i++)
	{
		SortedItem *s = &g_array_index (sorted, SortedItem, i);

		g_ptr_array_add (out, s->item);
		g_free (s->sort);
	}
	g_array_unref (sorted);
	return out;
}

/* One Location or LocationLink object -> a location. */
static void
add_location (LrgLspClient *self,
              GPtrArray    *out,
              JsonObject   *o)
{
	const gchar      *uri = obj_string (o, "uri");
	JsonObject       *range = obj_object (o, "range");
	g_autofree gchar *path = NULL;
	LrgLspLocation   *loc;

	if (uri == NULL)
	{
		uri = obj_string (o, "targetUri");
		range = obj_object (o, "targetSelectionRange");
		if (range == NULL)
			range = obj_object (o, "targetRange");
	}
	path = uri_to_path (uri);
	if (path == NULL)
		return;
	loc = g_new0 (LrgLspLocation, 1);
	g_mutex_lock (&self->lock);
	read_range_locked (self, path, range, &loc->line, &loc->column, &loc->end_line, &loc->end_column);
	g_mutex_unlock (&self->lock);
	loc->path = g_steal_pointer (&path);
	g_ptr_array_add (out, loc);
}

static GPtrArray *
parse_locations (LrgLspClient *self,
                 JsonNode     *result)
{
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_location_free);
	guint      i;

	if (result == NULL)
		return out;
	if (JSON_NODE_HOLDS_OBJECT (result))
		add_location (self, out, json_node_get_object (result));
	else if (JSON_NODE_HOLDS_ARRAY (result))
	{
		JsonArray *a = json_node_get_array (result);

		for (i = 0; i < json_array_get_length (a); i++)
			if (JSON_NODE_HOLDS_OBJECT (json_array_get_element (a, i)))
				add_location (self, out, json_array_get_object_element (a, i));
	}
	return out;
}

/* TextEdit[] of @path -> edits. */
static void
add_edits (LrgLspClient *self,
           GPtrArray    *out,
           const gchar  *path,
           JsonArray    *edits)
{
	guint i;

	for (i = 0; edits != NULL && i < json_array_get_length (edits); i++)
	{
		JsonNode       *n = json_array_get_element (edits, i);
		JsonObject     *o;
		LrgLspTextEdit *e;

		if (!JSON_NODE_HOLDS_OBJECT (n))
			continue;
		o = json_node_get_object (n);
		e = g_new0 (LrgLspTextEdit, 1);
		e->path = g_strdup (path);
		g_mutex_lock (&self->lock);
		read_range_locked (self, path, obj_object (o, "range"), &e->line, &e->column, &e->end_line,
		                   &e->end_column);
		g_mutex_unlock (&self->lock);
		e->new_text = g_strdup (obj_string (o, "newText") != NULL ? obj_string (o, "newText") : "");
		g_ptr_array_add (out, e);
	}
}

static GPtrArray *
parse_workspace_edit (LrgLspClient *self,
                      JsonNode     *result)
{
	GPtrArray  *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_text_edit_free);
	JsonObject *o;
	JsonObject *changes;
	JsonArray  *doc_changes;
	guint       i;

	if (result == NULL || !JSON_NODE_HOLDS_OBJECT (result))
		return out;
	o = json_node_get_object (result);

	doc_changes = obj_array (o, "documentChanges");
	if (doc_changes != NULL)
	{
		for (i = 0; i < json_array_get_length (doc_changes); i++)
		{
			JsonNode         *n = json_array_get_element (doc_changes, i);
			JsonObject       *dc;
			g_autofree gchar *path = NULL;

			if (!JSON_NODE_HOLDS_OBJECT (n))
				continue;
			dc = json_node_get_object (n);
			if (json_object_has_member (dc, "kind"))
				continue; /* create / rename / delete file: not supported */
			path = uri_to_path (obj_string (obj_object (dc, "textDocument"), "uri"));
			if (path != NULL)
				add_edits (self, out, path, obj_array (dc, "edits"));
		}
		return out;
	}

	changes = obj_object (o, "changes");
	if (changes != NULL)
	{
		g_autoptr(GList) members = json_object_get_members (changes);
		GList           *l;

		for (l = members; l != NULL; l = l->next)
		{
			g_autofree gchar *path = uri_to_path (l->data);
			JsonNode         *n = json_object_get_member (changes, l->data);

			if (path != NULL && JSON_NODE_HOLDS_ARRAY (n))
				add_edits (self, out, path, json_node_get_array (n));
		}
	}
	return out;
}

/* Async typed requests: an outer task around request_async. */
typedef struct
{
	GTask     *task;
	ParseKind  kind;
} Typed;

static void
typed_done (GObject      *source,
            GAsyncResult *result,
            gpointer      user_data)
{
	Typed               *t = user_data;
	LrgLspClient        *self = LRG_LSP_CLIENT (source);
	g_autoptr(GError)    error = NULL;
	g_autoptr(JsonNode)  node = lrg_lsp_client_request_finish (self, result, &error);

	if (node == NULL && error != NULL)
		g_task_return_error (t->task, g_steal_pointer (&error));
	else
	{
		switch (t->kind)
		{
		case PARSE_COMPLETION:
			g_task_return_pointer (t->task, parse_completion (node), (GDestroyNotify) g_ptr_array_unref);
			break;
		case PARSE_HOVER:
			g_task_return_pointer (t->task, parse_hover (node), g_free);
			break;
		case PARSE_LOCATIONS:
			g_task_return_pointer (t->task, parse_locations (self, node), (GDestroyNotify) g_ptr_array_unref);
			break;
		case PARSE_RAW:
		default:
			g_task_return_pointer (t->task, json_node_ref (node), (GDestroyNotify) json_node_unref);
			break;
		}
	}
	g_object_unref (t->task);
	g_free (t);
}

static void
typed_async (LrgLspClient        *self,
             const gchar         *method,
             ParseKind            kind,
             gpointer             tag,
             const gchar         *path,
             guint                line,
             guint                column,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
	Typed               *t = g_new0 (Typed, 1);
	g_autoptr(JsonNode)  params = position_params (self, path, line, column);

	t->task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (t->task, tag);
	t->kind = kind;
	lrg_lsp_client_request_async (self, method, params, cancellable, typed_done, t);
}

void
lrg_lsp_client_completion_async (LrgLspClient        *self,
                                 const gchar         *path,
                                 guint                line,
                                 guint                column,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);
	typed_async (self, "textDocument/completion", PARSE_COMPLETION, lrg_lsp_client_completion_async, path, line,
	             column, cancellable, callback, user_data);
}

GPtrArray *
lrg_lsp_client_completion_finish (LrgLspClient  *self,
                                  GAsyncResult  *result,
                                  GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

GPtrArray *
lrg_lsp_client_completion_sync (LrgLspClient  *self,
                                const gchar   *path,
                                guint          line,
                                guint          column,
                                guint          timeout_ms,
                                GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	params = position_params (self, path, line, column);
	node = lrg_lsp_client_request_sync (self, "textDocument/completion", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	return parse_completion (node);
}

void
lrg_lsp_client_hover_async (LrgLspClient        *self,
                            const gchar         *path,
                            guint                line,
                            guint                column,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);
	typed_async (self, "textDocument/hover", PARSE_HOVER, lrg_lsp_client_hover_async, path, line, column,
	             cancellable, callback, user_data);
}

gchar *
lrg_lsp_client_hover_finish (LrgLspClient  *self,
                             GAsyncResult  *result,
                             GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

gchar *
lrg_lsp_client_hover_sync (LrgLspClient  *self,
                           const gchar   *path,
                           guint          line,
                           guint          column,
                           guint          timeout_ms,
                           GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	params = position_params (self, path, line, column);
	node = lrg_lsp_client_request_sync (self, "textDocument/hover", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	return parse_hover (node);
}

void
lrg_lsp_client_definition_async (LrgLspClient        *self,
                                 const gchar         *path,
                                 guint                line,
                                 guint                column,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
	g_return_if_fail (LRG_IS_LSP_CLIENT (self));
	g_return_if_fail (path != NULL);
	typed_async (self, "textDocument/definition", PARSE_LOCATIONS, lrg_lsp_client_definition_async, path, line,
	             column, cancellable, callback, user_data);
}

GPtrArray *
lrg_lsp_client_definition_finish (LrgLspClient  *self,
                                  GAsyncResult  *result,
                                  GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

GPtrArray *
lrg_lsp_client_definition_sync (LrgLspClient  *self,
                                const gchar   *path,
                                guint          line,
                                guint          column,
                                guint          timeout_ms,
                                GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	params = position_params (self, path, line, column);
	node = lrg_lsp_client_request_sync (self, "textDocument/definition", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	return parse_locations (self, node);
}

GPtrArray *
lrg_lsp_client_references_sync (LrgLspClient  *self,
                                const gchar   *path,
                                guint          line,
                                guint          column,
                                gboolean       include_declaration,
                                guint          timeout_ms,
                                GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	JsonObject         *ctx;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	params = position_params (self, path, line, column);
	ctx = json_object_new ();
	json_object_set_boolean_member (ctx, "includeDeclaration", include_declaration);
	json_object_set_object_member (json_node_get_object (params), "context", ctx);
	node = lrg_lsp_client_request_sync (self, "textDocument/references", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	return parse_locations (self, node);
}

GPtrArray *
lrg_lsp_client_rename_sync (LrgLspClient  *self,
                            const gchar   *path,
                            guint          line,
                            guint          column,
                            const gchar   *new_name,
                            guint          timeout_ms,
                            GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (new_name != NULL, NULL);

	params = position_params (self, path, line, column);
	json_object_set_string_member (json_node_get_object (params), "newName", new_name);
	node = lrg_lsp_client_request_sync (self, "textDocument/rename", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	return parse_workspace_edit (self, node);
}

GPtrArray *
lrg_lsp_client_formatting_sync (LrgLspClient  *self,
                                const gchar   *path,
                                guint          tab_size,
                                gboolean       insert_spaces,
                                guint          timeout_ms,
                                GError       **error)
{
	g_autoptr(JsonNode) params = NULL;
	g_autoptr(JsonNode) node = NULL;
	JsonObject         *o;
	JsonObject         *opts;
	GPtrArray          *out;
	GError             *err = NULL;

	g_return_val_if_fail (LRG_IS_LSP_CLIENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	o = json_object_new ();
	json_object_set_object_member (o, "textDocument", text_document (path));
	opts = json_object_new ();
	json_object_set_int_member (opts, "tabSize", tab_size);
	json_object_set_boolean_member (opts, "insertSpaces", insert_spaces);
	json_object_set_object_member (o, "options", opts);
	params = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (params, o);
	node = lrg_lsp_client_request_sync (self, "textDocument/formatting", params, timeout_ms, &err);
	if (err != NULL)
	{
		g_propagate_error (error, err);
		return NULL;
	}
	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_lsp_text_edit_free);
	if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
		add_edits (self, out, path, json_node_get_array (node));
	return out;
}
