/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd.
 * Author        2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "libavutil/ffversion.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "dirac.h"
#include "put_bits.h"
#include "internal.h"

#include "vc2enc_dwt.h"
#include "diractab.h"

/* Quantizations above this usually zero coefficients and lower the quality */
#define MAX_QUANT_INDEX 100

#define COEF_LUT_TAB 2048

enum VC2_QM {
    VC2_QM_DEF = 0,
    VC2_QM_COL,
    VC2_QM_FLAT,

    VC2_QM_NB
};

typedef struct SubBand {
    dwtcoef *buf;
    ptrdiff_t stride;
    int width;
    int height;
} SubBand;

typedef struct Plane {
    SubBand band[MAX_DWT_LEVELS][4];
    dwtcoef *coef_buf;
    int width;
    int height;
    int dwt_width;
    int dwt_height;
    ptrdiff_t coef_stride;
} Plane;

typedef struct SliceArgs {
    PutBitContext pb;
    void *ctx;
    int x;
    int y;
    int quant_idx;
    int bits_ceil;
    int bytes;
} SliceArgs;

typedef struct TransformArgs {
    void *ctx;
    Plane *plane;
    void *idata;
    ptrdiff_t istride;
    int field;
    VC2TransformContext t;
} TransformArgs;

typedef struct VC2EncContext {
    AVClass *av_class;
    PutBitContext pb;
    Plane plane[3];
    AVCodecContext *avctx;
    DiracVersionInfo ver;

    SliceArgs *slice_args;
    TransformArgs transform_args[3];

    /* For conversion from unsigned pixel values to signed */
    int diff_offset;
    int bpp;

    /* Picture number */
    uint32_t picture_number;

    /* Base video format */
    int base_vf;
    int level;
    int profile;

    /* Quantization matrix */
    uint8_t quant[MAX_DWT_LEVELS][4];

    /* Coefficient LUT */
    uint32_t *coef_lut_val;
    uint8_t  *coef_lut_len;

    int num_x; /* #slices horizontally */
    int num_y; /* #slices vertically */
    int prefix_bytes;
    int size_scaler;
    int chroma_x_shift;
    int chroma_y_shift;

    /* Rate control stuff */
    int slice_max_bytes;
    int q_ceil;
    int q_start;

    /* Options */
    double tolerance;
    int wavelet_idx;
    int wavelet_depth;
    int strict_compliance;
    int slice_height;
    int slice_width;
    int interlaced;
    enum VC2_QM quant_matrix;

    /* Parse code state */
    uint32_t next_parse_offset;
    enum DiracParseCodes last_parse_code;
} VC2EncContext;

static av_always_inline void put_padding(PutBitContext *pb, int bytes)
{
    int bits = bytes*8;
    if (!bits)
        return;
    while (bits > 31) {
        put_bits(pb, 31, 0);
        bits -= 31;
    }
    if (bits)
        put_bits(pb, bits, 0);
}

static av_always_inline void put_vc2_ue_uint(PutBitContext *pb, uint32_t val)
{
    int i;
    int pbits = 0, bits = 0, topbit = 1, maxval = 1;

    if (!val++) {
        put_bits(pb, 1, 1);
        return;
    }

    while (val > maxval) {
        topbit <<= 1;
        maxval <<= 1;
        maxval |=  1;
    }

    bits = ff_log2(topbit);

    for (i = 0; i < bits; i++) {
        topbit >>= 1;
        pbits <<= 2;
        if (val & topbit)
            pbits |= 0x1;
    }

    put_bits(pb, bits*2 + 1, (pbits << 1) | 1);
}

static av_always_inline int count_vc2_ue_uint(uint16_t val)
{
    int topbit = 1, maxval = 1;

    if (!val++)
        return 1;

    while (val > maxval) {
        topbit <<= 1;
        maxval <<= 1;
        maxval |=  1;
    }

    return ff_log2(topbit)*2 + 1;
}

static av_always_inline void get_vc2_ue_uint(uint16_t val, uint8_t *nbits,
                                               uint32_t *eval)
{
    int i;
    int pbits = 0, bits = 0, topbit = 1, maxval = 1;

    if (!val++) {
        *nbits = 1;
        *eval = 1;
        return;
    }

    while (val > maxval) {
        topbit <<= 1;
        maxval <<= 1;
        maxval |=  1;
    }

    bits = ff_log2(topbit);

    for (i = 0; i < bits; i++) {
        topbit >>= 1;
        pbits <<= 2;
        if (val & topbit)
            pbits |= 0x1;
    }

    *nbits = bits*2 + 1;
    *eval = (pbits << 1) | 1;
}

/* VC-2 10.4 - parse_info() */
static void encode_parse_info(VC2EncContext *s, enum DiracParseCodes pcode)
{
    uint32_t cur_pos, dist;

    avpriv_align_put_bits(&s->pb);

    cur_pos = put_bits_count(&s->pb) >> 3;

    /* Magic string */
    avpriv_put_string(&s->pb, "BBCD", 0);

    /* Parse code */
    put_bits(&s->pb, 8, pcode);

    /* Next parse offset */
    dist = cur_pos - s->next_parse_offset;
    AV_WB32(s->pb.buf + s->next_parse_offset + 5, dist);
    s->next_parse_offset = cur_pos;
    put_bits32(&s->pb, pcode == DIRAC_PCODE_END_SEQ ? 13 : 0);

    /* Last parse offset */
    put_bits32(&s->pb, s->last_parse_code == DIRAC_PCODE_END_SEQ ? 13 : dist);

    s->last_parse_code = pcode;
}

