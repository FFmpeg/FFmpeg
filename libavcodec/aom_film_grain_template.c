/*
 * AOM film grain synthesis
 * Copyright (c) 2023 Niklas Haas <ffmpeg@haasn.xyz>
 *
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

/*
 * Copyright © 2018, Niklas Haas
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bit_depth_template.c"

#undef entry
#undef bitdepth
#undef bitdepth_max
#undef HBD_DECL
#undef HBD_CALL
#undef SCALING_SIZE

#if BIT_DEPTH > 8
# define entry int16_t
# define bitdepth_max ((1 << bitdepth) - 1)
# define HBD_DECL , const int bitdepth
# define HBD_CALL , bitdepth
# define SCALING_SIZE 4096
#else
# define entry int8_t
# define bitdepth 8
# define bitdepth_max UINT8_MAX
# define HBD_DECL
# define HBD_CALL
# define SCALING_SIZE 256
#endif

static void FUNC(generate_grain_y_c)(entry buf[][GRAIN_WIDTH],
                                     const AVFilmGrainParams *const params
                                     HBD_DECL)
{
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const int bitdepth_min_8 = bitdepth - 8;
    unsigned seed = params->seed;
    const int shift = 4 - bitdepth_min_8 + data->grain_scale_shift;
    const int grain_ctr = 128 << bitdepth_min_8;
    const int grain_min = -grain_ctr, grain_max = grain_ctr - 1;

    const int ar_pad = 3;
    const int ar_lag = data->ar_coeff_lag;

    for (int y = 0; y < GRAIN_HEIGHT; y++) {
        for (int x = 0; x < GRAIN_WIDTH; x++) {
            const int value = get_random_number(11, &seed);
            buf[y][x] = round2(gaussian_sequence[ value ], shift);
        }
    }

    for (int y = ar_pad; y < GRAIN_HEIGHT; y++) {
        for (int x = ar_pad; x < GRAIN_WIDTH - ar_pad; x++) {
            const int8_t *coeff = data->ar_coeffs_y;
            int sum = 0, grain;
            for (int dy = -ar_lag; dy <= 0; dy++) {
                for (int dx = -ar_lag; dx <= ar_lag; dx++) {
                    if (!dx && !dy)
                        break;
                    sum += *(coeff++) * buf[y + dy][x + dx];
                }
            }

            grain = buf[y][x] + round2(sum, data->ar_coeff_shift);
            buf[y][x] = av_clip(grain, grain_min, grain_max);
        }
    }
}

static void
FUNC(generate_grain_uv_c)(entry buf[][GRAIN_WIDTH],
                          const entry buf_y[][GRAIN_WIDTH],
                          const AVFilmGrainParams *const params, const intptr_t uv,
                          const int subx, const int suby HBD_DECL)
{
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const int bitdepth_min_8 = bitdepth - 8;
    unsigned seed = params->seed ^ (uv ? 0x49d8 : 0xb524);
    const int shift = 4 - bitdepth_min_8 + data->grain_scale_shift;
    const int grain_ctr = 128 << bitdepth_min_8;
    const int grain_min = -grain_ctr, grain_max = grain_ctr - 1;

    const int chromaW = subx ? SUB_GRAIN_WIDTH  : GRAIN_WIDTH;
    const int chromaH = suby ? SUB_GRAIN_HEIGHT : GRAIN_HEIGHT;

    const int ar_pad = 3;
    const int ar_lag = data->ar_coeff_lag;

    for (int y = 0; y < chromaH; y++) {
        for (int x = 0; x < chromaW; x++) {
            const int value = get_random_number(11, &seed);
            buf[y][x] = round2(gaussian_sequence[ value ], shift);
        }
    }

    for (int y = ar_pad; y < chromaH; y++) {
        for (int x = ar_pad; x < chromaW - ar_pad; x++) {
            const int8_t *coeff = data->ar_coeffs_uv[uv];
            int sum = 0, grain;
            for (int dy = -ar_lag; dy <= 0; dy++) {
                for (int dx = -ar_lag; dx <= ar_lag; dx++) {
                    // For the final (current) pixel, we need to add in the
                    // contribution from the luma grain texture
                    if (!dx && !dy) {
                        const int lumaX = ((x - ar_pad) << subx) + ar_pad;
                        const int lumaY = ((y - ar_pad) << suby) + ar_pad;
                        int luma = 0;
                        if (!data->num_y_points)
                            break;
                        for (int i = 0; i <= suby; i++) {
                            for (int j = 0; j <= subx; j++) {
                                luma += buf_y[lumaY + i][lumaX + j];
                            }
                        }
                        luma = round2(luma, subx + suby);
                        sum += luma * (*coeff);
                        break;
                    }

                    sum += *(coeff++) * buf[y + dy][x + dx];
                }
            }

            grain = buf[y][x] + round2(sum, data->ar_coeff_shift);
            buf[y][x] = av_clip(grain, grain_min, grain_max);
        }
    }
}

// samples from the correct block of a grain LUT, while taking into account the
// offsets provided by the offsets cache
static inline entry FUNC(sample_lut)(const entry grain_lut[][GRAIN_WIDTH],
                                     const int offsets[2][2],
                                     const int subx, const int suby,
                                     const int bx, const int by,
                                     const int x, const int y)
{
    const int randval = offsets[bx][by];
    const int offx = 3 + (2 >> subx) * (3 + (randval >> 4));
    const int offy = 3 + (2 >> suby) * (3 + (randval & 0xF));
    return grain_lut[offy + y + (FG_BLOCK_SIZE >> suby) * by]
                    [offx + x + (FG_BLOCK_SIZE >> subx) * bx];
}

static void FUNC(fgy_32x32xn_c)(pixel *const dst_row, const pixel *const src_row,
                                const ptrdiff_t stride,
                                const AVFilmGrainParams *const params, const size_t pw,
                                const uint8_t scaling[SCALING_SIZE],
                                const entry grain_lut[][GRAIN_WIDTH],
                                const int bh, const int row_num HBD_DECL)
{
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const int rows = 1 + (data->overlap_flag && row_num > 0);
    const int bitdepth_min_8 = bitdepth - 8;
    const int grain_ctr = 128 << bitdepth_min_8;
    const int grain_min = -grain_ctr, grain_max = grain_ctr - 1;
    unsigned seed[2];
    int offsets[2 /* col offset */][2 /* row offset */];

    int min_value, max_value;
    if (data->limit_output_range) {
        min_value = 16 << bitdepth_min_8;
        max_value = 235 << bitdepth_min_8;
    } else {
        min_value = 0;
        max_value = bitdepth_max;
    }

    // seed[0] contains the current row, seed[1] contains the previous
    for (int i = 0; i < rows; i++) {
        seed[i] = params->seed;
        seed[i] ^= (((row_num - i) * 37  + 178) & 0xFF) << 8;
        seed[i] ^= (((row_num - i) * 173 + 105) & 0xFF);
    }

    av_assert1(stride % (FG_BLOCK_SIZE * sizeof(pixel)) == 0);

    // process this row in FG_BLOCK_SIZE^2 blocks
    for (unsigned bx = 0; bx < pw; bx += FG_BLOCK_SIZE) {
        const int bw = FFMIN(FG_BLOCK_SIZE, (int) pw - bx);
        const pixel *src;
        pixel *dst;
        int noise;

        // x/y block offsets to compensate for overlapped regions
        const int ystart = data->overlap_flag && row_num ? FFMIN(2, bh) : 0;
        const int xstart = data->overlap_flag && bx      ? FFMIN(2, bw) : 0;

        static const int w[2][2] = { { 27, 17 }, { 17, 27 } };

        if (data->overlap_flag && bx) {
            // shift previous offsets left
            for (int i = 0; i < rows; i++)
                offsets[1][i] = offsets[0][i];
        }

        // update current offsets
        for (int i = 0; i < rows; i++)
            offsets[0][i] = get_random_number(8, &seed[i]);

#define add_noise_y(x, y, grain)                                                \
        src = (const pixel*)((const char*)src_row + (y) * stride) + (x) + bx;   \
        dst = (pixel*)((char*)dst_row + (y) * stride) + (x) + bx;               \
        noise = round2(scaling[ *src ] * (grain), data->scaling_shift);         \
        *dst = av_clip(*src + noise, min_value, max_value);

        for (int y = ystart; y < bh; y++) {
            // Non-overlapped image region (straightforward)
            for (int x = xstart; x < bw; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 0, x, y);
                add_noise_y(x, y, grain);
            }

            // Special case for overlapped column
            for (int x = 0; x < xstart; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 0, x, y);
                int old   = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 1, 0, x, y);
                grain = round2(old * w[x][0] + grain * w[x][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_y(x, y, grain);
            }
        }

        for (int y = 0; y < ystart; y++) {
            // Special case for overlapped row (sans corner)
            for (int x = xstart; x < bw; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 0, x, y);
                int old   = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 1, x, y);
                grain = round2(old * w[y][0] + grain * w[y][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_y(x, y, grain);
            }

            // Special case for doubly-overlapped corner
            for (int x = 0; x < xstart; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 0, x, y);
                int top = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 0, 1, x, y);
                int old = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 1, 1, x, y);

                // Blend the top pixel with the top left block
                top = round2(old * w[x][0] + top * w[x][1], 5);
                top = av_clip(top, grain_min, grain_max);

                // Blend the current pixel with the left block
                old = FUNC(sample_lut)(grain_lut, offsets, 0, 0, 1, 0, x, y);
                grain = round2(old * w[x][0] + grain * w[x][1], 5);
                grain = av_clip(grain, grain_min, grain_max);

                // Mix the row rows together and apply grain
                grain = round2(top * w[y][0] + grain * w[y][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_y(x, y, grain);
            }
        }
    }
}

