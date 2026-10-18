/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-market.h"
#include "lrg-mmo-service-private.h"

struct _LrgMmoMarket
{
    GObject parent_instance;
    LrgMmoStore *store;
    LrgMmoMarketClock clock;        /* NULL: the real clock */
    gpointer clock_data;
    GDestroyNotify clock_notify;
};
G_DEFINE_TYPE (LrgMmoMarket, lrg_mmo_market, G_TYPE_OBJECT)
static void
lrg_mmo_market_dispose (GObject *object)
{
    LrgMmoMarket *self = LRG_MMO_MARKET (object);
    if (self->clock_notify != NULL)
        self->clock_notify (self->clock_data);
    self->clock = NULL;
    self->clock_data = NULL;
    self->clock_notify = NULL;
    g_clear_object (&self->store);
    G_OBJECT_CLASS (lrg_mmo_market_parent_class)->dispose (object);
}
static void
lrg_mmo_market_class_init (LrgMmoMarketClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_market_dispose;
}
static void
lrg_mmo_market_init (LrgMmoMarket *self)
{
}
LrgMmoMarket *
lrg_mmo_market_new (LrgMmoStore *store)
{
    LrgMmoMarket *self;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    self = g_object_new (LRG_TYPE_MMO_MARKET, NULL);
    self->store = g_object_ref (store);
    return self;
}

void
lrg_mmo_market_set_clock (LrgMmoMarket *self, LrgMmoMarketClock clock,
                          gpointer user_data, GDestroyNotify notify)
{
    g_return_if_fail (LRG_IS_MMO_MARKET (self));
    if (self->clock_notify != NULL)
        self->clock_notify (self->clock_data);
    self->clock = clock;
    self->clock_data = user_data;
    self->clock_notify = notify;
}

gint64
lrg_mmo_market_now (LrgMmoMarket *self)
{
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), 0);
    if (self->clock != NULL)
        return self->clock (self->clock_data);
    return g_get_real_time () / G_TIME_SPAN_SECOND;
}

static gchar *
balance_key (const gchar *account, const gchar *resource)
{
    return g_strdup_printf ("market/balance/%s/%s", account, resource);
}

static gint64
balance (LrgMmoMarket *self, const gchar *account, const gchar *resource,
         guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GError) local_error = NULL;
    gint64 quantity;
    if (!_lrg_mmo_id_valid (account) || !_lrg_mmo_id_valid (resource))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid account or resource");
        return -1;
    }
    key = balance_key (account, resource);
    value = _lrg_mmo_load (self->store, key, "x", revision, &local_error);
    if (value == NULL)
    {
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return 0;
        g_propagate_error (error, g_steal_pointer (&local_error));
        return -1;
    }
    quantity = g_variant_get_int64 (value);
    if (quantity < 0)
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Negative stored resource balance");
    return quantity;
}

static void
change_balance (GVariantBuilder *batch, const gchar *account, const gchar *resource,
                guint64 revision, gint64 quantity)
{
    g_autofree gchar *key = balance_key (account, resource);
    _lrg_mmo_change (batch, key, revision, g_variant_new_int64 (quantity));
}

static gboolean
commit (LrgMmoMarket *self, GVariantBuilder *builder, GError **error)
{
    g_autoptr(GVariant) batch = g_variant_ref_sink (g_variant_builder_end (builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}

gint64
lrg_mmo_market_get_balance (LrgMmoMarket *self, const gchar *account, const gchar *resource, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), -1);
    return balance (self, account, resource, &revision, error);
}

gboolean
lrg_mmo_market_grant (LrgMmoMarket *self, const gchar *account, const gchar *resource,
                      gint64 quantity, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    GVariantBuilder builder;
    guint64 revision;
    gint64 current;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (account) || !_lrg_mmo_id_valid (resource) || quantity <= 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid reward");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "grant", account, resource, quantity));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    current = balance (self, account, resource, &revision, error);
    if (current < 0)
        return FALSE;
    if (quantity > G_MAXINT64 - current)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Resource balance would overflow");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, account, resource, revision, current + quantity);
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_transfer (LrgMmoMarket *self, const gchar *actor, const gchar *recipient,
                         const gchar *resource, gint64 quantity, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    GVariantBuilder builder;
    guint64 from_revision, to_revision;
    gint64 from, to;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (recipient) ||
        !_lrg_mmo_id_valid (resource) || quantity <= 0 || g_str_equal (actor, recipient))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid transfer");
    intent = g_variant_ref_sink (g_variant_new ("(ssssx)", "transfer", actor, recipient, resource, quantity));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    from = balance (self, actor, resource, &from_revision, error);
    if (from < 0)
        return FALSE;
    to = balance (self, recipient, resource, &to_revision, error);
    if (to < 0)
        return FALSE;
    if (from < quantity || quantity > G_MAXINT64 - to)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient resources or recipient overflow");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, resource, from_revision, from - quantity);
    change_balance (&builder, recipient, resource, to_revision, to + quantity);
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_list (LrgMmoMarket *self, const gchar *actor, const gchar *listing,
                     const gchar *resource, gint64 quantity, gint64 price, guint duration, GError **error)
{
    g_autofree gchar *key = NULL;
    GVariantBuilder builder;
    gint64 current;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (listing) || !_lrg_mmo_id_valid (resource) ||
        g_str_equal (resource, "coins") || quantity <= 0 || price <= 0 || duration == 0 || duration > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid listing");
    current = balance (self, actor, resource, &revision, error);
    if (current < 0)
        return FALSE;
    if (current < quantity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient items for escrow");
    key = g_strconcat ("market/listing/", listing, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, resource, revision, current - quantity);
    _lrg_mmo_change (&builder, key, 0, g_variant_new ("(ssxxxb)", actor, resource, quantity, price,
                     lrg_mmo_market_now (self) + duration, FALSE));
    return commit (self, &builder, error);
}

static GVariant *
listing_load (LrgMmoMarket *self, const gchar *listing, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (listing))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid listing ID");
        return NULL;
    }
    key = g_strconcat ("market/listing/", listing, NULL);
    return _lrg_mmo_load (self->store, key, "(ssxxxb)", revision, error);
}

GVariant *
lrg_mmo_market_inspect (LrgMmoMarket *self, const gchar *listing, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), NULL);
    return listing_load (self, listing, &revision, error);
}

