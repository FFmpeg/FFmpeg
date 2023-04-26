/*
 * Copyright (c) 2000-2003 Fabrice Bellard
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

/**
 * @file
 * multimedia converter based on the FFmpeg libraries
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#elif HAVE_GETPROCESSTIMES
#include <windows.h>
#endif
#if HAVE_GETPROCESSMEMORYINFO
#include <windows.h>
#include <psapi.h>
#endif
#if HAVE_SETCONSOLECTRLHANDLER
#include <windows.h>
#endif

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if HAVE_TERMIOS_H
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#elif HAVE_KBHIT
#include <conio.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/libm.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/version.h"

#include "libavformat/avformat.h"

#include "libavdevice/avdevice.h"

#include "libswresample/swresample.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#include "cmdutils.h"
#include "ffmpeg.h"
#include "sync_queue.h"

const char program_name[] = "ffmpeg";
const int program_birth_year = 2000;

FILE *vstats_file;

typedef struct BenchmarkTimeStamps {
    int64_t real_usec;
    int64_t user_usec;
    int64_t sys_usec;
} BenchmarkTimeStamps;

static BenchmarkTimeStamps get_benchmark_time_stamps(void);
static int64_t getmaxrss(void);

int64_t nb_frames_dup = 0;
int64_t nb_frames_drop = 0;
static int64_t decode_error_stat[2];
unsigned nb_output_dumped = 0;

static BenchmarkTimeStamps current_time;
AVIOContext *progress_avio = NULL;

InputFile   **input_files   = NULL;
int        nb_input_files   = 0;

OutputFile   **output_files   = NULL;
int         nb_output_files   = 0;

FilterGraph **filtergraphs;
int        nb_filtergraphs;

#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int restore_tty;
#endif

/* sub2video hack:
   Convert subtitles to video with alpha to insert them in filter graphs.
   This is a temporary solution until libavfilter gets real subtitles support.
 */

static int sub2video_get_blank_frame(InputStream *ist)
{
    int ret;
    AVFrame *frame = ist->sub2video.frame;

    av_frame_unref(frame);
    ist->sub2video.frame->width  = ist->dec_ctx->width  ? ist->dec_ctx->width  : ist->sub2video.w;
    ist->sub2video.frame->height = ist->dec_ctx->height ? ist->dec_ctx->height : ist->sub2video.h;
    ist->sub2video.frame->format = AV_PIX_FMT_RGB32;
    if ((ret = av_frame_get_buffer(frame, 0)) < 0)
        return ret;
    memset(frame->data[0], 0, frame->height * frame->linesize[0]);
    return 0;
}

static void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h,
                                AVSubtitleRect *r)
{
    uint32_t *pal, *dst2;
    uint8_t *src, *src2;
    int x, y;

    if (r->type != SUBTITLE_BITMAP) {
        av_log(NULL, AV_LOG_WARNING, "sub2video: non-bitmap subtitle\n");
        return;
    }
    if (r->x < 0 || r->x + r->w > w || r->y < 0 || r->y + r->h > h) {
        av_log(NULL, AV_LOG_WARNING, "sub2video: rectangle (%d %d %d %d) overflowing %d %d\n",
            r->x, r->y, r->w, r->h, w, h
        );
        return;
    }

    dst += r->y * dst_linesize + r->x * 4;
    src = r->data[0];
    pal = (uint32_t *)r->data[1];
    for (y = 0; y < r->h; y++) {
        dst2 = (uint32_t *)dst;
        src2 = src;
        for (x = 0; x < r->w; x++)
            *(dst2++) = pal[*(src2++)];
        dst += dst_linesize;
        src += r->linesize[0];
    }
}

static void sub2video_push_ref(InputStream *ist, int64_t pts)
{
    AVFrame *frame = ist->sub2video.frame;
    int i;
    int ret;

    av_assert1(frame->data[0]);
    ist->sub2video.last_pts = frame->pts = pts;
    for (i = 0; i < ist->nb_filters; i++) {
        ret = av_buffersrc_add_frame_flags(ist->filters[i]->filter, frame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF |
                                           AV_BUFFERSRC_FLAG_PUSH);
        if (ret != AVERROR_EOF && ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Error while add the frame to buffer source(%s).\n",
                   av_err2str(ret));
    }
}

void sub2video_update(InputStream *ist, int64_t heartbeat_pts, AVSubtitle *sub)
{
    AVFrame *frame = ist->sub2video.frame;
    int8_t *dst;
    int     dst_linesize;
    int num_rects, i;
    int64_t pts, end_pts;

    if (!frame)
        return;
    if (sub) {
        pts       = av_rescale_q(sub->pts + sub->start_display_time * 1000LL,
                                 AV_TIME_BASE_Q, ist->st->time_base);
        end_pts   = av_rescale_q(sub->pts + sub->end_display_time   * 1000LL,
                                 AV_TIME_BASE_Q, ist->st->time_base);
        num_rects = sub->num_rects;
    } else {
        /* If we are initializing the system, utilize current heartbeat
           PTS as the start time, and show until the following subpicture
           is received. Otherwise, utilize the previous subpicture's end time
           as the fall-back value. */
        pts       = ist->sub2video.initialize ?
                    heartbeat_pts : ist->sub2video.end_pts;
        end_pts   = INT64_MAX;
        num_rects = 0;
    }
    if (sub2video_get_blank_frame(ist) < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to get a blank canvas.\n");
        return;
    }
    dst          = frame->data    [0];
    dst_linesize = frame->linesize[0];
    for (i = 0; i < num_rects; i++)
        sub2video_copy_rect(dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(ist, pts);
    ist->sub2video.end_pts = end_pts;
    ist->sub2video.initialize = 0;
}

static void sub2video_heartbeat(InputStream *ist, int64_t pts)
{
    InputFile *infile = input_files[ist->file_index];
    int i, j, nb_reqs;
    int64_t pts2;

    /* When a frame is read from a file, examine all sub2video streams in
       the same file and send the sub2video frame again. Otherwise, decoded
       video frames could be accumulating in the filter graph while a filter
       (possibly overlay) is desperately waiting for a subtitle frame. */
    for (i = 0; i < infile->nb_streams; i++) {
        InputStream *ist2 = infile->streams[i];
        if (!ist2->sub2video.frame)
            continue;
        /* subtitles seem to be usually muxed ahead of other streams;
           if not, subtracting a larger time here is necessary */
        pts2 = av_rescale_q(pts, ist->st->time_base, ist2->st->time_base) - 1;
        /* do not send the heartbeat frame if the subtitle is already ahead */
        if (pts2 <= ist2->sub2video.last_pts)
            continue;
        if (pts2 >= ist2->sub2video.end_pts || ist2->sub2video.initialize)
            /* if we have hit the end of the current displayed subpicture,
               or if we need to initialize the system, update the
               overlayed subpicture and its start/end times */
            sub2video_update(ist2, pts2 + 1, NULL);
        for (j = 0, nb_reqs = 0; j < ist2->nb_filters; j++)
            nb_reqs += av_buffersrc_get_nb_failed_requests(ist2->filters[j]->filter);
        if (nb_reqs)
            sub2video_push_ref(ist2, pts2);
    }
}

static void sub2video_flush(InputStream *ist)
{
    int i;
    int ret;

    if (ist->sub2video.end_pts < INT64_MAX)
        sub2video_update(ist, INT64_MAX, NULL);
    for (i = 0; i < ist->nb_filters; i++) {
        ret = av_buffersrc_add_frame(ist->filters[i]->filter, NULL);
        if (ret != AVERROR_EOF && ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Flush the frame error.\n");
    }
}

/* end of sub2video hack */

static void term_exit_sigsafe(void)
{
#if HAVE_TERMIOS_H
    if(restore_tty)
        tcsetattr (0, TCSANOW, &oldtty);
#endif
}

void term_exit(void)
{
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    term_exit_sigsafe();
}

static volatile int received_sigterm = 0;
static volatile int received_nb_signals = 0;
static atomic_int transcode_init_done = ATOMIC_VAR_INIT(0);
static volatile int ffmpeg_exited = 0;
static int64_t copy_ts_first_pts = AV_NOPTS_VALUE;

static void
sigterm_handler(int sig)
{
    int ret;
    received_sigterm = sig;
    received_nb_signals++;
    term_exit_sigsafe();
    if(received_nb_signals > 3) {
        ret = write(2/*STDERR_FILENO*/, "Received > 3 system signals, hard exiting\n",
                    strlen("Received > 3 system signals, hard exiting\n"));
        if (ret < 0) { /* Do nothing */ };
        exit(123);
    }
}

#if HAVE_SETCONSOLECTRLHANDLER
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    av_log(NULL, AV_LOG_DEBUG, "\nReceived windows signal %ld\n", fdwCtrlType);

    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        sigterm_handler(SIGINT);
        return TRUE;

    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        sigterm_handler(SIGTERM);
        /* Basically, with these 3 events, when we return from this method the
           process is hard terminated, so stall as long as we need to
           to try and let the main thread(s) clean up and gracefully terminate
           (we have at most 5 seconds, but should be done far before that). */
        while (!ffmpeg_exited) {
            Sleep(0);
        }
        return TRUE;

    default:
        av_log(NULL, AV_LOG_ERROR, "Received unknown windows signal %ld\n", fdwCtrlType);
        return FALSE;
    }
}
#endif

