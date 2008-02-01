/*
 * VC3/DNxHD encoder
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
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

//#define DEBUG
#define RC_VARIANCE 1 // use variance or ssd for fast rc

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "dnxhddata.h"

typedef struct {
    uint16_t mb;
    int value;
} RCCMPEntry;

typedef struct {
    int ssd;
    int bits;
} RCEntry;

int dct_quantize_c(MpegEncContext *s, DCTELEM *block, int n, int qscale, int *overflow);

typedef struct DNXHDEncContext {
    MpegEncContext m; ///< Used for quantization dsp functions

    AVFrame frame;
    int cid;
    const CIDEntry *cid_table;
    uint8_t *msip; ///< Macroblock Scan Indices Payload
    uint32_t *slice_size;

    struct DNXHDEncContext *thread[MAX_THREADS];

    unsigned dct_y_offset;
    unsigned dct_uv_offset;
    int interlaced;
    int cur_field;

    DECLARE_ALIGNED_16(DCTELEM, blocks[8][64]);

    int      (*qmatrix_c)     [64];
    int      (*qmatrix_l)     [64];
    uint16_t (*qmatrix_l16)[2][64];
    uint16_t (*qmatrix_c16)[2][64];

    unsigned frame_bits;
    uint8_t *src[3];

    uint16_t *table_vlc_codes;
    uint8_t  *table_vlc_bits;
    uint16_t *table_run_codes;
    uint8_t  *table_run_bits;

    /** Rate control */
    unsigned slice_bits;
    unsigned qscale;
    unsigned lambda;

    unsigned thread_size;

    uint16_t *mb_bits;
    uint8_t  *mb_qscale;

    RCCMPEntry *mb_cmp;
    RCEntry   (*mb_rc)[8160];
} DNXHDEncContext;

#define LAMBDA_FRAC_BITS 10

