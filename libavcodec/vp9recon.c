/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"

#include "threadframe.h"
#include "videodsp.h"
#include "vp9data.h"
#include "vp9dec.h"

static av_always_inline int check_intra_mode(VP9TileData *td, int mode, uint8_t **a,
                                             uint8_t *dst_edge, ptrdiff_t stride_edge,
                                             uint8_t *dst_inner, ptrdiff_t stride_inner,
                                             uint8_t *l, int col, int x, int w,
                                             int row, int y, enum TxfmMode tx,
                                             int p, int ss_h, int ss_v, int bytesperpixel)
{
    VP9Context *s = td->s;
    int have_top = row > 0 || y > 0;
    int have_left = col > td->tile_col_start || x > 0;
    int have_right = x < w - 1;
    int bpp = s->s.h.bpp;
    static const uint8_t mode_conv[10][2 /* have_left */][2 /* have_top */] = {
        [VERT_PRED]            = { { DC_127_PRED,          VERT_PRED            },
                                   { DC_127_PRED,          VERT_PRED            } },
        [HOR_PRED]             = { { DC_129_PRED,          DC_129_PRED          },
                                   { HOR_PRED,             HOR_PRED             } },
        [DC_PRED]              = { { DC_128_PRED,          TOP_DC_PRED          },
                                   { LEFT_DC_PRED,         DC_PRED              } },
        [DIAG_DOWN_LEFT_PRED]  = { { DC_127_PRED,          DIAG_DOWN_LEFT_PRED  },
                                   { DC_127_PRED,          DIAG_DOWN_LEFT_PRED  } },
        [DIAG_DOWN_RIGHT_PRED] = { { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED },
                                   { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED } },
        [VERT_RIGHT_PRED]      = { { VERT_RIGHT_PRED,      VERT_RIGHT_PRED      },
                                   { VERT_RIGHT_PRED,      VERT_RIGHT_PRED      } },
        [HOR_DOWN_PRED]        = { { HOR_DOWN_PRED,        HOR_DOWN_PRED        },
                                   { HOR_DOWN_PRED,        HOR_DOWN_PRED        } },
        [VERT_LEFT_PRED]       = { { DC_127_PRED,          VERT_LEFT_PRED       },
                                   { DC_127_PRED,          VERT_LEFT_PRED       } },
        [HOR_UP_PRED]          = { { DC_129_PRED,          DC_129_PRED          },
                                   { HOR_UP_PRED,          HOR_UP_PRED          } },
        [TM_VP8_PRED]          = { { DC_129_PRED,          VERT_PRED            },
                                   { HOR_PRED,             TM_VP8_PRED          } },
    };
    static const struct {
        uint8_t needs_left:1;
        uint8_t needs_top:1;
        uint8_t needs_topleft:1;
        uint8_t needs_topright:1;
        uint8_t invert_left:1;
    } edges[N_INTRA_PRED_MODES] = {
        [VERT_PRED]            = { .needs_top  = 1 },
        [HOR_PRED]             = { .needs_left = 1 },
        [DC_PRED]              = { .needs_top  = 1, .needs_left = 1 },
        [DIAG_DOWN_LEFT_PRED]  = { .needs_top  = 1, .needs_topright = 1 },
        [DIAG_DOWN_RIGHT_PRED] = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [VERT_RIGHT_PRED]      = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [HOR_DOWN_PRED]        = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [VERT_LEFT_PRED]       = { .needs_top  = 1, .needs_topright = 1 },
        [HOR_UP_PRED]          = { .needs_left = 1, .invert_left = 1 },
        [TM_VP8_PRED]          = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [LEFT_DC_PRED]         = { .needs_left = 1 },
        [TOP_DC_PRED]          = { .needs_top  = 1 },
        [DC_128_PRED]          = { 0 },
        [DC_127_PRED]          = { 0 },
        [DC_129_PRED]          = { 0 }
    };

    av_assert2(mode >= 0 && mode < 10);
    mode = mode_conv[mode][have_left][have_top];
    if (edges[mode].needs_top) {
        uint8_t *top, *topleft;
        int n_px_need = 4 << tx, n_px_have = (((s->cols - col) << !ss_h) - x) * 4;
        int n_px_need_tr = 0;

        if (tx == TX_4X4 && edges[mode].needs_topright && have_right)
            n_px_need_tr = 4;

        // if top of sb64-row, use s->intra_pred_data[] instead of
        // dst[-stride] for intra prediction (it contains pre- instead of
        // post-loopfilter data)
        if (have_top) {
            top = !(row & 7) && !y ?
                s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
                y == 0 ? &dst_edge[-stride_edge] : &dst_inner[-stride_inner];
            if (have_left)
                topleft = !(row & 7) && !y ?
                    s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
                    y == 0 || x == 0 ? &dst_edge[-stride_edge] :
                    &dst_inner[-stride_inner];
        }

        if (have_top &&
            (!edges[mode].needs_topleft || (have_left && top == topleft)) &&
            (tx != TX_4X4 || !edges[mode].needs_topright || have_right) &&
            n_px_need + n_px_need_tr <= n_px_have) {
            *a = top;
        } else {
            if (have_top) {
                if (n_px_need <= n_px_have) {
                    memcpy(*a, top, n_px_need * bytesperpixel);
                } else {
#define memset_bpp(c, i1, v, i2, num) do { \
    if (bytesperpixel == 1) { \
        memset(&(c)[(i1)], (v)[(i2)], (num)); \
    } else { \
        int n, val = AV_RN16A(&(v)[(i2) * 2]); \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[((i1) + n) * 2], val); \
        } \
    } \
} while (0)
                    memcpy(*a, top, n_px_have * bytesperpixel);
                    memset_bpp(*a, n_px_have, (*a), n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
#define memset_val(c, val, num) do { \
    if (bytesperpixel == 1) { \
        memset((c), (val), (num)); \
    } else { \
        int n; \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[n * 2], (val)); \
        } \
    } \
} while (0)
                memset_val(*a, (128 << (bpp - 8)) - 1, n_px_need);
            }
            if (edges[mode].needs_topleft) {
                if (have_left && have_top) {
#define assign_bpp(c, i1, v, i2) do { \
    if (bytesperpixel == 1) { \
        (c)[(i1)] = (v)[(i2)]; \
    } else { \
        AV_COPY16(&(c)[(i1) * 2], &(v)[(i2) * 2]); \
    } \
} while (0)
                    assign_bpp(*a, -1, topleft, -1);
                } else {
#define assign_val(c, i, v) do { \
    if (bytesperpixel == 1) { \
        (c)[(i)] = (v); \
    } else { \
        AV_WN16A(&(c)[(i) * 2], (v)); \
    } \
} while (0)
                    assign_val((*a), -1, (128 << (bpp - 8)) + (have_top ? +1 : -1));
                }
            }
            if (tx == TX_4X4 && edges[mode].needs_topright) {
                if (have_top && have_right &&
                    n_px_need + n_px_need_tr <= n_px_have) {
                    memcpy(&(*a)[4 * bytesperpixel], &top[4 * bytesperpixel], 4 * bytesperpixel);
                } else {
                    memset_bpp(*a, 4, *a, 3, 4);
                }
            }
        }
    }
    if (edges[mode].needs_left) {
        if (have_left) {
            int n_px_need = 4 << tx, i, n_px_have = (((s->rows - row) << !ss_v) - y) * 4;
            uint8_t *dst = x == 0 ? dst_edge : dst_inner;
            ptrdiff_t stride = x == 0 ? stride_edge : stride_inner;

            if (edges[mode].invert_left) {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                    memset_bpp(l, n_px_have, l, n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                    memset_bpp(l, 0, l, n_px_need - n_px_have, n_px_need - n_px_have);
                }
            }
        } else {
            memset_val(l, (128 << (bpp - 8)) + 1, 4 << tx);
        }
    }

    return mode;
}

