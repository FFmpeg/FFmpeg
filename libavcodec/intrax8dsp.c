/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
* @file
 *@brief IntraX8 frame subdecoder image manipulation routines
 */

#include "dsputil.h"

/*
area positions, #3 is 1 pixel only, other are 8 pixels
   |66666666|
  3|44444444|55555555|
- -+--------+--------+
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
1 2|XXXXXXXX|
^-start
*/

#define area1 (0)
#define area2 (8)
#define area3 (8+8)
#define area4 (8+8+1)
#define area5 (8+8+1+8)
#define area6 (8+8+1+16)

/**
 Collect statistics and prepare the edge pixels required by the other spatial compensation functions.

 * @param src pointer to the beginning of the processed block
 * @param dst pointer to emu_edge, edge pixels are stored the way other compensation routines do.
 * @param linesize byte offset between 2 vertical pixels in the source image
 * @param range pointer to the variable where the edge pixel range is to be stored (max-min values)
 * @param psum  pointer to the variable where the edge pixel sum is to be stored
 * @param edges Informs this routine that the block is on an image border, so it has to interpolate the missing edge pixels.
                and some of the edge pixels should be interpolated, the flag has the following meaning:
                1   - mb_x==0 - first block in the row, interpolate area #1,#2,#3;
                2   - mb_y==0 - first row, interpolate area #3,#4,#5,#6;
        note:   1|2 - mb_x==mb_y==0 - first block, use 0x80 value for all areas;
                4   - mb_x>= (mb_width-1) last block in the row, interpolate area #5;
*/
static void x8_setup_spatial_compensation(uint8_t *src, uint8_t *dst, int linesize,
           int * range, int * psum,  int edges){
    uint8_t * ptr;
    int sum;
    int i;
    int min_pix,max_pix;
    uint8_t c;

    if((edges&3)==3){
        *psum=0x80*(8+1+8+2);
        *range=0;
        memset(dst,0x80,16+1+16+8);
        //this triggers flat_dc for sure.
        //flat_dc avoids all (other) prediction modes, but requires dc_level decoding.
        return;
    }

    min_pix=256;
    max_pix=-1;

    sum=0;

    if(!(edges&1)){//(mb_x!=0)//there is previous block on this row
        ptr=src-1;//left column, area 2
        for(i=7;i>=0;i--){
            c=*(ptr-1);//area1, same mb as area2, no need to check
            dst[area1+i]=c;
            c=*(ptr);

            sum+=c;
            min_pix=FFMIN(min_pix,c);
            max_pix=FFMAX(max_pix,c);
            dst[area2+i]=c;

            ptr+=linesize;
        }
    }

    if(!(edges&2)){  //(mb_y!=0)//there is row above
        ptr=src-linesize;//top line
        for(i=0;i<8;i++){
            c=*(ptr+i);
            sum+=c;
            min_pix=FFMIN(min_pix, c);
            max_pix=FFMAX(max_pix, c);
        }
        if(edges&4){//last block on the row?
            memset(dst+area5,c,8);//set with last pixel fr
            memcpy(dst+area4, ptr, 8);
        }else{
            memcpy(dst+area4, ptr, 16);//both area4 and 5
        }
        memcpy(dst+area6, ptr-linesize, 8);//area6 always present in the above block
    }
    //now calculate the stuff we need
    if(edges&3){//mb_x==0 || mb_y==0){
        int avg=(sum+4)>>3;
        if(edges&1){ //(mb_x==0) {//implies mb_y!=0
            memset(dst+area1,avg,8+8+1);//areas 1,2 and 3 are averaged
        }else{//implies y==0 x!=0
            memset(dst+area3,avg, 1+16+8);//areas 3, 4,5,6
        }
        sum+=avg*9;
    }else{
        uint8_t c=*(src-1-linesize);//the edge pixel, in the top line and left column
        dst[area3]=c;
        sum+=c;
        //edge pixel is not part of min/max
    }
    (*range) = max_pix - min_pix;
    sum += *(dst+area5) + *(dst+area5+1);
    *psum = sum;
}


static const uint16_t zero_prediction_weights[64*2] = {
    640,  640,  669,  480,  708,  354,  748, 257,  792, 198,  760, 143,  808, 101,  772,  72,
    480,  669,  537,  537,  598,  416,  661, 316,  719, 250,  707, 185,  768, 134,  745,  97,
    354,  708,  416,  598,  488,  488,  564, 388,  634, 317,  642, 241,  716, 179,  706, 132,
    257,  748,  316,  661,  388,  564,  469, 469,  543, 395,  571, 311,  655, 238,  660, 180,
    198,  792,  250,  719,  317,  634,  395, 543,  469, 469,  507, 380,  597, 299,  616, 231,
    161,  855,  206,  788,  266,  710,  340, 623,  411, 548,  455, 455,  548, 366,  576, 288,
    122,  972,  159,  914,  211,  842,  276, 758,  341, 682,  389, 584,  483, 483,  520, 390,
    110, 1172,  144, 1107,  193, 1028,  254, 932,  317, 846,  366, 731,  458, 611,  499, 499
};