#ifdef __linux__
#define SIGNAL(sig, func)               \
    do {                                \
        action.sa_handler = func;       \
        sigaction(sig, &action, NULL);  \
    } while (0)
#else
#define SIGNAL(sig, func) \
    signal(sig, func)
#endif

void term_init(void)
{
#if defined __linux__
    struct sigaction action = {0};
    action.sa_handler = sigterm_handler;

    /* block other interrupts while processing this one */
    sigfillset(&action.sa_mask);

    /* restart interruptible functions (i.e. don't fail with EINTR)  */
    action.sa_flags = SA_RESTART;
#endif

#if HAVE_TERMIOS_H
    if (stdin_interaction) {
        struct termios tty;
        if (tcgetattr (0, &tty) == 0) {
            oldtty = tty;
            restore_tty = 1;

            tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                             |INLCR|IGNCR|ICRNL|IXON);
            tty.c_oflag |= OPOST;
            tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
            tty.c_cflag &= ~(CSIZE|PARENB);
            tty.c_cflag |= CS8;
            tty.c_cc[VMIN] = 1;
            tty.c_cc[VTIME] = 0;

            tcsetattr (0, TCSANOW, &tty);
        }
        SIGNAL(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
    }
#endif

    SIGNAL(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    SIGNAL(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
#ifdef SIGXCPU
    SIGNAL(SIGXCPU, sigterm_handler);
#endif
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN); /* Broken pipe (POSIX). */
#endif
#if HAVE_SETCONSOLECTRLHANDLER
    SetConsoleCtrlHandler((PHANDLER_ROUTINE) CtrlHandler, TRUE);
#endif
}

/* read a key without blocking */
static int read_key(void)
{
    unsigned char ch;
#if HAVE_TERMIOS_H
    int n = 1;
    struct timeval tv;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(1, &rfds, NULL, NULL, &tv);
    if (n > 0) {
        n = read(0, &ch, 1);
        if (n == 1)
            return ch;

        return n;
    }
#elif HAVE_KBHIT
#    if HAVE_PEEKNAMEDPIPE
    static int is_pipe;
    static HANDLE input_handle;
    DWORD dw, nchars;
    if(!input_handle){
        input_handle = GetStdHandle(STD_INPUT_HANDLE);
        is_pipe = !GetConsoleMode(input_handle, &dw);
    }

    if (is_pipe) {
        /* When running under a GUI, you will end here. */
        if (!PeekNamedPipe(input_handle, NULL, 0, NULL, &nchars, NULL)) {
            // input pipe may have been closed by the program that ran ffmpeg
            return -1;
        }
        //Read it
        if(nchars != 0) {
            read(0, &ch, 1);
            return ch;
        }else{
            return -1;
        }
    }
#    endif
    if(kbhit())
        return(getch());
#endif
    return -1;
}

static int decode_interrupt_cb(void *ctx)
{
    return received_nb_signals > atomic_load(&transcode_init_done);
}

const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };

static void ffmpeg_cleanup(int ret)
{
    int i;

    if (do_benchmark) {
        int maxrss = getmaxrss() / 1024;
        av_log(NULL, AV_LOG_INFO, "bench: maxrss=%ikB\n", maxrss);
    }

    for (i = 0; i < nb_filtergraphs; i++)
        fg_free(&filtergraphs[i]);
    av_freep(&filtergraphs);

    /* close files */
    for (i = 0; i < nb_output_files; i++)
        of_close(&output_files[i]);

    for (i = 0; i < nb_input_files; i++)
        ifile_close(&input_files[i]);

    if (vstats_file) {
        if (fclose(vstats_file))
            av_log(NULL, AV_LOG_ERROR,
                   "Error closing vstats file, loss of information possible: %s\n",
                   av_err2str(AVERROR(errno)));
    }
    av_freep(&vstats_filename);
    of_enc_stats_close();

    hw_device_free_all();

    av_freep(&filter_nbthreads);

    av_freep(&input_files);
    av_freep(&output_files);

    uninit_opts();

    avformat_network_deinit();

    if (received_sigterm) {
        av_log(NULL, AV_LOG_INFO, "Exiting normally, received signal %d.\n",
               (int) received_sigterm);
    } else if (ret && atomic_load(&transcode_init_done)) {
        av_log(NULL, AV_LOG_INFO, "Conversion failed!\n");
    }
    term_exit();
    ffmpeg_exited = 1;
}

OutputStream *ost_iter(OutputStream *prev)
{
    int of_idx  = prev ? prev->file_index : 0;
    int ost_idx = prev ? prev->index + 1  : 0;

    for (; of_idx < nb_output_files; of_idx++) {
        OutputFile *of = output_files[of_idx];
        if (ost_idx < of->nb_streams)
            return of->streams[ost_idx];

        ost_idx = 0;
    }

    return NULL;
}

InputStream *ist_iter(InputStream *prev)
{
    int if_idx  = prev ? prev->file_index : 0;
    int ist_idx = prev ? prev->st->index + 1  : 0;

    for (; if_idx < nb_input_files; if_idx++) {
        InputFile *f = input_files[if_idx];
        if (ist_idx < f->nb_streams)
            return f->streams[ist_idx];

        ist_idx = 0;
    }

    return NULL;
}

void remove_avoptions(AVDictionary **a, AVDictionary *b)
{
    const AVDictionaryEntry *t = NULL;

    while ((t = av_dict_iterate(b, t))) {
        av_dict_set(a, t->key, NULL, AV_DICT_MATCH_CASE);
    }
}

void assert_avoptions(AVDictionary *m)
{
    const AVDictionaryEntry *t;
    if ((t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_FATAL, "Option %s not found.\n", t->key);
        exit_program(1);
    }
}

void update_benchmark(const char *fmt, ...)
{
    if (do_benchmark_all) {
        BenchmarkTimeStamps t = get_benchmark_time_stamps();
        va_list va;
        char buf[1024];

        if (fmt) {
            va_start(va, fmt);
            vsnprintf(buf, sizeof(buf), fmt, va);
            va_end(va);
            av_log(NULL, AV_LOG_INFO,
                   "bench: %8" PRIu64 " user %8" PRIu64 " sys %8" PRIu64 " real %s \n",
                   t.user_usec - current_time.user_usec,
                   t.sys_usec - current_time.sys_usec,
                   t.real_usec - current_time.real_usec, buf);
        }
        current_time = t;
    }
}

void close_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    ost->finished |= ENCODER_FINISHED;

    if (ost->sq_idx_encode >= 0)
        sq_send(of->sq_encode, ost->sq_idx_encode, SQFRAME(NULL));
}

