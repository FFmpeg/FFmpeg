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

#include <string.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavutil/libm.h"
#include "libavutil/parseutils.h"
#include "libavutil/timecode.h"
#include "libavutil/timestamp.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libpostproc/postprocess.h"
#include "cmdutils.h"

const char program_name[] = "ffprobe";
const int program_birth_year = 2007;

static int do_bitexact = 0;
static int do_count_frames = 0;
static int do_count_packets = 0;
static int do_read_frames  = 0;
static int do_read_packets = 0;
static int do_show_chapters = 0;
static int do_show_error   = 0;
static int do_show_format  = 0;
static int do_show_frames  = 0;
static int do_show_packets = 0;
static int do_show_programs = 0;
static int do_show_streams = 0;
static int do_show_stream_disposition = 0;
static int do_show_data    = 0;
static int do_show_program_version  = 0;
static int do_show_library_versions = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data            = 1;

static char *print_format;
static char *stream_specifier;

typedef struct {
    int id;             ///< identifier
    int64_t start, end; ///< start, end in second/AV_TIME_BASE units
    int has_start, has_end;
    int start_is_offset, end_is_offset;
    int duration_frames;
} ReadInterval;

static ReadInterval *read_intervals;
static int read_intervals_nb = 0;

/* section structure definition */

#define SECTION_MAX_NB_CHILDREN 10

struct section {
    int id;             ///< unique id identifying a section
    const char *name;

#define SECTION_FLAG_IS_WRAPPER      1 ///< the section only contains other sections, but has no data at its own level
#define SECTION_FLAG_IS_ARRAY        2 ///< the section contains an array of elements of the same type
#define SECTION_FLAG_HAS_VARIABLE_FIELDS 4 ///< the section may contain a variable number of fields with variable keys.
                                           ///  For these sections the element_name field is mandatory.
    int flags;
    int children_ids[SECTION_MAX_NB_CHILDREN+1]; ///< list of children section IDS, terminated by -1
    const char *element_name; ///< name of the contained element, if provided
    const char *unique_name;  ///< unique section name, in case the name is ambiguous
    AVDictionary *entries_to_show;
    int show_all_entries;
};

typedef enum {
    SECTION_ID_NONE = -1,
    SECTION_ID_CHAPTER,
    SECTION_ID_CHAPTER_TAGS,
    SECTION_ID_CHAPTERS,
    SECTION_ID_ERROR,
    SECTION_ID_FORMAT,
    SECTION_ID_FORMAT_TAGS,
    SECTION_ID_FRAME,
    SECTION_ID_FRAMES,
    SECTION_ID_FRAME_TAGS,
    SECTION_ID_LIBRARY_VERSION,
    SECTION_ID_LIBRARY_VERSIONS,
    SECTION_ID_PACKET,
    SECTION_ID_PACKETS,
    SECTION_ID_PACKETS_AND_FRAMES,
    SECTION_ID_PROGRAM_STREAM_DISPOSITION,
    SECTION_ID_PROGRAM_STREAM_TAGS,
    SECTION_ID_PROGRAM,
    SECTION_ID_PROGRAM_STREAMS,
    SECTION_ID_PROGRAM_STREAM,
    SECTION_ID_PROGRAM_TAGS,
    SECTION_ID_PROGRAM_VERSION,
    SECTION_ID_PROGRAMS,
    SECTION_ID_ROOT,
    SECTION_ID_STREAM,
    SECTION_ID_STREAM_DISPOSITION,
    SECTION_ID_STREAMS,
    SECTION_ID_STREAM_TAGS,
} SectionID;

static struct section sections[] = {
    [SECTION_ID_CHAPTERS] =           { SECTION_ID_CHAPTERS, "chapters", SECTION_FLAG_IS_ARRAY, { SECTION_ID_CHAPTER, -1 } },
    [SECTION_ID_CHAPTER] =            { SECTION_ID_CHAPTER, "chapter", 0, { SECTION_ID_CHAPTER_TAGS, -1 } },
    [SECTION_ID_CHAPTER_TAGS] =       { SECTION_ID_CHAPTER_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "chapter_tags" },
    [SECTION_ID_ERROR] =              { SECTION_ID_ERROR, "error", 0, { -1 } },
    [SECTION_ID_FORMAT] =             { SECTION_ID_FORMAT, "format", 0, { SECTION_ID_FORMAT_TAGS, -1 } },
    [SECTION_ID_FORMAT_TAGS] =        { SECTION_ID_FORMAT_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "format_tags" },
    [SECTION_ID_FRAMES] =             { SECTION_ID_FRAMES, "frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME, -1 } },
    [SECTION_ID_FRAME] =              { SECTION_ID_FRAME, "frame", 0, { SECTION_ID_FRAME_TAGS, -1 } },
    [SECTION_ID_FRAME_TAGS] =         { SECTION_ID_FRAME_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "frame_tags" },
    [SECTION_ID_LIBRARY_VERSIONS] =   { SECTION_ID_LIBRARY_VERSIONS, "library_versions", SECTION_FLAG_IS_ARRAY, { SECTION_ID_LIBRARY_VERSION, -1 } },
    [SECTION_ID_LIBRARY_VERSION] =    { SECTION_ID_LIBRARY_VERSION, "library_version", 0, { -1 } },
    [SECTION_ID_PACKETS] =            { SECTION_ID_PACKETS, "packets", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKETS_AND_FRAMES] = { SECTION_ID_PACKETS_AND_FRAMES, "packets_and_frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKET] =             { SECTION_ID_PACKET, "packet", 0, { -1 } },
    [SECTION_ID_PROGRAM_STREAM_DISPOSITION] = { SECTION_ID_PROGRAM_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "program_stream_disposition" },
    [SECTION_ID_PROGRAM_STREAM_TAGS] =        { SECTION_ID_PROGRAM_STREAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_stream_tags" },
    [SECTION_ID_PROGRAM] =                    { SECTION_ID_PROGRAM, "program", 0, { SECTION_ID_PROGRAM_TAGS, SECTION_ID_PROGRAM_STREAMS, -1 } },
    [SECTION_ID_PROGRAM_STREAMS] =            { SECTION_ID_PROGRAM_STREAMS, "streams", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM_STREAM, -1 }, .unique_name = "program_streams" },
    [SECTION_ID_PROGRAM_STREAM] =             { SECTION_ID_PROGRAM_STREAM, "stream", 0, { SECTION_ID_PROGRAM_STREAM_DISPOSITION, SECTION_ID_PROGRAM_STREAM_TAGS, -1 }, .unique_name = "program_stream" },
    [SECTION_ID_PROGRAM_TAGS] =               { SECTION_ID_PROGRAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_tags" },
    [SECTION_ID_PROGRAM_VERSION] =    { SECTION_ID_PROGRAM_VERSION, "program_version", 0, { -1 } },
    [SECTION_ID_PROGRAMS] =                   { SECTION_ID_PROGRAMS, "programs", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM, -1 } },
    [SECTION_ID_ROOT] =               { SECTION_ID_ROOT, "root", SECTION_FLAG_IS_WRAPPER,
                                        { SECTION_ID_CHAPTERS, SECTION_ID_FORMAT, SECTION_ID_FRAMES, SECTION_ID_PROGRAMS, SECTION_ID_STREAMS,
                                          SECTION_ID_PACKETS, SECTION_ID_ERROR, SECTION_ID_PROGRAM_VERSION, SECTION_ID_LIBRARY_VERSIONS, -1} },
    [SECTION_ID_STREAMS] =            { SECTION_ID_STREAMS, "streams", SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM, -1 } },
    [SECTION_ID_STREAM] =             { SECTION_ID_STREAM, "stream", 0, { SECTION_ID_STREAM_DISPOSITION, SECTION_ID_STREAM_TAGS, -1 } },
    [SECTION_ID_STREAM_DISPOSITION] = { SECTION_ID_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_disposition" },
    [SECTION_ID_STREAM_TAGS] =        { SECTION_ID_STREAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_tags" },
};

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
static int *selected_streams;

static void ffprobe_cleanup(int ret)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));
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

