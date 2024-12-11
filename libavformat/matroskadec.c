/*
 * Matroska file demuxer
 * Copyright (c) 2003-2008 The FFmpeg Project
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
 * Matroska file demuxer
 * @author Ronald Bultje <rbultje@ronald.bitfreak.net>
 * @author with a little help from Moritz Bunkus <moritz@bunkus.org>
 * @author totally reworked by Aurelien Jacobs <aurel@gnuage.org>
 * @see specs available on the Matroska project page: http://www.matroska.org/
 */

#include "config.h"
#include "config_components.h"

#include <inttypes.h>
#include <stdio.h>

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/bprint.h"
#include "libavutil/dict.h"
#include "libavutil/dict_internal.h"
#include "libavutil/display.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lzo.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time_internal.h"
#include "libavutil/spherical.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/defs.h"
#include "libavcodec/flac.h"
#include "libavcodec/itut35.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/packet_internal.h"

#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "dovi_isom.h"
#include "internal.h"
#include "isom.h"
#include "matroska.h"
#include "oggdec.h"
/* For ff_codec_get_id(). */
#include "riff.h"
#include "rmsipr.h"

#if CONFIG_BZLIB
#include <bzlib.h>
#endif
#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "qtpalette.h"

#define EBML_UNKNOWN_LENGTH  UINT64_MAX /* EBML unknown length, in uint64_t */
#define NEEDS_CHECKING                2 /* Indicates that some error checks
                                         * still need to be performed */
#define LEVEL_ENDED                   3 /* return value of ebml_parse when the
                                         * syntax level used for parsing ended. */
#define SKIP_THRESHOLD      1024 * 1024 /* In non-seekable mode, if more than SKIP_THRESHOLD
                                         * of unkown, potentially damaged data is encountered,
                                         * it is considered an error. */
#define UNKNOWN_EQUIV         50 * 1024 /* An unknown element is considered equivalent
                                         * to this many bytes of unknown data for the
                                         * SKIP_THRESHOLD check. */

typedef enum {
    EBML_NONE,
    EBML_UINT,
    EBML_SINT,
    EBML_FLOAT,
    EBML_STR,
    EBML_UTF8,
    EBML_BIN,
    EBML_NEST,
    EBML_LEVEL1,
    EBML_STOP,
    EBML_TYPE_COUNT
} EbmlType;

typedef struct CountedElement {
    union {
        uint64_t  u;
        int64_t   i;
        double    f;
        char     *s;
    } el;
    unsigned count;
} CountedElement;

typedef const struct EbmlSyntax {
    uint32_t id;
    uint8_t type;
    uint8_t is_counted;
    size_t list_elem_size;
    size_t data_offset;
    union {
        int64_t     i;
        uint64_t    u;
        double      f;
        const char *s;
        const struct EbmlSyntax *n;
    } def;
} EbmlSyntax;

typedef struct EbmlList {
    int nb_elem;
    unsigned int alloc_elem_size;
    void *elem;
} EbmlList;

typedef struct EbmlBin {
    int      size;
    AVBufferRef *buf;
    uint8_t *data;
    int64_t  pos;
} EbmlBin;

typedef struct Ebml {
    uint64_t version;
    uint64_t max_size;
    uint64_t id_length;
    char    *doctype;
    uint64_t doctype_version;
} Ebml;

typedef struct MatroskaTrackCompression {
    uint64_t algo;
    EbmlBin  settings;
} MatroskaTrackCompression;

typedef struct MatroskaTrackEncryption {
    uint64_t algo;
    EbmlBin  key_id;
} MatroskaTrackEncryption;

typedef struct MatroskaTrackEncoding {
    uint64_t scope;
    uint64_t type;
    MatroskaTrackCompression compression;
    MatroskaTrackEncryption encryption;
} MatroskaTrackEncoding;

typedef struct MatroskaMasteringMeta {
    double r_x;
    double r_y;
    double g_x;
    double g_y;
    double b_x;
    double b_y;
    double white_x;
    double white_y;
    double max_luminance;
    CountedElement min_luminance;
} MatroskaMasteringMeta;

typedef struct MatroskaTrackVideoColor {
    uint64_t matrix_coefficients;
    uint64_t bits_per_channel;
    uint64_t chroma_sub_horz;
    uint64_t chroma_sub_vert;
    uint64_t cb_sub_horz;
    uint64_t cb_sub_vert;
    uint64_t chroma_siting_horz;
    uint64_t chroma_siting_vert;
    uint64_t range;
    uint64_t transfer_characteristics;
    uint64_t primaries;
    uint64_t max_cll;
    uint64_t max_fall;
    MatroskaMasteringMeta mastering_meta;
} MatroskaTrackVideoColor;

typedef struct MatroskaTrackVideoProjection {
    uint64_t type;
    EbmlBin private;
    double yaw;
    double pitch;
    double roll;
} MatroskaTrackVideoProjection;

typedef struct MatroskaTrackVideo {
    double   frame_rate;
    uint64_t display_width;
    uint64_t display_height;
    uint64_t pixel_width;
    uint64_t pixel_height;
    uint64_t cropped_width;
    uint64_t cropped_height;
    EbmlBin  color_space;
    uint64_t pixel_cropt;
    uint64_t pixel_cropl;
    uint64_t pixel_cropb;
    uint64_t pixel_cropr;
    uint64_t display_unit;
    uint64_t interlaced;
    uint64_t field_order;
    uint64_t stereo_mode;
    uint64_t alpha_mode;
    EbmlList color;
    MatroskaTrackVideoProjection projection;
} MatroskaTrackVideo;

typedef struct MatroskaTrackAudio {
    double   samplerate;
    double   out_samplerate;
    uint64_t bitdepth;
    uint64_t channels;

    /* real audio header (extracted from extradata) */
    int      coded_framesize;
    int      sub_packet_h;
    int      frame_size;
    int      sub_packet_size;
    int      sub_packet_cnt;
    int      pkt_cnt;
    uint64_t buf_timecode;
    uint8_t *buf;
} MatroskaTrackAudio;

typedef struct MatroskaTrackPlane {
    uint64_t uid;
    uint64_t type;
} MatroskaTrackPlane;

typedef struct MatroskaTrackOperation {
    EbmlList combine_planes;
} MatroskaTrackOperation;

typedef struct MatroskaBlockAdditionMapping {
    uint64_t value;
    char *name;
    uint64_t type;
    EbmlBin extradata;
} MatroskaBlockAdditionMapping;

typedef struct MatroskaTrack {
    uint64_t num;
    uint64_t uid;
    uint64_t type;
    char    *name;
    char    *codec_id;
    EbmlBin  codec_priv;
    char    *language;
    double time_scale;
    uint64_t default_duration;
    uint64_t flag_default;
    uint64_t flag_forced;
    uint64_t flag_comment;
    uint64_t flag_hearingimpaired;
    uint64_t flag_visualimpaired;
    uint64_t flag_textdescriptions;
    CountedElement flag_original;
    uint64_t seek_preroll;
    MatroskaTrackVideo video;
    MatroskaTrackAudio audio;
    MatroskaTrackOperation operation;
    EbmlList encodings;
    uint64_t codec_delay;
    uint64_t codec_delay_in_track_tb;

    AVStream *stream;
    int64_t end_timecode;
    int ms_compat;
    int needs_decoding;
    uint64_t max_block_additional_id;
    EbmlList block_addition_mappings;

    uint32_t palette[AVPALETTE_COUNT];
    int has_palette;
} MatroskaTrack;

typedef struct MatroskaAttachment {
    uint64_t uid;
    char *filename;
    char *description;
    char *mime;
    EbmlBin bin;

    AVStream *stream;
} MatroskaAttachment;

typedef struct MatroskaChapter {
    uint64_t start;
    uint64_t end;
    uint64_t uid;
    char    *title;

    AVChapter *chapter;
} MatroskaChapter;

typedef struct MatroskaIndexPos {
    uint64_t track;
    uint64_t pos;
} MatroskaIndexPos;

typedef struct MatroskaIndex {
    uint64_t time;
    EbmlList pos;
} MatroskaIndex;

typedef struct MatroskaTag {
    char *name;
    char *string;
    char *lang;
    uint64_t def;
    EbmlList sub;
} MatroskaTag;

typedef struct MatroskaTagTarget {
    char    *type;
    uint64_t typevalue;
    uint64_t trackuid;
    uint64_t chapteruid;
    uint64_t attachuid;
} MatroskaTagTarget;

typedef struct MatroskaTags {
    MatroskaTagTarget target;
    EbmlList tag;
} MatroskaTags;

typedef struct MatroskaSeekhead {
    uint64_t id;
    uint64_t pos;
} MatroskaSeekhead;

typedef struct MatroskaLevel {
    uint64_t start;
    uint64_t length;
} MatroskaLevel;

typedef struct MatroskaBlockMore {
    uint64_t additional_id;
    EbmlBin  additional;
} MatroskaBlockMore;

typedef struct MatroskaBlock {
    uint64_t duration;
    CountedElement reference;
    uint64_t non_simple;
    EbmlBin  bin;
    EbmlList blockmore;
    int64_t  discard_padding;
} MatroskaBlock;

typedef struct MatroskaCluster {
    MatroskaBlock block;
    uint64_t timecode;
    int64_t pos;
} MatroskaCluster;

typedef struct MatroskaLevel1Element {
    int64_t  pos;
    uint32_t id;
    int parsed;
} MatroskaLevel1Element;

typedef struct MatroskaDemuxContext {
    const AVClass *class;
    AVFormatContext *ctx;

    /* EBML stuff */
    MatroskaLevel levels[EBML_MAX_DEPTH];
    int      num_levels;
    uint32_t current_id;
    int64_t  resync_pos;
    int      unknown_count;

    uint64_t time_scale;
    double   duration;
    char    *title;
    char    *muxingapp;
    EbmlBin  date_utc;
    EbmlList tracks;
    EbmlList attachments;
    EbmlList chapters;
    EbmlList index;
    EbmlList tags;
    EbmlList seekhead;

    /* byte position of the segment inside the stream */
    int64_t segment_start;

    /* This packet coincides with FFFormatContext.parse_pkt
     * and is not owned by us. */
    AVPacket *pkt;

    /* the packet queue */
    PacketList queue;

    int done;

    /* What to skip before effectively reading a packet. */
    int skip_to_keyframe;
    uint64_t skip_to_timecode;

    /* File has a CUES element, but we defer parsing until it is needed. */
    int cues_parsing_deferred;

    /* Level1 elements and whether they were read yet */
    MatroskaLevel1Element level1_elems[64];
    int num_level1_elems;

    MatroskaCluster current_cluster;

    int is_webm;

    /* WebM DASH Manifest live flag */
    int is_live;

    /* Bandwidth value for WebM DASH Manifest */
    int bandwidth;
} MatroskaDemuxContext;

#define CHILD_OF(parent) { .def = { .n = parent } }

// The following forward declarations need their size because
// a tentative definition with internal linkage must not be an
// incomplete type (6.7.2 in C90, 6.9.2 in C99).
// Removing the sizes breaks MSVC.
static EbmlSyntax ebml_syntax[3], matroska_segment[9], matroska_track_video_color[15], matroska_track_video[19],
                  matroska_track[33], matroska_track_encoding[6], matroska_track_encodings[2],
                  matroska_track_combine_planes[2], matroska_track_operation[2], matroska_block_addition_mapping[5], matroska_tracks[2],
                  matroska_attachments[2], matroska_chapter_entry[9], matroska_chapter[6], matroska_chapters[2],
                  matroska_index_entry[3], matroska_index[2], matroska_tag[3], matroska_tags[2], matroska_seekhead[2],
                  matroska_blockadditions[2], matroska_blockgroup[8], matroska_cluster_parsing[8];

static EbmlSyntax ebml_header[] = {
    { EBML_ID_EBMLREADVERSION,    EBML_UINT, 0, 0, offsetof(Ebml, version),         { .u = EBML_VERSION } },
    { EBML_ID_EBMLMAXSIZELENGTH,  EBML_UINT, 0, 0, offsetof(Ebml, max_size),        { .u = 8 } },
    { EBML_ID_EBMLMAXIDLENGTH,    EBML_UINT, 0, 0, offsetof(Ebml, id_length),       { .u = 4 } },
    { EBML_ID_DOCTYPE,            EBML_STR,  0, 0, offsetof(Ebml, doctype),         { .s = "(none)" } },
    { EBML_ID_DOCTYPEREADVERSION, EBML_UINT, 0, 0, offsetof(Ebml, doctype_version), { .u = 1 } },
    { EBML_ID_EBMLVERSION,        EBML_NONE },
    { EBML_ID_DOCTYPEVERSION,     EBML_NONE },
    CHILD_OF(ebml_syntax)
};

static EbmlSyntax ebml_syntax[] = {
    { EBML_ID_HEADER,      EBML_NEST, 0, 0, 0, { .n = ebml_header } },
    { MATROSKA_ID_SEGMENT, EBML_STOP },
    { 0 }
};

static EbmlSyntax matroska_info[] = {
    { MATROSKA_ID_TIMECODESCALE, EBML_UINT,  0, 0, offsetof(MatroskaDemuxContext, time_scale), { .u = 1000000 } },
    { MATROSKA_ID_DURATION,      EBML_FLOAT, 0, 0, offsetof(MatroskaDemuxContext, duration) },
    { MATROSKA_ID_TITLE,         EBML_UTF8,  0, 0, offsetof(MatroskaDemuxContext, title) },
    { MATROSKA_ID_WRITINGAPP,    EBML_NONE },
    { MATROSKA_ID_MUXINGAPP,     EBML_UTF8, 0, 0, offsetof(MatroskaDemuxContext, muxingapp) },
    { MATROSKA_ID_DATEUTC,       EBML_BIN,  0, 0, offsetof(MatroskaDemuxContext, date_utc) },
    { MATROSKA_ID_SEGMENTUID,    EBML_NONE },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_mastering_meta[] = {
    { MATROSKA_ID_VIDEOCOLOR_RX, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, r_x) },
    { MATROSKA_ID_VIDEOCOLOR_RY, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, r_y) },
    { MATROSKA_ID_VIDEOCOLOR_GX, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, g_x) },
    { MATROSKA_ID_VIDEOCOLOR_GY, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, g_y) },
    { MATROSKA_ID_VIDEOCOLOR_BX, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, b_x) },
    { MATROSKA_ID_VIDEOCOLOR_BY, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, b_y) },
    { MATROSKA_ID_VIDEOCOLOR_WHITEX, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, white_x) },
    { MATROSKA_ID_VIDEOCOLOR_WHITEY, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, white_y) },
    { MATROSKA_ID_VIDEOCOLOR_LUMINANCEMIN, EBML_FLOAT, 1, 0, offsetof(MatroskaMasteringMeta, min_luminance) },
    { MATROSKA_ID_VIDEOCOLOR_LUMINANCEMAX, EBML_FLOAT, 0, 0, offsetof(MatroskaMasteringMeta, max_luminance) },
    CHILD_OF(matroska_track_video_color)
};

static EbmlSyntax matroska_track_video_color[] = {
    { MATROSKA_ID_VIDEOCOLORMATRIXCOEFF,      EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, matrix_coefficients), { .u = AVCOL_SPC_UNSPECIFIED } },
    { MATROSKA_ID_VIDEOCOLORBITSPERCHANNEL,   EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, bits_per_channel), { .u = 0 } },
    { MATROSKA_ID_VIDEOCOLORCHROMASUBHORZ,    EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, chroma_sub_horz) },
    { MATROSKA_ID_VIDEOCOLORCHROMASUBVERT,    EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, chroma_sub_vert) },
    { MATROSKA_ID_VIDEOCOLORCBSUBHORZ,        EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, cb_sub_horz) },
    { MATROSKA_ID_VIDEOCOLORCBSUBVERT,        EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, cb_sub_vert) },
    { MATROSKA_ID_VIDEOCOLORCHROMASITINGHORZ, EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, chroma_siting_horz), { .u = MATROSKA_COLOUR_CHROMASITINGHORZ_UNDETERMINED } },
    { MATROSKA_ID_VIDEOCOLORCHROMASITINGVERT, EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, chroma_siting_vert), { .u = MATROSKA_COLOUR_CHROMASITINGVERT_UNDETERMINED } },
    { MATROSKA_ID_VIDEOCOLORRANGE,            EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, range), { .u = AVCOL_RANGE_UNSPECIFIED } },
    { MATROSKA_ID_VIDEOCOLORTRANSFERCHARACTERISTICS, EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, transfer_characteristics), { .u = AVCOL_TRC_UNSPECIFIED } },
    { MATROSKA_ID_VIDEOCOLORPRIMARIES,        EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, primaries), { .u = AVCOL_PRI_UNSPECIFIED } },
    { MATROSKA_ID_VIDEOCOLORMAXCLL,           EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, max_cll) },
    { MATROSKA_ID_VIDEOCOLORMAXFALL,          EBML_UINT, 0, 0, offsetof(MatroskaTrackVideoColor, max_fall) },
    { MATROSKA_ID_VIDEOCOLORMASTERINGMETA,    EBML_NEST, 0, 0, offsetof(MatroskaTrackVideoColor, mastering_meta), { .n = matroska_mastering_meta } },
    CHILD_OF(matroska_track_video)
};

static EbmlSyntax matroska_track_video_projection[] = {
    { MATROSKA_ID_VIDEOPROJECTIONTYPE,        EBML_UINT,  0, 0, offsetof(MatroskaTrackVideoProjection, type), { .u = MATROSKA_VIDEO_PROJECTION_TYPE_RECTANGULAR } },
    { MATROSKA_ID_VIDEOPROJECTIONPRIVATE,     EBML_BIN,   0, 0, offsetof(MatroskaTrackVideoProjection, private) },
    { MATROSKA_ID_VIDEOPROJECTIONPOSEYAW,     EBML_FLOAT, 0, 0, offsetof(MatroskaTrackVideoProjection, yaw),   { .f = 0.0 } },
    { MATROSKA_ID_VIDEOPROJECTIONPOSEPITCH,   EBML_FLOAT, 0, 0, offsetof(MatroskaTrackVideoProjection, pitch), { .f = 0.0 } },
    { MATROSKA_ID_VIDEOPROJECTIONPOSEROLL,    EBML_FLOAT, 0, 0, offsetof(MatroskaTrackVideoProjection, roll),  { .f = 0.0 } },
    CHILD_OF(matroska_track_video)
};

static EbmlSyntax matroska_track_video[] = {
    { MATROSKA_ID_VIDEOFRAMERATE,      EBML_FLOAT, 0, 0, offsetof(MatroskaTrackVideo, frame_rate) },
    { MATROSKA_ID_VIDEODISPLAYWIDTH,   EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, display_width), { .u=-1 } },
    { MATROSKA_ID_VIDEODISPLAYHEIGHT,  EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, display_height), { .u=-1 } },
    { MATROSKA_ID_VIDEOPIXELWIDTH,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_width) },
    { MATROSKA_ID_VIDEOPIXELHEIGHT,    EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_height) },
    { MATROSKA_ID_VIDEOCOLORSPACE,     EBML_BIN,   0, 0, offsetof(MatroskaTrackVideo, color_space) },
    { MATROSKA_ID_VIDEOALPHAMODE,      EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, alpha_mode), { .u = 0 } },
    { MATROSKA_ID_VIDEOCOLOR,          EBML_NEST,  0, sizeof(MatroskaTrackVideoColor), offsetof(MatroskaTrackVideo, color), { .n = matroska_track_video_color } },
    { MATROSKA_ID_VIDEOPROJECTION,     EBML_NEST,  0, 0, offsetof(MatroskaTrackVideo, projection), { .n = matroska_track_video_projection } },
    { MATROSKA_ID_VIDEOPIXELCROPB,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_cropb), {.u = 0 } },
    { MATROSKA_ID_VIDEOPIXELCROPT,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_cropt), {.u = 0 } },
    { MATROSKA_ID_VIDEOPIXELCROPL,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_cropl), {.u = 0 } },
    { MATROSKA_ID_VIDEOPIXELCROPR,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, pixel_cropr), {.u = 0 } },
    { MATROSKA_ID_VIDEODISPLAYUNIT,    EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, display_unit), { .u= MATROSKA_VIDEO_DISPLAYUNIT_PIXELS } },
    { MATROSKA_ID_VIDEOFLAGINTERLACED, EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, interlaced),  { .u = MATROSKA_VIDEO_INTERLACE_FLAG_UNDETERMINED } },
    { MATROSKA_ID_VIDEOFIELDORDER,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, field_order), { .u = MATROSKA_VIDEO_FIELDORDER_UNDETERMINED } },
    { MATROSKA_ID_VIDEOSTEREOMODE,     EBML_UINT,  0, 0, offsetof(MatroskaTrackVideo, stereo_mode), { .u = MATROSKA_VIDEO_STEREOMODE_TYPE_NB } },
    { MATROSKA_ID_VIDEOASPECTRATIO,    EBML_NONE },
    CHILD_OF(matroska_track)
};

static EbmlSyntax matroska_track_audio[] = {
    { MATROSKA_ID_AUDIOSAMPLINGFREQ,    EBML_FLOAT, 0, 0, offsetof(MatroskaTrackAudio, samplerate), { .f = 8000.0 } },
    { MATROSKA_ID_AUDIOOUTSAMPLINGFREQ, EBML_FLOAT, 0, 0, offsetof(MatroskaTrackAudio, out_samplerate) },
    { MATROSKA_ID_AUDIOBITDEPTH,        EBML_UINT,  0, 0, offsetof(MatroskaTrackAudio, bitdepth) },
    { MATROSKA_ID_AUDIOCHANNELS,        EBML_UINT,  0, 0, offsetof(MatroskaTrackAudio, channels),   { .u = 1 } },
    CHILD_OF(matroska_track)
};

static EbmlSyntax matroska_track_encoding_compression[] = {
    { MATROSKA_ID_ENCODINGCOMPALGO,     EBML_UINT, 0, 0, offsetof(MatroskaTrackCompression, algo), { .u = MATROSKA_TRACK_ENCODING_COMP_ZLIB } },
    { MATROSKA_ID_ENCODINGCOMPSETTINGS, EBML_BIN,  0, 0, offsetof(MatroskaTrackCompression, settings) },
    CHILD_OF(matroska_track_encoding)
};

static EbmlSyntax matroska_track_encoding_encryption[] = {
    { MATROSKA_ID_ENCODINGENCALGO,        EBML_UINT, 0, 0, offsetof(MatroskaTrackEncryption,algo), {.u = 0} },
    { MATROSKA_ID_ENCODINGENCKEYID,       EBML_BIN, 0, 0, offsetof(MatroskaTrackEncryption,key_id) },
    { MATROSKA_ID_ENCODINGENCAESSETTINGS, EBML_NONE },
    { MATROSKA_ID_ENCODINGSIGALGO,        EBML_NONE },
    { MATROSKA_ID_ENCODINGSIGHASHALGO,    EBML_NONE },
    { MATROSKA_ID_ENCODINGSIGKEYID,       EBML_NONE },
    { MATROSKA_ID_ENCODINGSIGNATURE,      EBML_NONE },
    CHILD_OF(matroska_track_encoding)
};
static EbmlSyntax matroska_track_encoding[] = {
    { MATROSKA_ID_ENCODINGSCOPE,       EBML_UINT, 0, 0, offsetof(MatroskaTrackEncoding, scope),       { .u = 1 } },
    { MATROSKA_ID_ENCODINGTYPE,        EBML_UINT, 0, 0, offsetof(MatroskaTrackEncoding, type),        { .u = 0 } },
    { MATROSKA_ID_ENCODINGCOMPRESSION, EBML_NEST, 0, 0, offsetof(MatroskaTrackEncoding, compression), { .n = matroska_track_encoding_compression } },
    { MATROSKA_ID_ENCODINGENCRYPTION,  EBML_NEST, 0, 0, offsetof(MatroskaTrackEncoding, encryption),  { .n = matroska_track_encoding_encryption } },
    { MATROSKA_ID_ENCODINGORDER,       EBML_NONE },
    CHILD_OF(matroska_track_encodings)
};

