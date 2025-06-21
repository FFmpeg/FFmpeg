/*
 * Copyright (c) 2008 Jaikrishnan Menon <realityman@gmx.net>
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
 * Copyright (c) 2010 Sebastian Vater <cdgs.basty@googlemail.com>
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
 * IFF file demuxer
 * by Jaikrishnan Menon
 * for more information on the .iff file format, visit:
 * http://wiki.multimedia.cx/index.php?title=IFF
 */

#include <inttypes.h>

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/mem.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "demux.h"
#include "id3v2.h"
#include "internal.h"

#define ID_8SVX       MKTAG('8','S','V','X')
#define ID_16SV       MKTAG('1','6','S','V')
#define ID_MAUD       MKTAG('M','A','U','D')
#define ID_MHDR       MKTAG('M','H','D','R')
#define ID_MDAT       MKTAG('M','D','A','T')
#define ID_VHDR       MKTAG('V','H','D','R')
#define ID_ATAK       MKTAG('A','T','A','K')
#define ID_RLSE       MKTAG('R','L','S','E')
#define ID_CHAN       MKTAG('C','H','A','N')
#define ID_PBM        MKTAG('P','B','M',' ')
#define ID_ILBM       MKTAG('I','L','B','M')
#define ID_BMHD       MKTAG('B','M','H','D')
#define ID_DGBL       MKTAG('D','G','B','L')
#define ID_CAMG       MKTAG('C','A','M','G')
#define ID_CMAP       MKTAG('C','M','A','P')
#define ID_ACBM       MKTAG('A','C','B','M')
#define ID_DEEP       MKTAG('D','E','E','P')
#define ID_RGB8       MKTAG('R','G','B','8')
#define ID_RGBN       MKTAG('R','G','B','N')
#define ID_DSD        MKTAG('D','S','D',' ')
#define ID_DST        MKTAG('D','S','T',' ')
#define ID_DSTC       MKTAG('D','S','T','C')
#define ID_DSTF       MKTAG('D','S','T','F')
#define ID_FRTE       MKTAG('F','R','T','E')
#define ID_ANIM       MKTAG('A','N','I','M')
#define ID_ANHD       MKTAG('A','N','H','D')
#define ID_DLTA       MKTAG('D','L','T','A')
#define ID_DPAN       MKTAG('D','P','A','N')

#define ID_FORM       MKTAG('F','O','R','M')
#define ID_FRM8       MKTAG('F','R','M','8')
#define ID_ANNO       MKTAG('A','N','N','O')
#define ID_AUTH       MKTAG('A','U','T','H')
#define ID_CHRS       MKTAG('C','H','R','S')
#define ID_COPYRIGHT  MKTAG('(','c',')',' ')
#define ID_CSET       MKTAG('C','S','E','T')
#define ID_FVER       MKTAG('F','V','E','R')
#define ID_NAME       MKTAG('N','A','M','E')
#define ID_TEXT       MKTAG('T','E','X','T')
#define ID_ABIT       MKTAG('A','B','I','T')
#define ID_BODY       MKTAG('B','O','D','Y')
#define ID_DBOD       MKTAG('D','B','O','D')
#define ID_DPEL       MKTAG('D','P','E','L')
#define ID_DLOC       MKTAG('D','L','O','C')
#define ID_TVDC       MKTAG('T','V','D','C')

#define LEFT    2
#define RIGHT   4
#define STEREO  6

/**
 * This number of bytes if added at the beginning of each AVPacket
 * which contain additional information about video properties
 * which has to be shared between demuxer and decoder.
 * This number may change between frames, e.g. the demuxer might
 * set it to smallest possible size of 2 to indicate that there's
 * no extradata changing in this frame.
 */
#define IFF_EXTRA_VIDEO_SIZE 41

typedef enum {
    COMP_NONE,
    COMP_FIB,
    COMP_EXP
} svx8_compression_type;

typedef struct IffDemuxContext {
    int      is_64bit;  ///< chunk size is 64-bit
    int64_t  body_pos;
    int64_t  body_end;
    int64_t  sbdy_pos;
    int64_t  resume_pos;
    uint32_t  body_size;
    svx8_compression_type   svx8_compression;
    unsigned  maud_bits;
    unsigned  maud_compression;
    unsigned  bitmap_compression;  ///< delta compression method used
    unsigned  bpp;          ///< bits per plane to decode (differs from bits_per_coded_sample if HAM)
    unsigned  ham;          ///< 0 if non-HAM or number of hold bits (6 for bpp > 6, 4 otherwise)
    unsigned  flags;        ///< 1 for EHB, 0 is no extra half darkening
    unsigned  transparency; ///< transparency color index in palette
    unsigned  masking;      ///< masking method used
    uint8_t   tvdc[32];     ///< TVDC lookup table
    uint32_t  form_tag;
    int       audio_stream_index;
    int       video_stream_index;
} IffDemuxContext;

