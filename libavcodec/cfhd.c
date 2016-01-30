/*
 * Copyright (c) 2015-2016 Kieran Kunhya <kieran@kunhya.com>
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

/**
 * @file
 * Cineform HD video decoder
 */

#include "libavutil/attributes.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bitstream.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"
#include "cfhd.h"

enum CFHDParam {
    ChannelCount     =  12,
    SubbandCount     =  14,
    ImageWidth       =  20,
    ImageHeight      =  21,
    LowpassPrecision =  35,
    SubbandNumber    =  48,
    Quantization     =  53,
    ChannelNumber    =  62,
    BitsPerComponent = 101,
    ChannelWidth     = 104,
    ChannelHeight    = 105,
    PrescaleShift    = 109,
};

static av_cold int cfhd_init(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;

    memset(s, 0, sizeof(*s));

    s->avctx                   = avctx;
    avctx->bits_per_raw_sample = 10;

    return ff_cfhd_init_vlcs(s);
}

static void init_plane_defaults(CFHDContext *s)
{
    s->subband_num        = 0;
    s->level              = 0;
    s->subband_num_actual = 0;
}

static void init_frame_defaults(CFHDContext *s)
{
    s->coded_format      = AV_PIX_FMT_YUV422P10;
    s->coded_width       = 0;
    s->coded_height      = 0;
    s->cropped_height    = 0;
    s->bpc               = 10;
    s->channel_cnt       = 4;
    s->subband_cnt       = SUBBAND_COUNT;
    s->channel_num       = 0;
    s->lowpass_precision = 16;
    s->quantisation      = 1;
    s->prescale_shift[0] = 0;
    s->prescale_shift[1] = 0;
    s->prescale_shift[2] = 0;
    s->wavelet_depth     = 3;
    s->pshift            = 1;
    s->codebook          = 0;
    init_plane_defaults(s);
}

/* TODO: merge with VLC tables or use LUT */
static inline int dequant_and_decompand(int level, int quantisation)
{
    int64_t abslevel = abs(level);
    return (abslevel + ((768 * abslevel * abslevel * abslevel) / (255 * 255 * 255))) *
           FFSIGN(level) * quantisation;
}

static inline void filter(int16_t *output, ptrdiff_t out_stride,
                          int16_t *low, ptrdiff_t low_stride,
                          int16_t *high, ptrdiff_t high_stride,
                          int len, int clip)
{
    int16_t tmp;
    int i;

    for (i = 0; i < len; i++) {
        if (i == 0) {
            tmp = (11 * low[0 * low_stride] - 4 * low[1 * low_stride] + low[2 * low_stride] + 4) >> 3;
            output[(2 * i + 0) * out_stride] = (tmp + high[0 * high_stride]) >> 1;
        } else if (i == len - 1) {
            tmp = (5 * low[i * low_stride] + 4 * low[(i - 1) * low_stride] - low[(i - 2) * low_stride] + 4) >> 3;
            output[(2 * i + 0) * out_stride] = (tmp + high[i * high_stride]) >> 1;
        } else {
            tmp = (low[(i - 1) * low_stride] - low[(i + 1) * low_stride] + 4) >> 3;
            output[(2 * i + 0) * out_stride] = (tmp + low[i * low_stride] + high[i * high_stride]) >> 1;
        }
        if (clip)
            output[(2 * i + 0) * out_stride] = av_clip_uintp2_c(output[(2 * i + 0) * out_stride], clip);

        if (i == 0) {
            tmp = (5 * low[0 * low_stride] + 4 * low[1 * low_stride] - low[2 * low_stride] + 4) >> 3;
            output[(2 * i + 1) * out_stride] = (tmp - high[0 * high_stride]) >> 1;
        } else if (i == len - 1) {
            tmp = (11 * low[i * low_stride] - 4 * low[(i - 1) * low_stride] + low[(i - 2) * low_stride] + 4) >> 3;
            output[(2 * i + 1) * out_stride] = (tmp - high[i * high_stride]) >> 1;
        } else {
            tmp = (low[(i + 1) * low_stride] - low[(i - 1) * low_stride] + 4) >> 3;
            output[(2 * i + 1) * out_stride] = (tmp + low[i * low_stride] - high[i * high_stride]) >> 1;
        }
        if (clip)
            output[(2 * i + 1) * out_stride] = av_clip_uintp2_c(output[(2 * i + 1) * out_stride], clip);
    }
}

