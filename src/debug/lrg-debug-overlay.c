/* lrg-debug-overlay.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug overlay implementation.
 */

#include "config.h"
#include "lrg-debug-overlay.h"
#include "lrg-profiler.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgDebugOverlay
{
    GObject              parent_instance;

    gboolean             visible;
    LrgDebugOverlayFlags flags;

    gint                 pos_x;
    gint                 pos_y;
    gint                 font_size;
    gint                 padding;

    GHashTable          *custom_lines;   /* key -> gchar* value */
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgDebugOverlay, lrg_debug_overlay, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgDebugOverlay *default_overlay = NULL;

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_debug_overlay_finalize (GObject *object)
{
    LrgDebugOverlay *self = LRG_DEBUG_OVERLAY (object);

    g_hash_table_destroy (self->custom_lines);

    if (default_overlay == self)
        default_overlay = NULL;

    G_OBJECT_CLASS (lrg_debug_overlay_parent_class)->finalize (object);
}

static void
lrg_debug_overlay_class_init (LrgDebugOverlayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_debug_overlay_finalize;
}

static void
lrg_debug_overlay_init (LrgDebugOverlay *self)
{
    self->visible = FALSE;
    self->flags = LRG_DEBUG_OVERLAY_FPS | LRG_DEBUG_OVERLAY_FRAME_TIME;

    self->pos_x = 10;
    self->pos_y = 10;
    self->font_size = 16;
    self->padding = 5;

    self->custom_lines = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, g_free);

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Created debug overlay");
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

LrgDebugOverlay *
lrg_debug_overlay_get_default (void)
{
    if (default_overlay == NULL)
        default_overlay = lrg_debug_overlay_new ();

    return default_overlay;
}

LrgDebugOverlay *
lrg_debug_overlay_new (void)
{
    return g_object_new (LRG_TYPE_DEBUG_OVERLAY, NULL);
}

/* ==========================================================================
 * Overlay Control
 * ========================================================================== */

gboolean
lrg_debug_overlay_is_visible (LrgDebugOverlay *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), FALSE);
    return self->visible;
}

void
lrg_debug_overlay_set_visible (LrgDebugOverlay *self,
                               gboolean         visible)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->visible = visible;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Debug overlay %s",
               visible ? "shown" : "hidden");
}

void
lrg_debug_overlay_toggle (LrgDebugOverlay *self)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    lrg_debug_overlay_set_visible (self, !self->visible);
}

/* ==========================================================================
 * Display Flags
 * ========================================================================== */

LrgDebugOverlayFlags
lrg_debug_overlay_get_flags (LrgDebugOverlay *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), LRG_DEBUG_OVERLAY_NONE);
    return self->flags;
}

void
lrg_debug_overlay_set_flags (LrgDebugOverlay      *self,
                             LrgDebugOverlayFlags  flags)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->flags = flags;
}

void
lrg_debug_overlay_add_flags (LrgDebugOverlay      *self,
                             LrgDebugOverlayFlags  flags)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->flags |= flags;
}

void
lrg_debug_overlay_remove_flags (LrgDebugOverlay      *self,
                                LrgDebugOverlayFlags  flags)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->flags &= ~flags;
}

gboolean
lrg_debug_overlay_has_flag (LrgDebugOverlay      *self,
                            LrgDebugOverlayFlags  flag)
{
    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), FALSE);
    return (self->flags & flag) != 0;
}

/* ==========================================================================
 * Position and Style
 * ========================================================================== */

void
lrg_debug_overlay_get_position (LrgDebugOverlay *self,
                                gint            *x,
                                gint            *y)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));

    if (x != NULL)
        *x = self->pos_x;
    if (y != NULL)
        *y = self->pos_y;
}

void
lrg_debug_overlay_set_position (LrgDebugOverlay *self,
                                gint             x,
                                gint             y)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->pos_x = x;
    self->pos_y = y;
}

gint
lrg_debug_overlay_get_font_size (LrgDebugOverlay *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), 0);
    return self->font_size;
}

void
lrg_debug_overlay_set_font_size (LrgDebugOverlay *self,
                                 gint             size)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->font_size = MAX (8, size);
}

gint
lrg_debug_overlay_get_padding (LrgDebugOverlay *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), 0);
    return self->padding;
}

void
lrg_debug_overlay_set_padding (LrgDebugOverlay *self,
                               gint             padding)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    self->padding = MAX (0, padding);
}

/* ==========================================================================
 * Custom Data Display
 * ========================================================================== */

void
lrg_debug_overlay_set_custom_line (LrgDebugOverlay *self,
                                   const gchar     *key,
                                   const gchar     *format,
                                   ...)
{
    va_list args;
    gchar *value;

    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    g_return_if_fail (key != NULL);

    if (format == NULL)
    {
        g_hash_table_remove (self->custom_lines, key);
        return;
    }

    va_start (args, format);
    value = g_strdup_vprintf (format, args);
    va_end (args);

    g_hash_table_insert (self->custom_lines, g_strdup (key), value);
}

void
lrg_debug_overlay_remove_custom_line (LrgDebugOverlay *self,
                                      const gchar     *key)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    g_return_if_fail (key != NULL);

    g_hash_table_remove (self->custom_lines, key);
}