static void spatial_compensation_0(uint8_t *src , uint8_t *dst, int linesize){
    int i,j;
    int x,y;
    unsigned int p;//power divided by 2
    int a;
    uint16_t left_sum[2][8];
    uint16_t  top_sum[2][8];
    memset(left_sum,0,2*8*sizeof(uint16_t));
    memset( top_sum,0,2*8*sizeof(uint16_t));

    for(i=0;i<8;i++){
        a=src[area2+7-i]<<4;
        for(j=0;j<8;j++){
            p=abs(i-j);
            left_sum[p&1][j]+= a>>(p>>1);
        }
    }

    for(i=0;i<8;i++){
        a=src[area4+i]<<4;
        for(j=0;j<8;j++){
            p=abs(i-j);
            top_sum[p&1][j]+=   a>>(p>>1);
        }
    }
    for(;i<10;i++){
        a=src[area4+i]<<4;
        for(j=5;j<8;j++){
            p=abs(i-j);
            top_sum[p&1][j]+=   a>>(p>>1);
        }
    }
    for(;i<12;i++){
        a=src[area4+i]<<4;
        for(j=7;j<8;j++){
            p=abs(i-j);
            top_sum[p&1][j]+=   a>>(p>>1);
        }
    }

    for(i=0;i<8;i++){
        top_sum [0][i]+=(top_sum [1][i]*181 + 128 )>>8;//181 is sqrt(2)/2
        left_sum[0][i]+=(left_sum[1][i]*181 + 128 )>>8;
    }
    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x] = (
                      (uint32_t)top_sum [0][x]*zero_prediction_weights[y*16+x*2+0] +
                      (uint32_t)left_sum[0][y]*zero_prediction_weights[y*16+x*2+1] +
                       0x8000
                      )>>16;
        }
        dst+=linesize;
    }
}
static void spatial_compensation_1(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=src[area4 + FFMIN(2*y+x+2, 15) ];
        }
        dst+=linesize;
    }
}
static void spatial_compensation_2(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=src[area4 +1+y+x];
        }
        dst+=linesize;
    }
}
static void spatial_compensation_3(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=src[area4 +((y+1)>>1)+x];
        }
        dst+=linesize;
    }
}
static void spatial_compensation_4(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=( src[area4+x] + src[area6+x] + 1 )>>1;
        }
        dst+=linesize;
    }
}
static void spatial_compensation_5(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            if(2*x-y<0){
                dst[x]=src[area2+9+2*x-y];
            }else{
                dst[x]=src[area4 +x-((y+1)>>1)];
            }
        }
        dst+=linesize;
    }
}
static void spatial_compensation_6(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=src[area3+x-y];
        }
        dst+=linesize;
    }
}
static void spatial_compensation_7(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            if(x-2*y>0){
                dst[x]=( src[area3-1+x-2*y] + src[area3+x-2*y] + 1)>>1;
            }else{
                dst[x]=src[area2+8-y +(x>>1)];
            }
        }
        dst+=linesize;
    }
}
static void spatial_compensation_8(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=( src[area1+7-y] + src[area2+7-y] + 1 )>>1;
        }
        dst+=linesize;
    }
}
static void spatial_compensation_9(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=src[area2+6-FFMIN(x+y,6)];
        }
        dst+=linesize;
    }
}
static void spatial_compensation_10(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=(src[area2+7-y]*(8-x)+src[area4+x]*x+4)>>3;
        }
        dst+=linesize;
    }
}
static void spatial_compensation_11(uint8_t *src , uint8_t *dst, int linesize){
    int x,y;

    for(y=0;y<8;y++){
        for(x=0;x<8;x++){
            dst[x]=(src[area2+7-y]*y+src[area4+x]*(8-y)+4)>>3;
        }
        dst+=linesize;
    }
}

