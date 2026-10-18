/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * lrg-mmo-holdings.c - deeds, leases and stakes between accounts
 *
 * Records in the store (all GVariant, normal form):
 *
 *   holdings/deed/ID     DEED_TYPE: owner, definition, metadata, the sale
 *                        (price, buyer, expiry) and the lease (state,
 *                        tenant, rent, period, periods, settled, starts,
 *                        paid-until, deposit, arrears, owed, earned)
 *   holdings/venture/ID  VENTURE_TYPE: manager, deed, unit price, units,
 *                        sold, paid, open, holders a(su)
 *   holdings/index/NAME  "as", sorted: owner/A, tenant/A, manager/A,
 *                        investor/A, sales, leases, ventures
 *   market/balance/A/coins  "x": the purse, shared with LrgMmoMarket
 *
 * Every public mutation reads what it needs into a transaction (Tx) that
 * remembers each record's revision, changes the copies, and commits all
 * of them in one LrgMmoStore batch: a record changed by someone else in
 * between makes the commit fail as a whole, so nothing is ever half done.
 * Balances and indexes touched several times in one operation (the
 * seller is also the fee account, the buyer is the tenant) are kept once
 * in the Tx, so the batch never names a key twice.
 */
#include "lrg-mmo-holdings.h"
#include "lrg-mmo-service-private.h"
#include <string.h>

#define DEED_TYPE      "(ssay(xsx)(ysxxuuxxxuxx))"
#define VENTURE_TYPE   "(ssxuuxba(su))"
#define METADATA_LIMIT (65536)
#define SALE_LIMIT     (2592000u)
#define PERIOD_LIMIT   (31536000)
#define TERM_LIMIT     (520u)
#define UNITS_LIMIT    (1000000u)

/* lease states, the first field of the lease tuple */
enum
{
    LEASE_NONE = 0,
    LEASE_OFFERED = 1,
    LEASE_ACTIVE = 2
};

struct _LrgMmoHoldings
{
    GObject      parent_instance;
    LrgMmoStore *store;
    gint64       clock;       /* fixed "now", 0 for the real time */
};

G_DEFINE_TYPE (LrgMmoHoldings, lrg_mmo_holdings, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/* Records                                                            */

typedef struct
{
    gchar   *owner;
    gchar   *definition;
    GBytes  *metadata;
    gint64   sale_price;
    gchar   *sale_buyer;
    gint64   sale_expiry;
    guint8   lease;
    gchar   *tenant;
    gint64   rent;
    gint64   period;
    guint32  periods;
    guint32  settled;
    gint64   starts;
    gint64   paid_until;
    gint64   deposit;
    guint32  arrears;
    gint64   owed;
    gint64   earned;
    guint64  revision;
} Deed;

static void
deed_free (Deed *deed)
{
    if (deed == NULL)
        return;
    g_free (deed->owner);
    g_free (deed->definition);
    g_clear_pointer (&deed->metadata, g_bytes_unref);
    g_free (deed->sale_buyer);
    g_free (deed->tenant);
    g_free (deed);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Deed, deed_free)

static GVariant *
deed_to_variant (const Deed *deed)
{
    return g_variant_new ("(ss@ay(xsx)(ysxxuuxxxuxx))",
                          deed->owner, deed->definition,
                          g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), deed->metadata, TRUE),
                          deed->sale_price, deed->sale_buyer, deed->sale_expiry,
                          deed->lease, deed->tenant, deed->rent, deed->period,
                          deed->periods, deed->settled, deed->starts, deed->paid_until,
                          deed->deposit, deed->arrears, deed->owed, deed->earned);
}

typedef struct
{
    gchar     *manager;
    gchar     *deed;
    gint64     unit_price;
    guint32    units;
    guint32    sold;
    gint64     paid;
    gboolean   open;
    GPtrArray *holders;   /* Holder* */
    guint64    revision;
} Venture;

typedef struct
{
    gchar   *account;
    guint32  units;
} Holder;

static void
holder_free (gpointer data)
{
    Holder *holder = data;
    g_free (holder->account);
    g_free (holder);
}

static void
venture_free (Venture *venture)
{
    if (venture == NULL)
        return;
    g_free (venture->manager);
    g_free (venture->deed);
    g_clear_pointer (&venture->holders, g_ptr_array_unref);
    g_free (venture);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Venture, venture_free)

static GVariant *
venture_to_variant (const Venture *venture)
{
    GVariantBuilder holders;
    guint i;
    g_variant_builder_init (&holders, G_VARIANT_TYPE ("a(su)"));
    for (i = 0; i < venture->holders->len; i++)
    {
        Holder *holder = g_ptr_array_index (venture->holders, i);
        g_variant_builder_add (&holders, "(su)", holder->account, holder->units);
    }
    return g_variant_new ("(ssxuuxba(su))", venture->manager, venture->deed,
                          venture->unit_price, venture->units, venture->sold,
                          venture->paid, venture->open, &holders);
}

/* ------------------------------------------------------------------ */
/* Transactions                                                       */

typedef struct
{
    guint64  revision;
    gint64   value;
    gboolean dirty;
} Balance;

typedef struct
{
    guint64    revision;
    GPtrArray *ids;       /* sorted gchar* */
    gboolean   dirty;
} Index;

