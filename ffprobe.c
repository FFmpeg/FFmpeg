/*
 * ffprobe : Simple Media Prober based on the FFmpeg libraries
 * Copyright (c) 2007-2010 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavdevice/avdevice.h"
#include "cmdutils.h"

const char program_name[] = "ffprobe";
const int program_birth_year = 2007;

static int do_show_format  = 0;
static int do_show_packets = 0;
static int do_show_streams = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;

static char *print_format;

static const OptionDef options[];

/* FFprobe context */
static const char *input_filename;
static AVInputFormat *iformat = NULL;

static const char *binary_unit_prefixes [] = { "", "Ki", "Mi", "Gi", "Ti", "Pi" };
static const char *decimal_unit_prefixes[] = { "", "K" , "M" , "G" , "T" , "P"  };

static const char *unit_second_str          = "s"    ;
static const char *unit_hertz_str           = "Hz"   ;
static const char *unit_byte_str            = "byte" ;
static const char *unit_bit_per_second_str  = "bit/s";

void av_noreturn exit_program(int ret)
{
    exit(ret);
}

struct unit_value {
    union { double d; int i; } val;
    const char *unit;
};

static char *value_string(char *buf, int buf_size, struct unit_value uv)
{
    double vald;
    int show_float = 0;

    if (uv.unit == unit_second_str) {
        vald = uv.val.d;
        show_float = 1;
    } else {
        vald = uv.val.i;
    }

    if (uv.unit == unit_second_str && use_value_sexagesimal_format) {
        double secs;
        int hours, mins;
        secs  = vald;
        mins  = (int)secs / 60;
        secs  = secs - mins * 60;
        hours = mins / 60;
        mins %= 60;
        snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
    } else if (use_value_prefix) {
        const char *prefix_string;
        int index, l;

        if (uv.unit == unit_byte_str && use_byte_value_binary_prefix) {
            index = (int) (log(vald)/log(2)) / 10;
            index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) -1);
            vald /= pow(2, index*10);
            prefix_string = binary_unit_prefixes[index];
        } else {
            index = (int) (log10(vald)) / 3;
            index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) -1);
            vald /= pow(10, index*3);
            prefix_string = decimal_unit_prefixes[index];
        }

        if (show_float || vald != (int)vald) l = snprintf(buf, buf_size, "%.3f", vald);
        else                                 l = snprintf(buf, buf_size, "%d",   (int)vald);
        snprintf(buf+l, buf_size-l, "%s%s%s", prefix_string || show_value_unit ? " " : "",
                 prefix_string, show_value_unit ? uv.unit : "");
    } else {
        int l;

        if (show_float) l = snprintf(buf, buf_size, "%.3f", vald);
        else            l = snprintf(buf, buf_size, "%d",   (int)vald);
        snprintf(buf+l, buf_size-l, "%s%s", show_value_unit ? " " : "",
                 show_value_unit ? uv.unit : "");
    }

    return buf;
}

/* WRITERS API */

typedef struct WriterContext WriterContext;

#define WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS 1

typedef struct Writer {
    int priv_size;                  ///< private size for the writer context
    const char *name;

    int  (*init)  (WriterContext *wctx, const char *args, void *opaque);
    void (*uninit)(WriterContext *wctx);

    void (*print_header)(WriterContext *ctx);
    void (*print_footer)(WriterContext *ctx);

    void (*print_chapter_header)(WriterContext *wctx, const char *);
    void (*print_chapter_footer)(WriterContext *wctx, const char *);
    void (*print_section_header)(WriterContext *wctx, const char *);
    void (*print_section_footer)(WriterContext *wctx, const char *);
    void (*print_integer)       (WriterContext *wctx, const char *, long long int);
    void (*print_string)        (WriterContext *wctx, const char *, const char *);
    void (*show_tags)           (WriterContext *wctx, AVDictionary *dict);
    int flags;                  ///< a combination or WRITER_FLAG_*
} Writer;

struct WriterContext {
    const AVClass *class;           ///< class of the writer
    const Writer *writer;           ///< the Writer of which this is an instance
    char *name;                     ///< name of this writer instance
    void *priv;                     ///< private data for use by the filter
    unsigned int nb_item;           ///< number of the item printed in the given section, starting at 0
    unsigned int nb_section;        ///< number of the section printed in the given section sequence, starting at 0
    unsigned int nb_chapter;        ///< number of the chapter, starting at 0
};

static const char *writer_get_name(void *p)
{
    WriterContext *wctx = p;
    return wctx->writer->name;
}

