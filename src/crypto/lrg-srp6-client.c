/* lrg-srp6-client.c - SRP-6a password-authenticated key exchange, client side
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * All arithmetic goes through LrgBigNum, so every intermediate secret
 * (x, v, S, the exponent a + u*x) is wiped when it is released.  Byte
 * results (K, M1, M2) live in fixed buffers in the instance that are
 * wiped on reset and finalize; getters hand out copies.
 *
 * Encoding rules per style (see docs/modules/crypto/index.org):
 *
 *   RFC5054: big-endian; k and u hash PAD()ded values (length of N);
 *            K, M1 and M2 hash minimal-length values.
 *   WOW:     little-endian; A, B, N and S always at the length of N,
 *            g at its natural length; hash digests read back as
 *            little-endian integers.
 */

#include "config.h"
#include "lrg-srp6-client.h"

#include <string.h>

#include <openssl/crypto.h>

/* Largest GChecksum digest (SHA-512). */
#define MAX_DIGEST (64)

struct _LrgSrp6Client
{
    GObject        parent_instance;

    LrgSrp6Style   style;
    GChecksumType  hash_type;

    LrgBigNum     *modulus;
    LrgBigNum     *generator;
    LrgBigNum     *multiplier;          /* NULL: the style's default */

    gchar         *username;            /* normalised for the style */
    guint8         identity_hash[MAX_DIGEST];
    gsize          identity_hash_len;   /* 0: no identity */

    LrgBigNum     *private_key;         /* a */
    gboolean       private_key_fixed;   /* set by the caller */
    LrgBigNum     *public_key;          /* A */

    gboolean       have_results;
    gsize          digest_len;
    guint8         session_key[2 * MAX_DIGEST];
    gsize          session_key_len;
    guint8         client_proof[MAX_DIGEST];
    guint8         server_proof[MAX_DIGEST];
};

enum
{
    PROP_0,
    PROP_STYLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_QUARK (lrg-srp6-error-quark, lrg_srp6_error)

G_DEFINE_ENUM_TYPE (LrgSrp6Error, lrg_srp6_error,
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_NOT_READY, "not-ready"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_INVALID_PARAMETERS, "invalid-parameters"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_BAD_SERVER_KEY, "bad-server-key"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_BAD_SCRAMBLER, "bad-scrambler"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_BAD_SERVER_PROOF, "bad-server-proof"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_ERROR_FAILED, "failed"))

G_DEFINE_ENUM_TYPE (LrgSrp6Style, lrg_srp6_style,
    G_DEFINE_ENUM_VALUE (LRG_SRP6_STYLE_RFC5054, "rfc5054"),
    G_DEFINE_ENUM_VALUE (LRG_SRP6_STYLE_WOW, "wow"))

