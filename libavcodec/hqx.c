/*
 * Canopus HQX decoder
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

#include <inttypes.h>

#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "canopus.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "thread.h"
#include "vlc.h"

#include "hqxdsp.h"
#include "hqxvlc.h"
#include "hq_common.h"

/* HQX has four modes - 422, 444, 422alpha and 444alpha - all 12-bit */
enum HQXFormat {
    HQX_422 = 0,
    HQX_444,
    HQX_422A,
    HQX_444A,
};

struct HQXContext;

typedef int (*mb_decode_func)(struct HQXContext *ctx,
                              int slice_no, int x, int y);

typedef struct HQXSlice {
    GetBitContext gb;
    DECLARE_ALIGNED(16, int16_t, block)[16][64];
} HQXSlice;

typedef struct HQXContext {
    HQXDSPContext hqxdsp;
    HQXSlice slice[16];

    AVFrame *pic;
    mb_decode_func decode_func;

    int format, dcb, width, height;
    int interlaced;

    const uint8_t *src;
    unsigned int data_size;
    uint32_t slice_off[17];

    const VLCElem *dc_vlc;

    VLC dc11_vlc;
} HQXContext;

#define HQX_HEADER_SIZE 59

#define AC_IDX(q) ((q) >= 128 ? HQX_AC_Q128 : (q) >= 64 ? HQX_AC_Q64 : \
                   (q) >=  32 ? HQX_AC_Q32  : (q) >= 16 ? HQX_AC_Q16 : \
                   (q) >=   8 ? HQX_AC_Q8   : HQX_AC_Q0)

/* macroblock selects a group of 4 possible quants and
 * a block can use any of those four quantisers
 * one column is powers of 2, the other one is powers of 2 * 3,
 * then there is the special one, powers of 2 * 5.
 * We also encode the corresponding AC index in these tables in bits 29-31. */
static const unsigned hqx_quants[16][4] = {
#define Q(q) ((unsigned)AC_IDX(q) << 29 | (q))
    { Q( 0x1), Q(  0x2), Q(  0x4), Q(  0x8) }, { Q( 0x1), Q( 0x3), Q(  0x6), Q(  0xC) },
    { Q( 0x2), Q(  0x4), Q(  0x8), Q( 0x10) }, { Q( 0x3), Q( 0x6), Q(  0xC), Q( 0x18) },
    { Q( 0x4), Q(  0x8), Q( 0x10), Q( 0x20) }, { Q( 0x6), Q( 0xC), Q( 0x18), Q( 0x30) },
    { Q( 0x8), Q( 0x10), Q( 0x20), Q( 0x40) },
                            { Q(0xA), Q(0x14), Q(0x28), Q(0x50) },
                                               { Q( 0xC), Q(0x18), Q( 0x30), Q( 0x60) },
    { Q(0x10), Q( 0x20), Q( 0x40), Q( 0x80) }, { Q(0x18), Q(0x30), Q( 0x60), Q( 0xC0) },
    { Q(0x20), Q( 0x40), Q( 0x80), Q(0x100) }, { Q(0x30), Q(0x60), Q( 0xC0), Q(0x180) },
    { Q(0x40), Q( 0x80), Q(0x100), Q(0x200) }, { Q(0x60), Q(0xC0), Q(0x180), Q(0x300) },
    { Q(0x80), Q(0x100), Q(0x200), Q(0x400) }
};

static const uint8_t hqx_quant_luma[64] = {
    16,  16,  16,  19,  19,  19,  42,  44,
    16,  16,  19,  19,  19,  38,  43,  45,
    16,  19,  19,  19,  40,  41,  45,  48,
    19,  19,  19,  40,  41,  42,  46,  49,
    19,  19,  40,  41,  42,  43,  48, 101,
    19,  38,  41,  42,  43,  44,  98, 104,
    42,  43,  45,  46,  48,  98, 109, 116,
    44,  45,  48,  49, 101, 104, 116, 123,
};

static const uint8_t hqx_quant_chroma[64] = {
    16,  16,  19,  25,  26,  26,  42,  44,
    16,  19,  25,  25,  26,  38,  43,  91,
    19,  25,  26,  27,  40,  41,  91,  96,
    25,  25,  27,  40,  41,  84,  93, 197,
    26,  26,  40,  41,  84,  86, 191, 203,
    26,  38,  41,  84,  86, 177, 197, 209,
    42,  43,  91,  93, 191, 197, 219, 232,
    44,  91,  96, 197, 203, 209, 232, 246,
};