static void
index_free (gpointer data)
{
    Index *index = data;
    g_ptr_array_unref (index->ids);
    g_free (index);
}

typedef struct
{
    LrgMmoHoldings  *self;
    GHashTable      *balances;    /* account -> Balance* */
    GHashTable      *indexes;     /* name -> Index* */
    GVariantBuilder  records;     /* other changes, a(stay) */
} Tx;

static void
tx_init (Tx *tx, LrgMmoHoldings *self)
{
    tx->self = self;
    tx->balances = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    tx->indexes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_free);
    g_variant_builder_init (&tx->records, G_VARIANT_TYPE ("a(stay)"));
}

static void
tx_clear (Tx *tx)
{
    g_clear_pointer (&tx->balances, g_hash_table_unref);
    g_clear_pointer (&tx->indexes, g_hash_table_unref);
    g_variant_builder_clear (&tx->records);
}

/* the purse of @account, read once per transaction */
static Balance *
tx_balance (Tx *tx, const gchar *account, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GError) local_error = NULL;
    Balance *balance;

    balance = g_hash_table_lookup (tx->balances, account);
    if (balance != NULL)
        return balance;
    if (!_lrg_mmo_id_valid (account))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid account");
        return NULL;
    }
    balance = g_new0 (Balance, 1);
    key = g_strdup_printf ("market/balance/%s/coins", account);
    value = _lrg_mmo_load (tx->self->store, key, "x", &balance->revision, &local_error);
    if (value != NULL)
        balance->value = g_variant_get_int64 (value);
    else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        balance->revision = 0;
    else
    {
        g_free (balance);
        g_propagate_error (error, g_steal_pointer (&local_error));
        return NULL;
    }
    if (balance->value < 0)
    {
        g_free (balance);
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Negative stored coin balance");
        return NULL;
    }
    g_hash_table_insert (tx->balances, g_strdup (account), balance);
    return balance;
}

/* moves @amount coins from @from to @to; insufficient coins or an
 * overflow fail with G_IO_ERROR_NO_SPACE and change nothing */
static gboolean
tx_move (Tx *tx, const gchar *from, const gchar *to, gint64 amount, GError **error)
{
    Balance *source;
    Balance *target;

    if (amount == 0)
        return TRUE;
    if (amount < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Negative coin movement");
    source = tx_balance (tx, from, error);
    if (source == NULL)
        return FALSE;
    target = tx_balance (tx, to, error);
    if (target == NULL)
        return FALSE;
    if (source == target)
        return TRUE;
    if (source->value < amount)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient coins");
    if (amount > G_MAXINT64 - target->value)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Coin balance would overflow");
    source->value -= amount;
    target->value += amount;
    source->dirty = TRUE;
    target->dirty = TRUE;
    return TRUE;
}

static Index *
tx_index (Tx *tx, const gchar *name, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GError) local_error = NULL;
    Index *index;

    index = g_hash_table_lookup (tx->indexes, name);
    if (index != NULL)
        return index;
    index = g_new0 (Index, 1);
    index->ids = g_ptr_array_new_with_free_func (g_free);
    key = g_strconcat ("holdings/index/", name, NULL);
    value = _lrg_mmo_load (tx->self->store, key, "as", &index->revision, &local_error);
    if (value != NULL)
    {
        GVariantIter iter;
        const gchar *id;
        g_variant_iter_init (&iter, value);
        while (g_variant_iter_next (&iter, "&s", &id))
            g_ptr_array_add (index->ids, g_strdup (id));
    }
    else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        index->revision = 0;
    else
    {
        index_free (index);
        g_propagate_error (error, g_steal_pointer (&local_error));
        return NULL;
    }
    g_hash_table_insert (tx->indexes, g_strdup (name), index);
    return index;
}

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
    return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* adds @id to the sorted index @name (no duplicates) */
static gboolean
tx_index_add (Tx *tx, const gchar *name, const gchar *id, GError **error)
{
    Index *index;
    guint i;

    index = tx_index (tx, name, error);
    if (index == NULL)
        return FALSE;
    for (i = 0; i < index->ids->len; i++)
        if (g_str_equal (g_ptr_array_index (index->ids, i), id))
            return TRUE;
    if (index->ids->len >= LRG_MMO_HOLDINGS_MAX_INDEX)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Holdings index is full");
    g_ptr_array_add (index->ids, g_strdup (id));
    g_ptr_array_sort (index->ids, compare_ids);
    index->dirty = TRUE;
    return TRUE;
}

static gboolean
tx_index_remove (Tx *tx, const gchar *name, const gchar *id, GError **error)
{
    Index *index;
    guint i;

    index = tx_index (tx, name, error);
    if (index == NULL)
        return FALSE;
    for (i = 0; i < index->ids->len; i++)
    {
        if (g_str_equal (g_ptr_array_index (index->ids, i), id))
        {
            g_ptr_array_remove_index (index->ids, i);
            index->dirty = TRUE;
            break;
        }
    }
    return TRUE;
}

/* index names with an account part: "owner/alice" */
static gchar *
account_index (const gchar *kind, const gchar *account)
{
    return g_strconcat (kind, "/", account, NULL);
}

