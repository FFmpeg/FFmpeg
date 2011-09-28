/*
 * Discrete wavelet transform
 * Copyright (c) 2007 Kamil Nowosad
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

/**
 * Discrete wavelet transform
 * @file
 * @author Kamil Nowosad
 */

#include "j2k_dwt.h"

const static float scale97[] = {1.625786, 1.230174};

static inline void extend53(int *p, int i0, int i1)
{
    p[i0 - 1] = p[i0 + 1];
    p[i1    ] = p[i1 - 2];
    p[i0 - 2] = p[i0 + 2];
    p[i1 + 1] = p[i1 - 3];
}

static inline void extend97(float *p, int i0, int i1)
{
    int i;

    for (i = 1; i <= 4; i++){
        p[i0 - i] = p[i0 + i];
        p[i1 + i - 1] = p[i1 - i - 1];
    }
}

static void sd_1d53(int *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend53(p, i0, i1);

    for (i = (i0+1)/2 - 1; i < (i1+1)/2; i++)
        p[2*i+1] -= (p[2*i] + p[2*i+2]) >> 1;
    for (i = (i0+1)/2; i < (i1+1)/2; i++)
        p[2*i] += (p[2*i-1] + p[2*i+1] + 2) >> 2;
}

static void dwt_encode53(DWTContext *s, int *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    int *line = s->linebuf;
    line += 3;

    for (lev = s->ndeclevels-1; lev >= 0; lev--){
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        int *l;

        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++){
            int i, j = 0;

            for (i = 0; i < lh; i++)
                l[i] = t[w*lp + i];

            sd_1d53(line, mh, mh + lh);

            // copy back and deinterleave
            for (i =   mh; i < lh; i+=2, j++)
                t[w*lp + j] = l[i];
            for (i = 1-mh; i < lh; i+=2, j++)
                t[w*lp + j] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;

            for (i = 0; i < lv; i++)
                l[i] = t[w*i + lp];

            sd_1d53(line, mv, mv + lv);

            // copy back and deinterleave
            for (i =   mv; i < lv; i+=2, j++)
                t[w*j + lp] = l[i];
            for (i = 1-mv; i < lv; i+=2, j++)
                t[w*j + lp] = l[i];
        }
    }
}

static void sd_1d97(float *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97(p, i0, i1);
    i0++; i1++;

    for (i = i0/2 - 2; i < i1/2 + 1; i++)
        p[2*i+1] -= 1.586134 * (p[2*i] + p[2*i+2]);
    for (i = i0/2 - 1; i < i1/2 + 1; i++)
        p[2*i] -= 0.052980 * (p[2*i-1] + p[2*i+1]);
    for (i = i0/2 - 1; i < i1/2; i++)
        p[2*i+1] += 0.882911 * (p[2*i] + p[2*i+2]);
    for (i = i0/2; i < i1/2; i++)
        p[2*i] += 0.443506 * (p[2*i-1] + p[2*i+1]);
}

static void dwt_encode97(DWTContext *s, int *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    float *line = s->linebuf;
    line += 5;

    for (lev = s->ndeclevels-1; lev >= 0; lev--){
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        float *l;

        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++){
            int i, j = 0;

            for (i = 0; i < lh; i++)
                l[i] = t[w*lp + i];

            sd_1d97(line, mh, mh + lh);

            // copy back and deinterleave
            for (i =   mh; i < lh; i+=2, j++)
                t[w*lp + j] = scale97[mh] * l[i] / 2;
            for (i = 1-mh; i < lh; i+=2, j++)
                t[w*lp + j] = scale97[mh] * l[i] / 2;
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;

            for (i = 0; i < lv; i++)
                l[i] = t[w*i + lp];

            sd_1d97(line, mv, mv + lv);

            // copy back and deinterleave
            for (i =   mv; i < lv; i+=2, j++)
                t[w*j + lp] = scale97[mv] * l[i] / 2;
            for (i = 1-mv; i < lv; i+=2, j++)
                t[w*j + lp] = scale97[mv] * l[i] / 2;
        }
    }
}

static void sr_1d53(int *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend53(p, i0, i1);

    for (i = i0/2; i < i1/2 + 1; i++)
        p[2*i] -= (p[2*i-1] + p[2*i+1] + 2) >> 2;
    for (i = i0/2; i < i1/2; i++)
        p[2*i+1] += (p[2*i] + p[2*i+2]) >> 1;
}

