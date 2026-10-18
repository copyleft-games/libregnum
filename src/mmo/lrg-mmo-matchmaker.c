/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-matchmaker.h"
#include "lrg-mmo-service-private.h"
#include <string.h>
/* a queued party; ratings holds one guint per member, in member order */
typedef struct { GPtrArray *members; GArray *ratings; gchar *mode; guint minimum, maximum; gint64 created; } Ticket;

/* how many placements lrg_mmo_matchmaker_take_teams() tries per candidate set */
#define TEAM_SEARCH_BUDGET (4096)
struct _LrgMmoMatchmaker
{
    GObject parent_instance;
    GQueue tickets;
    guint capacity;
    guint queued;
    gint64 last_time;
    gint64 lifetime;
};
G_DEFINE_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, G_TYPE_OBJECT)
static void
ticket_free (gpointer data)
{
    Ticket *ticket = data;
    g_ptr_array_unref (ticket->members);
    g_array_unref (ticket->ratings);
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
    self->lifetime = 60 * G_TIME_SPAN_SECOND;
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
        if (now - ticket->created >= self->lifetime)
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
    g_autoptr(GArray) ratings = g_array_new (FALSE, FALSE, sizeof (guint));
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
        g_array_append_val (ratings, rating);
    }
    if (accounts->len > self->capacity - self->queued)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Matchmaking queue full");
    ticket = g_new0 (Ticket, 1);
    ticket->members = g_steal_pointer (&accounts);
    ticket->ratings = g_steal_pointer (&ratings);
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

void
lrg_mmo_matchmaker_set_ticket_lifetime (LrgMmoMatchmaker *self, gint64 lifetime_us)
{
    g_return_if_fail (LRG_IS_MMO_MATCHMAKER (self));
    self->lifetime = MAX (lifetime_us, G_TIME_SPAN_SECOND);
}

gint64
lrg_mmo_matchmaker_get_ticket_lifetime (LrgMmoMatchmaker *self)
{
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), 0);
    return self->lifetime;
}

/* the ticket holding @account, or NULL */
static Ticket *
find_ticket (LrgMmoMatchmaker *self, const gchar *account)
{
    GList *item;
    guint i;
    if (account == NULL)
        return NULL;
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        for (i = 0; i < ticket->members->len; i++)
            if (g_str_equal (account, g_ptr_array_index (ticket->members, i)))
                return ticket;
    }
    return NULL;
}

gboolean
lrg_mmo_matchmaker_is_queued (LrgMmoMatchmaker *self, const gchar *account)
{
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    return find_ticket (self, account) != NULL;
}

guint
lrg_mmo_matchmaker_get_queued (LrgMmoMatchmaker *self, const gchar *mode)
{
    GList *item;
    guint count = 0;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), 0);
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        if (mode == NULL || g_str_equal (ticket->mode, mode))
            count += ticket->members->len;
    }
    return count;
}

gint64
lrg_mmo_matchmaker_get_wait (LrgMmoMatchmaker *self, const gchar *account, gint64 now_us)
{
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), -1);
    ticket = find_ticket (self, account);
    if (ticket == NULL)
        return -1;
    return MAX (now_us - ticket->created, (gint64) 0);
}

/* the first (oldest) ticket of @mode, or NULL */
static Ticket *
oldest_ticket (LrgMmoMatchmaker *self, const gchar *mode)
{
    GList *item;
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        if (mode != NULL && g_str_equal (ticket->mode, mode))
            return ticket;
    }
    return NULL;
}

gint64
lrg_mmo_matchmaker_get_oldest_wait (LrgMmoMatchmaker *self, const gchar *mode, gint64 now_us)
{
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), -1);
    ticket = oldest_ticket (self, mode);
    if (ticket == NULL)
        return -1;
    return MAX (now_us - ticket->created, (gint64) 0);
}

gboolean
lrg_mmo_matchmaker_get_oldest_rating (LrgMmoMatchmaker *self, const gchar *mode,
                                      guint *out_rating, guint *out_size)
{
    Ticket *ticket;
    guint64 sum = 0;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    g_return_val_if_fail (out_rating != NULL, FALSE);
    ticket = oldest_ticket (self, mode);
    if (ticket == NULL)
        return FALSE;
    for (i = 0; i < ticket->ratings->len; i++)
        sum += g_array_index (ticket->ratings, guint, i);
    *out_rating = (guint) (sum / MAX (ticket->ratings->len, 1u));
    if (out_size != NULL)
        *out_size = ticket->members->len;
    return TRUE;
}

/*
 * Team packing: the candidate tickets (exactly teams * team_size members)
 * are sorted by size, then rating, descending; each goes to an open team in
 * order of the team's rating total, smallest first, with backtracking when a
 * later party no longer fits. The search stops after TEAM_SEARCH_BUDGET
 * placements.
 */
typedef struct
{
    Ticket **tickets;
    guint n;
    guint team_size;
    guint teams;
    guint fill[16];
    guint64 sum[16];
    guint *assign;
    guint budget;
} Packing;

static guint64
ticket_sum (Ticket *ticket)
{
    guint64 sum = 0;
    guint i;
    for (i = 0; i < ticket->ratings->len; i++)
        sum += g_array_index (ticket->ratings, guint, i);
    return sum;
}

