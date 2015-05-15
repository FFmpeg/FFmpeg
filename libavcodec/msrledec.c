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

static int msrle_decode_pal4(AVCodecContext *avctx, AVPicture *pic,
                             GetByteContext *gb)
{
    unsigned char rle_code;
    unsigned char extra_byte, odd_pixel;
    unsigned char stream_byte;
    int pixel_ptr = 0;
    int line = avctx->height - 1;
    int i;

    while (line >= 0 && pixel_ptr <= avctx->width) {
        if (bytestream2_get_bytes_left(gb) <= 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "MS RLE: bytestream overrun, %dx%d left\n",
                   avctx->width - pixel_ptr, line);
            return AVERROR_INVALIDDATA;
        }
        rle_code = stream_byte = bytestream2_get_byteu(gb);
        if (rle_code == 0) {
            /* fetch the next byte to see how to handle escape code */
            stream_byte = bytestream2_get_byte(gb);
            if (stream_byte == 0) {
                /* line is done, goto the next one */
                line--;
                pixel_ptr = 0;
            } else if (stream_byte == 1) {
                /* decode is done */
                return 0;
            } else if (stream_byte == 2) {
                /* reposition frame decode coordinates */
                stream_byte = bytestream2_get_byte(gb);
                pixel_ptr += stream_byte;
                stream_byte = bytestream2_get_byte(gb);
                avpriv_request_sample(avctx, "Unused stream byte %X", stream_byte);
            } else {
                // copy pixels from encoded stream
                odd_pixel =  stream_byte & 1;
                rle_code = (stream_byte + 1) / 2;
                extra_byte = rle_code & 0x01;
                if (pixel_ptr + 2*rle_code - odd_pixel > avctx->width ||
                    bytestream2_get_bytes_left(gb) < rle_code) {
                    av_log(avctx, AV_LOG_ERROR,
                           "MS RLE: frame/stream ptr just went out of bounds (copy)\n");
                    return AVERROR_INVALIDDATA;
                }

                for (i = 0; i < rle_code; i++) {
                    if (pixel_ptr >= avctx->width)
                        break;
                    stream_byte = bytestream2_get_byteu(gb);
                    pic->data[0][line * pic->linesize[0] + pixel_ptr] = stream_byte >> 4;
                    pixel_ptr++;
                    if (i + 1 == rle_code && odd_pixel)
                        break;
                    if (pixel_ptr >= avctx->width)
                        break;
                    pic->data[0][line * pic->linesize[0] + pixel_ptr] = stream_byte & 0x0F;
                    pixel_ptr++;
                }

                // if the RLE code is odd, skip a byte in the stream
                if (extra_byte)
                    bytestream2_skip(gb, 1);
            }
        } else {
            // decode a run of data
            if (pixel_ptr + rle_code > avctx->width + 1) {
                av_log(avctx, AV_LOG_ERROR,
                       "MS RLE: frame ptr just went out of bounds (run) %d %d %d\n", pixel_ptr, rle_code, avctx->width);
                return AVERROR_INVALIDDATA;
            }
            stream_byte = bytestream2_get_byte(gb);
            for (i = 0; i < rle_code; i++) {
                if (pixel_ptr >= avctx->width)
                    break;
                if ((i & 1) == 0)
                    pic->data[0][line * pic->linesize[0] + pixel_ptr] = stream_byte >> 4;
                else
                    pic->data[0][line * pic->linesize[0] + pixel_ptr] = stream_byte & 0x0F;
                pixel_ptr++;
            }
        }
    }

    /* one last sanity check on the way out */
    if (bytestream2_get_bytes_left(gb)) {
        av_log(avctx, AV_LOG_ERROR,
               "MS RLE: ended frame decode with %d bytes left over\n",
               bytestream2_get_bytes_left(gb));
        return AVERROR_INVALIDDATA;
    }

    return 0;
}


