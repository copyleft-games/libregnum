/* lrg-csv.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A small RFC 4180 CSV reader.  One pass over the bytes: the state is
 * "at the start of a field", "inside an unquoted field", "inside a
 * quoted field" or "just after a quote inside a quoted field" (which
 * either closes the field or, followed by a second quote, is a literal
 * quote).  The text is validated as UTF-8 first, so every field is valid
 * UTF-8 as well (the separator and quote are ASCII and never split a
 * multibyte sequence).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_I18N

#include "lrg-csv.h"
#include "lrg-log.h"

#include <string.h>

G_DEFINE_QUARK (lrg-csv-error-quark, lrg_csv_error)

/* the row being built: its fields, then the field being built */
typedef struct
{
    GPtrArray *rows;    /* GStrv */
    GPtrArray *fields;  /* gchar *, the current row */
    GString   *field;   /* the current field */
    gboolean   row_has_content;
} CsvState;

/* ends the current field */
static void
end_field (CsvState *st)
{
    g_ptr_array_add (st->fields, g_string_free (st->field, FALSE));
    st->field = g_string_new (NULL);
}

/* ends the current row; a row of one empty field (an empty line) is dropped */
static void
end_row (CsvState *st)
{
    end_field (st);
    if (st->row_has_content || st->fields->len > 1 ||
        ((const gchar *)g_ptr_array_index (st->fields, 0))[0] != '\0')
    {
        g_ptr_array_add (st->fields, NULL);
        g_ptr_array_add (st->rows, g_ptr_array_free (st->fields, FALSE));
    }
    else
    {
        g_ptr_array_free (st->fields, TRUE);
    }
    st->fields = g_ptr_array_new_with_free_func (g_free);
    st->row_has_content = FALSE;
}

GPtrArray *
lrg_csv_parse (const gchar  *text,
               gssize        length,
               gchar         separator,
               GError      **error)
{
    enum { FIELD_START, UNQUOTED, QUOTED, QUOTE_IN_QUOTED } state;
    CsvState st;
    const gchar *p;
    const gchar *end;
    const gchar *bad;
    guint line;
    guint quote_line;

    g_return_val_if_fail (text != NULL || length == 0, NULL);
    g_return_val_if_fail (separator != '"' && separator != '\n' && separator != '\r', NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (length < 0)
        length = (gssize) strlen (text);

    /* validate first: every field is then valid UTF-8 */
    if (length > 0 && !g_utf8_validate (text, length, &bad))
    {
        g_set_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_INVALID_UTF8,
                     "CSV text is not valid UTF-8 at byte %" G_GSIZE_FORMAT,
                     (gsize) (bad - text));
        return NULL;
    }

    p = text;
    end = text + length;
    /* the byte order mark Windows editors write */
    if (length >= 3 && memcmp (p, "\xEF\xBB\xBF", 3) == 0)
        p += 3;

    st.rows = g_ptr_array_new_with_free_func ((GDestroyNotify) g_strfreev);
    st.fields = g_ptr_array_new_with_free_func (g_free);
    st.field = g_string_new (NULL);
    st.row_has_content = FALSE;
    state = FIELD_START;
    line = 1;
    quote_line = 1;

    while (p < end)
    {
        gchar c;

        c = *p;
        switch (state)
        {
        case FIELD_START:
        case UNQUOTED:
            if (c == '"' && state == FIELD_START)
            {
                state = QUOTED;
                quote_line = line;
                st.row_has_content = TRUE;
            }
            else if (c == separator)
            {
                end_field (&st);
                st.row_has_content = TRUE;
                state = FIELD_START;
            }
            else if (c == '\n' || c == '\r')
            {
                /* CRLF is one line end */
                if (c == '\r' && p + 1 < end && p[1] == '\n')
                    p++;
                end_row (&st);
                line++;
                state = FIELD_START;
            }
            else
            {
                g_string_append_c (st.field, c);
                state = UNQUOTED;
            }
            break;

        case QUOTED:
            if (c == '"')
                state = QUOTE_IN_QUOTED;
            else
            {
                if (c == '\n')
                    line++;
                g_string_append_c (st.field, c);
            }
            break;

        case QUOTE_IN_QUOTED:
            if (c == '"')
            {
                /* "" inside quotes is one literal quote */
                g_string_append_c (st.field, '"');
                state = QUOTED;
            }
            else if (c == separator)
            {
                end_field (&st);
                state = FIELD_START;
            }
            else if (c == '\n' || c == '\r')
            {
                if (c == '\r' && p + 1 < end && p[1] == '\n')
                    p++;
                end_row (&st);
                line++;
                state = FIELD_START;
            }
            else
            {
                g_set_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_MALFORMED,
                             "CSV line %u: unexpected '%c' after a closing quote",
                             line, c);
                goto fail;
            }
            break;
        }
        p++;
    }

    if (state == QUOTED)
    {
        g_set_error (error, LRG_CSV_ERROR, LRG_CSV_ERROR_UNTERMINATED_QUOTE,
                     "CSV line %u: quoted field is never closed", quote_line);
        goto fail;
    }
    /* the last row may lack a line end */
    if (state != FIELD_START || st.row_has_content || st.fields->len > 0)
        end_row (&st);

    g_string_free (st.field, TRUE);
    g_ptr_array_free (st.fields, TRUE);
    return st.rows;

fail:
    g_string_free (st.field, TRUE);
    g_ptr_array_free (st.fields, TRUE);
    g_ptr_array_free (st.rows, TRUE);
    return NULL;
}
