/*
 * ClearVideo decoder
 * Copyright (c) 2012-2018 Konstantin Shishkov
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
 * ClearVideo decoder
 */

#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "mathops.h"
#include "clearvideodata.h"

#define CLV_VLC_BITS 9

typedef struct LevelCodes {
    const VLCElem *flags_cb;
    const VLCElem *mv_cb;
    const VLCElem *bias_cb;
} LevelCodes;

typedef struct MV {
    int16_t x, y;
} MV;

static const MV zero_mv = { 0 };

typedef struct MVInfo {
    int mb_w;
    int mb_h;
    int mb_size;
    int mb_stride;
    int top;
    MV  *mv;
} MVInfo;

typedef struct CLVContext {
    AVCodecContext *avctx;
    IDCTDSPContext idsp;
    AVFrame        *pic;
    AVFrame        *prev;
    GetBitContext  gb;
    int            mb_width, mb_height;
    int            pmb_width, pmb_height;
    MVInfo         mvi;
    int            tile_size;
    int            tile_shift;
    int            luma_dc_quant, chroma_dc_quant, ac_quant;
    DECLARE_ALIGNED(16, int16_t, block)[64];
    int            top_dc[3], left_dc[4];
} CLVContext;

static VLCElem    dc_vlc[1104], ac_vlc[554];
static LevelCodes lev[4 + 3 + 3]; // 0..3: Y, 4..6: U, 7..9: V

static inline int decode_block(CLVContext *ctx, int16_t *blk, int has_ac,
                               int ac_quant)
{
    GetBitContext *gb = &ctx->gb;
    int idx = 1, last = 0, val, skip;

    memset(blk, 0, sizeof(*blk) * 64);
    blk[0] = get_vlc2(gb, dc_vlc, CLV_VLC_BITS, 3);

    if (!has_ac)
        return 0;

    while (idx < 64 && !last) {
        val = get_vlc2(gb, ac_vlc, CLV_VLC_BITS, 2);
        if (val < 0)
            return AVERROR_INVALIDDATA;
        if (val != 0x1BFF) {
            last =  val >> 12;
            skip = (val >> 4) & 0xFF;
            val &= 0xF;
            if (get_bits1(gb))
                val = -val;
        } else {
            last = get_bits1(gb);
            skip = get_bits(gb, 6);
            val  = get_sbits(gb, 8);
        }
        if (val) {
            int aval = FFABS(val), sign = val < 0;
            val = ac_quant * (2 * aval + 1);
            if (!(ac_quant & 1))
                val--;
            if (sign)
                val = -val;
        }
        idx += skip;
        if (idx >= 64)
            return AVERROR_INVALIDDATA;
        blk[ff_zigzag_direct[idx++]] = val;
    }

    return (idx <= 64 && last) ? 0 : -1;
}

#define DCT_TEMPLATE(blk, step, bias, shift, dshift, OP)                \
    const int t0 = OP(2841 * blk[1 * step] +  565 * blk[7 * step]);     \
    const int t1 = OP( 565 * blk[1 * step] - 2841 * blk[7 * step]);     \
    const int t2 = OP(1609 * blk[5 * step] + 2408 * blk[3 * step]);     \
    const int t3 = OP(2408 * blk[5 * step] - 1609 * blk[3 * step]);     \
    const int t4 = OP(1108 * blk[2 * step] - 2676 * blk[6 * step]);     \
    const int t5 = OP(2676 * blk[2 * step] + 1108 * blk[6 * step]);     \
    const int t6 = ((blk[0 * step] + blk[4 * step]) * (1 << dshift)) + bias;  \
    const int t7 = ((blk[0 * step] - blk[4 * step]) * (1 << dshift)) + bias;  \
    const int t8 = t0 + t2;                                             \
    const int t9 = t0 - t2;                                             \
    const int tA = (int)(181U * (t9 + (t1 - t3)) + 0x80) >> 8;          \
    const int tB = (int)(181U * (t9 - (t1 - t3)) + 0x80) >> 8;          \
    const int tC = t1 + t3;                                             \
                                                                        \
    blk[0 * step] = (t6 + t5 + t8) >> shift;                            \
    blk[1 * step] = (t7 + t4 + tA) >> shift;                            \
    blk[2 * step] = (t7 - t4 + tB) >> shift;                            \
    blk[3 * step] = (t6 - t5 + tC) >> shift;                            \
    blk[4 * step] = (t6 - t5 - tC) >> shift;                            \
    blk[5 * step] = (t7 - t4 - tB) >> shift;                            \
    blk[6 * step] = (t7 + t4 - tA) >> shift;                            \
    blk[7 * step] = (t6 + t5 - t8) >> shift;                            \

