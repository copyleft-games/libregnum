/* lrg-log.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Logging macros for Libregnum.
 *
 * This header provides per-module logging domains and convenience macros
 * that wrap GLib's g_log() facility.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Log Domains
 *
 * Each module has its own log domain for filtering.
 * Set G_MESSAGES_DEBUG environment variable to control which domains
 * output debug messages.
 *
 * Example: G_MESSAGES_DEBUG="Libregnum-Core Libregnum-ECS" ./my-game
 * Example: G_MESSAGES_DEBUG="all" ./my-game
 * ========================================================================== */

#define LRG_LOG_DOMAIN_CORE       "Libregnum-Core"
#define LRG_LOG_DOMAIN_ECS        "Libregnum-ECS"
#define LRG_LOG_DOMAIN_INPUT      "Libregnum-Input"
#define LRG_LOG_DOMAIN_UI         "Libregnum-UI"
#define LRG_LOG_DOMAIN_TILEMAP    "Libregnum-Tilemap"
#define LRG_LOG_DOMAIN_DIALOG     "Libregnum-Dialog"
#define LRG_LOG_DOMAIN_INVENTORY  "Libregnum-Inventory"
#define LRG_LOG_DOMAIN_QUEST      "Libregnum-Quest"
#define LRG_LOG_DOMAIN_SAVE       "Libregnum-Save"
#define LRG_LOG_DOMAIN_AUDIO      "Libregnum-Audio"
#define LRG_LOG_DOMAIN_AI         "Libregnum-AI"
#define LRG_LOG_DOMAIN_PATHFIND   "Libregnum-Pathfinding"
#define LRG_LOG_DOMAIN_PHYSICS    "Libregnum-Physics"
#define LRG_LOG_DOMAIN_I18N       "Libregnum-I18N"
#define LRG_LOG_DOMAIN_NET        "Libregnum-Net"
#define LRG_LOG_DOMAIN_WORLD3D    "Libregnum-World3D"
#define LRG_LOG_DOMAIN_DEBUG      "Libregnum-Debug"
#define LRG_LOG_DOMAIN_MOD        "Libregnum-Mod"
#define LRG_LOG_DOMAIN_SCRIPTING  "Libregnum-Scripting"
#define LRG_LOG_DOMAIN_ECONOMY    "Libregnum-Economy"
#define LRG_LOG_DOMAIN_IDLE       "Libregnum-Idle"
#define LRG_LOG_DOMAIN_BUILDING   "Libregnum-Building"
#define LRG_LOG_DOMAIN_VEHICLE    "Libregnum-Vehicle"
#define LRG_LOG_DOMAIN_TWEEN      "Libregnum-Tween"
#define LRG_LOG_DOMAIN_TRANSITION "Libregnum-Transition"
#define LRG_LOG_DOMAIN_TRIGGER2D  "Libregnum-Trigger2D"
#define LRG_LOG_DOMAIN_ATLAS      "Libregnum-Atlas"
#define LRG_LOG_DOMAIN_TUTORIAL   "Libregnum-Tutorial"
#define LRG_LOG_DOMAIN_WEATHER    "Libregnum-Weather"
#define LRG_LOG_DOMAIN_LIGHTING   "Libregnum-Lighting"
#define LRG_LOG_DOMAIN_ANALYTICS  "Libregnum-Analytics"
#define LRG_LOG_DOMAIN_ACHIEVEMENT "Libregnum-Achievement"
#define LRG_LOG_DOMAIN_PHOTOMODE  "Libregnum-PhotoMode"
#define LRG_LOG_DOMAIN_STEAM      "Libregnum-Steam"
#define LRG_LOG_DOMAIN_DEMO       "Libregnum-Demo"
#define LRG_LOG_DOMAIN_VR         "Libregnum-VR"
#define LRG_LOG_DOMAIN_DECKBUILDER "Libregnum-Deckbuilder"
#define LRG_LOG_DOMAIN_TEXT       "Libregnum-Text"
#define LRG_LOG_DOMAIN_TEMPLATE   "Libregnum-Template"

