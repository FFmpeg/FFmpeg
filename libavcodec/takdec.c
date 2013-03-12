/*
 * TAK decoder
 * Copyright (c) 2012 Paul B Mahol
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
 * TAK (Tom's lossless Audio Kompressor) decoder
 * @author Paul B Mahol
 */

#include "libavutil/internal.h"
#include "libavutil/samplefmt.h"
#include "tak.h"
#include "avcodec.h"
#include "dsputil.h"
#include "internal.h"
#include "unary.h"

#define MAX_SUBFRAMES     8                         ///< max number of subframes per channel
#define MAX_PREDICTORS  256

typedef struct MCDParam {
    int8_t present;                                 ///< decorrelation parameter availability for this channel
    int8_t index;                                   ///< index into array of decorrelation types
    int8_t chan1;
    int8_t chan2;
} MCDParam;

typedef struct TAKDecContext {
    AVCodecContext *avctx;                          ///< parent AVCodecContext
    DSPContext      dsp;
    TAKStreamInfo   ti;
    GetBitContext   gb;                             ///< bitstream reader initialized to start at the current frame

    int             uval;
    int             nb_samples;                     ///< number of samples in the current frame
    uint8_t        *decode_buffer;
    unsigned int    decode_buffer_size;
    int32_t        *decoded[TAK_MAX_CHANNELS];      ///< decoded samples for each channel

    int8_t          lpc_mode[TAK_MAX_CHANNELS];
    int8_t          sample_shift[TAK_MAX_CHANNELS]; ///< shift applied to every sample in the channel
    int16_t         predictors[MAX_PREDICTORS];
    int             nb_subframes;                   ///< number of subframes in the current frame
    int16_t         subframe_len[MAX_SUBFRAMES];    ///< subframe length in samples
    int             subframe_scale;

    int8_t          dmode;                          ///< channel decorrelation type in the current frame

    MCDParam        mcdparams[TAK_MAX_CHANNELS];    ///< multichannel decorrelation parameters

    int8_t          coding_mode[128];
    DECLARE_ALIGNED(16, int16_t, filter)[MAX_PREDICTORS];
    DECLARE_ALIGNED(16, int16_t, residues)[544];
} TAKDecContext;

static const int8_t mc_dmodes[] = { 1, 3, 4, 6, };

static const uint16_t predictor_sizes[] = {
    4, 8, 12, 16, 24, 32, 48, 64, 80, 96, 128, 160, 192, 224, 256, 0,
};

