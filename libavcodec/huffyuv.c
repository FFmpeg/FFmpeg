/*
 * huffyuv codec for libavcodec
 *
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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
 * huffyuv codec for libavcodec.
 */

#include "avcodec.h"
#include "get_bits.h"
#include "put_bits.h"
#include "dsputil.h"
#include "thread.h"

#define VLC_BITS 11

#if HAVE_BIGENDIAN
#define B 3
#define G 2
#define R 1
#define A 0
#else
#define B 0
#define G 1
#define R 2
#define A 3
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
    uint32_t pix_bgr_map[1<<VLC_BITS];
    VLC vlc[6];                             //Y,U,V,YY,YU,YV
    AVFrame picture;
    uint8_t *bitstream_buffer;
    unsigned int bitstream_buffer_size;
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

static inline void sub_left_prediction_bgr32(HYuvContext *s, uint8_t *dst, uint8_t *src, int w, int *red, int *green, int *blue){
    int i;
    int r,g,b;
    r= *red;
    g= *green;
    b= *blue;
    for(i=0; i<FFMIN(w,4); i++){
        const int rt= src[i*4+R];
        const int gt= src[i*4+G];
        const int bt= src[i*4+B];
        dst[i*4+R]= rt - r;
        dst[i*4+G]= gt - g;
        dst[i*4+B]= bt - b;
        r = rt;
        g = gt;
        b = bt;
    }
    s->dsp.diff_bytes(dst+16, src+16, src+12, w*4-16);
    *red=   src[(w-1)*4+R];
    *green= src[(w-1)*4+G];
    *blue=  src[(w-1)*4+B];
}

static int read_len_table(uint8_t *dst, GetBitContext *gb){
    int i, val, repeat;

    for(i=0; i<256;){
        repeat= get_bits(gb, 3);
        val   = get_bits(gb, 5);
        if(repeat==0)
            repeat= get_bits(gb, 8);
//printf("%d %d\n", val, repeat);
        if(i+repeat > 256) {
            av_log(NULL, AV_LOG_ERROR, "Error reading huffman table\n");
            return -1;
        }
        while (repeat--)
            dst[i++] = val;
    }
    return 0;
}