static av_always_inline void intra_recon(VP9TileData *td, ptrdiff_t y_off,
                                         ptrdiff_t uv_off, int bytesperpixel)
{
    VP9Context *s = td->s;
    VP9Block *b = td->b;
    int row = td->row, col = td->col;
    int w4 = ff_vp9_bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
    int h4 = ff_vp9_bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
    int uvstep1d = 1 << b->uvtx, p;
    uint8_t *dst = td->dst[0], *dst_r = s->s.frames[CUR_FRAME].tf.f->data[0] + y_off;
    LOCAL_ALIGNED_32(uint8_t, a_buf, [96]);
    LOCAL_ALIGNED_32(uint8_t, l, [64]);

    for (n = 0, y = 0; y < end_y; y += step1d) {
        uint8_t *ptr = dst, *ptr_r = dst_r;
        for (x = 0; x < end_x; x += step1d, ptr += 4 * step1d * bytesperpixel,
                               ptr_r += 4 * step1d * bytesperpixel, n += step) {
            int mode = b->mode[b->bs > BS_8x8 && b->tx == TX_4X4 ?
                               y * 2 + x : 0];
            uint8_t *a = &a_buf[32];
            enum TxfmType txtp = ff_vp9_intra_txfm_type[mode];
            int eob = b->skip ? 0 : b->tx > TX_8X8 ? AV_RN16A(&td->eob[n]) : td->eob[n];

            mode = check_intra_mode(td, mode, &a, ptr_r,
                                    s->s.frames[CUR_FRAME].tf.f->linesize[0],
                                    ptr, td->y_stride, l,
                                    col, x, w4, row, y, b->tx, 0, 0, 0, bytesperpixel);
            s->dsp.intra_pred[b->tx][mode](ptr, td->y_stride, l, a);
            if (eob)
                s->dsp.itxfm_add[tx][txtp](ptr, td->y_stride,
                                           td->block + 16 * n * bytesperpixel, eob);
        }
        dst_r += 4 * step1d * s->s.frames[CUR_FRAME].tf.f->linesize[0];
        dst   += 4 * step1d * td->y_stride;
    }

    // U/V
    w4    >>= s->ss_h;
    end_x >>= s->ss_h;
    end_y >>= s->ss_v;
    step = 1 << (b->uvtx * 2);
    for (p = 0; p < 2; p++) {
        dst   = td->dst[1 + p];
        dst_r = s->s.frames[CUR_FRAME].tf.f->data[1 + p] + uv_off;
        for (n = 0, y = 0; y < end_y; y += uvstep1d) {
            uint8_t *ptr = dst, *ptr_r = dst_r;
            for (x = 0; x < end_x; x += uvstep1d, ptr += 4 * uvstep1d * bytesperpixel,
                                   ptr_r += 4 * uvstep1d * bytesperpixel, n += step) {
                int mode = b->uvmode;
                uint8_t *a = &a_buf[32];
                int eob = b->skip ? 0 : b->uvtx > TX_8X8 ? AV_RN16A(&td->uveob[p][n]) : td->uveob[p][n];

                mode = check_intra_mode(td, mode, &a, ptr_r,
                                        s->s.frames[CUR_FRAME].tf.f->linesize[1],
                                        ptr, td->uv_stride, l, col, x, w4, row, y,
                                        b->uvtx, p + 1, s->ss_h, s->ss_v, bytesperpixel);
                s->dsp.intra_pred[b->uvtx][mode](ptr, td->uv_stride, l, a);
                if (eob)
                    s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, td->uv_stride,
                                                    td->uvblock[p] + 16 * n * bytesperpixel, eob);
            }
            dst_r += 4 * uvstep1d * s->s.frames[CUR_FRAME].tf.f->linesize[1];
            dst   += 4 * uvstep1d * td->uv_stride;
        }
    }
}

