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

#include "parser.h"
#include "bytestream.h"
#include "evc.h"
#include "evc_parse.h"

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
    EVCParserContext *ctx = s->priv_data;
    const uint8_t *data = buf;
    int data_size = buf_size;
    int bytes_read = 0;
    int nalu_size = 0;

    while (data_size > 0) {

        // Buffer size is not enough for buffer to store NAL unit 4-bytes prefix (length)
        if (data_size < EVC_NALU_LENGTH_PREFIX_SIZE)
            return AVERROR_INVALIDDATA;

        nalu_size = evc_read_nal_unit_length(data, data_size, avctx);

        bytes_read += EVC_NALU_LENGTH_PREFIX_SIZE;

        data += EVC_NALU_LENGTH_PREFIX_SIZE;
        data_size -= EVC_NALU_LENGTH_PREFIX_SIZE;

        if (data_size < nalu_size)
            return AVERROR_INVALIDDATA;

        if (ff_evc_parse_nal_unit(ctx, data, nalu_size, avctx) != 0) {
            av_log(avctx, AV_LOG_ERROR, "Parsing of NAL unit failed\n");
            return AVERROR_INVALIDDATA;
        }

        if(ctx->nalu_type == EVC_SPS_NUT) {

            s->coded_width         = ctx->coded_width;
            s->coded_height        = ctx->coded_height;
            s->width               = ctx->width;
            s->height              = ctx->height;

            s->format              = ctx->format;

            avctx->framerate       = ctx->framerate;
            avctx->gop_size        = ctx->gop_size;
            avctx->delay           = ctx->delay;
            avctx->profile         = ctx->profile;

        } else if(ctx->nalu_type == EVC_NOIDR_NUT || ctx->nalu_type == EVC_IDR_NUT) {

            s->pict_type = ctx->pict_type;
            s->key_frame = ctx->key_frame;
            s->output_picture_number = ctx->output_picture_number;

        }

        data += nalu_size;
        data_size -= nalu_size;
    }
    return 0;
}

// Decoding nal units from evcC (EVCDecoderConfigurationRecord)
// @see @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.2
static int decode_extradata(EVCParserContext *ctx, const uint8_t *data, int size, void *logctx)
{
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
            av_log(logctx, AV_LOG_ERROR, "evcC %d too short\n", size);
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
            av_log(logctx, AV_LOG_ERROR, "The length in bytes of the NALUnitLenght field in a EVC video stream has unsupported value of %d\n", nalu_length_field_size);
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
                    av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit size in extradata.\n");
                    return AVERROR_INVALIDDATA;
                }

                if( nal_unit_type == EVC_SPS_NUT ||
                    nal_unit_type == EVC_PPS_NUT ||
                    nal_unit_type == EVC_APS_NUT ||
                    nal_unit_type == EVC_SEI_NUT ) {
                    if (ff_evc_parse_nal_unit(ctx, gb.buffer, nal_unit_length, logctx) != 0) {
                        av_log(logctx, AV_LOG_ERROR, "Parsing of NAL unit failed\n");
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

    if (avctx->extradata && !ctx->parsed_extradata) {
        decode_extradata(ctx, avctx->extradata, avctx->extradata_size, avctx);
        ctx->parsed_extradata = 1;
    }

    next = buf_size;

    ret = parse_nal_units(s, avctx, buf, buf_size);
    if(ret < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;

    // poutbuf contains just one Access Unit
    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

static void evc_parser_close(AVCodecParserContext *s)
{
    EVCParserContext *ctx = s->priv_data;

    ff_evc_parse_free(ctx);
}

const AVCodecParser ff_evc_parser = {
    .codec_ids      = { AV_CODEC_ID_EVC },
    .priv_data_size = sizeof(EVCParserContext),
    .parser_parse   = evc_parse,
    .parser_close   = evc_parser_close,
};
