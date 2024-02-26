/*
 * AOM film grain synthesis
 * Copyright (c) 2023 Niklas Haas <ffmpeg@haasn.xyz>
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
 * AOM film grain synthesis.
 * @author Niklas Haas <ffmpeg@haasn.xyz>
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"

#include "aom_film_grain.h"
#include "get_bits.h"

// Common/shared helpers (not dependent on BIT_DEPTH)
static inline int get_random_number(const int bits, unsigned *const state) {
    const int r = *state;
    unsigned bit = ((r >> 0) ^ (r >> 1) ^ (r >> 3) ^ (r >> 12)) & 1;
    *state = (r >> 1) | (bit << 15);

    return (*state >> (16 - bits)) & ((1 << bits) - 1);
}

static inline int round2(const int x, const uint64_t shift) {
    return (x + ((1 << shift) >> 1)) >> shift;
}

enum {
    GRAIN_WIDTH      = 82,
    GRAIN_HEIGHT     = 73,
    SUB_GRAIN_WIDTH  = 44,
    SUB_GRAIN_HEIGHT = 38,
    FG_BLOCK_SIZE    = 32,
};

static const int16_t gaussian_sequence[2048];

#define BIT_DEPTH 16
#include "aom_film_grain_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 8
#include "aom_film_grain_template.c"
#undef BIT_DEPTH


int ff_aom_apply_film_grain(AVFrame *out, const AVFrame *in,
                            const AVFilmGrainParams *params)
{
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(out->format);
    const int subx = desc->log2_chroma_w, suby = desc->log2_chroma_h;
    const int pxstep = desc->comp[0].step;

    av_assert0(out->format == in->format);
    av_assert0(params->type == AV_FILM_GRAIN_PARAMS_AV1);

    // Copy over the non-modified planes
    if (!params->codec.aom.num_y_points) {
        av_image_copy_plane(out->data[0], out->linesize[0],
                            in->data[0], in->linesize[0],
                            out->width * pxstep, out->height);
    }
    for (int uv = 0; uv < 2; uv++) {
        if (!data->num_uv_points[uv]) {
            av_image_copy_plane(out->data[1+uv], out->linesize[1+uv],
                                in->data[1+uv], in->linesize[1+uv],
                                AV_CEIL_RSHIFT(out->width, subx) * pxstep,
                                AV_CEIL_RSHIFT(out->height, suby));
        }
    }

    switch (in->format) {
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
        return apply_film_grain_8(out, in, params);
    case AV_PIX_FMT_GRAY9:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUV444P9:
        return apply_film_grain_16(out, in, params, 9);
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV444P10:
        return apply_film_grain_16(out, in, params, 10);
    case AV_PIX_FMT_GRAY12:
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV422P12:
    case AV_PIX_FMT_YUV444P12:
        return apply_film_grain_16(out, in, params, 12);
    }

    /* The AV1 spec only defines film grain synthesis for these formats */
    return AVERROR_INVALIDDATA;
}

