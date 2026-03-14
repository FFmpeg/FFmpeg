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

#include "checkasm.h"
#include "libavcodec/mpeg4videodsp.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

enum {
    MAX_WIDTH        = 1024,
    MAX_HEIGHT       = 64,
    MAX_STRIDE       = MAX_WIDTH,
    MAX_BLOCK_HEIGHT = 16,
    W                = 8,
};

static_assert(MAX_WIDTH <= MAX_STRIDE, "stride needs to be >= width");

#define randomize_buffer(buf)                                      \
    do {                                                           \
        static_assert(!(sizeof(buf) % 4), "Tail handling needed"); \
        for (size_t k = 0; k < sizeof(buf); k += 4) {              \
            uint32_t r = rnd();                                    \
            AV_WN32A(buf + k, r);                                  \
        }                                                          \
    } while (0)

static int get_signed_rnd(int nb_bits)
{
    int32_t r = rnd();
    return r >> (32 - nb_bits);
}

static int get_mv_delta(int shift, int is_diag)
{
    // The coordinates of the motion vector differences are fixed point numbers
    // whose fractional part has 16+shift bits. We use 5+shift+4 bit mantissa
    // for the deviation from the normal, so that the absolute value corresponds
    // to < 2^(-7). For height 16, the maximum absolute deviation is < 1/8.
    // Additionally, we always use zero for the four least significant bits,
    // as the x86 implementation always falls back to the C one if it is not so.
    return get_signed_rnd(6 + shift) * 16 + (is_diag ? (1 << (16 + shift)) : 0);
}

static int modify_fpel(int coordinate, int size, int block_size, int type)
{
    switch (type) {
    default: av_unreachable("impossible");
    // fallthrough
    case 2: return coordinate; // do nothing
    // modify coordinate so that it requires pixel replication to the left/top
    case 1: return coordinate % block_size - block_size;
    // modify coordinate so that it requires pixel replication to the right/down
    case 0: return coordinate + block_size + (size - (block_size + 1) - coordinate) / block_size * block_size;
    }
}

static void checkasm_check_gmc(const Mpeg4VideoDSPContext *const mdsp)
{
    DECLARE_ALIGNED_8(uint8_t, buf_new)[MAX_BLOCK_HEIGHT * MAX_STRIDE];
    DECLARE_ALIGNED_8(uint8_t, buf_ref)[MAX_BLOCK_HEIGHT * MAX_STRIDE];
    DECLARE_ALIGNED_4(uint8_t, srcbuf)[MAX_STRIDE * MAX_HEIGHT];

    declare_func(void, uint8_t *dst, const uint8_t *src,
                 int stride, int h, int ox, int oy,
                 int dxx, int dxy, int dyx, int dyy,
                 int shift, int r, int width, int height);

    randomize_buffer(srcbuf);
    randomize_buffer(buf_ref);
    memcpy(buf_new, buf_ref, sizeof(buf_new));

    int shift = 1 + rnd() % 4; // range 1..4
    const int h = rnd() & 1 ? 16 : 8;
    const int r = (1 << (2 * shift - 1)) - (rnd() & 1);
    const int width  = FFALIGN(W + rnd() % (MAX_WIDTH - W + 1), 16);  // range 8..MAX_WIDTH
    const int height = FFALIGN(h + rnd() % (MAX_HEIGHT - h + 1), 8); // range h..MAX_HEIGHT
    ptrdiff_t stride = FFALIGN(width + rnd() % (MAX_STRIDE - width + 1), 8);
    const uint8_t *src = srcbuf;
    uint8_t *dst_new = buf_new, *dst_ref = buf_ref;

    if (rnd() & 1) { // negate stride
        dst_new += stride * (h - 1);
        dst_ref += stride * (h - 1);
        src     += stride * (height - 1);
        stride  *= -1;
    }
    // Get the fullpel component of the motion vector.
    // Restrict the range so that a (W+1)x(h+1) buffer fits in srcbuf
    // (if possible) in order to test the non-edge-emulation codepath.
    int fpel_x = width  == W ? 0 : rnd() % (width  - W);
    int fpel_y = height == h ? 0 : rnd() % (height - h);
    int dxx = get_mv_delta(shift, 1), dxy = get_mv_delta(shift, 0);
    int dyx = get_mv_delta(shift, 0), dyy = get_mv_delta(shift, 1);

    int ox  = fpel_x << (16 + shift) | rnd() & ((1 << (16 + shift)) - 1);
    int oy  = fpel_y << (16 + shift) | rnd() & ((1 << (16 + shift)) - 1);

    call_ref(dst_ref, src, stride, h, ox, oy,
             dxx, dxy, dyx, dyy, shift, r, width, height);
    call_new(dst_new, src, stride, h, ox, oy,
             dxx, dxy, dyx, dyy, shift, r, width, height);
    if (memcmp(buf_new, buf_ref, sizeof(buf_new)))
        fail();

    bench_new(dst_new, src, stride, h, ox, oy,
              dxx, dxy, dyx, dyy, shift, r, width, height);

    // Now test the case of src being partially outside of the actual picture.
    if (!check_func(mdsp->gmc, "gmc_edge_emulation"))
        return; // shouldn't happen
    int type = rnd() % 8;
    fpel_x = modify_fpel(fpel_x, width,  8, type % 3);
    fpel_y = modify_fpel(fpel_y, height, h, type / 3);
    ox  = fpel_x * (1 << (16 + shift)) | rnd() & ((1 << (16 + shift)) - 1);
    oy  = fpel_y * (1 << (16 + shift)) | rnd() & ((1 << (16 + shift)) - 1);
    call_ref(dst_ref, src, stride, h, ox, oy,
             dxx, dxy, dyx, dyy, shift, r, width, height);
    call_new(dst_new, src, stride, h, ox, oy,
             dxx, dxy, dyx, dyy, shift, r, width, height);
    if (memcmp(buf_new, buf_ref, sizeof(buf_new)))
        fail();

    bench_new(dst_new, src, stride, h, ox, oy,
              dxx, dxy, dyx, dyy, shift, r, width, height);
}

void checkasm_check_mpeg4videodsp(void)
{
    Mpeg4VideoDSPContext mdsp;

    ff_mpeg4videodsp_init(&mdsp);

    if (check_func(mdsp.gmc, "gmc")) {
        checkasm_check_gmc(&mdsp);
        report("gmc");
    }
}
