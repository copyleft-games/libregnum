/* lrg-decision-brain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecisionBrain: topics, each with its question out (a cancellable)
 * and its latest answers; a cache of answers by the request's JSON; a
 * journal; totals for the status.
 */

#include "lrg-decision-brain.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* a topic: its question out, its latest answers */
typedef struct
{
    gchar        *name;
    GCancellable *pending;     /* the question out, NULL none */
    gchar        *request;     /* its JSON, for the cache and the journal */
    gint64        sent;        /* monotonic microseconds */
    LrgJudgments *latest;
    gint64        latest_at;
    gchar        *error;
} Topic;

/* an answer kept for a request */
typedef struct
{
    LrgJudgments *judgments;
    gint64        at;
} Cached;

/* what an answer in flight carries back */
typedef struct
{
    LrgDecisionBrain *brain;    /* a weak pointer */
    gchar            *topic;
} Pending;

struct _LrgDecisionBrain
{
    GObject        parent_instance;

    LrgDecider    *decider;
    LrgBlackboard *blackboard;
    GHashTable    *topics;     /* name -> Topic */
    GHashTable    *cache;      /* request JSON -> Cached */
    gdouble        cache_seconds;
    gchar         *journal;
    gboolean       journal_failed;

    /* totals */
    guint          asked;
    guint          cached;
    guint          answered;
    guint          failed;
    gint64         input_tokens;
    gint64         output_tokens;
    gdouble        seconds;    /* the answered questions', summed */
};

enum
{
    SIGNAL_ANSWERED,
    SIGNAL_FAILED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgDecisionBrain, lrg_decision_brain, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
topic_free (gpointer data)
{
    Topic *topic = data;

    if (topic->pending != NULL)
        g_cancellable_cancel (topic->pending);
    g_clear_object (&topic->pending);
    g_clear_object (&topic->latest);
    g_free (topic->request);
    g_free (topic->error);
    g_free (topic->name);
    g_free (topic);
}

static void
cached_free (gpointer data)
{
    Cached *cached = data;

    g_clear_object (&cached->judgments);
    g_free (cached);
}

static void
lrg_decision_brain_dispose (GObject *object)
{
    LrgDecisionBrain *self = LRG_DECISION_BRAIN (object);

    /* the questions out are cancelled with their topics */
    g_clear_pointer (&self->topics, g_hash_table_unref);
    g_clear_pointer (&self->cache, g_hash_table_unref);
    g_clear_object (&self->decider);
    g_clear_object (&self->blackboard);

    G_OBJECT_CLASS (lrg_decision_brain_parent_class)->dispose (object);
}

static void
lrg_decision_brain_finalize (GObject *object)
{
    LrgDecisionBrain *self = LRG_DECISION_BRAIN (object);

    g_free (self->journal);

    G_OBJECT_CLASS (lrg_decision_brain_parent_class)->finalize (object);
}

static void
lrg_decision_brain_class_init (LrgDecisionBrainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_decision_brain_dispose;
    object_class->finalize = lrg_decision_brain_finalize;

    /**
     * LrgDecisionBrain::answered:
     * @self: the brain
     * @topic: the topic answered
     * @judgments: its answers (also its latest)
     *
     * A topic's question was answered (not from the cache).
     */
    signals[SIGNAL_ANSWERED] = g_signal_new ("answered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                             NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, LRG_TYPE_JUDGMENTS);

    /**
     * LrgDecisionBrain::failed:
     * @self: the brain
     * @topic: the topic
     * @message: why
     *
     * A topic's question failed; its latest answers stay what they were.
     */
    signals[SIGNAL_FAILED] = g_signal_new ("failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                           NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_decision_brain_init (LrgDecisionBrain *self)
{
    self->topics = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, topic_free);
    self->cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, cached_free);
}

LrgDecisionBrain *
lrg_decision_brain_new (LrgDecider *decider)
{
    LrgDecisionBrain *self;

    g_return_val_if_fail (LRG_IS_DECIDER (decider), NULL);

    self = g_object_new (LRG_TYPE_DECISION_BRAIN, NULL);
    self->decider = g_object_ref (decider);
    return self;
}

LrgDecider *
lrg_decision_brain_get_decider (LrgDecisionBrain *self)
{
    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), NULL);
    return self->decider;
}

