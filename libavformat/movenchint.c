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
#include "internal.h"
#include "rtpenc_chain.h"
#include "avio_internal.h"

int ff_mov_init_hinting(AVFormatContext *s, int index, int src_index)
{
    MOVMuxContext *mov  = s->priv_data;
    MOVTrack *track     = &mov->tracks[index];
    MOVTrack *src_track = &mov->tracks[src_index];
    AVStream *src_st    = s->streams[src_index];
    int ret = AVERROR(ENOMEM);

    track->tag = MKTAG('r','t','p',' ');
    track->src_track = src_index;

    track->enc = avcodec_alloc_context3(NULL);
    if (!track->enc)
        goto fail;
    track->enc->codec_type = AVMEDIA_TYPE_DATA;
    track->enc->codec_tag  = track->tag;

    track->rtp_ctx = ff_rtp_chain_mux_open(s, src_st, NULL,
                                           RTP_MAX_PACKET_SIZE);
    if (!track->rtp_ctx)
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
    av_freep(&track->enc);
    /* Set a default timescale, to avoid crashes in av_dump_format */
    track->timescale = 90000;
    return ret;
}

/**
 * Remove the first sample from the sample queue.
 */
static void sample_queue_pop(HintSampleQueue *queue)
{
    if (queue->len <= 0)
        return;
    if (queue->samples[0].own_data)
        av_free(queue->samples[0].data);
    queue->len--;
    memmove(queue->samples, queue->samples + 1, sizeof(HintSample)*queue->len);
}

/**
 * Empty the sample queue, releasing all memory.
 */
static void sample_queue_free(HintSampleQueue *queue)
{
    int i;
    for (i = 0; i < queue->len; i++)
        if (queue->samples[i].own_data)
            av_free(queue->samples[i].data);
    av_freep(&queue->samples);
    queue->len = 0;
    queue->size = 0;
}

/**
 * Add a reference to the sample data to the sample queue. The data is
 * not copied. sample_queue_retain should be called before pkt->data
 * is reused/freed.
 */
static void sample_queue_push(HintSampleQueue *queue, uint8_t *data, int size,
                              int sample)
{
    /* No need to keep track of smaller samples, since describing them
     * with immediates is more efficient. */
    if (size <= 14)
        return;
    if (!queue->samples || queue->len >= queue->size) {
        HintSample* samples;
        queue->size += 10;
        samples = av_realloc(queue->samples, sizeof(HintSample)*queue->size);
        if (!samples)
            return;
        queue->samples = samples;
    }
    queue->samples[queue->len].data = data;
    queue->samples[queue->len].size = size;
    queue->samples[queue->len].sample_number = sample;
    queue->samples[queue->len].offset = 0;
    queue->samples[queue->len].own_data = 0;
    queue->len++;
}

/**
 * Make local copies of all referenced sample data in the queue.
 */
static void sample_queue_retain(HintSampleQueue *queue)
{
    int i;
    for (i = 0; i < queue->len; ) {
        HintSample *sample = &queue->samples[i];
        if (!sample->own_data) {
            uint8_t* ptr = av_malloc(sample->size);
            if (!ptr) {
                /* Unable to allocate memory for this one, remove it */
                memmove(queue->samples + i, queue->samples + i + 1,
                        sizeof(HintSample)*(queue->len - i - 1));
                queue->len--;
                continue;
            }
            memcpy(ptr, sample->data, sample->size);
            sample->data = ptr;
            sample->own_data = 1;
        }
        i++;
    }
}

/**
 * Find matches of needle[n_pos ->] within haystack. If a sufficiently
 * large match is found, matching bytes before n_pos are included
 * in the match, too (within the limits of the arrays).
 *
 * @param haystack buffer that may contain parts of needle
 * @param h_len length of the haystack buffer
 * @param needle buffer containing source data that have been used to
 *               construct haystack
 * @param n_pos start position in needle used for looking for matches
 * @param n_len length of the needle buffer
 * @param match_h_offset_ptr offset of the first matching byte within haystack
 * @param match_n_offset_ptr offset of the first matching byte within needle
 * @param match_len_ptr length of the matched segment
 * @return 0 if a match was found, < 0 if no match was found
 */
