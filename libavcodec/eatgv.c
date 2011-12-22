/*
 * Electronic Arts TGV Video Decoder
 * Copyright (c) 2007-2008 Peter Ross
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 * Electronic Arts TGV Video Decoder
 * by Peter Ross (pross@xvid.org)
 *
 * Technical details here:
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_TGV
 */

#include "avcodec.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "libavutil/lzo.h"
#include "libavutil/imgutils.h"

#define EA_PREAMBLE_SIZE    8
#define kVGT_TAG MKTAG('k', 'V', 'G', 'T')

typedef struct TgvContext {
    AVCodecContext *avctx;
    AVFrame frame;
    AVFrame last_frame;
    int width,height;
    unsigned int palette[AVPALETTE_COUNT];

    int (*mv_codebook)[2];
    unsigned char (*block_codebook)[16];
    int num_mvs;           ///< current length of mv_codebook
    int num_blocks_packed; ///< current length of block_codebook
} TgvContext;

static av_cold int tgv_decode_init(AVCodecContext *avctx){
    TgvContext *s = avctx->priv_data;
    s->avctx = avctx;
    avctx->time_base = (AVRational){1, 15};
    avctx->pix_fmt = PIX_FMT_PAL8;
    return 0;
}

/**
 * Unpack buffer
 * @return 0 on success, -1 on critical buffer underflow
 */
static int unpack(const uint8_t *src, const uint8_t *src_end, unsigned char *dst, int width, int height) {
    unsigned char *dst_end = dst + width*height;
    int size, size1, size2, av_uninit(offset), run;
    unsigned char *dst_start = dst;

    if (src[0] & 0x01)
        src += 5;
    else
        src += 2;

    if (src+3>src_end)
        return -1;
    size = AV_RB24(src);
    src += 3;

    while(size>0 && src<src_end) {

        /* determine size1 and size2 */
        size1 = (src[0] & 3);
        if ( src[0] & 0x80 ) {  // 1
            if (src[0] & 0x40 ) {  // 11
                if ( src[0] & 0x20 ) {  // 111
                    if ( src[0] < 0xFC )  // !(111111)
                        size1 = (((src[0] & 31) + 1) << 2);
                    src++;
                    size2 = 0;
                } else {  // 110
                    offset = ((src[0] & 0x10) << 12) + AV_RB16(&src[1]) + 1;
                    size2 = ((src[0] & 0xC) << 6) + src[3] + 5;
                    src += 4;
                }
            } else {  // 10
                size1 = ( ( src[1] & 0xC0) >> 6 );
                offset = (AV_RB16(&src[1]) & 0x3FFF) + 1;
                size2 = (src[0] & 0x3F) + 4;
                src += 3;
            }
        } else {  // 0
            offset = ((src[0] & 0x60) << 3) + src[1] + 1;
            size2 = ((src[0] & 0x1C) >> 2) + 3;
            src += 2;
        }


        /* fetch strip from src */
        if (size1>src_end-src)
            break;

        if (size1>0) {
            size -= size1;
            run = FFMIN(size1, dst_end-dst);
            memcpy(dst, src, run);
            dst += run;
            src += run;
        }

        if (size2>0) {
            if (dst-dst_start<offset)
                return 0;
            size -= size2;
            run = FFMIN(size2, dst_end-dst);
            av_memcpy_backptr(dst, offset, run);
            dst += run;
        }
    }

    return 0;
}

/**
 * Decode inter-frame
 * @return 0 on success, -1 on critical buffer underflow
 */
static int tgv_decode_inter(TgvContext * s, const uint8_t *buf, const uint8_t *buf_end){
    unsigned char *frame0_end = s->last_frame.data[0] + s->avctx->width*s->last_frame.linesize[0];
    int num_mvs;
    int num_blocks_raw;
    int num_blocks_packed;
    int vector_bits;
    int i,j,x,y;
    GetBitContext gb;
    int mvbits;
    const unsigned char *blocks_raw;

    if(buf+12>buf_end)
        return -1;

    num_mvs           = AV_RL16(&buf[0]);
    num_blocks_raw    = AV_RL16(&buf[2]);
    num_blocks_packed = AV_RL16(&buf[4]);
    vector_bits       = AV_RL16(&buf[6]);
    buf += 12;

    /* allocate codebook buffers as necessary */
    if (num_mvs > s->num_mvs) {
        s->mv_codebook = av_realloc(s->mv_codebook, num_mvs*2*sizeof(int));
        s->num_mvs = num_mvs;
    }

    if (num_blocks_packed > s->num_blocks_packed) {
        s->block_codebook = av_realloc(s->block_codebook, num_blocks_packed*16*sizeof(unsigned char));
        s->num_blocks_packed = num_blocks_packed;
    }

    /* read motion vectors */
    mvbits = (num_mvs*2*10+31) & ~31;

    if (buf+(mvbits>>3)+16*num_blocks_raw+8*num_blocks_packed>buf_end)
        return -1;

    init_get_bits(&gb, buf, mvbits);
    for (i=0; i<num_mvs; i++) {
        s->mv_codebook[i][0] = get_sbits(&gb, 10);
        s->mv_codebook[i][1] = get_sbits(&gb, 10);
    }
    buf += mvbits>>3;

    /* note ptr to uncompressed blocks */
    blocks_raw = buf;
    buf += num_blocks_raw*16;

    /* read compressed blocks */
    init_get_bits(&gb, buf, (buf_end-buf)<<3);
    for (i=0; i<num_blocks_packed; i++) {
        int tmp[4];
        for(j=0; j<4; j++)
            tmp[j] = get_bits(&gb, 8);
        for(j=0; j<16; j++)
            s->block_codebook[i][15-j] = tmp[get_bits(&gb, 2)];
    }

    if (get_bits_left(&gb) < vector_bits *
        (s->avctx->height/4) * (s->avctx->width/4))
        return -1;

    /* read vectors and build frame */
    for(y=0; y<s->avctx->height/4; y++)
    for(x=0; x<s->avctx->width/4; x++) {
        unsigned int vector = get_bits(&gb, vector_bits);
        const unsigned char *src;
        int src_stride;

        if (vector < num_mvs) {
            src = s->last_frame.data[0] +
                  (y*4 + s->mv_codebook[vector][1])*s->last_frame.linesize[0] +
                   x*4 + s->mv_codebook[vector][0];
            src_stride = s->last_frame.linesize[0];
            if (src+3*src_stride+3>=frame0_end)
                continue;
        }else{
            int offset = vector - num_mvs;
            if (offset<num_blocks_raw)
                src = blocks_raw + 16*offset;
            else if (offset-num_blocks_raw<num_blocks_packed)
                src = s->block_codebook[offset-num_blocks_raw];
            else
                continue;
            src_stride = 4;
        }

        for(j=0; j<4; j++)
        for(i=0; i<4; i++)
            s->frame.data[0][ (y*4+j)*s->frame.linesize[0] + (x*4+i)  ] =
               src[j*src_stride + i];
    }

    return 0;
}