void ff_vp9_intra_recon_8bpp(VP9TileData *td, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(td, y_off, uv_off, 1);
}

void ff_vp9_intra_recon_16bpp(VP9TileData *td, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(td, y_off, uv_off, 2);
}

static av_always_inline void mc_luma_unscaled(VP9TileData *td, vp9_mc_func (*mc)[2],
                                              uint8_t *dst, ptrdiff_t dst_stride,
                                              const uint8_t *ref, ptrdiff_t ref_stride,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                              int bw, int bh, int w, int h, int bytesperpixel)
{
    VP9Context *s = td->s;
    int mx = mv->x, my = mv->y, th;

    y += my >> 3;
    x += mx >> 3;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 7;
    my &= 7;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (!!my * 5) than horizontally (!!mx * 4).
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 5 > h - bh) {
        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref - !!my * 3 * ref_stride - !!mx * 3 * bytesperpixel,
                                 160, ref_stride,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref = td->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        ref_stride = 160;
    }
    mc[!!mx][!!my](dst, dst_stride, ref, ref_stride, bh, mx << 1, my << 1);
}

static av_always_inline void mc_chroma_unscaled(VP9TileData *td, vp9_mc_func (*mc)[2],
                                                uint8_t *dst_u, uint8_t *dst_v,
                                                ptrdiff_t dst_stride,
                                                const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                                const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                                ThreadFrame *ref_frame,
                                                ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                                int bw, int bh, int w, int h, int bytesperpixel)
{
    VP9Context *s = td->s;
    int mx = mv->x * (1 << !s->ss_h), my = mv->y * (1 << !s->ss_v), th;

    y += my >> 4;
    x += mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (!!my * 5) than horizontally (!!mx * 4).
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 5 > h - bh) {
        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref_u - !!my * 3 * src_stride_u - !!mx * 3 * bytesperpixel,
                                 160, src_stride_u,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_u = td->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, 160, bh, mx, my);

        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref_v - !!my * 3 * src_stride_v - !!mx * 3 * bytesperpixel,
                                 160, src_stride_v,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_v = td->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, 160, bh, mx, my);
    } else {
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my);
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my);
    }
}

