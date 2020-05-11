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
#include <stdint.h>
#include <stdlib.h>

#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/video_enc_params.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

static int decode_read(AVCodecContext *decoder, AVFrame *frame, int flush, int max_frames)
{
    const int ret_done = flush ? AVERROR_EOF : AVERROR(EAGAIN);
    int ret = 0;

    while (ret >= 0 &&
           (max_frames == 0 ||  decoder->frame_number < max_frames)) {
        AVFrameSideData *sd;

        ret = avcodec_receive_frame(decoder, frame);
        if (ret < 0)
            return (ret == ret_done) ? 0 : ret;

        fprintf(stdout, "frame %d\n", decoder->frame_number - 1);

        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS);
        if (sd) {
            AVVideoEncParams *par = (AVVideoEncParams*)sd->data;

            fprintf(stdout, "AVVideoEncParams %d\n", par->type);
            fprintf(stdout, "qp %d\n", par->qp);
            for (int i = 0; i < FF_ARRAY_ELEMS(par->delta_qp); i++)
                for (int j = 0; j < FF_ARRAY_ELEMS(par->delta_qp[i]); j++) {
                    if (par->delta_qp[i][j])
                        fprintf(stdout, "delta_qp[%d][%d] %"PRId32"\n", i, j, par->delta_qp[i][j]);
                }

            if (par->nb_blocks) {
                fprintf(stdout, "nb_blocks %d\n", par->nb_blocks);
                for (int i = 0; i < par->nb_blocks; i++) {
                    AVVideoBlockParams *b = av_video_enc_params_block(par, i);

                    fprintf(stdout, "block %d %d:%d %dx%d %"PRId32"\n",
                            i, b->src_x, b->src_y, b->w, b->h, b->delta_qp);
                }
            }
        }

        av_frame_unref(frame);

        if (max_frames && decoder->frame_number == max_frames)
            return 1;
    }

    return (max_frames == 0 || decoder->frame_number < max_frames) ? 0 : 1;
}

static int decoder_init(AVFormatContext *demuxer, int stream_idx,
                        AVCodecContext **dec, AVDictionary **opts)
{
    const AVCodec *codec;
    int ret;

    if (stream_idx < 0 || stream_idx >= demuxer->nb_streams)
        return AVERROR(EINVAL);

    codec = avcodec_find_decoder(demuxer->streams[stream_idx]->codecpar->codec_id);
    if (!codec)
        return AVERROR_DECODER_NOT_FOUND;

    *dec = avcodec_alloc_context3(codec);
    if (!*dec)
        return AVERROR(ENOMEM);

    ret = avcodec_open2(*dec, NULL, opts);
    if (ret < 0)
        return ret;

    return 0;
}

int main(int argc, char **argv)
{
    AVFormatContext *demuxer = NULL;
    AVCodecContext  *decoder = NULL;
    AVDictionary       *opts = NULL;

    AVPacket *pkt   = NULL;
    AVFrame  *frame = NULL;

    unsigned int stream_idx, max_frames;
    const char *filename, *thread_type = NULL, *nb_threads = NULL;
    int ret = 0;

    if (argc <= 3) {
        fprintf(stderr, "Usage: %s <input file> <stream index> <max frame count> [<thread count> <thread type>]\n", argv[0]);
        return 0;
    }

    filename    = argv[1];
    stream_idx  = strtol(argv[2], NULL, 0);
    max_frames  = strtol(argv[3], NULL, 0);
    if (argc > 5) {
        nb_threads  = argv[4];
        thread_type = argv[5];
    }

    ret  = av_dict_set(&opts, "threads",          nb_threads,    0);
    ret |= av_dict_set(&opts, "thread_type",      thread_type,   0);
    ret |= av_dict_set(&opts, "export_side_data", "venc_params", 0);

    ret = avformat_open_input(&demuxer, filename, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening input file: %d\n", ret);
        return ret;
    }

    ret = decoder_init(demuxer, stream_idx, &decoder, &opts);
    if (ret < 0) {
        fprintf(stderr, "Error initializing decoder\n");
        goto finish;
    }

    pkt   = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    while (ret >= 0) {
        ret = av_read_frame(demuxer, pkt);
        if (ret < 0)
            goto flush;
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(decoder, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding: %d\n", ret);
            goto finish;
        }
        av_packet_unref(pkt);

        ret = decode_read(decoder, frame, 0, max_frames);
        if (ret < 0) {
            fprintf(stderr, "Error decoding: %d\n", ret);
            goto finish;
        } else if (ret > 0) {
            ret = 0;
            goto finish;
        }
    }

flush:
    avcodec_send_packet(decoder, NULL);
    ret = decode_read(decoder, frame, 1, max_frames);
    if (ret < 0) {
        fprintf(stderr, "Error flushing: %d\n", ret);
        goto finish;
    }
    ret = 0;

finish:
    av_dict_free(&opts);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&demuxer);

    return ret;
}
