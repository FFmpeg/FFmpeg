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
#include <math.h>

#include "libavformat/avformat.h"
#include "libavformat/version.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"
#include "libavutil/ambient_viewing_environment.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/display.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/iamf.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/hdr_dynamic_vivid_metadata.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/mem.h"
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
#include "libavfilter/version.h"
#include "textformat/avtextformat.h"
#include "cmdutils.h"
#include "opt_common.h"

#include "libavutil/thread.h"

// attached as opaque_ref to packets/frames
typedef struct FrameData {
    int64_t pkt_pos;
    int     pkt_size;
} FrameData;

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

static int do_analyze_frames = 0;
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
static int do_show_stream_groups = 0;
static int do_show_stream_group_components = 0;
static int do_show_streams = 0;
static int do_show_stream_disposition = 0;
static int do_show_stream_group_disposition = 0;
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
static int do_show_stream_group_tags = 0;
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

static char *output_format;
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

typedef enum {
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
    SECTION_ID_STREAM_GROUP_STREAM_DISPOSITION,
    SECTION_ID_STREAM_GROUP_STREAM_TAGS,
    SECTION_ID_STREAM_GROUP,
    SECTION_ID_STREAM_GROUP_COMPONENTS,
    SECTION_ID_STREAM_GROUP_COMPONENT,
    SECTION_ID_STREAM_GROUP_SUBCOMPONENTS,
    SECTION_ID_STREAM_GROUP_SUBCOMPONENT,
    SECTION_ID_STREAM_GROUP_PIECES,
    SECTION_ID_STREAM_GROUP_PIECE,
    SECTION_ID_STREAM_GROUP_SUBPIECES,
    SECTION_ID_STREAM_GROUP_SUBPIECE,
    SECTION_ID_STREAM_GROUP_BLOCKS,
    SECTION_ID_STREAM_GROUP_BLOCK,
    SECTION_ID_STREAM_GROUP_STREAMS,
    SECTION_ID_STREAM_GROUP_STREAM,
    SECTION_ID_STREAM_GROUP_DISPOSITION,
    SECTION_ID_STREAM_GROUP_TAGS,
    SECTION_ID_STREAM_GROUPS,
    SECTION_ID_ROOT,
    SECTION_ID_STREAM,
    SECTION_ID_STREAM_DISPOSITION,
    SECTION_ID_STREAMS,
    SECTION_ID_STREAM_TAGS,
    SECTION_ID_STREAM_SIDE_DATA_LIST,
    SECTION_ID_STREAM_SIDE_DATA,
    SECTION_ID_SUBTITLE,
} SectionID;

static const char *get_packet_side_data_type(const void *data)
{
    const AVPacketSideData *sd = (const AVPacketSideData *)data;
    return av_x_if_null(av_packet_side_data_name(sd->type), "unknown");
}

static const char *get_frame_side_data_type(const void *data)
{
    const AVFrameSideData *sd = (const AVFrameSideData *)data;
    return av_x_if_null(av_frame_side_data_name(sd->type), "unknown");
}

static const char *get_raw_string_type(const void *data)
{
    return data;
}

static const char *get_stream_group_type(const void *data)
{
    const AVStreamGroup *stg = (const AVStreamGroup *)data;
    return av_x_if_null(avformat_stream_group_name(stg->type), "unknown");
}

static struct AVTextFormatSection sections[] = {
    [SECTION_ID_CHAPTERS] =           { SECTION_ID_CHAPTERS, "chapters", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_CHAPTER, -1 } },
    [SECTION_ID_CHAPTER] =            { SECTION_ID_CHAPTER, "chapter", 0, { SECTION_ID_CHAPTER_TAGS, -1 } },
    [SECTION_ID_CHAPTER_TAGS] =       { SECTION_ID_CHAPTER_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "chapter_tags" },
    [SECTION_ID_ERROR] =              { SECTION_ID_ERROR, "error", 0, { -1 } },
    [SECTION_ID_FORMAT] =             { SECTION_ID_FORMAT, "format", 0, { SECTION_ID_FORMAT_TAGS, -1 } },
    [SECTION_ID_FORMAT_TAGS] =        { SECTION_ID_FORMAT_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "format_tags" },
    [SECTION_ID_FRAMES] =             { SECTION_ID_FRAMES, "frames", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME, SECTION_ID_SUBTITLE, -1 } },
    [SECTION_ID_FRAME] =              { SECTION_ID_FRAME, "frame", 0, { SECTION_ID_FRAME_TAGS, SECTION_ID_FRAME_SIDE_DATA_LIST, SECTION_ID_FRAME_LOGS, -1 } },
    [SECTION_ID_FRAME_TAGS] =         { SECTION_ID_FRAME_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "frame_tags" },
    [SECTION_ID_FRAME_SIDE_DATA_LIST] ={ SECTION_ID_FRAME_SIDE_DATA_LIST, "side_data_list", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "frame_side_data_list" },
    [SECTION_ID_FRAME_SIDE_DATA] =     { SECTION_ID_FRAME_SIDE_DATA, "side_data", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST, -1 }, .unique_name = "frame_side_data", .element_name = "side_datum", .get_type = get_frame_side_data_type },
    [SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST] =  { SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST, "timecodes", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_TIMECODE, -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_TIMECODE] =       { SECTION_ID_FRAME_SIDE_DATA_TIMECODE, "timecode", 0, { -1 } },
    [SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST] = { SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST, "components", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_COMPONENT, -1 }, .element_name = "component", .unique_name = "frame_side_data_components" },
    [SECTION_ID_FRAME_SIDE_DATA_COMPONENT] =      { SECTION_ID_FRAME_SIDE_DATA_COMPONENT, "component", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST, -1 }, .unique_name = "frame_side_data_component", .element_name = "component_entry", .get_type = get_raw_string_type },
    [SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST] =   { SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST, "pieces", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA_PIECE, -1 }, .element_name = "piece", .unique_name = "frame_side_data_pieces" },
    [SECTION_ID_FRAME_SIDE_DATA_PIECE] =        { SECTION_ID_FRAME_SIDE_DATA_PIECE, "piece", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { -1 }, .element_name = "piece_entry", .unique_name = "frame_side_data_piece", .get_type = get_raw_string_type },
    [SECTION_ID_FRAME_LOGS] =         { SECTION_ID_FRAME_LOGS, "logs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_LOG, -1 } },
    [SECTION_ID_FRAME_LOG] =          { SECTION_ID_FRAME_LOG, "log", 0, { -1 },  },
    [SECTION_ID_LIBRARY_VERSIONS] =   { SECTION_ID_LIBRARY_VERSIONS, "library_versions", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_LIBRARY_VERSION, -1 } },
    [SECTION_ID_LIBRARY_VERSION] =    { SECTION_ID_LIBRARY_VERSION, "library_version", 0, { -1 } },
    [SECTION_ID_PACKETS] =            { SECTION_ID_PACKETS, "packets", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKETS_AND_FRAMES] = { SECTION_ID_PACKETS_AND_FRAMES, "packets_and_frames", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE, { SECTION_ID_PACKET, -1} },
    [SECTION_ID_PACKET] =             { SECTION_ID_PACKET, "packet", 0, { SECTION_ID_PACKET_TAGS, SECTION_ID_PACKET_SIDE_DATA_LIST, -1 } },
    [SECTION_ID_PACKET_TAGS] =        { SECTION_ID_PACKET_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "packet_tags" },
    [SECTION_ID_PACKET_SIDE_DATA_LIST] ={ SECTION_ID_PACKET_SIDE_DATA_LIST, "side_data_list", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "packet_side_data_list" },
    [SECTION_ID_PACKET_SIDE_DATA] =     { SECTION_ID_PACKET_SIDE_DATA, "side_data", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { -1 }, .unique_name = "packet_side_data", .element_name = "side_datum", .get_type = get_packet_side_data_type },
    [SECTION_ID_PIXEL_FORMATS] =      { SECTION_ID_PIXEL_FORMATS, "pixel_formats", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PIXEL_FORMAT, -1 } },
    [SECTION_ID_PIXEL_FORMAT] =       { SECTION_ID_PIXEL_FORMAT, "pixel_format", 0, { SECTION_ID_PIXEL_FORMAT_FLAGS, SECTION_ID_PIXEL_FORMAT_COMPONENTS, -1 } },
    [SECTION_ID_PIXEL_FORMAT_FLAGS] = { SECTION_ID_PIXEL_FORMAT_FLAGS, "flags", 0, { -1 }, .unique_name = "pixel_format_flags" },
    [SECTION_ID_PIXEL_FORMAT_COMPONENTS] = { SECTION_ID_PIXEL_FORMAT_COMPONENTS, "components", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, {SECTION_ID_PIXEL_FORMAT_COMPONENT, -1 }, .unique_name = "pixel_format_components" },
    [SECTION_ID_PIXEL_FORMAT_COMPONENT]  = { SECTION_ID_PIXEL_FORMAT_COMPONENT, "component", 0, { -1 } },
    [SECTION_ID_PROGRAM_STREAM_DISPOSITION] = { SECTION_ID_PROGRAM_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "program_stream_disposition" },
    [SECTION_ID_PROGRAM_STREAM_TAGS] =        { SECTION_ID_PROGRAM_STREAM_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_stream_tags" },
    [SECTION_ID_PROGRAM] =                    { SECTION_ID_PROGRAM, "program", 0, { SECTION_ID_PROGRAM_TAGS, SECTION_ID_PROGRAM_STREAMS, -1 } },
    [SECTION_ID_PROGRAM_STREAMS] =            { SECTION_ID_PROGRAM_STREAMS, "streams", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM_STREAM, -1 }, .unique_name = "program_streams" },
    [SECTION_ID_PROGRAM_STREAM] =             { SECTION_ID_PROGRAM_STREAM, "stream", 0, { SECTION_ID_PROGRAM_STREAM_DISPOSITION, SECTION_ID_PROGRAM_STREAM_TAGS, -1 }, .unique_name = "program_stream" },
    [SECTION_ID_PROGRAM_TAGS] =               { SECTION_ID_PROGRAM_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_tags" },
    [SECTION_ID_PROGRAM_VERSION] =    { SECTION_ID_PROGRAM_VERSION, "program_version", 0, { -1 } },
    [SECTION_ID_PROGRAMS] =                   { SECTION_ID_PROGRAMS, "programs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM, -1 } },
    [SECTION_ID_STREAM_GROUP_STREAM_DISPOSITION] = { SECTION_ID_STREAM_GROUP_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_group_stream_disposition" },
    [SECTION_ID_STREAM_GROUP_STREAM_TAGS] =        { SECTION_ID_STREAM_GROUP_STREAM_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_group_stream_tags" },
    [SECTION_ID_STREAM_GROUP] =                    { SECTION_ID_STREAM_GROUP, "stream_group", 0, { SECTION_ID_STREAM_GROUP_TAGS, SECTION_ID_STREAM_GROUP_DISPOSITION, SECTION_ID_STREAM_GROUP_COMPONENTS, SECTION_ID_STREAM_GROUP_STREAMS, -1 } },
    [SECTION_ID_STREAM_GROUP_COMPONENTS] =         { SECTION_ID_STREAM_GROUP_COMPONENTS, "components", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_COMPONENT, -1 }, .element_name = "component", .unique_name = "stream_group_components" },
    [SECTION_ID_STREAM_GROUP_COMPONENT] =          { SECTION_ID_STREAM_GROUP_COMPONENT, "component", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_STREAM_GROUP_SUBCOMPONENTS, -1 }, .unique_name = "stream_group_component", .element_name = "component_entry", .get_type = get_stream_group_type },
    [SECTION_ID_STREAM_GROUP_SUBCOMPONENTS] =      { SECTION_ID_STREAM_GROUP_SUBCOMPONENTS, "subcomponents", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_SUBCOMPONENT, -1 }, .element_name = "component" },
    [SECTION_ID_STREAM_GROUP_SUBCOMPONENT] =       { SECTION_ID_STREAM_GROUP_SUBCOMPONENT, "subcomponent", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_STREAM_GROUP_PIECES, -1 }, .element_name = "subcomponent_entry", .get_type = get_raw_string_type },
    [SECTION_ID_STREAM_GROUP_PIECES] =             { SECTION_ID_STREAM_GROUP_PIECES, "pieces", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_PIECE, -1 }, .element_name = "piece", .unique_name = "stream_group_pieces" },
    [SECTION_ID_STREAM_GROUP_PIECE] =              { SECTION_ID_STREAM_GROUP_PIECE, "piece", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_STREAM_GROUP_SUBPIECES, -1 }, .unique_name = "stream_group_piece", .element_name = "piece_entry", .get_type = get_raw_string_type },
    [SECTION_ID_STREAM_GROUP_SUBPIECES] =          { SECTION_ID_STREAM_GROUP_SUBPIECES, "subpieces", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_SUBPIECE, -1 }, .element_name = "subpiece" },
    [SECTION_ID_STREAM_GROUP_SUBPIECE] =           { SECTION_ID_STREAM_GROUP_SUBPIECE, "subpiece", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { SECTION_ID_STREAM_GROUP_BLOCKS, -1 }, .element_name = "subpiece_entry", .get_type = get_raw_string_type },
    [SECTION_ID_STREAM_GROUP_BLOCKS] =             { SECTION_ID_STREAM_GROUP_BLOCKS, "blocks", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_BLOCK, -1 }, .element_name = "block" },
    [SECTION_ID_STREAM_GROUP_BLOCK] =              { SECTION_ID_STREAM_GROUP_BLOCK, "block", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS|AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE, { -1 }, .element_name = "block_entry", .get_type = get_raw_string_type },
    [SECTION_ID_STREAM_GROUP_STREAMS] =            { SECTION_ID_STREAM_GROUP_STREAMS, "streams", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP_STREAM, -1 }, .unique_name = "stream_group_streams" },
    [SECTION_ID_STREAM_GROUP_STREAM] =             { SECTION_ID_STREAM_GROUP_STREAM, "stream", 0, { SECTION_ID_STREAM_GROUP_STREAM_DISPOSITION, SECTION_ID_STREAM_GROUP_STREAM_TAGS, -1 }, .unique_name = "stream_group_stream" },
    [SECTION_ID_STREAM_GROUP_DISPOSITION] =        { SECTION_ID_STREAM_GROUP_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_group_disposition" },
    [SECTION_ID_STREAM_GROUP_TAGS] =               { SECTION_ID_STREAM_GROUP_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_group_tags" },
    [SECTION_ID_STREAM_GROUPS] =                   { SECTION_ID_STREAM_GROUPS, "stream_groups", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_GROUP, -1 } },
    [SECTION_ID_ROOT] =               { SECTION_ID_ROOT, "root", AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER,
                                        { SECTION_ID_CHAPTERS, SECTION_ID_FORMAT, SECTION_ID_FRAMES, SECTION_ID_PROGRAMS, SECTION_ID_STREAM_GROUPS, SECTION_ID_STREAMS,
                                          SECTION_ID_PACKETS, SECTION_ID_ERROR, SECTION_ID_PROGRAM_VERSION, SECTION_ID_LIBRARY_VERSIONS,
                                          SECTION_ID_PIXEL_FORMATS, -1} },
    [SECTION_ID_STREAMS] =            { SECTION_ID_STREAMS, "streams", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM, -1 } },
    [SECTION_ID_STREAM] =             { SECTION_ID_STREAM, "stream", 0, { SECTION_ID_STREAM_DISPOSITION, SECTION_ID_STREAM_TAGS, SECTION_ID_STREAM_SIDE_DATA_LIST, -1 } },
    [SECTION_ID_STREAM_DISPOSITION] = { SECTION_ID_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_disposition" },
    [SECTION_ID_STREAM_TAGS] =        { SECTION_ID_STREAM_TAGS, "tags", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_tags" },
    [SECTION_ID_STREAM_SIDE_DATA_LIST] ={ SECTION_ID_STREAM_SIDE_DATA_LIST, "side_data_list", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "stream_side_data_list" },
    [SECTION_ID_STREAM_SIDE_DATA] =     { SECTION_ID_STREAM_SIDE_DATA, "side_data", AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE|AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .unique_name = "stream_side_data", .element_name = "side_datum", .get_type = get_packet_side_data_type },
    [SECTION_ID_SUBTITLE] =           { SECTION_ID_SUBTITLE, "subtitle", 0, { -1 } },
};

static const OptionDef *options;

/* FFprobe context */
static const char *input_filename;
static const char *print_input_filename;
static const AVInputFormat *iformat = NULL;
static const char *output_filename = NULL;

static const char unit_second_str[]         = "s"    ;
static const char unit_hertz_str[]          = "Hz"   ;
static const char unit_byte_str[]           = "byte" ;
static const char unit_bit_per_second_str[] = "bit/s";

static int nb_streams;
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;
static int *selected_streams;
static int *streams_with_closed_captions;
static int *streams_with_film_grain;

static AVMutex log_mutex = AV_MUTEX_INITIALIZER;

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
    ff_mutex_lock(&log_mutex);

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

    ff_mutex_unlock(&log_mutex);
#endif
}


