/* lrg-ai-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAiComponent: one AI step (lrg_ai_component_step()):
 *   1. start when needed: load the .rbt / .rfsm given by path, resolve the
 *      scripting context (explicit, else the owner's LrgScriptComponent),
 *      build the LrgBtRunner and start the LrgLogicMachine on the shared
 *      blackboard (failures are logged once, not retried until restart);
 *   2. honour the tick interval (accumulate time, step with the sum);
 *   3. update the machine, then tick the tree; report a finished tree and
 *      state changes through signals.
 * The frame update or the fixed update calls the step, by setting.
 */

#include "config.h"

#include "lrg-ai-component.h"
#include "../lrg-entity3d.h"
#include "../../ecs/lrg-game-object.h"
#include "../../ecs/components/lrg-script-component.h"

/* how many updates a start waits for an owner script to load */
#define SCRIPT_WAIT_UPDATES (2)

struct _LrgAiComponent
{
	LrgComponent     parent_instance;

	LrgBlackboard   *blackboard;
	LrgScripting    *scripting;       /* explicit, or NULL */

	LrgBtAsset      *tree;
	gchar           *tree_path;
	LrgBtRunner     *runner;
	gboolean         tree_failed;

	LrgLogicMachine *machine;
	gchar           *machine_path;
	gboolean         machine_failed;
	gulong           transitioned_id;

	gboolean         started;
	guint            script_wait;
	gboolean         running;
	gboolean         use_fixed;
	gfloat           interval;
	gfloat           accum;
	LrgBTStatus      last_status;
};

G_DEFINE_FINAL_TYPE (LrgAiComponent, lrg_ai_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_TREE,
	PROP_TREE_PATH,
	PROP_MACHINE,
	PROP_MACHINE_PATH,
	PROP_SCRIPTING,
	PROP_TICK_INTERVAL,
	PROP_USE_FIXED_UPDATE,
	PROP_RUNNING,
	PROP_BLACKBOARD,
	N_PROPS
};