static int dnxhd_init_vlc(DNXHDEncContext *ctx)
{
    int i;

    CHECKED_ALLOCZ(ctx->table_vlc_codes, 449*2);
    CHECKED_ALLOCZ(ctx->table_vlc_bits,    449);
    CHECKED_ALLOCZ(ctx->table_run_codes,  63*2);
    CHECKED_ALLOCZ(ctx->table_run_bits,     63);

    for (i = 0; i < 257; i++) {
        int level = ctx->cid_table->ac_level[i] +
            (ctx->cid_table->ac_run_flag[i] << 7) + (ctx->cid_table->ac_index_flag[i] << 8);
        assert(level < 449);
        if (ctx->cid_table->ac_level[i] == 64 && ctx->cid_table->ac_index_flag[i])
            level -= 64; // use 0+(1<<8) level
        ctx->table_vlc_codes[level] = ctx->cid_table->ac_codes[i];
        ctx->table_vlc_bits [level] = ctx->cid_table->ac_bits[i];
    }
    for (i = 0; i < 62; i++) {
        int run = ctx->cid_table->run[i];
        assert(run < 63);
        ctx->table_run_codes[run] = ctx->cid_table->run_codes[i];
        ctx->table_run_bits [run] = ctx->cid_table->run_bits[i];
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

    CHECKED_ALLOCZ(ctx->qmatrix_l,   (ctx->m.avctx->qmax+1) * 64 * sizeof(int));
    CHECKED_ALLOCZ(ctx->qmatrix_c,   (ctx->m.avctx->qmax+1) * 64 * sizeof(int));
    CHECKED_ALLOCZ(ctx->qmatrix_l16, (ctx->m.avctx->qmax+1) * 64 * 2 * sizeof(uint16_t));
    CHECKED_ALLOCZ(ctx->qmatrix_c16, (ctx->m.avctx->qmax+1) * 64 * 2 * sizeof(uint16_t));

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
    return 0;
 fail:
    return -1;
}

static int dnxhd_init_rc(DNXHDEncContext *ctx)
{
    CHECKED_ALLOCZ(ctx->mb_rc, 8160*ctx->m.avctx->qmax*sizeof(RCEntry));
    if (ctx->m.avctx->mb_decision != FF_MB_DECISION_RD)
        CHECKED_ALLOCZ(ctx->mb_cmp, ctx->m.mb_num*sizeof(RCCMPEntry));

    ctx->frame_bits = (ctx->cid_table->coding_unit_size - 640 - 4) * 8;
    ctx->qscale = 1;
    ctx->lambda = 2<<LAMBDA_FRAC_BITS; // qscale 2
    return 0;
 fail:
    return -1;
}

static int dnxhd_encode_init(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int i, index;

    ctx->cid = ff_dnxhd_find_cid(avctx);
    if (!ctx->cid || avctx->pix_fmt != PIX_FMT_YUV422P) {
        av_log(avctx, AV_LOG_ERROR, "video parameters incompatible with DNxHD\n");
        return -1;
    }
    av_log(avctx, AV_LOG_DEBUG, "cid %d\n", ctx->cid);

    index = ff_dnxhd_get_cid_table(ctx->cid);
    ctx->cid_table = &ff_dnxhd_cid_table[index];

    ctx->m.avctx = avctx;
    ctx->m.mb_intra = 1;
    ctx->m.h263_aic = 1;

    dsputil_init(&ctx->m.dsp, avctx);
    ff_dct_common_init(&ctx->m);
    if (!ctx->m.dct_quantize)
        ctx->m.dct_quantize = dct_quantize_c;

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

    if (dnxhd_init_vlc(ctx) < 0)
        return -1;
    if (dnxhd_init_rc(ctx) < 0)
        return -1;

    CHECKED_ALLOCZ(ctx->slice_size, ctx->m.mb_height*sizeof(uint32_t));
    CHECKED_ALLOCZ(ctx->mb_bits,    ctx->m.mb_num   *sizeof(uint16_t));
    CHECKED_ALLOCZ(ctx->mb_qscale,  ctx->m.mb_num   *sizeof(uint8_t));

    ctx->frame.key_frame = 1;
    ctx->frame.pict_type = FF_I_TYPE;
    ctx->m.avctx->coded_frame = &ctx->frame;

    if (avctx->thread_count > MAX_THREADS || (avctx->thread_count > ctx->m.mb_height)) {
        av_log(avctx, AV_LOG_ERROR, "too many threads\n");
        return -1;
    }

    ctx->thread[0] = ctx;
    for (i = 1; i < avctx->thread_count; i++) {
        ctx->thread[i] =  av_malloc(sizeof(DNXHDEncContext));
        memcpy(ctx->thread[i], ctx, sizeof(DNXHDEncContext));
    }

    for (i = 0; i < avctx->thread_count; i++) {
        ctx->thread[i]->m.start_mb_y = (ctx->m.mb_height*(i  ) + avctx->thread_count/2) / avctx->thread_count;
        ctx->thread[i]->m.end_mb_y   = (ctx->m.mb_height*(i+1) + avctx->thread_count/2) / avctx->thread_count;
    }

    return 0;
 fail: //for CHECKED_ALLOCZ
    return -1;
}

static int dnxhd_write_header(AVCodecContext *avctx, uint8_t *buf)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    const uint8_t header_prefix[5] = { 0x00,0x00,0x02,0x80,0x01 };

    memcpy(buf, header_prefix, 5);
    buf[5] = ctx->interlaced ? ctx->cur_field+2 : 0x01;
    buf[6] = 0x80; // crc flag off
    buf[7] = 0xa0; // reserved
    AV_WB16(buf + 0x18, avctx->height); // ALPF
    AV_WB16(buf + 0x1a, avctx->width);  // SPL
    AV_WB16(buf + 0x1d, avctx->height); // NAL

    buf[0x21] = 0x38; // FIXME 8 bit per comp
    buf[0x22] = 0x88 + (ctx->frame.interlaced_frame<<2);
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
    int offset = 0;
    int slevel, i, j;

    dnxhd_encode_dc(ctx, block[0] - ctx->m.last_dc[n]);
    ctx->m.last_dc[n] = block[0];

    for (i = 1; i <= last_index; i++) {
        j = ctx->m.intra_scantable.permutated[i];
        slevel = block[j];
        if (slevel) {
            int run_level = i - last_non_zero - 1;
            int sign;
            MASK_ABS(sign, slevel);
            if (slevel > 64) {
                offset = (slevel-1) >> 6;
                slevel = 256 | (slevel & 63); // level 64 is treated as 0
            }
            if (run_level)
                slevel |= 128;
            put_bits(&ctx->m.pb, ctx->table_vlc_bits[slevel]+1, (ctx->table_vlc_codes[slevel]<<1)|(sign&1));
            if (offset) {
                put_bits(&ctx->m.pb, 4, offset);
                offset = 0;
            }
            if (run_level)
                put_bits(&ctx->m.pb, ctx->table_run_bits[run_level], ctx->table_run_codes[run_level]);
            last_non_zero = i;
        }
    }
    put_bits(&ctx->m.pb, ctx->table_vlc_bits[0], ctx->table_vlc_codes[0]); // EOB
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
                if (weight_matrix[i] != 32)
                    level += 32;
                level >>= 6;
                level = -level;
            } else {
                level = (2*level+1) * qscale * weight_matrix[i];
                if (weight_matrix[i] != 32)
                    level += 32;
                level >>= 6;
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
        score += (block[i]-qblock[i])*(block[i]-qblock[i]);
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
            level = FFABS(level);
            if (level > 64) {
                level = 256 | (level & 63); // level 64 is treated as 0
                bits += 4;
            }
            level |= (!!run_level)<<7;
            bits += ctx->table_vlc_bits[level]+1 + ctx->table_run_bits[run_level];
            last_non_zero = i;
        }
    }
    return bits;
}

