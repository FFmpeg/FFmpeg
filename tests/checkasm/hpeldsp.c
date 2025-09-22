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

#include "checkasm.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/hpeldsp.h"

#define MAX_BLOCK_SIZE 16
#define MAX_HEIGHT     16
#define MAX_STRIDE     64
// BUF_SIZE is bigger than necessary in order to test strides > block width.
#define BUF_SIZE ((MAX_HEIGHT - 1) * MAX_STRIDE + MAX_BLOCK_SIZE)
// Due to hpel interpolation the input needs to have one more line than
// the output and the last line needs one more element.
// The input is not subject to alignment requirements; making the input buffer
// bigger (by MAX_BLOCK_SIZE - 1) allows us to use a random misalignment.
#define INPUT_BUF_SIZE (MAX_HEIGHT * MAX_STRIDE + MAX_BLOCK_SIZE + 1 + (MAX_BLOCK_SIZE - 1))

#define randomize_buffers(buf0, buf1)                      \
    do {                                                   \
        static_assert(sizeof(buf0) == sizeof(buf1), "Incompatible buffers"); \
        static_assert(!(sizeof(buf0) % 4), "Tail handling needed"); \
        static_assert(sizeof(buf0[0]) == 1 && sizeof(buf1[0]) == 1, \
                      "Pointer arithmetic needs to be adapted"); \
        for (size_t k = 0; k < sizeof(buf0); k += 4) {     \
            uint32_t r = rnd();                            \
            AV_WN32A(buf0 + k, r);                         \
            AV_WN32A(buf1 + k, r);                         \
        }                                                  \
    } while (0)


void checkasm_check_hpeldsp(void)
{
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, srcbuf0)[INPUT_BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, srcbuf1)[INPUT_BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, dstbuf0)[BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, dstbuf1)[BUF_SIZE];
    HpelDSPContext hdsp;
    static const struct {
        const char *name;
        size_t offset;
        unsigned nb_blocksizes;
    } tests[] = {
#define TEST(NAME, NB) { .name = #NAME, .offset = offsetof(HpelDSPContext, NAME), .nb_blocksizes = NB }
        TEST(put_pixels_tab, 4),
        TEST(avg_pixels_tab, 4),
        TEST(put_no_rnd_pixels_tab, 2), // put_no_rnd_pixels_tab only has two usable blocksizes
        TEST(avg_no_rnd_pixels_tab, 1),
    };
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h);

    ff_hpeldsp_init(&hdsp, AV_CODEC_FLAG_BITEXACT);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(tests); ++i) {
        op_pixels_func (*func_tab)[4] = (op_pixels_func (*)[4])((char*)&hdsp + tests[i].offset);
        for (unsigned j = 0; j < tests[i].nb_blocksizes; ++j) {
            const unsigned blocksize = MAX_BLOCK_SIZE >> j;
            // h must always be a multiple of four, except when width is two or four.
            const unsigned h_mult = blocksize <= 4 ? 2 : 4;

            for (unsigned dxy = 0; dxy < 4; ++dxy) {
                if (check_func(func_tab[j][dxy], "%s[%u][%u]", tests[i].name, j, dxy)) {
                    // Don't always use output that is 16-aligned.
                    size_t dst_offset = (rnd() % (MAX_BLOCK_SIZE / blocksize)) * blocksize;
                    size_t src_offset = rnd() % MAX_BLOCK_SIZE;
                    ptrdiff_t stride  = (rnd() % (MAX_STRIDE / blocksize) + 1) * blocksize;
                    int h = (rnd() % (MAX_HEIGHT / h_mult) + 1) * h_mult;
                    const uint8_t *src0 = srcbuf0 + src_offset, *src1 = srcbuf1 + src_offset;
                    uint8_t *dst0 = dstbuf0 + dst_offset, *dst1 = dstbuf1 + dst_offset;

                    if (rnd() & 1) {
                        // Flip stride.
                        dst1  += (h - 1) * stride;
                        dst0  += (h - 1) * stride;
                        // Due to interpolation potentially h + 1 lines are read
                        // from src, hence h * stride.
                        src0  += h * stride;
                        src1  += h * stride;
                        stride = -stride;
                    }

                    randomize_buffers(srcbuf0, srcbuf1);
                    randomize_buffers(dstbuf0, dstbuf1);
                    call_ref(dst0, src0, stride, h);
                    call_new(dst1, src1, stride, h);
                    if (memcmp(srcbuf0, srcbuf1, sizeof(srcbuf0)) || memcmp(dstbuf0, dstbuf1, sizeof(dstbuf0)))
                        fail();
                    bench_new(dst0, src0, stride, h);
                }
            }
        }
    }
}
