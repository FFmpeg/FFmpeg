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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/mpegvideoencdsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        for (int j = 0; j < size; j += 4) \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static void check_pix_sum(MpegvideoEncDSPContext *c)
{
    LOCAL_ALIGNED_16(uint8_t, src, [16 * 16]);

    declare_func(int, const uint8_t *pix, ptrdiff_t line_size);

    randomize_buffers(src, 16 * 16);

    for (int n = 0; n < 2; n++) {
        const char *negstride_str = n ? "_negstride" : "";
        if (check_func(c->pix_sum, "pix_sum%s", negstride_str)) {
            int sum0, sum1;
            const uint8_t *pix = src + (n ? (15 * 16) : 0);
            ptrdiff_t line_size = 16 * (n ? -1 : 1);
            sum0 = call_ref(pix, line_size);
            sum1 = call_new(pix, line_size);
            if (sum0 != sum1)
                fail();
            bench_new(pix, line_size);
        }
    }
}

static void check_pix_norm1(MpegvideoEncDSPContext *c)
{
    LOCAL_ALIGNED_16(uint8_t, src, [16 * 16]);

    declare_func(int, const uint8_t *pix, ptrdiff_t line_size);

    randomize_buffers(src, 16 * 16);

    for (int n = 0; n < 2; n++) {
        const char *negstride_str = n ? "_negstride" : "";
        if (check_func(c->pix_norm1, "pix_norm1%s", negstride_str)) {
            int sum0, sum1;
            const uint8_t *pix = src + (n ? (15 * 16) : 0);
            ptrdiff_t line_size = 16 * (n ? -1 : 1);
            sum0 = call_ref(pix, line_size);
            sum1 = call_new(pix, line_size);
            if (sum0 != sum1)
                fail();
            bench_new(pix, line_size);
        }
    }
}

#define NUM_LINES 4
#define MAX_LINE_SIZE 1920
#define EDGE_WIDTH 16
#define LINESIZE (EDGE_WIDTH + MAX_LINE_SIZE + EDGE_WIDTH)
#define BUFSIZE ((EDGE_WIDTH + NUM_LINES + EDGE_WIDTH) * LINESIZE)

static void check_draw_edges(MpegvideoEncDSPContext *c)
{
    static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE, -MAX_LINE_SIZE};
    LOCAL_ALIGNED_16(uint8_t, buf0, [BUFSIZE]);
    LOCAL_ALIGNED_16(uint8_t, buf1, [BUFSIZE]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *buf, ptrdiff_t wrap, int width, int height,
                                             int w, int h, int sides);

    for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
        int input_size = input_sizes[isi];
        int negstride = input_size < 0;
        const char *negstride_str = negstride ? "_negstride" : "";
        int width = FFABS(input_size);
        ptrdiff_t linesize = EDGE_WIDTH + width + EDGE_WIDTH;
        /* calculate height based on specified width to use the entire buffer. */
        int height = (BUFSIZE / linesize) - (2 * EDGE_WIDTH);
        uint8_t *dst0 = buf0 + EDGE_WIDTH * linesize + EDGE_WIDTH;
        uint8_t *dst1 = buf1 + EDGE_WIDTH * linesize + EDGE_WIDTH;

        if (negstride) {
            dst0 += (height - 1) * linesize;
            dst1 += (height - 1) * linesize;
            linesize *= -1;
        }

        for (int shift = 0; shift < 3; shift++) {
            int edge = EDGE_WIDTH >> shift;
            if (check_func(c->draw_edges, "draw_edges_%d_%d_%d%s", width, height, edge, negstride_str)) {
                randomize_buffers(buf0, BUFSIZE);
                memcpy(buf1, buf0, BUFSIZE);
                call_ref(dst0, linesize, width, height, edge, edge, EDGE_BOTTOM | EDGE_TOP);
                call_new(dst1, linesize, width, height, edge, edge, EDGE_BOTTOM | EDGE_TOP);
                if (memcmp(buf0, buf1, BUFSIZE))
                    fail();
                bench_new(dst1, linesize, width, height, edge, edge, EDGE_BOTTOM | EDGE_TOP);
            }
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE
#undef EDGE_WIDTH
#undef LINESIZE
#undef BUFSIZE

void checkasm_check_mpegvideoencdsp(void)
{
    AVCodecContext avctx = {
        .bits_per_raw_sample = 8,
    };
    MpegvideoEncDSPContext c = { 0 };

    ff_mpegvideoencdsp_init(&c, &avctx);

    check_pix_sum(&c);
    report("pix_sum");
    check_pix_norm1(&c);
    report("pix_norm1");
    check_draw_edges(&c);
    report("draw_edges");
}
