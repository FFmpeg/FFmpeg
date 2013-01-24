/*
 * LucasArts Smush demuxer
 * Copyright (c) 2006 Cyril Zorin
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
#include "internal.h"
#include "avio.h"

typedef struct {
    int version;
    int audio_stream_index;
    int video_stream_index;
} SMUSHContext;

static int smush_read_probe(AVProbeData *p)
{
    if (((AV_RL32(p->buf) == MKTAG('S', 'A', 'N', 'M') &&
          AV_RL32(p->buf + 8) == MKTAG('S', 'H', 'D', 'R')) ||
         (AV_RL32(p->buf) == MKTAG('A', 'N', 'I', 'M') &&
          AV_RL32(p->buf + 8) == MKTAG('A', 'H', 'D', 'R')))) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static int smush_read_header(AVFormatContext *ctx)
{
    SMUSHContext *smush = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    AVStream *vst, *ast;
    uint32_t magic, nframes, size, subversion, i;
    uint32_t width = 0, height = 0, got_audio = 0, read = 0;
    uint32_t sample_rate, channels, palette[256];

    magic = avio_rb32(pb);
    avio_skip(pb, 4); // skip movie size

    if (magic == MKBETAG('A', 'N', 'I', 'M')) {
        if (avio_rb32(pb) != MKBETAG('A', 'H', 'D', 'R'))
            return AVERROR_INVALIDDATA;

        size = avio_rb32(pb);
        if (size < 3 * 256 + 6)
            return AVERROR_INVALIDDATA;

        smush->version = 0;
        subversion     = avio_rl16(pb);
        nframes        = avio_rl16(pb);

        avio_skip(pb, 2); // skip pad

        for (i = 0; i < 256; i++)
            palette[i] = avio_rb24(pb);

        avio_skip(pb, size - (3 * 256 + 6));
    } else if (magic == MKBETAG('S', 'A', 'N', 'M') ) {
        if (avio_rb32(pb) != MKBETAG('S', 'H', 'D', 'R'))
            return AVERROR_INVALIDDATA;

        size = avio_rb32(pb);
        if (size < 14)
            return AVERROR_INVALIDDATA;

        smush->version = 1;
        subversion     = avio_rl16(pb);
        nframes = avio_rl32(pb);
        avio_skip(pb, 2); // skip pad
        width  = avio_rl16(pb);
        height = avio_rl16(pb);
        avio_skip(pb, 2); // skip pad
        avio_skip(pb, size - 14);

        if (avio_rb32(pb) != MKBETAG('F', 'L', 'H', 'D'))
            return AVERROR_INVALIDDATA;

        size = avio_rb32(pb);
        while (!got_audio && ((read + 8) < size)) {
            uint32_t sig, chunk_size;

            if (url_feof(pb))
                return AVERROR_EOF;

            sig        = avio_rb32(pb);
            chunk_size = avio_rb32(pb);
            read += 8;
            switch (sig) {
            case MKBETAG('W', 'a', 'v', 'e'):
                got_audio = 1;
                sample_rate = avio_rl32(pb);
                channels    = avio_rl32(pb);
                avio_skip(pb, chunk_size - 8);
                read += chunk_size;
                break;
            case MKBETAG('B', 'l', '1', '6'):
            case MKBETAG('A', 'N', 'N', 'O'):
                avio_skip(pb, chunk_size);
                read += chunk_size;
                break;
            default:
                return AVERROR_INVALIDDATA;
                break;
            }
        }

        avio_skip(pb, size - read);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Wrong magic\n");
        return AVERROR_INVALIDDATA;
    }

    vst = avformat_new_stream(ctx, 0);
    if (!vst)
        return AVERROR(ENOMEM);

    smush->video_stream_index = vst->index;

    vst->start_time        = 0;
    vst->duration          =
    vst->nb_frames         = nframes;
    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = AV_CODEC_ID_SANM;
    vst->codec->codec_tag  = 0;
    vst->codec->width      = width;
    vst->codec->height     = height;

    avpriv_set_pts_info(vst, 64, 66667, 1000000);

    if (!smush->version) {
        vst->codec->extradata = av_malloc(1024 + 2 + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!vst->codec->extradata)
            return AVERROR(ENOMEM);

        vst->codec->extradata_size = 1024 + 2;
        AV_WL16(vst->codec->extradata, subversion);
        for (i = 0; i < 256; i++)
            AV_WL32(vst->codec->extradata + 2 + i * 4, palette[i]);
    }

    if (got_audio) {
        ast = avformat_new_stream(ctx, 0);
        if (!ast)
            return AVERROR(ENOMEM);

        smush->audio_stream_index = ast->index;

        ast->start_time         = 0;
        ast->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
        ast->codec->codec_id    = AV_CODEC_ID_VIMA;
        ast->codec->codec_tag   = 0;
        ast->codec->sample_rate = sample_rate;
        ast->codec->channels    = channels;

        avpriv_set_pts_info(ast, 64, 1, ast->codec->sample_rate);
    }

    return 0;
}

static int smush_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    SMUSHContext *smush = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    int done = 0;

    while (!done) {
        uint32_t sig, size;

        if (url_feof(pb))
            return AVERROR_EOF;

        sig    = avio_rb32(pb);
        size   = avio_rb32(pb);

        switch (sig) {
        case MKBETAG('F', 'R', 'M', 'E'):
            if (smush->version)
                break;
            if (av_get_packet(pb, pkt, size) < 0)
                return AVERROR(EIO);

            pkt->stream_index = smush->video_stream_index;
            done = 1;
            break;
        case MKBETAG('B', 'l', '1', '6'):
            if (av_get_packet(pb, pkt, size) < 0)
                return AVERROR(EIO);

            pkt->stream_index = smush->video_stream_index;
            pkt->duration = 1;
            done = 1;
            break;
        case MKBETAG('W', 'a', 'v', 'e'):
            if (size < 13)
                return AVERROR_INVALIDDATA;
            if (av_get_packet(pb, pkt, size) < 13)
                return AVERROR(EIO);

            pkt->stream_index = smush->audio_stream_index;
            pkt->flags       |= AV_PKT_FLAG_KEY;
            pkt->duration = AV_RB32(pkt->data);
            if (pkt->duration == 0xFFFFFFFFu)
                pkt->duration = AV_RB32(pkt->data + 8);
            done = 1;
            break;
        default:
            avio_skip(pb, size);
            break;
        }
    }

    return 0;
}

AVInputFormat ff_smush_demuxer = {
    .name           = "smush",
    .long_name      = NULL_IF_CONFIG_SMALL("LucasArts Smush"),
    .priv_data_size = sizeof(SMUSHContext),
    .read_probe     = smush_read_probe,
    .read_header    = smush_read_header,
    .read_packet    = smush_read_packet,
};
