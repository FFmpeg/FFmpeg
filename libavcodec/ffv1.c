/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 */
 
/**
 * @file ffv1.c
 * FF Video Codec 1 (a experimental lossless codec)
 */

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"
#include "cabac.h"

#define MAX_PLANES 4
#if 0
#define DEFAULT_QDIFF_COUNT (9)

static const uint8_t default_quant_table[512]={
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3,
 4,
 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};
#else
#define DEFAULT_QDIFF_COUNT (16)

static const uint8_t default_quant_table[256]={
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7,
8,
 9,10,11,11,12,12,12,12,13,13,13,13,13,13,13,13,
14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
 };
#endif

static const int to8[16]={
0,1,1,1,
1,2,2,3,
4,5,6,6,
7,7,7,7
};

typedef struct PlaneContext{
    uint8_t quant_table[256];
    int qdiff_count;
    int context_count;
    uint8_t (*state)[64]; //FIXME 64
    uint8_t interlace_bit_state[2];
} PlaneContext;

typedef struct FFV1Context{
    AVCodecContext *avctx;
    CABACContext c;
    int version;
    int width, height;
    int chroma_h_shift, chroma_v_shift;
    int flags;
    int picture_number;
    AVFrame picture;
    int plane_count;
    PlaneContext plane[MAX_PLANES];
    
    DSPContext dsp; 
}FFV1Context;

 //1.774215
static inline int predict(FFV1Context *s, uint8_t *src, int stride, int x, int y){
    if(x && y){
//        const int RT= src[+1-stride];
        const int LT= src[-1-stride];
        const int  T= src[  -stride];
        const int L = src[-1       ];
        uint8_t *cm = cropTbl + MAX_NEG_CROP;    
        const int gradient= cm[L + T - LT];

//        return gradient;
        return mid_pred(L, gradient, T);
    }else{
        if(y){
            return src[  -stride];
        }else if(x){
            return src[-1       ];
        }else{
            return 128;
        }
    }
}


#if 0
static inline void put_symbol(CABACContext, uint8_t *state, int v){
    put_cabac_ueg(c, state, v, 32, 1, 4 , 32);
}

static inline int get_symbol(CABACContext, uint8_t *state){
    return get_cabac_ueg(c, state, 32, 1, 4 , 32);
}
#elif 0
static inline void put_symbol(CABACContext *c, uint8_t *state, int v){
    if(v==0)
        put_cabac(c, state+0, 1);
    else{
        put_cabac(c, state+0, 0);
        put_cabac(c, state+1, v<0);
        if(v<0) state += 64;
        put_cabac_ueg(c, state+2, ABS(v)-1, 32, 0, 4 , 32);
    }
}

static inline int get_symbol(CABACContext *c, uint8_t *state){
    if(get_cabac(c, state+0))
        return 0;
    else{
        int sign= get_cabac(c, state+1);
        if(sign) 
            return -1-get_cabac_ueg(c, state+66, 32, 0, 4 , 32);
        else
            return  1+get_cabac_ueg(c, state+2 , 32, 0, 4 , 32);
    }
}
#else
/**
 * put 
 */
static inline void put_symbol(CABACContext *c, uint8_t *state, int v, int is_signed){
    int i;
#if 0
    const int a= ABS(v);
    const int e= av_log2(a+1);

    put_cabac_u(c, state+0, e, 7, 6, 1); //0..6
    if(e){
        put_cabac(c, state+6 + e, v < 0); //7..13
        
        for(i=; i<e; i++){
            
        }
    }
#else
// 0 1 2 3 4  5  6
// 0 1 2 3 4  5  6
// 0 0 1 3 6 10 15 21
    if(v){
        const int a= ABS(v);
        const int e= av_log2(a);

        put_cabac(c, state+0, 0);
        put_cabac_u(c, state+1, e, 7, 6, 1); //1..7
        if(e<7){
            for(i=e-1; i>=0; i--){
                static const int offset[7]= {14+0, 14+0, 14+1, 14+3, 14+6, 14+10, 14+15};
//                put_cabac(c, state+14+e-i, (a>>i)&1); //14..20
               put_cabac(c, state+offset[e]+i, (a>>i)&1); //14..34
            }

            if(is_signed)
                put_cabac(c, state+8 + e, v < 0); //8..14
        }
    }else{
        put_cabac(c, state+0, 1);
    }
#endif    
}

