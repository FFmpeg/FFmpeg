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
 * Cineform HD video decoder
 */

#include "libavutil/attributes.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "thread.h"
#include "cfhd.h"

#define ALPHA_COMPAND_DC_OFFSET 256
#define ALPHA_COMPAND_GAIN 9400

static av_cold int cfhd_init(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;

    s->avctx                   = avctx;

    for (int i = 0; i < 64; i++) {
        int val = i;

        if (val >= 40) {
            if (val >= 54) {
                val -= 54;
                val <<= 2;
                val += 54;
            }

            val -= 40;
            val <<= 2;
            val += 40;
        }

        s->lut[0][i] = val;
    }

    for (int i = 0; i < 256; i++)
        s->lut[1][i] = i + ((768LL * i * i * i) / (256 * 256 * 256));

    return ff_cfhd_init_vlcs(s);
}

static void init_plane_defaults(CFHDContext *s)
{
    s->subband_num        = 0;
    s->level              = 0;
    s->subband_num_actual = 0;
}

static void init_peak_table_defaults(CFHDContext *s)
{
    s->peak.level  = 0;
    s->peak.offset = 0;
    memset(&s->peak.base, 0, sizeof(s->peak.base));
}

static void init_frame_defaults(CFHDContext *s)
{
    s->coded_width       = 0;
    s->coded_height      = 0;
    s->coded_format      = AV_PIX_FMT_YUV422P10;
    s->cropped_height    = 0;
    s->bpc               = 10;
    s->channel_cnt       = 3;
    s->subband_cnt       = SUBBAND_COUNT;
    s->channel_num       = 0;
    s->lowpass_precision = 16;
    s->quantisation      = 1;
    s->codebook          = 0;
    s->difference_coding = 0;
    s->frame_type        = 0;
    s->sample_type       = 0;
    if (s->transform_type != 2)
        s->transform_type = -1;
    init_plane_defaults(s);
    init_peak_table_defaults(s);
}

static inline int dequant_and_decompand(CFHDContext *s, int level, int quantisation, int codebook)
{
    if (codebook == 0 || codebook == 1) {
        return s->lut[codebook][abs(level)] * FFSIGN(level) * quantisation;
    } else
        return level * quantisation;
}

static inline void difference_coding(int16_t *band, int width, int height)
{

    int i,j;
    for (i = 0; i < height; i++) {
        for (j = 1; j < width; j++) {
          band[j] += band[j-1];
        }
        band += width;
    }
}

static inline void peak_table(int16_t *band, Peak *peak, int length)
{
    int i;
    for (i = 0; i < length; i++)
        if (abs(band[i]) > peak->level)
            band[i] = bytestream2_get_le16(&peak->base);
}

static inline void process_alpha(int16_t *alpha, int width)
{
    int i, channel;
    for (i = 0; i < width; i++) {
        channel   = alpha[i];
        channel  -= ALPHA_COMPAND_DC_OFFSET;
        channel <<= 3;
        channel  *= ALPHA_COMPAND_GAIN;
        channel >>= 16;
        channel   = av_clip_uintp2(channel, 12);
        alpha[i]  = channel;
    }
}

static inline void process_bayer(AVFrame *frame, int bpc)
{
    const int linesize = frame->linesize[0];
    uint16_t *r = (uint16_t *)frame->data[0];
    uint16_t *g1 = (uint16_t *)(frame->data[0] + 2);
    uint16_t *g2 = (uint16_t *)(frame->data[0] + frame->linesize[0]);
    uint16_t *b = (uint16_t *)(frame->data[0] + frame->linesize[0] + 2);
    const int mid = 1 << (bpc - 1);
    const int factor = 1 << (16 - bpc);

    for (int y = 0; y < frame->height >> 1; y++) {
        for (int x = 0; x < frame->width; x += 2) {
            int R, G1, G2, B;
            int g, rg, bg, gd;

            g  = r[x];
            rg = g1[x];
            bg = g2[x];
            gd = b[x];
            gd -= mid;

            R  = (rg - mid) * 2 + g;
            G1 = g + gd;
            G2 = g - gd;
            B  = (bg - mid) * 2 + g;

            R  = av_clip_uintp2(R  * factor, 16);
            G1 = av_clip_uintp2(G1 * factor, 16);
            G2 = av_clip_uintp2(G2 * factor, 16);
            B  = av_clip_uintp2(B  * factor, 16);

            r[x]  = R;
            g1[x] = G1;
            g2[x] = G2;
            b[x]  = B;
        }

        r  += linesize;
        g1 += linesize;
        g2 += linesize;
        b  += linesize;
    }
}

static inline void interlaced_vertical_filter(int16_t *output, int16_t *low, int16_t *high,
                         int width, int linesize, int plane)
{
    int i;
    int16_t even, odd;
    for (i = 0; i < width; i++) {
        even = (low[i] - high[i])/2;
        odd  = (low[i] + high[i])/2;
        output[i]            = av_clip_uintp2(even, 10);
        output[i + linesize] = av_clip_uintp2(odd, 10);
    }
}

static inline void inverse_temporal_filter(int16_t *low, int16_t *high, int width)
{
    for (int i = 0; i < width; i++) {
        int even = (low[i] - high[i]) / 2;
        int odd  = (low[i] + high[i]) / 2;

        low[i]  = even;
        high[i] = odd;
    }
}

static void free_buffers(CFHDContext *s)
{
    int i, j;

    for (i = 0; i < FF_ARRAY_ELEMS(s->plane); i++) {
        av_freep(&s->plane[i].idwt_buf);
        av_freep(&s->plane[i].idwt_tmp);
        s->plane[i].idwt_size = 0;

        for (j = 0; j < SUBBAND_COUNT_3D; j++)
            s->plane[i].subband[j] = NULL;

        for (j = 0; j < 10; j++)
            s->plane[i].l_h[j] = NULL;
    }
    s->a_height = 0;
    s->a_width  = 0;
}

