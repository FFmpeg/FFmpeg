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
 * by Ronald Bultje <rbultje@ronald.bitfreak.net>
 * with a little help from Moritz Bunkus <moritz@bunkus.org>
 * totally reworked by Aurelien Jacobs <aurel@gnuage.org>
 * Specs available on the Matroska project page: http://www.matroska.org/.
 */

#include <stdio.h>
#include "avformat.h"
#include "internal.h"
/* For ff_codec_get_id(). */
#include "riff.h"
#include "isom.h"
#include "rm.h"
#include "matroska.h"
#include "libavcodec/mpeg4audio.h"
#include "libavutil/intfloat_readwrite.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/lzo.h"
#if CONFIG_ZLIB
#include <zlib.h>
#endif
#if CONFIG_BZLIB
#include <bzlib.h>
#endif

typedef enum {
    EBML_NONE,
    EBML_UINT,
    EBML_FLOAT,
    EBML_STR,
    EBML_UTF8,
    EBML_BIN,
    EBML_NEST,
    EBML_PASS,
    EBML_STOP,
} EbmlType;

typedef const struct EbmlSyntax {
    uint32_t id;
    EbmlType type;
    int list_elem_size;
    int data_offset;
    union {
        uint64_t    u;
        double      f;
        const char *s;
        const struct EbmlSyntax *n;
    } def;
} EbmlSyntax;

typedef struct {
    int nb_elem;
    void *elem;
} EbmlList;

typedef struct {
    int      size;
    uint8_t *data;
    int64_t  pos;
} EbmlBin;

typedef struct {
    uint64_t version;
    uint64_t max_size;
    uint64_t id_length;
    char    *doctype;
    uint64_t doctype_version;
} Ebml;

typedef struct {
    uint64_t algo;
    EbmlBin  settings;
} MatroskaTrackCompression;

typedef struct {
    uint64_t scope;
    uint64_t type;
    MatroskaTrackCompression compression;
} MatroskaTrackEncoding;

typedef struct {
    double   frame_rate;
    uint64_t display_width;
    uint64_t display_height;
    uint64_t pixel_width;
    uint64_t pixel_height;
    uint64_t fourcc;
} MatroskaTrackVideo;

typedef struct {
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
    uint8_t *buf;
} MatroskaTrackAudio;

typedef struct {
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
    MatroskaTrackVideo video;
    MatroskaTrackAudio audio;
    EbmlList encodings;

    AVStream *stream;
    int64_t end_timecode;
    int ms_compat;
} MatroskaTrack;

typedef struct {
    uint64_t uid;
    char *filename;
    char *mime;
    EbmlBin bin;

    AVStream *stream;
} MatroskaAttachement;

typedef struct {
    uint64_t start;
    uint64_t end;
    uint64_t uid;
    char    *title;

    AVChapter *chapter;
} MatroskaChapter;

typedef struct {
    uint64_t track;
    uint64_t pos;
} MatroskaIndexPos;

typedef struct {
    uint64_t time;
    EbmlList pos;
} MatroskaIndex;

typedef struct {
    char *name;
    char *string;
    char *lang;
    uint64_t def;
    EbmlList sub;
} MatroskaTag;

typedef struct {
    char    *type;
    uint64_t typevalue;
    uint64_t trackuid;
    uint64_t chapteruid;
    uint64_t attachuid;
} MatroskaTagTarget;

typedef struct {
    MatroskaTagTarget target;
    EbmlList tag;
} MatroskaTags;

typedef struct {
    uint64_t id;
    uint64_t pos;
} MatroskaSeekhead;

typedef struct {
    uint64_t start;
    uint64_t length;
} MatroskaLevel;

typedef struct {
    AVFormatContext *ctx;

    /* EBML stuff */
    int num_levels;
    MatroskaLevel levels[EBML_MAX_DEPTH];
    int level_up;

    uint64_t time_scale;
    double   duration;
    char    *title;
    EbmlList tracks;
    EbmlList attachments;
    EbmlList chapters;
    EbmlList index;
    EbmlList tags;
    EbmlList seekhead;

    /* byte position of the segment inside the stream */
    int64_t segment_start;

    /* the packet queue */
    AVPacket **packets;
    int num_packets;
    AVPacket *prev_pkt;

    int done;
    int has_cluster_id;

    /* What to skip before effectively reading a packet. */
    int skip_to_keyframe;
    uint64_t skip_to_timecode;
} MatroskaDemuxContext;

typedef struct {
    uint64_t duration;
    int64_t  reference;
    uint64_t non_simple;
    EbmlBin  bin;
} MatroskaBlock;

typedef struct {
    uint64_t timecode;
    EbmlList blocks;
} MatroskaCluster;

static EbmlSyntax ebml_header[] = {
    { EBML_ID_EBMLREADVERSION,        EBML_UINT, 0, offsetof(Ebml,version), {.u=EBML_VERSION} },
    { EBML_ID_EBMLMAXSIZELENGTH,      EBML_UINT, 0, offsetof(Ebml,max_size), {.u=8} },
    { EBML_ID_EBMLMAXIDLENGTH,        EBML_UINT, 0, offsetof(Ebml,id_length), {.u=4} },
    { EBML_ID_DOCTYPE,                EBML_STR,  0, offsetof(Ebml,doctype), {.s="(none)"} },
    { EBML_ID_DOCTYPEREADVERSION,     EBML_UINT, 0, offsetof(Ebml,doctype_version), {.u=1} },
    { EBML_ID_EBMLVERSION,            EBML_NONE },
    { EBML_ID_DOCTYPEVERSION,         EBML_NONE },
    { 0 }
};

static EbmlSyntax ebml_syntax[] = {
    { EBML_ID_HEADER,                 EBML_NEST, 0, 0, {.n=ebml_header} },
    { 0 }
};

