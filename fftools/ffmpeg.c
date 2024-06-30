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

#include "libavutil/avassert.h"

const char program_name[] = "ffmpeg";
const int program_birth_year = 2000;

static FILE *vstats_file;

const char *const forced_keyframes_const_names[] = {
    "n",
    "n_forced",
    "prev_forced_n",
    "prev_forced_t",
    "t",
    NULL
};

typedef struct BenchmarkTimeStamps {
    int64_t real_usec;
    int64_t user_usec;
    int64_t sys_usec;
} BenchmarkTimeStamps;

static void do_video_stats(OutputStream *ost, int frame_size);
static BenchmarkTimeStamps get_benchmark_time_stamps(void);
static int64_t getmaxrss(void);
static int ifilter_has_all_input_formats(FilterGraph *fg);

static int run_as_daemon  = 0;
static int nb_frames_dup = 0;
static unsigned dup_warning = 1000;
static int nb_frames_drop = 0;
static int64_t decode_error_stat[2];
static unsigned nb_output_dumped = 0;

static int want_sdp = 1;

static BenchmarkTimeStamps current_time;
AVIOContext *progress_avio = NULL;

static uint8_t *subtitle_out;

InputStream **input_streams = NULL;
int        nb_input_streams = 0;
InputFile   **input_files   = NULL;
int        nb_input_files   = 0;

OutputStream **output_streams = NULL;
int         nb_output_streams = 0;
OutputFile   **output_files   = NULL;
int         nb_output_files   = 0;

FilterGraph **filtergraphs;
int        nb_filtergraphs;

#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int restore_tty;
#endif

#if HAVE_THREADS
static void free_input_threads(void);
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
        av_log(ist->dec_ctx, AV_LOG_ERROR,
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
        InputStream *ist2 = input_streams[infile->ist_index + i];
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
static int main_return_code = 0;
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
    if (!run_as_daemon && stdin_interaction) {
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
            if (read(0, &ch, 1) == 1)
                return ch;
            return 0;
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

            while (av_fifo_size(ifilter->frame_queue)) {
                AVFrame *frame;
                av_fifo_generic_read(ifilter->frame_queue, &frame,
                                     sizeof(frame), NULL);
                av_frame_free(&frame);
            }
            av_fifo_freep(&ifilter->frame_queue);
            if (ist->sub2video.sub_queue) {
                while (av_fifo_size(ist->sub2video.sub_queue)) {
                    AVSubtitle sub;
                    av_fifo_generic_read(ist->sub2video.sub_queue,
                                         &sub, sizeof(sub), NULL);
                    avsubtitle_free(&sub);
                }
                av_fifo_freep(&ist->sub2video.sub_queue);
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
            av_freep(&ofilter->formats);
            av_freep(&ofilter->channel_layouts);
            av_freep(&ofilter->sample_rates);
            av_freep(&fg->outputs[j]);
        }
        av_freep(&fg->outputs);
        av_freep(&fg->graph_desc);

        av_freep(&filtergraphs[i]);
    }
    av_freep(&filtergraphs);

    av_freep(&subtitle_out);

    /* close files */
    for (i = 0; i < nb_output_files; i++) {
        OutputFile *of = output_files[i];
        AVFormatContext *s;
        if (!of)
            continue;
        s = of->ctx;
        if (s && s->oformat && !(s->oformat->flags & AVFMT_NOFILE))
            avio_closep(&s->pb);
        avformat_free_context(s);
        av_dict_free(&of->opts);

        av_freep(&output_files[i]);
    }
    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (!ost)
            continue;

        av_bsf_free(&ost->bsf_ctx);

        av_frame_free(&ost->filtered_frame);
        av_frame_free(&ost->last_frame);
        av_packet_free(&ost->pkt);
        av_dict_free(&ost->encoder_opts);

        av_freep(&ost->forced_keyframes);
        av_expr_free(ost->forced_keyframes_pexpr);
        av_freep(&ost->avfilter);
        av_freep(&ost->logfile_prefix);

        av_freep(&ost->audio_channels_map);
        ost->audio_channels_mapped = 0;

        av_dict_free(&ost->sws_dict);
        av_dict_free(&ost->swr_opts);

        avcodec_free_context(&ost->enc_ctx);
        avcodec_parameters_free(&ost->ref_par);

        if (ost->muxing_queue) {
            while (av_fifo_size(ost->muxing_queue)) {
                AVPacket *pkt;
                av_fifo_generic_read(ost->muxing_queue, &pkt, sizeof(pkt), NULL);
                av_packet_free(&pkt);
            }
            av_fifo_freep(&ost->muxing_queue);
        }

        av_freep(&output_streams[i]);
    }
#if HAVE_THREADS
    free_input_threads();
#endif
    for (i = 0; i < nb_input_files; i++) {
        avformat_close_input(&input_files[i]->ctx);
        av_packet_free(&input_files[i]->pkt);
        av_freep(&input_files[i]);
    }
    for (i = 0; i < nb_input_streams; i++) {
        InputStream *ist = input_streams[i];

        av_frame_free(&ist->decoded_frame);
        av_frame_free(&ist->filter_frame);
        av_packet_free(&ist->pkt);
        av_dict_free(&ist->decoder_opts);
        avsubtitle_free(&ist->prev_sub.subtitle);
        av_frame_free(&ist->sub2video.frame);
        av_freep(&ist->filters);
        av_freep(&ist->hwaccel_device);
        av_freep(&ist->dts_buffer);

        avcodec_free_context(&ist->dec_ctx);

        av_freep(&input_streams[i]);
    }

    if (vstats_file) {
        if (fclose(vstats_file))
            av_log(NULL, AV_LOG_ERROR,
                   "Error closing vstats file, loss of information possible: %s\n",
                   av_err2str(AVERROR(errno)));
    }
    av_freep(&vstats_filename);

    av_freep(&input_streams);
    av_freep(&input_files);
    av_freep(&output_streams);
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

void remove_avoptions(AVDictionary **a, AVDictionary *b)
{
    AVDictionaryEntry *t = NULL;

    while ((t = av_dict_get(b, "", t, AV_DICT_IGNORE_SUFFIX))) {
        av_dict_set(a, t->key, NULL, AV_DICT_MATCH_CASE);
    }
}

void assert_avoptions(AVDictionary *m)
{
    AVDictionaryEntry *t;
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

static void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others)
{
    int i;
    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost2 = output_streams[i];
        ost2->finished |= ost == ost2 ? this_stream : others;
    }
}

static void write_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int unqueue)
{
    AVFormatContext *s = of->ctx;
    AVStream *st = ost->st;
    int ret;

    /*
     * Audio encoders may split the packets --  #frames in != #packets out.
     * But there is no reordering, so we can limit the number of output packets
     * by simply dropping them here.
     * Counting encoded video frames needs to be done separately because of
     * reordering, see do_video_out().
     * Do not count the packet when unqueued because it has been counted when queued.
     */
    if (!(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->encoding_needed) && !unqueue) {
        if (ost->frame_number >= ost->max_frames) {
            av_packet_unref(pkt);
            return;
        }
        ost->frame_number++;
    }

    if (!of->header_written) {
        AVPacket *tmp_pkt;
        /* the muxer is not initialized yet, buffer the packet */
        if (!av_fifo_space(ost->muxing_queue)) {
            unsigned int are_we_over_size =
                (ost->muxing_queue_data_size + pkt->size) > ost->muxing_queue_data_threshold;
            int new_size = are_we_over_size ?
                           FFMIN(2 * av_fifo_size(ost->muxing_queue),
                                 ost->max_muxing_queue_size) :
                           2 * av_fifo_size(ost->muxing_queue);

            if (new_size <= av_fifo_size(ost->muxing_queue)) {
                av_log(NULL, AV_LOG_ERROR,
                       "Too many packets buffered for output stream %d:%d.\n",
                       ost->file_index, ost->st->index);
                exit_program(1);
            }
            ret = av_fifo_realloc2(ost->muxing_queue, new_size);
            if (ret < 0)
                exit_program(1);
        }
        ret = av_packet_make_refcounted(pkt);
        if (ret < 0)
            exit_program(1);
        tmp_pkt = av_packet_alloc();
        if (!tmp_pkt)
            exit_program(1);
        av_packet_move_ref(tmp_pkt, pkt);
        ost->muxing_queue_data_size += tmp_pkt->size;
        av_fifo_generic_write(ost->muxing_queue, &tmp_pkt, sizeof(tmp_pkt), NULL);
        return;
    }

    if ((st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_sync_method == VSYNC_DROP) ||
        (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_sync_method < 0))
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        int i;
        uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                              NULL);
        ost->quality = sd ? AV_RL32(sd) : -1;
        ost->pict_type = sd ? sd[4] : AV_PICTURE_TYPE_NONE;

        for (i = 0; i<FF_ARRAY_ELEMS(ost->error); i++) {
            if (sd && i < sd[5])
                ost->error[i] = AV_RL64(sd + 8 + 8*i);
            else
                ost->error[i] = -1;
        }

        if (ost->frame_rate.num && ost->is_cfr) {
            if (pkt->duration > 0)
                av_log(NULL, AV_LOG_WARNING, "Overriding packet duration by frame rate, this should not happen\n");
            pkt->duration = av_rescale_q(1, av_inv_q(ost->frame_rate),
                                         ost->mux_timebase);
        }
    }

    av_packet_rescale_ts(pkt, ost->mux_timebase, ost->st->time_base);

    if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
        if (pkt->dts != AV_NOPTS_VALUE &&
            pkt->pts != AV_NOPTS_VALUE &&
            pkt->dts > pkt->pts) {
            av_log(s, AV_LOG_WARNING, "Invalid DTS: %"PRId64" PTS: %"PRId64" in output stream %d:%d, replacing by guess\n",
                   pkt->dts, pkt->pts,
                   ost->file_index, ost->st->index);
            pkt->pts =
            pkt->dts = pkt->pts + pkt->dts + ost->last_mux_dts + 1
                     - FFMIN3(pkt->pts, pkt->dts, ost->last_mux_dts + 1)
                     - FFMAX3(pkt->pts, pkt->dts, ost->last_mux_dts + 1);
        }
        if ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) &&
            pkt->dts != AV_NOPTS_VALUE &&
            !(st->codecpar->codec_id == AV_CODEC_ID_VP9 && ost->stream_copy) &&
            ost->last_mux_dts != AV_NOPTS_VALUE) {
            int64_t max = ost->last_mux_dts + !(s->oformat->flags & AVFMT_TS_NONSTRICT);
            if (pkt->dts < max) {
                int loglevel = max - pkt->dts > 2 || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ? AV_LOG_WARNING : AV_LOG_DEBUG;
                if (exit_on_error)
                    loglevel = AV_LOG_ERROR;
                av_log(s, loglevel, "Non-monotonous DTS in output stream "
                       "%d:%d; previous: %"PRId64", current: %"PRId64"; ",
                       ost->file_index, ost->st->index, ost->last_mux_dts, pkt->dts);
                if (exit_on_error) {
                    av_log(NULL, AV_LOG_FATAL, "aborting.\n");
                    exit_program(1);
                }
                av_log(s, loglevel, "changing to %"PRId64". This may result "
                       "in incorrect timestamps in the output file.\n",
                       max);
                if (pkt->pts >= pkt->dts)
                    pkt->pts = FFMAX(pkt->pts, max);
                pkt->dts = max;
            }
        }
    }
    ost->last_mux_dts = pkt->dts;

    ost->data_size += pkt->size;
    ost->packets_written++;

    pkt->stream_index = ost->index;

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
                "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s size:%d\n",
                av_get_media_type_string(ost->enc_ctx->codec_type),
                av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
                av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
                pkt->size
              );
    }

    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0) {
        print_error("av_interleaved_write_frame()", ret);
        main_return_code = 1;
        close_all_output_streams(ost, MUXER_FINISHED | ENCODER_FINISHED, ENCODER_FINISHED);
    }
    av_packet_unref(pkt);
}

static void close_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    ost->finished |= ENCODER_FINISHED;
    if (of->shortest) {
        int64_t end = av_rescale_q(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, AV_TIME_BASE_Q);
        of->recording_time = FFMIN(of->recording_time, end);
    }
}

/*
 * Send a single packet to the output, applying any bitstream filters
 * associated with the output stream.  This may result in any number
 * of packets actually being written, depending on what bitstream
 * filters are applied.  The supplied packet is consumed and will be
 * blank (as if newly-allocated) when this function returns.
 *
 * If eof is set, instead indicate EOF to all bitstream filters and
 * therefore flush any delayed packets to the output.  A blank packet
 * must be supplied in this case.
 */