static inline void put_blocks(HQXContext *ctx, int plane,
                              int x, int y, int ilace,
                              int16_t *block0, int16_t *block1,
                              const uint8_t *quant)
{
    int fields = ilace ? 2 : 1;
    int lsize = ctx->pic->linesize[plane];
    uint8_t *p = ctx->pic->data[plane] + x * 2;

    ctx->hqxdsp.idct_put((uint16_t *)(p + y * lsize),
                         lsize * fields, block0, quant);
    ctx->hqxdsp.idct_put((uint16_t *)(p + (y + (ilace ? 1 : 8)) * lsize),
                         lsize * fields, block1, quant);
}

static inline void hqx_get_ac(GetBitContext *gb, const HQXAC *ac,
                              int *runp, int *lev)
{
    int level, run;
    OPEN_READER(re, gb);

    UPDATE_CACHE(re, gb);
    GET_RL_VLC(level, run, re, gb, ac->lut, ac->bits, 2, 0);
    CLOSE_READER(re, gb);
    *runp = run;
    *lev  = level;
}

static int decode_block(GetBitContext *gb, const VLCElem vlc[],
                        const unsigned *quants, int dcb,
                        int16_t block[64], int *last_dc)
{
    int run, lev, pos = 0;
    unsigned ac_idx, q;
    int dc;

    dc = get_vlc2(gb, vlc, HQX_DC_VLC_BITS, 2);
    *last_dc += dc;

    block[0] = sign_extend(*last_dc << (12 - dcb), 12);

    q = quants[get_bits(gb, 2)];
    // ac_idx is encoded in the high bits of quants;
    // because block is 16 bit, we do not even need to clear said bits.
    ac_idx = q >> 29;

    do {
        hqx_get_ac(gb, &hqx_ac[ac_idx], &run, &lev);
        pos += run;
        if (pos > 63)
            break;
        block[ff_zigzag_direct[pos]] = lev * q;
    } while (pos < 63);

    return 0;
}

