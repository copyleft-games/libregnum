/* lrg-script-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A component that binds a script to its owning game object.
 */

#include "lrg-script-component.h"
#include "../../scripting/lrg-scripting.h"
#include "../../scripting/lrg-scripting-manager.h"
#include "../../scripting/lrg-script-module.h"

struct _LrgScriptComponent
{
	LrgComponent parent_instance;

	LrgScriptLanguage  language;
	gchar             *script;
	LrgScripting      *context;   /* live context, or NULL if inert */
	gboolean           started;
	gboolean           run_in_editor;
};

G_DEFINE_FINAL_TYPE (LrgScriptComponent, lrg_script_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_LANGUAGE,
	PROP_SCRIPT,
	PROP_RUN_IN_EDITOR,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Lifecycle helpers
 * ========================================================================== */

static void
script_teardown (LrgScriptComponent *self)
{
	if (self->context == NULL)
		return;

	if (self->started)
		lrg_scripting_call_function (self->context, LRG_SCRIPT_HOOK_DETACH,
		                             NULL, 0, NULL, NULL);

	lrg_scripting_reset (self->context);
	g_clear_object (&self->context);
	self->started = FALSE;
}

static void
script_setup (LrgScriptComponent *self)
{
	LrgScriptingManager *manager = lrg_scripting_manager_get_default ();

	if (self->language == LRG_SCRIPT_LANGUAGE_NONE)
		return;
	if (!lrg_scripting_manager_is_available (manager, self->language))
		return;

	self->context = lrg_scripting_manager_create_context (manager, self->language);
	if (self->context == NULL)
		return;

	if (self->script != NULL)
		lrg_scripting_load_file (self->context, self->script, NULL);

	lrg_scripting_call_function (self->context, LRG_SCRIPT_HOOK_START,
	                             NULL, 0, NULL, NULL);
	self->started = TRUE;
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_script_component_attached (LrgComponent  *component,
                               LrgGameObject *owner)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (component);
	LrgComponentClass  *parent = LRG_COMPONENT_CLASS (lrg_script_component_parent_class);

	if (parent->attached != NULL)
		parent->attached (component, owner);

	script_setup (self);
}

static void
lrg_script_component_detached (LrgComponent *component)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (component);
	LrgComponentClass  *parent = LRG_COMPONENT_CLASS (lrg_script_component_parent_class);

	script_teardown (self);

	if (parent->detached != NULL)
		parent->detached (component);
}

static void
lrg_script_component_update (LrgComponent *component,
                             gfloat        delta)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (component);
	GValue              arg = G_VALUE_INIT;

	if (self->context == NULL || !self->started)
		return;

	g_value_init (&arg, G_TYPE_DOUBLE);
	g_value_set_double (&arg, (gdouble) delta);
	lrg_scripting_call_function (self->context, LRG_SCRIPT_HOOK_UPDATE,
	                             NULL, 1, &arg, NULL);
	g_value_unset (&arg);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_script_component_finalize (GObject *object)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (object);

	script_teardown (self);
	g_clear_pointer (&self->script, g_free);

	G_OBJECT_CLASS (lrg_script_component_parent_class)->finalize (object);
}

static void
lrg_script_component_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE:
		g_value_set_enum (value, self->language);
		break;
	case PROP_SCRIPT:
		g_value_set_string (value, self->script);
		break;
	case PROP_RUN_IN_EDITOR:
		g_value_set_boolean (value, self->run_in_editor);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_script_component_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgScriptComponent *self = LRG_SCRIPT_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE:
		self->language = g_value_get_enum (value);
		break;
	case PROP_SCRIPT:
		g_clear_pointer (&self->script, g_free);
		self->script = g_value_dup_string (value);
		break;
	case PROP_RUN_IN_EDITOR:
		lrg_script_component_set_run_in_editor (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_script_component_class_init (LrgScriptComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->finalize     = lrg_script_component_finalize;
	object_class->get_property = lrg_script_component_get_property;
	object_class->set_property = lrg_script_component_set_property;

	component_class->attached = lrg_script_component_attached;
	component_class->detached = lrg_script_component_detached;
	component_class->update   = lrg_script_component_update;

	/**
	 * LrgScriptComponent:language:
	 *
	 * The scripting language the bound script is authored in.
	 */
	properties[PROP_LANGUAGE] =
		g_param_spec_enum ("language",
		                   "Language",
		                   "Scripting language",
		                   LRG_TYPE_SCRIPT_LANGUAGE,
		                   LRG_SCRIPT_LANGUAGE_NONE,
		                   G_PARAM_READWRITE |
		                   G_PARAM_CONSTRUCT |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgScriptComponent:script:
	 *
	 * The path of the bound script.
	 */
	properties[PROP_SCRIPT] =
		g_param_spec_string ("script",
		                     "Script",
		                     "Script path",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgScriptComponent:run-in-editor:
	 *
	 * Copied from #LrgScriptBinding:run-in-editor when a level is
	 * instantiated. The component itself ticks the same way either way;
	 * the flag tells a host that steps a world while authoring (an
	 * editor preview world) which script components to update while the
	 * game is not playing: see lrg_script_component_should_update().
	 *
	 * Since: 0.9
	 */
	properties[PROP_RUN_IN_EDITOR] =
		g_param_spec_boolean ("run-in-editor",
		                      "Run in Editor",
		                      "Whether the script also runs while authoring",
		                      FALSE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_script_component_init (LrgScriptComponent *self)
{
	self->language = LRG_SCRIPT_LANGUAGE_NONE;
	self->script   = NULL;
	self->context  = NULL;
	self->started  = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgScriptComponent *
lrg_script_component_new (LrgScriptLanguage  language,
                          const gchar       *script)
{
	return g_object_new (LRG_TYPE_SCRIPT_COMPONENT,
	                     "language", language,
	                     "script", script,
	                     NULL);
}

LrgScriptLanguage
lrg_script_component_get_language (LrgScriptComponent *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_COMPONENT (self), LRG_SCRIPT_LANGUAGE_NONE);

	return self->language;
}

void
lrg_script_component_set_language (LrgScriptComponent *self,
                                   LrgScriptLanguage   language)
{
	g_return_if_fail (LRG_IS_SCRIPT_COMPONENT (self));

	self->language = language;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
}

const gchar *
lrg_script_component_get_script (LrgScriptComponent *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_COMPONENT (self), NULL);

	return self->script;
}

void
lrg_script_component_set_script (LrgScriptComponent *self,
                                 const gchar        *script)
{
	g_return_if_fail (LRG_IS_SCRIPT_COMPONENT (self));

	g_clear_pointer (&self->script, g_free);
	self->script = g_strdup (script);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCRIPT]);
}

LrgScripting *
lrg_script_component_get_context (LrgScriptComponent *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_COMPONENT (self), NULL);

	return self->context;
}

gboolean
lrg_script_component_get_run_in_editor (LrgScriptComponent *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_COMPONENT (self), FALSE);

	return self->run_in_editor;
}

void
lrg_script_component_set_run_in_editor (LrgScriptComponent *self,
                                        gboolean            run_in_editor)
{
	g_return_if_fail (LRG_IS_SCRIPT_COMPONENT (self));

	run_in_editor = !!run_in_editor;
	if (self->run_in_editor == run_in_editor)
		return;
	self->run_in_editor = run_in_editor;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUN_IN_EDITOR]);
}

gboolean
lrg_script_component_should_update (LrgScriptComponent *self,
                                    gboolean            authoring)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_COMPONENT (self), FALSE);

	/* While playing every script runs; while authoring only the
	 * run-in-editor ones */
	return !authoring || self->run_in_editor;
}