static int generate_bits_table(uint32_t *dst, const uint8_t *len_table){
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

#if CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER
typedef struct {
    uint64_t val;
    int name;
} HeapElem;

static void heap_sift(HeapElem *h, int root, int size)
{
    while(root*2+1 < size) {
        int child = root*2+1;
        if(child < size-1 && h[child].val > h[child+1].val)
            child++;
        if(h[root].val > h[child].val) {
            FFSWAP(HeapElem, h[root], h[child]);
            root = child;
        } else
            break;
    }
}

static void generate_len_table(uint8_t *dst, const uint64_t *stats){
    HeapElem h[256];
    int up[2*256];
    int len[2*256];
    int offset, i, next;
    int size = 256;

    for(offset=1; ; offset<<=1){
        for(i=0; i<size; i++){
            h[i].name = i;
            h[i].val = (stats[i] << 8) + offset;
        }
        for(i=size/2-1; i>=0; i--)
            heap_sift(h, i, size);

        for(next=size; next<size*2-1; next++){
            // merge the two smallest entries, and put it back in the heap
            uint64_t min1v = h[0].val;
            up[h[0].name] = next;
            h[0].val = INT64_MAX;
            heap_sift(h, 0, size);
            up[h[0].name] = next;
            h[0].name = next;
            h[0].val += min1v;
            heap_sift(h, 0, size);
        }

        len[2*size-2] = 0;
        for(i=2*size-3; i>=size; i--)
            len[i] = len[up[i]] + 1;
        for(i=0; i<size; i++) {
            dst[i] = len[up[i]] + 1;
            if(dst[i] >= 32) break;
        }
        if(i==size) break;
    }
}
#endif /* CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER */

static void generate_joint_tables(HYuvContext *s){
    uint16_t symbols[1<<VLC_BITS];
    uint16_t bits[1<<VLC_BITS];
    uint8_t len[1<<VLC_BITS];
    if(s->bitstream_bpp < 24){
        int p, i, y, u;
        for(p=0; p<3; p++){
            for(i=y=0; y<256; y++){
                int len0 = s->len[0][y];
                int limit = VLC_BITS - len0;
                if(limit <= 0)
                    continue;
                for(u=0; u<256; u++){
                    int len1 = s->len[p][u];
                    if(len1 > limit)
                        continue;
                    len[i] = len0 + len1;
                    bits[i] = (s->bits[0][y] << len1) + s->bits[p][u];
                    symbols[i] = (y<<8) + u;
                    if(symbols[i] != 0xffff) // reserved to mean "invalid"
                        i++;
                }
            }
            free_vlc(&s->vlc[3+p]);
            init_vlc_sparse(&s->vlc[3+p], VLC_BITS, i, len, 1, 1, bits, 2, 2, symbols, 2, 2, 0);
        }
    }else{
        uint8_t (*map)[4] = (uint8_t(*)[4])s->pix_bgr_map;
        int i, b, g, r, code;
        int p0 = s->decorrelate;
        int p1 = !s->decorrelate;
        // restrict the range to +/-16 becaues that's pretty much guaranteed to
        // cover all the combinations that fit in 11 bits total, and it doesn't
        // matter if we miss a few rare codes.
        for(i=0, g=-16; g<16; g++){
            int len0 = s->len[p0][g&255];
            int limit0 = VLC_BITS - len0;
            if(limit0 < 2)
                continue;
            for(b=-16; b<16; b++){
                int len1 = s->len[p1][b&255];
                int limit1 = limit0 - len1;
                if(limit1 < 1)
                    continue;
                code = (s->bits[p0][g&255] << len1) + s->bits[p1][b&255];
                for(r=-16; r<16; r++){
                    int len2 = s->len[2][r&255];
                    if(len2 > limit1)
                        continue;
                    len[i] = len0 + len1 + len2;
                    bits[i] = (code << len2) + s->bits[2][r&255];
                    if(s->decorrelate){
                        map[i][G] = g;
                        map[i][B] = g+b;
                        map[i][R] = g+r;
                    }else{
                        map[i][B] = g;
                        map[i][G] = b;
                        map[i][R] = r;
                    }
                    i++;
                }
            }
        }
        free_vlc(&s->vlc[3]);
        init_vlc(&s->vlc[3], VLC_BITS, i, len, 1, 1, bits, 2, 2, 0);
    }
}

static int read_huffman_tables(HYuvContext *s, const uint8_t *src, int length){
    GetBitContext gb;
    int i;

    init_get_bits(&gb, src, length*8);

    for(i=0; i<3; i++){
        if(read_len_table(s->len[i], &gb)<0)
            return -1;
        if(generate_bits_table(s->bits[i], s->len[i])<0){
            return -1;
        }
        free_vlc(&s->vlc[i]);
        init_vlc(&s->vlc[i], VLC_BITS, 256, s->len[i], 1, 1, s->bits[i], 4, 4, 0);
    }

    generate_joint_tables(s);

    return (get_bits_count(&gb)+7)/8;
}

static int read_old_huffman_tables(HYuvContext *s){
#if 1
    GetBitContext gb;
    int i;

    init_get_bits(&gb, classic_shift_luma, sizeof(classic_shift_luma)*8);
    if(read_len_table(s->len[0], &gb)<0)
        return -1;
    init_get_bits(&gb, classic_shift_chroma, sizeof(classic_shift_chroma)*8);
    if(read_len_table(s->len[1], &gb)<0)
        return -1;

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

    generate_joint_tables(s);

    return 0;
#else
    av_log(s->avctx, AV_LOG_DEBUG, "v1 huffyuv is not supported \n");
    return -1;
#endif
}

static av_cold void alloc_temp(HYuvContext *s){
    int i;

    if(s->bitstream_bpp<24){
        for(i=0; i<3; i++){
            s->temp[i]= av_malloc(s->width + 16);
        }
    }else{
        s->temp[0]= av_mallocz(4*s->width + 16);
    }
}

static av_cold int common_init(AVCodecContext *avctx){
    HYuvContext *s = avctx->priv_data;

    s->avctx= avctx;
    s->flags= avctx->flags;

    dsputil_init(&s->dsp, avctx);

    s->width= avctx->width;
    s->height= avctx->height;
    assert(s->width>0 && s->height>0);

    return 0;
}

#if CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER
static av_cold int decode_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    common_init(avctx);
    memset(s->vlc, 0, 3*sizeof(VLC));

    avctx->coded_frame= &s->picture;
    avcodec_get_frame_defaults(&s->picture);
    s->interlaced= s->height > 288;

s->bgr32=1;
//if(avctx->extradata)
//  printf("extradata:%X, extradata_size:%d\n", *(uint32_t*)avctx->extradata, avctx->extradata_size);
    if(avctx->extradata_size){
        if((avctx->bits_per_coded_sample&7) && avctx->bits_per_coded_sample != 12)
            s->version=1; // do such files exist at all?
        else
            s->version=2;
    }else
        s->version=0;

    if(s->version==2){
        int method, interlace;

        if (avctx->extradata_size < 4)
            return -1;

        method= ((uint8_t*)avctx->extradata)[0];
        s->decorrelate= method&64 ? 1 : 0;
        s->predictor= method&63;
        s->bitstream_bpp= ((uint8_t*)avctx->extradata)[1];
        if(s->bitstream_bpp==0)
            s->bitstream_bpp= avctx->bits_per_coded_sample&~7;
        interlace= (((uint8_t*)avctx->extradata)[2] & 0x30) >> 4;
        s->interlaced= (interlace==1) ? 1 : (interlace==2) ? 0 : s->interlaced;
        s->context= ((uint8_t*)avctx->extradata)[2] & 0x40 ? 1 : 0;

        if(read_huffman_tables(s, ((uint8_t*)avctx->extradata)+4, avctx->extradata_size-4) < 0)
            return -1;
    }else{
        switch(avctx->bits_per_coded_sample&7){
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
            s->decorrelate= avctx->bits_per_coded_sample >= 24;
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
        s->bitstream_bpp= avctx->bits_per_coded_sample & ~7;
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
            avctx->pix_fmt = PIX_FMT_YUYV422;
        }else{
            avctx->pix_fmt = PIX_FMT_YUV422P;
        }
        break;
    case 24:
    case 32:
        if(s->bgr32){
            avctx->pix_fmt = PIX_FMT_RGB32;
        }else{
            avctx->pix_fmt = PIX_FMT_BGR24;
        }
        break;
    default:
        assert(0);
    }

    alloc_temp(s);

//    av_log(NULL, AV_LOG_DEBUG, "pred:%d bpp:%d hbpp:%d il:%d\n", s->predictor, s->bitstream_bpp, avctx->bits_per_coded_sample, s->interlaced);

    return 0;
}