gboolean
lrg_mmo_market_buy (LrgMmoMarket *self, const gchar *actor, const gchar *listing,
                    const gchar *operation, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) intent = NULL;
    g_autofree gchar *key = NULL;
    const gchar *seller, *resource;
    gint64 quantity, price, expiry, funds, proceeds, items;
    guint64 revision, funds_revision, proceeds_revision, items_revision;
    gboolean closed;
    gint status;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (listing))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid purchase");
    intent = g_variant_ref_sink (g_variant_new ("(sss)", "buy", actor, listing));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    value = listing_load (self, listing, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&sxxxb)", &seller, &resource, &quantity, &price, &expiry, &closed);
    if (closed || quantity <= 0 || price <= 0 || expiry <= lrg_mmo_market_now (self) ||
        g_str_equal (seller, actor) || g_str_equal (resource, "coins"))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Listing is unavailable to this buyer");
    funds = balance (self, actor, "coins", &funds_revision, error);
    if (funds < 0)
        return FALSE;
    proceeds = balance (self, seller, "coins", &proceeds_revision, error);
    if (proceeds < 0)
        return FALSE;
    items = balance (self, actor, resource, &items_revision, error);
    if (items < 0)
        return FALSE;
    if (funds < price || price > G_MAXINT64 - proceeds || quantity > G_MAXINT64 - items)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient funds or settlement overflow");
    key = g_strconcat ("market/listing/", listing, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, "coins", funds_revision, funds - price);
    change_balance (&builder, seller, "coins", proceeds_revision, proceeds + price);
    change_balance (&builder, actor, resource, items_revision, items + quantity);
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(ssxxxb)", seller, resource, quantity, price, expiry, TRUE));
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_cancel (LrgMmoMarket *self, const gchar *actor, const gchar *listing, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autofree gchar *key = NULL;
    const gchar *seller, *resource;
    gint64 quantity, price, expiry, items;
    guint64 revision, items_revision;
    gboolean closed;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    value = listing_load (self, listing, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&sxxxb)", &seller, &resource, &quantity, &price, &expiry, &closed);
    if (closed || !g_str_equal (actor, seller) || quantity <= 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Listing cannot be cancelled by this actor");
    items = balance (self, seller, resource, &items_revision, error);
    if (items < 0)
        return FALSE;
    if (quantity > G_MAXINT64 - items)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Escrow return would overflow");
    key = g_strconcat ("market/listing/", listing, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, seller, resource, items_revision, items + quantity);
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(ssxxxb)", seller, resource, quantity, price, expiry, TRUE));
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_offer (LrgMmoMarket *self, const gchar *actor, const gchar *recipient,
                       const gchar *offer, const gchar *give_resource, gint64 give_quantity,
                       const gchar *take_resource, gint64 take_quantity, guint duration, GError **error)
{
    g_autofree gchar *key = NULL;
    guint64 revision;
    gint64 available;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (recipient) || !_lrg_mmo_id_valid (offer) ||
        !_lrg_mmo_id_valid (give_resource) || !_lrg_mmo_id_valid (take_resource) ||
        g_str_equal (actor, recipient) || g_str_equal (give_resource, take_resource) ||
        give_quantity <= 0 || take_quantity < 0 || duration == 0 || duration > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid trade or attachment offer");
    available = balance (self, actor, give_resource, &revision, error);
    if (available < 0)
        return FALSE;
    if (available < give_quantity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient escrow balance");
    key = g_strconcat ("market/offer/", offer, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, give_resource, revision, available - give_quantity);
    _lrg_mmo_change (&builder, key, 0, g_variant_new ("(ssssxxxb)", actor, recipient, give_resource,
                     take_resource, give_quantity, take_quantity, lrg_mmo_market_now (self) + duration, FALSE));
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_accept_offer (LrgMmoMarket *self, const gchar *actor, const gchar *offer,
                              const gchar *operation, gboolean cancel, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL, intent = NULL;
    const gchar *seller, *buyer, *give, *take;
    gint64 give_count, take_count, expiry, received, paid = 0, proceeds = 0;
    guint64 revision, receive_revision, pay_revision = 0, proceeds_revision = 0;
    gboolean closed;
    gint status;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (offer))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid trade actor or offer");
    intent = g_variant_ref_sink (g_variant_new ("(sssb)", "offer", actor, offer, cancel));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    key = g_strconcat ("market/offer/", offer, NULL);
    value = _lrg_mmo_load (self->store, key, "(ssssxxxb)", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&s&sxxxb)", &seller, &buyer, &give, &take, &give_count, &take_count, &expiry, &closed);
    if (closed || give_count <= 0 || take_count < 0 || g_str_equal (give, take) ||
        !g_str_equal (actor, cancel ? seller : buyer) ||
        (!cancel && expiry <= lrg_mmo_market_now (self)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Offer unavailable to this actor");
    received = balance (self, actor, give, &receive_revision, error);
    if (received < 0)
        return FALSE;
    if (give_count > G_MAXINT64 - received)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Offer receipt overflow");
    if (!cancel && take_count > 0)
    {
        paid = balance (self, buyer, take, &pay_revision, error);
        if (paid < 0)
            return FALSE;
        proceeds = balance (self, seller, take, &proceeds_revision, error);
        if (proceeds < 0)
            return FALSE;
        if (paid < take_count || take_count > G_MAXINT64 - proceeds)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Offer funding or settlement overflow");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, give, receive_revision, received + give_count);
    if (!cancel && take_count > 0)
    {
        change_balance (&builder, buyer, take, pay_revision, paid - take_count);
        change_balance (&builder, seller, take, proceeds_revision, proceeds + take_count);
    }
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(ssssxxxb)", seller, buyer, give, take,
                                                           give_count, take_count, expiry, TRUE));
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_auction (LrgMmoMarket *self, const gchar *actor, const gchar *auction,
                         const gchar *resource, gint64 quantity, gint64 minimum,
                         guint duration, GError **error)
{
    g_autofree gchar *key = NULL;
    gint64 available;
    guint64 revision;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (auction) || !_lrg_mmo_id_valid (resource) ||
        g_str_equal (resource, "coins") || quantity <= 0 || minimum <= 0 || duration == 0 || duration > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid auction");
    available = balance (self, actor, resource, &revision, error);
    if (available < 0)
        return FALSE;
    if (available < quantity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient auction escrow");
    key = g_strconcat ("market/auction/", auction, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, resource, revision, available - quantity);
    _lrg_mmo_change (&builder, key, 0, g_variant_new ("(sssxxxxb)", actor, resource, "", quantity,
                         minimum, (gint64) 0, lrg_mmo_market_now (self) + duration, FALSE));
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_bid (LrgMmoMarket *self, const gchar *actor, const gchar *auction,
                     gint64 amount, const gchar *operation, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL, intent = NULL;
    const gchar *seller, *resource, *bidder;
    gint64 quantity, minimum, old_bid, expiry, funds, refund = 0, debit;
    guint64 revision, funds_revision, refund_revision = 0;
    gboolean closed;
    gint status;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (auction) || amount <= 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid bid");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "bid", actor, auction, amount));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    key = g_strconcat ("market/auction/", auction, NULL);
    value = _lrg_mmo_load (self->store, key, "(sssxxxxb)", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&sxxxxb)", &seller, &resource, &bidder, &quantity, &minimum, &old_bid, &expiry, &closed);
    if (closed || expiry <= lrg_mmo_market_now (self) || g_str_equal (seller, actor) ||
        old_bid < 0 || amount < minimum || amount <= old_bid)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Bid is not eligible");
    funds = balance (self, actor, "coins", &funds_revision, error);
    if (funds < 0)
        return FALSE;
    debit = g_str_equal (actor, bidder) ? amount - old_bid : amount;
    if (funds < debit)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient bid funds");
    if (*bidder != '\0' && !g_str_equal (actor, bidder))
    {
        refund = balance (self, bidder, "coins", &refund_revision, error);
        if (refund < 0)
            return FALSE;
        if (old_bid > G_MAXINT64 - refund)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Refund balance overflow");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, "coins", funds_revision, funds - debit);
    if (*bidder != '\0' && !g_str_equal (actor, bidder))
        change_balance (&builder, bidder, "coins", refund_revision, refund + old_bid);
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(sssxxxxb)", seller, resource, actor,
                                                           quantity, minimum, amount, expiry, FALSE));
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_settle (LrgMmoMarket *self, const gchar *auction, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    const gchar *seller, *resource, *bidder, *recipient;
    gint64 quantity, minimum, bid, expiry, items, proceeds = 0;
    guint64 revision, items_revision, proceeds_revision = 0;
    gboolean closed;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (auction))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid auction");
    key = g_strconcat ("market/auction/", auction, NULL);
    value = _lrg_mmo_load (self->store, key, "(sssxxxxb)", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&sxxxxb)", &seller, &resource, &bidder, &quantity, &minimum, &bid, &expiry, &closed);
    if (closed)
        return TRUE;
    if (expiry > lrg_mmo_market_now (self) || quantity <= 0 || bid < 0 || g_str_equal (resource, "coins"))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Auction not ready for settlement");
    recipient = *bidder != '\0' ? bidder : seller;
    items = balance (self, recipient, resource, &items_revision, error);
    if (items < 0)
        return FALSE;
    if (quantity > G_MAXINT64 - items)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Auction item receipt overflow");
    if (*bidder != '\0')
    {
        proceeds = balance (self, seller, "coins", &proceeds_revision, error);
        if (proceeds < 0)
            return FALSE;
        if (bid > G_MAXINT64 - proceeds)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Auction proceeds overflow");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, recipient, resource, items_revision, items + quantity);
    if (*bidder != '\0')
        change_balance (&builder, seller, "coins", proceeds_revision, proceeds + bid);
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(sssxxxxb)", seller, resource, bidder,
                                                           quantity, minimum, bid, expiry, TRUE));
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_mint_item (LrgMmoMarket *self, const gchar *item, const gchar *owner,
                          const gchar *definition, GBytes *metadata, GError **error)
{
    g_autofree gchar *key = NULL;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (item) || !_lrg_mmo_id_valid (owner) || !_lrg_mmo_id_valid (definition) ||
        metadata == NULL || g_bytes_get_size (metadata) > 65536)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item");
    key = g_strconcat ("market/item/", item, NULL);
    return _lrg_mmo_put (self->store, key, 0, g_variant_new ("(ss@ay)", owner, definition,
                          g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), metadata, TRUE)), error);
}

GVariant *
lrg_mmo_market_get_item (LrgMmoMarket *self, const gchar *item, GError **error)
{
    g_autofree gchar *key = NULL;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), NULL);
    if (!_lrg_mmo_id_valid (item))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item ID");
        return NULL;
    }
    key = g_strconcat ("market/item/", item, NULL);
    return _lrg_mmo_load (self->store, key, "(ssay)", &revision, error);
}

