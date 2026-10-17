/* lrg-action-bar.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The contents are one array of pages * slots entries (page-major),
 * each an LrgActionSlot reference or NULL.
 */

#include "config.h"

#include "lrg-action-bar.h"

#include <gio/gio.h>

struct _LrgActionBar
{
    GObject         parent_instance;

    gchar          *name;
    guint           slots;
    guint           pages;
    guint           page;
    gboolean        visible;
    LrgActionSlot **contents;   /* pages * slots */
};

G_DEFINE_FINAL_TYPE (LrgActionBar, lrg_action_bar, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_SLOTS,
    PROP_PAGES,
    PROP_PAGE,
    PROP_VISIBLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_SLOT_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
free_contents (LrgActionSlot **contents,
               guint           count)
{
    guint i;

    if (contents == NULL)
        return;
    for (i = 0; i < count; i++)
    {
        if (contents[i] != NULL)
            lrg_action_slot_unref (contents[i]);
    }
    g_free (contents);
}

static void
lrg_action_bar_finalize (GObject *object)
{
    LrgActionBar *self = LRG_ACTION_BAR (object);

    free_contents (self->contents, self->slots * self->pages);
    g_free (self->name);

    G_OBJECT_CLASS (lrg_action_bar_parent_class)->finalize (object);
}

static void
lrg_action_bar_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgActionBar *self = LRG_ACTION_BAR (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_SLOTS:
        g_value_set_uint (value, self->slots);
        break;
    case PROP_PAGES:
        g_value_set_uint (value, self->pages);
        break;
    case PROP_PAGE:
        g_value_set_uint (value, self->page);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, self->visible);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_action_bar_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgActionBar *self = LRG_ACTION_BAR (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_SLOTS:
        lrg_action_bar_resize (self, g_value_get_uint (value), self->pages);
        break;
    case PROP_PAGES:
        lrg_action_bar_resize (self, self->slots, g_value_get_uint (value));
        break;
    case PROP_PAGE:
        lrg_action_bar_set_page (self, g_value_get_uint (value));
        break;
    case PROP_VISIBLE:
        lrg_action_bar_set_visible (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_action_bar_class_init (LrgActionBarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_action_bar_finalize;
    object_class->get_property = lrg_action_bar_get_property;
    object_class->set_property = lrg_action_bar_set_property;

    /**
     * LrgActionBar:name:
     *
     * The bar's name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "The bar's name", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgActionBar:slots:
     *
     * Slots per page.
     */
    properties[PROP_SLOTS] =
        g_param_spec_uint ("slots", "Slots", "Slots per page", 1, LRG_ACTION_BAR_MAX_SLOTS, 12,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgActionBar:pages:
     *
     * Pages.
     */
    properties[PROP_PAGES] =
        g_param_spec_uint ("pages", "Pages", "Pages", 1, LRG_ACTION_BAR_MAX_PAGES, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgActionBar:page:
     *
     * The page shown.
     */
    properties[PROP_PAGE] =
        g_param_spec_uint ("page", "Page", "The page shown", 0, LRG_ACTION_BAR_MAX_PAGES - 1, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgActionBar:visible:
     *
     * Whether the bar is shown.
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible", "Visible", "Whether the bar is shown", TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_SLOT_CHANGED] =
        g_signal_new ("slot-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
lrg_action_bar_init (LrgActionBar *self)
{
    self->slots = 12;
    self->pages = 1;
    self->visible = TRUE;
    self->contents = g_new0 (LrgActionSlot *, self->slots * self->pages);
}

LrgActionBar *
lrg_action_bar_new (const gchar *name,
                    guint        slots,
                    guint        pages)
{
    LrgActionBar *self;

    self = g_object_new (LRG_TYPE_ACTION_BAR, "name", name, NULL);
    lrg_action_bar_resize (self, slots, pages);
    return self;
}

const gchar *
lrg_action_bar_get_name (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), NULL);
    return self->name;
}

guint
lrg_action_bar_get_slot_count (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), 0);
    return self->slots;
}

guint
lrg_action_bar_get_page_count (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), 0);
    return self->pages;
}

void
lrg_action_bar_resize (LrgActionBar *self,
                       guint         slots,
                       guint         pages)
{
    LrgActionSlot **contents;
    guint p;
    guint i;

    g_return_if_fail (LRG_IS_ACTION_BAR (self));

    slots = CLAMP (slots, 1, LRG_ACTION_BAR_MAX_SLOTS);
    pages = CLAMP (pages, 1, LRG_ACTION_BAR_MAX_PAGES);
    if (slots == self->slots && pages == self->pages)
        return;

    /* --- keep what fits the new layout --- */
    contents = g_new0 (LrgActionSlot *, slots * pages);
    for (p = 0; p < MIN (pages, self->pages); p++)
    {
        for (i = 0; i < MIN (slots, self->slots); i++)
        {
            contents[p * slots + i] = self->contents[p * self->slots + i];
            self->contents[p * self->slots + i] = NULL;
        }
    }
    free_contents (self->contents, self->slots * self->pages);
    self->contents = contents;
    self->slots = slots;
    self->pages = pages;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLOTS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGES]);
    if (self->page >= pages)
    {
        self->page = pages - 1;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
    }
}

