/* lrg-card-keyword.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeyword - Built-in keyword helpers implementation.
 */

#include "lrg-card-keyword.h"
#include <string.h>

/* ==========================================================================
 * Keyword Data Tables
 * ========================================================================== */

typedef struct
{
    LrgCardKeyword keyword;
    const gchar   *name;
    const gchar   *description;
    const gchar   *icon;
    gboolean       positive;
    gboolean       negative;
    gboolean       affects_playability;
    gboolean       affects_discard;
} KeywordInfo;

static const KeywordInfo keyword_info[] =
{
    {
        LRG_CARD_KEYWORD_INNATE,
        "Innate",
        "Always drawn at the start of combat.",
        "keyword-innate",
        TRUE, FALSE, FALSE, FALSE
    },
    {
        LRG_CARD_KEYWORD_RETAIN,
        "Retain",
        "This card is not discarded at end of turn.",
        "keyword-retain",
        TRUE, FALSE, FALSE, TRUE
    },
    {
        LRG_CARD_KEYWORD_EXHAUST,
        "Exhaust",
        "When played, remove this card from combat.",
        "keyword-exhaust",
        FALSE, TRUE, FALSE, FALSE
    },
    {
        LRG_CARD_KEYWORD_ETHEREAL,
        "Ethereal",
        "If not played, exhaust at end of turn.",
        "keyword-ethereal",
        FALSE, TRUE, FALSE, TRUE
    },
    {
        LRG_CARD_KEYWORD_UNPLAYABLE,
        "Unplayable",
        "Cannot be played.",
        "keyword-unplayable",
        FALSE, TRUE, TRUE, FALSE
    },
    {
        LRG_CARD_KEYWORD_X_COST,
        "X Cost",
        "Uses all remaining energy.",
        "keyword-x-cost",
        FALSE, FALSE, FALSE, FALSE
    },
    {
        LRG_CARD_KEYWORD_FRAGILE,
        "Fragile",
        "Removed from deck at end of combat.",
        "keyword-fragile",
        FALSE, TRUE, FALSE, FALSE
    },
    {
        LRG_CARD_KEYWORD_FLEETING,
        "Fleeting",
        "Discarded at end of turn even with Retain.",
        "keyword-fleeting",
        FALSE, TRUE, FALSE, TRUE
    },
    { 0, NULL, NULL, NULL, FALSE, FALSE, FALSE, FALSE }
};

/* All keywords array for iteration */
static const LrgCardKeyword all_keywords[] =
{
    LRG_CARD_KEYWORD_INNATE,
    LRG_CARD_KEYWORD_RETAIN,
    LRG_CARD_KEYWORD_EXHAUST,
    LRG_CARD_KEYWORD_ETHEREAL,
    LRG_CARD_KEYWORD_UNPLAYABLE,
    LRG_CARD_KEYWORD_X_COST,
    LRG_CARD_KEYWORD_FRAGILE,
    LRG_CARD_KEYWORD_FLEETING,
    0  /* Terminator */
};

static const KeywordInfo *
find_keyword_info (LrgCardKeyword keyword)
{
    gsize i;

    for (i = 0; keyword_info[i].name != NULL; i++)
    {
        if (keyword_info[i].keyword == keyword)
            return &keyword_info[i];
    }
    return NULL;
}

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
const gchar *
lrg_card_keyword_get_name (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->name : NULL;
}

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
const gchar *
lrg_card_keyword_get_description (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->description : NULL;
}

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
const gchar *
lrg_card_keyword_get_icon (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->icon : NULL;
}

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
gboolean
lrg_card_keyword_is_positive (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->positive : FALSE;
}

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
gboolean
lrg_card_keyword_is_negative (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->negative : FALSE;
}

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
gboolean
lrg_card_keyword_affects_playability (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->affects_playability : FALSE;
}

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
gboolean
lrg_card_keyword_affects_discard (LrgCardKeyword keyword)
{
    const KeywordInfo *info = find_keyword_info (keyword);
    return info != NULL ? info->affects_discard : FALSE;
}

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
LrgCardKeyword
lrg_card_keyword_from_string (const gchar *str)
{
    gsize i;

    if (str == NULL)
        return LRG_CARD_KEYWORD_NONE;

    for (i = 0; keyword_info[i].name != NULL; i++)
    {
        if (g_ascii_strcasecmp (str, keyword_info[i].name) == 0)
            return keyword_info[i].keyword;
    }

    return LRG_CARD_KEYWORD_NONE;
}

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
const gchar *
lrg_card_keyword_to_string (LrgCardKeyword keyword)
{
    return lrg_card_keyword_get_name (keyword);
}

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
LrgCardKeyword
lrg_card_keywords_from_string (const gchar *str)
{
    LrgCardKeyword result = LRG_CARD_KEYWORD_NONE;
    g_auto(GStrv) parts = NULL;
    gsize i;

    if (str == NULL || *str == '\0')
        return LRG_CARD_KEYWORD_NONE;

    parts = g_strsplit (str, ",", -1);

    for (i = 0; parts[i] != NULL; i++)
    {
        gchar *trimmed = g_strstrip (parts[i]);
        LrgCardKeyword keyword = lrg_card_keyword_from_string (trimmed);
        result |= keyword;
    }

    return result;
}

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
gchar *
lrg_card_keywords_to_string (LrgCardKeyword keywords)
{
    GString *result;
    gboolean first = TRUE;
    gsize i;

    if (keywords == LRG_CARD_KEYWORD_NONE)
        return g_strdup ("");

    result = g_string_new (NULL);

    for (i = 0; all_keywords[i] != 0; i++)
    {
        if (keywords & all_keywords[i])
        {
            const gchar *name = lrg_card_keyword_get_name (all_keywords[i]);
            if (name != NULL)
            {
                if (!first)
                    g_string_append (result, ", ");
                g_string_append (result, name);
                first = FALSE;
            }
        }
    }

    return g_string_free (result, FALSE);
}

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
void
lrg_card_keyword_foreach (LrgCardKeyword  keywords,
                          GFunc           func,
                          gpointer        user_data)
{
    gsize i;

    g_return_if_fail (func != NULL);

    for (i = 0; all_keywords[i] != 0; i++)
    {
        if (keywords & all_keywords[i])
            func (GINT_TO_POINTER (all_keywords[i]), user_data);
    }
}

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
guint
lrg_card_keyword_count (LrgCardKeyword keywords)
{
    guint count = 0;
    gsize i;

    for (i = 0; all_keywords[i] != 0; i++)
    {
        if (keywords & all_keywords[i])
            count++;
    }

    return count;
}

/**
 * lrg_card_keyword_get_all:
 *
 * Gets an array of all built-in keyword values.
 *
 * Returns: (transfer none) (array zero-terminated=1): array of keywords
 *
 * Since: 1.0
 */
const LrgCardKeyword *
lrg_card_keyword_get_all (void)
{
    return all_keywords;
}
