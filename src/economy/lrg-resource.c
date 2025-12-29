/* lrg-resource.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-resource.h"
#include "../lrg-log.h"

#include <math.h>

/* Private data structure */
typedef struct
{
    gchar               *id;
    gchar               *name;
    gchar               *description;
    gchar               *icon;
    LrgResourceCategory  category;
    gdouble              min_value;
    gdouble              max_value;
    guint                decimal_places;
    gboolean             hidden;
} LrgResourcePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgResource, lrg_resource, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_CATEGORY,
    PROP_MIN_VALUE,
    PROP_MAX_VALUE,
    PROP_DECIMAL_PLACES,
    PROP_HIDDEN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Function Implementations
 * ========================================================================== */

static gchar *
lrg_resource_real_format_value (LrgResource *self,
                                gdouble      amount)
{
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

    /*
     * Default formatting based on category and decimal places.
     * Currency gets special handling with common abbreviations.
     */
    if (priv->category == LRG_RESOURCE_CATEGORY_CURRENCY)
    {
        /* Currency formatting with abbreviations for large numbers */
        if (fabs (amount) >= 1e12)
            return g_strdup_printf ("%.*fT", priv->decimal_places, amount / 1e12);
        else if (fabs (amount) >= 1e9)
            return g_strdup_printf ("%.*fB", priv->decimal_places, amount / 1e9);
        else if (fabs (amount) >= 1e6)
            return g_strdup_printf ("%.*fM", priv->decimal_places, amount / 1e6);
        else if (fabs (amount) >= 1e3)
            return g_strdup_printf ("%.*fK", priv->decimal_places, amount / 1e3);
    }

    /* Standard numeric formatting - use switch to avoid format-nonliteral warning */
    switch (priv->decimal_places)
    {
        case 0:
            return g_strdup_printf ("%.0f", amount);
        case 1:
            return g_strdup_printf ("%.1f", amount);
        case 2:
            return g_strdup_printf ("%.2f", amount);
        case 3:
            return g_strdup_printf ("%.3f", amount);
        case 4:
            return g_strdup_printf ("%.4f", amount);
        default:
            /* Fallback for unusual precision values */
            return g_strdup_printf ("%.2f", amount);
    }
}

static gboolean
lrg_resource_real_validate_amount (LrgResource *self,
                                   gdouble      amount)
{
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

    /* Check if amount is within valid range */
    if (amount < priv->min_value)
        return FALSE;

    if (amount > priv->max_value)
        return FALSE;

    /* Check for NaN or infinity */
    if (!isfinite (amount))
        return FALSE;

    return TRUE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_resource_finalize (GObject *object)
{
    LrgResource *self = LRG_RESOURCE (object);
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);

    G_OBJECT_CLASS (lrg_resource_parent_class)->finalize (object);
}

static void
lrg_resource_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgResource *self = LRG_RESOURCE (object);
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

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
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_CATEGORY:
        g_value_set_enum (value, priv->category);
        break;
    case PROP_MIN_VALUE:
        g_value_set_double (value, priv->min_value);
        break;
    case PROP_MAX_VALUE:
        g_value_set_double (value, priv->max_value);
        break;
    case PROP_DECIMAL_PLACES:
        g_value_set_uint (value, priv->decimal_places);
        break;
    case PROP_HIDDEN:
        g_value_set_boolean (value, priv->hidden);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_resource_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgResource *self = LRG_RESOURCE (object);
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        lrg_resource_set_name (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_resource_set_description (self, g_value_get_string (value));
        break;
    case PROP_ICON:
        lrg_resource_set_icon (self, g_value_get_string (value));
        break;
    case PROP_CATEGORY:
        lrg_resource_set_category (self, g_value_get_enum (value));
        break;
    case PROP_MIN_VALUE:
        lrg_resource_set_min_value (self, g_value_get_double (value));
        break;
    case PROP_MAX_VALUE:
        lrg_resource_set_max_value (self, g_value_get_double (value));
        break;
    case PROP_DECIMAL_PLACES:
        lrg_resource_set_decimal_places (self, g_value_get_uint (value));
        break;
    case PROP_HIDDEN:
        lrg_resource_set_hidden (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_resource_class_init (LrgResourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_resource_finalize;
    object_class->get_property = lrg_resource_get_property;
    object_class->set_property = lrg_resource_set_property;

    /* Virtual functions */
    klass->format_value = lrg_resource_real_format_value;
    klass->validate_amount = lrg_resource_real_validate_amount;

    /**
     * LrgResource:id:
     *
     * Unique identifier for this resource type.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier for this resource type",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:name:
     *
     * Display name for this resource.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:description:
     *
     * Description text for this resource.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Resource description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:icon:
     *
     * Icon path or identifier for this resource.
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon path or identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:category:
     *
     * The category of this resource.
     */
    properties[PROP_CATEGORY] =
        g_param_spec_enum ("category",
                           "Category",
                           "Resource category",
                           LRG_TYPE_RESOURCE_CATEGORY,
                           LRG_RESOURCE_CATEGORY_CUSTOM,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:min-value:
     *
     * Minimum allowed value for this resource. Use -G_MAXDOUBLE for
     * resources that can go negative (e.g., debt).
     */
    properties[PROP_MIN_VALUE] =
        g_param_spec_double ("min-value",
                             "Minimum Value",
                             "Minimum allowed value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:max-value:
     *
     * Maximum allowed value for this resource. Use G_MAXDOUBLE for
     * unlimited resources.
     */
    properties[PROP_MAX_VALUE] =
        g_param_spec_double ("max-value",
                             "Maximum Value",
                             "Maximum allowed value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, G_MAXDOUBLE,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:decimal-places:
     *
     * Number of decimal places for display formatting.
     */
    properties[PROP_DECIMAL_PLACES] =
        g_param_spec_uint ("decimal-places",
                           "Decimal Places",
                           "Number of decimal places for display",
                           0, 6, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgResource:hidden:
     *
     * Whether this resource is hidden from the player UI.
     */
    properties[PROP_HIDDEN] =
        g_param_spec_boolean ("hidden",
                              "Hidden",
                              "Whether hidden from UI",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_resource_init (LrgResource *self)
{
    LrgResourcePrivate *priv = lrg_resource_get_instance_private (self);

    priv->category = LRG_RESOURCE_CATEGORY_CUSTOM;
    priv->min_value = 0.0;
    priv->max_value = G_MAXDOUBLE;
    priv->decimal_places = 0;
    priv->hidden = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgResource *
lrg_resource_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_RESOURCE,
                         "id", id,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

const gchar *
lrg_resource_get_id (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), NULL);

    priv = lrg_resource_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_resource_get_name (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), NULL);

    priv = lrg_resource_get_instance_private (self);
    return priv->name;
}

void
lrg_resource_set_name (LrgResource *self,
                       const gchar *name)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

const gchar *
lrg_resource_get_description (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), NULL);

    priv = lrg_resource_get_instance_private (self);
    return priv->description;
}

void
lrg_resource_set_description (LrgResource  *self,
                              const gchar  *description)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) != 0)
    {
        g_free (priv->description);
        priv->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

const gchar *
lrg_resource_get_icon (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), NULL);

    priv = lrg_resource_get_instance_private (self);
    return priv->icon;
}

void
lrg_resource_set_icon (LrgResource *self,
                       const gchar *icon)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (g_strcmp0 (priv->icon, icon) != 0)
    {
        g_free (priv->icon);
        priv->icon = g_strdup (icon);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
    }
}