static gboolean
tx_index_add_account (Tx *tx, const gchar *kind, const gchar *account, const gchar *id,
                      GError **error)
{
    g_autofree gchar *name = account_index (kind, account);
    return tx_index_add (tx, name, id, error);
}

static gboolean
tx_index_remove_account (Tx *tx, const gchar *kind, const gchar *account, const gchar *id,
                         GError **error)
{
    g_autofree gchar *name = account_index (kind, account);
    return tx_index_remove (tx, name, id, error);
}

static void
tx_put_deed (Tx *tx, const gchar *id, const Deed *deed)
{
    g_autofree gchar *key = g_strconcat ("holdings/deed/", id, NULL);
    _lrg_mmo_change (&tx->records, key, deed->revision, deed_to_variant (deed));
}

static void
tx_put_venture (Tx *tx, const gchar *id, const Venture *venture)
{
    g_autofree gchar *key = g_strconcat ("holdings/venture/", id, NULL);
    _lrg_mmo_change (&tx->records, key, venture->revision, venture_to_variant (venture));
}

/* commits every change of @tx, with the receipt of @operation (nullable) */
static gboolean
tx_commit (Tx *tx, const gchar *operation, GVariant *intent, GError **error)
{
    g_autoptr(GVariant) batch = NULL;
    GHashTableIter iter;
    gpointer key, value;
    GVariantBuilder *builder = &tx->records;

    g_hash_table_iter_init (&iter, tx->balances);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Balance *balance = value;
        g_autofree gchar *record = NULL;
        if (!balance->dirty)
            continue;
        record = g_strdup_printf ("market/balance/%s/coins", (const gchar *) key);
        _lrg_mmo_change (builder, record, balance->revision, g_variant_new_int64 (balance->value));
    }
    g_hash_table_iter_init (&iter, tx->indexes);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Index *index = value;
        g_autofree gchar *record = NULL;
        if (!index->dirty)
            continue;
        record = g_strconcat ("holdings/index/", (const gchar *) key, NULL);
        _lrg_mmo_change (builder, record, index->revision,
                         g_variant_new_strv ((const gchar * const *) index->ids->pdata,
                                             (gssize) index->ids->len));
    }
    if (operation != NULL)
        _lrg_mmo_operation_add (builder, operation, intent);
    batch = g_variant_ref_sink (g_variant_builder_end (builder));
    /* the builder is spent: give tx_clear() a fresh, empty one */
    g_variant_builder_init (builder, G_VARIANT_TYPE ("a(stay)"));
    return lrg_mmo_store_commit (tx->self->store, batch, error);
}

/* ------------------------------------------------------------------ */
/* Loading                                                            */

static Deed *
deed_load (LrgMmoHoldings *self, const gchar *id, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) metadata = NULL;
    const gchar *owner, *definition, *buyer, *tenant;
    Deed *deed;

    if (!_lrg_mmo_id_valid (id))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid deed ID");
        return NULL;
    }
    key = g_strconcat ("holdings/deed/", id, NULL);
    deed = g_new0 (Deed, 1);
    value = _lrg_mmo_load (self->store, key, DEED_TYPE, &deed->revision, error);
    if (value == NULL)
    {
        g_free (deed);
        return NULL;
    }
    g_variant_get (value, "(&s&s@ay(x&sx)(y&sxxuuxxxuxx))", &owner, &definition, &metadata,
                   &deed->sale_price, &buyer, &deed->sale_expiry,
                   &deed->lease, &tenant, &deed->rent, &deed->period, &deed->periods,
                   &deed->settled, &deed->starts, &deed->paid_until, &deed->deposit,
                   &deed->arrears, &deed->owed, &deed->earned);
    deed->owner = g_strdup (owner);
    deed->definition = g_strdup (definition);
    deed->metadata = g_variant_get_data_as_bytes (metadata);
    deed->sale_buyer = g_strdup (buyer);
    deed->tenant = g_strdup (tenant);
    if (!_lrg_mmo_id_valid (deed->owner) || deed->sale_price < 0 || deed->lease > LEASE_ACTIVE ||
        deed->rent < 0 || deed->deposit < 0 || deed->owed < 0 || deed->earned < 0 ||
        (deed->lease == LEASE_ACTIVE && (!_lrg_mmo_id_valid (deed->tenant) || deed->period <= 0)))
    {
        deed_free (deed);
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Inconsistent stored deed");
        return NULL;
    }
    return deed;
}

static Venture *
venture_load (LrgMmoHoldings *self, const gchar *id, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariantIter) holders = NULL;
    const gchar *manager, *deed, *account;
    guint32 units;
    Venture *venture;

    if (!_lrg_mmo_id_valid (id))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid venture ID");
        return NULL;
    }
    key = g_strconcat ("holdings/venture/", id, NULL);
    venture = g_new0 (Venture, 1);
    value = _lrg_mmo_load (self->store, key, VENTURE_TYPE, &venture->revision, error);
    if (value == NULL)
    {
        g_free (venture);
        return NULL;
    }
    g_variant_get (value, "(&s&sxuuxba(su))", &manager, &deed, &venture->unit_price,
                   &venture->units, &venture->sold, &venture->paid, &venture->open, &holders);
    venture->manager = g_strdup (manager);
    venture->deed = g_strdup (deed);
    venture->holders = g_ptr_array_new_with_free_func (holder_free);
    while (g_variant_iter_next (holders, "(&su)", &account, &units))
    {
        Holder *holder = g_new0 (Holder, 1);
        holder->account = g_strdup (account);
        holder->units = units;
        g_ptr_array_add (venture->holders, holder);
    }
    if (venture->sold > venture->units || venture->unit_price <= 0)
    {
        venture_free (venture);
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Inconsistent stored venture");
        return NULL;
    }
    return venture;
}

