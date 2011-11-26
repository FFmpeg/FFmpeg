/*
 * MJPEG/AVI1 to JPEG/JFIF bitstream format filter
 * Copyright (c) 2010 Adrian Daerr and Nicolas George
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

/*
 * Adapted from mjpeg2jpeg.c, with original copyright:
 * Paris 2010 Adrian Daerr, public domain
 */

#include <string.h>
#include "avcodec.h"
#include "mjpeg.h"

static const uint8_t jpeg_header[] = {
    0xff, 0xd8,                     // SOI
    0xff, 0xe0,                     // APP0
    0x00, 0x10,                     // APP0 header size (including
                                    // this field, but excluding preceding)
    0x4a, 0x46, 0x49, 0x46, 0x00,   // ID string 'JFIF\0'
    0x01, 0x01,                     // version
    0x00,                           // bits per type
    0x00, 0x00,                     // X density
    0x00, 0x00,                     // Y density
    0x00,                           // X thumbnail size
    0x00,                           // Y thumbnail size
};

static const int dht_segment_size = 420;
static const uint8_t dht_segment_head[] = { 0xFF, 0xC4, 0x01, 0xA2, 0x00 };
static const uint8_t dht_segment_frag[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t *append(uint8_t *buf, const uint8_t *src, int size)
{
    memcpy(buf, src, size);
    return buf + size;
}

static uint8_t *append_dht_segment(uint8_t *buf)
{
    buf = append(buf, dht_segment_head, sizeof(dht_segment_head));
    buf = append(buf, ff_mjpeg_bits_dc_luminance + 1, 16);
    buf = append(buf, dht_segment_frag, sizeof(dht_segment_frag));
    buf = append(buf, ff_mjpeg_val_dc, 12);
    *(buf++) = 0x10;
    buf = append(buf, ff_mjpeg_bits_ac_luminance + 1, 16);
    buf = append(buf, ff_mjpeg_val_ac_luminance, 162);
    *(buf++) = 0x11;
    buf = append(buf, ff_mjpeg_bits_ac_chrominance + 1, 16);
    buf = append(buf, ff_mjpeg_val_ac_chrominance, 162);
    return buf;
}

static int mjpeg2jpeg_filter(AVBitStreamFilterContext *bsfc,
                             AVCodecContext *avctx, const char *args,
                             uint8_t **poutbuf, int *poutbuf_size,
                             const uint8_t *buf, int buf_size,
                             int keyframe)
{
    int input_skip, output_size;
    uint8_t *output, *out;

    if (buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "input is truncated\n");
        return AVERROR_INVALIDDATA;
    }
    if (memcmp("AVI1", buf + 6, 4)) {
        av_log(avctx, AV_LOG_ERROR, "input is not MJPEG/AVI1\n");
        return AVERROR_INVALIDDATA;
    }
    input_skip = (buf[4] << 8) + buf[5] + 4;
    if (buf_size < input_skip) {
        av_log(avctx, AV_LOG_ERROR, "input is truncated\n");
        return AVERROR_INVALIDDATA;
    }
    output_size = buf_size - input_skip +
                  sizeof(jpeg_header) + dht_segment_size;
    output = out = av_malloc(output_size);
    if (!output)
        return AVERROR(ENOMEM);
    out = append(out, jpeg_header, sizeof(jpeg_header));
    out = append_dht_segment(out);
    out = append(out, buf + input_skip, buf_size - input_skip);
    *poutbuf = output;
    *poutbuf_size = output_size;
    return 1;
}

AVBitStreamFilter ff_mjpeg2jpeg_bsf = {
    .name           = "mjpeg2jpeg",
    .filter         = mjpeg2jpeg_filter,
};
