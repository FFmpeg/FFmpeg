/*
 * VC3/DNxHD decoder.
 * Copyright (c) 2007 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 * Copyright (c) 2011 MirriAd Ltd
 * Copyright (c) 2015 Christophe Gisquet
 *
 * 10 bit support added by MirriAd Ltd, Joseph Artsimovich <joseph@mirriad.com>
 * Slice multithreading and MB interlaced support added by Christophe Gisquet
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "decode.h"
#define  UNCHECKED_BITSTREAM_READER 1
#include "get_bits.h"
#include "dnxhddata.h"
#include "idctdsp.h"
#include "profiles.h"
#include "thread.h"

typedef struct RowContext {
    DECLARE_ALIGNED(32, int16_t, blocks)[12][64];
    int luma_scale[64];
    int chroma_scale[64];
    GetBitContext gb;
    int last_dc[3];
    int last_qscale;
    int errors;
    /** -1:not set yet  0:off=RGB  1:on=YUV  2:variable */
    int format;
} RowContext;

typedef struct DNXHDContext {
    AVCodecContext *avctx;
    RowContext *rows;
    BlockDSPContext bdsp;
    const uint8_t* buf;
    int buf_size;
    int64_t cid;                        ///< compression id
    unsigned int width, height;
    enum AVPixelFormat pix_fmt;
    unsigned int mb_width, mb_height;
    uint32_t mb_scan_index[512];
    int data_offset;                    // End of mb_scan_index, where macroblocks start
    int cur_field;                      ///< current interlaced field
    VLC ac_vlc, dc_vlc, run_vlc;
    IDCTDSPContext idsp;
    uint8_t permutated_scantable[64];
    const CIDEntry *cid_table;
    int bit_depth; // 8, 10, 12 or 0 if not initialized at all.
    int is_444;
    int alpha;
    int lla;
    int mbaff;
    int act;
    int (*decode_dct_block)(const struct DNXHDContext *ctx,
                            RowContext *row, int n);
} DNXHDContext;

#define DNXHD_VLC_BITS 9
#define DNXHD_DC_VLC_BITS 7

static int dnxhd_decode_dct_block_8(const DNXHDContext *ctx,
                                    RowContext *row, int n);
static int dnxhd_decode_dct_block_10(const DNXHDContext *ctx,
                                     RowContext *row, int n);
static int dnxhd_decode_dct_block_10_444(const DNXHDContext *ctx,
                                         RowContext *row, int n);
static int dnxhd_decode_dct_block_12(const DNXHDContext *ctx,
                                     RowContext *row, int n);
static int dnxhd_decode_dct_block_12_444(const DNXHDContext *ctx,
                                         RowContext *row, int n);

