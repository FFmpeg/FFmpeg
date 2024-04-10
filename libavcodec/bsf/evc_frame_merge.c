/*
 * Copyright (c) 2019 James Almer <jamrial@gmail.com>
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
#include "libavutil/mem.h"
#include "get_bits.h"
#include "bsf.h"
#include "bsf_internal.h"

#include "evc.h"
#include "evc_parse.h"
#include "evc_ps.h"

// Access unit data
typedef struct AccessUnitBuffer {
    uint8_t *data;      // the data buffer
    size_t data_size;   // size of data in bytes
    unsigned capacity;  // buffer capacity
} AccessUnitBuffer;

typedef struct EVCFMergeContext {
    AVPacket *in, *buffer_pkt;
    EVCParamSets ps;
    EVCParserPoc poc;
    AccessUnitBuffer au_buffer;
} EVCFMergeContext;

static int end_of_access_unit_found(const EVCParamSets *ps, const EVCParserSliceHeader *sh,
                                    const EVCParserPoc *poc, enum EVCNALUnitType nalu_type)
{
    EVCParserPPS *pps = ps->pps[sh->slice_pic_parameter_set_id];
    EVCParserSPS *sps = ps->sps[pps->pps_seq_parameter_set_id];

    av_assert0(sps && pps);

    if (sps->profile_idc == 0) { // BASELINE profile
        if (nalu_type == EVC_NOIDR_NUT || nalu_type == EVC_IDR_NUT)
            return 1;
    } else { // MAIN profile
        if (nalu_type == EVC_NOIDR_NUT) {
            if (poc->PicOrderCntVal != poc->prevPicOrderCntVal)
                return 1;
        } else if (nalu_type == EVC_IDR_NUT)
            return 1;
    }
    return 0;
}

static void evc_frame_merge_flush(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    ff_evc_ps_free(&ctx->ps);
    av_packet_unref(ctx->in);
    av_packet_unref(ctx->buffer_pkt);
    ctx->au_buffer.data_size = 0;
}

static int parse_nal_unit(AVBSFContext *bsf, const uint8_t *buf, int buf_size)
{
    EVCFMergeContext *ctx = bsf->priv_data;
    GetBitContext gb;
    enum EVCNALUnitType nalu_type;
    int tid, err;

    err = init_get_bits8(&gb, buf, buf_size);
    if (err < 0)
        return err;

    // @see ISO_IEC_23094-1_2020, 7.4.2.2 NAL unit header semantic (Table 4 - NAL unit type codes and NAL unit type classes)
    // @see enum EVCNALUnitType in evc.h
    if (get_bits1(&gb)) {// forbidden_zero_bit
        av_log(bsf, AV_LOG_ERROR, "Invalid NAL unit header\n");
        return AVERROR_INVALIDDATA;
    }

    nalu_type = get_bits(&gb, 6) - 1;
    if (nalu_type < EVC_NOIDR_NUT || nalu_type > EVC_UNSPEC_NUT62) {
        av_log(bsf, AV_LOG_ERROR, "Invalid NAL unit type: (%d)\n", nalu_type);
        return AVERROR_INVALIDDATA;
    }

    tid = get_bits(&gb, 3);
    skip_bits(&gb, 5); // nuh_reserved_zero_5bits
    skip_bits1(&gb);   // nuh_extension_flag

    switch (nalu_type) {
    case EVC_SPS_NUT:
        err = ff_evc_parse_sps(&gb, &ctx->ps);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "SPS parsing error\n");
            return err;
        }
        break;
    case EVC_PPS_NUT:
        err = ff_evc_parse_pps(&gb, &ctx->ps);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "PPS parsing error\n");
            return err;
        }
        break;
    case EVC_IDR_NUT:   // Coded slice of a IDR or non-IDR picture
    case EVC_NOIDR_NUT: {
        EVCParserSliceHeader sh;

        err = ff_evc_parse_slice_header(&gb, &sh, &ctx->ps, nalu_type);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Slice header parsing error\n");
            return err;
        }

        // POC (picture order count of the current picture) derivation
        // @see ISO/IEC 23094-1:2020(E) 8.3.1 Decoding process for picture order count
        err = ff_evc_derive_poc(&ctx->ps, &sh, &ctx->poc, nalu_type, tid);
        if (err < 0)
            return err;

        return end_of_access_unit_found(&ctx->ps, &sh, &ctx->poc, nalu_type);

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

static int evc_frame_merge_filter(AVBSFContext *bsf, AVPacket *out)
{
    EVCFMergeContext *ctx = bsf->priv_data;
    AVPacket *in = ctx->in, *buffer_pkt = ctx->buffer_pkt;
    size_t data_size;
    int au_end_found = 0, err;

    while (!au_end_found) {
        uint8_t *buffer;
        uint32_t nalu_size;

        if (!in->size) {
            av_packet_unref(in);
            err = ff_bsf_get_packet_ref(bsf, in);
            if (err < 0) {
                if (err == AVERROR_EOF && ctx->au_buffer.data_size > 0)
                    break;
                return err;
            }
            /* Buffer packets with timestamps (there should be at most one per AU)
             * or any packet if buffer_pkt is empty. The latter is needed to
             * passthrough positions in case there are no timestamps like with
             * the raw EVC demuxer. */
            if (!buffer_pkt->data ||
                in->pts != AV_NOPTS_VALUE && buffer_pkt->pts == AV_NOPTS_VALUE) {
                err = av_packet_ref(buffer_pkt, in);
                if (err < 0)
                    goto end;
            }
        }

        // Buffer size is not enough for buffer to store NAL unit 4-bytes prefix (length)
        if (in->size < EVC_NALU_LENGTH_PREFIX_SIZE)
            return AVERROR_INVALIDDATA;

        nalu_size = evc_read_nal_unit_length(in->data, EVC_NALU_LENGTH_PREFIX_SIZE, bsf);
        if (!nalu_size || nalu_size > INT_MAX) {
            av_log(bsf, AV_LOG_ERROR, "Invalid NAL unit size: (%u)\n", nalu_size);
            err = AVERROR_INVALIDDATA;
            goto end;
        }

        if (in->size < nalu_size + EVC_NALU_LENGTH_PREFIX_SIZE) {
            err = AVERROR_INVALIDDATA;
            goto end;
        }

        err = parse_nal_unit(bsf, in->data + EVC_NALU_LENGTH_PREFIX_SIZE, nalu_size);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Parsing of NAL unit failed\n");
            goto end;
        }
        au_end_found = err;

        nalu_size += EVC_NALU_LENGTH_PREFIX_SIZE;

        data_size = ctx->au_buffer.data_size + nalu_size;
        if (data_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
            av_log(bsf, AV_LOG_ERROR, "Assembled packet is too big\n");
            err = AVERROR(ERANGE);
            goto end;
        }

        buffer = av_fast_realloc(ctx->au_buffer.data, &ctx->au_buffer.capacity,
                                 data_size);
        if (!buffer) {
            av_freep(&ctx->au_buffer.data);
            err = AVERROR_INVALIDDATA;
            goto end;
        }

        ctx->au_buffer.data = buffer;
        memcpy(ctx->au_buffer.data + ctx->au_buffer.data_size, in->data, nalu_size);

        ctx->au_buffer.data_size = data_size;

        in->data += nalu_size;
        in->size -= nalu_size;
    }

    av_packet_unref(in);
    data_size = ctx->au_buffer.data_size;

    ctx->au_buffer.data_size = 0;
    // drop the data in buffer_pkt, if any, but keep the props
    av_buffer_unref(&buffer_pkt->buf);
    err = av_buffer_realloc(&buffer_pkt->buf, data_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (err < 0)
        goto end;

    buffer_pkt->data = buffer_pkt->buf->data;
    buffer_pkt->size = data_size;
    av_packet_move_ref(out, buffer_pkt);
    memcpy(out->data, ctx->au_buffer.data, data_size);
    memset(out->data + data_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    err = 0;
end:
    if (err < 0) {
        av_packet_unref(in);
        av_packet_unref(buffer_pkt);
        ctx->au_buffer.data_size = 0;
    }
    return err;
}

static int evc_frame_merge_init(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    ctx->in  = av_packet_alloc();
    ctx->buffer_pkt = av_packet_alloc();
    if (!ctx->in || !ctx->buffer_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static void evc_frame_merge_close(AVBSFContext *bsf)
{
    EVCFMergeContext *ctx = bsf->priv_data;

    av_packet_free(&ctx->in);
    av_packet_free(&ctx->buffer_pkt);
    ff_evc_ps_free(&ctx->ps);

    ctx->au_buffer.capacity = 0;
    av_freep(&ctx->au_buffer.data);
    ctx->au_buffer.data_size = 0;
}

static const enum AVCodecID evc_frame_merge_codec_ids[] = {
    AV_CODEC_ID_EVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_evc_frame_merge_bsf = {
    .p.name         = "evc_frame_merge",
    .p.codec_ids    = evc_frame_merge_codec_ids,
    .priv_data_size = sizeof(EVCFMergeContext),
    .init           = evc_frame_merge_init,
    .flush          = evc_frame_merge_flush,
    .close          = evc_frame_merge_close,
    .filter         = evc_frame_merge_filter,
};
