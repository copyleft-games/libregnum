/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_MMO_TAG_BOARD (lrg_mmo_tag_board_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoTagBoard, lrg_mmo_tag_board, LRG, MMO_TAG_BOARD, GObject)

/**
 * LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS:
 *
 * The most accounts one target keeps contributions for.
 */
#define LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS (4096)

/**
 * lrg_mmo_tag_board_new:
 *
 * A board of who contributed to which shared target (a world boss, a raid
 * wave): everyone who hurts it or stands near it while it lives tags it,
 * not just the first.  Targets are caller-chosen 64-bit ids.  A tag keeps
 * the account's damage and the seconds it was in range and the order of
 * first contact; eligibility for the target's rewards is asked of the
 * board at the kill.  The board never reads a clock.  Thread confined.
 *
 * Returns: (transfer full): a new #LrgMmoTagBoard
 */
LRG_AVAILABLE_IN_ALL
LrgMmoTagBoard *lrg_mmo_tag_board_new (void);

/**
 * lrg_mmo_tag_board_tag:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @account: the contributor, 1 to 128 bytes
 * @amount: damage (or healing) done by this contribution, at least 0
 *
 * Tags @target for @account and adds @amount to its total.  The first
 * account ever to tag a target is its *first tagger*.
 *
 * Returns: %TRUE when @account had not tagged @target before; %FALSE
 *   when it had, when the target already holds
 *   %LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS accounts or an argument is invalid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_tag_board_tag (LrgMmoTagBoard *self,
                                guint64         target,
                                const gchar    *account,
                                gdouble         amount);

/**
 * lrg_mmo_tag_board_add_presence:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @account: the contributor
 * @seconds: seconds @account stood in range, greater than 0
 *
 * Adds time in range; it tags @target for @account like a blow without
 * damage does.
 *
 * Returns: %TRUE when @account had not tagged @target before
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_tag_board_add_presence (LrgMmoTagBoard *self,
                                         guint64         target,
                                         const gchar    *account,
                                         gdouble         seconds);

/**
 * lrg_mmo_tag_board_is_tagged:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @account: an account id
 *
 * Returns: whether @account tagged @target
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_tag_board_is_tagged (LrgMmoTagBoard *self,
                                      guint64         target,
                                      const gchar    *account);

/**
 * lrg_mmo_tag_board_get_first:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 *
 * Returns: (transfer none) (nullable): the first account to tag @target
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_mmo_tag_board_get_first (LrgMmoTagBoard *self,
                                          guint64         target);

/**
 * lrg_mmo_tag_board_get_amount:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @account: an account id
 *
 * Returns: the damage @account did to @target, 0 when it did none
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mmo_tag_board_get_amount (LrgMmoTagBoard *self,
                                      guint64         target,
                                      const gchar    *account);

/**
 * lrg_mmo_tag_board_get_presence:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @account: an account id
 *
 * Returns: the seconds @account stood in range of @target
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mmo_tag_board_get_presence (LrgMmoTagBoard *self,
                                        guint64         target,
                                        const gchar    *account);

/**
 * lrg_mmo_tag_board_get_count:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 *
 * Returns: how many accounts tagged @target
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mmo_tag_board_get_count (LrgMmoTagBoard *self,
                                   guint64         target);

/**
 * lrg_mmo_tag_board_get_total:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 *
 * Returns: the damage everybody did to @target
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mmo_tag_board_get_total (LrgMmoTagBoard *self,
                                     guint64         target);

/**
 * lrg_mmo_tag_board_list:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 *
 * Returns: (transfer full): an `a(sdd)` of account, damage and seconds in
 *   range, by damage descending then account
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_tag_board_list (LrgMmoTagBoard *self,
                                  guint64         target);

/**
 * lrg_mmo_tag_board_get_eligible:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 * @min_share: the least share of the total damage that qualifies, 0 to 1
 * @min_presence: the least seconds in range that qualify
 *
 * Who earns the target's rewards: an account qualifies by doing at
 * least @min_share of the total damage (and more than none) or by
 * standing in range for @min_presence seconds.  Someone who only passed
 * by earns nothing.
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type utf8):
 *   the accounts, sorted by damage descending then account
 */
LRG_AVAILABLE_IN_ALL
gchar **lrg_mmo_tag_board_get_eligible (LrgMmoTagBoard *self,
                                        guint64         target,
                                        gdouble         min_share,
                                        gdouble         min_presence);

/**
 * lrg_mmo_tag_board_clear:
 * @self: an #LrgMmoTagBoard
 * @target: a target id
 *
 * Forgets one target (it fell, it reset, it despawned).
 *
 * Returns: whether the target was known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_tag_board_clear (LrgMmoTagBoard *self,
                                  guint64         target);

/**
 * lrg_mmo_tag_board_clear_all:
 * @self: an #LrgMmoTagBoard
 *
 * Forgets every target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mmo_tag_board_clear_all (LrgMmoTagBoard *self);

/**
 * lrg_mmo_tag_board_get_target_count:
 * @self: an #LrgMmoTagBoard
 *
 * Returns: how many targets have tags
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mmo_tag_board_get_target_count (LrgMmoTagBoard *self);

G_END_DECLS
