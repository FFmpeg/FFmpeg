/*
 * VC3/DNxHD encoder
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 * Copyright (c) 2011 MirriAd Ltd
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
 * 10 bit support added by MirriAd Ltd, Joseph Artsimovich <joseph@mirriad.com>
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

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/timer.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "fdctdsp.h"
#include "internal.h"
#include "mpegvideo.h"
#include "pixblockdsp.h"
#include "profiles.h"
#include "dnxhdenc.h"

// The largest value that will not lead to overflow for 10-bit samples.
#define DNX10BIT_QMAT_SHIFT 18
#define RC_VARIANCE 1 // use variance or ssd for fast rc
#define LAMBDA_FRAC_BITS 10

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "nitris_compat", "encode with Avid Nitris compatibility",
        offsetof(DNXHDEncContext, nitris_compat), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "ibias", "intra quant bias",
        offsetof(DNXHDEncContext, intra_quant_bias), AV_OPT_TYPE_INT,
        { .i64 = 0 }, INT_MIN, INT_MAX, VE },
    { "profile",       NULL, offsetof(DNXHDEncContext, profile), AV_OPT_TYPE_INT,
        { .i64 = FF_PROFILE_DNXHD },
        FF_PROFILE_DNXHD, FF_PROFILE_DNXHR_444, VE, "profile" },
    { "dnxhd",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHD },
        0, 0, VE, "profile" },
    { "dnxhr_444", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHR_444 },
        0, 0, VE, "profile" },
    { "dnxhr_hqx", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHR_HQX },
        0, 0, VE, "profile" },
    { "dnxhr_hq",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHR_HQ },
        0, 0, VE, "profile" },
    { "dnxhr_sq",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHR_SQ },
        0, 0, VE, "profile" },
    { "dnxhr_lb",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_PROFILE_DNXHR_LB },
        0, 0, VE, "profile" },
    { NULL }
};

static const AVClass dnxhd_class = {
    .class_name = "dnxhd",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void dnxhd_8bit_get_pixels_8x4_sym(int16_t *av_restrict block,
                                          const uint8_t *pixels,
                                          ptrdiff_t line_size)
{
    int i;
    for (i = 0; i < 4; i++) {
        block[0] = pixels[0];
        block[1] = pixels[1];
        block[2] = pixels[2];
        block[3] = pixels[3];
        block[4] = pixels[4];
        block[5] = pixels[5];
        block[6] = pixels[6];
        block[7] = pixels[7];
        pixels  += line_size;
        block   += 8;
    }
    memcpy(block,      block -  8, sizeof(*block) * 8);
    memcpy(block +  8, block - 16, sizeof(*block) * 8);
    memcpy(block + 16, block - 24, sizeof(*block) * 8);
    memcpy(block + 24, block - 32, sizeof(*block) * 8);
}

static av_always_inline
void dnxhd_10bit_get_pixels_8x4_sym(int16_t *av_restrict block,
                                    const uint8_t *pixels,
                                    ptrdiff_t line_size)
{
    memcpy(block + 0 * 8, pixels + 0 * line_size, 8 * sizeof(*block));
    memcpy(block + 7 * 8, pixels + 0 * line_size, 8 * sizeof(*block));
    memcpy(block + 1 * 8, pixels + 1 * line_size, 8 * sizeof(*block));
    memcpy(block + 6 * 8, pixels + 1 * line_size, 8 * sizeof(*block));
    memcpy(block + 2 * 8, pixels + 2 * line_size, 8 * sizeof(*block));
    memcpy(block + 5 * 8, pixels + 2 * line_size, 8 * sizeof(*block));
    memcpy(block + 3 * 8, pixels + 3 * line_size, 8 * sizeof(*block));
    memcpy(block + 4 * 8, pixels + 3 * line_size, 8 * sizeof(*block));
}

static int dnxhd_10bit_dct_quantize_444(MpegEncContext *ctx, int16_t *block,
                                        int n, int qscale, int *overflow)
{
    int i, j, level, last_non_zero, start_i;
    const int *qmat;
    const uint8_t *scantable= ctx->intra_scantable.scantable;
    int bias;
    int max = 0;
    unsigned int threshold1, threshold2;

    ctx->fdsp.fdct(block);

    block[0] = (block[0] + 2) >> 2;
    start_i = 1;
    last_non_zero = 0;
    qmat = n < 4 ? ctx->q_intra_matrix[qscale] : ctx->q_chroma_intra_matrix[qscale];
    bias= ctx->intra_quant_bias * (1 << (16 - 8));
    threshold1 = (1 << 16) - bias - 1;
    threshold2 = (threshold1 << 1);

    for (i = 63; i >= start_i; i--) {
        j = scantable[i];
        level = block[j] * qmat[j];

        if (((unsigned)(level + threshold1)) > threshold2) {
            last_non_zero = i;
            break;
        } else{
            block[j]=0;
        }
    }

    for (i = start_i; i <= last_non_zero; i++) {
        j = scantable[i];
        level = block[j] * qmat[j];

        if (((unsigned)(level + threshold1)) > threshold2) {
            if (level > 0) {
                level = (bias + level) >> 16;
                block[j] = level;
            } else{
                level = (bias - level) >> 16;
                block[j] = -level;
            }
            max |= level;
        } else {
            block[j] = 0;
        }
    }
    *overflow = ctx->max_qcoeff < max; //overflow might have happened

    /* we need this permutation so that we correct the IDCT, we only permute the !=0 elements */
    if (ctx->idsp.perm_type != FF_IDCT_PERM_NONE)
        ff_block_permute(block, ctx->idsp.idct_permutation,
                         scantable, last_non_zero);

    return last_non_zero;
}

static int dnxhd_10bit_dct_quantize(MpegEncContext *ctx, int16_t *block,
                                    int n, int qscale, int *overflow)
{
    const uint8_t *scantable= ctx->intra_scantable.scantable;
    const int *qmat = n<4 ? ctx->q_intra_matrix[qscale] : ctx->q_chroma_intra_matrix[qscale];
    int last_non_zero = 0;
    int i;

    ctx->fdsp.fdct(block);

    // Divide by 4 with rounding, to compensate scaling of DCT coefficients
    block[0] = (block[0] + 2) >> 2;

    for (i = 1; i < 64; ++i) {
        int j = scantable[i];
        int sign = FF_SIGNBIT(block[j]);
        int level = (block[j] ^ sign) - sign;
        level = level * qmat[j] >> DNX10BIT_QMAT_SHIFT;
        block[j] = (level ^ sign) - sign;
        if (level)
            last_non_zero = i;
    }

    /* we need this permutation so that we correct the IDCT, we only permute the !=0 elements */
    if (ctx->idsp.perm_type != FF_IDCT_PERM_NONE)
        ff_block_permute(block, ctx->idsp.idct_permutation,
                         scantable, last_non_zero);

    return last_non_zero;
}

