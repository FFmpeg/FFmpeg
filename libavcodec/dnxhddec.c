/*
 * VC3/DNxHD decoder.
 * Copyright (c) 2007 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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

//#define TRACE
//#define DEBUG

#include "avcodec.h"
#include "bitstream.h"
#include "dnxhddata.h"
#include "dsputil.h"

typedef struct {
    AVCodecContext *avctx;
    AVFrame picture;
    GetBitContext gb;
    int cid;                            ///< compression id
    unsigned int width, height;
    unsigned int mb_width, mb_height;
    uint32_t mb_scan_index[68];         /* max for 1080p */
    int cur_field;                      ///< current interlaced field
    VLC ac_vlc, dc_vlc, run_vlc;
    int last_dc[3];
    DSPContext dsp;
    DECLARE_ALIGNED_16(DCTELEM, blocks[8][64]);
    DECLARE_ALIGNED_8(ScanTable, scantable);
    const CIDEntry *cid_table;
} DNXHDContext;

#define DNXHD_VLC_BITS 9
#define DNXHD_DC_VLC_BITS 7

static av_cold int dnxhd_decode_init(AVCodecContext *avctx)
{
    DNXHDContext *ctx = avctx->priv_data;

    ctx->avctx = avctx;
    dsputil_init(&ctx->dsp, avctx);
    avctx->coded_frame = &ctx->picture;
    ctx->picture.type = FF_I_TYPE;
    return 0;
}

static int dnxhd_init_vlc(DNXHDContext *ctx, int cid)
{
    if (!ctx->cid_table) {
        int index;

        if ((index = ff_dnxhd_get_cid_table(cid)) < 0) {
            av_log(ctx->avctx, AV_LOG_ERROR, "unsupported cid %d\n", cid);
            return -1;
        }
        ctx->cid_table = &ff_dnxhd_cid_table[index];
        init_vlc(&ctx->ac_vlc, DNXHD_VLC_BITS, 257,
                 ctx->cid_table->ac_bits, 1, 1,
                 ctx->cid_table->ac_codes, 2, 2, 0);
        init_vlc(&ctx->dc_vlc, DNXHD_DC_VLC_BITS, ctx->cid_table->bit_depth+4,
                 ctx->cid_table->dc_bits, 1, 1,
                 ctx->cid_table->dc_codes, 1, 1, 0);
        init_vlc(&ctx->run_vlc, DNXHD_VLC_BITS, 62,
                 ctx->cid_table->run_bits, 1, 1,
                 ctx->cid_table->run_codes, 2, 2, 0);

        ff_init_scantable(ctx->dsp.idct_permutation, &ctx->scantable, ff_zigzag_direct);
    }
    return 0;
}

static int dnxhd_decode_header(DNXHDContext *ctx, const uint8_t *buf, int buf_size, int first_field)
{
    static const uint8_t header_prefix[] = { 0x00, 0x00, 0x02, 0x80, 0x01 };
    int i;

    if (buf_size < 0x280)
        return -1;

    if (memcmp(buf, header_prefix, 5)) {
        av_log(ctx->avctx, AV_LOG_ERROR, "error in header\n");
        return -1;
    }
    if (buf[5] & 2) { /* interlaced */
        ctx->cur_field = buf[5] & 1;
        ctx->picture.interlaced_frame = 1;
        ctx->picture.top_field_first = first_field ^ ctx->cur_field;
        av_log(ctx->avctx, AV_LOG_DEBUG, "interlaced %d, cur field %d\n", buf[5] & 3, ctx->cur_field);
    }

    ctx->height = AV_RB16(buf + 0x18);
    ctx->width  = AV_RB16(buf + 0x1a);

    dprintf(ctx->avctx, "width %d, heigth %d\n", ctx->width, ctx->height);

    if (buf[0x21] & 0x40) {
        av_log(ctx->avctx, AV_LOG_ERROR, "10 bit per component\n");
        return -1;
    }

    ctx->cid = AV_RB32(buf + 0x28);
    dprintf(ctx->avctx, "compression id %d\n", ctx->cid);

    if (dnxhd_init_vlc(ctx, ctx->cid) < 0)
        return -1;

    if (buf_size < ctx->cid_table->coding_unit_size) {
        av_log(ctx->avctx, AV_LOG_ERROR, "incorrect frame size\n");
        return -1;
    }

    ctx->mb_width = ctx->width>>4;
    ctx->mb_height = buf[0x16d];

    if (ctx->mb_height > 68) {
        av_log(ctx->avctx, AV_LOG_ERROR, "mb height too big\n");
        return -1;
    }

    dprintf(ctx->avctx, "mb width %d, mb height %d\n", ctx->mb_width, ctx->mb_height);
    for (i = 0; i < ctx->mb_height; i++) {
        ctx->mb_scan_index[i] = AV_RB32(buf + 0x170 + (i<<2));
        dprintf(ctx->avctx, "mb scan index %d\n", ctx->mb_scan_index[i]);
        if (buf_size < ctx->mb_scan_index[i] + 0x280) {
            av_log(ctx->avctx, AV_LOG_ERROR, "invalid mb scan index\n");
            return -1;
        }
    }

    return 0;
}