static int hqx_decode_422(HQXContext *ctx, int slice_no, int x, int y)
{
    HQXSlice *slice = &ctx->slice[slice_no];
    GetBitContext *gb = &slice->gb;
    const unsigned *quants;
    int flag;
    int last_dc;
    int i, ret;

    memset(slice->block, 0, sizeof(*slice->block) * 8);

    if (ctx->interlaced)
        flag = get_bits1(gb);
    else
        flag = 0;

    quants = hqx_quants[get_bits(gb, 4)];

    for (i = 0; i < 8; i++) {
        if (i == 0 || i == 4 || i == 6)
            last_dc = 0;
        ret = decode_block(gb, ctx->dc_vlc, quants,
                           ctx->dcb, slice->block[i], &last_dc);
        if (ret < 0)
            return ret;
    }

    put_blocks(ctx, 0, x,      y, flag, slice->block[0], slice->block[2], hqx_quant_luma);
    put_blocks(ctx, 0, x + 8,  y, flag, slice->block[1], slice->block[3], hqx_quant_luma);
    put_blocks(ctx, 2, x >> 1, y, flag, slice->block[4], slice->block[5], hqx_quant_chroma);
    put_blocks(ctx, 1, x >> 1, y, flag, slice->block[6], slice->block[7], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_422a(HQXContext *ctx, int slice_no, int x, int y)
{
    HQXSlice *slice = &ctx->slice[slice_no];
    GetBitContext *gb = &slice->gb;
    int flag = 0;
    int last_dc;
    int i, ret;

    memset(slice->block, 0, sizeof(*slice->block) * 12);
    for (i = 0; i < 12; i++)
        slice->block[i][0] = -0x800;

    int cbp = get_vlc2(gb, ff_hq_cbp_vlc, HQ_CBP_VLC_BITS, 1);
    if (cbp) {
        const unsigned *quants;

        if (ctx->interlaced)
            flag = get_bits1(gb);

        quants = hqx_quants[get_bits(gb, 4)];

        if (cbp & 0x3)   // chroma CBP - top
            cbp |= 0x500;
        if (cbp & 0xC)   // chroma CBP - bottom
            cbp |= 0xA00;
        for (i = 0; i < 12; i++) {
            if (i == 0 || i == 4 || i == 8 || i == 10)
                last_dc = 0;
            if (cbp & (1 << i)) {
                ret = decode_block(gb, ctx->dc_vlc, quants,
                                   ctx->dcb, slice->block[i], &last_dc);
                if (ret < 0)
                    return ret;
            }
        }
    }

    put_blocks(ctx, 3, x,      y, flag, slice->block[ 0], slice->block[ 2], hqx_quant_luma);
    put_blocks(ctx, 3, x + 8,  y, flag, slice->block[ 1], slice->block[ 3], hqx_quant_luma);
    put_blocks(ctx, 0, x,      y, flag, slice->block[ 4], slice->block[ 6], hqx_quant_luma);
    put_blocks(ctx, 0, x + 8,  y, flag, slice->block[ 5], slice->block[ 7], hqx_quant_luma);
    put_blocks(ctx, 2, x >> 1, y, flag, slice->block[ 8], slice->block[ 9], hqx_quant_chroma);
    put_blocks(ctx, 1, x >> 1, y, flag, slice->block[10], slice->block[11], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_444(HQXContext *ctx, int slice_no, int x, int y)
{
    HQXSlice *slice = &ctx->slice[slice_no];
    GetBitContext *gb = &slice->gb;
    const unsigned *quants;
    int flag;
    int last_dc;
    int i, ret;

    memset(slice->block, 0, sizeof(*slice->block) * 12);

    if (ctx->interlaced)
        flag = get_bits1(gb);
    else
        flag = 0;

    quants = hqx_quants[get_bits(gb, 4)];

    for (i = 0; i < 12; i++) {
        if (!(i & 3))
            last_dc = 0;
        ret = decode_block(gb, ctx->dc_vlc, quants,
                           ctx->dcb, slice->block[i], &last_dc);
        if (ret < 0)
            return ret;
    }

    put_blocks(ctx, 0, x,     y, flag, slice->block[0], slice->block[ 2], hqx_quant_luma);
    put_blocks(ctx, 0, x + 8, y, flag, slice->block[1], slice->block[ 3], hqx_quant_luma);
    put_blocks(ctx, 2, x,     y, flag, slice->block[4], slice->block[ 6], hqx_quant_chroma);
    put_blocks(ctx, 2, x + 8, y, flag, slice->block[5], slice->block[ 7], hqx_quant_chroma);
    put_blocks(ctx, 1, x,     y, flag, slice->block[8], slice->block[10], hqx_quant_chroma);
    put_blocks(ctx, 1, x + 8, y, flag, slice->block[9], slice->block[11], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_444a(HQXContext *ctx, int slice_no, int x, int y)
{
    HQXSlice *slice = &ctx->slice[slice_no];
    GetBitContext *gb = &slice->gb;
    int flag = 0;
    int last_dc;
    int i, ret;

    memset(slice->block, 0, sizeof(*slice->block) * 16);
    for (i = 0; i < 16; i++)
        slice->block[i][0] = -0x800;

    int cbp = get_vlc2(gb, ff_hq_cbp_vlc, HQ_CBP_VLC_BITS, 1);
    if (cbp) {
        const unsigned *quants;

        if (ctx->interlaced)
            flag = get_bits1(gb);

        quants = hqx_quants[get_bits(gb, 4)];

        cbp |= cbp << 8; // chroma CBP
        for (i = 0; i < 16; i++) {
            if (!(i & 3))
                last_dc = 0;
            if (cbp & (1 << i)) {
                ret = decode_block(gb, ctx->dc_vlc, quants,
                                   ctx->dcb, slice->block[i], &last_dc);
                if (ret < 0)
                    return ret;
            }
        }
    }

    put_blocks(ctx, 3, x,     y, flag, slice->block[ 0], slice->block[ 2], hqx_quant_luma);
    put_blocks(ctx, 3, x + 8, y, flag, slice->block[ 1], slice->block[ 3], hqx_quant_luma);
    put_blocks(ctx, 0, x,     y, flag, slice->block[ 4], slice->block[ 6], hqx_quant_luma);
    put_blocks(ctx, 0, x + 8, y, flag, slice->block[ 5], slice->block[ 7], hqx_quant_luma);
    put_blocks(ctx, 2, x,     y, flag, slice->block[ 8], slice->block[10], hqx_quant_chroma);
    put_blocks(ctx, 2, x + 8, y, flag, slice->block[ 9], slice->block[11], hqx_quant_chroma);
    put_blocks(ctx, 1, x,     y, flag, slice->block[12], slice->block[14], hqx_quant_chroma);
    put_blocks(ctx, 1, x + 8, y, flag, slice->block[13], slice->block[15], hqx_quant_chroma);

    return 0;
}

static const int shuffle_16[16] = {
    0, 5, 11, 14, 2, 7, 9, 13, 1, 4, 10, 15, 3, 6, 8, 12
};

static int decode_slice(HQXContext *ctx, int slice_no)
{
    int mb_w = (ctx->width  + 15) >> 4;
    int mb_h = (ctx->height + 15) >> 4;
    int grp_w = (mb_w + 4) / 5;
    int grp_h = (mb_h + 4) / 5;
    int grp_h_edge = grp_w * (mb_w / grp_w);
    int grp_v_edge = grp_h * (mb_h / grp_h);
    int grp_v_rest = mb_w - grp_h_edge;
    int grp_h_rest = mb_h - grp_v_edge;
    int num_mbs = mb_w * mb_h;
    int num_tiles = (num_mbs + 479) / 480;
    int std_tile_blocks = num_mbs / (16 * num_tiles);
    int g_tile = slice_no * num_tiles;
    int blk_addr, loc_addr, mb_x, mb_y, pos, loc_row, i;
    int tile_blocks, tile_limit, tile_no;

    for (tile_no = 0; tile_no < num_tiles; tile_no++, g_tile++) {
        tile_blocks = std_tile_blocks;
        tile_limit = -1;
        if (g_tile < num_mbs - std_tile_blocks * 16 * num_tiles) {
            tile_limit = num_mbs / (16 * num_tiles);
            tile_blocks++;
        }
        for (i = 0; i < tile_blocks; i++) {
            if (i == tile_limit)
                blk_addr = g_tile + 16 * num_tiles * i;
            else
                blk_addr = tile_no + 16 * num_tiles * i +
                           num_tiles * shuffle_16[(i + slice_no) & 0xF];
            loc_row  = grp_h * (blk_addr / (grp_h * mb_w));
            loc_addr =          blk_addr % (grp_h * mb_w);
            if (loc_row >= grp_v_edge) {
                mb_x = grp_w * (loc_addr / (grp_h_rest * grp_w));
                pos  =          loc_addr % (grp_h_rest * grp_w);
            } else {
                mb_x = grp_w * (loc_addr / (grp_h * grp_w));
                pos  =          loc_addr % (grp_h * grp_w);
            }
            if (mb_x >= grp_h_edge) {
                mb_x +=            pos % grp_v_rest;
                mb_y  = loc_row + (pos / grp_v_rest);
            } else {
                mb_x +=            pos % grp_w;
                mb_y  = loc_row + (pos / grp_w);
            }
            ctx->decode_func(ctx, slice_no, mb_x * 16, mb_y * 16);
        }
    }

    return 0;
}

static int decode_slice_thread(AVCodecContext *avctx, void *arg,
                               int slice_no, int threadnr)
{
    HQXContext *ctx = avctx->priv_data;
    uint32_t *slice_off = ctx->slice_off;
    int ret;

    if (slice_off[slice_no] < HQX_HEADER_SIZE ||
        slice_off[slice_no] >= slice_off[slice_no + 1] ||
        slice_off[slice_no + 1] > ctx->data_size) {
        av_log(avctx, AV_LOG_ERROR, "Invalid slice size %d.\n", ctx->data_size);
        return AVERROR_INVALIDDATA;
    }

    ret = init_get_bits8(&ctx->slice[slice_no].gb,
                         ctx->src + slice_off[slice_no],
                         slice_off[slice_no + 1] - slice_off[slice_no]);
    if (ret < 0)
        return ret;

    return decode_slice(ctx, slice_no);
}

static int hqx_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_picture_ptr, AVPacket *avpkt)
{
    HQXContext *ctx = avctx->priv_data;
    const uint8_t *src = avpkt->data;
    uint32_t info_tag;
    int data_start, dcb_code;
    int i, ret;

    if (avpkt->size < 4 + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small %d.\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    info_tag    = AV_RL32(src);
    if (info_tag == MKTAG('I', 'N', 'F', 'O')) {
        uint32_t info_offset = AV_RL32(src + 4);
        if (info_offset > INT_MAX || info_offset + 8 > avpkt->size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid INFO header offset: 0x%08"PRIX32" is too large.\n",
                   info_offset);
            return AVERROR_INVALIDDATA;
        }
        ff_canopus_parse_info_tag(avctx, src + 8, info_offset);

        info_offset += 8;
        src         += info_offset;
    }

    data_start     = src - avpkt->data;
    ctx->data_size = avpkt->size - data_start;
    ctx->src       = src;
    ctx->pic       = frame;

    if (ctx->data_size < HQX_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Frame too small.\n");
        return AVERROR_INVALIDDATA;
    }

    if (src[0] != 'H' || src[1] != 'Q') {
        av_log(avctx, AV_LOG_ERROR, "Not an HQX frame.\n");
        return AVERROR_INVALIDDATA;
    }
    ctx->interlaced = !(src[2] & 0x80);
    ctx->format     = src[2] & 7;
    dcb_code        = src[3] & 3;
    ctx->width      = AV_RB16(src + 4);
    ctx->height     = AV_RB16(src + 6);
    for (i = 0; i < 17; i++)
        ctx->slice_off[i] = AV_RB24(src + 8 + i * 3);

    if (dcb_code == 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid DC precision 8.\n");
        return AVERROR_INVALIDDATA;
    }
    ctx->dc_vlc = dcb_code == 3 ? ctx->dc11_vlc.table : dc_vlc[dcb_code - 1];
    ctx->dcb    = dcb_code + 8;
    ret = av_image_check_size(ctx->width, ctx->height, 0, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid stored dimensions %dx%d.\n",
               ctx->width, ctx->height);
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_width         = FFALIGN(ctx->width,  16);
    avctx->coded_height        = FFALIGN(ctx->height, 16);
    avctx->width               = ctx->width;
    avctx->height              = ctx->height;
    avctx->bits_per_raw_sample = 10;

    //The minimum size is 2bit per macroblock
    // hqx_decode_422 & hqx_decode_444 have a unconditionally stored 4bits hqx_quants index
    // hqx_decode_422a & hqx_decode_444a use cbp_vlc which has a minimum length of 2 bits for its VLCs
    // The code rejects slices overlapping in their input data
    if (avctx->coded_width / 16 * (avctx->coded_height / 16) *
        (100 - avctx->discard_damaged_percentage) / 100 > 4LL * avpkt->size)
        return AVERROR_INVALIDDATA;

    switch (ctx->format) {
    case HQX_422:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P16;
        ctx->decode_func = hqx_decode_422;
        break;
    case HQX_444:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P16;
        ctx->decode_func = hqx_decode_444;
        break;
    case HQX_422A:
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P16;
        ctx->decode_func = hqx_decode_422a;
        break;
    case HQX_444A:
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P16;
        ctx->decode_func = hqx_decode_444a;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid format: %d.\n", ctx->format);
        return AVERROR_INVALIDDATA;
    }

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    avctx->execute2(avctx, decode_slice_thread, NULL, NULL, 16);

    *got_picture_ptr = 1;

    return avpkt->size;
}

static av_cold int hqx_decode_close(AVCodecContext *avctx)
{
    HQXContext *ctx = avctx->priv_data;

    ff_vlc_free(&ctx->dc11_vlc);

    return 0;
}

static av_cold int hqx_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    HQXContext *ctx = avctx->priv_data;
    int ret = vlc_init(&ctx->dc11_vlc, HQX_DC_VLC_BITS, FF_ARRAY_ELEMS(dc11_vlc_lens),
                       dc11_vlc_lens, 1, 1, dc11_vlc_bits, 2, 2, 0);

    if (ret < 0)
        return ret;

    ff_hqxdsp_init(&ctx->hqxdsp);

    ff_thread_once(&init_static_once, hqx_init_static);

    return 0;
}

const FFCodec ff_hqx_decoder = {
    .p.name         = "hqx",
    CODEC_LONG_NAME("Canopus HQX"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HQX,
    .priv_data_size = sizeof(HQXContext),
    .init           = hqx_decode_init,
    FF_CODEC_DECODE_CB(hqx_decode_frame),
    .close          = hqx_decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