static void
FUNC(fguv_32x32xn_c)(pixel *const dst_row, const pixel *const src_row,
                     const ptrdiff_t stride, const AVFilmGrainParams *const params,
                     const size_t pw, const uint8_t scaling[SCALING_SIZE],
                     const entry grain_lut[][GRAIN_WIDTH], const int bh,
                     const int row_num, const pixel *const luma_row,
                     const ptrdiff_t luma_stride, const int uv, const int is_id,
                     const int sx, const int sy HBD_DECL)
{
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const int rows = 1 + (data->overlap_flag && row_num > 0);
    const int bitdepth_min_8 = bitdepth - 8;
    const int grain_ctr = 128 << bitdepth_min_8;
    const int grain_min = -grain_ctr, grain_max = grain_ctr - 1;
    unsigned seed[2];
    int offsets[2 /* col offset */][2 /* row offset */];

    int min_value, max_value;
    if (data->limit_output_range) {
        min_value = 16 << bitdepth_min_8;
        max_value = (is_id ? 235 : 240) << bitdepth_min_8;
    } else {
        min_value = 0;
        max_value = bitdepth_max;
    }

    // seed[0] contains the current row, seed[1] contains the previous
    for (int i = 0; i < rows; i++) {
        seed[i] = params->seed;
        seed[i] ^= (((row_num - i) * 37  + 178) & 0xFF) << 8;
        seed[i] ^= (((row_num - i) * 173 + 105) & 0xFF);
    }

    av_assert1(stride % (FG_BLOCK_SIZE * sizeof(pixel)) == 0);

    // process this row in FG_BLOCK_SIZE^2 blocks (subsampled)
    for (unsigned bx = 0; bx < pw; bx += FG_BLOCK_SIZE >> sx) {
        const int bw = FFMIN(FG_BLOCK_SIZE >> sx, (int)(pw - bx));
        int val, lx, ly, noise;
        const pixel *src, *luma;
        pixel *dst, avg;

        // x/y block offsets to compensate for overlapped regions
        const int ystart = data->overlap_flag && row_num ? FFMIN(2 >> sy, bh) : 0;
        const int xstart = data->overlap_flag && bx      ? FFMIN(2 >> sx, bw) : 0;

        static const int w[2 /* sub */][2 /* off */][2] = {
            { { 27, 17 }, { 17, 27 } },
            { { 23, 22 } },
        };

        if (data->overlap_flag && bx) {
            // shift previous offsets left
            for (int i = 0; i < rows; i++)
                offsets[1][i] = offsets[0][i];
        }

        // update current offsets
        for (int i = 0; i < rows; i++)
            offsets[0][i] = get_random_number(8, &seed[i]);

#define add_noise_uv(x, y, grain)                                               \
            lx = (bx + x) << sx;                                                \
            ly = y << sy;                                                       \
            luma = (const pixel*)((const char*)luma_row + ly * luma_stride) + lx;\
            avg = luma[0];                                                      \
            if (sx)                                                             \
                avg = (avg + luma[1] + 1) >> 1;                                 \
            src = (const pixel*)((const char *)src_row + (y) * stride) + bx + (x);\
            dst = (pixel *) ((char *) dst_row + (y) * stride) + bx + (x);       \
            val = avg;                                                          \
            if (!data->chroma_scaling_from_luma) {                              \
                const int combined = avg * data->uv_mult_luma[uv] +             \
                                    *src * data->uv_mult[uv];                   \
                val = av_clip( (combined >> 6) +                                \
                               (data->uv_offset[uv] * (1 << bitdepth_min_8)),   \
                               0, bitdepth_max );                               \
            }                                                                   \
            noise = round2(scaling[ val ] * (grain), data->scaling_shift);      \
            *dst = av_clip(*src + noise, min_value, max_value);

        for (int y = ystart; y < bh; y++) {
            // Non-overlapped image region (straightforward)
            for (int x = xstart; x < bw; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 0, x, y);
                add_noise_uv(x, y, grain);
            }

            // Special case for overlapped column
            for (int x = 0; x < xstart; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 0, x, y);
                int old   = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 1, 0, x, y);
                grain = round2(old * w[sx][x][0] + grain * w[sx][x][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_uv(x, y, grain);
            }
        }

        for (int y = 0; y < ystart; y++) {
            // Special case for overlapped row (sans corner)
            for (int x = xstart; x < bw; x++) {
                int grain = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 0, x, y);
                int old   = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 1, x, y);
                grain = round2(old * w[sy][y][0] + grain * w[sy][y][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_uv(x, y, grain);
            }

            // Special case for doubly-overlapped corner
            for (int x = 0; x < xstart; x++) {
                int top = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 1, x, y);
                int old = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 1, 1, x, y);
                int grain = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 0, 0, x, y);

                // Blend the top pixel with the top left block
                top = round2(old * w[sx][x][0] + top * w[sx][x][1], 5);
                top = av_clip(top, grain_min, grain_max);

                // Blend the current pixel with the left block
                old = FUNC(sample_lut)(grain_lut, offsets, sx, sy, 1, 0, x, y);
                grain = round2(old * w[sx][x][0] + grain * w[sx][x][1], 5);
                grain = av_clip(grain, grain_min, grain_max);

                // Mix the row rows together and apply to image
                grain = round2(top * w[sy][y][0] + grain * w[sy][y][1], 5);
                grain = av_clip(grain, grain_min, grain_max);
                add_noise_uv(x, y, grain);
            }
        }
    }
}

