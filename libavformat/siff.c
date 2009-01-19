/*
 * Beam Software SIFF demuxer
 * Copyright (c) 2007 Konstantin Shishkov
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"

enum SIFFTags{
    TAG_SIFF = MKTAG('S', 'I', 'F', 'F'),
    TAG_BODY = MKTAG('B', 'O', 'D', 'Y'),
    TAG_VBHD = MKTAG('V', 'B', 'H', 'D'),
    TAG_SHDR = MKTAG('S', 'H', 'D', 'R'),
    TAG_VBV1 = MKTAG('V', 'B', 'V', '1'),
    TAG_SOUN = MKTAG('S', 'O', 'U', 'N'),
};

enum VBFlags{
    VB_HAS_GMC     = 0x01,
    VB_HAS_AUDIO   = 0x04,
    VB_HAS_VIDEO   = 0x08,
    VB_HAS_PALETTE = 0x10,
    VB_HAS_LENGTH  = 0x20
};

typedef struct SIFFContext{
    int frames;
    int cur_frame;
    int rate;
    int bits;
    int block_align;

    int has_video;
    int has_audio;

    int curstrm;
    int pktsize;
    int gmcsize;
    int sndsize;

    int flags;
    uint8_t gmc[4];
}SIFFContext;

static int siff_probe(AVProbeData *p)
{
    /* check file header */
    if (AV_RL32(p->buf) == TAG_SIFF)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int create_audio_stream(AVFormatContext *s, SIFFContext *c)
{
    AVStream *ast;
    ast = av_new_stream(s, 0);
    if (!ast)
        return -1;
    ast->codec->codec_type      = CODEC_TYPE_AUDIO;
    ast->codec->codec_id        = CODEC_ID_PCM_U8;
    ast->codec->channels        = 1;
    ast->codec->bits_per_coded_sample = c->bits;
    ast->codec->sample_rate     = c->rate;
    ast->codec->frame_size      = c->block_align;
    av_set_pts_info(ast, 16, 1, c->rate);
    return 0;
}

static int siff_parse_vbv1(AVFormatContext *s, SIFFContext *c, ByteIOContext *pb)
{
    AVStream *st;
    int width, height;

    if (get_le32(pb) != TAG_VBHD){
        av_log(s, AV_LOG_ERROR, "Header chunk is missing\n");
        return -1;
    }
    if(get_be32(pb) != 32){
        av_log(s, AV_LOG_ERROR, "Header chunk size is incorrect\n");
        return -1;
    }
    if(get_le16(pb) != 1){
        av_log(s, AV_LOG_ERROR, "Incorrect header version\n");
        return -1;
    }
    width = get_le16(pb);
    height = get_le16(pb);
    url_fskip(pb, 4);
    c->frames = get_le16(pb);
    if(!c->frames){
        av_log(s, AV_LOG_ERROR, "File contains no frames ???\n");
        return -1;
    }
    c->bits = get_le16(pb);
    c->rate = get_le16(pb);
    c->block_align = c->rate * (c->bits >> 3);

    url_fskip(pb, 16); //zeroes

    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_VB;
    st->codec->codec_tag  = MKTAG('V', 'B', 'V', '1');
    st->codec->width      = width;
    st->codec->height     = height;
    st->codec->pix_fmt    = PIX_FMT_PAL8;
    av_set_pts_info(st, 16, 1, 12);

    c->cur_frame = 0;
    c->has_video = 1;
    c->has_audio = !!c->rate;
    c->curstrm = -1;
    if (c->has_audio && create_audio_stream(s, c) < 0)
        return -1;
    return 0;
}

static int siff_parse_soun(AVFormatContext *s, SIFFContext *c, ByteIOContext *pb)
{
    if (get_le32(pb) != TAG_SHDR){
        av_log(s, AV_LOG_ERROR, "Header chunk is missing\n");
        return -1;
    }
    if(get_be32(pb) != 8){
        av_log(s, AV_LOG_ERROR, "Header chunk size is incorrect\n");
        return -1;
    }
    url_fskip(pb, 4); //unknown value
    c->rate = get_le16(pb);
    c->bits = get_le16(pb);
    c->block_align = c->rate * (c->bits >> 3);
    return create_audio_stream(s, c);
}

static int siff_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    SIFFContext *c = s->priv_data;
    uint32_t tag;

    if (get_le32(pb) != TAG_SIFF)
        return -1;
    url_fskip(pb, 4); //ignore size
    tag = get_le32(pb);

    if (tag != TAG_VBV1 && tag != TAG_SOUN){
        av_log(s, AV_LOG_ERROR, "Not a VBV file\n");
        return -1;
    }

    if (tag == TAG_VBV1 && siff_parse_vbv1(s, c, pb) < 0)
        return -1;
    if (tag == TAG_SOUN && siff_parse_soun(s, c, pb) < 0)
        return -1;
    if (get_le32(pb) != MKTAG('B', 'O', 'D', 'Y')){
        av_log(s, AV_LOG_ERROR, "'BODY' chunk is missing\n");
        return -1;
    }
    url_fskip(pb, 4); //ignore size

    return 0;
}

static int siff_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIFFContext *c = s->priv_data;
    int size;

    if (c->has_video){
        if (c->cur_frame >= c->frames)
            return AVERROR(EIO);
        if (c->curstrm == -1){
            c->pktsize = get_le32(s->pb) - 4;
            c->flags = get_le16(s->pb);
            c->gmcsize = (c->flags & VB_HAS_GMC) ? 4 : 0;
            if (c->gmcsize)
                get_buffer(s->pb, c->gmc, c->gmcsize);
            c->sndsize = (c->flags & VB_HAS_AUDIO) ? get_le32(s->pb): 0;
            c->curstrm = !!(c->flags & VB_HAS_AUDIO);
        }

        if (!c->curstrm){
            size = c->pktsize - c->sndsize;
            if (av_new_packet(pkt, size) < 0)
                return AVERROR(ENOMEM);
            AV_WL16(pkt->data, c->flags);
            if (c->gmcsize)
                memcpy(pkt->data + 2, c->gmc, c->gmcsize);
            get_buffer(s->pb, pkt->data + 2 + c->gmcsize, size - c->gmcsize - 2);
            pkt->stream_index = 0;
            c->curstrm = -1;
        }else{
            if (av_get_packet(s->pb, pkt, c->sndsize - 4) < 0)
                return AVERROR(EIO);
            pkt->stream_index = 1;
            c->curstrm = 0;
        }
        if(!c->cur_frame || c->curstrm)
            pkt->flags |= PKT_FLAG_KEY;
        if (c->curstrm == -1)
            c->cur_frame++;
    }else{
        size = av_get_packet(s->pb, pkt, c->block_align);
        if(size <= 0)
            return AVERROR(EIO);
    }
    return pkt->size;
}

AVInputFormat siff_demuxer = {
    "siff",
    NULL_IF_CONFIG_SMALL("Beam Software SIFF"),
    sizeof(SIFFContext),
    siff_probe,
    siff_read_header,
    siff_read_packet,
    .extensions = "vb,son"
};
