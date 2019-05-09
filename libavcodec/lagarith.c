/*
 * Lagarith lossless decoder
 * Copyright (c) 2009 Nathan Caldwell <saintdev (at) gmail.com>
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
 * Lagarith lossless decoder
 * @author Nathan Caldwell
 */

#include <inttypes.h>

#include "avcodec.h"
#include "get_bits.h"
#include "mathops.h"
#include "lagarithrac.h"
#include "lossless_videodsp.h"
#include "thread.h"

enum LagarithFrameType {
    FRAME_RAW           = 1,    /**< uncompressed */
    FRAME_U_RGB24       = 2,    /**< unaligned RGB24 */
    FRAME_ARITH_YUY2    = 3,    /**< arithmetic coded YUY2 */
    FRAME_ARITH_RGB24   = 4,    /**< arithmetic coded RGB24 */
    FRAME_SOLID_GRAY    = 5,    /**< solid grayscale color frame */
    FRAME_SOLID_COLOR   = 6,    /**< solid non-grayscale color frame */
    FRAME_OLD_ARITH_RGB = 7,    /**< obsolete arithmetic coded RGB (no longer encoded by upstream since version 1.1.0) */
    FRAME_ARITH_RGBA    = 8,    /**< arithmetic coded RGBA */
    FRAME_SOLID_RGBA    = 9,    /**< solid RGBA color frame */
    FRAME_ARITH_YV12    = 10,   /**< arithmetic coded YV12 */
    FRAME_REDUCED_RES   = 11,   /**< reduced resolution YV12 frame */
};

typedef struct LagarithContext {
    AVCodecContext *avctx;
    LLVidDSPContext llviddsp;
    int zeros;                  /**< number of consecutive zero bytes encountered */
    int zeros_rem;              /**< number of zero bytes remaining to output */
} LagarithContext;

/**
 * Compute the 52-bit mantissa of 1/(double)denom.
 * This crazy format uses floats in an entropy coder and we have to match x86
 * rounding exactly, thus ordinary floats aren't portable enough.
 * @param denom denominator
 * @return 52-bit mantissa
 * @see softfloat_mul
 */
static uint64_t softfloat_reciprocal(uint32_t denom)
{
    int shift = av_log2(denom - 1) + 1;
    uint64_t ret = (1ULL << 52) / denom;
    uint64_t err = (1ULL << 52) - ret * denom;
    ret <<= shift;
    err <<= shift;
    err +=  denom / 2;
    return ret + err / denom;
}

/**
 * (uint32_t)(x*f), where f has the given mantissa, and exponent 0
 * Used in combination with softfloat_reciprocal computes x/(double)denom.
 * @param x 32-bit integer factor
 * @param mantissa mantissa of f with exponent 0
 * @return 32-bit integer value (x*f)
 * @see softfloat_reciprocal
 */
static uint32_t softfloat_mul(uint32_t x, uint64_t mantissa)
{
    uint64_t l = x * (mantissa & 0xffffffff);
    uint64_t h = x * (mantissa >> 32);
    h += l >> 32;
    l &= 0xffffffff;
    l += 1LL << av_log2(h >> 21);
    h += l >> 32;
    return h >> 20;
}

static uint8_t lag_calc_zero_run(int8_t x)
{
    return (x * 2) ^ (x >> 7);
}

static int lag_decode_prob(GetBitContext *gb, uint32_t *value)
{
    static const uint8_t series[] = { 1, 2, 3, 5, 8, 13, 21 };
    int i;
    int bit     = 0;
    int bits    = 0;
    int prevbit = 0;
    unsigned val;

    for (i = 0; i < 7; i++) {
        if (prevbit && bit)
            break;
        prevbit = bit;
        bit = get_bits1(gb);
        if (bit && !prevbit)
            bits += series[i];
    }
    bits--;
    if (bits < 0 || bits > 31) {
        *value = 0;
        return -1;
    } else if (bits == 0) {
        *value = 0;
        return 0;
    }

    val  = get_bits_long(gb, bits);
    val |= 1U << bits;

    *value = val - 1;

    return 0;
}

