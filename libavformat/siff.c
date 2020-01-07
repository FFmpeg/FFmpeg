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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

enum SIFFTags {
    TAG_SIFF = MKTAG('S', 'I', 'F', 'F'),
    TAG_BODY = MKTAG('B', 'O', 'D', 'Y'),
    TAG_VBHD = MKTAG('V', 'B', 'H', 'D'),
    TAG_SHDR = MKTAG('S', 'H', 'D', 'R'),
    TAG_VBV1 = MKTAG('V', 'B', 'V', '1'),
    TAG_SOUN = MKTAG('S', 'O', 'U', 'N'),
};

enum VBFlags {
    VB_HAS_GMC     = 0x01,
    VB_HAS_AUDIO   = 0x04,
    VB_HAS_VIDEO   = 0x08,
    VB_HAS_PALETTE = 0x10,
    VB_HAS_LENGTH  = 0x20
};

typedef struct SIFFContext {
    int frames;
    int cur_frame;
    int rate;
    int bits;
    int block_align;

    int has_video;
    int has_audio;

    int curstrm;
    unsigned int pktsize;
    int gmcsize;
    unsigned int sndsize;

    unsigned int flags;
    uint8_t gmc[4];
} SIFFContext;

static int siff_probe(const AVProbeData *p)
{
    uint32_t tag = AV_RL32(p->buf + 8);
    /* check file header */
    if (AV_RL32(p->buf) != TAG_SIFF ||
        (tag != TAG_VBV1 && tag != TAG_SOUN))
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int create_audio_stream(AVFormatContext *s, SIFFContext *c)
{
    AVStream *ast;
    ast = avformat_new_stream(s, NULL);
    if (!ast)
        return AVERROR(ENOMEM);
    ast->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->codec_id              = AV_CODEC_ID_PCM_U8;
    ast->codecpar->channels              = 1;
    ast->codecpar->channel_layout        = AV_CH_LAYOUT_MONO;
    ast->codecpar->bits_per_coded_sample = 8;
    ast->codecpar->sample_rate           = c->rate;
    avpriv_set_pts_info(ast, 16, 1, c->rate);
    ast->start_time                   = 0;
    return 0;
}

static int siff_parse_vbv1(AVFormatContext *s, SIFFContext *c, AVIOContext *pb)
{
    AVStream *st;
    int width, height;

    if (avio_rl32(pb) != TAG_VBHD) {
        av_log(s, AV_LOG_ERROR, "Header chunk is missing\n");
        return AVERROR_INVALIDDATA;
    }
    if (avio_rb32(pb) != 32) {
        av_log(s, AV_LOG_ERROR, "Header chunk size is incorrect\n");
        return AVERROR_INVALIDDATA;
    }
    if (avio_rl16(pb) != 1) {
        av_log(s, AV_LOG_ERROR, "Incorrect header version\n");
        return AVERROR_INVALIDDATA;
    }
    width  = avio_rl16(pb);
    height = avio_rl16(pb);
    avio_skip(pb, 4);
    c->frames = avio_rl16(pb);
    if (!c->frames) {
        av_log(s, AV_LOG_ERROR, "File contains no frames ???\n");
        return AVERROR_INVALIDDATA;
    }
    c->bits        = avio_rl16(pb);
    c->rate        = avio_rl16(pb);
    c->block_align = c->rate * (c->bits >> 3);

    avio_skip(pb, 16); // zeroes

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_VB;
    st->codecpar->codec_tag  = MKTAG('V', 'B', 'V', '1');
    st->codecpar->width      = width;
    st->codecpar->height     = height;
    st->codecpar->format     = AV_PIX_FMT_PAL8;
    st->nb_frames            =
    st->duration             = c->frames;
    avpriv_set_pts_info(st, 16, 1, 12);

    c->cur_frame = 0;
    c->has_video = 1;
    c->has_audio = !!c->rate;
    c->curstrm   = -1;
    if (c->has_audio)
        return create_audio_stream(s, c);
    return 0;
}

static int siff_parse_soun(AVFormatContext *s, SIFFContext *c, AVIOContext *pb)
{
    if (avio_rl32(pb) != TAG_SHDR) {
        av_log(s, AV_LOG_ERROR, "Header chunk is missing\n");
        return AVERROR_INVALIDDATA;
    }
    if (avio_rb32(pb) != 8) {
        av_log(s, AV_LOG_ERROR, "Header chunk size is incorrect\n");
        return AVERROR_INVALIDDATA;
    }
    avio_skip(pb, 4); // unknown value
    c->rate        = avio_rl16(pb);
    c->bits        = avio_rl16(pb);
    c->block_align = c->rate * (c->bits >> 3);
    return create_audio_stream(s, c);
}

static int siff_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    SIFFContext *c  = s->priv_data;
    uint32_t tag;
    int ret;

    if (avio_rl32(pb) != TAG_SIFF)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 4); // ignore size
    tag = avio_rl32(pb);

    if (tag != TAG_VBV1 && tag != TAG_SOUN) {
        av_log(s, AV_LOG_ERROR, "Not a VBV file\n");
        return AVERROR_INVALIDDATA;
    }

    if (tag == TAG_VBV1 && (ret = siff_parse_vbv1(s, c, pb)) < 0)
        return ret;
    if (tag == TAG_SOUN && (ret = siff_parse_soun(s, c, pb)) < 0)
        return ret;
    if (avio_rl32(pb) != MKTAG('B', 'O', 'D', 'Y')) {
        av_log(s, AV_LOG_ERROR, "'BODY' chunk is missing\n");
        return AVERROR_INVALIDDATA;
    }
    avio_skip(pb, 4); // ignore size

    return 0;
}

