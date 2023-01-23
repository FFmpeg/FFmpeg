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
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>

#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/libm.h"
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libavutil/bprint.h"
#include "libavutil/time.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"
#include "libavcodec/mathops.h"
#include "libavcodec/version.h"
#include "libavformat/os_support.h"

# include "libavfilter/avfilter.h"
# include "libavfilter/buffersrc.h"
# include "libavfilter/buffersink.h"

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

#include <time.h>

#include "ffmpeg.h"
#include "cmdutils.h"
#include "sync_queue.h"

#include "libavutil/avassert.h"

const char program_name[] = "ffmpeg";
const int program_birth_year = 2000;

static FILE *vstats_file;

// optionally attached as opaque_ref to decoded AVFrames
typedef struct FrameData {
    uint64_t   idx;
    int64_t    pts;
    AVRational tb;
} FrameData;

typedef struct BenchmarkTimeStamps {
    int64_t real_usec;
    int64_t user_usec;
    int64_t sys_usec;
} BenchmarkTimeStamps;

static int trigger_fix_sub_duration_heartbeat(OutputStream *ost, const AVPacket *pkt);
static BenchmarkTimeStamps get_benchmark_time_stamps(void);
static int64_t getmaxrss(void);
static int ifilter_has_all_input_formats(FilterGraph *fg);

static int64_t nb_frames_dup = 0;
static uint64_t dup_warning = 1000;
static int64_t nb_frames_drop = 0;
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
int main_return_code = 0;
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
    int i, j;

    if (do_benchmark) {
        int maxrss = getmaxrss() / 1024;
        av_log(NULL, AV_LOG_INFO, "bench: maxrss=%ikB\n", maxrss);
    }

    for (i = 0; i < nb_filtergraphs; i++) {
        FilterGraph *fg = filtergraphs[i];
        avfilter_graph_free(&fg->graph);
        for (j = 0; j < fg->nb_inputs; j++) {
            InputFilter *ifilter = fg->inputs[j];
            struct InputStream *ist = ifilter->ist;

            if (ifilter->frame_queue) {
                AVFrame *frame;
                while (av_fifo_read(ifilter->frame_queue, &frame, 1) >= 0)
                    av_frame_free(&frame);
                av_fifo_freep2(&ifilter->frame_queue);
            }
            av_freep(&ifilter->displaymatrix);
            if (ist->sub2video.sub_queue) {
                AVSubtitle sub;
                while (av_fifo_read(ist->sub2video.sub_queue, &sub, 1) >= 0)
                    avsubtitle_free(&sub);
                av_fifo_freep2(&ist->sub2video.sub_queue);
            }
            av_buffer_unref(&ifilter->hw_frames_ctx);
            av_freep(&ifilter->name);
            av_freep(&fg->inputs[j]);
        }
        av_freep(&fg->inputs);
        for (j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *ofilter = fg->outputs[j];

            avfilter_inout_free(&ofilter->out_tmp);
            av_freep(&ofilter->name);
            av_channel_layout_uninit(&ofilter->ch_layout);
            av_freep(&fg->outputs[j]);
        }
        av_freep(&fg->outputs);
        av_freep(&fg->graph_desc);

        av_freep(&filtergraphs[i]);
    }
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

/* iterate over all output streams in all output files;
 * pass NULL to start iteration */
static OutputStream *ost_iter(OutputStream *prev)
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

static void abort_codec_experimental(const AVCodec *c, int encoder)
{
    exit_program(1);
}

static void update_benchmark(const char *fmt, ...)
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

static void close_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    ost->finished |= ENCODER_FINISHED;

    if (ost->sq_idx_encode >= 0)
        sq_send(of->sq_encode, ost->sq_idx_encode, SQFRAME(NULL));
}

static int check_recording_time(OutputStream *ost, int64_t ts, AVRational tb)
{
    OutputFile *of = output_files[ost->file_index];

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ts, tb, of->recording_time, AV_TIME_BASE_Q) >= 0) {
        close_output_stream(ost);
        return 0;
    }
    return 1;
}

static double adjust_frame_pts_to_encoder_tb(OutputFile *of, OutputStream *ost,
                                             AVFrame *frame)
{
    double float_pts = AV_NOPTS_VALUE; // this is identical to frame.pts but with higher precision
    const int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ?
                               0 : of->start_time;

    AVCodecContext *const enc = ost->enc_ctx;

    AVRational        tb = enc->time_base;
    AVRational filter_tb = frame->time_base;
    const int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

    if (frame->pts == AV_NOPTS_VALUE)
        goto early_exit;

    tb.den <<= extra_bits;
    float_pts = av_rescale_q(frame->pts, filter_tb, tb) -
                av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
    float_pts /= 1 << extra_bits;
    // avoid exact midoints to reduce the chance of rounding differences, this
    // can be removed in case the fps code is changed to work with integers
    float_pts += FFSIGN(float_pts) * 1.0 / (1<<17);

    frame->pts = av_rescale_q(frame->pts, filter_tb, enc->time_base) -
                 av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
    frame->time_base = enc->time_base;

early_exit:

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
               frame ? av_ts2str(frame->pts) : "NULL",
               (enc && frame) ? av_ts2timestr(frame->pts, &enc->time_base) : "NULL",
               float_pts,
               enc ? enc->time_base.num : -1,
               enc ? enc->time_base.den : -1);
    }

    return float_pts;
}

static int init_output_stream(OutputStream *ost, AVFrame *frame,
                              char *error, int error_len);

static int init_output_stream_wrapper(OutputStream *ost, AVFrame *frame,
                                      unsigned int fatal)
{
    int ret = AVERROR_BUG;
    char error[1024] = {0};

    if (ost->initialized)
        return 0;

    ret = init_output_stream(ost, frame, error, sizeof(error));
    if (ret < 0) {
        av_log(ost, AV_LOG_ERROR, "Error initializing output stream: %s\n",
               error);

        if (fatal)
            exit_program(1);
    }

    return ret;
}

static double psnr(double d)
{
    return -10.0 * log10(d);
}

static void update_video_stats(OutputStream *ost, const AVPacket *pkt, int write_vstats)
{
    const uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                                NULL);
    AVCodecContext *enc = ost->enc_ctx;
    int64_t frame_number;
    double ti1, bitrate, avg_bitrate;

    ost->quality   = sd ? AV_RL32(sd) : -1;
    ost->pict_type = sd ? sd[4] : AV_PICTURE_TYPE_NONE;

    for (int i = 0; i<FF_ARRAY_ELEMS(ost->error); i++) {
        if (sd && i < sd[5])
            ost->error[i] = AV_RL64(sd + 8 + 8*i);
        else
            ost->error[i] = -1;
    }

    if (!write_vstats)
        return;

    /* this is executed just the first time update_video_stats is called */
    if (!vstats_file) {
        vstats_file = fopen(vstats_filename, "w");
        if (!vstats_file) {
            perror("fopen");
            exit_program(1);
        }
    }

    frame_number = ost->packets_encoded;
    if (vstats_version <= 1) {
        fprintf(vstats_file, "frame= %5"PRId64" q= %2.1f ", frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    } else  {
        fprintf(vstats_file, "out= %2d st= %2d frame= %5"PRId64" q= %2.1f ", ost->file_index, ost->index, frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    }

    if (ost->error[0]>=0 && (enc->flags & AV_CODEC_FLAG_PSNR))
        fprintf(vstats_file, "PSNR= %6.2f ", psnr(ost->error[0] / (enc->width * enc->height * 255.0 * 255.0)));

    fprintf(vstats_file,"f_size= %6d ", pkt->size);
    /* compute pts value */
    ti1 = pkt->dts * av_q2d(pkt->time_base);
    if (ti1 < 0.01)
        ti1 = 0.01;

    bitrate     = (pkt->size * 8) / av_q2d(enc->time_base) / 1000.0;
    avg_bitrate = (double)(ost->data_size_enc * 8) / ti1 / 1000.0;
    fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
           (double)ost->data_size_enc / 1024, ti1, bitrate, avg_bitrate);
    fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(ost->pict_type));
}

void enc_stats_write(OutputStream *ost, EncStats *es,
                     const AVFrame *frame, const AVPacket *pkt,
                     uint64_t frame_num)
{
    AVIOContext *io = es->io;
    AVRational   tb = frame ? frame->time_base : pkt->time_base;
    int64_t     pts = frame ? frame->pts : pkt->pts;

    AVRational  tbi = (AVRational){ 0, 1};
    int64_t    ptsi = INT64_MAX;

    const FrameData *fd;

    if ((frame && frame->opaque_ref) || (pkt && pkt->opaque_ref)) {
        fd   = (const FrameData*)(frame ? frame->opaque_ref->data : pkt->opaque_ref->data);
        tbi  = fd->tb;
        ptsi = fd->pts;
    }

    for (size_t i = 0; i < es->nb_components; i++) {
        const EncStatsComponent *c = &es->components[i];

        switch (c->type) {
        case ENC_STATS_LITERAL:         avio_write (io, c->str,     c->str_len);                    continue;
        case ENC_STATS_FILE_IDX:        avio_printf(io, "%d",       ost->file_index);               continue;
        case ENC_STATS_STREAM_IDX:      avio_printf(io, "%d",       ost->index);                    continue;
        case ENC_STATS_TIMEBASE:        avio_printf(io, "%d/%d",    tb.num, tb.den);                continue;
        case ENC_STATS_TIMEBASE_IN:     avio_printf(io, "%d/%d",    tbi.num, tbi.den);              continue;
        case ENC_STATS_PTS:             avio_printf(io, "%"PRId64,  pts);                           continue;
        case ENC_STATS_PTS_IN:          avio_printf(io, "%"PRId64,  ptsi);                          continue;
        case ENC_STATS_PTS_TIME:        avio_printf(io, "%g",       pts * av_q2d(tb));              continue;
        case ENC_STATS_PTS_TIME_IN:     avio_printf(io, "%g",       ptsi == INT64_MAX ?
                                                                    INFINITY : ptsi * av_q2d(tbi)); continue;
        case ENC_STATS_FRAME_NUM:       avio_printf(io, "%"PRIu64,  frame_num);                     continue;
        case ENC_STATS_FRAME_NUM_IN:    avio_printf(io, "%"PRIu64,  fd ? fd->idx : -1);             continue;
        }

        if (frame) {
            switch (c->type) {
            case ENC_STATS_SAMPLE_NUM:  avio_printf(io, "%"PRIu64,  ost->samples_encoded);          continue;
            case ENC_STATS_NB_SAMPLES:  avio_printf(io, "%d",       frame->nb_samples);             continue;
            default: av_assert0(0);
            }
        } else {
            switch (c->type) {
            case ENC_STATS_DTS:         avio_printf(io, "%"PRId64,  pkt->dts);                      continue;
            case ENC_STATS_DTS_TIME:    avio_printf(io, "%g",       pkt->dts * av_q2d(tb));         continue;
            case ENC_STATS_PKT_SIZE:    avio_printf(io, "%d",       pkt->size);                     continue;
            case ENC_STATS_BITRATE: {
                double duration = FFMAX(pkt->duration, 1) * av_q2d(tb);
                avio_printf(io, "%g",  8.0 * pkt->size / duration);
                continue;
            }
            case ENC_STATS_AVG_BITRATE: {
                double duration = pkt->dts * av_q2d(tb);
                avio_printf(io, "%g",  duration > 0 ? 8.0 * ost->data_size_enc / duration : -1.);
                continue;
            }
            default: av_assert0(0);
            }
        }
    }
    avio_w8(io, '\n');
    avio_flush(io);
}

