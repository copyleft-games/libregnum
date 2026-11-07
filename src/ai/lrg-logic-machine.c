/* lrg-logic-machine.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLogicMachine: the .rfsm hierarchical logic state machine.
 *
 * Laid out as:
 *   1. enumerations and errors
 *   2. LrgLogicAction and LrgLogicCondition (boxed, immutable)
 *   3. LrgLogicTransition
 *   4. LrgLogicState
 *   5. LrgLogicMachine: parsing and writing
 *   6. LrgLogicMachine: target resolution and validation
 *   7. LrgLogicMachine: the runtime (start, update, events, debug state)
 *
 * The YAML, variant, script and registry helpers are shared with
 * lrg-bt-asset.c through the header's LRG_AI_PRIVATE block.
 */

#include "config.h"

#include <string.h>

#define LRG_AI_PRIVATE
#include "lrg-logic-machine.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

#define DEFAULT_MAX_TRANSITIONS (8)

/* ==========================================================================
 * 1. Enumerations and errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-logic-machine-error-quark, lrg_logic_machine_error)

GType
lrg_logic_machine_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LOGIC_MACHINE_ERROR_PARSE, "LRG_LOGIC_MACHINE_ERROR_PARSE", "parse" },
			{ LRG_LOGIC_MACHINE_ERROR_VERSION, "LRG_LOGIC_MACHINE_ERROR_VERSION", "version" },
			{ LRG_LOGIC_MACHINE_ERROR_INVALID, "LRG_LOGIC_MACHINE_ERROR_INVALID", "invalid" },
			{ LRG_LOGIC_MACHINE_ERROR_REFERENCE, "LRG_LOGIC_MACHINE_ERROR_REFERENCE", "reference" },
			{ LRG_LOGIC_MACHINE_ERROR_BINDING, "LRG_LOGIC_MACHINE_ERROR_BINDING", "binding" },
			{ LRG_LOGIC_MACHINE_ERROR_IO, "LRG_LOGIC_MACHINE_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgLogicMachineError", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

GType
lrg_logic_phase_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LOGIC_PHASE_ENTER, "LRG_LOGIC_PHASE_ENTER", "enter" },
			{ LRG_LOGIC_PHASE_UPDATE, "LRG_LOGIC_PHASE_UPDATE", "update" },
			{ LRG_LOGIC_PHASE_EXIT, "LRG_LOGIC_PHASE_EXIT", "exit" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgLogicPhase", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

GType
lrg_logic_action_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LOGIC_ACTION_CALL, "LRG_LOGIC_ACTION_CALL", "call" },
			{ LRG_LOGIC_ACTION_SCRIPT, "LRG_LOGIC_ACTION_SCRIPT", "script" },
			{ LRG_LOGIC_ACTION_SET, "LRG_LOGIC_ACTION_SET", "set" },
			{ LRG_LOGIC_ACTION_EVENT, "LRG_LOGIC_ACTION_EVENT", "event" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgLogicActionKind", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

GType
lrg_logic_condition_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LOGIC_CONDITION_CHECK, "LRG_LOGIC_CONDITION_CHECK", "check" },
			{ LRG_LOGIC_CONDITION_CALL, "LRG_LOGIC_CONDITION_CALL", "call" },
			{ LRG_LOGIC_CONDITION_SCRIPT, "LRG_LOGIC_CONDITION_SCRIPT", "script" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgLogicConditionKind", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

#define DOMAIN (LRG_LOGIC_MACHINE_ERROR)

/* ==========================================================================
 * 2. LrgLogicAction and LrgLogicCondition
 * ========================================================================== */

struct _LrgLogicAction
{
	LrgLogicActionKind  kind;
	gchar              *name;
	GVariant           *params;   /* a{sv} or NULL */
	GVariant           *value;    /* set actions */
};

G_DEFINE_BOXED_TYPE (LrgLogicAction, lrg_logic_action, lrg_logic_action_copy, lrg_logic_action_free)

/* a params dictionary taken as given (NULL or empty: none) */
static GVariant *
take_params (GVariant *params)
{
	if (params == NULL)
		return NULL;
	g_variant_ref_sink (params);
	if (!g_variant_is_of_type (params, G_VARIANT_TYPE ("a{sv}")) || g_variant_n_children (params) == 0)
	{
		if (!g_variant_is_of_type (params, G_VARIANT_TYPE ("a{sv}")))
			g_warning ("logic machine parameters must be an a{sv} dictionary");
		g_variant_unref (params);
		return NULL;
	}
	return params;
}

static LrgLogicAction *
action_new (LrgLogicActionKind  kind,
            const gchar        *name,
            GVariant           *params,
            GVariant           *value)
{
	LrgLogicAction *self = g_new0 (LrgLogicAction, 1);

	self->kind = kind;
	self->name = g_strdup (name);
	self->params = take_params (params);
	self->value = value != NULL ? g_variant_ref_sink (value) : NULL;
	return self;
}

LrgLogicAction *
lrg_logic_action_new_call (const gchar *name,
                           GVariant    *params)
{
	g_return_val_if_fail (name != NULL && *name != '\0', NULL);
	return action_new (LRG_LOGIC_ACTION_CALL, name, params, NULL);
}

LrgLogicAction *
lrg_logic_action_new_script (const gchar *func,
                             GVariant    *params)
{
	g_return_val_if_fail (func != NULL && *func != '\0', NULL);
	return action_new (LRG_LOGIC_ACTION_SCRIPT, func, params, NULL);
}

LrgLogicAction *
lrg_logic_action_new_set (const gchar *key,
                          GVariant    *value)
{
	g_return_val_if_fail (key != NULL && *key != '\0', NULL);
	g_return_val_if_fail (value != NULL, NULL);
	return action_new (LRG_LOGIC_ACTION_SET, key, NULL, value);
}

LrgLogicAction *
lrg_logic_action_new_event (const gchar *event)
{
	g_return_val_if_fail (event != NULL && *event != '\0', NULL);
	return action_new (LRG_LOGIC_ACTION_EVENT, event, NULL, NULL);
}

LrgLogicAction *
lrg_logic_action_copy (const LrgLogicAction *self)
{
	LrgLogicAction *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = g_new0 (LrgLogicAction, 1);
	copy->kind = self->kind;
	copy->name = g_strdup (self->name);
	copy->params = self->params != NULL ? g_variant_ref (self->params) : NULL;
	copy->value = self->value != NULL ? g_variant_ref (self->value) : NULL;
	return copy;
}

void
lrg_logic_action_free (LrgLogicAction *self)
{
	if (self == NULL)
		return;
	g_free (self->name);
	g_clear_pointer (&self->params, g_variant_unref);
	g_clear_pointer (&self->value, g_variant_unref);
	g_free (self);
}

LrgLogicActionKind
lrg_logic_action_get_kind (const LrgLogicAction *self)
{
	g_return_val_if_fail (self != NULL, LRG_LOGIC_ACTION_CALL);
	return self->kind;
}

const gchar *
lrg_logic_action_get_name (const LrgLogicAction *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->name;
}

GVariant *
lrg_logic_action_get_params (const LrgLogicAction *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->params;
}

GVariant *
lrg_logic_action_get_value (const LrgLogicAction *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->value;
}

struct _LrgLogicCondition
{
	LrgLogicConditionKind  kind;
	gchar                 *name;
	LrgBlackboardOp        op;
	GVariant              *value;
	GVariant              *params;
};

G_DEFINE_BOXED_TYPE (LrgLogicCondition, lrg_logic_condition, lrg_logic_condition_copy, lrg_logic_condition_free)

LrgLogicCondition *
lrg_logic_condition_new_check (const gchar     *key,
                               LrgBlackboardOp  op,
                               GVariant        *value)
{
	LrgLogicCondition *self;

	g_return_val_if_fail (key != NULL && *key != '\0', NULL);

	self = g_new0 (LrgLogicCondition, 1);
	self->kind = LRG_LOGIC_CONDITION_CHECK;
	self->name = g_strdup (key);
	self->op = op;
	self->value = value != NULL ? g_variant_ref_sink (value) : NULL;
	return self;
}

LrgLogicCondition *
lrg_logic_condition_new_call (const gchar *name,
                              GVariant    *params)
{
	LrgLogicCondition *self;

	g_return_val_if_fail (name != NULL && *name != '\0', NULL);

	self = g_new0 (LrgLogicCondition, 1);
	self->kind = LRG_LOGIC_CONDITION_CALL;
	self->name = g_strdup (name);
	self->params = take_params (params);
	return self;
}

LrgLogicCondition *
lrg_logic_condition_new_script (const gchar *func,
                                GVariant    *params)
{
	LrgLogicCondition *self;

	g_return_val_if_fail (func != NULL && *func != '\0', NULL);

	self = g_new0 (LrgLogicCondition, 1);
	self->kind = LRG_LOGIC_CONDITION_SCRIPT;
	self->name = g_strdup (func);
	self->params = take_params (params);
	return self;
}

LrgLogicCondition *
lrg_logic_condition_copy (const LrgLogicCondition *self)
{
	LrgLogicCondition *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = g_new0 (LrgLogicCondition, 1);
	copy->kind = self->kind;
	copy->name = g_strdup (self->name);
	copy->op = self->op;
	copy->value = self->value != NULL ? g_variant_ref (self->value) : NULL;
	copy->params = self->params != NULL ? g_variant_ref (self->params) : NULL;
	return copy;
}

