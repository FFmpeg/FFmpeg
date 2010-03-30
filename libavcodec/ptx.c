/*
 * V.Flash PTX (.ptx) image decoder
 * Copyright (c) 2007 Ivo van Poorten
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
#include "avcodec.h"

typedef struct PTXContext {
    AVFrame picture;
} PTXContext;

static av_cold int ptx_init(AVCodecContext *avctx) {
    PTXContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;

    return 0;
}

static int ptx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    PTXContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p = &s->picture;
    unsigned int offset, w, h, y, stride, bytes_per_pixel;
    uint8_t *ptr;

    offset          = AV_RL16(buf);
    w               = AV_RL16(buf+8);
    h               = AV_RL16(buf+10);
    bytes_per_pixel = AV_RL16(buf+12) >> 3;

    if (bytes_per_pixel != 2) {
        av_log(avctx, AV_LOG_ERROR, "image format is not rgb15, please report on ffmpeg-users mailing list\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_RGB555;

    if (offset != 0x2c)
        av_log(avctx, AV_LOG_WARNING, "offset != 0x2c, untested due to lack of sample files\n");

    buf += offset;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (avcodec_check_dimensions(avctx, w, h))
        return -1;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type = FF_I_TYPE;

    ptr    = p->data[0];
    stride = p->linesize[0];

    for (y=0; y<h; y++) {
#if HAVE_BIGENDIAN
        unsigned int x;
        for (x=0; x<w*bytes_per_pixel; x+=bytes_per_pixel)
            AV_WN16(ptr+x, AV_RL16(buf+x));
#else
        memcpy(ptr, buf, w*bytes_per_pixel);
#endif
        ptr += stride;
        buf += w*bytes_per_pixel;
    }

    *picture = s->picture;
    *data_size = sizeof(AVPicture);

    return offset + w*h*bytes_per_pixel;
}

static av_cold int ptx_end(AVCodecContext *avctx) {
    PTXContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ptx_decoder = {
    "ptx",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PTX,
    sizeof(PTXContext),
    ptx_init,
    NULL,
    ptx_end,
    ptx_decode_frame,
    CODEC_CAP_DR1,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("V.Flash PTX image"),
};
