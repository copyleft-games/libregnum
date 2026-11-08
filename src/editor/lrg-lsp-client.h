/* lrg-lsp-client.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLspClient: a Language Server Protocol client over stdio.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enums
 * ========================================================================== */

/**
 * LrgLspSeverity:
 * @LRG_LSP_SEVERITY_ERROR: an error (LSP DiagnosticSeverity 1)
 * @LRG_LSP_SEVERITY_WARNING: a warning (2)
 * @LRG_LSP_SEVERITY_INFORMATION: an informational message (3)
 * @LRG_LSP_SEVERITY_HINT: a hint (4)
 *
 * The severity of an #LrgLspDiagnostic. The values are the protocol's,
 * so a server's number converts with a cast.
 */
typedef enum
{
	LRG_LSP_SEVERITY_ERROR = 1,
	LRG_LSP_SEVERITY_WARNING = 2,
	LRG_LSP_SEVERITY_INFORMATION = 3,
	LRG_LSP_SEVERITY_HINT = 4
} LrgLspSeverity;

/**
 * lrg_lsp_severity_get_type:
 *
 * Returns: the #GType of #LrgLspSeverity
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_severity_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_SEVERITY (lrg_lsp_severity_get_type ())

/**
 * LrgLspClientState:
 * @LRG_LSP_CLIENT_STATE_STOPPED: not started (or stopped)
 * @LRG_LSP_CLIENT_STATE_STARTING: the server runs, `initialize` is pending
 * @LRG_LSP_CLIENT_STATE_READY: initialized: requests go out at once
 * @LRG_LSP_CLIENT_STATE_EXITED: the server process exited (or crashed)
 *
 * The life cycle of an #LrgLspClient.
 */
typedef enum
{
	LRG_LSP_CLIENT_STATE_STOPPED,
	LRG_LSP_CLIENT_STATE_STARTING,
	LRG_LSP_CLIENT_STATE_READY,
	LRG_LSP_CLIENT_STATE_EXITED
} LrgLspClientState;

/**
 * lrg_lsp_client_state_get_type:
 *
 * Returns: the #GType of #LrgLspClientState
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_client_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_CLIENT_STATE (lrg_lsp_client_state_get_type ())

/**
 * LRG_LSP_CLIENT_ERROR:
 *
 * Error domain of #LrgLspClient. Errors are #LrgLspClientError values,
 * except a server's own JSON-RPC error, whose code is the server's
 * (negative) number.
 */
#define LRG_LSP_CLIENT_ERROR (lrg_lsp_client_error_quark ())

/**
 * lrg_lsp_client_error_quark:
 *
 * Returns: the #GQuark of %LRG_LSP_CLIENT_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_lsp_client_error_quark (void);

/**
 * LrgLspClientError:
 * @LRG_LSP_CLIENT_ERROR_NOT_RUNNING: the server is not running
 * @LRG_LSP_CLIENT_ERROR_TIMED_OUT: no answer within the timeout
 * @LRG_LSP_CLIENT_ERROR_EXITED: the server exited before answering
 * @LRG_LSP_CLIENT_ERROR_PROTOCOL: the server sent something malformed
 * @LRG_LSP_CLIENT_ERROR_UNSUPPORTED: the server lacks the capability
 *
 * Client-side errors of #LrgLspClient.
 */
typedef enum
{
	LRG_LSP_CLIENT_ERROR_NOT_RUNNING = 1,
	LRG_LSP_CLIENT_ERROR_TIMED_OUT,
	LRG_LSP_CLIENT_ERROR_EXITED,
	LRG_LSP_CLIENT_ERROR_PROTOCOL,
	LRG_LSP_CLIENT_ERROR_UNSUPPORTED
} LrgLspClientError;

/* ==========================================================================
 * Boxed results
 * ========================================================================== */

