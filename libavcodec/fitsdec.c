/*
 * FITS image decoder
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
 * FITS image decoder
 *
 * Specification: https://fits.gsfc.nasa.gov/fits_standard.html Version 3.0
 *
 * Support all 2d images alongwith, bzero, bscale and blank keywords.
 * RGBA images are supported as NAXIS3 = 3 or 4 i.e. Planes in RGBA order. Also CTYPE = 'RGB ' should be present.
 * Also to interpret data, values are linearly scaled using min-max scaling but not RGB images.
 */

#include "avcodec.h"
#include "internal.h"
#include <float.h>
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "fits.h"

typedef struct FITSContext {
    const AVClass *class;
    int blank_val;
} FITSContext;

/**
 * Calculate the data_min and data_max values from the data.
 * This is called if the values are not present in the header.
 * @param ptr8 pointer to the data
 * @param header pointer to the header
 * @param end pointer to end of packet
 * @return 0 if calculated successfully otherwise AVERROR_INVALIDDATA
 */
static int fill_data_min_max(const uint8_t *ptr8, FITSHeader *header, const uint8_t *end)
{
    uint8_t t8;
    int16_t t16;
    int32_t t32;
    int64_t t64;
    float tflt;
    double tdbl;
    int i, j;

    header->data_min = DBL_MAX;
    header->data_max = -DBL_MAX;
    switch (header->bitpix) {
#define CASE_N(a, t, rd) \
    case a: \
        for (i = 0; i < header->naxisn[1]; i++) { \
            for (j = 0; j < header->naxisn[0]; j++) { \
                t = rd; \
                if (!header->blank_found || t != header->blank) { \
                    if (t > header->data_max) \
                        header->data_max = t; \
                    if (t < header->data_min) \
                        header->data_min = t; \
                } \
                ptr8 += abs(a) >> 3; \
            } \
        } \
        break

        CASE_N(-64, tdbl, av_int2double(AV_RB64(ptr8)));
        CASE_N(-32, tflt, av_int2float(AV_RB32(ptr8)));
        CASE_N(8, t8, ptr8[0]);
        CASE_N(16, t16, AV_RB16(ptr8));
        CASE_N(32, t32, AV_RB32(ptr8));
        CASE_N(64, t64, AV_RB64(ptr8));
        default:
            return AVERROR_INVALIDDATA;
    }
    return 0;
}

/**
 * Read the fits header and store the values in FITSHeader pointed by header
 * @param avctx AVCodec context
 * @param ptr pointer to pointer to the data
 * @param header pointer to the FITSHeader
 * @param end pointer to end of packet
 * @param metadata pointer to pointer to AVDictionary to store metadata
 * @return 0 if calculated successfully otherwise AVERROR_INVALIDDATA
 */
static int fits_read_header(AVCodecContext *avctx, const uint8_t **ptr, FITSHeader *header,
                            const uint8_t *end, AVDictionary **metadata)
{
    const uint8_t *ptr8 = *ptr;
    int lines_read, bytes_left, i, ret;
    size_t size;

    lines_read = 1; // to account for first header line, SIMPLE or XTENSION which is not included in packet...
    avpriv_fits_header_init(header, STATE_BITPIX);
    do {
        if (end - ptr8 < 80)
            return AVERROR_INVALIDDATA;
        ret = avpriv_fits_header_parse_line(avctx, header, ptr8, &metadata);
        ptr8 += 80;
        lines_read++;
    } while (!ret);
    if (ret < 0)
        return ret;

    bytes_left = (((lines_read + 35) / 36) * 36 - lines_read) * 80;
    if (end - ptr8 < bytes_left)
        return AVERROR_INVALIDDATA;
    ptr8 += bytes_left;

    if (header->rgb && (header->naxis != 3 || (header->naxisn[2] != 3 && header->naxisn[2] != 4))) {
        av_log(avctx, AV_LOG_ERROR, "File contains RGB image but NAXIS = %d and NAXIS3 = %d\n", header->naxis, header->naxisn[2]);
        return AVERROR_INVALIDDATA;
    }

    if (!header->rgb && header->naxis != 2) {
        av_log(avctx, AV_LOG_ERROR, "unsupported number of dimensions, NAXIS = %d\n", header->naxis);
        return AVERROR_INVALIDDATA;
    }

    if (header->blank_found && (header->bitpix == -32 || header->bitpix == -64)) {
        av_log(avctx, AV_LOG_WARNING, "BLANK keyword found but BITPIX = %d\n. Ignoring BLANK", header->bitpix);
        header->blank_found = 0;
    }

    size = abs(header->bitpix) >> 3;
    for (i = 0; i < header->naxis; i++) {
        if (size == 0 || header->naxisn[i] > SIZE_MAX / size) {
            av_log(avctx, AV_LOG_ERROR, "unsupported size of FITS image");
            return AVERROR_INVALIDDATA;
        }
        size *= header->naxisn[i];
    }

    if (end - ptr8 < size)
        return AVERROR_INVALIDDATA;
    *ptr = ptr8;

    if (!header->rgb && (!header->data_min_found || !header->data_max_found)) {
        ret = fill_data_min_max(ptr8, header, end);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "invalid BITPIX, %d\n", header->bitpix);
            return ret;
        }
    } else {
        /*
         * instead of applying bscale and bzero to every element,
         * we can do inverse transformation on data_min and data_max
         */
        header->data_min = (header->data_min - header->bzero) / header->bscale;
        header->data_max = (header->data_max - header->bzero) / header->bscale;
    }
    if (!header->rgb && header->data_min >= header->data_max) {
        if (header->data_min > header->data_max) {
            av_log(avctx, AV_LOG_ERROR, "data min/max (%g %g) is invalid\n", header->data_min, header->data_max);
            return AVERROR_INVALIDDATA;
        }
        av_log(avctx, AV_LOG_WARNING, "data min/max indicates a blank image\n");
        header->data_max ++;
    }

    return 0;
}