static av_cold int dnxhd_init_vlc(DNXHDEncContext *ctx)
{
    int i, j, level, run;
    int max_level = 1 << (ctx->bit_depth + 2);

    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->orig_vlc_codes,
                      max_level, 4 * sizeof(*ctx->orig_vlc_codes), fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->orig_vlc_bits,
                      max_level, 4 * sizeof(*ctx->orig_vlc_bits), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->run_codes,
                      63 * 2, fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->run_bits,
                      63, fail);

    ctx->vlc_codes = ctx->orig_vlc_codes + max_level * 2;
    ctx->vlc_bits  = ctx->orig_vlc_bits + max_level * 2;
    for (level = -max_level; level < max_level; level++) {
        for (run = 0; run < 2; run++) {
            int index = (level << 1) | run;
            int sign, offset = 0, alevel = level;

            MASK_ABS(sign, alevel);
            if (alevel > 64) {
                offset  = (alevel - 1) >> 6;
                alevel -= offset << 6;
            }
            for (j = 0; j < 257; j++) {
                if (ctx->cid_table->ac_info[2*j+0] >> 1 == alevel &&
                    (!offset || (ctx->cid_table->ac_info[2*j+1] & 1) && offset) &&
                    (!run    || (ctx->cid_table->ac_info[2*j+1] & 2) && run)) {
                    av_assert1(!ctx->vlc_codes[index]);
                    if (alevel) {
                        ctx->vlc_codes[index] =
                            (ctx->cid_table->ac_codes[j] << 1) | (sign & 1);
                        ctx->vlc_bits[index] = ctx->cid_table->ac_bits[j] + 1;
                    } else {
                        ctx->vlc_codes[index] = ctx->cid_table->ac_codes[j];
                        ctx->vlc_bits[index]  = ctx->cid_table->ac_bits[j];
                    }
                    break;
                }
            }
            av_assert0(!alevel || j < 257);
            if (offset) {
                ctx->vlc_codes[index] =
                    (ctx->vlc_codes[index] << ctx->cid_table->index_bits) | offset;
                ctx->vlc_bits[index] += ctx->cid_table->index_bits;
            }
        }
    }
    for (i = 0; i < 62; i++) {
        int run = ctx->cid_table->run[i];
        av_assert0(run < 63);
        ctx->run_codes[run] = ctx->cid_table->run_codes[i];
        ctx->run_bits[run]  = ctx->cid_table->run_bits[i];
    }
    return 0;
fail:
    return AVERROR(ENOMEM);
}

static av_cold int dnxhd_init_qmat(DNXHDEncContext *ctx, int lbias, int cbias)
{
    // init first elem to 1 to avoid div by 0 in convert_matrix
    uint16_t weight_matrix[64] = { 1, }; // convert_matrix needs uint16_t*
    int qscale, i;
    const uint8_t *luma_weight_table   = ctx->cid_table->luma_weight;
    const uint8_t *chroma_weight_table = ctx->cid_table->chroma_weight;

    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->qmatrix_l,
                      (ctx->m.avctx->qmax + 1), 64 * sizeof(int), fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->qmatrix_c,
                      (ctx->m.avctx->qmax + 1), 64 * sizeof(int), fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->qmatrix_l16,
                      (ctx->m.avctx->qmax + 1), 64 * 2 * sizeof(uint16_t),
                      fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->qmatrix_c16,
                      (ctx->m.avctx->qmax + 1), 64 * 2 * sizeof(uint16_t),
                      fail);

    if (ctx->bit_depth == 8) {
        for (i = 1; i < 64; i++) {
            int j = ctx->m.idsp.idct_permutation[ff_zigzag_direct[i]];
            weight_matrix[j] = ctx->cid_table->luma_weight[i];
        }
        ff_convert_matrix(&ctx->m, ctx->qmatrix_l, ctx->qmatrix_l16,
                          weight_matrix, ctx->intra_quant_bias, 1,
                          ctx->m.avctx->qmax, 1);
        for (i = 1; i < 64; i++) {
            int j = ctx->m.idsp.idct_permutation[ff_zigzag_direct[i]];
            weight_matrix[j] = ctx->cid_table->chroma_weight[i];
        }
        ff_convert_matrix(&ctx->m, ctx->qmatrix_c, ctx->qmatrix_c16,
                          weight_matrix, ctx->intra_quant_bias, 1,
                          ctx->m.avctx->qmax, 1);

        for (qscale = 1; qscale <= ctx->m.avctx->qmax; qscale++) {
            for (i = 0; i < 64; i++) {
                ctx->qmatrix_l[qscale][i]      <<= 2;
                ctx->qmatrix_c[qscale][i]      <<= 2;
                ctx->qmatrix_l16[qscale][0][i] <<= 2;
                ctx->qmatrix_l16[qscale][1][i] <<= 2;
                ctx->qmatrix_c16[qscale][0][i] <<= 2;
                ctx->qmatrix_c16[qscale][1][i] <<= 2;
            }
        }
    } else {
        // 10-bit
        for (qscale = 1; qscale <= ctx->m.avctx->qmax; qscale++) {
            for (i = 1; i < 64; i++) {
                int j = ff_zigzag_direct[i];

                /* The quantization formula from the VC-3 standard is:
                 * quantized = sign(block[i]) * floor(abs(block[i]/s) * p /
                 *             (qscale * weight_table[i]))
                 * Where p is 32 for 8-bit samples and 8 for 10-bit ones.
                 * The s factor compensates scaling of DCT coefficients done by
                 * the DCT routines, and therefore is not present in standard.
                 * It's 8 for 8-bit samples and 4 for 10-bit ones.
                 * We want values of ctx->qtmatrix_l and ctx->qtmatrix_r to be:
                 *     ((1 << DNX10BIT_QMAT_SHIFT) * (p / s)) /
                 *     (qscale * weight_table[i])
                 * For 10-bit samples, p / s == 2 */
                ctx->qmatrix_l[qscale][j] = (1 << (DNX10BIT_QMAT_SHIFT + 1)) /
                                            (qscale * luma_weight_table[i]);
                ctx->qmatrix_c[qscale][j] = (1 << (DNX10BIT_QMAT_SHIFT + 1)) /
                                            (qscale * chroma_weight_table[i]);
            }
        }
    }

    ctx->m.q_chroma_intra_matrix16 = ctx->qmatrix_c16;
    ctx->m.q_chroma_intra_matrix   = ctx->qmatrix_c;
    ctx->m.q_intra_matrix16        = ctx->qmatrix_l16;
    ctx->m.q_intra_matrix          = ctx->qmatrix_l;

    return 0;
fail:
    return AVERROR(ENOMEM);
}

