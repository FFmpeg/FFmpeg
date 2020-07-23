/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
 * Copyright (c) 2016 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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
#include "../hpeldsp.h"
#include "libavcodec/mips/hpeldsp_mips.h"

void ff_hpeldsp_init_mips(HpelDSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_mmi(cpu_flags)) {
        c->put_pixels_tab[0][0] = ff_put_pixels16_8_mmi;
        c->put_pixels_tab[0][1] = ff_put_pixels16_x2_8_mmi;
        c->put_pixels_tab[0][2] = ff_put_pixels16_y2_8_mmi;
        c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_8_mmi;

        c->put_pixels_tab[1][0] = ff_put_pixels8_8_mmi;
        c->put_pixels_tab[1][1] = ff_put_pixels8_x2_8_mmi;
        c->put_pixels_tab[1][2] = ff_put_pixels8_y2_8_mmi;
        c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_8_mmi;

        c->put_pixels_tab[2][0] = ff_put_pixels4_8_mmi;
        c->put_pixels_tab[2][1] = ff_put_pixels4_x2_8_mmi;
        c->put_pixels_tab[2][2] = ff_put_pixels4_y2_8_mmi;
        c->put_pixels_tab[2][3] = ff_put_pixels4_xy2_8_mmi;

        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_8_mmi;
        c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_8_mmi;
        c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_8_mmi;
        c->put_no_rnd_pixels_tab[0][3] = ff_put_no_rnd_pixels16_xy2_8_mmi;

        c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_8_mmi;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_8_mmi;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_8_mmi;
        c->put_no_rnd_pixels_tab[1][3] = ff_put_no_rnd_pixels8_xy2_8_mmi;

        c->avg_pixels_tab[0][0] = ff_avg_pixels16_8_mmi;
        c->avg_pixels_tab[0][1] = ff_avg_pixels16_x2_8_mmi;
        c->avg_pixels_tab[0][2] = ff_avg_pixels16_y2_8_mmi;
        c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_8_mmi;

        c->avg_pixels_tab[1][0] = ff_avg_pixels8_8_mmi;
        c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_8_mmi;
        c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_8_mmi;
        c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_8_mmi;

        c->avg_pixels_tab[2][0] = ff_avg_pixels4_8_mmi;
        c->avg_pixels_tab[2][1] = ff_avg_pixels4_x2_8_mmi;
        c->avg_pixels_tab[2][2] = ff_avg_pixels4_y2_8_mmi;
        c->avg_pixels_tab[2][3] = ff_avg_pixels4_xy2_8_mmi;
    }

    if (have_msa(cpu_flags)) {
        c->put_pixels_tab[0][0] = ff_put_pixels16_msa;
        c->put_pixels_tab[0][1] = ff_put_pixels16_x2_msa;
        c->put_pixels_tab[0][2] = ff_put_pixels16_y2_msa;
        c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_msa;

        c->put_pixels_tab[1][0] = ff_put_pixels8_msa;
        c->put_pixels_tab[1][1] = ff_put_pixels8_x2_msa;
        c->put_pixels_tab[1][2] = ff_put_pixels8_y2_msa;
        c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_msa;

        c->put_pixels_tab[2][1] = ff_put_pixels4_x2_msa;
        c->put_pixels_tab[2][2] = ff_put_pixels4_y2_msa;
        c->put_pixels_tab[2][3] = ff_put_pixels4_xy2_msa;

        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_msa;
        c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_msa;
        c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_msa;
        c->put_no_rnd_pixels_tab[0][3] = ff_put_no_rnd_pixels16_xy2_msa;

        c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_msa;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_msa;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_msa;
        c->put_no_rnd_pixels_tab[1][3] = ff_put_no_rnd_pixels8_xy2_msa;

        c->avg_pixels_tab[0][0] = ff_avg_pixels16_msa;
        c->avg_pixels_tab[0][1] = ff_avg_pixels16_x2_msa;
        c->avg_pixels_tab[0][2] = ff_avg_pixels16_y2_msa;
        c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_msa;

        c->avg_pixels_tab[1][0] = ff_avg_pixels8_msa;
        c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_msa;
        c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_msa;
        c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_msa;

        c->avg_pixels_tab[2][0] = ff_avg_pixels4_msa;
        c->avg_pixels_tab[2][1] = ff_avg_pixels4_x2_msa;
        c->avg_pixels_tab[2][2] = ff_avg_pixels4_y2_msa;
        c->avg_pixels_tab[2][3] = ff_avg_pixels4_xy2_msa;
    }
}
