/*
 * Altivec optimized snow DSP utils
 * Copyright (c) 2006 Luca Barbato <lu_zero@gentoo.org>
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
 *
 *
 */

#include "../dsputil.h"

#include "gcc_fixes.h"
#include "dsputil_altivec.h"
#include "../snow.h"

#undef NDEBUG
#include <assert.h>



//FIXME remove this replication
#define slice_buffer_get_line(slice_buf, line_num) ((slice_buf)->line[line_num] ? (slice_buf)->line[line_num] : slice_buffer_load_line((slice_buf), (line_num)))

static DWTELEM * slice_buffer_load_line(slice_buffer * buf, int line)
{
    int offset;
    DWTELEM * buffer;

//  av_log(NULL, AV_LOG_DEBUG, "Cache hit: %d\n", line);

    assert(buf->data_stack_top >= 0);
//  assert(!buf->line[line]);
    if (buf->line[line])
        return buf->line[line];

    offset = buf->line_width * line;
    buffer = buf->data_stack[buf->data_stack_top];
    buf->data_stack_top--;
    buf->line[line] = buffer;

//  av_log(NULL, AV_LOG_DEBUG, "slice_buffer_load_line: line: %d remaining: %d\n", line, buf->data_stack_top + 1);

    return buffer;
}


//altivec code

