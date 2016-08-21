/*
 * Copyright (c) 2015-2016 Kieran Kunhya <kieran@kunhya.com>
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
 * CFHD Video Decoder
 */

#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"
#include "thread.h"
#include "cfhd.h"

#define SUBBAND_COUNT 10

static av_cold int cfhd_decode_init(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;

    avctx->bits_per_raw_sample = 10;
    s->avctx                   = avctx;

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
    s->coded_width       = 0;
    s->coded_height      = 0;
    s->bpc               = 10;
    s->channel_cnt       = 4;
    s->subband_cnt       = 10;
    s->channel_num       = 0;
    s->lowpass_precision = 16;
    s->quantisation      = 1;
    s->wavelet_depth     = 3;
    s->pshift            = 1;
    s->codebook          = 0;
    init_plane_defaults(s);
}

/* TODO: merge with VLC tables or use LUT */
static inline int dequant_and_decompand(int level, int quantisation)
{
    int64_t abslevel = abs(level);
    return (abslevel + ((768 * abslevel * abslevel * abslevel) / (255 * 255 * 255))) * FFSIGN(level) * quantisation;
}

static inline void filter(int16_t *output, ptrdiff_t out_stride, int16_t *low, ptrdiff_t low_stride,
                          int16_t *high, ptrdiff_t high_stride, int len, uint8_t clip)
{
    int16_t tmp;

    int i;
    for (i = 0; i < len; i++) {
        if (i == 0) {
            tmp = (11*low[0*low_stride] - 4*low[1*low_stride] + low[2*low_stride] + 4) >> 3;
            output[(2*i+0)*out_stride] = (tmp + high[0*high_stride]) >> 1;
            if (clip)
                output[(2*i+0)*out_stride] = av_clip_uintp2_c(output[(2*i+0)*out_stride], clip);

            tmp = ( 5*low[0*low_stride] + 4*low[1*low_stride] - low[2*low_stride] + 4) >> 3;
            output[(2*i+1)*out_stride] = (tmp - high[0*high_stride]) >> 1;
            if (clip)
                output[(2*i+1)*out_stride] = av_clip_uintp2_c(output[(2*i+1)*out_stride], clip);
        } else if (i == len-1) {
            tmp = ( 5*low[i*low_stride] + 4*low[(i-1)*low_stride] - low[(i-2)*low_stride] + 4) >> 3;
            output[(2*i+0)*out_stride] = (tmp + high[i*high_stride]) >> 1;
            if (clip)
                output[(2*i+0)*out_stride] = av_clip_uintp2_c(output[(2*i+0)*out_stride], clip);

            tmp = (11*low[i*low_stride] - 4*low[(i-1)*low_stride] + low[(i-2)*low_stride] + 4) >> 3;
            output[(2*i+1)*out_stride] = (tmp - high[i*high_stride]) >> 1;
            if (clip)
                output[(2*i+1)*out_stride] = av_clip_uintp2_c(output[(2*i+1)*out_stride], clip);
        } else {
            tmp = (low[(i-1)*low_stride] - low[(i+1)*low_stride] + 4) >> 3;
            output[(2*i+0)*out_stride] = (tmp + low[i*low_stride] + high[i*high_stride]) >> 1;
            if (clip)
                output[(2*i+0)*out_stride] = av_clip_uintp2_c(output[(2*i+0)*out_stride], clip);

            tmp = (low[(i+1)*low_stride] - low[(i-1)*low_stride] + 4) >> 3;
            output[(2*i+1)*out_stride] = (tmp + low[i*low_stride] - high[i*high_stride]) >> 1;
            if (clip)
                output[(2*i+1)*out_stride] = av_clip_uintp2_c(output[(2*i+1)*out_stride], clip);
        }
    }
}

static void horiz_filter(int16_t *output, int16_t *low, int16_t *high, int width)
{
    filter(output, 1, low, 1, high, 1, width, 0);
}

static void horiz_filter_clip(int16_t *output, int16_t *low, int16_t *high, int width, uint8_t clip)
{
    filter(output, 1, low, 1, high, 1, width, clip);
}

