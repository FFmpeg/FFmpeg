/*
 * huffyuv codec for libavcodec
 *
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used 
 */
 
/**
 * @file huffyuv.c
 * huffyuv codec for libavcodec.
 */

#include "common.h"
#include "bitstream.h"
#include "avcodec.h"
#include "dsputil.h"

#define VLC_BITS 11

#ifdef WORDS_BIGENDIAN
#define B 3
#define G 2
#define R 1
#else
#define B 0
#define G 1
#define R 2
#endif

typedef enum Predictor{
    LEFT= 0,
    PLANE,
    MEDIAN,
} Predictor;
 
typedef struct HYuvContext{
    AVCodecContext *avctx;
    Predictor predictor;
    GetBitContext gb;
    PutBitContext pb;
    int interlaced;
    int decorrelate;
    int bitstream_bpp;
    int version;
    int yuy2;                               //use yuy2 instead of 422P
    int bgr32;                              //use bgr32 instead of bgr24
    int width, height;
    int flags;
    int context;
    int picture_number;
    int last_slice_end;
    uint8_t *temp[3];
    uint64_t stats[3][256];
    uint8_t len[3][256];
    uint32_t bits[3][256];
    VLC vlc[3];
    AVFrame picture;
    uint8_t *bitstream_buffer;
    int bitstream_buffer_size;
    DSPContext dsp; 
}HYuvContext;

static const unsigned char classic_shift_luma[] = {
  34,36,35,69,135,232,9,16,10,24,11,23,12,16,13,10,14,8,15,8,
  16,8,17,20,16,10,207,206,205,236,11,8,10,21,9,23,8,8,199,70,
  69,68, 0
};

static const unsigned char classic_shift_chroma[] = {
  66,36,37,38,39,40,41,75,76,77,110,239,144,81,82,83,84,85,118,183,
  56,57,88,89,56,89,154,57,58,57,26,141,57,56,58,57,58,57,184,119,
  214,245,116,83,82,49,80,79,78,77,44,75,41,40,39,38,37,36,34, 0
};

static const unsigned char classic_add_luma[256] = {
    3,  9,  5, 12, 10, 35, 32, 29, 27, 50, 48, 45, 44, 41, 39, 37,
   73, 70, 68, 65, 64, 61, 58, 56, 53, 50, 49, 46, 44, 41, 38, 36,
   68, 65, 63, 61, 58, 55, 53, 51, 48, 46, 45, 43, 41, 39, 38, 36,
   35, 33, 32, 30, 29, 27, 26, 25, 48, 47, 46, 44, 43, 41, 40, 39,
   37, 36, 35, 34, 32, 31, 30, 28, 27, 26, 24, 23, 22, 20, 19, 37,
   35, 34, 33, 31, 30, 29, 27, 26, 24, 23, 21, 20, 18, 17, 15, 29,
   27, 26, 24, 22, 21, 19, 17, 16, 14, 26, 25, 23, 21, 19, 18, 16,
   15, 27, 25, 23, 21, 19, 17, 16, 14, 26, 25, 23, 21, 18, 17, 14,
   12, 17, 19, 13,  4,  9,  2, 11,  1,  7,  8,  0, 16,  3, 14,  6,
   12, 10,  5, 15, 18, 11, 10, 13, 15, 16, 19, 20, 22, 24, 27, 15,
   18, 20, 22, 24, 26, 14, 17, 20, 22, 24, 27, 15, 18, 20, 23, 25,
   28, 16, 19, 22, 25, 28, 32, 36, 21, 25, 29, 33, 38, 42, 45, 49,
   28, 31, 34, 37, 40, 42, 44, 47, 49, 50, 52, 54, 56, 57, 59, 60,
   62, 64, 66, 67, 69, 35, 37, 39, 40, 42, 43, 45, 47, 48, 51, 52,
   54, 55, 57, 59, 60, 62, 63, 66, 67, 69, 71, 72, 38, 40, 42, 43,
   46, 47, 49, 51, 26, 28, 30, 31, 33, 34, 18, 19, 11, 13,  7,  8,
};

static const unsigned char classic_add_chroma[256] = {
    3,  1,  2,  2,  2,  2,  3,  3,  7,  5,  7,  5,  8,  6, 11,  9,
    7, 13, 11, 10,  9,  8,  7,  5,  9,  7,  6,  4,  7,  5,  8,  7,
   11,  8, 13, 11, 19, 15, 22, 23, 20, 33, 32, 28, 27, 29, 51, 77,
   43, 45, 76, 81, 46, 82, 75, 55, 56,144, 58, 80, 60, 74,147, 63,
  143, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
   80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 27, 30, 21, 22,
   17, 14,  5,  6,100, 54, 47, 50, 51, 53,106,107,108,109,110,111,
  112,113,114,115,  4,117,118, 92, 94,121,122,  3,124,103,  2,  1,
    0,129,130,131,120,119,126,125,136,137,138,139,140,141,142,134,
  135,132,133,104, 64,101, 62, 57,102, 95, 93, 59, 61, 28, 97, 96,
   52, 49, 48, 29, 32, 25, 24, 46, 23, 98, 45, 44, 43, 20, 42, 41,
   19, 18, 99, 40, 15, 39, 38, 16, 13, 12, 11, 37, 10,  9,  8, 36,
    7,128,127,105,123,116, 35, 34, 33,145, 31, 79, 42,146, 78, 26,
   83, 48, 49, 50, 44, 47, 26, 31, 30, 18, 17, 19, 21, 24, 25, 13,
   14, 16, 17, 18, 20, 21, 12, 14, 15,  9, 10,  6,  9,  6,  5,  8,
    6, 12,  8, 10,  7,  9,  6,  4,  6,  2,  2,  3,  3,  3,  3,  2,
};