static int dnxhd_decode_dc(DNXHDContext *ctx)
{
    int len;

    len = get_vlc2(&ctx->gb, ctx->dc_vlc.table, DNXHD_DC_VLC_BITS, 1);
    return len ? get_xbits(&ctx->gb, len) : 0;
}

static void dnxhd_decode_dct_block(DNXHDContext *ctx, DCTELEM *block, int n, int qscale)
{
    int i, j, index, index2;
    int level, component, sign;
    const uint8_t *weigth_matrix;

    if (n&2) {
        component = 1 + (n&1);
        weigth_matrix = ctx->cid_table->chroma_weight;
    } else {
        component = 0;
        weigth_matrix = ctx->cid_table->luma_weight;
    }

    ctx->last_dc[component] += dnxhd_decode_dc(ctx);
    block[0] = ctx->last_dc[component];
    //av_log(ctx->avctx, AV_LOG_DEBUG, "dc %d\n", block[0]);
    for (i = 1; ; i++) {
        index = get_vlc2(&ctx->gb, ctx->ac_vlc.table, DNXHD_VLC_BITS, 2);
        //av_log(ctx->avctx, AV_LOG_DEBUG, "index %d\n", index);
        level = ctx->cid_table->ac_level[index];
        if (!level) { /* EOB */
            //av_log(ctx->avctx, AV_LOG_DEBUG, "EOB\n");
            return;
        }
        sign = get_sbits(&ctx->gb, 1);

        if (ctx->cid_table->ac_index_flag[index]) {
            level += get_bits(&ctx->gb, ctx->cid_table->index_bits)<<6;
        }

        if (ctx->cid_table->ac_run_flag[index]) {
            index2 = get_vlc2(&ctx->gb, ctx->run_vlc.table, DNXHD_VLC_BITS, 2);
            i += ctx->cid_table->run[index2];
        }

        if (i > 63) {
            av_log(ctx->avctx, AV_LOG_ERROR, "ac tex damaged %d, %d\n", n, i);
            return;
        }

        j = ctx->scantable.permutated[i];
        //av_log(ctx->avctx, AV_LOG_DEBUG, "j %d\n", j);
        //av_log(ctx->avctx, AV_LOG_DEBUG, "level %d, weigth %d\n", level, weigth_matrix[i]);
        level = (2*level+1) * qscale * weigth_matrix[i];
        if (ctx->cid_table->bit_depth == 10) {
            if (weigth_matrix[i] != 8)
                level += 8;
            level >>= 4;
        } else {
            if (weigth_matrix[i] != 32)
                level += 32;
            level >>= 6;
        }
        //av_log(NULL, AV_LOG_DEBUG, "i %d, j %d, end level %d\n", i, j, level);
        block[j] = (level^sign) - sign;
    }
}

