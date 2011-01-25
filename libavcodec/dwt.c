/*
 * Copyright (C) 2004-2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/attributes.h"
#include "dsputil.h"
#include "dwt.h"

void ff_slice_buffer_init(slice_buffer * buf, int line_count, int max_allocated_lines, int line_width, IDWTELEM * base_buffer)
{
    int i;

    buf->base_buffer = base_buffer;
    buf->line_count = line_count;
    buf->line_width = line_width;
    buf->data_count = max_allocated_lines;
    buf->line = av_mallocz (sizeof(IDWTELEM *) * line_count);
    buf->data_stack = av_malloc (sizeof(IDWTELEM *) * max_allocated_lines);

    for(i = 0; i < max_allocated_lines; i++){
        buf->data_stack[i] = av_malloc (sizeof(IDWTELEM) * line_width);
    }

    buf->data_stack_top = max_allocated_lines - 1;
}

IDWTELEM * ff_slice_buffer_load_line(slice_buffer * buf, int line)
{
    IDWTELEM * buffer;

    assert(buf->data_stack_top >= 0);
//  assert(!buf->line[line]);
    if (buf->line[line])
        return buf->line[line];

    buffer = buf->data_stack[buf->data_stack_top];
    buf->data_stack_top--;
    buf->line[line] = buffer;

    return buffer;
}

void ff_slice_buffer_release(slice_buffer * buf, int line)
{
    IDWTELEM * buffer;

    assert(line >= 0 && line < buf->line_count);
    assert(buf->line[line]);

    buffer = buf->line[line];
    buf->data_stack_top++;
    buf->data_stack[buf->data_stack_top] = buffer;
    buf->line[line] = NULL;
}

void ff_slice_buffer_flush(slice_buffer * buf)
{
    int i;
    for(i = 0; i < buf->line_count; i++){
        if (buf->line[i])
            ff_slice_buffer_release(buf, i);
    }
}

void ff_slice_buffer_destroy(slice_buffer * buf)
{
    int i;
    ff_slice_buffer_flush(buf);

    for(i = buf->data_count - 1; i >= 0; i--){
        av_freep(&buf->data_stack[i]);
    }
    av_freep(&buf->data_stack);
    av_freep(&buf->line);
}

static inline int mirror(int v, int m){
    while((unsigned)v > (unsigned)m){
        v=-v;
        if(v<0) v+= 2*m;
    }
    return v;
}

static av_always_inline void
lift(DWTELEM *dst, DWTELEM *src, DWTELEM *ref,
     int dst_step, int src_step, int ref_step,
     int width, int mul, int add, int shift,
     int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

#define LIFT(src, ref, inv) ((src) + ((inv) ? - (ref) : + (ref)))
    if(mirror_left){
        dst[0] = LIFT(src[0], ((mul*2*ref[0]+add)>>shift), inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFT(src[i*src_step],
                 ((mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add)>>shift),
                 inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFT(src[w*src_step],
                 ((mul*2*ref[w*ref_step]+add)>>shift),
                 inverse);
    }
}

static av_always_inline void
inv_lift(IDWTELEM *dst, IDWTELEM *src, IDWTELEM *ref,
         int dst_step, int src_step, int ref_step,
         int width, int mul, int add, int shift,
         int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

#define LIFT(src, ref, inv) ((src) + ((inv) ? - (ref) : + (ref)))
    if(mirror_left){
        dst[0] = LIFT(src[0], ((mul*2*ref[0]+add)>>shift), inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFT(src[i*src_step],
                 ((mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add)>>shift),
                 inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFT(src[w*src_step],
                 ((mul*2*ref[w*ref_step]+add)>>shift),
                 inverse);
    }
}

#ifndef liftS
static av_always_inline void
liftS(DWTELEM *dst, DWTELEM *src, DWTELEM *ref,
      int dst_step, int src_step, int ref_step,
      int width, int mul, int add, int shift,
      int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    assert(shift == 4);
#define LIFTS(src, ref, inv) \
        ((inv) ? \
            (src) + (((ref) + 4*(src))>>shift): \
            -((-16*(src) + (ref) + add/4 + 1 + (5<<25))/(5*4) - (1<<23)))
    if(mirror_left){
        dst[0] = LIFTS(src[0], mul*2*ref[0]+add, inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFTS(src[i*src_step],
                  mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add,
                  inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFTS(src[w*src_step], mul*2*ref[w*ref_step]+add, inverse);
    }
}
static av_always_inline void
inv_liftS(IDWTELEM *dst, IDWTELEM *src, IDWTELEM *ref,
          int dst_step, int src_step, int ref_step,
          int width, int mul, int add, int shift,
          int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    assert(shift == 4);
#define LIFTS(src, ref, inv) \
    ((inv) ? \
        (src) + (((ref) + 4*(src))>>shift): \
        -((-16*(src) + (ref) + add/4 + 1 + (5<<25))/(5*4) - (1<<23)))
    if(mirror_left){
        dst[0] = LIFTS(src[0], mul*2*ref[0]+add, inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFTS(src[i*src_step],
                  mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add,
                  inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFTS(src[w*src_step], mul*2*ref[w*ref_step]+add, inverse);
    }
}
#endif /* ! liftS */

