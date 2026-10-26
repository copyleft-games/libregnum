/* lrg-decider.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecider: ai-glib's AiDecider backends behind a name, or a script.
 * The request goes over as the wire JSON (ai_decision_request_new_from_
 * json validates it as the backend will), the answer comes back as the
 * response JSON (LrgJudgments reads it): libregnum's public types never
 * carry ai-glib's.
 */

#include "lrg-decider.h"

#ifdef LRG_ENABLE_AI
#include <ai-glib.h>
#endif

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* the default deadline of a decision, milliseconds */
#define DEFAULT_TIMEOUT_MS (30000)

typedef enum
{
    BACKEND_SCRIPTED,
    BACKEND_TYPESAFE,
    BACKEND_OLLAMA,
    BACKEND_LAYA
} Backend;

struct _LrgDecider
{
    GObject               parent_instance;

    Backend               backend;
    gchar                *model;
    guint                 timeout_ms;

    GObject              *client;     /* the AiDecider (AI=1), NULL scripted */
    LrgDeciderScriptFunc  func;
    gpointer              func_data;
    GDestroyNotify        func_destroy;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgDecider, lrg_decider, G_TYPE_OBJECT)
#pragma GCC visibility pop

G_DEFINE_QUARK (lrg-decider-error-quark, lrg_decider_error)

static void
lrg_decider_finalize (GObject *object)
{
    LrgDecider *self = LRG_DECIDER (object);

    g_clear_object (&self->client);
    g_clear_pointer (&self->model, g_free);
    if (self->func_destroy != NULL)
        self->func_destroy (self->func_data);

    G_OBJECT_CLASS (lrg_decider_parent_class)->finalize (object);
}

static void
lrg_decider_class_init (LrgDeciderClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_decider_finalize;
}

static void
lrg_decider_init (LrgDecider *self)
{
    self->timeout_ms = DEFAULT_TIMEOUT_MS;
}

gboolean
lrg_decider_is_built_in (void)
{
#ifdef LRG_ENABLE_AI
    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef LRG_ENABLE_AI
/* the model and the deadline on the backend's own properties */
static void
apply_settings (LrgDecider *self)
{
    if (self->client == NULL)
        return;
    if (self->backend == BACKEND_OLLAMA)
    {
        if (self->model != NULL)
            g_object_set (self->client, "decision-model", self->model, NULL);
        g_object_set (self->client, "decision-timeout-ms", self->timeout_ms, NULL);
    }
    else
    {
        if (self->model != NULL)
            g_object_set (self->client, "model", self->model, NULL);
        g_object_set (self->client, "timeout-ms", self->timeout_ms, NULL);
    }
}
#endif

LrgDecider *
lrg_decider_new (const gchar  *provider,
                 GError      **error)
{
#ifdef LRG_ENABLE_AI
    g_autoptr(LrgDecider) self = NULL;
    const gchar *name;

    /* auto: Jev when its key is there, else a local Ollama */
    name = provider;
    if (name == NULL || *name == '\0' || g_ascii_strcasecmp (name, "auto") == 0)
        name = g_getenv ("TYPESAFE_API_KEY") != NULL ? "typesafe" : "ollama";

    self = g_object_new (LRG_TYPE_DECIDER, NULL);
    if (g_ascii_strcasecmp (name, "typesafe") == 0 || g_ascii_strcasecmp (name, "jev") == 0)
    {
        self->backend = BACKEND_TYPESAFE;
        self->client = G_OBJECT (ai_typesafe_client_new ());
    }
    else if (g_ascii_strcasecmp (name, "ollama") == 0)
    {
        self->backend = BACKEND_OLLAMA;
        self->client = G_OBJECT (ai_ollama_client_new ());
    }
    else if (g_ascii_strcasecmp (name, "laya") == 0)
    {
        self->backend = BACKEND_LAYA;
        self->client = G_OBJECT (ai_laya_client_new ());
    }
    else
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_UNSUPPORTED,
                     "unknown decision model provider \"%s\" (typesafe, jev, ollama, laya, auto)", name);
        return NULL;
    }
    apply_settings (self);
    return g_steal_pointer (&self);
#else
    g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_UNSUPPORTED,
                 "this libregnum was built without decision models (AI=0): no \"%s\"",
                 provider != NULL ? provider : "auto");
    return NULL;
#endif
}

LrgDecider *
lrg_decider_new_scripted (LrgDeciderScriptFunc func,
                          gpointer             user_data,
                          GDestroyNotify       destroy)
{
    LrgDecider *self;

    g_return_val_if_fail (func != NULL, NULL);

    self = g_object_new (LRG_TYPE_DECIDER, NULL);
    self->backend = BACKEND_SCRIPTED;
    self->func = func;
    self->func_data = user_data;
    self->func_destroy = destroy;
    return self;
}