#define ROP(x) x
#define COP(x) (((x) + 4) >> 3)

static void clv_dct(int16_t *block)
{
    int i;
    int16_t *ptr;

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 1, 0x80, 8, 11, ROP);
        ptr += 8;
    }

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 8, 0x2000, 14, 8, COP);
        ptr++;
    }
}

static int decode_mb(CLVContext *c, int x, int y)
{
    int i, has_ac[6], off;

    for (i = 0; i < 6; i++)
        has_ac[i] = get_bits1(&c->gb);

    off = x * 16 + y * 16 * c->pic->linesize[0];
    for (i = 0; i < 4; i++) {
        if (decode_block(c, c->block, has_ac[i], c->ac_quant) < 0)
            return AVERROR_INVALIDDATA;
        if (!x && !(i & 1)) {
            c->block[0] += c->top_dc[0];
            c->top_dc[0] = c->block[0];
        } else {
            c->block[0] += c->left_dc[(i & 2) >> 1];
        }
        c->left_dc[(i & 2) >> 1] = c->block[0];
        c->block[0]             *= c->luma_dc_quant;
        clv_dct(c->block);
        if (i == 2)
            off += c->pic->linesize[0] * 8;
        c->idsp.put_pixels_clamped(c->block,
                                   c->pic->data[0] + off + (i & 1) * 8,
                                   c->pic->linesize[0]);
    }

    off = x * 8 + y * 8 * c->pic->linesize[1];
    for (i = 1; i < 3; i++) {
        if (decode_block(c, c->block, has_ac[i + 3], c->ac_quant) < 0)
            return AVERROR_INVALIDDATA;
        if (!x) {
            c->block[0] += c->top_dc[i];
            c->top_dc[i] = c->block[0];
        } else {
            c->block[0] += c->left_dc[i + 1];
        }
        c->left_dc[i + 1] = c->block[0];
        c->block[0]      *= c->chroma_dc_quant;
        clv_dct(c->block);
        c->idsp.put_pixels_clamped(c->block, c->pic->data[i] + off,
                                   c->pic->linesize[i]);
    }

    return 0;
}

static int copy_block(AVCodecContext *avctx, AVFrame *dst, const AVFrame *src,
                      int plane, int x, int y, int dx, int dy, int size)
{
    int shift = plane > 0;
    int sx = x + dx;
    int sy = y + dy;
    int sstride, dstride, soff, doff;
    uint8_t *dbuf;
    const uint8_t *sbuf;
    int i;

    if (x < 0 || sx < 0 || y < 0 || sy < 0 ||
        x + size > avctx->coded_width >> shift ||
        y + size > avctx->coded_height >> shift ||
        sx + size > avctx->coded_width >> shift ||
        sy + size > avctx->coded_height >> shift)
        return AVERROR_INVALIDDATA;

    sstride = src->linesize[plane];
    dstride = dst->linesize[plane];
    soff    = sx + sy * sstride;
    sbuf    = src->data[plane];
    doff    = x + y * dstride;
    dbuf    = dst->data[plane];

    for (i = 0; i < size; i++) {
        uint8_t *dptr = &dbuf[doff];
        const uint8_t *sptr = &sbuf[soff];

        memcpy(dptr, sptr, size);
        doff += dstride;
        soff += sstride;
    }

    return 0;
}