#define mc_luma_dir(td, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_unscaled(td, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                     mv, bw, bh, w, h, bytesperpixel)
#define mc_chroma_dir(td, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_unscaled(td, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                       row, col, mv, bw, bh, w, h, bytesperpixel)
#define SCALED 0
#define FN(x) x##_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void mc_luma_scaled(VP9TileData *td, vp9_scaled_mc_func smc,
                                            vp9_mc_func (*mc)[2],
                                            uint8_t *dst, ptrdiff_t dst_stride,
                                            const uint8_t *ref, ptrdiff_t ref_stride,
                                            ThreadFrame *ref_frame,
                                            ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                            int px, int py, int pw, int ph,
                                            int bw, int bh, int w, int h, int bytesperpixel,
                                            const uint16_t *scale, const uint8_t *step)
{
    VP9Context *s = td->s;
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_luma_unscaled(td, mc, dst, dst_stride, ref, ref_stride, ref_frame,
                         y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
#define scale_mv(n, dim) (((int64_t)(n) * scale[dim]) >> 14)
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 8, (s->cols * 8 - x + px + 3) * 8);
    mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 8, (s->rows * 8 - y + py + 3) * 8);
    // BUG libvpx seems to scale the two components separately. This introduces
    // rounding errors but we have to reproduce them to be exactly compatible
    // with the output from libvpx...
    mx = scale_mv(mv.x * 2, 0) + scale_mv(x * 16, 0);
    my = scale_mv(mv.y * 2, 1) + scale_mv(y * 16, 1);

    y = my >> 4;
    x = mx >> 4;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (y + 5 >= h - refbh_m1) than horizontally (x + 4 >= w - refbw_m1).
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 5 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref - 3 * ref_stride - 3 * bytesperpixel,
                                 288, ref_stride,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref = td->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        ref_stride = 288;
    }
    smc(dst, dst_stride, ref, ref_stride, bh, mx, my, step[0], step[1]);
    }
}

static av_always_inline void mc_chroma_scaled(VP9TileData *td, vp9_scaled_mc_func smc,
                                              vp9_mc_func (*mc)[2],
                                              uint8_t *dst_u, uint8_t *dst_v,
                                              ptrdiff_t dst_stride,
                                              const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                              const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                              int px, int py, int pw, int ph,
                                              int bw, int bh, int w, int h, int bytesperpixel,
                                              const uint16_t *scale, const uint8_t *step)
{
    VP9Context *s = td->s;
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_chroma_unscaled(td, mc, dst_u, dst_v, dst_stride, ref_u, src_stride_u,
                           ref_v, src_stride_v, ref_frame,
                           y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    if (s->ss_h) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 16, (s->cols * 4 - x + px + 3) * 16);
        mx = scale_mv(mv.x, 0) + (scale_mv(x * 16, 0) & ~15) + (scale_mv(x * 32, 0) & 15);
    } else {
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 8, (s->cols * 8 - x + px + 3) * 8);
        mx = scale_mv(mv.x * 2, 0) + scale_mv(x * 16, 0);
    }
    if (s->ss_v) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 16, (s->rows * 4 - y + py + 3) * 16);
        my = scale_mv(mv.y, 1) + (scale_mv(y * 16, 1) & ~15) + (scale_mv(y * 32, 1) & 15);
    } else {
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 8, (s->rows * 8 - y + py + 3) * 8);
        my = scale_mv(mv.y * 2, 1) + scale_mv(y * 16, 1);
    }