static const AVClass writer_class = {
    "Writer",
    writer_get_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

static void writer_close(WriterContext **wctx)
{
    if (*wctx && (*wctx)->writer->uninit)
        (*wctx)->writer->uninit(*wctx);

    av_freep(&((*wctx)->priv));
    av_freep(wctx);
}

static int writer_open(WriterContext **wctx, const Writer *writer,
                       const char *args, void *opaque)
{
    int ret = 0;

    if (!(*wctx = av_malloc(sizeof(WriterContext)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!((*wctx)->priv = av_mallocz(writer->priv_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    (*wctx)->class = &writer_class;
    (*wctx)->writer = writer;
    if ((*wctx)->writer->init)
        ret = (*wctx)->writer->init(*wctx, args, opaque);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    writer_close(wctx);
    return ret;
}

static inline void writer_print_header(WriterContext *wctx)
{
    if (wctx->writer->print_header)
        wctx->writer->print_header(wctx);
    wctx->nb_chapter = 0;
}

static inline void writer_print_footer(WriterContext *wctx)
{
    if (wctx->writer->print_footer)
        wctx->writer->print_footer(wctx);
}

static inline void writer_print_chapter_header(WriterContext *wctx,
                                               const char *header)
{
    if (wctx->writer->print_chapter_header)
        wctx->writer->print_chapter_header(wctx, header);
    wctx->nb_section = 0;
}

static inline void writer_print_chapter_footer(WriterContext *wctx,
                                               const char *footer)
{
    if (wctx->writer->print_chapter_footer)
        wctx->writer->print_chapter_footer(wctx, footer);
    wctx->nb_chapter++;
}

static inline void writer_print_section_header(WriterContext *wctx,
                                               const char *header)
{
    if (wctx->writer->print_section_header)
        wctx->writer->print_section_header(wctx, header);
    wctx->nb_item = 0;
}

static inline void writer_print_section_footer(WriterContext *wctx,
                                               const char *footer)
{
    if (wctx->writer->print_section_footer)
        wctx->writer->print_section_footer(wctx, footer);
    wctx->nb_section++;
}

static inline void writer_print_integer(WriterContext *wctx,
                                        const char *key, long long int val)
{
    wctx->writer->print_integer(wctx, key, val);
    wctx->nb_item++;
}

static inline void writer_print_string(WriterContext *wctx,
                                       const char *key, const char *val, int opt)
{
    if (opt && !(wctx->writer->flags & WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS))
        return;
    wctx->writer->print_string(wctx, key, val);
    wctx->nb_item++;
}

static void writer_print_time(WriterContext *wctx, const char *key,
                              int64_t ts, const AVRational *time_base)
{
    char buf[128];

    if (ts == AV_NOPTS_VALUE) {
        writer_print_string(wctx, key, "N/A", 1);
    } else {
        double d = ts * av_q2d(*time_base);
        value_string(buf, sizeof(buf), (struct unit_value){.val.d=d, .unit=unit_second_str});
        writer_print_string(wctx, key, buf, 0);
    }
}

static void writer_print_ts(WriterContext *wctx, const char *key, int64_t ts)
{
    if (ts == AV_NOPTS_VALUE) {
        writer_print_string(wctx, key, "N/A", 1);
    } else {
        writer_print_integer(wctx, key, ts);
    }
}

static inline void writer_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    wctx->writer->show_tags(wctx, dict);
}

#define MAX_REGISTERED_WRITERS_NB 64

static const Writer *registered_writers[MAX_REGISTERED_WRITERS_NB + 1];

static int writer_register(const Writer *writer)
{
    static int next_registered_writer_idx = 0;

    if (next_registered_writer_idx == MAX_REGISTERED_WRITERS_NB)
        return AVERROR(ENOMEM);

    registered_writers[next_registered_writer_idx++] = writer;
    return 0;
}

static const Writer *writer_get_by_name(const char *name)
{
    int i;

    for (i = 0; registered_writers[i]; i++)
        if (!strcmp(registered_writers[i]->name, name))
            return registered_writers[i];

    return NULL;
}

/* Print helpers */

struct print_buf {
    char *s;
    int len;
};

static char *fast_asprintf(struct print_buf *pbuf, const char *fmt, ...)
{
    va_list va;
    int len;

    va_start(va, fmt);
    len = vsnprintf(NULL, 0, fmt, va);
    va_end(va);
    if (len < 0)
        goto fail;

    if (pbuf->len < len) {
        char *p = av_realloc(pbuf->s, len + 1);
        if (!p)
            goto fail;
        pbuf->s   = p;
        pbuf->len = len;
    }

    va_start(va, fmt);
    len = vsnprintf(pbuf->s, len + 1, fmt, va);
    va_end(va);
    if (len < 0)
        goto fail;
    return pbuf->s;

fail:
    av_freep(&pbuf->s);
    pbuf->len = 0;
    return NULL;
}

#define ESCAPE_INIT_BUF_SIZE 256

#define ESCAPE_CHECK_SIZE(src, size, max_size)                          \
    if (size > max_size) {                                              \
        char buf[64];                                                   \
        snprintf(buf, sizeof(buf), "%s", src);                          \
        av_log(log_ctx, AV_LOG_WARNING,                                 \
               "String '%s...' with is too big\n", buf);                \
        return "FFPROBE_TOO_BIG_STRING";                                \
    }

#define ESCAPE_REALLOC_BUF(dst_size_p, dst_p, src, size)                \
    if (*dst_size_p < size) {                                           \
        char *q = av_realloc(*dst_p, size);                             \
        if (!q) {                                                       \
            char buf[64];                                               \
            snprintf(buf, sizeof(buf), "%s", src);                      \
            av_log(log_ctx, AV_LOG_WARNING,                             \
                   "String '%s...' could not be escaped\n", buf);       \
            return "FFPROBE_THIS_STRING_COULD_NOT_BE_ESCAPED";          \
        }                                                               \
        *dst_size_p = size;                                             \
        *dst = q;                                                       \
    }

/* WRITERS */

/* Default output */

static void default_print_footer(WriterContext *wctx)
{
    printf("\n");
}

static void default_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    if (wctx->nb_chapter)
        printf("\n");
}

/* lame uppercasing routine, assumes the string is lower case ASCII */
static inline char *upcase_string(char *dst, size_t dst_size, const char *src)
{
    int i;
    for (i = 0; src[i] && i < dst_size-1; i++)
        dst[i] = src[i]-32;
    dst[i] = 0;
    return dst;
}

static void default_print_section_header(WriterContext *wctx, const char *section)
{
    char buf[32];

    if (wctx->nb_section)
        printf("\n");
    printf("[%s]\n", upcase_string(buf, sizeof(buf), section));
}

static void default_print_section_footer(WriterContext *wctx, const char *section)
{
    char buf[32];

    printf("[/%s]", upcase_string(buf, sizeof(buf), section));
}

static void default_print_str(WriterContext *wctx, const char *key, const char *value)
{
    printf("%s=%s\n", key, value);
}

static void default_print_int(WriterContext *wctx, const char *key, long long int value)
{
    printf("%s=%lld\n", key, value);
}

static void default_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        printf("TAG:");
        writer_print_string(wctx, tag->key, tag->value, 0);
    }
}

static const Writer default_writer = {
    .name                  = "default",
    .print_footer          = default_print_footer,
    .print_chapter_header  = default_print_chapter_header,
    .print_section_header  = default_print_section_header,
    .print_section_footer  = default_print_section_footer,
    .print_integer         = default_print_int,
    .print_string          = default_print_str,
    .show_tags             = default_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
};

/* Compact output */

/**
 * Escape \n, \r, \\ and sep characters contained in s, and print the
 * resulting string.
 */
static const char *c_escape_str(char **dst, size_t *dst_size,
                                const char *src, const char sep, void *log_ctx)
{
    const char *p;
    char *q;
    size_t size = 1;

    /* precompute size */
    for (p = src; *p; p++, size++) {
        ESCAPE_CHECK_SIZE(src, size, SIZE_MAX-2);
        if (*p == '\n' || *p == '\r' || *p == '\\')
            size++;
    }

    ESCAPE_REALLOC_BUF(dst_size, dst, src, size);

    q = *dst;
    for (p = src; *p; p++) {
        switch (*src) {
        case '\n': *q++ = '\\'; *q++ = 'n';  break;
        case '\r': *q++ = '\\'; *q++ = 'r';  break;
        case '\\': *q++ = '\\'; *q++ = '\\'; break;
        default:
            if (*p == sep)
                *q++ = '\\';
            *q++ = *p;
        }
    }
    *q = 0;
    return *dst;
}

/**
 * Quote fields containing special characters, check RFC4180.
 */
static const char *csv_escape_str(char **dst, size_t *dst_size,
                                  const char *src, const char sep, void *log_ctx)
{
    const char *p;
    char *q;
    size_t size = 1;
    int quote = 0;

    /* precompute size */
    for (p = src; *p; p++, size++) {
        ESCAPE_CHECK_SIZE(src, size, SIZE_MAX-4);
        if (*p == '"' || *p == sep || *p == '\n' || *p == '\r')
            if (!quote) {
                quote = 1;
                size += 2;
            }
        if (*p == '"')
            size++;
    }

    ESCAPE_REALLOC_BUF(dst_size, dst, src, size);

    q = *dst;
    p = src;
    if (quote)
        *q++ = '\"';
    while (*p) {
        if (*p == '"')
            *q++ = '\"';
        *q++ = *p++;
    }
    if (quote)
        *q++ = '\"';
    *q = 0;

    return *dst;
}

static const char *none_escape_str(char **dst, size_t *dst_size,
                                   const char *src, const char sep, void *log_ctx)
{
    return src;
}

typedef struct CompactContext {
    const AVClass *class;
    char *item_sep_str;
    char item_sep;
    int nokey;
    char  *buf;
    size_t buf_size;
    char *escape_mode_str;
    const char * (*escape_str)(char **dst, size_t *dst_size,
                               const char *src, const char sep, void *log_ctx);
} CompactContext;

#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption compact_options[]= {
    {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
    {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
    {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.dbl=0},    0,        1        },
    {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.dbl=0},    0,        1        },
    {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
    {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
    {NULL},
};

static const char *compact_get_name(void *ctx)
{
    return "compact";
}

static const AVClass compact_class = {
    "CompactContext",
    compact_get_name,
    compact_options
};

static av_cold int compact_init(WriterContext *wctx, const char *args, void *opaque)
{
    CompactContext *compact = wctx->priv;
    int err;

    compact->class = &compact_class;
    av_opt_set_defaults(compact);

    if (args &&
        (err = (av_set_options_string(compact, args, "=", ":"))) < 0) {
        av_log(wctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return err;
    }
    if (strlen(compact->item_sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               compact->item_sep_str);
        return AVERROR(EINVAL);
    }
    compact->item_sep = compact->item_sep_str[0];

    compact->buf_size = ESCAPE_INIT_BUF_SIZE;
    if (!(compact->buf = av_malloc(compact->buf_size)))
        return AVERROR(ENOMEM);

    if      (!strcmp(compact->escape_mode_str, "none")) compact->escape_str = none_escape_str;
    else if (!strcmp(compact->escape_mode_str, "c"   )) compact->escape_str = c_escape_str;
    else if (!strcmp(compact->escape_mode_str, "csv" )) compact->escape_str = csv_escape_str;
    else {
        av_log(wctx, AV_LOG_ERROR, "Unknown escape mode '%s'\n", compact->escape_mode_str);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void compact_uninit(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;

    av_freep(&compact->item_sep_str);
    av_freep(&compact->buf);
    av_freep(&compact->escape_mode_str);
}

static void compact_print_section_header(WriterContext *wctx, const char *section)
{
    CompactContext *compact = wctx->priv;

    printf("%s%c", section, compact->item_sep);
}

static void compact_print_section_footer(WriterContext *wctx, const char *section)
{
    printf("\n");
}

static void compact_print_str(WriterContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;

    if (wctx->nb_item) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s=", key);
    printf("%s", compact->escape_str(&compact->buf, &compact->buf_size,
                                     value, compact->item_sep, wctx));
}

static void compact_print_int(WriterContext *wctx, const char *key, long long int value)
{
    CompactContext *compact = wctx->priv;

    if (wctx->nb_item) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s=", key);
    printf("%lld", value);
}

static void compact_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    CompactContext *compact = wctx->priv;
    AVDictionaryEntry *tag = NULL;

    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (wctx->nb_item) printf("%c", compact->item_sep);
        if (!compact->nokey)
            printf("tag:%s=", compact->escape_str(&compact->buf, &compact->buf_size,
                                                  tag->key, compact->item_sep, wctx));
        printf("%s", compact->escape_str(&compact->buf, &compact->buf_size,
                                         tag->value, compact->item_sep, wctx));
    }
}

static const Writer compact_writer = {
    .name                 = "compact",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .uninit               = compact_uninit,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .show_tags            = compact_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
};

/* CSV output */

static av_cold int csv_init(WriterContext *wctx, const char *args, void *opaque)
{
    return compact_init(wctx, "item_sep=,:nokey=1:escape=csv", opaque);
}

static const Writer csv_writer = {
    .name                 = "csv",
    .priv_size            = sizeof(CompactContext),
    .init                 = csv_init,
    .uninit               = compact_uninit,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .show_tags            = compact_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
};

/* JSON output */

typedef struct {
    int multiple_entries; ///< tells if the given chapter requires multiple entries
    char *buf;
    size_t buf_size;
} JSONContext;

static av_cold int json_init(WriterContext *wctx, const char *args, void *opaque)
{
    JSONContext *json = wctx->priv;

    json->buf_size = ESCAPE_INIT_BUF_SIZE;
    if (!(json->buf = av_malloc(json->buf_size)))
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void json_uninit(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    av_freep(&json->buf);
}

static const char *json_escape_str(char **dst, size_t *dst_size, const char *src,
                                   void *log_ctx)
{
    static const char json_escape[] = {'"', '\\', '\b', '\f', '\n', '\r', '\t', 0};
    static const char json_subst[]  = {'"', '\\',  'b',  'f',  'n',  'r',  't', 0};
    const char *p;
    char *q;
    size_t size = 1;

    // compute the length of the escaped string
    for (p = src; *p; p++) {
        ESCAPE_CHECK_SIZE(src, size, SIZE_MAX-6);
        if (strchr(json_escape, *p))     size += 2; // simple escape
        else if ((unsigned char)*p < 32) size += 6; // handle non-printable chars
        else                             size += 1; // char copy
    }
    ESCAPE_REALLOC_BUF(dst_size, dst, src, size);

    q = *dst;
    for (p = src; *p; p++) {
        char *s = strchr(json_escape, *p);
        if (s) {
            *q++ = '\\';
            *q++ = json_subst[s - json_escape];
        } else if ((unsigned char)*p < 32) {
            snprintf(q, 7, "\\u00%02x", *p & 0xff);
            q += 6;
        } else {
            *q++ = *p;
        }
    }
    *q = 0;
    return *dst;
}

static void json_print_header(WriterContext *wctx)
{
    printf("{");
}

static void json_print_footer(WriterContext *wctx)
{
    printf("\n}\n");
}

static void json_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    JSONContext *json = wctx->priv;

    if (wctx->nb_chapter)
        printf(",");
    json->multiple_entries = !strcmp(chapter, "packets") || !strcmp(chapter, "streams");
    printf("\n  \"%s\":%s", json_escape_str(&json->buf, &json->buf_size, chapter, wctx),
           json->multiple_entries ? " [" : " ");
}

static void json_print_chapter_footer(WriterContext *wctx, const char *chapter)
{
    JSONContext *json = wctx->priv;

    if (json->multiple_entries)
        printf("]");
}

static void json_print_section_header(WriterContext *wctx, const char *section)
{
    if (wctx->nb_section) printf(",");
    printf("{\n");
}

static void json_print_section_footer(WriterContext *wctx, const char *section)
{
    printf("\n  }");
}

static inline void json_print_item_str(WriterContext *wctx,
                                       const char *key, const char *value,
                                       const char *indent)
{
    JSONContext *json = wctx->priv;

    printf("%s\"%s\":", indent, json_escape_str(&json->buf, &json->buf_size, key,   wctx));
    printf(" \"%s\"",           json_escape_str(&json->buf, &json->buf_size, value, wctx));
}

#define INDENT "    "

static void json_print_str(WriterContext *wctx, const char *key, const char *value)
{
    if (wctx->nb_item) printf(",\n");
    json_print_item_str(wctx, key, value, INDENT);
}

static void json_print_int(WriterContext *wctx, const char *key, long long int value)
{
    JSONContext *json = wctx->priv;

    if (wctx->nb_item) printf(",\n");
    printf(INDENT "\"%s\": %lld",
           json_escape_str(&json->buf, &json->buf_size, key, wctx), value);
}

static void json_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    AVDictionaryEntry *tag = NULL;
    int is_first = 1;
    if (!dict)
        return;
    printf(",\n" INDENT "\"tags\": {\n");
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (is_first) is_first = 0;
        else          printf(",\n");
        json_print_item_str(wctx, tag->key, tag->value, INDENT INDENT);
    }
    printf("\n    }");
}

static const Writer json_writer = {
    .name                 = "json",
    .priv_size            = sizeof(JSONContext),
    .init                 = json_init,
    .uninit               = json_uninit,
    .print_header         = json_print_header,
    .print_footer         = json_print_footer,
    .print_chapter_header = json_print_chapter_header,
    .print_chapter_footer = json_print_chapter_footer,
    .print_section_header = json_print_section_header,
    .print_section_footer = json_print_section_footer,
    .print_integer        = json_print_int,
    .print_string         = json_print_str,
    .show_tags            = json_show_tags,
};

static void writer_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    writer_register(&default_writer);
    writer_register(&compact_writer);
    writer_register(&csv_writer);
    writer_register(&json_writer);
}

#define print_fmt(k, f, ...) do {              \
    if (fast_asprintf(&pbuf, f, __VA_ARGS__))  \
        writer_print_string(w, k, pbuf.s, 0);  \
} while (0)

