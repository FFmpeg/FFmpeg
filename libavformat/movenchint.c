/*
 * MOV, 3GP, MP4 muxer RTP hinting
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

#include "movenc.h"
#include "libavutil/intreadwrite.h"

int ff_mov_init_hinting(AVFormatContext *s, int index, int src_index)
{
    MOVMuxContext *mov  = s->priv_data;
    MOVTrack *track     = &mov->tracks[index];
    MOVTrack *src_track = &mov->tracks[src_index];
    AVStream *src_st    = s->streams[src_index];
    int ret = AVERROR(ENOMEM);
    AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);

    track->tag = MKTAG('r','t','p',' ');
    track->src_track = src_index;

    if (!rtp_format) {
        ret = AVERROR(ENOENT);
        goto fail;
    }

    track->enc = avcodec_alloc_context();
    if (!track->enc)
        goto fail;
    track->enc->codec_type = AVMEDIA_TYPE_DATA;
    track->enc->codec_tag  = track->tag;

    track->rtp_ctx = avformat_alloc_context();
    if (!track->rtp_ctx)
        goto fail;
    track->rtp_ctx->oformat = rtp_format;
    if (!av_new_stream(track->rtp_ctx, 0))
        goto fail;

    /* Copy stream parameters */
    track->rtp_ctx->streams[0]->sample_aspect_ratio =
                        src_st->sample_aspect_ratio;

    /* Remove the allocated codec context, link to the original one
     * instead, to give the rtp muxer access to codec parameters. */
    av_free(track->rtp_ctx->streams[0]->codec);
    track->rtp_ctx->streams[0]->codec = src_st->codec;

    if ((ret = url_open_dyn_packet_buf(&track->rtp_ctx->pb,
                                       RTP_MAX_PACKET_SIZE)) < 0)
        goto fail;
    ret = av_write_header(track->rtp_ctx);
    if (ret)
        goto fail;

    /* Copy the RTP AVStream timebase back to the hint AVStream */
    track->timescale = track->rtp_ctx->streams[0]->time_base.den;

    /* Mark the hinted track that packets written to it should be
     * sent to this track for hinting. */
    src_track->hint_track = index;
    return 0;
fail:
    av_log(s, AV_LOG_WARNING,
           "Unable to initialize hinting of stream %d\n", src_index);
    if (track->rtp_ctx && track->rtp_ctx->pb) {
        uint8_t *buf;
        url_close_dyn_buf(track->rtp_ctx->pb, &buf);
        av_free(buf);
    }
    if (track->rtp_ctx && track->rtp_ctx->streams[0]) {
        av_metadata_free(&track->rtp_ctx->streams[0]->metadata);
        av_free(track->rtp_ctx->streams[0]);
    }
    if (track->rtp_ctx) {
        av_metadata_free(&track->rtp_ctx->metadata);
        av_free(track->rtp_ctx->priv_data);
        av_freep(&track->rtp_ctx);
    }
    av_freep(&track->enc);
    /* Set a default timescale, to avoid crashes in dump_format */
    track->timescale = 90000;
    return ret;
}

static void output_immediate(const uint8_t *data, int size,
                             ByteIOContext *out, int *entries)
{
    while (size > 0) {
        int len = size;
        if (len > 14)
            len = 14;
        put_byte(out, 1); /* immediate constructor */
        put_byte(out, len); /* amount of valid data */
        put_buffer(out, data, len);
        data += len;
        size -= len;

        for (; len < 14; len++)
            put_byte(out, 0);

        (*entries)++;
    }
}

static void describe_payload(const uint8_t *data, int size,
                             ByteIOContext *out, int *entries)
{
    /* Describe the payload using different constructors */
    output_immediate(data, size, out, entries);
}

/**
 * Write an RTP hint (that may contain one or more RTP packets)
 * for the packets in data. data contains one or more packets with a
 * BE32 size header.
 *
 * @param out buffer where the hints are written
 * @param data buffer containing RTP packets
 * @param size the size of the data buffer
 * @param trk the MOVTrack for the hint track
 * @param pts pointer where the timestamp for the written RTP hint is stored
 * @return the number of RTP packets in the written hint
 */