static int encode_frame(OutputFile *of, OutputStream *ost, AVFrame *frame)
{
    AVCodecContext   *enc = ost->enc_ctx;
    AVPacket         *pkt = ost->pkt;
    const char *type_desc = av_get_media_type_string(enc->codec_type);
    const char    *action = frame ? "encode" : "flush";
    int ret;

    if (frame) {
        if (ost->enc_stats_pre.io)
            enc_stats_write(ost, &ost->enc_stats_pre, frame, NULL,
                            ost->frames_encoded);

        ost->frames_encoded++;
        ost->samples_encoded += frame->nb_samples;

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder <- type:%s "
                   "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
                   type_desc,
                   av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
                   enc->time_base.num, enc->time_base.den);
        }
    }

    update_benchmark(NULL);

    ret = avcodec_send_frame(enc, frame);
    if (ret < 0 && !(ret == AVERROR_EOF && !frame)) {
        av_log(ost, AV_LOG_ERROR, "Error submitting %s frame to the encoder\n",
               type_desc);
        return ret;
    }

    while (1) {
        ret = avcodec_receive_packet(enc, pkt);
        update_benchmark("%s_%s %d.%d", action, type_desc,
                         ost->file_index, ost->index);

        pkt->time_base = enc->time_base;

        /* if two pass, output log on success and EOF */
        if ((ret >= 0 || ret == AVERROR_EOF) && ost->logfile && enc->stats_out)
            fprintf(ost->logfile, "%s", enc->stats_out);

        if (ret == AVERROR(EAGAIN)) {
            av_assert0(frame); // should never happen during flushing
            return 0;
        } else if (ret == AVERROR_EOF) {
            of_output_packet(of, pkt, ost, 1);
            return ret;
        } else if (ret < 0) {
            av_log(ost, AV_LOG_ERROR, "%s encoding failed\n", type_desc);
            return ret;
        }

        if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
            update_video_stats(ost, pkt, !!vstats_filename);
        if (ost->enc_stats_post.io)
            enc_stats_write(ost, &ost->enc_stats_post, NULL, pkt,
                            ost->packets_encoded);

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder -> type:%s "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s "
                   "duration:%s duration_time:%s\n",
                   type_desc,
                   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base),
                   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &enc->time_base));
        }

        av_packet_rescale_ts(pkt, pkt->time_base, ost->mux_timebase);
        pkt->time_base = ost->mux_timebase;

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder -> type:%s "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s "
                   "duration:%s duration_time:%s\n",
                   type_desc,
                   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base),
                   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &enc->time_base));
        }

        if ((ret = trigger_fix_sub_duration_heartbeat(ost, pkt)) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Subtitle heartbeat logic failed in %s! (%s)\n",
                   __func__, av_err2str(ret));
            exit_program(1);
        }

        ost->data_size_enc += pkt->size;

        ost->packets_encoded++;

        of_output_packet(of, pkt, ost, 0);
    }

    av_assert0(0);
}

static int submit_encode_frame(OutputFile *of, OutputStream *ost,
                               AVFrame *frame)
{
    int ret;

    if (ost->sq_idx_encode < 0)
        return encode_frame(of, ost, frame);

    if (frame) {
        ret = av_frame_ref(ost->sq_frame, frame);
        if (ret < 0)
            return ret;
        frame = ost->sq_frame;
    }

    ret = sq_send(of->sq_encode, ost->sq_idx_encode,
                  SQFRAME(frame));
    if (ret < 0) {
        if (frame)
            av_frame_unref(frame);
        if (ret != AVERROR_EOF)
            return ret;
    }

    while (1) {
        AVFrame *enc_frame = ost->sq_frame;

        ret = sq_receive(of->sq_encode, ost->sq_idx_encode,
                               SQFRAME(enc_frame));
        if (ret == AVERROR_EOF) {
            enc_frame = NULL;
        } else if (ret < 0) {
            return (ret == AVERROR(EAGAIN)) ? 0 : ret;
        }

        ret = encode_frame(of, ost, enc_frame);
        if (enc_frame)
            av_frame_unref(enc_frame);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                close_output_stream(ost);
            return ret;
        }
    }
}

static void do_audio_out(OutputFile *of, OutputStream *ost,
                         AVFrame *frame)
{
    AVCodecContext *enc = ost->enc_ctx;
    int ret;

    if (frame->pts == AV_NOPTS_VALUE)
        frame->pts = ost->next_pts;
    else {
        int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
        frame->pts =
            av_rescale_q(frame->pts, frame->time_base, enc->time_base) -
            av_rescale_q(start_time, AV_TIME_BASE_Q,   enc->time_base);
    }
    frame->time_base = enc->time_base;

    if (!check_recording_time(ost, frame->pts, frame->time_base))
        return;

    ost->next_pts = frame->pts + frame->nb_samples;

    ret = submit_encode_frame(of, ost, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        exit_program(1);
}

static void do_subtitle_out(OutputFile *of,
                            OutputStream *ost,
                            AVSubtitle *sub)
{
    int subtitle_out_max_size = 1024 * 1024;
    int subtitle_out_size, nb, i, ret;
    AVCodecContext *enc;
    AVPacket *pkt = ost->pkt;
    int64_t pts;

    if (sub->pts == AV_NOPTS_VALUE) {
        av_log(ost, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        if (exit_on_error)
            exit_program(1);
        return;
    }

    enc = ost->enc_ctx;

    /* Note: DVB subtitle need one packet to draw them and one other
       packet to clear them */
    /* XXX: signal it in the codec context ? */
    if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        nb = 2;
    else
        nb = 1;

    /* shift timestamp to honor -ss and make check_recording_time() work with -t */
    pts = sub->pts;
    if (output_files[ost->file_index]->start_time != AV_NOPTS_VALUE)
        pts -= output_files[ost->file_index]->start_time;
    for (i = 0; i < nb; i++) {
        unsigned save_num_rects = sub->num_rects;

        if (!check_recording_time(ost, pts, AV_TIME_BASE_Q))
            return;

        ret = av_new_packet(pkt, subtitle_out_max_size);
        if (ret < 0)
            report_and_exit(AVERROR(ENOMEM));

        sub->pts = pts;
        // start_display_time is required to be 0
        sub->pts               += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, AV_TIME_BASE_Q);
        sub->end_display_time  -= sub->start_display_time;
        sub->start_display_time = 0;
        if (i == 1)
            sub->num_rects = 0;

        ost->frames_encoded++;

        subtitle_out_size = avcodec_encode_subtitle(enc, pkt->data, pkt->size, sub);
        if (i == 1)
            sub->num_rects = save_num_rects;
        if (subtitle_out_size < 0) {
            av_log(ost, AV_LOG_FATAL, "Subtitle encoding failed\n");
            exit_program(1);
        }

        av_shrink_packet(pkt, subtitle_out_size);
        pkt->time_base = ost->mux_timebase;
        pkt->pts  = av_rescale_q(sub->pts, AV_TIME_BASE_Q, pkt->time_base);
        pkt->duration = av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt->pts += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
            else
                pkt->pts += av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
        }
        pkt->dts = pkt->pts;

        of_output_packet(of, pkt, ost, 0);
    }
}

/* Convert frame timestamps to the encoder timebase and decide how many times
 * should this (and possibly previous) frame be repeated in order to conform to
 * desired target framerate (if any).
 */
static void video_sync_process(OutputFile *of, OutputStream *ost,
                               AVFrame *next_picture, double duration,
                               int64_t *nb_frames, int64_t *nb_frames_prev)
{
    double delta0, delta;

    double sync_ipts = adjust_frame_pts_to_encoder_tb(of, ost, next_picture);
    /* delta0 is the "drift" between the input frame (next_picture) and
     * where it would fall in the output. */
    delta0 = sync_ipts - ost->next_pts;
    delta  = delta0 + duration;

    // tracks the number of times the PREVIOUS frame should be duplicated,
    // mostly for variable framerate (VFR)
    *nb_frames_prev = 0;
    /* by default, we output a single frame */
    *nb_frames = 1;

    if (delta0 < 0 &&
        delta > 0 &&
        ost->vsync_method != VSYNC_PASSTHROUGH &&
        ost->vsync_method != VSYNC_DROP) {
        if (delta0 < -0.6) {
            av_log(ost, AV_LOG_VERBOSE, "Past duration %f too large\n", -delta0);
        } else
            av_log(ost, AV_LOG_DEBUG, "Clipping frame in rate conversion by %f\n", -delta0);
        sync_ipts = ost->next_pts;
        duration += delta0;
        delta0 = 0;
    }

    switch (ost->vsync_method) {
    case VSYNC_VSCFR:
        if (ost->vsync_frame_number == 0 && delta0 >= 0.5) {
            av_log(ost, AV_LOG_DEBUG, "Not duplicating %d initial frames\n", (int)lrintf(delta0));
            delta = duration;
            delta0 = 0;
            ost->next_pts = llrint(sync_ipts);
        }
    case VSYNC_CFR:
        // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (frame_drop_threshold && delta < frame_drop_threshold && ost->vsync_frame_number) {
            *nb_frames = 0;
        } else if (delta < -1.1)
            *nb_frames = 0;
        else if (delta > 1.1) {
            *nb_frames = llrintf(delta);
            if (delta0 > 1.1)
                *nb_frames_prev = llrintf(delta0 - 0.6);
        }
        next_picture->duration = 1;
        break;
    case VSYNC_VFR:
        if (delta <= -0.6)
            *nb_frames = 0;
        else if (delta > 0.6)
            ost->next_pts = llrint(sync_ipts);
        next_picture->duration = duration;
        break;
    case VSYNC_DROP:
    case VSYNC_PASSTHROUGH:
        next_picture->duration = duration;
        ost->next_pts = llrint(sync_ipts);
        break;
    default:
        av_assert0(0);
    }
}