static av_cold int dnxhd_init_rc(DNXHDEncContext *ctx)
{
    FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->mb_rc, (ctx->m.avctx->qmax + 1),
                          ctx->m.mb_num * sizeof(RCEntry), fail);
    if (ctx->m.avctx->mb_decision != FF_MB_DECISION_RD) {
        FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->mb_cmp,
                          ctx->m.mb_num, sizeof(RCCMPEntry), fail);
        FF_ALLOCZ_ARRAY_OR_GOTO(ctx->m.avctx, ctx->mb_cmp_tmp,
                          ctx->m.mb_num, sizeof(RCCMPEntry), fail);
    }
    ctx->frame_bits = (ctx->coding_unit_size -
                       ctx->data_offset - 4 - ctx->min_padding) * 8;
    ctx->qscale = 1;
    ctx->lambda = 2 << LAMBDA_FRAC_BITS; // qscale 2
    return 0;
fail:
    return AVERROR(ENOMEM);
}

static av_cold int dnxhd_encode_init(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int i, index, ret;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV422P:
        ctx->bit_depth = 8;
        break;
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_GBRP10:
        ctx->bit_depth = 10;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "pixel format is incompatible with DNxHD\n");
        return AVERROR(EINVAL);
    }

    if ((ctx->profile == FF_PROFILE_DNXHR_444 && (avctx->pix_fmt != AV_PIX_FMT_YUV444P10 &&
                                                  avctx->pix_fmt != AV_PIX_FMT_GBRP10)) ||
        (ctx->profile != FF_PROFILE_DNXHR_444 && (avctx->pix_fmt == AV_PIX_FMT_YUV444P10 ||
                                                  avctx->pix_fmt == AV_PIX_FMT_GBRP10))) {
        av_log(avctx, AV_LOG_ERROR,
               "pixel format is incompatible with DNxHD profile\n");
        return AVERROR(EINVAL);
    }

    if (ctx->profile == FF_PROFILE_DNXHR_HQX && avctx->pix_fmt != AV_PIX_FMT_YUV422P10) {
        av_log(avctx, AV_LOG_ERROR,
               "pixel format is incompatible with DNxHR HQX profile\n");
        return AVERROR(EINVAL);
    }

    if ((ctx->profile == FF_PROFILE_DNXHR_LB ||
         ctx->profile == FF_PROFILE_DNXHR_SQ ||
         ctx->profile == FF_PROFILE_DNXHR_HQ) && avctx->pix_fmt != AV_PIX_FMT_YUV422P) {
        av_log(avctx, AV_LOG_ERROR,
               "pixel format is incompatible with DNxHR LB/SQ/HQ profile\n");
        return AVERROR(EINVAL);
    }

    ctx->is_444 = ctx->profile == FF_PROFILE_DNXHR_444;
    avctx->profile = ctx->profile;
    ctx->cid = ff_dnxhd_find_cid(avctx, ctx->bit_depth);
    if (!ctx->cid) {
        av_log(avctx, AV_LOG_ERROR,
               "video parameters incompatible with DNxHD. Valid DNxHD profiles:\n");
        ff_dnxhd_print_profiles(avctx, AV_LOG_ERROR);
        return AVERROR(EINVAL);
    }
    av_log(avctx, AV_LOG_DEBUG, "cid %d\n", ctx->cid);

    if (ctx->cid >= 1270 && ctx->cid <= 1274)
        avctx->codec_tag = MKTAG('A','V','d','h');

    if (avctx->width < 256 || avctx->height < 120) {
        av_log(avctx, AV_LOG_ERROR,
               "Input dimensions too small, input must be at least 256x120\n");
        return AVERROR(EINVAL);
    }

    index = ff_dnxhd_get_cid_table(ctx->cid);
    av_assert0(index >= 0);

    ctx->cid_table = &ff_dnxhd_cid_table[index];

    ctx->m.avctx    = avctx;
    ctx->m.mb_intra = 1;
    ctx->m.h263_aic = 1;

    avctx->bits_per_raw_sample = ctx->bit_depth;

    ff_blockdsp_init(&ctx->bdsp, avctx);
    ff_fdctdsp_init(&ctx->m.fdsp, avctx);
    ff_mpv_idct_init(&ctx->m);
    ff_mpegvideoencdsp_init(&ctx->m.mpvencdsp, avctx);
    ff_pixblockdsp_init(&ctx->m.pdsp, avctx);
    ff_dct_encode_init(&ctx->m);

    if (ctx->profile != FF_PROFILE_DNXHD)
        ff_videodsp_init(&ctx->m.vdsp, ctx->bit_depth);

    if (!ctx->m.dct_quantize)
        ctx->m.dct_quantize = ff_dct_quantize_c;

    if (ctx->is_444 || ctx->profile == FF_PROFILE_DNXHR_HQX) {
        ctx->m.dct_quantize     = dnxhd_10bit_dct_quantize_444;
        ctx->get_pixels_8x4_sym = dnxhd_10bit_get_pixels_8x4_sym;
        ctx->block_width_l2     = 4;
    } else if (ctx->bit_depth == 10) {
        ctx->m.dct_quantize     = dnxhd_10bit_dct_quantize;
        ctx->get_pixels_8x4_sym = dnxhd_10bit_get_pixels_8x4_sym;
        ctx->block_width_l2     = 4;
    } else {
        ctx->get_pixels_8x4_sym = dnxhd_8bit_get_pixels_8x4_sym;
        ctx->block_width_l2     = 3;
    }

    if (ARCH_X86)
        ff_dnxhdenc_init_x86(ctx);

    ctx->m.mb_height = (avctx->height + 15) / 16;
    ctx->m.mb_width  = (avctx->width  + 15) / 16;

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        ctx->interlaced   = 1;
        ctx->m.mb_height /= 2;
    }

    if (ctx->interlaced && ctx->profile != FF_PROFILE_DNXHD) {
        av_log(avctx, AV_LOG_ERROR,
               "Interlaced encoding is not supported for DNxHR profiles.\n");
        return AVERROR(EINVAL);
    }

    ctx->m.mb_num = ctx->m.mb_height * ctx->m.mb_width;

    if (ctx->cid_table->frame_size == DNXHD_VARIABLE) {
        ctx->frame_size = avpriv_dnxhd_get_hr_frame_size(ctx->cid,
                                                     avctx->width, avctx->height);
        av_assert0(ctx->frame_size >= 0);
        ctx->coding_unit_size = ctx->frame_size;
    } else {
        ctx->frame_size = ctx->cid_table->frame_size;
        ctx->coding_unit_size = ctx->cid_table->coding_unit_size;
    }

    if (ctx->m.mb_height > 68)
        ctx->data_offset = 0x170 + (ctx->m.mb_height << 2);
    else
        ctx->data_offset = 0x280;

    // XXX tune lbias/cbias
    if ((ret = dnxhd_init_qmat(ctx, ctx->intra_quant_bias, 0)) < 0)
        return ret;

    /* Avid Nitris hardware decoder requires a minimum amount of padding
     * in the coding unit payload */
    if (ctx->nitris_compat)
        ctx->min_padding = 1600;

    if ((ret = dnxhd_init_vlc(ctx)) < 0)
        return ret;
    if ((ret = dnxhd_init_rc(ctx)) < 0)
        return ret;

    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->slice_size,
                      ctx->m.mb_height * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->slice_offs,
                      ctx->m.mb_height * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_bits,
                      ctx->m.mb_num * sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_qscale,
                      ctx->m.mb_num * sizeof(uint8_t), fail);

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (avctx->active_thread_type == FF_THREAD_SLICE) {
        if (avctx->thread_count > MAX_THREADS) {
            av_log(avctx, AV_LOG_ERROR, "too many threads\n");
            return AVERROR(EINVAL);
        }
    }

    if (avctx->qmax <= 1) {
        av_log(avctx, AV_LOG_ERROR, "qmax must be at least 2\n");
        return AVERROR(EINVAL);
    }

    ctx->thread[0] = ctx;
    if (avctx->active_thread_type == FF_THREAD_SLICE) {
        for (i = 1; i < avctx->thread_count; i++) {
            ctx->thread[i] = av_malloc(sizeof(DNXHDEncContext));
            memcpy(ctx->thread[i], ctx, sizeof(DNXHDEncContext));
        }
    }

    return 0;