#define print_fmt_opt(k, f, ...) do {          \
    if (fast_asprintf(&pbuf, f, __VA_ARGS__))  \
        writer_print_string(w, k, pbuf.s, 1);  \
} while (0)

#define print_int(k, v)         writer_print_integer(w, k, v)
#define print_str(k, v)         writer_print_string(w, k, v, 0)
#define print_str_opt(k, v)     writer_print_string(w, k, v, 1)
#define print_time(k, v, tb)    writer_print_time(w, k, v, tb)
#define print_ts(k, v)          writer_print_ts(w, k, v)
#define print_val(k, v, u)      writer_print_string(w, k, \
    value_string(val_str, sizeof(val_str), (struct unit_value){.val.i = v, .unit=u}), 1)
#define print_section_header(s) writer_print_section_header(w, s)
#define print_section_footer(s) writer_print_section_footer(w, s)
#define show_tags(metadata)     writer_show_tags(w, metadata)

static void show_packet(WriterContext *w, AVFormatContext *fmt_ctx, AVPacket *pkt, int packet_idx)
{
    char val_str[128];
    AVStream *st = fmt_ctx->streams[pkt->stream_index];
    struct print_buf pbuf = {.s = NULL};
    const char *s;

    print_section_header("packet");
    s = av_get_media_type_string(st->codec->codec_type);
    if (s) print_str    ("codec_type", s);
    else   print_str_opt("codec_type", "unknown");
    print_int("stream_index",     pkt->stream_index);
    print_ts  ("pts",             pkt->pts);
    print_time("pts_time",        pkt->pts, &st->time_base);
    print_ts  ("dts",             pkt->dts);
    print_time("dts_time",        pkt->dts, &st->time_base);
    print_ts  ("duration",        pkt->duration);
    print_time("duration_time",   pkt->duration, &st->time_base);
    print_val("size",             pkt->size, unit_byte_str);
    if (pkt->pos != -1) print_fmt    ("pos", "%"PRId64, pkt->pos);
    else                print_str_opt("pos", "N/A");
    print_fmt("flags", "%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_');
    print_section_footer("packet");

    av_free(pbuf.s);
    fflush(stdout);
}

static void show_packets(WriterContext *w, AVFormatContext *fmt_ctx)
{
    AVPacket pkt;
    int i = 0;

    av_init_packet(&pkt);

    while (!av_read_frame(fmt_ctx, &pkt))
        show_packet(w, fmt_ctx, &pkt, i++);
}

static void show_stream(WriterContext *w, AVFormatContext *fmt_ctx, int stream_idx)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    AVCodec *dec;
    char val_str[128];
    const char *s;
    AVRational display_aspect_ratio;
    struct print_buf pbuf = {.s = NULL};

    print_section_header("stream");

    print_int("index", stream->index);

    if ((dec_ctx = stream->codec)) {
        if ((dec = dec_ctx->codec)) {
            print_str("codec_name",      dec->name);
            print_str("codec_long_name", dec->long_name);
        } else {
            print_str_opt("codec_name",      "unknown");
            print_str_opt("codec_long_name", "unknown");
        }

        s = av_get_media_type_string(dec_ctx->codec_type);
        if (s) print_str    ("codec_type", s);
        else   print_str_opt("codec_type", "unknown");
        print_fmt("codec_time_base", "%d/%d", dec_ctx->time_base.num, dec_ctx->time_base.den);

        /* print AVI/FourCC tag */
        av_get_codec_tag_string(val_str, sizeof(val_str), dec_ctx->codec_tag);
        print_str("codec_tag_string",    val_str);
        print_fmt("codec_tag", "0x%04x", dec_ctx->codec_tag);

        switch (dec_ctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            print_int("width",        dec_ctx->width);
            print_int("height",       dec_ctx->height);
            print_int("has_b_frames", dec_ctx->has_b_frames);
            if (dec_ctx->sample_aspect_ratio.num) {
                print_fmt("sample_aspect_ratio", "%d:%d",
                          dec_ctx->sample_aspect_ratio.num,
                          dec_ctx->sample_aspect_ratio.den);
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          dec_ctx->width  * dec_ctx->sample_aspect_ratio.num,
                          dec_ctx->height * dec_ctx->sample_aspect_ratio.den,
                          1024*1024);
                print_fmt("display_aspect_ratio", "%d:%d",
                          display_aspect_ratio.num,
                          display_aspect_ratio.den);
            } else {
                print_str_opt("sample_aspect_ratio", "N/A");
                print_str_opt("display_aspect_ratio", "N/A");
            }
            s = av_get_pix_fmt_name(dec_ctx->pix_fmt);
            if (s) print_str    ("pix_fmt", s);
            else   print_str_opt("pix_fmt", "unknown");
            print_int("level",   dec_ctx->level);
            break;

        case AVMEDIA_TYPE_AUDIO:
            s = av_get_sample_fmt_name(dec_ctx->sample_fmt);
            if (s) print_str    ("sample_fmt", s);
            else   print_str_opt("sample_fmt", "unknown");
            print_val("sample_rate",     dec_ctx->sample_rate, unit_hertz_str);
            print_int("channels",        dec_ctx->channels);
            print_int("bits_per_sample", av_get_bits_per_sample(dec_ctx->codec_id));
            break;
        }
    } else {
        print_str_opt("codec_type", "unknown");
    }
    if (dec_ctx->codec && dec_ctx->codec->priv_class) {
        const AVOption *opt = NULL;
        while (opt = av_opt_next(dec_ctx->priv_data,opt)) {
            uint8_t *str;
            if (opt->flags) continue;
            if (av_opt_get(dec_ctx->priv_data, opt->name, 0, &str) >= 0) {
                print_str(opt->name, str);
                av_free(str);
            }
        }
    }

    if (fmt_ctx->iformat->flags & AVFMT_SHOW_IDS) print_fmt    ("id", "0x%x", stream->id);
    else                                          print_str_opt("id", "N/A");
    print_fmt("r_frame_rate",   "%d/%d", stream->r_frame_rate.num,   stream->r_frame_rate.den);
    print_fmt("avg_frame_rate", "%d/%d", stream->avg_frame_rate.num, stream->avg_frame_rate.den);
    print_fmt("time_base",      "%d/%d", stream->time_base.num,      stream->time_base.den);
    print_time("start_time",    stream->start_time, &stream->time_base);
    print_time("duration",      stream->duration,   &stream->time_base);
    if (stream->nb_frames) print_fmt    ("nb_frames", "%"PRId64, stream->nb_frames);
    else                   print_str_opt("nb_frames", "N/A");
    show_tags(stream->metadata);

    print_section_footer("stream");
    av_free(pbuf.s);
    fflush(stdout);
}