G_DEFINE_FINAL_TYPE (LrgSrp6Client, lrg_srp6_client, G_TYPE_OBJECT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * style_order:
 * The byte order integers take on the wire and inside hashes.
 */
static LrgByteOrder
style_order (LrgSrp6Client *self)
{
    if (self->style == LRG_SRP6_STYLE_WOW)
        return LRG_BYTE_ORDER_LITTLE_ENDIAN;
    return LRG_BYTE_ORDER_BIG_ENDIAN;
}

/*
 * modulus_len:
 * The width, in bytes, that A, B and S are padded to.
 */
static gsize
modulus_len (LrgSrp6Client *self)
{
    return lrg_big_num_get_num_bytes (self->modulus);
}

/*
 * hash_num:
 * Feeds @n into @cs, written in @order and padded to @min_len.  The
 * temporary encoding is wiped since it may be a secret (S).
 */
static void
hash_num (GChecksum    *cs,
          LrgBigNum    *n,
          LrgByteOrder  order,
          gsize         min_len)
{
    guint8 *buf;
    gsize len;

    buf = lrg_big_num_to_data (n, order, min_len, &len);
    g_checksum_update (cs, buf, (gssize) len);
    OPENSSL_cleanse (buf, len);
    g_free (buf);
}

/*
 * hash_finish:
 * Writes the digest of @cs into @out (at least MAX_DIGEST bytes) and
 * frees @cs.
 */
static void
hash_finish (GChecksum *cs,
             guint8    *out)
{
    gsize len;

    len = MAX_DIGEST;
    g_checksum_get_digest (cs, out, &len);
    g_checksum_free (cs);
}

/*
 * hash_data:
 * H(@data) into @out.
 */
static void
hash_data (GChecksumType  type,
           const guint8  *data,
           gsize          length,
           guint8        *out)
{
    GChecksum *cs;

    cs = g_checksum_new (type);
    g_checksum_update (cs, data, (gssize) length);
    hash_finish (cs, out);
}

/*
 * interleave:
 * The interleaved hash of @in into @out (2 * digest bytes).  See
 * lrg_srp6_hash_interleave() for the rules.
 */
static void
interleave (GChecksumType  type,
            const guint8  *in,
            gsize          length,
            guint8        *out)
{
    g_autofree guint8 *half_buf = NULL;
    guint8 digest[MAX_DIGEST];
    gsize digest_len;
    gsize half;
    gsize i;

    /* Skip the zero bytes at the start, then one more byte when an odd
     * count remains, so the rest splits into equal halves. */
    while (length > 0 && in[0] == 0)
    {
        in++;
        length--;
    }
    if (length & 1)
    {
        in++;
        length--;
    }
    half = length / 2;
    half_buf = g_malloc (MAX (half, 1));
    digest_len = (gsize) g_checksum_type_get_length (type);

    /* Even offsets feed the first digest, which lands on the even
     * output bytes; odd offsets likewise on the odd output bytes. */
    for (i = 0; i < half; i++)
        half_buf[i] = in[2 * i];
    hash_data (type, half_buf, half, digest);
    for (i = 0; i < digest_len; i++)
        out[2 * i] = digest[i];

    for (i = 0; i < half; i++)
        half_buf[i] = in[2 * i + 1];
    hash_data (type, half_buf, half, digest);
    for (i = 0; i < digest_len; i++)
        out[2 * i + 1] = digest[i];

    OPENSSL_cleanse (half_buf, MAX (half, 1));
    OPENSSL_cleanse (digest, sizeof digest);
}

/*
 * clear_results:
 * Forgets K, M1 and M2.
 */
static void
clear_results (LrgSrp6Client *self)
{
    self->have_results = FALSE;
    OPENSSL_cleanse (self->session_key, sizeof self->session_key);
    OPENSSL_cleanse (self->client_proof, sizeof self->client_proof);
    OPENSSL_cleanse (self->server_proof, sizeof self->server_proof);
    self->session_key_len = 0;
}

/*
 * clear_keys:
 * Forgets A and, unless the caller fixed it, a.
 */
static void
clear_keys (LrgSrp6Client *self)
{
    g_clear_pointer (&self->public_key, lrg_big_num_unref);
    if (!self->private_key_fixed)
        g_clear_pointer (&self->private_key, lrg_big_num_unref);
}

/*
 * clear_identity:
 * Forgets the username and H(I ":" P).
 */
static void
clear_identity (LrgSrp6Client *self)
{
    g_clear_pointer (&self->username, g_free);
    OPENSSL_cleanse (self->identity_hash, sizeof self->identity_hash);
    self->identity_hash_len = 0;
}

/*
 * normalise:
 * The string as the style hashes it: upper-cased ASCII for WOW,
 * unchanged otherwise.
 */
static gchar *
normalise (LrgSrp6Client *self,
           const gchar   *str)
{
    if (self->style == LRG_SRP6_STYLE_WOW)
        return g_ascii_strup (str, -1);
    return g_strdup (str);
}

/*
 * check_parameters:
 * TRUE when N and g are set and usable: N >= 3 and 2 <= g < N.
 */
static gboolean
check_parameters (LrgSrp6Client  *self,
                  GError        **error)
{
    g_autoptr(LrgBigNum) two = NULL;

    if (self->modulus == NULL || self->generator == NULL)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY,
                             "SRP-6 parameters (N, g) are not set");
        return FALSE;
    }
    two = lrg_big_num_new_from_uint64 (2);
    if (lrg_big_num_compare (self->modulus, two) <= 0 ||
        lrg_big_num_compare (self->generator, two) < 0 ||
        lrg_big_num_compare (self->generator, self->modulus) >= 0)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_INVALID_PARAMETERS,
                             "SRP-6 parameters are invalid (need N >= 3 and 2 <= g < N)");
        return FALSE;
    }
    return TRUE;
}

