/*
 * PSX MDEC codec
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
 *
 * based upon code from Sebastian Jedruszkiewicz <elf@frogger.rules.pl>
 */
 
/**
 * @file mdec.c
 * PSX MDEC codec.
 * This is very similar to intra only MPEG1.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#undef NDEBUG
//#include <assert.h>

typedef struct MDECContext{
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame picture;
    PutBitContext pb;
    GetBitContext gb;
    ScanTable scantable;
    int version;
    int qscale;
    int last_dc[3];
    int mb_width;
    int mb_height;
    int mb_x, mb_y;
    DCTELEM __align8 block[6][64];
    uint16_t __align8 intra_matrix[64];
    int __align8 q_intra_matrix[64];
    uint8_t *bitstream_buffer;
    int bitstream_buffer_size;
    int block_last_index[6];
} MDECContext;

//very similar to mpeg1
static inline int mdec_decode_block_intra(MDECContext *a, DCTELEM *block, int n)
{
    int level, diff, i, j, run;
    int component;
    RLTable *rl = &rl_mpeg1;
    uint8_t * const scantable= a->scantable.permutated;
    const uint16_t *quant_matrix= ff_mpeg1_default_intra_matrix;
    const int qscale= a->qscale;

    /* DC coef */
    if(a->version==2){
        block[0]= 2*get_sbits(&a->gb, 10) + 1024;
    }else{
        component = (n <= 3 ? 0 : n - 4 + 1);
        diff = decode_dc(&a->gb, component);
        if (diff >= 0xffff)
            return -1;
        a->last_dc[component]+= diff;
        block[0] = a->last_dc[component]<<3;
    }
    
    i = 0;
    {
        OPEN_READER(re, &a->gb);    
        /* now quantify & encode AC coefs */
        for(;;) {
            UPDATE_CACHE(re, &a->gb);
            GET_RL_VLC(level, run, re, &a->gb, rl->rl_vlc[0], TEX_VLC_BITS, 2);
            
            if(level == 127){
                break;
            } else if(level != 0) {
                i += run;
                j = scantable[i];
                level= (level*qscale*quant_matrix[j])>>3;
//                level= (level-1)|1;
                level = (level ^ SHOW_SBITS(re, &a->gb, 1)) - SHOW_SBITS(re, &a->gb, 1);
                LAST_SKIP_BITS(re, &a->gb, 1);
            } else {
                /* escape */
                run = SHOW_UBITS(re, &a->gb, 6)+1; LAST_SKIP_BITS(re, &a->gb, 6);
                UPDATE_CACHE(re, &a->gb);
                level = SHOW_SBITS(re, &a->gb, 10); SKIP_BITS(re, &a->gb, 10);
                i += run;
                j = scantable[i];
                if(level<0){
                    level= -level;
                    level= (level*qscale*quant_matrix[j])>>3;
                    level= (level-1)|1;
                    level= -level;
                }else{
                    level= (level*qscale*quant_matrix[j])>>3;
                    level= (level-1)|1;
                }
            }
            if (i > 63){
                fprintf(stderr, "ac-tex damaged at %d %d\n", a->mb_x, a->mb_y);
                return -1;
            }

            block[j] = level;
        }
        CLOSE_READER(re, &a->gb);
    }
    a->block_last_index[n] = i;
    return 0;
}

static inline int decode_mb(MDECContext *a, DCTELEM block[6][64]){
    int i;
    const int block_index[6]= {5,4,0,1,2,3};

    a->dsp.clear_blocks(block[0]);
    
    for(i=0; i<6; i++){
        if( mdec_decode_block_intra(a, block[ block_index[i] ], block_index[i]) < 0) 
            return -1;
    }
    return 0;
}

static inline void idct_put(MDECContext *a, int mb_x, int mb_y){
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

static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    MDECContext * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int i;

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
    a->last_dc[0]=
    a->last_dc[1]=
    a->last_dc[2]= 0;

    a->bitstream_buffer= av_fast_realloc(a->bitstream_buffer, &a->bitstream_buffer_size, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    for(i=0; i<buf_size; i+=2){
        a->bitstream_buffer[i]  = buf[i+1];
        a->bitstream_buffer[i+1]= buf[i  ];
    }
    init_get_bits(&a->gb, a->bitstream_buffer, buf_size*8);
    
    /* skip over 4 preamble bytes in stream (typically 0xXX 0xXX 0x00 0x38) */
    skip_bits(&a->gb, 32);

    a->qscale=  get_bits(&a->gb, 16);
    a->version= get_bits(&a->gb, 16);
    
    printf("qscale:%d (0x%X), version:%d (0x%X)\n", a->qscale, a->qscale, a->version, a->version);
    
    for(a->mb_x=0; a->mb_x<a->mb_width; a->mb_x++){
        for(a->mb_y=0; a->mb_y<a->mb_height; a->mb_y++){
            if( decode_mb(a, a->block) <0)
                return -1;
             
            idct_put(a, a->mb_x, a->mb_y);
        }
    }

//    p->quality= (32 + a->inv_qscale/2)/a->inv_qscale;
//    memset(p->qscale_table, p->quality, p->qstride*a->mb_height);
    
    *picture= *(AVFrame*)&a->picture;
    *data_size = sizeof(AVPicture);

    emms_c();
    
    return (get_bits_count(&a->gb)+31)/32*4;
}

static void mdec_common_init(AVCodecContext *avctx){
    MDECContext * const a = avctx->priv_data;

    dsputil_init(&a->dsp, avctx);

    a->mb_width   = (avctx->width  + 15) / 16;
    a->mb_height  = (avctx->height + 15) / 16;

    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static int decode_init(AVCodecContext *avctx){
    MDECContext * const a = avctx->priv_data;
    AVFrame *p= (AVFrame*)&a->picture;
 
    mdec_common_init(avctx);
    init_vlcs();
    ff_init_scantable(a->dsp.idct_permutation, &a->scantable, ff_zigzag_direct);
/*
    for(i=0; i<64; i++){
        int index= ff_zigzag_direct[i];
        a->intra_matrix[i]= 64*ff_mpeg1_default_intra_matrix[index] / a->inv_qscale;
    }
*/
    p->qstride= a->mb_width;
    p->qscale_table= av_mallocz( p->qstride * a->mb_height);

    return 0;
}

static int decode_end(AVCodecContext *avctx){
    MDECContext * const a = avctx->priv_data;

    av_freep(&a->bitstream_buffer);
    av_freep(&a->picture.qscale_table);
    a->bitstream_buffer_size=0;
    
    avcodec_default_free_buffers(avctx);

    return 0;
}

AVCodec mdec_decoder = {
    "mdec",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MDEC,
    sizeof(MDECContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};

