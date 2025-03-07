/*
 * WebP encoding support via libwebp
 * Copyright (c) 2013 Justin Ruggles <justin.ruggles@gmail.com>
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
 * WebP encoder using libwebp (WebPEncode API)
 */

#include "libavutil/mem.h"
#include "codec_internal.h"
#include "encode.h"
#include "libwebpenc_common.h"

typedef LibWebPContextCommon LibWebPContext;

static av_cold int libwebp_encode_init(AVCodecContext *avctx)
{
    return ff_libwebp_encode_init_common(avctx);
}

static int libwebp_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    LibWebPContext *s  = avctx->priv_data;
    WebPPicture *pic = NULL;
    AVFrame *alt_frame = NULL;
    WebPMemoryWriter mw = { 0 };

    int ret = ff_libwebp_get_frame(avctx, s, frame, &alt_frame, &pic);
    if (ret < 0)
        goto end;

    WebPMemoryWriterInit(&mw);
    pic->custom_ptr = &mw;
    pic->writer     = WebPMemoryWrite;

    ret = WebPEncode(&s->config, pic);
    if (!ret) {
        av_log(avctx, AV_LOG_ERROR, "WebPEncode() failed with error: %d\n",
               pic->error_code);
        ret = ff_libwebp_error_to_averror(pic->error_code);
        goto end;
    }

    ret = ff_get_encode_buffer(avctx, pkt, mw.size, 0);
    if (ret < 0)
        goto end;
    memcpy(pkt->data, mw.mem, mw.size);

    *got_packet = 1;

end:
#if (WEBP_ENCODER_ABI_VERSION > 0x0203)
    WebPMemoryWriterClear(&mw);
#else
    free(mw.mem); /* must use free() according to libwebp documentation */
#endif
    WebPPictureFree(pic);
    av_freep(&pic);
    av_frame_free(&alt_frame);

    return ret;
}

static int libwebp_encode_close(AVCodecContext *avctx)
{
    LibWebPContextCommon *s  = avctx->priv_data;
    av_frame_free(&s->ref);

    return 0;
}

const FFCodec ff_libwebp_encoder = {
    .p.name         = "libwebp",
    CODEC_LONG_NAME("libwebp WebP image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WEBP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS_ARRAY(ff_libwebpenc_pix_fmts),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &ff_libwebpenc_class,
    .p.wrapper_name = "libwebp",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .priv_data_size = sizeof(LibWebPContext),
    .defaults       = ff_libwebp_defaults,
    .init           = libwebp_encode_init,
    FF_CODEC_ENCODE_CB(libwebp_encode_frame),
    .close          = libwebp_encode_close,
};
