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

#include "libavcodec/bytestream.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"

#define ID_8SVX       MKTAG('8','S','V','X')
#define ID_VHDR       MKTAG('V','H','D','R')
#define ID_ATAK       MKTAG('A','T','A','K')
#define ID_RLSE       MKTAG('R','L','S','E')
#define ID_CHAN       MKTAG('C','H','A','N')
#define ID_PBM        MKTAG('P','B','M',' ')
#define ID_ILBM       MKTAG('I','L','B','M')
#define ID_BMHD       MKTAG('B','M','H','D')
#define ID_CAMG       MKTAG('C','A','M','G')
#define ID_CMAP       MKTAG('C','M','A','P')
#define ID_ACBM       MKTAG('A','C','B','M')

#define ID_FORM       MKTAG('F','O','R','M')
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
#define ID_ANNO       MKTAG('A','N','N','O')

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
#define IFF_EXTRA_VIDEO_SIZE 9

typedef enum {
    COMP_NONE,
    COMP_FIB,
    COMP_EXP
} svx8_compression_type;

typedef enum {
    BITMAP_RAW,
    BITMAP_BYTERUN1
} bitmap_compression_type;

typedef struct {
    uint64_t  body_pos;
    uint32_t  body_size;
    uint32_t  sent_bytes;
    svx8_compression_type   svx8_compression;
    bitmap_compression_type bitmap_compression;  ///< delta compression method used
    unsigned  bpp;          ///< bits per plane to decode (differs from bits_per_coded_sample if HAM)
    unsigned  ham;          ///< 0 if non-HAM or number of hold bits (6 for bpp > 6, 4 otherwise)
    unsigned  flags;        ///< 1 for EHB, 0 is no extra half darkening
    unsigned  transparency; ///< transparency color index in palette
    unsigned  masking;      ///< masking method used
} IffDemuxContext;

