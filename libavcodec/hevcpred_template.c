/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#include "libavutil/pixdesc.h"
#include "bit_depth_template.c"
#include "hevcpred.h"

#define POS(x, y) src[(x) + stride * (y)]

static void FUNC(intra_pred)(HEVCContext *s, int x0, int y0, int log2_size, int c_idx)
{
#define PU(x) \
    ((x) >> s->sps->log2_min_pu_size)
#define MVF(x, y) \
    (s->ref->tab_mvf[(x) + (y) * pic_width_in_min_pu])
#define MVF_PU(x, y) \
    MVF(PU(x0 + ((x) << hshift)), PU(y0 + ((y) << vshift)))
#define IS_INTRA(x, y) \
    MVF_PU(x, y).is_intra
#define MIN_TB_ADDR_ZS(x, y) \
    s->pps->min_tb_addr_zs[(y) * s->sps->min_tb_width + (x)]
#define EXTEND_LEFT(ptr, start, length) \
        for (i = (start); i > (start) - (length); i--) \
            ptr[i - 1] = ptr[i]
#define EXTEND_RIGHT(ptr, start, length) \
        for (i = (start); i < (start) + (length); i++) \
            ptr[i] = ptr[i - 1]
#define EXTEND_UP(ptr, start, length)   EXTEND_LEFT(ptr, start, length)
#define EXTEND_DOWN(ptr, start, length) EXTEND_RIGHT(ptr, start, length)
#define EXTEND_LEFT_CIP(ptr, start, length) \
        for (i = (start); i > (start) - (length); i--) \
            if (!IS_INTRA(i - 1, -1)) \
                ptr[i - 1] = ptr[i]
#define EXTEND_RIGHT_CIP(ptr, start, length) \
        for (i = (start); i < (start) + (length); i++) \
            if (!IS_INTRA(i, -1)) \
                ptr[i] = ptr[i - 1]
#define EXTEND_UP_CIP(ptr, start, length) \
        for (i = (start); i > (start) - (length); i--) \
            if (!IS_INTRA(-1, i - 1)) \
                ptr[i - 1] = ptr[i]
#define EXTEND_UP_CIP_0(ptr, start, length) \
        for (i = (start); i > (start) - (length); i--) \
            ptr[i - 1] = ptr[i]
#define EXTEND_DOWN_CIP(ptr, start, length) \
        for (i = (start); i < (start) + (length); i++) \
            if (!IS_INTRA(-1, i)) \
                ptr[i] = ptr[i - 1]
    HEVCLocalContext *lc = &s->HEVClc;
    int i;
    int hshift = s->sps->hshift[c_idx];
    int vshift = s->sps->vshift[c_idx];
    int size = (1 << log2_size);
    int size_in_luma = size << hshift;
    int size_in_tbs = size_in_luma >> s->sps->log2_min_transform_block_size;
    int x = x0 >> hshift;
    int y = y0 >> vshift;
    int x_tb = x0 >> s->sps->log2_min_transform_block_size;
    int y_tb = y0 >> s->sps->log2_min_transform_block_size;
    int cur_tb_addr = MIN_TB_ADDR_ZS(x_tb, y_tb);

    ptrdiff_t stride = s->frame->linesize[c_idx] / sizeof(pixel);
    pixel *src = (pixel*)s->frame->data[c_idx] + x + y * stride;

    int pic_width_in_min_pu = PU(s->sps->width);

    enum IntraPredMode mode = c_idx ? lc->pu.intra_pred_mode_c :
                              lc->tu.cur_intra_pred_mode;

    pixel left_array[2 * MAX_TB_SIZE + 1];
    pixel filtered_left_array[2 * MAX_TB_SIZE + 1];
    pixel top_array[2 * MAX_TB_SIZE + 1];
    pixel filtered_top_array[2 * MAX_TB_SIZE + 1];

    pixel *left          = left_array + 1;
    pixel *top           = top_array + 1;
    pixel *filtered_left = filtered_left_array + 1;
    pixel *filtered_top  = filtered_top_array + 1;

    int cand_bottom_left = lc->na.cand_bottom_left && cur_tb_addr > MIN_TB_ADDR_ZS(x_tb - 1, y_tb + size_in_tbs);
    int cand_left        = lc->na.cand_left;
    int cand_up_left     = lc->na.cand_up_left;
    int cand_up          = lc->na.cand_up;
    int cand_up_right    = lc->na.cand_up_right && cur_tb_addr > MIN_TB_ADDR_ZS(x_tb + size_in_tbs, y_tb - 1);

    int bottom_left_size = (FFMIN(y0 + 2 * size_in_luma, s->sps->height) -
                            (y0 + size_in_luma)) >> vshift;
    int top_right_size   = (FFMIN(x0 + 2 * size_in_luma, s->sps->width) -
                            (x0 + size_in_luma)) >> hshift;

    if (s->pps->constrained_intra_pred_flag == 1) {
        int size_in_luma_pu = PU(size_in_luma);
        int on_pu_edge_x = !(x0 & ((1 << s->sps->log2_min_pu_size) - 1));
        int on_pu_edge_y = !(y0 & ((1 << s->sps->log2_min_pu_size) - 1));
        if(!size_in_luma_pu)
            size_in_luma_pu++;
        if (cand_bottom_left == 1 && on_pu_edge_x) {
            int x_left_pu   = PU(x0 - 1);
            int y_bottom_pu = PU(y0 + size_in_luma);
            cand_bottom_left = 0;
            for(i = 0; i < size_in_luma_pu; i++)
                cand_bottom_left |= MVF(x_left_pu, y_bottom_pu + i).is_intra;
        }
        if (cand_left == 1 && on_pu_edge_x) {
            int x_left_pu   = PU(x0 - 1);
            int y_left_pu   = PU(y0);
            cand_left = 0;
            for(i = 0; i < size_in_luma_pu; i++)
                cand_left |= MVF(x_left_pu, y_left_pu + i).is_intra;
        }
        if (cand_up_left == 1) {
            int x_left_pu   = PU(x0 - 1);
            int y_top_pu    = PU(y0 - 1);
            cand_up_left = MVF(x_left_pu, y_top_pu).is_intra;
        }
        if (cand_up == 1 && on_pu_edge_y) {
            int x_top_pu    = PU(x0);
            int y_top_pu    = PU(y0 - 1);
            cand_up = 0;
            for(i = 0; i < size_in_luma_pu; i++)
                cand_up |= MVF(x_top_pu + i, y_top_pu).is_intra;
        }
        if (cand_up_right == 1 && on_pu_edge_y) {
            int y_top_pu    = PU(y0 - 1);
            int x_right_pu  = PU(x0 + size_in_luma);
            cand_up_right = 0;
            for(i = 0; i < size_in_luma_pu; i++)
                cand_up_right |= MVF(x_right_pu + i, y_top_pu).is_intra;
        }
        for (i = 0; i < 2 * MAX_TB_SIZE; i++) {
            left[i] = 128;
            top[i]  = 128;
        }
    }
    if (cand_bottom_left) {
        for (i = size + bottom_left_size; i < (size << 1); i++)
            if (IS_INTRA(-1, size + bottom_left_size - 1) || !s->pps->constrained_intra_pred_flag)
                left[i] = POS(-1, size + bottom_left_size - 1);
        for (i = size + bottom_left_size - 1; i >= size; i--)
            if (IS_INTRA(-1, i) || !s->pps->constrained_intra_pred_flag)
                left[i] = POS(-1, i);
    }
    if (cand_left)
        for (i = size - 1; i >= 0; i--)
            if (IS_INTRA(-1, i) || !s->pps->constrained_intra_pred_flag)
                left[i] = POS(-1, i);
    if (cand_up_left)
        if (IS_INTRA(-1, -1) || !s->pps->constrained_intra_pred_flag) {
            left[-1] = POS(-1, -1);
            top[-1]  = left[-1];
        }
    if (cand_up)
        for (i = size - 1; i >= 0; i--)
            if (IS_INTRA(i, -1) || !s->pps->constrained_intra_pred_flag)
                top[i] = POS(i, -1);
    if (cand_up_right) {
        for (i = size + top_right_size; i < (size << 1); i++)
            if (IS_INTRA(size + top_right_size - 1, -1) || !s->pps->constrained_intra_pred_flag)
                top[i] = POS(size + top_right_size - 1, -1);
        for (i = size + top_right_size - 1; i >= size; i--)
            if (IS_INTRA(i, -1) || !s->pps->constrained_intra_pred_flag)
                top[i] = POS(i, -1);
    }

    if (s->pps->constrained_intra_pred_flag == 1) {
        if (cand_bottom_left || cand_left || cand_up_left || cand_up || cand_up_right) {
            int size_max_x = x0 + ((2 * size) << hshift) < s->sps->width ?
                                    2 * size : (s->sps->width - x0) >> hshift;
            int size_max_y = y0 + ((2 * size) << vshift) < s->sps->height ?
                                    2 * size : (s->sps->height - y0) >> vshift;
            int j = size + (cand_bottom_left? bottom_left_size: 0) -1;
            if (!cand_up_right) {
                size_max_x = x0 + ((size) << hshift) < s->sps->width ?
                                                    size : (s->sps->width - x0) >> hshift;
            }
            if (!cand_bottom_left) {
                size_max_y = y0 + (( size) << vshift) < s->sps->height ?
                                                     size : (s->sps->height - y0) >> vshift;
            }
            if (cand_bottom_left || cand_left || cand_up_left) {
                while (j>-1 && !IS_INTRA(-1, j)) j--;
                if (!IS_INTRA(-1, j)) {
                    j = 0;
                    while(j < size_max_x && !IS_INTRA(j, -1)) j++;
                    EXTEND_LEFT_CIP(top, j, j+1);
                    left[-1] = top[-1];
                    j = 0;
                }
            } else {
                j = 0;
                while (j < size_max_x && !IS_INTRA(j, -1)) j++;
                if (j > 0)
                    if (x0 > 0) {
                        EXTEND_LEFT_CIP(top, j, j+1);
                    } else {
                        EXTEND_LEFT_CIP(top, j, j);
                        top[-1] = top[0];
                    }
                left[-1] = top[-1];
                j = 0;
            }
            if (cand_bottom_left || cand_left) {
                EXTEND_DOWN_CIP(left, j, size_max_y-j);
            }
            if (!cand_left) {
                EXTEND_DOWN(left, 0, size);
            }
            if (!cand_bottom_left) {
                EXTEND_DOWN(left, size, size);
            }
            if (x0 != 0 && y0 != 0) {
                EXTEND_UP_CIP(left, size_max_y - 1, size_max_y);
            } else if( x0 == 0) {
                EXTEND_UP_CIP_0(left, size_max_y - 1, size_max_y);
            } else{
                EXTEND_UP_CIP(left, size_max_y - 1, size_max_y-1);
            }
            top[-1] = left[-1];
            if (y0 != 0) {
                EXTEND_RIGHT_CIP(top, 0, size_max_x);
            }
        }
    }
    // Infer the unavailable samples
    if (!cand_bottom_left) {
        if (cand_left) {
            EXTEND_DOWN(left, size, size);
        } else if (cand_up_left) {
            EXTEND_DOWN(left, 0, 2 * size);
            cand_left = 1;
        } else if (cand_up) {
            left[-1] = top[0];
            EXTEND_DOWN(left, 0, 2 * size);
            cand_up_left = 1;
            cand_left = 1;
        } else if (cand_up_right) {
            EXTEND_LEFT(top, size, size);
            left[-1] = top[0];
            EXTEND_DOWN(left ,0 , 2 * size);
            cand_up = 1;
            cand_up_left = 1;
            cand_left = 1;
        } else { // No samples available
            top[0] = left[-1] = (1 << (BIT_DEPTH - 1));
            EXTEND_RIGHT(top, 1, 2 * size - 1);
            EXTEND_DOWN(left, 0, 2 * size);
        }
    }

    if (!cand_left) {
        EXTEND_UP(left, size, size);
    }
    if (!cand_up_left) {
        left[-1] = left[0];
    }
    if (!cand_up) {
        top[0] = left[-1];
        EXTEND_RIGHT(top, 1, size-1);
    }
    if (!cand_up_right) {
        EXTEND_RIGHT(top, size, size);
    }

    top[-1] = left[-1];

#undef EXTEND_LEFT_CIP
#undef EXTEND_RIGHT_CIP
#undef EXTEND_UP_CIP
#undef EXTEND_DOWN_CIP
#undef IS_INTRA
#undef MVF_PU
#undef MVF
#undef PU
#undef EXTEND_LEFT
#undef EXTEND_RIGHT
#undef EXTEND_UP
#undef EXTEND_DOWN
#undef MIN_TB_ADDR_ZS

    // Filtering process
    if (c_idx == 0 && mode != INTRA_DC && size != 4) {
        int intra_hor_ver_dist_thresh[] = { 7, 1, 0 };
        int min_dist_vert_hor           = FFMIN(FFABS((int)mode - 26),
                                                FFABS((int)mode - 10));
        if (min_dist_vert_hor > intra_hor_ver_dist_thresh[log2_size - 3]) {
            int threshold = 1 << (BIT_DEPTH - 5);
            if (s->sps->sps_strong_intra_smoothing_enable_flag &&
                log2_size == 5 &&
                FFABS(top[-1] + top[63] - 2 * top[31]) < threshold &&
                FFABS(left[-1] + left[63] - 2 * left[31]) < threshold) {
                // We can't just overwrite values in top because it could be
                // a pointer into src
                filtered_top[-1] = top[-1];
                filtered_top[63] = top[63];
                for (i = 0; i < 63; i++)
                    filtered_top[i] = ((64 - (i + 1)) * top[-1] +
                                             (i + 1)  * top[63] + 32) >> 6;
                for (i = 0; i < 63; i++)
                    left[i] = ((64 - (i + 1)) * left[-1] +
                                     (i + 1)  * left[63] + 32) >> 6;
                top = filtered_top;
            } else {
                filtered_left[2 * size - 1] = left[2 * size - 1];
                filtered_top[2 * size - 1]  = top[2 * size - 1];
                for (i = 2 * size - 2; i >= 0; i--)
                    filtered_left[i] = (left[i + 1] + 2 * left[i] +
                                        left[i - 1] + 2) >> 2;
                filtered_top[-1] =
                filtered_left[-1] = (left[0] + 2 * left[-1] +
                                     top[0]  + 2) >> 2;
                for (i = 2 * size - 2; i >= 0; i--)
                    filtered_top[i] = (top[i + 1] + 2 * top[i] +
                                       top[i - 1] + 2) >> 2;
                left = filtered_left;
                top  = filtered_top;
            }
        }
    }

    switch (mode) {
    case INTRA_PLANAR:
        s->hpc.pred_planar[log2_size - 2]((uint8_t*)src, (uint8_t*)top,
                                          (uint8_t*)left, stride);
        break;
    case INTRA_DC:
        s->hpc.pred_dc((uint8_t*)src, (uint8_t*)top,
                       (uint8_t*)left, stride, log2_size, c_idx);
        break;
    default:
        s->hpc.pred_angular[log2_size - 2]((uint8_t*)src, (uint8_t*)top,
                                           (uint8_t*)left, stride, c_idx, mode);
        break;
    }
}

