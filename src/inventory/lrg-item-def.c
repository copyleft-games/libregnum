/* lrg-item-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INVENTORY

#include "config.h"
#include "lrg-item-def.h"
#include "../lrg-log.h"

/* Custom property value type */
typedef struct
{
    enum { PROP_INT, PROP_FLOAT, PROP_STRING, PROP_BOOL } type;
    union {
        gint    int_val;
        gfloat  float_val;
        gchar  *string_val;
        gboolean bool_val;
    } value;
} CustomProperty;

static void
custom_property_free (gpointer data)
{
    CustomProperty *prop = (CustomProperty *)data;
    if (prop->type == PROP_STRING)
        g_free (prop->value.string_val);
    g_free (prop);
}

/* Private data structure */
typedef struct
{
    gchar       *id;
    gchar       *name;
    gchar       *description;
    LrgItemType  item_type;
    gboolean     stackable;
    guint        max_stack;
    gint         value;
    GHashTable  *custom_props;  /* gchar* -> CustomProperty* */
} LrgItemDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgItemDef, lrg_item_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ITEM_TYPE,
    PROP_STACKABLE,
    PROP_MAX_STACK,
    PROP_VALUE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Function Implementations
 * ========================================================================== */

static gboolean
lrg_item_def_real_on_use (LrgItemDef *self,
                          GObject    *owner G_GNUC_UNUSED,
                          guint       quantity G_GNUC_UNUSED)
{
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    /* Default behavior: consumables are consumed, others are not */
    return priv->item_type == LRG_ITEM_TYPE_CONSUMABLE;
}

static gboolean
lrg_item_def_real_can_stack_with (LrgItemDef *self,
                                   LrgItemDef *other)
{
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);
    LrgItemDefPrivate *other_priv = lrg_item_def_get_instance_private (other);

    /* Default: can stack if same ID and both are stackable */
    if (!priv->stackable || !other_priv->stackable)
        return FALSE;

    return g_strcmp0 (priv->id, other_priv->id) == 0;
}

static gchar *
lrg_item_def_real_get_tooltip (LrgItemDef *self)
{
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    if (priv->description != NULL)
        return g_strdup (priv->description);

    return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_item_def_finalize (GObject *object)
{
    LrgItemDef *self = LRG_ITEM_DEF (object);
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->custom_props, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_item_def_parent_class)->finalize (object);
}

