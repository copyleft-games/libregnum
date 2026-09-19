/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-matchmaker.h"
#include "lrg-mmo-service-private.h"
typedef struct { GPtrArray *members; gchar *mode; guint minimum, maximum; gint64 created; } Ticket;
struct _LrgMmoMatchmaker
{
    GObject parent_instance;
    GQueue tickets;
    guint capacity;
    guint queued;
    gint64 last_time;
};
G_DEFINE_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, G_TYPE_OBJECT)
static void
ticket_free (gpointer data)
{
    Ticket *ticket = data;
    g_ptr_array_unref (ticket->members);
    g_free (ticket->mode);
    g_free (ticket);
}
static void
lrg_mmo_matchmaker_finalize (GObject *object)
{
    g_queue_clear_full (&LRG_MMO_MATCHMAKER (object)->tickets, ticket_free);
    G_OBJECT_CLASS (lrg_mmo_matchmaker_parent_class)->finalize (object);
}
static void
lrg_mmo_matchmaker_class_init (LrgMmoMatchmakerClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_matchmaker_finalize;
}
static void
lrg_mmo_matchmaker_init (LrgMmoMatchmaker *self)
{
    g_queue_init (&self->tickets);
}
LrgMmoMatchmaker *
lrg_mmo_matchmaker_new (guint capacity)
{
    LrgMmoMatchmaker *self;
    g_return_val_if_fail (capacity > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_MATCHMAKER, NULL);
    self->capacity = capacity;
    return self;
}
static void
expire (LrgMmoMatchmaker *self, gint64 now)
{
    GList *item = self->tickets.head;
    while (item != NULL)
    {
        GList *next = item->next;
        Ticket *ticket = item->data;
        if (now - ticket->created >= 60 * G_TIME_SPAN_SECOND)
        {
            self->queued -= ticket->members->len;
            g_queue_delete_link (&self->tickets, item);
            ticket_free (ticket);
        }
        item = next;
    }
}

gboolean
lrg_mmo_matchmaker_enqueue_party (LrgMmoMatchmaker *self, GVariant *members, const gchar *mode,
                                  gint64 now_us, GError **error)
{
    GList *item;
    GVariantIter iter;
    const gchar *account;
    guint rating, minimum = G_MAXUINT, maximum = 0, i;
    g_autoptr(GPtrArray) accounts = g_ptr_array_new_with_free_func (g_free);
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (members == NULL || !g_variant_is_of_type (members, G_VARIANT_TYPE ("a(su)")) ||
        g_variant_n_children (members) == 0 || g_variant_n_children (members) > 128 ||
        !_lrg_mmo_id_valid (mode) || now_us < self->last_time)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party ticket or clock");
    self->last_time = now_us;
    expire (self, now_us);
    g_variant_iter_init (&iter, members);
    while (g_variant_iter_next (&iter, "(&su)", &account, &rating))
    {
        if (!_lrg_mmo_id_valid (account))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party account");
        for (i = 0; i < accounts->len; i++)
            if (g_str_equal (account, g_ptr_array_index (accounts, i)))
                return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Duplicate party account");
        for (item = self->tickets.head; item != NULL; item = item->next)
        {
            Ticket *queued = item->data;
            for (i = 0; i < queued->members->len; i++)
                if (g_str_equal (account, g_ptr_array_index (queued->members, i)))
                    return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Account already queued");
        }
        minimum = MIN (minimum, rating);
        maximum = MAX (maximum, rating);
        g_ptr_array_add (accounts, g_strdup (account));
    }
    if (accounts->len > self->capacity - self->queued)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Matchmaking queue full");
    ticket = g_new0 (Ticket, 1);
    ticket->members = g_steal_pointer (&accounts);
    ticket->mode = g_strdup (mode);
    ticket->minimum = minimum;
    ticket->maximum = maximum;
    ticket->created = now_us;
    self->queued += ticket->members->len;
    g_queue_push_tail (&self->tickets, ticket);
    return TRUE;
}

gboolean
lrg_mmo_matchmaker_enqueue (LrgMmoMatchmaker *self, const gchar *account, const gchar *mode,
                            guint rating, gint64 now_us, GError **error)
{
    GVariantBuilder builder;
    g_autoptr(GVariant) members = NULL;
    if (!_lrg_mmo_id_valid (account))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid account");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));
    g_variant_builder_add (&builder, "(su)", account, rating);
    members = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_matchmaker_enqueue_party (self, members, mode, now_us, error);
}

void
lrg_mmo_matchmaker_cancel (LrgMmoMatchmaker *self, const gchar *account)
{
    GList *item;
    g_return_if_fail (LRG_IS_MMO_MATCHMAKER (self));
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        guint i;
        for (i = 0; i < ticket->members->len; i++)
        {
            if (g_strcmp0 (g_ptr_array_index (ticket->members, i), account) == 0)
            {
                self->queued -= ticket->members->len;
                g_queue_delete_link (&self->tickets, item);
                ticket_free (ticket);
                return;
            }
        }
    }
}

GPtrArray *
lrg_mmo_matchmaker_take (LrgMmoMatchmaker *self, const gchar *mode, guint size,
                         guint spread, gint64 now_us, GError **error)
{
    GList *seed;
    GPtrArray *result;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    if (!_lrg_mmo_id_valid (mode) || size < 2 || size > 128 || now_us < self->last_time)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match request");
        return NULL;
    }
    self->last_time = now_us;
    expire (self, now_us);
    result = g_ptr_array_new_with_free_func (g_free);
    for (seed = self->tickets.head; seed != NULL; seed = seed->next)
    {
        Ticket *first = seed->data;
        GList *item;
        guint minimum = first->minimum, maximum = first->maximum;
        guint i;
        if (!g_str_equal (first->mode, mode) || first->members->len > size || maximum - minimum > spread)
            continue;
        g_ptr_array_set_size (result, 0);
        for (i = 0; i < first->members->len; i++)
            g_ptr_array_add (result, g_strdup (g_ptr_array_index (first->members, i)));
        for (item = seed->next; item != NULL && result->len < size; item = item->next)
        {
            Ticket *ticket = item->data;
            guint low = MIN (minimum, ticket->minimum);
            guint high = MAX (maximum, ticket->maximum);
            if (g_str_equal (ticket->mode, mode) && high - low <= spread && ticket->members->len <= size - result->len)
            {
                minimum = low;
                maximum = high;
                for (i = 0; i < ticket->members->len; i++)
                    g_ptr_array_add (result, g_strdup (g_ptr_array_index (ticket->members, i)));
            }
        }
        if (result->len == size)
        {
            for (i = 0; i < result->len; i++)
                lrg_mmo_matchmaker_cancel (self, g_ptr_array_index (result, i));
            return result;
        }
    }
    g_ptr_array_set_size (result, 0);
    return result;
}