static void print_report(int is_last_report, int64_t timer_start, int64_t cur_time)
{
    AVBPrint buf, buf_script;
    int64_t total_size = of_filesize(output_files[0]);
    int vid;
    double bitrate;
    double speed;
    int64_t pts = INT64_MIN + 1;
    static int64_t last_time = -1;
    static int first_report = 1;
    int hours, mins, secs, us;
    const char *hours_sign;
    int ret;
    float t;

    if (!print_stats && !is_last_report && !progress_avio)
        return;

    if (!is_last_report) {
        if (last_time == -1) {
            last_time = cur_time;
        }
        if (((cur_time - last_time) < stats_period && !first_report) ||
            (first_report && nb_output_dumped < nb_output_files))
            return;
        last_time = cur_time;
    }

    t = (cur_time-timer_start) / 1000000.0;

    vid = 0;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprint_init(&buf_script, 0, AV_BPRINT_SIZE_AUTOMATIC);
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        const float q = ost->enc ? ost->quality / (float) FF_QP2LAMBDA : -1;

        if (vid && ost->type == AVMEDIA_TYPE_VIDEO) {
            av_bprintf(&buf, "q=%2.1f ", q);
            av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                       ost->file_index, ost->index, q);
        }
        if (!vid && ost->type == AVMEDIA_TYPE_VIDEO) {
            float fps;
            uint64_t frame_number = atomic_load(&ost->packets_written);

            fps = t > 1 ? frame_number / t : 0;
            av_bprintf(&buf, "frame=%5"PRId64" fps=%3.*f q=%3.1f ",
                     frame_number, fps < 9.95, fps, q);
            av_bprintf(&buf_script, "frame=%"PRId64"\n", frame_number);
            av_bprintf(&buf_script, "fps=%.2f\n", fps);
            av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                       ost->file_index, ost->index, q);
            if (is_last_report)
                av_bprintf(&buf, "L");

            vid = 1;
        }
        /* compute min output value */
        if (ost->last_mux_dts != AV_NOPTS_VALUE) {
            pts = FFMAX(pts, ost->last_mux_dts);
            if (copy_ts) {
                if (copy_ts_first_pts == AV_NOPTS_VALUE && pts > 1)
                    copy_ts_first_pts = pts;
                if (copy_ts_first_pts != AV_NOPTS_VALUE)
                    pts -= copy_ts_first_pts;
            }
        }

        if (is_last_report)
            nb_frames_drop += ost->last_dropped;
    }

    secs = FFABS(pts) / AV_TIME_BASE;
    us = FFABS(pts) % AV_TIME_BASE;
    mins = secs / 60;
    secs %= 60;
    hours = mins / 60;
    mins %= 60;
    hours_sign = (pts < 0) ? "-" : "";

    bitrate = pts && total_size >= 0 ? total_size * 8 / (pts / 1000.0) : -1;
    speed = t != 0.0 ? (double)pts / AV_TIME_BASE / t : -1;

    if (total_size < 0) av_bprintf(&buf, "size=N/A time=");
    else                av_bprintf(&buf, "size=%8.0fkB time=", total_size / 1024.0);
    if (pts == AV_NOPTS_VALUE) {
        av_bprintf(&buf, "N/A ");
    } else {
        av_bprintf(&buf, "%s%02d:%02d:%02d.%02d ",
                   hours_sign, hours, mins, secs, (100 * us) / AV_TIME_BASE);
    }

    if (bitrate < 0) {
        av_bprintf(&buf, "bitrate=N/A");
        av_bprintf(&buf_script, "bitrate=N/A\n");
    }else{
        av_bprintf(&buf, "bitrate=%6.1fkbits/s", bitrate);
        av_bprintf(&buf_script, "bitrate=%6.1fkbits/s\n", bitrate);
    }

    if (total_size < 0) av_bprintf(&buf_script, "total_size=N/A\n");
    else                av_bprintf(&buf_script, "total_size=%"PRId64"\n", total_size);
    if (pts == AV_NOPTS_VALUE) {
        av_bprintf(&buf_script, "out_time_us=N/A\n");
        av_bprintf(&buf_script, "out_time_ms=N/A\n");
        av_bprintf(&buf_script, "out_time=N/A\n");
    } else {
        av_bprintf(&buf_script, "out_time_us=%"PRId64"\n", pts);
        av_bprintf(&buf_script, "out_time_ms=%"PRId64"\n", pts);
        av_bprintf(&buf_script, "out_time=%s%02d:%02d:%02d.%06d\n",
                   hours_sign, hours, mins, secs, us);
    }

    if (nb_frames_dup || nb_frames_drop)
        av_bprintf(&buf, " dup=%"PRId64" drop=%"PRId64, nb_frames_dup, nb_frames_drop);
    av_bprintf(&buf_script, "dup_frames=%"PRId64"\n", nb_frames_dup);
    av_bprintf(&buf_script, "drop_frames=%"PRId64"\n", nb_frames_drop);

    if (speed < 0) {
        av_bprintf(&buf, " speed=N/A");
        av_bprintf(&buf_script, "speed=N/A\n");
    } else {
        av_bprintf(&buf, " speed=%4.3gx", speed);
        av_bprintf(&buf_script, "speed=%4.3gx\n", speed);
    }

    if (print_stats || is_last_report) {
        const char end = is_last_report ? '\n' : '\r';
        if (print_stats==1 && AV_LOG_INFO > av_log_get_level()) {
            fprintf(stderr, "%s    %c", buf.str, end);
        } else
            av_log(NULL, AV_LOG_INFO, "%s    %c", buf.str, end);

        fflush(stderr);
    }
    av_bprint_finalize(&buf, NULL);

    if (progress_avio) {
        av_bprintf(&buf_script, "progress=%s\n",
                   is_last_report ? "end" : "continue");
        avio_write(progress_avio, buf_script.str,
                   FFMIN(buf_script.len, buf_script.size - 1));
        avio_flush(progress_avio);
        av_bprint_finalize(&buf_script, NULL);
        if (is_last_report) {
            if ((ret = avio_closep(&progress_avio)) < 0)
                av_log(NULL, AV_LOG_ERROR,
                       "Error closing progress log, loss of information possible: %s\n", av_err2str(ret));
        }
    }

    first_report = 0;
}

int ifilter_parameters_from_codecpar(InputFilter *ifilter, AVCodecParameters *par)
{
    int ret;

    // We never got any input. Set a fake format, which will
    // come from libavformat.
    ifilter->format                 = par->format;
    ifilter->sample_rate            = par->sample_rate;
    ifilter->width                  = par->width;
    ifilter->height                 = par->height;
    ifilter->sample_aspect_ratio    = par->sample_aspect_ratio;
    ret = av_channel_layout_copy(&ifilter->ch_layout, &par->ch_layout);
    if (ret < 0)
        return ret;

    return 0;
}

