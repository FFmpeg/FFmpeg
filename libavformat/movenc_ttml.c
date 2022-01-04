/*
 * MP4, ISMV Muxer TTML helpers
 * Copyright (c) 2021 24i
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
#include "isom.h"
#include "movenc.h"
#include "movenc_ttml.h"
#include "libavcodec/packet_internal.h"

static const unsigned char empty_ttml_document[] =
    "<tt xml:lang=\"\" xmlns=\"http://www.w3.org/ns/ttml\" />";

static int mov_init_ttml_writer(MOVTrack *track, AVFormatContext **out_ctx)
{
    AVStream *movenc_stream = track->st, *ttml_stream = NULL;
    int ret = AVERROR_BUG;

    if ((ret = avformat_alloc_output_context2(out_ctx, NULL,
                                              "ttml", NULL)) < 0)
        return ret;

    if ((ret = avio_open_dyn_buf(&(*out_ctx)->pb)) < 0)
        return ret;

    if (!(ttml_stream = avformat_new_stream(*out_ctx, NULL))) {
        return AVERROR(ENOMEM);
    }

    if ((ret = avcodec_parameters_copy(ttml_stream->codecpar,
                                       movenc_stream->codecpar)) < 0)
        return ret;

    ttml_stream->time_base = movenc_stream->time_base;

    return 0;
}

static int mov_write_ttml_document_from_queue(AVFormatContext *s,
                                              AVFormatContext *ttml_ctx,
                                              MOVTrack *track,
                                              AVPacket *pkt,
                                              int64_t *out_start_ts,
                                              int64_t *out_duration)
{
    int ret = AVERROR_BUG;
    int64_t start_ts = track->start_dts == AV_NOPTS_VALUE ?
                       0 : (track->start_dts + track->track_duration);
    int64_t end_ts   = start_ts;

    if ((ret = avformat_write_header(ttml_ctx, NULL)) < 0) {
        return ret;
    }

    while (!avpriv_packet_list_get(&track->squashed_packet_queue, pkt)) {
        end_ts = FFMAX(end_ts, pkt->pts + pkt->duration);

        // in case of the 'dfxp' muxing mode, each written document is offset
        // to its containing sample's beginning.
        if (track->par->codec_tag == MOV_ISMV_TTML_TAG) {
            pkt->dts = pkt->pts = (pkt->pts - start_ts);
        }

        pkt->stream_index = 0;

        av_packet_rescale_ts(pkt, track->st->time_base,
                             ttml_ctx->streams[pkt->stream_index]->time_base);

        if ((ret = av_write_frame(ttml_ctx, pkt)) < 0) {
            goto cleanup;
        }

        av_packet_unref(pkt);
    }

    if ((ret = av_write_trailer(ttml_ctx)) < 0)
        goto cleanup;

    *out_start_ts = start_ts;
    *out_duration = end_ts - start_ts;

    ret = 0;

cleanup:
    return ret;
}

int ff_mov_generate_squashed_ttml_packet(AVFormatContext *s,
                                         MOVTrack *track, AVPacket *pkt)
{
    AVFormatContext *ttml_ctx = NULL;
    // values for the generated AVPacket
    int64_t start_ts = 0;
    int64_t duration = 0;

    int ret = AVERROR_BUG;

    if ((ret = mov_init_ttml_writer(track, &ttml_ctx)) < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to initialize the TTML writer: %s\n",
               av_err2str(ret));
        goto cleanup;
    }

    if (!track->squashed_packet_queue.head) {
        // empty queue, write minimal empty document with zero duration
        avio_write(ttml_ctx->pb, empty_ttml_document,
                   sizeof(empty_ttml_document) - 1);
        start_ts = 0;
        duration = 0;
        goto generate_packet;
    }

    if ((ret = mov_write_ttml_document_from_queue(s, ttml_ctx, track, pkt,
                                                  &start_ts,
                                                  &duration)) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Failed to generate a squashed TTML packet from the packet "
               "queue: %s\n",
               av_err2str(ret));
        goto cleanup;
    }

generate_packet:
    {
        // Generate an AVPacket from the data written into the dynamic buffer.
        uint8_t *buf = NULL;
        int buf_len = avio_close_dyn_buf(ttml_ctx->pb, &buf);
        ttml_ctx->pb = NULL;

        if ((ret = av_packet_from_data(pkt, buf, buf_len)) < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Failed to create a TTML AVPacket from AVIO data: %s\n",
                   av_err2str(ret));
            av_freep(&buf);
            goto cleanup;
        }

        pkt->pts = pkt->dts = start_ts;
        pkt->duration = duration;
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    ret = 0;

cleanup:
    if (ttml_ctx)
        ffio_free_dyn_buf(&ttml_ctx->pb);

    avformat_free_context(ttml_ctx);
    return ret;
}