static int copyadd_block(AVCodecContext *avctx, AVFrame *dst, const AVFrame *src,
                         int plane, int x, int y, int dx, int dy, int size, int bias)
{
    int shift = plane > 0;
    int sx = x + dx;
    int sy = y + dy;
    int sstride   = src->linesize[plane];
    int dstride   = dst->linesize[plane];
    int soff      = sx + sy * sstride;
    const uint8_t *sbuf = src->data[plane];
    int doff      = x + y * dstride;
    uint8_t *dbuf = dst->data[plane];
    int i, j;

    if (x < 0 || sx < 0 || y < 0 || sy < 0 ||
        x + size > avctx->coded_width >> shift ||
        y + size > avctx->coded_height >> shift ||
        sx + size > avctx->coded_width >> shift ||
        sy + size > avctx->coded_height >> shift)
        return AVERROR_INVALIDDATA;

    for (j = 0; j < size; j++) {
        uint8_t *dptr = &dbuf[doff];
        const uint8_t *sptr = &sbuf[soff];

        for (i = 0; i < size; i++) {
            int val = sptr[i] + bias;

            dptr[i] = av_clip_uint8(val);
        }

        doff += dstride;
        soff += sstride;
    }

    return 0;
}

static MV *mvi_predict(MVInfo *mvi, int mb_x, int mb_y)
{
    MV res, pred_mv;
    int left_mv, right_mv, top_mv, bot_mv;

    if (mvi->top) {
        if (mb_x > 0) {
            pred_mv = mvi->mv[mvi->mb_stride + mb_x - 1];
        } else {
            pred_mv = zero_mv;
        }
    } else if ((mb_x == 0) || (mb_x == mvi->mb_w - 1)) {
        pred_mv = mvi->mv[mb_x];
    } else {
        MV A = mvi->mv[mvi->mb_stride + mb_x - 1];
        MV B = mvi->mv[                 mb_x    ];
        MV C = mvi->mv[                 mb_x + 1];
        pred_mv.x = mid_pred(A.x, B.x, C.x);
        pred_mv.y = mid_pred(A.y, B.y, C.y);
    }

    res = pred_mv;

    left_mv = -((mb_x * mvi->mb_size));
    right_mv = ((mvi->mb_w - mb_x - 1) * mvi->mb_size);
    if (res.x < left_mv) {
        res.x = left_mv;
    }
    if (res.x > right_mv) {
        res.x = right_mv;
    }
    top_mv = -((mb_y * mvi->mb_size));
    bot_mv = ((mvi->mb_h - mb_y - 1) * mvi->mb_size);
    if (res.y < top_mv) {
        res.y = top_mv;
    }
    if (res.y > bot_mv) {
        res.y = bot_mv;
    }

    mvi->mv[mvi->mb_stride + mb_x].x = res.x;
    mvi->mv[mvi->mb_stride + mb_x].y = res.y;

    return &mvi->mv[mvi->mb_stride + mb_x];
}

static void mvi_update_prediction(MV *mv, MV diff)
{
    mv->x += diff.x;
    mv->y += diff.y;
}

static void mvi_reset(MVInfo *mvi, int mb_w, int mb_h, int mb_size)
{
    mvi->top       = 1;
    mvi->mb_w      = mb_w;
    mvi->mb_h      = mb_h;
    mvi->mb_size   = mb_size;
    mvi->mb_stride = mb_w;
    memset(mvi->mv, 0, sizeof(MV) * mvi->mb_stride * 2);
}

static void mvi_update_row(MVInfo *mvi)
{
    int i;

    mvi->top = 0;
    for (i = 0 ; i < mvi->mb_stride; i++) {
        mvi->mv[i] = mvi->mv[mvi->mb_stride + i];
    }
}

static int tile_do_block(AVCodecContext *avctx, AVFrame *dst, const AVFrame *src,
                         int plane, int x, int y, int dx, int dy, int size, int bias)
{
    int ret;

    if (!bias) {
        ret = copy_block(avctx, dst, src, plane, x, y, dx, dy, size);
    } else {
        ret = copyadd_block(avctx, dst, src, plane, x, y, dx, dy, size, bias);
    }

    return ret;
}

