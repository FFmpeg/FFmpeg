/*
 * OpenEXR (.exr) image decoder
 * Copyright (c) 2009 Jimmy Christensen
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
 * OpenEXR decoder
 * @author Jimmy Christensen
 *
 * For more information on the OpenEXR format, visit:
 *  http://openexr.com/
 *
 * exr_flt2uint() and exr_halflt2uint() is credited to  Reimar Döffinger
 */

#include "avcodec.h"
#include "bytestream.h"
#include "libavutil/imgutils.h"

enum ExrCompr {
    EXR_RAW   = 0,
    EXR_RLE   = 1,
    EXR_ZIP1  = 2,
    EXR_ZIP16 = 3,
    EXR_PIZ   = 4,
    EXR_B44   = 6
};

typedef struct EXRContext {
    AVFrame picture;
    int compr;
    int bits_per_color_id;
    int8_t channel_offsets[4]; // 0 = red, 1 = green, 2 = blue and 3 = alpha
} EXRContext;

/**
 * Converts from 32-bit float as uint32_t to uint16_t
 *
 * @param v 32-bit float
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_flt2uint(uint32_t v)
{
    unsigned int exp = v >> 23;
    // "HACK": negative values result in exp<  0, so clipping them to 0
    // is also handled by this condition, avoids explicit check for sign bit.
    if (exp<= 127 + 7 - 24) // we would shift out all bits anyway
        return 0;
    if (exp >= 127)
        return 0xffff;
    v &= 0x007fffff;
    return (v + (1 << 23)) >> (127 + 7 - exp);
}

/**
 * Converts from 16-bit float as uint16_t to uint16_t
 *
 * @param v 16-bit float
 * @return normalized 16-bit unsigned int
 */
static inline uint16_t exr_halflt2uint(uint16_t v)
{
    unsigned exp = 14 - (v >> 10);
    if (exp >= 14) {
        if (exp == 14) return (v >> 9) & 1;
        else           return (v & 0x8000) ? 0 : 0xffff;
    }
    v <<= 6;
    return (v + (1 << 16)) >> (exp + 1);
}

/**
 * Gets the size of the header variable
 *
 * @param **buf the current pointer location in the header where
 * the variable data starts
 * @param *buf_end pointer location of the end of the buffer
 * @return size of variable data
 */
static unsigned int get_header_variable_length(const uint8_t **buf,
                                               const uint8_t *buf_end)
{
    unsigned int variable_buffer_data_size = bytestream_get_le32(buf);
    if (variable_buffer_data_size >= buf_end - *buf)
        return 0;
    return variable_buffer_data_size;
}

/**
 * Checks if the variable name corresponds with it's data type
 *
 * @param *avctx the AVCodecContext
 * @param **buf the current pointer location in the header where
 * the variable name starts
 * @param *buf_end pointer location of the end of the buffer
 * @param *value_name name of the varible to check
 * @param *value_type type of the varible to check
 * @param minimum_length minimum length of the variable data
 * @param variable_buffer_data_size variable length read from the header
 * after it's checked
 * @return negative if variable is invalid
 */
