/*
 * On2 Audio for Video Codec decoder
 *
 * Copyright (c) 2013 Konstantin Shishkov
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

#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/float_dsp.h"
#include "avcodec.h"
#include "bytestream.h"
#include "fft.h"
#include "get_bits.h"
#include "internal.h"

#include "on2avcdata.h"

#define ON2AVC_SUBFRAME_SIZE   1024

enum WindowTypes {
    WINDOW_TYPE_LONG       = 0,
    WINDOW_TYPE_LONG_STOP,
    WINDOW_TYPE_LONG_START,
    WINDOW_TYPE_8SHORT     = 3,
    WINDOW_TYPE_EXT4,
    WINDOW_TYPE_EXT5,
    WINDOW_TYPE_EXT6,
    WINDOW_TYPE_EXT7,
};

typedef struct On2AVCContext {
    AVCodecContext *avctx;
    AVFloatDSPContext *fdsp;
    FFTContext mdct, mdct_half, mdct_small;
    FFTContext fft128, fft256, fft512, fft1024;
    void (*wtf)(struct On2AVCContext *ctx, float *out, float *in, int size);

    int is_av500;

    const On2AVCMode *modes;
    int window_type, prev_window_type;
    int num_windows, num_bands;
    int bits_per_section;
    const int *band_start;

    int grouping[8];
    int ms_present;
    int ms_info[ON2AVC_MAX_BANDS];

    int is_long;

    uint8_t band_type[ON2AVC_MAX_BANDS];
    uint8_t band_run_end[ON2AVC_MAX_BANDS];
    int     num_sections;

    float band_scales[ON2AVC_MAX_BANDS];

    VLC scale_diff;
    VLC cb_vlc[16];

    float scale_tab[128];

    DECLARE_ALIGNED(32, float, coeffs)[2][ON2AVC_SUBFRAME_SIZE];
    DECLARE_ALIGNED(32, float, delay) [2][ON2AVC_SUBFRAME_SIZE];

    DECLARE_ALIGNED(32, float, temp)     [ON2AVC_SUBFRAME_SIZE * 2];
    DECLARE_ALIGNED(32, float, mdct_buf) [ON2AVC_SUBFRAME_SIZE];
    DECLARE_ALIGNED(32, float, long_win) [ON2AVC_SUBFRAME_SIZE];
    DECLARE_ALIGNED(32, float, short_win)[ON2AVC_SUBFRAME_SIZE / 8];
} On2AVCContext;

static void on2avc_read_ms_info(On2AVCContext *c, GetBitContext *gb)
{
    int w, b, band_off = 0;

    c->ms_present = get_bits1(gb);
    if (!c->ms_present)
        return;
    for (w = 0; w < c->num_windows; w++) {
        if (!c->grouping[w]) {
            memcpy(c->ms_info + band_off,
                   c->ms_info + band_off - c->num_bands,
                   c->num_bands * sizeof(*c->ms_info));
            band_off += c->num_bands;
            continue;
        }
        for (b = 0; b < c->num_bands; b++)
            c->ms_info[band_off++] = get_bits1(gb);
    }
}

// do not see Table 17 in ISO/IEC 13818-7
static int on2avc_decode_band_types(On2AVCContext *c, GetBitContext *gb)
{
    int bits_per_sect = c->is_long ? 5 : 3;
    int esc_val = (1 << bits_per_sect) - 1;
    int num_bands = c->num_bands * c->num_windows;
    int band = 0, i, band_type, run_len, run;

    while (band < num_bands) {
        band_type = get_bits(gb, 4);
        run_len   = 1;
        do {
            run = get_bits(gb, bits_per_sect);
            if (run > num_bands - band - run_len) {
                av_log(c->avctx, AV_LOG_ERROR, "Invalid band type run\n");
                return AVERROR_INVALIDDATA;
            }
            run_len += run;
        } while (run == esc_val);
        for (i = band; i < band + run_len; i++) {
            c->band_type[i]    = band_type;
            c->band_run_end[i] = band + run_len;
        }
        band += run_len;
    }

    return 0;
}

// completely not like Table 18 in ISO/IEC 13818-7
// (no intensity stereo, different coding for the first coefficient)
static int on2avc_decode_band_scales(On2AVCContext *c, GetBitContext *gb)
{
    int w, w2, b, scale, first = 1;
    int band_off = 0;

    for (w = 0; w < c->num_windows; w++) {
        if (!c->grouping[w]) {
            memcpy(c->band_scales + band_off,
                   c->band_scales + band_off - c->num_bands,
                   c->num_bands * sizeof(*c->band_scales));
            band_off += c->num_bands;
            continue;
        }
        for (b = 0; b < c->num_bands; b++) {
            if (!c->band_type[band_off]) {
                int all_zero = 1;
                for (w2 = w + 1; w2 < c->num_windows; w2++) {
                    if (c->grouping[w2])
                        break;
                    if (c->band_type[w2 * c->num_bands + b]) {
                        all_zero = 0;
                        break;
                    }
                }
                if (all_zero) {
                    c->band_scales[band_off++] = 0;
                    continue;
                }
            }
            if (first) {
                scale = get_bits(gb, 7);
                first = 0;
            } else {
                scale += get_vlc2(gb, c->scale_diff.table, 9, 3) - 60;
            }
            if (scale < 0 || scale > 127) {
                av_log(c->avctx, AV_LOG_ERROR, "Invalid scale value %d\n",
                       scale);
                return AVERROR_INVALIDDATA;
            }
            c->band_scales[band_off++] = c->scale_tab[scale];
        }
    }

    return 0;
}

static inline float on2avc_scale(int v, float scale)
{
    return v * sqrtf(abs(v)) * scale;
}

// spectral data is coded completely differently - there are no unsigned codebooks
static int on2avc_decode_quads(On2AVCContext *c, GetBitContext *gb, float *dst,
                               int dst_size, int type, float band_scale)
{
    int i, j, val, val1;

    for (i = 0; i < dst_size; i += 4) {
        val = get_vlc2(gb, c->cb_vlc[type].table, 9, 2);

        for (j = 0; j < 4; j++) {
            val1 = sign_extend((val >> (12 - j * 4)) & 0xF, 4);
            *dst++ = on2avc_scale(val1, band_scale);
        }
    }

    return 0;
}

static inline int get_egolomb(GetBitContext *gb)
{
    int v = 4;

    while (get_bits1(gb)) {
        v++;
        if (v > 30) {
            av_log(NULL, AV_LOG_WARNING, "Too large golomb code in get_egolomb.\n");
            v = 30;
            break;
        }
    }

    return (1 << v) + get_bits_long(gb, v);
}

static int on2avc_decode_pairs(On2AVCContext *c, GetBitContext *gb, float *dst,
                               int dst_size, int type, float band_scale)
{
    int i, val, val1, val2, sign;

    for (i = 0; i < dst_size; i += 2) {
        val = get_vlc2(gb, c->cb_vlc[type].table, 9, 2);

        val1 = sign_extend(val >> 8,   8);
        val2 = sign_extend(val & 0xFF, 8);
        if (type == ON2AVC_ESC_CB) {
            if (val1 <= -16 || val1 >= 16) {
                sign = 1 - (val1 < 0) * 2;
                val1 = sign * get_egolomb(gb);
            }
            if (val2 <= -16 || val2 >= 16) {
                sign = 1 - (val2 < 0) * 2;
                val2 = sign * get_egolomb(gb);
            }
        }

        *dst++ = on2avc_scale(val1, band_scale);
        *dst++ = on2avc_scale(val2, band_scale);
    }

    return 0;
}

static int on2avc_read_channel_data(On2AVCContext *c, GetBitContext *gb, int ch)
{
    int ret;
    int w, b, band_idx;
    float *coeff_ptr;

    if ((ret = on2avc_decode_band_types(c, gb)) < 0)
        return ret;
    if ((ret = on2avc_decode_band_scales(c, gb)) < 0)
        return ret;

    coeff_ptr = c->coeffs[ch];
    band_idx  = 0;
    memset(coeff_ptr, 0, ON2AVC_SUBFRAME_SIZE * sizeof(*coeff_ptr));
    for (w = 0; w < c->num_windows; w++) {
        for (b = 0; b < c->num_bands; b++) {
            int band_size = c->band_start[b + 1] - c->band_start[b];
            int band_type = c->band_type[band_idx + b];

            if (!band_type) {
                coeff_ptr += band_size;
                continue;
            }
            if (band_type < 9)
                on2avc_decode_quads(c, gb, coeff_ptr, band_size, band_type,
                                    c->band_scales[band_idx + b]);
            else
                on2avc_decode_pairs(c, gb, coeff_ptr, band_size, band_type,
                                    c->band_scales[band_idx + b]);
            coeff_ptr += band_size;
        }
        band_idx += c->num_bands;
    }

    return 0;
}

static int on2avc_apply_ms(On2AVCContext *c)
{
    int w, b, i;
    int band_off = 0;
    float *ch0 = c->coeffs[0];
    float *ch1 = c->coeffs[1];

    for (w = 0; w < c->num_windows; w++) {
        for (b = 0; b < c->num_bands; b++) {
            if (c->ms_info[band_off + b]) {
                for (i = c->band_start[b]; i < c->band_start[b + 1]; i++) {
                    float l = *ch0, r = *ch1;
                    *ch0++ = l + r;
                    *ch1++ = l - r;
                }
            } else {
                ch0 += c->band_start[b + 1] - c->band_start[b];
                ch1 += c->band_start[b + 1] - c->band_start[b];
            }
        }
        band_off += c->num_bands;
    }
    return 0;
}

static void zero_head_and_tail(float *src, int len, int order0, int order1)
{
    memset(src,                0, sizeof(*src) * order0);
    memset(src + len - order1, 0, sizeof(*src) * order1);
}

static void pretwiddle(float *src, float *dst, int dst_len, int tab_step,
                       int step, int order0, int order1, const double * const *tabs)
{
    float *src2, *out;
    const double *tab;
    int i, j;

    out = dst;
    tab = tabs[0];
    for (i = 0; i < tab_step; i++) {
        double sum = 0;
        for (j = 0; j < order0; j++)
            sum += src[j] * tab[j * tab_step + i];
        out[i] += sum;
    }

    out = dst + dst_len - tab_step;
    tab = tabs[order0];
    src2 = src + (dst_len - tab_step) / step + 1 + order0;
    for (i = 0; i < tab_step; i++) {
        double sum = 0;
        for (j = 0; j < order1; j++)
            sum += src2[j] * tab[j * tab_step + i];
        out[i] += sum;
    }
}

static void twiddle(float *src1, float *src2, int src2_len,
                    const double *tab, int tab_len, int step,
                    int order0, int order1, const double * const *tabs)
{
    int steps;
    int mask;
    int i, j;

    steps = (src2_len - tab_len) / step + 1;
    pretwiddle(src1, src2, src2_len, tab_len, step, order0, order1, tabs);
    mask = tab_len - 1;

    for (i = 0; i < steps; i++) {
        float in0 = src1[order0 + i];
        int   pos = (src2_len - 1) & mask;

        if (pos < tab_len) {
            const double *t = tab;
            for (j = pos; j >= 0; j--)
                src2[j] += in0 * *t++;
            for (j = 0; j < tab_len - pos - 1; j++)
                src2[src2_len - j - 1] += in0 * tab[pos + 1 + j];
        } else {
            for (j = 0; j < tab_len; j++)
                src2[pos - j] += in0 * tab[j];
        }
        mask = pos + step;
    }
}

#define CMUL1_R(s, t, is, it) \
    s[is + 0] * t[it + 0] - s[is + 1] * t[it + 1]
#define CMUL1_I(s, t, is, it) \
    s[is + 0] * t[it + 1] + s[is + 1] * t[it + 0]
#define CMUL2_R(s, t, is, it) \
    s[is + 0] * t[it + 0] + s[is + 1] * t[it + 1]
#define CMUL2_I(s, t, is, it) \
    s[is + 0] * t[it + 1] - s[is + 1] * t[it + 0]

#define CMUL0(dst, id, s0, s1, s2, s3, t0, t1, t2, t3, is, it)         \
    dst[id]     = s0[is] * t0[it]     + s1[is] * t1[it]                \
                + s2[is] * t2[it]     + s3[is] * t3[it];               \
    dst[id + 1] = s0[is] * t0[it + 1] + s1[is] * t1[it + 1]            \
                + s2[is] * t2[it + 1] + s3[is] * t3[it + 1];

#define CMUL1(dst, s0, s1, s2, s3, t0, t1, t2, t3, is, it)             \
    *dst++ = CMUL1_R(s0, t0, is, it)                                   \
           + CMUL1_R(s1, t1, is, it)                                   \
           + CMUL1_R(s2, t2, is, it)                                   \
           + CMUL1_R(s3, t3, is, it);                                  \
    *dst++ = CMUL1_I(s0, t0, is, it)                                   \
           + CMUL1_I(s1, t1, is, it)                                   \
           + CMUL1_I(s2, t2, is, it)                                   \
           + CMUL1_I(s3, t3, is, it);

#define CMUL2(dst, s0, s1, s2, s3, t0, t1, t2, t3, is, it)             \
    *dst++ = CMUL2_R(s0, t0, is, it)                                   \
           + CMUL2_R(s1, t1, is, it)                                   \
           + CMUL2_R(s2, t2, is, it)                                   \
           + CMUL2_R(s3, t3, is, it);                                  \
    *dst++ = CMUL2_I(s0, t0, is, it)                                   \
           + CMUL2_I(s1, t1, is, it)                                   \
           + CMUL2_I(s2, t2, is, it)                                   \
           + CMUL2_I(s3, t3, is, it);

static void combine_fft(float *s0, float *s1, float *s2, float *s3, float *dst,
                        const float *t0, const float *t1,
                        const float *t2, const float *t3, int len, int step)
{
    const float *h0, *h1, *h2, *h3;
    float *d1, *d2;
    int tmp, half;
    int len2 = len >> 1, len4 = len >> 2;
    int hoff;
    int i, j, k;

    tmp = step;
    for (half = len2; tmp > 1; half <<= 1, tmp >>= 1);

    h0 = t0 + half;
    h1 = t1 + half;
    h2 = t2 + half;
    h3 = t3 + half;

    CMUL0(dst, 0, s0, s1, s2, s3, t0, t1, t2, t3, 0, 0);

    hoff = 2 * step * (len4 >> 1);

    j = 2;
    k = 2 * step;
    d1 = dst + 2;
    d2 = dst + 2 + (len >> 1);
    for (i = 0; i < (len4 - 1) >> 1; i++) {
        CMUL1(d1, s0, s1, s2, s3, t0, t1, t2, t3, j, k);
        CMUL1(d2, s0, s1, s2, s3, h0, h1, h2, h3, j, k);
        j += 2;
        k += 2 * step;
    }
    CMUL0(dst, len4,        s0, s1, s2, s3, t0, t1, t2, t3, 1, hoff);
    CMUL0(dst, len4 + len2, s0, s1, s2, s3, h0, h1, h2, h3, 1, hoff);

    j = len4;
    k = hoff + 2 * step * len4;
    d1 = dst + len4 + 2;
    d2 = dst + len4 + 2 + len2;
    for (i = 0; i < (len4 - 2) >> 1; i++) {
        CMUL2(d1, s0, s1, s2, s3, t0, t1, t2, t3, j, k);
        CMUL2(d2, s0, s1, s2, s3, h0, h1, h2, h3, j, k);
        j -= 2;
        k += 2 * step;
    }
    CMUL0(dst, len2 + 4, s0, s1, s2, s3, t0, t1, t2, t3, 0, k);
}

static void wtf_end_512(On2AVCContext *c, float *out, float *src,
                        float *tmp0, float *tmp1)
{
    memcpy(src,        tmp0,      384 * sizeof(*tmp0));
    memcpy(tmp0 + 384, src + 384, 128 * sizeof(*tmp0));

    zero_head_and_tail(src,       128, 16, 4);
    zero_head_and_tail(src + 128, 128, 16, 4);
    zero_head_and_tail(src + 256, 128, 13, 7);
    zero_head_and_tail(src + 384, 128, 15, 5);

    c->fft128.fft_permute(&c->fft128, (FFTComplex*)src);
    c->fft128.fft_permute(&c->fft128, (FFTComplex*)(src + 128));
    c->fft128.fft_permute(&c->fft128, (FFTComplex*)(src + 256));
    c->fft128.fft_permute(&c->fft128, (FFTComplex*)(src + 384));
    c->fft128.fft_calc(&c->fft128, (FFTComplex*)src);
    c->fft128.fft_calc(&c->fft128, (FFTComplex*)(src + 128));
    c->fft128.fft_calc(&c->fft128, (FFTComplex*)(src + 256));
    c->fft128.fft_calc(&c->fft128, (FFTComplex*)(src + 384));
    combine_fft(src, src + 128, src + 256, src + 384, tmp1,
                ff_on2avc_ctab_1, ff_on2avc_ctab_2,
                ff_on2avc_ctab_3, ff_on2avc_ctab_4, 512, 2);
    c->fft512.fft_permute(&c->fft512, (FFTComplex*)tmp1);
    c->fft512.fft_calc(&c->fft512, (FFTComplex*)tmp1);

    pretwiddle(&tmp0[  0], tmp1, 512, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
    pretwiddle(&tmp0[128], tmp1, 512, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
    pretwiddle(&tmp0[256], tmp1, 512, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
    pretwiddle(&tmp0[384], tmp1, 512, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);

    memcpy(src, tmp1, 512 * sizeof(float));
}

static void wtf_end_1024(On2AVCContext *c, float *out, float *src,
                         float *tmp0, float *tmp1)
{
    memcpy(src,        tmp0,      768 * sizeof(*tmp0));
    memcpy(tmp0 + 768, src + 768, 256 * sizeof(*tmp0));

    zero_head_and_tail(src,       256, 16, 4);
    zero_head_and_tail(src + 256, 256, 16, 4);
    zero_head_and_tail(src + 512, 256, 13, 7);
    zero_head_and_tail(src + 768, 256, 15, 5);

    c->fft256.fft_permute(&c->fft256, (FFTComplex*)src);
    c->fft256.fft_permute(&c->fft256, (FFTComplex*)(src + 256));
    c->fft256.fft_permute(&c->fft256, (FFTComplex*)(src + 512));
    c->fft256.fft_permute(&c->fft256, (FFTComplex*)(src + 768));
    c->fft256.fft_calc(&c->fft256, (FFTComplex*)src);
    c->fft256.fft_calc(&c->fft256, (FFTComplex*)(src + 256));
    c->fft256.fft_calc(&c->fft256, (FFTComplex*)(src + 512));
    c->fft256.fft_calc(&c->fft256, (FFTComplex*)(src + 768));
    combine_fft(src, src + 256, src + 512, src + 768, tmp1,
                ff_on2avc_ctab_1, ff_on2avc_ctab_2,
                ff_on2avc_ctab_3, ff_on2avc_ctab_4, 1024, 1);
    c->fft1024.fft_permute(&c->fft1024, (FFTComplex*)tmp1);
    c->fft1024.fft_calc(&c->fft1024, (FFTComplex*)tmp1);

    pretwiddle(&tmp0[  0], tmp1, 1024, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
    pretwiddle(&tmp0[256], tmp1, 1024, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
    pretwiddle(&tmp0[512], tmp1, 1024, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
    pretwiddle(&tmp0[768], tmp1, 1024, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);

    memcpy(src, tmp1, 1024 * sizeof(float));
}

static void wtf_40(On2AVCContext *c, float *out, float *src, int size)
{
    float *tmp0 = c->temp, *tmp1 = c->temp + 1024;

    memset(tmp0, 0, sizeof(*tmp0) * 1024);
    memset(tmp1, 0, sizeof(*tmp1) * 1024);

    if (size == 512) {
        twiddle(src,       &tmp0[  0], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +   8, &tmp0[  0], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  16, &tmp0[ 16], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  24, &tmp0[ 16], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +  32, &tmp0[ 32], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +  40, &tmp0[ 32], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  48, &tmp0[ 48], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  56, &tmp0[ 48], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(&tmp0[ 0], &tmp1[  0], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(&tmp0[16], &tmp1[  0], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(&tmp0[32], &tmp1[ 32], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(&tmp0[48], &tmp1[ 32], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  64, &tmp1[ 64], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  80, &tmp1[ 64], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  96, &tmp1[ 96], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 112, &tmp1[ 96], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src + 128, &tmp1[128], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src + 144, &tmp1[128], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 160, &tmp1[160], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 176, &tmp1[160], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);

        memset(tmp0, 0, 64 * sizeof(*tmp0));

        twiddle(&tmp1[  0], &tmp0[  0], 128, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(&tmp1[ 32], &tmp0[  0], 128, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(&tmp1[ 64], &tmp0[  0], 128, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(&tmp1[ 96], &tmp0[  0], 128, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);
        twiddle(&tmp1[128], &tmp0[128], 128, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);
        twiddle(&tmp1[160], &tmp0[128], 128, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 192,  &tmp0[128], 128, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 224,  &tmp0[128], 128, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 256,  &tmp0[256], 128, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 288,  &tmp0[256], 128, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 320,  &tmp0[256], 128, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 352,  &tmp0[256], 128, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);

        wtf_end_512(c, out, src, tmp0, tmp1);
    } else {
        twiddle(src,       &tmp0[  0], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  16, &tmp0[  0], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  32, &tmp0[ 32], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  48, &tmp0[ 32], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  64, &tmp0[ 64], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  80, &tmp0[ 64], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  96, &tmp0[ 96], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 112, &tmp0[ 96], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(&tmp0[ 0], &tmp1[  0], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(&tmp0[32], &tmp1[  0], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(&tmp0[64], &tmp1[ 64], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(&tmp0[96], &tmp1[ 64], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 128, &tmp1[128], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 160, &tmp1[128], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 192, &tmp1[192], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 224, &tmp1[192], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 256, &tmp1[256], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 288, &tmp1[256], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 320, &tmp1[320], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 352, &tmp1[320], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);

        memset(tmp0, 0, 128 * sizeof(*tmp0));

        twiddle(&tmp1[  0], &tmp0[  0], 256, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(&tmp1[ 64], &tmp0[  0], 256, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(&tmp1[128], &tmp0[  0], 256, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(&tmp1[192], &tmp0[  0], 256, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);
        twiddle(&tmp1[256], &tmp0[256], 256, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);
        twiddle(&tmp1[320], &tmp0[256], 256, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 384,  &tmp0[256], 256, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 448,  &tmp0[256], 256, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 512,  &tmp0[512], 256, ff_on2avc_tab_84_1, 84, 4, 16, 4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 576,  &tmp0[512], 256, ff_on2avc_tab_84_2, 84, 4, 16, 4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 640,  &tmp0[512], 256, ff_on2avc_tab_84_3, 84, 4, 13, 7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 704,  &tmp0[512], 256, ff_on2avc_tab_84_4, 84, 4, 15, 5, ff_on2avc_tabs_20_84_4);

        wtf_end_1024(c, out, src, tmp0, tmp1);
    }
}

static void wtf_44(On2AVCContext *c, float *out, float *src, int size)
{
    float *tmp0 = c->temp, *tmp1 = c->temp + 1024;

    memset(tmp0, 0, sizeof(*tmp0) * 1024);
    memset(tmp1, 0, sizeof(*tmp1) * 1024);

    if (size == 512) {
        twiddle(src,       &tmp0[ 0], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +   8, &tmp0[ 0], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  16, &tmp0[16], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  24, &tmp0[16], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +  32, &tmp0[32], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(src +  40, &tmp0[32], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  48, &tmp0[48], 16, ff_on2avc_tab_10_2, 10, 2, 3, 1, ff_on2avc_tabs_4_10_2);
        twiddle(src +  56, &tmp0[48], 16, ff_on2avc_tab_10_1, 10, 2, 1, 3, ff_on2avc_tabs_4_10_1);
        twiddle(&tmp0[ 0], &tmp1[ 0], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(&tmp0[16], &tmp1[ 0], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(&tmp0[32], &tmp1[32], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(&tmp0[48], &tmp1[32], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  64, &tmp1[64], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  80, &tmp1[64], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  96, &tmp1[96], 32, ff_on2avc_tab_20_2, 20, 2, 4, 5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 112, &tmp1[96], 32, ff_on2avc_tab_20_1, 20, 2, 5, 4, ff_on2avc_tabs_9_20_1);

        memset(tmp0, 0, 64 * sizeof(*tmp0));

        twiddle(&tmp1[ 0], &tmp0[  0], 128, ff_on2avc_tab_84_1, 84, 4, 16,  4, ff_on2avc_tabs_20_84_1);
        twiddle(&tmp1[32], &tmp0[  0], 128, ff_on2avc_tab_84_2, 84, 4, 16,  4, ff_on2avc_tabs_20_84_2);
        twiddle(&tmp1[64], &tmp0[  0], 128, ff_on2avc_tab_84_3, 84, 4, 13,  7, ff_on2avc_tabs_20_84_3);
        twiddle(&tmp1[96], &tmp0[  0], 128, ff_on2avc_tab_84_4, 84, 4, 15,  5, ff_on2avc_tabs_20_84_4);
        twiddle(src + 128, &tmp0[128], 128, ff_on2avc_tab_84_4, 84, 4, 15,  5, ff_on2avc_tabs_20_84_4);
        twiddle(src + 160, &tmp0[128], 128, ff_on2avc_tab_84_3, 84, 4, 13,  7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 192, &tmp0[128], 128, ff_on2avc_tab_84_2, 84, 4, 16,  4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 224, &tmp0[128], 128, ff_on2avc_tab_84_1, 84, 4, 16,  4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 256, &tmp0[256], 128, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 320, &tmp0[256], 128, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);

        wtf_end_512(c, out, src, tmp0, tmp1);
    } else {
        twiddle(src,       &tmp0[  0], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  16, &tmp0[  0], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  32, &tmp0[ 32], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  48, &tmp0[ 32], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  64, &tmp0[ 64], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(src +  80, &tmp0[ 64], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src +  96, &tmp0[ 96], 32, ff_on2avc_tab_20_2, 20, 2,  4,  5, ff_on2avc_tabs_9_20_2);
        twiddle(src + 112, &tmp0[ 96], 32, ff_on2avc_tab_20_1, 20, 2,  5,  4, ff_on2avc_tabs_9_20_1);
        twiddle(&tmp0[ 0], &tmp1[  0], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(&tmp0[32], &tmp1[  0], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(&tmp0[64], &tmp1[ 64], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(&tmp0[96], &tmp1[ 64], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 128, &tmp1[128], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 160, &tmp1[128], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 192, &tmp1[192], 64, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);
        twiddle(src + 224, &tmp1[192], 64, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);

        memset(tmp0, 0, 128 * sizeof(*tmp0));

        twiddle(&tmp1[  0], &tmp0[  0], 256, ff_on2avc_tab_84_1, 84, 4, 16,  4, ff_on2avc_tabs_20_84_1);
        twiddle(&tmp1[ 64], &tmp0[  0], 256, ff_on2avc_tab_84_2, 84, 4, 16,  4, ff_on2avc_tabs_20_84_2);
        twiddle(&tmp1[128], &tmp0[  0], 256, ff_on2avc_tab_84_3, 84, 4, 13,  7, ff_on2avc_tabs_20_84_3);
        twiddle(&tmp1[192], &tmp0[  0], 256, ff_on2avc_tab_84_4, 84, 4, 15,  5, ff_on2avc_tabs_20_84_4);
        twiddle(src + 256,  &tmp0[256], 256, ff_on2avc_tab_84_4, 84, 4, 15,  5, ff_on2avc_tabs_20_84_4);
        twiddle(src + 320,  &tmp0[256], 256, ff_on2avc_tab_84_3, 84, 4, 13,  7, ff_on2avc_tabs_20_84_3);
        twiddle(src + 384,  &tmp0[256], 256, ff_on2avc_tab_84_2, 84, 4, 16,  4, ff_on2avc_tabs_20_84_2);
        twiddle(src + 448,  &tmp0[256], 256, ff_on2avc_tab_84_1, 84, 4, 16,  4, ff_on2avc_tabs_20_84_1);
        twiddle(src + 512,  &tmp0[512], 256, ff_on2avc_tab_40_1, 40, 2, 11,  8, ff_on2avc_tabs_19_40_1);
        twiddle(src + 640,  &tmp0[512], 256, ff_on2avc_tab_40_2, 40, 2,  8, 11, ff_on2avc_tabs_19_40_2);

        wtf_end_1024(c, out, src, tmp0, tmp1);
    }
}

static int on2avc_reconstruct_channel_ext(On2AVCContext *c, AVFrame *dst, int offset)
{
    int ch, i;

    for (ch = 0; ch < c->avctx->channels; ch++) {
        float *out   = (float*)dst->extended_data[ch] + offset;
        float *in    = c->coeffs[ch];
        float *saved = c->delay[ch];
        float *buf   = c->mdct_buf;
        float *wout  = out + 448;

        switch (c->window_type) {
        case WINDOW_TYPE_EXT7:
            c->mdct.imdct_half(&c->mdct, buf, in);
            break;
        case WINDOW_TYPE_EXT4:
            c->wtf(c, buf, in, 1024);
            break;
        case WINDOW_TYPE_EXT5:
            c->wtf(c, buf, in, 512);
            c->mdct.imdct_half(&c->mdct_half, buf + 512, in + 512);
            for (i = 0; i < 256; i++) {
                FFSWAP(float, buf[i + 512], buf[1023 - i]);
            }
            break;
        case WINDOW_TYPE_EXT6:
            c->mdct.imdct_half(&c->mdct_half, buf, in);
            for (i = 0; i < 256; i++) {
                FFSWAP(float, buf[i], buf[511 - i]);
            }
            c->wtf(c, buf + 512, in + 512, 512);
            break;
        }

        memcpy(out, saved, 448 * sizeof(float));
        c->fdsp->vector_fmul_window(wout, saved + 448, buf, c->short_win, 64);
        memcpy(wout + 128,  buf + 64,         448 * sizeof(float));
        memcpy(saved,       buf + 512,        448 * sizeof(float));
        memcpy(saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
    }

    return 0;
}

// not borrowed from aacdec.c - the codec has original design after all
static int on2avc_reconstruct_channel(On2AVCContext *c, int channel,
                                      AVFrame *dst, int offset)
{
    int i;
    float *out   = (float*)dst->extended_data[channel] + offset;
    float *in    = c->coeffs[channel];
    float *saved = c->delay[channel];
    float *buf   = c->mdct_buf;
    float *temp  = c->temp;

    switch (c->window_type) {
    case WINDOW_TYPE_LONG_START:
    case WINDOW_TYPE_LONG_STOP:
    case WINDOW_TYPE_LONG:
        c->mdct.imdct_half(&c->mdct, buf, in);
        break;
    case WINDOW_TYPE_8SHORT:
        for (i = 0; i < ON2AVC_SUBFRAME_SIZE; i += ON2AVC_SUBFRAME_SIZE / 8)
            c->mdct_small.imdct_half(&c->mdct_small, buf + i, in + i);
        break;
    }

    if ((c->prev_window_type == WINDOW_TYPE_LONG ||
         c->prev_window_type == WINDOW_TYPE_LONG_STOP) &&
        (c->window_type == WINDOW_TYPE_LONG ||
         c->window_type == WINDOW_TYPE_LONG_START)) {
        c->fdsp->vector_fmul_window(out, saved, buf, c->long_win, 512);
    } else {
        float *wout = out + 448;
        memcpy(out, saved, 448 * sizeof(float));

        if (c->window_type == WINDOW_TYPE_8SHORT) {
            c->fdsp->vector_fmul_window(wout + 0*128, saved + 448,      buf + 0*128, c->short_win, 64);
            c->fdsp->vector_fmul_window(wout + 1*128, buf + 0*128 + 64, buf + 1*128, c->short_win, 64);
            c->fdsp->vector_fmul_window(wout + 2*128, buf + 1*128 + 64, buf + 2*128, c->short_win, 64);
            c->fdsp->vector_fmul_window(wout + 3*128, buf + 2*128 + 64, buf + 3*128, c->short_win, 64);
            c->fdsp->vector_fmul_window(temp,         buf + 3*128 + 64, buf + 4*128, c->short_win, 64);
            memcpy(wout + 4*128, temp, 64 * sizeof(float));
        } else {
            c->fdsp->vector_fmul_window(wout, saved + 448, buf, c->short_win, 64);
            memcpy(wout + 128, buf + 64, 448 * sizeof(float));
        }
    }

    // buffer update
    switch (c->window_type) {
    case WINDOW_TYPE_8SHORT:
        memcpy(saved,       temp + 64,         64 * sizeof(float));
        c->fdsp->vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, c->short_win, 64);
        c->fdsp->vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, c->short_win, 64);
        c->fdsp->vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, c->short_win, 64);
        memcpy(saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
        break;
    case WINDOW_TYPE_LONG_START:
        memcpy(saved,       buf + 512,        448 * sizeof(float));
        memcpy(saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
        break;
    case WINDOW_TYPE_LONG_STOP:
    case WINDOW_TYPE_LONG:
        memcpy(saved,       buf + 512,        512 * sizeof(float));
        break;
    }
    return 0;
}

static int on2avc_decode_subframe(On2AVCContext *c, const uint8_t *buf,
                                  int buf_size, AVFrame *dst, int offset)
{
    GetBitContext gb;
    int i, ret;

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;

    if (get_bits1(&gb)) {
        av_log(c->avctx, AV_LOG_ERROR, "enh bit set\n");
        return AVERROR_INVALIDDATA;
    }
    c->prev_window_type = c->window_type;
    c->window_type      = get_bits(&gb, 3);

    c->band_start  = c->modes[c->window_type].band_start;
    c->num_windows = c->modes[c->window_type].num_windows;
    c->num_bands   = c->modes[c->window_type].num_bands;
    c->is_long     = (c->window_type != WINDOW_TYPE_8SHORT);

    c->grouping[0] = 1;
    for (i = 1; i < c->num_windows; i++)
        c->grouping[i] = !get_bits1(&gb);

    on2avc_read_ms_info(c, &gb);
    for (i = 0; i < c->avctx->channels; i++)
        if ((ret = on2avc_read_channel_data(c, &gb, i)) < 0)
            return AVERROR_INVALIDDATA;
    if (c->avctx->channels == 2 && c->ms_present)
        on2avc_apply_ms(c);
    if (c->window_type < WINDOW_TYPE_EXT4) {
        for (i = 0; i < c->avctx->channels; i++)
            on2avc_reconstruct_channel(c, i, dst, offset);
    } else {
        on2avc_reconstruct_channel_ext(c, dst, offset);
    }

    return 0;
}

static int on2avc_decode_frame(AVCodecContext * avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    On2AVCContext *c   = avctx->priv_data;
    GetByteContext gb;
    int num_frames = 0, frame_size, audio_off;
    int ret;

    if (c->is_av500) {
        /* get output buffer */
        frame->nb_samples = ON2AVC_SUBFRAME_SIZE;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        if ((ret = on2avc_decode_subframe(c, buf, buf_size, frame, 0)) < 0)
            return ret;
    } else {
        bytestream2_init(&gb, buf, buf_size);
        while (bytestream2_get_bytes_left(&gb) > 2) {
            frame_size = bytestream2_get_le16(&gb);
            if (!frame_size || frame_size > bytestream2_get_bytes_left(&gb)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid subframe size %d\n",
                       frame_size);
                return AVERROR_INVALIDDATA;
            }
            num_frames++;
            bytestream2_skip(&gb, frame_size);
        }
        if (!num_frames) {
            av_log(avctx, AV_LOG_ERROR, "No subframes present\n");
            return AVERROR_INVALIDDATA;
        }

        /* get output buffer */
        frame->nb_samples = ON2AVC_SUBFRAME_SIZE * num_frames;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        audio_off = 0;
        bytestream2_init(&gb, buf, buf_size);
        while (bytestream2_get_bytes_left(&gb) > 2) {
            frame_size = bytestream2_get_le16(&gb);
            if ((ret = on2avc_decode_subframe(c, gb.buffer, frame_size,
                                              frame, audio_off)) < 0)
                return ret;
            audio_off += ON2AVC_SUBFRAME_SIZE;
            bytestream2_skip(&gb, frame_size);
        }
    }

    *got_frame_ptr = 1;

    return buf_size;
}

