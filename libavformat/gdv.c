/*
 * Gremlin Digital Video demuxer
 * Copyright (c) 2017 Paul B Mahol
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
#include "avio.h"
#include "internal.h"

typedef struct GDVContext {
    int is_first_video;
    int is_audio;
    int audio_size;
    int audio_stream_index;
    int video_stream_index;
    unsigned pal[256];
} GDVContext;

static int gdv_read_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) == 0x29111994)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static struct {
    uint16_t id;
    uint16_t width;
    uint16_t height;
} FixedSize[] = {
    { 0, 320, 200},
    { 1, 640, 200},
    { 2, 320, 167},
    { 3, 320, 180},
    { 4, 320, 400},
    { 5, 320, 170},
    { 6, 160,  85},
    { 7, 160,  83},
    { 8, 160,  90},
    { 9, 280, 128},
    {10, 320, 240},
    {11, 320, 201},
    {16, 640, 400},
    {17, 640, 200},
    {18, 640, 180},
    {19, 640, 167},
    {20, 640, 170},
    {21, 320, 240}
};

static int gdv_read_header(AVFormatContext *ctx)
{
    GDVContext *gdv = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    AVStream *vst, *ast;
    unsigned fps, snd_flags, vid_depth, size_id;

    avio_skip(pb, 4);
    size_id = avio_rl16(pb);

    vst = avformat_new_stream(ctx, 0);
    if (!vst)
        return AVERROR(ENOMEM);

    vst->start_time        = 0;
    vst->duration          =
    vst->nb_frames         = avio_rl16(pb);

    fps = avio_rl16(pb);
    if (!fps)
        return AVERROR_INVALIDDATA;

    snd_flags = avio_rl16(pb);
    if (snd_flags & 1) {
        ast = avformat_new_stream(ctx, 0);
        if (!ast)
            return AVERROR(ENOMEM);

        ast->start_time = 0;
        ast->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_tag   = 0;
        ast->codecpar->sample_rate = avio_rl16(pb);
        ast->codecpar->channels    = 1 + !!(snd_flags & 2);
        if (snd_flags & 8) {
            ast->codecpar->codec_id = AV_CODEC_ID_GREMLIN_DPCM;
        } else {
            ast->codecpar->codec_id = (snd_flags & 4) ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_PCM_U8;
        }

        avpriv_set_pts_info(ast, 64, 1, ast->codecpar->sample_rate);
        gdv->audio_size = (ast->codecpar->sample_rate / fps) *
                           ast->codecpar->channels * (1 + !!(snd_flags & 4)) / (1 + !!(snd_flags & 8));
        gdv->is_audio = 1;
    } else {
        avio_skip(pb, 2);
    }
    vid_depth = avio_rl16(pb);
    avio_skip(pb, 4);

    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_id   = AV_CODEC_ID_GDV;
    vst->codecpar->codec_tag  = 0;
    vst->codecpar->width      = avio_rl16(pb);
    vst->codecpar->height     = avio_rl16(pb);

    if (vst->codecpar->width == 0 || vst->codecpar->height == 0) {
        int i;

        for (i = 0; i < FF_ARRAY_ELEMS(FixedSize) - 1; i++) {
            if (FixedSize[i].id == size_id)
                break;
        }

        vst->codecpar->width  = FixedSize[i].width;
        vst->codecpar->height = FixedSize[i].height;
    }

    avpriv_set_pts_info(vst, 64, 1, fps);

    if (vid_depth & 1) {
        int i;

        for (i = 0; i < 256; i++) {
            unsigned r = avio_r8(pb);
            unsigned g = avio_r8(pb);
            unsigned b = avio_r8(pb);
            gdv->pal[i] = 0xFFU << 24 | r << 18 | g << 10 | b << 2;
        }
    }

    gdv->is_first_video = 1;

    return 0;
}

static int gdv_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    GDVContext *gdv = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    int ret;

    if (avio_feof(pb))
        return pb->error ? pb->error : AVERROR_EOF;

    if (gdv->audio_size && gdv->is_audio) {
        ret = av_get_packet(pb, pkt, gdv->audio_size);
        if (ret < 0)
            return ret;
        pkt->stream_index = 1;
        gdv->is_audio = 0;
    } else {
        uint8_t *pal;

        if (avio_rl16(pb) != 0x1305)
            return AVERROR_INVALIDDATA;
        ret = av_get_packet(pb, pkt, 4 + avio_rl16(pb));
        if (ret < 0)
            return ret;
        pkt->stream_index = 0;
        gdv->is_audio = 1;

        if (gdv->is_first_video) {
            pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                          AVPALETTE_SIZE);
            if (!pal) {
                return AVERROR(ENOMEM);
            }
            memcpy(pal, gdv->pal, AVPALETTE_SIZE);
            pkt->flags |= AV_PKT_FLAG_KEY;
            gdv->is_first_video = 0;
        }
    }

    return 0;
}

const AVInputFormat ff_gdv_demuxer = {
    .name           = "gdv",
    .long_name      = NULL_IF_CONFIG_SMALL("Gremlin Digital Video"),
    .priv_data_size = sizeof(GDVContext),
    .read_probe     = gdv_read_probe,
    .read_header    = gdv_read_header,
    .read_packet    = gdv_read_packet,
};
