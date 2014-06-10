/*
 * avconv main
 * Copyright (c) 2000-2011 The libav developers.
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>

#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavresample/avresample.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/libm.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
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

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if HAVE_PTHREADS
#include <pthread.h>
#endif

#include <time.h>

#include "avconv.h"
#include "cmdutils.h"

#include "libavutil/avassert.h"

const char program_name[] = "avconv";
const int program_birth_year = 2000;

static FILE *vstats_file;

static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;



#if HAVE_PTHREADS
/* signal to input threads that they should exit; set by the main thread */
static int transcoding_finished;
#endif

#define DEFAULT_PASS_LOGFILENAME_PREFIX "av2pass"

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

static void term_exit(void)
{
    av_log(NULL, AV_LOG_QUIET, "");
}

static volatile int received_sigterm = 0;
static volatile int received_nb_signals = 0;

static void
sigterm_handler(int sig)
{
    received_sigterm = sig;
    received_nb_signals++;
    term_exit();
}

static void term_init(void)
{
    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
#ifdef SIGXCPU
    signal(SIGXCPU, sigterm_handler);
#endif
}

static int decode_interrupt_cb(void *ctx)
{
    return received_nb_signals > 1;
}

const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };

static void avconv_cleanup(int ret)
{
    int i, j;

    for (i = 0; i < nb_filtergraphs; i++) {
        avfilter_graph_free(&filtergraphs[i]->graph);
        for (j = 0; j < filtergraphs[i]->nb_inputs; j++) {
            av_freep(&filtergraphs[i]->inputs[j]->name);
            av_freep(&filtergraphs[i]->inputs[j]);
        }
        av_freep(&filtergraphs[i]->inputs);
        for (j = 0; j < filtergraphs[i]->nb_outputs; j++) {
            av_freep(&filtergraphs[i]->outputs[j]->name);
            av_freep(&filtergraphs[i]->outputs[j]);
        }
        av_freep(&filtergraphs[i]->outputs);
        av_freep(&filtergraphs[i]->graph_desc);
        av_freep(&filtergraphs[i]);
    }
    av_freep(&filtergraphs);

    /* close files */
    for (i = 0; i < nb_output_files; i++) {
        AVFormatContext *s = output_files[i]->ctx;
        if (s && s->oformat && !(s->oformat->flags & AVFMT_NOFILE) && s->pb)
            avio_close(s->pb);
        avformat_free_context(s);
        av_dict_free(&output_files[i]->opts);
        av_freep(&output_files[i]);
    }
    for (i = 0; i < nb_output_streams; i++) {
        AVBitStreamFilterContext *bsfc = output_streams[i]->bitstream_filters;
        while (bsfc) {
            AVBitStreamFilterContext *next = bsfc->next;
            av_bitstream_filter_close(bsfc);
            bsfc = next;
        }
        output_streams[i]->bitstream_filters = NULL;
        av_frame_free(&output_streams[i]->filtered_frame);

        av_parser_close(output_streams[i]->parser);

        av_freep(&output_streams[i]->forced_keyframes);
        av_freep(&output_streams[i]->avfilter);
        av_freep(&output_streams[i]->logfile_prefix);
        av_freep(&output_streams[i]);
    }
    for (i = 0; i < nb_input_files; i++) {
        avformat_close_input(&input_files[i]->ctx);
        av_freep(&input_files[i]);
    }
    for (i = 0; i < nb_input_streams; i++) {
        av_frame_free(&input_streams[i]->decoded_frame);
        av_frame_free(&input_streams[i]->filter_frame);
        av_dict_free(&input_streams[i]->opts);
        av_freep(&input_streams[i]->filters);
        av_freep(&input_streams[i]->hwaccel_device);
        av_freep(&input_streams[i]);
    }

    if (vstats_file)
        fclose(vstats_file);
    av_free(vstats_filename);

    av_freep(&input_streams);
    av_freep(&input_files);
    av_freep(&output_streams);
    av_freep(&output_files);

    uninit_opts();

    avformat_network_deinit();

    if (received_sigterm) {
        av_log(NULL, AV_LOG_INFO, "Received signal %d: terminating.\n",
               (int) received_sigterm);
        exit (255);
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

static void abort_codec_experimental(AVCodec *c, int encoder)
{
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;
    av_log(NULL, AV_LOG_FATAL, "%s '%s' is experimental and might produce bad "
            "results.\nAdd '-strict experimental' if you want to use it.\n",
            codec_string, c->name);
    codec = encoder ? avcodec_find_encoder(c->id) : avcodec_find_decoder(c->id);
    if (!(codec->capabilities & CODEC_CAP_EXPERIMENTAL))
        av_log(NULL, AV_LOG_FATAL, "Or use the non experimental %s '%s'.\n",
               codec_string, codec->name);
    exit_program(1);
}

/*
 * Update the requested input sample format based on the output sample format.
 * This is currently only used to request float output from decoders which
 * support multiple sample formats, one of which is AV_SAMPLE_FMT_FLT.
 * Ideally this will be removed in the future when decoders do not do format
 * conversion and only output in their native format.
 */
static void update_sample_fmt(AVCodecContext *dec, AVCodec *dec_codec,
                              AVCodecContext *enc)
{
    /* if sample formats match or a decoder sample format has already been
       requested, just return */
    if (enc->sample_fmt == dec->sample_fmt ||
        dec->request_sample_fmt > AV_SAMPLE_FMT_NONE)
        return;

    /* if decoder supports more than one output format */
    if (dec_codec && dec_codec->sample_fmts &&
        dec_codec->sample_fmts[0] != AV_SAMPLE_FMT_NONE &&
        dec_codec->sample_fmts[1] != AV_SAMPLE_FMT_NONE) {
        const enum AVSampleFormat *p;
        int min_dec = INT_MAX, min_inc = INT_MAX;
        enum AVSampleFormat dec_fmt = AV_SAMPLE_FMT_NONE;
        enum AVSampleFormat inc_fmt = AV_SAMPLE_FMT_NONE;

        /* find a matching sample format in the encoder */
        for (p = dec_codec->sample_fmts; *p != AV_SAMPLE_FMT_NONE; p++) {
            if (*p == enc->sample_fmt) {
                dec->request_sample_fmt = *p;
                return;
            } else {
                enum AVSampleFormat dfmt = av_get_packed_sample_fmt(*p);
                enum AVSampleFormat efmt = av_get_packed_sample_fmt(enc->sample_fmt);
                int fmt_diff = 32 * abs(dfmt - efmt);
                if (av_sample_fmt_is_planar(*p) !=
                    av_sample_fmt_is_planar(enc->sample_fmt))
                    fmt_diff++;
                if (dfmt == efmt) {
                    min_inc = fmt_diff;
                    inc_fmt = *p;
                } else if (dfmt > efmt) {
                    if (fmt_diff < min_inc) {
                        min_inc = fmt_diff;
                        inc_fmt = *p;
                    }
                } else {
                    if (fmt_diff < min_dec) {
                        min_dec = fmt_diff;
                        dec_fmt = *p;
                    }
                }
            }
        }

        /* if none match, provide the one that matches quality closest */
        dec->request_sample_fmt = min_inc != INT_MAX ? inc_fmt : dec_fmt;
    }
}

static void write_frame(AVFormatContext *s, AVPacket *pkt, OutputStream *ost)
{
    AVBitStreamFilterContext *bsfc = ost->bitstream_filters;
    AVCodecContext          *avctx = ost->st->codec;
    int ret;

    /*
     * Audio encoders may split the packets --  #frames in != #packets out.
     * But there is no reordering, so we can limit the number of output packets
     * by simply dropping them here.
     * Counting encoded video frames needs to be done separately because of
     * reordering, see do_video_out()
     */
    if (!(avctx->codec_type == AVMEDIA_TYPE_VIDEO && avctx->codec)) {
        if (ost->frame_number >= ost->max_frames) {
            av_free_packet(pkt);
            return;
        }
        ost->frame_number++;
    }

    while (bsfc) {
        AVPacket new_pkt = *pkt;
        int a = av_bitstream_filter_filter(bsfc, avctx, NULL,
                                           &new_pkt.data, &new_pkt.size,
                                           pkt->data, pkt->size,
                                           pkt->flags & AV_PKT_FLAG_KEY);
        if (a > 0) {
            av_free_packet(pkt);
            new_pkt.buf = av_buffer_create(new_pkt.data, new_pkt.size,
                                           av_buffer_default_free, NULL, 0);
            if (!new_pkt.buf)
                exit_program(1);
        } else if (a < 0) {
            av_log(NULL, AV_LOG_ERROR, "%s failed for stream %d, codec %s",
                   bsfc->filter->name, pkt->stream_index,
                   avctx->codec ? avctx->codec->name : "copy");
            print_error("", a);
            if (exit_on_error)
                exit_program(1);
        }
        *pkt = new_pkt;

        bsfc = bsfc->next;
    }

    if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS) &&
        ost->last_mux_dts != AV_NOPTS_VALUE &&
        pkt->dts < ost->last_mux_dts + !(s->oformat->flags & AVFMT_TS_NONSTRICT)) {
        av_log(NULL, AV_LOG_WARNING, "Non-monotonous DTS in output stream "
               "%d:%d; previous: %"PRId64", current: %"PRId64"; ",
               ost->file_index, ost->st->index, ost->last_mux_dts, pkt->dts);
        if (exit_on_error) {
            av_log(NULL, AV_LOG_FATAL, "aborting.\n");
            exit_program(1);
        }
        av_log(NULL, AV_LOG_WARNING, "changing to %"PRId64". This may result "
               "in incorrect timestamps in the output file.\n",
               ost->last_mux_dts + 1);
        pkt->dts = ost->last_mux_dts + 1;
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts = FFMAX(pkt->pts, pkt->dts);
    }
    ost->last_mux_dts = pkt->dts;

    pkt->stream_index = ost->index;
    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0) {
        print_error("av_interleaved_write_frame()", ret);
        exit_program(1);
    }
}