static EbmlSyntax matroska_track_encodings[] = {
    { MATROSKA_ID_TRACKCONTENTENCODING, EBML_NEST, 0, sizeof(MatroskaTrackEncoding), offsetof(MatroskaTrack, encodings), { .n = matroska_track_encoding } },
    CHILD_OF(matroska_track)
};

static EbmlSyntax matroska_track_plane[] = {
    { MATROSKA_ID_TRACKPLANEUID,  EBML_UINT, 0, 0, offsetof(MatroskaTrackPlane,uid) },
    { MATROSKA_ID_TRACKPLANETYPE, EBML_UINT, 0, 0, offsetof(MatroskaTrackPlane,type) },
    CHILD_OF(matroska_track_combine_planes)
};

static EbmlSyntax matroska_track_combine_planes[] = {
    { MATROSKA_ID_TRACKPLANE, EBML_NEST, 0, sizeof(MatroskaTrackPlane), offsetof(MatroskaTrackOperation,combine_planes), {.n = matroska_track_plane} },
    CHILD_OF(matroska_track_operation)
};

static EbmlSyntax matroska_track_operation[] = {
    { MATROSKA_ID_TRACKCOMBINEPLANES, EBML_NEST, 0, 0, 0, {.n = matroska_track_combine_planes} },
    CHILD_OF(matroska_track)
};

static EbmlSyntax matroska_block_addition_mapping[] = {
    { MATROSKA_ID_BLKADDIDVALUE,      EBML_UINT, 0, 0, offsetof(MatroskaBlockAdditionMapping, value) },
    { MATROSKA_ID_BLKADDIDNAME,       EBML_STR,  0, 0, offsetof(MatroskaBlockAdditionMapping, name) },
    { MATROSKA_ID_BLKADDIDTYPE,       EBML_UINT, 0, 0, offsetof(MatroskaBlockAdditionMapping, type), { .u = MATROSKA_BLOCK_ADD_ID_TYPE_DEFAULT } },
    { MATROSKA_ID_BLKADDIDEXTRADATA,  EBML_BIN,  0, 0, offsetof(MatroskaBlockAdditionMapping, extradata) },
    CHILD_OF(matroska_track)
};

