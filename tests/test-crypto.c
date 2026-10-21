/* test-crypto.c - LrgBigNum, LrgSrp6Client, LrgArc4
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Known answers were computed independently (Python integers and
 * hashlib) while writing these tests; the ARC4 vectors are those of
 * RFC 6229.  The SRP-6 tests also run a small server written here from
 * the formulas, so client and server must agree end to end.
 */

#include <glib.h>
#include <libregnum.h>

#include <string.h>

/* The World of Warcraft 1.x-3.x login group (N big-endian, g = 7). */
#define WOW_N_HEX "894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7"
#define WOW_N_LEN (32)

/* Fixed ephemeral secrets so exchanges are reproducible. */
#define FIXED_A_HEX "6A1F4CDB7E2D9054A9C3E7F11B5D2C8E0F9A7B6C5D4E3F2A1B0C9D8E7F6A5B4C"
#define FIXED_B_HEX "3E5D7C9B1A2F4E6D8C0B9A7F5E3D1C2B4A6F8E0D2C4B6A8F0E1D3C5B7A9F8E6D"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static GBytes *
bytes_from_hex (const gchar *hex)
{
    gsize len;
    guint8 *buf;
    gsize i;

    len = strlen (hex) / 2;
    buf = g_malloc0 (MAX (len, 1));
    for (i = 0; i < len; i++)
        buf[i] = (guint8) ((g_ascii_xdigit_value (hex[2 * i]) << 4) |
                           g_ascii_xdigit_value (hex[2 * i + 1]));
    return g_bytes_new_take (buf, len);
}

static gchar *
bytes_to_hex (GBytes *bytes)
{
    const guint8 *data;
    GString *str;
    gsize len;
    gsize i;

    data = g_bytes_get_data (bytes, &len);
    str = g_string_new (NULL);
    for (i = 0; i < len; i++)
        g_string_append_printf (str, "%02x", data[i]);
    return g_string_free (str, FALSE);
}

static void
assert_bytes_hex (GBytes      *bytes,
                  const gchar *expected)
{
    g_autofree gchar *hex = NULL;

    g_assert_nonnull (bytes);
    hex = bytes_to_hex (bytes);
    g_assert_cmpstr (hex, ==, expected);
}

static LrgBigNum *
hex_num (const gchar *hex)
{
    g_autoptr(GError) error = NULL;
    LrgBigNum *n;

    n = lrg_big_num_new_from_hex (hex, &error);
    g_assert_no_error (error);
    g_assert_nonnull (n);
    return n;
}

static void
assert_num_hex (LrgBigNum   *n,
                const gchar *expected)
{
    g_autofree gchar *hex = NULL;

    g_assert_nonnull (n);
    hex = lrg_big_num_to_hex (n);
    g_assert_cmpstr (hex, ==, expected);
}

/* ==========================================================================
 * LrgBigNum
 * ========================================================================== */

