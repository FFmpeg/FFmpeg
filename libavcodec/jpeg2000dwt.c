/*
 * Discrete wavelet transform
 * Copyright (c) 2007 Kamil Nowosad
 * Copyright (c) 2013 Nicolas Bertrand <nicoinattendu@gmail.com>
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
 * @file
 * Discrete wavelet transform
 */

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "jpeg2000dwt.h"
#include "internal.h"

/* Defines for 9/7 DWT lifting parameters.
 * Parameters are in float. */
#define F_LFTG_ALPHA  1.586134342059924f
#define F_LFTG_BETA   0.052980118572961f
#define F_LFTG_GAMMA  0.882911075530934f
#define F_LFTG_DELTA  0.443506852043971f
#define F_LFTG_K      1.230174104914001f
#define F_LFTG_X      1.625732422f
/* FIXME: Why use 1.625732422 instead of 1/F_LFTG_K?
 * Incorrect value in JPEG2000 norm.
 * see (ISO/IEC 15444:1 (version 2002) F.3.8.2 */

/* Lifting parameters in integer format.
 * Computed as param = (float param) * (1 << 16) */
#define I_LFTG_ALPHA  103949
#define I_LFTG_BETA     3472
#define I_LFTG_GAMMA   57862
#define I_LFTG_DELTA   29066
#define I_LFTG_K       80621
#define I_LFTG_X      106544

static inline void extend53(int *p, int i0, int i1)
{
    p[i0 - 1] = p[i0 + 1];
    p[i1]     = p[i1 - 2];
    p[i0 - 2] = p[i0 + 2];
    p[i1 + 1] = p[i1 - 3];
}

static inline void extend97_float(float *p, int i0, int i1)
{
    int i;

    for (i = 1; i <= 4; i++) {
        p[i0 - i]     = p[i0 + i];
        p[i1 + i - 1] = p[i1 - i - 1];
    }
}

static inline void extend97_int(int32_t *p, int i0, int i1)
{
    int i;

    for (i = 1; i <= 4; i++) {
        p[i0 - i]     = p[i0 + i];
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
    int *line = s->i_linebuf;
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
static void sd_1d97_float(float *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97_float(p, i0, i1);
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

static void dwt_encode97_float(DWTContext *s, float *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    float *line = s->f_linebuf;
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

            sd_1d97_float(line, mh, mh + lh);

            // copy back and deinterleave
            for (i =   mh; i < lh; i+=2, j++)
                t[w*lp + j] = F_LFTG_X * l[i] / 2;
            for (i = 1-mh; i < lh; i+=2, j++)
                t[w*lp + j] = F_LFTG_K * l[i] / 2;
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;

            for (i = 0; i < lv; i++)
                l[i] = t[w*i + lp];

            sd_1d97_float(line, mv, mv + lv);

            // copy back and deinterleave
            for (i =   mv; i < lv; i+=2, j++)
                t[w*j + lp] = F_LFTG_X * l[i] / 2;
            for (i = 1-mv; i < lv; i+=2, j++)
                t[w*j + lp] = F_LFTG_K * l[i] / 2;
        }
    }
}

static void sd_1d97_int(int *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97_int(p, i0, i1);
    i0++; i1++;

    for (i = i0/2 - 2; i < i1/2 + 1; i++)
        p[2 * i + 1] -= (I_LFTG_ALPHA * (p[2 * i]     + p[2 * i + 2]) + (1 << 15)) >> 16;
    for (i = i0/2 - 1; i < i1/2 + 1; i++)
        p[2 * i]     -= (I_LFTG_BETA  * (p[2 * i - 1] + p[2 * i + 1]) + (1 << 15)) >> 16;
    for (i = i0/2 - 1; i < i1/2; i++)
        p[2 * i + 1] += (I_LFTG_GAMMA * (p[2 * i]     + p[2 * i + 2]) + (1 << 15)) >> 16;
    for (i = i0/2; i < i1/2; i++)
        p[2 * i]     += (I_LFTG_DELTA * (p[2 * i - 1] + p[2 * i + 1]) + (1 << 15)) >> 16;
}

static void dwt_encode97_int(DWTContext *s, int *t)
{
    int lev,
        w = s->linelen[s->ndeclevels-1][0];
    int *line = s->i_linebuf;
    line += 5;

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

            sd_1d97_int(line, mh, mh + lh);

            // copy back and deinterleave
            for (i =   mh; i < lh; i+=2, j++)
                t[w*lp + j] = ((l[i] * I_LFTG_X) + (1 << 16)) >> 17;
            for (i = 1-mh; i < lh; i+=2, j++)
                t[w*lp + j] = ((l[i] * I_LFTG_K) + (1 << 16)) >> 17;
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;

            for (i = 0; i < lv; i++)
                l[i] = t[w*i + lp];

            sd_1d97_int(line, mv, mv + lv);

            // copy back and deinterleave
            for (i =   mv; i < lv; i+=2, j++)
                t[w*j + lp] = ((l[i] * I_LFTG_X) + (1 << 16)) >> 17;
            for (i = 1-mv; i < lv; i+=2, j++)
                t[w*j + lp] = ((l[i] * I_LFTG_K) + (1 << 16)) >> 17;
        }
    }
}