static int decode_tile(AVCodecContext *avctx, GetBitContext *gb,
                       const LevelCodes *lc,
                       AVFrame *dst, const AVFrame *src,
                       int plane, int x, int y, int size,
                       MV root_mv, MV *pred)
{
    int i, flags = 0;
    int16_t bias = 0;
    MV mv = { 0 };
    int err;

    if (lc->flags_cb)
        flags = get_vlc2(gb, lc->flags_cb, CLV_VLC_BITS, 2);

    if (lc->mv_cb) {
        uint16_t mv_code = get_vlc2(gb, lc->mv_cb, CLV_VLC_BITS, 2);

        if (mv_code != MV_ESC) {
            mv.x = (int8_t)(mv_code & 0xff);
            mv.y = (int8_t)(mv_code >> 8);
        } else {
            mv.x = get_sbits(gb, 8);
            mv.y = get_sbits(gb, 8);
        }
        if (pred)
            mvi_update_prediction(pred, mv);
    }
    mv.x += root_mv.x;
    mv.y += root_mv.y;

    if (lc->bias_cb) {
        uint16_t bias_val = get_vlc2(gb, lc->bias_cb, CLV_VLC_BITS, 2);

        if (bias_val != BIAS_ESC) {
            bias = (int16_t)(bias_val);
        } else {
            bias = get_sbits(gb, 16);
        }
    }

    if (flags) {
        int hsize = size >> 1;
        for (i = 0; i < 4; i++) {
            int xoff = (i & 2) == 0 ? 0 : hsize;
            int yoff = (i & 1) == 0 ? 0 : hsize;

            if (flags & (1 << i)) {
                err = decode_tile(avctx, gb, lc + 1, dst, src, plane,
                                  x + xoff, y + yoff, hsize, root_mv, NULL);
            } else {
                err = tile_do_block(avctx, dst, src, plane, x + xoff, y + yoff,
                                    mv.x, mv.y, hsize, bias);
            }
            if (err < 0)
                return err;
        }
    } else {
        err = tile_do_block(avctx, dst, src, plane, x, y, mv.x, mv.y, size, bias);
        if (err < 0)
            return err;
    }

    return 0;
}

static void extend_edges(AVFrame *buf, int tile_size)
{
    int comp, i, j;

    for (comp = 0; comp < 3; comp++) {
        int shift = comp > 0;
        int w = buf->width  >> shift;
        int h = buf->height >> shift;
        int size = comp == 0 ? tile_size : tile_size >> 1;
        int stride = buf->linesize[comp];
        uint8_t *framebuf = buf->data[comp];

        int right  = size - (w & (size - 1));
        int bottom = size - (h & (size - 1));

        if ((right == size) && (bottom == size)) {
            return;
        }
        if (right != size) {
            int off = w;
            for (j = 0; j < h; j++) {
                for (i = 0; i < right; i++) {
                    framebuf[off + i] = 0x80;
                }
                off += stride;
            }
        }
        if (bottom != size) {
            int off = h * stride;
            for (j = 0; j < bottom; j++) {
                for (i = 0; i < stride; i++) {
                    framebuf[off + i] = 0x80;
                }
                off += stride;
            }
        }
    }
}