void ff_snow_horizontal_compose97i_altivec(DWTELEM *b, int width)
{
    const int w2= (width+1)>>1;
    DECLARE_ALIGNED_16(DWTELEM, temp[(width>>1)]);
    const int w_l= (width>>1);
    const int w_r= w2 - 1;
    int i;
    vector signed int t1, t2, x, y, tmp1, tmp2;
    vector signed int *vbuf, *vtmp;
    vector unsigned char align;



    { // Lift 0
        DWTELEM * const ref = b + w2 - 1;
        DWTELEM b_0 = b[0];
        vbuf = (vector signed int *)b;

        tmp1 = vec_ld (0, ref);
        align = vec_lvsl (0, ref);
        tmp2 = vec_ld (15, ref);
        t1= vec_perm(tmp1, tmp2, align);

        i = 0;

        for (i=0; i<w_l-15; i+=16) {
#if 0
        b[i+0] = b[i+0] - ((3 * (ref[i+0] + ref[i+1]) + 4) >> 3);
        b[i+1] = b[i+1] - ((3 * (ref[i+1] + ref[i+2]) + 4) >> 3);
        b[i+2] = b[i+2] - ((3 * (ref[i+2] + ref[i+3]) + 4) >> 3);
        b[i+3] = b[i+3] - ((3 * (ref[i+3] + ref[i+4]) + 4) >> 3);
#else

        tmp1 = vec_ld (0, ref+4+i);
        tmp2 = vec_ld (15, ref+4+i);

        t2 = vec_perm(tmp1, tmp2, align);

        y = vec_add(t1,vec_sld(t1,t2,4));
        y = vec_add(vec_add(y,y),y);

        tmp1 = vec_ld (0, ref+8+i);

        y = vec_add(y, vec_splat_s32(4));
        y = vec_sra(y, vec_splat_u32(3));

        tmp2 = vec_ld (15, ref+8+i);

        *vbuf = vec_sub(*vbuf, y);

        t1=t2;

        vbuf++;

        t2 = vec_perm(tmp1, tmp2, align);

        y = vec_add(t1,vec_sld(t1,t2,4));
        y = vec_add(vec_add(y,y),y);

        tmp1 = vec_ld (0, ref+12+i);

        y = vec_add(y, vec_splat_s32(4));
        y = vec_sra(y, vec_splat_u32(3));

        tmp2 = vec_ld (15, ref+12+i);

        *vbuf = vec_sub(*vbuf, y);

        t1=t2;

        vbuf++;

        t2 = vec_perm(tmp1, tmp2, align);

        y = vec_add(t1,vec_sld(t1,t2,4));
        y = vec_add(vec_add(y,y),y);

        tmp1 = vec_ld (0, ref+16+i);

        y = vec_add(y, vec_splat_s32(4));
        y = vec_sra(y, vec_splat_u32(3));

        tmp2 = vec_ld (15, ref+16+i);

        *vbuf = vec_sub(*vbuf, y);

        t1=t2;

        t2 = vec_perm(tmp1, tmp2, align);

        y = vec_add(t1,vec_sld(t1,t2,4));
        y = vec_add(vec_add(y,y),y);

        vbuf++;

        y = vec_add(y, vec_splat_s32(4));
        y = vec_sra(y, vec_splat_u32(3));
        *vbuf = vec_sub(*vbuf, y);

        t1=t2;

        vbuf++;

#endif
        }

        snow_horizontal_compose_lift_lead_out(i, b, b, ref, width, w_l, 0, W_DM, W_DO, W_DS);
        b[0] = b_0 - ((W_DM * 2 * ref[1]+W_DO)>>W_DS);
    }

    { // Lift 1
        DWTELEM * const dst = b+w2;

        i = 0;
        for(; (((long)&dst[i]) & 0xF) && i<w_r; i++){
            dst[i] = dst[i] - (b[i] + b[i + 1]);
        }

        align = vec_lvsl(0, b+i);
        tmp1 = vec_ld(0, b+i);
        vbuf = (vector signed int*) (dst + i);
        tmp2 = vec_ld(15, b+i);

        t1 = vec_perm(tmp1, tmp2, align);

        for (; i<w_r-3; i+=4) {

#if 0
            dst[i]   = dst[i]   - (b[i]   + b[i + 1]);
            dst[i+1] = dst[i+1] - (b[i+1] + b[i + 2]);
            dst[i+2] = dst[i+2] - (b[i+2] + b[i + 3]);
            dst[i+3] = dst[i+3] - (b[i+3] + b[i + 4]);
#else

        tmp1 = vec_ld(0, b+4+i);
        tmp2 = vec_ld(15, b+4+i);

        t2 = vec_perm(tmp1, tmp2, align);

        y = vec_add(t1, vec_sld(t1,t2,4));
        *vbuf = vec_sub (*vbuf, y);

        vbuf++;

        t1 = t2;

#endif

        }

        snow_horizontal_compose_lift_lead_out(i, dst, dst, b, width, w_r, 1, W_CM, W_CO, W_CS);
    }

    { // Lift 2
        DWTELEM * const ref = b+w2 - 1;
        DWTELEM b_0 = b[0];
        vbuf= (vector signed int *) b;

        tmp1 = vec_ld (0, ref);
        align = vec_lvsl (0, ref);
        tmp2 = vec_ld (15, ref);
        t1= vec_perm(tmp1, tmp2, align);

        i = 0;
        for (; i<w_l-15; i+=16) {
#if 0
            b[i]   = b[i]   - (((8 -(ref[i]   + ref[i+1])) - (b[i]  <<2)) >> 4);
            b[i+1] = b[i+1] - (((8 -(ref[i+1] + ref[i+2])) - (b[i+1]<<2)) >> 4);
            b[i+2] = b[i+2] - (((8 -(ref[i+2] + ref[i+3])) - (b[i+2]<<2)) >> 4);
            b[i+3] = b[i+3] - (((8 -(ref[i+3] + ref[i+4])) - (b[i+3]<<2)) >> 4);
#else
            tmp1 = vec_ld (0, ref+4+i);
            tmp2 = vec_ld (15, ref+4+i);

            t2 = vec_perm(tmp1, tmp2, align);

            y = vec_add(t1,vec_sld(t1,t2,4));
            y = vec_sub(vec_splat_s32(8),y);

            tmp1 = vec_ld (0, ref+8+i);

            x = vec_sl(*vbuf,vec_splat_u32(2));
            y = vec_sra(vec_sub(y,x),vec_splat_u32(4));

            tmp2 = vec_ld (15, ref+8+i);

            *vbuf = vec_sub( *vbuf, y);

            t1 = t2;

            vbuf++;

            t2 = vec_perm(tmp1, tmp2, align);

            y = vec_add(t1,vec_sld(t1,t2,4));
            y = vec_sub(vec_splat_s32(8),y);

            tmp1 = vec_ld (0, ref+12+i);

            x = vec_sl(*vbuf,vec_splat_u32(2));
            y = vec_sra(vec_sub(y,x),vec_splat_u32(4));

            tmp2 = vec_ld (15, ref+12+i);

            *vbuf = vec_sub( *vbuf, y);

            t1 = t2;

            vbuf++;

            t2 = vec_perm(tmp1, tmp2, align);

            y = vec_add(t1,vec_sld(t1,t2,4));
            y = vec_sub(vec_splat_s32(8),y);

            tmp1 = vec_ld (0, ref+16+i);

            x = vec_sl(*vbuf,vec_splat_u32(2));
            y = vec_sra(vec_sub(y,x),vec_splat_u32(4));

            tmp2 = vec_ld (15, ref+16+i);

            *vbuf = vec_sub( *vbuf, y);

            t1 = t2;

            vbuf++;

            t2 = vec_perm(tmp1, tmp2, align);

            y = vec_add(t1,vec_sld(t1,t2,4));
            y = vec_sub(vec_splat_s32(8),y);

            t1 = t2;

            x = vec_sl(*vbuf,vec_splat_u32(2));
            y = vec_sra(vec_sub(y,x),vec_splat_u32(4));
            *vbuf = vec_sub( *vbuf, y);

            vbuf++;

#endif
        }

        snow_horizontal_compose_liftS_lead_out(i, b, b, ref, width, w_l);
        b[0] = b_0 - (((-2 * ref[1] + W_BO) - 4 * b_0) >> W_BS);
    }

    { // Lift 3
        DWTELEM * const src = b+w2;

        vbuf = (vector signed int *)b;
        vtmp = (vector signed int *)temp;

        i = 0;
        align = vec_lvsl(0, src);

        for (; i<w_r-3; i+=4) {
#if 0
            temp[i] = src[i] - ((-3*(b[i] + b[i+1]))>>1);
            temp[i+1] = src[i+1] - ((-3*(b[i+1] + b[i+2]))>>1);
            temp[i+2] = src[i+2] - ((-3*(b[i+2] + b[i+3]))>>1);
            temp[i+3] = src[i+3] - ((-3*(b[i+3] + b[i+4]))>>1);
#else
            tmp1 = vec_ld(0,src+i);
            t1 = vec_add(vbuf[0],vec_sld(vbuf[0],vbuf[1],4));
            tmp2 = vec_ld(15,src+i);
            t1 = vec_sub(vec_splat_s32(0),t1); //bad!
            t1 = vec_add(t1,vec_add(t1,t1));
            t2 = vec_perm(tmp1 ,tmp2 ,align);
            t1 = vec_sra(t1,vec_splat_u32(1));
            vbuf++;
            *vtmp = vec_sub(t2,t1);
            vtmp++;

#endif

        }

        snow_horizontal_compose_lift_lead_out(i, temp, src, b, width, w_r, 1, -3, 0, 1);
    }

    {
    //Interleave
        int a;
        vector signed int *t = (vector signed int *)temp,
                          *v = (vector signed int *)b;

        snow_interleave_line_header(&i, width, b, temp);

        for (; (i & 0xE) != 0xE; i-=2){
            b[i+1] = temp[i>>1];
            b[i] = b[i>>1];
        }
        for (i-=14; i>=0; i-=16){
           a=i/4;

           v[a+3]=vec_mergel(v[(a>>1)+1],t[(a>>1)+1]);
           v[a+2]=vec_mergeh(v[(a>>1)+1],t[(a>>1)+1]);
           v[a+1]=vec_mergel(v[a>>1],t[a>>1]);
           v[a]=vec_mergeh(v[a>>1],t[a>>1]);

        }

    }
}