fail:  // for FF_ALLOCZ_OR_GOTO
    return AVERROR(ENOMEM);
}

static int dnxhd_write_header(AVCodecContext *avctx, uint8_t *buf)
{
    DNXHDEncContext *ctx = avctx->priv_data;

    memset(buf, 0, ctx->data_offset);

    // * write prefix */
    AV_WB16(buf + 0x02, ctx->data_offset);
    if (ctx->cid >= 1270 && ctx->cid <= 1274)
        buf[4] = 0x03;
    else
        buf[4] = 0x01;

    buf[5] = ctx->interlaced ? ctx->cur_field + 2 : 0x01;
    buf[6] = 0x80; // crc flag off
    buf[7] = 0xa0; // reserved
    AV_WB16(buf + 0x18, avctx->height >> ctx->interlaced); // ALPF
    AV_WB16(buf + 0x1a, avctx->width);  // SPL
    AV_WB16(buf + 0x1d, avctx->height >> ctx->interlaced); // NAL

    buf[0x21] = ctx->bit_depth == 10 ? 0x58 : 0x38;
    buf[0x22] = 0x88 + (ctx->interlaced << 2);
    AV_WB32(buf + 0x28, ctx->cid); // CID
    buf[0x2c] = (!ctx->interlaced << 7) | (ctx->is_444 << 6) | (avctx->pix_fmt == AV_PIX_FMT_YUV444P10);

    buf[0x5f] = 0x01; // UDL

    buf[0x167] = 0x02; // reserved
    AV_WB16(buf + 0x16a, ctx->m.mb_height * 4 + 4); // MSIPS
    AV_WB16(buf + 0x16c, ctx->m.mb_height); // Ns
    buf[0x16f] = 0x10; // reserved

    ctx->msip = buf + 0x170;
    return 0;
}

static av_always_inline void dnxhd_encode_dc(DNXHDEncContext *ctx, int diff)
{
    int nbits;
    if (diff < 0) {
        nbits = av_log2_16bit(-2 * diff);
        diff--;
    } else {
        nbits = av_log2_16bit(2 * diff);
    }
    put_bits(&ctx->m.pb, ctx->cid_table->dc_bits[nbits] + nbits,
             (ctx->cid_table->dc_codes[nbits] << nbits) +
             av_mod_uintp2(diff, nbits));
}

static av_always_inline
void dnxhd_encode_block(DNXHDEncContext *ctx, int16_t *block,
                        int last_index, int n)
{
    int last_non_zero = 0;
    int slevel, i, j;

    dnxhd_encode_dc(ctx, block[0] - ctx->m.last_dc[n]);
    ctx->m.last_dc[n] = block[0];

    for (i = 1; i <= last_index; i++) {
        j = ctx->m.intra_scantable.permutated[i];
        slevel = block[j];
        if (slevel) {
            int run_level = i - last_non_zero - 1;
            int rlevel = (slevel << 1) | !!run_level;
            put_bits(&ctx->m.pb, ctx->vlc_bits[rlevel], ctx->vlc_codes[rlevel]);
            if (run_level)
                put_bits(&ctx->m.pb, ctx->run_bits[run_level],
                         ctx->run_codes[run_level]);
            last_non_zero = i;
        }
    }
    put_bits(&ctx->m.pb, ctx->vlc_bits[0], ctx->vlc_codes[0]); // EOB
}

static av_always_inline
void dnxhd_unquantize_c(DNXHDEncContext *ctx, int16_t *block, int n,
                        int qscale, int last_index)
{
    const uint8_t *weight_matrix;
    int level;
    int i;

    if (ctx->is_444) {
        weight_matrix = ((n % 6) < 2) ? ctx->cid_table->luma_weight
                                      : ctx->cid_table->chroma_weight;
    } else {
        weight_matrix = (n & 2) ? ctx->cid_table->chroma_weight
                                : ctx->cid_table->luma_weight;
    }

    for (i = 1; i <= last_index; i++) {
        int j = ctx->m.intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = (1 - 2 * level) * qscale * weight_matrix[i];
                if (ctx->bit_depth == 10) {
                    if (weight_matrix[i] != 8)
                        level += 8;
                    level >>= 4;
                } else {
                    if (weight_matrix[i] != 32)
                        level += 32;
                    level >>= 6;
                }
                level = -level;
            } else {
                level = (2 * level + 1) * qscale * weight_matrix[i];
                if (ctx->bit_depth == 10) {
                    if (weight_matrix[i] != 8)
                        level += 8;
                    level >>= 4;
                } else {
                    if (weight_matrix[i] != 32)
                        level += 32;
                    level >>= 6;
                }
            }
            block[j] = level;
        }
    }
}

static av_always_inline int dnxhd_ssd_block(int16_t *qblock, int16_t *block)
{
    int score = 0;
    int i;
    for (i = 0; i < 64; i++)
        score += (block[i] - qblock[i]) * (block[i] - qblock[i]);
    return score;
}

static av_always_inline
int dnxhd_calc_ac_bits(DNXHDEncContext *ctx, int16_t *block, int last_index)
{
    int last_non_zero = 0;
    int bits = 0;
    int i, j, level;
    for (i = 1; i <= last_index; i++) {
        j = ctx->m.intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            int run_level = i - last_non_zero - 1;
            bits += ctx->vlc_bits[(level << 1) |
                    !!run_level] + ctx->run_bits[run_level];
            last_non_zero = i;
        }
    }
    return bits;
}

