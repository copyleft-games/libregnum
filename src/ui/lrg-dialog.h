/* lrg-dialog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDialog: A modal sheet with a title, content and response buttons.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"
#include "lrg-button.h"
#include "lrg-canvas.h"

G_BEGIN_DECLS

/**
 * LRG_DIALOG_RESPONSE_NONE:
 *
 * Response id meaning "no particular answer". Application responses
 * should use positive ids.
 */
#define LRG_DIALOG_RESPONSE_NONE (0)

/**
 * LRG_DIALOG_RESPONSE_CANCEL:
 *
 * Response emitted when the dialog is cancelled with Escape or the
 * gamepad B button. Also a sensible id for a "Cancel" button.
 */
#define LRG_DIALOG_RESPONSE_CANCEL (-1)

/**
 * LRG_DIALOG_RESPONSE_CLOSE:
 *
 * Response id for a plain "Close" / "OK" acknowledgement button.
 */
#define LRG_DIALOG_RESPONSE_CLOSE (-2)

#define LRG_TYPE_DIALOG (lrg_dialog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDialog, lrg_dialog, LRG, DIALOG, LrgContainer)

/**
 * lrg_dialog_new:
 * @title: (nullable): heading text
 * @body: (nullable): message text; explicit line breaks are kept and
 *   long lines are word-wrapped
 *
 * Returns: (transfer full): a new #LrgDialog
 */
LRG_AVAILABLE_IN_ALL
LrgDialog * lrg_dialog_new (const gchar *title,
                            const gchar *body);

/**
 * lrg_dialog_get_title:
 * @self: an #LrgDialog
 *
 * Returns: (transfer none) (nullable): the heading text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dialog_get_title (LrgDialog *self);

/**
 * lrg_dialog_set_title:
 * @self: an #LrgDialog
 * @title: (nullable): heading text
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_set_title (LrgDialog   *self,
                           const gchar *title);

/**
 * lrg_dialog_get_body:
 * @self: an #LrgDialog
 *
 * Returns: (transfer none) (nullable): the message text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dialog_get_body (LrgDialog *self);

/**
 * lrg_dialog_set_body:
 * @self: an #LrgDialog
 * @body: (nullable): message text
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_set_body (LrgDialog   *self,
                          const gchar *body);

/**
 * lrg_dialog_get_content:
 * @self: an #LrgDialog
 *
 * Returns: (transfer none) (nullable): the extra content widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_dialog_get_content (LrgDialog *self);

/**
 * lrg_dialog_set_content:
 * @self: an #LrgDialog
 * @content: (nullable): a widget shown between the body text and the
 *   buttons (a form, an image, a list...), or %NULL to remove it
 *
 * The content gets the dialog's inner width and its own preferred
 * height. It comes before the buttons in Tab order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_set_content (LrgDialog *self,
                             LrgWidget *content);

/**
 * lrg_dialog_add_button:
 * @self: an #LrgDialog
 * @label: button text
 * @response_id: id emitted with #LrgDialog::response when clicked
 * @primary: %TRUE for the main action: it gets the "primary" style
 *   class, initial focus, and is triggered by Enter / gamepad A on the
 *   dialog background
 *
 * Adds a button to the right-aligned button row, after the existing
 * ones. Adding a second primary button demotes the first.
 *
 * Returns: (transfer none): the new button
 */
LRG_AVAILABLE_IN_ALL
LrgButton * lrg_dialog_add_button (LrgDialog   *self,
                                   const gchar *label,
                                   gint         response_id,
                                   gboolean     primary);

/**
 * lrg_dialog_get_primary_button:
 * @self: an #LrgDialog
 *
 * Returns: (transfer none) (nullable): the primary button
 */
LRG_AVAILABLE_IN_ALL
LrgButton * lrg_dialog_get_primary_button (LrgDialog *self);

/**
 * lrg_dialog_present:
 * @self: an #LrgDialog
 * @canvas: the canvas to show it on
 *
 * Shows the dialog as a modal popup centred on @canvas: a veil dims
 * everything behind it, the dialog rises into place, input outside it
 * is blocked and focus moves to the primary button. A dialog that is
 * still fading out from a previous close cannot be presented again
 * until the fade finishes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_present (LrgDialog *self,
                         LrgCanvas *canvas);

/**
 * lrg_dialog_close:
 * @self: an #LrgDialog
 *
 * Dismisses the dialog without emitting a response. Focus returns to
 * the widget that had it before the dialog was presented.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_close (LrgDialog *self);

/**
 * lrg_dialog_respond:
 * @self: an #LrgDialog
 * @response_id: the response
 *
 * Emits #LrgDialog::response with @response_id, then closes the
 * dialog. This is what the buttons and Escape do.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dialog_respond (LrgDialog *self,
                         gint       response_id);

/**
 * lrg_dialog_get_presented:
 * @self: an #LrgDialog
 *
 * Returns: %TRUE while the dialog is open on a canvas
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dialog_get_presented (LrgDialog *self);

G_END_DECLS
