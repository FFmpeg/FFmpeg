/*
 * ASUS V1/V2 codec
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
 * ASUS V1/V2 codec.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#undef NDEBUG
//#include <assert.h>

#define VLC_BITS 6
#define ASV2_LEVEL_VLC_BITS 10
 
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
    0x16,0x1E,0x17,0x1F,0x24,0x2C,0x25,0x2D,
    0x32,0x3A,0x33,0x3B,0x26,0x2E,0x27,0x2F,
    0x34,0x3C,0x35,0x3D,0x36,0x3E,0x37,0x3F,
};


static const uint8_t reverse[256]={
0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF,
};

static const uint8_t ccp_tab[17][2]={
    {0x2,2}, {0x7,5}, {0xB,5}, {0x3,5},
    {0xD,5}, {0x5,5}, {0x9,5}, {0x1,5},
    {0xE,5}, {0x6,5}, {0xA,5}, {0x2,5}, 
    {0xC,5}, {0x4,5}, {0x8,5}, {0x3,2},
    {0xF,5}, //EOB
};

static const uint8_t level_tab[7][2]={
    {3,4}, {3,3}, {3,2}, {0,3}, {2,2}, {2,3}, {2,4}
};

static const uint8_t dc_ccp_tab[8][2]={
    {0x1,2}, {0xD,4}, {0xF,4}, {0xC,4},
    {0x5,3}, {0xE,4}, {0x4,3}, {0x0,2},
};

static const uint8_t ac_ccp_tab[16][2]={
    {0x00,2}, {0x3B,6}, {0x0A,4}, {0x3A,6},
    {0x02,3}, {0x39,6}, {0x3C,6}, {0x38,6},
    {0x03,3}, {0x3D,6}, {0x08,4}, {0x1F,5},
    {0x09,4}, {0x0B,4}, {0x0D,4}, {0x0C,4},
};

static const uint8_t asv2_level_tab[63][2]={
    {0x3F,10},{0x2F,10},{0x37,10},{0x27,10},{0x3B,10},{0x2B,10},{0x33,10},{0x23,10},
    {0x3D,10},{0x2D,10},{0x35,10},{0x25,10},{0x39,10},{0x29,10},{0x31,10},{0x21,10},
    {0x1F, 8},{0x17, 8},{0x1B, 8},{0x13, 8},{0x1D, 8},{0x15, 8},{0x19, 8},{0x11, 8},
    {0x0F, 6},{0x0B, 6},{0x0D, 6},{0x09, 6},
    {0x07, 4},{0x05, 4},
    {0x03, 2},
    {0x00, 5},
    {0x02, 2},
    {0x04, 4},{0x06, 4},
    {0x08, 6},{0x0C, 6},{0x0A, 6},{0x0E, 6},
    {0x10, 8},{0x18, 8},{0x14, 8},{0x1C, 8},{0x12, 8},{0x1A, 8},{0x16, 8},{0x1E, 8},
    {0x20,10},{0x30,10},{0x28,10},{0x38,10},{0x24,10},{0x34,10},{0x2C,10},{0x3C,10},
    {0x22,10},{0x32,10},{0x2A,10},{0x3A,10},{0x26,10},{0x36,10},{0x2E,10},{0x3E,10},
};


static VLC ccp_vlc;
static VLC level_vlc;
static VLC dc_ccp_vlc;
static VLC ac_ccp_vlc;
static VLC asv2_level_vlc;

static void init_vlcs(ASV1Context *a){
    static int done = 0;

    if (!done) {
        done = 1;

        init_vlc(&ccp_vlc, VLC_BITS, 17, 
                 &ccp_tab[0][1], 2, 1,
                 &ccp_tab[0][0], 2, 1, 1);
        init_vlc(&dc_ccp_vlc, VLC_BITS, 8, 
                 &dc_ccp_tab[0][1], 2, 1,
                 &dc_ccp_tab[0][0], 2, 1, 1);
        init_vlc(&ac_ccp_vlc, VLC_BITS, 16, 
                 &ac_ccp_tab[0][1], 2, 1,
                 &ac_ccp_tab[0][0], 2, 1, 1);
        init_vlc(&level_vlc,  VLC_BITS, 7, 
                 &level_tab[0][1], 2, 1,
                 &level_tab[0][0], 2, 1, 1);
        init_vlc(&asv2_level_vlc, ASV2_LEVEL_VLC_BITS, 63, 
                 &asv2_level_tab[0][1], 2, 1,
                 &asv2_level_tab[0][0], 2, 1, 1);
    }
}

//FIXME write a reversed bitstream reader to avoid the double reverse
static inline int asv2_get_bits(GetBitContext *gb, int n){
    return reverse[ get_bits(gb, n) << (8-n) ];
}

static inline void asv2_put_bits(PutBitContext *pb, int n, int v){
    put_bits(pb, n, reverse[ v << (8-n) ]);
}

static inline int asv1_get_level(GetBitContext *gb){
    int code= get_vlc2(gb, level_vlc.table, VLC_BITS, 1);

    if(code==3) return get_sbits(gb, 8);
    else        return code - 3;
}

static inline int asv2_get_level(GetBitContext *gb){
    int code= get_vlc2(gb, asv2_level_vlc.table, ASV2_LEVEL_VLC_BITS, 1);

    if(code==31) return (int8_t)asv2_get_bits(gb, 8);
    else         return code - 31;
}

static inline void asv1_put_level(PutBitContext *pb, int level){
    unsigned int index= level + 3;

    if(index <= 6) put_bits(pb, level_tab[index][1], level_tab[index][0]);
    else{
        put_bits(pb, level_tab[3][1], level_tab[3][0]);
        put_bits(pb, 8, level&0xFF);
    }
}

static inline void asv2_put_level(PutBitContext *pb, int level){
    unsigned int index= level + 31;

    if(index <= 62) put_bits(pb, asv2_level_tab[index][1], asv2_level_tab[index][0]);
    else{
        put_bits(pb, asv2_level_tab[31][1], asv2_level_tab[31][0]);
        asv2_put_bits(pb, 8, level&0xFF);
    }
}

static inline int asv1_decode_block(ASV1Context *a, DCTELEM block[64]){
    int i;

    block[0]= 8*get_bits(&a->gb, 8);
    
    for(i=0; i<11; i++){
        const int ccp= get_vlc2(&a->gb, ccp_vlc.table, VLC_BITS, 1);

        if(ccp){
            if(ccp == 16) break;
            if(ccp < 0 || i>=10){
                av_log(a->avctx, AV_LOG_ERROR, "coded coeff pattern damaged\n");
                return -1;
            }

            if(ccp&8) block[a->scantable.permutated[4*i+0]]= (asv1_get_level(&a->gb) * a->intra_matrix[4*i+0])>>4;
            if(ccp&4) block[a->scantable.permutated[4*i+1]]= (asv1_get_level(&a->gb) * a->intra_matrix[4*i+1])>>4;
            if(ccp&2) block[a->scantable.permutated[4*i+2]]= (asv1_get_level(&a->gb) * a->intra_matrix[4*i+2])>>4;
            if(ccp&1) block[a->scantable.permutated[4*i+3]]= (asv1_get_level(&a->gb) * a->intra_matrix[4*i+3])>>4;
        }
    }

    return 0;
}

static inline int asv2_decode_block(ASV1Context *a, DCTELEM block[64]){
    int i, count, ccp;

    count= asv2_get_bits(&a->gb, 4);
    
    block[0]= 8*asv2_get_bits(&a->gb, 8);
    
    ccp= get_vlc2(&a->gb, dc_ccp_vlc.table, VLC_BITS, 1);
    if(ccp){
        if(ccp&4) block[a->scantable.permutated[1]]= (asv2_get_level(&a->gb) * a->intra_matrix[1])>>4;
        if(ccp&2) block[a->scantable.permutated[2]]= (asv2_get_level(&a->gb) * a->intra_matrix[2])>>4;
        if(ccp&1) block[a->scantable.permutated[3]]= (asv2_get_level(&a->gb) * a->intra_matrix[3])>>4;
    }

    for(i=1; i<count+1; i++){
        const int ccp= get_vlc2(&a->gb, ac_ccp_vlc.table, VLC_BITS, 1);

        if(ccp){
            if(ccp&8) block[a->scantable.permutated[4*i+0]]= (asv2_get_level(&a->gb) * a->intra_matrix[4*i+0])>>4;
            if(ccp&4) block[a->scantable.permutated[4*i+1]]= (asv2_get_level(&a->gb) * a->intra_matrix[4*i+1])>>4;
            if(ccp&2) block[a->scantable.permutated[4*i+2]]= (asv2_get_level(&a->gb) * a->intra_matrix[4*i+2])>>4;
            if(ccp&1) block[a->scantable.permutated[4*i+3]]= (asv2_get_level(&a->gb) * a->intra_matrix[4*i+3])>>4;
        }
    }
    
    return 0;
}

static inline void asv1_encode_block(ASV1Context *a, DCTELEM block[64]){
    int i;
    int nc_count=0;
    
    put_bits(&a->pb, 8, (block[0] + 32)>>6);
    block[0]= 0;
    
    for(i=0; i<10; i++){
        const int index= scantab[4*i];
        int ccp=0;

        if( (block[index + 0] = (block[index + 0]*a->q_intra_matrix[index + 0] + (1<<15))>>16) ) ccp |= 8;
        if( (block[index + 8] = (block[index + 8]*a->q_intra_matrix[index + 8] + (1<<15))>>16) ) ccp |= 4;
        if( (block[index + 1] = (block[index + 1]*a->q_intra_matrix[index + 1] + (1<<15))>>16) ) ccp |= 2;
        if( (block[index + 9] = (block[index + 9]*a->q_intra_matrix[index + 9] + (1<<15))>>16) ) ccp |= 1;

        if(ccp){
            for(;nc_count; nc_count--) 
                put_bits(&a->pb, ccp_tab[0][1], ccp_tab[0][0]);

            put_bits(&a->pb, ccp_tab[ccp][1], ccp_tab[ccp][0]);
            
            if(ccp&8) asv1_put_level(&a->pb, block[index + 0]);
            if(ccp&4) asv1_put_level(&a->pb, block[index + 8]);
            if(ccp&2) asv1_put_level(&a->pb, block[index + 1]);
            if(ccp&1) asv1_put_level(&a->pb, block[index + 9]);
        }else{
            nc_count++;
        }
    }
    put_bits(&a->pb, ccp_tab[16][1], ccp_tab[16][0]);
}

static inline void asv2_encode_block(ASV1Context *a, DCTELEM block[64]){
    int i;
    int count=0;
    
    for(count=63; count>3; count--){
        const int index= scantab[count];

        if( (block[index]*a->q_intra_matrix[index] + (1<<15))>>16 ) 
            break;
    }
    
    count >>= 2;

    asv2_put_bits(&a->pb, 4, count);
    asv2_put_bits(&a->pb, 8, (block[0] + 32)>>6);
    block[0]= 0;
    
    for(i=0; i<=count; i++){
        const int index= scantab[4*i];
        int ccp=0;

        if( (block[index + 0] = (block[index + 0]*a->q_intra_matrix[index + 0] + (1<<15))>>16) ) ccp |= 8;
        if( (block[index + 8] = (block[index + 8]*a->q_intra_matrix[index + 8] + (1<<15))>>16) ) ccp |= 4;
        if( (block[index + 1] = (block[index + 1]*a->q_intra_matrix[index + 1] + (1<<15))>>16) ) ccp |= 2;
        if( (block[index + 9] = (block[index + 9]*a->q_intra_matrix[index + 9] + (1<<15))>>16) ) ccp |= 1;

        if(i) put_bits(&a->pb, ac_ccp_tab[ccp][1], ac_ccp_tab[ccp][0]);
        else  put_bits(&a->pb, dc_ccp_tab[ccp][1], dc_ccp_tab[ccp][0]);

        if(ccp){
            if(ccp&8) asv2_put_level(&a->pb, block[index + 0]);
            if(ccp&4) asv2_put_level(&a->pb, block[index + 8]);
            if(ccp&2) asv2_put_level(&a->pb, block[index + 1]);
            if(ccp&1) asv2_put_level(&a->pb, block[index + 9]);
        }
    }
}

static inline int decode_mb(ASV1Context *a, DCTELEM block[6][64]){
    int i;

    a->dsp.clear_blocks(block[0]);
    
    if(a->avctx->codec_id == CODEC_ID_ASV1){
        for(i=0; i<6; i++){
            if( asv1_decode_block(a, block[i]) < 0) 
                return -1;
        }
    }else{
        for(i=0; i<6; i++){
            if( asv2_decode_block(a, block[i]) < 0) 
                return -1;
        }
    }
    return 0;
}

static inline void encode_mb(ASV1Context *a, DCTELEM block[6][64]){
    int i;

    if(a->avctx->codec_id == CODEC_ID_ASV1){
        for(i=0; i<6; i++)
            asv1_encode_block(a, block[i]);
    }else{
        for(i=0; i<6; i++)
            asv2_encode_block(a, block[i]);
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

    /* special case for last picture */
    if (buf_size == 0) {
        return 0;
    }

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    a->bitstream_buffer= av_fast_realloc(a->bitstream_buffer, &a->bitstream_buffer_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    
    if(avctx->codec_id == CODEC_ID_ASV1)
        a->dsp.bswap_buf((uint32_t*)a->bitstream_buffer, (uint32_t*)buf, buf_size/4);
    else{
        int i;
        for(i=0; i<buf_size; i++)
            a->bitstream_buffer[i]= reverse[ buf[i] ];
    }

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

    init_put_bits(&a->pb, buf, buf_size);
    
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
    while(put_bits_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);
    
    size= put_bits_count(&a->pb)/32;
    
    if(avctx->codec_id == CODEC_ID_ASV1)
        a->dsp.bswap_buf((uint32_t*)buf, (uint32_t*)buf, size);
    else{
        int i;
        for(i=0; i<4*size; i++)
            buf[i]= reverse[ buf[i] ];
    }
    
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
    const int scale= avctx->codec_id == CODEC_ID_ASV1 ? 1 : 2;
 
    common_init(avctx);
    init_vlcs(a);
    ff_init_scantable(a->dsp.idct_permutation, &a->scantable, scantab);

    a->inv_qscale= ((uint8_t*)avctx->extradata)[0];
    if(a->inv_qscale == 0){
        av_log(avctx, AV_LOG_ERROR, "illegal qscale 0\n");
        if(avctx->codec_id == CODEC_ID_ASV1)
            a->inv_qscale= 6;
        else
            a->inv_qscale= 10;
    }

    for(i=0; i<64; i++){
        int index= scantab[i];

        a->intra_matrix[i]= 64*scale*ff_mpeg1_default_intra_matrix[index] / a->inv_qscale;
    }

    p->qstride= a->mb_width;
    p->qscale_table= av_mallocz( p->qstride * a->mb_height);
    p->quality= (32*scale + a->inv_qscale/2)/a->inv_qscale;
    memset(p->qscale_table, p->quality, p->qstride*a->mb_height);

    return 0;
}

