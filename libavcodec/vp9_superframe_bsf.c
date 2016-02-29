/*
 * Vp9 invisible (alt-ref) frame to superframe merge bitstream filter
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "get_bits.h"

#define MAX_CACHE 8
typedef struct VP9BSFContext {
    int n_cache;
    struct CachedBuf {
        uint8_t *data;
        int size;
    } cache[MAX_CACHE];
} VP9BSFContext;

static void stats(const struct CachedBuf *in, int n_in,
                  unsigned *_max, unsigned *_sum)
{
    int n;
    unsigned max = 0, sum = 0;

    for (n = 0; n < n_in; n++) {
        unsigned sz = in[n].size;

        if (sz > max)
            max = sz;
        sum += sz;
    }

    *_max = max;
    *_sum = sum;
}

static int merge_superframe(const struct CachedBuf *in, int n_in,
                            uint8_t **poutbuf, int *poutbuf_size)
{
    unsigned max, sum, mag, marker, n, sz;
    uint8_t *ptr;

    stats(in, n_in, &max, &sum);
    mag = av_log2(max) >> 3;
    marker = 0xC0 + (mag << 3) + (n_in - 1);
    sz = *poutbuf_size = sum + 2 + (mag + 1) * n_in;
    ptr = *poutbuf = av_malloc(sz);
    if (!ptr)
        return AVERROR(ENOMEM);

    for (n = 0; n < n_in; n++) {
        memcpy(ptr, in[n].data, in[n].size);
        ptr += in[n].size;
    }

#define wloop(mag, wr) \
    for (n = 0; n < n_in; n++) { \
        wr; \
        ptr += mag + 1; \
    }

    // write superframe with marker 110[mag:2][nframes:3]
    *ptr++ = marker;
    switch (mag) {
    case 0:
        wloop(mag, *ptr = in[n].size);
        break;
    case 1:
        wloop(mag, AV_WB16(ptr, in[n].size));
        break;
    case 2:
        wloop(mag, AV_WB24(ptr, in[n].size));
        break;
    case 3:
        wloop(mag, AV_WB32(ptr, in[n].size));
        break;
    }
    *ptr++ = marker;
    av_assert0(ptr == &(*poutbuf)[*poutbuf_size]);

    return 0;
}

static int vp9_superframe_filter(AVBitStreamFilterContext *bsfc,
                                 AVCodecContext *avctx, const char *args,
                                 uint8_t  **poutbuf, int *poutbuf_size,
                                 const uint8_t *buf, int      buf_size,
                                 int keyframe)
{
    GetBitContext gb;
    VP9BSFContext *ctx = bsfc->priv_data;
    int res, invisible, profile, marker, uses_superframe_syntax = 0, n;

    marker = buf[buf_size - 1];
    if ((marker & 0xe0) == 0xc0) {
        int nbytes = 1 + ((marker >> 3) & 0x3);
        int n_frames = 1 + (marker & 0x7), idx_sz = 2 + n_frames * nbytes;

        uses_superframe_syntax = buf_size >= idx_sz && buf[buf_size - idx_sz] == marker;
    }

    if ((res = init_get_bits8(&gb, buf, buf_size)) < 0)
        return res;

    get_bits(&gb, 2); // frame marker
    profile  = get_bits1(&gb);
    profile |= get_bits1(&gb) << 1;
    if (profile == 3) profile += get_bits1(&gb);

    if (get_bits1(&gb)) {
        invisible = 0;
    } else {
        get_bits1(&gb); // keyframe
        invisible = !get_bits1(&gb);
    }

    if (uses_superframe_syntax && ctx->n_cache > 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Mixing of superframe syntax and naked VP9 frames not supported");
        return AVERROR_INVALIDDATA;
    } else if ((!invisible || uses_superframe_syntax) && !ctx->n_cache) {
        // passthrough
        *poutbuf = (uint8_t *) buf;
        *poutbuf_size = buf_size;
        return 0;
    } else if (ctx->n_cache + 1 >= MAX_CACHE) {
        av_log(avctx, AV_LOG_ERROR,
               "Too many invisible frames");
        return AVERROR_INVALIDDATA;
    }

    ctx->cache[ctx->n_cache].size = buf_size;
    if (invisible && !uses_superframe_syntax) {
        ctx->cache[ctx->n_cache].data = av_malloc(buf_size);
        if (!ctx->cache[ctx->n_cache].data)
            return AVERROR(ENOMEM);
        memcpy(ctx->cache[ctx->n_cache++].data, buf, buf_size);
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return 0;
    }
    av_assert0(ctx->n_cache > 0);

    ctx->cache[ctx->n_cache].data = (uint8_t *) buf;

    // build superframe
    if ((res = merge_superframe(ctx->cache, ctx->n_cache + 1,
                                poutbuf, poutbuf_size)) < 0)
        return res;

    for (n = 0; n < ctx->n_cache; n++)
        av_freep(&ctx->cache[n].data);
    ctx->n_cache = 0;

    return 0;
}

static void vp9_superframe_close(AVBitStreamFilterContext *bsfc)
{
    VP9BSFContext *ctx = bsfc->priv_data;
    int n;

    // free cached data
    for (n = 0; n < ctx->n_cache; n++)
        av_freep(&ctx->cache[n].data);
}

AVBitStreamFilter ff_vp9_superframe_bsf = {
    .name           = "vp9_superframe",
    .priv_data_size = sizeof(VP9BSFContext),
    .filter         = vp9_superframe_filter,
    .close          = vp9_superframe_close,
};
