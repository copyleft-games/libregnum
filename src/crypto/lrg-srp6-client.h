/* lrg-srp6-client.h - SRP-6a password-authenticated key exchange, client side
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * SRP-6a lets a client prove it knows a password to a server that only
 * stores a verifier, and leaves both with a shared session key, without
 * the password or anything equivalent crossing the wire.  Protocols
 * agree on the math but differ in the details (how integers are encoded
 * before hashing, the multiplier k, how the session key is derived), so
 * #LrgSrp6Client takes an #LrgSrp6Style that fixes those details.  See
 * docs/modules/crypto/index.org for the exact formulas of each style.
 */

#ifndef LRG_SRP6_CLIENT_H
#define LRG_SRP6_CLIENT_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-num.h"

G_BEGIN_DECLS

/**
 * LRG_SRP6_ERROR:
 *
 * Error domain for #LrgSrp6Client.  Errors carry an #LrgSrp6Error code.
 */
#define LRG_SRP6_ERROR (lrg_srp6_error_quark ())

/**
 * LrgSrp6Error:
 * @LRG_SRP6_ERROR_NOT_READY: a step was attempted before its inputs were
 *   set (parameters, identity, or the server's challenge)
 * @LRG_SRP6_ERROR_INVALID_PARAMETERS: the group is unusable (N < 3, or g
 *   not in [2, N))
 * @LRG_SRP6_ERROR_BAD_SERVER_KEY: the server's public key B is zero
 *   modulo N or not below N; continuing would let a forged server
 *   force a known session key
 * @LRG_SRP6_ERROR_BAD_SCRAMBLER: the scrambling parameter u hashed to
 *   zero, which would remove the password from the session key
 * @LRG_SRP6_ERROR_BAD_SERVER_PROOF: the server's proof M2 does not
 *   match; the server does not know the verifier
 * @LRG_SRP6_ERROR_FAILED: another failure (the random generator)
 *
 * Error codes for the %LRG_SRP6_ERROR domain.
 */
typedef enum
{
    LRG_SRP6_ERROR_NOT_READY,
    LRG_SRP6_ERROR_INVALID_PARAMETERS,
    LRG_SRP6_ERROR_BAD_SERVER_KEY,
    LRG_SRP6_ERROR_BAD_SCRAMBLER,
    LRG_SRP6_ERROR_BAD_SERVER_PROOF,
    LRG_SRP6_ERROR_FAILED
} LrgSrp6Error;

LRG_AVAILABLE_IN_ALL
GQuark lrg_srp6_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_srp6_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SRP6_ERROR (lrg_srp6_error_get_type ())

/**
 * LrgSrp6Style:
 * @LRG_SRP6_STYLE_RFC5054: SRP-6a as in RFC 5054 and RFC 2945, with
 *   big-endian integers: k = H(N | PAD(g)), u = H(PAD(A) | PAD(B)),
 *   x = H(s | H(I ":" P)), K = H(S), M1 = H(H(N) xor H(g) | H(I) | s |
 *   A | B | K), M2 = H(A | M1 | K).  PAD() pads to the length of N;
 *   in K, M1 and M2 integers are written without leading zeros.
 * @LRG_SRP6_STYLE_WOW: the World of Warcraft login ("Grunt") variant:
 *   integers are little-endian on the wire and in every hash, A, B, N
 *   and S are written at the length of N, k = 3, the username and
 *   password are upper-cased (ASCII) before hashing, and K is the
 *   interleaved hash of S (lrg_srp6_hash_interleave()), twice the
 *   digest length.  M1 and M2 have the RFC 2945 shape.
 *
 * Selects how an #LrgSrp6Client encodes integers and derives k, K, M1
 * and M2.
 */
typedef enum
{
    LRG_SRP6_STYLE_RFC5054,
    LRG_SRP6_STYLE_WOW
} LrgSrp6Style;

LRG_AVAILABLE_IN_ALL
GType lrg_srp6_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SRP6_STYLE (lrg_srp6_style_get_type ())

#define LRG_TYPE_SRP6_CLIENT (lrg_srp6_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSrp6Client, lrg_srp6_client, LRG, SRP6_CLIENT, GObject)