static void horizontal_decompose53i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int width2= width>>1;
    int x;
    const int w2= (width+1)>>1;

    for(x=0; x<width2; x++){
        temp[x   ]= b[2*x    ];
        temp[x+w2]= b[2*x + 1];
    }
    if(width&1)
        temp[x   ]= b[2*x    ];
#if 0
    {
    int A1,A2,A3,A4;
    A2= temp[1       ];
    A4= temp[0       ];
    A1= temp[0+width2];
    A1 -= (A2 + A4)>>1;
    A4 += (A1 + 1)>>1;
    b[0+width2] = A1;
    b[0       ] = A4;
    for(x=1; x+1<width2; x+=2){
        A3= temp[x+width2];
        A4= temp[x+1     ];
        A3 -= (A2 + A4)>>1;
        A2 += (A1 + A3 + 2)>>2;
        b[x+width2] = A3;
        b[x       ] = A2;

        A1= temp[x+1+width2];
        A2= temp[x+2       ];
        A1 -= (A2 + A4)>>1;
        A4 += (A1 + A3 + 2)>>2;
        b[x+1+width2] = A1;
        b[x+1       ] = A4;
    }
    A3= temp[width-1];
    A3 -= A2;
    A2 += (A1 + A3 + 2)>>2;
    b[width -1] = A3;
    b[width2-1] = A2;
    }
#else
    lift(b+w2, temp+w2, temp, 1, 1, 1, width, -1, 0, 1, 1, 0);
    lift(b   , temp   , b+w2, 1, 1, 1, width,  1, 2, 2, 0, 0);
#endif /* 0 */
}

static void vertical_decompose53iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i])>>1;
    }
}

static void vertical_decompose53iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_decompose53i(DWTELEM *buffer, int width, int height, int stride){
    int y;
    DWTELEM *b0= buffer + mirror(-2-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-2  , height-1)*stride;

    for(y=-2; y<height; y+=2){
        DWTELEM *b2= buffer + mirror(y+1, height-1)*stride;
        DWTELEM *b3= buffer + mirror(y+2, height-1)*stride;

        if(y+1<(unsigned)height) horizontal_decompose53i(b2, width);
        if(y+2<(unsigned)height) horizontal_decompose53i(b3, width);

        if(y+1<(unsigned)height) vertical_decompose53iH0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_decompose53iL0(b0, b1, b2, width);

        b0=b2;
        b1=b3;
    }
}

