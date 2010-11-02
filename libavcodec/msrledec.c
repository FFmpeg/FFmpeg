/*
 * Microsoft RLE decoder
 * Copyright (C) 2008 Konstantin Shishkov
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

/**
 * @file
 * MS RLE decoder based on decoder by Mike Melanson and my own for TSCC
 * For more information about the MS RLE format, visit:
 *   http://www.multimedia.cx/msrle.txt
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "msrledec.h"

#define FETCH_NEXT_STREAM_BYTE() \
    if (stream_ptr >= data_size) \
    { \
      av_log(avctx, AV_LOG_ERROR, " MS RLE: stream ptr just went out of bounds (1)\n"); \
      return -1; \
    } \
    stream_byte = data[stream_ptr++];

static int msrle_decode_pal4(AVCodecContext *avctx, AVPicture *pic,
                              const uint8_t *data, int data_size)
{
    int stream_ptr = 0;
    unsigned char rle_code;
    unsigned char extra_byte, odd_pixel;
    unsigned char stream_byte;
    unsigned int pixel_ptr = 0;
    int row_dec = pic->linesize[0];
    int row_ptr = (avctx->height - 1) * row_dec;
    int frame_size = row_dec * avctx->height;
    int i;

    while (row_ptr >= 0) {
        FETCH_NEXT_STREAM_BYTE();
        rle_code = stream_byte;
        if (rle_code == 0) {
            /* fetch the next byte to see how to handle escape code */
            FETCH_NEXT_STREAM_BYTE();
            if (stream_byte == 0) {
                /* line is done, goto the next one */
                row_ptr -= row_dec;
                pixel_ptr = 0;
            } else if (stream_byte == 1) {
                /* decode is done */
                return 0;
            } else if (stream_byte == 2) {
                /* reposition frame decode coordinates */
                FETCH_NEXT_STREAM_BYTE();
                pixel_ptr += stream_byte;
                FETCH_NEXT_STREAM_BYTE();
                row_ptr -= stream_byte * row_dec;
            } else {
                // copy pixels from encoded stream
                odd_pixel =  stream_byte & 1;
                rle_code = (stream_byte + 1) / 2;
                extra_byte = rle_code & 0x01;
                if (row_ptr + pixel_ptr + stream_byte > frame_size) {
                    av_log(avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (1)\n");
                    return -1;
                }

                for (i = 0; i < rle_code; i++) {
                    if (pixel_ptr >= avctx->width)
                        break;
                    FETCH_NEXT_STREAM_BYTE();
                    pic->data[0][row_ptr + pixel_ptr] = stream_byte >> 4;
                    pixel_ptr++;
                    if (i + 1 == rle_code && odd_pixel)
                        break;
                    if (pixel_ptr >= avctx->width)
                        break;
                    pic->data[0][row_ptr + pixel_ptr] = stream_byte & 0x0F;
                    pixel_ptr++;
                }

                // if the RLE code is odd, skip a byte in the stream
                if (extra_byte)
                    stream_ptr++;
            }
        } else {
            // decode a run of data
            if (row_ptr + pixel_ptr + stream_byte > frame_size) {
                av_log(avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (1)\n");
                return -1;
            }
            FETCH_NEXT_STREAM_BYTE();
            for (i = 0; i < rle_code; i++) {
                if (pixel_ptr >= avctx->width)
                    break;
                if ((i & 1) == 0)
                    pic->data[0][row_ptr + pixel_ptr] = stream_byte >> 4;
                else
                    pic->data[0][row_ptr + pixel_ptr] = stream_byte & 0x0F;
                pixel_ptr++;
            }
        }
    }

    /* one last sanity check on the way out */
    if (stream_ptr < data_size) {
        av_log(avctx, AV_LOG_ERROR, " MS RLE: ended frame decode with bytes left over (%d < %d)\n",
            stream_ptr, data_size);
        return -1;
    }

    return 0;
}