void
lrg_decision_brain_set_blackboard (LrgDecisionBrain *self,
                                   LrgBlackboard    *blackboard)
{
    g_return_if_fail (LRG_IS_DECISION_BRAIN (self));
    g_set_object (&self->blackboard, blackboard);
}

void
lrg_decision_brain_set_cache_seconds (LrgDecisionBrain *self,
                                      gdouble           seconds)
{
    g_return_if_fail (LRG_IS_DECISION_BRAIN (self));

    self->cache_seconds = MAX (0.0, seconds);
    if (self->cache_seconds <= 0.0)
        g_hash_table_remove_all (self->cache);
}

void
lrg_decision_brain_set_journal (LrgDecisionBrain *self,
                                const gchar      *path)
{
    g_return_if_fail (LRG_IS_DECISION_BRAIN (self));

    g_free (self->journal);
    self->journal = g_strdup (path);
    self->journal_failed = FALSE;
}

/* the topic named @name, made the first time */
static Topic *
topic_get (LrgDecisionBrain *self,
           const gchar      *name)
{
    Topic *topic = g_hash_table_lookup (self->topics, name);

    if (topic == NULL)
    {
        topic = g_new0 (Topic, 1);
        topic->name = g_strdup (name);
        g_hash_table_insert (self->topics, topic->name, topic);
    }
    return topic;
}

/* one decision added to the journal, as a JSON line */
static void
journal_write (LrgDecisionBrain *self,
               const gchar      *topic,
               const gchar      *request,
               LrgJudgments     *judgments,
               const gchar      *error,
               gdouble           seconds,
               gboolean          cached)
{
    g_autoptr(JsonBuilder) builder = NULL;
    g_autoptr(JsonNode) root = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autofree gchar *stamp = NULL;
    g_autofree gchar *line = NULL;
    g_autofree gchar *dir = NULL;
    FILE *f;

    if (self->journal == NULL || self->journal_failed)
        return;

    now = g_date_time_new_now_local ();
    stamp = g_date_time_format_iso8601 (now);
    builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "time");
    json_builder_add_string_value (builder, stamp);
    json_builder_set_member_name (builder, "topic");
    json_builder_add_string_value (builder, topic);
    json_builder_set_member_name (builder, "provider");
    json_builder_add_string_value (builder, lrg_decider_get_provider (self->decider));
    json_builder_set_member_name (builder, "model");
    if (judgments != NULL && lrg_judgments_get_model (judgments) != NULL)
        json_builder_add_string_value (builder, lrg_judgments_get_model (judgments));
    else if (lrg_decider_get_model (self->decider) != NULL)
        json_builder_add_string_value (builder, lrg_decider_get_model (self->decider));
    else
        json_builder_add_null_value (builder);
    json_builder_set_member_name (builder, "elapsed_ms");
    json_builder_add_double_value (builder, seconds * 1000.0);
    json_builder_set_member_name (builder, "cached");
    json_builder_add_boolean_value (builder, cached);
    /* the request and the response as JSON, not strings of it */
    json_builder_set_member_name (builder, "request");
    json_builder_add_value (builder, json_from_string (request, NULL));
    if (judgments != NULL)
    {
        g_autofree gchar *response = lrg_judgments_dup_json (judgments);

        json_builder_set_member_name (builder, "response");
        json_builder_add_value (builder, json_from_string (response, NULL));
    }
    else
    {
        json_builder_set_member_name (builder, "error");
        json_builder_add_string_value (builder, error != NULL ? error : "");
    }
    json_builder_end_object (builder);
    root = json_builder_get_root (builder);
    line = json_to_string (root, FALSE);

    dir = g_path_get_dirname (self->journal);
    (void)g_mkdir_with_parents (dir, 0755);
    f = g_fopen (self->journal, "a");
    if (f == NULL)
    {
        /* said once; the decisions go on without it */
        lrg_warning (LRG_LOG_DOMAIN, "decision journal %s: %s", self->journal, g_strerror (errno));
        self->journal_failed = TRUE;
        return;
    }
    fputs (line, f);
    fputc ('\n', f);
    fclose (f);
}