/* Metadata string read */
static int get_metadata(AVFormatContext *s,
                        const char *const tag,
                        const unsigned data_size)
{
    uint8_t *buf = ((data_size + 1) == 0) ? NULL : av_malloc(data_size + 1);

    if (!buf)
        return AVERROR(ENOMEM);

    if (avio_read(s->pb, buf, data_size) != data_size) {
        av_free(buf);
        return AVERROR(EIO);
    }
    buf[data_size] = 0;
    av_dict_set(&s->metadata, tag, buf, AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int iff_probe(const AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if ( (AV_RL32(d)   == ID_FORM &&
         (AV_RL32(d+8) == ID_8SVX ||
          AV_RL32(d+8) == ID_16SV ||
          AV_RL32(d+8) == ID_MAUD ||
          AV_RL32(d+8) == ID_PBM  ||
          AV_RL32(d+8) == ID_ACBM ||
          AV_RL32(d+8) == ID_DEEP ||
          AV_RL32(d+8) == ID_ILBM ||
          AV_RL32(d+8) == ID_RGB8 ||
          AV_RL32(d+8) == ID_ANIM ||
          AV_RL32(d+8) == ID_RGBN)) ||
         (AV_RL32(d) == ID_FRM8 && AV_RL32(d+12) == ID_DSD))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static const AVCodecTag dsd_codec_tags[] = {
    { AV_CODEC_ID_DSD_MSBF, ID_DSD },
    { AV_CODEC_ID_DST,      ID_DST },
    { AV_CODEC_ID_NONE, 0 },
};


#define DSD_SLFT MKTAG('S','L','F','T')
#define DSD_SRGT MKTAG('S','R','G','T')
#define DSD_MLFT MKTAG('M','L','F','T')
#define DSD_MRGT MKTAG('M','R','G','T')
#define DSD_C    MKTAG('C',' ',' ',' ')
#define DSD_LS   MKTAG('L','S',' ',' ')
#define DSD_RS   MKTAG('R','S',' ',' ')
#define DSD_LFE  MKTAG('L','F','E',' ')

static const uint32_t dsd_stereo[]  = { DSD_SLFT, DSD_SRGT };
static const uint32_t dsd_5point0[] = { DSD_MLFT, DSD_MRGT, DSD_C, DSD_LS, DSD_RS };
static const uint32_t dsd_5point1[] = { DSD_MLFT, DSD_MRGT, DSD_C, DSD_LFE, DSD_LS, DSD_RS };

typedef struct {
    AVChannelLayout layout;
    const uint32_t * dsd_layout;
} DSDLayoutDesc;

static const DSDLayoutDesc dsd_channel_layout[] = {
    { AV_CHANNEL_LAYOUT_STEREO,  dsd_stereo },
    { AV_CHANNEL_LAYOUT_5POINT0, dsd_5point0 },
    { AV_CHANNEL_LAYOUT_5POINT1, dsd_5point1 },
};

static const AVChannelLayout dsd_loudspeaker_config[] = {
    AV_CHANNEL_LAYOUT_STEREO,
    { 0 }, { 0 },
    AV_CHANNEL_LAYOUT_5POINT0, AV_CHANNEL_LAYOUT_5POINT1,
};

static const char * dsd_source_comment[] = {
    "dsd_source_comment",
    "analogue_source_comment",
    "pcm_source_comment",
};

static const char * dsd_history_comment[] = {
    "general_remark",
    "operator_name",
    "creating_machine",
    "timezone",
    "file_revision"
};

static int parse_dsd_diin(AVFormatContext *s, AVStream *st, uint64_t eof)
{
    AVIOContext *pb = s->pb;

    while (av_sat_add64(avio_tell(pb), 12) <= eof && !avio_feof(pb)) {
        uint32_t tag      = avio_rl32(pb);
        uint64_t size     = avio_rb64(pb);
        uint64_t orig_pos = avio_tell(pb);
        const char * metadata_tag = NULL;

        if (size >= INT64_MAX)
            return AVERROR_INVALIDDATA;

        switch(tag) {
        case MKTAG('D','I','A','R'): metadata_tag = "artist"; break;
        case MKTAG('D','I','T','I'): metadata_tag = "title";  break;
        }

        if (metadata_tag && size > 4) {
            unsigned int tag_size = avio_rb32(pb);
            int ret = get_metadata(s, metadata_tag, FFMIN(tag_size, size - 4));
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", metadata_tag);
                return ret;
            }
        }

        avio_skip(pb, size - (avio_tell(pb) - orig_pos) + (size & 1));
    }

    return 0;
}

static int parse_dsd_prop(AVFormatContext *s, AVStream *st, uint64_t eof)
{
    AVIOContext *pb = s->pb;
    char abss[24];
    int hour, min, sec, i, ret, config;
    int dsd_layout[6];
    ID3v2ExtraMeta *id3v2_extra_meta;

    while (av_sat_add64(avio_tell(pb), 12) <= eof && !avio_feof(pb)) {
        uint32_t tag      = avio_rl32(pb);
        uint64_t size     = avio_rb64(pb);
        uint64_t orig_pos = avio_tell(pb);

        if (size >= INT64_MAX)
            return AVERROR_INVALIDDATA;

        switch(tag) {
        case MKTAG('A','B','S','S'):
            if (size < 8)
                return AVERROR_INVALIDDATA;
            hour = avio_rb16(pb);
            min  = avio_r8(pb);
            sec  = avio_r8(pb);
            snprintf(abss, sizeof(abss), "%02dh:%02dm:%02ds:%d", hour, min, sec, avio_rb32(pb));
            av_dict_set(&st->metadata, "absolute_start_time", abss, 0);
            break;

        case MKTAG('C','H','N','L'):
            if (size < 2)
                return AVERROR_INVALIDDATA;
            st->codecpar->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
            st->codecpar->ch_layout.nb_channels = avio_rb16(pb);
            if (size < 2 + st->codecpar->ch_layout.nb_channels * 4 || !st->codecpar->ch_layout.nb_channels)
                return AVERROR_INVALIDDATA;
            if (st->codecpar->ch_layout.nb_channels > FF_ARRAY_ELEMS(dsd_layout)) {
                avpriv_request_sample(s, "channel layout");
                break;
            }
            for (i = 0; i < st->codecpar->ch_layout.nb_channels; i++)
                dsd_layout[i] = avio_rl32(pb);
            for (i = 0; i < FF_ARRAY_ELEMS(dsd_channel_layout); i++) {
                const DSDLayoutDesc * d = &dsd_channel_layout[i];
                if (d->layout.nb_channels == st->codecpar->ch_layout.nb_channels &&
                    !memcmp(d->dsd_layout, dsd_layout, d->layout.nb_channels * sizeof(uint32_t))) {
                    st->codecpar->ch_layout = d->layout;
                    break;
                }
            }
            break;

        case MKTAG('C','M','P','R'):
            if (size < 4)
                return AVERROR_INVALIDDATA;
            st->codecpar->codec_tag = tag = avio_rl32(pb);
            st->codecpar->codec_id = ff_codec_get_id(dsd_codec_tags, tag);
            if (!st->codecpar->codec_id) {
                av_log(s, AV_LOG_ERROR, "'%s' compression is not supported\n",
                       av_fourcc2str(tag));
                return AVERROR_PATCHWELCOME;
            }
            break;

        case MKTAG('F','S',' ',' '):
            if (size < 4)
                return AVERROR_INVALIDDATA;
            st->codecpar->sample_rate = avio_rb32(pb) / 8;
            break;

        case MKTAG('I','D','3',' '):
            ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, size);
            if (id3v2_extra_meta) {
                if ((ret = ff_id3v2_parse_apic(s, id3v2_extra_meta)) < 0 ||
                    (ret = ff_id3v2_parse_chapters(s, id3v2_extra_meta)) < 0) {
                    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
                    return ret;
                }
                ff_id3v2_free_extra_meta(&id3v2_extra_meta);
            }

            if (size < avio_tell(pb) - orig_pos) {
                av_log(s, AV_LOG_ERROR, "id3 exceeds chunk size\n");
                return AVERROR_INVALIDDATA;
            }
            break;

        case MKTAG('L','S','C','O'):
            if (size < 2)
                return AVERROR_INVALIDDATA;
            config = avio_rb16(pb);
            if (config != 0xFFFF) {
                if (config < FF_ARRAY_ELEMS(dsd_loudspeaker_config))
                    st->codecpar->ch_layout = dsd_loudspeaker_config[config];
                if (!st->codecpar->ch_layout.nb_channels)
                    avpriv_request_sample(s, "loudspeaker configuration %d", config);
            }
            break;
        }

        avio_skip(pb, size - (avio_tell(pb) - orig_pos) + (size & 1));
    }

    return 0;
}

static int read_dst_frame(AVFormatContext *s, AVPacket *pkt)
{
    IffDemuxContext *iff = s->priv_data;
    AVIOContext *pb = s->pb;
    uint32_t chunk_id;
    uint64_t chunk_pos, data_pos, data_size;
    int ret = AVERROR_EOF;

    if (s->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    while (!avio_feof(pb)) {
        chunk_pos = avio_tell(pb);
        if (chunk_pos >= iff->body_end)
            return AVERROR_EOF;

        chunk_id = avio_rl32(pb);
        data_size = iff->is_64bit ? avio_rb64(pb) : avio_rb32(pb);
        data_pos = avio_tell(pb);

        if (data_size < 1 || data_size >= INT64_MAX)
            return AVERROR_INVALIDDATA;

        switch (chunk_id) {
        case ID_DSTF:
            if (!pkt) {
                iff->body_pos  = avio_tell(pb) - (iff->is_64bit ? 12 : 8);
                iff->body_size = iff->body_end - iff->body_pos;
                return 0;
            }
            ret = av_get_packet(pb, pkt, data_size);
            if (ret < 0)
                return ret;
            if (data_size & 1)
                avio_skip(pb, 1);
            pkt->flags |= AV_PKT_FLAG_KEY;
            pkt->stream_index = iff->audio_stream_index;
            pkt->duration = s->streams[0]->codecpar->sample_rate / 75;
            pkt->pos = chunk_pos;

            chunk_pos = avio_tell(pb);
            if (chunk_pos >= iff->body_end)
                return 0;

            avio_seek(pb, chunk_pos, SEEK_SET);
            return 0;

        case ID_FRTE:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            s->streams[0]->duration = avio_rb32(pb) * (uint64_t)s->streams[0]->codecpar->sample_rate / 75;

            break;
        }

        avio_skip(pb, data_size - (avio_tell(pb) - data_pos) + (data_size & 1));
    }

    return ret;
}

static const uint8_t deep_rgb24[] = {0, 0, 0, 3, 0, 1, 0, 8, 0, 2, 0, 8, 0, 3, 0, 8};
static const uint8_t deep_rgba[]  = {0, 0, 0, 4, 0, 1, 0, 8, 0, 2, 0, 8, 0, 3, 0, 8};
static const uint8_t deep_bgra[]  = {0, 0, 0, 4, 0, 3, 0, 8, 0, 2, 0, 8, 0, 1, 0, 8};
static const uint8_t deep_argb[]  = {0, 0, 0, 4, 0,17, 0, 8, 0, 1, 0, 8, 0, 2, 0, 8};
static const uint8_t deep_abgr[]  = {0, 0, 0, 4, 0,17, 0, 8, 0, 3, 0, 8, 0, 2, 0, 8};

static AVStream * new_stream(AVFormatContext *s, AVStream **st_ptr, int *index_ptr, enum AVMediaType codec_type)
{
    if (!*st_ptr) {
        *st_ptr = avformat_new_stream(s, NULL);
        if (!*st_ptr)
            return NULL;
        (*st_ptr)->codecpar->codec_type = codec_type;
        (*index_ptr) = (*st_ptr)->index;
     }
     return *st_ptr;
}

static int iff_read_header(AVFormatContext *s)
{
    IffDemuxContext *iff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *sta = NULL, *stv = NULL;
    uint8_t *buf;
    uint32_t chunk_id;
    uint64_t data_size;
    uint32_t screenmode = 0, num, den;
    unsigned transparency = 0;
    unsigned masking = 0; // no mask
    uint8_t fmt[16];
    int fmt_size;

    iff->audio_stream_index = -1;
    iff->video_stream_index = -1;
    iff->is_64bit = avio_rl32(pb) == ID_FRM8;
    avio_skip(pb, iff->is_64bit ? 8 : 4);
    iff->form_tag = avio_rl32(pb);
    if (iff->form_tag == ID_ANIM) {
        avio_skip(pb, 12);
    }
    iff->bitmap_compression = -1;
    iff->svx8_compression = -1;
    iff->maud_bits = -1;
    iff->maud_compression = -1;

    while(!avio_feof(pb)) {
        uint64_t orig_pos;
        int res;
        const char *metadata_tag = NULL;
        int version, nb_comments, i;
        chunk_id = avio_rl32(pb);
        data_size = iff->is_64bit ? avio_rb64(pb) : avio_rb32(pb);
        orig_pos = avio_tell(pb);

        if (data_size >= INT64_MAX)
            return AVERROR_INVALIDDATA;

        switch(chunk_id) {
        case ID_VHDR:
            if (data_size < 14)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &sta, &iff->audio_stream_index, AVMEDIA_TYPE_AUDIO))
                return AVERROR(ENOMEM);
            avio_skip(pb, 12);
            sta->codecpar->sample_rate = avio_rb16(pb);
            if (data_size >= 16) {
                avio_skip(pb, 1);
                iff->svx8_compression = avio_r8(pb);
            }
            sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
            break;

        case ID_MHDR:
            if (data_size < 32)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &sta, &iff->audio_stream_index, AVMEDIA_TYPE_AUDIO))
                return AVERROR(ENOMEM);
            avio_skip(pb, 4);
            iff->maud_bits = avio_rb16(pb);
            avio_skip(pb, 2);
            num = avio_rb32(pb);
            den = avio_rb16(pb);
            if (!den)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 2);
            sta->codecpar->sample_rate = num / den;
            sta->codecpar->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
            sta->codecpar->ch_layout.nb_channels = avio_rb16(pb);
            iff->maud_compression = avio_rb16(pb);
            if (sta->codecpar->ch_layout.nb_channels == 1)
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
            else if (sta->codecpar->ch_layout.nb_channels == 2)
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            else if (sta->codecpar->ch_layout.nb_channels == 0)
                return AVERROR_INVALIDDATA;
            break;

        case ID_ABIT:
        case ID_BODY:
        case ID_DBOD:
        case ID_DSD:
        case ID_DST:
        case ID_MDAT:
            iff->body_pos = avio_tell(pb);
            if (iff->body_pos < 0 || iff->body_pos + data_size > INT64_MAX)
                return AVERROR_INVALIDDATA;

            iff->body_end = iff->body_pos + data_size;
            iff->body_size = data_size;
            if (chunk_id == ID_DST) {
                int ret = read_dst_frame(s, NULL);
                if (ret < 0)
                    return ret;
            }
            break;

        case ID_CHAN:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            if (!sta)
                return AVERROR_INVALIDDATA;
            if (avio_rb32(pb) < 6) {
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
            } else {
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            }
            break;

        case ID_CAMG:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            screenmode                = avio_rb32(pb);
            break;

        case ID_CMAP:
            if (data_size < 3 || data_size > 768 || data_size % 3) {
                 av_log(s, AV_LOG_ERROR, "Invalid CMAP chunk size %"PRIu64"\n",
                        data_size);
                 return AVERROR_INVALIDDATA;
            }
            if (!stv)
                return AVERROR_INVALIDDATA;
            res = ff_alloc_extradata(stv->codecpar,
                                     data_size + IFF_EXTRA_VIDEO_SIZE);
            if (res < 0)
                return res;
            if (avio_read(pb, stv->codecpar->extradata + IFF_EXTRA_VIDEO_SIZE, data_size) < 0) {
                av_freep(&stv->codecpar->extradata);
                stv->codecpar->extradata_size = 0;
                return AVERROR(EIO);
            }
            break;

        case ID_BMHD:
            if (data_size <= 8)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &stv, &iff->video_stream_index, AVMEDIA_TYPE_VIDEO))
                return AVERROR(ENOMEM);
            stv->codecpar->width                 = avio_rb16(pb);
            stv->codecpar->height                = avio_rb16(pb);
            avio_skip(pb, 4); // x, y offset
            stv->codecpar->bits_per_coded_sample = avio_r8(pb);
            if (data_size >= 10)
                masking                      = avio_r8(pb);
            if (data_size >= 11)
                iff->bitmap_compression      = avio_r8(pb);
            if (data_size >= 14) {
                avio_skip(pb, 1); // padding
                transparency                 = avio_rb16(pb);
            }
            if (data_size >= 16) {
                stv->sample_aspect_ratio.num  = avio_r8(pb);
                stv->sample_aspect_ratio.den  = avio_r8(pb);
            }
            break;

        case ID_ANHD:
            break;

        case ID_DPAN:
            if (!stv)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 2);
            stv->duration = avio_rb16(pb);
            break;

        case ID_DPEL:
            if (data_size < 4 || (data_size & 3))
                return AVERROR_INVALIDDATA;
            if (!stv)
                return AVERROR_INVALIDDATA;
            if ((fmt_size = avio_read(pb, fmt, sizeof(fmt))) < 0)
                return fmt_size;
            if (fmt_size == sizeof(deep_rgb24) && !memcmp(fmt, deep_rgb24, sizeof(deep_rgb24)))
                stv->codecpar->format = AV_PIX_FMT_RGB24;
            else if (fmt_size == sizeof(deep_rgba) && !memcmp(fmt, deep_rgba, sizeof(deep_rgba)))
                stv->codecpar->format = AV_PIX_FMT_RGBA;
            else if (fmt_size == sizeof(deep_bgra) && !memcmp(fmt, deep_bgra, sizeof(deep_bgra)))
                stv->codecpar->format = AV_PIX_FMT_BGRA;
            else if (fmt_size == sizeof(deep_argb) && !memcmp(fmt, deep_argb, sizeof(deep_argb)))
                stv->codecpar->format = AV_PIX_FMT_ARGB;
            else if (fmt_size == sizeof(deep_abgr) && !memcmp(fmt, deep_abgr, sizeof(deep_abgr)))
                stv->codecpar->format = AV_PIX_FMT_ABGR;
            else {
                avpriv_request_sample(s, "color format %.16s", fmt);
                return AVERROR_PATCHWELCOME;
            }
            break;

        case ID_DGBL:
            if (data_size < 8)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &stv, &iff->video_stream_index, AVMEDIA_TYPE_VIDEO))
                return AVERROR(ENOMEM);
            stv->codecpar->width              = avio_rb16(pb);
            stv->codecpar->height             = avio_rb16(pb);
            iff->bitmap_compression          = avio_rb16(pb);
            stv->sample_aspect_ratio.num      = avio_r8(pb);
            stv->sample_aspect_ratio.den      = avio_r8(pb);
            stv->codecpar->bits_per_coded_sample = 24;
            break;

        case ID_DLOC:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &stv, &iff->video_stream_index, AVMEDIA_TYPE_VIDEO))
                return AVERROR(ENOMEM);
            stv->codecpar->width  = avio_rb16(pb);
            stv->codecpar->height = avio_rb16(pb);
            break;

        case ID_TVDC:
            if (data_size < sizeof(iff->tvdc))
                return AVERROR_INVALIDDATA;
            res = avio_read(pb, iff->tvdc, sizeof(iff->tvdc));
            if (res < 0)
                return res;
            break;

        case MKTAG('S','X','H','D'):
            if (data_size < 22)
                return AVERROR_INVALIDDATA;
            if (!new_stream(s, &sta, &iff->audio_stream_index, AVMEDIA_TYPE_AUDIO))
                return AVERROR(ENOMEM);
            sta->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            switch(avio_r8(pb)) {
            case 8:
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_S8_PLANAR;
                break;
            default:
                avpriv_request_sample(s, "sound bitdepth");
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, 9);
            if (avio_rb32(pb)) {
                avpriv_request_sample(s, "sound compression");
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, 1);
            sta->codecpar->ch_layout.nb_channels = avio_r8(pb);
            if (!sta->codecpar->ch_layout.nb_channels)
                return AVERROR_INVALIDDATA;
            if (sta->codecpar->ch_layout.nb_channels == 1)
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
            else if (sta->codecpar->ch_layout.nb_channels == 2)
                sta->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            sta->codecpar->sample_rate = avio_rb32(pb);
            avpriv_set_pts_info(sta, 64, 1, sta->codecpar->sample_rate);
            avio_skip(pb, 2);
            break;

        case ID_ANNO:
        case ID_TEXT:      metadata_tag = "comment";   break;
        case ID_AUTH:      metadata_tag = "artist";    break;
        case ID_COPYRIGHT: metadata_tag = "copyright"; break;
        case ID_NAME:      metadata_tag = "title";     break;

        /* DSD tags */

        case MKTAG('F','V','E','R'):
            if (iff->form_tag == ID_DSD || iff->form_tag == ID_DST) {
                if (data_size < 4)
                    return AVERROR_INVALIDDATA;
                version = avio_rb32(pb);
                av_log(s, AV_LOG_DEBUG, "DSIFF v%d.%d.%d.%d\n",version >> 24, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
                if (!new_stream(s, &sta, &iff->audio_stream_index, AVMEDIA_TYPE_AUDIO))
                    return AVERROR(ENOMEM);
                sta->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            }
            break;

        case MKTAG('D','I','I','N'):
            if (!sta)
                return AVERROR_INVALIDDATA;
            res = parse_dsd_diin(s, sta, orig_pos + data_size);
            if (res < 0)
                return res;
            break;

        case MKTAG('P','R','O','P'):
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            if (avio_rl32(pb) != MKTAG('S','N','D',' ')) {
                avpriv_request_sample(s, "unknown property type");
                break;
            }
            if (!sta)
                return AVERROR_INVALIDDATA;
            res = parse_dsd_prop(s, sta, orig_pos + data_size);
            if (res < 0)
                return res;
            break;

        case MKTAG('C','O','M','T'):
            if (data_size < 2)
                return AVERROR_INVALIDDATA;
            if (!sta)
                return AVERROR_INVALIDDATA;
            nb_comments = avio_rb16(pb);
            for (i = 0; i < nb_comments; i++) {
                int year, mon, day, hour, min, type, ref;
                char tmp[24];
                const char *tag;
                int metadata_size;

                year = avio_rb16(pb);
                mon  = avio_r8(pb);
                day  = avio_r8(pb);
                hour = avio_r8(pb);
                min  = avio_r8(pb);
                snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d %02d:%02d", year, mon, day, hour, min);
                av_dict_set(&sta->metadata, "comment_time", tmp, 0);

                type = avio_rb16(pb);
                ref  = avio_rb16(pb);
                switch (type) {
                case 1:
                    if (!i)
                        tag = "channel_comment";
                    else {
                        snprintf(tmp, sizeof(tmp), "channel%d_comment", ref);
                        tag = tmp;
                    }
                    break;
                case 2:
                    tag = ref < FF_ARRAY_ELEMS(dsd_source_comment) ? dsd_source_comment[ref] : "source_comment";
                    break;
                case 3:
                    tag = ref < FF_ARRAY_ELEMS(dsd_history_comment) ? dsd_history_comment[ref] : "file_history";
                    break;
                default:
                    tag = "comment";
                }

                metadata_size  = avio_rb32(pb);
                if ((res = get_metadata(s, tag, metadata_size)) < 0) {
                    av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", tag);
                    return res;
                }

                if (metadata_size & 1)
                    avio_skip(pb, 1);
            }
            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, metadata_tag, data_size)) < 0) {
                av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }
        avio_skip(pb, data_size - (avio_tell(pb) - orig_pos) + (data_size & 1));
    }

    if ((!sta && !stv) ||
        (iff->form_tag == ID_ANIM && !stv) ||
        (iff->form_tag != ID_ANIM && sta && stv))
        return AVERROR_INVALIDDATA;

    if (iff->form_tag == ID_ANIM)
        avio_seek(pb, 12, SEEK_SET);
    else
        avio_seek(pb, iff->body_pos, SEEK_SET);

    if (sta) {
        avpriv_set_pts_info(sta, 32, 1, sta->codecpar->sample_rate);

        if (sta->codecpar->codec_id != AV_CODEC_ID_NONE) {
            /* codec_id already set by PROP or SXHD chunk */
        } else if (iff->form_tag == ID_16SV)
            sta->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE_PLANAR;
        else if (iff->form_tag == ID_MAUD) {
            if (iff->maud_bits == 8 && !iff->maud_compression) {
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
            } else if (iff->maud_bits == 16 && !iff->maud_compression) {
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
            } else if (iff->maud_bits ==  8 && iff->maud_compression == 2) {
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
            } else if (iff->maud_bits ==  8 && iff->maud_compression == 3) {
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW;
            } else {
                avpriv_request_sample(s, "compression %d and bit depth %d", iff->maud_compression, iff->maud_bits);
                return AVERROR_PATCHWELCOME;
            }
        } else {
            switch (iff->svx8_compression) {
            case COMP_NONE:
                sta->codecpar->codec_id = AV_CODEC_ID_PCM_S8_PLANAR;
                break;
            case COMP_FIB:
                sta->codecpar->codec_id = AV_CODEC_ID_8SVX_FIB;
                break;
            case COMP_EXP:
                sta->codecpar->codec_id = AV_CODEC_ID_8SVX_EXP;
                break;
            default:
                av_log(s, AV_LOG_ERROR,
                       "Unknown SVX8 compression method '%d'\n", iff->svx8_compression);
                return -1;
            }
        }

        sta->codecpar->bits_per_coded_sample = av_get_bits_per_sample(sta->codecpar->codec_id);
        sta->codecpar->bit_rate    = (int64_t)sta->codecpar->ch_layout.nb_channels *
                                    sta->codecpar->sample_rate *
                                    sta->codecpar->bits_per_coded_sample;
        sta->codecpar->block_align = sta->codecpar->ch_layout.nb_channels *
                                    sta->codecpar->bits_per_coded_sample;
        if ((sta->codecpar->codec_tag == ID_DSD || iff->form_tag == ID_MAUD) && sta->codecpar->block_align <= 0)
            return AVERROR_INVALIDDATA;
    }

    if (stv) {
        iff->bpp          = stv->codecpar->bits_per_coded_sample;
        if (iff->form_tag == ID_ANIM)
            avpriv_set_pts_info(stv, 32, 1, 60);
        if ((screenmode & 0x800 /* Hold And Modify */) && iff->bpp <= 8) {
            iff->ham      = iff->bpp > 6 ? 6 : 4;
            stv->codecpar->bits_per_coded_sample = 24;
        }
        iff->flags        = (screenmode & 0x80 /* Extra HalfBrite */) && iff->bpp <= 8;
        iff->masking      = masking;
        iff->transparency = transparency;

        if (!stv->codecpar->extradata) {
            int ret = ff_alloc_extradata(stv->codecpar, IFF_EXTRA_VIDEO_SIZE);
            if (ret < 0)
                return ret;
        }
        av_assert0(stv->codecpar->extradata_size >= IFF_EXTRA_VIDEO_SIZE);
        buf = stv->codecpar->extradata;
        bytestream_put_be16(&buf, IFF_EXTRA_VIDEO_SIZE);
        bytestream_put_byte(&buf, iff->bitmap_compression);
        bytestream_put_byte(&buf, iff->bpp);
        bytestream_put_byte(&buf, iff->ham);
        bytestream_put_byte(&buf, iff->flags);
        bytestream_put_be16(&buf, iff->transparency);
        bytestream_put_byte(&buf, iff->masking);
        bytestream_put_buffer(&buf, iff->tvdc, sizeof(iff->tvdc));
        stv->codecpar->codec_id = AV_CODEC_ID_IFF_ILBM;
        stv->codecpar->codec_tag = iff->form_tag; // codec_tag used by ByteRun1 decoder to distinguish progressive (PBM) and interlaced (ILBM) content
    }

    return 0;
}