gboolean
lrg_mmo_market_transfer_item (LrgMmoMarket *self, const gchar *actor, const gchar *recipient,
                              const gchar *item, const gchar *operation, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL, intent = NULL, metadata = NULL;
    const gchar *owner, *definition;
    guint64 revision;
    gint status;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (recipient) || !_lrg_mmo_id_valid (item) ||
        g_str_equal (actor, recipient))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item transfer");
    intent = g_variant_ref_sink (g_variant_new ("(ssss)", "item", actor, recipient, item));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    key = g_strconcat ("market/item/", item, NULL);
    value = _lrg_mmo_load (self->store, key, "(ssay)", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s@ay)", &owner, &definition, &metadata);
    if (!g_str_equal (actor, owner))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Unique item belongs to another account");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(ss@ay)", recipient, definition, metadata));
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

/* ------------------------------------------------------------------ */
/* Withdrawals: coins and unique items leaving the market             */

gboolean
lrg_mmo_market_revoke (LrgMmoMarket *self, const gchar *account, const gchar *resource,
                       gint64 quantity, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    GVariantBuilder builder;
    guint64 revision;
    gint64 current;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (account) || !_lrg_mmo_id_valid (resource) || quantity <= 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid withdrawal");
    intent = g_variant_ref_sink (g_variant_new ("(sssx)", "revoke", account, resource, quantity));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    current = balance (self, account, resource, &revision, error);
    if (current < 0)
        return FALSE;
    if (current < quantity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient resources to withdraw");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, account, resource, revision, current - quantity);
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_has_operation (LrgMmoMarket *self, const gchar *operation, gboolean *applied,
                              GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) receipt = NULL;
    g_autoptr(GError) local_error = NULL;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    g_return_val_if_fail (applied != NULL, FALSE);
    *applied = FALSE;
    if (operation == NULL || !_lrg_mmo_id_valid (operation))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid durable operation ID");
    /* the receipt _lrg_mmo_operation_add() commits with every operation */
    key = g_strconcat ("service/operation/", operation, NULL);
    receipt = _lrg_mmo_load (self->store, key, "s", &revision, &local_error);
    if (receipt != NULL)
    {
        *applied = TRUE;
        return TRUE;
    }
    if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        return TRUE;
    g_propagate_error (error, g_steal_pointer (&local_error));
    return FALSE;
}

static gchar *
item_key (const gchar *item)
{
    return g_strconcat ("market/item/", item, NULL);
}

/* loads a unique item: owner and definition borrowed from the result */
static GVariant *
item_load (LrgMmoMarket *self, const gchar *item, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (item))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item ID");
        return NULL;
    }
    key = item_key (item);
    return _lrg_mmo_load (self->store, key, "(ssay)", revision, error);
}

