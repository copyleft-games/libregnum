/* lrg-meshopt.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * EXT_meshopt_compression decoding (see the header).  Every read is
 * bounds-checked against the source size; the codecs are:
 *
 * - attributes (0xa0): blocks of up to 256 elements; each block holds one
 *   byte lane after another, each lane a header of 2 bits per group of 16
 *   (0: zeros, 1: 2-bit sentinel, 2: 4-bit sentinel, 3: raw bytes) then
 *   the groups' bytes; the values are zigzag deltas against the previous
 *   element's byte, the first block's baseline being the last stride bytes
 *   of the stream (the tail, at least 32 bytes).
 * - triangles (0xe1): one code byte per triangle naming an edge of a
 *   16-entry edge FIFO and a third vertex (new, from a 16-entry vertex
 *   FIFO, last +- 1 or a varint delta), or a free triangle; a 16-byte
 *   table of aux codes ends the stream.
 * - indices (0xd1): varints, the low bit choosing one of two baselines,
 *   the rest a zigzag delta; a 4-byte tail.
 */

#include "config.h"

#include "graphics/lrg-meshopt.h"
#include "graphics/lrg-gltf-geometry.h"

#include <math.h>
#include <string.h>

#define ATTR_HEADER     (0xa0)
#define TRI_HEADER      (0xe1)
#define SEQ_HEADER      (0xd1)
#define ATTR_TAIL_MIN   (32)
#define GROUP           (16)

/* reading state: a cursor that never passes end */
typedef struct
{
    const guint8 *p;
    const guint8 *end;
    gboolean      bad;
} Reader;

static guint8
read_byte (Reader *r)
{
    if (r->p >= r->end)
    {
        r->bad = TRUE;
        return 0;
    }
    return *r->p++;
}

/* unsigned LEB128, at most 5 bytes (32 bits) */
static guint32
read_varint (Reader *r)
{
    guint32 value = 0;
    guint   shift;

    for (shift = 0; shift < 35; shift += 7)
    {
        guint8 b = read_byte (r);

        value |= (guint32)(b & 0x7f) << shift;
        if (b < 0x80)
            return value;
    }
    r->bad = TRUE;
    return value;
}

static guint8
unzigzag8 (guint8 v)
{
    return (guint8)((guint8)(0u - (v & 1u)) ^ (v >> 1));
}

static guint32
unzigzag32 (guint32 v)
{
    return (0u - (v & 1u)) ^ (v >> 1);
}

static void
put_index (guint8  *out,
           guint    stride,
           gsize    at,
           guint32  value)
{
    if (stride == 2)
    {
        out[at * 2] = (guint8)(value & 0xff);
        out[at * 2 + 1] = (guint8)((value >> 8) & 0xff);
    }
    else
    {
        out[at * 4] = (guint8)(value & 0xff);
        out[at * 4 + 1] = (guint8)((value >> 8) & 0xff);
        out[at * 4 + 2] = (guint8)((value >> 16) & 0xff);
        out[at * 4 + 3] = (guint8)((value >> 24) & 0xff);
    }
}

/* ------------------------------------------------------------------------ */
/* Attributes                                                               */
/* ------------------------------------------------------------------------ */

/* decode_group:
 * The 16 byte deltas of one group in @mode (the lane header's 2 bits). */
static void
decode_group (Reader *r,
              guint   mode,
              guint8 *deltas)
{
    const guint8 *packed;
    guint         m;

    switch (mode)
    {
    case 0:
        memset (deltas, 0, GROUP);
        return;
    case 1:
    case 2:
        {
            guint bits = mode == 1 ? 2 : 4;
            guint sentinel = (1u << bits) - 1u;
            guint packed_size = GROUP * bits / 8;

            if ((gsize)(r->end - r->p) < packed_size)
            {
                r->bad = TRUE;
                memset (deltas, 0, GROUP);
                return;
            }
            packed = r->p;
            r->p += packed_size;
            /* the highest bits of a byte hold the first value */
            for (m = 0; m < GROUP; m++)
            {
                guint per_byte = 8 / bits;
                guint shift = 8 - bits * (m % per_byte + 1);
                guint v = (packed[m / per_byte] >> shift) & sentinel;

                deltas[m] = v == sentinel ? read_byte (r) : (guint8)v;
            }
        }
        return;
    default:
        for (m = 0; m < GROUP; m++)
            deltas[m] = read_byte (r);
        return;
    }
}