static void dwt_decode53(DWTContext *s, int *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    int *line = s->linebuf;
    line += 3;

    for (lev = 0; lev < s->ndeclevels; lev++){
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        int *l;

        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++){
            int i, j = 0;
            // copy with interleaving
            for (i =   mh; i < lh; i+=2, j++)
                l[i] = t[w*lp + j];
            for (i = 1-mh; i < lh; i+=2, j++)
                l[i] = t[w*lp + j];

            sr_1d53(line, mh, mh + lh);

            for (i = 0; i < lh; i++)
                t[w*lp + i] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++){
            int i, j = 0;
            // copy with interleaving
            for (i =   mv; i < lv; i+=2, j++)
                l[i] = t[w*j + lp];
            for (i = 1-mv; i < lv; i+=2, j++)
                l[i] = t[w*j + lp];

            sr_1d53(line, mv, mv + lv);

            for (i = 0; i < lv; i++)
                t[w*i + lp] = l[i];
        }
    }
}

static void sr_1d97(float *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97(p, i0, i1);

    for (i = i0/2 - 1; i < i1/2 + 2; i++)
        p[2*i] -= 0.443506 * (p[2*i-1] + p[2*i+1]);
    for (i = i0/2 - 1; i < i1/2 + 1; i++)
        p[2*i+1] -= 0.882911 * (p[2*i] + p[2*i+2]);
    for (i = i0/2; i < i1/2 + 1; i++)
        p[2*i] += 0.052980 * (p[2*i-1] + p[2*i+1]);
    for (i = i0/2; i < i1/2; i++)
        p[2*i+1] += 1.586134 * (p[2*i] + p[2*i+2]);
}

static void dwt_decode97(DWTContext *s, int *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    float *line = s->linebuf;
    line += 5;

    for (lev = 0; lev < s->ndeclevels; lev++){
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        float *l;

        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++){
            int i, j = 0;
            // copy with interleaving
            for (i =   mh; i < lh; i+=2, j++)
                l[i] = scale97[1-mh] * t[w*lp + j];
            for (i = 1-mh; i < lh; i+=2, j++)
                l[i] = scale97[1-mh] * t[w*lp + j];

            sr_1d97(line, mh, mh + lh);

            for (i = 0; i < lh; i++)
                t[w*lp + i] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++){
            int i, j = 0;
            // copy with interleaving
            for (i =   mv; i < lv; i+=2, j++)
                l[i] = scale97[1-mv] * t[w*j + lp];
            for (i = 1-mv; i < lv; i+=2, j++)
                l[i] = scale97[1-mv] * t[w*j + lp];

            sr_1d97(line, mv, mv + lv);

            for (i = 0; i < lv; i++)
                t[w*i + lp] = l[i];
        }
    }
}

int ff_j2k_dwt_init(DWTContext *s, uint16_t border[2][2], int decomp_levels, int type)
{
    int i, j, lev = decomp_levels, maxlen,
        b[2][2];

    if (decomp_levels >= FF_DWT_MAX_DECLVLS)
        return AVERROR_INVALIDDATA;
    s->ndeclevels = decomp_levels;
    s->type = type;

    for (i = 0; i < 2; i++)
        for(j = 0; j < 2; j++)
            b[i][j] = border[i][j];

    maxlen = FFMAX(b[0][1] - b[0][0],
                   b[1][1] - b[1][0]);

    while(--lev >= 0){
        for (i = 0; i < 2; i++){
            s->linelen[lev][i] = b[i][1] - b[i][0];
            s->mod[lev][i] = b[i][0] & 1;
            for (j = 0; j < 2; j++)
                b[i][j] = (b[i][j] + 1) >> 1;
        }
    }
    if (type == FF_DWT97)
        s->linebuf = av_malloc((maxlen + 12) * sizeof(float));
    else if (type == FF_DWT53)
        s->linebuf = av_malloc((maxlen + 6) * sizeof(int));
    else
        return -1;

    if (!s->linebuf)
        return AVERROR(ENOMEM);

    return 0;
}

int ff_j2k_dwt_encode(DWTContext *s, int *t)
{
    switch(s->type){
        case FF_DWT97:
            dwt_encode97(s, t); break;
        case FF_DWT53:
            dwt_encode53(s, t); break;
        default:
            return -1;
    }
    return 0;
}

int ff_j2k_dwt_decode(DWTContext *s, int *t)
{
    switch(s->type){
        case FF_DWT97:
            dwt_decode97(s, t); break;
        case FF_DWT53:
            dwt_decode53(s, t); break;
        default:
            return -1;
    }
    return 0;
}

void ff_j2k_dwt_destroy(DWTContext *s)
{
    av_freep(&s->linebuf);
}