static void sr_1d53(int *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend53(p, i0, i1);

    for (i = i0 / 2; i < i1 / 2 + 1; i++)
        p[2 * i] -= (p[2 * i - 1] + p[2 * i + 1] + 2) >> 2;
    for (i = i0 / 2; i < i1 / 2; i++)
        p[2 * i + 1] += (p[2 * i] + p[2 * i + 2]) >> 1;
}

static void dwt_decode53(DWTContext *s, int *t)
{
    int lev;
    int w     = s->linelen[s->ndeclevels - 1][0];
    int32_t *line = s->i_linebuf;
    line += 3;

    for (lev = 0; lev < s->ndeclevels; lev++) {
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        int *l;

        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++) {
            int i, j = 0;
            // copy with interleaving
            for (i = mh; i < lh; i += 2, j++)
                l[i] = t[w * lp + j];
            for (i = 1 - mh; i < lh; i += 2, j++)
                l[i] = t[w * lp + j];

            sr_1d53(line, mh, mh + lh);

            for (i = 0; i < lh; i++)
                t[w * lp + i] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;
            // copy with interleaving
            for (i = mv; i < lv; i += 2, j++)
                l[i] = t[w * j + lp];
            for (i = 1 - mv; i < lv; i += 2, j++)
                l[i] = t[w * j + lp];

            sr_1d53(line, mv, mv + lv);

            for (i = 0; i < lv; i++)
                t[w * i + lp] = l[i];
        }
    }
}

static void sr_1d97_float(float *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97_float(p, i0, i1);

    for (i = i0 / 2 - 1; i < i1 / 2 + 2; i++)
        p[2 * i]     -= F_LFTG_DELTA * (p[2 * i - 1] + p[2 * i + 1]);
    /* step 4 */
    for (i = i0 / 2 - 1; i < i1 / 2 + 1; i++)
        p[2 * i + 1] -= F_LFTG_GAMMA * (p[2 * i]     + p[2 * i + 2]);
    /*step 5*/
    for (i = i0 / 2; i < i1 / 2 + 1; i++)
        p[2 * i]     += F_LFTG_BETA  * (p[2 * i - 1] + p[2 * i + 1]);
    /* step 6 */
    for (i = i0 / 2; i < i1 / 2; i++)
        p[2 * i + 1] += F_LFTG_ALPHA * (p[2 * i]     + p[2 * i + 2]);
}

static void dwt_decode97_float(DWTContext *s, float *t)
{
    int lev;
    int w       = s->linelen[s->ndeclevels - 1][0];
    float *line = s->f_linebuf;
    float *data = t;
    /* position at index O of line range [0-5,w+5] cf. extend function */
    line += 5;

    for (lev = 0; lev < s->ndeclevels; lev++) {
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        float *l;
        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++) {
            int i, j = 0;
            // copy with interleaving
            for (i = mh; i < lh; i += 2, j++)
                l[i] = data[w * lp + j] * F_LFTG_K;
            for (i = 1 - mh; i < lh; i += 2, j++)
                l[i] = data[w * lp + j] * F_LFTG_X;

            sr_1d97_float(line, mh, mh + lh);

            for (i = 0; i < lh; i++)
                data[w * lp + i] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;
            // copy with interleaving
            for (i = mv; i < lv; i += 2, j++)
                l[i] = data[w * j + lp] * F_LFTG_K;
            for (i = 1 - mv; i < lv; i += 2, j++)
                l[i] = data[w * j + lp] * F_LFTG_X;

            sr_1d97_float(line, mv, mv + lv);

            for (i = 0; i < lv; i++)
                data[w * i + lp] = l[i];
        }
    }
}

static void sr_1d97_int(int32_t *p, int i0, int i1)
{
    int i;

    if (i1 == i0 + 1)
        return;

    extend97_int(p, i0, i1);

    for (i = i0 / 2 - 1; i < i1 / 2 + 2; i++)
        p[2 * i]     -= (I_LFTG_DELTA * (p[2 * i - 1] + p[2 * i + 1]) + (1 << 15)) >> 16;
    /* step 4 */
    for (i = i0 / 2 - 1; i < i1 / 2 + 1; i++)
        p[2 * i + 1] -= (I_LFTG_GAMMA * (p[2 * i]     + p[2 * i + 2]) + (1 << 15)) >> 16;
    /*step 5*/
    for (i = i0 / 2; i < i1 / 2 + 1; i++)
        p[2 * i]     += (I_LFTG_BETA  * (p[2 * i - 1] + p[2 * i + 1]) + (1 << 15)) >> 16;
    /* step 6 */
    for (i = i0 / 2; i < i1 / 2; i++)
        p[2 * i + 1] += (I_LFTG_ALPHA * (p[2 * i]     + p[2 * i + 2]) + (1 << 15)) >> 16;
}