static inline int get_symbol(CABACContext *c, uint8_t *state, int is_signed){
    int i;

    if(get_cabac(c, state+0))
        return 0;
    else{
        const int e= get_cabac_u(c, state+1, 7, 6, 1); //1..7
        
        if(e<7){
            int a= 1<<e;

            for(i=e-1; i>=0; i--){
                static const int offset[7]= {14+0, 14+0, 14+1, 14+3, 14+6, 14+10, 14+15};
                a += get_cabac(c, state+offset[e]+i)<<i; //14..34
            }

            if(is_signed && get_cabac(c, state+8 + e)) //8..14
                return -a;
            else
                return a;
        }else
            return -128;
    }
}
#endif

static void encode_plane(FFV1Context *s, uint8_t *src, int w, int h, int stride, int plane_index){
    PlaneContext * const p= &s->plane[plane_index];
    CABACContext * const c= &s->c;
    int x,y;
    uint8_t pred_diff_buffer[4][w+6];
    uint8_t *pred_diff[4]= {pred_diff_buffer[0]+3, pred_diff_buffer[1]+3, pred_diff_buffer[2]+3, pred_diff_buffer[3]+3};
//    uint8_t temp_buf[3*w], *temp= temp_buf + 3*w;
    
    memset(pred_diff_buffer, 0, sizeof(pred_diff_buffer));
    
    for(y=0; y<h; y++){
        uint8_t *temp= pred_diff[0]; //FIXME try a normal buffer

        pred_diff[0]= pred_diff[1];
        pred_diff[1]= pred_diff[2];
        pred_diff[2]= pred_diff[3];
        pred_diff[3]= temp;

        for(x=0; x<w; x++){
            uint8_t *temp_src= src + x + stride*y;
            int diff, context, qdiff;
             
            if(p->context_count == 256)
                context= pred_diff[3+0][x-1] + 16*pred_diff[3-1][x+0];
            else
                context=            pred_diff[3+0][x-1] + 16*pred_diff[3-1][x+0] 
                        + 16*16*to8[pred_diff[3-1][x+1]] + 16*16*8*to8[pred_diff[3-0][x-2]] + 16*16*8*8*to8[pred_diff[3-2][x+0]];
             
            diff = (int8_t)(temp_src[0] - predict(s, temp_src, stride, x, y));
            
            qdiff= p->quant_table[128+diff];
            
            put_symbol(c, p->state[context], diff, 1);
            
            pred_diff[3][x]= qdiff;
        }
    }
}

static void write_quant_table(CABACContext *c, uint8_t *quant_table){
    int last=0;
    int i;
    uint8_t state[64]={0};

    for(i=1; i<256 ; i++){
        if(quant_table[i] != quant_table[i-1]){
            put_symbol(c, state, i-last-1, 0);
            last= i;
        }
    }
    put_symbol(c, state, i-last-1, 0);
}

static void write_header(FFV1Context *f){
    uint8_t state[64]={0};
    int i;
    CABACContext * const c= &f->c;

    put_symbol(c, state, f->version, 0);
    put_symbol(c, state, 0, 0); //YUV cs type 
    put_cabac(c, state, 1); //chroma planes
        put_symbol(c, state, f->chroma_h_shift, 0);
        put_symbol(c, state, f->chroma_v_shift, 0);
    put_cabac(c, state, 0); //no transparency plane

    for(i=0; i<3; i++){ //FIXME chroma & trasparency decission
        PlaneContext * const p= &f->plane[i];
        
        put_symbol(c, state, av_log2(p->context_count), 0);
        write_quant_table(c, p->quant_table);
    }
}

static int common_init(AVCodecContext *avctx){
    FFV1Context *s = avctx->priv_data;
    int i, j, width, height;

    s->avctx= avctx;
    s->flags= avctx->flags;
        
    dsputil_init(&s->dsp, avctx);
    
    width= s->width= avctx->width;
    height= s->height= avctx->height;
    
    assert(width && height);

    for(i=0; i<s->plane_count; i++){
        PlaneContext *p= &s->plane[i];
    }

    return 0;
}