static void horiz_filter(int16_t *output, int16_t *low, int16_t *high,
                         int width)
{
    filter(output, 1, low, 1, high, 1, width, 0);
}

static void horiz_filter_clip(int16_t *output, int16_t *low, int16_t *high,
                              int width, int clip)
{
    filter(output, 1, low, 1, high, 1, width, clip);
}

static void vert_filter(int16_t *output, ptrdiff_t out_stride,
                        int16_t *low, ptrdiff_t low_stride,
                        int16_t *high, ptrdiff_t high_stride, int len)
{
    filter(output, out_stride, low, low_stride, high, high_stride, len, 0);
}

static void free_buffers(CFHDContext *s)
{
    unsigned i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->plane); i++) {
        av_freep(&s->plane[i].idwt_buf);
        av_freep(&s->plane[i].idwt_tmp);
    }
    s->a_height = 0;
    s->a_width  = 0;
}

static int alloc_buffers(CFHDContext *s)
{
    int i, j, ret, planes;
    int chroma_x_shift, chroma_y_shift;
    unsigned k;

    if ((ret = av_pix_fmt_get_chroma_sub_sample(s->coded_format,
                                                &chroma_x_shift,
                                                &chroma_y_shift)) < 0)
        return ret;
    planes = av_pix_fmt_count_planes(s->coded_format);

    for (i = 0; i < planes; i++) {
        int w8, h8, w4, h4, w2, h2;
        int width  = i ? s->coded_width  >> chroma_x_shift : s->coded_width;
        int height = i ? s->coded_height >> chroma_y_shift : s->coded_height;
        ptrdiff_t stride = FFALIGN(width  / 8, 8) * 8;
        height           = FFALIGN(height / 8, 2) * 8;
        s->plane[i].width  = width;
        s->plane[i].height = height;
        s->plane[i].stride = stride;

        w8 = FFALIGN(s->plane[i].width  / 8, 8);
        h8 = FFALIGN(s->plane[i].height / 8, 2);
        w4 = w8 * 2;
        h4 = h8 * 2;
        w2 = w4 * 2;
        h2 = h4 * 2;

        s->plane[i].idwt_buf =
            av_malloc_array(height * stride, sizeof(*s->plane[i].idwt_buf));
        s->plane[i].idwt_tmp =
            av_malloc_array(height * stride, sizeof(*s->plane[i].idwt_tmp));
        if (!s->plane[i].idwt_buf || !s->plane[i].idwt_tmp)
            return AVERROR(ENOMEM);

        s->plane[i].subband[0] = s->plane[i].idwt_buf;
        s->plane[i].subband[1] = s->plane[i].idwt_buf + 2 * w8 * h8;
        s->plane[i].subband[2] = s->plane[i].idwt_buf + 1 * w8 * h8;
        s->plane[i].subband[3] = s->plane[i].idwt_buf + 3 * w8 * h8;
        s->plane[i].subband[4] = s->plane[i].idwt_buf + 2 * w4 * h4;
        s->plane[i].subband[5] = s->plane[i].idwt_buf + 1 * w4 * h4;
        s->plane[i].subband[6] = s->plane[i].idwt_buf + 3 * w4 * h4;
        s->plane[i].subband[7] = s->plane[i].idwt_buf + 2 * w2 * h2;
        s->plane[i].subband[8] = s->plane[i].idwt_buf + 1 * w2 * h2;
        s->plane[i].subband[9] = s->plane[i].idwt_buf + 3 * w2 * h2;

        for (j = 0; j < DWT_LEVELS; j++) {
            for (k = 0; k < FF_ARRAY_ELEMS(s->plane[i].band[j]); k++) {
                s->plane[i].band[j][k].a_width  = w8 << j;
                s->plane[i].band[j][k].a_height = h8 << j;
            }
        }

        /* ll2 and ll1 commented out because they are done in-place */
        s->plane[i].l_h[0] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[1] = s->plane[i].idwt_tmp + 2 * w8 * h8;
        // s->plane[i].l_h[2] = ll2;
        s->plane[i].l_h[3] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[4] = s->plane[i].idwt_tmp + 2 * w4 * h4;
        // s->plane[i].l_h[5] = ll1;
        s->plane[i].l_h[6] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[7] = s->plane[i].idwt_tmp + 2 * w2 * h2;
    }

    s->a_height = s->coded_height;
    s->a_width  = s->coded_width;
    s->a_format = s->coded_format;

    return 0;
}

