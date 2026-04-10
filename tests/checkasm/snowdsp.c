/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/snow.h"
#include "libavcodec/snow_dwt.h"

#include "checkasm.h"

#define randomize_buffer(buf)                                 \
do {                                                          \
    for (size_t k = 0; k < (sizeof(buf) & ~3); k += 4)        \
        AV_WN32A((char*)buf + k, rnd());                      \
    for (size_t k = sizeof(buf) & ~3; k < sizeof(buf); ++k)   \
        ((char*)buf)[k] = rnd();                              \
} while (0)

static void checkasm_check_inner_add_yblock(const SnowDWTContext *const snowdsp)
{
    enum {
        LOG2_MAX_BLOCKSIZE = 4,
        MAX_BLOCKSIZE      = 1 << LOG2_MAX_BLOCKSIZE,
        LOG2_MIN_BLOCKSIZE = 1,
        MAX_STRIDE         = 256,
    };
    declare_func_emms(AV_CPU_FLAG_MMX, void, const uint8_t *obmc, const int obmc_stride,
                       uint8_t **block, int b_w, int b_h, int src_x,
                       int src_stride, IDWTELEM * const *lines,
                       int add, uint8_t *dst8);
    DECLARE_ALIGNED(16, uint8_t, dst8_ref)[MAX_STRIDE * MAX_BLOCKSIZE];
    DECLARE_ALIGNED(16, uint8_t, dst8_new)[MAX_STRIDE * MAX_BLOCKSIZE];
    DECLARE_ALIGNED(16, uint8_t, block)[4][MAX_STRIDE * (MAX_BLOCKSIZE - 1) + MAX_BLOCKSIZE];
    DECLARE_ALIGNED(16, IDWTELEM, linebuf)[MAX_BLOCKSIZE][MAX_STRIDE];
    int inited = 0;

    for (int i = 0; i < 2; ++i) {
        for (int j = LOG2_MIN_BLOCKSIZE; j <= LOG2_MAX_BLOCKSIZE; ++j) {
            int b_w = 1 << j;

            if (!check_func(snowdsp->inner_add_yblock, "inner_add_yblock_%d%s", b_w, i ? "_border" : ""))
                continue;

            const uint8_t *obmc = ff_obmc_tab[LOG2_MAX_BLOCKSIZE - j];
            int obmc_stride = 2 << j;
            int b_h = b_w, mb_x;
            int width = 1 + rnd() % MAX_STRIDE;

            if (!i) {
                // Test the ordinary case of a complete block.
                width = FFMAX(width, 2 * b_w);
                int nb_complete_blocks = (width - b_w / 2) / b_w;
                mb_x = 1 + rnd() % nb_complete_blocks;
            } else {
                // Test a boundary block. If width == b_w/2 mod b_w,
                // there is no right boundary block, so use the left one.
                mb_x = (width + b_w/2) % b_w && rnd() & 1 ? (width + b_w/2) / b_w : 0;
            }
            ptrdiff_t src_stride = FFALIGN(width + rnd() % (MAX_STRIDE - width + 1), 16);
            int src_x = b_w*mb_x - b_w/2;

            if (src_x < 0) {
                obmc -= src_x;
                b_w  += src_x;
                src_x = 0;
            }
            if (src_x + b_w > width) {
                b_w = width - src_x;
            }

            uint8_t *dst8p_ref = dst8_ref + src_x;
            uint8_t *dst8p_new = dst8_new + src_x;
            unsigned rand = rnd();
            uint8_t *blocks[4] = { block[rand % 4],      block[rand / 4  % 4],
                                   block[rand / 16 % 4], block[rand / 64 % 4] };
            if (rnd() & 1) { // negate stride
                dst8p_ref += (b_h - 1) * src_stride;
                dst8p_new += (b_h - 1) * src_stride;
                blocks[0] += (b_h - 1) * src_stride;
                blocks[1] += (b_h - 1) * src_stride;
                blocks[2] += (b_h - 1) * src_stride;
                blocks[3] += (b_h - 1) * src_stride;
                src_stride = -src_stride;
            }
            uint8_t *blocks_backup[4] = { blocks[0], blocks[1],
                                          blocks[2], blocks[3] };
            IDWTELEM *lines[MAX_BLOCKSIZE];

            for (int k = 0; k < b_h; ++k)
                lines[k] = linebuf[rnd() % MAX_BLOCKSIZE];

            if (!inited) {
                inited = 1;
                randomize_buffer(block);
                randomize_buffer(dst8_ref);
                for (size_t k = 0; k < FF_ARRAY_ELEMS(linebuf); ++k) {
                    for (size_t l = 0; l < FF_ARRAY_ELEMS(linebuf[0]); ++l)
                        linebuf[k][l] = sign_extend(rnd(), 15);
                }
            }

            memcpy(dst8_new, dst8_ref, sizeof(dst8_new));

            call_ref(obmc, obmc_stride, blocks, b_w, b_h, src_x, src_stride, lines, 1, dst8p_ref);
            memcpy(blocks, blocks_backup, sizeof(blocks));\
            call_new(obmc, obmc_stride, blocks, b_w, b_h, src_x, src_stride, lines, 1, dst8p_new);

            if (memcmp(dst8_ref, dst8_new, sizeof(dst8_new)))
                fail();

#undef CALL4
#define CALL4(...)\
    do {\
        memcpy(blocks, blocks_backup, sizeof(blocks));\
        tfunc(__VA_ARGS__); \
        memcpy(blocks, blocks_backup, sizeof(blocks));\
        tfunc(__VA_ARGS__); \
        memcpy(blocks, blocks_backup, sizeof(blocks));\
        tfunc(__VA_ARGS__); \
        memcpy(blocks, blocks_backup, sizeof(blocks));\
        tfunc(__VA_ARGS__); \
    } while (0)

            bench_new(obmc, obmc_stride, blocks, b_w, b_h, src_x, src_stride, lines, 1, dst8p_new);
        }
    }
    report("inner_add_yblock");
}

void checkasm_check_snowdsp(void)
{
    SnowDWTContext snowdsp;

    ff_dwt_init(&snowdsp);

    checkasm_check_inner_add_yblock(&snowdsp);
}
