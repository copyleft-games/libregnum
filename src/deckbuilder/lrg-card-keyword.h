/* lrg-card-keyword.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeyword - Built-in keyword helpers and utilities.
 *
 * Keywords modify card behavior in standardized ways. Built-in keywords
 * are defined as flags in LrgCardKeyword. This module provides helper
 * functions for working with keywords.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Keyword Information
 * ========================================================================== */

/**
 * lrg_card_keyword_get_name:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Gets the display name for a keyword.
 *
 * Returns: (transfer none): the keyword name, or %NULL if invalid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_get_name (LrgCardKeyword keyword);

/**
 * lrg_card_keyword_get_description:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Gets the description for a keyword.
 *
 * Returns: (transfer none): the keyword description, or %NULL if invalid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_get_description (LrgCardKeyword keyword);

/**
 * lrg_card_keyword_get_icon:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Gets the icon identifier for a keyword.
 *
 * Returns: (transfer none) (nullable): the icon ID, or %NULL if none
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_get_icon (LrgCardKeyword keyword);

/* ==========================================================================
 * Keyword Queries
 * ========================================================================== */

/**
 * lrg_card_keyword_is_positive:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Checks if a keyword is generally beneficial.
 *
 * Returns: %TRUE if the keyword is positive
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_is_positive (LrgCardKeyword keyword);

/**
 * lrg_card_keyword_is_negative:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Checks if a keyword is generally detrimental.
 *
 * Returns: %TRUE if the keyword is negative
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_is_negative (LrgCardKeyword keyword);

/**
 * lrg_card_keyword_affects_playability:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Checks if a keyword affects whether a card can be played.
 *
 * Returns: %TRUE if the keyword affects playability
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_affects_playability (LrgCardKeyword keyword);

/**
 * lrg_card_keyword_affects_discard:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Checks if a keyword affects end-of-turn discard behavior.
 *
 * Returns: %TRUE if the keyword affects discard
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_affects_discard (LrgCardKeyword keyword);

/* ==========================================================================
 * Keyword Parsing
 * ========================================================================== */

/**
 * lrg_card_keyword_from_string:
 * @str: keyword name string
 *
 * Parses a keyword from its string name (case-insensitive).
 *
 * Returns: the keyword flag, or %LRG_CARD_KEYWORD_NONE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeyword lrg_card_keyword_from_string (const gchar *str);

/**
 * lrg_card_keyword_to_string:
 * @keyword: a single #LrgCardKeyword flag
 *
 * Converts a keyword to its canonical string name.
 *
 * Returns: (transfer none): the keyword string, or %NULL if invalid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_to_string (LrgCardKeyword keyword);

/**
 * lrg_card_keywords_from_string:
 * @str: comma-separated keyword names
 *
 * Parses multiple keywords from a comma-separated string.
 *
 * Returns: combined keyword flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeyword lrg_card_keywords_from_string (const gchar *str);

/**
 * lrg_card_keywords_to_string:
 * @keywords: combined keyword flags
 *
 * Converts keyword flags to a comma-separated string.
 *
 * Returns: (transfer full): comma-separated keyword names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_card_keywords_to_string (LrgCardKeyword keywords);

/* ==========================================================================
 * Keyword Iteration
 * ========================================================================== */

/**
 * lrg_card_keyword_foreach:
 * @keywords: combined keyword flags
 * @func: (scope call): callback for each keyword
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each keyword in the flags.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_foreach (LrgCardKeyword  keywords,
                               GFunc           func,
                               gpointer        user_data);

/**
 * lrg_card_keyword_count:
 * @keywords: combined keyword flags
 *
 * Counts the number of keywords set in the flags.
 *
 * Returns: the number of keywords
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_card_keyword_count (LrgCardKeyword keywords);

/**
 * lrg_card_keyword_get_all:
 *
 * Gets an array of all built-in keyword values.
 *
 * Returns: (transfer none) (array zero-terminated=1): array of keywords
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgCardKeyword * lrg_card_keyword_get_all (void);

G_END_DECLS