int ff_aom_parse_film_grain_sets(AVFilmGrainAFGS1Params *s,
                                 const uint8_t *payload, int payload_size)
{
    GetBitContext gbc, *gb = &gbc;
    AVFilmGrainAOMParams *aom;
    AVFilmGrainParams *fgp, *ref = NULL;
    int ret, num_sets, n, i, uv, num_y_coeffs, update_grain, luma_only;

    ret = init_get_bits8(gb, payload, payload_size);
    if (ret < 0)
        return ret;

    s->enable = get_bits1(gb);
    if (!s->enable)
        return 0;

    skip_bits(gb, 4); // reserved
    num_sets = get_bits(gb, 3) + 1;
    for (n = 0; n < num_sets; n++) {
        int payload_4byte, payload_size, set_idx, apply_units_log2, vsc_flag;
        int predict_scaling, predict_y_scaling, predict_uv_scaling[2];
        int payload_bits, start_position;

        start_position = get_bits_count(gb);
        payload_4byte = get_bits1(gb);
        payload_size = get_bits(gb, payload_4byte ? 2 : 8);
        set_idx = get_bits(gb, 3);
        fgp = &s->sets[set_idx];
        aom = &fgp->codec.aom;

        fgp->type = get_bits1(gb) ? AV_FILM_GRAIN_PARAMS_AV1 : AV_FILM_GRAIN_PARAMS_NONE;
        if (!fgp->type)
            continue;

        fgp->seed = get_bits(gb, 16);
        update_grain = get_bits1(gb);
        if (!update_grain)
            continue;

        apply_units_log2  = get_bits(gb, 4);
        fgp->width  = get_bits(gb, 12) << apply_units_log2;
        fgp->height = get_bits(gb, 12) << apply_units_log2;
        luma_only = get_bits1(gb);
        if (luma_only) {
            fgp->subsampling_x = fgp->subsampling_y = 0;
        } else {
            fgp->subsampling_x = get_bits1(gb);
            fgp->subsampling_y = get_bits1(gb);
        }

        fgp->bit_depth_luma  = fgp->bit_depth_chroma = 0;
        fgp->color_primaries = AVCOL_PRI_UNSPECIFIED;
        fgp->color_trc       = AVCOL_TRC_UNSPECIFIED;
        fgp->color_space     = AVCOL_SPC_UNSPECIFIED;
        fgp->color_range     = AVCOL_RANGE_UNSPECIFIED;

        vsc_flag = get_bits1(gb); // video_signal_characteristics_flag
        if (vsc_flag) {
            int cicp_flag;
            fgp->bit_depth_luma = get_bits(gb, 3) + 8;
            if (!luma_only)
                fgp->bit_depth_chroma = fgp->bit_depth_luma;
            cicp_flag = get_bits1(gb);
            if (cicp_flag) {
                fgp->color_primaries = get_bits(gb, 8);
                fgp->color_trc = get_bits(gb, 8);
                fgp->color_space = get_bits(gb, 8);
                fgp->color_range = get_bits1(gb) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
                if (fgp->color_primaries > AVCOL_PRI_NB ||
                    fgp->color_primaries == AVCOL_PRI_RESERVED ||
                    fgp->color_primaries == AVCOL_PRI_RESERVED0 ||
                    fgp->color_trc > AVCOL_TRC_NB ||
                    fgp->color_trc == AVCOL_TRC_RESERVED ||
                    fgp->color_trc == AVCOL_TRC_RESERVED0 ||
                    fgp->color_space > AVCOL_SPC_NB ||
                    fgp->color_space == AVCOL_SPC_RESERVED)
                    goto error;
            }
        }

        predict_scaling = get_bits1(gb);
        if (predict_scaling && (!ref || ref == fgp))
            goto error; // prediction must be from valid, different set

        predict_y_scaling = predict_scaling ? get_bits1(gb) : 0;
        if (predict_y_scaling) {
            int y_scale, y_offset, bits_res;
            y_scale = get_bits(gb, 9) - 256;
            y_offset = get_bits(gb, 9) - 256;
            bits_res = get_bits(gb, 3);
            if (bits_res) {
                int res[14], pred, granularity;
                aom->num_y_points = ref->codec.aom.num_y_points;
                for (i = 0; i < aom->num_y_points; i++)
                    res[i] = get_bits(gb, bits_res);
                granularity = get_bits(gb, 3);
                for (i = 0; i < aom->num_y_points; i++) {
                    pred = ref->codec.aom.y_points[i][1];
                    pred = ((pred * y_scale + 8) >> 4) + y_offset;
                    pred += (res[i] - (1 << (bits_res - 1))) * granularity;
                    aom->y_points[i][0] = ref->codec.aom.y_points[i][0];
                    aom->y_points[i][1] = av_clip_uint8(pred);
                }
            }
        } else {
            aom->num_y_points = get_bits(gb, 4);
            if (aom->num_y_points > 14) {
                goto error;
            } else if (aom->num_y_points) {
                int bits_inc, bits_scaling;
                int y_value = 0;
                bits_inc = get_bits(gb, 3) + 1;
                bits_scaling = get_bits(gb, 2) + 5;
                for (i = 0; i < aom->num_y_points; i++) {
                    y_value += get_bits(gb, bits_inc);
                    if (y_value > UINT8_MAX)
                        goto error;
                    aom->y_points[i][0] = y_value;
                    aom->y_points[i][1] = get_bits(gb, bits_scaling);
                }
            }
        }

        if (luma_only) {
            aom->chroma_scaling_from_luma = 0;
            aom->num_uv_points[0] = aom->num_uv_points[1] = 0;
        } else {
            aom->chroma_scaling_from_luma = get_bits1(gb);
            if (aom->chroma_scaling_from_luma) {
                aom->num_uv_points[0] = aom->num_uv_points[1] = 0;
            } else {
                for (uv = 0; uv < 2; uv++) {
                    predict_uv_scaling[uv] = predict_scaling ? get_bits1(gb) : 0;
                    if (predict_uv_scaling[uv]) {
                        int uv_scale, uv_offset, bits_res;
                        uv_scale = get_bits(gb, 9) - 256;
                        uv_offset = get_bits(gb, 9) - 256;
                        bits_res = get_bits(gb, 3);
                        aom->uv_mult[uv] = ref->codec.aom.uv_mult[uv];
                        aom->uv_mult_luma[uv] = ref->codec.aom.uv_mult_luma[uv];
                        aom->uv_offset[uv] = ref->codec.aom.uv_offset[uv];
                        if (bits_res) {
                            int res[10], pred, granularity;
                            aom->num_uv_points[uv] = ref->codec.aom.num_uv_points[uv];
                            for (i = 0; i < aom->num_uv_points[uv]; i++)
                                res[i] = get_bits(gb, bits_res);
                            granularity = get_bits(gb, 3);
                            for (i = 0; i < aom->num_uv_points[uv]; i++) {
                                pred = ref->codec.aom.uv_points[uv][i][1];
                                pred = ((pred * uv_scale + 8) >> 4) + uv_offset;
                                pred += (res[i] - (1 << (bits_res - 1))) * granularity;
                                aom->uv_points[uv][i][0] = ref->codec.aom.uv_points[uv][i][0];
                                aom->uv_points[uv][i][1] = av_clip_uint8(pred);
                            }
                        }
                    } else {
                        int bits_inc, bits_scaling, uv_offset;
                        int uv_value = 0;
                        aom->num_uv_points[uv] = get_bits(gb, 4);
                        if (aom->num_uv_points[uv] > 10)
                            goto error;
                        bits_inc = get_bits(gb, 3) + 1;
                        bits_scaling = get_bits(gb, 2) + 5;
                        uv_offset = get_bits(gb, 8);
                        for (i = 0; i < aom->num_uv_points[uv]; i++) {
                            uv_value += get_bits(gb, bits_inc);
                            if (uv_value > UINT8_MAX)
                                goto error;
                            aom->uv_points[uv][i][0] = uv_value;
                            aom->uv_points[uv][i][1] = get_bits(gb, bits_scaling) + uv_offset;
                        }
                    }
                }
            }
        }

        aom->scaling_shift = get_bits(gb, 2) + 8;
        aom->ar_coeff_lag = get_bits(gb, 2);
        num_y_coeffs = 2 * aom->ar_coeff_lag * (aom->ar_coeff_lag + 1);
        if (aom->num_y_points) {
            int ar_bits = get_bits(gb, 2) + 5;
            for (i = 0; i < num_y_coeffs; i++)
                aom->ar_coeffs_y[i] = get_bits(gb, ar_bits) - (1 << (ar_bits - 1));
        }
        for (uv = 0; uv < 2; uv++) {
            if (aom->chroma_scaling_from_luma || aom->num_uv_points[uv]) {
                int ar_bits = get_bits(gb, 2) + 5;
                for (i = 0; i < num_y_coeffs + !!aom->num_y_points; i++)
                    aom->ar_coeffs_uv[uv][i] = get_bits(gb, ar_bits) - (1 << (ar_bits - 1));
            }
        }
        aom->ar_coeff_shift = get_bits(gb, 2) + 6;
        aom->grain_scale_shift = get_bits(gb, 2);
        for (uv = 0; uv < 2; uv++) {
            if (aom->num_uv_points[uv] && !predict_uv_scaling[uv]) {
                aom->uv_mult[uv]      = get_bits(gb, 8) - 128;
                aom->uv_mult_luma[uv] = get_bits(gb, 8) - 128;
                aom->uv_offset[uv]    = get_bits(gb, 9) - 256;
            }
        }
        aom->overlap_flag = get_bits1(gb);
        aom->limit_output_range = get_bits1(gb);

        // use first set as reference only if it was fully transmitted
        if (n == 0)
            ref = fgp;

        payload_bits = get_bits_count(gb) - start_position;
        if (payload_bits > payload_size * 8)
            goto error;
        skip_bits(gb, payload_size * 8 - payload_bits);
    }
    return 0;

error:
    memset(s, 0, sizeof(*s));
    return AVERROR_INVALIDDATA;
}