#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    avtext_print_string(tfc, k, pbuf.str, 0);    \
} while (0)

#define print_list_fmt(k, f, n, m, ...) do {    \
    av_bprint_clear(&pbuf);                     \
    for (int idx = 0; idx < n; idx++) {         \
        for (int idx2 = 0; idx2 < m; idx2++) {  \
            if (idx > 0 || idx2 > 0)            \
                av_bprint_chars(&pbuf, ' ', 1); \
            av_bprintf(&pbuf, f, __VA_ARGS__);  \
        }                                       \
    }                                           \
    avtext_print_string(tfc, k, pbuf.str, 0);     \
} while (0)

#define print_int(k, v)         avtext_print_integer(tfc, k, v, 0)
#define print_q(k, v, s)        avtext_print_rational(tfc, k, v, s)
#define print_str(k, v)         avtext_print_string(tfc, k, v, 0)
#define print_str_opt(k, v)     avtext_print_string(tfc, k, v, AV_TEXTFORMAT_PRINT_STRING_OPTIONAL)
#define print_str_validate(k, v) avtext_print_string(tfc, k, v, AV_TEXTFORMAT_PRINT_STRING_VALIDATE)
#define print_time(k, v, tb)    avtext_print_time(tfc, k, v, tb, 0)
#define print_ts(k, v)          avtext_print_ts(tfc, k, v, 0)
#define print_duration_time(k, v, tb) avtext_print_time(tfc, k, v, tb, 1)
#define print_duration_ts(k, v)       avtext_print_ts(tfc, k, v, 1)
#define print_val(k, v, u)      avtext_print_unit_int(tfc, k, v, u)

#define REALLOCZ_ARRAY_STREAM(ptr, cur_n, new_n)                        \
{                                                                       \
    ret = av_reallocp_array(&(ptr), (new_n), sizeof(*(ptr)));           \
    if (ret < 0)                                                        \
        goto end;                                                       \
    memset( (ptr) + (cur_n), 0, ((new_n) - (cur_n)) * sizeof(*(ptr)) ); \
}

static inline int show_tags(AVTextFormatContext *tfc, AVDictionary *tags, int section_id)
{
    const AVDictionaryEntry *tag = NULL;
    int ret = 0;

    if (!tags)
        return 0;
    avtext_print_section_header(tfc, NULL, section_id);

    while ((tag = av_dict_iterate(tags, tag))) {
        if ((ret = print_str_validate(tag->key, tag->value)) < 0)
            break;
    }
    avtext_print_section_footer(tfc);

    return ret;
}

static void print_displaymatrix(AVTextFormatContext *tfc, const int32_t matrix[9])
{
    double rotation = av_display_rotation_get(matrix);
    if (isnan(rotation))
        rotation = 0;
    avtext_print_integers(tfc, "displaymatrix", (void*)matrix, 9, " %11d", 3, 4, 1);
    print_int("rotation", rotation);
}

static void print_mastering_display_metadata(AVTextFormatContext *tfc,
                                             const AVMasteringDisplayMetadata *metadata)
{
    if (metadata->has_primaries) {
        print_q("red_x",   metadata->display_primaries[0][0], '/');
        print_q("red_y",   metadata->display_primaries[0][1], '/');
        print_q("green_x", metadata->display_primaries[1][0], '/');
        print_q("green_y", metadata->display_primaries[1][1], '/');
        print_q("blue_x",  metadata->display_primaries[2][0], '/');
        print_q("blue_y",  metadata->display_primaries[2][1], '/');

        print_q("white_point_x", metadata->white_point[0], '/');
        print_q("white_point_y", metadata->white_point[1], '/');
    }

    if (metadata->has_luminance) {
        print_q("min_luminance", metadata->min_luminance, '/');
        print_q("max_luminance", metadata->max_luminance, '/');
    }
}

static void print_context_light_level(AVTextFormatContext *tfc,
                                      const AVContentLightMetadata *metadata)
{
    print_int("max_content", metadata->MaxCLL);
    print_int("max_average", metadata->MaxFALL);
}