static int parse_tag(CFHDContext *s, GetByteContext *gb,
                     int16_t *tag_, uint16_t *value, int *planes)
{
    /* Bit weird but implement the tag parsing as the spec says */
    uint16_t tagu   = bytestream2_get_be16(gb);
    int16_t tag     = tagu;
    int8_t tag8     = tagu >> 8;
    uint16_t abstag = abs(tag);
    int8_t abs_tag8 = abs(tag8);
    uint16_t data   = bytestream2_get_be16(gb);
    *tag_ = tag;
    *value = data;

    if (abs_tag8 >= 0x60 && abs_tag8 <= 0x6F) {
        av_log(s->avctx, AV_LOG_DEBUG, "large len %"PRIX16"\n",
               ((tagu & 0xFF) << 16) | data);
        return 0;
    } else if (abstag >= 0x4000 && abstag <= 0x40FF) {
        av_log(s->avctx, AV_LOG_DEBUG, "Small chunk length %"PRIu16" %s\n",
               data * 4, tag < 0 ? "optional" : "required");
        bytestream2_skipu(gb, data * 4);
        return 0;
    }

    switch (tag) {
    case 1:
        av_log(s->avctx, AV_LOG_DEBUG, "Sample type? %"PRIu16"\n", data);
        break;
    case 2:
    {
        int i;
        av_log(s->avctx, AV_LOG_DEBUG,
               "tag=2 header - skipping %"PRIu16" tag/value pairs\n", data);
        if (data > bytestream2_get_bytes_left(gb) / 4) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Too many tag/value pairs (%"PRIu16")\n", data);
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < data; i++) {
            uint16_t tag2 = bytestream2_get_be16(gb);
            uint16_t val2 = bytestream2_get_be16(gb);
            av_log(s->avctx, AV_LOG_DEBUG, "Tag/Value = %"PRIX16" %"PRIX16"\n",
                   tag2, val2);
        }
        break;
    }
    case 10:
        if (data != 0) {
            avpriv_report_missing_feature(s->avctx, "Transform type %"PRIu16, data);
            return AVERROR_PATCHWELCOME;
        }
        av_log(s->avctx, AV_LOG_DEBUG, "Transform-type? %"PRIu16"\n", data);
        break;
    case ChannelCount:
        av_log(s->avctx, AV_LOG_DEBUG, "Channel count: %"PRIu16"\n", data);
        if (data > 4) {
            avpriv_report_missing_feature(s->avctx, "Channel count %"PRIu16, data);
            return AVERROR_PATCHWELCOME;
        }
        s->channel_cnt = data;
        break;
    case SubbandCount:
        av_log(s->avctx, AV_LOG_DEBUG, "Subband count: %"PRIu16"\n", data);
        if (data != SUBBAND_COUNT) {
            avpriv_report_missing_feature(s->avctx, "Subband count %"PRIu16, data);
            return AVERROR_PATCHWELCOME;
        }
        break;
    case ImageWidth:
        av_log(s->avctx, AV_LOG_DEBUG, "Width %"PRIu16"\n", data);
        s->coded_width = data;
        break;
    case ImageHeight:
        av_log(s->avctx, AV_LOG_DEBUG, "Height %"PRIu16"\n", data);
        s->coded_height = data;
        break;
    case 23:
        avpriv_report_missing_feature(s->avctx, "Skip frame");
        return AVERROR_PATCHWELCOME;
    case 27:
        av_log(s->avctx, AV_LOG_DEBUG, "Lowpass width %"PRIu16"\n", data);
        if (data < 2 || data > s->plane[s->channel_num].band[0][0].a_width) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid lowpass width\n");
            return AVERROR_INVALIDDATA;
        }
        s->plane[s->channel_num].band[0][0].width  = data;
        s->plane[s->channel_num].band[0][0].stride = data;
        break;
    case 28:
        av_log(s->avctx, AV_LOG_DEBUG, "Lowpass height %"PRIu16"\n", data);
        if (data < 2 || data > s->plane[s->channel_num].band[0][0].a_height) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid lowpass height\n");
            return AVERROR_INVALIDDATA;
        }
        s->plane[s->channel_num].band[0][0].height = data;
        break;
    case LowpassPrecision:
        av_log(s->avctx, AV_LOG_DEBUG, "Lowpass precision bits: %"PRIu16"\n", data);
        break;
    case 41:
    case 49:
        av_log(s->avctx, AV_LOG_DEBUG,
               "Highpass width%s %"PRIu16" channel %i level %i subband %i\n",
               tag == 49 ? "2" : "", data,
               s->channel_num, s->level, s->subband_num);
        if (data < 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid highpass width%s\n", tag == 49 ? "2" : "");
            return AVERROR_INVALIDDATA;
        }
        s->plane[s->channel_num].band[s->level][s->subband_num].width  = data;
        s->plane[s->channel_num].band[s->level][s->subband_num].stride = FFALIGN(data, 8);
        break;
    case 42:
    case 50:
        av_log(s->avctx, AV_LOG_DEBUG, "Highpass height%s %"PRIu16"\n", tag == 50 ? "2" : "", data);
        if (data < 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid highpass height%s\n", tag == 50 ? "2" : "");
            return AVERROR_INVALIDDATA;
        }
        s->plane[s->channel_num].band[s->level][s->subband_num].height = data;
        break;
    case SubbandNumber:
        av_log(s->avctx, AV_LOG_DEBUG, "Subband number %"PRIu16"\n", data);
        if (data > 3) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid subband number\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->subband_num != 0 && data == 1) {
            if (s->level + 1 >= DWT_LEVELS) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid level\n");
                return AVERROR_INVALIDDATA;
            }

            s->level++;
        }
        s->subband_num = data;
        break;
    case 51:
        av_log(s->avctx, AV_LOG_DEBUG, "Subband number actual %"PRIu16"\n", data);
        if (data >= SUBBAND_COUNT) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid subband number actual\n");
            return AVERROR_INVALIDDATA;
        }
        s->subband_num_actual = data;
        break;
    case Quantization:
        s->quantisation = data;
        av_log(s->avctx, AV_LOG_DEBUG, "Quantisation: %"PRIu16"\n", data);
        break;
    case ChannelNumber:
        av_log(s->avctx, AV_LOG_DEBUG, "Channel number %"PRIu16"\n", data);
        if (data >= *planes) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid channel number\n");
            return AVERROR_INVALIDDATA;
        }
        s->channel_num = data;
        init_plane_defaults(s);
        break;
    case 70:
        av_log(s->avctx, AV_LOG_DEBUG,
               "Subsampling or bit-depth flag? %"PRIu16"\n", data);
        if (!(data == 10 || data == 12)) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid bits per channel\n");
            return AVERROR_INVALIDDATA;
        }
        s->bpc = data;
        break;
    case 71:
        s->codebook = data;
        av_log(s->avctx, AV_LOG_DEBUG, "Codebook %i\n", s->codebook);
        break;
    case 72:
        s->codebook = data;
        av_log(s->avctx, AV_LOG_DEBUG, "Other codebook? %i\n", s->codebook);
        break;
    case 84:
        av_log(s->avctx, AV_LOG_DEBUG, "Sample format? %"PRIu16"\n", data);
        switch (data) {
        case 1:
            s->coded_format = AV_PIX_FMT_YUV422P10;
            break;
        case 3:
            s->coded_format = AV_PIX_FMT_GBRP12;
            break;
        case 4:
            s->coded_format = AV_PIX_FMT_GBRAP12;
            break;
        default:
            avpriv_report_missing_feature(s->avctx, "Sample format %"PRIu16, data);
            return AVERROR_PATCHWELCOME;
        }
        *planes = av_pix_fmt_count_planes(s->coded_format);
        break;
    case -85:
        av_log(s->avctx, AV_LOG_DEBUG, "Cropped height %"PRIu16"\n", data);
        s->cropped_height = data;
        break;
    case 101:
        av_log(s->avctx, AV_LOG_DEBUG, "Bits per component: %"PRIu16"\n", data);
        s->bpc = data;
        break;
    case PrescaleShift:
        s->prescale_shift[0] = (data >> 0) & 0x7;
        s->prescale_shift[1] = (data >> 3) & 0x7;
        s->prescale_shift[2] = (data >> 6) & 0x7;
        av_log(s->avctx, AV_LOG_DEBUG, "Prescale shift (VC-5): %"PRIX16"\n", data);
        break;
    default:
        av_log(s->avctx, AV_LOG_DEBUG, "Unknown tag %"PRIu16" data %"PRIX16"\n",
               tag, data);
    }

    return 0;
}

