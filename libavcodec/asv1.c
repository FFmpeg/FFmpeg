/*
 * ASUS V1 codec
 * Copyright (c) 2003 Michael Niedermayer
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
 */
 
/**
 * @file asv1.c
 * ASUS V1 codec.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#undef NDEBUG
//#include <assert.h>

#define VLC_BITS 5

typedef struct ASV1Context{
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame picture;
    PutBitContext pb;
    GetBitContext gb;
    ScanTable scantable;
    int inv_qscale;
    int mb_width;
    int mb_height;
    int mb_width2;
    int mb_height2;
    DCTELEM __align8 block[6][64];
    uint16_t __align8 intra_matrix[64];
    int __align8 q_intra_matrix[64];
    uint8_t *bitstream_buffer;
    int bitstream_buffer_size;
} ASV1Context;

static const uint8_t scantab[64]={
    0x00,0x08,0x01,0x09,0x10,0x18,0x11,0x19,
    0x02,0x0A,0x03,0x0B,0x12,0x1A,0x13,0x1B,
    0x04,0x0C,0x05,0x0D,0x20,0x28,0x21,0x29,
    0x06,0x0E,0x07,0x0F,0x14,0x1C,0x15,0x1D,
    0x22,0x2A,0x23,0x2B,0x30,0x38,0x31,0x39,
};

static const uint8_t ccp_tab[17][2]={
    {0x2,2}, {0xE,5}, {0xD,5}, {0xC,5},
    {0xB,5}, {0xA,5}, {0x9,5}, {0x8,5},
    {0x7,5}, {0x6,5}, {0x5,5}, {0x4,5},
    {0x3,5}, {0x2,5}, {0x1,5}, {0x3,2},
    {0xF,5}, //EOB
};

static const uint8_t level_tab[7][2]={
    {3,4}, {3,3}, {3,2}, {0,3}, {2,2}, {2,3}, {2,4}
};

static VLC ccp_vlc;
static VLC level_vlc;

static void init_vlcs(ASV1Context *a){
    static int done = 0;

    if (!done) {
        done = 1;

        init_vlc(&ccp_vlc, VLC_BITS, 17, 
                 &ccp_tab[0][1], 2, 1,
                 &ccp_tab[0][0], 2, 1);
        init_vlc(&level_vlc,  VLC_BITS, 7, 
                 &level_tab[0][1], 2, 1,
                 &level_tab[0][0], 2, 1);
    }
}

static inline int get_level(GetBitContext *gb){
    int code= get_vlc2(gb, level_vlc.table, VLC_BITS, 1);

    if(code==3) return get_sbits(gb, 8);
    else        return code - 3;
}

static inline void put_level(PutBitContext *pb, int level){
    unsigned int index= level + 3;

    if(index <= 6) put_bits(pb, level_tab[index][1], level_tab[index][0]);
    else{
        put_bits(pb, level_tab[3][1], level_tab[3][0]);
        put_bits(pb, 8, level&0xFF);
    }
}

static inline int decode_block(ASV1Context *a, DCTELEM block[64]){
    int i;

    block[0]= 8*get_bits(&a->gb, 8);
    
    for(i=0; i<11; i++){
        const int ccp= get_vlc2(&a->gb, ccp_vlc.table, VLC_BITS, 1);

        if(ccp){
            if(ccp == 16) break;
            if(ccp < 0 || i>=10){
                printf("coded coeff pattern damaged\n");
                return -1;
            }

            if(ccp&1) block[a->scantable.permutated[4*i+0]]= (get_level(&a->gb) * a->intra_matrix[4*i+0])>>4;
            if(ccp&2) block[a->scantable.permutated[4*i+1]]= (get_level(&a->gb) * a->intra_matrix[4*i+1])>>4;;
            if(ccp&4) block[a->scantable.permutated[4*i+2]]= (get_level(&a->gb) * a->intra_matrix[4*i+2])>>4;;
            if(ccp&8) block[a->scantable.permutated[4*i+3]]= (get_level(&a->gb) * a->intra_matrix[4*i+3])>>4;;
        }
    }

    return 0;
}

static inline void encode_block(ASV1Context *a, DCTELEM block[64]){
    int i;
    int nc_count=0;
    
    put_bits(&a->pb, 8, (block[0] + 32)>>6);
    block[0]= 0;
    
    for(i=0; i<10; i++){
        const int index= scantab[4*i];
        int ccp=0;

        if( (block[index + 0] = (block[index + 0]*a->q_intra_matrix[index + 0] + (1<<15))>>16) ) ccp |= 1;
        if( (block[index + 8] = (block[index + 8]*a->q_intra_matrix[index + 8] + (1<<15))>>16) ) ccp |= 2;
        if( (block[index + 1] = (block[index + 1]*a->q_intra_matrix[index + 1] + (1<<15))>>16) ) ccp |= 4;
        if( (block[index + 9] = (block[index + 9]*a->q_intra_matrix[index + 9] + (1<<15))>>16) ) ccp |= 8;

        if(ccp){
            for(;nc_count; nc_count--) 
                put_bits(&a->pb, ccp_tab[0][1], ccp_tab[0][0]);

            put_bits(&a->pb, ccp_tab[ccp][1], ccp_tab[ccp][0]);
            
            if(ccp&1) put_level(&a->pb, block[index + 0]);
            if(ccp&2) put_level(&a->pb, block[index + 8]);
            if(ccp&4) put_level(&a->pb, block[index + 1]);
            if(ccp&8) put_level(&a->pb, block[index + 9]);
        }else{
            nc_count++;
        }
    }
    put_bits(&a->pb, ccp_tab[16][1], ccp_tab[16][0]);
}

static inline int decode_mb(ASV1Context *a, DCTELEM block[6][64]){
    int i;

    a->dsp.clear_blocks(block[0]);
    
    for(i=0; i<6; i++){
        if( decode_block(a, block[i]) < 0) 
            return -1;
    }
    return 0;
}

static inline void encode_mb(ASV1Context *a, DCTELEM block[6][64]){
    int i;

    for(i=0; i<6; i++){
        encode_block(a, block[i]);
    }
}
        
static inline void idct_put(ASV1Context *a, int mb_x, int mb_y){
    DCTELEM (*block)[64]= a->block;
    int linesize= a->picture.linesize[0];
    
    uint8_t *dest_y  = a->picture.data[0] + (mb_y * 16* linesize              ) + mb_x * 16;
    uint8_t *dest_cb = a->picture.data[1] + (mb_y * 8 * a->picture.linesize[1]) + mb_x * 8;
    uint8_t *dest_cr = a->picture.data[2] + (mb_y * 8 * a->picture.linesize[2]) + mb_x * 8;

    a->dsp.idct_put(dest_y                 , linesize, block[0]);
    a->dsp.idct_put(dest_y              + 8, linesize, block[1]);
    a->dsp.idct_put(dest_y + 8*linesize    , linesize, block[2]);
    a->dsp.idct_put(dest_y + 8*linesize + 8, linesize, block[3]);

    if(!(a->avctx->flags&CODEC_FLAG_GRAY)){
        a->dsp.idct_put(dest_cb, a->picture.linesize[1], block[4]);
        a->dsp.idct_put(dest_cr, a->picture.linesize[2], block[5]);
    }
}

static inline void dct_get(ASV1Context *a, int mb_x, int mb_y){
    DCTELEM (*block)[64]= a->block;
    int linesize= a->picture.linesize[0];
    int i;
    
    uint8_t *ptr_y  = a->picture.data[0] + (mb_y * 16* linesize              ) + mb_x * 16;
    uint8_t *ptr_cb = a->picture.data[1] + (mb_y * 8 * a->picture.linesize[1]) + mb_x * 8;
    uint8_t *ptr_cr = a->picture.data[2] + (mb_y * 8 * a->picture.linesize[2]) + mb_x * 8;

    a->dsp.get_pixels(block[0], ptr_y                 , linesize);
    a->dsp.get_pixels(block[1], ptr_y              + 8, linesize);
    a->dsp.get_pixels(block[2], ptr_y + 8*linesize    , linesize);
    a->dsp.get_pixels(block[3], ptr_y + 8*linesize + 8, linesize);
    for(i=0; i<4; i++)
        a->dsp.fdct(block[i]);
    
    if(!(a->avctx->flags&CODEC_FLAG_GRAY)){
        a->dsp.get_pixels(block[4], ptr_cb, a->picture.linesize[1]);
        a->dsp.get_pixels(block[5], ptr_cr, a->picture.linesize[2]);
        for(i=4; i<6; i++)
            a->dsp.fdct(block[i]);
    }
}

static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    ASV1Context * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int mb_x, mb_y;

    *data_size = 0;

    /* special case for last picture */
    if (buf_size == 0) {
        return 0;
    }

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        fprintf(stderr, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    a->bitstream_buffer= av_fast_realloc(a->bitstream_buffer, &a->bitstream_buffer_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    a->dsp.bswap_buf((uint32_t*)a->bitstream_buffer, (uint32_t*)buf, buf_size/4);
    init_get_bits(&a->gb, a->bitstream_buffer, buf_size*8);

    for(mb_y=0; mb_y<a->mb_height2; mb_y++){
        for(mb_x=0; mb_x<a->mb_width2; mb_x++){
            if( decode_mb(a, a->block) <0)
                return -1;
             
            idct_put(a, mb_x, mb_y);
        }
    }

    if(a->mb_width2 != a->mb_width){
        mb_x= a->mb_width2;
        for(mb_y=0; mb_y<a->mb_height2; mb_y++){
            if( decode_mb(a, a->block) <0)
                return -1;
             
            idct_put(a, mb_x, mb_y);
        }
    }

    if(a->mb_height2 != a->mb_height){
        mb_y= a->mb_height2;
        for(mb_x=0; mb_x<a->mb_width; mb_x++){
            if( decode_mb(a, a->block) <0)
                return -1;
             
            idct_put(a, mb_x, mb_y);
        }
    }
#if 0    
int i;
printf("%d %d\n", 8*buf_size, get_bits_count(&a->gb));
for(i=get_bits_count(&a->gb); i<8*buf_size; i++){
    printf("%d", get_bits1(&a->gb));
}

for(i=0; i<s->avctx->extradata_size; i++){
    printf("%c\n", ((uint8_t*)s->avctx->extradata)[i]);
}
#endif

    p->quality= (32 + a->inv_qscale/2)/a->inv_qscale;
    memset(p->qscale_table, p->quality, p->qstride*a->mb_height);
    
    *picture= *(AVFrame*)&a->picture;
    *data_size = sizeof(AVPicture);

    emms_c();
    
    return (get_bits_count(&a->gb)+31)/32*4;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    ASV1Context * const a = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int size;
    int mb_x, mb_y;

    init_put_bits(&a->pb, buf, buf_size, NULL, NULL);
    
    *p = *pict;
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    for(mb_y=0; mb_y<a->mb_height2; mb_y++){
        for(mb_x=0; mb_x<a->mb_width2; mb_x++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }

    if(a->mb_width2 != a->mb_width){
        mb_x= a->mb_width2;
        for(mb_y=0; mb_y<a->mb_height2; mb_y++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }

    if(a->mb_height2 != a->mb_height){
        mb_y= a->mb_height2;
        for(mb_x=0; mb_x<a->mb_width; mb_x++){
            dct_get(a, mb_x, mb_y);
            encode_mb(a, a->block);
        }
    }
    emms_c();
    
    align_put_bits(&a->pb);
    while(get_bit_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);
    
    size= get_bit_count(&a->pb)/32;
    
    a->dsp.bswap_buf((uint32_t*)buf, (uint32_t*)buf, size);
    
    return size*4;
}

static void common_init(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;

    dsputil_init(&a->dsp, avctx);

    a->mb_width   = (avctx->width  + 15) / 16;
    a->mb_height  = (avctx->height + 15) / 16;
    a->mb_width2  = (avctx->width  + 0) / 16;
    a->mb_height2 = (avctx->height + 0) / 16;

    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static int decode_init(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;
    AVFrame *p= (AVFrame*)&a->picture;
    int i;
 
    common_init(avctx);
    init_vlcs(a);
    ff_init_scantable(a->dsp.idct_permutation, &a->scantable, scantab);

    a->inv_qscale= le2me_32(((uint32_t*)avctx->extradata)[0]);
    if(a->inv_qscale == 0){
        printf("illegal qscale 0\n");
        a->inv_qscale= 6;
    }

    for(i=0; i<64; i++){
        int index= scantab[i];
        a->intra_matrix[i]= 64*ff_mpeg1_default_intra_matrix[index] / a->inv_qscale;
    }

    p->qstride= a->mb_width;
    p->qscale_table= av_mallocz( p->qstride * a->mb_height);

    return 0;
}

static int encode_init(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;
    int i;
 
    common_init(avctx);
    
    if(avctx->global_quality == 0) avctx->global_quality= 4*FF_QUALITY_SCALE;

    a->inv_qscale= (32*FF_QUALITY_SCALE +  avctx->global_quality/2) / avctx->global_quality;
    
    avctx->extradata= av_mallocz(8);
    avctx->extradata_size=8;
    ((uint32_t*)avctx->extradata)[0]= le2me_32(a->inv_qscale);
    ((uint32_t*)avctx->extradata)[1]= le2me_32(ff_get_fourcc("ASUS"));
    
    for(i=0; i<64; i++){
        int q= 32*ff_mpeg1_default_intra_matrix[i];
        a->q_intra_matrix[i]= ((a->inv_qscale<<16) + q/2) / q;
    }

    return 0;
}

static int decode_end(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;

    av_freep(&a->bitstream_buffer);
    av_freep(&a->picture.qscale_table);
    a->bitstream_buffer_size=0;
    
    avcodec_default_free_buffers(avctx);

    return 0;
}

AVCodec asv1_decoder = {
    "asv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ASV1,
    sizeof(ASV1Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};

AVCodec asv1_encoder = {
    "asv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ASV1,
    sizeof(ASV1Context),
    encode_init,
    encode_frame,
    //encode_end,
};