/* ------------------------------------------------------------------ */
/* Object                                                             */

static void
lrg_mmo_holdings_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_HOLDINGS (object)->store);
    G_OBJECT_CLASS (lrg_mmo_holdings_parent_class)->dispose (object);
}

static void
lrg_mmo_holdings_class_init (LrgMmoHoldingsClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_holdings_dispose;
}

static void
lrg_mmo_holdings_init (LrgMmoHoldings *self)
{
}

LrgMmoHoldings *
lrg_mmo_holdings_new (LrgMmoStore *store)
{
    LrgMmoHoldings *self;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    self = g_object_new (LRG_TYPE_MMO_HOLDINGS, NULL);
    self->store = g_object_ref (store);
    return self;
}

void
lrg_mmo_holdings_set_clock (LrgMmoHoldings *self, gint64 now)
{
    g_return_if_fail (LRG_IS_MMO_HOLDINGS (self));
    self->clock = MAX (now, 0);
}

gint64
lrg_mmo_holdings_get_time (LrgMmoHoldings *self)
{
    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), 0);
    return self->clock > 0 ? self->clock : g_get_real_time () / G_TIME_SPAN_SECOND;
}

gint64
lrg_mmo_holdings_get_coins (LrgMmoHoldings *self, const gchar *account, GError **error)
{
    Tx tx;
    Balance *balance;
    gint64 coins;
    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), -1);
    tx_init (&tx, self);
    balance = tx_balance (&tx, account, error);
    coins = balance != NULL ? balance->value : -1;
    tx_clear (&tx);
    return coins;
}

/* ------------------------------------------------------------------ */
/* Deeds                                                              */

gboolean
lrg_mmo_holdings_issue (LrgMmoHoldings *self, const gchar *deed_id, const gchar *owner,
                        const gchar *definition, GBytes *metadata, gint64 price,
                        const gchar *treasury, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Deed) existing = NULL;
    g_autoptr(GError) local_error = NULL;
    Deed deed;
    Tx tx;
    gint status;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (deed_id) || !_lrg_mmo_id_valid (owner) ||
        !_lrg_mmo_id_valid (definition) || metadata == NULL ||
        g_bytes_get_size (metadata) > METADATA_LIMIT || price < 0 ||
        (price > 0 && (!_lrg_mmo_id_valid (treasury) || g_str_equal (treasury, owner))))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid deed issue");
    intent = g_variant_ref_sink (g_variant_new ("(sssssx)", "issue", deed_id, owner, definition,
                                                treasury != NULL ? treasury : "", price));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    existing = deed_load (self, deed_id, &local_error);
    if (existing != NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Deed exists already");
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }

    memset (&deed, 0, sizeof (deed));
    deed.owner = (gchar *) owner;
    deed.definition = (gchar *) definition;
    deed.metadata = metadata;
    deed.sale_buyer = (gchar *) "";
    deed.tenant = (gchar *) "";

    tx_init (&tx, self);
    ok = (price == 0 || tx_move (&tx, owner, treasury, price, error)) &&
         tx_index_add_account (&tx, "owner", owner, deed_id, error);
    if (ok)
    {
        tx_put_deed (&tx, deed_id, &deed);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok;
}

GVariant *
lrg_mmo_holdings_get_deed (LrgMmoHoldings *self, const gchar *deed_id, GError **error)
{
    g_autoptr(Deed) deed = NULL;
    GVariantDict dict;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), NULL);
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return NULL;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "owner", "s", deed->owner);
    g_variant_dict_insert (&dict, "definition", "s", deed->definition);
    g_variant_dict_insert_value (&dict, "metadata",
                                 g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), deed->metadata, TRUE));
    g_variant_dict_insert (&dict, "sale-price", "x", deed->sale_price);
    g_variant_dict_insert (&dict, "sale-buyer", "s", deed->sale_buyer);
    g_variant_dict_insert (&dict, "sale-expiry", "x", deed->sale_expiry);
    g_variant_dict_insert (&dict, "lease", "y", deed->lease);
    g_variant_dict_insert (&dict, "tenant", "s", deed->tenant);
    g_variant_dict_insert (&dict, "rent", "x", deed->rent);
    g_variant_dict_insert (&dict, "period", "x", deed->period);
    g_variant_dict_insert (&dict, "periods", "u", deed->periods);
    g_variant_dict_insert (&dict, "settled", "u", deed->settled);
    g_variant_dict_insert (&dict, "starts", "x", deed->starts);
    g_variant_dict_insert (&dict, "paid-until", "x", deed->paid_until);
    g_variant_dict_insert (&dict, "deposit", "x", deed->deposit);
    g_variant_dict_insert (&dict, "arrears", "u", deed->arrears);
    g_variant_dict_insert (&dict, "owed", "x", deed->owed);
    g_variant_dict_insert (&dict, "earned", "x", deed->earned);
    return g_variant_ref_sink (g_variant_dict_end (&dict));
}