static int alloc_buffers(AVCodecContext *avctx)
{
    CFHDContext *s = avctx->priv_data;
    int i, j, ret, planes, bayer = 0;
    int chroma_x_shift, chroma_y_shift;
    unsigned k;

    if ((ret = ff_set_dimensions(avctx, s->coded_width, s->coded_height)) < 0)
        return ret;
    avctx->pix_fmt = s->coded_format;

    ff_cfhddsp_init(&s->dsp, s->bpc, avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16);

    if ((ret = av_pix_fmt_get_chroma_sub_sample(s->coded_format,
                                                &chroma_x_shift,
                                                &chroma_y_shift)) < 0)
        return ret;
    planes = av_pix_fmt_count_planes(s->coded_format);
    if (s->coded_format == AV_PIX_FMT_BAYER_RGGB16) {
        planes = 4;
        chroma_x_shift = 1;
        chroma_y_shift = 1;
        bayer = 1;
    }

    for (i = 0; i < planes; i++) {
        int w8, h8, w4, h4, w2, h2;
        int width  = (i || bayer) ? s->coded_width  >> chroma_x_shift : s->coded_width;
        int height = (i || bayer) ? s->coded_height >> chroma_y_shift : s->coded_height;
        ptrdiff_t stride = (FFALIGN(width  / 8, 8) + 64) * 8;

        if (chroma_y_shift && !bayer)
            height = FFALIGN(height / 8, 2) * 8;
        s->plane[i].width  = width;
        s->plane[i].height = height;
        s->plane[i].stride = stride;

        w8 = FFALIGN(s->plane[i].width  / 8, 8) + 64;
        h8 = FFALIGN(height, 8) / 8;
        w4 = w8 * 2;
        h4 = h8 * 2;
        w2 = w4 * 2;
        h2 = h4 * 2;

        if (s->transform_type == 0) {
            s->plane[i].idwt_size = FFALIGN(height, 8) * stride;
            s->plane[i].idwt_buf =
                av_mallocz_array(s->plane[i].idwt_size, sizeof(*s->plane[i].idwt_buf));
            s->plane[i].idwt_tmp =
                av_malloc_array(s->plane[i].idwt_size, sizeof(*s->plane[i].idwt_tmp));
        } else {
            s->plane[i].idwt_size = FFALIGN(height, 8) * stride * 2;
            s->plane[i].idwt_buf =
                av_mallocz_array(s->plane[i].idwt_size, sizeof(*s->plane[i].idwt_buf));
            s->plane[i].idwt_tmp =
                av_malloc_array(s->plane[i].idwt_size, sizeof(*s->plane[i].idwt_tmp));
        }

        if (!s->plane[i].idwt_buf || !s->plane[i].idwt_tmp)
            return AVERROR(ENOMEM);

        s->plane[i].subband[0] = s->plane[i].idwt_buf;
        s->plane[i].subband[1] = s->plane[i].idwt_buf + 2 * w8 * h8;
        s->plane[i].subband[2] = s->plane[i].idwt_buf + 1 * w8 * h8;
        s->plane[i].subband[3] = s->plane[i].idwt_buf + 3 * w8 * h8;
        s->plane[i].subband[4] = s->plane[i].idwt_buf + 2 * w4 * h4;
        s->plane[i].subband[5] = s->plane[i].idwt_buf + 1 * w4 * h4;
        s->plane[i].subband[6] = s->plane[i].idwt_buf + 3 * w4 * h4;
        if (s->transform_type == 0) {
            s->plane[i].subband[7] = s->plane[i].idwt_buf + 2 * w2 * h2;
            s->plane[i].subband[8] = s->plane[i].idwt_buf + 1 * w2 * h2;
            s->plane[i].subband[9] = s->plane[i].idwt_buf + 3 * w2 * h2;
        } else {
            int16_t *frame2 =
            s->plane[i].subband[7]  = s->plane[i].idwt_buf + 4 * w2 * h2;
            s->plane[i].subband[8]  = frame2 + 2 * w4 * h4;
            s->plane[i].subband[9]  = frame2 + 1 * w4 * h4;
            s->plane[i].subband[10] = frame2 + 3 * w4 * h4;
            s->plane[i].subband[11] = frame2 + 2 * w2 * h2;
            s->plane[i].subband[12] = frame2 + 1 * w2 * h2;
            s->plane[i].subband[13] = frame2 + 3 * w2 * h2;
            s->plane[i].subband[14] = s->plane[i].idwt_buf + 2 * w2 * h2;
            s->plane[i].subband[15] = s->plane[i].idwt_buf + 1 * w2 * h2;
            s->plane[i].subband[16] = s->plane[i].idwt_buf + 3 * w2 * h2;
        }

        if (s->transform_type == 0) {
            for (j = 0; j < DWT_LEVELS; j++) {
                for (k = 0; k < FF_ARRAY_ELEMS(s->plane[i].band[j]); k++) {
                    s->plane[i].band[j][k].a_width  = w8 << j;
                    s->plane[i].band[j][k].a_height = h8 << j;
                }
            }
        } else {
            for (j = 0; j < DWT_LEVELS_3D; j++) {
                int t = j < 1 ? 0 : (j < 3 ? 1 : 2);

                for (k = 0; k < FF_ARRAY_ELEMS(s->plane[i].band[j]); k++) {
                    s->plane[i].band[j][k].a_width  = w8 << t;
                    s->plane[i].band[j][k].a_height = h8 << t;
                }
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
        if (s->transform_type != 0) {
            int16_t *frame2 = s->plane[i].idwt_tmp + 4 * w2 * h2;

            s->plane[i].l_h[8] = frame2;
            s->plane[i].l_h[9] = frame2 + 2 * w2 * h2;
        }
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
    CFHDDSPContext *dsp = &s->dsp;
    GetByteContext gb;
    ThreadFrame frame = { .f = data };
    AVFrame *pic = data;
    int ret = 0, i, j, plane, got_buffer = 0;
    int16_t *coeff_data;

    init_frame_defaults(s);
    s->planes = av_pix_fmt_count_planes(s->coded_format);

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    while (bytestream2_get_bytes_left(&gb) >= 4) {
        /* Bit weird but implement the tag parsing as the spec says */
        uint16_t tagu   = bytestream2_get_be16(&gb);
        int16_t tag     = (int16_t)tagu;
        int8_t tag8     = (int8_t)(tagu >> 8);
        uint16_t abstag = abs(tag);
        int8_t abs_tag8 = abs(tag8);
        uint16_t data   = bytestream2_get_be16(&gb);
        if (abs_tag8 >= 0x60 && abs_tag8 <= 0x6f) {
            av_log(avctx, AV_LOG_DEBUG, "large len %x\n", ((tagu & 0xff) << 16) | data);
        } else if (tag == SampleFlags) {
            av_log(avctx, AV_LOG_DEBUG, "Progressive? %"PRIu16"\n", data);
            s->progressive = data & 0x0001;
        } else if (tag == FrameType) {
            s->frame_type = data;
            av_log(avctx, AV_LOG_DEBUG, "Frame type %"PRIu16"\n", data);
        } else if (abstag == VersionMajor) {
            av_log(avctx, AV_LOG_DEBUG, "Version major %"PRIu16"\n", data);
        } else if (abstag == VersionMinor) {
            av_log(avctx, AV_LOG_DEBUG, "Version minor %"PRIu16"\n", data);
        } else if (abstag == VersionRevision) {
            av_log(avctx, AV_LOG_DEBUG, "Version revision %"PRIu16"\n", data);
        } else if (abstag == VersionEdit) {
            av_log(avctx, AV_LOG_DEBUG, "Version edit %"PRIu16"\n", data);
        } else if (abstag == Version) {
            av_log(avctx, AV_LOG_DEBUG, "Version %"PRIu16"\n", data);
        } else if (tag == ImageWidth) {
            av_log(avctx, AV_LOG_DEBUG, "Width %"PRIu16"\n", data);
            s->coded_width = data;
        } else if (tag == ImageHeight) {
            av_log(avctx, AV_LOG_DEBUG, "Height %"PRIu16"\n", data);
            s->coded_height = data;
        } else if (tag == ChannelCount) {
            av_log(avctx, AV_LOG_DEBUG, "Channel Count: %"PRIu16"\n", data);
            s->channel_cnt = data;
            if (data > 4) {
                av_log(avctx, AV_LOG_ERROR, "Channel Count of %"PRIu16" is unsupported\n", data);
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }
        } else if (tag == SubbandCount) {
            av_log(avctx, AV_LOG_DEBUG, "Subband Count: %"PRIu16"\n", data);
            if (data != SUBBAND_COUNT && data != SUBBAND_COUNT_3D) {
                av_log(avctx, AV_LOG_ERROR, "Subband Count of %"PRIu16" is unsupported\n", data);
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }
        } else if (tag == ChannelNumber) {
            s->channel_num = data;
            av_log(avctx, AV_LOG_DEBUG, "Channel number %"PRIu16"\n", data);
            if (s->channel_num >= s->planes) {
                av_log(avctx, AV_LOG_ERROR, "Invalid channel number\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            init_plane_defaults(s);
        } else if (tag == SubbandNumber) {
            if (s->subband_num != 0 && data == 1 && (s->transform_type == 0 || s->transform_type == 2))  // hack
                s->level++;
            av_log(avctx, AV_LOG_DEBUG, "Subband number %"PRIu16"\n", data);
            s->subband_num = data;
            if ((s->transform_type == 0 && s->level >= DWT_LEVELS) ||
                (s->transform_type == 2 && s->level >= DWT_LEVELS_3D)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid level\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            if (s->subband_num > 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid subband number\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
        } else if (tag == SubbandBand) {
            av_log(avctx, AV_LOG_DEBUG, "Subband number actual %"PRIu16"\n", data);
            if ((s->transform_type == 0 && data >= SUBBAND_COUNT) ||
                (s->transform_type == 2 && data >= SUBBAND_COUNT_3D && data != 255)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid subband number actual\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            if (s->transform_type == 0 || s->transform_type == 2)
                s->subband_num_actual = data;
            else
                av_log(avctx, AV_LOG_WARNING, "Ignoring subband num actual %"PRIu16"\n", data);
        } else if (tag == LowpassPrecision)
            av_log(avctx, AV_LOG_DEBUG, "Lowpass precision bits: %"PRIu16"\n", data);
        else if (tag == Quantization) {
            s->quantisation = data;
            av_log(avctx, AV_LOG_DEBUG, "Quantisation: %"PRIu16"\n", data);
        } else if (tag == PrescaleTable) {
            for (i = 0; i < 8; i++)
                s->prescale_table[i] = (data >> (14 - i * 2)) & 0x3;
            av_log(avctx, AV_LOG_DEBUG, "Prescale table: %x\n", data);
        } else if (tag == BandEncoding) {
            if (!data || data > 5) {
                av_log(avctx, AV_LOG_ERROR, "Invalid band encoding\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            s->band_encoding = data;
            av_log(avctx, AV_LOG_DEBUG, "Encode Method for Subband %d : %x\n", s->subband_num_actual, data);
        } else if (tag == LowpassWidth) {
            av_log(avctx, AV_LOG_DEBUG, "Lowpass width %"PRIu16"\n", data);
            s->plane[s->channel_num].band[0][0].width  = data;
            s->plane[s->channel_num].band[0][0].stride = data;
        } else if (tag == LowpassHeight) {
            av_log(avctx, AV_LOG_DEBUG, "Lowpass height %"PRIu16"\n", data);
            s->plane[s->channel_num].band[0][0].height = data;
        } else if (tag == SampleType) {
            s->sample_type = data;
            av_log(avctx, AV_LOG_DEBUG, "Sample type? %"PRIu16"\n", data);
        } else if (tag == TransformType) {
            if (data > 2) {
                av_log(avctx, AV_LOG_ERROR, "Invalid transform type\n");
                ret = AVERROR(EINVAL);
                goto end;
            } else if (data == 1) {
                av_log(avctx, AV_LOG_ERROR, "unsupported transform type\n");
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }
            if (s->transform_type == -1) {
                s->transform_type = data;
                av_log(avctx, AV_LOG_DEBUG, "Transform type %"PRIu16"\n", data);
            } else {
                av_log(avctx, AV_LOG_DEBUG, "Ignoring additional transform type %"PRIu16"\n", data);
            }
        } else if (abstag >= 0x4000 && abstag <= 0x40ff) {
            if (abstag == 0x4001)
                s->peak.level = 0;
            av_log(avctx, AV_LOG_DEBUG, "Small chunk length %d %s\n", data * 4, tag < 0 ? "optional" : "required");
            bytestream2_skipu(&gb, data * 4);
        } else if (tag == FrameIndex) {
            av_log(avctx, AV_LOG_DEBUG, "Frame index %"PRIu16"\n", data);
            s->frame_index = data;
        } else if (tag == SampleIndexTable) {
            av_log(avctx, AV_LOG_DEBUG, "Sample index table - skipping %i values\n", data);
            if (data > bytestream2_get_bytes_left(&gb) / 4) {
                av_log(avctx, AV_LOG_ERROR, "too many values (%d)\n", data);
                ret = AVERROR_INVALIDDATA;
                goto end;
            }
            for (i = 0; i < data; i++) {
                uint32_t offset = bytestream2_get_be32(&gb);
                av_log(avctx, AV_LOG_DEBUG, "Offset = %"PRIu32"\n", offset);
            }
        } else if (tag == HighpassWidth) {
            av_log(avctx, AV_LOG_DEBUG, "Highpass width %i channel %i level %i subband %i\n", data, s->channel_num, s->level, s->subband_num);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass width\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            s->plane[s->channel_num].band[s->level][s->subband_num].width  = data;
            s->plane[s->channel_num].band[s->level][s->subband_num].stride = FFALIGN(data, 8);
        } else if (tag == HighpassHeight) {
            av_log(avctx, AV_LOG_DEBUG, "Highpass height %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass height\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            s->plane[s->channel_num].band[s->level][s->subband_num].height = data;
        } else if (tag == BandWidth) {
            av_log(avctx, AV_LOG_DEBUG, "Highpass width2 %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass width2\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            s->plane[s->channel_num].band[s->level][s->subband_num].width  = data;
            s->plane[s->channel_num].band[s->level][s->subband_num].stride = FFALIGN(data, 8);
        } else if (tag == BandHeight) {
            av_log(avctx, AV_LOG_DEBUG, "Highpass height2 %i\n", data);
            if (data < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid highpass height2\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            s->plane[s->channel_num].band[s->level][s->subband_num].height = data;
        } else if (tag == InputFormat) {
            av_log(avctx, AV_LOG_DEBUG, "Input format %i\n", data);
            if (s->coded_format == AV_PIX_FMT_NONE ||
                s->coded_format == AV_PIX_FMT_YUV422P10) {
                if (data >= 100 && data <= 105) {
                    s->coded_format = AV_PIX_FMT_BAYER_RGGB16;
                } else if (data >= 122 && data <= 128) {
                    s->coded_format = AV_PIX_FMT_GBRP12;
                } else if (data == 30) {
                    s->coded_format = AV_PIX_FMT_GBRAP12;
                } else {
                    s->coded_format = AV_PIX_FMT_YUV422P10;
                }
                s->planes = s->coded_format == AV_PIX_FMT_BAYER_RGGB16 ? 4 : av_pix_fmt_count_planes(s->coded_format);
            }
        } else if (tag == BandCodingFlags) {
            s->codebook = data & 0xf;
            s->difference_coding = (data >> 4) & 1;
            av_log(avctx, AV_LOG_DEBUG, "Other codebook? %i\n", s->codebook);
        } else if (tag == Precision) {
            av_log(avctx, AV_LOG_DEBUG, "Precision %i\n", data);
            if (!(data == 10 || data == 12)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid bits per channel\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            avctx->bits_per_raw_sample = s->bpc = data;
        } else if (tag == EncodedFormat) {
            av_log(avctx, AV_LOG_DEBUG, "Sample format? %i\n", data);
            if (data == 1) {
                s->coded_format = AV_PIX_FMT_YUV422P10;
            } else if (data == 2) {
                s->coded_format = AV_PIX_FMT_BAYER_RGGB16;
            } else if (data == 3) {
                s->coded_format = AV_PIX_FMT_GBRP12;
            } else if (data == 4) {
                s->coded_format = AV_PIX_FMT_GBRAP12;
            } else {
                avpriv_report_missing_feature(avctx, "Sample format of %"PRIu16, data);
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }
            s->planes = data == 2 ? 4 : av_pix_fmt_count_planes(s->coded_format);
        } else if (tag == -DisplayHeight) {
            av_log(avctx, AV_LOG_DEBUG, "Cropped height %"PRIu16"\n", data);
            s->cropped_height = data;
        } else if (tag == -PeakOffsetLow) {
            s->peak.offset &= ~0xffff;
            s->peak.offset |= (data & 0xffff);
            s->peak.base    = gb;
            s->peak.level   = 0;
        } else if (tag == -PeakOffsetHigh) {
            s->peak.offset &= 0xffff;
            s->peak.offset |= (data & 0xffffU)<<16;
            s->peak.base    = gb;
            s->peak.level   = 0;
        } else if (tag == -PeakLevel && s->peak.offset) {
            s->peak.level = data;
            bytestream2_seek(&s->peak.base, s->peak.offset - 4, SEEK_CUR);
        } else
            av_log(avctx, AV_LOG_DEBUG,  "Unknown tag %i data %x\n", tag, data);

        if (tag == BitstreamMarker && data == 0xf0f &&
            s->coded_format != AV_PIX_FMT_NONE) {
            int lowpass_height = s->plane[s->channel_num].band[0][0].height;
            int lowpass_width  = s->plane[s->channel_num].band[0][0].width;
            int factor = s->coded_format == AV_PIX_FMT_BAYER_RGGB16 ? 2 : 1;

            if (s->coded_width) {
                s->coded_width *= factor;
            }

            if (s->coded_height) {
                s->coded_height *= factor;
            }

            if (!s->a_width && !s->coded_width) {
                s->coded_width = lowpass_width * factor * 8;
            }

            if (!s->a_height && !s->coded_height) {
                s->coded_height = lowpass_height * factor * 8;
            }

            if (s->a_width && !s->coded_width)
                s->coded_width = s->a_width;
            if (s->a_height && !s->coded_height)
                s->coded_height = s->a_height;

            if (s->a_width != s->coded_width || s->a_height != s->coded_height ||
                s->a_format != s->coded_format) {
                free_buffers(s);
                if ((ret = alloc_buffers(avctx)) < 0) {
                    free_buffers(s);
                    return ret;
                }
            }
            ret = ff_set_dimensions(avctx, s->coded_width, s->coded_height);
            if (ret < 0)
                return ret;
            if (s->cropped_height) {
                unsigned height = s->cropped_height << (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16);
                if (avctx->height < height)
                    return AVERROR_INVALIDDATA;
                avctx->height = height;
            }
            frame.f->width =
            frame.f->height = 0;

            if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
                return ret;

            s->coded_width = 0;
            s->coded_height = 0;
            s->coded_format = AV_PIX_FMT_NONE;
            got_buffer = 1;
        } else if (tag == FrameIndex && data == 1 && s->sample_type == 1 && s->frame_type == 2) {
            frame.f->width =
            frame.f->height = 0;

            if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
                return ret;
            s->coded_width = 0;
            s->coded_height = 0;
            s->coded_format = AV_PIX_FMT_NONE;
            got_buffer = 1;
        }

        if (s->subband_num_actual == 255)
            goto finish;
        coeff_data = s->plane[s->channel_num].subband[s->subband_num_actual];

        /* Lowpass coefficients */
        if (tag == BitstreamMarker && data == 0xf0f && s->a_width && s->a_height) {
            int lowpass_height = s->plane[s->channel_num].band[0][0].height;
            int lowpass_width  = s->plane[s->channel_num].band[0][0].width;
            int lowpass_a_height = s->plane[s->channel_num].band[0][0].a_height;
            int lowpass_a_width  = s->plane[s->channel_num].band[0][0].a_width;

            if (lowpass_width < 3 ||
                lowpass_width > lowpass_a_width) {
                av_log(avctx, AV_LOG_ERROR, "Invalid lowpass width\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (lowpass_height < 3 ||
                lowpass_height > lowpass_a_height) {
                av_log(avctx, AV_LOG_ERROR, "Invalid lowpass height\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (!got_buffer) {
                av_log(avctx, AV_LOG_ERROR, "No end of header tag found\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (lowpass_height > lowpass_a_height || lowpass_width > lowpass_a_width ||
                lowpass_width * lowpass_height * sizeof(int16_t) > bytestream2_get_bytes_left(&gb)) {
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

        if ((tag == BandHeader || tag == BandSecondPass) && s->subband_num_actual != 255 && s->a_width && s->a_height) {
            int highpass_height = s->plane[s->channel_num].band[s->level][s->subband_num].height;
            int highpass_width  = s->plane[s->channel_num].band[s->level][s->subband_num].width;
            int highpass_a_width = s->plane[s->channel_num].band[s->level][s->subband_num].a_width;
            int highpass_a_height = s->plane[s->channel_num].band[s->level][s->subband_num].a_height;
            int highpass_stride = s->plane[s->channel_num].band[s->level][s->subband_num].stride;
            int expected;
            int a_expected = highpass_a_height * highpass_a_width;
            int level, run, coeff;
            int count = 0, bytes;

            if (!got_buffer) {
                av_log(avctx, AV_LOG_ERROR, "No end of header tag found\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (highpass_height > highpass_a_height || highpass_width > highpass_a_width || a_expected < highpass_height * (uint64_t)highpass_stride) {
                av_log(avctx, AV_LOG_ERROR, "Too many highpass coefficients\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            expected = highpass_height * highpass_stride;

            av_log(avctx, AV_LOG_DEBUG, "Start subband coeffs plane %i level %i codebook %i expected %i\n", s->channel_num, s->level, s->codebook, expected);

            ret = init_get_bits8(&s->gb, gb.buffer, bytestream2_get_bytes_left(&gb));
            if (ret < 0)
                goto end;
            {
                OPEN_READER(re, &s->gb);

                const int lossless = s->band_encoding == 5;

                if (s->codebook == 0 && s->transform_type == 2 && s->subband_num_actual == 7)
                    s->codebook = 1;
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

                        if (!lossless)
                            coeff = dequant_and_decompand(s, level, s->quantisation, 0);
                        else
                            coeff = level;
                        if (tag == BandSecondPass) {
                            const uint16_t q = s->quantisation;

                            for (i = 0; i < run; i++) {
                                *coeff_data |= coeff << 8;
                                *coeff_data++ *= q;
                            }
                        } else {
                            for (i = 0; i < run; i++)
                                *coeff_data++ = coeff;
                        }
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

                        if (!lossless)
                            coeff = dequant_and_decompand(s, level, s->quantisation, s->codebook);
                        else
                            coeff = level;
                        if (tag == BandSecondPass) {
                            const uint16_t q = s->quantisation;

                            for (i = 0; i < run; i++) {
                                *coeff_data |= coeff << 8;
                                *coeff_data++ *= q;
                            }
                        } else {
                            for (i = 0; i < run; i++)
                                *coeff_data++ = coeff;
                        }
                    }
                }
                CLOSE_READER(re, &s->gb);
            }

            if (count > expected) {
                av_log(avctx, AV_LOG_ERROR, "Escape codeword not found, probably corrupt data\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
            if (s->peak.level)
                peak_table(coeff_data - count, &s->peak, count);
            if (s->difference_coding)
                difference_coding(s->plane[s->channel_num].subband[s->subband_num_actual], highpass_width, highpass_height);

            bytes = FFALIGN(AV_CEIL_RSHIFT(get_bits_count(&s->gb), 3), 4);
            if (bytes > bytestream2_get_bytes_left(&gb)) {
                av_log(avctx, AV_LOG_ERROR, "Bitstream overread error\n");
                ret = AVERROR(EINVAL);
                goto end;
            } else
                bytestream2_seek(&gb, bytes, SEEK_CUR);

            av_log(avctx, AV_LOG_DEBUG, "End subband coeffs %i extra %i\n", count, count - expected);
finish:
            if (s->subband_num_actual != 255)
                s->codebook = 0;
        }
    }

    s->planes = av_pix_fmt_count_planes(avctx->pix_fmt);
    if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
        s->progressive = 1;
        s->planes = 4;
    }

    ff_thread_finish_setup(avctx);

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

    if (s->transform_type == 0 && s->sample_type != 1) {
        for (plane = 0; plane < s->planes && !ret; plane++) {
            /* level 1 */
            int lowpass_height  = s->plane[plane].band[0][0].height;
            int output_stride   = s->plane[plane].band[0][0].a_width;
            int lowpass_width   = s->plane[plane].band[0][0].width;
            int highpass_stride = s->plane[plane].band[0][1].stride;
            int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;
            ptrdiff_t dst_linesize;
            int16_t *low, *high, *output, *dst;

            if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                act_plane = 0;
                dst_linesize = pic->linesize[act_plane];
            } else {
                dst_linesize = pic->linesize[act_plane] / 2;
            }

            if (lowpass_height > s->plane[plane].band[0][0].a_height || lowpass_width > s->plane[plane].band[0][0].a_width ||
                !highpass_stride || s->plane[plane].band[0][1].width > s->plane[plane].band[0][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Decoding level 1 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

            low    = s->plane[plane].subband[0];
            high   = s->plane[plane].subband[2];
            output = s->plane[plane].l_h[0];
            dsp->vert_filter(output, output_stride, low, lowpass_width, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].subband[1];
            high   = s->plane[plane].subband[3];
            output = s->plane[plane].l_h[1];

            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].l_h[0];
            high   = s->plane[plane].l_h[1];
            output = s->plane[plane].subband[0];
            dsp->horiz_filter(output, output_stride, low, output_stride, high, output_stride, lowpass_width, lowpass_height * 2);
            if (s->bpc == 12) {
                output = s->plane[plane].subband[0];
                for (i = 0; i < lowpass_height * 2; i++) {
                    for (j = 0; j < lowpass_width * 2; j++)
                        output[j] *= 4;

                    output += output_stride * 2;
                }
            }

            /* level 2 */
            lowpass_height  = s->plane[plane].band[1][1].height;
            output_stride   = s->plane[plane].band[1][1].a_width;
            lowpass_width   = s->plane[plane].band[1][1].width;
            highpass_stride = s->plane[plane].band[1][1].stride;

            if (lowpass_height > s->plane[plane].band[1][1].a_height || lowpass_width > s->plane[plane].band[1][1].a_width ||
                !highpass_stride || s->plane[plane].band[1][1].width > s->plane[plane].band[1][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Level 2 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

            low    = s->plane[plane].subband[0];
            high   = s->plane[plane].subband[5];
            output = s->plane[plane].l_h[3];
            dsp->vert_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].subband[4];
            high   = s->plane[plane].subband[6];
            output = s->plane[plane].l_h[4];
            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].l_h[3];
            high   = s->plane[plane].l_h[4];
            output = s->plane[plane].subband[0];
            dsp->horiz_filter(output, output_stride, low, output_stride, high, output_stride, lowpass_width, lowpass_height * 2);

            output = s->plane[plane].subband[0];
            for (i = 0; i < lowpass_height * 2; i++) {
                for (j = 0; j < lowpass_width * 2; j++)
                    output[j] *= 4;

                output += output_stride * 2;
            }

            /* level 3 */
            lowpass_height  = s->plane[plane].band[2][1].height;
            output_stride   = s->plane[plane].band[2][1].a_width;
            lowpass_width   = s->plane[plane].band[2][1].width;
            highpass_stride = s->plane[plane].band[2][1].stride;

            if (lowpass_height > s->plane[plane].band[2][1].a_height || lowpass_width > s->plane[plane].band[2][1].a_width ||
                !highpass_stride || s->plane[plane].band[2][1].width > s->plane[plane].band[2][1].a_width ||
                lowpass_height < 3 || lowpass_width < 3 || lowpass_width * 2 > s->plane[plane].width) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Level 3 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);
            if (s->progressive) {
                low    = s->plane[plane].subband[0];
                high   = s->plane[plane].subband[8];
                output = s->plane[plane].l_h[6];
                dsp->vert_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[7];
                high   = s->plane[plane].subband[9];
                output = s->plane[plane].l_h[7];
                dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                dst = (int16_t *)pic->data[act_plane];
                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                    if (plane & 1)
                        dst++;
                    if (plane > 1)
                        dst += pic->linesize[act_plane] >> 1;
                }
                low  = s->plane[plane].l_h[6];
                high = s->plane[plane].l_h[7];

                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16 &&
                    (lowpass_height * 2 > avctx->coded_height / 2 ||
                     lowpass_width  * 2 > avctx->coded_width  / 2    )
                    ) {
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                }

                for (i = 0; i < s->plane[act_plane].height; i++) {
                    dsp->horiz_filter_clip(dst, low, high, lowpass_width, s->bpc);
                    if (avctx->pix_fmt == AV_PIX_FMT_GBRAP12 && act_plane == 3)
                        process_alpha(dst, lowpass_width * 2);
                    low  += output_stride;
                    high += output_stride;
                    dst  += dst_linesize;
                }
            } else {
                av_log(avctx, AV_LOG_DEBUG, "interlaced frame ? %d", pic->interlaced_frame);
                pic->interlaced_frame = 1;
                low    = s->plane[plane].subband[0];
                high   = s->plane[plane].subband[7];
                output = s->plane[plane].l_h[6];
                dsp->horiz_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[8];
                high   = s->plane[plane].subband[9];
                output = s->plane[plane].l_h[7];
                dsp->horiz_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                dst  = (int16_t *)pic->data[act_plane];
                low  = s->plane[plane].l_h[6];
                high = s->plane[plane].l_h[7];
                for (i = 0; i < s->plane[act_plane].height / 2; i++) {
                    interlaced_vertical_filter(dst, low, high, lowpass_width * 2,  pic->linesize[act_plane]/2, act_plane);
                    low  += output_stride * 2;
                    high += output_stride * 2;
                    dst  += pic->linesize[act_plane];
                }
            }
        }
    } else if (s->transform_type == 2 && (avctx->internal->is_copy || s->frame_index == 1 || s->sample_type != 1)) {
        for (plane = 0; plane < s->planes && !ret; plane++) {
            int lowpass_height  = s->plane[plane].band[0][0].height;
            int output_stride   = s->plane[plane].band[0][0].a_width;
            int lowpass_width   = s->plane[plane].band[0][0].width;
            int highpass_stride = s->plane[plane].band[0][1].stride;
            int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;
            int16_t *low, *high, *output, *dst;
            ptrdiff_t dst_linesize;

            if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                act_plane = 0;
                dst_linesize = pic->linesize[act_plane];
            } else {
                dst_linesize = pic->linesize[act_plane] / 2;
            }

            if (lowpass_height > s->plane[plane].band[0][0].a_height || lowpass_width > s->plane[plane].band[0][0].a_width ||
                !highpass_stride || s->plane[plane].band[0][1].width > s->plane[plane].band[0][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Decoding level 1 plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

            low    = s->plane[plane].subband[0];
            high   = s->plane[plane].subband[2];
            output = s->plane[plane].l_h[0];
            dsp->vert_filter(output, output_stride, low, lowpass_width, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].subband[1];
            high   = s->plane[plane].subband[3];
            output = s->plane[plane].l_h[1];
            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].l_h[0];
            high   = s->plane[plane].l_h[1];
            output = s->plane[plane].l_h[7];
            dsp->horiz_filter(output, output_stride, low, output_stride, high, output_stride, lowpass_width, lowpass_height * 2);
            if (s->bpc == 12) {
                output = s->plane[plane].l_h[7];
                for (i = 0; i < lowpass_height * 2; i++) {
                    for (j = 0; j < lowpass_width * 2; j++)
                        output[j] *= 4;

                    output += output_stride * 2;
                }
            }

            lowpass_height  = s->plane[plane].band[1][1].height;
            output_stride   = s->plane[plane].band[1][1].a_width;
            lowpass_width   = s->plane[plane].band[1][1].width;
            highpass_stride = s->plane[plane].band[1][1].stride;

            if (lowpass_height > s->plane[plane].band[1][1].a_height || lowpass_width > s->plane[plane].band[1][1].a_width ||
                !highpass_stride || s->plane[plane].band[1][1].width > s->plane[plane].band[1][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            av_log(avctx, AV_LOG_DEBUG, "Level 2 lowpass plane %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

            low    = s->plane[plane].l_h[7];
            high   = s->plane[plane].subband[5];
            output = s->plane[plane].l_h[3];
            dsp->vert_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].subband[4];
            high   = s->plane[plane].subband[6];
            output = s->plane[plane].l_h[4];
            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].l_h[3];
            high   = s->plane[plane].l_h[4];
            output = s->plane[plane].l_h[7];
            dsp->horiz_filter(output, output_stride, low, output_stride, high, output_stride, lowpass_width, lowpass_height * 2);

            output = s->plane[plane].l_h[7];
            for (i = 0; i < lowpass_height * 2; i++) {
                for (j = 0; j < lowpass_width * 2; j++)
                    output[j] *= 4;
                output += output_stride * 2;
            }

            low    = s->plane[plane].subband[7];
            high   = s->plane[plane].subband[9];
            output = s->plane[plane].l_h[3];
            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].subband[8];
            high   = s->plane[plane].subband[10];
            output = s->plane[plane].l_h[4];
            dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

            low    = s->plane[plane].l_h[3];
            high   = s->plane[plane].l_h[4];
            output = s->plane[plane].l_h[9];
            dsp->horiz_filter(output, output_stride, low, output_stride, high, output_stride, lowpass_width, lowpass_height * 2);

            lowpass_height  = s->plane[plane].band[4][1].height;
            output_stride   = s->plane[plane].band[4][1].a_width;
            lowpass_width   = s->plane[plane].band[4][1].width;
            highpass_stride = s->plane[plane].band[4][1].stride;
            av_log(avctx, AV_LOG_DEBUG, "temporal level %i %i %i %i\n", plane, lowpass_height, lowpass_width, highpass_stride);

            if (lowpass_height > s->plane[plane].band[4][1].a_height || lowpass_width > s->plane[plane].band[4][1].a_width ||
                !highpass_stride || s->plane[plane].band[4][1].width > s->plane[plane].band[4][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            low    = s->plane[plane].l_h[7];
            high   = s->plane[plane].l_h[9];
            output = s->plane[plane].l_h[7];
            for (i = 0; i < lowpass_height; i++) {
                inverse_temporal_filter(low, high, lowpass_width);
                low    += output_stride;
                high   += output_stride;
            }
            if (s->progressive) {
                low    = s->plane[plane].l_h[7];
                high   = s->plane[plane].subband[15];
                output = s->plane[plane].l_h[6];
                dsp->vert_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[14];
                high   = s->plane[plane].subband[16];
                output = s->plane[plane].l_h[7];
                dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].l_h[9];
                high   = s->plane[plane].subband[12];
                output = s->plane[plane].l_h[8];
                dsp->vert_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[11];
                high   = s->plane[plane].subband[13];
                output = s->plane[plane].l_h[9];
                dsp->vert_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                if (s->sample_type == 1)
                    continue;

                dst = (int16_t *)pic->data[act_plane];
                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                    if (plane & 1)
                        dst++;
                    if (plane > 1)
                        dst += pic->linesize[act_plane] >> 1;
                }

                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16 &&
                    (lowpass_height * 2 > avctx->coded_height / 2 ||
                     lowpass_width  * 2 > avctx->coded_width  / 2    )
                    ) {
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                }

                low  = s->plane[plane].l_h[6];
                high = s->plane[plane].l_h[7];
                for (i = 0; i < s->plane[act_plane].height; i++) {
                    dsp->horiz_filter_clip(dst, low, high, lowpass_width, s->bpc);
                    low  += output_stride;
                    high += output_stride;
                    dst  += dst_linesize;
                }
            } else {
                pic->interlaced_frame = 1;
                low    = s->plane[plane].l_h[7];
                high   = s->plane[plane].subband[14];
                output = s->plane[plane].l_h[6];
                dsp->horiz_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[15];
                high   = s->plane[plane].subband[16];
                output = s->plane[plane].l_h[7];
                dsp->horiz_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].l_h[9];
                high   = s->plane[plane].subband[11];
                output = s->plane[plane].l_h[8];
                dsp->horiz_filter(output, output_stride, low, output_stride, high, highpass_stride, lowpass_width, lowpass_height);

                low    = s->plane[plane].subband[12];
                high   = s->plane[plane].subband[13];
                output = s->plane[plane].l_h[9];
                dsp->horiz_filter(output, output_stride, low, highpass_stride, high, highpass_stride, lowpass_width, lowpass_height);

                if (s->sample_type == 1)
                    continue;

                dst  = (int16_t *)pic->data[act_plane];
                low  = s->plane[plane].l_h[6];
                high = s->plane[plane].l_h[7];
                for (i = 0; i < s->plane[act_plane].height / 2; i++) {
                    interlaced_vertical_filter(dst, low, high, lowpass_width * 2,  pic->linesize[act_plane]/2, act_plane);
                    low  += output_stride * 2;
                    high += output_stride * 2;
                    dst  += pic->linesize[act_plane];
                }
            }
        }
    }

    if (s->transform_type == 2 && s->sample_type == 1) {
        int16_t *low, *high, *dst;
        int output_stride, lowpass_height, lowpass_width;
        ptrdiff_t dst_linesize;

        for (plane = 0; plane < s->planes; plane++) {
            int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;

            if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                act_plane = 0;
                dst_linesize = pic->linesize[act_plane];
            } else {
                dst_linesize = pic->linesize[act_plane] / 2;
            }

            lowpass_height  = s->plane[plane].band[4][1].height;
            output_stride   = s->plane[plane].band[4][1].a_width;
            lowpass_width   = s->plane[plane].band[4][1].width;

            if (lowpass_height > s->plane[plane].band[4][1].a_height || lowpass_width > s->plane[plane].band[4][1].a_width ||
                s->plane[plane].band[4][1].width > s->plane[plane].band[4][1].a_width ||
                lowpass_width < 3 || lowpass_height < 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plane dimensions\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (s->progressive) {
                dst = (int16_t *)pic->data[act_plane];
                low  = s->plane[plane].l_h[8];
                high = s->plane[plane].l_h[9];

                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16) {
                    if (plane & 1)
                        dst++;
                    if (plane > 1)
                        dst += pic->linesize[act_plane] >> 1;
                }

                if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16 &&
                    (lowpass_height * 2 > avctx->coded_height / 2 ||
                     lowpass_width  * 2 > avctx->coded_width  / 2    )
                    ) {
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                }

                for (i = 0; i < s->plane[act_plane].height; i++) {
                    dsp->horiz_filter_clip(dst, low, high, lowpass_width, s->bpc);
                    low  += output_stride;
                    high += output_stride;
                    dst  += dst_linesize;
                }
            } else {
                dst  = (int16_t *)pic->data[act_plane];
                low  = s->plane[plane].l_h[8];
                high = s->plane[plane].l_h[9];
                for (i = 0; i < s->plane[act_plane].height / 2; i++) {
                    interlaced_vertical_filter(dst, low, high, lowpass_width * 2,  pic->linesize[act_plane]/2, act_plane);
                    low  += output_stride * 2;
                    high += output_stride * 2;
                    dst  += pic->linesize[act_plane];
                }
            }
        }
    }

    if (avctx->pix_fmt == AV_PIX_FMT_BAYER_RGGB16)
        process_bayer(pic, s->bpc);
end:
    if (ret < 0)
        return ret;

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

#if HAVE_THREADS
static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    CFHDContext *psrc = src->priv_data;
    CFHDContext *pdst = dst->priv_data;
    int ret;

    if (dst == src || psrc->transform_type == 0)
        return 0;

    if (pdst->plane[0].idwt_size != psrc->plane[0].idwt_size ||
        pdst->a_format != psrc->a_format ||
        pdst->a_width != psrc->a_width ||
        pdst->a_height != psrc->a_height)
        free_buffers(pdst);

    pdst->a_format = psrc->a_format;
    pdst->a_width  = psrc->a_width;
    pdst->a_height = psrc->a_height;
    pdst->transform_type = psrc->transform_type;
    pdst->progressive = psrc->progressive;
    pdst->planes = psrc->planes;

    if (!pdst->plane[0].idwt_buf) {
        pdst->coded_width  = pdst->a_width;
        pdst->coded_height = pdst->a_height;
        pdst->coded_format = pdst->a_format;
        ret = alloc_buffers(dst);
        if (ret < 0)
            return ret;
    }

    for (int plane = 0; plane < pdst->planes; plane++) {
        memcpy(pdst->plane[plane].band, psrc->plane[plane].band, sizeof(pdst->plane[plane].band));
        memcpy(pdst->plane[plane].idwt_buf, psrc->plane[plane].idwt_buf,
               pdst->plane[plane].idwt_size * sizeof(int16_t));
    }

    return 0;
}
#endif

AVCodec ff_cfhd_decoder = {
    .name             = "cfhd",
    .long_name        = NULL_IF_CONFIG_SMALL("GoPro CineForm HD"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_CFHD,
    .priv_data_size   = sizeof(CFHDContext),
    .init             = cfhd_init,
    .close            = cfhd_close,
    .decode           = cfhd_decode,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(update_thread_context),
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
