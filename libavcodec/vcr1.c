/*
 * ATI VCR1 codec
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
 * @file vcr1.c
 * ati vcr1 codec.
 */
 
#include "avcodec.h"
#include "mpegvideo.h"

//#undef NDEBUG
//#include <assert.h>

typedef struct VCR1Context{
    AVCodecContext *avctx;
    AVFrame picture;
    int delta[16];
    int offset[4];
} VCR1Context;

static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    VCR1Context * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    uint8_t *bytestream= buf;
    int i, x, y;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    for(i=0; i<16; i++){
        a->delta[i]= *(bytestream++);
        bytestream++;
    }
    
    for(y=0; y<avctx->height; y++){
        int offset;
        uint8_t *luma= &a->picture.data[0][ y*a->picture.linesize[0] ];

        if((y&3) == 0){
            uint8_t *cb= &a->picture.data[1][ (y>>2)*a->picture.linesize[1] ];
            uint8_t *cr= &a->picture.data[2][ (y>>2)*a->picture.linesize[2] ];

            for(i=0; i<4; i++)
                a->offset[i]= *(bytestream++);

            offset= a->offset[0] - a->delta[ bytestream[2]&0xF ];
            for(x=0; x<avctx->width; x+=4){
                luma[0]=( offset += a->delta[ bytestream[2]&0xF ]);
                luma[1]=( offset += a->delta[ bytestream[2]>>4  ]);
                luma[2]=( offset += a->delta[ bytestream[0]&0xF ]);
                luma[3]=( offset += a->delta[ bytestream[0]>>4  ]);
                luma += 4;
                
                *(cb++) = bytestream[3];
                *(cr++) = bytestream[1];
                
                bytestream+= 4;
            }
        }else{
            offset= a->offset[y&3] - a->delta[ bytestream[2]&0xF ];

            for(x=0; x<avctx->width; x+=8){
                luma[0]=( offset += a->delta[ bytestream[2]&0xF ]);
                luma[1]=( offset += a->delta[ bytestream[2]>>4  ]);
                luma[2]=( offset += a->delta[ bytestream[3]&0xF ]);
                luma[3]=( offset += a->delta[ bytestream[3]>>4  ]);
                luma[4]=( offset += a->delta[ bytestream[0]&0xF ]);
                luma[5]=( offset += a->delta[ bytestream[0]>>4  ]);
                luma[6]=( offset += a->delta[ bytestream[1]&0xF ]);
                luma[7]=( offset += a->delta[ bytestream[1]>>4  ]);
                luma += 8;
                bytestream+= 4;
            }
        }
    }

    *picture= *(AVFrame*)&a->picture;
    *data_size = sizeof(AVPicture);

    emms_c();
    
    return buf_size;
}

#if 0
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    VCR1Context * const a = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int size;
    int mb_x, mb_y;

    *p = *pict;
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    emms_c();
    
    align_put_bits(&a->pb);
    while(get_bit_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);
    
    size= get_bit_count(&a->pb)/32;
    
    return size*4;
}
#endif

static void common_init(AVCodecContext *avctx){
    VCR1Context * const a = avctx->priv_data;

    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static int decode_init(AVCodecContext *avctx){
 
    common_init(avctx);
    
    avctx->pix_fmt= PIX_FMT_YUV410P;

    return 0;
}

#if 0
static int encode_init(AVCodecContext *avctx){
 
    common_init(avctx);
    
    return 0;
}
#endif

AVCodec vcr1_decoder = {
    "vcr1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VCR1,
    sizeof(VCR1Context),
    decode_init,
    NULL,
    NULL,
    decode_frame,
    CODEC_CAP_DR1,
};
#if 0
#ifdef CONFIG_ENCODERS

AVCodec vcr1_encoder = {
    "vcr1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VCR1,
    sizeof(VCR1Context),
    encode_init,
    encode_frame,
    //encode_end,
};

#endif //CONFIG_ENCODERS
#endif