static void print_dovi_metadata(AVTextFormatContext *tfc, const AVDOVIMetadata *dovi)
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

        avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST);

        for (int c = 0; c < 3; c++) {
            const AVDOVIReshapingCurve *curve = &mapping->curves[c];
            avtext_print_section_header(tfc, "Reshaping curve", SECTION_ID_FRAME_SIDE_DATA_COMPONENT);

            print_list_fmt("pivots", "%"PRIu16, curve->num_pivots, 1, curve->pivots[idx]);

            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST);
            for (int i = 0; i < curve->num_pivots - 1; i++) {
                AVBPrint piece_buf;

                av_bprint_init(&piece_buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
                switch (curve->mapping_idc[i]) {
                case AV_DOVI_MAPPING_POLYNOMIAL:
                    av_bprintf(&piece_buf, "Polynomial");
                    break;
                case AV_DOVI_MAPPING_MMR:
                    av_bprintf(&piece_buf, "MMR");
                    break;
                default:
                    av_bprintf(&piece_buf, "Unknown");
                    break;
                }
                av_bprintf(&piece_buf, " mapping");

                avtext_print_section_header(tfc, piece_buf.str, SECTION_ID_FRAME_SIDE_DATA_PIECE);
                print_int("mapping_idc", curve->mapping_idc[i]);
                switch (curve->mapping_idc[i]) {
                case AV_DOVI_MAPPING_POLYNOMIAL:
                    print_str("mapping_idc_name",   "polynomial");
                    print_int("poly_order",         curve->poly_order[i]);
                    print_list_fmt("poly_coef", "%"PRIi64,
                                   curve->poly_order[i] + 1, 1,
                                   curve->poly_coef[i][idx]);
                    break;
                case AV_DOVI_MAPPING_MMR:
                    print_str("mapping_idc_name",   "mmr");
                    print_int("mmr_order",          curve->mmr_order[i]);
                    print_int("mmr_constant",       curve->mmr_constant[i]);
                    print_list_fmt("mmr_coef", "%"PRIi64,
                                   curve->mmr_order[i], 7,
                                   curve->mmr_coef[i][idx][idx2]);
                    break;
                default:
                    print_str("mapping_idc_name",   "unknown");
                    break;
                }

                // SECTION_ID_FRAME_SIDE_DATA_PIECE
                avtext_print_section_footer(tfc);
            }

            // SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST
            avtext_print_section_footer(tfc);

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
            avtext_print_section_footer(tfc);
        }

        // SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST
        avtext_print_section_footer(tfc);

        // color metadata
        print_int("dm_metadata_id",         color->dm_metadata_id);
        print_int("scene_refresh_flag",     color->scene_refresh_flag);
        print_list_fmt("ycc_to_rgb_matrix", "%d/%d",
                       FF_ARRAY_ELEMS(color->ycc_to_rgb_matrix), 1,
                       color->ycc_to_rgb_matrix[idx].num,
                       color->ycc_to_rgb_matrix[idx].den);
        print_list_fmt("ycc_to_rgb_offset", "%d/%d",
                       FF_ARRAY_ELEMS(color->ycc_to_rgb_offset), 1,
                       color->ycc_to_rgb_offset[idx].num,
                       color->ycc_to_rgb_offset[idx].den);
        print_list_fmt("rgb_to_lms_matrix", "%d/%d",
                       FF_ARRAY_ELEMS(color->rgb_to_lms_matrix), 1,
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

static void print_dynamic_hdr10_plus(AVTextFormatContext *tfc, const AVDynamicHDRPlus *metadata)
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

static void print_dynamic_hdr_vivid(AVTextFormatContext *tfc, const AVDynamicHDRVivid *metadata)
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
        if (params->tone_mapping_mode_flag) {
            print_int("tone_mapping_param_num", params->tone_mapping_param_num);
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

                    for (int j = 0; j < tm_params->three_Spline_num; j++) {
                        const AVHDRVivid3SplineParams *three_spline = &tm_params->three_spline[j];
                        print_int("3Spline_TH_mode", three_spline->th_mode);
                        if (three_spline->th_mode == 0 || three_spline->th_mode == 2)
                            print_q("3Spline_TH_enable_MB", three_spline->th_enable_mb, '/');
                        print_q("3Spline_TH_enable", three_spline->th_enable, '/');
                        print_q("3Spline_TH_Delta1", three_spline->th_delta1, '/');
                        print_q("3Spline_TH_Delta2", three_spline->th_delta2, '/');
                        print_q("3Spline_enable_Strength", three_spline->enable_strength, '/');
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

static void print_ambient_viewing_environment(AVTextFormatContext *tfc,
                                              const AVAmbientViewingEnvironment *env)
{
    if (!env)
        return;

    print_q("ambient_illuminance", env->ambient_illuminance, '/');
    print_q("ambient_light_x",     env->ambient_light_x,     '/');
    print_q("ambient_light_y",     env->ambient_light_y,     '/');
}

static void print_film_grain_params(AVTextFormatContext *tfc,
                                    const AVFilmGrainParams *fgp)
{
    const char *color_range, *color_primaries, *color_trc, *color_space;
    const char *const film_grain_type_names[] = {
        [AV_FILM_GRAIN_PARAMS_NONE] = "none",
        [AV_FILM_GRAIN_PARAMS_AV1]  = "av1",
        [AV_FILM_GRAIN_PARAMS_H274] = "h274",
    };

    AVBPrint pbuf;
    if (!fgp || fgp->type >= FF_ARRAY_ELEMS(film_grain_type_names))
        return;

    color_range     = av_color_range_name(fgp->color_range);
    color_primaries = av_color_primaries_name(fgp->color_primaries);
    color_trc       = av_color_transfer_name(fgp->color_trc);
    color_space     = av_color_space_name(fgp->color_space);

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);
    print_str("type", film_grain_type_names[fgp->type]);
    print_fmt("seed", "%"PRIu64, fgp->seed);
    print_int("width", fgp->width);
    print_int("height", fgp->height);
    print_int("subsampling_x", fgp->subsampling_x);
    print_int("subsampling_y", fgp->subsampling_y);
    print_str("color_range", color_range ? color_range : "unknown");
    print_str("color_primaries", color_primaries ? color_primaries : "unknown");
    print_str("color_trc", color_trc ? color_trc : "unknown");
    print_str("color_space", color_space ? color_space : "unknown");

    switch (fgp->type) {
    case AV_FILM_GRAIN_PARAMS_NONE:
        break;
    case AV_FILM_GRAIN_PARAMS_AV1: {
        const AVFilmGrainAOMParams *aom = &fgp->codec.aom;
        const int num_ar_coeffs_y = 2 * aom->ar_coeff_lag * (aom->ar_coeff_lag + 1);
        const int num_ar_coeffs_uv = num_ar_coeffs_y + !!aom->num_y_points;
        print_int("chroma_scaling_from_luma", aom->chroma_scaling_from_luma);
        print_int("scaling_shift", aom->scaling_shift);
        print_int("ar_coeff_lag", aom->ar_coeff_lag);
        print_int("ar_coeff_shift", aom->ar_coeff_shift);
        print_int("grain_scale_shift", aom->grain_scale_shift);
        print_int("overlap_flag", aom->overlap_flag);
        print_int("limit_output_range", aom->limit_output_range);

        avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST);

        if (aom->num_y_points) {
            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT);

            print_int("bit_depth_luma", fgp->bit_depth_luma);
            print_list_fmt("y_points_value", "%"PRIu8, aom->num_y_points, 1, aom->y_points[idx][0]);
            print_list_fmt("y_points_scaling", "%"PRIu8, aom->num_y_points, 1, aom->y_points[idx][1]);
            print_list_fmt("ar_coeffs_y", "%"PRId8, num_ar_coeffs_y, 1, aom->ar_coeffs_y[idx]);

            // SECTION_ID_FRAME_SIDE_DATA_COMPONENT
            avtext_print_section_footer(tfc);
        }

        for (int uv = 0; uv < 2; uv++) {
            if (!aom->num_uv_points[uv] && !aom->chroma_scaling_from_luma)
                continue;

            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT);

            print_int("bit_depth_chroma", fgp->bit_depth_chroma);
            print_list_fmt("uv_points_value", "%"PRIu8, aom->num_uv_points[uv], 1, aom->uv_points[uv][idx][0]);
            print_list_fmt("uv_points_scaling", "%"PRIu8, aom->num_uv_points[uv], 1, aom->uv_points[uv][idx][1]);
            print_list_fmt("ar_coeffs_uv", "%"PRId8, num_ar_coeffs_uv, 1, aom->ar_coeffs_uv[uv][idx]);
            print_int("uv_mult", aom->uv_mult[uv]);
            print_int("uv_mult_luma", aom->uv_mult_luma[uv]);
            print_int("uv_offset", aom->uv_offset[uv]);

            // SECTION_ID_FRAME_SIDE_DATA_COMPONENT
            avtext_print_section_footer(tfc);
        }

        // SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST
        avtext_print_section_footer(tfc);
        break;
    }
    case AV_FILM_GRAIN_PARAMS_H274: {
        const AVFilmGrainH274Params *h274 = &fgp->codec.h274;
        print_int("model_id", h274->model_id);
        print_int("blending_mode_id", h274->blending_mode_id);
        print_int("log2_scale_factor", h274->log2_scale_factor);

        avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST);

        for (int c = 0; c < 3; c++) {
            if (!h274->component_model_present[c])
                continue;

            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_COMPONENT);
            print_int(c ? "bit_depth_chroma" : "bit_depth_luma", c ? fgp->bit_depth_chroma : fgp->bit_depth_luma);

            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST);
            for (int i = 0; i < h274->num_intensity_intervals[c]; i++) {

                avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_PIECE);
                print_int("intensity_interval_lower_bound", h274->intensity_interval_lower_bound[c][i]);
                print_int("intensity_interval_upper_bound", h274->intensity_interval_upper_bound[c][i]);
                print_list_fmt("comp_model_value", "%"PRId16, h274->num_model_values[c], 1, h274->comp_model_value[c][i][idx]);

                // SECTION_ID_FRAME_SIDE_DATA_PIECE
                avtext_print_section_footer(tfc);
            }

            // SECTION_ID_FRAME_SIDE_DATA_PIECE_LIST
            avtext_print_section_footer(tfc);

            // SECTION_ID_FRAME_SIDE_DATA_COMPONENT
            avtext_print_section_footer(tfc);
        }

        // SECTION_ID_FRAME_SIDE_DATA_COMPONENT_LIST
        avtext_print_section_footer(tfc);
        break;
    }
    }

    av_bprint_finalize(&pbuf, NULL);
}

static void print_pkt_side_data(AVTextFormatContext *tfc,
                                AVCodecParameters *par,
                                const AVPacketSideData *sd,
                                SectionID id_data)
{
    const char *name = av_packet_side_data_name(sd->type);

    avtext_print_section_header(tfc, sd, id_data);
    print_str("side_data_type", name ? name : "unknown");
    if (sd->type == AV_PKT_DATA_DISPLAYMATRIX && sd->size >= 9*4) {
        print_displaymatrix(tfc, (const int32_t*)sd->data);
    } else if (sd->type == AV_PKT_DATA_STEREO3D) {
        const AVStereo3D *stereo = (AVStereo3D *)sd->data;
        print_str("type", av_stereo3d_type_name(stereo->type));
        print_int("inverted", !!(stereo->flags & AV_STEREO3D_FLAG_INVERT));
        print_str("view", av_stereo3d_view_name(stereo->view));
        print_str("primary_eye", av_stereo3d_primary_eye_name(stereo->primary_eye));
        print_int("baseline", stereo->baseline);
        print_q("horizontal_disparity_adjustment", stereo->horizontal_disparity_adjustment, '/');
        print_q("horizontal_field_of_view", stereo->horizontal_field_of_view, '/');
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
        print_mastering_display_metadata(tfc, (AVMasteringDisplayMetadata *)sd->data);
    } else if (sd->type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL) {
        print_context_light_level(tfc, (AVContentLightMetadata *)sd->data);
    } else if (sd->type == AV_PKT_DATA_AMBIENT_VIEWING_ENVIRONMENT) {
        print_ambient_viewing_environment(
            tfc, (const AVAmbientViewingEnvironment *)sd->data);
    } else if (sd->type == AV_PKT_DATA_DYNAMIC_HDR10_PLUS) {
        AVDynamicHDRPlus *metadata = (AVDynamicHDRPlus *)sd->data;
        print_dynamic_hdr10_plus(tfc, metadata);
    } else if (sd->type == AV_PKT_DATA_DOVI_CONF) {
        AVDOVIDecoderConfigurationRecord *dovi = (AVDOVIDecoderConfigurationRecord *)sd->data;
        const char *comp = "unknown";
        print_int("dv_version_major", dovi->dv_version_major);
        print_int("dv_version_minor", dovi->dv_version_minor);
        print_int("dv_profile", dovi->dv_profile);
        print_int("dv_level", dovi->dv_level);
        print_int("rpu_present_flag", dovi->rpu_present_flag);
        print_int("el_present_flag", dovi->el_present_flag);
        print_int("bl_present_flag", dovi->bl_present_flag);
        print_int("dv_bl_signal_compatibility_id", dovi->dv_bl_signal_compatibility_id);
        switch (dovi->dv_md_compression)
        {
            case AV_DOVI_COMPRESSION_NONE:     comp = "none";     break;
            case AV_DOVI_COMPRESSION_LIMITED:  comp = "limited";  break;
            case AV_DOVI_COMPRESSION_RESERVED: comp = "reserved"; break;
            case AV_DOVI_COMPRESSION_EXTENDED: comp = "extended"; break;
        }
        print_str("dv_md_compression", comp);
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
            avtext_print_data(tfc, "data", sd->data, sd->size);
        avtext_print_data_hash(tfc, "data_hash", sd->data, sd->size);
    } else if (sd->type == AV_PKT_DATA_FRAME_CROPPING && sd->size >= sizeof(uint32_t) * 4) {
        print_int("crop_top",    AV_RL32(sd->data));
        print_int("crop_bottom", AV_RL32(sd->data + 4));
        print_int("crop_left",   AV_RL32(sd->data + 8));
        print_int("crop_right",  AV_RL32(sd->data + 12));
    } else if (sd->type == AV_PKT_DATA_AFD && sd->size > 0) {
        print_int("active_format", *sd->data);
    }
}

