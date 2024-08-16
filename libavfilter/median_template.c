/*
 * Copyright (c) 2019 Paul B Mahol
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
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 */

#include "libavutil/avassert.h"
#include "avfilter.h"
#include "video.h"

#undef pixel
#if DEPTH == 8
#define pixel uint8_t
#else
#define pixel uint16_t
#endif

#undef htype
#define htype uint16_t

#undef fn
#undef fn2
#undef fn3
#define SHIFT   ((DEPTH + 1) / 2)
#define BINS    (1 << SHIFT)
#define MASK    (BINS - 1)
#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, DEPTH)

#define PICK_COARSE_BIN(x, y) (BINS * (x) + ((y) >> SHIFT))
#define PICK_FINE_BIN(x, y, z) (BINS * ((x) * ((y) >> SHIFT) + (z)) + ((y) & MASK))

static void fn(filter_plane)(AVFilterContext *ctx, const uint8_t *ssrc, int src_linesize,
                             uint8_t *ddst, int dst_linesize, int width, int height,
                             int slice_h_start, int slice_h_end, int jobnr)
{
    MedianContext *s = ctx->priv;
    htype *ccoarse = s->coarse[jobnr];
    htype *cfine = s->fine[jobnr];
    const int radius = s->radius;
    const int radiusV = s->radiusV;
    const int t = s->t;
    const pixel *src = (const pixel *)ssrc;
    pixel *dst = (pixel *)ddst;
    const pixel *srcp;
    const pixel *p;

    src_linesize /= sizeof(pixel);
    dst_linesize /= sizeof(pixel);

    memset(cfine, 0, s->fine_size * sizeof(*cfine));
    memset(ccoarse, 0, s->coarse_size * sizeof(*ccoarse));

    srcp = src + FFMAX(0, slice_h_start - radiusV) * src_linesize;
    if (jobnr == 0) {
        for (int i = 0; i < width; i++) {
            cfine[PICK_FINE_BIN(width, srcp[i], i)] += radiusV + 1;
            ccoarse[PICK_COARSE_BIN(i, srcp[i])] += radiusV + 1;
        }
    }

    srcp = src + FFMAX(0, slice_h_start - radiusV - (jobnr != 0)) * src_linesize;
    for (int i = 0; i < radiusV + (jobnr != 0) * (1 + radiusV); i++) {
        for (int j = 0; j < width; j++) {
            cfine[PICK_FINE_BIN(width, srcp[j], j)]++;
            ccoarse[PICK_COARSE_BIN(j, srcp[j])]++;
        }
        srcp += src_linesize;
    }

    srcp = src;

    for (int i = slice_h_start; i < slice_h_end; i++) {
        htype coarse[BINS] = { 0 };
        htype fine[BINS][BINS] = { { 0 } };
        htype luc[BINS] = { 0 };

        p = srcp + src_linesize * FFMAX(0, i - radiusV - 1);
        for (int j = 0; j < width; j++) {
            cfine[PICK_FINE_BIN(width, p[j], j)]--;
            ccoarse[PICK_COARSE_BIN(j, p[j])]--;
        }

        p = srcp + src_linesize * FFMIN(height - 1, i + radiusV);
        for (int j = 0; j < width; j++) {
            cfine[PICK_FINE_BIN(width, p[j], j)]++;
            ccoarse[PICK_COARSE_BIN(j, p[j])]++;
        }

        s->hmuladd(coarse, &ccoarse[0], radius, BINS);
        for (int j = 0; j < radius; j++)
            s->hadd(coarse, &ccoarse[BINS * j], BINS);
        for (int k = 0; k < BINS; k++)
            s->hmuladd(&fine[k][0], &cfine[BINS * width * k], 2 * radius + 1, BINS);

        for (int j = 0; j < width; j++) {
            int sum = 0, k, b;
            htype *segment;

            s->hadd(coarse, &ccoarse[BINS * FFMIN(j + radius, width - 1)], BINS);

            for (k = 0; k < BINS; k++) {
                sum += coarse[k];
                if (sum > t) {
                    sum -= coarse[k];
                    break;
                }
            }
            av_assert0(k < BINS);

            if (luc[k] <= j - radius) {
                memset(&fine[k], 0, BINS * sizeof(htype));
                for (luc[k] = j - radius; luc[k] < FFMIN(j + radius + 1, width); luc[k]++)
                    s->hadd(fine[k], &cfine[BINS * (width * k + luc[k])], BINS);
                if (luc[k] < j + radius + 1) {
                    s->hmuladd(&fine[k][0], &cfine[BINS * (width * k + width - 1)], j + radius + 1 - width, BINS);
                    luc[k] = j + radius + 1;
                }
            } else {
                for (; luc[k] < j + radius + 1; luc[k]++) {
                    s->hsub(fine[k], &cfine[BINS * (width * k + FFMAX(luc[k] - 2 * radius - 1, 0))], BINS);
                    s->hadd(fine[k], &cfine[BINS * (width * k + FFMIN(luc[k], width - 1))], BINS);
                }
            }

            s->hsub(coarse, &ccoarse[BINS * FFMAX(j - radius, 0)], BINS);

            segment = fine[k];
            for (b = 0; b < BINS; b++) {
                sum += segment[b];
                if (sum > t) {
                    dst[j] = BINS * k + b;
                    break;
                }
            }
            av_assert0(b < BINS);
        }

        dst += dst_linesize;
    }
}