static int siff_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIFFContext *c = s->priv_data;
    int ret;

    if (c->has_video) {
        unsigned int size;
        if (c->cur_frame >= c->frames)
            return AVERROR_EOF;
        if (c->curstrm == -1) {
            c->pktsize = avio_rl32(s->pb) - 4;
            c->flags   = avio_rl16(s->pb);
            c->gmcsize = (c->flags & VB_HAS_GMC) ? 4 : 0;
            if (c->gmcsize)
                avio_read(s->pb, c->gmc, c->gmcsize);
            c->sndsize = (c->flags & VB_HAS_AUDIO) ? avio_rl32(s->pb) : 0;
            c->curstrm = !!(c->flags & VB_HAS_AUDIO);
        }

        if (!c->curstrm) {
            if (c->pktsize < 2LL + c->sndsize + c->gmcsize)
                return AVERROR_INVALIDDATA;

            size = c->pktsize - c->sndsize - c->gmcsize - 2;
            size = ffio_limit(s->pb, size);
            if ((ret = av_new_packet(pkt, size + c->gmcsize + 2)) < 0)
                return ret;
            AV_WL16(pkt->data, c->flags);
            if (c->gmcsize)
                memcpy(pkt->data + 2, c->gmc, c->gmcsize);
            if (avio_read(s->pb, pkt->data + 2 + c->gmcsize, size) != size) {
                return AVERROR_INVALIDDATA;
            }
            pkt->stream_index = 0;
            c->curstrm        = -1;
        } else {
            int pktsize = av_get_packet(s->pb, pkt, c->sndsize - 4);
            if (pktsize < 0)
                return AVERROR(EIO);
            pkt->stream_index = 1;
            pkt->duration     = pktsize;
            c->curstrm        = 0;
        }
        if (!c->cur_frame || c->curstrm)
            pkt->flags |= AV_PKT_FLAG_KEY;
        if (c->curstrm == -1)
            c->cur_frame++;
    } else {
        int pktsize = av_get_packet(s->pb, pkt, c->block_align);
        if (!pktsize)
            return AVERROR_EOF;
        if (pktsize <= 0)
            return AVERROR(EIO);
        pkt->duration = pktsize;
    }
    return pkt->size;
}

AVInputFormat ff_siff_demuxer = {
    .name           = "siff",
    .long_name      = NULL_IF_CONFIG_SMALL("Beam Software SIFF"),
    .priv_data_size = sizeof(SIFFContext),
    .read_probe     = siff_probe,
    .read_header    = siff_read_header,
    .read_packet    = siff_read_packet,
    .extensions     = "vb,son",
};