static void horizontal_decompose97i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int w2= (width+1)>>1;

    lift (temp+w2, b    +1, b      , 1, 2, 2, width,  W_AM, W_AO, W_AS, 1, 1);
    liftS(temp   , b      , temp+w2, 1, 2, 1, width,  W_BM, W_BO, W_BS, 0, 0);
    lift (b   +w2, temp+w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 0);
    lift (b      , temp   , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 0);
}


static void vertical_decompose97iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_decompose97iH1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
    }
}

static void vertical_decompose97iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] -= (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] = (16*4*b1[i] - 4*(b0[i] + b2[i]) + W_BO*5 + (5<<27)) / (5*16) - (1<<23);
#endif
    }
}

static void vertical_decompose97iL1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

static void spatial_decompose97i(DWTELEM *buffer, int width, int height, int stride){
    int y;
    DWTELEM *b0= buffer + mirror(-4-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-4  , height-1)*stride;
    DWTELEM *b2= buffer + mirror(-4+1, height-1)*stride;
    DWTELEM *b3= buffer + mirror(-4+2, height-1)*stride;

    for(y=-4; y<height; y+=2){
        DWTELEM *b4= buffer + mirror(y+3, height-1)*stride;
        DWTELEM *b5= buffer + mirror(y+4, height-1)*stride;

        if(y+3<(unsigned)height) horizontal_decompose97i(b4, width);
        if(y+4<(unsigned)height) horizontal_decompose97i(b5, width);

        if(y+3<(unsigned)height) vertical_decompose97iH0(b3, b4, b5, width);
        if(y+2<(unsigned)height) vertical_decompose97iL0(b2, b3, b4, width);
        if(y+1<(unsigned)height) vertical_decompose97iH1(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_decompose97iL1(b0, b1, b2, width);

        b0=b2;
        b1=b3;
        b2=b4;
        b3=b5;
    }
}

void ff_spatial_dwt(DWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;

    for(level=0; level<decomposition_count; level++){
        switch(type){
        case DWT_97: spatial_decompose97i(buffer, width>>level, height>>level, stride<<level); break;
        case DWT_53: spatial_decompose53i(buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

static void horizontal_compose53i(IDWTELEM *b, int width){
    IDWTELEM temp[width];
    const int width2= width>>1;
    const int w2= (width+1)>>1;
    int x;

    for(x=0; x<width2; x++){
        temp[2*x    ]= b[x   ];
        temp[2*x + 1]= b[x+w2];
    }
    if(width&1)
        temp[2*x    ]= b[x   ];

    b[0] = temp[0] - ((temp[1]+1)>>1);
    for(x=2; x<width-1; x+=2){
        b[x  ] = temp[x  ] - ((temp[x-1] + temp[x+1]+2)>>2);
        b[x-1] = temp[x-1] + ((b   [x-2] + b   [x  ]+1)>>1);
    }
    if(width&1){
        b[x  ] = temp[x  ] - ((temp[x-1]+1)>>1);
        b[x-1] = temp[x-1] + ((b   [x-2] + b  [x  ]+1)>>1);
    }else
        b[x-1] = temp[x-1] + b[x-2];
}

static void vertical_compose53iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i])>>1;
    }
}

static void vertical_compose53iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_compose53i_buffered_init(DWTCompose *cs, slice_buffer * sb, int height, int stride_line){
    cs->b0 = slice_buffer_get_line(sb, mirror(-1-1, height-1) * stride_line);
    cs->b1 = slice_buffer_get_line(sb, mirror(-1  , height-1) * stride_line);
    cs->y = -1;
}

static void spatial_compose53i_init(DWTCompose *cs, IDWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-1-1, height-1)*stride;
    cs->b1 = buffer + mirror(-1  , height-1)*stride;
    cs->y = -1;
}