static int msrle_decode_8_16_24_32(AVCodecContext *avctx, AVPicture *pic, int depth,
                                    const uint8_t *data, int srcsize)
{
    uint8_t *output, *output_end;
    const uint8_t* src = data;
    int p1, p2, line=avctx->height - 1, pos=0, i;
    uint16_t av_uninit(pix16);
    uint32_t av_uninit(pix32);
    unsigned int width= FFABS(pic->linesize[0]) / (depth >> 3);

    output = pic->data[0] + (avctx->height - 1) * pic->linesize[0];
    output_end = pic->data[0] + (avctx->height) * pic->linesize[0];
    while(src < data + srcsize) {
        p1 = *src++;
        if(p1 == 0) { //Escape code
            p2 = *src++;
            if(p2 == 0) { //End-of-line
                output = pic->data[0] + (--line) * pic->linesize[0];
                if (line < 0 && !(src+1 < data + srcsize && AV_RB16(src) == 1)) {
                    av_log(avctx, AV_LOG_ERROR, "Next line is beyond picture bounds\n");
                    return -1;
                }
                pos = 0;
                continue;
            } else if(p2 == 1) { //End-of-picture
                return 0;
            } else if(p2 == 2) { //Skip
                p1 = *src++;
                p2 = *src++;
                line -= p2;
                pos += p1;
                if (line < 0 || pos >= width){
                    av_log(avctx, AV_LOG_ERROR, "Skip beyond picture bounds\n");
                    return -1;
                }
                output = pic->data[0] + line * pic->linesize[0] + pos * (depth >> 3);
                continue;
            }
            // Copy data
            if ((pic->linesize[0] > 0 && output + p2 * (depth >> 3) > output_end)
              ||(pic->linesize[0] < 0 && output + p2 * (depth >> 3) < output_end)) {
                src += p2 * (depth >> 3);
                continue;
            }
            if ((depth == 8) || (depth == 24)) {
                for(i = 0; i < p2 * (depth >> 3); i++) {
                    *output++ = *src++;
                }
                // RLE8 copy is actually padded - and runs are not!
                if(depth == 8 && (p2 & 1)) {
                    src++;
                }
            } else if (depth == 16) {
                for(i = 0; i < p2; i++) {
                    pix16 = AV_RL16(src);
                    src += 2;
                    *(uint16_t*)output = pix16;
                    output += 2;
                }
            } else if (depth == 32) {
                for(i = 0; i < p2; i++) {
                    pix32 = AV_RL32(src);
                    src += 4;
                    *(uint32_t*)output = pix32;
                    output += 4;
                }
            }
            pos += p2;
        } else { //run of pixels
            uint8_t pix[3]; //original pixel
            switch(depth){
            case  8: pix[0] = *src++;
                     break;
            case 16: pix16 = AV_RL16(src);
                     src += 2;
                     break;
            case 24: pix[0] = *src++;
                     pix[1] = *src++;
                     pix[2] = *src++;
                     break;
            case 32: pix32 = AV_RL32(src);
                     src += 4;
                     break;
            }
            if ((pic->linesize[0] > 0 && output + p1 * (depth >> 3) > output_end)
              ||(pic->linesize[0] < 0 && output + p1 * (depth >> 3) < output_end))
                continue;
            for(i = 0; i < p1; i++) {
                switch(depth){
                case  8: *output++ = pix[0];
                         break;
                case 16: *(uint16_t*)output = pix16;
                         output += 2;
                         break;
                case 24: *output++ = pix[0];
                         *output++ = pix[1];
                         *output++ = pix[2];
                         break;
                case 32: *(uint32_t*)output = pix32;
                         output += 4;
                         break;
                }
            }
            pos += p1;
        }
    }

    av_log(avctx, AV_LOG_WARNING, "MS RLE warning: no end-of-picture code\n");
    return 0;
}


int ff_msrle_decode(AVCodecContext *avctx, AVPicture *pic, int depth,
                    const uint8_t* data, int data_size)
{
    switch(depth){
    case  4:
        return msrle_decode_pal4(avctx, pic, data, data_size);
    case  8:
    case 16:
    case 24:
    case 32:
        return msrle_decode_8_16_24_32(avctx, pic, depth, data, data_size);
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown depth %d\n", depth);
        return -1;
    }
}

