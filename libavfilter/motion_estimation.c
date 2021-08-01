/**
 * Copyright (c) 2016 Davinder Singh (DSM_) <ds.mudhar<@gmail.com>
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

#include "libavutil/common.h"
#include "motion_estimation.h"

static const int8_t sqr1[8][2]  = {{ 0,-1}, { 0, 1}, {-1, 0}, { 1, 0}, {-1,-1}, {-1, 1}, { 1,-1}, { 1, 1}};
static const int8_t dia1[4][2]  = {{-1, 0}, { 0,-1}, { 1, 0}, { 0, 1}};
static const int8_t dia2[8][2]  = {{-2, 0}, {-1,-1}, { 0,-2}, { 1,-1}, { 2, 0}, { 1, 1}, { 0, 2}, {-1, 1}};
static const int8_t hex2[6][2]  = {{-2, 0}, {-1,-2}, {-1, 2}, { 1,-2}, { 1, 2}, { 2, 0}};
static const int8_t hex4[16][2] = {{-4,-2}, {-4,-1}, {-4, 0}, {-4, 1}, {-4, 2},
                                   { 4,-2}, { 4,-1}, { 4, 0}, { 4, 1}, { 4, 2},
                                   {-2, 3}, { 0, 4}, { 2, 3}, {-2,-3}, { 0,-4}, { 2,-3}};

#define COST_MV(x, y)\
do {\
    cost = me_ctx->get_cost(me_ctx, x_mb, y_mb, x, y);\
    if (cost < cost_min) {\
        cost_min = cost;\
        mv[0] = x;\
        mv[1] = y;\
    }\
} while(0)

#define COST_P_MV(x, y)\
if (x >= x_min && x <= x_max && y >= y_min && y <= y_max)\
    COST_MV(x, y);

void ff_me_init_context(AVMotionEstContext *me_ctx, int mb_size, int search_param,
                        int width, int height, int x_min, int x_max, int y_min, int y_max)
{
    me_ctx->width = width;
    me_ctx->height = height;
    me_ctx->mb_size = mb_size;
    me_ctx->search_param = search_param;
    me_ctx->get_cost = &ff_me_cmp_sad;
    me_ctx->x_min = x_min;
    me_ctx->x_max = x_max;
    me_ctx->y_min = y_min;
    me_ctx->y_max = y_max;
}

uint64_t ff_me_cmp_sad(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int x_mv, int y_mv)
{
    const int linesize = me_ctx->linesize;
    uint8_t *data_ref = me_ctx->data_ref;
    uint8_t *data_cur = me_ctx->data_cur;
    uint64_t sad = 0;
    int i, j;

    data_ref += y_mv * linesize;
    data_cur += y_mb * linesize;

    for (j = 0; j < me_ctx->mb_size; j++)
        for (i = 0; i < me_ctx->mb_size; i++)
            sad += FFABS(data_ref[x_mv + i + j * linesize] - data_cur[x_mb + i + j * linesize]);

    return sad;
}

uint64_t ff_me_search_esa(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    for (y = y_min; y <= y_max; y++)
        for (x = x_min; x <= x_max; x++)
            COST_MV(x, y);

    return cost_min;
}

uint64_t ff_me_search_tss(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int step = ROUNDED_DIV(me_ctx->search_param, 2);
    int i;

    mv[0] = x_mb;
    mv[1] = y_mb;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 8; i++)
            COST_P_MV(x + sqr1[i][0] * step, y + sqr1[i][1] * step);

        step = step >> 1;

    } while (step > 0);

    return cost_min;
}

uint64_t ff_me_search_tdls(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int step = ROUNDED_DIV(me_ctx->search_param, 2);
    int i;

    mv[0] = x_mb;
    mv[1] = y_mb;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 4; i++)
            COST_P_MV(x + dia1[i][0] * step, y + dia1[i][1] * step);

        if (x == mv[0] && y == mv[1])
            step = step >> 1;

    } while (step > 0);

    return cost_min;
}

uint64_t ff_me_search_ntss(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int step = ROUNDED_DIV(me_ctx->search_param, 2);
    int first_step = 1;
    int i;

    mv[0] = x_mb;
    mv[1] = y_mb;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 8; i++)
            COST_P_MV(x + sqr1[i][0] * step, y + sqr1[i][1] * step);

        /* addition to TSS in NTSS */
        if (first_step) {

            for (i = 0; i < 8; i++)
                COST_P_MV(x + sqr1[i][0], y + sqr1[i][1]);

            if (x == mv[0] && y == mv[1])
                return cost_min;

            if (FFABS(x - mv[0]) <= 1 && FFABS(y - mv[1]) <= 1) {
                x = mv[0];
                y = mv[1];

                for (i = 0; i < 8; i++)
                    COST_P_MV(x + sqr1[i][0], y + sqr1[i][1]);
                return cost_min;
            }

            first_step = 0;
        }

        step = step >> 1;

    } while (step > 0);

    return cost_min;
}

uint64_t ff_me_search_fss(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int step = 2;
    int i;

    mv[0] = x_mb;
    mv[1] = y_mb;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 8; i++)
            COST_P_MV(x + sqr1[i][0] * step, y + sqr1[i][1] * step);

        if (x == mv[0] && y == mv[1])
            step = step >> 1;

    } while (step > 0);

    return cost_min;
}