static int check_header_variable(AVCodecContext *avctx,
                                              const uint8_t **buf,
                                              const uint8_t *buf_end,
                                              const char *value_name,
                                              const char *value_type,
                                              unsigned int minimum_length,
                                              unsigned int *variable_buffer_data_size)
{
    if (buf_end - *buf >= minimum_length && !strcmp(*buf, value_name)) {
        *buf += strlen(value_name)+1;
        if (!strcmp(*buf, value_type)) {
            *buf += strlen(value_type)+1;
            *variable_buffer_data_size = get_header_variable_length(buf, buf_end);
            if (!*variable_buffer_data_size)
                av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
            if (*variable_buffer_data_size > buf_end - *buf)
                return -1;
            return 1;
        }
        *buf -= strlen(value_name)+1;
        av_log(avctx, AV_LOG_WARNING, "Unknown data type for header variable %s\n", value_name);
    }
    return -1;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data,
                        int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf      = avpkt->data;
    unsigned int   buf_size = avpkt->size;
    const uint8_t *buf_end  = buf + buf_size;

    EXRContext *const s = avctx->priv_data;
    AVFrame *picture  = data;
    AVFrame *const p = &s->picture;
    uint8_t *ptr;

    int i, x, y, stride, magic_number, version_flag;
    int w = 0;
    int h = 0;
    unsigned int xmin   = ~0;
    unsigned int xmax   = ~0;
    unsigned int ymin   = ~0;
    unsigned int ymax   = ~0;
    unsigned int xdelta = ~0;

    unsigned int current_channel_offset = 0;

    s->channel_offsets[0] = -1;
    s->channel_offsets[1] = -1;
    s->channel_offsets[2] = -1;
    s->channel_offsets[3] = -1;
    s->bits_per_color_id = -1;

    if (buf_end - buf < 10) {
        av_log(avctx, AV_LOG_ERROR, "Too short header to parse\n");
        return -1;
    }

    magic_number = bytestream_get_le32(&buf);
    if (magic_number != 20000630) { // As per documentation of OpenEXR it's supposed to be int 20000630 little-endian
        av_log(avctx, AV_LOG_ERROR, "Wrong magic number %d\n", magic_number);
        return -1;
    }

    version_flag = bytestream_get_le32(&buf);
    if ((version_flag & 0x200) == 0x200) {
        av_log(avctx, AV_LOG_ERROR, "Tile based images are not supported\n");
        return -1;
    }

    // Parse the header
    while (buf < buf_end && buf[0]) {
        unsigned int variable_buffer_data_size;
        // Process the channel list
        if (check_header_variable(avctx, &buf, buf_end, "channels", "chlist", 38, &variable_buffer_data_size) >= 0) {
            const uint8_t *channel_list_end;
            if (!variable_buffer_data_size)
                return -1;

            channel_list_end = buf + variable_buffer_data_size;
            while (channel_list_end - buf >= 19) {
                int current_bits_per_color_id = -1;
                int channel_index = -1;

                if (!strcmp(buf, "R"))
                    channel_index = 0;
                if (!strcmp(buf, "G"))
                    channel_index = 1;
                if (!strcmp(buf, "B"))
                    channel_index = 2;
                if (!strcmp(buf, "A"))
                    channel_index = 3;

                while (bytestream_get_byte(&buf) && buf < channel_list_end)
                    continue; /* skip */

                if (channel_list_end - * &buf < 4) {
                    av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
                    return -1;
                }

                current_bits_per_color_id = bytestream_get_le32(&buf);
                if (current_bits_per_color_id > 2) {
                    av_log(avctx, AV_LOG_ERROR, "Unknown color format\n");
                    return -1;
                }

                if (channel_index >= 0) {
                    if (s->bits_per_color_id != -1 && s->bits_per_color_id != current_bits_per_color_id) {
                        av_log(avctx, AV_LOG_ERROR, "RGB channels not of the same depth\n");
                        return -1;
                    }
                    s->bits_per_color_id  = current_bits_per_color_id;
                    s->channel_offsets[channel_index] = current_channel_offset;
                }

                current_channel_offset += 1 << current_bits_per_color_id;
                buf += 12;
            }

            /* Check if all channels are set with an offset or if the channels
             * are causing an overflow  */

            if (FFMIN3(s->channel_offsets[0],
                       s->channel_offsets[1],
                       s->channel_offsets[2]) < 0) {
                if (s->channel_offsets[0] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing red channel\n");
                if (s->channel_offsets[1] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing green channel\n");
                if (s->channel_offsets[2] < 0)
                    av_log(avctx, AV_LOG_ERROR, "Missing blue channel\n");
                return -1;
            }

            buf = channel_list_end;
            continue;
        }

        // Process the dataWindow variable
        if (check_header_variable(avctx, &buf, buf_end, "dataWindow", "box2i", 31, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return -1;

            xmin = AV_RL32(buf);
            ymin = AV_RL32(buf + 4);
            xmax = AV_RL32(buf + 8);
            ymax = AV_RL32(buf + 12);
            xdelta = (xmax-xmin) + 1;

            buf += variable_buffer_data_size;
            continue;
        }

        // Process the displayWindow variable
        if (check_header_variable(avctx, &buf, buf_end, "displayWindow", "box2i", 34, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return -1;

            w = AV_RL32(buf + 8) + 1;
            h = AV_RL32(buf + 12) + 1;

            buf += variable_buffer_data_size;
            continue;
        }

        // Process the lineOrder variable
        if (check_header_variable(avctx, &buf, buf_end, "lineOrder", "lineOrder", 25, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return -1;

            if (*buf) {
                av_log(avctx, AV_LOG_ERROR, "Doesn't support this line order : %d\n", *buf);
                return -1;
            }

            buf += variable_buffer_data_size;
            continue;
        }

        // Process the compression variable
        if (check_header_variable(avctx, &buf, buf_end, "compression", "compression", 29, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return -1;

            s->compr = *buf;
            switch (s->compr) {
            case EXR_RAW:
                break;
            case EXR_RLE:
            case EXR_ZIP1:
            case EXR_ZIP16:
            case EXR_PIZ:
            case EXR_B44:
            default:
                av_log(avctx, AV_LOG_ERROR, "Compression type %d is not supported\n", s->compr);
                return -1;
            }

            buf += variable_buffer_data_size;
            continue;
        }

        // Check if there is enough bytes for a header
        if (buf_end - buf <= 9) {
            av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
            return -1;
        }

        // Process unknown variables
        for (i = 0; i < 2; i++) {
            // Skip variable name/type
            while (++buf < buf_end)
                if (buf[0] == 0x0)
                    break;
        }
        buf++;
        // Skip variable length
        if (buf_end - buf >= 5) {
            variable_buffer_data_size = get_header_variable_length(&buf, buf_end);
            if (!variable_buffer_data_size) {
                av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
                return -1;
            }
            buf += variable_buffer_data_size;
        }
    }

    if (buf >= buf_end) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete frame\n");
        return -1;
    }
    buf++;

    switch (s->bits_per_color_id) {
    case 2: // 32-bit
    case 1: // 16-bit
        if (s->channel_offsets[3] >= 0)
            avctx->pix_fmt = PIX_FMT_RGBA64;
        else
            avctx->pix_fmt = PIX_FMT_RGB48;
        break;
    // 8-bit
    case 0:
        av_log_missing_feature(avctx, "8-bit OpenEXR", 1);
        return -1;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown color format : %d\n", s->bits_per_color_id);
        return -1;
    }

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    if (av_image_check_size(w, h, 0, avctx))
        return -1;

    // Verify the xmin, xmax, ymin, ymax and xdelta before setting the actual image size
    if (xmin > xmax || ymin > ymax || xdelta != xmax - xmin + 1 || xmax >= w || ymax >= h) {
        av_log(avctx, AV_LOG_ERROR, "Wrong sizing or missing size information\n");
        return -1;
    }

    if (w != avctx->width || h != avctx->height) {
        avcodec_set_dimensions(avctx, w, h);
    }

    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    ptr    = p->data[0];
    stride = p->linesize[0];

    // Zero out the start if ymin is not 0
    for (y = 0; y < ymin; y++) {
        memset(ptr, 0, avctx->width * 2 * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components);
        ptr += stride;
    }

    // Process the actual lines
    for (y = ymin; y <= ymax; y++) {
        uint16_t *ptr_x = (uint16_t *)ptr;
        if (buf_end - buf > 8) {
            /* Read the lineoffset from the line offset table and add 8 bytes
               to skip the coordinates and data size fields */
            const uint64_t line_offset = bytestream_get_le64(&buf) + 8;
            // Check if the buffer has the required bytes needed from the offset
            if (line_offset > avpkt->size - xdelta * current_channel_offset) {
                // Line offset is probably wrong and not inside the buffer
                av_log(avctx, AV_LOG_WARNING, "Line offset for line %d is out of reach setting it to black\n", y);
                memset(ptr_x, 0, avctx->width * 2 * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components);
            } else {
                const uint8_t *red_channel_buffer   = avpkt->data + line_offset + xdelta * s->channel_offsets[0];
                const uint8_t *green_channel_buffer = avpkt->data + line_offset + xdelta * s->channel_offsets[1];
                const uint8_t *blue_channel_buffer  = avpkt->data + line_offset + xdelta * s->channel_offsets[2];
                const uint8_t *alpha_channel_buffer = 0;

                if (s->channel_offsets[3] >= 0)
                    alpha_channel_buffer = avpkt->data + line_offset + xdelta * s->channel_offsets[3];

                // Zero out the start if xmin is not 0
                memset(ptr_x, 0, xmin * 2 * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components);
                ptr_x += xmin * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components;
                if (s->bits_per_color_id == 2) {
                    // 32-bit
                    for (x = 0; x < xdelta; x++) {
                        *ptr_x++ = exr_flt2uint(bytestream_get_le32(&red_channel_buffer));
                        *ptr_x++ = exr_flt2uint(bytestream_get_le32(&green_channel_buffer));
                        *ptr_x++ = exr_flt2uint(bytestream_get_le32(&blue_channel_buffer));
                        if (alpha_channel_buffer)
                            *ptr_x++ = exr_flt2uint(bytestream_get_le32(&alpha_channel_buffer));
                    }
                } else {
                    // 16-bit
                    for (x = 0; x < xdelta; x++) {
                        *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&red_channel_buffer));
                        *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&green_channel_buffer));
                        *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&blue_channel_buffer));
                        if (alpha_channel_buffer)
                            *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&alpha_channel_buffer));
                    }
                }

                // Zero out the end if xmax+1 is not w
                memset(ptr_x, 0, (avctx->width - (xmax + 1)) * 2 * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components);
                ptr_x += (avctx->width - (xmax + 1)) * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components;

            }
            // Move to next line
            ptr += stride;
        }
    }

    // Zero out the end if ymax+1 is not h
    for (y = ymax + 1; y < avctx->height; y++) {
        memset(ptr, 0, avctx->width * 2 * av_pix_fmt_descriptors[avctx->pix_fmt].nb_components);
        ptr += stride;
    }

    *picture   = s->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;
    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_exr_decoder = {
    .name               = "exr",
    .type               = AVMEDIA_TYPE_VIDEO,
    .id                 = CODEC_ID_EXR,
    .priv_data_size     = sizeof(EXRContext),
    .init               = decode_init,
    .close              = decode_end,
    .decode             = decode_frame,
    .long_name          = NULL_IF_CONFIG_SMALL("OpenEXR image"),
};
