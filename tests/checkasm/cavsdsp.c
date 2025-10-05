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
#include "libavcodec/cavsdsp.h"


enum {
// DECLARE_ALIGNED can't handle enum constants.
#define MAX_BLOCK_SIZE 16
    MAX_STRIDE     = 64,
    /// BUF_SIZE is bigger than necessary in order to test strides > block width.
    BUF_SIZE       = ((MAX_BLOCK_SIZE - 1) * MAX_STRIDE + MAX_BLOCK_SIZE),
    /**
     * The qpel interpolation code accesses two lines above and three lines
     * below the actual src block; it also accesses two pixels to the left
     * and three to the right.
     * The input is not subject to alignment requirements; making the input buffer
     * bigger (by MAX_BLOCK_SIZE - 1) allows us to use a random misalignment.
     */
    INPUT_BUF_SIZE = (2 + (2 + MAX_BLOCK_SIZE - 1 + 3) * MAX_STRIDE + MAX_BLOCK_SIZE + 3 + (MAX_BLOCK_SIZE - 1))
};

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


static void check_cavs_qpeldsp(void)
{
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, srcbuf0)[INPUT_BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, srcbuf1)[INPUT_BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, dstbuf0)[BUF_SIZE];
    DECLARE_ALIGNED(MAX_BLOCK_SIZE, uint8_t, dstbuf1)[BUF_SIZE];
    CAVSDSPContext cavsdsp;
    static const struct {
        const char *name;
        size_t offset;
    } tests[] = {
#define TEST(NAME) { .name = #NAME, .offset = offsetof(CAVSDSPContext, NAME) }
        TEST(put_cavs_qpel_pixels_tab),
        TEST(avg_cavs_qpel_pixels_tab),
    };
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

    ff_cavsdsp_init(&cavsdsp);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(tests); ++i) {
        qpel_mc_func (*func_tab)[16] = (qpel_mc_func (*)[16])((char*)&cavsdsp + tests[i].offset);
        for (unsigned j = 0; j < 2; ++j) {
            const unsigned blocksize = MAX_BLOCK_SIZE >> j;

            for (unsigned dxy = 0; dxy < 16; ++dxy) {
                if (check_func(func_tab[j][dxy], "%s[%u][%u]", tests[i].name, j, dxy)) {
                    // Don't always use output that is 16-aligned.
                    size_t dst_offset = (rnd() % (MAX_BLOCK_SIZE / blocksize)) * blocksize;
                    ptrdiff_t stride  = (rnd() % (MAX_STRIDE / blocksize) + 1) * blocksize;
                    size_t src_offset = 2 + 2 * stride + rnd() % MAX_BLOCK_SIZE;
                    const uint8_t *src0 = srcbuf0 + src_offset, *src1 = srcbuf1 + src_offset;
                    uint8_t *dst0 = dstbuf0 + dst_offset, *dst1 = dstbuf1 + dst_offset;

                    if (rnd() & 1) {
                        // Flip stride.
                        dst1  += (blocksize - 1) * stride;
                        dst0  += (blocksize - 1) * stride;
                        // We need two lines above src and three lines below the block,
                        // hence blocksize * stride.
                        src0  += blocksize * stride;
                        src1  += blocksize * stride;
                        stride = -stride;
                    }

                    randomize_buffers(srcbuf0, srcbuf1);
                    randomize_buffers(dstbuf0, dstbuf1);
                    call_ref(dst0, src0, stride);
                    call_new(dst1, src1, stride);
                    if (memcmp(srcbuf0, srcbuf1, sizeof(srcbuf0)) || memcmp(dstbuf0, dstbuf1, sizeof(dstbuf0)))
                        fail();
                    bench_new(dst0, src0, stride);
                }
            }
        }
    }
}

void checkasm_check_cavsdsp(void)
{
    check_cavs_qpeldsp();
}