/**
 * lrg_srp6_client_new:
 * @style: the protocol variant
 *
 * Creates a client using SHA-1 (both RFC 5054 and World of Warcraft
 * use it; see lrg_srp6_client_set_hash_type()).
 *
 * The usual sequence is: lrg_srp6_client_set_identity(),
 * lrg_srp6_client_set_parameters() with the server's N and g,
 * lrg_srp6_client_process_challenge() with the server's salt and B,
 * then send lrg_srp6_client_get_public_key() (A) and
 * lrg_srp6_client_get_client_proof() (M1), and finally check the
 * server's answer with lrg_srp6_client_verify_server_proof().
 *
 * Returns: (transfer full): a new client
 */
LRG_AVAILABLE_IN_ALL
LrgSrp6Client *lrg_srp6_client_new (LrgSrp6Style style);

/**
 * lrg_srp6_client_get_style:
 * @self: an #LrgSrp6Client
 *
 * Returns: the protocol variant
 */
LRG_AVAILABLE_IN_ALL
LrgSrp6Style lrg_srp6_client_get_style (LrgSrp6Client *self);

/**
 * lrg_srp6_client_set_hash_type:
 * @self: an #LrgSrp6Client
 * @hash_type: the hash function H
 *
 * Sets H for every hash in the exchange.  Changing it discards an
 * identity set with lrg_srp6_client_set_identity() (it was hashed with
 * the old function) and any computed results, so set it first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_hash_type (LrgSrp6Client *self,
                                    GChecksumType  hash_type);

/**
 * lrg_srp6_client_get_hash_type:
 * @self: an #LrgSrp6Client
 *
 * Returns: the hash function H
 */
LRG_AVAILABLE_IN_ALL
GChecksumType lrg_srp6_client_get_hash_type (LrgSrp6Client *self);

/**
 * lrg_srp6_client_set_parameters:
 * @self: an #LrgSrp6Client
 * @modulus: the safe prime N
 * @generator: the generator g
 *
 * Sets the group.  The length of N in bytes is the width A, B and S
 * are padded to.  Discards A, a randomly drawn private key (not one set
 * with lrg_srp6_client_set_private_key()) and any results.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_parameters (LrgSrp6Client *self,
                                     LrgBigNum     *modulus,
                                     LrgBigNum     *generator);

/**
 * lrg_srp6_client_set_multiplier:
 * @self: an #LrgSrp6Client
 * @multiplier: (nullable): the multiplier k, or %NULL for the style's
 *   default (H(N | PAD(g)) for RFC 5054, 3 for World of Warcraft)
 *
 * Overrides k, for protocols that use SRP-6 (k = 3) with otherwise
 * RFC 5054 encodings or a fixed k of their own.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_multiplier (LrgSrp6Client *self,
                                     LrgBigNum     *multiplier);

/**
 * lrg_srp6_client_set_identity:
 * @self: an #LrgSrp6Client
 * @username: the username I
 * @password: the password P
 *
 * Hashes H(I ":" P) at once; the password itself is not kept.  In
 * %LRG_SRP6_STYLE_WOW both strings are upper-cased first with
 * g_ascii_strup(): ASCII letters only, other bytes (UTF-8 sequences)
 * unchanged.  Callers that need another normalisation should apply it
 * before calling.  Discards any results.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_identity (LrgSrp6Client *self,
                                   const gchar   *username,
                                   const gchar   *password);

/**
 * lrg_srp6_client_set_identity_hash:
 * @self: an #LrgSrp6Client
 * @username: the username I
 * @identity_hash: H(I ":" P), as many bytes as the hash's digest
 *
 * Like lrg_srp6_client_set_identity() for callers that store the
 * credential hash instead of the password (a "remember password"
 * option).  In %LRG_SRP6_STYLE_WOW @username is upper-cased for H(I);
 * @identity_hash must already be the hash of the upper-cased strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_identity_hash (LrgSrp6Client *self,
                                        const gchar   *username,
                                        GBytes        *identity_hash);

/**
 * lrg_srp6_client_set_private_key:
 * @self: an #LrgSrp6Client
 * @private_key: (nullable): the ephemeral secret a, or %NULL to draw a
 *   fresh random one when needed
 *
 * Fixes a, for deterministic tests and test vectors.  Never reuse an a
 * across real exchanges.  Discards any results.
 */
LRG_AVAILABLE_IN_ALL
void lrg_srp6_client_set_private_key (LrgSrp6Client *self,
                                      LrgBigNum     *private_key);