static av_cold void on2avc_free_vlcs(On2AVCContext *c)
{
    int i;

    ff_free_vlc(&c->scale_diff);
    for (i = 1; i < 16; i++)
        ff_free_vlc(&c->cb_vlc[i]);
}

static av_cold int on2avc_decode_init(AVCodecContext *avctx)
{
    On2AVCContext *c = avctx->priv_data;
    int i;

    if (avctx->channels > 2U) {
        avpriv_request_sample(avctx, "Decoding more than 2 channels");
        return AVERROR_PATCHWELCOME;
    }

    c->avctx = avctx;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;
    avctx->channel_layout = (avctx->channels == 2) ? AV_CH_LAYOUT_STEREO
                                                   : AV_CH_LAYOUT_MONO;

    c->is_av500 = (avctx->codec_tag == 0x500);

    if (avctx->channels == 2)
        av_log(avctx, AV_LOG_WARNING,
               "Stereo mode support is not good, patch is welcome\n");

    // We add -0.01 before ceil() to avoid any values to fall at exactly the
    // midpoint between different ceil values. The results are identical to
    // using pow(10, i / 10.0) without such bias
    for (i = 0; i < 20; i++)
        c->scale_tab[i] = ceil(ff_exp10(i * 0.1) * 16 - 0.01) / 32;
    for (; i < 128; i++)
        c->scale_tab[i] = ceil(ff_exp10(i * 0.1) * 0.5 - 0.01);

    if (avctx->sample_rate < 32000 || avctx->channels == 1)
        memcpy(c->long_win, ff_on2avc_window_long_24000,
               1024 * sizeof(*c->long_win));
    else
        memcpy(c->long_win, ff_on2avc_window_long_32000,
               1024 * sizeof(*c->long_win));
    memcpy(c->short_win, ff_on2avc_window_short, 128 * sizeof(*c->short_win));

    c->modes = (avctx->sample_rate <= 40000) ? ff_on2avc_modes_40
                                             : ff_on2avc_modes_44;
    c->wtf   = (avctx->sample_rate <= 40000) ? wtf_40
                                             : wtf_44;

    ff_mdct_init(&c->mdct,       11, 1, 1.0 / (32768.0 * 1024.0));
    ff_mdct_init(&c->mdct_half,  10, 1, 1.0 / (32768.0 * 512.0));
    ff_mdct_init(&c->mdct_small,  8, 1, 1.0 / (32768.0 * 128.0));
    ff_fft_init(&c->fft128,  6, 0);
    ff_fft_init(&c->fft256,  7, 0);
    ff_fft_init(&c->fft512,  8, 1);
    ff_fft_init(&c->fft1024, 9, 1);
    c->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!c->fdsp)
        return AVERROR(ENOMEM);

    if (init_vlc(&c->scale_diff, 9, ON2AVC_SCALE_DIFFS,
                 ff_on2avc_scale_diff_bits,  1, 1,
                 ff_on2avc_scale_diff_codes, 4, 4, 0)) {
        goto vlc_fail;
    }
    for (i = 1; i < 16; i++) {
        int idx = i - 1, codes_size = ff_on2avc_cb_codes_sizes[idx];
        if (ff_init_vlc_sparse(&c->cb_vlc[i], 9, ff_on2avc_cb_elems[idx],
                               ff_on2avc_cb_bits[idx],  1, 1,
                               ff_on2avc_cb_codes[idx], codes_size, codes_size,
                               ff_on2avc_cb_syms[idx],  2, 2, 0)) {
            goto vlc_fail;
        }
    }

    return 0;
vlc_fail:
    av_log(avctx, AV_LOG_ERROR, "Cannot init VLC\n");
    return AVERROR(ENOMEM);
}

static av_cold int on2avc_decode_close(AVCodecContext *avctx)
{
    On2AVCContext *c = avctx->priv_data;

    ff_mdct_end(&c->mdct);
    ff_mdct_end(&c->mdct_half);
    ff_mdct_end(&c->mdct_small);
    ff_fft_end(&c->fft128);
    ff_fft_end(&c->fft256);
    ff_fft_end(&c->fft512);
    ff_fft_end(&c->fft1024);

    av_freep(&c->fdsp);

    on2avc_free_vlcs(c);

    return 0;
}


AVCodec ff_on2avc_decoder = {
    .name           = "on2avc",
    .long_name      = NULL_IF_CONFIG_SMALL("On2 Audio for Video Codec"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ON2AVC,
    .priv_data_size = sizeof(On2AVCContext),
    .init           = on2avc_decode_init,
    .decode         = on2avc_decode_frame,
    .close          = on2avc_decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
