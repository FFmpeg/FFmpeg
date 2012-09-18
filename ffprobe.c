/*
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

/**
 * @file
 * simple media prober based on the FFmpeg libraries
 */

#include "config.h"
#include "version.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavutil/libm.h"
#include "libavutil/timecode.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libpostproc/postprocess.h"
#include "cmdutils.h"

const char program_name[] = "ffprobe";
const int program_birth_year = 2007;

static int do_count_frames = 0;
static int do_count_packets = 0;
static int do_read_frames  = 0;
static int do_read_packets = 0;
static int do_show_error   = 0;
static int do_show_format  = 0;
static int do_show_frames  = 0;
static AVDictionary *fmt_entries_to_show = NULL;
static int do_show_packets = 0;
static int do_show_streams = 0;
static int do_show_data    = 0;
static int do_show_program_version  = 0;
static int do_show_library_versions = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data            = 1;

static char *print_format;

static const OptionDef *options;

/* FFprobe context */
static const char *input_filename;
static AVInputFormat *iformat = NULL;

static const char *const binary_unit_prefixes [] = { "", "Ki", "Mi", "Gi", "Ti", "Pi" };
static const char *const decimal_unit_prefixes[] = { "", "K" , "M" , "G" , "T" , "P"  };

static const char unit_second_str[]         = "s"    ;
static const char unit_hertz_str[]          = "Hz"   ;
static const char unit_byte_str[]           = "byte" ;
static const char unit_bit_per_second_str[] = "bit/s";
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;

void av_noreturn exit_program(int ret)
{
    av_dict_free(&fmt_entries_to_show);
    exit(ret);
}

struct unit_value {
    union { double d; long long int i; } val;
    const char *unit;
};

static char *value_string(char *buf, int buf_size, struct unit_value uv)
{
    double vald;
    long long int vali;
    int show_float = 0;

    if (uv.unit == unit_second_str) {
        vald = uv.val.d;
        show_float = 1;
    } else {
        vald = vali = uv.val.i;
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
    } else {
        const char *prefix_string = "";

        if (use_value_prefix && vald > 1) {
            long long int index;

            if (uv.unit == unit_byte_str && use_byte_value_binary_prefix) {
                index = (long long int) (log2(vald)) / 10;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) - 1);
                vald /= exp2(index * 10);
                prefix_string = binary_unit_prefixes[index];
            } else {
                index = (long long int) (log10(vald)) / 3;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) - 1);
                vald /= pow(10, index * 3);
                prefix_string = decimal_unit_prefixes[index];
            }
        }

        if (show_float || (use_value_prefix && vald != (long long int)vald))
            snprintf(buf, buf_size, "%f", vald);
        else
            snprintf(buf, buf_size, "%lld", vali);
        av_strlcatf(buf, buf_size, "%s%s%s", *prefix_string || show_value_unit ? " " : "",
                 prefix_string, show_value_unit ? uv.unit : "");
    }

    return buf;
}

/* WRITERS API */

typedef struct WriterContext WriterContext;

#define WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS 1
#define WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER 2

typedef struct Writer {
    const AVClass *priv_class;      ///< private class of the writer, if any
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
    void (*print_rational)      (WriterContext *wctx, AVRational *q, char *sep);
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
    unsigned int nb_section_packet; ///< number of the packet section in case we are in "packets_and_frames" section
    unsigned int nb_section_frame;  ///< number of the frame  section in case we are in "packets_and_frames" section
    unsigned int nb_section_packet_frame; ///< nb_section_packet or nb_section_frame according if is_packets_and_frames
    unsigned int nb_chapter;        ///< number of the chapter, starting at 0

    int multiple_sections;          ///< tells if the current chapter can contain multiple sections
    int is_fmt_chapter;             ///< tells if the current chapter is "format", required by the print_format_entry option
    int is_packets_and_frames;      ///< tells if the current section is "packets_and_frames"
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
    if (!*wctx)
        return;

    if ((*wctx)->writer->uninit)
        (*wctx)->writer->uninit(*wctx);
    if ((*wctx)->writer->priv_class)
        av_opt_free((*wctx)->priv);
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

    if (writer->priv_class) {
        void *priv_ctx = (*wctx)->priv;
        *((const AVClass **)priv_ctx) = writer->priv_class;
        av_opt_set_defaults(priv_ctx);

        if (args &&
            (ret = av_set_options_string(priv_ctx, args, "=", ":")) < 0)
            goto fail;
    }
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
                                               const char *chapter)
{
    wctx->nb_section =
    wctx->nb_section_packet = wctx->nb_section_frame =
    wctx->nb_section_packet_frame = 0;
    wctx->is_packets_and_frames = !strcmp(chapter, "packets_and_frames");
    wctx->multiple_sections = !strcmp(chapter, "packets") || !strcmp(chapter, "frames" ) ||
                              wctx->is_packets_and_frames ||
                              !strcmp(chapter, "streams") || !strcmp(chapter, "library_versions");
    wctx->is_fmt_chapter = !strcmp(chapter, "format");

    if (wctx->writer->print_chapter_header)
        wctx->writer->print_chapter_header(wctx, chapter);
}

static inline void writer_print_chapter_footer(WriterContext *wctx,
                                               const char *chapter)
{
    if (wctx->writer->print_chapter_footer)
        wctx->writer->print_chapter_footer(wctx, chapter);
    wctx->nb_chapter++;
}

static inline void writer_print_section_header(WriterContext *wctx,
                                               const char *section)
{
    if (wctx->is_packets_and_frames)
        wctx->nb_section_packet_frame = !strcmp(section, "packet") ? wctx->nb_section_packet
                                                                   : wctx->nb_section_frame;
    if (wctx->writer->print_section_header)
        wctx->writer->print_section_header(wctx, section);
    wctx->nb_item = 0;
}

static inline void writer_print_section_footer(WriterContext *wctx,
                                               const char *section)
{
    if (wctx->writer->print_section_footer)
        wctx->writer->print_section_footer(wctx, section);
    if (wctx->is_packets_and_frames) {
        if (!strcmp(section, "packet")) wctx->nb_section_packet++;
        else                            wctx->nb_section_frame++;
    }
    wctx->nb_section++;
}

static inline void writer_print_integer(WriterContext *wctx,
                                        const char *key, long long int val)
{
    if (!wctx->is_fmt_chapter || !fmt_entries_to_show || av_dict_get(fmt_entries_to_show, key, NULL, 0)) {
        wctx->writer->print_integer(wctx, key, val);
        wctx->nb_item++;
    }
}

static inline void writer_print_rational(WriterContext *wctx,
                                         const char *key, AVRational q, char sep)
{
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%d%c%d", q.num, sep, q.den);
    wctx->writer->print_string(wctx, key, buf.str);
    wctx->nb_item++;
}