void
lrg_logic_condition_free (LrgLogicCondition *self)
{
	if (self == NULL)
		return;
	g_free (self->name);
	g_clear_pointer (&self->value, g_variant_unref);
	g_clear_pointer (&self->params, g_variant_unref);
	g_free (self);
}

LrgLogicConditionKind
lrg_logic_condition_get_kind (const LrgLogicCondition *self)
{
	g_return_val_if_fail (self != NULL, LRG_LOGIC_CONDITION_CHECK);
	return self->kind;
}

const gchar *
lrg_logic_condition_get_name (const LrgLogicCondition *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->name;
}

LrgBlackboardOp
lrg_logic_condition_get_op (const LrgLogicCondition *self)
{
	g_return_val_if_fail (self != NULL, LRG_BLACKBOARD_OP_EQ);
	return self->op;
}

GVariant *
lrg_logic_condition_get_value (const LrgLogicCondition *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->value;
}

GVariant *
lrg_logic_condition_get_params (const LrgLogicCondition *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->params;
}

/* ==========================================================================
 * 3. LrgLogicTransition
 * ========================================================================== */

struct _LrgLogicTransition
{
	GObject      parent_instance;

	gchar       *target;
	gchar       *event;
	gdouble      after;
	GPtrArray   *conditions;  /* LrgLogicCondition */
	GPtrArray   *actions;     /* LrgLogicAction */
	guint64      fired;
	YamlMapping *extra;
};

G_DEFINE_FINAL_TYPE (LrgLogicTransition, lrg_logic_transition, G_TYPE_OBJECT)

static void
lrg_logic_transition_finalize (GObject *object)
{
	LrgLogicTransition *self = LRG_LOGIC_TRANSITION (object);

	g_free (self->target);
	g_free (self->event);
	g_clear_pointer (&self->conditions, g_ptr_array_unref);
	g_clear_pointer (&self->actions, g_ptr_array_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);

	G_OBJECT_CLASS (lrg_logic_transition_parent_class)->finalize (object);
}

static void
lrg_logic_transition_class_init (LrgLogicTransitionClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_logic_transition_finalize;
}

static void
lrg_logic_transition_init (LrgLogicTransition *self)
{
	self->conditions = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_logic_condition_free);
	self->actions = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_logic_action_free);
}

LrgLogicTransition *
lrg_logic_transition_new (const gchar *target)
{
	LrgLogicTransition *self;

	g_return_val_if_fail (target != NULL && *target != '\0', NULL);

	self = g_object_new (LRG_TYPE_LOGIC_TRANSITION, NULL);
	self->target = g_strdup (target);
	return self;
}

const gchar *
lrg_logic_transition_get_target (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), NULL);
	return self->target;
}

void
lrg_logic_transition_set_target (LrgLogicTransition *self,
                                 const gchar        *target)
{
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (self));
	g_return_if_fail (target != NULL && *target != '\0');

	g_free (self->target);
	self->target = g_strdup (target);
}

const gchar *
lrg_logic_transition_get_event (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), NULL);
	return self->event;
}

void
lrg_logic_transition_set_event (LrgLogicTransition *self,
                                const gchar        *event)
{
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (self));

	g_free (self->event);
	self->event = (event != NULL && *event != '\0') ? g_strdup (event) : NULL;
}

gdouble
lrg_logic_transition_get_after (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), 0.0);
	return self->after;
}

void
lrg_logic_transition_set_after (LrgLogicTransition *self,
                                gdouble             seconds)
{
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (self));
	self->after = MAX (seconds, 0.0);
}

void
lrg_logic_transition_add_condition (LrgLogicTransition      *self,
                                    const LrgLogicCondition *condition)
{
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (self));
	g_return_if_fail (condition != NULL);

	g_ptr_array_add (self->conditions, lrg_logic_condition_copy (condition));
}

gboolean
lrg_logic_transition_remove_condition (LrgLogicTransition *self,
                                       guint               index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), FALSE);

	if (index >= self->conditions->len)
		return FALSE;
	g_ptr_array_remove_index (self->conditions, index);
	return TRUE;
}

guint
lrg_logic_transition_get_condition_count (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), 0);
	return self->conditions->len;
}

const LrgLogicCondition *
lrg_logic_transition_get_condition (LrgLogicTransition *self,
                                    guint               index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), NULL);
	return index < self->conditions->len ? g_ptr_array_index (self->conditions, index) : NULL;
}

void
lrg_logic_transition_add_action (LrgLogicTransition   *self,
                                 const LrgLogicAction *action)
{
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (self));
	g_return_if_fail (action != NULL);

	g_ptr_array_add (self->actions, lrg_logic_action_copy (action));
}

gboolean
lrg_logic_transition_remove_action (LrgLogicTransition *self,
                                    guint               index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), FALSE);

	if (index >= self->actions->len)
		return FALSE;
	g_ptr_array_remove_index (self->actions, index);
	return TRUE;
}

guint
lrg_logic_transition_get_action_count (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), 0);
	return self->actions->len;
}

const LrgLogicAction *
lrg_logic_transition_get_action (LrgLogicTransition *self,
                                 guint               index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), NULL);
	return index < self->actions->len ? g_ptr_array_index (self->actions, index) : NULL;
}

guint64
lrg_logic_transition_get_fire_count (LrgLogicTransition *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_TRANSITION (self), 0);
	return self->fired;
}

/* ==========================================================================
 * 4. LrgLogicState
 * ========================================================================== */

#define N_PHASES (3)

struct _LrgLogicState
{
	GObject        parent_instance;

	gchar         *name;
	gchar         *initial;
	LrgLogicState *parent;               /* not owned */
	GPtrArray     *children;             /* LrgLogicState */
	GPtrArray     *actions[N_PHASES];    /* LrgLogicAction */
	GPtrArray     *transitions;          /* LrgLogicTransition */
	YamlMapping   *extra;
};

G_DEFINE_FINAL_TYPE (LrgLogicState, lrg_logic_state, G_TYPE_OBJECT)

static void
lrg_logic_state_dispose (GObject *object)
{
	LrgLogicState *self = LRG_LOGIC_STATE (object);
	guint          i;

	if (self->children != NULL)
	{
		for (i = 0; i < self->children->len; i++)
			((LrgLogicState *) g_ptr_array_index (self->children, i))->parent = NULL;
		g_clear_pointer (&self->children, g_ptr_array_unref);
	}
	g_clear_pointer (&self->transitions, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_logic_state_parent_class)->dispose (object);
}

static void
lrg_logic_state_finalize (GObject *object)
{
	LrgLogicState *self = LRG_LOGIC_STATE (object);
	guint          i;

	g_free (self->name);
	g_free (self->initial);
	for (i = 0; i < N_PHASES; i++)
		g_clear_pointer (&self->actions[i], g_ptr_array_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);

	G_OBJECT_CLASS (lrg_logic_state_parent_class)->finalize (object);
}

static void
lrg_logic_state_class_init (LrgLogicStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_logic_state_dispose;
	object_class->finalize = lrg_logic_state_finalize;
}

static void
lrg_logic_state_init (LrgLogicState *self)
{
	guint i;

	self->children = g_ptr_array_new_with_free_func (g_object_unref);
	self->transitions = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < N_PHASES; i++)
		self->actions[i] = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_logic_action_free);
}

LrgLogicState *
lrg_logic_state_new (const gchar *name)
{
	LrgLogicState *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_object_new (LRG_TYPE_LOGIC_STATE, NULL);
	self->name = g_strdup (name);
	return self;
}

const gchar *
lrg_logic_state_get_name (LrgLogicState *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	return self->name;
}

void
lrg_logic_state_set_name (LrgLogicState *self,
                          const gchar   *name)
{
	g_return_if_fail (LRG_IS_LOGIC_STATE (self));
	g_return_if_fail (name != NULL);

	g_free (self->name);
	self->name = g_strdup (name);
}

gchar *
lrg_logic_state_dup_path (LrgLogicState *self)
{
	GString       *out;
	GPtrArray     *chain;
	LrgLogicState *s;
	gint           i;

	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);

	/* the names from below the root down to @self, joined by '/' */
	chain = g_ptr_array_new ();
	for (s = self; s != NULL && s->parent != NULL; s = s->parent)
		g_ptr_array_add (chain, s);
	out = g_string_new (NULL);
	for (i = (gint) chain->len - 1; i >= 0; i--)
	{
		if (out->len > 0)
			g_string_append_c (out, '/');
		g_string_append (out, ((LrgLogicState *) g_ptr_array_index (chain, i))->name);
	}
	g_ptr_array_unref (chain);
	return g_string_free (out, FALSE);
}

LrgLogicState *
lrg_logic_state_get_parent (LrgLogicState *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	return self->parent;
}

void
lrg_logic_state_add_state (LrgLogicState *self,
                           LrgLogicState *child)
{
	g_return_if_fail (LRG_IS_LOGIC_STATE (self));
	g_return_if_fail (LRG_IS_LOGIC_STATE (child));
	g_return_if_fail (child->parent == NULL && child != self);

	child->parent = self;
	g_ptr_array_add (self->children, g_object_ref (child));
}

gboolean
lrg_logic_state_remove_state (LrgLogicState *self,
                              LrgLogicState *child)
{
	guint index;

	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), FALSE);
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (child), FALSE);

	if (!g_ptr_array_find (self->children, child, &index))
		return FALSE;
	child->parent = NULL;
	g_ptr_array_remove_index (self->children, index);
	return TRUE;
}