/**
 * LrgLspDiagnostic:
 * @line: 0-based start line
 * @column: 0-based start column, in characters (code points)
 * @end_line: 0-based end line
 * @end_column: 0-based end column (exclusive), in characters
 * @severity: the severity (servers that omit it report errors)
 * @message: the message
 * @source: (nullable): who produced it ("clangd", "pyflakes" ...)
 * @code: (nullable): the diagnostic code as text
 *
 * One diagnostic of a document, as published by the server with
 * `textDocument/publishDiagnostics`. Positions are already converted
 * from the server's encoding (UTF-16 by default) to characters.
 */
typedef struct
{
	guint           line;
	guint           column;
	guint           end_line;
	guint           end_column;
	LrgLspSeverity  severity;
	gchar          *message;
	gchar          *source;
	gchar          *code;
} LrgLspDiagnostic;

/**
 * lrg_lsp_diagnostic_get_type:
 *
 * Returns: the boxed #GType of #LrgLspDiagnostic
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_diagnostic_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_DIAGNOSTIC (lrg_lsp_diagnostic_get_type ())

/**
 * lrg_lsp_diagnostic_copy:
 * @diagnostic: an #LrgLspDiagnostic
 *
 * Returns: (transfer full): a deep copy of @diagnostic
 */
LRG_AVAILABLE_IN_ALL
LrgLspDiagnostic * lrg_lsp_diagnostic_copy (const LrgLspDiagnostic *diagnostic);

/**
 * lrg_lsp_diagnostic_free:
 * @diagnostic: (nullable): an #LrgLspDiagnostic
 *
 * Frees @diagnostic and its strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_diagnostic_free (LrgLspDiagnostic *diagnostic);

/**
 * LrgLspLocation:
 * @path: absolute file path (from a `file://` URI)
 * @line: 0-based line
 * @column: 0-based column, in characters
 * @end_line: 0-based end line
 * @end_column: 0-based end column (exclusive)
 *
 * A place in a file: the answer of go-to-definition and find-references.
 */
typedef struct
{
	gchar *path;
	guint  line;
	guint  column;
	guint  end_line;
	guint  end_column;
} LrgLspLocation;

/**
 * lrg_lsp_location_get_type:
 *
 * Returns: the boxed #GType of #LrgLspLocation
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_location_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_LOCATION (lrg_lsp_location_get_type ())

/**
 * lrg_lsp_location_copy:
 * @location: an #LrgLspLocation
 *
 * Returns: (transfer full): a deep copy of @location
 */
LRG_AVAILABLE_IN_ALL
LrgLspLocation * lrg_lsp_location_copy (const LrgLspLocation *location);

/**
 * lrg_lsp_location_free:
 * @location: (nullable): an #LrgLspLocation
 *
 * Frees @location.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_location_free (LrgLspLocation *location);

/**
 * LrgLspCompletionItem:
 * @label: what the list shows
 * @kind: the LSP CompletionItemKind (1 text, 2 method, 3 function,
 *   6 variable, 14 keyword ...), 0 when the server gave none
 * @detail: (nullable): a short type or signature
 * @insert_text: the text that replaces the word being completed (the
 *   item's textEdit, else insertText, else label; snippet placeholders
 *   are reduced to their default text)
 * @documentation: (nullable): plain-text documentation
 *
 * One `textDocument/completion` candidate.
 */
typedef struct
{
	gchar *label;
	gint   kind;
	gchar *detail;
	gchar *insert_text;
	gchar *documentation;
} LrgLspCompletionItem;

/**
 * lrg_lsp_completion_item_get_type:
 *
 * Returns: the boxed #GType of #LrgLspCompletionItem
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_completion_item_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_COMPLETION_ITEM (lrg_lsp_completion_item_get_type ())

/**
 * lrg_lsp_completion_item_copy:
 * @item: an #LrgLspCompletionItem
 *
 * Returns: (transfer full): a deep copy of @item
 */
LRG_AVAILABLE_IN_ALL
LrgLspCompletionItem * lrg_lsp_completion_item_copy (const LrgLspCompletionItem *item);

/**
 * lrg_lsp_completion_item_free:
 * @item: (nullable): an #LrgLspCompletionItem
 *
 * Frees @item.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_completion_item_free (LrgLspCompletionItem *item);

/**
 * LrgLspTextEdit:
 * @path: absolute path of the file the edit applies to
 * @line: 0-based start line
 * @column: 0-based start column, in characters
 * @end_line: 0-based end line
 * @end_column: 0-based end column (exclusive)
 * @new_text: the replacement text
 *
 * One text replacement of a rename or a formatting answer. Apply a
 * file's edits together with lrg_lsp_apply_edits().
 */