static void check_decode_result(InputStream *ist, int *got_output, int ret)
{
    if (*got_output || ret<0)
        decode_error_stat[ret<0] ++;

    if (ret < 0 && exit_on_error)
        exit_program(1);

    if (*got_output && ist) {
        if (ist->decoded_frame->decode_error_flags || (ist->decoded_frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            av_log(NULL, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "%s: corrupt decoded frame in stream %d\n", input_files[ist->file_index]->ctx->url, ist->st->index);
            if (exit_on_error)
                exit_program(1);
        }
    }
}

// Filters can be configured only if the formats of all inputs are known.
int ifilter_has_all_input_formats(FilterGraph *fg)
{
    int i;
    for (i = 0; i < fg->nb_inputs; i++) {
        if (fg->inputs[i]->format < 0 && (fg->inputs[i]->type == AVMEDIA_TYPE_AUDIO ||
                                          fg->inputs[i]->type == AVMEDIA_TYPE_VIDEO))
            return 0;
    }
    return 1;
}

// This does not quite work like avcodec_decode_audio4/avcodec_decode_video2.
// There is the following difference: if you got a frame, you must call
// it again with pkt=NULL. pkt==NULL is treated differently from pkt->size==0
// (pkt==NULL means get more output, pkt->size==0 is a flush/drain packet)
static int decode(InputStream *ist, AVCodecContext *avctx,
                  AVFrame *frame, int *got_frame, const AVPacket *pkt)
{
    int ret;

    *got_frame = 0;

    if (pkt) {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return ret;
    if (ret >= 0) {
        if (ist->want_frame_data) {
            FrameData *fd;

            av_assert0(!frame->opaque_ref);
            frame->opaque_ref = av_buffer_allocz(sizeof(*fd));
            if (!frame->opaque_ref) {
                av_frame_unref(frame);
                return AVERROR(ENOMEM);
            }
            fd      = (FrameData*)frame->opaque_ref->data;
            fd->pts = frame->pts;
            fd->tb  = avctx->pkt_timebase;
            fd->idx = avctx->frame_num - 1;
        }

        frame->time_base = avctx->pkt_timebase;

        *got_frame = 1;
    }

    return 0;
}

static int send_frame_to_filters(InputStream *ist, AVFrame *decoded_frame)
{
    int i, ret;

    av_assert1(ist->nb_filters > 0); /* ensure ret is initialized */
    for (i = 0; i < ist->nb_filters; i++) {
        ret = ifilter_send_frame(ist->filters[i], decoded_frame, i < ist->nb_filters - 1);
        if (ret == AVERROR_EOF)
            ret = 0; /* ignore */
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to inject frame into filter network: %s\n", av_err2str(ret));
            break;
        }
    }
    return ret;
}

static AVRational audio_samplerate_update(InputStream *ist, const AVFrame *frame)
{
    const int prev = ist->last_frame_tb.den;
    const int sr   = frame->sample_rate;

    AVRational tb_new;
    int64_t gcd;

    if (frame->sample_rate == ist->last_frame_sample_rate)
        goto finish;

    gcd  = av_gcd(prev, sr);

    if (prev / gcd >= INT_MAX / sr) {
        av_log(ist, AV_LOG_WARNING,
               "Audio timestamps cannot be represented exactly after "
               "sample rate change: %d -> %d\n", prev, sr);

        // LCM of 192000, 44100, allows to represent all common samplerates
        tb_new = (AVRational){ 1, 28224000 };
    } else
        tb_new = (AVRational){ 1, prev / gcd * sr };

    // keep the frame timebase if it is strictly better than
    // the samplerate-defined one
    if (frame->time_base.num == 1 && frame->time_base.den > tb_new.den &&
        !(frame->time_base.den % tb_new.den))
        tb_new = frame->time_base;

    if (ist->last_frame_pts != AV_NOPTS_VALUE)
        ist->last_frame_pts = av_rescale_q(ist->last_frame_pts,
                                           ist->last_frame_tb, tb_new);
    ist->last_frame_duration_est = av_rescale_q(ist->last_frame_duration_est,
                                                ist->last_frame_tb, tb_new);

    ist->last_frame_tb          = tb_new;
    ist->last_frame_sample_rate = frame->sample_rate;

finish:
    return ist->last_frame_tb;
}

static void audio_ts_process(InputStream *ist, AVFrame *frame)
{
    AVRational tb_filter = (AVRational){1, frame->sample_rate};
    AVRational tb;
    int64_t pts_pred;

    // on samplerate change, choose a new internal timebase for timestamp
    // generation that can represent timestamps from all the samplerates
    // seen so far
    tb = audio_samplerate_update(ist, frame);
    pts_pred = ist->last_frame_pts == AV_NOPTS_VALUE ? 0 :
               ist->last_frame_pts + ist->last_frame_duration_est;

    if (frame->pts == AV_NOPTS_VALUE) {
        frame->pts = pts_pred;
        frame->time_base = tb;
    } else if (ist->last_frame_pts != AV_NOPTS_VALUE &&
               frame->pts > av_rescale_q_rnd(pts_pred, tb, frame->time_base,
                                             AV_ROUND_UP)) {
        // there was a gap in timestamps, reset conversion state
        ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;
    }

    frame->pts = av_rescale_delta(frame->time_base, frame->pts,
                                  tb, frame->nb_samples,
                                  &ist->filter_in_rescale_delta_last, tb);

    ist->last_frame_pts          = frame->pts;
    ist->last_frame_duration_est = av_rescale_q(frame->nb_samples,
                                                tb_filter, tb);

    // finally convert to filtering timebase
    frame->pts       = av_rescale_q(frame->pts, tb, tb_filter);
    frame->duration  = frame->nb_samples;
    frame->time_base = tb_filter;
}

static int decode_audio(InputStream *ist, const AVPacket *pkt, int *got_output,
                        int *decode_failed)
{
    AVFrame *decoded_frame = ist->decoded_frame;
    AVCodecContext *avctx = ist->dec_ctx;
    int ret, err = 0;

    update_benchmark(NULL);
    ret = decode(ist, avctx, decoded_frame, got_output, pkt);
    update_benchmark("decode_audio %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

    if (ret != AVERROR_EOF)
        check_decode_result(ist, got_output, ret);

    if (!*got_output || ret < 0)
        return ret;

    ist->samples_decoded += decoded_frame->nb_samples;
    ist->frames_decoded++;

    audio_ts_process(ist, decoded_frame);

    ist->nb_samples = decoded_frame->nb_samples;
    err = send_frame_to_filters(ist, decoded_frame);

    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int64_t video_duration_estimate(const InputStream *ist, const AVFrame *frame)
{
    const InputFile   *ifile = input_files[ist->file_index];
    const int container_nots = !!(ifile->ctx->iformat->flags & AVFMT_NOTIMESTAMPS);
    int64_t codec_duration = 0;

    // XXX lavf currently makes up frame durations when they are not provided by
    // the container. As there is no way to reliably distinguish real container
    // durations from the fake made-up ones, we use heuristics based on whether
    // the container has timestamps. Eventually lavf should stop making up
    // durations, then this should be simplified.

    // prefer frame duration for containers with timestamps
    if (frame->duration > 0 && !container_nots)
        return frame->duration;

    if (ist->dec_ctx->framerate.den && ist->dec_ctx->framerate.num) {
        int ticks = frame->repeat_pict >= 0 ?
                    frame->repeat_pict + 1  :
                    ist->dec_ctx->ticks_per_frame;
        codec_duration = av_rescale_q(ticks, av_inv_q(ist->dec_ctx->framerate),
                                      ist->st->time_base);
    }

    // prefer codec-layer duration for containers without timestamps
    if (codec_duration > 0 && container_nots)
        return codec_duration;

    // when timestamps are available, repeat last frame's actual duration
    // (i.e. pts difference between this and last frame)
    if (frame->pts != AV_NOPTS_VALUE && ist->last_frame_pts != AV_NOPTS_VALUE &&
        frame->pts > ist->last_frame_pts)
        return frame->pts - ist->last_frame_pts;

    // try frame/codec duration
    if (frame->duration > 0)
        return frame->duration;
    if (codec_duration > 0)
        return codec_duration;

    // try average framerate
    if (ist->st->avg_frame_rate.num && ist->st->avg_frame_rate.den) {
        int64_t d = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate),
                                 ist->st->time_base);
        if (d > 0)
            return d;
    }

    // last resort is last frame's estimated duration, and 1
    return FFMAX(ist->last_frame_duration_est, 1);
}

static int decode_video(InputStream *ist, const AVPacket *pkt, int *got_output,
                        int64_t *duration_pts, int eof, int *decode_failed)
{
    AVFrame *decoded_frame = ist->decoded_frame;
    int ret = 0, err = 0;
    int64_t best_effort_timestamp;

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (!eof && pkt && pkt->size == 0)
        return 0;

    update_benchmark(NULL);
    ret = decode(ist, ist->dec_ctx, decoded_frame, got_output, pkt);
    update_benchmark("decode_video %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

    // The following line may be required in some cases where there is no parser
    // or the parser does not has_b_frames correctly
    if (ist->par->video_delay < ist->dec_ctx->has_b_frames) {
        if (ist->dec_ctx->codec_id == AV_CODEC_ID_H264) {
            ist->par->video_delay = ist->dec_ctx->has_b_frames;
        } else
            av_log(ist->dec_ctx, AV_LOG_WARNING,
                   "video_delay is larger in decoder than demuxer %d > %d.\n"
                   "If you want to help, upload a sample "
                   "of this file to https://streams.videolan.org/upload/ "
                   "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)\n",
                   ist->dec_ctx->has_b_frames,
                   ist->par->video_delay);
    }

    if (ret != AVERROR_EOF)
        check_decode_result(ist, got_output, ret);

    if (*got_output && ret >= 0) {
        if (ist->dec_ctx->width  != decoded_frame->width ||
            ist->dec_ctx->height != decoded_frame->height ||
            ist->dec_ctx->pix_fmt != decoded_frame->format) {
            av_log(NULL, AV_LOG_DEBUG, "Frame parameters mismatch context %d,%d,%d != %d,%d,%d\n",
                decoded_frame->width,
                decoded_frame->height,
                decoded_frame->format,
                ist->dec_ctx->width,
                ist->dec_ctx->height,
                ist->dec_ctx->pix_fmt);
        }
    }

    if (!*got_output || ret < 0)
        return ret;

    if(ist->top_field_first>=0)
        decoded_frame->top_field_first = ist->top_field_first;

    ist->frames_decoded++;

    if (ist->hwaccel_retrieve_data && decoded_frame->format == ist->hwaccel_pix_fmt) {
        err = ist->hwaccel_retrieve_data(ist->dec_ctx, decoded_frame);
        if (err < 0)
            goto fail;
    }

    best_effort_timestamp= decoded_frame->best_effort_timestamp;
    *duration_pts = decoded_frame->duration;

    if (ist->framerate.num)
        best_effort_timestamp = ist->cfr_next_pts++;

    // no timestamp available - extrapolate from previous frame duration
    if (best_effort_timestamp == AV_NOPTS_VALUE)
        best_effort_timestamp = ist->last_frame_pts == AV_NOPTS_VALUE ? 0 :
                                ist->last_frame_pts + ist->last_frame_duration_est;

    if(best_effort_timestamp != AV_NOPTS_VALUE) {
        decoded_frame->pts = best_effort_timestamp;
    }

    // update timestamp history
    ist->last_frame_duration_est = video_duration_estimate(ist, decoded_frame);
    ist->last_frame_pts          = decoded_frame->pts;
    ist->last_frame_tb           = decoded_frame->time_base;

    if (debug_ts) {
        av_log(ist, AV_LOG_INFO,
               "decoder -> pts:%s pts_time:%s "
               "pkt_dts:%s pkt_dts_time:%s "
               "best_effort_ts:%"PRId64" best_effort_ts_time:%s "
               "duration:%s duration_time:%s "
               "keyframe:%d frame_type:%d time_base:%d/%d\n",
               av_ts2str(decoded_frame->pts),
               av_ts2timestr(decoded_frame->pts, &ist->st->time_base),
               av_ts2str(decoded_frame->pkt_dts),
               av_ts2timestr(decoded_frame->pkt_dts, &ist->st->time_base),
               best_effort_timestamp,
               av_ts2timestr(best_effort_timestamp, &ist->st->time_base),
               av_ts2str(decoded_frame->duration),
               av_ts2timestr(decoded_frame->duration, &ist->st->time_base),
               decoded_frame->key_frame, decoded_frame->pict_type,
               ist->st->time_base.num, ist->st->time_base.den);
    }

    if (ist->st->sample_aspect_ratio.num)
        decoded_frame->sample_aspect_ratio = ist->st->sample_aspect_ratio;

    err = send_frame_to_filters(ist, decoded_frame);

fail:
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int process_subtitle(InputStream *ist, AVSubtitle *subtitle, int *got_output)
{
    int ret = 0;
    int free_sub = 1;

    if (ist->fix_sub_duration) {
        int end = 1;
        if (ist->prev_sub.got_output) {
            end = av_rescale(subtitle->pts - ist->prev_sub.subtitle.pts,
                             1000, AV_TIME_BASE);
            if (end < ist->prev_sub.subtitle.end_display_time) {
                av_log(NULL, AV_LOG_DEBUG,
                       "Subtitle duration reduced from %"PRId32" to %d%s\n",
                       ist->prev_sub.subtitle.end_display_time, end,
                       end <= 0 ? ", dropping it" : "");
                ist->prev_sub.subtitle.end_display_time = end;
            }
        }
        FFSWAP(int,        *got_output, ist->prev_sub.got_output);
        FFSWAP(int,        ret,         ist->prev_sub.ret);
        FFSWAP(AVSubtitle, *subtitle,   ist->prev_sub.subtitle);
        if (end <= 0)
            goto out;
    }

    if (!*got_output)
        return ret;

    if (ist->sub2video.frame) {
        sub2video_update(ist, INT64_MIN, subtitle);
    } else if (ist->nb_filters) {
        if (!ist->sub2video.sub_queue)
            ist->sub2video.sub_queue = av_fifo_alloc2(8, sizeof(AVSubtitle), AV_FIFO_FLAG_AUTO_GROW);
        if (!ist->sub2video.sub_queue)
            report_and_exit(AVERROR(ENOMEM));

        ret = av_fifo_write(ist->sub2video.sub_queue, subtitle, 1);
        if (ret < 0)
            exit_program(1);
        free_sub = 0;
    }

    if (!subtitle->num_rects)
        goto out;

    for (int oidx = 0; oidx < ist->nb_outputs; oidx++) {
        OutputStream *ost = ist->outputs[oidx];
        if (!ost->enc || ost->type != AVMEDIA_TYPE_SUBTITLE)
            continue;

        enc_subtitle(output_files[ost->file_index], ost, subtitle);
    }

out:
    if (free_sub)
        avsubtitle_free(subtitle);
    return ret;
}

static int copy_av_subtitle(AVSubtitle *dst, AVSubtitle *src)
{
    int ret = AVERROR_BUG;
    AVSubtitle tmp = {
        .format = src->format,
        .start_display_time = src->start_display_time,
        .end_display_time = src->end_display_time,
        .num_rects = 0,
        .rects = NULL,
        .pts = src->pts
    };

    if (!src->num_rects)
        goto success;

    if (!(tmp.rects = av_calloc(src->num_rects, sizeof(*tmp.rects))))
        return AVERROR(ENOMEM);

    for (int i = 0; i < src->num_rects; i++) {
        AVSubtitleRect *src_rect = src->rects[i];
        AVSubtitleRect *dst_rect;

        if (!(dst_rect = tmp.rects[i] = av_mallocz(sizeof(*tmp.rects[0])))) {
            ret = AVERROR(ENOMEM);
            goto cleanup;
        }

        tmp.num_rects++;

        dst_rect->type      = src_rect->type;
        dst_rect->flags     = src_rect->flags;

        dst_rect->x         = src_rect->x;
        dst_rect->y         = src_rect->y;
        dst_rect->w         = src_rect->w;
        dst_rect->h         = src_rect->h;
        dst_rect->nb_colors = src_rect->nb_colors;

        if (src_rect->text)
            if (!(dst_rect->text = av_strdup(src_rect->text))) {
                ret = AVERROR(ENOMEM);
                goto cleanup;
            }

        if (src_rect->ass)
            if (!(dst_rect->ass = av_strdup(src_rect->ass))) {
                ret = AVERROR(ENOMEM);
                goto cleanup;
            }

        for (int j = 0; j < 4; j++) {
            // SUBTITLE_BITMAP images are special in the sense that they
            // are like PAL8 images. first pointer to data, second to
            // palette. This makes the size calculation match this.
            size_t buf_size = src_rect->type == SUBTITLE_BITMAP && j == 1 ?
                              AVPALETTE_SIZE :
                              src_rect->h * src_rect->linesize[j];

            if (!src_rect->data[j])
                continue;

            if (!(dst_rect->data[j] = av_memdup(src_rect->data[j], buf_size))) {
                ret = AVERROR(ENOMEM);
                goto cleanup;
            }
            dst_rect->linesize[j] = src_rect->linesize[j];
        }
    }

success:
    *dst = tmp;

    return 0;

cleanup:
    avsubtitle_free(&tmp);

    return ret;
}

static int fix_sub_duration_heartbeat(InputStream *ist, int64_t signal_pts)
{
    int ret = AVERROR_BUG;
    int got_output = 1;
    AVSubtitle *prev_subtitle = &ist->prev_sub.subtitle;
    AVSubtitle subtitle;

    if (!ist->fix_sub_duration || !prev_subtitle->num_rects ||
        signal_pts <= prev_subtitle->pts)
        return 0;

    if ((ret = copy_av_subtitle(&subtitle, prev_subtitle)) < 0)
        return ret;

    subtitle.pts = signal_pts;

    return process_subtitle(ist, &subtitle, &got_output);
}

int trigger_fix_sub_duration_heartbeat(OutputStream *ost, const AVPacket *pkt)
{
    OutputFile *of = output_files[ost->file_index];
    int64_t signal_pts = av_rescale_q(pkt->pts, pkt->time_base,
                                      AV_TIME_BASE_Q);

    if (!ost->fix_sub_duration_heartbeat || !(pkt->flags & AV_PKT_FLAG_KEY))
        // we are only interested in heartbeats on streams configured, and
        // only on random access points.
        return 0;

    for (int i = 0; i < of->nb_streams; i++) {
        OutputStream *iter_ost = of->streams[i];
        InputStream  *ist      = iter_ost->ist;
        int ret = AVERROR_BUG;

        if (iter_ost == ost || !ist || !ist->decoding_needed ||
            ist->dec_ctx->codec_type != AVMEDIA_TYPE_SUBTITLE)
            // We wish to skip the stream that causes the heartbeat,
            // output streams without an input stream, streams not decoded
            // (as fix_sub_duration is only done for decoded subtitles) as
            // well as non-subtitle streams.
            continue;

        if ((ret = fix_sub_duration_heartbeat(ist, signal_pts)) < 0)
            return ret;
    }

    return 0;
}

static int transcode_subtitles(InputStream *ist, const AVPacket *pkt,
                               int *got_output, int *decode_failed)
{
    AVSubtitle subtitle;
    int ret = avcodec_decode_subtitle2(ist->dec_ctx,
                                       &subtitle, got_output, pkt);

    check_decode_result(NULL, got_output, ret);

    if (ret < 0 || !*got_output) {
        *decode_failed = 1;
        if (!pkt->size)
            sub2video_flush(ist);
        return ret;
    }

    ist->frames_decoded++;

    return process_subtitle(ist, &subtitle, got_output);
}

static int send_filter_eof(InputStream *ist)
{
    int i, ret;

    for (i = 0; i < ist->nb_filters; i++) {
        int64_t end_pts = ist->last_frame_pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                          ist->last_frame_pts + ist->last_frame_duration_est;
        ret = ifilter_send_eof(ist->filters[i], end_pts, ist->last_frame_tb);
        if (ret < 0)
            return ret;
    }
    return 0;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int process_input_packet(InputStream *ist, const AVPacket *pkt, int no_eof)
{
    InputFile *f = input_files[ist->file_index];
    const AVCodecParameters *par = ist->par;
    int ret = 0;
    int repeating = 0;
    int eof_reached = 0;
    int duration_exceeded;

    AVPacket *avpkt = ist->pkt;

    if (!ist->saw_first_ts) {
        ist->first_dts =
        ist->dts = ist->st->avg_frame_rate.num ? - ist->dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        if (pkt && pkt->pts != AV_NOPTS_VALUE) {
            ist->first_dts =
            ist->dts += av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
        }
        ist->saw_first_ts = 1;
    }

    if (ist->next_dts == AV_NOPTS_VALUE)
        ist->next_dts = ist->dts;

    if (pkt) {
        av_packet_unref(avpkt);
        ret = av_packet_ref(avpkt, pkt);
        if (ret < 0)
            return ret;
    }

    if (pkt && pkt->dts != AV_NOPTS_VALUE) {
        ist->next_dts = ist->dts = av_rescale_q(pkt->dts, pkt->time_base, AV_TIME_BASE_Q);
    }

    // while we have more to decode or while the decoder did output something on EOF
    while (ist->decoding_needed) {
        int64_t duration_pts = 0;
        int got_output = 0;
        int decode_failed = 0;

        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ret = decode_audio    (ist, repeating ? NULL : avpkt, &got_output,
                                   &decode_failed);
            av_packet_unref(avpkt);
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = decode_video    (ist, repeating ? NULL : avpkt, &got_output, &duration_pts, !pkt,
                                   &decode_failed);

            av_packet_unref(avpkt);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            if (repeating)
                break;
            ret = transcode_subtitles(ist, avpkt, &got_output, &decode_failed);
            if (!pkt && ret >= 0)
                ret = AVERROR_EOF;
            av_packet_unref(avpkt);
            break;
        default:
            return -1;
        }

        if (ret == AVERROR_EOF) {
            eof_reached = 1;
            break;
        }

        if (ret < 0) {
            if (decode_failed) {
                av_log(NULL, AV_LOG_ERROR, "Error while decoding stream #%d:%d: %s\n",
                       ist->file_index, ist->st->index, av_err2str(ret));
            } else {
                av_log(NULL, AV_LOG_FATAL, "Error while processing the decoded "
                       "data for stream #%d:%d\n", ist->file_index, ist->st->index);
            }
            if (!decode_failed || exit_on_error)
                exit_program(1);
            break;
        }

        if (got_output)
            ist->got_output = 1;

        if (!got_output)
            break;

        // During draining, we might get multiple output frames in this loop.
        // ffmpeg.c does not drain the filter chain on configuration changes,
        // which means if we send multiple frames at once to the filters, and
        // one of those frames changes configuration, the buffered frames will
        // be lost. This can upset certain FATE tests.
        // Decode only 1 frame per call on EOF to appease these FATE tests.
        // The ideal solution would be to rewrite decoding to use the new
        // decoding API in a better way.
        if (!pkt)
            break;

        repeating = 1;
    }

    /* after flushing, send an EOF on all the filter inputs attached to the stream */
    /* except when looping we need to flush but not to send an EOF */
    if (!pkt && ist->decoding_needed && eof_reached && !no_eof) {
        int ret = send_filter_eof(ist);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Error marking filters as finished\n");
            exit_program(1);
        }
    }

    if (pkt) {
        ist->dts = ist->next_dts;
        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            av_assert1(pkt->duration >= 0);
            if (par->sample_rate) {
                ist->next_dts += ((int64_t)AV_TIME_BASE * par->frame_size) /
                                  par->sample_rate;
            } else {
                ist->next_dts += av_rescale_q(pkt->duration, pkt->time_base, AV_TIME_BASE_Q);
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (ist->framerate.num) {
                // TODO: Remove work-around for c99-to-c89 issue 7
                AVRational time_base_q = AV_TIME_BASE_Q;
                int64_t next_dts = av_rescale_q(ist->next_dts, time_base_q, av_inv_q(ist->framerate));
                ist->next_dts = av_rescale_q(next_dts + 1, av_inv_q(ist->framerate), time_base_q);
            } else if (pkt->duration) {
                ist->next_dts += av_rescale_q(pkt->duration, pkt->time_base, AV_TIME_BASE_Q);
            } else if(ist->dec_ctx->framerate.num != 0) {
                int ticks = ist->last_pkt_repeat_pict >= 0 ?
                            ist->last_pkt_repeat_pict + 1  :
                            ist->dec_ctx->ticks_per_frame;
                ist->next_dts += ((int64_t)AV_TIME_BASE *
                                  ist->dec_ctx->framerate.den * ticks) /
                                  ist->dec_ctx->framerate.num / ist->dec_ctx->ticks_per_frame;
            }
            break;
        }
    }

    if (!pkt && !ist->decoding_needed)
        eof_reached = 1;

    duration_exceeded = 0;
    if (f->recording_time != INT64_MAX) {
        int64_t start_time = 0;
        if (copy_ts) {
            start_time += f->start_time != AV_NOPTS_VALUE ? f->start_time : 0;
            start_time += start_at_zero ? 0 : f->start_time_effective;
        }
        if (ist->dts >= f->recording_time + start_time)
            duration_exceeded = 1;
    }

    for (int oidx = 0; oidx < ist->nb_outputs; oidx++) {
        OutputStream *ost = ist->outputs[oidx];
        if (ost->enc || (!pkt && no_eof))
            continue;

        if (duration_exceeded) {
            close_output_stream(ost);
            continue;
        }

        of_streamcopy(ost, pkt, ist->dts);
    }

    return !eof_reached;
}

static int transcode_init(void)
{
    int ret = 0;

    /* init framerate emulation */
    for (int i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        if (ifile->readrate || ifile->rate_emu)
            for (int j = 0; j < ifile->nb_streams; j++)
                ifile->streams[j]->start = av_gettime_relative();
    }

    /* discard unused programs */
    for (int i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        for (int j = 0; j < ifile->ctx->nb_programs; j++) {
            AVProgram *p = ifile->ctx->programs[j];
            int discard  = AVDISCARD_ALL;

            for (int k = 0; k < p->nb_stream_indexes; k++)
                if (!ifile->streams[p->stream_index[k]]->discard) {
                    discard = AVDISCARD_DEFAULT;
                    break;
                }
            p->discard = discard;
        }
    }

    /* dump the stream mapping */
    av_log(NULL, AV_LOG_INFO, "Stream mapping:\n");
    for (InputStream *ist = ist_iter(NULL); ist; ist = ist_iter(ist)) {
        for (int j = 0; j < ist->nb_filters; j++) {
            if (!filtergraph_is_simple(ist->filters[j]->graph)) {
                av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d (%s) -> %s",
                       ist->file_index, ist->st->index, ist->dec ? ist->dec->name : "?",
                       ist->filters[j]->name);
                if (nb_filtergraphs > 1)
                    av_log(NULL, AV_LOG_INFO, " (graph %d)", ist->filters[j]->graph->index);
                av_log(NULL, AV_LOG_INFO, "\n");
            }
        }
    }

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        if (ost->attachment_filename) {
            /* an attached file */
            av_log(NULL, AV_LOG_INFO, "  File %s -> Stream #%d:%d\n",
                   ost->attachment_filename, ost->file_index, ost->index);
            continue;
        }

        if (ost->filter && !filtergraph_is_simple(ost->filter->graph)) {
            /* output from a complex graph */
            av_log(NULL, AV_LOG_INFO, "  %s", ost->filter->name);
            if (nb_filtergraphs > 1)
                av_log(NULL, AV_LOG_INFO, " (graph %d)", ost->filter->graph->index);

            av_log(NULL, AV_LOG_INFO, " -> Stream #%d:%d (%s)\n", ost->file_index,
                   ost->index, ost->enc_ctx->codec->name);
            continue;
        }

        av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d -> #%d:%d",
               ost->ist->file_index,
               ost->ist->st->index,
               ost->file_index,
               ost->index);
        if (ost->enc_ctx) {
            const AVCodec *in_codec    = ost->ist->dec;
            const AVCodec *out_codec   = ost->enc_ctx->codec;
            const char *decoder_name   = "?";
            const char *in_codec_name  = "?";
            const char *encoder_name   = "?";
            const char *out_codec_name = "?";
            const AVCodecDescriptor *desc;

            if (in_codec) {
                decoder_name  = in_codec->name;
                desc = avcodec_descriptor_get(in_codec->id);
                if (desc)
                    in_codec_name = desc->name;
                if (!strcmp(decoder_name, in_codec_name))
                    decoder_name = "native";
            }

            if (out_codec) {
                encoder_name   = out_codec->name;
                desc = avcodec_descriptor_get(out_codec->id);
                if (desc)
                    out_codec_name = desc->name;
                if (!strcmp(encoder_name, out_codec_name))
                    encoder_name = "native";
            }

            av_log(NULL, AV_LOG_INFO, " (%s (%s) -> %s (%s))",
                   in_codec_name, decoder_name,
                   out_codec_name, encoder_name);
        } else
            av_log(NULL, AV_LOG_INFO, " (copy)");
        av_log(NULL, AV_LOG_INFO, "\n");
    }

    if (ret)
        return ret;

    atomic_store(&transcode_init_done, 1);

    return 0;
}