static int check_recording_time(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ost->sync_opts - ost->first_pts, ost->st->codec->time_base, of->recording_time,
                      AV_TIME_BASE_Q) >= 0) {
        ost->finished = 1;
        return 0;
    }
    return 1;
}

static void do_audio_out(AVFormatContext *s, OutputStream *ost,
                         AVFrame *frame)
{
    AVCodecContext *enc = ost->st->codec;
    AVPacket pkt;
    int got_packet = 0;

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (frame->pts == AV_NOPTS_VALUE || audio_sync_method < 0)
        frame->pts = ost->sync_opts;
    ost->sync_opts = frame->pts + frame->nb_samples;

    if (avcodec_encode_audio2(enc, &pkt, frame, &got_packet) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Audio encoding failed\n");
        exit_program(1);
    }

    if (got_packet) {
        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts      = av_rescale_q(pkt.pts,      enc->time_base, ost->st->time_base);
        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts      = av_rescale_q(pkt.dts,      enc->time_base, ost->st->time_base);
        if (pkt.duration > 0)
            pkt.duration = av_rescale_q(pkt.duration, enc->time_base, ost->st->time_base);

        write_frame(s, &pkt, ost);

        audio_size += pkt.size;
    }
}

static void do_subtitle_out(AVFormatContext *s,
                            OutputStream *ost,
                            InputStream *ist,
                            AVSubtitle *sub,
                            int64_t pts)
{
    static uint8_t *subtitle_out = NULL;
    int subtitle_out_max_size = 1024 * 1024;
    int subtitle_out_size, nb, i;
    AVCodecContext *enc;
    AVPacket pkt;

    if (pts == AV_NOPTS_VALUE) {
        av_log(NULL, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        if (exit_on_error)
            exit_program(1);
        return;
    }

    enc = ost->st->codec;

    if (!subtitle_out) {
        subtitle_out = av_malloc(subtitle_out_max_size);
    }

    /* Note: DVB subtitle need one packet to draw them and one other
       packet to clear them */
    /* XXX: signal it in the codec context ? */
    if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        nb = 2;
    else
        nb = 1;

    for (i = 0; i < nb; i++) {
        ost->sync_opts = av_rescale_q(pts, ist->st->time_base, enc->time_base);
        if (!check_recording_time(ost))
            return;

        sub->pts = av_rescale_q(pts, ist->st->time_base, AV_TIME_BASE_Q);
        // start_display_time is required to be 0
        sub->pts               += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, AV_TIME_BASE_Q);
        sub->end_display_time  -= sub->start_display_time;
        sub->start_display_time = 0;
        subtitle_out_size = avcodec_encode_subtitle(enc, subtitle_out,
                                                    subtitle_out_max_size, sub);
        if (subtitle_out_size < 0) {
            av_log(NULL, AV_LOG_FATAL, "Subtitle encoding failed\n");
            exit_program(1);
        }

        av_init_packet(&pkt);
        pkt.data = subtitle_out;
        pkt.size = subtitle_out_size;
        pkt.pts  = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->st->time_base);
        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt.pts += 90 * sub->start_display_time;
            else
                pkt.pts += 90 * sub->end_display_time;
        }
        write_frame(s, &pkt, ost);
    }
}

static void do_video_out(AVFormatContext *s,
                         OutputStream *ost,
                         AVFrame *in_picture,
                         int *frame_size)
{
    int ret, format_video_sync;
    AVPacket pkt;
    AVCodecContext *enc = ost->st->codec;

    *frame_size = 0;

    format_video_sync = video_sync_method;
    if (format_video_sync == VSYNC_AUTO)
        format_video_sync = (s->oformat->flags & AVFMT_NOTIMESTAMPS) ? VSYNC_PASSTHROUGH :
                            (s->oformat->flags & AVFMT_VARIABLE_FPS) ? VSYNC_VFR : VSYNC_CFR;
    if (format_video_sync != VSYNC_PASSTHROUGH &&
        ost->frame_number &&
        in_picture->pts != AV_NOPTS_VALUE &&
        in_picture->pts < ost->sync_opts) {
        nb_frames_drop++;
        av_log(NULL, AV_LOG_VERBOSE, "*** drop!\n");
        return;
    }

    if (in_picture->pts == AV_NOPTS_VALUE)
        in_picture->pts = ost->sync_opts;
    ost->sync_opts = in_picture->pts;


    if (!ost->frame_number)
        ost->first_pts = in_picture->pts;

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (ost->frame_number >= ost->max_frames)
        return;

    if (s->oformat->flags & AVFMT_RAWPICTURE &&
        enc->codec->id == AV_CODEC_ID_RAWVIDEO) {
        /* raw pictures are written as AVPicture structure to
           avoid any copies. We support temporarily the older
           method. */
        enc->coded_frame->interlaced_frame = in_picture->interlaced_frame;
        enc->coded_frame->top_field_first  = in_picture->top_field_first;
        pkt.data   = (uint8_t *)in_picture;
        pkt.size   =  sizeof(AVPicture);
        pkt.pts    = av_rescale_q(in_picture->pts, enc->time_base, ost->st->time_base);
        pkt.flags |= AV_PKT_FLAG_KEY;

        write_frame(s, &pkt, ost);
    } else {
        int got_packet;

        if (ost->st->codec->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME) &&
            ost->top_field_first >= 0)
            in_picture->top_field_first = !!ost->top_field_first;

        in_picture->quality = ost->st->codec->global_quality;
        if (!enc->me_threshold)
            in_picture->pict_type = 0;
        if (ost->forced_kf_index < ost->forced_kf_count &&
            in_picture->pts >= ost->forced_kf_pts[ost->forced_kf_index]) {
            in_picture->pict_type = AV_PICTURE_TYPE_I;
            ost->forced_kf_index++;
        }
        ret = avcodec_encode_video2(enc, &pkt, in_picture, &got_packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
            exit_program(1);
        }

        if (got_packet) {
            if (pkt.pts != AV_NOPTS_VALUE)
                pkt.pts = av_rescale_q(pkt.pts, enc->time_base, ost->st->time_base);
            if (pkt.dts != AV_NOPTS_VALUE)
                pkt.dts = av_rescale_q(pkt.dts, enc->time_base, ost->st->time_base);

            write_frame(s, &pkt, ost);
            *frame_size = pkt.size;
            video_size += pkt.size;

            /* if two pass, output log */
            if (ost->logfile && enc->stats_out) {
                fprintf(ost->logfile, "%s", enc->stats_out);
            }
        }
    }
    ost->sync_opts++;
    /*
     * For video, number of frames in == number of packets out.
     * But there may be reordering, so we can't throw away frames on encoder
     * flush, we need to limit them here, before they go into encoder.
     */
    ost->frame_number++;
}

static double psnr(double d)
{
    return -10.0 * log(d) / log(10.0);
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

    enc = ost->st->codec;
    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        frame_number = ost->frame_number;
        fprintf(vstats_file, "frame= %5d q= %2.1f ", frame_number, enc->coded_frame->quality / (float)FF_QP2LAMBDA);
        if (enc->flags&CODEC_FLAG_PSNR)
            fprintf(vstats_file, "PSNR= %6.2f ", psnr(enc->coded_frame->error[0] / (enc->width * enc->height * 255.0 * 255.0)));

        fprintf(vstats_file,"f_size= %6d ", frame_size);
        /* compute pts value */
        ti1 = ost->sync_opts * av_q2d(enc->time_base);
        if (ti1 < 0.01)
            ti1 = 0.01;

        bitrate     = (frame_size * 8) / av_q2d(enc->time_base) / 1000.0;
        avg_bitrate = (double)(video_size * 8) / ti1 / 1000.0;
        fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
               (double)video_size / 1024, ti1, bitrate, avg_bitrate);
        fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(enc->coded_frame->pict_type));
    }
}

