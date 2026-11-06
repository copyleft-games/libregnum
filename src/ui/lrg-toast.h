/* lrg-toast.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToast: A short notification card.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/**
 * LrgToastKind:
 * @LRG_TOAST_KIND_INFO: neutral information (accent colour)
 * @LRG_TOAST_KIND_SUCCESS: something worked (success colour)
 * @LRG_TOAST_KIND_WARNING: needs attention (warning colour)
 * @LRG_TOAST_KIND_DANGER: something failed (danger colour)
 *
 * What a toast is about; picks the colour of its accent stripe and
 * progress hairline.
 */
typedef enum
{
	LRG_TOAST_KIND_INFO,
	LRG_TOAST_KIND_SUCCESS,
	LRG_TOAST_KIND_WARNING,
	LRG_TOAST_KIND_DANGER
} LrgToastKind;

LRG_AVAILABLE_IN_ALL
GType lrg_toast_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOAST_KIND (lrg_toast_kind_get_type ())

/**
 * LRG_TOAST_DEFAULT_DURATION:
 *
 * Seconds a toast stays up by default.
 */
#define LRG_TOAST_DEFAULT_DURATION (3.5)

#define LRG_TYPE_TOAST (lrg_toast_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgToast, lrg_toast, LRG, TOAST, LrgWidget)

/**
 * lrg_toast_new:
 * @title: (nullable): bold first line
 * @message: (nullable): muted detail text; explicit line breaks are kept
 *
 * Creates an %LRG_TOAST_KIND_INFO toast that lasts
 * %LRG_TOAST_DEFAULT_DURATION seconds. Show it with
 * lrg_toast_queue_show().
 *
 * Returns: (transfer full): a new #LrgToast
 */
LRG_AVAILABLE_IN_ALL
LrgToast * lrg_toast_new (const gchar *title,
                          const gchar *message);

/**
 * lrg_toast_get_title:
 * @self: an #LrgToast
 *
 * Returns: (transfer none) (nullable): the title
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_toast_get_title (LrgToast *self);

/**
 * lrg_toast_set_title:
 * @self: an #LrgToast
 * @title: (nullable): the title
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_set_title (LrgToast    *self,
                          const gchar *title);

/**
 * lrg_toast_get_message:
 * @self: an #LrgToast
 *
 * Returns: (transfer none) (nullable): the message
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_toast_get_message (LrgToast *self);

/**
 * lrg_toast_set_message:
 * @self: an #LrgToast
 * @message: (nullable): the message
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_set_message (LrgToast    *self,
                            const gchar *message);

/**
 * lrg_toast_get_kind:
 * @self: an #LrgToast
 *
 * Returns: the kind
 */
LRG_AVAILABLE_IN_ALL
LrgToastKind lrg_toast_get_kind (LrgToast *self);

/**
 * lrg_toast_set_kind:
 * @self: an #LrgToast
 * @kind: the kind
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_set_kind (LrgToast     *self,
                         LrgToastKind  kind);

/**
 * lrg_toast_get_duration:
 * @self: an #LrgToast
 *
 * Returns: seconds the toast stays up; 0 means until dismissed
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_toast_get_duration (LrgToast *self);

/**
 * lrg_toast_set_duration:
 * @self: an #LrgToast
 * @duration: seconds to stay up, or 0 to stay until clicked or
 *   dismissed with lrg_toast_dismiss()
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_set_duration (LrgToast *self,
                             gdouble   duration);

/**
 * lrg_toast_get_elapsed:
 * @self: an #LrgToast
 *
 * Returns: seconds the toast has been showing (paused while hovered)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_toast_get_elapsed (LrgToast *self);

/**
 * lrg_toast_get_remaining_fraction:
 * @self: an #LrgToast
 *
 * Returns: 1 when just shown, falling to 0 when it expires; always 1
 *   for toasts without a duration
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_toast_get_remaining_fraction (LrgToast *self);

/**
 * lrg_toast_advance:
 * @self: an #LrgToast
 * @seconds: time that passed
 *
 * Runs the toast's clock (the toast queue does this every frame). The
 * clock does not run while the pointer is over the toast. Dismisses
 * the toast when its duration runs out.
 *
 * Returns: %TRUE if the toast is dismissed (now or before)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_toast_advance (LrgToast *self,
                            gdouble   seconds);

/**
 * lrg_toast_dismiss:
 * @self: an #LrgToast
 *
 * Ends the toast early; emits #LrgToast::dismissed once. A toast queue
 * then fades it out and removes it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_dismiss (LrgToast *self);

/**
 * lrg_toast_get_dismissed:
 * @self: an #LrgToast
 *
 * Returns: %TRUE once the toast expired or was dismissed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_toast_get_dismissed (LrgToast *self);

G_END_DECLS