int ff_aom_attach_film_grain_sets(const AVFilmGrainAFGS1Params *s, AVFrame *frame)
{
    AVFilmGrainParams *fgp;
    if (!s->enable)
        return 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->sets); i++) {
        if (s->sets[i].type != AV_FILM_GRAIN_PARAMS_AV1)
            continue;
        fgp = av_film_grain_params_create_side_data(frame);
        if (!fgp)
            return AVERROR(ENOMEM);
        memcpy(fgp, &s->sets[i], sizeof(*fgp));
    }

    return 0;
}

// Taken from the AV1 spec. Range is [-2048, 2047], mean is 0 and stddev is 512
static const int16_t gaussian_sequence[2048] = {
    56,    568,   -180,  172,   124,   -84,   172,   -64,   -900,  24,   820,
    224,   1248,  996,   272,   -8,    -916,  -388,  -732,  -104,  -188, 800,
    112,   -652,  -320,  -376,  140,   -252,  492,   -168,  44,    -788, 588,
    -584,  500,   -228,  12,    680,   272,   -476,  972,   -100,  652,  368,
    432,   -196,  -720,  -192,  1000,  -332,  652,   -136,  -552,  -604, -4,
    192,   -220,  -136,  1000,  -52,   372,   -96,   -624,  124,   -24,  396,
    540,   -12,   -104,  640,   464,   244,   -208,  -84,   368,   -528, -740,
    248,   -968,  -848,  608,   376,   -60,   -292,  -40,   -156,  252,  -292,
    248,   224,   -280,  400,   -244,  244,   -60,   76,    -80,   212,  532,
    340,   128,   -36,   824,   -352,  -60,   -264,  -96,   -612,  416,  -704,
    220,   -204,  640,   -160,  1220,  -408,  900,   336,   20,    -336, -96,
    -792,  304,   48,    -28,   -1232, -1172, -448,  104,   -292,  -520, 244,
    60,    -948,  0,     -708,  268,   108,   356,   -548,  488,   -344, -136,
    488,   -196,  -224,  656,   -236,  -1128, 60,    4,     140,   276,  -676,
    -376,  168,   -108,  464,   8,     564,   64,    240,   308,   -300, -400,
    -456,  -136,  56,    120,   -408,  -116,  436,   504,   -232,  328,  844,
    -164,  -84,   784,   -168,  232,   -224,  348,   -376,  128,   568,  96,
    -1244, -288,  276,   848,   832,   -360,  656,   464,   -384,  -332, -356,
    728,   -388,  160,   -192,  468,   296,   224,   140,   -776,  -100, 280,
    4,     196,   44,    -36,   -648,  932,   16,    1428,  28,    528,  808,
    772,   20,    268,   88,    -332,  -284,  124,   -384,  -448,  208,  -228,
    -1044, -328,  660,   380,   -148,  -300,  588,   240,   540,   28,   136,
    -88,   -436,  256,   296,   -1000, 1400,  0,     -48,   1056,  -136, 264,
    -528,  -1108, 632,   -484,  -592,  -344,  796,   124,   -668,  -768, 388,
    1296,  -232,  -188,  -200,  -288,  -4,    308,   100,   -168,  256,  -500,
    204,   -508,  648,   -136,  372,   -272,  -120,  -1004, -552,  -548, -384,
    548,   -296,  428,   -108,  -8,    -912,  -324,  -224,  -88,   -112, -220,
    -100,  996,   -796,  548,   360,   -216,  180,   428,   -200,  -212, 148,
    96,    148,   284,   216,   -412,  -320,  120,   -300,  -384,  -604, -572,
    -332,  -8,    -180,  -176,  696,   116,   -88,   628,   76,    44,   -516,
    240,   -208,  -40,   100,   -592,  344,   -308,  -452,  -228,  20,   916,
    -1752, -136,  -340,  -804,  140,   40,    512,   340,   248,   184,  -492,
    896,   -156,  932,   -628,  328,   -688,  -448,  -616,  -752,  -100, 560,
    -1020, 180,   -800,  -64,   76,    576,   1068,  396,   660,   552,  -108,
    -28,   320,   -628,  312,   -92,   -92,   -472,  268,   16,    560,  516,
    -672,  -52,   492,   -100,  260,   384,   284,   292,   304,   -148, 88,
    -152,  1012,  1064,  -228,  164,   -376,  -684,  592,   -392,  156,  196,
    -524,  -64,   -884,  160,   -176,  636,   648,   404,   -396,  -436, 864,
    424,   -728,  988,   -604,  904,   -592,  296,   -224,  536,   -176, -920,
    436,   -48,   1176,  -884,  416,   -776,  -824,  -884,  524,   -548, -564,
    -68,   -164,  -96,   692,   364,   -692,  -1012, -68,   260,   -480, 876,
    -1116, 452,   -332,  -352,  892,   -1088, 1220,  -676,  12,    -292, 244,
    496,   372,   -32,   280,   200,   112,   -440,  -96,   24,    -644, -184,
    56,    -432,  224,   -980,  272,   -260,  144,   -436,  420,   356,  364,
    -528,  76,    172,   -744,  -368,  404,   -752,  -416,  684,   -688, 72,
    540,   416,   92,    444,   480,   -72,   -1416, 164,   -1172, -68,  24,
    424,   264,   1040,  128,   -912,  -524,  -356,  64,    876,   -12,  4,
    -88,   532,   272,   -524,  320,   276,   -508,  940,   24,    -400, -120,
    756,   60,    236,   -412,  100,   376,   -484,  400,   -100,  -740, -108,
    -260,  328,   -268,  224,   -200,  -416,  184,   -604,  -564,  -20,  296,
    60,    892,   -888,  60,    164,   68,    -760,  216,   -296,  904,  -336,
    -28,   404,   -356,  -568,  -208,  -1480, -512,  296,   328,   -360, -164,
    -1560, -776,  1156,  -428,  164,   -504,  -112,  120,   -216,  -148, -264,
    308,   32,    64,    -72,   72,    116,   176,   -64,   -272,  460,  -536,
    -784,  -280,  348,   108,   -752,  -132,  524,   -540,  -776,  116,  -296,
    -1196, -288,  -560,  1040,  -472,  116,   -848,  -1116, 116,   636,  696,
    284,   -176,  1016,  204,   -864,  -648,  -248,  356,   972,   -584, -204,
    264,   880,   528,   -24,   -184,  116,   448,   -144,  828,   524,  212,
    -212,  52,    12,    200,   268,   -488,  -404,  -880,  824,   -672, -40,
    908,   -248,  500,   716,   -576,  492,   -576,  16,    720,   -108, 384,
    124,   344,   280,   576,   -500,  252,   104,   -308,  196,   -188, -8,
    1268,  296,   1032,  -1196, 436,   316,   372,   -432,  -200,  -660, 704,
    -224,  596,   -132,  268,   32,    -452,  884,   104,   -1008, 424,  -1348,
    -280,  4,     -1168, 368,   476,   696,   300,   -8,    24,    180,  -592,
    -196,  388,   304,   500,   724,   -160,  244,   -84,   272,   -256, -420,
    320,   208,   -144,  -156,  156,   364,   452,   28,    540,   316,  220,
    -644,  -248,  464,   72,    360,   32,    -388,  496,   -680,  -48,  208,
    -116,  -408,  60,    -604,  -392,  548,   -840,  784,   -460,  656,  -544,
    -388,  -264,  908,   -800,  -628,  -612,  -568,  572,   -220,  164,  288,
    -16,   -308,  308,   -112,  -636,  -760,  280,   -668,  432,   364,  240,
    -196,  604,   340,   384,   196,   592,   -44,   -500,  432,   -580, -132,
    636,   -76,   392,   4,     -412,  540,   508,   328,   -356,  -36,  16,
    -220,  -64,   -248,  -60,   24,    -192,  368,   1040,  92,    -24,  -1044,
    -32,   40,    104,   148,   192,   -136,  -520,  56,    -816,  -224, 732,
    392,   356,   212,   -80,   -424,  -1008, -324,  588,   -1496, 576,  460,
    -816,  -848,  56,    -580,  -92,   -1372, -112,  -496,  200,   364,  52,
    -140,  48,    -48,   -60,   84,    72,    40,    132,   -356,  -268, -104,
    -284,  -404,  732,   -520,  164,   -304,  -540,  120,   328,   -76,  -460,
    756,   388,   588,   236,   -436,  -72,   -176,  -404,  -316,  -148, 716,
    -604,  404,   -72,   -88,   -888,  -68,   944,   88,    -220,  -344, 960,
    472,   460,   -232,  704,   120,   832,   -228,  692,   -508,  132,  -476,
    844,   -748,  -364,  -44,   1116,  -1104, -1056, 76,    428,   552,  -692,
    60,    356,   96,    -384,  -188,  -612,  -576,  736,   508,   892,  352,
    -1132, 504,   -24,   -352,  324,   332,   -600,  -312,  292,   508,  -144,
    -8,    484,   48,    284,   -260,  -240,  256,   -100,  -292,  -204, -44,
    472,   -204,  908,   -188,  -1000, -256,  92,    1164,  -392,  564,  356,
    652,   -28,   -884,  256,   484,   -192,  760,   -176,  376,   -524, -452,
    -436,  860,   -736,  212,   124,   504,   -476,  468,   76,    -472, 552,
    -692,  -944,  -620,  740,   -240,  400,   132,   20,    192,   -196, 264,
    -668,  -1012, -60,   296,   -316,  -828,  76,    -156,  284,   -768, -448,
    -832,  148,   248,   652,   616,   1236,  288,   -328,  -400,  -124, 588,
    220,   520,   -696,  1032,  768,   -740,  -92,   -272,  296,   448,  -464,
    412,   -200,  392,   440,   -200,  264,   -152,  -260,  320,   1032, 216,
    320,   -8,    -64,   156,   -1016, 1084,  1172,  536,   484,   -432, 132,
    372,   -52,   -256,  84,    116,   -352,  48,    116,   304,   -384, 412,
    924,   -300,  528,   628,   180,   648,   44,    -980,  -220,  1320, 48,
    332,   748,   524,   -268,  -720,  540,   -276,  564,   -344,  -208, -196,
    436,   896,   88,    -392,  132,   80,    -964,  -288,  568,   56,   -48,
    -456,  888,   8,     552,   -156,  -292,  948,   288,   128,   -716, -292,
    1192,  -152,  876,   352,   -600,  -260,  -812,  -468,  -28,   -120, -32,
    -44,   1284,  496,   192,   464,   312,   -76,   -516,  -380,  -456, -1012,
    -48,   308,   -156,  36,    492,   -156,  -808,  188,   1652,  68,   -120,
    -116,  316,   160,   -140,  352,   808,   -416,  592,   316,   -480, 56,
    528,   -204,  -568,  372,   -232,  752,   -344,  744,   -4,    324,  -416,
    -600,  768,   268,   -248,  -88,   -132,  -420,  -432,  80,    -288, 404,
    -316,  -1216, -588,  520,   -108,  92,    -320,  368,   -480,  -216, -92,
    1688,  -300,  180,   1020,  -176,  820,   -68,   -228,  -260,  436,  -904,
    20,    40,    -508,  440,   -736,  312,   332,   204,   760,   -372, 728,
    96,    -20,   -632,  -520,  -560,  336,   1076,  -64,   -532,  776,  584,
    192,   396,   -728,  -520,  276,   -188,  80,    -52,   -612,  -252, -48,
    648,   212,   -688,  228,   -52,   -260,  428,   -412,  -272,  -404, 180,
    816,   -796,  48,    152,   484,   -88,   -216,  988,   696,   188,  -528,
    648,   -116,  -180,  316,   476,   12,    -564,  96,    476,   -252, -364,
    -376,  -392,  556,   -256,  -576,  260,   -352,  120,   -16,   -136, -260,
    -492,  72,    556,   660,   580,   616,   772,   436,   424,   -32,  -324,
    -1268, 416,   -324,  -80,   920,   160,   228,   724,   32,    -516, 64,
    384,   68,    -128,  136,   240,   248,   -204,  -68,   252,   -932, -120,
    -480,  -628,  -84,   192,   852,   -404,  -288,  -132,  204,   100,  168,
    -68,   -196,  -868,  460,   1080,  380,   -80,   244,   0,     484,  -888,
    64,    184,   352,   600,   460,   164,   604,   -196,  320,   -64,  588,
    -184,  228,   12,    372,   48,    -848,  -344,  224,   208,   -200, 484,
    128,   -20,   272,   -468,  -840,  384,   256,   -720,  -520,  -464, -580,
    112,   -120,  644,   -356,  -208,  -608,  -528,  704,   560,   -424, 392,
    828,   40,    84,    200,   -152,  0,     -144,  584,   280,   -120, 80,
    -556,  -972,  -196,  -472,  724,   80,    168,   -32,   88,    160,  -688,
    0,     160,   356,   372,   -776,  740,   -128,  676,   -248,  -480, 4,
    -364,  96,    544,   232,   -1032, 956,   236,   356,   20,    -40,  300,
    24,    -676,  -596,  132,   1120,  -104,  532,   -1096, 568,   648,  444,
    508,   380,   188,   -376,  -604,  1488,  424,   24,    756,   -220, -192,
    716,   120,   920,   688,   168,   44,    -460,  568,   284,   1144, 1160,
    600,   424,   888,   656,   -356,  -320,  220,   316,   -176,  -724, -188,
    -816,  -628,  -348,  -228,  -380,  1012,  -452,  -660,  736,   928,  404,
    -696,  -72,   -268,  -892,  128,   184,   -344,  -780,  360,   336,  400,
    344,   428,   548,   -112,  136,   -228,  -216,  -820,  -516,  340,  92,
    -136,  116,   -300,  376,   -244,  100,   -316,  -520,  -284,  -12,  824,
    164,   -548,  -180,  -128,  116,   -924,  -828,  268,   -368,  -580, 620,
    192,   160,   0,     -1676, 1068,  424,   -56,   -360,  468,   -156, 720,
    288,   -528,  556,   -364,  548,   -148,  504,   316,   152,   -648, -620,
    -684,  -24,   -376,  -384,  -108,  -920,  -1032, 768,   180,   -264, -508,
    -1268, -260,  -60,   300,   -240,  988,   724,   -376,  -576,  -212, -736,
    556,   192,   1092,  -620,  -880,  376,   -56,   -4,    -216,  -32,  836,
    268,   396,   1332,  864,   -600,  100,   56,    -412,  -92,   356,  180,
    884,   -468,  -436,  292,   -388,  -804,  -704,  -840,  368,   -348, 140,
    -724,  1536,  940,   372,   112,   -372,  436,   -480,  1136,  296,  -32,
    -228,  132,   -48,   -220,  868,   -1016, -60,   -1044, -464,  328,  916,
    244,   12,    -736,  -296,  360,   468,   -376,  -108,  -92,   788,  368,
    -56,   544,   400,   -672,  -420,  728,   16,    320,   44,    -284, -380,
    -796,  488,   132,   204,   -596,  -372,  88,    -152,  -908,  -636, -572,
    -624,  -116,  -692,  -200,  -56,   276,   -88,   484,   -324,  948,  864,
    1000,  -456,  -184,  -276,  292,   -296,  156,   676,   320,   160,  908,
    -84,   -1236, -288,  -116,  260,   -372,  -644,  732,   -756,  -96,  84,
    344,   -520,  348,   -688,  240,   -84,   216,   -1044, -136,  -676, -396,
    -1500, 960,   -40,   176,   168,   1516,  420,   -504,  -344,  -364, -360,
    1216,  -940,  -380,  -212,  252,   -660,  -708,  484,   -444,  -152, 928,
    -120,  1112,  476,   -260,  560,   -148,  -344,  108,   -196,  228,  -288,
    504,   560,   -328,  -88,   288,   -1008, 460,   -228,  468,   -836, -196,
    76,    388,   232,   412,   -1168, -716,  -644,  756,   -172,  -356, -504,
    116,   432,   528,   48,    476,   -168,  -608,  448,   160,   -532, -272,
    28,    -676,  -12,   828,   980,   456,   520,   104,   -104,  256,  -344,
    -4,    -28,   -368,  -52,   -524,  -572,  -556,  -200,  768,   1124, -208,
    -512,  176,   232,   248,   -148,  -888,  604,   -600,  -304,  804,  -156,
    -212,  488,   -192,  -804,  -256,  368,   -360,  -916,  -328,  228,  -240,
    -448,  -472,  856,   -556,  -364,  572,   -12,   -156,  -368,  -340, 432,
    252,   -752,  -152,  288,   268,   -580,  -848,  -592,  108,   -76,  244,
    312,   -716,  592,   -80,   436,   360,   4,     -248,  160,   516,  584,
    732,   44,    -468,  -280,  -292,  -156,  -588,  28,    308,   912,  24,
    124,   156,   180,   -252,  944,   -924,  -772,  -520,  -428,  -624, 300,
    -212,  -1144, 32,    -724,  800,   -1128, -212,  -1288, -848,  180,  -416,
    440,   192,   -576,  -792,  -76,   -1080, 80,    -532,  -352,  -132, 380,
    -820,  148,   1112,  128,   164,   456,   700,   -924,  144,   -668, -384,
    648,   -832,  508,   552,   -52,   -100,  -656,  208,   -568,  748,  -88,
    680,   232,   300,   192,   -408,  -1012, -152,  -252,  -268,  272,  -876,
    -664,  -648,  -332,  -136,  16,    12,    1152,  -28,   332,   -536, 320,
    -672,  -460,  -316,  532,   -260,  228,   -40,   1052,  -816,  180,  88,
    -496,  -556,  -672,  -368,  428,   92,    356,   404,   -408,  252,  196,
    -176,  -556,  792,   268,   32,    372,   40,    96,    -332,  328,  120,
    372,   -900,  -40,   472,   -264,  -592,  952,   128,   656,   112,  664,
    -232,  420,   4,     -344,  -464,  556,   244,   -416,  -32,   252,  0,
    -412,  188,   -696,  508,   -476,  324,   -1096, 656,   -312,  560,  264,
    -136,  304,   160,   -64,   -580,  248,   336,   -720,  560,   -348, -288,
    -276,  -196,  -500,  852,   -544,  -236,  -1128, -992,  -776,  116,  56,
    52,    860,   884,   212,   -12,   168,   1020,  512,   -552,  924,  -148,
    716,   188,   164,   -340,  -520,  -184,  880,   -152,  -680,  -208, -1156,
    -300,  -528,  -472,  364,   100,   -744,  -1056, -32,   540,   280,  144,
    -676,  -32,   -232,  -280,  -224,  96,    568,   -76,   172,   148,  148,
    104,   32,    -296,  -32,   788,   -80,   32,    -16,   280,   288,  944,
    428,   -484
};