guint
lrg_logic_state_get_state_count (LrgLogicState *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), 0);
	return self->children->len;
}

LrgLogicState *
lrg_logic_state_get_state (LrgLogicState *self,
                           guint          index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	return index < self->children->len ? g_ptr_array_index (self->children, index) : NULL;
}

LrgLogicState *
lrg_logic_state_find_state (LrgLogicState *self,
                            const gchar   *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);

	for (i = 0; name != NULL && i < self->children->len; i++)
	{
		LrgLogicState *c = g_ptr_array_index (self->children, i);

		if (g_strcmp0 (c->name, name) == 0)
			return c;
	}
	return NULL;
}

const gchar *
lrg_logic_state_get_initial (LrgLogicState *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	return self->initial;
}

void
lrg_logic_state_set_initial (LrgLogicState *self,
                             const gchar   *name)
{
	g_return_if_fail (LRG_IS_LOGIC_STATE (self));

	g_free (self->initial);
	self->initial = g_strdup (name);
}

void
lrg_logic_state_add_action (LrgLogicState        *self,
                            LrgLogicPhase         phase,
                            const LrgLogicAction *action)
{
	g_return_if_fail (LRG_IS_LOGIC_STATE (self));
	g_return_if_fail ((guint) phase < N_PHASES);
	g_return_if_fail (action != NULL);

	g_ptr_array_add (self->actions[phase], lrg_logic_action_copy (action));
}

gboolean
lrg_logic_state_remove_action (LrgLogicState *self,
                               LrgLogicPhase  phase,
                               guint          index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), FALSE);
	g_return_val_if_fail ((guint) phase < N_PHASES, FALSE);

	if (index >= self->actions[phase]->len)
		return FALSE;
	g_ptr_array_remove_index (self->actions[phase], index);
	return TRUE;
}

guint
lrg_logic_state_get_action_count (LrgLogicState *self,
                                  LrgLogicPhase  phase)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), 0);
	g_return_val_if_fail ((guint) phase < N_PHASES, 0);
	return self->actions[phase]->len;
}

const LrgLogicAction *
lrg_logic_state_get_action (LrgLogicState *self,
                            LrgLogicPhase  phase,
                            guint          index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	g_return_val_if_fail ((guint) phase < N_PHASES, NULL);
	return index < self->actions[phase]->len ? g_ptr_array_index (self->actions[phase], index) : NULL;
}

void
lrg_logic_state_add_transition (LrgLogicState      *self,
                                LrgLogicTransition *transition)
{
	g_return_if_fail (LRG_IS_LOGIC_STATE (self));
	g_return_if_fail (LRG_IS_LOGIC_TRANSITION (transition));

	g_ptr_array_add (self->transitions, g_object_ref (transition));
}

gboolean
lrg_logic_state_remove_transition (LrgLogicState      *self,
                                   LrgLogicTransition *transition)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), FALSE);
	return g_ptr_array_remove (self->transitions, transition);
}

guint
lrg_logic_state_get_transition_count (LrgLogicState *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), 0);
	return self->transitions->len;
}

LrgLogicTransition *
lrg_logic_state_get_transition (LrgLogicState *self,
                                guint          index)
{
	g_return_val_if_fail (LRG_IS_LOGIC_STATE (self), NULL);
	return index < self->transitions->len ? g_ptr_array_index (self->transitions, index) : NULL;
}

/* ==========================================================================
 * 5. LrgLogicMachine: parsing and writing
 * ========================================================================== */

struct _LrgLogicMachine
{
	GObject              parent_instance;

	/* definition */
	gchar               *name;
	LrgBlackboardSchema *schema;
	LrgLogicState       *root;
	YamlMapping         *extra;
	gboolean             allow_unbound;
	guint                max_transitions;

	/* runtime */
	gboolean             running;
	LrgBlackboard       *blackboard;
	LrgScripting        *scripting;
	GPtrArray           *chain;      /* LrgLogicState (ref), root first */
	GArray              *times;      /* gdouble per chain entry */
	GPtrArray           *paths;      /* gchar * per chain entry */
	GQueue              *events;     /* gchar * */
	guint64              updates;
	LrgLogicTransition  *last;
	gchar               *last_from;
	gchar               *last_to;
	gchar               *last_event;
	GHashTable          *warned;     /* names already warned about */
};

enum
{
	SIGNAL_STATE_ENTERED,
	SIGNAL_STATE_EXITED,
	SIGNAL_TRANSITIONED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgLogicMachine, lrg_logic_machine, G_TYPE_OBJECT)

static void
lrg_logic_machine_dispose (GObject *object)
{
	LrgLogicMachine *self = LRG_LOGIC_MACHINE (object);

	/* no exit actions on disposal: the game is going away */
	self->running = FALSE;
	g_ptr_array_set_size (self->chain, 0);
	g_array_set_size (self->times, 0);
	g_ptr_array_set_size (self->paths, 0);
	g_clear_object (&self->blackboard);
	g_clear_object (&self->scripting);
	g_clear_object (&self->last);
	g_clear_object (&self->root);
	g_clear_object (&self->schema);

	G_OBJECT_CLASS (lrg_logic_machine_parent_class)->dispose (object);
}

static void
lrg_logic_machine_finalize (GObject *object)
{
	LrgLogicMachine *self = LRG_LOGIC_MACHINE (object);

	g_free (self->name);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	g_clear_pointer (&self->chain, g_ptr_array_unref);
	g_clear_pointer (&self->times, g_array_unref);
	g_clear_pointer (&self->paths, g_ptr_array_unref);
	g_queue_free_full (self->events, g_free);
	g_free (self->last_from);
	g_free (self->last_to);
	g_free (self->last_event);
	g_clear_pointer (&self->warned, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_logic_machine_parent_class)->finalize (object);
}

static void
lrg_logic_machine_class_init (LrgLogicMachineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_logic_machine_dispose;
	object_class->finalize = lrg_logic_machine_finalize;

	/**
	 * LrgLogicMachine::state-entered:
	 * @self: the machine
	 * @path: the entered state's path
	 *
	 * A state became active (after its `enter:` actions ran).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_STATE_ENTERED] =
		g_signal_new ("state-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgLogicMachine::state-exited:
	 * @self: the machine
	 * @path: the exited state's path
	 *
	 * A state stopped being active (after its `exit:` actions ran).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_STATE_EXITED] =
		g_signal_new ("state-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgLogicMachine::transitioned:
	 * @self: the machine
	 * @from: the innermost state path before
	 * @to: the innermost state path after
	 * @event: (nullable): the triggering event, %NULL for an event-less
	 *   transition
	 *
	 * A transition was taken (after the exits, `do:` actions and
	 * entries).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_TRANSITIONED] =
		g_signal_new ("transitioned", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_logic_machine_init (LrgLogicMachine *self)
{
	self->schema = lrg_blackboard_schema_new ();
	self->root = lrg_logic_state_new ("");
	self->max_transitions = DEFAULT_MAX_TRANSITIONS;
	self->chain = g_ptr_array_new_with_free_func (g_object_unref);
	self->times = g_array_new (FALSE, TRUE, sizeof (gdouble));
	self->paths = g_ptr_array_new_with_free_func (g_free);
	self->events = g_queue_new ();
	self->warned = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

LrgLogicMachine *
lrg_logic_machine_new (void)
{
	return g_object_new (LRG_TYPE_LOGIC_MACHINE, NULL);
}

/* --- parsing ----------------------------------------------------------- */

/* a transition with its key path, for the target check after parsing */
typedef struct
{
	LrgLogicState      *owner;
	LrgLogicTransition *transition;
	gchar              *path;
} PendingTarget;

static void
pending_free (gpointer data)
{
	PendingTarget *p = data;

	g_free (p->path);
	g_free (p);
}

typedef struct
{
	GError    **error;
	GPtrArray  *targets;    /* PendingTarget */
} Parse;

static LrgLogicState *resolve_target (LrgLogicState *owner, const gchar *target);

/* an `a{sv}` from an ordered dict, NULL when empty */
static GVariant *
dict_variant (GPtrArray *dict)
{
	if (dict == NULL || dict->len == 0)
		return NULL;
	return lrg_ai_priv_dict_to_variant (dict);
}

static gboolean
parse_params (YamlMapping  *map,
              const gchar  *path,
              GVariant    **out,
              GError      **error)
{
	g_autoptr(GPtrArray) dict = NULL;
	g_autofree gchar    *pp = lrg_ai_priv_sub_path (path, "params");

	*out = NULL;
	if (!yaml_mapping_has_member (map, "params"))
		return TRUE;
	if (!lrg_ai_priv_parse_dict (yaml_mapping_get_member (map, "params"), pp, &dict, DOMAIN, error))
		return FALSE;
	*out = dict_variant (dict);
	return TRUE;
}

/* the one key of @keys a mapping has (an error for none or several) */
static gboolean
one_of (YamlMapping        *map,
        const gchar        *path,
        const gchar *const *keys,
        const gchar       **out,
        GError            **error)
{
	guint i;

	*out = NULL;
	for (i = 0; keys[i] != NULL; i++)
	{
		if (!yaml_mapping_has_member (map, keys[i]))
			continue;
		if (*out != NULL)
			return lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path,
			                         "has both '%s' and '%s'", *out, keys[i]);
		*out = keys[i];
	}
	if (*out == NULL)
	{
		g_autofree gchar *list = g_strjoinv (", ", (gchar **) keys);

		return lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path, "needs one of: %s", list);
	}
	return TRUE;
}