static EbmlSyntax matroska_track[] = {
    { MATROSKA_ID_TRACKNUMBER,           EBML_UINT,  0, 0, offsetof(MatroskaTrack, num) },
    { MATROSKA_ID_TRACKNAME,             EBML_UTF8,  0, 0, offsetof(MatroskaTrack, name) },
    { MATROSKA_ID_TRACKUID,              EBML_UINT,  0, 0, offsetof(MatroskaTrack, uid) },
    { MATROSKA_ID_TRACKTYPE,             EBML_UINT,  0, 0, offsetof(MatroskaTrack, type) },
    { MATROSKA_ID_CODECID,               EBML_STR,   0, 0, offsetof(MatroskaTrack, codec_id) },
    { MATROSKA_ID_CODECPRIVATE,          EBML_BIN,   0, 0, offsetof(MatroskaTrack, codec_priv) },
    { MATROSKA_ID_CODECDELAY,            EBML_UINT,  0, 0, offsetof(MatroskaTrack, codec_delay),  { .u = 0 } },
    { MATROSKA_ID_TRACKLANGUAGE,         EBML_STR,   0, 0, offsetof(MatroskaTrack, language),     { .s = "eng" } },
    { MATROSKA_ID_TRACKDEFAULTDURATION,  EBML_UINT,  0, 0, offsetof(MatroskaTrack, default_duration) },
    { MATROSKA_ID_TRACKTIMECODESCALE,    EBML_FLOAT, 0, 0, offsetof(MatroskaTrack, time_scale),   { .f = 1.0 } },
    { MATROSKA_ID_TRACKFLAGCOMMENTARY,   EBML_UINT,  0, 0, offsetof(MatroskaTrack, flag_comment), { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGDEFAULT,      EBML_UINT,  0, 0, offsetof(MatroskaTrack, flag_default), { .u = 1 } },
    { MATROSKA_ID_TRACKFLAGFORCED,       EBML_UINT,  0, 0, offsetof(MatroskaTrack, flag_forced),  { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGHEARINGIMPAIRED, EBML_UINT, 0, 0, offsetof(MatroskaTrack, flag_hearingimpaired), { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGVISUALIMPAIRED, EBML_UINT, 0, 0, offsetof(MatroskaTrack, flag_visualimpaired), { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGTEXTDESCRIPTIONS, EBML_UINT, 0, 0, offsetof(MatroskaTrack, flag_textdescriptions), { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGORIGINAL,     EBML_UINT,  1, 0, offsetof(MatroskaTrack, flag_original), {.u = 0 } },
    { MATROSKA_ID_TRACKVIDEO,            EBML_NEST,  0, 0, offsetof(MatroskaTrack, video),        { .n = matroska_track_video } },
    { MATROSKA_ID_TRACKAUDIO,            EBML_NEST,  0, 0, offsetof(MatroskaTrack, audio),        { .n = matroska_track_audio } },
    { MATROSKA_ID_TRACKOPERATION,        EBML_NEST,  0, 0, offsetof(MatroskaTrack, operation),    { .n = matroska_track_operation } },
    { MATROSKA_ID_TRACKCONTENTENCODINGS, EBML_NEST,  0, 0, 0,                                     { .n = matroska_track_encodings } },
    { MATROSKA_ID_TRACKMAXBLKADDID,      EBML_UINT,  0, 0, offsetof(MatroskaTrack, max_block_additional_id), { .u = 0 } },
    { MATROSKA_ID_TRACKBLKADDMAPPING,    EBML_NEST,  0, sizeof(MatroskaBlockAdditionMapping), offsetof(MatroskaTrack, block_addition_mappings), { .n = matroska_block_addition_mapping } },
    { MATROSKA_ID_SEEKPREROLL,           EBML_UINT,  0, 0, offsetof(MatroskaTrack, seek_preroll), { .u = 0 } },
    { MATROSKA_ID_TRACKFLAGENABLED,      EBML_NONE },
    { MATROSKA_ID_TRACKFLAGLACING,       EBML_NONE },
    { MATROSKA_ID_CODECNAME,             EBML_NONE },
    { MATROSKA_ID_CODECDECODEALL,        EBML_NONE },
    { MATROSKA_ID_CODECINFOURL,          EBML_NONE },
    { MATROSKA_ID_CODECDOWNLOADURL,      EBML_NONE },
    { MATROSKA_ID_TRACKMINCACHE,         EBML_NONE },
    { MATROSKA_ID_TRACKMAXCACHE,         EBML_NONE },
    CHILD_OF(matroska_tracks)
};

static EbmlSyntax matroska_tracks[] = {
    { MATROSKA_ID_TRACKENTRY, EBML_NEST, 0, sizeof(MatroskaTrack), offsetof(MatroskaDemuxContext, tracks), { .n = matroska_track } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_attachment[] = {
    { MATROSKA_ID_FILEUID,      EBML_UINT, 0, 0, offsetof(MatroskaAttachment, uid) },
    { MATROSKA_ID_FILENAME,     EBML_UTF8, 0, 0, offsetof(MatroskaAttachment, filename) },
    { MATROSKA_ID_FILEMIMETYPE, EBML_STR,  0, 0, offsetof(MatroskaAttachment, mime) },
    { MATROSKA_ID_FILEDATA,     EBML_BIN,  0, 0, offsetof(MatroskaAttachment, bin) },
    { MATROSKA_ID_FILEDESC,     EBML_UTF8, 0, 0, offsetof(MatroskaAttachment, description) },
    CHILD_OF(matroska_attachments)
};

static EbmlSyntax matroska_attachments[] = {
    { MATROSKA_ID_ATTACHEDFILE, EBML_NEST, 0, sizeof(MatroskaAttachment), offsetof(MatroskaDemuxContext, attachments), { .n = matroska_attachment } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_chapter_display[] = {
    { MATROSKA_ID_CHAPSTRING,  EBML_UTF8, 0, 0, offsetof(MatroskaChapter, title) },
    { MATROSKA_ID_CHAPLANG,    EBML_NONE },
    { MATROSKA_ID_CHAPCOUNTRY, EBML_NONE },
    CHILD_OF(matroska_chapter_entry)
};

static EbmlSyntax matroska_chapter_entry[] = {
    { MATROSKA_ID_CHAPTERTIMESTART,   EBML_UINT, 0, 0, offsetof(MatroskaChapter, start), { .u = AV_NOPTS_VALUE } },
    { MATROSKA_ID_CHAPTERTIMEEND,     EBML_UINT, 0, 0, offsetof(MatroskaChapter, end),   { .u = AV_NOPTS_VALUE } },
    { MATROSKA_ID_CHAPTERUID,         EBML_UINT, 0, 0, offsetof(MatroskaChapter, uid) },
    { MATROSKA_ID_CHAPTERDISPLAY,     EBML_NEST, 0, 0,                        0,         { .n = matroska_chapter_display } },
    { MATROSKA_ID_CHAPTERFLAGHIDDEN,  EBML_NONE },
    { MATROSKA_ID_CHAPTERFLAGENABLED, EBML_NONE },
    { MATROSKA_ID_CHAPTERPHYSEQUIV,   EBML_NONE },
    { MATROSKA_ID_CHAPTERATOM,        EBML_NONE },
    CHILD_OF(matroska_chapter)
};

static EbmlSyntax matroska_chapter[] = {
    { MATROSKA_ID_CHAPTERATOM,        EBML_NEST, 0, sizeof(MatroskaChapter), offsetof(MatroskaDemuxContext, chapters), { .n = matroska_chapter_entry } },
    { MATROSKA_ID_EDITIONUID,         EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGHIDDEN,  EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGDEFAULT, EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGORDERED, EBML_NONE },
    CHILD_OF(matroska_chapters)
};

static EbmlSyntax matroska_chapters[] = {
    { MATROSKA_ID_EDITIONENTRY, EBML_NEST, 0, 0, 0, { .n = matroska_chapter } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_index_pos[] = {
    { MATROSKA_ID_CUETRACK,           EBML_UINT, 0, 0, offsetof(MatroskaIndexPos, track) },
    { MATROSKA_ID_CUECLUSTERPOSITION, EBML_UINT, 0, 0, offsetof(MatroskaIndexPos, pos) },
    { MATROSKA_ID_CUERELATIVEPOSITION,EBML_NONE },
    { MATROSKA_ID_CUEDURATION,        EBML_NONE },
    { MATROSKA_ID_CUEBLOCKNUMBER,     EBML_NONE },
    CHILD_OF(matroska_index_entry)
};

static EbmlSyntax matroska_index_entry[] = {
    { MATROSKA_ID_CUETIME,          EBML_UINT, 0, 0,                        offsetof(MatroskaIndex, time) },
    { MATROSKA_ID_CUETRACKPOSITION, EBML_NEST, 0, sizeof(MatroskaIndexPos), offsetof(MatroskaIndex, pos), { .n = matroska_index_pos } },
    CHILD_OF(matroska_index)
};

static EbmlSyntax matroska_index[] = {
    { MATROSKA_ID_POINTENTRY, EBML_NEST, 0, sizeof(MatroskaIndex), offsetof(MatroskaDemuxContext, index), { .n = matroska_index_entry } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_simpletag[] = {
    { MATROSKA_ID_TAGNAME,        EBML_UTF8, 0, 0,                   offsetof(MatroskaTag, name) },
    { MATROSKA_ID_TAGSTRING,      EBML_UTF8, 0, 0,                   offsetof(MatroskaTag, string) },
    { MATROSKA_ID_TAGLANG,        EBML_STR,  0, 0,                   offsetof(MatroskaTag, lang), { .s = "und" } },
    { MATROSKA_ID_TAGDEFAULT,     EBML_UINT, 0, 0,                   offsetof(MatroskaTag, def) },
    { MATROSKA_ID_TAGDEFAULT_BUG, EBML_UINT, 0, 0,                   offsetof(MatroskaTag, def) },
    { MATROSKA_ID_SIMPLETAG,      EBML_NEST, 0, sizeof(MatroskaTag), offsetof(MatroskaTag, sub),  { .n = matroska_simpletag } },
    CHILD_OF(matroska_tag)
};

static EbmlSyntax matroska_tagtargets[] = {
    { MATROSKA_ID_TAGTARGETS_TYPE,       EBML_STR,  0, 0, offsetof(MatroskaTagTarget, type) },
    { MATROSKA_ID_TAGTARGETS_TYPEVALUE,  EBML_UINT, 0, 0, offsetof(MatroskaTagTarget, typevalue),  { .u = 50 } },
    { MATROSKA_ID_TAGTARGETS_TRACKUID,   EBML_UINT, 0, 0, offsetof(MatroskaTagTarget, trackuid),   { .u = 0 } },
    { MATROSKA_ID_TAGTARGETS_CHAPTERUID, EBML_UINT, 0, 0, offsetof(MatroskaTagTarget, chapteruid), { .u = 0 } },
    { MATROSKA_ID_TAGTARGETS_ATTACHUID,  EBML_UINT, 0, 0, offsetof(MatroskaTagTarget, attachuid),  { .u = 0 } },
    CHILD_OF(matroska_tag)
};

static EbmlSyntax matroska_tag[] = {
    { MATROSKA_ID_SIMPLETAG,  EBML_NEST, 0, sizeof(MatroskaTag), offsetof(MatroskaTags, tag),    { .n = matroska_simpletag } },
    { MATROSKA_ID_TAGTARGETS, EBML_NEST, 0, 0,                   offsetof(MatroskaTags, target), { .n = matroska_tagtargets } },
    CHILD_OF(matroska_tags)
};

static EbmlSyntax matroska_tags[] = {
    { MATROSKA_ID_TAG, EBML_NEST, 0, sizeof(MatroskaTags), offsetof(MatroskaDemuxContext, tags), { .n = matroska_tag } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_seekhead_entry[] = {
    { MATROSKA_ID_SEEKID,       EBML_UINT, 0, 0, offsetof(MatroskaSeekhead, id) },
    { MATROSKA_ID_SEEKPOSITION, EBML_UINT, 0, 0, offsetof(MatroskaSeekhead, pos), { .u = -1 } },
    CHILD_OF(matroska_seekhead)
};

static EbmlSyntax matroska_seekhead[] = {
    { MATROSKA_ID_SEEKENTRY, EBML_NEST, 0, sizeof(MatroskaSeekhead), offsetof(MatroskaDemuxContext, seekhead), { .n = matroska_seekhead_entry } },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_segment[] = {
    { MATROSKA_ID_CLUSTER,     EBML_STOP },
    { MATROSKA_ID_INFO,        EBML_LEVEL1, 0, 0, 0, { .n = matroska_info } },
    { MATROSKA_ID_TRACKS,      EBML_LEVEL1, 0, 0, 0, { .n = matroska_tracks } },
    { MATROSKA_ID_ATTACHMENTS, EBML_LEVEL1, 0, 0, 0, { .n = matroska_attachments } },
    { MATROSKA_ID_CHAPTERS,    EBML_LEVEL1, 0, 0, 0, { .n = matroska_chapters } },
    { MATROSKA_ID_CUES,        EBML_LEVEL1, 0, 0, 0, { .n = matroska_index } },
    { MATROSKA_ID_TAGS,        EBML_LEVEL1, 0, 0, 0, { .n = matroska_tags } },
    { MATROSKA_ID_SEEKHEAD,    EBML_LEVEL1, 0, 0, 0, { .n = matroska_seekhead } },
    { 0 }   /* We don't want to go back to level 0, so don't add the parent. */
};

static EbmlSyntax matroska_segments[] = {
    { MATROSKA_ID_SEGMENT, EBML_NEST, 0, 0, 0, { .n = matroska_segment } },
    { 0 }
};

static EbmlSyntax matroska_blockmore[] = {
    { MATROSKA_ID_BLOCKADDID,      EBML_UINT, 0, 0, offsetof(MatroskaBlockMore,additional_id), { .u = MATROSKA_BLOCK_ADD_ID_OPAQUE } },
    { MATROSKA_ID_BLOCKADDITIONAL, EBML_BIN,  0, 0, offsetof(MatroskaBlockMore,additional) },
    CHILD_OF(matroska_blockadditions)
};

static EbmlSyntax matroska_blockadditions[] = {
    { MATROSKA_ID_BLOCKMORE, EBML_NEST, 0, sizeof(MatroskaBlockMore), offsetof(MatroskaBlock, blockmore), { .n = matroska_blockmore } },
    CHILD_OF(matroska_blockgroup)
};

static EbmlSyntax matroska_blockgroup[] = {
    { MATROSKA_ID_BLOCK,          EBML_BIN,  0, 0, offsetof(MatroskaBlock, bin) },
    { MATROSKA_ID_BLOCKADDITIONS, EBML_NEST, 0, 0, 0, { .n = matroska_blockadditions} },
    { MATROSKA_ID_BLOCKDURATION,  EBML_UINT, 0, 0, offsetof(MatroskaBlock, duration) },
    { MATROSKA_ID_DISCARDPADDING, EBML_SINT, 0, 0, offsetof(MatroskaBlock, discard_padding) },
    { MATROSKA_ID_BLOCKREFERENCE, EBML_SINT, 1, 0, offsetof(MatroskaBlock, reference) },
    { MATROSKA_ID_CODECSTATE,     EBML_NONE },
    {                          1, EBML_UINT, 0, 0, offsetof(MatroskaBlock, non_simple), { .u = 1 } },
    CHILD_OF(matroska_cluster_parsing)
};

// The following array contains SimpleBlock and BlockGroup twice
// in order to reuse the other values for matroska_cluster_enter.
static EbmlSyntax matroska_cluster_parsing[] = {
    { MATROSKA_ID_SIMPLEBLOCK,     EBML_BIN,  0, 0, offsetof(MatroskaBlock, bin) },
    { MATROSKA_ID_BLOCKGROUP,      EBML_NEST, 0, 0, 0, { .n = matroska_blockgroup } },
    { MATROSKA_ID_CLUSTERTIMECODE, EBML_UINT, 0, 0, offsetof(MatroskaCluster, timecode) },
    { MATROSKA_ID_SIMPLEBLOCK,     EBML_STOP },
    { MATROSKA_ID_BLOCKGROUP,      EBML_STOP },
    { MATROSKA_ID_CLUSTERPOSITION, EBML_NONE },
    { MATROSKA_ID_CLUSTERPREVSIZE, EBML_NONE },
    CHILD_OF(matroska_segment)
};

static EbmlSyntax matroska_cluster_enter[] = {
    { MATROSKA_ID_CLUSTER,     EBML_NEST, 0, 0, 0, { .n = &matroska_cluster_parsing[2] } },
    { 0 }
};
#undef CHILD_OF

static const CodecMime mkv_image_mime_tags[] = {
    {"image/gif"                  , AV_CODEC_ID_GIF},
    {"image/jpeg"                 , AV_CODEC_ID_MJPEG},
    {"image/png"                  , AV_CODEC_ID_PNG},
    {"image/tiff"                 , AV_CODEC_ID_TIFF},

    {""                           , AV_CODEC_ID_NONE}
};

static const CodecMime mkv_mime_tags[] = {
    {"application/x-truetype-font", AV_CODEC_ID_TTF},
    {"application/x-font"         , AV_CODEC_ID_TTF},
    {"application/vnd.ms-opentype", AV_CODEC_ID_OTF},
    {"binary"                     , AV_CODEC_ID_BIN_DATA},

    {""                           , AV_CODEC_ID_NONE}
};

static const char * const matroska_video_stereo_plane[MATROSKA_VIDEO_STEREO_PLANE_COUNT] = {
    "left",
    "right",
    "background",
};

static const char *const matroska_doctypes[] = { "matroska", "webm" };

/*
 * This function prepares the status for parsing of level 1 elements.
 */
static int matroska_reset_status(MatroskaDemuxContext *matroska,
                                 uint32_t id, int64_t position)
{
    int64_t err = 0;
    if (position >= 0) {
        err = avio_seek(matroska->ctx->pb, position, SEEK_SET);
        if (err > 0)
            err = 0;
    } else
        position = avio_tell(matroska->ctx->pb);

    matroska->current_id    = id;
    matroska->num_levels    = 1;
    matroska->unknown_count = 0;
    matroska->resync_pos    = position;
    if (id)
        matroska->resync_pos -= (av_log2(id) + 7) / 8;

    return err;
}

static int matroska_resync(MatroskaDemuxContext *matroska, int64_t last_pos)
{
    AVIOContext *pb = matroska->ctx->pb;
    uint32_t id;

    /* Try to seek to the last position to resync from. If this doesn't work,
     * we resync from the earliest position available: The start of the buffer. */
    if (last_pos < avio_tell(pb) && avio_seek(pb, last_pos + 1, SEEK_SET) < 0) {
        av_log(matroska->ctx, AV_LOG_WARNING,
               "Seek to desired resync point failed. Seeking to "
               "earliest point available instead.\n");
        avio_seek(pb, FFMAX(avio_tell(pb) + (pb->buffer - pb->buf_ptr),
                            last_pos + 1), SEEK_SET);
    }

    id = avio_rb32(pb);

    // try to find a toplevel element
    while (!avio_feof(pb)) {
        if (id == MATROSKA_ID_INFO     || id == MATROSKA_ID_TRACKS      ||
            id == MATROSKA_ID_CUES     || id == MATROSKA_ID_TAGS        ||
            id == MATROSKA_ID_SEEKHEAD || id == MATROSKA_ID_ATTACHMENTS ||
            id == MATROSKA_ID_CLUSTER  || id == MATROSKA_ID_CHAPTERS) {
            /* Prepare the context for parsing of a level 1 element. */
            matroska_reset_status(matroska, id, -1);
            /* Given that we are here means that an error has occurred,
             * so treat the segment as unknown length in order not to
             * discard valid data that happens to be beyond the designated
             * end of the segment. */
            matroska->levels[0].length = EBML_UNKNOWN_LENGTH;
            return 0;
        }
        id = (id << 8) | avio_r8(pb);
    }

    matroska->done = 1;
    return pb->error ? pb->error : AVERROR_EOF;
}

/*
 * Read: an "EBML number", which is defined as a variable-length
 * array of bytes. The first byte indicates the length by giving a
 * number of 0-bits followed by a one. The position of the first
 * "one" bit inside the first byte indicates the length of this
 * number.
 * Returns: number of bytes read, < 0 on error
 */
static int ebml_read_num(MatroskaDemuxContext *matroska, AVIOContext *pb,
                         int max_size, uint64_t *number, int eof_forbidden)
{
    int read, n = 1;
    uint64_t total;
    int64_t pos;

    /* The first byte tells us the length in bytes - except when it is zero. */
    total = avio_r8(pb);
    if (pb->eof_reached)
        goto err;

    /* get the length of the EBML number */
    read = 8 - ff_log2_tab[total];

    if (!total || read > max_size) {
        pos = avio_tell(pb) - 1;
        if (!total) {
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "0x00 at pos %"PRId64" (0x%"PRIx64") invalid as first byte "
                   "of an EBML number\n", pos, pos);
        } else {
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Length %d indicated by an EBML number's first byte 0x%02x "
                   "at pos %"PRId64" (0x%"PRIx64") exceeds max length %d.\n",
                   read, (uint8_t) total, pos, pos, max_size);
        }
        return AVERROR_INVALIDDATA;
    }

    /* read out length */
    total ^= 1 << ff_log2_tab[total];
    while (n++ < read)
        total = (total << 8) | avio_r8(pb);

    if (pb->eof_reached) {
        eof_forbidden = 1;
        goto err;
    }

    *number = total;

    return read;

err:
    pos = avio_tell(pb);
    if (pb->error) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Read error at pos. %"PRIu64" (0x%"PRIx64")\n",
               pos, pos);
        return pb->error;
    }
    if (eof_forbidden) {
        av_log(matroska->ctx, AV_LOG_ERROR, "File ended prematurely "
               "at pos. %"PRIu64" (0x%"PRIx64")\n", pos, pos);
        return AVERROR(EIO);
    }
    return AVERROR_EOF;
}

/**
 * Read a EBML length value.
 * This needs special handling for the "unknown length" case which has multiple
 * encodings.
 */
static int ebml_read_length(MatroskaDemuxContext *matroska, AVIOContext *pb,
                            uint64_t *number)
{
    int res = ebml_read_num(matroska, pb, 8, number, 1);
    if (res > 0 && *number + 1 == 1ULL << (7 * res))
        *number = EBML_UNKNOWN_LENGTH;
    return res;
}

/*
 * Read the next element as an unsigned int.
 * Returns NEEDS_CHECKING unless size == 0.
 */
static int ebml_read_uint(AVIOContext *pb, int size,
                          uint64_t default_value, uint64_t *num)
{
    int n = 0;

    if (size == 0) {
        *num = default_value;
        return 0;
    }
    /* big-endian ordering; build up number */
    *num = 0;
    while (n++ < size)
        *num = (*num << 8) | avio_r8(pb);

    return NEEDS_CHECKING;
}

/*
 * Read the next element as a signed int.
 * Returns NEEDS_CHECKING unless size == 0.
 */
static int ebml_read_sint(AVIOContext *pb, int size,
                          int64_t default_value, int64_t *num)
{
    int n = 1;

    if (size == 0) {
        *num = default_value;
        return 0;
    } else {
        *num = sign_extend(avio_r8(pb), 8);

        /* big-endian ordering; build up number */
        while (n++ < size)
            *num = ((uint64_t)*num << 8) | avio_r8(pb);
    }

    return NEEDS_CHECKING;
}

/*
 * Read the next element as a float.
 * Returns 0 if size == 0, NEEDS_CHECKING or < 0 on obvious failure.
 */
static int ebml_read_float(AVIOContext *pb, int size,
                           double default_value, double *num)
{
    if (size == 0) {
        *num = default_value;
        return 0;
    } else if (size == 4) {
        *num = av_int2float(avio_rb32(pb));
    } else if (size == 8) {
        *num = av_int2double(avio_rb64(pb));
    } else
        return AVERROR_INVALIDDATA;

    return NEEDS_CHECKING;
}

/*
 * Read the next element as an ASCII string.
 * 0 is success, < 0 or NEEDS_CHECKING is failure.
 */
static int ebml_read_ascii(AVIOContext *pb, int size,
                           const char *default_value, char **str)
{
    char *res;
    int ret;

    if (size == 0 && default_value) {
        res = av_strdup(default_value);
        if (!res)
            return AVERROR(ENOMEM);
    } else {
        /* EBML strings are usually not 0-terminated, so we allocate one
         * byte more, read the string and NUL-terminate it ourselves. */
        if (!(res = av_malloc(size + 1)))
            return AVERROR(ENOMEM);
        if ((ret = avio_read(pb, (uint8_t *) res, size)) != size) {
            av_free(res);
            return ret < 0 ? ret : NEEDS_CHECKING;
        }
        (res)[size] = '\0';
    }
    av_free(*str);
    *str = res;

    return 0;
}

/*
 * Read the next element as binary data.
 * 0 is success, < 0 or NEEDS_CHECKING is failure.
 */
static int ebml_read_binary(AVIOContext *pb, int length,
                            int64_t pos, EbmlBin *bin)
{
    int ret;

    ret = av_buffer_realloc(&bin->buf, length + AV_INPUT_BUFFER_PADDING_SIZE);
    if (ret < 0)
        return ret;
    memset(bin->buf->data + length, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    bin->data = bin->buf->data;
    bin->size = length;
    bin->pos  = pos;
    if ((ret = avio_read(pb, bin->data, length)) != length) {
        av_buffer_unref(&bin->buf);
        bin->data = NULL;
        bin->size = 0;
        return ret < 0 ? ret : NEEDS_CHECKING;
    }

    return 0;
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_master(MatroskaDemuxContext *matroska,
                            uint64_t length, int64_t pos)
{
    MatroskaLevel *level;

    if (matroska->num_levels >= EBML_MAX_DEPTH) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "File moves beyond max. allowed depth (%d)\n", EBML_MAX_DEPTH);
        return AVERROR(ENOSYS);
    }

    level         = &matroska->levels[matroska->num_levels++];
    level->start  = pos;
    level->length = length;

    return 0;
}

/*
 * Read a signed "EBML number"
 * Return: number of bytes processed, < 0 on error
 */
static int matroska_ebmlnum_sint(MatroskaDemuxContext *matroska,
                                 AVIOContext *pb, int64_t *num)
{
    uint64_t unum;
    int res;

    /* read as unsigned number first */
    if ((res = ebml_read_num(matroska, pb, 8, &unum, 1)) < 0)
        return res;

    /* make signed (weird way) */
    *num = unum - ((1LL << (7 * res - 1)) - 1);

    return res;
}

static int ebml_parse(MatroskaDemuxContext *matroska,
                      EbmlSyntax *syntax, void *data);

static EbmlSyntax *ebml_parse_id(EbmlSyntax *syntax, uint32_t id)
{
    int i;

    // Whoever touches this should be aware of the duplication
    // existing in matroska_cluster_parsing.
    for (i = 0; syntax[i].id; i++)
        if (id == syntax[i].id)
            break;

    return &syntax[i];
}

static int ebml_parse_nest(MatroskaDemuxContext *matroska, EbmlSyntax *syntax,
                           void *data)
{
    int res;

    if (data) {
        for (int i = 0; syntax[i].id; i++) {
            void *dst = (char *)data + syntax[i].data_offset;
            switch (syntax[i].type) {
            case EBML_UINT:
                *(uint64_t *)dst = syntax[i].def.u;
                break;
            case EBML_SINT:
                *(int64_t *) dst = syntax[i].def.i;
                break;
            case EBML_FLOAT:
                *(double *)  dst = syntax[i].def.f;
                break;
            case EBML_STR:
            case EBML_UTF8:
                // the default may be NULL
                if (syntax[i].def.s) {
                    *(char**)dst = av_strdup(syntax[i].def.s);
                    if (!*(char**)dst)
                        return AVERROR(ENOMEM);
                }
                break;
            }
        }

        if (!matroska->levels[matroska->num_levels - 1].length) {
            matroska->num_levels--;
            return 0;
        }
    }

    do {
        res = ebml_parse(matroska, syntax, data);
    } while (!res);

    return res == LEVEL_ENDED ? 0 : res;
}

static int is_ebml_id_valid(uint32_t id)
{
    // Due to endian nonsense in Matroska, the highest byte with any bits set
    // will contain the leading length bit. This bit in turn identifies the
    // total byte length of the element by its position within the byte.
    unsigned int bits = av_log2(id);
    return id && (bits + 7) / 8 ==  (8 - bits % 8);
}

/*
 * Allocate and return the entry for the level1 element with the given ID. If
 * an entry already exists, return the existing entry.
 */
static MatroskaLevel1Element *matroska_find_level1_elem(MatroskaDemuxContext *matroska,
                                                        uint32_t id, int64_t pos)
{
    int i;
    MatroskaLevel1Element *elem;

    if (!is_ebml_id_valid(id))
        return NULL;

    // Some files link to all clusters; useless.
    if (id == MATROSKA_ID_CLUSTER)
        return NULL;

    // There can be multiple SeekHeads and Tags.
    for (i = 0; i < matroska->num_level1_elems; i++) {
        if (matroska->level1_elems[i].id == id) {
            if (matroska->level1_elems[i].pos == pos ||
                id != MATROSKA_ID_SEEKHEAD && id != MATROSKA_ID_TAGS)
                return &matroska->level1_elems[i];
        }
    }

    // Only a completely broken file would have more elements.
    if (matroska->num_level1_elems >= FF_ARRAY_ELEMS(matroska->level1_elems)) {
        av_log(matroska->ctx, AV_LOG_ERROR, "Too many level1 elements.\n");
        return NULL;
    }

    elem = &matroska->level1_elems[matroska->num_level1_elems++];
    *elem = (MatroskaLevel1Element){.id = id};

    return elem;
}

static int ebml_parse(MatroskaDemuxContext *matroska,
                      EbmlSyntax *syntax, void *data)
{
    static const uint64_t max_lengths[EBML_TYPE_COUNT] = {
        // Forbid unknown-length EBML_NONE elements.
        [EBML_NONE]  = EBML_UNKNOWN_LENGTH - 1,
        [EBML_UINT]  = 8,
        [EBML_SINT]  = 8,
        [EBML_FLOAT] = 8,
        // max. 16 MB for strings
        [EBML_STR]   = 0x1000000,
        [EBML_UTF8]  = 0x1000000,
        // max. 256 MB for binary data
        [EBML_BIN]   = 0x10000000,
        // no limits for anything else
    };
    AVIOContext *pb = matroska->ctx->pb;
    uint32_t id;
    uint64_t length;
    int64_t pos = avio_tell(pb), pos_alt;
    int res, update_pos = 1, level_check;
    MatroskaLevel1Element *level1_elem;
    MatroskaLevel *level = matroska->num_levels ? &matroska->levels[matroska->num_levels - 1] : NULL;

    if (!matroska->current_id) {
        uint64_t id;
        res = ebml_read_num(matroska, pb, 4, &id, 0);
        if (res < 0) {
            if (pb->eof_reached && res == AVERROR_EOF) {
                if (matroska->is_live)
                    // in live mode, finish parsing if EOF is reached.
                    return 1;
                if (level && pos == avio_tell(pb)) {
                    if (level->length == EBML_UNKNOWN_LENGTH) {
                        // Unknown-length levels automatically end at EOF.
                        matroska->num_levels--;
                        return LEVEL_ENDED;
                    } else {
                        av_log(matroska->ctx, AV_LOG_ERROR, "File ended prematurely "
                               "at pos. %"PRIu64" (0x%"PRIx64")\n", pos, pos);
                    }
                }
            }
            return res;
        }
        matroska->current_id = id | 1 << 7 * res;
        pos_alt = pos + res;
    } else {
        pos_alt = pos;
        pos    -= (av_log2(matroska->current_id) + 7) / 8;
    }

    id = matroska->current_id;

    syntax = ebml_parse_id(syntax, id);
    if (!syntax->id && id != EBML_ID_VOID && id != EBML_ID_CRC32) {
        if (level && level->length == EBML_UNKNOWN_LENGTH) {
            // Unknown-length levels end when an element from an upper level
            // in the hierarchy is encountered.
            while (syntax->def.n) {
                syntax = ebml_parse_id(syntax->def.n, id);
                if (syntax->id) {
                    matroska->num_levels--;
                    return LEVEL_ENDED;
                }
                // We have not encountered a known element; syntax is a sentinel.
                av_assert1(syntax->type == EBML_NONE);
            };
        }

        av_log(matroska->ctx, AV_LOG_DEBUG, "Unknown entry 0x%"PRIX32" at pos. "
                                            "%"PRId64"\n", id, pos);
        update_pos = 0; /* Don't update resync_pos as an error might have happened. */
    }

    if (data) {
        data = (char *) data + syntax->data_offset;
        if (syntax->list_elem_size) {
            EbmlList *list = data;
            void *newelem;

            if ((unsigned)list->nb_elem + 1 >= UINT_MAX / syntax->list_elem_size)
                return AVERROR(ENOMEM);
            newelem = av_fast_realloc(list->elem,
                                      &list->alloc_elem_size,
                                      (list->nb_elem + 1) * syntax->list_elem_size);
            if (!newelem)
                return AVERROR(ENOMEM);
            list->elem = newelem;
            data = (char *) list->elem + list->nb_elem * syntax->list_elem_size;
            memset(data, 0, syntax->list_elem_size);
            list->nb_elem++;
        }
    }

    if (syntax->type != EBML_STOP) {
        matroska->current_id = 0;
        if ((res = ebml_read_length(matroska, pb, &length)) < 0)
            return res;

        pos_alt += res;

        if (matroska->num_levels > 0) {
            if (length != EBML_UNKNOWN_LENGTH &&
                level->length != EBML_UNKNOWN_LENGTH) {
                uint64_t elem_end = pos_alt + length,
                        level_end = level->start + level->length;

                if (elem_end < level_end) {
                    level_check = 0;
                } else if (elem_end == level_end) {
                    level_check = LEVEL_ENDED;
                } else {
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "Element at 0x%"PRIx64" ending at 0x%"PRIx64" exceeds "
                           "containing master element ending at 0x%"PRIx64"\n",
                           pos, elem_end, level_end);
                    return AVERROR_INVALIDDATA;
                }
            } else if (length != EBML_UNKNOWN_LENGTH) {
                level_check = 0;
            } else if (level->length != EBML_UNKNOWN_LENGTH) {
                av_log(matroska->ctx, AV_LOG_ERROR, "Unknown-sized element "
                       "at 0x%"PRIx64" inside parent with finite size\n", pos);
                return AVERROR_INVALIDDATA;
            } else {
                level_check = 0;
                if (id != MATROSKA_ID_CLUSTER && (syntax->type == EBML_LEVEL1
                                              ||  syntax->type == EBML_NEST)) {
                    // According to the current specifications only clusters and
                    // segments are allowed to be unknown-length. We also accept
                    // other unknown-length master elements.
                    av_log(matroska->ctx, AV_LOG_WARNING,
                           "Found unknown-length element 0x%"PRIX32" other than "
                           "a cluster at 0x%"PRIx64". Spec-incompliant, but "
                           "parsing will nevertheless be attempted.\n", id, pos);
                    update_pos = -1;
                }
            }
        } else
            level_check = 0;

        if (max_lengths[syntax->type] && length > max_lengths[syntax->type]) {
            if (length != EBML_UNKNOWN_LENGTH) {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Invalid length 0x%"PRIx64" > 0x%"PRIx64" for element "
                       "with ID 0x%"PRIX32" at 0x%"PRIx64"\n",
                       length, max_lengths[syntax->type], id, pos);
            } else if (syntax->type != EBML_NONE) {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Element with ID 0x%"PRIX32" at pos. 0x%"PRIx64" has "
                       "unknown length, yet the length of an element of its "
                       "type must be known.\n", id, pos);
            } else {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Found unknown-length element with ID 0x%"PRIX32" at "
                       "pos. 0x%"PRIx64" for which no syntax for parsing is "
                       "available.\n", id, pos);
            }
            return AVERROR_INVALIDDATA;
        }

        if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            // Loosing sync will likely manifest itself as encountering unknown
            // elements which are not reliably distinguishable from elements
            // belonging to future extensions of the format.
            // We use a heuristic to detect such situations: If the current
            // element is not expected at the current syntax level and there
            // were only a few unknown elements in a row, then the element is
            // skipped or considered defective based upon the length of the
            // current element (i.e. how much would be skipped); if there were
            // more than a few skipped elements in a row and skipping the current
            // element would lead us more than SKIP_THRESHOLD away from the last
            // known good position, then it is inferred that an error occurred.
            // The dependency on the number of unknown elements in a row exists
            // because the distance to the last known good position is
            // automatically big if the last parsed element was big.
            // In both cases, each unknown element is considered equivalent to
            // UNKNOWN_EQUIV of skipped bytes for the check.
            // The whole check is only done for non-seekable output, because
            // in this situation skipped data can't simply be rechecked later.
            // This is especially important when using unkown length elements
            // as the check for whether a child exceeds its containing master
            // element is not effective in this situation.
            if (update_pos) {
                matroska->unknown_count = 0;
            } else {
                int64_t dist = length + UNKNOWN_EQUIV * matroska->unknown_count++;

                if (matroska->unknown_count > 3)
                    dist += pos_alt - matroska->resync_pos;

                if (dist > SKIP_THRESHOLD) {
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "Unknown element %"PRIX32" at pos. 0x%"PRIx64" with "
                           "length 0x%"PRIx64" considered as invalid data. Last "
                           "known good position 0x%"PRIx64", %d unknown elements"
                           " in a row\n", id, pos, length, matroska->resync_pos,
                           matroska->unknown_count);
                    return AVERROR_INVALIDDATA;
                }
            }
        }

        if (update_pos > 0) {
            // We have found an element that is allowed at this place
            // in the hierarchy and it passed all checks, so treat the beginning
            // of the element as the "last known good" position.
            matroska->resync_pos = pos;
        }

        if (!data && length != EBML_UNKNOWN_LENGTH)
            goto skip;
    }

    switch (syntax->type) {
    case EBML_UINT:
        res = ebml_read_uint(pb, length, syntax->def.u, data);
        break;
    case EBML_SINT:
        res = ebml_read_sint(pb, length, syntax->def.i, data);
        break;
    case EBML_FLOAT:
        res = ebml_read_float(pb, length, syntax->def.f, data);
        break;
    case EBML_STR:
    case EBML_UTF8:
        res = ebml_read_ascii(pb, length, syntax->def.s, data);
        break;
    case EBML_BIN:
        res = ebml_read_binary(pb, length, pos_alt, data);
        break;
    case EBML_LEVEL1:
    case EBML_NEST:
        if ((res = ebml_read_master(matroska, length, pos_alt)) < 0)
            return res;
        if (id == MATROSKA_ID_SEGMENT)
            matroska->segment_start = pos_alt;
        if (id == MATROSKA_ID_CUES)
            matroska->cues_parsing_deferred = 0;
        if (syntax->type == EBML_LEVEL1 &&
            (level1_elem = matroska_find_level1_elem(matroska, syntax->id, pos))) {
            if (!level1_elem->pos) {
                // Zero is not a valid position for a level 1 element.
                level1_elem->pos = pos;
            } else if (level1_elem->pos != pos)
                av_log(matroska->ctx, AV_LOG_ERROR, "Duplicate element\n");
            level1_elem->parsed = 1;
        }
        if (res = ebml_parse_nest(matroska, syntax->def.n, data))
            return res;
        break;
    case EBML_STOP:
        return 1;
    skip:
    default:
        if (length) {
            int64_t res2;
            if (ffio_limit(pb, length) != length) {
                // ffio_limit emits its own error message,
                // so we don't have to.
                return AVERROR(EIO);
            }
            if ((res2 = avio_skip(pb, length - 1)) >= 0) {
                // avio_skip might take us past EOF. We check for this
                // by skipping only length - 1 bytes, reading a byte and
                // checking the error flags. This is done in order to check
                // that the element has been properly skipped even when
                // no filesize (that ffio_limit relies on) is available.
                avio_r8(pb);
                res = NEEDS_CHECKING;
            } else
                res = res2;
        } else
            res = 0;
    }
    if (res) {
        if (res == NEEDS_CHECKING) {
            if (pb->eof_reached) {
                if (pb->error)
                    res = pb->error;
                else
                    res = AVERROR_EOF;
            } else
                goto level_check;
        }

        if (res == AVERROR_INVALIDDATA)
            av_log(matroska->ctx, AV_LOG_ERROR, "Invalid element\n");
        else if (res == AVERROR(EIO))
            av_log(matroska->ctx, AV_LOG_ERROR, "Read error\n");
        else if (res == AVERROR_EOF) {
            av_log(matroska->ctx, AV_LOG_ERROR, "File ended prematurely\n");
            res = AVERROR(EIO);
        }

        return res;
    }

level_check:
    if (syntax->is_counted && data) {
        CountedElement *elem = data;
        if (elem->count != UINT_MAX)
            elem->count++;
    }

    if (level_check == LEVEL_ENDED && matroska->num_levels) {
        level = &matroska->levels[matroska->num_levels - 1];
        pos   = avio_tell(pb);

        // Given that pos >= level->start no check for
        // level->length != EBML_UNKNOWN_LENGTH is necessary.
        while (matroska->num_levels && pos == level->start + level->length) {
            matroska->num_levels--;
            level--;
        }
    }

    return level_check;
}

static void ebml_free(EbmlSyntax *syntax, void *data)
{
    int i, j;
    for (i = 0; syntax[i].id; i++) {
        void *data_off = (char *) data + syntax[i].data_offset;
        switch (syntax[i].type) {
        case EBML_STR:
        case EBML_UTF8:
            av_freep(data_off);
            break;
        case EBML_BIN:
            av_buffer_unref(&((EbmlBin *) data_off)->buf);
            break;
        case EBML_LEVEL1:
        case EBML_NEST:
            if (syntax[i].list_elem_size) {
                EbmlList *list = data_off;
                char *ptr = list->elem;
                for (j = 0; j < list->nb_elem;
                     j++, ptr += syntax[i].list_elem_size)
                    ebml_free(syntax[i].def.n, ptr);
                av_freep(&list->elem);
                list->nb_elem = 0;
                list->alloc_elem_size = 0;
            } else
                ebml_free(syntax[i].def.n, data_off);
        default:
            break;
        }
    }
}

/*
 * Autodetecting...
 */
static int matroska_probe(const AVProbeData *p)
{
    uint64_t total = 0;
    int len_mask = 0x80, size = 1, n = 1, i;

    /* EBML header? */
    if (AV_RB32(p->buf) != EBML_ID_HEADER)
        return 0;

    /* length of header */
    total = p->buf[4];
    while (size <= 8 && !(total & len_mask)) {
        size++;
        len_mask >>= 1;
    }
    if (size > 8)
        return 0;
    total &= (len_mask - 1);
    while (n < size)
        total = (total << 8) | p->buf[4 + n++];

    if (total + 1 == 1ULL << (7 * size)){
        /* Unknown-length header - simply parse the whole buffer. */
        total = p->buf_size - 4 - size;
    } else {
        /* Does the probe data contain the whole header? */
        if (p->buf_size < 4 + size + total)
            return 0;
    }

    /* The header should contain a known document type. For now,
     * we don't parse the whole header but simply check for the
     * availability of that array of characters inside the header.
     * Not fully fool-proof, but good enough. */
    for (i = 0; i < FF_ARRAY_ELEMS(matroska_doctypes); i++) {
        size_t probelen = strlen(matroska_doctypes[i]);
        if (total < probelen)
            continue;
        for (n = 4 + size; n <= 4 + size + total - probelen; n++)
            if (!memcmp(p->buf + n, matroska_doctypes[i], probelen))
                return AVPROBE_SCORE_MAX;
    }

    // probably valid EBML header but no recognized doctype
    return AVPROBE_SCORE_EXTENSION;
}

static MatroskaTrack *matroska_find_track_by_num(MatroskaDemuxContext *matroska,
                                                 uint64_t num)
{
    MatroskaTrack *tracks = matroska->tracks.elem;
    int i;

    for (i = 0; i < matroska->tracks.nb_elem; i++)
        if (tracks[i].num == num)
            return &tracks[i];

    av_log(matroska->ctx, AV_LOG_ERROR, "Invalid track number %"PRIu64"\n", num);
    return NULL;
}

static int matroska_decode_buffer(uint8_t **buf, int *buf_size,
                                  MatroskaTrack *track)
{
    MatroskaTrackEncoding *encodings = track->encodings.elem;
    uint8_t *data = *buf;
    int isize = *buf_size;
    uint8_t *pkt_data = NULL;
    uint8_t av_unused *newpktdata;
    int pkt_size = isize;
    int result = 0;
    int olen;

    if (pkt_size >= 10000000U)
        return AVERROR_INVALIDDATA;

    switch (encodings[0].compression.algo) {
    case MATROSKA_TRACK_ENCODING_COMP_HEADERSTRIP:
    {
        int header_size = encodings[0].compression.settings.size;
        uint8_t *header = encodings[0].compression.settings.data;

        if (header_size && !header) {
            av_log(NULL, AV_LOG_ERROR, "Compression size but no data in headerstrip\n");
            return -1;
        }

        if (!header_size)
            return 0;

        pkt_size = isize + header_size;
        pkt_data = av_malloc(pkt_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!pkt_data)
            return AVERROR(ENOMEM);

        memcpy(pkt_data, header, header_size);
        memcpy(pkt_data + header_size, data, isize);
        break;
    }
    case MATROSKA_TRACK_ENCODING_COMP_LZO:
        do {
            int insize = isize;
            olen       = pkt_size *= 3;
            newpktdata = av_realloc(pkt_data, pkt_size + AV_LZO_OUTPUT_PADDING
                                                       + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!newpktdata) {
                result = AVERROR(ENOMEM);
                goto failed;
            }
            pkt_data = newpktdata;
            result   = av_lzo1x_decode(pkt_data, &olen, data, &insize);
        } while (result == AV_LZO_OUTPUT_FULL && pkt_size < 10000000);
        if (result) {
            result = AVERROR_INVALIDDATA;
            goto failed;
        }
        pkt_size -= olen;
        break;
#if CONFIG_ZLIB
    case MATROSKA_TRACK_ENCODING_COMP_ZLIB:
    {
        z_stream zstream = { 0 };
        if (!pkt_size || inflateInit(&zstream) != Z_OK)
            return -1;
        zstream.next_in  = data;
        zstream.avail_in = isize;
        do {
            pkt_size  *= 3;
            newpktdata = av_realloc(pkt_data, pkt_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!newpktdata) {
                inflateEnd(&zstream);
                result = AVERROR(ENOMEM);
                goto failed;
            }
            pkt_data          = newpktdata;
            zstream.avail_out = pkt_size - zstream.total_out;
            zstream.next_out  = pkt_data + zstream.total_out;
            result = inflate(&zstream, Z_NO_FLUSH);
        } while (result == Z_OK && pkt_size < 10000000);
        pkt_size = zstream.total_out;
        inflateEnd(&zstream);
        if (result != Z_STREAM_END) {
            if (result == Z_MEM_ERROR)
                result = AVERROR(ENOMEM);
            else
                result = AVERROR_INVALIDDATA;
            goto failed;
        }
        break;
    }
#endif
#if CONFIG_BZLIB
    case MATROSKA_TRACK_ENCODING_COMP_BZLIB:
    {
        bz_stream bzstream = { 0 };
        if (!pkt_size || BZ2_bzDecompressInit(&bzstream, 0, 0) != BZ_OK)
            return -1;
        bzstream.next_in  = data;
        bzstream.avail_in = isize;
        do {
            pkt_size  *= 3;
            newpktdata = av_realloc(pkt_data, pkt_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!newpktdata) {
                BZ2_bzDecompressEnd(&bzstream);
                result = AVERROR(ENOMEM);
                goto failed;
            }
            pkt_data           = newpktdata;
            bzstream.avail_out = pkt_size - bzstream.total_out_lo32;
            bzstream.next_out  = pkt_data + bzstream.total_out_lo32;
            result = BZ2_bzDecompress(&bzstream);
        } while (result == BZ_OK && pkt_size < 10000000);
        pkt_size = bzstream.total_out_lo32;
        BZ2_bzDecompressEnd(&bzstream);
        if (result != BZ_STREAM_END) {
            if (result == BZ_MEM_ERROR)
                result = AVERROR(ENOMEM);
            else
                result = AVERROR_INVALIDDATA;
            goto failed;
        }
        break;
    }
#endif
    default:
        return AVERROR_INVALIDDATA;
    }

    memset(pkt_data + pkt_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *buf      = pkt_data;
    *buf_size = pkt_size;
    return 0;

failed:
    av_free(pkt_data);
    return result;
}

static void matroska_convert_tag(AVFormatContext *s, EbmlList *list,
                                 AVDictionary **metadata, char *prefix)
{
    MatroskaTag *tags = list->elem;
    char key[1024];
    int i;

    for (i = 0; i < list->nb_elem; i++) {
        const char *lang = tags[i].lang &&
                           strcmp(tags[i].lang, "und") ? tags[i].lang : NULL;

        if (!tags[i].name) {
            av_log(s, AV_LOG_WARNING, "Skipping invalid tag with no TagName.\n");
            continue;
        }
        if (prefix)
            snprintf(key, sizeof(key), "%s/%s", prefix, tags[i].name);
        else
            av_strlcpy(key, tags[i].name, sizeof(key));
        if (tags[i].def || !lang) {
            av_dict_set(metadata, key, tags[i].string, 0);
            if (tags[i].sub.nb_elem)
                matroska_convert_tag(s, &tags[i].sub, metadata, key);
        }
        if (lang) {
            av_strlcat(key, "-", sizeof(key));
            av_strlcat(key, lang, sizeof(key));
            av_dict_set(metadata, key, tags[i].string, 0);
            if (tags[i].sub.nb_elem)
                matroska_convert_tag(s, &tags[i].sub, metadata, key);
        }
    }
    ff_metadata_conv(metadata, NULL, ff_mkv_metadata_conv);
}

static void matroska_convert_tags(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTags *tags = matroska->tags.elem;
    int i, j;

    for (i = 0; i < matroska->tags.nb_elem; i++) {
        if (tags[i].target.attachuid) {
            MatroskaAttachment *attachment = matroska->attachments.elem;
            int found = 0;
            for (j = 0; j < matroska->attachments.nb_elem; j++) {
                if (attachment[j].uid == tags[i].target.attachuid &&
                    attachment[j].stream) {
                    matroska_convert_tag(s, &tags[i].tag,
                                         &attachment[j].stream->metadata, NULL);
                    found = 1;
                }
            }
            if (!found) {
                av_log(s, AV_LOG_WARNING,
                       "The tags at index %d refer to a "
                       "non-existent attachment %"PRId64".\n",
                       i, tags[i].target.attachuid);
            }
        } else if (tags[i].target.chapteruid) {
            MatroskaChapter *chapter = matroska->chapters.elem;
            int found = 0;
            for (j = 0; j < matroska->chapters.nb_elem; j++) {
                if (chapter[j].uid == tags[i].target.chapteruid &&
                    chapter[j].chapter) {
                    matroska_convert_tag(s, &tags[i].tag,
                                         &chapter[j].chapter->metadata, NULL);
                    found = 1;
                }
            }
            if (!found) {
                av_log(s, AV_LOG_WARNING,
                       "The tags at index %d refer to a non-existent chapter "
                       "%"PRId64".\n",
                       i, tags[i].target.chapteruid);
            }
        } else if (tags[i].target.trackuid) {
            MatroskaTrack *track = matroska->tracks.elem;
            int found = 0;
            for (j = 0; j < matroska->tracks.nb_elem; j++) {
                if (track[j].uid == tags[i].target.trackuid &&
                    track[j].stream) {
                    matroska_convert_tag(s, &tags[i].tag,
                                         &track[j].stream->metadata, NULL);
                    found = 1;
               }
            }
            if (!found) {
                av_log(s, AV_LOG_WARNING,
                       "The tags at index %d refer to a non-existent track "
                       "%"PRId64".\n",
                       i, tags[i].target.trackuid);
            }
        } else {
            matroska_convert_tag(s, &tags[i].tag, &s->metadata,
                                 tags[i].target.type);
        }
    }
}

static int matroska_parse_seekhead_entry(MatroskaDemuxContext *matroska,
                                         int64_t pos)
{
    uint32_t saved_id  = matroska->current_id;
    int64_t before_pos = avio_tell(matroska->ctx->pb);
    int ret = 0;
    int ret2;

    /* seek */
    if (avio_seek(matroska->ctx->pb, pos, SEEK_SET) == pos) {
        /* We don't want to lose our seekhead level, so we add
         * a dummy. This is a crude hack. */
        if (matroska->num_levels == EBML_MAX_DEPTH) {
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Max EBML element depth (%d) reached, "
                   "cannot parse further.\n", EBML_MAX_DEPTH);
            ret = AVERROR_INVALIDDATA;
        } else {
            matroska->levels[matroska->num_levels] = (MatroskaLevel) { 0, EBML_UNKNOWN_LENGTH };
            matroska->num_levels++;
            matroska->current_id                   = 0;

            ret = ebml_parse(matroska, matroska_segment, matroska);
            if (ret == LEVEL_ENDED) {
                /* This can only happen if the seek brought us beyond EOF. */
                ret = AVERROR_EOF;
            }
        }
    }
    /* Seek back - notice that in all instances where this is used
     * it is safe to set the level to 1. */
    ret2 = matroska_reset_status(matroska, saved_id, before_pos);
    if (ret >= 0)
        ret = ret2;

    return ret;
}

static void matroska_execute_seekhead(MatroskaDemuxContext *matroska)
{
    EbmlList *seekhead_list = &matroska->seekhead;
    int i;

    // we should not do any seeking in the streaming case
    if (!(matroska->ctx->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return;

    for (i = 0; i < seekhead_list->nb_elem; i++) {
        MatroskaSeekhead *seekheads = seekhead_list->elem;
        uint32_t id = seekheads[i].id;
        int64_t pos = seekheads[i].pos + matroska->segment_start;
        MatroskaLevel1Element *elem;

        if (id != seekheads[i].id || pos < matroska->segment_start)
            continue;

        elem = matroska_find_level1_elem(matroska, id, pos);
        if (!elem || elem->parsed)
            continue;

        elem->pos = pos;

        // defer cues parsing until we actually need cue data.
        if (id == MATROSKA_ID_CUES)
            continue;

        if (matroska_parse_seekhead_entry(matroska, pos) < 0) {
            // mark index as broken
            matroska->cues_parsing_deferred = -1;
            break;
        }

        elem->parsed = 1;
    }
}

static void matroska_add_index_entries(MatroskaDemuxContext *matroska)
{
    EbmlList *index_list;
    MatroskaIndex *index;
    uint64_t index_scale = 1;
    int i, j;

    if (matroska->ctx->flags & AVFMT_FLAG_IGNIDX)
        return;

    index_list = &matroska->index;
    index      = index_list->elem;
    if (index_list->nb_elem < 2)
        return;
    if (index[1].time > 1E14 / matroska->time_scale) {
        av_log(matroska->ctx, AV_LOG_WARNING, "Dropping apparently-broken index.\n");
        return;
    }
    for (i = 0; i < index_list->nb_elem; i++) {
        EbmlList *pos_list    = &index[i].pos;
        MatroskaIndexPos *pos = pos_list->elem;
        for (j = 0; j < pos_list->nb_elem; j++) {
            MatroskaTrack *track = matroska_find_track_by_num(matroska,
                                                              pos[j].track);
            if (track && track->stream)
                av_add_index_entry(track->stream,
                                   pos[j].pos + matroska->segment_start,
                                   index[i].time / index_scale, 0, 0,
                                   AVINDEX_KEYFRAME);
        }
    }
}

static void matroska_parse_cues(MatroskaDemuxContext *matroska) {
    int i;

    if (matroska->ctx->flags & AVFMT_FLAG_IGNIDX)
        return;

    for (i = 0; i < matroska->num_level1_elems; i++) {
        MatroskaLevel1Element *elem = &matroska->level1_elems[i];
        if (elem->id == MATROSKA_ID_CUES && !elem->parsed) {
            if (matroska_parse_seekhead_entry(matroska, elem->pos) < 0)
                matroska->cues_parsing_deferred = -1;
            elem->parsed = 1;
            break;
        }
    }

    matroska_add_index_entries(matroska);
}

static int matroska_parse_content_encodings(MatroskaTrackEncoding *encodings,
                                            unsigned nb_encodings,
                                            MatroskaTrack *track,
                                            char **key_id_base64, void *logctx)
{
    if (nb_encodings > 1) {
        av_log(logctx, AV_LOG_ERROR,
                "Multiple combined encodings not supported\n");
        return 0;
    }
    if (!nb_encodings)
        return 0;
    if (encodings->type) {
        if (encodings->encryption.key_id.size > 0) {
            /* Save the encryption key id to be stored later
             * as a metadata tag. */
            const int b64_size = AV_BASE64_SIZE(encodings->encryption.key_id.size);
            *key_id_base64 = av_malloc(b64_size);
            if (!*key_id_base64)
                return AVERROR(ENOMEM);

            av_base64_encode(*key_id_base64, b64_size,
                             encodings->encryption.key_id.data,
                             encodings->encryption.key_id.size);
        } else {
            encodings->scope = 0;
            av_log(logctx, AV_LOG_ERROR, "Unsupported encoding type\n");
        }
    } else if (
#if CONFIG_ZLIB
            encodings->compression.algo != MATROSKA_TRACK_ENCODING_COMP_ZLIB  &&
#endif
#if CONFIG_BZLIB
            encodings->compression.algo != MATROSKA_TRACK_ENCODING_COMP_BZLIB &&
#endif
            encodings->compression.algo != MATROSKA_TRACK_ENCODING_COMP_LZO   &&
            encodings->compression.algo != MATROSKA_TRACK_ENCODING_COMP_HEADERSTRIP) {
        encodings->scope = 0;
        av_log(logctx, AV_LOG_ERROR, "Unsupported encoding type\n");
    } else if (track->codec_priv.size && encodings[0].scope & 2) {
        uint8_t *codec_priv = track->codec_priv.data;
        int ret = matroska_decode_buffer(&track->codec_priv.data,
                                         &track->codec_priv.size,
                                         track);
        if (ret < 0) {
            track->codec_priv.data = NULL;
            track->codec_priv.size = 0;
            av_log(logctx, AV_LOG_ERROR,
                   "Failed to decode codec private data\n");
        }

        if (codec_priv != track->codec_priv.data) {
            av_buffer_unref(&track->codec_priv.buf);
            if (track->codec_priv.data) {
                track->codec_priv.buf = av_buffer_create(track->codec_priv.data,
                                                         track->codec_priv.size + AV_INPUT_BUFFER_PADDING_SIZE,
                                                         NULL, NULL, 0);
                if (!track->codec_priv.buf) {
                    av_freep(&track->codec_priv.data);
                    track->codec_priv.size = 0;
                    return AVERROR(ENOMEM);
                }
            }
        }
    }
    track->needs_decoding = !encodings->type &&
                            encodings->scope & 1 &&
                            (encodings->compression.algo !=
                                MATROSKA_TRACK_ENCODING_COMP_HEADERSTRIP ||
                             encodings->compression.settings.size);

    return 0;
}

static int matroska_aac_profile(char *codec_id)
{
    static const char *const aac_profiles[] = { "MAIN", "LC", "SSR" };
    int profile;

    for (profile = 0; profile < FF_ARRAY_ELEMS(aac_profiles); profile++)
        if (strstr(codec_id, aac_profiles[profile]))
            break;
    return profile + 1;
}

static int matroska_aac_sri(int samplerate)
{
    int sri;

    for (sri = 0; sri < FF_ARRAY_ELEMS(ff_mpeg4audio_sample_rates); sri++)
        if (ff_mpeg4audio_sample_rates[sri] == samplerate)
            break;
    return sri;
}

static void matroska_metadata_creation_time(AVDictionary **metadata, int64_t date_utc)
{
    /* Convert to seconds and adjust by number of seconds between 2001-01-01 and Epoch */
    avpriv_dict_set_timestamp(metadata, "creation_time", date_utc / 1000 + 978307200000000LL);
}

static int matroska_parse_flac(AVFormatContext *s,
                               MatroskaTrack *track,
                               int *offset)
{
    AVStream *st = track->stream;
    uint8_t *p = track->codec_priv.data;
    int size   = track->codec_priv.size;

    if (size < 8 + FLAC_STREAMINFO_SIZE || p[4] & 0x7f) {
        av_log(s, AV_LOG_WARNING, "Invalid FLAC private data\n");
        track->codec_priv.size = 0;
        return 0;
    }
    *offset = 8;
    track->codec_priv.size = 8 + FLAC_STREAMINFO_SIZE;

    p    += track->codec_priv.size;
    size -= track->codec_priv.size;

    /* parse the remaining metadata blocks if present */
    while (size >= 4) {
        int block_last, block_type, block_size;

        flac_parse_block_header(p, &block_last, &block_type, &block_size);

        p    += 4;
        size -= 4;
        if (block_size > size)
            return 0;

        /* check for the channel mask */
        if (block_type == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
            AVDictionary *dict = NULL;
            AVDictionaryEntry *chmask;

            ff_vorbis_comment(s, &dict, p, block_size, 0);
            chmask = av_dict_get(dict, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", NULL, 0);
            if (chmask) {
                uint64_t mask = strtol(chmask->value, NULL, 0);
                if (!mask || mask & ~0x3ffffULL) {
                    av_log(s, AV_LOG_WARNING,
                           "Invalid value of WAVEFORMATEXTENSIBLE_CHANNEL_MASK\n");
                } else
                    av_channel_layout_from_mask(&st->codecpar->ch_layout, mask);
            }
            av_dict_free(&dict);
        }

        p    += block_size;
        size -= block_size;
    }

    return 0;
}

static int mkv_field_order(const MatroskaDemuxContext *matroska, uint64_t field_order)
{
    int minor, micro, bttb = 0;

    /* workaround a bug in our Matroska muxer, introduced in version 57.36 alongside
     * this function, and fixed in 57.52 */
    if (matroska->muxingapp && sscanf(matroska->muxingapp, "Lavf57.%d.%d", &minor, &micro) == 2)
        bttb = (minor >= 36 && minor <= 51 && micro >= 100);

    switch (field_order) {
    case MATROSKA_VIDEO_FIELDORDER_PROGRESSIVE:
        return AV_FIELD_PROGRESSIVE;
    case MATROSKA_VIDEO_FIELDORDER_UNDETERMINED:
        return AV_FIELD_UNKNOWN;
    case MATROSKA_VIDEO_FIELDORDER_TT:
        return AV_FIELD_TT;
    case MATROSKA_VIDEO_FIELDORDER_BB:
        return AV_FIELD_BB;
    case MATROSKA_VIDEO_FIELDORDER_BT:
        return bttb ? AV_FIELD_TB : AV_FIELD_BT;
    case MATROSKA_VIDEO_FIELDORDER_TB:
        return bttb ? AV_FIELD_BT : AV_FIELD_TB;
    default:
        return AV_FIELD_UNKNOWN;
    }
}

static void mkv_stereo_mode_display_mul(int stereo_mode,
                                        int *h_width, int *h_height)
{
    switch (stereo_mode) {
        case MATROSKA_VIDEO_STEREOMODE_TYPE_MONO:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_RL:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_CHECKERBOARD_LR:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_RL:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTH_EYES_BLOCK_LR:
            break;
        case MATROSKA_VIDEO_STEREOMODE_TYPE_RIGHT_LEFT:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_LEFT_RIGHT:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_RL:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_COL_INTERLEAVED_LR:
            *h_width = 2;
            break;
        case MATROSKA_VIDEO_STEREOMODE_TYPE_BOTTOM_TOP:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_TOP_BOTTOM:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_RL:
        case MATROSKA_VIDEO_STEREOMODE_TYPE_ROW_INTERLEAVED_LR:
            *h_height = 2;
            break;
    }
}

static int mkv_stereo3d_conv(AVStream *st, MatroskaVideoStereoModeType stereo_mode)
{
    static const struct {
        char type;
        char flags;
    } stereo_mode_conv [] = {
#define STEREO_MODE_CONV(STEREOMODETYPE, STEREO3DTYPE, FLAGS, WDIV, HDIV, WEBM) \
    [(STEREOMODETYPE)] = { .type = (STEREO3DTYPE), .flags = (FLAGS) },
#define NOTHING(STEREOMODETYPE, WDIV, HDIV, WEBM)
        STEREOMODE_STEREO3D_MAPPING(STEREO_MODE_CONV, NOTHING)
    };
    AVStereo3D *stereo;
    size_t size;

    stereo = av_stereo3d_alloc_size(&size);
    if (!stereo)
        return AVERROR(ENOMEM);

    stereo->type  = stereo_mode_conv[stereo_mode].type;
    stereo->flags = stereo_mode_conv[stereo_mode].flags;

    if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_STEREO3D, stereo, size, 0)) {
        av_freep(&stereo);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int mkv_parse_video_color(AVStream *st, const MatroskaTrack *track) {
    const MatroskaTrackVideoColor *color = track->video.color.elem;
    const MatroskaMasteringMeta *mastering_meta;
    int has_mastering_primaries, has_mastering_luminance;

    if (!track->video.color.nb_elem)
        return 0;

    mastering_meta = &color->mastering_meta;
    // Mastering primaries are CIE 1931 coords, and must be > 0.
    has_mastering_primaries =
        mastering_meta->r_x > 0 && mastering_meta->r_y > 0 &&
        mastering_meta->g_x > 0 && mastering_meta->g_y > 0 &&
        mastering_meta->b_x > 0 && mastering_meta->b_y > 0 &&
        mastering_meta->white_x > 0 && mastering_meta->white_y > 0;
    has_mastering_luminance = mastering_meta->max_luminance >
                                  mastering_meta->min_luminance.el.f  &&
                              mastering_meta->min_luminance.el.f >= 0 &&
                              mastering_meta->min_luminance.count;

    if (color->matrix_coefficients != AVCOL_SPC_RESERVED)
        st->codecpar->color_space = color->matrix_coefficients;
    if (color->primaries != AVCOL_PRI_RESERVED &&
        color->primaries != AVCOL_PRI_RESERVED0)
        st->codecpar->color_primaries = color->primaries;
    if (color->transfer_characteristics != AVCOL_TRC_RESERVED &&
        color->transfer_characteristics != AVCOL_TRC_RESERVED0)
        st->codecpar->color_trc = color->transfer_characteristics;
    if (color->range != AVCOL_RANGE_UNSPECIFIED &&
        color->range <= AVCOL_RANGE_JPEG)
        st->codecpar->color_range = color->range;
    if (color->chroma_siting_horz != MATROSKA_COLOUR_CHROMASITINGHORZ_UNDETERMINED &&
        color->chroma_siting_vert != MATROSKA_COLOUR_CHROMASITINGVERT_UNDETERMINED &&
        color->chroma_siting_horz  < MATROSKA_COLOUR_CHROMASITINGHORZ_NB &&
        color->chroma_siting_vert  < MATROSKA_COLOUR_CHROMASITINGVERT_NB) {
        st->codecpar->chroma_location =
            av_chroma_location_pos_to_enum((color->chroma_siting_horz - 1) << 7,
                                           (color->chroma_siting_vert - 1) << 7);
    }
    if (color->max_cll && color->max_fall) {
        size_t size = 0;
        AVContentLightMetadata *metadata = av_content_light_metadata_alloc(&size);
        if (!metadata)
            return AVERROR(ENOMEM);
        if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_CONTENT_LIGHT_LEVEL, metadata, size, 0)) {
            av_freep(&metadata);
            return AVERROR(ENOMEM);
        }
        metadata->MaxCLL  = color->max_cll;
        metadata->MaxFALL = color->max_fall;
    }

    if (has_mastering_primaries || has_mastering_luminance) {
        size_t size = 0;
        AVMasteringDisplayMetadata *metadata = av_mastering_display_metadata_alloc_size(&size);
        if (!metadata)
            return AVERROR(ENOMEM);
        if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_MASTERING_DISPLAY_METADATA, metadata, size, 0)) {
            av_freep(&metadata);
            return AVERROR(ENOMEM);
        }
        if (has_mastering_primaries) {
            metadata->display_primaries[0][0] = av_d2q(mastering_meta->r_x, INT_MAX);
            metadata->display_primaries[0][1] = av_d2q(mastering_meta->r_y, INT_MAX);
            metadata->display_primaries[1][0] = av_d2q(mastering_meta->g_x, INT_MAX);
            metadata->display_primaries[1][1] = av_d2q(mastering_meta->g_y, INT_MAX);
            metadata->display_primaries[2][0] = av_d2q(mastering_meta->b_x, INT_MAX);
            metadata->display_primaries[2][1] = av_d2q(mastering_meta->b_y, INT_MAX);
            metadata->white_point[0] = av_d2q(mastering_meta->white_x, INT_MAX);
            metadata->white_point[1] = av_d2q(mastering_meta->white_y, INT_MAX);
            metadata->has_primaries = 1;
        }
        if (has_mastering_luminance) {
            metadata->max_luminance = av_d2q(mastering_meta->max_luminance, INT_MAX);
            metadata->min_luminance = av_d2q(mastering_meta->min_luminance.el.f, INT_MAX);
            metadata->has_luminance = 1;
        }
    }
    return 0;
}

static int mkv_create_display_matrix(AVStream *st,
                                     const MatroskaTrackVideoProjection *proj,
                                     void *logctx)
{
    AVPacketSideData *sd;
    double pitch = proj->pitch, yaw = proj->yaw, roll = proj->roll;
    int32_t *matrix;
    int hflip;

    if (pitch == 0.0 && yaw == 0.0 && roll == 0.0)
        return 0;

    /* Note: The following constants are exactly representable
     * as floating-point numbers. */
    if (pitch != 0.0 || (yaw != 0.0 && yaw != 180.0 && yaw != -180.0) ||
        isnan(roll)) {
        av_log(logctx, AV_LOG_WARNING, "Ignoring non-2D rectangular "
               "projection in stream %u (yaw %f, pitch %f, roll %f)\n",
               st->index, yaw, pitch, roll);
        return 0;
    }
    sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_DISPLAYMATRIX,
                                 9 * sizeof(*matrix), 0);
    if (!sd)
        return AVERROR(ENOMEM);
    matrix = (int32_t*)sd->data;

    hflip = yaw != 0.0;
    /* ProjectionPoseRoll is in the counter-clockwise direction
     * whereas av_display_rotation_set() expects its argument
     * to be oriented clockwise, so we need to negate roll.
     * Furthermore, if hflip is set, we need to negate it again
     * to account for the fact that the Matroska specifications
     * require the yaw rotation to be applied first. */
    av_display_rotation_set(matrix, roll * (2 * hflip - 1));
    av_display_matrix_flip(matrix, hflip, 0);

    return 0;
}

static int mkv_parse_video_projection(AVStream *st, const MatroskaTrack *track,
                                      void *logctx)
{
    AVSphericalMapping *spherical;
    const MatroskaTrackVideoProjection *mkv_projection = &track->video.projection;
    const uint8_t *priv_data = mkv_projection->private.data;
    enum AVSphericalProjection projection;
    size_t spherical_size;
    uint32_t l = 0, t = 0, r = 0, b = 0;
    uint32_t padding = 0;

    if (mkv_projection->private.size && priv_data[0] != 0) {
        av_log(logctx, AV_LOG_WARNING, "Unknown spherical metadata\n");
        return 0;
    }

    switch (track->video.projection.type) {
    case MATROSKA_VIDEO_PROJECTION_TYPE_RECTANGULAR:
        return mkv_create_display_matrix(st, mkv_projection, logctx);
    case MATROSKA_VIDEO_PROJECTION_TYPE_EQUIRECTANGULAR:
        if (track->video.projection.private.size == 20) {
            t = AV_RB32(priv_data +  4);
            b = AV_RB32(priv_data +  8);
            l = AV_RB32(priv_data + 12);
            r = AV_RB32(priv_data + 16);

            if (b >= UINT_MAX - t || r >= UINT_MAX - l) {
                av_log(logctx, AV_LOG_ERROR,
                       "Invalid bounding rectangle coordinates "
                       "%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32"\n",
                       l, t, r, b);
                return AVERROR_INVALIDDATA;
            }
        } else if (track->video.projection.private.size != 0) {
            av_log(logctx, AV_LOG_ERROR, "Unknown spherical metadata\n");
            return AVERROR_INVALIDDATA;
        }

        if (l || t || r || b)
            projection = AV_SPHERICAL_EQUIRECTANGULAR_TILE;
        else
            projection = AV_SPHERICAL_EQUIRECTANGULAR;
        break;
    case MATROSKA_VIDEO_PROJECTION_TYPE_CUBEMAP:
        if (track->video.projection.private.size < 4) {
            av_log(logctx, AV_LOG_ERROR, "Missing projection private properties\n");
            return AVERROR_INVALIDDATA;
        } else if (track->video.projection.private.size == 12) {
            uint32_t layout = AV_RB32(priv_data + 4);
            if (layout) {
                av_log(logctx, AV_LOG_WARNING,
                       "Unknown spherical cubemap layout %"PRIu32"\n", layout);
                return 0;
            }
            projection = AV_SPHERICAL_CUBEMAP;
            padding = AV_RB32(priv_data + 8);
        } else {
            av_log(logctx, AV_LOG_ERROR, "Unknown spherical metadata\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    default:
        av_log(logctx, AV_LOG_WARNING,
               "Unknown spherical metadata type %"PRIu64"\n",
               track->video.projection.type);
        return 0;
    }

    spherical = av_spherical_alloc(&spherical_size);
    if (!spherical)
        return AVERROR(ENOMEM);

    spherical->projection = projection;

    spherical->yaw   = (int32_t) (track->video.projection.yaw   * (1 << 16));
    spherical->pitch = (int32_t) (track->video.projection.pitch * (1 << 16));
    spherical->roll  = (int32_t) (track->video.projection.roll  * (1 << 16));

    spherical->padding = padding;

    spherical->bound_left   = l;
    spherical->bound_top    = t;
    spherical->bound_right  = r;
    spherical->bound_bottom = b;

    if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_SPHERICAL, spherical, spherical_size, 0)) {
        av_freep(&spherical);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int mkv_parse_dvcc_dvvc(AVFormatContext *s, AVStream *st, const MatroskaTrack *track,
                               EbmlBin *bin)
{
    return ff_isom_parse_dvcc_dvvc(s, st, bin->data, bin->size);
}

static int mkv_parse_block_addition_mappings(AVFormatContext *s, AVStream *st, MatroskaTrack *track)
{
    const EbmlList *mappings_list = &track->block_addition_mappings;
    MatroskaBlockAdditionMapping *mappings = mappings_list->elem;
    int ret;

    for (int i = 0; i < mappings_list->nb_elem; i++) {
        MatroskaBlockAdditionMapping *mapping = &mappings[i];
        uint64_t type = mapping->type;

        switch (mapping->type) {
        case MATROSKA_BLOCK_ADD_ID_TYPE_DEFAULT:
            av_log(s, AV_LOG_DEBUG,
                   "Explicit block Addition Mapping type \"Use BlockAddIDValue\", value %"PRIu64","
                   " name \"%s\" found.\n", mapping->value, mapping->name ? mapping->name : "");
            type = MATROSKA_BLOCK_ADD_ID_TYPE_OPAQUE;
            // fall-through
        case MATROSKA_BLOCK_ADD_ID_TYPE_OPAQUE:
        case MATROSKA_BLOCK_ADD_ID_TYPE_ITU_T_T35:
            if (mapping->value != type) {
                int strict = s->strict_std_compliance >= FF_COMPLIANCE_STRICT;
                av_log(s, strict ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "Invalid Block Addition Value 0x%"PRIx64" for Block Addition Mapping Type "
                       "0x%"PRIx64", name \"%s\"\n", mapping->value, mapping->type,
                       mapping->name ? mapping->name : "");
                if (strict)
                    return AVERROR_INVALIDDATA;
            }
            break;
        case MATROSKA_BLOCK_ADD_ID_TYPE_DVCC:
        case MATROSKA_BLOCK_ADD_ID_TYPE_DVVC:
            if ((ret = mkv_parse_dvcc_dvvc(s, st, track, &mapping->extradata)) < 0)
                return ret;

            break;
        default:
            av_log(s, AV_LOG_DEBUG,
                   "Unknown Block Addition Mapping type 0x%"PRIx64", value %"PRIu64", name \"%s\"\n",
                   mapping->type, mapping->value, mapping->name ? mapping->name : "");
            if (mapping->value < 2) {
                int strict = s->strict_std_compliance >= FF_COMPLIANCE_STRICT;
                av_log(s, strict ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "Invalid Block Addition value 0x%"PRIu64" for unknown Block Addition Mapping "
                       "type %"PRIx64", name \"%s\"\n", mapping->value, mapping->type,
                       mapping->name ? mapping->name : "");
                if (strict)
                    return AVERROR_INVALIDDATA;
            }
            break;
        }
    }

    return 0;
}

static int get_qt_codec(MatroskaTrack *track, uint32_t *fourcc, enum AVCodecID *codec_id)
{
    const AVCodecTag *codec_tags;

    codec_tags = track->type == MATROSKA_TRACK_TYPE_VIDEO ?
            ff_codec_movvideo_tags : ff_codec_movaudio_tags;

    /* Normalize noncompliant private data that starts with the fourcc
     * by expanding/shifting the data by 4 bytes and storing the data
     * size at the start. */
    if (ff_codec_get_id(codec_tags, AV_RL32(track->codec_priv.data))) {
        int ret = av_buffer_realloc(&track->codec_priv.buf,
                                    track->codec_priv.size + 4 + AV_INPUT_BUFFER_PADDING_SIZE);
        if (ret < 0)
            return ret;

        track->codec_priv.data = track->codec_priv.buf->data;
        memmove(track->codec_priv.data + 4, track->codec_priv.data, track->codec_priv.size);
        track->codec_priv.size += 4;
        AV_WB32(track->codec_priv.data, track->codec_priv.size);
    }

    *fourcc = AV_RL32(track->codec_priv.data + 4);
    *codec_id = ff_codec_get_id(codec_tags, *fourcc);

    return 0;
}

/* An enum with potential return values of the functions for parsing a track.
 * Apart from that all these functions can also indicate ordinary errors via
 * negative return values. */
enum {
    SKIP_TRACK = 1,
};

#define AAC_MAX_EXTRADATA_SIZE     5
#define TTA_EXTRADATA_SIZE        22
#define WAVPACK_EXTRADATA_SIZE     2
/* Performs the codec-specific part of parsing an audio track. */
static int mka_parse_audio_codec(MatroskaTrack *track, AVCodecParameters *par,
                                 const MatroskaDemuxContext *matroska,
                                 AVFormatContext *s, int *extradata_offset)
{
    uint8_t extradata[FFMAX3(AAC_MAX_EXTRADATA_SIZE,
                             TTA_EXTRADATA_SIZE,
                             WAVPACK_EXTRADATA_SIZE)];
    int extradata_size = 0; // > 0 means that the extradata buffer is used
    int ret;

    if (!strcmp(track->codec_id, "A_MS/ACM") &&
        track->codec_priv.size >= 14) {
        FFIOContext b;
        ffio_init_read_context(&b, track->codec_priv.data,
                               track->codec_priv.size);
        ret = ff_get_wav_header(s, &b.pub, par,
                                track->codec_priv.size, 0);
        if (ret < 0)
            return ret;
        *extradata_offset = FFMIN(track->codec_priv.size, 18);
        return 0;
    } else if (!strcmp(track->codec_id, "A_QUICKTIME") &&
               /* Normally 36, but allow noncompliant private data */
               track->codec_priv.size >= 32) {
        enum AVCodecID codec_id;
        uint32_t fourcc;
        uint16_t sample_size;

        ret = get_qt_codec(track, &fourcc, &codec_id);
        if (ret < 0)
            return ret;
        sample_size = AV_RB16(track->codec_priv.data + 26);
        if (fourcc == 0) {
            if (sample_size == 8) {
                fourcc = MKTAG('r','a','w',' ');
                codec_id = ff_codec_get_id(ff_codec_movaudio_tags, fourcc);
            } else if (sample_size == 16) {
                fourcc = MKTAG('t','w','o','s');
                codec_id = ff_codec_get_id(ff_codec_movaudio_tags, fourcc);
            }
        }
        if ((fourcc == MKTAG('t','w','o','s') ||
             fourcc == MKTAG('s','o','w','t')) && sample_size == 8)
            codec_id = AV_CODEC_ID_PCM_S8;
        par->codec_id  = codec_id;
        par->codec_tag = fourcc;
        return 0;
    }

    switch (par->codec_id) {
    case AV_CODEC_ID_PCM_S16BE:
        switch (track->audio.bitdepth) {
        case  8:
            par->codec_id = AV_CODEC_ID_PCM_U8;
            break;
        case 24:
            par->codec_id = AV_CODEC_ID_PCM_S24BE;
            break;
        case 32:
            par->codec_id = AV_CODEC_ID_PCM_S32BE;
            break;
        }
        break;
    case AV_CODEC_ID_PCM_S16LE:
        switch (track->audio.bitdepth) {
        case  8:
            par->codec_id = AV_CODEC_ID_PCM_U8;
            break;
        case 24:
            par->codec_id = AV_CODEC_ID_PCM_S24LE;
            break;
        case 32:
            par->codec_id = AV_CODEC_ID_PCM_S32LE;
            break;
        }
        break;
    case AV_CODEC_ID_PCM_F32LE:
        if (track->audio.bitdepth == 64)
            par->codec_id = AV_CODEC_ID_PCM_F64LE;
        break;
    case AV_CODEC_ID_AAC:
        if (!track->codec_priv.size) {
            int profile = matroska_aac_profile(track->codec_id);
            int sri     = matroska_aac_sri(track->audio.samplerate);

            extradata[0] = (profile << 3) | ((sri & 0x0E) >> 1);
            extradata[1] = ((sri & 0x01) << 7) | (track->audio.channels << 3);
            if (strstr(track->codec_id, "SBR")) {
                sri            = matroska_aac_sri(track->audio.out_samplerate);
                extradata[2]   = 0x56;
                extradata[3]   = 0xE5;
                extradata[4]   = 0x80 | (sri << 3);
                extradata_size = 5;
            } else
                extradata_size = 2;
        }
        break;
    case AV_CODEC_ID_ALAC:
        if (track->codec_priv.size && track->codec_priv.size < INT_MAX - 12 - AV_INPUT_BUFFER_PADDING_SIZE) {
            /* Only ALAC's magic cookie is stored in Matroska's track headers.
             * Create the "atom size", "tag", and "tag version" fields the
             * decoder expects manually. */
            ret = ff_alloc_extradata(par, 12 + track->codec_priv.size);
            if (ret < 0)
                return ret;
            AV_WB32(par->extradata, par->extradata_size);
            AV_WB32(&par->extradata[4], MKBETAG('a', 'l', 'a', 'c'));
            AV_WB32(&par->extradata[8], 0);
            memcpy(&par->extradata[12], track->codec_priv.data,
                   track->codec_priv.size);
        }
        break;
    case AV_CODEC_ID_TTA:
    {
        uint8_t *ptr;
        if (track->audio.channels > UINT16_MAX ||
            track->audio.bitdepth > UINT16_MAX) {
            av_log(matroska->ctx, AV_LOG_WARNING,
                   "Too large audio channel number %"PRIu64
                   " or bitdepth %"PRIu64". Skipping track.\n",
                   track->audio.channels, track->audio.bitdepth);
            if (matroska->ctx->error_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
            else
                return SKIP_TRACK;
        }
        if (track->audio.out_samplerate < 0 || track->audio.out_samplerate > INT_MAX)
            return AVERROR_INVALIDDATA;
        extradata_size = TTA_EXTRADATA_SIZE;
        ptr = extradata;
        bytestream_put_be32(&ptr, AV_RB32("TTA1"));
        bytestream_put_le16(&ptr, 1);
        bytestream_put_le16(&ptr, track->audio.channels);
        bytestream_put_le16(&ptr, track->audio.bitdepth);
        bytestream_put_le32(&ptr, track->audio.out_samplerate);
        bytestream_put_le32(&ptr, av_rescale(matroska->duration * matroska->time_scale,
                                             track->audio.out_samplerate,
                                             AV_TIME_BASE * 1000));
        break;
    }
    case AV_CODEC_ID_RA_144:
        track->audio.out_samplerate = 8000;
        track->audio.channels       = 1;
        break;
    case AV_CODEC_ID_RA_288:
    case AV_CODEC_ID_COOK:
    case AV_CODEC_ID_ATRAC3:
    case AV_CODEC_ID_SIPR:
    {
        const uint8_t *ptr = track->codec_priv.data;
        int flavor;

        if (!track->codec_priv.size)
            break;

        if (track->codec_priv.size < 46)
            return AVERROR_INVALIDDATA;
        ptr += 22;
        flavor                       = bytestream_get_be16(&ptr);
        track->audio.coded_framesize = bytestream_get_be32(&ptr);
        ptr += 12;
        track->audio.sub_packet_h    = bytestream_get_be16(&ptr);
        track->audio.frame_size      = bytestream_get_be16(&ptr);
        track->audio.sub_packet_size = bytestream_get_be16(&ptr);
        if (track->audio.coded_framesize <= 0 ||
            track->audio.sub_packet_h    <= 0 ||
            track->audio.frame_size      <= 0)
            return AVERROR_INVALIDDATA;

        if (par->codec_id == AV_CODEC_ID_RA_288) {
            if (track->audio.sub_packet_h & 1 || 2 * track->audio.frame_size
                != (int64_t)track->audio.sub_packet_h * track->audio.coded_framesize)
                return AVERROR_INVALIDDATA;
            par->block_align = track->audio.coded_framesize;
            track->codec_priv.size = 0;
        } else {
            if (par->codec_id == AV_CODEC_ID_SIPR) {
                static const int sipr_bit_rate[4] = { 6504, 8496, 5000, 16000 };
                if (flavor > 3)
                    return AVERROR_INVALIDDATA;
                track->audio.sub_packet_size = ff_sipr_subpk_size[flavor];
                par->bit_rate          = sipr_bit_rate[flavor];
            } else if (track->audio.sub_packet_size <= 0 ||
                        track->audio.frame_size % track->audio.sub_packet_size)
                return AVERROR_INVALIDDATA;
            par->block_align  = track->audio.sub_packet_size;
            *extradata_offset = 78;
        }
        track->audio.buf = av_malloc_array(track->audio.sub_packet_h,
                                            track->audio.frame_size);
        if (!track->audio.buf)
            return AVERROR(ENOMEM);
        break;
    }
    case AV_CODEC_ID_ATRAC1:
        /* ATRAC1 uses a constant frame size.
         * Typical ATRAC1 streams are either mono or stereo.
         * At most, ATRAC1 was used to store 8 channels of audio. */
        if (track->audio.channels > 8)
            return AVERROR_INVALIDDATA;
        par->block_align = track->audio.channels * 212;
        break;
    case AV_CODEC_ID_FLAC:
        if (track->codec_priv.size) {
            ret = matroska_parse_flac(s, track, extradata_offset);
            if (ret < 0)
                return ret;
        }
        break;
    case AV_CODEC_ID_WAVPACK:
        if (track->codec_priv.size < 2) {
            av_log(matroska->ctx, AV_LOG_INFO, "Assuming WavPack version 4.10 "
                   "in absence of valid CodecPrivate.\n");
            extradata_size = WAVPACK_EXTRADATA_SIZE;
            AV_WL16(extradata, 0x410);
        }
        break;
    }

    if (extradata_size > 0) {
        ret = ff_alloc_extradata(par, extradata_size);
        if (ret < 0)
            return ret;
        memcpy(par->extradata, extradata, extradata_size);
    }

    return 0;
}

/* Performs the generic part of parsing an audio track. */
static int mka_parse_audio(MatroskaTrack *track, AVStream *st,
                           AVCodecParameters *par,
                           const MatroskaDemuxContext *matroska,
                           AVFormatContext *s, int *extradata_offset)
{
    FFStream *const sti = ffstream(st);
    int ret;

    ret = mka_parse_audio_codec(track, par, matroska,
                                s, extradata_offset);
    if (ret)
        return ret;

    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->sample_rate = track->audio.out_samplerate;
    // channel layout may be already set by codec private checks above
    if (!av_channel_layout_check(&par->ch_layout)) {
        par->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
        par->ch_layout.nb_channels = track->audio.channels;
    }
    if (!par->bits_per_coded_sample)
        par->bits_per_coded_sample = track->audio.bitdepth;
    if (par->codec_id == AV_CODEC_ID_MP3 ||
        par->codec_id == AV_CODEC_ID_MLP ||
        par->codec_id == AV_CODEC_ID_TRUEHD)
        sti->need_parsing = AVSTREAM_PARSE_FULL;
    else if (par->codec_id != AV_CODEC_ID_AAC)
        sti->need_parsing = AVSTREAM_PARSE_HEADERS;
    if (track->codec_delay > 0) {
        par->initial_padding = av_rescale_q(track->codec_delay,
                                            (AVRational){1, 1000000000},
                                            (AVRational){1, par->codec_id == AV_CODEC_ID_OPUS ?
                                                            48000 : par->sample_rate});
    }
    if (track->seek_preroll > 0) {
        par->seek_preroll = av_rescale_q(track->seek_preroll,
                                         (AVRational){1, 1000000000},
                                         (AVRational){1, par->sample_rate});
    }

    return 0;
}

/* Performs the codec-specific part of parsing a video track. */
static int mkv_parse_video_codec(MatroskaTrack *track, AVCodecParameters *par,
                                 const MatroskaDemuxContext *matroska,
                                 int *extradata_offset)
{
    if (!strcmp(track->codec_id, "V_MS/VFW/FOURCC") &&
        track->codec_priv.size >= 40) {
        track->ms_compat    = 1;
        par->bits_per_coded_sample = AV_RL16(track->codec_priv.data + 14);
        par->codec_tag      = AV_RL32(track->codec_priv.data + 16);
        par->codec_id       = ff_codec_get_id(ff_codec_bmp_tags,
                                              par->codec_tag);
        if (!par->codec_id)
            par->codec_id   = ff_codec_get_id(ff_codec_movvideo_tags,
                                              par->codec_tag);
        *extradata_offset   = 40;
        return 0;
    } else if (!strcmp(track->codec_id, "V_QUICKTIME") &&
                track->codec_priv.size >= 21) {
        enum AVCodecID codec_id;
        uint32_t fourcc;
        int ret = get_qt_codec(track, &fourcc, &codec_id);
        if (ret < 0)
            return ret;
        if (codec_id == AV_CODEC_ID_NONE && AV_RL32(track->codec_priv.data+4) == AV_RL32("SMI ")) {
            fourcc   = MKTAG('S','V','Q','3');
            codec_id = ff_codec_get_id(ff_codec_movvideo_tags, fourcc);
        }
        par->codec_id = codec_id;
        if (codec_id == AV_CODEC_ID_NONE)
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "mov FourCC not found %s.\n", av_fourcc2str(fourcc));
        if (track->codec_priv.size >= 86) {
            FFIOContext b;
            unsigned bit_depth = AV_RB16(track->codec_priv.data + 82);
            ffio_init_read_context(&b, track->codec_priv.data,
                                   track->codec_priv.size);
            if (ff_get_qtpalette(codec_id, &b.pub, track->palette)) {
                bit_depth         &= 0x1F;
                track->has_palette = 1;
            }
            par->bits_per_coded_sample = bit_depth;
        }
        par->codec_tag = fourcc;
        return 0;
    }

    switch (par->codec_id) {
    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV20:
    case AV_CODEC_ID_RV30:
    case AV_CODEC_ID_RV40:
        *extradata_offset = 26;
        break;
    case AV_CODEC_ID_PRORES:
        if (track->codec_priv.size == 4)
            par->codec_tag = AV_RL32(track->codec_priv.data);
        break;
    case AV_CODEC_ID_VP9:
        /* we don't need any value stored in CodecPrivate.
         * make sure that it's not exported as extradata. */
        track->codec_priv.size = 0;
        break;
    }

    return 0;
}

/* Performs the generic part of parsing a video track. */
static int mkv_parse_video(MatroskaTrack *track, AVStream *st,
                           AVCodecParameters *par,
                           const MatroskaDemuxContext *matroska,
                           int *extradata_offset)
{
    FFStream *const sti = ffstream(st);
    MatroskaTrackPlane *planes;
    int display_width_mul  = 1;
    int display_height_mul = 1;
    int ret;

    if (track->video.color_space.size == 4)
        par->codec_tag = AV_RL32(track->video.color_space.data);

    ret = mkv_parse_video_codec(track, par, matroska,
                                extradata_offset);
    if (ret < 0)
        return ret;

    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->width      = track->video.pixel_width;
    par->height     = track->video.pixel_height;

    if (track->video.interlaced == MATROSKA_VIDEO_INTERLACE_FLAG_INTERLACED)
        par->field_order = mkv_field_order(matroska, track->video.field_order);
    else if (track->video.interlaced == MATROSKA_VIDEO_INTERLACE_FLAG_PROGRESSIVE)
        par->field_order = AV_FIELD_PROGRESSIVE;

    if (track->video.stereo_mode && track->video.stereo_mode < MATROSKA_VIDEO_STEREOMODE_TYPE_NB)
        mkv_stereo_mode_display_mul(track->video.stereo_mode,
                                    &display_width_mul, &display_height_mul);

    if (track->video.display_unit < MATROSKA_VIDEO_DISPLAYUNIT_UNKNOWN) {
        if (track->video.display_width       && track->video.display_height &&
            track->video.display_width != -1 && track->video.display_height != -1 &&
            track->video.cropped_height < INT64_MAX / track->video.display_width  / display_width_mul &&
            track->video.cropped_width  < INT64_MAX / track->video.display_height / display_height_mul)
            av_reduce(&st->sample_aspect_ratio.num,
                      &st->sample_aspect_ratio.den,
                      track->video.cropped_height * track->video.display_width  * display_width_mul,
                      track->video.cropped_width  * track->video.display_height * display_height_mul,
                      INT_MAX);
    }
    if (track->video.cropped_width  != track->video.pixel_width ||
        track->video.cropped_height != track->video.pixel_height) {
        uint8_t *cropping;
        AVPacketSideData *sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                       &st->codecpar->nb_coded_side_data,
                                                       AV_PKT_DATA_FRAME_CROPPING,
                                                       sizeof(uint32_t) * 4, 0);
        if (!sd)
            return AVERROR(ENOMEM);

        cropping = sd->data;
        bytestream_put_le32(&cropping, track->video.pixel_cropt);
        bytestream_put_le32(&cropping, track->video.pixel_cropb);
        bytestream_put_le32(&cropping, track->video.pixel_cropl);
        bytestream_put_le32(&cropping, track->video.pixel_cropr);
    }
    if (par->codec_id != AV_CODEC_ID_HEVC)
        sti->need_parsing = AVSTREAM_PARSE_HEADERS;

    if (track->default_duration) {
        int div = track->default_duration <= INT64_MAX ? 1 : 2;
        av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                  1000000000 / div, track->default_duration / div, 30000);
#if FF_API_R_FRAME_RATE
        if (   st->avg_frame_rate.num < st->avg_frame_rate.den * 1000LL
            && st->avg_frame_rate.num > st->avg_frame_rate.den * 5LL)
            st->r_frame_rate = st->avg_frame_rate;
#endif
    }

    /* export stereo mode flag as metadata tag */
    if (track->video.stereo_mode && track->video.stereo_mode < MATROSKA_VIDEO_STEREOMODE_TYPE_NB)
        av_dict_set(&st->metadata, "stereo_mode", ff_matroska_video_stereo_mode[track->video.stereo_mode], 0);

    /* export alpha mode flag as metadata tag  */
    if (track->video.alpha_mode)
        av_dict_set_int(&st->metadata, "alpha_mode", 1, 0);

    /* if we have virtual track, mark the real tracks */
    planes = track->operation.combine_planes.elem;
    for (int j = 0; j < track->operation.combine_planes.nb_elem; j++) {
        MatroskaTrack *tracks = matroska->tracks.elem;
        char buf[32];
        if (planes[j].type >= MATROSKA_VIDEO_STEREO_PLANE_COUNT)
            continue;
        snprintf(buf, sizeof(buf), "%s_%d",
                 matroska_video_stereo_plane[planes[j].type], st->index);
        for (int k = 0; k < matroska->tracks.nb_elem; k++)
            if (planes[j].uid == tracks[k].uid && tracks[k].stream) {
                av_dict_set(&tracks[k].stream->metadata,
                            "stereo_mode", buf, 0);
                break;
            }
    }
    // add stream level stereo3d side data if it is a supported format
    if (track->video.stereo_mode < MATROSKA_VIDEO_STEREOMODE_TYPE_NB &&
        track->video.stereo_mode != MATROSKA_VIDEO_STEREOMODE_TYPE_ANAGLYPH_CYAN_RED &&
        track->video.stereo_mode != MATROSKA_VIDEO_STEREOMODE_TYPE_ANAGLYPH_GREEN_MAG) {
        int ret = mkv_stereo3d_conv(st, track->video.stereo_mode);
        if (ret < 0)
            return ret;
    }

    ret = mkv_parse_video_color(st, track);
    if (ret < 0)
        return ret;
    ret = mkv_parse_video_projection(st, track, matroska->ctx);
    if (ret < 0)
        return ret;

    return 0;
}

/* Performs the codec-specific part of parsing a subtitle track. */
static int mkv_parse_subtitle_codec(MatroskaTrack *track, AVStream *st,
                                    AVCodecParameters *par,
                                    const MatroskaDemuxContext *matroska)
{
    switch (par->codec_id) {
    case AV_CODEC_ID_ARIB_CAPTION:
        if (track->codec_priv.size == 3) {
            int component_tag = track->codec_priv.data[0];
            int data_component_id = AV_RB16(track->codec_priv.data + 1);

            switch (data_component_id) {
            case 0x0008:
                // [0x30..0x37] are component tags utilized for
                // non-mobile captioning service ("profile A").
                if (component_tag >= 0x30 && component_tag <= 0x37) {
                    par->profile = AV_PROFILE_ARIB_PROFILE_A;
                }
                break;
            case 0x0012:
                // component tag 0x87 signifies a mobile/partial reception
                // (1seg) captioning service ("profile C").
                if (component_tag == 0x87) {
                    par->profile = AV_PROFILE_ARIB_PROFILE_C;
                }
                break;
            default:
                break;
            }

            if (par->profile == AV_PROFILE_UNKNOWN)
                av_log(matroska->ctx, AV_LOG_WARNING,
                       "Unknown ARIB caption profile utilized: %02x / %04x\n",
                       component_tag, data_component_id);

            track->codec_priv.size = 0;
        }
        break;
    case AV_CODEC_ID_WEBVTT:
        if (!strcmp(track->codec_id, "D_WEBVTT/CAPTIONS")) {
            st->disposition |= AV_DISPOSITION_CAPTIONS;
        } else if (!strcmp(track->codec_id, "D_WEBVTT/DESCRIPTIONS")) {
            st->disposition |= AV_DISPOSITION_DESCRIPTIONS;
        } else if (!strcmp(track->codec_id, "D_WEBVTT/METADATA")) {
            st->disposition |= AV_DISPOSITION_METADATA;
        }
        break;
    }

    return 0;
}

static int matroska_parse_tracks(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTrack *tracks = matroska->tracks.elem;
    int i, j, ret;

    for (i = 0; i < matroska->tracks.nb_elem; i++) {
        MatroskaTrack *track = &tracks[i];
        enum AVCodecID codec_id = AV_CODEC_ID_NONE;
        AVCodecParameters *par;
        MatroskaTrackType type;
        int extradata_offset = 0;
        AVStream *st;
        char* key_id_base64 = NULL;

        /* Apply some sanity checks. */
        if (track->type != MATROSKA_TRACK_TYPE_VIDEO &&
            track->type != MATROSKA_TRACK_TYPE_AUDIO &&
            track->type != MATROSKA_TRACK_TYPE_SUBTITLE &&
            track->type != MATROSKA_TRACK_TYPE_METADATA) {
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Unknown or unsupported track type %"PRIu64"\n",
                   track->type);
            continue;
        }
        if (!track->codec_id)
            continue;

        if (   track->type == MATROSKA_TRACK_TYPE_AUDIO && track->codec_id[0] != 'A'
            || track->type == MATROSKA_TRACK_TYPE_VIDEO && track->codec_id[0] != 'V'
            || track->type == MATROSKA_TRACK_TYPE_SUBTITLE && track->codec_id[0] != 'D' && track->codec_id[0] != 'S'
            || track->type == MATROSKA_TRACK_TYPE_METADATA && track->codec_id[0] != 'D' && track->codec_id[0] != 'S'
        ) {
            av_log(matroska->ctx, AV_LOG_INFO, "Inconsistent track type\n");
            continue;
        }

        if (track->audio.samplerate < 0 || track->audio.samplerate > INT_MAX ||
            isnan(track->audio.samplerate)) {
            av_log(matroska->ctx, AV_LOG_WARNING,
                   "Invalid sample rate %f, defaulting to 8000 instead.\n",
                   track->audio.samplerate);
            track->audio.samplerate = 8000;
        }

        if (track->type == MATROSKA_TRACK_TYPE_VIDEO) {
            if (!track->default_duration && track->video.frame_rate > 0) {
                double default_duration = 1000000000 / track->video.frame_rate;
                if (default_duration > UINT64_MAX || default_duration < 0) {
                    av_log(matroska->ctx, AV_LOG_WARNING,
                         "Invalid frame rate %e. Cannot calculate default duration.\n",
                         track->video.frame_rate);
                } else {
                    track->default_duration = default_duration;
                }
            }
            if (track->video.pixel_cropl >= INT_MAX - track->video.pixel_cropr ||
                track->video.pixel_cropt >= INT_MAX - track->video.pixel_cropb ||
                (track->video.pixel_cropl + track->video.pixel_cropr) >= track->video.pixel_width ||
                (track->video.pixel_cropt + track->video.pixel_cropb) >= track->video.pixel_height)
                return AVERROR_INVALIDDATA;
            track->video.cropped_width  = track->video.pixel_width  -
                                          track->video.pixel_cropl  - track->video.pixel_cropr;
            track->video.cropped_height = track->video.pixel_height -
                                          track->video.pixel_cropt  - track->video.pixel_cropb;
            if (track->video.display_unit == MATROSKA_VIDEO_DISPLAYUNIT_PIXELS) {
                if (track->video.display_width == -1)
                    track->video.display_width = track->video.cropped_width;
                if (track->video.display_height == -1)
                    track->video.display_height = track->video.cropped_height;
            }
        } else if (track->type == MATROSKA_TRACK_TYPE_AUDIO) {
            if (!track->audio.out_samplerate)
                track->audio.out_samplerate = track->audio.samplerate;
        }
        ret = matroska_parse_content_encodings(track->encodings.elem,
                                               track->encodings.nb_elem,
                                               track, &key_id_base64, matroska->ctx);
        if (ret < 0)
            return ret;

        for (j = 0; ff_mkv_codec_tags[j].id != AV_CODEC_ID_NONE; j++) {
            if (av_strstart(track->codec_id, ff_mkv_codec_tags[j].str, NULL)) {
                codec_id = ff_mkv_codec_tags[j].id;
                break;
            }
        }

        st = track->stream = avformat_new_stream(s, NULL);
        if (!st) {
            av_free(key_id_base64);
            return AVERROR(ENOMEM);
        }
        par = st->codecpar;

        par->codec_id  = codec_id;

        if (track->flag_default)
            st->disposition |= AV_DISPOSITION_DEFAULT;
        if (track->flag_forced)
            st->disposition |= AV_DISPOSITION_FORCED;
        if (track->flag_comment)
            st->disposition |= AV_DISPOSITION_COMMENT;
        if (track->flag_hearingimpaired)
            st->disposition |= AV_DISPOSITION_HEARING_IMPAIRED;
        if (track->flag_visualimpaired)
            st->disposition |= AV_DISPOSITION_VISUAL_IMPAIRED;
        if (track->flag_original.count > 0)
            st->disposition |= track->flag_original.el.u ? AV_DISPOSITION_ORIGINAL
                                                         : AV_DISPOSITION_DUB;

        if (key_id_base64) {
            /* export encryption key id as base64 metadata tag */
            av_dict_set(&st->metadata, "enc_key_id", key_id_base64,
                        AV_DICT_DONT_STRDUP_VAL);
        }

        if (strcmp(track->language, "und"))
            av_dict_set(&st->metadata, "language", track->language, 0);
        av_dict_set(&st->metadata, "title", track->name, 0);

        if (track->time_scale < 0.01) {
            av_log(matroska->ctx, AV_LOG_WARNING,
                   "Track TimestampScale too small %f, assuming 1.0.\n",
                   track->time_scale);
            track->time_scale = 1.0;
        }

        if (matroska->time_scale * track->time_scale > UINT_MAX)
            return AVERROR_INVALIDDATA;

        avpriv_set_pts_info(st, 64, matroska->time_scale * track->time_scale,
                            1000 * 1000 * 1000);    /* 64 bit pts in ns */

        /* convert the delay from ns to the track timebase */
        track->codec_delay_in_track_tb = av_rescale_q(track->codec_delay,
                                                      (AVRational){ 1, 1000000000 },
                                                      st->time_base);

        type = track->type;
        if (par->codec_id == AV_CODEC_ID_WEBVTT)
            type = MATROSKA_TRACK_TYPE_SUBTITLE;
        switch (type) {
        case MATROSKA_TRACK_TYPE_AUDIO:
            ret = mka_parse_audio(track, st, par, matroska,
                                  s, &extradata_offset);
            if (ret < 0)
                return ret;
            if (ret == SKIP_TRACK)
                continue;
            break;
        case MATROSKA_TRACK_TYPE_VIDEO:
            ret = mkv_parse_video(track, st, par, matroska, &extradata_offset);
            if (ret < 0)
                return ret;
            break;
        case MATROSKA_TRACK_TYPE_SUBTITLE:
            ret = mkv_parse_subtitle_codec(track, st, par, matroska);
            if (ret < 0)
                return ret;
            par->codec_type = AVMEDIA_TYPE_SUBTITLE;

            if (track->flag_textdescriptions)
                st->disposition |= AV_DISPOSITION_DESCRIPTIONS;
            break;
        }

        if (par->codec_id == AV_CODEC_ID_NONE)
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Unknown/unsupported AVCodecID %s.\n", track->codec_id);

        if (!par->extradata && track->codec_priv.size > extradata_offset) {
            const uint8_t *src = track->codec_priv.data + extradata_offset;
            unsigned extra_size = track->codec_priv.size - extradata_offset;
            ret = ff_alloc_extradata(par, extra_size);
            if (ret < 0)
                return ret;
            memcpy(par->extradata, src, extra_size);
        }

        ret = mkv_parse_block_addition_mappings(s, st, track);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int matroska_read_header(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    MatroskaDemuxContext *matroska = s->priv_data;
    EbmlList *attachments_list = &matroska->attachments;
    EbmlList *chapters_list    = &matroska->chapters;
    MatroskaAttachment *attachments;
    MatroskaChapter *chapters;
    uint64_t max_start = 0;
    int64_t pos;
    Ebml ebml = { 0 };
    int i, j, res;

    matroska->ctx = s;
    matroska->cues_parsing_deferred = 1;

    /* First read the EBML header. */
    if (ebml_parse(matroska, ebml_syntax, &ebml) || !ebml.doctype) {
        av_log(matroska->ctx, AV_LOG_ERROR, "EBML header parsing failed\n");
        ebml_free(ebml_syntax, &ebml);
        return AVERROR_INVALIDDATA;
    }
    if (ebml.version         > EBML_VERSION      ||
        ebml.max_size        > sizeof(uint64_t)  ||
        ebml.id_length       > sizeof(uint32_t)  ||
        ebml.doctype_version > 3) {
        avpriv_report_missing_feature(matroska->ctx,
                                      "EBML version %"PRIu64", doctype %s, doc version %"PRIu64,
                                      ebml.version, ebml.doctype, ebml.doctype_version);
        ebml_free(ebml_syntax, &ebml);
        return AVERROR_PATCHWELCOME;
    } else if (ebml.doctype_version == 3) {
        av_log(matroska->ctx, AV_LOG_WARNING,
               "EBML header using unsupported features\n"
               "(EBML version %"PRIu64", doctype %s, doc version %"PRIu64")\n",
               ebml.version, ebml.doctype, ebml.doctype_version);
    }
    for (i = 0; i < FF_ARRAY_ELEMS(matroska_doctypes); i++)
        if (!strcmp(ebml.doctype, matroska_doctypes[i]))
            break;
    if (i >= FF_ARRAY_ELEMS(matroska_doctypes)) {
        av_log(s, AV_LOG_WARNING, "Unknown EBML doctype '%s'\n", ebml.doctype);
        if (matroska->ctx->error_recognition & AV_EF_EXPLODE) {
            ebml_free(ebml_syntax, &ebml);
            return AVERROR_INVALIDDATA;
        }
    }
    matroska->is_webm = !strcmp(ebml.doctype, "webm");

    ebml_free(ebml_syntax, &ebml);

    matroska->pkt = si->parse_pkt;

    /* The next thing is a segment. */
    pos = avio_tell(matroska->ctx->pb);
    res = ebml_parse(matroska, matroska_segments, matroska);
    // Try resyncing until we find an EBML_STOP type element.
    while (res != 1) {
        res = matroska_resync(matroska, pos);
        if (res < 0)
            return res;
        pos = avio_tell(matroska->ctx->pb);
        res = ebml_parse(matroska, matroska_segment, matroska);
        if (res == AVERROR(EIO)) // EOF is translated to EIO, this exists the loop on EOF
            return res;
    }
    /* Set data_offset as it might be needed later by seek_frame_generic. */
    if (matroska->current_id == MATROSKA_ID_CLUSTER)
        si->data_offset = avio_tell(matroska->ctx->pb) - 4;
    matroska_execute_seekhead(matroska);

    if (!matroska->time_scale)
        matroska->time_scale = 1000000;
    if (isnan(matroska->duration))
        matroska->duration = 0;
    if (matroska->duration)
        matroska->ctx->duration = matroska->duration * matroska->time_scale *
                                  1000 / AV_TIME_BASE;
    av_dict_set(&s->metadata, "title", matroska->title, 0);
    av_dict_set(&s->metadata, "encoder", matroska->muxingapp, 0);

    if (matroska->date_utc.size == 8)
        matroska_metadata_creation_time(&s->metadata, AV_RB64(matroska->date_utc.data));

    res = matroska_parse_tracks(s);
    if (res < 0)
        return res;

    attachments = attachments_list->elem;
    for (j = 0; j < attachments_list->nb_elem; j++) {
        if (!(attachments[j].filename && attachments[j].mime &&
              attachments[j].bin.data && attachments[j].bin.size > 0)) {
            av_log(matroska->ctx, AV_LOG_ERROR, "incomplete attachment\n");
        } else {
            AVStream *st = avformat_new_stream(s, NULL);
            if (!st)
                break;
            av_dict_set(&st->metadata, "filename", attachments[j].filename, 0);
            av_dict_set(&st->metadata, "mimetype", attachments[j].mime, 0);
            if (attachments[j].description)
                av_dict_set(&st->metadata, "title", attachments[j].description, 0);
            st->codecpar->codec_id   = AV_CODEC_ID_NONE;

            for (i = 0; mkv_image_mime_tags[i].id != AV_CODEC_ID_NONE; i++) {
                if (av_strstart(attachments[j].mime, mkv_image_mime_tags[i].str, NULL)) {
                    st->codecpar->codec_id = mkv_image_mime_tags[i].id;
                    break;
                }
            }

            attachments[j].stream = st;

            if (st->codecpar->codec_id != AV_CODEC_ID_NONE) {
                res = ff_add_attached_pic(s, st, NULL, &attachments[j].bin.buf, 0);
                if (res < 0)
                    return res;
            } else {
                st->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
                if (ff_alloc_extradata(st->codecpar, attachments[j].bin.size))
                    break;
                memcpy(st->codecpar->extradata, attachments[j].bin.data,
                       attachments[j].bin.size);

                for (i = 0; mkv_mime_tags[i].id != AV_CODEC_ID_NONE; i++) {
                    if (av_strstart(attachments[j].mime, mkv_mime_tags[i].str, NULL)) {
                        st->codecpar->codec_id = mkv_mime_tags[i].id;
                        break;
                    }
                }
            }
        }
    }

    chapters = chapters_list->elem;
    for (i = 0; i < chapters_list->nb_elem; i++)
        if (chapters[i].start != AV_NOPTS_VALUE && chapters[i].uid &&
            (max_start == 0 || chapters[i].start > max_start)) {
            chapters[i].chapter =
                avpriv_new_chapter(s, chapters[i].uid,
                                   (AVRational) { 1, 1000000000 },
                                   chapters[i].start, chapters[i].end,
                                   chapters[i].title);
            max_start = chapters[i].start;
        }

    matroska_add_index_entries(matroska);

    matroska_convert_tags(s);

    return 0;
}

/*
 * Put one packet in an application-supplied AVPacket struct.
 * Returns 0 on success or -1 on failure.
 */
static int matroska_deliver_packet(MatroskaDemuxContext *matroska,
                                   AVPacket *pkt)
{
    if (matroska->queue.head) {
        MatroskaTrack *tracks = matroska->tracks.elem;
        MatroskaTrack *track;

        avpriv_packet_list_get(&matroska->queue, pkt);
        track = &tracks[pkt->stream_index];
        if (track->has_palette) {
            uint8_t *pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
            if (!pal) {
                av_log(matroska->ctx, AV_LOG_ERROR, "Cannot append palette to packet\n");
            } else {
                memcpy(pal, track->palette, AVPALETTE_SIZE);
            }
            track->has_palette = 0;
        }
        return 0;
    }

    return -1;
}

/*
 * Free all packets in our internal queue.
 */
static void matroska_clear_queue(MatroskaDemuxContext *matroska)
{
    avpriv_packet_list_free(&matroska->queue);
}

static int matroska_parse_laces(MatroskaDemuxContext *matroska, uint8_t **buf,
                                int size, int type, AVIOContext *pb,
                                uint32_t lace_size[256], int *laces)
{
    int n;
    uint8_t *data = *buf;

    if (!type) {
        *laces    = 1;
        lace_size[0] = size;
        return 0;
    }

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    *laces = *data + 1;
    data  += 1;
    size  -= 1;

    switch (type) {
    case 0x1: /* Xiph lacing */
    {
        uint8_t temp;
        uint32_t total = 0;
        for (n = 0; n < *laces - 1; n++) {
            lace_size[n] = 0;

            do {
                if (size <= total)
                    return AVERROR_INVALIDDATA;
                temp          = *data;
                total        += temp;
                lace_size[n] += temp;
                data         += 1;
                size         -= 1;
            } while (temp ==  0xff);
        }
        if (size < total)
            return AVERROR_INVALIDDATA;

        lace_size[n] = size - total;
        break;
    }

    case 0x2: /* fixed-size lacing */
        if (size % (*laces))
            return AVERROR_INVALIDDATA;
        for (n = 0; n < *laces; n++)
            lace_size[n] = size / *laces;
        break;

    case 0x3: /* EBML lacing */
    {
        uint64_t num;
        uint64_t total;
        int offset;

        avio_skip(pb, 4);

        n = ebml_read_num(matroska, pb, 8, &num, 1);
        if (n < 0)
            return n;
        if (num > INT_MAX)
            return AVERROR_INVALIDDATA;

        total = lace_size[0] = num;
        offset = n;
        for (n = 1; n < *laces - 1; n++) {
            int64_t snum;
            int r;
            r = matroska_ebmlnum_sint(matroska, pb, &snum);
            if (r < 0)
                return r;
            if (lace_size[n - 1] + snum > (uint64_t)INT_MAX)
                return AVERROR_INVALIDDATA;

            lace_size[n] = lace_size[n - 1] + snum;
            total       += lace_size[n];
            offset      += r;
        }
        data += offset;
        size -= offset;
        if (size < total)
            return AVERROR_INVALIDDATA;

        lace_size[*laces - 1] = size - total;
        break;
    }
    }

    *buf = data;

    return 0;
}

static int matroska_parse_rm_audio(MatroskaDemuxContext *matroska,
                                   MatroskaTrack *track, AVStream *st,
                                   uint8_t *data, int size, uint64_t timecode,
                                   int64_t pos)
{
    const int a   = st->codecpar->block_align;
    const int sps = track->audio.sub_packet_size;
    const int cfs = track->audio.coded_framesize;
    const int h   = track->audio.sub_packet_h;
    const int w   = track->audio.frame_size;
    int y   = track->audio.sub_packet_cnt;
    int x;

    if (!track->audio.pkt_cnt) {
        if (track->audio.sub_packet_cnt == 0)
            track->audio.buf_timecode = timecode;
        if (st->codecpar->codec_id == AV_CODEC_ID_RA_288) {
            if (size < cfs * h / 2) {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Corrupt int4 RM-style audio packet size\n");
                return AVERROR_INVALIDDATA;
            }
            for (x = 0; x < h / 2; x++)
                memcpy(track->audio.buf + x * 2 * w + y * cfs,
                       data + x * cfs, cfs);
        } else if (st->codecpar->codec_id == AV_CODEC_ID_SIPR) {
            if (size < w) {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Corrupt sipr RM-style audio packet size\n");
                return AVERROR_INVALIDDATA;
            }
            memcpy(track->audio.buf + y * w, data, w);
        } else {
            if (size < w) {
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Corrupt generic RM-style audio packet size\n");
                return AVERROR_INVALIDDATA;
            }
            for (x = 0; x < w / sps; x++)
                memcpy(track->audio.buf +
                       sps * (h * x + ((h + 1) / 2) * (y & 1) + (y >> 1)),
                       data + x * sps, sps);
        }

        if (++track->audio.sub_packet_cnt >= h) {
            if (st->codecpar->codec_id == AV_CODEC_ID_SIPR)
                ff_rm_reorder_sipr_data(track->audio.buf, h, w);
            track->audio.sub_packet_cnt = 0;
            track->audio.pkt_cnt        = h * w / a;
        }
    }

    while (track->audio.pkt_cnt) {
        int ret;
        AVPacket *pkt = matroska->pkt;

        ret = av_new_packet(pkt, a);
        if (ret < 0) {
            return ret;
        }
        memcpy(pkt->data,
               track->audio.buf + a * (h * w / a - track->audio.pkt_cnt--),
               a);
        pkt->pts                  = track->audio.buf_timecode;
        track->audio.buf_timecode = AV_NOPTS_VALUE;
        pkt->pos                  = pos;
        pkt->stream_index         = st->index;
        ret = avpriv_packet_list_put(&matroska->queue, pkt, NULL, 0);
        if (ret < 0) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

/* reconstruct full wavpack blocks from mangled matroska ones */
static int matroska_parse_wavpack(MatroskaTrack *track,
                                  uint8_t **data, int *size)
{
    uint8_t *dst = NULL;
    uint8_t *src = *data;
    int dstlen   = 0;
    int srclen   = *size;
    uint32_t samples;
    uint16_t ver;
    int ret, offset = 0;

    if (srclen < 12)
        return AVERROR_INVALIDDATA;

    av_assert1(track->stream->codecpar->extradata_size >= 2);
    ver = AV_RL16(track->stream->codecpar->extradata);

    samples = AV_RL32(src);
    src    += 4;
    srclen -= 4;

    while (srclen >= 8) {
        int multiblock;
        uint32_t blocksize;
        uint8_t *tmp;

        uint32_t flags = AV_RL32(src);
        uint32_t crc   = AV_RL32(src + 4);
        src    += 8;
        srclen -= 8;

        multiblock = (flags & 0x1800) != 0x1800;
        if (multiblock) {
            if (srclen < 4) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            blocksize = AV_RL32(src);
            src      += 4;
            srclen   -= 4;
        } else
            blocksize = srclen;

        if (blocksize > srclen) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        tmp = av_realloc(dst, dstlen + blocksize + 32 + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!tmp) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst     = tmp;
        dstlen += blocksize + 32;

        AV_WL32(dst + offset, MKTAG('w', 'v', 'p', 'k'));   // tag
        AV_WL32(dst + offset +  4, blocksize + 24);         // blocksize - 8
        AV_WL16(dst + offset +  8, ver);                    // version
        AV_WL16(dst + offset + 10, 0);                      // track/index_no
        AV_WL32(dst + offset + 12, 0);                      // total samples
        AV_WL32(dst + offset + 16, 0);                      // block index
        AV_WL32(dst + offset + 20, samples);                // number of samples
        AV_WL32(dst + offset + 24, flags);                  // flags
        AV_WL32(dst + offset + 28, crc);                    // crc
        memcpy(dst + offset + 32, src, blocksize);          // block data

        src    += blocksize;
        srclen -= blocksize;
        offset += blocksize + 32;
    }

    memset(dst + dstlen, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *data = dst;
    *size = dstlen;

    return 0;

fail:
    av_freep(&dst);
    return ret;
}

static int matroska_parse_prores(MatroskaTrack *track,
                                 uint8_t **data, int *size)
{
    uint8_t *dst;
    int dstlen = *size + 8;

    dst = av_malloc(dstlen + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!dst)
        return AVERROR(ENOMEM);

    AV_WB32(dst, dstlen);
    AV_WB32(dst + 4, MKBETAG('i', 'c', 'p', 'f'));
    memcpy(dst + 8, *data, dstlen - 8);
    memset(dst + dstlen, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *data = dst;
    *size = dstlen;

    return 0;
}

static int matroska_parse_webvtt(MatroskaDemuxContext *matroska,
                                 MatroskaTrack *track,
                                 AVStream *st,
                                 uint8_t *data, int data_len,
                                 uint64_t timecode,
                                 uint64_t duration,
                                 int64_t pos)
{
    AVPacket *pkt = matroska->pkt;
    uint8_t *id, *settings, *text, *buf;
    int id_len, settings_len, text_len;
    uint8_t *p, *q;
    int err;

    if (data_len <= 0)
        return AVERROR_INVALIDDATA;

    p = data;
    q = data + data_len;

    id = p;
    id_len = -1;
    while (p < q) {
        if (*p == '\r' || *p == '\n') {
            id_len = p - id;
            if (*p == '\r')
                p++;
            break;
        }
        p++;
    }

    if (p >= q || *p != '\n')
        return AVERROR_INVALIDDATA;
    p++;

    settings = p;
    settings_len = -1;
    while (p < q) {
        if (*p == '\r' || *p == '\n') {
            settings_len = p - settings;
            if (*p == '\r')
                p++;
            break;
        }
        p++;
    }

    if (p >= q || *p != '\n')
        return AVERROR_INVALIDDATA;
    p++;

    text = p;
    text_len = q - p;
    while (text_len > 0) {
        const int len = text_len - 1;
        const uint8_t c = p[len];
        if (c != '\r' && c != '\n')
            break;
        text_len = len;
    }

    if (text_len <= 0)
        return AVERROR_INVALIDDATA;

    err = av_new_packet(pkt, text_len);
    if (err < 0) {
        return err;
    }

    memcpy(pkt->data, text, text_len);

    if (id_len > 0) {
        buf = av_packet_new_side_data(pkt,
                                      AV_PKT_DATA_WEBVTT_IDENTIFIER,
                                      id_len);
        if (!buf) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        memcpy(buf, id, id_len);
    }

    if (settings_len > 0) {
        buf = av_packet_new_side_data(pkt,
                                      AV_PKT_DATA_WEBVTT_SETTINGS,
                                      settings_len);
        if (!buf) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        memcpy(buf, settings, settings_len);
    }

    // Do we need this for subtitles?
    // pkt->flags = AV_PKT_FLAG_KEY;

    pkt->stream_index = st->index;
    pkt->pts = timecode;

    // Do we need this for subtitles?
    // pkt->dts = timecode;

    pkt->duration = duration;
    pkt->pos = pos;

    err = avpriv_packet_list_put(&matroska->queue, pkt, NULL, 0);
    if (err < 0) {
        av_packet_unref(pkt);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int matroska_parse_block_additional(MatroskaDemuxContext *matroska,
                                           MatroskaTrack *track, AVPacket *pkt,
                                           const uint8_t *data, int size, uint64_t id)
{
    const EbmlList *mappings_list = &track->block_addition_mappings;
    MatroskaBlockAdditionMapping *mappings = mappings_list->elem, *mapping = NULL;
    uint8_t *side_data;
    int res;

    if (!matroska->is_webm && track->max_block_additional_id && id > track->max_block_additional_id) {
        int strict = matroska->ctx->strict_std_compliance >= FF_COMPLIANCE_STRICT;
        av_log(matroska->ctx, strict ? AV_LOG_ERROR : AV_LOG_WARNING,
               "BlockAddID %"PRIu64" is higher than the reported MaxBlockAdditionID %"PRIu64" "
               "for Track with TrackNumber %"PRIu64"\n", id, track->max_block_additional_id,
               track->num);
        if (strict)
            return AVERROR_INVALIDDATA;
    }

    for (int i = 0; i < mappings_list->nb_elem; i++) {
        if (id != mappings[i].value)
            continue;
        mapping = &mappings[i];
        break;
    }

    if (id != 1 && !matroska->is_webm && !mapping) {
        av_log(matroska->ctx, AV_LOG_WARNING, "BlockAddID %"PRIu64" has no mapping. Skipping\n", id);
        return 0;
    }

    if (mapping && mapping->type)
        id = mapping->type;

    switch (id) {
    case MATROSKA_BLOCK_ADD_ID_TYPE_ITU_T_T35: {
        GetByteContext bc;
        int country_code, provider_code;
        int provider_oriented_code, application_identifier;
        size_t hdrplus_size;
        AVDynamicHDRPlus *hdrplus;

        if (size < 6)
            break; //ignore

        bytestream2_init(&bc, data, size);

        /* ITU-T T.35 metadata */
        country_code  = bytestream2_get_byteu(&bc);
        provider_code = bytestream2_get_be16u(&bc);

        if (country_code != ITU_T_T35_COUNTRY_CODE_US ||
            provider_code != ITU_T_T35_PROVIDER_CODE_SMTPE)
            break; // ignore

        provider_oriented_code = bytestream2_get_be16u(&bc);
        application_identifier = bytestream2_get_byteu(&bc);

        if (provider_oriented_code != 1 || application_identifier != 4)
            break; // ignore

        hdrplus = av_dynamic_hdr_plus_alloc(&hdrplus_size);
        if (!hdrplus)
            return AVERROR(ENOMEM);

        if ((res = av_dynamic_hdr_plus_from_t35(hdrplus, bc.buffer,
                                                bytestream2_get_bytes_left(&bc))) < 0 ||
            (res = av_packet_add_side_data(pkt, AV_PKT_DATA_DYNAMIC_HDR10_PLUS,
                                           (uint8_t *)hdrplus, hdrplus_size)) < 0) {
            av_free(hdrplus);
            return res;
        }

        return 0;
    }
    default:
        break;
    }

    side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,
                                        size + (size_t)8);
    if (!side_data)
        return AVERROR(ENOMEM);

    AV_WB64(side_data, id);
    memcpy(side_data + 8, data, size);

    return 0;
}

static int matroska_parse_frame(MatroskaDemuxContext *matroska,
                                MatroskaTrack *track, AVStream *st,
                                AVBufferRef *buf, uint8_t *data, int pkt_size,
                                uint64_t timecode, uint64_t lace_duration,
                                int64_t pos, int is_keyframe,
                                MatroskaBlockMore *blockmore, int nb_blockmore,
                                int64_t discard_padding)
{
    uint8_t *pkt_data = data;
    int res = 0;
    AVPacket *pkt = matroska->pkt;

    if (st->codecpar->codec_id == AV_CODEC_ID_WAVPACK) {
        res = matroska_parse_wavpack(track, &pkt_data, &pkt_size);
        if (res < 0) {
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Error parsing a wavpack block.\n");
            goto fail;
        }
        if (!buf)
            av_freep(&data);
        buf = NULL;
    }

    if (st->codecpar->codec_id == AV_CODEC_ID_PRORES &&
        AV_RB32(pkt_data + 4)  != MKBETAG('i', 'c', 'p', 'f')) {
        res = matroska_parse_prores(track, &pkt_data, &pkt_size);
        if (res < 0) {
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Error parsing a prores block.\n");
            goto fail;
        }
        if (!buf)
            av_freep(&data);
        buf = NULL;
    }

    if (!pkt_size && !nb_blockmore)
        goto no_output;

    if (!matroska->is_webm && nb_blockmore && !track->max_block_additional_id) {
        int strict = matroska->ctx->strict_std_compliance >= FF_COMPLIANCE_STRICT;
        av_log(matroska->ctx, strict ? AV_LOG_ERROR : AV_LOG_WARNING,
               "Unexpected BlockAdditions found in a Block from Track with TrackNumber %"PRIu64" "
               "where MaxBlockAdditionID is 0\n", track->num);
        if (strict) {
            res = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if (!buf)
        pkt->buf = av_buffer_create(pkt_data, pkt_size + AV_INPUT_BUFFER_PADDING_SIZE,
                                    NULL, NULL, 0);
    else
        pkt->buf = av_buffer_ref(buf);

    if (!pkt->buf) {
        res = AVERROR(ENOMEM);
        goto fail;
    }

    pkt->data         = pkt_data;
    pkt->size         = pkt_size;
    pkt->flags        = is_keyframe;
    pkt->stream_index = st->index;

    for (int i = 0; i < nb_blockmore; i++) {
        MatroskaBlockMore *more = &blockmore[i];

        if (!more->additional.size)
            continue;

        res = matroska_parse_block_additional(matroska, track, pkt, more->additional.data,
                                              more->additional.size, more->additional_id);
        if (res < 0) {
            av_packet_unref(pkt);
            return res;
        }
    }

    if (discard_padding) {
        uint8_t *side_data = av_packet_new_side_data(pkt,
                                                     AV_PKT_DATA_SKIP_SAMPLES,
                                                     10);
        if (!side_data) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        discard_padding = av_rescale_q(discard_padding,
                                            (AVRational){1, 1000000000},
                                            (AVRational){1, st->codecpar->sample_rate});
        if (discard_padding > 0) {
            AV_WL32A(side_data + 4, discard_padding);
        } else {
            AV_WL32A(side_data, -discard_padding);
        }
    }

    if (track->ms_compat)
        pkt->dts = timecode;
    else
        pkt->pts = timecode;
    pkt->pos = pos;
    pkt->duration = lace_duration;

    res = avpriv_packet_list_put(&matroska->queue, pkt, NULL, 0);
    if (res < 0) {
        av_packet_unref(pkt);
        return AVERROR(ENOMEM);
    }

    return 0;

no_output:
fail:
    if (!buf)
        av_free(pkt_data);
    return res;
}

static int matroska_parse_block(MatroskaDemuxContext *matroska, AVBufferRef *buf, uint8_t *data,
                                int size, int64_t pos, uint64_t cluster_time,
                                uint64_t block_duration, int is_keyframe,
                                MatroskaBlockMore *blockmore, int nb_blockmore,
                                int64_t cluster_pos, int64_t discard_padding)
{
    uint64_t timecode = AV_NOPTS_VALUE;
    MatroskaTrack *track;
    FFIOContext pb;
    int res = 0;
    AVStream *st;
    int16_t block_time;
    uint32_t lace_size[256];
    int n, flags, laces = 0;
    uint64_t num;
    int trust_default_duration;

    av_assert1(buf);

    ffio_init_read_context(&pb, data, size);

    if ((n = ebml_read_num(matroska, &pb.pub, 8, &num, 1)) < 0)
        return n;
    data += n;
    size -= n;

    track = matroska_find_track_by_num(matroska, num);
    if (!track || size < 3)
        return AVERROR_INVALIDDATA;

    if (!(st = track->stream)) {
        av_log(matroska->ctx, AV_LOG_VERBOSE,
               "No stream associated to TrackNumber %"PRIu64". "
               "Ignoring Block with this TrackNumber.\n", num);
        return 0;
    }

    if (st->discard >= AVDISCARD_ALL)
        return res;
    if (block_duration > INT64_MAX)
        block_duration = INT64_MAX;

    block_time = sign_extend(AV_RB16(data), 16);
    data      += 2;
    flags      = *data++;
    size      -= 3;
    if (is_keyframe == -1)
        is_keyframe = flags & 0x80 ? AV_PKT_FLAG_KEY : 0;

    if (cluster_time != (uint64_t) -1 &&
        (block_time >= 0 || cluster_time >= -block_time)) {
        uint64_t timecode_cluster_in_track_tb = (double) cluster_time / track->time_scale;
        timecode = timecode_cluster_in_track_tb + block_time - track->codec_delay_in_track_tb;
        if (track->type == MATROSKA_TRACK_TYPE_SUBTITLE &&
            timecode < track->end_timecode)
            is_keyframe = 0;  /* overlapping subtitles are not key frame */
        if (is_keyframe) {
            ff_reduce_index(matroska->ctx, st->index);
            av_add_index_entry(st, cluster_pos, timecode, 0, 0,
                               AVINDEX_KEYFRAME);
        }
    }

    if (matroska->skip_to_keyframe &&
        track->type != MATROSKA_TRACK_TYPE_SUBTITLE) {
        // Compare signed timecodes. Timecode may be negative due to codec delay
        // offset. We don't support timestamps greater than int64_t anyway - see
        // AVPacket's pts.
        if ((int64_t)timecode < (int64_t)matroska->skip_to_timecode)
            return res;
        if (is_keyframe)
            matroska->skip_to_keyframe = 0;
        else if (!ffstream(st)->skip_to_keyframe) {
            av_log(matroska->ctx, AV_LOG_ERROR, "File is broken, keyframes not correctly marked!\n");
            matroska->skip_to_keyframe = 0;
        }
    }

    res = matroska_parse_laces(matroska, &data, size, (flags & 0x06) >> 1,
                               &pb.pub, lace_size, &laces);
    if (res < 0) {
        av_log(matroska->ctx, AV_LOG_ERROR, "Error parsing frame sizes.\n");
        return res;
    }

    trust_default_duration = track->default_duration != 0;
    if (track->audio.samplerate == 8000 && trust_default_duration) {
        // If this is needed for more codecs, then add them here
        if (st->codecpar->codec_id == AV_CODEC_ID_AC3) {
            if (track->audio.samplerate != st->codecpar->sample_rate || !st->codecpar->frame_size)
                trust_default_duration = 0;
        }
    }

    if (!block_duration && trust_default_duration)
        block_duration = track->default_duration * laces / matroska->time_scale;

    if (cluster_time != (uint64_t)-1 && (block_time >= 0 || cluster_time >= -block_time))
        track->end_timecode =
            FFMAX(track->end_timecode, timecode + block_duration);

    for (n = 0; n < laces; n++) {
        int64_t lace_duration = block_duration*(n+1) / laces - block_duration*n / laces;
        uint8_t *out_data = data;
        int      out_size = lace_size[n];

        if (track->needs_decoding) {
            res = matroska_decode_buffer(&out_data, &out_size, track);
            if (res < 0)
                return res;
            /* Given that we are here means that out_data is no longer
             * owned by buf, so set it to NULL. This depends upon
             * zero-length header removal compression being ignored. */
            av_assert1(out_data != data);
            buf = NULL;
        }

        if (track->audio.buf) {
            res = matroska_parse_rm_audio(matroska, track, st,
                                          out_data, out_size,
                                          timecode, pos);
            if (!buf)
                av_free(out_data);
            if (res)
                return res;
        } else if (st->codecpar->codec_id == AV_CODEC_ID_WEBVTT) {
            res = matroska_parse_webvtt(matroska, track, st,
                                        out_data, out_size,
                                        timecode, lace_duration,
                                        pos);
            if (!buf)
                av_free(out_data);
            if (res)
                return res;
        } else {
            res = matroska_parse_frame(matroska, track, st, buf, out_data,
                                       out_size, timecode, lace_duration,
                                       pos, is_keyframe,
                                       blockmore, nb_blockmore,
                                       discard_padding);
            if (res)
                return res;
        }

        if (timecode != AV_NOPTS_VALUE)
            timecode = lace_duration ? timecode + lace_duration : AV_NOPTS_VALUE;
        data += lace_size[n];
    }

    return 0;
}

static int matroska_parse_cluster(MatroskaDemuxContext *matroska)
{
    MatroskaCluster *cluster = &matroska->current_cluster;
    MatroskaBlock     *block = &cluster->block;
    int res;

    av_assert0(matroska->num_levels <= 2U);

    if (matroska->num_levels == 1) {
        res = ebml_parse(matroska, matroska_segment, NULL);

        if (res == 1) {
            /* Found a cluster: subtract the size of the ID already read. */
            cluster->pos = avio_tell(matroska->ctx->pb) - 4;

            res = ebml_parse(matroska, matroska_cluster_enter, cluster);
            if (res < 0)
                return res;
        }
    }

    if (matroska->num_levels == 2) {
        /* We are inside a cluster. */
        res = ebml_parse(matroska, matroska_cluster_parsing, cluster);

        if (res >= 0 && block->bin.size > 0) {
            int is_keyframe = block->non_simple ? block->reference.count == 0 : -1;

            res = matroska_parse_block(matroska, block->bin.buf, block->bin.data,
                                       block->bin.size, block->bin.pos,
                                       cluster->timecode, block->duration,
                                       is_keyframe, block->blockmore.elem,
                                       block->blockmore.nb_elem, cluster->pos,
                                       block->discard_padding);
        }

        ebml_free(matroska_blockgroup, block);
        memset(block, 0, sizeof(*block));
    } else if (!matroska->num_levels) {
        if (!avio_feof(matroska->ctx->pb)) {
            avio_r8(matroska->ctx->pb);
            if (!avio_feof(matroska->ctx->pb)) {
                av_log(matroska->ctx, AV_LOG_WARNING, "File extends beyond "
                       "end of segment.\n");
                return AVERROR_INVALIDDATA;
            }
        }
        matroska->done = 1;
        return AVERROR_EOF;
    }

    return res;
}

static int matroska_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    int ret = 0;

    if (matroska->resync_pos == -1) {
        // This can only happen if generic seeking has been used.
        matroska->resync_pos = avio_tell(s->pb);
    }

    while (matroska_deliver_packet(matroska, pkt)) {
        if (matroska->done)
            return (ret < 0) ? ret : AVERROR_EOF;
        if (matroska_parse_cluster(matroska) < 0 && !matroska->done)
            ret = matroska_resync(matroska, matroska->resync_pos);
    }

    return 0;
}

static int matroska_read_seek(AVFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTrack *tracks = NULL;
    AVStream *st = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    int i, index;

    /* Parse the CUES now since we need the index data to seek. */
    if (matroska->cues_parsing_deferred > 0) {
        matroska->cues_parsing_deferred = 0;
        matroska_parse_cues(matroska);
    }

    if (!sti->nb_index_entries)
        goto err;
    timestamp = FFMAX(timestamp, sti->index_entries[0].timestamp);

    if ((index = av_index_search_timestamp(st, timestamp, flags)) < 0 ||
         index == sti->nb_index_entries - 1) {
        matroska_reset_status(matroska, 0, sti->index_entries[sti->nb_index_entries - 1].pos);
        while ((index = av_index_search_timestamp(st, timestamp, flags)) < 0 ||
               index == sti->nb_index_entries - 1) {
            matroska_clear_queue(matroska);
            if (matroska_parse_cluster(matroska) < 0)
                break;
        }
    }

    matroska_clear_queue(matroska);
    if (index < 0 || (matroska->cues_parsing_deferred < 0 &&
                      index == sti->nb_index_entries - 1))
        goto err;

    tracks = matroska->tracks.elem;
    for (i = 0; i < matroska->tracks.nb_elem; i++) {
        tracks[i].audio.pkt_cnt        = 0;
        tracks[i].audio.sub_packet_cnt = 0;
        tracks[i].audio.buf_timecode   = AV_NOPTS_VALUE;
        tracks[i].end_timecode         = 0;
    }

    /* We seek to a level 1 element, so set the appropriate status. */
    matroska_reset_status(matroska, 0, sti->index_entries[index].pos);
    if (flags & AVSEEK_FLAG_ANY) {
        sti->skip_to_keyframe = 0;
        matroska->skip_to_timecode = timestamp;
    } else {
        sti->skip_to_keyframe = 1;
        matroska->skip_to_timecode = sti->index_entries[index].timestamp;
    }
    matroska->skip_to_keyframe = 1;
    matroska->done             = 0;
    avpriv_update_cur_dts(s, st, sti->index_entries[index].timestamp);
    return 0;
err:
    // slightly hackish but allows proper fallback to
    // the generic seeking code.
    matroska_reset_status(matroska, 0, -1);
    matroska->resync_pos = -1;
    matroska_clear_queue(matroska);
    sti->skip_to_keyframe =
    matroska->skip_to_keyframe = 0;
    matroska->done = 0;
    return -1;
}

static int matroska_read_close(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTrack *tracks = matroska->tracks.elem;
    int n;

    matroska_clear_queue(matroska);

    for (n = 0; n < matroska->tracks.nb_elem; n++)
        if (tracks[n].type == MATROSKA_TRACK_TYPE_AUDIO)
            av_freep(&tracks[n].audio.buf);
    ebml_free(matroska_segment, matroska);

    return 0;
}

#if CONFIG_WEBM_DASH_MANIFEST_DEMUXER
typedef struct {
    int64_t start_time_ns;
    int64_t end_time_ns;
    int64_t start_offset;
    int64_t end_offset;
} CueDesc;

/* This function searches all the Cues and returns the CueDesc corresponding to
 * the timestamp ts. Returned CueDesc will be such that start_time_ns <= ts <
 * end_time_ns. All 4 fields will be set to -1 if ts >= file's duration or
 * if an error occurred.
 */
static CueDesc get_cue_desc(AVFormatContext *s, int64_t ts, int64_t cues_start) {
    MatroskaDemuxContext *matroska = s->priv_data;
    FFStream *const sti = ffstream(s->streams[0]);
    AVIndexEntry *const index_entries = sti->index_entries;
    int nb_index_entries = sti->nb_index_entries;
    CueDesc cue_desc;
    int i;

    if (ts >= (int64_t)(matroska->duration * matroska->time_scale))
        return (CueDesc) {-1, -1, -1, -1};
    for (i = 1; i < nb_index_entries; i++) {
        if (index_entries[i - 1].timestamp * matroska->time_scale <= ts &&
            index_entries[i].timestamp * matroska->time_scale > ts) {
            break;
        }
    }
    --i;
    if (index_entries[i].timestamp > matroska->duration)
        return (CueDesc) {-1, -1, -1, -1};
    cue_desc.start_time_ns = index_entries[i].timestamp * matroska->time_scale;
    cue_desc.start_offset = index_entries[i].pos - matroska->segment_start;
    if (i != nb_index_entries - 1) {
        cue_desc.end_time_ns = index_entries[i + 1].timestamp * matroska->time_scale;
        cue_desc.end_offset = index_entries[i + 1].pos - matroska->segment_start;
    } else {
        cue_desc.end_time_ns = matroska->duration * matroska->time_scale;
        // FIXME: this needs special handling for files where Cues appear
        // before Clusters. the current logic assumes Cues appear after
        // Clusters.
        cue_desc.end_offset = cues_start - matroska->segment_start;
    }
    return cue_desc;
}

static int webm_clusters_start_with_keyframe(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    AVStream *const st  = s->streams[0];
    FFStream *const sti = ffstream(st);
    uint32_t id = matroska->current_id;
    int64_t cluster_pos, before_pos;
    int index, rv = 1;

    if (sti->nb_index_entries <= 0)
        return 0;

    // seek to the first cluster using cues.
    index = av_index_search_timestamp(st, 0, 0);
    if (index < 0)
        return 0;
    cluster_pos = sti->index_entries[index].pos;
    before_pos = avio_tell(s->pb);
    while (1) {
        uint64_t cluster_id, cluster_length;
        int read;
        AVPacket *pkt;
        avio_seek(s->pb, cluster_pos, SEEK_SET);
        // read cluster id and length
        read = ebml_read_num(matroska, matroska->ctx->pb, 4, &cluster_id, 1);
        if (read < 0 || cluster_id != 0xF43B675) // done with all clusters
            break;
        read = ebml_read_length(matroska, matroska->ctx->pb, &cluster_length);
        if (read < 0)
            break;

        matroska_reset_status(matroska, 0, cluster_pos);
        matroska_clear_queue(matroska);
        if (matroska_parse_cluster(matroska) < 0 ||
            !matroska->queue.head) {
            break;
        }
        pkt = &matroska->queue.head->pkt;
        // 4 + read is the length of the cluster id and the cluster length field.
        cluster_pos += 4 + read + cluster_length;
        if (!(pkt->flags & AV_PKT_FLAG_KEY)) {
            rv = 0;
            break;
        }
    }

    /* Restore the status after matroska_read_header: */
    matroska_reset_status(matroska, id, before_pos);

    return rv;
}

static int buffer_size_after_time_downloaded(int64_t time_ns, double search_sec, int64_t bps,
                                             double min_buffer, double* buffer,
                                             double* sec_to_download, AVFormatContext *s,
                                             int64_t cues_start)
{
    double nano_seconds_per_second = 1000000000.0;
    double time_sec = time_ns / nano_seconds_per_second;
    int rv = 0;
    int64_t time_to_search_ns = (int64_t)(search_sec * nano_seconds_per_second);
    int64_t end_time_ns = time_ns + time_to_search_ns;
    double sec_downloaded = 0.0;
    CueDesc desc_curr = get_cue_desc(s, time_ns, cues_start);
    if (desc_curr.start_time_ns == -1)
      return -1;
    *sec_to_download = 0.0;

    // Check for non cue start time.
    if (time_ns > desc_curr.start_time_ns) {
      int64_t cue_nano = desc_curr.end_time_ns - time_ns;
      double percent = (double)(cue_nano) / (desc_curr.end_time_ns - desc_curr.start_time_ns);
      double cueBytes = (desc_curr.end_offset - desc_curr.start_offset) * percent;
      double timeToDownload = (cueBytes * 8.0) / bps;

      sec_downloaded += (cue_nano / nano_seconds_per_second) - timeToDownload;
      *sec_to_download += timeToDownload;

      // Check if the search ends within the first cue.
      if (desc_curr.end_time_ns >= end_time_ns) {
          double desc_end_time_sec = desc_curr.end_time_ns / nano_seconds_per_second;
          double percent_to_sub = search_sec / (desc_end_time_sec - time_sec);
          sec_downloaded = percent_to_sub * sec_downloaded;
          *sec_to_download = percent_to_sub * *sec_to_download;
      }

      if ((sec_downloaded + *buffer) <= min_buffer) {
          return 1;
      }

      // Get the next Cue.
      desc_curr = get_cue_desc(s, desc_curr.end_time_ns, cues_start);
    }

    while (desc_curr.start_time_ns != -1) {
        int64_t desc_bytes = desc_curr.end_offset - desc_curr.start_offset;
        int64_t desc_ns = desc_curr.end_time_ns - desc_curr.start_time_ns;
        double desc_sec = desc_ns / nano_seconds_per_second;
        double bits = (desc_bytes * 8.0);
        double time_to_download = bits / bps;

        sec_downloaded += desc_sec - time_to_download;
        *sec_to_download += time_to_download;

        if (desc_curr.end_time_ns >= end_time_ns) {
            double desc_end_time_sec = desc_curr.end_time_ns / nano_seconds_per_second;
            double percent_to_sub = search_sec / (desc_end_time_sec - time_sec);
            sec_downloaded = percent_to_sub * sec_downloaded;
            *sec_to_download = percent_to_sub * *sec_to_download;

            if ((sec_downloaded + *buffer) <= min_buffer)
                rv = 1;
            break;
        }

        if ((sec_downloaded + *buffer) <= min_buffer) {
            rv = 1;
            break;
        }

        desc_curr = get_cue_desc(s, desc_curr.end_time_ns, cues_start);
    }
    *buffer = *buffer + sec_downloaded;
    return rv;
}

/* This function computes the bandwidth of the WebM file with the help of
 * buffer_size_after_time_downloaded() function. Both of these functions are
 * adapted from WebM Tools project and are adapted to work with FFmpeg's
 * Matroska parsing mechanism.
 *
 * Returns the bandwidth of the file on success; -1 on error.
 * */
static int64_t webm_dash_manifest_compute_bandwidth(AVFormatContext *s, int64_t cues_start)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    AVStream *st = s->streams[0];
    FFStream *const sti = ffstream(st);
    double bandwidth = 0.0;

    for (int i = 0; i < sti->nb_index_entries; i++) {
        int64_t prebuffer_ns = 1000000000;
        int64_t time_ns = sti->index_entries[i].timestamp * matroska->time_scale;
        double nano_seconds_per_second = 1000000000.0;
        int64_t prebuffered_ns;
        double prebuffer_bytes = 0.0;
        int64_t temp_prebuffer_ns = prebuffer_ns;
        int64_t pre_bytes, pre_ns;
        double pre_sec, prebuffer, bits_per_second;
        CueDesc desc_beg = get_cue_desc(s, time_ns, cues_start);
        // Start with the first Cue.
        CueDesc desc_end = desc_beg;

        if (time_ns > INT64_MAX - prebuffer_ns)
            return -1;
        prebuffered_ns = time_ns + prebuffer_ns;

        // Figure out how much data we have downloaded for the prebuffer. This will
        // be used later to adjust the bits per sample to try.
        while (desc_end.start_time_ns != -1 && desc_end.end_time_ns < prebuffered_ns) {
            // Prebuffered the entire Cue.
            prebuffer_bytes += desc_end.end_offset - desc_end.start_offset;
            temp_prebuffer_ns -= desc_end.end_time_ns - desc_end.start_time_ns;
            desc_end = get_cue_desc(s, desc_end.end_time_ns, cues_start);
        }
        if (desc_end.start_time_ns == -1) {
            // The prebuffer is larger than the duration.
            if (matroska->duration * matroska->time_scale >= prebuffered_ns)
              return -1;
            bits_per_second = 0.0;
        } else {
            // The prebuffer ends in the last Cue. Estimate how much data was
            // prebuffered.
            pre_bytes = desc_end.end_offset - desc_end.start_offset;
            if (desc_end.end_time_ns <= desc_end.start_time_ns ||
                desc_end.end_time_ns - (uint64_t)desc_end.start_time_ns > INT64_MAX)
                return -1;
            pre_ns = desc_end.end_time_ns - desc_end.start_time_ns;
            pre_sec = pre_ns / nano_seconds_per_second;
            prebuffer_bytes +=
                pre_bytes * ((temp_prebuffer_ns / nano_seconds_per_second) / pre_sec);

            prebuffer = prebuffer_ns / nano_seconds_per_second;

            // Set this to 0.0 in case our prebuffer buffers the entire video.
            bits_per_second = 0.0;
            do {
                int64_t desc_bytes = desc_end.end_offset - desc_beg.start_offset;
                int64_t desc_ns = desc_end.end_time_ns - desc_beg.start_time_ns;
                double desc_sec, calc_bits_per_second, percent, mod_bits_per_second;
                if (desc_bytes <= 0 || desc_bytes > INT64_MAX/8)
                    return -1;

                desc_sec = desc_ns / nano_seconds_per_second;
                calc_bits_per_second = (desc_bytes * 8) / desc_sec;

                // Drop the bps by the percentage of bytes buffered.
                percent = (desc_bytes - prebuffer_bytes) / desc_bytes;
                mod_bits_per_second = calc_bits_per_second * percent;

                if (prebuffer < desc_sec) {
                    double search_sec =
                        (double)(matroska->duration * matroska->time_scale) / nano_seconds_per_second;

                    // Add 1 so the bits per second should be a little bit greater than file
                    // datarate.
                    int64_t bps = (int64_t)(mod_bits_per_second) + 1;
                    const double min_buffer = 0.0;
                    double buffer = prebuffer;
                    double sec_to_download = 0.0;

                    int rv = buffer_size_after_time_downloaded(prebuffered_ns, search_sec, bps,
                                                               min_buffer, &buffer, &sec_to_download,
                                                               s, cues_start);
                    if (rv < 0) {
                        return -1;
                    } else if (rv == 0) {
                        bits_per_second = (double)(bps);
                        break;
                    }
                }

                desc_end = get_cue_desc(s, desc_end.end_time_ns, cues_start);
            } while (desc_end.start_time_ns != -1);
        }
        if (bandwidth < bits_per_second) bandwidth = bits_per_second;
    }
    return (int64_t)bandwidth;
}

static int webm_dash_manifest_cues(AVFormatContext *s, int64_t init_range)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    EbmlList *seekhead_list = &matroska->seekhead;
    MatroskaSeekhead *seekhead = seekhead_list->elem;
    AVStream *const st = s->streams[0];
    FFStream *const sti = ffstream(st);
    AVBPrint bprint;
    char *buf;
    int64_t cues_start = -1, cues_end = -1, before_pos, bandwidth;
    int i;
    int ret;

    // determine cues start and end positions
    for (i = 0; i < seekhead_list->nb_elem; i++)
        if (seekhead[i].id == MATROSKA_ID_CUES)
            break;

    if (i >= seekhead_list->nb_elem) return -1;

    before_pos = avio_tell(matroska->ctx->pb);
    cues_start = seekhead[i].pos + matroska->segment_start;
    if (avio_seek(matroska->ctx->pb, cues_start, SEEK_SET) == cues_start) {
        // cues_end is computed as cues_start + cues_length + length of the
        // Cues element ID (i.e. 4) + EBML length of the Cues element.
        // cues_end is inclusive and the above sum is reduced by 1.
        uint64_t cues_length, cues_id;
        int bytes_read;
        bytes_read = ebml_read_num   (matroska, matroska->ctx->pb, 4, &cues_id, 1);
        if (bytes_read < 0 || cues_id != (MATROSKA_ID_CUES & 0xfffffff))
            return bytes_read < 0 ? bytes_read : AVERROR_INVALIDDATA;
        bytes_read = ebml_read_length(matroska, matroska->ctx->pb, &cues_length);
        if (bytes_read < 0)
            return bytes_read;
        cues_end = cues_start + 4 + bytes_read + cues_length - 1;
    }
    avio_seek(matroska->ctx->pb, before_pos, SEEK_SET);
    if (cues_start == -1 || cues_end == -1) return -1;

    // parse the cues
    matroska_parse_cues(matroska);

    if (!sti->nb_index_entries)
        return AVERROR_INVALIDDATA;

    // cues start
    av_dict_set_int(&s->streams[0]->metadata, CUES_START, cues_start, 0);

    // cues end
    av_dict_set_int(&s->streams[0]->metadata, CUES_END, cues_end, 0);

    // if the file has cues at the start, fix up the init range so that
    // it does not include it
    if (cues_start <= init_range)
        av_dict_set_int(&s->streams[0]->metadata, INITIALIZATION_RANGE, cues_start - 1, 0);

    // bandwidth
    bandwidth = webm_dash_manifest_compute_bandwidth(s, cues_start);
    if (bandwidth < 0) return -1;
    av_dict_set_int(&s->streams[0]->metadata, BANDWIDTH, bandwidth, 0);

    // check if all clusters start with key frames
    av_dict_set_int(&s->streams[0]->metadata, CLUSTER_KEYFRAME, webm_clusters_start_with_keyframe(s), 0);

    // Store cue point timestamps as a comma separated list
    // for checking subsegment alignment in the muxer.
    av_bprint_init(&bprint, 0, AV_BPRINT_SIZE_UNLIMITED);
    for (int i = 0; i < sti->nb_index_entries; i++)
        av_bprintf(&bprint, "%" PRId64",", sti->index_entries[i].timestamp);
    if (!av_bprint_is_complete(&bprint)) {
        av_bprint_finalize(&bprint, NULL);
        return AVERROR(ENOMEM);
    }
    // Remove the trailing ','
    bprint.str[--bprint.len] = '\0';
    if ((ret = av_bprint_finalize(&bprint, &buf)) < 0)
        return ret;
    av_dict_set(&s->streams[0]->metadata, CUE_TIMESTAMPS,
                buf, AV_DICT_DONT_STRDUP_VAL);

    return 0;
}

static int webm_dash_manifest_read_header(AVFormatContext *s)
{
    char *buf;
    int ret = matroska_read_header(s);
    int64_t init_range;
    MatroskaTrack *tracks;
    MatroskaDemuxContext *matroska = s->priv_data;
    if (ret) {
        av_log(s, AV_LOG_ERROR, "Failed to read file headers\n");
        return -1;
    }
    if (!matroska->tracks.nb_elem || !s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "No track found\n");
        return AVERROR_INVALIDDATA;
    }

    if (!matroska->is_live) {
        buf = av_asprintf("%g", matroska->duration);
        if (!buf)
            return AVERROR(ENOMEM);
        av_dict_set(&s->streams[0]->metadata, DURATION,
                    buf, AV_DICT_DONT_STRDUP_VAL);

        // initialization range
        // 5 is the offset of Cluster ID.
        init_range = avio_tell(s->pb) - 5;
        av_dict_set_int(&s->streams[0]->metadata, INITIALIZATION_RANGE, init_range, 0);
    }

    // basename of the file
    buf = strrchr(s->url, '/');
    av_dict_set(&s->streams[0]->metadata, FILENAME, buf ? ++buf : s->url, 0);

    // track number
    tracks = matroska->tracks.elem;
    av_dict_set_int(&s->streams[0]->metadata, TRACK_NUMBER, tracks[0].num, 0);

    // parse the cues and populate Cue related fields
    if (!matroska->is_live) {
        ret = webm_dash_manifest_cues(s, init_range);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Error parsing Cues\n");
            return ret;
        }
    }

    // use the bandwidth from the command line if it was provided
    if (matroska->bandwidth > 0) {
        av_dict_set_int(&s->streams[0]->metadata, BANDWIDTH,
                        matroska->bandwidth, 0);
    }
    return 0;
}

static int webm_dash_manifest_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return AVERROR_EOF;
}

#define OFFSET(x) offsetof(MatroskaDemuxContext, x)
static const AVOption options[] = {
    { "live", "flag indicating that the input is a live file that only has the headers.", OFFSET(is_live), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "bandwidth", "bandwidth of this stream to be specified in the DASH manifest.", OFFSET(bandwidth), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass webm_dash_class = {
    .class_name = "WebM DASH Manifest demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_webm_dash_manifest_demuxer = {
    .p.name         = "webm_dash_manifest",
    .p.long_name    = NULL_IF_CONFIG_SMALL("WebM DASH Manifest"),
    .p.priv_class   = &webm_dash_class,
    .priv_data_size = sizeof(MatroskaDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_header    = webm_dash_manifest_read_header,
    .read_packet    = webm_dash_manifest_read_packet,
    .read_close     = matroska_read_close,
};
#endif

const FFInputFormat ff_matroska_demuxer = {
    .p.name         = "matroska,webm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Matroska / WebM"),
    .p.extensions   = "mkv,mk3d,mka,mks,webm",
    .p.mime_type    = "audio/webm,audio/x-matroska,video/webm,video/x-matroska",
    .priv_data_size = sizeof(MatroskaDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = matroska_probe,
    .read_header    = matroska_read_header,
    .read_packet    = matroska_read_packet,
    .read_close     = matroska_read_close,
    .read_seek      = matroska_read_seek,
};
