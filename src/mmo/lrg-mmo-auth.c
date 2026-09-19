/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-auth.h"
#include "lrg-mmo-service-private.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

struct _LrgMmoAuth
{
    GObject parent_instance;
    LrgMmoStore *store;
};
G_DEFINE_TYPE (LrgMmoAuth, lrg_mmo_auth, G_TYPE_OBJECT)

static void
lrg_mmo_auth_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_AUTH (object)->store);
    G_OBJECT_CLASS (lrg_mmo_auth_parent_class)->dispose (object);
}
static void
lrg_mmo_auth_class_init (LrgMmoAuthClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_auth_dispose;
}
static void
lrg_mmo_auth_init (LrgMmoAuth *self)
{
}
LrgMmoAuth *
lrg_mmo_auth_new (LrgMmoStore *store)
{
    LrgMmoAuth *self;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    self = g_object_new (LRG_TYPE_MMO_AUTH, NULL);
    self->store = g_object_ref (store);
    return self;
}

static gboolean
valid_password (const gchar *password)
{
    return password != NULL && strlen (password) >= 12 && strlen (password) <= 1024 &&
           g_utf8_validate (password, -1, NULL);
}

static GVariant *
credential (const gchar *password, guint64 generation, gboolean banned, GError **error)
{
    guint8 salt[16];
    guint8 digest[32];
    GVariant *value;
    if (!valid_password (password))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Password must contain 12 to 1024 UTF-8 bytes");
        return NULL;
    }
    if (RAND_bytes (salt, sizeof salt) != 1 ||
        PKCS5_PBKDF2_HMAC (password, strlen (password), salt, sizeof salt,
                          600000, EVP_sha256 (), sizeof digest, digest) != 1)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "Credential derivation failed");
        return NULL;
    }
    value = g_variant_ref_sink (g_variant_new ("(@ay@aytb)",
                               g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, salt, sizeof salt, 1),
                               g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, digest, sizeof digest, 1),
                               generation, banned));
    OPENSSL_cleanse (digest, sizeof digest);
    return value;
}

static gchar *
account_key (const gchar *account)
{
    return g_strconcat ("auth/account/", account, NULL);
}

static gboolean
token_valid (const gchar *token)
{
    guint i;
    if (token == NULL || strlen (token) != 64)
        return FALSE;
    for (i = 0; i < 64; i++)
        if (!g_ascii_isxdigit (token[i]))
            return FALSE;
    return TRUE;
}

static gchar *
token_key (const gchar *token, gboolean recovery)
{
    g_autofree gchar *digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, token, -1);
    return g_strconcat (recovery ? "auth/recovery/" : "auth/token/", digest, NULL);
}

static gchar *
random_token (GError **error)
{
    guint8 random[32];
    gchar *token;
    guint i;
    if (RAND_bytes (random, sizeof random) != 1)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "Secure random generation failed");
        return NULL;
    }
    token = g_malloc (65);
    for (i = 0; i < 32; i++)
        g_snprintf (token + i * 2, 3, "%02x", random[i]);
    OPENSSL_cleanse (random, sizeof random);
    return token;
}

static GVariant *
load_account (LrgMmoAuth *self, const gchar *account, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (account))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid account ID");
        return NULL;
    }
    key = account_key (account);
    return _lrg_mmo_load (self->store, key, "(ayaytb)", revision, error);
}

static gboolean
account_flags (GVariant *value, guint64 *generation, gboolean *banned, GError **error)
{
    g_autoptr(GVariant) salt = g_variant_get_child_value (value, 0);
    g_autoptr(GVariant) digest = g_variant_get_child_value (value, 1);
    g_variant_get_child (value, 2, "t", generation);
    g_variant_get_child (value, 3, "b", banned);
    if (g_variant_get_size (salt) != 16 || g_variant_get_size (digest) != 32 || *generation == 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid credential record");
    return TRUE;
}

gboolean
lrg_mmo_auth_register (LrgMmoAuth *self, const gchar *account, const gchar *password, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), FALSE);
    if (!_lrg_mmo_id_valid (account))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid account ID");
    key = account_key (account);
    value = credential (password, 1, FALSE, error);
    return value != NULL && _lrg_mmo_put (self->store, key, 0, value, error);
}

