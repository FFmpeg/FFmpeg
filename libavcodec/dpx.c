/*
 * DPX (.dpx) image decoder
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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/imgutils.h"
#include "libavutil/timecode.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

enum DPX_TRC {
    DPX_TRC_USER_DEFINED       = 0,
    DPX_TRC_PRINTING_DENSITY   = 1,
    DPX_TRC_LINEAR             = 2,
    DPX_TRC_LOGARITHMIC        = 3,
    DPX_TRC_UNSPECIFIED_VIDEO  = 4,
    DPX_TRC_SMPTE_274          = 5,
    DPX_TRC_ITU_R_709_4        = 6,
    DPX_TRC_ITU_R_601_625      = 7,
    DPX_TRC_ITU_R_601_525      = 8,
    DPX_TRC_SMPTE_170          = 9,
    DPX_TRC_ITU_R_624_4_PAL    = 10,
    DPX_TRC_Z_LINEAR           = 11,
    DPX_TRC_Z_HOMOGENEOUS      = 12,
};

enum DPX_COL_SPEC {
    DPX_COL_SPEC_USER_DEFINED       = 0,
    DPX_COL_SPEC_PRINTING_DENSITY   = 1,
    /* 2 = N/A */
    /* 3 = N/A */
    DPX_COL_SPEC_UNSPECIFIED_VIDEO  = 4,
    DPX_COL_SPEC_SMPTE_274          = 5,
    DPX_COL_SPEC_ITU_R_709_4        = 6,
    DPX_COL_SPEC_ITU_R_601_625      = 7,
    DPX_COL_SPEC_ITU_R_601_525      = 8,
    DPX_COL_SPEC_SMPTE_170          = 9,
    DPX_COL_SPEC_ITU_R_624_4_PAL    = 10,
    /* 11 = N/A */
    /* 12 = N/A */
};

static unsigned int read16(const uint8_t **ptr, int is_big)
{
    unsigned int temp;
    if (is_big) {
        temp = AV_RB16(*ptr);
    } else {
        temp = AV_RL16(*ptr);
    }
    *ptr += 2;
    return temp;
}

static unsigned int read32(const uint8_t **ptr, int is_big)
{
    unsigned int temp;
    if (is_big) {
        temp = AV_RB32(*ptr);
    } else {
        temp = AV_RL32(*ptr);
    }
    *ptr += 4;
    return temp;
}

static uint16_t read10in32_gray(const uint8_t **ptr, uint32_t *lbuf,
                                int *n_datum, int is_big, int shift)
{
    uint16_t temp;

    if (*n_datum)
        (*n_datum)--;
    else {
        *lbuf = read32(ptr, is_big);
        *n_datum = 2;
    }

    temp = *lbuf >> shift & 0x3FF;
    *lbuf = *lbuf >> 10;

    return temp;
}

static uint16_t read10in32(const uint8_t **ptr, uint32_t *lbuf,
                           int *n_datum, int is_big, int shift)
{
    if (*n_datum)
        (*n_datum)--;
    else {
        *lbuf = read32(ptr, is_big);
        *n_datum = 2;
    }

    *lbuf = *lbuf << 10 | *lbuf >> shift & 0x3FFFFF;

    return *lbuf & 0x3FF;
}

