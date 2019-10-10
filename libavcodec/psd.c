/*
 * Photoshop (PSD) image decoder
 * Copyright (c) 2016 Jokyo Images
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

#include "bytestream.h"
#include "internal.h"

enum PsdCompr {
    PSD_RAW,
    PSD_RLE,
    PSD_ZIP_WITHOUT_P,
    PSD_ZIP_WITH_P,
};

enum PsdColorMode {
    PSD_BITMAP,
    PSD_GRAYSCALE,
    PSD_INDEXED,
    PSD_RGB,
    PSD_CMYK,
    PSD_MULTICHANNEL,
    PSD_DUOTONE,
    PSD_LAB,
};

typedef struct PSDContext {
    AVClass *class;
    AVFrame *picture;
    AVCodecContext *avctx;
    GetByteContext gb;

    uint8_t * tmp;

    uint16_t channel_count;
    uint16_t channel_depth;

    uint64_t uncompressed_size;
    unsigned int pixel_size;/* 1 for 8 bits, 2 for 16 bits */
    uint64_t line_size;/* length of src data (even width) */

    int width;
    int height;

    enum PsdCompr compression;
    enum PsdColorMode color_mode;

    uint8_t palette[AVPALETTE_SIZE];
} PSDContext;

static int decode_header(PSDContext * s)
{
    int signature, version, color_mode;
    int64_t len_section;
    int ret = 0;

    if (bytestream2_get_bytes_left(&s->gb) < 30) {/* File header section + color map data section length */
        av_log(s->avctx, AV_LOG_ERROR, "Header too short to parse.\n");
        return AVERROR_INVALIDDATA;
    }

    signature = bytestream2_get_le32(&s->gb);
    if (signature != MKTAG('8','B','P','S')) {
        av_log(s->avctx, AV_LOG_ERROR, "Wrong signature %d.\n", signature);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream2_get_be16(&s->gb);
    if (version != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Wrong version %d.\n", version);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(&s->gb, 6);/* reserved */

    s->channel_count = bytestream2_get_be16(&s->gb);
    if ((s->channel_count < 1) || (s->channel_count > 56)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid channel count %d.\n", s->channel_count);
        return AVERROR_INVALIDDATA;
    }

    s->height = bytestream2_get_be32(&s->gb);

    if ((s->height > 30000) && (s->avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL)) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Height > 30000 is experimental, add "
               "'-strict %d' if you want to try to decode the picture.\n",
               FF_COMPLIANCE_EXPERIMENTAL);
        return AVERROR_EXPERIMENTAL;
    }

    s->width = bytestream2_get_be32(&s->gb);
    if ((s->width > 30000) && (s->avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL)) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Width > 30000 is experimental, add "
               "'-strict %d' if you want to try to decode the picture.\n",
               FF_COMPLIANCE_EXPERIMENTAL);
        return AVERROR_EXPERIMENTAL;
    }

    if ((ret = ff_set_dimensions(s->avctx, s->width, s->height)) < 0)
        return ret;

    s->channel_depth = bytestream2_get_be16(&s->gb);

    color_mode = bytestream2_get_be16(&s->gb);
    switch (color_mode) {
    case 0:
        s->color_mode = PSD_BITMAP;
        break;
    case 1:
        s->color_mode = PSD_GRAYSCALE;
        break;
    case 2:
        s->color_mode = PSD_INDEXED;
        break;
    case 3:
        s->color_mode = PSD_RGB;
        break;
    case 4:
        s->color_mode = PSD_CMYK;
        break;
    case 7:
        s->color_mode = PSD_MULTICHANNEL;
        break;
    case 8:
        s->color_mode = PSD_DUOTONE;
        break;
    case 9:
        s->color_mode = PSD_LAB;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "Unknown color mode %d.\n", color_mode);
        return AVERROR_INVALIDDATA;
    }

    /* color map data */
    len_section = bytestream2_get_be32(&s->gb);
    if (len_section < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Negative size for color map data section.\n");
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&s->gb) < (len_section + 4)) { /* section and len next section */
        av_log(s->avctx, AV_LOG_ERROR, "Incomplete file.\n");
        return AVERROR_INVALIDDATA;
    }
    if (len_section) {
        int i,j;
        memset(s->palette, 0xff, AVPALETTE_SIZE);
        for (j = HAVE_BIGENDIAN; j < 3 + HAVE_BIGENDIAN; j++)
            for (i = 0; i < FFMIN(256, len_section / 3); i++)
                s->palette[i * 4 + (HAVE_BIGENDIAN ? j : 2 - j)] = bytestream2_get_byteu(&s->gb);
        len_section -= i * 3;
    }
    bytestream2_skip(&s->gb, len_section);

    /* image ressources */
    len_section = bytestream2_get_be32(&s->gb);
    if (len_section < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Negative size for image ressources section.\n");
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&s->gb) < (len_section + 4)) { /* section and len next section */
        av_log(s->avctx, AV_LOG_ERROR, "Incomplete file.\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&s->gb, len_section);

    /* layers and masks */
    len_section = bytestream2_get_be32(&s->gb);
    if (len_section < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Negative size for layers and masks data section.\n");
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&s->gb) < len_section) {
        av_log(s->avctx, AV_LOG_ERROR, "Incomplete file.\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&s->gb, len_section);

    /* image section */
    if (bytestream2_get_bytes_left(&s->gb) < 2) {
        av_log(s->avctx, AV_LOG_ERROR, "File without image data section.\n");
        return AVERROR_INVALIDDATA;
    }

    s->compression = bytestream2_get_be16(&s->gb);
    switch (s->compression) {
    case 0:
    case 1:
        break;
    case 2:
        avpriv_request_sample(s->avctx, "ZIP without predictor compression");
        return AVERROR_PATCHWELCOME;
    case 3:
        avpriv_request_sample(s->avctx, "ZIP with predictor compression");
        return AVERROR_PATCHWELCOME;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "Unknown compression %d.\n", s->compression);
        return AVERROR_INVALIDDATA;
    }

    return ret;
}

static int decode_rle(PSDContext * s){
    unsigned int scanline_count;
    unsigned int sl, count;
    unsigned long target_index = 0;
    unsigned int p;
    int8_t rle_char;
    unsigned int repeat_count;
    uint8_t v;

    scanline_count = s->height * s->channel_count;

    /* scanline table */
    if (bytestream2_get_bytes_left(&s->gb) < scanline_count * 2) {
        av_log(s->avctx, AV_LOG_ERROR, "Not enough data for rle scanline table.\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&s->gb, scanline_count * 2);/* size of each scanline */

    /* decode rle data scanline by scanline */
    for (sl = 0; sl < scanline_count; sl++) {
        count = 0;

        while (count < s->line_size) {
            rle_char = bytestream2_get_byte(&s->gb);

            if (rle_char <= 0) {/* byte repeat */
                repeat_count = rle_char * -1;

                if (bytestream2_get_bytes_left(&s->gb) < 1) {
                    av_log(s->avctx, AV_LOG_ERROR, "Not enough data for rle scanline.\n");
                    return AVERROR_INVALIDDATA;
                }

                if (target_index + repeat_count >= s->uncompressed_size) {
                    av_log(s->avctx, AV_LOG_ERROR, "Invalid rle char.\n");
                    return AVERROR_INVALIDDATA;
                }

                v = bytestream2_get_byte(&s->gb);
                for (p = 0; p <= repeat_count; p++) {
                    s->tmp[target_index++] = v;
                }
                count += repeat_count + 1;
            } else {
                if (bytestream2_get_bytes_left(&s->gb) < rle_char) {
                    av_log(s->avctx, AV_LOG_ERROR, "Not enough data for rle scanline.\n");
                    return AVERROR_INVALIDDATA;
                }

                if (target_index + rle_char >= s->uncompressed_size) {
                    av_log(s->avctx, AV_LOG_ERROR, "Invalid rle char.\n");
                    return AVERROR_INVALIDDATA;
                }

                for (p = 0; p <= rle_char; p++) {
                    v = bytestream2_get_byte(&s->gb);
                    s->tmp[target_index++] = v;
                }
                count += rle_char + 1;
            }
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    int ret;
    uint8_t *ptr;
    const uint8_t *ptr_data;
    int index_out, c, y, x, p;
    uint8_t eq_channel[4] = {2,0,1,3};/* RGBA -> GBRA channel order */
    uint8_t plane_number;

    AVFrame *picture = data;

    PSDContext *s = avctx->priv_data;
    s->avctx     = avctx;
    s->channel_count = 0;
    s->channel_depth = 0;
    s->tmp           = NULL;
    s->line_size     = 0;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    if ((ret = decode_header(s)) < 0)
        return ret;

    s->pixel_size = s->channel_depth >> 3;/* in byte */
    s->line_size = s->width * s->pixel_size;

    switch (s->color_mode) {
    case PSD_BITMAP:
        if (s->channel_depth != 1 || s->channel_count != 1) {
            av_log(s->avctx, AV_LOG_ERROR,
                    "Invalid bitmap file (channel_depth %d, channel_count %d)\n",
                    s->channel_depth, s->channel_count);
            return AVERROR_INVALIDDATA;
        }
        s->line_size = s->width + 7 >> 3;
        avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
        break;
    case PSD_INDEXED:
        if (s->channel_depth != 8 || s->channel_count != 1) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid indexed file (channel_depth %d, channel_count %d)\n",
                   s->channel_depth, s->channel_count);
            return AVERROR_INVALIDDATA;
        }
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case PSD_CMYK:
        if (s->channel_count == 4) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP16BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for cmyk", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else if (s->channel_count == 5) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP16BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for cmyk", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else {
            avpriv_report_missing_feature(avctx, "channel count %d for cmyk", s->channel_count);
            return AVERROR_PATCHWELCOME;
        }
        break;
    case PSD_RGB:
        if (s->channel_count == 3) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_GBRP16BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for rgb", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else if (s->channel_count == 4) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_GBRAP16BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for rgb", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else {
            avpriv_report_missing_feature(avctx, "channel count %d for rgb", s->channel_count);
            return AVERROR_PATCHWELCOME;
        }
        break;
    case PSD_DUOTONE:
        av_log(avctx, AV_LOG_WARNING, "ignoring unknown duotone specification.\n");
    case PSD_GRAYSCALE:
        if (s->channel_count == 1) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_GRAY8;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_GRAY16BE;
            } else if (s->channel_depth == 32) {
                avctx->pix_fmt = AV_PIX_FMT_GRAYF32BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for grayscale", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else if (s->channel_count == 2) {
            if (s->channel_depth == 8) {
                avctx->pix_fmt = AV_PIX_FMT_YA8;
            } else if (s->channel_depth == 16) {
                avctx->pix_fmt = AV_PIX_FMT_YA16BE;
            } else {
                avpriv_report_missing_feature(avctx, "channel depth %d for grayscale", s->channel_depth);
                return AVERROR_PATCHWELCOME;
            }
        } else {
            avpriv_report_missing_feature(avctx, "channel count %d for grayscale", s->channel_count);
            return AVERROR_PATCHWELCOME;
        }
        break;
    default:
        avpriv_report_missing_feature(avctx, "color mode %d", s->color_mode);
        return AVERROR_PATCHWELCOME;
    }

    s->uncompressed_size = s->line_size * s->height * s->channel_count;

    if ((ret = ff_get_buffer(avctx, picture, 0)) < 0)
        return ret;

    /* decode picture if need */
    if (s->compression == PSD_RLE) {
        s->tmp = av_malloc(s->uncompressed_size);
        if (!s->tmp)
            return AVERROR(ENOMEM);

        ret = decode_rle(s);

        if (ret < 0) {
            av_freep(&s->tmp);
            return ret;
        }

        ptr_data = s->tmp;
    } else {
        if (bytestream2_get_bytes_left(&s->gb) < s->uncompressed_size) {
            av_log(s->avctx, AV_LOG_ERROR, "Not enough data for raw image data section.\n");
            return AVERROR_INVALIDDATA;
        }
        ptr_data = s->gb.buffer;
    }

    /* Store data */
    if ((avctx->pix_fmt == AV_PIX_FMT_YA8)||(avctx->pix_fmt == AV_PIX_FMT_YA16BE)){/* Interleaved */
        ptr = picture->data[0];
        for (c = 0; c < s->channel_count; c++) {
            for (y = 0; y < s->height; y++) {
                for (x = 0; x < s->width; x++) {
                    index_out = y * picture->linesize[0] + x * s->channel_count * s->pixel_size + c * s->pixel_size;
                    for (p = 0; p < s->pixel_size; p++) {
                        ptr[index_out + p] = *ptr_data;
                        ptr_data ++;
                    }
                }
            }
        }
    } else if (s->color_mode == PSD_CMYK) {
        uint8_t *dst[4] = { picture->data[0], picture->data[1], picture->data[2], picture->data[3] };
        const uint8_t *src[5] = { ptr_data };
        src[1] = src[0] + s->line_size * s->height;
        src[2] = src[1] + s->line_size * s->height;
        src[3] = src[2] + s->line_size * s->height;
        src[4] = src[3] + s->line_size * s->height;
        if (s->channel_depth == 8) {
            for (y = 0; y < s->height; y++) {
                for (x = 0; x < s->width; x++) {
                    int k = src[3][x];
                    int r = src[0][x] * k;
                    int g = src[1][x] * k;
                    int b = src[2][x] * k;
                    dst[0][x] = g * 257 >> 16;
                    dst[1][x] = b * 257 >> 16;
                    dst[2][x] = r * 257 >> 16;
                }
                dst[0] += picture->linesize[0];
                dst[1] += picture->linesize[1];
                dst[2] += picture->linesize[2];
                src[0] += s->line_size;
                src[1] += s->line_size;
                src[2] += s->line_size;
                src[3] += s->line_size;
            }
            if (avctx->pix_fmt == AV_PIX_FMT_GBRAP) {
                for (y = 0; y < s->height; y++) {
                    memcpy(dst[3], src[4], s->line_size);
                    src[4] += s->line_size;
                    dst[3] += picture->linesize[3];
                }
            }
        } else {
            for (y = 0; y < s->height; y++) {
                for (x = 0; x < s->width; x++) {
                    int64_t k = AV_RB16(&src[3][x * 2]);
                    int64_t r = AV_RB16(&src[0][x * 2]) * k;
                    int64_t g = AV_RB16(&src[1][x * 2]) * k;
                    int64_t b = AV_RB16(&src[2][x * 2]) * k;
                    AV_WB16(&dst[0][x * 2], g * 65537 >> 32);
                    AV_WB16(&dst[1][x * 2], b * 65537 >> 32);
                    AV_WB16(&dst[2][x * 2], r * 65537 >> 32);
                }
                dst[0] += picture->linesize[0];
                dst[1] += picture->linesize[1];
                dst[2] += picture->linesize[2];
                src[0] += s->line_size;
                src[1] += s->line_size;
                src[2] += s->line_size;
                src[3] += s->line_size;
            }
            if (avctx->pix_fmt == AV_PIX_FMT_GBRAP16BE) {
                for (y = 0; y < s->height; y++) {
                    memcpy(dst[3], src[4], s->line_size);
                    src[4] += s->line_size;
                    dst[3] += picture->linesize[3];
                }
            }
        }
    } else {/* Planar */
        if (s->channel_count == 1)/* gray 8 or gray 16be */
            eq_channel[0] = 0;/* assign first channel, to first plane */

        for (c = 0; c < s->channel_count; c++) {
            plane_number = eq_channel[c];
            ptr = picture->data[plane_number];/* get the right plane */
            for (y = 0; y < s->height; y++) {
                memcpy(ptr, ptr_data, s->line_size);
                ptr += picture->linesize[plane_number];
                ptr_data += s->line_size;
            }
        }
    }

    if (s->color_mode == PSD_INDEXED) {
        picture->palette_has_changed = 1;
        memcpy(picture->data[1], s->palette, AVPALETTE_SIZE);
    }

    av_freep(&s->tmp);

    picture->pict_type = AV_PICTURE_TYPE_I;
    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_psd_decoder = {
    .name             = "psd",
    .long_name        = NULL_IF_CONFIG_SMALL("Photoshop PSD file"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_PSD,
    .priv_data_size   = sizeof(PSDContext),
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
