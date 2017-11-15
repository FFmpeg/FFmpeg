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

#include <stdio.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/log.h"

#include "bsf.h"
#include "cbs.h"


typedef struct TraceHeadersContext {
    CodedBitstreamContext *cbc;
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
        CodedBitstreamFragment ps;

        av_log(bsf, AV_LOG_INFO, "Extradata\n");

        err = ff_cbs_read_extradata(ctx->cbc, &ps, bsf->par_in);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to read extradata.\n");
            return err;
        }

        ff_cbs_fragment_uninit(ctx->cbc, &ps);
    }

    return 0;
}

static void trace_headers_close(AVBSFContext *bsf)
{
    TraceHeadersContext *ctx = bsf->priv_data;

    ff_cbs_close(&ctx->cbc);
}

static int trace_headers(AVBSFContext *bsf, AVPacket *out)
{
    TraceHeadersContext *ctx = bsf->priv_data;
    CodedBitstreamFragment au;
    AVPacket *in;
    char tmp[256] = { 0 };
    int err;

    err = ff_bsf_get_packet(bsf, &in);
    if (err < 0)
        return err;

    if (in->flags & AV_PKT_FLAG_KEY)
        av_strlcat(tmp, ", key frame", sizeof(tmp));
    if (in->flags & AV_PKT_FLAG_CORRUPT)
        av_strlcat(tmp, ", corrupt", sizeof(tmp));

    if (in->pts != AV_NOPTS_VALUE)
        av_strlcatf(tmp, sizeof(tmp), ", pts %"PRId64, in->pts);
    else
        av_strlcat(tmp, ", no pts", sizeof(tmp));
    if (in->dts != AV_NOPTS_VALUE)
        av_strlcatf(tmp, sizeof(tmp), ", dts %"PRId64, in->dts);
    else
        av_strlcat(tmp, ", no dts", sizeof(tmp));
    if (in->duration > 0)
        av_strlcatf(tmp, sizeof(tmp), ", duration %"PRId64, in->duration);

    av_log(bsf, AV_LOG_INFO, "Packet: %d bytes%s.\n", in->size, tmp);

    err = ff_cbs_read_packet(ctx->cbc, &au, in);
    if (err < 0)
        return err;

    ff_cbs_fragment_uninit(ctx->cbc, &au);

    av_packet_move_ref(out, in);
    av_packet_free(&in);

    return 0;
}

static const enum AVCodecID trace_headers_codec_ids[] = {
    AV_CODEC_ID_H264,
    AV_CODEC_ID_HEVC,
    AV_CODEC_ID_MPEG2VIDEO,
    AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_trace_headers_bsf = {
    .name           = "trace_headers",
    .priv_data_size = sizeof(TraceHeadersContext),
    .init           = &trace_headers_init,
    .close          = &trace_headers_close,
    .filter         = &trace_headers,
    .codec_ids      = trace_headers_codec_ids,
};