/* refuses keys outside @known (actions and conditions keep no extras) */
static gboolean
no_extra (YamlMapping        *map,
          const gchar        *path,
          const gchar *const *known,
          GError            **error)
{
	g_autoptr(YamlMapping) extra = lrg_ai_priv_collect_extra (map, known);

	if (extra != NULL)
	{
		g_autoptr(GList) keys = yaml_mapping_get_members (extra);

		return lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path, "unknown key '%s'",
		                         (const gchar *) keys->data);
	}
	return TRUE;
}

static LrgLogicAction *
parse_action (YamlNode     *node,
              const gchar  *path,
              GError      **error)
{
	static const gchar * const kinds[] = { "action", "script", "set", "event", NULL };
	YamlMapping         *map = lrg_ai_priv_as_mapping (node, path, DOMAIN, error);
	const gchar         *kind;
	const gchar         *name;
	g_autoptr(GVariant)  params = NULL;

	if (map == NULL || !one_of (map, path, kinds, &kind, error))
		return NULL;
	if (!lrg_ai_priv_get_string (map, path, kind, TRUE, &name, DOMAIN, error))
		return NULL;
	if (*name == '\0')
	{
		lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path, "'%s' is empty", kind);
		return NULL;
	}

	if (g_strcmp0 (kind, "set") == 0)
	{
		static const gchar * const known[] = { "set", "value", NULL };
		g_autofree gchar   *vp = lrg_ai_priv_sub_path (path, "value");
		YamlNode           *v = yaml_mapping_get_member (map, "value");
		g_autoptr(GVariant) value = NULL;

		if (!no_extra (map, path, known, error))
			return NULL;
		if (v == NULL)
		{
			lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path, "'set' needs a 'value'");
			return NULL;
		}
		if (lrg_ai_priv_as_scalar (v, vp, DOMAIN, error) == NULL)
			return NULL;
		value = lrg_ai_priv_scalar_to_variant (v);
		return lrg_logic_action_new_set (name, value);
	}
	if (g_strcmp0 (kind, "event") == 0)
	{
		static const gchar * const known[] = { "event", NULL };

		if (!no_extra (map, path, known, error))
			return NULL;
		return lrg_logic_action_new_event (name);
	}
	{
		const gchar * const known[] = { kind, "params", NULL };

		if (!no_extra (map, path, known, error) || !parse_params (map, path, &params, error))
			return NULL;
	}
	if (g_strcmp0 (kind, "action") == 0)
		return lrg_logic_action_new_call (name, params);
	return lrg_logic_action_new_script (name, params);
}

static LrgLogicCondition *
parse_condition (YamlNode     *node,
                 const gchar  *path,
                 GError      **error)
{
	static const gchar * const kinds[] = { "key", "condition", "script", NULL };
	YamlMapping         *map = lrg_ai_priv_as_mapping (node, path, DOMAIN, error);
	const gchar         *kind;
	const gchar         *name;
	g_autoptr(GVariant)  params = NULL;

	if (map == NULL || !one_of (map, path, kinds, &kind, error))
		return NULL;
	if (!lrg_ai_priv_get_string (map, path, kind, TRUE, &name, DOMAIN, error))
		return NULL;
	if (g_strcmp0 (kind, "key") == 0)
	{
		static const gchar * const known[] = { "key", "op", "value", NULL };
		LrgBlackboardOp            op;
		g_autoptr(GVariant)        value = NULL;

		if (!no_extra (map, path, known, error) ||
		    !lrg_ai_priv_parse_op (map, path, &op, &value, DOMAIN, error))
			return NULL;
		return lrg_logic_condition_new_check (name, op, value);
	}
	{
		const gchar * const known[] = { kind, "params", NULL };

		if (!no_extra (map, path, known, error) || !parse_params (map, path, &params, error))
			return NULL;
	}
	if (g_strcmp0 (kind, "condition") == 0)
		return lrg_logic_condition_new_call (name, params);
	return lrg_logic_condition_new_script (name, params);
}

/* an action list (`enter:`, `update:`, `exit:`, `do:`) into @into */
static gboolean
parse_actions (YamlMapping  *map,
               const gchar  *path,
               const gchar  *key,
               GPtrArray    *into,
               GError      **error)
{
	g_autofree gchar *lp = lrg_ai_priv_sub_path (path, key);
	YamlSequence     *seq;
	guint             i;

	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	seq = lrg_ai_priv_as_sequence (yaml_mapping_get_member (map, key), lp, DOMAIN, error);
	if (seq == NULL)
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *ep = lrg_ai_priv_index_path (path, key, i);
		LrgLogicAction   *a = parse_action (yaml_sequence_get_element (seq, i), ep, error);

		if (a == NULL)
			return FALSE;
		g_ptr_array_add (into, a);
	}
	return TRUE;
}

static LrgLogicTransition *
parse_transition (Parse         *ctx,
                  LrgLogicState *owner,
                  YamlNode      *node,
                  const gchar   *path)
{
	static const gchar * const known[] = { "to", "event", "after", "when", "do", NULL };
	g_autoptr(LrgLogicTransition) t = NULL;
	YamlMapping                  *map = lrg_ai_priv_as_mapping (node, path, DOMAIN, ctx->error);
	const gchar                  *to;
	const gchar                  *event;
	gdouble                       after = 0.0;
	gboolean                      present;
	PendingTarget                *pending;

	if (map == NULL)
		return NULL;
	if (!lrg_ai_priv_get_string (map, path, "to", TRUE, &to, DOMAIN, ctx->error) ||
	    !lrg_ai_priv_get_string (map, path, "event", FALSE, &event, DOMAIN, ctx->error) ||
	    !lrg_ai_priv_get_number (map, path, "after", &after, &present, DOMAIN, ctx->error))
		return NULL;
	if (*to == '\0')
	{
		lrg_ai_priv_fail (ctx->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, path, "'to' is empty");
		return NULL;
	}
	if (present && after < 0.0)
	{
		g_autofree gchar *ap = lrg_ai_priv_sub_path (path, "after");

		lrg_ai_priv_fail (ctx->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, ap, "must not be negative");
		return NULL;
	}
	t = lrg_logic_transition_new (to);
	lrg_logic_transition_set_event (t, event);
	t->after = after;

	if (yaml_mapping_has_member (map, "when"))
	{
		g_autofree gchar *wp = lrg_ai_priv_sub_path (path, "when");
		YamlSequence     *seq = lrg_ai_priv_as_sequence (yaml_mapping_get_member (map, "when"), wp, DOMAIN,
		                                                 ctx->error);
		guint             i;

		if (seq == NULL)
			return NULL;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar  *ep = lrg_ai_priv_index_path (path, "when", i);
			LrgLogicCondition *c = parse_condition (yaml_sequence_get_element (seq, i), ep, ctx->error);

			if (c == NULL)
				return NULL;
			g_ptr_array_add (t->conditions, c);
		}
	}
	if (!parse_actions (map, path, "do", t->actions, ctx->error))
		return NULL;
	t->extra = lrg_ai_priv_collect_extra (map, known);

	/* the target is resolved once every state exists */
	pending = g_new0 (PendingTarget, 1);
	pending->owner = owner;
	pending->transition = t;
	pending->path = lrg_ai_priv_sub_path (path, "to");
	g_ptr_array_add (ctx->targets, pending);
	return g_steal_pointer (&t);
}

static gboolean parse_state_body (Parse *ctx, LrgLogicState *state, YamlMapping *map, const gchar *path);

/*
 * parse_state_body:
 *
 * The keys a state and the machine's top level share: initial, the three
 * action lists, transitions and child states.
 */
static gboolean
parse_state_body (Parse         *ctx,
                  LrgLogicState *state,
                  YamlMapping   *map,
                  const gchar   *path)
{
	static const gchar * const phase_keys[N_PHASES] = { "enter", "update", "exit" };
	const gchar *initial;
	guint        p;
	guint        i;

	for (p = 0; p < N_PHASES; p++)
		if (!parse_actions (map, path, phase_keys[p], state->actions[p], ctx->error))
			return FALSE;

	if (yaml_mapping_has_member (map, "states"))
	{
		g_autofree gchar *sp = lrg_ai_priv_sub_path (path, "states");
		YamlSequence     *seq = lrg_ai_priv_as_sequence (yaml_mapping_get_member (map, "states"), sp, DOMAIN,
		                                                 ctx->error);

		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			static const gchar * const known[] = { "name", "initial", "enter", "update", "exit", "transitions",
			                                       "states", NULL };
			g_autofree gchar         *ep = lrg_ai_priv_index_path (path, "states", i);
			YamlMapping              *cm = lrg_ai_priv_as_mapping (yaml_sequence_get_element (seq, i), ep, DOMAIN,
			                                                       ctx->error);
			const gchar              *name;
			g_autoptr(LrgLogicState)  child = NULL;

			if (cm == NULL || !lrg_ai_priv_get_string (cm, ep, "name", TRUE, &name, DOMAIN, ctx->error))
				return FALSE;
			if (*name == '\0' || strchr (name, '/') != NULL)
			{
				g_autofree gchar *np = lrg_ai_priv_sub_path (ep, "name");

				return lrg_ai_priv_fail (ctx->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, np,
				                         "state names are non-empty and contain no '/'");
			}
			if (lrg_logic_state_find_state (state, name) != NULL)
			{
				g_autofree gchar *np = lrg_ai_priv_sub_path (ep, "name");

				return lrg_ai_priv_fail (ctx->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, np,
				                         "duplicate state '%s'", name);
			}
			child = lrg_logic_state_new (name);
			lrg_logic_state_add_state (state, child);
			if (!parse_state_body (ctx, child, cm, ep))
				return FALSE;
			child->extra = lrg_ai_priv_collect_extra (cm, known);
		}
	}

	if (!lrg_ai_priv_get_string (map, path, "initial", FALSE, &initial, DOMAIN, ctx->error))
		return FALSE;
	if (initial != NULL)
	{
		if (lrg_logic_state_find_state (state, initial) == NULL)
		{
			g_autofree gchar *ip = lrg_ai_priv_sub_path (path, "initial");

			return lrg_ai_priv_fail (ctx->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_REFERENCE, ip,
			                         "unknown state '%s'", initial);
		}
		lrg_logic_state_set_initial (state, initial);
	}

	if (yaml_mapping_has_member (map, "transitions"))
	{
		g_autofree gchar *tp = lrg_ai_priv_sub_path (path, "transitions");
		YamlSequence     *seq = lrg_ai_priv_as_sequence (yaml_mapping_get_member (map, "transitions"), tp, DOMAIN,
		                                                 ctx->error);

		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar   *ep = lrg_ai_priv_index_path (path, "transitions", i);
			LrgLogicTransition *t = parse_transition (ctx, state, yaml_sequence_get_element (seq, i), ep);

			if (t == NULL)
				return FALSE;
			g_ptr_array_add (state->transitions, t);
		}
	}
	return TRUE;
}