static void show_streams(WriterContext *w, AVFormatContext *fmt_ctx)
{
    int i;
    for (i = 0; i < fmt_ctx->nb_streams; i++)
        show_stream(w, fmt_ctx, i);
}

static void show_format(WriterContext *w, AVFormatContext *fmt_ctx)
{
    char val_str[128];
    int64_t size = avio_size(fmt_ctx->pb);
    struct print_buf pbuf = {.s = NULL};

    print_section_header("format");
    print_str("filename",         fmt_ctx->filename);
    print_int("nb_streams",       fmt_ctx->nb_streams);
    print_str("format_name",      fmt_ctx->iformat->name);
    print_str("format_long_name", fmt_ctx->iformat->long_name);
    print_time("start_time",      fmt_ctx->start_time, &AV_TIME_BASE_Q);
    print_time("duration",        fmt_ctx->duration,   &AV_TIME_BASE_Q);
    if (size >= 0) print_val    ("size", size, unit_byte_str);
    else           print_str_opt("size", "N/A");
    if (fmt_ctx->bit_rate > 0) print_val    ("bit_rate", fmt_ctx->bit_rate, unit_bit_per_second_str);
    else                       print_str_opt("bit_rate", "N/A");
    show_tags(fmt_ctx->metadata);
    print_section_footer("format");
    av_free(pbuf.s);
    fflush(stdout);
}