static int lag_read_prob_header(lag_rac *rac, GetBitContext *gb)
{
    int i, j, scale_factor;
    unsigned prob, cumulative_target;
    unsigned cumul_prob = 0;
    unsigned scaled_cumul_prob = 0;
    int nnz = 0;

    rac->prob[0] = 0;
    rac->prob[257] = UINT_MAX;
    /* Read probabilities from bitstream */
    for (i = 1; i < 257; i++) {
        if (lag_decode_prob(gb, &rac->prob[i]) < 0) {
            av_log(rac->avctx, AV_LOG_ERROR, "Invalid probability encountered.\n");
            return -1;
        }
        if ((uint64_t)cumul_prob + rac->prob[i] > UINT_MAX) {
            av_log(rac->avctx, AV_LOG_ERROR, "Integer overflow encountered in cumulative probability calculation.\n");
            return -1;
        }
        cumul_prob += rac->prob[i];
        if (!rac->prob[i]) {
            if (lag_decode_prob(gb, &prob)) {
                av_log(rac->avctx, AV_LOG_ERROR, "Invalid probability run encountered.\n");
                return -1;
            }
            if (prob > 256 - i)
                prob = 256 - i;
            for (j = 0; j < prob; j++)
                rac->prob[++i] = 0;
        }else {
            nnz++;
        }
    }

    if (!cumul_prob) {
        av_log(rac->avctx, AV_LOG_ERROR, "All probabilities are 0!\n");
        return -1;
    }

    if (nnz == 1 && (show_bits_long(gb, 32) & 0xFFFFFF)) {
        return AVERROR_INVALIDDATA;
    }

    /* Scale probabilities so cumulative probability is an even power of 2. */
    scale_factor = av_log2(cumul_prob);

    if (cumul_prob & (cumul_prob - 1)) {
        uint64_t mul = softfloat_reciprocal(cumul_prob);
        for (i = 1; i <= 128; i++) {
            rac->prob[i] = softfloat_mul(rac->prob[i], mul);
            scaled_cumul_prob += rac->prob[i];
        }
        if (scaled_cumul_prob <= 0) {
            av_log(rac->avctx, AV_LOG_ERROR, "Scaled probabilities invalid\n");
            return AVERROR_INVALIDDATA;
        }
        for (; i < 257; i++) {
            rac->prob[i] = softfloat_mul(rac->prob[i], mul);
            scaled_cumul_prob += rac->prob[i];
        }

        scale_factor++;
        if (scale_factor >= 32U)
            return AVERROR_INVALIDDATA;
        cumulative_target = 1U << scale_factor;

        if (scaled_cumul_prob > cumulative_target) {
            av_log(rac->avctx, AV_LOG_ERROR,
                   "Scaled probabilities are larger than target!\n");
            return -1;
        }

        scaled_cumul_prob = cumulative_target - scaled_cumul_prob;

        for (i = 1; scaled_cumul_prob; i = (i & 0x7f) + 1) {
            if (rac->prob[i]) {
                rac->prob[i]++;
                scaled_cumul_prob--;
            }
            /* Comment from reference source:
             * if (b & 0x80 == 0) {     // order of operations is 'wrong'; it has been left this way
             *                          // since the compression change is negligible and fixing it
             *                          // breaks backwards compatibility
             *      b =- (signed int)b;
             *      b &= 0xFF;
             * } else {
             *      b++;
             *      b &= 0x7f;
             * }
             */
        }
    }

    rac->scale = scale_factor;

    /* Fill probability array with cumulative probability for each symbol. */
    for (i = 1; i < 257; i++)
        rac->prob[i] += rac->prob[i - 1];

    return 0;
}

static void add_lag_median_prediction(uint8_t *dst, uint8_t *src1,
                                      uint8_t *diff, int w, int *left,
                                      int *left_top)
{
    /* This is almost identical to add_hfyu_median_pred in huffyuvdsp.h.
     * However the &0xFF on the gradient predictor yields incorrect output
     * for lagarith.
     */
    int i;
    uint8_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        l = mid_pred(l, src1[i], l + src1[i] - lt) + diff[i];
        lt = src1[i];
        dst[i] = l;
    }

    *left     = l;
    *left_top = lt;
}

static void lag_pred_line(LagarithContext *l, uint8_t *buf,
                          int width, int stride, int line)
{
    int L, TL;

    if (!line) {
        /* Left prediction only for first line */
        L = l->llviddsp.add_left_pred(buf, buf, width, 0);
    } else {
        /* Left pixel is actually prev_row[width] */
        L = buf[width - stride - 1];

        if (line == 1) {
            /* Second line, left predict first pixel, the rest of the line is median predicted
             * NOTE: In the case of RGB this pixel is top predicted */
            TL = l->avctx->pix_fmt == AV_PIX_FMT_YUV420P ? buf[-stride] : L;
        } else {
            /* Top left is 2 rows back, last pixel */
            TL = buf[width - (2 * stride) - 1];
        }

        add_lag_median_prediction(buf, buf - stride, buf,
                                  width, &L, &TL);
    }
}