/* rewrites a unique item record with a new owner, keeping its metadata */
static void
item_change_owner (GVariantBuilder *batch, const gchar *item, guint64 revision,
                   GVariant *record, const gchar *owner)
{
    g_autofree gchar *key = item_key (item);
    g_autoptr(GVariant) metadata = NULL;
    const gchar *definition;
    g_variant_get (record, "(&s&s@ay)", NULL, &definition, &metadata);
    _lrg_mmo_change (batch, key, revision, g_variant_new ("(ss@ay)", owner, definition, metadata));
}

gboolean
lrg_mmo_market_burn_item (LrgMmoMarket *self, const gchar *actor, const gchar *item,
                          const gchar *operation, GError **error)
{
    g_autoptr(GVariant) value = NULL, intent = NULL;
    const gchar *owner;
    guint64 revision;
    gint status;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (item))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item withdrawal");
    intent = g_variant_ref_sink (g_variant_new ("(sss)", "burn", actor, item));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    value = item_load (self, item, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s@ay)", &owner, NULL, NULL);
    if (!g_str_equal (actor, owner))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Unique item belongs to another account");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    item_change_owner (&builder, item, revision, value, LRG_MMO_MARKET_OWNER_BURNED);
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

/* ------------------------------------------------------------------ */
/* Exchanges: two accounts swap unique items and coins at once        */

static gboolean
items_valid (const gchar *const *items, guint *count)
{
    guint n = 0;
    if (items != NULL)
        for (; items[n] != NULL; n++)
            if (!_lrg_mmo_id_valid (items[n]) || n >= LRG_MMO_MARKET_MAX_EXCHANGE_ITEMS)
                return FALSE;
    *count = n;
    return TRUE;
}

static void
intent_add_items (GVariantBuilder *intent, const gchar *const *items)
{
    guint i;
    g_variant_builder_open (intent, G_VARIANT_TYPE ("as"));
    for (i = 0; items != NULL && items[i] != NULL; i++)
        g_variant_builder_add (intent, "s", items[i]);
    g_variant_builder_close (intent);
}

/* checks each item is owned by @owner, then queues its move to @recipient */
static gboolean
exchange_items (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *const *items,
                const gchar *owner, const gchar *recipient, GError **error)
{
    guint i;
    for (i = 0; items != NULL && items[i] != NULL; i++)
    {
        g_autoptr(GVariant) value = NULL;
        const gchar *current;
        guint64 revision;
        value = item_load (self, items[i], &revision, error);
        if (value == NULL)
            return FALSE;
        g_variant_get (value, "(&s&s@ay)", &current, NULL, NULL);
        if (!g_str_equal (current, owner))
            return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED,
                                  "Exchanged item is not owned by its side");
        item_change_owner (batch, items[i], revision, value, recipient);
    }
    return TRUE;
}