static int read_lowpass_coeffs(CFHDContext *s, GetByteContext *gb,
                               int16_t *coeff_data)
{
    int i, j;
    int lowpass_height   = s->plane[s->channel_num].band[0][0].height;
    int lowpass_width    = s->plane[s->channel_num].band[0][0].width;
    int lowpass_a_height = s->plane[s->channel_num].band[0][0].a_height;
    int lowpass_a_width  = s->plane[s->channel_num].band[0][0].a_width;

    if (lowpass_height > lowpass_a_height ||
        lowpass_width  > lowpass_a_width  ||
        lowpass_a_width * lowpass_a_height * sizeof(*coeff_data) > bytestream2_get_bytes_left(gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "Too many lowpass coefficients\n");
        return AVERROR_INVALIDDATA;
    }

    av_log(s->avctx, AV_LOG_DEBUG,
           "Start of lowpass coeffs component %d height:%d, width:%d\n",
           s->channel_num, lowpass_height, lowpass_width);
    for (i = 0; i < lowpass_height; i++) {
        for (j = 0; j < lowpass_width; j++)
            coeff_data[j] = bytestream2_get_be16u(gb);

        coeff_data += lowpass_width;
    }

    /* Align to mod-4 position to continue reading tags */
    bytestream2_seek(gb, bytestream2_tell(gb) & 3, SEEK_CUR);

    /* Copy last coefficient line if height is odd. */
    if (lowpass_height & 1) {
        int16_t *last_line = &coeff_data[lowpass_height * lowpass_width];
        memcpy(last_line, &last_line[-lowpass_width],
               lowpass_width * sizeof(*coeff_data));
    }

    av_log(s->avctx, AV_LOG_DEBUG, "Lowpass coefficients %i\n",
           lowpass_width * lowpass_height);

    return 0;
}

