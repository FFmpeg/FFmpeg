/*
 * RTP muxer chaining code
 * Copyright (c) 2010 Martin Storsjo
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

#include "avformat.h"
#include "avio_internal.h"
#include "rtpenc_chain.h"
#include "rtp.h"
#include "url.h"
#include "libavutil/opt.h"

int ff_rtp_chain_mux_open(AVFormatContext **out, AVFormatContext *s,
                          AVStream *st, URLContext *handle, int packet_size,
                          int idx)
{
    AVFormatContext *rtpctx = NULL;
    int ret;
    const AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);
    uint8_t *rtpflags;
    AVDictionary *opts = NULL;

    if (!rtp_format) {
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    /* Allocate an AVFormatContext for each output stream */
    rtpctx = avformat_alloc_context();
    if (!rtpctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    rtpctx->oformat = rtp_format;
    if (!avformat_new_stream(rtpctx, NULL)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    /* Pass the interrupt callback on */
    rtpctx->interrupt_callback = s->interrupt_callback;
    /* Copy the max delay setting; the rtp muxer reads this. */
    rtpctx->max_delay = s->max_delay;
    /* Copy other stream parameters. */
    rtpctx->streams[0]->sample_aspect_ratio = st->sample_aspect_ratio;
    rtpctx->flags |= s->flags & AVFMT_FLAG_BITEXACT;
    rtpctx->strict_std_compliance = s->strict_std_compliance;

    /* Get the payload type from the codec */
    if (st->id < RTP_PT_PRIVATE)
        rtpctx->streams[0]->id =
            ff_rtp_get_payload_type(s, st->codecpar, idx);
    else
        rtpctx->streams[0]->id = st->id;


    if (av_opt_get(s, "rtpflags", AV_OPT_SEARCH_CHILDREN, &rtpflags) >= 0)
        av_dict_set(&opts, "rtpflags", rtpflags, AV_DICT_DONT_STRDUP_VAL);

    /* Set the synchronized start time. */
    rtpctx->start_time_realtime = s->start_time_realtime;

    avcodec_parameters_copy(rtpctx->streams[0]->codecpar, st->codecpar);
    rtpctx->streams[0]->time_base = st->time_base;

    if (handle) {
        ret = ffio_fdopen(&rtpctx->pb, handle);
        if (ret < 0)
            ffurl_close(handle);
    } else
        ret = ffio_open_dyn_packet_buf(&rtpctx->pb, packet_size);
    if (!ret)
        ret = avformat_write_header(rtpctx, &opts);
    av_dict_free(&opts);

    if (ret) {
        if (handle && rtpctx->pb) {
            avio_closep(&rtpctx->pb);
        } else if (rtpctx->pb) {
            ffio_free_dyn_buf(&rtpctx->pb);
        }
        avformat_free_context(rtpctx);
        return ret;
    }

    *out = rtpctx;
    return 0;

fail:
    avformat_free_context(rtpctx);
    if (handle)
        ffurl_close(handle);
    return ret;
}