gboolean
lrg_mmo_market_exchange (LrgMmoMarket *self,
                         const gchar *actor_a, const gchar *const *items_a, gint64 coins_a,
                         const gchar *actor_b, const gchar *const *items_b, gint64 coins_b,
                         const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(GHashTable) seen = NULL;
    GVariantBuilder intent_builder, builder;
    guint count_a, count_b, i;
    gint64 funds_a = 0, funds_b = 0, new_a, new_b;
    guint64 revision_a = 0, revision_b = 0;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor_a) || !_lrg_mmo_id_valid (actor_b) || g_str_equal (actor_a, actor_b) ||
        coins_a < 0 || coins_b < 0 || !items_valid (items_a, &count_a) || !items_valid (items_b, &count_b) ||
        (count_a == 0 && count_b == 0 && coins_a == 0 && coins_b == 0))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid exchange");
    /* one item may appear once in the whole exchange */
    seen = g_hash_table_new (g_str_hash, g_str_equal);
    for (i = 0; i < count_a; i++)
        if (!g_hash_table_add (seen, (gpointer) items_a[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Item repeated in exchange");
    for (i = 0; i < count_b; i++)
        if (!g_hash_table_add (seen, (gpointer) items_b[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Item repeated in exchange");

    g_variant_builder_init (&intent_builder, G_VARIANT_TYPE ("(sssxxasas)"));
    /* the intent: both sides in full, so a retry with other goods is refused */
    g_variant_builder_add (&intent_builder, "s", "exchange");
    g_variant_builder_add (&intent_builder, "s", actor_a);
    g_variant_builder_add (&intent_builder, "s", actor_b);
    g_variant_builder_add (&intent_builder, "x", coins_a);
    g_variant_builder_add (&intent_builder, "x", coins_b);
    intent_add_items (&intent_builder, items_a);
    intent_add_items (&intent_builder, items_b);
    intent = g_variant_ref_sink (g_variant_builder_end (&intent_builder));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (!exchange_items (self, &builder, items_a, actor_a, actor_b, error) ||
        !exchange_items (self, &builder, items_b, actor_b, actor_a, error))
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    if (coins_a > 0 || coins_b > 0)
    {
        funds_a = balance (self, actor_a, "coins", &revision_a, error);
        funds_b = funds_a < 0 ? -1 : balance (self, actor_b, "coins", &revision_b, error);
        if (funds_a < 0 || funds_b < 0)
        {
            g_variant_builder_clear (&builder);
            return FALSE;
        }
        if (funds_a < coins_a || funds_b < coins_b ||
            coins_b > G_MAXINT64 - (funds_a - coins_a) || coins_a > G_MAXINT64 - (funds_b - coins_b))
        {
            g_variant_builder_clear (&builder);
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient coins or exchange overflow");
        }
        new_a = funds_a - coins_a + coins_b;
        new_b = funds_b - coins_b + coins_a;
        change_balance (&builder, actor_a, "coins", revision_a, new_a);
        change_balance (&builder, actor_b, "coins", revision_b, new_b);
    }
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

/* ------------------------------------------------------------------ */
/* Lots: unique-item auctions with buyouts, deposits and a house cut  */

#define LOT_TYPE "(ssssxxxxxuy)"

static gchar *
lot_key (const gchar *lot)
{
    return g_strconcat ("market/lot/", lot, NULL);
}

static GVariant *
lot_load (LrgMmoMarket *self, const gchar *lot, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (lot))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lot ID");
        return NULL;
    }
    key = lot_key (lot);
    return _lrg_mmo_load (self->store, key, LOT_TYPE, revision, error);
}

gint64
lrg_mmo_market_lot_cut (gint64 price, guint cut)
{
    if (price <= 0 || cut == 0)
        return 0;
    if (cut > LRG_MMO_MARKET_CUT_SCALE)
        cut = LRG_MMO_MARKET_CUT_SCALE;
    /* split so price * cut cannot overflow */
    return (price / LRG_MMO_MARKET_CUT_SCALE) * (gint64) cut +
           ((price % LRG_MMO_MARKET_CUT_SCALE) * (gint64) cut) / LRG_MMO_MARKET_CUT_SCALE;
}

gboolean
lrg_mmo_market_lot_create (LrgMmoMarket *self, const gchar *actor, const gchar *lot,
                           const gchar *item, const gchar *house, gint64 minimum,
                           gint64 buyout, gint64 deposit, guint cut, guint duration,
                           GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autofree gchar *key = NULL;
    const gchar *owner;
    guint64 item_revision, funds_revision = 0;
    gint64 funds = 0;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (lot) || !_lrg_mmo_id_valid (item) ||
        !_lrg_mmo_id_valid (house) || g_str_equal (actor, house) || minimum <= 0 ||
        (buyout != 0 && buyout < minimum) || buyout < 0 || deposit < 0 ||
        cut > LRG_MMO_MARKET_CUT_SCALE || duration == 0 || duration > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lot");
    value = item_load (self, item, &item_revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s@ay)", &owner, NULL, NULL);
    if (!g_str_equal (owner, actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Lot item belongs to another account");
    if (deposit > 0)
    {
        funds = balance (self, actor, "coins", &funds_revision, error);
        if (funds < 0)
            return FALSE;
        if (funds < deposit)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient coins for the deposit");
    }
    key = lot_key (lot);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    item_change_owner (&builder, item, item_revision, value, LRG_MMO_MARKET_OWNER_LOT);
    if (deposit > 0)
        change_balance (&builder, actor, "coins", funds_revision, funds - deposit);
    _lrg_mmo_change (&builder, key, 0, g_variant_new (LOT_TYPE, actor, item, house, "", minimum,
                     (gint64) 0, buyout, deposit, lrg_mmo_market_now (self) + duration, (guint32) cut,
                     (guchar) LRG_MMO_MARKET_LOT_OPEN));
    return commit (self, &builder, error);
}

GVariant *
lrg_mmo_market_lot_inspect (LrgMmoMarket *self, const gchar *lot, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), NULL);
    return lot_load (self, lot, &revision, error);
}

/*
 * close_lot:
 * Queues the closing of a lot: the item to @recipient; on a sale the
 * seller's proceeds (bid - cut + deposit) and the house's cut; otherwise
 * the deposit forfeited to the house.  Balance keys never coincide: the
 * seller, the house and the bidder are distinct accounts.
 */
static gboolean
close_lot (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *lot, guint64 revision,
           GVariant *record, const gchar *bidder, gint64 bid, guchar state, GError **error)
{
    g_autoptr(GVariant) item_value = NULL;
    g_autofree gchar *key = lot_key (lot);
    const gchar *seller, *item, *house, *old_bidder;
    gint64 minimum, old_bid, buyout, deposit, expiry, cut_amount, proceeds, seller_funds, house_funds = 0;
    guint64 item_revision, seller_revision, house_revision = 0;
    guint32 cut;
    guchar old_state;
    gboolean sold = state == LRG_MMO_MARKET_LOT_SOLD;
    g_variant_get (record, "(&s&s&s&sxxxxxuy)", &seller, &item, &house, &old_bidder, &minimum,
                   &old_bid, &buyout, &deposit, &expiry, &cut, &old_state);
    item_value = item_load (self, item, &item_revision, error);
    if (item_value == NULL)
        return FALSE;
    item_change_owner (batch, item, item_revision, item_value, sold ? bidder : seller);
    cut_amount = sold ? lrg_mmo_market_lot_cut (bid, cut) : 0;
    proceeds = sold ? bid - cut_amount : 0;
    if (proceeds > G_MAXINT64 - deposit)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Lot proceeds overflow");
    /* a sale returns the deposit; an unsold or cancelled lot forfeits it */
    if (sold)
        proceeds += deposit;
    if (proceeds > 0)
    {
        seller_funds = balance (self, seller, "coins", &seller_revision, error);
        if (seller_funds < 0)
            return FALSE;
        if (proceeds > G_MAXINT64 - seller_funds)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Seller balance overflow");
        change_balance (batch, seller, "coins", seller_revision, seller_funds + proceeds);
    }
    if ((sold ? cut_amount : deposit) > 0)
    {
        gint64 fee = sold ? cut_amount : deposit;
        house_funds = balance (self, house, "coins", &house_revision, error);
        if (house_funds < 0)
            return FALSE;
        if (fee > G_MAXINT64 - house_funds)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "House balance overflow");
        change_balance (batch, house, "coins", house_revision, house_funds + fee);
    }
    _lrg_mmo_change (batch, key, revision, g_variant_new (LOT_TYPE, seller, item, house,
                     sold ? bidder : old_bidder, minimum, sold ? bid : old_bid, buyout, deposit,
                     expiry, cut, state));
    return TRUE;
}

static gboolean
lot_bid (LrgMmoMarket *self, const gchar *actor, const gchar *lot, gint64 amount,
         gboolean buyout_only, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) value = NULL, intent = NULL;
    g_autofree gchar *key = NULL;
    const gchar *seller, *item, *house, *bidder;
    gint64 minimum, bid, buyout, deposit, expiry, funds, refund = 0, debit;
    guint64 revision, funds_revision, refund_revision = 0;
    guint32 cut;
    guchar state;
    gboolean reached, other_bidder;
    gint status;
    GVariantBuilder builder;
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (lot) || (!buyout_only && amount <= 0))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid bid");
    intent = g_variant_ref_sink (g_variant_new ("(sssxb)", "lot-bid", actor, lot, amount, buyout_only));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    value = lot_load (self, lot, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&s&sxxxxxuy)", &seller, &item, &house, &bidder, &minimum, &bid,
                   &buyout, &deposit, &expiry, &cut, &state);
    if (buyout_only)
    {
        if (buyout <= 0)
            return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Lot has no buyout");
        amount = buyout;
    }
    /* a bid at or over the buyout is the buyout */
    reached = buyout > 0 && amount >= buyout;
    if (reached)
        amount = buyout;
    if (state != LRG_MMO_MARKET_LOT_OPEN || expiry <= lrg_mmo_market_now (self) ||
        g_str_equal (seller, actor) || g_str_equal (house, actor) || amount < minimum ||
        amount <= bid || bid < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Bid is not eligible");
    other_bidder = *bidder != '\0' && !g_str_equal (actor, bidder);
    funds = balance (self, actor, "coins", &funds_revision, error);
    if (funds < 0)
        return FALSE;
    debit = other_bidder || *bidder == '\0' ? amount : amount - bid;
    if (funds < debit)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient bid funds");
    if (other_bidder)
    {
        refund = balance (self, bidder, "coins", &refund_revision, error);
        if (refund < 0)
            return FALSE;
        if (bid > G_MAXINT64 - refund)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Refund balance overflow");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    change_balance (&builder, actor, "coins", funds_revision, funds - debit);
    if (other_bidder)
        change_balance (&builder, bidder, "coins", refund_revision, refund + bid);
    if (reached)
    {
        if (!close_lot (self, &builder, lot, revision, value, actor, amount,
                        LRG_MMO_MARKET_LOT_SOLD, error))
        {
            g_variant_builder_clear (&builder);
            return FALSE;
        }
    }
    else
    {
        key = lot_key (lot);
        _lrg_mmo_change (&builder, key, revision, g_variant_new (LOT_TYPE, seller, item, house, actor,
                         minimum, amount, buyout, deposit, expiry, cut, state));
    }
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_lot_bid (LrgMmoMarket *self, const gchar *actor, const gchar *lot,
                        gint64 amount, const gchar *operation, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    return lot_bid (self, actor, lot, amount, FALSE, operation, error);
}

gboolean
lrg_mmo_market_lot_buyout (LrgMmoMarket *self, const gchar *actor, const gchar *lot,
                           const gchar *operation, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    return lot_bid (self, actor, lot, 0, TRUE, operation, error);
}

gboolean
lrg_mmo_market_lot_settle (LrgMmoMarket *self, const gchar *lot, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    const gchar *bidder;
    gint64 bid, expiry;
    guint64 revision;
    guchar state;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    value = lot_load (self, lot, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&s&sxxxxxuy)", NULL, NULL, NULL, &bidder, NULL, &bid, NULL,
                   NULL, &expiry, NULL, &state);
    if (state != LRG_MMO_MARKET_LOT_OPEN)
        return TRUE;
    if (expiry > lrg_mmo_market_now (self))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Lot not ready for settlement");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (!close_lot (self, &builder, lot, revision, value, bidder, bid,
                    *bidder != '\0' ? LRG_MMO_MARKET_LOT_SOLD : LRG_MMO_MARKET_LOT_UNSOLD, error))
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_lot_cancel (LrgMmoMarket *self, const gchar *actor, const gchar *lot, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    const gchar *seller, *bidder;
    guint64 revision;
    guchar state;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    value = lot_load (self, lot, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s&s&s&sxxxxxuy)", &seller, NULL, NULL, &bidder, NULL, NULL, NULL,
                   NULL, NULL, NULL, &state);
    if (state != LRG_MMO_MARKET_LOT_OPEN || !g_str_equal (actor, seller) || *bidder != '\0')
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Lot cannot be cancelled by this actor");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (!close_lot (self, &builder, lot, revision, value, "", 0, LRG_MMO_MARKET_LOT_CANCELLED, error))
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    return commit (self, &builder, error);
}