static av_always_inline
void dnxhd_get_blocks(DNXHDEncContext *ctx, int mb_x, int mb_y)
{
    const int bs = ctx->block_width_l2;
    const int bw = 1 << bs;
    int dct_y_offset = ctx->dct_y_offset;
    int dct_uv_offset = ctx->dct_uv_offset;
    int linesize = ctx->m.linesize;
    int uvlinesize = ctx->m.uvlinesize;
    const uint8_t *ptr_y = ctx->thread[0]->src[0] +
                           ((mb_y << 4) * ctx->m.linesize) + (mb_x << bs + 1);
    const uint8_t *ptr_u = ctx->thread[0]->src[1] +
                           ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << bs + ctx->is_444);
    const uint8_t *ptr_v = ctx->thread[0]->src[2] +
                           ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << bs + ctx->is_444);
    PixblockDSPContext *pdsp = &ctx->m.pdsp;
    VideoDSPContext *vdsp = &ctx->m.vdsp;

    if (ctx->bit_depth != 10 && vdsp->emulated_edge_mc && ((mb_x << 4) + 16 > ctx->m.avctx->width ||
                                                           (mb_y << 4) + 16 > ctx->m.avctx->height)) {
        int y_w = ctx->m.avctx->width  - (mb_x << 4);
        int y_h = ctx->m.avctx->height - (mb_y << 4);
        int uv_w = (y_w + 1) / 2;
        int uv_h = y_h;
        linesize = 16;
        uvlinesize = 8;

        vdsp->emulated_edge_mc(&ctx->edge_buf_y[0], ptr_y,
                               linesize, ctx->m.linesize,
                               linesize, 16,
                               0, 0, y_w, y_h);
        vdsp->emulated_edge_mc(&ctx->edge_buf_uv[0][0], ptr_u,
                               uvlinesize, ctx->m.uvlinesize,
                               uvlinesize, 16,
                               0, 0, uv_w, uv_h);
        vdsp->emulated_edge_mc(&ctx->edge_buf_uv[1][0], ptr_v,
                               uvlinesize, ctx->m.uvlinesize,
                               uvlinesize, 16,
                               0, 0, uv_w, uv_h);

        dct_y_offset =  bw * linesize;
        dct_uv_offset = bw * uvlinesize;
        ptr_y = &ctx->edge_buf_y[0];
        ptr_u = &ctx->edge_buf_uv[0][0];
        ptr_v = &ctx->edge_buf_uv[1][0];
    } else if (ctx->bit_depth == 10 && vdsp->emulated_edge_mc && ((mb_x << 4) + 16 > ctx->m.avctx->width ||
                                                                  (mb_y << 4) + 16 > ctx->m.avctx->height)) {
        int y_w = ctx->m.avctx->width  - (mb_x << 4);
        int y_h = ctx->m.avctx->height - (mb_y << 4);
        int uv_w = ctx->is_444 ? y_w : (y_w + 1) / 2;
        int uv_h = y_h;
        linesize = 32;
        uvlinesize = 16 + 16 * ctx->is_444;

        vdsp->emulated_edge_mc(&ctx->edge_buf_y[0], ptr_y,
                               linesize, ctx->m.linesize,
                               linesize / 2, 16,
                               0, 0, y_w, y_h);
        vdsp->emulated_edge_mc(&ctx->edge_buf_uv[0][0], ptr_u,
                               uvlinesize, ctx->m.uvlinesize,
                               uvlinesize / 2, 16,
                               0, 0, uv_w, uv_h);
        vdsp->emulated_edge_mc(&ctx->edge_buf_uv[1][0], ptr_v,
                               uvlinesize, ctx->m.uvlinesize,
                               uvlinesize / 2, 16,
                               0, 0, uv_w, uv_h);

        dct_y_offset =  bw * linesize / 2;
        dct_uv_offset = bw * uvlinesize / 2;
        ptr_y = &ctx->edge_buf_y[0];
        ptr_u = &ctx->edge_buf_uv[0][0];
        ptr_v = &ctx->edge_buf_uv[1][0];
    }

    if (!ctx->is_444) {
        pdsp->get_pixels(ctx->blocks[0], ptr_y,      linesize);
        pdsp->get_pixels(ctx->blocks[1], ptr_y + bw, linesize);
        pdsp->get_pixels(ctx->blocks[2], ptr_u,      uvlinesize);
        pdsp->get_pixels(ctx->blocks[3], ptr_v,      uvlinesize);

        if (mb_y + 1 == ctx->m.mb_height && ctx->m.avctx->height == 1080) {
            if (ctx->interlaced) {
                ctx->get_pixels_8x4_sym(ctx->blocks[4],
                                        ptr_y + dct_y_offset,
                                        linesize);
                ctx->get_pixels_8x4_sym(ctx->blocks[5],
                                        ptr_y + dct_y_offset + bw,
                                        linesize);
                ctx->get_pixels_8x4_sym(ctx->blocks[6],
                                        ptr_u + dct_uv_offset,
                                        uvlinesize);
                ctx->get_pixels_8x4_sym(ctx->blocks[7],
                                        ptr_v + dct_uv_offset,
                                        uvlinesize);
            } else {
                ctx->bdsp.clear_block(ctx->blocks[4]);
                ctx->bdsp.clear_block(ctx->blocks[5]);
                ctx->bdsp.clear_block(ctx->blocks[6]);
                ctx->bdsp.clear_block(ctx->blocks[7]);
            }
        } else {
            pdsp->get_pixels(ctx->blocks[4],
                             ptr_y + dct_y_offset, linesize);
            pdsp->get_pixels(ctx->blocks[5],
                             ptr_y + dct_y_offset + bw, linesize);
            pdsp->get_pixels(ctx->blocks[6],
                             ptr_u + dct_uv_offset, uvlinesize);
            pdsp->get_pixels(ctx->blocks[7],
                             ptr_v + dct_uv_offset, uvlinesize);
        }
    } else {
        pdsp->get_pixels(ctx->blocks[0], ptr_y,      linesize);
        pdsp->get_pixels(ctx->blocks[1], ptr_y + bw, linesize);
        pdsp->get_pixels(ctx->blocks[6], ptr_y + dct_y_offset, linesize);
        pdsp->get_pixels(ctx->blocks[7], ptr_y + dct_y_offset + bw, linesize);

        pdsp->get_pixels(ctx->blocks[2], ptr_u,      uvlinesize);
        pdsp->get_pixels(ctx->blocks[3], ptr_u + bw, uvlinesize);
        pdsp->get_pixels(ctx->blocks[8], ptr_u + dct_uv_offset, uvlinesize);
        pdsp->get_pixels(ctx->blocks[9], ptr_u + dct_uv_offset + bw, uvlinesize);

        pdsp->get_pixels(ctx->blocks[4], ptr_v,      uvlinesize);
        pdsp->get_pixels(ctx->blocks[5], ptr_v + bw, uvlinesize);
        pdsp->get_pixels(ctx->blocks[10], ptr_v + dct_uv_offset, uvlinesize);
        pdsp->get_pixels(ctx->blocks[11], ptr_v + dct_uv_offset + bw, uvlinesize);
    }
}

static av_always_inline
int dnxhd_switch_matrix(DNXHDEncContext *ctx, int i)
{
    int x;

    if (ctx->is_444) {
        x = (i >> 1) % 3;
    } else {
        const static uint8_t component[8]={0,0,1,2,0,0,1,2};
        x = component[i];
    }
    return x;
}