/* the topic's answers: kept, to the blackboard, told */
static void
take_answer (LrgDecisionBrain *self,
             Topic            *topic,
             LrgJudgments     *judgments,
             gboolean          announce)
{
    g_set_object (&topic->latest, judgments);
    topic->latest_at = g_get_monotonic_time ();
    g_clear_pointer (&topic->error, g_free);
    if (self->blackboard != NULL)
    {
        g_autofree gchar *prefix = g_strconcat (topic->name, ".", NULL);

        lrg_judgments_to_blackboard (judgments, self->blackboard, prefix);
    }
    if (announce)
        g_signal_emit (self, signals[SIGNAL_ANSWERED], 0, topic->name, judgments);
}

static void
pending_free (Pending *pending)
{
    g_clear_weak_pointer (&pending->brain);
    g_free (pending->topic);
    g_free (pending);
}

/* a topic's question came back */
static void
answered (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
    Pending *pending = user_data;
    LrgDecisionBrain *self = pending->brain;
    g_autoptr(LrgJudgments) judgments = NULL;
    g_autoptr(GError) error = NULL;
    Topic *topic;
    gdouble seconds;

    judgments = lrg_decider_decide_finish (LRG_DECIDER (source), result, &error);
    /* the brain went, or the question was cancelled: nothing to keep */
    if (self == NULL || g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        pending_free (pending);
        return;
    }
    topic = g_hash_table_lookup (self->topics, pending->topic);
    pending_free (pending);
    if (topic == NULL)
        return;
    g_clear_object (&topic->pending);
    seconds = (gdouble)(g_get_monotonic_time () - topic->sent) / (gdouble)G_USEC_PER_SEC;

    if (judgments == NULL)
    {
        self->failed++;
        g_free (topic->error);
        topic->error = g_strdup (error->message);
        lrg_debug (LRG_LOG_DOMAIN, "decision %s failed: %s", topic->name, error->message);
        journal_write (self, topic->name, topic->request, NULL, error->message, seconds, FALSE);
        g_signal_emit (self, signals[SIGNAL_FAILED], 0, topic->name, error->message);
        return;
    }

    /* the totals, the cache, the journal, then the topic */
    self->answered++;
    self->seconds += seconds;
    {
        gint64 in = -1;
        gint64 out = -1;

        lrg_judgments_get_tokens (judgments, &in, &out);
        self->input_tokens += MAX (in, 0);
        self->output_tokens += MAX (out, 0);
    }
    if (self->cache_seconds > 0.0)
    {
        Cached *cached = g_new0 (Cached, 1);

        cached->judgments = g_object_ref (judgments);
        cached->at = g_get_monotonic_time ();
        g_hash_table_replace (self->cache, g_strdup (topic->request), cached);
    }
    journal_write (self, topic->name, topic->request, judgments, NULL, seconds, FALSE);
    take_answer (self, topic, judgments, TRUE);
}

gboolean
lrg_decision_brain_ask (LrgDecisionBrain    *self,
                        const gchar         *topic_name,
                        LrgDecisionRequest  *request,
                        gboolean            *out_cached,
                        GError             **error)
{
    Topic *topic;
    Pending *pending;
    Cached *cached;
    g_autofree gchar *json = NULL;

    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), FALSE);
    g_return_val_if_fail (topic_name != NULL, FALSE);
    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (request), FALSE);

    if (out_cached != NULL)
        *out_cached = FALSE;
    topic = topic_get (self, topic_name);
    if (topic->pending != NULL)
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_BUSY,
                     "the decision \"%s\" is already asked", topic_name);
        return FALSE;
    }

    /* the same request answered lately: that answer */
    json = lrg_decision_request_dup_json (request, NULL);
    cached = g_hash_table_lookup (self->cache, json);
    if (cached != NULL &&
        (gdouble)(g_get_monotonic_time () - cached->at) / (gdouble)G_USEC_PER_SEC <= self->cache_seconds)
    {
        self->cached++;
        journal_write (self, topic_name, json, cached->judgments, NULL, 0.0, TRUE);
        take_answer (self, topic, cached->judgments, FALSE);
        if (out_cached != NULL)
            *out_cached = TRUE;
        return TRUE;
    }
    if (cached != NULL)
        g_hash_table_remove (self->cache, json);

    /* out to the model */
    self->asked++;
    g_free (topic->request);
    topic->request = g_steal_pointer (&json);
    topic->sent = g_get_monotonic_time ();
    topic->pending = g_cancellable_new ();
    pending = g_new0 (Pending, 1);
    g_set_weak_pointer (&pending->brain, self);
    pending->topic = g_strdup (topic_name);
    lrg_decider_decide_async (self->decider, request, topic->pending, answered, pending);
    return TRUE;
}

