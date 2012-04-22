/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include "libavcodec/vp8dsp.h"
#include "vp8dsp.h"

void ff_vp8_luma_dc_wht_dc_armv6(DCTELEM block[4][4][16], DCTELEM dc[16]);

VP8_MC(pixels4, armv6);

av_cold void ff_vp8dsp_init_armv6(VP8DSPContext *dsp)
{
    dsp->vp8_luma_dc_wht_dc = ff_vp8_luma_dc_wht_dc_armv6;

    dsp->put_vp8_epel_pixels_tab[2][0][0] = ff_put_vp8_pixels4_armv6;

    dsp->put_vp8_bilinear_pixels_tab[2][0][0] = ff_put_vp8_pixels4_armv6;
}
