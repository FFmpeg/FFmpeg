/*
 * EVC format parser
 *
 * Copyright (C) 2021 Dawid Kozinski <d.kozinski@samsung.com>
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

#include "avcodec.h"
#include "bytestream.h"
#include "evc.h"
#include "evc_parse.h"

typedef struct EVCParserContext {
    EVCParamSets ps;
    EVCParserPoc poc;

    int parsed_extradata;
} EVCParserContext;

#define NUM_CHROMA_FORMATS      4   // @see ISO_IEC_23094-1 section 6.2 table 2

static const enum AVPixelFormat pix_fmts_8bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P
};

static const enum AVPixelFormat pix_fmts_9bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY9, AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9
};

static const enum AVPixelFormat pix_fmts_10bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10
};

static const enum AVPixelFormat pix_fmts_12bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12
};

static const enum AVPixelFormat pix_fmts_14bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14
};

static const enum AVPixelFormat pix_fmts_16bit[NUM_CHROMA_FORMATS] = {
    AV_PIX_FMT_GRAY16, AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16
};

static int parse_nal_unit(AVCodecParserContext *s, AVCodecContext *avctx,
                          const uint8_t *buf, int buf_size)
{
    EVCParserContext *ctx = s->priv_data;
    GetBitContext gb;
    int nalu_type, tid;
    int ret;

    if (buf_size <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit size: (%d)\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    ret = init_get_bits8(&gb, buf, buf_size);
    if (ret < 0)
        return ret;

    // @see ISO_IEC_23094-1_2020, 7.4.2.2 NAL unit header semantic (Table 4 - NAL unit type codes and NAL unit type classes)
    // @see enum EVCNALUnitType in evc.h
    if (get_bits1(&gb)) {// forbidden_zero_bit
        av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit header\n");
        return AVERROR_INVALIDDATA;
    }

    nalu_type = get_bits(&gb, 6) - 1;
    if (nalu_type < EVC_NOIDR_NUT || nalu_type > EVC_UNSPEC_NUT62) {
        av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit type: (%d)\n", nalu_type);
        return AVERROR_INVALIDDATA;
    }

    tid = get_bits(&gb, 3);
    skip_bits(&gb, 5); // nuh_reserved_zero_5bits
    skip_bits1(&gb);   // nuh_extension_flag

    switch (nalu_type) {
    case EVC_SPS_NUT:
        ret = ff_evc_parse_sps(&gb, &ctx->ps);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "SPS parsing error\n");
            return ret;
        }
        break;
    case EVC_PPS_NUT:
        ret = ff_evc_parse_pps(&gb, &ctx->ps);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "PPS parsing error\n");
            return ret;
        }
        break;
    case EVC_IDR_NUT:   // Coded slice of a IDR or non-IDR picture
    case EVC_NOIDR_NUT: {
        const EVCParserPPS *pps;
        const EVCParserSPS *sps;
        EVCParserSliceHeader sh;
        int bit_depth;

        ret = ff_evc_parse_slice_header(&gb, &sh, &ctx->ps, nalu_type);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Slice header parsing error\n");
            return ret;
        }

        pps = ctx->ps.pps[sh.slice_pic_parameter_set_id];
        sps = ctx->ps.sps[pps->pps_seq_parameter_set_id];
        av_assert0(sps && pps);

        s->coded_width  = sps->pic_width_in_luma_samples;
        s->coded_height = sps->pic_height_in_luma_samples;

        if (sps->picture_cropping_flag) {
            s->width    = sps->pic_width_in_luma_samples  - sps->picture_crop_left_offset - sps->picture_crop_right_offset;
            s->height   = sps->pic_height_in_luma_samples - sps->picture_crop_top_offset  - sps->picture_crop_bottom_offset;
        } else {
            s->width    = sps->pic_width_in_luma_samples;
            s->height   = sps->pic_height_in_luma_samples;
        }

        switch (sh.slice_type) {
        case EVC_SLICE_TYPE_B: {
            s->pict_type =  AV_PICTURE_TYPE_B;
            break;
        }
        case EVC_SLICE_TYPE_P: {
            s->pict_type =  AV_PICTURE_TYPE_P;
            break;
        }
        case EVC_SLICE_TYPE_I: {
            s->pict_type =  AV_PICTURE_TYPE_I;
            break;
        }
        default: {
            s->pict_type =  AV_PICTURE_TYPE_NONE;
        }
        }

        avctx->profile = sps->profile_idc;

        if (sps->vui_parameters_present_flag && sps->vui_parameters.timing_info_present_flag) {
            int64_t num = sps->vui_parameters.num_units_in_tick;
            int64_t den = sps->vui_parameters.time_scale;
            if (num != 0 && den != 0)
                av_reduce(&avctx->framerate.den, &avctx->framerate.num, num, den, 1 << 30);
        } else
            avctx->framerate = (AVRational) { 0, 1 };

        bit_depth = sps->bit_depth_chroma_minus8 + 8;
        s->format = AV_PIX_FMT_NONE;

        switch (bit_depth) {
        case 8:
            s->format = pix_fmts_8bit[sps->chroma_format_idc];
            break;
        case 9:
            s->format = pix_fmts_9bit[sps->chroma_format_idc];
            break;
        case 10:
            s->format = pix_fmts_10bit[sps->chroma_format_idc];
            break;
        case 12:
            s->format = pix_fmts_12bit[sps->chroma_format_idc];
            break;
        case 14:
            s->format = pix_fmts_14bit[sps->chroma_format_idc];
            break;
        case 16:
            s->format = pix_fmts_16bit[sps->chroma_format_idc];
            break;
        }

        s->key_frame = (nalu_type == EVC_IDR_NUT) ? 1 : 0;

        // POC (picture order count of the current picture) derivation
        // @see ISO/IEC 23094-1:2020(E) 8.3.1 Decoding process for picture order count
        ret = ff_evc_derive_poc(&ctx->ps, &sh, &ctx->poc, nalu_type, tid);
        if (ret < 0)
            return ret;

        s->output_picture_number = ctx->poc.PicOrderCntVal;

        break;
    }
    case EVC_SEI_NUT:   // Supplemental Enhancement Information
    case EVC_APS_NUT:   // Adaptation parameter set
    case EVC_FD_NUT:    // Filler data
    default:
        break;
    }

    return 0;
}

/**
 * Parse NAL units of found picture and decode some basic information.
 *
 * @param s codec parser context
 * @param avctx codec context
 * @param buf buffer with field/frame data
 * @param buf_size size of the buffer
 */
