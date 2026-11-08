/* lrg-decision-brain.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A game's questions to a decision model, paced for a game loop.
 *
 * A game asks by topic ("combat", "spells", "chat"): one request per
 * topic is in flight at a time, so the frame loop asks every frame and
 * the model is asked again only once it answered.  The game reads the
 * latest answers of a topic (and how old they are) whenever it decides;
 * it never waits for one.  The answers also go to a blackboard, under
 * "TOPIC.", for a behavior tree's LrgBTJudgment conditions.
 *
 * Asked again with the same request within the cache's seconds, the
 * answer kept is used (a spell's role, a quest's text do not change).
 * Every request and its answer (or error) can be journaled as JSON
 * lines, the material to tune questions and thresholds with.
 */

#ifndef LRG_DECISION_BRAIN_H
#define LRG_DECISION_BRAIN_H

#include <gio/gio.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-decision.h"
#include "lrg-decider.h"
#include "lrg-blackboard.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECISION_BRAIN (lrg_decision_brain_get_type ())

G_DECLARE_FINAL_TYPE (LrgDecisionBrain, lrg_decision_brain, LRG, DECISION_BRAIN, GObject)

/**
 * lrg_decision_brain_new:
 * @decider: the decision model
 *
 * Returns: (transfer full): a brain with no topics, no cache (0 seconds),
 *   no journal
 */
LRG_AVAILABLE_IN_ALL
LrgDecisionBrain *
lrg_decision_brain_new (LrgDecider *decider);

/**
 * lrg_decision_brain_get_decider:
 * @self: an #LrgDecisionBrain
 *
 * Returns: (transfer none): the decision model
 */
LRG_AVAILABLE_IN_ALL
LrgDecider *
lrg_decision_brain_get_decider (LrgDecisionBrain *self);

/**
 * lrg_decision_brain_set_blackboard:
 * @self: an #LrgDecisionBrain
 * @blackboard: (nullable): where answers go, under "TOPIC."
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_brain_set_blackboard (LrgDecisionBrain *self,
                                   LrgBlackboard    *blackboard);

/**
 * lrg_decision_brain_set_cache_seconds:
 * @self: an #LrgDecisionBrain
 * @seconds: how long an answer is reused for the same request, 0 never
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_brain_set_cache_seconds (LrgDecisionBrain *self,
                                      gdouble           seconds);

/**
 * lrg_decision_brain_set_journal:
 * @self: an #LrgDecisionBrain
 * @path: (nullable): a JSON lines file every decision is added to
 *   (`{"time", "topic", "provider", "model", "elapsed_ms", "cached",
 *   "request", "response"}` or `"error"`), %NULL none
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_brain_set_journal (LrgDecisionBrain *self,
                                const gchar      *path);

/**
 * lrg_decision_brain_ask:
 * @self: an #LrgDecisionBrain
 * @topic: what the questions are for (a name, kept per topic)
 * @request: the state and the questions
 * @out_cached: (out) (optional): the answer was the cache's, there now
 * @error: return location for a #GError
 *
 * Asks the model, unless that topic is already being asked
 * (%LRG_DECIDER_ERROR_BUSY).  The answer comes on the main loop: the
 * topic's latest answers and #LrgDecisionBrain::answered (or
 * #LrgDecisionBrain::failed).  A cached answer is the topic's at once.
 *
 * Returns: %TRUE when asked (or answered from the cache)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_brain_ask (LrgDecisionBrain    *self,
                        const gchar         *topic,
                        LrgDecisionRequest  *request,
                        gboolean            *out_cached,
                        GError             **error);

/**
 * lrg_decision_brain_is_busy:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * Returns: whether the topic's question is out
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_brain_is_busy (LrgDecisionBrain *self,
                            const gchar      *topic);

/**
 * lrg_decision_brain_get_latest:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 * @out_age: (out) (optional): seconds since they came, -1 none
 *
 * Returns: (transfer none) (nullable): the topic's latest answers, %NULL
 *   none yet
 */
LRG_AVAILABLE_IN_ALL
LrgJudgments *
lrg_decision_brain_get_latest (LrgDecisionBrain *self,
                               const gchar      *topic,
                               gdouble          *out_age);