/* VC-2 11.1 - parse_parameters()
 * The level dictates what the decoder should expect in terms of resolution
 * and allows it to quickly reject whatever it can't support. Remember,
 * this codec kinda targets cheapo FPGAs without much memory. Unfortunately
 * it also limits us greatly in our choice of formats, hence the flag to disable
 * strict_compliance */
static void encode_parse_params(VC2EncContext *s)
{
    put_vc2_ue_uint(&s->pb, s->ver.major); /* VC-2 demands this to be 2 */
    put_vc2_ue_uint(&s->pb, s->ver.minor); /* ^^ and this to be 0       */
    put_vc2_ue_uint(&s->pb, s->profile);   /* 3 to signal HQ profile    */
    put_vc2_ue_uint(&s->pb, s->level);     /* 3 - 1080/720, 6 - 4K      */
}

/* VC-2 11.3 - frame_size() */
static void encode_frame_size(VC2EncContext *s)
{
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance) {
        AVCodecContext *avctx = s->avctx;
        put_vc2_ue_uint(&s->pb, avctx->width);
        put_vc2_ue_uint(&s->pb, avctx->height);
    }
}

/* VC-2 11.3.3 - color_diff_sampling_format() */
static void encode_sample_fmt(VC2EncContext *s)
{
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance) {
        int idx;
        if (s->chroma_x_shift == 1 && s->chroma_y_shift == 0)
            idx = 1; /* 422 */
        else if (s->chroma_x_shift == 1 && s->chroma_y_shift == 1)
            idx = 2; /* 420 */
        else
            idx = 0; /* 444 */
        put_vc2_ue_uint(&s->pb, idx);
    }
}

/* VC-2 11.3.4 - scan_format() */
static void encode_scan_format(VC2EncContext *s)
{
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance)
        put_vc2_ue_uint(&s->pb, s->interlaced);
}

/* VC-2 11.3.5 - frame_rate() */
static void encode_frame_rate(VC2EncContext *s)
{
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance) {
        AVCodecContext *avctx = s->avctx;
        put_vc2_ue_uint(&s->pb, 0);
        put_vc2_ue_uint(&s->pb, avctx->time_base.den);
        put_vc2_ue_uint(&s->pb, avctx->time_base.num);
    }
}

/* VC-2 11.3.6 - aspect_ratio() */
static void encode_aspect_ratio(VC2EncContext *s)
{
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance) {
        AVCodecContext *avctx = s->avctx;
        put_vc2_ue_uint(&s->pb, 0);
        put_vc2_ue_uint(&s->pb, avctx->sample_aspect_ratio.num);
        put_vc2_ue_uint(&s->pb, avctx->sample_aspect_ratio.den);
    }
}

/* VC-2 11.3.7 - clean_area() */
static void encode_clean_area(VC2EncContext *s)
{
    put_bits(&s->pb, 1, 0);
}

/* VC-2 11.3.8 - signal_range() */
static void encode_signal_range(VC2EncContext *s)
{
    int idx;
    AVCodecContext *avctx = s->avctx;
    const AVPixFmtDescriptor *fmt = av_pix_fmt_desc_get(avctx->pix_fmt);
    const int depth = fmt->comp[0].depth;
    if (depth == 8 && avctx->color_range == AVCOL_RANGE_JPEG) {
        idx = 1;
        s->bpp = 1;
        s->diff_offset = 128;
    } else if (depth == 8 && (avctx->color_range == AVCOL_RANGE_MPEG ||
               avctx->color_range == AVCOL_RANGE_UNSPECIFIED)) {
        idx = 2;
        s->bpp = 1;
        s->diff_offset = 128;
    } else if (depth == 10) {
        idx = 3;
        s->bpp = 2;
        s->diff_offset = 512;
    } else {
        idx = 4;
        s->bpp = 2;
        s->diff_offset = 2048;
    }
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance)
        put_vc2_ue_uint(&s->pb, idx);
}

/* VC-2 11.3.9 - color_spec() */
static void encode_color_spec(VC2EncContext *s)
{
    AVCodecContext *avctx = s->avctx;
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance) {
        int val;
        put_vc2_ue_uint(&s->pb, 0);

        /* primaries */
        put_bits(&s->pb, 1, 1);
        if (avctx->color_primaries == AVCOL_PRI_BT470BG)
            val = 2;
        else if (avctx->color_primaries == AVCOL_PRI_SMPTE170M)
            val = 1;
        else if (avctx->color_primaries == AVCOL_PRI_SMPTE240M)
            val = 1;
        else
            val = 0;
        put_vc2_ue_uint(&s->pb, val);

        /* color matrix */
        put_bits(&s->pb, 1, 1);
        if (avctx->colorspace == AVCOL_SPC_RGB)
            val = 3;
        else if (avctx->colorspace == AVCOL_SPC_YCOCG)
            val = 2;
        else if (avctx->colorspace == AVCOL_SPC_BT470BG)
            val = 1;
        else
            val = 0;
        put_vc2_ue_uint(&s->pb, val);

        /* transfer function */
        put_bits(&s->pb, 1, 1);
        if (avctx->color_trc == AVCOL_TRC_LINEAR)
            val = 2;
        else if (avctx->color_trc == AVCOL_TRC_BT1361_ECG)
            val = 1;
        else
            val = 0;
        put_vc2_ue_uint(&s->pb, val);
    }
}

