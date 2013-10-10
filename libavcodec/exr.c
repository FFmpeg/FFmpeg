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

#include <zlib.h>

#include "avcodec.h"
#include "bytestream.h"
#include "mathops.h"
#include "thread.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

enum ExrCompr {
    EXR_RAW   = 0,
    EXR_RLE   = 1,
    EXR_ZIP1  = 2,
    EXR_ZIP16 = 3,
    EXR_PIZ   = 4,
    EXR_PXR24 = 5,
    EXR_B44   = 6,
    EXR_B44A  = 7,
};

enum ExrPixelType {
    EXR_UINT,
    EXR_HALF,
    EXR_FLOAT
};

typedef struct EXRChannel {
    int               xsub, ysub;
    enum ExrPixelType pixel_type;
} EXRChannel;

typedef struct EXRThreadData {
    uint8_t *uncompressed_data;
    int uncompressed_size;

    uint8_t *tmp;
    int tmp_size;
} EXRThreadData;

typedef struct EXRContext {
    AVFrame *picture;
    int compr;
    enum ExrPixelType pixel_type;
    int channel_offsets[4]; // 0 = red, 1 = green, 2 = blue and 3 = alpha
    const AVPixFmtDescriptor *desc;

    uint32_t xmax, xmin;
    uint32_t ymax, ymin;
    uint32_t xdelta, ydelta;

    int ysize;

    uint64_t scan_line_size;
    int scan_lines_per_block;

    const uint8_t *buf, *table;
    int buf_size;

    EXRChannel *channels;
    int nb_channels;

    EXRThreadData *thread_data;
    int thread_data_size;
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
            return 1;
        }
        *buf -= strlen(value_name)+1;
        av_log(avctx, AV_LOG_WARNING, "Unknown data type for header variable %s\n", value_name);
    }
    return -1;
}

static void predictor(uint8_t *src, int size)
{
    uint8_t *t = src + 1;
    uint8_t *stop = src + size;

    while (t < stop) {
        int d = (int)t[-1] + (int)t[0] - 128;
        t[0] = d;
        ++t;
    }
}

static void reorder_pixels(uint8_t *src, uint8_t *dst, int size)
{
    const int8_t *t1 = src;
    const int8_t *t2 = src + (size + 1) / 2;
    int8_t *s = dst;
    int8_t *stop = s + size;

    while (1) {
        if (s < stop)
            *(s++) = *(t1++);
        else
            break;

        if (s < stop)
            *(s++) = *(t2++);
        else
            break;
    }
}

static int zip_uncompress(const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    unsigned long dest_len = uncompressed_size;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK ||
        dest_len != uncompressed_size)
        return AVERROR(EINVAL);

    predictor(td->tmp, uncompressed_size);
    reorder_pixels(td->tmp, td->uncompressed_data, uncompressed_size);

    return 0;
}

static int rle_uncompress(const uint8_t *src, int compressed_size,
                          int uncompressed_size, EXRThreadData *td)
{
    int8_t *d = (int8_t *)td->tmp;
    const int8_t *s = (const int8_t *)src;
    int ssize = compressed_size;
    int dsize = uncompressed_size;
    int8_t *dend = d + dsize;
    int count;

    while (ssize > 0) {
        count = *s++;

        if (count < 0) {
            count = -count;

            if ((dsize -= count    ) < 0 ||
                (ssize -= count + 1) < 0)
                return -1;

            while (count--)
                *d++ = *s++;
        } else {
            count++;

            if ((dsize -= count) < 0 ||
                (ssize -= 2    ) < 0)
                return -1;

            while (count--)
                *d++ = *s;

            s++;
        }
    }

    if (dend != d)
        return AVERROR_INVALIDDATA;

    predictor(td->tmp, uncompressed_size);
    reorder_pixels(td->tmp, td->uncompressed_data, uncompressed_size);

    return 0;
}

