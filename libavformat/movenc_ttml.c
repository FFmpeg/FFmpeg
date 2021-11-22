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

#include "libavutil/mem.h"
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

static void mov_calculate_start_and_end_of_other_tracks(
    AVFormatContext *s, MOVTrack *track, int64_t *start_pts, int64_t *end_pts)
{
    MOVMuxContext *mov = s->priv_data;

    // Initialize at the end of the previous document/fragment, which is NOPTS
    // until the first fragment is created.
    int64_t max_track_end_dts = *start_pts = track->end_pts;

    for (unsigned int i = 0; i < s->nb_streams; i++) {
        MOVTrack *other_track = &mov->tracks[i];

        // Skip our own track, any other track that needs squashing,
        // or any track which still has its start_dts at NOPTS or
        // any track that did not yet get any packets.
        if (track == other_track ||
            other_track->squash_fragment_samples_to_one ||
            other_track->start_dts == AV_NOPTS_VALUE ||
            !other_track->entry) {
            continue;
        }

        int64_t picked_start = av_rescale_q_rnd(other_track->cluster[0].dts + other_track->cluster[0].cts,
                                                other_track->st->time_base,
                                                track->st->time_base,
                                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        int64_t picked_end   = av_rescale_q_rnd(other_track->end_pts,
                                                other_track->st->time_base,
                                                track->st->time_base,
                                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

        if (*start_pts == AV_NOPTS_VALUE)
            *start_pts = picked_start;
        else if (picked_start >= track->end_pts)
            *start_pts = FFMIN(*start_pts, picked_start);

        max_track_end_dts = FFMAX(max_track_end_dts, picked_end);
    }

    *end_pts = max_track_end_dts;
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
    unsigned int time_limited = 0;
    PacketList back_to_queue_list = { 0 };

    if (*out_start_ts != AV_NOPTS_VALUE) {
        // we have non-nopts values here, thus we have been given a time range
        time_limited = 1;
        start_ts = *out_start_ts;
        end_ts   = *out_start_ts + *out_duration;
    }

    if ((ret = avformat_write_header(ttml_ctx, NULL)) < 0) {
        return ret;
    }

    while (!avpriv_packet_list_get(&track->squashed_packet_queue, pkt)) {
        int64_t pts_before      = pkt->pts;
        int64_t duration_before = pkt->duration;

        if (time_limited) {
            // special cases first:
            if (pkt->pts + pkt->duration < start_ts) {
                // too late for our fragment, unfortunately
                // unref and proceed to next packet in queue.
                av_log(s, AV_LOG_WARNING,
                       "Very late TTML packet in queue, dropping packet with "
                       "pts: %"PRId64", duration: %"PRId64"\n",
                       pkt->pts, pkt->duration);
                av_packet_unref(pkt);
                continue;
            } else if (pkt->pts >= end_ts) {
                // starts after this fragment, put back to original queue
                ret = avpriv_packet_list_put(&track->squashed_packet_queue,
                                             pkt, av_packet_ref,
                                             FF_PACKETLIST_FLAG_PREPEND);
                if (ret < 0)
                    goto cleanup;

                break;
            }

            // limit packet pts to start_ts
            if (pkt->pts < start_ts) {
                pkt->duration -= start_ts - pkt->pts;
                pkt->pts = start_ts;
            }

            if (pkt->pts + pkt->duration > end_ts) {
                // goes over our current fragment, create duplicate and
                // put it back to list after iteration has finished in
                // order to handle multiple subtitles at the same time.
                int64_t offset = end_ts - pkt->pts;

                ret = avpriv_packet_list_put(&back_to_queue_list,
                                             pkt, av_packet_ref,
                                             FF_PACKETLIST_FLAG_PREPEND);
                if (ret < 0)
                    goto cleanup;

                back_to_queue_list.head->pkt.pts =
                back_to_queue_list.head->pkt.dts =
                back_to_queue_list.head->pkt.pts + offset;
                back_to_queue_list.head->pkt.duration -= offset;

                // and for our normal packet we just set duration to offset
                pkt->duration = offset;
            }
        } else {
            end_ts = FFMAX(end_ts, pkt->pts + pkt->duration);
        }

        av_log(s, AV_LOG_TRACE,
               "TTML packet writeout: pts: %"PRId64" (%"PRId64"), "
               "duration: %"PRId64"\n",
               pkt->pts, pkt->pts - start_ts, pkt->duration);
        if (pkt->pts != pts_before || pkt->duration != duration_before) {
            av_log(s, AV_LOG_TRACE,
                   "Adjustments: pts: %"PRId64", duration: %"PRId64"\n",
                   pkt->pts - pts_before, pkt->duration - duration_before);
        }

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
    while (!avpriv_packet_list_get(&back_to_queue_list, pkt)) {
        ret = avpriv_packet_list_put(&track->squashed_packet_queue,
                                     pkt, av_packet_ref,
                                     FF_PACKETLIST_FLAG_PREPEND);

        // unrelated to whether we succeed or not, we unref the packet
        // received from the temporary list.
        av_packet_unref(pkt);

        if (ret < 0) {
            avpriv_packet_list_free(&back_to_queue_list);
            break;
        }
    }
    return ret;
}

int ff_mov_generate_squashed_ttml_packet(AVFormatContext *s,
                                         MOVTrack *track, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    AVFormatContext *ttml_ctx = NULL;
    // values for the generated AVPacket
    int64_t start_ts = AV_NOPTS_VALUE;
    int64_t duration = 0;

    int ret = AVERROR_BUG;

    if ((ret = mov_init_ttml_writer(track, &ttml_ctx)) < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to initialize the TTML writer: %s\n",
               av_err2str(ret));
        goto cleanup;
    }

    if (mov->flags & FF_MOV_FLAG_FRAGMENT) {
        int64_t calculated_start = AV_NOPTS_VALUE;
        int64_t calculated_end = AV_NOPTS_VALUE;

        mov_calculate_start_and_end_of_other_tracks(s, track, &calculated_start, &calculated_end);

        if (calculated_start != AV_NOPTS_VALUE) {
            start_ts = calculated_start;
            duration = calculated_end - calculated_start;
            av_log(s, AV_LOG_VERBOSE,
                   "Calculated subtitle fragment start: %"PRId64", "
                   "duration: %"PRId64"\n",
                   start_ts, duration);
        }
    }

    if (!track->squashed_packet_queue.head) {
        // empty queue, write minimal empty document with zero duration
        avio_write(ttml_ctx->pb, empty_ttml_document,
                   sizeof(empty_ttml_document) - 1);
        if (start_ts == AV_NOPTS_VALUE) {
            start_ts = 0;
            duration = 0;
        }
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
