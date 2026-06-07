/* lrg-node-visual.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tagged visual/content payload for an editor node.
 */

#include "lrg-node-visual.h"

/**
 * LrgNodeVisual:
 *
 * The tagged content payload of an #LrgNode. See the header for the meaning
 * of each field per #LrgNodeVisualKind.
 */
struct _LrgNodeVisual
{
	GObject parent_instance;

	LrgNodeVisualKind  kind;
	LrgPrimitiveType   primitive;
	gchar             *asset;
	LrgMaterial3D     *material;
	GHashTable        *params;   /* gchar* -> GValue* */
};

G_DEFINE_FINAL_TYPE (LrgNodeVisual, lrg_node_visual, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_KIND,
	PROP_PRIMITIVE,
	PROP_ASSET,
	PROP_MATERIAL,
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
lrg_node_visual_finalize (GObject *object)
{
	LrgNodeVisual *self = LRG_NODE_VISUAL (object);

	g_clear_pointer (&self->asset, g_free);
	g_clear_object (&self->material);
	g_clear_pointer (&self->params, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_node_visual_parent_class)->finalize (object);
}

static void
lrg_node_visual_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgNodeVisual *self = LRG_NODE_VISUAL (object);

	switch (prop_id)
	{
	case PROP_KIND:
		g_value_set_enum (value, self->kind);
		break;
	case PROP_PRIMITIVE:
		g_value_set_enum (value, self->primitive);
		break;
	case PROP_ASSET:
		g_value_set_string (value, self->asset);
		break;
	case PROP_MATERIAL:
		g_value_set_object (value, self->material);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_node_visual_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgNodeVisual *self = LRG_NODE_VISUAL (object);

	switch (prop_id)
	{
	case PROP_KIND:
		self->kind = g_value_get_enum (value);
		break;
	case PROP_PRIMITIVE:
		self->primitive = g_value_get_enum (value);
		break;
	case PROP_ASSET:
		g_clear_pointer (&self->asset, g_free);
		self->asset = g_value_dup_string (value);
		break;
	case PROP_MATERIAL:
		g_set_object (&self->material, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_node_visual_class_init (LrgNodeVisualClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_node_visual_finalize;
	object_class->get_property = lrg_node_visual_get_property;
	object_class->set_property = lrg_node_visual_set_property;

	/**
	 * LrgNodeVisual:kind:
	 *
	 * The kind discriminator for this visual payload.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind",
		                   "Kind",
		                   "Visual kind discriminator",
		                   LRG_TYPE_NODE_VISUAL_KIND,
		                   LRG_NODE_VISUAL_NONE,
		                   G_PARAM_READWRITE |
		                   G_PARAM_CONSTRUCT |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNodeVisual:primitive:
	 *
	 * The primitive type, for %LRG_NODE_VISUAL_PRIMITIVE.
	 */
	properties[PROP_PRIMITIVE] =
		g_param_spec_enum ("primitive",
		                   "Primitive",
		                   "Primitive type",
		                   LRG_TYPE_PRIMITIVE_TYPE,
		                   LRG_PRIMITIVE_CUBE,
		                   G_PARAM_READWRITE |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNodeVisual:asset:
	 *
	 * The asset reference (path or guid) for asset-backed kinds.
	 */
	properties[PROP_ASSET] =
		g_param_spec_string ("asset",
		                     "Asset",
		                     "Asset reference (path or guid)",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNodeVisual:material:
	 *
	 * The material used by primitive and mesh kinds.
	 */
	properties[PROP_MATERIAL] =
		g_param_spec_object ("material",
		                     "Material",
		                     "Surface material",
		                     LRG_TYPE_MATERIAL3D,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_node_visual_init (LrgNodeVisual *self)
{
	self->kind      = LRG_NODE_VISUAL_NONE;
	self->primitive = LRG_PRIMITIVE_CUBE;
	self->asset     = NULL;
	self->material  = NULL;
	self->params    = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         g_free, value_free);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_node_visual_new:
 * @kind: the visual kind
 *
 * Creates a new #LrgNodeVisual of the given kind.
 *
 * Returns: (transfer full): a new #LrgNodeVisual
 */
LrgNodeVisual *
lrg_node_visual_new (LrgNodeVisualKind kind)
{
	return g_object_new (LRG_TYPE_NODE_VISUAL,
	                     "kind", kind,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_node_visual_get_kind:
 * @self: an #LrgNodeVisual
 *
 * Gets the visual kind.
 *
 * Returns: the #LrgNodeVisualKind
 */
LrgNodeVisualKind
lrg_node_visual_get_kind (LrgNodeVisual *self)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), LRG_NODE_VISUAL_NONE);

	return self->kind;
}

/**
 * lrg_node_visual_set_kind:
 * @self: an #LrgNodeVisual
 * @kind: the visual kind
 *
 * Sets the visual kind.
 */
void
lrg_node_visual_set_kind (LrgNodeVisual     *self,
                          LrgNodeVisualKind  kind)
{
	g_return_if_fail (LRG_IS_NODE_VISUAL (self));

	self->kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

/**
 * lrg_node_visual_get_primitive:
 * @self: an #LrgNodeVisual
 *
 * Gets the primitive type (meaningful for %LRG_NODE_VISUAL_PRIMITIVE).
 *
 * Returns: the #LrgPrimitiveType
 */
LrgPrimitiveType
lrg_node_visual_get_primitive (LrgNodeVisual *self)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), LRG_PRIMITIVE_CUBE);

	return self->primitive;
}

/**
 * lrg_node_visual_set_primitive:
 * @self: an #LrgNodeVisual
 * @primitive: the primitive type
 *
 * Sets the primitive type.
 */
void
lrg_node_visual_set_primitive (LrgNodeVisual    *self,
                               LrgPrimitiveType  primitive)
{
	g_return_if_fail (LRG_IS_NODE_VISUAL (self));

	self->primitive = primitive;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMITIVE]);
}

/**
 * lrg_node_visual_get_asset:
 * @self: an #LrgNodeVisual
 *
 * Gets the asset reference (path or guid) for asset-backed kinds.
 *
 * Returns: (transfer none) (nullable): the asset reference
 */
const gchar *
lrg_node_visual_get_asset (LrgNodeVisual *self)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), NULL);

	return self->asset;
}

