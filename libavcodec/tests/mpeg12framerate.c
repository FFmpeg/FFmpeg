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

#include "libavcodec/mpeg12.h"
#include "libavcodec/mpeg12data.h"

int main(void)
{
    int i;

#define TEST_MATCH(frame_rate, code, ext_n, ext_d) do { \
        AVRational fr = frame_rate; \
        int c, n, d; \
        ff_mpeg12_find_best_frame_rate(fr, &c, &n, &d, 0); \
        if (c != code || n != ext_n || d != ext_d) { \
            av_log(NULL, AV_LOG_ERROR, "Failed to match %d/%d: " \
                   "code = %d, ext_n = %d, ext_d = %d.\n", \
                   fr.num, fr.den, c, n, d); \
            return 1; \
        } \
    } while (0)
#define TEST_EXACT(frn, frd) do { \
        AVRational fr = (AVRational) { frn, frd }; \
        int c, n, d; \
        ff_mpeg12_find_best_frame_rate(fr, &c, &n, &d, 0); \
        if (av_cmp_q(fr, av_mul_q(ff_mpeg12_frame_rate_tab[c], \
                                  (AVRational) { n + 1, d + 1 })) != 0) { \
            av_log(NULL, AV_LOG_ERROR, "Failed to find exact %d/%d: " \
                   "code = %d, ext_n = %d, ext_d = %d.\n", \
                   fr.num, fr.den, c, n, d); \
            return 1; \
        } \
    } while (0)

    // Framerates in the table must be chosen exactly.
    for (i = 1; i <= 8; i++)
        TEST_MATCH(ff_mpeg12_frame_rate_tab[i], i, 0, 0);

    // As should the same ones with small perturbations.
    // (1/1000 used here to be smaller than half the difference
    // between 24 and 24000/1001.)
    for (i = 1; i <= 8; i++) {
        TEST_MATCH(av_sub_q(ff_mpeg12_frame_rate_tab[i],
                            (AVRational) { 1, 1000 }), i, 0, 0);
        TEST_MATCH(av_add_q(ff_mpeg12_frame_rate_tab[i],
                            (AVRational) { 1, 1000 }), i, 0, 0);
    }

    // Exactly constructable framerates should be exact.  Note that some
    // values can be made in multiple ways (e.g. 12 = 24 / 2 == 60 / 5),
    // and there is no reason to favour any particular choice.
    TEST_EXACT(     1,    1);
    TEST_EXACT(     2,    1);
    TEST_EXACT(    12,    1);
    TEST_EXACT( 15000, 1001);
    TEST_EXACT(    15,    1);
    TEST_EXACT(   120,    1);
    TEST_EXACT(120000, 1001);
    TEST_EXACT(   200,    1);
    TEST_EXACT(   240,    1);

    // Values higher than 240 (the highest representable, as 60 * 4 / 1)
    // should be mapped to 240.
    for (i = 240; i < 1000; i += 10)
        TEST_MATCH(((AVRational) { i, 1 }), 8, 3, 0);
    // Values lower than 24000/32032 (the lowest representable, as
    // 24000/1001 * 1 / 32) should be mapped to 24000/32032.
    for (i = 74; i > 0; i--)
        TEST_MATCH(((AVRational) { i, 100 }), 1, 0, 31);

    return 0;
}