void ff_snow_vertical_compose97i_altivec(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width)
{
    int i, w4 = width/4;
    vector signed int *v0, *v1,*v2,*v3,*v4,*v5;
    vector signed int t1, t2;

    v0=(vector signed int *)b0;
    v1=(vector signed int *)b1;
    v2=(vector signed int *)b2;
    v3=(vector signed int *)b3;
    v4=(vector signed int *)b4;
    v5=(vector signed int *)b5;

    for (i=0; i< w4;i++)
    {

    #if 0
        b4[i] -= (3*(b3[i] + b5[i])+4)>>3;
        b3[i] -= ((b2[i] + b4[i]));
        b2[i] += ((b1[i] + b3[i])+4*b2[i]+8)>>4;
        b1[i] += (3*(b0[i] + b2[i]))>>1;
    #else
        t1 = vec_add(v3[i], v5[i]);
        t2 = vec_add(t1, vec_add(t1,t1));
        t1 = vec_add(t2, vec_splat_s32(4));
        v4[i] = vec_sub(v4[i], vec_sra(t1,vec_splat_u32(3)));

        v3[i] = vec_sub(v3[i], vec_add(v2[i], v4[i]));

        t1 = vec_add(vec_splat_s32(8), vec_add(v1[i], v3[i]));
        t2 = vec_sl(v2[i], vec_splat_u32(2));
        v2[i] = vec_add(v2[i], vec_sra(vec_add(t1,t2),vec_splat_u32(4)));
        t1 = vec_add(v0[i], v2[i]);
        t2 = vec_add(t1, vec_add(t1,t1));
        v1[i] = vec_add(v1[i], vec_sra(t2,vec_splat_u32(1)));

    #endif
    }

    for(i*=4; i < width; i++)
    {
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

#define LOAD_BLOCKS \
            tmp1 = vec_ld(0, &block[3][y*src_stride]);\
            align = vec_lvsl(0, &block[3][y*src_stride]);\
            tmp2 = vec_ld(15, &block[3][y*src_stride]);\
\
            b3 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, &block[2][y*src_stride]);\
            align = vec_lvsl(0, &block[2][y*src_stride]);\
            tmp2 = vec_ld(15, &block[2][y*src_stride]);\
\
            b2 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, &block[1][y*src_stride]);\
            align = vec_lvsl(0, &block[1][y*src_stride]);\
            tmp2 = vec_ld(15, &block[1][y*src_stride]);\
