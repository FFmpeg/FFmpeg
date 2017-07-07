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

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "dirac.h"
#include "put_bits.h"
#include "internal.h"
#include "version.h"

#include "vc2enc_dwt.h"
#include "diractab.h"

/* Total range is -COEF_LUT_TAB to +COEFF_LUT_TAB, but total tab size is half
 * (COEF_LUT_TAB*DIRAC_MAX_QUANT_INDEX), as the sign is appended during encoding */
#define COEF_LUT_TAB 2048

/* The limited size resolution of each slice forces us to do this */
#define SSIZE_ROUND(b) (FFALIGN((b), s->size_scaler) + 4 + s->prefix_bytes)

/* Decides the cutoff point in # of slices to distribute the leftover bytes */
#define SLICE_REDIST_TOTAL 150

typedef struct VC2BaseVideoFormat {
    enum AVPixelFormat pix_fmt;
    AVRational time_base;
    int width, height, interlaced, level;
    const char *name;
} VC2BaseVideoFormat;

static const VC2BaseVideoFormat base_video_fmts[] = {
    { 0 }, /* Custom format, here just to make indexing equal to base_vf */
    { AV_PIX_FMT_YUV420P,   { 1001, 15000 },  176,  120, 0, 1,     "QSIF525" },
    { AV_PIX_FMT_YUV420P,   {    2,    25 },  176,  144, 0, 1,     "QCIF"    },
    { AV_PIX_FMT_YUV420P,   { 1001, 15000 },  352,  240, 0, 1,     "SIF525"  },
    { AV_PIX_FMT_YUV420P,   {    2,    25 },  352,  288, 0, 1,     "CIF"     },
    { AV_PIX_FMT_YUV420P,   { 1001, 15000 },  704,  480, 0, 1,     "4SIF525" },
    { AV_PIX_FMT_YUV420P,   {    2,    25 },  704,  576, 0, 1,     "4CIF"    },

    { AV_PIX_FMT_YUV422P10, { 1001, 30000 },  720,  480, 1, 2,   "SD480I-60" },
    { AV_PIX_FMT_YUV422P10, {    1,    25 },  720,  576, 1, 2,   "SD576I-50" },

    { AV_PIX_FMT_YUV422P10, { 1001, 60000 }, 1280,  720, 0, 3,  "HD720P-60"  },
    { AV_PIX_FMT_YUV422P10, {    1,    50 }, 1280,  720, 0, 3,  "HD720P-50"  },
    { AV_PIX_FMT_YUV422P10, { 1001, 30000 }, 1920, 1080, 1, 3,  "HD1080I-60" },
    { AV_PIX_FMT_YUV422P10, {    1,    25 }, 1920, 1080, 1, 3,  "HD1080I-50" },
    { AV_PIX_FMT_YUV422P10, { 1001, 60000 }, 1920, 1080, 0, 3,  "HD1080P-60" },
    { AV_PIX_FMT_YUV422P10, {    1,    50 }, 1920, 1080, 0, 3,  "HD1080P-50" },

    { AV_PIX_FMT_YUV444P12, {    1,    24 }, 2048, 1080, 0, 4,        "DC2K" },
    { AV_PIX_FMT_YUV444P12, {    1,    24 }, 4096, 2160, 0, 5,        "DC4K" },

    { AV_PIX_FMT_YUV422P10, { 1001, 60000 }, 3840, 2160, 0, 6, "UHDTV 4K-60" },
    { AV_PIX_FMT_YUV422P10, {    1,    50 }, 3840, 2160, 0, 6, "UHDTV 4K-50" },

    { AV_PIX_FMT_YUV422P10, { 1001, 60000 }, 7680, 4320, 0, 7, "UHDTV 8K-60" },
    { AV_PIX_FMT_YUV422P10, {    1,    50 }, 7680, 4320, 0, 7, "UHDTV 8K-50" },

    { AV_PIX_FMT_YUV422P10, { 1001, 24000 }, 1920, 1080, 0, 3,  "HD1080P-24" },
    { AV_PIX_FMT_YUV422P10, { 1001, 30000 },  720,  486, 1, 2,  "SD Pro486"  },
};
static const int base_video_fmts_len = FF_ARRAY_ELEMS(base_video_fmts);

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
    int cache[DIRAC_MAX_QUANT_INDEX];
    void *ctx;
    int x;
    int y;
    int quant_idx;
    int bits_ceil;
    int bits_floor;
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
    int bpp_idx;

    /* Picture number */
    uint32_t picture_number;

    /* Base video format */
    int base_vf;
    int level;
    int profile;

    /* Quantization matrix */
    uint8_t quant[MAX_DWT_LEVELS][4];
    int custom_quant_matrix;

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
    int slice_min_bytes;
    int q_ceil;
    int q_avg;

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