/* VC-2 11.3 - source_parameters() */
static void encode_source_params(VC2EncContext *s)
{
    encode_frame_size(s);
    encode_sample_fmt(s);
    encode_scan_format(s);
    encode_frame_rate(s);
    encode_aspect_ratio(s);
    encode_clean_area(s);
    encode_signal_range(s);
    encode_color_spec(s);
}

/* VC-2 11 - sequence_header() */
static void encode_seq_header(VC2EncContext *s)
{
    avpriv_align_put_bits(&s->pb);
    encode_parse_params(s);
    put_vc2_ue_uint(&s->pb, s->base_vf);
    encode_source_params(s);
    put_vc2_ue_uint(&s->pb, s->interlaced); /* Frames or fields coding */
}

/* VC-2 12.1 - picture_header() */
static void encode_picture_header(VC2EncContext *s)
{
    avpriv_align_put_bits(&s->pb);
    put_bits32(&s->pb, s->picture_number++);
}

/* VC-2 12.3.4.1 - slice_parameters() */
static void encode_slice_params(VC2EncContext *s)
{
    put_vc2_ue_uint(&s->pb, s->num_x);
    put_vc2_ue_uint(&s->pb, s->num_y);
    put_vc2_ue_uint(&s->pb, s->prefix_bytes);
    put_vc2_ue_uint(&s->pb, s->size_scaler);
}

/* 1st idx = LL, second - vertical, third - horizontal, fourth - total */
const uint8_t vc2_qm_col_tab[][4] = {
    {20,  9, 15,  4},
    { 0,  6,  6,  4},
    { 0,  3,  3,  5},
    { 0,  3,  5,  1},
    { 0, 11, 10, 11}
};

const uint8_t vc2_qm_flat_tab[][4] = {
    { 0,  0,  0,  0},
    { 0,  0,  0,  0},
    { 0,  0,  0,  0},
    { 0,  0,  0,  0},
    { 0,  0,  0,  0}
};

static void init_custom_qm(VC2EncContext *s)
{
    int level, orientation;

    if (s->quant_matrix == VC2_QM_DEF) {
        for (level = 0; level < s->wavelet_depth; level++) {
            for (orientation = 0; orientation < 4; orientation++) {
                if (level <= 3)
                    s->quant[level][orientation] = ff_dirac_default_qmat[s->wavelet_idx][level][orientation];
                else
                    s->quant[level][orientation] = vc2_qm_col_tab[level][orientation];
            }
        }
    } else if (s->quant_matrix == VC2_QM_COL) {
        for (level = 0; level < s->wavelet_depth; level++) {
            for (orientation = 0; orientation < 4; orientation++) {
                s->quant[level][orientation] = vc2_qm_col_tab[level][orientation];
            }
        }
    } else {
        for (level = 0; level < s->wavelet_depth; level++) {
            for (orientation = 0; orientation < 4; orientation++) {
                s->quant[level][orientation] = vc2_qm_flat_tab[level][orientation];
            }
        }
    }
}

/* VC-2 12.3.4.2 - quant_matrix() */
static void encode_quant_matrix(VC2EncContext *s)
{
    int level, custom_quant_matrix = 0;
    if (s->wavelet_depth > 4 || s->quant_matrix != VC2_QM_DEF)
        custom_quant_matrix = 1;
    put_bits(&s->pb, 1, custom_quant_matrix);
    if (custom_quant_matrix) {
        init_custom_qm(s);
        put_vc2_ue_uint(&s->pb, s->quant[0][0]);
        for (level = 0; level < s->wavelet_depth; level++) {
            put_vc2_ue_uint(&s->pb, s->quant[level][1]);
            put_vc2_ue_uint(&s->pb, s->quant[level][2]);
            put_vc2_ue_uint(&s->pb, s->quant[level][3]);
        }
    } else {
        for (level = 0; level < s->wavelet_depth; level++) {
            s->quant[level][0] = ff_dirac_default_qmat[s->wavelet_idx][level][0];
            s->quant[level][1] = ff_dirac_default_qmat[s->wavelet_idx][level][1];
            s->quant[level][2] = ff_dirac_default_qmat[s->wavelet_idx][level][2];
            s->quant[level][3] = ff_dirac_default_qmat[s->wavelet_idx][level][3];
        }
    }
}

/* VC-2 12.3 - transform_parameters() */
static void encode_transform_params(VC2EncContext *s)
{
    put_vc2_ue_uint(&s->pb, s->wavelet_idx);
    put_vc2_ue_uint(&s->pb, s->wavelet_depth);

    encode_slice_params(s);
    encode_quant_matrix(s);
}

/* VC-2 12.2 - wavelet_transform() */
static void encode_wavelet_transform(VC2EncContext *s)
{
    encode_transform_params(s);
    avpriv_align_put_bits(&s->pb);
    /* Continued after DWT in encode_transform_data() */
}

