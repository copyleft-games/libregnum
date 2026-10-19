/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-matchmaker.h"
#include "lrg-mmo-service-private.h"
#include <string.h>

/*
 * One ticket is a party queued together (a solo player is a party of
 * one).  Tickets stay in the queue ordered by @created (oldest first);
 * a ticket put back with an old @created keeps its place.  Each member
 * has a rating and a role mask (0: any role) for role queues.
 */
typedef struct
{
    GPtrArray *members;   /* gchar* account */
    GArray *ratings;      /* guint */
    GArray *roles;        /* guint mask */
    gchar *mode;
    guint minimum, maximum;
    gint64 created;
} Ticket;

struct _LrgMmoMatchmaker
{
    GObject parent_instance;
    GQueue tickets;
    guint capacity;
    guint queued;
    gint64 last_time;
    gint64 timeout;
};

G_DEFINE_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, G_TYPE_OBJECT)

#define DEFAULT_TIMEOUT (60 * G_TIME_SPAN_SECOND)
#define MAX_MEMBERS (128)

/* how many placements lrg_mmo_matchmaker_take_teams() tries per candidate set */
#define TEAM_SEARCH_BUDGET (4096)

static void
ticket_free (gpointer data)
{
    Ticket *ticket = data;
    g_ptr_array_unref (ticket->members);
    g_array_unref (ticket->ratings);
    g_array_unref (ticket->roles);
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
    self->timeout = DEFAULT_TIMEOUT;
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

/* the ticket and member index of @account, NULL when not queued */
static GList *
find_account (LrgMmoMatchmaker *self, const gchar *account, guint *index)
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
            {
                if (index != NULL)
                    *index = i;
                return item;
            }
    }
    return NULL;
}

static void
drop_ticket (LrgMmoMatchmaker *self, GList *item)
{
    Ticket *ticket = item->data;
    self->queued -= ticket->members->len;
    g_queue_delete_link (&self->tickets, item);
    ticket_free (ticket);
}

/* drops the tickets as old as the timeout; their accounts go to @expired */
static void
expire_into (LrgMmoMatchmaker *self, gint64 now, GPtrArray *expired)
{
    GList *item = self->tickets.head;
    while (item != NULL)
    {
        GList *next = item->next;
        Ticket *ticket = item->data;
        if (now - ticket->created >= self->timeout)
        {
            guint i;
            if (expired != NULL)
                for (i = 0; i < ticket->members->len; i++)
                    g_ptr_array_add (expired, g_strdup (g_ptr_array_index (ticket->members, i)));
            drop_ticket (self, item);
        }
        item = next;
    }
}

static void
expire (LrgMmoMatchmaker *self, gint64 now)
{
    expire_into (self, now, NULL);
}

/* inserts a ticket among the others by its creation time (stable) */
static void
insert_ticket (LrgMmoMatchmaker *self, Ticket *ticket)
{
    GList *item;
    for (item = self->tickets.tail; item != NULL; item = item->prev)
        if (((Ticket *) item->data)->created <= ticket->created)
            break;
    if (item == NULL)
        g_queue_push_head (&self->tickets, ticket);
    else
        g_queue_insert_after (&self->tickets, item, ticket);
    self->queued += ticket->members->len;
}

/*
 * Checks a(suu) members (or a(su) when @with_roles is FALSE) and builds a
 * ticket of them, or fails without touching the queue.
 */