static int encode_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i;

    common_init(avctx);
 
    s->version=0;
    
    s->plane_count=3;

    for(i=0; i<s->plane_count; i++){
        PlaneContext * const p= &s->plane[i];
        memcpy(p->quant_table, default_quant_table, sizeof(uint8_t)*256);
        p->qdiff_count= DEFAULT_QDIFF_COUNT;
        
#if 1
        p->context_count= 256;
        p->state= av_malloc(64*p->context_count*sizeof(uint8_t));
#else        
        p->context_count= 16*16*8*8*8; //256*16;
        p->state= av_malloc(64*p->context_count*sizeof(uint8_t));
#endif
    }

    avctx->coded_frame= &s->picture;
    switch(avctx->pix_fmt){
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV410P:
        avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_h_shift, &s->chroma_v_shift);
        break;
    default:
        fprintf(stderr, "format not supported\n");
        return -1;
    }
    
   
    s->picture_number=0;
    
    return 0;
}


static void clear_state(FFV1Context *f){
    int i;

    for(i=0; i<f->plane_count; i++){
        PlaneContext *p= &f->plane[i];

        p->interlace_bit_state[0]= 0;
        p->interlace_bit_state[1]= 0;
        
        memset(p->state, 0, p->context_count*sizeof(uint8_t)*64);
    }
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    FFV1Context *f = avctx->priv_data;
    CABACContext * const c= &f->c;
    AVFrame *pict = data;
    const int width= f->width;
    const int height= f->height;
    AVFrame * const p= &f->picture;

    if(avctx->strict_std_compliance >= 0){
        printf("this codec is under development, files encoded with it wont be decodeable with future versions!!!\n"
               "use vstrict=-1 to use it anyway\n");
        return -1;
    }
        
    ff_init_cabac_encoder(c, buf, buf_size);
    ff_init_cabac_states(c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);
    
    *p = *pict;
    p->pict_type= FF_I_TYPE;
    
    if(avctx->gop_size==0 || f->picture_number % avctx->gop_size == 0){
        put_cabac_bypass(c, 1);
        p->key_frame= 1;
        write_header(f);
        clear_state(f);
    }else{
        put_cabac_bypass(c, 0);
        p->key_frame= 0;
    }

    if(1){
        const int chroma_width = -((-width )>>f->chroma_h_shift);
        const int chroma_height= -((-height)>>f->chroma_v_shift);

        encode_plane(f, p->data[0], width, height, p->linesize[0], 0);

        encode_plane(f, p->data[1], chroma_width, chroma_height, p->linesize[1], 1);
        encode_plane(f, p->data[2], chroma_width, chroma_height, p->linesize[2], 2);
    }
    emms_c();
    
    f->picture_number++;

    return put_cabac_terminate(c, 1);
}

static void common_end(FFV1Context *s){
    int i; 

    for(i=0; i<s->plane_count; i++){
        PlaneContext *p= &s->plane[i];

        av_freep(&p->state);
    }
}

static int encode_end(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;

    common_end(s);

    return 0;
}

static void decode_plane(FFV1Context *s, uint8_t *src, int w, int h, int stride, int plane_index){
    PlaneContext * const p= &s->plane[plane_index];
    CABACContext * const c= &s->c;
    int x,y;
    uint8_t pred_diff_buffer[4][w+6];
    uint8_t *pred_diff[4]= {pred_diff_buffer[0]+3, pred_diff_buffer[1]+3, pred_diff_buffer[2]+3, pred_diff_buffer[3]+3};
//    uint8_t temp_buf[3*w], *temp= temp_buf + 3*w;
    
    memset(pred_diff_buffer, 0, sizeof(pred_diff_buffer));
    
    for(y=0; y<h; y++){
        uint8_t *temp= pred_diff[0]; //FIXME try a normal buffer

        pred_diff[0]= pred_diff[1];
        pred_diff[1]= pred_diff[2];
        pred_diff[2]= pred_diff[3];
        pred_diff[3]= temp;

        for(x=0; x<w; x++){
            uint8_t *temp_src= src + x + stride*y;
            int diff, context, qdiff;
             
            if(p->context_count == 256)
                context= pred_diff[3+0][x-1] + 16*pred_diff[3-1][x+0];
            else
                context=            pred_diff[3+0][x-1] + 16*pred_diff[3-1][x+0] 
                        + 16*16*to8[pred_diff[3-1][x+1]] + 16*16*8*to8[pred_diff[3-0][x-2]] + 16*16*8*8*to8[pred_diff[3-2][x+0]];

            diff= get_symbol(c, p->state[context], 1);

            temp_src[0] = predict(s, temp_src, stride, x, y) + diff;
            
            assert(diff>= -128 && diff <= 127);

            qdiff= p->quant_table[128+diff];

            pred_diff[3][x]= qdiff;
        }
    }
}