GVariant *
lrg_mmo_holdings_list (LrgMmoHoldings *self, const gchar *name, GError **error)
{
    Tx tx;
    Index *index;
    GVariant *ids = NULL;
    const gchar *slash;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), NULL);
    /* "sales", "leases", "ventures", or KIND/ACCOUNT of the four kinds */
    slash = name != NULL ? strchr (name, '/') : NULL;
    if (name == NULL ||
        (slash == NULL && !g_str_equal (name, "sales") && !g_str_equal (name, "leases") &&
         !g_str_equal (name, "ventures")) ||
        (slash != NULL && (!_lrg_mmo_id_valid (slash + 1) ||
                           !(g_str_has_prefix (name, "owner/") || g_str_has_prefix (name, "tenant/") ||
                             g_str_has_prefix (name, "manager/") || g_str_has_prefix (name, "investor/")))))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid holdings index");
        return NULL;
    }
    tx_init (&tx, self);
    index = tx_index (&tx, name, error);
    if (index != NULL)
        ids = g_variant_ref_sink (g_variant_new_strv ((const gchar * const *) index->ids->pdata,
                                                      (gssize) index->ids->len));
    tx_clear (&tx);
    return ids;
}

/* an unexpired sale offer stands */
static gboolean
sale_open (LrgMmoHoldings *self, const Deed *deed)
{
    return deed->sale_price > 0 && deed->sale_expiry > lrg_mmo_holdings_get_time (self);
}

