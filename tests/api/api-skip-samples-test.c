/*
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
 * Decode one packet carrying AV_PKT_DATA_SKIP_SAMPLES and print how many
 * samples are output, for skips at the start and padding at the end of the
 * frame, alone, together and overlapping.
 */

#include <stdio.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"

#define FRAME_SAMPLES 1000

static int decoded_samples(uint32_t skip, uint32_t discard)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);
    AVCodecContext *ctx = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    uint8_t *side;
    int nb_samples = 0;
    int ret;

    ctx   = avcodec_alloc_context3(codec);
    pkt   = av_packet_alloc();
    frame = av_frame_alloc();
    if (!ctx || !pkt || !frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ctx->sample_rate  = 48000;
    ctx->pkt_timebase = (AVRational){ 1, 48000 };
    av_channel_layout_default(&ctx->ch_layout, 1);
    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0)
        goto end;

    ret = av_new_packet(pkt, FRAME_SAMPLES * 2);
    if (ret < 0)
        goto end;
    memset(pkt->data, 0, pkt->size);
    pkt->pts = 0;
    side = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
    if (!side) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    AV_WL32(side, skip);
    AV_WL32(side + 4, discard);

    ret = avcodec_send_packet(ctx, pkt);
    if (ret < 0)
        goto end;
    ret = avcodec_send_packet(ctx, NULL);
    if (ret < 0)
        goto end;
    while ((ret = avcodec_receive_frame(ctx, frame)) >= 0) {
        nb_samples += frame->nb_samples;
        av_frame_unref(frame);
    }
    ret = ret == AVERROR_EOF ? nb_samples : ret;

end:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ret;
}

int main(void)
{
    static const uint32_t cases[][2] = {
        {   0,    0 },
        { 300,    0 },
        {   0,  400 },
        { 300,  400 },
        { 300,  700 },
        { 300,  800 },
        { 300, 1100 },
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(cases); i++) {
        int ret = decoded_samples(cases[i][0], cases[i][1]);
        if (ret < 0) {
            fprintf(stderr, "decoding failed: %s\n", av_err2str(ret));
            return 1;
        }
        printf("frame %d skip %4"PRIu32" discard %4"PRIu32": %d samples\n",
               FRAME_SAMPLES, cases[i][0], cases[i][1], ret);
    }
    return 0;
}
