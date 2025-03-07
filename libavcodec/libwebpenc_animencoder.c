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

#include "libavutil/buffer.h"
#include "libavutil/mem.h"

#include "codec_internal.h"
#include "encode.h"
#include "libwebpenc_common.h"

#include <webp/mux.h>

typedef struct LibWebPAnimContext {
    LibWebPContextCommon cc;
    WebPAnimEncoder *enc;     // the main AnimEncoder object
    int64_t first_frame_pts;  // pts of the first encoded frame.
    int64_t end_pts;          // pts + duration of the last frame

    void           *first_frame_opaque;
    AVBufferRef    *first_frame_opaque_ref;

    int done;                 // If true, we have assembled the bitstream already
} LibWebPAnimContext;

static av_cold int libwebp_anim_encode_init(AVCodecContext *avctx)
{
    int ret = ff_libwebp_encode_init_common(avctx);
    if (!ret) {
        LibWebPAnimContext *s = avctx->priv_data;
        WebPAnimEncoderOptions enc_options = { { 0 } };
        WebPAnimEncoderOptionsInit(&enc_options);
        enc_options.verbose = av_log_get_level() >= AV_LOG_VERBOSE;
        // TODO(urvang): Expose some options on command-line perhaps.
        s->enc = WebPAnimEncoderNew(avctx->width, avctx->height, &enc_options);
        if (!s->enc)
            return AVERROR(EINVAL);
        s->first_frame_pts = AV_NOPTS_VALUE;
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
                ret = ff_get_encode_buffer(avctx, pkt, assembled_data.size, 0);
                if (ret < 0) {
                    WebPDataClear(&assembled_data);
                    return ret;
                }
                memcpy(pkt->data, assembled_data.bytes, assembled_data.size);
                WebPDataClear(&assembled_data);
                s->done = 1;
                pkt->pts = s->first_frame_pts;

                if (pkt->pts != AV_NOPTS_VALUE && s->end_pts > pkt->pts)
                    pkt->duration = s->end_pts - pkt->pts;

                if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
                    pkt->opaque               = s->first_frame_opaque;
                    pkt->opaque_ref           = s->first_frame_opaque_ref;
                    s->first_frame_opaque_ref = NULL;
                }

                *got_packet = 1;
                return 0;
            } else {
                WebPDataClear(&assembled_data);
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

        if (!avctx->frame_num) {
            s->first_frame_pts = frame->pts;

            if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
                s->first_frame_opaque = frame->opaque;
                ret = av_buffer_replace(&s->first_frame_opaque_ref, frame->opaque_ref);
                if (ret < 0)
                    goto end;
            }
        }

        if (frame->pts != AV_NOPTS_VALUE)
            s->end_pts = frame->pts + frame->duration;

        ret = 0;
        *got_packet = 0;

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

    av_buffer_unref(&s->first_frame_opaque_ref);

    return 0;
}

const FFCodec ff_libwebp_anim_encoder = {
    .p.name         = "libwebp_anim",
    CODEC_LONG_NAME("libwebp WebP image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WEBP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS_ARRAY(ff_libwebpenc_pix_fmts),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &ff_libwebpenc_class,
    .p.wrapper_name = "libwebp",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .priv_data_size = sizeof(LibWebPAnimContext),
    .defaults       = ff_libwebp_defaults,
    .init           = libwebp_anim_encode_init,
    FF_CODEC_ENCODE_CB(libwebp_anim_encode_frame),
    .close          = libwebp_anim_encode_close,
};
