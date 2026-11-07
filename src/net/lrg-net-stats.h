/* lrg-net-stats.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNetStats: network traffic statistics (per peer, per message type,
 * rolling per-second windows, round-trip times and loss).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgNetStatsMetric:
 * @LRG_NET_STATS_METRIC_BYTES_IN: bytes received
 * @LRG_NET_STATS_METRIC_BYTES_OUT: bytes sent
 * @LRG_NET_STATS_METRIC_PACKETS_IN: messages (frames, datagrams) received
 * @LRG_NET_STATS_METRIC_PACKETS_OUT: messages sent
 * @LRG_NET_STATS_METRIC_RTT: average round-trip time in milliseconds of
 *   the samples recorded in a second (0 when there were none)
 * @LRG_NET_STATS_METRIC_LOST: packets detected as lost
 *
 * A quantity lrg_net_stats_get_history() reports per second.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_NET_STATS_METRIC_BYTES_IN,
	LRG_NET_STATS_METRIC_BYTES_OUT,
	LRG_NET_STATS_METRIC_PACKETS_IN,
	LRG_NET_STATS_METRIC_PACKETS_OUT,
	LRG_NET_STATS_METRIC_RTT,
	LRG_NET_STATS_METRIC_LOST
} LrgNetStatsMetric;

LRG_AVAILABLE_IN_ALL
GType lrg_net_stats_metric_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NET_STATS_METRIC (lrg_net_stats_metric_get_type ())

/**
 * LrgNetCounters:
 * @packets_in: messages received
 * @packets_out: messages sent
 * @bytes_in: bytes received (whole frames: header plus payload)
 * @bytes_out: bytes sent
 * @lost: packets detected as lost (sequence gaps on datagram channels)
 *
 * A set of traffic counters, filled by lrg_net_stats_get_totals() and
 * lrg_net_stats_get_window().
 *
 * Since: 0.9.0
 */
typedef struct
{
	guint64 packets_in;
	guint64 packets_out;
	guint64 bytes_in;
	guint64 bytes_out;
	guint64 lost;
} LrgNetCounters;

LRG_AVAILABLE_IN_ALL
GType lrg_net_counters_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NET_COUNTERS (lrg_net_counters_get_type ())

/**
 * lrg_net_counters_copy:
 * @self: an #LrgNetCounters
 *
 * Copies a counter set.
 *
 * Returns: (transfer full): a newly allocated copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNetCounters * lrg_net_counters_copy (const LrgNetCounters *self);

/**
 * lrg_net_counters_free:
 * @self: (nullable): an #LrgNetCounters from lrg_net_counters_copy()
 *
 * Frees a counter set.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_counters_free (LrgNetCounters *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgNetCounters, lrg_net_counters_free)

/**
 * LRG_NET_STATS_DEFAULT_WINDOW:
 *
 * The default history length of an #LrgNetStats, in seconds.
 *
 * Since: 0.9.0
 */
#define LRG_NET_STATS_DEFAULT_WINDOW (60)

/**
 * LRG_NET_STATS_MAX_WINDOW:
 *
 * The longest history an #LrgNetStats keeps, in seconds (one hour).
 *
 * Since: 0.9.0
 */
#define LRG_NET_STATS_MAX_WINDOW (3600)

#define LRG_TYPE_NET_STATS (lrg_net_stats_get_type ())

