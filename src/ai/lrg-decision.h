/* lrg-decision.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Typed questions to a decision model, and its answers.
 *
 * A decision model (TypeSafe's Jev, a local Ollama decision model...)
 * answers typed questions about state the game supplies: a Noul (the
 * probability a statement is true), a Choice among named options, a
 * Score on described levels.  The answers are judgments; what the game
 * does with them is decided in code (a behavior tree reading them from
 * a blackboard, LrgBTJudgment), never by the model.
 *
 * LrgDecisionRequest is the state and the questions, as the System One
 * wire format (POST /v1/systemone) every backend of LrgDecider speaks;
 * LrgJudgments is the answers.
 */

#ifndef LRG_DECISION_H
#define LRG_DECISION_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"
#include "lrg-blackboard.h"

G_BEGIN_DECLS

/**
 * LRG_JUDGMENT_YES:
 *
 * The default probability at or above which a Noul reads as yes.
 */
#define LRG_JUDGMENT_YES (0.7)

/**
 * LRG_JUDGMENT_NO:
 *
 * The default probability at or below which a Noul reads as no.
 */
#define LRG_JUDGMENT_NO (0.3)

/**
 * LRG_DECIDER_ERROR:
 *
 * The error domain of decisions (#LrgDeciderError).
 */
#define LRG_DECIDER_ERROR (lrg_decider_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark
lrg_decider_error_quark (void);

#define LRG_TYPE_DECISION_REQUEST (lrg_decision_request_get_type ())

G_DECLARE_FINAL_TYPE (LrgDecisionRequest, lrg_decision_request, LRG, DECISION_REQUEST, GObject)

/**
 * lrg_decision_request_new:
 *
 * A request with an empty text state and no questions.
 *
 * Returns: (transfer full): a new #LrgDecisionRequest
 */
LRG_AVAILABLE_IN_ALL
LrgDecisionRequest *
lrg_decision_request_new (void);

/**
 * lrg_decision_request_set_state_text:
 * @self: an #LrgDecisionRequest
 * @text: what the questions are about, as text
 *
 * Sets the state to a string.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_request_set_state_text (LrgDecisionRequest *self,
                                     const gchar        *text);

/**
 * lrg_decision_request_set_state_json:
 * @self: an #LrgDecisionRequest
 * @json: a JSON object or array: named fields the questions refer to
 * @error: return location for a #GError
 *
 * Sets the state to structured data.  Named fields work better than a
 * serialized string: a question can name the field it is about
 * (`target.name`).
 *
 * Returns: %TRUE when @json is an object or an array
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_request_set_state_json (LrgDecisionRequest  *self,
                                     const gchar         *json,
                                     GError             **error);

/**
 * lrg_decision_request_add_noul:
 * @self: an #LrgDecisionRequest
 * @id: the question's name (the answer's)
 * @instructions: a yes/no question, one judgment
 * @true_criterion: (nullable): what counts as yes, as a situation
 * @false_criterion: (nullable): what counts as no, as a situation
 * @error: return location for a #GError
 *
 * Adds a Noul: the answer is the probability that the answer is yes.
 *
 * Returns: %TRUE when added (a new, non-empty @id and @instructions)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_request_add_noul (LrgDecisionRequest  *self,
                               const gchar         *id,
                               const gchar         *instructions,
                               const gchar         *true_criterion,
                               const gchar         *false_criterion,
                               GError             **error);

/**
 * lrg_decision_request_add_choice:
 * @self: an #LrgDecisionRequest
 * @id: the question's name
 * @instructions: the question
 * @options: (array zero-terminated=1): the options' names, which the model
 *   reads: names that say what they are (`caster`, not `option_1`)
 * @descriptions: (array zero-terminated=1) (nullable): each option's
 *   description, as many as @options; %NULL: the names alone
 * @error: return location for a #GError
 *
 * Adds a Choice: the answer is one option, with a probability for each.
 * A Choice always picks one: pair it with a Noul asking whether any fits.
 *
 * Returns: %TRUE when added (at least one option, all distinct)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_request_add_choice (LrgDecisionRequest  *self,
                                 const gchar         *id,
                                 const gchar         *instructions,
                                 const gchar * const *options,
                                 const gchar * const *descriptions,
                                 GError             **error);

/**
 * lrg_decision_request_add_score:
 * @self: an #LrgDecisionRequest
 * @id: the question's name
 * @instructions: the question
 * @levels: (array zero-terminated=1): two or more levels, lowest first,
 *   each a situation that reads on its own
 * @error: return location for a #GError
 *
 * Adds a Score: the answer is a position on the levels (0 the first),
 * with a probability for each.
 *
 * Returns: %TRUE when added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_request_add_score (LrgDecisionRequest  *self,
                                const gchar         *id,
                                const gchar         *instructions,
                                const gchar * const *levels,
                                GError             **error);

/**
 * lrg_decision_request_get_n_questions:
 * @self: an #LrgDecisionRequest
 *
 * Returns: the questions added
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_decision_request_get_n_questions (LrgDecisionRequest *self);

/**
 * lrg_decision_request_dup_json:
 * @self: an #LrgDecisionRequest
 * @model: (nullable): the model to name in it, %NULL none
 *
 * The request as the System One wire format: `{"model", "state",
 * "questions": {id: {"type", "instructions", "criteria"}}}`.
 *
 * Returns: (transfer full): the JSON
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_decision_request_dup_json (LrgDecisionRequest *self,
                               const gchar        *model);

#define LRG_TYPE_JUDGMENTS (lrg_judgments_get_type ())

G_DECLARE_FINAL_TYPE (LrgJudgments, lrg_judgments, LRG, JUDGMENTS, GObject)

/**
 * lrg_judgments_new_from_json:
 * @json: a System One response: `{"model", "answers": {id: {...}},
 *   "usage"}`
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the answers, or %NULL with
 *   @error when @json is no response
 */
