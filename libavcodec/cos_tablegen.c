/*
 * Generate a header file for hardcoded ff_cos_* tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define BITS 16
#define FLOATFMT "%.18e"
#define FIXEDFMT "%6d"

static int clip_f15(int v)
{
    return v < -32767 ? -32767 :
           v >  32767 ?  32767 :
           v;
}

static void printval(double val, int fixed)
{
    if (fixed)
        printf(" "FIXEDFMT",", clip_f15(lrint(val * (double)(1<<15))));
    else
        printf(" "FLOATFMT",", val);

}

int main(int argc, char *argv[])
{
    int i, j;
    int do_sin = argc > 1 && !strcmp(argv[1], "sin");
    int fixed  = argc > 1 &&  strstr(argv[1], "fixed");
    double (*func)(double) = do_sin ? sin : cos;

    printf("/* This file was automatically generated. */\n");
    printf("#define CONFIG_FFT_FLOAT %d\n", !fixed);
    printf("#include \"libavcodec/%s\"\n", do_sin ? "rdft.h" : "fft.h");
    for (i = 4; i <= BITS; i++) {
        int m = 1 << i;
        double freq = 2*M_PI/m;
        printf("%s(%i) = {\n   ", do_sin ? "SINTABLE" : "COSTABLE", m);
        for (j = 0; j < m/2 - 1; j++) {
            int idx = j > m/4 ? m/2 - j : j;
            if (do_sin && j >= m/4)
                idx = m/4 - j;
            printval(func(idx*freq), fixed);
            if ((j & 3) == 3)
                printf("\n   ");
        }
        printval(func(do_sin ? -(m/4 - 1)*freq : freq), fixed);
        printf("\n};\n");
    }
    return 0;
}
