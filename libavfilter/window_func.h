/*
 * Copyright (c) 2015 Paul B Mahol
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


#ifndef AVFILTER_WINDOW_FUNC_H
#define AVFILTER_WINDOW_FUNC_H

enum WindowFunc     { WFUNC_RECT, WFUNC_HANNING, WFUNC_HAMMING, WFUNC_BLACKMAN,
                      WFUNC_BARTLETT, WFUNC_WELCH, WFUNC_FLATTOP,
                      WFUNC_BHARRIS, WFUNC_BNUTTALL, WFUNC_SINE, WFUNC_NUTTALL,
                      WFUNC_BHANN, WFUNC_LANCZOS, WFUNC_GAUSS, WFUNC_TUKEY,
                      WFUNC_DOLPH, WFUNC_CAUCHY, WFUNC_PARZEN, WFUNC_POISSON,
                      NB_WFUNC };

void ff_generate_window_func(float *lut, int N, int win_func, float *overlap);

#endif /* AVFILTER_WINDOW_FUNC_H */