    int  (*init)  (WriterContext *wctx);
    void (*uninit)(WriterContext *wctx);

    void (*print_section_header)(WriterContext *wctx);
    void (*print_section_footer)(WriterContext *wctx);
    void (*print_integer)       (WriterContext *wctx, const char *, long long int);
    void (*print_rational)      (WriterContext *wctx, AVRational *q, char *sep);
    void (*print_string)        (WriterContext *wctx, const char *, const char *);
    int flags;                  ///< a combination or WRITER_FLAG_*
} Writer;

#define SECTION_MAX_NB_LEVELS 10

struct WriterContext {
    const AVClass *class;           ///< class of the writer
    const Writer *writer;           ///< the Writer of which this is an instance
    char *name;                     ///< name of this writer instance
    void *priv;                     ///< private data for use by the filter

    const struct section *sections; ///< array containing all sections
    int nb_sections;                ///< number of sections

    int level;                      ///< current level, starting from 0

    /** number of the item printed in the given section, starting from 0 */
    unsigned int nb_item[SECTION_MAX_NB_LEVELS];

    /** section per each level */
    const struct section *section[SECTION_MAX_NB_LEVELS];
    AVBPrint section_pbuf[SECTION_MAX_NB_LEVELS]; ///< generic print buffer dedicated to each section,
                                                  ///  used by various writers

    unsigned int nb_section_packet; ///< number of the packet section in case we are in "packets_and_frames" section
    unsigned int nb_section_frame;  ///< number of the frame  section in case we are in "packets_and_frames" section
    unsigned int nb_section_packet_frame; ///< nb_section_packet or nb_section_frame according if is_packets_and_frames
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
    int i;

    if (!*wctx)
        return;

