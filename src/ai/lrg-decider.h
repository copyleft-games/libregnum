/* lrg-decider.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A decision model, by name.
 *
 * LrgDecider sends an LrgDecisionRequest to a decision model and gives
 * back LrgJudgments.  The models are ai-glib's AiDecider backends (built
 * in with AI=1): "typesafe" (TypeSafe's Jev; "jev" is the same), "ollama"
 * (a local Ollama decision model such as tev1), "laya" (a Laya server).
 * A game names one in its settings and never sees ai-glib's types, so a
 * backend added to ai-glib is one more name here.
 *
 * The scripted decider answers from a function instead: tests, a replay
 * of a journal, a game played without a model.
 *
 * A failed call is an error, never an answer of "no".
 */

#ifndef LRG_DECIDER_H
#define LRG_DECIDER_H

#include <gio/gio.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-decision.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECIDER (lrg_decider_get_type ())

G_DECLARE_FINAL_TYPE (LrgDecider, lrg_decider, LRG, DECIDER, GObject)

/**
 * LrgDeciderScriptFunc:
 * @decider: the scripted decider
 * @request_json: the request, as lrg_decision_request_dup_json() wrote it
 * @user_data: the function's data
 * @error: return location for a #GError
 *
 * Answers a request as a decision model would.
 *
 * Returns: (transfer full) (nullable): the response JSON (`{"model",
 *   "answers"}`), or %NULL with @error
 */
typedef gchar *(*LrgDeciderScriptFunc) (LrgDecider   *decider,
                                        const gchar  *request_json,
                                        gpointer      user_data,
                                        GError      **error);

/**
 * lrg_decider_new:
 * @provider: (nullable): "typesafe" (or "jev"), "ollama", "laya"; %NULL
 *   or "auto": TypeSafe when `TYPESAFE_API_KEY` is set, else Ollama
 * @error: return location for a #GError
 *
 * A decider over one of ai-glib's decision models.  Their settings come
 * from the environment as ai-glib reads them (`TYPESAFE_API_KEY`,
 * `TYPESAFE_BASE_URL`, `OLLAMA_HOST`...); the model and the deadline are
 * the decider's properties.  No request is sent here.
 *
 * Returns: (transfer full) (nullable): the decider, or %NULL with @error:
 *   %LRG_DECIDER_ERROR_UNSUPPORTED in a build without ai-glib (AI=0) or
 *   for an unknown name
 */
LRG_AVAILABLE_IN_ALL
LrgDecider *
lrg_decider_new (const gchar  *provider,
                 GError      **error);

/**
 * lrg_decider_new_scripted:
 * @func: (scope notified): what answers
 * @user_data: its data
 * @destroy: (nullable): frees @user_data with the decider
 *
 * A decider that answers from @func, at once (on the next main loop
 * iteration, as a model's answer would come).
 *
 * Returns: (transfer full): the decider (its provider: "scripted")
 */
LRG_AVAILABLE_IN_ALL
LrgDecider *
lrg_decider_new_scripted (LrgDeciderScriptFunc func,
                          gpointer             user_data,
                          GDestroyNotify       destroy);

/**
 * lrg_decider_is_built_in:
 *
 * Returns: whether this libregnum has ai-glib's decision models (AI=1)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decider_is_built_in (void);

/**
 * lrg_decider_get_provider:
 * @self: an #LrgDecider
 *
 * Returns: (transfer none): "typesafe", "ollama", "laya" or "scripted"
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_decider_get_provider (LrgDecider *self);

/**
 * lrg_decider_get_model:
 * @self: an #LrgDecider
 *
 * Returns: (transfer none) (nullable): the model asked for (`jev-latest`,
 *   `tev1`), %NULL the backend's default
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_decider_get_model (LrgDecider *self);

/**
 * lrg_decider_set_model:
 * @self: an #LrgDecider
 * @model: (nullable): the model, %NULL the backend's default
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decider_set_model (LrgDecider  *self,
                       const gchar *model);

/**
 * lrg_decider_get_timeout_ms:
 * @self: an #LrgDecider
 *
 * Returns: the deadline of a decision, milliseconds
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_decider_get_timeout_ms (LrgDecider *self);

/**
 * lrg_decider_set_timeout_ms:
 * @self: an #LrgDecider
 * @timeout_ms: the deadline of a decision, milliseconds (> 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decider_set_timeout_ms (LrgDecider *self,
                            guint       timeout_ms);

/**
 * lrg_decider_decide_async:
 * @self: an #LrgDecider
 * @request: the state and the questions (read now: a question added later
 *   is not sent)
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called with the answer, on the thread-default
 *   main context of this call
 * @user_data: its data
 *
 * Asks the model.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decider_decide_async (LrgDecider          *self,
                          LrgDecisionRequest  *request,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data);

/**
 * lrg_decider_decide_finish:
 * @self: an #LrgDecider
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the answers, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgJudgments *
lrg_decider_decide_finish (LrgDecider    *self,
                           GAsyncResult  *result,
                           GError       **error);

G_END_DECLS

#endif /* LRG_DECIDER_H */
