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

#include <float.h>
#include <stdint.h>

#include "ffmpeg.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/display.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"

static const char *const opt_name_discard[]                   = {"discard", NULL};
static const char *const opt_name_reinit_filters[]            = {"reinit_filter", NULL};
static const char *const opt_name_fix_sub_duration[]          = {"fix_sub_duration", NULL};
static const char *const opt_name_canvas_sizes[]              = {"canvas_size", NULL};
static const char *const opt_name_guess_layout_max[]          = {"guess_layout_max", NULL};
static const char *const opt_name_ts_scale[]                  = {"itsscale", NULL};
static const char *const opt_name_hwaccels[]                  = {"hwaccel", NULL};
static const char *const opt_name_hwaccel_devices[]           = {"hwaccel_device", NULL};
static const char *const opt_name_hwaccel_output_formats[]    = {"hwaccel_output_format", NULL};
static const char *const opt_name_autorotate[]                = {"autorotate", NULL};
static const char *const opt_name_display_rotations[]         = {"display_rotation", NULL};
static const char *const opt_name_display_hflips[]            = {"display_hflip", NULL};
static const char *const opt_name_display_vflips[]            = {"display_vflip", NULL};

typedef struct Demuxer {
    InputFile f;

    /* number of times input stream should be looped */
    int loop;
    /* actual duration of the longest stream in a file at the moment when
     * looping happens */
    int64_t duration;
    /* time base of the duration */
    AVRational time_base;

    /* number of streams that the user was warned of */
    int nb_streams_warn;

    AVThreadMessageQueue *in_thread_queue;
    int                   thread_queue_size;
    pthread_t             thread;
    int                   non_blocking;
} Demuxer;

typedef struct DemuxMsg {
    AVPacket *pkt;
    int looping;

    // repeat_pict from the demuxer-internal parser
    int repeat_pict;
} DemuxMsg;

static Demuxer *demuxer_from_ifile(InputFile *f)
{
    return (Demuxer*)f;
}

static void report_new_stream(Demuxer *d, const AVPacket *pkt)
{
    AVStream *st = d->f.ctx->streams[pkt->stream_index];

    if (pkt->stream_index < d->nb_streams_warn)
        return;
    av_log(NULL, AV_LOG_WARNING,
           "New %s stream %d:%d at pos:%"PRId64" and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           d->f.index, pkt->stream_index,
           pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    d->nb_streams_warn = pkt->stream_index + 1;
}

static void ifile_duration_update(Demuxer *d, InputStream *ist,
                                  int64_t last_duration)
{
    /* the total duration of the stream, max_pts - min_pts is
     * the duration of the stream without the last frame */
    if (ist->max_pts > ist->min_pts &&
        ist->max_pts - (uint64_t)ist->min_pts < INT64_MAX - last_duration)
        last_duration += ist->max_pts - ist->min_pts;

    if (!d->duration ||
        av_compare_ts(d->duration, d->time_base,
                      last_duration, ist->st->time_base) < 0) {
        d->duration = last_duration;
        d->time_base = ist->st->time_base;
    }
}

static int seek_to_start(Demuxer *d)
{
    InputFile    *ifile = &d->f;
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
            ifile_duration_update(d, ist, dur.duration);
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

            ifile_duration_update(d, ist, duration);
        }
    }

    if (d->loop > 0)
        d->loop--;

    return ret;
}

