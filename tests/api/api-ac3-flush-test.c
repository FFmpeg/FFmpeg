/*
 * Copyright (c) 2026 Jun Zhao <barryjzhao@tencent.com>
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

/*
 * Check that avcodec_flush_buffers() preserves the AVOptions of the
 * AC-3, E-AC-3 and fixed-point AC-3 decoders.
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

static int check_double(AVCodecContext *ctx, const char *name, double expected)
{
    double value = -12345.0;
    int ret = av_opt_get_double(ctx->priv_data, name, 0, &value);

    if (ret < 0) {
        fprintf(stderr, "%s: av_opt_get_double(\"%s\") failed: %s\n",
                ctx->codec->name, name, av_err2str(ret));
        return 1;
    }
    if (fabs(value - expected) > 1e-6) {
        fprintf(stderr, "%s: %s = %g, expected %g\n",
                ctx->codec->name, name, value, expected);
        return 1;
    }
    return 0;
}

static int check_int(AVCodecContext *ctx, const char *name, int64_t expected)
{
    int64_t value = -12345;
    int ret = av_opt_get_int(ctx->priv_data, name, 0, &value);

    if (ret < 0) {
        fprintf(stderr, "%s: av_opt_get_int(\"%s\") failed: %s\n",
                ctx->codec->name, name, av_err2str(ret));
        return 1;
    }
    if (value != expected) {
        fprintf(stderr, "%s: %s = %"PRId64", expected %"PRId64"\n",
                ctx->codec->name, name, value, expected);
        return 1;
    }
    return 0;
}

static int check_downmix(AVCodecContext *ctx)
{
    AVChannelLayout layout = { 0 };
    char buf[64];
    int ret = av_opt_get_chlayout(ctx->priv_data, "downmix", 0, &layout);

    if (ret < 0) {
        fprintf(stderr, "%s: av_opt_get_chlayout(\"downmix\") failed: %s\n",
                ctx->codec->name, av_err2str(ret));
        return 1;
    }
    av_channel_layout_describe(&layout, buf, sizeof(buf));
    av_channel_layout_uninit(&layout);
    if (strcmp(buf, "stereo") != 0) {
        fprintf(stderr, "%s: downmix = %s, expected stereo\n",
                ctx->codec->name, buf);
        return 1;
    }
    return 0;
}

static int check_options(AVCodecContext *ctx, int has_target_level)
{
    int ret = 0;

    ret |= check_double(ctx, "drc_scale", 0.5);
    ret |= check_int(ctx, "cons_noisegen", 1);
    ret |= check_int(ctx, "heavy_compr", 1);
    if (has_target_level)
        ret |= check_int(ctx, "target_level", -20);
    ret |= check_downmix(ctx);
    return ret;
}

struct ExportValues {
    int64_t dmix_mode;
    double ltrt_cmixlev;
    double ltrt_surmixlev;
    double loro_cmixlev;
    double loro_surmixlev;
};

static int get_export_values(AVCodecContext *ctx, struct ExportValues *values)
{
    if (!av_opt_find(ctx->priv_data, "dmix_mode", NULL, 0, 0))
        return 0;
    if (av_opt_get_int(ctx->priv_data, "dmix_mode", 0,
                       &values->dmix_mode) < 0 ||
        av_opt_get_double(ctx->priv_data, "ltrt_cmixlev", 0,
                          &values->ltrt_cmixlev) < 0 ||
        av_opt_get_double(ctx->priv_data, "ltrt_surmixlev", 0,
                          &values->ltrt_surmixlev) < 0 ||
        av_opt_get_double(ctx->priv_data, "loro_cmixlev", 0,
                          &values->loro_cmixlev) < 0 ||
        av_opt_get_double(ctx->priv_data, "loro_surmixlev", 0,
                          &values->loro_surmixlev) < 0)
        return 1;
    return 0;
}

static int generate_ac3_packet(AVPacket *pkt)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
    AVCodecContext *ctx = NULL;
    AVFrame *frame = NULL;
    int ret = AVERROR_ENCODER_NOT_FOUND;

    if (!codec)
        return ret;

    ctx = avcodec_alloc_context3(codec);
    frame = av_frame_alloc();
    if (!ctx || !frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ctx->sample_rate = 48000;
    ctx->bit_rate    = 128000;
    ctx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    ctx->time_base   = (AVRational){ 1, ctx->sample_rate };
    if ((ret = av_channel_layout_copy(&ctx->ch_layout,
                                      &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0)
        goto end;
    if ((ret = avcodec_open2(ctx, codec, NULL)) < 0)
        goto end;

    frame->nb_samples = ctx->frame_size;
    frame->format     = ctx->sample_fmt;
    frame->pts        = 0;
    if ((ret = av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout)) < 0)
        goto end;
    if ((ret = av_frame_get_buffer(frame, 0)) < 0 ||
        (ret = av_frame_make_writable(frame)) < 0)
        goto end;
    for (int ch = 0; ch < ctx->ch_layout.nb_channels; ch++) {
        float *samples = (float *)frame->extended_data[ch];
        for (int i = 0; i < frame->nb_samples; i++)
            samples[i] = 0.25f * ((i % 64) / 32.0f - 1.0f) * (ch ? -1.0f : 1.0f);
    }

    if ((ret = avcodec_send_frame(ctx, frame)) < 0)
        goto end;
    ret = avcodec_receive_packet(ctx, pkt);
    if (ret < 0)
        goto end;

    /* Drain the encoder so that closing it stays silent. */
    avcodec_send_frame(ctx, NULL);
    {
        AVPacket *tmp = av_packet_alloc();

        if (!tmp) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        while (avcodec_receive_packet(ctx, tmp) == 0)
            av_packet_unref(tmp);
        av_packet_free(&tmp);
    }
    ret = 0;