static inline int add_left_prediction(uint8_t *dst, uint8_t *src, int w, int acc){
    int i;

    for(i=0; i<w-1; i++){
        acc+= src[i];
        dst[i]= acc;
        i++;
        acc+= src[i];
        dst[i]= acc;
    }

    for(; i<w; i++){
        acc+= src[i];
        dst[i]= acc;
    }

    return acc;
}

static inline void add_median_prediction(uint8_t *dst, uint8_t *src1, uint8_t *diff, int w, int *left, int *left_top){
    int i;
    uint8_t l, lt;

    l= *left;
    lt= *left_top;

    for(i=0; i<w; i++){
        l= mid_pred(l, src1[i], (l + src1[i] - lt)&0xFF) + diff[i];
        lt= src1[i];
        dst[i]= l;
    }    

    *left= l;
    *left_top= lt;
}

static inline void add_left_prediction_bgr32(uint8_t *dst, uint8_t *src, int w, int *red, int *green, int *blue){
    int i;
    int r,g,b;
    r= *red;
    g= *green;
    b= *blue;

    for(i=0; i<w; i++){
        b+= src[4*i+B];
        g+= src[4*i+G];
        r+= src[4*i+R];
        
        dst[4*i+B]= b;
        dst[4*i+G]= g;
        dst[4*i+R]= r;
    }

    *red= r;
    *green= g;
    *blue= b;
}

static inline int sub_left_prediction(HYuvContext *s, uint8_t *dst, uint8_t *src, int w, int left){
    int i;
    if(w<32){
        for(i=0; i<w; i++){
            const int temp= src[i];
            dst[i]= temp - left;
            left= temp;
        }
        return left;
    }else{
        for(i=0; i<16; i++){
            const int temp= src[i];
            dst[i]= temp - left;
            left= temp;
        }
        s->dsp.diff_bytes(dst+16, src+16, src+15, w-16);
        return src[w-1];
    }
}

static void read_len_table(uint8_t *dst, GetBitContext *gb){
    int i, val, repeat;
  
    for(i=0; i<256;){
        repeat= get_bits(gb, 3);
        val   = get_bits(gb, 5);
        if(repeat==0)
            repeat= get_bits(gb, 8);
//printf("%d %d\n", val, repeat);
        while (repeat--)
            dst[i++] = val;
    }
}

static int generate_bits_table(uint32_t *dst, uint8_t *len_table){
    int len, index;
    uint32_t bits=0;

    for(len=32; len>0; len--){
        for(index=0; index<256; index++){
            if(len_table[index]==len)
                dst[index]= bits++;
        }
        if(bits & 1){
            av_log(NULL, AV_LOG_ERROR, "Error generating huffman table\n");
            return -1;
        }
        bits >>= 1;
    }
    return 0;
}

static void generate_len_table(uint8_t *dst, uint64_t *stats, int size){
    uint64_t counts[2*size];
    int up[2*size];
    int offset, i, next;
    
    for(offset=1; ; offset<<=1){
        for(i=0; i<size; i++){
            counts[i]= stats[i] + offset - 1;
        }
        
        for(next=size; next<size*2; next++){
            uint64_t min1, min2;
            int min1_i, min2_i;
            
            min1=min2= INT64_MAX;
            min1_i= min2_i=-1;
            
            for(i=0; i<next; i++){
                if(min2 > counts[i]){
                    if(min1 > counts[i]){
                        min2= min1;
                        min2_i= min1_i;
                        min1= counts[i];
                        min1_i= i;
                    }else{
                        min2= counts[i];
                        min2_i= i;
                    }
                }
            }
            
            if(min2==INT64_MAX) break;
            
            counts[next]= min1 + min2;
            counts[min1_i]=
            counts[min2_i]= INT64_MAX;
            up[min1_i]=
            up[min2_i]= next;
            up[next]= -1;
        }
        
        for(i=0; i<size; i++){
            int len;
            int index=i;
            
            for(len=0; up[index] != -1; len++)
                index= up[index];
                
            if(len >= 32) break;
            
            dst[i]= len;
        }
        if(i==size) break;
    }
}

static int read_huffman_tables(HYuvContext *s, uint8_t *src, int length){
    GetBitContext gb;
    int i;
    
    init_get_bits(&gb, src, length*8);
    
    for(i=0; i<3; i++){
        read_len_table(s->len[i], &gb);
        
        if(generate_bits_table(s->bits[i], s->len[i])<0){
            return -1;
        }
#if 0
for(j=0; j<256; j++){
printf("%6X, %2d,  %3d\n", s->bits[i][j], s->len[i][j], j);
}
#endif
        free_vlc(&s->vlc[i]);
        init_vlc(&s->vlc[i], VLC_BITS, 256, s->len[i], 1, 1, s->bits[i], 4, 4, 0);
    }
    
    return (get_bits_count(&gb)+7)/8;
}