static av_cold int decode_init_thread_copy(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i;

    avctx->coded_frame= &s->picture;
    alloc_temp(s);

    for (i = 0; i < 6; i++)
        s->vlc[i].table = NULL;

    if(s->version==2){
        if(read_huffman_tables(s, ((uint8_t*)avctx->extradata)+4, avctx->extradata_size) < 0)
            return -1;
    }else{
        if(read_old_huffman_tables(s) < 0)
            return -1;
    }

    return 0;
}
#endif /* CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER */

#if CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER
static int store_table(HYuvContext *s, const uint8_t *len, uint8_t *buf){
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

static av_cold int encode_init(AVCodecContext *avctx)
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
    case PIX_FMT_RGB32:
        s->bitstream_bpp= 24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "format not supported\n");
        return -1;
    }
    avctx->bits_per_coded_sample= s->bitstream_bpp;
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
    }

    if(s->bitstream_bpp>=24 && s->predictor==MEDIAN){
        av_log(avctx, AV_LOG_ERROR, "Error: RGB is incompatible with median predictor\n");
        return -1;
    }

    ((uint8_t*)avctx->extradata)[0]= s->predictor | (s->decorrelate << 6);
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
        generate_len_table(s->len[i], s->stats[i]);

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

//    printf("pred:%d bpp:%d hbpp:%d il:%d\n", s->predictor, s->bitstream_bpp, avctx->bits_per_coded_sample, s->interlaced);

    alloc_temp(s);

    s->picture_number=0;

    return 0;
}
#endif /* CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER */

/* TODO instead of restarting the read when the code isn't in the first level
 * of the joint table, jump into the 2nd level of the individual table. */