static av_cold int dnxhd_decode_init(AVCodecContext *avctx)
{
    DNXHDContext *ctx = avctx->priv_data;

    ctx->avctx = avctx;
    ctx->cid = -1;
    if (avctx->colorspace == AVCOL_SPC_UNSPECIFIED) {
        avctx->colorspace = AVCOL_SPC_BT709;
    }

    avctx->coded_width  = FFALIGN(avctx->width,  16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    ctx->rows = av_calloc(avctx->thread_count, sizeof(*ctx->rows));
    if (!ctx->rows)
        return AVERROR(ENOMEM);

    return 0;
}

static int dnxhd_init_vlc(DNXHDContext *ctx, uint32_t cid, int bitdepth)
{
    int ret;
    if (cid != ctx->cid) {
        const CIDEntry *cid_table = ff_dnxhd_get_cid_table(cid);

        if (!cid_table) {
            av_log(ctx->avctx, AV_LOG_ERROR, "unsupported cid %"PRIu32"\n", cid);
            return AVERROR(ENOSYS);
        }
        if (cid_table->bit_depth != bitdepth &&
            cid_table->bit_depth != DNXHD_VARIABLE) {
            av_log(ctx->avctx, AV_LOG_ERROR, "bit depth mismatches %d %d\n",
                   cid_table->bit_depth, bitdepth);
            return AVERROR_INVALIDDATA;
        }
        ctx->cid_table = cid_table;
        av_log(ctx->avctx, AV_LOG_VERBOSE, "Profile cid %"PRIu32".\n", cid);

        ff_vlc_free(&ctx->ac_vlc);
        ff_vlc_free(&ctx->dc_vlc);
        ff_vlc_free(&ctx->run_vlc);

        if ((ret = vlc_init(&ctx->ac_vlc, DNXHD_VLC_BITS, 257,
                 ctx->cid_table->ac_bits, 1, 1,
                 ctx->cid_table->ac_codes, 2, 2, 0)) < 0)
            goto out;
        if ((ret = vlc_init(&ctx->dc_vlc, DNXHD_DC_VLC_BITS, bitdepth > 8 ? 14 : 12,
                 ctx->cid_table->dc_bits, 1, 1,
                 ctx->cid_table->dc_codes, 1, 1, 0)) < 0)
            goto out;
        if ((ret = ff_vlc_init_sparse(&ctx->run_vlc, DNXHD_VLC_BITS, 62,
                 ctx->cid_table->run_bits, 1, 1,
                 ctx->cid_table->run_codes, 2, 2,
                 ctx->cid_table->run, 1, 1, 0)) < 0)
            goto out;

        ctx->cid = cid;
    }
    ret = 0;
out:
    if (ret < 0)
        av_log(ctx->avctx, AV_LOG_ERROR, "vlc_init failed\n");
    return ret;
}

static int dnxhd_get_profile(int cid)
{
    switch(cid) {
    case 1270:
        return AV_PROFILE_DNXHR_444;
    case 1271:
        return AV_PROFILE_DNXHR_HQX;
    case 1272:
        return AV_PROFILE_DNXHR_HQ;
    case 1273:
        return AV_PROFILE_DNXHR_SQ;
    case 1274:
        return AV_PROFILE_DNXHR_LB;
    }
    return AV_PROFILE_DNXHD;
}

static int dnxhd_decode_header(DNXHDContext *ctx, AVFrame *frame,
                               const uint8_t *buf, int buf_size,
                               int first_field)
{
    int i, cid, ret;
    int old_bit_depth = ctx->bit_depth, bitdepth;
    uint64_t header_prefix;
    if (buf_size < 0x280) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "buffer too small (%d < 640).\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    header_prefix = ff_dnxhd_parse_header_prefix(buf);
    if (header_prefix == 0) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "unknown header 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
               buf[0], buf[1], buf[2], buf[3], buf[4]);
        return AVERROR_INVALIDDATA;
    }
    if (buf[5] & 2) { /* interlaced */
        ctx->cur_field = first_field ? buf[5] & 1 : !ctx->cur_field;
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
        if (first_field ^ ctx->cur_field)
            frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
        av_log(ctx->avctx, AV_LOG_DEBUG,
               "interlaced %d, cur field %d\n", buf[5] & 3, ctx->cur_field);
    } else {
        ctx->cur_field = 0;
    }
    ctx->mbaff = (buf[0x6] >> 5) & 1;
    ctx->alpha = buf[0x7] & 1;
    ctx->lla   = (buf[0x7] >> 1) & 1;
    if (ctx->alpha)
        avpriv_request_sample(ctx->avctx, "alpha");

    ctx->height = AV_RB16(buf + 0x18);
    ctx->width  = AV_RB16(buf + 0x1a);

    switch(buf[0x21] >> 5) {
    case 1: bitdepth = 8; break;
    case 2: bitdepth = 10; break;
    case 3: bitdepth = 12; break;
    default:
        av_log(ctx->avctx, AV_LOG_ERROR,
               "Unknown bitdepth indicator (%d)\n", buf[0x21] >> 5);
        return AVERROR_INVALIDDATA;
    }

    cid = AV_RB32(buf + 0x28);

    ctx->avctx->profile = dnxhd_get_profile(cid);

    if ((ret = dnxhd_init_vlc(ctx, cid, bitdepth)) < 0)
        return ret;
    if (ctx->mbaff && ctx->cid_table->cid != 1260)
        av_log(ctx->avctx, AV_LOG_WARNING,
               "Adaptive MB interlace flag in an unsupported profile.\n");

    switch ((buf[0x2C] >> 1) & 3) {
    case 0: frame->colorspace = AVCOL_SPC_BT709;       break;
    case 1: frame->colorspace = AVCOL_SPC_BT2020_NCL;  break;
    case 2: frame->colorspace = AVCOL_SPC_BT2020_CL;   break;
    case 3: frame->colorspace = AVCOL_SPC_UNSPECIFIED; break;
    }

    ctx->act = buf[0x2C] & 1;
    if (ctx->act && ctx->cid_table->cid != 1256 && ctx->cid_table->cid != 1270)
        av_log(ctx->avctx, AV_LOG_WARNING,
               "Adaptive color transform in an unsupported profile.\n");

    ctx->is_444 = (buf[0x2C] >> 6) & 1;
    if (ctx->is_444) {
        if (bitdepth == 8) {
            avpriv_request_sample(ctx->avctx, "4:4:4 8 bits");
            return AVERROR_INVALIDDATA;
        } else if (bitdepth == 10) {
            ctx->decode_dct_block = dnxhd_decode_dct_block_10_444;
            ctx->pix_fmt = ctx->act ? AV_PIX_FMT_YUV444P10
                                    : AV_PIX_FMT_GBRP10;
        } else {
            ctx->decode_dct_block = dnxhd_decode_dct_block_12_444;
            ctx->pix_fmt = ctx->act ? AV_PIX_FMT_YUV444P12
                                    : AV_PIX_FMT_GBRP12;
        }
    } else if (bitdepth == 12) {
        ctx->decode_dct_block = dnxhd_decode_dct_block_12;
        ctx->pix_fmt = AV_PIX_FMT_YUV422P12;
    } else if (bitdepth == 10) {
        if (ctx->avctx->profile == AV_PROFILE_DNXHR_HQX)
            ctx->decode_dct_block = dnxhd_decode_dct_block_10_444;
        else
            ctx->decode_dct_block = dnxhd_decode_dct_block_10;
        ctx->pix_fmt = AV_PIX_FMT_YUV422P10;
    } else {
        ctx->decode_dct_block = dnxhd_decode_dct_block_8;
        ctx->pix_fmt = AV_PIX_FMT_YUV422P;
    }

    ctx->avctx->bits_per_raw_sample = ctx->bit_depth = bitdepth;
    if (ctx->bit_depth != old_bit_depth) {
        ff_blockdsp_init(&ctx->bdsp);
        ff_idctdsp_init(&ctx->idsp, ctx->avctx);
        ff_permute_scantable(ctx->permutated_scantable, ff_zigzag_direct,
                             ctx->idsp.idct_permutation);
    }

    // make sure profile size constraints are respected
    // DNx100 allows 1920->1440 and 1280->960 subsampling
    if (ctx->width != ctx->cid_table->width &&
        ctx->cid_table->width != DNXHD_VARIABLE) {
        av_reduce(&ctx->avctx->sample_aspect_ratio.num,
                  &ctx->avctx->sample_aspect_ratio.den,
                  ctx->width, ctx->cid_table->width, 255);
        ctx->width = ctx->cid_table->width;
    }

    if (buf_size < ctx->cid_table->coding_unit_size) {
        av_log(ctx->avctx, AV_LOG_ERROR, "incorrect frame size (%d < %u).\n",
               buf_size, ctx->cid_table->coding_unit_size);
        return AVERROR_INVALIDDATA;
    }

    ctx->mb_width  = (ctx->width + 15)>> 4;
    ctx->mb_height = AV_RB16(buf + 0x16c);

    if ((ctx->height + 15) >> 4 == ctx->mb_height && (frame->flags & AV_FRAME_FLAG_INTERLACED))
        ctx->height <<= 1;

    av_log(ctx->avctx, AV_LOG_VERBOSE, "%dx%d, 4:%s %d bits, MBAFF=%d ACT=%d\n",
           ctx->width, ctx->height, ctx->is_444 ? "4:4" : "2:2",
           ctx->bit_depth, ctx->mbaff, ctx->act);

    // Newer format supports variable mb_scan_index sizes
    if (ctx->mb_height > 68 && ff_dnxhd_check_header_prefix_hr(header_prefix)) {
        ctx->data_offset = 0x170 + (ctx->mb_height << 2);
    } else {
        if (ctx->mb_height > 68) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "mb height too big: %d\n", ctx->mb_height);
            return AVERROR_INVALIDDATA;
        }
        ctx->data_offset = 0x280;
    }
    if ((ctx->mb_height << !!(frame->flags & AV_FRAME_FLAG_INTERLACED)) > (ctx->height + 15) >> 4) {
        av_log(ctx->avctx, AV_LOG_ERROR,
                "mb height too big: %d\n", ctx->mb_height);
        return AVERROR_INVALIDDATA;
    }

    if (buf_size < ctx->data_offset) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "buffer too small (%d < %d).\n", buf_size, ctx->data_offset);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->mb_height > FF_ARRAY_ELEMS(ctx->mb_scan_index)) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "mb_height too big (%d > %"SIZE_SPECIFIER").\n", ctx->mb_height, FF_ARRAY_ELEMS(ctx->mb_scan_index));
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < ctx->mb_height; i++) {
        ctx->mb_scan_index[i] = AV_RB32(buf + 0x170 + (i << 2));
        ff_dlog(ctx->avctx, "mb scan index %d, pos %d: %"PRIu32"\n",
                i, 0x170 + (i << 2), ctx->mb_scan_index[i]);
        if (buf_size - ctx->data_offset < ctx->mb_scan_index[i]) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "invalid mb scan index (%"PRIu32" vs %u).\n",
                   ctx->mb_scan_index[i], buf_size - ctx->data_offset);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static av_always_inline int dnxhd_decode_dct_block(const DNXHDContext *ctx,
                                                   RowContext *row,
                                                   int n,
                                                   int index_bits,
                                                   int level_bias,
                                                   int level_shift,
                                                   int dc_shift)
{
    int i, j, index1, len, flags;
    int level, component, sign;
    const int *scale;
    const uint8_t *weight_matrix;
    const uint8_t *ac_info = ctx->cid_table->ac_info;
    int16_t *block = row->blocks[n];
    const int eob_index     = ctx->cid_table->eob_index;
    int ret = 0;
    OPEN_READER(bs, &row->gb);

    ctx->bdsp.clear_block(block);

    if (!ctx->is_444) {
        if (n & 2) {
            component     = 1 + (n & 1);
            scale = row->chroma_scale;
            weight_matrix = ctx->cid_table->chroma_weight;
        } else {
            component     = 0;
            scale = row->luma_scale;
            weight_matrix = ctx->cid_table->luma_weight;
        }
    } else {
        component = (n >> 1) % 3;
        if (component) {
            scale = row->chroma_scale;
            weight_matrix = ctx->cid_table->chroma_weight;
        } else {
            scale = row->luma_scale;
            weight_matrix = ctx->cid_table->luma_weight;
        }
    }

    UPDATE_CACHE(bs, &row->gb);
    GET_VLC(len, bs, &row->gb, ctx->dc_vlc.table, DNXHD_DC_VLC_BITS, 1);
    if (len < 0) {
        ret = len;
        goto error;
    }
    if (len) {
        level = GET_CACHE(bs, &row->gb);
        LAST_SKIP_BITS(bs, &row->gb, len);
        sign  = ~level >> 31;
        level = (NEG_USR32(sign ^ level, len) ^ sign) - sign;
        row->last_dc[component] += level * (1 << dc_shift);
    }
    block[0] = row->last_dc[component];

    i = 0;

    UPDATE_CACHE(bs, &row->gb);
    GET_VLC(index1, bs, &row->gb, ctx->ac_vlc.table,
            DNXHD_VLC_BITS, 2);

    while (index1 != eob_index) {
        level = ac_info[2*index1+0];
        flags = ac_info[2*index1+1];

        sign = SHOW_SBITS(bs, &row->gb, 1);
        SKIP_BITS(bs, &row->gb, 1);

        if (flags & 1) {
            level += SHOW_UBITS(bs, &row->gb, index_bits) << 7;
            SKIP_BITS(bs, &row->gb, index_bits);
        }

        if (flags & 2) {
            int run;
            UPDATE_CACHE(bs, &row->gb);
            GET_VLC(run, bs, &row->gb, ctx->run_vlc.table,
                    DNXHD_VLC_BITS, 2);
            i += run;
        }

        if (++i > 63) {
            av_log(ctx->avctx, AV_LOG_ERROR, "ac tex damaged %d, %d\n", n, i);
            ret = -1;
            break;
        }

        j      = ctx->permutated_scantable[i];
        level *= scale[i];
        level += scale[i] >> 1;
        if (level_bias < 32 || weight_matrix[i] != level_bias)
            level += level_bias; // 1<<(level_shift-1)
        level >>= level_shift;

        block[j] = (level ^ sign) - sign;

        UPDATE_CACHE(bs, &row->gb);
        GET_VLC(index1, bs, &row->gb, ctx->ac_vlc.table,
                DNXHD_VLC_BITS, 2);
    }
error:
    CLOSE_READER(bs, &row->gb);
    return ret;
}