LRG_AVAILABLE_IN_ALL
LrgJudgments *
lrg_judgments_new_from_json (const gchar  *json,
                             GError      **error);

/**
 * lrg_judgments_get_model:
 * @self: an #LrgJudgments
 *
 * Returns: (transfer none) (nullable): the model that answered (its
 *   version: `jev-1.14.0`)
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_judgments_get_model (LrgJudgments *self);

/**
 * lrg_judgments_has:
 * @self: an #LrgJudgments
 * @id: a question's name
 *
 * Returns: whether the question was answered
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_judgments_has (LrgJudgments *self,
                   const gchar  *id);

/**
 * lrg_judgments_get_noul:
 * @self: an #LrgJudgments
 * @id: a Noul's name
 *
 * Returns: the probability the answer is yes, 0 .. 1; -1 when @id is no
 *   Noul answered
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_judgments_get_noul (LrgJudgments *self,
                        const gchar  *id);

/**
 * lrg_judgments_get_band:
 * @self: an #LrgJudgments
 * @id: a Noul's name
 * @yes: the probability at or above which it is yes (%LRG_JUDGMENT_YES)
 * @no: the probability at or below which it is no (%LRG_JUDGMENT_NO)
 *
 * The Noul read against two thresholds: between them the state did not
 * settle the question, a row of its own in the code's decisions.
 *
 * Returns: the band, %LRG_JUDGMENT_BAND_MISSING without the answer
 */
LRG_AVAILABLE_IN_ALL
LrgJudgmentBand
lrg_judgments_get_band (LrgJudgments *self,
                        const gchar  *id,
                        gdouble       yes,
                        gdouble       no);

/**
 * lrg_judgments_get_choice:
 * @self: an #LrgJudgments
 * @id: a Choice's name
 *
 * Returns: (transfer none) (nullable): the option picked, %NULL when @id
 *   is no Choice answered
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_judgments_get_choice (LrgJudgments *self,
                          const gchar  *id);

/**
 * lrg_judgments_get_probability:
 * @self: an #LrgJudgments
 * @id: a Choice's or a Score's name
 * @option: an option's name, or a Score's level as its index ("0")
 *
 * Returns: the probability of @option, 0 .. 1; -1 when not answered
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_judgments_get_probability (LrgJudgments *self,
                               const gchar  *id,
                               const gchar  *option);

/**
 * lrg_judgments_get_score:
 * @self: an #LrgJudgments
 * @id: a Score's name
 *
 * Returns: the position on the levels (0 the first, fractional); -1
 *   when not answered
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_judgments_get_score (LrgJudgments *self,
                         const gchar  *id);

/**
 * lrg_judgments_get_tokens:
 * @self: an #LrgJudgments
 * @out_input: (out) (optional): the input tokens, -1 unknown
 * @out_output: (out) (optional): the output tokens, -1 unknown
 */
LRG_AVAILABLE_IN_ALL
void
lrg_judgments_get_tokens (LrgJudgments *self,
                          gint64       *out_input,
                          gint64       *out_output);

/**
 * lrg_judgments_dup_ids:
 * @self: an #LrgJudgments
 *
 * Returns: (transfer full) (array zero-terminated=1): the questions
 *   answered, by name
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_judgments_dup_ids (LrgJudgments *self);

/**
 * lrg_judgments_dup_json:
 * @self: an #LrgJudgments
 *
 * Returns: (transfer full): the response, as it came
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_judgments_dup_json (LrgJudgments *self);

/**
 * lrg_judgments_to_blackboard:
 * @self: an #LrgJudgments
 * @blackboard: the behavior tree's blackboard
 * @prefix: (nullable): before every key ("combat." gives "combat.flee")
 *
 * Writes every answer to @blackboard for the code's decisions: a Noul as
 * a float (the probability) at `PREFIXid`, a Choice as a string (the
 * option) at `PREFIXid`, a Score as a float (the position) at
 * `PREFIXid`.  LrgBTJudgment reads them.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_judgments_to_blackboard (LrgJudgments  *self,
                             LrgBlackboard *blackboard,
                             const gchar   *prefix);

G_END_DECLS

#endif /* LRG_DECISION_H */