/*
 * Read one frame for lavfi output for ost and encode it.
 */
static int poll_filter(OutputStream *ost)
{
    OutputFile    *of = output_files[ost->file_index];
    AVFrame *filtered_frame = NULL;
    int frame_size, ret;

    if (!ost->filtered_frame && !(ost->filtered_frame = av_frame_alloc())) {
        return AVERROR(ENOMEM);
    }
    filtered_frame = ost->filtered_frame;

    if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
        !(ost->enc->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE))
        ret = av_buffersink_get_samples(ost->filter->filter, filtered_frame,
                                         ost->st->codec->frame_size);
    else
        ret = av_buffersink_get_frame(ost->filter->filter, filtered_frame);

    if (ret < 0)
        return ret;

    if (filtered_frame->pts != AV_NOPTS_VALUE) {
        int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
        filtered_frame->pts = av_rescale_q(filtered_frame->pts,
                                           ost->filter->filter->inputs[0]->time_base,
                                           ost->st->codec->time_base) -
                              av_rescale_q(start_time,
                                           AV_TIME_BASE_Q,
                                           ost->st->codec->time_base);
    }

    switch (ost->filter->filter->inputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        if (!ost->frame_aspect_ratio)
            ost->st->codec->sample_aspect_ratio = filtered_frame->sample_aspect_ratio;

        do_video_out(of->ctx, ost, filtered_frame, &frame_size);
        if (vstats_filename && frame_size)
            do_video_stats(ost, frame_size);
        break;
    case AVMEDIA_TYPE_AUDIO:
        do_audio_out(of->ctx, ost, filtered_frame);
        break;
    default:
        // TODO support subtitle filters
        av_assert0(0);
    }

    av_frame_unref(filtered_frame);

    return 0;
}

static void finish_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    int i;

    ost->finished = 1;

    if (of->shortest) {
        for (i = 0; i < of->ctx->nb_streams; i++)
            output_streams[of->ost_index + i]->finished = 1;
    }
}

/*
 * Read as many frames from possible from lavfi and encode them.
 *
 * Always read from the active stream with the lowest timestamp. If no frames
 * are available for it then return EAGAIN and wait for more input. This way we
 * can use lavfi sources that generate unlimited amount of frames without memory
 * usage exploding.
 */
static int poll_filters(void)
{
    int i, ret = 0;

    while (ret >= 0 && !received_sigterm) {
        OutputStream *ost = NULL;
        int64_t min_pts = INT64_MAX;

        /* choose output stream with the lowest timestamp */
        for (i = 0; i < nb_output_streams; i++) {
            int64_t pts = output_streams[i]->sync_opts;

            if (!output_streams[i]->filter || output_streams[i]->finished)
                continue;

            pts = av_rescale_q(pts, output_streams[i]->st->codec->time_base,
                               AV_TIME_BASE_Q);
            if (pts < min_pts) {
                min_pts = pts;
                ost = output_streams[i];
            }
        }

        if (!ost)
            break;

        ret = poll_filter(ost);

        if (ret == AVERROR_EOF) {
            finish_output_stream(ost);
            ret = 0;
        } else if (ret == AVERROR(EAGAIN))
            return 0;
    }

    return ret;
}

static void print_report(int is_last_report, int64_t timer_start)
{
    char buf[1024];
    OutputStream *ost;
    AVFormatContext *oc;
    int64_t total_size;
    AVCodecContext *enc;
    int frame_number, vid, i;
    double bitrate, ti1, pts;
    static int64_t last_time = -1;
    static int qp_histogram[52];

    if (!print_stats && !is_last_report)
        return;

    if (!is_last_report) {
        int64_t cur_time;
        /* display the report every 0.5 seconds */
        cur_time = av_gettime();
        if (last_time == -1) {
            last_time = cur_time;
            return;
        }
        if ((cur_time - last_time) < 500000)
            return;
        last_time = cur_time;
    }


    oc = output_files[0]->ctx;

    total_size = avio_size(oc->pb);
    if (total_size <= 0) // FIXME improve avio_size() so it works with non seekable output too
        total_size = avio_tell(oc->pb);
    if (total_size < 0) {
        char errbuf[128];
        av_strerror(total_size, errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_VERBOSE, "Bitrate not available, "
               "avio_tell() failed: %s\n", errbuf);
        total_size = 0;
    }

    buf[0] = '\0';
    ti1 = 1e10;
    vid = 0;
    for (i = 0; i < nb_output_streams; i++) {
        float q = -1;
        ost = output_streams[i];
        enc = ost->st->codec;
        if (!ost->stream_copy && enc->coded_frame)
            q = enc->coded_frame->quality / (float)FF_QP2LAMBDA;
        if (vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "q=%2.1f ", q);
        }
        if (!vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            float t = (av_gettime() - timer_start) / 1000000.0;

            frame_number = ost->frame_number;
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "frame=%5d fps=%3d q=%3.1f ",
                     frame_number, (t > 1) ? (int)(frame_number / t + 0.5) : 0, q);
            if (is_last_report)
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "L");
            if (qp_hist) {
                int j;
                int qp = lrintf(q);
                if (qp >= 0 && qp < FF_ARRAY_ELEMS(qp_histogram))
                    qp_histogram[qp]++;
                for (j = 0; j < 32; j++)
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%X", (int)lrintf(log2(qp_histogram[j] + 1)));
            }
            if (enc->flags&CODEC_FLAG_PSNR) {
                int j;
                double error, error_sum = 0;
                double scale, scale_sum = 0;
                char type[3] = { 'Y','U','V' };
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "PSNR=");
                for (j = 0; j < 3; j++) {
                    if (is_last_report) {
                        error = enc->error[j];
                        scale = enc->width * enc->height * 255.0 * 255.0 * frame_number;
                    } else {
                        error = enc->coded_frame->error[j];
                        scale = enc->width * enc->height * 255.0 * 255.0;
                    }
                    if (j)
                        scale /= 4;
                    error_sum += error;
                    scale_sum += scale;
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%c:%2.2f ", type[j], psnr(error / scale));
                }
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "*:%2.2f ", psnr(error_sum / scale_sum));
            }
            vid = 1;
        }
        /* compute min output value */
        pts = (double)ost->st->pts.val * av_q2d(ost->st->time_base);
        if ((pts < ti1) && (pts > 0))
            ti1 = pts;
    }
    if (ti1 < 0.01)
        ti1 = 0.01;

    bitrate = (double)(total_size * 8) / ti1 / 1000.0;

    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
            "size=%8.0fkB time=%0.2f bitrate=%6.1fkbits/s",
            (double)total_size / 1024, ti1, bitrate);

    if (nb_frames_dup || nb_frames_drop)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " dup=%d drop=%d",
                nb_frames_dup, nb_frames_drop);

    av_log(NULL, AV_LOG_INFO, "%s    \r", buf);

    fflush(stderr);

    if (is_last_report) {
        int64_t raw   = audio_size + video_size + extra_size;
        float percent = 0.0;

        if (raw)
            percent = 100.0 * (total_size - raw) / raw;

        av_log(NULL, AV_LOG_INFO, "\n");
        av_log(NULL, AV_LOG_INFO, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
               video_size / 1024.0,
               audio_size / 1024.0,
               extra_size / 1024.0,
               percent);
    }
}

static void flush_encoders(void)
{
    int i, ret;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream   *ost = output_streams[i];
        AVCodecContext *enc = ost->st->codec;
        AVFormatContext *os = output_files[ost->file_index]->ctx;
        int stop_encoding = 0;

        if (!ost->encoding_needed)
            continue;

        if (ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO && enc->frame_size <= 1)
            continue;
        if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && (os->oformat->flags & AVFMT_RAWPICTURE) && enc->codec->id == AV_CODEC_ID_RAWVIDEO)
            continue;

        for (;;) {
            int (*encode)(AVCodecContext*, AVPacket*, const AVFrame*, int*) = NULL;
            const char *desc;
            int64_t *size;

            switch (ost->st->codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                encode = avcodec_encode_audio2;
                desc   = "Audio";
                size   = &audio_size;
                break;
            case AVMEDIA_TYPE_VIDEO:
                encode = avcodec_encode_video2;
                desc   = "Video";
                size   = &video_size;
                break;
            default:
                stop_encoding = 1;
            }

            if (encode) {
                AVPacket pkt;
                int got_packet;
                av_init_packet(&pkt);
                pkt.data = NULL;
                pkt.size = 0;

                ret = encode(enc, &pkt, NULL, &got_packet);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_FATAL, "%s encoding failed\n", desc);
                    exit_program(1);
                }
                *size += ret;
                if (ost->logfile && enc->stats_out) {
                    fprintf(ost->logfile, "%s", enc->stats_out);
                }
                if (!got_packet) {
                    stop_encoding = 1;
                    break;
                }
                if (pkt.pts != AV_NOPTS_VALUE)
                    pkt.pts = av_rescale_q(pkt.pts, enc->time_base, ost->st->time_base);
                if (pkt.dts != AV_NOPTS_VALUE)
                    pkt.dts = av_rescale_q(pkt.dts, enc->time_base, ost->st->time_base);
                if (pkt.duration > 0)
                    pkt.duration = av_rescale_q(pkt.duration, enc->time_base, ost->st->time_base);
                write_frame(os, &pkt, ost);
            }

            if (stop_encoding)
                break;
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

    if (of->start_time != AV_NOPTS_VALUE && ist->last_dts < of->start_time)
        return 0;

    return 1;
}