static int msrle_decode_8_16_24_32(AVCodecContext *avctx, AVPicture *pic,
                                   int depth, GetByteContext *gb)
{
    uint8_t *output, *output_end;
    int p1, p2, line=avctx->height - 1, pos=0, i;
    uint16_t pix16;
    uint32_t pix32;
    unsigned int width= FFABS(pic->linesize[0]) / (depth >> 3);

    output     = pic->data[0] + (avctx->height - 1) * pic->linesize[0];
    output_end = output + FFABS(pic->linesize[0]);

    while (bytestream2_get_bytes_left(gb) > 0) {
        p1 = bytestream2_get_byteu(gb);
        if(p1 == 0) { //Escape code
            p2 = bytestream2_get_byte(gb);
            if(p2 == 0) { //End-of-line
                if (--line < 0) {
                    if (bytestream2_get_be16(gb) == 1) { // end-of-picture
                        return 0;
                    } else {
                        av_log(avctx, AV_LOG_ERROR,
                               "Next line is beyond picture bounds (%d bytes left)\n",
                               bytestream2_get_bytes_left(gb));
                        return AVERROR_INVALIDDATA;
                    }
                }
                output = pic->data[0] + line * pic->linesize[0];
                output_end = output + FFABS(pic->linesize[0]);
                pos = 0;
                continue;
            } else if(p2 == 1) { //End-of-picture
                return 0;
            } else if(p2 == 2) { //Skip
                p1 = bytestream2_get_byte(gb);
                p2 = bytestream2_get_byte(gb);
                line -= p2;
                pos += p1;
                if (line < 0 || pos >= width){
                    av_log(avctx, AV_LOG_ERROR, "Skip beyond picture bounds\n");
                    return -1;
                }
                output = pic->data[0] + line * pic->linesize[0] + pos * (depth >> 3);
                output_end = pic->data[0] + line * pic->linesize[0] + FFABS(pic->linesize[0]);
                continue;
            }
            // Copy data
            if (output + p2 * (depth >> 3) > output_end) {
                bytestream2_skip(gb, 2 * (depth >> 3));
                continue;
            } else if (bytestream2_get_bytes_left(gb) < p2 * (depth >> 3)) {
                av_log(avctx, AV_LOG_ERROR, "bytestream overrun\n");
                return AVERROR_INVALIDDATA;
            }

            if ((depth == 8) || (depth == 24)) {
                bytestream2_get_bufferu(gb, output, p2 * (depth >> 3));
                output += p2 * (depth >> 3);

                // RLE8 copy is actually padded - and runs are not!
                if(depth == 8 && (p2 & 1)) {
                    bytestream2_skip(gb, 1);
                }
            } else if (depth == 16) {
                for(i = 0; i < p2; i++) {
                    *(uint16_t*)output = bytestream2_get_le16u(gb);
                    output += 2;
                }
            } else if (depth == 32) {
                for(i = 0; i < p2; i++) {
                    *(uint32_t*)output = bytestream2_get_le32u(gb);
                    output += 4;
                }
            }
            pos += p2;
        } else { //run of pixels
            uint8_t pix[3]; //original pixel
            if (output + p1 * (depth >> 3) > output_end)
                continue;

            switch(depth){
            case  8:
                pix[0] = bytestream2_get_byte(gb);
                memset(output, pix[0], p1);
                output += p1;
                break;
            case 16:
                pix16  = bytestream2_get_le16(gb);
                for(i = 0; i < p1; i++) {
                        *(uint16_t*)output = pix16;
                        output += 2;
                }
                break;
            case 24:
                pix[0] = bytestream2_get_byte(gb);
                pix[1] = bytestream2_get_byte(gb);
                pix[2] = bytestream2_get_byte(gb);
                for(i = 0; i < p1; i++) {
                        *output++ = pix[0];
                        *output++ = pix[1];
                        *output++ = pix[2];
                }
                break;
            case 32:
                pix32  = bytestream2_get_le32(gb);
                for(i = 0; i < p1; i++) {
                        *(uint32_t*)output = pix32;
                        output += 4;
                }
                break;
            }
            pos += p1;
        }
    }

    av_log(avctx, AV_LOG_WARNING, "MS RLE warning: no end-of-picture code\n");
    return 0;
}


int ff_msrle_decode(AVCodecContext *avctx, AVPicture *pic,
                    int depth, GetByteContext *gb)
{
    switch(depth){
    case  4:
        return msrle_decode_pal4(avctx, pic, gb);
    case  8:
    case 16:
    case 24:
    case 32:
        return msrle_decode_8_16_24_32(avctx, pic, depth, gb);
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown depth %d\n", depth);
        return -1;
    }
}