static int match_segments(const uint8_t *haystack, int h_len,
                          const uint8_t *needle, int n_pos, int n_len,
                          int *match_h_offset_ptr, int *match_n_offset_ptr,
                          int *match_len_ptr)
{
    int h_pos;
    for (h_pos = 0; h_pos < h_len; h_pos++) {
        int match_len = 0;
        int match_h_pos, match_n_pos;

        /* Check how many bytes match at needle[n_pos] and haystack[h_pos] */
        while (h_pos + match_len < h_len && n_pos + match_len < n_len &&
               needle[n_pos + match_len] == haystack[h_pos + match_len])
            match_len++;
        if (match_len <= 8)
            continue;

        /* If a sufficiently large match was found, try to expand
         * the matched segment backwards. */
        match_h_pos = h_pos;
        match_n_pos = n_pos;
        while (match_n_pos > 0 && match_h_pos > 0 &&
               needle[match_n_pos - 1] == haystack[match_h_pos - 1]) {
            match_n_pos--;
            match_h_pos--;
            match_len++;
        }
        if (match_len <= 14)
            continue;
        *match_h_offset_ptr = match_h_pos;
        *match_n_offset_ptr = match_n_pos;
        *match_len_ptr = match_len;
        return 0;
    }
    return -1;
}

/**
 * Look for segments in samples in the sample queue matching the data
 * in ptr. Samples not matching are removed from the queue. If a match
 * is found, the next time it will look for matches starting from the
 * end of the previous matched segment.
 *
 * @param data data to find matches for in the sample queue
 * @param len length of the data buffer
 * @param queue samples used for looking for matching segments
 * @param pos the offset in data of the matched segment
 * @param match_sample the number of the sample that contained the match
 * @param match_offset the offset of the matched segment within the sample
 * @param match_len the length of the matched segment
 * @return 0 if a match was found, < 0 if no match was found
 */
static int find_sample_match(const uint8_t *data, int len,
                             HintSampleQueue *queue, int *pos,
                             int *match_sample, int *match_offset,
                             int *match_len)
{
    while (queue->len > 0) {
        HintSample *sample = &queue->samples[0];
        /* If looking for matches in a new sample, skip the first 5 bytes,
         * since they often may be modified/removed in the output packet. */
        if (sample->offset == 0 && sample->size > 5)
            sample->offset = 5;

        if (match_segments(data, len, sample->data, sample->offset,
                           sample->size, pos, match_offset, match_len) == 0) {
            *match_sample = sample->sample_number;
            /* Next time, look for matches at this offset, with a little
             * margin to this match. */
            sample->offset = *match_offset + *match_len + 5;
            if (sample->offset + 10 >= sample->size)
                sample_queue_pop(queue); /* Not enough useful data left */
            return 0;
        }

        if (sample->offset < 10 && sample->size > 20) {
            /* No match found from the start of the sample,
             * try from the middle of the sample instead. */
            sample->offset = sample->size/2;
        } else {
            /* No match for this sample, remove it */
            sample_queue_pop(queue);
        }
    }
    return -1;
}

static void output_immediate(const uint8_t *data, int size,
                             AVIOContext *out, int *entries)
{
    while (size > 0) {
        int len = size;
        if (len > 14)
            len = 14;
        avio_w8(out, 1); /* immediate constructor */
        avio_w8(out, len); /* amount of valid data */
        avio_write(out, data, len);
        data += len;
        size -= len;

        for (; len < 14; len++)
            avio_w8(out, 0);

        (*entries)++;
    }
}

static void output_match(AVIOContext *out, int match_sample,
                         int match_offset, int match_len, int *entries)
{
    avio_w8(out, 2); /* sample constructor */
    avio_w8(out, 0); /* track reference */
    avio_wb16(out, match_len);
    avio_wb32(out, match_sample);
    avio_wb32(out, match_offset);
    avio_wb16(out, 1); /* bytes per block */
    avio_wb16(out, 1); /* samples per block */
    (*entries)++;
}

