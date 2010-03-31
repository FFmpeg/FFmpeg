/*
 * IFF (.iff) file demuxer
 * Copyright (c) 2008 Jaikrishnan Menon <realityman@gmx.net>
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * @file libavformat/iff.c
 * IFF file demuxer
 * by Jaikrishnan Menon
 * for more information on the .iff file format, visit:
 * http://wiki.multimedia.cx/index.php?title=IFF
 */

#include "libavutil/intreadwrite.h"
#include "libavcodec/iff.h"
#include "avformat.h"

#define ID_8SVX       MKTAG('8','S','V','X')
#define ID_VHDR       MKTAG('V','H','D','R')
#define ID_ATAK       MKTAG('A','T','A','K')
#define ID_RLSE       MKTAG('R','L','S','E')
#define ID_CHAN       MKTAG('C','H','A','N')
#define ID_PBM        MKTAG('P','B','M',' ')
#define ID_ILBM       MKTAG('I','L','B','M')
#define ID_BMHD       MKTAG('B','M','H','D')
#define ID_CMAP       MKTAG('C','M','A','P')

#define ID_FORM       MKTAG('F','O','R','M')
#define ID_ANNO       MKTAG('A','N','N','O')
#define ID_AUTH       MKTAG('A','U','T','H')
#define ID_CHRS       MKTAG('C','H','R','S')
#define ID_COPYRIGHT  MKTAG('(','c',')',' ')
#define ID_CSET       MKTAG('C','S','E','T')
#define ID_FVER       MKTAG('F','V','E','R')
#define ID_NAME       MKTAG('N','A','M','E')
#define ID_TEXT       MKTAG('T','E','X','T')
#define ID_BODY       MKTAG('B','O','D','Y')
#define ID_ANNO       MKTAG('A','N','N','O')

#define LEFT    2
#define RIGHT   4
#define STEREO  6

#define PACKET_SIZE 1024

typedef enum {COMP_NONE, COMP_FIB, COMP_EXP} svx8_compression_type;
typedef enum {BITMAP_RAW, BITMAP_BYTERUN1} bitmap_compression_type;

typedef struct {
    uint32_t  body_size;
    uint32_t  sent_bytes;
    uint32_t  audio_frame_count;
} IffDemuxContext;


static void interleave_stereo(const uint8_t *src, uint8_t *dest, int size)
{
    uint8_t *end = dest + size;
    size = size>>1;

    while(dest < end) {
        *dest++ = *src;
        *dest++ = *(src+size);
        src++;
    }
}