/* Return 1 if there remain streams where more output is wanted, 0 otherwise. */
static int need_output(void)
{
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        if (ost->finished)
            continue;

        return 1;
    }

    return 0;
}

/**
 * Select the output stream to process.
 *
 * @return  selected output stream, or NULL if none available
 */
static OutputStream *choose_output(void)
{
    int64_t opts_min = INT64_MAX;
    OutputStream *ost_min = NULL;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        int64_t opts;

        if (ost->filter && ost->filter->last_pts != AV_NOPTS_VALUE) {
            opts = ost->filter->last_pts;
        } else {
            opts = ost->last_mux_dts == AV_NOPTS_VALUE ?
                   INT64_MIN : ost->last_mux_dts;
            if (ost->last_mux_dts == AV_NOPTS_VALUE)
                av_log(ost, AV_LOG_DEBUG,
                    "cur_dts is invalid [init:%d i_done:%d finish:%d] (this is harmless if it occurs once at the start per stream)\n",
                    ost->initialized, ost->inputs_done, ost->finished);
        }

        if (!ost->initialized && !ost->inputs_done && !ost->finished)
            return ost->unavailable ? NULL : ost;

        if (!ost->finished && opts < opts_min) {
            opts_min = opts;
            ost_min  = ost->unavailable ? NULL : ost;
        }
    }
    return ost_min;
}