static gboolean
load_root (LrgLogicMachine  *tmp,
           YamlNode         *root,
           GError          **error)
{
	static const gchar * const top_known[] = { "version", "name", "blackboard", "initial", "enter", "update", "exit",
	                                           "transitions", "states", NULL };
	g_autoptr(GPtrArray) targets = g_ptr_array_new_with_free_func (pending_free);
	Parse                ctx = { error, targets };
	YamlMapping         *map;
	const gchar         *text;
	guint                i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_PARSE, "the document is not a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);

	if (!lrg_ai_priv_get_string (map, "", "version", FALSE, &text, DOMAIN, error))
		return FALSE;
	if (text != NULL)
	{
		gchar  *end = NULL;
		gint64  v = g_ascii_strtoll (text, &end, 10);

		if (end == text || *end != '\0' || v < 1)
			return lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, "version",
			                         "expected a positive integer");
		if (v > LRG_LOGIC_MACHINE_FORMAT_VERSION)
		{
			g_set_error (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_VERSION,
			             "version: %" G_GINT64_FORMAT " is newer than this build reads (%d)", v,
			             LRG_LOGIC_MACHINE_FORMAT_VERSION);
			return FALSE;
		}
	}
	if (!lrg_ai_priv_get_string (map, "", "name", FALSE, &text, DOMAIN, error))
		return FALSE;
	tmp->name = g_strdup (text);
	if (yaml_mapping_has_member (map, "blackboard") &&
	    !lrg_ai_priv_parse_schema (tmp->schema, yaml_mapping_get_member (map, "blackboard"), "blackboard", DOMAIN,
	                               error))
		return FALSE;
	if (!parse_state_body (&ctx, tmp->root, map, ""))
		return FALSE;

	/* every transition target resolves */
	for (i = 0; i < targets->len; i++)
	{
		PendingTarget *p = g_ptr_array_index (targets, i);
		LrgLogicState *t = resolve_target (p->owner, p->transition->target);

		if (t == NULL || t->parent == NULL)
			return lrg_ai_priv_fail (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_REFERENCE, p->path,
			                         "unknown state '%s'", p->transition->target);
	}
	tmp->extra = lrg_ai_priv_collect_extra (map, top_known);
	return TRUE;
}

gboolean
lrg_logic_machine_load_from_data (LrgLogicMachine  *self,
                                  const gchar      *data,
                                  gssize            length,
                                  GError          **error)
{
	g_autoptr(YamlParser)      parser = NULL;
	g_autoptr(GError)          local = NULL;
	g_autoptr(LrgLogicMachine) tmp = NULL;
	YamlNode                  *root;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_PARSE, "%s", local->message);
		return FALSE;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error_literal (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_PARSE, "the document is empty");
		return FALSE;
	}
	tmp = lrg_logic_machine_new ();
	if (!load_root (tmp, root, error))
		return FALSE;

	/* adopt the definition */
	lrg_logic_machine_stop (self);
	g_free (self->name);
	self->name = g_steal_pointer (&tmp->name);
	g_set_object (&self->schema, tmp->schema);
	g_set_object (&self->root, tmp->root);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	self->extra = g_steal_pointer (&tmp->extra);
	return TRUE;
}

gboolean
lrg_logic_machine_load_from_file (LrgLogicMachine  *self,
                                  const gchar      *path,
                                  GError          **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	if (!lrg_logic_machine_load_from_data (self, text, (gssize) len, error))
	{
		g_prefix_error (error, "%s: ", path);
		return FALSE;
	}
	return TRUE;
}

LrgLogicMachine *
lrg_logic_machine_new_from_file (const gchar  *path,
                                 GError      **error)
{
	g_autoptr(LrgLogicMachine) self = lrg_logic_machine_new ();

	if (!lrg_logic_machine_load_from_file (self, path, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgLogicMachine *
lrg_logic_machine_new_from_data (const gchar  *data,
                                 gssize        length,
                                 GError      **error)
{
	g_autoptr(LrgLogicMachine) self = lrg_logic_machine_new ();

	if (!lrg_logic_machine_load_from_data (self, data, length, error))
		return NULL;
	return g_steal_pointer (&self);
}

/* --- writing ----------------------------------------------------------- */

static YamlNode *
params_node (GVariant *params)
{
	YamlNode     *node = yaml_node_new_mapping (NULL);
	YamlMapping  *map = yaml_node_get_mapping (node);
	GVariantIter  iter;
	const gchar  *k;
	GVariant     *v;

	g_variant_iter_init (&iter, params);
	while (g_variant_iter_next (&iter, "{&sv}", &k, &v))
	{
		lrg_ai_priv_put (map, k, lrg_ai_priv_variant_to_node (v));
		g_variant_unref (v);
	}
	return node;
}

static YamlNode *
write_action (const LrgLogicAction *a)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	const gchar *key = a->kind == LRG_LOGIC_ACTION_CALL ? "action" :
	                   a->kind == LRG_LOGIC_ACTION_SCRIPT ? "script" :
	                   a->kind == LRG_LOGIC_ACTION_SET ? "set" : "event";

	lrg_ai_priv_put (map, key, lrg_ai_priv_variant_to_node (g_variant_new_string (a->name)));
	if (a->value != NULL)
		lrg_ai_priv_put (map, "value", lrg_ai_priv_variant_to_node (a->value));
	if (a->params != NULL)
		lrg_ai_priv_put (map, "params", params_node (a->params));
	return node;
}

static YamlNode *
write_condition (const LrgLogicCondition *c)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);

	if (c->kind == LRG_LOGIC_CONDITION_CHECK)
	{
		gboolean default_op;

		lrg_ai_priv_put (map, "key", lrg_ai_priv_variant_to_node (g_variant_new_string (c->name)));
		/* the operator is left out when it is what its absence means */
		default_op = (c->value != NULL && c->op == LRG_BLACKBOARD_OP_EQ) ||
		             (c->value == NULL && c->op == LRG_BLACKBOARD_OP_SET);
		if (!default_op)
			yaml_mapping_set_string_member (map, "op", lrg_blackboard_op_to_string (c->op));
		if (c->value != NULL)
			lrg_ai_priv_put (map, "value", lrg_ai_priv_variant_to_node (c->value));
		return node;
	}
	lrg_ai_priv_put (map, c->kind == LRG_LOGIC_CONDITION_CALL ? "condition" : "script",
	                 lrg_ai_priv_variant_to_node (g_variant_new_string (c->name)));
	if (c->params != NULL)
		lrg_ai_priv_put (map, "params", params_node (c->params));
	return node;
}

static void
write_actions (YamlMapping *map,
               const gchar *key,
               GPtrArray   *actions)
{
	YamlNode     *node;
	YamlSequence *seq;
	guint         i;

	if (actions->len == 0)
		return;
	node = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < actions->len; i++)
	{
		YamlNode *e = write_action (g_ptr_array_index (actions, i));

		yaml_sequence_add_element (seq, e);
		yaml_node_unref (e);
	}
	lrg_ai_priv_put (map, key, node);
}

static YamlNode *
write_transition (LrgLogicTransition *t)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	guint        i;

	lrg_ai_priv_put (map, "to", lrg_ai_priv_variant_to_node (g_variant_new_string (t->target)));
	if (t->event != NULL)
		lrg_ai_priv_put (map, "event", lrg_ai_priv_variant_to_node (g_variant_new_string (t->event)));
	if (t->after > 0.0)
		lrg_ai_priv_put (map, "after", lrg_ai_priv_number_node (t->after));
	if (t->conditions->len > 0)
	{
		YamlNode     *wnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (wnode);

		for (i = 0; i < t->conditions->len; i++)
		{
			YamlNode *e = write_condition (g_ptr_array_index (t->conditions, i));

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		lrg_ai_priv_put (map, "when", wnode);
	}
	write_actions (map, "do", t->actions);
	lrg_ai_priv_copy_extra (map, t->extra);
	return node;
}

static void write_state_body (YamlMapping *map, LrgLogicState *state);