guint
lrg_action_bar_get_page (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), 0);
    return self->page;
}

void
lrg_action_bar_set_page (LrgActionBar *self,
                         guint         page)
{
    g_return_if_fail (LRG_IS_ACTION_BAR (self));
    page = MIN (page, self->pages - 1);
    if (page == self->page)
        return;
    self->page = page;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
}

guint
lrg_action_bar_next_page (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), 0);
    lrg_action_bar_set_page (self, (self->page + 1) % self->pages);
    return self->page;
}

guint
lrg_action_bar_previous_page (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), 0);
    lrg_action_bar_set_page (self, (self->page + self->pages - 1) % self->pages);
    return self->page;
}

gboolean
lrg_action_bar_get_visible (LrgActionBar *self)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    return self->visible;
}

void
lrg_action_bar_set_visible (LrgActionBar *self,
                            gboolean      visible)
{
    g_return_if_fail (LRG_IS_ACTION_BAR (self));
    visible = !!visible;
    if (visible == self->visible)
        return;
    self->visible = visible;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
}

LrgActionSlot *
lrg_action_bar_get_slot_at (LrgActionBar *self,
                            guint         page,
                            guint         index)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), NULL);
    if (page >= self->pages || index >= self->slots)
        return NULL;
    return self->contents[page * self->slots + index];
}

LrgActionSlot *
lrg_action_bar_get_slot (LrgActionBar *self,
                         guint         index)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), NULL);
    return lrg_action_bar_get_slot_at (self, self->page, index);
}

gboolean
lrg_action_bar_set_slot_at (LrgActionBar  *self,
                            guint          page,
                            guint          index,
                            LrgActionSlot *slot)
{
    LrgActionSlot **cell;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    if (page >= self->pages || index >= self->slots)
        return FALSE;
    cell = &self->contents[page * self->slots + index];
    if (*cell == slot)
        return TRUE;
    if (slot != NULL)
        lrg_action_slot_ref (slot);
    if (*cell != NULL)
        lrg_action_slot_unref (*cell);
    *cell = slot;
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, page, index);
    return TRUE;
}

gboolean
lrg_action_bar_set_slot (LrgActionBar  *self,
                         guint          index,
                         LrgActionSlot *slot)
{
    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    return lrg_action_bar_set_slot_at (self, self->page, index, slot);
}

gboolean
lrg_action_bar_swap_slots (LrgActionBar *self,
                           guint         a,
                           guint         b)
{
    LrgActionSlot *tmp;
    guint base;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    if (a >= self->slots || b >= self->slots)
        return FALSE;
    if (a == b)
        return TRUE;
    base = self->page * self->slots;
    tmp = self->contents[base + a];
    self->contents[base + a] = self->contents[base + b];
    self->contents[base + b] = tmp;
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, self->page, a);
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, self->page, b);
    return TRUE;
}