    if ((*wctx)->writer->uninit)
        (*wctx)->writer->uninit(*wctx);
    for (i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_finalize(&(*wctx)->section_pbuf[i], NULL);
    if ((*wctx)->writer->priv_class)
        av_opt_free((*wctx)->priv);
    av_freep(&((*wctx)->priv));
    av_freep(wctx);
}

static int writer_open(WriterContext **wctx, const Writer *writer, const char *args,
                       const struct section *sections, int nb_sections)
{
    int i, ret = 0;

    if (!(*wctx = av_mallocz(sizeof(WriterContext)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!((*wctx)->priv = av_mallocz(writer->priv_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    (*wctx)->class = &writer_class;
    (*wctx)->writer = writer;
    (*wctx)->level = -1;
    (*wctx)->sections = sections;
    (*wctx)->nb_sections = nb_sections;

    if (writer->priv_class) {
        void *priv_ctx = (*wctx)->priv;
        *((const AVClass **)priv_ctx) = writer->priv_class;
        av_opt_set_defaults(priv_ctx);

        if (args &&
            (ret = av_set_options_string(priv_ctx, args, "=", ":")) < 0)
            goto fail;
    }

    for (i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_init(&(*wctx)->section_pbuf[i], 1, AV_BPRINT_SIZE_UNLIMITED);

    if ((*wctx)->writer->init)
        ret = (*wctx)->writer->init(*wctx);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    writer_close(wctx);
    return ret;
}

static inline void writer_print_section_header(WriterContext *wctx,
                                               int section_id)
{
    int parent_section_id;
    wctx->level++;
    av_assert0(wctx->level < SECTION_MAX_NB_LEVELS);
    parent_section_id = wctx->level ?
        (wctx->section[wctx->level-1])->id : SECTION_ID_NONE;

    wctx->nb_item[wctx->level] = 0;
    wctx->section[wctx->level] = &wctx->sections[section_id];

    if (section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        wctx->nb_section_packet = wctx->nb_section_frame =
        wctx->nb_section_packet_frame = 0;
    } else if (parent_section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        wctx->nb_section_packet_frame = section_id == SECTION_ID_PACKET ?
            wctx->nb_section_packet : wctx->nb_section_frame;
    }

    if (wctx->writer->print_section_header)
        wctx->writer->print_section_header(wctx);
}

static inline void writer_print_section_footer(WriterContext *wctx)
{
    int section_id = wctx->section[wctx->level]->id;
    int parent_section_id = wctx->level ?
        wctx->section[wctx->level-1]->id : SECTION_ID_NONE;

    if (parent_section_id != SECTION_ID_NONE)
        wctx->nb_item[wctx->level-1]++;
    if (parent_section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        if (section_id == SECTION_ID_PACKET) wctx->nb_section_packet++;
        else                                     wctx->nb_section_frame++;
    }
    if (wctx->writer->print_section_footer)
        wctx->writer->print_section_footer(wctx);
    wctx->level--;
}

static inline void writer_print_integer(WriterContext *wctx,
                                        const char *key, long long int val)
{
    const struct section *section = wctx->section[wctx->level];

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        wctx->writer->print_integer(wctx, key, val);
        wctx->nb_item[wctx->level]++;
    }
}

static inline void writer_print_string(WriterContext *wctx,
                                       const char *key, const char *val, int opt)
{
    const struct section *section = wctx->section[wctx->level];

    if (opt && !(wctx->writer->flags & WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS))
        return;

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        wctx->writer->print_string(wctx, key, val);
        wctx->nb_item[wctx->level]++;
    }
}

static inline void writer_print_rational(WriterContext *wctx,
                                         const char *key, AVRational q, char sep)
{
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%d%c%d", q.num, sep, q.den);
    writer_print_string(wctx, key, buf.str, 0);
}

static void writer_print_time(WriterContext *wctx, const char *key,
                              int64_t ts, const AVRational *time_base, int is_duration)
{
    char buf[128];

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

static void writer_print_ts(WriterContext *wctx, const char *key, int64_t ts, int is_duration)
{
    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        writer_print_string(wctx, key, "N/A", 1);
    } else {
        writer_print_integer(wctx, key, ts);
    }
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
    int nested_section[SECTION_MAX_NB_LEVELS];
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

static void default_print_section_header(WriterContext *wctx)
{
    DefaultContext *def = wctx->priv;
    char buf[32];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    av_bprint_clear(&wctx->section_pbuf[wctx->level]);
    if (parent_section &&
        !(parent_section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY))) {
        def->nested_section[wctx->level] = 1;
        av_bprintf(&wctx->section_pbuf[wctx->level], "%s%s:",
                   wctx->section_pbuf[wctx->level-1].str,
                   upcase_string(buf, sizeof(buf),
                                 av_x_if_null(section->element_name, section->name)));
    }

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("[%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_section_footer(WriterContext *wctx)
{
    DefaultContext *def = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    char buf[32];

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("[/%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_str(WriterContext *wctx, const char *key, const char *value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%s\n", value);
}

static void default_print_int(WriterContext *wctx, const char *key, long long int value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%lld\n", value);
}

static const Writer default_writer = {
    .name                  = "default",
    .priv_size             = sizeof(DefaultContext),
    .print_section_header  = default_print_section_header,
    .print_section_footer  = default_print_section_footer,
    .print_integer         = default_print_int,
    .print_string          = default_print_str,
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
    char meta_chars[] = { sep, '"', '\n', '\r', '\0' };
    int needs_quoting = !!src[strcspn(src, meta_chars)];

    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);

    for (; *src; src++) {
        if (*src == '"')
            av_bprint_chars(dst, '"', 1);
        av_bprint_chars(dst, *src, 1);
    }
    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);
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
    int nested_section[SECTION_MAX_NB_LEVELS];
    int has_nested_elems[SECTION_MAX_NB_LEVELS];
    int terminate_line[SECTION_MAX_NB_LEVELS];
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

static av_cold int compact_init(WriterContext *wctx)
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

static void compact_print_section_header(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;
    compact->terminate_line[wctx->level] = 1;
    compact->has_nested_elems[wctx->level] = 0;

    av_bprint_clear(&wctx->section_pbuf[wctx->level]);
    if (!(section->flags & SECTION_FLAG_IS_ARRAY) && parent_section &&
        !(parent_section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY))) {
        compact->nested_section[wctx->level] = 1;
        compact->has_nested_elems[wctx->level-1] = 1;
        av_bprintf(&wctx->section_pbuf[wctx->level], "%s%s:",
                   wctx->section_pbuf[wctx->level-1].str,
                   (char *)av_x_if_null(section->element_name, section->name));
        wctx->nb_item[wctx->level] = wctx->nb_item[wctx->level-1];
    } else {
        if (parent_section && compact->has_nested_elems[wctx->level-1] &&
            (section->flags & SECTION_FLAG_IS_ARRAY)) {
            compact->terminate_line[wctx->level-1] = 0;
            printf("\n");
        }
        if (compact->print_section &&
            !(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
            printf("%s%c", section->name, compact->item_sep);
    }
}

static void compact_print_section_footer(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;

    if (!compact->nested_section[wctx->level] &&
        compact->terminate_line[wctx->level] &&
        !(wctx->section[wctx->level]->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("\n");
}

static void compact_print_str(WriterContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level]) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s", compact->escape_str(&buf, value, compact->item_sep, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void compact_print_int(WriterContext *wctx, const char *key, long long int value)
{
    CompactContext *compact = wctx->priv;

    if (wctx->nb_item[wctx->level]) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%lld", value);
}

static const Writer compact_writer = {
    .name                 = "compact",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
    .priv_class           = &compact_class,
};

/* CSV output */

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption csv_options[] = {
    {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  CHAR_MIN, CHAR_MAX },
    {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  CHAR_MIN, CHAR_MAX },
    {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, CHAR_MIN, CHAR_MAX },
    {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, CHAR_MIN, CHAR_MAX },
    {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_INT,    {.i64=1},    0,        1        },
    {NULL},
};

DEFINE_WRITER_CLASS(csv);

static const Writer csv_writer = {
    .name                 = "csv",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
    .priv_class           = &csv_class,
};

/* Flat output */

typedef struct FlatContext {
    const AVClass *class;
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

static av_cold int flat_init(WriterContext *wctx)
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

static void flat_print_section_header(WriterContext *wctx)
{
    FlatContext *flat = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    /* build section header */
    av_bprint_clear(buf);
    if (!parent_section)
        return;
    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level-1].str);

    if (flat->hierarchical ||
        !(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", wctx->section[wctx->level]->name, flat->sep_str);

        if (parent_section->flags & SECTION_FLAG_IS_ARRAY) {
            int n = parent_section->id == SECTION_ID_PACKETS_AND_FRAMES ?
                wctx->nb_section_packet_frame : wctx->nb_item[wctx->level-1];
            av_bprintf(buf, "%d%s", n, flat->sep_str);
        }
    }
}

static void flat_print_int(WriterContext *wctx, const char *key, long long int value)
{
    printf("%s%s=%lld\n", wctx->section_pbuf[wctx->level].str, key, value);
}

static void flat_print_str(WriterContext *wctx, const char *key, const char *value)
{
    FlatContext *flat = wctx->priv;
    AVBPrint buf;

    printf("%s", wctx->section_pbuf[wctx->level].str);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=", flat_escape_key_str(&buf, key, flat->sep));
    av_bprint_clear(&buf);
    printf("\"%s\"\n", flat_escape_value_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static const Writer flat_writer = {
    .name                  = "flat",
    .priv_size             = sizeof(FlatContext),
    .init                  = flat_init,
    .print_section_header  = flat_print_section_header,
    .print_integer         = flat_print_int,
    .print_string          = flat_print_str,
    .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS|WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
    .priv_class            = &flat_class,
};

/* INI format output */

typedef struct {
    const AVClass *class;
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

static void ini_print_section_header(WriterContext *wctx)
{
    INIContext *ini = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    av_bprint_clear(buf);
    if (!parent_section) {
        printf("# ffprobe output\n\n");
        return;
    }

    if (wctx->nb_item[wctx->level-1])
        printf("\n");

    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level-1].str);
    if (ini->hierarchical ||
        !(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", buf->str[0] ? "." : "", wctx->section[wctx->level]->name);

        if (parent_section->flags & SECTION_FLAG_IS_ARRAY) {
            int n = parent_section->id == SECTION_ID_PACKETS_AND_FRAMES ?
                wctx->nb_section_packet_frame : wctx->nb_item[wctx->level-1];
            av_bprintf(buf, ".%d", n);
        }
    }

    if (!(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER)))
        printf("[%s]\n", buf->str);
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

static const Writer ini_writer = {
    .name                  = "ini",
    .priv_size             = sizeof(INIContext),
    .print_section_header  = ini_print_section_header,
    .print_integer         = ini_print_int,
    .print_string          = ini_print_str,
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

static av_cold int json_init(WriterContext *wctx)
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

#define JSON_INDENT() printf("%*c", json->indent_level * 4, ' ')

static void json_print_section_header(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->level && wctx->nb_item[wctx->level-1])
        printf(",\n");

    if (section->flags & SECTION_FLAG_IS_WRAPPER) {
        printf("{\n");
        json->indent_level++;
    } else {
        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        json_escape_str(&buf, section->name, wctx);
        JSON_INDENT();

        json->indent_level++;
        if (section->flags & SECTION_FLAG_IS_ARRAY) {
            printf("\"%s\": [\n", buf.str);
        } else if (parent_section && !(parent_section->flags & SECTION_FLAG_IS_ARRAY)) {
            printf("\"%s\": {%s", buf.str, json->item_start_end);
        } else {
            printf("{%s", json->item_start_end);

            /* this is required so the parser can distinguish between packets and frames */
            if (parent_section && parent_section->id == SECTION_ID_PACKETS_AND_FRAMES) {
                if (!json->compact)
                    JSON_INDENT();
                printf("\"type\": \"%s\"%s", section->name, json->item_sep);
            }
        }
        av_bprint_finalize(&buf, NULL);
    }
}

static void json_print_section_footer(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    if (wctx->level == 0) {
        json->indent_level--;
        printf("\n}\n");
    } else if (section->flags & SECTION_FLAG_IS_ARRAY) {
        printf("\n");
        json->indent_level--;
        JSON_INDENT();
        printf("]");
    } else {
        printf("%s", json->item_start_end);
        json->indent_level--;
        if (!json->compact)
            JSON_INDENT();
        printf("}");
    }
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

    if (wctx->nb_item[wctx->level])
        printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    json_print_item_str(wctx, key, value);
}

static void json_print_int(WriterContext *wctx, const char *key, long long int value)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level])
        printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("\"%s\": %lld", json_escape_str(&buf, key, wctx), value);
    av_bprint_finalize(&buf, NULL);
}

static const Writer json_writer = {
    .name                 = "json",
    .priv_size            = sizeof(JSONContext),
    .init                 = json_init,
    .print_section_header = json_print_section_header,
    .print_section_footer = json_print_section_footer,
    .print_integer        = json_print_int,
    .print_string         = json_print_str,
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

static av_cold int xml_init(WriterContext *wctx)
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
        case '"' : av_bprintf(dst, "%s", "&quot;"); break;
        case '\'': av_bprintf(dst, "%s", "&apos;"); break;
        default: av_bprint_chars(dst, *p, 1);
        }
    }

    return dst->str;
}

#define XML_INDENT() printf("%*c", xml->indent_level * 4, ' ')

static void xml_print_section_header(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->level == 0) {
        const char *qual = " xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
            "xmlns:ffprobe='http://www.ffmpeg.org/schema/ffprobe' "
            "xsi:schemaLocation='http://www.ffmpeg.org/schema/ffprobe ffprobe.xsd'";

        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        printf("<%sffprobe%s>\n",
               xml->fully_qualified ? "ffprobe:" : "",
               xml->fully_qualified ? qual : "");
        return;
    }

    if (xml->within_tag) {
        xml->within_tag = 0;
        printf(">\n");
    }
    if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        xml->indent_level++;
    } else {
        if (parent_section && (parent_section->flags & SECTION_FLAG_IS_WRAPPER) &&
            wctx->level && wctx->nb_item[wctx->level-1])
            printf("\n");
        xml->indent_level++;

        if (section->flags & SECTION_FLAG_IS_ARRAY) {
            XML_INDENT(); printf("<%s>\n", section->name);
        } else {
            XML_INDENT(); printf("<%s ", section->name);
            xml->within_tag = 1;
        }
    }
}

static void xml_print_section_footer(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    if (wctx->level == 0) {
        printf("</%sffprobe>\n", xml->fully_qualified ? "ffprobe:" : "");
    } else if (xml->within_tag) {
        xml->within_tag = 0;
        printf("/>\n");
        xml->indent_level--;
    } else if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        xml->indent_level--;
    } else {
        XML_INDENT(); printf("</%s>\n", section->name);
        xml->indent_level--;
    }
}

static void xml_print_str(WriterContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);

    if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        XML_INDENT();
        printf("<%s key=\"%s\"",
               section->element_name, xml_escape_str(&buf, key, wctx));
        av_bprint_clear(&buf);
        printf(" value=\"%s\"/>\n", xml_escape_str(&buf, value, wctx));
    } else {
        if (wctx->nb_item[wctx->level])
            printf(" ");
        printf("%s=\"%s\"", key, xml_escape_str(&buf, value, wctx));
    }