static YamlNode *
write_state (LrgLogicState *state)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);

	lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (state->name)));
	write_state_body (map, state);
	lrg_ai_priv_copy_extra (map, state->extra);
	return node;
}

static void
write_state_body (YamlMapping   *map,
                  LrgLogicState *state)
{
	guint i;

	if (state->initial != NULL)
		lrg_ai_priv_put (map, "initial", lrg_ai_priv_variant_to_node (g_variant_new_string (state->initial)));
	write_actions (map, "enter", state->actions[LRG_LOGIC_PHASE_ENTER]);
	write_actions (map, "update", state->actions[LRG_LOGIC_PHASE_UPDATE]);
	write_actions (map, "exit", state->actions[LRG_LOGIC_PHASE_EXIT]);
	if (state->transitions->len > 0)
	{
		YamlNode     *tnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (tnode);

		for (i = 0; i < state->transitions->len; i++)
		{
			YamlNode *e = write_transition (g_ptr_array_index (state->transitions, i));

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		lrg_ai_priv_put (map, "transitions", tnode);
	}
	if (state->children->len > 0)
	{
		YamlNode     *snode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (snode);

		for (i = 0; i < state->children->len; i++)
		{
			YamlNode *e = write_state (g_ptr_array_index (state->children, i));

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		lrg_ai_priv_put (map, "states", snode);
	}
}

gchar *
lrg_logic_machine_to_data (LrgLogicMachine  *self,
                           GError          **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlMapping             *map;
	YamlNode                *schema;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_LOGIC_MACHINE_FORMAT_VERSION);
	if (self->name != NULL)
		lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (self->name)));
	schema = lrg_ai_priv_write_schema (self->schema);
	if (schema != NULL)
		lrg_ai_priv_put (map, "blackboard", schema);
	write_state_body (map, self->root);
	lrg_ai_priv_copy_extra (map, self->extra);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_logic_machine_save_to_file (LrgLogicMachine  *self,
                                const gchar      *path,
                                GError          **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_logic_machine_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

LrgLogicMachine *
lrg_logic_machine_duplicate (LrgLogicMachine  *self,
                             GError          **error)
{
	g_autofree gchar *text = NULL;
	LrgLogicMachine  *copy;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	text = lrg_logic_machine_to_data (self, error);
	if (text == NULL)
		return NULL;
	copy = lrg_logic_machine_new_from_data (text, -1, error);
	if (copy == NULL)
		return NULL;
	copy->allow_unbound = self->allow_unbound;
	copy->max_transitions = self->max_transitions;
	return copy;
}

/* --- accessors --------------------------------------------------------- */

const gchar *
lrg_logic_machine_get_name (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	return self->name;
}

void
lrg_logic_machine_set_name (LrgLogicMachine *self,
                            const gchar     *name)
{
	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));

	g_free (self->name);
	self->name = g_strdup (name);
}

LrgBlackboardSchema *
lrg_logic_machine_get_schema (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	return self->schema;
}

LrgLogicState *
lrg_logic_machine_get_root (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	return self->root;
}

/* descends @path ('/'-separated names) from @from */
static LrgLogicState *
descend (LrgLogicState *from,
         const gchar   *path)
{
	g_auto(GStrv)  parts = NULL;
	LrgLogicState *s = from;
	guint          i;

	if (path == NULL)
		return NULL;
	parts = g_strsplit (path, "/", -1);
	for (i = 0; s != NULL && parts[i] != NULL; i++)
	{
		if (*parts[i] == '\0')
			continue;
		s = lrg_logic_state_find_state (s, parts[i]);
	}
	return s;
}

LrgLogicState *
lrg_logic_machine_find_state (LrgLogicMachine *self,
                              const gchar     *path)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	return descend (self->root, path);
}

gboolean
lrg_logic_machine_get_allow_unbound (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	return self->allow_unbound;
}

void
lrg_logic_machine_set_allow_unbound (LrgLogicMachine *self,
                                     gboolean         allow)
{
	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));
	self->allow_unbound = allow;
}

guint
lrg_logic_machine_get_max_transitions (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), DEFAULT_MAX_TRANSITIONS);
	return self->max_transitions;
}

void
lrg_logic_machine_set_max_transitions (LrgLogicMachine *self,
                                       guint            max)
{
	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));
	self->max_transitions = MAX (max, 1u);
}

/* ==========================================================================
 * 6. Target resolution and validation
 * ========================================================================== */

static LrgLogicState *
root_of (LrgLogicState *s)
{
	while (s != NULL && s->parent != NULL)
		s = s->parent;
	return s;
}

/*
 * resolve_target:
 *
 * `/a/b` from the root; otherwise the first segment is looked up among
 * the owner's siblings, then its parent's siblings and so on up to the
 * top-level states, then among the owner's own children; the rest of the
 * path descends from there.
 */
static LrgLogicState *
resolve_target (LrgLogicState *owner,
                const gchar   *target)
{
	g_auto(GStrv)  parts = NULL;
	LrgLogicState *scope;
	LrgLogicState *found = NULL;
	guint          i;

	if (target == NULL || *target == '\0')
		return NULL;
	if (target[0] == '/')
		return descend (root_of (owner), target + 1);
	parts = g_strsplit (target, "/", -1);
	for (scope = owner->parent; scope != NULL && found == NULL; scope = scope->parent)
		found = lrg_logic_state_find_state (scope, parts[0]);
	if (found == NULL)
		found = lrg_logic_state_find_state (owner, parts[0]);
	for (i = 1; found != NULL && parts[i] != NULL; i++)
		found = lrg_logic_state_find_state (found, parts[i]);
	return found;
}

/* the child a composite state enters first */
static LrgLogicState *
initial_child (LrgLogicState *state)
{
	if (state->children->len == 0)
		return NULL;
	if (state->initial != NULL)
	{
		LrgLogicState *c = lrg_logic_state_find_state (state, state->initial);

		if (c != NULL)
			return c;
	}
	return g_ptr_array_index (state->children, 0);
}

typedef struct
{
	LrgLogicMachine *machine;
	LrgScripting    *scripting;
	GPtrArray       *missing;     /* or NULL */
	GError         **error;
} Check;

static gchar *
state_label (LrgLogicState *s)
{
	g_autofree gchar *path = lrg_logic_state_dup_path (s);

	return *path == '\0' ? g_strdup ("machine") : g_strdup_printf ("state '%s'", path);
}

static gboolean
check_key (Check       *c,
           const gchar *key,
           const gchar *where)
{
	if (lrg_blackboard_schema_get_key_count (c->machine->schema) == 0 ||
	    lrg_blackboard_schema_has_key (c->machine->schema, key))
		return TRUE;
	return lrg_ai_priv_fail (c->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_REFERENCE, where,
	                         "blackboard key '%s' is not declared under blackboard:", key);
}

static void
note_missing (Check       *c,
              const gchar *what,
              const gchar *name)
{
	g_autofree gchar *entry = NULL;
	guint             i;

	if (c->missing == NULL)
		return;
	entry = g_strdup_printf ("%s:%s", what, name);
	for (i = 0; i < c->missing->len; i++)
		if (g_strcmp0 (g_ptr_array_index (c->missing, i), entry) == 0)
			return;
	g_ptr_array_add (c->missing, g_steal_pointer (&entry));
}

static gboolean
check_action (Check                *c,
              const LrgLogicAction *a,
              const gchar          *where)
{
	if (a->kind == LRG_LOGIC_ACTION_SET)
		return check_key (c, a->name, where);
	if (a->kind == LRG_LOGIC_ACTION_CALL && !lrg_bt_has_action (a->name))
		note_missing (c, "action", a->name);
	if (a->kind == LRG_LOGIC_ACTION_SCRIPT &&
	    (c->scripting == NULL || !lrg_scripting_has_function (c->scripting, a->name)))
		note_missing (c, "script", a->name);
	return TRUE;
}

static gboolean
check_state (Check         *c,
             LrgLogicState *s)
{
	g_autofree gchar *label = state_label (s);
	guint             p;
	guint             i;
	guint             j;

	for (p = 0; p < N_PHASES; p++)
		for (i = 0; i < s->actions[p]->len; i++)
			if (!check_action (c, g_ptr_array_index (s->actions[p], i), label))
				return FALSE;

	/* child names unique, the initial one exists */
	for (i = 0; i < s->children->len; i++)
	{
		LrgLogicState *a = g_ptr_array_index (s->children, i);

		if (a->name == NULL || *a->name == '\0' || strchr (a->name, '/') != NULL)
			return lrg_ai_priv_fail (c->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, label,
			                         "a child state has an empty name or one with '/'");
		for (j = i + 1; j < s->children->len; j++)
			if (g_strcmp0 (a->name, ((LrgLogicState *) g_ptr_array_index (s->children, j))->name) == 0)
				return lrg_ai_priv_fail (c->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, label,
				                         "duplicate state '%s'", a->name);
	}
	if (s->initial != NULL && lrg_logic_state_find_state (s, s->initial) == NULL)
		return lrg_ai_priv_fail (c->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_REFERENCE, label,
		                         "initial state '%s' is not a child", s->initial);

	for (i = 0; i < s->transitions->len; i++)
	{
		LrgLogicTransition *t = g_ptr_array_index (s->transitions, i);
		LrgLogicState      *target = resolve_target (s, t->target);
		g_autofree gchar   *where = g_strdup_printf ("%s transition %u", label, i);

		if (target == NULL || target->parent == NULL)
			return lrg_ai_priv_fail (c->error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_REFERENCE, where,
			                         "unknown state '%s'", t->target);
		for (j = 0; j < t->conditions->len; j++)
		{
			LrgLogicCondition *cond = g_ptr_array_index (t->conditions, j);

			if (cond->kind == LRG_LOGIC_CONDITION_CHECK && !check_key (c, cond->name, where))
				return FALSE;
			if (cond->kind == LRG_LOGIC_CONDITION_CALL && !lrg_bt_has_condition (cond->name))
				note_missing (c, "condition", cond->name);
			if (cond->kind == LRG_LOGIC_CONDITION_SCRIPT &&
			    (c->scripting == NULL || !lrg_scripting_has_function (c->scripting, cond->name)))
				note_missing (c, "script", cond->name);
		}
		for (j = 0; j < t->actions->len; j++)
			if (!check_action (c, g_ptr_array_index (t->actions, j), where))
				return FALSE;
	}

	for (i = 0; i < s->children->len; i++)
		if (!check_state (c, g_ptr_array_index (s->children, i)))
			return FALSE;
	return TRUE;
}