static void output_packet(OutputFile *of, AVPacket *pkt,
                          OutputStream *ost, int eof)
{
    int ret = 0;

    /* apply the output bitstream filters */
    if (ost->bsf_ctx) {
        ret = av_bsf_send_packet(ost->bsf_ctx, eof ? NULL : pkt);
        if (ret < 0)
            goto finish;
        while ((ret = av_bsf_receive_packet(ost->bsf_ctx, pkt)) >= 0)
            write_packet(of, pkt, ost, 0);
        if (ret == AVERROR(EAGAIN))
            ret = 0;
    } else if (!eof)
        write_packet(of, pkt, ost, 0);

finish:
    if (ret < 0 && ret != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error applying bitstream filters to an output "
               "packet for stream #%d:%d.\n", ost->file_index, ost->index);
        if(exit_on_error)
            exit_program(1);
    }
}

static int check_recording_time(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, of->recording_time,
                      AV_TIME_BASE_Q) >= 0) {
        close_output_stream(ost);
        return 0;
    }
    return 1;
}

static double adjust_frame_pts_to_encoder_tb(OutputFile *of, OutputStream *ost,
                                             AVFrame *frame)
{
    double float_pts = AV_NOPTS_VALUE; // this is identical to frame.pts but with higher precision
    AVCodecContext *enc = ost->enc_ctx;
    if (!frame || frame->pts == AV_NOPTS_VALUE ||
        !enc || !ost->filter || !ost->filter->graph->graph)
        goto early_exit;

    {
        AVFilterContext *filter = ost->filter->filter;

        int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
        AVRational filter_tb = av_buffersink_get_time_base(filter);
        AVRational tb = enc->time_base;
        int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

        tb.den <<= extra_bits;
        float_pts =
            av_rescale_q(frame->pts, filter_tb, tb) -
            av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
        float_pts /= 1 << extra_bits;
        // avoid exact midoints to reduce the chance of rounding differences, this can be removed in case the fps code is changed to work with integers
        float_pts += FFSIGN(float_pts) * 1.0 / (1<<17);

        frame->pts =
            av_rescale_q(frame->pts, filter_tb, enc->time_base) -
            av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
    }

early_exit:

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
               frame ? av_ts2str(frame->pts) : "NULL",
               frame ? av_ts2timestr(frame->pts, &enc->time_base) : "NULL",
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
        av_log(NULL, AV_LOG_ERROR, "Error initializing output stream %d:%d -- %s\n",
               ost->file_index, ost->index, error);

        if (fatal)
            exit_program(1);
    }

    return ret;
}

static void do_audio_out(OutputFile *of, OutputStream *ost,
                         AVFrame *frame)
{
    AVCodecContext *enc = ost->enc_ctx;
    AVPacket *pkt = ost->pkt;
    int ret;

    adjust_frame_pts_to_encoder_tb(of, ost, frame);

    if (!check_recording_time(ost))
        return;

    if (frame->pts == AV_NOPTS_VALUE || audio_sync_method < 0)
        frame->pts = ost->sync_opts;
    ost->sync_opts = frame->pts + frame->nb_samples;
    ost->samples_encoded += frame->nb_samples;
    ost->frames_encoded++;

    update_benchmark(NULL);
    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "encoder <- type:audio "
               "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
               av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
               enc->time_base.num, enc->time_base.den);
    }

    ret = avcodec_send_frame(enc, frame);
    if (ret < 0)
        goto error;

    while (1) {
        av_packet_unref(pkt);
        ret = avcodec_receive_packet(enc, pkt);
        if (ret == AVERROR(EAGAIN))
            break;
        if (ret < 0)
            goto error;

        update_benchmark("encode_audio %d.%d", ost->file_index, ost->index);

        av_packet_rescale_ts(pkt, enc->time_base, ost->mux_timebase);

        if (debug_ts) {
            av_log(NULL, AV_LOG_INFO, "encoder -> type:audio "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base));
        }

        output_packet(of, pkt, ost, 0);
    }

    return;
error:
    av_log(NULL, AV_LOG_FATAL, "Audio encoding failed\n");
    exit_program(1);
}

static void do_subtitle_out(OutputFile *of,
                            OutputStream *ost,
                            AVSubtitle *sub)
{
    int subtitle_out_max_size = 1024 * 1024;
    int subtitle_out_size, nb, i;
    AVCodecContext *enc;
    AVPacket *pkt = ost->pkt;
    int64_t pts;

    if (sub->pts == AV_NOPTS_VALUE) {
        av_log(NULL, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        if (exit_on_error)
            exit_program(1);
        return;
    }

    enc = ost->enc_ctx;

    if (!subtitle_out) {
        subtitle_out = av_malloc(subtitle_out_max_size);
        if (!subtitle_out) {
            av_log(NULL, AV_LOG_FATAL, "Failed to allocate subtitle_out\n");
            exit_program(1);
        }
    }

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

        ost->sync_opts = av_rescale_q(pts, AV_TIME_BASE_Q, enc->time_base);
        if (!check_recording_time(ost))
            return;

        sub->pts = pts;
        // start_display_time is required to be 0
        sub->pts               += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, AV_TIME_BASE_Q);
        sub->end_display_time  -= sub->start_display_time;
        sub->start_display_time = 0;
        if (i == 1)
            sub->num_rects = 0;

        ost->frames_encoded++;

        subtitle_out_size = avcodec_encode_subtitle(enc, subtitle_out,
                                                    subtitle_out_max_size, sub);
        if (i == 1)
            sub->num_rects = save_num_rects;
        if (subtitle_out_size < 0) {
            av_log(NULL, AV_LOG_FATAL, "Subtitle encoding failed\n");
            exit_program(1);
        }

        av_packet_unref(pkt);
        pkt->data = subtitle_out;
        pkt->size = subtitle_out_size;
        pkt->pts  = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->mux_timebase);
        pkt->duration = av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, ost->mux_timebase);
        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt->pts += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, ost->mux_timebase);
            else
                pkt->pts += av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, ost->mux_timebase);
        }
        pkt->dts = pkt->pts;
        output_packet(of, pkt, ost, 0);
    }
}

static void do_video_out(OutputFile *of,
                         OutputStream *ost,
                         AVFrame *next_picture)
{
    int ret, format_video_sync;
    AVPacket *pkt = ost->pkt;
    AVCodecContext *enc = ost->enc_ctx;
    AVRational frame_rate;
    int nb_frames, nb0_frames, i;
    double delta, delta0;
    double duration = 0;
    double sync_ipts = AV_NOPTS_VALUE;
    int frame_size = 0;
    InputStream *ist = NULL;
    AVFilterContext *filter = ost->filter->filter;

    init_output_stream_wrapper(ost, next_picture, 1);
    sync_ipts = adjust_frame_pts_to_encoder_tb(of, ost, next_picture);

    if (ost->source_index >= 0)
        ist = input_streams[ost->source_index];

    frame_rate = av_buffersink_get_frame_rate(filter);
    if (frame_rate.num > 0 && frame_rate.den > 0)
        duration = 1/(av_q2d(frame_rate) * av_q2d(enc->time_base));

    if(ist && ist->st->start_time != AV_NOPTS_VALUE && ist->st->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
        duration = FFMIN(duration, 1/(av_q2d(ost->frame_rate) * av_q2d(enc->time_base)));

    if (!ost->filters_script &&
        !ost->filters &&
        (nb_filtergraphs == 0 || !filtergraphs[0]->graph_desc) &&
        next_picture &&
        ist &&
        lrintf(next_picture->pkt_duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base)) > 0) {
        duration = lrintf(next_picture->pkt_duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base));
    }

    if (!next_picture) {
        //end, flushing
        nb0_frames = nb_frames = mid_pred(ost->last_nb0_frames[0],
                                          ost->last_nb0_frames[1],
                                          ost->last_nb0_frames[2]);
    } else {
        delta0 = sync_ipts - ost->sync_opts; // delta0 is the "drift" between the input frame (next_picture) and where it would fall in the output.
        delta  = delta0 + duration;

        /* by default, we output a single frame */
        nb0_frames = 0; // tracks the number of times the PREVIOUS frame should be duplicated, mostly for variable framerate (VFR)
        nb_frames = 1;

        format_video_sync = video_sync_method;
        if (format_video_sync == VSYNC_AUTO) {
            if(!strcmp(of->ctx->oformat->name, "avi")) {
                format_video_sync = VSYNC_VFR;
            } else
                format_video_sync = (of->ctx->oformat->flags & AVFMT_VARIABLE_FPS) ? ((of->ctx->oformat->flags & AVFMT_NOTIMESTAMPS) ? VSYNC_PASSTHROUGH : VSYNC_VFR) : VSYNC_CFR;
            if (   ist
                && format_video_sync == VSYNC_CFR
                && input_files[ist->file_index]->ctx->nb_streams == 1
                && input_files[ist->file_index]->input_ts_offset == 0) {
                format_video_sync = VSYNC_VSCFR;
            }
            if (format_video_sync == VSYNC_CFR && copy_ts) {
                format_video_sync = VSYNC_VSCFR;
            }
        }
        ost->is_cfr = (format_video_sync == VSYNC_CFR || format_video_sync == VSYNC_VSCFR);

        if (delta0 < 0 &&
            delta > 0 &&
            format_video_sync != VSYNC_PASSTHROUGH &&
            format_video_sync != VSYNC_DROP) {
            if (delta0 < -0.6) {
                av_log(NULL, AV_LOG_VERBOSE, "Past duration %f too large\n", -delta0);
            } else
                av_log(NULL, AV_LOG_DEBUG, "Clipping frame in rate conversion by %f\n", -delta0);
            sync_ipts = ost->sync_opts;
            duration += delta0;
            delta0 = 0;
        }

        switch (format_video_sync) {
        case VSYNC_VSCFR:
            if (ost->frame_number == 0 && delta0 >= 0.5) {
                av_log(NULL, AV_LOG_DEBUG, "Not duplicating %d initial frames\n", (int)lrintf(delta0));
                delta = duration;
                delta0 = 0;
                ost->sync_opts = llrint(sync_ipts);
            }
        case VSYNC_CFR:
            // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
            if (frame_drop_threshold && delta < frame_drop_threshold && ost->frame_number) {
                nb_frames = 0;
            } else if (delta < -1.1)
                nb_frames = 0;
            else if (delta > 1.1) {
                nb_frames = lrintf(delta);
                if (delta0 > 1.1)
                    nb0_frames = llrintf(delta0 - 0.6);
            }
            break;
        case VSYNC_VFR:
            if (delta <= -0.6)
                nb_frames = 0;
            else if (delta > 0.6)
                ost->sync_opts = llrint(sync_ipts);
            break;
        case VSYNC_DROP:
        case VSYNC_PASSTHROUGH:
            ost->sync_opts = llrint(sync_ipts);
            break;
        default:
            av_assert0(0);
        }
    }

    nb_frames = FFMIN(nb_frames, ost->max_frames - ost->frame_number);
    nb0_frames = FFMIN(nb0_frames, nb_frames);

    memmove(ost->last_nb0_frames + 1,
            ost->last_nb0_frames,
            sizeof(ost->last_nb0_frames[0]) * (FF_ARRAY_ELEMS(ost->last_nb0_frames) - 1));
    ost->last_nb0_frames[0] = nb0_frames;

    if (nb0_frames == 0 && ost->last_dropped) {
        nb_frames_drop++;
        av_log(NULL, AV_LOG_VERBOSE,
               "*** dropping frame %d from stream %d at ts %"PRId64"\n",
               ost->frame_number, ost->st->index, ost->last_frame->pts);
    }
    if (nb_frames > (nb0_frames && ost->last_dropped) + (nb_frames > nb0_frames)) {
        if (nb_frames > dts_error_threshold * 30) {
            av_log(NULL, AV_LOG_ERROR, "%d frame duplication too large, skipping\n", nb_frames - 1);
            nb_frames_drop++;
            return;
        }
        nb_frames_dup += nb_frames - (nb0_frames && ost->last_dropped) - (nb_frames > nb0_frames);
        av_log(NULL, AV_LOG_VERBOSE, "*** %d dup!\n", nb_frames - 1);
        if (nb_frames_dup > dup_warning) {
            av_log(NULL, AV_LOG_WARNING, "More than %d frames duplicated\n", dup_warning);
            dup_warning *= 10;
        }
    }
    ost->last_dropped = nb_frames == nb0_frames && next_picture;

    /* duplicates frame if needed */
    for (i = 0; i < nb_frames; i++) {
        AVFrame *in_picture;
        int forced_keyframe = 0;
        double pts_time;

        if (i < nb0_frames && ost->last_frame) {
            in_picture = ost->last_frame;
        } else
            in_picture = next_picture;

        if (!in_picture)
            return;

        in_picture->pts = ost->sync_opts;

        if (!check_recording_time(ost))
            return;

        in_picture->quality = enc->global_quality;
        in_picture->pict_type = 0;

        if (ost->forced_kf_ref_pts == AV_NOPTS_VALUE &&
            in_picture->pts != AV_NOPTS_VALUE)
            ost->forced_kf_ref_pts = in_picture->pts;

        pts_time = in_picture->pts != AV_NOPTS_VALUE ?
            (in_picture->pts - ost->forced_kf_ref_pts) * av_q2d(enc->time_base) : NAN;
        if (ost->forced_kf_index < ost->forced_kf_count &&
            in_picture->pts >= ost->forced_kf_pts[ost->forced_kf_index]) {
            ost->forced_kf_index++;
            forced_keyframe = 1;
        } else if (ost->forced_keyframes_pexpr) {
            double res;
            ost->forced_keyframes_expr_const_values[FKF_T] = pts_time;
            res = av_expr_eval(ost->forced_keyframes_pexpr,
                               ost->forced_keyframes_expr_const_values, NULL);
            ff_dlog(NULL, "force_key_frame: n:%f n_forced:%f prev_forced_n:%f t:%f prev_forced_t:%f -> res:%f\n",
                    ost->forced_keyframes_expr_const_values[FKF_N],
                    ost->forced_keyframes_expr_const_values[FKF_N_FORCED],
                    ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N],
                    ost->forced_keyframes_expr_const_values[FKF_T],
                    ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T],
                    res);
            if (res) {
                forced_keyframe = 1;
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N] =
                    ost->forced_keyframes_expr_const_values[FKF_N];
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T] =
                    ost->forced_keyframes_expr_const_values[FKF_T];
                ost->forced_keyframes_expr_const_values[FKF_N_FORCED] += 1;
            }

            ost->forced_keyframes_expr_const_values[FKF_N] += 1;
        } else if (   ost->forced_keyframes
                   && !strncmp(ost->forced_keyframes, "source", 6)
                   && in_picture->key_frame==1
                   && !i) {
            forced_keyframe = 1;
        }

        if (forced_keyframe) {
            in_picture->pict_type = AV_PICTURE_TYPE_I;
            av_log(NULL, AV_LOG_DEBUG, "Forced keyframe at time %f\n", pts_time);
        }

        update_benchmark(NULL);
        if (debug_ts) {
            av_log(NULL, AV_LOG_INFO, "encoder <- type:video "
                   "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
                   av_ts2str(in_picture->pts), av_ts2timestr(in_picture->pts, &enc->time_base),
                   enc->time_base.num, enc->time_base.den);
        }

        ost->frames_encoded++;

        ret = avcodec_send_frame(enc, in_picture);
        if (ret < 0)
            goto error;
        // Make sure Closed Captions will not be duplicated
        av_frame_remove_side_data(in_picture, AV_FRAME_DATA_A53_CC);

        while (1) {
            av_packet_unref(pkt);
            ret = avcodec_receive_packet(enc, pkt);
            update_benchmark("encode_video %d.%d", ost->file_index, ost->index);
            if (ret == AVERROR(EAGAIN))
                break;
            if (ret < 0)
                goto error;

            if (debug_ts) {
                av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                       "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                       av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                       av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base));
            }

            if (pkt->pts == AV_NOPTS_VALUE && !(enc->codec->capabilities & AV_CODEC_CAP_DELAY))
                pkt->pts = ost->sync_opts;

            av_packet_rescale_ts(pkt, enc->time_base, ost->mux_timebase);

            if (debug_ts) {
                av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                    "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                    av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->mux_timebase),
                    av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->mux_timebase));
            }

            frame_size = pkt->size;
            output_packet(of, pkt, ost, 0);

            /* if two pass, output log */
            if (ost->logfile && enc->stats_out) {
                fprintf(ost->logfile, "%s", enc->stats_out);
            }
        }
        ost->sync_opts++;
        /*
         * For video, number of frames in == number of packets out.
         * But there may be reordering, so we can't throw away frames on encoder
         * flush, we need to limit them here, before they go into encoder.
         */
        ost->frame_number++;

        if (vstats_filename && frame_size)
            do_video_stats(ost, frame_size);
    }

    if (!ost->last_frame)
        ost->last_frame = av_frame_alloc();
    av_frame_unref(ost->last_frame);
    if (next_picture && ost->last_frame)
        av_frame_ref(ost->last_frame, next_picture);
    else
        av_frame_free(&ost->last_frame);

    return;