static gchar *
issue (LrgMmoAuth *self, const gchar *account, guint64 generation,
       gint64 now, gboolean recovery, GError **error)
{
    g_autofree gchar *token = NULL;
    g_autofree gchar *key = NULL;
    if (now < 0 || now > G_MAXINT64 - 3600)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid server time");
        return NULL;
    }
    token = random_token (error);
    if (token == NULL)
        return NULL;
    key = token_key (token, recovery);
    if (!_lrg_mmo_put (self->store, key, 0,
                       g_variant_new ("(stx)", account, generation, now + (recovery ? 900 : 3600)), error))
        return NULL;
    return g_steal_pointer (&token);
}

static gboolean
match_totp (GBytes *secret, guint code, gint64 now, gint64 last, gint64 *matched)
{
    gint shift;
    gint64 step;
    gsize size;
    const guint8 *key = g_bytes_get_data (secret, &size);
    if (now < 0 || now > G_MAXINT64 - 3600 || code > 999999 || size < 20 || size > 64)
        return FALSE;
    step = now / 30;
    for (shift = -1; shift <= 1; shift++)
    {
        gint64 candidate = step + shift;
        guint64 counter = GUINT64_TO_BE ((guint64) candidate);
        g_autoptr(GHmac) hmac = NULL;
        guint8 digest[20];
        gsize length = sizeof digest;
        guint offset, value;
        if (candidate < 0 || candidate <= last)
            continue;
        hmac = g_hmac_new (G_CHECKSUM_SHA1, key, size);
        g_hmac_update (hmac, (const guint8 *) &counter, sizeof counter);
        g_hmac_get_digest (hmac, digest, &length);
        offset = digest[19] & 15;
        value = ((guint) (digest[offset] & 127) << 24) | ((guint) digest[offset + 1] << 16) |
                ((guint) digest[offset + 2] << 8) | digest[offset + 3];
        OPENSSL_cleanse (digest, sizeof digest);
        if (value % 1000000 == code)
        {
            *matched = candidate;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
consume_totp (LrgMmoAuth *self, const gchar *account, gboolean supplied, guint code,
              gint64 now, GError **error)
{
    g_autofree gchar *key = g_strconcat ("auth/totp/", account, NULL);
    g_autoptr(GVariant) value = NULL, bytes = NULL;
    g_autoptr(GBytes) secret = NULL;
    g_autoptr(GError) local_error = NULL;
    guint64 revision;
    gint64 last, matched;
    value = _lrg_mmo_load (self->store, key, "(ayx)", &revision, &local_error);
    if (value == NULL)
    {
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return TRUE;
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    g_variant_get (value, "(@ayx)", &bytes, &last);
    if (g_variant_get_size (bytes) == 0)
        return TRUE;
    secret = g_variant_get_data_as_bytes (bytes);
    if (!supplied || !match_totp (secret, code, now, last, &matched))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Second factor missing, invalid or replayed");
    return _lrg_mmo_put (self->store, key, revision, g_variant_new ("(@ayx)", bytes, matched), error);
}

static gchar *
login_internal (LrgMmoAuth *self, const gchar *account, const gchar *password,
                    gint64 now, gboolean supplied, guint code, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) salt = NULL;
    g_autoptr(GVariant) stored = NULL;
    g_autoptr(GError) local_error = NULL;
    guint64 revision, generation = 0;
    gboolean banned = TRUE;
    guint8 digest[32];
    guint8 dummy_salt[16] = { 0 };
    guint8 dummy_digest[32] = { 0 };
    const guint8 *salt_data = dummy_salt;
    const guint8 *stored_data = dummy_digest;
    gboolean matches;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), NULL);
    if (!valid_password (password))
        goto denied;
    value = load_account (self, account, &revision, &local_error);
    if (value != NULL && account_flags (value, &generation, &banned, &local_error))
    {
        salt = g_variant_get_child_value (value, 0);
        stored = g_variant_get_child_value (value, 1);
        salt_data = g_variant_get_data (salt);
        stored_data = g_variant_get_data (stored);
    }
    /* Unknown accounts perform the same expensive derivation. */
    if (PKCS5_PBKDF2_HMAC (password, strlen (password), salt_data, 16,
                          600000, EVP_sha256 (), 32, digest) != 1)
        goto denied;
    matches = CRYPTO_memcmp (digest, stored_data, 32) == 0;
    OPENSSL_cleanse (digest, sizeof digest);
    if (!matches || banned || local_error != NULL)
        goto denied;
    if (!consume_totp (self, account, supplied, code, now, error))
        return NULL;
    return issue (self, account, generation, now, FALSE, error);
denied:
    _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Invalid credentials or unavailable account");
    return NULL;
}

gchar *
lrg_mmo_auth_login (LrgMmoAuth *self, const gchar *account, const gchar *password, gint64 now, GError **error)
{
    return login_internal (self, account, password, now, FALSE, 0, error);
}

gchar *
lrg_mmo_auth_login_totp (LrgMmoAuth *self, const gchar *account, const gchar *password,
                        guint code, gint64 now, GError **error)
{
    return login_internal (self, account, password, now, TRUE, code, error);
}

GBytes *
lrg_mmo_auth_generate_totp_secret (GError **error)
{
    guint8 secret[20];
    GBytes *bytes;
    if (RAND_bytes (secret, sizeof secret) != 1)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "Secure random generation failed");
        return NULL;
    }
    bytes = g_bytes_new (secret, sizeof secret);
    OPENSSL_cleanse (secret, sizeof secret);
    return bytes;
}

