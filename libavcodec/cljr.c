/*
 * Cirrus Logic AccuPak (CLJR) codec
 * Copyright (c) 2003 Alex Beregszaszi
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
 * @file cljr.c
 * Cirrus Logic AccuPak codec.
 */
 
#include "avcodec.h"
#include "mpegvideo.h"

typedef struct CLJRContext{
    AVCodecContext *avctx;
    AVFrame picture;
    int delta[16];
    int offset[4];
    GetBitContext gb;
} CLJRContext;

static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    CLJRContext * const a = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&a->picture;
    int x, y;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= I_TYPE;
    p->key_frame= 1;

    init_get_bits(&a->gb, buf, buf_size);

    for(y=0; y<avctx->height; y++){
        uint8_t *luma= &a->picture.data[0][ y*a->picture.linesize[0] ];
        uint8_t *cb= &a->picture.data[1][ y*a->picture.linesize[1] ];
        uint8_t *cr= &a->picture.data[2][ y*a->picture.linesize[2] ];
        for(x=0; x<avctx->width; x+=4){
    	    luma[3] = get_bits(&a->gb, 5) << 3;
	    luma[2] = get_bits(&a->gb, 5) << 3;
	    luma[1] = get_bits(&a->gb, 5) << 3;
	    luma[0] = get_bits(&a->gb, 5) << 3;
	    luma+= 4;
	    *(cb++) = get_bits(&a->gb, 6) << 2;
	    *(cr++) = get_bits(&a->gb, 6) << 2;
        }
    }

    *picture= *(AVFrame*)&a->picture;
    *data_size = sizeof(AVPicture);

    emms_c();
    
    return buf_size;
}

#if 0
static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    CLJRContext * const a = avctx->priv_data;
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
    CLJRContext * const a = avctx->priv_data;

    avctx->coded_frame= (AVFrame*)&a->picture;
    a->avctx= avctx;
}

static int decode_init(AVCodecContext *avctx){

    common_init(avctx);
    
    avctx->pix_fmt= PIX_FMT_YUV411P;

    return 0;
}

#if 0
static int encode_init(AVCodecContext *avctx){

    common_init(avctx);
    
    return 0;
}
#endif

AVCodec cljr_decoder = {
    "cljr",
    CODEC_TYPE_VIDEO,
    CODEC_ID_CLJR,
    sizeof(CLJRContext),
    decode_init,
    NULL,
    NULL,
    decode_frame,
    CODEC_CAP_DR1,
};
#if 0
#ifdef CONFIG_ENCODERS

AVCodec cljr_encoder = {
    "cljr",
    CODEC_TYPE_VIDEO,
    CODEC_ID_cljr,
    sizeof(CLJRContext),
    encode_init,
    encode_frame,
    //encode_end,
};

#endif //CONFIG_ENCODERS
#endif
