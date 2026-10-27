/* lrg-audio-thread-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal: the hand-off between the game thread and the audio device's
 * thread used by the synthesizers that render in a graylib stream
 * callback (LrgMidiSynth, LrgSoundBed, LrgProceduralAudio).  Not
 * installed or introspected.
 *
 * The audio thread owns a synthesizer's playing state while its stream
 * callback is attached; the game thread talks to it through two
 * lock-free lists of intrusive nodes (allocated and freed on the game
 * thread only, so the audio thread never allocates, frees or blocks):
 *
 *   - commands: the game thread pushes a node per API call; the audio
 *     thread takes them all, oldest first, at the start of a block and
 *     applies them in order;
 *   - garbage: the audio thread pushes what it is done with (applied
 *     commands, songs or layers it dropped); the game thread frees them
 *     on its next call.
 *
 * Both are Treiber stacks on one pointer: a push is a compare-and-swap
 * loop, a take swaps the head for NULL.  Pushes may come from several
 * threads; takes from one thread at a time (whoever owns the state).
 * With a single taker there is no ABA: a node is only pushed again
 * after the taker let go of it.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _LrgAudioNode LrgAudioNode;

/*
 * LrgAudioNode:
 * The first member of every command, song or layer that crosses
 * threads.  @free_func frees the whole object (it is called with the
 * node's address, which is the object's) on the game thread.
 */
struct _LrgAudioNode
{
    LrgAudioNode   *next;
    GDestroyNotify  free_func;
};

/* pushes one node (any thread) */
G_GNUC_INTERNAL void
lrg_audio_list_push (LrgAudioNode **head,
                     LrgAudioNode  *node);

/* pushes a chain already linked from @first to @last (any thread) */
G_GNUC_INTERNAL void
lrg_audio_list_push_chain (LrgAudioNode **head,
                           LrgAudioNode  *first,
                           LrgAudioNode  *last);

/* takes every node, in the order they were pushed (oldest first) */
G_GNUC_INTERNAL LrgAudioNode *
lrg_audio_list_take_fifo (LrgAudioNode **head);

/* takes every node and frees each with its free_func (game thread) */
G_GNUC_INTERNAL void
lrg_audio_list_free_all (LrgAudioNode **head);

/* a gfloat through an atomic gint (the audio thread publishes levels) */
G_GNUC_INTERNAL void
lrg_audio_atomic_float_set (gint   *atomic,
                            gfloat  value);

G_GNUC_INTERNAL gfloat
lrg_audio_atomic_float_get (gint *atomic);

G_END_DECLS
