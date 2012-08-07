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
 * @file
 * 8088flex TMV file demuxer
 * @author Daniel Verkamp
 * @see http://www.oldskool.org/pc/8088_Corruption
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

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

#define TMV_HEADER_SIZE       12

#define PROBE_MIN_SAMPLE_RATE 5000
#define PROBE_MAX_FPS         120
#define PROBE_MIN_AUDIO_SIZE  (PROBE_MIN_SAMPLE_RATE / PROBE_MAX_FPS)

static int tmv_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf)   == TMV_TAG &&
        AV_RL16(p->buf+4) >= PROBE_MIN_SAMPLE_RATE &&
        AV_RL16(p->buf+6) >= PROBE_MIN_AUDIO_SIZE  &&
               !p->buf[8] && // compression method
                p->buf[9] && // char cols
                p->buf[10])  // char rows
        return AVPROBE_SCORE_MAX /
            ((p->buf[9] == 40 && p->buf[10] == 25) ? 1 : 4);
    return 0;
}

static int tmv_read_header(AVFormatContext *s)
{
    TMVContext *tmv   = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *vst, *ast;
    AVRational fps;
    unsigned comp_method, char_cols, char_rows, features;

    if (avio_rl32(pb) != TMV_TAG)
        return -1;

    if (!(vst = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    if (!(ast = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    ast->codec->sample_rate = avio_rl16(pb);
    if (!ast->codec->sample_rate) {
        av_log(s, AV_LOG_ERROR, "invalid sample rate\n");
        return -1;
    }

    tmv->audio_chunk_size   = avio_rl16(pb);
    if (!tmv->audio_chunk_size) {
        av_log(s, AV_LOG_ERROR, "invalid audio chunk size\n");
        return -1;
    }

    comp_method             = avio_r8(pb);
    if (comp_method) {
        av_log(s, AV_LOG_ERROR, "unsupported compression method %d\n",
               comp_method);
        return -1;
    }

    char_cols = avio_r8(pb);
    char_rows = avio_r8(pb);
    tmv->video_chunk_size = char_cols * char_rows * 2;

    features  = avio_r8(pb);
    if (features & ~(TMV_PADDING | TMV_STEREO)) {
        av_log(s, AV_LOG_ERROR, "unsupported features 0x%02x\n",
               features & ~(TMV_PADDING | TMV_STEREO));
        return -1;
    }

    ast->codec->codec_type            = AVMEDIA_TYPE_AUDIO;
    ast->codec->codec_id              = AV_CODEC_ID_PCM_U8;
    ast->codec->channels              = features & TMV_STEREO ? 2 : 1;
    ast->codec->bits_per_coded_sample = 8;
    ast->codec->bit_rate              = ast->codec->sample_rate *
                                        ast->codec->bits_per_coded_sample;
    avpriv_set_pts_info(ast, 32, 1, ast->codec->sample_rate);

    fps.num = ast->codec->sample_rate * ast->codec->channels;
    fps.den = tmv->audio_chunk_size;
    av_reduce(&fps.num, &fps.den, fps.num, fps.den, 0xFFFFFFFFLL);

    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = AV_CODEC_ID_TMV;
    vst->codec->pix_fmt    = PIX_FMT_PAL8;
    vst->codec->width      = char_cols * 8;
    vst->codec->height     = char_rows * 8;
    avpriv_set_pts_info(vst, 32, fps.den, fps.num);

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
    AVIOContext *pb = s->pb;
    int ret, pkt_size = tmv->stream_index ?
                        tmv->audio_chunk_size : tmv->video_chunk_size;

    if (url_feof(pb))
        return AVERROR_EOF;

    ret = av_get_packet(pb, pkt, pkt_size);

    if (tmv->stream_index)
        avio_skip(pb, tmv->padding);

    pkt->stream_index  = tmv->stream_index;
    tmv->stream_index ^= 1;
    pkt->flags        |= AV_PKT_FLAG_KEY;

    return ret;
}

static int tmv_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    TMVContext *tmv = s->priv_data;
    int64_t pos;

    if (stream_index)
        return -1;

    pos = timestamp *
          (tmv->audio_chunk_size + tmv->video_chunk_size + tmv->padding);

    if (avio_seek(s->pb, pos + TMV_HEADER_SIZE, SEEK_SET) < 0)
        return -1;
    tmv->stream_index = 0;
    return 0;
}

AVInputFormat ff_tmv_demuxer = {
    .name           = "tmv",
    .long_name      = NULL_IF_CONFIG_SMALL("8088flex TMV"),
    .priv_data_size = sizeof(TMVContext),
    .read_probe     = tmv_probe,
    .read_header    = tmv_read_header,
    .read_packet    = tmv_read_packet,
    .read_seek      = tmv_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
};
