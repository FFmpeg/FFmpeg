/*
 * FLI/FLC Animation Video Decoder
 * Copyright (C) 2003, 2004 the ffmpeg project
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
 * @file flic.c
 * Autodesk Animator FLI/FLC Video Decoder
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .fli/.flc file format and all of its many
 * variations, visit:
 *   http://www.compuphase.com/flic.htm
 *
 * This decoder outputs PAL8 colorspace data. To use this decoder, be
 * sure that your demuxer sends the FLI file header to the decoder via
 * the extradata chunk in AVCodecContext. The chunk should be 128 bytes
 * large. The only exception is for FLI files from the game "Magic Carpet",
 * in which the header is only 12 bytes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "bswap.h"

#define FLI_256_COLOR 4
#define FLI_DELTA     7
#define FLI_COLOR     11
#define FLI_LC        12
#define FLI_BLACK     13
#define FLI_BRUN      15
#define FLI_COPY      16
#define FLI_MINI      18

typedef struct FlicDecodeContext {
    AVCodecContext *avctx;
    AVFrame frame;

    unsigned int palette[256];
    int new_palette;
    int fli_type;  /* either 0xAF11 or 0xAF12, affects palette resolution */
} FlicDecodeContext;

static int flic_decode_init(AVCodecContext *avctx)
{
    FlicDecodeContext *s = (FlicDecodeContext *)avctx->priv_data;
    unsigned char *fli_header = (unsigned char *)avctx->extradata;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;

    if (s->avctx->extradata_size == 12) {
        /* special case for magic carpet FLIs */
        s->fli_type = 0xAF13;
    } else if (s->avctx->extradata_size == 128) {
        s->fli_type = LE_16(&fli_header[4]);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Expected extradata of 12 or 128 bytes\n");
        return -1;
    }

    s->frame.data[0] = NULL;
    s->new_palette = 0;

    return 0;
}

