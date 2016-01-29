/*
 * Copyright (c) 2015 Muhammad Faiz <mfcc64@gmail.com>
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

#ifndef AVFILTER_SHOWCQT_H
#define AVFILTER_SHOWCQT_H

#include "libavcodec/avfft.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    FFTSample *val;
    int start, len;
} Coeffs;

enum CoeffsType {
    COEFFS_TYPE_DEFAULT,
    COEFFS_TYPE_INTERLEAVE
};

typedef struct {
    float r, g, b;
} RGBFloat;

typedef struct {
    float y, u, v;
} YUVFloat;

typedef union {
    RGBFloat rgb;
    YUVFloat yuv;
} ColorFloat;

typedef struct {
    const AVClass       *class;
    AVFilterContext     *ctx;
    AVFrame             *axis_frame;
    AVFrame             *sono_frame;
    enum AVPixelFormat  format;
    int                 sono_idx;
    int                 sono_count;
    int                 step;
    AVRational          step_frac;
    int                 remaining_frac;
    int                 remaining_fill;
    int64_t             frame_count;
    double              *freq;
    FFTContext          *fft_ctx;
    Coeffs              *coeffs;
    FFTComplex          *fft_data;
    FFTComplex          *fft_result;
    FFTComplex          *cqt_result;
    int                 fft_bits;
    int                 fft_len;
    int                 cqt_len;
    int                 cqt_align;
    enum CoeffsType     cqt_coeffs_type;
    ColorFloat          *c_buf;
    float               *h_buf;
    float               *rcp_h_buf;
    float               *sono_v_buf;
    float               *bar_v_buf;
    /* callback */
    void                (*cqt_calc)(FFTComplex *dst, const FFTComplex *src, const Coeffs *coeffs,
                                    int len, int fft_len);
    void                (*draw_bar)(AVFrame *out, const float *h, const float *rcp_h,
                                    const ColorFloat *c, int bar_h);
    void                (*draw_axis)(AVFrame *out, AVFrame *axis, const ColorFloat *c, int off);
    void                (*draw_sono)(AVFrame *out, AVFrame *sono, int off, int idx);
    void                (*update_sono)(AVFrame *sono, const ColorFloat *c, int idx);
    /* option */
    int                 width, height;
    AVRational          rate;
    int                 bar_h;
    int                 axis_h;
    int                 sono_h;
    int                 fullhd; /* deprecated */
    char                *sono_v;
    char                *bar_v;
    float               sono_g;
    float               bar_g;
    double              timeclamp;
    double              basefreq;
    double              endfreq;
    float               coeffclamp; /* deprecated - ignored */
    char                *tlength;
    int                 count;
    int                 fcount;
    char                *fontfile;
    char                *fontcolor;
    char                *axisfile;
    int                 axis;
} ShowCQTContext;

#endif