static int clv_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CLVContext *c = avctx->priv_data;
    GetByteContext gb;
    uint32_t frame_type;
    int i, j, ret;
    int mb_ret = 0;

    bytestream2_init(&gb, buf, buf_size);
    if (avctx->codec_tag == MKTAG('C', 'L', 'V', '1')) {
        int skip = bytestream2_get_byte(&gb);
        bytestream2_skip(&gb, (skip + 1) * 8);
    }

    frame_type = bytestream2_get_byte(&gb);

    if ((frame_type & 0x7f) == 0x30) {
        *got_frame = 0;
        return buf_size;
    } else if (frame_type & 0x2) {
        if (buf_size < c->mb_width * c->mb_height) {
            av_log(avctx, AV_LOG_ERROR, "Packet too small\n");
            return AVERROR_INVALIDDATA;
        }

        if ((ret = ff_reget_buffer(avctx, c->pic, 0)) < 0)
            return ret;

        c->pic->flags |= AV_FRAME_FLAG_KEY;
        c->pic->pict_type = AV_PICTURE_TYPE_I;

        bytestream2_get_be32(&gb); // frame size;
        c->ac_quant        = bytestream2_get_byte(&gb);
        c->luma_dc_quant   = 32;
        c->chroma_dc_quant = 32;

        if ((ret = init_get_bits8(&c->gb, buf + bytestream2_tell(&gb),
                                  buf_size - bytestream2_tell(&gb))) < 0)
            return ret;

        for (i = 0; i < 3; i++)
            c->top_dc[i] = 32;
        for (i = 0; i < 4; i++)
            c->left_dc[i] = 32;

        for (j = 0; j < c->mb_height; j++) {
            for (i = 0; i < c->mb_width; i++) {
                ret = decode_mb(c, i, j);
                if (ret < 0)
                    mb_ret = ret;
            }
        }
        extend_edges(c->pic, c->tile_size);
    } else {
        int plane;

        if (c->pmb_width * c->pmb_height > 8LL*(buf_size - bytestream2_tell(&gb)))
            return AVERROR_INVALIDDATA;

        if ((ret = ff_reget_buffer(avctx, c->pic, 0)) < 0)
            return ret;

        ret = av_frame_copy(c->pic, c->prev);
        if (ret < 0)
            return ret;

        if ((ret = init_get_bits8(&c->gb, buf + bytestream2_tell(&gb),
                                  buf_size - bytestream2_tell(&gb))) < 0)
            return ret;

        mvi_reset(&c->mvi, c->pmb_width, c->pmb_height, 1 << c->tile_shift);

        for (j = 0; j < c->pmb_height; j++) {
            for (i = 0; i < c->pmb_width; i++) {
                MV *mvp, mv;
                if (get_bits_left(&c->gb) <= 0)
                    return AVERROR_INVALIDDATA;

                mvp = mvi_predict(&c->mvi, i, j);
                mv  = *mvp;
                if (get_bits1(&c->gb)) {
                    for (plane = 0; plane < 3; plane++) {
                        int16_t x = plane == 0 ? i << c->tile_shift : i << (c->tile_shift - 1);
                        int16_t y = plane == 0 ? j << c->tile_shift : j << (c->tile_shift - 1);
                        int16_t size = plane == 0 ? 1 << c->tile_shift : 1 << (c->tile_shift - 1);
                        int16_t mx = plane == 0 ? mv.x : mv.x / 2;
                        int16_t my = plane == 0 ? mv.y : mv.y / 2;

                        ret = copy_block(avctx, c->pic, c->prev, plane, x, y, mx, my, size);
                        if (ret < 0)
                            mb_ret = ret;
                    }
                } else {
                    int x = i << c->tile_shift;
                    int y = j << c->tile_shift;
                    int size = 1 << c->tile_shift;
                    MV cmv;

                    ret = decode_tile(avctx, &c->gb, &lev[0], c->pic, c->prev,  // Y
                                      0, x, y, size, mv, mvp);
                    if (ret < 0)
                        mb_ret = ret;
                    x = i << (c->tile_shift - 1);
                    y = j << (c->tile_shift - 1);
                    size = 1 << (c->tile_shift - 1);
                    cmv = *mvp;
                    cmv.x /= 2;
                    cmv.y /= 2;
                    ret = decode_tile(avctx, &c->gb, &lev[4], c->pic, c->prev,  // U
                                      1, x, y, size, cmv, NULL);
                    if (ret < 0)
                        mb_ret = ret;
                    ret = decode_tile(avctx, &c->gb, &lev[7], c->pic, c->prev,  // U
                                      2, x, y, size, cmv, NULL);
                    if (ret < 0)
                        mb_ret = ret;
                }
            }
            mvi_update_row(&c->mvi);
        }
        extend_edges(c->pic, c->tile_size);

        c->pic->flags &= ~AV_FRAME_FLAG_KEY;
        c->pic->pict_type = AV_PICTURE_TYPE_P;
    }

    if ((ret = av_frame_ref(rframe, c->pic)) < 0)
        return ret;

    FFSWAP(AVFrame *, c->pic, c->prev);

    *got_frame = 1;

    if (get_bits_left(&c->gb) < 0)
        av_log(c->avctx, AV_LOG_WARNING, "overread %d\n", -get_bits_left(&c->gb));

    return mb_ret < 0 ? mb_ret : buf_size;
}