static unsigned get_anim_duration(uint8_t *buf, int size)
{
    GetByteContext gb;

    bytestream2_init(&gb, buf, size);
    bytestream2_skip(&gb, 4);
    while (bytestream2_get_bytes_left(&gb) > 8) {
        unsigned chunk = bytestream2_get_le32(&gb);
        unsigned size = bytestream2_get_be32(&gb);

        if (chunk == ID_ANHD) {
            if (size < 40)
                break;
            bytestream2_skip(&gb, 14);
            return bytestream2_get_be32(&gb);
        } else {
            bytestream2_skip(&gb, size + size & 1);
        }
    }
    return 10;
}

static int64_t get_sbdy_offset(uint8_t *buf, int size)
{
    GetByteContext gb;

    bytestream2_init(&gb, buf, size);
    bytestream2_skip(&gb, 4);
    while (bytestream2_get_bytes_left(&gb) > 8) {
        unsigned chunk = bytestream2_get_le32(&gb);
        unsigned size = bytestream2_get_be32(&gb);

        if (chunk == MKTAG('S','B','D','Y'))
            return bytestream2_tell(&gb);

        bytestream2_skip(&gb, size + size & 1);
    }

    return 0;
}

static int iff_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    IffDemuxContext *iff = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;
    int64_t pos = avio_tell(pb);

    if (avio_feof(pb))
        return AVERROR_EOF;
    if (iff->form_tag != ID_ANIM && pos >= iff->body_end)
        return AVERROR_EOF;

    if (iff->sbdy_pos) {
        int64_t data_size;
        avio_seek(pb, iff->sbdy_pos, SEEK_SET);
        data_size = iff->is_64bit ? avio_rb64(pb) : avio_rb32(pb);
        ret = av_get_packet(pb, pkt, data_size);
        pkt->stream_index = iff->audio_stream_index;
        pkt->duration = data_size / s->streams[iff->audio_stream_index]->codecpar->ch_layout.nb_channels;
        pkt->pos = INT_MAX; /* not seekable */

        iff->sbdy_pos = 0;
        avio_seek(pb, iff->resume_pos, SEEK_SET);
        return ret;
    }

    if (iff->audio_stream_index >= 0 && iff->video_stream_index < 0) { /* audio only */
        AVStream *sta = s->streams[iff->audio_stream_index];
        if (sta->codecpar->codec_tag == ID_DSD || iff->form_tag == ID_MAUD) {
            ret = av_get_packet(pb, pkt, FFMIN(iff->body_end - pos, 1024 * sta->codecpar->block_align));
        } else if (sta->codecpar->codec_tag == ID_DST) {
            return read_dst_frame(s, pkt);
        } else {
            if (iff->body_size > INT_MAX || !iff->body_size)
                return AVERROR_INVALIDDATA;
            ret = av_get_packet(pb, pkt, iff->body_size);
        }
        pkt->stream_index = iff->audio_stream_index;
    } else if (iff->form_tag == ID_ANIM) {
        uint64_t data_size, orig_pos;
        uint32_t chunk_id, chunk_id2;

        while (!avio_feof(pb)) {
            if (avio_feof(pb))
                return AVERROR_EOF;

            orig_pos  = avio_tell(pb);
            chunk_id  = avio_rl32(pb);
            data_size = avio_rb32(pb);
            chunk_id2 = avio_rl32(pb);

            if (chunk_id  == ID_FORM &&
                chunk_id2 == ID_ILBM) {
                avio_skip(pb, -4);
                break;
            } else if (chunk_id == ID_FORM &&
                       chunk_id2 == ID_ANIM) {
                continue;
            } else {
                avio_skip(pb, data_size);
            }
        }
        ret = av_get_packet(pb, pkt, data_size);
        pkt->stream_index = iff->video_stream_index;
        pkt->pos = orig_pos;
        pkt->duration = get_anim_duration(pkt->data, pkt->size);
        if (pos == 12)
            pkt->flags |= AV_PKT_FLAG_KEY;

        if (iff->audio_stream_index >= 0) {
            iff->sbdy_pos = get_sbdy_offset(pkt->data, pkt->size);
            if (iff->sbdy_pos) {
                iff->sbdy_pos += orig_pos + 4;
                iff->resume_pos = avio_tell(pb);
            }
        }
    } else if (iff->video_stream_index >= 0 && iff->audio_stream_index < 0) { /* video only */
        if (iff->body_size > INT_MAX || !iff->body_size)
            return AVERROR_INVALIDDATA;
        ret = av_get_packet(pb, pkt, iff->body_size);
        pkt->stream_index = iff->video_stream_index;
        pkt->pos = pos;
        if (pos == iff->body_pos)
            pkt->flags |= AV_PKT_FLAG_KEY;
    } else {
        av_assert0(0);
    }

    return ret;
}

const FFInputFormat ff_iff_demuxer = {
    .p.name         = "iff",
    .p.long_name    = NULL_IF_CONFIG_SMALL("IFF (Interchange File Format)"),
    .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK,
    .priv_data_size = sizeof(IffDemuxContext),
    .read_probe     = iff_probe,
    .read_header    = iff_read_header,
    .read_packet    = iff_read_packet,
};