static void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
{
    OutputFile *of = output_files[ost->file_index];
    InputFile   *f = input_files [ist->file_index];
    int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
    int64_t ost_tb_start_time = av_rescale_q(start_time, AV_TIME_BASE_Q, ost->st->time_base);
    AVPacket opkt;

    av_init_packet(&opkt);

    if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) &&
        !ost->copy_initial_nonkeyframes)
        return;

    if (of->recording_time != INT64_MAX &&
        ist->last_dts >= of->recording_time + start_time) {
        ost->finished = 1;
        return;
    }

    if (f->recording_time != INT64_MAX) {
        start_time = f->ctx->start_time;
        if (f->start_time != AV_NOPTS_VALUE)
            start_time += f->start_time;
        if (ist->last_dts >= f->recording_time + start_time) {
            ost->finished = 1;
            return;
        }
    }

    /* force the input stream PTS */
    if (ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        audio_size += pkt->size;
    else if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_size += pkt->size;
        ost->sync_opts++;
    }

    if (pkt->pts != AV_NOPTS_VALUE)
        opkt.pts = av_rescale_q(pkt->pts, ist->st->time_base, ost->st->time_base) - ost_tb_start_time;
    else
        opkt.pts = AV_NOPTS_VALUE;

    if (pkt->dts == AV_NOPTS_VALUE)
        opkt.dts = av_rescale_q(ist->last_dts, AV_TIME_BASE_Q, ost->st->time_base);
    else
        opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->st->time_base);
    opkt.dts -= ost_tb_start_time;

    opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->st->time_base);
    opkt.flags    = pkt->flags;

    // FIXME remove the following 2 lines they shall be replaced by the bitstream filters
    if (  ost->st->codec->codec_id != AV_CODEC_ID_H264
       && ost->st->codec->codec_id != AV_CODEC_ID_MPEG1VIDEO
       && ost->st->codec->codec_id != AV_CODEC_ID_MPEG2VIDEO
       && ost->st->codec->codec_id != AV_CODEC_ID_VC1
       ) {
        if (av_parser_change(ost->parser, ost->st->codec,
                             &opkt.data, &opkt.size,
                             pkt->data, pkt->size,
                             pkt->flags & AV_PKT_FLAG_KEY)) {
            opkt.buf = av_buffer_create(opkt.data, opkt.size, av_buffer_default_free, NULL, 0);
            if (!opkt.buf)
                exit_program(1);
        }
    } else {
        opkt.data = pkt->data;
        opkt.size = pkt->size;
    }

    write_frame(of->ctx, &opkt, ost);
    ost->st->codec->frame_number++;
}

int guess_input_channel_layout(InputStream *ist)
{
    AVCodecContext *dec = ist->st->codec;

    if (!dec->channel_layout) {
        char layout_name[256];

        dec->channel_layout = av_get_default_channel_layout(dec->channels);
        if (!dec->channel_layout)
            return 0;
        av_get_channel_layout_string(layout_name, sizeof(layout_name),
                                     dec->channels, dec->channel_layout);
        av_log(NULL, AV_LOG_WARNING, "Guessed Channel Layout for  Input Stream "
               "#%d.%d : %s\n", ist->file_index, ist->st->index, layout_name);
    }
    return 1;
}