static int encode_init(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;
    int i;
    const int scale= avctx->codec_id == CODEC_ID_ASV1 ? 1 : 2;

    common_init(avctx);
    
    if(avctx->global_quality == 0) avctx->global_quality= 4*FF_QUALITY_SCALE;

    a->inv_qscale= (32*scale*FF_QUALITY_SCALE +  avctx->global_quality/2) / avctx->global_quality;
    
    avctx->extradata= av_mallocz(8);
    avctx->extradata_size=8;
    ((uint32_t*)avctx->extradata)[0]= le2me_32(a->inv_qscale);
    ((uint32_t*)avctx->extradata)[1]= le2me_32(ff_get_fourcc("ASUS"));
    
    for(i=0; i<64; i++){
        int q= 32*scale*ff_mpeg1_default_intra_matrix[i];
        a->q_intra_matrix[i]= ((a->inv_qscale<<16) + q/2) / q;
    }

    return 0;
}

static int decode_end(AVCodecContext *avctx){
    ASV1Context * const a = avctx->priv_data;

    av_freep(&a->bitstream_buffer);
    av_freep(&a->picture.qscale_table);
    a->bitstream_buffer_size=0;
    
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

AVCodec asv2_decoder = {
    "asv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ASV2,
    sizeof(ASV1Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};

#ifdef CONFIG_ENCODERS

AVCodec asv1_encoder = {
    "asv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ASV1,
    sizeof(ASV1Context),
    encode_init,
    encode_frame,
    //encode_end,
};

AVCodec asv2_encoder = {
    "asv2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ASV2,
    sizeof(ASV1Context),
    encode_init,
    encode_frame,
    //encode_end,
};

#endif //CONFIG_ENCODERS