static int open_input_file(AVFormatContext **fmt_ctx_ptr, const char *filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;

    if ((err = avformat_open_input(&fmt_ctx, filename, iformat, &format_opts)) < 0) {
        print_error(filename, err);
        return err;
    }
    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        return AVERROR_OPTION_NOT_FOUND;
    }


    /* fill the streams in the format context */
    if ((err = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        print_error(filename, err);
        return err;
    }

    av_dump_format(fmt_ctx, 0, filename, 0);

    /* bind a decoder to each input stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *stream = fmt_ctx->streams[i];
        AVCodec *codec;

        if (!(codec = avcodec_find_decoder(stream->codec->codec_id))) {
            fprintf(stderr, "Unsupported codec with id %d for input stream %d\n",
                    stream->codec->codec_id, stream->index);
        } else if (avcodec_open2(stream->codec, codec, NULL) < 0) {
            fprintf(stderr, "Error while opening codec for input stream %d\n",
                    stream->index);
        }
    }

    *fmt_ctx_ptr = fmt_ctx;
    return 0;
}

#define PRINT_CHAPTER(name) do {                                        \
    if (do_show_ ## name) {                                             \
        writer_print_chapter_header(wctx, #name);                       \
        show_ ## name (wctx, fmt_ctx);                                  \
        writer_print_chapter_footer(wctx, #name);                       \
    }                                                                   \
} while (0)

static int probe_file(const char *filename)
{
    AVFormatContext *fmt_ctx;
    int ret;
    const Writer *w;
    char *buf;
    char *w_name = NULL, *w_args = NULL;
    WriterContext *wctx;

    writer_register_all();

    if (!print_format)
        print_format = av_strdup("default");
    w_name = av_strtok(print_format, "=", &buf);
    w_args = buf;

    w = writer_get_by_name(w_name);
    if (!w) {
        av_log(NULL, AV_LOG_ERROR, "Unknown output format with name '%s'\n", w_name);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if ((ret = writer_open(&wctx, w, w_args, NULL)) < 0)
        goto end;
    if ((ret = open_input_file(&fmt_ctx, filename)))
        goto end;

    writer_print_header(wctx);
    PRINT_CHAPTER(packets);
    PRINT_CHAPTER(streams);
    PRINT_CHAPTER(format);
    writer_print_footer(wctx);

    av_close_input_file(fmt_ctx);
    writer_close(&wctx);

end:
    av_freep(&print_format);

    return ret;
}

static void show_usage(void)
{
    printf("Simple multimedia streams analyzer\n");
    printf("usage: %s [OPTIONS] [INPUT_FILE]\n", program_name);
    printf("\n");
}

static int opt_format(const char *opt, const char *arg)
{
    iformat = av_find_input_format(arg);
    if (!iformat) {
        fprintf(stderr, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static void opt_input_file(void *optctx, const char *arg)
{
    if (input_filename) {
        fprintf(stderr, "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        exit(1);
    }
    if (!strcmp(arg, "-"))
        arg = "pipe:";
    input_filename = arg;
}

static int opt_help(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:\n", 0, 0);
    printf("\n");

    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);

    return 0;
}

static int opt_pretty(const char *opt, const char *arg)
{
    show_value_unit              = 1;
    use_value_prefix             = 1;
    use_byte_value_binary_prefix = 1;
    use_value_sexagesimal_format = 1;
    return 0;
}

static const OptionDef options[] = {
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "format" },
    { "unit", OPT_BOOL, {(void*)&show_value_unit}, "show unit of the displayed values" },
    { "prefix", OPT_BOOL, {(void*)&use_value_prefix}, "use SI prefixes for the displayed values" },
    { "byte_binary_prefix", OPT_BOOL, {(void*)&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal", OPT_BOOL,  {(void*)&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty", 0, {(void*)&opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "print_format", OPT_STRING | HAS_ARG, {(void*)&print_format},
      "set the output printing format (available formats are: default, compact, csv, json)", "format" },
    { "show_format",  OPT_BOOL, {(void*)&do_show_format} , "show format/container info" },
    { "show_packets", OPT_BOOL, {(void*)&do_show_packets}, "show packets info" },
    { "show_streams", OPT_BOOL, {(void*)&do_show_streams}, "show streams info" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { "i", HAS_ARG, {(void *)opt_input_file}, "read specified file", "input_file"},
    { NULL, },
};

int main(int argc, char **argv)
{
    int ret;

    parse_loglevel(argc, argv, options);
    av_register_all();
    avformat_network_init();
    init_opts();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner();
    parse_options(NULL, argc, argv, options, opt_input_file);

    if (!input_filename) {
        show_usage();
        fprintf(stderr, "You have to specify one input file.\n");
        fprintf(stderr, "Use -h to get full help or, even better, run 'man %s'.\n", program_name);
        exit(1);
    }

    ret = probe_file(input_filename);

    avformat_network_deinit();

    return ret;
}