void
lrg_debug_overlay_clear_custom_lines (LrgDebugOverlay *self)
{
    g_return_if_fail (LRG_IS_DEBUG_OVERLAY (self));
    g_hash_table_remove_all (self->custom_lines);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

static void
append_fps_info (GString     *str,
                 LrgProfiler *profiler)
{
    gdouble fps;

    fps = lrg_profiler_get_fps (profiler);
    g_string_append_printf (str, "FPS: %.1f\n", fps);
}

static void
append_frame_time_info (GString     *str,
                        LrgProfiler *profiler)
{
    gdouble frame_time;

    frame_time = lrg_profiler_get_frame_time_ms (profiler);
    g_string_append_printf (str, "Frame: %.2f ms\n", frame_time);
}

static void
append_memory_info (GString *str)
{
    /*
     * Memory info is platform-specific.
     * This is a placeholder that could be implemented
     * using /proc/self/status on Linux or other platform APIs.
     */
    g_string_append (str, "Memory: N/A\n");
}

static void
append_profiler_info (GString     *str,
                      LrgProfiler *profiler)
{
    GList *sections;
    GList *l;

    sections = lrg_profiler_get_section_names (profiler);

    for (l = sections; l != NULL; l = l->next)
    {
        const gchar *name = l->data;
        gdouble avg_ms;

        avg_ms = lrg_profiler_get_average_ms (profiler, name);
        g_string_append_printf (str, "  %s: %.2f ms\n", name, avg_ms);
    }

    g_list_free (sections);
}

static void
append_entity_info (GString *str)
{
    /*
     * Entity count info would come from the ECS.
     * This is a placeholder for integration with LrgWorld.
     */
    g_string_append (str, "Entities: N/A\n");
}

static void
append_physics_info (GString *str)
{
    /*
     * Physics info would come from the physics world.
     * This is a placeholder for integration with LrgPhysicsWorld.
     */
    g_string_append (str, "Bodies: N/A\n");
}

static void
append_custom_lines (GString    *str,
                     GHashTable *custom_lines)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init (&iter, custom_lines);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_string_append_printf (str, "%s: %s\n", (const gchar *)key, (const gchar *)value);
    }
}

gchar *
lrg_debug_overlay_get_text (LrgDebugOverlay *self)
{
    GString *str;
    LrgProfiler *profiler;

    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), NULL);

    if (!self->visible || self->flags == LRG_DEBUG_OVERLAY_NONE)
        return g_strdup ("");

    str = g_string_new (NULL);
    profiler = lrg_profiler_get_default ();

    /* FPS */
    if (self->flags & LRG_DEBUG_OVERLAY_FPS)
    {
        append_fps_info (str, profiler);
    }

    /* Frame time */
    if (self->flags & LRG_DEBUG_OVERLAY_FRAME_TIME)
    {
        append_frame_time_info (str, profiler);
    }

    /* Memory */
    if (self->flags & LRG_DEBUG_OVERLAY_MEMORY)
    {
        append_memory_info (str);
    }

    /* Profiler sections */
    if (self->flags & LRG_DEBUG_OVERLAY_PROFILER)
    {
        g_string_append (str, "Profiler:\n");
        append_profiler_info (str, profiler);
    }

    /* Entity info */
    if (self->flags & LRG_DEBUG_OVERLAY_ENTITIES)
    {
        append_entity_info (str);
    }

    /* Physics info */
    if (self->flags & LRG_DEBUG_OVERLAY_PHYSICS)
    {
        append_physics_info (str);
    }

    /* Custom lines */
    if (self->flags & LRG_DEBUG_OVERLAY_CUSTOM)
    {
        append_custom_lines (str, self->custom_lines);
    }

    /* Remove trailing newline */
    if (str->len > 0 && str->str[str->len - 1] == '\n')
        g_string_truncate (str, str->len - 1);

    return g_string_free (str, FALSE);
}

guint
lrg_debug_overlay_get_line_count (LrgDebugOverlay *self)
{
    guint count;
    LrgProfiler *profiler;
    GList *sections;

    g_return_val_if_fail (LRG_IS_DEBUG_OVERLAY (self), 0);

    if (!self->visible || self->flags == LRG_DEBUG_OVERLAY_NONE)
        return 0;

    count = 0;

    if (self->flags & LRG_DEBUG_OVERLAY_FPS)
        count++;

    if (self->flags & LRG_DEBUG_OVERLAY_FRAME_TIME)
        count++;

    if (self->flags & LRG_DEBUG_OVERLAY_MEMORY)
        count++;

    if (self->flags & LRG_DEBUG_OVERLAY_PROFILER)
    {
        count++;  /* "Profiler:" header */
        profiler = lrg_profiler_get_default ();
        sections = lrg_profiler_get_section_names (profiler);
        count += g_list_length (sections);
        g_list_free (sections);
    }

    if (self->flags & LRG_DEBUG_OVERLAY_ENTITIES)
        count++;

    if (self->flags & LRG_DEBUG_OVERLAY_PHYSICS)
        count++;

    if (self->flags & LRG_DEBUG_OVERLAY_CUSTOM)
        count += g_hash_table_size (self->custom_lines);

    return count;
}