static int read_old_huffman_tables(HYuvContext *s){
#if 1
    GetBitContext gb;
    int i;

    init_get_bits(&gb, classic_shift_luma, sizeof(classic_shift_luma)*8);
    read_len_table(s->len[0], &gb);
    init_get_bits(&gb, classic_shift_chroma, sizeof(classic_shift_chroma)*8);
    read_len_table(s->len[1], &gb);
    
    for(i=0; i<256; i++) s->bits[0][i] = classic_add_luma  [i];
    for(i=0; i<256; i++) s->bits[1][i] = classic_add_chroma[i];

    if(s->bitstream_bpp >= 24){
        memcpy(s->bits[1], s->bits[0], 256*sizeof(uint32_t));
        memcpy(s->len[1] , s->len [0], 256*sizeof(uint8_t));
    }
    memcpy(s->bits[2], s->bits[1], 256*sizeof(uint32_t));
    memcpy(s->len[2] , s->len [1], 256*sizeof(uint8_t));
    
    for(i=0; i<3; i++){
        free_vlc(&s->vlc[i]);
        init_vlc(&s->vlc[i], VLC_BITS, 256, s->len[i], 1, 1, s->bits[i], 4, 4, 0);
    }
    
    return 0;
#else
    fprintf(stderr, "v1 huffyuv is not supported \n");
    return -1;
#endif
}

static void alloc_temp(HYuvContext *s){
    int i;
    
    if(s->bitstream_bpp<24){
        for(i=0; i<3; i++){
            s->temp[i]= av_malloc(s->width + 16);
        }
    }else{
        s->temp[0]= av_malloc(4*s->width + 16);
    }
}

static int common_init(AVCodecContext *avctx){
    HYuvContext *s = avctx->priv_data;

    s->avctx= avctx;
    s->flags= avctx->flags;
        
    dsputil_init(&s->dsp, avctx);
    
    s->width= avctx->width;
    s->height= avctx->height;
    assert(s->width>0 && s->height>0);
        
    return 0;
}

static int decode_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    common_init(avctx);
    memset(s->vlc, 0, 3*sizeof(VLC));
    
    avctx->coded_frame= &s->picture;
    s->interlaced= s->height > 288;

s->bgr32=1;
//if(avctx->extradata)
//  printf("extradata:%X, extradata_size:%d\n", *(uint32_t*)avctx->extradata, avctx->extradata_size);
    if(avctx->extradata_size){
        if((avctx->bits_per_sample&7) && avctx->bits_per_sample != 12)
            s->version=1; // do such files exist at all?
        else
            s->version=2;
    }else
        s->version=0;
    
    if(s->version==2){
        int method, interlace;

        method= ((uint8_t*)avctx->extradata)[0];
        s->decorrelate= method&64 ? 1 : 0;
        s->predictor= method&63;
        s->bitstream_bpp= ((uint8_t*)avctx->extradata)[1];
        if(s->bitstream_bpp==0) 
            s->bitstream_bpp= avctx->bits_per_sample&~7;
        interlace= (((uint8_t*)avctx->extradata)[2] & 0x30) >> 4;
        s->interlaced= (interlace==1) ? 1 : (interlace==2) ? 0 : s->interlaced;
        s->context= ((uint8_t*)avctx->extradata)[2] & 0x40 ? 1 : 0;
            
        if(read_huffman_tables(s, ((uint8_t*)avctx->extradata)+4, avctx->extradata_size) < 0)
            return -1;
    }else{
        switch(avctx->bits_per_sample&7){
        case 1:
            s->predictor= LEFT;
            s->decorrelate= 0;
            break;
        case 2:
            s->predictor= LEFT;
            s->decorrelate= 1;
            break;
        case 3:
            s->predictor= PLANE;
            s->decorrelate= avctx->bits_per_sample >= 24;
            break;
        case 4:
            s->predictor= MEDIAN;
            s->decorrelate= 0;
            break;
        default:
            s->predictor= LEFT; //OLD
            s->decorrelate= 0;
            break;
        }
        s->bitstream_bpp= avctx->bits_per_sample & ~7;
        s->context= 0;
        
        if(read_old_huffman_tables(s) < 0)
            return -1;
    }
    
    switch(s->bitstream_bpp){
    case 12:
        avctx->pix_fmt = PIX_FMT_YUV420P;
        break;
    case 16:
        if(s->yuy2){
            avctx->pix_fmt = PIX_FMT_YUV422;
        }else{
            avctx->pix_fmt = PIX_FMT_YUV422P;
        }
        break;
    case 24:
    case 32:
        if(s->bgr32){
            avctx->pix_fmt = PIX_FMT_RGBA32;
        }else{
            avctx->pix_fmt = PIX_FMT_BGR24;
        }
        break;
    default:
        assert(0);
    }
    
    alloc_temp(s);
    
//    av_log(NULL, AV_LOG_DEBUG, "pred:%d bpp:%d hbpp:%d il:%d\n", s->predictor, s->bitstream_bpp, avctx->bits_per_sample, s->interlaced);

    return 0;
}

static int store_table(HYuvContext *s, uint8_t *len, uint8_t *buf){
    int i;
    int index= 0;

    for(i=0; i<256;){
        int val= len[i];
        int repeat=0;
        
        for(; i<256 && len[i]==val && repeat<255; i++)
            repeat++;
        
        assert(val < 32 && val >0 && repeat<256 && repeat>0);
        if(repeat>7){
            buf[index++]= val;
            buf[index++]= repeat;
        }else{
            buf[index++]= val | (repeat<<5);
        }
    }
    
    return index;
}