/* ==========================================================================
 * Logging Macros
 *
 * These wrap g_log() with the appropriate log level.
 * ========================================================================== */

/**
 * lrg_debug:
 * @domain: the log domain (e.g., LRG_LOG_DOMAIN_CORE)
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs a debug message. Only output if G_MESSAGES_DEBUG includes the domain.
 */
#define lrg_debug(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)

/**
 * lrg_info:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs an informational message.
 */
#define lrg_info(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_INFO, format, ##__VA_ARGS__)

/**
 * lrg_message:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs a message.
 */
#define lrg_message(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_MESSAGE, format, ##__VA_ARGS__)

/**
 * lrg_warning:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs a warning message.
 */
#define lrg_warning(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_WARNING, format, ##__VA_ARGS__)

/**
 * lrg_critical:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs a critical error message.
 */
#define lrg_critical(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_CRITICAL, format, ##__VA_ARGS__)

/**
 * lrg_error:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs an error message. This is typically fatal.
 */
#define lrg_error(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_ERROR, format, ##__VA_ARGS__)

/* ==========================================================================
 * Trace Macros
 *
 * Trace macros are only compiled in if LRG_ENABLE_TRACE is defined.
 * Use these for very verbose debugging output that would be too noisy
 * for normal debug builds.
 *
 * Build with: make ENABLE_TRACE=1
 * ========================================================================== */

#ifdef LRG_ENABLE_TRACE

/**
 * lrg_trace:
 * @domain: the log domain
 * @format: printf-style format string
 * @...: format arguments
 *
 * Logs a trace message. Only available when compiled with LRG_ENABLE_TRACE.
 */
#define lrg_trace(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_DEBUG, "[TRACE] " format, ##__VA_ARGS__)

/**
 * lrg_trace_func:
 * @domain: the log domain
 *
 * Logs entry into a function. Use at the start of functions.
 */
#define lrg_trace_func(domain) \
    g_log (domain, G_LOG_LEVEL_DEBUG, "[TRACE] %s()", G_STRFUNC)

/**
 * lrg_trace_func_with:
 * @domain: the log domain
 * @format: printf-style format string for arguments
 * @...: format arguments
 *
 * Logs entry into a function with argument values.
 */
#define lrg_trace_func_with(domain, format, ...) \
    g_log (domain, G_LOG_LEVEL_DEBUG, "[TRACE] %s(" format ")", G_STRFUNC, ##__VA_ARGS__)

#else /* !LRG_ENABLE_TRACE */

#define lrg_trace(domain, format, ...) G_STMT_START { } G_STMT_END
#define lrg_trace_func(domain) G_STMT_START { } G_STMT_END
#define lrg_trace_func_with(domain, format, ...) G_STMT_START { } G_STMT_END

#endif /* LRG_ENABLE_TRACE */

/* ==========================================================================
 * Convenience Macros
 *
 * These use the default domain for the current compilation unit.
 * Define LRG_LOG_DOMAIN before including this header to use these.
 * ========================================================================== */

#ifdef LRG_LOG_DOMAIN

#define lrg_log_debug(format, ...) \
    lrg_debug (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_info(format, ...) \
    lrg_info (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_message(format, ...) \
    lrg_message (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_warning(format, ...) \
    lrg_warning (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_critical(format, ...) \
    lrg_critical (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_error(format, ...) \
    lrg_error (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_trace(format, ...) \
    lrg_trace (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#define lrg_log_trace_func() \
    lrg_trace_func (LRG_LOG_DOMAIN)

#define lrg_log_trace_func_with(format, ...) \
    lrg_trace_func_with (LRG_LOG_DOMAIN, format, ##__VA_ARGS__)

#endif /* LRG_LOG_DOMAIN */

G_END_DECLS
