/*
 * xWMA demuxer
 * Copyright (c) 2011 Max Horn
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

#include <inttypes.h>

#include "avformat.h"
#include "internal.h"
#include "riff.h"

/*
 * Demuxer for xWMA, a Microsoft audio container used by XAudio 2.
 */

typedef struct {
    int64_t data_end;
} XWMAContext;

static int xwma_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, "RIFF", 4) && !memcmp(p->buf + 8, "XWMA", 4))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int xwma_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int64_t size, av_uninit(data_size);
    int ret;
    uint32_t dpds_table_size = 0;
    uint32_t *dpds_table = 0;
    unsigned int tag;
    AVIOContext *pb = s->pb;
    AVStream *st;
    XWMAContext *xwma = s->priv_data;
    int i;

    /* The following code is mostly copied from wav.c, with some
     * minor alterations.
     */

    /* check RIFF header */
    tag = avio_rl32(pb);
    if (tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    avio_rl32(pb); /* file size */
    tag = avio_rl32(pb);
    if (tag != MKTAG('X', 'W', 'M', 'A'))
        return -1;

    /* parse fmt header */
    tag = avio_rl32(pb);
    if (tag != MKTAG('f', 'm', 't', ' '))
        return -1;
    size = avio_rl32(pb);
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    ret = ff_get_wav_header(pb, st->codec, size);
    if (ret < 0)
        return ret;
    st->need_parsing = AVSTREAM_PARSE_NONE;

    /* All xWMA files I have seen contained WMAv2 data. If there are files
     * using WMA Pro or some other codec, then we need to figure out the right
     * extradata for that. Thus, ask the user for feedback, but try to go on
     * anyway.
     */
    if (st->codec->codec_id != CODEC_ID_WMAV2) {
        av_log(s, AV_LOG_WARNING, "unexpected codec (tag 0x04%x; id %d)\n",
                              st->codec->codec_tag, st->codec->codec_id);
        av_log_ask_for_sample(s, NULL);
    } else {
        /* In all xWMA files I have seen, there is no extradata. But the WMA
         * codecs require extradata, so we provide our own fake extradata.
         *
         * First, check that there really was no extradata in the header. If
         * there was, then try to use it, after asking the user to provide a
         * sample of this unusual file.
         */
        if (st->codec->extradata_size != 0) {
            /* Surprise, surprise: We *did* get some extradata. No idea
             * if it will work, but just go on and try it, after asking
             * the user for a sample.
             */
            av_log(s, AV_LOG_WARNING, "unexpected extradata (%d bytes)\n",
                                  st->codec->extradata_size);
            av_log_ask_for_sample(s, NULL);
        } else {
            st->codec->extradata_size = 6;
            st->codec->extradata      = av_mallocz(6 + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);

            /* setup extradata with our experimentally obtained value */
            st->codec->extradata[4] = 31;
        }
    }

    if (!st->codec->channels) {
        av_log(s, AV_LOG_WARNING, "Invalid channel count: %d\n",
               st->codec->channels);
        return AVERROR_INVALIDDATA;
    }
    if (!st->codec->bits_per_coded_sample) {
        av_log(s, AV_LOG_WARNING, "Invalid bits_per_coded_sample: %d\n",
               st->codec->bits_per_coded_sample);
        return AVERROR_INVALIDDATA;
    }

    /* set the sample rate */
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    /* parse the remaining RIFF chunks */
    for (;;) {
        if (pb->eof_reached)
            return -1;
        /* read next chunk tag */
        tag = avio_rl32(pb);
        size = avio_rl32(pb);
        if (tag == MKTAG('d', 'a', 't', 'a')) {
            /* We assume that the data chunk comes last. */
            break;
        } else if (tag == MKTAG('d','p','d','s')) {
            /* Quoting the MSDN xWMA docs on the dpds chunk: "Contains the
             * decoded packet cumulative data size array, each element is the
             * number of bytes accumulated after the corresponding xWMA packet
             * is decoded in order."
             *
             * Each packet has size equal to st->codec->block_align, which in
             * all cases I saw so far was always 2230. Thus, we can use the
             * dpds data to compute a seeking index.
             */

            /* Error out if there is more than one dpds chunk. */
            if (dpds_table) {
                av_log(s, AV_LOG_ERROR, "two dpds chunks present\n");
                return -1;
            }

            /* Compute the number of entries in the dpds chunk. */
            if (size & 3) {  /* Size should be divisible by four */
                av_log(s, AV_LOG_WARNING,
                       "dpds chunk size %"PRId64" not divisible by 4\n", size);
            }
            dpds_table_size = size / 4;
            if (dpds_table_size == 0 || dpds_table_size >= INT_MAX / 4) {
                av_log(s, AV_LOG_ERROR,
                       "dpds chunk size %"PRId64" invalid\n", size);
                return -1;
            }

            /* Allocate some temporary storage to keep the dpds data around.
             * for processing later on.
             */
            dpds_table = av_malloc(dpds_table_size * sizeof(uint32_t));
            if (!dpds_table) {
                return AVERROR(ENOMEM);
            }

            for (i = 0; i < dpds_table_size; ++i) {
                dpds_table[i] = avio_rl32(pb);
                size -= 4;
            }
        }
        avio_skip(pb, size);
    }

    /* Determine overall data length */
    if (size < 0)
        return -1;
    if (!size) {
        xwma->data_end = INT64_MAX;
    } else
        xwma->data_end = avio_tell(pb) + size;


    if (dpds_table && dpds_table_size) {
        int64_t cur_pos;
        const uint32_t bytes_per_sample
                = (st->codec->channels * st->codec->bits_per_coded_sample) >> 3;

        /* Estimate the duration from the total number of output bytes. */
        const uint64_t total_decoded_bytes = dpds_table[dpds_table_size - 1];
        st->duration = total_decoded_bytes / bytes_per_sample;

        /* Use the dpds data to build a seek table.  We can only do this after
         * we know the offset to the data chunk, as we need that to determine
         * the actual offset to each input block.
         * Note: If we allowed ourselves to assume that the data chunk always
         * follows immediately after the dpds block, we could of course guess
         * the data block's start offset already while reading the dpds chunk.
         * I decided against that, just in case other chunks ever are
         * discovered.
         */
        cur_pos = avio_tell(pb);
        for (i = 0; i < dpds_table_size; ++i) {
            /* From the number of output bytes that would accumulate in the
             * output buffer after decoding the first (i+1) packets, we compute
             * an offset / timestamp pair.
             */
            av_add_index_entry(st,
                               cur_pos + (i+1) * st->codec->block_align, /* pos */
                               dpds_table[i] / bytes_per_sample,         /* timestamp */
                               st->codec->block_align,                   /* size */
                               0,                                        /* duration */
                               AVINDEX_KEYFRAME);
        }
    } else if (st->codec->bit_rate) {
        /* No dpds chunk was present (or only an empty one), so estimate
         * the total duration using the average bits per sample and the
         * total data length.
         */
        st->duration = (size<<3) * st->codec->sample_rate / st->codec->bit_rate;
    }

    av_free(dpds_table);

    return 0;
}

static int xwma_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    int64_t left;
    AVStream *st;
    XWMAContext *xwma = s->priv_data;

    st = s->streams[0];

    left = xwma->data_end - avio_tell(s->pb);
    if (left <= 0) {
        return AVERROR_EOF;
    }

    /* read a single block; the default block size is 2230. */
    size = (st->codec->block_align > 1) ? st->codec->block_align : 2230;
    size = FFMIN(size, left);

    ret  = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;

    pkt->stream_index = 0;
    return ret;
}

AVInputFormat ff_xwma_demuxer = {
    .name           = "xwma",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft xWMA"),
    .priv_data_size = sizeof(XWMAContext),
    .read_probe     = xwma_probe,
    .read_header    = xwma_read_header,
    .read_packet    = xwma_read_packet,
};
