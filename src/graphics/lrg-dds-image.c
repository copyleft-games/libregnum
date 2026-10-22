/* lrg-dds-image.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DDS reader and CPU decoder, written from Microsoft's public "DDS
 * Programming Guide" (DDS_HEADER, DDS_PIXELFORMAT, DDS_HEADER_DXT10),
 * the Direct3D block compression description (BC1 .. BC3) and the BC7
 * format description ("BC7 Format", Direct3D 11 documentation, which
 * matches the BPTC section of the Khronos Data Format Specification).
 *
 * Layout: "DDS " magic, a 124-byte DDS_HEADER whose DDS_PIXELFORMAT
 * starts at header offset 72, an optional 20-byte DX10 header when the
 * FourCC is "DX10", then the mip levels of the first surface back to
 * back.  Every read is bounds checked against the GBytes; the mip table
 * is computed once in parse() and decode() only touches offsets it
 * proved are inside the file.
 */

#include "config.h"

#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-dds-image.h"

#define DDS_MAGIC            (0x20534444u)  /* "DDS " */
#define DDS_HEADER_SIZE      (124u)
#define DDS_PIXELFORMAT_SIZE (32u)
#define DDS_DX10_SIZE        (20u)
#define DDS_MAX_LEVELS       (16u)

/* DDS_HEADER.dwFlags */
#define DDSD_MIPMAPCOUNT     (0x00020000u)
/* DDS_HEADER.dwCaps2 */
#define DDSCAPS2_CUBEMAP     (0x00000200u)
#define DDSCAPS2_VOLUME      (0x00200000u)
/* DDS_PIXELFORMAT.dwFlags */
#define DDPF_ALPHAPIXELS     (0x00000001u)
#define DDPF_ALPHA           (0x00000002u)
#define DDPF_FOURCC          (0x00000004u)
#define DDPF_RGB             (0x00000040u)
#define DDPF_LUMINANCE       (0x00020000u)

#define FOURCC(a, b, c, d) \
    ((guint32)(guint8)(a) | ((guint32)(guint8)(b) << 8) | \
     ((guint32)(guint8)(c) << 16) | ((guint32)(guint8)(d) << 24))

/* DXGI_FORMAT values accepted through the DX10 header */
#define DXGI_R8G8B8A8_UNORM       (28u)
#define DXGI_R8G8B8A8_UNORM_SRGB  (29u)
#define DXGI_BC1_UNORM            (71u)
#define DXGI_BC1_UNORM_SRGB       (72u)
#define DXGI_BC2_UNORM            (74u)
#define DXGI_BC2_UNORM_SRGB       (75u)
#define DXGI_BC3_UNORM            (77u)
#define DXGI_BC3_UNORM_SRGB       (78u)
#define DXGI_B8G8R8A8_UNORM       (87u)
#define DXGI_B8G8R8A8_UNORM_SRGB  (91u)
#define DXGI_BC7_UNORM            (98u)
#define DXGI_BC7_UNORM_SRGB       (99u)

typedef struct
{
    gsize offset;
    gsize size;
    guint width;
    guint height;
} DdsLevel;

struct _LrgDdsImage
{
    GObject       parent_instance;

    GBytes       *data;
    guint         width;
    guint         height;
    LrgDdsFormat  format;
    gboolean      has_alpha;

    /* uncompressed surfaces: bytes per pixel and the channel masks */
    guint         bytes_per_pixel;
    guint32       masks[4];       /* r, g, b, a */

    guint         n_levels;
    DdsLevel      levels[DDS_MAX_LEVELS];
};

G_DEFINE_FINAL_TYPE (LrgDdsImage, lrg_dds_image, G_TYPE_OBJECT)

static void
lrg_dds_image_finalize (GObject *object)
{
    LrgDdsImage *self = LRG_DDS_IMAGE (object);

    g_clear_pointer (&self->data, g_bytes_unref);

    G_OBJECT_CLASS (lrg_dds_image_parent_class)->finalize (object);
}

static void
lrg_dds_image_class_init (LrgDdsImageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_dds_image_finalize;
}

static void
lrg_dds_image_init (LrgDdsImage *self)
{
    self->format = LRG_DDS_FORMAT_RGBA;
}

/* little-endian u32 at @offset; the caller proved it is in range */
static guint32
read_u32 (const guint8 *p,
          gsize         offset)
{
    return (guint32)p[offset] | ((guint32)p[offset + 1] << 8) |
           ((guint32)p[offset + 2] << 16) | ((guint32)p[offset + 3] << 24);
}

static gboolean
is_compressed (LrgDdsFormat format)
{
    return format == LRG_DDS_FORMAT_DXT1 || format == LRG_DDS_FORMAT_DXT3 ||
           format == LRG_DDS_FORMAT_DXT5 || format == LRG_DDS_FORMAT_BC7;
}

/* bytes one level of @width x @height takes in @self's format */
static gsize
level_size (LrgDdsImage *self,
            guint        width,
            guint        height)
{
    if (is_compressed (self->format))
    {
        gsize blocks_x = ((gsize)width + 3) / 4;
        gsize blocks_y = ((gsize)height + 3) / 4;
        gsize block = self->format == LRG_DDS_FORMAT_DXT1 ? 8 : 16;

        return blocks_x * blocks_y * block;
    }
    return (gsize)width * (gsize)height * (gsize)self->bytes_per_pixel;
}

/*
 * set_masked_format:
 * Classifies an uncompressed DDS_PIXELFORMAT.  Bit counts other than
 * 8/16/24/32 are not real DDS files.
 */
