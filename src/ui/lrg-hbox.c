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
#include "lrg-widget-private.h"
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
    /* Shared box layout: margins, alignment, fill weights, min/max */
    _lrg_box_layout (container, FALSE, LRG_HBOX (container)->homogeneous);
}

static void
lrg_hbox_measure (LrgWidget *widget,
                  gfloat    *preferred_width,
                  gfloat    *preferred_height)
{
    _lrg_box_measure (LRG_CONTAINER (widget), FALSE, LRG_HBOX (widget)->homogeneous,
                      preferred_width, preferred_height);
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