/*
 * default_multiplier:
 * k for the style: 3 for WOW, H(N | PAD(g)) for RFC 5054.
 */
static LrgBigNum *
default_multiplier (LrgSrp6Client *self)
{
    guint8 digest[MAX_DIGEST];
    GChecksum *cs;
    gsize n_len;

    if (self->style == LRG_SRP6_STYLE_WOW)
        return lrg_big_num_new_from_uint64 (3);

    n_len = modulus_len (self);
    cs = g_checksum_new (self->hash_type);
    hash_num (cs, self->modulus, LRG_BYTE_ORDER_BIG_ENDIAN, n_len);
    hash_num (cs, self->generator, LRG_BYTE_ORDER_BIG_ENDIAN, n_len);
    hash_finish (cs, digest);
    return lrg_big_num_new_from_data (digest, self->digest_len, LRG_BYTE_ORDER_BIG_ENDIAN);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_srp6_client_finalize (GObject *object)
{
    LrgSrp6Client *self = LRG_SRP6_CLIENT (object);

    clear_results (self);
    clear_identity (self);
    self->private_key_fixed = FALSE;
    clear_keys (self);
    g_clear_pointer (&self->modulus, lrg_big_num_unref);
    g_clear_pointer (&self->generator, lrg_big_num_unref);
    g_clear_pointer (&self->multiplier, lrg_big_num_unref);

    G_OBJECT_CLASS (lrg_srp6_client_parent_class)->finalize (object);
}

static void
lrg_srp6_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgSrp6Client *self = LRG_SRP6_CLIENT (object);

    switch (prop_id)
    {
    case PROP_STYLE:
        g_value_set_enum (value, self->style);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_srp6_client_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgSrp6Client *self = LRG_SRP6_CLIENT (object);

    switch (prop_id)
    {
    case PROP_STYLE:
        self->style = (LrgSrp6Style) g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_srp6_client_class_init (LrgSrp6ClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_srp6_client_finalize;
    object_class->get_property = lrg_srp6_client_get_property;
    object_class->set_property = lrg_srp6_client_set_property;

    /**
     * LrgSrp6Client:style:
     *
     * The protocol variant: integer encodings and the derivation of k,
     * K, M1 and M2.
     */
    properties[PROP_STYLE] =
        g_param_spec_enum ("style", "Style", "The SRP-6 protocol variant",
                           LRG_TYPE_SRP6_STYLE, LRG_SRP6_STYLE_RFC5054,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_srp6_client_init (LrgSrp6Client *self)
{
    self->style = LRG_SRP6_STYLE_RFC5054;
    self->hash_type = G_CHECKSUM_SHA1;
    self->digest_len = (gsize) g_checksum_type_get_length (G_CHECKSUM_SHA1);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSrp6Client *
lrg_srp6_client_new (LrgSrp6Style style)
{
    return g_object_new (LRG_TYPE_SRP6_CLIENT, "style", style, NULL);
}

LrgSrp6Style
lrg_srp6_client_get_style (LrgSrp6Client *self)
{
    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), LRG_SRP6_STYLE_RFC5054);
    return self->style;
}

void
lrg_srp6_client_set_hash_type (LrgSrp6Client *self,
                               GChecksumType  hash_type)
{
    gssize len;

    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));
    len = g_checksum_type_get_length (hash_type);
    g_return_if_fail (len > 0 && len <= MAX_DIGEST);

    if (hash_type == self->hash_type)
        return;
    self->hash_type = hash_type;
    self->digest_len = (gsize) len;
    clear_identity (self);
    clear_results (self);
}

GChecksumType
lrg_srp6_client_get_hash_type (LrgSrp6Client *self)
{
    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), G_CHECKSUM_SHA1);
    return self->hash_type;
}

void
lrg_srp6_client_set_parameters (LrgSrp6Client *self,
                                LrgBigNum     *modulus,
                                LrgBigNum     *generator)
{
    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));
    g_return_if_fail (modulus != NULL);
    g_return_if_fail (generator != NULL);

    /* Ref before dropping, in case the caller passes the current ones. */
    lrg_big_num_ref (modulus);
    lrg_big_num_ref (generator);
    g_clear_pointer (&self->modulus, lrg_big_num_unref);
    g_clear_pointer (&self->generator, lrg_big_num_unref);
    self->modulus = modulus;
    self->generator = generator;
    clear_keys (self);
    clear_results (self);
}