#undef scale_mv
    y = my >> 4;
    x = mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (y + 5 >= h - refbh_m1) than horizontally (x + 4 >= w - refbw_m1).
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 5 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref_u - 3 * src_stride_u - 3 * bytesperpixel,
                                 288, src_stride_u,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_u = td->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_u, dst_stride, ref_u, 288, bh, mx, my, step[0], step[1]);

        s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                 ref_v - 3 * src_stride_v - 3 * bytesperpixel,
                                 288, src_stride_v,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_v = td->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_v, dst_stride, ref_v, 288, bh, mx, my, step[0], step[1]);
    } else {
        smc(dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my, step[0], step[1]);
        smc(dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my, step[0], step[1]);
    }
    }
}

#define mc_luma_dir(td, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_scaled(td, s->dsp.s##mc, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                   mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                   s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define mc_chroma_dir(td, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_scaled(td, s->dsp.s##mc, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                     row, col, mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                     s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define SCALED 1
#define FN(x) x##_scaled_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_scaled_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void inter_recon(VP9TileData *td, int bytesperpixel)
{
    VP9Context *s = td->s;
    VP9Block *b = td->b;
    int row = td->row, col = td->col;

    if (s->mvscale[b->ref[0]][0] == REF_INVALID_SCALE ||
        (b->comp && s->mvscale[b->ref[1]][0] == REF_INVALID_SCALE)) {
        if (!s->td->error_info) {
            s->td->error_info = AVERROR_INVALIDDATA;
            av_log(NULL, AV_LOG_ERROR, "Bitstream not supported, "
                                       "reference frame has invalid dimensions\n");
        }
        return;
    }

    if (s->mvscale[b->ref[0]][0] || (b->comp && s->mvscale[b->ref[1]][0])) {
        if (bytesperpixel == 1) {
            inter_pred_scaled_8bpp(td);
        } else {
            inter_pred_scaled_16bpp(td);
        }
    } else {
        if (bytesperpixel == 1) {
            inter_pred_8bpp(td);
        } else {
            inter_pred_16bpp(td);
        }
    }

    if (!b->skip) {
        /* mostly copied intra_recon() */

        int w4 = ff_vp9_bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
        int h4 = ff_vp9_bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
        int end_x = FFMIN(2 * (s->cols - col), w4);
        int end_y = FFMIN(2 * (s->rows - row), h4);
        int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
        int uvstep1d = 1 << b->uvtx, p;
        uint8_t *dst = td->dst[0];

        // y itxfm add
        for (n = 0, y = 0; y < end_y; y += step1d) {
            uint8_t *ptr = dst;
            for (x = 0; x < end_x; x += step1d,
                 ptr += 4 * step1d * bytesperpixel, n += step) {
                int eob = b->tx > TX_8X8 ? AV_RN16A(&td->eob[n]) : td->eob[n];

                if (eob)
                    s->dsp.itxfm_add[tx][DCT_DCT](ptr, td->y_stride,
                                                  td->block + 16 * n * bytesperpixel, eob);
            }
            dst += 4 * td->y_stride * step1d;
        }

        // uv itxfm add
        end_x >>= s->ss_h;
        end_y >>= s->ss_v;
        step = 1 << (b->uvtx * 2);
        for (p = 0; p < 2; p++) {
            dst = td->dst[p + 1];
            for (n = 0, y = 0; y < end_y; y += uvstep1d) {
                uint8_t *ptr = dst;
                for (x = 0; x < end_x; x += uvstep1d,
                     ptr += 4 * uvstep1d * bytesperpixel, n += step) {
                    int eob = b->uvtx > TX_8X8 ? AV_RN16A(&td->uveob[p][n]) : td->uveob[p][n];

                    if (eob)
                        s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, td->uv_stride,
                                                        td->uvblock[p] + 16 * n * bytesperpixel, eob);
                }
                dst += 4 * uvstep1d * td->uv_stride;
            }
        }
    }
}

void ff_vp9_inter_recon_8bpp(VP9TileData *td)
{
    inter_recon(td, 1);
}

void ff_vp9_inter_recon_16bpp(VP9TileData *td)
{
    inter_recon(td, 2);
}