uint64_t ff_me_search_ds(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int i;
    av_unused int dir_x, dir_y;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    x = x_mb; y = y_mb;
    dir_x = dir_y = 0;

    do {
        x = mv[0];
        y = mv[1];

#if 1
        for (i = 0; i < 8; i++)
            COST_P_MV(x + dia2[i][0], y + dia2[i][1]);
#else
        /* this version skips previously examined 3 or 5 locations based on prev origin */
        if (dir_x <= 0)
            COST_P_MV(x - 2, y);
        if (dir_x <= 0 && dir_y <= 0)
            COST_P_MV(x - 1, y - 1);
        if (dir_y <= 0)
            COST_P_MV(x, y - 2);
        if (dir_x >= 0 && dir_y <= 0)
            COST_P_MV(x + 1, y - 1);
        if (dir_x >= 0)
            COST_P_MV(x + 2, y);
        if (dir_x >= 0 && dir_y >= 0)
            COST_P_MV(x + 1, y + 1);
        if (dir_y >= 0)
            COST_P_MV(x, y + 2);
        if (dir_x <= 0 && dir_y >= 0)
            COST_P_MV(x - 1, y + 1);

        dir_x = mv[0] - x;
        dir_y = mv[1] - y;
#endif

    } while (x != mv[0] || y != mv[1]);

    for (i = 0; i < 4; i++)
        COST_P_MV(x + dia1[i][0], y + dia1[i][1]);

    return cost_min;
}

uint64_t ff_me_search_hexbs(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int i;

    if (!(cost_min = me_ctx->get_cost(me_ctx, x_mb, y_mb, x_mb, y_mb)))
        return cost_min;

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 6; i++)
            COST_P_MV(x + hex2[i][0], y + hex2[i][1]);

    } while (x != mv[0] || y != mv[1]);

    for (i = 0; i < 4; i++)
        COST_P_MV(x + dia1[i][0], y + dia1[i][1]);

    return cost_min;
}

/* two subsets of predictors are used
   me->pred_x|y is set to median of current frame's left, top, top-right
   set 1: me->preds[0] has: (0, 0), left, top, top-right, collocated block in prev frame
   set 2: me->preds[1] has: accelerator mv, top, left, right, bottom adj mb of prev frame
*/
uint64_t ff_me_search_epzs(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int i;

    AVMotionEstPredictor *preds = me_ctx->preds;

    cost_min = UINT64_MAX;

    COST_P_MV(x_mb + me_ctx->pred_x, y_mb + me_ctx->pred_y);

    for (i = 0; i < preds[0].nb; i++)
        COST_P_MV(x_mb + preds[0].mvs[i][0], y_mb + preds[0].mvs[i][1]);

    for (i = 0; i < preds[1].nb; i++)
        COST_P_MV(x_mb + preds[1].mvs[i][0], y_mb + preds[1].mvs[i][1]);

    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 4; i++)
            COST_P_MV(x + dia1[i][0], y + dia1[i][1]);

    } while (x != mv[0] || y != mv[1]);

    return cost_min;
}

/* required predictor order: median, (0,0), left, top, top-right
   rules when mb not available:
   replace left with (0, 0)
   replace top-right with top-left
   replace top two with left
   repeated can be skipped, if no predictors are used, set me_ctx->pred to (0,0)
*/
uint64_t ff_me_search_umh(AVMotionEstContext *me_ctx, int x_mb, int y_mb, int *mv)
{
    int x, y;
    int x_min = FFMAX(me_ctx->x_min, x_mb - me_ctx->search_param);
    int y_min = FFMAX(me_ctx->y_min, y_mb - me_ctx->search_param);
    int x_max = FFMIN(x_mb + me_ctx->search_param, me_ctx->x_max);
    int y_max = FFMIN(y_mb + me_ctx->search_param, me_ctx->y_max);
    uint64_t cost, cost_min;
    int d, i;
    int end_x, end_y;

    AVMotionEstPredictor *pred = &me_ctx->preds[0];

    cost_min = UINT64_MAX;

    COST_P_MV(x_mb + me_ctx->pred_x, y_mb + me_ctx->pred_y);

    for (i = 0; i < pred->nb; i++)
        COST_P_MV(x_mb + pred->mvs[i][0], y_mb + pred->mvs[i][1]);

    // Unsymmetrical-cross Search
    x = mv[0];
    y = mv[1];
    for (d = 1; d <= me_ctx->search_param; d += 2) {
        COST_P_MV(x - d, y);
        COST_P_MV(x + d, y);
        if (d <= me_ctx->search_param / 2) {
            COST_P_MV(x, y - d);
            COST_P_MV(x, y + d);
        }
    }

    // Uneven Multi-Hexagon-Grid Search
    end_x = FFMIN(mv[0] + 2, x_max);
    end_y = FFMIN(mv[1] + 2, y_max);
    for (y = FFMAX(y_min, mv[1] - 2); y <= end_y; y++)
        for (x = FFMAX(x_min, mv[0] - 2); x <= end_x; x++)
            COST_P_MV(x, y);

    x = mv[0];
    y = mv[1];
    for (d = 1; d <= me_ctx->search_param / 4; d++)
        for (i = 1; i < 16; i++)
            COST_P_MV(x + hex4[i][0] * d, y + hex4[i][1] * d);

    // Extended Hexagon-based Search
    do {
        x = mv[0];
        y = mv[1];

        for (i = 0; i < 6; i++)
            COST_P_MV(x + hex2[i][0], y + hex2[i][1]);

    } while (x != mv[0] || y != mv[1]);

    for (i = 0; i < 4; i++)
        COST_P_MV(x + dia1[i][0], y + dia1[i][1]);

    return cost_min;
}
