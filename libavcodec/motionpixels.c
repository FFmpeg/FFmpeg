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

#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"

#define MAX_HUFF_CODES 16

typedef struct YuvPixel {
    int8_t y, v, u;
} YuvPixel;

typedef struct HuffCode {
    int code;
    uint8_t size;
    uint8_t delta;
} HuffCode;

typedef struct MotionPixelsContext {
    AVCodecContext *avctx;
    AVFrame frame;
    DSPContext dsp;
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

static YuvPixel mp_rgb_yuv_table[1 << 15];

static int mp_yuv_to_rgb(int y, int v, int u, int clip_rgb) {
    static const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int r, g, b;

    r = (1000 * y + 701 * v) / 1000;
    g = (1000 * y - 357 * v - 172 * u) / 1000;
    b = (1000 * y + 886 * u) / 1000;
    if (clip_rgb)
        return ((cm[r * 8] & 0xF8) << 7) | ((cm[g * 8] & 0xF8) << 2) | (cm[b * 8] >> 3);
    if ((unsigned)r < 32 && (unsigned)g < 32 && (unsigned)b < 32)
        return (r << 10) | (g << 5) | b;
    return 1 << 15;
}

static void mp_set_zero_yuv(YuvPixel *p)
{
    int i, j;

    for (i = 0; i < 31; ++i) {
        for (j = 31; j > i; --j)
            if (!(p[j].u | p[j].v | p[j].y))
                p[j] = p[j - 1];
        for (j = 0; j < 31 - i; ++j)
            if (!(p[j].u | p[j].v | p[j].y))
                p[j] = p[j + 1];
    }
}

static void mp_build_rgb_yuv_table(YuvPixel *p)
{
    int y, v, u, i;

    for (y = 0; y <= 31; ++y)
        for (v = -31; v <= 31; ++v)
            for (u = -31; u <= 31; ++u) {
                i = mp_yuv_to_rgb(y, v, u, 0);
                if (i < (1 << 15) && !(p[i].u | p[i].v | p[i].y)) {
                    p[i].y = y;
                    p[i].v = v;
                    p[i].u = u;
                }
            }
    for (i = 0; i < 1024; ++i)
        mp_set_zero_yuv(p + i * 32);
}

static av_cold int mp_decode_init(AVCodecContext *avctx)
{
    MotionPixelsContext *mp = avctx->priv_data;

    if (!mp_rgb_yuv_table[0].u) {
        mp_build_rgb_yuv_table(mp_rgb_yuv_table);
    }
    mp->avctx = avctx;
    dsputil_init(&mp->dsp, avctx);
    mp->changes_map = av_mallocz(avctx->width * avctx->height);
    mp->offset_bits_len = av_log2(avctx->width * avctx->height) + 1;
    mp->vpt = av_mallocz(avctx->height * sizeof(YuvPixel));
    mp->hpt = av_mallocz(avctx->height * avctx->width / 16 * sizeof(YuvPixel));
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
        pixels = (uint16_t *)&mp->frame.data[0][y * mp->frame.linesize[0] + x * 2];
        while (h--) {
            mp->changes_map[offset] = w;
            if (read_color)
                for (i = 0; i < w; ++i)
                    pixels[i] = color;
            offset += mp->avctx->width;
            pixels += mp->frame.linesize[0] / 2;
        }
    }
}

static void mp_get_code(MotionPixelsContext *mp, GetBitContext *gb, int size, int code)
{
    while (get_bits1(gb)) {
        ++size;
        if (size > mp->max_codes_bits) {
            av_log(mp->avctx, AV_LOG_ERROR, "invalid code size %d/%d\n", size, mp->max_codes_bits);
            return;
        }
        code <<= 1;
        mp_get_code(mp, gb, size, code + 1);
    }
    if (mp->current_codes_count >= MAX_HUFF_CODES) {
        av_log(mp->avctx, AV_LOG_ERROR, "too many codes\n");
        return;
    }
    mp->codes[mp->current_codes_count  ].code = code;
    mp->codes[mp->current_codes_count++].size = size;
}

static void mp_read_codes_table(MotionPixelsContext *mp, GetBitContext *gb)
{
    if (mp->codes_count == 1) {
        mp->codes[0].delta = get_bits(gb, 4);
    } else {
        int i;

        mp->max_codes_bits = get_bits(gb, 4);
        for (i = 0; i < mp->codes_count; ++i)
            mp->codes[i].delta = get_bits(gb, 4);
        mp->current_codes_count = 0;
        mp_get_code(mp, gb, 0, 0);
   }
}