static const struct CParam {
    int init;
    int escape;
    int scale;
    int aescape;
    int bias;
} xcodes[50] = {
    { 0x01, 0x0000001, 0x0000001, 0x0000003, 0x0000008 },
    { 0x02, 0x0000003, 0x0000001, 0x0000007, 0x0000006 },
    { 0x03, 0x0000005, 0x0000002, 0x000000E, 0x000000D },
    { 0x03, 0x0000003, 0x0000003, 0x000000D, 0x0000018 },
    { 0x04, 0x000000B, 0x0000004, 0x000001C, 0x0000019 },
    { 0x04, 0x0000006, 0x0000006, 0x000001A, 0x0000030 },
    { 0x05, 0x0000016, 0x0000008, 0x0000038, 0x0000032 },
    { 0x05, 0x000000C, 0x000000C, 0x0000034, 0x0000060 },
    { 0x06, 0x000002C, 0x0000010, 0x0000070, 0x0000064 },
    { 0x06, 0x0000018, 0x0000018, 0x0000068, 0x00000C0 },
    { 0x07, 0x0000058, 0x0000020, 0x00000E0, 0x00000C8 },
    { 0x07, 0x0000030, 0x0000030, 0x00000D0, 0x0000180 },
    { 0x08, 0x00000B0, 0x0000040, 0x00001C0, 0x0000190 },
    { 0x08, 0x0000060, 0x0000060, 0x00001A0, 0x0000300 },
    { 0x09, 0x0000160, 0x0000080, 0x0000380, 0x0000320 },
    { 0x09, 0x00000C0, 0x00000C0, 0x0000340, 0x0000600 },
    { 0x0A, 0x00002C0, 0x0000100, 0x0000700, 0x0000640 },
    { 0x0A, 0x0000180, 0x0000180, 0x0000680, 0x0000C00 },
    { 0x0B, 0x0000580, 0x0000200, 0x0000E00, 0x0000C80 },
    { 0x0B, 0x0000300, 0x0000300, 0x0000D00, 0x0001800 },
    { 0x0C, 0x0000B00, 0x0000400, 0x0001C00, 0x0001900 },
    { 0x0C, 0x0000600, 0x0000600, 0x0001A00, 0x0003000 },
    { 0x0D, 0x0001600, 0x0000800, 0x0003800, 0x0003200 },
    { 0x0D, 0x0000C00, 0x0000C00, 0x0003400, 0x0006000 },
    { 0x0E, 0x0002C00, 0x0001000, 0x0007000, 0x0006400 },
    { 0x0E, 0x0001800, 0x0001800, 0x0006800, 0x000C000 },
    { 0x0F, 0x0005800, 0x0002000, 0x000E000, 0x000C800 },
    { 0x0F, 0x0003000, 0x0003000, 0x000D000, 0x0018000 },
    { 0x10, 0x000B000, 0x0004000, 0x001C000, 0x0019000 },
    { 0x10, 0x0006000, 0x0006000, 0x001A000, 0x0030000 },
    { 0x11, 0x0016000, 0x0008000, 0x0038000, 0x0032000 },
    { 0x11, 0x000C000, 0x000C000, 0x0034000, 0x0060000 },
    { 0x12, 0x002C000, 0x0010000, 0x0070000, 0x0064000 },
    { 0x12, 0x0018000, 0x0018000, 0x0068000, 0x00C0000 },
    { 0x13, 0x0058000, 0x0020000, 0x00E0000, 0x00C8000 },
    { 0x13, 0x0030000, 0x0030000, 0x00D0000, 0x0180000 },
    { 0x14, 0x00B0000, 0x0040000, 0x01C0000, 0x0190000 },
    { 0x14, 0x0060000, 0x0060000, 0x01A0000, 0x0300000 },
    { 0x15, 0x0160000, 0x0080000, 0x0380000, 0x0320000 },
    { 0x15, 0x00C0000, 0x00C0000, 0x0340000, 0x0600000 },
    { 0x16, 0x02C0000, 0x0100000, 0x0700000, 0x0640000 },
    { 0x16, 0x0180000, 0x0180000, 0x0680000, 0x0C00000 },
    { 0x17, 0x0580000, 0x0200000, 0x0E00000, 0x0C80000 },
    { 0x17, 0x0300000, 0x0300000, 0x0D00000, 0x1800000 },
    { 0x18, 0x0B00000, 0x0400000, 0x1C00000, 0x1900000 },
    { 0x18, 0x0600000, 0x0600000, 0x1A00000, 0x3000000 },
    { 0x19, 0x1600000, 0x0800000, 0x3800000, 0x3200000 },
    { 0x19, 0x0C00000, 0x0C00000, 0x3400000, 0x6000000 },
    { 0x1A, 0x2C00000, 0x1000000, 0x7000000, 0x6400000 },
    { 0x1A, 0x1800000, 0x1800000, 0x6800000, 0xC000000 },
};

