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

#include "bsf.h"
#include "bsf_internal.h"
#include "codec_id.h"
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

    return 0;
}

static int ff_eia608_to_smpte436m_filter(AVBSFContext *ctx, AVPacket *out)
{
    EIA608ToSMPTE436MContext *priv = ctx->priv_data;
    AVPacket                 *in;

    int ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

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

    int cc_count           = in->size / 3;
    int space_left         = AV_SMPTE_291M_ANC_PAYLOAD_CAPACITY - (p - anc.payload);
    int cc_data_space_left = space_left - CDP_FOOTER_SIZE;
    int max_cc_count       = FFMAX(cc_data_space_left / 3, CC_COUNT_MASK);

    if (cc_count > max_cc_count) {
        av_log(ctx,
               AV_LOG_ERROR,
               "cc_count (%d) is bigger than the maximum supported (%d), truncating captions packet\n",
               cc_count,
               max_cc_count);
        cc_count = max_cc_count;
    }

    *cc_count_p = cc_count | ~CC_COUNT_MASK; // other bits are reserved and set to ones

    for (size_t i = 0; i < cc_count; i++) {
        size_t start = i * 3;
        *p++         = in->data[start] | 0xF8; // fill reserved bits with ones
        *p++         = in->data[start + 1];
        *p++         = in->data[start + 2];
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

    ret = 0;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
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
};