static int encode_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i, j;

    common_init(avctx);
    
    avctx->extradata= av_mallocz(1024*30); // 256*3+4 == 772
    avctx->stats_out= av_mallocz(1024*30); // 21*256*3(%llu ) + 3(\n) + 1(0) = 16132
    s->version=2;
    
    avctx->coded_frame= &s->picture;
    
    switch(avctx->pix_fmt){
    case PIX_FMT_YUV420P:
        s->bitstream_bpp= 12;
        break;
    case PIX_FMT_YUV422P:
        s->bitstream_bpp= 16;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "format not supported\n");
        return -1;
    }
    avctx->bits_per_sample= s->bitstream_bpp;
    s->decorrelate= s->bitstream_bpp >= 24;
    s->predictor= avctx->prediction_method;
    s->interlaced= avctx->flags&CODEC_FLAG_INTERLACED_ME ? 1 : 0;
    if(avctx->context_model==1){
        s->context= avctx->context_model;
        if(s->flags & (CODEC_FLAG_PASS1|CODEC_FLAG_PASS2)){
            av_log(avctx, AV_LOG_ERROR, "context=1 is not compatible with 2 pass huffyuv encoding\n");
            return -1;
        }
    }else s->context= 0;
    
    if(avctx->codec->id==CODEC_ID_HUFFYUV){
        if(avctx->pix_fmt==PIX_FMT_YUV420P){
            av_log(avctx, AV_LOG_ERROR, "Error: YV12 is not supported by huffyuv; use vcodec=ffvhuff or format=422p\n");
            return -1;
        }
        if(avctx->context_model){
            av_log(avctx, AV_LOG_ERROR, "Error: per-frame huffman tables are not supported by huffyuv; use vcodec=ffvhuff\n");
            return -1;
        }
        if(s->interlaced != ( s->height > 288 ))
            av_log(avctx, AV_LOG_INFO, "using huffyuv 2.2.0 or newer interlacing flag\n");
    }else if(avctx->strict_std_compliance>FF_COMPLIANCE_EXPERIMENTAL){
        av_log(avctx, AV_LOG_ERROR, "This codec is under development; files encoded with it may not be decodable with future versions!!! Set vstrict=-2 / -strict -2 to use it anyway.\n");
        return -1;
    }
    
    ((uint8_t*)avctx->extradata)[0]= s->predictor;
    ((uint8_t*)avctx->extradata)[1]= s->bitstream_bpp;
    ((uint8_t*)avctx->extradata)[2]= s->interlaced ? 0x10 : 0x20;
    if(s->context)
        ((uint8_t*)avctx->extradata)[2]|= 0x40;
    ((uint8_t*)avctx->extradata)[3]= 0;
    s->avctx->extradata_size= 4;
    
    if(avctx->stats_in){
        char *p= avctx->stats_in;
    
        for(i=0; i<3; i++)
            for(j=0; j<256; j++)
                s->stats[i][j]= 1;

        for(;;){
            for(i=0; i<3; i++){
                char *next;

                for(j=0; j<256; j++){
                    s->stats[i][j]+= strtol(p, &next, 0);
                    if(next==p) return -1;
                    p=next;
                }        
            }
            if(p[0]==0 || p[1]==0 || p[2]==0) break;
        }
    }else{
        for(i=0; i<3; i++)
            for(j=0; j<256; j++){
                int d= FFMIN(j, 256-j);
                
                s->stats[i][j]= 100000000/(d+1);
            }
    }
    
    for(i=0; i<3; i++){
        generate_len_table(s->len[i], s->stats[i], 256);

        if(generate_bits_table(s->bits[i], s->len[i])<0){
            return -1;
        }
        
        s->avctx->extradata_size+=
        store_table(s, s->len[i], &((uint8_t*)s->avctx->extradata)[s->avctx->extradata_size]);
    }

    if(s->context){
        for(i=0; i<3; i++){
            int pels = s->width*s->height / (i?40:10);
            for(j=0; j<256; j++){
                int d= FFMIN(j, 256-j);
                s->stats[i][j]= pels/(d+1);
            }
        }
    }else{
        for(i=0; i<3; i++)
            for(j=0; j<256; j++)
                s->stats[i][j]= 0;
    }
    
//    printf("pred:%d bpp:%d hbpp:%d il:%d\n", s->predictor, s->bitstream_bpp, avctx->bits_per_sample, s->interlaced);

    alloc_temp(s);

    s->picture_number=0;

    return 0;
}

static void decode_422_bitstream(HYuvContext *s, int count){
    int i;

    count/=2;
    
    for(i=0; i<count; i++){
        s->temp[0][2*i  ]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3); 
        s->temp[1][  i  ]= get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3); 
        s->temp[0][2*i+1]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3); 
        s->temp[2][  i  ]= get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3); 
    }
}

static void decode_gray_bitstream(HYuvContext *s, int count){
    int i;
    
    count/=2;
    
    for(i=0; i<count; i++){
        s->temp[0][2*i  ]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3); 
        s->temp[0][2*i+1]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3); 
    }
}