/* VC-2 12 - picture_parse() */
static void encode_picture_start(VC2EncContext *s)
{
    avpriv_align_put_bits(&s->pb);
    encode_picture_header(s);
    avpriv_align_put_bits(&s->pb);
    encode_wavelet_transform(s);
}

#define QUANT(c)  \
    c <<= 2;      \
    c /= qfactor; \

static av_always_inline void coeff_quantize_get(qcoef coeff, int qfactor,
                                                uint8_t *len, uint32_t *eval)
{
    QUANT(coeff)
    get_vc2_ue_uint(abs(coeff), len, eval);
    if (coeff) {
        *eval = (*eval << 1) | (coeff < 0);
        *len += 1;
    }
}

static av_always_inline void coeff_quantize_encode(PutBitContext *pb, qcoef coeff,
                                                   int qfactor)
{
    QUANT(coeff)
    put_vc2_ue_uint(pb, abs(coeff));
    if (coeff)
        put_bits(pb, 1, coeff < 0);
}

/* VC-2 13.5.5.2 - slice_band() */
static void encode_subband(VC2EncContext *s, PutBitContext *pb, int sx, int sy,
                           SubBand *b, int quant)
{
    int x, y;

    int left   = b->width  * (sx+0) / s->num_x;
    int right  = b->width  * (sx+1) / s->num_x;
    int top    = b->height * (sy+0) / s->num_y;
    int bottom = b->height * (sy+1) / s->num_y;

    int qfactor = ff_dirac_qscale_tab[quant];
    uint8_t  *len_lut = &s->coef_lut_len[2*quant*COEF_LUT_TAB + COEF_LUT_TAB];
    uint32_t *val_lut = &s->coef_lut_val[2*quant*COEF_LUT_TAB + COEF_LUT_TAB];

    dwtcoef *coeff = b->buf + top * b->stride;

    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            if (coeff[x] >= -COEF_LUT_TAB && coeff[x] < COEF_LUT_TAB)
                put_bits(pb, len_lut[coeff[x]], val_lut[coeff[x]]);
            else
                coeff_quantize_encode(pb, coeff[x], qfactor);
        }
        coeff += b->stride;
    }
}

static int count_hq_slice(VC2EncContext *s, int slice_x,
                          int slice_y, int quant_idx)
{
    int x, y, left, right, top, bottom, qfactor;
    uint8_t quants[MAX_DWT_LEVELS][4];
    int bits = 0, p, level, orientation;

    bits += 8*s->prefix_bytes;
    bits += 8; /* quant_idx */

    for (level = 0; level < s->wavelet_depth; level++)
        for (orientation = !!level; orientation < 4; orientation++)
            quants[level][orientation] = FFMAX(quant_idx - s->quant[level][orientation], 0);

    for (p = 0; p < 3; p++) {
        int bytes_start, bytes_len, pad_s, pad_c;
        bytes_start = bits >> 3;
        bits += 8;
        for (level = 0; level < s->wavelet_depth; level++) {
            for (orientation = !!level; orientation < 4; orientation++) {
                dwtcoef *buf;
                SubBand *b = &s->plane[p].band[level][orientation];

                quant_idx = quants[level][orientation];
                qfactor = ff_dirac_qscale_tab[quant_idx];

                left   = b->width  * slice_x    / s->num_x;
                right  = b->width  *(slice_x+1) / s->num_x;
                top    = b->height * slice_y    / s->num_y;
                bottom = b->height *(slice_y+1) / s->num_y;

                buf = b->buf + top * b->stride;

                for (y = top; y < bottom; y++) {
                    for (x = left; x < right; x++) {
                        qcoef coeff = (qcoef)buf[x];
                        if (coeff >= -COEF_LUT_TAB && coeff < COEF_LUT_TAB) {
                            bits += s->coef_lut_len[2*quant_idx*COEF_LUT_TAB + coeff + COEF_LUT_TAB];
                        } else {
                            QUANT(coeff)
                            bits += count_vc2_ue_uint(abs(coeff));
                            bits += !!coeff;
                        }
                    }
                    buf += b->stride;
                }
            }
        }
        bits += FFALIGN(bits, 8) - bits;
        bytes_len = (bits >> 3) - bytes_start - 1;
        pad_s = FFALIGN(bytes_len, s->size_scaler)/s->size_scaler;
        pad_c = (pad_s*s->size_scaler) - bytes_len;
        bits += pad_c*8;
    }

    return bits;
}

/* Approaches the best possible quantizer asymptotically, its kinda exaustive
 * but we have a LUT to get the coefficient size in bits. Guaranteed to never
 * overshoot, which is apparently very important when streaming */
static int rate_control(AVCodecContext *avctx, void *arg)
{
    SliceArgs *slice_dat = arg;
    VC2EncContext *s = slice_dat->ctx;
    const int sx = slice_dat->x;
    const int sy = slice_dat->y;
    int bits_last = INT_MAX, quant_buf[2] = {-1, -1};
    int quant = s->q_start, range = s->q_start/3;
    const int64_t top = slice_dat->bits_ceil;
    const double percent = s->tolerance;
    const double bottom = top - top*(percent/100.0f);
    int bits = count_hq_slice(s, sx, sy, quant);
    range -= range & 1; /* Make it an even number */
    while ((bits > top) || (bits < bottom)) {
        range *= bits > top ? +1 : -1;
        quant = av_clip(quant + range, 0, s->q_ceil);
        bits = count_hq_slice(s, sx, sy, quant);
        range = av_clip(range/2, 1, s->q_ceil);
        if (quant_buf[1] == quant) {
            quant = bits_last < bits ? quant_buf[0] : quant;
            bits  = bits_last < bits ? bits_last : bits;
            break;
        }
        quant_buf[1] = quant_buf[0];
        quant_buf[0] = quant;
        bits_last = bits;
    }
    slice_dat->quant_idx = av_clip(quant, 0, s->q_ceil);
    slice_dat->bytes = FFALIGN((bits >> 3), s->size_scaler) + 4 + s->prefix_bytes;

    return 0;
}

