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

#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"

typedef struct DemuxMsg {
    AVPacket *pkt;
    int looping;
} DemuxMsg;

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

static void ifile_duration_update(InputFile *f, InputStream *ist,
                                  int64_t last_duration)
{
    /* the total duration of the stream, max_pts - min_pts is
     * the duration of the stream without the last frame */
    if (ist->max_pts > ist->min_pts &&
        ist->max_pts - (uint64_t)ist->min_pts < INT64_MAX - last_duration)
        last_duration += ist->max_pts - ist->min_pts;

    if (!f->duration ||
        av_compare_ts(f->duration, f->time_base,
                      last_duration, ist->st->time_base) < 0) {
        f->duration = last_duration;
        f->time_base = ist->st->time_base;
    }
}

static int seek_to_start(InputFile *ifile)
{
    AVFormatContext *is = ifile->ctx;
    InputStream *ist;
    int ret;

    ret = avformat_seek_file(is, -1, INT64_MIN, is->start_time, is->start_time, 0);
    if (ret < 0)
        return ret;

    if (ifile->audio_duration_queue_size) {
        /* duration is the length of the last frame in a stream
         * when audio stream is present we don't care about
         * last video frame length because it's not defined exactly */
        int got_durations = 0;

        while (got_durations < ifile->audio_duration_queue_size) {
            LastFrameDuration dur;
            ret = av_thread_message_queue_recv(ifile->audio_duration_queue, &dur, 0);
            if (ret < 0)
                return ret;
            got_durations++;

            ist = input_streams[ifile->ist_index + dur.stream_idx];
            ifile_duration_update(ifile, ist, dur.duration);
        }
    } else {
        for (int i = 0; i < ifile->nb_streams; i++) {
            int64_t duration = 0;
            ist   = input_streams[ifile->ist_index + i];

            if (ist->framerate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->framerate), ist->st->time_base);
            } else if (ist->st->avg_frame_rate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate), ist->st->time_base);
            } else {
                duration = 1;
            }

            ifile_duration_update(ifile, ist, duration);
        }
    }

    if (ifile->loop > 0)
        ifile->loop--;

    return ret;
}

static void *input_thread(void *arg)
{
    InputFile *f = arg;
    AVPacket *pkt = f->pkt;
    unsigned flags = f->non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
    int ret = 0;

    while (1) {
        DemuxMsg msg = { NULL };

        ret = av_read_frame(f->ctx, pkt);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            continue;
        }
        if (ret < 0) {
            if (f->loop) {
                /* signal looping to the consumer thread */
                msg.looping = 1;
                ret = av_thread_message_queue_send(f->in_thread_queue, &msg, 0);
                if (ret >= 0)
                    ret = seek_to_start(f);
                if (ret >= 0)
                    continue;

                /* fallthrough to the error path */
            }

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

        msg.pkt = av_packet_alloc();
        if (!msg.pkt) {
            av_packet_unref(pkt);
            ret = AVERROR(ENOMEM);
            break;
        }
        av_packet_move_ref(msg.pkt, pkt);
        ret = av_thread_message_queue_send(f->in_thread_queue, &msg, flags);
        if (flags && ret == AVERROR(EAGAIN)) {
            flags = 0;
            ret = av_thread_message_queue_send(f->in_thread_queue, &msg, flags);
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
            av_packet_free(&msg.pkt);
            break;
        }
    }

    av_assert0(ret < 0);
    av_thread_message_queue_set_err_recv(f->in_thread_queue, ret);

    return NULL;
}

static void free_input_thread(int i)
{
    InputFile *f = input_files[i];
    DemuxMsg msg;

    if (!f || !f->in_thread_queue)
        return;
    av_thread_message_queue_set_err_send(f->in_thread_queue, AVERROR_EOF);
    while (av_thread_message_queue_recv(f->in_thread_queue, &msg, 0) >= 0)
        av_packet_free(&msg.pkt);

    pthread_join(f->thread, NULL);
    av_thread_message_queue_free(&f->in_thread_queue);
    av_thread_message_queue_free(&f->audio_duration_queue);
}

void free_input_threads(void)
{
    int i;

    for (i = 0; i < nb_input_files; i++)
        free_input_thread(i);
}

static int init_input_thread(int i)
{
    int ret;
    InputFile *f = input_files[i];

    if (f->thread_queue_size <= 0)
        f->thread_queue_size = (nb_input_files > 1 ? 8 : 1);

    if (f->ctx->pb ? !f->ctx->pb->seekable :
        strcmp(f->ctx->iformat->name, "lavfi"))
        f->non_blocking = 1;
    ret = av_thread_message_queue_alloc(&f->in_thread_queue,
                                        f->thread_queue_size, sizeof(DemuxMsg));
    if (ret < 0)
        return ret;

    if (f->loop) {
        int nb_audio_dec = 0;

        for (int i = 0; i < f->nb_streams; i++) {
            InputStream *ist = input_streams[f->ist_index + i];
            nb_audio_dec += !!(ist->decoding_needed &&
                               ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
        }

        if (nb_audio_dec) {
            ret = av_thread_message_queue_alloc(&f->audio_duration_queue,
                                                nb_audio_dec, sizeof(LastFrameDuration));
            if (ret < 0)
                goto fail;
            f->audio_duration_queue_size = nb_audio_dec;
        }
    }

    if ((ret = pthread_create(&f->thread, NULL, input_thread, f))) {
        av_log(NULL, AV_LOG_ERROR, "pthread_create failed: %s. Try to increase `ulimit -v` or decrease `ulimit -s`.\n", strerror(ret));
        ret = AVERROR(ret);
        goto fail;
    }

    return 0;
fail:
    av_thread_message_queue_free(&f->in_thread_queue);
    return ret;
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

int ifile_get_packet(InputFile *f, AVPacket **pkt)
{
    DemuxMsg msg;
    int ret;

    if (f->readrate || f->rate_emu) {
        int i;
        int64_t file_start = copy_ts * (
                              (f->ctx->start_time != AV_NOPTS_VALUE ? f->ctx->start_time * !start_at_zero : 0) +
                              (f->start_time != AV_NOPTS_VALUE ? f->start_time : 0)
                             );
        float scale = f->rate_emu ? 1.0 : f->readrate;
        for (i = 0; i < f->nb_streams; i++) {
            InputStream *ist = input_streams[f->ist_index + i];
            int64_t stream_ts_offset, pts, now;
            if (!ist->nb_packets || (ist->decoding_needed && !ist->got_output)) continue;
            stream_ts_offset = FFMAX(ist->first_dts != AV_NOPTS_VALUE ? ist->first_dts : 0, file_start);
            pts = av_rescale(ist->dts, 1000000, AV_TIME_BASE);
            now = (av_gettime_relative() - ist->start) * scale + stream_ts_offset;
            if (pts > now)
                return AVERROR(EAGAIN);
        }
    }

    ret = av_thread_message_queue_recv(f->in_thread_queue, &msg,
                                       f->non_blocking ?
                                       AV_THREAD_MESSAGE_NONBLOCK : 0);
    if (ret < 0)
        return ret;
    if (msg.looping)
        return 1;

    *pkt = msg.pkt;
    return 0;
}