    av_bprint_finalize(&buf, NULL);
}

static void xml_print_int(WriterContext *wctx, const char *key, long long int value)
{
    if (wctx->nb_item[wctx->level])
        printf(" ");
    printf("%s=\"%lld\"", key, value);
}

static Writer xml_writer = {
    .name                 = "xml",
    .priv_size            = sizeof(XMLContext),
    .init                 = xml_init,
    .print_section_header = xml_print_section_header,
    .print_section_footer = xml_print_section_footer,
    .print_integer        = xml_print_int,
    .print_string         = xml_print_str,
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

static inline void show_tags(WriterContext *wctx, AVDictionary *tags, int section_id)
{
    AVDictionaryEntry *tag = NULL;

    if (!tags)
        return;
    writer_print_section_header(wctx, section_id);
    while ((tag = av_dict_get(tags, "", tag, AV_DICT_IGNORE_SUFFIX)))
        writer_print_string(wctx, tag->key, tag->value, 0);
    writer_print_section_footer(wctx);
}

static void show_packet(WriterContext *w, AVFormatContext *fmt_ctx, AVPacket *pkt, int packet_idx)
{
    char val_str[128];
    AVStream *st = fmt_ctx->streams[pkt->stream_index];
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, SECTION_ID_PACKET);

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
    writer_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_frame(WriterContext *w, AVFrame *frame, AVStream *stream,
                       AVFormatContext *fmt_ctx)
{
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, SECTION_ID_FRAME);

    s = av_get_media_type_string(stream->codec->codec_type);
    if (s) print_str    ("media_type", s);
    else   print_str_opt("media_type", "unknown");
    print_int("key_frame",              frame->key_frame);
    print_ts  ("pkt_pts",               frame->pkt_pts);
    print_time("pkt_pts_time",          frame->pkt_pts, &stream->time_base);
    print_ts  ("pkt_dts",               frame->pkt_dts);
    print_time("pkt_dts_time",          frame->pkt_dts, &stream->time_base);
    print_duration_ts  ("pkt_duration",      av_frame_get_pkt_duration(frame));
    print_duration_time("pkt_duration_time", av_frame_get_pkt_duration(frame), &stream->time_base);
    if (av_frame_get_pkt_pos (frame) != -1) print_fmt    ("pkt_pos", "%"PRId64, av_frame_get_pkt_pos(frame));
    else                      print_str_opt("pkt_pos", "N/A");
    if (av_frame_get_pkt_size(frame) != -1) print_fmt    ("pkt_size", "%d", av_frame_get_pkt_size(frame));
    else                       print_str_opt("pkt_size", "N/A");

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
    show_tags(w, av_frame_get_metadata(frame), SECTION_ID_FRAME_TAGS);

    writer_print_section_footer(w);

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

static void log_read_interval(const ReadInterval *interval, void *log_ctx, int log_level)
{
    av_log(log_ctx, log_level, "id:%d", interval->id);

    if (interval->has_start) {
        av_log(log_ctx, log_level, " start:%s%s", interval->start_is_offset ? "+" : "",
               av_ts2timestr(interval->start, &AV_TIME_BASE_Q));
    } else {
        av_log(log_ctx, log_level, " start:N/A");
    }

    if (interval->has_end) {
        av_log(log_ctx, log_level, " end:%s", interval->end_is_offset ? "+" : "");
        if (interval->duration_frames)
            av_log(log_ctx, log_level, "#%"PRId64, interval->end);
        else
            av_log(log_ctx, log_level, "%s", av_ts2timestr(interval->end, &AV_TIME_BASE_Q));
    } else {
        av_log(log_ctx, log_level, " end:N/A");
    }

    av_log(log_ctx, log_level, "\n");
}

static int read_interval_packets(WriterContext *w, AVFormatContext *fmt_ctx,
                                 const ReadInterval *interval, int64_t *cur_ts)
{
    AVPacket pkt, pkt1;
    AVFrame frame;
    int ret = 0, i = 0, frame_count = 0;
    int64_t start = -INT64_MAX, end = interval->end;
    int has_start = 0, has_end = interval->has_end && !interval->end_is_offset;

    av_init_packet(&pkt);

    av_log(NULL, AV_LOG_VERBOSE, "Processing read interval ");
    log_read_interval(interval, NULL, AV_LOG_VERBOSE);

    if (interval->has_start) {
        int64_t target;
        if (interval->start_is_offset) {
            if (*cur_ts == AV_NOPTS_VALUE) {
                av_log(NULL, AV_LOG_ERROR,
                       "Could not seek to relative position since current "
                       "timestamp is not defined\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            target = *cur_ts + interval->start;
        } else {
            target = interval->start;
        }

        av_log(NULL, AV_LOG_VERBOSE, "Seeking to read interval start point %s\n",
               av_ts2timestr(target, &AV_TIME_BASE_Q));
        if ((ret = avformat_seek_file(fmt_ctx, -1, -INT64_MAX, target, INT64_MAX, 0)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not seek to position %"PRId64": %s\n",
                   interval->start, av_err2str(ret));
            goto end;
        }
    }

    while (!av_read_frame(fmt_ctx, &pkt)) {
        if (selected_streams[pkt.stream_index]) {
            AVRational tb = fmt_ctx->streams[pkt.stream_index]->time_base;

            if (pkt.pts != AV_NOPTS_VALUE)
                *cur_ts = av_rescale_q(pkt.pts, tb, AV_TIME_BASE_Q);

            if (!has_start && *cur_ts != AV_NOPTS_VALUE) {
                start = *cur_ts;
                has_start = 1;
            }

            if (has_start && !has_end && interval->end_is_offset) {
                end = start + interval->end;
                has_end = 1;
            }

            if (interval->end_is_offset && interval->duration_frames) {
                if (frame_count >= interval->end)
                    break;
            } else if (has_end && *cur_ts != AV_NOPTS_VALUE && *cur_ts >= end) {
                break;
            }

            frame_count++;
            if (do_read_packets) {
                if (do_show_packets)
                    show_packet(w, fmt_ctx, &pkt, i++);
                nb_streams_packets[pkt.stream_index]++;
            }
            if (do_read_frames) {
                pkt1 = pkt;
                while (pkt1.size && process_frame(w, fmt_ctx, &frame, &pkt1) > 0);
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
        if (do_read_frames)
            while (process_frame(w, fmt_ctx, &frame, &pkt) > 0);
    }

end:
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not read packets in interval ");
        log_read_interval(interval, NULL, AV_LOG_ERROR);
    }
    return ret;
}

static void read_packets(WriterContext *w, AVFormatContext *fmt_ctx)
{
    int i, ret = 0;
    int64_t cur_ts = fmt_ctx->start_time;

    if (read_intervals_nb == 0) {
        ReadInterval interval = (ReadInterval) { .has_start = 0, .has_end = 0 };
        ret = read_interval_packets(w, fmt_ctx, &interval, &cur_ts);
    } else {
        for (i = 0; i < read_intervals_nb; i++) {
            ret = read_interval_packets(w, fmt_ctx, &read_intervals[i], &cur_ts);
            if (ret < 0)
                break;
        }
    }
}

static void show_stream(WriterContext *w, AVFormatContext *fmt_ctx, int stream_idx, int in_program)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    const AVCodec *dec;
    char val_str[128];
    const char *s;
    AVRational sar, dar;
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, in_program ? SECTION_ID_PROGRAM_STREAM : SECTION_ID_STREAM);

    print_int("index", stream->index);

    if ((dec_ctx = stream->codec)) {
        const char *profile = NULL;
        dec = dec_ctx->codec;
        if (dec) {
            print_str("codec_name", dec->name);
            if (!do_bitexact) {
                if (dec->long_name) print_str    ("codec_long_name", dec->long_name);
                else                print_str_opt("codec_long_name", "unknown");
            }
        } else {
            print_str_opt("codec_name", "unknown");
            if (!do_bitexact) {
                print_str_opt("codec_long_name", "unknown");
            }
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
            break;

        case AVMEDIA_TYPE_AUDIO:
            s = av_get_sample_fmt_name(dec_ctx->sample_fmt);
            if (s) print_str    ("sample_fmt", s);
            else   print_str_opt("sample_fmt", "unknown");
            print_val("sample_rate",     dec_ctx->sample_rate, unit_hertz_str);
            print_int("channels",        dec_ctx->channels);
            print_int("bits_per_sample", av_get_bits_per_sample(dec_ctx->codec_id));
            break;

        case AVMEDIA_TYPE_SUBTITLE:
            if (dec_ctx->width)
                print_int("width",       dec_ctx->width);
            else
                print_str_opt("width",   "N/A");
            if (dec_ctx->height)
                print_int("height",      dec_ctx->height);
            else
                print_str_opt("height",  "N/A");
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

    /* Print disposition information */
#define PRINT_DISPOSITION(flagname, name) do {                                \
        print_int(name, !!(stream->disposition & AV_DISPOSITION_##flagname)); \
    } while (0)

    if (do_show_stream_disposition) {
    writer_print_section_header(w, in_program ? SECTION_ID_PROGRAM_STREAM_DISPOSITION : SECTION_ID_STREAM_DISPOSITION);
    PRINT_DISPOSITION(DEFAULT,          "default");
    PRINT_DISPOSITION(DUB,              "dub");
    PRINT_DISPOSITION(ORIGINAL,         "original");
    PRINT_DISPOSITION(COMMENT,          "comment");
    PRINT_DISPOSITION(LYRICS,           "lyrics");
    PRINT_DISPOSITION(KARAOKE,          "karaoke");
    PRINT_DISPOSITION(FORCED,           "forced");
    PRINT_DISPOSITION(HEARING_IMPAIRED, "hearing_impaired");
    PRINT_DISPOSITION(VISUAL_IMPAIRED,  "visual_impaired");
    PRINT_DISPOSITION(CLEAN_EFFECTS,    "clean_effects");
    PRINT_DISPOSITION(ATTACHED_PIC,     "attached_pic");
    writer_print_section_footer(w);
    }

    show_tags(w, stream->metadata, in_program ? SECTION_ID_PROGRAM_STREAM_TAGS : SECTION_ID_STREAM_TAGS);

    writer_print_section_footer(w);
    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_streams(WriterContext *w, AVFormatContext *fmt_ctx)
{
    int i;
    writer_print_section_header(w, SECTION_ID_STREAMS);
    for (i = 0; i < fmt_ctx->nb_streams; i++)
        if (selected_streams[i])
            show_stream(w, fmt_ctx, i, 0);
    writer_print_section_footer(w);
}

static void show_program(WriterContext *w, AVFormatContext *fmt_ctx, AVProgram *program)
{
    int i;

    writer_print_section_header(w, SECTION_ID_PROGRAM);
    print_int("program_id", program->id);
    print_int("program_num", program->program_num);
    print_int("nb_streams", program->nb_stream_indexes);
    print_int("pmt_pid", program->pmt_pid);
    print_int("pcr_pid", program->pcr_pid);
    print_ts("start_pts", program->start_time);
    print_time("start_time", program->start_time, &AV_TIME_BASE_Q);
    print_ts("end_pts", program->end_time);
    print_time("end_time", program->end_time, &AV_TIME_BASE_Q);
    show_tags(w, program->metadata, SECTION_ID_PROGRAM_TAGS);

    writer_print_section_header(w, SECTION_ID_PROGRAM_STREAMS);
    for (i = 0; i < program->nb_stream_indexes; i++) {
        if (selected_streams[program->stream_index[i]])
            show_stream(w, fmt_ctx, program->stream_index[i], 1);
    }
    writer_print_section_footer(w);

    writer_print_section_footer(w);
}

static void show_programs(WriterContext *w, AVFormatContext *fmt_ctx)
{
    int i;

    writer_print_section_header(w, SECTION_ID_PROGRAMS);
    for (i = 0; i < fmt_ctx->nb_programs; i++) {
        AVProgram *program = fmt_ctx->programs[i];
        if (!program)
            continue;
        show_program(w, fmt_ctx, program);
    }
    writer_print_section_footer(w);
}

static void show_chapters(WriterContext *w, AVFormatContext *fmt_ctx)
{
    int i;

    writer_print_section_header(w, SECTION_ID_CHAPTERS);
    for (i = 0; i < fmt_ctx->nb_chapters; i++) {
        AVChapter *chapter = fmt_ctx->chapters[i];

        writer_print_section_header(w, SECTION_ID_CHAPTER);
        print_int("id", chapter->id);
        print_q  ("time_base", chapter->time_base, '/');
        print_int("start", chapter->start);
        print_time("start_time", chapter->start, &chapter->time_base);
        print_int("end", chapter->end);
        print_time("end_time", chapter->end, &chapter->time_base);
        show_tags(w, chapter->metadata, SECTION_ID_CHAPTER_TAGS);
        writer_print_section_footer(w);
    }
    writer_print_section_footer(w);
}

static void show_format(WriterContext *w, AVFormatContext *fmt_ctx)
{
    char val_str[128];
    int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;

    writer_print_section_header(w, SECTION_ID_FORMAT);
    print_str("filename",         fmt_ctx->filename);
    print_int("nb_streams",       fmt_ctx->nb_streams);
    print_int("nb_programs",      fmt_ctx->nb_programs);
    print_str("format_name",      fmt_ctx->iformat->name);
    if (!do_bitexact) {
        if (fmt_ctx->iformat->long_name) print_str    ("format_long_name", fmt_ctx->iformat->long_name);
        else                             print_str_opt("format_long_name", "unknown");
    }
    print_time("start_time",      fmt_ctx->start_time, &AV_TIME_BASE_Q);
    print_time("duration",        fmt_ctx->duration,   &AV_TIME_BASE_Q);
    if (size >= 0) print_val    ("size", size, unit_byte_str);
    else           print_str_opt("size", "N/A");
    if (fmt_ctx->bit_rate > 0) print_val    ("bit_rate", fmt_ctx->bit_rate, unit_bit_per_second_str);
    else                       print_str_opt("bit_rate", "N/A");
    print_int("probe_score", av_format_get_probe_score(fmt_ctx));
    show_tags(w, fmt_ctx->metadata, SECTION_ID_FORMAT_TAGS);

    writer_print_section_footer(w);
    fflush(stdout);
}

static void show_error(WriterContext *w, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));

    writer_print_section_header(w, SECTION_ID_ERROR);
    print_int("code", err);
    print_str("string", errbuf_ptr);
    writer_print_section_footer(w);
}

static int open_input_file(AVFormatContext **fmt_ctx_ptr, const char *filename)
{
    int err, i, orig_nb_streams;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;
    AVDictionary **opts;

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
    opts = setup_find_stream_info_opts(fmt_ctx, codec_opts);
    orig_nb_streams = fmt_ctx->nb_streams;

    if ((err = avformat_find_stream_info(fmt_ctx, opts)) < 0) {
        print_error(filename, err);
        return err;
    }
    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);

    av_dump_format(fmt_ctx, 0, filename, 0);

    /* bind a decoder to each input stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *stream = fmt_ctx->streams[i];
        AVCodec *codec;

        if (stream->codec->codec_id == AV_CODEC_ID_PROBE) {
            av_log(NULL, AV_LOG_WARNING,
                   "Failed to probe codec for input stream %d\n",
                    stream->index);
        } else if (!(codec = avcodec_find_decoder(stream->codec->codec_id))) {
            av_log(NULL, AV_LOG_WARNING,
                    "Unsupported codec with id %d for input stream %d\n",
                    stream->codec->codec_id, stream->index);
        } else {
            AVDictionary *opts = filter_codec_opts(codec_opts, stream->codec->codec_id,
                                                   fmt_ctx, stream, codec);
            if (avcodec_open2(stream->codec, codec, &opts) < 0) {
                av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n",
                       stream->index);
            }
            if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
                av_log(NULL, AV_LOG_ERROR, "Option %s for input stream %d not found\n",
                       t->key, stream->index);
                return AVERROR_OPTION_NOT_FOUND;
            }
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

static int probe_file(WriterContext *wctx, const char *filename)
{
    AVFormatContext *fmt_ctx;
    int ret, i;
    int section_id;

    do_read_frames = do_show_frames || do_count_frames;
    do_read_packets = do_show_packets || do_count_packets;

    ret = open_input_file(&fmt_ctx, filename);
    if (ret < 0)
        return ret;

    nb_streams_frames  = av_calloc(fmt_ctx->nb_streams, sizeof(*nb_streams_frames));
    nb_streams_packets = av_calloc(fmt_ctx->nb_streams, sizeof(*nb_streams_packets));
    selected_streams   = av_calloc(fmt_ctx->nb_streams, sizeof(*selected_streams));

    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        if (stream_specifier) {
            ret = avformat_match_stream_specifier(fmt_ctx,
                                                  fmt_ctx->streams[i],
                                                  stream_specifier);
            if (ret < 0)
                goto end;
            else
                selected_streams[i] = ret;
            ret = 0;
        } else {
            selected_streams[i] = 1;
        }
    }

    if (do_read_frames || do_read_packets) {
        if (do_show_frames && do_show_packets &&
            wctx->writer->flags & WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER)
            section_id = SECTION_ID_PACKETS_AND_FRAMES;
        else if (do_show_packets && !do_show_frames)
            section_id = SECTION_ID_PACKETS;
        else // (!do_show_packets && do_show_frames)
            section_id = SECTION_ID_FRAMES;
        if (do_show_frames || do_show_packets)
            writer_print_section_header(wctx, section_id);
        read_packets(wctx, fmt_ctx);
        if (do_show_frames || do_show_packets)
            writer_print_section_footer(wctx);
    }
    if (do_show_programs)
        show_programs(wctx, fmt_ctx);
    if (do_show_streams)
        show_streams(wctx, fmt_ctx);
    if (do_show_chapters)
        show_chapters(wctx, fmt_ctx);
    if (do_show_format)
        show_format(wctx, fmt_ctx);

end:
    close_input_file(&fmt_ctx);
    av_freep(&nb_streams_frames);
    av_freep(&nb_streams_packets);
    av_freep(&selected_streams);

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

    writer_print_section_header(w, SECTION_ID_PROGRAM_VERSION);
    print_str("version", FFMPEG_VERSION);
    print_fmt("copyright", "Copyright (c) %d-%d the FFmpeg developers",
              program_birth_year, this_year);
    print_str("build_date", __DATE__);
    print_str("build_time", __TIME__);
    print_str("compiler_ident", CC_IDENT);
    print_str("configuration", FFMPEG_CONFIGURATION);
    writer_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
}

#define SHOW_LIB_VERSION(libname, LIBNAME)                              \
    do {                                                                \
        if (CONFIG_##LIBNAME) {                                         \
            unsigned int version = libname##_version();                 \
            writer_print_section_header(w, SECTION_ID_LIBRARY_VERSION); \
            print_str("name",    "lib" #libname);                       \
            print_int("major",   LIB##LIBNAME##_VERSION_MAJOR);         \
            print_int("minor",   LIB##LIBNAME##_VERSION_MINOR);         \
            print_int("micro",   LIB##LIBNAME##_VERSION_MICRO);         \
            print_int("version", version);                              \
            print_str("ident",   LIB##LIBNAME##_IDENT);                 \
            writer_print_section_footer(w);                             \
        }                                                               \
    } while (0)

static void ffprobe_show_library_versions(WriterContext *w)
{
    writer_print_section_header(w, SECTION_ID_LIBRARY_VERSIONS);
    SHOW_LIB_VERSION(avutil,     AVUTIL);
    SHOW_LIB_VERSION(avcodec,    AVCODEC);
    SHOW_LIB_VERSION(avformat,   AVFORMAT);
    SHOW_LIB_VERSION(avdevice,   AVDEVICE);
    SHOW_LIB_VERSION(avfilter,   AVFILTER);
    SHOW_LIB_VERSION(swscale,    SWSCALE);
    SHOW_LIB_VERSION(swresample, SWRESAMPLE);
    SHOW_LIB_VERSION(postproc,   POSTPROC);
    writer_print_section_footer(w);
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

static inline void mark_section_show_entries(SectionID section_id,
                                             int show_all_entries, AVDictionary *entries)
{
    struct section *section = &sections[section_id];

    section->show_all_entries = show_all_entries;
    if (show_all_entries) {
        SectionID *id;
        for (id = section->children_ids; *id != -1; id++)
            mark_section_show_entries(*id, show_all_entries, entries);
    } else {
        av_dict_copy(&section->entries_to_show, entries, 0);
    }
}

static int match_section(const char *section_name,
                         int show_all_entries, AVDictionary *entries)
{
    int i, ret = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++) {
        const struct section *section = &sections[i];
        if (!strcmp(section_name, section->name) ||
            (section->unique_name && !strcmp(section_name, section->unique_name))) {
            av_log(NULL, AV_LOG_DEBUG,
                   "'%s' matches section with unique name '%s'\n", section_name,
                   (char *)av_x_if_null(section->unique_name, section->name));
            ret++;
            mark_section_show_entries(section->id, show_all_entries, entries);
        }
    }
    return ret;
}

static int opt_show_entries(void *optctx, const char *opt, const char *arg)
{
    const char *p = arg;
    int ret = 0;

    while (*p) {
        AVDictionary *entries = NULL;
        char *section_name = av_get_token(&p, "=:");
        int show_all_entries = 0;

        if (!section_name) {
            av_log(NULL, AV_LOG_ERROR,
                   "Missing section name for option '%s'\n", opt);
            return AVERROR(EINVAL);
        }

        if (*p == '=') {
            p++;
            while (*p && *p != ':') {
                char *entry = av_get_token(&p, ",:");
                if (!entry)
                    break;
                av_log(NULL, AV_LOG_VERBOSE,
                       "Adding '%s' to the entries to show in section '%s'\n",
                       entry, section_name);
                av_dict_set(&entries, entry, "", AV_DICT_DONT_STRDUP_KEY);
                if (*p == ',')
                    p++;
            }
        } else {
            show_all_entries = 1;
        }

        ret = match_section(section_name, show_all_entries, entries);
        if (ret == 0) {
            av_log(NULL, AV_LOG_ERROR, "No match for section '%s'\n", section_name);
            ret = AVERROR(EINVAL);
        }
        av_dict_free(&entries);
        av_free(section_name);

        if (ret <= 0)
            break;
        if (*p)
            p++;
    }

    return ret;
}

static int opt_show_format_entry(void *optctx, const char *opt, const char *arg)
{
    char *buf = av_asprintf("format=%s", arg);
    int ret;

    av_log(NULL, AV_LOG_WARNING,
           "Option '%s' is deprecated, use '-show_entries format=%s' instead\n",
           opt, arg);
    ret = opt_show_entries(optctx, opt, buf);
    av_free(buf);
    return ret;
}

static void opt_input_file(void *optctx, const char *arg)
{
    if (input_filename) {
        av_log(NULL, AV_LOG_ERROR,
                "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        exit_program(1);
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

/**
 * Parse interval specification, according to the format:
 * INTERVAL ::= [START|+START_OFFSET][%[END|+END_OFFSET]]
 * INTERVALS ::= INTERVAL[,INTERVALS]
*/
static int parse_read_interval(const char *interval_spec,
                               ReadInterval *interval)
{
    int ret = 0;
    char *next, *p, *spec = av_strdup(interval_spec);
    if (!spec)
        return AVERROR(ENOMEM);

    if (!*spec) {
        av_log(NULL, AV_LOG_ERROR, "Invalid empty interval specification\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    p = spec;
    next = strchr(spec, '%');
    if (next)
        *next++ = 0;

    /* parse first part */
    if (*p) {
        interval->has_start = 1;

        if (*p == '+') {
            interval->start_is_offset = 1;
            p++;
        } else {
            interval->start_is_offset = 0;
        }

        ret = av_parse_time(&interval->start, p, 1);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Invalid interval start specification '%s'\n", p);
            goto end;
        }
    } else {
        interval->has_start = 0;
    }

    /* parse second part */
    p = next;
    if (p && *p) {
        int64_t us;
        interval->has_end = 1;

        if (*p == '+') {
            interval->end_is_offset = 1;
            p++;
        } else {
            interval->end_is_offset = 0;
        }

        if (interval->end_is_offset && *p == '#') {
            long long int lli;
            char *tail;
            interval->duration_frames = 1;
            p++;
            lli = strtoll(p, &tail, 10);
            if (*tail || lli < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "Invalid or negative value '%s' for duration number of frames\n", p);
                goto end;
            }
            interval->end = lli;
        } else {
            ret = av_parse_time(&us, p, 1);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Invalid interval end/duration specification '%s'\n", p);
                goto end;
            }
            interval->end = us;
        }
    } else {
        interval->has_end = 0;
    }

end:
    av_free(spec);
    return ret;
}

static int parse_read_intervals(const char *intervals_spec)
{
    int ret, n, i;
    char *p, *spec = av_strdup(intervals_spec);
    if (!spec)
        return AVERROR(ENOMEM);

    /* preparse specification, get number of intervals */
    for (n = 0, p = spec; *p; p++)
        if (*p == ',')
            n++;
    n++;

    read_intervals = av_malloc(n * sizeof(*read_intervals));
    if (!read_intervals) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    read_intervals_nb = n;

    /* parse intervals */
    p = spec;
    for (i = 0; i < n; i++) {
        char *next = strchr(p, ',');
        if (next)
            *next++ = 0;

        read_intervals[i].id = i;
        ret = parse_read_interval(p, &read_intervals[i]);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing read interval #%d '%s'\n",
                   i, p);
            goto end;
        }
        av_log(NULL, AV_LOG_VERBOSE, "Parsed log interval ");
        log_read_interval(&read_intervals[i], NULL, AV_LOG_VERBOSE);
        p = next;
        av_assert0(i <= read_intervals_nb);
    }
    av_assert0(i == read_intervals_nb);

end:
    av_free(spec);
    return ret;
}

static int opt_read_intervals(void *optctx, const char *opt, const char *arg)
{
    return parse_read_intervals(arg);
}

static int opt_pretty(void *optctx, const char *opt, const char *arg)
{
    show_value_unit              = 1;
    use_value_prefix             = 1;
    use_byte_value_binary_prefix = 1;
    use_value_sexagesimal_format = 1;
    return 0;
}

static void print_section(SectionID id, int level)
{
    const SectionID *pid;
    const struct section *section = &sections[id];
    printf("%c%c%c",
           section->flags & SECTION_FLAG_IS_WRAPPER           ? 'W' : '.',
           section->flags & SECTION_FLAG_IS_ARRAY             ? 'A' : '.',
           section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS  ? 'V' : '.');
    printf("%*c  %s", level * 4, ' ', section->name);
    if (section->unique_name)
        printf("/%s", section->unique_name);
    printf("\n");

    for (pid = section->children_ids; *pid != -1; pid++)
        print_section(*pid, level+1);
}

static int opt_sections(void *optctx, const char *opt, const char *arg)
{
    printf("Sections:\n"
           "W.. = Section is a wrapper (contains other sections, no local entries)\n"
           ".A. = Section contains an array of elements of the same type\n"
           "..V = Section may contain a variable number of fields with variable keys\n"
           "FLAGS NAME/UNIQUE_NAME\n"
           "---\n");
    print_section(SECTION_ID_ROOT, 0);
    return 0;
}

static int opt_show_versions(const char *opt, const char *arg)
{
    mark_section_show_entries(SECTION_ID_PROGRAM_VERSION, 1, NULL);
    mark_section_show_entries(SECTION_ID_LIBRARY_VERSION, 1, NULL);
    return 0;
}

#define DEFINE_OPT_SHOW_SECTION(section, target_section_id)             \
    static int opt_show_##section(const char *opt, const char *arg)     \
    {                                                                   \
        mark_section_show_entries(SECTION_ID_##target_section_id, 1, NULL); \
        return 0;                                                       \
    }

DEFINE_OPT_SHOW_SECTION(chapters,         CHAPTERS);
DEFINE_OPT_SHOW_SECTION(error,            ERROR);
DEFINE_OPT_SHOW_SECTION(format,           FORMAT);
DEFINE_OPT_SHOW_SECTION(frames,           FRAMES);
DEFINE_OPT_SHOW_SECTION(library_versions, LIBRARY_VERSIONS);
DEFINE_OPT_SHOW_SECTION(packets,          PACKETS);
DEFINE_OPT_SHOW_SECTION(program_version,  PROGRAM_VERSION);
DEFINE_OPT_SHOW_SECTION(streams,          STREAMS);
DEFINE_OPT_SHOW_SECTION(programs,         PROGRAMS);

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
    { "select_streams", OPT_STRING | HAS_ARG, {(void*)&stream_specifier}, "select the specified streams", "stream_specifier" },
    { "sections", OPT_EXIT, {.func_arg = opt_sections}, "print sections structure and section information, and exit" },
    { "show_data",    OPT_BOOL, {(void*)&do_show_data}, "show packets data" },
    { "show_error",   0, {(void*)&opt_show_error},  "show probing error" },
    { "show_format",  0, {(void*)&opt_show_format}, "show format/container info" },
    { "show_frames",  0, {(void*)&opt_show_frames}, "show frames info" },
    { "show_format_entry", HAS_ARG, {.func_arg = opt_show_format_entry},
      "show a particular entry from the format/container info", "entry" },
    { "show_entries", HAS_ARG, {.func_arg = opt_show_entries},
      "show a set of specified entries", "entry_list" },
    { "show_packets", 0, {(void*)&opt_show_packets}, "show packets info" },
    { "show_programs", 0, {(void*)&opt_show_programs}, "show programs info" },
    { "show_streams", 0, {(void*)&opt_show_streams}, "show streams info" },
    { "show_chapters", 0, {(void*)&opt_show_chapters}, "show chapters info" },
    { "count_frames", OPT_BOOL, {(void*)&do_count_frames}, "count the number of frames per stream" },
    { "count_packets", OPT_BOOL, {(void*)&do_count_packets}, "count the number of packets per stream" },
    { "show_program_version",  0, {(void*)&opt_show_program_version},  "show ffprobe version" },
    { "show_library_versions", 0, {(void*)&opt_show_library_versions}, "show library versions" },
    { "show_versions",         0, {(void*)&opt_show_versions}, "show program and library versions" },
    { "show_private_data", OPT_BOOL, {(void*)&show_private_data}, "show private data" },
    { "private",           OPT_BOOL, {(void*)&show_private_data}, "same as show_private_data" },
    { "bitexact", OPT_BOOL, {&do_bitexact}, "force bitexact output" },
    { "read_intervals", HAS_ARG, {.func_arg = opt_read_intervals}, "set read intervals", "read_intervals" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_default}, "generic catch all option", "" },
    { "i", HAS_ARG, {.func_arg = opt_input_file_i}, "read specified file", "input_file"},
    { NULL, },
};