static void calc_slice_sizes(VC2EncContext *s)
{
    int slice_x, slice_y;
    SliceArgs *enc_args = s->slice_args;

    for (slice_y = 0; slice_y < s->num_y; slice_y++) {
        for (slice_x = 0; slice_x < s->num_x; slice_x++) {
            SliceArgs *args = &enc_args[s->num_x*slice_y + slice_x];
            args->ctx = s;
            args->x = slice_x;
            args->y = slice_y;
            args->bits_ceil = s->slice_max_bytes << 3;
        }
    }

    /* Determine quantization indices and bytes per slice */
    s->avctx->execute(s->avctx, rate_control, enc_args, NULL, s->num_x*s->num_y,
                      sizeof(SliceArgs));
}

/* VC-2 13.5.3 - hq_slice */
static int encode_hq_slice(AVCodecContext *avctx, void *arg)
{
    SliceArgs *slice_dat = arg;
    VC2EncContext *s = slice_dat->ctx;
    PutBitContext *pb = &slice_dat->pb;
    const int slice_x = slice_dat->x;
    const int slice_y = slice_dat->y;
    const int quant_idx = slice_dat->quant_idx;
    const int slice_bytes_max = slice_dat->bytes;
    uint8_t quants[MAX_DWT_LEVELS][4];
    int p, level, orientation;

    avpriv_align_put_bits(pb);
    put_padding(pb, s->prefix_bytes);
    put_bits(pb, 8, quant_idx);

    /* Slice quantization (slice_quantizers() in the specs) */
    for (level = 0; level < s->wavelet_depth; level++)
        for (orientation = !!level; orientation < 4; orientation++)
            quants[level][orientation] = FFMAX(quant_idx - s->quant[level][orientation], 0);

    /* Luma + 2 Chroma planes */
    for (p = 0; p < 3; p++) {
        int bytes_start, bytes_len, pad_s, pad_c;
        bytes_start = put_bits_count(pb) >> 3;
        put_bits(pb, 8, 0);
        for (level = 0; level < s->wavelet_depth; level++) {
            for (orientation = !!level; orientation < 4; orientation++) {
                encode_subband(s, pb, slice_x, slice_y,
                               &s->plane[p].band[level][orientation],
                               quants[level][orientation]);
            }
        }
        avpriv_align_put_bits(pb);
        bytes_len = (put_bits_count(pb) >> 3) - bytes_start - 1;
        if (p == 2) {
            int len_diff = slice_bytes_max - (put_bits_count(pb) >> 3);
            pad_s = FFALIGN((bytes_len + len_diff), s->size_scaler)/s->size_scaler;
            pad_c = (pad_s*s->size_scaler) - bytes_len;
        } else {
            pad_s = FFALIGN(bytes_len, s->size_scaler)/s->size_scaler;
            pad_c = (pad_s*s->size_scaler) - bytes_len;
        }
        pb->buf[bytes_start] = pad_s;
        put_padding(pb, pad_c);
    }

    return 0;
}

/* VC-2 13.5.1 - low_delay_transform_data() */
static int encode_slices(VC2EncContext *s)
{
    uint8_t *buf;
    int slice_x, slice_y, skip = 0;
    SliceArgs *enc_args = s->slice_args;

    avpriv_align_put_bits(&s->pb);
    flush_put_bits(&s->pb);
    buf = put_bits_ptr(&s->pb);

    for (slice_y = 0; slice_y < s->num_y; slice_y++) {
        for (slice_x = 0; slice_x < s->num_x; slice_x++) {
            SliceArgs *args = &enc_args[s->num_x*slice_y + slice_x];
            init_put_bits(&args->pb, buf + skip, args->bytes);
            s->q_start = (s->q_start + args->quant_idx)/2;
            skip += args->bytes;
        }
    }

    s->avctx->execute(s->avctx, encode_hq_slice, enc_args, NULL, s->num_x*s->num_y,
                      sizeof(SliceArgs));

    skip_put_bytes(&s->pb, skip);

    return 0;
}