gboolean
lrg_mmo_holdings_offer_sale (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id,
                             gint64 price, const gchar *buyer, guint duration, GError **error)
{
    g_autoptr(Deed) deed = NULL;
    Tx tx;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (buyer != NULL && *buyer == '\0')
        buyer = NULL;
    if (!_lrg_mmo_id_valid (actor) || price <= 0 || duration == 0 || duration > SALE_LIMIT ||
        (buyer != NULL && (!_lrg_mmo_id_valid (buyer) || g_str_equal (buyer, actor))))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid sale offer");
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    if (!g_str_equal (deed->owner, actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed belongs to another account");
    if (sale_open (self, deed))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed is already in escrow for sale");
    deed->sale_price = price;
    g_free (deed->sale_buyer);
    deed->sale_buyer = g_strdup (buyer != NULL ? buyer : "");
    deed->sale_expiry = lrg_mmo_holdings_get_time (self) + duration;

    tx_init (&tx, self);
    ok = tx_index_add (&tx, "sales", deed_id, error);
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    return ok;
}

gboolean
lrg_mmo_holdings_cancel_sale (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id,
                              GError **error)
{
    g_autoptr(Deed) deed = NULL;
    Tx tx;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    if (!g_str_equal (deed->owner, actor) || deed->sale_price == 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "No sale of this owner to withdraw");
    deed->sale_price = 0;
    deed->sale_expiry = 0;
    g_free (deed->sale_buyer);
    deed->sale_buyer = g_strdup ("");

    tx_init (&tx, self);
    ok = tx_index_remove (&tx, "sales", deed_id, error);
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    return ok;
}

/*
 * Ends the active lease of @deed in @tx: the deposit pays the owner what
 * is owed, the rest returns to the tenant, and what the deposit cannot
 * cover is written off.  The lease fields are cleared.
 */
static gboolean
tx_end_lease (Tx *tx, const gchar *deed_id, Deed *deed, GError **error)
{
    gint64 cover;

    if (deed->lease == LEASE_ACTIVE)
    {
        cover = MIN (deed->deposit, deed->owed);
        /* the deposit sits in the deed: it reaches the purses through the
         * tenant's balance, so nobody's coins can go negative */
        if (!g_str_equal (deed->tenant, deed->owner))
        {
            Balance *tenant = tx_balance (tx, deed->tenant, error);
            if (tenant == NULL)
                return FALSE;
            if (deed->deposit > G_MAXINT64 - tenant->value)
                return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Coin balance would overflow");
            tenant->value += deed->deposit;
            tenant->dirty = TRUE;
            if (!tx_move (tx, deed->tenant, deed->owner, cover, error))
                return FALSE;
        }
        else
        {
            Balance *owner = tx_balance (tx, deed->owner, error);
            if (owner == NULL)
                return FALSE;
            if (deed->deposit > G_MAXINT64 - owner->value)
                return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Coin balance would overflow");
            owner->value += deed->deposit;
            owner->dirty = TRUE;
        }
        deed->earned += cover;
        if (!tx_index_remove_account (tx, "tenant", deed->tenant, deed_id, error))
            return FALSE;
    }
    deed->lease = LEASE_NONE;
    g_free (deed->tenant);
    deed->tenant = g_strdup ("");
    deed->rent = 0;
    deed->period = 0;
    deed->periods = 0;
    deed->settled = 0;
    deed->starts = 0;
    deed->paid_until = 0;
    deed->deposit = 0;
    deed->arrears = 0;
    deed->owed = 0;
    return tx_index_remove (tx, "leases", deed_id, error);
}

gboolean
lrg_mmo_holdings_buy (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id, gint64 price,
                      guint cut_permille, const gchar *fee_account, const gchar *operation,
                      GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Deed) deed = NULL;
    g_autofree gchar *seller = NULL;
    gint64 cut;
    Tx tx;
    gint status;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || price <= 0 || cut_permille > 1000 ||
        (cut_permille > 0 && !_lrg_mmo_id_valid (fee_account)))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid purchase");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "buy-deed", actor, deed_id, price));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    if (!sale_open (self, deed) || g_str_equal (deed->owner, actor) ||
        (deed->sale_buyer[0] != '\0' && !g_str_equal (deed->sale_buyer, actor)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed is not for sale to this buyer");
    if (deed->sale_price != price)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "The asking price changed");
    seller = g_strdup (deed->owner);
    /* the cut rounds down, in favour of the seller; no overflow: the
     * per mille factor is at most 1000 and checked before the multiply */
    cut = (price / 1000) * cut_permille + ((price % 1000) * cut_permille) / 1000;

    tx_init (&tx, self);
    ok = tx_move (&tx, actor, seller, price, error) &&
         (cut == 0 || tx_move (&tx, seller, fee_account, cut, error));
    /* a tenant buying its own home: the lease ends, settled with the
     * seller first (the deposit returns to the buyer less what it owed) */
    if (ok && deed->lease == LEASE_ACTIVE && g_str_equal (deed->tenant, actor))
        ok = tx_end_lease (&tx, deed_id, deed, error);
    else if (ok && deed->lease == LEASE_OFFERED && g_str_equal (deed->tenant, actor))
        ok = tx_end_lease (&tx, deed_id, deed, error);
    if (ok)
    {
        g_free (deed->owner);
        deed->owner = g_strdup (actor);
        deed->sale_price = 0;
        deed->sale_expiry = 0;
        g_free (deed->sale_buyer);
        deed->sale_buyer = g_strdup ("");
        ok = tx_index_remove (&tx, "sales", deed_id, error) &&
             tx_index_remove_account (&tx, "owner", seller, deed_id, error) &&
             tx_index_add_account (&tx, "owner", actor, deed_id, error);
    }
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Leases                                                             */

gboolean
lrg_mmo_holdings_offer_lease (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id,
                              const gchar *tenant, gint64 rent, gint64 period, guint periods,
                              gint64 deposit, GError **error)
{
    g_autoptr(Deed) deed = NULL;
    Tx tx;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (tenant != NULL && *tenant == '\0')
        tenant = NULL;
    if (!_lrg_mmo_id_valid (actor) || rent <= 0 || period <= 0 || period > PERIOD_LIMIT ||
        periods == 0 || periods > TERM_LIMIT || deposit < 0 ||
        (tenant != NULL && (!_lrg_mmo_id_valid (tenant) || g_str_equal (tenant, actor))))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lease offer");
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    if (!g_str_equal (deed->owner, actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed belongs to another account");
    if (deed->lease == LEASE_ACTIVE)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed is leased already");
    deed->lease = LEASE_OFFERED;
    g_free (deed->tenant);
    deed->tenant = g_strdup (tenant != NULL ? tenant : "");
    deed->rent = rent;
    deed->period = period;
    deed->periods = periods;
    deed->settled = 0;
    deed->starts = 0;
    deed->paid_until = 0;
    deed->deposit = deposit;
    deed->arrears = 0;
    deed->owed = 0;

    tx_init (&tx, self);
    ok = tx_index_add (&tx, "leases", deed_id, error);
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    return ok;
}

gboolean
lrg_mmo_holdings_take_lease (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id,
                             gint64 rent, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Deed) deed = NULL;
    Balance *balance;
    gint64 now;
    Tx tx;
    gint status;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || rent <= 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lease");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "lease", actor, deed_id, rent));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    if (deed->lease != LEASE_OFFERED || g_str_equal (deed->owner, actor) ||
        (deed->tenant[0] != '\0' && !g_str_equal (deed->tenant, actor)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed is not offered to this tenant");
    if (deed->rent != rent)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "The rent changed");
    now = lrg_mmo_holdings_get_time (self);

    tx_init (&tx, self);
    /* the first period's rent to the owner, the deposit into the deed */
    ok = tx_move (&tx, actor, deed->owner, deed->rent, error);
    if (ok)
    {
        balance = tx_balance (&tx, actor, error);
        ok = balance != NULL;
        if (ok && balance->value < deed->deposit)
            ok = _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient coins for the deposit");
        if (ok)
        {
            balance->value -= deed->deposit;
            balance->dirty = TRUE;
        }
    }
    if (ok)
    {
        deed->lease = LEASE_ACTIVE;
        g_free (deed->tenant);
        deed->tenant = g_strdup (actor);
        deed->settled = 1;
        deed->starts = now;
        deed->paid_until = now + deed->period;
        deed->arrears = 0;
        deed->owed = 0;
        deed->earned += deed->rent;
        ok = tx_index_add_account (&tx, "tenant", actor, deed_id, error);
    }
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok;
}

gint
lrg_mmo_holdings_collect (LrgMmoHoldings *self, const gchar *deed_id, guint max_arrears,
                          GError **error)
{
    g_autoptr(Deed) deed = NULL;
    Balance *tenant;
    gint64 now, due, paid;
    gint settled = 0;
    Tx tx;
    gboolean ok = TRUE;
    gboolean changed = FALSE;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), -1);
    if (max_arrears == 0)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Arrears limit must be positive");
        return -1;
    }
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return -1;
    if (deed->lease != LEASE_ACTIVE)
        return 0;
    now = lrg_mmo_holdings_get_time (self);

    tx_init (&tx, self);
    /* each due period: the tenant pays what it owes, as far as it can */
    while (ok && settled < LRG_MMO_HOLDINGS_MAX_PERIODS && deed->settled < deed->periods &&
           deed->paid_until <= now)
    {
        tenant = tx_balance (&tx, deed->tenant, error);
        if (tenant == NULL)
        {
            ok = FALSE;
            break;
        }
        due = deed->owed + deed->rent;
        if (due < deed->owed)
        {
            ok = _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Owed rent would overflow");
            break;
        }
        paid = MIN (tenant->value, due);
        ok = tx_move (&tx, deed->tenant, deed->owner, paid, error);
        if (!ok)
            break;
        deed->earned += paid;
        deed->owed = due - paid;
        deed->arrears = deed->owed > 0 ? deed->arrears + 1 : 0;
        deed->settled++;
        deed->paid_until += deed->period;
        settled++;
        changed = TRUE;
        if (deed->arrears >= max_arrears)
        {
            /* evicted: the deposit pays the owner what it can */
            ok = tx_end_lease (&tx, deed_id, deed, error);
            break;
        }
    }
    /* the end of the term: the deposit returns less anything owed */
    if (ok && deed->lease == LEASE_ACTIVE && deed->settled >= deed->periods &&
        deed->paid_until <= now)
    {
        ok = tx_end_lease (&tx, deed_id, deed, error);
        changed = TRUE;
    }
    if (ok && changed)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    return ok ? settled : -1;
}