static Ticket *
ticket_build (LrgMmoMatchmaker *self, GVariant *members, gboolean with_roles,
              const gchar *mode, gint64 created, GError **error)
{
    GVariantIter iter;
    const gchar *account;
    guint rating, roles, i;
    Ticket *ticket;
    g_autoptr(GPtrArray) accounts = g_ptr_array_new_with_free_func (g_free);
    g_autoptr(GArray) ratings = g_array_new (FALSE, FALSE, sizeof (guint));
    g_autoptr(GArray) masks = g_array_new (FALSE, FALSE, sizeof (guint));
    guint minimum = G_MAXUINT, maximum = 0;

    if (members == NULL ||
        !g_variant_is_of_type (members, with_roles ? G_VARIANT_TYPE ("a(suu)") : G_VARIANT_TYPE ("a(su)")) ||
        g_variant_n_children (members) == 0 || g_variant_n_children (members) > MAX_MEMBERS ||
        !_lrg_mmo_id_valid (mode))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party ticket or clock");
        return NULL;
    }
    g_variant_iter_init (&iter, members);
    while (with_roles ? g_variant_iter_next (&iter, "(&suu)", &account, &rating, &roles)
                      : g_variant_iter_next (&iter, "(&su)", &account, &rating))
    {
        if (!with_roles)
            roles = 0;
        if (!_lrg_mmo_id_valid (account))
        {
            _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party account");
            return NULL;
        }
        if (roles >= (1u << LRG_MMO_MATCHMAKER_MAX_ROLES))
        {
            _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid role mask");
            return NULL;
        }
        for (i = 0; i < accounts->len; i++)
            if (g_str_equal (account, g_ptr_array_index (accounts, i)))
            {
                _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Duplicate party account");
                return NULL;
            }
        if (find_account (self, account, NULL) != NULL)
        {
            _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Account already queued");
            return NULL;
        }
        minimum = MIN (minimum, rating);
        maximum = MAX (maximum, rating);
        g_ptr_array_add (accounts, g_strdup (account));
        g_array_append_val (ratings, rating);
        g_array_append_val (masks, roles);
    }
    if (accounts->len > self->capacity - self->queued)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Matchmaking queue full");
        return NULL;
    }
    ticket = g_new0 (Ticket, 1);
    ticket->members = g_steal_pointer (&accounts);
    ticket->ratings = g_steal_pointer (&ratings);
    ticket->roles = g_steal_pointer (&masks);
    ticket->mode = g_strdup (mode);
    ticket->minimum = minimum;
    ticket->maximum = maximum;
    ticket->created = created;
    return ticket;
}

gboolean
lrg_mmo_matchmaker_enqueue_party (LrgMmoMatchmaker *self, GVariant *members, const gchar *mode,
                                  gint64 now_us, GError **error)
{
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (now_us < self->last_time)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party ticket or clock");
    self->last_time = now_us;
    expire (self, now_us);
    ticket = ticket_build (self, members, FALSE, mode, now_us, error);
    if (ticket == NULL)
        return FALSE;
    insert_ticket (self, ticket);
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
    item = find_account (self, account, NULL);
    if (item != NULL)
        drop_ticket (self, item);
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

/* ------------------------------------------------------------------ */
/* Role queues                                                        */

void
lrg_mmo_matchmaker_set_timeout (LrgMmoMatchmaker *self, gint64 timeout_us)
{
    g_return_if_fail (LRG_IS_MMO_MATCHMAKER (self));
    g_return_if_fail (timeout_us >= G_TIME_SPAN_SECOND);
    self->timeout = timeout_us;
}

gint64
lrg_mmo_matchmaker_get_timeout (LrgMmoMatchmaker *self)
{
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), 0);
    return self->timeout;
}

