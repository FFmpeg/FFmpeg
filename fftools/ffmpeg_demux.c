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

#include "ffmpeg.h"

#include "libavutil/error.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"

static void report_new_stream(InputFile *file, const AVPacket *pkt)
{
    AVStream *st = file->ctx->streams[pkt->stream_index];

    if (pkt->stream_index < file->nb_streams_warn)
        return;
    av_log(file->ctx, AV_LOG_WARNING,
           "New %s stream %d:%d at pos:%"PRId64" and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           file->index, pkt->stream_index,
           pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    file->nb_streams_warn = pkt->stream_index + 1;
}

static void *input_thread(void *arg)
{
    InputFile *f = arg;
    AVPacket *pkt = f->pkt, *queue_pkt;
    unsigned flags = f->non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
    int ret = 0;

    while (1) {
        ret = av_read_frame(f->ctx, pkt);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            continue;
        }
        if (ret < 0) {
            av_thread_message_queue_set_err_recv(f->in_thread_queue, ret);
            break;
        }

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_INFO, pkt, do_hex_dump,
                             f->ctx->streams[pkt->stream_index]);
        }

        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt->stream_index >= f->nb_streams) {
            report_new_stream(f, pkt);
            av_packet_unref(pkt);
            continue;
        }

        queue_pkt = av_packet_alloc();
        if (!queue_pkt) {
            av_packet_unref(pkt);
            av_thread_message_queue_set_err_recv(f->in_thread_queue, AVERROR(ENOMEM));
            break;
        }
        av_packet_move_ref(queue_pkt, pkt);
        ret = av_thread_message_queue_send(f->in_thread_queue, &queue_pkt, flags);
        if (flags && ret == AVERROR(EAGAIN)) {
            flags = 0;
            ret = av_thread_message_queue_send(f->in_thread_queue, &queue_pkt, flags);
            av_log(f->ctx, AV_LOG_WARNING,
                   "Thread message queue blocking; consider raising the "
                   "thread_queue_size option (current value: %d)\n",
                   f->thread_queue_size);
        }
        if (ret < 0) {
            if (ret != AVERROR_EOF)
                av_log(f->ctx, AV_LOG_ERROR,
                       "Unable to send packet to main thread: %s\n",
                       av_err2str(ret));
            av_packet_free(&queue_pkt);
            av_thread_message_queue_set_err_recv(f->in_thread_queue, ret);
            break;
        }
    }

    return NULL;
}

void free_input_thread(int i)
{
    InputFile *f = input_files[i];
    AVPacket *pkt;

    if (!f || !f->in_thread_queue)
        return;
    av_thread_message_queue_set_err_send(f->in_thread_queue, AVERROR_EOF);
    while (av_thread_message_queue_recv(f->in_thread_queue, &pkt, 0) >= 0)
        av_packet_free(&pkt);

    pthread_join(f->thread, NULL);
    av_thread_message_queue_free(&f->in_thread_queue);
}

void free_input_threads(void)
{
    int i;

    for (i = 0; i < nb_input_files; i++)
        free_input_thread(i);
}

int init_input_thread(int i)
{
    int ret;
    InputFile *f = input_files[i];

    if (f->thread_queue_size <= 0)
        f->thread_queue_size = (nb_input_files > 1 ? 8 : 1);

    if (f->ctx->pb ? !f->ctx->pb->seekable :
        strcmp(f->ctx->iformat->name, "lavfi"))
        f->non_blocking = 1;
    ret = av_thread_message_queue_alloc(&f->in_thread_queue,
                                        f->thread_queue_size, sizeof(f->pkt));
    if (ret < 0)
        return ret;

    if ((ret = pthread_create(&f->thread, NULL, input_thread, f))) {
        av_log(NULL, AV_LOG_ERROR, "pthread_create failed: %s. Try to increase `ulimit -v` or decrease `ulimit -s`.\n", strerror(ret));
        av_thread_message_queue_free(&f->in_thread_queue);
        return AVERROR(ret);
    }

    return 0;
}

int init_input_threads(void)
{
    int i, ret;

    for (i = 0; i < nb_input_files; i++) {
        ret = init_input_thread(i);
        if (ret < 0)
            return ret;
    }
    return 0;
}