static void ts_fixup(Demuxer *d, AVPacket *pkt, int *repeat_pict)
{
    InputFile *ifile = &d->f;
    InputStream *ist = input_streams[ifile->ist_index + pkt->stream_index];
    const int64_t start_time = ifile->start_time_effective;
    int64_t duration;

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "demuxer -> ist_index:%d type:%s "
               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s\n",
               ifile->ist_index + pkt->stream_index,
               av_get_media_type_string(ist->st->codecpar->codec_type),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ist->st->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ist->st->time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ist->st->time_base));
    }

    if (!ist->wrap_correction_done && start_time != AV_NOPTS_VALUE &&
        ist->st->pts_wrap_bits < 64) {
        int64_t stime, stime2;

        stime = av_rescale_q(start_time, AV_TIME_BASE_Q, ist->st->time_base);
        stime2= stime + (1ULL<<ist->st->pts_wrap_bits);
        ist->wrap_correction_done = 1;

        if(stime2 > stime && pkt->dts != AV_NOPTS_VALUE && pkt->dts > stime + (1LL<<(ist->st->pts_wrap_bits-1))) {
            pkt->dts -= 1ULL<<ist->st->pts_wrap_bits;
            ist->wrap_correction_done = 0;
        }
        if(stime2 > stime && pkt->pts != AV_NOPTS_VALUE && pkt->pts > stime + (1LL<<(ist->st->pts_wrap_bits-1))) {
            pkt->pts -= 1ULL<<ist->st->pts_wrap_bits;
            ist->wrap_correction_done = 0;
        }
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);

    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts *= ist->ts_scale;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts *= ist->ts_scale;

    duration = av_rescale_q(d->duration, d->time_base, ist->st->time_base);
    if (pkt->pts != AV_NOPTS_VALUE) {
        pkt->pts += duration;
        ist->max_pts = FFMAX(pkt->pts, ist->max_pts);
        ist->min_pts = FFMIN(pkt->pts, ist->min_pts);
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += duration;

    *repeat_pict = -1;
    if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        av_stream_get_parser(ist->st))
        *repeat_pict = av_stream_get_parser(ist->st)->repeat_pict;
}

static void thread_set_name(InputFile *f)
{
    char name[16];
    snprintf(name, sizeof(name), "dmx%d:%s", f->index, f->ctx->iformat->name);
    ff_thread_setname(name);
}

static void *input_thread(void *arg)
{
    Demuxer   *d = arg;
    InputFile *f = &d->f;
    AVPacket *pkt;
    unsigned flags = d->non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
    int ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    thread_set_name(f);

    while (1) {
        DemuxMsg msg = { NULL };

        ret = av_read_frame(f->ctx, pkt);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            continue;
        }
        if (ret < 0) {
            if (d->loop) {
                /* signal looping to the consumer thread */
                msg.looping = 1;
                ret = av_thread_message_queue_send(d->in_thread_queue, &msg, 0);
                if (ret >= 0)
                    ret = seek_to_start(d);
                if (ret >= 0)
                    continue;

                /* fallthrough to the error path */
            }

            if (ret == AVERROR_EOF)
                av_log(NULL, AV_LOG_VERBOSE, "EOF in input file %d\n", f->index);
            else
                av_log(NULL, AV_LOG_ERROR, "Error demuxing input file %d: %s\n",
                       f->index, av_err2str(ret));

            break;
        }

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_INFO, pkt, do_hex_dump,
                             f->ctx->streams[pkt->stream_index]);
        }

        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt->stream_index >= f->nb_streams) {
            report_new_stream(d, pkt);
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
            av_log(NULL, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "%s: corrupt input packet in stream %d\n",
                   f->ctx->url, pkt->stream_index);
            if (exit_on_error) {
                av_packet_unref(pkt);
                ret = AVERROR_INVALIDDATA;
                break;
            }
        }

        ts_fixup(d, pkt, &msg.repeat_pict);

        msg.pkt = av_packet_alloc();
        if (!msg.pkt) {
            av_packet_unref(pkt);
            ret = AVERROR(ENOMEM);
            break;
        }
        av_packet_move_ref(msg.pkt, pkt);
        ret = av_thread_message_queue_send(d->in_thread_queue, &msg, flags);
        if (flags && ret == AVERROR(EAGAIN)) {
            flags = 0;
            ret = av_thread_message_queue_send(d->in_thread_queue, &msg, flags);
            av_log(f->ctx, AV_LOG_WARNING,
                   "Thread message queue blocking; consider raising the "
                   "thread_queue_size option (current value: %d)\n",
                   d->thread_queue_size);
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

finish:
    av_assert0(ret < 0);
    av_thread_message_queue_set_err_recv(d->in_thread_queue, ret);

    av_packet_free(&pkt);

    av_log(NULL, AV_LOG_VERBOSE, "Terminating demuxer thread %d\n", f->index);

    return NULL;
}