static enum AVPictureType forced_kf_apply(void *logctx, KeyframeForceCtx *kf,
                                          AVRational tb, const AVFrame *in_picture,
                                          int dup_idx)
{
    double pts_time;

    if (kf->ref_pts == AV_NOPTS_VALUE)
        kf->ref_pts = in_picture->pts;

    pts_time = (in_picture->pts - kf->ref_pts) * av_q2d(tb);
    if (kf->index < kf->nb_pts &&
        av_compare_ts(in_picture->pts, tb, kf->pts[kf->index], AV_TIME_BASE_Q) >= 0) {
        kf->index++;
        goto force_keyframe;
    } else if (kf->pexpr) {
        double res;
        kf->expr_const_values[FKF_T] = pts_time;
        res = av_expr_eval(kf->pexpr,
                           kf->expr_const_values, NULL);
        ff_dlog(NULL, "force_key_frame: n:%f n_forced:%f prev_forced_n:%f t:%f prev_forced_t:%f -> res:%f\n",
                kf->expr_const_values[FKF_N],
                kf->expr_const_values[FKF_N_FORCED],
                kf->expr_const_values[FKF_PREV_FORCED_N],
                kf->expr_const_values[FKF_T],
                kf->expr_const_values[FKF_PREV_FORCED_T],
                res);

        kf->expr_const_values[FKF_N] += 1;

        if (res) {
            kf->expr_const_values[FKF_PREV_FORCED_N] = kf->expr_const_values[FKF_N] - 1;
            kf->expr_const_values[FKF_PREV_FORCED_T] = kf->expr_const_values[FKF_T];
            kf->expr_const_values[FKF_N_FORCED]     += 1;
            goto force_keyframe;
        }
    } else if (kf->type == KF_FORCE_SOURCE &&
               in_picture->key_frame == 1 && !dup_idx) {
            goto force_keyframe;
    } else if (kf->type == KF_FORCE_SOURCE_NO_DROP && !dup_idx) {
        kf->dropped_keyframe = 0;
        if ((in_picture->key_frame == 1) || kf->dropped_keyframe)
            goto force_keyframe;
    }

    return AV_PICTURE_TYPE_NONE;

force_keyframe:
    av_log(logctx, AV_LOG_DEBUG, "Forced keyframe at time %f\n", pts_time);
    return AV_PICTURE_TYPE_I;
}

/* May modify/reset next_picture */
static void do_video_out(OutputFile *of,
                         OutputStream *ost,
                         AVFrame *next_picture)
{
    int ret;
    AVCodecContext *enc = ost->enc_ctx;
    AVRational frame_rate;
    int64_t nb_frames, nb_frames_prev, i;
    double duration = 0;
    InputStream *ist = ost->ist;
    AVFilterContext *filter = ost->filter->filter;

    init_output_stream_wrapper(ost, next_picture, 1);

    frame_rate = av_buffersink_get_frame_rate(filter);
    if (frame_rate.num > 0 && frame_rate.den > 0)
        duration = 1/(av_q2d(frame_rate) * av_q2d(enc->time_base));

    if(ist && ist->st->start_time != AV_NOPTS_VALUE && ist->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
        duration = FFMIN(duration, 1/(av_q2d(ost->frame_rate) * av_q2d(enc->time_base)));

    if (!ost->filters_script &&
        !ost->filters &&
        (nb_filtergraphs == 0 || !filtergraphs[0]->graph_desc) &&
        next_picture &&
        ist &&
        lrintf(next_picture->duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base)) > 0) {
        duration = lrintf(next_picture->duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base));
    }

    if (!next_picture) {
        //end, flushing
        nb_frames_prev = nb_frames = mid_pred(ost->last_nb0_frames[0],
                                              ost->last_nb0_frames[1],
                                              ost->last_nb0_frames[2]);
    } else {
        video_sync_process(of, ost, next_picture, duration,
                           &nb_frames, &nb_frames_prev);
    }

    memmove(ost->last_nb0_frames + 1,
            ost->last_nb0_frames,
            sizeof(ost->last_nb0_frames[0]) * (FF_ARRAY_ELEMS(ost->last_nb0_frames) - 1));
    ost->last_nb0_frames[0] = nb_frames_prev;

    if (nb_frames_prev == 0 && ost->last_dropped) {
        nb_frames_drop++;
        av_log(ost, AV_LOG_VERBOSE,
               "*** dropping frame %"PRId64" at ts %"PRId64"\n",
               ost->vsync_frame_number, ost->last_frame->pts);
    }
    if (nb_frames > (nb_frames_prev && ost->last_dropped) + (nb_frames > nb_frames_prev)) {
        if (nb_frames > dts_error_threshold * 30) {
            av_log(ost, AV_LOG_ERROR, "%"PRId64" frame duplication too large, skipping\n", nb_frames - 1);
            nb_frames_drop++;
            return;
        }
        nb_frames_dup += nb_frames - (nb_frames_prev && ost->last_dropped) - (nb_frames > nb_frames_prev);
        av_log(ost, AV_LOG_VERBOSE, "*** %"PRId64" dup!\n", nb_frames - 1);
        if (nb_frames_dup > dup_warning) {
            av_log(ost, AV_LOG_WARNING, "More than %"PRIu64" frames duplicated\n", dup_warning);
            dup_warning *= 10;
        }
    }
    ost->last_dropped = nb_frames == nb_frames_prev && next_picture;
    ost->kf.dropped_keyframe = ost->last_dropped && next_picture && next_picture->key_frame;

    /* duplicates frame if needed */
    for (i = 0; i < nb_frames; i++) {
        AVFrame *in_picture;

        if (i < nb_frames_prev && ost->last_frame->buf[0]) {
            in_picture = ost->last_frame;
        } else
            in_picture = next_picture;

        if (!in_picture)
            return;

        in_picture->pts = ost->next_pts;

        if (!check_recording_time(ost, in_picture->pts, ost->enc_ctx->time_base))
            return;

        in_picture->quality = enc->global_quality;
        in_picture->pict_type = forced_kf_apply(ost, &ost->kf, enc->time_base, in_picture, i);

        ret = submit_encode_frame(of, ost, in_picture);
        if (ret == AVERROR_EOF)
            break;
        else if (ret < 0)
            exit_program(1);

        ost->next_pts++;
        ost->vsync_frame_number++;
    }

    av_frame_unref(ost->last_frame);
    if (next_picture)
        av_frame_move_ref(ost->last_frame, next_picture);
}

/**
 * Get and encode new output from any of the filtergraphs, without causing
 * activity.
 *
 * @return  0 for success, <0 for severe errors
 */
static int reap_filters(int flush)
{
    AVFrame *filtered_frame = NULL;

    /* Reap all buffers present in the buffer sinks */
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        OutputFile    *of = output_files[ost->file_index];
        AVFilterContext *filter;
        AVCodecContext *enc = ost->enc_ctx;
        int ret = 0;

        if (!ost->filter || !ost->filter->graph->graph)
            continue;
        filter = ost->filter->filter;

        /*
         * Unlike video, with audio the audio frame size matters.
         * Currently we are fully reliant on the lavfi filter chain to
         * do the buffering deed for us, and thus the frame size parameter
         * needs to be set accordingly. Where does one get the required
         * frame size? From the initialized AVCodecContext of an audio
         * encoder. Thus, if we have gotten to an audio stream, initialize
         * the encoder earlier than receiving the first AVFrame.
         */
        if (av_buffersink_get_type(filter) == AVMEDIA_TYPE_AUDIO)
            init_output_stream_wrapper(ost, NULL, 1);

        filtered_frame = ost->filtered_frame;

        while (1) {
            ret = av_buffersink_get_frame_flags(filter, filtered_frame,
                                               AV_BUFFERSINK_FLAG_NO_REQUEST);
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                    av_log(NULL, AV_LOG_WARNING,
                           "Error in av_buffersink_get_frame_flags(): %s\n", av_err2str(ret));
                } else if (flush && ret == AVERROR_EOF) {
                    if (av_buffersink_get_type(filter) == AVMEDIA_TYPE_VIDEO)
                        do_video_out(of, ost, NULL);
                }
                break;
            }
            if (ost->finished) {
                av_frame_unref(filtered_frame);
                continue;
            }

            if (filtered_frame->pts != AV_NOPTS_VALUE) {
                AVRational tb = av_buffersink_get_time_base(filter);
                ost->last_filter_pts = av_rescale_q(filtered_frame->pts, tb,
                                                    AV_TIME_BASE_Q);
                filtered_frame->time_base = tb;

                if (debug_ts)
                    av_log(NULL, AV_LOG_INFO, "filter_raw -> pts:%s pts_time:%s time_base:%d/%d\n",
                           av_ts2str(filtered_frame->pts),
                           av_ts2timestr(filtered_frame->pts, &tb),
                           tb.num, tb.den);
            }

            switch (av_buffersink_get_type(filter)) {
            case AVMEDIA_TYPE_VIDEO:
                if (!ost->frame_aspect_ratio.num)
                    enc->sample_aspect_ratio = filtered_frame->sample_aspect_ratio;

                do_video_out(of, ost, filtered_frame);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
                    enc->ch_layout.nb_channels != filtered_frame->ch_layout.nb_channels) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Audio filter graph output is not normalized and encoder does not support parameter changes\n");
                    break;
                }
                do_audio_out(of, ost, filtered_frame);
                break;
            default:
                // TODO support subtitle filters
                av_assert0(0);
            }

            av_frame_unref(filtered_frame);
        }
    }

    return 0;
}