error:
    av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
    exit_program(1);
}

static double psnr(double d)
{
    return -10.0 * log10(d);
}

static void do_video_stats(OutputStream *ost, int frame_size)
{
    AVCodecContext *enc;
    int frame_number;
    double ti1, bitrate, avg_bitrate;

    /* this is executed just the first time do_video_stats is called */
    if (!vstats_file) {
        vstats_file = fopen(vstats_filename, "w");
        if (!vstats_file) {
            perror("fopen");
            exit_program(1);
        }
    }

    enc = ost->enc_ctx;
    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        frame_number = ost->st->nb_frames;
        if (vstats_version <= 1) {
            fprintf(vstats_file, "frame= %5d q= %2.1f ", frame_number,
                    ost->quality / (float)FF_QP2LAMBDA);
        } else  {
            fprintf(vstats_file, "out= %2d st= %2d frame= %5d q= %2.1f ", ost->file_index, ost->index, frame_number,
                    ost->quality / (float)FF_QP2LAMBDA);
        }

        if (ost->error[0]>=0 && (enc->flags & AV_CODEC_FLAG_PSNR))
            fprintf(vstats_file, "PSNR= %6.2f ", psnr(ost->error[0] / (enc->width * enc->height * 255.0 * 255.0)));

        fprintf(vstats_file,"f_size= %6d ", frame_size);
        /* compute pts value */
        ti1 = av_stream_get_end_pts(ost->st) * av_q2d(ost->st->time_base);
        if (ti1 < 0.01)
            ti1 = 0.01;

        bitrate     = (frame_size * 8) / av_q2d(enc->time_base) / 1000.0;
        avg_bitrate = (double)(ost->data_size * 8) / ti1 / 1000.0;
        fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
               (double)ost->data_size / 1024, ti1, bitrate, avg_bitrate);
        fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(ost->pict_type));
    }
}

static void finish_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    int i;

    ost->finished = ENCODER_FINISHED | MUXER_FINISHED;

    if (of->shortest) {
        for (i = 0; i < of->ctx->nb_streams; i++)
            output_streams[of->ost_index + i]->finished = ENCODER_FINISHED | MUXER_FINISHED;
    }
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
    int i;

    /* Reap all buffers present in the buffer sinks */
    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];
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

        if (!ost->pkt && !(ost->pkt = av_packet_alloc())) {
            return AVERROR(ENOMEM);
        }
        if (!ost->filtered_frame && !(ost->filtered_frame = av_frame_alloc())) {
            return AVERROR(ENOMEM);
        }
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

            switch (av_buffersink_get_type(filter)) {
            case AVMEDIA_TYPE_VIDEO:
                if (!ost->frame_aspect_ratio.num)
                    enc->sample_aspect_ratio = filtered_frame->sample_aspect_ratio;

                do_video_out(of, ost, filtered_frame);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
                    enc->channels != filtered_frame->channels) {
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

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];
        switch (ost->enc_ctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO: video_size += ost->data_size; break;
            case AVMEDIA_TYPE_AUDIO: audio_size += ost->data_size; break;
            case AVMEDIA_TYPE_SUBTITLE: subtitle_size += ost->data_size; break;
            default:                 other_size += ost->data_size; break;
        }
        extra_size += ost->enc_ctx->extradata_size;
        data_size  += ost->data_size;
        if (   (ost->enc_ctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2))
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
            InputStream *ist = input_streams[f->ist_index + j];
            enum AVMediaType type = ist->dec_ctx->codec_type;

            total_size    += ist->data_size;
            total_packets += ist->nb_packets;

            av_log(NULL, AV_LOG_VERBOSE, "  Input stream #%d:%d (%s): ",
                   i, j, media_type_string(type));
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
               i, of->ctx->url);

        for (j = 0; j < of->ctx->nb_streams; j++) {
            OutputStream *ost = output_streams[of->ost_index + j];
            enum AVMediaType type = ost->enc_ctx->codec_type;

            total_size    += ost->data_size;
            total_packets += ost->packets_written;

            av_log(NULL, AV_LOG_VERBOSE, "  Output stream #%d:%d (%s): ",
                   i, j, media_type_string(type));
            if (ost->encoding_needed) {
                av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" frames encoded",
                       ost->frames_encoded);
                if (type == AVMEDIA_TYPE_AUDIO)
                    av_log(NULL, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ost->samples_encoded);
                av_log(NULL, AV_LOG_VERBOSE, "; ");
            }

            av_log(NULL, AV_LOG_VERBOSE, "%"PRIu64" packets muxed (%"PRIu64" bytes); ",
                   ost->packets_written, ost->data_size);

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
    OutputStream *ost;
    AVFormatContext *oc;
    int64_t total_size;
    AVCodecContext *enc;
    int frame_number, vid, i;
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


    oc = output_files[0]->ctx;

    total_size = avio_size(oc->pb);
    if (total_size <= 0) // FIXME improve avio_size() so it works with non seekable output too
        total_size = avio_tell(oc->pb);

    vid = 0;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprint_init(&buf_script, 0, AV_BPRINT_SIZE_AUTOMATIC);
    for (i = 0; i < nb_output_streams; i++) {
        float q = -1;
        ost = output_streams[i];
        enc = ost->enc_ctx;
        if (!ost->stream_copy)
            q = ost->quality / (float) FF_QP2LAMBDA;

        if (vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_bprintf(&buf, "q=%2.1f ", q);
            av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                       ost->file_index, ost->index, q);
        }
        if (!vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            float fps;

            frame_number = ost->frame_number;
            fps = t > 1 ? frame_number / t : 0;
            av_bprintf(&buf, "frame=%5d fps=%3.*f q=%3.1f ",
                     frame_number, fps < 9.95, fps, q);
            av_bprintf(&buf_script, "frame=%d\n", frame_number);
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

            if ((enc->flags & AV_CODEC_FLAG_PSNR) && (ost->pict_type != AV_PICTURE_TYPE_NONE || is_last_report)) {
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
        if (av_stream_get_end_pts(ost->st) != AV_NOPTS_VALUE) {
            pts = FFMAX(pts, av_rescale_q(av_stream_get_end_pts(ost->st),
                                          ost->st->time_base, AV_TIME_BASE_Q));
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
        av_bprintf(&buf, " dup=%d drop=%d", nb_frames_dup, nb_frames_drop);
    av_bprintf(&buf_script, "dup_frames=%d\n", nb_frames_dup);
    av_bprintf(&buf_script, "drop_frames=%d\n", nb_frames_drop);

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

static void ifilter_parameters_from_codecpar(InputFilter *ifilter, AVCodecParameters *par)
{
    // We never got any input. Set a fake format, which will
    // come from libavformat.
    ifilter->format                 = par->format;
    ifilter->sample_rate            = par->sample_rate;
    ifilter->channels               = par->channels;
    ifilter->channel_layout         = par->channel_layout;
    ifilter->width                  = par->width;
    ifilter->height                 = par->height;
    ifilter->sample_aspect_ratio    = par->sample_aspect_ratio;
}

static void flush_encoders(void)
{
    int i, ret;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream   *ost = output_streams[i];
        AVCodecContext *enc = ost->enc_ctx;
        OutputFile      *of = output_files[ost->file_index];

        if (!ost->encoding_needed)
            continue;

        // Try to enable encoding with no input frames.
        // Maybe we should just let encoding fail instead.
        if (!ost->initialized) {
            FilterGraph *fg = ost->filter->graph;

            av_log(NULL, AV_LOG_WARNING,
                   "Finishing stream %d:%d without any data written to it.\n",
                   ost->file_index, ost->st->index);

            if (ost->filter && !fg->graph) {
                int x;
                for (x = 0; x < fg->nb_inputs; x++) {
                    InputFilter *ifilter = fg->inputs[x];
                    if (ifilter->format < 0)
                        ifilter_parameters_from_codecpar(ifilter, ifilter->ist->st->codecpar);
                }

                if (!ifilter_has_all_input_formats(fg))
                    continue;

                ret = configure_filtergraph(fg);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error configuring filter graph\n");
                    exit_program(1);
                }

                finish_output_stream(ost);
            }

            init_output_stream_wrapper(ost, NULL, 1);
        }

        if (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        for (;;) {
            const char *desc = NULL;
            AVPacket *pkt = ost->pkt;
            int pkt_size;

            if (!pkt)
                break;

            switch (enc->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                desc   = "audio";
                break;
            case AVMEDIA_TYPE_VIDEO:
                desc   = "video";
                break;
            default:
                av_assert0(0);
            }

            update_benchmark(NULL);

            av_packet_unref(pkt);
            while ((ret = avcodec_receive_packet(enc, pkt)) == AVERROR(EAGAIN)) {
                ret = avcodec_send_frame(enc, NULL);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_FATAL, "%s encoding failed: %s\n",
                           desc,
                           av_err2str(ret));
                    exit_program(1);
                }
            }

            update_benchmark("flush_%s %d.%d", desc, ost->file_index, ost->index);
            if (ret < 0 && ret != AVERROR_EOF) {
                av_log(NULL, AV_LOG_FATAL, "%s encoding failed: %s\n",
                       desc,
                       av_err2str(ret));
                exit_program(1);
            }
            if (ost->logfile && enc->stats_out) {
                fprintf(ost->logfile, "%s", enc->stats_out);
            }
            if (ret == AVERROR_EOF) {
                output_packet(of, pkt, ost, 1);
                break;
            }
            if (ost->finished & MUXER_FINISHED) {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_rescale_ts(pkt, enc->time_base, ost->mux_timebase);
            pkt_size = pkt->size;
            output_packet(of, pkt, ost, 0);
            if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO && vstats_filename) {
                do_video_stats(ost, pkt_size);
            }
        }
    }
}

