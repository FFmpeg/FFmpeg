/*
 * Copyright (C) 2007 Vitor Sessak <vitor1001@gmail.com>
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
 * Codebook Generator using the ELBG algorithm
 */

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "elbg.h"

#define DELTA_ERR_MAX 0.1  ///< Precision of the ELBG algorithm (as percentage error)

/**
 * In the ELBG jargon, a cell is the set of points that are closest to a
 * codebook entry. Not to be confused with a RoQ Video cell. */
typedef struct cell_s {
    int index;
    struct cell_s *next;
} cell;

/**
 * ELBG internal data
 */
typedef struct ELBGContext {
    int error;
    int dim;
    int num_cb;
    int *codebook;
    cell **cells;
    int *utility;
    int *utility_inc;
    int *nearest_cb;
    int *points;
    int *temp_points;
    int *size_part;
    AVLFG *rand_state;
    int *scratchbuf;
    cell *cell_buffer;

    /* Sizes for the buffers above. Pointers without such a field
     * are not allocated by us and only valid for the duration
     * of a single call to avpriv_elbg_do(). */
    unsigned utility_allocated;
    unsigned utility_inc_allocated;
    unsigned size_part_allocated;
    unsigned cells_allocated;
    unsigned scratchbuf_allocated;
    unsigned cell_buffer_allocated;
    unsigned temp_points_allocated;
} ELBGContext;

static inline int distance_limited(int *a, int *b, int dim, int limit)
{
    int i, dist=0;
    for (i=0; i<dim; i++) {
        int64_t distance = a[i] - b[i];

        distance *= distance;
        if (dist >= limit - distance)
            return limit;
        dist += distance;
    }

    return dist;
}

static inline void vect_division(int *res, int *vect, int div, int dim)
{
    int i;
    if (div > 1)
        for (i=0; i<dim; i++)
            res[i] = ROUNDED_DIV(vect[i],div);
    else if (res != vect)
        memcpy(res, vect, dim*sizeof(int));

}

static int eval_error_cell(ELBGContext *elbg, int *centroid, cell *cells)
{
    int error=0;
    for (; cells; cells=cells->next) {
        int distance = distance_limited(centroid, elbg->points + cells->index*elbg->dim, elbg->dim, INT_MAX);
        if (error >= INT_MAX - distance)
            return INT_MAX;
        error += distance;
    }

    return error;
}

static int get_closest_codebook(ELBGContext *elbg, int index)
{
    int pick = 0;
    for (int i = 0, diff_min = INT_MAX; i < elbg->num_cb; i++)
        if (i != index) {
            int diff;
            diff = distance_limited(elbg->codebook + i*elbg->dim, elbg->codebook + index*elbg->dim, elbg->dim, diff_min);
            if (diff < diff_min) {
                pick = i;
                diff_min = diff;
            }
        }
    return pick;
}

static int get_high_utility_cell(ELBGContext *elbg)
{
    int i=0;
    /* Using linear search, do binary if it ever turns to be speed critical */
    uint64_t r;

    if (elbg->utility_inc[elbg->num_cb - 1] < INT_MAX) {
        r = av_lfg_get(elbg->rand_state) % (unsigned int)elbg->utility_inc[elbg->num_cb - 1] + 1;
    } else {
        r = av_lfg_get(elbg->rand_state);
        r = (av_lfg_get(elbg->rand_state) + (r<<32)) % elbg->utility_inc[elbg->num_cb - 1] + 1;
    }

    while (elbg->utility_inc[i] < r) {
        i++;
    }

    av_assert2(elbg->cells[i]);

    return i;
}

/**
 * Implementation of the simple LBG algorithm for just two codebooks
 */
static int simple_lbg(ELBGContext *elbg,
                      int dim,
                      int *centroid[3],
                      int newutility[3],
                      int *points,
                      cell *cells)
{
    int i, idx;
    int numpoints[2] = {0,0};
    int *newcentroid[2] = {
        elbg->scratchbuf + 3*dim,
        elbg->scratchbuf + 4*dim
    };
    cell *tempcell;

    memset(newcentroid[0], 0, 2 * dim * sizeof(*newcentroid[0]));

    newutility[0] =
    newutility[1] = 0;

    for (tempcell = cells; tempcell; tempcell=tempcell->next) {
        idx = distance_limited(centroid[0], points + tempcell->index*dim, dim, INT_MAX)>=
              distance_limited(centroid[1], points + tempcell->index*dim, dim, INT_MAX);
        numpoints[idx]++;
        for (i=0; i<dim; i++)
            newcentroid[idx][i] += points[tempcell->index*dim + i];
    }

    vect_division(centroid[0], newcentroid[0], numpoints[0], dim);
    vect_division(centroid[1], newcentroid[1], numpoints[1], dim);

    for (tempcell = cells; tempcell; tempcell=tempcell->next) {
        int dist[2] = {distance_limited(centroid[0], points + tempcell->index*dim, dim, INT_MAX),
                       distance_limited(centroid[1], points + tempcell->index*dim, dim, INT_MAX)};
        int idx = dist[0] > dist[1];
        if (newutility[idx] >= INT_MAX - dist[idx])
            newutility[idx] = INT_MAX;
        else
            newutility[idx] += dist[idx];
    }

    return (newutility[0] >= INT_MAX - newutility[1]) ? INT_MAX : newutility[0] + newutility[1];
}