gboolean
lrg_mmo_matchmaker_enqueue_roles (LrgMmoMatchmaker *self, GVariant *members, const gchar *mode,
                                  gint64 since_us, gint64 now_us, GError **error)
{
    Ticket *ticket;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (now_us < self->last_time || since_us > now_us || since_us < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid party ticket or clock");
    self->last_time = now_us;
    expire (self, now_us);
    if (now_us - since_us >= self->timeout)
        return _lrg_mmo_fail (error, G_IO_ERROR_TIMED_OUT, "Ticket already expired");
    ticket = ticket_build (self, members, TRUE, mode, since_us, error);
    if (ticket == NULL)
        return FALSE;
    insert_ticket (self, ticket);
    return TRUE;
}

gboolean
lrg_mmo_matchmaker_set_roles (LrgMmoMatchmaker *self, const gchar *account, guint roles,
                              GError **error)
{
    GList *item;
    guint index;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (roles >= (1u << LRG_MMO_MATCHMAKER_MAX_ROLES))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid role mask");
    item = find_account (self, account, &index);
    if (item == NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "Account not queued");
    g_array_index (((Ticket *) item->data)->roles, guint, index) = roles;
    return TRUE;
}

/*
 * Role assignment: members are matched to seats (one per slot of a role)
 * by Kuhn's augmenting paths, so the result is a maximum matching: a
 * group is feasible when every member gets a seat.
 */
typedef struct
{
    GArray *masks;      /* guint per member */
    GArray *seat_role;  /* guint role per seat */
    gint *seat_member;  /* member per seat, -1 */
    gint *member_seat;  /* seat per member, -1 */
    gboolean *visited;  /* per seat, during one search */
} Assignment;

static gboolean
seat_fits (Assignment *a, guint member, guint seat)
{
    guint mask = g_array_index (a->masks, guint, member);
    return mask == 0 || (mask & (1u << g_array_index (a->seat_role, guint, seat))) != 0;
}

static gboolean
augment (Assignment *a, guint member)
{
    guint seat;
    for (seat = 0; seat < a->seat_role->len; seat++)
    {
        if (a->visited[seat] || !seat_fits (a, member, seat))
            continue;
        a->visited[seat] = TRUE;
        if (a->seat_member[seat] < 0 || augment (a, (guint) a->seat_member[seat]))
        {
            a->seat_member[seat] = (gint) member;
            a->member_seat[member] = (gint) seat;
            return TRUE;
        }
    }
    return FALSE;
}

/* whether every member of @masks gets a seat; @out_roles (optional) the roles */
static gboolean
assign_roles (GArray *masks, GArray *seat_role, GArray *out_roles)
{
    Assignment a;
    guint i;
    gboolean ok = TRUE;
    if (masks->len > seat_role->len)
        return FALSE;
    a.masks = masks;
    a.seat_role = seat_role;
    a.seat_member = g_new (gint, MAX (seat_role->len, 1));
    a.member_seat = g_new (gint, MAX (masks->len, 1));
    a.visited = g_new (gboolean, MAX (seat_role->len, 1));
    for (i = 0; i < seat_role->len; i++)
        a.seat_member[i] = -1;
    for (i = 0; i < masks->len; i++)
        a.member_seat[i] = -1;
    for (i = 0; i < masks->len && ok; i++)
    {
        memset (a.visited, 0, sizeof (gboolean) * MAX (seat_role->len, 1));
        ok = augment (&a, i);
    }
    if (ok && out_roles != NULL)
    {
        g_array_set_size (out_roles, 0);
        for (i = 0; i < masks->len; i++)
        {
            guint role = g_array_index (seat_role, guint, (guint) a.member_seat[i]);
            g_array_append_val (out_roles, role);
        }
    }
    g_free (a.seat_member);
    g_free (a.member_seat);
    g_free (a.visited);
    return ok;
}

/* the tickets (GList links) of a group grown from @seed */
static GPtrArray *
grow_group (LrgMmoMatchmaker *self, GList *seed, const gchar *mode, GArray *seat_role,
            guint spread, GArray *masks)
{
    GPtrArray *taken = g_ptr_array_new ();
    Ticket *first = seed->data;
    GList *item;
    guint minimum = first->minimum, maximum = first->maximum;
    guint i;

    g_array_set_size (masks, 0);
    for (i = 0; i < first->roles->len; i++)
        g_array_append_val (masks, g_array_index (first->roles, guint, i));
    if (!assign_roles (masks, seat_role, NULL))
    {
        g_array_set_size (masks, 0);
        return taken;
    }
    g_ptr_array_add (taken, seed);
    for (item = seed->next; item != NULL && masks->len < seat_role->len; item = item->next)
    {
        Ticket *ticket = item->data;
        guint low, high, before;
        if (!g_str_equal (ticket->mode, mode) || ticket->members->len > seat_role->len - masks->len)
            continue;
        low = MIN (minimum, ticket->minimum);
        high = MAX (maximum, ticket->maximum);
        if (high - low > spread)
            continue;
        before = masks->len;
        for (i = 0; i < ticket->roles->len; i++)
            g_array_append_val (masks, g_array_index (ticket->roles, guint, i));
        if (!assign_roles (masks, seat_role, NULL))
        {
            g_array_set_size (masks, before);
            continue;
        }
        minimum = low;
        maximum = high;
        g_ptr_array_add (taken, item);
    }
    return taken;
}

/* builds the a(su) result of @links and removes their tickets */
static GVariant *
take_group (LrgMmoMatchmaker *self, GPtrArray *links, GArray *masks, GArray *seat_role)
{
    GVariantBuilder builder;
    g_autoptr(GArray) roles = g_array_new (FALSE, FALSE, sizeof (guint));
    guint i, j, n = 0;

    assign_roles (masks, seat_role, roles);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));
    for (i = 0; i < links->len; i++)
    {
        Ticket *ticket = ((GList *) g_ptr_array_index (links, i))->data;
        for (j = 0; j < ticket->members->len; j++)
            g_variant_builder_add (&builder, "(su)", (const gchar *) g_ptr_array_index (ticket->members, j),
                                   g_array_index (roles, guint, n++));
    }
    for (i = 0; i < links->len; i++)
        drop_ticket (self, g_ptr_array_index (links, i));
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