static void
lrg_item_def_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgItemDef *self = LRG_ITEM_DEF (object);
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_ITEM_TYPE:
        g_value_set_enum (value, priv->item_type);
        break;
    case PROP_STACKABLE:
        g_value_set_boolean (value, priv->stackable);
        break;
    case PROP_MAX_STACK:
        g_value_set_uint (value, priv->max_stack);
        break;
    case PROP_VALUE:
        g_value_set_int (value, priv->value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_item_def_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgItemDef *self = LRG_ITEM_DEF (object);
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        lrg_item_def_set_name (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_item_def_set_description (self, g_value_get_string (value));
        break;
    case PROP_ITEM_TYPE:
        lrg_item_def_set_item_type (self, g_value_get_enum (value));
        break;
    case PROP_STACKABLE:
        lrg_item_def_set_stackable (self, g_value_get_boolean (value));
        break;
    case PROP_MAX_STACK:
        lrg_item_def_set_max_stack (self, g_value_get_uint (value));
        break;
    case PROP_VALUE:
        lrg_item_def_set_value (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_item_def_class_init (LrgItemDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_item_def_finalize;
    object_class->get_property = lrg_item_def_get_property;
    object_class->set_property = lrg_item_def_set_property;

    /* Virtual functions */
    klass->on_use = lrg_item_def_real_on_use;
    klass->can_stack_with = lrg_item_def_real_can_stack_with;
    klass->get_tooltip = lrg_item_def_real_get_tooltip;

    /**
     * LrgItemDef:id:
     *
     * Unique identifier for this item type.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier for this item type",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:name:
     *
     * Display name for this item.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:description:
     *
     * Description text for this item.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Item description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:item-type:
     *
     * The type of item.
     */
    properties[PROP_ITEM_TYPE] =
        g_param_spec_enum ("item-type",
                           "Item Type",
                           "The type of item",
                           LRG_TYPE_ITEM_TYPE,
                           LRG_ITEM_TYPE_GENERIC,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:stackable:
     *
     * Whether this item can be stacked.
     */
    properties[PROP_STACKABLE] =
        g_param_spec_boolean ("stackable",
                              "Stackable",
                              "Whether this item can be stacked",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:max-stack:
     *
     * Maximum stack size for this item.
     */
    properties[PROP_MAX_STACK] =
        g_param_spec_uint ("max-stack",
                           "Max Stack",
                           "Maximum stack size",
                           1, G_MAXUINT, 99,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgItemDef:value:
     *
     * Base value/price of the item.
     */
    properties[PROP_VALUE] =
        g_param_spec_int ("value",
                          "Value",
                          "Base value/price",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_item_def_init (LrgItemDef *self)
{
    LrgItemDefPrivate *priv = lrg_item_def_get_instance_private (self);

    priv->item_type = LRG_ITEM_TYPE_GENERIC;
    priv->stackable = TRUE;
    priv->max_stack = 99;
    priv->value = 0;
    priv->custom_props = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 custom_property_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgItemDef *
lrg_item_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_ITEM_DEF,
                         "id", id,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

const gchar *
lrg_item_def_get_id (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), NULL);

    priv = lrg_item_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_item_def_get_name (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), NULL);

    priv = lrg_item_def_get_instance_private (self);
    return priv->name;
}

void
lrg_item_def_set_name (LrgItemDef  *self,
                       const gchar *name)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

const gchar *
lrg_item_def_get_description (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), NULL);

    priv = lrg_item_def_get_instance_private (self);
    return priv->description;
}

void
lrg_item_def_set_description (LrgItemDef  *self,
                              const gchar *description)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) != 0)
    {
        g_free (priv->description);
        priv->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

LrgItemType
lrg_item_def_get_item_type (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), LRG_ITEM_TYPE_GENERIC);

    priv = lrg_item_def_get_instance_private (self);
    return priv->item_type;
}

void
lrg_item_def_set_item_type (LrgItemDef  *self,
                            LrgItemType  item_type)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    if (priv->item_type != item_type)
    {
        priv->item_type = item_type;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM_TYPE]);
    }
}

gboolean
lrg_item_def_get_stackable (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), FALSE);

    priv = lrg_item_def_get_instance_private (self);
    return priv->stackable;
}

void
lrg_item_def_set_stackable (LrgItemDef *self,
                            gboolean    stackable)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    if (priv->stackable != stackable)
    {
        priv->stackable = stackable;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STACKABLE]);
    }
}

guint
lrg_item_def_get_max_stack (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), 1);

    priv = lrg_item_def_get_instance_private (self);

    /* Non-stackable items always have max_stack of 1 */
    if (!priv->stackable)
        return 1;

    return priv->max_stack;
}

void
lrg_item_def_set_max_stack (LrgItemDef *self,
                            guint       max_stack)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    /* Minimum of 1 */
    max_stack = MAX (max_stack, 1);

    if (priv->max_stack != max_stack)
    {
        priv->max_stack = max_stack;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_STACK]);
    }
}

gint
lrg_item_def_get_value (LrgItemDef *self)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), 0);

    priv = lrg_item_def_get_instance_private (self);
    return priv->value;
}

void
lrg_item_def_set_value (LrgItemDef *self,
                        gint        value)
{
    LrgItemDefPrivate *priv;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));

    priv = lrg_item_def_get_instance_private (self);

    value = MAX (value, 0);

    if (priv->value != value)
    {
        priv->value = value;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
    }
}

/* ==========================================================================
 * Custom Properties
 * ========================================================================== */

gint
lrg_item_def_get_property_int (LrgItemDef  *self,
                               const gchar *key,
                               gint         default_value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    priv = lrg_item_def_get_instance_private (self);
    prop = g_hash_table_lookup (priv->custom_props, key);

    if (prop != NULL && prop->type == PROP_INT)
        return prop->value.int_val;

    return default_value;
}

void
lrg_item_def_set_property_int (LrgItemDef  *self,
                               const gchar *key,
                               gint         value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));
    g_return_if_fail (key != NULL);

    priv = lrg_item_def_get_instance_private (self);

    prop = g_new0 (CustomProperty, 1);
    prop->type = PROP_INT;
    prop->value.int_val = value;

    g_hash_table_insert (priv->custom_props, g_strdup (key), prop);
}

