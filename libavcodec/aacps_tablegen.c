/*
 * Generate a header file for hardcoded Parametric Stereo tables
 *
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#include <stdlib.h>
#define CONFIG_HARDCODED_TABLES 0
#include "aacps_tablegen.h"
#include "tableprint.h"

void write_float_3d_array (const void *p, int b, int c, int d)
{
    int i;
    const float *f = p;
    for (i = 0; i < b; i++) {
        printf("{\n");
        write_float_2d_array(f, c, d);
        printf("},\n");
        f += c * d;
    }
}

void write_float_4d_array (const void *p, int a, int b, int c, int d)
{
    int i;
    const float *f = p;
    for (i = 0; i < a; i++) {
        printf("{\n");
        write_float_3d_array(f, b, c, d);
        printf("},\n");
        f += b * c * d;
    }
}

int main(void)
{
    ps_tableinit();

    write_fileheader();

    printf("static const float pd_re_smooth[8*8*8] = {\n");
    write_float_array(pd_re_smooth, 8*8*8);
    printf("};\n");
    printf("static const float pd_im_smooth[8*8*8] = {\n");
    write_float_array(pd_im_smooth, 8*8*8);
    printf("};\n");

    printf("static const float HA[46][8][4] = {\n");
    write_float_3d_array(HA, 46, 8, 4);
    printf("};\n");
    printf("static const float HB[46][8][4] = {\n");
    write_float_3d_array(HB, 46, 8, 4);
    printf("};\n");

    printf("static const DECLARE_ALIGNED(16, float, f20_0_8)[8][8][2] = {\n");
    write_float_3d_array(f20_0_8, 8, 8, 2);
    printf("};\n");
    printf("static const DECLARE_ALIGNED(16, float, f34_0_12)[12][8][2] = {\n");
    write_float_3d_array(f34_0_12, 12, 8, 2);
    printf("};\n");
    printf("static const DECLARE_ALIGNED(16, float, f34_1_8)[8][8][2] = {\n");
    write_float_3d_array(f34_1_8, 8, 8, 2);
    printf("};\n");
    printf("static const DECLARE_ALIGNED(16, float, f34_2_4)[4][8][2] = {\n");
    write_float_3d_array(f34_2_4, 4, 8, 2);
    printf("};\n");

    printf("static TABLE_CONST DECLARE_ALIGNED(16, float, Q_fract_allpass)[2][50][3][2] = {\n");
    write_float_4d_array(Q_fract_allpass, 2, 50, 3, 2);
    printf("};\n");
    printf("static const DECLARE_ALIGNED(16, float, phi_fract)[2][50][2] = {\n");
    write_float_3d_array(phi_fract, 2, 50, 2);
    printf("};\n");

    return 0;
}