static void print_final_stats(int64_t total_size)
{
    uint64_t video_size = 0, audio_size = 0, extra_size = 0, other_size = 0;
    uint64_t subtitle_size = 0;
    uint64_t data_size = 0;
    float percent = -1.0;
    int i, j;
    int pass1_used = 1;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        AVCodecParameters *par = ost->st->codecpar;
        const uint64_t s = ost->data_size_mux;

        switch (par->codec_type) {
            case AVMEDIA_TYPE_VIDEO:    video_size    += s; break;
            case AVMEDIA_TYPE_AUDIO:    audio_size    += s; break;
            case AVMEDIA_TYPE_SUBTITLE: subtitle_size += s; break;
            default:                    other_size    += s; break;
        }
        extra_size += par->extradata_size;
        data_size  += s;
        if (ost->enc_ctx &&
            (ost->enc_ctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2))
            != AV_CODEC_FLAG_PASS1)
            pass1_used = 0;
    }

    if (data_size && total_size>0 && total_size >= data_size)
        percent = 100.0 * (total_size - data_size) / data_size;

    av_log(NULL, AV_LOG_INFO, "video:%1.0fkB audio:%1.0fkB subtitle:%1.0fkB other streams:%1.0fkB global headers:%1.0fkB muxing overhead: ",
           video_size / 1024.0,
           audio_size / 1024.0,
           subtitle_size / 1024.0,
           other_size / 1024.0,
           extra_size / 1024.0);
    if (percent >= 0.0)
        av_log(NULL, AV_LOG_INFO, "%f%%", percent);
    else
        av_log(NULL, AV_LOG_INFO, "unknown");
    av_log(NULL, AV_LOG_INFO, "\n");

    /* print verbose per-stream stats */
    for (i = 0; i < nb_input_files; i++) {
        InputFile *f = input_files[i];
        uint64_t total_packets = 0, total_size = 0;

        av_log(NULL, AV_LOG_VERBOSE, "Input file #%d (%s):\n",
               i, f->ctx->url);

        for (j = 0; j < f->nb_streams; j++) {
            InputStream *ist = f->streams[j];
            enum AVMediaType type = ist->par->codec_type;

            total_size    += ist->data_size;
            total_packets += ist->nb_packets;

            av_log(NULL, AV_LOG_VERBOSE, "  Input stream #%d:%d (%s): ",
                   i, j, av_get_media_type_string(type));
            av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" packets read (%"PRIu64" bytes); ",
                   ist->nb_packets, ist->data_size);

            if (ist->decoding_needed) {
                av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" frames decoded",
                       ist->frames_decoded);
                if (type == AVMEDIA_TYPE_AUDIO)
                    av_log(NULL, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ist->samples_decoded);
                av_log(NULL, AV_LOG_VERBOSE, "; ");
            }

            av_log(NULL, AV_LOG_VERBOSE, "\n");
        }

        av_log(NULL, AV_LOG_VERBOSE, "  Total: %"PRIu64" packets (%"PRIu64" bytes) demuxed\n",
               total_packets, total_size);
    }

    for (i = 0; i < nb_output_files; i++) {
        OutputFile *of = output_files[i];
        uint64_t total_packets = 0, total_size = 0;

        av_log(NULL, AV_LOG_VERBOSE, "Output file #%d (%s):\n",
               i, of->url);

        for (j = 0; j < of->nb_streams; j++) {
            OutputStream *ost = of->streams[j];
            enum AVMediaType type = ost->st->codecpar->codec_type;

            total_size    += ost->data_size_mux;
            total_packets += atomic_load(&ost->packets_written);

            av_log(NULL, AV_LOG_VERBOSE, "  Output stream #%d:%d (%s): ",
                   i, j, av_get_media_type_string(type));
            if (ost->enc_ctx) {
                av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" frames encoded",
                       ost->frames_encoded);
                if (type == AVMEDIA_TYPE_AUDIO)
                    av_log(NULL, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ost->samples_encoded);
                av_log(NULL, AV_LOG_VERBOSE, "; ");
            }

            av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" packets muxed (%"PRIu64" bytes); ",
                   atomic_load(&ost->packets_written), ost->data_size_mux);

            av_log(NULL, AV_LOG_VERBOSE, "\n");
        }

        av_log(NULL, AV_LOG_VERBOSE, "  Total: %"PRIu64" packets (%"PRIu64" bytes) muxed\n",
               total_packets, total_size);
    }
    if(video_size + data_size + audio_size + subtitle_size + extra_size == 0){
        av_log(NULL, AV_LOG_WARNING, "Output file is empty, nothing was encoded ");
        if (pass1_used) {
            av_log(NULL, AV_LOG_WARNING, "\n");
        } else {
            av_log(NULL, AV_LOG_WARNING, "(check -ss / -t / -frames parameters if used)\n");
        }
    }
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
    static int qp_histogram[52];
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
        const AVCodecContext * const enc = ost->enc_ctx;
        const float q = enc ? ost->quality / (float) FF_QP2LAMBDA : -1;

        if (vid && ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_bprintf(&buf, "q=%2.1f ", q);
            av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                       ost->file_index, ost->index, q);
        }
        if (!vid && ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
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
            if (qp_hist) {
                int j;
                int qp = lrintf(q);
                if (qp >= 0 && qp < FF_ARRAY_ELEMS(qp_histogram))
                    qp_histogram[qp]++;
                for (j = 0; j < 32; j++)
                    av_bprintf(&buf, "%X", av_log2(qp_histogram[j] + 1));
            }

            if (enc && (enc->flags & AV_CODEC_FLAG_PSNR) &&
                (ost->pict_type != AV_PICTURE_TYPE_NONE || is_last_report)) {
                int j;
                double error, error_sum = 0;
                double scale, scale_sum = 0;
                double p;
                char type[3] = { 'Y','U','V' };
                av_bprintf(&buf, "PSNR=");
                for (j = 0; j < 3; j++) {
                    if (is_last_report) {
                        error = enc->error[j];
                        scale = enc->width * enc->height * 255.0 * 255.0 * frame_number;
                    } else {
                        error = ost->error[j];
                        scale = enc->width * enc->height * 255.0 * 255.0;
                    }
                    if (j)
                        scale /= 4;
                    error_sum += error;
                    scale_sum += scale;
                    p = psnr(error / scale);
                    av_bprintf(&buf, "%c:%2.2f ", type[j], p);
                    av_bprintf(&buf_script, "stream_%d_%d_psnr_%c=%2.2f\n",
                               ost->file_index, ost->index, type[j] | 32, p);
                }
                p = psnr(error_sum / scale_sum);
                av_bprintf(&buf, "*:%2.2f ", psnr(error_sum / scale_sum));
                av_bprintf(&buf_script, "stream_%d_%d_psnr_all=%2.2f\n",
                           ost->file_index, ost->index, p);
            }
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

    if (is_last_report)
        print_final_stats(total_size);
}

static int ifilter_parameters_from_codecpar(InputFilter *ifilter, AVCodecParameters *par)
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

static void flush_encoders(void)
{
    int ret;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        OutputFile      *of = output_files[ost->file_index];
        if (ost->sq_idx_encode >= 0)
            sq_send(of->sq_encode, ost->sq_idx_encode, SQFRAME(NULL));
    }

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        AVCodecContext *enc = ost->enc_ctx;
        OutputFile      *of = output_files[ost->file_index];

        if (!enc)
            continue;

        // Try to enable encoding with no input frames.
        // Maybe we should just let encoding fail instead.
        if (!ost->initialized) {
            FilterGraph *fg = ost->filter->graph;

            av_log(ost, AV_LOG_WARNING,
                   "Finishing stream without any data written to it.\n");

            if (ost->filter && !fg->graph) {
                int x;
                for (x = 0; x < fg->nb_inputs; x++) {
                    InputFilter *ifilter = fg->inputs[x];
                    if (ifilter->format < 0 &&
                        ifilter_parameters_from_codecpar(ifilter, ifilter->ist->par) < 0) {
                        av_log(ost, AV_LOG_ERROR, "Error copying paramerets from input stream\n");
                        exit_program(1);
                    }
                }

                if (!ifilter_has_all_input_formats(fg))
                    continue;

                ret = configure_filtergraph(fg);
                if (ret < 0) {
                    av_log(ost, AV_LOG_ERROR, "Error configuring filter graph\n");
                    exit_program(1);
                }

                of_output_packet(of, ost->pkt, ost, 1);
            }

            init_output_stream_wrapper(ost, NULL, 1);
        }

        if (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        ret = submit_encode_frame(of, ost, NULL);
        if (ret != AVERROR_EOF)
            exit_program(1);
    }
}

/*
 * Check whether a packet from ist should be written into ost at this time
 */
static int check_output_constraints(InputStream *ist, OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    if (ost->ist != ist)
        return 0;

    if (ost->finished & MUXER_FINISHED)
        return 0;

    if (of->start_time != AV_NOPTS_VALUE && ist->pts < of->start_time)
        return 0;

    return 1;
}

static void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
{
    OutputFile *of = output_files[ost->file_index];
    InputFile   *f = input_files [ist->file_index];
    int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
    int64_t ost_tb_start_time = av_rescale_q(start_time, AV_TIME_BASE_Q, ost->mux_timebase);
    AVPacket *opkt = ost->pkt;

    av_packet_unref(opkt);
    // EOF: flush output bitstream filters.
    if (!pkt) {
        of_output_packet(of, opkt, ost, 1);
        return;
    }

    if (!ost->streamcopy_started && !(pkt->flags & AV_PKT_FLAG_KEY) &&
        !ost->copy_initial_nonkeyframes)
        return;

    if (!ost->streamcopy_started && !ost->copy_prior_start) {
        if (pkt->pts == AV_NOPTS_VALUE ?
            ist->pts < ost->ts_copy_start :
            pkt->pts < av_rescale_q(ost->ts_copy_start, AV_TIME_BASE_Q, ist->st->time_base))
            return;
    }

    if (of->recording_time != INT64_MAX &&
        ist->pts >= of->recording_time + start_time) {
        close_output_stream(ost);
        return;
    }

    if (f->recording_time != INT64_MAX) {
        start_time = 0;
        if (copy_ts) {
            start_time += f->start_time != AV_NOPTS_VALUE ? f->start_time : 0;
            start_time += start_at_zero ? 0 : f->start_time_effective;
        }
        if (ist->pts >= f->recording_time + start_time) {
            close_output_stream(ost);
            return;
        }
    }

    if (av_packet_ref(opkt, pkt) < 0)
        exit_program(1);

    opkt->time_base = ost->mux_timebase;

    if (pkt->pts != AV_NOPTS_VALUE)
        opkt->pts = av_rescale_q(pkt->pts, ist->st->time_base, opkt->time_base) - ost_tb_start_time;

    if (pkt->dts == AV_NOPTS_VALUE) {
        opkt->dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, opkt->time_base);
    } else if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        int duration = av_get_audio_frame_duration2(ist->par, pkt->size);
        if(!duration)
            duration = ist->par->frame_size;
        opkt->dts = av_rescale_delta(ist->st->time_base, pkt->dts,
                                    (AVRational){1, ist->par->sample_rate}, duration,
                                    &ist->filter_in_rescale_delta_last, opkt->time_base);
        /* dts will be set immediately afterwards to what pts is now */
        opkt->pts = opkt->dts - ost_tb_start_time;
    } else
        opkt->dts = av_rescale_q(pkt->dts, ist->st->time_base, opkt->time_base);
    opkt->dts -= ost_tb_start_time;

    opkt->duration = av_rescale_q(pkt->duration, ist->st->time_base, opkt->time_base);

    {
        int ret = trigger_fix_sub_duration_heartbeat(ost, pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Subtitle heartbeat logic failed in %s! (%s)\n",
                   __func__, av_err2str(ret));
            exit_program(1);
        }
    }

    of_output_packet(of, opkt, ost, 0);

    ost->streamcopy_started = 1;
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
static int ifilter_has_all_input_formats(FilterGraph *fg)
{
    int i;
    for (i = 0; i < fg->nb_inputs; i++) {
        if (fg->inputs[i]->format < 0 && (fg->inputs[i]->type == AVMEDIA_TYPE_AUDIO ||
                                          fg->inputs[i]->type == AVMEDIA_TYPE_VIDEO))
            return 0;
    }
    return 1;
}

