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

#include "libavfilter/vf_nlmeans.c"

static void display_integral(const uint32_t *ii, int w, int h, int lz_32)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            printf(" %7x", ii[y*lz_32 + x]);
        printf("\n");
    }
    printf("---------------\n");
}

int main(void)
{
    int ret = 0, xoff, yoff;

    // arbitrary test source of size 6x4 and linesize=8
    const int w = 6, h = 5, lz = 8;
    static const uint8_t src[] = {
        0xb0, 0x71, 0xfb, 0xd8, 0x01, 0xd9, /***/ 0x01, 0x02,
        0x51, 0x8e, 0x41, 0x0f, 0x84, 0x58, /***/ 0x03, 0x04,
        0xc7, 0x8d, 0x07, 0x70, 0x5c, 0x47, /***/ 0x05, 0x06,
        0x09, 0x4e, 0xfc, 0x74, 0x8f, 0x9a, /***/ 0x07, 0x08,
        0x60, 0x8e, 0x20, 0xaa, 0x95, 0x7d, /***/ 0x09, 0x0a,
    };

    const int e = 3;
    const int ii_w = w+e*2, ii_h = h+e*2;

    // align to 4 the linesize, "+1" is for the space of the left 0-column
    const int ii_lz_32 = ((ii_w + 1) + 3) & ~3;

    // "+1" is for the space of the top 0-line
    uint32_t *ii  = av_mallocz_array(ii_h + 1, ii_lz_32 * sizeof(*ii));
    uint32_t *ii2 = av_mallocz_array(ii_h + 1, ii_lz_32 * sizeof(*ii2));

    uint32_t *ii_start  = ii  + ii_lz_32 + 1; // skip top 0-line and left 0-column
    uint32_t *ii_start2 = ii2 + ii_lz_32 + 1; // skip top 0-line and left 0-column

    if (!ii || !ii2)
        return -1;

    for (yoff = -e; yoff <= e; yoff++) {
        for (xoff = -e; xoff <= e; xoff++) {
            printf("xoff=%d yoff=%d\n", xoff, yoff);

            compute_ssd_integral_image(ii_start, ii_lz_32,
                                       src, lz, xoff, yoff, e, w, h);
            display_integral(ii_start, ii_w, ii_h, ii_lz_32);

            compute_unsafe_ssd_integral_image(ii_start2, ii_lz_32,
                                              0, 0,
                                              src, lz,
                                              xoff, yoff, e, w, h,
                                              ii_w, ii_h);
            display_integral(ii_start2, ii_w, ii_h, ii_lz_32);

            if (memcmp(ii, ii2, (ii_h+1) * ii_lz_32 * sizeof(*ii))) {
                printf("Integral mismatch\n");
                ret = 1;
                goto end;
            }
        }
    }

end:
    av_freep(&ii);
    av_freep(&ii2);
    return ret;
}
