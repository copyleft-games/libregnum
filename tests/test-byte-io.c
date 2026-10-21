/* test-byte-io.c - LrgByteReader and LrgByteWriter
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include <glib.h>
#include <libregnum.h>

#include <string.h>

static void
test_reader_integers (void)
{
    static const guint8 data[] = {
        0x01,                               /* u8 */
        0x34, 0x12,                         /* u16 LE 0x1234 */
        0x56, 0x34, 0x12,                   /* u24 LE 0x123456 */
        0x78, 0x56, 0x34, 0x12,             /* u32 LE 0x12345678 */
        0xFF, 0xFF, 0xFF, 0xFF,             /* i32 LE -1 */
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, /* u64 LE */
        0x12, 0x34                          /* u16 BE 0x1234 */
    };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GError) error = NULL;
    guint8 u8;
    guint16 u16;
    guint32 u32;
    gint32 i32;
    guint64 u64;

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    g_assert_cmpuint (lrg_byte_reader_get_size (reader), ==, sizeof data);

    g_assert_true (lrg_byte_reader_read_u8 (reader, &u8, &error));
    g_assert_cmpuint (u8, ==, 0x01);
    g_assert_true (lrg_byte_reader_read_u16 (reader, &u16, &error));
    g_assert_cmpuint (u16, ==, 0x1234);
    g_assert_true (lrg_byte_reader_read_u24 (reader, &u32, &error));
    g_assert_cmpuint (u32, ==, 0x123456);
    g_assert_true (lrg_byte_reader_read_u32 (reader, &u32, &error));
    g_assert_cmpuint (u32, ==, 0x12345678);
    g_assert_true (lrg_byte_reader_read_i32 (reader, &i32, &error));
    g_assert_cmpint (i32, ==, -1);
    g_assert_true (lrg_byte_reader_read_u64 (reader, &u64, &error));
    g_assert_cmpuint (u64, ==, G_GUINT64_CONSTANT (0x0102030405060708));

    lrg_byte_reader_set_byte_order (reader, LRG_BYTE_ORDER_BIG_ENDIAN);
    g_assert_true (lrg_byte_reader_read_u16 (reader, &u16, &error));
    g_assert_cmpuint (u16, ==, 0x1234);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_byte_reader_get_remaining (reader), ==, 0);
}

/* a failed read reports EOF and moves nothing */
static void
test_reader_eof (void)
{
    static const guint8 data[] = { 0xAA, 0xBB, 0xCC };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GError) error = NULL;
    guint32 u32 = 7;
    guint16 u16;

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    g_assert_false (lrg_byte_reader_read_u32 (reader, &u32, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_EOF);
    g_assert_cmpuint (u32, ==, 7);
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 0);
    g_clear_error (&error);

    g_assert_true (lrg_byte_reader_skip (reader, 2, &error));
    g_assert_false (lrg_byte_reader_read_u16 (reader, &u16, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_EOF);
    g_clear_error (&error);
    g_assert_false (lrg_byte_reader_skip (reader, 2, &error));
    g_clear_error (&error);
    g_assert_null (lrg_byte_reader_read_data (reader, 2, &error));
    g_clear_error (&error);
    g_assert_null (lrg_byte_reader_read_bytes (reader, 2, &error));
    g_clear_error (&error);

    /* the end itself is a valid position, one past it is not */
    g_assert_true (lrg_byte_reader_seek (reader, 3, &error));
    g_assert_false (lrg_byte_reader_seek (reader, 4, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE);
}

static void
test_reader_floats (void)
{
    g_autoptr(LrgByteWriter) writer = NULL;
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GError) error = NULL;
    gfloat f;
    gdouble d;

    writer = lrg_byte_writer_new (LRG_BYTE_ORDER_BIG_ENDIAN);
    lrg_byte_writer_write_f32 (writer, 1.5f);
    lrg_byte_writer_write_f64 (writer, -2.25);
    bytes = lrg_byte_writer_to_bytes (writer);

    /* 1.5f is 0x3FC00000: big-endian puts 0x3F first */
    g_assert_cmpuint (((const guint8 *) g_bytes_get_data (bytes, NULL))[0], ==, 0x3F);

    reader = lrg_byte_reader_new (bytes, LRG_BYTE_ORDER_BIG_ENDIAN);
    g_assert_true (lrg_byte_reader_read_f32 (reader, &f, &error));
    g_assert_cmpfloat (f, ==, 1.5f);
    g_assert_true (lrg_byte_reader_read_f64 (reader, &d, &error));
    g_assert_cmpfloat (d, ==, -2.25);
}