static int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame, int keep_reference)
{
    FilterGraph *fg = ifilter->graph;
    AVFrameSideData *sd;
    int need_reinit, ret;
    int buffersrc_flags = AV_BUFFERSRC_FLAG_PUSH;

    if (keep_reference)
        buffersrc_flags |= AV_BUFFERSRC_FLAG_KEEP_REF;

    /* determine if the parameters for this input changed */
    need_reinit = ifilter->format != frame->format;

    switch (ifilter->ist->par->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        need_reinit |= ifilter->sample_rate    != frame->sample_rate ||
                       av_channel_layout_compare(&ifilter->ch_layout, &frame->ch_layout);
        break;
    case AVMEDIA_TYPE_VIDEO:
        need_reinit |= ifilter->width  != frame->width ||
                       ifilter->height != frame->height;
        break;
    }

    if (!ifilter->ist->reinit_filters && fg->graph)
        need_reinit = 0;

    if (!!ifilter->hw_frames_ctx != !!frame->hw_frames_ctx ||
        (ifilter->hw_frames_ctx && ifilter->hw_frames_ctx->data != frame->hw_frames_ctx->data))
        need_reinit = 1;

    if (sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        if (!ifilter->displaymatrix || memcmp(sd->data, ifilter->displaymatrix, sizeof(int32_t) * 9))
            need_reinit = 1;
    } else if (ifilter->displaymatrix)
        need_reinit = 1;

    if (need_reinit) {
        ret = ifilter_parameters_from_frame(ifilter, frame);
        if (ret < 0)
            return ret;
    }

    /* (re)init the graph if possible, otherwise buffer the frame and return */
    if (need_reinit || !fg->graph) {
        if (!ifilter_has_all_input_formats(fg)) {
            AVFrame *tmp = av_frame_clone(frame);
            if (!tmp)
                return AVERROR(ENOMEM);

            ret = av_fifo_write(ifilter->frame_queue, &tmp, 1);
            if (ret < 0)
                av_frame_free(&tmp);

            return ret;
        }

        ret = reap_filters(1);
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(NULL, AV_LOG_ERROR, "Error while filtering: %s\n", av_err2str(ret));
            return ret;
        }

        ret = configure_filtergraph(fg);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error reinitializing filters!\n");
            return ret;
        }
    }

    ret = av_buffersrc_add_frame_flags(ifilter->filter, frame, buffersrc_flags);
    if (ret < 0) {
        if (ret != AVERROR_EOF)
            av_log(NULL, AV_LOG_ERROR, "Error while filtering: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

static int ifilter_send_eof(InputFilter *ifilter, int64_t pts)
{
    int ret;

    ifilter->eof = 1;

    if (ifilter->filter) {
        ret = av_buffersrc_close(ifilter->filter, pts, AV_BUFFERSRC_FLAG_PUSH);
        if (ret < 0)
            return ret;
    } else {
        // the filtergraph was never configured
        if (ifilter->format < 0) {
            ret = ifilter_parameters_from_codecpar(ifilter, ifilter->ist->par);
            if (ret < 0)
                return ret;
        }
        if (ifilter->format < 0 && (ifilter->type == AVMEDIA_TYPE_AUDIO || ifilter->type == AVMEDIA_TYPE_VIDEO)) {
            av_log(NULL, AV_LOG_ERROR, "Cannot determine format of input stream %d:%d after EOF\n", ifilter->ist->file_index, ifilter->ist->st->index);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

// This does not quite work like avcodec_decode_audio4/avcodec_decode_video2.
// There is the following difference: if you got a frame, you must call
// it again with pkt=NULL. pkt==NULL is treated differently from pkt->size==0
// (pkt==NULL means get more output, pkt->size==0 is a flush/drain packet)
static int decode(InputStream *ist, AVCodecContext *avctx,
                  AVFrame *frame, int *got_frame, AVPacket *pkt)
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

static int decode_audio(InputStream *ist, AVPacket *pkt, int *got_output,
                        int *decode_failed)
{
    AVFrame *decoded_frame = ist->decoded_frame;
    AVCodecContext *avctx = ist->dec_ctx;
    int ret, err = 0;
    AVRational decoded_frame_tb;

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

    /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
    ist->next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     decoded_frame->sample_rate;
    ist->next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     decoded_frame->sample_rate;

    if (decoded_frame->pts != AV_NOPTS_VALUE) {
        decoded_frame_tb   = ist->st->time_base;
    } else if (pkt && pkt->pts != AV_NOPTS_VALUE) {
        decoded_frame->pts = pkt->pts;
        decoded_frame_tb   = ist->st->time_base;
    }else {
        decoded_frame->pts = ist->dts;
        decoded_frame_tb   = AV_TIME_BASE_Q;
    }
    if (pkt && pkt->duration && ist->prev_pkt_pts != AV_NOPTS_VALUE &&
        pkt->pts != AV_NOPTS_VALUE && pkt->pts - ist->prev_pkt_pts > pkt->duration)
        ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;
    if (pkt)
        ist->prev_pkt_pts = pkt->pts;
    if (decoded_frame->pts != AV_NOPTS_VALUE)
        decoded_frame->pts = av_rescale_delta(decoded_frame_tb, decoded_frame->pts,
                                              (AVRational){1, decoded_frame->sample_rate},
                                              decoded_frame->nb_samples,
                                              &ist->filter_in_rescale_delta_last,
                                              (AVRational){1, decoded_frame->sample_rate});
    ist->nb_samples = decoded_frame->nb_samples;
    err = send_frame_to_filters(ist, decoded_frame);

    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int decode_video(InputStream *ist, AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof,
                        int *decode_failed)
{
    AVFrame *decoded_frame = ist->decoded_frame;
    int i, ret = 0, err = 0;
    int64_t best_effort_timestamp;
    int64_t dts = AV_NOPTS_VALUE;

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (!eof && pkt && pkt->size == 0)
        return 0;

    if (ist->dts != AV_NOPTS_VALUE)
        dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ist->st->time_base);
    if (pkt) {
        pkt->dts = dts; // ffmpeg.c probably shouldn't do this
    }

    // The old code used to set dts on the drain packet, which does not work
    // with the new API anymore.
    if (eof) {
        void *new = av_realloc_array(ist->dts_buffer, ist->nb_dts_buffer + 1, sizeof(ist->dts_buffer[0]));
        if (!new)
            return AVERROR(ENOMEM);
        ist->dts_buffer = new;
        ist->dts_buffer[ist->nb_dts_buffer++] = dts;
    }

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

    if (eof && best_effort_timestamp == AV_NOPTS_VALUE && ist->nb_dts_buffer > 0) {
        best_effort_timestamp = ist->dts_buffer[0];

        for (i = 0; i < ist->nb_dts_buffer - 1; i++)
            ist->dts_buffer[i] = ist->dts_buffer[i + 1];
        ist->nb_dts_buffer--;
    }

    if(best_effort_timestamp != AV_NOPTS_VALUE) {
        int64_t ts = av_rescale_q(decoded_frame->pts = best_effort_timestamp, ist->st->time_base, AV_TIME_BASE_Q);

        if (ts != AV_NOPTS_VALUE)
            ist->next_pts = ist->pts = ts;
    }

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "decoder -> ist_index:%d type:video "
               "frame_pts:%s frame_pts_time:%s best_effort_ts:%"PRId64" best_effort_ts_time:%s keyframe:%d frame_type:%d time_base:%d/%d\n",
               ist->st->index, av_ts2str(decoded_frame->pts),
               av_ts2timestr(decoded_frame->pts, &ist->st->time_base),
               best_effort_timestamp,
               av_ts2timestr(best_effort_timestamp, &ist->st->time_base),
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

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        if (!check_output_constraints(ist, ost) || !ost->enc_ctx
            || ost->enc_ctx->codec_type != AVMEDIA_TYPE_SUBTITLE)
            continue;

        do_subtitle_out(output_files[ost->file_index], ost, subtitle);
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

static int trigger_fix_sub_duration_heartbeat(OutputStream *ost, const AVPacket *pkt)
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
    /* TODO keep pts also in stream time base to avoid converting back */
    int64_t pts = av_rescale_q_rnd(ist->pts, AV_TIME_BASE_Q, ist->st->time_base,
                                   AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    for (i = 0; i < ist->nb_filters; i++) {
        ret = ifilter_send_eof(ist->filters[i], pts);
        if (ret < 0)
            return ret;
    }
    return 0;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int process_input_packet(InputStream *ist, const AVPacket *pkt, int no_eof)
{
    const AVCodecParameters *par = ist->par;
    int ret = 0;
    int repeating = 0;
    int eof_reached = 0;

    AVPacket *avpkt = ist->pkt;

    if (!ist->saw_first_ts) {
        ist->first_dts =
        ist->dts = ist->st->avg_frame_rate.num ? - ist->dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        ist->pts = 0;
        if (pkt && pkt->pts != AV_NOPTS_VALUE && !ist->decoding_needed) {
            ist->first_dts =
            ist->dts += av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
            ist->pts = ist->dts; //unused but better to set it to a value thats not totally wrong
        }
        ist->saw_first_ts = 1;
    }

    if (ist->next_dts == AV_NOPTS_VALUE)
        ist->next_dts = ist->dts;
    if (ist->next_pts == AV_NOPTS_VALUE)
        ist->next_pts = ist->pts;

    if (pkt) {
        av_packet_unref(avpkt);
        ret = av_packet_ref(avpkt, pkt);
        if (ret < 0)
            return ret;
    }

    if (pkt && pkt->dts != AV_NOPTS_VALUE) {
        ist->next_dts = ist->dts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
        if (par->codec_type != AVMEDIA_TYPE_VIDEO || !ist->decoding_needed)
            ist->next_pts = ist->pts = ist->dts;
    }

    // while we have more to decode or while the decoder did output something on EOF
    while (ist->decoding_needed) {
        int64_t duration_dts = 0;
        int64_t duration_pts = 0;
        int got_output = 0;
        int decode_failed = 0;

        ist->pts = ist->next_pts;
        ist->dts = ist->next_dts;

        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ret = decode_audio    (ist, repeating ? NULL : avpkt, &got_output,
                                   &decode_failed);
            av_packet_unref(avpkt);
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = decode_video    (ist, repeating ? NULL : avpkt, &got_output, &duration_pts, !pkt,
                                   &decode_failed);
            if (!repeating || !pkt || got_output) {
                if (pkt && pkt->duration) {
                    duration_dts = av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
                } else if(ist->dec_ctx->framerate.num != 0 && ist->dec_ctx->framerate.den != 0) {
                    int ticks = ist->last_pkt_repeat_pict >= 0 ?
                                ist->last_pkt_repeat_pict + 1  :
                                ist->dec_ctx->ticks_per_frame;
                    duration_dts = ((int64_t)AV_TIME_BASE *
                                    ist->dec_ctx->framerate.den * ticks) /
                                    ist->dec_ctx->framerate.num / ist->dec_ctx->ticks_per_frame;
                }

                if(ist->dts != AV_NOPTS_VALUE && duration_dts) {
                    ist->next_dts += duration_dts;
                }else
                    ist->next_dts = AV_NOPTS_VALUE;
            }

            if (got_output) {
                if (duration_pts > 0) {
                    ist->next_pts += av_rescale_q(duration_pts, ist->st->time_base, AV_TIME_BASE_Q);
                } else {
                    ist->next_pts += duration_dts;
                }
            }
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

    /* handle stream copy */
    if (!ist->decoding_needed && pkt) {
        ist->dts = ist->next_dts;
        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            av_assert1(pkt->duration >= 0);
            if (par->sample_rate) {
                ist->next_dts += ((int64_t)AV_TIME_BASE * par->frame_size) /
                                  par->sample_rate;
            } else {
                ist->next_dts += av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (ist->framerate.num) {
                // TODO: Remove work-around for c99-to-c89 issue 7
                AVRational time_base_q = AV_TIME_BASE_Q;
                int64_t next_dts = av_rescale_q(ist->next_dts, time_base_q, av_inv_q(ist->framerate));
                ist->next_dts = av_rescale_q(next_dts + 1, av_inv_q(ist->framerate), time_base_q);
            } else if (pkt->duration) {
                ist->next_dts += av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
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
        ist->pts = ist->dts;
        ist->next_pts = ist->next_dts;
    } else if (!ist->decoding_needed)
        eof_reached = 1;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        if (!check_output_constraints(ist, ost) || ost->enc_ctx ||
            (!pkt && no_eof))
            continue;

        do_streamcopy(ist, ost, pkt);
    }

    return !eof_reached;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (ist->hwaccel_id == HWACCEL_GENERIC ||
            ist->hwaccel_id == HWACCEL_AUTO) {
            for (i = 0;; i++) {
                config = avcodec_get_hw_config(s->codec, i);
                if (!config)
                    break;
                if (!(config->methods &
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    continue;
                if (config->pix_fmt == *p)
                    break;
            }
        }
        if (config && config->device_type == ist->hwaccel_device_type) {
            ret = hwaccel_decode_init(s);
            if (ret < 0) {
                if (ist->hwaccel_id == HWACCEL_GENERIC) {
                    av_log(NULL, AV_LOG_FATAL,
                           "%s hwaccel requested for input stream #%d:%d, "
                           "but cannot be initialized.\n",
                           av_hwdevice_get_type_name(config->device_type),
                           ist->file_index, ist->st->index);
                    return AV_PIX_FMT_NONE;
                }
                continue;
            }

            ist->hwaccel_pix_fmt = *p;
            break;
        }
    }

    return *p;
}

static int init_input_stream(InputStream *ist, char *error, int error_len)
{
    int ret;

    if (ist->decoding_needed) {
        const AVCodec *codec = ist->dec;
        if (!codec) {
            snprintf(error, error_len, "Decoder (codec %s) not found for input stream #%d:%d",
                    avcodec_get_name(ist->dec_ctx->codec_id), ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        ist->dec_ctx->opaque                = ist;
        ist->dec_ctx->get_format            = get_format;

        if (ist->dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE &&
           (ist->decoding_needed & DECODING_FOR_OST)) {
            av_dict_set(&ist->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
            if (ist->decoding_needed & DECODING_FOR_FILTER)
                av_log(NULL, AV_LOG_WARNING, "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
        }

        /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
         * audio, and video decoders such as cuvid or mediacodec */
        ist->dec_ctx->pkt_timebase = ist->st->time_base;

        if (!av_dict_get(ist->decoder_opts, "threads", NULL, 0))
            av_dict_set(&ist->decoder_opts, "threads", "auto", 0);
        /* Attached pics are sparse, therefore we would not want to delay their decoding till EOF. */
        if (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            av_dict_set(&ist->decoder_opts, "threads", "1", 0);

        ret = hw_device_setup_for_decode(ist);
        if (ret < 0) {
            snprintf(error, error_len, "Device setup failed for "
                     "decoder on input stream #%d:%d : %s",
                     ist->file_index, ist->st->index, av_err2str(ret));
            return ret;
        }

        if ((ret = avcodec_open2(ist->dec_ctx, codec, &ist->decoder_opts)) < 0) {
            if (ret == AVERROR_EXPERIMENTAL)
                abort_codec_experimental(codec, 0);

            snprintf(error, error_len,
                     "Error while opening decoder for input stream "
                     "#%d:%d : %s",
                     ist->file_index, ist->st->index, av_err2str(ret));
            return ret;
        }
        assert_avoptions(ist->decoder_opts);
    }

    ist->next_pts = AV_NOPTS_VALUE;
    ist->next_dts = AV_NOPTS_VALUE;

    return 0;
}

static int init_output_stream_streamcopy(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    InputStream *ist = ost->ist;
    InputFile *ifile = input_files[ist->file_index];
    AVCodecParameters *par = ost->st->codecpar;
    AVCodecContext *codec_ctx;
    AVRational sar;
    int i, ret;
    uint32_t codec_tag = par->codec_tag;

    av_assert0(ist && !ost->filter);

    codec_ctx = avcodec_alloc_context3(NULL);
    if (!codec_ctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(codec_ctx, ist->par);
    if (ret >= 0)
        ret = av_opt_set_dict(codec_ctx, &ost->encoder_opts);
    if (ret < 0) {
        av_log(ost, AV_LOG_FATAL,
               "Error setting up codec context options.\n");
        avcodec_free_context(&codec_ctx);
        return ret;
    }

    ret = avcodec_parameters_from_context(par, codec_ctx);
    avcodec_free_context(&codec_ctx);
    if (ret < 0) {
        av_log(ost, AV_LOG_FATAL,
               "Error getting reference codec parameters.\n");
        return ret;
    }

    if (!codec_tag) {
        unsigned int codec_tag_tmp;
        if (!of->format->codec_tag ||
            av_codec_get_id (of->format->codec_tag, par->codec_tag) == par->codec_id ||
            !av_codec_get_tag2(of->format->codec_tag, par->codec_id, &codec_tag_tmp))
            codec_tag = par->codec_tag;
    }

    par->codec_tag = codec_tag;

    if (!ost->frame_rate.num)
        ost->frame_rate = ist->framerate;

    if (ost->frame_rate.num)
        ost->st->avg_frame_rate = ost->frame_rate;
    else
        ost->st->avg_frame_rate = ist->st->avg_frame_rate;

    ret = avformat_transfer_internal_stream_timing_info(of->format, ost->st, ist->st, copy_tb);
    if (ret < 0)
        return ret;

    // copy timebase while removing common factors
    if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0) {
        if (ost->frame_rate.num)
            ost->st->time_base = av_inv_q(ost->frame_rate);
        else
            ost->st->time_base = av_add_q(av_stream_get_codec_timebase(ost->st), (AVRational){0, 1});
    }

    // copy estimated duration as a hint to the muxer
    if (ost->st->duration <= 0 && ist->st->duration > 0)
        ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);

    if (!ost->copy_prior_start) {
        ost->ts_copy_start = (of->start_time == AV_NOPTS_VALUE) ?
                             0 : of->start_time;
        if (copy_ts && ifile->start_time != AV_NOPTS_VALUE) {
            ost->ts_copy_start = FFMAX(ost->ts_copy_start,
                                       ifile->start_time + ifile->ts_offset);
        }
    }

    if (ist->st->nb_side_data) {
        for (i = 0; i < ist->st->nb_side_data; i++) {
            const AVPacketSideData *sd_src = &ist->st->side_data[i];
            uint8_t *dst_data;

            dst_data = av_stream_new_side_data(ost->st, sd_src->type, sd_src->size);
            if (!dst_data)
                return AVERROR(ENOMEM);
            memcpy(dst_data, sd_src->data, sd_src->size);
        }
    }

#if FFMPEG_ROTATION_METADATA
    if (ost->rotate_overridden) {
        uint8_t *sd = av_stream_new_side_data(ost->st, AV_PKT_DATA_DISPLAYMATRIX,
                                              sizeof(int32_t) * 9);
        if (sd)
            av_display_rotation_set((int32_t *)sd, -ost->rotate_override_value);
    }
#endif

    switch (par->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if ((par->block_align == 1 || par->block_align == 1152 || par->block_align == 576) &&
            par->codec_id == AV_CODEC_ID_MP3)
            par->block_align = 0;
        if (par->codec_id == AV_CODEC_ID_AC3)
            par->block_align = 0;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (ost->frame_aspect_ratio.num) { // overridden by the -aspect cli option
            sar =
                av_mul_q(ost->frame_aspect_ratio,
                         (AVRational){ par->height, par->width });
            av_log(ost, AV_LOG_WARNING, "Overriding aspect ratio "
                   "with stream copy may produce invalid files\n");
            }
        else if (ist->st->sample_aspect_ratio.num)
            sar = ist->st->sample_aspect_ratio;
        else
            sar = par->sample_aspect_ratio;
        ost->st->sample_aspect_ratio = par->sample_aspect_ratio = sar;
        ost->st->avg_frame_rate = ist->st->avg_frame_rate;
        ost->st->r_frame_rate = ist->st->r_frame_rate;
        break;
    }

    ost->mux_timebase = ist->st->time_base;

    return 0;
}

static void set_encoder_id(OutputFile *of, OutputStream *ost)
{
    const char *cname = ost->enc_ctx->codec->name;
    uint8_t *encoder_string;
    int encoder_string_len;

    if (av_dict_get(ost->st->metadata, "encoder",  NULL, 0))
        return;

    encoder_string_len = sizeof(LIBAVCODEC_IDENT) + strlen(cname) + 2;
    encoder_string     = av_mallocz(encoder_string_len);
    if (!encoder_string)
        report_and_exit(AVERROR(ENOMEM));

    if (!of->bitexact && !ost->bitexact)
        av_strlcpy(encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
    else
        av_strlcpy(encoder_string, "Lavc ", encoder_string_len);
    av_strlcat(encoder_string, cname, encoder_string_len);
    av_dict_set(&ost->st->metadata, "encoder",  encoder_string,
                AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
}

static void init_encoder_time_base(OutputStream *ost, AVRational default_time_base)
{
    InputStream *ist = ost->ist;
    AVCodecContext *enc_ctx = ost->enc_ctx;

    if (ost->enc_timebase.num > 0) {
        enc_ctx->time_base = ost->enc_timebase;
        return;
    }

    if (ost->enc_timebase.num < 0) {
        if (ist) {
            enc_ctx->time_base = ist->st->time_base;
            return;
        }

        av_log(ost, AV_LOG_WARNING,
               "Input stream data not available, using default time base\n");
    }

    enc_ctx->time_base = default_time_base;
}

static int init_output_stream_encode(OutputStream *ost, AVFrame *frame)
{
    InputStream *ist = ost->ist;
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVCodecContext *dec_ctx = NULL;
    OutputFile      *of = output_files[ost->file_index];
    int ret;

    set_encoder_id(output_files[ost->file_index], ost);

    if (ist) {
        dec_ctx = ist->dec_ctx;
    }

    if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!ost->frame_rate.num)
            ost->frame_rate = av_buffersink_get_frame_rate(ost->filter->filter);
        if (!ost->frame_rate.num && !ost->max_frame_rate.num) {
            ost->frame_rate = (AVRational){25, 1};
            av_log(ost, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps. Use the -r option "
                   "if you want a different framerate.\n");
        }

        if (ost->max_frame_rate.num &&
            (av_q2d(ost->frame_rate) > av_q2d(ost->max_frame_rate) ||
            !ost->frame_rate.den))
            ost->frame_rate = ost->max_frame_rate;

        if (enc_ctx->codec->supported_framerates && !ost->force_fps) {
            int idx = av_find_nearest_q_idx(ost->frame_rate, enc_ctx->codec->supported_framerates);
            ost->frame_rate = enc_ctx->codec->supported_framerates[idx];
        }
        // reduce frame rate for mpeg4 to be within the spec limits
        if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4) {
            av_reduce(&ost->frame_rate.num, &ost->frame_rate.den,
                      ost->frame_rate.num, ost->frame_rate.den, 65535);
        }
    }

    switch (enc_ctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        enc_ctx->sample_fmt     = av_buffersink_get_format(ost->filter->filter);
        enc_ctx->sample_rate    = av_buffersink_get_sample_rate(ost->filter->filter);
        ret = av_buffersink_get_ch_layout(ost->filter->filter, &enc_ctx->ch_layout);
        if (ret < 0)
            return ret;

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else if (dec_ctx && ost->filter->graph->is_meta)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);

        init_encoder_time_base(ost, av_make_q(1, enc_ctx->sample_rate));
        break;

    case AVMEDIA_TYPE_VIDEO:
        init_encoder_time_base(ost, av_inv_q(ost->frame_rate));

        if (!(enc_ctx->time_base.num && enc_ctx->time_base.den))
            enc_ctx->time_base = av_buffersink_get_time_base(ost->filter->filter);
        if (   av_q2d(enc_ctx->time_base) < 0.001 && ost->vsync_method != VSYNC_PASSTHROUGH
           && (ost->vsync_method == VSYNC_CFR || ost->vsync_method == VSYNC_VSCFR ||
               (ost->vsync_method == VSYNC_AUTO && !(of->format->flags & AVFMT_VARIABLE_FPS)))){
            av_log(ost, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                        "Please consider specifying a lower framerate, a different muxer or "
                                        "setting vsync/fps_mode to vfr\n");
        }

        enc_ctx->width  = av_buffersink_get_w(ost->filter->filter);
        enc_ctx->height = av_buffersink_get_h(ost->filter->filter);
        enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
            ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
            av_mul_q(ost->frame_aspect_ratio, (AVRational){ enc_ctx->height, enc_ctx->width }) :
            av_buffersink_get_sample_aspect_ratio(ost->filter->filter);

        enc_ctx->pix_fmt = av_buffersink_get_format(ost->filter->filter);

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else if (dec_ctx && ost->filter->graph->is_meta)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_pix_fmt_desc_get(enc_ctx->pix_fmt)->comp[0].depth);

        if (frame) {
            enc_ctx->color_range            = frame->color_range;
            enc_ctx->color_primaries        = frame->color_primaries;
            enc_ctx->color_trc              = frame->color_trc;
            enc_ctx->colorspace             = frame->colorspace;
            enc_ctx->chroma_sample_location = frame->chroma_location;
        }

        enc_ctx->framerate = ost->frame_rate;

        ost->st->avg_frame_rate = ost->frame_rate;

        // Field order: autodetection
        if (frame) {
            if (enc_ctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME) &&
                ost->top_field_first >= 0)
                frame->top_field_first = !!ost->top_field_first;

            if (frame->interlaced_frame) {
                if (enc_ctx->codec->id == AV_CODEC_ID_MJPEG)
                    enc_ctx->field_order = frame->top_field_first ? AV_FIELD_TT:AV_FIELD_BB;
                else
                    enc_ctx->field_order = frame->top_field_first ? AV_FIELD_TB:AV_FIELD_BT;
            } else
                enc_ctx->field_order = AV_FIELD_PROGRESSIVE;
        }

        // Field order: override
        if (ost->top_field_first == 0) {
            enc_ctx->field_order = AV_FIELD_BB;
        } else if (ost->top_field_first == 1) {
            enc_ctx->field_order = AV_FIELD_TT;
        }

        break;
    case AVMEDIA_TYPE_SUBTITLE:
        enc_ctx->time_base = AV_TIME_BASE_Q;
        if (!enc_ctx->width) {
            enc_ctx->width     = ost->ist->par->width;
            enc_ctx->height    = ost->ist->par->height;
        }
        if (dec_ctx && dec_ctx->subtitle_header) {
            /* ASS code assumes this buffer is null terminated so add extra byte. */
            ost->enc_ctx->subtitle_header = av_mallocz(dec_ctx->subtitle_header_size + 1);
            if (!ost->enc_ctx->subtitle_header)
                return AVERROR(ENOMEM);
            memcpy(ost->enc_ctx->subtitle_header, dec_ctx->subtitle_header,
                   dec_ctx->subtitle_header_size);
            ost->enc_ctx->subtitle_header_size = dec_ctx->subtitle_header_size;
        }
        if (ist && ist->dec->type == AVMEDIA_TYPE_SUBTITLE &&
            enc_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int input_props = 0, output_props = 0;
            AVCodecDescriptor const *input_descriptor =
                avcodec_descriptor_get(ist->dec->id);
            AVCodecDescriptor const *output_descriptor =
                avcodec_descriptor_get(ost->enc_ctx->codec_id);
            if (input_descriptor)
                input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (output_descriptor)
                output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (input_props && output_props && input_props != output_props) {
                av_log(ost, AV_LOG_ERROR,
                       "Subtitle encoding currently only possible from text to text "
                       "or bitmap to bitmap");
                return AVERROR_INVALIDDATA;
            }
        }

        break;
    case AVMEDIA_TYPE_DATA:
        break;
    default:
        abort();
        break;
    }

    if (ost->bitexact)
        enc_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    if (ost->sq_idx_encode >= 0)
        sq_set_tb(of->sq_encode, ost->sq_idx_encode, enc_ctx->time_base);

    ost->mux_timebase = enc_ctx->time_base;

    return 0;
}

static int init_output_stream(OutputStream *ost, AVFrame *frame,
                              char *error, int error_len)
{
    int ret = 0;

    if (ost->enc_ctx) {
        const AVCodec *codec = ost->enc_ctx->codec;
        InputStream *ist = ost->ist;

        ret = init_output_stream_encode(ost, frame);
        if (ret < 0)
            return ret;

        if (!av_dict_get(ost->encoder_opts, "threads", NULL, 0))
            av_dict_set(&ost->encoder_opts, "threads", "auto", 0);

        if (codec->capabilities & AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE) {
            ret = av_dict_set(&ost->encoder_opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);
            if (ret < 0)
                return ret;
        }

        ret = hw_device_setup_for_encode(ost);
        if (ret < 0) {
            snprintf(error, error_len, "Device setup failed for "
                     "encoder on output stream #%d:%d : %s",
                     ost->file_index, ost->index, av_err2str(ret));
            return ret;
        }

        if ((ret = avcodec_open2(ost->enc_ctx, codec, &ost->encoder_opts)) < 0) {
            if (ret == AVERROR_EXPERIMENTAL)
                abort_codec_experimental(codec, 1);
            snprintf(error, error_len,
                     "Error while opening encoder for output stream #%d:%d - "
                     "maybe incorrect parameters such as bit_rate, rate, width or height",
                    ost->file_index, ost->index);
            return ret;
        }
        if (codec->type == AVMEDIA_TYPE_AUDIO &&
            !(codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
            av_buffersink_set_frame_size(ost->filter->filter,
                                            ost->enc_ctx->frame_size);
        assert_avoptions(ost->encoder_opts);
        if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000 &&
            ost->enc_ctx->codec_id != AV_CODEC_ID_CODEC2 /* don't complain about 700 bit/s modes */)
            av_log(ost, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                        " It takes bits/s as argument, not kbits/s\n");

        ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc_ctx);
        if (ret < 0) {
            av_log(ost, AV_LOG_FATAL,
                   "Error initializing the output stream codec context.\n");
            exit_program(1);
        }

        if (ost->enc_ctx->nb_coded_side_data) {
            int i;

            for (i = 0; i < ost->enc_ctx->nb_coded_side_data; i++) {
                const AVPacketSideData *sd_src = &ost->enc_ctx->coded_side_data[i];
                uint8_t *dst_data;

                dst_data = av_stream_new_side_data(ost->st, sd_src->type, sd_src->size);
                if (!dst_data)
                    return AVERROR(ENOMEM);
                memcpy(dst_data, sd_src->data, sd_src->size);
            }
        }

        /*
         * Add global input side data. For now this is naive, and copies it
         * from the input stream's global side data. All side data should
         * really be funneled over AVFrame and libavfilter, then added back to
         * packet side data, and then potentially using the first packet for
         * global side data.
         */
        if (ist) {
            int i;
            for (i = 0; i < ist->st->nb_side_data; i++) {
                AVPacketSideData *sd = &ist->st->side_data[i];
                if (sd->type != AV_PKT_DATA_CPB_PROPERTIES) {
                    uint8_t *dst = av_stream_new_side_data(ost->st, sd->type, sd->size);
                    if (!dst)
                        return AVERROR(ENOMEM);
                    memcpy(dst, sd->data, sd->size);
                    if (ist->autorotate && sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                        av_display_rotation_set((int32_t *)dst, 0);
                }
            }
        }

        // copy timebase while removing common factors
        if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
            ost->st->time_base = av_add_q(ost->enc_ctx->time_base, (AVRational){0, 1});

        // copy estimated duration as a hint to the muxer
        if (ost->st->duration <= 0 && ist && ist->st->duration > 0)
            ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);
    } else if (ost->ist) {
        ret = init_output_stream_streamcopy(ost);
        if (ret < 0)
            return ret;
    }

    ret = of_stream_init(output_files[ost->file_index], ost);
    if (ret < 0)
        return ret;

    return ret;
}

static int transcode_init(void)
{
    int ret = 0;
    char error[1024] = {0};

    /* init framerate emulation */
    for (int i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        if (ifile->readrate || ifile->rate_emu)
            for (int j = 0; j < ifile->nb_streams; j++)
                ifile->streams[j]->start = av_gettime_relative();
    }

    /* init input streams */
    for (InputStream *ist = ist_iter(NULL); ist; ist = ist_iter(ist))
        if ((ret = init_input_stream(ist, error, sizeof(error))) < 0)
            goto dump_format;

    /*
     * initialize stream copy and subtitle/data streams.
     * Encoded AVFrame based streams will get initialized as follows:
     * - when the first AVFrame is received in do_video_out
     * - just before the first AVFrame is received in either transcode_step
     *   or reap_filters due to us requiring the filter chain buffer sink
     *   to be configured with the correct audio frame size, which is only
     *   known after the encoder is initialized.
     */
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        if (ost->enc_ctx &&
            (ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
             ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))
            continue;

        ret = init_output_stream_wrapper(ost, NULL, 0);
        if (ret < 0)
            goto dump_format;
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

 dump_format:
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

    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", error);
        return ret;
    }

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

        if (ost->filter && ost->last_filter_pts != AV_NOPTS_VALUE) {
            opts = ost->last_filter_pts;
        } else {
            opts = ost->last_mux_dts == AV_NOPTS_VALUE ?
                   INT64_MIN : ost->last_mux_dts;
            if (ost->last_mux_dts == AV_NOPTS_VALUE)
                av_log(ost, AV_LOG_DEBUG,
                    "cur_dts is invalid [init:%d i_done:%d finish:%d] (this is harmless if it occurs once at the start per stream)\n",
                    ost->initialized, ost->inputs_done, ost->finished);
        }

        if (!ost->initialized && !ost->inputs_done)
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
    if (key == 's') qp_hist     ^= 1;
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
    if (key == 'd' || key == 'D'){
        int debug=0;
        if(key == 'D') {
            InputStream *ist = ist_iter(NULL);

            if (ist)
                debug = ist->dec_ctx->debug << 1;

            if(!debug) debug = 1;
            while (debug & FF_DEBUG_DCT_COEFF) //unsupported, would just crash
                debug += debug;
        }else{
            char buf[32];
            int k = 0;
            i = 0;
            set_tty_echo(1);
            while ((k = read_key()) != '\n' && k != '\r' && i < sizeof(buf)-1)
                if (k > 0)
                    buf[i++] = k;
            buf[i] = 0;
            set_tty_echo(0);
            fprintf(stderr, "\n");
            if (k <= 0 || sscanf(buf, "%d", &debug)!=1)
                fprintf(stderr,"error parsing debug value\n");
        }
        for (InputStream *ist = ist_iter(NULL); ist; ist = ist_iter(ist))
            ist->dec_ctx->debug = debug;
        for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
            if (ost->enc_ctx)
                ost->enc_ctx->debug = debug;
        }
        if(debug) av_log_set_level(AV_LOG_DEBUG);
        fprintf(stderr,"debug=%d\n", debug);
    }
    if (key == '?'){
        fprintf(stderr, "key    function\n"
                        "?      show this help\n"
                        "+      increase verbosity\n"
                        "-      decrease verbosity\n"
                        "c      Send command to first matching filter supporting it\n"
                        "C      Send/Queue command to all matching filters\n"
                        "D      cycle through available debug modes\n"
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

        if (!ist->processing_needed)
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
    int64_t pkt_dts = av_rescale_q_rnd(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q,
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    if (copy_ts && ist->next_dts != AV_NOPTS_VALUE &&
        fmt_is_discont && ist->st->pts_wrap_bits < 60) {
        int64_t wrap_dts = av_rescale_q_rnd(pkt->dts + (1LL<<ist->st->pts_wrap_bits),
                                            ist->st->time_base, AV_TIME_BASE_Q,
                                            AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        if (FFABS(wrap_dts - ist->next_dts) < FFABS(pkt_dts - ist->next_dts)/10)
            disable_discontinuity_correction = 0;
    }

    if (ist->next_dts != AV_NOPTS_VALUE && !disable_discontinuity_correction) {
        int64_t delta = pkt_dts - ist->next_dts;
        if (fmt_is_discont) {
            if (FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE ||
                pkt_dts + AV_TIME_BASE/10 < FFMAX(ist->pts, ist->dts)) {
                ifile->ts_offset_discont -= delta;
                av_log(NULL, AV_LOG_DEBUG,
                       "timestamp discontinuity for stream #%d:%d "
                       "(id=%d, type=%s): %"PRId64", new offset= %"PRId64"\n",
                       ist->file_index, ist->st->index, ist->st->id,
                       av_get_media_type_string(ist->par->codec_type),
                       delta, ifile->ts_offset_discont);
                pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
        } else {
            if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                av_log(NULL, AV_LOG_WARNING, "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n", pkt->dts, ist->next_dts, pkt->stream_index);
                pkt->dts = AV_NOPTS_VALUE;
            }
            if (pkt->pts != AV_NOPTS_VALUE){
                int64_t pkt_pts = av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
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
            pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            if (pkt->pts != AV_NOPTS_VALUE)
                pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
        }
    }

    ifile->last_ts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
}

static void ts_discontinuity_process(InputFile *ifile, InputStream *ist,
                                     AVPacket *pkt)
{
    int64_t offset = av_rescale_q(ifile->ts_offset_discont, AV_TIME_BASE_Q,
                                  ist->st->time_base);

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
            if (ist->processing_needed) {
                ret = process_input_packet(ist, NULL, 0);
                if (ret>0)
                    return 0;
            }

            /* mark all outputs that don't go through lavfi as finished */
            for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
                if (ost->ist == ist &&
                    (!ost->enc_ctx || ost->enc_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE)) {
                    OutputFile *of = output_files[ost->file_index];
                    of_output_packet(of, ost->pkt, ost, 1);
                }
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
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ist->st->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ist->st->time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ist->st->time_base),
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
 * Perform a step of transcoding for the specified filter graph.
 *
 * @param[in]  graph     filter graph to consider
 * @param[out] best_ist  input stream where a frame would allow to continue
 * @return  0 for success, <0 for error
 */
static int transcode_from_filter(FilterGraph *graph, InputStream **best_ist)
{
    int i, ret;
    int nb_requests, nb_requests_max = 0;
    InputFilter *ifilter;
    InputStream *ist;

    *best_ist = NULL;
    ret = avfilter_graph_request_oldest(graph->graph);
    if (ret >= 0)
        return reap_filters(0);

    if (ret == AVERROR_EOF) {
        ret = reap_filters(1);
        for (i = 0; i < graph->nb_outputs; i++)
            close_output_stream(graph->outputs[i]->ost);
        return ret;
    }
    if (ret != AVERROR(EAGAIN))
        return ret;

    for (i = 0; i < graph->nb_inputs; i++) {
        ifilter = graph->inputs[i];
        ist = ifilter->ist;
        if (input_files[ist->file_index]->eagain ||
            input_files[ist->file_index]->eof_reached)
            continue;
        nb_requests = av_buffersrc_get_nb_failed_requests(ifilter->filter);
        if (nb_requests > nb_requests_max) {
            nb_requests_max = nb_requests;
            *best_ist = ist;
        }
    }

    if (!*best_ist)
        for (i = 0; i < graph->nb_outputs; i++)
            graph->outputs[i]->ost->unavailable = 1;

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
        /*
         * Similar case to the early audio initialization in reap_filters.
         * Audio is special in ffmpeg.c currently as we depend on lavfi's
         * audio frame buffering/creation to get the output audio frame size
         * in samples correct. The audio frame size for the filter chain is
         * configured during the output stream initialization.
         *
         * Apparently avfilter_graph_request_oldest (called in
         * transcode_from_filter just down the line) peeks. Peeking already
         * puts one frame "ready to be given out", which means that any
         * update in filter buffer sink configuration afterwards will not
         * help us. And yes, even if it would be utilized,
         * av_buffersink_get_samples is affected, as it internally utilizes
         * the same early exit for peeked frames.
         *
         * In other words, if avfilter_graph_request_oldest would not make
         * further filter chain configuration or usage of
         * av_buffersink_get_samples useless (by just causing the return
         * of the peeked AVFrame as-is), we could get rid of this additional
         * early encoder initialization.
         */
        if (av_buffersink_get_type(ost->filter->filter) == AVMEDIA_TYPE_AUDIO)
            init_output_stream_wrapper(ost, NULL, 1);

        if ((ret = transcode_from_filter(ost->filter->graph, &ist)) < 0)
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
    int64_t total_packets_written = 0;

    ret = transcode_init();
    if (ret < 0)
        goto fail;

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
    flush_encoders();

    term_exit();

    /* write the trailer if needed */
    for (i = 0; i < nb_output_files; i++) {
        ret = of_write_trailer(output_files[i]);
        if (ret < 0 && exit_on_error)
            exit_program(1);
    }

    /* dump report by using the first video and audio streams */
    print_report(1, timer_start, av_gettime_relative());

    /* close each encoder */
    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        uint64_t packets_written;
        packets_written = atomic_load(&ost->packets_written);
        total_packets_written += packets_written;
        if (!packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM)) {
            av_log(ost, AV_LOG_FATAL, "Empty output\n");
            exit_program(1);
        }
    }

    if (!total_packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT)) {
        av_log(NULL, AV_LOG_FATAL, "Empty output\n");
        exit_program(1);
    }

    hw_device_free_all();

    /* finished ! */
    ret = 0;

 fail:
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
    if (transcode() < 0)
        exit_program(1);
    if (do_benchmark) {
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

    exit_program(received_nb_signals ? 255 : main_return_code);
    return main_return_code;
}