static int dnxhd_calc_bits_thread(AVCodecContext *avctx, void *arg,
                                  int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x;
    int qscale = ctx->qscale;
    LOCAL_ALIGNED_16(int16_t, block, [64]);
    ctx = ctx->thread[threadnr];

    ctx->m.last_dc[0] =
    ctx->m.last_dc[1] =
    ctx->m.last_dc[2] = 1 << (ctx->bit_depth + 2);

    for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
        unsigned mb = mb_y * ctx->m.mb_width + mb_x;
        int ssd     = 0;
        int ac_bits = 0;
        int dc_bits = 0;
        int i;

        dnxhd_get_blocks(ctx, mb_x, mb_y);

        for (i = 0; i < 8 + 4 * ctx->is_444; i++) {
            int16_t *src_block = ctx->blocks[i];
            int overflow, nbits, diff, last_index;
            int n = dnxhd_switch_matrix(ctx, i);

            memcpy(block, src_block, 64 * sizeof(*block));
            last_index = ctx->m.dct_quantize(&ctx->m, block,
                                             ctx->is_444 ? 4 * (n > 0): 4 & (2*i),
                                             qscale, &overflow);
            ac_bits   += dnxhd_calc_ac_bits(ctx, block, last_index);

            diff = block[0] - ctx->m.last_dc[n];
            if (diff < 0)
                nbits = av_log2_16bit(-2 * diff);
            else
                nbits = av_log2_16bit(2 * diff);

            av_assert1(nbits < ctx->bit_depth + 4);
            dc_bits += ctx->cid_table->dc_bits[nbits] + nbits;

            ctx->m.last_dc[n] = block[0];

            if (avctx->mb_decision == FF_MB_DECISION_RD || !RC_VARIANCE) {
                dnxhd_unquantize_c(ctx, block, i, qscale, last_index);
                ctx->m.idsp.idct(block);
                ssd += dnxhd_ssd_block(block, src_block);
            }
        }
        ctx->mb_rc[(qscale * ctx->m.mb_num) + mb].ssd  = ssd;
        ctx->mb_rc[(qscale * ctx->m.mb_num) + mb].bits = ac_bits + dc_bits + 12 +
                                     (1 + ctx->is_444) * 8 * ctx->vlc_bits[0];
    }
    return 0;
}

static int dnxhd_encode_thread(AVCodecContext *avctx, void *arg,
                               int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x;
    ctx = ctx->thread[threadnr];
    init_put_bits(&ctx->m.pb, (uint8_t *)arg + ctx->data_offset + ctx->slice_offs[jobnr],
                  ctx->slice_size[jobnr]);

    ctx->m.last_dc[0] =
    ctx->m.last_dc[1] =
    ctx->m.last_dc[2] = 1 << (ctx->bit_depth + 2);
    for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
        unsigned mb = mb_y * ctx->m.mb_width + mb_x;
        int qscale = ctx->mb_qscale[mb];
        int i;

        put_bits(&ctx->m.pb, 11, qscale);
        put_bits(&ctx->m.pb, 1, avctx->pix_fmt == AV_PIX_FMT_YUV444P10);

        dnxhd_get_blocks(ctx, mb_x, mb_y);

        for (i = 0; i < 8 + 4 * ctx->is_444; i++) {
            int16_t *block = ctx->blocks[i];
            int overflow, n = dnxhd_switch_matrix(ctx, i);
            int last_index = ctx->m.dct_quantize(&ctx->m, block,
                                                 ctx->is_444 ? (((i >> 1) % 3) < 1 ? 0 : 4): 4 & (2*i),
                                                 qscale, &overflow);
            // START_TIMER;
            dnxhd_encode_block(ctx, block, last_index, n);
            // STOP_TIMER("encode_block");
        }
    }
    if (put_bits_count(&ctx->m.pb) & 31)
        put_bits(&ctx->m.pb, 32 - (put_bits_count(&ctx->m.pb) & 31), 0);
    flush_put_bits(&ctx->m.pb);
    return 0;
}

static void dnxhd_setup_threads_slices(DNXHDEncContext *ctx)
{
    int mb_y, mb_x;
    int offset = 0;
    for (mb_y = 0; mb_y < ctx->m.mb_height; mb_y++) {
        int thread_size;
        ctx->slice_offs[mb_y] = offset;
        ctx->slice_size[mb_y] = 0;
        for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
            unsigned mb = mb_y * ctx->m.mb_width + mb_x;
            ctx->slice_size[mb_y] += ctx->mb_bits[mb];
        }
        ctx->slice_size[mb_y]   = (ctx->slice_size[mb_y] + 31) & ~31;
        ctx->slice_size[mb_y] >>= 3;
        thread_size = ctx->slice_size[mb_y];
        offset += thread_size;
    }
}

static int dnxhd_mb_var_thread(AVCodecContext *avctx, void *arg,
                               int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x, x, y;
    int partial_last_row = (mb_y == ctx->m.mb_height - 1) &&
                           ((avctx->height >> ctx->interlaced) & 0xF);

    ctx = ctx->thread[threadnr];
    if (ctx->bit_depth == 8) {
        uint8_t *pix = ctx->thread[0]->src[0] + ((mb_y << 4) * ctx->m.linesize);
        for (mb_x = 0; mb_x < ctx->m.mb_width; ++mb_x, pix += 16) {
            unsigned mb = mb_y * ctx->m.mb_width + mb_x;
            int sum;
            int varc;

            if (!partial_last_row && mb_x * 16 <= avctx->width - 16 && (avctx->width % 16) == 0) {
                sum  = ctx->m.mpvencdsp.pix_sum(pix, ctx->m.linesize);
                varc = ctx->m.mpvencdsp.pix_norm1(pix, ctx->m.linesize);
            } else {
                int bw = FFMIN(avctx->width - 16 * mb_x, 16);
                int bh = FFMIN((avctx->height >> ctx->interlaced) - 16 * mb_y, 16);
                sum = varc = 0;
                for (y = 0; y < bh; y++) {
                    for (x = 0; x < bw; x++) {
                        uint8_t val = pix[x + y * ctx->m.linesize];
                        sum  += val;
                        varc += val * val;
                    }
                }
            }
            varc = (varc - (((unsigned) sum * sum) >> 8) + 128) >> 8;

            ctx->mb_cmp[mb].value = varc;
            ctx->mb_cmp[mb].mb    = mb;
        }
    } else { // 10-bit
        const int linesize = ctx->m.linesize >> 1;
        for (mb_x = 0; mb_x < ctx->m.mb_width; ++mb_x) {
            uint16_t *pix = (uint16_t *)ctx->thread[0]->src[0] +
                            ((mb_y << 4) * linesize) + (mb_x << 4);
            unsigned mb  = mb_y * ctx->m.mb_width + mb_x;
            int sum = 0;
            int sqsum = 0;
            int bw = FFMIN(avctx->width - 16 * mb_x, 16);
            int bh = FFMIN((avctx->height >> ctx->interlaced) - 16 * mb_y, 16);
            int mean, sqmean;
            int i, j;
            // Macroblocks are 16x16 pixels, unlike DCT blocks which are 8x8.
            for (i = 0; i < bh; ++i) {
                for (j = 0; j < bw; ++j) {
                    // Turn 16-bit pixels into 10-bit ones.
                    const int sample = (unsigned) pix[j] >> 6;
                    sum   += sample;
                    sqsum += sample * sample;
                    // 2^10 * 2^10 * 16 * 16 = 2^28, which is less than INT_MAX
                }
                pix += linesize;
            }
            mean = sum >> 8; // 16*16 == 2^8
            sqmean = sqsum >> 8;
            ctx->mb_cmp[mb].value = sqmean - mean * mean;
            ctx->mb_cmp[mb].mb    = mb;
        }
    }
    return 0;
}