static int set_bps_params(AVCodecContext *avctx)
{
    switch (avctx->bits_per_raw_sample) {
    case 8:
        avctx->sample_fmt = AV_SAMPLE_FMT_U8P;
        break;
    case 16:
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
        break;
    case 24:
        avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "invalid/unsupported bits per sample: %d\n",
               avctx->bits_per_raw_sample);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static void set_sample_rate_params(AVCodecContext *avctx)
{
    TAKDecContext *s  = avctx->priv_data;
    int shift         = 3 - (avctx->sample_rate / 11025);
    shift             = FFMAX(0, shift);
    s->uval           = FFALIGN(avctx->sample_rate + 511 >> 9, 4) << shift;
    s->subframe_scale = FFALIGN(avctx->sample_rate + 511 >> 9, 4) << 1;
}

static av_cold int tak_decode_init(AVCodecContext *avctx)
{
    TAKDecContext *s = avctx->priv_data;

    ff_tak_init_crc();
    ff_dsputil_init(&s->dsp, avctx);

    s->avctx = avctx;
    avctx->bits_per_raw_sample = avctx->bits_per_coded_sample;

    set_sample_rate_params(avctx);

    return set_bps_params(avctx);
}

static void decode_lpc(int32_t *coeffs, int mode, int length)
{
    int i;

    if (length < 2)
        return;

    if (mode == 1) {
        int a1 = *coeffs++;
        for (i = 0; i < length - 1 >> 1; i++) {
            *coeffs   += a1;
            coeffs[1] += *coeffs;
            a1         = coeffs[1];
            coeffs    += 2;
        }
        if (length - 1 & 1)
            *coeffs += a1;
    } else if (mode == 2) {
        int a1    = coeffs[1];
        int a2    = a1 + *coeffs;
        coeffs[1] = a2;
        if (length > 2) {
            coeffs += 2;
            for (i = 0; i < length - 2 >> 1; i++) {
                int a3    = *coeffs + a1;
                int a4    = a3 + a2;
                *coeffs   = a4;
                a1        = coeffs[1] + a3;
                a2        = a1 + a4;
                coeffs[1] = a2;
                coeffs   += 2;
            }
            if (length & 1)
                *coeffs += a1 + a2;
        }
    } else if (mode == 3) {
        int a1    = coeffs[1];
        int a2    = a1 + *coeffs;
        coeffs[1] = a2;
        if (length > 2) {
            int a3  = coeffs[2];
            int a4  = a3 + a1;
            int a5  = a4 + a2;
            coeffs += 3;
            for (i = 0; i < length - 3; i++) {
                a3     += *coeffs;
                a4     += a3;
                a5     += a4;
                *coeffs = a5;
                coeffs++;
            }
        }
    }
}

static int decode_segment(TAKDecContext *s, int8_t mode, int32_t *decoded, int len)
{
    struct CParam code;
    GetBitContext *gb = &s->gb;
    int i;

    if (!mode) {
        memset(decoded, 0, len * sizeof(*decoded));
        return 0;
    }

    if (mode > FF_ARRAY_ELEMS(xcodes))
        return AVERROR_INVALIDDATA;
    code = xcodes[mode - 1];

    for (i = 0; i < len; i++) {
        int x = get_bits_long(gb, code.init);
        if (x >= code.escape && get_bits1(gb)) {
            x |= 1 << code.init;
            if (x >= code.aescape) {
                int scale = get_unary(gb, 1, 9);
                if (scale == 9) {
                    int scale_bits = get_bits(gb, 3);
                    if (scale_bits > 0) {
                        if (scale_bits == 7) {
                            scale_bits += get_bits(gb, 5);
                            if (scale_bits > 29)
                                return AVERROR_INVALIDDATA;
                        }
                        scale = get_bits_long(gb, scale_bits) + 1;
                        x    += code.scale * scale;
                    }
                    x += code.bias;
                } else
                    x += code.scale * scale - code.escape;
            } else
                x -= code.escape;
        }
        decoded[i] = (x >> 1) ^ -(x & 1);
    }

    return 0;
}

static int decode_residues(TAKDecContext *s, int32_t *decoded, int length)
{
    GetBitContext *gb = &s->gb;
    int i, mode, ret;

    if (length > s->nb_samples)
        return AVERROR_INVALIDDATA;

    if (get_bits1(gb)) {
        int wlength, rval;

        wlength = length / s->uval;

        rval = length - (wlength * s->uval);

        if (rval < s->uval / 2)
            rval += s->uval;
        else
            wlength++;

        if (wlength <= 1 || wlength > 128)
            return AVERROR_INVALIDDATA;

        s->coding_mode[0] = mode = get_bits(gb, 6);

        for (i = 1; i < wlength; i++) {
            int c = get_unary(gb, 1, 6);

            switch (c) {
            case 6:
                mode = get_bits(gb, 6);
                break;
            case 5:
            case 4:
            case 3: {
                /* mode += sign ? (1 - c) : (c - 1) */
                int sign = get_bits1(gb);
                mode    += (-sign ^ (c - 1)) + sign;
                break;
            }
            case 2:
                mode++;
                break;
            case 1:
                mode--;
                break;
            }
            s->coding_mode[i] = mode;
        }

        i = 0;
        while (i < wlength) {
            int len = 0;

            mode = s->coding_mode[i];
            do {
                if (i >= wlength - 1)
                    len += rval;
                else
                    len += s->uval;
                i++;

                if (i == wlength)
                    break;
            } while (s->coding_mode[i] == mode);

            if ((ret = decode_segment(s, mode, decoded, len)) < 0)
                return ret;
            decoded += len;
        }
    } else {
        mode = get_bits(gb, 6);
        if ((ret = decode_segment(s, mode, decoded, length)) < 0)
            return ret;
    }

    return 0;
}

static int get_bits_esc4(GetBitContext *gb)
{
    if (get_bits1(gb))
        return get_bits(gb, 4) + 1;
    else
        return 0;
}

static int decode_subframe(TAKDecContext *s, int32_t *decoded,
                           int subframe_size, int prev_subframe_size)
{
    GetBitContext *gb = &s->gb;
    int tmp, x, y, i, j, ret = 0;
    int dshift, size, filter_quant, filter_order;
    int tfilter[MAX_PREDICTORS];

    if (!get_bits1(gb))
        return decode_residues(s, decoded, subframe_size);

    filter_order = predictor_sizes[get_bits(gb, 4)];

    if (prev_subframe_size > 0 && get_bits1(gb)) {
        if (filter_order > prev_subframe_size)
            return AVERROR_INVALIDDATA;

        decoded       -= filter_order;
        subframe_size += filter_order;

        if (filter_order > subframe_size)
            return AVERROR_INVALIDDATA;
    } else {
        int lpc_mode;

        if (filter_order > subframe_size)
            return AVERROR_INVALIDDATA;

        lpc_mode = get_bits(gb, 2);
        if (lpc_mode > 2)
            return AVERROR_INVALIDDATA;

        if ((ret = decode_residues(s, decoded, filter_order)) < 0)
            return ret;

        if (lpc_mode)
            decode_lpc(decoded, lpc_mode, filter_order);
    }

    dshift = get_bits_esc4(gb);
    size   = get_bits1(gb) + 6;

    filter_quant = 10;
    if (get_bits1(gb)) {
        filter_quant -= get_bits(gb, 3) + 1;
        if (filter_quant < 3)
            return AVERROR_INVALIDDATA;
    }

    s->predictors[0] = get_sbits(gb, 10);
    s->predictors[1] = get_sbits(gb, 10);
    s->predictors[2] = get_sbits(gb, size) << (10 - size);
    s->predictors[3] = get_sbits(gb, size) << (10 - size);
    if (filter_order > 4) {
        tmp = size - get_bits1(gb);

        for (i = 4; i < filter_order; i++) {
            if (!(i & 3))
                x = tmp - get_bits(gb, 2);
            s->predictors[i] = get_sbits(gb, x) << (10 - size);
        }
    }

    tfilter[0] = s->predictors[0] << 6;
    for (i = 1; i < filter_order; i++) {
        int32_t *p1 = &tfilter[0];
        int32_t *p2 = &tfilter[i - 1];

        for (j = 0; j < (i + 1) / 2; j++) {
            x     = *p1 + (s->predictors[i] * *p2 + 256 >> 9);
            *p2  += s->predictors[i] * *p1 + 256 >> 9;
            *p1++ = x;
            p2--;
        }

        tfilter[i] = s->predictors[i] << 6;
    }

    x = 1 << (32 - (15 - filter_quant));
    y = 1 << ((15 - filter_quant) - 1);
    for (i = 0, j = filter_order - 1; i < filter_order / 2; i++, j--) {
        tmp = y + tfilter[j];
        s->filter[j] = x - ((tfilter[i] + y) >> (15 - filter_quant));
        s->filter[i] = x - ((tfilter[j] + y) >> (15 - filter_quant));
    }

    if ((ret = decode_residues(s, &decoded[filter_order],
                               subframe_size - filter_order)) < 0)
        return ret;

    for (i = 0; i < filter_order; i++)
        s->residues[i] = *decoded++ >> dshift;

    y    = FF_ARRAY_ELEMS(s->residues) - filter_order;
    x    = subframe_size - filter_order;
    while (x > 0) {
        tmp = FFMIN(y, x);

        for (i = 0; i < tmp; i++) {
            int v = 1 << (filter_quant - 1);

            if (!(filter_order & 15)) {
                v += s->dsp.scalarproduct_int16(&s->residues[i], s->filter,
                                                filter_order);
            } else if (filter_order & 4) {
                for (j = 0; j < filter_order; j += 4) {
                    v += s->residues[i + j + 3] * s->filter[j + 3] +
                         s->residues[i + j + 2] * s->filter[j + 2] +
                         s->residues[i + j + 1] * s->filter[j + 1] +
                         s->residues[i + j    ] * s->filter[j    ];
                }
            } else {
                for (j = 0; j < filter_order; j += 8) {
                    v += s->residues[i + j + 7] * s->filter[j + 7] +
                         s->residues[i + j + 6] * s->filter[j + 6] +
                         s->residues[i + j + 5] * s->filter[j + 5] +
                         s->residues[i + j + 4] * s->filter[j + 4] +
                         s->residues[i + j + 3] * s->filter[j + 3] +
                         s->residues[i + j + 2] * s->filter[j + 2] +
                         s->residues[i + j + 1] * s->filter[j + 1] +
                         s->residues[i + j    ] * s->filter[j    ];
                }
            }
            v = (av_clip(v >> filter_quant, -8192, 8191) << dshift) - *decoded;
            *decoded++ = v;
            s->residues[filter_order + i] = v >> dshift;
        }

        x -= tmp;
        if (x > 0)
            memcpy(s->residues, &s->residues[y], 2 * filter_order);
    }

    emms_c();

    return 0;
}

static int decode_channel(TAKDecContext *s, int chan)
{
    AVCodecContext *avctx = s->avctx;
    GetBitContext *gb     = &s->gb;
    int32_t *decoded      = s->decoded[chan];
    int left              = s->nb_samples - 1;
    int i = 0, ret, prev = 0;

    s->sample_shift[chan] = get_bits_esc4(gb);
    if (s->sample_shift[chan] >= avctx->bits_per_raw_sample)
        return AVERROR_INVALIDDATA;

    *decoded++ = get_sbits(gb, avctx->bits_per_raw_sample - s->sample_shift[chan]);
    s->lpc_mode[chan] = get_bits(gb, 2);
    s->nb_subframes   = get_bits(gb, 3) + 1;

    if (s->nb_subframes > 1) {
        if (get_bits_left(gb) < (s->nb_subframes - 1) * 6)
            return AVERROR_INVALIDDATA;

        for (; i < s->nb_subframes - 1; i++) {
            int v = get_bits(gb, 6);

            s->subframe_len[i] = (v - prev) * s->subframe_scale;
            if (s->subframe_len[i] <= 0)
                return AVERROR_INVALIDDATA;

            left -= s->subframe_len[i];
            prev  = v;
        }

        if (left <= 0)
            return AVERROR_INVALIDDATA;
    }
    s->subframe_len[i] = left;

    prev = 0;
    for (i = 0; i < s->nb_subframes; i++) {
        if ((ret = decode_subframe(s, decoded, s->subframe_len[i], prev)) < 0)
            return ret;
        decoded += s->subframe_len[i];
        prev     = s->subframe_len[i];
    }

    return 0;
}

static int decorrelate(TAKDecContext *s, int c1, int c2, int length)
{
    GetBitContext *gb = &s->gb;
    int32_t *p1       = s->decoded[c1] + 1;
    int32_t *p2       = s->decoded[c2] + 1;
    int i;
    int dshift, dfactor;

    switch (s->dmode) {
    case 1: /* left/side */
        for (i = 0; i < length; i++) {
            int32_t a = p1[i];
            int32_t b = p2[i];
            p2[i]     = a + b;
        }
        break;
    case 2: /* side/right */
        for (i = 0; i < length; i++) {
            int32_t a = p1[i];
            int32_t b = p2[i];
            p1[i]     = b - a;
        }
        break;
    case 3: /* side/mid */
        for (i = 0; i < length; i++) {
            int32_t a = p1[i];
            int32_t b = p2[i];
            a        -= b >> 1;
            p1[i]     = a;
            p2[i]     = a + b;
        }
        break;
    case 4: /* side/left with scale factor */
        FFSWAP(int32_t*, p1, p2);
    case 5: /* side/right with scale factor */
        dshift  = get_bits_esc4(gb);
        dfactor = get_sbits(gb, 10);
        for (i = 0; i < length; i++) {
            int32_t a = p1[i];
            int32_t b = p2[i];
            b         = dfactor * (b >> dshift) + 128 >> 8 << dshift;
            p1[i]     = b - a;
        }
        break;
    case 6:
        FFSWAP(int32_t*, p1, p2);
    case 7: {
        int length2, order_half, filter_order, dval1, dval2;
        int tmp, x, code_size;

        if (length < 256)
            return AVERROR_INVALIDDATA;

        dshift       = get_bits_esc4(gb);
        filter_order = 8 << get_bits1(gb);
        dval1        = get_bits1(gb);
        dval2        = get_bits1(gb);

        for (i = 0; i < filter_order; i++) {
            if (!(i & 3))
                code_size = 14 - get_bits(gb, 3);
            s->filter[i] = get_sbits(gb, code_size);
        }

        order_half = filter_order / 2;
        length2    = length - (filter_order - 1);

        /* decorrelate beginning samples */
        if (dval1) {
            for (i = 0; i < order_half; i++) {
                int32_t a = p1[i];
                int32_t b = p2[i];
                p1[i]     = a + b;
            }
        }

        /* decorrelate ending samples */
        if (dval2) {
            for (i = length2 + order_half; i < length; i++) {
                int32_t a = p1[i];
                int32_t b = p2[i];
                p1[i]     = a + b;
            }
        }


        for (i = 0; i < filter_order; i++)
            s->residues[i] = *p2++ >> dshift;

        p1 += order_half;
        x = FF_ARRAY_ELEMS(s->residues) - filter_order;
        for (; length2 > 0; length2 -= tmp) {
            tmp = FFMIN(length2, x);

            for (i = 0; i < tmp; i++)
                s->residues[filter_order + i] = *p2++ >> dshift;

            for (i = 0; i < tmp; i++) {
                int v = 1 << 9;

                if (filter_order == 16) {
                    v += s->dsp.scalarproduct_int16(&s->residues[i], s->filter,
                                                    filter_order);
                } else {
                    v += s->residues[i + 7] * s->filter[7] +
                         s->residues[i + 6] * s->filter[6] +
                         s->residues[i + 5] * s->filter[5] +
                         s->residues[i + 4] * s->filter[4] +
                         s->residues[i + 3] * s->filter[3] +
                         s->residues[i + 2] * s->filter[2] +
                         s->residues[i + 1] * s->filter[1] +
                         s->residues[i    ] * s->filter[0];
                }

                v = (av_clip(v >> 10, -8192, 8191) << dshift) - *p1;
                *p1++ = v;
            }

            memcpy(s->residues, &s->residues[tmp], 2 * filter_order);
        }

        emms_c();
        break;
    }
    }

    return 0;
}

static int tak_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *pkt)
{
    TAKDecContext *s  = avctx->priv_data;
    AVFrame *frame    = data;
    GetBitContext *gb = &s->gb;
    int chan, i, ret, hsize;

    if (pkt->size < TAK_MIN_FRAME_HEADER_BYTES)
        return AVERROR_INVALIDDATA;

    if ((ret = init_get_bits8(gb, pkt->data, pkt->size)) < 0)
        return ret;

    if ((ret = ff_tak_decode_frame_header(avctx, gb, &s->ti, 0)) < 0)
        return ret;

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        hsize = get_bits_count(gb) / 8;
        if (ff_tak_check_crc(pkt->data, hsize)) {
            av_log(avctx, AV_LOG_ERROR, "CRC error\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->ti.codec != TAK_CODEC_MONO_STEREO &&
        s->ti.codec != TAK_CODEC_MULTICHANNEL) {
        av_log(avctx, AV_LOG_ERROR, "unsupported codec: %d\n", s->ti.codec);
        return AVERROR_PATCHWELCOME;
    }
    if (s->ti.data_type) {
        av_log(avctx, AV_LOG_ERROR,
               "unsupported data type: %d\n", s->ti.data_type);
        return AVERROR_INVALIDDATA;
    }
    if (s->ti.codec == TAK_CODEC_MONO_STEREO && s->ti.channels > 2) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid number of channels: %d\n", s->ti.channels);
        return AVERROR_INVALIDDATA;
    }
    if (s->ti.channels > 6) {
        av_log(avctx, AV_LOG_ERROR,
               "unsupported number of channels: %d\n", s->ti.channels);
        return AVERROR_INVALIDDATA;
    }

    if (s->ti.frame_samples <= 0) {
        av_log(avctx, AV_LOG_ERROR, "unsupported/invalid number of samples\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->ti.bps != avctx->bits_per_raw_sample) {
        avctx->bits_per_raw_sample = s->ti.bps;
        if ((ret = set_bps_params(avctx)) < 0)
            return ret;
    }
    if (s->ti.sample_rate != avctx->sample_rate) {
        avctx->sample_rate = s->ti.sample_rate;
        set_sample_rate_params(avctx);
    }
    if (s->ti.ch_layout)
        avctx->channel_layout = s->ti.ch_layout;
    avctx->channels = s->ti.channels;

    s->nb_samples = s->ti.last_frame_samples ? s->ti.last_frame_samples
                                             : s->ti.frame_samples;

    frame->nb_samples = s->nb_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (avctx->bits_per_raw_sample <= 16) {
        int buf_size = av_samples_get_buffer_size(NULL, avctx->channels,
                                                  s->nb_samples,
                                                  AV_SAMPLE_FMT_S32P, 0);
        av_fast_malloc(&s->decode_buffer, &s->decode_buffer_size, buf_size);
        if (!s->decode_buffer)
            return AVERROR(ENOMEM);
        ret = av_samples_fill_arrays((uint8_t **)s->decoded, NULL,
                                     s->decode_buffer, avctx->channels,
                                     s->nb_samples, AV_SAMPLE_FMT_S32P, 0);
        if (ret < 0)
            return ret;
    } else {
        for (chan = 0; chan < avctx->channels; chan++)
            s->decoded[chan] = (int32_t *)frame->extended_data[chan];
    }

    if (s->nb_samples < 16) {
        for (chan = 0; chan < avctx->channels; chan++) {
            int32_t *decoded = s->decoded[chan];
            for (i = 0; i < s->nb_samples; i++)
                decoded[i] = get_sbits(gb, avctx->bits_per_raw_sample);
        }
    } else {
        if (s->ti.codec == TAK_CODEC_MONO_STEREO) {
            for (chan = 0; chan < avctx->channels; chan++)
                if (ret = decode_channel(s, chan))
                    return ret;

            if (avctx->channels == 2) {
                s->nb_subframes = get_bits(gb, 1) + 1;
                if (s->nb_subframes > 1) {
                    s->subframe_len[1] = get_bits(gb, 6);
                }

                s->dmode = get_bits(gb, 3);
                if (ret = decorrelate(s, 0, 1, s->nb_samples - 1))
                    return ret;
            }
        } else if (s->ti.codec == TAK_CODEC_MULTICHANNEL) {
            if (get_bits1(gb)) {
                int ch_mask = 0;

                chan = get_bits(gb, 4) + 1;
                if (chan > avctx->channels)
                    return AVERROR_INVALIDDATA;

                for (i = 0; i < chan; i++) {
                    int nbit = get_bits(gb, 4);

                    if (nbit >= avctx->channels)
                        return AVERROR_INVALIDDATA;

                    if (ch_mask & 1 << nbit)
                        return AVERROR_INVALIDDATA;

                    s->mcdparams[i].present = get_bits1(gb);
                    if (s->mcdparams[i].present) {
                        s->mcdparams[i].index = get_bits(gb, 2);
                        s->mcdparams[i].chan2 = get_bits(gb, 4);
                        if (s->mcdparams[i].index == 1) {
                            if ((nbit == s->mcdparams[i].chan2) ||
                                (ch_mask & 1 << s->mcdparams[i].chan2))
                                return AVERROR_INVALIDDATA;

                            ch_mask |= 1 << s->mcdparams[i].chan2;
                        } else if (!(ch_mask & 1 << s->mcdparams[i].chan2)) {
                            return AVERROR_INVALIDDATA;
                        }
                    }
                    s->mcdparams[i].chan1 = nbit;

                    ch_mask |= 1 << nbit;
                }
            } else {
                chan = avctx->channels;
                for (i = 0; i < chan; i++) {
                    s->mcdparams[i].present = 0;
                    s->mcdparams[i].chan1   = i;
                }
            }

            for (i = 0; i < chan; i++) {
                if (s->mcdparams[i].present && s->mcdparams[i].index == 1)
                    if (ret = decode_channel(s, s->mcdparams[i].chan2))
                        return ret;

                if (ret = decode_channel(s, s->mcdparams[i].chan1))
                    return ret;

                if (s->mcdparams[i].present) {
                    s->dmode = mc_dmodes[s->mcdparams[i].index];
                    if (ret = decorrelate(s,
                                          s->mcdparams[i].chan2,
                                          s->mcdparams[i].chan1,
                                          s->nb_samples - 1))
                        return ret;
                }
            }
        }

        for (chan = 0; chan < avctx->channels; chan++) {
            int32_t *decoded = s->decoded[chan];

            if (s->lpc_mode[chan])
                decode_lpc(decoded, s->lpc_mode[chan], s->nb_samples);

            if (s->sample_shift[chan] > 0)
                for (i = 0; i < s->nb_samples; i++)
                    decoded[i] <<= s->sample_shift[chan];
        }
    }

    align_get_bits(gb);
    skip_bits(gb, 24);
    if (get_bits_left(gb) < 0)
        av_log(avctx, AV_LOG_DEBUG, "overread\n");
    else if (get_bits_left(gb) > 0)
        av_log(avctx, AV_LOG_DEBUG, "underread\n");

    if (avctx->err_recognition & AV_EF_CRCCHECK) {
        if (ff_tak_check_crc(pkt->data + hsize,
                             get_bits_count(gb) / 8 - hsize)) {
            av_log(avctx, AV_LOG_ERROR, "CRC error\n");
            return AVERROR_INVALIDDATA;
        }
    }

    /* convert to output buffer */
    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8P:
        for (chan = 0; chan < avctx->channels; chan++) {
            uint8_t *samples = (uint8_t *)frame->extended_data[chan];
            int32_t *decoded = s->decoded[chan];
            for (i = 0; i < s->nb_samples; i++)
                samples[i] = decoded[i] + 0x80;
        }
        break;
    case AV_SAMPLE_FMT_S16P:
        for (chan = 0; chan < avctx->channels; chan++) {
            int16_t *samples = (int16_t *)frame->extended_data[chan];
            int32_t *decoded = s->decoded[chan];
            for (i = 0; i < s->nb_samples; i++)
                samples[i] = decoded[i];
        }
        break;
    case AV_SAMPLE_FMT_S32P:
        for (chan = 0; chan < avctx->channels; chan++) {
            int32_t *samples = (int32_t *)frame->extended_data[chan];
            for (i = 0; i < s->nb_samples; i++)
                samples[i] <<= 8;
        }
        break;
    }

    *got_frame_ptr = 1;

    return pkt->size;
}

static av_cold int tak_decode_close(AVCodecContext *avctx)
{
    TAKDecContext *s = avctx->priv_data;

    av_freep(&s->decode_buffer);

    return 0;
}

AVCodec ff_tak_decoder = {
    .name             = "tak",
    .type             = AVMEDIA_TYPE_AUDIO,
    .id               = AV_CODEC_ID_TAK,
    .priv_data_size   = sizeof(TAKDecContext),
    .init             = tak_decode_init,
    .close            = tak_decode_close,
    .decode           = tak_decode_frame,
    .capabilities     = CODEC_CAP_DR1,
    .long_name        = NULL_IF_CONFIG_SMALL("TAK (Tom's lossless Audio Kompressor)"),
    .sample_fmts      = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                        AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_S32P,
                                                        AV_SAMPLE_FMT_NONE },
};
