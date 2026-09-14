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

#include <string.h>
#include "checkasm.h"
#include "libavcodec/vvc/dsp.h"
#include "libavcodec/vvc/intra.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };
static const int mip_modes[] = {16, 8, 6};

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define PIXEL_STRIDE (MAX_TB_SIZE * 2)                  // 2 * max transform size
#define BUF_SIZE     (2 * PIXEL_STRIDE * 64)  // Enough for 64x64 with stride=128
// IntraEdgeParams arrays are uint16_t, rounded up as they are filled 4 bytes at a time
#define REF_BYTES    FFALIGN(2 * (6 * MAX_TB_SIZE + 5), 4)
#define REF_OFFSET   (MAX_TB_SIZE + 3)                  // Same as offset from prepare_intra_edge_params

#define randomize_buffers()                                                 \
    do {                                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];                   \
        for (int i = 0; i < BUF_SIZE; i += 4) {                             \
            uint32_t r = rnd() & mask;                                      \
            AV_WN32A(buf0 + i, r);                                          \
            AV_WN32A(buf1 + i, r);                                          \
        }                                                                   \
        /* Make sure reference buffer writes don't go out of bounds */      \
        for (int i = -(REF_OFFSET * SIZEOF_PIXEL);                          \
             i < REF_BYTES - REF_OFFSET * SIZEOF_PIXEL; i += 4) {           \
            AV_WN32A(top + i, rnd() & mask);                                \
            AV_WN32A(left + i, rnd() & mask);                               \
        }                                                                   \
    } while (0)

// From prepare_intra_edge_params in avcodec/vvc/intra_template.c
static int derive_filter_flag(const int mode, const int ref_idx, const int w, const int h)
{
    if (ff_vvc_ref_filter_flag_derive(mode) || ref_idx)
        return 0;
    const int min_dist_ver_hor = FFMIN(abs(mode - 50), abs(mode - 18));
    const int intra_hor_ver_dist_thres[] = {24, 14, 2, 0, 0};
    const int ntbs = (av_log2(w) + av_log2(h)) >> 1;
    return min_dist_ver_hor > intra_hor_ver_dist_thres[ntbs - 2];
}

static void check_pred_dc(VVCDSPContext *c,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, const int w, const int h, const ptrdiff_t stride);

    /* Test all sizes 4x4 up to 64x64, including non-square */
    for (int log2_w = 2; log2_w <= 6; log2_w++) {
        for (int log2_h = 2; log2_h <= 6; log2_h++) {
            int w = 1 << log2_w;
            int h = 1 << log2_h;
            ptrdiff_t stride = FFALIGN((w + rnd() % 64), 32);

            if (check_func(c->intra.pred_dc, "vvc_pred_dc_%dx%d_%d",
                           w, h, bit_depth)) {
                randomize_buffers();
                call_ref(buf0, top, left, w, h, stride);
                call_new(buf1, top, left, w, h, stride);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                if (w == h)
                    bench_new(buf1, top, left, w, h, stride);
            }
        }
    }

    report("pred_dc");
}

static void check_pred_planar(VVCDSPContext *c,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, const int w, const int h, const ptrdiff_t stride);

    /* Test all sizes 4x1 up to 64x64, including non-square. Heights of 1 and 2
     * come from ISP splits and from chroma. */
    for (int log2_w = 2; log2_w <= 6; log2_w++) {
        for (int log2_h = 0; log2_h <= 6; log2_h++) {
            int w = 1 << log2_w;
            int h = 1 << log2_h;
            ptrdiff_t stride = FFALIGN((w + rnd() % 64), 32);

            if (check_func(c->intra.pred_planar, "vvc_pred_planar_%dx%d_%d",
                           w, h, bit_depth)) {
                randomize_buffers();
                call_ref(buf0, top, left, w, h, stride);
                call_new(buf1, top, left, w, h, stride);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                if (w == h)
                    bench_new(buf1, top, left, w, h, stride);
            }
        }
    }

    report("pred_planar");
}

static void check_pred_vh(VVCDSPContext *c,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    declare_func(void, uint8_t *src, const uint8_t *ref,
                 const int w, const int h, const ptrdiff_t stride);

    /* Test all sizes 4x4 up to 64x64, including non-square */
    for (int log2_w = 2; log2_w <= 6; log2_w++) {
        for (int log2_h = 2; log2_h <= 6; log2_h++) {
            int w = 1 << log2_w;
            int h = 1 << log2_h;
            ptrdiff_t stride = FFALIGN((w + rnd() % 64), 32);

            if (check_func(c->intra.pred_v, "vvc_pred_v_%dx%d_%d",
                           w, h, bit_depth)) {
                randomize_buffers();
                call_ref(buf0, top, w, h, stride);
                call_new(buf1, top, w, h, stride);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                if (w == h)
                    bench_new(buf1, top, w, h, stride);
            }

            if (check_func(c->intra.pred_h, "vvc_pred_h_%dx%d_%d",
                           w, h, bit_depth)) {
                randomize_buffers();
                call_ref(buf0, left, w, h, stride);
                call_new(buf1, left, w, h, stride);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                if (w == h)
                    bench_new(buf1, left, w, h, stride);
            }
        }
    }

    report("pred_vh");
}

