/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 * Copyright (C) 2013 Lenny Wang
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

#ifndef AVFILTER_DESHAKE_H
#define AVFILTER_DESHAKE_H

#include "config.h"
#include "avfilter.h"
#include "transform.h"
#include "libavutil/pixelutils.h"


enum SearchMethod {
    EXHAUSTIVE,        ///< Search all possible positions
    SMART_EXHAUSTIVE,  ///< Search most possible positions (faster)
    SEARCH_COUNT
};

typedef struct IntMotionVector {
    int x;             ///< Horizontal shift
    int y;             ///< Vertical shift
} IntMotionVector;

typedef struct MotionVector {
    double x;             ///< Horizontal shift
    double y;             ///< Vertical shift
} MotionVector;

typedef struct Transform {
    MotionVector vec;     ///< Motion vector
    double angle;         ///< Angle of rotation
    double zoom;          ///< Zoom percentage
} Transform;

#define MAX_R 64

typedef struct DeshakeContext {
    const AVClass *class;
    int counts[2*MAX_R+1][2*MAX_R+1]; /// < Scratch buffer for motion search
    double *angles;            ///< Scratch buffer for block angles
    unsigned angles_size;
    AVFrame *ref;              ///< Previous frame
    int rx;                    ///< Maximum horizontal shift
    int ry;                    ///< Maximum vertical shift
    int edge;                  ///< Edge fill method
    int blocksize;             ///< Size of blocks to compare
    int contrast;              ///< Contrast threshold
    int search;                ///< Motion search method
    av_pixelutils_sad_fn sad;  ///< Sum of the absolute difference function
    Transform last;            ///< Transform from last frame
    int refcount;              ///< Number of reference frames (defines averaging window)
    FILE *fp;
    Transform avg;
    int cw;                    ///< Crop motion search to this box
    int ch;
    int cx;
    int cy;
    char *filename;            ///< Motion search detailed log filename
    int opencl;
    int (* transform)(AVFilterContext *ctx, int width, int height, int cw, int ch,
                      const float *matrix_y, const float *matrix_uv, enum InterpolateMethod interpolate,
                      enum FillMethod fill, AVFrame *in, AVFrame *out);
} DeshakeContext;

#endif /* AVFILTER_DESHAKE_H */