GVariant *
lrg_mmo_matchmaker_take_roles (LrgMmoMatchmaker *self, const gchar *mode, const guint *slots,
                               guint n_slots, guint spread, guint min_size, gint64 partial_after_us,
                               gint64 now_us, GError **error)
{
    g_autoptr(GArray) seat_role = NULL;
    g_autoptr(GArray) masks = NULL;
    g_autoptr(GPtrArray) oldest = NULL;
    g_autoptr(GArray) oldest_masks = NULL;
    GList *seed;
    guint i, j;

    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    if (!_lrg_mmo_id_valid (mode) || slots == NULL || n_slots == 0 ||
        n_slots > LRG_MMO_MATCHMAKER_MAX_ROLES || min_size == 0 || now_us < self->last_time)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match request");
        return NULL;
    }
    seat_role = g_array_new (FALSE, FALSE, sizeof (guint));
    for (i = 0; i < n_slots; i++)
        for (j = 0; j < slots[i]; j++)
        {
            if (seat_role->len >= MAX_MEMBERS)
            {
                _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match request");
                return NULL;
            }
            g_array_append_val (seat_role, i);
        }
    if (seat_role->len == 0)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match request");
        return NULL;
    }
    self->last_time = now_us;
    expire (self, now_us);

    masks = g_array_new (FALSE, FALSE, sizeof (guint));
    for (seed = self->tickets.head; seed != NULL; seed = seed->next)
    {
        Ticket *first = seed->data;
        g_autoptr(GPtrArray) links = NULL;
        if (!g_str_equal (first->mode, mode) || first->maximum - first->minimum > spread)
            continue;
        links = grow_group (self, seed, mode, seat_role, spread, masks);
        if (links->len == 0)
            continue;
        if (masks->len == seat_role->len)
            return take_group (self, links, masks, seat_role);
        /* the oldest ticket's group, for a start short of members */
        if (oldest == NULL)
        {
            oldest = g_steal_pointer (&links);
            oldest_masks = g_array_copy (masks);
        }
    }
    if (oldest != NULL && oldest_masks->len >= min_size &&
        now_us - ((Ticket *) ((GList *) g_ptr_array_index (oldest, 0))->data)->created >= partial_after_us)
        return take_group (self, oldest, oldest_masks, seat_role);
    return g_variant_ref_sink (g_variant_new_array (G_VARIANT_TYPE ("(su)"), NULL, 0));
}

gboolean
lrg_mmo_matchmaker_is_queued (LrgMmoMatchmaker *self, const gchar *account)
{
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    return find_account (self, account, NULL) != NULL;
}

GPtrArray *
lrg_mmo_matchmaker_get_ticket (LrgMmoMatchmaker *self, const gchar *account, const gchar **mode,
                               gint64 *since_us, guint *roles)
{
    GList *item;
    Ticket *ticket;
    GPtrArray *out;
    guint index, i;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    item = find_account (self, account, &index);
    if (item == NULL)
        return NULL;
    ticket = item->data;
    if (mode != NULL)
        *mode = ticket->mode;
    if (since_us != NULL)
        *since_us = ticket->created;
    if (roles != NULL)
        *roles = g_array_index (ticket->roles, guint, index);
    out = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < ticket->members->len; i++)
        g_ptr_array_add (out, g_strdup (g_ptr_array_index (ticket->members, i)));
    return out;
}

guint
lrg_mmo_matchmaker_get_queued (LrgMmoMatchmaker *self, const gchar *mode)
{
    GList *item;
    guint count = 0;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), 0);
    if (mode == NULL)
        return self->queued;
    for (item = self->tickets.head; item != NULL; item = item->next)
        if (g_str_equal (((Ticket *) item->data)->mode, mode))
            count += ((Ticket *) item->data)->members->len;
    return count;
}

GPtrArray *
lrg_mmo_matchmaker_list_modes (LrgMmoMatchmaker *self)
{
    GList *item;
    GPtrArray *out;
    guint index;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    out = g_ptr_array_new_with_free_func (g_free);
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        const gchar *mode = ((Ticket *) item->data)->mode;
        if (!g_ptr_array_find_with_equal_func (out, mode, g_str_equal, &index))
            g_ptr_array_add (out, g_strdup (mode));
    }
    return out;
}

GPtrArray *
lrg_mmo_matchmaker_expire (LrgMmoMatchmaker *self, gint64 now_us)
{
    GPtrArray *expired;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    expired = g_ptr_array_new_with_free_func (g_free);
    if (now_us < self->last_time)
        return expired;
    self->last_time = now_us;
    expire_into (self, now_us, expired);
    return expired;
}