#define DECODE_SUBBAND_COEFFS(TABLE, COND)                              \
    while (1) {                                                         \
        int level, run, coeff;                                          \
        BITSTREAM_RL_VLC(level, run, &s->bc, s->TABLE, VLC_BITS, 3);    \
                                                                        \
        /* escape */                                                    \
        if (COND)                                                       \
            break;                                                      \
                                                                        \
        count += run;                                                   \
                                                                        \
        if (count > expected) {                                         \
            av_log(s->avctx, AV_LOG_ERROR, "Escape codeword not found, " \
                   "probably corrupt data\n");                          \
            return AVERROR_INVALIDDATA;                                 \
        }                                                               \
                                                                        \
        coeff = dequant_and_decompand(level, s->quantisation);          \
        for (i = 0; i < run; i++)                                       \
            *coeff_data++ = coeff;                                      \
    }                                                                   \

static int read_highpass_coeffs(CFHDContext *s, GetByteContext *gb,
                                int16_t *coeff_data)
{
    int i, ret;
    int highpass_height       = s->plane[s->channel_num].band[s->level][s->subband_num].height;
    int highpass_width        = s->plane[s->channel_num].band[s->level][s->subband_num].width;
    int highpass_a_width      = s->plane[s->channel_num].band[s->level][s->subband_num].a_width;
    int highpass_a_height     = s->plane[s->channel_num].band[s->level][s->subband_num].a_height;
    ptrdiff_t highpass_stride = s->plane[s->channel_num].band[s->level][s->subband_num].stride;
    int expected   = highpass_height   * highpass_stride;
    int a_expected = highpass_a_height * highpass_a_width;
    int count = 0;
    unsigned bytes;

    if (highpass_height > highpass_a_height ||
        highpass_width  > highpass_a_width  ||
        a_expected      < expected) {
        av_log(s->avctx, AV_LOG_ERROR, "Too many highpass coefficients\n");
        return AVERROR_INVALIDDATA;
    }

    av_log(s->avctx, AV_LOG_DEBUG,
           "Start subband coeffs plane %i level %i codebook %i expected %i\n",
           s->channel_num, s->level, s->codebook, expected);

    if ((ret = bitstream_init8(&s->bc, gb->buffer,
                               bytestream2_get_bytes_left(gb))) < 0)
        return ret;
    if (!s->codebook) {
        DECODE_SUBBAND_COEFFS(table_9_rl_vlc, level == 64)
    } else {
        DECODE_SUBBAND_COEFFS(table_18_rl_vlc, level == 255 && run == 2)
    }

    bytes = FFALIGN(AV_CEIL_RSHIFT(bitstream_tell(&s->bc), 3), 4);
    if (bytes > bytestream2_get_bytes_left(gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "Bitstream overread error\n");
        return AVERROR_INVALIDDATA;
    } else
        bytestream2_seek(gb, bytes, SEEK_CUR);

    av_log(s->avctx, AV_LOG_DEBUG, "End subband coeffs %i extra %i\n",
           count, count - expected);
    s->codebook = 0;

    /* Copy last coefficient line if height is odd. */
    if (highpass_height & 1) {
        int16_t *last_line = &coeff_data[expected];
        memcpy(last_line, &last_line[-highpass_stride],
               highpass_stride * sizeof(*coeff_data));
    }

    return 0;
}