static void get_new_centroids(ELBGContext *elbg, int huc, int *newcentroid_i,
                              int *newcentroid_p)
{
    cell *tempcell;
    int *min = newcentroid_i;
    int *max = newcentroid_p;
    int i;

    for (i=0; i< elbg->dim; i++) {
        min[i]=INT_MAX;
        max[i]=0;
    }

    for (tempcell = elbg->cells[huc]; tempcell; tempcell = tempcell->next)
        for(i=0; i<elbg->dim; i++) {
            min[i]=FFMIN(min[i], elbg->points[tempcell->index*elbg->dim + i]);
            max[i]=FFMAX(max[i], elbg->points[tempcell->index*elbg->dim + i]);
        }

    for (i=0; i<elbg->dim; i++) {
        int ni = min[i] + (max[i] - min[i])/3;
        int np = min[i] + (2*(max[i] - min[i]))/3;
        newcentroid_i[i] = ni;
        newcentroid_p[i] = np;
    }
}

/**
 * Add the points in the low utility cell to its closest cell. Split the high
 * utility cell, putting the separated points in the (now empty) low utility
 * cell.
 *
 * @param elbg         Internal elbg data
 * @param indexes      {luc, huc, cluc}
 * @param newcentroid  A vector with the position of the new centroids
 */
static void shift_codebook(ELBGContext *elbg, int *indexes,
                           int *newcentroid[3])
{
    cell *tempdata;
    cell **pp = &elbg->cells[indexes[2]];

    while(*pp)
        pp= &(*pp)->next;

    *pp = elbg->cells[indexes[0]];

    elbg->cells[indexes[0]] = NULL;
    tempdata = elbg->cells[indexes[1]];
    elbg->cells[indexes[1]] = NULL;

    while(tempdata) {
        cell *tempcell2 = tempdata->next;
        int idx = distance_limited(elbg->points + tempdata->index*elbg->dim,
                           newcentroid[0], elbg->dim, INT_MAX) >
                  distance_limited(elbg->points + tempdata->index*elbg->dim,
                           newcentroid[1], elbg->dim, INT_MAX);

        tempdata->next = elbg->cells[indexes[idx]];
        elbg->cells[indexes[idx]] = tempdata;
        tempdata = tempcell2;
    }
}

static void evaluate_utility_inc(ELBGContext *elbg)
{
    int64_t inc=0;

    for (int i = 0; i < elbg->num_cb; i++) {
        if (elbg->num_cb * (int64_t)elbg->utility[i] > elbg->error)
            inc += elbg->utility[i];
        elbg->utility_inc[i] = FFMIN(inc, INT_MAX);
    }
}


static void update_utility_and_n_cb(ELBGContext *elbg, int idx, int newutility)
{
    cell *tempcell;

    elbg->utility[idx] = newutility;
    for (tempcell=elbg->cells[idx]; tempcell; tempcell=tempcell->next)
        elbg->nearest_cb[tempcell->index] = idx;
}

/**
 * Evaluate if a shift lower the error. If it does, call shift_codebooks
 * and update elbg->error, elbg->utility and elbg->nearest_cb.
 *
 * @param elbg  Internal elbg data
 * @param idx   {luc (low utility cell, huc (high utility cell), cluc (closest cell to low utility cell)}
 */