/*
 * Transform basics for a 3 level transform
 * |---------------------------------------------------------------------|
 * |  LL-0  | HL-0  |                 |                                  |
 * |--------|-------|      HL-1       |                                  |
 * |  LH-0  | HH-0  |                 |                                  |
 * |----------------|-----------------|              HL-2                |
 * |                |                 |                                  |
 * |     LH-1       |      HH-1       |                                  |
 * |                |                 |                                  |
 * |----------------------------------|----------------------------------|
 * |                                  |                                  |
 * |                                  |                                  |
 * |                                  |                                  |
 * |              LH-2                |              HH-2                |
 * |                                  |                                  |
 * |                                  |                                  |
 * |                                  |                                  |
 * |---------------------------------------------------------------------|
 *
 * DWT transforms are generally applied by splitting the image in two vertically
 * and applying a low pass transform on the left part and a corresponding high
 * pass transform on the right hand side. This is known as the horizontal filter
 * stage.
 * After that, the same operation is performed except the image is divided
 * horizontally, with the high pass on the lower and the low pass on the higher
 * side.
 * Therefore, you're left with 4 subdivisions - known as  low-low, low-high,
 * high-low and high-high. They're referred to as orientations in the decoder
 * and encoder.
 *
 * The LL (low-low) area contains the original image downsampled by the amount
 * of levels. The rest of the areas can be thought as the details needed
 * to restore the image perfectly to its original size.
 */


static int dwt_plane(AVCodecContext *avctx, void *arg)
{
    TransformArgs *transform_dat = arg;
    VC2EncContext *s = transform_dat->ctx;
    const void *frame_data = transform_dat->idata;
    const ptrdiff_t linesize = transform_dat->istride;
    const int field = transform_dat->field;
    const Plane *p = transform_dat->plane;
    VC2TransformContext *t = &transform_dat->t;
    dwtcoef *buf = p->coef_buf;
    const int idx = s->wavelet_idx;
    const int skip = 1 + s->interlaced;

    int x, y, level, offset;
    ptrdiff_t pix_stride = linesize >> (s->bpp - 1);

    if (field == 1) {
        offset = 0;
        pix_stride <<= 1;
    } else if (field == 2) {
        offset = pix_stride;
        pix_stride <<= 1;
    } else {
        offset = 0;
    }

    if (s->bpp == 1) {
        const uint8_t *pix = (const uint8_t *)frame_data + offset;
        for (y = 0; y < p->height*skip; y+=skip) {
            for (x = 0; x < p->width; x++) {
                buf[x] = pix[x] - s->diff_offset;
            }
            buf += p->coef_stride;
            pix += pix_stride;
        }
    } else {
        const uint16_t *pix = (const uint16_t *)frame_data + offset;
        for (y = 0; y < p->height*skip; y+=skip) {
            for (x = 0; x < p->width; x++) {
                buf[x] = pix[x] - s->diff_offset;
            }
            buf += p->coef_stride;
            pix += pix_stride;
        }
    }

    memset(buf, 0, p->coef_stride * (p->dwt_height - p->height) * sizeof(dwtcoef));

    for (level = s->wavelet_depth-1; level >= 0; level--) {
        const SubBand *b = &p->band[level][0];
        t->vc2_subband_dwt[idx](t, p->coef_buf, p->coef_stride,
                                b->width, b->height);
    }

    return 0;
}

static void encode_frame(VC2EncContext *s, const AVFrame *frame,
                         const char *aux_data, int field)
{
    int i;

    /* Sequence header */
    encode_parse_info(s, DIRAC_PCODE_SEQ_HEADER);
    encode_seq_header(s);

    /* Encoder version */
    if (aux_data) {
        encode_parse_info(s, DIRAC_PCODE_AUX);
        avpriv_put_string(&s->pb, aux_data, 1);
    }

    /* Picture header */
    encode_parse_info(s, DIRAC_PCODE_PICTURE_HQ);
    encode_picture_start(s);

    for (i = 0; i < 3; i++) {
        s->transform_args[i].ctx   = s;
        s->transform_args[i].field = field;
        s->transform_args[i].plane = &s->plane[i];
        s->transform_args[i].idata = frame->data[i];
        s->transform_args[i].istride = frame->linesize[i];
    }

    /* Do a DWT transform */
    s->avctx->execute(s->avctx, dwt_plane, s->transform_args, NULL, 3,
                      sizeof(TransformArgs));

    /* Calculate per-slice quantizers and sizes */
    calc_slice_sizes(s);

    /* Init planes and encode slices */
    encode_slices(s);

    /* End sequence */
    encode_parse_info(s, DIRAC_PCODE_END_SEQ);
}

static av_cold int vc2_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                      const AVFrame *frame, int *got_packet_ptr)
{
    int ret;
    int max_frame_bytes, sig_size = 256;
    VC2EncContext *s = avctx->priv_data;
    const char aux_data[] = "FFmpeg version "FFMPEG_VERSION;
    const int aux_data_size = sizeof(aux_data);
    const int header_size = 100 + aux_data_size;
    int64_t r_bitrate = avctx->bit_rate >> (s->interlaced);

    s->avctx = avctx;
    s->size_scaler = 1;
    s->prefix_bytes = 0;
    s->last_parse_code = 0;
    s->next_parse_offset = 0;

    /* Rate control */
    max_frame_bytes = (av_rescale(r_bitrate, s->avctx->time_base.num,
                                  s->avctx->time_base.den) >> 3) - header_size;

    /* Find an appropriate size scaler */
    while (sig_size > 255) {
        s->slice_max_bytes = FFALIGN(av_rescale(max_frame_bytes, 1,
                                     s->num_x*s->num_y), s->size_scaler);
        s->slice_max_bytes += 4 + s->prefix_bytes;
        sig_size = s->slice_max_bytes/s->size_scaler; /* Signalled slize size */
        s->size_scaler <<= 1;
    }

    ret = ff_alloc_packet2(avctx, avpkt, max_frame_bytes*2, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    } else {
        init_put_bits(&s->pb, avpkt->data, avpkt->size);
    }

    encode_frame(s, frame, aux_data, s->interlaced);
    if (s->interlaced)
        encode_frame(s, frame, NULL, 2);

    flush_put_bits(&s->pb);
    avpkt->size = put_bits_count(&s->pb) >> 3;

    *got_packet_ptr = 1;

    return 0;
}