static void set_tty_echo(int on)
{
#if HAVE_TERMIOS_H
    struct termios tty;
    if (tcgetattr(0, &tty) == 0) {
        if (on) tty.c_lflag |= ECHO;
        else    tty.c_lflag &= ~ECHO;
        tcsetattr(0, TCSANOW, &tty);
    }
#endif
}

static int check_keyboard_interaction(int64_t cur_time)
{
    int i, ret, key;
    static int64_t last_time;
    if (received_nb_signals)
        return AVERROR_EXIT;
    /* read_key() returns 0 on EOF */
    if (cur_time - last_time >= 100000) {
        key =  read_key();
        last_time = cur_time;
    }else
        key = -1;
    if (key == 'q') {
        av_log(NULL, AV_LOG_INFO, "\n\n[q] command received. Exiting.\n\n");
        return AVERROR_EXIT;
    }
    if (key == '+') av_log_set_level(av_log_get_level()+10);
    if (key == '-') av_log_set_level(av_log_get_level()-10);
    if (key == 'c' || key == 'C'){
        char buf[4096], target[64], command[256], arg[256] = {0};
        double time;
        int k, n = 0;
        fprintf(stderr, "\nEnter command: <target>|all <time>|-1 <command>[ <argument>]\n");
        i = 0;
        set_tty_echo(1);
        while ((k = read_key()) != '\n' && k != '\r' && i < sizeof(buf)-1)
            if (k > 0)
                buf[i++] = k;
        buf[i] = 0;
        set_tty_echo(0);
        fprintf(stderr, "\n");
        if (k > 0 &&
            (n = sscanf(buf, "%63[^ ] %lf %255[^ ] %255[^\n]", target, &time, command, arg)) >= 3) {
            av_log(NULL, AV_LOG_DEBUG, "Processing command target:%s time:%f command:%s arg:%s",
                   target, time, command, arg);
            for (i = 0; i < nb_filtergraphs; i++) {
                FilterGraph *fg = filtergraphs[i];
                if (fg->graph) {
                    if (time < 0) {
                        ret = avfilter_graph_send_command(fg->graph, target, command, arg, buf, sizeof(buf),
                                                          key == 'c' ? AVFILTER_CMD_FLAG_ONE : 0);
                        fprintf(stderr, "Command reply for stream %d: ret:%d res:\n%s", i, ret, buf);
                    } else if (key == 'c') {
                        fprintf(stderr, "Queuing commands only on filters supporting the specific command is unsupported\n");
                        ret = AVERROR_PATCHWELCOME;
                    } else {
                        ret = avfilter_graph_queue_command(fg->graph, target, command, arg, 0, time);
                        if (ret < 0)
                            fprintf(stderr, "Queuing command failed with error %s\n", av_err2str(ret));
                    }
                }
            }
        } else {
            av_log(NULL, AV_LOG_ERROR,
                   "Parse error, at least 3 arguments were expected, "
                   "only %d given in string '%s'\n", n, buf);
        }
    }
    if (key == '?'){
        fprintf(stderr, "key    function\n"
                        "?      show this help\n"
                        "+      increase verbosity\n"
                        "-      decrease verbosity\n"
                        "c      Send command to first matching filter supporting it\n"
                        "C      Send/Queue command to all matching filters\n"
                        "h      dump packets/hex press to cycle through the 3 states\n"
                        "q      quit\n"
                        "s      Show QP histogram\n"
        );
    }
    return 0;
}

