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
static int do_show_program_version  = 0;
static int do_show_library_versions = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data            = 1;

static char *print_format;

static const OptionDef options[];

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
    } else {
        const char *prefix_string = "";
        int l;

        if (use_value_prefix && vald > 1) {
            long long int index;

            if (uv.unit == unit_byte_str && use_byte_value_binary_prefix) {
                index = (long long int) (log(vald)/log(2)) / 10;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) - 1);
                vald /= pow(2, index * 10);
                prefix_string = binary_unit_prefixes[index];
            } else {
                index = (long long int) (log10(vald)) / 3;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) - 1);
                vald /= pow(10, index * 3);
                prefix_string = decimal_unit_prefixes[index];
            }
        }

        if (show_float || (use_value_prefix && vald != (long long int)vald))
            l = snprintf(buf, buf_size, "%f", vald);
        else
            l = snprintf(buf, buf_size, "%lld", (long long int)vald);
        snprintf(buf+l, buf_size-l, "%s%s%s", *prefix_string || show_value_unit ? " " : "",
                 prefix_string, show_value_unit ? uv.unit : "");
    }

    return buf;
}

/* WRITERS API */

typedef struct WriterContext WriterContext;

#define WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS 1
#define WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER 2

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

    int is_fmt_chapter;             ///< tells if the current chapter is "format", required by the print_format_entry option
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
                                               const char *chapter)
{
    if (wctx->writer->print_chapter_header)
        wctx->writer->print_chapter_header(wctx, chapter);
    wctx->nb_section = 0;

    wctx->is_fmt_chapter = !strcmp(chapter, "format");
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
    if (wctx->writer->print_section_header)
        wctx->writer->print_section_header(wctx, section);
    wctx->nb_item = 0;
}