static inline void writer_print_string(WriterContext *wctx,
                                       const char *key, const char *val, int opt)
{
    if (opt && !(wctx->writer->flags & WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS))
        return;
    if (!wctx->is_fmt_chapter || !fmt_entries_to_show || av_dict_get(fmt_entries_to_show, key, NULL, 0)) {
        wctx->writer->print_string(wctx, key, val);
        wctx->nb_item++;
    }
}

static void writer_print_time(WriterContext *wctx, const char *key,
                              int64_t ts, const AVRational *time_base, int is_duration)
{
    char buf[128];

    if (!wctx->is_fmt_chapter || !fmt_entries_to_show || av_dict_get(fmt_entries_to_show, key, NULL, 0)) {
        if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
            writer_print_string(wctx, key, "N/A", 1);
        } else {
            double d = ts * av_q2d(*time_base);
            struct unit_value uv;
            uv.val.d = d;
            uv.unit = unit_second_str;
            value_string(buf, sizeof(buf), uv);
            writer_print_string(wctx, key, buf, 0);
        }
    }
}

static void writer_print_ts(WriterContext *wctx, const char *key, int64_t ts, int is_duration)
{
    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        writer_print_string(wctx, key, "N/A", 1);
    } else {
        writer_print_integer(wctx, key, ts);
    }
}

static inline void writer_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    wctx->writer->show_tags(wctx, dict);
}

static void writer_print_data(WriterContext *wctx, const char *name,
                              uint8_t *data, int size)
{
    AVBPrint bp;
    int offset = 0, l, i;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, 16);
        for (i = 0; i < l; i++) {
            av_bprintf(&bp, "%02x", data[i]);
            if (i & 1)
                av_bprintf(&bp, " ");
        }
        av_bprint_chars(&bp, ' ', 41 - 2 * i - i / 2);
        for (i = 0; i < l; i++)
            av_bprint_chars(&bp, data[i] - 32U < 95 ? data[i] : '.', 1);
        av_bprintf(&bp, "\n");
        offset += l;
        data   += l;
        size   -= l;
    }
    writer_print_string(wctx, name, bp.str, 0);
    av_bprint_finalize(&bp, NULL);
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


/* WRITERS */

#define DEFINE_WRITER_CLASS(name)                   \
static const char *name##_get_name(void *ctx)       \
{                                                   \
    return #name ;                                  \
}                                                   \
static const AVClass name##_class = {               \
    #name,                                          \
    name##_get_name,                                \
    name##_options                                  \
}

/* Default output */

typedef struct DefaultContext {
    const AVClass *class;
    int nokey;
    int noprint_wrappers;
} DefaultContext;

#define OFFSET(x) offsetof(DefaultContext, x)

static const AVOption default_options[] = {
    { "noprint_wrappers", "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    { "nw",               "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    { "nokey",          "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    { "nk",             "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    {NULL},
};

DEFINE_WRITER_CLASS(default);

/* lame uppercasing routine, assumes the string is lower case ASCII */
static inline char *upcase_string(char *dst, size_t dst_size, const char *src)
{
    int i;
    for (i = 0; src[i] && i < dst_size-1; i++)
        dst[i] = av_toupper(src[i]);
    dst[i] = 0;
    return dst;
}

static void default_print_section_header(WriterContext *wctx, const char *section)
{
    DefaultContext *def = wctx->priv;
    char buf[32];

    if (!def->noprint_wrappers)
        printf("[%s]\n", upcase_string(buf, sizeof(buf), section));
}

static void default_print_section_footer(WriterContext *wctx, const char *section)
{
    DefaultContext *def = wctx->priv;
    char buf[32];

    if (!def->noprint_wrappers)
        printf("[/%s]\n", upcase_string(buf, sizeof(buf), section));
}

static void default_print_str(WriterContext *wctx, const char *key, const char *value)
{
    DefaultContext *def = wctx->priv;
    if (!def->nokey)
        printf("%s=", key);
    printf("%s\n", value);
}

static void default_print_int(WriterContext *wctx, const char *key, long long int value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        printf("%s=", key);
    printf("%lld\n", value);
}

static void default_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (!fmt_entries_to_show || (tag->key && av_dict_get(fmt_entries_to_show, tag->key, NULL, 0)))
            printf("TAG:");
        writer_print_string(wctx, tag->key, tag->value, 0);
    }
}

static const Writer default_writer = {
    .name                  = "default",
    .priv_size             = sizeof(DefaultContext),
    .print_section_header  = default_print_section_header,
    .print_section_footer  = default_print_section_footer,
    .print_integer         = default_print_int,
    .print_string          = default_print_str,
    .show_tags             = default_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
    .priv_class            = &default_class,
};

/* Compact output */

/**
 * Apply C-language-like string escaping.
 */
static const char *c_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
        case '\b': av_bprintf(dst, "%s", "\\b");  break;
        case '\f': av_bprintf(dst, "%s", "\\f");  break;
        case '\n': av_bprintf(dst, "%s", "\\n");  break;
        case '\r': av_bprintf(dst, "%s", "\\r");  break;
        case '\\': av_bprintf(dst, "%s", "\\\\"); break;
        default:
            if (*p == sep)
                av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

/**
 * Quote fields containing special characters, check RFC4180.
 */
static const char *csv_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    const char *p;
    int quote = 0;

    /* check if input needs quoting */
    for (p = src; *p; p++)
        if (*p == '"' || *p == sep || *p == '\n' || *p == '\r')
            quote = 1;

    if (quote)
        av_bprint_chars(dst, '\"', 1);

    for (p = src; *p; p++) {
        if (*p == '"')
            av_bprint_chars(dst, '\"', 1);
        av_bprint_chars(dst, *p, 1);
    }
    if (quote)
        av_bprint_chars(dst, '\"', 1);
    return dst->str;
}

static const char *none_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    return src;
}

