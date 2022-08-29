/*
 * BitJazz SheerVideo decoder
 * Copyright (c) 2016 Paul B Mahol
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

#define CACHED_BITSTREAM_READER !ARCH_X86_32
#define SHEER_VLC_BITS 12

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "thread.h"
#include "sheervideodata.h"

typedef struct SheerVideoContext {
    unsigned format;
    int alt;
    VLC vlc[2];
    void (*decode_frame)(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb);
} SheerVideoContext;

static void decode_ca4i(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_a = (uint16_t *)p->data[3];
    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 10);
                dst_y[x] = get_bits(gb, 10);
                dst_u[x] = get_bits(gb, 10);
                dst_v[x] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 502, 512, 512, 502 };

            for (x = 0; x < avctx->width; x++) {
                int y, u, v, a;

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred[3] = (a + pred[3]) & 0x3ff;
                dst_y[x] = pred[0] = (y + pred[0]) & 0x3ff;
                dst_u[x] = pred[1] = (u + pred[1]) & 0x3ff;
                dst_v[x] = pred[2] = (v + pred[2]) & 0x3ff;
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_ca4p(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_a = (uint16_t *)p->data[3];
    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_a[x] = get_bits(gb, 10);
            dst_y[x] = get_bits(gb, 10);
            dst_u[x] = get_bits(gb, 10);
            dst_v[x] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 502, 512, 512, 502 };

        for (x = 0; x < avctx->width; x++) {
            int y, u, v, a;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_a[x] = pred[3] = (a + pred[3]) & 0x3ff;
            dst_y[x] = pred[0] = (y + pred[0]) & 0x3ff;
            dst_u[x] = pred[1] = (u + pred[1]) & 0x3ff;
            dst_v[x] = pred[2] = (v + pred[2]) & 0x3ff;
        }
    }

    dst_y += p->linesize[0] / 2;
    dst_u += p->linesize[1] / 2;
    dst_v += p->linesize[2] / 2;
    dst_a += p->linesize[3] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 10);
                dst_y[x] = get_bits(gb, 10);
                dst_u[x] = get_bits(gb, 10);
                dst_v[x] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int y, u, v, a;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0] / 2];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1] / 2];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2] / 2];
            pred_TL[3] = pred_L[3] = dst_a[-p->linesize[3] / 2];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_y[-p->linesize[0] / 2 + x];
                pred_T[1] = dst_u[-p->linesize[1] / 2 + x];
                pred_T[2] = dst_v[-p->linesize[2] / 2 + x];
                pred_T[3] = dst_a[-p->linesize[3] / 2 + x];

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred_L[3] = (a + ((3 * (pred_T[3] + pred_L[3]) - 2 * pred_TL[3]) >> 2)) & 0x3ff;
                dst_y[x] = pred_L[0] = (y + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_u[x] = pred_L[1] = (u + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0x3ff;
                dst_v[x] = pred_L[2] = (v + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0x3ff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[3] = pred_T[3];
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_ybr10i(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_y[x] = get_bits(gb, 10);
                dst_u[x] = get_bits(gb, 10);
                dst_v[x] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 502, 512, 512, 512 };

            for (x = 0; x < avctx->width; x++) {
                int y, u, v;

                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x] = pred[0] = (y + pred[0]) & 0x3ff;
                dst_u[x] = pred[1] = (u + pred[1]) & 0x3ff;
                dst_v[x] = pred[2] = (v + pred[2]) & 0x3ff;
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
    }
}

static void decode_ybr10(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_y[x] = get_bits(gb, 10);
            dst_u[x] = get_bits(gb, 10);
            dst_v[x] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 502, 512, 512, 512 };

        for (x = 0; x < avctx->width; x++) {
            int y, u, v;

            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x] = pred[0] = (y + pred[0]) & 0x3ff;
            dst_u[x] = pred[1] = (u + pred[1]) & 0x3ff;
            dst_v[x] = pred[2] = (v + pred[2]) & 0x3ff;
        }
    }

    dst_y += p->linesize[0] / 2;
    dst_u += p->linesize[1] / 2;
    dst_v += p->linesize[2] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_y[x] = get_bits(gb, 10);
                dst_u[x] = get_bits(gb, 10);
                dst_v[x] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int y, u, v;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0] / 2];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1] / 2];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2] / 2];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_y[-p->linesize[0] / 2 + x];
                pred_T[1] = dst_u[-p->linesize[1] / 2 + x];
                pred_T[2] = dst_v[-p->linesize[2] / 2 + x];

                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x] = pred_L[0] = (y + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_u[x] = pred_L[1] = (u + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0x3ff;
                dst_v[x] = pred_L[2] = (v + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0x3ff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
    }
}

static void decode_yry10i(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_y[x    ] = get_bits(gb, 10);
                dst_u[x / 2] = get_bits(gb, 10);
                dst_y[x + 1] = get_bits(gb, 10);
                dst_v[x / 2] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 502, 512, 512, 0 };

            for (x = 0; x < avctx->width; x += 2) {
                int y1, y2, u, v;

                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0x3ff;
                dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0x3ff;
                dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0x3ff;
                dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0x3ff;
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
    }
}

static void decode_yry10(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_y[x    ] = get_bits(gb, 10);
            dst_u[x / 2] = get_bits(gb, 10);
            dst_y[x + 1] = get_bits(gb, 10);
            dst_v[x / 2] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 502, 512, 512, 0 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v;

            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0x3ff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0x3ff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0x3ff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0x3ff;
        }
    }

    dst_y += p->linesize[0] / 2;
    dst_u += p->linesize[1] / 2;
    dst_v += p->linesize[2] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_y[x    ] = get_bits(gb, 10);
                dst_u[x / 2] = get_bits(gb, 10);
                dst_y[x + 1] = get_bits(gb, 10);
                dst_v[x / 2] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[6], pred_L[6], pred_T[6];
            int y1, y2, u, v;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0] / 2];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1] / 2];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2] / 2];

            for (x = 0; x < avctx->width; x += 2) {
                pred_T[0] = dst_y[-p->linesize[0] / 2 + x];
                pred_T[3] = dst_y[-p->linesize[0] / 2 + x + 1];
                pred_T[1] = dst_u[-p->linesize[1] / 2 + x / 2];
                pred_T[2] = dst_v[-p->linesize[2] / 2 + x / 2];

                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_u[x / 2] = pred_L[1] = (u + (((pred_L[1] - pred_TL[1]) >> 1) + pred_T[1])) & 0x3ff;
                dst_y[x + 1] = pred_L[0] = (y2 + ((3 * (pred_T[3] + pred_L[0]) - 2 * pred_T[0]) >> 2)) & 0x3ff;
                dst_v[x / 2] = pred_L[2] = (v + (((pred_L[2] - pred_TL[2]) >> 1) + pred_T[2])) & 0x3ff;

                pred_TL[0] = pred_T[3];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
    }
}

static void decode_ca2i(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];
    dst_a = (uint16_t *)p->data[3];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_a[x    ] = get_bits(gb, 10);
                dst_y[x    ] = get_bits(gb, 10);
                dst_u[x / 2] = get_bits(gb, 10);
                dst_a[x + 1] = get_bits(gb, 10);
                dst_y[x + 1] = get_bits(gb, 10);
                dst_v[x / 2] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 502, 512, 512, 502 };

            for (x = 0; x < avctx->width; x += 2) {
                int y1, y2, u, v, a1, a2;

                a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0x3ff;
                dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0x3ff;
                dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0x3ff;
                dst_a[x    ] = pred[3] = (a1 + pred[3]) & 0x3ff;
                dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0x3ff;
                dst_a[x + 1] = pred[3] = (a2 + pred[3]) & 0x3ff;
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_ca2p(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_y = (uint16_t *)p->data[0];
    dst_u = (uint16_t *)p->data[1];
    dst_v = (uint16_t *)p->data[2];
    dst_a = (uint16_t *)p->data[3];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_a[x    ] = get_bits(gb, 10);
            dst_y[x    ] = get_bits(gb, 10);
            dst_u[x / 2] = get_bits(gb, 10);
            dst_a[x + 1] = get_bits(gb, 10);
            dst_y[x + 1] = get_bits(gb, 10);
            dst_v[x / 2] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 502, 512, 512, 502 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v, a1, a2;

            a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0x3ff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0x3ff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0x3ff;
            dst_a[x    ] = pred[3] = (a1 + pred[3]) & 0x3ff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0x3ff;
            dst_a[x + 1] = pred[3] = (a2 + pred[3]) & 0x3ff;
        }
    }

    dst_y += p->linesize[0] / 2;
    dst_u += p->linesize[1] / 2;
    dst_v += p->linesize[2] / 2;
    dst_a += p->linesize[3] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_a[x    ] = get_bits(gb, 10);
                dst_y[x    ] = get_bits(gb, 10);
                dst_u[x / 2] = get_bits(gb, 10);
                dst_a[x + 1] = get_bits(gb, 10);
                dst_y[x + 1] = get_bits(gb, 10);
                dst_v[x / 2] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[6], pred_L[6], pred_T[6];
            int y1, y2, u, v, a1, a2;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0] / 2];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1] / 2];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2] / 2];
            pred_TL[4] = pred_L[4] = dst_a[-p->linesize[3] / 2];

            for (x = 0; x < avctx->width; x += 2) {
                pred_T[0] = dst_y[-p->linesize[0] / 2 + x];
                pred_T[3] = dst_y[-p->linesize[0] / 2 + x + 1];
                pred_T[1] = dst_u[-p->linesize[1] / 2 + x / 2];
                pred_T[2] = dst_v[-p->linesize[2] / 2 + x / 2];
                pred_T[4] = dst_a[-p->linesize[3] / 2 + x];
                pred_T[5] = dst_a[-p->linesize[3] / 2 + x + 1];

                a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_u[x / 2] = pred_L[1] = (u + (((pred_L[1] - pred_TL[1]) >> 1) + pred_T[1])) & 0x3ff;
                dst_y[x + 1] = pred_L[0] = (y2 + ((3 * (pred_T[3] + pred_L[0]) - 2 * pred_T[0]) >> 2)) & 0x3ff;
                dst_v[x / 2] = pred_L[2] = (v + (((pred_L[2] - pred_TL[2]) >> 1) + pred_T[2])) & 0x3ff;
                dst_a[x    ] = pred_L[4] = (a1 + ((3 * (pred_T[4] + pred_L[4]) - 2 * pred_TL[4]) >> 2)) & 0x3ff;
                dst_a[x + 1] = pred_L[4] = (a2 + ((3 * (pred_T[5] + pred_L[4]) - 2 * pred_T[4]) >> 2)) & 0x3ff;

                pred_TL[0] = pred_T[3];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[4] = pred_T[5];
            }
        }

        dst_y += p->linesize[0] / 2;
        dst_u += p->linesize[1] / 2;
        dst_v += p->linesize[2] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_c82i(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];
    dst_a = p->data[3];

    for (y = 0; y < avctx->height; y += 1) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_a[x    ] = get_bits(gb, 8);
                dst_y[x    ] = get_bits(gb, 8);
                dst_u[x / 2] = get_bits(gb, 8);
                dst_a[x + 1] = get_bits(gb, 8);
                dst_y[x + 1] = get_bits(gb, 8);
                dst_v[x / 2] = get_bits(gb, 8);
            }
        } else {
            int pred[4] = { 125, -128, -128, 125 };

            for (x = 0; x < avctx->width; x += 2) {
                int y1, y2, u, v, a1, a2;

                a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0xff;
                dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0xff;
                dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0xff;
                dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0xff;
                dst_a[x    ] = pred[3] = (a1 + pred[3]) & 0xff;
                dst_a[x + 1] = pred[3] = (a2 + pred[3]) & 0xff;
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
        dst_a += p->linesize[3];
    }
}

static void decode_c82p(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v, *dst_a;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];
    dst_a = p->data[3];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_a[x    ] = get_bits(gb, 8);
            dst_y[x    ] = get_bits(gb, 8);
            dst_u[x / 2] = get_bits(gb, 8);
            dst_a[x + 1] = get_bits(gb, 8);
            dst_y[x + 1] = get_bits(gb, 8);
            dst_v[x / 2] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { 125, -128, -128, 125 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v, a1, a2;

            a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0xff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0xff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0xff;
            dst_a[x    ] = pred[3] = (a1 + pred[3]) & 0xff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0xff;
            dst_a[x + 1] = pred[3] = (a2 + pred[3]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];
    dst_a += p->linesize[3];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_a[x    ] = get_bits(gb, 8);
                dst_y[x    ] = get_bits(gb, 8);
                dst_u[x / 2] = get_bits(gb, 8);
                dst_a[x + 1] = get_bits(gb, 8);
                dst_y[x + 1] = get_bits(gb, 8);
                dst_v[x / 2] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[6], pred_L[6], pred_T[6];
            int y1, y2, u, v, a1, a2;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0]];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1]];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2]];
            pred_TL[4] = pred_L[4] = dst_a[-p->linesize[3]];

            for (x = 0; x < avctx->width; x += 2) {
                pred_T[0] = dst_y[-p->linesize[0] + x];
                pred_T[3] = dst_y[-p->linesize[0] + x + 1];
                pred_T[1] = dst_u[-p->linesize[1] + x / 2];
                pred_T[2] = dst_v[-p->linesize[2] + x / 2];
                pred_T[4] = dst_a[-p->linesize[3] + x];
                pred_T[5] = dst_a[-p->linesize[3] + x + 1];

                a1 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                a2 = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst_u[x / 2] = pred_L[1] = (u + (((pred_L[1] - pred_TL[1]) >> 1) + pred_T[1])) & 0xff;
                dst_y[x + 1] = pred_L[0] = (y2 + ((3 * (pred_T[3] + pred_L[0]) - 2 * pred_T[0]) >> 2)) & 0xff;
                dst_v[x / 2] = pred_L[2] = (v + (((pred_L[2] - pred_TL[2]) >> 1) + pred_T[2])) & 0xff;
                dst_a[x    ] = pred_L[4] = (a1 + ((3 * (pred_T[4] + pred_L[4]) - 2 * pred_TL[4]) >> 2)) & 0xff;
                dst_a[x + 1] = pred_L[4] = (a2 + ((3 * (pred_T[5] + pred_L[4]) - 2 * pred_T[4]) >> 2)) & 0xff;

                pred_TL[0] = pred_T[3];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[4] = pred_T[5];
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
        dst_a += p->linesize[3];
    }
}

static void decode_ybyr(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_y[x    ] = get_bits(gb, 8);
            dst_u[x / 2] = get_bits(gb, 8) + 128;
            dst_y[x + 1] = get_bits(gb, 8);
            dst_v[x / 2] = get_bits(gb, 8) + 128;
        }
    } else {
        int pred[4] = { -128, 128, 128, 0 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v;

            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0xff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0xff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0xff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_y[x    ] = get_bits(gb, 8);
                dst_u[x / 2] = get_bits(gb, 8) + 128;
                dst_y[x + 1] = get_bits(gb, 8);
                dst_v[x / 2] = get_bits(gb, 8) + 128;
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int y1, y2, u, v;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0]];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1]];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x += 2) {
                pred_T[0] = dst_y[-p->linesize[0] + x];
                pred_T[3] = dst_y[-p->linesize[0] + x + 1];
                pred_T[1] = dst_u[-p->linesize[1] + x / 2];
                pred_T[2] = dst_v[-p->linesize[2] + x / 2];

                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst_u[x / 2] = pred_L[1] = (u + (((pred_L[1] - pred_TL[1]) >> 1) + pred_T[1])) & 0xff;
                dst_y[x + 1] = pred_L[0] = (y2 + ((3 * (pred_T[3] + pred_L[0]) - 2 * pred_T[0]) >> 2)) & 0xff;
                dst_v[x / 2] = pred_L[2] = (v + (((pred_L[2] - pred_TL[2]) >> 1) + pred_T[2])) & 0xff;

                pred_TL[0] = pred_T[3];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_byryi(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_y[x    ] = get_bits(gb, 8);
            dst_u[x / 2] = get_bits(gb, 8);
            dst_y[x + 1] = get_bits(gb, 8);
            dst_v[x / 2] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { 125, -128, -128, 0 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v;

            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0xff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0xff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0xff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_y[x    ] = get_bits(gb, 8);
                dst_u[x / 2] = get_bits(gb, 8);
                dst_y[x + 1] = get_bits(gb, 8);
                dst_v[x / 2] = get_bits(gb, 8);
            }
        } else {
            int pred_L[4];
            int y1, y2, u, v;

            pred_L[0] = dst_y[-p->linesize[0]];
            pred_L[1] = dst_u[-p->linesize[1]];
            pred_L[2] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x += 2) {
                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + pred_L[0]) & 0xff;
                dst_u[x / 2] = pred_L[1] = (u  + pred_L[1]) & 0xff;
                dst_y[x + 1] = pred_L[0] = (y2 + pred_L[0]) & 0xff;
                dst_v[x / 2] = pred_L[2] = (v +  pred_L[2]) & 0xff;
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_byry(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x += 2) {
            dst_y[x    ] = get_bits(gb, 8);
            dst_u[x / 2] = get_bits(gb, 8);
            dst_y[x + 1] = get_bits(gb, 8);
            dst_v[x / 2] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { 125, -128, -128, 0 };

        for (x = 0; x < avctx->width; x += 2) {
            int y1, y2, u, v;

            y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x    ] = pred[0] = (y1 + pred[0]) & 0xff;
            dst_u[x / 2] = pred[1] = (u  + pred[1]) & 0xff;
            dst_y[x + 1] = pred[0] = (y2 + pred[0]) & 0xff;
            dst_v[x / 2] = pred[2] = (v  + pred[2]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x += 2) {
                dst_y[x    ] = get_bits(gb, 8);
                dst_u[x / 2] = get_bits(gb, 8);
                dst_y[x + 1] = get_bits(gb, 8);
                dst_v[x / 2] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int y1, y2, u, v;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0]];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1]];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x += 2) {
                pred_T[0] = dst_y[-p->linesize[0] + x];
                pred_T[3] = dst_y[-p->linesize[0] + x + 1];
                pred_T[1] = dst_u[-p->linesize[1] + x / 2];
                pred_T[2] = dst_v[-p->linesize[2] + x / 2];

                y1 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y2 = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                v  = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x    ] = pred_L[0] = (y1 + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst_u[x / 2] = pred_L[1] = (u + (((pred_L[1] - pred_TL[1]) >> 1) + pred_T[1])) & 0xff;
                dst_y[x + 1] = pred_L[0] = (y2 + ((3 * (pred_T[3] + pred_L[0]) - 2 * pred_T[0]) >> 2)) & 0xff;
                dst_v[x / 2] = pred_L[2] = (v + (((pred_L[2] - pred_TL[2]) >> 1) + pred_T[2])) & 0xff;

                pred_TL[0] = pred_T[3];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_ybri(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_y[x] = get_bits(gb, 8);
            dst_u[x] = get_bits(gb, 8);
            dst_v[x] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { s->alt ? 125 : -146, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int y, u, v;

            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x] = pred[0] = (y + pred[0]) & 0xff;
            dst_u[x] = pred[1] = (u + pred[1]) & 0xff;
            dst_v[x] = pred[2] = (v + pred[2]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_y[x] = get_bits(gb, 8);
                dst_u[x] = get_bits(gb, 8);
                dst_v[x] = get_bits(gb, 8);
            }
        } else {
            int pred_L[4];
            int y, u, v;

            pred_L[0] = dst_y[-p->linesize[0]];
            pred_L[1] = dst_u[-p->linesize[1]];
            pred_L[2] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x++) {
                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x] = pred_L[0] = (y + pred_L[0]) & 0xff;
                dst_u[x] = pred_L[1] = (u + pred_L[1]) & 0xff;
                dst_v[x] = pred_L[2] = (v + pred_L[2]) & 0xff;
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_ybr(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_y[x] = get_bits(gb, 8);
            dst_u[x] = get_bits(gb, 8);
            dst_v[x] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { s->alt ? 125 : -146, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int y, u, v;

            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_y[x] = pred[0] = (y + pred[0]) & 0xff;
            dst_u[x] = pred[1] = (u + pred[1]) & 0xff;
            dst_v[x] = pred[2] = (v + pred[2]) & 0xff;
        }
    }

    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_y[x] = get_bits(gb, 8);
                dst_u[x] = get_bits(gb, 8);
                dst_v[x] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int y, u, v;

            pred_TL[0] = pred_L[0] = dst_y[-p->linesize[0]];
            pred_TL[1] = pred_L[1] = dst_u[-p->linesize[1]];
            pred_TL[2] = pred_L[2] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_y[-p->linesize[0] + x];
                pred_T[1] = dst_u[-p->linesize[1] + x];
                pred_T[2] = dst_v[-p->linesize[2] + x];

                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_y[x] = pred_L[0] = (y + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst_u[x] = pred_L[1] = (u + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0xff;
                dst_v[x] = pred_L[2] = (v + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0xff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_aybri(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_a, *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_a = p->data[3];
    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_a[x] = get_bits(gb, 8);
            dst_y[x] = get_bits(gb, 8);
            dst_u[x] = get_bits(gb, 8);
            dst_v[x] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { 125, s->alt ? 125 : -146, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int a, y, u, v;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_a[x] = pred[0] = (a + pred[0]) & 0xff;
            dst_y[x] = pred[1] = (y + pred[1]) & 0xff;
            dst_u[x] = pred[2] = (u + pred[2]) & 0xff;
            dst_v[x] = pred[3] = (v + pred[3]) & 0xff;
        }
    }

    dst_a += p->linesize[3];
    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 8);
                dst_y[x] = get_bits(gb, 8);
                dst_u[x] = get_bits(gb, 8);
                dst_v[x] = get_bits(gb, 8);
            }
        } else {
            int pred_L[4];
            int a, y, u, v;

            pred_L[0] = dst_a[-p->linesize[3]];
            pred_L[1] = dst_y[-p->linesize[0]];
            pred_L[2] = dst_u[-p->linesize[1]];
            pred_L[3] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x++) {
                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred_L[0] = (a + pred_L[0]) & 0xff;
                dst_y[x] = pred_L[1] = (y + pred_L[1]) & 0xff;
                dst_u[x] = pred_L[2] = (u + pred_L[2]) & 0xff;
                dst_v[x] = pred_L[3] = (v + pred_L[3]) & 0xff;
            }
        }

        dst_a += p->linesize[3];
        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_aybr(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst_a, *dst_y, *dst_u, *dst_v;
    int x, y;

    dst_a = p->data[3];
    dst_y = p->data[0];
    dst_u = p->data[1];
    dst_v = p->data[2];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_a[x] = get_bits(gb, 8);
            dst_y[x] = get_bits(gb, 8);
            dst_u[x] = get_bits(gb, 8);
            dst_v[x] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { 125, s->alt ? 125 : -146, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int a, y, u, v;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_a[x] = pred[0] = (a + pred[0]) & 0xff;
            dst_y[x] = pred[1] = (y + pred[1]) & 0xff;
            dst_u[x] = pred[2] = (u + pred[2]) & 0xff;
            dst_v[x] = pred[3] = (v + pred[3]) & 0xff;
        }
    }

    dst_a += p->linesize[3];
    dst_y += p->linesize[0];
    dst_u += p->linesize[1];
    dst_v += p->linesize[2];

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 8);
                dst_y[x] = get_bits(gb, 8);
                dst_u[x] = get_bits(gb, 8);
                dst_v[x] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int a, y, u, v;

            pred_TL[0] = pred_L[0] = dst_a[-p->linesize[3]];
            pred_TL[1] = pred_L[1] = dst_y[-p->linesize[0]];
            pred_TL[2] = pred_L[2] = dst_u[-p->linesize[1]];
            pred_TL[3] = pred_L[3] = dst_v[-p->linesize[2]];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_a[-p->linesize[3] + x];
                pred_T[1] = dst_y[-p->linesize[0] + x];
                pred_T[2] = dst_u[-p->linesize[1] + x];
                pred_T[3] = dst_v[-p->linesize[2] + x];

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                y = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                u = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                v = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred_L[0] = (a + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst_y[x] = pred_L[1] = (y + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0xff;
                dst_u[x] = pred_L[2] = (u + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0xff;
                dst_v[x] = pred_L[3] = (v + ((3 * (pred_T[3] + pred_L[3]) - 2 * pred_TL[3]) >> 2)) & 0xff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[3] = pred_T[3];
            }
        }

        dst_a += p->linesize[3];
        dst_y += p->linesize[0];
        dst_u += p->linesize[1];
        dst_v += p->linesize[2];
    }
}

static void decode_argxi(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_r, *dst_g, *dst_b, *dst_a;
    int x, y;

    dst_r = (uint16_t *)p->data[2];
    dst_g = (uint16_t *)p->data[0];
    dst_b = (uint16_t *)p->data[1];
    dst_a = (uint16_t *)p->data[3];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 10);
                dst_r[x] = get_bits(gb, 10);
                dst_g[x] = get_bits(gb, 10);
                dst_b[x] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 512, 512, 512, 512 };

            for (x = 0; x < avctx->width; x++) {
                int r, g, b, a;

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred[3] = (a + pred[3]) & 0x3ff;
                dst_r[x] = pred[0] = (r + pred[0]) & 0x3ff;
                dst_g[x] = pred[1] = (r + g + pred[1]) & 0x3ff;
                dst_b[x] = pred[2] = (r + g + b + pred[2]) & 0x3ff;
            }
        }

        dst_r += p->linesize[2] / 2;
        dst_g += p->linesize[0] / 2;
        dst_b += p->linesize[1] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_argx(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_r, *dst_g, *dst_b, *dst_a;
    int x, y;

    dst_r = (uint16_t *)p->data[2];
    dst_g = (uint16_t *)p->data[0];
    dst_b = (uint16_t *)p->data[1];
    dst_a = (uint16_t *)p->data[3];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_a[x] = get_bits(gb, 10);
            dst_r[x] = get_bits(gb, 10);
            dst_g[x] = get_bits(gb, 10);
            dst_b[x] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 512, 512, 512, 512 };

        for (x = 0; x < avctx->width; x++) {
            int r, g, b, a;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_a[x] = pred[3] = (a + pred[3]) & 0x3ff;
            dst_r[x] = pred[0] = (r + pred[0]) & 0x3ff;
            dst_g[x] = pred[1] = (r + g + pred[1]) & 0x3ff;
            dst_b[x] = pred[2] = (r + g + b + pred[2]) & 0x3ff;
        }
    }

    dst_r += p->linesize[2] / 2;
    dst_g += p->linesize[0] / 2;
    dst_b += p->linesize[1] / 2;
    dst_a += p->linesize[3] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_a[x] = get_bits(gb, 10);
                dst_r[x] = get_bits(gb, 10);
                dst_g[x] = get_bits(gb, 10);
                dst_b[x] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int r, g, b, a;

            pred_TL[0] = pred_L[0] = dst_r[-p->linesize[2] / 2];
            pred_TL[1] = pred_L[1] = dst_g[-p->linesize[0] / 2];
            pred_TL[2] = pred_L[2] = dst_b[-p->linesize[1] / 2];
            pred_TL[3] = pred_L[3] = dst_a[-p->linesize[3] / 2];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_r[-p->linesize[2] / 2 + x];
                pred_T[1] = dst_g[-p->linesize[0] / 2 + x];
                pred_T[2] = dst_b[-p->linesize[1] / 2 + x];
                pred_T[3] = dst_a[-p->linesize[3] / 2 + x];

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_a[x] = pred_L[3] = (a + ((3 * (pred_T[3] + pred_L[3]) - 2 * pred_TL[3]) >> 2)) & 0x3ff;
                dst_r[x] = pred_L[0] = (r + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_g[x] = pred_L[1] = (r + g + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0x3ff;
                dst_b[x] = pred_L[2] = (r + g + b + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0x3ff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[3] = pred_T[3];
            }
        }

        dst_r += p->linesize[2] / 2;
        dst_g += p->linesize[0] / 2;
        dst_b += p->linesize[1] / 2;
        dst_a += p->linesize[3] / 2;
    }
}

static void decode_rgbxi(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_r, *dst_g, *dst_b;
    int x, y;

    dst_r = (uint16_t *)p->data[2];
    dst_g = (uint16_t *)p->data[0];
    dst_b = (uint16_t *)p->data[1];

    for (y = 0; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_r[x] = get_bits(gb, 10);
                dst_g[x] = get_bits(gb, 10);
                dst_b[x] = get_bits(gb, 10);
            }
        } else {
            int pred[4] = { 512, 512, 512, 0 };

            for (x = 0; x < avctx->width; x++) {
                int r, g, b;

                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_r[x] = pred[0] = (r + pred[0]) & 0x3ff;
                dst_g[x] = pred[1] = (r + g + pred[1]) & 0x3ff;
                dst_b[x] = pred[2] = (r + g + b + pred[2]) & 0x3ff;
            }
        }

        dst_r += p->linesize[2] / 2;
        dst_g += p->linesize[0] / 2;
        dst_b += p->linesize[1] / 2;
    }
}

static void decode_rgbx(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint16_t *dst_r, *dst_g, *dst_b;
    int x, y;

    dst_r = (uint16_t *)p->data[2];
    dst_g = (uint16_t *)p->data[0];
    dst_b = (uint16_t *)p->data[1];

    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst_r[x] = get_bits(gb, 10);
            dst_g[x] = get_bits(gb, 10);
            dst_b[x] = get_bits(gb, 10);
        }
    } else {
        int pred[4] = { 512, 512, 512, 0 };

        for (x = 0; x < avctx->width; x++) {
            int r, g, b;

            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst_r[x] = pred[0] = (r + pred[0]) & 0x3ff;
            dst_g[x] = pred[1] = (r + g + pred[1]) & 0x3ff;
            dst_b[x] = pred[2] = (r + g + b + pred[2]) & 0x3ff;
        }
    }

    dst_r += p->linesize[2] / 2;
    dst_g += p->linesize[0] / 2;
    dst_b += p->linesize[1] / 2;

    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst_r[x] = get_bits(gb, 10);
                dst_g[x] = get_bits(gb, 10);
                dst_b[x] = get_bits(gb, 10);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int r, g, b;

            pred_TL[0] = pred_L[0] = dst_r[-p->linesize[2] / 2];
            pred_TL[1] = pred_L[1] = dst_g[-p->linesize[0] / 2];
            pred_TL[2] = pred_L[2] = dst_b[-p->linesize[1] / 2];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst_r[-p->linesize[2] / 2 + x];
                pred_T[1] = dst_g[-p->linesize[0] / 2 + x];
                pred_T[2] = dst_b[-p->linesize[1] / 2 + x];

                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst_r[x] = pred_L[0] = (r + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0x3ff;
                dst_g[x] = pred_L[1] = (r + g + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0x3ff;
                dst_b[x] = pred_L[2] = (r + g + b + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0x3ff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }

        dst_r += p->linesize[2] / 2;
        dst_g += p->linesize[0] / 2;
        dst_b += p->linesize[1] / 2;
    }
}

static void decode_argbi(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst;
    int x, y;

    dst = p->data[0];
    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst[x * 4 + 0] = get_bits(gb, 8);
            dst[x * 4 + 1] = get_bits(gb, 8);
            dst[x * 4 + 2] = get_bits(gb, 8);
            dst[x * 4 + 3] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { -128, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int a, r, g, b;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst[4 * x + 0] = pred[0] = (a + pred[0]) & 0xff;
            dst[4 * x + 1] = pred[1] = (r + pred[1]) & 0xff;
            dst[4 * x + 2] = pred[2] = (r + g + pred[2]) & 0xff;
            dst[4 * x + 3] = pred[3] = (r + g + b + pred[3]) & 0xff;
        }
    }

    dst += p->linesize[0];
    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst[x * 4 + 0] = get_bits(gb, 8);
                dst[x * 4 + 1] = get_bits(gb, 8);
                dst[x * 4 + 2] = get_bits(gb, 8);
                dst[x * 4 + 3] = get_bits(gb, 8);
            }
        } else {
            int pred_L[4];
            int a, r, g, b;

            pred_L[0] = dst[-p->linesize[0] + 0];
            pred_L[1] = dst[-p->linesize[0] + 1];
            pred_L[2] = dst[-p->linesize[0] + 2];
            pred_L[3] = dst[-p->linesize[0] + 3];

            for (x = 0; x < avctx->width; x++) {
                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst[4 * x + 0] = pred_L[0] = (a + pred_L[0]) & 0xff;
                dst[4 * x + 1] = pred_L[1] = (r + pred_L[1]) & 0xff;
                dst[4 * x + 2] = pred_L[2] = (r + g + pred_L[2]) & 0xff;
                dst[4 * x + 3] = pred_L[3] = (r + g + b + pred_L[3]) & 0xff;
            }
        }
        dst += p->linesize[0];
    }
}

static void decode_argb(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst;
    int x, y;

    dst = p->data[0];
    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst[x * 4 + 0] = get_bits(gb, 8);
            dst[x * 4 + 1] = get_bits(gb, 8);
            dst[x * 4 + 2] = get_bits(gb, 8);
            dst[x * 4 + 3] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { -128, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int a, r, g, b;

            a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst[4 * x + 0] = pred[0] = (a + pred[0]) & 0xff;
            dst[4 * x + 1] = pred[1] = (r + pred[1]) & 0xff;
            dst[4 * x + 2] = pred[2] = (r + g + pred[2]) & 0xff;
            dst[4 * x + 3] = pred[3] = (r + g + b + pred[3]) & 0xff;
        }
    }

    dst += p->linesize[0];
    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst[x * 4 + 0] = get_bits(gb, 8);
                dst[x * 4 + 1] = get_bits(gb, 8);
                dst[x * 4 + 2] = get_bits(gb, 8);
                dst[x * 4 + 3] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int a, r, g, b;

            pred_TL[0] = pred_L[0] = dst[-p->linesize[0] + 0];
            pred_TL[1] = pred_L[1] = dst[-p->linesize[0] + 1];
            pred_TL[2] = pred_L[2] = dst[-p->linesize[0] + 2];
            pred_TL[3] = pred_L[3] = dst[-p->linesize[0] + 3];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst[-p->linesize[0] + 4 * x + 0];
                pred_T[1] = dst[-p->linesize[0] + 4 * x + 1];
                pred_T[2] = dst[-p->linesize[0] + 4 * x + 2];
                pred_T[3] = dst[-p->linesize[0] + 4 * x + 3];

                a = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst[4 * x + 0] = pred_L[0] = (a + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst[4 * x + 1] = pred_L[1] = (r + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0xff;
                dst[4 * x + 2] = pred_L[2] = (r + g + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0xff;
                dst[4 * x + 3] = pred_L[3] = (r + g + b + ((3 * (pred_T[3] + pred_L[3]) - 2 * pred_TL[3]) >> 2)) & 0xff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
                pred_TL[3] = pred_T[3];
            }
        }
        dst += p->linesize[0];
    }
}

static void decode_rgbi(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst;
    int x, y;

    dst = p->data[0];
    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst[x * 4 + 0] = get_bits(gb, 8);
            dst[x * 4 + 1] = get_bits(gb, 8);
            dst[x * 4 + 2] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { -128, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int r, g, b;

            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst[4 * x + 0] = pred[0] = (r + pred[0]) & 0xff;
            dst[4 * x + 1] = pred[1] = (r + g + pred[1]) & 0xff;
            dst[4 * x + 2] = pred[2] = (r + g + b + pred[2]) & 0xff;
        }
    }

    dst += p->linesize[0];
    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst[x * 4 + 0] = get_bits(gb, 8);
                dst[x * 4 + 1] = get_bits(gb, 8);
                dst[x * 4 + 2] = get_bits(gb, 8);
            }
        } else {
            int pred_L[4];
            int r, g, b;

            pred_L[0] = dst[-p->linesize[0] + 0];
            pred_L[1] = dst[-p->linesize[0] + 1];
            pred_L[2] = dst[-p->linesize[0] + 2];

            for (x = 0; x < avctx->width; x++) {
                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst[4 * x + 0] = pred_L[0] = (r + pred_L[0]) & 0xff;
                dst[4 * x + 1] = pred_L[1] = (r + g + pred_L[1]) & 0xff;
                dst[4 * x + 2] = pred_L[2] = (r + g + b + pred_L[2]) & 0xff;
            }
        }
        dst += p->linesize[0];
    }
}

static void decode_rgb(AVCodecContext *avctx, AVFrame *p, GetBitContext *gb)
{
    SheerVideoContext *s = avctx->priv_data;
    uint8_t *dst;
    int x, y;

    dst = p->data[0];
    if (get_bits1(gb)) {
        for (x = 0; x < avctx->width; x++) {
            dst[x * 4 + 0] = get_bits(gb, 8);
            dst[x * 4 + 1] = get_bits(gb, 8);
            dst[x * 4 + 2] = get_bits(gb, 8);
        }
    } else {
        int pred[4] = { -128, -128, -128, -128 };

        for (x = 0; x < avctx->width; x++) {
            int r, g, b;

            r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
            g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
            b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

            dst[4 * x + 0] = pred[0] = (r + pred[0]) & 0xff;
            dst[4 * x + 1] = pred[1] = (r + g + pred[1]) & 0xff;
            dst[4 * x + 2] = pred[2] = (r + g + b + pred[2]) & 0xff;
        }
    }

    dst += p->linesize[0];
    for (y = 1; y < avctx->height; y++) {
        if (get_bits1(gb)) {
            for (x = 0; x < avctx->width; x++) {
                dst[x * 4 + 0] = get_bits(gb, 8);
                dst[x * 4 + 1] = get_bits(gb, 8);
                dst[x * 4 + 2] = get_bits(gb, 8);
            }
        } else {
            int pred_TL[4], pred_L[4], pred_T[4];
            int r, g, b;

            pred_TL[0] = pred_L[0] = dst[-p->linesize[0] + 0];
            pred_TL[1] = pred_L[1] = dst[-p->linesize[0] + 1];
            pred_TL[2] = pred_L[2] = dst[-p->linesize[0] + 2];

            for (x = 0; x < avctx->width; x++) {
                pred_T[0] = dst[-p->linesize[0] + 4 * x + 0];
                pred_T[1] = dst[-p->linesize[0] + 4 * x + 1];
                pred_T[2] = dst[-p->linesize[0] + 4 * x + 2];

                r = get_vlc2(gb, s->vlc[0].table, SHEER_VLC_BITS, 2);
                g = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);
                b = get_vlc2(gb, s->vlc[1].table, SHEER_VLC_BITS, 2);

                dst[4 * x + 0] = pred_L[0] = (r + ((3 * (pred_T[0] + pred_L[0]) - 2 * pred_TL[0]) >> 2)) & 0xff;
                dst[4 * x + 1] = pred_L[1] = (r + g + ((3 * (pred_T[1] + pred_L[1]) - 2 * pred_TL[1]) >> 2)) & 0xff;
                dst[4 * x + 2] = pred_L[2] = (r + g + b + ((3 * (pred_T[2] + pred_L[2]) - 2 * pred_TL[2]) >> 2)) & 0xff;

                pred_TL[0] = pred_T[0];
                pred_TL[1] = pred_T[1];
                pred_TL[2] = pred_T[2];
            }
        }
        dst += p->linesize[0];
    }
}

static av_cold int build_vlc(VLC *vlc, const SheerTable *table)
{
    const uint8_t *cur = table->lens;
    uint8_t  lens[1024];
    unsigned count = 0;

    for (int step = 1, len = 1; len > 0; len += step) {
        unsigned new_count = count;

        if (len == 16) {
            new_count += table->nb_16s;
            step       = -1;
        } else
            new_count += *cur++;

        for (; count < new_count; count++)
            lens[count]  = len;
    }

    ff_free_vlc(vlc);
    return ff_init_vlc_from_lengths(vlc, SHEER_VLC_BITS, count,
                                    lens, sizeof(*lens), NULL, 0, 0, 0, 0, NULL);
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    SheerVideoContext *s = avctx->priv_data;
    const SheerTable *table;
    GetBitContext gb;
    unsigned format;
    int ret;

    if (avpkt->size <= 20)
        return AVERROR_INVALIDDATA;

    if (AV_RL32(avpkt->data) != MKTAG('S','h','i','r') &&
        AV_RL32(avpkt->data) != MKTAG('Z','w','a','k'))
        return AVERROR_INVALIDDATA;

    s->alt = 0;
    format = AV_RL32(avpkt->data + 16);
    av_log(avctx, AV_LOG_DEBUG, "format: %s\n", av_fourcc2str(format));
    switch (format) {
    case MKTAG(' ', 'R', 'G', 'B'):
        avctx->pix_fmt = AV_PIX_FMT_RGB0;
        s->decode_frame = decode_rgb;
        table           = rgb;
        break;
    case MKTAG(' ', 'r', 'G', 'B'):
        avctx->pix_fmt = AV_PIX_FMT_RGB0;
        s->decode_frame = decode_rgbi;
        table           = rgbi;
        break;
    case MKTAG('A', 'R', 'G', 'X'):
        avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
        s->decode_frame = decode_argx;
        table           = rgbx;
        break;
    case MKTAG('A', 'r', 'G', 'X'):
        avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
        s->decode_frame = decode_argxi;
        table           = rgbxi;
        break;
    case MKTAG('R', 'G', 'B', 'X'):
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        s->decode_frame = decode_rgbx;
        table           = rgbx;
        break;
    case MKTAG('r', 'G', 'B', 'X'):
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        s->decode_frame = decode_rgbxi;
        table           = rgbxi;
        break;
    case MKTAG('A', 'R', 'G', 'B'):
        avctx->pix_fmt = AV_PIX_FMT_ARGB;
        s->decode_frame = decode_argb;
        table           = rgb;
        break;
    case MKTAG('A', 'r', 'G', 'B'):
        avctx->pix_fmt = AV_PIX_FMT_ARGB;
        s->decode_frame = decode_argbi;
        table           = rgbi;
        break;
    case MKTAG('A', 'Y', 'B', 'R'):
        s->alt = 1;
    case MKTAG('A', 'Y', 'b', 'R'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        s->decode_frame = decode_aybr;
        table           = ybr;
        break;
    case MKTAG('A', 'y', 'B', 'R'):
        s->alt = 1;
    case MKTAG('A', 'y', 'b', 'R'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        s->decode_frame = decode_aybri;
        table           = ybri;
        break;
    case MKTAG(' ', 'Y', 'B', 'R'):
        s->alt = 1;
    case MKTAG(' ', 'Y', 'b', 'R'):
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        s->decode_frame = decode_ybr;
        table           = ybr;
        break;
    case MKTAG(' ', 'y', 'B', 'R'):
        s->alt = 1;
    case MKTAG(' ', 'y', 'b', 'R'):
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        s->decode_frame = decode_ybri;
        table           = ybri;
        break;
    case MKTAG('Y', 'B', 'R', 0x0a):
        avctx->pix_fmt = AV_PIX_FMT_YUV444P10;
        s->decode_frame = decode_ybr10;
        table           = ybr10;
        break;
    case MKTAG('y', 'B', 'R', 0x0a):
        avctx->pix_fmt = AV_PIX_FMT_YUV444P10;
        s->decode_frame = decode_ybr10i;
        table           = ybr10i;
        break;
    case MKTAG('C', 'A', '4', 'p'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P10;
        s->decode_frame = decode_ca4p;
        table           = ybr10;
        break;
    case MKTAG('C', 'A', '4', 'i'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P10;
        s->decode_frame = decode_ca4i;
        table           = ybr10i;
        break;
    case MKTAG('B', 'Y', 'R', 'Y'):
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->decode_frame = decode_byry;
        table           = byry;
        break;
    case MKTAG('B', 'Y', 'R', 'y'):
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->decode_frame = decode_byryi;
        table           = byryi;
        break;
    case MKTAG('Y', 'b', 'Y', 'r'):
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->decode_frame = decode_ybyr;
        table           = ybyr;
        break;
    case MKTAG('C', '8', '2', 'p'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        s->decode_frame = decode_c82p;
        table           = byry;
        break;
    case MKTAG('C', '8', '2', 'i'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        s->decode_frame = decode_c82i;
        table           = byryi;
        break;
    case MKTAG(0xa2, 'Y', 'R', 'Y'):
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
        s->decode_frame = decode_yry10;
        table           = yry10;
        break;
    case MKTAG(0xa2, 'Y', 'R', 'y'):
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
        s->decode_frame = decode_yry10i;
        table           = yry10i;
        break;
    case MKTAG('C', 'A', '2', 'p'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P10;
        s->decode_frame = decode_ca2p;
        table           = yry10;
        break;
    case MKTAG('C', 'A', '2', 'i'):
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P10;
        s->decode_frame = decode_ca2i;
        table           = yry10i;
        break;
    default:
        avpriv_request_sample(avctx, "unsupported format: 0x%X", format);
        return AVERROR_PATCHWELCOME;
    }

    if (s->format != format) {
        if ((ret = build_vlc(&s->vlc[0], &table[0])) < 0 ||
            (ret = build_vlc(&s->vlc[1], &table[1])) < 0) {
            s->format = 0;
            return ret;
        }
        s->format = format;
    }
    if (avpkt->size < 20 + avctx->width * avctx->height / 16) {
        av_log(avctx, AV_LOG_ERROR, "Input packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    if ((ret = init_get_bits8(&gb, avpkt->data + 20, avpkt->size - 20)) < 0)
        return ret;

    s->decode_frame(avctx, p, &gb);

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    SheerVideoContext *s = avctx->priv_data;

    ff_free_vlc(&s->vlc[0]);
    ff_free_vlc(&s->vlc[1]);

    return 0;
}

const FFCodec ff_sheervideo_decoder = {
    .p.name           = "sheervideo",
    CODEC_LONG_NAME("BitJazz SheerVideo"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_SHEERVIDEO,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .priv_data_size   = sizeof(SheerVideoContext),
    .close            = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
};