gfloat
lrg_item_def_get_property_float (LrgItemDef  *self,
                                 const gchar *key,
                                 gfloat       default_value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    priv = lrg_item_def_get_instance_private (self);
    prop = g_hash_table_lookup (priv->custom_props, key);

    if (prop != NULL && prop->type == PROP_FLOAT)
        return prop->value.float_val;

    return default_value;
}

void
lrg_item_def_set_property_float (LrgItemDef  *self,
                                 const gchar *key,
                                 gfloat       value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));
    g_return_if_fail (key != NULL);

    priv = lrg_item_def_get_instance_private (self);

    prop = g_new0 (CustomProperty, 1);
    prop->type = PROP_FLOAT;
    prop->value.float_val = value;

    g_hash_table_insert (priv->custom_props, g_strdup (key), prop);
}

const gchar *
lrg_item_def_get_property_string (LrgItemDef  *self,
                                  const gchar *key)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    priv = lrg_item_def_get_instance_private (self);
    prop = g_hash_table_lookup (priv->custom_props, key);

    if (prop != NULL && prop->type == PROP_STRING)
        return prop->value.string_val;

    return NULL;
}

void
lrg_item_def_set_property_string (LrgItemDef  *self,
                                  const gchar *key,
                                  const gchar *value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));
    g_return_if_fail (key != NULL);

    priv = lrg_item_def_get_instance_private (self);

    prop = g_new0 (CustomProperty, 1);
    prop->type = PROP_STRING;
    prop->value.string_val = g_strdup (value);

    g_hash_table_insert (priv->custom_props, g_strdup (key), prop);
}

gboolean
lrg_item_def_get_property_bool (LrgItemDef  *self,
                                const gchar *key,
                                gboolean     default_value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    priv = lrg_item_def_get_instance_private (self);
    prop = g_hash_table_lookup (priv->custom_props, key);

    if (prop != NULL && prop->type == PROP_BOOL)
        return prop->value.bool_val;

    return default_value;
}

void
lrg_item_def_set_property_bool (LrgItemDef  *self,
                                const gchar *key,
                                gboolean     value)
{
    LrgItemDefPrivate *priv;
    CustomProperty *prop;

    g_return_if_fail (LRG_IS_ITEM_DEF (self));
    g_return_if_fail (key != NULL);

    priv = lrg_item_def_get_instance_private (self);

    prop = g_new0 (CustomProperty, 1);
    prop->type = PROP_BOOL;
    prop->value.bool_val = value;

    g_hash_table_insert (priv->custom_props, g_strdup (key), prop);
}

gboolean
lrg_item_def_has_custom_property (LrgItemDef  *self,
                                  const gchar *key)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    priv = lrg_item_def_get_instance_private (self);
    return g_hash_table_contains (priv->custom_props, key);
}

gboolean
lrg_item_def_remove_custom_property (LrgItemDef  *self,
                                     const gchar *key)
{
    LrgItemDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    priv = lrg_item_def_get_instance_private (self);
    return g_hash_table_remove (priv->custom_props, key);
}

/* ==========================================================================
 * Virtual Function Wrappers
 * ========================================================================== */

gboolean
lrg_item_def_use (LrgItemDef *self,
                  GObject    *owner,
                  guint       quantity)
{
    LrgItemDefClass *klass;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), FALSE);

    klass = LRG_ITEM_DEF_GET_CLASS (self);
    if (klass->on_use != NULL)
        return klass->on_use (self, owner, quantity);

    return FALSE;
}

gboolean
lrg_item_def_can_stack_with (LrgItemDef *self,
                             LrgItemDef *other)
{
    LrgItemDefClass *klass;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_ITEM_DEF (other), FALSE);

    klass = LRG_ITEM_DEF_GET_CLASS (self);
    if (klass->can_stack_with != NULL)
        return klass->can_stack_with (self, other);

    return FALSE;
}

gchar *
lrg_item_def_get_tooltip (LrgItemDef *self)
{
    LrgItemDefClass *klass;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (self), NULL);

    klass = LRG_ITEM_DEF_GET_CLASS (self);
    if (klass->get_tooltip != NULL)
        return klass->get_tooltip (self);

    return NULL;
}