static void vert_filter(int16_t *output, int out_stride, int16_t *low, int low_stride,
                        int16_t *high, int high_stride, int len)
{
    filter(output, out_stride, low, low_stride, high, high_stride, len, 0);
}

static void free_buffers(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;
    int i, j;

    for (i = 0; i < 4; i++) {
        av_freep(&s->plane[i].idwt_buf);
        av_freep(&s->plane[i].idwt_tmp);

        for (j = 0; j < 9; j++)
            s->plane[i].subband[j] = NULL;

        for (j = 0; j < 8; j++)
            s->plane[i].l_h[j] = NULL;
    }
    s->a_height = 0;
    s->a_width  = 0;
}

static int alloc_buffers(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;
    int i, j, k, ret, planes;

    if ((ret = ff_set_dimensions(avctx, s->coded_width, s->coded_height)) < 0)
        return ret;
    avctx->pix_fmt = s->coded_format;

    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);
    planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    for (i = 0; i < planes; i++) {
        int width = i ? avctx->width >> s->chroma_x_shift : avctx->width;
        int height = i ? avctx->height >> s->chroma_y_shift : avctx->height;
        int stride = FFALIGN(width / 8, 8) * 8;
        int w8, h8, w4, h4, w2, h2;
        height = FFALIGN(height / 8, 2) * 8;
        s->plane[i].width = width;
        s->plane[i].height = height;
        s->plane[i].stride = stride;

        w8 = FFALIGN(s->plane[i].width / 8, 8);
        h8 = FFALIGN(s->plane[i].height / 8, 2);
        w4 = w8 * 2;
        h4 = h8 * 2;
        w2 = w4 * 2;
        h2 = h4 * 2;

        s->plane[i].idwt_buf = av_mallocz_array(height * stride, sizeof(*s->plane[i].idwt_buf));
        s->plane[i].idwt_tmp = av_malloc_array(height * stride, sizeof(*s->plane[i].idwt_tmp));
        if (!s->plane[i].idwt_buf || !s->plane[i].idwt_tmp) {
            return AVERROR(ENOMEM);
        }

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
            for(k = 0; k < 4; k++) {
                s->plane[i].band[j][k].a_width  = w8 << j;
                s->plane[i].band[j][k].a_height = h8 << j;
            }
        }

        /* ll2 and ll1 commented out because they are done in-place */
        s->plane[i].l_h[0] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[1] = s->plane[i].idwt_tmp + 2 * w8 * h8;
        //s->plane[i].l_h[2] = ll2;
        s->plane[i].l_h[3] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[4] = s->plane[i].idwt_tmp + 2 * w4 * h4;
        //s->plane[i].l_h[5] = ll1;
        s->plane[i].l_h[6] = s->plane[i].idwt_tmp;
        s->plane[i].l_h[7] = s->plane[i].idwt_tmp + 2 * w2 * h2;
    }

    s->a_height = s->coded_height;
    s->a_width  = s->coded_width;
    s->a_format = s->coded_format;

    return 0;
}