static void spatial_compose53i_dy_buffered(DWTCompose *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y= cs->y;

    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= slice_buffer_get_line(sb, mirror(y+1, height-1) * stride_line);
    IDWTELEM *b3= slice_buffer_get_line(sb, mirror(y+2, height-1) * stride_line);

    if(y+1<(unsigned)height && y<(unsigned)height){
        int x;

        for(x=0; x<width; x++){
            b2[x] -= (b1[x] + b3[x] + 2)>>2;
            b1[x] += (b0[x] + b2[x])>>1;
        }
    }else{
        if(y+1<(unsigned)height) vertical_compose53iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose53iH0(b0, b1, b2, width);
    }

        if(y-1<(unsigned)height) horizontal_compose53i(b0, width);
        if(y+0<(unsigned)height) horizontal_compose53i(b1, width);

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void spatial_compose53i_dy(DWTCompose *cs, IDWTELEM *buffer, int width, int height, int stride){
    int y= cs->y;
    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= buffer + mirror(y+1, height-1)*stride;
    IDWTELEM *b3= buffer + mirror(y+2, height-1)*stride;

        if(y+1<(unsigned)height) vertical_compose53iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose53iH0(b0, b1, b2, width);

        if(y-1<(unsigned)height) horizontal_compose53i(b0, width);
        if(y+0<(unsigned)height) horizontal_compose53i(b1, width);

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void av_unused spatial_compose53i(IDWTELEM *buffer, int width, int height, int stride){
    DWTCompose cs;
    spatial_compose53i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose53i_dy(&cs, buffer, width, height, stride);
}


void ff_snow_horizontal_compose97i(IDWTELEM *b, int width){
    IDWTELEM temp[width];
    const int w2= (width+1)>>1;

#if 0 //maybe more understadable but slower
    inv_lift (temp   , b      , b   +w2, 2, 1, 1, width,  W_DM, W_DO, W_DS, 0, 1);
    inv_lift (temp+1 , b   +w2, temp   , 2, 1, 2, width,  W_CM, W_CO, W_CS, 1, 1);

    inv_liftS(b      , temp   , temp+1 , 2, 2, 2, width,  W_BM, W_BO, W_BS, 0, 1);
    inv_lift (b+1    , temp+1 , b      , 2, 2, 2, width,  W_AM, W_AO, W_AS, 1, 0);
#else
    int x;
    temp[0] = b[0] - ((3*b[w2]+2)>>2);
    for(x=1; x<(width>>1); x++){
        temp[2*x  ] = b[x     ] - ((3*(b   [x+w2-1] + b[x+w2])+4)>>3);
        temp[2*x-1] = b[x+w2-1] - temp[2*x-2] - temp[2*x];
    }
    if(width&1){
        temp[2*x  ] = b[x     ] - ((3*b   [x+w2-1]+2)>>2);
        temp[2*x-1] = b[x+w2-1] - temp[2*x-2] - temp[2*x];
    }else
        temp[2*x-1] = b[x+w2-1] - 2*temp[2*x-2];

    b[0] = temp[0] + ((2*temp[0] + temp[1]+4)>>3);
    for(x=2; x<width-1; x+=2){
        b[x  ] = temp[x  ] + ((4*temp[x  ] + temp[x-1] + temp[x+1]+8)>>4);
        b[x-1] = temp[x-1] + ((3*(b  [x-2] + b   [x  ] ))>>1);
    }
    if(width&1){
        b[x  ] = temp[x  ] + ((2*temp[x  ] + temp[x-1]+4)>>3);
        b[x-1] = temp[x-1] + ((3*(b  [x-2] + b   [x  ] ))>>1);
    }else
        b[x-1] = temp[x-1] + 3*b [x-2];
#endif
}

static void vertical_compose97iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_compose97iH1(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
    }
}

static void vertical_compose97iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] += (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] += (W_BM*(b0[i] + b2[i])+4*b1[i]+W_BO)>>W_BS;
#endif
    }
}

