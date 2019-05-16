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
#include "bsf.h"
#include "get_bits.h"

#define MAX_CACHE 8
typedef struct VP9BSFContext {
    int n_cache;
    AVPacket *cache[MAX_CACHE];
} VP9BSFContext;

static void stats(AVPacket * const *in, int n_in,
                  unsigned *_max, unsigned *_sum)
{
    int n;
    unsigned max = 0, sum = 0;

    for (n = 0; n < n_in; n++) {
        unsigned sz = in[n]->size;

        if (sz > max)
            max = sz;
        sum += sz;
    }

    *_max = max;
    *_sum = sum;
}

static int merge_superframe(AVPacket * const *in, int n_in, AVPacket *out)
{
    unsigned max, sum, mag, marker, n, sz;
    uint8_t *ptr;
    int res;

    stats(in, n_in, &max, &sum);
    mag = av_log2(max) >> 3;
    marker = 0xC0 + (mag << 3) + (n_in - 1);
    sz = sum + 2 + (mag + 1) * n_in;
    res = av_new_packet(out, sz);
    if (res < 0)
        return res;
    ptr = out->data;
    for (n = 0; n < n_in; n++) {
        memcpy(ptr, in[n]->data, in[n]->size);
        ptr += in[n]->size;
    }

#define wloop(mag, wr) \
    do { \
        for (n = 0; n < n_in; n++) { \
            wr; \
            ptr += mag + 1; \
        } \
    } while (0)

    // write superframe with marker 110[mag:2][nframes:3]
    *ptr++ = marker;
    switch (mag) {
    case 0:
        wloop(mag, *ptr = in[n]->size);
        break;
    case 1:
        wloop(mag, AV_WL16(ptr, in[n]->size));
        break;
    case 2:
        wloop(mag, AV_WL24(ptr, in[n]->size));
        break;
    case 3:
        wloop(mag, AV_WL32(ptr, in[n]->size));
        break;
    }
    *ptr++ = marker;
    av_assert0(ptr == &out->data[out->size]);

    return 0;
}

static int vp9_superframe_filter(AVBSFContext *ctx, AVPacket *out)
{
    GetBitContext gb;
    VP9BSFContext *s = ctx->priv_data;
    AVPacket *in;
    int res, invisible, profile, marker, uses_superframe_syntax = 0, n;

    res = ff_bsf_get_packet(ctx, &in);
    if (res < 0)
        return res;

    marker = in->data[in->size - 1];
    if ((marker & 0xe0) == 0xc0) {
        int nbytes = 1 + ((marker >> 3) & 0x3);
        int n_frames = 1 + (marker & 0x7), idx_sz = 2 + n_frames * nbytes;

        uses_superframe_syntax = in->size >= idx_sz && in->data[in->size - idx_sz] == marker;
    }

    if ((res = init_get_bits8(&gb, in->data, in->size)) < 0)
        goto done;

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

    if (uses_superframe_syntax && s->n_cache > 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Mixing of superframe syntax and naked VP9 frames not supported\n");
        res = AVERROR(ENOSYS);
        goto done;
    } else if ((!invisible || uses_superframe_syntax) && !s->n_cache) {
        // passthrough
        av_packet_move_ref(out, in);
        goto done;
    } else if (s->n_cache + 1 >= MAX_CACHE) {
        av_log(ctx, AV_LOG_ERROR,
               "Too many invisible frames\n");
        res = AVERROR_INVALIDDATA;
        goto done;
    }

    av_packet_move_ref(s->cache[s->n_cache++], in);

    if (invisible) {
        res = AVERROR(EAGAIN);
        goto done;
    }
    av_assert0(s->n_cache > 0);

    // build superframe
    if ((res = merge_superframe(s->cache, s->n_cache, out)) < 0)
        goto done;

    res = av_packet_copy_props(out, s->cache[s->n_cache - 1]);
    if (res < 0)
        goto done;

    for (n = 0; n < s->n_cache; n++)
        av_packet_unref(s->cache[n]);
    s->n_cache = 0;

done:
    if (res < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return res;
}

static int vp9_superframe_init(AVBSFContext *ctx)
{
    VP9BSFContext *s = ctx->priv_data;
    int n;

    // alloc cache packets
    for (n = 0; n < MAX_CACHE; n++) {
        s->cache[n] = av_packet_alloc();
        if (!s->cache[n])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void vp9_superframe_flush(AVBSFContext *ctx)
{
    VP9BSFContext *s = ctx->priv_data;
    int n;

    // unref cached data
    for (n = 0; n < s->n_cache; n++)
        av_packet_unref(s->cache[n]);
    s->n_cache = 0;
}

static void vp9_superframe_close(AVBSFContext *ctx)
{
    VP9BSFContext *s = ctx->priv_data;
    int n;

    // free cached data
    for (n = 0; n < MAX_CACHE; n++)
        av_packet_free(&s->cache[n]);
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_VP9, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_vp9_superframe_bsf = {
    .name           = "vp9_superframe",
    .priv_data_size = sizeof(VP9BSFContext),
    .filter         = vp9_superframe_filter,
    .init           = vp9_superframe_init,
    .flush          = vp9_superframe_flush,
    .close          = vp9_superframe_close,
    .codec_ids      = codec_ids,
};
