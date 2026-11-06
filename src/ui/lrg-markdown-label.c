/* lrg-markdown-label.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMarkdownLabel: A label that renders a Markdown subset (headings, emphasis, lists, code, links).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-markdown-label.h"
#include "../lrg-log.h"

struct _LrgMarkdownLabel
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMarkdownLabel, lrg_markdown_label, LRG_TYPE_WIDGET)

static void
lrg_markdown_label_class_init (LrgMarkdownLabelClass *klass)
{
}

static void
lrg_markdown_label_init (LrgMarkdownLabel *self)
{
}

LrgMarkdownLabel *
lrg_markdown_label_new (void)
{
	return g_object_new (LRG_TYPE_MARKDOWN_LABEL, NULL);
}
