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
#include "libavutil/ffversion.h"

#include <string.h>

#include "libavformat/avformat.h"
#include "libavformat/version.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/display.h"
#include "libavutil/hash.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/hdr_dynamic_vivid_metadata.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/libm.h"
#include "libavutil/parseutils.h"
#include "libavutil/timecode.h"
#include "libavutil/timestamp.h"
#include "libavdevice/avdevice.h"
#include "libavdevice/version.h"
#include "libswscale/swscale.h"
#include "libswscale/version.h"
#include "libswresample/swresample.h"
#include "libswresample/version.h"
#include "libpostproc/postprocess.h"
#include "libpostproc/version.h"
#include "libavfilter/version.h"
#include "cmdutils.h"
#include "opt_common.h"

#include "libavutil/thread.h"

#if !HAVE_THREADS
#  ifdef pthread_mutex_lock
#    undef pthread_mutex_lock
#  endif
#  define pthread_mutex_lock(a) do{}while(0)
#  ifdef pthread_mutex_unlock
#    undef pthread_mutex_unlock
#  endif
#  define pthread_mutex_unlock(a) do{}while(0)
#endif

typedef struct InputStream {
    AVStream *st;

    AVCodecContext *dec_ctx;
} InputStream;

typedef struct InputFile {
    AVFormatContext *fmt_ctx;

    InputStream *streams;
    int       nb_streams;
} InputFile;

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
static int do_show_pixel_formats = 0;
static int do_show_pixel_format_flags = 0;
static int do_show_pixel_format_components = 0;
static int do_show_log = 0;

static int do_show_chapter_tags = 0;
static int do_show_format_tags = 0;
static int do_show_frame_tags = 0;
static int do_show_program_tags = 0;
static int do_show_stream_tags = 0;
static int do_show_packet_tags = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data            = 1;

#define SHOW_OPTIONAL_FIELDS_AUTO       -1
#define SHOW_OPTIONAL_FIELDS_NEVER       0
#define SHOW_OPTIONAL_FIELDS_ALWAYS      1
static int show_optional_fields = SHOW_OPTIONAL_FIELDS_AUTO;

static char *print_format;
static char *stream_specifier;
static char *show_data_hash;

typedef struct ReadInterval {
    int id;             ///< identifier
    int64_t start, end; ///< start, end in second/AV_TIME_BASE units
    int has_start, has_end;
    int start_is_offset, end_is_offset;
    int duration_frames;
} ReadInterval;

static ReadInterval *read_intervals;
static int read_intervals_nb = 0;

static int find_stream_info  = 1;

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
    SECTION_ID_FRAME_SIDE_DATA_LIST,
    SECTION_ID_FRAME_SIDE_DATA,
    SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST,
    SECTION_ID_FRAME_SIDE_DATA_TIMECODE,
    SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST,
    SECTION_ID_FRAME_SIDE_DATA_COMPONENT,
    SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST,
    SECTION_ID_FRAME_SIDE_DATA_PIECE,
    SECTION_ID_FRAME_LOG,
    SECTION_ID_FRAME_LOGS,
    SECTION_ID_LIBRARY_VERSION,
    SECTION_ID_LIBRARY_VERSIONS,
    SECTION_ID_PACKET,
    SECTION_ID_PACKET_TAGS,
    SECTION_ID_PACKETS,
    SECTION_ID_PACKETS_AND_FRAMES,
    SECTION_ID_PACKET_SIDE_DATA_LIST,
    SECTION_ID_PACKET_SIDE_DATA,
    SECTION_ID_PIXEL_FORMAT,
    SECTION_ID_PIXEL_FORMAT_FLAGS,
    SECTION_ID_PIXEL_FORMAT_COMPONENT,
    SECTION_ID_PIXEL_FORMAT_COMPONENTS,
    SECTION_ID_PIXEL_FORMATS,
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
    SECTION_ID_STREAM_SIDE_DATA_LIST,
    SECTION_ID_STREAM_SIDE_DATA,
    SECTION_ID_SUBTITLE,
} SectionID;

static struct section sections[] = {
    [SECTION_ID_CHAPTERS] =           { SECTION_ID_CHAPTERS, "chapters", SECTION_FLAG_IS_ARRAY, { SECTION_ID_CHAPTER, -1 } },
    [SECTION_ID_CHAPTER] =            { SECTION_ID_CHAPTER, "chapter", 0, { SECTION_ID_CHAPTER_TAGS, -1 } },
    [SECTION_ID_CHAPTER_TAGS] =       { SECTION_ID_CHAPTER_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "chapter_tags" },
    [SECTION_ID_ERROR] =              { SECTION_ID_ERROR, "error", 0, { -1 } },
    [SECTION_ID_FORMAT] =             { SECTION_ID_FORMAT, "format", 0, { SECTION_ID_FORMAT_TAGS, -1 } },
    [SECTION_ID_FORMAT_TAGS] =        { SECTION_ID_FORMAT_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "format_tags" },
    [SECTION_ID_FRAMES] =             { SECTION_ID_FRAMES, "frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME, SECTION_ID_SUBTITLE, -1 } },
    [SECTION_ID_FRAME] =              { SECTION_ID_FRAME, "frame", 0, { SECTION_ID_FRAME_TAGS, SECTION_ID_FRAME_SIDE_DATA_LIST, SECTION_ID_FRAME_LOGS, -1 } },
    [SECTION_ID_FRAME_TAGS] =         { SECTION_ID_FRAME_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "frame_tags" },
    [SECTION_ID_FRAME_SIDE_DATA_LIST] ={ SECTION_ID_FRAME_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "frame_side_data_list" },
    [SECTION_ID_FRAME_SIDE_DATA] =     { SECTION_ID_FRAME_SIDE_DATA, "side_data", 0, { SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST, -1 }, .unique_name = "frame_side_data" },
    [SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST] =  { SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST, "timecodes", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_TIMECODE, -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_TIMECODE] =       { SECTION_ID_FRAME_SIDE_DATA_TIMECODE, "timecode", 0, { -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST] = { SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST, "components", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_COMPONENT, -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_COMPONENT] =      { SECTION_ID_FRAME_SIDE_DATA_COMPONENT, "component", 0, { SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST, -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST] =   { SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST, "pieces", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_PIECE, -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_PIECE] =        { SECTION_ID_FRAME_SIDE_DATA_PIECE, "section", 0, { -1 } },
    [SECTION_ID_FRAME_LOGS] =         { SECTION_ID_FRAME_LOGS, "logs", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_LOG, -1 } },
    [SECTION_ID_FRAME_LOG] =          { SECTION_ID_FRAME_LOG, "log", 0, { -1 },  },
    [SECTION_ID_LIBRARY_VERSIONS] =   { SECTION_ID_LIBRARY_VERSIONS, "library_versions", SECTION_FLAG_IS_ARRAY, { SECTION_ID_LIBRARY_VERSION, -1 } },
    [SECTION_ID_LIBRARY_VERSION] =    { SECTION_ID_LIBRARY_VERSION, "library_version", 0, { -1 } },
    [SECTION_ID_PACKETS] =            { SECTION_ID_PACKETS, "packets", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKETS_AND_FRAMES] = { SECTION_ID_PACKETS_AND_FRAMES, "packets_and_frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKET] =             { SECTION_ID_PACKET, "packet", 0, { SECTION_ID_PACKET_TAGS, SECTION_ID_PACKET_SIDE_DATA_LIST, -1 } },
    [SECTION_ID_PACKET_TAGS] =        { SECTION_ID_PACKET_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "packet_tags" },
    [SECTION_ID_PACKET_SIDE_DATA_LIST] ={ SECTION_ID_PACKET_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "packet_side_data_list" },
    [SECTION_ID_PACKET_SIDE_DATA] =     { SECTION_ID_PACKET_SIDE_DATA, "side_data", 0, { -1 }, .unique_name = "packet_side_data" },
    [SECTION_ID_PIXEL_FORMATS] =      { SECTION_ID_PIXEL_FORMATS, "pixel_formats", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PIXEL_FORMAT, -1 } },
    [SECTION_ID_PIXEL_FORMAT] =       { SECTION_ID_PIXEL_FORMAT, "pixel_format", 0, { SECTION_ID_PIXEL_FORMAT_FLAGS, SECTION_ID_PIXEL_FORMAT_COMPONENTS, -1 } },
    [SECTION_ID_PIXEL_FORMAT_FLAGS] = { SECTION_ID_PIXEL_FORMAT_FLAGS, "flags", 0, { -1 }, .unique_name = "pixel_format_flags" },
    [SECTION_ID_PIXEL_FORMAT_COMPONENTS] = { SECTION_ID_PIXEL_FORMAT_COMPONENTS, "components", SECTION_FLAG_IS_ARRAY, {SECTION_ID_PIXEL_FORMAT_COMPONENT, -1 }, .unique_name = "pixel_format_components" },
    [SECTION_ID_PIXEL_FORMAT_COMPONENT]  = { SECTION_ID_PIXEL_FORMAT_COMPONENT, "component", 0, { -1 } },
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
                                          SECTION_ID_PACKETS, SECTION_ID_ERROR, SECTION_ID_PROGRAM_VERSION, SECTION_ID_LIBRARY_VERSIONS,
                                          SECTION_ID_PIXEL_FORMATS, -1} },
    [SECTION_ID_STREAMS] =            { SECTION_ID_STREAMS, "streams", SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM, -1 } },
    [SECTION_ID_STREAM] =             { SECTION_ID_STREAM, "stream", 0, { SECTION_ID_STREAM_DISPOSITION, SECTION_ID_STREAM_TAGS, SECTION_ID_STREAM_SIDE_DATA_LIST, -1 } },
    [SECTION_ID_STREAM_DISPOSITION] = { SECTION_ID_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_disposition" },
    [SECTION_ID_STREAM_TAGS] =        { SECTION_ID_STREAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_tags" },
    [SECTION_ID_STREAM_SIDE_DATA_LIST] ={ SECTION_ID_STREAM_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "stream_side_data_list" },
    [SECTION_ID_STREAM_SIDE_DATA] =     { SECTION_ID_STREAM_SIDE_DATA, "side_data", 0, { -1 }, .unique_name = "stream_side_data" },
    [SECTION_ID_SUBTITLE] =           { SECTION_ID_SUBTITLE, "subtitle", 0, { -1 } },
};

static const OptionDef *options;

/* FFprobe context */
static const char *input_filename;
static const char *print_input_filename;
static const AVInputFormat *iformat = NULL;

static struct AVHashContext *hash;

static const struct {
    double bin_val;
    double dec_val;
    const char *bin_str;
    const char *dec_str;
} si_prefixes[] = {
    { 1.0, 1.0, "", "" },
    { 1.024e3, 1e3, "Ki", "K" },
    { 1.048576e6, 1e6, "Mi", "M" },
    { 1.073741824e9, 1e9, "Gi", "G" },
    { 1.099511627776e12, 1e12, "Ti", "T" },
    { 1.125899906842624e15, 1e15, "Pi", "P" },
};

static const char unit_second_str[]         = "s"    ;
static const char unit_hertz_str[]          = "Hz"   ;
static const char unit_byte_str[]           = "byte" ;
static const char unit_bit_per_second_str[] = "bit/s";

static int nb_streams;
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;
static int *selected_streams;

#if HAVE_THREADS
pthread_mutex_t log_mutex;
#endif
typedef struct LogBuffer {
    char *context_name;
    int log_level;
    char *log_message;
    AVClassCategory category;
    char *parent_name;
    AVClassCategory parent_category;
}LogBuffer;

static LogBuffer *log_buffer;
static int log_buffer_size;

static void log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    AVClass* avc = ptr ? *(AVClass **) ptr : NULL;
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;
    void *new_log_buffer;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

#if HAVE_THREADS
    pthread_mutex_lock(&log_mutex);

    new_log_buffer = av_realloc_array(log_buffer, log_buffer_size + 1, sizeof(*log_buffer));
    if (new_log_buffer) {
        char *msg;
        int i;

        log_buffer = new_log_buffer;
        memset(&log_buffer[log_buffer_size], 0, sizeof(log_buffer[log_buffer_size]));
        log_buffer[log_buffer_size].context_name= avc ? av_strdup(avc->item_name(ptr)) : NULL;
        if (avc) {
            if (avc->get_category) log_buffer[log_buffer_size].category = avc->get_category(ptr);
            else                   log_buffer[log_buffer_size].category = avc->category;
        }
        log_buffer[log_buffer_size].log_level   = level;
        msg = log_buffer[log_buffer_size].log_message = av_strdup(line);
        for (i=strlen(msg) - 1; i>=0 && msg[i] == '\n'; i--) {
            msg[i] = 0;
        }
        if (avc && avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) ptr) +
                                   avc->parent_log_context_offset);
            if (parent && *parent) {
                log_buffer[log_buffer_size].parent_name = av_strdup((*parent)->item_name(parent));
                log_buffer[log_buffer_size].parent_category =
                    (*parent)->get_category ? (*parent)->get_category(parent) :(*parent)->category;
            }
        }
        log_buffer_size ++;
    }

    pthread_mutex_unlock(&log_mutex);