static av_always_inline void dnxhd_get_pixels_4x8(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
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
        pixels += line_size;
        block += 8;
    }
    memcpy(block   , block- 8, sizeof(*block)*8);
    memcpy(block+ 8, block-16, sizeof(*block)*8);
    memcpy(block+16, block-24, sizeof(*block)*8);
    memcpy(block+24, block-32, sizeof(*block)*8);
}

static av_always_inline void dnxhd_get_blocks(DNXHDEncContext *ctx, int mb_x, int mb_y)
{
    const uint8_t *ptr_y = ctx->thread[0]->src[0] + ((mb_y << 4) * ctx->m.linesize)   + (mb_x << 4);
    const uint8_t *ptr_u = ctx->thread[0]->src[1] + ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << 3);
    const uint8_t *ptr_v = ctx->thread[0]->src[2] + ((mb_y << 4) * ctx->m.uvlinesize) + (mb_x << 3);
    DSPContext *dsp = &ctx->m.dsp;

    dsp->get_pixels(ctx->blocks[0], ptr_y    , ctx->m.linesize);
    dsp->get_pixels(ctx->blocks[1], ptr_y + 8, ctx->m.linesize);
    dsp->get_pixels(ctx->blocks[2], ptr_u    , ctx->m.uvlinesize);
    dsp->get_pixels(ctx->blocks[3], ptr_v    , ctx->m.uvlinesize);

    if (mb_y+1 == ctx->m.mb_height && ctx->m.avctx->height == 1080) {
        if (ctx->interlaced) {
            dnxhd_get_pixels_4x8(ctx->blocks[4], ptr_y + ctx->dct_y_offset    , ctx->m.linesize);
            dnxhd_get_pixels_4x8(ctx->blocks[5], ptr_y + ctx->dct_y_offset + 8, ctx->m.linesize);
            dnxhd_get_pixels_4x8(ctx->blocks[6], ptr_u + ctx->dct_uv_offset   , ctx->m.uvlinesize);
            dnxhd_get_pixels_4x8(ctx->blocks[7], ptr_v + ctx->dct_uv_offset   , ctx->m.uvlinesize);
        } else
            memset(ctx->blocks[4], 0, 4*64*sizeof(DCTELEM));
    } else {
        dsp->get_pixels(ctx->blocks[4], ptr_y + ctx->dct_y_offset    , ctx->m.linesize);
        dsp->get_pixels(ctx->blocks[5], ptr_y + ctx->dct_y_offset + 8, ctx->m.linesize);
        dsp->get_pixels(ctx->blocks[6], ptr_u + ctx->dct_uv_offset   , ctx->m.uvlinesize);
        dsp->get_pixels(ctx->blocks[7], ptr_v + ctx->dct_uv_offset   , ctx->m.uvlinesize);
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

static int dnxhd_calc_bits_thread(AVCodecContext *avctx, void *arg)
{
    DNXHDEncContext *ctx = arg;
    int mb_y, mb_x;
    int qscale = ctx->thread[0]->qscale;

    for (mb_y = ctx->m.start_mb_y; mb_y < ctx->m.end_mb_y; mb_y++) {
        ctx->m.last_dc[0] =
        ctx->m.last_dc[1] =
        ctx->m.last_dc[2] = 1024;

        for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
            unsigned mb = mb_y * ctx->m.mb_width + mb_x;
            int ssd     = 0;
            int ac_bits = 0;
            int dc_bits = 0;
            int i;

            dnxhd_get_blocks(ctx, mb_x, mb_y);

            for (i = 0; i < 8; i++) {
                DECLARE_ALIGNED_16(DCTELEM, block[64]);
                DCTELEM *src_block = ctx->blocks[i];
                int overflow, nbits, diff, last_index;
                int n = dnxhd_switch_matrix(ctx, i);

                memcpy(block, src_block, sizeof(block));
                last_index = ctx->m.dct_quantize((MpegEncContext*)ctx, block, i, qscale, &overflow);
                ac_bits += dnxhd_calc_ac_bits(ctx, block, last_index);

                diff = block[0] - ctx->m.last_dc[n];
                if (diff < 0) nbits = av_log2_16bit(-2*diff);
                else          nbits = av_log2_16bit( 2*diff);
                dc_bits += ctx->cid_table->dc_bits[nbits] + nbits;

                ctx->m.last_dc[n] = block[0];

                if (avctx->mb_decision == FF_MB_DECISION_RD || !RC_VARIANCE) {
                    dnxhd_unquantize_c(ctx, block, i, qscale, last_index);
                    ctx->m.dsp.idct(block);
                    ssd += dnxhd_ssd_block(block, src_block);
                }
            }
            ctx->mb_rc[qscale][mb].ssd = ssd;
            ctx->mb_rc[qscale][mb].bits = ac_bits+dc_bits+12+8*ctx->table_vlc_bits[0];
        }
    }
    return 0;
}