static void thread_stop(Demuxer *d)
{
    InputFile *f = &d->f;
    DemuxMsg msg;

    if (!d->in_thread_queue)
        return;
    av_thread_message_queue_set_err_send(d->in_thread_queue, AVERROR_EOF);
    while (av_thread_message_queue_recv(d->in_thread_queue, &msg, 0) >= 0)
        av_packet_free(&msg.pkt);

    pthread_join(d->thread, NULL);
    av_thread_message_queue_free(&d->in_thread_queue);
    av_thread_message_queue_free(&f->audio_duration_queue);
}

static int thread_start(Demuxer *d)
{
    int ret;
    InputFile *f = &d->f;

    if (d->thread_queue_size <= 0)
        d->thread_queue_size = (nb_input_files > 1 ? 8 : 1);

    if (f->ctx->pb ? !f->ctx->pb->seekable :
        strcmp(f->ctx->iformat->name, "lavfi"))
        d->non_blocking = 1;
    ret = av_thread_message_queue_alloc(&d->in_thread_queue,
                                        d->thread_queue_size, sizeof(DemuxMsg));
    if (ret < 0)
        return ret;

    if (d->loop) {
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

    if ((ret = pthread_create(&d->thread, NULL, input_thread, d))) {
        av_log(NULL, AV_LOG_ERROR, "pthread_create failed: %s. Try to increase `ulimit -v` or decrease `ulimit -s`.\n", strerror(ret));
        ret = AVERROR(ret);
        goto fail;
    }

    return 0;
fail:
    av_thread_message_queue_free(&d->in_thread_queue);
    return ret;
}

int ifile_get_packet(InputFile *f, AVPacket **pkt)
{
    Demuxer *d = demuxer_from_ifile(f);
    InputStream *ist;
    DemuxMsg msg;
    int ret;

    if (!d->in_thread_queue) {
        ret = thread_start(d);
        if (ret < 0)
            return ret;
    }

    if (f->readrate || f->rate_emu) {
        int i;
        int64_t file_start = copy_ts * (
                              (f->start_time_effective != AV_NOPTS_VALUE ? f->start_time_effective * !start_at_zero : 0) +
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

    ret = av_thread_message_queue_recv(d->in_thread_queue, &msg,
                                       d->non_blocking ?
                                       AV_THREAD_MESSAGE_NONBLOCK : 0);
    if (ret < 0)
        return ret;
    if (msg.looping)
        return 1;

    ist = input_streams[f->ist_index + msg.pkt->stream_index];
    ist->last_pkt_repeat_pict = msg.repeat_pict;

    *pkt = msg.pkt;
    return 0;
}

void ifile_close(InputFile **pf)
{
    InputFile *f = *pf;
    Demuxer   *d = demuxer_from_ifile(f);

    if (!f)
        return;

    thread_stop(d);

    avformat_close_input(&f->ctx);

    av_freep(pf);
}

static const AVCodec *choose_decoder(OptionsContext *o, AVFormatContext *s, AVStream *st,
                                     enum HWAccelID hwaccel_id, enum AVHWDeviceType hwaccel_device_type)

{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st);
    if (codec_name) {
        const AVCodec *codec = find_codec_or_die(codec_name, st->codecpar->codec_type, 0);
        st->codecpar->codec_id = codec->id;
        if (recast_media && st->codecpar->codec_type != codec->type)
            st->codecpar->codec_type = codec->type;
        return codec;
    } else {
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            hwaccel_id == HWACCEL_GENERIC &&
            hwaccel_device_type != AV_HWDEVICE_TYPE_NONE) {
            const AVCodec *c;
            void *i = NULL;

            while ((c = av_codec_iterate(&i))) {
                const AVCodecHWConfig *config;

                if (c->id != st->codecpar->codec_id ||
                    !av_codec_is_decoder(c))
                    continue;

                for (int j = 0; config = avcodec_get_hw_config(c, j); j++) {
                    if (config->device_type == hwaccel_device_type) {
                        av_log(NULL, AV_LOG_VERBOSE, "Selecting decoder '%s' because of requested hwaccel method %s\n",
                               c->name, av_hwdevice_get_type_name(hwaccel_device_type));
                        return c;
                    }
                }
            }
        }

        return avcodec_find_decoder(st->codecpar->codec_id);
    }
}

static int guess_input_channel_layout(InputStream *ist)
{
    AVCodecContext *dec = ist->dec_ctx;

    if (dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        char layout_name[256];

        if (dec->ch_layout.nb_channels > ist->guess_layout_max)
            return 0;
        av_channel_layout_default(&dec->ch_layout, dec->ch_layout.nb_channels);
        if (dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            return 0;
        av_channel_layout_describe(&dec->ch_layout, layout_name, sizeof(layout_name));
        av_log(NULL, AV_LOG_WARNING, "Guessed Channel Layout for Input Stream "
               "#%d.%d : %s\n", ist->file_index, ist->st->index, layout_name);
    }
    return 1;
}

static void add_display_matrix_to_stream(OptionsContext *o,
                                         AVFormatContext *ctx, AVStream *st)
{
    double rotation = DBL_MAX;
    int hflip = -1, vflip = -1;
    int hflip_set = 0, vflip_set = 0, rotation_set = 0;
    int32_t *buf;

    MATCH_PER_STREAM_OPT(display_rotations, dbl, rotation, ctx, st);
    MATCH_PER_STREAM_OPT(display_hflips,    i,   hflip,    ctx, st);
    MATCH_PER_STREAM_OPT(display_vflips,    i,   vflip,    ctx, st);

    rotation_set = rotation != DBL_MAX;
    hflip_set    = hflip != -1;
    vflip_set    = vflip != -1;

    if (!rotation_set && !hflip_set && !vflip_set)
        return;

    buf = (int32_t *)av_stream_new_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, sizeof(int32_t) * 9);
    if (!buf) {
        av_log(NULL, AV_LOG_FATAL, "Failed to generate a display matrix!\n");
        exit_program(1);
    }

    av_display_rotation_set(buf,
                            rotation_set ? -(rotation) : -0.0f);

    av_display_matrix_flip(buf,
                           hflip_set ? hflip : 0,
                           vflip_set ? vflip : 0);
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
static void add_input_streams(OptionsContext *o, AVFormatContext *ic)
{
    int i, ret;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecParameters *par = st->codecpar;
        InputStream *ist;
        char *framerate = NULL, *hwaccel_device = NULL;
        const char *hwaccel = NULL;
        char *hwaccel_output_format = NULL;
        char *codec_tag = NULL;
        char *next;
        char *discard_str = NULL;
        const AVClass *cc = avcodec_get_class();
        const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL,
                                                  0, AV_OPT_SEARCH_FAKE_OBJ);

        ist = ALLOC_ARRAY_ELEM(input_streams, nb_input_streams);
        ist->st = st;
        ist->file_index = nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->nb_samples = 0;
        ist->first_dts = AV_NOPTS_VALUE;
        ist->min_pts = INT64_MAX;
        ist->max_pts = INT64_MIN;

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st);

        ist->autorotate = 1;
        MATCH_PER_STREAM_OPT(autorotate, i, ist->autorotate, ic, st);

        MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st);
        if (codec_tag) {
            uint32_t tag = strtol(codec_tag, &next, 0);
            if (*next)
                tag = AV_RL32(codec_tag);
            st->codecpar->codec_tag = tag;
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            add_display_matrix_to_stream(o, ic, st);

            MATCH_PER_STREAM_OPT(hwaccels, str, hwaccel, ic, st);
            MATCH_PER_STREAM_OPT(hwaccel_output_formats, str,
                                 hwaccel_output_format, ic, st);

            if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "cuvid")) {
                av_log(NULL, AV_LOG_WARNING,
                    "WARNING: defaulting hwaccel_output_format to cuda for compatibility "
                    "with old commandlines. This behaviour is DEPRECATED and will be removed "
                    "in the future. Please explicitly set \"-hwaccel_output_format cuda\".\n");
                ist->hwaccel_output_format = AV_PIX_FMT_CUDA;
            } else if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "qsv")) {
                av_log(NULL, AV_LOG_WARNING,
                    "WARNING: defaulting hwaccel_output_format to qsv for compatibility "
                    "with old commandlines. This behaviour is DEPRECATED and will be removed "
                    "in the future. Please explicitly set \"-hwaccel_output_format qsv\".\n");
                ist->hwaccel_output_format = AV_PIX_FMT_QSV;
            } else if (hwaccel_output_format) {
                ist->hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
                if (ist->hwaccel_output_format == AV_PIX_FMT_NONE) {
                    av_log(NULL, AV_LOG_FATAL, "Unrecognised hwaccel output "
                           "format: %s", hwaccel_output_format);
                }
            } else {
                ist->hwaccel_output_format = AV_PIX_FMT_NONE;
            }

            if (hwaccel) {
                // The NVDEC hwaccels use a CUDA device, so remap the name here.
                if (!strcmp(hwaccel, "nvdec") || !strcmp(hwaccel, "cuvid"))
                    hwaccel = "cuda";

                if (!strcmp(hwaccel, "none"))
                    ist->hwaccel_id = HWACCEL_NONE;
                else if (!strcmp(hwaccel, "auto"))
                    ist->hwaccel_id = HWACCEL_AUTO;
                else {
                    enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwaccel);
                    if (type != AV_HWDEVICE_TYPE_NONE) {
                        ist->hwaccel_id = HWACCEL_GENERIC;
                        ist->hwaccel_device_type = type;
                    }

                    if (!ist->hwaccel_id) {
                        av_log(NULL, AV_LOG_FATAL, "Unrecognized hwaccel: %s.\n",
                               hwaccel);
                        av_log(NULL, AV_LOG_FATAL, "Supported hwaccels: ");
                        type = AV_HWDEVICE_TYPE_NONE;
                        while ((type = av_hwdevice_iterate_types(type)) !=
                               AV_HWDEVICE_TYPE_NONE)
                            av_log(NULL, AV_LOG_FATAL, "%s ",
                                   av_hwdevice_get_type_name(type));
                        av_log(NULL, AV_LOG_FATAL, "\n");
                        exit_program(1);
                    }
                }
            }

            MATCH_PER_STREAM_OPT(hwaccel_devices, str, hwaccel_device, ic, st);
            if (hwaccel_device) {
                ist->hwaccel_device = av_strdup(hwaccel_device);
                if (!ist->hwaccel_device)
                    report_and_exit(AVERROR(ENOMEM));
            }

            ist->hwaccel_pix_fmt = AV_PIX_FMT_NONE;
        }

        ist->dec = choose_decoder(o, ic, st, ist->hwaccel_id, ist->hwaccel_device_type);
        ist->decoder_opts = filter_codec_opts(o->g->codec_opts, ist->st->codecpar->codec_id, ic, st, ist->dec);

        ist->reinit_filters = -1;
        MATCH_PER_STREAM_OPT(reinit_filters, i, ist->reinit_filters, ic, st);

        MATCH_PER_STREAM_OPT(discard, str, discard_str, ic, st);
        ist->user_set_discard = AVDISCARD_NONE;

        if ((o->video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ||
            (o->audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ||
            (o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ||
            (o->data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA))
                ist->user_set_discard = AVDISCARD_ALL;

        if (discard_str && av_opt_eval_int(&cc, discard_opt, discard_str, &ist->user_set_discard) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing discard %s.\n",
                    discard_str);
            exit_program(1);
        }

        ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;
        ist->prev_pkt_pts = AV_NOPTS_VALUE;

        ist->dec_ctx = avcodec_alloc_context3(ist->dec);
        if (!ist->dec_ctx)
            report_and_exit(AVERROR(ENOMEM));

        ret = avcodec_parameters_to_context(ist->dec_ctx, par);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
            exit_program(1);
        }

        ist->decoded_frame = av_frame_alloc();
        if (!ist->decoded_frame)
            report_and_exit(AVERROR(ENOMEM));

        ist->pkt = av_packet_alloc();
        if (!ist->pkt)
            report_and_exit(AVERROR(ENOMEM));

        if (o->bitexact)
            ist->dec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            // avformat_find_stream_info() doesn't set this for us anymore.
            ist->dec_ctx->framerate = st->avg_frame_rate;

            MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st);
            if (framerate && av_parse_video_rate(&ist->framerate,
                                                 framerate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error parsing framerate %s.\n",
                       framerate);
                exit_program(1);
            }

            ist->top_field_first = -1;
            MATCH_PER_STREAM_OPT(top_field_first, i, ist->top_field_first, ic, st);

            ist->framerate_guessed = av_guess_frame_rate(ic, st, NULL);

            break;
        case AVMEDIA_TYPE_AUDIO:
            ist->guess_layout_max = INT_MAX;
            MATCH_PER_STREAM_OPT(guess_layout_max, i, ist->guess_layout_max, ic, st);
            guess_input_channel_layout(ist);
            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_SUBTITLE: {
            char *canvas_size = NULL;
            MATCH_PER_STREAM_OPT(fix_sub_duration, i, ist->fix_sub_duration, ic, st);
            MATCH_PER_STREAM_OPT(canvas_sizes, str, canvas_size, ic, st);
            if (canvas_size &&
                av_parse_video_size(&ist->dec_ctx->width, &ist->dec_ctx->height, canvas_size) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Invalid canvas size: %s.\n", canvas_size);
                exit_program(1);
            }
            break;
        }
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        default:
            abort();
        }

        ist->par = avcodec_parameters_alloc();
        if (!ist->par)
            report_and_exit(AVERROR(ENOMEM));

        ret = avcodec_parameters_from_context(ist->par, ist->dec_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
            exit_program(1);
        }
    }
}

