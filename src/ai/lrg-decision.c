/* lrg-decision.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecisionRequest and LrgJudgments: the System One wire format's
 * request and response, built and read with json-glib.  The request is
 * kept as a JSON object, `{"state": ..., "questions": {...}}`, so it is
 * sent as it is built; the response is kept as it came and read on
 * demand.
 */

#include "lrg-decision.h"

#include <json-glib/json-glib.h>
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* ------------------------------------------------------------------ */
/* the request                                                         */
/* ------------------------------------------------------------------ */

struct _LrgDecisionRequest
{
    GObject     parent_instance;

    JsonNode   *state;       /* a string, an object or an array */
    JsonObject *questions;   /* id -> {type, instructions, criteria} */
    guint       n_questions;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgDecisionRequest, lrg_decision_request, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_decision_request_finalize (GObject *object)
{
    LrgDecisionRequest *self = LRG_DECISION_REQUEST (object);

    g_clear_pointer (&self->state, json_node_unref);
    g_clear_pointer (&self->questions, json_object_unref);

    G_OBJECT_CLASS (lrg_decision_request_parent_class)->finalize (object);
}

static void
lrg_decision_request_class_init (LrgDecisionRequestClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_decision_request_finalize;
}

static void
lrg_decision_request_init (LrgDecisionRequest *self)
{
    self->state = json_node_new (JSON_NODE_VALUE);
    json_node_set_string (self->state, "");
    self->questions = json_object_new ();
}

LrgDecisionRequest *
lrg_decision_request_new (void)
{
    return g_object_new (LRG_TYPE_DECISION_REQUEST, NULL);
}

void
lrg_decision_request_set_state_text (LrgDecisionRequest *self,
                                     const gchar        *text)
{
    g_return_if_fail (LRG_IS_DECISION_REQUEST (self));

    json_node_unref (self->state);
    self->state = json_node_new (JSON_NODE_VALUE);
    json_node_set_string (self->state, text != NULL ? text : "");
}

gboolean
lrg_decision_request_set_state_json (LrgDecisionRequest  *self,
                                     const gchar         *json,
                                     GError             **error)
{
    g_autoptr(JsonParser) parser = NULL;
    JsonNode *root;

    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), FALSE);
    g_return_val_if_fail (json != NULL, FALSE);

    /* only an object or an array: a string state has its own setter */
    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json, -1, error))
        return FALSE;
    root = json_parser_get_root (parser);
    if (root == NULL || (!JSON_NODE_HOLDS_OBJECT (root) && !JSON_NODE_HOLDS_ARRAY (root)))
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "a decision state must be a JSON object or array");
        return FALSE;
    }
    json_node_unref (self->state);
    self->state = json_node_copy (root);
    return TRUE;
}

/*
 * add_question:
 *
 * Checks the name and the text, then keeps the question with its
 * criteria (taken).
 */
static gboolean
add_question (LrgDecisionRequest  *self,
              const gchar         *id,
              const gchar         *type,
              const gchar         *instructions,
              JsonNode            *criteria,
              GError             **error)
{
    JsonObject *question;

    if (id == NULL || *id == '\0' || instructions == NULL || *instructions == '\0')
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "a question needs a name and instructions");
        if (criteria != NULL)
            json_node_unref (criteria);
        return FALSE;
    }
    if (json_object_has_member (self->questions, id))
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "a question named \"%s\" is already asked", id);
        if (criteria != NULL)
            json_node_unref (criteria);
        return FALSE;
    }
    question = json_object_new ();
    json_object_set_string_member (question, "type", type);
    json_object_set_string_member (question, "instructions", instructions);
    if (criteria != NULL)
        json_object_set_member (question, "criteria", criteria);
    json_object_set_object_member (self->questions, id, question);
    self->n_questions++;
    return TRUE;
}

gboolean
lrg_decision_request_add_noul (LrgDecisionRequest  *self,
                               const gchar         *id,
                               const gchar         *instructions,
                               const gchar         *true_criterion,
                               const gchar         *false_criterion,
                               GError             **error)
{
    JsonNode *criteria = NULL;

    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), FALSE);

    /* {"true": ..., "false": ...}, either side optional */
    if (true_criterion != NULL || false_criterion != NULL)
    {
        JsonObject *object = json_object_new ();

        if (true_criterion != NULL)
            json_object_set_string_member (object, "true", true_criterion);
        if (false_criterion != NULL)
            json_object_set_string_member (object, "false", false_criterion);
        criteria = json_node_new (JSON_NODE_OBJECT);
        json_node_take_object (criteria, object);
    }
    return add_question (self, id, "noul", instructions, criteria, error);
}

