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

#include "config.h"
#include "vorbisdsp.h"
#include "vorbis.h"

void ff_vorbisdsp_init(VorbisDSPContext *dsp)
{
    dsp->vorbis_inverse_coupling = ff_vorbis_inverse_coupling;

    if (ARCH_X86)
        ff_vorbisdsp_init_x86(dsp);
    if (ARCH_PPC)
        ff_vorbisdsp_init_ppc(dsp);
    if (ARCH_ARM)
        ff_vorbisdsp_init_arm(dsp);
}