static int cfhd_decode(AVCodecContext *avctx, void *data, int *got_frame,
                       AVPacket *avpkt)
{
    CFHDContext *s = avctx->priv_data;
    GetByteContext gb;
    ThreadFrame frame = { .f = data };
    AVFrame *pic = data;
    int ret = 0, i, j, planes, plane, got_buffer = 0;
    int16_t *coeff_data;

    s->coded_format = AV_PIX_FMT_YUV422P10;
    init_frame_defaults(s);
    planes = av_pix_fmt_count_planes(s->coded_format);

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    while (bytestream2_get_bytes_left(&gb) > 4) {
        /* Bit weird but implement the tag parsing as the spec says */
        uint16_t tagu   = bytestream2_get_be16(&gb);
        int16_t tag     = (int16_t)tagu;
        int8_t tag8     = (int8_t)(tagu >> 8);
        uint16_t abstag = abs(tag);
        int8_t abs_tag8 = abs(tag8);
        uint16_t data   = bytestream2_get_be16(&gb);
        if (abs_tag8 >= 0x60 && abs_tag8 <= 0x6f) {
            av_log(avctx, AV_LOG_DEBUG, "large len %x\n", ((tagu & 0xff) << 16) | data);
        } else if (tag == 20) {
            av_log(avctx, AV_LOG_DEBUG, "Width %"PRIu16"\n", data);
            s->coded_width = data;
        } else if (tag == 21) {
            av_log(avctx, AV_LOG_DEBUG, "Height %"PRIu16"\n", data);
            s->coded_height = data;
        } else if (tag == 101) {
            av_log(avctx, AV_LOG_DEBUG, "Bits per component: %"PRIu16"\n", data);
            s->bpc = data;
        } else if (tag == 12) {
            av_log(avctx, AV_LOG_DEBUG, "Channel Count: %"PRIu16"\n", data);
            s->channel_cnt = data;
            if (data > 4) {
                av_log(avctx, AV_LOG_ERROR, "Channel Count of %"PRIu16" is unsupported\n", data);
                ret = AVERROR_PATCHWELCOME;
                break;
            }
        } else if (tag == 14) {
            av_log(avctx, AV_LOG_DEBUG, "Subband Count: %"PRIu16"\n", data);
            if (data != SUBBAND_COUNT) {
                av_log(avctx, AV_LOG_ERROR, "Subband Count of %"PRIu16" is unsupported\n", data);
                ret = AVERROR_PATCHWELCOME;
                break;
            }
        } else if (tag == 62) {
            s->channel_num = data;
            av_log(avctx, AV_LOG_DEBUG, "Channel number %"PRIu16"\n", data);
            if (s->channel_num >= planes) {
                av_log(avctx, AV_LOG_ERROR, "Invalid channel number\n");
                ret = AVERROR(EINVAL);
                break;
            }
            init_plane_defaults(s);
        } else if (tag == 48) {
            if (s->subband_num != 0 && data == 1)  // hack
                s->level++;
            av_log(avctx, AV_LOG_DEBUG, "Subband number %"PRIu16"\n", data);
            s->subband_num = data;
            if (s->level >= DWT_LEVELS) {
                av_log(avctx, AV_LOG_ERROR, "Invalid level\n");
                ret = AVERROR(EINVAL);
                break;
            }
            if (s->subband_num > 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid subband number\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 51) {
            av_log(avctx, AV_LOG_DEBUG, "Subband number actual %"PRIu16"\n", data);
            s->subband_num_actual = data;
            if (s->subband_num_actual >= 10) {
                av_log(avctx, AV_LOG_ERROR, "Invalid subband number actual\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 35)
            av_log(avctx, AV_LOG_DEBUG, "Lowpass precision bits: %"PRIu16"\n", data);
        else if (tag == 53) {
            s->quantisation = data;
            av_log(avctx, AV_LOG_DEBUG, "Quantisation: %"PRIu16"\n", data);
        } else if (tag == 109) {
            s->prescale_shift[0] = (data >> 0) & 0x7;
            s->prescale_shift[1] = (data >> 3) & 0x7;
            s->prescale_shift[2] = (data >> 6) & 0x7;
            av_log(avctx, AV_LOG_DEBUG, "Prescale shift (VC-5): %x\n", data);
        } else if (tag == 27) {
            s->plane[s->channel_num].band[0][0].width  = data;
            s->plane[s->channel_num].band[0][0].stride = data;
            av_log(avctx, AV_LOG_DEBUG, "Lowpass width %"PRIu16"\n", data);
            if (data < 3 || data > s->plane[s->channel_num].band[0][0].a_width) {
                av_log(avctx, AV_LOG_ERROR, "Invalid lowpass width\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 28) {
            s->plane[s->channel_num].band[0][0].height = data;
            av_log(avctx, AV_LOG_DEBUG, "Lowpass height %"PRIu16"\n", data);
            if (data < 3 || data > s->plane[s->channel_num].band[0][0].height) {
                av_log(avctx, AV_LOG_ERROR, "Invalid lowpass height\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 1)
            av_log(avctx, AV_LOG_DEBUG, "Sample type? %"PRIu16"\n", data);
        else if (tag == 10) {
            if (data != 0) {
                avpriv_report_missing_feature(avctx, "Transform type of %"PRIu16, data);
                ret = AVERROR_PATCHWELCOME;
                break;
            }
            av_log(avctx, AV_LOG_DEBUG, "Transform-type? %"PRIu16"\n", data);
        } else if (abstag >= 0x4000 && abstag <= 0x40ff) {
            av_log(avctx, AV_LOG_DEBUG, "Small chunk length %d %s\n", data * 4, tag < 0 ? "optional" : "required");
            bytestream2_skipu(&gb, data * 4);
        } else if (tag == 23) {
            av_log(avctx, AV_LOG_DEBUG, "Skip frame\n");
            avpriv_report_missing_feature(avctx, "Skip frame");
            ret = AVERROR_PATCHWELCOME;
            break;
        } else if (tag == 2) {
            av_log(avctx, AV_LOG_DEBUG, "tag=2 header - skipping %i tag/value pairs\n", data);
            if (data > bytestream2_get_bytes_left(&gb) / 4) {
                av_log(avctx, AV_LOG_ERROR, "too many tag/value pairs (%d)\n", data);
                ret = AVERROR_INVALIDDATA;
                break;
            }
            for (i = 0; i < data; i++) {
                uint16_t tag2 = bytestream2_get_be16(&gb);
                uint16_t val2 = bytestream2_get_be16(&gb);
                av_log(avctx, AV_LOG_DEBUG, "Tag/Value = %x %x\n", tag2, val2);
            }
        } else if (tag == 41) {
            s->plane[s->channel_num].band[s->level][s->subband_num].width  = data;
            s->plane[s->channel_num].band[s->level][s->subband_num].stride = FFALIGN(data, 8);
            av_log(avctx, AV_LOG_DEBUG, "Highpass width %i channel %i level %i subband %i\n", data, s->channel_num, s->level, s->subband_num);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass width\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 42) {
            s->plane[s->channel_num].band[s->level][s->subband_num].height = data;
            av_log(avctx, AV_LOG_DEBUG, "Highpass height %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass height\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 49) {
            s->plane[s->channel_num].band[s->level][s->subband_num].width  = data;
            s->plane[s->channel_num].band[s->level][s->subband_num].stride = FFALIGN(data, 8);
            av_log(avctx, AV_LOG_DEBUG, "Highpass width2 %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass width2\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 50) {
            s->plane[s->channel_num].band[s->level][s->subband_num].height = data;
            av_log(avctx, AV_LOG_DEBUG, "Highpass height2 %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass height2\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 71) {
            s->codebook = data;
            av_log(avctx, AV_LOG_DEBUG, "Codebook %i\n", s->codebook);
        } else if (tag == 72) {
            s->codebook = data;
            av_log(avctx, AV_LOG_DEBUG, "Other codebook? %i\n", s->codebook);
        } else if (tag == 70) {
            av_log(avctx, AV_LOG_DEBUG, "Subsampling or bit-depth flag? %i\n", data);
            s->bpc = data;
            if (!(s->bpc == 10 || s->bpc == 12)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid bits per channel\n");
                ret = AVERROR(EINVAL);
                break;
            }
        } else if (tag == 84) {
            av_log(avctx, AV_LOG_DEBUG, "Sample format? %i\n", data);
            if (data == 1)
                s->coded_format = AV_PIX_FMT_YUV422P10;
            else if (data == 3)
                s->coded_format = AV_PIX_FMT_GBRP12;
            else if (data == 4)
                s->coded_format = AV_PIX_FMT_GBRAP12;
            else {
                avpriv_report_missing_feature(avctx, "Sample format of %"PRIu16, data);
                ret = AVERROR_PATCHWELCOME;
                break;
            }
            planes = av_pix_fmt_count_planes(s->coded_format);
        } else
            av_log(avctx, AV_LOG_DEBUG,  "Unknown tag %i data %x\n", tag, data);

        /* Some kind of end of header tag */
        if (tag == 4 && data == 0x1a4a && s->coded_width && s->coded_height &&
            s->coded_format != AV_PIX_FMT_NONE) {
            if (s->a_width != s->coded_width || s->a_height != s->coded_height ||
                s->a_format != s->coded_format) {
                free_buffers(avctx);
                if ((ret = alloc_buffers(avctx)) < 0) {
                    free_buffers(avctx);
                    return ret;
                }
            }
            ret = ff_set_dimensions(avctx, s->coded_width, s->coded_height);
            if (ret < 0)
                return ret;
            frame.f->width =
            frame.f->height = 0;

            if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
                return ret;

            s->coded_width = 0;
            s->coded_height = 0;
            s->coded_format = AV_PIX_FMT_NONE;
            got_buffer = 1;
        }
        coeff_data = s->plane[s->channel_num].subband[s->subband_num_actual];

        /* Lowpass coefficients */
        if (tag == 4 && data == 0xf0f && s->a_width && s->a_height) {
            int lowpass_height = s->plane[s->channel_num].band[0][0].height;
            int lowpass_width  = s->plane[s->channel_num].band[0][0].width;
            int lowpass_a_height = s->plane[s->channel_num].band[0][0].a_height;
            int lowpass_a_width  = s->plane[s->channel_num].band[0][0].a_width;

            if (!got_buffer) {
                av_log(avctx, AV_LOG_ERROR, "No end of header tag found\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (lowpass_height > lowpass_a_height || lowpass_width > lowpass_a_width ||
                lowpass_a_width * lowpass_a_height * sizeof(int16_t) > bytestream2_get_bytes_left(&gb)) {
                av_log(avctx, AV_LOG_ERROR, "Too many lowpass coefficients\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Start of lowpass coeffs component %d height:%d, width:%d\n", s->channel_num, lowpass_height, lowpass_width);
            for (i = 0; i < lowpass_height; i++) {
                for (j = 0; j < lowpass_width; j++)
                    coeff_data[j] = bytestream2_get_be16u(&gb);

                coeff_data += lowpass_width;
            }

            /* Align to mod-4 position to continue reading tags */
            bytestream2_seek(&gb, bytestream2_tell(&gb) & 3, SEEK_CUR);

            /* Copy last line of coefficients if odd height */
            if (lowpass_height & 1) {
                memcpy(&coeff_data[lowpass_height * lowpass_width],
                       &coeff_data[(lowpass_height - 1) * lowpass_width],
                       lowpass_width * sizeof(*coeff_data));
            }

            av_log(avctx, AV_LOG_DEBUG, "Lowpass coefficients %d\n", lowpass_width * lowpass_height);
        }

        if (tag == 55 && s->subband_num_actual != 255 && s->a_width && s->a_height) {
            int highpass_height = s->plane[s->channel_num].band[s->level][s->subband_num].height;
            int highpass_width  = s->plane[s->channel_num].band[s->level][s->subband_num].width;
            int highpass_a_width = s->plane[s->channel_num].band[s->level][s->subband_num].a_width;
            int highpass_a_height = s->plane[s->channel_num].band[s->level][s->subband_num].a_height;
            int highpass_stride = s->plane[s->channel_num].band[s->level][s->subband_num].stride;
            int expected = highpass_height * highpass_stride;
            int a_expected = highpass_a_height * highpass_a_width;
            int level, run, coeff;
            int count = 0, bytes;

            if (!got_buffer) {
                av_log(avctx, AV_LOG_ERROR, "No end of header tag found\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (highpass_height > highpass_a_height || highpass_width > highpass_a_width || a_expected < expected) {
                av_log(avctx, AV_LOG_ERROR, "Too many highpass coefficients\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Start subband coeffs plane %i level %i codebook %i expected %i\n", s->channel_num, s->level, s->codebook, expected);

            init_get_bits(&s->gb, gb.buffer, bytestream2_get_bytes_left(&gb) * 8);
            {
                OPEN_READER(re, &s->gb);
                if (!s->codebook) {
                    while (1) {
                        UPDATE_CACHE(re, &s->gb);
                        GET_RL_VLC(level, run, re, &s->gb, s->table_9_rl_vlc,
                                   VLC_BITS, 3, 1);

                        /* escape */
                        if (level == 64)
                            break;

                        count += run;

                        if (count > expected)
                            break;

                        coeff = dequant_and_decompand(level, s->quantisation);
                        for (i = 0; i < run; i++)
                            *coeff_data++ = coeff;
                    }
                } else {
                    while (1) {
                        UPDATE_CACHE(re, &s->gb);
                        GET_RL_VLC(level, run, re, &s->gb, s->table_18_rl_vlc,
                                   VLC_BITS, 3, 1);

                        /* escape */
                        if (level == 255 && run == 2)
                            break;

                        count += run;

                        if (count > expected)
                            break;

                        coeff = dequant_and_decompand(level, s->quantisation);
                        for (i = 0; i < run; i++)
                            *coeff_data++ = coeff;
                    }
                }
                CLOSE_READER(re, &s->gb);
            }

            if (count > expected) {
                av_log(avctx, AV_LOG_ERROR, "Escape codeword not found, probably corrupt data\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            bytes = FFALIGN(FF_CEIL_RSHIFT(get_bits_count(&s->gb), 3), 4);
            if (bytes > bytestream2_get_bytes_left(&gb)) {
                av_log(avctx, AV_LOG_ERROR, "Bitstream overread error\n");
                ret = AVERROR(EINVAL);
                goto end;
            } else
                bytestream2_seek(&gb, bytes, SEEK_CUR);

            av_log(avctx, AV_LOG_DEBUG, "End subband coeffs %i extra %i\n", count, count - expected);
            s->codebook = 0;

            /* Copy last line of coefficients if odd height */
            if (highpass_height & 1) {
                memcpy(&coeff_data[highpass_height * highpass_stride],
                       &coeff_data[(highpass_height - 1) * highpass_stride],
                       highpass_stride * sizeof(*coeff_data));
            }
        }
    }

    if (!s->a_width || !s->a_height || s->a_format == AV_PIX_FMT_NONE ||
        s->coded_width || s->coded_height || s->coded_format != AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid dimensions\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!got_buffer) {
        av_log(avctx, AV_LOG_ERROR, "No end of header tag found\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    planes = av_pix_fmt_count_planes(avctx->pix_fmt);
    for (plane = 0; plane < planes && !ret; plane++) {
        /* level 1 */
        int lowpass_height  = s->plane[plane].band[0][0].height;
        int lowpass_width   = s->plane[plane].band[0][0].width;
        int highpass_stride = s->plane[plane].band[0][1].stride;
        int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;
        int16_t *low, *high, *output, *dst;

        if (lowpass_height > s->plane[plane].band[0][0].a_height || lowpass_width > s->plane[plane].band[0][0].a_width ||
            !highpass_stride || s->plane[plane].band[0][1].width > s->plane[plane].band[0][1].a_width) {
            av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
            ret = AVERROR(EINVAL);
            goto end;
        }

        av_log(avctx, AV_LOG_DEBUG, "Decoding level 1 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

        low    = s->plane[plane].subband[0];
        high   = s->plane[plane].subband[2];
        output = s->plane[plane].l_h[0];
        for (i = 0; i < lowpass_width; i++) {
            vert_filter(output, lowpass_width, low, lowpass_width, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        low    = s->plane[plane].subband[1];
        high   = s->plane[plane].subband[3];
        output = s->plane[plane].l_h[1];

        for (i = 0; i < lowpass_width; i++) {
            // note the stride of "low" is highpass_stride
            vert_filter(output, lowpass_width, low, highpass_stride, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        low    = s->plane[plane].l_h[0];
        high   = s->plane[plane].l_h[1];
        output = s->plane[plane].subband[0];
        for (i = 0; i < lowpass_height * 2; i++) {
            horiz_filter(output, low, high, lowpass_width);
            low    += lowpass_width;
            high   += lowpass_width;
            output += lowpass_width * 2;
        }
        if (s->bpc == 12) {
            output = s->plane[plane].subband[0];
            for (i = 0; i < lowpass_height * 2; i++) {
                for (j = 0; j < lowpass_width * 2; j++)
                    output[j] <<= 2;

                output += lowpass_width * 2;
            }
        }

        /* level 2 */
        lowpass_height  = s->plane[plane].band[1][1].height;
        lowpass_width   = s->plane[plane].band[1][1].width;
        highpass_stride = s->plane[plane].band[1][1].stride;

        if (lowpass_height > s->plane[plane].band[1][1].a_height || lowpass_width > s->plane[plane].band[1][1].a_width ||
            !highpass_stride || s->plane[plane].band[1][1].width > s->plane[plane].band[1][1].a_width) {
            av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
            ret = AVERROR(EINVAL);
            goto end;
        }

        av_log(avctx, AV_LOG_DEBUG, "Level 2 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

        low    = s->plane[plane].subband[0];
        high   = s->plane[plane].subband[5];
        output = s->plane[plane].l_h[3];
        for (i = 0; i < lowpass_width; i++) {
            vert_filter(output, lowpass_width, low, lowpass_width, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        low    = s->plane[plane].subband[4];
        high   = s->plane[plane].subband[6];
        output = s->plane[plane].l_h[4];
        for (i = 0; i < lowpass_width; i++) {
            vert_filter(output, lowpass_width, low, highpass_stride, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        low    = s->plane[plane].l_h[3];
        high   = s->plane[plane].l_h[4];
        output = s->plane[plane].subband[0];
        for (i = 0; i < lowpass_height * 2; i++) {
            horiz_filter(output, low, high, lowpass_width);
            low    += lowpass_width;
            high   += lowpass_width;
            output += lowpass_width * 2;
        }

        output = s->plane[plane].subband[0];
        for (i = 0; i < lowpass_height * 2; i++) {
            for (j = 0; j < lowpass_width * 2; j++)
                output[j] <<= 2;

            output += lowpass_width * 2;
        }

        /* level 3 */
        lowpass_height  = s->plane[plane].band[2][1].height;
        lowpass_width   = s->plane[plane].band[2][1].width;
        highpass_stride = s->plane[plane].band[2][1].stride;

        if (lowpass_height > s->plane[plane].band[2][1].a_height || lowpass_width > s->plane[plane].band[2][1].a_width ||
            !highpass_stride || s->plane[plane].band[2][1].width > s->plane[plane].band[2][1].a_width) {
            av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
            ret = AVERROR(EINVAL);
            goto end;
        }

        av_log(avctx, AV_LOG_DEBUG, "Level 3 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

        low    = s->plane[plane].subband[0];
        high   = s->plane[plane].subband[8];
        output = s->plane[plane].l_h[6];
        for (i = 0; i < lowpass_width; i++) {
            vert_filter(output, lowpass_width, low, lowpass_width, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        low    = s->plane[plane].subband[7];
        high   = s->plane[plane].subband[9];
        output = s->plane[plane].l_h[7];
        for (i = 0; i < lowpass_width; i++) {
            vert_filter(output, lowpass_width, low, highpass_stride, high, highpass_stride, lowpass_height);
            low++;
            high++;
            output++;
        }

        dst = (int16_t *)pic->data[act_plane];
        low  = s->plane[plane].l_h[6];
        high = s->plane[plane].l_h[7];
        for (i = 0; i < lowpass_height * 2; i++) {
            horiz_filter_clip(dst, low, high, lowpass_width, s->bpc);
            low  += lowpass_width;
            high += lowpass_width;
            dst  += pic->linesize[act_plane] / 2;
        }
    }


end:
    if (ret < 0)
        return ret;

    *got_frame = 1;
    return avpkt->size;
}

static av_cold int cfhd_close_decoder(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;

    free_buffers(avctx);

    if (!avctx->internal->is_copy) {
        ff_free_vlc(&s->vlc_9);
        ff_free_vlc(&s->vlc_18);
    }

    return 0;
}

AVCodec ff_cfhd_decoder = {
    .name           = "cfhd",
    .long_name      = NULL_IF_CONFIG_SMALL("Cineform HD"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CFHD,
    .priv_data_size = sizeof(CFHDContext),
    .init           = cfhd_decode_init,
    .close          = cfhd_close_decoder,
    .decode         = cfhd_decode,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
