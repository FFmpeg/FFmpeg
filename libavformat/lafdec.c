/*
 * Limitless Audio Format demuxer
 * Copyright (c) 2022 Paul B Mahol
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
#include "libavutil/mem.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

#define MAX_STREAMS 4096

typedef struct StreamParams {
    AVChannelLayout layout;
    float horizontal;
    float vertical;
    int lfe;
    int stored;
} StreamParams;

typedef struct LAFContext {
    uint8_t *data;
    unsigned nb_stored;
    unsigned stored_index;
    unsigned index;
    unsigned bpp;

    StreamParams p[MAX_STREAMS];

    int header_len;
    uint8_t header[(MAX_STREAMS + 7) / 8];
} LAFContext;

static int laf_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "LIMITLESS", 9))
        return 0;
    if (memcmp(p->buf + 9, "HEAD", 4))
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int laf_read_header(AVFormatContext *ctx)
{
    LAFContext *s = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    unsigned st_count, mode;
    unsigned sample_rate;
    int64_t duration;
    int codec_id;
    int quality;
    int bpp;

    avio_skip(pb, 9);
    if (avio_rb32(pb) != MKBETAG('H','E','A','D'))
        return AVERROR_INVALIDDATA;

    quality = avio_r8(pb);
    if (quality > 3)
        return AVERROR_INVALIDDATA;
    mode = avio_r8(pb);
    if (mode > 1)
        return AVERROR_INVALIDDATA;
    st_count = avio_rl32(pb);
    if (st_count == 0 || st_count > MAX_STREAMS)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < st_count; i++) {
        StreamParams *stp = &s->p[i];

        stp->vertical = av_int2float(avio_rl32(pb));
        stp->horizontal = av_int2float(avio_rl32(pb));
        stp->lfe = avio_r8(pb);
        if (stp->lfe) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_LOW_FREQUENCY));
        } else if (stp->vertical == 0.f &&
                   stp->horizontal == 0.f) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_FRONT_CENTER));
        } else if (stp->vertical == 0.f &&
                   stp->horizontal == -30.f) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_FRONT_LEFT));
        } else if (stp->vertical == 0.f &&
                   stp->horizontal == 30.f) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_FRONT_RIGHT));
        } else if (stp->vertical == 0.f &&
                   stp->horizontal == -110.f) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_SIDE_LEFT));
        } else if (stp->vertical == 0.f &&
                   stp->horizontal == 110.f) {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MASK(1, (AV_CH_SIDE_RIGHT));
        } else {
            stp->layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        }
    }

    sample_rate = avio_rl32(pb);
    duration = avio_rl64(pb) / st_count;

    if (avio_feof(pb))
        return AVERROR_INVALIDDATA;

    switch (quality) {
    case 0:
        codec_id = AV_CODEC_ID_PCM_U8;
        bpp = 1;
        break;
    case 1:
        codec_id = AV_CODEC_ID_PCM_S16LE;
        bpp = 2;
        break;
    case 2:
        codec_id = AV_CODEC_ID_PCM_F32LE;
        bpp = 4;
        break;
    case 3:
        codec_id = AV_CODEC_ID_PCM_S24LE;
        bpp = 3;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    s->index = 0;
    s->stored_index = 0;
    s->bpp = bpp;
    if ((int64_t)bpp * st_count * (int64_t)sample_rate >= INT32_MAX ||
        (int64_t)bpp * st_count * (int64_t)sample_rate == 0
    )
        return AVERROR_INVALIDDATA;
    s->data = av_calloc(st_count * sample_rate, bpp);
    if (!s->data)
        return AVERROR(ENOMEM);

    for (unsigned i = 0; i < st_count; i++) {
        StreamParams *stp = &s->p[i];
        AVCodecParameters *par;
        AVStream *st = avformat_new_stream(ctx, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        par = st->codecpar;
        par->codec_id = codec_id;
        par->codec_type = AVMEDIA_TYPE_AUDIO;
        par->ch_layout.nb_channels = 1;
        par->ch_layout = stp->layout;
        par->sample_rate = sample_rate;
        st->duration = duration;

        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    }

    s->header_len = (ctx->nb_streams + 7) / 8;

    return 0;
}

static int laf_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    AVIOContext *pb = ctx->pb;
    LAFContext *s = ctx->priv_data;
    AVStream *st = ctx->streams[0];
    const int bpp = s->bpp;
    StreamParams *stp;
    int64_t pos;
    int ret;

    pos = avio_tell(pb);

again:
    if (avio_feof(pb))
        return AVERROR_EOF;

    if (s->index >= ctx->nb_streams) {
        int cur_st = 0, st_count = 0, st_index = 0;

        ret = ffio_read_size(pb, s->header, s->header_len);
        if (ret < 0)
            return ret;
        for (int i = 0; i < s->header_len; i++) {
            uint8_t val = s->header[i];

            for (int j = 0; j < 8 && cur_st < ctx->nb_streams; j++, cur_st++) {
                StreamParams *stp = &s->p[st_index];

                stp->stored = 0;
                if (val & 1) {
                    stp->stored = 1;
                    st_count++;
                }
                val >>= 1;
                st_index++;
            }
        }

        s->index = s->stored_index = 0;
        s->nb_stored = st_count;
        if (!st_count)
            return AVERROR_INVALIDDATA;
        ret = ffio_read_size(pb, s->data, st_count * st->codecpar->sample_rate * bpp);
        if (ret < 0)
            return ret;
    }

    st = ctx->streams[s->index];
    stp = &s->p[s->index];
    while (!stp->stored) {
        s->index++;
        if (s->index >= ctx->nb_streams)
            goto again;
        stp = &s->p[s->index];
    }
    st = ctx->streams[s->index];

    ret = av_new_packet(pkt, st->codecpar->sample_rate * bpp);
    if (ret < 0)
        return ret;

    switch (bpp) {
    case 1:
        for (int n = 0; n < st->codecpar->sample_rate; n++)
            pkt->data[n] = s->data[n * s->nb_stored + s->stored_index];
        break;
    case 2:
        for (int n = 0; n < st->codecpar->sample_rate; n++)
            AV_WN16(pkt->data + n * 2, AV_RN16(s->data + n * s->nb_stored * 2 + s->stored_index * 2));
        break;
    case 3:
        for (int n = 0; n < st->codecpar->sample_rate; n++)
            AV_WL24(pkt->data + n * 3, AV_RL24(s->data + n * s->nb_stored * 3 + s->stored_index * 3));
        break;
    case 4:
        for (int n = 0; n < st->codecpar->sample_rate; n++)
            AV_WN32(pkt->data + n * 4, AV_RN32(s->data + n * s->nb_stored * 4 + s->stored_index * 4));
        break;
    }

    pkt->stream_index = s->index;
    pkt->pos = pos;
    s->index++;
    s->stored_index++;

    return 0;
}

static int laf_read_close(AVFormatContext *ctx)
{
    LAFContext *s = ctx->priv_data;

    av_freep(&s->data);

    return 0;
}

static int laf_read_seek(AVFormatContext *ctx, int stream_index,
                         int64_t timestamp, int flags)
{
    LAFContext *s = ctx->priv_data;

    s->stored_index = s->index = s->nb_stored = 0;

    return -1;
}

const FFInputFormat ff_laf_demuxer = {
    .p.name         = "laf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LAF (Limitless Audio Format)"),
    .p.extensions   = "laf",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .priv_data_size = sizeof(LAFContext),
    .read_probe     = laf_probe,
    .read_header    = laf_read_header,
    .read_packet    = laf_read_packet,
    .read_close     = laf_read_close,
    .read_seek      = laf_read_seek,
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
};