static void check_pred_mip(VVCDSPContext *c,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, const int w, const int h,
                 const ptrdiff_t stride, int mode_id, int is_transposed);

    /* Test all sizes 4x4 up to 64x64, including non-square */
    for (int log2_w = 2; log2_w <= 6; log2_w++) {
        for (int log2_h = 2; log2_h <= 6; log2_h++) {
            int w = 1 << log2_w;
            int h = 1 << log2_h;
            ptrdiff_t stride = FFALIGN((w + rnd() % 64), 32);
            int size_id = ff_vvc_get_mip_size_id(w, h);

            for (int mode_id = 0; mode_id < mip_modes[size_id]; mode_id++) {
                if (check_func(c->intra.pred_mip, "vvc_pred_mip_%dx%d_mode%d_%d",
                               w, h, mode_id, bit_depth)) {
                    randomize_buffers();
                    call_ref(buf0, top, left, w, h, stride, mode_id, 0);
                    call_new(buf1, top, left, w, h, stride, mode_id, 0);
                    if (memcmp(buf0, buf1, BUF_SIZE))
                        fail();

                    /* Test transposed */
                    randomize_buffers();
                    call_ref(buf0, top, left, w, h, stride, mode_id, 1);
                    call_new(buf1, top, left, w, h, stride, mode_id, 1);
                    if (memcmp(buf0, buf1, BUF_SIZE))
                        fail();

                    if (w == h)
                        bench_new(buf1, top, left, w, h, stride, mode_id, 0);
                }
            }
        }
    }

    report("pred_mip");
}

static void check_pred_angular(VVCDSPContext *c,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    // Set modes to bench
    static const int bench_modes[] = { 2, 10, 26, INTRA_DIAG, 42, 58, INTRA_VDIAG };

    declare_func(void, uint8_t *src, const uint8_t *top, const uint8_t *left,
                 const int w, const int h, const ptrdiff_t stride, const int c_idx,
                 const int mode, const int ref_idx, const int filter_flag,
                 const int need_pdpc);

    /* Test all sizes 4x4 up to 64x64, including non-square */
    for (int log2_w = 2; log2_w <= 6; log2_w++) {
        for (int log2_h = 2; log2_h <= 6; log2_h++) {
            int w = 1 << log2_w;
            int h = 1 << log2_h;
            ptrdiff_t stride = FFALIGN((w + rnd() % 64), 32);

            for (int mode = 2; mode <= INTRA_VDIAG; mode++) {
                // Skip vertical and horizontal (pred_v, pred_h)
                if (mode == INTRA_VERT || mode == INTRA_HORZ)
                    continue;

                int dir = mode >= INTRA_DIAG;
                const char *mode_category;

                if (mode == 2)
                    mode_category = "Hdiag";
                else if (mode > 2 && mode < INTRA_HORZ)
                    mode_category = "Hpos";
                else if (mode > INTRA_HORZ && mode < INTRA_DIAG)
                    mode_category = "Hneg";
                else if (mode == INTRA_DIAG)
                    mode_category = "Diag";
                else if (mode > INTRA_DIAG && mode < INTRA_VERT)
                    mode_category = "Vneg";
                else if (mode > INTRA_VERT && mode < INTRA_VDIAG)
                    mode_category = "Vpos";
                else
                    mode_category = "Vdiag";

                for (int ref_idx = 0; ref_idx <= 2; ref_idx++) {
                    if (check_func(dir ? c->intra.pred_angular_v : c->intra.pred_angular_h, "vvc_pred_angular_%c_%dx%d_%s_mode%d_ref%d_%d",
                                   dir ? 'v' : 'h', w, h, mode_category, mode, ref_idx, bit_depth)) {
                        int filter_flag = derive_filter_flag(mode, ref_idx, w, h);
                        int need_pdpc   = ff_vvc_need_pdpc(w, h, 0, mode, ref_idx);

                        randomize_buffers();
                        call_ref(buf0, top, left, w, h, stride, 0, mode, ref_idx, filter_flag, need_pdpc);
                        call_new(buf1, top, left, w, h, stride, 0, mode, ref_idx, filter_flag, need_pdpc);
                        if (memcmp(buf0, buf1, BUF_SIZE))
                            fail();
                        for (int i = 0; i < FF_ARRAY_ELEMS(bench_modes); i++) {
                            if (mode == bench_modes[i] && w == h && ref_idx == 0)
                                bench_new(buf1, top, left, w, h, stride, 0, mode, ref_idx, filter_flag, need_pdpc);
                        }
                    }
                }
                // TODO: Add c_idx=1 (chroma)
            }
        }
    }

    report("pred_angular");
}

void checkasm_check_vvc_pred(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, top_buf, [REF_BYTES + 16]);
    LOCAL_ALIGNED_32(uint8_t, left_buf, [REF_BYTES + 16]);

    for (int bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        VVCDSPContext c;

        uint8_t *top  = top_buf  + REF_OFFSET * SIZEOF_PIXEL;
        uint8_t *left = left_buf + REF_OFFSET * SIZEOF_PIXEL;

        ff_vvc_dsp_init(&c, bit_depth);
        check_pred_dc(&c, buf0, buf1, top, left, bit_depth);
        check_pred_planar(&c, buf0, buf1, top, left, bit_depth);
        check_pred_vh(&c, buf0, buf1, top, left, bit_depth);
        check_pred_mip(&c, buf0, buf1, top, left, bit_depth);
        check_pred_angular(&c, buf0, buf1, top, left, bit_depth);
    }
}