LrgResourceCategory
lrg_resource_get_category (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), LRG_RESOURCE_CATEGORY_CUSTOM);

    priv = lrg_resource_get_instance_private (self);
    return priv->category;
}

void
lrg_resource_set_category (LrgResource         *self,
                           LrgResourceCategory  category)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (priv->category != category)
    {
        priv->category = category;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY]);
    }
}

gdouble
lrg_resource_get_min_value (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), 0.0);

    priv = lrg_resource_get_instance_private (self);
    return priv->min_value;
}

void
lrg_resource_set_min_value (LrgResource *self,
                            gdouble      min_value)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (priv->min_value != min_value)
    {
        priv->min_value = min_value;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_VALUE]);
    }
}

gdouble
lrg_resource_get_max_value (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), G_MAXDOUBLE);

    priv = lrg_resource_get_instance_private (self);
    return priv->max_value;
}

void
lrg_resource_set_max_value (LrgResource *self,
                            gdouble      max_value)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (priv->max_value != max_value)
    {
        priv->max_value = max_value;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
    }
}

guint
lrg_resource_get_decimal_places (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), 0);

    priv = lrg_resource_get_instance_private (self);
    return priv->decimal_places;
}

void
lrg_resource_set_decimal_places (LrgResource *self,
                                 guint        decimal_places)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    /* Clamp to valid range */
    decimal_places = MIN (decimal_places, 6);

    priv = lrg_resource_get_instance_private (self);

    if (priv->decimal_places != decimal_places)
    {
        priv->decimal_places = decimal_places;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DECIMAL_PLACES]);
    }
}

gboolean
lrg_resource_get_hidden (LrgResource *self)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), FALSE);

    priv = lrg_resource_get_instance_private (self);
    return priv->hidden;
}

void
lrg_resource_set_hidden (LrgResource *self,
                         gboolean     hidden)
{
    LrgResourcePrivate *priv;

    g_return_if_fail (LRG_IS_RESOURCE (self));

    priv = lrg_resource_get_instance_private (self);

    if (priv->hidden != hidden)
    {
        priv->hidden = hidden;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIDDEN]);
    }
}

/* ==========================================================================
 * Virtual Function Wrappers
 * ========================================================================== */

gchar *
lrg_resource_format_value (LrgResource *self,
                           gdouble      amount)
{
    LrgResourceClass *klass;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), NULL);

    klass = LRG_RESOURCE_GET_CLASS (self);
    if (klass->format_value != NULL)
        return klass->format_value (self, amount);

    return g_strdup_printf ("%g", amount);
}

gboolean
lrg_resource_validate_amount (LrgResource *self,
                              gdouble      amount)
{
    LrgResourceClass *klass;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), FALSE);

    klass = LRG_RESOURCE_GET_CLASS (self);
    if (klass->validate_amount != NULL)
        return klass->validate_amount (self, amount);

    return TRUE;
}

gdouble
lrg_resource_clamp_amount (LrgResource *self,
                           gdouble      amount)
{
    LrgResourcePrivate *priv;

    g_return_val_if_fail (LRG_IS_RESOURCE (self), amount);

    priv = lrg_resource_get_instance_private (self);

    /* Handle NaN and infinity */
    if (!isfinite (amount))
        return priv->min_value;

    /* Clamp to valid range */
    if (amount < priv->min_value)
        return priv->min_value;
    if (amount > priv->max_value)
        return priv->max_value;

    return amount;
}