end:
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return ret;
}

static int decode_one_frame(AVCodecContext *ctx, const AVPacket *src_pkt)
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!pkt || !frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    if ((ret = av_packet_ref(pkt, src_pkt)) < 0)
        goto end;
    ret = avcodec_send_packet(ctx, pkt);
    av_packet_unref(pkt);
    if (ret < 0)
        goto end;
    ret = avcodec_receive_frame(ctx, frame);
end:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return ret;
}

static int check_export_values(AVCodecContext *ctx,
                               const struct ExportValues *expected)
{
    int ret = 0;

    if (!av_opt_find(ctx->priv_data, "dmix_mode", NULL, 0, 0))
        return 0;
    ret |= check_int(ctx, "dmix_mode", expected->dmix_mode);
    ret |= check_double(ctx, "ltrt_cmixlev", expected->ltrt_cmixlev);
    ret |= check_double(ctx, "ltrt_surmixlev", expected->ltrt_surmixlev);
    ret |= check_double(ctx, "loro_cmixlev", expected->loro_cmixlev);
    ret |= check_double(ctx, "loro_surmixlev", expected->loro_surmixlev);
    return ret;
}

static int run_test(const char *name, const AVPacket *pkt)
{
    const AVCodec *codec = avcodec_find_decoder_by_name(name);
    AVCodecContext *ctx;
    struct ExportValues export_values = { 0 };
    int has_target_level;
    int ret = 1;

    if (!codec)
        return 0;

    ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return 1;
    ctx->pkt_timebase = (AVRational){ 1, 48000 };

    has_target_level = !!av_opt_find(ctx->priv_data, "target_level", NULL, 0, 0);

    if (av_opt_set_double(ctx->priv_data, "drc_scale", 0.5, 0) < 0 ||
        av_opt_set_int(ctx->priv_data, "cons_noisegen", 1, 0) < 0 ||
        av_opt_set_int(ctx->priv_data, "heavy_compr", 1, 0) < 0 ||
        (has_target_level &&
         av_opt_set_int(ctx->priv_data, "target_level", -20, 0) < 0) ||
        av_opt_set(ctx->priv_data, "downmix", "stereo", 0) < 0) {
        fprintf(stderr, "%s: failed to set decoder options\n", name);
        goto end;
    }

    if (avcodec_open2(ctx, codec, NULL) < 0) {
        fprintf(stderr, "%s: avcodec_open2() failed\n", name);
        goto end;
    }

    if (check_options(ctx, has_target_level))
        goto end;

    /* Decode a frame first so that the exported downmix fields hold
     * values set from the frame header. Checking them across a flush
     * is only meaningful once they differ from their initial values. */
    if (decode_one_frame(ctx, pkt) < 0) {
        fprintf(stderr, "%s: failed to decode a frame\n", name);
        goto end;
    }
    if (get_export_values(ctx, &export_values))
        goto end;
    if (av_opt_find(ctx->priv_data, "dmix_mode", NULL, 0, 0) &&
        export_values.ltrt_cmixlev == 0.0 &&
        export_values.ltrt_surmixlev == 0.0 &&
        export_values.loro_cmixlev == 0.0 &&
        export_values.loro_surmixlev == 0.0) {
        fprintf(stderr, "%s: exported downmix fields were not set by decoding\n",
                name);
        goto end;
    }

    avcodec_flush_buffers(ctx);
    if (check_options(ctx, has_target_level) ||
        check_export_values(ctx, &export_values))
        goto end;

    /* Flushing twice must not change anything either. */
    avcodec_flush_buffers(ctx);
    if (check_options(ctx, has_target_level) ||
        check_export_values(ctx, &export_values))
        goto end;

    printf("%s: options preserved across avcodec_flush_buffers()\n", name);
    ret = 0;
end:
    avcodec_free_context(&ctx);
    return ret;
}

int main(void)
{
    static const char *const decoder_names[] = { "ac3", "eac3", "ac3_fixed" };
    AVPacket *pkt = av_packet_alloc();
    int tested = 0;
    int ret;

    if (!pkt)
        return 1;
    ret = generate_ac3_packet(pkt);
    if (ret < 0) {
        fprintf(stderr, "failed to generate an AC-3 packet: %s\n",
                av_err2str(ret));
        av_packet_free(&pkt);
        return 1;
    }

    ret = 0;
    for (int i = 0; i < (int)(sizeof(decoder_names) / sizeof(decoder_names[0])); i++) {
        if (!avcodec_find_decoder_by_name(decoder_names[i]))
            continue;
        tested++;
        ret |= run_test(decoder_names[i], pkt);
    }
    av_packet_free(&pkt);

    if (!tested) {
        fprintf(stderr, "no (E-)AC-3 decoder available\n");
        return 1;
    }
    return ret;
}