static void x8_loop_filter(uint8_t * ptr, const int a_stride, const int b_stride, int quant){
    int i,t;
    int p0,p1,p2,p3,p4,p5,p6,p7,p8,p9;
    int ql=(quant+10)>>3;

    for(i=0; i<8; i++,ptr+=b_stride){
        p0=ptr[-5*a_stride];
        p1=ptr[-4*a_stride];
        p2=ptr[-3*a_stride];
        p3=ptr[-2*a_stride];
        p4=ptr[-1*a_stride];
        p5=ptr[ 0         ];
        p6=ptr[ 1*a_stride];
        p7=ptr[ 2*a_stride];
        p8=ptr[ 3*a_stride];
        p9=ptr[ 4*a_stride];

        t=
            (FFABS(p1-p2) <= ql) +
            (FFABS(p2-p3) <= ql) +
            (FFABS(p3-p4) <= ql) +
            (FFABS(p4-p5) <= ql);
        if(t>0){//You need at least 1 to be able to reach a total score of 6.
            t+=
                (FFABS(p5-p6) <= ql) +
                (FFABS(p6-p7) <= ql) +
                (FFABS(p7-p8) <= ql) +
                (FFABS(p8-p9) <= ql) +
                (FFABS(p0-p1) <= ql);
            if(t>=6){
                int min,max;

                min=max=p1;
                min=FFMIN(min,p3); max=FFMAX(max,p3);
                min=FFMIN(min,p5); max=FFMAX(max,p5);
                min=FFMIN(min,p8); max=FFMAX(max,p8);
                if(max-min<2*quant){//early stop
                    min=FFMIN(min,p2); max=FFMAX(max,p2);
                    min=FFMIN(min,p4); max=FFMAX(max,p4);
                    min=FFMIN(min,p6); max=FFMAX(max,p6);
                    min=FFMIN(min,p7); max=FFMAX(max,p7);
                    if(max-min<2*quant){
                        ptr[-2*a_stride]=(4*p2 + 3*p3 + 1*p7 + 4)>>3;
                        ptr[-1*a_stride]=(3*p2 + 3*p4 + 2*p7 + 4)>>3;
                        ptr[ 0         ]=(2*p2 + 3*p5 + 3*p7 + 4)>>3;
                        ptr[ 1*a_stride]=(1*p2 + 3*p6 + 4*p7 + 4)>>3;
                        continue;
                    };
                }
            }
        }
        {
            int x,x0,x1,x2;
            int m;

            x0 =   (2*p3 - 5*p4 + 5*p5 - 2*p6 + 4)>>3;
            if(FFABS(x0) < quant){
                x1=(2*p1 - 5*p2 + 5*p3 - 2*p4 + 4)>>3;
                x2=(2*p5 - 5*p6 + 5*p7 - 2*p8 + 4)>>3;

                x=FFABS(x0) - FFMIN( FFABS(x1), FFABS(x2) );
                m=p4-p5;

                if( x > 0 && (m^x0) <0){
                    int32_t sign;

                    sign=m>>31;
                    m=(m^sign)-sign;//abs(m)
                    m>>=1;

                    x=(5*x)>>3;

                    if(x>m) x=m;

                    x=(x^sign)-sign;

                    ptr[-1*a_stride] -= x;
                    ptr[ 0]          += x;
                }
            }
        }
    }
}

static void x8_h_loop_filter(uint8_t *src, int stride, int qscale){
    x8_loop_filter(src, stride, 1, qscale);
}

static void x8_v_loop_filter(uint8_t *src, int stride, int qscale){
    x8_loop_filter(src, 1, stride, qscale);
}

av_cold void ff_intrax8dsp_init(DSPContext* dsp, AVCodecContext *avctx) {
    dsp->x8_h_loop_filter=x8_h_loop_filter;
    dsp->x8_v_loop_filter=x8_v_loop_filter;
    dsp->x8_setup_spatial_compensation=x8_setup_spatial_compensation;
    dsp->x8_spatial_compensation[0]=spatial_compensation_0;
    dsp->x8_spatial_compensation[1]=spatial_compensation_1;
    dsp->x8_spatial_compensation[2]=spatial_compensation_2;
    dsp->x8_spatial_compensation[3]=spatial_compensation_3;
    dsp->x8_spatial_compensation[4]=spatial_compensation_4;
    dsp->x8_spatial_compensation[5]=spatial_compensation_5;
    dsp->x8_spatial_compensation[6]=spatial_compensation_6;
    dsp->x8_spatial_compensation[7]=spatial_compensation_7;
    dsp->x8_spatial_compensation[8]=spatial_compensation_8;
    dsp->x8_spatial_compensation[9]=spatial_compensation_9;
    dsp->x8_spatial_compensation[10]=spatial_compensation_10;
    dsp->x8_spatial_compensation[11]=spatial_compensation_11;
}