GVariant *
lrg_mmo_matchmaker_save (LrgMmoMatchmaker *self, gint64 now_us)
{
    GVariantBuilder builder;
    GList *item;
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sxa(suu))"));
    for (item = self->tickets.head; item != NULL; item = item->next)
    {
        Ticket *ticket = item->data;
        GVariantBuilder members;
        guint i;
        g_variant_builder_init (&members, G_VARIANT_TYPE ("a(suu)"));
        for (i = 0; i < ticket->members->len; i++)
            g_variant_builder_add (&members, "(suu)", (const gchar *) g_ptr_array_index (ticket->members, i),
                                   g_array_index (ticket->ratings, guint, i),
                                   g_array_index (ticket->roles, guint, i));
        g_variant_builder_add (&builder, "(sxa(suu))", ticket->mode, MAX (now_us - ticket->created, (gint64) 0),
                               &members);
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
lrg_mmo_matchmaker_load (LrgMmoMatchmaker *self, GVariant *saved, gint64 now_us, GError **error)
{
    GVariantIter iter;
    const gchar *mode;
    gint64 age;
    GVariant *members;
    g_autoptr(GPtrArray) tickets = NULL;
    g_autoptr(GHashTable) seen = NULL;
    guint total = 0, i;

    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), FALSE);
    if (saved == NULL || !g_variant_is_of_type (saved, G_VARIANT_TYPE ("a(sxa(suu))")) || now_us < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid saved queue");
    if (self->tickets.length > 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "The queue is not empty");

    /* --- everything checked into a private list first --- */
    tickets = g_ptr_array_new_with_free_func (ticket_free);
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_variant_iter_init (&iter, saved);
    while (g_variant_iter_next (&iter, "(&sx@a(suu))", &mode, &age, &members))
    {
        Ticket *ticket;
        gint64 created;
        if (age < 0)
        {
            g_variant_unref (members);
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid ticket age");
        }
        created = now_us - age;
        ticket = ticket_build (self, members, TRUE, mode, created, error);
        g_variant_unref (members);
        if (ticket == NULL)
            return FALSE;
        for (i = 0; i < ticket->members->len; i++)
        {
            if (g_hash_table_contains (seen, g_ptr_array_index (ticket->members, i)))
            {
                ticket_free (ticket);
                return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Duplicate saved account");
            }
            g_hash_table_add (seen, g_strdup (g_ptr_array_index (ticket->members, i)));
        }
        total += ticket->members->len;
        if (total > self->capacity)
        {
            ticket_free (ticket);
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Matchmaking queue full");
        }
        /* an age as long as the timeout: the ticket expired while the host was down */
        if (age >= self->timeout)
        {
            ticket_free (ticket);
            continue;
        }
        g_ptr_array_add (tickets, ticket);
    }

    /* --- then queued, oldest first --- */
    self->last_time = MAX (self->last_time, now_us);
    for (i = 0; i < tickets->len; i++)
        insert_ticket (self, g_ptr_array_index (tickets, i));
    g_ptr_array_set_free_func (tickets, NULL);
    return TRUE;
}

gboolean
lrg_mmo_matchmaker_roles_fit (const guint *masks, guint n_masks, const guint *slots, guint n_slots)
{
    g_autoptr(GArray) members = NULL;
    g_autoptr(GArray) seat_role = NULL;
    guint i, j;
    if (masks == NULL || slots == NULL || n_masks == 0 || n_slots == 0 ||
        n_slots > LRG_MMO_MATCHMAKER_MAX_ROLES || n_masks > MAX_MEMBERS)
        return FALSE;
    members = g_array_new (FALSE, FALSE, sizeof (guint));
    seat_role = g_array_new (FALSE, FALSE, sizeof (guint));
    g_array_append_vals (members, masks, n_masks);
    for (i = 0; i < n_slots; i++)
        for (j = 0; j < slots[i] && seat_role->len < MAX_MEMBERS; j++)
            g_array_append_val (seat_role, i);
    return assign_roles (members, seat_role, NULL);
}

/* ------------------------------------------------------------------ */
/* Team matches (PvP)                                                 */

/*
 * The ticket lifetime is the role queues' timeout: one field, two names
 * (the PvP queues' setter raises short values to a second instead of
 * refusing them).
 */
void
lrg_mmo_matchmaker_set_ticket_lifetime (LrgMmoMatchmaker *self, gint64 lifetime_us)
{
    g_return_if_fail (LRG_IS_MMO_MATCHMAKER (self));
    self->timeout = MAX (lifetime_us, G_TIME_SPAN_SECOND);
}

gint64
lrg_mmo_matchmaker_get_ticket_lifetime (LrgMmoMatchmaker *self)
{
    g_return_val_if_fail (LRG_IS_MMO_MATCHMAKER (self), 0);
    return self->timeout;
}

/* the ticket holding @account, or NULL */
static Ticket *
find_ticket (LrgMmoMatchmaker *self, const gchar *account)
{
    GList *item = find_account (self, account, NULL);
    return item != NULL ? item->data : NULL;
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
