/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "mpegaudio.h"


static int mp3_header_compress(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    uint32_t header, extraheader;
    int mode_extension, header_size;

    if(avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL){
        av_log(avctx, AV_LOG_ERROR, "not standards compliant\n");
        return -1;
    }

    header = AV_RB32(buf);
    mode_extension= (header>>4)&3;

    if(ff_mpa_check_header(header) < 0 || (header&0x60000) != 0x20000){
output_unchanged:
        *poutbuf= (uint8_t *) buf;
        *poutbuf_size= buf_size;

        av_log(avctx, AV_LOG_INFO, "cannot compress %08X\n", header);
        return 0;
    }

    if(avctx->extradata_size == 0){
        avctx->extradata_size=15;
        avctx->extradata= av_malloc(avctx->extradata_size);
        strcpy(avctx->extradata, "FFCMP3 0.0");
        memcpy(avctx->extradata+11, buf, 4);
    }
    if(avctx->extradata_size != 15){
        av_log(avctx, AV_LOG_ERROR, "Extradata invalid\n");
        return -1;
    }
    extraheader = AV_RB32(avctx->extradata+11);
    if((extraheader&MP3_MASK) != (header&MP3_MASK))
        goto output_unchanged;

    header_size= (header&0x10000) ? 4 : 6;

    *poutbuf_size= buf_size - header_size;
    *poutbuf= av_malloc(buf_size - header_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(*poutbuf, buf + header_size, buf_size - header_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if(avctx->channels==2){
        if((header & (3<<19)) != 3<<19){
            (*poutbuf)[1] &= 0x3F;
            (*poutbuf)[1] |= mode_extension<<6;
            FFSWAP(int, (*poutbuf)[1], (*poutbuf)[2]);
        }else{
            (*poutbuf)[1] &= 0x8F;
            (*poutbuf)[1] |= mode_extension<<4;
        }
    }

    return 1;
}

AVBitStreamFilter mp3_header_compress_bsf={
    "mp3comp",
    0,
    mp3_header_compress,
};