static int encode_422_bitstream(HYuvContext *s, int count){
    int i;
    
    if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 2*4*count){
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }
    
    count/=2;
    if(s->flags&CODEC_FLAG_PASS1){
        for(i=0; i<count; i++){
            s->stats[0][ s->temp[0][2*i  ] ]++;
            s->stats[1][ s->temp[1][  i  ] ]++;
            s->stats[0][ s->temp[0][2*i+1] ]++;
            s->stats[2][ s->temp[2][  i  ] ]++;
        }
    }
    if(s->avctx->flags2&CODEC_FLAG2_NO_OUTPUT)
        return 0;
    if(s->context){
        for(i=0; i<count; i++){
            s->stats[0][ s->temp[0][2*i  ] ]++;
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i  ] ], s->bits[0][ s->temp[0][2*i  ] ]);
            s->stats[1][ s->temp[1][  i  ] ]++;
            put_bits(&s->pb, s->len[1][ s->temp[1][  i  ] ], s->bits[1][ s->temp[1][  i  ] ]);
            s->stats[0][ s->temp[0][2*i+1] ]++;
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i+1] ], s->bits[0][ s->temp[0][2*i+1] ]);
            s->stats[2][ s->temp[2][  i  ] ]++;
            put_bits(&s->pb, s->len[2][ s->temp[2][  i  ] ], s->bits[2][ s->temp[2][  i  ] ]);
        }
    }else{
        for(i=0; i<count; i++){
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i  ] ], s->bits[0][ s->temp[0][2*i  ] ]);
            put_bits(&s->pb, s->len[1][ s->temp[1][  i  ] ], s->bits[1][ s->temp[1][  i  ] ]);
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i+1] ], s->bits[0][ s->temp[0][2*i+1] ]);
            put_bits(&s->pb, s->len[2][ s->temp[2][  i  ] ], s->bits[2][ s->temp[2][  i  ] ]);
        }
    }
    return 0;
}

static int encode_gray_bitstream(HYuvContext *s, int count){
    int i;
    
    if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 4*count){
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    count/=2;
    if(s->flags&CODEC_FLAG_PASS1){
        for(i=0; i<count; i++){
            s->stats[0][ s->temp[0][2*i  ] ]++;
            s->stats[0][ s->temp[0][2*i+1] ]++;
        }
    }
    if(s->avctx->flags2&CODEC_FLAG2_NO_OUTPUT)
        return 0;
    
    if(s->context){
        for(i=0; i<count; i++){
            s->stats[0][ s->temp[0][2*i  ] ]++;
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i  ] ], s->bits[0][ s->temp[0][2*i  ] ]);
            s->stats[0][ s->temp[0][2*i+1] ]++;
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i+1] ], s->bits[0][ s->temp[0][2*i+1] ]);
        }
    }else{
        for(i=0; i<count; i++){
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i  ] ], s->bits[0][ s->temp[0][2*i  ] ]);
            put_bits(&s->pb, s->len[0][ s->temp[0][2*i+1] ], s->bits[0][ s->temp[0][2*i+1] ]);
        }
    }
    return 0;
}

static void decode_bgr_bitstream(HYuvContext *s, int count){
    int i;

    if(s->decorrelate){
        if(s->bitstream_bpp==24){
            for(i=0; i<count; i++){
                s->temp[0][4*i+G]= get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3); 
                s->temp[0][4*i+B]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3) + s->temp[0][4*i+G];
                s->temp[0][4*i+R]= get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3) + s->temp[0][4*i+G];
            }
        }else{
            for(i=0; i<count; i++){
                s->temp[0][4*i+G]= get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3); 
                s->temp[0][4*i+B]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3) + s->temp[0][4*i+G];
                s->temp[0][4*i+R]= get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3) + s->temp[0][4*i+G]; 
                                   get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3); //?!
            }
        }
    }else{
        if(s->bitstream_bpp==24){
            for(i=0; i<count; i++){
                s->temp[0][4*i+B]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);
                s->temp[0][4*i+G]= get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3); 
                s->temp[0][4*i+R]= get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3); 
            }
        }else{
            for(i=0; i<count; i++){
                s->temp[0][4*i+B]= get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);
                s->temp[0][4*i+G]= get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3); 
                s->temp[0][4*i+R]= get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3); 
                                   get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3); //?!
            }
        }
    }
}