gboolean
lrg_mmo_auth_set_totp (LrgMmoAuth *self, const gchar *account, GBytes *secret,
                      guint confirmation, gint64 now, GError **error)
{
    g_autoptr(GVariant) credentials = NULL, old = NULL, salt = NULL, hash = NULL, batch = NULL;
    g_autoptr(GError) local_error = NULL;
    g_autofree gchar *key = NULL, *credentials_key = NULL;
    guint64 revision, account_revision, generation;
    gboolean banned;
    gint64 matched = -1;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), FALSE);
    credentials = load_account (self, account, &account_revision, error);
    if (credentials == NULL || !account_flags (credentials, &generation, &banned, error))
        return FALSE;
    if (generation == G_MAXUINT64 || banned ||
        (secret != NULL && !match_totp (secret, confirmation, now, -1, &matched)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Second factor enrollment denied");
    key = g_strconcat ("auth/totp/", account, NULL);
    old = _lrg_mmo_load (self->store, key, "(ayx)", &revision, &local_error);
    if (old == NULL && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    credentials_key = account_key (account);
    salt = g_variant_get_child_value (credentials, 0);
    hash = g_variant_get_child_value (credentials, 1);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, credentials_key, account_revision,
                     g_variant_new ("(@ay@aytb)", salt, hash, generation + 1, banned));
    _lrg_mmo_change (&builder, key, revision, g_variant_new ("(@ayx)",
                     secret != NULL ? g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), secret, TRUE) :
                                      g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, NULL, 0, 1), matched));
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}

static GVariant *
verify_token (LrgMmoAuth *self, const gchar *token, gboolean recovery,
              gint64 now, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) account = NULL;
    g_autoptr(GError) local_error = NULL;
    const gchar *identity;
    guint64 generation, current, account_revision;
    gboolean banned;
    gint64 expiry;
    if (!token_valid (token) || now < 0)
        goto denied;
    key = token_key (token, recovery);
    value = _lrg_mmo_load (self->store, key, "(stx)", revision, &local_error);
    if (value == NULL)
        goto denied;
    g_variant_get (value, "(&stx)", &identity, &generation, &expiry);
    if (expiry <= now)
        goto denied;
    account = load_account (self, identity, &account_revision, &local_error);
    if (account == NULL || !account_flags (account, &current, &banned, &local_error) ||
        banned || current != generation)
        goto denied;
    return g_steal_pointer (&value);
denied:
    _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Invalid, revoked or expired token");
    return NULL;
}

gchar *
lrg_mmo_auth_verify (LrgMmoAuth *self, const gchar *token, gint64 now, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    const gchar *account;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), NULL);
    value = verify_token (self, token, FALSE, now, &revision, error);
    if (value == NULL)
        return NULL;
    g_variant_get_child (value, 0, "&s", &account);
    return g_strdup (account);
}

