/* lrg-component-desc.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Serializable description of a component attached to an editor node.
 */

#include "lrg-component-desc.h"

/**
 * LrgComponentDesc:
 *
 * A data-only record describing a component to instantiate: a registry
 * type name plus a bag of typed property values keyed by name.
 */
struct _LrgComponentDesc
{
	GObject parent_instance;

	gchar      *type_name;
	GHashTable *props;       /* gchar* -> GValue* */
};

G_DEFINE_FINAL_TYPE (LrgComponentDesc, lrg_component_desc, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TYPE_NAME,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

static void
value_free (gpointer data)
{
	GValue *value = data;

	g_value_unset (value);
	g_free (value);
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_component_desc_finalize (GObject *object)
{
	LrgComponentDesc *self = LRG_COMPONENT_DESC (object);

	g_clear_pointer (&self->type_name, g_free);
	g_clear_pointer (&self->props, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_component_desc_parent_class)->finalize (object);
}

static void
lrg_component_desc_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgComponentDesc *self = LRG_COMPONENT_DESC (object);

	switch (prop_id)
	{
	case PROP_TYPE_NAME:
		g_value_set_string (value, self->type_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_component_desc_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgComponentDesc *self = LRG_COMPONENT_DESC (object);

	switch (prop_id)
	{
	case PROP_TYPE_NAME:
		g_clear_pointer (&self->type_name, g_free);
		self->type_name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_component_desc_class_init (LrgComponentDescClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_component_desc_finalize;
	object_class->get_property = lrg_component_desc_get_property;
	object_class->set_property = lrg_component_desc_set_property;

	/**
	 * LrgComponentDesc:type-name:
	 *
	 * The registry type name of the component to instantiate.
	 */
	properties[PROP_TYPE_NAME] =
		g_param_spec_string ("type-name",
		                     "Type Name",
		                     "Registry type name of the component",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_component_desc_init (LrgComponentDesc *self)
{
	self->type_name = NULL;
	self->props     = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         g_free, value_free);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_component_desc_new:
 * @type_name: the registry type name of the component (e.g. "health")
 *
 * Creates a new, empty #LrgComponentDesc for the given registry type name.
 *
 * Returns: (transfer full): a new #LrgComponentDesc
 */
LrgComponentDesc *
lrg_component_desc_new (const gchar *type_name)
{
	return g_object_new (LRG_TYPE_COMPONENT_DESC,
	                     "type-name", type_name,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_component_desc_get_type_name:
 * @self: an #LrgComponentDesc
 *
 * Gets the registry type name of the component.
 *
 * Returns: (transfer none) (nullable): the type name
 */
const gchar *
lrg_component_desc_get_type_name (LrgComponentDesc *self)
{
	g_return_val_if_fail (LRG_IS_COMPONENT_DESC (self), NULL);

	return self->type_name;
}

/**
 * lrg_component_desc_set_type_name:
 * @self: an #LrgComponentDesc
 * @type_name: (nullable): the registry type name
 *
 * Sets the registry type name of the component.
 */
void
lrg_component_desc_set_type_name (LrgComponentDesc *self,
                                  const gchar      *type_name)
{
	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));

	g_clear_pointer (&self->type_name, g_free);
	self->type_name = g_strdup (type_name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TYPE_NAME]);
}

/* ==========================================================================
 * Property Values
 * ========================================================================== */

/**
 * lrg_component_desc_set_value:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the value to store (copied)
 *
 * Stores a typed property value, replacing any existing value for @name.
 */
void
lrg_component_desc_set_value (LrgComponentDesc *self,
                              const gchar      *name,
                              const GValue     *value)
{
	GValue *copy;

	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL && G_IS_VALUE (value));

	copy = g_new0 (GValue, 1);
	g_value_init (copy, G_VALUE_TYPE (value));
	g_value_copy (value, copy);

	g_hash_table_insert (self->props, g_strdup (name), copy);
}

/**
 * lrg_component_desc_get_value:
 * @self: an #LrgComponentDesc
 * @name: the property name
 *
 * Gets the stored value for @name, if any.
 *
 * Returns: (transfer none) (nullable): the stored #GValue, owned by @self
 */
const GValue *
lrg_component_desc_get_value (LrgComponentDesc *self,
                              const gchar      *name)
{
	g_return_val_if_fail (LRG_IS_COMPONENT_DESC (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return g_hash_table_lookup (self->props, name);
}

/**
 * lrg_component_desc_set_string:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: (nullable): the string value
 *
 * Convenience setter for a string property value.
 */
void
lrg_component_desc_set_string (LrgComponentDesc *self,
                               const gchar      *name,
                               const gchar      *value)
{
	GValue v = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));
	g_return_if_fail (name != NULL);

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, value);
	lrg_component_desc_set_value (self, name, &v);
	g_value_unset (&v);
}

/**
 * lrg_component_desc_set_int:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the integer value
 *
 * Convenience setter for an integer property value.
 */
void
lrg_component_desc_set_int (LrgComponentDesc *self,
                            const gchar      *name,
                            gint              value)
{
	GValue v = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));
	g_return_if_fail (name != NULL);

	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, value);
	lrg_component_desc_set_value (self, name, &v);
	g_value_unset (&v);
}

/**
 * lrg_component_desc_set_double:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the double value
 *
 * Convenience setter for a double property value.
 */
void
lrg_component_desc_set_double (LrgComponentDesc *self,
                               const gchar      *name,
                               gdouble           value)
{
	GValue v = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));
	g_return_if_fail (name != NULL);

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, value);
	lrg_component_desc_set_value (self, name, &v);
	g_value_unset (&v);
}

/**
 * lrg_component_desc_set_boolean:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the boolean value
 *
 * Convenience setter for a boolean property value.
 */
void
lrg_component_desc_set_boolean (LrgComponentDesc *self,
                                const gchar      *name,
                                gboolean          value)
{
	GValue v = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_COMPONENT_DESC (self));
	g_return_if_fail (name != NULL);

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, value);
	lrg_component_desc_set_value (self, name, &v);
	g_value_unset (&v);
}

/**
 * lrg_component_desc_has:
 * @self: an #LrgComponentDesc
 * @name: the property name
 *
 * Checks whether a property value is set for @name.
 *
 * Returns: %TRUE if a value exists
 */
gboolean
lrg_component_desc_has (LrgComponentDesc *self,
                        const gchar      *name)
{
	g_return_val_if_fail (LRG_IS_COMPONENT_DESC (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return g_hash_table_contains (self->props, name);
}

/**
 * lrg_component_desc_get_keys:
 * @self: an #LrgComponentDesc
 *
 * Gets the names of all set property values.
 *
 * Returns: (transfer container) (element-type utf8): list of property names
 */
GList *
lrg_component_desc_get_keys (LrgComponentDesc *self)
{
	g_return_val_if_fail (LRG_IS_COMPONENT_DESC (self), NULL);

	return g_hash_table_get_keys (self->props);
}