static uint16_t read12in32(const uint8_t **ptr, uint32_t *lbuf,
                           int *n_datum, int is_big)
{
    if (*n_datum)
        (*n_datum)--;
    else {
        *lbuf = read32(ptr, is_big);
        *n_datum = 7;
    }

    switch (*n_datum){
    case 7: return *lbuf & 0xFFF;
    case 6: return (*lbuf >> 12) & 0xFFF;
    case 5: {
            uint32_t c = *lbuf >> 24;
            *lbuf = read32(ptr, is_big);
            c |= *lbuf << 8;
            return c & 0xFFF;
            }
    case 4: return (*lbuf >> 4) & 0xFFF;
    case 3: return (*lbuf >> 16) & 0xFFF;
    case 2: {
            uint32_t c = *lbuf >> 28;
            *lbuf = read32(ptr, is_big);
            c |= *lbuf << 4;
            return c & 0xFFF;
            }
    case 1: return (*lbuf >> 8) & 0xFFF;
    default: return *lbuf >> 20;
    }
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    uint8_t *ptr[AV_NUM_DATA_POINTERS];
    uint32_t header_version, version = 0;
    char creator[101] = { 0 };
    char input_device[33] = { 0 };

    unsigned int offset;
    int magic_num, endian;
    int x, y, stride, i, j, ret;
    int w, h, bits_per_color, descriptor, elements, packing;
    int yuv, color_trc, color_spec;
    int encoding, need_align = 0, unpadded_10bit = 0;

    unsigned int rgbBuffer = 0;
    int n_datum = 0;

    if (avpkt->size <= 1634) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small for DPX header\n");
        return AVERROR_INVALIDDATA;
    }

    magic_num = AV_RB32(buf);
    buf += 4;

    /* Check if the files "magic number" is "SDPX" which means it uses
     * big-endian or XPDS which is for little-endian files */
    if (magic_num == AV_RL32("SDPX")) {
        endian = 0;
    } else if (magic_num == AV_RB32("SDPX")) {
        endian = 1;
    } else {
        av_log(avctx, AV_LOG_ERROR, "DPX marker not found\n");
        return AVERROR_INVALIDDATA;
    }

    offset = read32(&buf, endian);
    if (avpkt->size <= offset) {
        av_log(avctx, AV_LOG_ERROR, "Invalid data start offset\n");
        return AVERROR_INVALIDDATA;
    }

    header_version = read32(&buf, 0);
    if (header_version == MKTAG('V','1','.','0'))
        version = 1;
    if (header_version == MKTAG('V','2','.','0'))
        version = 2;
    if (!version)
        av_log(avctx, AV_LOG_WARNING, "Unknown header format version %s.\n",
               av_fourcc2str(header_version));

    // Check encryption
    buf = avpkt->data + 660;
    ret = read32(&buf, endian);
    if (ret != 0xFFFFFFFF) {
        avpriv_report_missing_feature(avctx, "Encryption");
        av_log(avctx, AV_LOG_WARNING, "The image is encrypted and may "
               "not properly decode.\n");
    }

    // Need to end in 0x304 offset from start of file
    buf = avpkt->data + 0x304;
    w = read32(&buf, endian);
    h = read32(&buf, endian);

    if ((ret = ff_set_dimensions(avctx, w, h)) < 0)
        return ret;

    // Need to end in 0x320 to read the descriptor
    buf += 20;
    descriptor = buf[0];
    color_trc = buf[1];
    color_spec = buf[2];

    // Need to end in 0x323 to read the bits per color
    buf += 3;
    avctx->bits_per_raw_sample =
    bits_per_color = buf[0];
    buf++;
    packing = read16(&buf, endian);
    encoding = read16(&buf, endian);

    if (encoding) {
        avpriv_report_missing_feature(avctx, "Encoding %d", encoding);
        return AVERROR_PATCHWELCOME;
    }

    if (bits_per_color > 31)
        return AVERROR_INVALIDDATA;

    buf += 820;
    avctx->sample_aspect_ratio.num = read32(&buf, endian);
    avctx->sample_aspect_ratio.den = read32(&buf, endian);
    if (avctx->sample_aspect_ratio.num > 0 && avctx->sample_aspect_ratio.den > 0)
        av_reduce(&avctx->sample_aspect_ratio.num, &avctx->sample_aspect_ratio.den,
                   avctx->sample_aspect_ratio.num,  avctx->sample_aspect_ratio.den,
                  0x10000);
    else
        avctx->sample_aspect_ratio = (AVRational){ 0, 1 };

    /* preferred frame rate from Motion-picture film header */
    if (offset >= 1724 + 4) {
        buf = avpkt->data + 1724;
        i = read32(&buf, endian);
        if(i && i != 0xFFFFFFFF) {
            AVRational q = av_d2q(av_int2float(i), 4096);
            if (q.num > 0 && q.den > 0)
                avctx->framerate = q;
        }
    }

    /* alternative frame rate from television header */
    if (offset >= 1940 + 4 &&
        !(avctx->framerate.num && avctx->framerate.den)) {
        buf = avpkt->data + 1940;
        i = read32(&buf, endian);
        if(i && i != 0xFFFFFFFF) {
            AVRational q = av_d2q(av_int2float(i), 4096);
            if (q.num > 0 && q.den > 0)
                avctx->framerate = q;
        }
    }

    /* SMPTE TC from television header */
    if (offset >= 1920 + 4) {
        uint32_t tc;
        uint32_t *tc_sd;
        char tcbuf[AV_TIMECODE_STR_SIZE];

        buf = avpkt->data + 1920;
        // read32 to native endian, av_bswap32 to opposite of native for
        // compatibility with av_timecode_make_smpte_tc_string2 etc
        tc = av_bswap32(read32(&buf, endian));

        if (i != 0xFFFFFFFF) {
            AVFrameSideData *tcside;
            ret = ff_frame_new_side_data(avctx, p, AV_FRAME_DATA_S12M_TIMECODE,
                                         sizeof(uint32_t) * 4, &tcside);
            if (ret < 0)
                return ret;

            if (tcside) {
                tc_sd = (uint32_t*)tcside->data;
                tc_sd[0] = 1;
                tc_sd[1] = tc;

                av_timecode_make_smpte_tc_string2(tcbuf, avctx->framerate,
                                                  tc_sd[1], 0, 0);
                av_dict_set(&p->metadata, "timecode", tcbuf, 0);
            }
        }
    }

    /* color range from television header */
    if (offset >= 1964 + 4) {
        buf = avpkt->data + 1952;
        i = read32(&buf, endian);

        buf = avpkt->data + 1964;
        j = read32(&buf, endian);

        if (i != 0xFFFFFFFF && j != 0xFFFFFFFF) {
            float minCV, maxCV;
            minCV = av_int2float(i);
            maxCV = av_int2float(j);
            if (bits_per_color >= 1 &&
                minCV == 0.0f && maxCV == ((1U<<bits_per_color) - 1)) {
                avctx->color_range = AVCOL_RANGE_JPEG;
            } else if (bits_per_color >= 8 &&
                       minCV == (1  <<(bits_per_color - 4)) &&
                       maxCV == (235<<(bits_per_color - 8))) {
                avctx->color_range = AVCOL_RANGE_MPEG;
            }
        }
    }

    switch (descriptor) {
    case 1:  // R
    case 2:  // G
    case 3:  // B
    case 4:  // A
    case 6:  // Y
        elements = 1;
        yuv = 1;
        break;
    case 50: // RGB
        elements = 3;
        yuv = 0;
        break;
    case 52: // ABGR
    case 51: // RGBA
        elements = 4;
        yuv = 0;
        break;
    case 100: // UYVY422
        elements = 2;
        yuv = 1;
        break;
    case 102: // UYV444
        elements = 3;
        yuv = 1;
        break;
    case 103: // UYVA4444
        elements = 4;
        yuv = 1;
        break;
    default:
        avpriv_report_missing_feature(avctx, "Descriptor %d", descriptor);
        return AVERROR_PATCHWELCOME;
    }

    switch (bits_per_color) {
    case 8:
        stride = avctx->width * elements;
        break;
    case 10:
        if (!packing) {
            av_log(avctx, AV_LOG_ERROR, "Packing to 32bit required\n");
            return -1;
        }
        stride = (avctx->width * elements + 2) / 3 * 4;
        break;
    case 12:
        stride = avctx->width * elements;
        if (packing) {
            stride *= 2;
        } else {
            stride *= 3;
            if (stride % 8) {
                stride /= 8;
                stride++;
                stride *= 8;
            }
            stride /= 2;
        }
        break;
    case 16:
        stride = 2 * avctx->width * elements;
        break;
    case 32:
        stride = 4 * avctx->width * elements;
        break;
    case 1:
    case 64:
        avpriv_report_missing_feature(avctx, "Depth %d", bits_per_color);
        return AVERROR_PATCHWELCOME;
    default:
        return AVERROR_INVALIDDATA;
    }

    switch (color_trc) {
    case DPX_TRC_LINEAR:
        avctx->color_trc = AVCOL_TRC_LINEAR;
        break;
    case DPX_TRC_SMPTE_274:
    case DPX_TRC_ITU_R_709_4:
        avctx->color_trc = AVCOL_TRC_BT709;
        break;
    case DPX_TRC_ITU_R_601_625:
    case DPX_TRC_ITU_R_601_525:
    case DPX_TRC_SMPTE_170:
        avctx->color_trc = AVCOL_TRC_SMPTE170M;
        break;
    case DPX_TRC_ITU_R_624_4_PAL:
        avctx->color_trc = AVCOL_TRC_GAMMA28;
        break;
    case DPX_TRC_USER_DEFINED:
    case DPX_TRC_UNSPECIFIED_VIDEO:
        /* Nothing to do */
        break;
    default:
        av_log(avctx, AV_LOG_VERBOSE, "Cannot map DPX transfer characteristic "
            "%d to color_trc.\n", color_trc);
        break;
    }

    switch (color_spec) {
    case DPX_COL_SPEC_SMPTE_274:
    case DPX_COL_SPEC_ITU_R_709_4:
        avctx->color_primaries = AVCOL_PRI_BT709;
        break;
    case DPX_COL_SPEC_ITU_R_601_625:
    case DPX_COL_SPEC_ITU_R_624_4_PAL:
        avctx->color_primaries = AVCOL_PRI_BT470BG;
        break;
    case DPX_COL_SPEC_ITU_R_601_525:
    case DPX_COL_SPEC_SMPTE_170:
        avctx->color_primaries = AVCOL_PRI_SMPTE170M;
        break;
    case DPX_COL_SPEC_USER_DEFINED:
    case DPX_COL_SPEC_UNSPECIFIED_VIDEO:
        /* Nothing to do */
        break;
    default:
        av_log(avctx, AV_LOG_VERBOSE, "Cannot map DPX color specification "
            "%d to color_primaries.\n", color_spec);
        break;
    }

    if (yuv) {
        switch (color_spec) {
        case DPX_COL_SPEC_SMPTE_274:
        case DPX_COL_SPEC_ITU_R_709_4:
            avctx->colorspace = AVCOL_SPC_BT709;
            break;
        case DPX_COL_SPEC_ITU_R_601_625:
        case DPX_COL_SPEC_ITU_R_624_4_PAL:
            avctx->colorspace = AVCOL_SPC_BT470BG;
            break;
        case DPX_COL_SPEC_ITU_R_601_525:
        case DPX_COL_SPEC_SMPTE_170:
            avctx->colorspace = AVCOL_SPC_SMPTE170M;
            break;
        case DPX_COL_SPEC_USER_DEFINED:
        case DPX_COL_SPEC_UNSPECIFIED_VIDEO:
            /* Nothing to do */
            break;
        default:
            av_log(avctx, AV_LOG_INFO, "Cannot map DPX color specification "
                "%d to colorspace.\n", color_spec);
            break;
        }
    } else {
        avctx->colorspace = AVCOL_SPC_RGB;
    }

    av_strlcpy(creator, avpkt->data + 160, 100);
    creator[100] = '\0';
    av_dict_set(&p->metadata, "Creator", creator, 0);

    av_strlcpy(input_device, avpkt->data + 1556, 32);
    input_device[32] = '\0';
    av_dict_set(&p->metadata, "Input Device", input_device, 0);

    // Some devices do not pad 10bit samples to whole 32bit words per row
    if (!memcmp(input_device, "Scanity", 7) ||
        !memcmp(creator, "Lasergraphics Inc.", 18)) {
        if (bits_per_color == 10)
            unpadded_10bit = 1;
    }

    // Table 3c: Runs will always break at scan line boundaries. Packing
    // will always break to the next 32-bit word at scan-line boundaries.
    // Unfortunately, the encoder produced invalid files, so attempt
    // to detect it
    // Also handle special case with unpadded content
    need_align = FFALIGN(stride, 4);
    if (need_align*avctx->height + (int64_t)offset > avpkt->size &&
        (!unpadded_10bit || (avctx->width * avctx->height * elements + 2) / 3 * 4 + (int64_t)offset > avpkt->size)) {
        // Alignment seems unappliable, try without
        if (stride*avctx->height + (int64_t)offset > avpkt->size || unpadded_10bit) {
            av_log(avctx, AV_LOG_ERROR, "Overread buffer. Invalid header?\n");
            return AVERROR_INVALIDDATA;
        } else {
            av_log(avctx, AV_LOG_INFO, "Decoding DPX without scanline "
                   "alignment.\n");
            need_align = 0;
        }
    } else {
        need_align -= stride;
        stride = FFALIGN(stride, 4);
    }

    switch (1000 * descriptor + 10 * bits_per_color + endian) {
    case 1081:
    case 1080:
    case 2081:
    case 2080:
    case 3081:
    case 3080:
    case 4081:
    case 4080:
    case 6081:
    case 6080:
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        break;
    case 6121:
    case 6120:
        avctx->pix_fmt = AV_PIX_FMT_GRAY12;
        break;
    case 1320:
    case 2320:
    case 3320:
    case 4320:
    case 6320:
        avctx->pix_fmt = AV_PIX_FMT_GRAYF32LE;
        break;
    case 1321:
    case 2321:
    case 3321:
    case 4321:
    case 6321:
        avctx->pix_fmt = AV_PIX_FMT_GRAYF32BE;
        break;
    case 50081:
    case 50080:
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;
    case 52081:
    case 52080:
        avctx->pix_fmt = AV_PIX_FMT_ABGR;
        break;
    case 51081:
    case 51080:
        avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case 50100:
    case 50101:
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        break;
    case 51100:
    case 51101:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
        break;
    case 50120:
    case 50121:
        avctx->pix_fmt = AV_PIX_FMT_GBRP12;
        break;
    case 51120:
    case 51121:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP12;
        break;
    case 6100:
    case 6101:
        avctx->pix_fmt = AV_PIX_FMT_GRAY10;
        break;
    case 6161:
        avctx->pix_fmt = AV_PIX_FMT_GRAY16BE;
        break;
    case 6160:
        avctx->pix_fmt = AV_PIX_FMT_GRAY16LE;
        break;
    case 50161:
        avctx->pix_fmt = AV_PIX_FMT_RGB48BE;
        break;
    case 50160:
        avctx->pix_fmt = AV_PIX_FMT_RGB48LE;
        break;
    case 51161:
        avctx->pix_fmt = AV_PIX_FMT_RGBA64BE;
        break;
    case 51160:
        avctx->pix_fmt = AV_PIX_FMT_RGBA64LE;
        break;
    case 50320:
        avctx->pix_fmt = AV_PIX_FMT_GBRPF32LE;
        break;
    case 50321:
        avctx->pix_fmt = AV_PIX_FMT_GBRPF32BE;
        break;
    case 51320:
        avctx->pix_fmt = AV_PIX_FMT_GBRAPF32LE;
        break;
    case 51321:
        avctx->pix_fmt = AV_PIX_FMT_GBRAPF32BE;
        break;
    case 100081:
        avctx->pix_fmt = AV_PIX_FMT_UYVY422;
        break;
    case 102081:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        break;
    case 103081:
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported format %d\n",
               1000 * descriptor + 10 * bits_per_color + endian);
        return AVERROR_PATCHWELCOME;
    }

    ff_set_sar(avctx, avctx->sample_aspect_ratio);

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // Move pointer to offset from start of file
    buf =  avpkt->data + offset;

    for (i=0; i<AV_NUM_DATA_POINTERS; i++)
        ptr[i] = p->data[i];

    switch (bits_per_color) {
    case 10:
        for (x = 0; x < avctx->height; x++) {
            uint16_t *dst[4] = {(uint16_t*)ptr[0],
                                (uint16_t*)ptr[1],
                                (uint16_t*)ptr[2],
                                (uint16_t*)ptr[3]};
            int shift = elements > 1 ? packing == 1 ? 22 : 20 : packing == 1 ? 2 : 0;
            for (y = 0; y < avctx->width; y++) {
                if (elements >= 3)
                    *dst[2]++ = read10in32(&buf, &rgbBuffer,
                                           &n_datum, endian, shift);
                if (elements == 1)
                    *dst[0]++ = read10in32_gray(&buf, &rgbBuffer,
                                                &n_datum, endian, shift);
                else
                    *dst[0]++ = read10in32(&buf, &rgbBuffer,
                                           &n_datum, endian, shift);
                if (elements >= 2)
                    *dst[1]++ = read10in32(&buf, &rgbBuffer,
                                           &n_datum, endian, shift);
                if (elements == 4)
                    *dst[3]++ =
                    read10in32(&buf, &rgbBuffer,
                               &n_datum, endian, shift);
            }
            if (!unpadded_10bit)
                n_datum = 0;
            for (i = 0; i < elements; i++)
                ptr[i] += p->linesize[i];
        }
        break;
    case 12:
        for (x = 0; x < avctx->height; x++) {
            uint16_t *dst[4] = {(uint16_t*)ptr[0],
                                (uint16_t*)ptr[1],
                                (uint16_t*)ptr[2],
                                (uint16_t*)ptr[3]};
            int shift = packing == 1 ? 4 : 0;
            for (y = 0; y < avctx->width; y++) {
                if (packing) {
                    if (elements >= 3)
                        *dst[2]++ = read16(&buf, endian) >> shift & 0xFFF;
                    *dst[0]++ = read16(&buf, endian) >> shift & 0xFFF;
                    if (elements >= 2)
                        *dst[1]++ = read16(&buf, endian) >> shift & 0xFFF;
                    if (elements == 4)
                        *dst[3]++ = read16(&buf, endian) >> shift & 0xFFF;
                } else {
                    if (elements >= 3)
                        *dst[2]++ = read12in32(&buf, &rgbBuffer,
                                               &n_datum, endian);
                    *dst[0]++ = read12in32(&buf, &rgbBuffer,
                                           &n_datum, endian);
                    if (elements >= 2)
                        *dst[1]++ = read12in32(&buf, &rgbBuffer,
                                               &n_datum, endian);
                    if (elements == 4)
                        *dst[3]++ = read12in32(&buf, &rgbBuffer,
                                               &n_datum, endian);
                }
            }
            n_datum = 0;
            for (i = 0; i < elements; i++)
                ptr[i] += p->linesize[i];
            // Jump to next aligned position
            buf += need_align;
        }
        break;
    case 32:
        if (elements == 1) {
            av_image_copy_plane(ptr[0], p->linesize[0],
                                buf, stride,
                                elements * avctx->width * 4, avctx->height);
        } else {
            for (y = 0; y < avctx->height; y++) {
                ptr[0] = p->data[0] + y * p->linesize[0];
                ptr[1] = p->data[1] + y * p->linesize[1];
                ptr[2] = p->data[2] + y * p->linesize[2];
                ptr[3] = p->data[3] + y * p->linesize[3];
                for (x = 0; x < avctx->width; x++) {
                    AV_WN32(ptr[2], AV_RN32(buf));
                    AV_WN32(ptr[0], AV_RN32(buf + 4));
                    AV_WN32(ptr[1], AV_RN32(buf + 8));
                    if (avctx->pix_fmt == AV_PIX_FMT_GBRAPF32BE ||
                        avctx->pix_fmt == AV_PIX_FMT_GBRAPF32LE) {
                        AV_WN32(ptr[3], AV_RN32(buf + 12));
                        buf += 4;
                        ptr[3] += 4;
                    }

                    buf += 12;
                    ptr[2] += 4;
                    ptr[0] += 4;
                    ptr[1] += 4;
                }
            }
        }
        break;
    case 16:
        elements *= 2;
    case 8:
        if (   avctx->pix_fmt == AV_PIX_FMT_YUVA444P
            || avctx->pix_fmt == AV_PIX_FMT_YUV444P) {
            for (x = 0; x < avctx->height; x++) {
                ptr[0] = p->data[0] + x * p->linesize[0];
                ptr[1] = p->data[1] + x * p->linesize[1];
                ptr[2] = p->data[2] + x * p->linesize[2];
                ptr[3] = p->data[3] + x * p->linesize[3];
                for (y = 0; y < avctx->width; y++) {
                    *ptr[1]++ = *buf++;
                    *ptr[0]++ = *buf++;
                    *ptr[2]++ = *buf++;
                    if (avctx->pix_fmt == AV_PIX_FMT_YUVA444P)
                        *ptr[3]++ = *buf++;
                }
            }
        } else {
        av_image_copy_plane(ptr[0], p->linesize[0],
                            buf, stride,
                            elements * avctx->width, avctx->height);
        }
        break;
    }

    *got_frame = 1;

    return buf_size;
}

const FFCodec ff_dpx_decoder = {
    .p.name         = "dpx",
    CODEC_LONG_NAME("DPX (Digital Picture Exchange) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_DPX,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