\
            b1 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, &block[0][y*src_stride]);\
            align = vec_lvsl(0, &block[0][y*src_stride]);\
            tmp2 = vec_ld(15, &block[0][y*src_stride]);\
\
            b0 = vec_perm(tmp1,tmp2,align);

#define LOAD_OBMCS \
            tmp1 = vec_ld(0, obmc1);\
            align = vec_lvsl(0, obmc1);\
            tmp2 = vec_ld(15, obmc1);\
\
            ob1 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, obmc2);\
            align = vec_lvsl(0, obmc2);\
            tmp2 = vec_ld(15, obmc2);\
\
            ob2 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, obmc3);\
            align = vec_lvsl(0, obmc3);\
            tmp2 = vec_ld(15, obmc3);\
\
            ob3 = vec_perm(tmp1,tmp2,align);\
\
            tmp1 = vec_ld(0, obmc4);\
            align = vec_lvsl(0, obmc4);\
            tmp2 = vec_ld(15, obmc4);\
\
            ob4 = vec_perm(tmp1,tmp2,align);

/* interleave logic
 * h1 <- [ a,b,a,b, a,b,a,b, a,b,a,b, a,b,a,b ]
 * h2 <- [ c,d,c,d, c,d,c,d, c,d,c,d, c,d,c,d ]
 * h  <- [ a,b,c,d, a,b,c,d, a,b,c,d, a,b,c,d ]
 */

#define STEPS_0_1\
            h1 = (vector unsigned short)\
                 vec_mergeh(ob1, ob2);\
\
            h2 = (vector unsigned short)\
                 vec_mergeh(ob3, ob4);\
\
            ih = (vector unsigned char)\
                 vec_mergeh(h1,h2);\
\
            l1 = (vector unsigned short) vec_mergeh(b3, b2);\
\
            ih1 = (vector unsigned char) vec_mergel(h1, h2);\
\
            l2 = (vector unsigned short) vec_mergeh(b1, b0);\
\
            il = (vector unsigned char) vec_mergeh(l1, l2);\
\
            v[0] = (vector signed int) vec_msum(ih, il, vec_splat_u32(0));\
\
            il1 = (vector unsigned char) vec_mergel(l1, l2);\
\
            v[1] = (vector signed int) vec_msum(ih1, il1, vec_splat_u32(0));

#define FINAL_STEP_SCALAR\
        for(x=0; x<b_w; x++)\
            if(add){\
                vbuf[x] += dst[x + src_x];\
                vbuf[x] = (vbuf[x] + (1<<(FRAC_BITS-1))) >> FRAC_BITS;\
                if(vbuf[x]&(~255)) vbuf[x]= ~(vbuf[x]>>31);\
                dst8[x + y*src_stride] = vbuf[x];\
            }else{\
                dst[x + src_x] -= vbuf[x];\
            }