static void dump_attachment(AVStream *st, const char *filename)
{
    int ret;
    AVIOContext *out = NULL;
    const AVDictionaryEntry *e;

    if (!st->codecpar->extradata_size) {
        av_log(NULL, AV_LOG_WARNING, "No extradata to dump in stream #%d:%d.\n",
               nb_input_files - 1, st->index);
        return;
    }
    if (!*filename && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
        filename = e->value;
    if (!*filename) {
        av_log(NULL, AV_LOG_FATAL, "No filename specified and no 'filename' tag"
               "in stream #%d:%d.\n", nb_input_files - 1, st->index);
        exit_program(1);
    }

    assert_file_overwrite(filename);

    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, &int_cb, NULL)) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open file %s for writing.\n",
               filename);
        exit_program(1);
    }

    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
    avio_flush(out);
    avio_close(out);
}

int ifile_open(OptionsContext *o, const char *filename)
{
    Demuxer   *d;
    InputFile *f;
    AVFormatContext *ic;
    const AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    AVDictionary *unused_opts = NULL;
    const AVDictionaryEntry *e = NULL;
    char *   video_codec_name = NULL;
    char *   audio_codec_name = NULL;
    char *subtitle_codec_name = NULL;
    char *    data_codec_name = NULL;
    int scan_all_pmts_set = 0;

    int64_t start_time     = o->start_time;
    int64_t start_time_eof = o->start_time_eof;
    int64_t stop_time      = o->stop_time;
    int64_t recording_time = o->recording_time;

    if (stop_time != INT64_MAX && recording_time != INT64_MAX) {
        stop_time = INT64_MAX;
        av_log(NULL, AV_LOG_WARNING, "-t and -to cannot be used together; using -t.\n");
    }

    if (stop_time != INT64_MAX && recording_time == INT64_MAX) {
        int64_t start = start_time == AV_NOPTS_VALUE ? 0 : start_time;
        if (stop_time <= start) {
            av_log(NULL, AV_LOG_ERROR, "-to value smaller than -ss; aborting.\n");
            exit_program(1);
        } else {
            recording_time = stop_time - start;
        }
    }

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(NULL, AV_LOG_FATAL, "Unknown input format: '%s'\n", o->format);
            exit_program(1);
        }
    }

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    stdin_interaction &= strncmp(filename, "pipe:", 5) &&
                         strcmp(filename, "/dev/stdin");

    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic)
        report_and_exit(AVERROR(ENOMEM));
    if (o->nb_audio_sample_rate) {
        av_dict_set_int(&o->g->format_opts, "sample_rate", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i, 0);
    }
    if (o->nb_audio_channels) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%dC", o->audio_channels[o->nb_audio_channels - 1].u.i);
            av_dict_set(&o->g->format_opts, "ch_layout", buf, 0);
        }
    }
    if (o->nb_audio_ch_layouts) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "ch_layout", o->audio_ch_layouts[o->nb_audio_ch_layouts - 1].u.str, 0);
        }
    }
    if (o->nb_frame_rates) {
        const AVClass *priv_class;
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    MATCH_PER_TYPE_OPT(codec_names, str,    video_codec_name, ic, "v");
    MATCH_PER_TYPE_OPT(codec_names, str,    audio_codec_name, ic, "a");
    MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, ic, "s");
    MATCH_PER_TYPE_OPT(codec_names, str,     data_codec_name, ic, "d");

    if (video_codec_name)
        ic->video_codec    = find_codec_or_die(video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0);
    if (audio_codec_name)
        ic->audio_codec    = find_codec_or_die(audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0);
    if (subtitle_codec_name)
        ic->subtitle_codec = find_codec_or_die(subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0);
    if (data_codec_name)
        ic->data_codec     = find_codec_or_die(data_codec_name    , AVMEDIA_TYPE_DATA    , 0);

    ic->video_codec_id     = video_codec_name    ? ic->video_codec->id    : AV_CODEC_ID_NONE;
    ic->audio_codec_id     = audio_codec_name    ? ic->audio_codec->id    : AV_CODEC_ID_NONE;
    ic->subtitle_codec_id  = subtitle_codec_name ? ic->subtitle_codec->id : AV_CODEC_ID_NONE;
    ic->data_codec_id      = data_codec_name     ? ic->data_codec->id     : AV_CODEC_ID_NONE;

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    if (o->bitexact)
        ic->flags |= AVFMT_FLAG_BITEXACT;
    ic->interrupt_callback = int_cb;

    if (!av_dict_get(o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&o->g->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    /* open the input file with generic avformat function */
    err = avformat_open_input(&ic, filename, file_iformat, &o->g->format_opts);
    if (err < 0) {
        print_error(filename, err);
        if (err == AVERROR_PROTOCOL_NOT_FOUND)
            av_log(NULL, AV_LOG_ERROR, "Did you mean file:%s?\n", filename);
        exit_program(1);
    }
    if (scan_all_pmts_set)
        av_dict_set(&o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    remove_avoptions(&o->g->format_opts, o->g->codec_opts);
    assert_avoptions(o->g->format_opts);

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        choose_decoder(o, ic, ic->streams[i], HWACCEL_NONE, AV_HWDEVICE_TYPE_NONE);

    if (o->find_stream_info) {
        AVDictionary **opts = setup_find_stream_info_opts(ic, o->g->codec_opts);
        int orig_nb_streams = ic->nb_streams;

        /* If not enough info to get the stream parameters, we decode the
           first frames to get it. (used in mpeg case for example) */
        ret = avformat_find_stream_info(ic, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "%s: could not find codec parameters\n", filename);
            if (ic->nb_streams == 0) {
                avformat_close_input(&ic);
                exit_program(1);
            }
        }
    }

    if (start_time != AV_NOPTS_VALUE && start_time_eof != AV_NOPTS_VALUE) {
        av_log(NULL, AV_LOG_WARNING, "Cannot use -ss and -sseof both, using -ss for %s\n", filename);
        start_time_eof = AV_NOPTS_VALUE;
    }

    if (start_time_eof != AV_NOPTS_VALUE) {
        if (start_time_eof >= 0) {
            av_log(NULL, AV_LOG_ERROR, "-sseof value must be negative; aborting\n");
            exit_program(1);
        }
        if (ic->duration > 0) {
            start_time = start_time_eof + ic->duration;
            if (start_time < 0) {
                av_log(NULL, AV_LOG_WARNING, "-sseof value seeks to before start of file %s; ignored\n", filename);
                start_time = AV_NOPTS_VALUE;
            }
        } else
            av_log(NULL, AV_LOG_WARNING, "Cannot use -sseof, duration of %s not known\n", filename);
    }
    timestamp = (start_time == AV_NOPTS_VALUE) ? 0 : start_time;
    /* add the stream start time */
    if (!o->seek_timestamp && ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t seek_timestamp = timestamp;

        if (!(ic->iformat->flags & AVFMT_SEEK_TO_PTS)) {
            int dts_heuristic = 0;
            for (i=0; i<ic->nb_streams; i++) {
                const AVCodecParameters *par = ic->streams[i]->codecpar;
                if (par->video_delay) {
                    dts_heuristic = 1;
                    break;
                }
            }
            if (dts_heuristic) {
                seek_timestamp -= 3*AV_TIME_BASE / 23;
            }
        }
        ret = avformat_seek_file(ic, -1, INT64_MIN, seek_timestamp, seek_timestamp, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    /* update the current parameters so that they match the one of the input stream */
    add_input_streams(o, ic);

    /* dump the file content */
    av_dump_format(ic, nb_input_files, filename, 0);

    d = allocate_array_elem(&input_files, sizeof(*d), &nb_input_files);
    f = &d->f;

    f->ctx        = ic;
    f->index      = nb_input_files - 1;
    f->ist_index  = nb_input_streams - ic->nb_streams;
    f->start_time = start_time;
    f->recording_time = recording_time;
    f->input_sync_ref = o->input_sync_ref;
    f->input_ts_offset = o->input_ts_offset;
    f->ts_offset  = o->input_ts_offset - (copy_ts ? (start_at_zero && ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0) : timestamp);
    f->nb_streams = ic->nb_streams;
    f->rate_emu   = o->rate_emu;
    f->accurate_seek = o->accurate_seek;
    d->loop = o->loop;
    d->duration = 0;
    d->time_base = (AVRational){ 1, 1 };

    f->readrate = o->readrate ? o->readrate : 0.0;
    if (f->readrate < 0.0f) {
        av_log(NULL, AV_LOG_ERROR, "Option -readrate for Input #%d is %0.3f; it must be non-negative.\n", f->index, f->readrate);
        exit_program(1);
    }
    if (f->readrate && f->rate_emu) {
        av_log(NULL, AV_LOG_WARNING, "Both -readrate and -re set for Input #%d. Using -readrate %0.3f.\n", f->index, f->readrate);
        f->rate_emu = 0;
    }

    d->thread_queue_size = o->thread_queue_size;

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = f->ist_index; i < nb_input_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(input_streams[i]->decoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                   "input file #%d (%s) is not a decoding option.\n", e->key,
                   option->help ? option->help : "", f->index,
                   filename);
            exit_program(1);
        }

        av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
               "input file #%d (%s) has not been used for any stream. The most "
               "likely reason is either wrong type (e.g. a video option with "
               "no video streams) or that it is a private option of some decoder "
               "which was not actually used for any stream.\n", e->key,
               option->help ? option->help : "", f->index, filename);
    }
    av_dict_free(&unused_opts);

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < ic->nb_streams; j++) {
            AVStream *st = ic->streams[j];

            if (check_stream_specifier(ic, st, o->dump_attachment[i].specifier) == 1)
                dump_attachment(st, o->dump_attachment[i].u.str);
        }
    }

    input_stream_potentially_available = 1;

    return 0;
}