static int write_hint_packets(ByteIOContext *out, const uint8_t *data,
                              int size, MOVTrack *trk, int64_t *pts)
{
    int64_t curpos;
    int64_t count_pos, entries_pos;
    int count = 0, entries;

    count_pos = url_ftell(out);
    /* RTPsample header */
    put_be16(out, 0); /* packet count */
    put_be16(out, 0); /* reserved */

    while (size > 4) {
        uint32_t packet_len = AV_RB32(data);
        uint16_t seq;
        uint32_t ts;

        data += 4;
        size -= 4;
        if (packet_len > size || packet_len <= 12)
            break;
        if (data[1] >= 200 && data[1] <= 204) {
            /* RTCP packet, just skip */
            data += packet_len;
            size -= packet_len;
            continue;
        }

        if (packet_len > trk->max_packet_size)
            trk->max_packet_size = packet_len;

        seq = AV_RB16(&data[2]);
        ts = AV_RB32(&data[4]);

        if (trk->prev_rtp_ts == 0)
            trk->prev_rtp_ts = ts;
        /* Unwrap the 32-bit RTP timestamp that wraps around often
         * into a not (as often) wrapping 64-bit timestamp. */
        trk->cur_rtp_ts_unwrapped += (int32_t) (ts - trk->prev_rtp_ts);
        trk->prev_rtp_ts = ts;
        if (*pts == AV_NOPTS_VALUE)
            *pts = trk->cur_rtp_ts_unwrapped;

        count++;
        /* RTPpacket header */
        put_be32(out, 0); /* relative_time */
        put_buffer(out, data, 2); /* RTP header */
        put_be16(out, seq); /* RTPsequenceseed */
        put_be16(out, 0); /* reserved + flags */
        entries_pos = url_ftell(out);
        put_be16(out, 0); /* entry count */

        data += 12;
        size -= 12;
        packet_len -= 12;

        entries = 0;
        /* Write one or more constructors describing the payload data */
        describe_payload(data, packet_len, out, &entries);
        data += packet_len;
        size -= packet_len;

        curpos = url_ftell(out);
        url_fseek(out, entries_pos, SEEK_SET);
        put_be16(out, entries);
        url_fseek(out, curpos, SEEK_SET);
    }

    curpos = url_ftell(out);
    url_fseek(out, count_pos, SEEK_SET);
    put_be16(out, count);
    url_fseek(out, curpos, SEEK_SET);
    return count;
}

int ff_mov_add_hinted_packet(AVFormatContext *s, AVPacket *pkt,
                             int track_index, int sample)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *trk = &mov->tracks[track_index];
    AVFormatContext *rtp_ctx = trk->rtp_ctx;
    uint8_t *buf = NULL;
    int size;
    ByteIOContext *hintbuf = NULL;
    AVPacket hint_pkt;
    AVPacket local_pkt;
    int ret = 0, count;

    if (!rtp_ctx)
        return AVERROR(ENOENT);
    if (!rtp_ctx->pb)
        return AVERROR(ENOMEM);

    /* Feed the packet to the RTP muxer */
    local_pkt = *pkt;
    local_pkt.stream_index = 0;
    local_pkt.pts = av_rescale_q(pkt->pts,
        s->streams[pkt->stream_index]->time_base,
        rtp_ctx->streams[0]->time_base);
    local_pkt.dts = av_rescale_q(pkt->dts,
        s->streams[pkt->stream_index]->time_base,
        rtp_ctx->streams[0]->time_base);
    av_write_frame(rtp_ctx, &local_pkt);

    /* Fetch the output from the RTP muxer, open a new output buffer
     * for next time. */
    size = url_close_dyn_buf(rtp_ctx->pb, &buf);
    if ((ret = url_open_dyn_packet_buf(&rtp_ctx->pb,
                                       RTP_MAX_PACKET_SIZE)) < 0)
        goto done;

    if (size <= 0)
        goto done;

    /* Open a buffer for writing the hint */
    if ((ret = url_open_dyn_buf(&hintbuf)) < 0)
        goto done;
    av_init_packet(&hint_pkt);
    count = write_hint_packets(hintbuf, buf, size, trk, &hint_pkt.dts);
    av_freep(&buf);

    /* Write the hint data into the hint track */
    hint_pkt.size = size = url_close_dyn_buf(hintbuf, &buf);
    hint_pkt.data = buf;
    hint_pkt.pts  = hint_pkt.dts;
    hint_pkt.stream_index = track_index;
    if (pkt->flags & AV_PKT_FLAG_KEY)
        hint_pkt.flags |= AV_PKT_FLAG_KEY;
    if (count > 0)
        ff_mov_write_packet(s, &hint_pkt);
done:
    av_free(buf);
    return ret;
}

void ff_mov_close_hinting(MOVTrack *track) {
    AVFormatContext* rtp_ctx = track->rtp_ctx;
    uint8_t *ptr;

    av_freep(&track->enc);
    if (!rtp_ctx)
        return;
    if (rtp_ctx->pb) {
        av_write_trailer(rtp_ctx);
        url_close_dyn_buf(rtp_ctx->pb, &ptr);
        av_free(ptr);
    }
    av_metadata_free(&rtp_ctx->streams[0]->metadata);
    av_metadata_free(&rtp_ctx->metadata);
    av_free(rtp_ctx->streams[0]);
    av_freep(&rtp_ctx);
}