static gint
ticket_order (gconstpointer a, gconstpointer b)
{
    Ticket *first = *(Ticket * const *) a;
    Ticket *second = *(Ticket * const *) b;
    guint64 x, y;
    if (first->members->len != second->members->len)
        return first->members->len > second->members->len ? -1 : 1;
    x = ticket_sum (first);
    y = ticket_sum (second);
    if (x != y)
        return x > y ? -1 : 1;
    /* stable for equal tickets: older first */
    return first->created < second->created ? -1 : (first->created > second->created ? 1 : 0);
}

static gboolean
pack (Packing *p, guint index)
{
    guint order[16];
    guint i, j, k;
    gboolean tried_empty = FALSE;
    Ticket *ticket;
    if (index == p->n)
        return TRUE;
    if (p->budget == 0)
        return FALSE;
    p->budget--;
    ticket = p->tickets[index];
    /* the teams by rating total, then by fill, ascending */
    for (i = 0; i < p->teams; i++)
        order[i] = i;
    for (i = 1; i < p->teams; i++)
    {
        guint value = order[i];
        j = i;
        while (j > 0 && (p->sum[order[j - 1]] > p->sum[value] ||
                         (p->sum[order[j - 1]] == p->sum[value] && p->fill[order[j - 1]] > p->fill[value])))
        {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = value;
    }
    for (k = 0; k < p->teams; k++)
    {
        j = order[k];
        if (p->fill[j] + ticket->members->len > p->team_size)
            continue;
        /* empty teams are alike: trying one is trying them all */
        if (p->fill[j] == 0)
        {
            if (tried_empty)
                continue;
            tried_empty = TRUE;
        }
        p->fill[j] += ticket->members->len;
        p->sum[j] += ticket_sum (ticket);
        p->assign[index] = j;
        if (pack (p, index + 1))
            return TRUE;
        p->fill[j] -= ticket->members->len;
        p->sum[j] -= ticket_sum (ticket);
    }
    return FALSE;
}

GPtrArray *
lrg_mmo_matchmaker_take_teams (LrgMmoMatchmaker *self, const gchar *mode, guint team_size,
                               guint teams, guint spread, gint64 now_us, GError **error)
{
    GList *seed;
    GPtrArray *result;
    guint needed;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    if (!_lrg_mmo_id_valid (mode) || team_size < 1 || team_size > 64 || teams < 2 || teams > 16 ||
        now_us < self->last_time)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid team match request");
        return NULL;
    }
    self->last_time = now_us;
    expire (self, now_us);
    needed = team_size * teams;
    result = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
    for (seed = self->tickets.head; seed != NULL; seed = seed->next)
    {
        Ticket *first = seed->data;
        g_autoptr(GPtrArray) chosen = NULL;
        g_autofree guint *assign = NULL;
        GList *item;
        guint minimum = first->minimum, maximum = first->maximum;
        guint total, i;
        Packing packing;
        if (!g_str_equal (first->mode, mode) || first->members->len > team_size ||
            maximum - minimum > spread)
            continue;
        /* --- the candidates: whole FIFO tickets up to the match's size --- */
        chosen = g_ptr_array_new ();
        g_ptr_array_add (chosen, first);
        total = first->members->len;
        for (item = seed->next; item != NULL && total < needed; item = item->next)
        {
            Ticket *ticket = item->data;
            guint low = MIN (minimum, ticket->minimum);
            guint high = MAX (maximum, ticket->maximum);
            if (!g_str_equal (ticket->mode, mode) || high - low > spread ||
                ticket->members->len > team_size || total + ticket->members->len > needed)
                continue;
            minimum = low;
            maximum = high;
            g_ptr_array_add (chosen, ticket);
            total += ticket->members->len;
        }
        if (total != needed)
            continue;
        /* --- the teams: parties whole, ratings balanced --- */
        g_ptr_array_sort (chosen, ticket_order);
        memset (&packing, 0, sizeof (packing));
        assign = g_new0 (guint, chosen->len);
        packing.tickets = (Ticket **) chosen->pdata;
        packing.n = chosen->len;
        packing.team_size = team_size;
        packing.teams = teams;
        packing.assign = assign;
        packing.budget = TEAM_SEARCH_BUDGET;
        if (!pack (&packing, 0))
            continue;
        for (i = 0; i < teams; i++)
            g_ptr_array_add (result, g_ptr_array_new_with_free_func (g_free));
        for (i = 0; i < chosen->len; i++)
        {
            Ticket *ticket = g_ptr_array_index (chosen, i);
            GPtrArray *team = g_ptr_array_index (result, assign[i]);
            guint j;
            for (j = 0; j < ticket->members->len; j++)
                g_ptr_array_add (team, g_strdup (g_ptr_array_index (ticket->members, j)));
        }
        /* --- the chosen tickets leave the queue --- */
        for (i = 0; i < chosen->len; i++)
        {
            Ticket *ticket = g_ptr_array_index (chosen, i);
            self->queued -= ticket->members->len;
            g_queue_remove (&self->tickets, ticket);
            ticket_free (ticket);
        }
        return result;
    }
    return result;
}