static int reconstruct_level(CFHDContext *s, AVFrame *pic, int plane, int level)
{
    int i, j, idx = level - 1, idx2 = level > 1 ? 1 : 0;
    int16_t *low, *high, *output, *dst;
    int lowpass_height        = s->plane[plane].band[idx][idx2].height;
    int lowpass_width         = s->plane[plane].band[idx][idx2].width;
    ptrdiff_t highpass_stride = s->plane[plane].band[idx][1].stride;

    if (lowpass_height                     > s->plane[plane].band[idx][idx2].a_height ||
        lowpass_width                      > s->plane[plane].band[idx][idx2].a_width  ||
        s->plane[plane].band[idx][1].width > s->plane[plane].band[idx][1].a_width     ||
        !highpass_stride) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
        return AVERROR_INVALIDDATA;
    }

    av_log(s->avctx, AV_LOG_DEBUG, "Level %d plane %i %i %i %ti\n",
           level, plane, lowpass_height, lowpass_width, highpass_stride);

    low    = s->plane[plane].subband[0];
    high   = s->plane[plane].subband[2 + 3 * idx];
    output = s->plane[plane].l_h[3 * idx];
    for (i = 0; i < lowpass_width; i++) {
        vert_filter(output, lowpass_width, low, lowpass_width, high,
                    highpass_stride, lowpass_height);
        low++;
        high++;
        output++;
    }

    low    = s->plane[plane].subband[1 + 3 * idx];
    high   = s->plane[plane].subband[3 + 3 * idx];
    output = s->plane[plane].l_h[1 + 3 * idx];
    for (i = 0; i < lowpass_width; i++) {
        // note the stride of "low" is highpass_stride
        vert_filter(output, lowpass_width, low, highpass_stride, high,
                    highpass_stride, lowpass_height);
        low++;
        high++;
        output++;
    }

    low  = s->plane[plane].l_h[0 + 3 * idx];
    high = s->plane[plane].l_h[1 + 3 * idx];

    if (level != 3) {
        output = s->plane[plane].subband[0];
        for (i = 0; i < lowpass_height * 2; i++) {
            horiz_filter(output, low, high, lowpass_width);
            low    += lowpass_width;
            high   += lowpass_width;
            output += lowpass_width * 2;
        }
        if (s->bpc == 12 || level == 2) {
            output = s->plane[plane].subband[0];
            for (i = 0; i < lowpass_height * 2; i++) {
                for (j = 0; j < lowpass_width * 2; j++)
                    output[j] <<= 2;

                output += lowpass_width * 2;
            }
        }
    } else {
        int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;
        dst = (int16_t *)pic->data[act_plane];
        for (i = 0; i < lowpass_height * 2; i++) {
            horiz_filter_clip(dst, low, high, lowpass_width, s->bpc);
            low  += lowpass_width;
            high += lowpass_width;
            dst  += pic->linesize[act_plane] / 2;
        }
    }

    return 0;
}