static inline int check_section_show_entries(int section_id)
{
    int *id;
    struct section *section = &sections[section_id];
    if (sections[section_id].show_all_entries || sections[section_id].entries_to_show)
        return 1;
    for (id = section->children_ids; *id != -1; id++)
        if (check_section_show_entries(*id))
            return 1;
    return 0;
}

#define SET_DO_SHOW(id, varname) do {                                   \
        if (check_section_show_entries(SECTION_ID_##id))                \
            do_show_##varname = 1;                                      \
    } while (0)

int main(int argc, char **argv)
{
    const Writer *w;
    WriterContext *wctx;
    char *buf;
    char *w_name = NULL, *w_args = NULL;
    int ret, i;

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    register_exit(ffprobe_cleanup);

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

    /* mark things to show, based on -show_entries */
    SET_DO_SHOW(CHAPTERS, chapters);
    SET_DO_SHOW(ERROR, error);
    SET_DO_SHOW(FORMAT, format);
    SET_DO_SHOW(FRAMES, frames);
    SET_DO_SHOW(LIBRARY_VERSIONS, library_versions);
    SET_DO_SHOW(PACKETS, packets);
    SET_DO_SHOW(PROGRAM_VERSION, program_version);
    SET_DO_SHOW(PROGRAMS, programs);
    SET_DO_SHOW(STREAMS, streams);
    SET_DO_SHOW(STREAM_DISPOSITION, stream_disposition);
    SET_DO_SHOW(PROGRAM_STREAM_DISPOSITION, stream_disposition);

    if (do_bitexact && (do_show_program_version || do_show_library_versions)) {
        av_log(NULL, AV_LOG_ERROR,
               "-bitexact and -show_program_version or -show_library_versions "
               "options are incompatible\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    writer_register_all();

    if (!print_format)
        print_format = av_strdup("default");
    if (!print_format) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    w_name = av_strtok(print_format, "=", &buf);
    w_args = buf;

    w = writer_get_by_name(w_name);
    if (!w) {
        av_log(NULL, AV_LOG_ERROR, "Unknown output format with name '%s'\n", w_name);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if ((ret = writer_open(&wctx, w, w_args,
                           sections, FF_ARRAY_ELEMS(sections))) >= 0) {
        writer_print_section_header(wctx, SECTION_ID_ROOT);

        if (do_show_program_version)
            ffprobe_show_program_version(wctx);
        if (do_show_library_versions)
            ffprobe_show_library_versions(wctx);

        if (!input_filename &&
            ((do_show_format || do_show_programs || do_show_streams || do_show_chapters || do_show_packets || do_show_error) ||
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

        writer_print_section_footer(wctx);
        writer_close(&wctx);
    }

end:
    av_freep(&print_format);
    av_freep(&read_intervals);

    uninit_opts();
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));

    avformat_network_deinit();

    return ret < 0;
}