static void try_shift_candidate(ELBGContext *elbg, int idx[3])
{
    int j, k, cont=0, tmp;
    int64_t olderror=0, newerror;
    int newutility[3];
    int *newcentroid[3] = {
        elbg->scratchbuf,
        elbg->scratchbuf + elbg->dim,
        elbg->scratchbuf + 2*elbg->dim
    };
    cell *tempcell;

    for (j=0; j<3; j++)
        olderror += elbg->utility[idx[j]];

    memset(newcentroid[2], 0, elbg->dim*sizeof(int));

    for (k=0; k<2; k++)
        for (tempcell=elbg->cells[idx[2*k]]; tempcell; tempcell=tempcell->next) {
            cont++;
            for (j=0; j<elbg->dim; j++)
                newcentroid[2][j] += elbg->points[tempcell->index*elbg->dim + j];
        }

    vect_division(newcentroid[2], newcentroid[2], cont, elbg->dim);

    get_new_centroids(elbg, idx[1], newcentroid[0], newcentroid[1]);

    newutility[2]  = eval_error_cell(elbg, newcentroid[2], elbg->cells[idx[0]]);
    tmp            = eval_error_cell(elbg, newcentroid[2], elbg->cells[idx[2]]);
    newutility[2]  = (tmp >= INT_MAX - newutility[2]) ? INT_MAX : newutility[2] + tmp;

    newerror = newutility[2];

    tmp = simple_lbg(elbg, elbg->dim, newcentroid, newutility, elbg->points,
                           elbg->cells[idx[1]]);
    if (tmp >= INT_MAX - newerror)
        newerror = INT_MAX;
    else
        newerror += tmp;

    if (olderror > newerror) {
        shift_codebook(elbg, idx, newcentroid);

        elbg->error += newerror - olderror;

        for (j=0; j<3; j++)
            update_utility_and_n_cb(elbg, idx[j], newutility[j]);

        evaluate_utility_inc(elbg);
    }
 }

/**
 * Implementation of the ELBG block
 */
static void do_shiftings(ELBGContext *elbg)
{
    int idx[3];

    evaluate_utility_inc(elbg);

    for (idx[0]=0; idx[0] < elbg->num_cb; idx[0]++)
        if (elbg->num_cb * (int64_t)elbg->utility[idx[0]] < elbg->error) {
            if (elbg->utility_inc[elbg->num_cb - 1] == 0)
                return;

            idx[1] = get_high_utility_cell(elbg);
            idx[2] = get_closest_codebook(elbg, idx[0]);

            if (idx[1] != idx[0] && idx[1] != idx[2])
                try_shift_candidate(elbg, idx);
        }
}

static void do_elbg(ELBGContext *restrict elbg, int *points, int numpoints,
                    int max_steps)
{
    int *const size_part = elbg->size_part;
    int i, j, steps = 0;
    int best_idx = 0;
    int last_error;

    elbg->error = INT_MAX;
    elbg->points = points;

    do {
        cell *free_cells = elbg->cell_buffer;
        last_error = elbg->error;
        steps++;
        memset(elbg->utility, 0, elbg->num_cb * sizeof(*elbg->utility));
        memset(elbg->cells,   0, elbg->num_cb * sizeof(*elbg->cells));

        elbg->error = 0;

        /* This loop evaluate the actual Voronoi partition. It is the most
           costly part of the algorithm. */
        for (i=0; i < numpoints; i++) {
            int best_dist = distance_limited(elbg->points   + i * elbg->dim,
                                             elbg->codebook + best_idx * elbg->dim,
                                             elbg->dim, INT_MAX);
            for (int k = 0; k < elbg->num_cb; k++) {
                int dist = distance_limited(elbg->points   + i * elbg->dim,
                                            elbg->codebook + k * elbg->dim,
                                            elbg->dim, best_dist);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = k;
                }
            }
            elbg->nearest_cb[i] = best_idx;
            elbg->error = (elbg->error >= INT_MAX - best_dist) ? INT_MAX : elbg->error + best_dist;
            elbg->utility[elbg->nearest_cb[i]] = (elbg->utility[elbg->nearest_cb[i]] >= INT_MAX - best_dist) ?
                                                  INT_MAX : elbg->utility[elbg->nearest_cb[i]] + best_dist;
            free_cells->index = i;
            free_cells->next = elbg->cells[elbg->nearest_cb[i]];
            elbg->cells[elbg->nearest_cb[i]] = free_cells;
            free_cells++;
        }

        do_shiftings(elbg);

        memset(size_part,      0, elbg->num_cb * sizeof(*size_part));

        memset(elbg->codebook, 0, elbg->num_cb * elbg->dim * sizeof(*elbg->codebook));

        for (i=0; i < numpoints; i++) {
            size_part[elbg->nearest_cb[i]]++;
            for (j=0; j < elbg->dim; j++)
                elbg->codebook[elbg->nearest_cb[i]*elbg->dim + j] +=
                    elbg->points[i*elbg->dim + j];
        }

        for (int i = 0; i < elbg->num_cb; i++)
            vect_division(elbg->codebook + i*elbg->dim,
                          elbg->codebook + i*elbg->dim, size_part[i], elbg->dim);

    } while(((last_error - elbg->error) > DELTA_ERR_MAX*elbg->error) &&
            (steps < max_steps));
}