static int decode_audio(InputStream *ist, AVPacket *pkt, int *got_output)
{
    AVFrame *decoded_frame, *f;
    AVCodecContext *avctx = ist->st->codec;
    int i, ret, err = 0, resample_changed;

    if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = ist->decoded_frame;

    ret = avcodec_decode_audio4(avctx, decoded_frame, got_output, pkt);
    if (!*got_output || ret < 0) {
        if (!pkt->size) {
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_add_frame(ist->filters[i]->filter, NULL);
        }
        return ret;
    }

    /* if the decoder provides a pts, use it instead of the last packet pts.
       the decoder could be delaying output by a packet or more. */
    if (decoded_frame->pts != AV_NOPTS_VALUE)
        ist->next_dts = decoded_frame->pts;
    else if (pkt->pts != AV_NOPTS_VALUE) {
        decoded_frame->pts = pkt->pts;
        pkt->pts           = AV_NOPTS_VALUE;
    }

    resample_changed = ist->resample_sample_fmt     != decoded_frame->format         ||
                       ist->resample_channels       != avctx->channels               ||
                       ist->resample_channel_layout != decoded_frame->channel_layout ||
                       ist->resample_sample_rate    != decoded_frame->sample_rate;
    if (resample_changed) {
        char layout1[64], layout2[64];

        if (!guess_input_channel_layout(ist)) {
            av_log(NULL, AV_LOG_FATAL, "Unable to find default channel "
                   "layout for Input Stream #%d.%d\n", ist->file_index,
                   ist->st->index);
            exit_program(1);
        }
        decoded_frame->channel_layout = avctx->channel_layout;

        av_get_channel_layout_string(layout1, sizeof(layout1), ist->resample_channels,
                                     ist->resample_channel_layout);
        av_get_channel_layout_string(layout2, sizeof(layout2), avctx->channels,
                                     decoded_frame->channel_layout);

        av_log(NULL, AV_LOG_INFO,
               "Input stream #%d:%d frame changed from rate:%d fmt:%s ch:%d chl:%s to rate:%d fmt:%s ch:%d chl:%s\n",
               ist->file_index, ist->st->index,
               ist->resample_sample_rate,  av_get_sample_fmt_name(ist->resample_sample_fmt),
               ist->resample_channels, layout1,
               decoded_frame->sample_rate, av_get_sample_fmt_name(decoded_frame->format),
               avctx->channels, layout2);

        ist->resample_sample_fmt     = decoded_frame->format;
        ist->resample_sample_rate    = decoded_frame->sample_rate;
        ist->resample_channel_layout = decoded_frame->channel_layout;
        ist->resample_channels       = avctx->channels;

        for (i = 0; i < nb_filtergraphs; i++)
            if (ist_in_filtergraph(filtergraphs[i], ist) &&
                configure_filtergraph(filtergraphs[i]) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error reinitializing filters!\n");
                exit_program(1);
            }
    }

    if (decoded_frame->pts != AV_NOPTS_VALUE)
        decoded_frame->pts = av_rescale_q(decoded_frame->pts,
                                          ist->st->time_base,
                                          (AVRational){1, ist->st->codec->sample_rate});
    for (i = 0; i < ist->nb_filters; i++) {
        if (i < ist->nb_filters - 1) {
            f = ist->filter_frame;
            err = av_frame_ref(f, decoded_frame);
            if (err < 0)
                break;
        } else
            f = decoded_frame;

        err = av_buffersrc_add_frame(ist->filters[i]->filter, f);
        if (err < 0)
            break;
    }

    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int decode_video(InputStream *ist, AVPacket *pkt, int *got_output)
{
    AVFrame *decoded_frame, *f;
    int i, ret = 0, err = 0, resample_changed;

    if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = ist->decoded_frame;

    ret = avcodec_decode_video2(ist->st->codec,
                                decoded_frame, got_output, pkt);
    if (!*got_output || ret < 0) {
        if (!pkt->size) {
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_add_frame(ist->filters[i]->filter, NULL);
        }
        return ret;
    }

    if (ist->hwaccel_retrieve_data && decoded_frame->format == ist->hwaccel_pix_fmt) {
        err = ist->hwaccel_retrieve_data(ist->st->codec, decoded_frame);
        if (err < 0)
            goto fail;
    }
    ist->hwaccel_retrieved_pix_fmt = decoded_frame->format;

    decoded_frame->pts = guess_correct_pts(&ist->pts_ctx, decoded_frame->pkt_pts,
                                           decoded_frame->pkt_dts);
    pkt->size = 0;

    if (ist->st->sample_aspect_ratio.num)
        decoded_frame->sample_aspect_ratio = ist->st->sample_aspect_ratio;

    resample_changed = ist->resample_width   != decoded_frame->width  ||
                       ist->resample_height  != decoded_frame->height ||
                       ist->resample_pix_fmt != decoded_frame->format;
    if (resample_changed) {
        av_log(NULL, AV_LOG_INFO,
               "Input stream #%d:%d frame changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
               ist->file_index, ist->st->index,
               ist->resample_width,  ist->resample_height,  av_get_pix_fmt_name(ist->resample_pix_fmt),
               decoded_frame->width, decoded_frame->height, av_get_pix_fmt_name(decoded_frame->format));

        ret = poll_filters();
        if (ret < 0 && (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)))
            av_log(NULL, AV_LOG_ERROR, "Error while filtering.\n");

        ist->resample_width   = decoded_frame->width;
        ist->resample_height  = decoded_frame->height;
        ist->resample_pix_fmt = decoded_frame->format;

        for (i = 0; i < nb_filtergraphs; i++)
            if (ist_in_filtergraph(filtergraphs[i], ist) &&
                configure_filtergraph(filtergraphs[i]) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error reinitializing filters!\n");
                exit_program(1);
            }
    }

    for (i = 0; i < ist->nb_filters; i++) {
        if (i < ist->nb_filters - 1) {
            f = ist->filter_frame;
            err = av_frame_ref(f, decoded_frame);
            if (err < 0)
                break;
        } else
            f = decoded_frame;

        err = av_buffersrc_add_frame(ist->filters[i]->filter, f);
        if (err < 0)
            break;
    }

fail:
    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

static int transcode_subtitles(InputStream *ist, AVPacket *pkt, int *got_output)
{
    AVSubtitle subtitle;
    int i, ret = avcodec_decode_subtitle2(ist->st->codec,
                                          &subtitle, got_output, pkt);
    if (ret < 0)
        return ret;
    if (!*got_output)
        return ret;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (!check_output_constraints(ist, ost) || !ost->encoding_needed)
            continue;

        do_subtitle_out(output_files[ost->file_index]->ctx, ost, ist, &subtitle, pkt->pts);
    }

    avsubtitle_free(&subtitle);
    return ret;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int output_packet(InputStream *ist, const AVPacket *pkt)
{
    int i;
    int got_output;
    AVPacket avpkt;

    if (ist->next_dts == AV_NOPTS_VALUE)
        ist->next_dts = ist->last_dts;

    if (pkt == NULL) {
        /* EOF handling */
        av_init_packet(&avpkt);
        avpkt.data = NULL;
        avpkt.size = 0;
        goto handle_eof;
    } else {
        avpkt = *pkt;
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        ist->next_dts = ist->last_dts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);

    // while we have more to decode or while the decoder did output something on EOF
    while (ist->decoding_needed && (avpkt.size > 0 || (!pkt && got_output))) {
        int ret = 0;
    handle_eof:

        ist->last_dts = ist->next_dts;

        if (avpkt.size && avpkt.size != pkt->size) {
            av_log(NULL, ist->showed_multi_packet_warning ? AV_LOG_VERBOSE : AV_LOG_WARNING,
                   "Multiple frames in a packet from stream %d\n", pkt->stream_index);
            ist->showed_multi_packet_warning = 1;
        }

        switch (ist->st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ret = decode_audio    (ist, &avpkt, &got_output);
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = decode_video    (ist, &avpkt, &got_output);
            if (avpkt.duration)
                ist->next_dts += av_rescale_q(avpkt.duration, ist->st->time_base, AV_TIME_BASE_Q);
            else if (ist->st->avg_frame_rate.num)
                ist->next_dts += av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate),
                                              AV_TIME_BASE_Q);
            else if (ist->st->codec->time_base.num != 0) {
                int ticks      = ist->st->parser ? ist->st->parser->repeat_pict + 1 :
                                                   ist->st->codec->ticks_per_frame;
                ist->next_dts += av_rescale_q(ticks, ist->st->codec->time_base, AV_TIME_BASE_Q);
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            ret = transcode_subtitles(ist, &avpkt, &got_output);
            break;
        default:
            return -1;
        }

        if (ret < 0)
            return ret;
        // touch data and size only if not EOF
        if (pkt) {
            avpkt.data += ret;
            avpkt.size -= ret;
        }
        if (!got_output) {
            continue;
        }
    }

    /* handle stream copy */
    if (!ist->decoding_needed) {
        ist->last_dts = ist->next_dts;
        switch (ist->st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ist->next_dts += ((int64_t)AV_TIME_BASE * ist->st->codec->frame_size) /
                             ist->st->codec->sample_rate;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (ist->st->codec->time_base.num != 0) {
                int ticks = ist->st->parser ? ist->st->parser->repeat_pict + 1 : ist->st->codec->ticks_per_frame;
                ist->next_dts += ((int64_t)AV_TIME_BASE *
                                  ist->st->codec->time_base.num * ticks) /
                                  ist->st->codec->time_base.den;
            }
            break;
        }
    }
    for (i = 0; pkt && i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (!check_output_constraints(ist, ost) || ost->encoding_needed)
            continue;

        do_streamcopy(ist, ost, pkt);
    }

    return 0;
}

static void print_sdp(void)
{
    char sdp[16384];
    int i;
    AVFormatContext **avc = av_malloc(sizeof(*avc) * nb_output_files);

    if (!avc)
        exit_program(1);
    for (i = 0; i < nb_output_files; i++)
        avc[i] = output_files[i]->ctx;

    av_sdp_create(avc, nb_output_files, sdp, sizeof(sdp));
    printf("SDP:\n%s\n", sdp);
    fflush(stdout);
    av_freep(&avc);
}

static const HWAccel *get_hwaccel(enum AVPixelFormat pix_fmt)
{
    int i;
    for (i = 0; hwaccels[i].name; i++)
        if (hwaccels[i].pix_fmt == pix_fmt)
            return &hwaccels[i];
    return NULL;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != -1; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const HWAccel *hwaccel;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        hwaccel = get_hwaccel(*p);
        if (!hwaccel ||
            (ist->active_hwaccel_id && ist->active_hwaccel_id != hwaccel->id) ||
            (ist->hwaccel_id != HWACCEL_AUTO && ist->hwaccel_id != hwaccel->id))
            continue;

        ret = hwaccel->init(s);
        if (ret < 0) {
            if (ist->hwaccel_id == hwaccel->id) {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream #%d:%d, "
                       "but cannot be initialized.\n", hwaccel->name,
                       ist->file_index, ist->st->index);
                exit_program(1);
            }
            continue;
        }
        ist->active_hwaccel_id = hwaccel->id;
        ist->hwaccel_pix_fmt   = *p;
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
    int i, ret;
    InputStream *ist = input_streams[ist_index];
    if (ist->decoding_needed) {
        AVCodec *codec = ist->dec;
        if (!codec) {
            snprintf(error, error_len, "Decoder (codec id %d) not found for input stream #%d:%d",
                    ist->st->codec->codec_id, ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        /* update requested sample format for the decoder based on the
           corresponding encoder sample format */
        for (i = 0; i < nb_output_streams; i++) {
            OutputStream *ost = output_streams[i];
            if (ost->source_index == ist_index) {
                update_sample_fmt(ist->st->codec, codec, ost->st->codec);
                break;
            }
        }

        ist->st->codec->opaque      = ist;
        ist->st->codec->get_format  = get_format;
        ist->st->codec->get_buffer2 = get_buffer;
        ist->st->codec->thread_safe_callbacks = 1;

        av_opt_set_int(ist->st->codec, "refcounted_frames", 1, 0);

        if (!av_dict_get(ist->opts, "threads", NULL, 0))
            av_dict_set(&ist->opts, "threads", "auto", 0);
        if ((ret = avcodec_open2(ist->st->codec, codec, &ist->opts)) < 0) {
            char errbuf[128];
            if (ret == AVERROR_EXPERIMENTAL)
                abort_codec_experimental(codec, 0);

            av_strerror(ret, errbuf, sizeof(errbuf));

            snprintf(error, error_len,
                     "Error while opening decoder for input stream "
                     "#%d:%d : %s",
                     ist->file_index, ist->st->index, errbuf);
            return ret;
        }
        assert_avoptions(ist->opts);
    }

    ist->last_dts = ist->st->avg_frame_rate.num ? - ist->st->codec->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
    ist->next_dts = AV_NOPTS_VALUE;
    init_pts_correction(&ist->pts_ctx);
    ist->is_start = 1;

    return 0;
}

static InputStream *get_input_stream(OutputStream *ost)
{
    if (ost->source_index >= 0)
        return input_streams[ost->source_index];

    if (ost->filter) {
        FilterGraph *fg = ost->filter->graph;
        int i;

        for (i = 0; i < fg->nb_inputs; i++)
            if (fg->inputs[i]->ist->st->codec->codec_type == ost->st->codec->codec_type)
                return fg->inputs[i]->ist;
    }

    return NULL;
}

static void parse_forced_key_frames(char *kf, OutputStream *ost,
                                    AVCodecContext *avctx)
{
    char *p;
    int n = 1, i;
    int64_t t;

    for (p = kf; *p; p++)
        if (*p == ',')
            n++;
    ost->forced_kf_count = n;
    ost->forced_kf_pts   = av_malloc(sizeof(*ost->forced_kf_pts) * n);
    if (!ost->forced_kf_pts) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.\n");
        exit_program(1);
    }

    p = kf;
    for (i = 0; i < n; i++) {
        char *next = strchr(p, ',');

        if (next)
            *next++ = 0;

        t = parse_time_or_die("force_key_frames", p, 1);
        ost->forced_kf_pts[i] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);

        p = next;
    }
}