/* ------------------------------------------------------------------ */
/* Conversions and work orders                                        */

/*
 * resources_valid:
 * An a{sx} of resources: valid IDs named once, positive quantities, at
 * most LRG_MMO_MARKET_MAX_RESOURCES of them, coins only when allowed.
 * NULL is the empty set.
 */
static gboolean
resources_valid (GVariant *resources, gboolean allow_coins, guint *count)
{
    g_autoptr(GHashTable) seen = NULL;
    GVariantIter iter;
    const gchar *resource;
    gint64 quantity;
    guint n = 0;
    *count = 0;
    if (resources == NULL)
        return TRUE;
    if (!g_variant_is_of_type (resources, G_VARIANT_TYPE ("a{sx}")))
        return FALSE;
    seen = g_hash_table_new (g_str_hash, g_str_equal);
    g_variant_iter_init (&iter, resources);
    while (g_variant_iter_next (&iter, "{&sx}", &resource, &quantity))
    {
        if (!_lrg_mmo_id_valid (resource) || quantity <= 0 || ++n > LRG_MMO_MARKET_MAX_RESOURCES ||
            (!allow_coins && g_str_equal (resource, "coins")) ||
            !g_hash_table_add (seen, (gpointer) resource))
            return FALSE;
    }
    *count = n;
    return TRUE;
}

/* whether two resource sets share a resource */
static gboolean
resources_overlap (GVariant *a, GVariant *b)
{
    GVariantIter iter;
    const gchar *resource;
    gint64 quantity;
    if (a == NULL || b == NULL)
        return FALSE;
    g_variant_iter_init (&iter, a);
    while (g_variant_iter_next (&iter, "{&sx}", &resource, &quantity))
    {
        gint64 other;
        if (g_variant_lookup (b, resource, "x", &other))
            return TRUE;
    }
    return FALSE;
}

/* the empty a{sx} for intents and records */
static GVariant *
resources_or_empty (GVariant *resources)
{
    if (resources != NULL)
        return g_variant_ref (resources);
    return g_variant_ref_sink (g_variant_new_array (G_VARIANT_TYPE ("{sx}"), NULL, 0));
}

/* queues the debit (@sign < 0) or credit of every resource of @resources
 * for @account, checked against the stored balances */
static gboolean
apply_resources (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *account,
                 GVariant *resources, gint sign, GError **error)
{
    GVariantIter iter;
    const gchar *resource;
    gint64 quantity;
    if (resources == NULL)
        return TRUE;
    g_variant_iter_init (&iter, resources);
    while (g_variant_iter_next (&iter, "{&sx}", &resource, &quantity))
    {
        guint64 revision;
        gint64 current = balance (self, account, resource, &revision, error);
        if (current < 0)
            return FALSE;
        if (sign < 0 && current < quantity)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient resources");
        if (sign > 0 && quantity > G_MAXINT64 - current)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Resource balance would overflow");
        change_balance (batch, account, resource, revision, sign < 0 ? current - quantity : current + quantity);
    }
    return TRUE;
}