static void lag_pred_line_yuy2(LagarithContext *l, uint8_t *buf,
                               int width, int stride, int line,
                               int is_luma)
{
    int L, TL;

    if (!line) {
        L= buf[0];
        if (is_luma)
            buf[0] = 0;
        l->llviddsp.add_left_pred(buf, buf, width, 0);
        if (is_luma)
            buf[0] = L;
        return;
    }
    if (line == 1) {
        const int HEAD = is_luma ? 4 : 2;
        int i;

        L  = buf[width - stride - 1];
        TL = buf[HEAD  - stride - 1];
        for (i = 0; i < HEAD; i++) {
            L += buf[i];
            buf[i] = L;
        }
        for (; i < width; i++) {
            L      = mid_pred(L & 0xFF, buf[i - stride], (L + buf[i - stride] - TL) & 0xFF) + buf[i];
            TL     = buf[i - stride];
            buf[i] = L;
        }
    } else {
        TL = buf[width - (2 * stride) - 1];
        L  = buf[width - stride - 1];
        l->llviddsp.add_median_pred(buf, buf - stride, buf, width, &L, &TL);
    }
}

static int lag_decode_line(LagarithContext *l, lag_rac *rac,
                           uint8_t *dst, int width, int stride,
                           int esc_count)
{
    int i = 0;
    int ret = 0;

    if (!esc_count)
        esc_count = -1;

    /* Output any zeros remaining from the previous run */
handle_zeros:
    if (l->zeros_rem) {
        int count = FFMIN(l->zeros_rem, width - i);
        memset(dst + i, 0, count);
        i += count;
        l->zeros_rem -= count;
    }

    while (i < width) {
        dst[i] = lag_get_rac(rac);
        ret++;

        if (dst[i])
            l->zeros = 0;
        else
            l->zeros++;

        i++;
        if (l->zeros == esc_count) {
            int index = lag_get_rac(rac);
            ret++;

            l->zeros = 0;

            l->zeros_rem = lag_calc_zero_run(index);
            goto handle_zeros;
        }
    }
    return ret;
}

static int lag_decode_zero_run_line(LagarithContext *l, uint8_t *dst,
                                    const uint8_t *src, const uint8_t *src_end,
                                    int width, int esc_count)
{
    int i = 0;
    int count;
    uint8_t zero_run = 0;
    const uint8_t *src_start = src;
    uint8_t mask1 = -(esc_count < 2);
    uint8_t mask2 = -(esc_count < 3);
    uint8_t *end = dst + (width - 2);

    avpriv_request_sample(l->avctx, "zero_run_line");

    memset(dst, 0, width);

output_zeros:
    if (l->zeros_rem) {
        count = FFMIN(l->zeros_rem, width - i);
        if (end - dst < count) {
            av_log(l->avctx, AV_LOG_ERROR, "Too many zeros remaining.\n");
            return AVERROR_INVALIDDATA;
        }

        memset(dst, 0, count);
        l->zeros_rem -= count;
        dst += count;
    }

    while (dst < end) {
        i = 0;
        while (!zero_run && dst + i < end) {
            i++;
            if (i+2 >= src_end - src)
                return AVERROR_INVALIDDATA;
            zero_run =
                !(src[i] | (src[i + 1] & mask1) | (src[i + 2] & mask2));
        }
        if (zero_run) {
            zero_run = 0;
            i += esc_count;
            memcpy(dst, src, i);
            dst += i;
            l->zeros_rem = lag_calc_zero_run(src[i]);

            src += i + 1;
            goto output_zeros;
        } else {
            memcpy(dst, src, i);
            src += i;
            dst += i;
        }
    }
    return  src - src_start;
}



