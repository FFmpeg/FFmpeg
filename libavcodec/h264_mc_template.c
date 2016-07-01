/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#include "h264.h"

#undef MCFUNC

#if   CHROMA_IDC == 1
#   define MCFUNC(n) FUNC(n ## _420)
#elif CHROMA_IDC == 2
#   define MCFUNC(n) FUNC(n ## _422)
#elif CHROMA_IDC == 3
#   define MCFUNC(n) FUNC(n ## _444)
#endif

#undef  mc_part
#define mc_part MCFUNC(mc_part)

static void mc_part(const H264Context *h, H264SliceContext *sl,
                    int n, int square,
                    int height, int delta,
                    uint8_t *dest_y, uint8_t *dest_cb,
                    uint8_t *dest_cr,
                    int x_offset, int y_offset,
                    const qpel_mc_func *qpix_put,
                    h264_chroma_mc_func chroma_put,
                    const qpel_mc_func *qpix_avg,
                    h264_chroma_mc_func chroma_avg,
                    const h264_weight_func *weight_op,
                    const h264_biweight_func *weight_avg,
                    int list0, int list1)
{
    if ((sl->pwt.use_weight == 2 && list0 && list1 &&
         (sl->pwt.implicit_weight[sl->ref_cache[0][scan8[n]]][sl->ref_cache[1][scan8[n]]][sl->mb_y & 1] != 32)) ||
        sl->pwt.use_weight == 1)
        mc_part_weighted(h, sl, n, square, height, delta, dest_y, dest_cb, dest_cr,
                         x_offset, y_offset, qpix_put, chroma_put,
                         weight_op[0], weight_op[1], weight_avg[0],
                         weight_avg[1], list0, list1, PIXEL_SHIFT, CHROMA_IDC);
    else
        mc_part_std(h, sl, n, square, height, delta, dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put, qpix_avg,
                    chroma_avg, list0, list1, PIXEL_SHIFT, CHROMA_IDC);
}

static void MCFUNC(hl_motion)(const H264Context *h, H264SliceContext *sl,
                              uint8_t *dest_y,
                              uint8_t *dest_cb, uint8_t *dest_cr,
                              const qpel_mc_func(*qpix_put)[16],
                              const h264_chroma_mc_func(*chroma_put),
                              const qpel_mc_func(*qpix_avg)[16],
                              const h264_chroma_mc_func(*chroma_avg),
                              const h264_weight_func *weight_op,
                              const h264_biweight_func *weight_avg)
{
    const int mb_xy   = sl->mb_xy;
    const int mb_type = h->cur_pic.mb_type[mb_xy];

    av_assert2(IS_INTER(mb_type));

    if (HAVE_THREADS && (h->avctx->active_thread_type & FF_THREAD_FRAME))
        await_references(h, sl);
    prefetch_motion(h, sl, 0, PIXEL_SHIFT, CHROMA_IDC);

    if (IS_16X16(mb_type)) {
        mc_part(h, sl, 0, 1, 16, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    } else if (IS_16X8(mb_type)) {
        mc_part(h, sl, 0, 0, 8, 8 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, sl, 8, 0, 8, 8 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                weight_op, weight_avg,
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    } else if (IS_8X16(mb_type)) {
        mc_part(h, sl, 0, 0, 16, 8 * sl->mb_linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, sl, 4, 0, 16, 8 * sl->mb_linesize, dest_y, dest_cb, dest_cr, 4, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                &weight_op[1], &weight_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    } else {
        int i;

        av_assert2(IS_8X8(mb_type));

        for (i = 0; i < 4; i++) {
            const int sub_mb_type = sl->sub_mb_type[i];
            const int n  = 4 * i;
            int x_offset = (i & 1) << 2;
            int y_offset = (i & 2) << 1;

            if (IS_SUB_8X8(sub_mb_type)) {
                mc_part(h, sl, n, 1, 8, 0, dest_y, dest_cb, dest_cr,
                        x_offset, y_offset,
                        qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else if (IS_SUB_8X4(sub_mb_type)) {
                mc_part(h, sl, n, 0, 4, 4 << PIXEL_SHIFT, dest_y, dest_cb, dest_cr,
                        x_offset, y_offset,
                        qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, sl, n + 2, 0, 4, 4 << PIXEL_SHIFT,
                        dest_y, dest_cb, dest_cr, x_offset, y_offset + 2,
                        qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                        &weight_op[1], &weight_avg[1],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else if (IS_SUB_4X8(sub_mb_type)) {
                mc_part(h, sl, n, 0, 8, 4 * sl->mb_linesize,
                        dest_y, dest_cb, dest_cr, x_offset, y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[2], &weight_avg[2],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, sl, n + 1, 0, 8, 4 * sl->mb_linesize,
                        dest_y, dest_cb, dest_cr, x_offset + 2, y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        &weight_op[2], &weight_avg[2],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            } else {
                int j;
                av_assert2(IS_SUB_4X4(sub_mb_type));
                for (j = 0; j < 4; j++) {
                    int sub_x_offset = x_offset + 2 * (j & 1);
                    int sub_y_offset = y_offset + (j & 2);
                    mc_part(h, sl, n + j, 1, 4, 0,
                            dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                            qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                            &weight_op[2], &weight_avg[2],
                            IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }

    if (USES_LIST(mb_type, 1))
        prefetch_motion(h, sl, 1, PIXEL_SHIFT, CHROMA_IDC);
}