static void print_private_data(AVTextFormatContext *tfc, void *priv_data)
{
    const AVOption *opt = NULL;
    while (opt = av_opt_next(priv_data, opt)) {
        uint8_t *str;
        if (!(opt->flags & AV_OPT_FLAG_EXPORT)) continue;
        if (av_opt_get(priv_data, opt->name, 0, &str) >= 0) {
            print_str(opt->name, str);
            av_free(str);
        }
    }
}

static void print_pixel_format(AVTextFormatContext *tfc, enum AVPixelFormat pix_fmt)
{
    const char *s = av_get_pix_fmt_name(pix_fmt);
    enum AVPixelFormat swapped_pix_fmt;

    if (!s) {
        print_str_opt("pix_fmt", "unknown");
    } else if (!do_bitexact ||
               (swapped_pix_fmt = av_pix_fmt_swap_endianness(pix_fmt)) == AV_PIX_FMT_NONE) {
        print_str    ("pix_fmt", s);
    } else {
        const char *s2 = av_get_pix_fmt_name(swapped_pix_fmt);
        char buf[128];
        size_t i = 0;

        while (s[i] && s[i] == s2[i] && i < sizeof(buf) - 1) {
            buf[i] = s[i];
            i++;
        }
        buf[i] = '\0';

        print_str    ("pix_fmt", buf);
    }
}

static void print_color_range(AVTextFormatContext *tfc, enum AVColorRange color_range)
{
    const char *val = av_color_range_name(color_range);
    if (!val || color_range == AVCOL_RANGE_UNSPECIFIED) {
        print_str_opt("color_range", "unknown");
    } else {
        print_str("color_range", val);
    }
}

static void print_color_space(AVTextFormatContext *tfc, enum AVColorSpace color_space)
{
    const char *val = av_color_space_name(color_space);
    if (!val || color_space == AVCOL_SPC_UNSPECIFIED) {
        print_str_opt("color_space", "unknown");
    } else {
        print_str("color_space", val);
    }
}

static void print_primaries(AVTextFormatContext *tfc, enum AVColorPrimaries color_primaries)
{
    const char *val = av_color_primaries_name(color_primaries);
    if (!val || color_primaries == AVCOL_PRI_UNSPECIFIED) {
        print_str_opt("color_primaries", "unknown");
    } else {
        print_str("color_primaries", val);
    }
}

static void print_color_trc(AVTextFormatContext *tfc, enum AVColorTransferCharacteristic color_trc)
{
    const char *val = av_color_transfer_name(color_trc);
    if (!val || color_trc == AVCOL_TRC_UNSPECIFIED) {
        print_str_opt("color_transfer", "unknown");
    } else {
        print_str("color_transfer", val);
    }
}

static void print_chroma_location(AVTextFormatContext *tfc, enum AVChromaLocation chroma_location)
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
        ff_mutex_lock(&log_mutex);
    for (i=0; i<log_buffer_size; i++) {
        av_freep(&log_buffer[i].context_name);
        av_freep(&log_buffer[i].parent_name);
        av_freep(&log_buffer[i].log_message);
    }
    log_buffer_size = 0;
    if(need_lock)
        ff_mutex_unlock(&log_mutex);
}

static int show_log(AVTextFormatContext *tfc, int section_ids, int section_id, int log_level)
{
    int i;
    ff_mutex_lock(&log_mutex);
    if (!log_buffer_size) {
        ff_mutex_unlock(&log_mutex);
        return 0;
    }
    avtext_print_section_header(tfc, NULL, section_ids);

    for (i=0; i<log_buffer_size; i++) {
        if (log_buffer[i].log_level <= log_level) {
            avtext_print_section_header(tfc, NULL, section_id);
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
            avtext_print_section_footer(tfc);
        }
    }
    clear_log(0);
    ff_mutex_unlock(&log_mutex);

    avtext_print_section_footer(tfc);

    return 0;
}

static void show_packet(AVTextFormatContext *tfc, InputFile *ifile, AVPacket *pkt, int packet_idx)
{
    AVStream *st = ifile->streams[pkt->stream_index].st;
    AVBPrint pbuf;
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(tfc, NULL, SECTION_ID_PACKET);

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
    print_fmt("flags", "%c%c%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_',
              pkt->flags & AV_PKT_FLAG_DISCARD ? 'D' : '_',
              pkt->flags & AV_PKT_FLAG_CORRUPT ? 'C' : '_');
    if (do_show_data)
        avtext_print_data(tfc, "data", pkt->data, pkt->size);
    avtext_print_data_hash(tfc, "data_hash", pkt->data, pkt->size);

    if (pkt->side_data_elems) {
        size_t size;
        const uint8_t *side_metadata;

        side_metadata = av_packet_get_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA, &size);
        if (side_metadata && size && do_show_packet_tags) {
            AVDictionary *dict = NULL;
            if (av_packet_unpack_dictionary(side_metadata, size, &dict) >= 0)
                show_tags(tfc, dict, SECTION_ID_PACKET_TAGS);
            av_dict_free(&dict);
        }

        avtext_print_section_header(tfc, NULL, SECTION_ID_PACKET_SIDE_DATA_LIST);
        for (int i = 0; i < pkt->side_data_elems; i++) {
            print_pkt_side_data(tfc, st->codecpar, &pkt->side_data[i],
                                SECTION_ID_PACKET_SIDE_DATA);
            avtext_print_section_footer(tfc);
        }
        avtext_print_section_footer(tfc);
    }

    avtext_print_section_footer(tfc);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void show_subtitle(AVTextFormatContext *tfc, AVSubtitle *sub, AVStream *stream,
                          AVFormatContext *fmt_ctx)
{
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(tfc, NULL, SECTION_ID_SUBTITLE);

    print_str ("media_type",         "subtitle");
    print_ts  ("pts",                 sub->pts);
    print_time("pts_time",            sub->pts, &AV_TIME_BASE_Q);
    print_int ("format",              sub->format);
    print_int ("start_display_time",  sub->start_display_time);
    print_int ("end_display_time",    sub->end_display_time);
    print_int ("num_rects",           sub->num_rects);

    avtext_print_section_footer(tfc);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static void print_frame_side_data(AVTextFormatContext *tfc,
                                  const AVFrame *frame,
                                  const AVStream *stream)
{
    avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_LIST);

    for (int i = 0; i < frame->nb_side_data; i++) {
        const AVFrameSideData *sd = frame->side_data[i];
        const char *name;

        avtext_print_section_header(tfc, sd, SECTION_ID_FRAME_SIDE_DATA);
        name = av_frame_side_data_name(sd->type);
        print_str("side_data_type", name ? name : "unknown");
        if (sd->type == AV_FRAME_DATA_DISPLAYMATRIX && sd->size >= 9*4) {
            print_displaymatrix(tfc, (const int32_t*)sd->data);
        } else if (sd->type == AV_FRAME_DATA_AFD && sd->size > 0) {
            print_int("active_format", *sd->data);
        } else if (sd->type == AV_FRAME_DATA_GOP_TIMECODE && sd->size >= 8) {
            char tcbuf[AV_TIMECODE_STR_SIZE];
            av_timecode_make_mpeg_tc_string(tcbuf, *(int64_t *)(sd->data));
            print_str("timecode", tcbuf);
        } else if (sd->type == AV_FRAME_DATA_S12M_TIMECODE && sd->size == 16) {
            uint32_t *tc = (uint32_t*)sd->data;
            int m = FFMIN(tc[0],3);
            avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_TIMECODE_LIST);
            for (int j = 1; j <= m ; j++) {
                char tcbuf[AV_TIMECODE_STR_SIZE];
                av_timecode_make_smpte_tc_string2(tcbuf, stream->avg_frame_rate, tc[j], 0, 0);
                avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME_SIDE_DATA_TIMECODE);
                print_str("value", tcbuf);
                avtext_print_section_footer(tfc);
            }
            avtext_print_section_footer(tfc);
        } else if (sd->type == AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) {
            print_mastering_display_metadata(tfc, (AVMasteringDisplayMetadata *)sd->data);
        } else if (sd->type == AV_FRAME_DATA_DYNAMIC_HDR_PLUS) {
            AVDynamicHDRPlus *metadata = (AVDynamicHDRPlus *)sd->data;
            print_dynamic_hdr10_plus(tfc, metadata);
        } else if (sd->type == AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) {
            print_context_light_level(tfc, (AVContentLightMetadata *)sd->data);
        } else if (sd->type == AV_FRAME_DATA_ICC_PROFILE) {
            const AVDictionaryEntry *tag = av_dict_get(sd->metadata, "name", NULL, AV_DICT_MATCH_CASE);
            if (tag)
                print_str(tag->key, tag->value);
            print_int("size", sd->size);
        } else if (sd->type == AV_FRAME_DATA_DOVI_METADATA) {
            print_dovi_metadata(tfc, (const AVDOVIMetadata *)sd->data);
        } else if (sd->type == AV_FRAME_DATA_DYNAMIC_HDR_VIVID) {
            AVDynamicHDRVivid *metadata = (AVDynamicHDRVivid *)sd->data;
            print_dynamic_hdr_vivid(tfc, metadata);
        } else if (sd->type == AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT) {
            print_ambient_viewing_environment(tfc, (const AVAmbientViewingEnvironment *)sd->data);
        } else if (sd->type == AV_FRAME_DATA_FILM_GRAIN_PARAMS) {
            AVFilmGrainParams *fgp = (AVFilmGrainParams *)sd->data;
            print_film_grain_params(tfc, fgp);
        } else if (sd->type == AV_FRAME_DATA_VIEW_ID) {
            print_int("view_id", *(int*)sd->data);
        }
        avtext_print_section_footer(tfc);
    }
    avtext_print_section_footer(tfc);
}