#endif
}

static void ffprobe_cleanup(int ret)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));

#if HAVE_THREADS
    pthread_mutex_destroy(&log_mutex);
#endif
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
                index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].bin_val;
                prefix_string = si_prefixes[index].bin_str;
            } else {
                index = (long long int) (log10(vald)) / 3;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].dec_val;
                prefix_string = si_prefixes[index].dec_str;
            }
            vali = vald;
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

typedef enum {
    WRITER_STRING_VALIDATION_FAIL,
    WRITER_STRING_VALIDATION_REPLACE,
    WRITER_STRING_VALIDATION_IGNORE,
    WRITER_STRING_VALIDATION_NB
} StringValidation;

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

    int string_validation;
    char *string_validation_replacement;
    unsigned int string_validation_utf8_flags;
};

static const char *writer_get_name(void *p)
{
    WriterContext *wctx = p;
    return wctx->writer->name;
}

#define OFFSET(x) offsetof(WriterContext, x)

static const AVOption writer_options[] = {
    { "string_validation", "set string validation mode",
      OFFSET(string_validation), AV_OPT_TYPE_INT, {.i64=WRITER_STRING_VALIDATION_REPLACE}, 0, WRITER_STRING_VALIDATION_NB-1, .unit = "sv" },
    { "sv", "set string validation mode",
      OFFSET(string_validation), AV_OPT_TYPE_INT, {.i64=WRITER_STRING_VALIDATION_REPLACE}, 0, WRITER_STRING_VALIDATION_NB-1, .unit = "sv" },
    { "ignore",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_IGNORE},  .unit = "sv" },
    { "replace", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_REPLACE}, .unit = "sv" },
    { "fail",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_FAIL},    .unit = "sv" },
    { "string_validation_replacement", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, {.str=""}},
    { "svr", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, {.str="\xEF\xBF\xBD"}},
    { NULL }
};

static void *writer_child_next(void *obj, void *prev)
{
    WriterContext *ctx = obj;
    if (!prev && ctx->writer && ctx->writer->priv_class && ctx->priv)
        return ctx->priv;
    return NULL;
}

static const AVClass writer_class = {
    .class_name = "Writer",
    .item_name  = writer_get_name,
    .option     = writer_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .child_next = writer_child_next,
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
    av_opt_free(*wctx);
    av_freep(wctx);
}