static av_cold int vc2_encode_end(AVCodecContext *avctx)
{
    int i;
    VC2EncContext *s = avctx->priv_data;

    for (i = 0; i < 3; i++) {
        ff_vc2enc_free_transforms(&s->transform_args[i].t);
        av_freep(&s->plane[i].coef_buf);
    }

    av_freep(&s->slice_args);
    av_freep(&s->coef_lut_len);
    av_freep(&s->coef_lut_val);

    return 0;
}


static av_cold int vc2_encode_init(AVCodecContext *avctx)
{
    Plane *p;
    SubBand *b;
    int i, j, level, o, shift;
    VC2EncContext *s = avctx->priv_data;

    s->picture_number = 0;

    /* Total allowed quantization range */
    s->q_ceil    = MAX_QUANT_INDEX;

    s->ver.major = 2;
    s->ver.minor = 0;
    s->profile   = 3;
    s->level     = 3;

    s->base_vf   = -1;
    s->strict_compliance = 1;

    /* Mark unknown as progressive */
    s->interlaced = !((avctx->field_order == AV_FIELD_UNKNOWN) ||
                      (avctx->field_order == AV_FIELD_PROGRESSIVE));

    if (avctx->pix_fmt == AV_PIX_FMT_YUV422P10) {
        if (avctx->width == 1280 && avctx->height == 720) {
            s->level = 3;
            if (avctx->time_base.num == 1001 && avctx->time_base.den == 60000)
                s->base_vf = 9;
            if (avctx->time_base.num == 1 && avctx->time_base.den == 50)
                s->base_vf = 10;
        } else if (avctx->width == 1920 && avctx->height == 1080) {
            s->level = 3;
            if (s->interlaced) {
                if (avctx->time_base.num == 1001 && avctx->time_base.den == 30000)
                    s->base_vf = 11;
                if (avctx->time_base.num == 1 && avctx->time_base.den == 50)
                    s->base_vf = 12;
            } else {
                if (avctx->time_base.num == 1001 && avctx->time_base.den == 60000)
                    s->base_vf = 13;
                if (avctx->time_base.num == 1 && avctx->time_base.den == 50)
                    s->base_vf = 14;
                if (avctx->time_base.num == 1001 && avctx->time_base.den == 24000)
                    s->base_vf = 21;
            }
        } else if (avctx->width == 3840 && avctx->height == 2160) {
            s->level = 6;
            if (avctx->time_base.num == 1001 && avctx->time_base.den == 60000)
                s->base_vf = 17;
            if (avctx->time_base.num == 1 && avctx->time_base.den == 50)
                s->base_vf = 18;
        }
    }

    if (s->interlaced && s->base_vf <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Interlacing not supported with non standard formats!\n");
        return AVERROR_UNKNOWN;
    }

    if (s->interlaced)
        av_log(avctx, AV_LOG_WARNING, "Interlacing enabled!\n");

    if ((s->slice_width  & (s->slice_width  - 1)) ||
        (s->slice_height & (s->slice_height - 1))) {
        av_log(avctx, AV_LOG_ERROR, "Slice size is not a power of two!\n");
        return AVERROR_UNKNOWN;
    }

    if ((s->slice_width > avctx->width) ||
        (s->slice_height > avctx->height)) {
        av_log(avctx, AV_LOG_ERROR, "Slice size is bigger than the image!\n");
        return AVERROR_UNKNOWN;
    }

    if (s->base_vf <= 0) {
        if (avctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            s->strict_compliance = s->base_vf = 0;
            av_log(avctx, AV_LOG_WARNING, "Disabling strict compliance\n");
        } else {
            av_log(avctx, AV_LOG_WARNING, "Given format does not strictly comply with "
                   "the specifications, please add a -strict -1 flag to use it\n");
            return AVERROR_UNKNOWN;
        }
    } else {
        av_log(avctx, AV_LOG_INFO, "Selected base video format = %i\n", s->base_vf);
    }

    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);

    /* Planes initialization */
    for (i = 0; i < 3; i++) {
        int w, h;
        p = &s->plane[i];
        p->width      = avctx->width  >> (i ? s->chroma_x_shift : 0);
        p->height     = avctx->height >> (i ? s->chroma_y_shift : 0);
        if (s->interlaced)
            p->height >>= 1;
        p->dwt_width  = w = FFALIGN(p->width,  (1 << s->wavelet_depth));
        p->dwt_height = h = FFALIGN(p->height, (1 << s->wavelet_depth));
        p->coef_stride = FFALIGN(p->dwt_width, 32);
        p->coef_buf = av_malloc(p->coef_stride*p->dwt_height*sizeof(dwtcoef));
        if (!p->coef_buf)
            goto alloc_fail;
        for (level = s->wavelet_depth-1; level >= 0; level--) {
            w = w >> 1;
            h = h >> 1;
            for (o = 0; o < 4; o++) {
                b = &p->band[level][o];
                b->width  = w;
                b->height = h;
                b->stride = p->coef_stride;
                shift = (o > 1)*b->height*b->stride + (o & 1)*b->width;
                b->buf = p->coef_buf + shift;
            }
        }

        /* DWT init */
        if (ff_vc2enc_init_transforms(&s->transform_args[i].t,
                                        s->plane[0].coef_stride,
                                        s->plane[0].dwt_height))
            goto alloc_fail;
    }

    /* Slices */
    s->num_x = s->plane[0].dwt_width/s->slice_width;
    s->num_y = s->plane[0].dwt_height/s->slice_height;

    s->slice_args = av_malloc(s->num_x*s->num_y*sizeof(SliceArgs));
    if (!s->slice_args)
        goto alloc_fail;

    /* Lookup tables */
    s->coef_lut_len = av_malloc(2*COEF_LUT_TAB*s->q_ceil*sizeof(*s->coef_lut_len));
    if (!s->coef_lut_len)
        goto alloc_fail;

    s->coef_lut_val = av_malloc(2*COEF_LUT_TAB*s->q_ceil*sizeof(*s->coef_lut_val));
    if (!s->coef_lut_val)
        goto alloc_fail;

    for (i = 0; i < s->q_ceil; i++) {
        for (j = -COEF_LUT_TAB; j < COEF_LUT_TAB; j++) {
            uint8_t  *len_lut = &s->coef_lut_len[2*i*COEF_LUT_TAB + COEF_LUT_TAB];
            uint32_t *val_lut = &s->coef_lut_val[2*i*COEF_LUT_TAB + COEF_LUT_TAB];
            coeff_quantize_get(j, ff_dirac_qscale_tab[i], &len_lut[j], &val_lut[j]);
        }
    }

    return 0;

