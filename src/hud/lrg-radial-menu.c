/* lrg-radial-menu.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRadialMenu: n items share the circle in sectors of 360 / n
 * degrees; item i's sector is centred at start + i * 360 / n (clockwise
 * from up).  A direction's angle is atan2(dx, -dy) (clockwise from up
 * with +Y down), shifted by half a sector so the boundaries fall between
 * items.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "hud/lrg-radial-menu.h"

#include <math.h>

typedef struct
{
    gchar *id;
    gchar *label;
} Item;

struct _LrgRadialMenu
{
    GObject  parent_instance;

    GArray  *items;       /* Item */
    gfloat   start_angle;
    gfloat   dead_zone;
    gboolean open;
    gint     selected;
};

G_DEFINE_TYPE (LrgRadialMenu, lrg_radial_menu, G_TYPE_OBJECT)

enum
{
    SIGNAL_SELECTION_CHANGED,
    SIGNAL_ACTIVATED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
item_clear (gpointer data)
{
    Item *item = data;

    g_free (item->id);
    g_free (item->label);
}

static void
lrg_radial_menu_finalize (GObject *object)
{
    LrgRadialMenu *self = LRG_RADIAL_MENU (object);

    g_array_unref (self->items);
    G_OBJECT_CLASS (lrg_radial_menu_parent_class)->finalize (object);
}

static void
lrg_radial_menu_class_init (LrgRadialMenuClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_radial_menu_finalize;

    signals[SIGNAL_SELECTION_CHANGED] = g_signal_new ("selection-changed",
                                                      G_TYPE_FROM_CLASS (klass),
                                                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                                      G_TYPE_NONE, 1, G_TYPE_INT);
    signals[SIGNAL_ACTIVATED] = g_signal_new ("activated",
                                              G_TYPE_FROM_CLASS (klass),
                                              G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
}

static void
lrg_radial_menu_init (LrgRadialMenu *self)
{
    self->items = g_array_new (FALSE, TRUE, sizeof (Item));
    g_array_set_clear_func (self->items, item_clear);
    self->dead_zone = LRG_RADIAL_MENU_DEFAULT_DEAD_ZONE;
    self->selected = -1;
}

LrgRadialMenu *
lrg_radial_menu_new (void)
{
    return g_object_new (LRG_TYPE_RADIAL_MENU, NULL);
}

gint
lrg_radial_menu_find (LrgRadialMenu *self,
                      const gchar   *item_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    if (item_id == NULL)
        return -1;
    for (i = 0; i < self->items->len; i++)
    {
        if (g_strcmp0 (g_array_index (self->items, Item, i).id, item_id) == 0)
            return (gint) i;
    }
    return -1;
}

gint
lrg_radial_menu_add_item (LrgRadialMenu *self,
                          const gchar   *item_id,
                          const gchar   *label)
{
    Item item;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    if (item_id == NULL || *item_id == '\0' || self->items->len >= LRG_RADIAL_MENU_MAX_ITEMS ||
        lrg_radial_menu_find (self, item_id) >= 0)
        return -1;
    item.id = g_strdup (item_id);
    item.label = g_strdup (label != NULL ? label : item_id);
    g_array_append_val (self->items, item);
    return (gint) self->items->len - 1;
}

guint
lrg_radial_menu_get_count (LrgRadialMenu *self)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), 0);
    return self->items->len;
}

const gchar *
lrg_radial_menu_get_item_id (LrgRadialMenu *self,
                             guint          index)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), NULL);
    return index < self->items->len ? g_array_index (self->items, Item, index).id : NULL;
}

const gchar *
lrg_radial_menu_get_item_label (LrgRadialMenu *self,
                                guint          index)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), NULL);
    return index < self->items->len ? g_array_index (self->items, Item, index).label : NULL;
}

void
lrg_radial_menu_set_start_angle (LrgRadialMenu *self,
                                 gfloat         degrees)
{
    g_return_if_fail (LRG_IS_RADIAL_MENU (self));

    if (!isfinite (degrees))
        degrees = 0.0f;
    degrees = fmodf (degrees, 360.0f);
    if (degrees < 0.0f)
        degrees += 360.0f;
    self->start_angle = degrees;
}