static gboolean
set_masked_format (LrgDdsImage  *self,
                   guint32       pf_flags,
                   guint32       bit_count,
                   const guint8 *pf,
                   GError      **error)
{
    if (bit_count != 8 && bit_count != 16 && bit_count != 24 && bit_count != 32)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                     "DDS: unsupported bit count %u", bit_count);
        return FALSE;
    }

    self->bytes_per_pixel = bit_count / 8;
    self->masks[0] = read_u32 (pf, 16);
    self->masks[1] = read_u32 (pf, 20);
    self->masks[2] = read_u32 (pf, 24);
    self->masks[3] = (pf_flags & (DDPF_ALPHAPIXELS | DDPF_ALPHA)) != 0 ? read_u32 (pf, 28) : 0;

    if ((pf_flags & DDPF_RGB) != 0)
    {
        self->format = self->masks[3] != 0 ? LRG_DDS_FORMAT_RGBA : LRG_DDS_FORMAT_RGB;
    }
    else if ((pf_flags & DDPF_LUMINANCE) != 0)
    {
        self->format = self->masks[3] != 0 ? LRG_DDS_FORMAT_LUMINANCE_ALPHA
                                           : LRG_DDS_FORMAT_LUMINANCE;
    }
    else if ((pf_flags & DDPF_ALPHA) != 0)
    {
        self->format = LRG_DDS_FORMAT_ALPHA;
    }
    else
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                     "DDS: pixel format flags 0x%x name no known layout", pf_flags);
        return FALSE;
    }

    if (self->format != LRG_DDS_FORMAT_ALPHA && self->masks[0] == 0)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: uncompressed surface without a colour mask");
        return FALSE;
    }
    if (self->format == LRG_DDS_FORMAT_ALPHA && self->masks[3] == 0)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: alpha surface without an alpha mask");
        return FALSE;
    }
    self->has_alpha = self->masks[3] != 0;
    return TRUE;
}

/* DX10 header: only the DXGI formats the decoder can handle */
static gboolean
set_dxgi_format (LrgDdsImage  *self,
                 guint32       dxgi,
                 GError      **error)
{
    switch (dxgi)
    {
    case DXGI_BC1_UNORM:
    case DXGI_BC1_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_DXT1;
        break;
    case DXGI_BC2_UNORM:
    case DXGI_BC2_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_DXT3;
        break;
    case DXGI_BC3_UNORM:
    case DXGI_BC3_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_DXT5;
        break;
    case DXGI_BC7_UNORM:
    case DXGI_BC7_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_BC7;
        break;
    case DXGI_R8G8B8A8_UNORM:
    case DXGI_R8G8B8A8_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_RGBA;
        self->bytes_per_pixel = 4;
        self->masks[0] = 0x000000ffu;
        self->masks[1] = 0x0000ff00u;
        self->masks[2] = 0x00ff0000u;
        self->masks[3] = 0xff000000u;
        break;
    case DXGI_B8G8R8A8_UNORM:
    case DXGI_B8G8R8A8_UNORM_SRGB:
        self->format = LRG_DDS_FORMAT_RGBA;
        self->bytes_per_pixel = 4;
        self->masks[0] = 0x00ff0000u;
        self->masks[1] = 0x0000ff00u;
        self->masks[2] = 0x000000ffu;
        self->masks[3] = 0xff000000u;
        break;
    default:
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                     "DDS: unsupported DXGI format %u", dxgi);
        return FALSE;
    }
    self->has_alpha = TRUE;
    return TRUE;
}

/*
 * parse:
 * Validates the header, classifies the format and builds the mip table.
 * Level 0 must be complete; later levels that run past the end of the
 * file are dropped (some exporters write a short chain).
 */
static gboolean
parse (LrgDdsImage  *self,
       GError      **error)
{
    const guint8 *p;
    const guint8 *pf;
    gsize length;
    gsize offset;
    guint32 flags;
    guint32 pf_flags;
    guint32 fourcc;
    guint32 caps2;
    guint declared;
    guint w;
    guint h;
    guint i;

    p = g_bytes_get_data (self->data, &length);
    if (length < 4 + DDS_HEADER_SIZE || read_u32 (p, 0) != DDS_MAGIC)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: missing \"DDS \" magic or header (%" G_GSIZE_FORMAT " bytes)",
                     length);
        return FALSE;
    }
    if (read_u32 (p, 4) != DDS_HEADER_SIZE || read_u32 (p, 4 + 72) != DDS_PIXELFORMAT_SIZE)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: header size %u / pixel format size %u, expected 124 / 32",
                     read_u32 (p, 4), read_u32 (p, 4 + 72));
        return FALSE;
    }

    flags = read_u32 (p, 4 + 4);
    self->height = read_u32 (p, 4 + 8);
    self->width = read_u32 (p, 4 + 12);
    declared = read_u32 (p, 4 + 24);
    caps2 = read_u32 (p, 4 + 108);
    pf = p + 4 + 72;
    pf_flags = read_u32 (pf, 4);
    fourcc = read_u32 (pf, 8);

    if (self->width == 0 || self->height == 0 ||
        self->width > LRG_DDS_IMAGE_MAX_SIZE || self->height > LRG_DDS_IMAGE_MAX_SIZE)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: impossible size %u x %u", self->width, self->height);
        return FALSE;
    }
    if ((caps2 & DDSCAPS2_VOLUME) != 0)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                     "DDS: volume textures are not supported");
        return FALSE;
    }

    offset = 4 + DDS_HEADER_SIZE;
    if ((pf_flags & DDPF_FOURCC) != 0)
    {
        if (fourcc == FOURCC ('D', 'X', 'T', '1'))
        {
            self->format = LRG_DDS_FORMAT_DXT1;
            self->has_alpha = TRUE;
        }
        else if (fourcc == FOURCC ('D', 'X', 'T', '2') || fourcc == FOURCC ('D', 'X', 'T', '3'))
        {
            self->format = LRG_DDS_FORMAT_DXT3;
            self->has_alpha = TRUE;
        }
        else if (fourcc == FOURCC ('D', 'X', 'T', '4') || fourcc == FOURCC ('D', 'X', 'T', '5'))
        {
            self->format = LRG_DDS_FORMAT_DXT5;
            self->has_alpha = TRUE;
        }
        else if (fourcc == FOURCC ('D', 'X', '1', '0'))
        {
            if (length < offset + DDS_DX10_SIZE)
            {
                g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_TRUNCATED,
                             "DDS: DX10 header cut short");
                return FALSE;
            }
            /* resourceDimension 4 is Texture3D */
            if (read_u32 (p, offset + 4) == 4)
            {
                g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                             "DDS: volume textures are not supported");
                return FALSE;
            }
            if (!set_dxgi_format (self, read_u32 (p, offset), error))
                return FALSE;
            offset += DDS_DX10_SIZE;
        }
        else
        {
            gchar code[5];

            code[0] = (gchar)(fourcc & 0xff);
            code[1] = (gchar)((fourcc >> 8) & 0xff);
            code[2] = (gchar)((fourcc >> 16) & 0xff);
            code[3] = (gchar)((fourcc >> 24) & 0xff);
            code[4] = '\0';
            for (i = 0; i < 4; i++)
            {
                if (!g_ascii_isprint (code[i]))
                    code[i] = '?';
            }
            g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED,
                         "DDS: unsupported FourCC \"%s\"", code);
            return FALSE;
        }
    }
    else if (!set_masked_format (self, pf_flags, read_u32 (pf, 12), pf, error))
    {
        return FALSE;
    }

    /* the mip chain of the first surface (a cube map's +X face) */
    if ((flags & DDSD_MIPMAPCOUNT) == 0 || declared == 0)
        declared = 1;
    if (declared > DDS_MAX_LEVELS)
        declared = DDS_MAX_LEVELS;

    w = self->width;
    h = self->height;
    self->n_levels = 0;
    for (i = 0; i < declared; i++)
    {
        gsize size = level_size (self, w, h);

        if (offset > length || size > length - offset)
            break;
        self->levels[i].offset = offset;
        self->levels[i].size = size;
        self->levels[i].width = w;
        self->levels[i].height = h;
        self->n_levels++;
        offset += size;
        if (w == 1 && h == 1)
            break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    if (self->n_levels == 0)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_TRUNCATED,
                     "DDS: %u x %u surface needs %" G_GSIZE_FORMAT " bytes, file has %"
                     G_GSIZE_FORMAT " after the header",
                     self->width, self->height, level_size (self, self->width, self->height),
                     length > offset ? length - offset : (gsize)0);
        return FALSE;
    }
    return TRUE;
}

