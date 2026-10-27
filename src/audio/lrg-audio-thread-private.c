/* lrg-audio-thread-private.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lock-free node lists of lrg-audio-thread-private.h.
 */

#include "config.h"

#include "lrg-audio-thread-private.h"

/*
 * lrg_audio_list_push_chain:
 * Links @last to the current head and swings the head to @first; a
 * failed swap (another push won) retries with the new head.  The
 * compare-and-swap is a full barrier, so the nodes' contents are
 * visible to whoever takes them.
 */
void
lrg_audio_list_push_chain (LrgAudioNode **head,
                           LrgAudioNode  *first,
                           LrgAudioNode  *last)
{
    LrgAudioNode *old;

    do
    {
        old = g_atomic_pointer_get (head);
        last->next = old;
    }
    while (!g_atomic_pointer_compare_and_exchange (head, old, first));
}

void
lrg_audio_list_push (LrgAudioNode **head,
                     LrgAudioNode  *node)
{
    lrg_audio_list_push_chain (head, node, node);
}

/* takes the whole stack (newest first) */
static LrgAudioNode *
take_all (LrgAudioNode **head)
{
    LrgAudioNode *old;

    do
    {
        old = g_atomic_pointer_get (head);
        if (old == NULL)
            return NULL;
    }
    while (!g_atomic_pointer_compare_and_exchange (head, old, NULL));
    return old;
}

/* the stack is newest first: reversing it gives the push order */
LrgAudioNode *
lrg_audio_list_take_fifo (LrgAudioNode **head)
{
    LrgAudioNode *node;
    LrgAudioNode *reversed;

    node = take_all (head);
    reversed = NULL;
    while (node != NULL)
    {
        LrgAudioNode *next;

        next = node->next;
        node->next = reversed;
        reversed = node;
        node = next;
    }
    return reversed;
}

void
lrg_audio_list_free_all (LrgAudioNode **head)
{
    LrgAudioNode *node;

    node = take_all (head);
    while (node != NULL)
    {
        LrgAudioNode *next;

        next = node->next;
        node->free_func (node);
        node = next;
    }
}

typedef union
{
    gfloat f;
    gint   i;
} FloatBits;

void
lrg_audio_atomic_float_set (gint   *atomic,
                            gfloat  value)
{
    FloatBits bits;

    bits.f = value;
    g_atomic_int_set (atomic, bits.i);
}

gfloat
lrg_audio_atomic_float_get (gint *atomic)
{
    FloatBits bits;

    bits.i = g_atomic_int_get (atomic);
    return bits.f;
}
