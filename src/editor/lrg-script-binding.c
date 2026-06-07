/* lrg-script-binding.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Serializable binding of a script to an editor node.
 */

#include "lrg-script-binding.h"

/**
 * LrgScriptBinding:
 *
 * Data-only record of a script attached to a node: language, a script path
 * or asset guid, and an enabled flag.
 */
struct _LrgScriptBinding
{
	GObject parent_instance;

	LrgScriptLanguage language;
	gchar            *script;
	gboolean          enabled;
};

G_DEFINE_FINAL_TYPE (LrgScriptBinding, lrg_script_binding, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_LANGUAGE,
	PROP_SCRIPT,
	PROP_ENABLED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_script_binding_finalize (GObject *object)
{
	LrgScriptBinding *self = LRG_SCRIPT_BINDING (object);

	g_clear_pointer (&self->script, g_free);

	G_OBJECT_CLASS (lrg_script_binding_parent_class)->finalize (object);
}

static void
lrg_script_binding_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgScriptBinding *self = LRG_SCRIPT_BINDING (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE:
		g_value_set_enum (value, self->language);
		break;
	case PROP_SCRIPT:
		g_value_set_string (value, self->script);
		break;
	case PROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_script_binding_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgScriptBinding *self = LRG_SCRIPT_BINDING (object);

	switch (prop_id)
	{
	case PROP_LANGUAGE:
		self->language = g_value_get_enum (value);
		break;
	case PROP_SCRIPT:
		g_clear_pointer (&self->script, g_free);
		self->script = g_value_dup_string (value);
		break;
	case PROP_ENABLED:
		self->enabled = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_script_binding_class_init (LrgScriptBindingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_script_binding_finalize;
	object_class->get_property = lrg_script_binding_get_property;
	object_class->set_property = lrg_script_binding_set_property;

	/**
	 * LrgScriptBinding:language:
	 *
	 * The scripting language the script is authored in.
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
	 * LrgScriptBinding:script:
	 *
	 * The script path or asset guid.
	 */
	properties[PROP_SCRIPT] =
		g_param_spec_string ("script",
		                     "Script",
		                     "Script path or asset guid",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgScriptBinding:enabled:
	 *
	 * Whether the binding is active.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled",
		                      "Enabled",
		                      "Whether the binding is active",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_script_binding_init (LrgScriptBinding *self)
{
	self->language = LRG_SCRIPT_LANGUAGE_NONE;
	self->script   = NULL;
	self->enabled  = TRUE;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_script_binding_new:
 * @language: the scripting language
 * @script: (nullable): the script path or asset guid
 *
 * Creates a new #LrgScriptBinding, enabled by default.
 *
 * Returns: (transfer full): a new #LrgScriptBinding
 */
LrgScriptBinding *
lrg_script_binding_new (LrgScriptLanguage  language,
                        const gchar       *script)
{
	return g_object_new (LRG_TYPE_SCRIPT_BINDING,
	                     "language", language,
	                     "script", script,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_script_binding_get_language:
 * @self: an #LrgScriptBinding
 *
 * Gets the scripting language of the binding.
 *
 * Returns: the #LrgScriptLanguage
 */
LrgScriptLanguage
lrg_script_binding_get_language (LrgScriptBinding *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BINDING (self), LRG_SCRIPT_LANGUAGE_NONE);

	return self->language;
}

/**
 * lrg_script_binding_set_language:
 * @self: an #LrgScriptBinding
 * @language: the scripting language
 *
 * Sets the scripting language of the binding.
 */
void
lrg_script_binding_set_language (LrgScriptBinding  *self,
                                 LrgScriptLanguage  language)
{
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (self));

	self->language = language;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
}

/**
 * lrg_script_binding_get_script:
 * @self: an #LrgScriptBinding
 *
 * Gets the script path or asset guid.
 *
 * Returns: (transfer none) (nullable): the script reference
 */
const gchar *
lrg_script_binding_get_script (LrgScriptBinding *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BINDING (self), NULL);

	return self->script;
}

/**
 * lrg_script_binding_set_script:
 * @self: an #LrgScriptBinding
 * @script: (nullable): the script path or asset guid
 *
 * Sets the script path or asset guid.
 */
void
lrg_script_binding_set_script (LrgScriptBinding *self,
                               const gchar      *script)
{
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (self));

	g_clear_pointer (&self->script, g_free);
	self->script = g_strdup (script);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCRIPT]);
}

/**
 * lrg_script_binding_get_enabled:
 * @self: an #LrgScriptBinding
 *
 * Gets whether the binding is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_script_binding_get_enabled (LrgScriptBinding *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BINDING (self), FALSE);

	return self->enabled;
}

/**
 * lrg_script_binding_set_enabled:
 * @self: an #LrgScriptBinding
 * @enabled: whether the binding is enabled
 *
 * Sets whether the binding is enabled.
 */
void
lrg_script_binding_set_enabled (LrgScriptBinding *self,
                                gboolean          enabled)
{
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (self));

	self->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}