static int flic_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    FlicDecodeContext *s = (FlicDecodeContext *)avctx->priv_data;

    int stream_ptr = 0;
    int stream_ptr_after_color_chunk;
    int pixel_ptr;
    int palette_ptr;
    unsigned char palette_idx1;
    unsigned char palette_idx2;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j;

    int color_packets;
    int color_changes;
    int color_shift;
    unsigned char r, g, b;

    int lines;
    int compressed_lines;
    int starting_line;
    signed short line_packets;
    int y_ptr;
    signed char byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    pixels = s->frame.data[0];

    frame_size = LE_32(&buf[stream_ptr]);
    stream_ptr += 6;  /* skip the magic number */
    num_chunks = LE_16(&buf[stream_ptr]);
    stream_ptr += 10;  /* skip padding */

    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size > 0) && (num_chunks > 0)) {
        chunk_size = LE_32(&buf[stream_ptr]);
        stream_ptr += 4;
        chunk_type = LE_16(&buf[stream_ptr]);
        stream_ptr += 2;

        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            stream_ptr_after_color_chunk = stream_ptr + chunk_size - 6;
            s->new_palette = 1;

            /* check special case: If this file is from the Magic Carpet 
             * game and uses 6-bit colors even though it reports 256-color 
             * chunks in a 0xAF12-type file (fli_type is set to 0xAF13 during
             * initialization) */
            if ((chunk_type == FLI_256_COLOR) && (s->fli_type != 0xAF13))
                color_shift = 0;
            else
                color_shift = 2;
            /* set up the palette */
            color_packets = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            palette_ptr = 0;
            for (i = 0; i < color_packets; i++) {
                /* first byte is how many colors to skip */
                palette_ptr += buf[stream_ptr++];

                /* next byte indicates how many entries to change */
                color_changes = buf[stream_ptr++];

                /* if there are 0 color changes, there are actually 256 */
                if (color_changes == 0)
                    color_changes = 256;

                for (j = 0; j < color_changes; j++) {

                    /* wrap around, for good measure */
                    if (palette_ptr >= 256)
                        palette_ptr = 0;

                    r = buf[stream_ptr++] << color_shift;
                    g = buf[stream_ptr++] << color_shift;
                    b = buf[stream_ptr++] << color_shift;
                    s->palette[palette_ptr++] = (r << 16) | (g << 8) | b;
                }
            }

            /* color chunks sometimes have weird 16-bit alignment issues;
             * therefore, take the hardline approach and set the stream_ptr
             * to the value calculated w.r.t. the size specified by the color
             * chunk header */
            stream_ptr = stream_ptr_after_color_chunk;

            break;

        case FLI_DELTA:
            y_ptr = 0;
            compressed_lines = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                line_packets = LE_16(&buf[stream_ptr]);
                stream_ptr += 2;
                if (line_packets < 0) {
                    line_packets = -line_packets;
                    y_ptr += line_packets * s->frame.linesize[0];
                } else {
                    compressed_lines--;
                    pixel_ptr = y_ptr;
                    pixel_countdown = s->avctx->width;
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        pixel_skip = buf[stream_ptr++];
                        pixel_ptr += pixel_skip;
                        pixel_countdown -= pixel_skip;
                        byte_run = buf[stream_ptr++];
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_idx1 = buf[stream_ptr++];
                            palette_idx2 = buf[stream_ptr++];
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 2) {
                                pixels[pixel_ptr++] = palette_idx1;
                                pixels[pixel_ptr++] = palette_idx2;
                            }
                        } else {
                            for (j = 0; j < byte_run * 2; j++, pixel_countdown--) {
                                palette_idx1 = buf[stream_ptr++];
                                pixels[pixel_ptr++] = palette_idx1;
                            }
                        }
                    }

                    y_ptr += s->frame.linesize[0];
                }
            }
            break;

        case FLI_LC:
            /* line compressed */
            starting_line = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            y_ptr = 0;
            y_ptr += starting_line * s->frame.linesize[0];

            compressed_lines = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                pixel_ptr = y_ptr;
                pixel_countdown = s->avctx->width;
                line_packets = buf[stream_ptr++];
                if (line_packets > 0) {
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        pixel_skip = buf[stream_ptr++];
                        pixel_ptr += pixel_skip;
                        pixel_countdown -= pixel_skip;
                        byte_run = buf[stream_ptr++];
                        if (byte_run > 0) {
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                palette_idx1 = buf[stream_ptr++];
                                pixels[pixel_ptr++] = palette_idx1;
                            }
                        } else {
                            byte_run = -byte_run;
                            palette_idx1 = buf[stream_ptr++];
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                pixels[pixel_ptr++] = palette_idx1;
                            }
                        }
                    }
                }

                y_ptr += s->frame.linesize[0];
                compressed_lines--;
            }
            break;

        case FLI_BLACK:
            /* set the whole frame to color 0 (which is usually black) */
            memset(pixels, 0,
                s->frame.linesize[0] * s->avctx->height);
            break;

        case FLI_BRUN:
            /* Byte run compression: This chunk type only occurs in the first
             * FLI frame and it will update the entire frame. */
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                stream_ptr++;
                pixel_countdown = s->avctx->width;
                while (pixel_countdown > 0) {
                    byte_run = buf[stream_ptr++];
                    if (byte_run > 0) {
                        palette_idx1 = buf[stream_ptr++];
                        for (j = 0; j < byte_run; j++) {
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    } else {  /* copy bytes if byte_run < 0 */
                        byte_run = -byte_run;
                        for (j = 0; j < byte_run; j++) {
                            palette_idx1 = buf[stream_ptr++];
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    }
                }

                y_ptr += s->frame.linesize[0];
            }
            break;

        case FLI_COPY:
            /* copy the chunk (uncompressed frame) */
            if (chunk_size - 6 > s->avctx->width * s->avctx->height) {
                av_log(avctx, AV_LOG_ERROR, "In chunk FLI_COPY : source data (%d bytes) " \
                       "bigger than image, skipping chunk\n", chunk_size - 6);
                stream_ptr += chunk_size - 6;
            } else {
                for (y_ptr = 0; y_ptr < s->frame.linesize[0] * s->avctx->height;
                     y_ptr += s->frame.linesize[0]) {
                    memcpy(&pixels[y_ptr], &buf[stream_ptr],
                        s->avctx->width);
                    stream_ptr += s->avctx->width;
                }
            }
            break;

        case FLI_MINI:
            /* some sort of a thumbnail? disregard this chunk... */
            stream_ptr += chunk_size - 6;
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Unrecognized chunk type: %d\n", chunk_type);
            break;
        }

        frame_size -= chunk_size;
        num_chunks--;
    }

    /* by the end of the chunk, the stream ptr should equal the frame
     * size (minus 1, possibly); if it doesn't, issue a warning */
    if ((stream_ptr != buf_size) && (stream_ptr != buf_size - 1))
        av_log(avctx, AV_LOG_ERROR, "Processed FLI chunk where chunk size = %d " \
               "and final chunk ptr = %d\n", buf_size, stream_ptr);

    /* make the palette available on the way out */
//    if (s->new_palette) {
    if (1) {
        memcpy(s->frame.data[1], s->palette, AVPALETTE_SIZE);
        s->frame.palette_has_changed = 1;
        s->new_palette = 0;
    }

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static int flic_decode_end(AVCodecContext *avctx)
{
    FlicDecodeContext *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec flic_decoder = {
    "flic",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FLIC,
    sizeof(FlicDecodeContext),
    flic_decode_init,
    NULL,
    flic_decode_end,
    flic_decode_frame,
    CODEC_CAP_DR1,
    NULL
};
