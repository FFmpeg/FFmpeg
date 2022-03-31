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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/vc1dsp.h"

#include "config.h"

void ff_vc1_inv_trans_8x8_neon(int16_t *block);
void ff_vc1_inv_trans_8x4_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x8_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);

void ff_vc1_inv_trans_8x8_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_8x4_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x8_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);
void ff_vc1_inv_trans_4x4_dc_neon(uint8_t *dest, ptrdiff_t stride, int16_t *block);

void ff_vc1_v_loop_filter4_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter4_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_v_loop_filter8_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter8_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_v_loop_filter16_neon(uint8_t *src, ptrdiff_t stride, int pq);
void ff_vc1_h_loop_filter16_neon(uint8_t *src, ptrdiff_t stride, int pq);

void ff_put_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_avg_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_put_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);
void ff_avg_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
                                int h, int x, int y);

int ff_vc1_unescape_buffer_helper_neon(const uint8_t *src, int size, uint8_t *dst);

static int vc1_unescape_buffer_neon(const uint8_t *src, int size, uint8_t *dst)
{
    /* Dealing with starting and stopping, and removing escape bytes, are
     * comparatively less time-sensitive, so are more clearly expressed using
     * a C wrapper around the assembly inner loop. Note that we assume a
     * little-endian machine that supports unaligned loads. */
    int dsize = 0;
    while (size >= 4)
    {
        int found = 0;
        while (!found && (((uintptr_t) dst) & 7) && size >= 4)
        {
            found = (AV_RL32(src) &~ 0x03000000) == 0x00030000;
            if (!found)
            {
                *dst++ = *src++;
                --size;
                ++dsize;
            }
        }
        if (!found)
        {
            int skip = size - ff_vc1_unescape_buffer_helper_neon(src, size, dst);
            dst += skip;
            src += skip;
            size -= skip;
            dsize += skip;
            while (!found && size >= 4)
            {
                found = (AV_RL32(src) &~ 0x03000000) == 0x00030000;
                if (!found)
                {
                    *dst++ = *src++;
                    --size;
                    ++dsize;
                }
            }
        }
        if (found)
        {
            *dst++ = *src++;
            *dst++ = *src++;
            ++src;
            size -= 3;
            dsize += 2;
        }
    }
    while (size > 0)
    {
        *dst++ = *src++;
        --size;
        ++dsize;
    }
    return dsize;
}

av_cold void ff_vc1dsp_init_aarch64(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->vc1_inv_trans_8x8 = ff_vc1_inv_trans_8x8_neon;
        dsp->vc1_inv_trans_8x4 = ff_vc1_inv_trans_8x4_neon;
        dsp->vc1_inv_trans_4x8 = ff_vc1_inv_trans_4x8_neon;
        dsp->vc1_inv_trans_4x4 = ff_vc1_inv_trans_4x4_neon;
        dsp->vc1_inv_trans_8x8_dc = ff_vc1_inv_trans_8x8_dc_neon;
        dsp->vc1_inv_trans_8x4_dc = ff_vc1_inv_trans_8x4_dc_neon;
        dsp->vc1_inv_trans_4x8_dc = ff_vc1_inv_trans_4x8_dc_neon;
        dsp->vc1_inv_trans_4x4_dc = ff_vc1_inv_trans_4x4_dc_neon;

        dsp->vc1_v_loop_filter4  = ff_vc1_v_loop_filter4_neon;
        dsp->vc1_h_loop_filter4  = ff_vc1_h_loop_filter4_neon;
        dsp->vc1_v_loop_filter8  = ff_vc1_v_loop_filter8_neon;
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_neon;
        dsp->vc1_v_loop_filter16 = ff_vc1_v_loop_filter16_neon;
        dsp->vc1_h_loop_filter16 = ff_vc1_h_loop_filter16_neon;

        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_neon;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_neon;
        dsp->put_no_rnd_vc1_chroma_pixels_tab[1] = ff_put_vc1_chroma_mc4_neon;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[1] = ff_avg_vc1_chroma_mc4_neon;

        dsp->vc1_unescape_buffer = vc1_unescape_buffer_neon;
    }
}