/* Metadata string read */
static int get_metadata(AVFormatContext *s,
                        const char *const tag,
                        const unsigned data_size)
{
    uint8_t *buf = ((data_size + 1) == 0) ? NULL : av_malloc(data_size + 1);

    if (!buf)
        return AVERROR(ENOMEM);

    if (avio_read(s->pb, buf, data_size) < 0) {
        av_free(buf);
        return AVERROR(EIO);
    }
    buf[data_size] = 0;
    av_dict_set(&s->metadata, tag, buf, AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int iff_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if ( AV_RL32(d)   == ID_FORM &&
         (AV_RL32(d+8) == ID_8SVX || AV_RL32(d+8) == ID_PBM || AV_RL32(d+8) == ID_ILBM || AV_RL32(d+8) == ID_ACBM) )
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int iff_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    IffDemuxContext *iff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint8_t *buf;
    uint32_t chunk_id, data_size;
    uint32_t screenmode = 0;
    unsigned transparency = 0;
    unsigned masking = 0; // no mask

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->channels = 1;
    avio_skip(pb, 8);
    // codec_tag used by ByteRun1 decoder to distinguish progressive (PBM) and interlaced (ILBM) content
    st->codec->codec_tag = avio_rl32(pb);

    while(!url_feof(pb)) {
        uint64_t orig_pos;
        int res;
        const char *metadata_tag = NULL;
        chunk_id = avio_rl32(pb);
        data_size = avio_rb32(pb);
        orig_pos = avio_tell(pb);

        switch(chunk_id) {
        case ID_VHDR:
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

            if (data_size < 14)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 12);
            st->codec->sample_rate = avio_rb16(pb);
            if (data_size >= 16) {
                avio_skip(pb, 1);
                iff->svx8_compression = avio_r8(pb);
            }
            break;

        case ID_ABIT:
        case ID_BODY:
            iff->body_pos = avio_tell(pb);
            iff->body_size = data_size;
            break;

        case ID_CHAN:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            st->codec->channels = (avio_rb32(pb) < 6) ? 1 : 2;
            break;

        case ID_CAMG:
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            screenmode                = avio_rb32(pb);
            break;

        case ID_CMAP:
            st->codec->extradata_size = data_size + IFF_EXTRA_VIDEO_SIZE;
            st->codec->extradata      = av_malloc(data_size + IFF_EXTRA_VIDEO_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
            if (avio_read(pb, st->codec->extradata + IFF_EXTRA_VIDEO_SIZE, data_size) < 0)
                return AVERROR(EIO);
            break;

        case ID_BMHD:
            iff->bitmap_compression = -1;
            st->codec->codec_type            = AVMEDIA_TYPE_VIDEO;
            if (data_size <= 8)
                return AVERROR_INVALIDDATA;
            st->codec->width                 = avio_rb16(pb);
            st->codec->height                = avio_rb16(pb);
            avio_skip(pb, 4); // x, y offset
            st->codec->bits_per_coded_sample = avio_r8(pb);
            if (data_size >= 10)
                masking                      = avio_r8(pb);
            if (data_size >= 11)
                iff->bitmap_compression      = avio_r8(pb);
            if (data_size >= 14) {
                avio_skip(pb, 1); // padding
                transparency                 = avio_rb16(pb);
            }
            if (data_size >= 16) {
                st->sample_aspect_ratio.num  = avio_r8(pb);
                st->sample_aspect_ratio.den  = avio_r8(pb);
            }
            break;

        case ID_ANNO:
        case ID_TEXT:      metadata_tag = "comment";   break;
        case ID_AUTH:      metadata_tag = "artist";    break;
        case ID_COPYRIGHT: metadata_tag = "copyright"; break;
        case ID_NAME:      metadata_tag = "title";     break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, metadata_tag, data_size)) < 0) {
                av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        avio_skip(pb, data_size - (avio_tell(pb) - orig_pos) + (data_size & 1));
    }

    avio_seek(pb, iff->body_pos, SEEK_SET);

    switch(st->codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_set_pts_info(st, 32, 1, st->codec->sample_rate);

        switch (iff->svx8_compression) {
        case COMP_NONE:
            st->codec->codec_id = CODEC_ID_PCM_S8_PLANAR;
            break;
        case COMP_FIB:
            st->codec->codec_id = CODEC_ID_8SVX_FIB;
            break;
        case COMP_EXP:
            st->codec->codec_id = CODEC_ID_8SVX_EXP;
            break;
        default:
            av_log(s, AV_LOG_ERROR,
                   "Unknown SVX8 compression method '%d'\n", iff->svx8_compression);
            return -1;
        }

        st->codec->bits_per_coded_sample = iff->svx8_compression == COMP_NONE ? 8 : 4;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate * st->codec->bits_per_coded_sample;
        st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;
        break;

    case AVMEDIA_TYPE_VIDEO:
        iff->bpp          = st->codec->bits_per_coded_sample;
        if ((screenmode & 0x800 /* Hold And Modify */) && iff->bpp <= 8) {
            iff->ham      = iff->bpp > 6 ? 6 : 4;
            st->codec->bits_per_coded_sample = 24;
        }
        iff->flags        = (screenmode & 0x80 /* Extra HalfBrite */) && iff->bpp <= 8;
        iff->masking      = masking;
        iff->transparency = transparency;

        if (!st->codec->extradata) {
            st->codec->extradata_size = IFF_EXTRA_VIDEO_SIZE;
            st->codec->extradata      = av_malloc(IFF_EXTRA_VIDEO_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
        }
        buf = st->codec->extradata;
        bytestream_put_be16(&buf, IFF_EXTRA_VIDEO_SIZE);
        bytestream_put_byte(&buf, iff->bitmap_compression);
        bytestream_put_byte(&buf, iff->bpp);
        bytestream_put_byte(&buf, iff->ham);
        bytestream_put_byte(&buf, iff->flags);
        bytestream_put_be16(&buf, iff->transparency);
        bytestream_put_byte(&buf, iff->masking);

        switch (iff->bitmap_compression) {
        case BITMAP_RAW:
            st->codec->codec_id = CODEC_ID_IFF_ILBM;
            break;
        case BITMAP_BYTERUN1:
            st->codec->codec_id = CODEC_ID_IFF_BYTERUN1;
            break;
        default:
            av_log(s, AV_LOG_ERROR,
                   "Unknown bitmap compression method '%d'\n", iff->bitmap_compression);
            return AVERROR_INVALIDDATA;
        }
        break;
    default:
        return -1;
    }

    return 0;
}

static int iff_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    IffDemuxContext *iff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int ret;

    if(iff->sent_bytes >= iff->body_size)
        return AVERROR_EOF;

    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = av_get_packet(pb, pkt, iff->body_size);
    } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        uint8_t *buf;

        if (av_new_packet(pkt, iff->body_size + 2) < 0) {
            return AVERROR(ENOMEM);
        }

        buf = pkt->data;
        bytestream_put_be16(&buf, 2);
        ret = avio_read(pb, buf, iff->body_size);
    } else {
        av_abort();
    }

    if(iff->sent_bytes == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;
    iff->sent_bytes = iff->body_size;

    pkt->stream_index = 0;
    return ret;
}

AVInputFormat ff_iff_demuxer = {
    .name           = "IFF",
    .long_name      = NULL_IF_CONFIG_SMALL("IFF format"),
    .priv_data_size = sizeof(IffDemuxContext),
    .read_probe     = iff_probe,
    .read_header    = iff_read_header,
    .read_packet    = iff_read_packet,
};