LrgDdsImage *
lrg_dds_image_new_from_bytes (GBytes  *data,
                              GError **error)
{
    g_autoptr(LrgDdsImage) self = NULL;

    g_return_val_if_fail (data != NULL, NULL);

    self = g_object_new (LRG_TYPE_DDS_IMAGE, NULL);
    self->data = g_bytes_ref (data);
    if (!parse (self, error))
        return NULL;
    return g_steal_pointer (&self);
}

LrgDdsImage *
lrg_dds_image_new_from_file (const gchar  *path,
                             GError      **error)
{
    g_autoptr(GMappedFile) mapped = NULL;
    g_autoptr(GBytes) bytes = NULL;

    g_return_val_if_fail (path != NULL, NULL);

    mapped = g_mapped_file_new (path, FALSE, error);
    if (mapped == NULL)
        return NULL;
    bytes = g_mapped_file_get_bytes (mapped);
    return lrg_dds_image_new_from_bytes (bytes, error);
}

guint
lrg_dds_image_get_width (LrgDdsImage *self)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), 0);
    return self->width;
}

guint
lrg_dds_image_get_height (LrgDdsImage *self)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), 0);
    return self->height;
}

LrgDdsFormat
lrg_dds_image_get_format (LrgDdsImage *self)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), LRG_DDS_FORMAT_RGBA);
    return self->format;
}

gboolean
lrg_dds_image_has_alpha (LrgDdsImage *self)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), FALSE);
    return self->has_alpha;
}

guint
lrg_dds_image_get_mip_count (LrgDdsImage *self)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), 0);
    return self->n_levels;
}

gboolean
lrg_dds_image_get_mip_size (LrgDdsImage *self,
                            guint        level,
                            guint       *out_width,
                            guint       *out_height)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), FALSE);

    if (level >= self->n_levels)
        return FALSE;
    if (out_width != NULL)
        *out_width = self->levels[level].width;
    if (out_height != NULL)
        *out_height = self->levels[level].height;
    return TRUE;
}

GBytes *
lrg_dds_image_get_mip_data (LrgDdsImage *self,
                            guint        level)
{
    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), NULL);

    if (level >= self->n_levels)
        return NULL;
    return g_bytes_new_from_bytes (self->data, self->levels[level].offset,
                                   self->levels[level].size);
}

/* --- block decoding --- */

static void
rgb565 (guint16  v,
        guint8  *out)
{
    guint r = (v >> 11) & 0x1f;
    guint g = (v >> 5) & 0x3f;
    guint b = v & 0x1f;

    out[0] = (guint8)((r << 3) | (r >> 2));
    out[1] = (guint8)((g << 2) | (g >> 4));
    out[2] = (guint8)((b << 3) | (b >> 2));
    out[3] = 255;
}

/*
 * decode_color_block:
 * The 8-byte BC1 colour block into a 4x4 RGBA tile.  @dxt1 enables the
 * three-colour mode with transparent black when c0 <= c1; BC2 and BC3
 * colour blocks always use four colours.
 */
static void
decode_color_block (const guint8 *block,
                    gboolean      dxt1,
                    guint8        tile[16][4])
{
    guint8 palette[4][4];
    guint16 c0 = (guint16)(block[0] | (block[1] << 8));
    guint16 c1 = (guint16)(block[2] | (block[3] << 8));
    guint32 bits = read_u32 (block, 4);
    guint i;
    guint k;

    rgb565 (c0, palette[0]);
    rgb565 (c1, palette[1]);
    if (!dxt1 || c0 > c1)
    {
        for (k = 0; k < 3; k++)
        {
            palette[2][k] = (guint8)((2 * palette[0][k] + palette[1][k]) / 3);
            palette[3][k] = (guint8)((palette[0][k] + 2 * palette[1][k]) / 3);
        }
        palette[2][3] = 255;
        palette[3][3] = 255;
    }
    else
    {
        for (k = 0; k < 3; k++)
            palette[2][k] = (guint8)((palette[0][k] + palette[1][k]) / 2);
        palette[2][3] = 255;
        palette[3][0] = 0;
        palette[3][1] = 0;
        palette[3][2] = 0;
        palette[3][3] = 0;
    }

    for (i = 0; i < 16; i++)
    {
        guint index = (bits >> (2 * i)) & 3;

        memcpy (tile[i], palette[index], 4);
    }
}

/* BC2: sixteen explicit 4-bit alphas */
static void
decode_explicit_alpha (const guint8 *block,
                       guint8        tile[16][4])
{
    guint i;

    for (i = 0; i < 16; i++)
    {
        guint8 nibble = (guint8)((block[i / 2] >> ((i % 2) * 4)) & 0xf);

        tile[i][3] = (guint8)(nibble * 17);
    }
}

