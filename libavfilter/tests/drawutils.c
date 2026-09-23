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

#include <stdio.h>

#include "libavutil/pixdesc.h"
#include "libavfilter/drawutils.h"

int main(void)
{
    enum AVPixelFormat f;
    const AVPixFmtDescriptor *desc;
    FFDrawContext draw;
    FFDrawColor color;
    int r, i;

    for (f = 0; av_pix_fmt_desc_get(f); f++) {
        desc = av_pix_fmt_desc_get(f);
        if (!desc->name)
            continue;
        printf("Testing %s...%*s", desc->name,
               (int)(16 - strlen(desc->name)), "");
        r = ff_draw_init(&draw, f, 0);
        if (r < 0) {
            printf("no: %s\n", av_err2str(r));
            continue;
        }
        ff_draw_color(&draw, &color, (uint8_t[]) { 1, 0, 0, 1 });
        for (i = 0; i < sizeof(color); i++)
            if (((uint8_t *)&color)[i] != 128)
                break;
        if (i == sizeof(color)) {
            printf("fallback color\n");
            continue;
        }
        printf("ok\n");
    }

    ff_draw_init(&draw, AV_PIX_FMT_RGBA, FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&draw, &color, (uint8_t[]) { 255, 0, 0, 128 });
    for (int pass = 0; pass < 2; pass++) {
        uint8_t pixel[4] = { 0 };
        uint8_t *dst[4] = { pixel }, mask = 0xFF;
        int linesize[4] = { 4 };

        if (pass)
            ff_blend_rectangle(&draw, &color, dst, linesize, 1, 1, 0, 0, 1, 1);
        else
            ff_blend_mask(&draw, &color, dst, linesize, 1, 1, &mask, 1, 1, 1, 3, 0, 0, 0);
        printf("half transparent red over transparent rgba by %s: %d %d %d %d\n",
               pass ? "rectangle" : "mask", pixel[0], pixel[1], pixel[2], pixel[3]);
    }
    return 0;
}
