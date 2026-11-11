/*
 * EIA-608 to MXF SMPTE-436M ANC bitstream filter
 * Copyright (c) 2025 Jacob Lifshay
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

#include "libavcodec/bsf.h"
#include "libavcodec/bsf_internal.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/smpte_436m.h"
#include "libavcodec/smpte_436m_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"

typedef struct EIA608ToSMPTE436MContext {
    const AVClass *class;
    unsigned                       line_number;
    unsigned                       cdp_sequence_cntr;
    unsigned                       wrapping_type_opt;
    unsigned                       sample_coding_opt;
    AVSmpte436mWrappingType        wrapping_type;
    AVSmpte436mPayloadSampleCoding sample_coding;
    AVRational                     cdp_frame_rate;
    uint8_t                        cdp_frame_rate_byte;
    AVPacket                      *in;          ///< input packet being split
    int                            in_pos;      ///< number of bytes of in already output
    int                            in_frames;   ///< number of frames of in already output
    int                            in_delayed;  ///< in was delayed by the split of the previous packets
    int64_t                        next_pts;    ///< pts of the frame following the last split packet
    int                            split;
    int64_t                        field_credit;      ///< fraction of a byte pair a field can still carry, in field_credit_den units
    int64_t                        field_credit_step; ///< byte pairs of one field carried by a frame, in field_credit_den units
    int64_t                        field_credit_den;
} EIA608ToSMPTE436MContext;

// clang-format off
static const AVSmpte291mAnc8bit test_anc = {
    .did         = 0x61,
    .sdid_or_dbn = 0x01,
    .data_count  = 0x49,
    .payload     = {
        // header
        0x96, 0x69, 0x49, 0x7F, 0x43, 0xFA, 0x8D, 0x72, 0xF4,

        // 608 triples
        0xFC, 0x80, 0x80, 0xFD, 0x80, 0x80,

        // 708 padding
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,
        0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00, 0xFA, 0x00, 0x00,

        // footer
        0x74, 0xFA, 0x8D, 0x81,
    },
    .checksum = 0xAB,
};
// clang-format on

static av_cold int ff_eia608_to_smpte436m_init(AVBSFContext *ctx)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;

    priv->wrapping_type = priv->wrapping_type_opt;
    priv->sample_coding = priv->sample_coding_opt;

    // validate we can handle the selected wrapping type and sample coding

    AVSmpte436mCodedAnc coded_anc;

    int ret = av_smpte_291m_anc_8bit_encode(
        &coded_anc, priv->line_number, priv->wrapping_type, priv->sample_coding, &test_anc, ctx);
    if (ret < 0)
        return ret;

    ctx->par_out->codec_type = AVMEDIA_TYPE_DATA;
    ctx->par_out->codec_id   = AV_CODEC_ID_SMPTE_436M_ANC;

    static const struct {
        AVRational frame_rate;
        uint8_t    cdp_frame_rate;
    } known_frame_rates[] = {
        { .frame_rate = { .num = 24000, .den = 1001 }, .cdp_frame_rate = 0x1F },
        { .frame_rate = { .num = 24, .den = 1 },       .cdp_frame_rate = 0x2F },
        { .frame_rate = { .num = 25, .den = 1 },       .cdp_frame_rate = 0x3F },
        { .frame_rate = { .num = 30000, .den = 1001 }, .cdp_frame_rate = 0x4F },
        { .frame_rate = { .num = 30, .den = 1 },       .cdp_frame_rate = 0x5F },
        { .frame_rate = { .num = 50, .den = 1 },       .cdp_frame_rate = 0x6F },
        { .frame_rate = { .num = 60000, .den = 1001 }, .cdp_frame_rate = 0x7F },
        { .frame_rate = { .num = 60, .den = 1 },       .cdp_frame_rate = 0x8F },
    };

    priv->cdp_frame_rate_byte = 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(known_frame_rates); i++) {
        if (known_frame_rates[i].frame_rate.num == priv->cdp_frame_rate.num && known_frame_rates[i].frame_rate.den == priv->cdp_frame_rate.den) {
            priv->cdp_frame_rate_byte = known_frame_rates[i].cdp_frame_rate;
            break;
        }
    }

    if (priv->cdp_frame_rate_byte == 0) {
        av_log(ctx,
               AV_LOG_FATAL,
               "cdp_frame_rate not supported: %d/%d\n",
               priv->cdp_frame_rate.num,
               priv->cdp_frame_rate.den);
        return AVERROR(EINVAL);
    }

    // EIA-608 sends one byte pair per field at 30000/1001 frames per second,
    // so a frame carries 30000/1001 / cdp_frame_rate byte pairs of each field
    priv->field_credit_step = 30000LL * priv->cdp_frame_rate.den;
    priv->field_credit_den  =  1001LL * priv->cdp_frame_rate.num;
    priv->next_pts          = AV_NOPTS_VALUE;

    return 0;
}

static int64_t frames_to_ts(AVBSFContext *ctx, int frames)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;

    if (ctx->time_base_in.num <= 0 || ctx->time_base_in.den <= 0)
        return 0;
    return av_rescale_q(frames, av_inv_q(priv->cdp_frame_rate), ctx->time_base_in);
}

/**
 * Return the number of leading byte triples of data going in the next frame,
 * and skip the frames which cannot carry any EIA-608 byte pair.
 */