static EbmlSyntax matroska_info[] = {
    { MATROSKA_ID_TIMECODESCALE,      EBML_UINT,  0, offsetof(MatroskaDemuxContext,time_scale), {.u=1000000} },
    { MATROSKA_ID_DURATION,           EBML_FLOAT, 0, offsetof(MatroskaDemuxContext,duration) },
    { MATROSKA_ID_TITLE,              EBML_UTF8,  0, offsetof(MatroskaDemuxContext,title) },
    { MATROSKA_ID_WRITINGAPP,         EBML_NONE },
    { MATROSKA_ID_MUXINGAPP,          EBML_NONE },
    { MATROSKA_ID_DATEUTC,            EBML_NONE },
    { MATROSKA_ID_SEGMENTUID,         EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_track_video[] = {
    { MATROSKA_ID_VIDEOFRAMERATE,     EBML_FLOAT,0, offsetof(MatroskaTrackVideo,frame_rate) },
    { MATROSKA_ID_VIDEODISPLAYWIDTH,  EBML_UINT, 0, offsetof(MatroskaTrackVideo,display_width) },
    { MATROSKA_ID_VIDEODISPLAYHEIGHT, EBML_UINT, 0, offsetof(MatroskaTrackVideo,display_height) },
    { MATROSKA_ID_VIDEOPIXELWIDTH,    EBML_UINT, 0, offsetof(MatroskaTrackVideo,pixel_width) },
    { MATROSKA_ID_VIDEOPIXELHEIGHT,   EBML_UINT, 0, offsetof(MatroskaTrackVideo,pixel_height) },
    { MATROSKA_ID_VIDEOCOLORSPACE,    EBML_UINT, 0, offsetof(MatroskaTrackVideo,fourcc) },
    { MATROSKA_ID_VIDEOPIXELCROPB,    EBML_NONE },
    { MATROSKA_ID_VIDEOPIXELCROPT,    EBML_NONE },
    { MATROSKA_ID_VIDEOPIXELCROPL,    EBML_NONE },
    { MATROSKA_ID_VIDEOPIXELCROPR,    EBML_NONE },
    { MATROSKA_ID_VIDEODISPLAYUNIT,   EBML_NONE },
    { MATROSKA_ID_VIDEOFLAGINTERLACED,EBML_NONE },
    { MATROSKA_ID_VIDEOSTEREOMODE,    EBML_NONE },
    { MATROSKA_ID_VIDEOASPECTRATIO,   EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_track_audio[] = {
    { MATROSKA_ID_AUDIOSAMPLINGFREQ,  EBML_FLOAT,0, offsetof(MatroskaTrackAudio,samplerate), {.f=8000.0} },
    { MATROSKA_ID_AUDIOOUTSAMPLINGFREQ,EBML_FLOAT,0,offsetof(MatroskaTrackAudio,out_samplerate) },
    { MATROSKA_ID_AUDIOBITDEPTH,      EBML_UINT, 0, offsetof(MatroskaTrackAudio,bitdepth) },
    { MATROSKA_ID_AUDIOCHANNELS,      EBML_UINT, 0, offsetof(MatroskaTrackAudio,channels), {.u=1} },
    { 0 }
};

static EbmlSyntax matroska_track_encoding_compression[] = {
    { MATROSKA_ID_ENCODINGCOMPALGO,   EBML_UINT, 0, offsetof(MatroskaTrackCompression,algo), {.u=0} },
    { MATROSKA_ID_ENCODINGCOMPSETTINGS,EBML_BIN, 0, offsetof(MatroskaTrackCompression,settings) },
    { 0 }
};

static EbmlSyntax matroska_track_encoding[] = {
    { MATROSKA_ID_ENCODINGSCOPE,      EBML_UINT, 0, offsetof(MatroskaTrackEncoding,scope), {.u=1} },
    { MATROSKA_ID_ENCODINGTYPE,       EBML_UINT, 0, offsetof(MatroskaTrackEncoding,type), {.u=0} },
    { MATROSKA_ID_ENCODINGCOMPRESSION,EBML_NEST, 0, offsetof(MatroskaTrackEncoding,compression), {.n=matroska_track_encoding_compression} },
    { MATROSKA_ID_ENCODINGORDER,      EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_track_encodings[] = {
    { MATROSKA_ID_TRACKCONTENTENCODING, EBML_NEST, sizeof(MatroskaTrackEncoding), offsetof(MatroskaTrack,encodings), {.n=matroska_track_encoding} },
    { 0 }
};

static EbmlSyntax matroska_track[] = {
    { MATROSKA_ID_TRACKNUMBER,          EBML_UINT, 0, offsetof(MatroskaTrack,num) },
    { MATROSKA_ID_TRACKNAME,            EBML_UTF8, 0, offsetof(MatroskaTrack,name) },
    { MATROSKA_ID_TRACKUID,             EBML_UINT, 0, offsetof(MatroskaTrack,uid) },
    { MATROSKA_ID_TRACKTYPE,            EBML_UINT, 0, offsetof(MatroskaTrack,type) },
    { MATROSKA_ID_CODECID,              EBML_STR,  0, offsetof(MatroskaTrack,codec_id) },
    { MATROSKA_ID_CODECPRIVATE,         EBML_BIN,  0, offsetof(MatroskaTrack,codec_priv) },
    { MATROSKA_ID_TRACKLANGUAGE,        EBML_UTF8, 0, offsetof(MatroskaTrack,language), {.s="eng"} },
    { MATROSKA_ID_TRACKDEFAULTDURATION, EBML_UINT, 0, offsetof(MatroskaTrack,default_duration) },
    { MATROSKA_ID_TRACKTIMECODESCALE,   EBML_FLOAT,0, offsetof(MatroskaTrack,time_scale), {.f=1.0} },
    { MATROSKA_ID_TRACKFLAGDEFAULT,     EBML_UINT, 0, offsetof(MatroskaTrack,flag_default), {.u=1} },
    { MATROSKA_ID_TRACKVIDEO,           EBML_NEST, 0, offsetof(MatroskaTrack,video), {.n=matroska_track_video} },
    { MATROSKA_ID_TRACKAUDIO,           EBML_NEST, 0, offsetof(MatroskaTrack,audio), {.n=matroska_track_audio} },
    { MATROSKA_ID_TRACKCONTENTENCODINGS,EBML_NEST, 0, 0, {.n=matroska_track_encodings} },
    { MATROSKA_ID_TRACKFLAGENABLED,     EBML_NONE },
    { MATROSKA_ID_TRACKFLAGFORCED,      EBML_NONE },
    { MATROSKA_ID_TRACKFLAGLACING,      EBML_NONE },
    { MATROSKA_ID_CODECNAME,            EBML_NONE },
    { MATROSKA_ID_CODECDECODEALL,       EBML_NONE },
    { MATROSKA_ID_CODECINFOURL,         EBML_NONE },
    { MATROSKA_ID_CODECDOWNLOADURL,     EBML_NONE },
    { MATROSKA_ID_TRACKMINCACHE,        EBML_NONE },
    { MATROSKA_ID_TRACKMAXCACHE,        EBML_NONE },
    { MATROSKA_ID_TRACKMAXBLKADDID,     EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_tracks[] = {
    { MATROSKA_ID_TRACKENTRY,         EBML_NEST, sizeof(MatroskaTrack), offsetof(MatroskaDemuxContext,tracks), {.n=matroska_track} },
    { 0 }
};

static EbmlSyntax matroska_attachment[] = {
    { MATROSKA_ID_FILEUID,            EBML_UINT, 0, offsetof(MatroskaAttachement,uid) },
    { MATROSKA_ID_FILENAME,           EBML_UTF8, 0, offsetof(MatroskaAttachement,filename) },
    { MATROSKA_ID_FILEMIMETYPE,       EBML_STR,  0, offsetof(MatroskaAttachement,mime) },
    { MATROSKA_ID_FILEDATA,           EBML_BIN,  0, offsetof(MatroskaAttachement,bin) },
    { MATROSKA_ID_FILEDESC,           EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_attachments[] = {
    { MATROSKA_ID_ATTACHEDFILE,       EBML_NEST, sizeof(MatroskaAttachement), offsetof(MatroskaDemuxContext,attachments), {.n=matroska_attachment} },
    { 0 }
};

static EbmlSyntax matroska_chapter_display[] = {
    { MATROSKA_ID_CHAPSTRING,         EBML_UTF8, 0, offsetof(MatroskaChapter,title) },
    { MATROSKA_ID_CHAPLANG,           EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_chapter_entry[] = {
    { MATROSKA_ID_CHAPTERTIMESTART,   EBML_UINT, 0, offsetof(MatroskaChapter,start), {.u=AV_NOPTS_VALUE} },
    { MATROSKA_ID_CHAPTERTIMEEND,     EBML_UINT, 0, offsetof(MatroskaChapter,end), {.u=AV_NOPTS_VALUE} },
    { MATROSKA_ID_CHAPTERUID,         EBML_UINT, 0, offsetof(MatroskaChapter,uid) },
    { MATROSKA_ID_CHAPTERDISPLAY,     EBML_NEST, 0, 0, {.n=matroska_chapter_display} },
    { MATROSKA_ID_CHAPTERFLAGHIDDEN,  EBML_NONE },
    { MATROSKA_ID_CHAPTERFLAGENABLED, EBML_NONE },
    { MATROSKA_ID_CHAPTERPHYSEQUIV,   EBML_NONE },
    { MATROSKA_ID_CHAPTERATOM,        EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_chapter[] = {
    { MATROSKA_ID_CHAPTERATOM,        EBML_NEST, sizeof(MatroskaChapter), offsetof(MatroskaDemuxContext,chapters), {.n=matroska_chapter_entry} },
    { MATROSKA_ID_EDITIONUID,         EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGHIDDEN,  EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGDEFAULT, EBML_NONE },
    { MATROSKA_ID_EDITIONFLAGORDERED, EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_chapters[] = {
    { MATROSKA_ID_EDITIONENTRY,       EBML_NEST, 0, 0, {.n=matroska_chapter} },
    { 0 }
};

static EbmlSyntax matroska_index_pos[] = {
    { MATROSKA_ID_CUETRACK,           EBML_UINT, 0, offsetof(MatroskaIndexPos,track) },
    { MATROSKA_ID_CUECLUSTERPOSITION, EBML_UINT, 0, offsetof(MatroskaIndexPos,pos)   },
    { MATROSKA_ID_CUEBLOCKNUMBER,     EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_index_entry[] = {
    { MATROSKA_ID_CUETIME,            EBML_UINT, 0, offsetof(MatroskaIndex,time) },
    { MATROSKA_ID_CUETRACKPOSITION,   EBML_NEST, sizeof(MatroskaIndexPos), offsetof(MatroskaIndex,pos), {.n=matroska_index_pos} },
    { 0 }
};

static EbmlSyntax matroska_index[] = {
    { MATROSKA_ID_POINTENTRY,         EBML_NEST, sizeof(MatroskaIndex), offsetof(MatroskaDemuxContext,index), {.n=matroska_index_entry} },
    { 0 }
};

static EbmlSyntax matroska_simpletag[] = {
    { MATROSKA_ID_TAGNAME,            EBML_UTF8, 0, offsetof(MatroskaTag,name) },
    { MATROSKA_ID_TAGSTRING,          EBML_UTF8, 0, offsetof(MatroskaTag,string) },
    { MATROSKA_ID_TAGLANG,            EBML_STR,  0, offsetof(MatroskaTag,lang), {.s="und"} },
    { MATROSKA_ID_TAGDEFAULT,         EBML_UINT, 0, offsetof(MatroskaTag,def) },
    { MATROSKA_ID_SIMPLETAG,          EBML_NEST, sizeof(MatroskaTag), offsetof(MatroskaTag,sub), {.n=matroska_simpletag} },
    { 0 }
};

static EbmlSyntax matroska_tagtargets[] = {
    { MATROSKA_ID_TAGTARGETS_TYPE,      EBML_STR,  0, offsetof(MatroskaTagTarget,type) },
    { MATROSKA_ID_TAGTARGETS_TYPEVALUE, EBML_UINT, 0, offsetof(MatroskaTagTarget,typevalue), {.u=50} },
    { MATROSKA_ID_TAGTARGETS_TRACKUID,  EBML_UINT, 0, offsetof(MatroskaTagTarget,trackuid) },
    { MATROSKA_ID_TAGTARGETS_CHAPTERUID,EBML_UINT, 0, offsetof(MatroskaTagTarget,chapteruid) },
    { MATROSKA_ID_TAGTARGETS_ATTACHUID, EBML_UINT, 0, offsetof(MatroskaTagTarget,attachuid) },
    { 0 }
};

static EbmlSyntax matroska_tag[] = {
    { MATROSKA_ID_SIMPLETAG,          EBML_NEST, sizeof(MatroskaTag), offsetof(MatroskaTags,tag), {.n=matroska_simpletag} },
    { MATROSKA_ID_TAGTARGETS,         EBML_NEST, 0, offsetof(MatroskaTags,target), {.n=matroska_tagtargets} },
    { 0 }
};

static EbmlSyntax matroska_tags[] = {
    { MATROSKA_ID_TAG,                EBML_NEST, sizeof(MatroskaTags), offsetof(MatroskaDemuxContext,tags), {.n=matroska_tag} },
    { 0 }
};

static EbmlSyntax matroska_seekhead_entry[] = {
    { MATROSKA_ID_SEEKID,             EBML_UINT, 0, offsetof(MatroskaSeekhead,id) },
    { MATROSKA_ID_SEEKPOSITION,       EBML_UINT, 0, offsetof(MatroskaSeekhead,pos), {.u=-1} },
    { 0 }
};

static EbmlSyntax matroska_seekhead[] = {
    { MATROSKA_ID_SEEKENTRY,          EBML_NEST, sizeof(MatroskaSeekhead), offsetof(MatroskaDemuxContext,seekhead), {.n=matroska_seekhead_entry} },
    { 0 }
};

static EbmlSyntax matroska_segment[] = {
    { MATROSKA_ID_INFO,           EBML_NEST, 0, 0, {.n=matroska_info       } },
    { MATROSKA_ID_TRACKS,         EBML_NEST, 0, 0, {.n=matroska_tracks     } },
    { MATROSKA_ID_ATTACHMENTS,    EBML_NEST, 0, 0, {.n=matroska_attachments} },
    { MATROSKA_ID_CHAPTERS,       EBML_NEST, 0, 0, {.n=matroska_chapters   } },
    { MATROSKA_ID_CUES,           EBML_NEST, 0, 0, {.n=matroska_index      } },
    { MATROSKA_ID_TAGS,           EBML_NEST, 0, 0, {.n=matroska_tags       } },
    { MATROSKA_ID_SEEKHEAD,       EBML_NEST, 0, 0, {.n=matroska_seekhead   } },
    { MATROSKA_ID_CLUSTER,        EBML_STOP, 0, offsetof(MatroskaDemuxContext,has_cluster_id) },
    { 0 }
};

static EbmlSyntax matroska_segments[] = {
    { MATROSKA_ID_SEGMENT,        EBML_NEST, 0, 0, {.n=matroska_segment    } },
    { 0 }
};

static EbmlSyntax matroska_blockgroup[] = {
    { MATROSKA_ID_BLOCK,          EBML_BIN,  0, offsetof(MatroskaBlock,bin) },
    { MATROSKA_ID_SIMPLEBLOCK,    EBML_BIN,  0, offsetof(MatroskaBlock,bin) },
    { MATROSKA_ID_BLOCKDURATION,  EBML_UINT, 0, offsetof(MatroskaBlock,duration), {.u=AV_NOPTS_VALUE} },
    { MATROSKA_ID_BLOCKREFERENCE, EBML_UINT, 0, offsetof(MatroskaBlock,reference) },
    { 1,                          EBML_UINT, 0, offsetof(MatroskaBlock,non_simple), {.u=1} },
    { 0 }
};

static EbmlSyntax matroska_cluster[] = {
    { MATROSKA_ID_CLUSTERTIMECODE,EBML_UINT,0, offsetof(MatroskaCluster,timecode) },
    { MATROSKA_ID_BLOCKGROUP,     EBML_NEST, sizeof(MatroskaBlock), offsetof(MatroskaCluster,blocks), {.n=matroska_blockgroup} },
    { MATROSKA_ID_SIMPLEBLOCK,    EBML_PASS, sizeof(MatroskaBlock), offsetof(MatroskaCluster,blocks), {.n=matroska_blockgroup} },
    { MATROSKA_ID_CLUSTERPOSITION,EBML_NONE },
    { MATROSKA_ID_CLUSTERPREVSIZE,EBML_NONE },
    { 0 }
};

static EbmlSyntax matroska_clusters[] = {
    { MATROSKA_ID_CLUSTER,        EBML_NEST, 0, 0, {.n=matroska_cluster} },
    { MATROSKA_ID_INFO,           EBML_NONE },
    { MATROSKA_ID_CUES,           EBML_NONE },
    { MATROSKA_ID_TAGS,           EBML_NONE },
    { MATROSKA_ID_SEEKHEAD,       EBML_NONE },
    { 0 }
};

static const char *matroska_doctypes[] = { "matroska", "webm" };

/*
 * Return: Whether we reached the end of a level in the hierarchy or not.
 */
static int ebml_level_end(MatroskaDemuxContext *matroska)
{
    ByteIOContext *pb = matroska->ctx->pb;
    int64_t pos = url_ftell(pb);

    if (matroska->num_levels > 0) {
        MatroskaLevel *level = &matroska->levels[matroska->num_levels - 1];
        if (pos - level->start >= level->length) {
            matroska->num_levels--;
            return 1;
        }
    }
    return 0;
}

/*
 * Read: an "EBML number", which is defined as a variable-length
 * array of bytes. The first byte indicates the length by giving a
 * number of 0-bits followed by a one. The position of the first
 * "one" bit inside the first byte indicates the length of this
 * number.
 * Returns: number of bytes read, < 0 on error
 */
static int ebml_read_num(MatroskaDemuxContext *matroska, ByteIOContext *pb,
                         int max_size, uint64_t *number)
{
    int len_mask = 0x80, read = 1, n = 1;
    int64_t total = 0;

    /* The first byte tells us the length in bytes - get_byte() can normally
     * return 0, but since that's not a valid first ebmlID byte, we can
     * use it safely here to catch EOS. */
    if (!(total = get_byte(pb))) {
        /* we might encounter EOS here */
        if (!url_feof(pb)) {
            int64_t pos = url_ftell(pb);
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Read error at pos. %"PRIu64" (0x%"PRIx64")\n",
                   pos, pos);
        }
        return AVERROR(EIO); /* EOS or actual I/O error */
    }

    /* get the length of the EBML number */
    while (read <= max_size && !(total & len_mask)) {
        read++;
        len_mask >>= 1;
    }
    if (read > max_size) {
        int64_t pos = url_ftell(pb) - 1;
        av_log(matroska->ctx, AV_LOG_ERROR,
               "Invalid EBML number size tag 0x%02x at pos %"PRIu64" (0x%"PRIx64")\n",
               (uint8_t) total, pos, pos);
        return AVERROR_INVALIDDATA;
    }

    /* read out length */
    total &= ~len_mask;
    while (n++ < read)
        total = (total << 8) | get_byte(pb);

    *number = total;

    return read;
}

/*
 * Read the next element as an unsigned int.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_uint(ByteIOContext *pb, int size, uint64_t *num)
{
    int n = 0;

    if (size < 1 || size > 8)
        return AVERROR_INVALIDDATA;

    /* big-endian ordering; build up number */
    *num = 0;
    while (n++ < size)
        *num = (*num << 8) | get_byte(pb);

    return 0;
}

/*
 * Read the next element as a float.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_float(ByteIOContext *pb, int size, double *num)
{
    if (size == 4) {
        *num= av_int2flt(get_be32(pb));
    } else if(size==8){
        *num= av_int2dbl(get_be64(pb));
    } else
        return AVERROR_INVALIDDATA;

    return 0;
}

/*
 * Read the next element as an ASCII string.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_ascii(ByteIOContext *pb, int size, char **str)
{
    av_free(*str);
    /* EBML strings are usually not 0-terminated, so we allocate one
     * byte more, read the string and NULL-terminate it ourselves. */
    if (!(*str = av_malloc(size + 1)))
        return AVERROR(ENOMEM);
    if (get_buffer(pb, (uint8_t *) *str, size) != size) {
        av_free(*str);
        return AVERROR(EIO);
    }
    (*str)[size] = '\0';

    return 0;
}

/*
 * Read the next element as binary data.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_binary(ByteIOContext *pb, int length, EbmlBin *bin)
{
    av_free(bin->data);
    if (!(bin->data = av_malloc(length)))
        return AVERROR(ENOMEM);

    bin->size = length;
    bin->pos  = url_ftell(pb);
    if (get_buffer(pb, bin->data, length) != length)
        return AVERROR(EIO);

    return 0;
}

/*
 * Read the next element, but only the header. The contents
 * are supposed to be sub-elements which can be read separately.
 * 0 is success, < 0 is failure.
 */
static int ebml_read_master(MatroskaDemuxContext *matroska, int length)
{
    ByteIOContext *pb = matroska->ctx->pb;
    MatroskaLevel *level;

    if (matroska->num_levels >= EBML_MAX_DEPTH) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "File moves beyond max. allowed depth (%d)\n", EBML_MAX_DEPTH);
        return AVERROR(ENOSYS);
    }

    level = &matroska->levels[matroska->num_levels++];
    level->start = url_ftell(pb);
    level->length = length;

    return 0;
}

/*
 * Read signed/unsigned "EBML" numbers.
 * Return: number of bytes processed, < 0 on error
 */
static int matroska_ebmlnum_uint(MatroskaDemuxContext *matroska,
                                 uint8_t *data, uint32_t size, uint64_t *num)
{
    ByteIOContext pb;
    init_put_byte(&pb, data, size, 0, NULL, NULL, NULL, NULL);
    return ebml_read_num(matroska, &pb, 8, num);
}

/*
 * Same as above, but signed.
 */
static int matroska_ebmlnum_sint(MatroskaDemuxContext *matroska,
                                 uint8_t *data, uint32_t size, int64_t *num)
{
    uint64_t unum;
    int res;

    /* read as unsigned number first */
    if ((res = matroska_ebmlnum_uint(matroska, data, size, &unum)) < 0)
        return res;

    /* make signed (weird way) */
    *num = unum - ((1LL << (7*res - 1)) - 1);

    return res;
}

static int ebml_parse_elem(MatroskaDemuxContext *matroska,
                           EbmlSyntax *syntax, void *data);

static int ebml_parse_id(MatroskaDemuxContext *matroska, EbmlSyntax *syntax,
                         uint32_t id, void *data)
{
    int i;
    for (i=0; syntax[i].id; i++)
        if (id == syntax[i].id)
            break;
    if (!syntax[i].id && id != EBML_ID_VOID && id != EBML_ID_CRC32)
        av_log(matroska->ctx, AV_LOG_INFO, "Unknown entry 0x%X\n", id);
    return ebml_parse_elem(matroska, &syntax[i], data);
}

static int ebml_parse(MatroskaDemuxContext *matroska, EbmlSyntax *syntax,
                      void *data)
{
    uint64_t id;
    int res = ebml_read_num(matroska, matroska->ctx->pb, 4, &id);
    id |= 1 << 7*res;
    return res < 0 ? res : ebml_parse_id(matroska, syntax, id, data);
}

static int ebml_parse_nest(MatroskaDemuxContext *matroska, EbmlSyntax *syntax,
                           void *data)
{
    int i, res = 0;

    for (i=0; syntax[i].id; i++)
        switch (syntax[i].type) {
        case EBML_UINT:
            *(uint64_t *)((char *)data+syntax[i].data_offset) = syntax[i].def.u;
            break;
        case EBML_FLOAT:
            *(double   *)((char *)data+syntax[i].data_offset) = syntax[i].def.f;
            break;
        case EBML_STR:
        case EBML_UTF8:
            *(char    **)((char *)data+syntax[i].data_offset) = av_strdup(syntax[i].def.s);
            break;
        }

    while (!res && !ebml_level_end(matroska))
        res = ebml_parse(matroska, syntax, data);

    return res;
}

static int ebml_parse_elem(MatroskaDemuxContext *matroska,
                           EbmlSyntax *syntax, void *data)
{
    ByteIOContext *pb = matroska->ctx->pb;
    uint32_t id = syntax->id;
    uint64_t length;
    int res;

    data = (char *)data + syntax->data_offset;
    if (syntax->list_elem_size) {
        EbmlList *list = data;
        list->elem = av_realloc(list->elem, (list->nb_elem+1)*syntax->list_elem_size);
        data = (char*)list->elem + list->nb_elem*syntax->list_elem_size;
        memset(data, 0, syntax->list_elem_size);
        list->nb_elem++;
    }

    if (syntax->type != EBML_PASS && syntax->type != EBML_STOP)
        if ((res = ebml_read_num(matroska, pb, 8, &length)) < 0)
            return res;

    switch (syntax->type) {
    case EBML_UINT:  res = ebml_read_uint  (pb, length, data);  break;
    case EBML_FLOAT: res = ebml_read_float (pb, length, data);  break;
    case EBML_STR:
    case EBML_UTF8:  res = ebml_read_ascii (pb, length, data);  break;
    case EBML_BIN:   res = ebml_read_binary(pb, length, data);  break;
    case EBML_NEST:  if ((res=ebml_read_master(matroska, length)) < 0)
                         return res;
                     if (id == MATROSKA_ID_SEGMENT)
                         matroska->segment_start = url_ftell(matroska->ctx->pb);
                     return ebml_parse_nest(matroska, syntax->def.n, data);
    case EBML_PASS:  return ebml_parse_id(matroska, syntax->def.n, id, data);
    case EBML_STOP:  *(int *)data = 1;      return 1;
    default:         return url_fseek(pb,length,SEEK_CUR)<0 ? AVERROR(EIO) : 0;
    }
    if (res == AVERROR_INVALIDDATA)
        av_log(matroska->ctx, AV_LOG_ERROR, "Invalid element\n");
    else if (res == AVERROR(EIO))
        av_log(matroska->ctx, AV_LOG_ERROR, "Read error\n");
    return res;
}

static void ebml_free(EbmlSyntax *syntax, void *data)
{
    int i, j;
    for (i=0; syntax[i].id; i++) {
        void *data_off = (char *)data + syntax[i].data_offset;
        switch (syntax[i].type) {
        case EBML_STR:
        case EBML_UTF8:  av_freep(data_off);                      break;
        case EBML_BIN:   av_freep(&((EbmlBin *)data_off)->data);  break;
        case EBML_NEST:
            if (syntax[i].list_elem_size) {
                EbmlList *list = data_off;
                char *ptr = list->elem;
                for (j=0; j<list->nb_elem; j++, ptr+=syntax[i].list_elem_size)
                    ebml_free(syntax[i].def.n, ptr);
                av_free(list->elem);
            } else
                ebml_free(syntax[i].def.n, data_off);
        default:  break;
        }
    }
}


/*
 * Autodetecting...
 */
static int matroska_probe(AVProbeData *p)
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

    /* Does the probe data contain the whole header? */
    if (p->buf_size < 4 + size + total)
      return 0;

    /* The header should contain a known document type. For now,
     * we don't parse the whole header but simply check for the
     * availability of that array of characters inside the header.
     * Not fully fool-proof, but good enough. */
    for (i = 0; i < FF_ARRAY_ELEMS(matroska_doctypes); i++) {
        int probelen = strlen(matroska_doctypes[i]);
        for (n = 4+size; n <= 4+size+total-probelen; n++)
            if (!memcmp(p->buf+n, matroska_doctypes[i], probelen))
                return AVPROBE_SCORE_MAX;
    }

    // probably valid EBML header but no recognized doctype
    return AVPROBE_SCORE_MAX/2;
}

static MatroskaTrack *matroska_find_track_by_num(MatroskaDemuxContext *matroska,
                                                 int num)
{
    MatroskaTrack *tracks = matroska->tracks.elem;
    int i;

    for (i=0; i < matroska->tracks.nb_elem; i++)
        if (tracks[i].num == num)
            return &tracks[i];

    av_log(matroska->ctx, AV_LOG_ERROR, "Invalid track number %d\n", num);
    return NULL;
}

static int matroska_decode_buffer(uint8_t** buf, int* buf_size,
                                  MatroskaTrack *track)
{
    MatroskaTrackEncoding *encodings = track->encodings.elem;
    uint8_t* data = *buf;
    int isize = *buf_size;
    uint8_t* pkt_data = NULL;
    int pkt_size = isize;
    int result = 0;
    int olen;

    switch (encodings[0].compression.algo) {
    case MATROSKA_TRACK_ENCODING_COMP_HEADERSTRIP:
        return encodings[0].compression.settings.size;
    case MATROSKA_TRACK_ENCODING_COMP_LZO:
        do {
            olen = pkt_size *= 3;
            pkt_data = av_realloc(pkt_data, pkt_size+AV_LZO_OUTPUT_PADDING);
            result = av_lzo1x_decode(pkt_data, &olen, data, &isize);
        } while (result==AV_LZO_OUTPUT_FULL && pkt_size<10000000);
        if (result)
            goto failed;
        pkt_size -= olen;
        break;
#if CONFIG_ZLIB
    case MATROSKA_TRACK_ENCODING_COMP_ZLIB: {
        z_stream zstream = {0};
        if (inflateInit(&zstream) != Z_OK)
            return -1;
        zstream.next_in = data;
        zstream.avail_in = isize;
        do {
            pkt_size *= 3;
            pkt_data = av_realloc(pkt_data, pkt_size);
            zstream.avail_out = pkt_size - zstream.total_out;
            zstream.next_out = pkt_data + zstream.total_out;
            result = inflate(&zstream, Z_NO_FLUSH);
        } while (result==Z_OK && pkt_size<10000000);
        pkt_size = zstream.total_out;
        inflateEnd(&zstream);
        if (result != Z_STREAM_END)
            goto failed;
        break;
    }
#endif
#if CONFIG_BZLIB
    case MATROSKA_TRACK_ENCODING_COMP_BZLIB: {
        bz_stream bzstream = {0};
        if (BZ2_bzDecompressInit(&bzstream, 0, 0) != BZ_OK)
            return -1;
        bzstream.next_in = data;
        bzstream.avail_in = isize;
        do {
            pkt_size *= 3;
            pkt_data = av_realloc(pkt_data, pkt_size);
            bzstream.avail_out = pkt_size - bzstream.total_out_lo32;
            bzstream.next_out = pkt_data + bzstream.total_out_lo32;
            result = BZ2_bzDecompress(&bzstream);
        } while (result==BZ_OK && pkt_size<10000000);
        pkt_size = bzstream.total_out_lo32;
        BZ2_bzDecompressEnd(&bzstream);
        if (result != BZ_STREAM_END)
            goto failed;
        break;
    }
#endif
    default:
        return -1;
    }

    *buf = pkt_data;
    *buf_size = pkt_size;
    return 0;
 failed:
    av_free(pkt_data);
    return -1;
}

static void matroska_fix_ass_packet(MatroskaDemuxContext *matroska,
                                    AVPacket *pkt, uint64_t display_duration)
{
    char *line, *layer, *ptr = pkt->data, *end = ptr+pkt->size;
    for (; *ptr!=',' && ptr<end-1; ptr++);
    if (*ptr == ',')
        layer = ++ptr;
    for (; *ptr!=',' && ptr<end-1; ptr++);
    if (*ptr == ',') {
        int64_t end_pts = pkt->pts + display_duration;
        int sc = matroska->time_scale * pkt->pts / 10000000;
        int ec = matroska->time_scale * end_pts  / 10000000;
        int sh, sm, ss, eh, em, es, len;
        sh = sc/360000;  sc -= 360000*sh;
        sm = sc/  6000;  sc -=   6000*sm;
        ss = sc/   100;  sc -=    100*ss;
        eh = ec/360000;  ec -= 360000*eh;
        em = ec/  6000;  ec -=   6000*em;
        es = ec/   100;  ec -=    100*es;
        *ptr++ = '\0';
        len = 50 + end-ptr + FF_INPUT_BUFFER_PADDING_SIZE;
        if (!(line = av_malloc(len)))
            return;
        snprintf(line,len,"Dialogue: %s,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,%s\r\n",
                 layer, sh, sm, ss, sc, eh, em, es, ec, ptr);
        av_free(pkt->data);
        pkt->data = line;
        pkt->size = strlen(line);
    }
}

static void matroska_merge_packets(AVPacket *out, AVPacket *in)
{
    out->data = av_realloc(out->data, out->size+in->size);
    memcpy(out->data+out->size, in->data, in->size);
    out->size += in->size;
    av_destruct_packet(in);
    av_free(in);
}

static void matroska_convert_tag(AVFormatContext *s, EbmlList *list,
                                 AVMetadata **metadata, char *prefix)
{
    MatroskaTag *tags = list->elem;
    char key[1024];
    int i;

    for (i=0; i < list->nb_elem; i++) {
        const char *lang = strcmp(tags[i].lang, "und") ? tags[i].lang : NULL;
        if (prefix)  snprintf(key, sizeof(key), "%s/%s", prefix, tags[i].name);
        else         av_strlcpy(key, tags[i].name, sizeof(key));
        if (tags[i].def || !lang) {
        av_metadata_set2(metadata, key, tags[i].string, 0);
        if (tags[i].sub.nb_elem)
            matroska_convert_tag(s, &tags[i].sub, metadata, key);
        }
        if (lang) {
            av_strlcat(key, "-", sizeof(key));
            av_strlcat(key, lang, sizeof(key));
            av_metadata_set2(metadata, key, tags[i].string, 0);
            if (tags[i].sub.nb_elem)
                matroska_convert_tag(s, &tags[i].sub, metadata, key);
        }
    }
}

static void matroska_convert_tags(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTags *tags = matroska->tags.elem;
    int i, j;

    for (i=0; i < matroska->tags.nb_elem; i++) {
        if (tags[i].target.attachuid) {
            MatroskaAttachement *attachment = matroska->attachments.elem;
            for (j=0; j<matroska->attachments.nb_elem; j++)
                if (attachment[j].uid == tags[i].target.attachuid)
                    matroska_convert_tag(s, &tags[i].tag,
                                         &attachment[j].stream->metadata, NULL);
        } else if (tags[i].target.chapteruid) {
            MatroskaChapter *chapter = matroska->chapters.elem;
            for (j=0; j<matroska->chapters.nb_elem; j++)
                if (chapter[j].uid == tags[i].target.chapteruid)
                    matroska_convert_tag(s, &tags[i].tag,
                                         &chapter[j].chapter->metadata, NULL);
        } else if (tags[i].target.trackuid) {
            MatroskaTrack *track = matroska->tracks.elem;
            for (j=0; j<matroska->tracks.nb_elem; j++)
                if (track[j].uid == tags[i].target.trackuid)
                    matroska_convert_tag(s, &tags[i].tag,
                                         &track[j].stream->metadata, NULL);
        } else {
            matroska_convert_tag(s, &tags[i].tag, &s->metadata,
                                 tags[i].target.type);
        }
    }
}

static void matroska_execute_seekhead(MatroskaDemuxContext *matroska)
{
    EbmlList *seekhead_list = &matroska->seekhead;
    MatroskaSeekhead *seekhead = seekhead_list->elem;
    uint32_t level_up = matroska->level_up;
    int64_t before_pos = url_ftell(matroska->ctx->pb);
    MatroskaLevel level;
    int i;

    for (i=0; i<seekhead_list->nb_elem; i++) {
        int64_t offset = seekhead[i].pos + matroska->segment_start;

        if (seekhead[i].pos <= before_pos
            || seekhead[i].id == MATROSKA_ID_SEEKHEAD
            || seekhead[i].id == MATROSKA_ID_CLUSTER)
            continue;

        /* seek */
        if (url_fseek(matroska->ctx->pb, offset, SEEK_SET) != offset)
            continue;

        /* We don't want to lose our seekhead level, so we add
         * a dummy. This is a crude hack. */
        if (matroska->num_levels == EBML_MAX_DEPTH) {
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Max EBML element depth (%d) reached, "
                   "cannot parse further.\n", EBML_MAX_DEPTH);
            break;
        }

        level.start = 0;
        level.length = (uint64_t)-1;
        matroska->levels[matroska->num_levels] = level;
        matroska->num_levels++;

        ebml_parse(matroska, matroska_segment, matroska);

        /* remove dummy level */
        while (matroska->num_levels) {
            uint64_t length = matroska->levels[--matroska->num_levels].length;
            if (length == (uint64_t)-1)
                break;
        }
    }

    /* seek back */
    url_fseek(matroska->ctx->pb, before_pos, SEEK_SET);
    matroska->level_up = level_up;
}

static int matroska_aac_profile(char *codec_id)
{
    static const char * const aac_profiles[] = { "MAIN", "LC", "SSR" };
    int profile;

    for (profile=0; profile<FF_ARRAY_ELEMS(aac_profiles); profile++)
        if (strstr(codec_id, aac_profiles[profile]))
            break;
    return profile + 1;
}

static int matroska_aac_sri(int samplerate)
{
    int sri;

    for (sri=0; sri<FF_ARRAY_ELEMS(ff_mpeg4audio_sample_rates); sri++)
        if (ff_mpeg4audio_sample_rates[sri] == samplerate)
            break;
    return sri;
}

static int matroska_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    EbmlList *attachements_list = &matroska->attachments;
    MatroskaAttachement *attachements;
    EbmlList *chapters_list = &matroska->chapters;
    MatroskaChapter *chapters;
    MatroskaTrack *tracks;
    EbmlList *index_list;
    MatroskaIndex *index;
    int index_scale = 1;
    uint64_t max_start = 0;
    Ebml ebml = { 0 };
    AVStream *st;
    int i, j;

    matroska->ctx = s;

    /* First read the EBML header. */
    if (ebml_parse(matroska, ebml_syntax, &ebml)
        || ebml.version > EBML_VERSION       || ebml.max_size > sizeof(uint64_t)
        || ebml.id_length > sizeof(uint32_t) || ebml.doctype_version > 2) {
        av_log(matroska->ctx, AV_LOG_ERROR,
               "EBML header using unsupported features\n"
               "(EBML version %"PRIu64", doctype %s, doc version %"PRIu64")\n",
               ebml.version, ebml.doctype, ebml.doctype_version);
        ebml_free(ebml_syntax, &ebml);
        return AVERROR_PATCHWELCOME;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(matroska_doctypes); i++)
        if (!strcmp(ebml.doctype, matroska_doctypes[i]))
            break;
    if (i >= FF_ARRAY_ELEMS(matroska_doctypes)) {
        av_log(s, AV_LOG_WARNING, "Unknown EBML doctype '%s'\n", ebml.doctype);
    }
    av_metadata_set2(&s->metadata, "doctype", ebml.doctype, 0);
    ebml_free(ebml_syntax, &ebml);

    /* The next thing is a segment. */
    if (ebml_parse(matroska, matroska_segments, matroska) < 0)
        return -1;
    matroska_execute_seekhead(matroska);

    if (matroska->duration)
        matroska->ctx->duration = matroska->duration * matroska->time_scale
                                  * 1000 / AV_TIME_BASE;
    av_metadata_set2(&s->metadata, "title", matroska->title, 0);

    tracks = matroska->tracks.elem;
    for (i=0; i < matroska->tracks.nb_elem; i++) {
        MatroskaTrack *track = &tracks[i];
        enum CodecID codec_id = CODEC_ID_NONE;
        EbmlList *encodings_list = &tracks->encodings;
        MatroskaTrackEncoding *encodings = encodings_list->elem;
        uint8_t *extradata = NULL;
        int extradata_size = 0;
        int extradata_offset = 0;
        ByteIOContext b;

        /* Apply some sanity checks. */
        if (track->type != MATROSKA_TRACK_TYPE_VIDEO &&
            track->type != MATROSKA_TRACK_TYPE_AUDIO &&
            track->type != MATROSKA_TRACK_TYPE_SUBTITLE) {
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Unknown or unsupported track type %"PRIu64"\n",
                   track->type);
            continue;
        }
        if (track->codec_id == NULL)
            continue;

        if (track->type == MATROSKA_TRACK_TYPE_VIDEO) {
            if (!track->default_duration)
                track->default_duration = 1000000000/track->video.frame_rate;
            if (!track->video.display_width)
                track->video.display_width = track->video.pixel_width;
            if (!track->video.display_height)
                track->video.display_height = track->video.pixel_height;
        } else if (track->type == MATROSKA_TRACK_TYPE_AUDIO) {
            if (!track->audio.out_samplerate)
                track->audio.out_samplerate = track->audio.samplerate;
        }
        if (encodings_list->nb_elem > 1) {
            av_log(matroska->ctx, AV_LOG_ERROR,
                   "Multiple combined encodings no supported");
        } else if (encodings_list->nb_elem == 1) {
            if (encodings[0].type ||
                (encodings[0].compression.algo != MATROSKA_TRACK_ENCODING_COMP_HEADERSTRIP &&
#if CONFIG_ZLIB
                 encodings[0].compression.algo != MATROSKA_TRACK_ENCODING_COMP_ZLIB &&
#endif
#if CONFIG_BZLIB
                 encodings[0].compression.algo != MATROSKA_TRACK_ENCODING_COMP_BZLIB &&
#endif
                 encodings[0].compression.algo != MATROSKA_TRACK_ENCODING_COMP_LZO)) {
                encodings[0].scope = 0;
                av_log(matroska->ctx, AV_LOG_ERROR,
                       "Unsupported encoding type");
            } else if (track->codec_priv.size && encodings[0].scope&2) {
                uint8_t *codec_priv = track->codec_priv.data;
                int offset = matroska_decode_buffer(&track->codec_priv.data,
                                                    &track->codec_priv.size,
                                                    track);
                if (offset < 0) {
                    track->codec_priv.data = NULL;
                    track->codec_priv.size = 0;
                    av_log(matroska->ctx, AV_LOG_ERROR,
                           "Failed to decode codec private data\n");
                } else if (offset > 0) {
                    track->codec_priv.data = av_malloc(track->codec_priv.size + offset);
                    memcpy(track->codec_priv.data,
                           encodings[0].compression.settings.data, offset);
                    memcpy(track->codec_priv.data+offset, codec_priv,
                           track->codec_priv.size);
                    track->codec_priv.size += offset;
                }
                if (codec_priv != track->codec_priv.data)
                    av_free(codec_priv);
            }
        }

        for(j=0; ff_mkv_codec_tags[j].id != CODEC_ID_NONE; j++){
            if(!strncmp(ff_mkv_codec_tags[j].str, track->codec_id,
                        strlen(ff_mkv_codec_tags[j].str))){
                codec_id= ff_mkv_codec_tags[j].id;
                break;
            }
        }

        st = track->stream = av_new_stream(s, 0);
        if (st == NULL)
            return AVERROR(ENOMEM);

        if (!strcmp(track->codec_id, "V_MS/VFW/FOURCC")
            && track->codec_priv.size >= 40
            && track->codec_priv.data != NULL) {
            track->ms_compat = 1;
            track->video.fourcc = AV_RL32(track->codec_priv.data + 16);
            codec_id = ff_codec_get_id(ff_codec_bmp_tags, track->video.fourcc);
            extradata_offset = 40;
        } else if (!strcmp(track->codec_id, "A_MS/ACM")
                   && track->codec_priv.size >= 14
                   && track->codec_priv.data != NULL) {
            init_put_byte(&b, track->codec_priv.data, track->codec_priv.size,
                          URL_RDONLY, NULL, NULL, NULL, NULL);
            ff_get_wav_header(&b, st->codec, track->codec_priv.size);
            codec_id = st->codec->codec_id;
            extradata_offset = FFMIN(track->codec_priv.size, 18);
        } else if (!strcmp(track->codec_id, "V_QUICKTIME")
                   && (track->codec_priv.size >= 86)
                   && (track->codec_priv.data != NULL)) {
            track->video.fourcc = AV_RL32(track->codec_priv.data);
            codec_id=ff_codec_get_id(codec_movvideo_tags, track->video.fourcc);
        } else if (codec_id == CODEC_ID_PCM_S16BE) {
            switch (track->audio.bitdepth) {
            case  8:  codec_id = CODEC_ID_PCM_U8;     break;
            case 24:  codec_id = CODEC_ID_PCM_S24BE;  break;
            case 32:  codec_id = CODEC_ID_PCM_S32BE;  break;
            }
        } else if (codec_id == CODEC_ID_PCM_S16LE) {
            switch (track->audio.bitdepth) {
            case  8:  codec_id = CODEC_ID_PCM_U8;     break;
            case 24:  codec_id = CODEC_ID_PCM_S24LE;  break;
            case 32:  codec_id = CODEC_ID_PCM_S32LE;  break;
            }
        } else if (codec_id==CODEC_ID_PCM_F32LE && track->audio.bitdepth==64) {
            codec_id = CODEC_ID_PCM_F64LE;
        } else if (codec_id == CODEC_ID_AAC && !track->codec_priv.size) {
            int profile = matroska_aac_profile(track->codec_id);
            int sri = matroska_aac_sri(track->audio.samplerate);
            extradata = av_malloc(5);
            if (extradata == NULL)
                return AVERROR(ENOMEM);
            extradata[0] = (profile << 3) | ((sri&0x0E) >> 1);
            extradata[1] = ((sri&0x01) << 7) | (track->audio.channels<<3);
            if (strstr(track->codec_id, "SBR")) {
                sri = matroska_aac_sri(track->audio.out_samplerate);
                extradata[2] = 0x56;
                extradata[3] = 0xE5;
                extradata[4] = 0x80 | (sri<<3);
                extradata_size = 5;
            } else
                extradata_size = 2;
        } else if (codec_id == CODEC_ID_TTA) {
            extradata_size = 30;
            extradata = av_mallocz(extradata_size);
            if (extradata == NULL)
                return AVERROR(ENOMEM);
            init_put_byte(&b, extradata, extradata_size, 1,
                          NULL, NULL, NULL, NULL);
            put_buffer(&b, "TTA1", 4);
            put_le16(&b, 1);
            put_le16(&b, track->audio.channels);
            put_le16(&b, track->audio.bitdepth);
            put_le32(&b, track->audio.out_samplerate);
            put_le32(&b, matroska->ctx->duration * track->audio.out_samplerate);
        } else if (codec_id == CODEC_ID_RV10 || codec_id == CODEC_ID_RV20 ||
                   codec_id == CODEC_ID_RV30 || codec_id == CODEC_ID_RV40) {
            extradata_offset = 26;
        } else if (codec_id == CODEC_ID_RA_144) {
            track->audio.out_samplerate = 8000;
            track->audio.channels = 1;
        } else if (codec_id == CODEC_ID_RA_288 || codec_id == CODEC_ID_COOK ||
                   codec_id == CODEC_ID_ATRAC3 || codec_id == CODEC_ID_SIPR) {
            int flavor;
            init_put_byte(&b, track->codec_priv.data,track->codec_priv.size,
                          0, NULL, NULL, NULL, NULL);
            url_fskip(&b, 22);
            flavor                       = get_be16(&b);
            track->audio.coded_framesize = get_be32(&b);
            url_fskip(&b, 12);
            track->audio.sub_packet_h    = get_be16(&b);
            track->audio.frame_size      = get_be16(&b);
            track->audio.sub_packet_size = get_be16(&b);
            track->audio.buf = av_malloc(track->audio.frame_size * track->audio.sub_packet_h);
            if (codec_id == CODEC_ID_RA_288) {
                st->codec->block_align = track->audio.coded_framesize;
                track->codec_priv.size = 0;
            } else {
                if (codec_id == CODEC_ID_SIPR && flavor < 4) {
                    const int sipr_bit_rate[4] = { 6504, 8496, 5000, 16000 };
                    track->audio.sub_packet_size = ff_sipr_subpk_size[flavor];
                    st->codec->bit_rate = sipr_bit_rate[flavor];
                }
                st->codec->block_align = track->audio.sub_packet_size;
                extradata_offset = 78;
            }
        }
        track->codec_priv.size -= extradata_offset;

        if (codec_id == CODEC_ID_NONE)
            av_log(matroska->ctx, AV_LOG_INFO,
                   "Unknown/unsupported CodecID %s.\n", track->codec_id);

        if (track->time_scale < 0.01)
            track->time_scale = 1.0;
        av_set_pts_info(st, 64, matroska->time_scale*track->time_scale, 1000*1000*1000); /* 64 bit pts in ns */

        st->codec->codec_id = codec_id;
        st->start_time = 0;
        if (strcmp(track->language, "und"))
            av_metadata_set2(&st->metadata, "language", track->language, 0);
        av_metadata_set2(&st->metadata, "title", track->name, 0);

        if (track->flag_default)
            st->disposition |= AV_DISPOSITION_DEFAULT;

        if (track->default_duration)
            av_reduce(&st->codec->time_base.num, &st->codec->time_base.den,
                      track->default_duration, 1000000000, 30000);

        if (!st->codec->extradata) {
            if(extradata){
                st->codec->extradata = extradata;
                st->codec->extradata_size = extradata_size;
            } else if(track->codec_priv.data && track->codec_priv.size > 0){
                st->codec->extradata = av_mallocz(track->codec_priv.size +
                                                  FF_INPUT_BUFFER_PADDING_SIZE);
                if(st->codec->extradata == NULL)
                    return AVERROR(ENOMEM);
                st->codec->extradata_size = track->codec_priv.size;
                memcpy(st->codec->extradata,
                       track->codec_priv.data + extradata_offset,
                       track->codec_priv.size);
            }
        }

        if (track->type == MATROSKA_TRACK_TYPE_VIDEO) {
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_tag  = track->video.fourcc;
            st->codec->width  = track->video.pixel_width;
            st->codec->height = track->video.pixel_height;
            av_reduce(&st->sample_aspect_ratio.num,
                      &st->sample_aspect_ratio.den,
                      st->codec->height * track->video.display_width,
                      st->codec-> width * track->video.display_height,
                      255);
            if (st->codec->codec_id != CODEC_ID_H264)
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
            if (track->default_duration)
                st->avg_frame_rate = av_d2q(1000000000.0/track->default_duration, INT_MAX);
        } else if (track->type == MATROSKA_TRACK_TYPE_AUDIO) {
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->sample_rate = track->audio.out_samplerate;
            st->codec->channels = track->audio.channels;
            if (st->codec->codec_id != CODEC_ID_AAC)
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
        } else if (track->type == MATROSKA_TRACK_TYPE_SUBTITLE) {
            st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
        }
    }

    attachements = attachements_list->elem;
    for (j=0; j<attachements_list->nb_elem; j++) {
        if (!(attachements[j].filename && attachements[j].mime &&
              attachements[j].bin.data && attachements[j].bin.size > 0)) {
            av_log(matroska->ctx, AV_LOG_ERROR, "incomplete attachment\n");
        } else {
            AVStream *st = av_new_stream(s, 0);
            if (st == NULL)
                break;
            av_metadata_set2(&st->metadata, "filename",attachements[j].filename, 0);
            st->codec->codec_id = CODEC_ID_NONE;
            st->codec->codec_type = AVMEDIA_TYPE_ATTACHMENT;
            st->codec->extradata  = av_malloc(attachements[j].bin.size);
            if(st->codec->extradata == NULL)
                break;
            st->codec->extradata_size = attachements[j].bin.size;
            memcpy(st->codec->extradata, attachements[j].bin.data, attachements[j].bin.size);

            for (i=0; ff_mkv_mime_tags[i].id != CODEC_ID_NONE; i++) {
                if (!strncmp(ff_mkv_mime_tags[i].str, attachements[j].mime,
                             strlen(ff_mkv_mime_tags[i].str))) {
                    st->codec->codec_id = ff_mkv_mime_tags[i].id;
                    break;
                }
            }
            attachements[j].stream = st;
        }
    }

    chapters = chapters_list->elem;
    for (i=0; i<chapters_list->nb_elem; i++)
        if (chapters[i].start != AV_NOPTS_VALUE && chapters[i].uid
            && (max_start==0 || chapters[i].start > max_start)) {
            chapters[i].chapter =
            ff_new_chapter(s, chapters[i].uid, (AVRational){1, 1000000000},
                           chapters[i].start, chapters[i].end,
                           chapters[i].title);
            av_metadata_set2(&chapters[i].chapter->metadata,
                             "title", chapters[i].title, 0);
            max_start = chapters[i].start;
        }

    index_list = &matroska->index;
    index = index_list->elem;
    if (index_list->nb_elem
        && index[0].time > 100000000000000/matroska->time_scale) {
        av_log(matroska->ctx, AV_LOG_WARNING, "Working around broken index.\n");
        index_scale = matroska->time_scale;
    }
    for (i=0; i<index_list->nb_elem; i++) {
        EbmlList *pos_list = &index[i].pos;
        MatroskaIndexPos *pos = pos_list->elem;
        for (j=0; j<pos_list->nb_elem; j++) {
            MatroskaTrack *track = matroska_find_track_by_num(matroska,
                                                              pos[j].track);
            if (track && track->stream)
                av_add_index_entry(track->stream,
                                   pos[j].pos + matroska->segment_start,
                                   index[i].time/index_scale, 0, 0,
                                   AVINDEX_KEYFRAME);
        }
    }

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
    if (matroska->num_packets > 0) {
        memcpy(pkt, matroska->packets[0], sizeof(AVPacket));
        av_free(matroska->packets[0]);
        if (matroska->num_packets > 1) {
            memmove(&matroska->packets[0], &matroska->packets[1],
                    (matroska->num_packets - 1) * sizeof(AVPacket *));
            matroska->packets =
                av_realloc(matroska->packets, (matroska->num_packets - 1) *
                           sizeof(AVPacket *));
        } else {
            av_freep(&matroska->packets);
        }
        matroska->num_packets--;
        return 0;
    }

    return -1;
}

/*
 * Free all packets in our internal queue.
 */
static void matroska_clear_queue(MatroskaDemuxContext *matroska)
{
    if (matroska->packets) {
        int n;
        for (n = 0; n < matroska->num_packets; n++) {
            av_free_packet(matroska->packets[n]);
            av_free(matroska->packets[n]);
        }
        av_freep(&matroska->packets);
        matroska->num_packets = 0;
    }
}

static int matroska_parse_block(MatroskaDemuxContext *matroska, uint8_t *data,
                                int size, int64_t pos, uint64_t cluster_time,
                                uint64_t duration, int is_keyframe,
                                int64_t cluster_pos)
{
    uint64_t timecode = AV_NOPTS_VALUE;
    MatroskaTrack *track;
    int res = 0;
    AVStream *st;
    AVPacket *pkt;
    int16_t block_time;
    uint32_t *lace_size = NULL;
    int n, flags, laces = 0;
    uint64_t num;

    if ((n = matroska_ebmlnum_uint(matroska, data, size, &num)) < 0) {
        av_log(matroska->ctx, AV_LOG_ERROR, "EBML block data error\n");
        return res;
    }
    data += n;
    size -= n;

    track = matroska_find_track_by_num(matroska, num);
    if (size <= 3 || !track || !track->stream) {
        av_log(matroska->ctx, AV_LOG_INFO,
               "Invalid stream %"PRIu64" or size %u\n", num, size);
        return res;
    }
    st = track->stream;
    if (st->discard >= AVDISCARD_ALL)
        return res;
    if (duration == AV_NOPTS_VALUE)
        duration = track->default_duration / matroska->time_scale;

    block_time = AV_RB16(data);
    data += 2;
    flags = *data++;
    size -= 3;
    if (is_keyframe == -1)
        is_keyframe = flags & 0x80 ? AV_PKT_FLAG_KEY : 0;

    if (cluster_time != (uint64_t)-1
        && (block_time >= 0 || cluster_time >= -block_time)) {
        timecode = cluster_time + block_time;
        if (track->type == MATROSKA_TRACK_TYPE_SUBTITLE
            && timecode < track->end_timecode)
            is_keyframe = 0;  /* overlapping subtitles are not key frame */
        if (is_keyframe)
            av_add_index_entry(st, cluster_pos, timecode, 0,0,AVINDEX_KEYFRAME);
        track->end_timecode = FFMAX(track->end_timecode, timecode+duration);
    }

    if (matroska->skip_to_keyframe && track->type != MATROSKA_TRACK_TYPE_SUBTITLE) {
        if (!is_keyframe || timecode < matroska->skip_to_timecode)
            return res;
        matroska->skip_to_keyframe = 0;
    }

    switch ((flags & 0x06) >> 1) {
        case 0x0: /* no lacing */
            laces = 1;
            lace_size = av_mallocz(sizeof(int));
            lace_size[0] = size;
            break;

        case 0x1: /* Xiph lacing */
        case 0x2: /* fixed-size lacing */
        case 0x3: /* EBML lacing */
            assert(size>0); // size <=3 is checked before size-=3 above
            laces = (*data) + 1;
            data += 1;
            size -= 1;
            lace_size = av_mallocz(laces * sizeof(int));

            switch ((flags & 0x06) >> 1) {
                case 0x1: /* Xiph lacing */ {
                    uint8_t temp;
                    uint32_t total = 0;
                    for (n = 0; res == 0 && n < laces - 1; n++) {
                        while (1) {
                            if (size == 0) {
                                res = -1;
                                break;
                            }
                            temp = *data;
                            lace_size[n] += temp;
                            data += 1;
                            size -= 1;
                            if (temp != 0xff)
                                break;
                        }
                        total += lace_size[n];
                    }
                    lace_size[n] = size - total;
                    break;
                }

                case 0x2: /* fixed-size lacing */
                    for (n = 0; n < laces; n++)
                        lace_size[n] = size / laces;
                    break;

                case 0x3: /* EBML lacing */ {
                    uint32_t total;
                    n = matroska_ebmlnum_uint(matroska, data, size, &num);
                    if (n < 0) {
                        av_log(matroska->ctx, AV_LOG_INFO,
                               "EBML block data error\n");
                        break;
                    }
                    data += n;
                    size -= n;
                    total = lace_size[0] = num;
                    for (n = 1; res == 0 && n < laces - 1; n++) {
                        int64_t snum;
                        int r;
                        r = matroska_ebmlnum_sint(matroska, data, size, &snum);
                        if (r < 0) {
                            av_log(matroska->ctx, AV_LOG_INFO,
                                   "EBML block data error\n");
                            break;
                        }
                        data += r;
                        size -= r;
                        lace_size[n] = lace_size[n - 1] + snum;
                        total += lace_size[n];
                    }
                    lace_size[n] = size - total;
                    break;
                }
            }
            break;
    }

    if (res == 0) {
        for (n = 0; n < laces; n++) {
            if ((st->codec->codec_id == CODEC_ID_RA_288 ||
                 st->codec->codec_id == CODEC_ID_COOK ||
                 st->codec->codec_id == CODEC_ID_SIPR ||
                 st->codec->codec_id == CODEC_ID_ATRAC3) &&
                 st->codec->block_align && track->audio.sub_packet_size) {
                int a = st->codec->block_align;
                int sps = track->audio.sub_packet_size;
                int cfs = track->audio.coded_framesize;
                int h = track->audio.sub_packet_h;
                int y = track->audio.sub_packet_cnt;
                int w = track->audio.frame_size;
                int x;

                if (!track->audio.pkt_cnt) {
                    if (st->codec->codec_id == CODEC_ID_RA_288)
                        for (x=0; x<h/2; x++)
                            memcpy(track->audio.buf+x*2*w+y*cfs,
                                   data+x*cfs, cfs);
                    else if (st->codec->codec_id == CODEC_ID_SIPR)
                        memcpy(track->audio.buf + y*w, data, w);
                    else
                        for (x=0; x<w/sps; x++)
                            memcpy(track->audio.buf+sps*(h*x+((h+1)/2)*(y&1)+(y>>1)), data+x*sps, sps);

                    if (++track->audio.sub_packet_cnt >= h) {
                        if (st->codec->codec_id == CODEC_ID_SIPR)
                            ff_rm_reorder_sipr_data(track->audio.buf, h, w);
                        track->audio.sub_packet_cnt = 0;
                        track->audio.pkt_cnt = h*w / a;
                    }
                }
                while (track->audio.pkt_cnt) {
                    pkt = av_mallocz(sizeof(AVPacket));
                    av_new_packet(pkt, a);
                    memcpy(pkt->data, track->audio.buf
                           + a * (h*w / a - track->audio.pkt_cnt--), a);
                    pkt->pos = pos;
                    pkt->stream_index = st->index;
                    dynarray_add(&matroska->packets,&matroska->num_packets,pkt);
                }
            } else {
                MatroskaTrackEncoding *encodings = track->encodings.elem;
                int offset = 0, pkt_size = lace_size[n];
                uint8_t *pkt_data = data;

                if (lace_size[n] > size) {
                    av_log(matroska->ctx, AV_LOG_ERROR, "Invalid packet size\n");
                    break;
                }

                if (encodings && encodings->scope & 1) {
                    offset = matroska_decode_buffer(&pkt_data,&pkt_size, track);
                    if (offset < 0)
                        continue;
                }

                pkt = av_mallocz(sizeof(AVPacket));
                /* XXX: prevent data copy... */
                if (av_new_packet(pkt, pkt_size+offset) < 0) {
                    av_free(pkt);
                    res = AVERROR(ENOMEM);
                    break;
                }
                if (offset)
                    memcpy (pkt->data, encodings->compression.settings.data, offset);
                memcpy (pkt->data+offset, pkt_data, pkt_size);

                if (pkt_data != data)
                    av_free(pkt_data);

                if (n == 0)
                    pkt->flags = is_keyframe;
                pkt->stream_index = st->index;

                if (track->ms_compat)
                    pkt->dts = timecode;
                else
                    pkt->pts = timecode;
                pkt->pos = pos;
                if (st->codec->codec_id == CODEC_ID_TEXT)
                    pkt->convergence_duration = duration;
                else if (track->type != MATROSKA_TRACK_TYPE_SUBTITLE)
                    pkt->duration = duration;

                if (st->codec->codec_id == CODEC_ID_SSA)
                    matroska_fix_ass_packet(matroska, pkt, duration);

                if (matroska->prev_pkt &&
                    timecode != AV_NOPTS_VALUE &&
                    matroska->prev_pkt->pts == timecode &&
                    matroska->prev_pkt->stream_index == st->index)
                    matroska_merge_packets(matroska->prev_pkt, pkt);
                else {
                    dynarray_add(&matroska->packets,&matroska->num_packets,pkt);
                    matroska->prev_pkt = pkt;
                }
            }

            if (timecode != AV_NOPTS_VALUE)
                timecode = duration ? timecode + duration : AV_NOPTS_VALUE;
            data += lace_size[n];
            size -= lace_size[n];
        }
    }

    av_free(lace_size);
    return res;
}

static int matroska_parse_cluster(MatroskaDemuxContext *matroska)
{
    MatroskaCluster cluster = { 0 };
    EbmlList *blocks_list;
    MatroskaBlock *blocks;
    int i, res;
    int64_t pos = url_ftell(matroska->ctx->pb);
    matroska->prev_pkt = NULL;
    if (matroska->has_cluster_id){
        /* For the first cluster we parse, its ID was already read as
           part of matroska_read_header(), so don't read it again */
        res = ebml_parse_id(matroska, matroska_clusters,
                            MATROSKA_ID_CLUSTER, &cluster);
        pos -= 4;  /* sizeof the ID which was already read */
        matroska->has_cluster_id = 0;
    } else
        res = ebml_parse(matroska, matroska_clusters, &cluster);
    blocks_list = &cluster.blocks;
    blocks = blocks_list->elem;
    for (i=0; i<blocks_list->nb_elem; i++)
        if (blocks[i].bin.size > 0) {
            int is_keyframe = blocks[i].non_simple ? !blocks[i].reference : -1;
            res=matroska_parse_block(matroska,
                                     blocks[i].bin.data, blocks[i].bin.size,
                                     blocks[i].bin.pos,  cluster.timecode,
                                     blocks[i].duration, is_keyframe,
                                     pos);
        }
    ebml_free(matroska_cluster, &cluster);
    if (res < 0)  matroska->done = 1;
    return res;
}

static int matroska_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MatroskaDemuxContext *matroska = s->priv_data;

    while (matroska_deliver_packet(matroska, pkt)) {
        if (matroska->done)
            return AVERROR_EOF;
        matroska_parse_cluster(matroska);
    }

    return 0;
}

static int matroska_read_seek(AVFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTrack *tracks = matroska->tracks.elem;
    AVStream *st = s->streams[stream_index];
    int i, index, index_sub, index_min;

    if (!st->nb_index_entries)
        return 0;
    timestamp = FFMAX(timestamp, st->index_entries[0].timestamp);

    if ((index = av_index_search_timestamp(st, timestamp, flags)) < 0) {
        url_fseek(s->pb, st->index_entries[st->nb_index_entries-1].pos, SEEK_SET);
        while ((index = av_index_search_timestamp(st, timestamp, flags)) < 0) {
            matroska_clear_queue(matroska);
            if (matroska_parse_cluster(matroska) < 0)
                break;
        }
    }

    matroska_clear_queue(matroska);
    if (index < 0)
        return 0;

    index_min = index;
    for (i=0; i < matroska->tracks.nb_elem; i++) {
        tracks[i].end_timecode = 0;
        if (tracks[i].type == MATROSKA_TRACK_TYPE_SUBTITLE
            && !tracks[i].stream->discard != AVDISCARD_ALL) {
            index_sub = av_index_search_timestamp(tracks[i].stream, st->index_entries[index].timestamp, AVSEEK_FLAG_BACKWARD);
            if (index_sub >= 0
                && st->index_entries[index_sub].pos < st->index_entries[index_min].pos
                && st->index_entries[index].timestamp - st->index_entries[index_sub].timestamp < 30000000000/matroska->time_scale)
                index_min = index_sub;
        }
    }

    url_fseek(s->pb, st->index_entries[index_min].pos, SEEK_SET);
    matroska->skip_to_keyframe = !(flags & AVSEEK_FLAG_ANY);
    matroska->skip_to_timecode = st->index_entries[index].timestamp;
    matroska->done = 0;
    av_update_cur_dts(s, st, st->index_entries[index].timestamp);
    return 0;
}

static int matroska_read_close(AVFormatContext *s)
{
    MatroskaDemuxContext *matroska = s->priv_data;
    MatroskaTrack *tracks = matroska->tracks.elem;
    int n;

    matroska_clear_queue(matroska);

    for (n=0; n < matroska->tracks.nb_elem; n++)
        if (tracks[n].type == MATROSKA_TRACK_TYPE_AUDIO)
            av_free(tracks[n].audio.buf);
    ebml_free(matroska_segment, matroska);

    return 0;
}

AVInputFormat matroska_demuxer = {
    "matroska",
    NULL_IF_CONFIG_SMALL("Matroska file format"),
    sizeof(MatroskaDemuxContext),
    matroska_probe,
    matroska_read_header,
    matroska_read_packet,
    matroska_read_close,
    matroska_read_seek,
    .metadata_conv = ff_mkv_metadata_conv,
};
