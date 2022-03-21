/*
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

#include <inttypes.h>

#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/log.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"


typedef struct TraceHeadersContext {
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment fragment;
} TraceHeadersContext;


static int trace_headers_init(AVBSFContext *bsf)
{
    TraceHeadersContext *ctx = bsf->priv_data;
    int err;

    err = ff_cbs_init(&ctx->cbc, bsf->par_in->codec_id, bsf);
    if (err < 0)
        return err;

    ctx->cbc->trace_enable = 1;
    ctx->cbc->trace_level  = AV_LOG_INFO;

    if (bsf->par_in->extradata) {
        CodedBitstreamFragment *frag = &ctx->fragment;

        av_log(bsf, AV_LOG_INFO, "Extradata\n");

        err = ff_cbs_read_extradata(ctx->cbc, frag, bsf->par_in);

        ff_cbs_fragment_reset(frag);
    }

    return err;
}

static void trace_headers_close(AVBSFContext *bsf)
{
    TraceHeadersContext *ctx = bsf->priv_data;

    ff_cbs_fragment_free(&ctx->fragment);
    ff_cbs_close(&ctx->cbc);
}

static int trace_headers(AVBSFContext *bsf, AVPacket *pkt)
{
    TraceHeadersContext *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->fragment;
    char tmp[256] = { 0 };
    int err;

    err = ff_bsf_get_packet_ref(bsf, pkt);
    if (err < 0)
        return err;

    if (pkt->flags & AV_PKT_FLAG_KEY)
        av_strlcat(tmp, ", key frame", sizeof(tmp));
    if (pkt->flags & AV_PKT_FLAG_CORRUPT)
        av_strlcat(tmp, ", corrupt", sizeof(tmp));

    if (pkt->pts != AV_NOPTS_VALUE)
        av_strlcatf(tmp, sizeof(tmp), ", pts %"PRId64, pkt->pts);
    else
        av_strlcat(tmp, ", no pts", sizeof(tmp));
    if (pkt->dts != AV_NOPTS_VALUE)
        av_strlcatf(tmp, sizeof(tmp), ", dts %"PRId64, pkt->dts);
    else
        av_strlcat(tmp, ", no dts", sizeof(tmp));
    if (pkt->duration > 0)
        av_strlcatf(tmp, sizeof(tmp), ", duration %"PRId64, pkt->duration);

    av_log(bsf, AV_LOG_INFO, "Packet: %d bytes%s.\n", pkt->size, tmp);

    if (av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, NULL)) {
        av_log(bsf, AV_LOG_INFO, "Side data:\n");

        err = ff_cbs_read_packet_side_data(ctx->cbc, frag, pkt);
        ff_cbs_fragment_reset(frag);

        if (err < 0) {
            av_packet_unref(pkt);
            return err;
        }
        av_log(bsf, AV_LOG_INFO, "Payload:\n");
    }

    err = ff_cbs_read_packet(ctx->cbc, frag, pkt);

    ff_cbs_fragment_reset(frag);

    if (err < 0)
        av_packet_unref(pkt);
    return err;
}

const FFBitStreamFilter ff_trace_headers_bsf = {
    .p.name         = "trace_headers",
    .p.codec_ids    = ff_cbs_all_codec_ids,
    .priv_data_size = sizeof(TraceHeadersContext),
    .init           = &trace_headers_init,
    .close          = &trace_headers_close,
    .filter         = &trace_headers,
};
