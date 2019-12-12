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

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/attributes.h"
#include "kbdwin.h"

#define BESSEL_I0_ITER 50 // default: 50 iterations of Bessel I0 approximation

av_cold void ff_kbd_window_init(float *window, float alpha, int n)
{
   int i, j;
   double sum = 0.0, bessel, tmp;
   double local_window[FF_KBD_WINDOW_MAX];
   double alpha2 = (alpha * M_PI / n) * (alpha * M_PI / n);

   av_assert0(n <= FF_KBD_WINDOW_MAX);

   for (i = 0; i < n; i++) {
       tmp = i * (n - i) * alpha2;
       bessel = 1.0;
       for (j = BESSEL_I0_ITER; j > 0; j--)
           bessel = bessel * tmp / (j * j) + 1;
       sum += bessel;
       local_window[i] = sum;
   }

   sum++;
   for (i = 0; i < n; i++)
       window[i] = sqrt(local_window[i] / sum);
}

av_cold void ff_kbd_window_init_fixed(int32_t *window, float alpha, int n)
{
    int i;
    float local_window[FF_KBD_WINDOW_MAX];

    ff_kbd_window_init(local_window, alpha, n);
    for (i = 0; i < n; i++)
        window[i] = (int)floor(2147483647.0 * local_window[i] + 0.5);
}