static int got_eagain(void)
{
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost))
        if (ost->unavailable)
            return 1;
    return 0;
}

static void reset_eagain(void)
{
    int i;
    for (i = 0; i < nb_input_files; i++)
        input_files[i]->eagain = 0;
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost))
        ost->unavailable = 0;
}

static void decode_flush(InputFile *ifile)
{
    for (int i = 0; i < ifile->nb_streams; i++) {
        InputStream *ist = ifile->streams[i];
        int ret;

        if (ist->discard)
            continue;

        do {
            ret = process_input_packet(ist, NULL, 1);
        } while (ret > 0);

        if (ist->decoding_needed) {
            /* report last frame duration to the demuxer thread */
            if (ist->par->codec_type == AVMEDIA_TYPE_AUDIO) {
                LastFrameDuration dur;

                dur.stream_idx = i;
                dur.duration   = av_rescale_q(ist->nb_samples,
                                              (AVRational){ 1, ist->dec_ctx->sample_rate},
                                              ist->st->time_base);

                av_thread_message_queue_send(ifile->audio_duration_queue, &dur, 0);
            }

            avcodec_flush_buffers(ist->dec_ctx);
        }
    }
}

static void ts_discontinuity_detect(InputFile *ifile, InputStream *ist,
                                    AVPacket *pkt)
{
    const int fmt_is_discont = ifile->ctx->iformat->flags & AVFMT_TS_DISCONT;
    int disable_discontinuity_correction = copy_ts;
    int64_t pkt_dts = av_rescale_q_rnd(pkt->dts, pkt->time_base, AV_TIME_BASE_Q,
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    if (copy_ts && ist->next_dts != AV_NOPTS_VALUE &&
        fmt_is_discont && ist->st->pts_wrap_bits < 60) {
        int64_t wrap_dts = av_rescale_q_rnd(pkt->dts + (1LL<<ist->st->pts_wrap_bits),
                                            pkt->time_base, AV_TIME_BASE_Q,
                                            AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        if (FFABS(wrap_dts - ist->next_dts) < FFABS(pkt_dts - ist->next_dts)/10)
            disable_discontinuity_correction = 0;
    }

    if (ist->next_dts != AV_NOPTS_VALUE && !disable_discontinuity_correction) {
        int64_t delta = pkt_dts - ist->next_dts;
        if (fmt_is_discont) {
            if (FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE ||
                pkt_dts + AV_TIME_BASE/10 < ist->dts) {
                ifile->ts_offset_discont -= delta;
                av_log(NULL, AV_LOG_WARNING,
                       "timestamp discontinuity for stream #%d:%d "
                       "(id=%d, type=%s): %"PRId64", new offset= %"PRId64"\n",
                       ist->file_index, ist->st->index, ist->st->id,
                       av_get_media_type_string(ist->par->codec_type),
                       delta, ifile->ts_offset_discont);
                pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
            }
        } else {
            if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                av_log(NULL, AV_LOG_WARNING, "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n", pkt->dts, ist->next_dts, pkt->stream_index);
                pkt->dts = AV_NOPTS_VALUE;
            }
            if (pkt->pts != AV_NOPTS_VALUE){
                int64_t pkt_pts = av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
                delta = pkt_pts - ist->next_dts;
                if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                    av_log(NULL, AV_LOG_WARNING, "PTS %"PRId64", next:%"PRId64" invalid dropping st:%d\n", pkt->pts, ist->next_dts, pkt->stream_index);
                    pkt->pts = AV_NOPTS_VALUE;
                }
            }
        }
    } else if (ist->next_dts == AV_NOPTS_VALUE && !copy_ts &&
               fmt_is_discont && ifile->last_ts != AV_NOPTS_VALUE) {
        int64_t delta = pkt_dts - ifile->last_ts;
        if (FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE) {
            ifile->ts_offset_discont -= delta;
            av_log(NULL, AV_LOG_DEBUG,
                   "Inter stream timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                   delta, ifile->ts_offset_discont);
            pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
            if (pkt->pts != AV_NOPTS_VALUE)
                pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
        }
    }

    ifile->last_ts = av_rescale_q(pkt->dts, pkt->time_base, AV_TIME_BASE_Q);
}

static void ts_discontinuity_process(InputFile *ifile, InputStream *ist,
                                     AVPacket *pkt)
{
    int64_t offset = av_rescale_q(ifile->ts_offset_discont, AV_TIME_BASE_Q,
                                  pkt->time_base);

    // apply previously-detected timestamp-discontinuity offset
    // (to all streams, not just audio/video)
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += offset;
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += offset;

    // detect timestamp discontinuities for audio/video
    if ((ist->par->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->par->codec_type == AVMEDIA_TYPE_AUDIO) &&
        pkt->dts != AV_NOPTS_VALUE)
        ts_discontinuity_detect(ifile, ist, pkt);
}

/*
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
static int process_input(int file_index)
{
    InputFile *ifile = input_files[file_index];
    AVFormatContext *is;
    InputStream *ist;
    AVPacket *pkt;
    int ret, i;

    is  = ifile->ctx;
    ret = ifile_get_packet(ifile, &pkt);

    if (ret == AVERROR(EAGAIN)) {
        ifile->eagain = 1;
        return ret;
    }
    if (ret == 1) {
        /* the input file is looped: flush the decoders */
        decode_flush(ifile);
        return AVERROR(EAGAIN);
    }
    if (ret < 0) {
        if (ret != AVERROR_EOF) {
            print_error(is->url, ret);
            if (exit_on_error)
                exit_program(1);
        }

        for (i = 0; i < ifile->nb_streams; i++) {
            ist = ifile->streams[i];
            if (!ist->discard) {
                ret = process_input_packet(ist, NULL, 0);
                if (ret>0)
                    return 0;
            }

            /* mark all outputs that don't go through lavfi as finished */
            for (int oidx = 0; oidx < ist->nb_outputs; oidx++) {
                OutputStream *ost = ist->outputs[oidx];
                OutputFile    *of = output_files[ost->file_index];
                close_output_stream(ost);
                of_output_packet(of, ost->pkt, ost, 1);
            }
        }

        ifile->eof_reached = 1;
        return AVERROR(EAGAIN);
    }

    reset_eagain();

    ist = ifile->streams[pkt->stream_index];

    ist->data_size += pkt->size;
    ist->nb_packets++;

    if (ist->discard)
        goto discard_packet;

    /* add the stream-global side data to the first packet */
    if (ist->nb_packets == 1) {
        for (i = 0; i < ist->st->nb_side_data; i++) {
            AVPacketSideData *src_sd = &ist->st->side_data[i];
            uint8_t *dst_data;

            if (src_sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                continue;

            if (av_packet_get_side_data(pkt, src_sd->type, NULL))
                continue;

            dst_data = av_packet_new_side_data(pkt, src_sd->type, src_sd->size);
            if (!dst_data)
                report_and_exit(AVERROR(ENOMEM));

            memcpy(dst_data, src_sd->data, src_sd->size);
        }
    }

    // detect and try to correct for timestamp discontinuities
    ts_discontinuity_process(ifile, ist, pkt);

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s off:%s off_time:%s\n",
               ifile->index, pkt->stream_index,
               av_get_media_type_string(ist->par->codec_type),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &pkt->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &pkt->time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &pkt->time_base),
               av_ts2str(input_files[ist->file_index]->ts_offset),
               av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    }

    sub2video_heartbeat(ist, pkt->pts);

    process_input_packet(ist, pkt, 0);

