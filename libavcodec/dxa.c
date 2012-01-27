/*
 * Feeble Files/ScummVM DXA decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * DXA Video decoder
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

#include <zlib.h>

/*
 * Decoder context
 */
typedef struct DxaDecContext {
    AVCodecContext *avctx;
    AVFrame pic, prev;

    int dsize;
    uint8_t *decomp_buf;
    uint32_t pal[256];
} DxaDecContext;

static const int shift1[6] = { 0, 8, 8, 8, 4, 4 };
static const int shift2[6] = { 0, 0, 8, 4, 0, 4 };

static int decode_13(AVCodecContext *avctx, DxaDecContext *c, uint8_t* dst, uint8_t *src, uint8_t *ref)
{
    uint8_t *code, *data, *mv, *msk, *tmp, *tmp2;
    int i, j, k;
    int type, x, y, d, d2;
    int stride = c->pic.linesize[0];
    uint32_t mask;

    code = src  + 12;
    data = code + ((avctx->width * avctx->height) >> 4);
    mv   = data + AV_RB32(src + 0);
    msk  = mv   + AV_RB32(src + 4);

    for(j = 0; j < avctx->height; j += 4){
        for(i = 0; i < avctx->width; i += 4){
            tmp  = dst + i;
            tmp2 = ref + i;
            type = *code++;
            switch(type){
            case 4: // motion compensation
                x = (*mv) >> 4;    if(x & 8) x = 8 - x;
                y = (*mv++) & 0xF; if(y & 8) y = 8 - y;
                tmp2 += x + y*stride;
            case 0: // skip
            case 5: // skip in method 12
                for(y = 0; y < 4; y++){
                    memcpy(tmp, tmp2, 4);
                    tmp  += stride;
                    tmp2 += stride;
                }
                break;
            case 1:  // masked change
            case 10: // masked change with only half of pixels changed
            case 11: // cases 10-15 are for method 12 only
            case 12:
            case 13:
            case 14:
            case 15:
                if(type == 1){
                    mask = AV_RB16(msk);
                    msk += 2;
                }else{
                    type -= 10;
                    mask = ((msk[0] & 0xF0) << shift1[type]) | ((msk[0] & 0xF) << shift2[type]);
                    msk++;
                }
                for(y = 0; y < 4; y++){
                    for(x = 0; x < 4; x++){
                        tmp[x] = (mask & 0x8000) ? *data++ : tmp2[x];
                        mask <<= 1;
                    }
                    tmp  += stride;
                    tmp2 += stride;
                }
                break;
            case 2: // fill block
                for(y = 0; y < 4; y++){
                    memset(tmp, data[0], 4);
                    tmp += stride;
                }
                data++;
                break;
            case 3: // raw block
                for(y = 0; y < 4; y++){
                    memcpy(tmp, data, 4);
                    data += 4;
                    tmp  += stride;
                }
                break;
            case 8: // subblocks - method 13 only
                mask = *msk++;
                for(k = 0; k < 4; k++){
                    d  = ((k & 1) << 1) + ((k & 2) * stride);
                    d2 = ((k & 1) << 1) + ((k & 2) * stride);
                    tmp2 = ref + i + d2;
                    switch(mask & 0xC0){
                    case 0x80: // motion compensation
                        x = (*mv) >> 4;    if(x & 8) x = 8 - x;
                        y = (*mv++) & 0xF; if(y & 8) y = 8 - y;
                        tmp2 += x + y*stride;
                    case 0x00: // skip
                        tmp[d + 0         ] = tmp2[0];
                        tmp[d + 1         ] = tmp2[1];
                        tmp[d + 0 + stride] = tmp2[0 + stride];
                        tmp[d + 1 + stride] = tmp2[1 + stride];
                        break;
                    case 0x40: // fill
                        tmp[d + 0         ] = data[0];
                        tmp[d + 1         ] = data[0];
                        tmp[d + 0 + stride] = data[0];
                        tmp[d + 1 + stride] = data[0];
                        data++;
                        break;
                    case 0xC0: // raw
                        tmp[d + 0         ] = *data++;
                        tmp[d + 1         ] = *data++;
                        tmp[d + 0 + stride] = *data++;
                        tmp[d + 1 + stride] = *data++;
                        break;
                    }
                    mask <<= 2;
                }
                break;
            case 32: // vector quantization - 2 colors
                mask = AV_RB16(msk);
                msk += 2;
                for(y = 0; y < 4; y++){
                    for(x = 0; x < 4; x++){
                        tmp[x] = data[mask & 1];
                        mask >>= 1;
                    }
                    tmp  += stride;
                    tmp2 += stride;
                }
                data += 2;
                break;
            case 33: // vector quantization - 3 or 4 colors
            case 34:
                mask = AV_RB32(msk);
                msk += 4;
                for(y = 0; y < 4; y++){
                    for(x = 0; x < 4; x++){
                        tmp[x] = data[mask & 3];
                        mask >>= 2;
                    }
                    tmp  += stride;
                    tmp2 += stride;
                }
                data += type - 30;
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "Unknown opcode %d\n", type);
                return -1;
            }
        }
        dst += stride * 4;
        ref += stride * 4;
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    DxaDecContext * const c = avctx->priv_data;
    uint8_t *outptr, *srcptr, *tmpptr;
    unsigned long dsize;
    int i, j, compr;
    int stride;
    int orig_buf_size = buf_size;
    int pc = 0;

    /* make the palette available on the way out */
    if(buf[0]=='C' && buf[1]=='M' && buf[2]=='A' && buf[3]=='P'){
        int r, g, b;

        buf += 4;
        for(i = 0; i < 256; i++){
            r = *buf++;
            g = *buf++;
            b = *buf++;
            c->pal[i] = (r << 16) | (g << 8) | b;
        }
        pc = 1;
        buf_size -= 768+4;
    }

    if(avctx->get_buffer(avctx, &c->pic) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    memcpy(c->pic.data[1], c->pal, AVPALETTE_SIZE);
    c->pic.palette_has_changed = pc;

    outptr = c->pic.data[0];
    srcptr = c->decomp_buf;
    tmpptr = c->prev.data[0];
    stride = c->pic.linesize[0];

    if(buf[0]=='N' && buf[1]=='U' && buf[2]=='L' && buf[3]=='L')
        compr = -1;
    else
        compr = buf[4];

    dsize = c->dsize;
    if((compr != 4 && compr != -1) && uncompress(c->decomp_buf, &dsize, buf + 9, buf_size - 9) != Z_OK){
        av_log(avctx, AV_LOG_ERROR, "Uncompress failed!\n");
        return -1;
    }
    switch(compr){
    case -1:
        c->pic.key_frame = 0;
        c->pic.pict_type = AV_PICTURE_TYPE_P;
        if(c->prev.data[0])
            memcpy(c->pic.data[0], c->prev.data[0], c->pic.linesize[0] * avctx->height);
        else{ // Should happen only when first frame is 'NULL'
            memset(c->pic.data[0], 0, c->pic.linesize[0] * avctx->height);
            c->pic.key_frame = 1;
            c->pic.pict_type = AV_PICTURE_TYPE_I;
        }
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        c->pic.key_frame = !(compr & 1);
        c->pic.pict_type = (compr & 1) ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
        for(j = 0; j < avctx->height; j++){
            if(compr & 1){
                for(i = 0; i < avctx->width; i++)
                    outptr[i] = srcptr[i] ^ tmpptr[i];
                tmpptr += stride;
            }else
                memcpy(outptr, srcptr, avctx->width);
            outptr += stride;
            srcptr += avctx->width;
        }
        break;
    case 12: // ScummVM coding
    case 13:
        c->pic.key_frame = 0;
        c->pic.pict_type = AV_PICTURE_TYPE_P;
        decode_13(avctx, c, c->pic.data[0], srcptr, c->prev.data[0]);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown/unsupported compression type %d\n", buf[4]);
        return -1;
    }

    FFSWAP(AVFrame, c->pic, c->prev);
    if(c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->prev;

    /* always report that the buffer was completely consumed */
    return orig_buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    DxaDecContext * const c = avctx->priv_data;

    c->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;

    c->dsize = avctx->width * avctx->height * 2;
    if((c->decomp_buf = av_malloc(c->dsize)) == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
        return -1;
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    DxaDecContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);
    if(c->prev.data[0])
        avctx->release_buffer(avctx, &c->prev);
    if(c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    return 0;
}

AVCodec ff_dxa_decoder = {
    .name           = "dxa",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_DXA,
    .priv_data_size = sizeof(DxaDecContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Feeble Files/ScummVM DXA"),
};