static void bprint_bytes(AVBPrint *bp, const uint8_t *ubuf, size_t ubuf_size)
{
    int i;
    av_bprintf(bp, "0X");
    for (i = 0; i < ubuf_size; i++)
        av_bprintf(bp, "%02X", ubuf[i]);
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

    av_opt_set_defaults(*wctx);

    if (writer->priv_class) {
        void *priv_ctx = (*wctx)->priv;
        *((const AVClass **)priv_ctx) = writer->priv_class;
        av_opt_set_defaults(priv_ctx);
    }

    /* convert options to dictionary */
    if (args) {
        AVDictionary *opts = NULL;
        const AVDictionaryEntry *opt = NULL;

        if ((ret = av_dict_parse_string(&opts, args, "=", ":", 0)) < 0) {
            av_log(*wctx, AV_LOG_ERROR, "Failed to parse option string '%s' provided to writer context\n", args);
            av_dict_free(&opts);
            goto fail;
        }

        while ((opt = av_dict_get(opts, "", opt, AV_DICT_IGNORE_SUFFIX))) {
            if ((ret = av_opt_set(*wctx, opt->key, opt->value, AV_OPT_SEARCH_CHILDREN)) < 0) {
                av_log(*wctx, AV_LOG_ERROR, "Failed to set option '%s' with value '%s' provided to writer context\n",
                       opt->key, opt->value);
                av_dict_free(&opts);
                goto fail;
            }
        }

        av_dict_free(&opts);
    }

    /* validate replace string */
    {
        const uint8_t *p = (*wctx)->string_validation_replacement;
        const uint8_t *endp = p + strlen(p);
        while (*p) {
            const uint8_t *p0 = p;
            int32_t code;
            ret = av_utf8_decode(&code, &p, endp, (*wctx)->string_validation_utf8_flags);
            if (ret < 0) {
                AVBPrint bp;
                av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
                bprint_bytes(&bp, p0, p-p0),
                    av_log(wctx, AV_LOG_ERROR,
                           "Invalid UTF8 sequence %s found in string validation replace '%s'\n",
                           bp.str, (*wctx)->string_validation_replacement);
                return ret;
            }
        }
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

static inline int validate_string(WriterContext *wctx, char **dstp, const char *src)
{
    const uint8_t *p, *endp;
    AVBPrint dstbuf;
    int invalid_chars_nb = 0, ret = 0;

    av_bprint_init(&dstbuf, 0, AV_BPRINT_SIZE_UNLIMITED);

    endp = src + strlen(src);
    for (p = (uint8_t *)src; *p;) {
        uint32_t code;
        int invalid = 0;
        const uint8_t *p0 = p;

        if (av_utf8_decode(&code, &p, endp, wctx->string_validation_utf8_flags) < 0) {
            AVBPrint bp;
            av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
            bprint_bytes(&bp, p0, p-p0);
            av_log(wctx, AV_LOG_DEBUG,
                   "Invalid UTF-8 sequence %s found in string '%s'\n", bp.str, src);
            invalid = 1;
        }

        if (invalid) {
            invalid_chars_nb++;

            switch (wctx->string_validation) {
            case WRITER_STRING_VALIDATION_FAIL:
                av_log(wctx, AV_LOG_ERROR,
                       "Invalid UTF-8 sequence found in string '%s'\n", src);
                ret = AVERROR_INVALIDDATA;
                goto end;
                break;

            case WRITER_STRING_VALIDATION_REPLACE:
                av_bprintf(&dstbuf, "%s", wctx->string_validation_replacement);
                break;
            }
        }

        if (!invalid || wctx->string_validation == WRITER_STRING_VALIDATION_IGNORE)
            av_bprint_append_data(&dstbuf, p0, p-p0);
    }

    if (invalid_chars_nb && wctx->string_validation == WRITER_STRING_VALIDATION_REPLACE) {
        av_log(wctx, AV_LOG_WARNING,
               "%d invalid UTF-8 sequence(s) found in string '%s', replaced with '%s'\n",
               invalid_chars_nb, src, wctx->string_validation_replacement);
    }

end:
    av_bprint_finalize(&dstbuf, dstp);
    return ret;
}

#define PRINT_STRING_OPT      1
#define PRINT_STRING_VALIDATE 2

static inline int writer_print_string(WriterContext *wctx,
                                      const char *key, const char *val, int flags)
{
    const struct section *section = wctx->section[wctx->level];
    int ret = 0;

    if (show_optional_fields == SHOW_OPTIONAL_FIELDS_NEVER ||
        (show_optional_fields == SHOW_OPTIONAL_FIELDS_AUTO
        && (flags & PRINT_STRING_OPT)
        && !(wctx->writer->flags & WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS)))
        return 0;

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        if (flags & PRINT_STRING_VALIDATE) {
            char *key1 = NULL, *val1 = NULL;
            ret = validate_string(wctx, &key1, key);
            if (ret < 0) goto end;
            ret = validate_string(wctx, &val1, val);
            if (ret < 0) goto end;
            wctx->writer->print_string(wctx, key1, val1);
        end:
            if (ret < 0) {
                av_log(wctx, AV_LOG_ERROR,
                       "Invalid key=value string combination %s=%s in section %s\n",
                       key, val, section->unique_name);
            }
            av_free(key1);
            av_free(val1);
        } else {
            wctx->writer->print_string(wctx, key, val);
        }

        wctx->nb_item[wctx->level]++;
    }

    return ret;
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
        writer_print_string(wctx, key, "N/A", PRINT_STRING_OPT);
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
        writer_print_string(wctx, key, "N/A", PRINT_STRING_OPT);
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

static void writer_print_data_hash(WriterContext *wctx, const char *name,
                                   uint8_t *data, int size)
{
    char *p, buf[AV_HASH_MAX_SIZE * 2 + 64] = { 0 };

    if (!hash)
        return;
    av_hash_init(hash);
    av_hash_update(hash, data, size);
    snprintf(buf, sizeof(buf), "%s:", av_hash_get_name(hash));
    p = buf + strlen(buf);
    av_hash_final_hex(hash, p, buf + sizeof(buf) - p);
    writer_print_string(wctx, name, buf, 0);
}

static void writer_print_integers(WriterContext *wctx, const char *name,
                                  uint8_t *data, int size, const char *format,
                                  int columns, int bytes, int offset_add)
{
    AVBPrint bp;
    int offset = 0, l, i;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, columns);
        for (i = 0; i < l; i++) {
            if      (bytes == 1) av_bprintf(&bp, format, *data);
            else if (bytes == 2) av_bprintf(&bp, format, AV_RN16(data));
            else if (bytes == 4) av_bprintf(&bp, format, AV_RN32(data));
            data += bytes;
            size --;
        }
        av_bprintf(&bp, "\n");
        offset += offset_add;
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
    .class_name = #name,                            \
    .item_name  = name##_get_name,                  \
    .option     = name##_options                    \
}

/* Default output */

typedef struct DefaultContext {
    const AVClass *class;
    int nokey;
    int noprint_wrappers;
    int nested_section[SECTION_MAX_NB_LEVELS];
} DefaultContext;

#undef OFFSET
#define OFFSET(x) offsetof(DefaultContext, x)

static const AVOption default_options[] = {
    { "noprint_wrappers", "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { "nw",               "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { "nokey",          "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { "nk",             "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
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
    {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  0, 0 },
    {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  0, 0 },
    {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=0},    0,        1        },
    {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=0},    0,        1        },
    {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  0, 0 },
    {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  0, 0 },
    {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
    {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
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
        }
        if (parent_section && !(parent_section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)) &&
            wctx->level && wctx->nb_item[wctx->level-1])
            printf("%c", compact->item_sep);
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
    {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  0, 0 },
    {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  0, 0 },
    {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
    {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
    {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, 0, 0 },
    {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, 0, 0 },
    {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
    {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
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
    {"sep_char", "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  0, 0 },
    {"s",        "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  0, 0 },
    {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
    {"h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
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

typedef struct INIContext {
    const AVClass *class;
    int hierarchical;
} INIContext;

#undef OFFSET
#define OFFSET(x) offsetof(INIContext, x)

static const AVOption ini_options[] = {
    {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
    {"h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
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

typedef struct JSONContext {
    const AVClass *class;
    int indent_level;
    int compact;
    const char *item_sep, *item_start_end;
} JSONContext;

#undef OFFSET
#define OFFSET(x) offsetof(JSONContext, x)

static const AVOption json_options[]= {
    { "compact", "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { "c",       "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
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
                printf("\"type\": \"%s\"", section->name);
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
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->nb_item[wctx->level] || (parent_section && parent_section->id == SECTION_ID_PACKETS_AND_FRAMES))
        printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    json_print_item_str(wctx, key, value);
}

static void json_print_int(WriterContext *wctx, const char *key, long long int value)
{
    JSONContext *json = wctx->priv;
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level] || (parent_section && parent_section->id == SECTION_ID_PACKETS_AND_FRAMES))
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

typedef struct XMLContext {
    const AVClass *class;
    int within_tag;
    int indent_level;
    int fully_qualified;
    int xsd_strict;
} XMLContext;

#undef OFFSET
#define OFFSET(x) offsetof(XMLContext, x)

static const AVOption xml_options[] = {
    {"fully_qualified", "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
    {"q",               "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
    {"xsd_strict",      "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
    {"x",               "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
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
    }

    return 0;
}

#define XML_INDENT() printf("%*c", xml->indent_level * 4, ' ')

static void xml_print_section_header(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->level == 0) {
        const char *qual = " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
            "xmlns:ffprobe=\"http://www.ffmpeg.org/schema/ffprobe\" "
            "xsi:schemaLocation=\"http://www.ffmpeg.org/schema/ffprobe ffprobe.xsd\"";

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
        av_bprint_escape(&buf, key, NULL,
                         AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        printf("<%s key=\"%s\"",
               section->element_name, buf.str);
        av_bprint_clear(&buf);

        av_bprint_escape(&buf, value, NULL,
                         AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        printf(" value=\"%s\"/>\n", buf.str);
    } else {
        if (wctx->nb_item[wctx->level])
            printf(" ");

        av_bprint_escape(&buf, value, NULL,
                         AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        printf("%s=\"%s\"", key, buf.str);
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

#define print_list_fmt(k, f, n, ...) do {       \
    av_bprint_clear(&pbuf);                     \
    for (int idx = 0; idx < n; idx++) {         \
        if (idx > 0)                            \
            av_bprint_chars(&pbuf, ' ', 1);     \
        av_bprintf(&pbuf, f, __VA_ARGS__);      \
    }                                           \
    writer_print_string(w, k, pbuf.str, 0);     \
} while (0)

#define print_int(k, v)         writer_print_integer(w, k, v)
#define print_q(k, v, s)        writer_print_rational(w, k, v, s)
#define print_str(k, v)         writer_print_string(w, k, v, 0)
#define print_str_opt(k, v)     writer_print_string(w, k, v, PRINT_STRING_OPT)
#define print_str_validate(k, v) writer_print_string(w, k, v, PRINT_STRING_VALIDATE)
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

#define REALLOCZ_ARRAY_STREAM(ptr, cur_n, new_n)                        \
{                                                                       \
    ret = av_reallocp_array(&(ptr), (new_n), sizeof(*(ptr)));           \
    if (ret < 0)                                                        \
        goto end;                                                       \
    memset( (ptr) + (cur_n), 0, ((new_n) - (cur_n)) * sizeof(*(ptr)) ); \
}

static inline int show_tags(WriterContext *w, AVDictionary *tags, int section_id)
{
    const AVDictionaryEntry *tag = NULL;
    int ret = 0;

    if (!tags)
        return 0;
    writer_print_section_header(w, section_id);

    while ((tag = av_dict_get(tags, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if ((ret = print_str_validate(tag->key, tag->value)) < 0)
            break;
    }
    writer_print_section_footer(w);

    return ret;
}

static void print_dovi_metadata(WriterContext *w, const AVDOVIMetadata *dovi)
{
    if (!dovi)
        return;

    {
        const AVDOVIRpuDataHeader *hdr     = av_dovi_get_header(dovi);
        const AVDOVIDataMapping   *mapping = av_dovi_get_mapping(dovi);
        const AVDOVIColorMetadata *color   = av_dovi_get_color(dovi);
        AVBPrint pbuf;

        av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

        // header
        print_int("rpu_type",        hdr->rpu_type);
        print_int("rpu_format",      hdr->rpu_format);
        print_int("vdr_rpu_profile", hdr->vdr_rpu_profile);
        print_int("vdr_rpu_level",   hdr->vdr_rpu_level);
        print_int("chroma_resampling_explicit_filter_flag",
                  hdr->chroma_resampling_explicit_filter_flag);
        print_int("coef_data_type",           hdr->coef_data_type);
        print_int("coef_log2_denom",          hdr->coef_log2_denom);
        print_int("vdr_rpu_normalized_idc",   hdr->vdr_rpu_normalized_idc);
        print_int("bl_video_full_range_flag", hdr->bl_video_full_range_flag);
        print_int("bl_bit_depth",             hdr->bl_bit_depth);
        print_int("el_bit_depth",             hdr->el_bit_depth);
        print_int("vdr_bit_depth",            hdr->vdr_bit_depth);
        print_int("spatial_resampling_filter_flag",
                  hdr->spatial_resampling_filter_flag);
        print_int("el_spatial_resampling_filter_flag",
                  hdr->el_spatial_resampling_filter_flag);
        print_int("disable_residual_flag",     hdr->disable_residual_flag);

        // data mapping values
        print_int("vdr_rpu_id",                mapping->vdr_rpu_id);
        print_int("mapping_color_space",       mapping->mapping_color_space);
        print_int("mapping_chroma_format_idc",
                  mapping->mapping_chroma_format_idc);

        print_int("nlq_method_idc",            mapping->nlq_method_idc);
        switch (mapping->nlq_method_idc) {
        case AV_DOVI_NLQ_NONE:
            print_str("nlq_method_idc_name", "none");
            break;
        case AV_DOVI_NLQ_LINEAR_DZ:
            print_str("nlq_method_idc_name", "linear_dz");
            break;
        default:
            print_str("nlq_method_idc_name", "unknown");
            break;
        }

        print_int("num_x_partitions",          mapping->num_x_partitions);
        print_int("num_y_partitions",          mapping->num_y_partitions);

        writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST);

        for (int c = 0; c < 3; c++) {
            const AVDOVIReshapingCurve *curve = &mapping->curves[c];
            writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_COMPONENT);

            print_list_fmt("pivots", "%"PRIu16, curve->num_pivots, curve->pivots[idx]);

            writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST);
            for (int i = 0; i < curve->num_pivots - 1; i++) {

                writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_PIECE);
                print_int("mapping_idc", curve->mapping_idc[i]);
                switch (curve->mapping_idc[i]) {
                case AV_DOVI_MAPPING_POLYNOMIAL:
                    print_str("mapping_idc_name",   "polynomial");
                    print_int("poly_order",         curve->poly_order[i]);
                    print_list_fmt("poly_coef", "%"PRIi64,
                                   curve->poly_order[i] + 1,
                                   curve->poly_coef[i][idx]);
                    break;
                case AV_DOVI_MAPPING_MMR:
                    print_str("mapping_idc_name",   "mmr");
                    print_int("mmr_order",          curve->mmr_order[i]);
                    print_int("mmr_constant",       curve->mmr_constant[i]);
                    print_list_fmt("mmr_coef", "%"PRIi64,
                                   curve->mmr_order[i] * 7,
                                   curve->mmr_coef[i][0][idx]);
                    break;
                default:
                    print_str("mapping_idc_name",   "unknown");
                    break;
                }

                // SECTION_ID_FRAME_SIDE_DATA_PIECE
                writer_print_section_footer(w);
            }

            // SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST
            writer_print_section_footer(w);

            if (mapping->nlq_method_idc != AV_DOVI_NLQ_NONE) {
                const AVDOVINLQParams *nlq  = &mapping->nlq[c];
                print_int("nlq_offset", nlq->nlq_offset);
                print_int("vdr_in_max", nlq->vdr_in_max);

                switch (mapping->nlq_method_idc) {
                case AV_DOVI_NLQ_LINEAR_DZ:
                    print_int("linear_deadzone_slope",      nlq->linear_deadzone_slope);
                    print_int("linear_deadzone_threshold",  nlq->linear_deadzone_threshold);
                    break;
                }
            }

            // SECTION_ID_FRAME_SIDE_DATA_COMPONENT
            writer_print_section_footer(w);
        }

        // SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST
        writer_print_section_footer(w);

        // color metadata
        print_int("dm_metadata_id",         color->dm_metadata_id);
        print_int("scene_refresh_flag",     color->scene_refresh_flag);
        print_list_fmt("ycc_to_rgb_matrix", "%d/%d",
                       FF_ARRAY_ELEMS(color->ycc_to_rgb_matrix),
                       color->ycc_to_rgb_matrix[idx].num,
                       color->ycc_to_rgb_matrix[idx].den);
        print_list_fmt("ycc_to_rgb_offset", "%d/%d",
                       FF_ARRAY_ELEMS(color->ycc_to_rgb_offset),
                       color->ycc_to_rgb_offset[idx].num,
                       color->ycc_to_rgb_offset[idx].den);
        print_list_fmt("rgb_to_lms_matrix", "%d/%d",
                       FF_ARRAY_ELEMS(color->rgb_to_lms_matrix),
                       color->rgb_to_lms_matrix[idx].num,
                       color->rgb_to_lms_matrix[idx].den);
        print_int("signal_eotf",            color->signal_eotf);
        print_int("signal_eotf_param0",     color->signal_eotf_param0);
        print_int("signal_eotf_param1",     color->signal_eotf_param1);
        print_int("signal_eotf_param2",     color->signal_eotf_param2);
        print_int("signal_bit_depth",       color->signal_bit_depth);
        print_int("signal_color_space",     color->signal_color_space);
        print_int("signal_chroma_format",   color->signal_chroma_format);
        print_int("signal_full_range_flag", color->signal_full_range_flag);
        print_int("source_min_pq",          color->source_min_pq);
        print_int("source_max_pq",          color->source_max_pq);
        print_int("source_diagonal",        color->source_diagonal);

        av_bprint_finalize(&pbuf, NULL);
    }
}

static void print_dynamic_hdr10_plus(WriterContext *w, const AVDynamicHDRPlus *metadata)
{
    if (!metadata)
        return;
    print_int("application version", metadata->application_version);
    print_int("num_windows", metadata->num_windows);
    for (int n = 1; n < metadata->num_windows; n++) {
        const AVHDRPlusColorTransformParams *params = &metadata->params[n];
        print_q("window_upper_left_corner_x",
                params->window_upper_left_corner_x,'/');
        print_q("window_upper_left_corner_y",
                params->window_upper_left_corner_y,'/');
        print_q("window_lower_right_corner_x",
                params->window_lower_right_corner_x,'/');
        print_q("window_lower_right_corner_y",
                params->window_lower_right_corner_y,'/');
        print_q("window_upper_left_corner_x",
                params->window_upper_left_corner_x,'/');
        print_q("window_upper_left_corner_y",
                params->window_upper_left_corner_y,'/');
        print_int("center_of_ellipse_x",
                  params->center_of_ellipse_x ) ;
        print_int("center_of_ellipse_y",
                  params->center_of_ellipse_y );
        print_int("rotation_angle",
                  params->rotation_angle);
        print_int("semimajor_axis_internal_ellipse",
                  params->semimajor_axis_internal_ellipse);
        print_int("semimajor_axis_external_ellipse",
                  params->semimajor_axis_external_ellipse);
        print_int("semiminor_axis_external_ellipse",
                  params->semiminor_axis_external_ellipse);
        print_int("overlap_process_option",
                  params->overlap_process_option);
    }
    print_q("targeted_system_display_maximum_luminance",
            metadata->targeted_system_display_maximum_luminance,'/');
    if (metadata->targeted_system_display_actual_peak_luminance_flag) {
        print_int("num_rows_targeted_system_display_actual_peak_luminance",
                  metadata->num_rows_targeted_system_display_actual_peak_luminance);
        print_int("num_cols_targeted_system_display_actual_peak_luminance",
                  metadata->num_cols_targeted_system_display_actual_peak_luminance);
        for (int i = 0; i < metadata->num_rows_targeted_system_display_actual_peak_luminance; i++) {
            for (int j = 0; j < metadata->num_cols_targeted_system_display_actual_peak_luminance; j++) {
                print_q("targeted_system_display_actual_peak_luminance",
                        metadata->targeted_system_display_actual_peak_luminance[i][j],'/');
            }
        }
    }
    for (int n = 0; n < metadata->num_windows; n++) {
        const AVHDRPlusColorTransformParams *params = &metadata->params[n];
        for (int i = 0; i < 3; i++) {
            print_q("maxscl",params->maxscl[i],'/');
        }
        print_q("average_maxrgb",
                params->average_maxrgb,'/');
        print_int("num_distribution_maxrgb_percentiles",
                  params->num_distribution_maxrgb_percentiles);
        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            print_int("distribution_maxrgb_percentage",
                      params->distribution_maxrgb[i].percentage);
            print_q("distribution_maxrgb_percentile",
                    params->distribution_maxrgb[i].percentile,'/');
        }
        print_q("fraction_bright_pixels",
                params->fraction_bright_pixels,'/');
    }
    if (metadata->mastering_display_actual_peak_luminance_flag) {
        print_int("num_rows_mastering_display_actual_peak_luminance",
                  metadata->num_rows_mastering_display_actual_peak_luminance);
        print_int("num_cols_mastering_display_actual_peak_luminance",
                  metadata->num_cols_mastering_display_actual_peak_luminance);
        for (int i = 0; i < metadata->num_rows_mastering_display_actual_peak_luminance; i++) {
            for (int j = 0; j <  metadata->num_cols_mastering_display_actual_peak_luminance; j++) {
                print_q("mastering_display_actual_peak_luminance",
                        metadata->mastering_display_actual_peak_luminance[i][j],'/');
            }
        }
    }

    for (int n = 0; n < metadata->num_windows; n++) {
        const AVHDRPlusColorTransformParams *params = &metadata->params[n];
        if (params->tone_mapping_flag) {
            print_q("knee_point_x", params->knee_point_x,'/');
            print_q("knee_point_y", params->knee_point_y,'/');
            print_int("num_bezier_curve_anchors",
                      params->num_bezier_curve_anchors );
            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                print_q("bezier_curve_anchors",
                        params->bezier_curve_anchors[i],'/');
            }
        }
        if (params->color_saturation_mapping_flag) {
            print_q("color_saturation_weight",
                    params->color_saturation_weight,'/');
        }
    }
}

static void print_dynamic_hdr_vivid(WriterContext *w, const AVDynamicHDRVivid *metadata)
{
    if (!metadata)
        return;
    print_int("system_start_code", metadata->system_start_code);
    print_int("num_windows", metadata->num_windows);

    for (int n = 0; n < metadata->num_windows; n++) {
        const AVHDRVividColorTransformParams *params = &metadata->params[n];

        print_q("minimum_maxrgb", params->minimum_maxrgb, '/');
        print_q("average_maxrgb", params->average_maxrgb, '/');
        print_q("variance_maxrgb", params->variance_maxrgb, '/');
        print_q("maximum_maxrgb", params->maximum_maxrgb, '/');
    }

    for (int n = 0; n < metadata->num_windows; n++) {
        const AVHDRVividColorTransformParams *params = &metadata->params[n];

        print_int("tone_mapping_mode_flag", params->tone_mapping_mode_flag);
        print_int("tone_mapping_param_num", params->tone_mapping_param_num);
        if (params->tone_mapping_mode_flag) {
            for (int i = 0; i < params->tone_mapping_param_num; i++) {
                const AVHDRVividColorToneMappingParams *tm_params = &params->tm_params[i];

                print_q("targeted_system_display_maximum_luminance",
                        tm_params->targeted_system_display_maximum_luminance, '/');
                print_int("base_enable_flag", tm_params->base_enable_flag);
                if (tm_params->base_enable_flag) {
                    print_q("base_param_m_p", tm_params->base_param_m_p, '/');
                    print_q("base_param_m_m", tm_params->base_param_m_m, '/');
                    print_q("base_param_m_a", tm_params->base_param_m_a, '/');
                    print_q("base_param_m_b", tm_params->base_param_m_b, '/');
                    print_q("base_param_m_n", tm_params->base_param_m_n, '/');

                    print_int("base_param_k1", tm_params->base_param_k1);
                    print_int("base_param_k2", tm_params->base_param_k2);
                    print_int("base_param_k3", tm_params->base_param_k3);
                    print_int("base_param_Delta_enable_mode",
                              tm_params->base_param_Delta_enable_mode);
                    print_q("base_param_Delta", tm_params->base_param_Delta, '/');
                }
                print_int("3Spline_enable_flag", tm_params->three_Spline_enable_flag);
                if (tm_params->three_Spline_enable_flag) {
                    print_int("3Spline_num", tm_params->three_Spline_num);
                    print_int("3Spline_TH_mode", tm_params->three_Spline_TH_mode);

                    for (int j = 0; j < tm_params->three_Spline_num; j++) {
                        print_q("3Spline_TH_enable_MB", tm_params->three_Spline_TH_enable_MB, '/');
                        print_q("3Spline_TH_enable", tm_params->three_Spline_TH_enable, '/');
                        print_q("3Spline_TH_Delta1", tm_params->three_Spline_TH_Delta1, '/');
                        print_q("3Spline_TH_Delta2", tm_params->three_Spline_TH_Delta2, '/');
                        print_q("3Spline_enable_Strength", tm_params->three_Spline_enable_Strength, '/');
                    }
                }
            }
        }

        print_int("color_saturation_mapping_flag", params->color_saturation_mapping_flag);
        if (params->color_saturation_mapping_flag) {
            print_int("color_saturation_num", params->color_saturation_num);
            for (int i = 0; i < params->color_saturation_num; i++) {
                print_q("color_saturation_gain", params->color_saturation_gain[i], '/');
            }
        }
    }
}

static void print_pkt_side_data(WriterContext *w,
                                AVCodecParameters *par,
                                const AVPacketSideData *side_data,
                                int nb_side_data,
                                SectionID id_data_list,
                                SectionID id_data)
{
    int i;

    writer_print_section_header(w, id_data_list);
    for (i = 0; i < nb_side_data; i++) {
        const AVPacketSideData *sd = &side_data[i];
        const char *name = av_packet_side_data_name(sd->type);

        writer_print_section_header(w, id_data);
        print_str("side_data_type", name ? name : "unknown");
        if (sd->type == AV_PKT_DATA_DISPLAYMATRIX && sd->size >= 9*4) {
            writer_print_integers(w, "displaymatrix", sd->data, 9, " %11d", 3, 4, 1);
            print_int("rotation", av_display_rotation_get((int32_t *)sd->data));
        } else if (sd->type == AV_PKT_DATA_STEREO3D) {
            const AVStereo3D *stereo = (AVStereo3D *)sd->data;
            print_str("type", av_stereo3d_type_name(stereo->type));
            print_int("inverted", !!(stereo->flags & AV_STEREO3D_FLAG_INVERT));
        } else if (sd->type == AV_PKT_DATA_SPHERICAL) {
            const AVSphericalMapping *spherical = (AVSphericalMapping *)sd->data;
            print_str("projection", av_spherical_projection_name(spherical->projection));
            if (spherical->projection == AV_SPHERICAL_CUBEMAP) {
                print_int("padding", spherical->padding);
            } else if (spherical->projection == AV_SPHERICAL_EQUIRECTANGULAR_TILE) {
                size_t l, t, r, b;
                av_spherical_tile_bounds(spherical, par->width, par->height,
                                         &l, &t, &r, &b);
                print_int("bound_left", l);
                print_int("bound_top", t);
                print_int("bound_right", r);
                print_int("bound_bottom", b);
            }

            print_int("yaw", (double) spherical->yaw / (1 << 16));
            print_int("pitch", (double) spherical->pitch / (1 << 16));
            print_int("roll", (double) spherical->roll / (1 << 16));
        } else if (sd->type == AV_PKT_DATA_SKIP_SAMPLES && sd->size == 10) {
            print_int("skip_samples",    AV_RL32(sd->data));
            print_int("discard_padding", AV_RL32(sd->data + 4));
            print_int("skip_reason",     AV_RL8(sd->data + 8));
            print_int("discard_reason",  AV_RL8(sd->data + 9));
        } else if (sd->type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA) {
            AVMasteringDisplayMetadata *metadata = (AVMasteringDisplayMetadata *)sd->data;

            if (metadata->has_primaries) {
                print_q("red_x", metadata->display_primaries[0][0], '/');
                print_q("red_y", metadata->display_primaries[0][1], '/');
                print_q("green_x", metadata->display_primaries[1][0], '/');
                print_q("green_y", metadata->display_primaries[1][1], '/');
                print_q("blue_x", metadata->display_primaries[2][0], '/');
                print_q("blue_y", metadata->display_primaries[2][1], '/');

                print_q("white_point_x", metadata->white_point[0], '/');
                print_q("white_point_y", metadata->white_point[1], '/');
            }

            if (metadata->has_luminance) {
                print_q("min_luminance", metadata->min_luminance, '/');
                print_q("max_luminance", metadata->max_luminance, '/');
            }
        } else if (sd->type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL) {
            AVContentLightMetadata *metadata = (AVContentLightMetadata *)sd->data;
            print_int("max_content", metadata->MaxCLL);
            print_int("max_average", metadata->MaxFALL);
        } else if (sd->type == AV_PKT_DATA_DOVI_CONF) {
            AVDOVIDecoderConfigurationRecord *dovi = (AVDOVIDecoderConfigurationRecord *)sd->data;
            print_int("dv_version_major", dovi->dv_version_major);
            print_int("dv_version_minor", dovi->dv_version_minor);
            print_int("dv_profile", dovi->dv_profile);
            print_int("dv_level", dovi->dv_level);
            print_int("rpu_present_flag", dovi->rpu_present_flag);
            print_int("el_present_flag", dovi->el_present_flag);
            print_int("bl_present_flag", dovi->bl_present_flag);
            print_int("dv_bl_signal_compatibility_id", dovi->dv_bl_signal_compatibility_id);
        } else if (sd->type == AV_PKT_DATA_AUDIO_SERVICE_TYPE) {
            enum AVAudioServiceType *t = (enum AVAudioServiceType *)sd->data;
            print_int("service_type", *t);
        } else if (sd->type == AV_PKT_DATA_MPEGTS_STREAM_ID) {
            print_int("id", *sd->data);
        } else if (sd->type == AV_PKT_DATA_CPB_PROPERTIES) {
            const AVCPBProperties *prop = (AVCPBProperties *)sd->data;
            print_int("max_bitrate", prop->max_bitrate);
            print_int("min_bitrate", prop->min_bitrate);
            print_int("avg_bitrate", prop->avg_bitrate);
            print_int("buffer_size", prop->buffer_size);
            print_int("vbv_delay",   prop->vbv_delay);
        } else if (sd->type == AV_PKT_DATA_WEBVTT_IDENTIFIER ||
                   sd->type == AV_PKT_DATA_WEBVTT_SETTINGS) {
            if (do_show_data)
                writer_print_data(w, "data", sd->data, sd->size);
            writer_print_data_hash(w, "data_hash", sd->data, sd->size);
        }
        writer_print_section_footer(w);
    }
    writer_print_section_footer(w);
}

static void print_color_range(WriterContext *w, enum AVColorRange color_range)
{
    const char *val = av_color_range_name(color_range);
    if (!val || color_range == AVCOL_RANGE_UNSPECIFIED) {
        print_str_opt("color_range", "unknown");
    } else {
        print_str("color_range", val);
    }
}

static void print_color_space(WriterContext *w, enum AVColorSpace color_space)
{
    const char *val = av_color_space_name(color_space);
    if (!val || color_space == AVCOL_SPC_UNSPECIFIED) {
        print_str_opt("color_space", "unknown");
    } else {
        print_str("color_space", val);
    }
}

static void print_primaries(WriterContext *w, enum AVColorPrimaries color_primaries)
{
    const char *val = av_color_primaries_name(color_primaries);
    if (!val || color_primaries == AVCOL_PRI_UNSPECIFIED) {
        print_str_opt("color_primaries", "unknown");
    } else {
        print_str("color_primaries", val);
    }
}

static void print_color_trc(WriterContext *w, enum AVColorTransferCharacteristic color_trc)
{
    const char *val = av_color_transfer_name(color_trc);
    if (!val || color_trc == AVCOL_TRC_UNSPECIFIED) {
        print_str_opt("color_transfer", "unknown");
    } else {
        print_str("color_transfer", val);
    }
}

static void print_chroma_location(WriterContext *w, enum AVChromaLocation chroma_location)
{
    const char *val = av_chroma_location_name(chroma_location);
    if (!val || chroma_location == AVCHROMA_LOC_UNSPECIFIED) {
        print_str_opt("chroma_location", "unspecified");
    } else {
        print_str("chroma_location", val);
    }
}


static void clear_log(int need_lock)
{
    int i;

    if (need_lock)
        pthread_mutex_lock(&log_mutex);
    for (i=0; i<log_buffer_size; i++) {
        av_freep(&log_buffer[i].context_name);
        av_freep(&log_buffer[i].parent_name);
        av_freep(&log_buffer[i].log_message);
    }
    log_buffer_size = 0;
    if(need_lock)
        pthread_mutex_unlock(&log_mutex);
}

static int show_log(WriterContext *w, int section_ids, int section_id, int log_level)
{
    int i;
    pthread_mutex_lock(&log_mutex);
    if (!log_buffer_size) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }
    writer_print_section_header(w, section_ids);

    for (i=0; i<log_buffer_size; i++) {
        if (log_buffer[i].log_level <= log_level) {
            writer_print_section_header(w, section_id);
            print_str("context", log_buffer[i].context_name);
            print_int("level", log_buffer[i].log_level);
            print_int("category", log_buffer[i].category);
            if (log_buffer[i].parent_name) {
                print_str("parent_context", log_buffer[i].parent_name);
                print_int("parent_category", log_buffer[i].parent_category);
            } else {
                print_str_opt("parent_context", "N/A");
                print_str_opt("parent_category", "N/A");
            }
            print_str("message", log_buffer[i].log_message);
            writer_print_section_footer(w);
        }
    }
    clear_log(0);
    pthread_mutex_unlock(&log_mutex);

    writer_print_section_footer(w);

    return 0;
}

static void show_packet(WriterContext *w, InputFile *ifile, AVPacket *pkt, int packet_idx)
{
    char val_str[128];
    AVStream *st = ifile->streams[pkt->stream_index].st;
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, SECTION_ID_PACKET);

    s = av_get_media_type_string(st->codecpar->codec_type);
    if (s) print_str    ("codec_type", s);
    else   print_str_opt("codec_type", "unknown");
    print_int("stream_index",     pkt->stream_index);
    print_ts  ("pts",             pkt->pts);
    print_time("pts_time",        pkt->pts, &st->time_base);
    print_ts  ("dts",             pkt->dts);
    print_time("dts_time",        pkt->dts, &st->time_base);
    print_duration_ts("duration",        pkt->duration);
    print_duration_time("duration_time", pkt->duration, &st->time_base);
    print_val("size",             pkt->size, unit_byte_str);
    if (pkt->pos != -1) print_fmt    ("pos", "%"PRId64, pkt->pos);
    else                print_str_opt("pos", "N/A");
    print_fmt("flags", "%c%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_',
              pkt->flags & AV_PKT_FLAG_DISCARD ? 'D' : '_');

    if (pkt->side_data_elems) {
        size_t size;
        const uint8_t *side_metadata;

        side_metadata = av_packet_get_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA, &size);
        if (side_metadata && size && do_show_packet_tags) {
            AVDictionary *dict = NULL;
            if (av_packet_unpack_dictionary(side_metadata, size, &dict) >= 0)
                show_tags(w, dict, SECTION_ID_PACKET_TAGS);
            av_dict_free(&dict);
        }

        print_pkt_side_data(w, st->codecpar, pkt->side_data, pkt->side_data_elems,
                            SECTION_ID_PACKET_SIDE_DATA_LIST,
                            SECTION_ID_PACKET_SIDE_DATA);
    }

    if (do_show_data)
        writer_print_data(w, "data", pkt->data, pkt->size);
    writer_print_data_hash(w, "data_hash", pkt->data, pkt->size);
    writer_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_subtitle(WriterContext *w, AVSubtitle *sub, AVStream *stream,
                          AVFormatContext *fmt_ctx)
{
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, SECTION_ID_SUBTITLE);

    print_str ("media_type",         "subtitle");
    print_ts  ("pts",                 sub->pts);
    print_time("pts_time",            sub->pts, &AV_TIME_BASE_Q);
    print_int ("format",              sub->format);
    print_int ("start_display_time",  sub->start_display_time);
    print_int ("end_display_time",    sub->end_display_time);
    print_int ("num_rects",           sub->num_rects);

    writer_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_frame(WriterContext *w, AVFrame *frame, AVStream *stream,
                       AVFormatContext *fmt_ctx)
{
    AVBPrint pbuf;
    char val_str[128];
    const char *s;
    int i;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, SECTION_ID_FRAME);

    s = av_get_media_type_string(stream->codecpar->codec_type);
    if (s) print_str    ("media_type", s);
    else   print_str_opt("media_type", "unknown");
    print_int("stream_index",           stream->index);
    print_int("key_frame",              frame->key_frame);
    print_ts  ("pts",                   frame->pts);
    print_time("pts_time",              frame->pts, &stream->time_base);
    print_ts  ("pkt_dts",               frame->pkt_dts);
    print_time("pkt_dts_time",          frame->pkt_dts, &stream->time_base);
    print_ts  ("best_effort_timestamp", frame->best_effort_timestamp);
    print_time("best_effort_timestamp_time", frame->best_effort_timestamp, &stream->time_base);
    print_duration_ts  ("pkt_duration",      frame->pkt_duration);
    print_duration_time("pkt_duration_time", frame->pkt_duration, &stream->time_base);
    if (frame->pkt_pos != -1) print_fmt    ("pkt_pos", "%"PRId64, frame->pkt_pos);
    else                      print_str_opt("pkt_pos", "N/A");
    if (frame->pkt_size != -1) print_val    ("pkt_size", frame->pkt_size, unit_byte_str);
    else                       print_str_opt("pkt_size", "N/A");

    switch (stream->codecpar->codec_type) {
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

        print_color_range(w, frame->color_range);
        print_color_space(w, frame->colorspace);
        print_primaries(w, frame->color_primaries);
        print_color_trc(w, frame->color_trc);
        print_chroma_location(w, frame->chroma_location);
        break;

    case AVMEDIA_TYPE_AUDIO:
        s = av_get_sample_fmt_name(frame->format);
        if (s) print_str    ("sample_fmt", s);
        else   print_str_opt("sample_fmt", "unknown");
        print_int("nb_samples",         frame->nb_samples);
        print_int("channels", frame->ch_layout.nb_channels);
        if (frame->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_describe(&frame->ch_layout, val_str, sizeof(val_str));
            print_str    ("channel_layout", val_str);
        } else
            print_str_opt("channel_layout", "unknown");
        break;
    }
    if (do_show_frame_tags)
        show_tags(w, frame->metadata, SECTION_ID_FRAME_TAGS);
    if (do_show_log)
        show_log(w, SECTION_ID_FRAME_LOGS, SECTION_ID_FRAME_LOG, do_show_log);
    if (frame->nb_side_data) {
        writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_LIST);
        for (i = 0; i < frame->nb_side_data; i++) {
            AVFrameSideData *sd = frame->side_data[i];
            const char *name;

            writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA);
            name = av_frame_side_data_name(sd->type);
            print_str("side_data_type", name ? name : "unknown");
            if (sd->type == AV_FRAME_DATA_DISPLAYMATRIX && sd->size >= 9*4) {
                writer_print_integers(w, "displaymatrix", sd->data, 9, " %11d", 3, 4, 1);
                print_int("rotation", av_display_rotation_get((int32_t *)sd->data));
            } else if (sd->type == AV_FRAME_DATA_GOP_TIMECODE && sd->size >= 8) {
                char tcbuf[AV_TIMECODE_STR_SIZE];
                av_timecode_make_mpeg_tc_string(tcbuf, *(int64_t *)(sd->data));
                print_str("timecode", tcbuf);
            } else if (sd->type == AV_FRAME_DATA_S12M_TIMECODE && sd->size == 16) {
                uint32_t *tc = (uint32_t*)sd->data;
                int m = FFMIN(tc[0],3);
                writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST);
                for (int j = 1; j <= m ; j++) {
                    char tcbuf[AV_TIMECODE_STR_SIZE];
                    av_timecode_make_smpte_tc_string2(tcbuf, stream->avg_frame_rate, tc[j], 0, 0);
                    writer_print_section_header(w, SECTION_ID_FRAME_SIDE_DATA_TIMECODE);
                    print_str("value", tcbuf);
                    writer_print_section_footer(w);
                }
                writer_print_section_footer(w);
            } else if (sd->type == AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) {
                AVMasteringDisplayMetadata *metadata = (AVMasteringDisplayMetadata *)sd->data;

                if (metadata->has_primaries) {
                    print_q("red_x", metadata->display_primaries[0][0], '/');
                    print_q("red_y", metadata->display_primaries[0][1], '/');
                    print_q("green_x", metadata->display_primaries[1][0], '/');
                    print_q("green_y", metadata->display_primaries[1][1], '/');
                    print_q("blue_x", metadata->display_primaries[2][0], '/');
                    print_q("blue_y", metadata->display_primaries[2][1], '/');

                    print_q("white_point_x", metadata->white_point[0], '/');
                    print_q("white_point_y", metadata->white_point[1], '/');
                }

                if (metadata->has_luminance) {
                    print_q("min_luminance", metadata->min_luminance, '/');
                    print_q("max_luminance", metadata->max_luminance, '/');
                }
            } else if (sd->type == AV_FRAME_DATA_DYNAMIC_HDR_PLUS) {
                AVDynamicHDRPlus *metadata = (AVDynamicHDRPlus *)sd->data;
                print_dynamic_hdr10_plus(w, metadata);
            } else if (sd->type == AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) {
                AVContentLightMetadata *metadata = (AVContentLightMetadata *)sd->data;
                print_int("max_content", metadata->MaxCLL);
                print_int("max_average", metadata->MaxFALL);
            } else if (sd->type == AV_FRAME_DATA_ICC_PROFILE) {
                const AVDictionaryEntry *tag = av_dict_get(sd->metadata, "name", NULL, AV_DICT_MATCH_CASE);
                if (tag)
                    print_str(tag->key, tag->value);
                print_int("size", sd->size);
            } else if (sd->type == AV_FRAME_DATA_DOVI_METADATA) {
                print_dovi_metadata(w, (const AVDOVIMetadata *)sd->data);
            } else if (sd->type == AV_FRAME_DATA_DYNAMIC_HDR_VIVID) {
                AVDynamicHDRVivid *metadata = (AVDynamicHDRVivid *)sd->data;
                print_dynamic_hdr_vivid(w, metadata);
            }
            writer_print_section_footer(w);
        }
        writer_print_section_footer(w);
    }

    writer_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static av_always_inline int process_frame(WriterContext *w,
                                          InputFile *ifile,
                                          AVFrame *frame, AVPacket *pkt,
                                          int *packet_new)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    AVCodecContext *dec_ctx = ifile->streams[pkt->stream_index].dec_ctx;
    AVCodecParameters *par = ifile->streams[pkt->stream_index].st->codecpar;
    AVSubtitle sub;
    int ret = 0, got_frame = 0;

    clear_log(1);
    if (dec_ctx) {
        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
        case AVMEDIA_TYPE_AUDIO:
            if (*packet_new) {
                ret = avcodec_send_packet(dec_ctx, pkt);
                if (ret == AVERROR(EAGAIN)) {
                    ret = 0;
                } else if (ret >= 0 || ret == AVERROR_EOF) {
                    ret = 0;
                    *packet_new = 0;
                }
            }
            if (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret >= 0) {
                    got_frame = 1;
                } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    ret = 0;
                }
            }
            break;

        case AVMEDIA_TYPE_SUBTITLE:
            if (*packet_new)
                ret = avcodec_decode_subtitle2(dec_ctx, &sub, &got_frame, pkt);
            *packet_new = 0;
            break;
        default:
            *packet_new = 0;
        }
    } else {
        *packet_new = 0;
    }

    if (ret < 0)
        return ret;
    if (got_frame) {
        int is_sub = (par->codec_type == AVMEDIA_TYPE_SUBTITLE);
        nb_streams_frames[pkt->stream_index]++;
        if (do_show_frames)
            if (is_sub)
                show_subtitle(w, &sub, ifile->streams[pkt->stream_index].st, fmt_ctx);
            else
                show_frame(w, frame, ifile->streams[pkt->stream_index].st, fmt_ctx);
        if (is_sub)
            avsubtitle_free(&sub);
    }
    return got_frame || *packet_new;
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

static int read_interval_packets(WriterContext *w, InputFile *ifile,
                                 const ReadInterval *interval, int64_t *cur_ts)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int ret = 0, i = 0, frame_count = 0;
    int64_t start = -INT64_MAX, end = interval->end;
    int has_start = 0, has_end = interval->has_end && !interval->end_is_offset;

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

    frame = av_frame_alloc();
    if (!frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    while (!av_read_frame(fmt_ctx, pkt)) {
        if (fmt_ctx->nb_streams > nb_streams) {
            REALLOCZ_ARRAY_STREAM(nb_streams_frames,  nb_streams, fmt_ctx->nb_streams);
            REALLOCZ_ARRAY_STREAM(nb_streams_packets, nb_streams, fmt_ctx->nb_streams);
            REALLOCZ_ARRAY_STREAM(selected_streams,   nb_streams, fmt_ctx->nb_streams);
            nb_streams = fmt_ctx->nb_streams;
        }
        if (selected_streams[pkt->stream_index]) {
            AVRational tb = ifile->streams[pkt->stream_index].st->time_base;

            if (pkt->pts != AV_NOPTS_VALUE)
                *cur_ts = av_rescale_q(pkt->pts, tb, AV_TIME_BASE_Q);

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
                    show_packet(w, ifile, pkt, i++);
                nb_streams_packets[pkt->stream_index]++;
            }
            if (do_read_frames) {
                int packet_new = 1;
                while (process_frame(w, ifile, frame, pkt, &packet_new) > 0);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_unref(pkt);
    //Flush remaining frames that are cached in the decoder
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        pkt->stream_index = i;
        if (do_read_frames) {
            while (process_frame(w, ifile, frame, pkt, &(int){1}) > 0);
            if (ifile->streams[i].dec_ctx)
                avcodec_flush_buffers(ifile->streams[i].dec_ctx);
        }
    }

end:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not read packets in interval ");
        log_read_interval(interval, NULL, AV_LOG_ERROR);
    }
    return ret;
}

static int read_packets(WriterContext *w, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;
    int64_t cur_ts = fmt_ctx->start_time;

    if (read_intervals_nb == 0) {
        ReadInterval interval = (ReadInterval) { .has_start = 0, .has_end = 0 };
        ret = read_interval_packets(w, ifile, &interval, &cur_ts);
    } else {
        for (i = 0; i < read_intervals_nb; i++) {
            ret = read_interval_packets(w, ifile, &read_intervals[i], &cur_ts);
            if (ret < 0)
                break;
        }
    }

    return ret;
}

static int show_stream(WriterContext *w, AVFormatContext *fmt_ctx, int stream_idx, InputStream *ist, int in_program)
{
    AVStream *stream = ist->st;
    AVCodecParameters *par;
    AVCodecContext *dec_ctx;
    char val_str[128];
    const char *s;
    AVRational sar, dar;
    AVBPrint pbuf;
    const AVCodecDescriptor *cd;
    int ret = 0;
    const char *profile = NULL;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    writer_print_section_header(w, in_program ? SECTION_ID_PROGRAM_STREAM : SECTION_ID_STREAM);

    print_int("index", stream->index);

    par     = stream->codecpar;
    dec_ctx = ist->dec_ctx;
    if (cd = avcodec_descriptor_get(par->codec_id)) {
        print_str("codec_name", cd->name);
        if (!do_bitexact) {
            print_str("codec_long_name",
                      cd->long_name ? cd->long_name : "unknown");
        }
    } else {
        print_str_opt("codec_name", "unknown");
        if (!do_bitexact) {
            print_str_opt("codec_long_name", "unknown");
        }
    }

    if (!do_bitexact && (profile = avcodec_profile_name(par->codec_id, par->profile)))
        print_str("profile", profile);
    else {
        if (par->profile != FF_PROFILE_UNKNOWN) {
            char profile_num[12];
            snprintf(profile_num, sizeof(profile_num), "%d", par->profile);
            print_str("profile", profile_num);
        } else
            print_str_opt("profile", "unknown");
    }

    s = av_get_media_type_string(par->codec_type);
    if (s) print_str    ("codec_type", s);
    else   print_str_opt("codec_type", "unknown");

    /* print AVI/FourCC tag */
    print_str("codec_tag_string",    av_fourcc2str(par->codec_tag));
    print_fmt("codec_tag", "0x%04"PRIx32, par->codec_tag);

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        print_int("width",        par->width);
        print_int("height",       par->height);
        if (dec_ctx) {
            print_int("coded_width",  dec_ctx->coded_width);
            print_int("coded_height", dec_ctx->coded_height);
            print_int("closed_captions", !!(dec_ctx->properties & FF_CODEC_PROPERTY_CLOSED_CAPTIONS));
            print_int("film_grain", !!(dec_ctx->properties & FF_CODEC_PROPERTY_FILM_GRAIN));
        }
        print_int("has_b_frames", par->video_delay);
        sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, NULL);
        if (sar.num) {
            print_q("sample_aspect_ratio", sar, ':');
            av_reduce(&dar.num, &dar.den,
                      par->width  * sar.num,
                      par->height * sar.den,
                      1024*1024);
            print_q("display_aspect_ratio", dar, ':');
        } else {
            print_str_opt("sample_aspect_ratio", "N/A");
            print_str_opt("display_aspect_ratio", "N/A");
        }
        s = av_get_pix_fmt_name(par->format);
        if (s) print_str    ("pix_fmt", s);
        else   print_str_opt("pix_fmt", "unknown");
        print_int("level",   par->level);

        print_color_range(w, par->color_range);
        print_color_space(w, par->color_space);
        print_color_trc(w, par->color_trc);
        print_primaries(w, par->color_primaries);
        print_chroma_location(w, par->chroma_location);

        if (par->field_order == AV_FIELD_PROGRESSIVE)
            print_str("field_order", "progressive");
        else if (par->field_order == AV_FIELD_TT)
            print_str("field_order", "tt");
        else if (par->field_order == AV_FIELD_BB)
            print_str("field_order", "bb");
        else if (par->field_order == AV_FIELD_TB)
            print_str("field_order", "tb");
        else if (par->field_order == AV_FIELD_BT)
            print_str("field_order", "bt");
        else
            print_str_opt("field_order", "unknown");

        if (dec_ctx)
            print_int("refs", dec_ctx->refs);
        break;

    case AVMEDIA_TYPE_AUDIO:
        s = av_get_sample_fmt_name(par->format);
        if (s) print_str    ("sample_fmt", s);
        else   print_str_opt("sample_fmt", "unknown");
        print_val("sample_rate",     par->sample_rate, unit_hertz_str);
        print_int("channels",        par->ch_layout.nb_channels);

        if (par->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_describe(&par->ch_layout, val_str, sizeof(val_str));
            print_str    ("channel_layout", val_str);
        } else {
            print_str_opt("channel_layout", "unknown");
        }

        print_int("bits_per_sample", av_get_bits_per_sample(par->codec_id));
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        if (par->width)
            print_int("width",       par->width);
        else
            print_str_opt("width",   "N/A");
        if (par->height)
            print_int("height",      par->height);
        else
            print_str_opt("height",  "N/A");
        break;
    }

    if (dec_ctx && dec_ctx->codec->priv_class && show_private_data) {
        const AVOption *opt = NULL;
        while (opt = av_opt_next(dec_ctx->priv_data,opt)) {
            uint8_t *str;
            if (!(opt->flags & AV_OPT_FLAG_EXPORT)) continue;
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
    if (par->bit_rate > 0)     print_val    ("bit_rate", par->bit_rate, unit_bit_per_second_str);
    else                       print_str_opt("bit_rate", "N/A");
    if (dec_ctx && dec_ctx->rc_max_rate > 0)
        print_val ("max_bit_rate", dec_ctx->rc_max_rate, unit_bit_per_second_str);
    else
        print_str_opt("max_bit_rate", "N/A");
    if (dec_ctx && dec_ctx->bits_per_raw_sample > 0) print_fmt("bits_per_raw_sample", "%d", dec_ctx->bits_per_raw_sample);
    else                                             print_str_opt("bits_per_raw_sample", "N/A");
    if (stream->nb_frames) print_fmt    ("nb_frames", "%"PRId64, stream->nb_frames);
    else                   print_str_opt("nb_frames", "N/A");
    if (nb_streams_frames[stream_idx])  print_fmt    ("nb_read_frames", "%"PRIu64, nb_streams_frames[stream_idx]);
    else                                print_str_opt("nb_read_frames", "N/A");
    if (nb_streams_packets[stream_idx]) print_fmt    ("nb_read_packets", "%"PRIu64, nb_streams_packets[stream_idx]);
    else                                print_str_opt("nb_read_packets", "N/A");
    if (do_show_data)
        writer_print_data(w, "extradata", par->extradata,
                                          par->extradata_size);

    if (par->extradata_size > 0) {
        print_int("extradata_size", par->extradata_size);
        writer_print_data_hash(w, "extradata_hash", par->extradata,
                                                    par->extradata_size);
    }

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
        PRINT_DISPOSITION(TIMED_THUMBNAILS, "timed_thumbnails");
        PRINT_DISPOSITION(CAPTIONS,         "captions");
        PRINT_DISPOSITION(DESCRIPTIONS,     "descriptions");
        PRINT_DISPOSITION(METADATA,         "metadata");
        PRINT_DISPOSITION(DEPENDENT,        "dependent");
        PRINT_DISPOSITION(STILL_IMAGE,      "still_image");
        writer_print_section_footer(w);
    }

    if (do_show_stream_tags)
        ret = show_tags(w, stream->metadata, in_program ? SECTION_ID_PROGRAM_STREAM_TAGS : SECTION_ID_STREAM_TAGS);

    if (stream->nb_side_data) {
        print_pkt_side_data(w, stream->codecpar, stream->side_data, stream->nb_side_data,
                            SECTION_ID_STREAM_SIDE_DATA_LIST,
                            SECTION_ID_STREAM_SIDE_DATA);
    }

    writer_print_section_footer(w);
    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);

    return ret;
}

static int show_streams(WriterContext *w, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    writer_print_section_header(w, SECTION_ID_STREAMS);
    for (i = 0; i < ifile->nb_streams; i++)
        if (selected_streams[i]) {
            ret = show_stream(w, fmt_ctx, i, &ifile->streams[i], 0);
            if (ret < 0)
                break;
        }
    writer_print_section_footer(w);

    return ret;
}

static int show_program(WriterContext *w, InputFile *ifile, AVProgram *program)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    writer_print_section_header(w, SECTION_ID_PROGRAM);
    print_int("program_id", program->id);
    print_int("program_num", program->program_num);
    print_int("nb_streams", program->nb_stream_indexes);
    print_int("pmt_pid", program->pmt_pid);
    print_int("pcr_pid", program->pcr_pid);
    if (do_show_program_tags)
        ret = show_tags(w, program->metadata, SECTION_ID_PROGRAM_TAGS);
    if (ret < 0)
        goto end;

    writer_print_section_header(w, SECTION_ID_PROGRAM_STREAMS);
    for (i = 0; i < program->nb_stream_indexes; i++) {
        if (selected_streams[program->stream_index[i]]) {
            ret = show_stream(w, fmt_ctx, program->stream_index[i], &ifile->streams[program->stream_index[i]], 1);
            if (ret < 0)
                break;
        }
    }
    writer_print_section_footer(w);

end:
    writer_print_section_footer(w);
    return ret;
}

static int show_programs(WriterContext *w, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    writer_print_section_header(w, SECTION_ID_PROGRAMS);
    for (i = 0; i < fmt_ctx->nb_programs; i++) {
        AVProgram *program = fmt_ctx->programs[i];
        if (!program)
            continue;
        ret = show_program(w, ifile, program);
        if (ret < 0)
            break;
    }
    writer_print_section_footer(w);
    return ret;
}

static int show_chapters(WriterContext *w, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

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
        if (do_show_chapter_tags)
            ret = show_tags(w, chapter->metadata, SECTION_ID_CHAPTER_TAGS);
        writer_print_section_footer(w);
    }
    writer_print_section_footer(w);

    return ret;
}

static int show_format(WriterContext *w, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    char val_str[128];
    int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;
    int ret = 0;

    writer_print_section_header(w, SECTION_ID_FORMAT);
    print_str_validate("filename", fmt_ctx->url);
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
    print_int("probe_score", fmt_ctx->probe_score);
    if (do_show_format_tags)
        ret = show_tags(w, fmt_ctx->metadata, SECTION_ID_FORMAT_TAGS);

    writer_print_section_footer(w);
    fflush(stdout);
    return ret;
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

static int open_input_file(InputFile *ifile, const char *filename,
                           const char *print_filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    const AVDictionaryEntry *t = NULL;
    int scan_all_pmts_set = 0;

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        print_error(filename, AVERROR(ENOMEM));
        exit_program(1);
    }

    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    if ((err = avformat_open_input(&fmt_ctx, filename,
                                   iformat, &format_opts)) < 0) {
        print_error(filename, err);
        return err;
    }
    if (print_filename) {
        av_freep(&fmt_ctx->url);
        fmt_ctx->url = av_strdup(print_filename);
    }
    ifile->fmt_ctx = fmt_ctx;
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    while ((t = av_dict_get(format_opts, "", t, AV_DICT_IGNORE_SUFFIX)))
        av_log(NULL, AV_LOG_WARNING, "Option %s skipped - not known to demuxer.\n", t->key);

    if (find_stream_info) {
        AVDictionary **opts = setup_find_stream_info_opts(fmt_ctx, codec_opts);
        int orig_nb_streams = fmt_ctx->nb_streams;

        err = avformat_find_stream_info(fmt_ctx, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (err < 0) {
            print_error(filename, err);
            return err;
        }
    }

    av_dump_format(fmt_ctx, 0, filename, 0);

    ifile->streams = av_calloc(fmt_ctx->nb_streams, sizeof(*ifile->streams));
    if (!ifile->streams)
        exit(1);
    ifile->nb_streams = fmt_ctx->nb_streams;

    /* bind a decoder to each input stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        InputStream *ist = &ifile->streams[i];
        AVStream *stream = fmt_ctx->streams[i];
        const AVCodec *codec;

        ist->st = stream;

        if (stream->codecpar->codec_id == AV_CODEC_ID_PROBE) {
            av_log(NULL, AV_LOG_WARNING,
                   "Failed to probe codec for input stream %d\n",
                    stream->index);
            continue;
        }

        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            av_log(NULL, AV_LOG_WARNING,
                    "Unsupported codec with id %d for input stream %d\n",
                    stream->codecpar->codec_id, stream->index);
            continue;
        }
        {
            AVDictionary *opts = filter_codec_opts(codec_opts, stream->codecpar->codec_id,
                                                   fmt_ctx, stream, codec);

            ist->dec_ctx = avcodec_alloc_context3(codec);
            if (!ist->dec_ctx)
                exit(1);

            err = avcodec_parameters_to_context(ist->dec_ctx, stream->codecpar);
            if (err < 0)
                exit(1);

            if (do_show_log) {
                // For loging it is needed to disable at least frame threads as otherwise
                // the log information would need to be reordered and matches up to contexts and frames
                // That is in fact possible but not trivial
                av_dict_set(&codec_opts, "threads", "1", 0);
            }

            ist->dec_ctx->pkt_timebase = stream->time_base;

            if (avcodec_open2(ist->dec_ctx, codec, &opts) < 0) {
                av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n",
                       stream->index);
                exit(1);
            }

            if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
                av_log(NULL, AV_LOG_ERROR, "Option %s for input stream %d not found\n",
                       t->key, stream->index);
                return AVERROR_OPTION_NOT_FOUND;
            }
        }
    }

    ifile->fmt_ctx = fmt_ctx;
    return 0;
}

static void close_input_file(InputFile *ifile)
{
    int i;

    /* close decoder for each stream */
    for (i = 0; i < ifile->nb_streams; i++)
        avcodec_free_context(&ifile->streams[i].dec_ctx);

    av_freep(&ifile->streams);
    ifile->nb_streams = 0;

    avformat_close_input(&ifile->fmt_ctx);
}

static int probe_file(WriterContext *wctx, const char *filename,
                      const char *print_filename)
{
    InputFile ifile = { 0 };
    int ret, i;
    int section_id;

    do_read_frames = do_show_frames || do_count_frames;
    do_read_packets = do_show_packets || do_count_packets;

    ret = open_input_file(&ifile, filename, print_filename);
    if (ret < 0)
        goto end;

#define CHECK_END if (ret < 0) goto end

    nb_streams = ifile.fmt_ctx->nb_streams;
    REALLOCZ_ARRAY_STREAM(nb_streams_frames,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(nb_streams_packets,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(selected_streams,0,ifile.fmt_ctx->nb_streams);

    for (i = 0; i < ifile.fmt_ctx->nb_streams; i++) {
        if (stream_specifier) {
            ret = avformat_match_stream_specifier(ifile.fmt_ctx,
                                                  ifile.fmt_ctx->streams[i],
                                                  stream_specifier);
            CHECK_END;
            else
                selected_streams[i] = ret;
            ret = 0;
        } else {
            selected_streams[i] = 1;
        }
        if (!selected_streams[i])
            ifile.fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
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
        ret = read_packets(wctx, &ifile);
        if (do_show_frames || do_show_packets)
            writer_print_section_footer(wctx);
        CHECK_END;
    }

    if (do_show_programs) {
        ret = show_programs(wctx, &ifile);
        CHECK_END;
    }

    if (do_show_streams) {
        ret = show_streams(wctx, &ifile);
        CHECK_END;
    }
    if (do_show_chapters) {
        ret = show_chapters(wctx, &ifile);
        CHECK_END;
    }
    if (do_show_format) {
        ret = show_format(wctx, &ifile);
        CHECK_END;
    }

end:
    if (ifile.fmt_ctx)
        close_input_file(&ifile);
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
              program_birth_year, CONFIG_THIS_YEAR);
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

#define PRINT_PIX_FMT_FLAG(flagname, name)                                \
    do {                                                                  \
        print_int(name, !!(pixdesc->flags & AV_PIX_FMT_FLAG_##flagname)); \
    } while (0)

static void ffprobe_show_pixel_formats(WriterContext *w)
{
    const AVPixFmtDescriptor *pixdesc = NULL;
    int i, n;

    writer_print_section_header(w, SECTION_ID_PIXEL_FORMATS);
    while (pixdesc = av_pix_fmt_desc_next(pixdesc)) {
        writer_print_section_header(w, SECTION_ID_PIXEL_FORMAT);
        print_str("name", pixdesc->name);
        print_int("nb_components", pixdesc->nb_components);
        if ((pixdesc->nb_components >= 3) && !(pixdesc->flags & AV_PIX_FMT_FLAG_RGB)) {
            print_int    ("log2_chroma_w", pixdesc->log2_chroma_w);
            print_int    ("log2_chroma_h", pixdesc->log2_chroma_h);
        } else {
            print_str_opt("log2_chroma_w", "N/A");
            print_str_opt("log2_chroma_h", "N/A");
        }
        n = av_get_bits_per_pixel(pixdesc);
        if (n) print_int    ("bits_per_pixel", n);
        else   print_str_opt("bits_per_pixel", "N/A");
        if (do_show_pixel_format_flags) {
            writer_print_section_header(w, SECTION_ID_PIXEL_FORMAT_FLAGS);
            PRINT_PIX_FMT_FLAG(BE,        "big_endian");
            PRINT_PIX_FMT_FLAG(PAL,       "palette");
            PRINT_PIX_FMT_FLAG(BITSTREAM, "bitstream");
            PRINT_PIX_FMT_FLAG(HWACCEL,   "hwaccel");
            PRINT_PIX_FMT_FLAG(PLANAR,    "planar");
            PRINT_PIX_FMT_FLAG(RGB,       "rgb");
            PRINT_PIX_FMT_FLAG(ALPHA,     "alpha");
            writer_print_section_footer(w);
        }
        if (do_show_pixel_format_components && (pixdesc->nb_components > 0)) {
            writer_print_section_header(w, SECTION_ID_PIXEL_FORMAT_COMPONENTS);
            for (i = 0; i < pixdesc->nb_components; i++) {
                writer_print_section_header(w, SECTION_ID_PIXEL_FORMAT_COMPONENT);
                print_int("index", i + 1);
                print_int("bit_depth", pixdesc->comp[i].depth);
                writer_print_section_footer(w);
            }
            writer_print_section_footer(w);
        }
        writer_print_section_footer(w);
    }
    writer_print_section_footer(w);
}

static int opt_show_optional_fields(void *optctx, const char *opt, const char *arg)
{
    if      (!av_strcasecmp(arg, "always")) show_optional_fields = SHOW_OPTIONAL_FIELDS_ALWAYS;
    else if (!av_strcasecmp(arg, "never"))  show_optional_fields = SHOW_OPTIONAL_FIELDS_NEVER;
    else if (!av_strcasecmp(arg, "auto"))   show_optional_fields = SHOW_OPTIONAL_FIELDS_AUTO;

    if (show_optional_fields == SHOW_OPTIONAL_FIELDS_AUTO && av_strcasecmp(arg, "auto"))
        show_optional_fields = parse_number_or_die("show_optional_fields", arg, OPT_INT, SHOW_OPTIONAL_FIELDS_AUTO, SHOW_OPTIONAL_FIELDS_ALWAYS);
    return 0;
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

static int opt_print_filename(void *optctx, const char *opt, const char *arg)
{
    print_input_filename = arg;
    return 0;
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, 0, 0);
    printf("\n");

    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
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
            interval->duration_frames = 0;
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

    read_intervals = av_malloc_array(n, sizeof(*read_intervals));
    if (!read_intervals) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    read_intervals_nb = n;

    /* parse intervals */
    p = spec;
    for (i = 0; p; i++) {
        char *next;

        av_assert0(i < read_intervals_nb);
        next = strchr(p, ',');
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

static int opt_show_versions(void *optctx, const char *opt, const char *arg)
{
    mark_section_show_entries(SECTION_ID_PROGRAM_VERSION, 1, NULL);
    mark_section_show_entries(SECTION_ID_LIBRARY_VERSION, 1, NULL);
    return 0;
}

#define DEFINE_OPT_SHOW_SECTION(section, target_section_id)             \
    static int opt_show_##section(void *optctx, const char *opt, const char *arg) \
    {                                                                   \
        mark_section_show_entries(SECTION_ID_##target_section_id, 1, NULL); \
        return 0;                                                       \
    }

DEFINE_OPT_SHOW_SECTION(chapters,         CHAPTERS)
DEFINE_OPT_SHOW_SECTION(error,            ERROR)
DEFINE_OPT_SHOW_SECTION(format,           FORMAT)
DEFINE_OPT_SHOW_SECTION(frames,           FRAMES)
DEFINE_OPT_SHOW_SECTION(library_versions, LIBRARY_VERSIONS)
DEFINE_OPT_SHOW_SECTION(packets,          PACKETS)
DEFINE_OPT_SHOW_SECTION(pixel_formats,    PIXEL_FORMATS)
DEFINE_OPT_SHOW_SECTION(program_version,  PROGRAM_VERSION)
DEFINE_OPT_SHOW_SECTION(streams,          STREAMS)
DEFINE_OPT_SHOW_SECTION(programs,         PROGRAMS)

static const OptionDef real_options[] = {
    CMDUTILS_COMMON_OPTIONS
    { "f", HAS_ARG, {.func_arg = opt_format}, "force format", "format" },
    { "unit", OPT_BOOL, {&show_value_unit}, "show unit of the displayed values" },
    { "prefix", OPT_BOOL, {&use_value_prefix}, "use SI prefixes for the displayed values" },
    { "byte_binary_prefix", OPT_BOOL, {&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal", OPT_BOOL,  {&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty", 0, {.func_arg = opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "print_format", OPT_STRING | HAS_ARG, { &print_format },
      "set the output printing format (available formats are: default, compact, csv, flat, ini, json, xml)", "format" },
    { "of", OPT_STRING | HAS_ARG, { &print_format }, "alias for -print_format", "format" },
    { "select_streams", OPT_STRING | HAS_ARG, { &stream_specifier }, "select the specified streams", "stream_specifier" },
    { "sections", OPT_EXIT, {.func_arg = opt_sections}, "print sections structure and section information, and exit" },
    { "show_data",    OPT_BOOL, { &do_show_data }, "show packets data" },
    { "show_data_hash", OPT_STRING | HAS_ARG, { &show_data_hash }, "show packets data hash" },
    { "show_error",   0, { .func_arg = &opt_show_error },  "show probing error" },
    { "show_format",  0, { .func_arg = &opt_show_format }, "show format/container info" },
    { "show_frames",  0, { .func_arg = &opt_show_frames }, "show frames info" },
    { "show_entries", HAS_ARG, {.func_arg = opt_show_entries},
      "show a set of specified entries", "entry_list" },
#if HAVE_THREADS
    { "show_log", OPT_INT|HAS_ARG, { &do_show_log }, "show log" },
#endif
    { "show_packets", 0, { .func_arg = &opt_show_packets }, "show packets info" },
    { "show_programs", 0, { .func_arg = &opt_show_programs }, "show programs info" },
    { "show_streams", 0, { .func_arg = &opt_show_streams }, "show streams info" },
    { "show_chapters", 0, { .func_arg = &opt_show_chapters }, "show chapters info" },
    { "count_frames", OPT_BOOL, { &do_count_frames }, "count the number of frames per stream" },
    { "count_packets", OPT_BOOL, { &do_count_packets }, "count the number of packets per stream" },
    { "show_program_version",  0, { .func_arg = &opt_show_program_version },  "show ffprobe version" },
    { "show_library_versions", 0, { .func_arg = &opt_show_library_versions }, "show library versions" },
    { "show_versions",         0, { .func_arg = &opt_show_versions }, "show program and library versions" },
    { "show_pixel_formats", 0, { .func_arg = &opt_show_pixel_formats }, "show pixel format descriptions" },
    { "show_optional_fields", HAS_ARG, { .func_arg = &opt_show_optional_fields }, "show optional fields" },
    { "show_private_data", OPT_BOOL, { &show_private_data }, "show private data" },
    { "private",           OPT_BOOL, { &show_private_data }, "same as show_private_data" },
    { "bitexact", OPT_BOOL, {&do_bitexact}, "force bitexact output" },
    { "read_intervals", HAS_ARG, {.func_arg = opt_read_intervals}, "set read intervals", "read_intervals" },
    { "i", HAS_ARG, {.func_arg = opt_input_file_i}, "read specified file", "input_file"},
    { "print_filename", HAS_ARG, {.func_arg = opt_print_filename}, "override the printed input filename", "print_file"},
    { "find_stream_info", OPT_BOOL | OPT_INPUT | OPT_EXPERT, { &find_stream_info },
        "read and decode the streams to fill missing information with heuristics" },
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

    init_dynload();

#if HAVE_THREADS
    ret = pthread_mutex_init(&log_mutex, NULL);
    if (ret != 0) {
        goto end;
    }
#endif
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    register_exit(ffprobe_cleanup);

    options = real_options;
    parse_loglevel(argc, argv, options);
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner(argc, argv, options);
    parse_options(NULL, argc, argv, options, opt_input_file);

    if (do_show_log)
        av_log_set_callback(log_callback);

    /* mark things to show, based on -show_entries */
    SET_DO_SHOW(CHAPTERS, chapters);
    SET_DO_SHOW(ERROR, error);
    SET_DO_SHOW(FORMAT, format);
    SET_DO_SHOW(FRAMES, frames);
    SET_DO_SHOW(LIBRARY_VERSIONS, library_versions);
    SET_DO_SHOW(PACKETS, packets);
    SET_DO_SHOW(PIXEL_FORMATS, pixel_formats);
    SET_DO_SHOW(PIXEL_FORMAT_FLAGS, pixel_format_flags);
    SET_DO_SHOW(PIXEL_FORMAT_COMPONENTS, pixel_format_components);
    SET_DO_SHOW(PROGRAM_VERSION, program_version);
    SET_DO_SHOW(PROGRAMS, programs);
    SET_DO_SHOW(STREAMS, streams);
    SET_DO_SHOW(STREAM_DISPOSITION, stream_disposition);
    SET_DO_SHOW(PROGRAM_STREAM_DISPOSITION, stream_disposition);

    SET_DO_SHOW(CHAPTER_TAGS, chapter_tags);
    SET_DO_SHOW(FORMAT_TAGS, format_tags);
    SET_DO_SHOW(FRAME_TAGS, frame_tags);
    SET_DO_SHOW(PROGRAM_TAGS, program_tags);
    SET_DO_SHOW(STREAM_TAGS, stream_tags);
    SET_DO_SHOW(PROGRAM_STREAM_TAGS, stream_tags);
    SET_DO_SHOW(PACKET_TAGS, packet_tags);

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
    if (!w_name) {
        av_log(NULL, AV_LOG_ERROR,
               "No name specified for the output format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    w_args = buf;

    if (show_data_hash) {
        if ((ret = av_hash_alloc(&hash, show_data_hash)) < 0) {
            if (ret == AVERROR(EINVAL)) {
                const char *n;
                av_log(NULL, AV_LOG_ERROR,
                       "Unknown hash algorithm '%s'\nKnown algorithms:",
                       show_data_hash);
                for (i = 0; (n = av_hash_names(i)); i++)
                    av_log(NULL, AV_LOG_ERROR, " %s", n);
                av_log(NULL, AV_LOG_ERROR, "\n");
            }
            goto end;
        }
    }

    w = writer_get_by_name(w_name);
    if (!w) {
        av_log(NULL, AV_LOG_ERROR, "Unknown output format with name '%s'\n", w_name);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if ((ret = writer_open(&wctx, w, w_args,
                           sections, FF_ARRAY_ELEMS(sections))) >= 0) {
        if (w == &xml_writer)
            wctx->string_validation_utf8_flags |= AV_UTF8_FLAG_EXCLUDE_XML_INVALID_CONTROL_CODES;

        writer_print_section_header(wctx, SECTION_ID_ROOT);

        if (do_show_program_version)
            ffprobe_show_program_version(wctx);
        if (do_show_library_versions)
            ffprobe_show_library_versions(wctx);
        if (do_show_pixel_formats)
            ffprobe_show_pixel_formats(wctx);

        if (!input_filename &&
            ((do_show_format || do_show_programs || do_show_streams || do_show_chapters || do_show_packets || do_show_error) ||
             (!do_show_program_version && !do_show_library_versions && !do_show_pixel_formats))) {
            show_usage();
            av_log(NULL, AV_LOG_ERROR, "You have to specify one input file.\n");
            av_log(NULL, AV_LOG_ERROR, "Use -h to get full help or, even better, run 'man %s'.\n", program_name);
            ret = AVERROR(EINVAL);
        } else if (input_filename) {
            ret = probe_file(wctx, input_filename, print_input_filename);
            if (ret < 0 && do_show_error)
                show_error(wctx, ret);
        }

        writer_print_section_footer(wctx);
        writer_close(&wctx);
    }

end:
    av_freep(&print_format);
    av_freep(&read_intervals);
    av_hash_freep(&hash);

    uninit_opts();
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));

    avformat_network_deinit();

    return ret < 0;
}