gboolean
lrg_decision_request_add_choice (LrgDecisionRequest  *self,
                                 const gchar         *id,
                                 const gchar         *instructions,
                                 const gchar * const *options,
                                 const gchar * const *descriptions,
                                 GError             **error)
{
    JsonObject *object;
    JsonNode *criteria;
    guint i;

    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), FALSE);

    if (options == NULL || options[0] == NULL)
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "the choice \"%s\" has no options", id != NULL ? id : "");
        return FALSE;
    }

    /* {option: description}, the name itself when there is none */
    object = json_object_new ();
    for (i = 0; options[i] != NULL; i++)
    {
        const gchar *description = NULL;

        if (options[i][0] == '\0' || json_object_has_member (object, options[i]))
        {
            g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                         "the choice \"%s\" has an empty or repeated option", id != NULL ? id : "");
            json_object_unref (object);
            return FALSE;
        }
        if (descriptions != NULL)
        {
            description = descriptions[i];
            if (description == NULL)
            {
                g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                             "the choice \"%s\" has fewer descriptions than options", id != NULL ? id : "");
                json_object_unref (object);
                return FALSE;
            }
        }
        json_object_set_string_member (object, options[i], description != NULL ? description : options[i]);
    }
    criteria = json_node_new (JSON_NODE_OBJECT);
    json_node_take_object (criteria, object);
    return add_question (self, id, "choice", instructions, criteria, error);
}

gboolean
lrg_decision_request_add_score (LrgDecisionRequest  *self,
                                const gchar         *id,
                                const gchar         *instructions,
                                const gchar * const *levels,
                                GError             **error)
{
    JsonArray *array;
    JsonNode *criteria;
    guint i;

    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), FALSE);

    if (levels == NULL || levels[0] == NULL || levels[1] == NULL)
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "the score \"%s\" needs two levels or more", id != NULL ? id : "");
        return FALSE;
    }
    array = json_array_new ();
    for (i = 0; levels[i] != NULL; i++)
        json_array_add_string_element (array, levels[i]);
    criteria = json_node_new (JSON_NODE_ARRAY);
    json_node_take_array (criteria, array);
    return add_question (self, id, "score", instructions, criteria, error);
}

guint
lrg_decision_request_get_n_questions (LrgDecisionRequest *self)
{
    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), 0);
    return self->n_questions;
}

gchar *
lrg_decision_request_dup_json (LrgDecisionRequest *self,
                               const gchar        *model)
{
    g_autoptr(JsonNode) root = NULL;
    JsonObject *object;

    g_return_val_if_fail (LRG_IS_DECISION_REQUEST (self), NULL);

    object = json_object_new ();
    if (model != NULL && *model != '\0')
        json_object_set_string_member (object, "model", model);
    json_object_set_member (object, "state", json_node_copy (self->state));
    json_object_set_object_member (object, "questions", json_object_ref (self->questions));
    root = json_node_new (JSON_NODE_OBJECT);
    json_node_take_object (root, object);
    return json_to_string (root, FALSE);
}

/* ------------------------------------------------------------------ */
/* the answers                                                         */
/* ------------------------------------------------------------------ */

struct _LrgJudgments
{
    GObject     parent_instance;

    JsonNode   *root;        /* the response */
    JsonObject *answers;     /* borrowed from root */
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgJudgments, lrg_judgments, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_judgments_finalize (GObject *object)
{
    LrgJudgments *self = LRG_JUDGMENTS (object);

    g_clear_pointer (&self->root, json_node_unref);

    G_OBJECT_CLASS (lrg_judgments_parent_class)->finalize (object);
}

static void
lrg_judgments_class_init (LrgJudgmentsClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_judgments_finalize;
}

static void
lrg_judgments_init (LrgJudgments *self)
{
}

LrgJudgments *
lrg_judgments_new_from_json (const gchar  *json,
                             GError      **error)
{
    g_autoptr(JsonParser) parser = NULL;
    LrgJudgments *self;
    JsonNode *root;
    JsonObject *object;
    JsonNode *answers;

    g_return_val_if_fail (json != NULL, NULL);

    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json, -1, error))
        return NULL;
    root = json_parser_get_root (parser);
    object = root != NULL && JSON_NODE_HOLDS_OBJECT (root) ? json_node_get_object (root) : NULL;
    answers = object != NULL ? json_object_get_member (object, "answers") : NULL;
    if (answers == NULL || !JSON_NODE_HOLDS_OBJECT (answers))
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID,
                     "a decision response has no answers");
        return NULL;
    }
    self = g_object_new (LRG_TYPE_JUDGMENTS, NULL);
    self->root = json_node_copy (root);
    self->answers = json_object_get_object_member (json_node_get_object (self->root), "answers");
    return self;
}

/* the answer named @id, if it is of @type (NULL: any) */
static JsonObject *
answer (LrgJudgments *self,
        const gchar  *id,
        const gchar  *type)
{
    JsonNode *node;
    JsonObject *object;

    if (id == NULL)
        return NULL;
    node = json_object_get_member (self->answers, id);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
        return NULL;
    object = json_node_get_object (node);
    if (type != NULL && g_strcmp0 (json_object_get_string_member_with_default (object, "type", ""), type) != 0)
        return NULL;
    return object;
}

/* a member that is a number, else @fallback */
static gdouble
number (JsonObject  *object,
        const gchar *member,
        gdouble      fallback)
{
    JsonNode *node = object != NULL ? json_object_get_member (object, member) : NULL;

    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) ||
        (json_node_get_value_type (node) != G_TYPE_DOUBLE && json_node_get_value_type (node) != G_TYPE_INT64))
        return fallback;
    return json_node_get_double (node);
}

