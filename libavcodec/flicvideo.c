/*
 * FLI/FLC Animation Video Decoder
 * Copyright (C) 2003, 2004 The FFmpeg project
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
 * Autodesk Animator FLI/FLC Video Decoder
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .fli/.flc file format and all of its many
 * variations, visit:
 *   http://www.compuphase.com/flic.htm
 *
 * This decoder outputs PAL8/RGB555/RGB565/BGR24. To use this decoder, be
 * sure that your demuxer sends the FLI file header to the decoder via
 * the extradata chunk in AVCodecContext. The chunk should be 128 bytes
 * large. The only exception is for FLI files from the game "Magic Carpet",
 * in which the header is only 12 bytes.
 */

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "mathops.h"

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
        av_log (s->avctx, AV_LOG_ERROR, "Invalid pixel_ptr = %d > pixel_limit = %d\n", \
        pixel_ptr + n, pixel_limit); \
        return AVERROR_INVALIDDATA; \
    } \

typedef struct FlicDecodeContext {
    AVCodecContext *avctx;
    AVFrame *frame;

    unsigned int palette[256];
    int new_palette;
    int fli_type;  /* either 0xAF11 or 0xAF12, affects palette resolution */
} FlicDecodeContext;

static av_cold int flic_decode_init(AVCodecContext *avctx)
{
    FlicDecodeContext *s = avctx->priv_data;
    unsigned char *fli_header = (unsigned char *)avctx->extradata;
    int depth;

    if (avctx->extradata_size != 0 &&
        avctx->extradata_size != 12 &&
        avctx->extradata_size != 128 &&
        avctx->extradata_size != 256 &&
        avctx->extradata_size != 904 &&
        avctx->extradata_size != 1024) {
        av_log(avctx, AV_LOG_ERROR, "Unexpected extradata size %d\n", avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    s->avctx = avctx;

    if (s->avctx->extradata_size == 12) {
        /* special case for magic carpet FLIs */
        s->fli_type = FLC_MAGIC_CARPET_SYNTHETIC_TYPE_CODE;
        depth = 8;
    } else if (avctx->extradata_size == 1024) {
        uint8_t *ptr = avctx->extradata;
        int i;

        for (i = 0; i < 256; i++) {
            s->palette[i] = AV_RL32(ptr);
            ptr += 4;
        }
        depth = 8;
        /* FLI in MOV, see e.g. FFmpeg trac issue #626 */
    } else if (avctx->extradata_size == 0 ||
               avctx->extradata_size == 256 ||
        /* see FFmpeg ticket #1234 */
               avctx->extradata_size == 904) {
        s->fli_type = FLI_TYPE_CODE;
        depth = 8;
    } else {
        s->fli_type = AV_RL16(&fli_header[4]);
        depth = AV_RL16(&fli_header[12]);
    }

    if (depth == 0) {
        depth = 8; /* Some FLC generators set depth to zero, when they mean 8Bpp. Fix up here */
    }

    if ((s->fli_type == FLC_FLX_TYPE_CODE) && (depth == 16)) {
        depth = 15; /* Original Autodesk FLX's say the depth is 16Bpp when it is really 15Bpp */
    }

    switch (depth) {
        case 8  : avctx->pix_fmt = AV_PIX_FMT_PAL8; break;
        case 15 : avctx->pix_fmt = AV_PIX_FMT_RGB555; break;
        case 16 : avctx->pix_fmt = AV_PIX_FMT_RGB565; break;
        case 24 : avctx->pix_fmt = AV_PIX_FMT_BGR24; break;
        default :
                  av_log(avctx, AV_LOG_ERROR, "Unknown FLC/FLX depth of %d Bpp is unsupported.\n",depth);
                  return AVERROR_INVALIDDATA;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->new_palette = 0;

    return 0;
}

static int flic_decode_frame_8BPP(AVCodecContext *avctx,
                                  AVFrame *rframe, int *got_frame,
                                  const uint8_t *buf, int buf_size)
{
    FlicDecodeContext *s = avctx->priv_data;

    GetByteContext g2;
    int pixel_ptr;
    int palette_ptr;
    unsigned char palette_idx1;
    unsigned char palette_idx2;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j, ret;

    int color_packets;
    int color_changes;
    int color_shift;
    unsigned char r, g, b;

    int lines;
    int compressed_lines;
    int starting_line;
    int line_packets;
    int y_ptr;
    int byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;
    unsigned int pixel_limit;

    bytestream2_init(&g2, buf, buf_size);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    pixels = s->frame->data[0];
    pixel_limit = s->avctx->height * s->frame->linesize[0];
    if (buf_size < 16 || buf_size > INT_MAX - (3 * 256 + AV_INPUT_BUFFER_PADDING_SIZE))
        return AVERROR_INVALIDDATA;
    frame_size = bytestream2_get_le32(&g2);
    if (frame_size > buf_size)
        frame_size = buf_size;
    bytestream2_skip(&g2, 2); /* skip the magic number */
    num_chunks = bytestream2_get_le16(&g2);
    bytestream2_skip(&g2, 8);  /* skip padding */

    if (frame_size < 16)
        return AVERROR_INVALIDDATA;

    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size >= 6) && (num_chunks > 0) &&
            bytestream2_get_bytes_left(&g2) >= 4) {
        int stream_ptr_after_chunk;
        chunk_size = bytestream2_get_le32(&g2);
        if (chunk_size > frame_size) {
            av_log(avctx, AV_LOG_WARNING,
                   "Invalid chunk_size = %u > frame_size = %u\n", chunk_size, frame_size);
            chunk_size = frame_size;
        }
        stream_ptr_after_chunk = bytestream2_tell(&g2) - 4 + chunk_size;

        chunk_type = bytestream2_get_le16(&g2);

        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            /* check special case: If this file is from the Magic Carpet
             * game and uses 6-bit colors even though it reports 256-color
             * chunks in a 0xAF12-type file (fli_type is set to 0xAF13 during
             * initialization) */
            if ((chunk_type == FLI_256_COLOR) && (s->fli_type != FLC_MAGIC_CARPET_SYNTHETIC_TYPE_CODE))
                color_shift = 0;
            else
                color_shift = 2;
            /* set up the palette */
            color_packets = bytestream2_get_le16(&g2);
            palette_ptr = 0;
            for (i = 0; i < color_packets; i++) {
                /* first byte is how many colors to skip */
                palette_ptr += bytestream2_get_byte(&g2);

                /* next byte indicates how many entries to change */
                color_changes = bytestream2_get_byte(&g2);

                /* if there are 0 color changes, there are actually 256 */
                if (color_changes == 0)
                    color_changes = 256;

                if (bytestream2_tell(&g2) + color_changes * 3 > stream_ptr_after_chunk)
                    break;

                for (j = 0; j < color_changes; j++) {
                    unsigned int entry;

                    /* wrap around, for good measure */
                    if ((unsigned)palette_ptr >= 256)
                        palette_ptr = 0;

                    r = bytestream2_get_byte(&g2) << color_shift;
                    g = bytestream2_get_byte(&g2) << color_shift;
                    b = bytestream2_get_byte(&g2) << color_shift;
                    entry = 0xFFU << 24 | r << 16 | g << 8 | b;
                    if (color_shift == 2)
                        entry |= entry >> 6 & 0x30303;
                    if (s->palette[palette_ptr] != entry)
                        s->new_palette = 1;
                    s->palette[palette_ptr++] = entry;
                }
            }
            break;

        case FLI_DELTA:
            y_ptr = 0;
            compressed_lines = bytestream2_get_le16(&g2);
            while (compressed_lines > 0) {
                if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                    break;
                if (y_ptr > pixel_limit)
                    return AVERROR_INVALIDDATA;
                line_packets = sign_extend(bytestream2_get_le16(&g2), 16);
                if ((line_packets & 0xC000) == 0xC000) {
                    // line skip opcode
                    line_packets = -line_packets;
                    if (line_packets > s->avctx->height)
                        return AVERROR_INVALIDDATA;
                    y_ptr += line_packets * s->frame->linesize[0];
                } else if ((line_packets & 0xC000) == 0x4000) {
                    av_log(avctx, AV_LOG_ERROR, "Undefined opcode (%x) in DELTA_FLI\n", line_packets);
                } else if ((line_packets & 0xC000) == 0x8000) {
                    // "last byte" opcode
                    pixel_ptr= y_ptr + s->frame->linesize[0] - 1;
                    CHECK_PIXEL_PTR(0);
                    pixels[pixel_ptr] = line_packets & 0xff;
                } else {
                    compressed_lines--;
                    pixel_ptr = y_ptr;
                    CHECK_PIXEL_PTR(0);
                    pixel_countdown = s->avctx->width;
                    for (i = 0; i < line_packets; i++) {
                        if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                            break;
                        /* account for the skip bytes */
                        pixel_skip = bytestream2_get_byte(&g2);
                        pixel_ptr += pixel_skip;
                        pixel_countdown -= pixel_skip;
                        byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_idx1 = bytestream2_get_byte(&g2);
                            palette_idx2 = bytestream2_get_byte(&g2);
                            CHECK_PIXEL_PTR(byte_run * 2);
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 2) {
                                pixels[pixel_ptr++] = palette_idx1;
                                pixels[pixel_ptr++] = palette_idx2;
                            }
                        } else {
                            CHECK_PIXEL_PTR(byte_run * 2);
                            if (bytestream2_tell(&g2) + byte_run * 2 > stream_ptr_after_chunk)
                                break;
                            for (j = 0; j < byte_run * 2; j++, pixel_countdown--) {
                                pixels[pixel_ptr++] = bytestream2_get_byte(&g2);
                            }
                        }
                    }

                    y_ptr += s->frame->linesize[0];
                }
            }
            break;

        case FLI_LC:
            /* line compressed */
            starting_line = bytestream2_get_le16(&g2);
            if (starting_line >= s->avctx->height)
                return AVERROR_INVALIDDATA;
            y_ptr = 0;
            y_ptr += starting_line * s->frame->linesize[0];

            compressed_lines = bytestream2_get_le16(&g2);
            while (compressed_lines > 0) {
                pixel_ptr = y_ptr;
                CHECK_PIXEL_PTR(0);
                pixel_countdown = s->avctx->width;
                if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                    break;
                line_packets = bytestream2_get_byte(&g2);
                if (line_packets > 0) {
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                            break;
                        pixel_skip = bytestream2_get_byte(&g2);
                        pixel_ptr += pixel_skip;
                        pixel_countdown -= pixel_skip;
                        byte_run = sign_extend(bytestream2_get_byte(&g2),8);
                        if (byte_run > 0) {
                            CHECK_PIXEL_PTR(byte_run);
                            if (bytestream2_tell(&g2) + byte_run > stream_ptr_after_chunk)
                                break;
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                pixels[pixel_ptr++] = bytestream2_get_byte(&g2);
                            }
                        } else if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_idx1 = bytestream2_get_byte(&g2);
                            CHECK_PIXEL_PTR(byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                pixels[pixel_ptr++] = palette_idx1;
                            }
                        }
                    }
                }

                y_ptr += s->frame->linesize[0];
                compressed_lines--;
            }
            break;

        case FLI_BLACK:
            /* set the whole frame to color 0 (which is usually black) */
            memset(pixels, 0,
                s->frame->linesize[0] * s->avctx->height);
            break;

        case FLI_BRUN:
            /* Byte run compression: This chunk type only occurs in the first
             * FLI frame and it will update the entire frame. */
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                 bytestream2_skip(&g2, 1);
                pixel_countdown = s->avctx->width;
                while (pixel_countdown > 0) {
                    if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                        break;
                    byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                    if (!byte_run) {
                        av_log(avctx, AV_LOG_ERROR, "Invalid byte run value.\n");
                        return AVERROR_INVALIDDATA;
                    }

                    if (byte_run > 0) {
                        palette_idx1 = bytestream2_get_byte(&g2);
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
                        if (bytestream2_tell(&g2) + byte_run > stream_ptr_after_chunk)
                            break;
                        for (j = 0; j < byte_run; j++) {
                            pixels[pixel_ptr++] = bytestream2_get_byte(&g2);
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
                        }
                    }
                }

                y_ptr += s->frame->linesize[0];
            }
            break;

        case FLI_COPY:
            /* copy the chunk (uncompressed frame) */
            if (chunk_size - 6 != FFALIGN(s->avctx->width, 4) * s->avctx->height) {
                av_log(avctx, AV_LOG_ERROR, "In chunk FLI_COPY : source data (%d bytes) " \
                       "has incorrect size, skipping chunk\n", chunk_size - 6);
                bytestream2_skip(&g2, chunk_size - 6);
            } else {
                for (y_ptr = 0; y_ptr < s->frame->linesize[0] * s->avctx->height;
                     y_ptr += s->frame->linesize[0]) {
                    bytestream2_get_buffer(&g2, &pixels[y_ptr],
                                           s->avctx->width);
                    if (s->avctx->width & 3)
                        bytestream2_skip(&g2, 4 - (s->avctx->width & 3));
                }
            }
            break;

        case FLI_MINI:
            /* some sort of a thumbnail? disregard this chunk... */
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Unrecognized chunk type: %d\n", chunk_type);
            break;
        }

        if (stream_ptr_after_chunk - bytestream2_tell(&g2) >= 0) {
            bytestream2_skip(&g2, stream_ptr_after_chunk - bytestream2_tell(&g2));
        } else {
            av_log(avctx, AV_LOG_ERROR, "Chunk overread\n");
            break;
        }

        frame_size -= chunk_size;
        num_chunks--;
    }

    /* by the end of the chunk, the stream ptr should equal the frame
     * size (minus 1 or 2, possibly); if it doesn't, issue a warning */
    if (bytestream2_get_bytes_left(&g2) > 2)
        av_log(avctx, AV_LOG_ERROR, "Processed FLI chunk where chunk size = %d " \
               "and final chunk ptr = %d\n", buf_size,
               buf_size - bytestream2_get_bytes_left(&g2));

    /* make the palette available on the way out */
    memcpy(s->frame->data[1], s->palette, AVPALETTE_SIZE);
    if (s->new_palette) {
        s->frame->palette_has_changed = 1;
        s->new_palette = 0;
    }

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static int flic_decode_frame_15_16BPP(AVCodecContext *avctx,
                                      AVFrame *rframe, int *got_frame,
                                      const uint8_t *buf, int buf_size)
{
    /* Note, the only difference between the 15Bpp and 16Bpp */
    /* Format is the pixel format, the packets are processed the same. */
    FlicDecodeContext *s = avctx->priv_data;

    GetByteContext g2;
    int pixel_ptr;
    unsigned char palette_idx1;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j, ret;

    int lines;
    int compressed_lines;
    int line_packets;
    int y_ptr;
    int byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;
    int pixel;
    unsigned int pixel_limit;

    bytestream2_init(&g2, buf, buf_size);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    pixels = s->frame->data[0];
    pixel_limit = s->avctx->height * s->frame->linesize[0];

    frame_size = bytestream2_get_le32(&g2);
    bytestream2_skip(&g2, 2);  /* skip the magic number */
    num_chunks = bytestream2_get_le16(&g2);
    bytestream2_skip(&g2, 8);  /* skip padding */
    if (frame_size > buf_size)
        frame_size = buf_size;

    if (frame_size < 16)
        return AVERROR_INVALIDDATA;
    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size > 0) && (num_chunks > 0) &&
            bytestream2_get_bytes_left(&g2) >= 4) {
        int stream_ptr_after_chunk;
        chunk_size = bytestream2_get_le32(&g2);
        if (chunk_size > frame_size) {
            av_log(avctx, AV_LOG_WARNING,
                   "Invalid chunk_size = %u > frame_size = %u\n", chunk_size, frame_size);
            chunk_size = frame_size;
        }
        stream_ptr_after_chunk = bytestream2_tell(&g2) - 4 + chunk_size;

        chunk_type = bytestream2_get_le16(&g2);


        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            /* For some reason, it seems that non-palettized flics do
             * include one of these chunks in their first frame.
             * Why I do not know, it seems rather extraneous. */
            ff_dlog(avctx,
                    "Unexpected Palette chunk %d in non-palettized FLC\n",
                    chunk_type);
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        case FLI_DELTA:
        case FLI_DTA_LC:
            y_ptr = 0;
            compressed_lines = bytestream2_get_le16(&g2);
            while (compressed_lines > 0) {
                if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                    break;
                if (y_ptr > pixel_limit)
                    return AVERROR_INVALIDDATA;
                line_packets = sign_extend(bytestream2_get_le16(&g2), 16);
                if (line_packets < 0) {
                    line_packets = -line_packets;
                    if (line_packets > s->avctx->height)
                        return AVERROR_INVALIDDATA;
                    y_ptr += line_packets * s->frame->linesize[0];
                } else {
                    compressed_lines--;
                    pixel_ptr = y_ptr;
                    CHECK_PIXEL_PTR(0);
                    pixel_countdown = s->avctx->width;
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                            break;
                        pixel_skip = bytestream2_get_byte(&g2);
                        pixel_ptr += (pixel_skip*2); /* Pixel is 2 bytes wide */
                        pixel_countdown -= pixel_skip;
                        byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            pixel    = bytestream2_get_le16(&g2);
                            CHECK_PIXEL_PTR(2 * byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 2) {
                                *((signed short*)(&pixels[pixel_ptr])) = pixel;
                                pixel_ptr += 2;
                            }
                        } else {
                            if (bytestream2_tell(&g2) + 2*byte_run > stream_ptr_after_chunk)
                                break;
                            CHECK_PIXEL_PTR(2 * byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                *((signed short*)(&pixels[pixel_ptr])) = bytestream2_get_le16(&g2);
                                pixel_ptr += 2;
                            }
                        }
                    }

                    y_ptr += s->frame->linesize[0];
                }
            }
            break;

        case FLI_LC:
            av_log(avctx, AV_LOG_ERROR, "Unexpected FLI_LC chunk in non-palettized FLC\n");
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        case FLI_BLACK:
            /* set the whole frame to 0x0000 which is black in both 15Bpp and 16Bpp modes. */
            memset(pixels, 0x0000,
                   s->frame->linesize[0] * s->avctx->height);
            break;

        case FLI_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                bytestream2_skip(&g2, 1);
                pixel_countdown = (s->avctx->width * 2);

                while (pixel_countdown > 0) {
                    if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                        break;
                    byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                    if (byte_run > 0) {
                        palette_idx1 = bytestream2_get_byte(&g2);
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
                        if (bytestream2_tell(&g2) + byte_run > stream_ptr_after_chunk)
                            break;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            palette_idx1 = bytestream2_get_byte(&g2);
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
                        }
                    }
                }

                /* Now FLX is strange, in that it is "byte" as opposed to "pixel" run length compressed.
                 * This does not give us any good opportunity to perform word endian conversion
                 * during decompression. So if it is required (i.e., this is not a LE target, we do
                 * a second pass over the line here, swapping the bytes.
                 */