alloc_fail:
    vc2_encode_end(avctx);
    av_log(avctx, AV_LOG_ERROR, "Unable to allocate memory!\n");
    return AVERROR(ENOMEM);
}

#define VC2ENC_FLAGS (AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption vc2enc_options[] = {
    {"tolerance",     "Max undershoot in percent", offsetof(VC2EncContext, tolerance), AV_OPT_TYPE_DOUBLE, {.dbl = 10.0f}, 0.0f, 45.0f, VC2ENC_FLAGS, "tolerance"},
    {"slice_width",   "Slice width",  offsetof(VC2EncContext, slice_width), AV_OPT_TYPE_INT, {.i64 = 128}, 32, 1024, VC2ENC_FLAGS, "slice_width"},
    {"slice_height",  "Slice height", offsetof(VC2EncContext, slice_height), AV_OPT_TYPE_INT, {.i64 = 64}, 8, 1024, VC2ENC_FLAGS, "slice_height"},
    {"wavelet_depth", "Transform depth", offsetof(VC2EncContext, wavelet_depth), AV_OPT_TYPE_INT, {.i64 = 5}, 1, 5, VC2ENC_FLAGS, "wavelet_depth"},
    {"wavelet_type",  "Transform type",  offsetof(VC2EncContext, wavelet_idx), AV_OPT_TYPE_INT, {.i64 = VC2_TRANSFORM_9_7}, 0, VC2_TRANSFORMS_NB, VC2ENC_FLAGS, "wavelet_idx"},
        {"9_7",       "Deslauriers-Dubuc (9,7)", 0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_9_7}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
        {"5_3",       "LeGall (5,3)",            0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_5_3}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
    {"qm", "Custom quantization matrix", offsetof(VC2EncContext, quant_matrix), AV_OPT_TYPE_INT, {.i64 = VC2_QM_DEF}, 0, VC2_QM_NB, VC2ENC_FLAGS, "quant_matrix"},
        {"default",   "Default from the specifications", 0, AV_OPT_TYPE_CONST, {.i64 = VC2_QM_DEF}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "quant_matrix"},
        {"color",     "Prevents low bitrate discoloration", 0, AV_OPT_TYPE_CONST, {.i64 = VC2_QM_COL}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "quant_matrix"},
        {"flat",      "Optimize for PSNR", 0, AV_OPT_TYPE_CONST, {.i64 = VC2_QM_FLAT}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "quant_matrix"},
    {NULL}
};

static const AVClass vc2enc_class = {
    .class_name = "SMPTE VC-2 encoder",
    .category = AV_CLASS_CATEGORY_ENCODER,
    .option = vc2enc_options,
    .item_name = av_default_item_name,
    .version = LIBAVUTIL_VERSION_INT
};

static const AVCodecDefault vc2enc_defaults[] = {
    { "b",              "600000000"   },
    { NULL },
};

static const enum AVPixelFormat allowed_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,   AV_PIX_FMT_YUV422P,   AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

AVCodec ff_vc2_encoder = {
    .name = "vc2",
    .long_name = NULL_IF_CONFIG_SMALL("SMPTE VC-2"),
    .type = AVMEDIA_TYPE_VIDEO,
    .id = AV_CODEC_ID_DIRAC,
    .priv_data_size = sizeof(VC2EncContext),
    .init = vc2_encode_init,
    .close = vc2_encode_end,
    .capabilities = AV_CODEC_CAP_SLICE_THREADS,
    .encode2 = vc2_encode_frame,
    .priv_class = &vc2enc_class,
    .defaults = vc2enc_defaults,
    .pix_fmts = allowed_pix_fmts
};