static void show_frame(AVTextFormatContext *tfc, AVFrame *frame, AVStream *stream,
                       AVFormatContext *fmt_ctx)
{
    FrameData *fd = frame->opaque_ref ? (FrameData*)frame->opaque_ref->data : NULL;
    AVBPrint pbuf;
    char val_str[128];
    const char *s;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(tfc, NULL, SECTION_ID_FRAME);

    s = av_get_media_type_string(stream->codecpar->codec_type);
    if (s) print_str    ("media_type", s);
    else   print_str_opt("media_type", "unknown");
    print_int("stream_index",           stream->index);
    print_int("key_frame",           !!(frame->flags & AV_FRAME_FLAG_KEY));
    print_ts  ("pts",                   frame->pts);
    print_time("pts_time",              frame->pts, &stream->time_base);
    print_ts  ("pkt_dts",               frame->pkt_dts);
    print_time("pkt_dts_time",          frame->pkt_dts, &stream->time_base);
    print_ts  ("best_effort_timestamp", frame->best_effort_timestamp);
    print_time("best_effort_timestamp_time", frame->best_effort_timestamp, &stream->time_base);
    print_duration_ts  ("duration",          frame->duration);
    print_duration_time("duration_time",     frame->duration, &stream->time_base);
    if (fd && fd->pkt_pos != -1)  print_fmt    ("pkt_pos", "%"PRId64, fd->pkt_pos);
    else                          print_str_opt("pkt_pos", "N/A");
    if (fd && fd->pkt_size != -1) print_val    ("pkt_size", fd->pkt_size, unit_byte_str);
    else                          print_str_opt("pkt_size", "N/A");

    switch (stream->codecpar->codec_type) {
        AVRational sar;

    case AVMEDIA_TYPE_VIDEO:
        print_int("width",                  frame->width);
        print_int("height",                 frame->height);
        print_int("crop_top",               frame->crop_top);
        print_int("crop_bottom",            frame->crop_bottom);
        print_int("crop_left",              frame->crop_left);
        print_int("crop_right",             frame->crop_right);
        print_pixel_format(tfc, frame->format);
        sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, frame);
        if (sar.num) {
            print_q("sample_aspect_ratio", sar, ':');
        } else {
            print_str_opt("sample_aspect_ratio", "N/A");
        }
        print_fmt("pict_type",              "%c", av_get_picture_type_char(frame->pict_type));
        print_int("interlaced_frame",       !!(frame->flags & AV_FRAME_FLAG_INTERLACED));
        print_int("top_field_first",        !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));
        print_int("lossless",               !!(frame->flags & AV_FRAME_FLAG_LOSSLESS));
        print_int("repeat_pict",            frame->repeat_pict);

        print_color_range(tfc, frame->color_range);
        print_color_space(tfc, frame->colorspace);
        print_primaries(tfc, frame->color_primaries);
        print_color_trc(tfc, frame->color_trc);
        print_chroma_location(tfc, frame->chroma_location);
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
        show_tags(tfc, frame->metadata, SECTION_ID_FRAME_TAGS);
    if (do_show_log)
        show_log(tfc, SECTION_ID_FRAME_LOGS, SECTION_ID_FRAME_LOG, do_show_log);
    if (frame->nb_side_data)
        print_frame_side_data(tfc, frame, stream);

    avtext_print_section_footer(tfc);

    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);
}

static av_always_inline int process_frame(AVTextFormatContext *tfc,
                                          InputFile *ifile,
                                          AVFrame *frame, const AVPacket *pkt,
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
                show_subtitle(tfc, &sub, ifile->streams[pkt->stream_index].st, fmt_ctx);
            else
                show_frame(tfc, frame, ifile->streams[pkt->stream_index].st, fmt_ctx);

        if (!is_sub && do_analyze_frames) {
            for (int i = 0; i < frame->nb_side_data; i++) {
                if (frame->side_data[i]->type == AV_FRAME_DATA_A53_CC)
                    streams_with_closed_captions[pkt->stream_index] = 1;
                else if (frame->side_data[i]->type == AV_FRAME_DATA_FILM_GRAIN_PARAMS)
                    streams_with_film_grain[pkt->stream_index] = 1;
            }
        }

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

static int read_interval_packets(AVTextFormatContext *tfc, InputFile *ifile,
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
            REALLOCZ_ARRAY_STREAM(streams_with_closed_captions,   nb_streams, fmt_ctx->nb_streams);
            REALLOCZ_ARRAY_STREAM(streams_with_film_grain,        nb_streams, fmt_ctx->nb_streams);
            nb_streams = fmt_ctx->nb_streams;
        }
        if (selected_streams[pkt->stream_index]) {
            AVRational tb = ifile->streams[pkt->stream_index].st->time_base;
            int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;

            if (pts != AV_NOPTS_VALUE)
                *cur_ts = av_rescale_q(pts, tb, AV_TIME_BASE_Q);

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
                    show_packet(tfc, ifile, pkt, i++);
                nb_streams_packets[pkt->stream_index]++;
            }
            if (do_read_frames) {
                int packet_new = 1;
                FrameData *fd;

                pkt->opaque_ref = av_buffer_allocz(sizeof(*fd));
                if (!pkt->opaque_ref) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
                fd = (FrameData*)pkt->opaque_ref->data;
                fd->pkt_pos  = pkt->pos;
                fd->pkt_size = pkt->size;

                while (process_frame(tfc, ifile, frame, pkt, &packet_new) > 0);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_unref(pkt);
    //Flush remaining frames that are cached in the decoder
    for (i = 0; i < ifile->nb_streams; i++) {
        pkt->stream_index = i;
        if (do_read_frames) {
            while (process_frame(tfc, ifile, frame, pkt, &(int){1}) > 0);
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

static int read_packets(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;
    int64_t cur_ts = fmt_ctx->start_time;

    if (read_intervals_nb == 0) {
        ReadInterval interval = (ReadInterval) { .has_start = 0, .has_end = 0 };
        ret = read_interval_packets(tfc, ifile, &interval, &cur_ts);
    } else {
        for (i = 0; i < read_intervals_nb; i++) {
            ret = read_interval_packets(tfc, ifile, &read_intervals[i], &cur_ts);
            if (ret < 0)
                break;
        }
    }

    return ret;
}

static void print_dispositions(AVTextFormatContext *tfc, uint32_t disposition, SectionID section_id)
{
    avtext_print_section_header(tfc, NULL, section_id);
    for (int i = 0; i < sizeof(disposition) * CHAR_BIT; i++) {
        const char *disposition_str = av_disposition_to_string(1U << i);

        if (disposition_str)
            print_int(disposition_str, !!(disposition & (1U << i)));
    }
    avtext_print_section_footer(tfc);
}

#define IN_PROGRAM 1
#define IN_STREAM_GROUP 2

static int show_stream(AVTextFormatContext *tfc, AVFormatContext *fmt_ctx, int stream_idx, InputStream *ist, int container)
{
    AVStream *stream = ist->st;
    AVCodecParameters *par;
    AVCodecContext *dec_ctx;
    char val_str[128];
    const char *s;
    AVRational sar, dar;
    AVBPrint pbuf;
    const AVCodecDescriptor *cd;
    const SectionID section_header[] = {
        SECTION_ID_STREAM,
        SECTION_ID_PROGRAM_STREAM,
        SECTION_ID_STREAM_GROUP_STREAM,
    };
    const SectionID section_disposition[] = {
        SECTION_ID_STREAM_DISPOSITION,
        SECTION_ID_PROGRAM_STREAM_DISPOSITION,
        SECTION_ID_STREAM_GROUP_STREAM_DISPOSITION,
    };
    const SectionID section_tags[] = {
        SECTION_ID_STREAM_TAGS,
        SECTION_ID_PROGRAM_STREAM_TAGS,
        SECTION_ID_STREAM_GROUP_STREAM_TAGS,
    };
    int ret = 0;
    const char *profile = NULL;

    av_assert0(container < FF_ARRAY_ELEMS(section_header));

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(tfc, NULL, section_header[container]);

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
        if (par->profile != AV_PROFILE_UNKNOWN) {
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

            if (do_analyze_frames) {
                print_int("closed_captions", streams_with_closed_captions[stream->index]);
                print_int("film_grain",      streams_with_film_grain[stream->index]);
            }
        }
        print_int("has_b_frames", par->video_delay);
        sar = av_guess_sample_aspect_ratio(fmt_ctx, stream, NULL);
        if (sar.num) {
            print_q("sample_aspect_ratio", sar, ':');
            av_reduce(&dar.num, &dar.den,
                      (int64_t) par->width  * sar.num,
                      (int64_t) par->height * sar.den,
                      1024*1024);
            print_q("display_aspect_ratio", dar, ':');
        } else {
            print_str_opt("sample_aspect_ratio", "N/A");
            print_str_opt("display_aspect_ratio", "N/A");
        }
        print_pixel_format(tfc, par->format);
        print_int("level",   par->level);

        print_color_range(tfc, par->color_range);
        print_color_space(tfc, par->color_space);
        print_color_trc(tfc, par->color_trc);
        print_primaries(tfc, par->color_primaries);
        print_chroma_location(tfc, par->chroma_location);

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

        print_int("initial_padding", par->initial_padding);
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

    if (show_private_data) {
        if (dec_ctx && dec_ctx->codec->priv_class)
            print_private_data(tfc, dec_ctx->priv_data);
        if (fmt_ctx->iformat->priv_class)
            print_private_data(tfc, fmt_ctx->priv_data);
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
        avtext_print_data(tfc, "extradata", par->extradata,
                                          par->extradata_size);

    if (par->extradata_size > 0) {
        print_int("extradata_size", par->extradata_size);
        avtext_print_data_hash(tfc, "extradata_hash", par->extradata,
                                                    par->extradata_size);
    }

    /* Print disposition information */
    if (do_show_stream_disposition) {
        av_assert0(container < FF_ARRAY_ELEMS(section_disposition));
        print_dispositions(tfc, stream->disposition, section_disposition[container]);
    }

    if (do_show_stream_tags) {
        av_assert0(container < FF_ARRAY_ELEMS(section_tags));
        ret = show_tags(tfc, stream->metadata, section_tags[container]);
    }

    if (stream->codecpar->nb_coded_side_data) {
        avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_SIDE_DATA_LIST);
        for (int i = 0; i < stream->codecpar->nb_coded_side_data; i++) {
            print_pkt_side_data(tfc, stream->codecpar, &stream->codecpar->coded_side_data[i],
                                SECTION_ID_STREAM_SIDE_DATA);
            avtext_print_section_footer(tfc);
        }
        avtext_print_section_footer(tfc);
    }

    avtext_print_section_footer(tfc);
    av_bprint_finalize(&pbuf, NULL);
    fflush(stdout);

    return ret;
}

static int show_streams(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAMS);
    for (i = 0; i < ifile->nb_streams; i++)
        if (selected_streams[i]) {
            ret = show_stream(tfc, fmt_ctx, i, &ifile->streams[i], 0);
            if (ret < 0)
                break;
        }
    avtext_print_section_footer(tfc);

    return ret;
}

static int show_program(AVTextFormatContext *tfc, InputFile *ifile, AVProgram *program)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_PROGRAM);
    print_int("program_id", program->id);
    print_int("program_num", program->program_num);
    print_int("nb_streams", program->nb_stream_indexes);
    print_int("pmt_pid", program->pmt_pid);
    print_int("pcr_pid", program->pcr_pid);
    if (do_show_program_tags)
        ret = show_tags(tfc, program->metadata, SECTION_ID_PROGRAM_TAGS);
    if (ret < 0)
        goto end;

    avtext_print_section_header(tfc, NULL, SECTION_ID_PROGRAM_STREAMS);
    for (i = 0; i < program->nb_stream_indexes; i++) {
        if (selected_streams[program->stream_index[i]]) {
            ret = show_stream(tfc, fmt_ctx, program->stream_index[i], &ifile->streams[program->stream_index[i]], IN_PROGRAM);
            if (ret < 0)
                break;
        }
    }
    avtext_print_section_footer(tfc);

end:
    avtext_print_section_footer(tfc);
    return ret;
}

static int show_programs(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_PROGRAMS);
    for (i = 0; i < fmt_ctx->nb_programs; i++) {
        AVProgram *program = fmt_ctx->programs[i];
        if (!program)
            continue;
        ret = show_program(tfc, ifile, program);
        if (ret < 0)
            break;
    }
    avtext_print_section_footer(tfc);
    return ret;
}