static void inner_add_yblock_bw_8_obmc_16_altivec(uint8_t *obmc,
                                             const int obmc_stride,
                                             uint8_t * * block, int b_w,
                                             int b_h, int src_x, int src_y,
                                             int src_stride, slice_buffer * sb,
                                             int add, uint8_t * dst8)
{
    int y, x;
    DWTELEM * dst;
    vector unsigned short h1, h2, l1, l2;
    vector unsigned char ih, il, ih1, il1, tmp1, tmp2, align;
    vector unsigned char b0,b1,b2,b3;
    vector unsigned char ob1,ob2,ob3,ob4;

    DECLARE_ALIGNED_16(int, vbuf[16]);
    vector signed int *v = (vector signed int *)vbuf, *d;

    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride

        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);

        dst = slice_buffer_get_line(sb, src_y + y);
        d = (vector signed int *)(dst + src_x);

//FIXME i could avoid some loads!

        // load blocks
        LOAD_BLOCKS

        // load obmcs
        LOAD_OBMCS

        // steps 0 1
        STEPS_0_1

        FINAL_STEP_SCALAR

       }

}

#define STEPS_2_3\
            h1 = (vector unsigned short) vec_mergel(ob1, ob2);\
\
            h2 = (vector unsigned short) vec_mergel(ob3, ob4);\
\
            ih = (vector unsigned char) vec_mergeh(h1,h2);\
\
            l1 = (vector unsigned short) vec_mergel(b3, b2);\
\
            l2 = (vector unsigned short) vec_mergel(b1, b0);\
\
            ih1 = (vector unsigned char) vec_mergel(h1,h2);\
\
            il = (vector unsigned char) vec_mergeh(l1,l2);\
\
            v[2] = (vector signed int) vec_msum(ih, il, vec_splat_u32(0));\
\
            il1 = (vector unsigned char) vec_mergel(l1,l2);\
\
            v[3] = (vector signed int) vec_msum(ih1, il1, vec_splat_u32(0));


static void inner_add_yblock_bw_16_obmc_32_altivec(uint8_t *obmc,
                                             const int obmc_stride,
                                             uint8_t * * block, int b_w,
                                             int b_h, int src_x, int src_y,
                                             int src_stride, slice_buffer * sb,
                                             int add, uint8_t * dst8)
{
    int y, x;
    DWTELEM * dst;
    vector unsigned short h1, h2, l1, l2;
    vector unsigned char ih, il, ih1, il1, tmp1, tmp2, align;
    vector unsigned char b0,b1,b2,b3;
    vector unsigned char ob1,ob2,ob3,ob4;
    DECLARE_ALIGNED_16(int, vbuf[b_w]);
    vector signed int *v = (vector signed int *)vbuf, *d;

    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride

        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);

        dst = slice_buffer_get_line(sb, src_y + y);
        d = (vector signed int *)(dst + src_x);

        // load blocks
        LOAD_BLOCKS

        // load obmcs
        LOAD_OBMCS

        // steps 0 1 2 3
        STEPS_0_1

        STEPS_2_3

        FINAL_STEP_SCALAR

    }
}

#define FINAL_STEP_VEC \
\
    if(add)\
        {\
            for(x=0; x<b_w/4; x++)\
            {\
                v[x] = vec_add(v[x], d[x]);\
                v[x] = vec_sra(vec_add(v[x],\
                                       vec_sl( vec_splat_s32(1),\
                                               vec_splat_u32(7))),\
                               vec_splat_u32(8));\
\
                mask = (vector bool int) vec_sl((vector signed int)\
                        vec_cmpeq(v[x],v[x]),vec_splat_u32(8));\
                mask = (vector bool int) vec_and(v[x],vec_nor(mask,mask));\
\
                mask = (vector bool int)\
                        vec_cmpeq((vector signed int)mask,\
                                  (vector signed int)vec_splat_u32(0));\
\
                vs = vec_sra(v[x],vec_splat_u32(8));\
                vs = vec_sra(v[x],vec_splat_u32(8));\
                vs = vec_sra(v[x],vec_splat_u32(15));\
\
                vs = vec_nor(vs,vs);\
\
                v[x]= vec_sel(v[x],vs,mask);\
            }\
\
            for(x=0; x<b_w; x++)\
                dst8[x + y*src_stride] = vbuf[x];\
\
        }\
         else\
            for(x=0; x<b_w/4; x++)\
                d[x] = vec_sub(d[x], v[x]);

