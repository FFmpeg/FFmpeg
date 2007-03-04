/*
 * NuppelVideo decoder
 * Copyright (c) 2006 Reimar Doeffinger
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
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "avcodec.h"

#include "bswap.h"
#include "dsputil.h"
#include "lzo.h"
#include "rtjpeg.h"

typedef struct {
    AVFrame pic;
    int width, height;
    unsigned int decomp_size;
    unsigned char* decomp_buf;
    uint32_t lq[64], cq[64];
    RTJpegContext rtj;
    DSPContext dsp;
} NuvContext;

/**
 * \brief copy frame data from buffer to AVFrame, handling stride.
 * \param f destination AVFrame
 * \param src source buffer, does not use any line-stride
 * \param width width of the video frame
 * \param height height of the video frame
 */
static void copy_frame(AVFrame *f, uint8_t *src,
                       int width, int height) {
    AVPicture pic;
    avpicture_fill(&pic, src, PIX_FMT_YUV420P, width, height);
    av_picture_copy((AVPicture *)f, &pic, PIX_FMT_YUV420P, width, height);
}

/**
 * \brief extract quantization tables from codec data into our context
 */
static int get_quant(AVCodecContext *avctx, NuvContext *c,
                     uint8_t *buf, int size) {
    int i;
    if (size < 2 * 64 * 4) {
        av_log(avctx, AV_LOG_ERROR, "insufficient rtjpeg quant data\n");
        return -1;
    }
    for (i = 0; i < 64; i++, buf += 4)
        c->lq[i] = AV_RL32(buf);
    for (i = 0; i < 64; i++, buf += 4)
        c->cq[i] = AV_RL32(buf);
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        uint8_t *buf, int buf_size) {
    NuvContext *c = (NuvContext *)avctx->priv_data;
    AVFrame *picture = data;
    int orig_size = buf_size;
    enum {NUV_UNCOMPRESSED = '0', NUV_RTJPEG = '1',
          NUV_RTJPEG_IN_LZO = '2', NUV_LZO = '3',
          NUV_BLACK = 'N', NUV_COPY_LAST = 'L'} comptype;

    if (buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "coded frame too small\n");
        return -1;
    }

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_READABLE |
                          FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->get_buffer(avctx, &c->pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    // codec data (rtjpeg quant tables)
    if (buf[0] == 'D' && buf[1] == 'R') {
        int ret;
        // skip rest of the frameheader.
        buf = &buf[12];
        buf_size -= 12;
        ret = get_quant(avctx, c, buf, buf_size);
        if (ret < 0)
            return ret;
        rtjpeg_decode_init(&c->rtj, &c->dsp, c->width, c->height, c->lq, c->cq);
        return orig_size;
    }

    if (buf[0] != 'V' || buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "not a nuv video frame\n");
        return -1;
    }
    comptype = buf[1];
    // skip rest of the frameheader.
    buf = &buf[12];
    buf_size -= 12;

    c->pic.pict_type = FF_I_TYPE;
    c->pic.key_frame = 1;
    // decompress/copy/whatever data
    switch (comptype) {
        case NUV_UNCOMPRESSED: {
            int height = c->height;
            if (buf_size < c->width * height * 3 / 2) {
                av_log(avctx, AV_LOG_ERROR, "uncompressed frame too short\n");
                height = buf_size / c->width / 3 * 2;
            }
            copy_frame(&c->pic, buf, c->width, height);
            break;
        }
        case NUV_RTJPEG: {
            rtjpeg_decode_frame_yuv420(&c->rtj, &c->pic, buf, buf_size);
            break;
        }
        case NUV_RTJPEG_IN_LZO: {
            int outlen = c->decomp_size, inlen = buf_size;
            if (lzo1x_decode(c->decomp_buf, &outlen, buf, &inlen))
                av_log(avctx, AV_LOG_ERROR, "error during lzo decompression\n");
            rtjpeg_decode_frame_yuv420(&c->rtj, &c->pic, c->decomp_buf, c->decomp_size);
            break;
        }
        case NUV_LZO: {
            int outlen = c->decomp_size, inlen = buf_size;
            if (lzo1x_decode(c->decomp_buf, &outlen, buf, &inlen))
                av_log(avctx, AV_LOG_ERROR, "error during lzo decompression\n");
            copy_frame(&c->pic, c->decomp_buf, c->width, c->height);
            break;
        }
        case NUV_BLACK: {
            memset(c->pic.data[0], 0, c->width * c->height);
            memset(c->pic.data[1], 128, c->width * c->height / 4);
            memset(c->pic.data[2], 128, c->width * c->height / 4);
            break;
        }
        case NUV_COPY_LAST: {
            c->pic.pict_type = FF_P_TYPE;
            c->pic.key_frame = 0;
            /* nothing more to do here */
            break;
        }
        default:
            av_log(avctx, AV_LOG_ERROR, "unknown compression\n");
            return -1;
    }

    *picture = c->pic;
    *data_size = sizeof(AVFrame);
    return orig_size;
}

static int decode_init(AVCodecContext *avctx) {
    NuvContext *c = (NuvContext *)avctx->priv_data;
    avctx->width = (avctx->width + 1) & ~1;
    avctx->height = (avctx->height + 1) & ~1;
    if (avcodec_check_dimensions(avctx, avctx->height, avctx->width) < 0) {
        return 1;
    }
    avctx->has_b_frames = 0;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    c->pic.data[0] = NULL;
    c->width = avctx->width;
    c->height = avctx->height;
    c->decomp_size = c->height * c->width * 3 / 2;
    c->decomp_buf = av_malloc(c->decomp_size + LZO_OUTPUT_PADDING);
    if (!c->decomp_buf) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
        return 1;
    }
    dsputil_init(&c->dsp, avctx);
    if (avctx->extradata_size)
        get_quant(avctx, c, avctx->extradata, avctx->extradata_size);
    rtjpeg_decode_init(&c->rtj, &c->dsp, c->width, c->height, c->lq, c->cq);
    return 0;
}

static int decode_end(AVCodecContext *avctx) {
    NuvContext *c = (NuvContext *)avctx->priv_data;
    av_freep(&c->decomp_buf);
    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    return 0;
}

AVCodec nuv_decoder = {
    "nuv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_NUV,
    sizeof(NuvContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};

