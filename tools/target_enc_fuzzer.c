/*
 * Copyright (c) 2024 Michael Niedermayer <michael-ffmpeg@niedermayer.cc>
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
 *
 * Based on target_dec_fuzzer
 */

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/codec_internal.h"
#include "libavformat/avformat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

extern const FFCodec * codec_list[];

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static const FFCodec *c = NULL;

// Ensure we don't loop forever
const uint32_t maxiteration = 8096;


static int encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret;

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN)) {
            return 0;
        } else if (ret < 0) {
            return ret;
        }

        av_packet_unref(pkt);
    }
    av_assert0(0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint64_t maxpixels_per_frame = 512 * 512;
    uint64_t maxpixels;

    const uint8_t *end = data + size;
    uint32_t it = 0;
    uint64_t nb_samples = 0;
    AVDictionary *opts = NULL;
    uint64_t ec_pixels = 0;

    if (!c) {
#define ENCODER_SYMBOL0(CODEC) ff_##CODEC##_encoder
#define ENCODER_SYMBOL(CODEC) ENCODER_SYMBOL0(CODEC)
        extern FFCodec ENCODER_SYMBOL(FFMPEG_ENCODER);
        codec_list[0] = &ENCODER_SYMBOL(FFMPEG_ENCODER);

        c = &ENCODER_SYMBOL(FFMPEG_ENCODER);
        av_log_set_level(AV_LOG_PANIC);
    }

    if (c->p.type != AVMEDIA_TYPE_VIDEO)
        return 0;

    maxpixels = maxpixels_per_frame * maxiteration;
    switch (c->p.id) {
    case AV_CODEC_ID_A64_MULTI:         maxpixels  /= 65536;  break;
    case AV_CODEC_ID_A64_MULTI5:        maxpixels  /= 65536;  break;
    }

    maxpixels_per_frame  = FFMIN(maxpixels_per_frame , maxpixels);

    AVCodecContext* ctx = avcodec_alloc_context3(&c->p);
    if (!ctx)
        error("Failed memory allocation");

    if (ctx->max_pixels == 0 || ctx->max_pixels > maxpixels_per_frame)
        ctx->max_pixels = maxpixels_per_frame; //To reduce false positive OOM and hangs

    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if (size > 1024) {
        GetByteContext gbc;
        int flags;
        int64_t flags64;

        size -= 1024;
        bytestream2_init(&gbc, data + size, 1024);
        ctx->width                              = bytestream2_get_le32(&gbc) & 0xFFFF;
        ctx->height                             = bytestream2_get_le32(&gbc) & 0xFFFF;
        ctx->bit_rate                           = bytestream2_get_le64(&gbc);
        ctx->gop_size                           = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->max_b_frames                       = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->time_base.num                      = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->time_base.den                      = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->framerate.num                      = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->framerate.den                      = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;

        flags = bytestream2_get_byte(&gbc);
        if (flags & 2)
            ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

        if (flags & 0x40)
            av_force_cpu_flags(0);

        flags64 = bytestream2_get_le64(&gbc);

        if (c->p.pix_fmts) {
            int npixfmts = 0;
            while (c->p.pix_fmts[npixfmts++] != AV_PIX_FMT_NONE)
                ;
            ctx->pix_fmt = c->p.pix_fmts[bytestream2_get_byte(&gbc) % npixfmts];
        }

        switch (c->p.id) {
        case AV_CODEC_ID_FFV1:{
            int coder = bytestream2_get_byte(&gbc)&3;
            if (coder == 3) coder = -2;
            av_dict_set_int(&opts, "coder", coder, 0);
            av_dict_set_int(&opts, "context", bytestream2_get_byte(&gbc)&1, 0);
            av_dict_set_int(&opts, "slicecrc", bytestream2_get_byte(&gbc)&1, 0);
            break;}
        }
    }
    if (ctx->width == 0 || av_image_check_size(ctx->width, ctx->height, 0, ctx))
        ctx->width = ctx->height = 64;

    int res = avcodec_open2(ctx, &c->p, &opts);
    if (res < 0) {
        avcodec_free_context(&ctx);
        av_dict_free(&opts);
        return 0; // Failure of avcodec_open2() does not imply that a issue was found
    }


    AVFrame *frame = av_frame_alloc();
    AVPacket *avpkt = av_packet_alloc();
    if (!frame || !avpkt)
        error("Failed memory allocation");

    frame->format = ctx->pix_fmt;
    frame->width  = ctx->width;
    frame->height = ctx->height;

    while (data < end && it < maxiteration) {
        ec_pixels += (ctx->width + 32LL) * (ctx->height + 32LL);
        if (ec_pixels > maxpixels)
            goto maximums_reached;

        res = av_frame_get_buffer(frame, 0);
        if (res < 0)
            error("Failed av_frame_get_buffer");

        for (int i=0; i<FF_ARRAY_ELEMS(frame->buf); i++) {
            if (frame->buf[i]) {
                int buf_size = FFMIN(end-data, frame->buf[i]->size);
                memcpy(frame->buf[i]->data, data, buf_size);
                memset(frame->buf[i]->data + buf_size, 0, frame->buf[i]->size - buf_size);
                data += buf_size;
            }
        }

        frame->pts = nb_samples;

        res = encode(ctx, frame, avpkt);
        if (res < 0)
            break;
        it++;
        for (int i=0; i<FF_ARRAY_ELEMS(frame->buf); i++)
            av_buffer_unref(&frame->buf[i]);

        av_packet_unref(avpkt);
    }
maximums_reached:
    encode(ctx, NULL, avpkt);
    av_packet_unref(avpkt);

//     fprintf(stderr, "frames encoded: %"PRId64",  iterations: %d\n", nb_samples  , it);

    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    av_packet_free(&avpkt);
    av_dict_free(&opts);
    return 0;
}