static void FUNC(generate_scaling)(const uint8_t points[][2], const int num,
                                   uint8_t scaling[SCALING_SIZE] HBD_DECL)
{
    const int shift_x = bitdepth - 8;
    const int scaling_size = 1 << bitdepth;
    const int max_value = points[num - 1][0] << shift_x;
    av_assert0(scaling_size <= SCALING_SIZE);

    if (num == 0) {
        memset(scaling, 0, scaling_size);
        return;
    }

    // Fill up the preceding entries with the initial value
    memset(scaling, points[0][1], points[0][0] << shift_x);

    // Linearly interpolate the values in the middle
    for (int i = 0; i < num - 1; i++) {
        const int bx = points[i][0];
        const int by = points[i][1];
        const int ex = points[i+1][0];
        const int ey = points[i+1][1];
        const int dx = ex - bx;
        const int dy = ey - by;
        const int delta = dy * ((0x10000 + (dx >> 1)) / dx);
        av_assert1(dx > 0);
        for (int x = 0, d = 0x8000; x < dx; x++) {
            scaling[(bx + x) << shift_x] = by + (d >> 16);
            d += delta;
        }
    }

    // Fill up the remaining entries with the final value
    memset(&scaling[max_value], points[num - 1][1], scaling_size - max_value);

#if BIT_DEPTH != 8
    for (int i = 0; i < num - 1; i++) {
        const int pad = 1 << shift_x, rnd = pad >> 1;
        const int bx = points[i][0] << shift_x;
        const int ex = points[i+1][0] << shift_x;
        const int dx = ex - bx;
        for (int x = 0; x < dx; x += pad) {
            const int range = scaling[bx + x + pad] - scaling[bx + x];
            for (int n = 1, r = rnd; n < pad; n++) {
                r += range;
                scaling[bx + x + n] = scaling[bx + x] + (r >> shift_x);
            }
        }
    }
#endif
}