/**
 * lrg_node_visual_set_asset:
 * @self: an #LrgNodeVisual
 * @asset: (nullable): the asset reference (path or guid)
 *
 * Sets the asset reference.
 */
void
lrg_node_visual_set_asset (LrgNodeVisual *self,
                           const gchar   *asset)
{
	g_return_if_fail (LRG_IS_NODE_VISUAL (self));

	g_clear_pointer (&self->asset, g_free);
	self->asset = g_strdup (asset);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET]);
}

/**
 * lrg_node_visual_get_material:
 * @self: an #LrgNodeVisual
 *
 * Gets the material, if any (used by primitive and mesh kinds).
 *
 * Returns: (transfer none) (nullable): the #LrgMaterial3D
 */
LrgMaterial3D *
lrg_node_visual_get_material (LrgNodeVisual *self)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), NULL);

	return self->material;
}

/**
 * lrg_node_visual_set_material:
 * @self: an #LrgNodeVisual
 * @material: (transfer none) (nullable): the material
 *
 * Sets the material. A reference is taken on @material.
 */
void
lrg_node_visual_set_material (LrgNodeVisual *self,
                              LrgMaterial3D *material)
{
	g_return_if_fail (LRG_IS_NODE_VISUAL (self));
	g_return_if_fail (material == NULL || LRG_IS_MATERIAL3D (material));

	if (g_set_object (&self->material, material))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATERIAL]);
}

/* ==========================================================================
 * Kind-specific Parameters
 * ========================================================================== */

/**
 * lrg_node_visual_set_param:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @value: the value to store (copied)
 *
 * Stores a typed kind-specific parameter, replacing any existing value.
 */
void
lrg_node_visual_set_param (LrgNodeVisual *self,
                           const gchar   *name,
                           const GValue  *value)
{
	GValue *copy;

	g_return_if_fail (LRG_IS_NODE_VISUAL (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL && G_IS_VALUE (value));

	copy = g_new0 (GValue, 1);
	g_value_init (copy, G_VALUE_TYPE (value));
	g_value_copy (value, copy);

	g_hash_table_insert (self->params, g_strdup (name), copy);
}

/**
 * lrg_node_visual_get_param:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 *
 * Gets a stored kind-specific parameter.
 *
 * Returns: (transfer none) (nullable): the stored #GValue, owned by @self
 */
const GValue *
lrg_node_visual_get_param (LrgNodeVisual *self,
                           const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return g_hash_table_lookup (self->params, name);
}

/**
 * lrg_node_visual_set_param_double:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @value: the value
 *
 * Convenience setter for a double parameter.
 */
void
lrg_node_visual_set_param_double (LrgNodeVisual *self,
                                  const gchar   *name,
                                  gdouble        value)
{
	GValue v = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_NODE_VISUAL (self));
	g_return_if_fail (name != NULL);

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, value);
	lrg_node_visual_set_param (self, name, &v);
	g_value_unset (&v);
}

/**
 * lrg_node_visual_get_param_double:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @default_value: value to return if not set or wrong type
 *
 * Convenience getter for a double parameter.
 *
 * Returns: the parameter value, or @default_value
 */
gdouble
lrg_node_visual_get_param_double (LrgNodeVisual *self,
                                  const gchar   *name,
                                  gdouble        default_value)
{
	const GValue *v;

	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), default_value);
	g_return_val_if_fail (name != NULL, default_value);

	v = g_hash_table_lookup (self->params, name);
	if (v == NULL)
		return default_value;

	/* Coerce any numeric type: serialized whole doubles round-trip as ints. */
	if (G_VALUE_HOLDS_DOUBLE (v))
		return g_value_get_double (v);
	if (G_VALUE_HOLDS_FLOAT (v))
		return (gdouble) g_value_get_float (v);
	if (G_VALUE_HOLDS_INT64 (v))
		return (gdouble) g_value_get_int64 (v);
	if (G_VALUE_HOLDS_INT (v))
		return (gdouble) g_value_get_int (v);
	if (G_VALUE_HOLDS_UINT (v))
		return (gdouble) g_value_get_uint (v);

	return default_value;
}

/**
 * lrg_node_visual_get_param_names:
 * @self: an #LrgNodeVisual
 *
 * Gets the names of all set parameters.
 *
 * Returns: (transfer container) (element-type utf8): list of parameter names
 */
GList *
lrg_node_visual_get_param_names (LrgNodeVisual *self)
{
	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), NULL);

	return g_hash_table_get_keys (self->params);
}
