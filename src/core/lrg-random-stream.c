/* SPDX-License-Identifier: AGPL-3.0-or-later
 * PCG XSH-RR algorithm described by M.E. O'Neill, pcg-random.org.
 */
#include "config.h"
#include "lrg-random-stream.h"
#include <gio/gio.h>
#include <string.h>

struct _LrgRandomStream
{
    GObject parent_instance;
    guint64 state;
    guint64 increment;
};
G_DEFINE_TYPE (LrgRandomStream, lrg_random_stream, G_TYPE_OBJECT)
static void
lrg_random_stream_class_init (LrgRandomStreamClass *klass)
{
}
static void
lrg_random_stream_init (LrgRandomStream *self)
{
    self->increment = 1;
}

guint32
lrg_random_stream_next_uint (LrgRandomStream *self)
{
    guint64 before;
    guint32 bits;
    guint rotation;

    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), 0);
    before = self->state;
    self->state = before * G_GUINT64_CONSTANT (6364136223846793005) + self->increment;
    bits = (guint32) (((before >> 18) ^ before) >> 27);
    rotation = before >> 59;
    return (bits >> rotation) | (bits << ((32 - rotation) & 31));
}

LrgRandomStream *
lrg_random_stream_new (guint64 seed, guint64 sequence)
{
    LrgRandomStream *self = g_object_new (LRG_TYPE_RANDOM_STREAM, NULL);

    self->increment = (sequence << 1) | 1;
    lrg_random_stream_next_uint (self);
    self->state += seed;
    lrg_random_stream_next_uint (self);
    return self;
}

void
lrg_random_stream_advance (LrgRandomStream *self,
                           guint64          draws)
{
    guint64 multiplier = G_GUINT64_CONSTANT (6364136223846793005);
    guint64 increment;
    guint64 accumulated_multiplier = 1;
    guint64 accumulated_increment = 0;

    g_return_if_fail (LRG_IS_RANDOM_STREAM (self));

    increment = self->increment;
    /* Compose affine state transitions by squaring. Unsigned wraparound is
     * intentional: PCG's state arithmetic is modulo 2^64. */
    while (draws != 0)
    {
        if (draws & 1)
        {
            accumulated_multiplier *= multiplier;
            accumulated_increment = accumulated_increment * multiplier + increment;
        }
        increment *= multiplier + 1;
        multiplier *= multiplier;
        draws >>= 1;
    }
    self->state = accumulated_multiplier * self->state + accumulated_increment;
}

guint32
lrg_random_stream_bounded (LrgRandomStream *self, guint32 bound)
{
    guint32 value;
    guint32 threshold;

    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), 0);
    g_return_val_if_fail (bound > 0, 0);
    threshold = (guint32) (0u - bound) % bound;
    do
        value = lrg_random_stream_next_uint (self);
    while (value < threshold);
    return value % bound;
}

gdouble
lrg_random_stream_next_double (LrgRandomStream *self)
{
    guint64 high;
    guint64 low;

    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), 0.0);
    high = lrg_random_stream_next_uint (self) >> 5;
    low = lrg_random_stream_next_uint (self) >> 6;
    return (high * G_GUINT64_CONSTANT (67108864) + low) / 9007199254740992.0;
}

gchar *
lrg_random_stream_snapshot (LrgRandomStream *self)
{
    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), NULL);
    return g_strdup_printf ("pcg32-v1:%016" G_GINT64_MODIFIER "x:%016" G_GINT64_MODIFIER "x",
                            self->state, self->increment);
}

gboolean
lrg_random_stream_restore (LrgRandomStream *self, const gchar *state, GError **error)
{
    guint64 values[2] = { 0, 0 };
    guint i;
    guint j;

    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    if (state == NULL || strlen (state) != 42 ||
        strncmp (state, "pcg32-v1:", 9) != 0 || state[25] != ':')
        goto invalid;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 16; j++)
        {
            gint digit = g_ascii_xdigit_value (state[9 + i * 17 + j]);

            if (digit < 0)
                goto invalid;
            values[i] = (values[i] << 4) | digit;
        }
    if ((values[1] & 1) == 0)
        goto invalid;
    self->state = values[0];
    self->increment = values[1];
    return TRUE;
invalid:
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Invalid PCG32 v1 snapshot");
    return FALSE;
}

LrgRandomStream *
lrg_random_stream_copy (LrgRandomStream *self)
{
    LrgRandomStream *copy;

    g_return_val_if_fail (LRG_IS_RANDOM_STREAM (self), NULL);
    copy = g_object_new (LRG_TYPE_RANDOM_STREAM, NULL);
    copy->state = self->state;
    copy->increment = self->increment;
    return copy;
}
