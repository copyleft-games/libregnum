/* lrg-time-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimePicker: Hour / minute / second spin segments with 12/24-hour modes.
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
 * LrgTimePickerSegment:
 * @LRG_TIME_PICKER_SEGMENT_HOUR: the hour field
 * @LRG_TIME_PICKER_SEGMENT_MINUTE: the minute field
 * @LRG_TIME_PICKER_SEGMENT_SECOND: the second field (only with
 *   #LrgTimePicker:show-seconds)
 * @LRG_TIME_PICKER_SEGMENT_PERIOD: the AM / PM toggle (only in 12-hour
 *   mode)
 *
 * The editable fields of an #LrgTimePicker.
 */
typedef enum
{
	LRG_TIME_PICKER_SEGMENT_HOUR,
	LRG_TIME_PICKER_SEGMENT_MINUTE,
	LRG_TIME_PICKER_SEGMENT_SECOND,
	LRG_TIME_PICKER_SEGMENT_PERIOD
} LrgTimePickerSegment;

#define LRG_TYPE_TIME_PICKER_SEGMENT (lrg_time_picker_segment_get_type ())

/**
 * lrg_time_picker_segment_get_type:
 *
 * Returns: the #GType of #LrgTimePickerSegment
 */
LRG_AVAILABLE_IN_ALL
GType lrg_time_picker_segment_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_TIME_PICKER (lrg_time_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimePicker, lrg_time_picker, LRG, TIME_PICKER, LrgWidget)

/**
 * lrg_time_picker_new:
 *
 * Creates a 24-hour picker showing 00:00 without seconds.
 *
 * Returns: (transfer full): a new #LrgTimePicker
 */
LRG_AVAILABLE_IN_ALL
LrgTimePicker * lrg_time_picker_new (void);

/**
 * lrg_time_picker_get_hour:
 * @self: an #LrgTimePicker
 *
 * Returns: the hour, 0..23 (always 24-hour, whatever the display mode)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_time_picker_get_hour (LrgTimePicker *self);

/**
 * lrg_time_picker_set_hour:
 * @self: an #LrgTimePicker
 * @hour: hour, 0..23 (clamped)
 *
 * Sets the hour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_hour (LrgTimePicker *self,
                               gint           hour);

/**
 * lrg_time_picker_get_minute:
 * @self: an #LrgTimePicker
 *
 * Returns: the minute, 0..59
 */
LRG_AVAILABLE_IN_ALL
gint lrg_time_picker_get_minute (LrgTimePicker *self);

/**
 * lrg_time_picker_set_minute:
 * @self: an #LrgTimePicker
 * @minute: minute, 0..59 (clamped)
 *
 * Sets the minute.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_minute (LrgTimePicker *self,
                                 gint           minute);

/**
 * lrg_time_picker_get_second:
 * @self: an #LrgTimePicker
 *
 * Returns: the second, 0..59
 */
LRG_AVAILABLE_IN_ALL
gint lrg_time_picker_get_second (LrgTimePicker *self);

/**
 * lrg_time_picker_set_second:
 * @self: an #LrgTimePicker
 * @second: second, 0..59 (clamped)
 *
 * Sets the second (kept even while seconds are not shown).
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_second (LrgTimePicker *self,
                                 gint           second);

/**
 * lrg_time_picker_set_time:
 * @self: an #LrgTimePicker
 * @hour: hour, 0..23
 * @minute: minute, 0..59
 * @second: second, 0..59
 *
 * Sets all three fields at once, emitting #LrgTimePicker::time-changed
 * at most once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_time (LrgTimePicker *self,
                               gint           hour,
                               gint           minute,
                               gint           second);

/**
 * lrg_time_picker_get_show_seconds:
 * @self: an #LrgTimePicker
 *
 * Returns: whether the seconds field is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_time_picker_get_show_seconds (LrgTimePicker *self);

/**
 * lrg_time_picker_set_show_seconds:
 * @self: an #LrgTimePicker
 * @show_seconds: whether to show the seconds field
 *
 * Shows or hides the seconds field.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_show_seconds (LrgTimePicker *self,
                                       gboolean       show_seconds);

/**
 * lrg_time_picker_get_use_24h:
 * @self: an #LrgTimePicker
 *
 * Returns: %TRUE for a 24-hour display, %FALSE for 12-hour with AM/PM
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_time_picker_get_use_24h (LrgTimePicker *self);

/**
 * lrg_time_picker_set_use_24h:
 * @self: an #LrgTimePicker
 * @use_24h: %TRUE for 00..23, %FALSE for 12, 1..11 with an AM/PM toggle
 *
 * Switches the hour display. The stored time does not change.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_use_24h (LrgTimePicker *self,
                                  gboolean       use_24h);

/**
 * lrg_time_picker_get_is_pm:
 * @self: an #LrgTimePicker
 *
 * Returns: whether the hour is 12 or later
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_time_picker_get_is_pm (LrgTimePicker *self);

/**
 * lrg_time_picker_get_active_segment:
 * @self: an #LrgTimePicker
 *
 * Returns: the field that the keyboard, wheel and typed digits edit
 */
LRG_AVAILABLE_IN_ALL
LrgTimePickerSegment lrg_time_picker_get_active_segment (LrgTimePicker *self);

/**
 * lrg_time_picker_set_active_segment:
 * @self: an #LrgTimePicker
 * @segment: a field that is currently shown
 *
 * Moves the edit cursor to @segment; hidden fields are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_set_active_segment (LrgTimePicker        *self,
                                         LrgTimePickerSegment  segment);

/**
 * lrg_time_picker_step_segment:
 * @self: an #LrgTimePicker
 * @segment: the field to change
 * @delta: steps to add (negative to go back)
 *
 * Adds @delta to one field, wrapping within the field (59 + 1 minutes
 * is 0 minutes; the hour does not change). On the period field any odd
 * @delta toggles AM / PM. This is what the arrow keys, wheel and drag
 * do.
 */
LRG_AVAILABLE_IN_ALL
void lrg_time_picker_step_segment (LrgTimePicker        *self,
                                   LrgTimePickerSegment  segment,
                                   gint                  delta);

/**
 * lrg_time_picker_get_segment_bounds:
 * @self: an #LrgTimePicker
 * @segment: a field
 * @bounds: (out caller-allocates): the field's rectangle in canvas
 *   coordinates
 *
 * Returns: %FALSE if @segment is not shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_time_picker_get_segment_bounds (LrgTimePicker        *self,
                                             LrgTimePickerSegment  segment,
                                             GrlRectangle         *bounds);

/**
 * lrg_time_picker_get_text:
 * @self: an #LrgTimePicker
 *
 * The time as displayed: "HH:MM", "HH:MM:SS", or with " AM" / " PM" in
 * 12-hour mode.
 *
 * Returns: (transfer full): the formatted time
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_time_picker_get_text (LrgTimePicker *self);

G_END_DECLS