static int dnxhd_decode_dct_block_8(const DNXHDContext *ctx,
                                    RowContext *row, int n)
{
    return dnxhd_decode_dct_block(ctx, row, n, 4, 32, 6, 0);
}

static int dnxhd_decode_dct_block_10(const DNXHDContext *ctx,
                                     RowContext *row, int n)
{
    return dnxhd_decode_dct_block(ctx, row, n, 6, 8, 4, 0);
}

static int dnxhd_decode_dct_block_10_444(const DNXHDContext *ctx,
                                         RowContext *row, int n)
{
    return dnxhd_decode_dct_block(ctx, row, n, 6, 32, 6, 0);
}

static int dnxhd_decode_dct_block_12(const DNXHDContext *ctx,
                                     RowContext *row, int n)
{
    return dnxhd_decode_dct_block(ctx, row, n, 6, 8, 4, 2);
}

static int dnxhd_decode_dct_block_12_444(const DNXHDContext *ctx,
                                         RowContext *row, int n)
{
    return dnxhd_decode_dct_block(ctx, row, n, 6, 32, 4, 2);
}

static int dnxhd_decode_macroblock(const DNXHDContext *ctx, RowContext *row,
                                   AVFrame *frame, int x, int y)
{
    int shift1 = ctx->bit_depth >= 10;
    int dct_linesize_luma   = frame->linesize[0];
    int dct_linesize_chroma = frame->linesize[1];
    uint8_t *dest_y, *dest_u, *dest_v;
    int dct_y_offset, dct_x_offset;
    int qscale, i, act;
    int interlaced_mb = 0;

    if (ctx->mbaff) {
        interlaced_mb = get_bits1(&row->gb);
        qscale = get_bits(&row->gb, 10);
    } else {
        qscale = get_bits(&row->gb, 11);
    }
    act = get_bits1(&row->gb);
    if (act) {
        if (!ctx->act) {
            static int act_warned;
            if (!act_warned) {
                act_warned = 1;
                av_log(ctx->avctx, AV_LOG_ERROR,
                       "ACT flag set, in violation of frame header.\n");
            }
        } else if (row->format == -1) {
            row->format = act;
        } else if (row->format != act) {
            row->format = 2; // Variable
        }
    }

    if (qscale != row->last_qscale) {
        for (i = 0; i < 64; i++) {
            row->luma_scale[i]   = qscale * ctx->cid_table->luma_weight[i];
            row->chroma_scale[i] = qscale * ctx->cid_table->chroma_weight[i];
        }
        row->last_qscale = qscale;
    }

    for (i = 0; i < 8 + 4 * ctx->is_444; i++) {
        if (ctx->decode_dct_block(ctx, row, i) < 0)
            return AVERROR_INVALIDDATA;
    }

    if (frame->flags & AV_FRAME_FLAG_INTERLACED) {
        dct_linesize_luma   <<= 1;
        dct_linesize_chroma <<= 1;
    }

    dest_y = frame->data[0] + ((y * dct_linesize_luma)   << 4) + (x << (4 + shift1));
    dest_u = frame->data[1] + ((y * dct_linesize_chroma) << 4) + (x << (3 + shift1 + ctx->is_444));
    dest_v = frame->data[2] + ((y * dct_linesize_chroma) << 4) + (x << (3 + shift1 + ctx->is_444));

    if ((frame->flags & AV_FRAME_FLAG_INTERLACED) && ctx->cur_field) {
        dest_y += frame->linesize[0];
        dest_u += frame->linesize[1];
        dest_v += frame->linesize[2];
    }
    if (interlaced_mb) {
        dct_linesize_luma   <<= 1;
        dct_linesize_chroma <<= 1;
    }

    dct_y_offset = interlaced_mb ? frame->linesize[0] : (dct_linesize_luma << 3);
    dct_x_offset = 8 << shift1;
    if (!ctx->is_444) {
        ctx->idsp.idct_put(dest_y,                               dct_linesize_luma, row->blocks[0]);
        ctx->idsp.idct_put(dest_y + dct_x_offset,                dct_linesize_luma, row->blocks[1]);
        ctx->idsp.idct_put(dest_y + dct_y_offset,                dct_linesize_luma, row->blocks[4]);
        ctx->idsp.idct_put(dest_y + dct_y_offset + dct_x_offset, dct_linesize_luma, row->blocks[5]);

        if (!(ctx->avctx->flags & AV_CODEC_FLAG_GRAY)) {
            dct_y_offset = interlaced_mb ? frame->linesize[1] : (dct_linesize_chroma << 3);
            ctx->idsp.idct_put(dest_u,                dct_linesize_chroma, row->blocks[2]);
            ctx->idsp.idct_put(dest_v,                dct_linesize_chroma, row->blocks[3]);
            ctx->idsp.idct_put(dest_u + dct_y_offset, dct_linesize_chroma, row->blocks[6]);
            ctx->idsp.idct_put(dest_v + dct_y_offset, dct_linesize_chroma, row->blocks[7]);
        }
    } else {
        ctx->idsp.idct_put(dest_y,                               dct_linesize_luma, row->blocks[0]);
        ctx->idsp.idct_put(dest_y + dct_x_offset,                dct_linesize_luma, row->blocks[1]);
        ctx->idsp.idct_put(dest_y + dct_y_offset,                dct_linesize_luma, row->blocks[6]);
        ctx->idsp.idct_put(dest_y + dct_y_offset + dct_x_offset, dct_linesize_luma, row->blocks[7]);

        if (!(ctx->avctx->flags & AV_CODEC_FLAG_GRAY)) {
            dct_y_offset = interlaced_mb ? frame->linesize[1] : (dct_linesize_chroma << 3);
            ctx->idsp.idct_put(dest_u,                               dct_linesize_chroma, row->blocks[2]);
            ctx->idsp.idct_put(dest_u + dct_x_offset,                dct_linesize_chroma, row->blocks[3]);
            ctx->idsp.idct_put(dest_u + dct_y_offset,                dct_linesize_chroma, row->blocks[8]);
            ctx->idsp.idct_put(dest_u + dct_y_offset + dct_x_offset, dct_linesize_chroma, row->blocks[9]);
            ctx->idsp.idct_put(dest_v,                               dct_linesize_chroma, row->blocks[4]);
            ctx->idsp.idct_put(dest_v + dct_x_offset,                dct_linesize_chroma, row->blocks[5]);
            ctx->idsp.idct_put(dest_v + dct_y_offset,                dct_linesize_chroma, row->blocks[10]);
            ctx->idsp.idct_put(dest_v + dct_y_offset + dct_x_offset, dct_linesize_chroma, row->blocks[11]);
        }
    }

    return 0;
}