static void dwt_decode97_int(DWTContext *s, int32_t *t)
{
    int lev;
    int w       = s->linelen[s->ndeclevels - 1][0];
    int32_t *line = s->i_linebuf;
    int32_t *data = t;
    /* position at index O of line range [0-5,w+5] cf. extend function */
    line += 5;

    for (lev = 0; lev < s->ndeclevels; lev++) {
        int lh = s->linelen[lev][0],
            lv = s->linelen[lev][1],
            mh = s->mod[lev][0],
            mv = s->mod[lev][1],
            lp;
        int32_t *l;
        // HOR_SD
        l = line + mh;
        for (lp = 0; lp < lv; lp++) {
            int i, j = 0;
            // rescale with interleaving
            for (i = mh; i < lh; i += 2, j++)
                l[i] = ((data[w * lp + j] * I_LFTG_K) + (1 << 15)) >> 16;
            for (i = 1 - mh; i < lh; i += 2, j++)
                l[i] = ((data[w * lp + j] * I_LFTG_X) + (1 << 15)) >> 16;

            sr_1d97_int(line, mh, mh + lh);

            for (i = 0; i < lh; i++)
                data[w * lp + i] = l[i];
        }

        // VER_SD
        l = line + mv;
        for (lp = 0; lp < lh; lp++) {
            int i, j = 0;
            // rescale with interleaving
            for (i = mv; i < lv; i += 2, j++)
                l[i] = ((data[w * j + lp] * I_LFTG_K) + (1 << 15)) >> 16;
            for (i = 1 - mv; i < lv; i += 2, j++)
                l[i] = ((data[w * j + lp] * I_LFTG_X) + (1 << 15)) >> 16;

            sr_1d97_int(line, mv, mv + lv);

            for (i = 0; i < lv; i++)
                data[w * i + lp] = l[i];
        }
    }
}

int ff_jpeg2000_dwt_init(DWTContext *s, uint16_t border[2][2],
                         int decomp_levels, int type)
{
    int i, j, lev = decomp_levels, maxlen,
        b[2][2];

    s->ndeclevels = decomp_levels;
    s->type       = type;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            b[i][j] = border[i][j];

    maxlen = FFMAX(b[0][1] - b[0][0],
                   b[1][1] - b[1][0]);
    while (--lev >= 0)
        for (i = 0; i < 2; i++) {
            s->linelen[lev][i] = b[i][1] - b[i][0];
            s->mod[lev][i]     = b[i][0] & 1;
            for (j = 0; j < 2; j++)
                b[i][j] = (b[i][j] + 1) >> 1;
        }
    switch (type) {
    case FF_DWT97:
        s->f_linebuf = av_malloc_array((maxlen + 12), sizeof(*s->f_linebuf));
        if (!s->f_linebuf)
            return AVERROR(ENOMEM);
        break;
     case FF_DWT97_INT:
        s->i_linebuf = av_malloc_array((maxlen + 12), sizeof(*s->i_linebuf));
        if (!s->i_linebuf)
            return AVERROR(ENOMEM);
        break;
    case FF_DWT53:
        s->i_linebuf = av_malloc_array((maxlen +  6), sizeof(*s->i_linebuf));
        if (!s->i_linebuf)
            return AVERROR(ENOMEM);
        break;
    default:
        return -1;
    }
    return 0;
}

int ff_dwt_encode(DWTContext *s, void *t)
{
    switch(s->type){
        case FF_DWT97:
            dwt_encode97_float(s, t); break;
        case FF_DWT97_INT:
            dwt_encode97_int(s, t); break;
        case FF_DWT53:
            dwt_encode53(s, t); break;
        default:
            return -1;
    }
    return 0;
}

int ff_dwt_decode(DWTContext *s, void *t)
{
    switch (s->type) {
    case FF_DWT97:
        dwt_decode97_float(s, t);
        break;
    case FF_DWT97_INT:
        dwt_decode97_int(s, t);
        break;
    case FF_DWT53:
        dwt_decode53(s, t);
        break;
    default:
        return -1;
    }
    return 0;
}

void ff_dwt_destroy(DWTContext *s)
{
    av_freep(&s->f_linebuf);
    av_freep(&s->i_linebuf);
}
