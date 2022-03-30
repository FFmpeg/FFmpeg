/*
 * Motion Pixels Video Decoder
 * Copyright (c) 2008 Gregory Montoir (cyx@users.sourceforge.net)
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

#include "libavutil/thread.h"

#include "config.h"

#include "avcodec.h"
#include "get_bits.h"
#include "bswapdsp.h"
#include "codec_internal.h"
#include "internal.h"

#define MAX_HUFF_CODES 16

#include "motionpixels_tablegen.h"

typedef struct HuffCode {
    uint8_t size;
    uint8_t delta;
} HuffCode;

typedef struct MotionPixelsContext {
    AVCodecContext *avctx;
    AVFrame *frame;
    BswapDSPContext bdsp;
    uint8_t *changes_map;
    int offset_bits_len;
    int codes_count, current_codes_count;
    int max_codes_bits;
    HuffCode codes[MAX_HUFF_CODES];
    VLC vlc;
    YuvPixel *vpt, *hpt;
    uint8_t gradient_scale[3];
    uint8_t *bswapbuf;
    int bswapbuf_size;
} MotionPixelsContext;

static av_cold int mp_decode_end(AVCodecContext *avctx)
{
    MotionPixelsContext *mp = avctx->priv_data;

    av_freep(&mp->changes_map);
    av_freep(&mp->vpt);
    av_freep(&mp->hpt);
    av_freep(&mp->bswapbuf);
    av_frame_free(&mp->frame);

    return 0;
}

static av_cold int mp_decode_init(AVCodecContext *avctx)
{
    av_unused static AVOnce init_static_once = AV_ONCE_INIT;
    MotionPixelsContext *mp = avctx->priv_data;
    int w4 = (avctx->width  + 3) & ~3;
    int h4 = (avctx->height + 3) & ~3;

    if(avctx->extradata_size < 2){
        av_log(avctx, AV_LOG_ERROR, "extradata too small\n");
        return AVERROR_INVALIDDATA;
    }

    mp->avctx = avctx;
    ff_bswapdsp_init(&mp->bdsp);
    mp->changes_map = av_calloc(avctx->width, h4);
    mp->offset_bits_len = av_log2(avctx->width * avctx->height) + 1;
    mp->vpt = av_calloc(avctx->height, sizeof(*mp->vpt));
    mp->hpt = av_calloc(h4 / 4, w4 / 4 * sizeof(*mp->hpt));
    if (!mp->changes_map || !mp->vpt || !mp->hpt)
        return AVERROR(ENOMEM);
    avctx->pix_fmt = AV_PIX_FMT_RGB555;

    mp->frame = av_frame_alloc();
    if (!mp->frame)
        return AVERROR(ENOMEM);

#if !CONFIG_HARDCODED_TABLES
    ff_thread_once(&init_static_once, motionpixels_tableinit);
#endif

    return 0;
}

static void mp_read_changes_map(MotionPixelsContext *mp, GetBitContext *gb, int count, int bits_len, int read_color)
{
    uint16_t *pixels;
    int offset, w, h, color = 0, x, y, i;

    while (count--) {
        offset = get_bits_long(gb, mp->offset_bits_len);
        w      = get_bits(gb, bits_len) + 1;
        h      = get_bits(gb, bits_len) + 1;
        if (read_color)
            color = get_bits(gb, 15);
        x = offset % mp->avctx->width;
        y = offset / mp->avctx->width;
        if (y >= mp->avctx->height)
            continue;
        w = FFMIN(w, mp->avctx->width  - x);
        h = FFMIN(h, mp->avctx->height - y);
        pixels = (uint16_t *)&mp->frame->data[0][y * mp->frame->linesize[0] + x * 2];
        while (h--) {
            mp->changes_map[offset] = w;
            if (read_color)
                for (i = 0; i < w; ++i)
                    pixels[i] = color;
            offset += mp->avctx->width;
            pixels += mp->frame->linesize[0] / 2;
        }
    }
}

static int mp_get_code(MotionPixelsContext *mp, GetBitContext *gb, int size)
{
    while (get_bits1(gb)) {
        ++size;
        if (size > mp->max_codes_bits) {
            av_log(mp->avctx, AV_LOG_ERROR, "invalid code size %d/%d\n", size, mp->max_codes_bits);
            return AVERROR_INVALIDDATA;
        }
        if (mp_get_code(mp, gb, size) < 0)
            return AVERROR_INVALIDDATA;
    }
    if (mp->current_codes_count >= mp->codes_count) {
        av_log(mp->avctx, AV_LOG_ERROR, "too many codes\n");
        return AVERROR_INVALIDDATA;
    }

    mp->codes[mp->current_codes_count++].size = size;
    return 0;
}

static int mp_read_codes_table(MotionPixelsContext *mp, GetBitContext *gb)
{
    if (mp->codes_count == 1) {
        mp->codes[0].delta = get_bits(gb, 4);
    } else {
        int i;
        int ret;

        mp->max_codes_bits = get_bits(gb, 4);
        for (i = 0; i < mp->codes_count; ++i)
            mp->codes[i].delta = get_bits(gb, 4);
        mp->current_codes_count = 0;
        if ((ret = mp_get_code(mp, gb, 0)) < 0)
            return ret;
        if (mp->current_codes_count < mp->codes_count) {
            av_log(mp->avctx, AV_LOG_ERROR, "too few codes\n");
            return AVERROR_INVALIDDATA;
        }
   }
   return 0;
}

static av_always_inline int mp_gradient(MotionPixelsContext *mp, int component, int v)
{
    int delta;

    delta = (v - 7) * mp->gradient_scale[component];
    mp->gradient_scale[component] = (v == 0 || v == 14) ? 2 : 1;
    return delta;
}

static YuvPixel mp_get_yuv_from_rgb(MotionPixelsContext *mp, int x, int y)
{
    int color;

    color = *(uint16_t *)&mp->frame->data[0][y * mp->frame->linesize[0] + x * 2];
    return mp_rgb_yuv_table[color];
}

static void mp_set_rgb_from_yuv(MotionPixelsContext *mp, int x, int y, const YuvPixel *p)
{
    int color;

    color = mp_yuv_to_rgb(p->y, p->v, p->u, 1);
    *(uint16_t *)&mp->frame->data[0][y * mp->frame->linesize[0] + x * 2] = color;
}

static av_always_inline int mp_get_vlc(MotionPixelsContext *mp, GetBitContext *gb)
{
    return mp->vlc.table ? get_vlc2(gb, mp->vlc.table, mp->max_codes_bits, 1)
                         : mp->codes[0].delta;
}

static void mp_decode_line(MotionPixelsContext *mp, GetBitContext *gb, int y)
{
    YuvPixel p;
    const int y0 = y * mp->avctx->width;
    int w, i, x = 0;

    p = mp->vpt[y];
    if (mp->changes_map[y0 + x] == 0) {
        memset(mp->gradient_scale, 1, sizeof(mp->gradient_scale));
        ++x;
    }
    while (x < mp->avctx->width) {
        w = mp->changes_map[y0 + x];
        if (w != 0) {
            if ((y & 3) == 0) {
                if (mp->changes_map[y0 + x + mp->avctx->width] < w ||
                    mp->changes_map[y0 + x + mp->avctx->width * 2] < w ||
                    mp->changes_map[y0 + x + mp->avctx->width * 3] < w) {
                    for (i = (x + 3) & ~3; i < x + w; i += 4) {
                        mp->hpt[((y / 4) * mp->avctx->width + i) / 4] = mp_get_yuv_from_rgb(mp, i, y);
                    }
                }
            }
            x += w;
            memset(mp->gradient_scale, 1, sizeof(mp->gradient_scale));
            p = mp_get_yuv_from_rgb(mp, x - 1, y);
        } else {
            p.y += mp_gradient(mp, 0, mp_get_vlc(mp, gb));
            p.y = av_clip_uintp2(p.y, 5);
            if ((x & 3) == 0) {
                if ((y & 3) == 0) {
                    p.v += mp_gradient(mp, 1, mp_get_vlc(mp, gb));
                    p.v = av_clip_intp2(p.v, 5);
                    p.u += mp_gradient(mp, 2, mp_get_vlc(mp, gb));
                    p.u = av_clip_intp2(p.u, 5);
                    mp->hpt[((y / 4) * mp->avctx->width + x) / 4] = p;
                } else {
                    p.v = mp->hpt[((y / 4) * mp->avctx->width + x) / 4].v;
                    p.u = mp->hpt[((y / 4) * mp->avctx->width + x) / 4].u;
                }
            }
            mp_set_rgb_from_yuv(mp, x, y, &p);
            ++x;
        }
    }
}

static void mp_decode_frame_helper(MotionPixelsContext *mp, GetBitContext *gb)
{
    YuvPixel p;
    int y, y0;

    av_assert1(mp->changes_map[0]);

    for (y = 0; y < mp->avctx->height; ++y) {
        if (mp->changes_map[y * mp->avctx->width] != 0) {
            memset(mp->gradient_scale, 1, sizeof(mp->gradient_scale));
            p = mp_get_yuv_from_rgb(mp, 0, y);
        } else {
            p.y += mp_gradient(mp, 0, mp_get_vlc(mp, gb));
            p.y = av_clip_uintp2(p.y, 5);
            if ((y & 3) == 0) {
                p.v += mp_gradient(mp, 1, mp_get_vlc(mp, gb));
                p.v = av_clip_intp2(p.v, 5);
                p.u += mp_gradient(mp, 2, mp_get_vlc(mp, gb));
                p.u = av_clip_intp2(p.u, 5);
            }
            mp->vpt[y] = p;
            mp_set_rgb_from_yuv(mp, 0, y, &p);
        }
    }
    for (y0 = 0; y0 < 2; ++y0)
        for (y = y0; y < mp->avctx->height; y += 2)
            mp_decode_line(mp, gb, y);
}

static int mp_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                           int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MotionPixelsContext *mp = avctx->priv_data;
    GetBitContext gb;
    int i, count1, count2, sz, ret;

    if ((ret = ff_reget_buffer(avctx, mp->frame, 0)) < 0)
        return ret;

    /* le32 bitstream msb first */
    av_fast_padded_malloc(&mp->bswapbuf, &mp->bswapbuf_size, buf_size);
    if (!mp->bswapbuf)
        return AVERROR(ENOMEM);
    mp->bdsp.bswap_buf((uint32_t *) mp->bswapbuf, (const uint32_t *) buf,
                       buf_size / 4);
    if (buf_size & 3)
        memcpy(mp->bswapbuf + (buf_size & ~3), buf + (buf_size & ~3), buf_size & 3);
    init_get_bits(&gb, mp->bswapbuf, buf_size * 8);

    memset(mp->changes_map, 0, avctx->width * avctx->height);
    for (i = !(avctx->extradata[1] & 2); i < 2; ++i) {
        count1 = get_bits(&gb, 12);
        count2 = get_bits(&gb, 12);
        mp_read_changes_map(mp, &gb, count1, 8, i);
        mp_read_changes_map(mp, &gb, count2, 4, i);
    }

    mp->codes_count = get_bits(&gb, 4);
    if (mp->codes_count == 0)
        goto end;

    if (mp->changes_map[0] == 0) {
        *(uint16_t *)mp->frame->data[0] = get_bits(&gb, 15);
        mp->changes_map[0] = 1;
    }
    if (mp_read_codes_table(mp, &gb) < 0)
        goto end;

    sz = get_bits(&gb, 18);
    if (avctx->extradata[0] != 5)
        sz += get_bits(&gb, 18);
    if (sz == 0)
        goto end;

    if (mp->codes_count > 1) {
        /* The entries of the mp->codes array are sorted from right to left
         * in the Huffman tree, hence -(int)sizeof(HuffCode). */
        ret = ff_init_vlc_from_lengths(&mp->vlc, mp->max_codes_bits, mp->codes_count,
                                       &mp->codes[mp->codes_count - 1].size,  -(int)sizeof(HuffCode),
                                       &mp->codes[mp->codes_count - 1].delta, -(int)sizeof(HuffCode), 1,
                                       0, 0, avctx);
        if (ret < 0)
            goto end;
    }
    mp_decode_frame_helper(mp, &gb);
    ff_free_vlc(&mp->vlc);

end:
    if ((ret = av_frame_ref(rframe, mp->frame)) < 0)
        return ret;
    *got_frame       = 1;
    return buf_size;
}

const FFCodec ff_motionpixels_decoder = {
    .p.name         = "motionpixels",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Motion Pixels video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MOTIONPIXELS,
    .priv_data_size = sizeof(MotionPixelsContext),
    .init           = mp_decode_init,
    .close          = mp_decode_end,
    FF_CODEC_DECODE_CB(mp_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_INIT_THREADSAFE,
};