static int parse_nal_units(AVCodecParserContext *s, AVCodecContext *avctx, const uint8_t *buf, int buf_size)
{
    const uint8_t *data = buf;
    int data_size = buf_size;

    while (data_size > 0) {
        int nalu_size = 0;
        int ret;

        // Buffer size is not enough for buffer to store NAL unit 4-bytes prefix (length)
        if (data_size < EVC_NALU_LENGTH_PREFIX_SIZE)
            return AVERROR_INVALIDDATA;

        nalu_size = evc_read_nal_unit_length(data, data_size, avctx);


        data += EVC_NALU_LENGTH_PREFIX_SIZE;
        data_size -= EVC_NALU_LENGTH_PREFIX_SIZE;

        if (data_size < nalu_size)
            return AVERROR_INVALIDDATA;

        ret = parse_nal_unit(s, avctx, data, nalu_size);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Parsing of NAL unit failed\n");
            return AVERROR_INVALIDDATA;
        }

        data += nalu_size;
        data_size -= nalu_size;
    }
    return 0;
}

// Decoding nal units from evcC (EVCDecoderConfigurationRecord)
// @see @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.2
static int decode_extradata(AVCodecParserContext *s, AVCodecContext *avctx)
{
    const uint8_t *data = avctx->extradata;
    int size = avctx->extradata_size;
    int ret = 0;
    GetByteContext gb;

    bytestream2_init(&gb, data, size);

    if (!data || size <= 0)
        return -1;

    // extradata is encoded as evcC format.
    if (data[0] == 1) {
        int num_of_arrays;  // indicates the number of arrays of NAL units of the indicated type(s)

        int nalu_length_field_size; // indicates the length in bytes of the NALUnitLenght field in EVC video stream sample in the stream
        // The value of this field shall be one of 0, 1, or 3 corresponding to a length encoded with 1, 2, or 4 bytes, respectively.

        if (bytestream2_get_bytes_left(&gb) < 18) {
            av_log(avctx, AV_LOG_ERROR, "evcC %d too short\n", size);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gb, 16);

        // @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
        // LengthSizeMinusOne plus 1 indicates the length in bytes of the NALUnitLength field in a EVC video stream sample in the stream to which this configuration record applies. For example, a size of one byte is indicated with a value of 0.
        // The value of this field shall be one of 0, 1, or 3 corresponding to a length encoded with 1, 2, or 4 bytes, respectively.
        nalu_length_field_size = (bytestream2_get_byte(&gb) & 3) + 1;
        if( nalu_length_field_size != 1 &&
            nalu_length_field_size != 2 &&
            nalu_length_field_size != 4 ) {
            av_log(avctx, AV_LOG_ERROR, "The length in bytes of the NALUnitLenght field in a EVC video stream has unsupported value of %d\n", nalu_length_field_size);
            return AVERROR_INVALIDDATA;
        }

        num_of_arrays = bytestream2_get_byte(&gb);

        /* Decode nal units from evcC. */
        for (int i = 0; i < num_of_arrays; i++) {

            // @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
            // NAL_unit_type indicates the type of the NAL units in the following array (which shall be all of that type);
            // - it takes a value as defined in ISO/IEC 23094-1;
            // - it is restricted to take one of the values indicating a SPS, PPS, APS, or SEI NAL unit.
            int nal_unit_type = bytestream2_get_byte(&gb) & 0x3f;
            int num_nalus  = bytestream2_get_be16(&gb);

            for (int j = 0; j < num_nalus; j++) {

                int nal_unit_length = bytestream2_get_be16(&gb);

                if (bytestream2_get_bytes_left(&gb) < nal_unit_length) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit size in extradata.\n");
                    return AVERROR_INVALIDDATA;
                }

                if( nal_unit_type == EVC_SPS_NUT ||
                    nal_unit_type == EVC_PPS_NUT ||
                    nal_unit_type == EVC_APS_NUT ||
                    nal_unit_type == EVC_SEI_NUT ) {
                    if (parse_nal_unit(s, avctx, gb.buffer, nal_unit_length) != 0) {
                        av_log(avctx, AV_LOG_ERROR, "Parsing of NAL unit failed\n");
                        return AVERROR_INVALIDDATA;
                    }
                }

                bytestream2_skip(&gb, nal_unit_length);
            }
        }
    } else
        return -1;

    return ret;
}

static int evc_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    int next;
    int ret;
    EVCParserContext *ctx = s->priv_data;

    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;
    s->key_frame = 0;

    if (avctx->extradata && !ctx->parsed_extradata) {
        decode_extradata(s, avctx);
        ctx->parsed_extradata = 1;
    }

    next = buf_size;

    ret = parse_nal_units(s, avctx, buf, buf_size);
    if(ret < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    // poutbuf contains just one Access Unit
    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

static void evc_parser_close(AVCodecParserContext *s)
{
    EVCParserContext *ctx = s->priv_data;

    ff_evc_ps_free(&ctx->ps);
}

const AVCodecParser ff_evc_parser = {
    .codec_ids      = { AV_CODEC_ID_EVC },
    .priv_data_size = sizeof(EVCParserContext),
    .parser_parse   = evc_parse,
    .parser_close   = evc_parser_close,
};