static inline void writer_print_section_footer(WriterContext *wctx,
                                               const char *section)
{
    if (wctx->writer->print_section_footer)
        wctx->writer->print_section_footer(wctx, section);
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
                              int64_t ts, const AVRational *time_base)
{
    char buf[128];

    if (!wctx->is_fmt_chapter || !fmt_entries_to_show || av_dict_get(fmt_entries_to_show, key, NULL, 0)) {
        if (ts == AV_NOPTS_VALUE) {
            writer_print_string(wctx, key, "N/A", 1);
        } else {
            double d = ts * av_q2d(*time_base);
            value_string(buf, sizeof(buf), (struct unit_value){.val.d=d, .unit=unit_second_str});
            writer_print_string(wctx, key, buf, 0);
        }
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


/* WRITERS */

/* Default output */

typedef struct DefaultContext {
    const AVClass *class;
    int nokey;
    int noprint_wrappers;
} DefaultContext;

#define OFFSET(x) offsetof(DefaultContext, x)

static const AVOption default_options[] = {
    { "noprint_wrappers", "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    { "nw",               "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    { "nokey",          "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    { "nk",             "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    {NULL},
};

static const char *default_get_name(void *ctx)
{
    return "default";
}

static const AVClass default_class = {
    "DefaultContext",
    default_get_name,
    default_options
};

static av_cold int default_init(WriterContext *wctx, const char *args, void *opaque)
{
    DefaultContext *def = wctx->priv;
    int err;

    def->class = &default_class;
    av_opt_set_defaults(def);

    if (args &&
        (err = (av_set_options_string(def, args, "=", ":"))) < 0) {
        av_log(wctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return err;
    }

    return 0;
}

static void default_print_footer(WriterContext *wctx)
{
    DefaultContext *def = wctx->priv;

    if (!def->noprint_wrappers)
        printf("\n");
}

static void default_print_chapter_header(WriterContext *wctx, const char *chapter)
{
    DefaultContext *def = wctx->priv;

    if (!def->noprint_wrappers && wctx->nb_chapter)
        printf("\n");
}

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

    if (wctx->nb_section)
        printf("\n");
    if (!def->noprint_wrappers)
        printf("[%s]\n", upcase_string(buf, sizeof(buf), section));
}

static void default_print_section_footer(WriterContext *wctx, const char *section)
{
    DefaultContext *def = wctx->priv;
    char buf[32];

    if (!def->noprint_wrappers)
        printf("[/%s]", upcase_string(buf, sizeof(buf), section));
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
    .init                  = default_init,
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
static const char *c_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*src) {
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
    char *escape_mode_str;
    const char * (*escape_str)(AVBPrint *dst, const char *src, const char sep, void *log_ctx);
} CompactContext;

#undef OFFSET
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

    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (wctx->nb_item) printf("%c", compact->item_sep);

        if (!compact->nokey) {
            av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
            printf("tag:%s=", compact->escape_str(&buf, tag->key, compact->item_sep, wctx));
            av_bprint_finalize(&buf, NULL);
        }

        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        printf("%s", compact->escape_str(&buf, tag->value, compact->item_sep, wctx));
        av_bprint_finalize(&buf, NULL);
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
    const AVClass *class;
    int multiple_entries; ///< tells if the given chapter requires multiple entries
    int print_packets_and_frames;
    int indent_level;
    int compact;
    const char *item_sep, *item_start_end;
} JSONContext;

#undef OFFSET
#define OFFSET(x) offsetof(JSONContext, x)

static const AVOption json_options[]= {
    { "compact", "enable compact output", OFFSET(compact), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    { "c",       "enable compact output", OFFSET(compact), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1 },
    { NULL }
};

static const char *json_get_name(void *ctx)
{
    return "json";
}

static const AVClass json_class = {
    "JSONContext",
    json_get_name,
    json_options
};

static av_cold int json_init(WriterContext *wctx, const char *args, void *opaque)
{
    JSONContext *json = wctx->priv;
    int err;

    json->class = &json_class;
    av_opt_set_defaults(json);

    if (args &&
        (err = (av_set_options_string(json, args, "=", ":"))) < 0) {
        av_log(wctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return err;
    }

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
    json->multiple_entries = !strcmp(chapter, "packets") || !strcmp(chapter, "frames" ) ||
                             !strcmp(chapter, "packets_and_frames") ||
                             !strcmp(chapter, "streams") || !strcmp(chapter, "library_versions");
    if (json->multiple_entries) {
        JSON_INDENT();
        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        printf("\"%s\": [\n", json_escape_str(&buf, chapter, wctx));
        av_bprint_finalize(&buf, NULL);
        json->print_packets_and_frames = !strcmp(chapter, "packets_and_frames");
        json->indent_level++;
    }
}

static void json_print_chapter_footer(WriterContext *wctx, const char *chapter)
{
    JSONContext *json = wctx->priv;

    if (json->multiple_entries) {
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
    if (!json->multiple_entries)
        printf("\"%s\": ", section);
    printf("{%s", json->item_start_end);
    json->indent_level++;
    /* this is required so the parser can distinguish between packets and frames */
    if (json->print_packets_and_frames) {
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
    av_bprint_finalize(&buf, NULL);

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
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
};

/* XML output */

typedef struct {
    const AVClass *class;
    int within_tag;
    int multiple_entries; ///< tells if the given chapter requires multiple entries
    int indent_level;
    int fully_qualified;
    int xsd_strict;
} XMLContext;

#undef OFFSET
#define OFFSET(x) offsetof(XMLContext, x)

static const AVOption xml_options[] = {
    {"fully_qualified", "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_INT, {.dbl=0},  0, 1 },
    {"q",               "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_INT, {.dbl=0},  0, 1 },
    {"xsd_strict",      "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_INT, {.dbl=0},  0, 1 },
    {"x",               "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_INT, {.dbl=0},  0, 1 },
    {NULL},
};

static const char *xml_get_name(void *ctx)
{
    return "xml";
}

static const AVClass xml_class = {
    "XMLContext",
    xml_get_name,
    xml_options
};

static av_cold int xml_init(WriterContext *wctx, const char *args, void *opaque)
{
    XMLContext *xml = wctx->priv;
    int err;

    xml->class = &xml_class;
    av_opt_set_defaults(xml);

    if (args &&
        (err = (av_set_options_string(xml, args, "=", ":"))) < 0) {
        av_log(wctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return err;
    }

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
    xml->multiple_entries = !strcmp(chapter, "packets") || !strcmp(chapter, "frames") ||
                            !strcmp(chapter, "packets_and_frames") ||
                            !strcmp(chapter, "streams") || !strcmp(chapter, "library_versions");

    if (xml->multiple_entries) {
        XML_INDENT(); printf("<%s>\n", chapter);
        xml->indent_level++;
    }
}

static void xml_print_chapter_footer(WriterContext *wctx, const char *chapter)
{
    XMLContext *xml = wctx->priv;

    if (xml->multiple_entries) {
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

    xml->indent_level++;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if (is_first) {
            /* close section tag */
            printf(">\n");
            xml->within_tag = 0;
            is_first = 0;
        }
        XML_INDENT();

        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        printf("<tag key=\"%s\"", xml_escape_str(&buf, tag->key, wctx));
        av_bprint_finalize(&buf, NULL);

        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        printf(" value=\"%s\"/>\n", xml_escape_str(&buf, tag->value, wctx));
        av_bprint_finalize(&buf, NULL);
    }
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
    writer_register(&xml_writer);
}

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    writer_print_string(w, k, pbuf.str, 0);    \
} while (0)

#define print_int(k, v)         writer_print_integer(w, k, v)
#define print_str(k, v)         writer_print_string(w, k, v, 0)
#define print_str_opt(k, v)     writer_print_string(w, k, v, 1)
#define print_time(k, v, tb)    writer_print_time(w, k, v, tb)
#define print_ts(k, v)          writer_print_ts(w, k, v)
#define print_val(k, v, u)      writer_print_string(w, k, \
    value_string(val_str, sizeof(val_str), (struct unit_value){.val.i = v, .unit=u}), 0)
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
    print_ts  ("duration",        pkt->duration);
    print_time("duration_time",   pkt->duration, &st->time_base);
    print_val("size",             pkt->size, unit_byte_str);
    if (pkt->pos != -1) print_fmt    ("pos", "%"PRId64, pkt->pos);
    else                print_str_opt("pos", "N/A");
    print_fmt("flags", "%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_');
    print_section_footer("packet");

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_frame(WriterContext *w, AVFrame *frame, AVStream *stream)
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
    if (frame->pkt_pos != -1) print_fmt    ("pkt_pos", "%"PRId64, frame->pkt_pos);
    else                      print_str_opt("pkt_pos", "N/A");

    switch (stream->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        print_int("width",                  frame->width);
        print_int("height",                 frame->height);
        s = av_get_pix_fmt_name(frame->format);
        if (s) print_str    ("pix_fmt", s);
        else   print_str_opt("pix_fmt", "unknown");
        if (frame->sample_aspect_ratio.num) {
            print_fmt("sample_aspect_ratio", "%d:%d",
                      frame->sample_aspect_ratio.num,
                      frame->sample_aspect_ratio.den);
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
        break;
    }

    print_section_footer("frame");

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static av_always_inline int get_decoded_frame(AVFormatContext *fmt_ctx,
                                              AVFrame *frame, int *got_frame,
                                              AVPacket *pkt)
{
    AVCodecContext *dec_ctx = fmt_ctx->streams[pkt->stream_index]->codec;
    int ret = 0;

    *got_frame = 0;
    switch (dec_ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        ret = avcodec_decode_video2(dec_ctx, frame, got_frame, pkt);
        break;

    case AVMEDIA_TYPE_AUDIO:
        ret = avcodec_decode_audio4(dec_ctx, frame, got_frame, pkt);
        break;
    }

    return ret;
}

static void read_packets(WriterContext *w, AVFormatContext *fmt_ctx)
{
    AVPacket pkt, pkt1;
    AVFrame frame;
    int i = 0, ret, got_frame;

    av_init_packet(&pkt);

    while (!av_read_frame(fmt_ctx, &pkt)) {
        if (do_read_packets) {
            if (do_show_packets)
                show_packet(w, fmt_ctx, &pkt, i++);
            nb_streams_packets[pkt.stream_index]++;
        }
        if (do_read_frames) {
            pkt1 = pkt;
            while (pkt1.size) {
                avcodec_get_frame_defaults(&frame);
                ret = get_decoded_frame(fmt_ctx, &frame, &got_frame, &pkt1);
                if (ret < 0 || !got_frame)
                    break;
                if (do_show_frames)
                    show_frame(w, &frame, fmt_ctx->streams[pkt.stream_index]);
                pkt1.data += ret;
                pkt1.size -= ret;
                nb_streams_frames[pkt.stream_index]++;
            }
        }
        av_free_packet(&pkt);
    }
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    //Flush remaining frames that are cached in the decoder
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        pkt.stream_index = i;
        while (get_decoded_frame(fmt_ctx, &frame, &got_frame, &pkt) >= 0 && got_frame) {
            if (do_read_frames) {
                if (do_show_frames)
                    show_frame(w, &frame, fmt_ctx->streams[pkt.stream_index]);
                nb_streams_frames[pkt.stream_index]++;
            }
        }
    }
}

static void show_stream(WriterContext *w, AVFormatContext *fmt_ctx, int stream_idx)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    AVCodec *dec;
    char val_str[128];
    const char *s;
    AVRational display_aspect_ratio;
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

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
            if (dec_ctx->timecode_frame_start >= 0) {
                char tcbuf[AV_TIMECODE_STR_SIZE];
                av_timecode_make_mpeg_tc_string(tcbuf, dec_ctx->timecode_frame_start);
                print_str("timecode", tcbuf);
            } else {
                print_str_opt("timecode", "N/A");
            }
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
    print_fmt("r_frame_rate",   "%d/%d", stream->r_frame_rate.num,   stream->r_frame_rate.den);
    print_fmt("avg_frame_rate", "%d/%d", stream->avg_frame_rate.num, stream->avg_frame_rate.den);
    print_fmt("time_base",      "%d/%d", stream->time_base.num,      stream->time_base.den);
    print_time("start_time",    stream->start_time, &stream->time_base);
    print_time("duration",      stream->duration,   &stream->time_base);
    if (dec_ctx->bit_rate > 0) print_val    ("bit_rate", dec_ctx->bit_rate, unit_bit_per_second_str);
    else                       print_str_opt("bit_rate", "N/A");
    if (stream->nb_frames) print_fmt    ("nb_frames", "%"PRId64, stream->nb_frames);
    else                   print_str_opt("nb_frames", "N/A");
    if (nb_streams_frames[stream_idx])  print_fmt    ("nb_read_frames", "%"PRIu64, nb_streams_frames[stream_idx]);
    else                                print_str_opt("nb_read_frames", "N/A");
    if (nb_streams_packets[stream_idx]) print_fmt    ("nb_read_packets", "%"PRIu64, nb_streams_packets[stream_idx]);
    else                                print_str_opt("nb_read_packets", "N/A");
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

        if (!(codec = avcodec_find_decoder(stream->codec->codec_id))) {
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
        if (fmt_ctx->streams[i]->codec->codec_id != CODEC_ID_NONE)
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
    print_str("compiler_type", CC_TYPE);
    print_str("compiler_version", CC_VERSION);
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

static int opt_format(const char *opt, const char *arg)
{
    iformat = av_find_input_format(arg);
    if (!iformat) {
        av_log(NULL, AV_LOG_ERROR, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_show_format_entry(const char *opt, const char *arg)
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

static int opt_show_versions(const char *opt, const char *arg)
{
    do_show_program_version  = 1;
    do_show_library_versions = 1;
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
      "set the output printing format (available formats are: default, compact, csv, json, xml)", "format" },
    { "show_error",   OPT_BOOL, {(void*)&do_show_error} ,  "show probing error" },
    { "show_format",  OPT_BOOL, {(void*)&do_show_format} , "show format/container info" },
    { "show_frames",  OPT_BOOL, {(void*)&do_show_frames} , "show frames info" },
    { "show_format_entry", HAS_ARG, {(void*)opt_show_format_entry},
      "show a particular entry from the format/container info", "entry" },
    { "show_packets", OPT_BOOL, {(void*)&do_show_packets}, "show packets info" },
    { "show_streams", OPT_BOOL, {(void*)&do_show_streams}, "show streams info" },
    { "count_frames", OPT_BOOL, {(void*)&do_count_frames}, "count the number of frames per stream" },
    { "count_packets", OPT_BOOL, {(void*)&do_count_packets}, "count the number of packets per stream" },
    { "show_program_version",  OPT_BOOL, {(void*)&do_show_program_version},  "show ffprobe version" },
    { "show_library_versions", OPT_BOOL, {(void*)&do_show_library_versions}, "show library versions" },
    { "show_versions",         0, {(void*)&opt_show_versions}, "show program and library versions" },
    { "show_private_data", OPT_BOOL, {(void*)&show_private_data}, "show private data" },
    { "private",           OPT_BOOL, {(void*)&show_private_data}, "same as show_private_data" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { "i", HAS_ARG, {(void *)opt_input_file}, "read specified file", "input_file"},
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
