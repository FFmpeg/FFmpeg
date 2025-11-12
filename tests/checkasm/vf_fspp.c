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

#include <stddef.h>
#include <stdint.h>

#include "checkasm.h"
#include "libavfilter/vf_fsppdsp.h"
#include "libavcodec/mathops.h"
#include "libavutil/mem_internal.h"

#define randomize_buffers(buf)                           \
    do {                                                 \
        for (size_t j = 0; j < FF_ARRAY_ELEMS(buf); ++j) \
            buf[j] = rnd();                              \
    } while (0)

#define randomize_mask_buffers(buf, buf2, nb_elems, nb_bits)\
    do {                                                    \
        for (size_t j = 0; j < nb_elems; ++j)               \
            buf[j] = buf2[j] = sign_extend(rnd(), nb_bits); \
    } while (0)

#define randomize_buffer_range(buf, min, max)               \
    do {                                                    \
        for (size_t j = 0; j < FF_ARRAY_ELEMS(buf); ++j)    \
            buf[j] = min + rnd() % (max - min + 1);         \
    } while (0)

static void check_store_slice(void)
{
    enum {
        MAX_WIDTH  = 256,
        /// in elements, not in bytes; 32 is arbitrary
        MAX_STRIDE = MAX_WIDTH + 32,
        MAX_HEIGHT = 8,
    };
    FSPPDSPContext fspp;
    ff_fsppdsp_init(&fspp);
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, int16_t *src,
                      ptrdiff_t dst_stride, ptrdiff_t src_stride,
                      ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale);

    for (int i = 0; i < 2; ++i) {
        if (check_func(i ? fspp.store_slice2 : fspp.store_slice, "store_slice%s", i ? "2" : "")) {
            // store slice resets the row eight lines above the current one
            DECLARE_ALIGNED(16, int16_t, src_ref1)[MAX_STRIDE * ( 8 + MAX_HEIGHT - 1) + MAX_WIDTH];
            DECLARE_ALIGNED(16, int16_t, src_new1)[MAX_STRIDE * ( 8 + MAX_HEIGHT - 1) + MAX_WIDTH];
            // store_slice2 resets the row 16 lines below the current one
            DECLARE_ALIGNED(16, int16_t, src_ref2)[MAX_STRIDE * (16 + MAX_HEIGHT - 1) + MAX_WIDTH];
            DECLARE_ALIGNED(16, int16_t, src_new2)[MAX_STRIDE * (16 + MAX_HEIGHT - 1) + MAX_WIDTH];
            uint8_t dstbuf_new[MAX_STRIDE * (MAX_HEIGHT - 1) + MAX_WIDTH], dstbuf_ref[MAX_STRIDE * (MAX_HEIGHT - 1) + MAX_WIDTH];
            uint8_t *dst_new = dstbuf_new, *dst_ref = dstbuf_ref;
            int16_t *src_ref, *src_new, *or_src_ref, *or_src_new;
            ptrdiff_t      width = 1 + rnd() % MAX_WIDTH;
            ptrdiff_t src_stride = FFALIGN(width + 1 + rnd() % (MAX_STRIDE - MAX_WIDTH), 8);
            ptrdiff_t dst_stride = FFALIGN(width + 1 + rnd() % (MAX_STRIDE - MAX_WIDTH), 8);
            ptrdiff_t height = 1 + rnd() % 8;
            size_t nb_elems;

            if (i) {
                src_ref      = src_ref2;
                src_new      = src_new2;
                or_src_ref   = src_ref2;
                or_src_new   = src_new2;
                nb_elems     = FF_ARRAY_ELEMS(src_ref2);
            } else {
                src_ref      = src_ref1 + 8 * src_stride;
                src_new      = src_new1 + 8 * src_stride;
                or_src_ref   = src_ref1;
                or_src_new   = src_new1;
                nb_elems     = FF_ARRAY_ELEMS(src_ref1);
            }
            if (rnd() & 1) {
                dst_ref    += dst_stride * (height - 1);
                dst_new    += dst_stride * (height - 1);
                dst_stride *= -1;
            }
            randomize_buffers(dstbuf_new);
            memcpy(dstbuf_ref, dstbuf_new, sizeof(dstbuf_ref));
            randomize_mask_buffers(or_src_ref, or_src_new, nb_elems, 14);

            ptrdiff_t log2_scale = rnd() & 1;
            call_ref(dst_ref, src_ref, dst_stride, src_stride, width, height, log2_scale);
            call_new(dst_new, src_new, dst_stride, src_stride, width, height, log2_scale);
            if (memcmp(dstbuf_new, dstbuf_ref, sizeof(dstbuf_ref)) ||
                memcmp(or_src_ref, or_src_new, sizeof(*or_src_new) * nb_elems))
                fail();
            // don't use random parameters for benchmarks
            src_ref = or_src_ref + !i * 8 * MAX_STRIDE;
            bench_new(dstbuf_new, src_ref,
                      MAX_STRIDE, MAX_STRIDE, MAX_WIDTH, 8, 1);
        }
    }
}

static void check_mul_thrmat(void)
{
    FSPPDSPContext fspp;
    DECLARE_ALIGNED(16, int16_t, src)[64];
    DECLARE_ALIGNED(16, int16_t, dst_ref)[64];
    DECLARE_ALIGNED(16, int16_t, dst_new)[64];
    const int q = (uint8_t)rnd();
    declare_func(void, const int16_t *thr_adr_noq, int16_t *thr_adr, int q);

    ff_fsppdsp_init(&fspp);

    if (check_func(fspp.mul_thrmat, "mul_thrmat")) {
        randomize_buffers(src);
        call_ref(src, dst_ref, q);
        call_new(src, dst_new, q);
        if (memcmp(dst_ref, dst_new, sizeof(dst_ref)))
            fail();
        bench_new(src, dst_new, q);
    }
}

static void check_column_fidct(void)
{
    enum {
        NB_BLOCKS = 8, ///< arbitrary
    };
    FSPPDSPContext fspp;
    declare_func(void, const int16_t *thr_adr, const int16_t *data,
                       int16_t *output, int cnt);

    ff_fsppdsp_init(&fspp);

    if (check_func(fspp.column_fidct, "column_fidct")) {
        DECLARE_ALIGNED(16, int16_t, threshold)[64];
        DECLARE_ALIGNED(16, int16_t, src)[8*(8*NB_BLOCKS + 6)];
        DECLARE_ALIGNED(16, int16_t, dst_new)[8*(8*NB_BLOCKS + 6)];
        DECLARE_ALIGNED(16, int16_t, dst_ref)[8*(8*NB_BLOCKS + 6)];

        randomize_buffer_range(threshold, 0, INT16_MAX);
        randomize_buffer_range(src, -1284, 1284);
        randomize_buffers(dst_new);
        memcpy(dst_ref, dst_new, sizeof(dst_ref));

        call_ref(threshold, src, dst_ref, NB_BLOCKS * 8);
        call_new(threshold, src, dst_new, NB_BLOCKS * 8);

        if (memcmp(dst_new, dst_ref, sizeof(dst_new)))
            fail();

        bench_new(threshold, src, dst_new, NB_BLOCKS * 8);
    }
}

void checkasm_check_vf_fspp(void)
{
    check_store_slice();
    check_mul_thrmat();
    check_column_fidct();
}