static int transcode_init(void)
{
    int ret = 0, i, j, k;
    AVFormatContext *oc;
    AVCodecContext *codec;
    OutputStream *ost;
    InputStream *ist;
    char error[1024];
    int want_sdp = 1;

    /* init framerate emulation */
    for (i = 0; i < nb_input_files; i++) {
        InputFile *ifile = input_files[i];
        if (ifile->rate_emu)
            for (j = 0; j < ifile->nb_streams; j++)
                input_streams[j + ifile->ist_index]->start = av_gettime();
    }

    /* output stream init */
    for (i = 0; i < nb_output_files; i++) {
        oc = output_files[i]->ctx;
        if (!oc->nb_streams && !(oc->oformat->flags & AVFMT_NOSTREAMS)) {
            av_dump_format(oc, i, oc->filename, 1);
            av_log(NULL, AV_LOG_ERROR, "Output file #%d does not contain any stream\n", i);
            return AVERROR(EINVAL);
        }
    }

    /* init complex filtergraphs */
    for (i = 0; i < nb_filtergraphs; i++)
        if ((ret = avfilter_graph_config(filtergraphs[i]->graph, NULL)) < 0)
            return ret;

    /* for each output stream, we compute the right encoding parameters */
    for (i = 0; i < nb_output_streams; i++) {
        AVCodecContext *icodec = NULL;
        ost = output_streams[i];
        oc  = output_files[ost->file_index]->ctx;
        ist = get_input_stream(ost);

        if (ost->attachment_filename)
            continue;

        codec  = ost->st->codec;

        if (ist) {
            icodec = ist->st->codec;

            ost->st->disposition          = ist->st->disposition;
            codec->bits_per_raw_sample    = icodec->bits_per_raw_sample;
            codec->chroma_sample_location = icodec->chroma_sample_location;
        }

        if (ost->stream_copy) {
            AVRational sar;
            uint64_t extra_size;

            av_assert0(ist && !ost->filter);

            extra_size = (uint64_t)icodec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;

            if (extra_size > INT_MAX) {
                return AVERROR(EINVAL);
            }

            /* if stream_copy is selected, no need to decode or encode */
            codec->codec_id   = icodec->codec_id;
            codec->codec_type = icodec->codec_type;

            if (!codec->codec_tag) {
                if (!oc->oformat->codec_tag ||
                     av_codec_get_id (oc->oformat->codec_tag, icodec->codec_tag) == codec->codec_id ||
                     av_codec_get_tag(oc->oformat->codec_tag, icodec->codec_id) <= 0)
                    codec->codec_tag = icodec->codec_tag;
            }

            codec->bit_rate       = icodec->bit_rate;
            codec->rc_max_rate    = icodec->rc_max_rate;
            codec->rc_buffer_size = icodec->rc_buffer_size;
            codec->field_order    = icodec->field_order;
            codec->extradata      = av_mallocz(extra_size);
            if (!codec->extradata) {
                return AVERROR(ENOMEM);
            }
            memcpy(codec->extradata, icodec->extradata, icodec->extradata_size);
            codec->extradata_size = icodec->extradata_size;
            if (!copy_tb) {
                codec->time_base      = icodec->time_base;
                codec->time_base.num *= icodec->ticks_per_frame;
                av_reduce(&codec->time_base.num, &codec->time_base.den,
                          codec->time_base.num, codec->time_base.den, INT_MAX);
            } else
                codec->time_base = ist->st->time_base;

            ost->parser = av_parser_init(codec->codec_id);

            switch (codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (audio_volume != 256) {
                    av_log(NULL, AV_LOG_FATAL, "-acodec copy and -vol are incompatible (frames are not decoded)\n");
                    exit_program(1);
                }
                codec->channel_layout     = icodec->channel_layout;
                codec->sample_rate        = icodec->sample_rate;
                codec->channels           = icodec->channels;
                codec->frame_size         = icodec->frame_size;
                codec->audio_service_type = icodec->audio_service_type;
                codec->block_align        = icodec->block_align;
                break;
            case AVMEDIA_TYPE_VIDEO:
                codec->pix_fmt            = icodec->pix_fmt;
                codec->width              = icodec->width;
                codec->height             = icodec->height;
                codec->has_b_frames       = icodec->has_b_frames;
                if (ost->frame_aspect_ratio)
                    sar = av_d2q(ost->frame_aspect_ratio * codec->height / codec->width, 255);
                else if (ist->st->sample_aspect_ratio.num)
                    sar = ist->st->sample_aspect_ratio;
                else
                    sar = icodec->sample_aspect_ratio;
                ost->st->sample_aspect_ratio = codec->sample_aspect_ratio = sar;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                codec->width  = icodec->width;
                codec->height = icodec->height;
                break;
            case AVMEDIA_TYPE_DATA:
            case AVMEDIA_TYPE_ATTACHMENT:
                break;
            default:
                abort();
            }
        } else {
            if (!ost->enc) {
                /* should only happen when a default codec is not present. */
                snprintf(error, sizeof(error), "Automatic encoder selection "
                         "failed for output stream #%d:%d. Default encoder for "
                         "format %s is probably disabled. Please choose an "
                         "encoder manually.\n", ost->file_index, ost->index,
                         oc->oformat->name);
                ret = AVERROR(EINVAL);
                goto dump_format;
            }

            if (ist)
                ist->decoding_needed = 1;
            ost->encoding_needed = 1;

            /*
             * We want CFR output if and only if one of those is true:
             * 1) user specified output framerate with -r
             * 2) user specified -vsync cfr
             * 3) output format is CFR and the user didn't force vsync to
             *    something else than CFR
             *
             * in such a case, set ost->frame_rate
             */
            if (codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                !ost->frame_rate.num && ist &&
                (video_sync_method ==  VSYNC_CFR ||
                 (video_sync_method ==  VSYNC_AUTO &&
                  !(oc->oformat->flags & (AVFMT_NOTIMESTAMPS | AVFMT_VARIABLE_FPS))))) {
                if (ist->framerate.num)
                    ost->frame_rate = ist->framerate;
                else if (ist->st->avg_frame_rate.num)
                    ost->frame_rate = ist->st->avg_frame_rate;
                else {
                    av_log(NULL, AV_LOG_WARNING, "Constant framerate requested "
                           "for the output stream #%d:%d, but no information "
                           "about the input framerate is available. Falling "
                           "back to a default value of 25fps. Use the -r option "
                           "if you want a different framerate.\n",
                           ost->file_index, ost->index);
                    ost->frame_rate = (AVRational){ 25, 1 };
                }

                if (ost->enc && ost->enc->supported_framerates && !ost->force_fps) {
                    int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
                    ost->frame_rate = ost->enc->supported_framerates[idx];
                }
            }

            if (!ost->filter &&
                (codec->codec_type == AVMEDIA_TYPE_VIDEO ||
                 codec->codec_type == AVMEDIA_TYPE_AUDIO)) {
                    FilterGraph *fg;
                    fg = init_simple_filtergraph(ist, ost);
                    if (configure_filtergraph(fg)) {
                        av_log(NULL, AV_LOG_FATAL, "Error opening filters!\n");
                        exit_program(1);
                    }
            }

            switch (codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                codec->sample_fmt     = ost->filter->filter->inputs[0]->format;
                codec->sample_rate    = ost->filter->filter->inputs[0]->sample_rate;
                codec->channel_layout = ost->filter->filter->inputs[0]->channel_layout;
                codec->channels       = av_get_channel_layout_nb_channels(codec->channel_layout);
                codec->time_base      = (AVRational){ 1, codec->sample_rate };
                break;
            case AVMEDIA_TYPE_VIDEO:
                codec->time_base = ost->filter->filter->inputs[0]->time_base;

                codec->width  = ost->filter->filter->inputs[0]->w;
                codec->height = ost->filter->filter->inputs[0]->h;
                codec->sample_aspect_ratio = ost->st->sample_aspect_ratio =
                    ost->frame_aspect_ratio ? // overridden by the -aspect cli option
                    av_d2q(ost->frame_aspect_ratio * codec->height/codec->width, 255) :
                    ost->filter->filter->inputs[0]->sample_aspect_ratio;
                codec->pix_fmt = ost->filter->filter->inputs[0]->format;

                if (icodec &&
                    (codec->width   != icodec->width  ||
                     codec->height  != icodec->height ||
                     codec->pix_fmt != icodec->pix_fmt)) {
                    codec->bits_per_raw_sample = 0;
                }

                if (ost->forced_keyframes)
                    parse_forced_key_frames(ost->forced_keyframes, ost,
                                            ost->st->codec);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                codec->time_base = (AVRational){1, 1000};
                break;
            default:
                abort();
                break;
            }
            /* two pass mode */
            if ((codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2))) {
                char logfilename[1024];
                FILE *f;

                snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                         ost->logfile_prefix ? ost->logfile_prefix :
                                               DEFAULT_PASS_LOGFILENAME_PREFIX,
                         i);
                if (!strcmp(ost->enc->name, "libx264")) {
                    av_dict_set(&ost->opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
                } else {
                    if (codec->flags & CODEC_FLAG_PASS1) {
                        f = fopen(logfilename, "wb");
                        if (!f) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot write log file '%s' for pass-1 encoding: %s\n",
                                   logfilename, strerror(errno));
                            exit_program(1);
                        }
                        ost->logfile = f;
                    } else {
                        char  *logbuffer;
                        size_t logbuffer_size;
                        if (cmdutils_read_file(logfilename, &logbuffer, &logbuffer_size) < 0) {
                            av_log(NULL, AV_LOG_FATAL, "Error reading log file '%s' for pass-2 encoding\n",
                                   logfilename);
                            exit_program(1);
                        }
                        codec->stats_in = logbuffer;
                    }
                }
            }
        }
    }

    /* open each encoder */
    for (i = 0; i < nb_output_streams; i++) {
        ost = output_streams[i];
        if (ost->encoding_needed) {
            AVCodec      *codec = ost->enc;
            AVCodecContext *dec = NULL;

            if ((ist = get_input_stream(ost)))
                dec = ist->st->codec;
            if (dec && dec->subtitle_header) {
                ost->st->codec->subtitle_header = av_malloc(dec->subtitle_header_size);
                if (!ost->st->codec->subtitle_header) {
                    ret = AVERROR(ENOMEM);
                    goto dump_format;
                }
                memcpy(ost->st->codec->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
                ost->st->codec->subtitle_header_size = dec->subtitle_header_size;
            }
            if (!av_dict_get(ost->opts, "threads", NULL, 0))
                av_dict_set(&ost->opts, "threads", "auto", 0);
            if ((ret = avcodec_open2(ost->st->codec, codec, &ost->opts)) < 0) {
                if (ret == AVERROR_EXPERIMENTAL)
                    abort_codec_experimental(codec, 1);
                snprintf(error, sizeof(error), "Error while opening encoder for output stream #%d:%d - maybe incorrect parameters such as bit_rate, rate, width or height",
                        ost->file_index, ost->index);
                goto dump_format;
            }
            assert_avoptions(ost->opts);
            if (ost->st->codec->bit_rate && ost->st->codec->bit_rate < 1000)
                av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                             "It takes bits/s as argument, not kbits/s\n");
            extra_size += ost->st->codec->extradata_size;
        } else {
            av_opt_set_dict(ost->st->codec, &ost->opts);
        }
    }

    /* init input streams */
    for (i = 0; i < nb_input_streams; i++)
        if ((ret = init_input_stream(i, error, sizeof(error))) < 0)
            goto dump_format;

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

    /* open files and write file headers */
    for (i = 0; i < nb_output_files; i++) {
        oc = output_files[i]->ctx;
        oc->interrupt_callback = int_cb;
        if ((ret = avformat_write_header(oc, &output_files[i]->opts)) < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            snprintf(error, sizeof(error),
                     "Could not write header for output file #%d "
                     "(incorrect codec parameters ?): %s",
                     i, errbuf);
            ret = AVERROR(EINVAL);
            goto dump_format;
        }
        assert_avoptions(output_files[i]->opts);
        if (strcmp(oc->oformat->name, "rtp")) {
            want_sdp = 0;
        }
    }

 dump_format:
    /* dump the file output parameters - cannot be done before in case
       of stream copy */
    for (i = 0; i < nb_output_files; i++) {
        av_dump_format(output_files[i]->ctx, i, output_files[i]->ctx->filename, 1);
    }

    /* dump the stream mapping */
    av_log(NULL, AV_LOG_INFO, "Stream mapping:\n");
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];

        for (j = 0; j < ist->nb_filters; j++) {
            if (ist->filters[j]->graph->graph_desc) {
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

        if (ost->filter && ost->filter->graph->graph_desc) {
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
        else
            av_log(NULL, AV_LOG_INFO, " (%s -> %s)", input_streams[ost->source_index]->dec ?
                   input_streams[ost->source_index]->dec->name : "?",
                   ost->enc ? ost->enc->name : "?");
        av_log(NULL, AV_LOG_INFO, "\n");
    }

    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", error);
        return ret;
    }

    if (want_sdp) {
        print_sdp();
    }

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
                output_streams[of->ost_index + j]->finished = 1;
            continue;
        }

        return 1;
    }

    return 0;
}