/* BC3: two endpoints and sixteen 3-bit indices into 8 alphas */
static void
decode_interpolated_alpha (const guint8 *block,
                           guint8        tile[16][4])
{
    guint8 alpha[8];
    guint64 bits = 0;
    guint i;

    alpha[0] = block[0];
    alpha[1] = block[1];
    if (alpha[0] > alpha[1])
    {
        for (i = 1; i < 7; i++)
            alpha[i + 1] = (guint8)(((7 - i) * alpha[0] + i * alpha[1]) / 7);
    }
    else
    {
        for (i = 1; i < 5; i++)
            alpha[i + 1] = (guint8)(((5 - i) * alpha[0] + i * alpha[1]) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }

    for (i = 0; i < 6; i++)
        bits |= (guint64)block[2 + i] << (8 * i);
    for (i = 0; i < 16; i++)
        tile[i][3] = alpha[(bits >> (3 * i)) & 7];
}

/* --- BC7 (BPTC) ---
 *
 * Written from the BC7 format description (Direct3D 11 "BC7 Format")
 * and the BPTC section of the Khronos Data Format Specification; the
 * tables below are the specification's own (mode table, the 2- and
 * 3-subset partition tables, the anchor index tables and the index
 * interpolation weights).
 *
 * A block is 128 bits read least significant bit first (bit 0 is the
 * low bit of byte 0).  The mode is the number of 0 bits before the
 * first 1 bit; a first byte of 0 is the reserved mode, which decodes to
 * transparent black.  After the mode come, in order: the partition
 * number, the rotation, the index selection bit, the endpoints (all the
 * red fields of every subset's two endpoints, then all green, all blue,
 * then all alpha), the p-bits, the primary indices and the secondary
 * indices (modes 4 and 5 only).
 */

typedef struct
{
    guint8 subsets;         /* NS: 1, 2 or 3 */
    guint8 partition_bits;  /* PB */
    guint8 rotation_bits;   /* RB */
    guint8 selection_bits;  /* ISB */
    guint8 color_bits;      /* CB, per channel, before the p-bit */
    guint8 alpha_bits;      /* AB, 0 for an opaque mode */
    guint8 endpoint_pbits;  /* EPB: one p-bit per endpoint */
    guint8 shared_pbits;    /* SPB: one p-bit per subset */
    guint8 index_bits;      /* IB: primary index precision */
    guint8 index2_bits;     /* IB2: secondary index precision, 0 if none */
} Bc7Mode;

/* the specification's mode table, modes 0 .. 7 */
static const Bc7Mode bc7_modes[8] = {
    { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
    { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
    { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
    { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
    { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
    { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
    { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
    { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

/* the two-subset partitions: subset of each pixel, row-major */
static const guint8 bc7_partition2[64][16] = {
    { 0,0,1,1, 0,0,1,1, 0,0,1,1, 0,0,1,1 }, { 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1 },
    { 0,1,1,1, 0,1,1,1, 0,1,1,1, 0,1,1,1 }, { 0,0,0,1, 0,0,1,1, 0,0,1,1, 0,1,1,1 },
    { 0,0,0,0, 0,0,0,1, 0,0,0,1, 0,0,1,1 }, { 0,0,1,1, 0,1,1,1, 0,1,1,1, 1,1,1,1 },
    { 0,0,0,1, 0,0,1,1, 0,1,1,1, 1,1,1,1 }, { 0,0,0,0, 0,0,0,1, 0,0,1,1, 0,1,1,1 },
    { 0,0,0,0, 0,0,0,0, 0,0,0,1, 0,0,1,1 }, { 0,0,1,1, 0,1,1,1, 1,1,1,1, 1,1,1,1 },
    { 0,0,0,0, 0,0,0,1, 0,1,1,1, 1,1,1,1 }, { 0,0,0,0, 0,0,0,0, 0,0,0,1, 0,1,1,1 },
    { 0,0,0,1, 0,1,1,1, 1,1,1,1, 1,1,1,1 }, { 0,0,0,0, 0,0,0,0, 1,1,1,1, 1,1,1,1 },
    { 0,0,0,0, 1,1,1,1, 1,1,1,1, 1,1,1,1 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 1,1,1,1 },
    { 0,0,0,0, 1,0,0,0, 1,1,1,0, 1,1,1,1 }, { 0,1,1,1, 0,0,0,1, 0,0,0,0, 0,0,0,0 },
    { 0,0,0,0, 0,0,0,0, 1,0,0,0, 1,1,1,0 }, { 0,1,1,1, 0,0,1,1, 0,0,0,1, 0,0,0,0 },
    { 0,0,1,1, 0,0,0,1, 0,0,0,0, 0,0,0,0 }, { 0,0,0,0, 1,0,0,0, 1,1,0,0, 1,1,1,0 },
    { 0,0,0,0, 0,0,0,0, 1,0,0,0, 1,1,0,0 }, { 0,1,1,1, 0,0,1,1, 0,0,1,1, 0,0,0,1 },
    { 0,0,1,1, 0,0,0,1, 0,0,0,1, 0,0,0,0 }, { 0,0,0,0, 1,0,0,0, 1,0,0,0, 1,1,0,0 },
    { 0,1,1,0, 0,1,1,0, 0,1,1,0, 0,1,1,0 }, { 0,0,1,1, 0,1,1,0, 0,1,1,0, 1,1,0,0 },
    { 0,0,0,1, 0,1,1,1, 1,1,1,0, 1,0,0,0 }, { 0,0,0,0, 1,1,1,1, 1,1,1,1, 0,0,0,0 },
    { 0,1,1,1, 0,0,0,1, 1,0,0,0, 1,1,1,0 }, { 0,0,1,1, 1,0,0,1, 1,0,0,1, 1,1,0,0 },
    { 0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1 }, { 0,0,0,0, 1,1,1,1, 0,0,0,0, 1,1,1,1 },
    { 0,1,0,1, 1,0,1,0, 0,1,0,1, 1,0,1,0 }, { 0,0,1,1, 0,0,1,1, 1,1,0,0, 1,1,0,0 },
    { 0,0,1,1, 1,1,0,0, 0,0,1,1, 1,1,0,0 }, { 0,1,0,1, 0,1,0,1, 1,0,1,0, 1,0,1,0 },
    { 0,1,1,0, 1,0,0,1, 0,1,1,0, 1,0,0,1 }, { 0,1,0,1, 1,0,1,0, 1,0,1,0, 0,1,0,1 },
    { 0,1,1,1, 0,0,1,1, 1,1,0,0, 1,1,1,0 }, { 0,0,0,1, 0,0,1,1, 1,1,0,0, 1,0,0,0 },
    { 0,0,1,1, 0,0,1,0, 0,1,0,0, 1,1,0,0 }, { 0,0,1,1, 1,0,1,1, 1,1,0,1, 1,1,0,0 },
    { 0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0 }, { 0,0,1,1, 1,1,0,0, 1,1,0,0, 0,0,1,1 },
    { 0,1,1,0, 0,1,1,0, 1,0,0,1, 1,0,0,1 }, { 0,0,0,0, 0,1,1,0, 0,1,1,0, 0,0,0,0 },
    { 0,1,0,0, 1,1,1,0, 0,1,0,0, 0,0,0,0 }, { 0,0,1,0, 0,1,1,1, 0,0,1,0, 0,0,0,0 },
    { 0,0,0,0, 0,0,1,0, 0,1,1,1, 0,0,1,0 }, { 0,0,0,0, 0,1,0,0, 1,1,1,0, 0,1,0,0 },
    { 0,1,1,0, 1,1,0,0, 1,0,0,1, 0,0,1,1 }, { 0,0,1,1, 0,1,1,0, 1,1,0,0, 1,0,0,1 },
    { 0,1,1,0, 0,0,1,1, 1,0,0,1, 1,1,0,0 }, { 0,0,1,1, 1,0,0,1, 1,1,0,0, 0,1,1,0 },
    { 0,1,1,0, 1,1,0,0, 1,1,0,0, 1,0,0,1 }, { 0,1,1,0, 0,0,1,1, 0,0,1,1, 1,0,0,1 },
    { 0,1,1,1, 1,1,1,0, 1,0,0,0, 0,0,0,1 }, { 0,0,0,1, 1,0,0,0, 1,1,1,0, 0,1,1,1 },
    { 0,0,0,0, 1,1,1,1, 0,0,1,1, 0,0,1,1 }, { 0,0,1,1, 0,0,1,1, 1,1,1,1, 0,0,0,0 },
    { 0,0,1,0, 0,0,1,0, 1,1,1,0, 1,1,1,0 }, { 0,1,0,0, 0,1,0,0, 0,1,1,1, 0,1,1,1 }
};

/* the three-subset partitions */
static const guint8 bc7_partition3[64][16] = {
    { 0,0,1,1, 0,0,1,1, 0,2,2,1, 2,2,2,2 }, { 0,0,0,1, 0,0,1,1, 2,2,1,1, 2,2,2,1 },
    { 0,0,0,0, 2,0,0,1, 2,2,1,1, 2,2,1,1 }, { 0,2,2,2, 0,0,2,2, 0,0,1,1, 0,1,1,1 },
    { 0,0,0,0, 0,0,0,0, 1,1,2,2, 1,1,2,2 }, { 0,0,1,1, 0,0,1,1, 0,0,2,2, 0,0,2,2 },
    { 0,0,2,2, 0,0,2,2, 1,1,1,1, 1,1,1,1 }, { 0,0,1,1, 0,0,1,1, 2,2,1,1, 2,2,1,1 },
    { 0,0,0,0, 0,0,0,0, 1,1,1,1, 2,2,2,2 }, { 0,0,0,0, 1,1,1,1, 1,1,1,1, 2,2,2,2 },
    { 0,0,0,0, 1,1,1,1, 2,2,2,2, 2,2,2,2 }, { 0,0,1,2, 0,0,1,2, 0,0,1,2, 0,0,1,2 },
    { 0,1,1,2, 0,1,1,2, 0,1,1,2, 0,1,1,2 }, { 0,1,2,2, 0,1,2,2, 0,1,2,2, 0,1,2,2 },
    { 0,0,1,1, 0,1,1,2, 1,1,2,2, 1,2,2,2 }, { 0,0,1,1, 2,0,0,1, 2,2,0,0, 2,2,2,0 },
    { 0,0,0,1, 0,0,1,1, 0,1,1,2, 1,1,2,2 }, { 0,1,1,1, 0,0,1,1, 2,0,0,1, 2,2,0,0 },
    { 0,0,0,0, 1,1,2,2, 1,1,2,2, 1,1,2,2 }, { 0,0,2,2, 0,0,2,2, 0,0,2,2, 1,1,1,1 },
    { 0,1,1,1, 0,1,1,1, 0,2,2,2, 0,2,2,2 }, { 0,0,0,1, 0,0,0,1, 2,2,2,1, 2,2,2,1 },
    { 0,0,0,0, 0,0,1,1, 0,1,2,2, 0,1,2,2 }, { 0,0,0,0, 1,1,0,0, 2,2,1,0, 2,2,1,0 },
    { 0,1,2,2, 0,1,2,2, 0,0,1,1, 0,0,0,0 }, { 0,0,1,2, 0,0,1,2, 1,1,2,2, 2,2,2,2 },
    { 0,1,1,0, 1,2,2,1, 1,2,2,1, 0,1,1,0 }, { 0,0,0,0, 0,1,1,0, 1,2,2,1, 1,2,2,1 },
    { 0,0,2,2, 1,1,0,2, 1,1,0,2, 0,0,2,2 }, { 0,1,1,0, 0,1,1,0, 2,0,0,2, 2,2,2,2 },
    { 0,0,1,1, 0,1,2,2, 0,1,2,2, 0,0,1,1 }, { 0,0,0,0, 2,0,0,0, 2,2,1,1, 2,2,2,1 },
    { 0,0,0,0, 0,0,0,2, 1,1,2,2, 1,2,2,2 }, { 0,2,2,2, 0,0,2,2, 0,0,1,2, 0,0,1,1 },
    { 0,0,1,1, 0,0,1,2, 0,0,2,2, 0,2,2,2 }, { 0,1,2,0, 0,1,2,0, 0,1,2,0, 0,1,2,0 },
    { 0,0,0,0, 1,1,1,1, 2,2,2,2, 0,0,0,0 }, { 0,1,2,0, 1,2,0,1, 2,0,1,2, 0,1,2,0 },
    { 0,1,2,0, 2,0,1,2, 1,2,0,1, 0,1,2,0 }, { 0,0,1,1, 2,2,0,0, 1,1,2,2, 0,0,1,1 },
    { 0,0,1,1, 1,1,2,2, 2,2,0,0, 0,0,1,1 }, { 0,1,0,1, 0,1,0,1, 2,2,2,2, 2,2,2,2 },
    { 0,0,0,0, 0,0,0,0, 2,1,2,1, 2,1,2,1 }, { 0,0,2,2, 1,1,2,2, 0,0,2,2, 1,1,2,2 },
    { 0,0,2,2, 0,0,1,1, 0,0,2,2, 0,0,1,1 }, { 0,2,2,0, 1,2,2,1, 0,2,2,0, 1,2,2,1 },
    { 0,1,0,1, 2,2,2,2, 2,2,2,2, 0,1,0,1 }, { 0,0,0,0, 2,1,2,1, 2,1,2,1, 2,1,2,1 },
    { 0,1,0,1, 0,1,0,1, 0,1,0,1, 2,2,2,2 }, { 0,2,2,2, 0,1,1,1, 0,2,2,2, 0,1,1,1 },
    { 0,0,0,2, 1,1,1,2, 0,0,0,2, 1,1,1,2 }, { 0,0,0,0, 2,1,1,2, 2,1,1,2, 2,1,1,2 },
    { 0,2,2,2, 0,1,1,1, 0,1,1,1, 0,2,2,2 }, { 0,0,0,2, 1,1,1,2, 1,1,1,2, 0,0,0,2 },
    { 0,1,1,0, 0,1,1,0, 0,1,1,0, 2,2,2,2 }, { 0,0,0,0, 0,0,0,0, 2,1,1,2, 2,1,1,2 },
    { 0,1,1,0, 0,1,1,0, 2,2,2,2, 2,2,2,2 }, { 0,0,2,2, 0,0,1,1, 0,0,1,1, 0,0,2,2 },
    { 0,0,2,2, 1,1,2,2, 1,1,2,2, 0,0,2,2 }, { 0,0,0,0, 0,0,0,0, 0,0,0,0, 2,1,1,2 },
    { 0,0,0,2, 0,0,0,1, 0,0,0,2, 0,0,0,1 }, { 0,2,2,2, 1,2,2,2, 0,2,2,2, 1,2,2,2 },
    { 0,1,0,1, 2,2,2,2, 2,2,2,2, 2,2,2,2 }, { 0,1,1,1, 2,0,1,1, 2,2,0,1, 2,2,2,0 }
};

/* anchor pixel of the second subset of each two-subset partition */
static const guint8 bc7_anchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15,  2,  8,  2,  2,  8,  8, 15,  2,  8,  2,  2,  8,  8,  2,  2,
    15, 15,  6,  8,  2,  8, 15, 15,  2,  8,  2,  2,  2, 15, 15,  6,
     6,  2,  6,  8, 15, 15,  2,  2, 15, 15, 15, 15, 15,  2,  2, 15
};

/* anchor pixels of the second and third subsets of each three-subset
 * partition */
static const guint8 bc7_anchor3_second[64] = {
     3,  3, 15, 15,  8,  3, 15, 15,  8,  8,  6,  6,  6,  5,  3,  3,
     3,  3,  8, 15,  3,  3,  6, 10,  5,  8,  8,  6,  8,  5, 15, 15,
     8, 15,  3,  5,  6, 10,  8, 15, 15,  3, 15,  5, 15, 15, 15, 15,
     3, 15,  5,  5,  5,  8,  5, 10,  5, 10,  8, 13, 15, 12,  3,  3
};

static const guint8 bc7_anchor3_third[64] = {
    15,  8,  8,  3, 15, 15,  3,  8, 15, 15, 15, 15, 15, 15, 15,  8,
    15,  8, 15,  3, 15,  8, 15,  8,  3, 15,  6, 10, 15, 15, 10,  8,
    15,  3, 15, 10, 10,  8,  9, 10,  6, 15,  8, 15,  3,  6,  6,  8,
    15,  3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  3, 15, 15,  8
};

/* interpolation weights out of 64 for 2-, 3- and 4-bit indices */
static const guint8 bc7_weights2[4] = { 0, 21, 43, 64 };
static const guint8 bc7_weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const guint8 bc7_weights4[16] = {
    0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64
};

/* a 128-bit block read least significant bit first */
typedef struct
{
    const guint8 *block;
    guint         pos;
} Bc7Bits;

/* the next @count (0 .. 8) bits of the block; never reads past bit 127
 * because every mode's fields add up to exactly 128 bits */
static guint
bc7_read (Bc7Bits *bits,
          guint    count)
{
    guint value = 0;
    guint i;

    for (i = 0; i < count; i++)
    {
        guint bit = bits->pos + i;

        value |= (guint)((bits->block[bit >> 3] >> (bit & 7)) & 1) << i;
    }
    bits->pos += count;
    return value;
}

/* an endpoint channel of @count bits widened to 8 by replicating its
 * high bits into the low bits (the specification's unquantisation) */
static guint8
bc7_expand (guint value,
            guint count)
{
    value <<= 8 - count;
    return (guint8)(value | (value >> count));
}

/* ((64 - w) * e0 + w * e1 + 32) >> 6, the specification's interpolation */
static guint8
bc7_interpolate (guint8 e0,
                 guint8 e1,
                 guint  index,
                 guint  index_bits)
{
    guint w;

    if (index_bits == 2)
        w = bc7_weights2[index];
    else if (index_bits == 3)
        w = bc7_weights3[index];
    else
        w = bc7_weights4[index];
    return (guint8)(((64 - w) * e0 + w * e1 + 32) >> 6);
}

/* the subset pixel @i belongs to, for @subsets subsets and @partition */
static guint
bc7_subset_of (guint subsets,
               guint partition,
               guint i)
{
    if (subsets == 2)
        return bc7_partition2[partition][i];
    if (subsets == 3)
        return bc7_partition3[partition][i];
    return 0;
}

/* whether pixel @i is the anchor of its subset: its index is stored
 * with one bit fewer, the omitted high bit being 0 */
static gboolean
bc7_is_anchor (guint subsets,
               guint partition,
               guint i)
{
    guint subset;

    if (i == 0)
        return TRUE;
    subset = bc7_subset_of (subsets, partition, i);
    if (subsets == 2 && subset == 1)
        return i == bc7_anchor2[partition];
    if (subsets == 3 && subset == 1)
        return i == bc7_anchor3_second[partition];
    if (subsets == 3 && subset == 2)
        return i == bc7_anchor3_third[partition];
    return FALSE;
}

/*
 * decode_bc7_block:
 * One 16-byte BC7 block into a 4x4 RGBA tile.
 *
 * Endpoints: each channel field is CB (or AB) bits; a mode with p-bits
 * appends the endpoint's p-bit (EPB) or its subset's shared p-bit (SPB)
 * as a new least significant bit, to colour and alpha alike, and the
 * widened value is expanded to 8 bits.  Modes without alpha bits are
 * opaque (alpha 255).
 *
 * Indices: pixel i of subset s uses endpoints 2s and 2s+1.  Modes 4
 * and 5 carry a second index set: the primary indices (IB bits) are
 * stored first, then the secondary (IB2 bits).  Mode 5 always takes
 * colour from the primary set and alpha from the secondary; mode 4's
 * index selection bit swaps them when set.  Every other mode
 * interpolates colour and alpha with the same index.
 *
 * Rotation (modes 4 and 5): 1 swaps red and alpha, 2 green and alpha,
 * 3 blue and alpha, after interpolation.
 */
static void
decode_bc7_block (const guint8 *block,
                  guint8        tile[16][4])
{
    const Bc7Mode *m;
    Bc7Bits bits;
    guint8 endpoints[6][4];
    guint raw[6][4];
    guint8 index1[16];
    guint8 index2[16];
    guint mode;
    guint partition;
    guint rotation;
    guint selection;
    guint n_endpoints;
    guint color_bits;
    guint alpha_bits;
    guint e;
    guint c;
    guint i;

    /* reserved mode: no 1 bit in the first byte */
    if (block[0] == 0)
    {
        memset (tile, 0, 16 * 4);
        return;
    }
    mode = 0;
    while (((block[0] >> mode) & 1) == 0)
        mode++;
    m = &bc7_modes[mode];

    bits.block = block;
    bits.pos = mode + 1;
    partition = bc7_read (&bits, m->partition_bits);
    rotation = bc7_read (&bits, m->rotation_bits);
    selection = bc7_read (&bits, m->selection_bits);
    n_endpoints = (guint)m->subsets * 2;

    /* endpoint fields: channel-major (all red, all green, all blue,
     * then all alpha) */
    for (c = 0; c < 3; c++)
    {
        for (e = 0; e < n_endpoints; e++)
            raw[e][c] = bc7_read (&bits, m->color_bits);
    }
    for (e = 0; e < n_endpoints; e++)
        raw[e][3] = m->alpha_bits > 0 ? bc7_read (&bits, m->alpha_bits) : 0;

    /* p-bits widen every channel of the endpoint by one low bit */
    color_bits = m->color_bits;
    alpha_bits = m->alpha_bits;
    if (m->endpoint_pbits || m->shared_pbits)
    {
        guint pbits[6];

        if (m->endpoint_pbits)
        {
            for (e = 0; e < n_endpoints; e++)
                pbits[e] = bc7_read (&bits, 1);
        }
        else
        {
            for (e = 0; e < m->subsets; e++)
            {
                pbits[e * 2] = bc7_read (&bits, 1);
                pbits[e * 2 + 1] = pbits[e * 2];
            }
        }
        for (e = 0; e < n_endpoints; e++)
        {
            for (c = 0; c < 4; c++)
                raw[e][c] = (raw[e][c] << 1) | pbits[e];
        }
        color_bits++;
        if (alpha_bits > 0)
            alpha_bits++;
    }

    for (e = 0; e < n_endpoints; e++)
    {
        for (c = 0; c < 3; c++)
            endpoints[e][c] = bc7_expand (raw[e][c], color_bits);
        endpoints[e][3] = alpha_bits > 0 ? bc7_expand (raw[e][3], alpha_bits) : 255;
    }

    /* primary indices, then the secondary set; anchors lose a bit (the
     * secondary set's only anchor is pixel 0, as its modes have one
     * subset) */
    for (i = 0; i < 16; i++)
    {
        guint count = m->index_bits;

        if (bc7_is_anchor (m->subsets, partition, i))
            count--;
        index1[i] = (guint8)bc7_read (&bits, count);
    }
    for (i = 0; i < 16; i++)
    {
        index2[i] = 0;
        if (m->index2_bits > 0)
            index2[i] = (guint8)bc7_read (&bits, i == 0 ? m->index2_bits - 1u : m->index2_bits);
    }

    for (i = 0; i < 16; i++)
    {
        guint s = bc7_subset_of (m->subsets, partition, i);
        const guint8 *e0 = endpoints[s * 2];
        const guint8 *e1 = endpoints[s * 2 + 1];
        guint color_index = index1[i];
        guint color_index_bits = m->index_bits;
        guint alpha_index = index1[i];
        guint alpha_index_bits = m->index_bits;
        guint8 swap;

        if (m->index2_bits > 0)
        {
            if (selection == 0)
            {
                alpha_index = index2[i];
                alpha_index_bits = m->index2_bits;
            }
            else
            {
                color_index = index2[i];
                color_index_bits = m->index2_bits;
            }
        }

        for (c = 0; c < 3; c++)
            tile[i][c] = bc7_interpolate (e0[c], e1[c], color_index, color_index_bits);
        tile[i][3] = bc7_interpolate (e0[3], e1[3], alpha_index, alpha_index_bits);

        if (rotation > 0)
        {
            swap = tile[i][3];
            tile[i][3] = tile[i][rotation - 1];
            tile[i][rotation - 1] = swap;
        }
    }
}

/* every block of a level in @format; @src holds them all (checked by
 * the caller) */
static void
decode_blocks (LrgDdsFormat  format,
               const guint8 *src,
               guint         width,
               guint         height,
               guint8       *dst)
{
    guint blocks_x = (width + 3) / 4;
    guint blocks_y = (height + 3) / 4;
    guint block_size = format == LRG_DDS_FORMAT_DXT1 ? 8 : 16;
    guint bx;
    guint by;

    for (by = 0; by < blocks_y; by++)
    {
        for (bx = 0; bx < blocks_x; bx++)
        {
            const guint8 *block = src + ((gsize)by * blocks_x + bx) * block_size;
            guint8 tile[16][4];
            guint px;
            guint py;

            if (format == LRG_DDS_FORMAT_DXT1)
            {
                decode_color_block (block, TRUE, tile);
            }
            else if (format == LRG_DDS_FORMAT_BC7)
            {
                decode_bc7_block (block, tile);
            }
            else
            {
                decode_color_block (block + 8, FALSE, tile);
                if (format == LRG_DDS_FORMAT_DXT3)
                    decode_explicit_alpha (block, tile);
                else
                    decode_interpolated_alpha (block, tile);
            }

            /* blocks overhanging a non-multiple-of-4 edge are clipped */
            for (py = 0; py < 4; py++)
            {
                guint y = by * 4 + py;

                if (y >= height)
                    break;
                for (px = 0; px < 4; px++)
                {
                    guint x = bx * 4 + px;

                    if (x >= width)
                        break;
                    memcpy (dst + ((gsize)y * width + x) * 4, tile[py * 4 + px], 4);
                }
            }
        }
    }
}

/* one channel of a masked pixel, scaled to 0 .. 255 */
static guint8
extract_channel (guint32 pixel,
                 guint32 mask)
{
    guint shift = 0;
    guint32 max;
    guint32 value;

    if (mask == 0)
        return 0;
    while (((mask >> shift) & 1) == 0)
        shift++;
    max = mask >> shift;
    value = (pixel & mask) >> shift;
    return (guint8)(((guint64)value * 255 + max / 2) / max);
}

static void
decode_masked (LrgDdsImage  *self,
               const guint8 *src,
               guint         width,
               guint         height,
               guint8       *dst)
{
    gsize n = (gsize)width * height;
    gsize i;

    for (i = 0; i < n; i++)
    {
        const guint8 *s = src + i * self->bytes_per_pixel;
        guint8 *d = dst + i * 4;
        guint32 pixel = 0;
        guint b;

        for (b = 0; b < self->bytes_per_pixel; b++)
            pixel |= (guint32)s[b] << (8 * b);

        switch (self->format)
        {
        case LRG_DDS_FORMAT_LUMINANCE:
        case LRG_DDS_FORMAT_LUMINANCE_ALPHA:
            d[0] = extract_channel (pixel, self->masks[0]);
            d[1] = d[0];
            d[2] = d[0];
            break;
        case LRG_DDS_FORMAT_ALPHA:
            d[0] = 0;
            d[1] = 0;
            d[2] = 0;
            break;
        case LRG_DDS_FORMAT_RGB:
        case LRG_DDS_FORMAT_RGBA:
        case LRG_DDS_FORMAT_DXT1:
        case LRG_DDS_FORMAT_DXT3:
        case LRG_DDS_FORMAT_DXT5:
        case LRG_DDS_FORMAT_BC7:
        default:
            d[0] = extract_channel (pixel, self->masks[0]);
            d[1] = extract_channel (pixel, self->masks[1]);
            d[2] = extract_channel (pixel, self->masks[2]);
            break;
        }
        d[3] = self->masks[3] != 0 ? extract_channel (pixel, self->masks[3]) : 255;
    }
}

GBytes *
lrg_dds_image_decode (LrgDdsImage  *self,
                      guint         level,
                      GError      **error)
{
    const guint8 *data;
    const DdsLevel *lv;
    guint8 *pixels;

    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), NULL);

    if (level >= self->n_levels)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: no mip level %u (the image has %u)", level, self->n_levels);
        return NULL;
    }

    lv = &self->levels[level];
    data = (const guint8 *)g_bytes_get_data (self->data, NULL) + lv->offset;
    pixels = g_malloc ((gsize)lv->width * lv->height * 4);
    if (is_compressed (self->format))
        decode_blocks (self->format, data, lv->width, lv->height, pixels);
    else
        decode_masked (self, data, lv->width, lv->height, pixels);
    return g_bytes_new_take (pixels, (gsize)lv->width * lv->height * 4);
}

GrlImage *
lrg_dds_image_to_image (LrgDdsImage  *self,
                        guint         level,
                        GError      **error)
{
    g_autoptr(GBytes) pixels = NULL;
    GrlImage *image;

    g_return_val_if_fail (LRG_IS_DDS_IMAGE (self), NULL);

    pixels = lrg_dds_image_decode (self, level, error);
    if (pixels == NULL)
        return NULL;

    image = grl_image_new_from_pixels ((gint)self->levels[level].width,
                                       (gint)self->levels[level].height,
                                       GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                                       g_bytes_get_data (pixels, NULL));
    if (image == NULL)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "DDS: could not create a %u x %u image",
                     self->levels[level].width, self->levels[level].height);
    }
    return image;
}

GBytes *
lrg_dds_decode_bc7 (const guint8  *blocks,
                    gsize          length,
                    guint          width,
                    guint          height,
                    GError       **error)
{
    gsize needed;
    guint8 *pixels;

    /* sizes first, so the block count below cannot overflow */
    if (width == 0 || height == 0 ||
        width > LRG_DDS_IMAGE_MAX_SIZE || height > LRG_DDS_IMAGE_MAX_SIZE)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID,
                     "BC7: impossible size %u x %u", width, height);
        return NULL;
    }

    needed = (((gsize)width + 3) / 4) * (((gsize)height + 3) / 4) * LRG_DDS_BC7_BLOCK_SIZE;
    if (blocks == NULL || length < needed)
    {
        g_set_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_TRUNCATED,
                     "BC7: %u x %u needs %" G_GSIZE_FORMAT " bytes of blocks, got %"
                     G_GSIZE_FORMAT, width, height, needed, blocks == NULL ? (gsize)0 : length);
        return NULL;
    }

    pixels = g_malloc ((gsize)width * height * 4);
    decode_blocks (LRG_DDS_FORMAT_BC7, blocks, width, height, pixels);
    return g_bytes_new_take (pixels, (gsize)width * height * 4);
}
