/* lrg-hbox.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Horizontal box layout container.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-hbox.h"
#include "../lrg-log.h"

struct _LrgHBox
{
    LrgContainer  parent_instance;
    gboolean      homogeneous;
};

G_DEFINE_TYPE (LrgHBox, lrg_hbox, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_HOMOGENEOUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_hbox_layout_children (LrgContainer *container)
{
    LrgHBox *self = LRG_HBOX (container);
    GList   *children;
    GList   *l;
    gfloat   padding;
    gfloat   spacing;
    gfloat   x_offset;
    gfloat   container_height;
    guint    visible_count = 0;

    children = lrg_container_get_children (container);
    padding = lrg_container_get_padding (container);
    spacing = lrg_container_get_spacing (container);
    container_height = lrg_widget_get_height (LRG_WIDGET (container));

    for (l = children; l != NULL; l = l->next)
    {
        if (lrg_widget_get_visible (LRG_WIDGET (l->data)))
        {
            visible_count++;
        }
    }

    if (visible_count == 0)
    {
        return;
    }

    x_offset = padding;

    if (self->homogeneous)
    {
        gfloat container_width = lrg_widget_get_width (LRG_WIDGET (container));
        gfloat total_spacing = spacing * (visible_count - 1);
        gfloat available_width = container_width - padding * 2 - total_spacing;
        gfloat child_width = available_width / visible_count;

        for (l = children; l != NULL; l = l->next)
        {
            LrgWidget *child = LRG_WIDGET (l->data);

            if (!lrg_widget_get_visible (child))
            {
                continue;
            }

            lrg_widget_set_position (child, x_offset, padding);
            lrg_widget_set_size (child, child_width, container_height - padding * 2);

            x_offset += child_width + spacing;
        }
    }
    else
    {
        for (l = children; l != NULL; l = l->next)
        {
            LrgWidget *child = LRG_WIDGET (l->data);
            gfloat     pref_width, pref_height;

            if (!lrg_widget_get_visible (child))
            {
                continue;
            }

            lrg_widget_measure (child, &pref_width, &pref_height);
            lrg_widget_set_position (child, x_offset, padding);
            lrg_widget_set_size (child, pref_width, container_height - padding * 2);

            x_offset += pref_width + spacing;
        }
    }
}

static void
lrg_hbox_measure (LrgWidget *widget,
                  gfloat    *preferred_width,
                  gfloat    *preferred_height)
{
    LrgContainer *container = LRG_CONTAINER (widget);
    GList        *children;
    GList        *l;
    gfloat        total_width = 0.0f;
    gfloat        max_height = 0.0f;
    gfloat        padding;
    gfloat        spacing;
    guint         visible_count = 0;

    children = lrg_container_get_children (container);
    padding = lrg_container_get_padding (container);
    spacing = lrg_container_get_spacing (container);

    for (l = children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     child_width, child_height;

        if (!lrg_widget_get_visible (child))
        {
            continue;
        }

        lrg_widget_measure (child, &child_width, &child_height);

        total_width += child_width;
        if (child_height > max_height)
        {
            max_height = child_height;
        }
        visible_count++;
    }

    if (visible_count > 0)
    {
        total_width += spacing * (visible_count - 1);
    }

    if (preferred_width != NULL)
    {
        *preferred_width = total_width + padding * 2;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = max_height + padding * 2;
    }
}

static void
lrg_hbox_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgHBox *self = LRG_HBOX (object);

    switch (prop_id)
    {
    case PROP_HOMOGENEOUS:
        g_value_set_boolean (value, self->homogeneous);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_hbox_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgHBox *self = LRG_HBOX (object);

    switch (prop_id)
    {
    case PROP_HOMOGENEOUS:
        lrg_hbox_set_homogeneous (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_hbox_class_init (LrgHBoxClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
    LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

    object_class->get_property = lrg_hbox_get_property;
    object_class->set_property = lrg_hbox_set_property;

    widget_class->measure = lrg_hbox_measure;
    container_class->layout_children = lrg_hbox_layout_children;

    properties[PROP_HOMOGENEOUS] =
        g_param_spec_boolean ("homogeneous",
                              "Homogeneous",
                              "Whether children get equal widths",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_hbox_init (LrgHBox *self)
{
    self->homogeneous = FALSE;
}

LrgHBox *
lrg_hbox_new (void)
{
    return g_object_new (LRG_TYPE_HBOX, NULL);
}

gboolean
lrg_hbox_get_homogeneous (LrgHBox *self)
{
    g_return_val_if_fail (LRG_IS_HBOX (self), FALSE);
    return self->homogeneous;
}

void
lrg_hbox_set_homogeneous (LrgHBox  *self,
                          gboolean  homogeneous)
{
    g_return_if_fail (LRG_IS_HBOX (self));

    homogeneous = !!homogeneous;

    if (self->homogeneous != homogeneous)
    {
        self->homogeneous = homogeneous;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOMOGENEOUS]);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}