static void print_tile_grid_params(AVTextFormatContext *tfc, const AVStreamGroup *stg,
                                   const AVStreamGroupTileGrid *tile_grid)
{
    avtext_print_section_header(tfc, stg, SECTION_ID_STREAM_GROUP_COMPONENT);
    print_int("nb_tiles",          tile_grid->nb_tiles);
    print_int("coded_width",       tile_grid->coded_width);
    print_int("coded_height",      tile_grid->coded_height);
    print_int("horizontal_offset", tile_grid->horizontal_offset);
    print_int("vertical_offset",   tile_grid->vertical_offset);
    print_int("width",             tile_grid->width);
    print_int("height",            tile_grid->height);
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_SUBCOMPONENTS);
    for (int i = 0; i < tile_grid->nb_tiles; i++) {
        avtext_print_section_header(tfc, "tile_offset", SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
        print_int("stream_index",           tile_grid->offsets[i].idx);
        print_int("tile_horizontal_offset", tile_grid->offsets[i].horizontal);
        print_int("tile_vertical_offset",   tile_grid->offsets[i].vertical);
        avtext_print_section_footer(tfc);
    }
    avtext_print_section_footer(tfc);
    avtext_print_section_footer(tfc);
}

static void print_iamf_param_definition(AVTextFormatContext *tfc, const char *name,
                                        const AVIAMFParamDefinition *param, SectionID section_id)
{
    SectionID subsection_id, parameter_section_id;
    subsection_id = sections[section_id].children_ids[0];
    av_assert0(subsection_id != -1);
    parameter_section_id = sections[subsection_id].children_ids[0];
    av_assert0(parameter_section_id != -1);
    avtext_print_section_header(tfc, "IAMF Param Definition", section_id);
    print_str("name",           name);
    print_int("nb_subblocks",   param->nb_subblocks);
    print_int("type",           param->type);
    print_int("parameter_id",   param->parameter_id);
    print_int("parameter_rate", param->parameter_rate);
    print_int("duration",       param->duration);
    print_int("constant_subblock_duration",          param->constant_subblock_duration);
    if (param->nb_subblocks > 0)
        avtext_print_section_header(tfc, NULL, subsection_id);
    for (int i = 0; i < param->nb_subblocks; i++) {
        const void *subblock = av_iamf_param_definition_get_subblock(param, i);
        switch(param->type) {
        case AV_IAMF_PARAMETER_DEFINITION_MIX_GAIN: {
            const AVIAMFMixGain *mix = subblock;
            avtext_print_section_header(tfc, "IAMF Mix Gain Parameters", parameter_section_id);
            print_int("subblock_duration",         mix->subblock_duration);
            print_int("animation_type",            mix->animation_type);
            print_q("start_point_value",           mix->start_point_value, '/');
            print_q("end_point_value",             mix->end_point_value, '/');
            print_q("control_point_value",         mix->control_point_value, '/');
            print_q("control_point_relative_time", mix->control_point_relative_time, '/');
            avtext_print_section_footer(tfc); // parameter_section_id
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_DEMIXING: {
            const AVIAMFDemixingInfo *demix = subblock;
            avtext_print_section_header(tfc, "IAMF Demixing Info", parameter_section_id);
            print_int("subblock_duration", demix->subblock_duration);
            print_int("dmixp_mode",        demix->dmixp_mode);
            avtext_print_section_footer(tfc); // parameter_section_id
            break;
        }
        case AV_IAMF_PARAMETER_DEFINITION_RECON_GAIN: {
            const AVIAMFReconGain *recon = subblock;
            avtext_print_section_header(tfc, "IAMF Recon Gain", parameter_section_id);
            print_int("subblock_duration", recon->subblock_duration);
            avtext_print_section_footer(tfc); // parameter_section_id
            break;
        }
        }
    }
    if (param->nb_subblocks > 0)
        avtext_print_section_footer(tfc); // subsection_id
    avtext_print_section_footer(tfc); // section_id
}

static void print_iamf_audio_element_params(AVTextFormatContext *tfc, const AVStreamGroup *stg,
                                            const AVIAMFAudioElement *audio_element)
{
    avtext_print_section_header(tfc, stg, SECTION_ID_STREAM_GROUP_COMPONENT);
    print_int("nb_layers",          audio_element->nb_layers);
    print_int("audio_element_type", audio_element->audio_element_type);
    print_int("default_w",          audio_element->default_w);
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_SUBCOMPONENTS);
    for (int i = 0; i < audio_element->nb_layers; i++) {
        const AVIAMFLayer *layer = audio_element->layers[i];
        char val_str[128];
        avtext_print_section_header(tfc, "IAMF Audio Layer", SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
        av_channel_layout_describe(&layer->ch_layout, val_str, sizeof(val_str));
        print_str("channel_layout", val_str);
        if (audio_element->audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_CHANNEL) {
            print_int("output_gain_flags", layer->output_gain_flags);
            print_q("output_gain",         layer->output_gain, '/');
        } else if (audio_element->audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_SCENE)
            print_int("ambisonics_mode",   layer->ambisonics_mode);
        avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBCOMPONENT
    }
    if (audio_element->demixing_info)
        print_iamf_param_definition(tfc, "demixing_info", audio_element->demixing_info,
                                    SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
    if (audio_element->recon_gain_info)
        print_iamf_param_definition(tfc, "recon_gain_info", audio_element->recon_gain_info,
                                    SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBCOMPONENTS
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_COMPONENT
}

static void print_iamf_submix_params(AVTextFormatContext *tfc, const AVIAMFSubmix *submix)
{
    avtext_print_section_header(tfc, "IAMF Submix", SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
    print_int("nb_elements",    submix->nb_elements);
    print_int("nb_layouts",     submix->nb_layouts);
    print_q("default_mix_gain", submix->default_mix_gain, '/');
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_PIECES);
    for (int i = 0; i < submix->nb_elements; i++) {
        const AVIAMFSubmixElement *element = submix->elements[i];
        avtext_print_section_header(tfc, "IAMF Submix Element", SECTION_ID_STREAM_GROUP_PIECE);
        print_int("stream_id",                 element->audio_element_id);
        print_q("default_mix_gain",            element->default_mix_gain, '/');
        print_int("headphones_rendering_mode", element->headphones_rendering_mode);
        avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_SUBPIECES);
        if (element->annotations) {
            const AVDictionaryEntry *annotation = NULL;
            avtext_print_section_header(tfc, "IAMF Annotations", SECTION_ID_STREAM_GROUP_SUBPIECE);
            while (annotation = av_dict_iterate(element->annotations, annotation))
                print_str(annotation->key, annotation->value);
            avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBPIECE
        }
        if (element->element_mix_config)
            print_iamf_param_definition(tfc, "element_mix_config", element->element_mix_config,
                                        SECTION_ID_STREAM_GROUP_SUBPIECE);
        avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBPIECES
        avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_PIECE
    }
    if (submix->output_mix_config)
        print_iamf_param_definition(tfc, "output_mix_config", submix->output_mix_config,
                                    SECTION_ID_STREAM_GROUP_PIECE);
    for (int i = 0; i < submix->nb_layouts; i++) {
        const AVIAMFSubmixLayout *layout = submix->layouts[i];
        char val_str[128];
        avtext_print_section_header(tfc, "IAMF Submix Layout", SECTION_ID_STREAM_GROUP_PIECE);
        av_channel_layout_describe(&layout->sound_system, val_str, sizeof(val_str));
        print_str("sound_system",             val_str);
        print_q("integrated_loudness",        layout->integrated_loudness, '/');
        print_q("digital_peak",               layout->digital_peak, '/');
        print_q("true_peak",                  layout->true_peak, '/');
        print_q("dialogue_anchored_loudness", layout->dialogue_anchored_loudness, '/');
        print_q("album_anchored_loudness",    layout->album_anchored_loudness, '/');
        avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_PIECE
    }
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_PIECES
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBCOMPONENT
}

static void print_iamf_mix_presentation_params(AVTextFormatContext *tfc, const AVStreamGroup *stg,
                                               const AVIAMFMixPresentation *mix_presentation)
{
    avtext_print_section_header(tfc, stg, SECTION_ID_STREAM_GROUP_COMPONENT);
    print_int("nb_submixes", mix_presentation->nb_submixes);
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_SUBCOMPONENTS);
    if (mix_presentation->annotations) {
        const AVDictionaryEntry *annotation = NULL;
        avtext_print_section_header(tfc, "IAMF Annotations", SECTION_ID_STREAM_GROUP_SUBCOMPONENT);
        while (annotation = av_dict_iterate(mix_presentation->annotations, annotation))
            print_str(annotation->key, annotation->value);
        avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBCOMPONENT
    }
    for (int i = 0; i < mix_presentation->nb_submixes; i++)
        print_iamf_submix_params(tfc, mix_presentation->submixes[i]);
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_SUBCOMPONENTS
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_COMPONENT
}

static void print_stream_group_params(AVTextFormatContext *tfc, AVStreamGroup *stg)
{
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_COMPONENTS);
    if (stg->type == AV_STREAM_GROUP_PARAMS_TILE_GRID)
        print_tile_grid_params(tfc, stg, stg->params.tile_grid);
    else if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
        print_iamf_audio_element_params(tfc, stg, stg->params.iamf_audio_element);
    else if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
        print_iamf_mix_presentation_params(tfc, stg, stg->params.iamf_mix_presentation);
    avtext_print_section_footer(tfc); // SECTION_ID_STREAM_GROUP_COMPONENTS
}

static int show_stream_group(AVTextFormatContext *tfc, InputFile *ifile, AVStreamGroup *stg)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    AVBPrint pbuf;
    int i, ret = 0;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);
    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP);
    print_int("index", stg->index);
    if (fmt_ctx->iformat->flags & AVFMT_SHOW_IDS) print_fmt    ("id", "0x%"PRIx64, stg->id);
    else                                          print_str_opt("id", "N/A");
    print_int("nb_streams", stg->nb_streams);
    if (stg->type != AV_STREAM_GROUP_PARAMS_NONE)
        print_str("type", av_x_if_null(avformat_stream_group_name(stg->type), "unknown"));
    else
        print_str_opt("type", "unknown");
    if (do_show_stream_group_components)
        print_stream_group_params(tfc, stg);

    /* Print disposition information */
    if (do_show_stream_group_disposition)
        print_dispositions(tfc, stg->disposition, SECTION_ID_STREAM_GROUP_DISPOSITION);

    if (do_show_stream_group_tags)
        ret = show_tags(tfc, stg->metadata, SECTION_ID_STREAM_GROUP_TAGS);
    if (ret < 0)
        goto end;

    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUP_STREAMS);
    for (i = 0; i < stg->nb_streams; i++) {
        if (selected_streams[stg->streams[i]->index]) {
            ret = show_stream(tfc, fmt_ctx, stg->streams[i]->index, &ifile->streams[stg->streams[i]->index], IN_STREAM_GROUP);
            if (ret < 0)
                break;
        }
    }
    avtext_print_section_footer(tfc);

end:
    av_bprint_finalize(&pbuf, NULL);
    avtext_print_section_footer(tfc);
    return ret;
}

