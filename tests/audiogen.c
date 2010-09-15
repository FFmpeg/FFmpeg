/*
 * Generates a synthetic stereo sound
 * NOTE: No floats are used to guarantee a bit exact output.
 *
 * Copyright (c) 2002 Fabrice Bellard
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
#include <stdio.h>

#define MAX_CHANNELS 8

static unsigned int myrnd(unsigned int *seed_ptr, int n)
{
    unsigned int seed, val;

    seed = *seed_ptr;
    seed = (seed * 314159) + 1;
    if (n == 256) {
        val = seed >> 24;
    } else {
        val = seed % n;
    }
    *seed_ptr = seed;
    return val;
}

#define FRAC_BITS 16
#define FRAC_ONE (1 << FRAC_BITS)

#define COS_TABLE_BITS 7

/* integer cosinus */
static const unsigned short cos_table[(1 << COS_TABLE_BITS) + 2] = {
 0x8000, 0x7ffe, 0x7ff6, 0x7fea, 0x7fd9, 0x7fc2, 0x7fa7, 0x7f87,
 0x7f62, 0x7f38, 0x7f0a, 0x7ed6, 0x7e9d, 0x7e60, 0x7e1e, 0x7dd6,
 0x7d8a, 0x7d3a, 0x7ce4, 0x7c89, 0x7c2a, 0x7bc6, 0x7b5d, 0x7aef,
 0x7a7d, 0x7a06, 0x798a, 0x790a, 0x7885, 0x77fb, 0x776c, 0x76d9,
 0x7642, 0x75a6, 0x7505, 0x7460, 0x73b6, 0x7308, 0x7255, 0x719e,
 0x70e3, 0x7023, 0x6f5f, 0x6e97, 0x6dca, 0x6cf9, 0x6c24, 0x6b4b,
 0x6a6e, 0x698c, 0x68a7, 0x67bd, 0x66d0, 0x65de, 0x64e9, 0x63ef,
 0x62f2, 0x61f1, 0x60ec, 0x5fe4, 0x5ed7, 0x5dc8, 0x5cb4, 0x5b9d,
 0x5a82, 0x5964, 0x5843, 0x571e, 0x55f6, 0x54ca, 0x539b, 0x5269,
 0x5134, 0x4ffb, 0x4ec0, 0x4d81, 0x4c40, 0x4afb, 0x49b4, 0x486a,
 0x471d, 0x45cd, 0x447b, 0x4326, 0x41ce, 0x4074, 0x3f17, 0x3db8,
 0x3c57, 0x3af3, 0x398d, 0x3825, 0x36ba, 0x354e, 0x33df, 0x326e,
 0x30fc, 0x2f87, 0x2e11, 0x2c99, 0x2b1f, 0x29a4, 0x2827, 0x26a8,
 0x2528, 0x23a7, 0x2224, 0x209f, 0x1f1a, 0x1d93, 0x1c0c, 0x1a83,
 0x18f9, 0x176e, 0x15e2, 0x1455, 0x12c8, 0x113a, 0x0fab, 0x0e1c,
 0x0c8c, 0x0afb, 0x096b, 0x07d9, 0x0648, 0x04b6, 0x0324, 0x0192,
 0x0000, 0x0000,
};

#define CSHIFT (FRAC_BITS - COS_TABLE_BITS - 2)

static int int_cos(int a)
{
    int neg, v, f;
    const unsigned short *p;

    a = a & (FRAC_ONE - 1); /* modulo 2 * pi */
    if (a >= (FRAC_ONE / 2))
        a = FRAC_ONE - a;
    neg = 0;
    if (a > (FRAC_ONE / 4)) {
        neg = -1;
        a = (FRAC_ONE / 2) - a;
    }
    p = cos_table + (a >> CSHIFT);
    /* linear interpolation */
    f = a & ((1 << CSHIFT) - 1);
    v = p[0] + (((p[1] - p[0]) * f + (1 << (CSHIFT - 1))) >> CSHIFT);
    v = (v ^ neg) - neg;
    v = v << (FRAC_BITS - 15);
    return v;
}