static int pxr24_uncompress(EXRContext *s, const uint8_t *src,
                            int compressed_size, int uncompressed_size,
                            EXRThreadData *td)
{
    unsigned long dest_len = uncompressed_size;
    const uint8_t *in = td->tmp;
    uint8_t *out;
    int c, i, j;

    if (uncompress(td->tmp, &dest_len, src, compressed_size) != Z_OK ||
        dest_len != uncompressed_size)
        return AVERROR(EINVAL);

    out = td->uncompressed_data;
    for (i = 0; i < s->ysize; i++) {
        for (c = 0; c < s->nb_channels; c++) {
            EXRChannel *channel = &s->channels[c];
            const uint8_t *ptr[4];
            uint32_t pixel = 0;

            switch (channel->pixel_type) {
            case EXR_FLOAT:
                ptr[0] = in;
                ptr[1] = ptr[0] + s->xdelta;
                ptr[2] = ptr[1] + s->xdelta;
                in = ptr[2] + s->xdelta;

                for (j = 0; j < s->xdelta; ++j) {
                    uint32_t diff = (*(ptr[0]++) << 24) |
                                    (*(ptr[1]++) << 16) |
                                    (*(ptr[2]++) <<  8);
                    pixel += diff;
                    bytestream_put_le32(&out, pixel);
                }
                break;
            case EXR_HALF:
                ptr[0] = in;
                ptr[1] = ptr[0] + s->xdelta;
                in = ptr[1] + s->xdelta;
                for (j = 0; j < s->xdelta; j++) {
                    uint32_t diff = (*(ptr[0]++) << 8) | *(ptr[1]++);

                    pixel += diff;
                    bytestream_put_le16(&out, pixel);
                }
                break;
            default:
                av_assert1(0);
            }
        }
    }

    return 0;
}