static void vertical_compose97iL1(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

void ff_snow_vertical_compose97i(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, IDWTELEM *b5, int width){
    int i;

    for(i=0; i<width; i++){
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
#ifdef liftS
        b2[i] += (W_BM*(b1[i] + b3[i])+W_BO)>>W_BS;
#else
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
#endif
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void spatial_compose97i_buffered_init(DWTCompose *cs, slice_buffer * sb, int height, int stride_line){
    cs->b0 = slice_buffer_get_line(sb, mirror(-3-1, height-1) * stride_line);
    cs->b1 = slice_buffer_get_line(sb, mirror(-3  , height-1) * stride_line);
    cs->b2 = slice_buffer_get_line(sb, mirror(-3+1, height-1) * stride_line);
    cs->b3 = slice_buffer_get_line(sb, mirror(-3+2, height-1) * stride_line);
    cs->y = -3;
}

static void spatial_compose97i_init(DWTCompose *cs, IDWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-3-1, height-1)*stride;
    cs->b1 = buffer + mirror(-3  , height-1)*stride;
    cs->b2 = buffer + mirror(-3+1, height-1)*stride;
    cs->b3 = buffer + mirror(-3+2, height-1)*stride;
    cs->y = -3;
}

static void spatial_compose97i_dy_buffered(DWTContext *dsp, DWTCompose *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y = cs->y;

    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= cs->b2;
    IDWTELEM *b3= cs->b3;
    IDWTELEM *b4= slice_buffer_get_line(sb, mirror(y + 3, height - 1) * stride_line);
    IDWTELEM *b5= slice_buffer_get_line(sb, mirror(y + 4, height - 1) * stride_line);

    if(y>0 && y+4<height){
        dsp->vertical_compose97i(b0, b1, b2, b3, b4, b5, width);
    }else{
        if(y+3<(unsigned)height) vertical_compose97iL1(b3, b4, b5, width);
        if(y+2<(unsigned)height) vertical_compose97iH1(b2, b3, b4, width);
        if(y+1<(unsigned)height) vertical_compose97iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose97iH0(b0, b1, b2, width);
    }

    if(y-1<(unsigned)height) dsp->horizontal_compose97i(b0, width);
    if(y+0<(unsigned)height) dsp->horizontal_compose97i(b1, width);

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void spatial_compose97i_dy(DWTCompose *cs, IDWTELEM *buffer, int width, int height, int stride){
    int y = cs->y;
    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= cs->b2;
    IDWTELEM *b3= cs->b3;
    IDWTELEM *b4= buffer + mirror(y+3, height-1)*stride;
    IDWTELEM *b5= buffer + mirror(y+4, height-1)*stride;

    if(y+3<(unsigned)height) vertical_compose97iL1(b3, b4, b5, width);
    if(y+2<(unsigned)height) vertical_compose97iH1(b2, b3, b4, width);
    if(y+1<(unsigned)height) vertical_compose97iL0(b1, b2, b3, width);
    if(y+0<(unsigned)height) vertical_compose97iH0(b0, b1, b2, width);

    if(y-1<(unsigned)height) ff_snow_horizontal_compose97i(b0, width);
    if(y+0<(unsigned)height) ff_snow_horizontal_compose97i(b1, width);

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void av_unused spatial_compose97i(IDWTELEM *buffer, int width, int height, int stride){
    DWTCompose cs;
    spatial_compose97i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose97i_dy(&cs, buffer, width, height, stride);
}

void ff_spatial_idwt_buffered_init(DWTCompose *cs, slice_buffer * sb, int width, int height, int stride_line, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case DWT_97: spatial_compose97i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        case DWT_53: spatial_compose53i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        }
    }
}

void ff_spatial_idwt_buffered_slice(DWTContext *dsp, DWTCompose *cs, slice_buffer * slice_buf, int width, int height, int stride_line, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case DWT_97: spatial_compose97i_dy_buffered(dsp, cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                break;
            case DWT_53: spatial_compose53i_dy_buffered(cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                break;
            }
        }
    }
}

static void ff_spatial_idwt_init(DWTCompose *cs, IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case DWT_97: spatial_compose97i_init(cs+level, buffer, height>>level, stride<<level); break;
        case DWT_53: spatial_compose53i_init(cs+level, buffer, height>>level, stride<<level); break;
        }
    }
}