/*
 * Check whether a packet from ist should be written into ost at this time
 */
static int check_output_constraints(InputStream *ist, OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    int ist_index  = input_files[ist->file_index]->ist_index + ist->st->index;

    if (ost->source_index != ist_index)
        return 0;

    if (ost->finished)
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
        output_packet(of, opkt, ost, 1);
        return;
    }

    if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) &&
        !ost->copy_initial_nonkeyframes)
        return;

    if (!ost->frame_number && !ost->copy_prior_start) {
        int64_t comp_start = start_time;
        if (copy_ts && f->start_time != AV_NOPTS_VALUE)
            comp_start = FFMAX(start_time, f->start_time + f->ts_offset);
        if (pkt->pts == AV_NOPTS_VALUE ?
            ist->pts < comp_start :
            pkt->pts < av_rescale_q(comp_start, AV_TIME_BASE_Q, ist->st->time_base))
            return;
    }

    if (of->recording_time != INT64_MAX &&
        ist->pts >= of->recording_time + start_time) {
        close_output_stream(ost);
        return;
    }

    if (f->recording_time != INT64_MAX) {
        start_time = f->ctx->start_time;
        if (f->start_time != AV_NOPTS_VALUE && copy_ts)
            start_time += f->start_time;
        if (ist->pts >= f->recording_time + start_time) {
            close_output_stream(ost);
            return;
        }
    }

    /* force the input stream PTS */
    if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
        ost->sync_opts++;

    if (av_packet_ref(opkt, pkt) < 0)
        exit_program(1);

    if (pkt->pts != AV_NOPTS_VALUE)
        opkt->pts = av_rescale_q(pkt->pts, ist->st->time_base, ost->mux_timebase) - ost_tb_start_time;

    if (pkt->dts == AV_NOPTS_VALUE) {
        opkt->dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ost->mux_timebase);
    } else if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        int duration = av_get_audio_frame_duration(ist->dec_ctx, pkt->size);
        if(!duration)
            duration = ist->dec_ctx->frame_size;
        opkt->dts = av_rescale_delta(ist->st->time_base, pkt->dts,
                                    (AVRational){1, ist->dec_ctx->sample_rate}, duration,
                                    &ist->filter_in_rescale_delta_last, ost->mux_timebase);
        /* dts will be set immediately afterwards to what pts is now */
        opkt->pts = opkt->dts - ost_tb_start_time;
    } else
        opkt->dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->mux_timebase);
    opkt->dts -= ost_tb_start_time;

    opkt->duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->mux_timebase);

    output_packet(of, opkt, ost, 0);
}