FILE *outfile;

static void put_sample(int v)
{
    fputc(v & 0xff, outfile);
    fputc((v >> 8) & 0xff, outfile);
}

int main(int argc, char **argv)
{
    int i, a, v, j, f, amp, ampa;
    unsigned int seed = 1;
    int tabf1[MAX_CHANNELS], tabf2[MAX_CHANNELS];
    int taba[MAX_CHANNELS];
    int sample_rate = 44100;
    int nb_channels = 2;

    if (argc < 2 || argc > 4) {
        printf("usage: %s file [<sample rate> [<channels>]]\n"
               "generate a test raw 16 bit audio stream\n"
               "default: 44100 Hz stereo\n", argv[0]);
        exit(1);
    }

    if (argc > 2) {
        sample_rate = atoi(argv[2]);
        if (sample_rate <= 0) {
            fprintf(stderr, "invalid sample rate: %d\n", sample_rate);
            return 1;
        }
    }

    if (argc > 3) {
        nb_channels = atoi(argv[3]);
        if (nb_channels < 1 || nb_channels > MAX_CHANNELS) {
            fprintf(stderr, "invalid number of channels: %d\n", nb_channels);
            return 1;
        }
    }

    outfile = fopen(argv[1], "wb");
    if (!outfile) {
        perror(argv[1]);
        return 1;
    }

    /* 1 second of single freq sinus at 1000 Hz */
    a = 0;
    for(i=0;i<1 * sample_rate;i++) {
        v = (int_cos(a) * 10000) >> FRAC_BITS;
        for(j=0;j<nb_channels;j++)
            put_sample(v);
        a += (1000 * FRAC_ONE) / sample_rate;
    }

    /* 1 second of varing frequency between 100 and 10000 Hz */
    a = 0;
    for(i=0;i<1 * sample_rate;i++) {
        v = (int_cos(a) * 10000) >> FRAC_BITS;
        for(j=0;j<nb_channels;j++)
            put_sample(v);
        f = 100 + (((10000 - 100) * i) / sample_rate);
        a += (f * FRAC_ONE) / sample_rate;
    }

    /* 0.5 second of low amplitude white noise */
    for(i=0;i<sample_rate / 2;i++) {
        v = myrnd(&seed, 20000) - 10000;
        for(j=0;j<nb_channels;j++)
            put_sample(v);
    }

    /* 0.5 second of high amplitude white noise */
    for(i=0;i<sample_rate / 2;i++) {
        v = myrnd(&seed, 65535) - 32768;
        for(j=0;j<nb_channels;j++)
            put_sample(v);
    }

    /* 1 second of unrelated ramps for each channel */
    for(j=0;j<nb_channels;j++) {
        taba[j] = 0;
        tabf1[j] = 100 + myrnd(&seed, 5000);
        tabf2[j] = 100 + myrnd(&seed, 5000);
    }
    for(i=0;i<1 * sample_rate;i++) {
        for(j=0;j<nb_channels;j++) {
            v = (int_cos(taba[j]) * 10000) >> FRAC_BITS;
            put_sample(v);
            f = tabf1[j] + (((tabf2[j] - tabf1[j]) * i) / sample_rate);
            taba[j] += (f * FRAC_ONE) / sample_rate;
        }
    }

    /* 2 seconds of 500 Hz with varying volume */
    a = 0;
    ampa = 0;
    for(i=0;i<2 * sample_rate;i++) {
        for(j=0;j<nb_channels;j++) {
            amp = ((FRAC_ONE + int_cos(ampa)) * 5000) >> FRAC_BITS;
            if (j & 1)
                amp = 10000 - amp;
            v = (int_cos(a) * amp) >> FRAC_BITS;
            put_sample(v);
            a += (500 * FRAC_ONE) / sample_rate;
            ampa += (2 * FRAC_ONE) / sample_rate;
        }
    }

    fclose(outfile);
    return 0;
}
