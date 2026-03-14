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
 * Print encoder properties (bit_rate, block_align, bits_per_coded_sample,
 * frame_size) after init.
 * Useful for verifying that encoders correctly set their codec parameters.
 *
 * Usage: encinfo <codec_name> [sample_rate] [channels]
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"

int main(int argc, char **argv)
{
    const AVCodec *codec;
    AVCodecContext *ctx;
    int sample_rate = 44100;
    int channels    = 2;
    int ret;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <codec_name> [sample_rate] [channels]\n",
                argv[0]);
        return 1;
    }

    if (argc >= 3)
        sample_rate = atoi(argv[2]);
    if (argc >= 4)
        channels = atoi(argv[3]);

    codec = avcodec_find_encoder_by_name(argv[1]);
    if (!codec) {
        fprintf(stderr, "Encoder '%s' not found\n", argv[1]);
        return 1;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->sample_rate = sample_rate;
    ctx->sample_fmt  = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_S16;
    av_channel_layout_default(&ctx->ch_layout, channels);

    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %d\n", ret);
        avcodec_free_context(&ctx);
        return 1;
    }

    printf("%s bit_rate=%"PRId64" block_align=%d bits_per_coded_sample=%d frame_size=%d\n",
           codec->name, ctx->bit_rate, ctx->block_align,
           ctx->bits_per_coded_sample, ctx->frame_size);

    avcodec_free_context(&ctx);
    return 0;
}
