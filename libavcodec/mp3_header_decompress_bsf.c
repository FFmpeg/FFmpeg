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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "mpegaudiodecheader.h"
#include "mpegaudiodata.h"


static int mp3_header_decompress(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){
    uint32_t header;
    int sample_rate= avctx->sample_rate;
    int sample_rate_index=0;
    int lsf, mpeg25, bitrate_index, frame_size;

    header = AV_RB32(buf);
    if(ff_mpa_check_header(header) >= 0){
        *poutbuf= (uint8_t *) buf;
        *poutbuf_size= buf_size;

        return 0;
    }

    if(avctx->extradata_size != 15 || strcmp(avctx->extradata, "FFCMP3 0.0")){
        av_log(avctx, AV_LOG_ERROR, "Extradata invalid %d\n", avctx->extradata_size);
        return -1;
    }

    header= AV_RB32(avctx->extradata+11) & MP3_MASK;

    lsf     = sample_rate < (24000+32000)/2;
    mpeg25  = sample_rate < (12000+16000)/2;
    sample_rate_index= (header>>10)&3;
    sample_rate= avpriv_mpa_freq_tab[sample_rate_index] >> (lsf + mpeg25); //in case sample rate is a little off

    for(bitrate_index=2; bitrate_index<30; bitrate_index++){
        frame_size = avpriv_mpa_bitrate_tab[lsf][2][bitrate_index>>1];
        frame_size = (frame_size * 144000) / (sample_rate << lsf) + (bitrate_index&1);
        if(frame_size == buf_size + 4)
            break;
        if(frame_size == buf_size + 6)
            break;
    }
    if(bitrate_index == 30){
        av_log(avctx, AV_LOG_ERROR, "Could not find bitrate_index.\n");
        return -1;
    }

    header |= (bitrate_index&1)<<9;
    header |= (bitrate_index>>1)<<12;
    header |= (frame_size == buf_size + 4)<<16; //FIXME actually set a correct crc instead of 0

    *poutbuf_size= frame_size;
    *poutbuf= av_malloc(frame_size + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(*poutbuf + frame_size - buf_size, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if(avctx->channels==2){
        uint8_t *p= *poutbuf + frame_size - buf_size;
        if(lsf){
            FFSWAP(int, p[1], p[2]);
            header |= (p[1] & 0xC0)>>2;
            p[1] &= 0x3F;
        }else{
            header |= p[1] & 0x30;
            p[1] &= 0xCF;
        }
    }

    AV_WB32(*poutbuf, header);

    return 1;
}

AVBitStreamFilter ff_mp3_header_decompress_bsf={
    "mp3decomp",
    0,
    mp3_header_decompress,
};
