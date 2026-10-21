/* lrg-arc4.h - The ARC4 (RC4) stream cipher
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * ARC4 is broken as a general-purpose cipher and must not be used to
 * protect new protocols.  It exists here because legacy game protocols
 * need it bit-for-bit: World of Warcraft 3.x, for example, encrypts
 * world-packet headers with ARC4 keyed from the SRP-6 session key and
 * discards the first 1024 keystream bytes (lrg_arc4_new_drop()).
 * OpenSSL 3 moved RC4 into its legacy provider, so libregnum carries
 * its own implementation.
 */

#ifndef LRG_ARC4_H
#define LRG_ARC4_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARC4 (lrg_arc4_get_type ())

/**
 * LrgArc4:
 *
 * An ARC4 cipher state: a 256-byte permutation and two indices.
 * Encryption and decryption are the same operation (XOR with the
 * keystream), and every call continues the keystream where the last one
 * stopped, so one #LrgArc4 handles one direction of a stream.
 *
 * #LrgArc4 is a boxed type with value semantics: lrg_arc4_copy()
 * duplicates the current state, after which the copy and the original
 * produce the same keystream independently.  The state is wiped by
 * lrg_arc4_free().  A state is not thread-safe.
 */
typedef struct _LrgArc4 LrgArc4;

LRG_AVAILABLE_IN_ALL
GType lrg_arc4_get_type (void) G_GNUC_CONST;

/**
 * lrg_arc4_new:
 * @key: the key, 1 to 256 bytes
 *
 * Runs the ARC4 key schedule.
 *
 * Returns: (transfer full): a cipher positioned at keystream offset 0
 */
LRG_AVAILABLE_IN_ALL
LrgArc4 *lrg_arc4_new (GBytes *key);

/**
 * lrg_arc4_new_drop:
 * @key: the key, 1 to 256 bytes
 * @n_drop: keystream bytes to discard
 *
 * The "RC4-drop[n]" variant: the key schedule followed by
 * lrg_arc4_discard() of @n_drop bytes.  World of Warcraft 3.3.5 uses
 * @n_drop 1024.
 *
 * Returns: (transfer full): a cipher positioned at keystream offset
 *   @n_drop
 */
LRG_AVAILABLE_IN_ALL
LrgArc4 *lrg_arc4_new_drop (GBytes *key,
                            gsize   n_drop);

/**
 * lrg_arc4_copy:
 * @self: an #LrgArc4
 *
 * Returns: (transfer full): an independent copy of the current state
 */
LRG_AVAILABLE_IN_ALL
LrgArc4 *lrg_arc4_copy (const LrgArc4 *self);

/**
 * lrg_arc4_free:
 * @self: (transfer full): an #LrgArc4
 *
 * Wipes and frees the state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_arc4_free (LrgArc4 *self);

/**
 * lrg_arc4_discard:
 * @self: an #LrgArc4
 * @n_bytes: keystream bytes to skip
 *
 * Advances the keystream without producing output.
 */
LRG_AVAILABLE_IN_ALL
void lrg_arc4_discard (LrgArc4 *self,
                       gsize    n_bytes);

/**
 * lrg_arc4_process:
 * @self: an #LrgArc4
 * @data: (array length=length) (element-type guint8): the buffer,
 *   encrypted or decrypted in place
 * @length: the length of @data
 *
 * XORs @data with the next @length keystream bytes.  Language bindings
 * that cannot modify a buffer in place should use
 * lrg_arc4_process_bytes().
 */
LRG_AVAILABLE_IN_ALL
void lrg_arc4_process (LrgArc4 *self,
                       guint8  *data,
                       gsize    length);

/**
 * lrg_arc4_process_bytes:
 * @self: an #LrgArc4
 * @input: the plaintext or ciphertext
 *
 * Like lrg_arc4_process() on a copy of @input.
 *
 * Returns: (transfer full): @input XOR the next keystream bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_arc4_process_bytes (LrgArc4 *self,
                                GBytes  *input);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgArc4, lrg_arc4_free)

G_END_DECLS

#endif /* LRG_ARC4_H */