static av_always_inline void
FUNC(apply_grain_row)(AVFrame *out, const AVFrame *in,
                      const int ss_x, const int ss_y,
                      const uint8_t scaling[3][SCALING_SIZE],
                      const entry grain_lut[3][GRAIN_HEIGHT+1][GRAIN_WIDTH],
                      const AVFilmGrainParams *params,
                      const int row HBD_DECL)
{
    // Synthesize grain for the affected planes
    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const int cpw = (out->width + ss_x) >> ss_x;
    const int is_id = out->colorspace == AVCOL_SPC_RGB;
    const int bh = (FFMIN(out->height - row * FG_BLOCK_SIZE, FG_BLOCK_SIZE) + ss_y) >> ss_y;
    const ptrdiff_t uv_off = row * FG_BLOCK_SIZE * out->linesize[1] >> ss_y;
    pixel *const luma_src = (pixel *)
        ((char *) in->data[0] + row * FG_BLOCK_SIZE * in->linesize[0]);

    if (data->num_y_points) {
        const int bh = FFMIN(out->height - row * FG_BLOCK_SIZE, FG_BLOCK_SIZE);
        const ptrdiff_t off = row * FG_BLOCK_SIZE * out->linesize[0];
        FUNC(fgy_32x32xn_c)((pixel *) ((char *) out->data[0] + off), luma_src,
                            out->linesize[0], params, out->width, scaling[0],
                            grain_lut[0], bh, row HBD_CALL);
    }

    if (!data->num_uv_points[0] && !data->num_uv_points[1] &&
        !data->chroma_scaling_from_luma)
    {
        return;
    }

    // extend padding pixels
    if (out->width & ss_x) {
        pixel *ptr = luma_src;
        for (int y = 0; y < bh; y++) {
            ptr[out->width] = ptr[out->width - 1];
            ptr = (pixel *) ((char *) ptr + (in->linesize[0] << ss_y));
        }
    }

    if (data->chroma_scaling_from_luma) {
        for (int pl = 0; pl < 2; pl++)
            FUNC(fguv_32x32xn_c)((pixel *) ((char *) out->data[1 + pl] + uv_off),
                                 (const pixel *) ((const char *) in->data[1 + pl] + uv_off),
                                 in->linesize[1], params, cpw, scaling[0],
                                 grain_lut[1 + pl], bh, row, luma_src,
                                 in->linesize[0], pl, is_id, ss_x, ss_y HBD_CALL);
    } else {
        for (int pl = 0; pl < 2; pl++) {
            if (data->num_uv_points[pl]) {
                FUNC(fguv_32x32xn_c)((pixel *) ((char *) out->data[1 + pl] + uv_off),
                                     (const pixel *) ((const char *) in->data[1 + pl] + uv_off),
                                     in->linesize[1], params, cpw, scaling[1 + pl],
                                     grain_lut[1 + pl], bh, row, luma_src,
                                     in->linesize[0], pl, is_id, ss_x, ss_y HBD_CALL);
            }
        }
    }
}

