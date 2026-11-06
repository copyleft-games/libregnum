/* lrg-markdown-label.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMarkdownLabel: A label that renders a Markdown subset (headings, emphasis, lists, code, links).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_MARKDOWN_LABEL (lrg_markdown_label_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMarkdownLabel, lrg_markdown_label, LRG, MARKDOWN_LABEL, LrgWidget)

/**
 * lrg_markdown_label_new:
 *
 * Returns: (transfer full): a new #LrgMarkdownLabel
 */
LRG_AVAILABLE_IN_ALL
LrgMarkdownLabel * lrg_markdown_label_new (void);

G_END_DECLS