static void
test_reader_strings (void)
{
    static const guint8 data[] = { 'a', 'b', 0, 'x', 'y', 'z', 0, 0, 'n', 'o' };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *s = NULL;

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    s = lrg_byte_reader_read_cstring (reader, 0, &error);
    g_assert_cmpstr (s, ==, "ab");
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 3);
    g_clear_pointer (&s, g_free);

    /* a fixed field stops at its first NUL */
    s = lrg_byte_reader_read_fixed_string (reader, 5, &error);
    g_assert_cmpstr (s, ==, "xyz");
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 8);
    g_clear_pointer (&s, g_free);

    /* "no" has no NUL before the end */
    g_assert_null (lrg_byte_reader_read_cstring (reader, 0, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID);
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 8);
    g_clear_error (&error);

    /* random access into a string block */
    s = lrg_byte_reader_get_cstring_at (reader, 3, &error);
    g_assert_cmpstr (s, ==, "xyz");
    g_clear_pointer (&s, g_free);
    g_assert_null (lrg_byte_reader_get_cstring_at (reader, 100, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE);
    g_clear_error (&error);

    /* max_length bounds the search */
    g_assert_true (lrg_byte_reader_seek (reader, 3, NULL));
    g_assert_null (lrg_byte_reader_read_cstring (reader, 2, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID);
    g_clear_error (&error);
    s = lrg_byte_reader_read_cstring (reader, 3, &error);
    g_assert_cmpstr (s, ==, "xyz");
}

static void
test_reader_magic_and_fourcc (void)
{
    static const guint8 data[] = { 'R', 'E', 'V', 'M', 0x12, 0x00, 0x00, 0x00 };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GError) error = NULL;
    guint32 tag;

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    g_assert_true (lrg_byte_reader_read_fourcc (reader, &tag, &error));
    /* a little-endian chunk tag stored reversed reads as the name */
    g_assert_cmpuint (tag, ==, ((guint32) 'M' << 24) | ((guint32) 'V' << 16) | ((guint32) 'E' << 8) | 'R');

    g_assert_false (lrg_byte_reader_expect_u32 (reader, 0x13, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID);
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 4);
    g_clear_error (&error);
    g_assert_true (lrg_byte_reader_expect_u32 (reader, 0x12, &error));
}

static void
test_sub_readers (void)
{
    static const guint8 data[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(LrgByteReader) sub = NULL;
    g_autoptr(LrgByteReader) chunk = NULL;
    g_autoptr(GError) error = NULL;
    guint8 b;
    guint32 u32;

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    sub = lrg_byte_reader_new_sub_reader (reader, 2, 3, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_byte_reader_get_size (sub), ==, 3);
    g_assert_true (lrg_byte_reader_read_u8 (sub, &b, &error));
    g_assert_cmpuint (b, ==, 2);
    /* the slice ends at 5: a 4-byte read cannot reach the neighbour */
    g_assert_false (lrg_byte_reader_read_u32 (sub, &u32, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_EOF);
    g_clear_error (&error);

    /* the parent's cursor never moved */
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 0);

    chunk = lrg_byte_reader_read_sub_reader (reader, 4, &error);
    g_assert_nonnull (chunk);
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 4);

    /* out of range, including offsets that would overflow */
    g_assert_null (lrg_byte_reader_new_sub_reader (reader, 6, 3, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE);
    g_clear_error (&error);
    g_assert_null (lrg_byte_reader_new_sub_reader (reader, 2, G_MAXSIZE, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE);
}

static void
test_align_and_bytes (void)
{
    static const guint8 data[] = { 9, 8, 7, 6, 5, 4, 3, 2 };
    g_autoptr(LrgByteReader) reader = NULL;
    g_autoptr(GBytes) slice = NULL;
    g_autoptr(GError) error = NULL;
    guint8 buf[2];

    reader = lrg_byte_reader_new_from_data (data, sizeof data, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    g_assert_true (lrg_byte_reader_skip (reader, 1, &error));
    g_assert_true (lrg_byte_reader_align (reader, 4, &error));
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 4);
    g_assert_true (lrg_byte_reader_align (reader, 4, &error));
    g_assert_cmpuint (lrg_byte_reader_get_position (reader), ==, 4);

    slice = lrg_byte_reader_read_bytes (reader, 2, &error);
    g_assert_cmpmem (g_bytes_get_data (slice, NULL), 2, data + 4, 2);
    g_assert_true (lrg_byte_reader_read_into (reader, buf, 2, &error));
    g_assert_cmpmem (buf, 2, data + 6, 2);
    g_assert_cmpmem (lrg_byte_reader_peek_data (reader, 0, &error), 0, NULL, 0);
}

static void
test_writer (void)
{
    g_autoptr(LrgByteWriter) writer = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GError) error = NULL;
    static const guint8 expected[] = {
        0x00, 0x00, 0x00, 0x00,     /* patched length: 0x0B */
        0xEF, 0xBE,                 /* u16 LE */
        0x03, 0x02, 0x01,           /* u24 LE */
        'h', 'i', 0,                /* cstring */
        'a', 'b', 0, 0,             /* fixed 4 */
        'l', 'o', 'n'               /* fixed 3, truncated */
    };
    guint8 want[sizeof expected];
    gsize len;

    writer = lrg_byte_writer_new (LRG_BYTE_ORDER_LITTLE_ENDIAN);
    lrg_byte_writer_write_u32 (writer, 0);
    lrg_byte_writer_write_u16 (writer, 0xBEEF);
    lrg_byte_writer_write_u24 (writer, 0x010203);
    lrg_byte_writer_write_cstring (writer, "hi");
    lrg_byte_writer_write_fixed_string (writer, "ab", 4);
    lrg_byte_writer_write_fixed_string (writer, "long", 3);
    g_assert_true (lrg_byte_writer_patch_u32 (writer, 0, 0x0B, &error));
    g_assert_false (lrg_byte_writer_patch_u32 (writer, 17, 1, &error));
    g_assert_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE);

    memcpy (want, expected, sizeof expected);
    want[0] = 0x0B;
    lrg_byte_writer_get_data (writer, &len);
    g_assert_cmpuint (len, ==, sizeof expected);
    bytes = lrg_byte_writer_steal_bytes (writer);
    g_assert_cmpmem (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), want, sizeof want);
    g_assert_cmpuint (lrg_byte_writer_get_size (writer), ==, 0);

    /* still usable after stealing */
    lrg_byte_writer_write_zeros (writer, 3);
    g_assert_cmpuint (lrg_byte_writer_get_size (writer), ==, 3);
    lrg_byte_writer_clear (writer);
    g_assert_cmpuint (lrg_byte_writer_get_size (writer), ==, 0);
}