void
lrg_srp6_client_set_multiplier (LrgSrp6Client *self,
                                LrgBigNum     *multiplier)
{
    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));

    if (multiplier != NULL)
        lrg_big_num_ref (multiplier);
    g_clear_pointer (&self->multiplier, lrg_big_num_unref);
    self->multiplier = multiplier;
    clear_results (self);
}

void
lrg_srp6_client_set_identity (LrgSrp6Client *self,
                              const gchar   *username,
                              const gchar   *password)
{
    g_autofree gchar *user = NULL;
    gchar *pass;
    GChecksum *cs;

    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));
    g_return_if_fail (username != NULL);
    g_return_if_fail (password != NULL);

    user = normalise (self, username);
    pass = normalise (self, password);

    /* H(I ":" P), then wipe the normalised password copy. */
    cs = g_checksum_new (self->hash_type);
    g_checksum_update (cs, (const guchar *) user, (gssize) strlen (user));
    g_checksum_update (cs, (const guchar *) ":", 1);
    g_checksum_update (cs, (const guchar *) pass, (gssize) strlen (pass));
    OPENSSL_cleanse (pass, strlen (pass));
    g_free (pass);

    clear_identity (self);
    hash_finish (cs, self->identity_hash);
    self->identity_hash_len = self->digest_len;
    self->username = g_steal_pointer (&user);
    clear_results (self);
}

void
lrg_srp6_client_set_identity_hash (LrgSrp6Client *self,
                                   const gchar   *username,
                                   GBytes        *identity_hash)
{
    const guint8 *data;
    gsize len;

    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));
    g_return_if_fail (username != NULL);
    g_return_if_fail (identity_hash != NULL);
    data = g_bytes_get_data (identity_hash, &len);
    g_return_if_fail (len == self->digest_len);

    clear_identity (self);
    memcpy (self->identity_hash, data, len);
    self->identity_hash_len = len;
    self->username = normalise (self, username);
    clear_results (self);
}

void
lrg_srp6_client_set_private_key (LrgSrp6Client *self,
                                 LrgBigNum     *private_key)
{
    g_return_if_fail (LRG_IS_SRP6_CLIENT (self));

    if (private_key != NULL)
        lrg_big_num_ref (private_key);
    self->private_key_fixed = FALSE;
    clear_keys (self);
    self->private_key = private_key;
    self->private_key_fixed = (private_key != NULL);
    clear_results (self);
}

gboolean
lrg_srp6_client_generate_public_key (LrgSrp6Client  *self,
                                     GError        **error)
{
    g_autoptr(GError) local_error = NULL;
    LrgBigNum *a;
    LrgBigNum *A;
    guint n_bits;

    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->public_key != NULL)
        return TRUE;
    if (!check_parameters (self, error))
        return FALSE;

    /* Draw a when the caller did not fix one.  A zero a (or one that
     * gives A = 1) is astronomically unlikely but would be fatal to
     * secrecy, so draw again. */
    n_bits = MAX (256u, lrg_big_num_get_num_bits (self->modulus));
    for (;;)
    {
        if (self->private_key == NULL)
        {
            a = lrg_big_num_new_random (n_bits, &local_error);
            if (a == NULL)
            {
                g_set_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_FAILED,
                             "Cannot draw the SRP-6 private key: %s",
                             local_error->message);
                return FALSE;
            }
            if (lrg_big_num_is_zero (a))
            {
                lrg_big_num_unref (a);
                continue;
            }
            self->private_key = a;
        }

        A = lrg_big_num_mod_exp (self->generator, self->private_key,
                                 self->modulus, error);
        if (A == NULL)
            return FALSE;
        if (lrg_big_num_get_num_bits (A) > 1 || self->private_key_fixed)
            break;
        /* A is 0 or 1: discard the random a and try again. */
        lrg_big_num_unref (A);
        g_clear_pointer (&self->private_key, lrg_big_num_unref);
    }
    self->public_key = A;
    return TRUE;
}

