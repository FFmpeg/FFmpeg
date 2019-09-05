/*
 * Copyright (C) 2004-2010 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2008 David Conrad
 * Copyright (C) 2015 Open Broadcast Systems Ltd.
 * Author    (C) 2015 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#if defined(TEMPLATE_8bit)

#    define RENAME(N)   N ## _8bit
#    define TYPE        int16_t
#    undef  TEMPLATE_8bit

#elif defined(TEMPLATE_10bit)

#    define RENAME(N)   N ## _10bit
#    define TYPE        int32_t
#    undef  TEMPLATE_10bit

#elif defined(TEMPLATE_12bit)

#    define RENAME(N)   N ## _12bit
#    define TYPE        int32_t
#    undef  TEMPLATE_12bit

#endif

static void RENAME(vertical_compose53iL0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2,
                                          int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;
    for (i = 0; i < width; i++)
        b1[i] -= (unsigned)((int)(b0[i] + (unsigned)b2[i] + 2) >> 2);
}

static av_always_inline void RENAME(interleave)(TYPE *dst, TYPE *src0, TYPE *src1, int w2,
                                                int add, int shift)
{
    int i;
    for (i = 0; i < w2; i++) {
        dst[2*i  ] = ((int)(src0[i] + (unsigned)add)) >> shift;
        dst[2*i+1] = ((int)(src1[i] + (unsigned)add)) >> shift;
    }
}

static void RENAME(horizontal_compose_dirac53i)(uint8_t *_b, uint8_t *_temp, int w)
{
    int x;
    const int w2 = w >> 1;
    TYPE *b     = (TYPE *)_b;
    TYPE *temp  = (TYPE *)_temp;

    temp[0] = COMPOSE_53iL0(b[w2], b[0], b[w2]);
    for (x = 1; x < w2; x++) {
        temp[x     ] = COMPOSE_53iL0     (b[x+w2-1], b[x     ], b[x+w2]);
        temp[x+w2-1] = COMPOSE_DIRAC53iH0(temp[x-1], b[x+w2-1], temp[x]);
    }
    temp[w-1] = COMPOSE_DIRAC53iH0(temp[w2-1], b[w-1], temp[w2-1]);

    RENAME(interleave)(b, temp, temp+w2, w2, 1, 1);
}

static void RENAME(horizontal_compose_dd97i)(uint8_t *_b, uint8_t *_tmp, int w)
{
    int x;
    const int w2 = w >> 1;
    TYPE *b   = (TYPE *)_b;
    TYPE *tmp = (TYPE *)_tmp;

    tmp[0] = COMPOSE_53iL0(b[w2], b[0], b[w2]);
    for (x = 1; x < w2; x++)
        tmp[x] = COMPOSE_53iL0(b[x+w2-1], b[x], b[x+w2]);

    // extend the edges
    tmp[-1]   = tmp[0];
    tmp[w2+1] = tmp[w2] = tmp[w2-1];

    for (x = 0; x < w2; x++) {
        b[2*x  ] = ((int)(tmp[x] + 1U))>>1;
        b[2*x+1] = ((int)(COMPOSE_DD97iH0(tmp[x-1], tmp[x], b[x+w2], tmp[x+1], tmp[x+2]) + 1U))>>1;
    }
}

static void RENAME(horizontal_compose_dd137i)(uint8_t *_b, uint8_t *_tmp, int w)
{
    const int w2 = w >> 1;
    int x;
    TYPE *b   = (TYPE *)_b;
    TYPE *tmp = (TYPE *)_tmp;

    tmp[0] = COMPOSE_DD137iL0(b[w2], b[w2], b[0], b[w2  ], b[w2+1]);
    tmp[1] = COMPOSE_DD137iL0(b[w2], b[w2], b[1], b[w2+1], b[w2+2]);
    for (x = 2; x < w2-1; x++)
        tmp[x] = COMPOSE_DD137iL0(b[x+w2-2], b[x+w2-1], b[x], b[x+w2], b[x+w2+1]);
    tmp[w2-1] = COMPOSE_DD137iL0(b[w-3], b[w-2], b[w2-1], b[w-1], b[w-1]);

    // extend the edges
    tmp[-1]   = tmp[0];
    tmp[w2+1] = tmp[w2] = tmp[w2-1];

    for (x = 0; x < w2; x++) {
        b[2*x  ] = ((int)(tmp[x] + 1U))>>1;
        b[2*x+1] = ((int)(COMPOSE_DD97iH0(tmp[x-1], tmp[x], b[x+w2], tmp[x+1], tmp[x+2]) + 1U))>>1;
    }
}

static av_always_inline void RENAME(horizontal_compose_haari)(TYPE *b, TYPE *temp,
                                                              int w, int shift)
{
    const int w2 = w >> 1;
    int x;

    for (x = 0; x < w2; x++) {
        temp[x   ] = COMPOSE_HAARiL0(b[x   ], b[x+w2]);
        temp[x+w2] = COMPOSE_HAARiH0(b[x+w2], temp[x]);
    }

    RENAME(interleave)(b, temp, temp+w2, w2, shift, shift);
}

static void RENAME(horizontal_compose_haar0i)(uint8_t *_b, uint8_t *_temp, int w)
{
    TYPE *b    = (TYPE *)_b;
    TYPE *temp = (TYPE *)_temp;
    RENAME(horizontal_compose_haari)(b, temp, w, 0);
}

static void RENAME(horizontal_compose_haar1i)(uint8_t *_b, uint8_t *_temp, int w)
{
    TYPE *b    = (TYPE *)_b;
    TYPE *temp = (TYPE *)_temp;
    RENAME(horizontal_compose_haari)(b, temp, w, 1);
}

static void RENAME(horizontal_compose_fidelityi)(uint8_t *_b, uint8_t *_tmp, int w)
{
    const int w2 = w >> 1;
    int i, x;
    TYPE v[8];
    TYPE *b   = (TYPE *)_b;
    TYPE *tmp = (TYPE *)_tmp;

    for (x = 0; x < w2; x++) {
        for (i = 0; i < 8; i++)
            v[i] = b[av_clip(x-3+i, 0, w2-1)];
        tmp[x] = COMPOSE_FIDELITYiH0(v[0], v[1], v[2], v[3], b[x+w2], v[4], v[5], v[6], v[7]);
    }

    for (x = 0; x < w2; x++) {
        for (i = 0; i < 8; i++)
            v[i] = tmp[av_clip(x-4+i, 0, w2-1)];
        tmp[x+w2] = COMPOSE_FIDELITYiL0(v[0], v[1], v[2], v[3], b[x], v[4], v[5], v[6], v[7]);
    }

    RENAME(interleave)(b, tmp+w2, tmp, w2, 0, 0);
}

static void RENAME(horizontal_compose_daub97i)(uint8_t *_b, uint8_t *_temp, int w)
{
    const int w2 = w >> 1;
    int x, b0, b1, b2;
    TYPE *b    = (TYPE *)_b;
    TYPE *temp = (TYPE *)_temp;

    temp[0] = COMPOSE_DAUB97iL1(b[w2], b[0], b[w2]);
    for (x = 1; x < w2; x++) {
        temp[x     ] = COMPOSE_DAUB97iL1(b[x+w2-1], b[x     ], b[x+w2]);
        temp[x+w2-1] = COMPOSE_DAUB97iH1(temp[x-1], b[x+w2-1], temp[x]);
    }
    temp[w-1] = COMPOSE_DAUB97iH1(temp[w2-1], b[w-1], temp[w2-1]);

    // second stage combined with interleave and shift
    b0 = b2 = COMPOSE_DAUB97iL0(temp[w2], temp[0], temp[w2]);
    b[0] = ~((~b0) >> 1);
    for (x = 1; x < w2; x++) {
        b2 = COMPOSE_DAUB97iL0(temp[x+w2-1], temp[x     ], temp[x+w2]);
        b1 = COMPOSE_DAUB97iH0(          b0, temp[x+w2-1], b2        );
        b[2*x-1] = ~((~b1) >> 1);
        b[2*x  ] = ~((~b2) >> 1);
        b0 = b2;
    }
    b[w-1] = ~((~COMPOSE_DAUB97iH0(b2, temp[w-1], b2)) >> 1);
}

static void RENAME(vertical_compose_dirac53iH0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2,
                                                int width)
{
    int i;
    TYPE *b0  = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;
    for(i=0; i<width; i++){
        b1[i] = COMPOSE_DIRAC53iH0(b0[i], b1[i], b2[i]);
    }
}

static void RENAME(vertical_compose_dd97iH0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2,
                                             uint8_t *_b3, uint8_t *_b4, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;
    TYPE *b3 = (TYPE *)_b3;
    TYPE *b4 = (TYPE *)_b4;
    for(i=0; i<width; i++){
        b2[i] = COMPOSE_DD97iH0(b0[i], b1[i], b2[i], b3[i], b4[i]);
    }
}

static void RENAME(vertical_compose_dd137iL0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2,
                                              uint8_t *_b3, uint8_t *_b4, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;
    TYPE *b3 = (TYPE *)_b3;
    TYPE *b4 = (TYPE *)_b4;
    for(i=0; i<width; i++){
        b2[i] = COMPOSE_DD137iL0(b0[i], b1[i], b2[i], b3[i], b4[i]);
    }
}

static void RENAME(vertical_compose_haar)(uint8_t *_b0, uint8_t *_b1, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;

    for (i = 0; i < width; i++) {
        b0[i] = COMPOSE_HAARiL0(b0[i], b1[i]);
        b1[i] = COMPOSE_HAARiH0(b1[i], b0[i]);
    }
}

static void RENAME(vertical_compose_fidelityiH0)(uint8_t *_dst, uint8_t *_b[8], int width)
{
    int i;
    TYPE *dst = (TYPE *)_dst;
    TYPE *b0  = (TYPE *)_b[0];
    TYPE *b1  = (TYPE *)_b[1];
    TYPE *b2  = (TYPE *)_b[2];
    TYPE *b3  = (TYPE *)_b[3];
    TYPE *b4  = (TYPE *)_b[4];
    TYPE *b5  = (TYPE *)_b[5];
    TYPE *b6  = (TYPE *)_b[6];
    TYPE *b7  = (TYPE *)_b[7];
    for(i=0; i<width; i++){
        dst[i] = COMPOSE_FIDELITYiH0(b0[i], b1[i], b2[i], b3[i], dst[i], b4[i], b5[i], b6[i], b7[i]);
    }
}

static void RENAME(vertical_compose_fidelityiL0)(uint8_t *_dst, uint8_t *_b[8], int width)
{
    int i;
    TYPE *dst = (TYPE *)_dst;
    TYPE *b0  = (TYPE *)_b[0];
    TYPE *b1  = (TYPE *)_b[1];
    TYPE *b2  = (TYPE *)_b[2];
    TYPE *b3  = (TYPE *)_b[3];
    TYPE *b4  = (TYPE *)_b[4];
    TYPE *b5  = (TYPE *)_b[5];
    TYPE *b6  = (TYPE *)_b[6];
    TYPE *b7  = (TYPE *)_b[7];

    for(i=0; i<width; i++){
        dst[i] = COMPOSE_FIDELITYiL0(b0[i], b1[i], b2[i], b3[i], dst[i], b4[i], b5[i], b6[i], b7[i]);
    }
}

static void RENAME(vertical_compose_daub97iH0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;

    for(i=0; i<width; i++){
        b1[i] = COMPOSE_DAUB97iH0(b0[i], b1[i], b2[i]);
    }
}

static void RENAME(vertical_compose_daub97iH1)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;

    for(i=0; i<width; i++){
        b1[i] = COMPOSE_DAUB97iH1(b0[i], b1[i], b2[i]);
    }
}

static void RENAME(vertical_compose_daub97iL0)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;

    for(i=0; i<width; i++){
        b1[i] = COMPOSE_DAUB97iL0(b0[i], b1[i], b2[i]);
    }
}

static void RENAME(vertical_compose_daub97iL1)(uint8_t *_b0, uint8_t *_b1, uint8_t *_b2, int width)
{
    int i;
    TYPE *b0 = (TYPE *)_b0;
    TYPE *b1 = (TYPE *)_b1;
    TYPE *b2 = (TYPE *)_b2;

    for(i=0; i<width; i++){
        b1[i] = COMPOSE_DAUB97iL1(b0[i], b1[i], b2[i]);
    }
}

static void RENAME(spatial_compose_dd97i_dy)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_3tap vertical_compose_l0 = (void*)d->vertical_compose_l0;
    vertical_compose_5tap vertical_compose_h0 = (void*)d->vertical_compose_h0;
    DWTCompose *cs = d->cs + level;

    int i, y = cs->y;
    uint8_t *b[8];
    for (i = 0; i < 6; i++)
        b[i] = cs->b[i];
    b[6] = d->buffer + av_clip(y+5, 0, height-2)*stride;
    b[7] = d->buffer + av_clip(y+6, 1, height-1)*stride;

    if(y+5<(unsigned)height) vertical_compose_l0(      b[5], b[6], b[7],       width);
    if(y+1<(unsigned)height) vertical_compose_h0(b[0], b[2], b[3], b[4], b[6], width);

    if(y-1<(unsigned)height) d->horizontal_compose(b[0], d->temp, width);
    if(y+0<(unsigned)height) d->horizontal_compose(b[1], d->temp, width);

    for (i = 0; i < 6; i++)
        cs->b[i] = b[i+2];
    cs->y += 2;
}

static void RENAME(spatial_compose_dirac53i_dy)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_3tap vertical_compose_l0 = (void*)d->vertical_compose_l0;
    vertical_compose_3tap vertical_compose_h0 = (void*)d->vertical_compose_h0;
    DWTCompose *cs = d->cs + level;

    int y= cs->y;
    uint8_t *b[4] = { cs->b[0], cs->b[1] };
    b[2] = d->buffer + avpriv_mirror(y+1, height-1)*stride;
    b[3] = d->buffer + avpriv_mirror(y+2, height-1)*stride;

    if(y+1<(unsigned)height) vertical_compose_l0(b[1], b[2], b[3], width);
    if(y+0<(unsigned)height) vertical_compose_h0(b[0], b[1], b[2], width);

    if(y-1<(unsigned)height) d->horizontal_compose(b[0], d->temp, width);
    if(y+0<(unsigned)height) d->horizontal_compose(b[1], d->temp, width);

    cs->b[0] = b[2];
    cs->b[1] = b[3];
    cs->y += 2;
}

static void RENAME(spatial_compose_dd137i_dy)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_5tap vertical_compose_l0 = (void*)d->vertical_compose_l0;
    vertical_compose_5tap vertical_compose_h0 = (void*)d->vertical_compose_h0;
    DWTCompose *cs = d->cs + level;

    int i, y = cs->y;
    uint8_t *b[10];
    for (i = 0; i < 8; i++)
        b[i] = cs->b[i];
    b[8] = d->buffer + av_clip(y+7, 0, height-2)*stride;
    b[9] = d->buffer + av_clip(y+8, 1, height-1)*stride;

    if(y+5<(unsigned)height) vertical_compose_l0(b[3], b[5], b[6], b[7], b[9], width);
    if(y+1<(unsigned)height) vertical_compose_h0(b[0], b[2], b[3], b[4], b[6], width);

    if(y-1<(unsigned)height) d->horizontal_compose(b[0], d->temp, width);
    if(y+0<(unsigned)height) d->horizontal_compose(b[1], d->temp, width);

    for (i = 0; i < 8; i++)
        cs->b[i] = b[i+2];
    cs->y += 2;
}

// haar makes the assumption that height is even (always true for dirac)
static void RENAME(spatial_compose_haari_dy)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_2tap vertical_compose = (void*)d->vertical_compose;
    int y = d->cs[level].y;
    uint8_t *b0 = d->buffer + (y-1)*stride;
    uint8_t *b1 = d->buffer + (y  )*stride;

    vertical_compose(b0, b1, width);
    d->horizontal_compose(b0, d->temp, width);
    d->horizontal_compose(b1, d->temp, width);

    d->cs[level].y += 2;
}

// Don't do sliced idwt for fidelity; the 9 tap filter makes it a bit annoying
// Fortunately, this filter isn't used in practice.
static void RENAME(spatial_compose_fidelity)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_9tap vertical_compose_l0 = (void*)d->vertical_compose_l0;
    vertical_compose_9tap vertical_compose_h0 = (void*)d->vertical_compose_h0;
    int i, y;
    uint8_t *b[8];

    for (y = 1; y < height; y += 2) {
        for (i = 0; i < 8; i++)
            b[i] = d->buffer + av_clip((y-7 + 2*i), 0, height-2)*stride;
        vertical_compose_h0(d->buffer + y*stride, b, width);
    }

    for (y = 0; y < height; y += 2) {
        for (i = 0; i < 8; i++)
            b[i] = d->buffer + av_clip((y-7 + 2*i), 1, height-1)*stride;
        vertical_compose_l0(d->buffer + y*stride, b, width);
    }

    for (y = 0; y < height; y++)
        d->horizontal_compose(d->buffer + y*stride, d->temp, width);

    d->cs[level].y = height+1;
}

static void RENAME(spatial_compose_daub97i_dy)(DWTContext *d, int level, int width, int height, int stride)
{
    vertical_compose_3tap vertical_compose_l0 = (void*)d->vertical_compose_l0;
    vertical_compose_3tap vertical_compose_h0 = (void*)d->vertical_compose_h0;
    vertical_compose_3tap vertical_compose_l1 = (void*)d->vertical_compose_l1;
    vertical_compose_3tap vertical_compose_h1 = (void*)d->vertical_compose_h1;
    DWTCompose *cs = d->cs + level;

    int i, y = cs->y;
    uint8_t *b[6];
    for (i = 0; i < 4; i++)
        b[i] = cs->b[i];
    b[4] = d->buffer + avpriv_mirror(y+3, height-1)*stride;
    b[5] = d->buffer + avpriv_mirror(y+4, height-1)*stride;

    if(y+3<(unsigned)height) vertical_compose_l1(b[3], b[4], b[5], width);
    if(y+2<(unsigned)height) vertical_compose_h1(b[2], b[3], b[4], width);
    if(y+1<(unsigned)height) vertical_compose_l0(b[1], b[2], b[3], width);
    if(y+0<(unsigned)height) vertical_compose_h0(b[0], b[1], b[2], width);

    if(y-1<(unsigned)height) d->horizontal_compose(b[0], d->temp, width);
    if(y+0<(unsigned)height) d->horizontal_compose(b[1], d->temp, width);

    for (i = 0; i < 4; i++)
        cs->b[i] = b[i+2];
    cs->y += 2;
}

static void RENAME(spatial_compose97i_init)(DWTCompose *cs, uint8_t *buffer, int height, int stride)
{
    cs->b[0] = buffer + avpriv_mirror(-3-1, height-1)*stride;
    cs->b[1] = buffer + avpriv_mirror(-3  , height-1)*stride;
    cs->b[2] = buffer + avpriv_mirror(-3+1, height-1)*stride;
    cs->b[3] = buffer + avpriv_mirror(-3+2, height-1)*stride;
    cs->y = -3;
}

static void RENAME(spatial_compose53i_init)(DWTCompose *cs, uint8_t *buffer, int height, int stride)
{
    cs->b[0] = buffer + avpriv_mirror(-1-1, height-1)*stride;
    cs->b[1] = buffer + avpriv_mirror(-1  , height-1)*stride;
    cs->y = -1;
}

static void RENAME(spatial_compose_dd97i_init)(DWTCompose *cs, uint8_t *buffer, int height, int stride)
{
    cs->b[0] = buffer + av_clip(-5-1, 0, height-2)*stride;
    cs->b[1] = buffer + av_clip(-5  , 1, height-1)*stride;
    cs->b[2] = buffer + av_clip(-5+1, 0, height-2)*stride;
    cs->b[3] = buffer + av_clip(-5+2, 1, height-1)*stride;
    cs->b[4] = buffer + av_clip(-5+3, 0, height-2)*stride;
    cs->b[5] = buffer + av_clip(-5+4, 1, height-1)*stride;
    cs->y = -5;
}

static void RENAME(spatial_compose_dd137i_init)(DWTCompose *cs, uint8_t *buffer, int height, int stride)
{
    cs->b[0] = buffer + av_clip(-5-1, 0, height-2)*stride;
    cs->b[1] = buffer + av_clip(-5  , 1, height-1)*stride;
    cs->b[2] = buffer + av_clip(-5+1, 0, height-2)*stride;
    cs->b[3] = buffer + av_clip(-5+2, 1, height-1)*stride;
    cs->b[4] = buffer + av_clip(-5+3, 0, height-2)*stride;
    cs->b[5] = buffer + av_clip(-5+4, 1, height-1)*stride;
    cs->b[6] = buffer + av_clip(-5+5, 0, height-2)*stride;
    cs->b[7] = buffer + av_clip(-5+6, 1, height-1)*stride;
    cs->y = -5;
}

static int RENAME(ff_spatial_idwt_init)(DWTContext *d, enum dwt_type type)
{
    int level;

    d->temp = (uint8_t *)(((TYPE *)d->temp) + 8);

    for (level = d->decomposition_count - 1; level >= 0; level--){
        int hl = d->height >> level;
        int stride_l = d->stride << level;

        switch(type){
            case DWT_DIRAC_DD9_7:
                RENAME(spatial_compose_dd97i_init)(d->cs+level, d->buffer, hl, stride_l);
                break;
            case DWT_DIRAC_LEGALL5_3:
                RENAME(spatial_compose53i_init)(d->cs+level, d->buffer, hl, stride_l);
                break;
            case DWT_DIRAC_DD13_7:
                RENAME(spatial_compose_dd137i_init)(d->cs+level, d->buffer, hl, stride_l);
                break;
            case DWT_DIRAC_HAAR0:
            case DWT_DIRAC_HAAR1:
                d->cs[level].y = 1;
                break;
            case DWT_DIRAC_DAUB9_7:
                RENAME(spatial_compose97i_init)(d->cs+level, d->buffer, hl, stride_l);
                break;
            default:
                d->cs[level].y = 0;
                break;
        }
    }

    switch (type) {
        case DWT_DIRAC_DD9_7:
            d->spatial_compose = RENAME(spatial_compose_dd97i_dy);
            d->vertical_compose_l0 = (void*)RENAME(vertical_compose53iL0);
            d->vertical_compose_h0 = (void*)RENAME(vertical_compose_dd97iH0);
            d->horizontal_compose = RENAME(horizontal_compose_dd97i);
            d->support = 7;
            break;
        case DWT_DIRAC_LEGALL5_3:
            d->spatial_compose = RENAME(spatial_compose_dirac53i_dy);
            d->vertical_compose_l0 = (void*)RENAME(vertical_compose53iL0);
            d->vertical_compose_h0 = (void*)RENAME(vertical_compose_dirac53iH0);
            d->horizontal_compose = RENAME(horizontal_compose_dirac53i);
            d->support = 3;
            break;
        case DWT_DIRAC_DD13_7:
            d->spatial_compose = RENAME(spatial_compose_dd137i_dy);
            d->vertical_compose_l0 = (void*)RENAME(vertical_compose_dd137iL0);
            d->vertical_compose_h0 = (void*)RENAME(vertical_compose_dd97iH0);
            d->horizontal_compose = RENAME(horizontal_compose_dd137i);
            d->support = 7;
            break;
        case DWT_DIRAC_HAAR0:
        case DWT_DIRAC_HAAR1:
            d->spatial_compose = RENAME(spatial_compose_haari_dy);
            d->vertical_compose = (void*)RENAME(vertical_compose_haar);
            if (type == DWT_DIRAC_HAAR0)
                d->horizontal_compose = RENAME(horizontal_compose_haar0i);
            else
                d->horizontal_compose = RENAME(horizontal_compose_haar1i);
            d->support = 1;
            break;
        case DWT_DIRAC_FIDELITY:
            d->spatial_compose = RENAME(spatial_compose_fidelity);
            d->vertical_compose_l0 = (void*)RENAME(vertical_compose_fidelityiL0);
            d->vertical_compose_h0 = (void*)RENAME(vertical_compose_fidelityiH0);
            d->horizontal_compose = RENAME(horizontal_compose_fidelityi);
            d->support = 0; // not really used
            break;
        case DWT_DIRAC_DAUB9_7:
            d->spatial_compose = RENAME(spatial_compose_daub97i_dy);
            d->vertical_compose_l0 = (void*)RENAME(vertical_compose_daub97iL0);
            d->vertical_compose_h0 = (void*)RENAME(vertical_compose_daub97iH0);
            d->vertical_compose_l1 = (void*)RENAME(vertical_compose_daub97iL1);
            d->vertical_compose_h1 = (void*)RENAME(vertical_compose_daub97iH1);
            d->horizontal_compose = RENAME(horizontal_compose_daub97i);
            d->support = 5;
            break;
        default:
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

#undef RENAME
#undef TYPE
