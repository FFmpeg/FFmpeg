/*
 * WebP encoding support via libwebp
 * Copyright (c) 2015 Urvang Joshi
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
 * WebP encoder using libwebp (WebPAnimEncoder API)
 */

#include "config.h"
#include "libwebpenc_common.h"

#include <webp/mux.h>

typedef struct LibWebPAnimContext {
    LibWebPContextCommon cc;
    WebPAnimEncoder *enc;     // the main AnimEncoder object
    int64_t prev_frame_pts;   // pts of the previously encoded frame.
    int done;                 // If true, we have assembled the bitstream already
} LibWebPAnimContext;

static av_cold int libwebp_anim_encode_init(AVCodecContext *avctx)
{
    int ret = ff_libwebp_encode_init_common(avctx);
    if (!ret) {
        LibWebPAnimContext *s = avctx->priv_data;
        WebPAnimEncoderOptions enc_options;
        WebPAnimEncoderOptionsInit(&enc_options);
        // TODO(urvang): Expose some options on command-line perhaps.
        s->enc = WebPAnimEncoderNew(avctx->width, avctx->height, &enc_options);
        if (!s->enc)
            return AVERROR(EINVAL);
        s->prev_frame_pts = -1;
        s->done = 0;
    }
    return ret;
}

static int libwebp_anim_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                     const AVFrame *frame, int *got_packet) {
    LibWebPAnimContext *s = avctx->priv_data;
    int ret;

    if (!frame) {
        if (s->done) {  // Second flush: return empty package to denote finish.
            *got_packet = 0;
            return 0;
        } else {  // First flush: assemble bitstream and return it.
            WebPData assembled_data = { 0 };
            ret = WebPAnimEncoderAssemble(s->enc, &assembled_data);
            if (ret) {
                ret = ff_alloc_packet(pkt, assembled_data.size);
                if (ret < 0)
                    return ret;
                memcpy(pkt->data, assembled_data.bytes, assembled_data.size);
                s->done = 1;
                pkt->flags |= AV_PKT_FLAG_KEY;
                pkt->pts = pkt->dts = s->prev_frame_pts + 1;
                *got_packet = 1;
                return 0;
            } else {
                av_log(s, AV_LOG_ERROR,
                       "WebPAnimEncoderAssemble() failed with error: %d\n",
                       VP8_ENC_ERROR_OUT_OF_MEMORY);
                return AVERROR(ENOMEM);
            }
        }
    } else {
        int timestamp_ms;
        WebPPicture *pic = NULL;
        AVFrame *alt_frame = NULL;
        ret = ff_libwebp_get_frame(avctx, &s->cc, frame, &alt_frame, &pic);
        if (ret < 0)
            goto end;

        timestamp_ms =
            avctx->time_base.num * frame->pts * 1000 / avctx->time_base.den;
        ret = WebPAnimEncoderAdd(s->enc, pic, timestamp_ms, &s->cc.config);
        if (!ret) {
                av_log(avctx, AV_LOG_ERROR,
                       "Encoding WebP frame failed with error: %d\n",
                   pic->error_code);
            ret = ff_libwebp_error_to_averror(pic->error_code);
            goto end;
        }

        pkt->pts = pkt->dts = frame->pts;
        s->prev_frame_pts = frame->pts;  // Save for next frame.
        ret = 0;
        *got_packet = 1;

end:
        WebPPictureFree(pic);
        av_freep(&pic);
        av_frame_free(&alt_frame);
        return ret;
    }
}

static int libwebp_anim_encode_close(AVCodecContext *avctx)
{
    LibWebPAnimContext *s = avctx->priv_data;
    av_frame_free(&s->cc.ref);
    WebPAnimEncoderDelete(s->enc);

    return 0;
}

static const AVClass class = {
    .class_name = "libwebp_anim",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libwebp_anim_encoder = {
    .name           = "libwebp_anim",
    .long_name      = NULL_IF_CONFIG_SMALL("libwebp WebP image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WEBP,
    .priv_data_size = sizeof(LibWebPAnimContext),
    .init           = libwebp_anim_encode_init,
    .encode2        = libwebp_anim_encode_frame,
    .close          = libwebp_anim_encode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB32,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    },
    .priv_class     = &class,
    .defaults       = libwebp_defaults,
};