static gboolean
check_machine (LrgLogicMachine  *self,
               LrgScripting     *scripting,
               GPtrArray        *missing,
               GError          **error)
{
	Check c = { self, scripting, missing, error };

	if (self->root->children->len == 0)
	{
		g_set_error_literal (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_INVALID, "states: the machine has no states");
		return FALSE;
	}
	return check_state (&c, self->root);
}

gboolean
lrg_logic_machine_validate (LrgLogicMachine  *self,
                            GError          **error)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	return check_machine (self, NULL, NULL, error);
}

gchar **
lrg_logic_machine_dup_unbound (LrgLogicMachine *self,
                               LrgScripting    *scripting)
{
	GPtrArray *missing;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	g_return_val_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting), NULL);

	missing = g_ptr_array_new ();
	(void) check_machine (self, scripting, missing, NULL);
	g_ptr_array_add (missing, NULL);
	return (gchar **) g_ptr_array_free (missing, FALSE);
}

/* ==========================================================================
 * 7. The runtime
 * ========================================================================== */

/* logs a missing binding once per name */
static void
warn_once (LrgLogicMachine *self,
           const gchar     *what,
           const gchar     *name,
           const gchar     *why)
{
	g_autofree gchar *key = g_strdup_printf ("%s:%s", what, name);

	if (g_hash_table_contains (self->warned, key))
		return;
	lrg_warning (LRG_LOG_DOMAIN, "logic machine %s: %s '%s' %s", self->name != NULL ? self->name : "(unnamed)", what,
	             name, why);
	g_hash_table_add (self->warned, g_steal_pointer (&key));
}

static void
run_action (LrgLogicMachine      *self,
            const LrgLogicAction *a,
            gfloat                delta_time)
{
	switch (a->kind)
	{
	case LRG_LOGIC_ACTION_CALL:
	{
		LrgAiBound          *bound = lrg_ai_priv_lookup_action (a->name);
		g_autoptr(GVariant)  params = NULL;

		if (bound == NULL)
		{
			warn_once (self, "action", a->name, "is not registered; skipped");
			return;
		}
		params = a->params != NULL ? g_variant_ref (a->params) : lrg_ai_priv_empty_dict ();
		(void) lrg_ai_priv_bound_act (bound, self->blackboard, delta_time, params);
		lrg_ai_priv_bound_unref (bound);
		return;
	}

	case LRG_LOGIC_ACTION_SCRIPT:
	{
		GValue            ret = G_VALUE_INIT;
		g_autoptr(GError) error = NULL;

		if (self->scripting == NULL || !lrg_scripting_has_function (self->scripting, a->name))
		{
			warn_once (self, "script", a->name, "is not defined; skipped");
			return;
		}
		if (!lrg_ai_priv_call_script (self->scripting, a->name, self->blackboard, delta_time, a->params, &ret,
		                              &error))
			warn_once (self, "script", a->name, error != NULL ? error->message : "failed");
		if (G_IS_VALUE (&ret))
			g_value_unset (&ret);
		return;
	}

	case LRG_LOGIC_ACTION_SET:
		lrg_blackboard_set_variant (self->blackboard, a->name, a->value);
		return;

	case LRG_LOGIC_ACTION_EVENT:
	default:
		g_queue_push_tail (self->events, g_strdup (a->name));
		return;
	}
}

static void
run_actions (LrgLogicMachine *self,
             GPtrArray       *actions,
             gfloat           delta_time)
{
	guint i;

	for (i = 0; i < actions->len; i++)
		run_action (self, g_ptr_array_index (actions, i), delta_time);
}

static gboolean
condition_holds (LrgLogicMachine         *self,
                 const LrgLogicCondition *c)
{
	switch (c->kind)
	{
	case LRG_LOGIC_CONDITION_CHECK:
		return lrg_blackboard_test (self->blackboard, c->name, c->op, c->value);

	case LRG_LOGIC_CONDITION_CALL:
	{
		LrgAiBound          *bound = lrg_ai_priv_lookup_condition (c->name);
		g_autoptr(GVariant)  params = NULL;
		gboolean             truth;

		if (bound == NULL)
		{
			warn_once (self, "condition", c->name, "is not registered; false");
			return FALSE;
		}
		params = c->params != NULL ? g_variant_ref (c->params) : lrg_ai_priv_empty_dict ();
		truth = lrg_ai_priv_bound_test (bound, self->blackboard, params);
		lrg_ai_priv_bound_unref (bound);
		return truth;
	}

	case LRG_LOGIC_CONDITION_SCRIPT:
	default:
	{
		GValue            ret = G_VALUE_INIT;
		g_autoptr(GError) error = NULL;
		gboolean          truth = FALSE;

		if (self->scripting == NULL || !lrg_scripting_has_function (self->scripting, c->name))
		{
			warn_once (self, "script", c->name, "is not defined; false");
			return FALSE;
		}
		if (lrg_ai_priv_call_script (self->scripting, c->name, self->blackboard, 0.0f, c->params, &ret, &error))
			truth = lrg_ai_priv_truth_of_value (&ret);
		else
			warn_once (self, "script", c->name, error != NULL ? error->message : "failed");
		if (G_IS_VALUE (&ret))
			g_value_unset (&ret);
		return truth;
	}
	}
}

/* pushes @state onto the active chain and runs its enter actions */
static void
enter_state (LrgLogicMachine *self,
             LrgLogicState   *state)
{
	gdouble zero = 0.0;
	gchar  *path = lrg_logic_state_dup_path (state);

	g_ptr_array_add (self->chain, g_object_ref (state));
	g_array_append_val (self->times, zero);
	g_ptr_array_add (self->paths, path);
	run_actions (self, state->actions[LRG_LOGIC_PHASE_ENTER], 0.0f);
	if (state->parent != NULL)
		g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0, path);
}

/* enters the initial chain below @state */
static void
enter_initial (LrgLogicMachine *self,
               LrgLogicState   *state)
{
	LrgLogicState *c;

	for (c = initial_child (state); c != NULL; c = initial_child (c))
		enter_state (self, c);
}

/* pops the innermost active state, running its exit actions */
static void
exit_top (LrgLogicMachine *self)
{
	guint             last = self->chain->len - 1;
	LrgLogicState    *state = g_ptr_array_index (self->chain, last);
	g_autofree gchar *path = g_strdup (g_ptr_array_index (self->paths, last));

	run_actions (self, state->actions[LRG_LOGIC_PHASE_EXIT], 0.0f);
	g_ptr_array_remove_index (self->chain, last);
	g_array_remove_index (self->times, last);
	g_ptr_array_remove_index (self->paths, last);
	if (*path != '\0')
		g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0, path);
}

static gboolean
is_ancestor (LrgLogicState *a,
             LrgLogicState *s)
{
	for (s = s->parent; s != NULL; s = s->parent)
		if (s == a)
			return TRUE;
	return FALSE;
}

static gint
chain_index (LrgLogicMachine *self,
             LrgLogicState   *state)
{
	guint i;

	for (i = 0; i < self->chain->len; i++)
		if (g_ptr_array_index (self->chain, i) == state)
			return (gint) i;
	return -1;
}

/*
 * fire:
 *
 * Takes transition @t from @owner to @target: exits the active states
 * below the domain (the deepest state strictly containing both; the root
 * for any-state transitions), runs the transition's actions, enters the
 * states from the domain down to @target and then @target's initial
 * chain.
 */
static void
fire (LrgLogicMachine    *self,
      LrgLogicState      *owner,
      LrgLogicTransition *t,
      LrgLogicState      *target,
      const gchar        *event)
{
	g_autofree gchar     *from = g_strdup (lrg_logic_machine_get_active_state (self));
	g_autoptr(GPtrArray)  down = g_ptr_array_new ();
	LrgLogicState        *domain;
	LrgLogicState        *s;
	gint                  di;
	gint                  i;

	if (owner->parent == NULL)
		domain = owner;
	else
	{
		for (domain = owner->parent; domain != NULL && !is_ancestor (domain, target); domain = domain->parent)
			;
		if (domain == NULL)
			domain = root_of (owner);
	}
	di = chain_index (self, domain);
	while ((gint) self->chain->len > di + 1)
		exit_top (self);

	g_object_ref (t);
	run_actions (self, t->actions, 0.0f);
	for (s = target; s != NULL && s != domain; s = s->parent)
		g_ptr_array_add (down, s);
	for (i = (gint) down->len - 1; i >= 0; i--)
		enter_state (self, g_ptr_array_index (down, i));
	enter_initial (self, target);

	t->fired++;
	g_set_object (&self->last, t);
	g_free (self->last_from);
	self->last_from = g_steal_pointer (&from);
	g_free (self->last_to);
	self->last_to = g_strdup (lrg_logic_machine_get_active_state (self));
	g_free (self->last_event);
	self->last_event = g_strdup (event);
	g_signal_emit (self, signals[SIGNAL_TRANSITIONED], 0, self->last_from, self->last_to, event);
	g_object_unref (t);
}