static InputFile *select_input_file(void)
{
    InputFile *ifile = NULL;
    int64_t ipts_min = INT64_MAX;
    int i;

    for (i = 0; i < nb_input_streams; i++) {
        InputStream *ist = input_streams[i];
        int64_t ipts     = ist->last_dts;

        if (ist->discard || input_files[ist->file_index]->eagain)
            continue;
        if (!input_files[ist->file_index]->eof_reached) {
            if (ipts < ipts_min) {
                ipts_min = ipts;
                ifile    = input_files[ist->file_index];
            }
        }
    }

    return ifile;
}

#if HAVE_PTHREADS
static void *input_thread(void *arg)
{
    InputFile *f = arg;
    int ret = 0;

    while (!transcoding_finished && ret >= 0) {
        AVPacket pkt;
        ret = av_read_frame(f->ctx, &pkt);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            ret = 0;
            continue;
        } else if (ret < 0)
            break;

        pthread_mutex_lock(&f->fifo_lock);
        while (!av_fifo_space(f->fifo))
            pthread_cond_wait(&f->fifo_cond, &f->fifo_lock);

        av_dup_packet(&pkt);
        av_fifo_generic_write(f->fifo, &pkt, sizeof(pkt), NULL);

        pthread_mutex_unlock(&f->fifo_lock);
    }

    f->finished = 1;
    return NULL;
}

static void free_input_threads(void)
{
    int i;

    if (nb_input_files == 1)
        return;

    transcoding_finished = 1;

    for (i = 0; i < nb_input_files; i++) {
        InputFile *f = input_files[i];
        AVPacket pkt;

        if (!f->fifo || f->joined)
            continue;

        pthread_mutex_lock(&f->fifo_lock);
        while (av_fifo_size(f->fifo)) {
            av_fifo_generic_read(f->fifo, &pkt, sizeof(pkt), NULL);
            av_free_packet(&pkt);
        }
        pthread_cond_signal(&f->fifo_cond);
        pthread_mutex_unlock(&f->fifo_lock);

        pthread_join(f->thread, NULL);
        f->joined = 1;

        while (av_fifo_size(f->fifo)) {
            av_fifo_generic_read(f->fifo, &pkt, sizeof(pkt), NULL);
            av_free_packet(&pkt);
        }
        av_fifo_free(f->fifo);
    }
}

static int init_input_threads(void)
{
    int i, ret;

    if (nb_input_files == 1)
        return 0;

    for (i = 0; i < nb_input_files; i++) {
        InputFile *f = input_files[i];

        if (!(f->fifo = av_fifo_alloc(8*sizeof(AVPacket))))
            return AVERROR(ENOMEM);

        pthread_mutex_init(&f->fifo_lock, NULL);
        pthread_cond_init (&f->fifo_cond, NULL);

        if ((ret = pthread_create(&f->thread, NULL, input_thread, f)))
            return AVERROR(ret);
    }
    return 0;
}

static int get_input_packet_mt(InputFile *f, AVPacket *pkt)
{
    int ret = 0;

    pthread_mutex_lock(&f->fifo_lock);

    if (av_fifo_size(f->fifo)) {
        av_fifo_generic_read(f->fifo, pkt, sizeof(*pkt), NULL);
        pthread_cond_signal(&f->fifo_cond);
    } else {
        if (f->finished)
            ret = AVERROR_EOF;
        else
            ret = AVERROR(EAGAIN);
    }

    pthread_mutex_unlock(&f->fifo_lock);

    return ret;
}
#endif