gfloat
lrg_radial_menu_get_start_angle (LrgRadialMenu *self)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), 0.0f);
    return self->start_angle;
}

void
lrg_radial_menu_set_dead_zone (LrgRadialMenu *self,
                               gfloat         radius)
{
    g_return_if_fail (LRG_IS_RADIAL_MENU (self));
    self->dead_zone = isfinite (radius) ? MAX (radius, 0.0f) : 0.0f;
}

gfloat
lrg_radial_menu_get_dead_zone (LrgRadialMenu *self)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), 0.0f);
    return self->dead_zone;
}

gint
lrg_radial_menu_index_at (LrgRadialMenu *self,
                          gfloat         dx,
                          gfloat         dy)
{
    gdouble angle;
    gdouble sector;
    guint n;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    n = self->items->len;
    if (n == 0 || !isfinite (dx) || !isfinite (dy))
        return -1;
    if (sqrtf (dx * dx + dy * dy) < self->dead_zone || (dx == 0.0f && dy == 0.0f))
        return -1;

    /* clockwise from up, relative to the first sector's start edge */
    sector = 360.0 / (gdouble) n;
    angle = atan2 ((gdouble) dx, (gdouble) -dy) * 180.0 / G_PI;
    angle = angle - (gdouble) self->start_angle + sector * 0.5;
    angle = fmod (angle, 360.0);
    if (angle < 0.0)
        angle += 360.0;
    return (gint) MIN ((guint) (angle / sector), n - 1);
}

static void
select_index (LrgRadialMenu *self,
              gint           index)
{
    if (index == self->selected)
        return;
    self->selected = index;
    g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0, index);
}

void
lrg_radial_menu_open (LrgRadialMenu *self,
                      gint           selected)
{
    g_return_if_fail (LRG_IS_RADIAL_MENU (self));

    self->open = TRUE;
    select_index (self, selected >= 0 && (guint) selected < self->items->len ? selected : -1);
}

gboolean
lrg_radial_menu_is_open (LrgRadialMenu *self)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), FALSE);
    return self->open;
}

gint
lrg_radial_menu_point (LrgRadialMenu *self,
                       gfloat         dx,
                       gfloat         dy)
{
    gint index;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    if (!self->open)
        return -1;
    index = lrg_radial_menu_index_at (self, dx, dy);
    if (index >= 0)
        select_index (self, index);
    return self->selected;
}

gint
lrg_radial_menu_step (LrgRadialMenu *self,
                      gint           steps)
{
    gint n;
    gint index;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    n = (gint) self->items->len;
    if (!self->open || n == 0)
        return -1;
    if (self->selected < 0)
        index = steps >= 0 ? MAX (steps - 1, 0) % n : ((steps % n) + n) % n;
    else
        index = (((self->selected + steps) % n) + n) % n;
    select_index (self, index);
    return self->selected;
}

gint
lrg_radial_menu_get_selected (LrgRadialMenu *self)
{
    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);
    return self->selected;
}

gint
lrg_radial_menu_close (LrgRadialMenu *self,
                       gboolean       activate)
{
    gint chosen;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), -1);

    if (!self->open)
        return -1;
    self->open = FALSE;
    chosen = activate ? self->selected : -1;
    if (chosen >= 0)
        g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, chosen,
                       g_array_index (self->items, Item, chosen).id);
    return chosen;
}

gboolean
lrg_radial_menu_get_item_position (LrgRadialMenu *self,
                                   guint          index,
                                   gfloat         radius,
                                   gfloat        *out_x,
                                   gfloat        *out_y)
{
    gdouble angle;

    g_return_val_if_fail (LRG_IS_RADIAL_MENU (self), FALSE);
    g_return_val_if_fail (out_x != NULL && out_y != NULL, FALSE);

    if (index >= self->items->len)
        return FALSE;
    angle = ((gdouble) self->start_angle + 360.0 / (gdouble) self->items->len * (gdouble) index) *
            G_PI / 180.0;
    *out_x = (gfloat) (sin (angle) * (gdouble) radius);
    *out_y = (gfloat) (-cos (angle) * (gdouble) radius);
    return TRUE;
}
