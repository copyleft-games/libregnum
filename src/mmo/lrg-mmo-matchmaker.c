/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-matchmaker.h"
#include "lrg-mmo-service-private.h"
typedef struct { gchar *account; gchar *mode; guint rating; gint64 created; } Ticket;
struct _LrgMmoMatchmaker
{
    GObject parent_instance;
    GQueue tickets;
    guint capacity;
    gint64 last_time;
};
G_DEFINE_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, G_TYPE_OBJECT)
static void
ticket_free (gpointer data)
{
    Ticket *ticket = data;
    g_free (ticket->account);
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
            g_queue_delete_link (&self->tickets, item);
            ticket_free (ticket);
        }
        item = next;
    }
}

gboolean
lrg_mmo_matchmaker_enqueue (LrgMmoMatchmaker *self, const gchar *account, const gchar *mode,
                            guint rating, gint64 now_us, GError **error)
{
    GList *item;
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (!_lrg_mmo_id_valid (account) || !_lrg_mmo_id_valid (mode) || now_us < self->last_time)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid matchmaking ticket or clock");
    self->last_time = now_us;
    expire (self, now_us);
    for (item = self->tickets.head; item != NULL; item = item->next)
        if (g_str_equal (((Ticket *) item->data)->account, account))
            return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Account already queued");
    if (self->tickets.length >= self->capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Matchmaking queue full");
    ticket = g_new0 (Ticket, 1);
    ticket->account = g_strdup (account);
    ticket->mode = g_strdup (mode);
    ticket->rating = rating;
    ticket->created = now_us;
    g_queue_push_tail (&self->tickets, ticket);
    return TRUE;
}

void
lrg_mmo_matchmaker_cancel (LrgMmoMatchmaker *self, const gchar *account)
{
    GList *item;
    g_return_if_fail (LRG_IS_MMO_MATCHMAKER (self));
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        if (g_strcmp0 (ticket->account, account) == 0)
        {
            g_queue_delete_link (&self->tickets, item);
            ticket_free (ticket);
            return;
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
        guint minimum = first->rating, maximum = first->rating;
        guint i;
        if (!g_str_equal (first->mode, mode))
            continue;
        g_ptr_array_set_size (result, 0);
        g_ptr_array_add (result, g_strdup (first->account));
        for (item = seed->next; item != NULL && result->len < size; item = item->next)
        {
            Ticket *ticket = item->data;
            guint low = MIN (minimum, ticket->rating);
            guint high = MAX (maximum, ticket->rating);
            if (g_str_equal (ticket->mode, mode) && high - low <= spread)
            {
                minimum = low;
                maximum = high;
                g_ptr_array_add (result, g_strdup (ticket->account));
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