typedef struct
{
	gchar *path;
	guint  line;
	guint  column;
	guint  end_line;
	guint  end_column;
	gchar *new_text;
} LrgLspTextEdit;

/**
 * lrg_lsp_text_edit_get_type:
 *
 * Returns: the boxed #GType of #LrgLspTextEdit
 */
LRG_AVAILABLE_IN_ALL
GType lrg_lsp_text_edit_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LSP_TEXT_EDIT (lrg_lsp_text_edit_get_type ())

/**
 * lrg_lsp_text_edit_copy:
 * @edit: an #LrgLspTextEdit
 *
 * Returns: (transfer full): a deep copy of @edit
 */
LRG_AVAILABLE_IN_ALL
LrgLspTextEdit * lrg_lsp_text_edit_copy (const LrgLspTextEdit *edit);

/**
 * lrg_lsp_text_edit_free:
 * @edit: (nullable): an #LrgLspTextEdit
 *
 * Frees @edit.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_text_edit_free (LrgLspTextEdit *edit);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/**
 * lrg_lsp_language_for_path:
 * @path: a file name or path
 *
 * The LSP language identifier for a file name: "c" (.c .h, which is
 * also what crispy scripts are), "lua", "python" (.py .pyw),
 * "javascript" (.js .mjs, GJS) or "typescript" (.ts).
 *
 * Returns: (nullable): a static language id, or %NULL for other files
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lsp_language_for_path (const gchar *path);

/**
 * lrg_lsp_find_server:
 * @language: an LSP language id (see lrg_lsp_language_for_path())
 *
 * Finds an installed language server for @language on `PATH`
 * (g_find_program_in_path()), trying in order: c: clangd; lua:
 * lua-language-server; python: pylsp, pyright-langserver --stdio,
 * basedpyright-langserver --stdio, jedi-language-server;
 * javascript / typescript: typescript-language-server --stdio.
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): the
 *   command line (argv[0] is the full path), or %NULL when none is
 *   installed
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_lsp_find_server (const gchar *language);

/**
 * lrg_lsp_apply_edits:
 * @text: the document text
 * @edits: (element-type LrgLspTextEdit): edits of that document
 *
 * Applies @edits to @text. The edits must not overlap (servers
 * guarantee it); they are applied from the last to the first so their
 * positions stay valid. Positions past the end clamp to it.
 *
 * Returns: (transfer full): the new text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_lsp_apply_edits (const gchar *text,
                             GPtrArray   *edits);

/* ==========================================================================
 * LrgLspClient
 * ========================================================================== */

#define LRG_TYPE_LSP_CLIENT (lrg_lsp_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLspClient, lrg_lsp_client, LRG, LSP_CLIENT, GObject)

/**
 * lrg_lsp_client_new:
 * @language_id: the LSP language id of the documents ("lua", "c" ...)
 * @argv: (array zero-terminated=1): the server's command line
 *
 * Creates a client for one language server process. Nothing runs until
 * lrg_lsp_client_start().
 *
 * Returns: (transfer full): a new #LrgLspClient
 */
LRG_AVAILABLE_IN_ALL
LrgLspClient * lrg_lsp_client_new (const gchar        *language_id,
                                   const gchar * const *argv);

/**
 * lrg_lsp_client_get_language_id:
 * @self: an #LrgLspClient
 *
 * Returns: (transfer none): the language id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lsp_client_get_language_id (LrgLspClient *self);

/**
 * lrg_lsp_client_get_argv:
 * @self: an #LrgLspClient
 *
 * Returns: (transfer none) (array zero-terminated=1): the server's
 *   command line
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_lsp_client_get_argv (LrgLspClient *self);

/**
 * lrg_lsp_client_set_root_path:
 * @self: an #LrgLspClient
 * @path: (nullable): the workspace folder (the project directory)
 *
 * Sets the workspace root sent with `initialize`. Takes effect at the
 * next start; the server's working directory is the root too.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_set_root_path (LrgLspClient *self,
                                   const gchar  *path);

/**
 * lrg_lsp_client_get_root_path:
 * @self: an #LrgLspClient
 *
 * Returns: (transfer none) (nullable): the workspace root
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lsp_client_get_root_path (LrgLspClient *self);

/**
 * lrg_lsp_client_get_state:
 * @self: an #LrgLspClient
 *
 * Returns: the life-cycle state
 */