#define READ_2PIX(dst0, dst1, plane1){\
    uint16_t code = get_vlc2(&s->gb, s->vlc[3+plane1].table, VLC_BITS, 1);\
    if(code != 0xffff){\
        dst0 = code>>8;\
        dst1 = code;\
    }else{\
        dst0 = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);\
        dst1 = get_vlc2(&s->gb, s->vlc[plane1].table, VLC_BITS, 3);\
    }\
}

static void decode_422_bitstream(HYuvContext *s, int count){
    int i;

    count/=2;

    if(count >= (get_bits_left(&s->gb))/(31*4)){
        for(i=0; i<count && get_bits_count(&s->gb) < s->gb.size_in_bits; i++){
            READ_2PIX(s->temp[0][2*i  ], s->temp[1][i], 1);
            READ_2PIX(s->temp[0][2*i+1], s->temp[2][i], 2);
        }
    }else{
        for(i=0; i<count; i++){
            READ_2PIX(s->temp[0][2*i  ], s->temp[1][i], 1);
            READ_2PIX(s->temp[0][2*i+1], s->temp[2][i], 2);
        }
    }
}

static void decode_gray_bitstream(HYuvContext *s, int count){
    int i;

    count/=2;

    if(count >= (get_bits_left(&s->gb))/(31*2)){
        for(i=0; i<count && get_bits_count(&s->gb) < s->gb.size_in_bits; i++){
            READ_2PIX(s->temp[0][2*i  ], s->temp[0][2*i+1], 0);
        }
    }else{
        for(i=0; i<count; i++){
            READ_2PIX(s->temp[0][2*i  ], s->temp[0][2*i+1], 0);
        }
    }
}

#if CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER
static int encode_422_bitstream(HYuvContext *s, int offset, int count){
    int i;
    const uint8_t *y = s->temp[0] + offset;
    const uint8_t *u = s->temp[1] + offset/2;
    const uint8_t *v = s->temp[2] + offset/2;

    if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 2*4*count){
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

#define LOAD4\
            int y0 = y[2*i];\
            int y1 = y[2*i+1];\
            int u0 = u[i];\
            int v0 = v[i];

    count/=2;
    if(s->flags&CODEC_FLAG_PASS1){
        for(i=0; i<count; i++){
            LOAD4;
            s->stats[0][y0]++;
            s->stats[1][u0]++;
            s->stats[0][y1]++;
            s->stats[2][v0]++;
        }
    }
    if(s->avctx->flags2&CODEC_FLAG2_NO_OUTPUT)
        return 0;
    if(s->context){
        for(i=0; i<count; i++){
            LOAD4;
            s->stats[0][y0]++;
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);
            s->stats[1][u0]++;
            put_bits(&s->pb, s->len[1][u0], s->bits[1][u0]);
            s->stats[0][y1]++;
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);
            s->stats[2][v0]++;
            put_bits(&s->pb, s->len[2][v0], s->bits[2][v0]);
        }
    }else{
        for(i=0; i<count; i++){
            LOAD4;
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);
            put_bits(&s->pb, s->len[1][u0], s->bits[1][u0]);
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);
            put_bits(&s->pb, s->len[2][v0], s->bits[2][v0]);
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

#define LOAD2\
            int y0 = s->temp[0][2*i];\
            int y1 = s->temp[0][2*i+1];
#define STAT2\
            s->stats[0][y0]++;\
            s->stats[0][y1]++;
#define WRITE2\
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);\
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);

    count/=2;
    if(s->flags&CODEC_FLAG_PASS1){
        for(i=0; i<count; i++){
            LOAD2;
            STAT2;
        }
    }
    if(s->avctx->flags2&CODEC_FLAG2_NO_OUTPUT)
        return 0;

    if(s->context){
        for(i=0; i<count; i++){
            LOAD2;
            STAT2;
            WRITE2;
        }
    }else{
        for(i=0; i<count; i++){
            LOAD2;
            WRITE2;
        }
    }
    return 0;
}
#endif /* CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER */

static av_always_inline void decode_bgr_1(HYuvContext *s, int count, int decorrelate, int alpha){
    int i;
    for(i=0; i<count; i++){
        int code = get_vlc2(&s->gb, s->vlc[3].table, VLC_BITS, 1);
        if(code != -1){
            *(uint32_t*)&s->temp[0][4*i] = s->pix_bgr_map[code];
        }else if(decorrelate){
            s->temp[0][4*i+G] = get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3);
            s->temp[0][4*i+B] = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3) + s->temp[0][4*i+G];
            s->temp[0][4*i+R] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3) + s->temp[0][4*i+G];
        }else{
            s->temp[0][4*i+B] = get_vlc2(&s->gb, s->vlc[0].table, VLC_BITS, 3);
            s->temp[0][4*i+G] = get_vlc2(&s->gb, s->vlc[1].table, VLC_BITS, 3);
            s->temp[0][4*i+R] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3);
        }
        if(alpha)
            s->temp[0][4*i+A] = get_vlc2(&s->gb, s->vlc[2].table, VLC_BITS, 3);
    }
}

static void decode_bgr_bitstream(HYuvContext *s, int count){
    if(s->decorrelate){
        if(s->bitstream_bpp==24)
            decode_bgr_1(s, count, 1, 0);
        else
            decode_bgr_1(s, count, 1, 1);
    }else{
        if(s->bitstream_bpp==24)
            decode_bgr_1(s, count, 0, 0);
        else
            decode_bgr_1(s, count, 0, 1);
    }
}

static int encode_bgr_bitstream(HYuvContext *s, int count){
    int i;

    if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < 3*4*count){
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

#define LOAD3\
            int g= s->temp[0][4*i+G];\
            int b= (s->temp[0][4*i+B] - g) & 0xff;\
            int r= (s->temp[0][4*i+R] - g) & 0xff;
#define STAT3\
            s->stats[0][b]++;\
            s->stats[1][g]++;\
            s->stats[2][r]++;
#define WRITE3\
            put_bits(&s->pb, s->len[1][g], s->bits[1][g]);\
            put_bits(&s->pb, s->len[0][b], s->bits[0][b]);\
            put_bits(&s->pb, s->len[2][r], s->bits[2][r]);

    if((s->flags&CODEC_FLAG_PASS1) && (s->avctx->flags2&CODEC_FLAG2_NO_OUTPUT)){
        for(i=0; i<count; i++){
            LOAD3;
            STAT3;
        }
    }else if(s->context || (s->flags&CODEC_FLAG_PASS1)){
        for(i=0; i<count; i++){
            LOAD3;
            STAT3;
            WRITE3;
        }
    }else{
        for(i=0; i<count; i++){
            LOAD3;
            WRITE3;
        }
    }
    return 0;
}