const gchar *
lrg_decider_get_provider (LrgDecider *self)
{
    g_return_val_if_fail (LRG_IS_DECIDER (self), NULL);

    switch (self->backend)
    {
    case BACKEND_TYPESAFE:
        return "typesafe";
    case BACKEND_OLLAMA:
        return "ollama";
    case BACKEND_LAYA:
        return "laya";
    case BACKEND_SCRIPTED:
    default:
        return "scripted";
    }
}

const gchar *
lrg_decider_get_model (LrgDecider *self)
{
    g_return_val_if_fail (LRG_IS_DECIDER (self), NULL);
    return self->model;
}

void
lrg_decider_set_model (LrgDecider  *self,
                       const gchar *model)
{
    g_return_if_fail (LRG_IS_DECIDER (self));

    g_free (self->model);
    self->model = model != NULL && *model != '\0' ? g_strdup (model) : NULL;
#ifdef LRG_ENABLE_AI
    apply_settings (self);
#endif
}

guint
lrg_decider_get_timeout_ms (LrgDecider *self)
{
    g_return_val_if_fail (LRG_IS_DECIDER (self), 0);
    return self->timeout_ms;
}

void
lrg_decider_set_timeout_ms (LrgDecider *self,
                            guint       timeout_ms)
{
    g_return_if_fail (LRG_IS_DECIDER (self));
    g_return_if_fail (timeout_ms > 0);

    self->timeout_ms = timeout_ms;
#ifdef LRG_ENABLE_AI
    apply_settings (self);
#endif
}

/* a script's answer, given on the next main loop iteration */
static gboolean
scripted_answer (gpointer data)
{
    GTask *task = G_TASK (data);
    LrgDecider *self = g_task_get_source_object (task);
    const gchar *json = g_task_get_task_data (task);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *response = NULL;
    LrgJudgments *judgments;

    if (g_task_return_error_if_cancelled (task))
        return G_SOURCE_REMOVE;
    response = self->func (self, json, self->func_data, &error);
    if (response == NULL)
    {
        if (error == NULL)
            g_set_error (&error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_FAILED, "the script gave no answer");
        g_task_return_error (task, g_steal_pointer (&error));
        return G_SOURCE_REMOVE;
    }
    judgments = lrg_judgments_new_from_json (response, &error);
    if (judgments == NULL)
        g_task_return_error (task, g_steal_pointer (&error));
    else
        g_task_return_pointer (task, judgments, g_object_unref);
    return G_SOURCE_REMOVE;
}

#ifdef LRG_ENABLE_AI
/* the backend answered: its response JSON read as judgments */
static void
backend_answered (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    g_autoptr(GTask) task = G_TASK (user_data);
    g_autoptr(AiDecisionResponse) response = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *json = NULL;
    LrgJudgments *judgments;

    response = ai_decider_decide_finish (AI_DECIDER (source), result, &error);
    if (response == NULL)
    {
        g_task_return_error (task, g_steal_pointer (&error));
        return;
    }
    json = ai_decision_response_dup_json (response);
    judgments = lrg_judgments_new_from_json (json, &error);
    if (judgments == NULL)
        g_task_return_error (task, g_steal_pointer (&error));
    else
        g_task_return_pointer (task, judgments, g_object_unref);
}
#endif

void
lrg_decider_decide_async (LrgDecider          *self,
                          LrgDecisionRequest  *request,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    g_autoptr(GTask) task = NULL;
    gchar *json;

    g_return_if_fail (LRG_IS_DECIDER (self));
    g_return_if_fail (LRG_IS_DECISION_REQUEST (request));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_source_tag (task, lrg_decider_decide_async);

    /* the request is read now; the model is the backend's property */
    json = lrg_decision_request_dup_json (request, NULL);
    if (self->backend == BACKEND_SCRIPTED)
    {
        g_task_set_task_data (task, json, g_free);
        g_idle_add (scripted_answer, g_steal_pointer (&task));
        return;
    }
#ifdef LRG_ENABLE_AI
    {
        g_autoptr(AiDecisionRequest) wire = NULL;
        g_autoptr(GError) error = NULL;

        wire = ai_decision_request_new_from_json (json, &error);
        g_free (json);
        if (wire == NULL)
        {
            g_task_return_error (task, g_steal_pointer (&error));
            return;
        }
        ai_decider_decide_async (AI_DECIDER (self->client), wire, cancellable, backend_answered,
                                 g_steal_pointer (&task));
    }
#else
    g_free (json);
    g_task_return_new_error (task, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_UNSUPPORTED,
                             "this libregnum was built without decision models (AI=0)");
#endif
}

LrgJudgments *
lrg_decider_decide_finish (LrgDecider    *self,
                           GAsyncResult  *result,
                           GError       **error)
{
    g_return_val_if_fail (LRG_IS_DECIDER (self), NULL);
    g_return_val_if_fail (g_task_is_valid (result, self), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}