static int split_cc_count(EIA608ToSMPTE436MContext *priv, const uint8_t *data, int count)
{
    int field_count[2] = { 0 };
    int limit, i;

    // CTA-708 byte triples are only found in packets already holding
    // the data of a single frame
    for (i = 0; i < count; i++)
        if ((data[i * 3] & 3) >= 2)
            return count;

    for (;;) {
        priv->field_credit += priv->field_credit_step;
        limit               = priv->field_credit / priv->field_credit_den;
        priv->field_credit -= limit * priv->field_credit_den;
        if (limit)
            break;
        priv->in_frames++;
    }

    for (i = 0; i < count; i++) {
        int cc_valid = data[i * 3] & 4;
        int cc_type  = data[i * 3] & 3;
        if (cc_valid && field_count[cc_type]++ == limit)
            break;
    }
    return i;
}

static int ff_eia608_to_smpte436m_filter(AVBSFContext *ctx, AVPacket *out)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;
    AVPacket                 *in   = priv->in;
    int ret;

    if (!in) {
        ret = ff_bsf_get_packet(ctx, &priv->in);
        if (ret < 0)
            return ret;
        in              = priv->in;
        priv->in_pos    = 0;
        priv->in_frames = 0;
        // the previous packets were split over the frames up to next_pts
        priv->in_delayed = priv->next_pts != AV_NOPTS_VALUE && in->pts != AV_NOPTS_VALUE &&
                           in->pts < priv->next_pts;
        if (priv->in_delayed) {
            if (in->dts != AV_NOPTS_VALUE)
                in->dts += priv->next_pts - in->pts;
            in->pts = priv->next_pts;
        } else {
            // the first frame of a packet carries at least one byte pair
            priv->field_credit = FFMAX(priv->field_credit_den - priv->field_credit_step, 0);
        }
        priv->next_pts = AV_NOPTS_VALUE;
    }

    const uint8_t *data = in->data + priv->in_pos;
    int in_count        = (in->size - priv->in_pos) / 3;

    AVSmpte291mAnc8bit anc;
    anc.did         = 0x61;
    anc.sdid_or_dbn = 0x1;

    uint8_t *p = anc.payload;

    *p++ = 0x96; // cdp_identifier -- always 0x9669
    *p++ = 0x69;

    uint8_t *cdp_length_p = p++;

    *p++ = priv->cdp_frame_rate_byte;

    const uint8_t FLAG_CC_DATA_PRESENT        = 0x40;
    const uint8_t FLAG_CAPTION_SERVICE_ACTIVE = 0x2;
    const uint8_t FLAG_RESERVED               = 0x1; // must always be set

    *p++ = FLAG_CC_DATA_PRESENT | FLAG_CAPTION_SERVICE_ACTIVE | FLAG_RESERVED;

    AV_WB16(p, priv->cdp_sequence_cntr);
    p += 2;

    const uint8_t CC_DATA_SECTION_ID = 0x72;

    *p++ = CC_DATA_SECTION_ID;

    uint8_t *cc_count_p = p++;

    const uint8_t CC_COUNT_MASK   = 0x1F;
    const int     CDP_FOOTER_SIZE = 4;

    int cc_count           = in_count;
    int space_left         = AV_SMPTE_291M_ANC_PAYLOAD_CAPACITY - (p - anc.payload);
    int cc_data_space_left = space_left - CDP_FOOTER_SIZE;
    int max_cc_count       = FFMIN(cc_data_space_left / 3, CC_COUNT_MASK);

    // the byte pairs a frame cannot carry go in the following frames
    if (priv->split)
        cc_count = split_cc_count(priv, data, in_count);

    if (cc_count > max_cc_count) {
        av_log(ctx,
               AV_LOG_ERROR,
               "cc_count (%d) is bigger than the maximum supported (%d), truncating captions packet\n",
               cc_count,
               max_cc_count);
        cc_count = in_count = max_cc_count;
    }

    *cc_count_p = cc_count | ~CC_COUNT_MASK; // other bits are reserved and set to ones

    for (size_t i = 0; i < cc_count; i++) {
        size_t start = i * 3;
        *p++         = data[start] | 0xF8; // fill reserved bits with ones
        *p++         = data[start + 1];
        *p++         = data[start + 2];
    }

    const uint8_t CDP_FOOTER_ID = 0x74;

    *p++ = CDP_FOOTER_ID;

    AV_WB16(p, priv->cdp_sequence_cntr);
    p += 2;

    uint8_t *packet_checksum_p = p;
    *p++                       = 0;

    anc.data_count = p - anc.payload;
    *cdp_length_p  = anc.data_count;

    int sum = 0;
    for (int i = 0; i < anc.data_count; i++) {
        sum += anc.payload[i];
    }
    // set to an 8-bit value such that the sum of the bytes of the whole CDP mod 2^8 is 0
    *packet_checksum_p = -sum;

    priv->cdp_sequence_cntr++;
    // cdp_sequence_cntr wraps around at 16-bits
    priv->cdp_sequence_cntr &= 0xFFFFU;

    av_smpte_291m_anc_8bit_fill_checksum(&anc);

    AVSmpte436mCodedAnc coded_anc;
    ret = av_smpte_291m_anc_8bit_encode(
        &coded_anc, priv->line_number, (AVSmpte436mWrappingType)priv->wrapping_type, priv->sample_coding, &anc, ctx);
    if (ret < 0)
        goto fail;

    ret = av_smpte_436m_anc_encode(NULL, 0, 1, &coded_anc);
    if (ret < 0)
        goto fail;

    ret = av_new_packet(out, ret);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    ret = av_smpte_436m_anc_encode(out->data, out->size, 1, &coded_anc);
    if (ret < 0)
        goto fail;

    // the timestamps are only changed when splitting
    if (priv->split) {
        if (in->pts != AV_NOPTS_VALUE)
            out->pts += frames_to_ts(ctx, priv->in_frames);
        if (in->dts != AV_NOPTS_VALUE)
            out->dts += frames_to_ts(ctx, priv->in_frames);

        if (cc_count < in_count || priv->in_frames || priv->in_delayed) {
            if (cc_count < in_count || priv->in_frames)
                out->duration = frames_to_ts(ctx, 1);
            priv->in_pos += cc_count * 3;
            priv->in_frames++;
            if (in->pts != AV_NOPTS_VALUE)
                priv->next_pts = in->pts + frames_to_ts(ctx, priv->in_frames);
        }
    }

    ret = 0;