/** release AVFrame buffers if allocated */
static void cond_release_buffer(AVFrame *pic)
{
    if (pic->data[0]) {
        av_freep(&pic->data[0]);
        av_free(pic->data[1]);
    }
}

static int tgv_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TgvContext *s = avctx->priv_data;
    const uint8_t *buf_end = buf + buf_size;
    int chunk_type;

    chunk_type = AV_RL32(&buf[0]);
    buf += EA_PREAMBLE_SIZE;

    if (chunk_type==kVGT_TAG) {
        int pal_count, i;
        if(buf+12>buf_end) {
            av_log(avctx, AV_LOG_WARNING, "truncated header\n");
            return -1;
        }

        s->width  = AV_RL16(&buf[0]);
        s->height = AV_RL16(&buf[2]);
        if (s->avctx->width!=s->width || s->avctx->height!=s->height) {
            avcodec_set_dimensions(s->avctx, s->width, s->height);
            cond_release_buffer(&s->frame);
            cond_release_buffer(&s->last_frame);
        }

        pal_count = AV_RL16(&buf[6]);
        buf += 12;
        for(i=0; i<pal_count && i<AVPALETTE_COUNT && buf+2<buf_end; i++) {
            s->palette[i] = AV_RB24(buf);
            buf += 3;
        }
    }

    if (av_image_check_size(s->width, s->height, 0, avctx))
        return -1;

    /* shuffle */
    FFSWAP(AVFrame, s->frame, s->last_frame);
    if (!s->frame.data[0]) {
        s->frame.reference = 1;
        s->frame.buffer_hints = FF_BUFFER_HINTS_VALID;
        s->frame.linesize[0] = s->width;

        /* allocate additional 12 bytes to accommodate av_memcpy_backptr() OUTBUF_PADDED optimisation */
        s->frame.data[0] = av_malloc(s->width*s->height + 12);
        if (!s->frame.data[0])
            return AVERROR(ENOMEM);
        s->frame.data[1] = av_malloc(AVPALETTE_SIZE);
        if (!s->frame.data[1]) {
            av_freep(&s->frame.data[0]);
            return AVERROR(ENOMEM);
        }
    }
    memcpy(s->frame.data[1], s->palette, AVPALETTE_SIZE);

    if(chunk_type==kVGT_TAG) {
        s->frame.key_frame = 1;
        s->frame.pict_type = AV_PICTURE_TYPE_I;
        if (unpack(buf, buf_end, s->frame.data[0], s->avctx->width, s->avctx->height)<0) {
            av_log(avctx, AV_LOG_WARNING, "truncated intra frame\n");
            return -1;
        }
    }else{
        if (!s->last_frame.data[0]) {
            av_log(avctx, AV_LOG_WARNING, "inter frame without corresponding intra frame\n");
            return buf_size;
        }
        s->frame.key_frame = 0;
        s->frame.pict_type = AV_PICTURE_TYPE_P;
        if (tgv_decode_inter(s, buf, buf_end)<0) {
            av_log(avctx, AV_LOG_WARNING, "truncated inter frame\n");
            return -1;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static av_cold int tgv_decode_end(AVCodecContext *avctx)
{
    TgvContext *s = avctx->priv_data;
    cond_release_buffer(&s->frame);
    cond_release_buffer(&s->last_frame);
    av_free(s->mv_codebook);
    av_free(s->block_codebook);
    return 0;
}

AVCodec ff_eatgv_decoder = {
    .name           = "eatgv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TGV,
    .priv_data_size = sizeof(TgvContext),
    .init           = tgv_decode_init,
    .close          = tgv_decode_end,
    .decode         = tgv_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Electronic Arts TGV video"),
};