#if HAVE_BIGENDIAN
                pixel_ptr = y_ptr;
                pixel_countdown = s->avctx->width;
                while (pixel_countdown > 0) {
                    *((signed short*)(&pixels[pixel_ptr])) = AV_RL16(&buf[pixel_ptr]);
                    pixel_ptr += 2;
                }
#endif
                y_ptr += s->frame->linesize[0];
            }
            break;

        case FLI_DTA_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                bytestream2_skip(&g2, 1);
                pixel_countdown = s->avctx->width; /* Width is in pixels, not bytes */

                while (pixel_countdown > 0) {
                    if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                        break;
                    byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                    if (byte_run > 0) {
                        pixel    = bytestream2_get_le16(&g2);
                        CHECK_PIXEL_PTR(2 * byte_run);
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
                        if (bytestream2_tell(&g2) + 2 * byte_run > stream_ptr_after_chunk)
                            break;
                        CHECK_PIXEL_PTR(2 * byte_run);
                        for (j = 0; j < byte_run; j++) {
                            *((signed short*)(&pixels[pixel_ptr])) = bytestream2_get_le16(&g2);
                            pixel_ptr  += 2;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    }
                }

                y_ptr += s->frame->linesize[0];
            }
            break;

        case FLI_COPY:
        case FLI_DTA_COPY:
            /* copy the chunk (uncompressed frame) */
            if (chunk_size - 6 > (unsigned int)(FFALIGN(s->avctx->width, 2) * s->avctx->height)*2) {
                av_log(avctx, AV_LOG_ERROR, "In chunk FLI_COPY : source data (%d bytes) " \
                       "bigger than image, skipping chunk\n", chunk_size - 6);
                bytestream2_skip(&g2, chunk_size - 6);
            } else {

                if (bytestream2_get_bytes_left(&g2) < 2 * s->avctx->width * s->avctx->height )
                    return AVERROR_INVALIDDATA;
                for (y_ptr = 0; y_ptr < s->frame->linesize[0] * s->avctx->height;
                     y_ptr += s->frame->linesize[0]) {

                    pixel_countdown = s->avctx->width;
                    pixel_ptr = 0;
                    while (pixel_countdown > 0) {
                      *((signed short*)(&pixels[y_ptr + pixel_ptr])) = bytestream2_get_le16(&g2);
                      pixel_ptr += 2;
                      pixel_countdown--;
                    }
                    if (s->avctx->width & 1)
                        bytestream2_skip(&g2, 2);
                }
            }
            break;

        case FLI_MINI:
            /* some sort of a thumbnail? disregard this chunk... */
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Unrecognized chunk type: %d\n", chunk_type);
            break;
        }

        if (stream_ptr_after_chunk - bytestream2_tell(&g2) >= 0) {
            bytestream2_skip(&g2, stream_ptr_after_chunk - bytestream2_tell(&g2));
        } else {
            av_log(avctx, AV_LOG_ERROR, "Chunk overread\n");
            break;
        }

        frame_size -= chunk_size;
        num_chunks--;
    }

    /* by the end of the chunk, the stream ptr should equal the frame
     * size (minus 1, possibly); if it doesn't, issue a warning */
    if ((bytestream2_get_bytes_left(&g2) != 0) && (bytestream2_get_bytes_left(&g2) != 1))
        av_log(avctx, AV_LOG_ERROR, "Processed FLI chunk where chunk size = %d " \
               "and final chunk ptr = %d\n", buf_size, bytestream2_tell(&g2));

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static int flic_decode_frame_24BPP(AVCodecContext *avctx,
                                   AVFrame *rframe, int *got_frame,
                                   const uint8_t *buf, int buf_size)
{
    FlicDecodeContext *s = avctx->priv_data;

    GetByteContext g2;
    int pixel_ptr;
    unsigned char palette_idx1;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j, ret;

    int lines;
    int compressed_lines;
    int line_packets;
    int y_ptr;
    int byte_run;
    int pixel_skip;
    int pixel_countdown;
    unsigned char *pixels;
    int pixel;
    unsigned int pixel_limit;

    bytestream2_init(&g2, buf, buf_size);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    pixels = s->frame->data[0];
    pixel_limit = s->avctx->height * s->frame->linesize[0];

    frame_size = bytestream2_get_le32(&g2);
    bytestream2_skip(&g2, 2);  /* skip the magic number */
    num_chunks = bytestream2_get_le16(&g2);
    bytestream2_skip(&g2, 8);  /* skip padding */
    if (frame_size > buf_size)
        frame_size = buf_size;

    if (frame_size < 16)
        return AVERROR_INVALIDDATA;
    frame_size -= 16;

    /* iterate through the chunks */
    while ((frame_size > 0) && (num_chunks > 0) &&
            bytestream2_get_bytes_left(&g2) >= 4) {
        int stream_ptr_after_chunk;
        chunk_size = bytestream2_get_le32(&g2);
        if (chunk_size > frame_size) {
            av_log(avctx, AV_LOG_WARNING,
                   "Invalid chunk_size = %u > frame_size = %u\n", chunk_size, frame_size);
            chunk_size = frame_size;
        }
        stream_ptr_after_chunk = bytestream2_tell(&g2) - 4 + chunk_size;

        chunk_type = bytestream2_get_le16(&g2);


        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            /* For some reason, it seems that non-palettized flics do
             * include one of these chunks in their first frame.
             * Why I do not know, it seems rather extraneous. */
            ff_dlog(avctx,
                    "Unexpected Palette chunk %d in non-palettized FLC\n",
                    chunk_type);
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        case FLI_DELTA:
        case FLI_DTA_LC:
            y_ptr = 0;
            compressed_lines = bytestream2_get_le16(&g2);
            while (compressed_lines > 0) {
                if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                    break;
                if (y_ptr > pixel_limit)
                    return AVERROR_INVALIDDATA;
                line_packets = sign_extend(bytestream2_get_le16(&g2), 16);
                if (line_packets < 0) {
                    line_packets = -line_packets;
                    if (line_packets > s->avctx->height)
                        return AVERROR_INVALIDDATA;
                    y_ptr += line_packets * s->frame->linesize[0];
                } else {
                    compressed_lines--;
                    pixel_ptr = y_ptr;
                    CHECK_PIXEL_PTR(0);
                    pixel_countdown = s->avctx->width;
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        if (bytestream2_tell(&g2) + 2 > stream_ptr_after_chunk)
                            break;
                        pixel_skip = bytestream2_get_byte(&g2);
                        pixel_ptr += (pixel_skip*3); /* Pixel is 3 bytes wide */
                        pixel_countdown -= pixel_skip;
                        byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            pixel    = bytestream2_get_le24(&g2);
                            CHECK_PIXEL_PTR(3 * byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown -= 1) {
                                AV_WL24(&pixels[pixel_ptr], pixel);
                                pixel_ptr += 3;
                            }
                        } else {
                            if (bytestream2_tell(&g2) + 2*byte_run > stream_ptr_after_chunk)
                                break;
                            CHECK_PIXEL_PTR(3 * byte_run);
                            for (j = 0; j < byte_run; j++, pixel_countdown--) {
                                pixel = bytestream2_get_le24(&g2);
                                AV_WL24(&pixels[pixel_ptr], pixel);
                                pixel_ptr += 3;
                            }
                        }
                    }

                    y_ptr += s->frame->linesize[0];
                }
            }
            break;

        case FLI_LC:
            av_log(avctx, AV_LOG_ERROR, "Unexpected FLI_LC chunk in non-palettized FLC\n");
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        case FLI_BLACK:
            /* set the whole frame to 0x00 which is black for 24 bit mode. */
            memset(pixels, 0x00,
                   s->frame->linesize[0] * s->avctx->height);
            break;

        case FLI_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                bytestream2_skip(&g2, 1);
                pixel_countdown = (s->avctx->width * 3);

                while (pixel_countdown > 0) {
                    if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                        break;
                    byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                    if (byte_run > 0) {
                        palette_idx1 = bytestream2_get_byte(&g2);
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
                        if (bytestream2_tell(&g2) + byte_run > stream_ptr_after_chunk)
                            break;
                        CHECK_PIXEL_PTR(byte_run);
                        for (j = 0; j < byte_run; j++) {
                            palette_idx1 = bytestream2_get_byte(&g2);
                            pixels[pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d) at line %d\n",
                                       pixel_countdown, lines);
                        }
                    }
                }

                y_ptr += s->frame->linesize[0];
            }
            break;

        case FLI_DTA_BRUN:
            y_ptr = 0;
            for (lines = 0; lines < s->avctx->height; lines++) {
                pixel_ptr = y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                bytestream2_skip(&g2, 1);
                pixel_countdown = s->avctx->width; /* Width is in pixels, not bytes */

                while (pixel_countdown > 0) {
                    if (bytestream2_tell(&g2) + 1 > stream_ptr_after_chunk)
                        break;
                    byte_run = sign_extend(bytestream2_get_byte(&g2), 8);
                    if (byte_run > 0) {
                        pixel = bytestream2_get_le24(&g2);
                        CHECK_PIXEL_PTR(3 * byte_run);
                        for (j = 0; j < byte_run; j++) {
                            AV_WL24(pixels + pixel_ptr, pixel);
                            pixel_ptr += 3;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    } else {  /* copy pixels if byte_run < 0 */
                        byte_run = -byte_run;
                        if (bytestream2_tell(&g2) + 3 * byte_run > stream_ptr_after_chunk)
                            break;
                        CHECK_PIXEL_PTR(3 * byte_run);
                        for (j = 0; j < byte_run; j++) {
                            pixel = bytestream2_get_le24(&g2);
                            AV_WL24(pixels + pixel_ptr, pixel);
                            pixel_ptr  += 3;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                av_log(avctx, AV_LOG_ERROR, "pixel_countdown < 0 (%d)\n",
                                       pixel_countdown);
                        }
                    }
                }

                y_ptr += s->frame->linesize[0];
            }
            break;

        case FLI_COPY:
        case FLI_DTA_COPY:
            /* copy the chunk (uncompressed frame) */
            if (chunk_size - 6 > (unsigned int)(FFALIGN(s->avctx->width, 2) * s->avctx->height)*3) {
                av_log(avctx, AV_LOG_ERROR, "In chunk FLI_COPY : source data (%d bytes) " \
                       "bigger than image, skipping chunk\n", chunk_size - 6);
                bytestream2_skip(&g2, chunk_size - 6);
            } else {
                for (y_ptr = 0; y_ptr < s->frame->linesize[0] * s->avctx->height;
                     y_ptr += s->frame->linesize[0]) {

                    bytestream2_get_buffer(&g2, pixels + y_ptr, 3*s->avctx->width);
                    if (s->avctx->width & 1)
                        bytestream2_skip(&g2, 3);
                }
            }
            break;

        case FLI_MINI:
            /* some sort of a thumbnail? disregard this chunk... */
            bytestream2_skip(&g2, chunk_size - 6);
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Unrecognized chunk type: %d\n", chunk_type);
            break;
        }

        if (stream_ptr_after_chunk - bytestream2_tell(&g2) >= 0) {
            bytestream2_skip(&g2, stream_ptr_after_chunk - bytestream2_tell(&g2));
        } else {
            av_log(avctx, AV_LOG_ERROR, "Chunk overread\n");
            break;
        }

        frame_size -= chunk_size;
        num_chunks--;
    }

    /* by the end of the chunk, the stream ptr should equal the frame
     * size (minus 1, possibly); if it doesn't, issue a warning */
    if ((bytestream2_get_bytes_left(&g2) != 0) && (bytestream2_get_bytes_left(&g2) != 1))
        av_log(avctx, AV_LOG_ERROR, "Processed FLI chunk where chunk size = %d " \
               "and final chunk ptr = %d\n", buf_size, bytestream2_tell(&g2));

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static int flic_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        return flic_decode_frame_8BPP(avctx, frame, got_frame,
                                      buf, buf_size);
    } else if ((avctx->pix_fmt == AV_PIX_FMT_RGB555) ||
               (avctx->pix_fmt == AV_PIX_FMT_RGB565)) {
        return flic_decode_frame_15_16BPP(avctx, frame, got_frame,
                                          buf, buf_size);
    } else if (avctx->pix_fmt == AV_PIX_FMT_BGR24) {
        return flic_decode_frame_24BPP(avctx, frame, got_frame,
                                       buf, buf_size);
    }

    /* Should not get  here, ever as the pix_fmt is processed */
    /* in flic_decode_init and the above if should deal with */
    /* the finite set of possibilities allowable by here. */
    /* But in case we do, just error out. */
    av_log(avctx, AV_LOG_ERROR, "Unknown FLC format, my science cannot explain how this happened.\n");
    return AVERROR_BUG;
}


static av_cold int flic_decode_end(AVCodecContext *avctx)
{
    FlicDecodeContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

const FFCodec ff_flic_decoder = {
    .p.name         = "flic",
    CODEC_LONG_NAME("Autodesk Animator Flic video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FLIC,
    .priv_data_size = sizeof(FlicDecodeContext),
    .init           = flic_decode_init,
    .close          = flic_decode_end,
    FF_CODEC_DECODE_CB(flic_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
