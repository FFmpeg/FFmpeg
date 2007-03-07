/*
 * FLI/FLC Animation Video Decoder
 * Copyright (C) 2003, 2004 the ffmpeg project
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
 * This decoder outputs PAL8/RGB555/RGB565 and maybe one day RGB24
 * colorspace data, depending on the FLC. To use this decoder, be
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
#define FLI_DTA_BRUN  25
#define FLI_DTA_COPY  26
#define FLI_DTA_LC    27

#define FLI_TYPE_CODE     (0xAF11)
#define FLC_FLX_TYPE_CODE (0xAF12)
#define FLC_DTA_TYPE_CODE (0xAF44) /* Marks an "Extended FLC" comes from Dave's Targa Animator (DTA) */
#define FLC_MAGIC_CARPET_SYNTHETIC_TYPE_CODE (0xAF13)

#define CHECK_PIXEL_PTR(n) \
    if (pixel_ptr + n > pixel_limit) { \
        av_log (s->avctx, AV_LOG_INFO, "Problem: pixel_ptr >= pixel_limit (%d >= %d)\n", \
        pixel_ptr + n, pixel_limit); \
        return -1; \
    } \

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
    int depth;

    s->avctx = avctx;
    avctx->has_b_frames = 0;

    s->fli_type = AV_RL16(&fli_header[4]); /* Might be overridden if a Magic Carpet FLC */
    depth       = AV_RL16(&fli_header[12]);

    if (depth == 0) {
      depth = 8; /* Some FLC generators set depth to zero, when they mean 8Bpp. Fix up here */
    }

    if (s->avctx->extradata_size == 12) {
        /* special case for magic carpet FLIs */
        s->fli_type = FLC_MAGIC_CARPET_SYNTHETIC_TYPE_CODE;
    } else if (s->avctx->extradata_size != 128) {
        av_log(avctx, AV_LOG_ERROR, "Expected extradata of 12 or 128 bytes\n");
        return -1;
    }

    if ((s->fli_type == FLC_FLX_TYPE_CODE) && (depth == 16)) {
        depth = 15; /* Original Autodesk FLX's say the depth is 16Bpp when it is really 15Bpp */
    }

    switch (depth) {
        case 8  : avctx->pix_fmt = PIX_FMT_PAL8; break;
        case 15 : avctx->pix_fmt = PIX_FMT_RGB555; break;
        case 16 : avctx->pix_fmt = PIX_FMT_RGB565; break;
        case 24 : avctx->pix_fmt = PIX_FMT_BGR24; /* Supposedly BGR, but havent any files to test with */
                  av_log(avctx, AV_LOG_ERROR, "24Bpp FLC/FLX is unsupported due to no test files.\n");
                  return -1;
                  break;
        default :
                  av_log(avctx, AV_LOG_ERROR, "Unknown FLC/FLX depth of %d Bpp is unsupported.\n",depth);
                  return -1;
    }

    s->frame.data[0] = NULL;
    s->new_palette = 0;

    return 0;
}