static int get_input_packet(InputFile *f, AVPacket *pkt)
{
    if (f->rate_emu) {
        int i;
        for (i = 0; i < f->nb_streams; i++) {
            InputStream *ist = input_streams[f->ist_index + i];
            int64_t pts = av_rescale(ist->last_dts, 1000000, AV_TIME_BASE);
            int64_t now = av_gettime() - ist->start;
            if (pts > now)
                return AVERROR(EAGAIN);
        }
    }

#if HAVE_PTHREADS
    if (nb_input_files > 1)
        return get_input_packet_mt(f, pkt);
#endif
    return av_read_frame(f->ctx, pkt);
}

static int got_eagain(void)
{
    int i;
    for (i = 0; i < nb_input_files; i++)
        if (input_files[i]->eagain)
            return 1;
    return 0;
}

static void reset_eagain(void)
{
    int i;
    for (i = 0; i < nb_input_files; i++)
        input_files[i]->eagain = 0;
}

/*
 * Read one packet from an input file and send it for
 * - decoding -> lavfi (audio/video)
 * - decoding -> encoding -> muxing (subtitles)
 * - muxing (streamcopy)
 *
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
static int process_input(void)
{
    InputFile *ifile;
    AVFormatContext *is;
    InputStream *ist;
    AVPacket pkt;
    int ret, i, j;

    /* select the stream that we must read now */
    ifile = select_input_file();
    /* if none, if is finished */
    if (!ifile) {
        if (got_eagain()) {
            reset_eagain();
            av_usleep(10000);
            return AVERROR(EAGAIN);
        }
        av_log(NULL, AV_LOG_VERBOSE, "No more inputs to read from.\n");
        return AVERROR_EOF;
    }

    is  = ifile->ctx;
    ret = get_input_packet(ifile, &pkt);

    if (ret == AVERROR(EAGAIN)) {
        ifile->eagain = 1;
        return ret;
    }
    if (ret < 0) {
        if (ret != AVERROR_EOF) {
            print_error(is->filename, ret);
            if (exit_on_error)
                exit_program(1);
        }
        ifile->eof_reached = 1;

        for (i = 0; i < ifile->nb_streams; i++) {
            ist = input_streams[ifile->ist_index + i];
            if (ist->decoding_needed)
                output_packet(ist, NULL);

            /* mark all outputs that don't go through lavfi as finished */
            for (j = 0; j < nb_output_streams; j++) {
                OutputStream *ost = output_streams[j];

                if (ost->source_index == ifile->ist_index + i &&
                    (ost->stream_copy || ost->enc->type == AVMEDIA_TYPE_SUBTITLE))
                    finish_output_stream(ost);
            }
        }

        return AVERROR(EAGAIN);
    }

    reset_eagain();

    if (do_pkt_dump) {
        av_pkt_dump_log2(NULL, AV_LOG_DEBUG, &pkt, do_hex_dump,
                         is->streams[pkt.stream_index]);
    }
    /* the following test is needed in case new streams appear
       dynamically in stream : we ignore them */
    if (pkt.stream_index >= ifile->nb_streams)
        goto discard_packet;

    ist = input_streams[ifile->ist_index + pkt.stream_index];
    if (ist->discard)
        goto discard_packet;

    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);
    if (pkt.pts != AV_NOPTS_VALUE)
        pkt.pts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);

    if (pkt.pts != AV_NOPTS_VALUE)
        pkt.pts *= ist->ts_scale;
    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts *= ist->ts_scale;

    if (pkt.dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE &&
        (is->iformat->flags & AVFMT_TS_DISCONT)) {
        int64_t pkt_dts = av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);
        int64_t delta   = pkt_dts - ist->next_dts;

        if ((FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE || pkt_dts + 1 < ist->last_dts) && !copy_ts) {
            ifile->ts_offset -= delta;
            av_log(NULL, AV_LOG_DEBUG,
                   "timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                   delta, ifile->ts_offset);
            pkt.dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            if (pkt.pts != AV_NOPTS_VALUE)
                pkt.pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
        }
    }

    ret = output_packet(ist, &pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while decoding stream #%d:%d\n",
               ist->file_index, ist->st->index);
        if (exit_on_error)
            exit_program(1);
    }

discard_packet:
    av_free_packet(&pkt);

    return 0;
}

/*
 * The following code is the main loop of the file converter
 */
static int transcode(void)
{
    int ret, i, need_input = 1;
    AVFormatContext *os;
    OutputStream *ost;
    InputStream *ist;
    int64_t timer_start;

    ret = transcode_init();
    if (ret < 0)
        goto fail;

    av_log(NULL, AV_LOG_INFO, "Press ctrl-c to stop encoding\n");
    term_init();

    timer_start = av_gettime();

#if HAVE_PTHREADS
    if ((ret = init_input_threads()) < 0)
        goto fail;
#endif

    while (!received_sigterm) {
        /* check if there's any stream where output is still needed */
        if (!need_output()) {
            av_log(NULL, AV_LOG_VERBOSE, "No more output streams to write to, finishing.\n");
            break;
        }

        /* read and process one input packet if needed */
        if (need_input) {
            ret = process_input();
            if (ret == AVERROR_EOF)
                need_input = 0;
        }

        ret = poll_filters();
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                continue;

            av_log(NULL, AV_LOG_ERROR, "Error while filtering.\n");
            break;
        }

        /* dump report by using the output first video and audio streams */
        print_report(0, timer_start);
    }
#if HAVE_PTHREADS
    free_input_threads();
#endif

    /* at the end of stream, we must flush the decoder buffers */
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];
        if (!input_files[ist->file_index]->eof_reached && ist->decoding_needed) {
            output_packet(ist, NULL);
        }
    }
    poll_filters();
    flush_encoders();

    term_exit();

    /* write the trailer if needed and close file */
    for (i = 0; i < nb_output_files; i++) {
        os = output_files[i]->ctx;
        av_write_trailer(os);
    }

    /* dump report by using the first video and audio streams */
    print_report(1, timer_start);

    /* close each encoder */
    for (i = 0; i < nb_output_streams; i++) {
        ost = output_streams[i];
        if (ost->encoding_needed) {
            av_freep(&ost->st->codec->stats_in);
            avcodec_close(ost->st->codec);
        }
    }

    /* close each decoder */
    for (i = 0; i < nb_input_streams; i++) {
        ist = input_streams[i];
        if (ist->decoding_needed) {
            avcodec_close(ist->st->codec);
            if (ist->hwaccel_uninit)
                ist->hwaccel_uninit(ist->st->codec);
        }
    }

    /* finished ! */
    ret = 0;

 fail:
#if HAVE_PTHREADS
    free_input_threads();
#endif

    if (output_streams) {
        for (i = 0; i < nb_output_streams; i++) {
            ost = output_streams[i];
            if (ost) {
                if (ost->stream_copy)
                    av_freep(&ost->st->codec->extradata);
                if (ost->logfile) {
                    fclose(ost->logfile);
                    ost->logfile = NULL;
                }
                av_freep(&ost->st->codec->subtitle_header);
                av_free(ost->forced_kf_pts);
                av_dict_free(&ost->opts);
                av_dict_free(&ost->resample_opts);
            }
        }
    }
    return ret;
}

static int64_t getutime(void)
{
#if HAVE_GETRUSAGE
    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    return (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
#elif HAVE_GETPROCESSTIMES
    HANDLE proc;
    FILETIME c, e, k, u;
    proc = GetCurrentProcess();
    GetProcessTimes(proc, &c, &e, &k, &u);
    return ((int64_t) u.dwHighDateTime << 32 | u.dwLowDateTime) / 10;
#else
    return av_gettime();
#endif
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
    int64_t ti;

    register_exit(avconv_cleanup);

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    avcodec_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avfilter_register_all();
    av_register_all();
    avformat_network_init();

    show_banner();

    /* parse options and open all input/output files */
    ret = avconv_parse_options(argc, argv);
    if (ret < 0)
        exit_program(1);

    if (nb_output_files <= 0 && nb_input_files == 0) {
        show_usage();
        av_log(NULL, AV_LOG_WARNING, "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit_program(1);
    }

    /* file converter / grab */
    if (nb_output_files <= 0) {
        fprintf(stderr, "At least one output file must be specified\n");
        exit_program(1);
    }

    ti = getutime();
    if (transcode() < 0)
        exit_program(1);
    ti = getutime() - ti;
    if (do_benchmark) {
        int maxrss = getmaxrss() / 1024;
        printf("bench: utime=%0.3fs maxrss=%ikB\n", ti / 1000000.0, maxrss);
    }

    exit_program(0);
    return 0;
}
