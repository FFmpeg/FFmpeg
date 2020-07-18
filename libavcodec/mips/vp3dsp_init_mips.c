
/*
 * Copyright (c) 2018 gxw <guxiwei-hf@loongson.cn>
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

#include "libavutil/mips/cpu.h"
#include "config.h"
#include "libavutil/attributes.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/vp3dsp.h"
#include "vp3dsp_mips.h"

av_cold void ff_vp3dsp_init_mips(VP3DSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_mmi(cpu_flags)) {
        c->put_no_rnd_pixels_l2 = ff_put_no_rnd_pixels_l2_mmi;

        c->idct_add      = ff_vp3_idct_add_mmi;
        c->idct_put      = ff_vp3_idct_put_mmi;
        c->idct_dc_add   = ff_vp3_idct_dc_add_mmi;
    }

    if (have_msa(cpu_flags)) {
        c->put_no_rnd_pixels_l2 = ff_put_no_rnd_pixels_l2_msa;

        c->idct_add      = ff_vp3_idct_add_msa;
        c->idct_put      = ff_vp3_idct_put_msa;
        c->idct_dc_add   = ff_vp3_idct_dc_add_msa;
        c->v_loop_filter = ff_vp3_v_loop_filter_msa;
        c->h_loop_filter = ff_vp3_h_loop_filter_msa;
    }
}
