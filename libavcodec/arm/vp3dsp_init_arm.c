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
#include "libavutil/arm/cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/vp3dsp.h"

void ff_vp3_idct_put_neon(uint8_t *dest, int line_size, int16_t *data);
void ff_vp3_idct_add_neon(uint8_t *dest, int line_size, int16_t *data);
void ff_vp3_idct_dc_add_neon(uint8_t *dest, int line_size, const int16_t *data);

void ff_vp3_v_loop_filter_neon(uint8_t *, int, int *);
void ff_vp3_h_loop_filter_neon(uint8_t *, int, int *);

av_cold void ff_vp3dsp_init_arm(VP3DSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->idct_put      = ff_vp3_idct_put_neon;
        c->idct_add      = ff_vp3_idct_add_neon;
        c->idct_dc_add   = ff_vp3_idct_dc_add_neon;
        c->v_loop_filter = ff_vp3_v_loop_filter_neon;
        c->h_loop_filter = ff_vp3_h_loop_filter_neon;
        c->idct_perm     = FF_TRANSPOSE_IDCT_PERM;
    }
}