static int dnxhd_encode_thread(AVCodecContext *avctx, void *arg)
{
    DNXHDEncContext *ctx = arg;
    int mb_y, mb_x;

    for (mb_y = ctx->m.start_mb_y; mb_y < ctx->m.end_mb_y; mb_y++) {
        ctx->m.last_dc[0] =
        ctx->m.last_dc[1] =
        ctx->m.last_dc[2] = 1024;
        for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
            unsigned mb = mb_y * ctx->m.mb_width + mb_x;
            int qscale = ctx->mb_qscale[mb];
            int i;

            put_bits(&ctx->m.pb, 12, qscale<<1);

            dnxhd_get_blocks(ctx, mb_x, mb_y);

            for (i = 0; i < 8; i++) {
                DCTELEM *block = ctx->blocks[i];
                int last_index, overflow;
                int n = dnxhd_switch_matrix(ctx, i);
                last_index = ctx->m.dct_quantize((MpegEncContext*)ctx, block, i, qscale, &overflow);
                dnxhd_encode_block(ctx, block, last_index, n);
            }
        }
        if (put_bits_count(&ctx->m.pb)&31)
            put_bits(&ctx->m.pb, 32-(put_bits_count(&ctx->m.pb)&31), 0);
    }
    flush_put_bits(&ctx->m.pb);
    return 0;
}

static void dnxhd_setup_threads_slices(DNXHDEncContext *ctx, uint8_t *buf)
{
    int mb_y, mb_x;
    int i, offset = 0;
    for (i = 0; i < ctx->m.avctx->thread_count; i++) {
        int thread_size = 0;
        for (mb_y = ctx->thread[i]->m.start_mb_y; mb_y < ctx->thread[i]->m.end_mb_y; mb_y++) {
            ctx->slice_size[mb_y] = 0;
            for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
                unsigned mb = mb_y * ctx->m.mb_width + mb_x;
                ctx->slice_size[mb_y] += ctx->mb_bits[mb];
            }
            ctx->slice_size[mb_y] = (ctx->slice_size[mb_y]+31)&~31;
            ctx->slice_size[mb_y] >>= 3;
            thread_size += ctx->slice_size[mb_y];
        }
        init_put_bits(&ctx->thread[i]->m.pb, buf + 640 + offset, thread_size);
        offset += thread_size;
    }
}