static int dnxhd_encode_rdo(AVCodecContext *avctx, DNXHDEncContext *ctx)
{
    int lambda, up_step, down_step;
    int last_lower = INT_MAX, last_higher = 0;
    int x, y, q;

    for (q = 1; q < avctx->qmax; q++) {
        ctx->qscale = q;
        avctx->execute2(avctx, dnxhd_calc_bits_thread,
                        NULL, NULL, ctx->m.mb_height);
    }
    up_step = down_step = 2 << LAMBDA_FRAC_BITS;
    lambda  = ctx->lambda;

    for (;;) {
        int bits = 0;
        int end  = 0;
        if (lambda == last_higher) {
            lambda++;
            end = 1; // need to set final qscales/bits
        }
        for (y = 0; y < ctx->m.mb_height; y++) {
            for (x = 0; x < ctx->m.mb_width; x++) {
                unsigned min = UINT_MAX;
                int qscale = 1;
                int mb     = y * ctx->m.mb_width + x;
                int rc = 0;
                for (q = 1; q < avctx->qmax; q++) {
                    int i = (q*ctx->m.mb_num) + mb;
                    unsigned score = ctx->mb_rc[i].bits * lambda +
                                     ((unsigned) ctx->mb_rc[i].ssd << LAMBDA_FRAC_BITS);
                    if (score < min) {
                        min    = score;
                        qscale = q;
                        rc = i;
                    }
                }
                bits += ctx->mb_rc[rc].bits;
                ctx->mb_qscale[mb] = qscale;
                ctx->mb_bits[mb]   = ctx->mb_rc[rc].bits;
            }
            bits = (bits + 31) & ~31; // padding
            if (bits > ctx->frame_bits)
                break;
        }
        if (end) {
            if (bits > ctx->frame_bits)
                return AVERROR(EINVAL);
            break;
        }
        if (bits < ctx->frame_bits) {
            last_lower = FFMIN(lambda, last_lower);
            if (last_higher != 0)
                lambda = (lambda+last_higher)>>1;
            else
                lambda -= down_step;
            down_step = FFMIN((int64_t)down_step*5, INT_MAX);
            up_step = 1<<LAMBDA_FRAC_BITS;
            lambda = FFMAX(1, lambda);
            if (lambda == last_lower)
                break;
        } else {
            last_higher = FFMAX(lambda, last_higher);
            if (last_lower != INT_MAX)
                lambda = (lambda+last_lower)>>1;
            else if ((int64_t)lambda + up_step > INT_MAX)
                return AVERROR(EINVAL);
            else
                lambda += up_step;
            up_step = FFMIN((int64_t)up_step*5, INT_MAX);
            down_step = 1<<LAMBDA_FRAC_BITS;
        }
    }
    ctx->lambda = lambda;
    return 0;
}

static int dnxhd_find_qscale(DNXHDEncContext *ctx)
{
    int bits = 0;
    int up_step = 1;
    int down_step = 1;
    int last_higher = 0;
    int last_lower = INT_MAX;
    int qscale;
    int x, y;

    qscale = ctx->qscale;
    for (;;) {
        bits = 0;
        ctx->qscale = qscale;
        // XXX avoid recalculating bits
        ctx->m.avctx->execute2(ctx->m.avctx, dnxhd_calc_bits_thread,
                               NULL, NULL, ctx->m.mb_height);
        for (y = 0; y < ctx->m.mb_height; y++) {
            for (x = 0; x < ctx->m.mb_width; x++)
                bits += ctx->mb_rc[(qscale*ctx->m.mb_num) + (y*ctx->m.mb_width+x)].bits;
            bits = (bits+31)&~31; // padding
            if (bits > ctx->frame_bits)
                break;
        }
        if (bits < ctx->frame_bits) {
            if (qscale == 1)
                return 1;
            if (last_higher == qscale - 1) {
                qscale = last_higher;
                break;
            }
            last_lower = FFMIN(qscale, last_lower);
            if (last_higher != 0)
                qscale = (qscale + last_higher) >> 1;
            else
                qscale -= down_step++;
            if (qscale < 1)
                qscale = 1;
            up_step = 1;
        } else {
            if (last_lower == qscale + 1)
                break;
            last_higher = FFMAX(qscale, last_higher);
            if (last_lower != INT_MAX)
                qscale = (qscale + last_lower) >> 1;
            else
                qscale += up_step++;
            down_step = 1;
            if (qscale >= ctx->m.avctx->qmax)
                return AVERROR(EINVAL);
        }
    }
    ctx->qscale = qscale;
    return 0;
}

#define BUCKET_BITS 8
#define RADIX_PASSES 4
#define NBUCKETS (1 << BUCKET_BITS)

static inline int get_bucket(int value, int shift)
{
    value >>= shift;
    value  &= NBUCKETS - 1;
    return NBUCKETS - 1 - value;
}

static void radix_count(const RCCMPEntry *data, int size,
                        int buckets[RADIX_PASSES][NBUCKETS])
{
    int i, j;
    memset(buckets, 0, sizeof(buckets[0][0]) * RADIX_PASSES * NBUCKETS);
    for (i = 0; i < size; i++) {
        int v = data[i].value;
        for (j = 0; j < RADIX_PASSES; j++) {
            buckets[j][get_bucket(v, 0)]++;
            v >>= BUCKET_BITS;
        }
        av_assert1(!v);
    }
    for (j = 0; j < RADIX_PASSES; j++) {
        int offset = size;
        for (i = NBUCKETS - 1; i >= 0; i--)
            buckets[j][i] = offset -= buckets[j][i];
        av_assert1(!buckets[j][0]);
    }
}

static void radix_sort_pass(RCCMPEntry *dst, const RCCMPEntry *data,
                            int size, int buckets[NBUCKETS], int pass)
{
    int shift = pass * BUCKET_BITS;
    int i;
    for (i = 0; i < size; i++) {
        int v   = get_bucket(data[i].value, shift);
        int pos = buckets[v]++;
        dst[pos] = data[i];
    }
}

static void radix_sort(RCCMPEntry *data, RCCMPEntry *tmp, int size)
{
    int buckets[RADIX_PASSES][NBUCKETS];
    radix_count(data, size, buckets);
    radix_sort_pass(tmp, data, size, buckets[0], 0);
    radix_sort_pass(data, tmp, size, buckets[1], 1);
    if (buckets[2][NBUCKETS - 1] || buckets[3][NBUCKETS - 1]) {
        radix_sort_pass(tmp, data, size, buckets[2], 2);
        radix_sort_pass(data, tmp, size, buckets[3], 3);
    }
}