static int iff_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if ( AV_RL32(d)   == ID_FORM &&
         (AV_RL32(d+8) == ID_8SVX || AV_RL32(d+8) == ID_PBM || AV_RL32(d+8) == ID_ILBM) )
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int iff_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    IffDemuxContext *iff = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    uint32_t chunk_id, data_size;
    int padding, done = 0;
    int compression = -1;
    char *buf;

    st = av_new_stream(s, 0);
    if (!st)
      return AVERROR(ENOMEM);

    st->codec->channels = 1;
    url_fskip(pb, 8);
    // codec_tag used by ByteRun1 decoder to distinguish progressive (PBM) and interlaced (ILBM) content
    st->codec->codec_tag = get_le32(pb);

    while(!done && !url_feof(pb)) {
        chunk_id = get_le32(pb);
        data_size = get_be32(pb);
        padding = data_size & 1;

        switch(chunk_id) {
        case ID_VHDR:
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            url_fskip(pb, 12);
            st->codec->sample_rate = get_be16(pb);
            url_fskip(pb, 1);
            compression            = get_byte(pb);
            url_fskip(pb, 4);
            break;

        case ID_BODY:
            iff->body_size = data_size;
            done = 1;
            break;

        case ID_CHAN:
            st->codec->channels = (get_be32(pb) < 6) ? 1 : 2;
            break;

        case ID_CMAP:
            st->codec->extradata_size = data_size;
            st->codec->extradata      = av_malloc(data_size);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
            if (get_buffer(pb, st->codec->extradata, data_size) < 0)
                return AVERROR(EIO);
            break;

        case ID_BMHD:
            st->codec->codec_type            = AVMEDIA_TYPE_VIDEO;
            st->codec->width                 = get_be16(pb);
            st->codec->height                = get_be16(pb);
            url_fskip(pb, 4); // x, y offset
            st->codec->bits_per_coded_sample = get_byte(pb);
            url_fskip(pb, 1); // masking
            compression                      = get_byte(pb);
            url_fskip(pb, 3); // paddding, transparent
            st->sample_aspect_ratio.num      = get_byte(pb);
            st->sample_aspect_ratio.den      = get_byte(pb);
            url_fskip(pb, 4); // source page width, height
            break;

        case ID_ANNO:
            buf = av_malloc(data_size + 1);
            if (!buf)
                break;
            get_buffer(pb, buf, data_size);
            buf[data_size] = 0;
            av_metadata_set2(&s->metadata, "comment", buf, AV_METADATA_DONT_STRDUP_VAL);
            break;

        default:
            url_fseek(pb, data_size + padding, SEEK_CUR);
            break;
        }
    }

    switch(st->codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
    av_set_pts_info(st, 32, 1, st->codec->sample_rate);

    switch(compression) {
    case COMP_NONE:
        st->codec->codec_id = CODEC_ID_PCM_S8;
        break;
    case COMP_FIB:
        st->codec->codec_id = CODEC_ID_8SVX_FIB;
        break;
    case COMP_EXP:
        st->codec->codec_id = CODEC_ID_8SVX_EXP;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "iff: unknown compression method\n");
        return -1;
    }

    st->codec->bits_per_coded_sample = 8;
    st->codec->bit_rate = st->codec->channels * st->codec->sample_rate * st->codec->bits_per_coded_sample;
    st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;
    break;

    case AVMEDIA_TYPE_VIDEO:
        switch (compression) {
        case BITMAP_RAW:
            if (st->codec->codec_tag == ID_ILBM) {
                st->codec->codec_id = CODEC_ID_IFF_ILBM;
            } else {
                st->codec->codec_id = CODEC_ID_RAWVIDEO;
                st->codec->pix_fmt  = PIX_FMT_PAL8;
                st->codec->codec_tag = 0;
            }
            break;
        case BITMAP_BYTERUN1:
            st->codec->codec_id = CODEC_ID_IFF_BYTERUN1;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "unknown compression method\n");
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
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int ret;

    if(iff->sent_bytes >= iff->body_size)
        return AVERROR(EIO);

    if(s->streams[0]->codec->channels == 2) {
        uint8_t sample_buffer[PACKET_SIZE];

        ret = get_buffer(pb, sample_buffer, PACKET_SIZE);
        if(av_new_packet(pkt, PACKET_SIZE) < 0) {
            av_log(s, AV_LOG_ERROR, "iff: cannot allocate packet \n");
            return AVERROR(ENOMEM);
        }
        interleave_stereo(sample_buffer, pkt->data, PACKET_SIZE);
    } else if (s->streams[0]->codec->codec_id == CODEC_ID_RAWVIDEO) {
        if(av_new_packet(pkt, iff->body_size + AVPALETTE_SIZE) < 0) {
            return AVERROR(ENOMEM);
        }

        ret = ff_cmap_read_palette(st->codec, (uint32_t*)(pkt->data + iff->body_size));
        if (ret < 0)
            return ret;
        av_freep(&st->codec->extradata);
        st->codec->extradata_size = 0;

        ret = get_buffer(pb, pkt->data, iff->body_size);
    } else if (s->streams[0]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = av_get_packet(pb, pkt, iff->body_size);
    } else {
        ret = av_get_packet(pb, pkt, PACKET_SIZE);
    }

    if(iff->sent_bytes == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;

    if(s->streams[0]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        iff->sent_bytes += PACKET_SIZE;
    } else {
        iff->sent_bytes = iff->body_size;
    }
    pkt->stream_index = 0;
    if(s->streams[0]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        pkt->pts = iff->audio_frame_count;
        iff->audio_frame_count += ret / s->streams[0]->codec->channels;
    }
    return ret;
}

AVInputFormat iff_demuxer = {
    "IFF",
    NULL_IF_CONFIG_SMALL("IFF format"),
    sizeof(IffDemuxContext),
    iff_probe,
    iff_read_header,
    iff_read_packet,
};