gboolean
lrg_mmo_holdings_end_lease (LrgMmoHoldings *self, const gchar *actor, const gchar *deed_id,
                            GError **error)
{
    g_autoptr(Deed) deed = NULL;
    gboolean is_owner, is_tenant;
    Tx tx;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    deed = deed_load (self, deed_id, error);
    if (deed == NULL)
        return FALSE;
    is_owner = g_str_equal (deed->owner, actor);
    is_tenant = deed->lease == LEASE_ACTIVE && g_str_equal (deed->tenant, actor);
    if (deed->lease == LEASE_NONE ||
        (deed->lease == LEASE_OFFERED && !is_owner) ||
        (deed->lease == LEASE_ACTIVE && !is_tenant && !(is_owner && deed->owed > 0)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "This account cannot end the lease");

    tx_init (&tx, self);
    ok = tx_end_lease (&tx, deed_id, deed, error);
    if (ok)
    {
        tx_put_deed (&tx, deed_id, deed);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Ventures                                                           */

gboolean
lrg_mmo_holdings_open_venture (LrgMmoHoldings *self, const gchar *actor, const gchar *venture_id,
                               const gchar *deed_id, gint64 unit_price, guint units, GError **error)
{
    g_autoptr(GError) local_error = NULL;
    g_autoptr(Venture) existing = NULL;
    Venture venture;
    Tx tx;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (deed_id != NULL && *deed_id == '\0')
        deed_id = NULL;
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (venture_id) || unit_price <= 0 ||
        units == 0 || units > UNITS_LIMIT || unit_price > G_MAXINT64 / (gint64) units)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid venture");
    if (deed_id != NULL)
    {
        g_autoptr(Deed) deed = deed_load (self, deed_id, error);
        if (deed == NULL)
            return FALSE;
        if (!g_str_equal (deed->owner, actor))
            return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Deed belongs to another account");
    }
    existing = venture_load (self, venture_id, &local_error);
    if (existing != NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Venture exists already");
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    memset (&venture, 0, sizeof (venture));
    venture.manager = (gchar *) actor;
    venture.deed = (gchar *) (deed_id != NULL ? deed_id : "");
    venture.unit_price = unit_price;
    venture.units = units;
    venture.open = TRUE;
    venture.holders = g_ptr_array_new_with_free_func (holder_free);

    tx_init (&tx, self);
    ok = tx_index_add (&tx, "ventures", venture_id, error) &&
         tx_index_add_account (&tx, "manager", actor, venture_id, error);
    if (ok)
    {
        tx_put_venture (&tx, venture_id, &venture);
        ok = tx_commit (&tx, NULL, NULL, error);
    }
    tx_clear (&tx);
    g_ptr_array_unref (venture.holders);
    return ok;
}

GVariant *
lrg_mmo_holdings_get_venture (LrgMmoHoldings *self, const gchar *venture_id, GError **error)
{
    g_autoptr(Venture) venture = NULL;
    GVariantDict dict;
    GVariantBuilder holders;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), NULL);
    venture = venture_load (self, venture_id, error);
    if (venture == NULL)
        return NULL;
    g_variant_builder_init (&holders, G_VARIANT_TYPE ("a(su)"));
    for (i = 0; i < venture->holders->len; i++)
    {
        Holder *holder = g_ptr_array_index (venture->holders, i);
        g_variant_builder_add (&holders, "(su)", holder->account, holder->units);
    }
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "manager", "s", venture->manager);
    g_variant_dict_insert (&dict, "deed", "s", venture->deed);
    g_variant_dict_insert (&dict, "unit-price", "x", venture->unit_price);
    g_variant_dict_insert (&dict, "units", "u", venture->units);
    g_variant_dict_insert (&dict, "sold", "u", venture->sold);
    g_variant_dict_insert (&dict, "paid", "x", venture->paid);
    g_variant_dict_insert (&dict, "open", "b", venture->open);
    g_variant_dict_insert_value (&dict, "holders", g_variant_builder_end (&holders));
    return g_variant_ref_sink (g_variant_dict_end (&dict));
}