static int FUNC(apply_film_grain)(AVFrame *out_frame, const AVFrame *in_frame,
                                  const AVFilmGrainParams *params HBD_DECL)
{
    entry grain_lut[3][GRAIN_HEIGHT + 1][GRAIN_WIDTH];
    uint8_t scaling[3][SCALING_SIZE];

    const AVFilmGrainAOMParams *const data = &params->codec.aom;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(out_frame->format);
    const int rows = AV_CEIL_RSHIFT(out_frame->height, 5); /* log2(FG_BLOCK_SIZE) */
    const int subx = desc->log2_chroma_w, suby = desc->log2_chroma_h;

    // Generate grain LUTs as needed
    FUNC(generate_grain_y_c)(grain_lut[0], params HBD_CALL);
    if (data->num_uv_points[0] || data->chroma_scaling_from_luma)
        FUNC(generate_grain_uv_c)(grain_lut[1], grain_lut[0], params, 0, subx, suby HBD_CALL);
    if (data->num_uv_points[1] || data->chroma_scaling_from_luma)
        FUNC(generate_grain_uv_c)(grain_lut[2], grain_lut[0], params, 1, subx, suby HBD_CALL);

    // Generate scaling LUTs as needed
    if (data->num_y_points || data->chroma_scaling_from_luma)
        FUNC(generate_scaling)(data->y_points, data->num_y_points, scaling[0] HBD_CALL);
    if (data->num_uv_points[0])
        FUNC(generate_scaling)(data->uv_points[0], data->num_uv_points[0], scaling[1] HBD_CALL);
    if (data->num_uv_points[1])
        FUNC(generate_scaling)(data->uv_points[1], data->num_uv_points[1], scaling[2] HBD_CALL);

    for (int row = 0; row < rows; row++) {
        FUNC(apply_grain_row)(out_frame, in_frame, subx, suby, scaling, grain_lut,
                              params, row HBD_CALL);
    }

    return 0;
}
