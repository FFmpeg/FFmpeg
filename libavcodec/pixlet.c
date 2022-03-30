/*
 * Apple Pixlet decoder
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

#include <stdint.h>

#include "libavutil/imgutils.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "internal.h"
#include "thread.h"
#include "unary.h"

#define NB_LEVELS 4

#define PIXLET_MAGIC 0xDEADBEEF

#define H 0
#define V 1

typedef struct SubBand {
    unsigned width, height;
    unsigned size;
    unsigned x, y;
} SubBand;

typedef struct PixletContext {
    AVClass *class;

    GetByteContext gb;
    GetBitContext bc;

    int levels;
    int depth;
    int w, h;

    int16_t *filter[2];
    int16_t *prediction;
    int64_t scaling[4][2][NB_LEVELS];
    uint16_t lut[65536];
    SubBand band[4][NB_LEVELS * 3 + 1];
} PixletContext;

static av_cold int pixlet_init(AVCodecContext *avctx)
{
    avctx->pix_fmt     = AV_PIX_FMT_YUV420P16;
    avctx->color_range = AVCOL_RANGE_JPEG;
    return 0;
}

static void free_buffers(AVCodecContext *avctx)
{
    PixletContext *ctx = avctx->priv_data;

    av_freep(&ctx->filter[0]);
    av_freep(&ctx->filter[1]);
    av_freep(&ctx->prediction);
}

static av_cold int pixlet_close(AVCodecContext *avctx)
{
    PixletContext *ctx = avctx->priv_data;
    free_buffers(avctx);
    ctx->w = 0;
    ctx->h = 0;
    return 0;
}

static int init_decoder(AVCodecContext *avctx)
{
    PixletContext *ctx = avctx->priv_data;
    int i, plane;

    ctx->filter[0]  = av_malloc_array(ctx->h, sizeof(int16_t));
    ctx->filter[1]  = av_malloc_array(FFMAX(ctx->h, ctx->w) + 16, sizeof(int16_t));
    ctx->prediction = av_malloc_array((ctx->w >> NB_LEVELS), sizeof(int16_t));
    if (!ctx->filter[0] || !ctx->filter[1] || !ctx->prediction)
        return AVERROR(ENOMEM);

    for (plane = 0; plane < 3; plane++) {
        unsigned shift = plane > 0;
        unsigned w     = ctx->w >> shift;
        unsigned h     = ctx->h >> shift;

        ctx->band[plane][0].width  =  w >> NB_LEVELS;
        ctx->band[plane][0].height =  h >> NB_LEVELS;
        ctx->band[plane][0].size   = (w >> NB_LEVELS) * (h >> NB_LEVELS);

        for (i = 0; i < NB_LEVELS * 3; i++) {
            unsigned scale = ctx->levels - (i / 3);

            ctx->band[plane][i + 1].width  =  w >> scale;
            ctx->band[plane][i + 1].height =  h >> scale;
            ctx->band[plane][i + 1].size   = (w >> scale) * (h >> scale);

            ctx->band[plane][i + 1].x = (w >> scale) * (((i + 1) % 3) != 2);
            ctx->band[plane][i + 1].y = (h >> scale) * (((i + 1) % 3) != 1);
        }
    }

    return 0;
}

static int read_low_coeffs(AVCodecContext *avctx, int16_t *dst, int size,
                           int width, ptrdiff_t stride)
{
    PixletContext *ctx = avctx->priv_data;
    GetBitContext *bc = &ctx->bc;
    unsigned cnt1, nbits, k, j = 0, i = 0;
    int64_t value, state = 3;
    int rlen, escape, flag = 0;

    while (i < size) {
        nbits = FFMIN(ff_clz((state >> 8) + 3) ^ 0x1F, 14);

        cnt1 = get_unary(bc, 0, 8);
        if (cnt1 < 8) {
            value = show_bits(bc, nbits);
            if (value <= 1) {
                skip_bits(bc, nbits - 1);
                escape = ((1 << nbits) - 1) * cnt1;
            } else {
                skip_bits(bc, nbits);
                escape = value + ((1 << nbits) - 1) * cnt1 - 1;
            }
        } else {
            escape = get_bits(bc, 16);
        }

        value    = -((escape + flag) & 1) | 1;
        dst[j++] = value * ((escape + flag + 1) >> 1);
        i++;
        if (j == width) {
            j    = 0;
            dst += stride;
        }
        state = 120 * (escape + flag) + state - (120 * state >> 8);
        flag  = 0;

        if (state * 4ULL > 0xFF || i >= size)
            continue;

        nbits  = ((state + 8) >> 5) + (state ? ff_clz(state) : 32) - 24;
        escape = av_mod_uintp2(16383, nbits);
        cnt1   = get_unary(bc, 0, 8);
        if (cnt1 > 7) {
            rlen = get_bits(bc, 16);
        } else {
            value = show_bits(bc, nbits);
            if (value > 1) {
                skip_bits(bc, nbits);
                rlen = value + escape * cnt1 - 1;
            } else {
                skip_bits(bc, nbits - 1);
                rlen = escape * cnt1;
            }
        }

        if (rlen > size - i)
            return AVERROR_INVALIDDATA;
        i += rlen;

        for (k = 0; k < rlen; k++) {
            dst[j++] = 0;
            if (j == width) {
                j    = 0;
                dst += stride;
            }
        }

        state = 0;
        flag  = rlen < 0xFFFF ? 1 : 0;
    }

    align_get_bits(bc);
    return get_bits_count(bc) >> 3;
}

static int read_high_coeffs(AVCodecContext *avctx, uint8_t *src, int16_t *dst,
                            int size, int c, int a, int d,
                            int width, ptrdiff_t stride)
{
    PixletContext *ctx = avctx->priv_data;
    GetBitContext *bc = &ctx->bc;
    unsigned cnt1, shbits, rlen, nbits, length, i = 0, j = 0, k;
    int ret, escape, pfx, value, yflag, xflag, flag = 0;
    int64_t state = 3, tmp;

    ret = init_get_bits8(bc, src, bytestream2_get_bytes_left(&ctx->gb));
    if (ret < 0)
        return ret;

    if (a ^ (a >> 31)) {
        nbits = 33 - ff_clz(a ^ (a >> 31));
        if (nbits > 16)
            return AVERROR_INVALIDDATA;
    } else {
        nbits = 1;
    }

    length = 25 - nbits;

    while (i < size) {
        if (((state >> 8) + 3) & 0xFFFFFFF)
            value = ff_clz((state >> 8) + 3) ^ 0x1F;
        else
            value = -1;

        cnt1 = get_unary(bc, 0, length);
        if (cnt1 >= length) {
            cnt1 = get_bits(bc, nbits);
        } else {
            pfx = 14 + ((((uint64_t)(value - 14)) >> 32) & (value - 14));
            if (pfx < 1 || pfx > 25)
                return AVERROR_INVALIDDATA;
            cnt1 *= (1 << pfx) - 1;
            shbits = show_bits(bc, pfx);
            if (shbits <= 1) {
                skip_bits(bc, pfx - 1);
            } else {
                skip_bits(bc, pfx);
                cnt1 += shbits - 1;
            }
        }

        xflag = flag + cnt1;
        yflag = xflag;

        if (flag + cnt1 == 0) {
            value = 0;
        } else {
            xflag &= 1u;
            tmp    = (int64_t)c * ((yflag + 1) >> 1) + (c >> 1);
            value  = xflag + (tmp ^ -xflag);
        }

        i++;
        dst[j++] = value;
        if (j == width) {
            j    = 0;
            dst += stride;
        }
        state += (int64_t)d * (uint64_t)yflag - ((int64_t)(d * (uint64_t)state) >> 8);

        flag = 0;

        if ((uint64_t)state > 0xFF / 4 || i >= size)
            continue;

        pfx    = ((state + 8) >> 5) + (state ? ff_clz(state) : 32) - 24;
        escape = av_mod_uintp2(16383, pfx);
        cnt1   = get_unary(bc, 0, 8);
        if (cnt1 < 8) {
            if (pfx < 1 || pfx > 25)
                return AVERROR_INVALIDDATA;

            value = show_bits(bc, pfx);
            if (value > 1) {
                skip_bits(bc, pfx);
                rlen = value + escape * cnt1 - 1;
            } else {
                skip_bits(bc, pfx - 1);
                rlen = escape * cnt1;
            }
        } else {
            if (get_bits1(bc))
                value = get_bits(bc, 16);
            else
                value = get_bits(bc, 8);

            rlen = value + 8 * escape;
        }

        if (rlen > 0xFFFF || i + rlen > size)
            return AVERROR_INVALIDDATA;
        i += rlen;

        for (k = 0; k < rlen; k++) {
            dst[j++] = 0;
            if (j == width) {
                j    = 0;
                dst += stride;
            }
        }

        state = 0;
        flag  = rlen < 0xFFFF ? 1 : 0;
    }

    align_get_bits(bc);
    return get_bits_count(bc) >> 3;
}

static int read_highpass(AVCodecContext *avctx, uint8_t *ptr,
                         int plane, AVFrame *frame)
{
    PixletContext *ctx = avctx->priv_data;
    ptrdiff_t stride = frame->linesize[plane] / 2;
    int i, ret;

    for (i = 0; i < ctx->levels * 3; i++) {
        int32_t a = bytestream2_get_be32(&ctx->gb);
        int32_t b = bytestream2_get_be32(&ctx->gb);
        int32_t c = bytestream2_get_be32(&ctx->gb);
        int32_t d = bytestream2_get_be32(&ctx->gb);
        int16_t *dest = (int16_t *)frame->data[plane] +
                        ctx->band[plane][i + 1].x +
                        ctx->band[plane][i + 1].y * stride;
        unsigned size = ctx->band[plane][i + 1].size;
        uint32_t magic = bytestream2_get_be32(&ctx->gb);

        if (magic != PIXLET_MAGIC) {
            av_log(avctx, AV_LOG_ERROR,
                   "wrong magic number: 0x%08"PRIX32" for plane %d, band %d\n",
                   magic, plane, i);
            return AVERROR_INVALIDDATA;
        }

        if (a == INT32_MIN)
            return AVERROR_INVALIDDATA;

        ret = read_high_coeffs(avctx, ptr + bytestream2_tell(&ctx->gb), dest, size,
                               c, (b >= FFABS(a)) ? b : a, d,
                               ctx->band[plane][i + 1].width, stride);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "error in highpass coefficients for plane %d, band %d\n",
                   plane, i);
            return ret;
        }
        bytestream2_skip(&ctx->gb, ret);
    }

    return 0;
}

static void lowpass_prediction(int16_t *dst, int16_t *pred,
                               int width, int height, ptrdiff_t stride)
{
    int16_t val;
    int i, j;

    memset(pred, 0, width * sizeof(*pred));

    for (i = 0; i < height; i++) {
        val    = pred[0] + dst[0];
        dst[0] = pred[0] = val;
        for (j = 1; j < width; j++) {
            val     = pred[j] + dst[j];
            dst[j]  = pred[j] = val;
            dst[j] += dst[j-1];
        }
        dst += stride;
    }
}

static void filterfn(int16_t *dest, int16_t *tmp, unsigned size, int64_t scale)
{
    int16_t *low, *high, *ll, *lh, *hl, *hh;
    int hsize, i, j;
    int64_t value;

    hsize = size >> 1;
    low   = tmp + 4;
    high  = &low[hsize + 8];

    memcpy(low, dest, size);
    memcpy(high, dest + hsize, size);

    ll = &low[hsize];
    lh = &low[hsize];
    hl = &high[hsize];
    hh = hl;
    for (i = 4, j = 2; i; i--, j++, ll--, hh++, lh++, hl--) {
        low[i - 5]  = low[j - 1];
        lh[0]       = ll[-1];
        high[i - 5] = high[j - 2];
        hh[0]       = hl[-2];
    }

    for (i = 0; i < hsize; i++) {
        value = (int64_t) low [i + 1] * -INT64_C(325392907)  +
                (int64_t) low [i + 0] *  INT64_C(3687786320) +
                (int64_t) low [i - 1] * -INT64_C(325392907)  +
                (int64_t) high[i + 0] *  INT64_C(1518500249) +
                (int64_t) high[i - 1] *  INT64_C(1518500249);
        dest[i * 2] = av_clip_int16(((value >> 32) * (uint64_t)scale) >> 32);
    }

    for (i = 0; i < hsize; i++) {
        value = (int64_t) low [i + 2] * -INT64_C(65078576)   +
                (int64_t) low [i + 1] *  INT64_C(1583578880) +
                (int64_t) low [i + 0] *  INT64_C(1583578880) +
                (int64_t) low [i - 1] * -INT64_C(65078576)   +
                (int64_t) high[i + 1] *  INT64_C(303700064)  +
                (int64_t) high[i + 0] * -INT64_C(3644400640) +
                (int64_t) high[i - 1] *  INT64_C(303700064);
        dest[i * 2 + 1] = av_clip_int16(((value >> 32) * (uint64_t)scale) >> 32);
    }
}

static void reconstruction(AVCodecContext *avctx, int16_t *dest,
                           unsigned width, unsigned height, ptrdiff_t stride,
                           int64_t *scaling_h, int64_t *scaling_v)
{
    PixletContext *ctx = avctx->priv_data;
    unsigned scaled_width, scaled_height;
    int16_t *ptr, *tmp;
    int i, j, k;

    scaled_width  = width  >> NB_LEVELS;
    scaled_height = height >> NB_LEVELS;
    tmp           = ctx->filter[0];

    for (i = 0; i < NB_LEVELS; i++) {
        int64_t scale_v = scaling_v[i];
        int64_t scale_h = scaling_h[i];
        scaled_width  <<= 1;
        scaled_height <<= 1;

        ptr = dest;
        for (j = 0; j < scaled_height; j++) {
            filterfn(ptr, ctx->filter[1], scaled_width, scale_v);
            ptr += stride;
        }

        for (j = 0; j < scaled_width; j++) {
            ptr = dest + j;
            for (k = 0; k < scaled_height; k++) {
                tmp[k] = *ptr;
                ptr   += stride;
            }

            filterfn(tmp, ctx->filter[1], scaled_height, scale_h);

            ptr = dest + j;
            for (k = 0; k < scaled_height; k++) {
                *ptr = tmp[k];
                ptr += stride;
            }
        }
    }
}

static void build_luma_lut(AVCodecContext *avctx, int depth)
{
    PixletContext *ctx = avctx->priv_data;
    int max = (1 << depth) - 1;

    if (ctx->depth == depth)
        return;
    ctx->depth = depth;

    for (int i = 0; i < FF_ARRAY_ELEMS(ctx->lut); i++)
        ctx->lut[i] = ((int64_t)i * i * 65535LL) / max / max;
}

static void postprocess_luma(AVCodecContext *avctx, AVFrame *frame,
                             int w, int h, int depth)
{
    PixletContext *ctx = avctx->priv_data;
    uint16_t *dsty = (uint16_t *)frame->data[0];
    int16_t *srcy  = (int16_t *)frame->data[0];
    ptrdiff_t stridey = frame->linesize[0] / 2;
    uint16_t *lut = ctx->lut;
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            if (srcy[i] <= 0)
                dsty[i] = 0;
            else if (srcy[i] > ((1 << depth) - 1))
                dsty[i] = 65535;
            else
                dsty[i] = lut[srcy[i]];
        }
        dsty += stridey;
        srcy += stridey;
    }
}

static void postprocess_chroma(AVFrame *frame, int w, int h, int depth)
{
    uint16_t *dstu = (uint16_t *)frame->data[1];
    uint16_t *dstv = (uint16_t *)frame->data[2];
    int16_t *srcu  = (int16_t *)frame->data[1];
    int16_t *srcv  = (int16_t *)frame->data[2];
    ptrdiff_t strideu = frame->linesize[1] / 2;
    ptrdiff_t stridev = frame->linesize[2] / 2;
    const unsigned add = 1 << (depth - 1);
    const unsigned shift = 16 - depth;
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            dstu[i] = av_clip_uintp2_c(add + srcu[i], depth) << shift;
            dstv[i] = av_clip_uintp2_c(add + srcv[i], depth) << shift;
        }
        dstu += strideu;
        dstv += stridev;
        srcu += strideu;
        srcv += stridev;
    }
}

static int decode_plane(AVCodecContext *avctx, int plane,
                        const AVPacket *avpkt, AVFrame *frame)
{
    PixletContext *ctx = avctx->priv_data;
    ptrdiff_t stride   = frame->linesize[plane] / 2;
    unsigned shift     = plane > 0;
    int16_t *dst;
    int i, ret;

    for (i = ctx->levels - 1; i >= 0; i--) {
        int32_t h = sign_extend(bytestream2_get_be32(&ctx->gb), 32);
        int32_t v = sign_extend(bytestream2_get_be32(&ctx->gb), 32);

        if (!h || !v)
            return AVERROR_INVALIDDATA;

        ctx->scaling[plane][H][i] = (1000000ULL << 32) / h;
        ctx->scaling[plane][V][i] = (1000000ULL << 32) / v;
    }

    bytestream2_skip(&ctx->gb, 4);

    dst    = (int16_t *)frame->data[plane];
    dst[0] = sign_extend(bytestream2_get_be16(&ctx->gb), 16);

    ret = init_get_bits8(&ctx->bc, avpkt->data + bytestream2_tell(&ctx->gb),
                         bytestream2_get_bytes_left(&ctx->gb));
    if (ret < 0)
        return ret;

    ret = read_low_coeffs(avctx, dst + 1, ctx->band[plane][0].width - 1,
                          ctx->band[plane][0].width - 1, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "error in lowpass coefficients for plane %d, top row\n", plane);
        return ret;
    }

    ret = read_low_coeffs(avctx, dst + stride,
                          ctx->band[plane][0].height - 1, 1, stride);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "error in lowpass coefficients for plane %d, left column\n",
               plane);
        return ret;
    }

    ret = read_low_coeffs(avctx, dst + stride + 1,
                          (ctx->band[plane][0].width - 1) * (ctx->band[plane][0].height - 1),
                          ctx->band[plane][0].width - 1, stride);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "error in lowpass coefficients for plane %d, rest\n", plane);
        return ret;
    }

    bytestream2_skip(&ctx->gb, ret);
    if (bytestream2_get_bytes_left(&ctx->gb) <= 0) {
        av_log(avctx, AV_LOG_ERROR, "no bytes left\n");
        return AVERROR_INVALIDDATA;
    }

    ret = read_highpass(avctx, avpkt->data, plane, frame);
    if (ret < 0)
        return ret;

    lowpass_prediction(dst, ctx->prediction, ctx->band[plane][0].width,
                       ctx->band[plane][0].height, stride);

    reconstruction(avctx, (int16_t *)frame->data[plane], ctx->w >> shift,
                   ctx->h >> shift, stride, ctx->scaling[plane][H],
                   ctx->scaling[plane][V]);

    return 0;
}

static int pixlet_decode_frame(AVCodecContext *avctx, AVFrame *p,
                               int *got_frame, AVPacket *avpkt)
{
    PixletContext *ctx = avctx->priv_data;
    int i, w, h, width, height, ret, version;
    uint32_t pktsize, depth;

    bytestream2_init(&ctx->gb, avpkt->data, avpkt->size);

    pktsize = bytestream2_get_be32(&ctx->gb);
    if (pktsize <= 44 || pktsize - 4 > bytestream2_get_bytes_left(&ctx->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid packet size %"PRIu32"\n", pktsize);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream2_get_le32(&ctx->gb);
    if (version != 1)
        avpriv_request_sample(avctx, "Version %d", version);

    bytestream2_skip(&ctx->gb, 4);
    if (bytestream2_get_be32(&ctx->gb) != 1)
        return AVERROR_INVALIDDATA;
    bytestream2_skip(&ctx->gb, 4);

    width  = bytestream2_get_be32(&ctx->gb);
    height = bytestream2_get_be32(&ctx->gb);

    if (    width > INT_MAX - (1U << (NB_LEVELS + 1))
        || height > INT_MAX - (1U << (NB_LEVELS + 1)))
        return AVERROR_INVALIDDATA;

    w = FFALIGN(width,  1 << (NB_LEVELS + 1));
    h = FFALIGN(height, 1 << (NB_LEVELS + 1));

    ctx->levels = bytestream2_get_be32(&ctx->gb);
    if (ctx->levels != NB_LEVELS)
        return AVERROR_INVALIDDATA;
    depth = bytestream2_get_be32(&ctx->gb);
    if (depth < 8 || depth > 15) {
        avpriv_request_sample(avctx, "Depth %d", depth);
        return AVERROR_INVALIDDATA;
    }

    build_luma_lut(avctx, depth);

    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;
    avctx->width  = width;
    avctx->height = height;

    if (ctx->w != w || ctx->h != h) {
        free_buffers(avctx);
        ctx->w = w;
        ctx->h = h;

        ret = init_decoder(avctx);
        if (ret < 0) {
            free_buffers(avctx);
            ctx->w = 0;
            ctx->h = 0;
            return ret;
        }
    }

    bytestream2_skip(&ctx->gb, 8);

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    p->color_range = AVCOL_RANGE_JPEG;

    ret = ff_thread_get_buffer(avctx, p, 0);
    if (ret < 0)
        return ret;

    for (i = 0; i < 3; i++) {
        ret = decode_plane(avctx, i, avpkt, p);
        if (ret < 0)
            return ret;
        if (avctx->flags & AV_CODEC_FLAG_GRAY)
            break;
    }

    postprocess_luma(avctx, p, ctx->w, ctx->h, ctx->depth);
    postprocess_chroma(p, ctx->w >> 1, ctx->h >> 1, ctx->depth);

    *got_frame = 1;

    return pktsize;
}

const FFCodec ff_pixlet_decoder = {
    .p.name           = "pixlet",
    .p.long_name      = NULL_IF_CONFIG_SMALL("Apple Pixlet"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_PIXLET,
    .init             = pixlet_init,
    .close            = pixlet_close,
    FF_CODEC_DECODE_CB(pixlet_decode_frame),
    .priv_data_size   = sizeof(PixletContext),
    .p.capabilities   = AV_CODEC_CAP_DR1 |
                        AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