static gboolean
decode_attributes (Reader  *r,
                   guint8  *out,
                   guint    count,
                   guint    stride)
{
    guint8 baseline[256];
    gsize  tail;
    guint  block_max;
    guint  base;

    tail = MAX ((gsize)stride, (gsize)ATTR_TAIL_MIN);
    if ((gsize)(r->end - r->p) < tail + 1 || read_byte (r) != ATTR_HEADER)
        return FALSE;
    memcpy (baseline, r->end - stride, stride);
    /* the stream proper stops where the tail begins */
    r->end -= tail;

    block_max = MIN ((8192u / stride) & ~15u, 256u);
    for (base = 0; base < count; base += block_max)
    {
        guint n = MIN (count - base, block_max);
        guint groups = (n + GROUP - 1) / GROUP;
        guint header_size = (groups + 3) / 4;
        guint lane;

        for (lane = 0; lane < stride; lane++)
        {
            const guint8 *header;
            guint         g;

            if ((gsize)(r->end - r->p) < header_size)
                return FALSE;
            header = r->p;
            r->p += header_size;
            for (g = 0; g < groups; g++)
            {
                guint8 deltas[GROUP];
                guint  mode = (header[g / 4] >> ((g % 4) * 2)) & 3u;
                guint  m;

                decode_group (r, mode, deltas);
                if (r->bad)
                    return FALSE;
                for (m = 0; m < GROUP; m++)
                {
                    guint element = base + g * GROUP + m;

                    if (element >= base + n)
                        break;
                    baseline[lane] = (guint8)(baseline[lane] + unzigzag8 (deltas[m]));
                    out[(gsize)element * stride + lane] = baseline[lane];
                }
            }
        }
    }
    /* everything up to the tail was used */
    return r->p == r->end;
}

/* ------------------------------------------------------------------------ */
/* Filters                                                                  */
/* ------------------------------------------------------------------------ */

static gint
get_s8 (const guint8 *p)
{
    return (gint)(gint8)p[0];
}

static gint
get_s16 (const guint8 *p)
{
    return (gint)(gint16)(guint16)(p[0] | (p[1] << 8));
}

static void
set_s16 (guint8 *p,
         gint    v)
{
    guint16 u = (guint16)(gint16)v;

    p[0] = (guint8)(u & 0xff);
    p[1] = (guint8)(u >> 8);
}

static gint
round_signed (gfloat v)
{
    return (gint)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static void
filter_octahedral (guint8 *data,
                   guint   count,
                   guint   stride)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        guint8 *e = data + (gsize)i * stride;
        gfloat  x;
        gfloat  y;
        gfloat  z;
        gfloat  t;
        gfloat  scale;
        gfloat  limit;
        gfloat  length;

        if (stride == 4)
        {
            x = (gfloat)get_s8 (e);
            y = (gfloat)get_s8 (e + 1);
            z = (gfloat)get_s8 (e + 2) - fabsf (x) - fabsf (y);
            limit = 127.0f;
        }
        else
        {
            x = (gfloat)get_s16 (e);
            y = (gfloat)get_s16 (e + 2);
            z = (gfloat)get_s16 (e + 4) - fabsf (x) - fabsf (y);
            limit = 32767.0f;
        }
        /* fold the lower hemisphere back */
        t = z >= 0.0f ? 0.0f : z;
        x += x >= 0.0f ? t : -t;
        y += y >= 0.0f ? t : -t;
        length = sqrtf (x * x + y * y + z * z);
        scale = length > 0.0f ? limit / length : 0.0f;
        if (stride == 4)
        {
            e[0] = (guint8)(gint8)round_signed (x * scale);
            e[1] = (guint8)(gint8)round_signed (y * scale);
            e[2] = (guint8)(gint8)round_signed (z * scale);
        }
        else
        {
            set_s16 (e, round_signed (x * scale));
            set_s16 (e + 2, round_signed (y * scale));
            set_s16 (e + 4, round_signed (z * scale));
        }
    }
}

static void
filter_quaternion (guint8 *data,
                   guint   count)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        guint8 *e = data + (gsize)i * 8;
        gint    packed = get_s16 (e + 6);
        gfloat  s = (gfloat)G_SQRT2 * 0.5f / (gfloat)(packed | 3);
        gfloat  x = (gfloat)get_s16 (e) * s;
        gfloat  y = (gfloat)get_s16 (e + 2) * s;
        gfloat  z = (gfloat)get_s16 (e + 4) * s;
        gfloat  ww = 1.0f - x * x - y * y - z * z;
        gfloat  w = sqrtf (ww > 0.0f ? ww : 0.0f);
        guint   largest = (guint)packed & 3u;

        set_s16 (e + ((largest + 1) & 3) * 2, round_signed (x * 32767.0f));
        set_s16 (e + ((largest + 2) & 3) * 2, round_signed (y * 32767.0f));
        set_s16 (e + ((largest + 3) & 3) * 2, round_signed (z * 32767.0f));
        set_s16 (e + largest * 2, round_signed (w * 32767.0f));
    }
}