enum
{
	SIGNAL_TREE_COMPLETED,
	SIGNAL_STATE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Starting and stepping
 * ========================================================================== */

static void
on_transitioned (LrgLogicMachine *machine,
                 const gchar     *from,
                 const gchar     *to,
                 const gchar     *event,
                 gpointer         user_data)
{
	g_signal_emit (LRG_AI_COMPONENT (user_data), signals[SIGNAL_STATE_CHANGED], 0, from, to);
}

/* drops the runner and stops the machine (nothing is reloaded here) */
static void
shut_down (LrgAiComponent *self)
{
	g_clear_object (&self->runner);
	if (self->machine != NULL)
	{
		g_clear_signal_handler (&self->transitioned_id, self->machine);
		lrg_logic_machine_stop (self->machine);
	}
	self->started = FALSE;
	self->script_wait = 0;
	self->accum = 0.0f;
	self->last_status = LRG_BT_STATUS_INVALID;
}

/* the owner's script component context, NULL when none (or not loaded) */
static LrgScripting *
owner_scripting (LrgAiComponent *self,
                 gboolean       *has_script)
{
	LrgComponent *component = LRG_COMPONENT (self);
	LrgComponent *script = NULL;
	LrgEntity3D  *entity = lrg_component_get_entity (component);
	LrgGameObject *owner = lrg_component_get_owner (component);

	if (entity != NULL)
		script = lrg_entity3d_get_component (entity, LRG_TYPE_SCRIPT_COMPONENT);
	else if (owner != NULL)
		script = lrg_game_object_get_component (owner, LRG_TYPE_SCRIPT_COMPONENT);
	*has_script = script != NULL;
	return script != NULL ? lrg_script_component_get_context (LRG_SCRIPT_COMPONENT (script)) : NULL;
}

/*
 * start:
 *
 * Loads what was given by path, then builds the runner and starts the
 * machine. Returns FALSE while it is waiting for the owner's script.
 */
static gboolean
start (LrgAiComponent *self)
{
	LrgScripting *scripting = self->scripting;
	gboolean      has_script = FALSE;

	if (scripting == NULL)
	{
		scripting = owner_scripting (self, &has_script);
		/* an owner script that has not loaded yet: give it a frame or two */
		if (scripting == NULL && has_script && self->script_wait++ < SCRIPT_WAIT_UPDATES)
			return FALSE;
	}

	/* the tree */
	if (self->tree == NULL && self->tree_path != NULL && !self->tree_failed)
	{
		g_autoptr(GError) error = NULL;

		self->tree = lrg_bt_asset_new_from_file (self->tree_path, &error);
		if (self->tree == NULL)
		{
			g_warning ("LrgAiComponent: cannot load '%s': %s", self->tree_path, error->message);
			self->tree_failed = TRUE;
		}
	}
	if (self->tree != NULL && !self->tree_failed)
	{
		g_autoptr(GError) error = NULL;

		self->runner = lrg_bt_runner_new (self->tree, scripting, self->blackboard, &error);
		if (self->runner == NULL)
		{
			g_warning ("LrgAiComponent: cannot build the tree%s%s: %s", self->tree_path != NULL ? " " : "",
			           self->tree_path != NULL ? self->tree_path : "", error->message);
			self->tree_failed = TRUE;
		}
	}

	/* the machine */
	if (self->machine == NULL && self->machine_path != NULL && !self->machine_failed)
	{
		g_autoptr(GError) error = NULL;

		self->machine = lrg_logic_machine_new_from_file (self->machine_path, &error);
		if (self->machine == NULL)
		{
			g_warning ("LrgAiComponent: cannot load '%s': %s", self->machine_path, error->message);
			self->machine_failed = TRUE;
		}
	}
	if (self->machine != NULL && !self->machine_failed)
	{
		g_autoptr(GError) error = NULL;

		self->transitioned_id = g_signal_connect (self->machine, "transitioned", G_CALLBACK (on_transitioned), self);
		if (!lrg_logic_machine_start (self->machine, self->blackboard, scripting, &error))
		{
			g_warning ("LrgAiComponent: cannot start the machine%s%s: %s", self->machine_path != NULL ? " " : "",
			           self->machine_path != NULL ? self->machine_path : "", error->message);
			g_clear_signal_handler (&self->transitioned_id, self->machine);
			self->machine_failed = TRUE;
		}
	}
	self->started = TRUE;
	return TRUE;
}

void
lrg_ai_component_step (LrgAiComponent *self,
                       gfloat          delta_time)
{
	gfloat dt;

	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	if (!self->running)
		return;
	if (!self->started && !start (self))
		return;

	/* the tick interval: step with the time accumulated since the last */
	self->accum += delta_time;
	if (self->interval > 0.0f && self->accum + 1e-6f < self->interval)
		return;
	dt = self->accum;
	self->accum = 0.0f;

	g_object_ref (self);
	if (self->machine != NULL && !self->machine_failed)
		lrg_logic_machine_update (self->machine, dt);
	if (self->runner != NULL)
	{
		LrgBTStatus status = lrg_bt_runner_tick (self->runner, dt);

		if (status != LRG_BT_STATUS_RUNNING && status != LRG_BT_STATUS_INVALID)
			g_signal_emit (self, signals[SIGNAL_TREE_COMPLETED], 0, status);
		self->last_status = status;
	}
	g_object_unref (self);
}

/* ==========================================================================
 * LrgComponent
 * ========================================================================== */

static void
set_owner_key (LrgAiComponent *self,
               GObject        *owner)
{
	if (owner != NULL)
		lrg_blackboard_set_pointer (self->blackboard, LRG_AI_COMPONENT_OWNER_KEY, owner, NULL);
	else
		lrg_blackboard_remove (self->blackboard, LRG_AI_COMPONENT_OWNER_KEY);
}

static void
lrg_ai_component_attached (LrgComponent  *component,
                           LrgGameObject *owner)
{
	set_owner_key (LRG_AI_COMPONENT (component), G_OBJECT (owner));
}

static void
lrg_ai_component_detached (LrgComponent *component)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (component);

	shut_down (self);
	set_owner_key (self, NULL);
}

static void
lrg_ai_component_attached_entity (LrgComponent *component,
                                  LrgEntity3D  *entity)
{
	set_owner_key (LRG_AI_COMPONENT (component), G_OBJECT (entity));
}