static void ff_spatial_idwt_slice(DWTCompose *cs, IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case DWT_97: spatial_compose97i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                break;
            case DWT_53: spatial_compose53i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                break;
            }
        }
    }
}

void ff_spatial_idwt(IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
        DWTCompose cs[MAX_DECOMPOSITIONS];
        int y;
        ff_spatial_idwt_init(cs, buffer, width, height, stride, type, decomposition_count);
        for(y=0; y<height; y+=4)
            ff_spatial_idwt_slice(cs, buffer, width, height, stride, type, decomposition_count, y);
}

static inline int w_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int w, int h, int type){
    int s, i, j;
    const int dec_count= w==8 ? 3 : 4;
    int tmp[32*32];
    int level, ori;
    static const int scale[2][2][4][4]={
      {
        {
            // 9/7 8x8 dec=3
            {268, 239, 239, 213},
            {  0, 224, 224, 152},
            {  0, 135, 135, 110},
        },{
            // 9/7 16x16 or 32x32 dec=4
            {344, 310, 310, 280},
            {  0, 320, 320, 228},
            {  0, 175, 175, 136},
            {  0, 129, 129, 102},
        }
      },{
        {
            // 5/3 8x8 dec=3
            {275, 245, 245, 218},
            {  0, 230, 230, 156},
            {  0, 138, 138, 113},
        },{
            // 5/3 16x16 or 32x32 dec=4
            {352, 317, 317, 286},
            {  0, 328, 328, 233},
            {  0, 180, 180, 140},
            {  0, 132, 132, 105},
        }
      }
    };

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j+=4) {
            tmp[32*i+j+0] = (pix1[j+0] - pix2[j+0])<<4;
            tmp[32*i+j+1] = (pix1[j+1] - pix2[j+1])<<4;
            tmp[32*i+j+2] = (pix1[j+2] - pix2[j+2])<<4;
            tmp[32*i+j+3] = (pix1[j+3] - pix2[j+3])<<4;
        }
        pix1 += line_size;
        pix2 += line_size;
    }

    ff_spatial_dwt(tmp, w, h, 32, type, dec_count);

    s=0;
    assert(w==h);
    for(level=0; level<dec_count; level++){
        for(ori= level ? 1 : 0; ori<4; ori++){
            int size= w>>(dec_count-level);
            int sx= (ori&1) ? size : 0;
            int stride= 32<<(dec_count-level);
            int sy= (ori&2) ? stride>>1 : 0;

            for(i=0; i<size; i++){
                for(j=0; j<size; j++){
                    int v= tmp[sx + sy + i*stride + j] * scale[type][dec_count-3][level][ori];
                    s += FFABS(v);
                }
            }
        }
    }
    assert(s>=0);
    return s>>9;
}

static int w53_8_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size,  8, h, 1);
}

static int w97_8_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size,  8, h, 0);
}

static int w53_16_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size, 16, h, 1);
}

static int w97_16_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size, 16, h, 0);
}

int ff_w53_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size, 32, h, 1);
}

int ff_w97_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h){
    return w_c(v, pix1, pix2, line_size, 32, h, 0);
}

void ff_dsputil_init_dwt(DSPContext *c)
{
    c->w53[0]= w53_16_c;
    c->w53[1]= w53_8_c;
    c->w97[0]= w97_16_c;
    c->w97[1]= w97_8_c;
}

void ff_dwt_init(DWTContext *c)
{
    c->vertical_compose97i = ff_snow_vertical_compose97i;
    c->horizontal_compose97i = ff_snow_horizontal_compose97i;
    c->inner_add_yblock = ff_snow_inner_add_yblock;

    if (HAVE_MMX) ff_dwt_init_x86(c);
}