gboolean
lrg_srp6_client_process_challenge (LrgSrp6Client  *self,
                                   GBytes         *salt,
                                   GBytes         *server_public_key,
                                   GError        **error)
{
    g_autoptr(LrgBigNum) B = NULL;
    g_autoptr(LrgBigNum) x = NULL;
    g_autoptr(LrgBigNum) u = NULL;
    g_autoptr(LrgBigNum) k = NULL;
    g_autoptr(LrgBigNum) v = NULL;
    g_autoptr(LrgBigNum) kv = NULL;
    g_autoptr(LrgBigNum) base = NULL;
    g_autoptr(LrgBigNum) ux = NULL;
    g_autoptr(LrgBigNum) exponent = NULL;
    g_autoptr(LrgBigNum) S = NULL;
    guint8 digest[MAX_DIGEST];
    guint8 hash_g[MAX_DIGEST];
    guint8 *s_bytes;
    const guint8 *salt_data;
    LrgByteOrder order;
    GChecksum *cs;
    gsize salt_len;
    gsize n_len;
    gsize s_len;
    gsize hash_n_len;
    gsize i;

    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), FALSE);
    g_return_val_if_fail (salt != NULL, FALSE);
    g_return_val_if_fail (server_public_key != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    clear_results (self);

    /* --- inputs ---------------------------------------------------- */
    if (!check_parameters (self, error))
        return FALSE;
    if (self->identity_hash_len == 0)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY,
                             "SRP-6 identity is not set");
        return FALSE;
    }
    order = style_order (self);
    n_len = modulus_len (self);
    salt_data = g_bytes_get_data (salt, &salt_len);

    /* B must be a non-zero residue: B mod N == 0 would make S
     * independent of the password, so a fake server could impersonate
     * any account.  Values >= N are not valid encodings either. */
    B = lrg_big_num_new_from_bytes (server_public_key, order);
    if (lrg_big_num_is_zero (B) || lrg_big_num_compare (B, self->modulus) >= 0)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_KEY,
                             "Server public key B is zero modulo N or not below N");
        return FALSE;
    }

    if (!lrg_srp6_client_generate_public_key (self, error))
        return FALSE;

    /* --- u = H(PAD(A) | PAD(B)) ------------------------------------ */
    cs = g_checksum_new (self->hash_type);
    hash_num (cs, self->public_key, order, n_len);
    hash_num (cs, B, order, n_len);
    hash_finish (cs, digest);
    u = lrg_big_num_new_from_data (digest, self->digest_len, order);
    if (lrg_big_num_is_zero (u))
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SCRAMBLER,
                             "Scrambling parameter u is zero");
        return FALSE;
    }

    /* --- x = H(s | H(I ":" P)) ------------------------------------- */
    cs = g_checksum_new (self->hash_type);
    g_checksum_update (cs, salt_data, (gssize) salt_len);
    g_checksum_update (cs, self->identity_hash, (gssize) self->identity_hash_len);
    hash_finish (cs, digest);
    x = lrg_big_num_new_from_data (digest, self->digest_len, order);
    OPENSSL_cleanse (digest, sizeof digest);

    /* --- S = (B - k * g^x) ^ (a + u * x) mod N ---------------------- */
    if (self->multiplier != NULL)
        k = lrg_big_num_ref (self->multiplier);
    else
        k = default_multiplier (self);
    v = lrg_big_num_mod_exp (self->generator, x, self->modulus, error);
    if (v == NULL)
        return FALSE;
    kv = lrg_big_num_mod_mul (k, v, self->modulus, error);
    if (kv == NULL)
        return FALSE;
    base = lrg_big_num_mod_sub (B, kv, self->modulus, error);
    if (base == NULL)
        return FALSE;
    ux = lrg_big_num_mul (u, x);
    exponent = lrg_big_num_add (self->private_key, ux);
    S = lrg_big_num_mod_exp (base, exponent, self->modulus, error);
    if (S == NULL)
        return FALSE;

    /* --- K ---------------------------------------------------------- */
    if (self->style == LRG_SRP6_STYLE_WOW)
    {
        s_bytes = lrg_big_num_to_data (S, order, n_len, &s_len);
        interleave (self->hash_type, s_bytes, s_len, self->session_key);
        self->session_key_len = 2 * self->digest_len;
    }
    else
    {
        s_bytes = lrg_big_num_to_data (S, order, 0, &s_len);
        hash_data (self->hash_type, s_bytes, s_len, self->session_key);
        self->session_key_len = self->digest_len;
    }
    OPENSSL_cleanse (s_bytes, s_len);
    g_free (s_bytes);

    /* --- M1 = H(H(N) xor H(g) | H(I) | s | A | B | K) --------------- *
     * WOW writes N, A and B at the length of N; RFC 5054 (RFC 2945)
     * writes them without leading zeros.  g is always natural length. */
    hash_n_len = (self->style == LRG_SRP6_STYLE_WOW) ? n_len : 0;

    cs = g_checksum_new (self->hash_type);
    hash_num (cs, self->modulus, order, hash_n_len);
    hash_finish (cs, digest);
    cs = g_checksum_new (self->hash_type);
    hash_num (cs, self->generator, order, 0);
    hash_finish (cs, hash_g);
    for (i = 0; i < self->digest_len; i++)
        digest[i] ^= hash_g[i];

    cs = g_checksum_new (self->hash_type);
    g_checksum_update (cs, digest, (gssize) self->digest_len);
    hash_data (self->hash_type, (const guint8 *) self->username,
               strlen (self->username), digest);
    g_checksum_update (cs, digest, (gssize) self->digest_len);
    g_checksum_update (cs, salt_data, (gssize) salt_len);
    hash_num (cs, self->public_key, order, hash_n_len);
    hash_num (cs, B, order, hash_n_len);
    g_checksum_update (cs, self->session_key, (gssize) self->session_key_len);
    hash_finish (cs, self->client_proof);

    /* --- M2 = H(A | M1 | K) ----------------------------------------- */
    cs = g_checksum_new (self->hash_type);
    hash_num (cs, self->public_key, order, hash_n_len);
    g_checksum_update (cs, self->client_proof, (gssize) self->digest_len);
    g_checksum_update (cs, self->session_key, (gssize) self->session_key_len);
    hash_finish (cs, self->server_proof);

    self->have_results = TRUE;
    return TRUE;
}

