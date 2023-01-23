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

/* shared code for simple demux/decode tools */

#include <stdlib.h>
#include <string.h>

#include "decode_simple.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"

#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"

static int decode_read(DecodeContext *dc, int flush)
{
    const int ret_done = flush ? AVERROR_EOF : AVERROR(EAGAIN);
    int ret = 0;

    while (ret >= 0 &&
           (dc->max_frames == 0 || dc->decoder->frame_num < dc->max_frames)) {
        ret = avcodec_receive_frame(dc->decoder, dc->frame);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                int err = dc->process_frame(dc, NULL);
                if (err < 0)
                    return err;
            }

            return (ret == ret_done) ? 0 : ret;
        }

        ret = dc->process_frame(dc, dc->frame);
        av_frame_unref(dc->frame);
        if (ret < 0)
            return ret;

        if (dc->max_frames && dc->decoder->frame_num == dc->max_frames)
            return 1;
    }

    return (dc->max_frames == 0 || dc->decoder->frame_num < dc->max_frames) ? 0 : 1;
}

int ds_run(DecodeContext *dc)
{
    int ret;

    ret = avcodec_open2(dc->decoder, NULL, &dc->decoder_opts);
    if (ret < 0)
        return ret;

    while (ret >= 0) {
        ret = av_read_frame(dc->demuxer, dc->pkt);
        if (ret < 0)
            goto flush;
        if (dc->pkt->stream_index != dc->stream->index) {
            av_packet_unref(dc->pkt);
            continue;
        }

        ret = avcodec_send_packet(dc->decoder, dc->pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding: %d\n", ret);
            return ret;
        }
        av_packet_unref(dc->pkt);

        ret = decode_read(dc, 0);
        if (ret < 0) {
            fprintf(stderr, "Error decoding: %d\n", ret);
            return ret;
        } else if (ret > 0)
            return 0;
    }

flush:
    avcodec_send_packet(dc->decoder, NULL);
    ret = decode_read(dc, 1);
    if (ret < 0) {
        fprintf(stderr, "Error flushing: %d\n", ret);
        return ret;
    }

    return 0;
}

void ds_free(DecodeContext *dc)
{
    av_dict_free(&dc->decoder_opts);

    av_frame_free(&dc->frame);
    av_packet_free(&dc->pkt);

    avcodec_free_context(&dc->decoder);
    avformat_close_input(&dc->demuxer);
}

int ds_open(DecodeContext *dc, const char *url, int stream_idx)
{
    const AVCodec *codec;
    int ret;

    memset(dc, 0, sizeof(*dc));

    dc->pkt   = av_packet_alloc();
    dc->frame = av_frame_alloc();
    if (!dc->pkt || !dc->frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avformat_open_input(&dc->demuxer, url, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening input file: %d\n", ret);
        return ret;
    }

    if (stream_idx < 0 || stream_idx >= dc->demuxer->nb_streams)
        return AVERROR(EINVAL);

    dc->stream = dc->demuxer->streams[stream_idx];

    codec = avcodec_find_decoder(dc->stream->codecpar->codec_id);
    if (!codec)
        return AVERROR_DECODER_NOT_FOUND;

    dc->decoder = avcodec_alloc_context3(codec);
    if (!dc->decoder)
        return AVERROR(ENOMEM);

    return 0;

fail:
    ds_free(dc);
    return ret;
}