/**
 * lrg_srp6_client_generate_public_key:
 * @self: an #LrgSrp6Client
 * @error: return location for a #GError
 *
 * Computes A = g^a mod N, drawing a random a (at least 256 bits and at
 * least as wide as N) unless one was set.  Protocols where the client
 * speaks first call this before lrg_srp6_client_get_public_key();
 * lrg_srp6_client_process_challenge() calls it when needed.  Needs the
 * parameters.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_srp6_client_generate_public_key (LrgSrp6Client  *self,
                                              GError        **error);

/**
 * lrg_srp6_client_process_challenge:
 * @self: an #LrgSrp6Client
 * @salt: the salt s, used exactly as the server sent it
 * @server_public_key: the server's public key B, in the style's byte
 *   order
 * @error: return location for a #GError
 *
 * Computes A (if not yet done), u, x, S = (B - k * g^x)^(a + u * x)
 * mod N, the session key K, the client proof M1 and the expected server
 * proof M2.  Fails with %LRG_SRP6_ERROR_BAD_SERVER_KEY when B mod N is
 * zero or B is not below N, and with %LRG_SRP6_ERROR_BAD_SCRAMBLER when
 * u is zero; nothing is computed then.  Needs the parameters and an
 * identity.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_srp6_client_process_challenge (LrgSrp6Client  *self,
                                            GBytes         *salt,
                                            GBytes         *server_public_key,
                                            GError        **error);

/**
 * lrg_srp6_client_get_public_key:
 * @self: an #LrgSrp6Client
 *
 * The client's public key A, in the style's byte order, zero-padded to
 * the length of N: the form sent on the wire and hashed into u.
 *
 * Returns: (transfer full) (nullable): A, or %NULL before it is
 *   computed
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_srp6_client_get_public_key (LrgSrp6Client *self);

/**
 * lrg_srp6_client_get_client_proof:
 * @self: an #LrgSrp6Client
 *
 * Returns: (transfer full) (nullable): M1 (one digest), or %NULL before
 *   lrg_srp6_client_process_challenge() succeeded
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_srp6_client_get_client_proof (LrgSrp6Client *self);

/**
 * lrg_srp6_client_get_session_key:
 * @self: an #LrgSrp6Client
 *
 * The shared session key K: one digest in %LRG_SRP6_STYLE_RFC5054, two
 * (40 bytes with SHA-1) in %LRG_SRP6_STYLE_WOW.  Protocols derive their
 * packet-encryption and signing keys from it.  It is a secret: wipe
 * copies when done.
 *
 * Returns: (transfer full) (nullable): K, or %NULL before
 *   lrg_srp6_client_process_challenge() succeeded
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_srp6_client_get_session_key (LrgSrp6Client *self);

/**
 * lrg_srp6_client_verify_server_proof:
 * @self: an #LrgSrp6Client
 * @server_proof: the server's M2
 * @error: return location for a #GError
 *
 * Compares @server_proof with H(A | M1 | K) in constant time.  Only
 * after this succeeds has the server proven it holds the verifier.
 *
 * Returns: %TRUE if the proof matches; otherwise %FALSE with
 *   %LRG_SRP6_ERROR_BAD_SERVER_PROOF (or %LRG_SRP6_ERROR_NOT_READY
 *   before lrg_srp6_client_process_challenge())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_srp6_client_verify_server_proof (LrgSrp6Client  *self,
                                              GBytes         *server_proof,
                                              GError        **error);

/**
 * lrg_srp6_hash_interleave:
 * @hash_type: the hash function H
 * @secret: the secret, least significant byte first as in
 *   %LRG_SRP6_STYLE_WOW
 *
 * The interleaved hash ("SHA_Interleave" of RFC 2945 section 3.1,
 * byte-reversed as World of Warcraft uses it): skip the zero bytes at
 * the start of @secret, then one more byte if an odd number remain;
 * hash the bytes at even offsets and those at odd offsets separately;
 * the result alternates bytes of the two digests, even digest first.
 *
 * Returns: (transfer full): twice the digest length of @hash_type
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_srp6_hash_interleave (GChecksumType  hash_type,
                                  GBytes        *secret);

G_END_DECLS

#endif /* LRG_SRP6_CLIENT_H */