static void FUNC(pred_planar_0)(uint8_t *_src, const uint8_t *_top,
                                const uint8_t *_left,
                                ptrdiff_t stride)
{
    int x, y;
    pixel *src = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            POS(x, y) = ((3 - x) * left[y]  + (x + 1) * top[4] +
                         (3 - y) * top[x]   + (y + 1) * left[4] + 4) >> 3;
}

static void FUNC(pred_planar_1)(uint8_t *_src, const uint8_t *_top,
                                const uint8_t *_left, ptrdiff_t stride)
{
    int x, y;
    pixel *src = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            POS(x, y) = ((7 - x) * left[y]  + (x + 1) * top[8] +
                         (7 - y) * top[x]   + (y + 1) * left[8] + 8) >> 4;
}

static void FUNC(pred_planar_2)(uint8_t *_src, const uint8_t *_top,
                                const uint8_t *_left, ptrdiff_t stride)
{
    int x, y;
    pixel *src = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            POS(x, y) = ((15 - x) * left[y]  + (x + 1) * top[16] +
                         (15 - y) * top[x]   + (y + 1) * left[16] + 16) >> 5;
}

static void FUNC(pred_planar_3)(uint8_t *_src, const uint8_t *_top,
                                const uint8_t *_left, ptrdiff_t stride)
{
    int x, y;
    pixel *src = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            POS(x, y) = ((31 - x) * left[y]  + (x + 1) * top[32] +
                         (31 - y) * top[x]   + (y + 1) * left[32] + 32) >> 6;
}