typedef struct CompactContext {
    const AVClass *class;
    char *item_sep_str;
    char item_sep;
    int nokey;
    int print_section;
    char *escape_mode_str;
    const char * (*escape_str)(AVBPrint *dst, const char *src, const char sep, void *log_ctx);
} CompactContext;

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption compact_options[]= {
    {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
    {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
    {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.i64=0},    0,        1        },
    {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.i64=0},    0,        1        },
    {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
    {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
    {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {NULL},
};

DEFINE_WRITER_CLASS(compact);

static av_cold int compact_init(WriterContext *wctx, const char *args, void *opaque)
{
    CompactContext *compact = wctx->priv;

    if (strlen(compact->item_sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               compact->item_sep_str);
        return AVERROR(EINVAL);
    }
    compact->item_sep = compact->item_sep_str[0];

    if      (!strcmp(compact->escape_mode_str, "none")) compact->escape_str = none_escape_str;
    else if (!strcmp(compact->escape_mode_str, "c"   )) compact->escape_str = c_escape_str;
    else if (!strcmp(compact->escape_mode_str, "csv" )) compact->escape_str = csv_escape_str;
    else {
        av_log(wctx, AV_LOG_ERROR, "Unknown escape mode '%s'\n", compact->escape_mode_str);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void compact_print_section_header(WriterContext *wctx, const char *section)
{
    CompactContext *compact = wctx->priv;

    if (compact->print_section)
        printf("%s%c", section, compact->item_sep);
}

static void compact_print_section_footer(WriterContext *wctx, const char *section)
{
    printf("\n");
}

static void compact_print_str(WriterContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s=", key);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s", compact->escape_str(&buf, value, compact->item_sep, wctx));
    av_bprint_finalize(&buf, NULL);
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
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (wctx->nb_item) printf("%c", compact->item_sep);
        if (!compact->nokey) {
            av_bprint_clear(&buf);
            printf("tag:%s=", compact->escape_str(&buf, tag->key, compact->item_sep, wctx));
        }
        av_bprint_clear(&buf);
        printf("%s", compact->escape_str(&buf, tag->value, compact->item_sep, wctx));
    }
    av_bprint_finalize(&buf, NULL);
}

static const Writer compact_writer = {
    .name                 = "compact",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .show_tags            = compact_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
    .priv_class           = &compact_class,
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
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .show_tags            = compact_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
    .priv_class           = &compact_class,
};

/* Flat output */

typedef struct FlatContext {
    const AVClass *class;
    const char *section, *chapter;
    const char *sep_str;
    char sep;
    int hierarchical;
} FlatContext;

#undef OFFSET
#define OFFSET(x) offsetof(FlatContext, x)

static const AVOption flat_options[]= {
    {"sep_char", "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  CHAR_MIN, CHAR_MAX },
    {"s",        "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  CHAR_MIN, CHAR_MAX },
    {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_INT, {.i64=1}, 0, 1 },
    {"h",           "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_INT, {.i64=1}, 0, 1 },
    {NULL},
};

DEFINE_WRITER_CLASS(flat);

static av_cold int flat_init(WriterContext *wctx, const char *args, void *opaque)
{
    FlatContext *flat = wctx->priv;

    if (strlen(flat->sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               flat->sep_str);
        return AVERROR(EINVAL);
    }
    flat->sep = flat->sep_str[0];
    return 0;
}

static const char *flat_escape_key_str(AVBPrint *dst, const char *src, const char sep)
{
    const char *p;

    for (p = src; *p; p++) {
        if (!((*p >= '0' && *p <= '9') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z')))
            av_bprint_chars(dst, '_', 1);
        else
            av_bprint_chars(dst, *p, 1);
    }
    return dst->str;
}

static const char *flat_escape_value_str(AVBPrint *dst, const char *src)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
        case '\n': av_bprintf(dst, "%s", "\\n");  break;
        case '\r': av_bprintf(dst, "%s", "\\r");  break;
        case '\\': av_bprintf(dst, "%s", "\\\\"); break;
        case '"':  av_bprintf(dst, "%s", "\\\""); break;
        case '`':  av_bprintf(dst, "%s", "\\`");  break;
        case '$':  av_bprintf(dst, "%s", "\\$");  break;
        default:   av_bprint_chars(dst, *p, 1);   break;
        }
    }
    return dst->str;
}

static void flat_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    FlatContext *flat = wctx->priv;
    flat->chapter = chapter;
}

static void flat_print_section_header(WriterContext *wctx, const char *section)
{
    FlatContext *flat = wctx->priv;
    flat->section = section;
}

static void flat_print_section(WriterContext *wctx)
{
    FlatContext *flat = wctx->priv;
    int n = wctx->is_packets_and_frames ? wctx->nb_section_packet_frame
                                        : wctx->nb_section;

    if (flat->hierarchical && wctx->multiple_sections)
        printf("%s%c", flat->chapter, flat->sep);
    printf("%s%c", flat->section, flat->sep);
    if (wctx->multiple_sections)
        printf("%d%c", n, flat->sep);
}

static void flat_print_int(WriterContext *wctx, const char *key, long long int value)
{
    flat_print_section(wctx);
    printf("%s=%lld\n", key, value);
}

static void flat_print_str(WriterContext *wctx, const char *key, const char *value)
{
    FlatContext *flat = wctx->priv;
    AVBPrint buf;

    flat_print_section(wctx);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=", flat_escape_key_str(&buf, key, flat->sep));
    av_bprint_clear(&buf);
    printf("\"%s\"\n", flat_escape_value_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static void flat_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    FlatContext *flat = wctx->priv;
    AVBPrint buf;
    AVDictionaryEntry *tag = NULL;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        flat_print_section(wctx);
        av_bprint_clear(&buf);
        printf("tags%c%s=", flat->sep, flat_escape_key_str(&buf, tag->key, flat->sep));
        av_bprint_clear(&buf);
        printf("\"%s\"\n", flat_escape_value_str(&buf, tag->value));
    }
    av_bprint_finalize(&buf, NULL);
}

static const Writer flat_writer = {
    .name                  = "flat",
    .priv_size             = sizeof(FlatContext),
    .init                  = flat_init,
    .print_chapter_header  = flat_print_chapter_header,
    .print_section_header  = flat_print_section_header,
    .print_integer         = flat_print_int,
    .print_string          = flat_print_str,
    .show_tags             = flat_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS|WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
    .priv_class            = &flat_class,
};

/* INI format output */

typedef struct {
    const AVClass *class;
    AVBPrint chapter_name, section_name;
    int hierarchical;
} INIContext;

#undef OFFSET
#define OFFSET(x) offsetof(INIContext, x)

static const AVOption ini_options[] = {
    {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_INT, {.i64=1}, 0, 1 },
    {"h",           "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_INT, {.i64=1}, 0, 1 },
    {NULL},
};

DEFINE_WRITER_CLASS(ini);

static av_cold int ini_init(WriterContext *wctx, const char *args, void *opaque)
{
    INIContext *ini = wctx->priv;

    av_bprint_init(&ini->chapter_name, 1, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&ini->section_name, 1, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

static av_cold void ini_uninit(WriterContext *wctx)
{
    INIContext *ini = wctx->priv;
    av_bprint_finalize(&ini->chapter_name, NULL);
    av_bprint_finalize(&ini->section_name, NULL);
}

static void ini_print_header(WriterContext *wctx)
{
    printf("# ffprobe output\n\n");
}

static char *ini_escape_str(AVBPrint *dst, const char *src)
{
    int i = 0;
    char c = 0;

    while (c = src[i++]) {
        switch (c) {
        case '\b': av_bprintf(dst, "%s", "\\b"); break;
        case '\f': av_bprintf(dst, "%s", "\\f"); break;
        case '\n': av_bprintf(dst, "%s", "\\n"); break;
        case '\r': av_bprintf(dst, "%s", "\\r"); break;
        case '\t': av_bprintf(dst, "%s", "\\t"); break;
        case '\\':
        case '#' :
        case '=' :
        case ':' : av_bprint_chars(dst, '\\', 1);
        default:
            if ((unsigned char)c < 32)
                av_bprintf(dst, "\\x00%02x", c & 0xff);
            else
                av_bprint_chars(dst, c, 1);
            break;
        }
    }
    return dst->str;
}

static void ini_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    INIContext *ini = wctx->priv;

    av_bprint_clear(&ini->chapter_name);
    av_bprintf(&ini->chapter_name, "%s", chapter);

    if (wctx->nb_chapter)
        printf("\n");
}

static void ini_print_section_header(WriterContext *wctx, const char *section)
{
    INIContext *ini = wctx->priv;
    int n = wctx->is_packets_and_frames ? wctx->nb_section_packet_frame
                                        : wctx->nb_section;
    if (wctx->nb_section)
        printf("\n");
    av_bprint_clear(&ini->section_name);

    if (ini->hierarchical && wctx->multiple_sections)
        av_bprintf(&ini->section_name, "%s.", ini->chapter_name.str);
    av_bprintf(&ini->section_name, "%s", section);

    if (wctx->multiple_sections)
        av_bprintf(&ini->section_name, ".%d", n);
    printf("[%s]\n", ini->section_name.str);
}

static void ini_print_str(WriterContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=", ini_escape_str(&buf, key));
    av_bprint_clear(&buf);
    printf("%s\n", ini_escape_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static void ini_print_int(WriterContext *wctx, const char *key, long long int value)
{
    printf("%s=%lld\n", key, value);
}

static void ini_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    INIContext *ini = wctx->priv;
    AVDictionaryEntry *tag = NULL;
    int is_first = 1;

    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (is_first) {
            printf("\n[%s.tags]\n", ini->section_name.str);
            is_first = 0;
        }
        writer_print_string(wctx, tag->key, tag->value, 0);
    }
}

static const Writer ini_writer = {
    .name                  = "ini",
    .priv_size             = sizeof(INIContext),
    .init                  = ini_init,
    .uninit                = ini_uninit,
    .print_header          = ini_print_header,
    .print_chapter_header  = ini_print_chapter_header,
    .print_section_header  = ini_print_section_header,
    .print_integer         = ini_print_int,
    .print_string          = ini_print_str,
    .show_tags             = ini_show_tags,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS|WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
    .priv_class            = &ini_class,
};

/* JSON output */

typedef struct {
    const AVClass *class;
    int indent_level;
    int compact;
    const char *item_sep, *item_start_end;
} JSONContext;

#undef OFFSET
#define OFFSET(x) offsetof(JSONContext, x)

static const AVOption json_options[]= {
    { "compact", "enable compact output", OFFSET(compact), AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    { "c",       "enable compact output", OFFSET(compact), AV_OPT_TYPE_INT, {.i64=0}, 0, 1 },
    { NULL }
};

DEFINE_WRITER_CLASS(json);

static av_cold int json_init(WriterContext *wctx, const char *args, void *opaque)
{
    JSONContext *json = wctx->priv;

    json->item_sep       = json->compact ? ", " : ",\n";
    json->item_start_end = json->compact ? " "  : "\n";

    return 0;
}

static const char *json_escape_str(AVBPrint *dst, const char *src, void *log_ctx)
{
    static const char json_escape[] = {'"', '\\', '\b', '\f', '\n', '\r', '\t', 0};
    static const char json_subst[]  = {'"', '\\',  'b',  'f',  'n',  'r',  't', 0};
    const char *p;

    for (p = src; *p; p++) {
        char *s = strchr(json_escape, *p);
        if (s) {
            av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, json_subst[s - json_escape], 1);
        } else if ((unsigned char)*p < 32) {
            av_bprintf(dst, "\\u00%02x", *p & 0xff);
        } else {
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

static void json_print_header(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    printf("{");
    json->indent_level++;
}

static void json_print_footer(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    json->indent_level--;
    printf("\n}\n");
}

#define JSON_INDENT() printf("%*c", json->indent_level * 4, ' ')

static void json_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_chapter)
        printf(",");
    printf("\n");
    if (wctx->multiple_sections) {
        JSON_INDENT();
        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        printf("\"%s\": [\n", json_escape_str(&buf, chapter, wctx));
        av_bprint_finalize(&buf, NULL);
        json->indent_level++;
    }
}

static void json_print_chapter_footer(WriterContext *wctx, const char *chapter)
{
    JSONContext *json = wctx->priv;

    if (wctx->multiple_sections) {
        printf("\n");
        json->indent_level--;
        JSON_INDENT();
        printf("]");
    }
}

static void json_print_section_header(WriterContext *wctx, const char *section)
{
    JSONContext *json = wctx->priv;

    if (wctx->nb_section)
        printf(",\n");
    JSON_INDENT();
    if (!wctx->multiple_sections)
        printf("\"%s\": ", section);
    printf("{%s", json->item_start_end);
    json->indent_level++;
    /* this is required so the parser can distinguish between packets and frames */
    if (wctx->is_packets_and_frames) {
        if (!json->compact)
            JSON_INDENT();
        printf("\"type\": \"%s\"%s", section, json->item_sep);
    }
}

static void json_print_section_footer(WriterContext *wctx, const char *section)
{
    JSONContext *json = wctx->priv;

    printf("%s", json->item_start_end);
    json->indent_level--;
    if (!json->compact)
        JSON_INDENT();
    printf("}");
}

static inline void json_print_item_str(WriterContext *wctx,
                                       const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("\"%s\":", json_escape_str(&buf, key,   wctx));
    av_bprint_clear(&buf);
    printf(" \"%s\"", json_escape_str(&buf, value, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void json_print_str(WriterContext *wctx, const char *key, const char *value)
{
    JSONContext *json = wctx->priv;

    if (wctx->nb_item) printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    json_print_item_str(wctx, key, value);
}

static void json_print_int(WriterContext *wctx, const char *key, long long int value)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item) printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("\"%s\": %lld", json_escape_str(&buf, key, wctx), value);
    av_bprint_finalize(&buf, NULL);
}

static void json_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    JSONContext *json = wctx->priv;
    AVDictionaryEntry *tag = NULL;
    int is_first = 1;
    if (!dict)
        return;
    printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    printf("\"tags\": {%s", json->item_start_end);
    json->indent_level++;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (is_first) is_first = 0;
        else          printf("%s", json->item_sep);
        if (!json->compact)
            JSON_INDENT();
        json_print_item_str(wctx, tag->key, tag->value);
    }
    json->indent_level--;
    printf("%s", json->item_start_end);
    if (!json->compact)
        JSON_INDENT();
    printf("}");
}

static const Writer json_writer = {
    .name                 = "json",
    .priv_size            = sizeof(JSONContext),
    .init                 = json_init,
    .print_header         = json_print_header,
    .print_footer         = json_print_footer,
    .print_chapter_header = json_print_chapter_header,
    .print_chapter_footer = json_print_chapter_footer,
    .print_section_header = json_print_section_header,
    .print_section_footer = json_print_section_footer,
    .print_integer        = json_print_int,
    .print_string         = json_print_str,
    .show_tags            = json_show_tags,
    .flags = WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
    .priv_class           = &json_class,
};

/* XML output */

typedef struct {
    const AVClass *class;
    int within_tag;
    int indent_level;
    int fully_qualified;
    int xsd_strict;
} XMLContext;

#undef OFFSET
#define OFFSET(x) offsetof(XMLContext, x)

static const AVOption xml_options[] = {
    {"fully_qualified", "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_INT, {.i64=0},  0, 1 },
    {"q",               "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_INT, {.i64=0},  0, 1 },
    {"xsd_strict",      "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_INT, {.i64=0},  0, 1 },
    {"x",               "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_INT, {.i64=0},  0, 1 },
    {NULL},
};

DEFINE_WRITER_CLASS(xml);

static av_cold int xml_init(WriterContext *wctx, const char *args, void *opaque)
{
    XMLContext *xml = wctx->priv;

    if (xml->xsd_strict) {
        xml->fully_qualified = 1;
#define CHECK_COMPLIANCE(opt, opt_name)                                 \
        if (opt) {                                                      \
            av_log(wctx, AV_LOG_ERROR,                                  \
                   "XSD-compliant output selected but option '%s' was selected, XML output may be non-compliant.\n" \
                   "You need to disable such option with '-no%s'\n", opt_name, opt_name); \
            return AVERROR(EINVAL);                                     \
        }
        CHECK_COMPLIANCE(show_private_data, "private");
        CHECK_COMPLIANCE(show_value_unit,   "unit");
        CHECK_COMPLIANCE(use_value_prefix,  "prefix");

        if (do_show_frames && do_show_packets) {
            av_log(wctx, AV_LOG_ERROR,
                   "Interleaved frames and packets are not allowed in XSD. "
                   "Select only one between the -show_frames and the -show_packets options.\n");
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static const char *xml_escape_str(AVBPrint *dst, const char *src, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
        case '&' : av_bprintf(dst, "%s", "&amp;");  break;
        case '<' : av_bprintf(dst, "%s", "&lt;");   break;
        case '>' : av_bprintf(dst, "%s", "&gt;");   break;
        case '\"': av_bprintf(dst, "%s", "&quot;"); break;
        case '\'': av_bprintf(dst, "%s", "&apos;"); break;
        default: av_bprint_chars(dst, *p, 1);
        }
    }

    return dst->str;
}

static void xml_print_header(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const char *qual = " xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
        "xmlns:ffprobe='http://www.ffmpeg.org/schema/ffprobe' "
        "xsi:schemaLocation='http://www.ffmpeg.org/schema/ffprobe ffprobe.xsd'";

    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<%sffprobe%s>\n",
           xml->fully_qualified ? "ffprobe:" : "",
           xml->fully_qualified ? qual : "");

    xml->indent_level++;
}

static void xml_print_footer(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;

    xml->indent_level--;
    printf("</%sffprobe>\n", xml->fully_qualified ? "ffprobe:" : "");
}

#define XML_INDENT() printf("%*c", xml->indent_level * 4, ' ')

static void xml_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    XMLContext *xml = wctx->priv;

    if (wctx->nb_chapter)
        printf("\n");
    if (wctx->multiple_sections) {
        XML_INDENT(); printf("<%s>\n", chapter);
        xml->indent_level++;
    }
}

static void xml_print_chapter_footer(WriterContext *wctx, const char *chapter)
{
    XMLContext *xml = wctx->priv;

    if (wctx->multiple_sections) {
        xml->indent_level--;
        XML_INDENT(); printf("</%s>\n", chapter);
    }
}

static void xml_print_section_header(WriterContext *wctx, const char *section)
{
    XMLContext *xml = wctx->priv;

    XML_INDENT(); printf("<%s ", section);
    xml->within_tag = 1;
}

static void xml_print_section_footer(WriterContext *wctx, const char *section)
{
    XMLContext *xml = wctx->priv;

    if (xml->within_tag)
        printf("/>\n");
    else {
        XML_INDENT(); printf("</%s>\n", section);
    }
}

static void xml_print_str(WriterContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;

    if (wctx->nb_item)
        printf(" ");
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=\"%s\"", key, xml_escape_str(&buf, value, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void xml_print_int(WriterContext *wctx, const char *key, long long int value)
{
    if (wctx->nb_item)
        printf(" ");
    printf("%s=\"%lld\"", key, value);
}

static void xml_show_tags(WriterContext *wctx, AVDictionary *dict)
{
    XMLContext *xml = wctx->priv;
    AVDictionaryEntry *tag = NULL;
    int is_first = 1;
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    xml->indent_level++;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (is_first) {
            /* close section tag */
            printf(">\n");
            xml->within_tag = 0;
            is_first = 0;
        }
        XML_INDENT();

        av_bprint_clear(&buf);
        printf("<tag key=\"%s\"", xml_escape_str(&buf, tag->key, wctx));
        av_bprint_clear(&buf);
        printf(" value=\"%s\"/>\n", xml_escape_str(&buf, tag->value, wctx));
    }
    av_bprint_finalize(&buf, NULL);
    xml->indent_level--;
}

static Writer xml_writer = {
    .name                 = "xml",
    .priv_size            = sizeof(XMLContext),
    .init                 = xml_init,
    .print_header         = xml_print_header,
    .print_footer         = xml_print_footer,
    .print_chapter_header = xml_print_chapter_header,
    .print_chapter_footer = xml_print_chapter_footer,
    .print_section_header = xml_print_section_header,
    .print_section_footer = xml_print_section_footer,
    .print_integer        = xml_print_int,
    .print_string         = xml_print_str,
    .show_tags            = xml_show_tags,
    .flags = WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
    .priv_class           = &xml_class,
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
    writer_register(&flat_writer);
    writer_register(&ini_writer);
    writer_register(&json_writer);
    writer_register(&xml_writer);
}

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    writer_print_string(w, k, pbuf.str, 0);    \
} while (0)

#define print_int(k, v)         writer_print_integer(w, k, v)
#define print_q(k, v, s)        writer_print_rational(w, k, v, s)
#define print_str(k, v)         writer_print_string(w, k, v, 0)
#define print_str_opt(k, v)     writer_print_string(w, k, v, 1)
#define print_time(k, v, tb)    writer_print_time(w, k, v, tb, 0)
#define print_ts(k, v)          writer_print_ts(w, k, v, 0)
#define print_duration_time(k, v, tb) writer_print_time(w, k, v, tb, 1)
#define print_duration_ts(k, v)       writer_print_ts(w, k, v, 1)
#define print_val(k, v, u) do {                                     \
    struct unit_value uv;                                           \
    uv.val.i = v;                                                   \
    uv.unit = u;                                                    \
    writer_print_string(w, k, value_string(val_str, sizeof(val_str), uv), 0); \
} while (0)

#define print_section_header(s) writer_print_section_header(w, s)
#define print_section_footer(s) writer_print_section_footer(w, s)
#define show_tags(metadata)     writer_show_tags(w, metadata)

static void show_packet(WriterContext *w, AVFormatContext *fmt_ctx, AVPacket *pkt, int packet_idx)
{
    char val_str[128];
    AVStream *st = fmt_ctx->streams[pkt->stream_index];
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    print_section_header("packet");
    s = av_get_media_type_string(st->codec->codec_type);
    if (s) print_str    ("codec_type", s);
    else   print_str_opt("codec_type", "unknown");
    print_int("stream_index",     pkt->stream_index);
    print_ts  ("pts",             pkt->pts);
    print_time("pts_time",        pkt->pts, &st->time_base);
    print_ts  ("dts",             pkt->dts);
    print_time("dts_time",        pkt->dts, &st->time_base);
    print_duration_ts("duration",        pkt->duration);
    print_duration_time("duration_time", pkt->duration, &st->time_base);
    print_duration_ts("convergence_duration", pkt->convergence_duration);
    print_duration_time("convergence_duration_time", pkt->convergence_duration, &st->time_base);
    print_val("size",             pkt->size, unit_byte_str);
    if (pkt->pos != -1) print_fmt    ("pos", "%"PRId64, pkt->pos);
    else                print_str_opt("pos", "N/A");
    print_fmt("flags", "%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_');
    if (do_show_data)
        writer_print_data(w, "data", pkt->data, pkt->size);
    print_section_footer("packet");

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_frame(WriterContext *w, AVFrame *frame, AVStream *stream,
                       AVFormatContext *fmt_ctx)
{
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    print_section_header("frame");

    s = av_get_media_type_string(stream->codec->codec_type);
    if (s) print_str    ("media_type", s);
    else   print_str_opt("media_type", "unknown");
    print_int("key_frame",              frame->key_frame);
    print_ts  ("pkt_pts",               frame->pkt_pts);
    print_time("pkt_pts_time",          frame->pkt_pts, &stream->time_base);
    print_ts  ("pkt_dts",               frame->pkt_dts);
    print_time("pkt_dts_time",          frame->pkt_dts, &stream->time_base);
    print_duration_ts  ("pkt_duration",      frame->pkt_duration);
    print_duration_time("pkt_duration_time", frame->pkt_duration, &stream->time_base);
    if (frame->pkt_pos != -1) print_fmt    ("pkt_pos", "%"PRId64, frame->pkt_pos);
    else                      print_str_opt("pkt_pos", "N/A");

    switch (stream->codec->codec_type) {
        AVRational sar;

    case AVMEDIA_TYPE_VIDEO:
        print_int("width",                  frame->width);
        print_int("height",                 frame->height);
        s = av_get_pix_fmt_name(frame->format);
        if (s) print_str    ("pix_fmt", s);
        else   print_str_opt("pix_fmt", "unknown");
        sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, frame);
        if (sar.num) {
            print_q("sample_aspect_ratio", sar, ':');
        } else {
            print_str_opt("sample_aspect_ratio", "N/A");
        }
        print_fmt("pict_type",              "%c", av_get_picture_type_char(frame->pict_type));
        print_int("coded_picture_number",   frame->coded_picture_number);
        print_int("display_picture_number", frame->display_picture_number);
        print_int("interlaced_frame",       frame->interlaced_frame);
        print_int("top_field_first",        frame->top_field_first);
        print_int("repeat_pict",            frame->repeat_pict);
        print_int("reference",              frame->reference);
        break;

    case AVMEDIA_TYPE_AUDIO:
        s = av_get_sample_fmt_name(frame->format);
        if (s) print_str    ("sample_fmt", s);
        else   print_str_opt("sample_fmt", "unknown");
        print_int("nb_samples",         frame->nb_samples);
        print_int("channels", av_frame_get_channels(frame));
        if (av_frame_get_channel_layout(frame)) {
            av_bprint_clear(&pbuf);
            av_bprint_channel_layout(&pbuf, av_frame_get_channels(frame),
                                     av_frame_get_channel_layout(frame));
            print_str    ("channel_layout", pbuf.str);
        } else
            print_str_opt("channel_layout", "unknown");
        break;
    }
    show_tags(av_frame_get_metadata(frame));

    print_section_footer("frame");

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static av_always_inline int process_frame(WriterContext *w,
                                          AVFormatContext *fmt_ctx,
                                          AVFrame *frame, AVPacket *pkt)
{
    AVCodecContext *dec_ctx = fmt_ctx->streams[pkt->stream_index]->codec;
    int ret = 0, got_frame = 0;

    avcodec_get_frame_defaults(frame);
    if (dec_ctx->codec) {
        switch (dec_ctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            ret = avcodec_decode_video2(dec_ctx, frame, &got_frame, pkt);
            break;

        case AVMEDIA_TYPE_AUDIO:
            ret = avcodec_decode_audio4(dec_ctx, frame, &got_frame, pkt);
            break;
        }
    }

    if (ret < 0)
        return ret;
    ret = FFMIN(ret, pkt->size); /* guard against bogus return values */
    pkt->data += ret;
    pkt->size -= ret;
    if (got_frame) {
        nb_streams_frames[pkt->stream_index]++;
        if (do_show_frames)
            show_frame(w, frame, fmt_ctx->streams[pkt->stream_index], fmt_ctx);
    }
    return got_frame;
}

static void read_packets(WriterContext *w, AVFormatContext *fmt_ctx)
{
    AVPacket pkt, pkt1;
    AVFrame frame;
    int i = 0;

    av_init_packet(&pkt);

    while (!av_read_frame(fmt_ctx, &pkt)) {
        if (do_read_packets) {
            if (do_show_packets)
                show_packet(w, fmt_ctx, &pkt, i++);
            nb_streams_packets[pkt.stream_index]++;
        }
        if (do_read_frames) {
            pkt1 = pkt;
            while (pkt1.size && process_frame(w, fmt_ctx, &frame, &pkt1) > 0);
        }
        av_free_packet(&pkt);
    }
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    //Flush remaining frames that are cached in the decoder
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        pkt.stream_index = i;
        if (do_read_frames)
            while (process_frame(w, fmt_ctx, &frame, &pkt) > 0);
    }
}

static void show_stream(WriterContext *w, AVFormatContext *fmt_ctx, int stream_idx)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    const AVCodec *dec;
    char val_str[128];
    const char *s;
    AVRational sar, dar;
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    print_section_header("stream");

    print_int("index", stream->index);

    if ((dec_ctx = stream->codec)) {
        const char *profile = NULL;
        if ((dec = dec_ctx->codec)) {
            print_str("codec_name",      dec->name);
            print_str("codec_long_name", dec->long_name);
        } else {
            print_str_opt("codec_name",      "unknown");
            print_str_opt("codec_long_name", "unknown");
        }

        if (dec && (profile = av_get_profile_name(dec, dec_ctx->profile)))
            print_str("profile", profile);
        else
            print_str_opt("profile", "unknown");

        s = av_get_media_type_string(dec_ctx->codec_type);
        if (s) print_str    ("codec_type", s);
        else   print_str_opt("codec_type", "unknown");
        print_q("codec_time_base", dec_ctx->time_base, '/');

        /* print AVI/FourCC tag */
        av_get_codec_tag_string(val_str, sizeof(val_str), dec_ctx->codec_tag);
        print_str("codec_tag_string",    val_str);
        print_fmt("codec_tag", "0x%04x", dec_ctx->codec_tag);

        /* Print useful disposition */
        print_int("default", !!(stream->disposition & AV_DISPOSITION_DEFAULT));
        print_int("forced", !!(stream->disposition & AV_DISPOSITION_FORCED));

        switch (dec_ctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            print_int("width",        dec_ctx->width);
            print_int("height",       dec_ctx->height);
            print_int("has_b_frames", dec_ctx->has_b_frames);
            sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, NULL);
            if (sar.den) {
                print_q("sample_aspect_ratio", sar, ':');
                av_reduce(&dar.num, &dar.den,
                          dec_ctx->width  * sar.num,
                          dec_ctx->height * sar.den,
                          1024*1024);
                print_q("display_aspect_ratio", dar, ':');
            } else {
                print_str_opt("sample_aspect_ratio", "N/A");
                print_str_opt("display_aspect_ratio", "N/A");
            }
            s = av_get_pix_fmt_name(dec_ctx->pix_fmt);
            if (s) print_str    ("pix_fmt", s);
            else   print_str_opt("pix_fmt", "unknown");
            print_int("level",   dec_ctx->level);
            if (dec_ctx->timecode_frame_start >= 0) {
                char tcbuf[AV_TIMECODE_STR_SIZE];
                av_timecode_make_mpeg_tc_string(tcbuf, dec_ctx->timecode_frame_start);
                print_str("timecode", tcbuf);
            } else {
                print_str_opt("timecode", "N/A");
            }
            print_int("attached_pic",
                      !!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC));
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
    if (dec_ctx->codec && dec_ctx->codec->priv_class && show_private_data) {
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
    print_q("r_frame_rate",   stream->r_frame_rate,   '/');
    print_q("avg_frame_rate", stream->avg_frame_rate, '/');
    print_q("time_base",      stream->time_base,      '/');
    print_ts  ("start_pts",   stream->start_time);
    print_time("start_time",  stream->start_time, &stream->time_base);
    print_ts  ("duration_ts", stream->duration);
    print_time("duration",    stream->duration, &stream->time_base);
    if (dec_ctx->bit_rate > 0) print_val    ("bit_rate", dec_ctx->bit_rate, unit_bit_per_second_str);
    else                       print_str_opt("bit_rate", "N/A");
    if (stream->nb_frames) print_fmt    ("nb_frames", "%"PRId64, stream->nb_frames);
    else                   print_str_opt("nb_frames", "N/A");
    if (nb_streams_frames[stream_idx])  print_fmt    ("nb_read_frames", "%"PRIu64, nb_streams_frames[stream_idx]);
    else                                print_str_opt("nb_read_frames", "N/A");
    if (nb_streams_packets[stream_idx]) print_fmt    ("nb_read_packets", "%"PRIu64, nb_streams_packets[stream_idx]);
    else                                print_str_opt("nb_read_packets", "N/A");
    if (do_show_data)
        writer_print_data(w, "extradata", dec_ctx->extradata,
                                          dec_ctx->extradata_size);
    show_tags(stream->metadata);

    print_section_footer("stream");
    av_bprint_finalize(&pbuf, NULL);
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
    int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;

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
    fflush(stdout);
}

static void show_error(WriterContext *w, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));

    writer_print_chapter_header(w, "error");
    print_section_header("error");
    print_int("code", err);
    print_str("string", errbuf_ptr);
    print_section_footer("error");
    writer_print_chapter_footer(w, "error");
}

static int open_input_file(AVFormatContext **fmt_ctx_ptr, const char *filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;

    if ((err = avformat_open_input(&fmt_ctx, filename,
                                   iformat, &format_opts)) < 0) {
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

        if (stream->codec->codec_id == AV_CODEC_ID_PROBE) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to probe codec for input stream %d\n",
                    stream->index);
        } else if (!(codec = avcodec_find_decoder(stream->codec->codec_id))) {
            av_log(NULL, AV_LOG_ERROR,
                    "Unsupported codec with id %d for input stream %d\n",
                    stream->codec->codec_id, stream->index);
        } else if (avcodec_open2(stream->codec, codec, NULL) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while opening codec for input stream %d\n",
                   stream->index);
        }
    }

    *fmt_ctx_ptr = fmt_ctx;
    return 0;
}

static void close_input_file(AVFormatContext **ctx_ptr)
{
    int i;
    AVFormatContext *fmt_ctx = *ctx_ptr;

    /* close decoder for each stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++)
        if (fmt_ctx->streams[i]->codec->codec_id != AV_CODEC_ID_NONE)
            avcodec_close(fmt_ctx->streams[i]->codec);

    avformat_close_input(ctx_ptr);
}

#define PRINT_CHAPTER(name) do {                                        \
    if (do_show_ ## name) {                                             \
        writer_print_chapter_header(wctx, #name);                       \
        show_ ## name (wctx, fmt_ctx);                                  \
        writer_print_chapter_footer(wctx, #name);                       \
    }                                                                   \
} while (0)

static int probe_file(WriterContext *wctx, const char *filename)
{
    AVFormatContext *fmt_ctx;
    int ret;

    do_read_frames = do_show_frames || do_count_frames;
    do_read_packets = do_show_packets || do_count_packets;

    ret = open_input_file(&fmt_ctx, filename);
    if (ret >= 0) {
        nb_streams_frames  = av_calloc(fmt_ctx->nb_streams, sizeof(*nb_streams_frames));
        nb_streams_packets = av_calloc(fmt_ctx->nb_streams, sizeof(*nb_streams_packets));
        if (do_read_frames || do_read_packets) {
            const char *chapter;
            if (do_show_frames && do_show_packets &&
                wctx->writer->flags & WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER)
                chapter = "packets_and_frames";
            else if (do_show_packets && !do_show_frames)
                chapter = "packets";
            else // (!do_show_packets && do_show_frames)
                chapter = "frames";
            if (do_show_frames || do_show_packets)
                writer_print_chapter_header(wctx, chapter);
            read_packets(wctx, fmt_ctx);
            if (do_show_frames || do_show_packets)
                writer_print_chapter_footer(wctx, chapter);
        }
        PRINT_CHAPTER(streams);
        PRINT_CHAPTER(format);
        close_input_file(&fmt_ctx);
        av_freep(&nb_streams_frames);
        av_freep(&nb_streams_packets);
    }
    return ret;
}

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Simple multimedia streams analyzer\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [OPTIONS] [INPUT_FILE]\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

static void ffprobe_show_program_version(WriterContext *w)
{
    AVBPrint pbuf;
    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_chapter_header(w, "program_version");
    print_section_header("program_version");
    print_str("version", FFMPEG_VERSION);
    print_fmt("copyright", "Copyright (c) %d-%d the FFmpeg developers",
              program_birth_year, this_year);
    print_str("build_date", __DATE__);
    print_str("build_time", __TIME__);
    print_str("compiler_ident", CC_IDENT);
    print_str("configuration", FFMPEG_CONFIGURATION);
    print_section_footer("program_version");
    writer_print_chapter_footer(w, "program_version");

    av_bprint_finalize(&pbuf, NULL);
}

#define SHOW_LIB_VERSION(libname, LIBNAME)                              \
    do {                                                                \
        if (CONFIG_##LIBNAME) {                                         \
            unsigned int version = libname##_version();                 \
            print_section_header("library_version");                    \
            print_str("name",    "lib" #libname);                       \
            print_int("major",   LIB##LIBNAME##_VERSION_MAJOR);         \
            print_int("minor",   LIB##LIBNAME##_VERSION_MINOR);         \
            print_int("micro",   LIB##LIBNAME##_VERSION_MICRO);         \
            print_int("version", version);                              \
            print_section_footer("library_version");                    \
        }                                                               \
    } while (0)

static void ffprobe_show_library_versions(WriterContext *w)
{
    writer_print_chapter_header(w, "library_versions");
    SHOW_LIB_VERSION(avutil,     AVUTIL);
    SHOW_LIB_VERSION(avcodec,    AVCODEC);
    SHOW_LIB_VERSION(avformat,   AVFORMAT);
    SHOW_LIB_VERSION(avdevice,   AVDEVICE);
    SHOW_LIB_VERSION(avfilter,   AVFILTER);
    SHOW_LIB_VERSION(swscale,    SWSCALE);
    SHOW_LIB_VERSION(swresample, SWRESAMPLE);
    SHOW_LIB_VERSION(postproc,   POSTPROC);
    writer_print_chapter_footer(w, "library_versions");
}

static int opt_format(void *optctx, const char *opt, const char *arg)
{
    iformat = av_find_input_format(arg);
    if (!iformat) {
        av_log(NULL, AV_LOG_ERROR, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_show_format_entry(void *optctx, const char *opt, const char *arg)
{
    do_show_format = 1;
    av_dict_set(&fmt_entries_to_show, arg, "", 0);
    return 0;
}

static void opt_input_file(void *optctx, const char *arg)
{
    if (input_filename) {
        av_log(NULL, AV_LOG_ERROR,
                "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        exit(1);
    }
    if (!strcmp(arg, "-"))
        arg = "pipe:";
    input_filename = arg;
}

static int opt_input_file_i(void *optctx, const char *opt, const char *arg)
{
    opt_input_file(optctx, arg);
    return 0;
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, 0, 0);
    printf("\n");

    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
}

static int opt_pretty(void *optctx, const char *opt, const char *arg)
{
    show_value_unit              = 1;
    use_value_prefix             = 1;
    use_byte_value_binary_prefix = 1;
    use_value_sexagesimal_format = 1;
    return 0;
}

static int opt_show_versions(const char *opt, const char *arg)
{
    do_show_program_version  = 1;
    do_show_library_versions = 1;
    return 0;
}

static const OptionDef real_options[] = {
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG, {.func_arg = opt_format}, "force format", "format" },
    { "unit", OPT_BOOL, {&show_value_unit}, "show unit of the displayed values" },
    { "prefix", OPT_BOOL, {&use_value_prefix}, "use SI prefixes for the displayed values" },
    { "byte_binary_prefix", OPT_BOOL, {&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal", OPT_BOOL,  {&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty", 0, {.func_arg = opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "print_format", OPT_STRING | HAS_ARG, {(void*)&print_format},
      "set the output printing format (available formats are: default, compact, csv, flat, ini, json, xml)", "format" },
    { "of", OPT_STRING | HAS_ARG, {(void*)&print_format}, "alias for -print_format", "format" },
    { "show_data",    OPT_BOOL, {(void*)&do_show_data}, "show packets data" },
    { "show_error",   OPT_BOOL, {(void*)&do_show_error} ,  "show probing error" },
    { "show_format",  OPT_BOOL, {&do_show_format} , "show format/container info" },
    { "show_frames",  OPT_BOOL, {(void*)&do_show_frames} , "show frames info" },
    { "show_format_entry", HAS_ARG, {.func_arg = opt_show_format_entry},
      "show a particular entry from the format/container info", "entry" },
    { "show_packets", OPT_BOOL, {&do_show_packets}, "show packets info" },
    { "show_streams", OPT_BOOL, {&do_show_streams}, "show streams info" },
    { "count_frames", OPT_BOOL, {(void*)&do_count_frames}, "count the number of frames per stream" },
    { "count_packets", OPT_BOOL, {(void*)&do_count_packets}, "count the number of packets per stream" },
    { "show_program_version",  OPT_BOOL, {(void*)&do_show_program_version},  "show ffprobe version" },
    { "show_library_versions", OPT_BOOL, {(void*)&do_show_library_versions}, "show library versions" },
    { "show_versions",         0, {(void*)&opt_show_versions}, "show program and library versions" },
    { "show_private_data", OPT_BOOL, {(void*)&show_private_data}, "show private data" },
    { "private",           OPT_BOOL, {(void*)&show_private_data}, "same as show_private_data" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_default}, "generic catch all option", "" },
    { "i", HAS_ARG, {.func_arg = opt_input_file_i}, "read specified file", "input_file"},
    { NULL, },
};

int main(int argc, char **argv)
{
    const Writer *w;
    WriterContext *wctx;
    char *buf;
    char *w_name = NULL, *w_args = NULL;
    int ret;

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    options = real_options;
    parse_loglevel(argc, argv, options);
    av_register_all();
    avformat_network_init();
    init_opts();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner(argc, argv, options);
    parse_options(NULL, argc, argv, options, opt_input_file);

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

    if ((ret = writer_open(&wctx, w, w_args, NULL)) >= 0) {
        writer_print_header(wctx);

        if (do_show_program_version)
            ffprobe_show_program_version(wctx);
        if (do_show_library_versions)
            ffprobe_show_library_versions(wctx);

        if (!input_filename &&
            ((do_show_format || do_show_streams || do_show_packets || do_show_error) ||
             (!do_show_program_version && !do_show_library_versions))) {
            show_usage();
            av_log(NULL, AV_LOG_ERROR, "You have to specify one input file.\n");
            av_log(NULL, AV_LOG_ERROR, "Use -h to get full help or, even better, run 'man %s'.\n", program_name);
            ret = AVERROR(EINVAL);
        } else if (input_filename) {
            ret = probe_file(wctx, input_filename);
            if (ret < 0 && do_show_error)
                show_error(wctx, ret);
        }

        writer_print_footer(wctx);
        writer_close(&wctx);
    }

end:
    av_freep(&print_format);

    uninit_opts();
    av_dict_free(&fmt_entries_to_show);

    avformat_network_deinit();

    return ret;
}