static int dnxhd_decode_row(AVCodecContext *avctx, void *data,
                            int rownb, int threadnb)
{
    const DNXHDContext *ctx = avctx->priv_data;
    uint32_t offset = ctx->mb_scan_index[rownb];
    RowContext *row = ctx->rows + threadnb;
    int x, ret;

    row->last_dc[0] =
    row->last_dc[1] =
    row->last_dc[2] = 1 << (ctx->bit_depth + 2); // for levels +2^(bitdepth-1)
    ret = init_get_bits8(&row->gb, ctx->buf + offset, ctx->buf_size - offset);
    if (ret < 0) {
        row->errors++;
        return ret;
    }
    for (x = 0; x < ctx->mb_width; x++) {
        int ret = dnxhd_decode_macroblock(ctx, row, data, x, rownb);
        if (ret < 0) {
            row->errors++;
            return ret;
        }
    }

    return 0;
}

static int dnxhd_decode_frame(AVCodecContext *avctx, AVFrame *picture,
                              int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    DNXHDContext *ctx = avctx->priv_data;
    int first_field = 1;
    int ret, i;

    ff_dlog(avctx, "frame size %d\n", buf_size);

    for (i = 0; i < avctx->thread_count; i++)
        ctx->rows[i].format = -1;

decode_coding_unit:
    if ((ret = dnxhd_decode_header(ctx, picture, buf, buf_size, first_field)) < 0)
        return ret;

    if ((avctx->width || avctx->height) &&
        (ctx->width != avctx->width || ctx->height != avctx->height)) {
        av_log(avctx, AV_LOG_WARNING, "frame size changed: %dx%d -> %ux%u\n",
               avctx->width, avctx->height, ctx->width, ctx->height);
        first_field = 1;
    }
    if (avctx->pix_fmt != AV_PIX_FMT_NONE && avctx->pix_fmt != ctx->pix_fmt) {
        av_log(avctx, AV_LOG_WARNING, "pix_fmt changed: %s -> %s\n",
               av_get_pix_fmt_name(avctx->pix_fmt), av_get_pix_fmt_name(ctx->pix_fmt));
        first_field = 1;
    }

    avctx->pix_fmt = ctx->pix_fmt;
    ret = ff_set_dimensions(avctx, ctx->width, ctx->height);
    if (ret < 0)
        return ret;

    if (first_field) {
        if ((ret = ff_thread_get_buffer(avctx, picture, 0)) < 0)
            return ret;
    }

    ctx->buf_size = buf_size - ctx->data_offset;
    ctx->buf = buf + ctx->data_offset;
    avctx->execute2(avctx, dnxhd_decode_row, picture, NULL, ctx->mb_height);

    if (first_field && (picture->flags & AV_FRAME_FLAG_INTERLACED)) {
        buf      += ctx->cid_table->coding_unit_size;
        buf_size -= ctx->cid_table->coding_unit_size;
        first_field = 0;
        goto decode_coding_unit;
    }

    ret = 0;
    for (i = 0; i < avctx->thread_count; i++) {
        ret += ctx->rows[i].errors;
        ctx->rows[i].errors = 0;
    }

    if (ctx->act) {
        static int act_warned;
        int format = ctx->rows[0].format;
        for (i = 1; i < avctx->thread_count; i++) {
            if (ctx->rows[i].format != format &&
                ctx->rows[i].format != -1 /* not run */) {
                format = 2;
                break;
            }
        }
        switch (format) {
        case -1:
        case 2:
            if (!act_warned) {
                act_warned = 1;
                av_log(ctx->avctx, AV_LOG_ERROR,
                       "Unsupported: variable ACT flag.\n");
            }
            break;
        case 0:
            ctx->pix_fmt = ctx->bit_depth==10
                         ? AV_PIX_FMT_GBRP10 : AV_PIX_FMT_GBRP12;
            break;
        case 1:
            ctx->pix_fmt = ctx->bit_depth==10
                         ? AV_PIX_FMT_YUV444P10 : AV_PIX_FMT_YUV444P12;
            break;
        }
    }
    avctx->pix_fmt = ctx->pix_fmt;
    if (ret) {
        av_log(ctx->avctx, AV_LOG_ERROR, "%d lines with errors\n", ret);
        return AVERROR_INVALIDDATA;
    }

    *got_frame = 1;
    return avpkt->size;
}

static av_cold int dnxhd_decode_close(AVCodecContext *avctx)
{
    DNXHDContext *ctx = avctx->priv_data;

    ff_vlc_free(&ctx->ac_vlc);
    ff_vlc_free(&ctx->dc_vlc);
    ff_vlc_free(&ctx->run_vlc);

    av_freep(&ctx->rows);

    return 0;
}

const FFCodec ff_dnxhd_decoder = {
    .p.name         = "dnxhd",
    CODEC_LONG_NAME("VC3/DNxHD"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_DNXHD,
    .priv_data_size = sizeof(DNXHDContext),
    .init           = dnxhd_decode_init,
    .close          = dnxhd_decode_close,
    FF_CODEC_DECODE_CB(dnxhd_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_SLICE_THREADS,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_dnxhd_profiles),
};
