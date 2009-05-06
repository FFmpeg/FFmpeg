/*
 * 8088flex TMV file demuxer
 * Copyright (c) 2009 Daniel Verkamp <daniel at drv.nu>
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
 * 8088flex TMV file demuxer
 * @file libavformat/tmv.c
 * @author Daniel Verkamp
 * @sa http://www.oldskool.org/pc/8088_Corruption
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

enum {
    TMV_PADDING = 0x01,
    TMV_STEREO  = 0x02,
};

#define TMV_TAG MKTAG('T', 'M', 'A', 'V')

typedef struct TMVContext {
    unsigned audio_chunk_size;
    unsigned video_chunk_size;
    unsigned padding;
    unsigned stream_index;
} TMVContext;

static int tmv_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == TMV_TAG)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int tmv_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    TMVContext *tmv   = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *vst, *ast;
    AVRational fps;
    unsigned comp_method, char_cols, char_rows, features;

    if (get_le32(pb) != TMV_TAG)
        return -1;

    if (!(vst = av_new_stream(s, 0)))
        return AVERROR(ENOMEM);

    if (!(ast = av_new_stream(s, 0)))
        return AVERROR(ENOMEM);

    ast->codec->sample_rate = get_le16(pb);
    tmv->audio_chunk_size   = get_le16(pb);
    if (!tmv->audio_chunk_size) {
        av_log(s, AV_LOG_ERROR, "invalid audio chunk size\n");
        return -1;
    }

    comp_method             = get_byte(pb);
    if (comp_method) {
        av_log(s, AV_LOG_ERROR, "unsupported compression method %d\n",
               comp_method);
        return -1;
    }

    char_cols = get_byte(pb);
    char_rows = get_byte(pb);
    tmv->video_chunk_size = char_cols * char_rows * 2;

    features  = get_byte(pb);
    if (features & ~(TMV_PADDING | TMV_STEREO)) {
        av_log(s, AV_LOG_ERROR, "unsupported features 0x%02x\n",
               features & ~(TMV_PADDING | TMV_STEREO));
        return -1;
    }

    ast->codec->codec_type            = CODEC_TYPE_AUDIO;
    ast->codec->codec_id              = CODEC_ID_PCM_U8;
    ast->codec->sample_fmt            = SAMPLE_FMT_U8;
    ast->codec->channels              = features & TMV_STEREO ? 2 : 1;
    ast->codec->bits_per_coded_sample = 8;
    ast->codec->bit_rate              = ast->codec->sample_rate *
                                        ast->codec->bits_per_coded_sample;
    av_set_pts_info(ast, 32, 1, ast->codec->sample_rate);

    fps.num = ast->codec->sample_rate * ast->codec->channels;
    fps.den = tmv->audio_chunk_size;
    av_reduce(&fps.num, &fps.den, fps.num, fps.den, 0xFFFFFFFFLL);

    vst->codec->codec_type = CODEC_TYPE_VIDEO;
    vst->codec->codec_id   = CODEC_ID_TMV;
    vst->codec->pix_fmt    = PIX_FMT_PAL8;
    vst->codec->width      = char_cols * 8;
    vst->codec->height     = char_rows * 8;
    av_set_pts_info(vst, 32, fps.den, fps.num);

    if (features & TMV_PADDING)
        tmv->padding =
            ((tmv->video_chunk_size + tmv->audio_chunk_size + 511) & ~511) -
             (tmv->video_chunk_size + tmv->audio_chunk_size);

    vst->codec->bit_rate = ((tmv->video_chunk_size + tmv->padding) *
                            fps.num * 8) / fps.den;

    return 0;
}

static int tmv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TMVContext *tmv   = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret, pkt_size = tmv->stream_index ?
                        tmv->audio_chunk_size : tmv->video_chunk_size;

    if (url_feof(pb))
        return AVERROR_EOF;

    ret = av_get_packet(pb, pkt, pkt_size);

    if (tmv->stream_index)
        url_fskip(pb, tmv->padding);

    pkt->stream_index  = tmv->stream_index;
    tmv->stream_index ^= 1;
    pkt->flags        |= PKT_FLAG_KEY;

    return ret;
}

AVInputFormat tmv_demuxer = {
    "tmv",
    NULL_IF_CONFIG_SMALL("8088flex TMV"),
    sizeof(TMVContext),
    tmv_probe,
    tmv_read_header,
    tmv_read_packet,
    .flags = AVFMT_GENERIC_INDEX,
};