gboolean
lrg_decision_brain_is_busy (LrgDecisionBrain *self,
                            const gchar      *topic_name)
{
    Topic *topic;

    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), FALSE);

    topic = topic_name != NULL ? g_hash_table_lookup (self->topics, topic_name) : NULL;
    return topic != NULL && topic->pending != NULL;
}

LrgJudgments *
lrg_decision_brain_get_latest (LrgDecisionBrain *self,
                               const gchar      *topic_name,
                               gdouble          *out_age)
{
    Topic *topic;

    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), NULL);

    topic = topic_name != NULL ? g_hash_table_lookup (self->topics, topic_name) : NULL;
    if (out_age != NULL)
        *out_age = topic != NULL && topic->latest != NULL ?
                   (gdouble)(g_get_monotonic_time () - topic->latest_at) / (gdouble)G_USEC_PER_SEC : -1.0;
    return topic != NULL ? topic->latest : NULL;
}

const gchar *
lrg_decision_brain_get_last_error (LrgDecisionBrain *self,
                                   const gchar      *topic_name)
{
    Topic *topic;

    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), NULL);

    topic = topic_name != NULL ? g_hash_table_lookup (self->topics, topic_name) : NULL;
    return topic != NULL ? topic->error : NULL;
}

void
lrg_decision_brain_cancel (LrgDecisionBrain *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_DECISION_BRAIN (self));

    g_hash_table_iter_init (&iter, self->topics);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Topic *topic = value;

        if (topic->pending != NULL)
        {
            g_cancellable_cancel (topic->pending);
            g_clear_object (&topic->pending);
        }
    }
}

/* a topic name's order, for a stable listing */
static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

gchar *
lrg_decision_brain_dup_status (LrgDecisionBrain *self)
{
    g_autoptr(GPtrArray) names = NULL;
    GHashTableIter iter;
    gpointer key;
    GString *out;
    guint i;

    g_return_val_if_fail (LRG_IS_DECISION_BRAIN (self), NULL);

    out = g_string_new (NULL);
    g_string_append_printf (out, "provider: %s\nmodel: %s\nasked: %u\nanswered: %u\ncached: %u\nfailed: %u\n"
                            "input-tokens: %" G_GINT64_FORMAT "\noutput-tokens: %" G_GINT64_FORMAT
                            "\nmean-seconds: %.3f\njournal: %s\ntopics:",
                            lrg_decider_get_provider (self->decider),
                            lrg_decider_get_model (self->decider) != NULL ? lrg_decider_get_model (self->decider) : "~",
                            self->asked, self->answered, self->cached, self->failed, self->input_tokens,
                            self->output_tokens, self->answered > 0 ? self->seconds / (gdouble)self->answered : 0.0,
                            self->journal != NULL ? self->journal : "~");

    names = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, self->topics);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (names, key);
    g_ptr_array_sort (names, compare_names);
    if (names->len == 0)
        g_string_append (out, " []");
    for (i = 0; i < names->len; i++)
    {
        Topic *topic = g_hash_table_lookup (self->topics, g_ptr_array_index (names, i));
        gdouble age = -1.0;
        g_autofree gchar *error = NULL;

        (void)lrg_decision_brain_get_latest (self, topic->name, &age);
        error = topic->error != NULL ? g_strescape (topic->error, NULL) : NULL;
        g_string_append_printf (out, "\n  - {topic: %s, busy: %s, age: %.2f, model: %s, error: %s%s%s}", topic->name,
                                topic->pending != NULL ? "true" : "false", age,
                                topic->latest != NULL && lrg_judgments_get_model (topic->latest) != NULL ?
                                lrg_judgments_get_model (topic->latest) : "~",
                                error != NULL ? "\"" : "", error != NULL ? error : "~", error != NULL ? "\"" : "");
    }
    return g_string_free (out, FALSE);
}