int guess_input_channel_layout(InputStream *ist)
{
    AVCodecContext *dec = ist->dec_ctx;

    if (!dec->channel_layout) {
        char layout_name[256];

        if (dec->channels > ist->guess_layout_max)
            return 0;
        dec->channel_layout = av_get_default_channel_layout(dec->channels);
        if (!dec->channel_layout)
            return 0;
        av_get_channel_layout_string(layout_name, sizeof(layout_name),
                                     dec->channels, dec->channel_layout);
        av_log(NULL, AV_LOG_WARNING, "Guessed Channel Layout for Input Stream "
               "#%d.%d : %s\n", ist->file_index, ist->st->index, layout_name);
    }
    return 1;
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

static int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame)
{
    FilterGraph *fg = ifilter->graph;
    int need_reinit, ret, i;

    /* determine if the parameters for this input changed */
    need_reinit = ifilter->format != frame->format;

    switch (ifilter->ist->st->codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        need_reinit |= ifilter->sample_rate    != frame->sample_rate ||
                       ifilter->channels       != frame->channels ||
                       ifilter->channel_layout != frame->channel_layout;
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

    if (need_reinit) {
        ret = ifilter_parameters_from_frame(ifilter, frame);
        if (ret < 0)
            return ret;
    }

    /* (re)init the graph if possible, otherwise buffer the frame and return */
    if (need_reinit || !fg->graph) {
        for (i = 0; i < fg->nb_inputs; i++) {
            if (!ifilter_has_all_input_formats(fg)) {
                AVFrame *tmp = av_frame_clone(frame);
                if (!tmp)
                    return AVERROR(ENOMEM);
                av_frame_unref(frame);

                if (!av_fifo_space(ifilter->frame_queue)) {
                    ret = av_fifo_realloc2(ifilter->frame_queue, 2 * av_fifo_size(ifilter->frame_queue));
                    if (ret < 0) {
                        av_frame_free(&tmp);
                        return ret;
                    }
                }
                av_fifo_generic_write(ifilter->frame_queue, &tmp, sizeof(tmp), NULL);
                return 0;
            }
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

    ret = av_buffersrc_add_frame_flags(ifilter->filter, frame, AV_BUFFERSRC_FLAG_PUSH);
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
        if (ifilter->format < 0)
            ifilter_parameters_from_codecpar(ifilter, ifilter->ist->st->codecpar);
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
static int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt)
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
    if (ret >= 0)
        *got_frame = 1;

    return 0;
}

static int send_frame_to_filters(InputStream *ist, AVFrame *decoded_frame)
{
    int i, ret;
    AVFrame *f;

    av_assert1(ist->nb_filters > 0); /* ensure ret is initialized */
    for (i = 0; i < ist->nb_filters; i++) {
        if (i < ist->nb_filters - 1) {
            f = ist->filter_frame;
            ret = av_frame_ref(f, decoded_frame);
            if (ret < 0)
                break;
        } else
            f = decoded_frame;
        ret = ifilter_send_frame(ist->filters[i], f);
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
    AVFrame *decoded_frame;
    AVCodecContext *avctx = ist->dec_ctx;
    int ret, err = 0;
    AVRational decoded_frame_tb;

    if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = ist->decoded_frame;

    update_benchmark(NULL);
    ret = decode(avctx, decoded_frame, got_output, pkt);
    update_benchmark("decode_audio %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

    if (ret >= 0 && avctx->sample_rate <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d invalid\n", avctx->sample_rate);
        ret = AVERROR_INVALIDDATA;
    }

    if (ret != AVERROR_EOF)
        check_decode_result(ist, got_output, ret);

    if (!*got_output || ret < 0)
        return ret;

    ist->samples_decoded += decoded_frame->nb_samples;
    ist->frames_decoded++;

    /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
    ist->next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     avctx->sample_rate;
    ist->next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     avctx->sample_rate;

    if (decoded_frame->pts != AV_NOPTS_VALUE) {
        decoded_frame_tb   = ist->st->time_base;
    } else if (pkt && pkt->pts != AV_NOPTS_VALUE) {
        decoded_frame->pts = pkt->pts;
        decoded_frame_tb   = ist->st->time_base;
    }else {
        decoded_frame->pts = ist->dts;
        decoded_frame_tb   = AV_TIME_BASE_Q;
    }
    if (decoded_frame->pts != AV_NOPTS_VALUE)
        decoded_frame->pts = av_rescale_delta(decoded_frame_tb, decoded_frame->pts,
                                              (AVRational){1, avctx->sample_rate}, decoded_frame->nb_samples, &ist->filter_in_rescale_delta_last,
                                              (AVRational){1, avctx->sample_rate});
    ist->nb_samples = decoded_frame->nb_samples;
    err = send_frame_to_filters(ist, decoded_frame);

    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int decode_video(InputStream *ist, AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof,
                        int *decode_failed)
{
    AVFrame *decoded_frame;
    int i, ret = 0, err = 0;
    int64_t best_effort_timestamp;
    int64_t dts = AV_NOPTS_VALUE;

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (!eof && pkt && pkt->size == 0)
        return 0;

    if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = ist->decoded_frame;
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
    ret = decode(ist->dec_ctx, decoded_frame, got_output, pkt);
    update_benchmark("decode_video %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

    // The following line may be required in some cases where there is no parser
    // or the parser does not has_b_frames correctly
    if (ist->st->codecpar->video_delay < ist->dec_ctx->has_b_frames) {
        if (ist->dec_ctx->codec_id == AV_CODEC_ID_H264) {
            ist->st->codecpar->video_delay = ist->dec_ctx->has_b_frames;
        } else
            av_log(ist->dec_ctx, AV_LOG_WARNING,
                   "video_delay is larger in decoder than demuxer %d > %d.\n"
                   "If you want to help, upload a sample "
                   "of this file to https://streams.videolan.org/upload/ "
                   "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)\n",
                   ist->dec_ctx->has_b_frames,
                   ist->st->codecpar->video_delay);
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
    ist->hwaccel_retrieved_pix_fmt = decoded_frame->format;

    best_effort_timestamp= decoded_frame->best_effort_timestamp;
    *duration_pts = decoded_frame->pkt_duration;

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
    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int transcode_subtitles(InputStream *ist, AVPacket *pkt, int *got_output,
                               int *decode_failed)
{
    AVSubtitle subtitle;
    int free_sub = 1;
    int i, ret = avcodec_decode_subtitle2(ist->dec_ctx,
                                          &subtitle, got_output, pkt);

    check_decode_result(NULL, got_output, ret);

    if (ret < 0 || !*got_output) {
        *decode_failed = 1;
        if (!pkt->size)
            sub2video_flush(ist);
        return ret;
    }

    if (ist->fix_sub_duration) {
        int end = 1;
        if (ist->prev_sub.got_output) {
            end = av_rescale(subtitle.pts - ist->prev_sub.subtitle.pts,
                             1000, AV_TIME_BASE);
            if (end < ist->prev_sub.subtitle.end_display_time) {
                av_log(ist->dec_ctx, AV_LOG_DEBUG,
                       "Subtitle duration reduced from %"PRId32" to %d%s\n",
                       ist->prev_sub.subtitle.end_display_time, end,
                       end <= 0 ? ", dropping it" : "");
                ist->prev_sub.subtitle.end_display_time = end;
            }
        }
        FFSWAP(int,        *got_output, ist->prev_sub.got_output);
        FFSWAP(int,        ret,         ist->prev_sub.ret);
        FFSWAP(AVSubtitle, subtitle,    ist->prev_sub.subtitle);
        if (end <= 0)
            goto out;
    }

    if (!*got_output)
        return ret;

    if (ist->sub2video.frame) {
        sub2video_update(ist, INT64_MIN, &subtitle);
    } else if (ist->nb_filters) {
        if (!ist->sub2video.sub_queue)
            ist->sub2video.sub_queue = av_fifo_alloc(8 * sizeof(AVSubtitle));
        if (!ist->sub2video.sub_queue)
            exit_program(1);
        if (!av_fifo_space(ist->sub2video.sub_queue)) {
            ret = av_fifo_realloc2(ist->sub2video.sub_queue, 2 * av_fifo_size(ist->sub2video.sub_queue));
            if (ret < 0)
                exit_program(1);
        }
        av_fifo_generic_write(ist->sub2video.sub_queue, &subtitle, sizeof(subtitle), NULL);
        free_sub = 0;
    }

    if (!subtitle.num_rects)
        goto out;

    ist->frames_decoded++;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (!ost->pkt && !(ost->pkt = av_packet_alloc()))
            exit_program(1);
        if (!check_output_constraints(ist, ost) || !ost->encoding_needed
            || ost->enc->type != AVMEDIA_TYPE_SUBTITLE)
            continue;

        do_subtitle_out(output_files[ost->file_index], ost, &subtitle);
    }

out:
    if (free_sub)
        avsubtitle_free(&subtitle);
    return ret;
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
    int ret = 0, i;
    int repeating = 0;
    int eof_reached = 0;

    AVPacket *avpkt;

    if (!ist->pkt && !(ist->pkt = av_packet_alloc()))
        return AVERROR(ENOMEM);
    avpkt = ist->pkt;

    if (!ist->saw_first_ts) {
        ist->dts = ist->st->avg_frame_rate.num ? - ist->dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        ist->pts = 0;
        if (pkt && pkt->pts != AV_NOPTS_VALUE && !ist->decoding_needed) {
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
        if (ist->dec_ctx->codec_type != AVMEDIA_TYPE_VIDEO || !ist->decoding_needed)
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

        switch (ist->dec_ctx->codec_type) {
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
                    int ticks= av_stream_get_parser(ist->st) ? av_stream_get_parser(ist->st)->repeat_pict+1 : ist->dec_ctx->ticks_per_frame;
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
        switch (ist->dec_ctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            av_assert1(pkt->duration >= 0);
            if (ist->dec_ctx->sample_rate) {
                ist->next_dts += ((int64_t)AV_TIME_BASE * ist->dec_ctx->frame_size) /
                                  ist->dec_ctx->sample_rate;
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
                int ticks= av_stream_get_parser(ist->st) ? av_stream_get_parser(ist->st)->repeat_pict + 1 : ist->dec_ctx->ticks_per_frame;
                ist->next_dts += ((int64_t)AV_TIME_BASE *
                                  ist->dec_ctx->framerate.den * ticks) /
                                  ist->dec_ctx->framerate.num / ist->dec_ctx->ticks_per_frame;
            }
            break;
        }
        ist->pts = ist->dts;
        ist->next_pts = ist->next_dts;
    }
    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (!ost->pkt && !(ost->pkt = av_packet_alloc()))
            exit_program(1);
        if (!check_output_constraints(ist, ost) || ost->encoding_needed)
            continue;

        do_streamcopy(ist, ost, pkt);
    }

    return !eof_reached;
}

static void print_sdp(void)
{
    char sdp[16384];
    int i;
    int j;
    AVIOContext *sdp_pb;
    AVFormatContext **avc;

    for (i = 0; i < nb_output_files; i++) {
        if (!output_files[i]->header_written)
            return;
    }

    avc = av_malloc_array(nb_output_files, sizeof(*avc));
    if (!avc)
        exit_program(1);
    for (i = 0, j = 0; i < nb_output_files; i++) {
        if (!strcmp(output_files[i]->ctx->oformat->name, "rtp")) {
            avc[j] = output_files[i]->ctx;
            j++;
        }
    }

    if (!j)
        goto fail;

    av_sdp_create(avc, j, sdp, sizeof(sdp));

    if (!sdp_filename) {
        printf("SDP:\n%s\n", sdp);
        fflush(stdout);
    } else {
        if (avio_open2(&sdp_pb, sdp_filename, AVIO_FLAG_WRITE, &int_cb, NULL) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open sdp file '%s'\n", sdp_filename);
        } else {
            avio_print(sdp_pb, sdp);
            avio_closep(&sdp_pb);
            av_freep(&sdp_filename);
        }
    }

fail:
    av_freep(&avc);
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
        if (config) {
            if (config->device_type != ist->hwaccel_device_type) {
                // Different hwaccel offered, ignore.
                continue;
            }

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
        } else {
            const HWAccel *hwaccel = NULL;
            int i;
            for (i = 0; hwaccels[i].name; i++) {
                if (hwaccels[i].pix_fmt == *p) {
                    hwaccel = &hwaccels[i];
                    break;
                }
            }
            if (!hwaccel) {
                // No hwaccel supporting this pixfmt.
                continue;
            }
            if (hwaccel->id != ist->hwaccel_id) {
                // Does not match requested hwaccel.
                continue;
            }

            ret = hwaccel->init(s);
            if (ret < 0) {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream #%d:%d, "
                       "but cannot be initialized.\n", hwaccel->name,
                       ist->file_index, ist->st->index);
                return AV_PIX_FMT_NONE;
            }
        }

        if (ist->hw_frames_ctx) {
            s->hw_frames_ctx = av_buffer_ref(ist->hw_frames_ctx);
            if (!s->hw_frames_ctx)
                return AV_PIX_FMT_NONE;
        }

        ist->hwaccel_pix_fmt = *p;
        break;
    }

    return *p;
}

static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    if (ist->hwaccel_get_buffer && frame->format == ist->hwaccel_pix_fmt)
        return ist->hwaccel_get_buffer(s, frame, flags);

    return avcodec_default_get_buffer2(s, frame, flags);
}

static int init_input_stream(int ist_index, char *error, int error_len)
{
    int ret;
    InputStream *ist = input_streams[ist_index];

    if (ist->decoding_needed) {
        const AVCodec *codec = ist->dec;
        if (!codec) {
            snprintf(error, error_len, "Decoder (codec %s) not found for input stream #%d:%d",
                    avcodec_get_name(ist->dec_ctx->codec_id), ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        ist->dec_ctx->opaque                = ist;
        ist->dec_ctx->get_format            = get_format;
        ist->dec_ctx->get_buffer2           = get_buffer;
#if LIBAVCODEC_VERSION_MAJOR < 60
        ist->dec_ctx->thread_safe_callbacks = 1;
#endif

        if (ist->dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE &&
           (ist->decoding_needed & DECODING_FOR_OST)) {
            av_dict_set(&ist->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
            if (ist->decoding_needed & DECODING_FOR_FILTER)
                av_log(NULL, AV_LOG_WARNING, "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
        }

        av_dict_set(&ist->decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

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

static InputStream *get_input_stream(OutputStream *ost)
{
    if (ost->source_index >= 0)
        return input_streams[ost->source_index];
    return NULL;
}

static int compare_int64(const void *a, const void *b)
{
    return FFDIFFSIGN(*(const int64_t *)a, *(const int64_t *)b);
}

/* open the muxer when all the streams are initialized */
static int check_init_output_file(OutputFile *of, int file_index)
{
    int ret, i;

    for (i = 0; i < of->ctx->nb_streams; i++) {
        OutputStream *ost = output_streams[of->ost_index + i];
        if (!ost->initialized)
            return 0;
    }

    of->ctx->interrupt_callback = int_cb;

    ret = avformat_write_header(of->ctx, &of->opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not write header for output file #%d "
               "(incorrect codec parameters ?): %s\n",
               file_index, av_err2str(ret));
        return ret;
    }
    //assert_avoptions(of->opts);
    of->header_written = 1;

    av_dump_format(of->ctx, file_index, of->ctx->url, 1);
    nb_output_dumped++;

    if (sdp_filename || want_sdp)
        print_sdp();

    /* flush the muxing queues */
    for (i = 0; i < of->ctx->nb_streams; i++) {
        OutputStream *ost = output_streams[of->ost_index + i];

        /* try to improve muxing time_base (only possible if nothing has been written yet) */
        if (!av_fifo_size(ost->muxing_queue))
            ost->mux_timebase = ost->st->time_base;

        while (av_fifo_size(ost->muxing_queue)) {
            AVPacket *pkt;
            av_fifo_generic_read(ost->muxing_queue, &pkt, sizeof(pkt), NULL);
            ost->muxing_queue_data_size -= pkt->size;
            write_packet(of, pkt, ost, 1);
            av_packet_free(&pkt);
        }
    }

    return 0;
}

static int init_output_bsfs(OutputStream *ost)
{
    AVBSFContext *ctx = ost->bsf_ctx;
    int ret;

    if (!ctx)
        return 0;

    ret = avcodec_parameters_copy(ctx->par_in, ost->st->codecpar);
    if (ret < 0)
        return ret;

    ctx->time_base_in = ost->st->time_base;

    ret = av_bsf_init(ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing bitstream filter: %s\n",
               ctx->filter->name);
        return ret;
    }

    ret = avcodec_parameters_copy(ost->st->codecpar, ctx->par_out);
    if (ret < 0)
        return ret;
    ost->st->time_base = ctx->time_base_out;

    return 0;
}

static int init_output_stream_streamcopy(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    InputStream *ist = get_input_stream(ost);
    AVCodecParameters *par_dst = ost->st->codecpar;
    AVCodecParameters *par_src = ost->ref_par;
    AVRational sar;
    int i, ret;
    uint32_t codec_tag = par_dst->codec_tag;

    av_assert0(ist && !ost->filter);

    ret = avcodec_parameters_to_context(ost->enc_ctx, ist->st->codecpar);
    if (ret >= 0)
        ret = av_opt_set_dict(ost->enc_ctx, &ost->encoder_opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL,
               "Error setting up codec context options.\n");
        return ret;
    }

    ret = avcodec_parameters_from_context(par_src, ost->enc_ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL,
               "Error getting reference codec parameters.\n");
        return ret;
    }

    if (!codec_tag) {
        unsigned int codec_tag_tmp;
        if (!of->ctx->oformat->codec_tag ||
            av_codec_get_id (of->ctx->oformat->codec_tag, par_src->codec_tag) == par_src->codec_id ||
            !av_codec_get_tag2(of->ctx->oformat->codec_tag, par_src->codec_id, &codec_tag_tmp))
            codec_tag = par_src->codec_tag;
    }

    ret = avcodec_parameters_copy(par_dst, par_src);
    if (ret < 0)
        return ret;

    par_dst->codec_tag = codec_tag;

    if (!ost->frame_rate.num)
        ost->frame_rate = ist->framerate;
    ost->st->avg_frame_rate = ost->frame_rate;

    ret = avformat_transfer_internal_stream_timing_info(of->ctx->oformat, ost->st, ist->st, copy_tb);
    if (ret < 0)
        return ret;

    // copy timebase while removing common factors
    if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
        ost->st->time_base = av_add_q(av_stream_get_codec_timebase(ost->st), (AVRational){0, 1});

    // copy estimated duration as a hint to the muxer
    if (ost->st->duration <= 0 && ist->st->duration > 0)
        ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);

    // copy disposition
    ost->st->disposition = ist->st->disposition;

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

    if (ost->rotate_overridden) {
        uint8_t *sd = av_stream_new_side_data(ost->st, AV_PKT_DATA_DISPLAYMATRIX,
                                              sizeof(int32_t) * 9);
        if (sd)
            av_display_rotation_set((int32_t *)sd, -ost->rotate_override_value);
    }

    switch (par_dst->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (audio_volume != 256) {
            av_log(NULL, AV_LOG_FATAL, "-acodec copy and -vol are incompatible (frames are not decoded)\n");
            exit_program(1);
        }
        if((par_dst->block_align == 1 || par_dst->block_align == 1152 || par_dst->block_align == 576) && par_dst->codec_id == AV_CODEC_ID_MP3)
            par_dst->block_align= 0;
        if(par_dst->codec_id == AV_CODEC_ID_AC3)
            par_dst->block_align= 0;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (ost->frame_aspect_ratio.num) { // overridden by the -aspect cli option
            sar =
                av_mul_q(ost->frame_aspect_ratio,
                         (AVRational){ par_dst->height, par_dst->width });
            av_log(NULL, AV_LOG_WARNING, "Overriding aspect ratio "
                   "with stream copy may produce invalid files\n");
            }
        else if (ist->st->sample_aspect_ratio.num)
            sar = ist->st->sample_aspect_ratio;
        else
            sar = par_src->sample_aspect_ratio;
        ost->st->sample_aspect_ratio = par_dst->sample_aspect_ratio = sar;
        ost->st->avg_frame_rate = ist->st->avg_frame_rate;
        ost->st->r_frame_rate = ist->st->r_frame_rate;
        break;
    }

    ost->mux_timebase = ist->st->time_base;

    return 0;
}

static void set_encoder_id(OutputFile *of, OutputStream *ost)
{
    AVDictionaryEntry *e;

    uint8_t *encoder_string;
    int encoder_string_len;
    int format_flags = 0;
    int codec_flags = ost->enc_ctx->flags;

    if (av_dict_get(ost->st->metadata, "encoder",  NULL, 0))
        return;

    e = av_dict_get(of->opts, "fflags", NULL, 0);
    if (e) {
        const AVOption *o = av_opt_find(of->ctx, "fflags", NULL, 0, 0);
        if (!o)
            return;
        av_opt_eval_flags(of->ctx, o, e->value, &format_flags);
    }
    e = av_dict_get(ost->encoder_opts, "flags", NULL, 0);
    if (e) {
        const AVOption *o = av_opt_find(ost->enc_ctx, "flags", NULL, 0, 0);
        if (!o)
            return;
        av_opt_eval_flags(ost->enc_ctx, o, e->value, &codec_flags);
    }

    encoder_string_len = sizeof(LIBAVCODEC_IDENT) + strlen(ost->enc->name) + 2;
    encoder_string     = av_mallocz(encoder_string_len);
    if (!encoder_string)
        exit_program(1);

    if (!(format_flags & AVFMT_FLAG_BITEXACT) && !(codec_flags & AV_CODEC_FLAG_BITEXACT))
        av_strlcpy(encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
    else
        av_strlcpy(encoder_string, "Lavc ", encoder_string_len);
    av_strlcat(encoder_string, ost->enc->name, encoder_string_len);
    av_dict_set(&ost->st->metadata, "encoder",  encoder_string,
                AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
}

static void parse_forced_key_frames(char *kf, OutputStream *ost,
                                    AVCodecContext *avctx)
{
    char *p;
    int n = 1, i, size, index = 0;
    int64_t t, *pts;

    for (p = kf; *p; p++)
        if (*p == ',')
            n++;
    size = n;
    pts = av_malloc_array(size, sizeof(*pts));
    if (!pts) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.\n");
        exit_program(1);
    }

    p = kf;
    for (i = 0; i < n; i++) {
        char *next = strchr(p, ',');

        if (next)
            *next++ = 0;

        if (!memcmp(p, "chapters", 8)) {

            AVFormatContext *avf = output_files[ost->file_index]->ctx;
            int j;

            if (avf->nb_chapters > INT_MAX - size ||
                !(pts = av_realloc_f(pts, size += avf->nb_chapters - 1,
                                     sizeof(*pts)))) {
                av_log(NULL, AV_LOG_FATAL,
                       "Could not allocate forced key frames array.\n");
                exit_program(1);
            }
            t = p[8] ? parse_time_or_die("force_key_frames", p + 8, 1) : 0;
            t = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);

            for (j = 0; j < avf->nb_chapters; j++) {
                AVChapter *c = avf->chapters[j];
                av_assert1(index < size);
                pts[index++] = av_rescale_q(c->start, c->time_base,
                                            avctx->time_base) + t;
            }

        } else {

            t = parse_time_or_die("force_key_frames", p, 1);
            av_assert1(index < size);
            pts[index++] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);

        }

        p = next;
    }

    av_assert0(index == size);
    qsort(pts, size, sizeof(*pts), compare_int64);
    ost->forced_kf_count = size;
    ost->forced_kf_pts   = pts;
}

static void init_encoder_time_base(OutputStream *ost, AVRational default_time_base)
{
    InputStream *ist = get_input_stream(ost);
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVFormatContext *oc;

    if (ost->enc_timebase.num > 0) {
        enc_ctx->time_base = ost->enc_timebase;
        return;
    }

    if (ost->enc_timebase.num < 0) {
        if (ist) {
            enc_ctx->time_base = ist->st->time_base;
            return;
        }

        oc = output_files[ost->file_index]->ctx;
        av_log(oc, AV_LOG_WARNING, "Input stream data not available, using default time base\n");
    }

    enc_ctx->time_base = default_time_base;
}

static int init_output_stream_encode(OutputStream *ost, AVFrame *frame)
{
    InputStream *ist = get_input_stream(ost);
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVCodecContext *dec_ctx = NULL;
    AVFormatContext *oc = output_files[ost->file_index]->ctx;
    int j, ret;

    set_encoder_id(output_files[ost->file_index], ost);

    // Muxers use AV_PKT_DATA_DISPLAYMATRIX to signal rotation. On the other
    // hand, the legacy API makes demuxers set "rotate" metadata entries,
    // which have to be filtered out to prevent leaking them to output files.
    av_dict_set(&ost->st->metadata, "rotate", NULL, 0);

    if (ist) {
        ost->st->disposition          = ist->st->disposition;

        dec_ctx = ist->dec_ctx;

        enc_ctx->chroma_sample_location = dec_ctx->chroma_sample_location;
    } else {
        for (j = 0; j < oc->nb_streams; j++) {
            AVStream *st = oc->streams[j];
            if (st != ost->st && st->codecpar->codec_type == ost->st->codecpar->codec_type)
                break;
        }
        if (j == oc->nb_streams)
            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                ost->st->disposition = AV_DISPOSITION_DEFAULT;
    }

    if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!ost->frame_rate.num)
            ost->frame_rate = av_buffersink_get_frame_rate(ost->filter->filter);
        if (ist && !ost->frame_rate.num)
            ost->frame_rate = ist->framerate;
        if (ist && !ost->frame_rate.num)
            ost->frame_rate = ist->st->r_frame_rate;
        if (ist && !ost->frame_rate.num && !ost->max_frame_rate.num) {
            ost->frame_rate = (AVRational){25, 1};
            av_log(NULL, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps for output stream #%d:%d. Use the -r option "
                   "if you want a different framerate.\n",
                   ost->file_index, ost->index);
        }

        if (ost->max_frame_rate.num &&
            (av_q2d(ost->frame_rate) > av_q2d(ost->max_frame_rate) ||
            !ost->frame_rate.den))
            ost->frame_rate = ost->max_frame_rate;

        if (ost->enc->supported_framerates && !ost->force_fps) {
            int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
            ost->frame_rate = ost->enc->supported_framerates[idx];
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
        if (dec_ctx)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);
        enc_ctx->sample_rate    = av_buffersink_get_sample_rate(ost->filter->filter);
        enc_ctx->channel_layout = av_buffersink_get_channel_layout(ost->filter->filter);
        enc_ctx->channels       = av_buffersink_get_channels(ost->filter->filter);

        init_encoder_time_base(ost, av_make_q(1, enc_ctx->sample_rate));
        break;

    case AVMEDIA_TYPE_VIDEO:
        init_encoder_time_base(ost, av_inv_q(ost->frame_rate));

        if (!(enc_ctx->time_base.num && enc_ctx->time_base.den))
            enc_ctx->time_base = av_buffersink_get_time_base(ost->filter->filter);
        if (   av_q2d(enc_ctx->time_base) < 0.001 && video_sync_method != VSYNC_PASSTHROUGH
           && (video_sync_method == VSYNC_CFR || video_sync_method == VSYNC_VSCFR || (video_sync_method == VSYNC_AUTO && !(oc->oformat->flags & AVFMT_VARIABLE_FPS)))){
            av_log(oc, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                       "Please consider specifying a lower framerate, a different muxer or -vsync 2\n");
        }

        enc_ctx->width  = av_buffersink_get_w(ost->filter->filter);
        enc_ctx->height = av_buffersink_get_h(ost->filter->filter);
        enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
            ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
            av_mul_q(ost->frame_aspect_ratio, (AVRational){ enc_ctx->height, enc_ctx->width }) :
            av_buffersink_get_sample_aspect_ratio(ost->filter->filter);

        enc_ctx->pix_fmt = av_buffersink_get_format(ost->filter->filter);
        if (dec_ctx)
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

        if (!dec_ctx ||
            enc_ctx->width   != dec_ctx->width  ||
            enc_ctx->height  != dec_ctx->height ||
            enc_ctx->pix_fmt != dec_ctx->pix_fmt) {
            enc_ctx->bits_per_raw_sample = frame_bits_per_raw_sample;
        }

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

        if (ost->forced_keyframes) {
            if (!strncmp(ost->forced_keyframes, "expr:", 5)) {
                ret = av_expr_parse(&ost->forced_keyframes_pexpr, ost->forced_keyframes+5,
                                    forced_keyframes_const_names, NULL, NULL, NULL, NULL, 0, NULL);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Invalid force_key_frames expression '%s'\n", ost->forced_keyframes+5);
                    return ret;
                }
                ost->forced_keyframes_expr_const_values[FKF_N] = 0;
                ost->forced_keyframes_expr_const_values[FKF_N_FORCED] = 0;
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N] = NAN;
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T] = NAN;

                // Don't parse the 'forced_keyframes' in case of 'keep-source-keyframes',
                // parse it only for static kf timings
            } else if(strncmp(ost->forced_keyframes, "source", 6)) {
                parse_forced_key_frames(ost->forced_keyframes, ost, ost->enc_ctx);
            }
        }
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        enc_ctx->time_base = AV_TIME_BASE_Q;
        if (!enc_ctx->width) {
            enc_ctx->width     = input_streams[ost->source_index]->st->codecpar->width;
            enc_ctx->height    = input_streams[ost->source_index]->st->codecpar->height;
        }
        break;
    case AVMEDIA_TYPE_DATA:
        break;
    default:
        abort();
        break;
    }

    ost->mux_timebase = enc_ctx->time_base;

    return 0;
}

static int init_output_stream(OutputStream *ost, AVFrame *frame,
                              char *error, int error_len)
{
    int ret = 0;

    if (ost->encoding_needed) {
        const AVCodec *codec = ost->enc;
        AVCodecContext *dec = NULL;
        InputStream *ist;

        ret = init_output_stream_encode(ost, frame);
        if (ret < 0)
            return ret;

        if ((ist = get_input_stream(ost)))
            dec = ist->dec_ctx;
        if (dec && dec->subtitle_header) {
            /* ASS code assumes this buffer is null terminated so add extra byte. */
            ost->enc_ctx->subtitle_header = av_mallocz(dec->subtitle_header_size + 1);
            if (!ost->enc_ctx->subtitle_header)
                return AVERROR(ENOMEM);
            memcpy(ost->enc_ctx->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
            ost->enc_ctx->subtitle_header_size = dec->subtitle_header_size;
        }
        if (!av_dict_get(ost->encoder_opts, "threads", NULL, 0))
            av_dict_set(&ost->encoder_opts, "threads", "auto", 0);
        if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
            !codec->defaults &&
            !av_dict_get(ost->encoder_opts, "b", NULL, 0) &&
            !av_dict_get(ost->encoder_opts, "ab", NULL, 0))
            av_dict_set(&ost->encoder_opts, "b", "128000", 0);

        ret = hw_device_setup_for_encode(ost);
        if (ret < 0) {
            snprintf(error, error_len, "Device setup failed for "
                     "encoder on output stream #%d:%d : %s",
                     ost->file_index, ost->index, av_err2str(ret));
            return ret;
        }

        if (ist && ist->dec->type == AVMEDIA_TYPE_SUBTITLE && ost->enc->type == AVMEDIA_TYPE_SUBTITLE) {
            int input_props = 0, output_props = 0;
            AVCodecDescriptor const *input_descriptor =
                avcodec_descriptor_get(dec->codec_id);
            AVCodecDescriptor const *output_descriptor =
                avcodec_descriptor_get(ost->enc_ctx->codec_id);
            if (input_descriptor)
                input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (output_descriptor)
                output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (input_props && output_props && input_props != output_props) {
                snprintf(error, error_len,
                         "Subtitle encoding currently only possible from text to text "
                         "or bitmap to bitmap");
                return AVERROR_INVALIDDATA;
            }
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
        if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
            !(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
            av_buffersink_set_frame_size(ost->filter->filter,
                                            ost->enc_ctx->frame_size);
        assert_avoptions(ost->encoder_opts);
        if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000 &&
            ost->enc_ctx->codec_id != AV_CODEC_ID_CODEC2 /* don't complain about 700 bit/s modes */)
            av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                         " It takes bits/s as argument, not kbits/s\n");

        ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL,
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
                        av_display_rotation_set((uint32_t *)dst, 0);
                }
            }
        }

        // copy timebase while removing common factors
        if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
            ost->st->time_base = av_add_q(ost->enc_ctx->time_base, (AVRational){0, 1});

        // copy estimated duration as a hint to the muxer
        if (ost->st->duration <= 0 && ist && ist->st->duration > 0)
            ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);
    } else if (ost->stream_copy) {
        ret = init_output_stream_streamcopy(ost);
        if (ret < 0)
            return ret;
    }

    // parse user provided disposition, and update stream values
    if (ost->disposition) {
        static const AVOption opts[] = {
            { "disposition"         , NULL, 0, AV_OPT_TYPE_FLAGS, { .i64 = 0 }, INT64_MIN, INT64_MAX, .unit = "flags" },
            { "default"             , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEFAULT           },    .unit = "flags" },
            { "dub"                 , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DUB               },    .unit = "flags" },
            { "original"            , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ORIGINAL          },    .unit = "flags" },
            { "comment"             , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_COMMENT           },    .unit = "flags" },
            { "lyrics"              , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_LYRICS            },    .unit = "flags" },
            { "karaoke"             , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_KARAOKE           },    .unit = "flags" },
            { "forced"              , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_FORCED            },    .unit = "flags" },
            { "hearing_impaired"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_HEARING_IMPAIRED  },    .unit = "flags" },
            { "visual_impaired"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_VISUAL_IMPAIRED   },    .unit = "flags" },
            { "clean_effects"       , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CLEAN_EFFECTS     },    .unit = "flags" },
            { "attached_pic"        , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ATTACHED_PIC      },    .unit = "flags" },
            { "captions"            , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CAPTIONS          },    .unit = "flags" },
            { "descriptions"        , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DESCRIPTIONS      },    .unit = "flags" },
            { "dependent"           , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEPENDENT         },    .unit = "flags" },
            { "metadata"            , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_METADATA          },    .unit = "flags" },
            { NULL },
        };
        static const AVClass class = {
            .class_name = "",
            .item_name  = av_default_item_name,
            .option     = opts,
            .version    = LIBAVUTIL_VERSION_INT,
        };
        const AVClass *pclass = &class;

        ret = av_opt_eval_flags(&pclass, &opts[0], ost->disposition, &ost->st->disposition);
        if (ret < 0)
            return ret;
    }

    /* initialize bitstream filters for the output stream
     * needs to be done here, because the codec id for streamcopy is not
     * known until now */
    ret = init_output_bsfs(ost);
    if (ret < 0)
        return ret;

    ost->initialized = 1;

    ret = check_init_output_file(output_files[ost->file_index], ost->file_index);
    if (ret < 0)
        return ret;

    return ret;
}