static void inner_add_yblock_a_bw_8_obmc_16_altivec(uint8_t *obmc,
                                             const int obmc_stride,
                                             uint8_t * * block, int b_w,
                                             int b_h, int src_x, int src_y,
                                             int src_stride, slice_buffer * sb,
                                             int add, uint8_t * dst8)
{
    int y, x;
    DWTELEM * dst;
    vector bool int mask;
    vector signed int vs;
    vector unsigned short h1, h2, l1, l2;
    vector unsigned char ih, il, ih1, il1, tmp1, tmp2, align;
    vector unsigned char b0,b1,b2,b3;
    vector unsigned char ob1,ob2,ob3,ob4;

    DECLARE_ALIGNED_16(int, vbuf[16]);
    vector signed int *v = (vector signed int *)vbuf, *d;

    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride

        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);

        dst = slice_buffer_get_line(sb, src_y + y);
        d = (vector signed int *)(dst + src_x);

//FIXME i could avoid some loads!

        // load blocks
        LOAD_BLOCKS

        // load obmcs
        LOAD_OBMCS

        // steps 0 1
        STEPS_0_1

        FINAL_STEP_VEC

       }

}

static void inner_add_yblock_a_bw_16_obmc_32_altivec(uint8_t *obmc,
                                             const int obmc_stride,
                                             uint8_t * * block, int b_w,
                                             int b_h, int src_x, int src_y,
                                             int src_stride, slice_buffer * sb,
                                             int add, uint8_t * dst8)
{
    int y, x;
    DWTELEM * dst;
    vector bool int mask;
    vector signed int vs;
    vector unsigned short h1, h2, l1, l2;
    vector unsigned char ih, il, ih1, il1, tmp1, tmp2, align;
    vector unsigned char b0,b1,b2,b3;
    vector unsigned char ob1,ob2,ob3,ob4;
    DECLARE_ALIGNED_16(int, vbuf[b_w]);
    vector signed int *v = (vector signed int *)vbuf, *d;

    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride

        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);

        dst = slice_buffer_get_line(sb, src_y + y);
        d = (vector signed int *)(dst + src_x);

        // load blocks
        LOAD_BLOCKS

        // load obmcs
        LOAD_OBMCS

        // steps 0 1 2 3
        STEPS_0_1

        STEPS_2_3

        FINAL_STEP_VEC

    }
}


void ff_snow_inner_add_yblock_altivec(uint8_t *obmc, const int obmc_stride,
                                      uint8_t * * block, int b_w, int b_h,
                                      int src_x, int src_y, int src_stride,
                                      slice_buffer * sb, int add,
                                      uint8_t * dst8)
{
    if (src_x&15) {
        if (b_w == 16)
            inner_add_yblock_bw_16_obmc_32_altivec(obmc, obmc_stride, block,
                                                   b_w, b_h, src_x, src_y,
                                                   src_stride, sb, add, dst8);
        else if (b_w == 8)
            inner_add_yblock_bw_8_obmc_16_altivec(obmc, obmc_stride, block,
                                                  b_w, b_h, src_x, src_y,
                                                  src_stride, sb, add, dst8);
        else
            ff_snow_inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,
                                     src_y, src_stride, sb, add, dst8);
    } else {
        if (b_w == 16)
            inner_add_yblock_a_bw_16_obmc_32_altivec(obmc, obmc_stride, block,
                                                     b_w, b_h, src_x, src_y,
                                                     src_stride, sb, add, dst8);
        else if (b_w == 8)
            inner_add_yblock_a_bw_8_obmc_16_altivec(obmc, obmc_stride, block,
                                                    b_w, b_h, src_x, src_y,
                                                    src_stride, sb, add, dst8);
        else
            ff_snow_inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,
                                     src_y, src_stride, sb, add, dst8);
    }
}


void snow_init_altivec(DSPContext* c, AVCodecContext *avctx)
{
        c->horizontal_compose97i = ff_snow_horizontal_compose97i_altivec;
        c->vertical_compose97i = ff_snow_vertical_compose97i_altivec;
        c->inner_add_yblock = ff_snow_inner_add_yblock_altivec;
}
