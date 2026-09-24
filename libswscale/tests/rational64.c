/*
 * rational numbers
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

#include "libswscale/rational64.c"
#include "libavutil/integer.h"
#include "libavutil/intfloat.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"

int main(void)
{
    AVRational64 a64,b64,r64;
    int i;

    for (a64.num = -2; a64.num <= 2; a64.num++) {
        for (a64.den = -2; a64.den <= 2; a64.den++) {
            for (b64.num = -2; b64.num <= 2; b64.num++) {
                for (b64.den = -2; b64.den <= 2; b64.den++) {
                    const double adbl = ff_q2d_64(a64);
                    const double bdbl = ff_q2d_64(b64);
                    const int c = ff_cmp_q64(a64,b64);
                    const int d = adbl == bdbl ?  0 :
                                  adbl >  bdbl ?  1 :
                                  adbl <  bdbl ? -1 : INT_MIN;

                    if (c != d)
                        av_log(NULL, AV_LOG_ERROR, "%lld/%lld %lld/%lld, %d != %d\n",
                               (long long) a64.num, (long long) a64.den,
                               (long long) b64.num, (long long) b64.den, c,d);

                    // Check arithmetic result
                    if (a64.den && b64.den) {
                        double rdbl;

                        r64 = ff_add_q64(a64, b64);
                        rdbl = ff_q2d_64(r64);
                        if (rdbl != adbl + bdbl) {
                            av_log(NULL, AV_LOG_ERROR, "%f + %f = %f != %f\n",
                                   adbl, bdbl, rdbl, adbl + bdbl);
                        }

                        r64 = ff_mul_q64(a64, b64);
                        rdbl = ff_q2d_64(r64);
                        if (rdbl != adbl * bdbl) {
                            av_log(NULL, AV_LOG_ERROR, "%f * %f = %f != %f\n",
                                   adbl, bdbl, rdbl, adbl * bdbl);
                        }
                    }

                    // Check addition round-trip
                    r64 = ff_sub_q64(ff_add_q64(a64, b64), b64);
                    if (b64.den && (r64.num*a64.den != a64.num*r64.den ||
                        !r64.num != !a64.num ||
                        !r64.den != !a64.den))
                    {
                        av_log(NULL, AV_LOG_ERROR, "%lld/%lld != %lld/%lld\n",
                               (long long) a64.num, (long long) a64.den,
                               (long long) r64.num, (long long) r64.den);
                    }

                    if (b64.num) {
                        // Check multiplication round-trip
                        r64 = ff_div_q64(ff_mul_q64(a64, b64), b64);
                        if (b64.den && (r64.num*a64.den != a64.num*r64.den ||
                            !r64.num != !a64.num ||
                            !r64.den != !a64.den))
                        {
                            av_log(NULL, AV_LOG_ERROR, "%lld/%lld != %lld/%lld\n",
                                   (long long) a64.num, (long long) a64.den,
                                   (long long) r64.num, (long long) r64.den);
                        }
                    }
                }
            }
        }
    }

    /* Check overflow behavior and edge cases */
    static const AVRational64 unit_mul_q64[][3] = {
        {{INT64_MAX, 2},      { 2, 1},              { INT64_MAX, 1}},
        {{INT64_MAX, 2},      {-2, 1},              {-INT64_MAX, 1}},
        {{INT64_MAX, 2},      { 0, 1},              {0, 1}},
        {{INT64_MIN, 2},      { 2, 1},              {-INT64_MAX, 1}}, /* not INT64_MIN */
        {{INT64_MIN, 2},      {-2, 1},              { INT64_MAX, 1}},
        {{INT64_MIN, 2},      { 0, 1},              {0, 1}},
        {{INT64_MAX >> 8, 1}, {INT64_MAX >> 8, 1},  {INT64_MAX, 1}},
        {{1, INT64_MAX >> 8}, {1, INT64_MAX >> 8},  {0, 1}},
        {{1, 1},              {0, 0},               {0, 0}},
        {{0, 1},              {0, 0},               {0, 0}},
    };

    for (i = 0; i < FF_ARRAY_ELEMS(unit_mul_q64); i++) {
        for (int c = 0; c < 2; c++) { /* test commutativity */
            AVRational64 a = unit_mul_q64[i][c ? 1 : 0];
            AVRational64 b = unit_mul_q64[i][c ? 0 : 1];
            AVRational64 c = unit_mul_q64[i][2];
            AVRational64 r = ff_mul_q64(a, b);
            if (r.num != c.num || r.den != c.den) {
                av_log(NULL, AV_LOG_ERROR, "%lld/%lld * %lld/%lld = %lld/%lld, expected %lld/%lld\n",
                       (long long) a.num, (long long) a.den,
                       (long long) b.num, (long long) b.den,
                       (long long) r.num, (long long) r.den,
                       (long long) c.num, (long long) c.den);
            }
        }
    }

    static const AVRational64 unit_add_q64[][3] = {
        {{INT64_MAX, 1},      { 2, 2},            { INT64_MAX, 1}},
        {{INT64_MAX, 1},      {-2, 2},            { INT64_MAX - 1, 1}},
        {{INT64_MAX, 1},      { 0, 2},            { INT64_MAX, 1}},
        {{INT64_MIN, 1},      { 2, 2},            {-INT64_MAX, 1}},
        {{INT64_MIN, 1},      {-2, 2},            {-INT64_MAX, 1}},
        {{INT64_MIN, 1},      { 0, 2},            {-INT64_MAX, 1}},
        {{INT64_MAX - 10, 1}, {20, 1},            { INT64_MAX, 1}},
        {{2, INT64_MAX},      {2, INT64_MAX},     {4, INT64_MAX}},
        {{1, 1},              {0, 0},             {0, 0}},
        {{0, 1},              {0, 0},             {0, 0}},
    };

    for (i = 0; i < FF_ARRAY_ELEMS(unit_add_q64); i++) {
        for (int c = 0; c < 2; c++) { /* test commutativity */
            AVRational64 a = unit_add_q64[i][c ? 1 : 0];
            AVRational64 b = unit_add_q64[i][c ? 0 : 1];
            AVRational64 c = unit_add_q64[i][2];
            AVRational64 r = ff_add_q64(a, b);
            if (r.num != c.num || r.den != c.den) {
                av_log(NULL, AV_LOG_ERROR, "%lld/%lld + %lld/%lld = %lld/%lld, expected %lld/%lld\n",
                       (long long) a.num, (long long) a.den,
                       (long long) b.num, (long long) b.den,
                       (long long) r.num, (long long) r.den,
                       (long long) c.num, (long long) c.den);
            }
        }
    }

    return 0;
}