static int show_stream_groups(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAM_GROUPS);
    for (i = 0; i < fmt_ctx->nb_stream_groups; i++) {
        AVStreamGroup *stg = fmt_ctx->stream_groups[i];

        ret = show_stream_group(tfc, ifile, stg);
        if (ret < 0)
            break;
    }
    avtext_print_section_footer(tfc);
    return ret;
}

static int show_chapters(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int i, ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_CHAPTERS);
    for (i = 0; i < fmt_ctx->nb_chapters; i++) {
        AVChapter *chapter = fmt_ctx->chapters[i];

        avtext_print_section_header(tfc, NULL, SECTION_ID_CHAPTER);
        print_int("id", chapter->id);
        print_q  ("time_base", chapter->time_base, '/');
        print_int("start", chapter->start);
        print_time("start_time", chapter->start, &chapter->time_base);
        print_int("end", chapter->end);
        print_time("end_time", chapter->end, &chapter->time_base);
        if (do_show_chapter_tags)
            ret = show_tags(tfc, chapter->metadata, SECTION_ID_CHAPTER_TAGS);
        avtext_print_section_footer(tfc);
    }
    avtext_print_section_footer(tfc);

    return ret;
}

static int show_format(AVTextFormatContext *tfc, InputFile *ifile)
{
    AVFormatContext *fmt_ctx = ifile->fmt_ctx;
    int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;
    int ret = 0;

    avtext_print_section_header(tfc, NULL, SECTION_ID_FORMAT);
    print_str_validate("filename", fmt_ctx->url);
    print_int("nb_streams",       fmt_ctx->nb_streams);
    print_int("nb_programs",      fmt_ctx->nb_programs);
    print_int("nb_stream_groups", fmt_ctx->nb_stream_groups);
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
        ret = show_tags(tfc, fmt_ctx->metadata, SECTION_ID_FORMAT_TAGS);

    avtext_print_section_footer(tfc);
    fflush(stdout);
    return ret;
}

static void show_error(AVTextFormatContext *tfc, int err)
{
    avtext_print_section_header(tfc, NULL, SECTION_ID_ERROR);
    print_int("code", err);
    print_str("string", av_err2str(err));
    avtext_print_section_footer(tfc);
}

static int open_input_file(InputFile *ifile, const char *filename,
                           const char *print_filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    const AVDictionaryEntry *t = NULL;
    int scan_all_pmts_set = 0;

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx)
        return AVERROR(ENOMEM);

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
    while ((t = av_dict_iterate(format_opts, t)))
        av_log(NULL, AV_LOG_WARNING, "Option %s skipped - not known to demuxer.\n", t->key);

    if (find_stream_info) {
        AVDictionary **opts;
        int orig_nb_streams = fmt_ctx->nb_streams;

        err = setup_find_stream_info_opts(fmt_ctx, codec_opts, &opts);
        if (err < 0)
            return err;

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
            AVDictionary *opts;

            err = filter_codec_opts(codec_opts, stream->codecpar->codec_id,
                                    fmt_ctx, stream, codec, &opts, NULL);
            if (err < 0)
                exit(1);

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

            av_dict_set(&opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);

            ist->dec_ctx->pkt_timebase = stream->time_base;

            if (avcodec_open2(ist->dec_ctx, codec, &opts) < 0) {
                av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n",
                       stream->index);
                exit(1);
            }

            if ((t = av_dict_iterate(opts, NULL))) {
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

static int probe_file(AVTextFormatContext *tfc, const char *filename,
                      const char *print_filename)
{
    InputFile ifile = { 0 };
    int ret, i;
    int section_id;

    do_analyze_frames = do_analyze_frames && do_show_streams;
    do_read_frames = do_show_frames || do_count_frames || do_analyze_frames;
    do_read_packets = do_show_packets || do_count_packets;

    ret = open_input_file(&ifile, filename, print_filename);
    if (ret < 0)
        goto end;

#define CHECK_END if (ret < 0) goto end

    nb_streams = ifile.fmt_ctx->nb_streams;
    REALLOCZ_ARRAY_STREAM(nb_streams_frames,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(nb_streams_packets,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(selected_streams,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(streams_with_closed_captions,0,ifile.fmt_ctx->nb_streams);
    REALLOCZ_ARRAY_STREAM(streams_with_film_grain,0,ifile.fmt_ctx->nb_streams);

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
            tfc->formatter->flags & AV_TEXTFORMAT_FLAG_SUPPORTS_MIXED_ARRAY_CONTENT)
            section_id = SECTION_ID_PACKETS_AND_FRAMES;
        else if (do_show_packets && !do_show_frames)
            section_id = SECTION_ID_PACKETS;
        else // (!do_show_packets && do_show_frames)
            section_id = SECTION_ID_FRAMES;
        if (do_show_frames || do_show_packets)
            avtext_print_section_header(tfc, NULL, section_id);
        ret = read_packets(tfc, &ifile);
        if (do_show_frames || do_show_packets)
            avtext_print_section_footer(tfc);
        CHECK_END;
    }

    if (do_show_programs) {
        ret = show_programs(tfc, &ifile);
        CHECK_END;
    }

    if (do_show_stream_groups) {
        ret = show_stream_groups(tfc, &ifile);
        CHECK_END;
    }

    if (do_show_streams) {
        ret = show_streams(tfc, &ifile);
        CHECK_END;
    }
    if (do_show_chapters) {
        ret = show_chapters(tfc, &ifile);
        CHECK_END;
    }
    if (do_show_format) {
        ret = show_format(tfc, &ifile);
        CHECK_END;
    }

end:
    if (ifile.fmt_ctx)
        close_input_file(&ifile);
    av_freep(&nb_streams_frames);
    av_freep(&nb_streams_packets);
    av_freep(&selected_streams);
    av_freep(&streams_with_closed_captions);
    av_freep(&streams_with_film_grain);

    return ret;
}

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Simple multimedia streams analyzer\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [OPTIONS] INPUT_FILE\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

static void ffprobe_show_program_version(AVTextFormatContext *tfc)
{
    AVBPrint pbuf;
    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(tfc, NULL, SECTION_ID_PROGRAM_VERSION);
    print_str("version", FFMPEG_VERSION);
    print_fmt("copyright", "Copyright (c) %d-%d the FFmpeg developers",
              program_birth_year, CONFIG_THIS_YEAR);
    print_str("compiler_ident", CC_IDENT);
    print_str("configuration", FFMPEG_CONFIGURATION);
    avtext_print_section_footer(tfc);

    av_bprint_finalize(&pbuf, NULL);
}

#define SHOW_LIB_VERSION(libname, LIBNAME)                              \
    do {                                                                \
        if (CONFIG_##LIBNAME) {                                         \
            unsigned int version = libname##_version();                 \
            avtext_print_section_header(tfc, NULL, SECTION_ID_LIBRARY_VERSION); \
            print_str("name",    "lib" #libname);                       \
            print_int("major",   LIB##LIBNAME##_VERSION_MAJOR);         \
            print_int("minor",   LIB##LIBNAME##_VERSION_MINOR);         \
            print_int("micro",   LIB##LIBNAME##_VERSION_MICRO);         \
            print_int("version", version);                              \
            print_str("ident",   LIB##LIBNAME##_IDENT);                 \
            avtext_print_section_footer(tfc);                             \
        }                                                               \
    } while (0)

static void ffprobe_show_library_versions(AVTextFormatContext *tfc)
{
    avtext_print_section_header(tfc, NULL, SECTION_ID_LIBRARY_VERSIONS);
    SHOW_LIB_VERSION(avutil,     AVUTIL);
    SHOW_LIB_VERSION(avcodec,    AVCODEC);
    SHOW_LIB_VERSION(avformat,   AVFORMAT);
    SHOW_LIB_VERSION(avdevice,   AVDEVICE);
    SHOW_LIB_VERSION(avfilter,   AVFILTER);
    SHOW_LIB_VERSION(swscale,    SWSCALE);
    SHOW_LIB_VERSION(swresample, SWRESAMPLE);
    avtext_print_section_footer(tfc);
}

#define PRINT_PIX_FMT_FLAG(flagname, name)                                \
    do {                                                                  \
        print_int(name, !!(pixdesc->flags & AV_PIX_FMT_FLAG_##flagname)); \
    } while (0)

static void ffprobe_show_pixel_formats(AVTextFormatContext *tfc)
{
    const AVPixFmtDescriptor *pixdesc = NULL;
    int i, n;

    avtext_print_section_header(tfc, NULL, SECTION_ID_PIXEL_FORMATS);
    while (pixdesc = av_pix_fmt_desc_next(pixdesc)) {
        avtext_print_section_header(tfc, NULL, SECTION_ID_PIXEL_FORMAT);
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
            avtext_print_section_header(tfc, NULL, SECTION_ID_PIXEL_FORMAT_FLAGS);
            PRINT_PIX_FMT_FLAG(BE,        "big_endian");
            PRINT_PIX_FMT_FLAG(PAL,       "palette");
            PRINT_PIX_FMT_FLAG(BITSTREAM, "bitstream");
            PRINT_PIX_FMT_FLAG(HWACCEL,   "hwaccel");
            PRINT_PIX_FMT_FLAG(PLANAR,    "planar");
            PRINT_PIX_FMT_FLAG(RGB,       "rgb");
            PRINT_PIX_FMT_FLAG(ALPHA,     "alpha");
            avtext_print_section_footer(tfc);
        }
        if (do_show_pixel_format_components && (pixdesc->nb_components > 0)) {
            avtext_print_section_header(tfc, NULL, SECTION_ID_PIXEL_FORMAT_COMPONENTS);
            for (i = 0; i < pixdesc->nb_components; i++) {
                avtext_print_section_header(tfc, NULL, SECTION_ID_PIXEL_FORMAT_COMPONENT);
                print_int("index", i + 1);
                print_int("bit_depth", pixdesc->comp[i].depth);
                avtext_print_section_footer(tfc);
            }
            avtext_print_section_footer(tfc);
        }
        avtext_print_section_footer(tfc);
    }
    avtext_print_section_footer(tfc);
}

static int opt_show_optional_fields(void *optctx, const char *opt, const char *arg)
{
    if      (!av_strcasecmp(arg, "always")) show_optional_fields = SHOW_OPTIONAL_FIELDS_ALWAYS;
    else if (!av_strcasecmp(arg, "never"))  show_optional_fields = SHOW_OPTIONAL_FIELDS_NEVER;
    else if (!av_strcasecmp(arg, "auto"))   show_optional_fields = SHOW_OPTIONAL_FIELDS_AUTO;

    if (show_optional_fields == SHOW_OPTIONAL_FIELDS_AUTO && av_strcasecmp(arg, "auto")) {
        double num;
        int ret = parse_number("show_optional_fields", arg, OPT_TYPE_INT,
                               SHOW_OPTIONAL_FIELDS_AUTO, SHOW_OPTIONAL_FIELDS_ALWAYS, &num);
        if (ret < 0)
            return ret;
        show_optional_fields = num;
    }
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
    struct AVTextFormatSection *section = &sections[section_id];

    section->show_all_entries = show_all_entries;
    if (show_all_entries) {
        for (const int *id = section->children_ids; *id != -1; id++)
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
        const struct AVTextFormatSection *section = &sections[i];
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

static int opt_input_file(void *optctx, const char *arg)
{
    if (input_filename) {
        av_log(NULL, AV_LOG_ERROR,
                "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        return AVERROR(EINVAL);
    }
    if (!strcmp(arg, "-"))
        arg = "fd:";
    input_filename = av_strdup(arg);
    if (!input_filename)
        return AVERROR(ENOMEM);

    return 0;
}

static int opt_input_file_i(void *optctx, const char *opt, const char *arg)
{
    opt_input_file(optctx, arg);
    return 0;
}

static int opt_output_file_o(void *optctx, const char *opt, const char *arg)
{
    if (output_filename) {
        av_log(NULL, AV_LOG_ERROR,
                "Argument '%s' provided as output filename, but '%s' was already specified.\n",
                arg, output_filename);
        return AVERROR(EINVAL);
    }
    if (!strcmp(arg, "-"))
        arg = "fd:";
    output_filename = av_strdup(arg);
    if (!output_filename)
        return AVERROR(ENOMEM);

    return 0;
}

static int opt_print_filename(void *optctx, const char *opt, const char *arg)
{
    av_freep(&print_input_filename);
    print_input_filename = av_strdup(arg);
    return print_input_filename ? 0 : AVERROR(ENOMEM);
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, 0);
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
    const int *pid;
    const struct AVTextFormatSection *section = &sections[id];
    printf("%c%c%c%c",
           section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER           ? 'W' : '.',
           section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY             ? 'A' : '.',
           section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS  ? 'V' : '.',
           section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE             ? 'T' : '.');
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
           "W... = Section is a wrapper (contains other sections, no local entries)\n"
           ".A.. = Section contains an array of elements of the same type\n"
           "..V. = Section may contain a variable number of fields with variable keys\n"
           "...T = Section contain a unique type\n"
           "FLAGS NAME/UNIQUE_NAME\n"
           "----\n");
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
DEFINE_OPT_SHOW_SECTION(stream_groups,    STREAM_GROUPS)

static const OptionDef real_options[] = {
    CMDUTILS_COMMON_OPTIONS
    { "f",                     OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_format}, "force format", "format" },
    { "unit",                  OPT_TYPE_BOOL,        0, {&show_value_unit}, "show unit of the displayed values" },
    { "prefix",                OPT_TYPE_BOOL,        0, {&use_value_prefix}, "use SI prefixes for the displayed values" },
    { "byte_binary_prefix",    OPT_TYPE_BOOL,        0, {&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal",           OPT_TYPE_BOOL,        0, {&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty",                OPT_TYPE_FUNC,        0, {.func_arg = opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "output_format",         OPT_TYPE_STRING,      0, { &output_format },
      "set the output printing format (available formats are: default, compact, csv, flat, ini, json, xml)", "format" },
    { "print_format",          OPT_TYPE_STRING,      0, { &output_format }, "alias for -output_format (deprecated)" },
    { "of",                    OPT_TYPE_STRING,      0, { &output_format }, "alias for -output_format", "format" },
    { "select_streams",        OPT_TYPE_STRING,      0, { &stream_specifier }, "select the specified streams", "stream_specifier" },
    { "sections",              OPT_TYPE_FUNC, OPT_EXIT, {.func_arg = opt_sections}, "print sections structure and section information, and exit" },
    { "show_data",             OPT_TYPE_BOOL,        0, { &do_show_data }, "show packets data" },
    { "show_data_hash",        OPT_TYPE_STRING,      0, { &show_data_hash }, "show packets data hash" },
    { "show_error",            OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_error },  "show probing error" },
    { "show_format",           OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_format }, "show format/container info" },
    { "show_frames",           OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_frames }, "show frames info" },
    { "show_entries",          OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_show_entries},
      "show a set of specified entries", "entry_list" },
#if HAVE_THREADS
    { "show_log",              OPT_TYPE_INT,         0, { &do_show_log }, "show log" },
#endif
    { "show_packets",          OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_packets }, "show packets info" },
    { "show_programs",         OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_programs }, "show programs info" },
    { "show_stream_groups",    OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_stream_groups }, "show stream groups info" },
    { "show_streams",          OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_streams }, "show streams info" },
    { "show_chapters",         OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_chapters }, "show chapters info" },
    { "count_frames",          OPT_TYPE_BOOL,        0, { &do_count_frames }, "count the number of frames per stream" },
    { "count_packets",         OPT_TYPE_BOOL,        0, { &do_count_packets }, "count the number of packets per stream" },
    { "show_program_version",  OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_program_version },  "show ffprobe version" },
    { "show_library_versions", OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_library_versions }, "show library versions" },
    { "show_versions",         OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_versions }, "show program and library versions" },
    { "show_pixel_formats",    OPT_TYPE_FUNC,        0, { .func_arg = &opt_show_pixel_formats }, "show pixel format descriptions" },
    { "show_optional_fields",  OPT_TYPE_FUNC, OPT_FUNC_ARG, { .func_arg = &opt_show_optional_fields }, "show optional fields" },
    { "show_private_data",     OPT_TYPE_BOOL,        0, { &show_private_data }, "show private data" },
    { "private",               OPT_TYPE_BOOL,        0, { &show_private_data }, "same as show_private_data" },
    { "analyze_frames",        OPT_TYPE_BOOL,        0, { &do_analyze_frames }, "analyze frames to provide additional stream-level information" },
    { "bitexact",              OPT_TYPE_BOOL,        0, {&do_bitexact}, "force bitexact output" },
    { "read_intervals",        OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_read_intervals}, "set read intervals", "read_intervals" },
    { "i",                     OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_input_file_i}, "read specified file", "input_file"},
    { "o",                     OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_output_file_o}, "write to specified output", "output_file"},
    { "print_filename",        OPT_TYPE_FUNC, OPT_FUNC_ARG, {.func_arg = opt_print_filename}, "override the printed input filename", "print_file"},
    { "find_stream_info",      OPT_TYPE_BOOL, OPT_INPUT | OPT_EXPERT, { &find_stream_info },
        "read and decode the streams to fill missing information with heuristics" },
    { NULL, },
};