LRG_AVAILABLE_IN_ALL
LrgLspClientState lrg_lsp_client_get_state (LrgLspClient *self);

/**
 * lrg_lsp_client_start:
 * @self: an #LrgLspClient
 * @error: return location for a #GError
 *
 * Spawns the server (stdin/stdout pipes, stderr discarded), starts the
 * reader thread and sends `initialize`. Returns at once: messages sent
 * before the server answers are queued and go out, in order, after the
 * `initialized` notification. #LrgLspClient::ready fires when the
 * server answered. Starting a running client does nothing.
 *
 * Returns: %TRUE if the process was spawned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lsp_client_start (LrgLspClient  *self,
                               GError       **error);

/**
 * lrg_lsp_client_wait_ready:
 * @self: an #LrgLspClient
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * Blocks until the server answered `initialize` (or exited). Does not
 * iterate any main context.
 *
 * Returns: %TRUE when the client is ready
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lsp_client_wait_ready (LrgLspClient  *self,
                                    guint          timeout_ms,
                                    GError       **error);

/**
 * lrg_lsp_client_stop:
 * @self: an #LrgLspClient
 *
 * Polite shutdown: `shutdown` (waits up to half a second), `exit`, then
 * the process is killed if it is still running after a second, and the
 * reader thread is joined. Pending requests fail with
 * %LRG_LSP_CLIENT_ERROR_EXITED. Open documents are forgotten.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_stop (LrgLspClient *self);

/**
 * lrg_lsp_client_get_server_name:
 * @self: an #LrgLspClient
 *
 * Returns: (transfer none) (nullable): the `serverInfo.name` of the
 *   initialize answer, else the program's basename
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lsp_client_get_server_name (LrgLspClient *self);

/**
 * lrg_lsp_client_has_capability:
 * @self: an #LrgLspClient
 * @name: a member of the server's capabilities, such as "hoverProvider"
 *
 * Whether the server announced @name with a value other than false or
 * null. Before the client is ready nothing is announced.
 *
 * Returns: %TRUE if the capability is present
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lsp_client_has_capability (LrgLspClient *self,
                                        const gchar  *name);

/**
 * lrg_lsp_client_get_trigger_characters:
 * @self: an #LrgLspClient
 *
 * Returns: (transfer full) (nullable): the server's completion trigger
 *   characters concatenated ("." ":" -> ".:"), or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_lsp_client_get_trigger_characters (LrgLspClient *self);

/* --- document sync ------------------------------------------------------ */

