/* lrg-date-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDatePicker: A date field that opens a month calendar popup.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_DATE_PICKER (lrg_date_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDatePicker, lrg_date_picker, LRG, DATE_PICKER, LrgWidget)

/**
 * lrg_date_picker_new:
 *
 * Creates a date picker with no date set.
 *
 * Returns: (transfer full): a new #LrgDatePicker
 */
LRG_AVAILABLE_IN_ALL
LrgDatePicker * lrg_date_picker_new (void);

/**
 * lrg_date_picker_get_date:
 * @self: an #LrgDatePicker
 *
 * Returns: (nullable) (transfer none): the picked date (local midnight),
 *   or %NULL when none is set
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_date_picker_get_date (LrgDatePicker *self);

/**
 * lrg_date_picker_set_date:
 * @self: an #LrgDatePicker
 * @date: (nullable): the date; only its year, month and day are used
 *
 * Sets the date, clamped into #LrgDatePicker:min-date ..
 * #LrgDatePicker:max-date. Emits #LrgDatePicker::date-changed when the
 * day changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_date (LrgDatePicker *self,
                               GDateTime     *date);

/**
 * lrg_date_picker_set_date_ymd:
 * @self: an #LrgDatePicker
 * @year: year
 * @month: month, 1..12
 * @day: day of the month, 1..31
 *
 * Sets the date from its parts. Invalid dates are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_date_ymd (LrgDatePicker *self,
                                   gint           year,
                                   gint           month,
                                   gint           day);

/**
 * lrg_date_picker_get_date_ymd:
 * @self: an #LrgDatePicker
 * @year: (out) (optional): year
 * @month: (out) (optional): month, 1..12
 * @day: (out) (optional): day of the month
 *
 * Returns: %FALSE (and leaves the outputs alone) when no date is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_date_picker_get_date_ymd (LrgDatePicker *self,
                                       gint          *year,
                                       gint          *month,
                                       gint          *day);

/**
 * lrg_date_picker_get_text:
 * @self: an #LrgDatePicker
 *
 * Returns: (transfer full) (nullable): the date formatted with
 *   #LrgDatePicker:format, or %NULL when no date is set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_date_picker_get_text (LrgDatePicker *self);

/**
 * lrg_date_picker_get_format:
 * @self: an #LrgDatePicker
 *
 * Returns: the g_date_time_format() pattern used in the field
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_date_picker_get_format (LrgDatePicker *self);

/**
 * lrg_date_picker_set_format:
 * @self: an #LrgDatePicker
 * @format: (nullable): a g_date_time_format() pattern, %NULL for
 *   "%Y-%m-%d"
 *
 * Sets how the field shows the date.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_format (LrgDatePicker *self,
                                 const gchar   *format);

/**
 * lrg_date_picker_get_placeholder:
 * @self: an #LrgDatePicker
 *
 * Returns: (nullable): the text shown while no date is set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_date_picker_get_placeholder (LrgDatePicker *self);

/**
 * lrg_date_picker_set_placeholder:
 * @self: an #LrgDatePicker
 * @placeholder: (nullable): muted text shown while no date is set
 *
 * Sets the placeholder.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_placeholder (LrgDatePicker *self,
                                      const gchar   *placeholder);

/**
 * lrg_date_picker_get_min_date:
 * @self: an #LrgDatePicker
 *
 * Returns: (nullable) (transfer none): the earliest pickable date
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_date_picker_get_min_date (LrgDatePicker *self);

/**
 * lrg_date_picker_set_min_date:
 * @self: an #LrgDatePicker
 * @date: (nullable): earliest pickable date, %NULL for no limit
 *
 * Days before it are disabled in the calendar and the current date is
 * clamped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_min_date (LrgDatePicker *self,
                                   GDateTime     *date);

/**
 * lrg_date_picker_get_max_date:
 * @self: an #LrgDatePicker
 *
 * Returns: (nullable) (transfer none): the latest pickable date
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_date_picker_get_max_date (LrgDatePicker *self);

/**
 * lrg_date_picker_set_max_date:
 * @self: an #LrgDatePicker
 * @date: (nullable): latest pickable date, %NULL for no limit
 *
 * Days after it are disabled in the calendar and the current date is
 * clamped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_max_date (LrgDatePicker *self,
                                   GDateTime     *date);

/**
 * lrg_date_picker_get_today:
 * @self: an #LrgDatePicker
 *
 * Returns: (nullable) (transfer none): the explicit "today", or %NULL
 *   when the system date is used
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_date_picker_get_today (LrgDatePicker *self);

/**
 * lrg_date_picker_set_today:
 * @self: an #LrgDatePicker
 * @today: (nullable): the day ringed as today, %NULL for the system date
 *   (read when the calendar opens)
 *
 * Games with an in-world calendar, and tests, can pin "today".
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_set_today (LrgDatePicker *self,
                                GDateTime     *today);

/**
 * lrg_date_picker_popup:
 * @self: an #LrgDatePicker
 *
 * Opens the calendar below the field, showing the month of the date
 * (or of today). The picker must be on an #LrgCanvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_popup (LrgDatePicker *self);

/**
 * lrg_date_picker_popdown:
 * @self: an #LrgDatePicker
 *
 * Closes the calendar if it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_popdown (LrgDatePicker *self);

/**
 * lrg_date_picker_get_popup_open:
 * @self: an #LrgDatePicker
 *
 * Returns: whether the calendar is showing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_date_picker_get_popup_open (LrgDatePicker *self);

/**
 * lrg_date_picker_get_visible_month:
 * @self: an #LrgDatePicker
 * @year: (out) (optional): year of the month on show
 * @month: (out) (optional): month on show, 1..12
 *
 * The month the calendar shows (or will show when it next opens).
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_get_visible_month (LrgDatePicker *self,
                                        gint          *year,
                                        gint          *month);

/**
 * lrg_date_picker_show_month:
 * @self: an #LrgDatePicker
 * @year: year
 * @month: month, 1..12
 *
 * Turns the open calendar to a month (sliding), with the keyboard
 * cursor on its first pickable day.
 */
LRG_AVAILABLE_IN_ALL
void lrg_date_picker_show_month (LrgDatePicker *self,
                                 gint           year,
                                 gint           month);

/**
 * lrg_date_picker_get_cursor_date:
 * @self: an #LrgDatePicker
 *
 * The day under the calendar's keyboard cursor.
 *
 * Returns: (transfer full) (nullable): the cursor day, or %NULL when
 *   the calendar is closed
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_date_picker_get_cursor_date (LrgDatePicker *self);

/**
 * lrg_date_picker_get_day_bounds:
 * @self: an #LrgDatePicker
 * @year: year
 * @month: month, 1..12
 * @day: day of the month
 * @bounds: (out caller-allocates): the day's cell in canvas coordinates
 *
 * Where a day is drawn in the open calendar, for hit testing.
 *
 * Returns: %FALSE if the calendar is closed or the day is not on show
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_date_picker_get_day_bounds (LrgDatePicker *self,
                                         gint           year,
                                         gint           month,
                                         gint           day,
                                         GrlRectangle  *bounds);

G_END_DECLS