/**
 * LrgNetStats:
 *
 * Network traffic statistics.
 *
 * An #LrgNetStats counts messages and bytes in each direction, per peer
 * and per message type, keeps a rolling per-second history of the last
 * #LrgNetStats:window seconds, and tracks round-trip times and loss per
 * peer. Peers and message types are plain strings: the transport layers
 * name a peer `address:port` and a message type by its #LrgNetMessageType
 * nick (`data`, `ping`, ...), `mmo:<opcode>` for lrg_mmo_protocol_encode() frames
 * and `dtls` for #LrgMmoDatagram records.
 *
 * The process-wide instance from lrg_net_stats_get_default() is fed by
 * #LrgNetServer, #LrgNetClient, lrg_net_peer_update_rtt(), the MMO protocol
 * codec and #LrgMmoDatagram once it is enabled (it starts disabled
 * unless the `LRG_NET_STATS` environment variable is `1`, so the hooks
 * cost one atomic read per message). Instances made with
 * lrg_net_stats_new() start enabled and count only what the caller
 * records. Every method is thread safe.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNetStats, lrg_net_stats, LRG, NET_STATS, GObject)

/**
 * lrg_net_stats_new:
 *
 * Creates an empty, enabled statistics collector with a
 * %LRG_NET_STATS_DEFAULT_WINDOW second history.
 *
 * Returns: (transfer full): a new #LrgNetStats
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNetStats * lrg_net_stats_new (void);

/**
 * lrg_net_stats_get_default:
 *
 * Gets the process-wide collector the networking layers feed. It is
 * created disabled (enabled when the `LRG_NET_STATS` environment
 * variable is `1`); turn it on with lrg_net_stats_set_enabled().
 *
 * Returns: (transfer none): the default #LrgNetStats
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNetStats * lrg_net_stats_get_default (void);

/**
 * lrg_net_stats_get_enabled:
 * @self: an #LrgNetStats
 *
 * Whether the collector records anything.
 *
 * Returns: %TRUE when enabled
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_stats_get_enabled (LrgNetStats *self);

/**
 * lrg_net_stats_set_enabled:
 * @self: an #LrgNetStats
 * @enabled: whether to record
 *
 * Enables or disables recording. A disabled collector ignores every
 * lrg_net_stats_record_*() call and keeps what it already counted.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_set_enabled (LrgNetStats *self,
                                gboolean     enabled);

/**
 * lrg_net_stats_get_window:
 * @self: an #LrgNetStats
 *
 * Gets the history length.
 *
 * Returns: the number of one-second buckets kept
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_stats_get_window (LrgNetStats *self);

/**
 * lrg_net_stats_set_window:
 * @self: an #LrgNetStats
 * @seconds: history length, clamped to 1 .. %LRG_NET_STATS_MAX_WINDOW
 *
 * Sets the history length. Changing it drops the per-second history
 * (the running totals stay).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_set_window (LrgNetStats *self,
                               guint        seconds);

/**
 * lrg_net_stats_set_time:
 * @self: an #LrgNetStats
 * @monotonic_us: the clock in microseconds, or a negative value to go
 *   back to g_get_monotonic_time()
 *
 * Overrides the clock the per-second buckets and ping timing use, for
 * deterministic tests and replays.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_set_time (LrgNetStats *self,
                             gint64       monotonic_us);

/**
 * lrg_net_stats_reset:
 * @self: an #LrgNetStats
 *
 * Forgets every peer, message type, counter and history bucket.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_reset (LrgNetStats *self);

/**
 * lrg_net_stats_record_sent:
 * @self: an #LrgNetStats
 * @peer: (nullable): the peer key, or %NULL for traffic not tied to a peer
 * @message_type: (nullable): the message type, or %NULL for "unknown"
 * @bytes: wire size of the message
 *
 * Counts one sent message in the totals, the message type, the peer and
 * the current second.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_sent (LrgNetStats *self,
                                const gchar *peer,
                                const gchar *message_type,
                                gsize        bytes);

/**
 * lrg_net_stats_record_received:
 * @self: an #LrgNetStats
 * @peer: (nullable): the peer key, or %NULL for traffic not tied to a peer
 * @message_type: (nullable): the message type, or %NULL for "unknown"
 * @bytes: wire size of the message
 *
 * Counts one received message.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_received (LrgNetStats *self,
                                    const gchar *peer,
                                    const gchar *message_type,
                                    gsize        bytes);

/**
 * lrg_net_stats_record_rtt:
 * @self: an #LrgNetStats
 * @peer: the peer key
 * @rtt_ms: a round-trip time sample in milliseconds
 *
 * Adds a round-trip time sample for @peer (last, smoothed average,
 * minimum and maximum are kept; the history averages per second).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_rtt (LrgNetStats *self,
                               const gchar *peer,
                               gdouble      rtt_ms);

/**
 * lrg_net_stats_record_lost:
 * @self: an #LrgNetStats
 * @peer: (nullable): the peer key
 * @count: packets lost; negative when a packet counted as lost arrived
 *   late after all
 *
 * Adjusts the loss counters. Totals never go below zero.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_lost (LrgNetStats *self,
                                const gchar *peer,
                                gint         count);

/**
 * lrg_net_stats_record_ping:
 * @self: an #LrgNetStats
 * @peer: the peer key
 * @sequence: the ping's sequence number
 *
 * Remembers when a ping with @sequence left. A later
 * lrg_net_stats_record_pong() with the same sequence records the
 * round trip. The transports call both for %LRG_NET_MESSAGE_TYPE_PING
 * and %LRG_NET_MESSAGE_TYPE_PONG messages, so answering a ping with a
 * pong carrying its sequence is all a game needs for RTT graphs.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_ping (LrgNetStats *self,
                                const gchar *peer,
                                guint32      sequence);

/**
 * lrg_net_stats_record_pong:
 * @self: an #LrgNetStats
 * @peer: the peer key
 * @sequence: the sequence the pong echoes
 *
 * Completes a round trip started by lrg_net_stats_record_ping(); does
 * nothing for an unknown sequence.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_record_pong (LrgNetStats *self,
                                const gchar *peer,
                                guint32      sequence);

/**
 * lrg_net_stats_set_peer_connected:
 * @self: an #LrgNetStats
 * @peer: the peer key
 * @connected: whether the peer is connected now
 *
 * Records a connection state change. Peers stay listed (with their
 * counters) after they disconnect, until lrg_net_stats_reset().
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_stats_set_peer_connected (LrgNetStats *self,
                                       const gchar *peer,
                                       gboolean     connected);

/**
 * lrg_net_stats_get_peers:
 * @self: an #LrgNetStats
 *
 * Lists every peer seen since the last reset, sorted.
 *
 * Returns: (transfer full) (array zero-terminated=1): the peer keys
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_net_stats_get_peers (LrgNetStats *self);

/**
 * lrg_net_stats_get_message_types:
 * @self: an #LrgNetStats
 * @peer: (nullable): only types seen with this peer, or %NULL for all
 *
 * Lists the message types seen since the last reset, sorted.
 *
 * Returns: (transfer full) (array zero-terminated=1): the type names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_net_stats_get_message_types (LrgNetStats *self,
                                          const gchar *peer);

/**
 * lrg_net_stats_get_peer_connected:
 * @self: an #LrgNetStats
 * @peer: the peer key
 *
 * Whether @peer is connected as far as the collector knows.
 *
 * Returns: %TRUE when the last recorded state is connected (a peer that
 *   only ever exchanged traffic counts as connected)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_stats_get_peer_connected (LrgNetStats *self,
                                           const gchar *peer);

/**
 * lrg_net_stats_get_totals:
 * @self: an #LrgNetStats
 * @peer: (nullable): a peer key, or %NULL for every peer
 * @message_type: (nullable): a message type, or %NULL for every type
 * @out: (out caller-allocates): the counters since the last reset
 *
 * Reads the running totals for a peer, a message type, a type on one
 * peer, or everything.
 *
 * Returns: %TRUE when the peer / type is known (else @out is zeroed)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_stats_get_totals (LrgNetStats    *self,
                                   const gchar    *peer,
                                   const gchar    *message_type,
                                   LrgNetCounters *out);

/**
 * lrg_net_stats_get_window_counters:
 * @self: an #LrgNetStats
 * @peer: (nullable): a peer key, or %NULL for every peer
 * @message_type: (nullable): a message type, or %NULL for every type;
 *   with both @peer and @message_type set only @peer is used (per-peer
 *   type traffic has no history)
 * @seconds: how many of the most recent seconds to add up (including
 *   the current, partial one), clamped to the window
 * @out: (out caller-allocates): the summed counters
 *
 * Adds up the history buckets of the last @seconds seconds; divide by
 * @seconds for a rate.
 *
 * Returns: %TRUE when the peer / type is known (else @out is zeroed)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_stats_get_window_counters (LrgNetStats    *self,
                                            const gchar    *peer,
                                            const gchar    *message_type,
                                            guint           seconds,
                                            LrgNetCounters *out);

/**
 * lrg_net_stats_get_history:
 * @self: an #LrgNetStats
 * @peer: (nullable): a peer key, or %NULL for every peer
 * @message_type: (nullable): a message type, or %NULL for every type
 *   (ignored when @peer is set)
 * @metric: the quantity
 * @n_values: (out): the number of values, the window length
 *
 * Gets one value per second for the whole window, oldest first; the
 * last value is the current (partial) second. Seconds without traffic
 * are 0.
 *
 * Returns: (transfer full) (array length=n_values): the values
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble * lrg_net_stats_get_history (LrgNetStats       *self,
                                     const gchar       *peer,
                                     const gchar       *message_type,
                                     LrgNetStatsMetric  metric,
                                     guint             *n_values);

/**
 * lrg_net_stats_get_rtt:
 * @self: an #LrgNetStats
 * @peer: the peer key
 * @last_ms: (out) (optional): the latest sample
 * @average_ms: (out) (optional): exponentially smoothed average
 *   (RFC 6298 style, alpha 1/8)
 * @min_ms: (out) (optional): smallest sample
 * @max_ms: (out) (optional): largest sample
 *
 * Reads a peer's round-trip times.
 *
 * Returns: %TRUE when the peer has at least one RTT sample (outputs are
 *   0 otherwise)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_stats_get_rtt (LrgNetStats *self,
                                const gchar *peer,
                                gdouble     *last_ms,
                                gdouble     *average_ms,
                                gdouble     *min_ms,
                                gdouble     *max_ms);

/**
 * lrg_net_stats_get_loss:
 * @self: an #LrgNetStats
 * @peer: (nullable): a peer key, or %NULL for every peer
 *
 * The packet loss ratio since the last reset: lost / (lost + received).
 *
 * Returns: the ratio, 0.0 .. 1.0
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_net_stats_get_loss (LrgNetStats *self,
                                const gchar *peer);

G_END_DECLS