/*
 * try_fire:
 *
 * Looks for the first transition that may be taken now: outermost active
 * state first, each state's transitions in order. @event NULL considers
 * event-less transitions, otherwise those of that event.
 */
static gboolean
try_fire (LrgLogicMachine *self,
          const gchar     *event)
{
	guint i;
	guint j;
	guint k;

	for (i = 0; i < self->chain->len; i++)
	{
		LrgLogicState *s = g_ptr_array_index (self->chain, i);
		gdouble        time = g_array_index (self->times, gdouble, i);

		for (j = 0; j < s->transitions->len; j++)
		{
			LrgLogicTransition *t = g_ptr_array_index (s->transitions, j);
			LrgLogicState      *target;
			gboolean            ok = TRUE;

			if (g_strcmp0 (t->event, event) != 0)
				continue;
			if (t->after > 0.0 && time + 1e-9 < t->after)
				continue;
			target = resolve_target (s, t->target);
			if (target == NULL || target->parent == NULL)
			{
				warn_once (self, "target", t->target, "does not resolve; transition skipped");
				continue;
			}
			/* an any-state transition does not re-enter where it leads */
			if (s->parent == NULL && chain_index (self, target) >= 0)
				continue;
			for (k = 0; ok && k < t->conditions->len; k++)
				ok = condition_holds (self, g_ptr_array_index (t->conditions, k));
			if (!ok)
				continue;
			fire (self, s, t, target, event);
			return TRUE;
		}
	}
	return FALSE;
}

gboolean
lrg_logic_machine_start (LrgLogicMachine  *self,
                         LrgBlackboard    *blackboard,
                         LrgScripting     *scripting,
                         GError          **error)
{
	g_autoptr(GPtrArray) missing = NULL;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	g_return_val_if_fail (blackboard == NULL || LRG_IS_BLACKBOARD (blackboard), FALSE);
	g_return_val_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting), FALSE);

	lrg_logic_machine_stop (self);
	missing = g_ptr_array_new_with_free_func (g_free);
	if (!check_machine (self, scripting, missing, error))
		return FALSE;
	if (missing->len > 0 && !self->allow_unbound)
	{
		g_set_error (error, DOMAIN, LRG_LOGIC_MACHINE_ERROR_BINDING, "%s is not bound",
		             (const gchar *) g_ptr_array_index (missing, 0));
		return FALSE;
	}

	if (blackboard != NULL)
		g_set_object (&self->blackboard, blackboard);
	else if (self->blackboard == NULL)
		self->blackboard = lrg_blackboard_new ();
	g_set_object (&self->scripting, scripting);
	lrg_blackboard_schema_apply_defaults (self->schema, self->blackboard, FALSE);

	self->running = TRUE;
	self->updates = 0;
	g_clear_object (&self->last);
	g_clear_pointer (&self->last_from, g_free);
	g_clear_pointer (&self->last_to, g_free);
	g_clear_pointer (&self->last_event, g_free);
	g_hash_table_remove_all (self->warned);
	enter_state (self, self->root);
	enter_initial (self, self->root);
	return TRUE;
}

void
lrg_logic_machine_stop (LrgLogicMachine *self)
{
	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));

	if (!self->running)
		return;
	while (self->chain->len > 0)
		exit_top (self);
	self->running = FALSE;
	while (!g_queue_is_empty (self->events))
		g_free (g_queue_pop_head (self->events));
}

gboolean
lrg_logic_machine_is_running (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	return self->running;
}

void
lrg_logic_machine_update (LrgLogicMachine *self,
                          gfloat           delta_time)
{
	guint n;
	guint i;
	g_autoptr(GPtrArray) snapshot = NULL;

	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));

	if (!self->running)
		return;
	g_object_ref (self);
	self->updates++;

	/* 1. timers */
	for (i = 0; i < self->times->len; i++)
		g_array_index (self->times, gdouble, i) += (gdouble) delta_time;

	/* 2. the events queued before this update, one transition each */
	n = g_queue_get_length (self->events);
	for (i = 0; i < n && self->running; i++)
	{
		g_autofree gchar *event = g_queue_pop_head (self->events);

		(void) try_fire (self, event);
	}

	/* 3. event-less transitions until none fires (capped) */
	for (i = 0; i < self->max_transitions && self->running; i++)
		if (!try_fire (self, NULL))
			break;

	/* 4. update actions, outermost first, over a snapshot of the chain */
	if (self->running)
	{
		snapshot = g_ptr_array_new_with_free_func (g_object_unref);
		for (i = 0; i < self->chain->len; i++)
			g_ptr_array_add (snapshot, g_object_ref (g_ptr_array_index (self->chain, i)));
		for (i = 0; i < snapshot->len; i++)
			run_actions (self, ((LrgLogicState *) g_ptr_array_index (snapshot, i))->actions[LRG_LOGIC_PHASE_UPDATE],
			             delta_time);
	}
	g_object_unref (self);
}

void
lrg_logic_machine_send_event (LrgLogicMachine *self,
                              const gchar     *event)
{
	g_return_if_fail (LRG_IS_LOGIC_MACHINE (self));
	g_return_if_fail (event != NULL && *event != '\0');

	g_queue_push_tail (self->events, g_strdup (event));
}

LrgBlackboard *
lrg_logic_machine_get_blackboard (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);
	return self->blackboard;
}

const gchar *
lrg_logic_machine_get_active_state (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	if (!self->running || self->paths->len < 2)
		return NULL;
	return g_ptr_array_index (self->paths, self->paths->len - 1);
}

gchar **
lrg_logic_machine_dup_active_path (LrgLogicMachine *self)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	out = g_ptr_array_new ();
	for (i = 1; self->running && i < self->paths->len; i++)
		g_ptr_array_add (out, g_strdup (g_ptr_array_index (self->paths, i)));
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* the chain index of an active state's path (a leading '/' allowed) */
static gint
path_index (LrgLogicMachine *self,
            const gchar     *path)
{
	guint i;

	if (path != NULL && path[0] == '/')
		path++;
	for (i = 1; i < self->paths->len; i++)
		if (g_strcmp0 (g_ptr_array_index (self->paths, i), path) == 0)
			return (gint) i;
	return -1;
}

gboolean
lrg_logic_machine_is_in_state (LrgLogicMachine *self,
                               const gchar     *path)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	return self->running && path_index (self, path) >= 0;
}

gdouble
lrg_logic_machine_get_state_time (LrgLogicMachine *self,
                                  const gchar     *path)
{
	gint i;

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), -1.0);

	if (!self->running || self->times->len < 2)
		return -1.0;
	i = path == NULL ? (gint) self->times->len - 1 : path_index (self, path);
	return i >= 0 ? g_array_index (self->times, gdouble, i) : -1.0;
}

guint64
lrg_logic_machine_get_update_count (LrgLogicMachine *self)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), 0);
	return self->updates;
}

LrgLogicTransition *
lrg_logic_machine_get_last_transition (LrgLogicMachine  *self,
                                       const gchar     **out_from,
                                       const gchar     **out_to,
                                       const gchar     **out_event)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	if (out_from != NULL)
		*out_from = self->last_from;
	if (out_to != NULL)
		*out_to = self->last_to;
	if (out_event != NULL)
		*out_event = self->last_event;
	return self->last;
}

gchar *
lrg_logic_machine_dup_debug_state (LrgLogicMachine *self)
{
	GString *out;
	GList   *l;
	guint    i;
	gchar    buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (self), NULL);

	out = g_string_new (NULL);
	g_string_append_printf (out, "running: %s\nupdates: %" G_GUINT64_FORMAT "\nactive:",
	                        self->running ? "true" : "false", self->updates);
	if (!self->running || self->paths->len < 2)
		g_string_append (out, " []");
	for (i = 1; self->running && i < self->paths->len; i++)
		g_string_append_printf (out, "\n- {state: \"%s\", time: %s}", (const gchar *) g_ptr_array_index (self->paths, i),
		                        g_ascii_formatd (buf, sizeof buf, "%.4f", g_array_index (self->times, gdouble, i)));
	if (self->last != NULL)
		g_string_append_printf (out, "\nlast_transition: {from: \"%s\", to: \"%s\", event: %s%s%s}",
		                        self->last_from != NULL ? self->last_from : "",
		                        self->last_to != NULL ? self->last_to : "",
		                        self->last_event != NULL ? "\"" : "",
		                        self->last_event != NULL ? self->last_event : "~",
		                        self->last_event != NULL ? "\"" : "");
	else
		g_string_append (out, "\nlast_transition: ~");
	g_string_append (out, "\nqueued: [");
	for (l = self->events->head; l != NULL; l = l->next)
		g_string_append_printf (out, "%s\"%s\"", l == self->events->head ? "" : ", ", (const gchar *) l->data);
	g_string_append (out, "]\n");
	return g_string_free (out, FALSE);
}
