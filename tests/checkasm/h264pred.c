/*
 * Copyright (c) 2015 Henrik Gramner
 *
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
#include "libavcodec/avcodec.h"
#include "libavcodec/h264pred.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

static const int codec_ids[4] = { AV_CODEC_ID_H264, AV_CODEC_ID_VP8, AV_CODEC_ID_RV40, AV_CODEC_ID_SVQ3 };

static const char * const pred4x4_modes[4][15] = {
    { /* H264 */
        [VERT_PRED           ] = "vertical",
        [HOR_PRED            ] = "horizontal",
        [DC_PRED             ] = "dc",
        [DIAG_DOWN_LEFT_PRED ] = "down_left",
        [DIAG_DOWN_RIGHT_PRED] = "down_right",
        [VERT_RIGHT_PRED     ] = "vertical_right",
        [HOR_DOWN_PRED       ] = "horizontal_right",
        [VERT_LEFT_PRED      ] = "vertical_left",
        [HOR_UP_PRED         ] = "horizontal_up",
        [LEFT_DC_PRED        ] = "left_dc",
        [TOP_DC_PRED         ] = "top_dc",
        [DC_128_PRED         ] = "dc_128",
    },
    { /* VP8 */
        [VERT_PRED     ] = "vertical_vp8",
        [HOR_PRED      ] = "horizontal_vp8",
        [VERT_LEFT_PRED] = "vertical_left_vp8",
        [TM_VP8_PRED   ] = "tm_vp8",
        [DC_127_PRED   ] = "dc_127_vp8",
        [DC_129_PRED   ] = "dc_129_vp8",
    },
    { /* RV40 */
        [DIAG_DOWN_LEFT_PRED            ] = "down_left_rv40",
        [VERT_LEFT_PRED                 ] = "vertical_left_rv40",
        [HOR_UP_PRED                    ] = "horizontal_up_rv40",
        [DIAG_DOWN_LEFT_PRED_RV40_NODOWN] = "down_left_nodown_rv40",
        [HOR_UP_PRED_RV40_NODOWN        ] = "horizontal_up_nodown_rv40",
        [VERT_LEFT_PRED_RV40_NODOWN     ] = "vertical_left_nodown_rv40",
    },
    { /* SVQ3 */
        [DIAG_DOWN_LEFT_PRED] = "down_left_svq3",
    },
};

static const char * const pred8x8_modes[4][11] = {
    { /* H264 */
        [DC_PRED8x8              ] = "dc",
        [HOR_PRED8x8             ] = "horizontal",
        [VERT_PRED8x8            ] = "vertical",
        [PLANE_PRED8x8           ] = "plane",
        [LEFT_DC_PRED8x8         ] = "left_dc",
        [TOP_DC_PRED8x8          ] = "top_dc",
        [DC_128_PRED8x8          ] = "dc_128",
        [ALZHEIMER_DC_L0T_PRED8x8] = "mad_cow_dc_l0t",
        [ALZHEIMER_DC_0LT_PRED8x8] = "mad_cow_dc_0lt",
        [ALZHEIMER_DC_L00_PRED8x8] = "mad_cow_dc_l00",
        [ALZHEIMER_DC_0L0_PRED8x8] = "mad_cow_dc_0l0",
    },
    { /* VP8 */
        [PLANE_PRED8x8 ] = "tm_vp8",
        [DC_127_PRED8x8] = "dc_127_vp8",
        [DC_129_PRED8x8] = "dc_129_vp8",
    },
    { /* RV40 */
        [DC_PRED8x8     ] = "dc_rv40",
        [LEFT_DC_PRED8x8] = "left_dc_rv40",
        [TOP_DC_PRED8x8 ] = "top_dc_rv40",
    },
    /* nothing for SVQ3 */
};

static const char * const pred16x16_modes[4][9] = {
    { /* H264 */
        [DC_PRED8x8     ] = "dc",
        [HOR_PRED8x8    ] = "horizontal",
        [VERT_PRED8x8   ] = "vertical",
        [PLANE_PRED8x8  ] = "plane",
        [LEFT_DC_PRED8x8] = "left_dc",
        [TOP_DC_PRED8x8 ] = "top_dc",
        [DC_128_PRED8x8 ] = "dc_128",
    },
    { /* VP8 */
        [PLANE_PRED8x8 ] = "tm_vp8",
        [DC_127_PRED8x8] = "dc_127_vp8",
        [DC_129_PRED8x8] = "dc_129_vp8",
    },
    { /* RV40 */
        [PLANE_PRED8x8] = "plane_rv40",
    },
    { /* SVQ3 */
        [PLANE_PRED8x8] = "plane_svq3",
    },
};

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE (3 * 16 * 17)

#define check_pred_func(func, name, mode_name)                                    \
    (mode_name && ((codec_ids[codec] == AV_CODEC_ID_H264) ?                       \
                   check_func(func, "pred%s_%s_%d", name, mode_name, bit_depth) : \
                   check_func(func, "pred%s_%s", name, mode_name)))

#define randomize_buffers()                        \
    do {                                           \
        uint32_t mask = pixel_mask[bit_depth - 8]; \
        int i;                                     \
        for (i = 0; i < BUF_SIZE; i += 4) {        \
            uint32_t r = rnd() & mask;             \
            AV_WN32A(buf0 + i, r);                 \
            AV_WN32A(buf1 + i, r);                 \
        }                                          \
    } while (0)