discard_packet:
    av_packet_free(&pkt);

    return 0;
}

/**
 * Run a single step of transcoding.
 *
 * @return  0 for success, <0 for error
 */
static int transcode_step(void)
{
    OutputStream *ost;
    InputStream  *ist = NULL;
    int ret;

    ost = choose_output();
    if (!ost) {
        if (got_eagain()) {
            reset_eagain();
            av_usleep(10000);
            return 0;
        }
        av_log(NULL, AV_LOG_VERBOSE, "No more inputs to read from, finishing.\n");
        return AVERROR_EOF;
    }

    if (ost->filter && !ost->filter->graph->graph) {
        if (ifilter_has_all_input_formats(ost->filter->graph)) {
            ret = configure_filtergraph(ost->filter->graph);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error reinitializing filters!\n");
                return ret;
            }
        }
    }

    if (ost->filter && ost->filter->graph->graph) {
        if ((ret = fg_transcode_step(ost->filter->graph, &ist)) < 0)
            return ret;
        if (!ist)
            return 0;
    } else if (ost->filter) {
        int i;
        for (i = 0; i < ost->filter->graph->nb_inputs; i++) {
            InputFilter *ifilter = ost->filter->graph->inputs[i];
            if (!ifilter->ist->got_output && !input_files[ifilter->ist->file_index]->eof_reached) {
                ist = ifilter->ist;
                break;
            }
        }
        if (!ist) {
            ost->inputs_done = 1;
            return 0;
        }
    } else {
        ist = ost->ist;
        av_assert0(ist);
    }

    ret = process_input(ist->file_index);
    if (ret == AVERROR(EAGAIN)) {
        if (input_files[ist->file_index]->eagain)
            ost->unavailable = 1;
        return 0;
    }

    if (ret < 0)
        return ret == AVERROR_EOF ? 0 : ret;

    return reap_filters(0);
}

/*
 * The following code is the main loop of the file converter
 */
static int transcode(void)
{
    int ret, i;
    InputStream *ist;
    int64_t timer_start;

    ret = transcode_init();
    if (ret < 0)
        return ret;

    if (stdin_interaction) {
        av_log(NULL, AV_LOG_INFO, "Press [q] to stop, [?] for help\n");
    }

    timer_start = av_gettime_relative();

    while (!received_sigterm) {
        int64_t cur_time= av_gettime_relative();

        /* if 'q' pressed, exits */
        if (stdin_interaction)
            if (check_keyboard_interaction(cur_time) < 0)
                break;

        /* check if there's any stream where output is still needed */
        if (!need_output()) {
            av_log(NULL, AV_LOG_VERBOSE, "No more output streams to write to, finishing.\n");
            break;
        }

        ret = transcode_step();
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(NULL, AV_LOG_ERROR, "Error while filtering: %s\n", av_err2str(ret));
            break;
        }

        /* dump report by using the output first video and audio streams */
        print_report(0, timer_start, cur_time);
    }

    /* at the end of stream, we must flush the decoder buffers */
    for (ist = ist_iter(NULL); ist; ist = ist_iter(ist)) {
        if (!input_files[ist->file_index]->eof_reached) {
            process_input_packet(ist, NULL, 0);
        }
    }
    enc_flush();

    term_exit();

    /* write the trailer if needed */
    for (i = 0; i < nb_output_files; i++) {
        int err = of_write_trailer(output_files[i]);
        ret = err_merge(ret, err);
    }

    /* dump report by using the first video and audio streams */
    print_report(1, timer_start, av_gettime_relative());

    return ret;
}

static BenchmarkTimeStamps get_benchmark_time_stamps(void)
{
    BenchmarkTimeStamps time_stamps = { av_gettime_relative() };
#if HAVE_GETRUSAGE
    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    time_stamps.user_usec =
        (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
    time_stamps.sys_usec =
        (rusage.ru_stime.tv_sec * 1000000LL) + rusage.ru_stime.tv_usec;
#elif HAVE_GETPROCESSTIMES
    HANDLE proc;
    FILETIME c, e, k, u;
    proc = GetCurrentProcess();
    GetProcessTimes(proc, &c, &e, &k, &u);
    time_stamps.user_usec =
        ((int64_t)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10;
    time_stamps.sys_usec =
        ((int64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) / 10;
#else
    time_stamps.user_usec = time_stamps.sys_usec = 0;
#endif
    return time_stamps;
}

static int64_t getmaxrss(void)
{
#if HAVE_GETRUSAGE && HAVE_STRUCT_RUSAGE_RU_MAXRSS
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
    return (int64_t)rusage.ru_maxrss * 1024;
#elif HAVE_GETPROCESSMEMORYINFO
    HANDLE proc;
    PROCESS_MEMORY_COUNTERS memcounters;
    proc = GetCurrentProcess();
    memcounters.cb = sizeof(memcounters);
    GetProcessMemoryInfo(proc, &memcounters, sizeof(memcounters));
    return memcounters.PeakPagefileUsage;
#else
    return 0;
#endif
}

int main(int argc, char **argv)
{
    int ret;
    BenchmarkTimeStamps ti;

    init_dynload();

    register_exit(ffmpeg_cleanup);

    setvbuf(stderr,NULL,_IONBF,0); /* win32 runtime needs this */

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    show_banner(argc, argv, options);

    /* parse options and open all input/output files */
    ret = ffmpeg_parse_options(argc, argv);
    if (ret < 0)
        exit_program(1);

    if (nb_output_files <= 0 && nb_input_files == 0) {
        show_usage();
        av_log(NULL, AV_LOG_WARNING, "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit_program(1);
    }

    /* file converter / grab */
    if (nb_output_files <= 0) {
        av_log(NULL, AV_LOG_FATAL, "At least one output file must be specified\n");
        exit_program(1);
    }

    current_time = ti = get_benchmark_time_stamps();
    ret = transcode();
    if (ret >= 0 && do_benchmark) {
        int64_t utime, stime, rtime;
        current_time = get_benchmark_time_stamps();
        utime = current_time.user_usec - ti.user_usec;
        stime = current_time.sys_usec  - ti.sys_usec;
        rtime = current_time.real_usec - ti.real_usec;
        av_log(NULL, AV_LOG_INFO,
               "bench: utime=%0.3fs stime=%0.3fs rtime=%0.3fs\n",
               utime / 1000000.0, stime / 1000000.0, rtime / 1000000.0);
    }
    av_log(NULL, AV_LOG_DEBUG, "%"PRIu64" frames successfully decoded, %"PRIu64" decoding errors\n",
           decode_error_stat[0], decode_error_stat[1]);
    if ((decode_error_stat[0] + decode_error_stat[1]) * max_error_rate < decode_error_stat[1])
        exit_program(69);

    ret = received_nb_signals ? 255 : ret;
    exit_program(ret);
    return ret;
}