static void
lrg_ai_component_detached_entity (LrgComponent *component,
                                  LrgEntity3D  *entity)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (component);

	shut_down (self);
	set_owner_key (self, NULL);
}

static void
lrg_ai_component_update (LrgComponent *component,
                         gfloat        delta)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (component);

	if (!self->use_fixed)
		lrg_ai_component_step (self, delta);
}

static void
lrg_ai_component_fixed_update (LrgComponent *component,
                               gfloat        step)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (component);

	if (self->use_fixed)
		lrg_ai_component_step (self, step);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_ai_component_dispose (GObject *object)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (object);

	shut_down (self);
	g_clear_object (&self->machine);
	g_clear_object (&self->tree);
	g_clear_object (&self->scripting);
	g_clear_object (&self->blackboard);

	G_OBJECT_CLASS (lrg_ai_component_parent_class)->dispose (object);
}

static void
lrg_ai_component_finalize (GObject *object)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (object);

	g_free (self->tree_path);
	g_free (self->machine_path);

	G_OBJECT_CLASS (lrg_ai_component_parent_class)->finalize (object);
}

static void
lrg_ai_component_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_TREE:             g_value_set_object (value, self->tree); break;
	case PROP_TREE_PATH:        g_value_set_string (value, self->tree_path); break;
	case PROP_MACHINE:          g_value_set_object (value, self->machine); break;
	case PROP_MACHINE_PATH:     g_value_set_string (value, self->machine_path); break;
	case PROP_SCRIPTING:        g_value_set_object (value, self->scripting); break;
	case PROP_TICK_INTERVAL:    g_value_set_float (value, self->interval); break;
	case PROP_USE_FIXED_UPDATE: g_value_set_boolean (value, self->use_fixed); break;
	case PROP_RUNNING:          g_value_set_boolean (value, self->running); break;
	case PROP_BLACKBOARD:       g_value_set_object (value, self->blackboard); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ai_component_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgAiComponent *self = LRG_AI_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_TREE:
		lrg_ai_component_set_tree (self, g_value_get_object (value));
		break;
	case PROP_TREE_PATH:
		lrg_ai_component_set_tree_path (self, g_value_get_string (value));
		break;
	case PROP_MACHINE:
		lrg_ai_component_set_machine (self, g_value_get_object (value));
		break;
	case PROP_MACHINE_PATH:
		lrg_ai_component_set_machine_path (self, g_value_get_string (value));
		break;
	case PROP_SCRIPTING:
		lrg_ai_component_set_scripting (self, g_value_get_object (value));
		break;
	case PROP_TICK_INTERVAL:
		lrg_ai_component_set_tick_interval (self, g_value_get_float (value));
		break;
	case PROP_USE_FIXED_UPDATE:
		lrg_ai_component_set_use_fixed_update (self, g_value_get_boolean (value));
		break;
	case PROP_RUNNING:
		lrg_ai_component_set_running (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ai_component_class_init (LrgAiComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_ai_component_dispose;
	object_class->finalize = lrg_ai_component_finalize;
	object_class->get_property = lrg_ai_component_get_property;
	object_class->set_property = lrg_ai_component_set_property;

	component_class->attached = lrg_ai_component_attached;
	component_class->detached = lrg_ai_component_detached;
	component_class->attached_entity = lrg_ai_component_attached_entity;
	component_class->detached_entity = lrg_ai_component_detached_entity;
	component_class->update = lrg_ai_component_update;
	component_class->fixed_update = lrg_ai_component_fixed_update;

	/**
	 * LrgAiComponent:tree:
	 *
	 * The behaviour tree asset run on the owner.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_TREE] =
		g_param_spec_object ("tree", NULL, NULL, LRG_TYPE_BT_ASSET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:tree-path:
	 *
	 * The `.rbt` file loaded on the next update (level files set this).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_TREE_PATH] =
		g_param_spec_string ("tree-path", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:machine:
	 *
	 * The logic state machine run on the owner.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MACHINE] =
		g_param_spec_object ("machine", NULL, NULL, LRG_TYPE_LOGIC_MACHINE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:machine-path:
	 *
	 * The `.rfsm` file loaded on the next update (level files set this).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MACHINE_PATH] =
		g_param_spec_string ("machine-path", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:scripting:
	 *
	 * The scripting context script entries call into; %NULL uses the
	 * owner's #LrgScriptComponent context.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_SCRIPTING] =
		g_param_spec_object ("scripting", NULL, NULL, LRG_TYPE_SCRIPTING,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:tick-interval:
	 *
	 * Seconds between AI steps; 0 steps every update.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_TICK_INTERVAL] =
		g_param_spec_float ("tick-interval", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:use-fixed-update:
	 *
	 * Step in the world's fixed update instead of the frame update.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_USE_FIXED_UPDATE] =
		g_param_spec_boolean ("use-fixed-update", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:running:
	 *
	 * %FALSE pauses the AI.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_RUNNING] =
		g_param_spec_boolean ("running", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAiComponent:blackboard:
	 *
	 * The blackboard shared by the tree and the machine.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_BLACKBOARD] =
		g_param_spec_object ("blackboard", NULL, NULL, LRG_TYPE_BLACKBOARD,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgAiComponent::tree-completed:
	 * @self: the component
	 * @status: %LRG_BT_STATUS_SUCCESS or %LRG_BT_STATUS_FAILURE
	 *
	 * A tree tick ended with the root done (the tree starts over on the
	 * next tick).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_TREE_COMPLETED] =
		g_signal_new ("tree-completed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_BT_STATUS);

	/**
	 * LrgAiComponent::state-changed:
	 * @self: the component
	 * @from: the machine's innermost state path before
	 * @to: the machine's innermost state path after
	 *
	 * The machine took a transition.
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_ai_component_init (LrgAiComponent *self)
{
	self->blackboard = lrg_blackboard_new ();
	self->running = TRUE;
	self->last_status = LRG_BT_STATUS_INVALID;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAiComponent *
lrg_ai_component_new (void)
{
	return g_object_new (LRG_TYPE_AI_COMPONENT, NULL);
}

LrgAiComponent *
lrg_ai_component_new_with_tree (LrgBtAsset *tree)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (tree), NULL);
	return g_object_new (LRG_TYPE_AI_COMPONENT, "tree", tree, NULL);
}

LrgAiComponent *
lrg_ai_component_new_with_machine (LrgLogicMachine *machine)
{
	g_return_val_if_fail (LRG_IS_LOGIC_MACHINE (machine), NULL);
	return g_object_new (LRG_TYPE_AI_COMPONENT, "machine", machine, NULL);
}

LrgBtAsset *
lrg_ai_component_get_tree (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->tree;
}

void
lrg_ai_component_set_tree (LrgAiComponent *self,
                           LrgBtAsset     *tree)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));
	g_return_if_fail (tree == NULL || LRG_IS_BT_ASSET (tree));

	if (self->tree == tree)
		return;
	shut_down (self);
	g_set_object (&self->tree, tree);
	self->tree_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TREE]);
}

const gchar *
lrg_ai_component_get_tree_path (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->tree_path;
}

void
lrg_ai_component_set_tree_path (LrgAiComponent *self,
                                const gchar    *path)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	if (g_strcmp0 (self->tree_path, path) == 0)
		return;
	shut_down (self);
	g_free (self->tree_path);
	self->tree_path = g_strdup (path);
	g_clear_object (&self->tree);
	self->tree_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TREE_PATH]);
}

LrgLogicMachine *
lrg_ai_component_get_machine (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->machine;
}

void
lrg_ai_component_set_machine (LrgAiComponent  *self,
                              LrgLogicMachine *machine)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));
	g_return_if_fail (machine == NULL || LRG_IS_LOGIC_MACHINE (machine));

	if (self->machine == machine)
		return;
	shut_down (self);
	g_set_object (&self->machine, machine);
	self->machine_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MACHINE]);
}

const gchar *
lrg_ai_component_get_machine_path (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->machine_path;
}

void
lrg_ai_component_set_machine_path (LrgAiComponent *self,
                                   const gchar    *path)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	if (g_strcmp0 (self->machine_path, path) == 0)
		return;
	shut_down (self);
	g_free (self->machine_path);
	self->machine_path = g_strdup (path);
	g_clear_object (&self->machine);
	self->machine_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MACHINE_PATH]);
}

LrgScripting *
lrg_ai_component_get_scripting (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->scripting;
}

void
lrg_ai_component_set_scripting (LrgAiComponent *self,
                                LrgScripting   *scripting)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));
	g_return_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting));

	if (g_set_object (&self->scripting, scripting))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCRIPTING]);
}

gfloat
lrg_ai_component_get_tick_interval (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), 0.0f);
	return self->interval;
}

void
lrg_ai_component_set_tick_interval (LrgAiComponent *self,
                                    gfloat          seconds)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	seconds = MAX (seconds, 0.0f);
	if (self->interval == seconds)
		return;
	self->interval = seconds;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TICK_INTERVAL]);
}

gboolean
lrg_ai_component_get_use_fixed_update (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), FALSE);
	return self->use_fixed;
}

void
lrg_ai_component_set_use_fixed_update (LrgAiComponent *self,
                                       gboolean        use_fixed)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	use_fixed = !!use_fixed;
	if (self->use_fixed == use_fixed)
		return;
	self->use_fixed = use_fixed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USE_FIXED_UPDATE]);
}

gboolean
lrg_ai_component_get_running (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), FALSE);
	return self->running;
}

void
lrg_ai_component_set_running (LrgAiComponent *self,
                              gboolean        running)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	running = !!running;
	if (self->running == running)
		return;
	self->running = running;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

LrgBlackboard *
lrg_ai_component_get_blackboard (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->blackboard;
}

LrgBtRunner *
lrg_ai_component_get_runner (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->runner;
}

void
lrg_ai_component_send_event (LrgAiComponent *self,
                             const gchar    *event)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));
	g_return_if_fail (event != NULL);

	if (self->machine != NULL)
		lrg_logic_machine_send_event (self->machine, event);
}

const gchar *
lrg_ai_component_get_current_node (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->runner != NULL ? lrg_bt_runner_get_current_node (self->runner) : NULL;
}

const gchar *
lrg_ai_component_get_current_state (LrgAiComponent *self)
{
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);
	return self->machine != NULL ? lrg_logic_machine_get_active_state (self->machine) : NULL;
}

void
lrg_ai_component_restart (LrgAiComponent *self)
{
	g_return_if_fail (LRG_IS_AI_COMPONENT (self));

	shut_down (self);
	/* reload what came from files */
	if (self->tree_path != NULL)
		g_clear_object (&self->tree);
	if (self->machine_path != NULL)
		g_clear_object (&self->machine);
	self->tree_failed = FALSE;
	self->machine_failed = FALSE;
}

/* appends @text indented by two spaces under @key */
static void
append_section (GString     *out,
                const gchar *key,
                const gchar *text)
{
	g_auto(GStrv) lines = NULL;
	guint         i;

	if (text == NULL)
	{
		g_string_append_printf (out, "%s: ~\n", key);
		return;
	}
	g_string_append_printf (out, "%s:\n", key);
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
		if (*lines[i] != '\0')
			g_string_append_printf (out, "  %s\n", lines[i]);
}

gchar *
lrg_ai_component_dup_debug_state (LrgAiComponent *self)
{
	GString          *out;
	g_autofree gchar *tree = NULL;
	g_autofree gchar *machine = NULL;
	g_auto(GStrv)     keys = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_AI_COMPONENT (self), NULL);

	out = g_string_new (NULL);
	if (self->runner != NULL)
		tree = lrg_bt_runner_dup_debug_state (self->runner);
	if (self->machine != NULL)
		machine = lrg_logic_machine_dup_debug_state (self->machine);
	append_section (out, "tree", tree);
	append_section (out, "machine", machine);

	keys = lrg_blackboard_dup_keys (self->blackboard);
	if (keys[0] == NULL)
		g_string_append (out, "blackboard: {}\n");
	else
		g_string_append (out, "blackboard:\n");
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *text = lrg_blackboard_dup_value_text (self->blackboard, keys[i]);
		g_autofree gchar *esc = g_strescape (text != NULL ? text : "", NULL);

		g_string_append_printf (out, "  \"%s\": \"%s\"\n", keys[i], esc);
	}
	return g_string_free (out, FALSE);
}