static void draw_slice(HYuvContext *s, int y){
    int h, cy;
    int offset[4];
    
    if(s->avctx->draw_horiz_band==NULL) 
        return;
        
    h= y - s->last_slice_end;
    y -= h;
    
    if(s->bitstream_bpp==12){
        cy= y>>1;
    }else{
        cy= y;
    }

    offset[0] = s->picture.linesize[0]*y;
    offset[1] = s->picture.linesize[1]*cy;
    offset[2] = s->picture.linesize[2]*cy;
    offset[3] = 0;
    emms_c();

    s->avctx->draw_horiz_band(s->avctx, &s->picture, offset, y, 3, h);
    
    s->last_slice_end= y + h;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size){
    HYuvContext *s = avctx->priv_data;
    const int width= s->width;
    const int width2= s->width>>1;
    const int height= s->height;
    int fake_ystride, fake_ustride, fake_vstride;
    AVFrame * const p= &s->picture;
    int table_size= 0;

    AVFrame *picture = data;

    s->bitstream_buffer= av_fast_realloc(s->bitstream_buffer, &s->bitstream_buffer_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

    s->dsp.bswap_buf((uint32_t*)s->bitstream_buffer, (uint32_t*)buf, buf_size/4);
    
    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    
    if(s->context){
        table_size = read_huffman_tables(s, s->bitstream_buffer, buf_size);
        if(table_size < 0)
            return -1;
    }

    init_get_bits(&s->gb, s->bitstream_buffer+table_size, (buf_size-table_size)*8);

    fake_ystride= s->interlaced ? p->linesize[0]*2  : p->linesize[0];
    fake_ustride= s->interlaced ? p->linesize[1]*2  : p->linesize[1];
    fake_vstride= s->interlaced ? p->linesize[2]*2  : p->linesize[2];
    
    s->last_slice_end= 0;
        
    if(s->bitstream_bpp<24){
        int y, cy;
        int lefty, leftu, leftv;
        int lefttopy, lefttopu, lefttopv;
        
        if(s->yuy2){
            p->data[0][3]= get_bits(&s->gb, 8);
            p->data[0][2]= get_bits(&s->gb, 8);
            p->data[0][1]= get_bits(&s->gb, 8);
            p->data[0][0]= get_bits(&s->gb, 8);
            
            av_log(avctx, AV_LOG_ERROR, "YUY2 output is not implemented yet\n");
            return -1;
        }else{
        
            leftv= p->data[2][0]= get_bits(&s->gb, 8);
            lefty= p->data[0][1]= get_bits(&s->gb, 8);
            leftu= p->data[1][0]= get_bits(&s->gb, 8);
                   p->data[0][0]= get_bits(&s->gb, 8);
        
            switch(s->predictor){
            case LEFT:
            case PLANE:
                decode_422_bitstream(s, width-2);
                lefty= add_left_prediction(p->data[0] + 2, s->temp[0], width-2, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= add_left_prediction(p->data[1] + 1, s->temp[1], width2-1, leftu);
                    leftv= add_left_prediction(p->data[2] + 1, s->temp[2], width2-1, leftv);
                }

                for(cy=y=1; y<s->height; y++,cy++){
                    uint8_t *ydst, *udst, *vdst;
                    
                    if(s->bitstream_bpp==12){
                        decode_gray_bitstream(s, width);
                    
                        ydst= p->data[0] + p->linesize[0]*y;

                        lefty= add_left_prediction(ydst, s->temp[0], width, lefty);
                        if(s->predictor == PLANE){
                            if(y>s->interlaced)
                                s->dsp.add_bytes(ydst, ydst - fake_ystride, width);
                        }
                        y++;
                        if(y>=s->height) break;
                    }
                    
                    draw_slice(s, y);
                    
                    ydst= p->data[0] + p->linesize[0]*y;
                    udst= p->data[1] + p->linesize[1]*cy;
                    vdst= p->data[2] + p->linesize[2]*cy;
                    
                    decode_422_bitstream(s, width);
                    lefty= add_left_prediction(ydst, s->temp[0], width, lefty);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        leftu= add_left_prediction(udst, s->temp[1], width2, leftu);
                        leftv= add_left_prediction(vdst, s->temp[2], width2, leftv);
                    }
                    if(s->predictor == PLANE){
                        if(cy>s->interlaced){
                            s->dsp.add_bytes(ydst, ydst - fake_ystride, width);
                            if(!(s->flags&CODEC_FLAG_GRAY)){
                                s->dsp.add_bytes(udst, udst - fake_ustride, width2);
                                s->dsp.add_bytes(vdst, vdst - fake_vstride, width2);
                            }
                        }
                    }
                }
                draw_slice(s, height);
                
                break;
            case MEDIAN:
                /* first line except first 2 pixels is left predicted */
                decode_422_bitstream(s, width-2);
                lefty= add_left_prediction(p->data[0] + 2, s->temp[0], width-2, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= add_left_prediction(p->data[1] + 1, s->temp[1], width2-1, leftu);
                    leftv= add_left_prediction(p->data[2] + 1, s->temp[2], width2-1, leftv);
                }
                
                cy=y=1;
                
                /* second line is left predicted for interlaced case */
                if(s->interlaced){
                    decode_422_bitstream(s, width);
                    lefty= add_left_prediction(p->data[0] + p->linesize[0], s->temp[0], width, lefty);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        leftu= add_left_prediction(p->data[1] + p->linesize[2], s->temp[1], width2, leftu);
                        leftv= add_left_prediction(p->data[2] + p->linesize[1], s->temp[2], width2, leftv);
                    }
                    y++; cy++;
                }

                /* next 4 pixels are left predicted too */
                decode_422_bitstream(s, 4);
                lefty= add_left_prediction(p->data[0] + fake_ystride, s->temp[0], 4, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= add_left_prediction(p->data[1] + fake_ustride, s->temp[1], 2, leftu);
                    leftv= add_left_prediction(p->data[2] + fake_vstride, s->temp[2], 2, leftv);
                }

                /* next line except the first 4 pixels is median predicted */
                lefttopy= p->data[0][3];
                decode_422_bitstream(s, width-4);
                add_median_prediction(p->data[0] + fake_ystride+4, p->data[0]+4, s->temp[0], width-4, &lefty, &lefttopy);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    lefttopu= p->data[1][1];
                    lefttopv= p->data[2][1];
                    add_median_prediction(p->data[1] + fake_ustride+2, p->data[1]+2, s->temp[1], width2-2, &leftu, &lefttopu);
                    add_median_prediction(p->data[2] + fake_vstride+2, p->data[2]+2, s->temp[2], width2-2, &leftv, &lefttopv);
                }
                y++; cy++;
                
                for(; y<height; y++,cy++){
                    uint8_t *ydst, *udst, *vdst;

                    if(s->bitstream_bpp==12){
                        while(2*cy > y){
                            decode_gray_bitstream(s, width);
                            ydst= p->data[0] + p->linesize[0]*y;
                            add_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                            y++;
                        }
                        if(y>=height) break;
                    }
                    draw_slice(s, y);

                    decode_422_bitstream(s, width);

                    ydst= p->data[0] + p->linesize[0]*y;
                    udst= p->data[1] + p->linesize[1]*cy;
                    vdst= p->data[2] + p->linesize[2]*cy;

                    add_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        add_median_prediction(udst, udst - fake_ustride, s->temp[1], width2, &leftu, &lefttopu);
                        add_median_prediction(vdst, vdst - fake_vstride, s->temp[2], width2, &leftv, &lefttopv);
                    }
                }

                draw_slice(s, height);
                break;
            }
        }
    }else{
        int y;
        int leftr, leftg, leftb;
        const int last_line= (height-1)*p->linesize[0];
        
        if(s->bitstream_bpp==32){
            skip_bits(&s->gb, 8);
            leftr= p->data[0][last_line+R]= get_bits(&s->gb, 8);
            leftg= p->data[0][last_line+G]= get_bits(&s->gb, 8);
            leftb= p->data[0][last_line+B]= get_bits(&s->gb, 8);
        }else{
            leftr= p->data[0][last_line+R]= get_bits(&s->gb, 8);
            leftg= p->data[0][last_line+G]= get_bits(&s->gb, 8);
            leftb= p->data[0][last_line+B]= get_bits(&s->gb, 8);
            skip_bits(&s->gb, 8);
        }
        
        if(s->bgr32){
            switch(s->predictor){
            case LEFT:
            case PLANE:
                decode_bgr_bitstream(s, width-1);
                add_left_prediction_bgr32(p->data[0] + last_line+4, s->temp[0], width-1, &leftr, &leftg, &leftb);

                for(y=s->height-2; y>=0; y--){ //yes its stored upside down
                    decode_bgr_bitstream(s, width);
                    
                    add_left_prediction_bgr32(p->data[0] + p->linesize[0]*y, s->temp[0], width, &leftr, &leftg, &leftb);
                    if(s->predictor == PLANE){
                        if((y&s->interlaced)==0 && y<s->height-1-s->interlaced){
                            s->dsp.add_bytes(p->data[0] + p->linesize[0]*y, 
                                             p->data[0] + p->linesize[0]*y + fake_ystride, fake_ystride);
                        }
                    }
                }
                draw_slice(s, height); // just 1 large slice as this is not possible in reverse order
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "prediction type not supported!\n");
            }
        }else{

            av_log(avctx, AV_LOG_ERROR, "BGR24 output is not implemented yet\n");
            return -1;
        }
    }
    emms_c();
    
    *picture= *p;
    *data_size = sizeof(AVFrame);
    
    return (get_bits_count(&s->gb)+31)/32*4;
}