/* the writer's output reads back through the reader in both orders */
static void
test_round_trip (void)
{
    LrgByteOrder orders[] = { LRG_BYTE_ORDER_LITTLE_ENDIAN, LRG_BYTE_ORDER_BIG_ENDIAN };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (orders); i++)
    {
        g_autoptr(LrgByteWriter) writer = NULL;
        g_autoptr(LrgByteReader) reader = NULL;
        g_autoptr(GBytes) bytes = NULL;
        guint64 u64;
        guint32 u32;
        gint32 i32;

        writer = lrg_byte_writer_new (orders[i]);
        lrg_byte_writer_write_u64 (writer, G_GUINT64_CONSTANT (0xDEADBEEFCAFEF00D));
        lrg_byte_writer_write_u32 (writer, 0xA1B2C3D4);
        lrg_byte_writer_write_i32 (writer, -12340);
        bytes = lrg_byte_writer_to_bytes (writer);

        reader = lrg_byte_reader_new (bytes, orders[i]);
        g_assert_true (lrg_byte_reader_read_u64 (reader, &u64, NULL));
        g_assert_cmpuint (u64, ==, G_GUINT64_CONSTANT (0xDEADBEEFCAFEF00D));
        g_assert_true (lrg_byte_reader_read_u32 (reader, &u32, NULL));
        g_assert_cmpuint (u32, ==, 0xA1B2C3D4);
        g_assert_true (lrg_byte_reader_read_i32 (reader, &i32, NULL));
        g_assert_cmpint (i32, ==, -12340);
    }
}

static void
test_boxed (void)
{
    g_autoptr(LrgByteReader) reader = NULL;
    LrgByteReader *copy;

    reader = lrg_byte_reader_new_from_data (NULL, 0, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    copy = g_boxed_copy (LRG_TYPE_BYTE_READER, reader);
    g_assert_true (copy == reader);
    g_boxed_free (LRG_TYPE_BYTE_READER, copy);
    g_assert_cmpstr (g_enum_to_string (LRG_TYPE_BYTE_ORDER, LRG_BYTE_ORDER_BIG_ENDIAN), ==,
                     "LRG_BYTE_ORDER_BIG_ENDIAN");
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/byte-io/reader/integers", test_reader_integers);
    g_test_add_func ("/byte-io/reader/eof", test_reader_eof);
    g_test_add_func ("/byte-io/reader/floats", test_reader_floats);
    g_test_add_func ("/byte-io/reader/strings", test_reader_strings);
    g_test_add_func ("/byte-io/reader/magic-and-fourcc", test_reader_magic_and_fourcc);
    g_test_add_func ("/byte-io/reader/sub-readers", test_sub_readers);
    g_test_add_func ("/byte-io/reader/align-and-bytes", test_align_and_bytes);
    g_test_add_func ("/byte-io/writer", test_writer);
    g_test_add_func ("/byte-io/round-trip", test_round_trip);
    g_test_add_func ("/byte-io/boxed", test_boxed);

    return g_test_run ();
}