static int fits_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    AVFrame *p=data;
    const uint8_t *ptr8 = avpkt->data, *end;
    uint8_t t8;
    int16_t t16;
    int32_t t32;
    int64_t t64;
    float   tflt;
    double  tdbl;
    int ret, i, j, k;
    const int map[] = {2, 0, 1, 3}; // mapping from GBRA -> RGBA as RGBA is to be stored in FITS file..
    uint8_t *dst8;
    uint16_t *dst16;
    uint64_t t;
    FITSHeader header;
    FITSContext * fitsctx = avctx->priv_data;

    end = ptr8 + avpkt->size;
    p->metadata = NULL;
    ret = fits_read_header(avctx, &ptr8, &header, end, &p->metadata);
    if (ret < 0)
        return ret;

    if (header.rgb) {
        if (header.bitpix == 8) {
            if (header.naxisn[2] == 3) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP;
            }
        } else if (header.bitpix == 16) {
            if (header.naxisn[2] == 3) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP16;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP16;
            }
        } else {
            av_log(avctx, AV_LOG_ERROR, "unsupported BITPIX = %d\n", header.bitpix);
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (header.bitpix == 8) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        } else {
            avctx->pix_fmt = AV_PIX_FMT_GRAY16;
        }
    }

    if ((ret = ff_set_dimensions(avctx, header.naxisn[0], header.naxisn[1])) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    /*
     * FITS stores images with bottom row first. Therefore we have
     * to fill the image from bottom to top.
     */
    if (header.rgb) {
        switch(header.bitpix) {
#define CASE_RGB(cas, dst, type, dref) \
    case cas: \
        for (k = 0; k < header.naxisn[2]; k++) { \
            for (i = 0; i < avctx->height; i++) { \
                dst = (type *) (p->data[map[k]] + (avctx->height - i - 1) * p->linesize[map[k]]); \
                for (j = 0; j < avctx->width; j++) { \
                    t32 = dref(ptr8); \
                    if (!header.blank_found || t32 != header.blank) { \
                        t = t32 * header.bscale + header.bzero; \
                    } else { \
                        t = fitsctx->blank_val; \
                    } \
                    *dst++ = (type) t; \
                    ptr8 += cas >> 3; \
                } \
            } \
        } \
        break

            CASE_RGB(8, dst8, uint8_t, *);
            CASE_RGB(16, dst16, uint16_t, AV_RB16);
        }
    } else {
        double scale = header.data_max - header.data_min;

        if (scale <= 0 || !isfinite(scale)) {
            scale = 1;
        }
        scale = 1/scale;

        switch (header.bitpix) {
#define CASE_GRAY(cas, dst, type, t, rd) \
    case cas: \
        for (i = 0; i < avctx->height; i++) { \
            dst = (type *) (p->data[0] + (avctx->height-i-1)* p->linesize[0]); \
            for (j = 0; j < avctx->width; j++) { \
                t = rd; \
                if (!header.blank_found || t != header.blank) { \
                    *dst++ = lrint(((t - header.data_min) * ((1 << (sizeof(type) * 8)) - 1)) * scale); \
                } else { \
                    *dst++ = fitsctx->blank_val; \
                } \
                ptr8 += abs(cas) >> 3; \
            } \
        } \
        break

            CASE_GRAY(-64, dst16, uint16_t, tdbl, av_int2double(AV_RB64(ptr8)));
            CASE_GRAY(-32, dst16, uint16_t, tflt, av_int2float(AV_RB32(ptr8)));
            CASE_GRAY(8, dst8, uint8_t, t8, ptr8[0]);
            CASE_GRAY(16, dst16, uint16_t, t16, AV_RB16(ptr8));
            CASE_GRAY(32, dst16, uint16_t, t32, AV_RB32(ptr8));
            CASE_GRAY(64, dst16, uint16_t, t64, AV_RB64(ptr8));
            default:
                av_log(avctx, AV_LOG_ERROR, "invalid BITPIX, %d\n", header.bitpix);
                return AVERROR_INVALIDDATA;
        }
    }

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame = 1;

    return avpkt->size;
}

static const AVOption fits_options[] = {
    { "blank_value", "value that is used to replace BLANK pixels in data array", offsetof(FITSContext, blank_val), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 65535, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM},
    { NULL },
};

static const AVClass fits_decoder_class = {
    .class_name = "FITS decoder",
    .item_name  = av_default_item_name,
    .option     = fits_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_fits_decoder = {
    .name           = "fits",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FITS,
    .priv_data_size = sizeof(FITSContext),
    .decode         = fits_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Flexible Image Transport System"),
    .priv_class     = &fits_decoder_class
};