static int flic_decode_frame_8BPP(AVCodecContext *avctx,
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
    int byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;
    int pixel_limit;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    pixels = s->frame.data[0];
    pixel_limit = s->avctx->height * s->frame.linesize[0];

    frame_size = AV_RL32(&buf[stream_ptr]);
    stream_ptr += 6;  /* skip the magic number */
    num_chunks = AV_RL16(&buf[stream_ptr]);
    stream_ptr += 10;  /* skip padding */

    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size > 0) && (num_chunks > 0)) {
        chunk_size = AV_RL32(&buf[stream_ptr]);
        stream_ptr += 4;
        chunk_type = AV_RL16(&buf[stream_ptr]);
        stream_ptr += 2;

        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            stream_ptr_after_color_chunk = stream_ptr + chunk_size - 6;

            /* check special case: If this file is from the Magic Carpet
             * game and uses 6-bit colors even though it reports 256-color
             * chunks in a 0xAF12-type file (fli_type is set to 0xAF13 during
             * initialization) */
            if ((chunk_type == FLI_256_COLOR) && (s->fli_type != FLC_MAGIC_CARPET_SYNTHETIC_TYPE_CODE))
                color_shift = 0;
            else
                color_shift = 2;
            /* set up the palette */
            color_packets = AV_RL16(&buf[stream_ptr]);
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
                    unsigned int entry;

                    /* wrap around, for good measure */
                    if ((unsigned)palette_ptr >= 256)
                        palette_ptr = 0;

                    r = buf[stream_ptr++] << color_shift;
                    g = buf[stream_ptr++] << color_shift;
                    b = buf[stream_ptr++] << color_shift;
                    entry = (r << 16) | (g << 8) | b;
                    if (s->palette[palette_ptr] != entry)
                        s->new_palette = 1;
                    s->palette[palette_ptr++] = entry;
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
            compressed_lines = AV_RL16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                line_packets = AV_RL16(&buf[stream_ptr]);
                stream_ptr += 2;
                if ((line_packets & 0xC000) == 0xC000) {
                    // line skip opcode
                    line_packets = -line_packets;
                    y_ptr += line_packets * s->frame.linesize[0];
                } else if ((line_packets & 0xC000) == 0x4000) {
                    av_log(avctx, AV_LOG_ERROR, "Undefined opcode (%x) in DELTA_FLI\n", line_packets);
                } else if ((line_packets & 0xC000) == 0x8000) {
                    // "last byte" opcode
                    pixels[y_ptr + s->frame.linesize[0] - 1] = line_packets & 0xff;
                } else {
                    compressed_lines--;
                    pixel_ptr = y_ptr;
                    pixel_countdown = s->avctx->width;
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        pixel_skip = buf[stream_ptr++];
                        pixel_ptr += pixel_skip;
                        pixel_countdown -= pixel_skip;
                        byte_run = (signed char)(buf[stream_ptr++]);
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_idx1 = buf[stream_ptr++];
                            palette_idx2 = buf[stream_ptr++];
                            CHECK_PIXEL_PTR(byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 2) {
                                pixels[pixel_ptr++] = palette_idx1;
                                pixels[pixel_ptr++] = palette_idx2;
                            }
                        } else {
                            CHECK_PIXEL_PTR(byte_run * 2);
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
            starting_line = AV_RL16(&buf[stream_ptr]);
            stream_ptr += 2;
            y_ptr = 0;
            y_ptr += starting_line * s->frame.linesize[0];

            compressed_lines = AV_RL16(&buf[stream_ptr]);
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
                        byte_run = (signed char)(buf[stream_ptr++]);
                        if (byte_run > 0) {
                            CHECK_PIXEL_PTR(byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                palette_idx1 = buf[stream_ptr++];
                                pixels[pixel_ptr++] = palette_idx1;
                            }
                        } else if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_idx1 = buf[stream_ptr++];
                            CHECK_PIXEL_PTR(byte_run);
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
                    byte_run = (signed char)(buf[stream_ptr++]);
                    if (byte_run > 0) {
                        palette_idx1 = buf[stream_ptr++];
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
                        }
                    } else {  /* copy bytes if byte_run < 0 */
                        byte_run = -byte_run;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            palette_idx1 = buf[stream_ptr++];
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
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
    memcpy(s->frame.data[1], s->palette, AVPALETTE_SIZE);
    if (s->new_palette) {
        s->frame.palette_has_changed = 1;
        s->new_palette = 0;
    }

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static int flic_decode_frame_15_16BPP(AVCodecContext *avctx,
                                      void *data, int *data_size,
                                      uint8_t *buf, int buf_size)
{
    /* Note, the only difference between the 15Bpp and 16Bpp */
    /* Format is the pixel format, the packets are processed the same. */
    FlicDecodeContext *s = (FlicDecodeContext *)avctx->priv_data;

    int stream_ptr = 0;
    int pixel_ptr;
    unsigned char palette_idx1;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j;

    int lines;
    int compressed_lines;
    signed short line_packets;
    int y_ptr;
    int byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;
    int pixel;
    int pixel_limit;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    pixels = s->frame.data[0];
    pixel_limit = s->avctx->height * s->frame.linesize[0];

    frame_size = AV_RL32(&buf[stream_ptr]);
    stream_ptr += 6;  /* skip the magic number */
    num_chunks = AV_RL16(&buf[stream_ptr]);
    stream_ptr += 10;  /* skip padding */

    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size > 0) && (num_chunks > 0)) {
        chunk_size = AV_RL32(&buf[stream_ptr]);
        stream_ptr += 4;
        chunk_type = AV_RL16(&buf[stream_ptr]);
        stream_ptr += 2;

        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            /* For some reason, it seems that non-paletised flics do include one of these */
            /* chunks in their first frame.  Why i do not know, it seems rather extraneous */
/*            av_log(avctx, AV_LOG_ERROR, "Unexpected Palette chunk %d in non-paletised FLC\n",chunk_type);*/
            stream_ptr = stream_ptr + chunk_size - 6;
            break;

        case FLI_DELTA:
        case FLI_DTA_LC:
            y_ptr = 0;
            compressed_lines = AV_RL16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                line_packets = AV_RL16(&buf[stream_ptr]);
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
                        pixel_ptr += (pixel_skip*2); /* Pixel is 2 bytes wide */
                        pixel_countdown -= pixel_skip;
                        byte_run = (signed char)(buf[stream_ptr++]);
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            pixel    = AV_RL16(&buf[stream_ptr]);
                            stream_ptr += 2;
                            CHECK_PIXEL_PTR(byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 2) {
                                *((signed short*)(&pixels[pixel_ptr])) = pixel;
                                pixel_ptr += 2;
                            }
                        } else {
                            CHECK_PIXEL_PTR(byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                *((signed short*)(&pixels[pixel_ptr])) = AV_RL16(&buf[stream_ptr]);
                                stream_ptr += 2;
                                pixel_ptr += 2;
                            }
                        }
                    }

                    y_ptr += s->frame.linesize[0];
                }
            }
            break;

        case FLI_LC:
            av_log(avctx, AV_LOG_ERROR, "Unexpected FLI_LC chunk in non-paletised FLC\n");
            stream_ptr = stream_ptr + chunk_size - 6;
            break;

        case FLI_BLACK:
            /* set the whole frame to 0x0000 which is black in both 15Bpp and 16Bpp modes. */
            memset(pixels, 0x0000,
                   s->frame.linesize[0] * s->avctx->height);
            break;

        case FLI_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                stream_ptr++;
                pixel_countdown = (s->avctx->width * 2);

                while (pixel_countdown > 0) {
                    byte_run = (signed char)(buf[stream_ptr++]);
                    if (byte_run > 0) {
                        palette_idx1 = buf[stream_ptr++];
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) (linea%d)\n",
                                       pixel_countdown, lines);
                        }
                    } else {  /* copy bytes if byte_run < 0 */
                        byte_run = -byte_run;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            palette_idx1 = buf[stream_ptr++];
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
                        }
                    }
                }

                /* Now FLX is strange, in that it is "byte" as opposed to "pixel" run length compressed.
                 * This doesnt give us any good oportunity to perform word endian conversion
                 * during decompression. So if its requried (ie, this isnt a LE target, we do
                 * a second pass over the line here, swapping the bytes.
                 */
                pixel = 0xFF00;
                if (0xFF00 != AV_RL16(&pixel)) /* Check if its not an LE Target */
                {
                  pixel_ptr = y_ptr;
                  pixel_countdown = s->avctx->width;
                  while (pixel_countdown > 0) {
                    *((signed short*)(&pixels[pixel_ptr])) = AV_RL16(&buf[pixel_ptr]);
                    pixel_ptr += 2;
                  }
                }
                y_ptr += s->frame.linesize[0];
            }
            break;

        case FLI_DTA_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                stream_ptr++;
                pixel_countdown = s->avctx->width; /* Width is in pixels, not bytes */

                while (pixel_countdown > 0) {
                    byte_run = (signed char)(buf[stream_ptr++]);
                    if (byte_run > 0) {
                        pixel    = AV_RL16(&buf[stream_ptr]);
                        stream_ptr += 2;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            *((signed short*)(&pixels[pixel_ptr])) = pixel;
                            pixel_ptr += 2;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    } else {  /* copy pixels if byte_run < 0 */
                        byte_run = -byte_run;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            *((signed short*)(&pixels[pixel_ptr])) = AV_RL16(&buf[stream_ptr]);
                            stream_ptr += 2;
                            pixel_ptr  += 2;
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
        case FLI_DTA_COPY:
            /* copy the chunk (uncompressed frame) */
            if (chunk_size - 6 > (unsigned int)(s->avctx->width * s->avctx->height)*2) {
                av_log(avctx, AV_LOG_ERROR, "In chunk FLI_COPY : source data (%d bytes) " \
                       "bigger than image, skipping chunk\n", chunk_size - 6);
                stream_ptr += chunk_size - 6;
            } else {

                for (y_ptr = 0; y_ptr < s->frame.linesize[0] * s->avctx->height;
                     y_ptr += s->frame.linesize[0]) {

                    pixel_countdown = s->avctx->width;
                    pixel_ptr = 0;
                    while (pixel_countdown > 0) {
                      *((signed short*)(&pixels[y_ptr + pixel_ptr])) = AV_RL16(&buf[stream_ptr+pixel_ptr]);
                      pixel_ptr += 2;
                      pixel_countdown--;
                    }
                    stream_ptr += s->avctx->width*2;
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


    *data_size=sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static int flic_decode_frame_24BPP(AVCodecContext *avctx,
                                   void *data, int *data_size,
                                   uint8_t *buf, int buf_size)
{
  av_log(avctx, AV_LOG_ERROR, "24Bpp FLC Unsupported due to lack of test files.\n");
  return -1;
}

static int flic_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    if (avctx->pix_fmt == PIX_FMT_PAL8) {
      return flic_decode_frame_8BPP(avctx, data, data_size,
                                    buf, buf_size);
    }
    else if ((avctx->pix_fmt == PIX_FMT_RGB555) ||
             (avctx->pix_fmt == PIX_FMT_RGB565)) {
      return flic_decode_frame_15_16BPP(avctx, data, data_size,
                                        buf, buf_size);
    }
    else if (avctx->pix_fmt == PIX_FMT_BGR24) {
      return flic_decode_frame_24BPP(avctx, data, data_size,
                                     buf, buf_size);
    }

    /* Shouldnt get  here, ever as the pix_fmt is processed */
    /* in flic_decode_init and the above if should deal with */
    /* the finite set of possibilites allowable by here. */
    /* but in case we do, just error out. */
    av_log(avctx, AV_LOG_ERROR, "Unknown Format of FLC. My Science cant explain how this happened\n");
    return -1;
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
    NULL,
    NULL,
    NULL,
    NULL
};