/* queues a credit of one resource to @account */
static gboolean
apply_resources_one (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *account,
                     const gchar *resource, gint64 quantity, GError **error)
{
    guint64 revision;
    gint64 current = balance (self, account, resource, &revision, error);
    if (current < 0)
        return FALSE;
    if (quantity > G_MAXINT64 - current)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Resource balance would overflow");
    change_balance (batch, account, resource, revision, current + quantity);
    return TRUE;
}

/* queues the mint of a unique item (must not exist yet) */
static gboolean
mint_in_batch (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *item, const gchar *owner,
               const gchar *definition, GBytes *metadata, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) existing = NULL;
    g_autoptr(GError) local_error = NULL;
    guint64 revision;
    if (!_lrg_mmo_id_valid (item) || !_lrg_mmo_id_valid (definition) || metadata == NULL ||
        g_bytes_get_size (metadata) > 65536)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid unique item");
    existing = item_load (self, item, &revision, &local_error);
    if (existing != NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Unique item ID already minted");
    if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    key = item_key (item);
    _lrg_mmo_change (batch, key, 0, g_variant_new ("(ss@ay)", owner, definition,
                     g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), metadata, TRUE)));
    return TRUE;
}

/* the digest of an item's metadata for intents ("" without one) */
static gchar *
metadata_digest (GBytes *metadata)
{
    if (metadata == NULL)
        return g_strdup ("");
    return g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, metadata);
}

gboolean
lrg_mmo_market_convert (LrgMmoMarket *self, const gchar *account, GVariant *take, GVariant *give,
                        const gchar *item, const gchar *definition, GBytes *metadata,
                        const gchar *operation, GError **error)
{
    g_autoptr(GVariant) intent = NULL, take_all = NULL, give_all = NULL;
    g_autofree gchar *digest = NULL;
    GVariantBuilder builder;
    guint takes, gives;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (account) || !resources_valid (take, TRUE, &takes) ||
        !resources_valid (give, TRUE, &gives) || resources_overlap (take, give) ||
        (takes == 0 && gives == 0 && item == NULL) ||
        (item != NULL && (definition == NULL || metadata == NULL)))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid conversion");
    take_all = resources_or_empty (take);
    give_all = resources_or_empty (give);
    digest = metadata_digest (metadata);
    intent = g_variant_ref_sink (g_variant_new ("(ss@a{sx}@a{sx}sss)", "convert", account, take_all,
                                                give_all, item != NULL ? item : "",
                                                definition != NULL ? definition : "", digest));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (!apply_resources (self, &builder, account, take, -1, error) ||
        !apply_resources (self, &builder, account, give, 1, error) ||
        (item != NULL && !mint_in_batch (self, &builder, item, account, definition, metadata, error)))
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

#define ORDER_TYPE "(ssa{sx}xuxsxy)"
/* the same record for g_variant_new() with the escrow as a GVariant */
#define ORDER_NEW "(ss@a{sx}xuxsxy)"

static gchar *
order_key (const gchar *order)
{
    return g_strconcat ("market/order/", order, NULL);
}

static GVariant *
order_load (LrgMmoMarket *self, const gchar *order, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (order))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid order ID");
        return NULL;
    }
    key = order_key (order);
    return _lrg_mmo_load (self->store, key, ORDER_TYPE, revision, error);
}

/* an order record's fields; strings and escrow borrowed from @record */
typedef struct
{
    const gchar *requester;
    const gchar *house;
    GVariant *escrow;
    gint64 fee;
    guint32 cut;
    gint64 expiry;
    const gchar *worker;
    gint64 claim_expiry;
    guchar state;
} Order;

static void
order_read (GVariant *record, Order *out)
{
    g_variant_get (record, "(&s&s@a{sx}xux&sxy)", &out->requester, &out->house, &out->escrow,
                   &out->fee, &out->cut, &out->expiry, &out->worker, &out->claim_expiry, &out->state);
}

static GVariant *
order_record (const Order *order, const gchar *worker, gint64 claim_expiry, guchar state)
{
    return g_variant_new (ORDER_NEW, order->requester, order->house, order->escrow, order->fee,
                          order->cut, order->expiry, worker, claim_expiry, state);
}

/* whether a worker holds the order now */
static gboolean
order_held (LrgMmoMarket *self, const Order *order)
{
    return order->worker[0] != '\0' && order->claim_expiry > lrg_mmo_market_now (self);
}

gboolean
lrg_mmo_market_order_create (LrgMmoMarket *self, const gchar *actor, const gchar *order,
                             const gchar *house, GVariant *escrow, gint64 fee, guint cut,
                             guint duration, GError **error)
{
    g_autoptr(GVariant) escrow_all = NULL;
    g_autofree gchar *key = NULL;
    GVariantBuilder builder;
    guint count;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (order) || !_lrg_mmo_id_valid (house) ||
        g_str_equal (actor, house) || !resources_valid (escrow, FALSE, &count) || fee < 0 ||
        (count == 0 && fee == 0) || cut > LRG_MMO_MARKET_CUT_SCALE || duration == 0 || duration > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid order");
    escrow_all = resources_or_empty (escrow);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (!apply_resources (self, &builder, actor, escrow, -1, error))
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    if (fee > 0)
    {
        guint64 revision;
        gint64 funds = balance (self, actor, "coins", &revision, error);
        if (funds < 0 || funds < fee)
        {
            g_variant_builder_clear (&builder);
            return funds < 0 ? FALSE : _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Insufficient coins for the fee");
        }
        change_balance (&builder, actor, "coins", revision, funds - fee);
    }
    /* revision 0: the commit fails when the order ID exists */
    key = order_key (order);
    _lrg_mmo_change (&builder, key, 0, g_variant_new (ORDER_NEW, actor, house, escrow_all, fee,
                     (guint32) cut, lrg_mmo_market_now (self) + duration, "", (gint64) 0,
                     (guchar) LRG_MMO_MARKET_ORDER_OPEN));
    return commit (self, &builder, error);
}

GVariant *
lrg_mmo_market_order_inspect (LrgMmoMarket *self, const gchar *order, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), NULL);
    return order_load (self, order, &revision, error);
}