static int dnxhd_mb_var_thread(AVCodecContext *avctx, void *arg)
{
    DNXHDEncContext *ctx = arg;
    int mb_y, mb_x;
    for (mb_y = ctx->m.start_mb_y; mb_y < ctx->m.end_mb_y; mb_y++) {
        for (mb_x = 0; mb_x < ctx->m.mb_width; mb_x++) {
            unsigned mb  = mb_y * ctx->m.mb_width + mb_x;
            uint8_t *pix = ctx->thread[0]->src[0] + ((mb_y<<4) * ctx->m.linesize) + (mb_x<<4);
            int sum      = ctx->m.dsp.pix_sum(pix, ctx->m.linesize);
            int varc     = (ctx->m.dsp.pix_norm1(pix, ctx->m.linesize) - (((unsigned)(sum*sum))>>8)+128)>>8;
            ctx->mb_cmp[mb].value = varc;
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
        avctx->execute(avctx, dnxhd_calc_bits_thread, (void**)&ctx->thread[0], NULL, avctx->thread_count);
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
                    unsigned score = ctx->mb_rc[q][mb].bits*lambda+(ctx->mb_rc[q][mb].ssd<<LAMBDA_FRAC_BITS);
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
        //dprintf(ctx->m.avctx, "lambda %d, up %u, down %u, bits %d, frame %d\n",
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
            down_step *= 5; // XXX tune ?
            up_step = 1<<LAMBDA_FRAC_BITS;
            lambda = FFMAX(1, lambda);
            if (lambda == last_lower)
                break;
        } else {
            last_higher = FFMAX(lambda, last_higher);
            if (last_lower != INT_MAX)
                lambda = (lambda+last_lower)>>1;
            else
                lambda += up_step;
            up_step *= 5;
            down_step = 1<<LAMBDA_FRAC_BITS;
        }
    }
    //dprintf(ctx->m.avctx, "out lambda %d\n", lambda);
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
        ctx->m.avctx->execute(ctx->m.avctx, dnxhd_calc_bits_thread, (void**)&ctx->thread[0], NULL, ctx->m.avctx->thread_count);
        for (y = 0; y < ctx->m.mb_height; y++) {
            for (x = 0; x < ctx->m.mb_width; x++)
                bits += ctx->mb_rc[qscale][y*ctx->m.mb_width+x].bits;
            bits = (bits+31)&~31; // padding
            if (bits > ctx->frame_bits)
                break;
        }
        //dprintf(ctx->m.avctx, "%d, qscale %d, bits %d, frame %d, higher %d, lower %d\n",
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
    //dprintf(ctx->m.avctx, "out qscale %d\n", qscale);
    ctx->qscale = qscale;
    return 0;
}

static int dnxhd_rc_cmp(const void *a, const void *b)
{
    return ((const RCCMPEntry *)b)->value - ((const RCCMPEntry *)a)->value;
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
            avctx->execute(avctx, dnxhd_mb_var_thread, (void**)&ctx->thread[0], NULL, avctx->thread_count);
        qsort(ctx->mb_cmp, ctx->m.mb_num, sizeof(RCEntry), dnxhd_rc_cmp);
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

static int dnxhd_encode_picture(AVCodecContext *avctx, unsigned char *buf, int buf_size, const void *data)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int first_field = 1;
    int offset, i, ret;

    if (buf_size < ctx->cid_table->frame_size) {
        av_log(avctx, AV_LOG_ERROR, "output buffer is too small to compress picture\n");
        return -1;
    }

    dnxhd_load_picture(ctx, data);

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
        av_log(avctx, AV_LOG_ERROR, "picture could not fit ratecontrol constraints\n");
        return -1;
    }

    dnxhd_setup_threads_slices(ctx, buf);

    offset = 0;
    for (i = 0; i < ctx->m.mb_height; i++) {
        AV_WB32(ctx->msip + i * 4, offset);
        offset += ctx->slice_size[i];
        assert(!(ctx->slice_size[i] & 3));
    }

    avctx->execute(avctx, dnxhd_encode_thread, (void**)&ctx->thread[0], NULL, avctx->thread_count);

    AV_WB32(buf + ctx->cid_table->coding_unit_size - 4, 0x600DC0DE); // EOF

    if (ctx->interlaced && first_field) {
        first_field     = 0;
        ctx->cur_field ^= 1;
        buf      += ctx->cid_table->coding_unit_size;
        buf_size -= ctx->cid_table->coding_unit_size;
        goto encode_coding_unit;
    }

    return ctx->cid_table->frame_size;
}

static int dnxhd_encode_end(AVCodecContext *avctx)
{
    DNXHDEncContext *ctx = avctx->priv_data;
    int i;

    av_freep(&ctx->table_vlc_codes);
    av_freep(&ctx->table_vlc_bits);
    av_freep(&ctx->table_run_codes);
    av_freep(&ctx->table_run_bits);

    av_freep(&ctx->mb_bits);
    av_freep(&ctx->mb_qscale);
    av_freep(&ctx->mb_rc);
    av_freep(&ctx->mb_cmp);
    av_freep(&ctx->slice_size);

    av_freep(&ctx->qmatrix_c);
    av_freep(&ctx->qmatrix_l);
    av_freep(&ctx->qmatrix_c16);
    av_freep(&ctx->qmatrix_l16);

    for (i = 1; i < avctx->thread_count; i++)
        av_freep(&ctx->thread[i]);

    return 0;
}

AVCodec dnxhd_encoder = {
    "dnxhd",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DNXHD,
    sizeof(DNXHDEncContext),
    dnxhd_encode_init,
    dnxhd_encode_picture,
    dnxhd_encode_end,
    .pix_fmts = (enum PixelFormat[]){PIX_FMT_YUV422P, -1},
};
