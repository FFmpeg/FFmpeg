/*
 * LOCO codec
 * Copyright (c) 2005 Konstantin Shishkov
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
 * LOCO codec.
 */

#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "mathops.h"

enum LOCO_MODE {LOCO_UNKN=0, LOCO_CYUY2=-1, LOCO_CRGB=-2, LOCO_CRGBA=-3, LOCO_CYV12=-4,
 LOCO_YUY2=1, LOCO_UYVY=2, LOCO_RGB=3, LOCO_RGBA=4, LOCO_YV12=5};

typedef struct LOCOContext{
    AVCodecContext *avctx;
    AVFrame pic;
    int lossy;
    int mode;
} LOCOContext;

typedef struct RICEContext{
    GetBitContext gb;
    int save, run, run2; /* internal rice decoder state */
    int sum, count; /* sum and count for getting rice parameter */
    int lossy;
}RICEContext;

static int loco_get_rice_param(RICEContext *r)
{
    int cnt = 0;
    int val = r->count;

    while(r->sum > val && cnt < 9) {
        val <<= 1;
        cnt++;
    }

    return cnt;
}

static inline void loco_update_rice_param(RICEContext *r, int val)
{
    r->sum += val;
    r->count++;

    if(r->count == 16) {
        r->sum >>= 1;
        r->count >>= 1;
    }
}

static inline int loco_get_rice(RICEContext *r)
{
    int v;
    if (r->run > 0) { /* we have zero run */
        r->run--;
        loco_update_rice_param(r, 0);
        return 0;
    }
    v = get_ur_golomb_jpegls(&r->gb, loco_get_rice_param(r), INT_MAX, 0);
    loco_update_rice_param(r, (v+1)>>1);
    if (!v) {
        if (r->save >= 0) {
            r->run = get_ur_golomb_jpegls(&r->gb, 2, INT_MAX, 0);
            if(r->run > 1)
                r->save += r->run + 1;
            else
                r->save -= 3;
        }
        else
            r->run2++;
    } else {
        v = ((v>>1) + r->lossy) ^ -(v&1);
        if (r->run2 > 0) {
            if (r->run2 > 2)
                r->save += r->run2;
            else
                r->save -= 3;
            r->run2 = 0;
        }
    }

    return v;
}

/* LOCO main predictor - LOCO-I/JPEG-LS predictor */
static inline int loco_predict(uint8_t* data, int stride, int step)
{
    int a, b, c;

    a = data[-stride];
    b = data[-step];
    c = data[-stride - step];

    return mid_pred(a, a + b - c, b);
}

static int loco_decode_plane(LOCOContext *l, uint8_t *data, int width, int height,
                             int stride, const uint8_t *buf, int buf_size, int step)
{
    RICEContext rc;
    int val;
    int i, j;

    init_get_bits(&rc.gb, buf, buf_size*8);
    rc.save = 0;
    rc.run = 0;
    rc.run2 = 0;
    rc.lossy = l->lossy;

    rc.sum = 8;
    rc.count = 1;

    /* restore top left pixel */
    val = loco_get_rice(&rc);
    data[0] = 128 + val;
    /* restore top line */
    for (i = 1; i < width; i++) {
        val = loco_get_rice(&rc);
        data[i * step] = data[i * step - step] + val;
    }
    data += stride;
    for (j = 1; j < height; j++) {
        /* restore left column */
        val = loco_get_rice(&rc);
        data[0] = data[-stride] + val;
        /* restore all other pixels */
        for (i = 1; i < width; i++) {
            val = loco_get_rice(&rc);
            data[i * step] = loco_predict(&data[i * step], stride, step) + val;
        }
        data += stride;
    }

    return (get_bits_count(&rc.gb) + 7) >> 3;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LOCOContext * const l = avctx->priv_data;
    AVFrame * const p= (AVFrame*)&l->pic;
    int decoded;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->key_frame = 1;

    switch(l->mode) {
    case LOCO_CYUY2: case LOCO_YUY2: case LOCO_UYVY:
        decoded = loco_decode_plane(l, p->data[0], avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 1);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[1], avctx->width / 2, avctx->height,
                                    p->linesize[1], buf, buf_size, 1);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[2], avctx->width / 2, avctx->height,
                                    p->linesize[2], buf, buf_size, 1);
        break;
    case LOCO_CYV12: case LOCO_YV12:
        decoded = loco_decode_plane(l, p->data[0], avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 1);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[2], avctx->width / 2, avctx->height / 2,
                                    p->linesize[2], buf, buf_size, 1);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[1], avctx->width / 2, avctx->height / 2,
                                    p->linesize[1], buf, buf_size, 1);
        break;
    case LOCO_CRGB: case LOCO_RGB:
        decoded = loco_decode_plane(l, p->data[0] + p->linesize[0]*(avctx->height-1), avctx->width, avctx->height,
                                    -p->linesize[0], buf, buf_size, 3);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[0] + p->linesize[0]*(avctx->height-1) + 1, avctx->width, avctx->height,
                                    -p->linesize[0], buf, buf_size, 3);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[0] + p->linesize[0]*(avctx->height-1) + 2, avctx->width, avctx->height,
                                    -p->linesize[0], buf, buf_size, 3);
        break;
    case LOCO_RGBA:
        decoded = loco_decode_plane(l, p->data[0], avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 4);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[0] + 1, avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 4);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[0] + 2, avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 4);
        buf += decoded; buf_size -= decoded;
        decoded = loco_decode_plane(l, p->data[0] + 3, avctx->width, avctx->height,
                                    p->linesize[0], buf, buf_size, 4);
        break;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = l->pic;

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx){
    LOCOContext * const l = avctx->priv_data;
    int version;

    l->avctx = avctx;
    if (avctx->extradata_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "Extradata size must be >= 12 instead of %i\n",
               avctx->extradata_size);
        return -1;
    }
    version = AV_RL32(avctx->extradata);
    switch(version) {
    case 1:
        l->lossy = 0;
        break;
    case 2:
        l->lossy = AV_RL32(avctx->extradata + 8);
        break;
    default:
        l->lossy = AV_RL32(avctx->extradata + 8);
        av_log_ask_for_sample(avctx, "This is LOCO codec version %i.\n", version);
    }

    l->mode = AV_RL32(avctx->extradata + 4);
    switch(l->mode) {
    case LOCO_CYUY2: case LOCO_YUY2: case LOCO_UYVY:
        avctx->pix_fmt = PIX_FMT_YUV422P;
        break;
    case LOCO_CRGB: case LOCO_RGB:
        avctx->pix_fmt = PIX_FMT_BGR24;
        break;
    case LOCO_CYV12: case LOCO_YV12:
        avctx->pix_fmt = PIX_FMT_YUV420P;
        break;
    case LOCO_CRGBA: case LOCO_RGBA:
        avctx->pix_fmt = PIX_FMT_RGB32;
        break;
    default:
        av_log(avctx, AV_LOG_INFO, "Unknown colorspace, index = %i\n", l->mode);
        return -1;
    }
    if(avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_INFO, "lossy:%i, version:%i, mode: %i\n", l->lossy, version, l->mode);

    avcodec_get_frame_defaults(&l->pic);

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx){
    LOCOContext * const l = avctx->priv_data;
    AVFrame *pic = &l->pic;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    return 0;
}

AVCodec ff_loco_decoder = {
    "loco",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_LOCO,
    sizeof(LOCOContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("LOCO"),
};