GBytes *
lrg_srp6_client_get_public_key (LrgSrp6Client *self)
{
    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), NULL);

    if (self->public_key == NULL)
        return NULL;
    return lrg_big_num_to_bytes (self->public_key, style_order (self), modulus_len (self));
}

GBytes *
lrg_srp6_client_get_client_proof (LrgSrp6Client *self)
{
    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), NULL);

    if (!self->have_results)
        return NULL;
    return g_bytes_new (self->client_proof, self->digest_len);
}

GBytes *
lrg_srp6_client_get_session_key (LrgSrp6Client *self)
{
    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), NULL);

    if (!self->have_results)
        return NULL;
    return g_bytes_new (self->session_key, self->session_key_len);
}

gboolean
lrg_srp6_client_verify_server_proof (LrgSrp6Client  *self,
                                     GBytes         *server_proof,
                                     GError        **error)
{
    const guint8 *data;
    gsize len;

    g_return_val_if_fail (LRG_IS_SRP6_CLIENT (self), FALSE);
    g_return_val_if_fail (server_proof != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!self->have_results)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY,
                             "No SRP-6 exchange has been computed");
        return FALSE;
    }
    data = g_bytes_get_data (server_proof, &len);
    /* The length is public; the contents are compared in constant time. */
    if (len != self->digest_len ||
        CRYPTO_memcmp (data, self->server_proof, len) != 0)
    {
        g_set_error_literal (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_PROOF,
                             "Server proof M2 does not match");
        return FALSE;
    }
    return TRUE;
}

GBytes *
lrg_srp6_hash_interleave (GChecksumType  hash_type,
                          GBytes        *secret)
{
    guint8 out[2 * MAX_DIGEST];
    const guint8 *data;
    gssize digest_len;
    GBytes *result;
    gsize len;

    g_return_val_if_fail (secret != NULL, NULL);
    digest_len = g_checksum_type_get_length (hash_type);
    g_return_val_if_fail (digest_len > 0 && digest_len <= MAX_DIGEST, NULL);

    data = g_bytes_get_data (secret, &len);
    interleave (hash_type, data, len, out);
    result = g_bytes_new (out, 2 * (gsize) digest_len);
    OPENSSL_cleanse (out, sizeof out);
    return result;
}