static void report_new_stream(int input_index, AVPacket *pkt)
{
    InputFile *file = input_files[input_index];
    AVStream *st = file->ctx->streams[pkt->stream_index];

    if (pkt->stream_index < file->nb_streams_warn)
        return;
    av_log(file->ctx, AV_LOG_WARNING,
           "New %s stream %d:%d at pos:%"PRId64" and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           input_index, pkt->stream_index,
           pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    file->nb_streams_warn = pkt->stream_index + 1;
}

static int transcode_init(void)
{
    int ret = 0, i, j, k;
    AVFormatContext *oc;
    OutputStream *ost;
    InputStream *ist;
    char error[1024] = {0};

    for (i = 0; i < nb_filtergraphs; i++) {
        FilterGraph *fg = filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *ofilter = fg->outputs[j];
            if (!ofilter->ost || ofilter->ost->source_index >= 0)
                continue;
            if (fg->nb_inputs != 1)
                continue;
            for (k = nb_input_streams-1; k >= 0 ; k--)
                if (fg->inputs[0]->ist == input_streams[k])
                    break;
            ofilter->ost->source_index = k;
        }
    }

    /* init framerate emulation */
    for (i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        if (ifile->rate_emu)
            for (j = 0; j < ifile->nb_streams; j++)
                input_streams[j + ifile->ist_index]->start = av_gettime_relative();
    }

    /* init input streams */
    for (i = 0; i < nb_input_streams; i++)
        if ((ret = init_input_stream(i, error, sizeof(error))) < 0) {
            for (i = 0; i < nb_output_streams; i++) {
                ost = output_streams[i];
                avcodec_close(ost->enc_ctx);
            }
            goto dump_format;
        }

    /*
     * initialize stream copy and subtitle/data streams.
     * Encoded AVFrame based streams will get initialized as follows:
     * - when the first AVFrame is received in do_video_out
     * - just before the first AVFrame is received in either transcode_step
     *   or reap_filters due to us requiring the filter chain buffer sink
     *   to be configured with the correct audio frame size, which is only
     *   known after the encoder is initialized.
     */
    for (i = 0; i < nb_output_streams; i++) {
        if (!output_streams[i]->stream_copy &&
            (output_streams[i]->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
             output_streams[i]->enc_ctx->codec_type == AVMEDIA_TYPE_AUDIO))
            continue;

        ret = init_output_stream_wrapper(output_streams[i], NULL, 0);
        if (ret < 0)
            goto dump_format;
    }

    /* discard unused programs */
    for (i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        for (j = 0; j < ifile->ctx->nb_programs; j++) {
            AVProgram *p = ifile->ctx->programs[j];
            int discard  = AVDISCARD_ALL;

            for (k = 0; k < p->nb_stream_indexes; k++)
                if (!input_streams[ifile->ist_index + p->stream_index[k]]->discard) {
                    discard = AVDISCARD_DEFAULT;
                    break;
                }
            p->discard = discard;
        }
    }

    /* write headers for files with no streams */
    for (i = 0; i < nb_output_files; i++) {
        oc = output_files[i]->ctx;
        if (oc->oformat->flags & AVFMT_NOSTREAMS && oc->nb_streams == 0) {
            ret = check_init_output_file(output_files[i], i);
            if (ret < 0)
                goto dump_format;
        }
    }

 dump_format:
    /* dump the stream mapping */
    av_log(NULL, AV_LOG_INFO, "Stream mapping:\n");
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];

        for (j = 0; j < ist->nb_filters; j++) {
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

    for (i = 0; i < nb_output_streams; i++) {
        ost = output_streams[i];

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
                   ost->index, ost->enc ? ost->enc->name : "?");
            continue;
        }

        av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d -> #%d:%d",
               input_streams[ost->source_index]->file_index,
               input_streams[ost->source_index]->st->index,
               ost->file_index,
               ost->index);
        if (ost->sync_ist != input_streams[ost->source_index])
            av_log(NULL, AV_LOG_INFO, " [sync #%d:%d]",
                   ost->sync_ist->file_index,
                   ost->sync_ist->st->index);
        if (ost->stream_copy)
            av_log(NULL, AV_LOG_INFO, " (copy)");
        else {
            const AVCodec *in_codec    = input_streams[ost->source_index]->dec;
            const AVCodec *out_codec   = ost->enc;
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
        }
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
    int i;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost    = output_streams[i];
        OutputFile *of       = output_files[ost->file_index];
        AVFormatContext *os  = output_files[ost->file_index]->ctx;

        if (ost->finished ||
            (os->pb && avio_tell(os->pb) >= of->limit_filesize))
            continue;
        if (ost->frame_number >= ost->max_frames) {
            int j;
            for (j = 0; j < of->ctx->nb_streams; j++)
                close_output_stream(output_streams[of->ost_index + j]);
            continue;
        }

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
    int i;
    int64_t opts_min = INT64_MAX;
    OutputStream *ost_min = NULL;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];
        int64_t opts = ost->st->cur_dts == AV_NOPTS_VALUE ? INT64_MIN :
                       av_rescale_q(ost->st->cur_dts, ost->st->time_base,
                                    AV_TIME_BASE_Q);
        if (ost->st->cur_dts == AV_NOPTS_VALUE)
            av_log(NULL, AV_LOG_DEBUG,
                "cur_dts is invalid st:%d (%d) [init:%d i_done:%d finish:%d] (this is harmless if it occurs once at the start per stream)\n",
                ost->st->index, ost->st->id, ost->initialized, ost->inputs_done, ost->finished);

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
    if(cur_time - last_time >= 100000 && !run_as_daemon){
        key =  read_key();
        last_time = cur_time;
    }else
        key = -1;
    if (key == 'q')
        return AVERROR_EXIT;
    if (key == '+') av_log_set_level(av_log_get_level()+10);
    if (key == '-') av_log_set_level(av_log_get_level()-10);
    if (key == 's') qp_hist     ^= 1;
    if (key == 'h'){
        if (do_hex_dump){
            do_hex_dump = do_pkt_dump = 0;
        } else if(do_pkt_dump){
            do_hex_dump = 1;
        } else
            do_pkt_dump = 1;
        av_log_set_level(AV_LOG_DEBUG);
    }
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
            debug = input_streams[0]->dec_ctx->debug << 1;
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
        for(i=0;i<nb_input_streams;i++) {
            input_streams[i]->dec_ctx->debug = debug;
        }
        for(i=0;i<nb_output_streams;i++) {
            OutputStream *ost = output_streams[i];
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

#if HAVE_THREADS
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

static void free_input_thread(int i)
{
    InputFile *f = input_files[i];
    AVPacket *pkt;

    if (!f || !f->in_thread_queue)
        return;
    av_thread_message_queue_set_err_send(f->in_thread_queue, AVERROR_EOF);
    while (av_thread_message_queue_recv(f->in_thread_queue, &pkt, 0) >= 0)
        av_packet_free(&pkt);

    pthread_join(f->thread, NULL);
    f->joined = 1;
    av_thread_message_queue_free(&f->in_thread_queue);
}

static void free_input_threads(void)
{
    int i;

    for (i = 0; i < nb_input_files; i++)
        free_input_thread(i);
}

static int init_input_thread(int i)
{
    int ret;
    InputFile *f = input_files[i];

    if (f->thread_queue_size < 0)
        f->thread_queue_size = (nb_input_files > 1 ? 8 : 0);
    if (!f->thread_queue_size)
        return 0;

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

static int init_input_threads(void)
{
    int i, ret;

    for (i = 0; i < nb_input_files; i++) {
        ret = init_input_thread(i);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int get_input_packet_mt(InputFile *f, AVPacket **pkt)
{
    return av_thread_message_queue_recv(f->in_thread_queue, pkt,
                                        f->non_blocking ?
                                        AV_THREAD_MESSAGE_NONBLOCK : 0);
}
#endif

static int get_input_packet(InputFile *f, AVPacket **pkt)
{
    if (f->rate_emu) {
        int i;
        for (i = 0; i < f->nb_streams; i++) {
            InputStream *ist = input_streams[f->ist_index + i];
            int64_t pts = av_rescale(ist->dts, 1000000, AV_TIME_BASE);
            int64_t now = av_gettime_relative() - ist->start;
            if (pts > now)
                return AVERROR(EAGAIN);
        }
    }

#if HAVE_THREADS
    if (f->thread_queue_size)
        return get_input_packet_mt(f, pkt);
#endif
    *pkt = f->pkt;
    return av_read_frame(f->ctx, *pkt);
}

static int got_eagain(void)
{
    int i;
    for (i = 0; i < nb_output_streams; i++)
        if (output_streams[i]->unavailable)
            return 1;
    return 0;
}

static void reset_eagain(void)
{
    int i;
    for (i = 0; i < nb_input_files; i++)
        input_files[i]->eagain = 0;
    for (i = 0; i < nb_output_streams; i++)
        output_streams[i]->unavailable = 0;
}

// set duration to max(tmp, duration) in a proper time base and return duration's time_base
static AVRational duration_max(int64_t tmp, int64_t *duration, AVRational tmp_time_base,
                               AVRational time_base)
{
    int ret;

    if (!*duration) {
        *duration = tmp;
        return tmp_time_base;
    }

    ret = av_compare_ts(*duration, time_base, tmp, tmp_time_base);
    if (ret < 0) {
        *duration = tmp;
        return tmp_time_base;
    }

    return time_base;
}

static int seek_to_start(InputFile *ifile, AVFormatContext *is)
{
    InputStream *ist;
    AVCodecContext *avctx;
    int i, ret, has_audio = 0;
    int64_t duration = 0;

    ret = avformat_seek_file(is, -1, INT64_MIN, is->start_time, is->start_time, 0);
    if (ret < 0)
        return ret;

    for (i = 0; i < ifile->nb_streams; i++) {
        ist   = input_streams[ifile->ist_index + i];
        avctx = ist->dec_ctx;

        /* duration is the length of the last frame in a stream
         * when audio stream is present we don't care about
         * last video frame length because it's not defined exactly */
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && ist->nb_samples)
            has_audio = 1;
    }

    for (i = 0; i < ifile->nb_streams; i++) {
        ist   = input_streams[ifile->ist_index + i];
        avctx = ist->dec_ctx;

        if (has_audio) {
            if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && ist->nb_samples) {
                AVRational sample_rate = {1, avctx->sample_rate};

                duration = av_rescale_q(ist->nb_samples, sample_rate, ist->st->time_base);
            } else {
                continue;
            }
        } else {
            if (ist->framerate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->framerate), ist->st->time_base);
            } else if (ist->st->avg_frame_rate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate), ist->st->time_base);
            } else {
                duration = 1;
            }
        }
        if (!ifile->duration)
            ifile->time_base = ist->st->time_base;
        /* the total duration of the stream, max_pts - min_pts is
         * the duration of the stream without the last frame */
        if (ist->max_pts > ist->min_pts && ist->max_pts - (uint64_t)ist->min_pts < INT64_MAX - duration)
            duration += ist->max_pts - ist->min_pts;
        ifile->time_base = duration_max(duration, &ifile->duration, ist->st->time_base,
                                        ifile->time_base);
    }

    if (ifile->loop > 0)
        ifile->loop--;

    return ret;
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
    int ret, thread_ret, i, j;
    int64_t duration;
    int64_t pkt_dts;
    int disable_discontinuity_correction = copy_ts;

    is  = ifile->ctx;
    ret = get_input_packet(ifile, &pkt);

    if (ret == AVERROR(EAGAIN)) {
        ifile->eagain = 1;
        return ret;
    }
    if (ret < 0 && ifile->loop) {
        AVCodecContext *avctx;
        for (i = 0; i < ifile->nb_streams; i++) {
            ist = input_streams[ifile->ist_index + i];
            avctx = ist->dec_ctx;
            if (ist->decoding_needed) {
                ret = process_input_packet(ist, NULL, 1);
                if (ret>0)
                    return 0;
                avcodec_flush_buffers(avctx);
            }
        }
#if HAVE_THREADS
        free_input_thread(file_index);
#endif
        ret = seek_to_start(ifile, is);
#if HAVE_THREADS
        thread_ret = init_input_thread(file_index);
        if (thread_ret < 0)
            return thread_ret;
#endif
        if (ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Seek to start failed.\n");
        else
            ret = get_input_packet(ifile, &pkt);
        if (ret == AVERROR(EAGAIN)) {
            ifile->eagain = 1;
            return ret;
        }
    }
    if (ret < 0) {
        if (ret != AVERROR_EOF) {
            print_error(is->url, ret);
            if (exit_on_error)
                exit_program(1);
        }

        for (i = 0; i < ifile->nb_streams; i++) {
            ist = input_streams[ifile->ist_index + i];
            if (ist->decoding_needed) {
                ret = process_input_packet(ist, NULL, 0);
                if (ret>0)
                    return 0;
            }

            /* mark all outputs that don't go through lavfi as finished */
            for (j = 0; j < nb_output_streams; j++) {
                OutputStream *ost = output_streams[j];

                if (ost->source_index == ifile->ist_index + i &&
                    (ost->stream_copy || ost->enc->type == AVMEDIA_TYPE_SUBTITLE))
                    finish_output_stream(ost);
            }
        }

        ifile->eof_reached = 1;
        return AVERROR(EAGAIN);
    }

    reset_eagain();

    if (do_pkt_dump) {
        av_pkt_dump_log2(NULL, AV_LOG_INFO, pkt, do_hex_dump,
                         is->streams[pkt->stream_index]);
    }
    /* the following test is needed in case new streams appear
       dynamically in stream : we ignore them */
    if (pkt->stream_index >= ifile->nb_streams) {
        report_new_stream(file_index, pkt);
        goto discard_packet;
    }

    ist = input_streams[ifile->ist_index + pkt->stream_index];

    ist->data_size += pkt->size;
    ist->nb_packets++;

    if (ist->discard)
        goto discard_packet;

    if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
        av_log(NULL, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
               "%s: corrupt input packet in stream %d\n", is->url, pkt->stream_index);
        if (exit_on_error)
            exit_program(1);
    }

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "demuxer -> ist_index:%d type:%s "
               "next_dts:%s next_dts_time:%s next_pts:%s next_pts_time:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%s off_time:%s\n",
               ifile->ist_index + pkt->stream_index, av_get_media_type_string(ist->dec_ctx->codec_type),
               av_ts2str(ist->next_dts), av_ts2timestr(ist->next_dts, &AV_TIME_BASE_Q),
               av_ts2str(ist->next_pts), av_ts2timestr(ist->next_pts, &AV_TIME_BASE_Q),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ist->st->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ist->st->time_base),
               av_ts2str(input_files[ist->file_index]->ts_offset),
               av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    }

    if(!ist->wrap_correction_done && is->start_time != AV_NOPTS_VALUE && ist->st->pts_wrap_bits < 64){
        int64_t stime, stime2;
        // Correcting starttime based on the enabled streams
        // FIXME this ideally should be done before the first use of starttime but we do not know which are the enabled streams at that point.
        //       so we instead do it here as part of discontinuity handling
        if (   ist->next_dts == AV_NOPTS_VALUE
            && ifile->ts_offset == -is->start_time
            && (is->iformat->flags & AVFMT_TS_DISCONT)) {
            int64_t new_start_time = INT64_MAX;
            for (i=0; i<is->nb_streams; i++) {
                AVStream *st = is->streams[i];
                if(st->discard == AVDISCARD_ALL || st->start_time == AV_NOPTS_VALUE)
                    continue;
                new_start_time = FFMIN(new_start_time, av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q));
            }
            if (new_start_time > is->start_time) {
                av_log(is, AV_LOG_VERBOSE, "Correcting start time by %"PRId64"\n", new_start_time - is->start_time);
                ifile->ts_offset = -new_start_time;
            }
        }

        stime = av_rescale_q(is->start_time, AV_TIME_BASE_Q, ist->st->time_base);
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
                exit_program(1);

            memcpy(dst_data, src_sd->data, src_sd->size);
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

    pkt_dts = av_rescale_q_rnd(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    if ((ist->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
        pkt_dts != AV_NOPTS_VALUE && ist->next_dts == AV_NOPTS_VALUE && !copy_ts
        && (is->iformat->flags & AVFMT_TS_DISCONT) && ifile->last_ts != AV_NOPTS_VALUE) {
        int64_t delta   = pkt_dts - ifile->last_ts;
        if (delta < -1LL*dts_delta_threshold*AV_TIME_BASE ||
            delta >  1LL*dts_delta_threshold*AV_TIME_BASE){
            ifile->ts_offset -= delta;
            av_log(NULL, AV_LOG_DEBUG,
                   "Inter stream timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                   delta, ifile->ts_offset);
            pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            if (pkt->pts != AV_NOPTS_VALUE)
                pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
        }
    }

    duration = av_rescale_q(ifile->duration, ifile->time_base, ist->st->time_base);
    if (pkt->pts != AV_NOPTS_VALUE) {
        pkt->pts += duration;
        ist->max_pts = FFMAX(pkt->pts, ist->max_pts);
        ist->min_pts = FFMIN(pkt->pts, ist->min_pts);
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += duration;

    pkt_dts = av_rescale_q_rnd(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);

    if (copy_ts && pkt_dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE &&
        (is->iformat->flags & AVFMT_TS_DISCONT) && ist->st->pts_wrap_bits < 60) {
        int64_t wrap_dts = av_rescale_q_rnd(pkt->dts + (1LL<<ist->st->pts_wrap_bits),
                                            ist->st->time_base, AV_TIME_BASE_Q,
                                            AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        if (FFABS(wrap_dts - ist->next_dts) < FFABS(pkt_dts - ist->next_dts)/10)
            disable_discontinuity_correction = 0;
    }

    if ((ist->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
         pkt_dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE &&
        !disable_discontinuity_correction) {
        int64_t delta   = pkt_dts - ist->next_dts;
        if (is->iformat->flags & AVFMT_TS_DISCONT) {
            if (delta < -1LL*dts_delta_threshold*AV_TIME_BASE ||
                delta >  1LL*dts_delta_threshold*AV_TIME_BASE ||
                pkt_dts + AV_TIME_BASE/10 < FFMAX(ist->pts, ist->dts)) {
                ifile->ts_offset -= delta;
                av_log(NULL, AV_LOG_DEBUG,
                       "timestamp discontinuity for stream #%d:%d "
                       "(id=%d, type=%s): %"PRId64", new offset= %"PRId64"\n",
                       ist->file_index, ist->st->index, ist->st->id,
                       av_get_media_type_string(ist->dec_ctx->codec_type),
                       delta, ifile->ts_offset);
                pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
        } else {
            if ( delta < -1LL*dts_error_threshold*AV_TIME_BASE ||
                 delta >  1LL*dts_error_threshold*AV_TIME_BASE) {
                av_log(NULL, AV_LOG_WARNING, "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n", pkt->dts, ist->next_dts, pkt->stream_index);
                pkt->dts = AV_NOPTS_VALUE;
            }
            if (pkt->pts != AV_NOPTS_VALUE){
                int64_t pkt_pts = av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
                delta   = pkt_pts - ist->next_dts;
                if ( delta < -1LL*dts_error_threshold*AV_TIME_BASE ||
                     delta >  1LL*dts_error_threshold*AV_TIME_BASE) {
                    av_log(NULL, AV_LOG_WARNING, "PTS %"PRId64", next:%"PRId64" invalid dropping st:%d\n", pkt->pts, ist->next_dts, pkt->stream_index);
                    pkt->pts = AV_NOPTS_VALUE;
                }
            }
        }
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        ifile->last_ts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%s off_time:%s\n",
               ifile->ist_index + pkt->stream_index, av_get_media_type_string(ist->dec_ctx->codec_type),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ist->st->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ist->st->time_base),
               av_ts2str(input_files[ist->file_index]->ts_offset),
               av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    }

    sub2video_heartbeat(ist, pkt->pts);

    process_input_packet(ist, pkt, 0);

discard_packet:
#if HAVE_THREADS
    if (ifile->thread_queue_size)
        av_packet_free(&pkt);
    else
#endif
    av_packet_unref(pkt);

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
        av_assert0(ost->source_index >= 0);
        ist = input_streams[ost->source_index];
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
    AVFormatContext *os;
    OutputStream *ost;
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

#if HAVE_THREADS
    if ((ret = init_input_threads()) < 0)
        goto fail;
#endif

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
#if HAVE_THREADS
    free_input_threads();
#endif

    /* at the end of stream, we must flush the decoder buffers */
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];
        if (!input_files[ist->file_index]->eof_reached) {
            process_input_packet(ist, NULL, 0);
        }
    }
    flush_encoders();

    term_exit();

    /* write the trailer if needed and close file */
    for (i = 0; i < nb_output_files; i++) {
        os = output_files[i]->ctx;
        if (!output_files[i]->header_written) {
            av_log(NULL, AV_LOG_ERROR,
                   "Nothing was written into output file %d (%s), because "
                   "at least one of its streams received no packets.\n",
                   i, os->url);
            continue;
        }
        if ((ret = av_write_trailer(os)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error writing trailer of %s: %s\n", os->url, av_err2str(ret));
            if (exit_on_error)
                exit_program(1);
        }
    }

    /* dump report by using the first video and audio streams */
    print_report(1, timer_start, av_gettime_relative());

    /* close each encoder */
    for (i = 0; i < nb_output_streams; i++) {
        ost = output_streams[i];
        if (ost->encoding_needed) {
            av_freep(&ost->enc_ctx->stats_in);
        }
        total_packets_written += ost->packets_written;
        if (!ost->packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM)) {
            av_log(NULL, AV_LOG_FATAL, "Empty output on stream %d.\n", i);
            exit_program(1);
        }
    }

    if (!total_packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT)) {
        av_log(NULL, AV_LOG_FATAL, "Empty output\n");
        exit_program(1);
    }

    /* close each decoder */
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];
        if (ist->decoding_needed) {
            avcodec_close(ist->dec_ctx);
            if (ist->hwaccel_uninit)
                ist->hwaccel_uninit(ist->dec_ctx);
        }
    }

    hw_device_free_all();

    /* finished ! */
    ret = 0;

 fail:
#if HAVE_THREADS
    free_input_threads();
#endif

    if (output_streams) {
        for (i = 0; i < nb_output_streams; i++) {
            ost = output_streams[i];
            if (ost) {
                if (ost->logfile) {
                    if (fclose(ost->logfile))
                        av_log(NULL, AV_LOG_ERROR,
                               "Error closing logfile, loss of information possible: %s\n",
                               av_err2str(AVERROR(errno)));
                    ost->logfile = NULL;
                }
                av_freep(&ost->forced_kf_pts);
                av_freep(&ost->apad);
                av_freep(&ost->disposition);
                av_dict_free(&ost->encoder_opts);
                av_dict_free(&ost->sws_dict);
                av_dict_free(&ost->swr_opts);
                av_dict_free(&ost->resample_opts);
            }
        }
    }
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

static void log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{
}

int main(int argc, char **argv)
{
    int i, ret;
    BenchmarkTimeStamps ti;

    init_dynload();

    register_exit(ffmpeg_cleanup);

    setvbuf(stderr,NULL,_IONBF,0); /* win32 runtime needs this */

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    if(argc>1 && !strcmp(argv[1], "-d")){
        run_as_daemon=1;
        av_log_set_callback(log_callback_null);
        argc--;
        argv++;
    }

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

    for (i = 0; i < nb_output_files; i++) {
        if (strcmp(output_files[i]->ctx->oformat->name, "rtp"))
            want_sdp = 0;
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
