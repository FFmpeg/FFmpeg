/*
 * MXF SMPTE-436M ANC to EIA-608 bitstream filter
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
#include "libavutil/error.h"

static av_cold int ff_smpte436m_to_eia608_init(AVBSFContext *ctx)
{
    ctx->par_out->codec_type = AVMEDIA_TYPE_SUBTITLE;
    ctx->par_out->codec_id   = AV_CODEC_ID_EIA_608;
    return 0;
}

static int ff_smpte436m_to_eia608_filter(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int       ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    AVSmpte436mAncIterator iter;
    ret = av_smpte_436m_anc_iter_init(&iter, in->data, in->size);
    if (ret < 0)
        goto fail;
    AVSmpte436mCodedAnc coded_anc;
    while ((ret = av_smpte_436m_anc_iter_next(&iter, &coded_anc)) >= 0) {
        AVSmpte291mAnc8bit anc;
        ret = av_smpte_291m_anc_8bit_decode(
            &anc, coded_anc.payload_sample_coding, coded_anc.payload_sample_count, coded_anc.payload, ctx);
        if (ret < 0)
            goto fail;
        ret = av_smpte_291m_anc_8bit_extract_cta_708(&anc, NULL, ctx);
        if (ret == AVERROR(EAGAIN))
            continue;
        if (ret < 0)
            goto fail;
        int cc_count = ret;

        ret = av_new_packet(out, 3 * cc_count);
        if (ret < 0)
            goto fail;

        ret = av_packet_copy_props(out, in);
        if (ret < 0)
            goto fail;

        // verified it won't fail by running it above
        av_smpte_291m_anc_8bit_extract_cta_708(&anc, out->data, ctx);

        av_packet_free(&in);

        return 0;
    }
    if (ret != AVERROR_EOF)
        return ret;
    ret = AVERROR(EAGAIN);

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

const FFBitStreamFilter ff_smpte436m_to_eia608_bsf = {
    .p.name      = "smpte436m_to_eia608",
    .p.codec_ids = (const enum AVCodecID[]){ AV_CODEC_ID_SMPTE_436M_ANC, AV_CODEC_ID_NONE },
    .init        = ff_smpte436m_to_eia608_init,
    .filter      = ff_smpte436m_to_eia608_filter,
};