static int mp_gradient(MotionPixelsContext *mp, int component, int v)
{
    int delta;

    delta = (v - 7) * mp->gradient_scale[component];
    mp->gradient_scale[component] = (v == 0 || v == 14) ? 2 : 1;
    return delta;
}

static YuvPixel mp_get_yuv_from_rgb(MotionPixelsContext *mp, int x, int y)
{
    int color;

    color = *(uint16_t *)&mp->frame.data[0][y * mp->frame.linesize[0] + x * 2];
    return mp_rgb_yuv_table[color];
}

static void mp_set_rgb_from_yuv(MotionPixelsContext *mp, int x, int y, const YuvPixel *p)
{
    int color;

    color = mp_yuv_to_rgb(p->y, p->v, p->u, 1);
    *(uint16_t *)&mp->frame.data[0][y * mp->frame.linesize[0] + x * 2] = color;
}

static int mp_get_vlc(MotionPixelsContext *mp, GetBitContext *gb)
{
    int i;

    i = (mp->codes_count == 1) ? 0 : get_vlc2(gb, mp->vlc.table, mp->max_codes_bits, 1);
    return mp->codes[i].delta;
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
            if ((x & 3) == 0) {
                if ((y & 3) == 0) {
                    p.v += mp_gradient(mp, 1, mp_get_vlc(mp, gb));
                    p.u += mp_gradient(mp, 2, mp_get_vlc(mp, gb));
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

    for (y = 0; y < mp->avctx->height; ++y) {
        if (mp->changes_map[y * mp->avctx->width] != 0) {
            memset(mp->gradient_scale, 1, sizeof(mp->gradient_scale));
            p = mp_get_yuv_from_rgb(mp, 0, y);
        } else {
            p.y += mp_gradient(mp, 0, mp_get_vlc(mp, gb));
            if ((y & 3) == 0) {
                p.v += mp_gradient(mp, 1, mp_get_vlc(mp, gb));
                p.u += mp_gradient(mp, 2, mp_get_vlc(mp, gb));
            }
            mp->vpt[y] = p;
            mp_set_rgb_from_yuv(mp, 0, y, &p);
        }
    }
    for (y0 = 0; y0 < 2; ++y0)
        for (y = y0; y < mp->avctx->height; y += 2)
            mp_decode_line(mp, gb, y);
}

static int mp_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 const uint8_t *buf, int buf_size)
{
    MotionPixelsContext *mp = avctx->priv_data;
    GetBitContext gb;
    int i, count1, count2, sz;

    mp->frame.reference = 1;
    mp->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &mp->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    /* le32 bitstream msb first */
    mp->bswapbuf = av_fast_realloc(mp->bswapbuf, &mp->bswapbuf_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    mp->dsp.bswap_buf((uint32_t *)mp->bswapbuf, (const uint32_t *)buf, buf_size / 4);
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
        *(uint16_t *)mp->frame.data[0] = get_bits(&gb, 15);
        mp->changes_map[0] = 1;
    }
    mp_read_codes_table(mp, &gb);

    sz = get_bits(&gb, 18);
    if (avctx->extradata[0] != 5)
        sz += get_bits(&gb, 18);
    if (sz == 0)
        goto end;

    init_vlc(&mp->vlc, mp->max_codes_bits, mp->codes_count, &mp->codes[0].size, sizeof(HuffCode), 1, &mp->codes[0].code, sizeof(HuffCode), 4, 0);
    mp_decode_frame_helper(mp, &gb);
    free_vlc(&mp->vlc);

end:
    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = mp->frame;
    return buf_size;
}

static av_cold int mp_decode_end(AVCodecContext *avctx)
{
    MotionPixelsContext *mp = avctx->priv_data;

    av_freep(&mp->changes_map);
    av_freep(&mp->vpt);
    av_freep(&mp->hpt);
    av_freep(&mp->bswapbuf);
    if (mp->frame.data[0])
        avctx->release_buffer(avctx, &mp->frame);

    return 0;
}

AVCodec motionpixels_decoder = {
    "motionpixels",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MOTIONPIXELS,
    sizeof(MotionPixelsContext),
    mp_decode_init,
    NULL,
    mp_decode_end,
    mp_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Motion Pixels Video"),
};