static av_cold const VLCElem *build_vlc(VLCInitState *state,
                                        const uint8_t counts[16],
                                        const uint16_t **syms)
{
    uint8_t lens[MAX_VLC_ENTRIES];
    const uint16_t *symbols = *syms;
    unsigned num = 0;

    for (int i = 0; i < 16; i++) {
        unsigned count = counts[i];
        if (count == 255) /* Special case for Y_3 table */
            count = 303;
        for (count += num; num < count; num++)
            lens[num] = i + 1;
    }
    *syms += num;
    return ff_vlc_init_tables_from_lengths(state, CLV_VLC_BITS, num, lens, 1,
                                           symbols, 2, 2, 0, 0);
}

static av_cold void clv_init_static(void)
{
    static VLCElem vlc_buf[16716];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);
    const uint16_t *mv_syms = clv_mv_syms, *bias_syms = clv_bias_syms;

    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(dc_vlc, CLV_VLC_BITS, NUM_DC_CODES,
                                       clv_dc_lens, 1,
                                       clv_dc_syms, 1, 1, -63, 0);
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(ac_vlc, CLV_VLC_BITS, NUM_AC_CODES,
                                       clv_ac_bits, 1,
                                       clv_ac_syms, 2, 2, 0, 0);
    for (unsigned i = 0, j = 0, k = 0;; i++) {
        if (0x36F & (1 << i)) {
            lev[i].mv_cb = build_vlc(&state, clv_mv_len_counts[k], &mv_syms);
            k++;
        }
        if (i == FF_ARRAY_ELEMS(lev) - 1)
            break;
        if (0x1B7 & (1 << i)) {
            lev[i].flags_cb =
                ff_vlc_init_tables_from_lengths(&state, CLV_VLC_BITS, 16,
                                                clv_flags_bits[j], 1,
                                                clv_flags_syms[j], 1, 1,
                                                0, 0);

            lev[i + 1].bias_cb = build_vlc(&state, clv_bias_len_counts[j],
                                           &bias_syms);
            j++;
        }
    }
}

static av_cold int clv_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    CLVContext *const c = avctx->priv_data;
    int ret, w, h;

    if (avctx->extradata_size == 110) {
        c->tile_size = AV_RL32(&avctx->extradata[94]);
    } else if (avctx->extradata_size == 150) {
        c->tile_size = AV_RB32(&avctx->extradata[134]);
    } else if (!avctx->extradata_size) {
        c->tile_size = 16;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Unsupported extradata size: %d\n", avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    c->tile_shift = av_log2(c->tile_size);
    if (1U << c->tile_shift != c->tile_size || c->tile_shift < 1 || c->tile_shift > 30) {
        av_log(avctx, AV_LOG_ERROR, "Tile size: %d, is not power of 2 > 1 and < 2^31\n", c->tile_size);
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    w = avctx->width;
    h = avctx->height;
    ret = ff_set_dimensions(avctx, FFALIGN(w, 1 << c->tile_shift), FFALIGN(h, 1 << c->tile_shift));
    if (ret < 0)
        return ret;
    avctx->width  = w;
    avctx->height = h;

    c->avctx           = avctx;
    c->mb_width        = FFALIGN(avctx->width,  16) >> 4;
    c->mb_height       = FFALIGN(avctx->height, 16) >> 4;
    c->pmb_width       = (w + c->tile_size - 1) >> c->tile_shift;
    c->pmb_height      = (h + c->tile_size - 1) >> c->tile_shift;
    c->pic             = av_frame_alloc();
    c->prev            = av_frame_alloc();
    c->mvi.mv          = av_calloc(c->pmb_width * 2, sizeof(*c->mvi.mv));
    if (!c->pic || !c->prev || !c->mvi.mv)
        return AVERROR(ENOMEM);

    ff_idctdsp_init(&c->idsp, avctx);

    ff_thread_once(&init_static_once, clv_init_static);

    return 0;
}

static av_cold int clv_decode_end(AVCodecContext *avctx)
{
    CLVContext *const c = avctx->priv_data;

    av_frame_free(&c->prev);
    av_frame_free(&c->pic);

    av_freep(&c->mvi.mv);

    return 0;
}

const FFCodec ff_clearvideo_decoder = {
    .p.name         = "clearvideo",
    CODEC_LONG_NAME("Iterated Systems ClearVideo"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_CLEARVIDEO,
    .priv_data_size = sizeof(CLVContext),
    .init           = clv_decode_init,
    .close          = clv_decode_end,
    FF_CODEC_DECODE_CB(clv_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