#if CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER
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

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt){
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    HYuvContext *s = avctx->priv_data;
    const int width= s->width;
    const int width2= s->width>>1;
    const int height= s->height;
    int fake_ystride, fake_ustride, fake_vstride;
    AVFrame * const p= &s->picture;
    int table_size= 0;

    AVFrame *picture = data;

    av_fast_malloc(&s->bitstream_buffer, &s->bitstream_buffer_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!s->bitstream_buffer)
        return AVERROR(ENOMEM);

    memset(s->bitstream_buffer + buf_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    s->dsp.bswap_buf((uint32_t*)s->bitstream_buffer, (const uint32_t*)buf, buf_size/4);

    if(p->data[0])
        ff_thread_release_buffer(avctx, p);

    p->reference= 0;
    if(ff_thread_get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if(s->context){
        table_size = read_huffman_tables(s, s->bitstream_buffer, buf_size);
        if(table_size < 0)
            return -1;
    }

    if((unsigned)(buf_size-table_size) >= INT_MAX/8)
        return -1;

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
                lefty= s->dsp.add_hfyu_left_prediction(p->data[0] + 2, s->temp[0], width-2, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= s->dsp.add_hfyu_left_prediction(p->data[1] + 1, s->temp[1], width2-1, leftu);
                    leftv= s->dsp.add_hfyu_left_prediction(p->data[2] + 1, s->temp[2], width2-1, leftv);
                }

                for(cy=y=1; y<s->height; y++,cy++){
                    uint8_t *ydst, *udst, *vdst;

                    if(s->bitstream_bpp==12){
                        decode_gray_bitstream(s, width);

                        ydst= p->data[0] + p->linesize[0]*y;

                        lefty= s->dsp.add_hfyu_left_prediction(ydst, s->temp[0], width, lefty);
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
                    lefty= s->dsp.add_hfyu_left_prediction(ydst, s->temp[0], width, lefty);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        leftu= s->dsp.add_hfyu_left_prediction(udst, s->temp[1], width2, leftu);
                        leftv= s->dsp.add_hfyu_left_prediction(vdst, s->temp[2], width2, leftv);
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
                lefty= s->dsp.add_hfyu_left_prediction(p->data[0] + 2, s->temp[0], width-2, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= s->dsp.add_hfyu_left_prediction(p->data[1] + 1, s->temp[1], width2-1, leftu);
                    leftv= s->dsp.add_hfyu_left_prediction(p->data[2] + 1, s->temp[2], width2-1, leftv);
                }

                cy=y=1;

                /* second line is left predicted for interlaced case */
                if(s->interlaced){
                    decode_422_bitstream(s, width);
                    lefty= s->dsp.add_hfyu_left_prediction(p->data[0] + p->linesize[0], s->temp[0], width, lefty);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        leftu= s->dsp.add_hfyu_left_prediction(p->data[1] + p->linesize[2], s->temp[1], width2, leftu);
                        leftv= s->dsp.add_hfyu_left_prediction(p->data[2] + p->linesize[1], s->temp[2], width2, leftv);
                    }
                    y++; cy++;
                }

                /* next 4 pixels are left predicted too */
                decode_422_bitstream(s, 4);
                lefty= s->dsp.add_hfyu_left_prediction(p->data[0] + fake_ystride, s->temp[0], 4, lefty);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    leftu= s->dsp.add_hfyu_left_prediction(p->data[1] + fake_ustride, s->temp[1], 2, leftu);
                    leftv= s->dsp.add_hfyu_left_prediction(p->data[2] + fake_vstride, s->temp[2], 2, leftv);
                }

                /* next line except the first 4 pixels is median predicted */
                lefttopy= p->data[0][3];
                decode_422_bitstream(s, width-4);
                s->dsp.add_hfyu_median_prediction(p->data[0] + fake_ystride+4, p->data[0]+4, s->temp[0], width-4, &lefty, &lefttopy);
                if(!(s->flags&CODEC_FLAG_GRAY)){
                    lefttopu= p->data[1][1];
                    lefttopv= p->data[2][1];
                    s->dsp.add_hfyu_median_prediction(p->data[1] + fake_ustride+2, p->data[1]+2, s->temp[1], width2-2, &leftu, &lefttopu);
                    s->dsp.add_hfyu_median_prediction(p->data[2] + fake_vstride+2, p->data[2]+2, s->temp[2], width2-2, &leftv, &lefttopv);
                }
                y++; cy++;

                for(; y<height; y++,cy++){
                    uint8_t *ydst, *udst, *vdst;

                    if(s->bitstream_bpp==12){
                        while(2*cy > y){
                            decode_gray_bitstream(s, width);
                            ydst= p->data[0] + p->linesize[0]*y;
                            s->dsp.add_hfyu_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                            y++;
                        }
                        if(y>=height) break;
                    }
                    draw_slice(s, y);

                    decode_422_bitstream(s, width);

                    ydst= p->data[0] + p->linesize[0]*y;
                    udst= p->data[1] + p->linesize[1]*cy;
                    vdst= p->data[2] + p->linesize[2]*cy;

                    s->dsp.add_hfyu_median_prediction(ydst, ydst - fake_ystride, s->temp[0], width, &lefty, &lefttopy);
                    if(!(s->flags&CODEC_FLAG_GRAY)){
                        s->dsp.add_hfyu_median_prediction(udst, udst - fake_ustride, s->temp[1], width2, &leftu, &lefttopu);
                        s->dsp.add_hfyu_median_prediction(vdst, vdst - fake_vstride, s->temp[2], width2, &leftv, &lefttopv);
                    }
                }

                draw_slice(s, height);
                break;
            }
        }
    }else{
        int y;
        int leftr, leftg, leftb, lefta;
        const int last_line= (height-1)*p->linesize[0];

        if(s->bitstream_bpp==32){
            lefta= p->data[0][last_line+A]= get_bits(&s->gb, 8);
            leftr= p->data[0][last_line+R]= get_bits(&s->gb, 8);
            leftg= p->data[0][last_line+G]= get_bits(&s->gb, 8);
            leftb= p->data[0][last_line+B]= get_bits(&s->gb, 8);
        }else{
            leftr= p->data[0][last_line+R]= get_bits(&s->gb, 8);
            leftg= p->data[0][last_line+G]= get_bits(&s->gb, 8);
            leftb= p->data[0][last_line+B]= get_bits(&s->gb, 8);
            lefta= p->data[0][last_line+A]= 255;
            skip_bits(&s->gb, 8);
        }

        if(s->bgr32){
            switch(s->predictor){
            case LEFT:
            case PLANE:
                decode_bgr_bitstream(s, width-1);
                s->dsp.add_hfyu_left_prediction_bgr32(p->data[0] + last_line+4, s->temp[0], width-1, &leftr, &leftg, &leftb, &lefta);

                for(y=s->height-2; y>=0; y--){ //Yes it is stored upside down.
                    decode_bgr_bitstream(s, width);

                    s->dsp.add_hfyu_left_prediction_bgr32(p->data[0] + p->linesize[0]*y, s->temp[0], width, &leftr, &leftg, &leftb, &lefta);
                    if(s->predictor == PLANE){
                        if(s->bitstream_bpp!=32) lefta=0;
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

    return (get_bits_count(&s->gb)+31)/32*4 + table_size;
}
#endif /* CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER */

static int common_end(HYuvContext *s){
    int i;

    for(i=0; i<3; i++){
        av_freep(&s->temp[i]);
    }
    return 0;
}

#if CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER
static av_cold int decode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    common_end(s);
    av_freep(&s->bitstream_buffer);

    for(i=0; i<6; i++){
        free_vlc(&s->vlc[i]);
    }

    return 0;
}
#endif /* CONFIG_HUFFYUV_DECODER || CONFIG_FFVHUFF_DECODER */

#if CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER
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
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    if(s->context){
        for(i=0; i<3; i++){
            generate_len_table(s->len[i], s->stats[i]);
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

        lefty= sub_left_prediction(s, s->temp[0], p->data[0], width , 0);
        leftu= sub_left_prediction(s, s->temp[1], p->data[1], width2, 0);
        leftv= sub_left_prediction(s, s->temp[2], p->data[2], width2, 0);

        encode_422_bitstream(s, 2, width-2);

        if(s->predictor==MEDIAN){
            int lefttopy, lefttopu, lefttopv;
            cy=y=1;
            if(s->interlaced){
                lefty= sub_left_prediction(s, s->temp[0], p->data[0]+p->linesize[0], width , lefty);
                leftu= sub_left_prediction(s, s->temp[1], p->data[1]+p->linesize[1], width2, leftu);
                leftv= sub_left_prediction(s, s->temp[2], p->data[2]+p->linesize[2], width2, leftv);

                encode_422_bitstream(s, 0, width);
                y++; cy++;
            }

            lefty= sub_left_prediction(s, s->temp[0], p->data[0]+fake_ystride, 4, lefty);
            leftu= sub_left_prediction(s, s->temp[1], p->data[1]+fake_ustride, 2, leftu);
            leftv= sub_left_prediction(s, s->temp[2], p->data[2]+fake_vstride, 2, leftv);

            encode_422_bitstream(s, 0, 4);

            lefttopy= p->data[0][3];
            lefttopu= p->data[1][1];
            lefttopv= p->data[2][1];
            s->dsp.sub_hfyu_median_prediction(s->temp[0], p->data[0]+4, p->data[0] + fake_ystride+4, width-4 , &lefty, &lefttopy);
            s->dsp.sub_hfyu_median_prediction(s->temp[1], p->data[1]+2, p->data[1] + fake_ustride+2, width2-2, &leftu, &lefttopu);
            s->dsp.sub_hfyu_median_prediction(s->temp[2], p->data[2]+2, p->data[2] + fake_vstride+2, width2-2, &leftv, &lefttopv);
            encode_422_bitstream(s, 0, width-4);
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

                encode_422_bitstream(s, 0, width);
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

                encode_422_bitstream(s, 0, width);
            }
        }
    }else if(avctx->pix_fmt == PIX_FMT_RGB32){
        uint8_t *data = p->data[0] + (height-1)*p->linesize[0];
        const int stride = -p->linesize[0];
        const int fake_stride = -fake_ystride;
        int y;
        int leftr, leftg, leftb;

        put_bits(&s->pb, 8, leftr= data[R]);
        put_bits(&s->pb, 8, leftg= data[G]);
        put_bits(&s->pb, 8, leftb= data[B]);
        put_bits(&s->pb, 8, 0);

        sub_left_prediction_bgr32(s, s->temp[0], data+4, width-1, &leftr, &leftg, &leftb);
        encode_bgr_bitstream(s, width-1);

        for(y=1; y<s->height; y++){
            uint8_t *dst = data + y*stride;
            if(s->predictor == PLANE && s->interlaced < y){
                s->dsp.diff_bytes(s->temp[1], dst, dst - fake_stride, width*4);
                sub_left_prediction_bgr32(s, s->temp[0], s->temp[1], width, &leftr, &leftg, &leftb);
            }else{
                sub_left_prediction_bgr32(s, s->temp[0], dst, width, &leftr, &leftg, &leftb);
            }
            encode_bgr_bitstream(s, width);
        }
    }else{
        av_log(avctx, AV_LOG_ERROR, "Format not supported!\n");
    }
    emms_c();

    size+= (put_bits_count(&s->pb)+31)/8;
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 15, 0);
    size/= 4;

    if((s->flags&CODEC_FLAG_PASS1) && (s->picture_number&31)==0){
        int j;
        char *p= avctx->stats_out;
        char *end= p + 1024*30;
        for(i=0; i<3; i++){
            for(j=0; j<256; j++){
                snprintf(p, end-p, "%"PRIu64" ", s->stats[i][j]);
                p+= strlen(p);
                s->stats[i][j]= 0;
            }
            snprintf(p, end-p, "\n");
            p++;
        }
    } else
        avctx->stats_out[0] = '\0';
    if(!(s->avctx->flags2 & CODEC_FLAG2_NO_OUTPUT)){
        flush_put_bits(&s->pb);
        s->dsp.bswap_buf((uint32_t*)buf, (uint32_t*)buf, size);
    }

    s->picture_number++;

    return size*4;
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    common_end(s);

    av_freep(&avctx->extradata);
    av_freep(&avctx->stats_out);

    return 0;
}
#endif /* CONFIG_HUFFYUV_ENCODER || CONFIG_FFVHUFF_ENCODER */

#if CONFIG_HUFFYUV_DECODER
AVCodec ff_huffyuv_decoder = {
    .name           = "huffyuv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_HUFFYUV,
    .priv_data_size = sizeof(HYuvContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_FRAME_THREADS,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .long_name = NULL_IF_CONFIG_SMALL("Huffyuv / HuffYUV"),
};
#endif

#if CONFIG_FFVHUFF_DECODER
AVCodec ff_ffvhuff_decoder = {
    .name           = "ffvhuff",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_FFVHUFF,
    .priv_data_size = sizeof(HYuvContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND | CODEC_CAP_FRAME_THREADS,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .long_name = NULL_IF_CONFIG_SMALL("Huffyuv FFmpeg variant"),
};
#endif

#if CONFIG_HUFFYUV_ENCODER
AVCodec ff_huffyuv_encoder = {
    .name           = "huffyuv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_HUFFYUV,
    .priv_data_size = sizeof(HYuvContext),
    .init           = encode_init,
    .encode         = encode_frame,
    .close          = encode_end,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_YUV422P, PIX_FMT_RGB32, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Huffyuv / HuffYUV"),
};
#endif

#if CONFIG_FFVHUFF_ENCODER
AVCodec ff_ffvhuff_encoder = {
    .name           = "ffvhuff",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_FFVHUFF,
    .priv_data_size = sizeof(HYuvContext),
    .init           = encode_init,
    .encode         = encode_frame,
    .close          = encode_end,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_RGB32, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Huffyuv FFmpeg variant"),
};
#endif