/**
 * lrg_lsp_client_did_open:
 * @self: an #LrgLspClient
 * @path: absolute file path
 * @text: the whole text
 *
 * Sends `textDocument/didOpen` (version 1) and remembers the text for
 * position conversion. Opening an open document sends a didChange.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_did_open (LrgLspClient *self,
                              const gchar  *path,
                              const gchar  *text);

/**
 * lrg_lsp_client_did_change:
 * @self: an #LrgLspClient
 * @path: absolute file path of an open document
 * @text: the whole new text
 *
 * Sends `textDocument/didChange` with the full text (allowed by every
 * sync kind) and bumps the version. Unknown documents are opened.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_did_change (LrgLspClient *self,
                                const gchar  *path,
                                const gchar  *text);

/**
 * lrg_lsp_client_did_save:
 * @self: an #LrgLspClient
 * @path: absolute file path of an open document
 * @text: (nullable): the saved text (sent when the server asked for it)
 *
 * Sends `textDocument/didSave`.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_did_save (LrgLspClient *self,
                              const gchar  *path,
                              const gchar  *text);

/**
 * lrg_lsp_client_did_close:
 * @self: an #LrgLspClient
 * @path: absolute file path of an open document
 *
 * Sends `textDocument/didClose` and drops the document's text and
 * diagnostics.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_did_close (LrgLspClient *self,
                               const gchar  *path);

/**
 * lrg_lsp_client_is_open:
 * @self: an #LrgLspClient
 * @path: absolute file path
 *
 * Returns: whether @path was opened with lrg_lsp_client_did_open()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lsp_client_is_open (LrgLspClient *self,
                                 const gchar  *path);

/**
 * lrg_lsp_client_get_version:
 * @self: an #LrgLspClient
 * @path: absolute file path
 *
 * Returns: the document's version (1 after didOpen, +1 per didChange),
 *   0 when it is not open
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lsp_client_get_version (LrgLspClient *self,
                                 const gchar  *path);

/**
 * lrg_lsp_client_get_diagnostics:
 * @self: an #LrgLspClient
 * @path: absolute file path
 *
 * The last diagnostics the server published for @path.
 *
 * Returns: (transfer container) (element-type LrgLspDiagnostic): a new
 *   array of copies (empty when there are none)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_get_diagnostics (LrgLspClient *self,
                                            const gchar  *path);

/* --- requests ----------------------------------------------------------- */

/**
 * lrg_lsp_client_notify:
 * @self: an #LrgLspClient
 * @method: the notification method
 * @params: (nullable): its params (not consumed)
 *
 * Sends a notification (no answer).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_notify (LrgLspClient *self,
                            const gchar  *method,
                            JsonNode     *params);

/**
 * lrg_lsp_client_request_async:
 * @self: an #LrgLspClient
 * @method: the request method
 * @params: (nullable): its params (not consumed)
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called in the thread-default main context
 *   of the caller with the answer
 * @user_data: data for @callback
 *
 * Sends a JSON-RPC request. A cancelled request is not cancelled on the
 * server; its answer is dropped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_request_async (LrgLspClient        *self,
                                   const gchar         *method,
                                   JsonNode            *params,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data);

/**
 * lrg_lsp_client_request_finish:
 * @self: an #LrgLspClient
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the `result` member (a null
 *   node for a null result), or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
JsonNode * lrg_lsp_client_request_finish (LrgLspClient  *self,
                                          GAsyncResult  *result,
                                          GError       **error);

/**
 * lrg_lsp_client_request_sync:
 * @self: an #LrgLspClient
 * @method: the request method
 * @params: (nullable): its params (not consumed)
 * @timeout_ms: how long to wait for the answer
 * @error: return location for a #GError
 *
 * Sends a request and blocks until its answer (the reader thread
 * delivers it; no main context is iterated, so this is safe in IPC
 * handlers and tests).
 *
 * Returns: (transfer full) (nullable): the `result` member, or %NULL
 *   with @error set
 */
LRG_AVAILABLE_IN_ALL
JsonNode * lrg_lsp_client_request_sync (LrgLspClient  *self,
                                        const gchar   *method,
                                        JsonNode      *params,
                                        guint          timeout_ms,
                                        GError       **error);