static void FUNC(pred_dc)(uint8_t *_src, const uint8_t *_top,
                          const uint8_t *_left,
                          ptrdiff_t stride, int log2_size, int c_idx)
{
    int i, j, x, y;
    int size = (1 << log2_size);
    pixel *src        = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;
    int dc = size;
    pixel4 a;
    for (i = 0; i < size; i++)
        dc += left[i] + top[i];

    dc >>= log2_size + 1;

    a = PIXEL_SPLAT_X4(dc);

    for (i = 0; i < size; i++)
        for (j = 0; j < size / 4; j++)
            AV_WN4PA(&POS(j * 4, i), a);

    if (c_idx == 0 && size < 32) {
        POS(0, 0) = (left[0] + 2 * dc  + top[0] + 2) >> 2;
        for (x = 1; x < size; x++)
            POS(x, 0) = (top[x] + 3 * dc + 2) >> 2;
        for (y = 1; y < size; y++)
            POS(0, y) = (left[y] + 3 * dc + 2) >> 2;
    }
}

static av_always_inline void FUNC(pred_angular)(uint8_t *_src,
                                                const uint8_t *_top,
                                                const uint8_t *_left,
                                                ptrdiff_t stride, int c_idx,
                                                int mode, int size)
{
    int x, y;
    pixel *src = (pixel*)_src;
    const pixel *top  = (const pixel*)_top;
    const pixel *left = (const pixel*)_left;

    static const int intra_pred_angle[] = {
        32, 26, 21, 17, 13, 9, 5, 2, 0, -2, -5, -9, -13, -17, -21, -26, -32,
        -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32
    };
    static const int inv_angle[] = {
        -4096, -1638, -910, -630, -482, -390, -315, -256, -315, -390, -482,
        -630, -910, -1638, -4096
    };

    int angle = intra_pred_angle[mode - 2];
    pixel ref_array[3 * MAX_TB_SIZE + 1];
    pixel *ref_tmp = ref_array + size;
    const pixel *ref;
    int last = (size * angle) >> 5;

    if (mode >= 18) {
        ref = top - 1;
        if (angle < 0 && last < -1) {
            for (x = 0; x <= size; x++)
                ref_tmp[x] = top[x - 1];
            for (x = last; x <= -1; x++)
                ref_tmp[x] = left[-1 + ((x * inv_angle[mode-11] + 128) >> 8)];
            ref = ref_tmp;
        }

        for (y = 0; y < size; y++) {
            int idx  = ((y + 1) * angle) >> 5;
            int fact = ((y + 1) * angle) & 31;
            if (fact) {
                for (x = 0; x < size; x++) {
                    POS(x, y) = ((32 - fact) * ref[x + idx + 1] +
                                       fact  * ref[x + idx + 2] + 16) >> 5;
                }
            } else {
                for (x = 0; x < size; x++) {
                    POS(x, y) = ref[x + idx + 1];
                }
            }
        }
        if (mode == 26 && c_idx == 0 && size < 32) {
            for (y = 0; y < size; y++)
                POS(0, y) = av_clip_pixel(top[0] + ((left[y] - left[-1]) >> 1));
        }
    } else {
        ref = left - 1;
        if (angle < 0 && last < -1) {
            for (x = 0; x <= size; x++)
                ref_tmp[x] = left[x - 1];
            for (x = last; x <= -1; x++)
                ref_tmp[x] = top[-1 + ((x * inv_angle[mode-11] + 128) >> 8)];
            ref = ref_tmp;
        }

        for (x = 0; x < size; x++) {
            int idx  = ((x + 1) * angle) >> 5;
            int fact = ((x + 1) * angle) & 31;
            if (fact) {
                for (y = 0; y < size; y++) {
                    POS(x, y) = ((32 - fact) * ref[y + idx + 1] +
                                       fact  * ref[y + idx + 2] + 16) >> 5;
                }
            } else {
                for (y = 0; y < size; y++) {
                    POS(x, y) = ref[y + idx + 1];
                }
            }
        }
        if (mode == 10 && c_idx == 0 && size < 32) {
            for (x = 0; x < size; x++)
                POS(x, 0) = av_clip_pixel(left[0] + ((top[x] - top[-1]) >> 1));
        }
    }
}

static void FUNC(pred_angular_0)(uint8_t *src, const uint8_t *top,
                                 const uint8_t *left,
                                 ptrdiff_t stride, int c_idx, int mode)
{
    FUNC(pred_angular)(src, top, left, stride, c_idx, mode, 1 << 2);
}

static void FUNC(pred_angular_1)(uint8_t *src, const uint8_t *top,
                                 const uint8_t *left,
                                 ptrdiff_t stride, int c_idx, int mode)
{
    FUNC(pred_angular)(src, top, left, stride, c_idx, mode, 1 << 3);
}

static void FUNC(pred_angular_2)(uint8_t *src, const uint8_t *top,
                                 const uint8_t *left,
                                 ptrdiff_t stride, int c_idx, int mode)
{
    FUNC(pred_angular)(src, top, left, stride, c_idx, mode, 1 << 4);
}

static void FUNC(pred_angular_3)(uint8_t *src, const uint8_t *top,
                                 const uint8_t *left,
                                 ptrdiff_t stride, int c_idx, int mode)
{
    FUNC(pred_angular)(src, top, left, stride, c_idx, mode, 1 << 5);
}
#undef POS