gboolean
lrg_mmo_market_order_claim (LrgMmoMarket *self, const gchar *actor, const gchar *order,
                            guint claim_seconds, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) value = NULL, intent = NULL;
    g_autofree gchar *key = NULL;
    GVariantBuilder builder;
    Order o;
    guint64 revision;
    gint64 now, until;
    gint status;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || claim_seconds == 0 || claim_seconds > 604800)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid claim");
    intent = g_variant_ref_sink (g_variant_new ("(sssu)", "order-claim", actor, order, claim_seconds));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    value = order_load (self, order, &revision, error);
    if (value == NULL)
        return FALSE;
    order_read (value, &o);
    now = lrg_mmo_market_now (self);
    if (o.state != LRG_MMO_MARKET_ORDER_OPEN || o.expiry <= now || g_str_equal (actor, o.requester) ||
        g_str_equal (actor, o.house) || (order_held (self, &o) && !g_str_equal (actor, o.worker)))
    {
        g_variant_unref (o.escrow);
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Order cannot be claimed");
    }
    until = MIN (now + (gint64) claim_seconds, o.expiry);
    key = order_key (order);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, revision, order_record (&o, actor, until, LRG_MMO_MARKET_ORDER_OPEN));
    g_variant_unref (o.escrow);
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_order_release (LrgMmoMarket *self, const gchar *actor, const gchar *order, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autofree gchar *key = NULL;
    GVariantBuilder builder;
    Order o;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    value = order_load (self, order, &revision, error);
    if (value == NULL)
        return FALSE;
    order_read (value, &o);
    if (o.state != LRG_MMO_MARKET_ORDER_OPEN || !g_str_equal (actor, o.worker))
    {
        g_variant_unref (o.escrow);
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Order is not held by this actor");
    }
    key = order_key (order);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, revision, order_record (&o, "", 0, LRG_MMO_MARKET_ORDER_OPEN));
    g_variant_unref (o.escrow);
    return commit (self, &builder, error);
}

/* queues an order's refund (escrow and fee to the requester) and its close */
static gboolean
order_refund (LrgMmoMarket *self, GVariantBuilder *batch, const gchar *order, guint64 revision,
              const Order *o, guchar state, GError **error)
{
    g_autofree gchar *key = order_key (order);
    if (!apply_resources (self, batch, o->requester, o->escrow, 1, error))
        return FALSE;
    if (o->fee > 0)
    {
        guint64 funds_revision;
        gint64 funds = balance (self, o->requester, "coins", &funds_revision, error);
        if (funds < 0)
            return FALSE;
        if (o->fee > G_MAXINT64 - funds)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Refund balance overflow");
        change_balance (batch, o->requester, "coins", funds_revision, funds + o->fee);
    }
    _lrg_mmo_change (batch, key, revision, order_record (o, o->worker, o->claim_expiry, state));
    return TRUE;
}

gboolean
lrg_mmo_market_order_cancel (LrgMmoMarket *self, const gchar *actor, const gchar *order, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    GVariantBuilder builder;
    Order o;
    guint64 revision;
    gboolean ok;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid actor");
    value = order_load (self, order, &revision, error);
    if (value == NULL)
        return FALSE;
    order_read (value, &o);
    if (o.state != LRG_MMO_MARKET_ORDER_OPEN || !g_str_equal (actor, o.requester) || order_held (self, &o))
    {
        g_variant_unref (o.escrow);
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Order cannot be cancelled by this actor");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    ok = order_refund (self, &builder, order, revision, &o, LRG_MMO_MARKET_ORDER_CANCELLED, error);
    g_variant_unref (o.escrow);
    if (!ok)
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_order_settle (LrgMmoMarket *self, const gchar *order, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    GVariantBuilder builder;
    Order o;
    guint64 revision;
    gboolean ok;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    value = order_load (self, order, &revision, error);
    if (value == NULL)
        return FALSE;
    order_read (value, &o);
    if (o.state != LRG_MMO_MARKET_ORDER_OPEN)
    {
        g_variant_unref (o.escrow);
        return TRUE;
    }
    if (o.expiry > lrg_mmo_market_now (self))
    {
        g_variant_unref (o.escrow);
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Order not ready for settlement");
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    ok = order_refund (self, &builder, order, revision, &o, LRG_MMO_MARKET_ORDER_EXPIRED, error);
    g_variant_unref (o.escrow);
    if (!ok)
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    return commit (self, &builder, error);
}

gboolean
lrg_mmo_market_order_fulfil (LrgMmoMarket *self, const gchar *actor, const gchar *order,
                             GVariant *give, const gchar *item, const gchar *definition,
                             GBytes *metadata, const gchar *operation, GError **error)
{
    g_autoptr(GVariant) value = NULL, intent = NULL, give_all = NULL;
    g_autofree gchar *key = NULL, *digest = NULL;
    GVariantBuilder builder;
    Order o;
    guint64 revision;
    gint64 cut_amount, pay;
    guint gives;
    gint status;
    gboolean ok = TRUE;
    g_return_val_if_fail (LRG_IS_MMO_MARKET (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !resources_valid (give, TRUE, &gives) ||
        (item != NULL && (definition == NULL || metadata == NULL)))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid delivery");
    give_all = resources_or_empty (give);
    digest = metadata_digest (metadata);
    intent = g_variant_ref_sink (g_variant_new ("(sss@a{sx}sss)", "order-fulfil", actor, order, give_all,
                                                item != NULL ? item : "", definition != NULL ? definition : "",
                                                digest));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    value = order_load (self, order, &revision, error);
    if (value == NULL)
        return FALSE;
    order_read (value, &o);
    if (o.state != LRG_MMO_MARKET_ORDER_OPEN || !g_str_equal (actor, o.worker) || !order_held (self, &o))
    {
        g_variant_unref (o.escrow);
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Order is not held by this actor");
    }
    /* the fee: the worker's share and the house's cut (distinct accounts) */
    cut_amount = lrg_mmo_market_lot_cut (o.fee, o.cut);
    pay = o.fee - cut_amount;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    if (pay > 0)
        ok = apply_resources_one (self, &builder, actor, "coins", pay, error);
    if (ok && cut_amount > 0)
        ok = apply_resources_one (self, &builder, o.house, "coins", cut_amount, error);
    /* the product to the requester; the escrow is consumed (already debited) */
    if (ok)
        ok = apply_resources (self, &builder, o.requester, give, 1, error);
    if (ok && item != NULL)
        ok = mint_in_batch (self, &builder, item, o.requester, definition, metadata, error);
    if (ok)
    {
        key = order_key (order);
        _lrg_mmo_change (&builder, key, revision, order_record (&o, actor, o.claim_expiry,
                         LRG_MMO_MARKET_ORDER_FULFILLED));
    }
    g_variant_unref (o.escrow);
    if (!ok)
    {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    _lrg_mmo_operation_add (&builder, operation, intent);
    return commit (self, &builder, error);
}