static inline int check_section_show_entries(int section_id)
{
    struct AVTextFormatSection *section = &sections[section_id];
    if (sections[section_id].show_all_entries || sections[section_id].entries_to_show)
        return 1;
    for (const int *id = section->children_ids; *id != -1; id++)
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
    const AVTextFormatter *f;
    AVTextFormatContext *tctx;
    AVTextWriterContext *wctx;
    char *buf;
    char *f_name = NULL, *f_args = NULL;
    int ret, input_ret, i;

    init_dynload();

    setvbuf(stderr, NULL, _IONBF, 0); /* win32 runtime needs this */

    av_log_set_flags(AV_LOG_SKIP_REPEATED);

    options = real_options;
    parse_loglevel(argc, argv, options);
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner(argc, argv, options);
    ret = parse_options(NULL, argc, argv, options, opt_input_file);
    if (ret < 0) {
        ret = (ret == AVERROR_EXIT) ? 0 : ret;
        goto end;
    }

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
    SET_DO_SHOW(STREAM_GROUP_DISPOSITION, stream_group_disposition);
    SET_DO_SHOW(STREAM_GROUPS, stream_groups);
    SET_DO_SHOW(STREAM_GROUP_COMPONENTS, stream_group_components);
    SET_DO_SHOW(STREAMS, streams);
    SET_DO_SHOW(STREAM_DISPOSITION, stream_disposition);
    SET_DO_SHOW(PROGRAM_STREAM_DISPOSITION, stream_disposition);
    SET_DO_SHOW(STREAM_GROUP_STREAM_DISPOSITION, stream_disposition);

    SET_DO_SHOW(CHAPTER_TAGS, chapter_tags);
    SET_DO_SHOW(FORMAT_TAGS, format_tags);
    SET_DO_SHOW(FRAME_TAGS, frame_tags);
    SET_DO_SHOW(PROGRAM_TAGS, program_tags);
    SET_DO_SHOW(STREAM_GROUP_TAGS, stream_group_tags);
    SET_DO_SHOW(STREAM_TAGS, stream_tags);
    SET_DO_SHOW(PROGRAM_STREAM_TAGS, stream_tags);
    SET_DO_SHOW(STREAM_GROUP_STREAM_TAGS, stream_tags);
    SET_DO_SHOW(PACKET_TAGS, packet_tags);

    if (do_bitexact && (do_show_program_version || do_show_library_versions)) {
        av_log(NULL, AV_LOG_ERROR,
               "-bitexact and -show_program_version or -show_library_versions "
               "options are incompatible\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!output_format)
        output_format = av_strdup("default");
    if (!output_format) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    f_name = av_strtok(output_format, "=", &buf);
    if (!f_name) {
        av_log(NULL, AV_LOG_ERROR,
               "No name specified for the output format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    f_args = buf;

    f = avtext_get_formatter_by_name(f_name);
    if (!f) {
        av_log(NULL, AV_LOG_ERROR, "Unknown output format with name '%s'\n", f_name);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (output_filename) {
        ret = avtextwriter_create_file(&wctx, output_filename);
    } else
        ret = avtextwriter_create_stdout(&wctx);

    if (ret < 0)
        goto end;

    AVTextFormatOptions tf_options = {
        .show_optional_fields = show_optional_fields,
        .show_value_unit = show_value_unit,
        .use_value_prefix = use_value_prefix,
        .use_byte_value_binary_prefix = use_byte_value_binary_prefix,
        .use_value_sexagesimal_format = use_value_sexagesimal_format,
    };

    if ((ret = avtext_context_open(&tctx, f, wctx, f_args, sections, FF_ARRAY_ELEMS(sections), tf_options, show_data_hash)) >= 0) {
        if (f == &avtextformatter_xml)
            tctx->string_validation_utf8_flags |= AV_UTF8_FLAG_EXCLUDE_XML_INVALID_CONTROL_CODES;

        avtext_print_section_header(tctx, NULL, SECTION_ID_ROOT);

        if (do_show_program_version)
            ffprobe_show_program_version(tctx);
        if (do_show_library_versions)
            ffprobe_show_library_versions(tctx);
        if (do_show_pixel_formats)
            ffprobe_show_pixel_formats(tctx);

        if (!input_filename &&
            ((do_show_format || do_show_programs || do_show_stream_groups || do_show_streams || do_show_chapters || do_show_packets || do_show_error) ||
             (!do_show_program_version && !do_show_library_versions && !do_show_pixel_formats))) {
            show_usage();
            av_log(NULL, AV_LOG_ERROR, "You have to specify one input file.\n");
            av_log(NULL, AV_LOG_ERROR, "Use -h to get full help or, even better, run 'man %s'.\n", program_name);
            ret = AVERROR(EINVAL);
        } else if (input_filename) {
            ret = probe_file(tctx, input_filename, print_input_filename);
            if (ret < 0 && do_show_error)
                show_error(tctx, ret);
        }

        input_ret = ret;

        avtext_print_section_footer(tctx);

        ret = avtextwriter_context_close(&wctx);
        if (ret < 0)
            av_log(NULL, AV_LOG_ERROR, "Writing output failed (closing writer): %s\n", av_err2str(ret));

        ret = avtext_context_close(&tctx);
        if (ret < 0)
            av_log(NULL, AV_LOG_ERROR, "Writing output failed (closing formatter): %s\n", av_err2str(ret));

        ret = FFMIN(ret, input_ret);
    }

end:
    av_freep(&output_format);
    av_freep(&output_filename);
    av_freep(&input_filename);
    av_freep(&print_input_filename);
    av_freep(&read_intervals);

    uninit_opts();
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));

    avformat_network_deinit();

    return ret < 0;
}