const gchar *
lrg_judgments_get_model (LrgJudgments *self)
{
    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), NULL);
    return json_object_get_string_member_with_default (json_node_get_object (self->root), "model", NULL);
}

gboolean
lrg_judgments_has (LrgJudgments *self,
                   const gchar  *id)
{
    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), FALSE);
    return answer (self, id, NULL) != NULL;
}

gdouble
lrg_judgments_get_noul (LrgJudgments *self,
                        const gchar  *id)
{
    gdouble p;

    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), -1.0);

    p = number (answer (self, id, "noul"), "noul", -1.0);
    return p < 0.0 || !isfinite (p) ? -1.0 : CLAMP (p, 0.0, 1.0);
}

LrgJudgmentBand
lrg_judgments_get_band (LrgJudgments *self,
                        const gchar  *id,
                        gdouble       yes,
                        gdouble       no)
{
    gdouble p;

    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), LRG_JUDGMENT_BAND_MISSING);

    p = lrg_judgments_get_noul (self, id);
    if (p < 0.0)
        return LRG_JUDGMENT_BAND_MISSING;
    if (p >= yes)
        return LRG_JUDGMENT_BAND_YES;
    if (p <= no)
        return LRG_JUDGMENT_BAND_NO;
    return LRG_JUDGMENT_BAND_UNSETTLED;
}

const gchar *
lrg_judgments_get_choice (LrgJudgments *self,
                          const gchar  *id)
{
    JsonObject *object;

    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), NULL);

    object = answer (self, id, "choice");
    return object != NULL ? json_object_get_string_member_with_default (object, "choice", NULL) : NULL;
}

gdouble
lrg_judgments_get_probability (LrgJudgments *self,
                               const gchar  *id,
                               const gchar  *option)
{
    JsonObject *object;
    JsonNode *probabilities;

    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), -1.0);

    object = answer (self, id, NULL);
    probabilities = object != NULL && option != NULL ? json_object_get_member (object, "probabilities") : NULL;
    if (probabilities == NULL || !JSON_NODE_HOLDS_OBJECT (probabilities))
        return -1.0;
    return number (json_node_get_object (probabilities), option, -1.0);
}

gdouble
lrg_judgments_get_score (LrgJudgments *self,
                         const gchar  *id)
{
    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), -1.0);
    return number (answer (self, id, "score"), "score", -1.0);
}

void
lrg_judgments_get_tokens (LrgJudgments *self,
                          gint64       *out_input,
                          gint64       *out_output)
{
    JsonNode *usage;
    JsonObject *object = NULL;

    g_return_if_fail (LRG_IS_JUDGMENTS (self));

    usage = json_object_get_member (json_node_get_object (self->root), "usage");
    if (usage != NULL && JSON_NODE_HOLDS_OBJECT (usage))
        object = json_node_get_object (usage);
    if (out_input != NULL)
        *out_input = (gint64)number (object, "input_tokens", -1.0);
    if (out_output != NULL)
        *out_output = (gint64)number (object, "output_tokens", -1.0);
}

gchar **
lrg_judgments_dup_ids (LrgJudgments *self)
{
    g_autoptr(GList) members = NULL;
    GPtrArray *ids;
    GList *l;

    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), NULL);

    ids = g_ptr_array_new ();
    members = json_object_get_members (self->answers);
    for (l = members; l != NULL; l = l->next)
        g_ptr_array_add (ids, g_strdup (l->data));
    g_ptr_array_add (ids, NULL);
    return (gchar **)g_ptr_array_free (ids, FALSE);
}

gchar *
lrg_judgments_dup_json (LrgJudgments *self)
{
    g_return_val_if_fail (LRG_IS_JUDGMENTS (self), NULL);
    return json_to_string (self->root, FALSE);
}

void
lrg_judgments_to_blackboard (LrgJudgments  *self,
                             LrgBlackboard *blackboard,
                             const gchar   *prefix)
{
    g_autoptr(GList) members = NULL;
    GList *l;

    g_return_if_fail (LRG_IS_JUDGMENTS (self));
    g_return_if_fail (LRG_IS_BLACKBOARD (blackboard));

    /* each answer under its name: Nouls and Scores as floats, Choices as
     * their option's name */
    members = json_object_get_members (self->answers);
    for (l = members; l != NULL; l = l->next)
    {
        const gchar *id = l->data;
        g_autofree gchar *key = g_strconcat (prefix != NULL ? prefix : "", id, NULL);
        const gchar *choice;
        gdouble value;

        if ((value = lrg_judgments_get_noul (self, id)) >= 0.0)
            lrg_blackboard_set_float (blackboard, key, (gfloat)value);
        else if ((choice = lrg_judgments_get_choice (self, id)) != NULL)
            lrg_blackboard_set_string (blackboard, key, choice);
        else if ((value = lrg_judgments_get_score (self, id)) >= 0.0)
            lrg_blackboard_set_float (blackboard, key, (gfloat)value);
    }
}