static int lag_decode_arith_plane(LagarithContext *l, uint8_t *dst,
                                  int width, int height, int stride,
                                  const uint8_t *src, int src_size)
{
    int i = 0;
    int read = 0;
    uint32_t length;
    uint32_t offset = 1;
    int esc_count;
    GetBitContext gb;
    lag_rac rac;
    const uint8_t *src_end = src + src_size;
    int ret;

    rac.avctx = l->avctx;
    l->zeros = 0;

    if(src_size < 2)
        return AVERROR_INVALIDDATA;

    esc_count = src[0];
    if (esc_count < 4) {
        length = width * height;
        if(src_size < 5)
            return AVERROR_INVALIDDATA;
        if (esc_count && AV_RL32(src + 1) < length) {
            length = AV_RL32(src + 1);
            offset += 4;
        }

        if ((ret = init_get_bits8(&gb, src + offset, src_size - offset)) < 0)
            return ret;

        if (lag_read_prob_header(&rac, &gb) < 0)
            return -1;

        ff_lag_rac_init(&rac, &gb, length - stride);
        for (i = 0; i < height; i++) {
            if (rac.overread > MAX_OVERREAD)
                return AVERROR_INVALIDDATA;
            read += lag_decode_line(l, &rac, dst + (i * stride), width,
                                    stride, esc_count);
        }

        if (read > length)
            av_log(l->avctx, AV_LOG_WARNING,
                   "Output more bytes than length (%d of %"PRIu32")\n", read,
                   length);
    } else if (esc_count < 8) {
        esc_count -= 4;
        src ++;
        src_size --;
        if (esc_count > 0) {
            /* Zero run coding only, no range coding. */
            for (i = 0; i < height; i++) {
                int res = lag_decode_zero_run_line(l, dst + (i * stride), src,
                                                   src_end, width, esc_count);
                if (res < 0)
                    return res;
                src += res;
            }
        } else {
            if (src_size < width * height)
                return AVERROR_INVALIDDATA; // buffer not big enough
            /* Plane is stored uncompressed */
            for (i = 0; i < height; i++) {
                memcpy(dst + (i * stride), src, width);
                src += width;
            }
        }
    } else if (esc_count == 0xff) {
        /* Plane is a solid run of given value */
        for (i = 0; i < height; i++)
            memset(dst + i * stride, src[1], width);
        /* Do not apply prediction.
           Note: memset to 0 above, setting first value to src[1]
           and applying prediction gives the same result. */
        return 0;
    } else {
        av_log(l->avctx, AV_LOG_ERROR,
               "Invalid zero run escape code! (%#x)\n", esc_count);
        return -1;
    }

    if (l->avctx->pix_fmt != AV_PIX_FMT_YUV422P) {
        for (i = 0; i < height; i++) {
            lag_pred_line(l, dst, width, stride, i);
            dst += stride;
        }
    } else {
        for (i = 0; i < height; i++) {
            lag_pred_line_yuy2(l, dst, width, stride, i,
                               width == l->avctx->width);
            dst += stride;
        }
    }

    return 0;
}

/**
 * Decode a frame.
 * @param avctx codec context
 * @param data output AVFrame
 * @param data_size size of output data or 0 if no picture is returned
 * @param avpkt input packet
 * @return number of consumed bytes on success or negative if decode fails
 */