static int cfhd_decode(AVCodecContext *avctx, void *data, int *got_frame,
                       AVPacket *avpkt)
{
    CFHDContext *s = avctx->priv_data;
    GetByteContext gb;
    ThreadFrame frame = { .f = data };
    int ret = 0, planes, plane;
    int16_t tag;
    uint16_t value;

    init_frame_defaults(s);
    planes = av_pix_fmt_count_planes(s->coded_format);

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    while (bytestream2_get_bytes_left(&gb) > 4) {
        if ((ret = parse_tag(s, &gb, &tag, &value, &planes)) < 0)
            return ret;

        /* Some kind of end of header tag */
        if (tag == 4 && value == 0x1A4A)
            break;
    }

    if (s->coded_width <= 0 || s->coded_height <= 0 || s->coded_format == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Video dimensions/format missing or invalid\n");
        return AVERROR_INVALIDDATA;
    }

    ret = ff_set_dimensions(s->avctx, s->coded_width, s->coded_height);
    if (ret < 0)
        return ret;
    if (s->cropped_height)
        s->avctx->height = s->cropped_height;

    s->avctx->pix_fmt = s->coded_format;

    if (s->a_width != s->coded_width || s->a_height != s->coded_height ||
        s->a_format != s->coded_format) {
        free_buffers(s);
        if ((ret = alloc_buffers(s)) < 0) {
            free_buffers(s);
            return ret;
        }
    }

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    s->coded_width  = 0;
    s->coded_height = 0;
    s->coded_format = AV_PIX_FMT_NONE;

    while (bytestream2_get_bytes_left(&gb) > 4) {
        int16_t *coeff_data;

        if ((ret = parse_tag(s, &gb, &tag, &value, &planes)) < 0)
            return ret;

        coeff_data = s->plane[s->channel_num].subband[s->subband_num_actual];
        if (tag == 4 && value == 0x0F0F) {
            if ((ret = read_lowpass_coeffs(s, &gb, coeff_data)) < 0)
                return ret;
        } else if (tag == 55 && s->subband_num_actual != 255) {
            if ((ret = read_highpass_coeffs(s, &gb, coeff_data)) < 0)
                return ret;
        }
    }

    if (s->coded_width || s->coded_height || s->coded_format != AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid dimensions\n");
        return AVERROR_INVALIDDATA;
    }

    planes = av_pix_fmt_count_planes(avctx->pix_fmt);
    for (plane = 0; plane < planes; plane++) {
        /* level 1 */
        if ((ret = reconstruct_level(s, data, plane, 1)) < 0)
            return ret;

        /* level 2 */
        if ((ret = reconstruct_level(s, data, plane, 2)) < 0)
            return ret;

        /* level 3 */
        if ((ret = reconstruct_level(s, data, plane, 3)) < 0)
            return ret;
    }

    *got_frame = 1;
    return avpkt->size;
}

static av_cold int cfhd_close(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;

    free_buffers(s);

    ff_free_vlc(&s->vlc_9);
    ff_free_vlc(&s->vlc_18);

    return 0;
}

AVCodec ff_cfhd_decoder = {
    .name             = "cfhd",
    .long_name        = NULL_IF_CONFIG_SMALL("Cineform HD"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_CFHD,
    .priv_data_size   = sizeof(CFHDContext),
    .init             = cfhd_init,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(cfhd_init),
    .close            = cfhd_close,
    .decode           = cfhd_decode,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