gchar *
lrg_mmo_auth_rotate (LrgMmoAuth *self, const gchar *token, gint64 now, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) batch = NULL;
    g_autofree gchar *replacement = NULL;
    g_autofree gchar *old_key = NULL;
    g_autofree gchar *new_key = NULL;
    const gchar *account;
    guint64 revision, generation;
    gint64 expiry;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), NULL);
    value = verify_token (self, token, FALSE, now, &revision, error);
    if (value == NULL)
        return NULL;
    if (now > G_MAXINT64 - 3600)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid server time");
        return NULL;
    }
    replacement = random_token (error);
    if (replacement == NULL)
        return NULL;
    g_variant_get (value, "(&stx)", &account, &generation, &expiry);
    old_key = token_key (token, FALSE);
    new_key = token_key (replacement, FALSE);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, old_key, revision, g_variant_new ("(stx)", account, generation, (gint64) 0));
    _lrg_mmo_change (&builder, new_key, 0, g_variant_new ("(stx)", account, generation, now + 3600));
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    if (!lrg_mmo_store_commit (self->store, batch, error))
        return NULL;
    return g_steal_pointer (&replacement);
}

gboolean
lrg_mmo_auth_revoke (LrgMmoAuth *self, const gchar *token, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autofree gchar *key = NULL;
    guint64 revision, generation;
    const gchar *account;
    gint64 expiry;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), FALSE);
    if (!token_valid (token))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid token");
    key = token_key (token, FALSE);
    value = _lrg_mmo_load (self->store, key, "(stx)", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&stx)", &account, &generation, &expiry);
    return expiry == 0 || _lrg_mmo_put (self->store, key, revision,
                                        g_variant_new ("(stx)", account, generation, (gint64) 0), error);
}

gboolean
lrg_mmo_auth_set_banned (LrgMmoAuth *self, const gchar *account, gboolean banned, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) salt = NULL;
    g_autoptr(GVariant) hash = NULL;
    g_autofree gchar *key = NULL;
    guint64 revision, generation;
    gboolean old_ban;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), FALSE);
    value = load_account (self, account, &revision, error);
    if (value == NULL || !account_flags (value, &generation, &old_ban, error))
        return FALSE;
    if (generation == G_MAXUINT64)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Account generation exhausted");
    salt = g_variant_get_child_value (value, 0);
    hash = g_variant_get_child_value (value, 1);
    key = account_key (account);
    return _lrg_mmo_put (self->store, key, revision,
                         g_variant_new ("(@ay@aytb)", salt, hash,
                                        generation + 1, banned), error);
}

gchar *
lrg_mmo_auth_begin_recovery (LrgMmoAuth *self, const gchar *account, gint64 now, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    guint64 revision, generation;
    gboolean banned;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), NULL);
    value = load_account (self, account, &revision, error);
    if (value == NULL || !account_flags (value, &generation, &banned, error))
        return NULL;
    if (banned)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Account unavailable");
        return NULL;
    }
    return issue (self, account, generation, now, TRUE, error);
}

gboolean
lrg_mmo_auth_recover (LrgMmoAuth *self, const gchar *token, const gchar *password,
                      gint64 now, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) account_value = NULL;
    g_autoptr(GVariant) updated = NULL;
    g_autoptr(GVariant) batch = NULL;
    g_autofree gchar *key = NULL;
    g_autofree gchar *reset_key = NULL;
    const gchar *account;
    guint64 revision, generation, current, account_revision;
    gboolean banned;
    gint64 expiry;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_AUTH (self), FALSE);
    value = verify_token (self, token, TRUE, now, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&stx)", &account, &generation, &expiry);
    account_value = load_account (self, account, &account_revision, error);
    if (account_value == NULL || !account_flags (account_value, &current, &banned, error))
        return FALSE;
    if (generation != current || banned || generation == G_MAXUINT64)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Recovery no longer authorized");
    updated = credential (password, generation + 1, FALSE, error);
    if (updated == NULL)
        return FALSE;
    key = account_key (account);
    reset_key = token_key (token, TRUE);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, account_revision, updated);
    _lrg_mmo_change (&builder, reset_key, revision, g_variant_new ("(stx)", account, generation, (gint64) 0));
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}