/**
 * lrg_decision_brain_get_last_error:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * Returns: (transfer none) (nullable): why the topic's last question
 *   failed, %NULL when it was answered
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_decision_brain_get_last_error (LrgDecisionBrain *self,
                                   const gchar      *topic);

/**
 * lrg_decision_brain_cancel:
 * @self: an #LrgDecisionBrain
 *
 * Cancels every question out; their answers never come.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_brain_cancel (LrgDecisionBrain *self);

/**
 * lrg_decision_brain_dup_status:
 * @self: an #LrgDecisionBrain
 *
 * Returns: (transfer full): the provider, the model, the totals (asked,
 *   answered from the cache, failed, tokens, mean seconds) and every
 *   topic (busy, the latest answers' age, the last error), as YAML
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_decision_brain_dup_status (LrgDecisionBrain *self);

/* --- inspector access (0.9.0) ------------------------------------------ */

/**
 * lrg_decision_brain_dup_topics:
 * @self: an #LrgDecisionBrain
 *
 * Every topic asked so far, in bytewise order: the rows of a decisions
 * inspector.
 *
 * Returns: (transfer full) (array zero-terminated=1): topic names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_decision_brain_dup_topics (LrgDecisionBrain *self);

/**
 * lrg_decision_brain_get_request:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * The topic's last question as JSON (its state and the typed questions:
 * what the decider turns into the model prompt), whether it went out or
 * was answered from the cache.
 *
 * Returns: (transfer none) (nullable): the JSON, %NULL before the first
 *   question
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_decision_brain_get_request (LrgDecisionBrain *self,
                                const gchar      *topic);

/**
 * lrg_decision_brain_get_latency:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * Returns: the seconds the topic's last answered question took, -1 when
 *   none was answered by the model
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_decision_brain_get_latency (LrgDecisionBrain *self,
                                const gchar      *topic);

/**
 * lrg_decision_brain_get_pending_seconds:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * Returns: how long the topic's question has been out, -1 when it is not
 *   busy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_decision_brain_get_pending_seconds (LrgDecisionBrain *self,
                                        const gchar      *topic);

/**
 * lrg_decision_brain_get_topic_counts:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 * @out_asked: (out) (optional): questions sent to the model
 * @out_answered: (out) (optional): questions the model answered
 * @out_cached: (out) (optional): questions answered from the cache
 * @out_failed: (out) (optional): questions that failed
 *
 * Returns: %TRUE if @topic is known (the counts are 0 otherwise)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_decision_brain_get_topic_counts (LrgDecisionBrain *self,
                                     const gchar      *topic,
                                     guint            *out_asked,
                                     guint            *out_answered,
                                     guint            *out_cached,
                                     guint            *out_failed);

/**
 * lrg_decision_brain_get_totals:
 * @self: an #LrgDecisionBrain
 * @out_asked: (out) (optional): questions sent to the model
 * @out_answered: (out) (optional): questions the model answered
 * @out_cached: (out) (optional): questions answered from the cache
 * @out_failed: (out) (optional): questions that failed
 * @out_input_tokens: (out) (optional): prompt tokens reported
 * @out_output_tokens: (out) (optional): answer tokens reported
 * @out_mean_seconds: (out) (optional): mean latency of answered questions
 *
 * The brain-wide counters (what lrg_decision_brain_dup_status() prints).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_decision_brain_get_totals (LrgDecisionBrain *self,
                               guint            *out_asked,
                               guint            *out_answered,
                               guint            *out_cached,
                               guint            *out_failed,
                               gint64           *out_input_tokens,
                               gint64           *out_output_tokens,
                               gdouble          *out_mean_seconds);

/**
 * lrg_decision_brain_dup_topic_state:
 * @self: an #LrgDecisionBrain
 * @topic: a topic
 *
 * One topic for an inspector, as YAML: `topic`, `busy`,
 * `pending-seconds`, `latency`, `age`, the four counts, `model`, `error`,
 * `request` (the question JSON) and `latest` (the answer JSON).
 *
 * Returns: (transfer full) (nullable): the text, %NULL for an unknown topic
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_decision_brain_dup_topic_state (LrgDecisionBrain *self,
                                    const gchar      *topic);

G_END_DECLS

#endif /* LRG_DECISION_BRAIN_H */
