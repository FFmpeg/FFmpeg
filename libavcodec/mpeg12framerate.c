/*
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

#include "libavutil/rational.h"

#include "mpeg12.h"
#include "mpeg12data.h"

const AVRational ff_mpeg12_frame_rate_tab[16] = {
    {    0,    0},
    {24000, 1001},
    {   24,    1},
    {   25,    1},
    {30000, 1001},
    {   30,    1},
    {   50,    1},
    {60000, 1001},
    {   60,    1},
  // Xing's 15fps: (9)
    {   15,    1},
  // libmpeg3's "Unofficial economy rates": (10-13)
    {    5,    1},
    {   10,    1},
    {   12,    1},
    {   15,    1},
    {    0,    0},
};

void ff_mpeg12_find_best_frame_rate(AVRational frame_rate,
                                    int *code, int *ext_n, int *ext_d,
                                    int nonstandard)
{
    int mpeg2 = ext_n && ext_d;
    int max_code = nonstandard ? 12 : 8;
    int c, n, d, best_c, best_n, best_d;
    AVRational best_error = { INT_MAX, 1 };

    // Default to NTSC if the inputs make no sense.
    best_c = 4;
    best_n = best_d = 1;

    for (c = 1; c <= max_code; c++) {
        if (av_cmp_q(frame_rate, ff_mpeg12_frame_rate_tab[c]) == 0) {
            best_c = c;
            goto found;
        }
    }

    for (c = 1; c <= max_code; c++) {
        for (n = 1; n <= (mpeg2 ? 4 : 1); n++) {
            for (d = 1; d <= (mpeg2 ? 32 : 1); d++) {
                AVRational test, error;
                int cmp;

                test = av_mul_q(ff_mpeg12_frame_rate_tab[c],
                                (AVRational) { n, d });

                cmp = av_cmp_q(test, frame_rate);
                if (cmp == 0) {
                    best_c = c;
                    best_n = n;
                    best_d = d;
                    goto found;
                }

                if (cmp < 0)
                    error = av_div_q(frame_rate, test);
                else
                    error = av_div_q(test, frame_rate);

                cmp = av_cmp_q(error, best_error);
                if (cmp < 0 || (cmp == 0 && n == 1 && d == 1)) {
                    best_c = c;
                    best_n = n;
                    best_d = d;
                    best_error = error;
                }
            }
        }
    }

found:
    *code = best_c;
    if (mpeg2) {
        *ext_n = best_n - 1;
        *ext_d = best_d - 1;
    }
}