static int lag_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    unsigned int buf_size = avpkt->size;
    LagarithContext *l = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *const p  = data;
    uint8_t frametype;
    uint32_t offset_gu = 0, offset_bv = 0, offset_ry = 9;
    uint32_t offs[4];
    uint8_t *srcs[4];
    int i, j, planes = 3;
    int ret;

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    frametype = buf[0];

    offset_gu = AV_RL32(buf + 1);
    offset_bv = AV_RL32(buf + 5);

    switch (frametype) {
    case FRAME_SOLID_RGBA:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
    case FRAME_SOLID_GRAY:
        if (frametype == FRAME_SOLID_GRAY)
            if (avctx->bits_per_coded_sample == 24) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP;
                planes = 4;
            }

        if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
            return ret;

        if (frametype == FRAME_SOLID_RGBA) {
            for (i = 0; i < avctx->height; i++) {
                memset(p->data[0] + i * p->linesize[0], buf[2], avctx->width);
                memset(p->data[1] + i * p->linesize[1], buf[1], avctx->width);
                memset(p->data[2] + i * p->linesize[2], buf[3], avctx->width);
                memset(p->data[3] + i * p->linesize[3], buf[4], avctx->width);
            }
        } else {
            for (i = 0; i < avctx->height; i++) {
                for (j = 0; j < planes; j++)
                    memset(p->data[j] + i * p->linesize[j], buf[1], avctx->width);
            }
        }
        break;
    case FRAME_SOLID_COLOR:
        if (avctx->bits_per_coded_sample == 24) {
            avctx->pix_fmt = AV_PIX_FMT_GBRP;
        } else {
            avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        }

        if ((ret = ff_thread_get_buffer(avctx, &frame,0)) < 0)
            return ret;

        for (i = 0; i < avctx->height; i++) {
            memset(p->data[0] + i * p->linesize[0], buf[2], avctx->width);
            memset(p->data[1] + i * p->linesize[1], buf[1], avctx->width);
            memset(p->data[2] + i * p->linesize[2], buf[3], avctx->width);
            if (avctx->pix_fmt == AV_PIX_FMT_GBRAP)
                memset(p->data[3] + i * p->linesize[3], 0xFFu, avctx->width);
        }
        break;
    case FRAME_ARITH_RGBA:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        planes = 4;
        offset_ry += 4;
        offs[3] = AV_RL32(buf + 9);
    case FRAME_ARITH_RGB24:
    case FRAME_U_RGB24:
        if (frametype == FRAME_ARITH_RGB24 || frametype == FRAME_U_RGB24)
            avctx->pix_fmt = AV_PIX_FMT_GBRP;

        if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
            return ret;

        offs[0] = offset_bv;
        offs[1] = offset_gu;
        offs[2] = offset_ry;

        for (i = 0; i < planes; i++)
            srcs[i] = p->data[i] + (avctx->height - 1) * p->linesize[i];
        for (i = 0; i < planes; i++)
            if (buf_size <= offs[i]) {
                av_log(avctx, AV_LOG_ERROR,
                        "Invalid frame offsets\n");
                return AVERROR_INVALIDDATA;
            }

        for (i = 0; i < planes; i++)
            lag_decode_arith_plane(l, srcs[i],
                                   avctx->width, avctx->height,
                                   -p->linesize[i], buf + offs[i],
                                   buf_size - offs[i]);
        for (i = 0; i < avctx->height; i++) {
            l->llviddsp.add_bytes(p->data[0] + i * p->linesize[0], p->data[1] + i * p->linesize[1], avctx->width);
            l->llviddsp.add_bytes(p->data[2] + i * p->linesize[2], p->data[1] + i * p->linesize[1], avctx->width);
        }
        FFSWAP(uint8_t*, p->data[0], p->data[1]);
        FFSWAP(int, p->linesize[0], p->linesize[1]);
        FFSWAP(uint8_t*, p->data[2], p->data[1]);
        FFSWAP(int, p->linesize[2], p->linesize[1]);
        break;
    case FRAME_ARITH_YUY2:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;

        if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
            return ret;

        if (offset_ry >= buf_size ||
            offset_gu >= buf_size ||
            offset_bv >= buf_size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid frame offsets\n");
            return AVERROR_INVALIDDATA;
        }

        lag_decode_arith_plane(l, p->data[0], avctx->width, avctx->height,
                               p->linesize[0], buf + offset_ry,
                               buf_size - offset_ry);
        lag_decode_arith_plane(l, p->data[1], (avctx->width + 1) / 2,
                               avctx->height, p->linesize[1],
                               buf + offset_gu, buf_size - offset_gu);
        lag_decode_arith_plane(l, p->data[2], (avctx->width + 1) / 2,
                               avctx->height, p->linesize[2],
                               buf + offset_bv, buf_size - offset_bv);
        break;
    case FRAME_ARITH_YV12:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;

        if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
            return ret;

        if (offset_ry >= buf_size ||
            offset_gu >= buf_size ||
            offset_bv >= buf_size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid frame offsets\n");
            return AVERROR_INVALIDDATA;
        }

        lag_decode_arith_plane(l, p->data[0], avctx->width, avctx->height,
                               p->linesize[0], buf + offset_ry,
                               buf_size - offset_ry);
        lag_decode_arith_plane(l, p->data[2], (avctx->width + 1) / 2,
                               (avctx->height + 1) / 2, p->linesize[2],
                               buf + offset_gu, buf_size - offset_gu);
        lag_decode_arith_plane(l, p->data[1], (avctx->width + 1) / 2,
                               (avctx->height + 1) / 2, p->linesize[1],
                               buf + offset_bv, buf_size - offset_bv);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported Lagarith frame type: %#"PRIx8"\n", frametype);
        return AVERROR_PATCHWELCOME;
    }

    *got_frame = 1;

    return buf_size;
}

static av_cold int lag_decode_init(AVCodecContext *avctx)
{
    LagarithContext *l = avctx->priv_data;
    l->avctx = avctx;

    ff_llviddsp_init(&l->llviddsp);

    return 0;
}

#if HAVE_THREADS
static av_cold int lag_decode_init_thread_copy(AVCodecContext *avctx)
{
    LagarithContext *l = avctx->priv_data;
    l->avctx = avctx;

    return 0;
}
#endif

AVCodec ff_lagarith_decoder = {
    .name           = "lagarith",
    .long_name      = NULL_IF_CONFIG_SMALL("Lagarith lossless"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LAGARITH,
    .priv_data_size = sizeof(LagarithContext),
    .init           = lag_decode_init,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(lag_decode_init_thread_copy),
    .decode         = lag_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