static void
filter_exponential (guint8 *data,
                    gsize   size)
{
    gsize i;

    for (i = 0; i + 4 <= size; i += 4)
    {
        guint32 v = (guint32)data[i] | ((guint32)data[i + 1] << 8) |
                    ((guint32)data[i + 2] << 16) | ((guint32)data[i + 3] << 24);
        gint32  mantissa = (gint32)(v << 8) >> 8;
        gint32  exponent = (gint32)v >> 24;
        gfloat  f = ldexpf ((gfloat)mantissa, exponent);
        guint32 bits;

        memcpy (&bits, &f, 4);
        data[i] = (guint8)(bits & 0xff);
        data[i + 1] = (guint8)((bits >> 8) & 0xff);
        data[i + 2] = (guint8)((bits >> 16) & 0xff);
        data[i + 3] = (guint8)((bits >> 24) & 0xff);
    }
}

/* ------------------------------------------------------------------------ */
/* Indices                                                                  */
/* ------------------------------------------------------------------------ */

static void
push_vertex (guint32 *fifo,
             guint   *offset,
             guint32  v,
             gboolean advance)
{
    fifo[*offset] = v;
    if (advance)
        *offset = (*offset + 1) & 15u;
}

static void
push_edge (guint32 (*fifo)[2],
           guint    *offset,
           guint32   a,
           guint32   b)
{
    fifo[*offset][0] = a;
    fifo[*offset][1] = b;
    *offset = (*offset + 1) & 15u;
}

static gboolean
decode_triangles (Reader  *r,
                  guint8  *out,
                  guint    count,
                  guint    stride)
{
    guint32       edges[16][2];
    guint32       vertices[16];
    guint         edge_at = 0;
    guint         vertex_at = 0;
    guint32       next = 0;
    guint32       last = 0;
    const guint8 *codes;
    const guint8 *aux;
    gsize         triangles = count / 3;
    gsize         i;

    if ((gsize)(r->end - r->p) < 1 + triangles + 16 || read_byte (r) != TRI_HEADER)
        return FALSE;
    memset (edges, 0, sizeof edges);
    memset (vertices, 0, sizeof vertices);
    codes = r->p;
    r->p += triangles;
    aux = r->end - 16;
    r->end = aux;

    for (i = 0; i < triangles; i++)
    {
        guint   code = codes[i];
        guint32 a;
        guint32 b;
        guint32 c;

        if (code < 0xf0)
        {
            guint fe = code >> 4;
            guint fc = code & 15u;

            /* an edge of the FIFO and a third vertex */
            a = edges[(edge_at - 1 - fe) & 15u][0];
            b = edges[(edge_at - 1 - fe) & 15u][1];
            if (fc == 0)
            {
                c = next++;
                push_vertex (vertices, &vertex_at, c, TRUE);
            }
            else if (fc < 13)
            {
                c = vertices[(vertex_at - 1 - fc) & 15u];
            }
            else
            {
                /* 13, 14: last -1, +1; 15: a varint delta */
                if (fc == 13)
                    c = --last;
                else if (fc == 14)
                    c = ++last;
                else
                    c = last = last + unzigzag32 (read_varint (r));
                push_vertex (vertices, &vertex_at, c, TRUE);
            }
            push_edge (edges, &edge_at, c, b);
            push_edge (edges, &edge_at, a, c);
        }
        else
        {
            guint fb;
            guint fc;

            if (code < 0xfe)
            {
                /* a new vertex and two from the aux table's codes */
                guint8 e = aux[code & 15u];

                fb = e >> 4;
                fc = e & 15u;
                a = next++;
                b = fb == 0 ? next++ : vertices[(vertex_at - fb) & 15u];
                c = fc == 0 ? next++ : vertices[(vertex_at - fc) & 15u];
                push_vertex (vertices, &vertex_at, a, TRUE);
                push_vertex (vertices, &vertex_at, b, fb == 0);
                push_vertex (vertices, &vertex_at, c, fc == 0);
            }
            else
            {
                /* a free triangle: each vertex new, from the FIFO or a varint */
                guint8 e = read_byte (r);
                gboolean fa_explicit = code == 0xff;

                if (e == 0)
                    next = 0;
                fb = e >> 4;
                fc = e & 15u;
                a = fa_explicit ? 0 : next++;
                b = fb == 0 ? next++ : fb == 15 ? 0 : vertices[(vertex_at - fb) & 15u];
                c = fc == 0 ? next++ : fc == 15 ? 0 : vertices[(vertex_at - fc) & 15u];
                if (fa_explicit)
                    a = last = last + unzigzag32 (read_varint (r));
                if (fb == 15)
                    b = last = last + unzigzag32 (read_varint (r));
                if (fc == 15)
                    c = last = last + unzigzag32 (read_varint (r));
                push_vertex (vertices, &vertex_at, a, TRUE);
                push_vertex (vertices, &vertex_at, b, fb == 0 || fb == 15);
                push_vertex (vertices, &vertex_at, c, fc == 0 || fc == 15);
            }
            push_edge (edges, &edge_at, b, a);
            push_edge (edges, &edge_at, c, b);
            push_edge (edges, &edge_at, a, c);
        }
        if (r->bad)
            return FALSE;
        put_index (out, stride, i * 3, a);
        put_index (out, stride, i * 3 + 1, b);
        put_index (out, stride, i * 3 + 2, c);
    }
    return r->p == r->end;
}