static av_always_inline int count_vc2_ue_uint(uint32_t val)
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

static av_always_inline void get_vc2_ue_uint(int val, uint8_t *nbits,
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
    put_bits(&s->pb, 1, !s->strict_compliance);
    if (!s->strict_compliance)
        put_vc2_ue_uint(&s->pb, s->bpp_idx);
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

static void init_quant_matrix(VC2EncContext *s)
{
    int level, orientation;

    if (s->wavelet_depth <= 4 && s->quant_matrix == VC2_QM_DEF) {
        s->custom_quant_matrix = 0;
        for (level = 0; level < s->wavelet_depth; level++) {
            s->quant[level][0] = ff_dirac_default_qmat[s->wavelet_idx][level][0];
            s->quant[level][1] = ff_dirac_default_qmat[s->wavelet_idx][level][1];
            s->quant[level][2] = ff_dirac_default_qmat[s->wavelet_idx][level][2];
            s->quant[level][3] = ff_dirac_default_qmat[s->wavelet_idx][level][3];
        }
        return;
    }

    s->custom_quant_matrix = 1;

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
    int level;
    put_bits(&s->pb, 1, s->custom_quant_matrix);
    if (s->custom_quant_matrix) {
        put_vc2_ue_uint(&s->pb, s->quant[0][0]);
        for (level = 0; level < s->wavelet_depth; level++) {
            put_vc2_ue_uint(&s->pb, s->quant[level][1]);
            put_vc2_ue_uint(&s->pb, s->quant[level][2]);
            put_vc2_ue_uint(&s->pb, s->quant[level][3]);
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
}

/* VC-2 12 - picture_parse() */
static void encode_picture_start(VC2EncContext *s)
{
    avpriv_align_put_bits(&s->pb);
    encode_picture_header(s);
    avpriv_align_put_bits(&s->pb);
    encode_wavelet_transform(s);
}

#define QUANT(c, qf) (((c) << 2)/(qf))

/* VC-2 13.5.5.2 - slice_band() */
static void encode_subband(VC2EncContext *s, PutBitContext *pb, int sx, int sy,
                           SubBand *b, int quant)
{
    int x, y;

    const int left   = b->width  * (sx+0) / s->num_x;
    const int right  = b->width  * (sx+1) / s->num_x;
    const int top    = b->height * (sy+0) / s->num_y;
    const int bottom = b->height * (sy+1) / s->num_y;

    const int qfactor = ff_dirac_qscale_tab[quant];
    const uint8_t  *len_lut = &s->coef_lut_len[quant*COEF_LUT_TAB];
    const uint32_t *val_lut = &s->coef_lut_val[quant*COEF_LUT_TAB];

    dwtcoef *coeff = b->buf + top * b->stride;

    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            const int neg = coeff[x] < 0;
            uint32_t c_abs = FFABS(coeff[x]);
            if (c_abs < COEF_LUT_TAB) {
                put_bits(pb, len_lut[c_abs], val_lut[c_abs] | neg);
            } else {
                c_abs = QUANT(c_abs, qfactor);
                put_vc2_ue_uint(pb, c_abs);
                if (c_abs)
                    put_bits(pb, 1, neg);
            }
        }
        coeff += b->stride;
    }
}

static int count_hq_slice(SliceArgs *slice, int quant_idx)
{
    int x, y;
    uint8_t quants[MAX_DWT_LEVELS][4];
    int bits = 0, p, level, orientation;
    VC2EncContext *s = slice->ctx;

    if (slice->cache[quant_idx])
        return slice->cache[quant_idx];

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
                SubBand *b = &s->plane[p].band[level][orientation];

                const int q_idx = quants[level][orientation];
                const uint8_t *len_lut = &s->coef_lut_len[q_idx*COEF_LUT_TAB];
                const int qfactor = ff_dirac_qscale_tab[q_idx];

                const int left   = b->width  * slice->x    / s->num_x;
                const int right  = b->width  *(slice->x+1) / s->num_x;
                const int top    = b->height * slice->y    / s->num_y;
                const int bottom = b->height *(slice->y+1) / s->num_y;

                dwtcoef *buf = b->buf + top * b->stride;

                for (y = top; y < bottom; y++) {
                    for (x = left; x < right; x++) {
                        uint32_t c_abs = FFABS(buf[x]);
                        if (c_abs < COEF_LUT_TAB) {
                            bits += len_lut[c_abs];
                        } else {
                            c_abs = QUANT(c_abs, qfactor);
                            bits += count_vc2_ue_uint(c_abs);
                            bits += !!c_abs;
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

    slice->cache[quant_idx] = bits;

    return bits;
}

/* Approaches the best possible quantizer asymptotically, its kinda exaustive
 * but we have a LUT to get the coefficient size in bits. Guaranteed to never
 * overshoot, which is apparently very important when streaming */
static int rate_control(AVCodecContext *avctx, void *arg)
{
    SliceArgs *slice_dat = arg;
    VC2EncContext *s = slice_dat->ctx;
    const int top = slice_dat->bits_ceil;
    const int bottom = slice_dat->bits_floor;
    int quant_buf[2] = {-1, -1};
    int quant = slice_dat->quant_idx, step = 1;
    int bits_last, bits = count_hq_slice(slice_dat, quant);
    while ((bits > top) || (bits < bottom)) {
        const int signed_step = bits > top ? +step : -step;
        quant  = av_clip(quant + signed_step, 0, s->q_ceil-1);
        bits   = count_hq_slice(slice_dat, quant);
        if (quant_buf[1] == quant) {
            quant = FFMAX(quant_buf[0], quant);
            bits  = quant == quant_buf[0] ? bits_last : bits;
            break;
        }
        step         = av_clip(step/2, 1, (s->q_ceil-1)/2);
        quant_buf[1] = quant_buf[0];
        quant_buf[0] = quant;
        bits_last    = bits;
    }
    slice_dat->quant_idx = av_clip(quant, 0, s->q_ceil-1);
    slice_dat->bytes = SSIZE_ROUND(bits >> 3);
    return 0;
}

static int calc_slice_sizes(VC2EncContext *s)
{
    int i, j, slice_x, slice_y, bytes_left = 0;
    int bytes_top[SLICE_REDIST_TOTAL] = {0};
    int64_t total_bytes_needed = 0;
    int slice_redist_range = FFMIN(SLICE_REDIST_TOTAL, s->num_x*s->num_y);
    SliceArgs *enc_args = s->slice_args;
    SliceArgs *top_loc[SLICE_REDIST_TOTAL] = {NULL};

    init_quant_matrix(s);

    for (slice_y = 0; slice_y < s->num_y; slice_y++) {
        for (slice_x = 0; slice_x < s->num_x; slice_x++) {
            SliceArgs *args = &enc_args[s->num_x*slice_y + slice_x];
            args->ctx = s;
            args->x   = slice_x;
            args->y   = slice_y;
            args->bits_ceil  = s->slice_max_bytes << 3;
            args->bits_floor = s->slice_min_bytes << 3;
            memset(args->cache, 0, s->q_ceil*sizeof(*args->cache));
        }
    }

    /* First pass - determine baseline slice sizes w.r.t. max_slice_size */
    s->avctx->execute(s->avctx, rate_control, enc_args, NULL, s->num_x*s->num_y,
                      sizeof(SliceArgs));

    for (i = 0; i < s->num_x*s->num_y; i++) {
        SliceArgs *args = &enc_args[i];
        bytes_left += s->slice_max_bytes - args->bytes;
        for (j = 0; j < slice_redist_range; j++) {
            if (args->bytes > bytes_top[j]) {
                bytes_top[j] = args->bytes;
                top_loc[j]   = args;
                break;
            }
        }
    }

    /* Second pass - distribute leftover bytes */
    while (1) {
        int distributed = 0;
        for (i = 0; i < slice_redist_range; i++) {
            SliceArgs *args;
            int bits, bytes, diff, prev_bytes, new_idx;
            if (bytes_left <= 0)
                break;
            if (!top_loc[i] || !top_loc[i]->quant_idx)
                break;
            args = top_loc[i];
            prev_bytes = args->bytes;
            new_idx = FFMAX(args->quant_idx - 1, 0);
            bits  = count_hq_slice(args, new_idx);
            bytes = SSIZE_ROUND(bits >> 3);
            diff  = bytes - prev_bytes;
            if ((bytes_left - diff) > 0) {
                args->quant_idx = new_idx;
                args->bytes = bytes;
                bytes_left -= diff;
                distributed++;
            }
        }
        if (!distributed)
            break;
    }

    for (i = 0; i < s->num_x*s->num_y; i++) {
        SliceArgs *args = &enc_args[i];
        total_bytes_needed += args->bytes;
        s->q_avg = (s->q_avg + args->quant_idx)/2;
    }

    return total_bytes_needed;
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

    /* The reference decoder ignores it, and its typical length is 0 */
    memset(put_bits_ptr(pb), 0, s->prefix_bytes);
    skip_put_bytes(pb, s->prefix_bytes);

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
        flush_put_bits(pb);
        /* vc2-reference uses that padding that decodes to '0' coeffs */
        memset(put_bits_ptr(pb), 0xFF, pad_c);
        skip_put_bytes(pb, pad_c);
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
            init_put_bits(&args->pb, buf + skip, args->bytes+s->prefix_bytes);
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

static int encode_frame(VC2EncContext *s, AVPacket *avpkt, const AVFrame *frame,
                        const char *aux_data, const int header_size, int field)
{
    int i, ret;
    int64_t max_frame_bytes;

     /* Threaded DWT transform */
    for (i = 0; i < 3; i++) {
        s->transform_args[i].ctx   = s;
        s->transform_args[i].field = field;
        s->transform_args[i].plane = &s->plane[i];
        s->transform_args[i].idata = frame->data[i];
        s->transform_args[i].istride = frame->linesize[i];
    }
    s->avctx->execute(s->avctx, dwt_plane, s->transform_args, NULL, 3,
                      sizeof(TransformArgs));

    /* Calculate per-slice quantizers and sizes */
    max_frame_bytes = header_size + calc_slice_sizes(s);

    if (field < 2) {
        ret = ff_alloc_packet2(s->avctx, avpkt,
                               max_frame_bytes << s->interlaced,
                               max_frame_bytes << s->interlaced);
        if (ret) {
            av_log(s->avctx, AV_LOG_ERROR, "Error getting output packet.\n");
            return ret;
        }
        init_put_bits(&s->pb, avpkt->data, avpkt->size);
    }

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

    /* Encode slices */
    encode_slices(s);

    /* End sequence */
    encode_parse_info(s, DIRAC_PCODE_END_SEQ);

    return 0;
}

static av_cold int vc2_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                      const AVFrame *frame, int *got_packet)
{
    int ret = 0;
    int sig_size = 256;
    VC2EncContext *s = avctx->priv_data;
    const int bitexact = avctx->flags & AV_CODEC_FLAG_BITEXACT;
    const char *aux_data = bitexact ? "Lavc" : LIBAVCODEC_IDENT;
    const int aux_data_size = bitexact ? sizeof("Lavc") : sizeof(LIBAVCODEC_IDENT);
    const int header_size = 100 + aux_data_size;
    int64_t max_frame_bytes, r_bitrate = avctx->bit_rate >> (s->interlaced);

    s->avctx = avctx;
    s->size_scaler = 2;
    s->prefix_bytes = 0;
    s->last_parse_code = 0;
    s->next_parse_offset = 0;

    /* Rate control */
    max_frame_bytes = (av_rescale(r_bitrate, s->avctx->time_base.num,
                                  s->avctx->time_base.den) >> 3) - header_size;
    s->slice_max_bytes = av_rescale(max_frame_bytes, 1, s->num_x*s->num_y);

    /* Find an appropriate size scaler */
    while (sig_size > 255) {
        int r_size = SSIZE_ROUND(s->slice_max_bytes);
        sig_size = r_size/s->size_scaler; /* Signalled slize size */
        s->size_scaler <<= 1;
    }

    s->slice_max_bytes = SSIZE_ROUND(s->slice_max_bytes);
    s->slice_min_bytes = s->slice_max_bytes - s->slice_max_bytes*(s->tolerance/100.0f);

    ret = encode_frame(s, avpkt, frame, aux_data, header_size, s->interlaced);
    if (ret)
        return ret;
    if (s->interlaced) {
        ret = encode_frame(s, avpkt, frame, aux_data, header_size, 2);
        if (ret)
            return ret;
    }

    flush_put_bits(&s->pb);
    avpkt->size = put_bits_count(&s->pb) >> 3;

    *got_packet = 1;

    return 0;
}

static av_cold int vc2_encode_end(AVCodecContext *avctx)
{
    int i;
    VC2EncContext *s = avctx->priv_data;

    av_log(avctx, AV_LOG_INFO, "Qavg: %i\n", s->q_avg);

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
    const AVPixFmtDescriptor *fmt = av_pix_fmt_desc_get(avctx->pix_fmt);
    const int depth = fmt->comp[0].depth;
    VC2EncContext *s = avctx->priv_data;

    s->picture_number = 0;

    /* Total allowed quantization range */
    s->q_ceil    = DIRAC_MAX_QUANT_INDEX;

    s->ver.major = 2;
    s->ver.minor = 0;
    s->profile   = 3;
    s->level     = 3;

    s->base_vf   = -1;
    s->strict_compliance = 1;

    s->q_avg = 0;
    s->slice_max_bytes = 0;
    s->slice_min_bytes = 0;

    /* Mark unknown as progressive */
    s->interlaced = !((avctx->field_order == AV_FIELD_UNKNOWN) ||
                      (avctx->field_order == AV_FIELD_PROGRESSIVE));

    for (i = 0; i < base_video_fmts_len; i++) {
        const VC2BaseVideoFormat *fmt = &base_video_fmts[i];
        if (avctx->pix_fmt != fmt->pix_fmt)
            continue;
        if (avctx->time_base.num != fmt->time_base.num)
            continue;
        if (avctx->time_base.den != fmt->time_base.den)
            continue;
        if (avctx->width != fmt->width)
            continue;
        if (avctx->height != fmt->height)
            continue;
        if (s->interlaced != fmt->interlaced)
            continue;
        s->base_vf = i;
        s->level   = base_video_fmts[i].level;
        break;
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
        if (avctx->strict_std_compliance < FF_COMPLIANCE_STRICT) {
            s->strict_compliance = s->base_vf = 0;
            av_log(avctx, AV_LOG_WARNING, "Format does not strictly comply with VC2 specs\n");
        } else {
            av_log(avctx, AV_LOG_WARNING, "Given format does not strictly comply with "
                   "the specifications, decrease strictness to use it.\n");
            return AVERROR_UNKNOWN;
        }
    } else {
        av_log(avctx, AV_LOG_INFO, "Selected base video format = %i (%s)\n",
               s->base_vf, base_video_fmts[s->base_vf].name);
    }

    /* Chroma subsampling */
    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);

    /* Bit depth and color range index */
    if (depth == 8 && avctx->color_range == AVCOL_RANGE_JPEG) {
        s->bpp = 1;
        s->bpp_idx = 1;
        s->diff_offset = 128;
    } else if (depth == 8 && (avctx->color_range == AVCOL_RANGE_MPEG ||
               avctx->color_range == AVCOL_RANGE_UNSPECIFIED)) {
        s->bpp = 1;
        s->bpp_idx = 2;
        s->diff_offset = 128;
    } else if (depth == 10) {
        s->bpp = 2;
        s->bpp_idx = 3;
        s->diff_offset = 512;
    } else {
        s->bpp = 2;
        s->bpp_idx = 4;
        s->diff_offset = 2048;
    }

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
                                      s->plane[i].coef_stride,
                                      s->plane[i].dwt_height))
            goto alloc_fail;
    }

    /* Slices */
    s->num_x = s->plane[0].dwt_width/s->slice_width;
    s->num_y = s->plane[0].dwt_height/s->slice_height;

    s->slice_args = av_calloc(s->num_x*s->num_y, sizeof(SliceArgs));
    if (!s->slice_args)
        goto alloc_fail;

    /* Lookup tables */
    s->coef_lut_len = av_malloc(COEF_LUT_TAB*(s->q_ceil+1)*sizeof(*s->coef_lut_len));
    if (!s->coef_lut_len)
        goto alloc_fail;

    s->coef_lut_val = av_malloc(COEF_LUT_TAB*(s->q_ceil+1)*sizeof(*s->coef_lut_val));
    if (!s->coef_lut_val)
        goto alloc_fail;

    for (i = 0; i < s->q_ceil; i++) {
        uint8_t  *len_lut = &s->coef_lut_len[i*COEF_LUT_TAB];
        uint32_t *val_lut = &s->coef_lut_val[i*COEF_LUT_TAB];
        for (j = 0; j < COEF_LUT_TAB; j++) {
            get_vc2_ue_uint(QUANT(j, ff_dirac_qscale_tab[i]),
                            &len_lut[j], &val_lut[j]);
            if (len_lut[j] != 1) {
                len_lut[j] += 1;
                val_lut[j] <<= 1;
            } else {
                val_lut[j] = 1;
            }
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
    {"tolerance",     "Max undershoot in percent", offsetof(VC2EncContext, tolerance), AV_OPT_TYPE_DOUBLE, {.dbl = 5.0f}, 0.0f, 45.0f, VC2ENC_FLAGS, "tolerance"},
    {"slice_width",   "Slice width",  offsetof(VC2EncContext, slice_width), AV_OPT_TYPE_INT, {.i64 = 32}, 32, 1024, VC2ENC_FLAGS, "slice_width"},
    {"slice_height",  "Slice height", offsetof(VC2EncContext, slice_height), AV_OPT_TYPE_INT, {.i64 = 16}, 8, 1024, VC2ENC_FLAGS, "slice_height"},
    {"wavelet_depth", "Transform depth", offsetof(VC2EncContext, wavelet_depth), AV_OPT_TYPE_INT, {.i64 = 4}, 1, 5, VC2ENC_FLAGS, "wavelet_depth"},
    {"wavelet_type",  "Transform type",  offsetof(VC2EncContext, wavelet_idx), AV_OPT_TYPE_INT, {.i64 = VC2_TRANSFORM_9_7}, 0, VC2_TRANSFORMS_NB, VC2ENC_FLAGS, "wavelet_idx"},
        {"9_7",          "Deslauriers-Dubuc (9,7)", 0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_9_7},    INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
        {"5_3",          "LeGall (5,3)",            0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_5_3},    INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
        {"haar",         "Haar (with shift)",       0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_HAAR_S}, INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
        {"haar_noshift", "Haar (without shift)",    0, AV_OPT_TYPE_CONST, {.i64 = VC2_TRANSFORM_HAAR},   INT_MIN, INT_MAX, VC2ENC_FLAGS, "wavelet_idx"},
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
    .name           = "vc2",
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE VC-2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DIRAC,
    .priv_data_size = sizeof(VC2EncContext),
    .init           = vc2_encode_init,
    .close          = vc2_encode_end,
    .capabilities   = AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
    .encode2        = vc2_encode_frame,
    .priv_class     = &vc2enc_class,
    .defaults       = vc2enc_defaults,
    .pix_fmts       = allowed_pix_fmts
};
