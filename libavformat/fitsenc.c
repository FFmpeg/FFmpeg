/*
 * FITS muxer
 * Copyright (c) 2017 Paras Chadha
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
 * FITS muxer.
 */

#include "internal.h"

typedef struct FITSContext {
    int first_image;
} FITSContext;

static int fits_write_header(AVFormatContext *s)
{
    FITSContext *fitsctx = s->priv_data;
    fitsctx->first_image = 1;
    return 0;
}

/**
 * Write one header line comprising of keyword and value(int)
 * @param s AVFormat Context
 * @param keyword pointer to the char array in which keyword is stored
 * @param value the value corresponding to the keyword
 * @param lines_written to keep track of lines written so far
 * @return 0
 */
static int write_keyword_value(AVFormatContext *s, const char *fmt,
                               const char *keyword, void *value, int *lines_written)
{
    int len, ret;
    uint8_t header[80];

    len = strlen(keyword);
    memset(header, ' ', sizeof(header));
    memcpy(header, keyword, len);

    header[8] = '=';
    header[9] = ' ';

    if (!strcmp(fmt, "%d")) {
        ret = snprintf(header + 10, 70, fmt, *(int *)value);
    } else {
        ret = snprintf(header + 10, 70, fmt, *(float *)value);
    }

    memset(&header[ret + 10], ' ', sizeof(header) - (ret + 10));

    avio_write(s->pb, header, sizeof(header));
    *lines_written += 1;
    return 0;
}

static int write_image_header(AVFormatContext *s)
{
    AVStream *st = s->streams[0];
    AVCodecParameters *encctx = st->codecpar;
    FITSContext *fitsctx = s->priv_data;
    uint8_t buffer[80];
    int bitpix, naxis, naxis3 = 1, bzero = 0, rgb = 0, lines_written = 0, lines_left;
    int pcount = 0, gcount = 1;
    float datamax, datamin;

    switch (encctx->format) {
        case AV_PIX_FMT_GRAY8:
            bitpix = 8;
            naxis = 2;
            datamin = 0;
            datamax = 255;
            break;
        case AV_PIX_FMT_GRAY16BE:
            bitpix = 16;
            naxis = 2;
            bzero = 32768;
            datamin = 0;
            datamax = 65535;
            break;
        case AV_PIX_FMT_GBRP:
        case AV_PIX_FMT_GBRAP:
            bitpix = 8;
            naxis = 3;
            rgb = 1;
            if (encctx->format == AV_PIX_FMT_GBRP) {
                naxis3 = 3;
            } else {
                naxis3 = 4;
            }
            datamin = 0;
            datamax = 255;
            break;
        case AV_PIX_FMT_GBRP16BE:
        case AV_PIX_FMT_GBRAP16BE:
            bitpix = 16;
            naxis = 3;
            rgb = 1;
            if (encctx->format == AV_PIX_FMT_GBRP16BE) {
                naxis3 = 3;
            } else {
                naxis3 = 4;
            }
            bzero = 32768;
            datamin = 0;
            datamax = 65535;
            break;
        default:
            return AVERROR(EINVAL);
    }

    if (fitsctx->first_image) {
        memcpy(buffer, "SIMPLE  = ", 10);
        memset(buffer + 10, ' ', 70);
        buffer[29] = 'T';
        avio_write(s->pb, buffer, sizeof(buffer));
    } else {
        memcpy(buffer, "XTENSION= 'IMAGE   '", 20);
        memset(buffer + 20, ' ', 60);
        avio_write(s->pb, buffer, sizeof(buffer));
    }
    lines_written++;

    write_keyword_value(s, "%d", "BITPIX", &bitpix, &lines_written);         // no of bits per pixel
    write_keyword_value(s, "%d", "NAXIS", &naxis, &lines_written);           // no of dimensions of image
    write_keyword_value(s, "%d", "NAXIS1", &encctx->width, &lines_written);   // first dimension i.e. width
    write_keyword_value(s, "%d", "NAXIS2", &encctx->height, &lines_written);  // second dimension i.e. height

    if (rgb)
        write_keyword_value(s, "%d", "NAXIS3", &naxis3, &lines_written);     // third dimension to store RGBA planes

    if (!fitsctx->first_image) {
        write_keyword_value(s, "%d", "PCOUNT", &pcount, &lines_written);
        write_keyword_value(s, "%d", "GCOUNT", &gcount, &lines_written);
    } else {
        fitsctx->first_image = 0;
    }

    write_keyword_value(s, "%g", "DATAMIN", &datamin, &lines_written);
    write_keyword_value(s, "%g", "DATAMAX", &datamax, &lines_written);

    /*
     * Since FITS does not support unsigned 16 bit integers,
     * BZERO = 32768 is used to store unsigned 16 bit integers as
     * signed integers so that it can be read properly.
     */
    if (bitpix == 16)
        write_keyword_value(s, "%d", "BZERO", &bzero, &lines_written);

    if (rgb) {
        memcpy(buffer, "CTYPE3  = 'RGB     '", 20);
        memset(buffer + 20, ' ', 60);
        avio_write(s->pb, buffer, sizeof(buffer));
        lines_written++;
    }

    memcpy(buffer, "END", 3);
    memset(buffer + 3, ' ', 77);
    avio_write(s->pb, buffer, sizeof(buffer));
    lines_written++;

    lines_left = ((lines_written + 35) / 36) * 36 - lines_written;
    memset(buffer, ' ', 80);
    while (lines_left > 0) {
        avio_write(s->pb, buffer, sizeof(buffer));
        lines_left--;
    }
    return 0;
}

static int fits_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = write_image_header(s);
    if (ret < 0)
        return ret;
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

const AVOutputFormat ff_fits_muxer = {
    .name         = "fits",
    .long_name    = NULL_IF_CONFIG_SMALL("Flexible Image Transport System"),
    .extensions   = "fits",
    .priv_data_size = sizeof(FITSContext),
    .audio_codec  = AV_CODEC_ID_NONE,
    .video_codec  = AV_CODEC_ID_FITS,
    .write_header = fits_write_header,
    .write_packet = fits_write_packet,
};