static gboolean
decode_sequence (Reader  *r,
                 guint8  *out,
                 guint    count,
                 guint    stride)
{
    guint32 last[2] = { 0, 0 };
    guint   i;

    if ((gsize)(r->end - r->p) < 1 + 4 || read_byte (r) != SEQ_HEADER)
        return FALSE;
    r->end -= 4;
    for (i = 0; i < count; i++)
    {
        guint32 v = read_varint (r);
        guint   which = v & 1u;

        if (r->bad)
            return FALSE;
        last[which] += unzigzag32 (v >> 1);
        put_index (out, stride, i, last[which]);
    }
    return r->p == r->end;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

GBytes *
lrg_meshopt_decode (LrgMeshoptMode     mode,
                    LrgMeshoptFilter   filter,
                    const guint8      *source,
                    gsize              source_size,
                    guint              count,
                    guint              stride,
                    GError           **error)
{
    g_autofree guint8 *out = NULL;
    Reader             r;
    gboolean           ok;
    gsize              size;

    g_return_val_if_fail (source != NULL || source_size == 0, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* the extension's validity rules */
    if (mode == LRG_MESHOPT_MODE_ATTRIBUTES)
        ok = stride > 0 && stride % 4 == 0 && stride <= 256 &&
             (filter == LRG_MESHOPT_FILTER_NONE ||
              (filter == LRG_MESHOPT_FILTER_OCTAHEDRAL && (stride == 4 || stride == 8)) ||
              (filter == LRG_MESHOPT_FILTER_QUATERNION && stride == 8) ||
              filter == LRG_MESHOPT_FILTER_EXPONENTIAL);
    else if (mode == LRG_MESHOPT_MODE_TRIANGLES || mode == LRG_MESHOPT_MODE_INDICES)
        ok = (stride == 2 || stride == 4) && filter == LRG_MESHOPT_FILTER_NONE &&
             (mode != LRG_MESHOPT_MODE_TRIANGLES || count % 3 == 0);
    else
        ok = FALSE;
    if (!ok || count > LRG_GLTF_GEOMETRY_MAX_VERTICES)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "meshopt: mode %d, filter %d, stride %u and count %u do not go together",
                     (gint)mode, (gint)filter, stride, count);
        return NULL;
    }

    size = (gsize)count * stride;
    out = g_malloc0 (MAX (size, (gsize)1));
    r.p = source;
    r.end = source + source_size;
    r.bad = FALSE;
    if (mode == LRG_MESHOPT_MODE_ATTRIBUTES)
        ok = decode_attributes (&r, out, count, stride);
    else if (mode == LRG_MESHOPT_MODE_TRIANGLES)
        ok = decode_triangles (&r, out, count, stride);
    else
        ok = decode_sequence (&r, out, count, stride);
    if (!ok || r.bad)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "meshopt: the %s stream (%" G_GSIZE_FORMAT " bytes, %u x %u) is malformed",
                     mode == LRG_MESHOPT_MODE_ATTRIBUTES ? "attribute" :
                     mode == LRG_MESHOPT_MODE_TRIANGLES ? "triangle" : "index",
                     source_size, count, stride);
        return NULL;
    }

    if (filter == LRG_MESHOPT_FILTER_OCTAHEDRAL)
        filter_octahedral (out, count, stride);
    else if (filter == LRG_MESHOPT_FILTER_QUATERNION)
        filter_quaternion (out, count);
    else if (filter == LRG_MESHOPT_FILTER_EXPONENTIAL)
        filter_exponential (out, size);
    return g_bytes_new_take (g_steal_pointer (&out), size);
}