fail:
    if (ret < 0)
        av_packet_unref(out);
    if (ret < 0 || cc_count == in_count)
        av_packet_free(&priv->in);
    return ret;
}

static void ff_eia608_to_smpte436m_flush(AVBSFContext *ctx)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;

    av_packet_free(&priv->in);
    priv->next_pts = AV_NOPTS_VALUE;
}

static av_cold void ff_eia608_to_smpte436m_close(AVBSFContext *ctx)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;

    av_packet_free(&priv->in);
}

#define OFFSET(x) offsetof(EIA608ToSMPTE436MContext, x)
#define FLAGS AV_OPT_FLAG_BSF_PARAM
// clang-format off
static const AVOption options[] = {
    { "line_number", "line number -- you probably want 9 or 11", OFFSET(line_number), AV_OPT_TYPE_UINT, { .i64 = 9 }, 0, 0xFFFF, FLAGS },
    { "wrapping_type", "wrapping type", OFFSET(wrapping_type_opt), AV_OPT_TYPE_UINT, { .i64 = AV_SMPTE_436M_WRAPPING_TYPE_VANC_FRAME }, 0, 0xFF, FLAGS, .unit = "wrapping_type" },
    FF_SMPTE_436M_WRAPPING_TYPE_VANC_AVOPTIONS(FLAGS, "wrapping_type"),
    { "sample_coding", "payload sample coding", OFFSET(sample_coding_opt), AV_OPT_TYPE_UINT, { .i64 = AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA }, 0, 0xFF, FLAGS, .unit = "sample_coding" },
    FF_SMPTE_436M_PAYLOAD_SAMPLE_CODING_ANC_AVOPTIONS(FLAGS, "sample_coding"),
    { "initial_cdp_sequence_cntr", "initial cdp_*_sequence_cntr value", OFFSET(cdp_sequence_cntr), AV_OPT_TYPE_UINT, { .i64 = 0 }, 0, 0xFFFF, FLAGS },
    { "cdp_frame_rate", "set the `cdp_frame_rate` fields", OFFSET(cdp_frame_rate), AV_OPT_TYPE_VIDEO_RATE, { .str = "30000/1001" }, 0, INT_MAX, FLAGS },
    { "split", "split the packets holding the byte pairs of several frames", OFFSET(split), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { NULL },
};
// clang-format on

static const AVClass eia608_to_smpte436m_class = {
    .class_name = "eia608_to_smpte436m bitstream filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_eia608_to_smpte436m_bsf = {
    .p.name         = "eia608_to_smpte436m",
    .p.codec_ids    = (const enum AVCodecID[]){ AV_CODEC_ID_EIA_608, AV_CODEC_ID_NONE },
    .p.priv_class   = &eia608_to_smpte436m_class,
    .priv_data_size = sizeof(EIA608ToSMPTE436MContext),
    .init           = ff_eia608_to_smpte436m_init,
    .filter         = ff_eia608_to_smpte436m_filter,
    .flush          = ff_eia608_to_smpte436m_flush,
    .close          = ff_eia608_to_smpte436m_close,
};