static int read_quant_table(CABACContext *c, uint8_t *quant_table){
    int v;
    int i=0;
    uint8_t state[64]={0};

    for(v=0; i<256 ; v++){
        int len= get_symbol(c, state, 0) + 1;

        if(len + i > 256) return -1;
        
        while(len--){
            quant_table[i++] = v;
//printf("%2d ",v);
//if(i%16==0) printf("\n");
        }
    }
    
    return v;
}

static int read_header(FFV1Context *f){
    uint8_t state[64]={0};
    int i;
    CABACContext * const c= &f->c;
    
    f->version= get_symbol(c, state, 0);
    get_symbol(c, state, 0); //YUV cs type
    get_cabac(c, state); //no chroma = false
    f->chroma_h_shift= get_symbol(c, state, 0);
    f->chroma_v_shift= get_symbol(c, state, 0);
    get_cabac(c, state); //transparency plane
    f->plane_count= 3;
    
    for(i=0; i<f->plane_count; i++){
        PlaneContext * const p= &f->plane[i];

        p->context_count= 1<<get_symbol(c, state, 0);
        p->qdiff_count= read_quant_table(c, p->quant_table);
        if(p->qdiff_count < 0) return -1;
        
        if(!p->state)
            p->state= av_malloc(64*p->context_count*sizeof(uint8_t));
    }
    
    return 0;
}

static int decode_init(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;

    common_init(avctx);
    
#if 0    
    switch(s->bitstream_bpp){
    case 12:
        avctx->pix_fmt = PIX_FMT_YUV420P;
        break;
    case 16:
        avctx->pix_fmt = PIX_FMT_YUV422P;
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
#endif
    
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size){
    FFV1Context *f = avctx->priv_data;
    CABACContext * const c= &f->c;
    const int width= f->width;
    const int height= f->height;
    AVFrame * const p= &f->picture;
    int bytes_read;

    AVFrame *picture = data;

    *data_size = 0;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    ff_init_cabac_decoder(c, buf, buf_size);
    ff_init_cabac_states(c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        fprintf(stderr, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type= FF_I_TYPE; //FIXME I vs. P
    if(get_cabac_bypass(c)){
        p->key_frame= 1;
        read_header(f);
        clear_state(f);
    }else{
        p->key_frame= 0;
    }
    if(avctx->debug&FF_DEBUG_PICT_INFO)
        printf("keyframe:%d\n", p->key_frame);
    
    
    if(1){
        const int chroma_width = -((-width )>>f->chroma_h_shift);
        const int chroma_height= -((-height)>>f->chroma_v_shift);
        decode_plane(f, p->data[0], width, height, p->linesize[0], 0);
        
        decode_plane(f, p->data[1], chroma_width, chroma_height, p->linesize[1], 1);
        decode_plane(f, p->data[2], chroma_width, chroma_height, p->linesize[2], 2);
    }
        
    emms_c();

    f->picture_number++;

    *picture= *p;
    
    avctx->release_buffer(avctx, p); //FIXME

    *data_size = sizeof(AVFrame);
    
    bytes_read= get_cabac_terminate(c);
    if(bytes_read ==0) printf("error at end of frame\n");
    
    return bytes_read;
}

static int decode_end(AVCodecContext *avctx)
{
    FFV1Context *s = avctx->priv_data;
    int i;
    
    if(avctx->get_buffer == avcodec_default_get_buffer){
        for(i=0; i<4; i++){
            av_freep(&s->picture.base[i]);
            s->picture.data[i]= NULL;
        }
        av_freep(&s->picture.opaque);
    }

    return 0;
}

AVCodec ffv1_decoder = {
    "ffv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FFV1,
    sizeof(FFV1Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    /*CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND*/ 0,
    NULL
};

AVCodec ffv1_encoder = {
    "ffv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FFV1,
    sizeof(FFV1Context),
    encode_init,
    encode_frame,
    encode_end,
};
