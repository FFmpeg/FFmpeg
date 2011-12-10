/*
 * ATI VCR1 codec
 * Copyright (c) 2003 Michael Niedermayer
 *
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
 * ati vcr1 codec.
 */

#include "avcodec.h"
#include "dsputil.h"

//#undef NDEBUG
//#include <assert.h>

/* Disable the encoder. */
#undef CONFIG_VCR1_ENCODER
#define CONFIG_VCR1_ENCODER 0

typedef struct VCR1Context{
    AVCodecContext *avctx;
    AVFrame picture;
    int delta[16];
    int offset[4];
} VCR1Context;

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    VCR1Context * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    const uint8_t *bytestream= buf;
    int i, x, y;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= AV_PICTURE_TYPE_I;
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

    return buf_size;
}

#if CONFIG_VCR1_ENCODER
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    VCR1Context * const a = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int size;

    *p = *pict;
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

    avpriv_align_put_bits(&a->pb);
    while(get_bit_count(&a->pb)&31)
        put_bits(&a->pb, 8, 0);

    size= get_bit_count(&a->pb)/32;

    return size*4;
}
#endif

static av_cold void common_init(AVCodecContext *avctx){
    VCR1Context * const a = avctx->priv_data;

    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static av_cold int decode_init(AVCodecContext *avctx){

    common_init(avctx);

    avctx->pix_fmt= PIX_FMT_YUV410P;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx){
    VCR1Context *s = avctx->priv_data;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

#if CONFIG_VCR1_ENCODER
static av_cold int encode_init(AVCodecContext *avctx){

    common_init(avctx);

    return 0;
}
#endif

AVCodec ff_vcr1_decoder = {
    .name           = "vcr1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VCR1,
    .priv_data_size = sizeof(VCR1Context),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("ATI VCR1"),
};

#if CONFIG_VCR1_ENCODER
AVCodec ff_vcr1_encoder = {
    .name           = "vcr1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VCR1,
    .priv_data_size = sizeof(VCR1Context),
    .init           = encode_init,
    .encode         = encode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("ATI VCR1"),
};
#endif