#define src0 (buf0 + 4 * 16) /* Offset to allow room for top and left */
#define src1 (buf1 + 4 * 16)

static void check_pred4x4(H264PredContext *h, uint8_t *buf0, uint8_t *buf1,
                          int codec, int chroma_format, int bit_depth)
{
    if (chroma_format == 1) {
        uint8_t *topright = buf0 + 2*16;
        int pred_mode;
        declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *src, const uint8_t *topright, ptrdiff_t stride);

        for (pred_mode = 0; pred_mode < 15; pred_mode++) {
            if (check_pred_func(h->pred4x4[pred_mode], "4x4", pred4x4_modes[codec][pred_mode])) {
                randomize_buffers();
                call_ref(src0, topright, 12*SIZEOF_PIXEL);
                call_new(src1, topright, 12*SIZEOF_PIXEL);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                bench_new(src1, topright, 12*SIZEOF_PIXEL);
            }
        }
    }
}

static void check_pred8x8(H264PredContext *h, uint8_t *buf0, uint8_t *buf1,
                          int codec, int chroma_format, int bit_depth)
{
    int pred_mode;
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *src, ptrdiff_t stride);

    for (pred_mode = 0; pred_mode < 11; pred_mode++) {
        if (check_pred_func(h->pred8x8[pred_mode], (chroma_format == 2) ? "8x16" : "8x8",
                            pred8x8_modes[codec][pred_mode])) {
            randomize_buffers();
            call_ref(src0, 24*SIZEOF_PIXEL);
            call_new(src1, 24*SIZEOF_PIXEL);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
            bench_new(src1, 24*SIZEOF_PIXEL);
        }
    }
}

static void check_pred16x16(H264PredContext *h, uint8_t *buf0, uint8_t *buf1,
                            int codec, int chroma_format, int bit_depth)
{
    if (chroma_format == 1) {
        int pred_mode;
        declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *src, ptrdiff_t stride);

        for (pred_mode = 0; pred_mode < 9; pred_mode++) {
            if (check_pred_func(h->pred16x16[pred_mode], "16x16", pred16x16_modes[codec][pred_mode])) {
                randomize_buffers();
                call_ref(src0, 48);
                call_new(src1, 48);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                bench_new(src1, 48);
            }
        }
    }
}

static void check_pred8x8l(H264PredContext *h, uint8_t *buf0, uint8_t *buf1,
                           int codec, int chroma_format, int bit_depth)
{
    if (chroma_format == 1 && codec_ids[codec] == AV_CODEC_ID_H264) {
        int pred_mode;
        declare_func_emms(AV_CPU_FLAG_MMXEXT, void, uint8_t *src, int topleft, int topright, ptrdiff_t stride);

        for (pred_mode = 0; pred_mode < 12; pred_mode++) {
            if (check_pred_func(h->pred8x8l[pred_mode], "8x8l", pred4x4_modes[codec][pred_mode])) {
                int neighbors;
                for (neighbors = 0; neighbors <= 0xc000; neighbors += 0x4000) {
                    int has_topleft  = neighbors & 0x8000;
                    int has_topright = neighbors & 0x4000;

                    if ((pred_mode == DIAG_DOWN_RIGHT_PRED || pred_mode == VERT_RIGHT_PRED) && !has_topleft)
                        continue; /* Those aren't allowed according to the spec */

                    randomize_buffers();
                    call_ref(src0, has_topleft, has_topright, 24*SIZEOF_PIXEL);
                    call_new(src1, has_topleft, has_topright, 24*SIZEOF_PIXEL);
                    if (memcmp(buf0, buf1, BUF_SIZE))
                        fail();
                    bench_new(src1, has_topleft, has_topright, 24*SIZEOF_PIXEL);
                }
            }
        }
    }
}

/* TODO: Add tests for H.264 lossless H/V prediction */

void checkasm_check_h264pred(void)
{
    static const struct {
        void (*func)(H264PredContext*, uint8_t*, uint8_t*, int, int, int);
        const char *name;
    } tests[] = {
        { check_pred4x4,   "pred4x4"   },
        { check_pred8x8,   "pred8x8"   },
        { check_pred16x16, "pred16x16" },
        { check_pred8x8l,  "pred8x8l"  },
    };

    LOCAL_ALIGNED_16(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, buf1, [BUF_SIZE]);
    H264PredContext h;
    int test, codec, chroma_format, bit_depth;

    for (test = 0; test < FF_ARRAY_ELEMS(tests); test++) {
        for (codec = 0; codec < 4; codec++) {
            int codec_id = codec_ids[codec];
            for (bit_depth = 8; bit_depth <= (codec_id == AV_CODEC_ID_H264 ? 10 : 8); bit_depth++)
                for (chroma_format = 1; chroma_format <= (codec_id == AV_CODEC_ID_H264 ? 2 : 1); chroma_format++) {
                    ff_h264_pred_init(&h, codec_id, bit_depth, chroma_format);
                    tests[test].func(&h, buf0, buf1, codec, chroma_format, bit_depth);
                }
        }
        report("%s", tests[test].name);
    }
}