static int dnxhd_decode_macroblock(DNXHDContext *ctx, int x, int y)
{
    int dct_linesize_luma   = ctx->picture.linesize[0];
    int dct_linesize_chroma = ctx->picture.linesize[1];
    uint8_t *dest_y, *dest_u, *dest_v;
    int dct_offset;
    int qscale, i;

    qscale = get_bits(&ctx->gb, 11);
    skip_bits1(&ctx->gb);
    //av_log(ctx->avctx, AV_LOG_DEBUG, "qscale %d\n", qscale);

    for (i = 0; i < 8; i++) {
        ctx->dsp.clear_block(ctx->blocks[i]);
        dnxhd_decode_dct_block(ctx, ctx->blocks[i], i, qscale);
    }

    if (ctx->picture.interlaced_frame) {
        dct_linesize_luma   <<= 1;
        dct_linesize_chroma <<= 1;
    }

    dest_y = ctx->picture.data[0] + ((y * dct_linesize_luma)   << 4) + (x << 4);
    dest_u = ctx->picture.data[1] + ((y * dct_linesize_chroma) << 4) + (x << 3);
    dest_v = ctx->picture.data[2] + ((y * dct_linesize_chroma) << 4) + (x << 3);

    if (ctx->cur_field) {
        dest_y += ctx->picture.linesize[0];
        dest_u += ctx->picture.linesize[1];
        dest_v += ctx->picture.linesize[2];
    }

    dct_offset = dct_linesize_luma << 3;
    ctx->dsp.idct_put(dest_y,                  dct_linesize_luma, ctx->blocks[0]);
    ctx->dsp.idct_put(dest_y + 8,              dct_linesize_luma, ctx->blocks[1]);
    ctx->dsp.idct_put(dest_y + dct_offset,     dct_linesize_luma, ctx->blocks[4]);
    ctx->dsp.idct_put(dest_y + dct_offset + 8, dct_linesize_luma, ctx->blocks[5]);

    if (!(ctx->avctx->flags & CODEC_FLAG_GRAY)) {
        dct_offset = dct_linesize_chroma << 3;
        ctx->dsp.idct_put(dest_u,              dct_linesize_chroma, ctx->blocks[2]);
        ctx->dsp.idct_put(dest_v,              dct_linesize_chroma, ctx->blocks[3]);
        ctx->dsp.idct_put(dest_u + dct_offset, dct_linesize_chroma, ctx->blocks[6]);
        ctx->dsp.idct_put(dest_v + dct_offset, dct_linesize_chroma, ctx->blocks[7]);
    }

    return 0;
}

static int dnxhd_decode_macroblocks(DNXHDContext *ctx, const uint8_t *buf, int buf_size)
{
    int x, y;
    for (y = 0; y < ctx->mb_height; y++) {
        ctx->last_dc[0] =
        ctx->last_dc[1] =
        ctx->last_dc[2] = 1<<(ctx->cid_table->bit_depth+2); // for levels +2^(bitdepth-1)
        init_get_bits(&ctx->gb, buf + ctx->mb_scan_index[y], (buf_size - ctx->mb_scan_index[y]) << 3);
        for (x = 0; x < ctx->mb_width; x++) {
            //START_TIMER;
            dnxhd_decode_macroblock(ctx, x, y);
            //STOP_TIMER("decode macroblock");
        }
    }
    return 0;
}

static int dnxhd_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                              const uint8_t *buf, int buf_size)
{
    DNXHDContext *ctx = avctx->priv_data;
    AVFrame *picture = data;
    int first_field = 1;

    dprintf(avctx, "frame size %d\n", buf_size);

 decode_coding_unit:
    if (dnxhd_decode_header(ctx, buf, buf_size, first_field) < 0)
        return -1;

    avctx->pix_fmt = PIX_FMT_YUV422P;
    if (avcodec_check_dimensions(avctx, ctx->width, ctx->height))
        return -1;
    avcodec_set_dimensions(avctx, ctx->width, ctx->height);

    if (first_field) {
        if (ctx->picture.data[0])
            avctx->release_buffer(avctx, &ctx->picture);
        if (avctx->get_buffer(avctx, &ctx->picture) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return -1;
        }
    }

    dnxhd_decode_macroblocks(ctx, buf + 0x280, buf_size - 0x280);

    if (first_field && ctx->picture.interlaced_frame) {
        buf      += ctx->cid_table->coding_unit_size;
        buf_size -= ctx->cid_table->coding_unit_size;
        first_field = 0;
        goto decode_coding_unit;
    }

    *picture = ctx->picture;
    *data_size = sizeof(AVPicture);
    return buf_size;
}

static av_cold int dnxhd_decode_close(AVCodecContext *avctx)
{
    DNXHDContext *ctx = avctx->priv_data;

    if (ctx->picture.data[0])
        avctx->release_buffer(avctx, &ctx->picture);
    free_vlc(&ctx->ac_vlc);
    free_vlc(&ctx->dc_vlc);
    free_vlc(&ctx->run_vlc);
    return 0;
}

AVCodec dnxhd_decoder = {
    "dnxhd",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DNXHD,
    sizeof(DNXHDContext),
    dnxhd_decode_init,
    NULL,
    dnxhd_decode_close,
    dnxhd_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("VC3/DNxHD"),
};