static void describe_payload(const uint8_t *data, int size,
                             AVIOContext *out, int *entries,
                             HintSampleQueue *queue)
{
    /* Describe the payload using different constructors */
    while (size > 0) {
        int match_sample, match_offset, match_len, pos;
        if (find_sample_match(data, size, queue, &pos, &match_sample,
                              &match_offset, &match_len) < 0)
            break;
        output_immediate(data, pos, out, entries);
        data += pos;
        size -= pos;
        output_match(out, match_sample, match_offset, match_len, entries);
        data += match_len;
        size -= match_len;
    }
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
static int write_hint_packets(AVIOContext *out, const uint8_t *data,
                              int size, MOVTrack *trk, int64_t *pts)
{
    int64_t curpos;
    int64_t count_pos, entries_pos;
    int count = 0, entries;

    count_pos = avio_tell(out);
    /* RTPsample header */
    avio_wb16(out, 0); /* packet count */
    avio_wb16(out, 0); /* reserved */

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
        avio_wb32(out, 0); /* relative_time */
        avio_write(out, data, 2); /* RTP header */
        avio_wb16(out, seq); /* RTPsequenceseed */
        avio_wb16(out, 0); /* reserved + flags */
        entries_pos = avio_tell(out);
        avio_wb16(out, 0); /* entry count */

        data += 12;
        size -= 12;
        packet_len -= 12;

        entries = 0;
        /* Write one or more constructors describing the payload data */
        describe_payload(data, packet_len, out, &entries, &trk->sample_queue);
        data += packet_len;
        size -= packet_len;

        curpos = avio_tell(out);
        avio_seek(out, entries_pos, SEEK_SET);
        avio_wb16(out, entries);
        avio_seek(out, curpos, SEEK_SET);
    }

    curpos = avio_tell(out);
    avio_seek(out, count_pos, SEEK_SET);
    avio_wb16(out, count);
    avio_seek(out, curpos, SEEK_SET);
    return count;
}

int ff_mov_add_hinted_packet(AVFormatContext *s, AVPacket *pkt,
                             int track_index, int sample,
                             uint8_t *sample_data, int sample_size)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *trk = &mov->tracks[track_index];
    AVFormatContext *rtp_ctx = trk->rtp_ctx;
    uint8_t *buf = NULL;
    int size;
    AVIOContext *hintbuf = NULL;
    AVPacket hint_pkt;
    int ret = 0, count;

    if (!rtp_ctx)
        return AVERROR(ENOENT);
    if (!rtp_ctx->pb)
        return AVERROR(ENOMEM);

    if (sample_data)
        sample_queue_push(&trk->sample_queue, sample_data, sample_size, sample);
    else
        sample_queue_push(&trk->sample_queue, pkt->data, pkt->size, sample);

    /* Feed the packet to the RTP muxer */
    ff_write_chained(rtp_ctx, 0, pkt, s);

    /* Fetch the output from the RTP muxer, open a new output buffer
     * for next time. */
    size = avio_close_dyn_buf(rtp_ctx->pb, &buf);
    if ((ret = ffio_open_dyn_packet_buf(&rtp_ctx->pb,
                                       RTP_MAX_PACKET_SIZE)) < 0)
        goto done;

    if (size <= 0)
        goto done;

    /* Open a buffer for writing the hint */
    if ((ret = avio_open_dyn_buf(&hintbuf)) < 0)
        goto done;
    av_init_packet(&hint_pkt);
    count = write_hint_packets(hintbuf, buf, size, trk, &hint_pkt.dts);
    av_freep(&buf);

    /* Write the hint data into the hint track */
    hint_pkt.size = size = avio_close_dyn_buf(hintbuf, &buf);
    hint_pkt.data = buf;
    hint_pkt.pts  = hint_pkt.dts;
    hint_pkt.stream_index = track_index;
    if (pkt->flags & AV_PKT_FLAG_KEY)
        hint_pkt.flags |= AV_PKT_FLAG_KEY;
    if (count > 0)
        ff_mov_write_packet(s, &hint_pkt);
done:
    av_free(buf);
    sample_queue_retain(&trk->sample_queue);
    return ret;
}

void ff_mov_close_hinting(MOVTrack *track) {
    AVFormatContext* rtp_ctx = track->rtp_ctx;
    uint8_t *ptr;

    av_freep(&track->enc);
    sample_queue_free(&track->sample_queue);
    if (!rtp_ctx)
        return;
    if (rtp_ctx->pb) {
        av_write_trailer(rtp_ctx);
        avio_close_dyn_buf(rtp_ctx->pb, &ptr);
        av_free(ptr);
    }
    avformat_free_context(rtp_ctx);
}