gboolean
lrg_mmo_holdings_invest (LrgMmoHoldings *self, const gchar *actor, const gchar *venture_id,
                         guint units, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Venture) venture = NULL;
    Holder *holder = NULL;
    gint64 cost;
    guint i;
    Tx tx;
    gint status;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || units == 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid investment");
    intent = g_variant_ref_sink (g_variant_new ("(sssu)", "invest", actor, venture_id, units));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    venture = venture_load (self, venture_id, error);
    if (venture == NULL)
        return FALSE;
    if (!venture->open || g_str_equal (venture->manager, actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Venture is not open to this investor");
    if (units > venture->units - venture->sold)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Not that many units are left");
    for (i = 0; i < venture->holders->len; i++)
    {
        Holder *candidate = g_ptr_array_index (venture->holders, i);
        if (g_str_equal (candidate->account, actor))
            holder = candidate;
    }
    if (holder == NULL && venture->holders->len >= LRG_MMO_HOLDINGS_MAX_HOLDERS)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Venture has its most holders");
    /* unit_price * units fits: open_venture checked price * all units */
    cost = venture->unit_price * (gint64) units;

    tx_init (&tx, self);
    ok = tx_move (&tx, actor, venture->manager, cost, error) &&
         tx_index_add_account (&tx, "investor", actor, venture_id, error);
    if (ok)
    {
        if (holder == NULL)
        {
            holder = g_new0 (Holder, 1);
            holder->account = g_strdup (actor);
            g_ptr_array_add (venture->holders, holder);
        }
        holder->units += units;
        venture->sold += units;
        tx_put_venture (&tx, venture_id, venture);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok;
}

gint64
lrg_mmo_holdings_pay_dividend (LrgMmoHoldings *self, const gchar *actor, const gchar *venture_id,
                               gint64 amount, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Venture) venture = NULL;
    gint64 total = 0;
    guint i;
    Tx tx;
    gint status;
    gboolean ok = TRUE;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), -1);
    if (!_lrg_mmo_id_valid (actor) || amount <= 0)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid dividend");
        return -1;
    }
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "dividend", actor, venture_id, amount));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status < 0)
        return -1;
    venture = venture_load (self, venture_id, error);
    if (venture == NULL)
        return -1;
    if (status > 0)
        return 0;
    if (!venture->open || !g_str_equal (venture->manager, actor))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Only the manager pays an open venture's dividend");
        return -1;
    }
    if (venture->sold == 0)
        return 0;

    tx_init (&tx, self);
    /* each holder's share, floor(amount * units / sold) without overflow */
    for (i = 0; ok && i < venture->holders->len; i++)
    {
        Holder *holder = g_ptr_array_index (venture->holders, i);
        gint64 share = (amount / venture->sold) * holder->units +
                       ((amount % venture->sold) * (gint64) holder->units) / venture->sold;
        ok = tx_move (&tx, actor, holder->account, share, error);
        total += share;
    }
    if (ok && total > 0)
    {
        venture->paid += total;
        tx_put_venture (&tx, venture_id, venture);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok ? total : -1;
}

gboolean
lrg_mmo_holdings_close_venture (LrgMmoHoldings *self, const gchar *actor, const gchar *venture_id,
                                gint64 buyback, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(Venture) venture = NULL;
    guint i;
    Tx tx;
    gint status;
    gboolean ok = TRUE;

    g_return_val_if_fail (LRG_IS_MMO_HOLDINGS (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || buyback < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid venture closing");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "close", actor, venture_id, buyback));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    venture = venture_load (self, venture_id, error);
    if (venture == NULL)
        return FALSE;
    if (!venture->open || !g_str_equal (venture->manager, actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Only the manager closes an open venture");
    if (buyback > 0 && buyback > G_MAXINT64 / (gint64) MAX (venture->sold, 1u))
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Buyback would overflow");

    tx_init (&tx, self);
    for (i = 0; ok && i < venture->holders->len; i++)
    {
        Holder *holder = g_ptr_array_index (venture->holders, i);
        ok = tx_move (&tx, actor, holder->account, buyback * (gint64) holder->units, error);
    }
    if (ok)
    {
        venture->open = FALSE;
        ok = tx_index_remove (&tx, "ventures", venture_id, error);
    }
    if (ok)
    {
        tx_put_venture (&tx, venture_id, venture);
        ok = tx_commit (&tx, operation, intent, error);
    }
    tx_clear (&tx);
    return ok;
}