static void
test_bignum_hex (void)
{
    g_autoptr(LrgBigNum) n = NULL;
    g_autoptr(LrgBigNum) zero = NULL;
    g_autoptr(LrgBigNum) p = NULL;
    g_autoptr(GError) error = NULL;
    guint64 v;

    n = hex_num ("0x00ff0A");
    assert_num_hex (n, "FF0A");
    g_assert_true (lrg_big_num_to_uint64 (n, &v));
    g_assert_cmpuint (v, ==, 0xFF0A);
    g_assert_cmpuint (lrg_big_num_get_num_bytes (n), ==, 2);
    g_assert_cmpuint (lrg_big_num_get_num_bits (n), ==, 16);

    zero = hex_num ("000");
    assert_num_hex (zero, "0");
    g_assert_true (lrg_big_num_is_zero (zero));
    g_assert_cmpuint (lrg_big_num_get_num_bytes (zero), ==, 0);
    g_assert_cmpuint (lrg_big_num_get_num_bits (zero), ==, 0);

    p = hex_num (WOW_N_HEX);
    assert_num_hex (p, WOW_N_HEX);
    g_assert_cmpuint (lrg_big_num_get_num_bytes (p), ==, 32);
    g_assert_cmpuint (lrg_big_num_get_num_bits (p), ==, 256);
    g_assert_false (lrg_big_num_to_uint64 (p, &v));

    /* odd digit counts keep no leading zero */
    g_clear_pointer (&n, lrg_big_num_unref);
    n = hex_num ("abc");
    assert_num_hex (n, "ABC");

    /* bad input */
    g_assert_null (lrg_big_num_new_from_hex ("", &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_new_from_hex ("0x", &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_new_from_hex ("-12", &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_new_from_hex ("12 34", &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_new_from_hex ("12g4", &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_PARSE);
}

static void
test_bignum_bytes (void)
{
    static const guint8 le[] = { 0x01, 0x02, 0x03, 0x00 };
    g_autoptr(LrgBigNum) a = NULL;
    g_autoptr(LrgBigNum) b = NULL;
    g_autoptr(LrgBigNum) c = NULL;
    g_autoptr(LrgBigNum) zero = NULL;
    g_autoptr(GBytes) out = NULL;
    g_autoptr(GBytes) in = NULL;
    guint64 v;

    /* trailing zero in LE = leading zero in value: ignored */
    a = lrg_big_num_new_from_data (le, sizeof le, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    g_assert_true (lrg_big_num_to_uint64 (a, &v));
    g_assert_cmpuint (v, ==, 0x030201);

    b = lrg_big_num_new_from_data (le, sizeof le, LRG_BYTE_ORDER_BIG_ENDIAN);
    g_assert_true (lrg_big_num_to_uint64 (b, &v));
    g_assert_cmpuint (v, ==, 0x01020300);

    /* natural length, then padded on the most significant side */
    out = lrg_big_num_to_bytes (a, LRG_BYTE_ORDER_LITTLE_ENDIAN, 0);
    assert_bytes_hex (out, "010203");
    g_clear_pointer (&out, g_bytes_unref);
    out = lrg_big_num_to_bytes (a, LRG_BYTE_ORDER_LITTLE_ENDIAN, 6);
    assert_bytes_hex (out, "010203000000");
    g_clear_pointer (&out, g_bytes_unref);
    out = lrg_big_num_to_bytes (a, LRG_BYTE_ORDER_BIG_ENDIAN, 6);
    assert_bytes_hex (out, "000000030201");
    g_clear_pointer (&out, g_bytes_unref);

    /* never truncated */
    out = lrg_big_num_to_bytes (a, LRG_BYTE_ORDER_BIG_ENDIAN, 2);
    assert_bytes_hex (out, "030201");
    g_clear_pointer (&out, g_bytes_unref);

    /* zero */
    zero = lrg_big_num_new_from_data (NULL, 0, LRG_BYTE_ORDER_BIG_ENDIAN);
    g_assert_true (lrg_big_num_is_zero (zero));
    out = lrg_big_num_to_bytes (zero, LRG_BYTE_ORDER_BIG_ENDIAN, 0);
    g_assert_cmpuint (g_bytes_get_size (out), ==, 0);
    g_clear_pointer (&out, g_bytes_unref);
    out = lrg_big_num_to_bytes (zero, LRG_BYTE_ORDER_LITTLE_ENDIAN, 3);
    assert_bytes_hex (out, "000000");
    g_clear_pointer (&out, g_bytes_unref);

    /* round trip through GBytes in both orders */
    in = bytes_from_hex ("894b645e89e1535bbdad5b8b290650530801b18ebfbf5e8fab3c82872a3e9bb7");
    c = lrg_big_num_new_from_bytes (in, LRG_BYTE_ORDER_BIG_ENDIAN);
    assert_num_hex (c, WOW_N_HEX);
    out = lrg_big_num_to_bytes (c, LRG_BYTE_ORDER_LITTLE_ENDIAN, WOW_N_LEN);
    assert_bytes_hex (out, "b79b3e2a87823cab8f5ebfbf8eb10108535006298b5badbd5b53e1895e644b89");
    g_clear_pointer (&c, lrg_big_num_unref);
    c = lrg_big_num_new_from_bytes (out, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    assert_num_hex (c, WOW_N_HEX);

    /* uint64 extremes */
    g_clear_pointer (&c, lrg_big_num_unref);
    c = lrg_big_num_new_from_uint64 (G_MAXUINT64);
    assert_num_hex (c, "FFFFFFFFFFFFFFFF");
    g_assert_true (lrg_big_num_to_uint64 (c, &v));
    g_assert_cmpuint (v, ==, G_MAXUINT64);
}

static void
test_bignum_arith (void)
{
    g_autoptr(LrgBigNum) a = NULL;
    g_autoptr(LrgBigNum) b = NULL;
    g_autoptr(LrgBigNum) m = NULL;
    g_autoptr(LrgBigNum) zero = NULL;
    g_autoptr(LrgBigNum) r = NULL;
    g_autoptr(LrgBigNum) big = NULL;
    g_autoptr(LrgBigNum) big_e = NULL;
    g_autoptr(LrgBigNum) a_copy = NULL;
    g_autoptr(GError) error = NULL;
    guint64 v;

    a = lrg_big_num_new_from_uint64 (100);
    b = lrg_big_num_new_from_uint64 (37);
    m = lrg_big_num_new_from_uint64 (17);
    zero = lrg_big_num_new_from_uint64 (0);

    g_assert_cmpint (lrg_big_num_compare (a, b), >, 0);
    g_assert_cmpint (lrg_big_num_compare (b, a), <, 0);
    a_copy = lrg_big_num_new_from_uint64 (100);
    g_assert_cmpint (lrg_big_num_compare (a, a_copy), ==, 0);
    g_assert_true (lrg_big_num_equal (a, a_copy));
    g_assert_false (lrg_big_num_equal (a, b));

#define CHECK(expr, expected) \
    G_STMT_START { \
        r = (expr); \
        g_assert_no_error (error); \
        g_assert_true (lrg_big_num_to_uint64 (r, &v)); \
        g_assert_cmpuint (v, ==, (expected)); \
        g_clear_pointer (&r, lrg_big_num_unref); \
    } G_STMT_END

    CHECK (lrg_big_num_add (a, b), 137);
    CHECK (lrg_big_num_sub (a, b, &error), 63);
    CHECK (lrg_big_num_sub (a, a, &error), 0);
    CHECK (lrg_big_num_mul (a, b), 3700);
    CHECK (lrg_big_num_mod (a, m, &error), 15);
    CHECK (lrg_big_num_mod_add (a, b, m, &error), 137 % 17);
    CHECK (lrg_big_num_mod_sub (a, b, m, &error), 63 % 17);
    /* (37 - 100) mod 17 = -63 mod 17 = 5: never negative */
    CHECK (lrg_big_num_mod_sub (b, a, m, &error), 5);
    CHECK (lrg_big_num_mod_mul (a, b, m, &error), 3700 % 17);
    /* 100^37 mod 17: 100 = 15 (mod 17), 15^16 = 1, 15^37 = 15^5 = 2 */
    CHECK (lrg_big_num_mod_exp (a, b, m, &error), 2);
    /* even modulus takes the non-constant-time path: 3^5 mod 16 = 3 */
    {
        g_autoptr(LrgBigNum) three = lrg_big_num_new_from_uint64 (3);
        g_autoptr(LrgBigNum) five = lrg_big_num_new_from_uint64 (5);
        g_autoptr(LrgBigNum) sixteen = lrg_big_num_new_from_uint64 (16);

        CHECK (lrg_big_num_mod_exp (three, five, sixteen, &error), 3);
        CHECK (lrg_big_num_mod_exp (three, zero, sixteen, &error), 1);
    }
#undef CHECK

    /* negative results and zero moduli are errors */
    g_assert_null (lrg_big_num_sub (b, a, &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_NEGATIVE);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_mod (a, zero, &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_mod_exp (a, b, zero, &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO);
    g_clear_error (&error);
    g_assert_null (lrg_big_num_mod_mul (a, b, zero, &error));
    g_assert_error (error, LRG_BIG_NUM_ERROR, LRG_BIG_NUM_ERROR_DIVISION_BY_ZERO);
    g_clear_error (&error);

    /* inputs are untouched (immutable) */
    g_assert_true (lrg_big_num_to_uint64 (a, &v));
    g_assert_cmpuint (v, ==, 100);

    /* large values, checked against Python */
    big = hex_num ("123456789ABCDEF0FEDCBA9876543210");
    big_e = hex_num ("DEADBEEFCAFEBABE0123456789ABCDEFDEADBEEFCAFEBABE0123456789ABCDEF");
    r = lrg_big_num_mul (big, big_e);
    assert_num_hex (r, "FD5BDEEEB2A01D8C92DB8CD43D35B8D7DF18F93F288F30AEB64915D2934E87D6E1BD1A5075EF1322236D88FE5618CF0");
    g_clear_pointer (&r, lrg_big_num_unref);

    g_clear_pointer (&m, lrg_big_num_unref);
    m = hex_num (WOW_N_HEX);
    r = lrg_big_num_mod_exp (big, big_e, m, &error);
    g_assert_no_error (error);
    assert_num_hex (r, "458976F941E1CB8A9121CD0EB6D44D1F4978323C43DE0EBFF919D00300C78AE2");
    g_clear_pointer (&r, lrg_big_num_unref);

    g_clear_pointer (&m, lrg_big_num_unref);
    m = hex_num ("100000000000000000000000000000000000000000000006072");
    r = lrg_big_num_mod_exp (big, big_e, m, &error);
    g_assert_no_error (error);
    assert_num_hex (r, "D5DDC0B4FA3D351756C4FDC414BBDCC7AFE7AB5C7BA2A9ED60");
}

static void
test_bignum_random (void)
{
    g_autoptr(LrgBigNum) a = NULL;
    g_autoptr(LrgBigNum) b = NULL;
    g_autoptr(LrgBigNum) z = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    a = lrg_big_num_new_random (256, &error);
    g_assert_no_error (error);
    b = lrg_big_num_new_random (256, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_big_num_get_num_bits (a), <=, 256);
    /* 2^-256 chance of a false failure */
    g_assert_false (lrg_big_num_equal (a, b));

    /* odd bit counts stay below 2^n */
    for (i = 0; i < 64; i++)
    {
        g_autoptr(LrgBigNum) r = lrg_big_num_new_random (13, &error);

        g_assert_no_error (error);
        g_assert_cmpuint (lrg_big_num_get_num_bits (r), <=, 13);
    }

    z = lrg_big_num_new_random (0, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_big_num_is_zero (z));
}

/* ==========================================================================
 * LrgArc4
 * ========================================================================== */

typedef struct
{
    guint        offset;
    const gchar *hex;
} Arc4Vector;

/* RFC 6229 section 2, keystream at selected offsets for three keys. */
static const Arc4Vector rfc6229_40[] = {
    {    0, "b2396305f03dc027ccc3524a0a1118a8" },
    {   16, "6982944f18fc82d589c403a47a0d0919" },
    {  240, "28cb1132c96ce286421dcaadb8b69eae" },
    {  256, "1cfcf62b03eddb641d77dfcf7f8d8c93" },
    {  496, "42b7d0cdd918a8a33dd51781c81f4041" },
    {  512, "6459844432a7da923cfb3eb4980661f6" },
    {  752, "ec10327bde2beefd18f9277680457e22" },
    {  768, "eb62638d4f0ba1fe9fca20e05bf8ff2b" },
    { 1008, "45129048e6a0ed0b56b490338f078da5" },
    { 1024, "30abbcc7c20b01609f23ee2d5f6bb7df" },
    { 1520, "3294f744d8f9790507e70f62e5bbceea" },
    { 1536, "d8729db41882259bee4f825325f5a130" },
    { 2032, "1eb14a0c13b3bf47fa2a0ba93ad45b8b" },
    { 2048, "cc582f8ba9f265e2b1be9112e975d2d7" },
    { 3056, "f2e30f9bd102ecbf75aaade9bc35c43c" },
    { 3072, "ec0e11c479dc329dc8da7968fe965681" },
    { 4080, "068326a2118416d21f9d04b2cd1ca050" },
    { 4096, "ff25b58995996707e51fbdf08b34d875" }
};

static const Arc4Vector rfc6229_128[] = {
    {    0, "9ac7cc9a609d1ef7b2932899cde41b97" },
    {   16, "5248c4959014126a6e8a84f11d1a9e1c" },
    {  240, "065902e4b620f6cc36c8589f66432f2b" },
    {  256, "d39d566bc6bce3010768151549f3873f" },
    {  496, "b6d1e6c4a5e4771cad79538df295fb11" },
    {  512, "c68c1d5c559a974123df1dbc52a43b89" },
    {  752, "c5ecf88de897fd57fed301701b82a259" },
    {  768, "eccbe13de1fcc91c11a0b26c0bc8fa4d" },
    { 1008, "e7a72574f8782ae26aabcf9ebcd66065" },
    { 1024, "bdf0324e6083dcc6d3cedd3ca8c53c16" },
    { 1520, "b40110c4190b5622a96116b0017ed297" },
    { 1536, "ffa0b514647ec04f6306b892ae661181" },
    { 2032, "d03d1bc03cd33d70dff9fa5d71963ebd" },
    { 2048, "8a44126411eaa78bd51e8d87a8879bf5" },
    { 3056, "fabeb76028ade2d0e48722e46c4615a3" },
    { 3072, "c05d88abd50357f935a63c59ee537623" },
    { 4080, "ff38265c1642c1abe8d3c2fe5e572bf8" },
    { 4096, "a36a4c301ae8ac13610ccbc12256cacc" }
};

static const Arc4Vector rfc6229_256[] = {
    {    0, "eaa6bd25880bf93d3f5d1e4ca2611d91" },
    {   16, "cfa45c9f7e714b54bdfa80027cb14380" },
    {  240, "114ae344ded71b35f2e60febad727fd8" },
    {  256, "02e1e7056b0f623900496422943e97b6" },
    {  496, "91cb93c787964e10d9527d999c6f936b" },
    {  512, "49b18b42f8e8367cbeb5ef104ba1c7cd" },
    {  752, "87084b3ba700bade955610672745b374" },
    {  768, "e7a7b9e9ec540d5ff43bdb12792d1b35" },
    { 1008, "c799b596738f6b018c76c74b1759bd90" },
    { 1024, "7fec5bfd9f9b89ce6548309092d7e958" },
    { 1520, "40f250b26d1f096a4afd4c340a588815" },
    { 1536, "3e34135c79db010200767651cf263073" },
    { 2032, "f656abccf88dd827027b2ce917d464ec" },
    { 2048, "18b62503bfbc077fbabb98f20d98ab34" },
    { 3056, "8aed95ee5b0dcbfbef4eb21d3a3f52f9" },
    { 3072, "625a1ab00ee39a5327346bddb01a9c18" },
    { 4080, "a13a7c79c7e119b5ab0296ab28c300b9" },
    { 4096, "f3e4c0a2e02d1d01f7f0a74618af2b48" }
};

/*
 * check_rfc6229:
 * Encrypting zeros yields the keystream; compare every listed window.
 */
static void
check_rfc6229 (const gchar      *key_hex,
               const Arc4Vector *vectors,
               gsize             n_vectors)
{
    g_autoptr(GBytes) key = NULL;
    g_autoptr(LrgArc4) arc4 = NULL;
    guint8 stream[4112];
    gsize i;

    key = bytes_from_hex (key_hex);
    arc4 = lrg_arc4_new (key);
    memset (stream, 0, sizeof stream);
    lrg_arc4_process (arc4, stream, sizeof stream);

    for (i = 0; i < n_vectors; i++)
    {
        g_autoptr(GBytes) window = g_bytes_new (stream + vectors[i].offset, 16);

        assert_bytes_hex (window, vectors[i].hex);
    }
}

static void
test_arc4_rfc6229 (void)
{
    check_rfc6229 ("0102030405", rfc6229_40, G_N_ELEMENTS (rfc6229_40));
    check_rfc6229 ("0102030405060708090a0b0c0d0e0f10",
                   rfc6229_128, G_N_ELEMENTS (rfc6229_128));
    check_rfc6229 ("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                   rfc6229_256, G_N_ELEMENTS (rfc6229_256));
}

static void
test_arc4_drop (void)
{
    g_autoptr(GBytes) key = NULL;
    g_autoptr(GBytes) zeros = NULL;
    g_autoptr(GBytes) out = NULL;
    g_autoptr(LrgArc4) dropped = NULL;
    g_autoptr(LrgArc4) skipped = NULL;
    guint8 buf[16];

    key = bytes_from_hex ("0102030405");
    zeros = g_bytes_new (memset (buf, 0, sizeof buf), sizeof buf);

    /* drop-1024 starts at the RFC's offset-1024 window */
    dropped = lrg_arc4_new_drop (key, 1024);
    out = lrg_arc4_process_bytes (dropped, zeros);
    assert_bytes_hex (out, "30abbcc7c20b01609f23ee2d5f6bb7df");
    g_clear_pointer (&out, g_bytes_unref);

    /* discard in pieces is the same as one drop */
    skipped = lrg_arc4_new (key);
    lrg_arc4_discard (skipped, 1000);
    lrg_arc4_discard (skipped, 8);
    out = lrg_arc4_process_bytes (skipped, zeros);
    assert_bytes_hex (out, "45129048e6a0ed0b56b490338f078da5");
    g_clear_pointer (&out, g_bytes_unref);

    /* drop 0 is plain ARC4 */
    g_clear_pointer (&dropped, lrg_arc4_free);
    dropped = lrg_arc4_new_drop (key, 0);
    out = lrg_arc4_process_bytes (dropped, zeros);
    assert_bytes_hex (out, "b2396305f03dc027ccc3524a0a1118a8");
}

static void
test_arc4_stream_and_copy (void)
{
    static const gchar plain[] = "World of Warcraft world packet header";
    g_autoptr(GBytes) key = NULL;
    g_autoptr(GBytes) input = NULL;
    g_autoptr(GBytes) whole = NULL;
    g_autoptr(GBytes) back = NULL;
    g_autoptr(LrgArc4) enc = NULL;
    g_autoptr(LrgArc4) dec = NULL;
    g_autoptr(LrgArc4) copy = NULL;
    g_autoptr(GBytes) a = NULL;
    g_autoptr(GBytes) b = NULL;
    guint8 buf[sizeof plain];
    gsize len;

    key = bytes_from_hex ("c2b3723cc6aed9b5343c53ee2f4367ce");
    input = g_bytes_new_static (plain, sizeof plain);

    /* one call over everything equals calls in pieces */
    enc = lrg_arc4_new_drop (key, 1024);
    whole = lrg_arc4_process_bytes (enc, input);
    g_assert_false (g_bytes_equal (whole, input));

    g_clear_pointer (&enc, lrg_arc4_free);
    enc = lrg_arc4_new_drop (key, 1024);
    memcpy (buf, plain, sizeof plain);
    lrg_arc4_process (enc, buf, 4);
    lrg_arc4_process (enc, buf + 4, 0);
    lrg_arc4_process (enc, buf + 4, sizeof buf - 4);
    g_assert_cmpmem (buf, sizeof buf, g_bytes_get_data (whole, &len), g_bytes_get_size (whole));

    /* decryption is the same operation with a fresh state */
    dec = lrg_arc4_new_drop (key, 1024);
    back = lrg_arc4_process_bytes (dec, whole);
    g_assert_true (g_bytes_equal (back, input));

    /* a copy continues the keystream independently */
    copy = lrg_arc4_copy (dec);
    a = lrg_arc4_process_bytes (dec, input);
    b = lrg_arc4_process_bytes (copy, input);
    g_assert_true (g_bytes_equal (a, b));
    g_clear_pointer (&a, g_bytes_unref);
    a = lrg_arc4_process_bytes (dec, input);
    g_assert_false (g_bytes_equal (a, b));
}

/* ==========================================================================
 * SRP-6: interleaved hash
 * ========================================================================== */

static void
test_srp6_interleave (void)
{
    g_autoptr(GBytes) s = NULL;
    g_autoptr(GBytes) k = NULL;
    guint8 buf[32];
    guint i;

    /* no leading zeros */
    for (i = 0; i < 32; i++)
        buf[i] = (guint8) (i + 1);
    s = g_bytes_new (buf, sizeof buf);
    k = lrg_srp6_hash_interleave (G_CHECKSUM_SHA1, s);
    g_assert_cmpuint (g_bytes_get_size (k), ==, 40);
    assert_bytes_hex (k, "ed2976c475109d044df3443c9cc86c31077e9c7db8e04d3d699f68bd6bcc6c6b20df81f6b7d66fa0");
    g_clear_pointer (&s, g_bytes_unref);
    g_clear_pointer (&k, g_bytes_unref);

    /* one zero byte: skipped, and since 31 bytes remain the next one is
     * dropped too, leaving bytes 2..31 */
    for (i = 0; i < 32; i++)
        buf[i] = (guint8) i;
    s = g_bytes_new (buf, sizeof buf);
    k = lrg_srp6_hash_interleave (G_CHECKSUM_SHA1, s);
    assert_bytes_hex (k, "9f32f1e4f14df7375d78e0a1d04857bada15d3d91e3ba8100472b55f54c99c703e7592b8e38cb19c");
    g_clear_pointer (&s, g_bytes_unref);
    g_clear_pointer (&k, g_bytes_unref);

    /* two zero bytes: also bytes 2..31, so the same key */
    buf[1] = 0;
    s = g_bytes_new (buf, sizeof buf);
    k = lrg_srp6_hash_interleave (G_CHECKSUM_SHA1, s);
    assert_bytes_hex (k, "9f32f1e4f14df7375d78e0a1d04857bada15d3d91e3ba8100472b55f54c99c703e7592b8e38cb19c");
    g_clear_pointer (&s, g_bytes_unref);
    g_clear_pointer (&k, g_bytes_unref);

    /* three zero bytes: 29 remain, drop one more, bytes 4..31 */
    buf[2] = 0;
    s = g_bytes_new (buf, sizeof buf);
    k = lrg_srp6_hash_interleave (G_CHECKSUM_SHA1, s);
    assert_bytes_hex (k, "a8242d813f8d81dcfd347eee260dd9acd01c58ba12a8d6e319fd711c532c6de87821bbc4641ab616");
}

/* ==========================================================================
 * SRP-6: a test server written from the formulas
 * ========================================================================== */

typedef struct
{
    LrgSrp6Style  style;
    LrgByteOrder  order;
    LrgBigNum    *N;
    LrgBigNum    *g;
    LrgBigNum    *k;
    LrgBigNum    *v;
    LrgBigNum    *b;
    LrgBigNum    *B;
    GBytes       *salt;
    gchar        *user;
    gsize         n_len;
} TestServer;

static void
digest_into (GChecksum *cs,
             guint8    *out)
{
    gsize len = 64;

    g_checksum_get_digest (cs, out, &len);
    g_checksum_free (cs);
}

static void
update_num (GChecksum    *cs,
            LrgBigNum    *n,
            LrgByteOrder  order,
            gsize         pad)
{
    g_autoptr(GBytes) bytes = lrg_big_num_to_bytes (n, order, pad);
    gsize len;
    const guint8 *data = g_bytes_get_data (bytes, &len);

    g_checksum_update (cs, data, (gssize) len);
}

static gchar *
server_norm (LrgSrp6Style  style,
             const gchar  *s)
{
    return style == LRG_SRP6_STYLE_WOW ? g_ascii_strup (s, -1) : g_strdup (s);
}

static TestServer *
test_server_new (LrgSrp6Style  style,
                 const gchar  *user,
                 const gchar  *pass,
                 GBytes       *salt)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgBigNum) x = NULL;
    g_autoptr(LrgBigNum) kv = NULL;
    g_autoptr(LrgBigNum) gb = NULL;
    g_autofree gchar *u = NULL;
    g_autofree gchar *p = NULL;
    g_autofree gchar *cred = NULL;
    TestServer *srv;
    guint8 ih[64];
    guint8 d[64];
    GChecksum *cs;
    gsize len;

    srv = g_new0 (TestServer, 1);
    srv->style = style;
    srv->order = style == LRG_SRP6_STYLE_WOW ? LRG_BYTE_ORDER_LITTLE_ENDIAN
                                             : LRG_BYTE_ORDER_BIG_ENDIAN;
    srv->N = hex_num (WOW_N_HEX);
    srv->g = lrg_big_num_new_from_uint64 (7);
    srv->n_len = WOW_N_LEN;
    srv->salt = g_bytes_ref (salt);
    srv->user = server_norm (style, user);

    /* k */
    if (style == LRG_SRP6_STYLE_WOW)
        srv->k = lrg_big_num_new_from_uint64 (3);
    else
    {
        cs = g_checksum_new (G_CHECKSUM_SHA1);
        update_num (cs, srv->N, LRG_BYTE_ORDER_BIG_ENDIAN, srv->n_len);
        update_num (cs, srv->g, LRG_BYTE_ORDER_BIG_ENDIAN, srv->n_len);
        digest_into (cs, d);
        srv->k = lrg_big_num_new_from_data (d, 20, LRG_BYTE_ORDER_BIG_ENDIAN);
    }

    /* v = g^x, x = H(s | H(I ":" P)) */
    u = server_norm (style, user);
    p = server_norm (style, pass);
    cred = g_strconcat (u, ":", p, NULL);
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    g_checksum_update (cs, (const guchar *) cred, -1);
    digest_into (cs, ih);
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    g_checksum_update (cs, g_bytes_get_data (salt, &len), (gssize) g_bytes_get_size (salt));
    g_checksum_update (cs, ih, 20);
    digest_into (cs, d);
    x = lrg_big_num_new_from_data (d, 20, srv->order);
    srv->v = lrg_big_num_mod_exp (srv->g, x, srv->N, &error);
    g_assert_no_error (error);

    /* B = k*v + g^b */
    srv->b = hex_num (FIXED_B_HEX);
    kv = lrg_big_num_mod_mul (srv->k, srv->v, srv->N, &error);
    g_assert_no_error (error);
    gb = lrg_big_num_mod_exp (srv->g, srv->b, srv->N, &error);
    g_assert_no_error (error);
    srv->B = lrg_big_num_mod_add (kv, gb, srv->N, &error);
    g_assert_no_error (error);
    return srv;
}

static void
test_server_free (TestServer *srv)
{
    lrg_big_num_unref (srv->N);
    lrg_big_num_unref (srv->g);
    lrg_big_num_unref (srv->k);
    lrg_big_num_unref (srv->v);
    lrg_big_num_unref (srv->b);
    lrg_big_num_unref (srv->B);
    g_bytes_unref (srv->salt);
    g_free (srv->user);
    g_free (srv);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (TestServer, test_server_free)

static GBytes *
test_server_get_B (TestServer *srv)
{
    return lrg_big_num_to_bytes (srv->B, srv->order, srv->n_len);
}

/*
 * test_server_verify:
 * Checks the client's A and M1.  On success returns TRUE with the
 * server's K and M2.
 */
static gboolean
test_server_verify (TestServer  *srv,
                    GBytes      *A_bytes,
                    GBytes      *M1,
                    GBytes     **K_out,
                    GBytes     **M2_out)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgBigNum) A = NULL;
    g_autoptr(LrgBigNum) u = NULL;
    g_autoptr(LrgBigNum) vu = NULL;
    g_autoptr(LrgBigNum) avu = NULL;
    g_autoptr(LrgBigNum) S = NULL;
    g_autoptr(GBytes) K = NULL;
    g_autoptr(GBytes) mine = NULL;
    guint8 d[64];
    guint8 hg[64];
    gsize wide;
    GChecksum *cs;
    guint i;

    A = lrg_big_num_new_from_bytes (A_bytes, srv->order);

    /* u = H(PAD(A) | PAD(B)) */
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    update_num (cs, A, srv->order, srv->n_len);
    update_num (cs, srv->B, srv->order, srv->n_len);
    digest_into (cs, d);
    u = lrg_big_num_new_from_data (d, 20, srv->order);

    /* S = (A * v^u)^b */
    vu = lrg_big_num_mod_exp (srv->v, u, srv->N, &error);
    g_assert_no_error (error);
    avu = lrg_big_num_mod_mul (A, vu, srv->N, &error);
    g_assert_no_error (error);
    S = lrg_big_num_mod_exp (avu, srv->b, srv->N, &error);
    g_assert_no_error (error);

    /* K */
    if (srv->style == LRG_SRP6_STYLE_WOW)
    {
        g_autoptr(GBytes) s_bytes = lrg_big_num_to_bytes (S, srv->order, srv->n_len);

        K = lrg_srp6_hash_interleave (G_CHECKSUM_SHA1, s_bytes);
    }
    else
    {
        cs = g_checksum_new (G_CHECKSUM_SHA1);
        update_num (cs, S, srv->order, 0);
        digest_into (cs, d);
        K = g_bytes_new (d, 20);
    }

    /* M1 = H(H(N) xor H(g) | H(I) | s | A | B | K) */
    wide = srv->style == LRG_SRP6_STYLE_WOW ? srv->n_len : 0;
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    update_num (cs, srv->N, srv->order, wide);
    digest_into (cs, d);
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    update_num (cs, srv->g, srv->order, 0);
    digest_into (cs, hg);
    for (i = 0; i < 20; i++)
        d[i] ^= hg[i];
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    g_checksum_update (cs, d, 20);
    {
        GChecksum *hi = g_checksum_new (G_CHECKSUM_SHA1);

        g_checksum_update (hi, (const guchar *) srv->user, -1);
        digest_into (hi, hg);
    }
    g_checksum_update (cs, hg, 20);
    g_checksum_update (cs, g_bytes_get_data (srv->salt, NULL), (gssize) g_bytes_get_size (srv->salt));
    update_num (cs, A, srv->order, wide);
    update_num (cs, srv->B, srv->order, wide);
    g_checksum_update (cs, g_bytes_get_data (K, NULL), (gssize) g_bytes_get_size (K));
    digest_into (cs, d);
    mine = g_bytes_new (d, 20);
    if (!g_bytes_equal (mine, M1))
        return FALSE;

    /* M2 = H(A | M1 | K) */
    cs = g_checksum_new (G_CHECKSUM_SHA1);
    update_num (cs, A, srv->order, wide);
    g_checksum_update (cs, d, 20);
    g_checksum_update (cs, g_bytes_get_data (K, NULL), (gssize) g_bytes_get_size (K));
    digest_into (cs, d);

    *M2_out = g_bytes_new (d, 20);
    *K_out = g_steal_pointer (&K);
    return TRUE;
}

static LrgSrp6Client *
make_client (LrgSrp6Style  style,
             const gchar  *user,
             const gchar  *pass,
             gboolean      fixed_a)
{
    g_autoptr(LrgBigNum) N = NULL;
    g_autoptr(LrgBigNum) g = NULL;
    LrgSrp6Client *client;

    client = lrg_srp6_client_new (style);
    g_assert_cmpint (lrg_srp6_client_get_style (client), ==, style);
    g_assert_cmpint (lrg_srp6_client_get_hash_type (client), ==, G_CHECKSUM_SHA1);
    if (fixed_a)
    {
        g_autoptr(LrgBigNum) a = hex_num (FIXED_A_HEX);

        lrg_srp6_client_set_private_key (client, a);
    }
    N = hex_num (WOW_N_HEX);
    g = lrg_big_num_new_from_uint64 (7);
    lrg_srp6_client_set_parameters (client, N, g);
    lrg_srp6_client_set_identity (client, user, pass);
    return client;
}

/*
 * run_exchange:
 * A full exchange against the test server; returns the client's K.
 */
static GBytes *
run_exchange (LrgSrp6Style  style,
              const gchar  *user,
              const gchar  *pass,
              GBytes       *salt,
              gboolean      fixed_a)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(TestServer) srv = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GBytes) A = NULL;
    g_autoptr(GBytes) M1 = NULL;
    g_autoptr(GBytes) K = NULL;
    g_autoptr(GBytes) server_K = NULL;
    g_autoptr(GBytes) M2 = NULL;
    g_autoptr(GError) error = NULL;

    client = make_client (style, user, pass, fixed_a);
    srv = test_server_new (style, user, pass, salt);
    B = test_server_get_B (srv);

    g_assert_null (lrg_srp6_client_get_client_proof (client));
    g_assert_null (lrg_srp6_client_get_session_key (client));
    g_assert_true (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_no_error (error);

    A = lrg_srp6_client_get_public_key (client);
    g_assert_cmpuint (g_bytes_get_size (A), ==, WOW_N_LEN);
    M1 = lrg_srp6_client_get_client_proof (client);
    g_assert_cmpuint (g_bytes_get_size (M1), ==, 20);
    K = lrg_srp6_client_get_session_key (client);
    g_assert_cmpuint (g_bytes_get_size (K), ==, style == LRG_SRP6_STYLE_WOW ? 40 : 20);

    g_assert_true (test_server_verify (srv, A, M1, &server_K, &M2));
    g_assert_true (g_bytes_equal (K, server_K));
    g_assert_true (lrg_srp6_client_verify_server_proof (client, M2, &error));
    g_assert_no_error (error);

    return g_steal_pointer (&K);
}

static void
test_srp6_exchange_rfc5054 (void)
{
    g_autoptr(GBytes) salt = bytes_from_hex ("beb25379d1a8581eb5a727673a2441ee");
    g_autoptr(GBytes) k1 = NULL;
    g_autoptr(GBytes) k2 = NULL;

    k1 = run_exchange (LRG_SRP6_STYLE_RFC5054, "alice", "password123", salt, TRUE);
    /* a random a gives a different key, still agreed on */
    k2 = run_exchange (LRG_SRP6_STYLE_RFC5054, "alice", "password123", salt, FALSE);
    g_assert_false (g_bytes_equal (k1, k2));
}

static void
test_srp6_exchange_wow (void)
{
    g_autoptr(GBytes) salt = NULL;
    g_autoptr(GBytes) k1 = NULL;
    g_autoptr(GBytes) k2 = NULL;
    g_autoptr(GBytes) k3 = NULL;
    guint8 buf[32];
    guint i;

    for (i = 0; i < 32; i++)
        buf[i] = (guint8) (0xA0 + i);
    salt = g_bytes_new (buf, sizeof buf);

    k1 = run_exchange (LRG_SRP6_STYLE_WOW, "Player", "Secret1", salt, TRUE);
    /* the WOW style is case-insensitive: the server stores upper case */
    k2 = run_exchange (LRG_SRP6_STYLE_WOW, "PLAYER", "secret1", salt, TRUE);
    g_assert_true (g_bytes_equal (k1, k2));
    k3 = run_exchange (LRG_SRP6_STYLE_WOW, "Player", "Secret1", salt, FALSE);
    g_assert_false (g_bytes_equal (k1, k3));
}

/*
 * check_vector:
 * The fixed exchange of a style against values from an independent
 * Python implementation.
 */
static void
check_vector (LrgSrp6Style  style,
              const gchar  *user,
              const gchar  *pass,
              GBytes       *salt,
              const gchar  *B_hex,
              const gchar  *A_hex,
              const gchar  *K_hex,
              const gchar  *M1_hex,
              const gchar  *M2_hex)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GBytes) A = NULL;
    g_autoptr(GBytes) K = NULL;
    g_autoptr(GBytes) M1 = NULL;
    g_autoptr(GBytes) M2 = NULL;
    g_autoptr(GError) error = NULL;

    client = make_client (style, user, pass, TRUE);
    B = bytes_from_hex (B_hex);
    g_assert_true (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_no_error (error);

    A = lrg_srp6_client_get_public_key (client);
    assert_bytes_hex (A, A_hex);
    K = lrg_srp6_client_get_session_key (client);
    assert_bytes_hex (K, K_hex);
    M1 = lrg_srp6_client_get_client_proof (client);
    assert_bytes_hex (M1, M1_hex);
    M2 = bytes_from_hex (M2_hex);
    g_assert_true (lrg_srp6_client_verify_server_proof (client, M2, &error));
    g_assert_no_error (error);
}

static void
test_srp6_vector_wow (void)
{
    g_autoptr(GBytes) salt = NULL;
    guint8 buf[32];
    guint i;

    for (i = 0; i < 32; i++)
        buf[i] = (guint8) (0xA0 + i);
    salt = g_bytes_new (buf, sizeof buf);

    /* A and B are little-endian, as on the wire */
    check_vector (LRG_SRP6_STYLE_WOW, "Player", "Secret1", salt,
                  "e4f17a30a3ce79e3e7c8fcfa06024750025590427da429eb8b3f824c25ca260b",
                  "ee9e84a13f7b992ae10dc9d509e8bfded6a56b26430407ef233706aa469af028",
                  "aaa932e60bd1e58c21c039d6523f256b69854d90b1c2508d4715c2e7a4ac7845a612aab1b4e59130",
                  "427b33fe73e8a1c5ddfb949480cc30eb270fbabe",
                  "a38be580b643e1d7f0b074ec4aae6832ef9dd36b");
}

static void
test_srp6_vector_rfc5054 (void)
{
    g_autoptr(GBytes) salt = bytes_from_hex ("beb25379d1a8581eb5a727673a2441ee");

    /* the same a as the WOW vector, so A is its byte reversal */
    check_vector (LRG_SRP6_STYLE_RFC5054, "alice", "password123", salt,
                  "0eb63bb5dbceee65131ac806da6312b9d2e6f0df2855c7cbc1fe1f17d4487e99",
                  "28f09a46aa063723ef070443266ba5d6debfe809d5c90de12a997b3fa1849eee",
                  "771dac8841baa221152fdcdf124843cf5b111f81",
                  "bd08510e1cb82d16a895906eba13a240308f2018",
                  "8c4dd960dcede654698e0229c42d0063963ff22d");
}

static void
test_srp6_identity_hash (void)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(GBytes) salt = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GBytes) ih = NULL;
    g_autoptr(GBytes) K = NULL;
    g_autoptr(GError) error = NULL;
    guint8 buf[32];
    guint i;

    for (i = 0; i < 32; i++)
        buf[i] = (guint8) (0xA0 + i);
    salt = g_bytes_new (buf, sizeof buf);
    B = bytes_from_hex ("e4f17a30a3ce79e3e7c8fcfa06024750025590427da429eb8b3f824c25ca260b");

    /* SHA1("PLAYER:SECRET1"); the username is upper-cased by the client */
    client = make_client (LRG_SRP6_STYLE_WOW, "x", "y", TRUE);
    ih = bytes_from_hex ("6e56718d1ec18a524b787c98b0831224f880713d");
    lrg_srp6_client_set_identity_hash (client, "player", ih);
    g_assert_true (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_no_error (error);
    K = lrg_srp6_client_get_session_key (client);
    assert_bytes_hex (K, "aaa932e60bd1e58c21c039d6523f256b69854d90b1c2508d4715c2e7a4ac7845a612aab1b4e59130");
}

static void
test_srp6_bad_server_key (void)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(LrgBigNum) N = NULL;
    g_autoptr(GBytes) salt = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GError) error = NULL;

    client = make_client (LRG_SRP6_STYLE_WOW, "Player", "Secret1", TRUE);
    salt = bytes_from_hex ("00112233");
    N = hex_num (WOW_N_HEX);

    /* B = 0 */
    B = g_bytes_new_take (g_malloc0 (WOW_N_LEN), WOW_N_LEN);
    g_assert_false (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_KEY);
    g_assert_null (lrg_srp6_client_get_session_key (client));
    g_clear_error (&error);
    g_clear_pointer (&B, g_bytes_unref);

    /* B = N (zero modulo N) */
    B = lrg_big_num_to_bytes (N, LRG_BYTE_ORDER_LITTLE_ENDIAN, WOW_N_LEN);
    g_assert_false (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_KEY);
    g_clear_error (&error);
    g_clear_pointer (&B, g_bytes_unref);

    /* B > N */
    B = bytes_from_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    g_assert_false (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_KEY);
    g_assert_null (lrg_srp6_client_get_client_proof (client));
}

static void
test_srp6_bad_server_proof (void)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(GBytes) salt = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GBytes) M2 = NULL;
    g_autoptr(GError) error = NULL;
    guint8 buf[32];
    guint i;

    for (i = 0; i < 32; i++)
        buf[i] = (guint8) (0xA0 + i);
    salt = g_bytes_new (buf, sizeof buf);
    B = bytes_from_hex ("e4f17a30a3ce79e3e7c8fcfa06024750025590427da429eb8b3f824c25ca260b");
    client = make_client (LRG_SRP6_STYLE_WOW, "Player", "Secret1", TRUE);

    /* verifying before the exchange */
    M2 = bytes_from_hex ("a38be580b643e1d7f0b074ec4aae6832ef9dd36b");
    g_assert_false (lrg_srp6_client_verify_server_proof (client, M2, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY);
    g_clear_error (&error);
    g_clear_pointer (&M2, g_bytes_unref);

    g_assert_true (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_no_error (error);

    /* one bit off */
    M2 = bytes_from_hex ("a38be580b643e1d7f0b074ec4aae6832ef9dd36a");
    g_assert_false (lrg_srp6_client_verify_server_proof (client, M2, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_PROOF);
    g_clear_error (&error);
    g_clear_pointer (&M2, g_bytes_unref);

    /* wrong length */
    M2 = bytes_from_hex ("a38be580b643e1d7f0b074ec4aae6832ef9dd3");
    g_assert_false (lrg_srp6_client_verify_server_proof (client, M2, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_BAD_SERVER_PROOF);
    g_clear_error (&error);
    g_clear_pointer (&M2, g_bytes_unref);

    /* a wrong password gives a different M1 that the server rejects */
    {
        g_autoptr(LrgSrp6Client) wrong = NULL;
        g_autoptr(TestServer) srv = NULL;
        g_autoptr(GBytes) sB = NULL;
        g_autoptr(GBytes) A = NULL;
        g_autoptr(GBytes) M1 = NULL;
        g_autoptr(GBytes) K = NULL;
        g_autoptr(GBytes) sM2 = NULL;

        srv = test_server_new (LRG_SRP6_STYLE_WOW, "Player", "Secret1", salt);
        sB = test_server_get_B (srv);
        wrong = make_client (LRG_SRP6_STYLE_WOW, "Player", "Secret2", TRUE);
        g_assert_true (lrg_srp6_client_process_challenge (wrong, salt, sB, &error));
        g_assert_no_error (error);
        A = lrg_srp6_client_get_public_key (wrong);
        M1 = lrg_srp6_client_get_client_proof (wrong);
        g_assert_false (test_server_verify (srv, A, M1, &K, &sM2));
    }
}

static void
test_srp6_not_ready (void)
{
    g_autoptr(LrgSrp6Client) client = NULL;
    g_autoptr(LrgBigNum) N = NULL;
    g_autoptr(LrgBigNum) g = NULL;
    g_autoptr(GBytes) salt = NULL;
    g_autoptr(GBytes) B = NULL;
    g_autoptr(GError) error = NULL;

    client = lrg_srp6_client_new (LRG_SRP6_STYLE_RFC5054);
    salt = bytes_from_hex ("0011");
    B = bytes_from_hex ("05");

    g_assert_null (lrg_srp6_client_get_public_key (client));
    g_assert_false (lrg_srp6_client_generate_public_key (client, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY);
    g_clear_error (&error);

    /* parameters but no identity */
    N = hex_num (WOW_N_HEX);
    g = lrg_big_num_new_from_uint64 (7);
    lrg_srp6_client_set_parameters (client, N, g);
    g_assert_false (lrg_srp6_client_process_challenge (client, salt, B, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_NOT_READY);
    g_clear_error (&error);

    /* the client may speak first: A without a challenge */
    g_assert_true (lrg_srp6_client_generate_public_key (client, &error));
    g_assert_no_error (error);
    {
        g_autoptr(GBytes) A = lrg_srp6_client_get_public_key (client);

        g_assert_cmpuint (g_bytes_get_size (A), ==, WOW_N_LEN);
    }

    /* g >= N is rejected */
    lrg_srp6_client_set_parameters (client, N, N);
    g_assert_false (lrg_srp6_client_generate_public_key (client, &error));
    g_assert_error (error, LRG_SRP6_ERROR, LRG_SRP6_ERROR_INVALID_PARAMETERS);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/crypto/bignum/hex", test_bignum_hex);
    g_test_add_func ("/crypto/bignum/bytes", test_bignum_bytes);
    g_test_add_func ("/crypto/bignum/arith", test_bignum_arith);
    g_test_add_func ("/crypto/bignum/random", test_bignum_random);
    g_test_add_func ("/crypto/arc4/rfc6229", test_arc4_rfc6229);
    g_test_add_func ("/crypto/arc4/drop", test_arc4_drop);
    g_test_add_func ("/crypto/arc4/stream-and-copy", test_arc4_stream_and_copy);
    g_test_add_func ("/crypto/srp6/interleave", test_srp6_interleave);
    g_test_add_func ("/crypto/srp6/exchange-rfc5054", test_srp6_exchange_rfc5054);
    g_test_add_func ("/crypto/srp6/exchange-wow", test_srp6_exchange_wow);
    g_test_add_func ("/crypto/srp6/vector-wow", test_srp6_vector_wow);
    g_test_add_func ("/crypto/srp6/vector-rfc5054", test_srp6_vector_rfc5054);
    g_test_add_func ("/crypto/srp6/identity-hash", test_srp6_identity_hash);
    g_test_add_func ("/crypto/srp6/bad-server-key", test_srp6_bad_server_key);
    g_test_add_func ("/crypto/srp6/bad-server-proof", test_srp6_bad_server_proof);
    g_test_add_func ("/crypto/srp6/not-ready", test_srp6_not_ready);

    return g_test_run ();
}
