/*
 * VC3/DNxHD encoder
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 * Copyright (c) 2011 MirriAd Ltd
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
 * 10 bit support added by MirriAd Ltd, Joseph Artsimovich <joseph@mirriad.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

//#define DEBUG
#define RC_VARIANCE 1 // use variance or ssd for fast rc

#include "libavutil/opt.h"
#include "avcodec.h"
#include "dsputil.h"
#include "internal.h"
#include "mpegvideo.h"
#include "mpegvideo_common.h"
#include "dnxhdenc.h"

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define DNX10BIT_QMAT_SHIFT 18 // The largest value that will not lead to overflow for 10bit samples.

static const AVOption options[]={
    {"nitris_compat", "encode with Avid Nitris compatibility", offsetof(DNXHDEncContext, nitris_compat), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 1, VE},
{NULL}
};
static const AVClass class = { "dnxhd", av_default_item_name, options, LIBAVUTIL_VERSION_INT };

#define LAMBDA_FRAC_BITS 10

static void dnxhd_8bit_get_pixels_8x4_sym(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
{
    int i;
    for (i = 0; i < 4; i++) {
        block[0] = pixels[0]; block[1] = pixels[1];
        block[2] = pixels[2]; block[3] = pixels[3];
        block[4] = pixels[4]; block[5] = pixels[5];
        block[6] = pixels[6]; block[7] = pixels[7];
        pixels += line_size;
        block += 8;
    }
    memcpy(block,      block -  8, sizeof(*block) * 8);
    memcpy(block +  8, block - 16, sizeof(*block) * 8);
    memcpy(block + 16, block - 24, sizeof(*block) * 8);
    memcpy(block + 24, block - 32, sizeof(*block) * 8);
}

static av_always_inline void dnxhd_10bit_get_pixels_8x4_sym(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
{
    int i;

    block += 32;

    for (i = 0; i < 4; i++) {
        memcpy(block + i     * 8, pixels + i * line_size, 8 * sizeof(*block));
        memcpy(block - (i+1) * 8, pixels + i * line_size, 8 * sizeof(*block));
    }
}

static int dnxhd_10bit_dct_quantize(MpegEncContext *ctx, DCTELEM *block,
                                    int n, int qscale, int *overflow)
{
    const uint8_t *scantable= ctx->intra_scantable.scantable;
    const int *qmat = ctx->q_intra_matrix[qscale];
    int last_non_zero = 0;
    int i;

    ctx->dsp.fdct(block);

    // Divide by 4 with rounding, to compensate scaling of DCT coefficients
    block[0] = (block[0] + 2) >> 2;

    for (i = 1; i < 64; ++i) {
        int j = scantable[i];
        int sign = block[j] >> 31;
        int level = (block[j] ^ sign) - sign;
        level = level * qmat[j] >> DNX10BIT_QMAT_SHIFT;
        block[j] = (level ^ sign) - sign;
        if (level)
            last_non_zero = i;
    }

    return last_non_zero;
}

static int dnxhd_init_vlc(DNXHDEncContext *ctx)
{
    int i, j, level, run;
    int max_level = 1<<(ctx->cid_table->bit_depth+2);

    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->vlc_codes, max_level*4*sizeof(*ctx->vlc_codes), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->vlc_bits,  max_level*4*sizeof(*ctx->vlc_bits) , fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->run_codes, 63*2,                                fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->run_bits,  63,                                  fail);

    ctx->vlc_codes += max_level*2;
    ctx->vlc_bits  += max_level*2;
    for (level = -max_level; level < max_level; level++) {
        for (run = 0; run < 2; run++) {
            int index = (level<<1)|run;
            int sign, offset = 0, alevel = level;

            MASK_ABS(sign, alevel);
            if (alevel > 64) {
                offset = (alevel-1)>>6;
                alevel -= offset<<6;
            }
            for (j = 0; j < 257; j++) {
                if (ctx->cid_table->ac_level[j] == alevel &&
                    (!offset || (ctx->cid_table->ac_index_flag[j] && offset)) &&
                    (!run    || (ctx->cid_table->ac_run_flag  [j] && run))) {
                    assert(!ctx->vlc_codes[index]);
                    if (alevel) {
                        ctx->vlc_codes[index] = (ctx->cid_table->ac_codes[j]<<1)|(sign&1);
                        ctx->vlc_bits [index] = ctx->cid_table->ac_bits[j]+1;
                    } else {
                        ctx->vlc_codes[index] = ctx->cid_table->ac_codes[j];
                        ctx->vlc_bits [index] = ctx->cid_table->ac_bits [j];
                    }
                    break;
                }
            }
            assert(!alevel || j < 257);
            if (offset) {
                ctx->vlc_codes[index] = (ctx->vlc_codes[index]<<ctx->cid_table->index_bits)|offset;
                ctx->vlc_bits [index]+= ctx->cid_table->index_bits;
            }
        }
    }
    for (i = 0; i < 62; i++) {
        int run = ctx->cid_table->run[i];
        assert(run < 63);
        ctx->run_codes[run] = ctx->cid_table->run_codes[i];
        ctx->run_bits [run] = ctx->cid_table->run_bits[i];
    }
    return 0;
 fail:
    return -1;
}

static int dnxhd_init_qmat(DNXHDEncContext *ctx, int lbias, int cbias)
{
    // init first elem to 1 to avoid div by 0 in convert_matrix
    uint16_t weight_matrix[64] = {1,}; // convert_matrix needs uint16_t*
    int qscale, i;
    const uint8_t *luma_weight_table   = ctx->cid_table->luma_weight;
    const uint8_t *chroma_weight_table = ctx->cid_table->chroma_weight;

    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->qmatrix_l,   (ctx->m.avctx->qmax+1) * 64 *     sizeof(int),      fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->qmatrix_c,   (ctx->m.avctx->qmax+1) * 64 *     sizeof(int),      fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->qmatrix_l16, (ctx->m.avctx->qmax+1) * 64 * 2 * sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->qmatrix_c16, (ctx->m.avctx->qmax+1) * 64 * 2 * sizeof(uint16_t), fail);

    if (ctx->cid_table->bit_depth == 8) {
        for (i = 1; i < 64; i++) {
            int j = ctx->m.dsp.idct_permutation[ff_zigzag_direct[i]];
            weight_matrix[j] = ctx->cid_table->luma_weight[i];
        }
        ff_convert_matrix(&ctx->m.dsp, ctx->qmatrix_l, ctx->qmatrix_l16, weight_matrix,
                          ctx->m.intra_quant_bias, 1, ctx->m.avctx->qmax, 1);
        for (i = 1; i < 64; i++) {
            int j = ctx->m.dsp.idct_permutation[ff_zigzag_direct[i]];
            weight_matrix[j] = ctx->cid_table->chroma_weight[i];
        }
        ff_convert_matrix(&ctx->m.dsp, ctx->qmatrix_c, ctx->qmatrix_c16, weight_matrix,
                          ctx->m.intra_quant_bias, 1, ctx->m.avctx->qmax, 1);

        for (qscale = 1; qscale <= ctx->m.avctx->qmax; qscale++) {
            for (i = 0; i < 64; i++) {
                ctx->qmatrix_l  [qscale]   [i] <<= 2; ctx->qmatrix_c  [qscale]   [i] <<= 2;
                ctx->qmatrix_l16[qscale][0][i] <<= 2; ctx->qmatrix_l16[qscale][1][i] <<= 2;
                ctx->qmatrix_c16[qscale][0][i] <<= 2; ctx->qmatrix_c16[qscale][1][i] <<= 2;
            }
        }
    } else {
        // 10-bit
        for (qscale = 1; qscale <= ctx->m.avctx->qmax; qscale++) {
            for (i = 1; i < 64; i++) {
                int j = ctx->m.dsp.idct_permutation[ff_zigzag_direct[i]];

                // The quantization formula from the VC-3 standard is:
                // quantized = sign(block[i]) * floor(abs(block[i]/s) * p / (qscale * weight_table[i]))
                // Where p is 32 for 8-bit samples and 8 for 10-bit ones.
                // The s factor compensates scaling of DCT coefficients done by the DCT routines,
                // and therefore is not present in standard.  It's 8 for 8-bit samples and 4 for 10-bit ones.
                // We want values of ctx->qtmatrix_l and ctx->qtmatrix_r to be:
                // ((1 << DNX10BIT_QMAT_SHIFT) * (p / s)) / (qscale * weight_table[i])
                // For 10-bit samples, p / s == 2
                ctx->qmatrix_l[qscale][j] = (1 << (DNX10BIT_QMAT_SHIFT + 1)) / (qscale * luma_weight_table[i]);
                ctx->qmatrix_c[qscale][j] = (1 << (DNX10BIT_QMAT_SHIFT + 1)) / (qscale * chroma_weight_table[i]);
            }
        }
    }

    return 0;
 fail:
    return -1;
}

static int dnxhd_init_rc(DNXHDEncContext *ctx)
{
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_rc, 8160*ctx->m.avctx->qmax*sizeof(RCEntry), fail);
    if (ctx->m.avctx->mb_decision != FF_MB_DECISION_RD)
        FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_cmp, ctx->m.mb_num*sizeof(RCCMPEntry), fail);

    ctx->frame_bits = (ctx->cid_table->coding_unit_size - 640 - 4 - ctx->min_padding) * 8;
    ctx->qscale = 1;
    ctx->lambda = 2<<LAMBDA_FRAC_BITS; // qscale 2
    return 0;
 fail:
    return -1;
}

static int dnxhd_encode_init(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int i, index, bit_depth;

    switch (avctx->pix_fmt) {
    case PIX_FMT_YUV422P:
        bit_depth = 8;
        break;
    case PIX_FMT_YUV422P10:
        bit_depth = 10;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "pixel format is incompatible with DNxHD\n");
        return -1;
    }

    ctx->cid = ff_dnxhd_find_cid(avctx, bit_depth);
    if (!ctx->cid) {
        av_log(avctx, AV_LOG_ERROR, "video parameters incompatible with DNxHD\n");
        return -1;
    }
    av_log(avctx, AV_LOG_DEBUG, "cid %d\n", ctx->cid);

    index = ff_dnxhd_get_cid_table(ctx->cid);
    ctx->cid_table = &ff_dnxhd_cid_table[index];

    ctx->m.avctx = avctx;
    ctx->m.mb_intra = 1;
    ctx->m.h263_aic = 1;

    avctx->bits_per_raw_sample = ctx->cid_table->bit_depth;

    ff_dsputil_init(&ctx->m.dsp, avctx);
    ff_dct_common_init(&ctx->m);
    if (!ctx->m.dct_quantize)
        ctx->m.dct_quantize = ff_dct_quantize_c;

    if (ctx->cid_table->bit_depth == 10) {
       ctx->m.dct_quantize = dnxhd_10bit_dct_quantize;
       ctx->get_pixels_8x4_sym = dnxhd_10bit_get_pixels_8x4_sym;
       ctx->block_width_l2 = 4;
    } else {
       ctx->get_pixels_8x4_sym = dnxhd_8bit_get_pixels_8x4_sym;
       ctx->block_width_l2 = 3;
    }

#if HAVE_MMX
    ff_dnxhd_init_mmx(ctx);
#endif

    ctx->m.mb_height = (avctx->height + 15) / 16;
    ctx->m.mb_width  = (avctx->width  + 15) / 16;

    if (avctx->flags & CODEC_FLAG_INTERLACED_DCT) {
        ctx->interlaced = 1;
        ctx->m.mb_height /= 2;
    }

    ctx->m.mb_num = ctx->m.mb_height * ctx->m.mb_width;

    if (avctx->intra_quant_bias != FF_DEFAULT_QUANT_BIAS)
        ctx->m.intra_quant_bias = avctx->intra_quant_bias;
    if (dnxhd_init_qmat(ctx, ctx->m.intra_quant_bias, 0) < 0) // XXX tune lbias/cbias
        return -1;

    // Avid Nitris hardware decoder requires a minimum amount of padding in the coding unit payload
    if (ctx->nitris_compat)
        ctx->min_padding = 1600;

    if (dnxhd_init_vlc(ctx) < 0)
        return -1;
    if (dnxhd_init_rc(ctx) < 0)
        return -1;

    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->slice_size, ctx->m.mb_height*sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->slice_offs, ctx->m.mb_height*sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_bits,    ctx->m.mb_num   *sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(ctx->m.avctx, ctx->mb_qscale,  ctx->m.mb_num   *sizeof(uint8_t),  fail);

    ctx->frame.key_frame = 1;
    ctx->frame.pict_type = AV_PICTURE_TYPE_I;
    ctx->m.avctx->coded_frame = &ctx->frame;

    if (avctx->thread_count > MAX_THREADS) {
        av_log(avctx, AV_LOG_ERROR, "too many threads\n");
        return -1;
    }

    ctx->thread[0] = ctx;
    for (i = 1; i < avctx->thread_count; i++) {
        ctx->thread[i] =  av_malloc(sizeof(DNXHDEncContext));
        memcpy(ctx->thread[i], ctx, sizeof(DNXHDEncContext));
    }

    return 0;
 fail: //for FF_ALLOCZ_OR_GOTO
    return -1;
}

static int dnxhd_write_header(AVCodecContext *avctx, uint8_t *buf)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    const uint8_t header_prefix[5] = { 0x00,0x00,0x02,0x80,0x01 };

    memset(buf, 0, 640);

    memcpy(buf, header_prefix, 5);
    buf[5] = ctx->interlaced ? ctx->cur_field+2 : 0x01;
    buf[6] = 0x80; // crc flag off
    buf[7] = 0xa0; // reserved
    AV_WB16(buf + 0x18, avctx->height>>ctx->interlaced); // ALPF
    AV_WB16(buf + 0x1a, avctx->width);  // SPL
    AV_WB16(buf + 0x1d, avctx->height>>ctx->interlaced); // NAL

    buf[0x21] = ctx->cid_table->bit_depth == 10 ? 0x58 : 0x38;
    buf[0x22] = 0x88 + (ctx->interlaced<<2);
    AV_WB32(buf + 0x28, ctx->cid); // CID
    buf[0x2c] = ctx->interlaced ? 0 : 0x80;

    buf[0x5f] = 0x01; // UDL

    buf[0x167] = 0x02; // reserved
    AV_WB16(buf + 0x16a, ctx->m.mb_height * 4 + 4); // MSIPS
    buf[0x16d] = ctx->m.mb_height; // Ns
    buf[0x16f] = 0x10; // reserved

    ctx->msip = buf + 0x170;
    return 0;
}

static av_always_inline void dnxhd_encode_dc(DNXHDEncContext *ctx, int diff)
{
    int nbits;
    if (diff < 0) {
        nbits = av_log2_16bit(-2*diff);
        diff--;
    } else {
        nbits = av_log2_16bit(2*diff);
    }
    put_bits(&ctx->m.pb, ctx->cid_table->dc_bits[nbits] + nbits,
             (ctx->cid_table->dc_codes[nbits]<<nbits) + (diff & ((1 << nbits) - 1)));
}

static av_always_inline void dnxhd_encode_block(DNXHDEncContext *ctx, DCTELEM *block, int last_index, int n)
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
            int rlevel = (slevel<<1)|!!run_level;
            put_bits(&ctx->m.pb, ctx->vlc_bits[rlevel], ctx->vlc_codes[rlevel]);
            if (run_level)
                put_bits(&ctx->m.pb, ctx->run_bits[run_level], ctx->run_codes[run_level]);
            last_non_zero = i;
        }
    }
    put_bits(&ctx->m.pb, ctx->vlc_bits[0], ctx->vlc_codes[0]); // EOB
}

static av_always_inline void dnxhd_unquantize_c(DNXHDEncContext *ctx, DCTELEM *block, int n, int qscale, int last_index)
{
    const uint8_t *weight_matrix;
    int level;
    int i;

    weight_matrix = (n&2) ? ctx->cid_table->chroma_weight : ctx->cid_table->luma_weight;

    for (i = 1; i <= last_index; i++) {
        int j = ctx->m.intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = (1-2*level) * qscale * weight_matrix[i];
                if (ctx->cid_table->bit_depth == 10) {
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
                level = (2*level+1) * qscale * weight_matrix[i];
                if (ctx->cid_table->bit_depth == 10) {
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

static av_always_inline int dnxhd_ssd_block(DCTELEM *qblock, DCTELEM *block)
{
    int score = 0;
    int i;
    for (i = 0; i < 64; i++)
        score += (block[i] - qblock[i]) * (block[i] - qblock[i]);
    return score;
}

static av_always_inline int dnxhd_calc_ac_bits(DNXHDEncContext *ctx, DCTELEM *block, int last_index)
{
    int last_non_zero = 0;
    int bits = 0;
    int i, j, level;
    for (i = 1; i <= last_index; i++) {
        j = ctx->m.intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            int run_level = i - last_non_zero - 1;
            bits += ctx->vlc_bits[(level<<1)|!!run_level]+ctx->run_bits[run_level];
            last_non_zero = i;
        }
    }
    return bits;
}

static av_always_inline void dnxhd_get_blocks(DNXHDEncContext *ctx, int mb_x, int mb_y)
{
    const int bs = ctx->block_width_l2;
    const int bw = 1 << bs;
    const uint8_t *ptr_y = ctx->thread[0]->src[0] + ((mb_y << 4) * ctx->m.linesize)   + (mb_x << bs+1);
    const uint8_t *ptr_u = ctx->thread[0]->src[1] + ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << bs);
    const uint8_t *ptr_v = ctx->thread[0]->src[2] + ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << bs);
    DSPContext *dsp = &ctx->m.dsp;

    dsp->get_pixels(ctx->blocks[0], ptr_y,      ctx->m.linesize);
    dsp->get_pixels(ctx->blocks[1], ptr_y + bw, ctx->m.linesize);
    dsp->get_pixels(ctx->blocks[2], ptr_u,      ctx->m.uvlinesize);
    dsp->get_pixels(ctx->blocks[3], ptr_v,      ctx->m.uvlinesize);

    if (mb_y+1 == ctx->m.mb_height && ctx->m.avctx->height == 1080) {
        if (ctx->interlaced) {
            ctx->get_pixels_8x4_sym(ctx->blocks[4], ptr_y + ctx->dct_y_offset,      ctx->m.linesize);
            ctx->get_pixels_8x4_sym(ctx->blocks[5], ptr_y + ctx->dct_y_offset + bw, ctx->m.linesize);
            ctx->get_pixels_8x4_sym(ctx->blocks[6], ptr_u + ctx->dct_uv_offset,     ctx->m.uvlinesize);
            ctx->get_pixels_8x4_sym(ctx->blocks[7], ptr_v + ctx->dct_uv_offset,     ctx->m.uvlinesize);
        } else {
            dsp->clear_block(ctx->blocks[4]);
            dsp->clear_block(ctx->blocks[5]);
            dsp->clear_block(ctx->blocks[6]);
            dsp->clear_block(ctx->blocks[7]);
        }
    } else {
        dsp->get_pixels(ctx->blocks[4], ptr_y + ctx->dct_y_offset,      ctx->m.linesize);
        dsp->get_pixels(ctx->blocks[5], ptr_y + ctx->dct_y_offset + bw, ctx->m.linesize);
        dsp->get_pixels(ctx->blocks[6], ptr_u + ctx->dct_uv_offset,     ctx->m.uvlinesize);
        dsp->get_pixels(ctx->blocks[7], ptr_v + ctx->dct_uv_offset,     ctx->m.uvlinesize);
    }
}

static av_always_inline int dnxhd_switch_matrix(DNXHDEncContext *ctx, int i)
{
    if (i&2) {
        ctx->m.q_intra_matrix16 = ctx->qmatrix_c16;
        ctx->m.q_intra_matrix   = ctx->qmatrix_c;
        return 1 + (i&1);
    } else {
        ctx->m.q_intra_matrix16 = ctx->qmatrix_l16;
        ctx->m.q_intra_matrix   = ctx->qmatrix_l;
        return 0;
    }
}

static int dnxhd_calc_bits_thread(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x;
    int qscale = ctx->qscale;
    LOCAL_ALIGNED_16(DCTELEM, block, [64]);
    ctx = ctx->thread[threadnr];

    ctx->m.last_dc[0] =
    ctx->m.last_dc[1] =
    ctx->m.last_dc[2] = 1 << (ctx->cid_table->bit_depth + 2);

    for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
        unsigned mb = mb_y * ctx->m.mb_width + mb_x;
        int ssd     = 0;
        int ac_bits = 0;
        int dc_bits = 0;
        int i;

        dnxhd_get_blocks(ctx, mb_x, mb_y);

        for (i = 0; i < 8; i++) {
            DCTELEM *src_block = ctx->blocks[i];
            int overflow, nbits, diff, last_index;
            int n = dnxhd_switch_matrix(ctx, i);

            memcpy(block, src_block, 64*sizeof(*block));
            last_index = ctx->m.dct_quantize(&ctx->m, block, i, qscale, &overflow);
            ac_bits += dnxhd_calc_ac_bits(ctx, block, last_index);

            diff = block[0] - ctx->m.last_dc[n];
            if (diff < 0) nbits = av_log2_16bit(-2*diff);
            else          nbits = av_log2_16bit( 2*diff);

            assert(nbits < ctx->cid_table->bit_depth + 4);
            dc_bits += ctx->cid_table->dc_bits[nbits] + nbits;

            ctx->m.last_dc[n] = block[0];

            if (avctx->mb_decision == FF_MB_DECISION_RD || !RC_VARIANCE) {
                dnxhd_unquantize_c(ctx, block, i, qscale, last_index);
                ctx->m.dsp.idct(block);
                ssd += dnxhd_ssd_block(block, src_block);
            }
        }
        ctx->mb_rc[qscale][mb].ssd = ssd;
        ctx->mb_rc[qscale][mb].bits = ac_bits+dc_bits+12+8*ctx->vlc_bits[0];
    }
    return 0;
}

static int dnxhd_encode_thread(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x;
    ctx = ctx->thread[threadnr];
    init_put_bits(&ctx->m.pb, (uint8_t *)arg + 640 + ctx->slice_offs[jobnr], ctx->slice_size[jobnr]);

    ctx->m.last_dc[0] =
    ctx->m.last_dc[1] =
    ctx->m.last_dc[2] = 1 << (ctx->cid_table->bit_depth + 2);
    for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
        unsigned mb = mb_y * ctx->m.mb_width + mb_x;
        int qscale = ctx->mb_qscale[mb];
        int i;

        put_bits(&ctx->m.pb, 12, qscale<<1);

        dnxhd_get_blocks(ctx, mb_x, mb_y);

        for (i = 0; i < 8; i++) {
            DCTELEM *block = ctx->blocks[i];
            int overflow, n = dnxhd_switch_matrix(ctx, i);
            int last_index = ctx->m.dct_quantize(&ctx->m, block, i,
                                                 qscale, &overflow);
            //START_TIMER;
            dnxhd_encode_block(ctx, block, last_index, n);
            //STOP_TIMER("encode_block");
        }
    }
    if (put_bits_count(&ctx->m.pb)&31)
        put_bits(&ctx->m.pb, 32-(put_bits_count(&ctx->m.pb)&31), 0);
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
        ctx->slice_size[mb_y] = (ctx->slice_size[mb_y]+31)&~31;
        ctx->slice_size[mb_y] >>= 3;
        thread_size = ctx->slice_size[mb_y];
        offset += thread_size;
    }
}

static int dnxhd_mb_var_thread(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int mb_y = jobnr, mb_x;
    ctx = ctx->thread[threadnr];
    if (ctx->cid_table->bit_depth == 8) {
        uint8_t *pix = ctx->thread[0]->src[0] + ((mb_y<<4) * ctx->m.linesize);
        for (mb_x = 0; mb_x < ctx->m.mb_width; ++mb_x, pix += 16) {
            unsigned mb  = mb_y * ctx->m.mb_width + mb_x;
            int sum = ctx->m.dsp.pix_sum(pix, ctx->m.linesize);
            int varc = (ctx->m.dsp.pix_norm1(pix, ctx->m.linesize) - (((unsigned)sum*sum)>>8)+128)>>8;
            ctx->mb_cmp[mb].value = varc;
            ctx->mb_cmp[mb].mb = mb;
        }
    } else { // 10-bit
        int const linesize = ctx->m.linesize >> 1;
        for (mb_x = 0; mb_x < ctx->m.mb_width; ++mb_x) {
            uint16_t *pix = (uint16_t*)ctx->thread[0]->src[0] + ((mb_y << 4) * linesize) + (mb_x << 4);
            unsigned mb  = mb_y * ctx->m.mb_width + mb_x;
            int sum = 0;
            int sqsum = 0;
            int mean, sqmean;
            int i, j;
            // Macroblocks are 16x16 pixels, unlike DCT blocks which are 8x8.
            for (i = 0; i < 16; ++i) {
                for (j = 0; j < 16; ++j) {
                    // Turn 16-bit pixels into 10-bit ones.
                    int const sample = (unsigned)pix[j] >> 6;
                    sum += sample;
                    sqsum += sample * sample;
                    // 2^10 * 2^10 * 16 * 16 = 2^28, which is less than INT_MAX
                }
                pix += linesize;
            }
            mean = sum >> 8; // 16*16 == 2^8
            sqmean = sqsum >> 8;
            ctx->mb_cmp[mb].value = sqmean - mean * mean;
            ctx->mb_cmp[mb].mb = mb;
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
        avctx->execute2(avctx, dnxhd_calc_bits_thread, NULL, NULL, ctx->m.mb_height);
    }
    up_step = down_step = 2<<LAMBDA_FRAC_BITS;
    lambda = ctx->lambda;

    for (;;) {
        int bits = 0;
        int end = 0;
        if (lambda == last_higher) {
            lambda++;
            end = 1; // need to set final qscales/bits
        }
        for (y = 0; y < ctx->m.mb_height; y++) {
            for (x = 0; x < ctx->m.mb_width; x++) {
                unsigned min = UINT_MAX;
                int qscale = 1;
                int mb = y*ctx->m.mb_width+x;
                for (q = 1; q < avctx->qmax; q++) {
                    unsigned score = ctx->mb_rc[q][mb].bits*lambda+
                        ((unsigned)ctx->mb_rc[q][mb].ssd<<LAMBDA_FRAC_BITS);
                    if (score < min) {
                        min = score;
                        qscale = q;
                    }
                }
                bits += ctx->mb_rc[qscale][mb].bits;
                ctx->mb_qscale[mb] = qscale;
                ctx->mb_bits[mb] = ctx->mb_rc[qscale][mb].bits;
            }
            bits = (bits+31)&~31; // padding
            if (bits > ctx->frame_bits)
                break;
        }
        //av_dlog(ctx->m.avctx, "lambda %d, up %u, down %u, bits %d, frame %d\n",
        //        lambda, last_higher, last_lower, bits, ctx->frame_bits);
        if (end) {
            if (bits > ctx->frame_bits)
                return -1;
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
                return -1;
            else
                lambda += up_step;
            up_step = FFMIN((int64_t)up_step*5, INT_MAX);
            down_step = 1<<LAMBDA_FRAC_BITS;
        }
    }
    //av_dlog(ctx->m.avctx, "out lambda %d\n", lambda);
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
        ctx->m.avctx->execute2(ctx->m.avctx, dnxhd_calc_bits_thread, NULL, NULL, ctx->m.mb_height);
        for (y = 0; y < ctx->m.mb_height; y++) {
            for (x = 0; x < ctx->m.mb_width; x++)
                bits += ctx->mb_rc[qscale][y*ctx->m.mb_width+x].bits;
            bits = (bits+31)&~31; // padding
            if (bits > ctx->frame_bits)
                break;
        }
        //av_dlog(ctx->m.avctx, "%d, qscale %d, bits %d, frame %d, higher %d, lower %d\n",
        //        ctx->m.avctx->frame_number, qscale, bits, ctx->frame_bits, last_higher, last_lower);
        if (bits < ctx->frame_bits) {
            if (qscale == 1)
                return 1;
            if (last_higher == qscale - 1) {
                qscale = last_higher;
                break;
            }
            last_lower = FFMIN(qscale, last_lower);
            if (last_higher != 0)
                qscale = (qscale+last_higher)>>1;
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
                qscale = (qscale+last_lower)>>1;
            else
                qscale += up_step++;
            down_step = 1;
            if (qscale >= ctx->m.avctx->qmax)
                return -1;
        }
    }
    //av_dlog(ctx->m.avctx, "out qscale %d\n", qscale);
    ctx->qscale = qscale;
    return 0;
}

#define BUCKET_BITS 8
#define RADIX_PASSES 4
#define NBUCKETS (1 << BUCKET_BITS)

static inline int get_bucket(int value, int shift)
{
    value >>= shift;
    value &= NBUCKETS - 1;
    return NBUCKETS - 1 - value;
}

static void radix_count(const RCCMPEntry *data, int size, int buckets[RADIX_PASSES][NBUCKETS])
{
    int i, j;
    memset(buckets, 0, sizeof(buckets[0][0]) * RADIX_PASSES * NBUCKETS);
    for (i = 0; i < size; i++) {
        int v = data[i].value;
        for (j = 0; j < RADIX_PASSES; j++) {
            buckets[j][get_bucket(v, 0)]++;
            v >>= BUCKET_BITS;
        }
        assert(!v);
    }
    for (j = 0; j < RADIX_PASSES; j++) {
        int offset = size;
        for (i = NBUCKETS - 1; i >= 0; i--)
            buckets[j][i] = offset -= buckets[j][i];
        assert(!buckets[j][0]);
    }
}

static void radix_sort_pass(RCCMPEntry *dst, const RCCMPEntry *data, int size, int buckets[NBUCKETS], int pass)
{
    int shift = pass * BUCKET_BITS;
    int i;
    for (i = 0; i < size; i++) {
        int v = get_bucket(data[i].value, shift);
        int pos = buckets[v]++;
        dst[pos] = data[i];
    }
}

static void radix_sort(RCCMPEntry *data, int size)
{
    int buckets[RADIX_PASSES][NBUCKETS];
    RCCMPEntry *tmp = av_malloc(sizeof(*tmp) * size);
    radix_count(data, size, buckets);
    radix_sort_pass(tmp, data, size, buckets[0], 0);
    radix_sort_pass(data, tmp, size, buckets[1], 1);
    if (buckets[2][NBUCKETS - 1] || buckets[3][NBUCKETS - 1]) {
        radix_sort_pass(tmp, data, size, buckets[2], 2);
        radix_sort_pass(data, tmp, size, buckets[3], 3);
    }
    av_free(tmp);
}

static int dnxhd_encode_fast(AVCodecContext *avctx, DNXHDEncContext *ctx)
{
    int max_bits = 0;
    int ret, x, y;
    if ((ret = dnxhd_find_qscale(ctx)) < 0)
        return -1;
    for (y = 0; y < ctx->m.mb_height; y++) {
        for (x = 0; x < ctx->m.mb_width; x++) {
            int mb = y*ctx->m.mb_width+x;
            int delta_bits;
            ctx->mb_qscale[mb] = ctx->qscale;
            ctx->mb_bits[mb] = ctx->mb_rc[ctx->qscale][mb].bits;
            max_bits += ctx->mb_rc[ctx->qscale][mb].bits;
            if (!RC_VARIANCE) {
                delta_bits = ctx->mb_rc[ctx->qscale][mb].bits-ctx->mb_rc[ctx->qscale+1][mb].bits;
                ctx->mb_cmp[mb].mb = mb;
                ctx->mb_cmp[mb].value = delta_bits ?
                    ((ctx->mb_rc[ctx->qscale][mb].ssd-ctx->mb_rc[ctx->qscale+1][mb].ssd)*100)/delta_bits
                    : INT_MIN; //avoid increasing qscale
            }
        }
        max_bits += 31; //worst padding
    }
    if (!ret) {
        if (RC_VARIANCE)
            avctx->execute2(avctx, dnxhd_mb_var_thread, NULL, NULL, ctx->m.mb_height);
        radix_sort(ctx->mb_cmp, ctx->m.mb_num);
        for (x = 0; x < ctx->m.mb_num && max_bits > ctx->frame_bits; x++) {
            int mb = ctx->mb_cmp[x].mb;
            max_bits -= ctx->mb_rc[ctx->qscale][mb].bits - ctx->mb_rc[ctx->qscale+1][mb].bits;
            ctx->mb_qscale[mb] = ctx->qscale+1;
            ctx->mb_bits[mb] = ctx->mb_rc[ctx->qscale+1][mb].bits;
        }
    }
    return 0;
}

static void dnxhd_load_picture(DNXHDEncContext *ctx, const AVFrame *frame)
{
    int i;

    for (i = 0; i < 3; i++) {
        ctx->frame.data[i]     = frame->data[i];
        ctx->frame.linesize[i] = frame->linesize[i];
    }

    for (i = 0; i < ctx->m.avctx->thread_count; i++) {
        ctx->thread[i]->m.linesize    = ctx->frame.linesize[0]<<ctx->interlaced;
        ctx->thread[i]->m.uvlinesize  = ctx->frame.linesize[1]<<ctx->interlaced;
        ctx->thread[i]->dct_y_offset  = ctx->m.linesize  *8;
        ctx->thread[i]->dct_uv_offset = ctx->m.uvlinesize*8;
    }

    ctx->frame.interlaced_frame = frame->interlaced_frame;
    ctx->cur_field = frame->interlaced_frame && !frame->top_field_first;
}

static int dnxhd_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int first_field = 1;
    int offset, i, ret;
    uint8_t *buf;

    if ((ret = ff_alloc_packet(pkt, ctx->cid_table->frame_size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "output buffer is too small to compress picture\n");
        return ret;
    }
    buf = pkt->data;

    dnxhd_load_picture(ctx, frame);

 encode_coding_unit:
    for (i = 0; i < 3; i++) {
        ctx->src[i] = ctx->frame.data[i];
        if (ctx->interlaced && ctx->cur_field)
            ctx->src[i] += ctx->frame.linesize[i];
    }

    dnxhd_write_header(avctx, buf);

    if (avctx->mb_decision == FF_MB_DECISION_RD)
        ret = dnxhd_encode_rdo(avctx, ctx);
    else
        ret = dnxhd_encode_fast(avctx, ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "picture could not fit ratecontrol constraints, increase qmax\n");
        return -1;
    }

    dnxhd_setup_threads_slices(ctx);

    offset = 0;
    for (i = 0; i < ctx->m.mb_height; i++) {
        AV_WB32(ctx->msip + i * 4, offset);
        offset += ctx->slice_size[i];
        assert(!(ctx->slice_size[i] & 3));
    }

    avctx->execute2(avctx, dnxhd_encode_thread, buf, NULL, ctx->m.mb_height);

    assert(640 + offset + 4 <= ctx->cid_table->coding_unit_size);
    memset(buf + 640 + offset, 0, ctx->cid_table->coding_unit_size - 4 - offset - 640);

    AV_WB32(buf + ctx->cid_table->coding_unit_size - 4, 0x600DC0DE); // EOF

    if (ctx->interlaced && first_field) {
        first_field     = 0;
        ctx->cur_field ^= 1;
        buf      += ctx->cid_table->coding_unit_size;
        goto encode_coding_unit;
    }

    ctx->frame.quality = ctx->qscale*FF_QP2LAMBDA;

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static int dnxhd_encode_end(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int max_level = 1<<(ctx->cid_table->bit_depth+2);
    int i;

    av_free(ctx->vlc_codes-max_level*2);
    av_free(ctx->vlc_bits -max_level*2);
    av_freep(&ctx->run_codes);
    av_freep(&ctx->run_bits);

    av_freep(&ctx->mb_bits);
    av_freep(&ctx->mb_qscale);
    av_freep(&ctx->mb_rc);
    av_freep(&ctx->mb_cmp);
    av_freep(&ctx->slice_size);
    av_freep(&ctx->slice_offs);

    av_freep(&ctx->qmatrix_c);
    av_freep(&ctx->qmatrix_l);
    av_freep(&ctx->qmatrix_c16);
    av_freep(&ctx->qmatrix_l16);

    for (i = 1; i < avctx->thread_count; i++)
        av_freep(&ctx->thread[i]);

    return 0;
}

AVCodec ff_dnxhd_encoder = {
    .name           = "dnxhd",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_DNXHD,
    .priv_data_size = sizeof(DNXHDEncContext),
    .init           = dnxhd_encode_init,
    .encode2        = dnxhd_encode_picture,
    .close          = dnxhd_encode_end,
    .capabilities   = CODEC_CAP_SLICE_THREADS,
    .pix_fmts       = (const enum PixelFormat[]){ PIX_FMT_YUV422P,
                                                  PIX_FMT_YUV422P10,
                                                  PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("VC3/DNxHD"),
    .priv_class     = &class,
};
