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
#include "rtpenc_chain.h"

AVFormatContext *ff_rtp_chain_mux_open(AVFormatContext *s, AVStream *st,
                                       URLContext *handle, int packet_size)
{
    AVFormatContext *rtpctx;
    int ret;
    AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);

    if (!rtp_format)
        return NULL;

    /* Allocate an AVFormatContext for each output stream */
    rtpctx = avformat_alloc_context();
    if (!rtpctx)
        return NULL;

    rtpctx->oformat = rtp_format;
    if (!av_new_stream(rtpctx, 0)) {
        av_free(rtpctx);
        return NULL;
    }
    /* Copy the max delay setting; the rtp muxer reads this. */
    rtpctx->max_delay = s->max_delay;
    /* Copy other stream parameters. */
    rtpctx->streams[0]->sample_aspect_ratio = st->sample_aspect_ratio;

    /* Set the synchronized start time. */
    rtpctx->start_time_realtime = s->start_time_realtime;

    /* Remove the local codec, link to the original codec
     * context instead, to give the rtp muxer access to
     * codec parameters. */
    av_free(rtpctx->streams[0]->codec);
    rtpctx->streams[0]->codec = st->codec;

    if (handle) {
        url_fdopen(&rtpctx->pb, handle);
    } else
        url_open_dyn_packet_buf(&rtpctx->pb, packet_size);
    ret = av_write_header(rtpctx);

    if (ret) {
        if (handle) {
            url_fclose(rtpctx->pb);
        } else {
            uint8_t *ptr;
            url_close_dyn_buf(rtpctx->pb, &ptr);
            av_free(ptr);
        }
        av_free(rtpctx->streams[0]);
        av_free(rtpctx);
        return NULL;
    }

    /* Copy the RTP AVStream timebase back to the original AVStream */
    st->time_base = rtpctx->streams[0]->time_base;
    return rtpctx;
}