#define BIG_PRIME 433494437LL

/**
 * Initialize the codebook vector for the elbg algorithm.
 * If numpoints <= 24 * num_cb this function fills codebook with random numbers.
 * If not, it calls do_elbg for a (smaller) random sample of the points in
 * points.
 */
static void init_elbg(ELBGContext *restrict elbg, int *points, int *temp_points,
                      int numpoints, int max_steps)
{
    int dim = elbg->dim;

    if (numpoints > 24LL * elbg->num_cb) {
        /* ELBG is very costly for a big number of points. So if we have a lot
           of them, get a good initial codebook to save on iterations       */
        for (int i = 0; i < numpoints / 8; i++) {
            int k = (i*BIG_PRIME) % numpoints;
            memcpy(temp_points + i*dim, points + k*dim, dim * sizeof(*temp_points));
        }

        /* If anything is changed in the recursion parameters,
         * the allocated size of temp_points will also need to be updated. */
        init_elbg(elbg, temp_points, temp_points + numpoints / 8 * dim,
                  numpoints / 8, 2 * max_steps);
        do_elbg(elbg, temp_points, numpoints / 8, 2 * max_steps);
    } else  // If not, initialize the codebook with random positions
        for (int i = 0; i < elbg->num_cb; i++)
            memcpy(elbg->codebook + i * dim, points + ((i*BIG_PRIME)%numpoints)*dim,
                   dim * sizeof(*elbg->codebook));
}

int avpriv_elbg_do(ELBGContext **elbgp, int *points, int dim, int numpoints,
                   int *codebook, int num_cb, int max_steps,
                   int *closest_cb, AVLFG *rand_state, uintptr_t flags)
{
    ELBGContext *const restrict elbg = *elbgp ? *elbgp : av_mallocz(sizeof(*elbg));

    if (!elbg)
        return AVERROR(ENOMEM);
    *elbgp = elbg;

    elbg->nearest_cb = closest_cb;
    elbg->rand_state = rand_state;
    elbg->codebook   = codebook;
    elbg->num_cb     = num_cb;
    elbg->dim        = dim;

#define ALLOCATE_IF_NECESSARY(field, new_elements, multiplicator)            \
    if (elbg->field ## _allocated < new_elements) {                          \
        av_freep(&elbg->field);                                              \
        elbg->field = av_malloc_array(new_elements,                          \
                                      multiplicator * sizeof(*elbg->field)); \
        if (!elbg->field) {                                                  \
            elbg->field ## _allocated = 0;                                   \
            return AVERROR(ENOMEM);                                          \
        }                                                                    \
        elbg->field ## _allocated = new_elements;                            \
    }
    /* Allocating the buffers for do_elbg() here once relies
     * on their size being always the same even when do_elbg()
     * is called from init_elbg(). It also relies on do_elbg()
     * never calling itself recursively. */
    ALLOCATE_IF_NECESSARY(cells,       num_cb,    1)
    ALLOCATE_IF_NECESSARY(utility,     num_cb,    1)
    ALLOCATE_IF_NECESSARY(utility_inc, num_cb,    1)
    ALLOCATE_IF_NECESSARY(size_part,   num_cb,    1)
    ALLOCATE_IF_NECESSARY(cell_buffer, numpoints, 1)
    ALLOCATE_IF_NECESSARY(scratchbuf,  dim,       5)
    if (numpoints > 24LL * elbg->num_cb) {
        /* The first step in the recursion in init_elbg() needs a buffer with
        * (numpoints / 8) * dim elements; the next step needs numpoints / 8 / 8
        * * dim elements etc. The geometric series leads to an upper bound of
        * numpoints / 8 * 8 / 7 * dim elements. */
        uint64_t prod = dim * (uint64_t)(numpoints / 7U);
        if (prod > INT_MAX)
            return AVERROR(ERANGE);
        ALLOCATE_IF_NECESSARY(temp_points, prod, 1)
    }

    init_elbg(elbg, points, elbg->temp_points, numpoints, max_steps);
    do_elbg (elbg, points, numpoints, max_steps);
    return 0;
}

av_cold void avpriv_elbg_free(ELBGContext **elbgp)
{
    ELBGContext *elbg = *elbgp;
    if (!elbg)
        return;

    av_freep(&elbg->size_part);
    av_freep(&elbg->utility);
    av_freep(&elbg->cell_buffer);
    av_freep(&elbg->cells);
    av_freep(&elbg->utility_inc);
    av_freep(&elbg->scratchbuf);
    av_freep(&elbg->temp_points);

    av_freep(elbgp);
}
