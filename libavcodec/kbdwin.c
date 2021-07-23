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
#include "libavutil/error.h"
#include "libavutil/libm.h"
#include "libavutil/mathematics.h"
#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "kbdwin.h"

av_cold static int kbd_window_init(float *float_window, int *int_window, float alpha, int n)
{
   int i;
   double sum = 0.0, tmp;
   double scale = 0.0;
   double temp_small[FF_KBD_WINDOW_MAX / 2 + 1];
   double *temp= n<=FF_KBD_WINDOW_MAX ? temp_small : av_malloc((n/2+1) * sizeof(*temp));
   double alpha2 = 4 * (alpha * M_PI / n) * (alpha * M_PI / n);

   if (!temp)
       return AVERROR(ENOMEM);

   for (i = 0; i <= n / 2; i++) {
       tmp = alpha2 * i * (n - i);
       temp[i] = av_bessel_i0(sqrt(tmp));
       scale += temp[i] * (1 + (i && i<n/2));
   }
   scale = 1.0/(scale + 1);

   for (i = 0; i <= n / 2; i++) {
       sum += temp[i];
       if (float_window) float_window[i] = sqrt(sum * scale);
       else                int_window[i] = lrint(2147483647 * sqrt(sum * scale));
   }
   for (; i < n; i++) {
       sum += temp[n - i];
       if (float_window) float_window[i] = sqrt(sum * scale);
       else                int_window[i] = lrint(2147483647 * sqrt(sum * scale));
   }
   if (temp != temp_small)
       av_free(temp);
   return 0;
}

av_cold int avpriv_kbd_window_init(float *window, float alpha, int n)
{
    return kbd_window_init(window, NULL, alpha, n);
}

av_cold int avpriv_kbd_window_init_fixed(int32_t *window, float alpha, int n)
{
    return kbd_window_init(NULL, window, alpha, n);
}