static int common_end(HYuvContext *s){
    int i;
    
    for(i=0; i<3; i++){
        av_freep(&s->temp[i]);
    }
    return 0;
}

static int decode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i;
    
    common_end(s);
    av_freep(&s->bitstream_buffer);
    
    for(i=0; i<3; i++){
        free_vlc(&s->vlc[i]);
    }

    return 0;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    HYuvContext *s = avctx->priv_data;
    AVFrame *pict = data;
    const int width= s->width;
    const int width2= s->width>>1;
    const int height= s->height;
    const int fake_ystride= s->interlaced ? pict->linesize[0]*2  : pict->linesize[0];
    const int fake_ustride= s->interlaced ? pict->linesize[1]*2  : pict->linesize[1];
    const int fake_vstride= s->interlaced ? pict->linesize[2]*2  : pict->linesize[2];
    AVFrame * const p= &s->picture;
    int i, j, size=0;

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    
    if(s->context){
        for(i=0; i<3; i++){
            generate_len_table(s->len[i], s->stats[i], 256);
            if(generate_bits_table(s->bits[i], s->len[i])<0)
                return -1;
            size+= store_table(s, s->len[i], &buf[size]);
        }

        for(i=0; i<3; i++)
            for(j=0; j<256; j++)
                s->stats[i][j] >>= 1;
    }

    init_put_bits(&s->pb, buf+size, buf_size-size);

    if(avctx->pix_fmt == PIX_FMT_YUV422P || avctx->pix_fmt == PIX_FMT_YUV420P){
        int lefty, leftu, leftv, y, cy;

        put_bits(&s->pb, 8, leftv= p->data[2][0]);
        put_bits(&s->pb, 8, lefty= p->data[0][1]);
        put_bits(&s->pb, 8, leftu= p->data[1][0]);
        put_bits(&s->pb, 8,        p->data[0][0]);
        
        lefty= sub_left_prediction(s, s->temp[0], p->data[0]+2, width-2 , lefty);
        leftu= sub_left_prediction(s, s->temp[1], p->data[1]+1, width2-1, leftu);
        leftv= sub_left_prediction(s, s->temp[2], p->data[2]+1, width2-1, leftv);
        
        encode_422_bitstream(s, width-2);
        
        if(s->predictor==MEDIAN){
            int lefttopy, lefttopu, lefttopv;
            cy=y=1;
            if(s->interlaced){
                lefty= sub_left_prediction(s, s->temp[0], p->data[0]+p->linesize[0], width , lefty);
                leftu= sub_left_prediction(s, s->temp[1], p->data[1]+p->linesize[1], width2, leftu);
                leftv= sub_left_prediction(s, s->temp[2], p->data[2]+p->linesize[2], width2, leftv);
        
                encode_422_bitstream(s, width);
                y++; cy++;
            }
            
            lefty= sub_left_prediction(s, s->temp[0], p->data[0]+fake_ystride, 4, lefty);
            leftu= sub_left_prediction(s, s->temp[1], p->data[1]+fake_ustride, 2, leftu);
            leftv= sub_left_prediction(s, s->temp[2], p->data[2]+fake_vstride, 2, leftv);
        
            encode_422_bitstream(s, 4);

            lefttopy= p->data[0][3];
            lefttopu= p->data[1][1];
            lefttopv= p->data[2][1];
            s->dsp.sub_hfyu_median_prediction(s->temp[0], p->data[0]+4, p->data[0] + fake_ystride+4, width-4 , &lefty, &lefttopy);
            s->dsp.sub_hfyu_median_prediction(s->temp[1], p->data[1]+2, p->data[1] + fake_ustride+2, width2-2, &leftu, &lefttopu);
            s->dsp.sub_hfyu_median_prediction(s->temp[2], p->data[2]+2, p->data[2] + fake_vstride+2, width2-2, &leftv, &lefttopv);
            encode_422_bitstream(s, width-4);
            y++; cy++;

            for(; y<height; y++,cy++){
                uint8_t *ydst, *udst, *vdst;
                    
                if(s->bitstream_bpp==12){
                    while(2*cy > y){
                        ydst= p->data[0] + p->linesize[0]*y;
                        s->dsp.sub_hfyu_median_prediction(s->temp[0], ydst - fake_ystride, ydst, width , &lefty, &lefttopy);
                        encode_gray_bitstream(s, width);
                        y++;
                    }
                    if(y>=height) break;
                }
                ydst= p->data[0] + p->linesize[0]*y;
                udst= p->data[1] + p->linesize[1]*cy;
                vdst= p->data[2] + p->linesize[2]*cy;

                s->dsp.sub_hfyu_median_prediction(s->temp[0], ydst - fake_ystride, ydst, width , &lefty, &lefttopy);
                s->dsp.sub_hfyu_median_prediction(s->temp[1], udst - fake_ustride, udst, width2, &leftu, &lefttopu);
                s->dsp.sub_hfyu_median_prediction(s->temp[2], vdst - fake_vstride, vdst, width2, &leftv, &lefttopv);

                encode_422_bitstream(s, width);
            }
        }else{
            for(cy=y=1; y<height; y++,cy++){
                uint8_t *ydst, *udst, *vdst;
                
                /* encode a luma only line & y++ */
                if(s->bitstream_bpp==12){
                    ydst= p->data[0] + p->linesize[0]*y;

                    if(s->predictor == PLANE && s->interlaced < y){
                        s->dsp.diff_bytes(s->temp[1], ydst, ydst - fake_ystride, width);

                        lefty= sub_left_prediction(s, s->temp[0], s->temp[1], width , lefty);
                    }else{
                        lefty= sub_left_prediction(s, s->temp[0], ydst, width , lefty);
                    }
                    encode_gray_bitstream(s, width);
                    y++;
                    if(y>=height) break;
                }
                
                ydst= p->data[0] + p->linesize[0]*y;
                udst= p->data[1] + p->linesize[1]*cy;
                vdst= p->data[2] + p->linesize[2]*cy;

                if(s->predictor == PLANE && s->interlaced < cy){
                    s->dsp.diff_bytes(s->temp[1], ydst, ydst - fake_ystride, width);
                    s->dsp.diff_bytes(s->temp[2], udst, udst - fake_ustride, width2);
                    s->dsp.diff_bytes(s->temp[2] + width2, vdst, vdst - fake_vstride, width2);

                    lefty= sub_left_prediction(s, s->temp[0], s->temp[1], width , lefty);
                    leftu= sub_left_prediction(s, s->temp[1], s->temp[2], width2, leftu);
                    leftv= sub_left_prediction(s, s->temp[2], s->temp[2] + width2, width2, leftv);
                }else{
                    lefty= sub_left_prediction(s, s->temp[0], ydst, width , lefty);
                    leftu= sub_left_prediction(s, s->temp[1], udst, width2, leftu);
                    leftv= sub_left_prediction(s, s->temp[2], vdst, width2, leftv);
                }

                encode_422_bitstream(s, width);
            }
        }        
    }else{
        av_log(avctx, AV_LOG_ERROR, "Format not supported!\n");
    }
    emms_c();
    
    size+= (put_bits_count(&s->pb)+31)/8;
    size/= 4;
    
    if((s->flags&CODEC_FLAG_PASS1) && (s->picture_number&31)==0){
        int j;
        char *p= avctx->stats_out;
        char *end= p + 1024*30;
        for(i=0; i<3; i++){
            for(j=0; j<256; j++){
                snprintf(p, end-p, "%llu ", s->stats[i][j]);
                p+= strlen(p);
                s->stats[i][j]= 0;
            }
            snprintf(p, end-p, "\n");
            p++;
        }
    }
    if(!(s->avctx->flags2 & CODEC_FLAG2_NO_OUTPUT)){
        flush_put_bits(&s->pb);
        s->dsp.bswap_buf((uint32_t*)buf, (uint32_t*)buf, size);
        avctx->stats_out[0] = '\0';
    }
    
    s->picture_number++;

    return size*4;
}

static int encode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    
    common_end(s);

    av_freep(&avctx->extradata);
    av_freep(&avctx->stats_out);
    
    return 0;
}

AVCodec huffyuv_decoder = {
    "huffyuv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_HUFFYUV,
    sizeof(HYuvContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND,
    NULL
};

AVCodec ffvhuff_decoder = {
    "ffvhuff",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FFVHUFF,
    sizeof(HYuvContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND,
    NULL
};

#ifdef CONFIG_ENCODERS

AVCodec huffyuv_encoder = {
    "huffyuv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_HUFFYUV,
    sizeof(HYuvContext),
    encode_init,
    encode_frame,
    encode_end,
};

AVCodec ffvhuff_encoder = {
    "ffvhuff",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FFVHUFF,
    sizeof(HYuvContext),
    encode_init,
    encode_frame,
    encode_end,
};

#endif //CONFIG_ENCODERS