gboolean
lrg_action_bar_find (LrgActionBar *self,
                     const gchar  *kind,
                     const gchar  *id,
                     guint        *out_page,
                     guint        *out_index)
{
    guint k;
    guint i;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    g_return_val_if_fail (kind != NULL && id != NULL, FALSE);

    for (k = 0; k < self->pages; k++)
    {
        guint page = (self->page + k) % self->pages;

        for (i = 0; i < self->slots; i++)
        {
            LrgActionSlot *slot = self->contents[page * self->slots + i];

            if (slot != NULL && g_strcmp0 (lrg_action_slot_get_kind (slot), kind) == 0 &&
                g_strcmp0 (lrg_action_slot_get_id (slot), id) == 0)
            {
                if (out_page != NULL)
                    *out_page = page;
                if (out_index != NULL)
                    *out_index = i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

void
lrg_action_bar_clear (LrgActionBar *self)
{
    guint p;
    guint i;

    g_return_if_fail (LRG_IS_ACTION_BAR (self));
    for (p = 0; p < self->pages; p++)
    {
        for (i = 0; i < self->slots; i++)
        {
            if (self->contents[p * self->slots + i] != NULL)
                lrg_action_bar_set_slot_at (self, p, i, NULL);
        }
    }
}

gboolean
lrg_action_bar_query (LrgActionBar          *self,
                      guint                  index,
                      LrgActionSlotProvider *provider,
                      LrgActionSlotState    *state)
{
    LrgActionSlot *slot;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    g_return_val_if_fail (LRG_IS_ACTION_SLOT_PROVIDER (provider), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    slot = lrg_action_bar_get_slot (self, index);
    if (slot == NULL)
    {
        lrg_action_slot_state_init (state);
        return FALSE;
    }
    lrg_action_slot_provider_query (provider, slot, state);
    return TRUE;
}

gboolean
lrg_action_bar_activate (LrgActionBar           *self,
                         guint                   index,
                         LrgActionSlotProvider  *provider,
                         GError                **error)
{
    g_autoptr(LrgActionSlot) slot = NULL;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    g_return_val_if_fail (LRG_IS_ACTION_SLOT_PROVIDER (provider), FALSE);

    /* the provider may change the bar while it uses the slot */
    if (lrg_action_bar_get_slot (self, index) == NULL)
        return FALSE;
    slot = lrg_action_slot_ref (lrg_action_bar_get_slot (self, index));
    return lrg_action_slot_provider_activate (provider, slot, error);
}

GVariant *
lrg_action_bar_to_variant (LrgActionBar *self)
{
    GVariantBuilder filled;
    guint p;
    guint i;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), NULL);

    g_variant_builder_init (&filled, G_VARIANT_TYPE ("a(uu(ssss))"));
    for (p = 0; p < self->pages; p++)
    {
        for (i = 0; i < self->slots; i++)
        {
            LrgActionSlot *slot = self->contents[p * self->slots + i];

            if (slot != NULL)
                g_variant_builder_add (&filled, "(uu@(ssss))", p, i, lrg_action_slot_to_variant (slot));
        }
    }
    return g_variant_new ("(uuuba(uu(ssss)))", self->slots, self->pages, self->page,
                          self->visible, &filled);
}

gboolean
lrg_action_bar_restore (LrgActionBar  *self,
                        GVariant      *variant,
                        GError       **error)
{
    g_autoptr(GVariantIter) iter = NULL;
    GVariant *slot_value;
    guint slots;
    guint pages;
    guint page;
    gboolean visible;
    guint p;
    guint i;

    g_return_val_if_fail (LRG_IS_ACTION_BAR (self), FALSE);
    g_return_val_if_fail (variant != NULL, FALSE);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_ACTION_BAR_VARIANT_TYPE)))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "action bar: expected %s, got %s", LRG_ACTION_BAR_VARIANT_TYPE,
                     g_variant_get_type_string (variant));
        return FALSE;
    }
    g_variant_get (variant, "(uuuba(uu(ssss)))", &slots, &pages, &page, &visible, &iter);
    if (slots < 1 || slots > LRG_ACTION_BAR_MAX_SLOTS || pages < 1 || pages > LRG_ACTION_BAR_MAX_PAGES)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "action bar: a layout of %u slots and %u pages", slots, pages);
        return FALSE;
    }

    /* --- stage every slot first: a bad one changes nothing --- */
    {
        g_autofree LrgActionSlot **cells = g_new0 (LrgActionSlot *, slots * pages);

        while (g_variant_iter_next (iter, "(uu@(ssss))", &p, &i, &slot_value))
        {
            LrgActionSlot *slot;

            slot = lrg_action_slot_new_from_variant (slot_value);
            g_variant_unref (slot_value);
            if (slot == NULL || p >= pages || i >= slots || cells[p * slots + i] != NULL)
            {
                guint k;

                if (slot != NULL)
                    lrg_action_slot_unref (slot);
                for (k = 0; k < slots * pages; k++)
                {
                    if (cells[k] != NULL)
                        lrg_action_slot_unref (cells[k]);
                }
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "action bar: a bad slot at page %u, index %u", p, i);
                return FALSE;
            }
            cells[p * slots + i] = slot;
        }

        /* --- apply --- */
        lrg_action_bar_clear (self);
        lrg_action_bar_resize (self, slots, pages);
        for (p = 0; p < pages; p++)
        {
            for (i = 0; i < slots; i++)
            {
                if (cells[p * slots + i] == NULL)
                    continue;
                lrg_action_bar_set_slot_at (self, p, i, cells[p * slots + i]);
                lrg_action_slot_unref (cells[p * slots + i]);
            }
        }
    }
    lrg_action_bar_set_page (self, page);
    lrg_action_bar_set_visible (self, visible);
    return TRUE;
}