static int decode_block(AVCodecContext *avctx, void *tdata,
                        int jobnr, int threadnr)
{
    EXRContext *s = avctx->priv_data;
    AVFrame *const p = s->picture;
    EXRThreadData *td = &s->thread_data[threadnr];
    const uint8_t *channel_buffer[4] = { 0 };
    const uint8_t *buf = s->buf;
    uint64_t line_offset, uncompressed_size;
    uint32_t xdelta = s->xdelta;
    uint16_t *ptr_x;
    uint8_t *ptr;
    int32_t data_size, line;
    const uint8_t *src;
    int axmax = (avctx->width - (s->xmax + 1)) * 2 * s->desc->nb_components;
    int bxmin = s->xmin * 2 * s->desc->nb_components;
    int i, x, buf_size = s->buf_size;
    int av_unused ret;

    line_offset = AV_RL64(s->table + jobnr * 8);
    // Check if the buffer has the required bytes needed from the offset
    if (line_offset > buf_size - 8)
        return AVERROR_INVALIDDATA;

    src = buf + line_offset + 8;
    line = AV_RL32(src - 8);
    if (line < s->ymin || line > s->ymax)
        return AVERROR_INVALIDDATA;

    data_size = AV_RL32(src - 4);
    if (data_size <= 0 || data_size > buf_size)
        return AVERROR_INVALIDDATA;

    s->ysize = FFMIN(s->scan_lines_per_block, s->ymax - line + 1);
    uncompressed_size = s->scan_line_size * s->ysize;
    if ((s->compr == EXR_RAW && (data_size != uncompressed_size ||
                                 line_offset > buf_size - uncompressed_size)) ||
        (s->compr != EXR_RAW && (data_size > uncompressed_size ||
                                 line_offset > buf_size - data_size))) {
        return AVERROR_INVALIDDATA;
    }

    if (data_size < uncompressed_size) {
        av_fast_padded_malloc(&td->uncompressed_data, &td->uncompressed_size, uncompressed_size);
        av_fast_padded_malloc(&td->tmp, &td->tmp_size, uncompressed_size);
        if (!td->uncompressed_data || !td->tmp)
            return AVERROR(ENOMEM);

        switch (s->compr) {
        case EXR_ZIP1:
        case EXR_ZIP16:
            ret = zip_uncompress(src, data_size, uncompressed_size, td);
            break;
        case EXR_PXR24:
            ret = pxr24_uncompress(s, src, data_size, uncompressed_size, td);
            break;
        case EXR_RLE:
            ret = rle_uncompress(src, data_size, uncompressed_size, td);
        }

        src = td->uncompressed_data;
    }

    channel_buffer[0] = src + xdelta * s->channel_offsets[0];
    channel_buffer[1] = src + xdelta * s->channel_offsets[1];
    channel_buffer[2] = src + xdelta * s->channel_offsets[2];
    if (s->channel_offsets[3] >= 0)
        channel_buffer[3] = src + xdelta * s->channel_offsets[3];

    ptr = p->data[0] + line * p->linesize[0];
    for (i = 0; i < s->scan_lines_per_block && line + i <= s->ymax; i++, ptr += p->linesize[0]) {
        const uint8_t *r, *g, *b, *a;

        r = channel_buffer[0];
        g = channel_buffer[1];
        b = channel_buffer[2];
        if (channel_buffer[3])
            a = channel_buffer[3];

        ptr_x = (uint16_t *)ptr;

        // Zero out the start if xmin is not 0
        memset(ptr_x, 0, bxmin);
        ptr_x += s->xmin * s->desc->nb_components;
        if (s->pixel_type == EXR_FLOAT) {
            // 32-bit
            for (x = 0; x < xdelta; x++) {
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&r));
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&g));
                *ptr_x++ = exr_flt2uint(bytestream_get_le32(&b));
                if (channel_buffer[3])
                    *ptr_x++ = exr_flt2uint(bytestream_get_le32(&a));
            }
        } else {
            // 16-bit
            for (x = 0; x < xdelta; x++) {
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&r));
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&g));
                *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&b));
                if (channel_buffer[3])
                    *ptr_x++ = exr_halflt2uint(bytestream_get_le16(&a));
            }
        }

        // Zero out the end if xmax+1 is not w
        memset(ptr_x, 0, axmax);

        channel_buffer[0] += s->scan_line_size;
        channel_buffer[1] += s->scan_line_size;
        channel_buffer[2] += s->scan_line_size;
        if (channel_buffer[3])
            channel_buffer[3] += s->scan_line_size;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data,
                        int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf      = avpkt->data;
    unsigned int   buf_size = avpkt->size;
    const uint8_t *buf_end  = buf + buf_size;

    EXRContext *const s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *picture  = data;
    uint8_t *ptr;

    int i, y, magic_number, version, flags, ret;
    int w = 0;
    int h = 0;

    int out_line_size;
    int scan_line_blocks;

    unsigned int current_channel_offset = 0;

    s->xmin = ~0;
    s->xmax = ~0;
    s->ymin = ~0;
    s->ymax = ~0;
    s->xdelta = ~0;
    s->ydelta = ~0;
    s->channel_offsets[0] = -1;
    s->channel_offsets[1] = -1;
    s->channel_offsets[2] = -1;
    s->channel_offsets[3] = -1;
    s->pixel_type = -1;
    s->nb_channels = 0;
    s->compr = -1;
    s->buf = buf;
    s->buf_size = buf_size;

    if (buf_size < 10) {
        av_log(avctx, AV_LOG_ERROR, "Too short header to parse\n");
        return AVERROR_INVALIDDATA;
    }

    magic_number = bytestream_get_le32(&buf);
    if (magic_number != 20000630) { // As per documentation of OpenEXR it's supposed to be int 20000630 little-endian
        av_log(avctx, AV_LOG_ERROR, "Wrong magic number %d\n", magic_number);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream_get_byte(&buf);
    if (version != 2) {
        avpriv_report_missing_feature(avctx, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    flags = bytestream_get_le24(&buf);
    if (flags & 0x2) {
        avpriv_report_missing_feature(avctx, "Tile support");
        return AVERROR_PATCHWELCOME;
    }

    // Parse the header
    while (buf < buf_end && buf[0]) {
        unsigned int variable_buffer_data_size;
        // Process the channel list
        if (check_header_variable(avctx, &buf, buf_end, "channels", "chlist", 38, &variable_buffer_data_size) >= 0) {
            const uint8_t *channel_list_end;
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            channel_list_end = buf + variable_buffer_data_size;
            while (channel_list_end - buf >= 19) {
                EXRChannel *channel;
                enum ExrPixelType current_pixel_type;
                int channel_index = -1;
                int xsub, ysub;

                if (!strcmp(buf, "R"))
                    channel_index = 0;
                else if (!strcmp(buf, "G"))
                    channel_index = 1;
                else if (!strcmp(buf, "B"))
                    channel_index = 2;
                else if (!strcmp(buf, "A"))
                    channel_index = 3;
                else
                    av_log(avctx, AV_LOG_WARNING, "Unsupported channel %.256s\n", buf);

                while (bytestream_get_byte(&buf) && buf < channel_list_end)
                    continue; /* skip */

                if (channel_list_end - * &buf < 4) {
                    av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
                    return AVERROR_INVALIDDATA;
                }

                current_pixel_type = bytestream_get_le32(&buf);
                if (current_pixel_type > 2) {
                    av_log(avctx, AV_LOG_ERROR, "Unknown pixel type\n");
                    return AVERROR_INVALIDDATA;
                }

                buf += 4;
                xsub = bytestream_get_le32(&buf);
                ysub = bytestream_get_le32(&buf);
                if (xsub != 1 || ysub != 1) {
                    avpriv_report_missing_feature(avctx, "Subsampling %dx%d", xsub, ysub);
                    return AVERROR_PATCHWELCOME;
                }

                if (channel_index >= 0) {
                    if (s->pixel_type != -1 && s->pixel_type != current_pixel_type) {
                        av_log(avctx, AV_LOG_ERROR, "RGB channels not of the same depth\n");
                        return AVERROR_INVALIDDATA;
                    }
                    s->pixel_type = current_pixel_type;
                    s->channel_offsets[channel_index] = current_channel_offset;
                }

                s->channels = av_realloc_f(s->channels, ++s->nb_channels, sizeof(EXRChannel));
                if (!s->channels)
                    return AVERROR(ENOMEM);
                channel = &s->channels[s->nb_channels - 1];
                channel->pixel_type = current_pixel_type;
                channel->xsub = xsub;
                channel->ysub = ysub;

                current_channel_offset += 1 << current_pixel_type;
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
                return AVERROR_INVALIDDATA;
            }

            buf = channel_list_end;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "dataWindow", "box2i", 31, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            s->xmin = AV_RL32(buf);
            s->ymin = AV_RL32(buf + 4);
            s->xmax = AV_RL32(buf + 8);
            s->ymax = AV_RL32(buf + 12);
            s->xdelta = (s->xmax - s->xmin) + 1;
            s->ydelta = (s->ymax - s->ymin) + 1;

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "displayWindow", "box2i", 34, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            w = AV_RL32(buf + 8) + 1;
            h = AV_RL32(buf + 12) + 1;

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "lineOrder", "lineOrder", 25, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            av_log(avctx, AV_LOG_DEBUG, "line order : %d\n", *buf);
            if (*buf > 2) {
                av_log(avctx, AV_LOG_ERROR, "Unknown line order\n");
                return AVERROR_INVALIDDATA;
            }

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "pixelAspectRatio", "float", 31, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            avctx->sample_aspect_ratio = av_d2q(av_int2float(AV_RL32(buf)), 255);

            buf += variable_buffer_data_size;
            continue;
        } else if (check_header_variable(avctx, &buf, buf_end, "compression", "compression", 29, &variable_buffer_data_size) >= 0) {
            if (!variable_buffer_data_size)
                return AVERROR_INVALIDDATA;

            if (s->compr == -1)
                s->compr = *buf;
            else
                av_log(avctx, AV_LOG_WARNING, "Found more than one compression attribute\n");

            buf += variable_buffer_data_size;
            continue;
        }

        // Check if there is enough bytes for a header
        if (buf_end - buf <= 9) {
            av_log(avctx, AV_LOG_ERROR, "Incomplete header\n");
            return AVERROR_INVALIDDATA;
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
                return AVERROR_INVALIDDATA;
            }
            buf += variable_buffer_data_size;
        }
    }

    if (s->compr == -1) {
        av_log(avctx, AV_LOG_ERROR, "Missing compression attribute\n");
        return AVERROR_INVALIDDATA;
    }

    if (buf >= buf_end) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete frame\n");
        return AVERROR_INVALIDDATA;
    }
    buf++;

    switch (s->pixel_type) {
    case EXR_FLOAT:
    case EXR_HALF:
        if (s->channel_offsets[3] >= 0)
            avctx->pix_fmt = AV_PIX_FMT_RGBA64;
        else
            avctx->pix_fmt = AV_PIX_FMT_RGB48;
        break;
    case EXR_UINT:
        avpriv_request_sample(avctx, "32-bit unsigned int");
        return AVERROR_PATCHWELCOME;
    default:
        av_log(avctx, AV_LOG_ERROR, "Missing channel list\n");
        return AVERROR_INVALIDDATA;
    }

    switch (s->compr) {
    case EXR_RAW:
    case EXR_RLE:
    case EXR_ZIP1:
        s->scan_lines_per_block = 1;
        break;
    case EXR_PXR24:
    case EXR_ZIP16:
        s->scan_lines_per_block = 16;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Compression %d", s->compr);
        return AVERROR_PATCHWELCOME;
    }

    if (av_image_check_size(w, h, 0, avctx))
        return AVERROR_INVALIDDATA;

    // Verify the xmin, xmax, ymin, ymax and xdelta before setting the actual image size
    if (s->xmin > s->xmax ||
        s->ymin > s->ymax ||
        s->xdelta != s->xmax - s->xmin + 1 ||
        s->xmax >= w || s->ymax >= h) {
        av_log(avctx, AV_LOG_ERROR, "Wrong sizing or missing size information\n");
        return AVERROR_INVALIDDATA;
    }

    if (w != avctx->width || h != avctx->height) {
        avcodec_set_dimensions(avctx, w, h);
    }

    s->desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    out_line_size = avctx->width * 2 * s->desc->nb_components;
    s->scan_line_size = s->xdelta * current_channel_offset;
    scan_line_blocks = (s->ydelta + s->scan_lines_per_block - 1) / s->scan_lines_per_block;

    if (s->compr != EXR_RAW) {
        size_t thread_data_size, prev_size;
        EXRThreadData *m;

        prev_size = s->thread_data_size;
        if (av_size_mult(avctx->thread_count, sizeof(EXRThreadData), &thread_data_size))
            return AVERROR(EINVAL);

        m = av_fast_realloc(s->thread_data, &s->thread_data_size, thread_data_size);
        if (!m)
            return AVERROR(ENOMEM);
        s->thread_data = m;
        memset(s->thread_data + prev_size, 0, s->thread_data_size - prev_size);
    }

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    if (buf_end - buf < scan_line_blocks * 8)
        return AVERROR_INVALIDDATA;
    s->table = buf;
    ptr = picture->data[0];

    // Zero out the start if ymin is not 0
    for (y = 0; y < s->ymin; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    s->picture = picture;
    avctx->execute2(avctx, decode_block, s->thread_data, NULL, scan_line_blocks);

    // Zero out the end if ymax+1 is not h
    for (y = s->ymax + 1; y < avctx->height; y++) {
        memset(ptr, 0, out_line_size);
        ptr += picture->linesize[0];
    }

    picture->pict_type = AV_PICTURE_TYPE_I;
    *got_frame = 1;

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    EXRContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->thread_data_size / sizeof(EXRThreadData); i++) {
        EXRThreadData *td = &s->thread_data[i];
        av_free(td->uncompressed_data);
        av_free(td->tmp);
    }

    av_freep(&s->thread_data);
    s->thread_data_size = 0;
    av_freep(&s->channels);

    return 0;
}

AVCodec ff_exr_decoder = {
    .name               = "exr",
    .long_name          = NULL_IF_CONFIG_SMALL("OpenEXR image"),
    .type               = AVMEDIA_TYPE_VIDEO,
    .id                 = AV_CODEC_ID_EXR,
    .priv_data_size     = sizeof(EXRContext),
    .close              = decode_end,
    .decode             = decode_frame,
    .capabilities       = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS,
};