static int dnxhd_encode_fast(AVCodecContext *avctx, DNXHDEncContext *ctx)
{
    int max_bits = 0;
    int ret, x, y;
    if ((ret = dnxhd_find_qscale(ctx)) < 0)
        return ret;
    for (y = 0; y < ctx->m.mb_height; y++) {
        for (x = 0; x < ctx->m.mb_width; x++) {
            int mb = y * ctx->m.mb_width + x;
            int rc = (ctx->qscale * ctx->m.mb_num ) + mb;
            int delta_bits;
            ctx->mb_qscale[mb] = ctx->qscale;
            ctx->mb_bits[mb] = ctx->mb_rc[rc].bits;
            max_bits += ctx->mb_rc[rc].bits;
            if (!RC_VARIANCE) {
                delta_bits = ctx->mb_rc[rc].bits -
                             ctx->mb_rc[rc + ctx->m.mb_num].bits;
                ctx->mb_cmp[mb].mb = mb;
                ctx->mb_cmp[mb].value =
                    delta_bits ? ((ctx->mb_rc[rc].ssd -
                                   ctx->mb_rc[rc + ctx->m.mb_num].ssd) * 100) /
                                  delta_bits
                               : INT_MIN; // avoid increasing qscale
            }
        }
        max_bits += 31; // worst padding
    }
    if (!ret) {
        if (RC_VARIANCE)
            avctx->execute2(avctx, dnxhd_mb_var_thread,
                            NULL, NULL, ctx->m.mb_height);
        radix_sort(ctx->mb_cmp, ctx->mb_cmp_tmp, ctx->m.mb_num);
        for (x = 0; x < ctx->m.mb_num && max_bits > ctx->frame_bits; x++) {
            int mb = ctx->mb_cmp[x].mb;
            int rc = (ctx->qscale * ctx->m.mb_num ) + mb;
            max_bits -= ctx->mb_rc[rc].bits -
                        ctx->mb_rc[rc + ctx->m.mb_num].bits;
            ctx->mb_qscale[mb] = ctx->qscale + 1;
            ctx->mb_bits[mb]   = ctx->mb_rc[rc + ctx->m.mb_num].bits;
        }
    }
    return 0;
}

static void dnxhd_load_picture(DNXHDEncContext *ctx, const AVFrame *frame)
{
    int i;

    for (i = 0; i < ctx->m.avctx->thread_count; i++) {
        ctx->thread[i]->m.linesize    = frame->linesize[0] << ctx->interlaced;
        ctx->thread[i]->m.uvlinesize  = frame->linesize[1] << ctx->interlaced;
        ctx->thread[i]->dct_y_offset  = ctx->m.linesize  *8;
        ctx->thread[i]->dct_uv_offset = ctx->m.uvlinesize*8;
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    ctx->m.avctx->coded_frame->interlaced_frame = frame->interlaced_frame;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    ctx->cur_field = frame->interlaced_frame && !frame->top_field_first;
}

static int dnxhd_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int first_field = 1;
    int offset, i, ret;
    uint8_t *buf;

    if ((ret = ff_alloc_packet2(avctx, pkt, ctx->frame_size, 0)) < 0)
        return ret;
    buf = pkt->data;

    dnxhd_load_picture(ctx, frame);

encode_coding_unit:
    for (i = 0; i < 3; i++) {
        ctx->src[i] = frame->data[i];
        if (ctx->interlaced && ctx->cur_field)
            ctx->src[i] += frame->linesize[i];
    }

    dnxhd_write_header(avctx, buf);

    if (avctx->mb_decision == FF_MB_DECISION_RD)
        ret = dnxhd_encode_rdo(avctx, ctx);
    else
        ret = dnxhd_encode_fast(avctx, ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "picture could not fit ratecontrol constraints, increase qmax\n");
        return ret;
    }

    dnxhd_setup_threads_slices(ctx);

    offset = 0;
    for (i = 0; i < ctx->m.mb_height; i++) {
        AV_WB32(ctx->msip + i * 4, offset);
        offset += ctx->slice_size[i];
        av_assert1(!(ctx->slice_size[i] & 3));
    }

    avctx->execute2(avctx, dnxhd_encode_thread, buf, NULL, ctx->m.mb_height);

    av_assert1(ctx->data_offset + offset + 4 <= ctx->coding_unit_size);
    memset(buf + ctx->data_offset + offset, 0,
           ctx->coding_unit_size - 4 - offset - ctx->data_offset);

    AV_WB32(buf + ctx->coding_unit_size - 4, 0x600DC0DE); // EOF

    if (ctx->interlaced && first_field) {
        first_field     = 0;
        ctx->cur_field ^= 1;
        buf            += ctx->coding_unit_size;
        goto encode_coding_unit;
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->quality = ctx->qscale * FF_QP2LAMBDA;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ff_side_data_set_encoder_stats(pkt, ctx->qscale * FF_QP2LAMBDA, NULL, 0, AV_PICTURE_TYPE_I);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int dnxhd_encode_end(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int i;

    av_freep(&ctx->orig_vlc_codes);
    av_freep(&ctx->orig_vlc_bits);
    av_freep(&ctx->run_codes);
    av_freep(&ctx->run_bits);

    av_freep(&ctx->mb_bits);
    av_freep(&ctx->mb_qscale);
    av_freep(&ctx->mb_rc);
    av_freep(&ctx->mb_cmp);
    av_freep(&ctx->mb_cmp_tmp);
    av_freep(&ctx->slice_size);
    av_freep(&ctx->slice_offs);

    av_freep(&ctx->qmatrix_c);
    av_freep(&ctx->qmatrix_l);
    av_freep(&ctx->qmatrix_c16);
    av_freep(&ctx->qmatrix_l16);

    if (avctx->active_thread_type == FF_THREAD_SLICE) {
        for (i = 1; i < avctx->thread_count; i++)
            av_freep(&ctx->thread[i]);
    }

    return 0;
}

static const AVCodecDefault dnxhd_defaults[] = {
    { "qmax", "1024" }, /* Maximum quantization scale factor allowed for VC-3 */
    { NULL },
};

AVCodec ff_dnxhd_encoder = {
    .name           = "dnxhd",
    .long_name      = NULL_IF_CONFIG_SMALL("VC3/DNxHD"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DNXHD,
    .priv_data_size = sizeof(DNXHDEncContext),
    .init           = dnxhd_encode_init,
    .encode2        = dnxhd_encode_picture,
    .close          = dnxhd_encode_end,
    .capabilities   = AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_INTRA_ONLY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV422P10,
        AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_NONE
    },
    .priv_class     = &dnxhd_class,
    .defaults       = dnxhd_defaults,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_dnxhd_profiles),
};
