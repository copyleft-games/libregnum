/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-market.h"
#include "lrg-mmo-service-private.h"

struct _LrgMmoMarket
{
    GObject parent_instance;
    LrgMmoStore *store;
};
G_DEFINE_TYPE (LrgMmoMarket, lrg_mmo_market, G_TYPE_OBJECT)
static void
lrg_mmo_market_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_MARKET (object)->store);
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
                     g_get_real_time () / G_TIME_SPAN_SECOND + duration, FALSE));
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
    if (closed || quantity <= 0 || price <= 0 || expiry <= g_get_real_time () / G_TIME_SPAN_SECOND ||
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
                     take_resource, give_quantity, take_quantity, g_get_real_time () / G_TIME_SPAN_SECOND + duration, FALSE));
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
        (!cancel && expiry <= g_get_real_time () / G_TIME_SPAN_SECOND))
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
                         minimum, (gint64) 0, g_get_real_time () / G_TIME_SPAN_SECOND + duration, FALSE));
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
    if (closed || expiry <= g_get_real_time () / G_TIME_SPAN_SECOND || g_str_equal (seller, actor) ||
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
    if (expiry > g_get_real_time () / G_TIME_SPAN_SECOND || quantity <= 0 || bid < 0 || g_str_equal (resource, "coins"))
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