/**
 * lrg_lsp_client_completion_async:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called with the answer
 * @user_data: data for @callback
 *
 * Asks `textDocument/completion` at a position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_completion_async (LrgLspClient        *self,
                                      const gchar         *path,
                                      guint                line,
                                      guint                column,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data);

/**
 * lrg_lsp_client_completion_finish:
 * @self: an #LrgLspClient
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer container) (element-type LrgLspCompletionItem)
 *   (nullable): the candidates in the server's order (sortText when
 *   given), or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_completion_finish (LrgLspClient  *self,
                                              GAsyncResult  *result,
                                              GError       **error);

/**
 * lrg_lsp_client_completion_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * Blocking lrg_lsp_client_completion_async().
 *
 * Returns: (transfer container) (element-type LrgLspCompletionItem)
 *   (nullable): the candidates, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_completion_sync (LrgLspClient  *self,
                                            const gchar   *path,
                                            guint          line,
                                            guint          column,
                                            guint          timeout_ms,
                                            GError       **error);

/**
 * lrg_lsp_client_hover_async:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called with the answer
 * @user_data: data for @callback
 *
 * Asks `textDocument/hover` at a position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_hover_async (LrgLspClient        *self,
                                 const gchar         *path,
                                 guint                line,
                                 guint                column,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data);

/**
 * lrg_lsp_client_hover_finish:
 * @self: an #LrgLspClient
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * The hover contents flattened to plain text: markdown code fences are
 * dropped, marked strings joined with blank lines.
 *
 * Returns: (transfer full) (nullable): the text, or %NULL when there is
 *   nothing to show (no @error) or on failure (@error set)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_lsp_client_hover_finish (LrgLspClient  *self,
                                     GAsyncResult  *result,
                                     GError       **error);

/**
 * lrg_lsp_client_hover_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * Blocking lrg_lsp_client_hover_async().
 *
 * Returns: (transfer full) (nullable): the text, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_lsp_client_hover_sync (LrgLspClient  *self,
                                   const gchar   *path,
                                   guint          line,
                                   guint          column,
                                   guint          timeout_ms,
                                   GError       **error);

/**
 * lrg_lsp_client_definition_async:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called with the answer
 * @user_data: data for @callback
 *
 * Asks `textDocument/definition` at a position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lsp_client_definition_async (LrgLspClient        *self,
                                      const gchar         *path,
                                      guint                line,
                                      guint                column,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data);

/**
 * lrg_lsp_client_definition_finish:
 * @self: an #LrgLspClient
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer container) (element-type LrgLspLocation)
 *   (nullable): the definitions (Location, Location[] and LocationLink[]
 *   answers all become locations), or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_definition_finish (LrgLspClient  *self,
                                              GAsyncResult  *result,
                                              GError       **error);

/**
 * lrg_lsp_client_definition_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * Blocking lrg_lsp_client_definition_async().
 *
 * Returns: (transfer container) (element-type LrgLspLocation)
 *   (nullable): the definitions, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_definition_sync (LrgLspClient  *self,
                                            const gchar   *path,
                                            guint          line,
                                            guint          column,
                                            guint          timeout_ms,
                                            GError       **error);

/**
 * lrg_lsp_client_references_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @include_declaration: whether the declaration itself is listed
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * `textDocument/references`, blocking.
 *
 * Returns: (transfer container) (element-type LrgLspLocation)
 *   (nullable): the references, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_references_sync (LrgLspClient  *self,
                                            const gchar   *path,
                                            guint          line,
                                            guint          column,
                                            gboolean       include_declaration,
                                            guint          timeout_ms,
                                            GError       **error);

/**
 * lrg_lsp_client_rename_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @line: 0-based line
 * @column: 0-based column in characters
 * @new_name: the new name
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * `textDocument/rename`, blocking. The workspace edit (`changes` or
 * `documentChanges`) is flattened into text edits of possibly several
 * files; group them by #LrgLspTextEdit.path and apply each file's with
 * lrg_lsp_apply_edits().
 *
 * Returns: (transfer container) (element-type LrgLspTextEdit)
 *   (nullable): the edits, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_rename_sync (LrgLspClient  *self,
                                        const gchar   *path,
                                        guint          line,
                                        guint          column,
                                        const gchar   *new_name,
                                        guint          timeout_ms,
                                        GError       **error);

/**
 * lrg_lsp_client_formatting_sync:
 * @self: an #LrgLspClient
 * @path: absolute path of an open document
 * @tab_size: the tab width
 * @insert_spaces: whether to indent with spaces
 * @timeout_ms: how long to wait
 * @error: return location for a #GError
 *
 * `textDocument/formatting`, blocking.
 *
 * Returns: (transfer container) (element-type LrgLspTextEdit)
 *   (nullable): the edits (all of @path), or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lsp_client_formatting_sync (LrgLspClient  *self,
                                            const gchar   *path,
                                            guint          tab_size,
                                            gboolean       insert_spaces,
                                            guint          timeout_ms,
                                            GError       **error);

G_END_DECLS
