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

#include "libavutil/imgutils.c"

#undef printf

int main(void)
{
    const AVPixFmtDescriptor *desc = NULL;
    int64_t x, y;

    for (y = -1; y<UINT_MAX; y+= y/2 + 1) {
        for (x = -1; x<UINT_MAX; x+= x/2 + 1) {
            int ret = av_image_check_size(x, y, 0, NULL);
            printf("%d", ret >= 0);
        }
        printf("\n");
    }
    printf("\n");

    while (desc = av_pix_fmt_desc_next(desc)) {
        uint8_t *data[4];
        size_t sizes[4];
        ptrdiff_t linesizes1[4], offsets[3] = { 0 };
        int i, total_size, w = 64, h = 48, linesizes[4];
        enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(desc);

        if (av_image_fill_linesizes(linesizes, pix_fmt, w) < 0)
            continue;
        for (i = 0; i < 4; i++)
            linesizes1[i] = linesizes[i];
        if (av_image_fill_plane_sizes(sizes, pix_fmt, h, linesizes1) < 0)
            continue;
        total_size = av_image_fill_pointers(data, pix_fmt, h, (void *)1, linesizes);
        if (total_size < 0)
            continue;
        printf("%-16s", desc->name);
        for (i = 0; i < 4 && data[i]; i++);
        printf("planes: %d", i);
        // Test the output of av_image_fill_linesizes()
        printf(", linesizes:");
        for (i = 0; i < 4; i++)
            printf(" %3d", linesizes[i]);
        // Test the output of av_image_fill_plane_sizes()
        printf(", plane_sizes:");
        for (i = 0; i < 4; i++)
            printf(" %5"SIZE_SPECIFIER, sizes[i]);
        // Test the output of av_image_fill_pointers()
        for (i = 0; i < 3 && data[i + 1]; i++)
            offsets[i] = data[i + 1] - data[i];
        printf(", plane_offsets:");
        for (i = 0; i < 3; i++)
            printf(" %5"PTRDIFF_SPECIFIER, offsets[i]);
        printf(", total_size: %d\n", total_size);
    }

    return 0;
}
