/*
 * ffmpeg main
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

#include "config.h"
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/audioconvert.h"
#include "libavutil/audioconvert.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/colorspace.h"
#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/libm.h"
#include "libavformat/os_support.h"

#include "libavformat/ffm.h" // not public API

#if CONFIG_AVFILTER
# include "libavfilter/avcodec.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/vsink_buffer.h"
# include "libavfilter/vsrc_buffer.h"
#endif

#if HAVE_SYS_RESOURCE_H
#include <sys/types.h>
#include <sys/time.h>
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

#if HAVE_TERMIOS_H
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#elif HAVE_KBHIT
#include <conio.h>
#endif
#include <time.h>

#include "cmdutils.h"

#include "libavutil/avassert.h"

const char program_name[] = "avconv";
const int program_birth_year = 2000;

/* select an input stream for an output stream */
typedef struct StreamMap {
    int disabled;           /** 1 is this mapping is disabled by a negative map */
    int file_index;
    int stream_index;
    int sync_file_index;
    int sync_stream_index;
} StreamMap;

/**
 * select an input file for an output file
 */
typedef struct MetadataMap {
    int  file;      ///< file index
    char type;      ///< type of metadata to copy -- (g)lobal, (s)tream, (c)hapter or (p)rogram
    int  index;     ///< stream/chapter/program number
} MetadataMap;

static const OptionDef options[];

#define MAX_STREAMS 1024    /* arbitrary sanity check value */
static const char *last_asked_format = NULL;
static AVDictionary *ts_scale;

static StreamMap *stream_maps = NULL;
static int nb_stream_maps;

static AVDictionary *codec_names;

/* first item specifies output metadata, second is input */
static MetadataMap (*meta_data_maps)[2] = NULL;
static int nb_meta_data_maps;
static int metadata_global_autocopy   = 1;
static int metadata_streams_autocopy  = 1;
static int metadata_chapters_autocopy = 1;

static int chapters_input_file = INT_MAX;

/* indexed by output file stream index */
static int *streamid_map = NULL;
static int nb_streamid_map = 0;

static int frame_width  = 0;
static int frame_height = 0;
static float frame_aspect_ratio = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_NONE;
static int frame_bits_per_raw_sample = 0;
static enum AVSampleFormat audio_sample_fmt = AV_SAMPLE_FMT_NONE;
static int max_frames[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
static AVRational frame_rate;
static float video_qscale = 0;
static uint16_t *intra_matrix = NULL;
static uint16_t *inter_matrix = NULL;
static const char *video_rc_override_string=NULL;
static int video_disable = 0;
static int video_discard = 0;
static unsigned int video_codec_tag = 0;
static char *video_language = NULL;
static int same_quant = 0;
static int do_deinterlace = 0;
static int top_field_first = -1;
static int me_threshold = 0;
static int intra_dc_precision = 8;
static int qp_hist = 0;
#if CONFIG_AVFILTER
static char *vfilters = NULL;
#endif

static int audio_sample_rate = 0;
#define QSCALE_NONE -99999
static float audio_qscale = QSCALE_NONE;
static int audio_disable = 0;
static int audio_channels = 0;
static unsigned int audio_codec_tag = 0;
static char *audio_language = NULL;

static int subtitle_disable = 0;
static char *subtitle_language = NULL;
static unsigned int subtitle_codec_tag = 0;

static int data_disable = 0;
static unsigned int data_codec_tag = 0;

static float mux_preload= 0.5;
static float mux_max_delay= 0.7;

static int64_t recording_time = INT64_MAX;
static int64_t start_time = 0;
static int64_t input_ts_offset = 0;
static int file_overwrite = 0;
static AVDictionary *metadata;
static int do_benchmark = 0;
static int do_hex_dump = 0;
static int do_pkt_dump = 0;
static int do_psnr = 0;
static int do_pass = 0;
static const char *pass_logfilename_prefix;
static int video_sync_method= -1;
static int audio_sync_method= 0;
static float audio_drift_threshold= 0.1;
static int copy_ts= 0;
static int copy_tb= 0;
static int opt_shortest = 0;
static char *vstats_filename;
static FILE *vstats_file;
static int opt_programid = 0;
static int copy_initial_nonkeyframes = 0;

static int rate_emu = 0;

static int audio_volume = 256;

static int exit_on_error = 0;
static int using_stdin = 0;
static int verbose = 1;
static int run_as_daemon  = 0;
static int thread_count= 1;
static int q_pressed = 0;
static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;
static int input_sync;
static uint64_t limit_filesize = UINT64_MAX;
static int force_fps = 0;
static char *forced_key_frames = NULL;

static float dts_delta_threshold = 10;

static uint8_t *audio_buf;
static uint8_t *audio_out;
static unsigned int allocated_audio_out_size, allocated_audio_buf_size;

static short *samples;

static AVBitStreamFilterContext *video_bitstream_filters=NULL;
static AVBitStreamFilterContext *audio_bitstream_filters=NULL;
static AVBitStreamFilterContext *subtitle_bitstream_filters=NULL;

#define DEFAULT_PASS_LOGFILENAME_PREFIX "av2pass"

typedef struct InputStream {
    int file_index;
    AVStream *st;
    int discard;             /* true if stream data should be discarded */
    int decoding_needed;     /* true if the packets must be decoded in 'raw_fifo' */
    AVCodec *dec;

    int64_t       start;     /* time when read started */
    int64_t       next_pts;  /* synthetic pts for cases where pkt.pts
                                is not defined */
    int64_t       pts;       /* current pts */
    double ts_scale;
    int is_start;            /* is 1 at the start and after a discontinuity */
    int showed_multi_packet_warning;
    AVDictionary *opts;
} InputStream;

typedef struct InputFile {
    AVFormatContext *ctx;
    int eof_reached;      /* true if eof reached */
    int ist_index;        /* index of first stream in ist_table */
    int buffer_size;      /* current total buffer size */
    int nb_streams;
    int64_t ts_offset;
} InputFile;

typedef struct OutputStream {
    int file_index;          /* file index */
    int index;               /* stream index in the output file */
    int source_index;        /* InputStream index */
    AVStream *st;            /* stream in the output file */
    int encoding_needed;     /* true if encoding needed for this stream */
    int frame_number;
    /* input pts and corresponding output pts
       for A/V sync */
    //double sync_ipts;        /* dts from the AVPacket of the demuxer in second units */
    struct InputStream *sync_ist; /* input stream to sync against */
    int64_t sync_opts;       /* output frame counter, could be changed to some true timestamp */ //FIXME look at frame_number
    AVBitStreamFilterContext *bitstream_filters;
    AVCodec *enc;

    /* video only */
    int video_resample;
    AVFrame resample_frame;              /* temporary frame for image resampling */
    struct SwsContext *img_resample_ctx; /* for image resampling */
    int resample_height;
    int resample_width;
    int resample_pix_fmt;
    AVRational frame_rate;

    float frame_aspect_ratio;

    /* forced key frames */
    int64_t *forced_kf_pts;
    int forced_kf_count;
    int forced_kf_index;

    /* audio only */
    int audio_resample;
    ReSampleContext *resample; /* for audio resampling */
    int resample_sample_fmt;
    int resample_channels;
    int resample_sample_rate;
    int reformat_pair;
    AVAudioConvert *reformat_ctx;
    AVFifoBuffer *fifo;     /* for compression: one audio fifo per codec */
    FILE *logfile;

#if CONFIG_AVFILTER
    AVFilterContext *output_video_filter;
    AVFilterContext *input_video_filter;
    AVFilterBufferRef *picref;
    char *avfilter;
    AVFilterGraph *graph;
#endif

   int sws_flags;
   AVDictionary *opts;
   int is_past_recording_time;
} OutputStream;

#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
#endif

typedef struct OutputFile {
    AVFormatContext *ctx;
    AVDictionary *opts;
    int ost_index;       /* index of the first stream in output_streams */
    int64_t recording_time; /* desired length of the resulting file in microseconds */
    int64_t start_time;     /* start time in microseconds */
    uint64_t limit_filesize;
} OutputFile;

static InputStream *input_streams = NULL;
static int         nb_input_streams = 0;
static InputFile   *input_files   = NULL;
static int         nb_input_files   = 0;

static OutputStream *output_streams = NULL;
static int        nb_output_streams = 0;
static OutputFile   *output_files   = NULL;
static int        nb_output_files   = 0;

#if CONFIG_AVFILTER

static int configure_video_filters(InputStream *ist, OutputStream *ost)
{
    AVFilterContext *last_filter, *filter;
    /** filter graph containing all filters including input & output */
    AVCodecContext *codec = ost->st->codec;
    AVCodecContext *icodec = ist->st->codec;
    enum PixelFormat pix_fmts[] = { codec->pix_fmt, PIX_FMT_NONE };
    AVRational sample_aspect_ratio;
    char args[255];
    int ret;

    ost->graph = avfilter_graph_alloc();

    if (ist->st->sample_aspect_ratio.num){
        sample_aspect_ratio = ist->st->sample_aspect_ratio;
    }else
        sample_aspect_ratio = ist->st->codec->sample_aspect_ratio;

    snprintf(args, 255, "%d:%d:%d:%d:%d:%d:%d", ist->st->codec->width,
             ist->st->codec->height, ist->st->codec->pix_fmt, 1, AV_TIME_BASE,
             sample_aspect_ratio.num, sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&ost->input_video_filter, avfilter_get_by_name("buffer"),
                                       "src", args, NULL, ost->graph);
    if (ret < 0)
        return ret;
    ret = avfilter_graph_create_filter(&ost->output_video_filter, avfilter_get_by_name("buffersink"),
                                       "out", NULL, pix_fmts, ost->graph);
    if (ret < 0)
        return ret;
    last_filter = ost->input_video_filter;

    if (codec->width  != icodec->width || codec->height != icodec->height) {
        snprintf(args, 255, "%d:%d:flags=0x%X",
                 codec->width,
                 codec->height,
                 ost->sws_flags);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                NULL, args, NULL, ost->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, 0, filter, 0)) < 0)
            return ret;
        last_filter = filter;
    }

    snprintf(args, sizeof(args), "flags=0x%X", ost->sws_flags);
    ost->graph->scale_sws_opts = av_strdup(args);

    if (ost->avfilter) {
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs  = avfilter_inout_alloc();

        outputs->name    = av_strdup("in");
        outputs->filter_ctx = last_filter;
        outputs->pad_idx = 0;
        outputs->next    = NULL;

        inputs->name    = av_strdup("out");
        inputs->filter_ctx = ost->output_video_filter;
        inputs->pad_idx = 0;
        inputs->next    = NULL;

        if ((ret = avfilter_graph_parse(ost->graph, ost->avfilter, &inputs, &outputs, NULL)) < 0)
            return ret;
        av_freep(&ost->avfilter);
    } else {
        if ((ret = avfilter_link(last_filter, 0, ost->output_video_filter, 0)) < 0)
            return ret;
    }

    if ((ret = avfilter_graph_config(ost->graph, NULL)) < 0)
        return ret;

    codec->width  = ost->output_video_filter->inputs[0]->w;
    codec->height = ost->output_video_filter->inputs[0]->h;
    codec->sample_aspect_ratio = ost->st->sample_aspect_ratio =
        ost->frame_aspect_ratio ? // overridden by the -aspect cli option
        av_d2q(ost->frame_aspect_ratio*codec->height/codec->width, 255) :
        ost->output_video_filter->inputs[0]->sample_aspect_ratio;

    return 0;
}
#endif /* CONFIG_AVFILTER */

static void term_exit(void)
{
    av_log(NULL, AV_LOG_QUIET, "%s", "");
#if HAVE_TERMIOS_H
    if(!run_as_daemon)
        tcsetattr (0, TCSANOW, &oldtty);
#endif
}

static volatile int received_sigterm = 0;

static void
sigterm_handler(int sig)
{
    received_sigterm = sig;
    q_pressed++;
    term_exit();
}

static void term_init(void)
{
#if HAVE_TERMIOS_H
    if(!run_as_daemon){
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;
    atexit(term_exit);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr (0, TCSANOW, &tty);
    signal(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
    }
#endif

    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).  */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
#ifdef SIGXCPU
    signal(SIGXCPU, sigterm_handler);
#endif
}

/* read a key without blocking */
static int read_key(void)
{
#if HAVE_TERMIOS_H
    int n = 1;
    unsigned char ch;
    struct timeval tv;
    fd_set rfds;

    if(run_as_daemon)
        return -1;

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
    if(kbhit())
        return(getch());
#endif
    return -1;
}

static int decode_interrupt_cb(void)
{
    q_pressed += read_key() == 'q';
    return q_pressed > 1;
}

static int exit_program(int ret)
{
    int i;

    /* close files */
    for(i=0;i<nb_output_files;i++) {
        AVFormatContext *s = output_files[i].ctx;
        if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
            avio_close(s->pb);
        avformat_free_context(s);
        av_dict_free(&output_files[i].opts);
    }
    for(i=0;i<nb_input_files;i++) {
        av_close_input_file(input_files[i].ctx);
    }
    for (i = 0; i < nb_input_streams; i++)
        av_dict_free(&input_streams[i].opts);

    av_free(intra_matrix);
    av_free(inter_matrix);

    if (vstats_file)
        fclose(vstats_file);
    av_free(vstats_filename);

    av_free(meta_data_maps);

    av_freep(&input_streams);
    av_freep(&input_files);
    av_freep(&output_streams);
    av_freep(&output_files);

    uninit_opts();
    av_free(audio_buf);
    av_free(audio_out);
    allocated_audio_buf_size= allocated_audio_out_size= 0;
    av_free(samples);

#if CONFIG_AVFILTER
    avfilter_uninit();
#endif

    if (received_sigterm) {
        fprintf(stderr,
            "Received signal %d: terminating.\n",
            (int) received_sigterm);
        exit (255);
    }

    exit(ret); /* not all OS-es handle main() return value */
    return ret;
}

static void assert_avoptions(AVDictionary *m)
{
    AVDictionaryEntry *t;
    if ((t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        exit_program(1);
    }
}

static void assert_codec_experimental(AVCodecContext *c, int encoder)
{
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;
    if (c->codec->capabilities & CODEC_CAP_EXPERIMENTAL &&
        c->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(NULL, AV_LOG_ERROR, "%s '%s' is experimental and might produce bad "
                "results.\nAdd '-strict experimental' if you want to use it.\n",
                codec_string, c->codec->name);
        codec = encoder ? avcodec_find_encoder(c->codec->id) : avcodec_find_decoder(c->codec->id);
        if (!(codec->capabilities & CODEC_CAP_EXPERIMENTAL))
            av_log(NULL, AV_LOG_ERROR, "Or use the non experimental %s '%s'.\n",
                   codec_string, codec->name);
        exit_program(1);
    }
}

/* similar to ff_dynarray_add() and av_fast_realloc() */
static void *grow_array(void *array, int elem_size, int *size, int new_size)
{
    if (new_size >= INT_MAX / elem_size) {
        fprintf(stderr, "Array too big.\n");
        exit_program(1);
    }
    if (*size < new_size) {
        uint8_t *tmp = av_realloc(array, new_size*elem_size);
        if (!tmp) {
            fprintf(stderr, "Could not alloc buffer.\n");
            exit_program(1);
        }
        memset(tmp + *size*elem_size, 0, (new_size-*size) * elem_size);
        *size = new_size;
        return tmp;
    }
    return array;
}

static void choose_sample_fmt(AVStream *st, AVCodec *codec)
{
    if(codec && codec->sample_fmts){
        const enum AVSampleFormat *p= codec->sample_fmts;
        for(; *p!=-1; p++){
            if(*p == st->codec->sample_fmt)
                break;
        }
        if (*p == -1) {
            if((codec->capabilities & CODEC_CAP_LOSSLESS) && av_get_sample_fmt_name(st->codec->sample_fmt) > av_get_sample_fmt_name(codec->sample_fmts[0]))
                av_log(NULL, AV_LOG_ERROR, "Convertion will not be lossless'\n");
            if(av_get_sample_fmt_name(st->codec->sample_fmt))
            av_log(NULL, AV_LOG_WARNING,
                   "Incompatible sample format '%s' for codec '%s', auto-selecting format '%s'\n",
                   av_get_sample_fmt_name(st->codec->sample_fmt),
                   codec->name,
                   av_get_sample_fmt_name(codec->sample_fmts[0]));
            st->codec->sample_fmt = codec->sample_fmts[0];
        }
    }
}

static void choose_sample_rate(AVStream *st, AVCodec *codec)
{
    if(codec && codec->supported_samplerates){
        const int *p= codec->supported_samplerates;
        int best=0;
        int best_dist=INT_MAX;
        for(; *p; p++){
            int dist= abs(st->codec->sample_rate - *p);
            if(dist < best_dist){
                best_dist= dist;
                best= *p;
            }
        }
        if(best_dist){
            av_log(st->codec, AV_LOG_WARNING, "Requested sampling rate unsupported using closest supported (%d)\n", best);
        }
        st->codec->sample_rate= best;
    }
}

static void choose_pixel_fmt(AVStream *st, AVCodec *codec)
{
    if(codec && codec->pix_fmts){
        const enum PixelFormat *p= codec->pix_fmts;
        if(st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL){
            if(st->codec->codec_id==CODEC_ID_MJPEG){
                p= (const enum PixelFormat[]){PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_NONE};
            }else if(st->codec->codec_id==CODEC_ID_LJPEG){
                p= (const enum PixelFormat[]){PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_BGRA, PIX_FMT_NONE};
            }
        }
        for(; *p!=-1; p++){
            if(*p == st->codec->pix_fmt)
                break;
        }
        if (*p == -1) {
            if(st->codec->pix_fmt != PIX_FMT_NONE)
                av_log(NULL, AV_LOG_WARNING,
                        "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
                        av_pix_fmt_descriptors[st->codec->pix_fmt].name,
                        codec->name,
                        av_pix_fmt_descriptors[codec->pix_fmts[0]].name);
            st->codec->pix_fmt = codec->pix_fmts[0];
        }
    }
}

static double
get_sync_ipts(const OutputStream *ost)
{
    const InputStream *ist = ost->sync_ist;
    OutputFile *of = &output_files[ost->file_index];
    return (double)(ist->pts - of->start_time)/AV_TIME_BASE;
}

static void write_frame(AVFormatContext *s, AVPacket *pkt, AVCodecContext *avctx, AVBitStreamFilterContext *bsfc){
    int ret;

    while(bsfc){
        AVPacket new_pkt= *pkt;
        int a= av_bitstream_filter_filter(bsfc, avctx, NULL,
                                          &new_pkt.data, &new_pkt.size,
                                          pkt->data, pkt->size,
                                          pkt->flags & AV_PKT_FLAG_KEY);
        if(a>0){
            av_free_packet(pkt);
            new_pkt.destruct= av_destruct_packet;
        } else if(a<0){
            fprintf(stderr, "%s failed for stream %d, codec %s",
                    bsfc->filter->name, pkt->stream_index,
                    avctx->codec ? avctx->codec->name : "copy");
            print_error("", a);
            if (exit_on_error)
                exit_program(1);
        }
        *pkt= new_pkt;

        bsfc= bsfc->next;
    }

    ret= av_interleaved_write_frame(s, pkt);
    if(ret < 0){
        print_error("av_interleaved_write_frame()", ret);
        exit_program(1);
    }
}

static void do_audio_out(AVFormatContext *s,
                         OutputStream *ost,
                         InputStream *ist,
                         unsigned char *buf, int size)
{
    uint8_t *buftmp;
    int64_t audio_out_size, audio_buf_size;
    int64_t allocated_for_size= size;

    int size_out, frame_bytes, ret, resample_changed;
    AVCodecContext *enc= ost->st->codec;
    AVCodecContext *dec= ist->st->codec;
    int osize = av_get_bytes_per_sample(enc->sample_fmt);
    int isize = av_get_bytes_per_sample(dec->sample_fmt);
    const int coded_bps = av_get_bits_per_sample(enc->codec->id);

need_realloc:
    audio_buf_size= (allocated_for_size + isize*dec->channels - 1) / (isize*dec->channels);
    audio_buf_size= (audio_buf_size*enc->sample_rate + dec->sample_rate) / dec->sample_rate;
    audio_buf_size= audio_buf_size*2 + 10000; //safety factors for the deprecated resampling API
    audio_buf_size= FFMAX(audio_buf_size, enc->frame_size);
    audio_buf_size*= osize*enc->channels;

    audio_out_size= FFMAX(audio_buf_size, enc->frame_size * osize * enc->channels);
    if(coded_bps > 8*osize)
        audio_out_size= audio_out_size * coded_bps / (8*osize);
    audio_out_size += FF_MIN_BUFFER_SIZE;

    if(audio_out_size > INT_MAX || audio_buf_size > INT_MAX){
        fprintf(stderr, "Buffer sizes too large\n");
        exit_program(1);
    }

    av_fast_malloc(&audio_buf, &allocated_audio_buf_size, audio_buf_size);
    av_fast_malloc(&audio_out, &allocated_audio_out_size, audio_out_size);
    if (!audio_buf || !audio_out){
        fprintf(stderr, "Out of memory in do_audio_out\n");
        exit_program(1);
    }

    if (enc->channels != dec->channels)
        ost->audio_resample = 1;

    resample_changed = ost->resample_sample_fmt  != dec->sample_fmt ||
                       ost->resample_channels    != dec->channels   ||
                       ost->resample_sample_rate != dec->sample_rate;

    if ((ost->audio_resample && !ost->resample) || resample_changed) {
        if (resample_changed) {
            av_log(NULL, AV_LOG_INFO, "Input stream #%d.%d frame changed from rate:%d fmt:%s ch:%d to rate:%d fmt:%s ch:%d\n",
                   ist->file_index, ist->st->index,
                   ost->resample_sample_rate, av_get_sample_fmt_name(ost->resample_sample_fmt), ost->resample_channels,
                   dec->sample_rate, av_get_sample_fmt_name(dec->sample_fmt), dec->channels);
            ost->resample_sample_fmt  = dec->sample_fmt;
            ost->resample_channels    = dec->channels;
            ost->resample_sample_rate = dec->sample_rate;
            if (ost->resample)
                audio_resample_close(ost->resample);
        }
        /* if audio_sync_method is >1 the resampler is needed for audio drift compensation */
        if (audio_sync_method <= 1 &&
            ost->resample_sample_fmt  == enc->sample_fmt &&
            ost->resample_channels    == enc->channels   &&
            ost->resample_sample_rate == enc->sample_rate) {
            ost->resample = NULL;
            ost->audio_resample = 0;
        } else {
            if (dec->sample_fmt != AV_SAMPLE_FMT_S16)
                fprintf(stderr, "Warning, using s16 intermediate sample format for resampling\n");
            ost->resample = av_audio_resample_init(enc->channels,    dec->channels,
                                                   enc->sample_rate, dec->sample_rate,
                                                   enc->sample_fmt,  dec->sample_fmt,
                                                   16, 10, 0, 0.8);
            if (!ost->resample) {
                fprintf(stderr, "Can not resample %d channels @ %d Hz to %d channels @ %d Hz\n",
                        dec->channels, dec->sample_rate,
                        enc->channels, enc->sample_rate);
                exit_program(1);
            }
        }
    }

#define MAKE_SFMT_PAIR(a,b) ((a)+AV_SAMPLE_FMT_NB*(b))
    if (!ost->audio_resample && dec->sample_fmt!=enc->sample_fmt &&
        MAKE_SFMT_PAIR(enc->sample_fmt,dec->sample_fmt)!=ost->reformat_pair) {
        if (ost->reformat_ctx)
            av_audio_convert_free(ost->reformat_ctx);
        ost->reformat_ctx = av_audio_convert_alloc(enc->sample_fmt, 1,
                                                   dec->sample_fmt, 1, NULL, 0);
        if (!ost->reformat_ctx) {
            fprintf(stderr, "Cannot convert %s sample format to %s sample format\n",
                av_get_sample_fmt_name(dec->sample_fmt),
                av_get_sample_fmt_name(enc->sample_fmt));
            exit_program(1);
        }
        ost->reformat_pair=MAKE_SFMT_PAIR(enc->sample_fmt,dec->sample_fmt);
    }

    if(audio_sync_method){
        double delta = get_sync_ipts(ost) * enc->sample_rate - ost->sync_opts
                - av_fifo_size(ost->fifo)/(enc->channels * 2);
        double idelta= delta*dec->sample_rate / enc->sample_rate;
        int byte_delta= ((int)idelta)*2*dec->channels;

        //FIXME resample delay
        if(fabs(delta) > 50){
            if(ist->is_start || fabs(delta) > audio_drift_threshold*enc->sample_rate){
                if(byte_delta < 0){
                    byte_delta= FFMAX(byte_delta, -size);
                    size += byte_delta;
                    buf  -= byte_delta;
                    if(verbose > 2)
                        fprintf(stderr, "discarding %d audio samples\n", (int)-delta);
                    if(!size)
                        return;
                    ist->is_start=0;
                }else{
                    static uint8_t *input_tmp= NULL;
                    input_tmp= av_realloc(input_tmp, byte_delta + size);

                    if(byte_delta > allocated_for_size - size){
                        allocated_for_size= byte_delta + (int64_t)size;
                        goto need_realloc;
                    }
                    ist->is_start=0;

                    memset(input_tmp, 0, byte_delta);
                    memcpy(input_tmp + byte_delta, buf, size);
                    buf= input_tmp;
                    size += byte_delta;
                    if(verbose > 2)
                        fprintf(stderr, "adding %d audio samples of silence\n", (int)delta);
                }
            }else if(audio_sync_method>1){
                int comp= av_clip(delta, -audio_sync_method, audio_sync_method);
                av_assert0(ost->audio_resample);
                if(verbose > 2)
                    fprintf(stderr, "compensating audio timestamp drift:%f compensation:%d in:%d\n", delta, comp, enc->sample_rate);
//                fprintf(stderr, "drift:%f len:%d opts:%"PRId64" ipts:%"PRId64" fifo:%d\n", delta, -1, ost->sync_opts, (int64_t)(get_sync_ipts(ost) * enc->sample_rate), av_fifo_size(ost->fifo)/(ost->st->codec->channels * 2));
                av_resample_compensate(*(struct AVResampleContext**)ost->resample, comp, enc->sample_rate);
            }
        }
    }else
        ost->sync_opts= lrintf(get_sync_ipts(ost) * enc->sample_rate)
                        - av_fifo_size(ost->fifo)/(enc->channels * 2); //FIXME wrong

    if (ost->audio_resample) {
        buftmp = audio_buf;
        size_out = audio_resample(ost->resample,
                                  (short *)buftmp, (short *)buf,
                                  size / (dec->channels * isize));
        size_out = size_out * enc->channels * osize;
    } else {
        buftmp = buf;
        size_out = size;
    }

    if (!ost->audio_resample && dec->sample_fmt!=enc->sample_fmt) {
        const void *ibuf[6]= {buftmp};
        void *obuf[6]= {audio_buf};
        int istride[6]= {isize};
        int ostride[6]= {osize};
        int len= size_out/istride[0];
        if (av_audio_convert(ost->reformat_ctx, obuf, ostride, ibuf, istride, len)<0) {
            printf("av_audio_convert() failed\n");
            if (exit_on_error)
                exit_program(1);
            return;
        }
        buftmp = audio_buf;
        size_out = len*osize;
    }

    /* now encode as many frames as possible */
    if (enc->frame_size > 1) {
        /* output resampled raw samples */
        if (av_fifo_realloc2(ost->fifo, av_fifo_size(ost->fifo) + size_out) < 0) {
            fprintf(stderr, "av_fifo_realloc2() failed\n");
            exit_program(1);
        }
        av_fifo_generic_write(ost->fifo, buftmp, size_out, NULL);

        frame_bytes = enc->frame_size * osize * enc->channels;

        while (av_fifo_size(ost->fifo) >= frame_bytes) {
            AVPacket pkt;
            av_init_packet(&pkt);

            av_fifo_generic_read(ost->fifo, audio_buf, frame_bytes, NULL);

            //FIXME pass ost->sync_opts as AVFrame.pts in avcodec_encode_audio()

            ret = avcodec_encode_audio(enc, audio_out, audio_out_size,
                                       (short *)audio_buf);
            if (ret < 0) {
                fprintf(stderr, "Audio encoding failed\n");
                exit_program(1);
            }
            audio_size += ret;
            pkt.stream_index= ost->index;
            pkt.data= audio_out;
            pkt.size= ret;
            if(enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
                pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
            pkt.flags |= AV_PKT_FLAG_KEY;
            write_frame(s, &pkt, enc, ost->bitstream_filters);

            ost->sync_opts += enc->frame_size;
        }
    } else {
        AVPacket pkt;
        av_init_packet(&pkt);

        ost->sync_opts += size_out / (osize * enc->channels);

        /* output a pcm frame */
        /* determine the size of the coded buffer */
        size_out /= osize;
        if (coded_bps)
            size_out = size_out*coded_bps/8;

        if(size_out > audio_out_size){
            fprintf(stderr, "Internal error, buffer size too small\n");
            exit_program(1);
        }

        //FIXME pass ost->sync_opts as AVFrame.pts in avcodec_encode_audio()
        ret = avcodec_encode_audio(enc, audio_out, size_out,
                                   (short *)buftmp);
        if (ret < 0) {
            fprintf(stderr, "Audio encoding failed\n");
            exit_program(1);
        }
        audio_size += ret;
        pkt.stream_index= ost->index;
        pkt.data= audio_out;
        pkt.size= ret;
        if(enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
            pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
        pkt.flags |= AV_PKT_FLAG_KEY;
        write_frame(s, &pkt, enc, ost->bitstream_filters);
    }
}

static void pre_process_video_frame(InputStream *ist, AVPicture *picture, void **bufp)
{
    AVCodecContext *dec;
    AVPicture *picture2;
    AVPicture picture_tmp;
    uint8_t *buf = 0;

    dec = ist->st->codec;

    /* deinterlace : must be done before any resize */
    if (do_deinterlace) {
        int size;

        /* create temporary picture */
        size = avpicture_get_size(dec->pix_fmt, dec->width, dec->height);
        buf = av_malloc(size);
        if (!buf)
            return;

        picture2 = &picture_tmp;
        avpicture_fill(picture2, buf, dec->pix_fmt, dec->width, dec->height);

        if(avpicture_deinterlace(picture2, picture,
                                 dec->pix_fmt, dec->width, dec->height) < 0) {
            /* if error, do not deinterlace */
            fprintf(stderr, "Deinterlacing failed\n");
            av_free(buf);
            buf = NULL;
            picture2 = picture;
        }
    } else {
        picture2 = picture;
    }

    if (picture != picture2)
        *picture = *picture2;
    *bufp = buf;
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
        fprintf(stderr, "Subtitle packets must have a pts\n");
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
    if (enc->codec_id == CODEC_ID_DVB_SUBTITLE)
        nb = 2;
    else
        nb = 1;

    for(i = 0; i < nb; i++) {
        sub->pts = av_rescale_q(pts, ist->st->time_base, AV_TIME_BASE_Q);
        // start_display_time is required to be 0
        sub->pts              += av_rescale_q(sub->start_display_time, (AVRational){1, 1000}, AV_TIME_BASE_Q);
        sub->end_display_time -= sub->start_display_time;
        sub->start_display_time = 0;
        subtitle_out_size = avcodec_encode_subtitle(enc, subtitle_out,
                                                    subtitle_out_max_size, sub);
        if (subtitle_out_size < 0) {
            fprintf(stderr, "Subtitle encoding failed\n");
            exit_program(1);
        }

        av_init_packet(&pkt);
        pkt.stream_index = ost->index;
        pkt.data = subtitle_out;
        pkt.size = subtitle_out_size;
        pkt.pts = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->st->time_base);
        if (enc->codec_id == CODEC_ID_DVB_SUBTITLE) {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt.pts += 90 * sub->start_display_time;
            else
                pkt.pts += 90 * sub->end_display_time;
        }
        write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
    }
}

static int bit_buffer_size= 1024*256;
static uint8_t *bit_buffer= NULL;

static void do_video_resample(OutputStream *ost,
                              InputStream *ist,
                              AVFrame *in_picture,
                              AVFrame **out_picture)
{
    int resample_changed = 0;
    AVCodecContext *dec = ist->st->codec;
    AVCodecContext *enc = ost->st->codec;
    *out_picture = in_picture;

    resample_changed = ost->resample_width   != dec->width  ||
                       ost->resample_height  != dec->height ||
                       ost->resample_pix_fmt != dec->pix_fmt;

#if !CONFIG_AVFILTER
    if (resample_changed) {
        av_log(NULL, AV_LOG_INFO,
               "Input stream #%d.%d frame changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
               ist->file_index, ist->st->index,
               ost->resample_width, ost->resample_height, av_get_pix_fmt_name(ost->resample_pix_fmt),
               dec->width         , dec->height         , av_get_pix_fmt_name(dec->pix_fmt));
        ost->resample_width   = dec->width;
        ost->resample_height  = dec->height;
        ost->resample_pix_fmt = dec->pix_fmt;
    }

    ost->video_resample = dec->width   != enc->width  ||
                          dec->height  != enc->height ||
                          dec->pix_fmt != enc->pix_fmt;

    if (ost->video_resample) {
        *out_picture = &ost->resample_frame;
        if (!ost->img_resample_ctx || resample_changed) {
            /* initialize the destination picture */
            if (!ost->resample_frame.data[0]) {
                avcodec_get_frame_defaults(&ost->resample_frame);
                if (avpicture_alloc((AVPicture *)&ost->resample_frame, enc->pix_fmt,
                                    enc->width, enc->height)) {
                    fprintf(stderr, "Cannot allocate temp picture, check pix fmt\n");
                    exit_program(1);
                }
            }
            /* initialize a new scaler context */
            sws_freeContext(ost->img_resample_ctx);
            ost->img_resample_ctx = sws_getContext(dec->width, dec->height, dec->pix_fmt,
                                                   enc->width, enc->height, enc->pix_fmt,
                                                   ost->sws_flags, NULL, NULL, NULL);
            if (ost->img_resample_ctx == NULL) {
                fprintf(stderr, "Cannot get resampling context\n");
                exit_program(1);
            }
        }
        sws_scale(ost->img_resample_ctx, in_picture->data, in_picture->linesize,
              0, ost->resample_height, (*out_picture)->data, (*out_picture)->linesize);
    }
#else
    if (resample_changed) {
        avfilter_graph_free(&ost->graph);
        if (configure_video_filters(ist, ost)) {
            fprintf(stderr, "Error reinitializing filters!\n");
            exit_program(1);
        }
    }
#endif
    if (resample_changed) {
        ost->resample_width   = dec->width;
        ost->resample_height  = dec->height;
        ost->resample_pix_fmt = dec->pix_fmt;
    }
}


static void do_video_out(AVFormatContext *s,
                         OutputStream *ost,
                         InputStream *ist,
                         AVFrame *in_picture,
                         int *frame_size, float quality)
{
    int nb_frames, i, ret, format_video_sync;
    AVFrame *final_picture;
    AVCodecContext *enc, *dec;
    double sync_ipts;

    enc = ost->st->codec;
    dec = ist->st->codec;

    sync_ipts = get_sync_ipts(ost) / av_q2d(enc->time_base);

    /* by default, we output a single frame */
    nb_frames = 1;

    *frame_size = 0;

    format_video_sync = video_sync_method;
    if (format_video_sync < 0)
        format_video_sync = (s->oformat->flags & AVFMT_VARIABLE_FPS) ? 2 : 1;

    if (format_video_sync) {
        double vdelta = sync_ipts - ost->sync_opts;
        //FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (vdelta < -1.1)
            nb_frames = 0;
        else if (format_video_sync == 2) {
            if(vdelta<=-0.6){
                nb_frames=0;
            }else if(vdelta>0.6)
                ost->sync_opts= lrintf(sync_ipts);
        }else if (vdelta > 1.1)
            nb_frames = lrintf(vdelta);
//fprintf(stderr, "vdelta:%f, ost->sync_opts:%"PRId64", ost->sync_ipts:%f nb_frames:%d\n", vdelta, ost->sync_opts, get_sync_ipts(ost), nb_frames);
        if (nb_frames == 0){
            ++nb_frames_drop;
            if (verbose>2)
                fprintf(stderr, "*** drop!\n");
        }else if (nb_frames > 1) {
            nb_frames_dup += nb_frames - 1;
            if (verbose>2)
                fprintf(stderr, "*** %d dup!\n", nb_frames-1);
        }
    }else
        ost->sync_opts= lrintf(sync_ipts);

    nb_frames= FFMIN(nb_frames, max_frames[AVMEDIA_TYPE_VIDEO] - ost->frame_number);
    if (nb_frames <= 0)
        return;

    do_video_resample(ost, ist, in_picture, &final_picture);

    /* duplicates frame if needed */
    for(i=0;i<nb_frames;i++) {
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.stream_index= ost->index;

        if (s->oformat->flags & AVFMT_RAWPICTURE) {
            /* raw pictures are written as AVPicture structure to
               avoid any copies. We support temporarily the older
               method. */
            AVFrame* old_frame = enc->coded_frame;
            enc->coded_frame = dec->coded_frame; //FIXME/XXX remove this hack
            pkt.data= (uint8_t *)final_picture;
            pkt.size=  sizeof(AVPicture);
            pkt.pts= av_rescale_q(ost->sync_opts, enc->time_base, ost->st->time_base);
            pkt.flags |= AV_PKT_FLAG_KEY;

            write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
            enc->coded_frame = old_frame;
        } else {
            AVFrame big_picture;

            big_picture= *final_picture;
            /* better than nothing: use input picture interlaced
               settings */
            big_picture.interlaced_frame = in_picture->interlaced_frame;
            if (ost->st->codec->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME)) {
                if(top_field_first == -1)
                    big_picture.top_field_first = in_picture->top_field_first;
                else
                    big_picture.top_field_first = top_field_first;
            }

            /* handles same_quant here. This is not correct because it may
               not be a global option */
            big_picture.quality = quality;
            if(!me_threshold)
                big_picture.pict_type = 0;
//            big_picture.pts = AV_NOPTS_VALUE;
            big_picture.pts= ost->sync_opts;
//            big_picture.pts= av_rescale(ost->sync_opts, AV_TIME_BASE*(int64_t)enc->time_base.num, enc->time_base.den);
//av_log(NULL, AV_LOG_DEBUG, "%"PRId64" -> encoder\n", ost->sync_opts);
            if (ost->forced_kf_index < ost->forced_kf_count &&
                big_picture.pts >= ost->forced_kf_pts[ost->forced_kf_index]) {
                big_picture.pict_type = AV_PICTURE_TYPE_I;
                ost->forced_kf_index++;
            }
            ret = avcodec_encode_video(enc,
                                       bit_buffer, bit_buffer_size,
                                       &big_picture);
            if (ret < 0) {
                fprintf(stderr, "Video encoding failed\n");
                exit_program(1);
            }

            if(ret>0){
                pkt.data= bit_buffer;
                pkt.size= ret;
                if(enc->coded_frame->pts != AV_NOPTS_VALUE)
                    pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
/*av_log(NULL, AV_LOG_DEBUG, "encoder -> %"PRId64"/%"PRId64"\n",
   pkt.pts != AV_NOPTS_VALUE ? av_rescale(pkt.pts, enc->time_base.den, AV_TIME_BASE*(int64_t)enc->time_base.num) : -1,
   pkt.dts != AV_NOPTS_VALUE ? av_rescale(pkt.dts, enc->time_base.den, AV_TIME_BASE*(int64_t)enc->time_base.num) : -1);*/

                if(enc->coded_frame->key_frame)
                    pkt.flags |= AV_PKT_FLAG_KEY;
                write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
                *frame_size = ret;
                video_size += ret;
                //fprintf(stderr,"\nFrame: %3d size: %5d type: %d",
                //        enc->frame_number-1, ret, enc->pict_type);
                /* if two pass, output log */
                if (ost->logfile && enc->stats_out) {
                    fprintf(ost->logfile, "%s", enc->stats_out);
                }
            }
        }
        ost->sync_opts++;
        ost->frame_number++;
    }
}

static double psnr(double d){
    return -10.0*log(d)/log(10.0);
}

static void do_video_stats(AVFormatContext *os, OutputStream *ost,
                           int frame_size)
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
        fprintf(vstats_file, "frame= %5d q= %2.1f ", frame_number, enc->coded_frame->quality/(float)FF_QP2LAMBDA);
        if (enc->flags&CODEC_FLAG_PSNR)
            fprintf(vstats_file, "PSNR= %6.2f ", psnr(enc->coded_frame->error[0]/(enc->width*enc->height*255.0*255.0)));

        fprintf(vstats_file,"f_size= %6d ", frame_size);
        /* compute pts value */
        ti1 = ost->sync_opts * av_q2d(enc->time_base);
        if (ti1 < 0.01)
            ti1 = 0.01;

        bitrate = (frame_size * 8) / av_q2d(enc->time_base) / 1000.0;
        avg_bitrate = (double)(video_size * 8) / ti1 / 1000.0;
        fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
            (double)video_size / 1024, ti1, bitrate, avg_bitrate);
        fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(enc->coded_frame->pict_type));
    }
}

static void print_report(OutputFile *output_files,
                         OutputStream *ost_table, int nb_ostreams,
                         int is_last_report, int64_t timer_start)
{
    char buf[1024];
    OutputStream *ost;
    AVFormatContext *oc;
    int64_t total_size;
    AVCodecContext *enc;
    int frame_number, vid, i;
    double bitrate;
    int64_t pts = INT64_MAX;
    static int64_t last_time = -1;
    static int qp_histogram[52];

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


    oc = output_files[0].ctx;

    total_size = avio_size(oc->pb);
    if(total_size<0) // FIXME improve avio_size() so it works with non seekable output too
        total_size= avio_tell(oc->pb);

    buf[0] = '\0';
    vid = 0;
    for(i=0;i<nb_ostreams;i++) {
        float q = -1;
        ost = &ost_table[i];
        enc = ost->st->codec;
        if (!ost->st->stream_copy && enc->coded_frame)
            q = enc->coded_frame->quality/(float)FF_QP2LAMBDA;
        if (vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "q=%2.1f ", q);
        }
        if (!vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            float t = (av_gettime()-timer_start) / 1000000.0;

            frame_number = ost->frame_number;
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "frame=%5d fps=%3d q=%3.1f ",
                     frame_number, (t>1)?(int)(frame_number/t+0.5) : 0, q);
            if(is_last_report)
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "L");
            if(qp_hist){
                int j;
                int qp = lrintf(q);
                if(qp>=0 && qp<FF_ARRAY_ELEMS(qp_histogram))
                    qp_histogram[qp]++;
                for(j=0; j<32; j++)
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%X", (int)lrintf(log(qp_histogram[j]+1)/log(2)));
            }
            if (enc->flags&CODEC_FLAG_PSNR){
                int j;
                double error, error_sum=0;
                double scale, scale_sum=0;
                char type[3]= {'Y','U','V'};
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "PSNR=");
                for(j=0; j<3; j++){
                    if(is_last_report){
                        error= enc->error[j];
                        scale= enc->width*enc->height*255.0*255.0*frame_number;
                    }else{
                        error= enc->coded_frame->error[j];
                        scale= enc->width*enc->height*255.0*255.0;
                    }
                    if(j) scale/=4;
                    error_sum += error;
                    scale_sum += scale;
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%c:%2.2f ", type[j], psnr(error/scale));
                }
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "*:%2.2f ", psnr(error_sum/scale_sum));
            }
            vid = 1;
        }
        /* compute min output value */
        pts = FFMIN(pts, av_rescale_q(ost->st->pts.val,
                                      ost->st->time_base, AV_TIME_BASE_Q));
    }

    if (verbose > 0 || is_last_report) {
        int hours, mins, secs, us;
        secs = pts / AV_TIME_BASE;
        us = pts % AV_TIME_BASE;
        mins = secs / 60;
        secs %= 60;
        hours = mins / 60;
        mins %= 60;

        bitrate = pts ? total_size * 8 / (pts / 1000.0) : 0;

        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                 "size=%8.0fkB time=", total_size / 1024.0);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                 "%02d:%02d:%02d.%02d ", hours, mins, secs,
                 (100 * us) / AV_TIME_BASE);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                 "bitrate=%6.1fkbits/s", bitrate);

        if (nb_frames_dup || nb_frames_drop)
          snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " dup=%d drop=%d",
                  nb_frames_dup, nb_frames_drop);

        if (verbose >= 0)
            fprintf(stderr, "%s    \r", buf);

        fflush(stderr);
    }

    if (is_last_report && verbose >= 0){
        int64_t raw= audio_size + video_size + extra_size;
        fprintf(stderr, "\n");
        fprintf(stderr, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
                video_size/1024.0,
                audio_size/1024.0,
                extra_size/1024.0,
                100.0*(total_size - raw)/raw
        );
    }
}

static void generate_silence(uint8_t* buf, enum AVSampleFormat sample_fmt, size_t size)
{
    int fill_char = 0x00;
    if (sample_fmt == AV_SAMPLE_FMT_U8)
        fill_char = 0x80;
    memset(buf, fill_char, size);
}

static void flush_encoders(OutputStream *ost_table, int nb_ostreams)
{
    int i, ret;

    for (i = 0; i < nb_ostreams; i++) {
        OutputStream   *ost = &ost_table[i];
        AVCodecContext *enc = ost->st->codec;
        AVFormatContext *os = output_files[ost->file_index].ctx;

        if (!ost->encoding_needed)
            continue;

        if (ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO && enc->frame_size <=1)
            continue;
        if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && (os->oformat->flags & AVFMT_RAWPICTURE))
            continue;

        for(;;) {
            AVPacket pkt;
            int fifo_bytes;
            av_init_packet(&pkt);
            pkt.stream_index= ost->index;

            switch (ost->st->codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                fifo_bytes = av_fifo_size(ost->fifo);
                ret = 0;
                /* encode any samples remaining in fifo */
                if (fifo_bytes > 0) {
                    int osize = av_get_bytes_per_sample(enc->sample_fmt);
                    int fs_tmp = enc->frame_size;

                    av_fifo_generic_read(ost->fifo, audio_buf, fifo_bytes, NULL);
                    if (enc->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME) {
                        enc->frame_size = fifo_bytes / (osize * enc->channels);
                    } else { /* pad */
                        int frame_bytes = enc->frame_size*osize*enc->channels;
                        if (allocated_audio_buf_size < frame_bytes)
                            exit_program(1);
                        generate_silence(audio_buf+fifo_bytes, enc->sample_fmt, frame_bytes - fifo_bytes);
                    }

                    ret = avcodec_encode_audio(enc, bit_buffer, bit_buffer_size, (short *)audio_buf);
                    pkt.duration = av_rescale((int64_t)enc->frame_size*ost->st->time_base.den,
                                              ost->st->time_base.num, enc->sample_rate);
                    enc->frame_size = fs_tmp;
                }
                if (ret <= 0) {
                    ret = avcodec_encode_audio(enc, bit_buffer, bit_buffer_size, NULL);
                }
                if (ret < 0) {
                    fprintf(stderr, "Audio encoding failed\n");
                    exit_program(1);
                }
                audio_size += ret;
                pkt.flags |= AV_PKT_FLAG_KEY;
                break;
            case AVMEDIA_TYPE_VIDEO:
                ret = avcodec_encode_video(enc, bit_buffer, bit_buffer_size, NULL);
                if (ret < 0) {
                    fprintf(stderr, "Video encoding failed\n");
                    exit_program(1);
                }
                video_size += ret;
                if(enc->coded_frame && enc->coded_frame->key_frame)
                    pkt.flags |= AV_PKT_FLAG_KEY;
                if (ost->logfile && enc->stats_out) {
                    fprintf(ost->logfile, "%s", enc->stats_out);
                }
                break;
            default:
                ret=-1;
            }

            if (ret <= 0)
                break;
            pkt.data = bit_buffer;
            pkt.size = ret;
            if (enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
                pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
            write_frame(os, &pkt, ost->st->codec, ost->bitstream_filters);
        }
    }
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int output_packet(InputStream *ist, int ist_index,
                         OutputStream *ost_table, int nb_ostreams,
                         const AVPacket *pkt)
{
    AVFormatContext *os;
    OutputStream *ost;
    int ret, i;
    int got_output;
    AVFrame picture;
    void *buffer_to_free = NULL;
    static unsigned int samples_size= 0;
    AVSubtitle subtitle, *subtitle_to_free;
    int64_t pkt_pts = AV_NOPTS_VALUE;
#if CONFIG_AVFILTER
    int frame_available;
#endif
    float quality;

    AVPacket avpkt;
    int bps = av_get_bytes_per_sample(ist->st->codec->sample_fmt);

    if(ist->next_pts == AV_NOPTS_VALUE)
        ist->next_pts= ist->pts;

    if (pkt == NULL) {
        /* EOF handling */
        av_init_packet(&avpkt);
        avpkt.data = NULL;
        avpkt.size = 0;
        goto handle_eof;
    } else {
        avpkt = *pkt;
    }

    if(pkt->dts != AV_NOPTS_VALUE)
        ist->next_pts = ist->pts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
    if(pkt->pts != AV_NOPTS_VALUE)
        pkt_pts = av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);

    //while we have more to decode or while the decoder did output something on EOF
    while (avpkt.size > 0 || (!pkt && got_output)) {
        uint8_t *data_buf, *decoded_data_buf;
        int data_size, decoded_data_size;
    handle_eof:
        ist->pts= ist->next_pts;

        if(avpkt.size && avpkt.size != pkt->size &&
           ((!ist->showed_multi_packet_warning && verbose>0) || verbose>1)){
            fprintf(stderr, "Multiple frames in a packet from stream %d\n", pkt->stream_index);
            ist->showed_multi_packet_warning=1;
        }

        /* decode the packet if needed */
        decoded_data_buf = NULL; /* fail safe */
        decoded_data_size= 0;
        data_buf  = avpkt.data;
        data_size = avpkt.size;
        subtitle_to_free = NULL;
        if (ist->decoding_needed) {
            switch(ist->st->codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:{
                if(pkt && samples_size < FFMAX(pkt->size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE)) {
                    samples_size = FFMAX(pkt->size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE);
                    av_free(samples);
                    samples= av_malloc(samples_size);
                }
                decoded_data_size= samples_size;
                    /* XXX: could avoid copy if PCM 16 bits with same
                       endianness as CPU */
                ret = avcodec_decode_audio3(ist->st->codec, samples, &decoded_data_size,
                                            &avpkt);
                if (ret < 0)
                    return ret;
                avpkt.data += ret;
                avpkt.size -= ret;
                data_size   = ret;
                got_output  = decoded_data_size > 0;
                /* Some bug in mpeg audio decoder gives */
                /* decoded_data_size < 0, it seems they are overflows */
                if (!got_output) {
                    /* no audio frame */
                    continue;
                }
                decoded_data_buf = (uint8_t *)samples;
                ist->next_pts += ((int64_t)AV_TIME_BASE/bps * decoded_data_size) /
                    (ist->st->codec->sample_rate * ist->st->codec->channels);
                break;}
            case AVMEDIA_TYPE_VIDEO:
                    decoded_data_size = (ist->st->codec->width * ist->st->codec->height * 3) / 2;
                    /* XXX: allocate picture correctly */
                    avcodec_get_frame_defaults(&picture);
                    avpkt.pts = pkt_pts;
                    avpkt.dts = ist->pts;
                    pkt_pts = AV_NOPTS_VALUE;

                    ret = avcodec_decode_video2(ist->st->codec,
                                                &picture, &got_output, &avpkt);
                    quality = same_quant ? picture.quality : 0;
                    if (ret < 0)
                        return ret;
                    if (!got_output) {
                        /* no picture yet */
                        goto discard_packet;
                    }
                    ist->next_pts = ist->pts = picture.best_effort_timestamp;
                    if (ist->st->codec->time_base.num != 0) {
                        int ticks= ist->st->parser ? ist->st->parser->repeat_pict+1 : ist->st->codec->ticks_per_frame;
                        ist->next_pts += ((int64_t)AV_TIME_BASE *
                                          ist->st->codec->time_base.num * ticks) /
                            ist->st->codec->time_base.den;
                    }
                    avpkt.size = 0;
                    buffer_to_free = NULL;
                    pre_process_video_frame(ist, (AVPicture *)&picture, &buffer_to_free);
                    break;
            case AVMEDIA_TYPE_SUBTITLE:
                ret = avcodec_decode_subtitle2(ist->st->codec,
                                               &subtitle, &got_output, &avpkt);
                if (ret < 0)
                    return ret;
                if (!got_output) {
                    goto discard_packet;
                }
                subtitle_to_free = &subtitle;
                avpkt.size = 0;
                break;
            default:
                return -1;
            }
        } else {
            switch(ist->st->codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                ist->next_pts += ((int64_t)AV_TIME_BASE * ist->st->codec->frame_size) /
                    ist->st->codec->sample_rate;
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (ist->st->codec->time_base.num != 0) {
                    int ticks= ist->st->parser ? ist->st->parser->repeat_pict+1 : ist->st->codec->ticks_per_frame;
                    ist->next_pts += ((int64_t)AV_TIME_BASE *
                                      ist->st->codec->time_base.num * ticks) /
                        ist->st->codec->time_base.den;
                }
                break;
            }
            avpkt.size = 0;
        }

        // preprocess audio (volume)
        if (ist->st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_volume != 256) {
                short *volp;
                volp = samples;
                for(i=0;i<(decoded_data_size / sizeof(short));i++) {
                    int v = ((*volp) * audio_volume + 128) >> 8;
                    *volp++ = av_clip_int16(v);
                }
            }
        }

        /* frame rate emulation */
        if (rate_emu) {
            int64_t pts = av_rescale(ist->pts, 1000000, AV_TIME_BASE);
            int64_t now = av_gettime() - ist->start;
            if (pts > now)
                usleep(pts - now);
        }
        /* if output time reached then transcode raw format,
           encode packets and output them */
        for (i = 0; i < nb_ostreams; i++) {
            OutputFile *of = &output_files[ost_table[i].file_index];
            int frame_size;

            ost = &ost_table[i];
            if (ost->source_index != ist_index)
                continue;

            if (of->start_time && ist->pts < of->start_time)
                continue;

            if (of->recording_time != INT64_MAX &&
                av_compare_ts(ist->pts, AV_TIME_BASE_Q, of->recording_time + of->start_time,
                              (AVRational){1, 1000000}) >= 0) {
                ost->is_past_recording_time = 1;
                continue;
            }

#if CONFIG_AVFILTER
            if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                ost->input_video_filter) {
                if (!picture.sample_aspect_ratio.num)
                    picture.sample_aspect_ratio = ist->st->sample_aspect_ratio;
                picture.pts = ist->pts;

                av_vsrc_buffer_add_frame(ost->input_video_filter, &picture, AV_VSRC_BUF_FLAG_OVERWRITE);
            }
            frame_available = ist->st->codec->codec_type != AVMEDIA_TYPE_VIDEO ||
                !ost->output_video_filter || avfilter_poll_frame(ost->output_video_filter->inputs[0]);
            while (frame_available) {
                if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && ost->output_video_filter) {
                    AVRational ist_pts_tb = ost->output_video_filter->inputs[0]->time_base;
                    if (av_vsink_buffer_get_video_buffer_ref(ost->output_video_filter, &ost->picref, 0) < 0)
                        goto cont;
                    if (ost->picref) {
                        avfilter_fill_frame_from_video_buffer_ref(&picture, ost->picref);
                        ist->pts = av_rescale_q(ost->picref->pts, ist_pts_tb, AV_TIME_BASE_Q);
                    }
                }
#endif
                os = output_files[ost->file_index].ctx;

                /* set the input output pts pairs */
                //ost->sync_ipts = (double)(ist->pts + input_files[ist->file_index].ts_offset - start_time)/ AV_TIME_BASE;

                if (ost->encoding_needed) {
                    av_assert0(ist->decoding_needed);
                    switch(ost->st->codec->codec_type) {
                    case AVMEDIA_TYPE_AUDIO:
                        do_audio_out(os, ost, ist, decoded_data_buf, decoded_data_size);
                        break;
                    case AVMEDIA_TYPE_VIDEO:
#if CONFIG_AVFILTER
                        if (ost->picref->video && !ost->frame_aspect_ratio)
                            ost->st->codec->sample_aspect_ratio = ost->picref->video->sample_aspect_ratio;
#endif
                        do_video_out(os, ost, ist, &picture, &frame_size,
                                        same_quant ? quality : ost->st->codec->global_quality);
                        if (vstats_filename && frame_size)
                            do_video_stats(os, ost, frame_size);
                        break;
                    case AVMEDIA_TYPE_SUBTITLE:
                        do_subtitle_out(os, ost, ist, &subtitle,
                                        pkt->pts);
                        break;
                    default:
                        abort();
                    }
                } else {
                    AVFrame avframe; //FIXME/XXX remove this
                    AVPicture pict;
                    AVPacket opkt;
                    int64_t ost_tb_start_time= av_rescale_q(of->start_time, AV_TIME_BASE_Q, ost->st->time_base);
                    av_init_packet(&opkt);

                    if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) && !copy_initial_nonkeyframes)
#if !CONFIG_AVFILTER
                        continue;
#else
                        goto cont;
#endif

                    /* no reencoding needed : output the packet directly */
                    /* force the input stream PTS */

                    avcodec_get_frame_defaults(&avframe);
                    ost->st->codec->coded_frame= &avframe;
                    avframe.key_frame = pkt->flags & AV_PKT_FLAG_KEY;

                    if(ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
                        audio_size += data_size;
                    else if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                        video_size += data_size;
                        ost->sync_opts++;
                    }

                    opkt.stream_index= ost->index;
                    if(pkt->pts != AV_NOPTS_VALUE)
                        opkt.pts= av_rescale_q(pkt->pts, ist->st->time_base, ost->st->time_base) - ost_tb_start_time;
                    else
                        opkt.pts= AV_NOPTS_VALUE;

                    if (pkt->dts == AV_NOPTS_VALUE)
                        opkt.dts = av_rescale_q(ist->pts, AV_TIME_BASE_Q, ost->st->time_base);
                    else
                        opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->st->time_base);
                    opkt.dts -= ost_tb_start_time;

                    opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->st->time_base);
                    opkt.flags= pkt->flags;

                    //FIXME remove the following 2 lines they shall be replaced by the bitstream filters
                    if(   ost->st->codec->codec_id != CODEC_ID_H264
                       && ost->st->codec->codec_id != CODEC_ID_MPEG1VIDEO
                       && ost->st->codec->codec_id != CODEC_ID_MPEG2VIDEO
                       ) {
                        if(av_parser_change(ist->st->parser, ost->st->codec, &opkt.data, &opkt.size, data_buf, data_size, pkt->flags & AV_PKT_FLAG_KEY))
                            opkt.destruct= av_destruct_packet;
                    } else {
                        opkt.data = data_buf;
                        opkt.size = data_size;
                    }

                    if (os->oformat->flags & AVFMT_RAWPICTURE) {
                        /* store AVPicture in AVPacket, as expected by the output format */
                        avpicture_fill(&pict, opkt.data, ost->st->codec->pix_fmt, ost->st->codec->width, ost->st->codec->height);
                        opkt.data = (uint8_t *)&pict;
                        opkt.size = sizeof(AVPicture);
                        opkt.flags |= AV_PKT_FLAG_KEY;
                    }
                    write_frame(os, &opkt, ost->st->codec, ost->bitstream_filters);
                    ost->st->codec->frame_number++;
                    ost->frame_number++;
                    av_free_packet(&opkt);
                }
#if CONFIG_AVFILTER
                cont:
                frame_available = (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) &&
                                   ost->output_video_filter && avfilter_poll_frame(ost->output_video_filter->inputs[0]);
                if (ost->picref)
                    avfilter_unref_buffer(ost->picref);
            }
#endif
            }

        av_free(buffer_to_free);
        /* XXX: allocate the subtitles in the codec ? */
        if (subtitle_to_free) {
            avsubtitle_free(subtitle_to_free);
            subtitle_to_free = NULL;
        }
    }
 discard_packet:

    return 0;
}

static void print_sdp(OutputFile *output_files, int n)
{
    char sdp[2048];
    int i;
    AVFormatContext **avc = av_malloc(sizeof(*avc)*n);

    if (!avc)
        exit_program(1);
    for (i = 0; i < n; i++)
        avc[i] = output_files[i].ctx;

    av_sdp_create(avc, n, sdp, sizeof(sdp));
    printf("SDP:\n%s\n", sdp);
    fflush(stdout);
    av_freep(&avc);
}

static int init_input_stream(int ist_index, OutputStream *output_streams, int nb_output_streams,
                             char *error, int error_len)
{
    int i;
    InputStream *ist = &input_streams[ist_index];
    if (ist->decoding_needed) {
        AVCodec *codec = ist->dec;
        if (!codec) {
            snprintf(error, sizeof(error), "Decoder (codec id %d) not found for input stream #%d.%d",
                    ist->st->codec->codec_id, ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        if (avcodec_open2(ist->st->codec, codec, &ist->opts) < 0) {
            snprintf(error, sizeof(error), "Error while opening decoder for input stream #%d.%d",
                    ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }
        assert_codec_experimental(ist->st->codec, 0);
        assert_avoptions(ist->opts);
    }

    ist->pts = ist->st->avg_frame_rate.num ? - ist->st->codec->has_b_frames*AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
    ist->next_pts = AV_NOPTS_VALUE;
    ist->is_start = 1;

    return 0;
}

static int transcode_init(OutputFile *output_files,
                          int nb_output_files,
                          InputFile *input_files,
                          int nb_input_files)
{
    int ret = 0, i;
    AVFormatContext *os;
    AVCodecContext *codec, *icodec;
    OutputStream *ost;
    InputStream *ist;
    char error[1024];
    int want_sdp = 1;

    if (rate_emu)
        for (i = 0; i < nb_input_streams; i++)
            input_streams[i].start = av_gettime();

    /* output stream init */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i].ctx;
        if (!os->nb_streams && !(os->oformat->flags & AVFMT_NOSTREAMS)) {
            av_dump_format(os, i, os->filename, 1);
            fprintf(stderr, "Output file #%d does not contain any stream\n", i);
            return AVERROR(EINVAL);
        }
    }

    /* for each output stream, we compute the right encoding parameters */
    for (i = 0; i < nb_output_streams; i++) {
        ost = &output_streams[i];
        os = output_files[ost->file_index].ctx;
        ist = &input_streams[ost->source_index];

        codec = ost->st->codec;
        icodec = ist->st->codec;

        ost->st->disposition = ist->st->disposition;
        codec->bits_per_raw_sample= icodec->bits_per_raw_sample;
        codec->chroma_sample_location = icodec->chroma_sample_location;

        if (ost->st->stream_copy) {
            uint64_t extra_size = (uint64_t)icodec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;

            if (extra_size > INT_MAX) {
                return AVERROR(EINVAL);
            }

            /* if stream_copy is selected, no need to decode or encode */
            codec->codec_id = icodec->codec_id;
            codec->codec_type = icodec->codec_type;

            if(!codec->codec_tag){
                if(   !os->oformat->codec_tag
                   || av_codec_get_id (os->oformat->codec_tag, icodec->codec_tag) == codec->codec_id
                   || av_codec_get_tag(os->oformat->codec_tag, icodec->codec_id) <= 0)
                    codec->codec_tag = icodec->codec_tag;
            }

            codec->bit_rate = icodec->bit_rate;
            codec->rc_max_rate    = icodec->rc_max_rate;
            codec->rc_buffer_size = icodec->rc_buffer_size;
            codec->extradata= av_mallocz(extra_size);
            if (!codec->extradata) {
                return AVERROR(ENOMEM);
            }
            memcpy(codec->extradata, icodec->extradata, icodec->extradata_size);
            codec->extradata_size= icodec->extradata_size;

            codec->time_base = ist->st->time_base;
            if(!strcmp(os->oformat->name, "avi")) {
                if(!copy_tb && av_q2d(icodec->time_base)*icodec->ticks_per_frame > 2*av_q2d(ist->st->time_base) && av_q2d(ist->st->time_base) < 1.0/500){
                    codec->time_base = icodec->time_base;
                    codec->time_base.num *= icodec->ticks_per_frame;
                    codec->time_base.den *= 2;
                }
            } else if(!(os->oformat->flags & AVFMT_VARIABLE_FPS)) {
                if(!copy_tb && av_q2d(icodec->time_base)*icodec->ticks_per_frame > av_q2d(ist->st->time_base) && av_q2d(ist->st->time_base) < 1.0/500){
                    codec->time_base = icodec->time_base;
                    codec->time_base.num *= icodec->ticks_per_frame;
                }
            }
            av_reduce(&codec->time_base.num, &codec->time_base.den,
                        codec->time_base.num, codec->time_base.den, INT_MAX);

            switch(codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if(audio_volume != 256) {
                    fprintf(stderr,"-acodec copy and -vol are incompatible (frames are not decoded)\n");
                    exit_program(1);
                }
                codec->channel_layout = icodec->channel_layout;
                codec->sample_rate = icodec->sample_rate;
                codec->channels = icodec->channels;
                codec->frame_size = icodec->frame_size;
                codec->audio_service_type = icodec->audio_service_type;
                codec->block_align= icodec->block_align;
                if(codec->block_align == 1 && codec->codec_id == CODEC_ID_MP3)
                    codec->block_align= 0;
                if(codec->codec_id == CODEC_ID_AC3)
                    codec->block_align= 0;
                break;
            case AVMEDIA_TYPE_VIDEO:
                codec->pix_fmt = icodec->pix_fmt;
                codec->width = icodec->width;
                codec->height = icodec->height;
                codec->has_b_frames = icodec->has_b_frames;
                if (!codec->sample_aspect_ratio.num) {
                    codec->sample_aspect_ratio =
                    ost->st->sample_aspect_ratio =
                        ist->st->sample_aspect_ratio.num ? ist->st->sample_aspect_ratio :
                        ist->st->codec->sample_aspect_ratio.num ?
                        ist->st->codec->sample_aspect_ratio : (AVRational){0, 1};
                }
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                codec->width = icodec->width;
                codec->height = icodec->height;
                break;
            case AVMEDIA_TYPE_DATA:
                break;
            default:
                abort();
            }
        } else {
            if (!ost->enc)
                ost->enc = avcodec_find_encoder(ost->st->codec->codec_id);
            switch(codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                ost->fifo= av_fifo_alloc(1024);
                if (!ost->fifo) {
                    return AVERROR(ENOMEM);
                }
                ost->reformat_pair = MAKE_SFMT_PAIR(AV_SAMPLE_FMT_NONE,AV_SAMPLE_FMT_NONE);
                if (!codec->sample_rate) {
                    codec->sample_rate = icodec->sample_rate;
                }
                choose_sample_rate(ost->st, ost->enc);
                codec->time_base = (AVRational){1, codec->sample_rate};
                if (codec->sample_fmt == AV_SAMPLE_FMT_NONE)
                    codec->sample_fmt = icodec->sample_fmt;
                choose_sample_fmt(ost->st, ost->enc);
                if (!codec->channels) {
                    codec->channels = icodec->channels;
                    codec->channel_layout = icodec->channel_layout;
                }
                if (av_get_channel_layout_nb_channels(codec->channel_layout) != codec->channels)
                    codec->channel_layout = 0;
                ost->audio_resample = codec->sample_rate != icodec->sample_rate || audio_sync_method > 1;
                icodec->request_channels = codec->channels;
                ist->decoding_needed = 1;
                ost->encoding_needed = 1;
                ost->resample_sample_fmt  = icodec->sample_fmt;
                ost->resample_sample_rate = icodec->sample_rate;
                ost->resample_channels    = icodec->channels;
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (codec->pix_fmt == PIX_FMT_NONE)
                    codec->pix_fmt = icodec->pix_fmt;
                choose_pixel_fmt(ost->st, ost->enc);

                if (ost->st->codec->pix_fmt == PIX_FMT_NONE) {
                    fprintf(stderr, "Video pixel format is unknown, stream cannot be encoded\n");
                    exit_program(1);
                }

                if (!codec->width || !codec->height) {
                    codec->width  = icodec->width;
                    codec->height = icodec->height;
                }

                ost->video_resample = codec->width   != icodec->width  ||
                                      codec->height  != icodec->height ||
                                      codec->pix_fmt != icodec->pix_fmt;
                if (ost->video_resample) {
                    codec->bits_per_raw_sample= frame_bits_per_raw_sample;
                }

                ost->resample_height = icodec->height;
                ost->resample_width  = icodec->width;
                ost->resample_pix_fmt= icodec->pix_fmt;
                ost->encoding_needed = 1;
                ist->decoding_needed = 1;

                if (!ost->frame_rate.num)
                    ost->frame_rate = ist->st->r_frame_rate.num ? ist->st->r_frame_rate : (AVRational){25,1};
                if (ost->enc && ost->enc->supported_framerates && !force_fps) {
                    int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
                    ost->frame_rate = ost->enc->supported_framerates[idx];
                }
                codec->time_base = (AVRational){ost->frame_rate.den, ost->frame_rate.num};
                if(   av_q2d(codec->time_base) < 0.001 && video_sync_method
                   && (video_sync_method==1 || (video_sync_method<0 && !(os->oformat->flags & AVFMT_VARIABLE_FPS)))){
                    av_log(os, AV_LOG_WARNING, "Frame rate very high for a muxer not effciciently supporting it.\n"
                                               "Please consider specifiying a lower framerate, a different muxer or -vsync 2\n");
                }

#if CONFIG_AVFILTER
                if (configure_video_filters(ist, ost)) {
                    fprintf(stderr, "Error opening filters!\n");
                    exit(1);
                }
#endif
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                ost->encoding_needed = 1;
                ist->decoding_needed = 1;
                break;
            default:
                abort();
                break;
            }
            /* two pass mode */
            if (ost->encoding_needed && codec->codec_id != CODEC_ID_H264 &&
                (codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2))) {
                char logfilename[1024];
                FILE *f;

                snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                         pass_logfilename_prefix ? pass_logfilename_prefix : DEFAULT_PASS_LOGFILENAME_PREFIX,
                         i);
                if (codec->flags & CODEC_FLAG_PASS1) {
                    f = fopen(logfilename, "wb");
                    if (!f) {
                        fprintf(stderr, "Cannot write log file '%s' for pass-1 encoding: %s\n", logfilename, strerror(errno));
                        exit_program(1);
                    }
                    ost->logfile = f;
                } else {
                    char  *logbuffer;
                    size_t logbuffer_size;
                    if (read_file(logfilename, &logbuffer, &logbuffer_size) < 0) {
                        fprintf(stderr, "Error reading log file '%s' for pass-2 encoding\n", logfilename);
                        exit_program(1);
                    }
                    codec->stats_in = logbuffer;
                }
            }
        }
        if(codec->codec_type == AVMEDIA_TYPE_VIDEO){
            /* maximum video buffer size is 6-bytes per pixel, plus DPX header size */
            int size= codec->width * codec->height;
            bit_buffer_size= FFMAX(bit_buffer_size, 6*size + 1664);
        }
    }

    if (!bit_buffer)
        bit_buffer = av_malloc(bit_buffer_size);
    if (!bit_buffer) {
        fprintf(stderr, "Cannot allocate %d bytes output buffer\n",
                bit_buffer_size);
        return AVERROR(ENOMEM);
    }

    /* open each encoder */
    for (i = 0; i < nb_output_streams; i++) {
        ost = &output_streams[i];
        if (ost->encoding_needed) {
            AVCodec *codec = ost->enc;
            AVCodecContext *dec = input_streams[ost->source_index].st->codec;
            if (!codec) {
                snprintf(error, sizeof(error), "Encoder (codec id %d) not found for output stream #%d.%d",
                         ost->st->codec->codec_id, ost->file_index, ost->index);
                ret = AVERROR(EINVAL);
                goto dump_format;
            }
            if (dec->subtitle_header) {
                ost->st->codec->subtitle_header = av_malloc(dec->subtitle_header_size);
                if (!ost->st->codec->subtitle_header) {
                    ret = AVERROR(ENOMEM);
                    goto dump_format;
                }
                memcpy(ost->st->codec->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
                ost->st->codec->subtitle_header_size = dec->subtitle_header_size;
            }
            if (avcodec_open2(ost->st->codec, codec, &ost->opts) < 0) {
                snprintf(error, sizeof(error), "Error while opening encoder for output stream #%d.%d - maybe incorrect parameters such as bit_rate, rate, width or height",
                        ost->file_index, ost->index);
                ret = AVERROR(EINVAL);
                goto dump_format;
            }
            assert_codec_experimental(ost->st->codec, 1);
            assert_avoptions(ost->opts);
            if (ost->st->codec->bit_rate && ost->st->codec->bit_rate < 1000)
                av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                             "It takes bits/s as argument, not kbits/s\n");
            extra_size += ost->st->codec->extradata_size;
        }
    }

    /* init input streams */
    for (i = 0; i < nb_input_streams; i++)
        if ((ret = init_input_stream(i, output_streams, nb_output_streams, error, sizeof(error)) < 0))
            goto dump_format;

    /* open files and write file headers */
    for (i = 0; i < nb_output_files; i++) {
        os = output_files[i].ctx;
        if (avformat_write_header(os, &output_files[i].opts) < 0) {
            snprintf(error, sizeof(error), "Could not write header for output file #%d (incorrect codec parameters ?)", i);
            ret = AVERROR(EINVAL);
            goto dump_format;
        }
//        assert_avoptions(output_files[i].opts);
        if (strcmp(os->oformat->name, "rtp")) {
            want_sdp = 0;
        }
    }

 dump_format:
    /* dump the file output parameters - cannot be done before in case
       of stream copy */
    for(i=0;i<nb_output_files;i++) {
        av_dump_format(output_files[i].ctx, i, output_files[i].ctx->filename, 1);
    }

    /* dump the stream mapping */
    if (verbose >= 0) {
        fprintf(stderr, "Stream mapping:\n");
        for (i = 0; i < nb_output_streams;i ++) {
            ost = &output_streams[i];
            fprintf(stderr, "  Stream #%d.%d -> #%d.%d",
                    input_streams[ost->source_index].file_index,
                    input_streams[ost->source_index].st->index,
                    ost->file_index,
                    ost->index);
            if (ost->sync_ist != &input_streams[ost->source_index])
                fprintf(stderr, " [sync #%d.%d]",
                        ost->sync_ist->file_index,
                        ost->sync_ist->st->index);
            if (ost->st->stream_copy)
                fprintf(stderr, " (copy)");
            else
                fprintf(stderr, " (%s -> %s)", input_streams[ost->source_index].dec ?
                        input_streams[ost->source_index].dec->name : "?",
                        ost->enc ? ost->enc->name : "?");
            fprintf(stderr, "\n");
        }
    }

    if (ret) {
        fprintf(stderr, "%s\n", error);
        return ret;
    }

    if (want_sdp) {
        print_sdp(output_files, nb_output_files);
    }

    return 0;
}

/*
 * The following code is the main loop of the file converter
 */
static int transcode(OutputFile *output_files,
                     int nb_output_files,
                     InputFile *input_files,
                     int nb_input_files)
{
    int ret, i;
    AVFormatContext *is, *os;
    OutputStream *ost;
    InputStream *ist;
    uint8_t *no_packet;
    int no_packet_count=0;
    int64_t timer_start;
    int key;

    if (!(no_packet = av_mallocz(nb_input_files)))
        exit_program(1);

    ret = transcode_init(output_files, nb_output_files, input_files, nb_input_files);
    if (ret < 0)
        goto fail;

    if (!using_stdin) {
        if(verbose >= 0)
            fprintf(stderr, "Press [q] to stop, [?] for help\n");
        avio_set_interrupt_cb(decode_interrupt_cb);
    }
    term_init();

    timer_start = av_gettime();

    for(; received_sigterm == 0;) {
        int file_index, ist_index;
        AVPacket pkt;
        int64_t ipts_min;
        double opts_min;

        ipts_min = INT64_MAX;
        opts_min= 1e100;
        /* if 'q' pressed, exits */
        if (!using_stdin) {
            if (q_pressed)
                break;
            /* read_key() returns 0 on EOF */
            key = read_key();
            if (key == 'q')
                break;
            if (key == '+') verbose++;
            if (key == '-') verbose--;
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
            if (key == 'd' || key == 'D'){
                int debug=0;
                if(key == 'D') {
                    debug = input_streams[0].st->codec->debug<<1;
                    if(!debug) debug = 1;
                    while(debug & (FF_DEBUG_DCT_COEFF|FF_DEBUG_VIS_QP|FF_DEBUG_VIS_MB_TYPE)) //unsupported, would just crash
                        debug += debug;
                }else
                    scanf("%d", &debug);
                for(i=0;i<nb_input_streams;i++) {
                    input_streams[i].st->codec->debug = debug;
                }
                for(i=0;i<nb_output_streams;i++) {
                    ost = &output_streams[i];
                    ost->st->codec->debug = debug;
                }
                if(debug) av_log_set_level(AV_LOG_DEBUG);
                fprintf(stderr,"debug=%d\n", debug);
            }
            if (key == '?'){
                fprintf(stderr, "key    function\n"
                                "?      show this help\n"
                                "+      increase verbosity\n"
                                "-      decrease verbosity\n"
                                "D      cycle through available debug modes\n"
                                "h      dump packets/hex press to cycle through the 3 states\n"
                                "q      quit\n"
                                "s      Show QP histogram\n"
                );
            }
        }

        /* select the stream that we must read now by looking at the
           smallest output pts */
        file_index = -1;
        for (i = 0; i < nb_output_streams; i++) {
            OutputFile *of;
            int64_t ipts;
            double  opts;
            ost = &output_streams[i];
            of = &output_files[ost->file_index];
            os = output_files[ost->file_index].ctx;
            ist = &input_streams[ost->source_index];
            if (ost->is_past_recording_time || no_packet[ist->file_index] ||
                (os->pb && avio_tell(os->pb) >= of->limit_filesize))
                continue;
            opts = ost->st->pts.val * av_q2d(ost->st->time_base);
            ipts = ist->pts;
            if (!input_files[ist->file_index].eof_reached){
                if(ipts < ipts_min) {
                    ipts_min = ipts;
                    if(input_sync ) file_index = ist->file_index;
                }
                if(opts < opts_min) {
                    opts_min = opts;
                    if(!input_sync) file_index = ist->file_index;
                }
            }
            if(ost->frame_number >= max_frames[ost->st->codec->codec_type]){
                file_index= -1;
                break;
            }
        }
        /* if none, if is finished */
        if (file_index < 0) {
            if(no_packet_count){
                no_packet_count=0;
                memset(no_packet, 0, nb_input_files);
                usleep(10000);
                continue;
            }
            break;
        }

        /* read a frame from it and output it in the fifo */
        is = input_files[file_index].ctx;
        ret= av_read_frame(is, &pkt);
        if(ret == AVERROR(EAGAIN)){
            no_packet[file_index]=1;
            no_packet_count++;
            continue;
        }
        if (ret < 0) {
            input_files[file_index].eof_reached = 1;
            if (opt_shortest)
                break;
            else
                continue;
        }

        no_packet_count=0;
        memset(no_packet, 0, nb_input_files);

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_DEBUG, &pkt, do_hex_dump,
                             is->streams[pkt.stream_index]);
        }
        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt.stream_index >= input_files[file_index].nb_streams)
            goto discard_packet;
        ist_index = input_files[file_index].ist_index + pkt.stream_index;
        ist = &input_streams[ist_index];
        if (ist->discard)
            goto discard_packet;

        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts += av_rescale_q(input_files[ist->file_index].ts_offset, AV_TIME_BASE_Q, ist->st->time_base);
        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts += av_rescale_q(input_files[ist->file_index].ts_offset, AV_TIME_BASE_Q, ist->st->time_base);

        if (ist->ts_scale) {
            if(pkt.pts != AV_NOPTS_VALUE)
                pkt.pts *= ist->ts_scale;
            if(pkt.dts != AV_NOPTS_VALUE)
                pkt.dts *= ist->ts_scale;
        }

//        fprintf(stderr, "next:%"PRId64" dts:%"PRId64" off:%"PRId64" %d\n", ist->next_pts, pkt.dts, input_files[ist->file_index].ts_offset, ist->st->codec->codec_type);
        if (pkt.dts != AV_NOPTS_VALUE && ist->next_pts != AV_NOPTS_VALUE
            && (is->iformat->flags & AVFMT_TS_DISCONT)) {
            int64_t pkt_dts= av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);
            int64_t delta= pkt_dts - ist->next_pts;
            if((FFABS(delta) > 1LL*dts_delta_threshold*AV_TIME_BASE || pkt_dts+1<ist->pts)&& !copy_ts){
                input_files[ist->file_index].ts_offset -= delta;
                if (verbose > 2)
                    fprintf(stderr, "timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                            delta, input_files[ist->file_index].ts_offset);
                pkt.dts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if(pkt.pts != AV_NOPTS_VALUE)
                    pkt.pts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
        }

        //fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->st->index, pkt.size);
        if (output_packet(ist, ist_index, output_streams, nb_output_streams, &pkt) < 0) {

            if (verbose >= 0)
                fprintf(stderr, "Error while decoding stream #%d.%d\n",
                        ist->file_index, ist->st->index);
            if (exit_on_error)
                exit_program(1);
            av_free_packet(&pkt);
            continue;
        }

    discard_packet:
        av_free_packet(&pkt);

        /* dump report by using the output first video and audio streams */
        print_report(output_files, output_streams, nb_output_streams, 0, timer_start);
    }

    /* at the end of stream, we must flush the decoder buffers */
    for (i = 0; i < nb_input_streams; i++) {
        ist = &input_streams[i];
        if (ist->decoding_needed) {
            output_packet(ist, i, output_streams, nb_output_streams, NULL);
        }
    }
    flush_encoders(output_streams, nb_output_streams);

    term_exit();

    /* write the trailer if needed and close file */
    for(i=0;i<nb_output_files;i++) {
        os = output_files[i].ctx;
        av_write_trailer(os);
    }

    /* dump report by using the first video and audio streams */
    print_report(output_files, output_streams, nb_output_streams, 1, timer_start);

    /* close each encoder */
    for (i = 0; i < nb_output_streams; i++) {
        ost = &output_streams[i];
        if (ost->encoding_needed) {
            av_freep(&ost->st->codec->stats_in);
            avcodec_close(ost->st->codec);
        }
#if CONFIG_AVFILTER
        avfilter_graph_free(&ost->graph);
#endif
    }

    /* close each decoder */
    for (i = 0; i < nb_input_streams; i++) {
        ist = &input_streams[i];
        if (ist->decoding_needed) {
            avcodec_close(ist->st->codec);
        }
    }

    /* finished ! */
    ret = 0;

 fail:
    av_freep(&bit_buffer);
    av_freep(&no_packet);

    if (output_streams) {
        for (i = 0; i < nb_output_streams; i++) {
            ost = &output_streams[i];
            if (ost) {
                if (ost->st->stream_copy)
                    av_freep(&ost->st->codec->extradata);
                if (ost->logfile) {
                    fclose(ost->logfile);
                    ost->logfile = NULL;
                }
                av_fifo_free(ost->fifo); /* works even if fifo is not
                                             initialized but set to zero */
                av_freep(&ost->st->codec->subtitle_header);
                av_free(ost->resample_frame.data[0]);
                av_free(ost->forced_kf_pts);
                if (ost->video_resample)
                    sws_freeContext(ost->img_resample_ctx);
                if (ost->resample)
                    audio_resample_close(ost->resample);
                if (ost->reformat_ctx)
                    av_audio_convert_free(ost->reformat_ctx);
                av_dict_free(&ost->opts);
            }
        }
    }
    return ret;
}

static int opt_format(const char *opt, const char *arg)
{
    last_asked_format = arg;
    return 0;
}

static int opt_video_rc_override_string(const char *opt, const char *arg)
{
    video_rc_override_string = arg;
    return 0;
}

static int opt_me_threshold(const char *opt, const char *arg)
{
    me_threshold = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
    return 0;
}

static int opt_verbose(const char *opt, const char *arg)
{
    verbose = parse_number_or_die(opt, arg, OPT_INT64, -10, 10);
    return 0;
}

static int opt_frame_rate(const char *opt, const char *arg)
{
    if (av_parse_video_rate(&frame_rate, arg) < 0) {
        fprintf(stderr, "Incorrect value for %s: %s\n", opt, arg);
        exit_program(1);
    }
    return 0;
}

static int opt_frame_crop(const char *opt, const char *arg)
{
    fprintf(stderr, "Option '%s' has been removed, use the crop filter instead\n", opt);
    return AVERROR(EINVAL);
}

static int opt_frame_size(const char *opt, const char *arg)
{
    if (av_parse_video_size(&frame_width, &frame_height, arg) < 0) {
        fprintf(stderr, "Incorrect frame size\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_pad(const char *opt, const char *arg) {
    fprintf(stderr, "Option '%s' has been removed, use the pad filter instead\n", opt);
    return -1;
}

static int opt_frame_pix_fmt(const char *opt, const char *arg)
{
    if (strcmp(arg, "list")) {
        frame_pix_fmt = av_get_pix_fmt(arg);
        if (frame_pix_fmt == PIX_FMT_NONE) {
            fprintf(stderr, "Unknown pixel format requested: %s\n", arg);
            return AVERROR(EINVAL);
        }
    } else {
        opt_pix_fmts(NULL, NULL);
        exit_program(0);
    }
    return 0;
}

static int opt_frame_aspect_ratio(const char *opt, const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;
    char *end;

    p = strchr(arg, ':');
    if (p) {
        x = strtol(arg, &end, 10);
        if (end == p)
            y = strtol(end+1, &end, 10);
        if (x > 0 && y > 0)
            ar = (double)x / (double)y;
    } else
        ar = strtod(arg, NULL);

    if (!ar) {
        fprintf(stderr, "Incorrect aspect ratio specification.\n");
        return AVERROR(EINVAL);
    }
    frame_aspect_ratio = ar;
    return 0;
}

static int opt_metadata(const char *opt, const char *arg)
{
    char *mid= strchr(arg, '=');

    if(!mid){
        fprintf(stderr, "Missing =\n");
        exit_program(1);
    }
    *mid++= 0;

    av_dict_set(&metadata, arg, mid, 0);

    return 0;
}

static int opt_qscale(const char *opt, const char *arg)
{
    video_qscale = parse_number_or_die(opt, arg, OPT_FLOAT, 0, 255);
    if (video_qscale <= 0 || video_qscale > 255) {
        fprintf(stderr, "qscale must be > 0.0 and <= 255\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_top_field_first(const char *opt, const char *arg)
{
    top_field_first = parse_number_or_die(opt, arg, OPT_INT, 0, 1);
    return opt_default(opt, arg);
}

static int opt_thread_count(const char *opt, const char *arg)
{
    thread_count= parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
#if !HAVE_THREADS
    if (verbose >= 0)
        fprintf(stderr, "Warning: not compiled with thread support, using thread emulation\n");
#endif
    return 0;
}

static int opt_audio_sample_fmt(const char *opt, const char *arg)
{
    if (strcmp(arg, "list")) {
        audio_sample_fmt = av_get_sample_fmt(arg);
        if (audio_sample_fmt == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_ERROR, "Invalid sample format '%s'\n", arg);
            return AVERROR(EINVAL);
        }
    } else {
        int i;
        char fmt_str[128];
        for (i = -1; i < AV_SAMPLE_FMT_NB; i++)
            printf("%s\n", av_get_sample_fmt_string(fmt_str, sizeof(fmt_str), i));
        exit_program(0);
    }
    return 0;
}

static int opt_audio_rate(const char *opt, const char *arg)
{
    audio_sample_rate = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    return 0;
}

static int opt_audio_channels(const char *opt, const char *arg)
{
    audio_channels = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    return 0;
}

static int opt_codec(const char *opt, const char *arg)
{
    return av_dict_set(&codec_names, opt, arg, 0);
}

static int opt_audio_codec(const char *opt, const char *arg)
{
    return opt_codec("codec:a", arg);
}

static int opt_video_codec(const char *opt, const char *arg)
{
    return opt_codec("codec:v", arg);
}

static int opt_subtitle_codec(const char *opt, const char *arg)
{
    return opt_codec("codec:s", arg);
}

static int opt_data_codec(const char *opt, const char *arg)
{
    return opt_codec("codec:d", arg);
}

static int opt_codec_tag(const char *opt, const char *arg)
{
    char *tail;
    uint32_t *codec_tag;

    codec_tag = !strcmp(opt, "atag") ? &audio_codec_tag :
                !strcmp(opt, "vtag") ? &video_codec_tag :
                !strcmp(opt, "stag") ? &subtitle_codec_tag : NULL;
    if (!codec_tag)
        return -1;

    *codec_tag = strtol(arg, &tail, 0);
    if (!tail || *tail)
        *codec_tag = AV_RL32(arg);

    return 0;
}

static int opt_map(const char *opt, const char *arg)
{
    StreamMap *m = NULL;
    int i, negative = 0, file_idx;
    int sync_file_idx = -1, sync_stream_idx;
    char *p, *sync;
    char *map;

    if (*arg == '-') {
        negative = 1;
        arg++;
    }
    map = av_strdup(arg);

    /* parse sync stream first, just pick first matching stream */
    if (sync = strchr(map, ',')) {
        *sync = 0;
        sync_file_idx = strtol(sync + 1, &sync, 0);
        if (sync_file_idx >= nb_input_files || sync_file_idx < 0) {
            av_log(NULL, AV_LOG_ERROR, "Invalid sync file index: %d.\n", sync_file_idx);
            exit_program(1);
        }
        if (*sync)
            sync++;
        for (i = 0; i < input_files[sync_file_idx].ctx->nb_streams; i++)
            if (check_stream_specifier(input_files[sync_file_idx].ctx,
                                       input_files[sync_file_idx].ctx->streams[i], sync) == 1) {
                sync_stream_idx = i;
                break;
            }
        if (i == input_files[sync_file_idx].ctx->nb_streams) {
            av_log(NULL, AV_LOG_ERROR, "Sync stream specification in map %s does not "
                                       "match any streams.\n", arg);
            exit_program(1);
        }
    }


    file_idx = strtol(map, &p, 0);
    if (file_idx >= nb_input_files || file_idx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid input file index: %d.\n", file_idx);
        exit_program(1);
    }
    if (negative)
        /* disable some already defined maps */
        for (i = 0; i < nb_stream_maps; i++) {
            m = &stream_maps[i];
            if (check_stream_specifier(input_files[m->file_index].ctx,
                                       input_files[m->file_index].ctx->streams[m->stream_index],
                                       *p == ':' ? p + 1 : p) > 0)
                m->disabled = 1;
        }
    else
        for (i = 0; i < input_files[file_idx].ctx->nb_streams; i++) {
            if (check_stream_specifier(input_files[file_idx].ctx, input_files[file_idx].ctx->streams[i],
                        *p == ':' ? p + 1 : p) <= 0)
                continue;
            stream_maps = grow_array(stream_maps, sizeof(*stream_maps), &nb_stream_maps, nb_stream_maps + 1);
            m = &stream_maps[nb_stream_maps - 1];

            m->file_index   = file_idx;
            m->stream_index = i;

            if (sync_file_idx >= 0) {
                m->sync_file_index   = sync_file_idx;
                m->sync_stream_index = sync_stream_idx;
            } else {
                m->sync_file_index   = file_idx;
                m->sync_stream_index = i;
            }
        }

    if (!m) {
        av_log(NULL, AV_LOG_ERROR, "Stream map '%s' matches no streams.\n", arg);
        exit_program(1);
    }

    av_freep(&map);
    return 0;
}

static void parse_meta_type(char *arg, char *type, int *index)
{
    if (*arg == ':') {
        *type = *(++arg);
        switch (*arg) {
        case 'g':
            break;
        case 's':
        case 'c':
        case 'p':
            if (*(++arg) == ':')
                *index = strtol(++arg, NULL, 0);
            break;
        default:
            fprintf(stderr, "Invalid metadata type %c.\n", *arg);
            exit_program(1);
        }
    } else
        *type = 'g';
}

static int opt_map_metadata(const char *opt, const char *arg)
{
    MetadataMap *m, *m1;
    char *p;

    meta_data_maps = grow_array(meta_data_maps, sizeof(*meta_data_maps),
                                &nb_meta_data_maps, nb_meta_data_maps + 1);

    m = &meta_data_maps[nb_meta_data_maps - 1][1];
    m->file = strtol(arg, &p, 0);
    parse_meta_type(p, &m->type, &m->index);

    m1 = &meta_data_maps[nb_meta_data_maps - 1][0];
    if (p = strchr(opt, ':'))
        parse_meta_type(p, &m1->type, &m1->index);
    else
        m1->type = 'g';

    if (m->type == 'g' || m1->type == 'g')
        metadata_global_autocopy = 0;
    if (m->type == 's' || m1->type == 's')
        metadata_streams_autocopy = 0;
    if (m->type == 'c' || m1->type == 'c')
        metadata_chapters_autocopy = 0;

    return 0;
}

static int opt_input_ts_scale(const char *opt, const char *arg)
{
    return av_dict_set(&ts_scale, opt, arg, 0);
}

static int opt_recording_time(const char *opt, const char *arg)
{
    recording_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_start_time(const char *opt, const char *arg)
{
    start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_input_ts_offset(const char *opt, const char *arg)
{
    input_ts_offset = parse_time_or_die(opt, arg, 1);
    return 0;
}

static enum CodecID find_codec_or_die(const char *name, enum AVMediaType type, int encoder)
{
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;

    if(!name)
        return CODEC_ID_NONE;
    codec = encoder ?
        avcodec_find_encoder_by_name(name) :
        avcodec_find_decoder_by_name(name);
    if(!codec) {
        av_log(NULL, AV_LOG_ERROR, "Unknown %s '%s'\n", codec_string, name);
        exit_program(1);
    }
    if(codec->type != type) {
        av_log(NULL, AV_LOG_ERROR, "Invalid %s type '%s'\n", codec_string, name);
        exit_program(1);
    }
    return codec->id;
}

static AVCodec *choose_codec(AVFormatContext *s, AVStream *st, enum AVMediaType type, AVDictionary *codec_names)
{
    AVDictionaryEntry *e = NULL;
    char *codec_name = NULL;
    int ret;

    while (e = av_dict_get(codec_names, "", e, AV_DICT_IGNORE_SUFFIX)) {
        char *p = strchr(e->key, ':');

        if ((ret = check_stream_specifier(s, st, p ? p + 1 : "")) > 0)
            codec_name = e->value;
        else if (ret < 0)
            exit_program(1);
    }

    if (!codec_name) {
        if (s->oformat) {
            st->codec->codec_id = av_guess_codec(s->oformat, NULL, s->filename, NULL, type);
            return avcodec_find_encoder(st->codec->codec_id);
        }
    } else if (!strcmp(codec_name, "copy"))
        st->stream_copy = 1;
    else {
        st->codec->codec_id = find_codec_or_die(codec_name, type, s->iformat == NULL);
        return s->oformat ? avcodec_find_encoder_by_name(codec_name) :
                            avcodec_find_decoder_by_name(codec_name);
    }

    return NULL;
}

/**
 * Add all the streams from the given input file to the global
 * list of input streams.
 */
static void add_input_streams(AVFormatContext *ic)
{
    int i, rfps, rfps_base, ret;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecContext *dec = st->codec;
        AVDictionaryEntry *e = NULL;
        InputStream *ist;
        char *scale = NULL;

        dec->thread_count = thread_count;

        input_streams = grow_array(input_streams, sizeof(*input_streams), &nb_input_streams, nb_input_streams + 1);
        ist = &input_streams[nb_input_streams - 1];
        ist->st = st;
        ist->file_index = nb_input_files;
        ist->discard = 1;
        ist->opts = filter_codec_opts(codec_opts, ist->st->codec->codec_id, ic, st);

        while (e = av_dict_get(ts_scale, "", e, AV_DICT_IGNORE_SUFFIX)) {
            char *p = strchr(e->key, ':');

            if ((ret = check_stream_specifier(ic, st, p ? p + 1 : "")) > 0)
                scale = e->value;
            else if (ret < 0)
                exit_program(1);
        }
        if (scale)
            ist->ts_scale = strtod(scale, NULL);

        ist->dec = choose_codec(ic, st, dec->codec_type, codec_names);
        if (!ist->dec)
            ist->dec = avcodec_find_decoder(dec->codec_id);

        switch (dec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(dec->codec_id);
            if(audio_disable)
                st->discard= AVDISCARD_ALL;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(dec->codec_id);
            rfps      = ic->streams[i]->r_frame_rate.num;
            rfps_base = ic->streams[i]->r_frame_rate.den;
            if (dec->lowres) {
                dec->flags |= CODEC_FLAG_EMU_EDGE;
            }
            if(me_threshold)
                dec->debug |= FF_DEBUG_MV;

            if (dec->time_base.den != rfps*dec->ticks_per_frame || dec->time_base.num != rfps_base) {

                if (verbose >= 0)
                    fprintf(stderr,"\nSeems stream %d codec frame rate differs from container frame rate: %2.2f (%d/%d) -> %2.2f (%d/%d)\n",
                            i, (float)dec->time_base.den / dec->time_base.num, dec->time_base.den, dec->time_base.num,

                    (float)rfps / rfps_base, rfps, rfps_base);
            }

            if(video_disable)
                st->discard= AVDISCARD_ALL;
            else if(video_discard)
                st->discard= video_discard;
            break;
        case AVMEDIA_TYPE_DATA:
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(dec->codec_id);
            if(subtitle_disable)
                st->discard = AVDISCARD_ALL;
            break;
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        default:
            abort();
        }
    }
}

static int opt_input_file(const char *opt, const char *filename)
{
    AVFormatContext *ic;
    AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    uint8_t buf[128];
    AVDictionary **opts;
    int orig_nb_streams;                     // number of streams before avformat_find_stream_info

    if (last_asked_format) {
        if (!(file_iformat = av_find_input_format(last_asked_format))) {
            fprintf(stderr, "Unknown input format: '%s'\n", last_asked_format);
            exit_program(1);
        }
        last_asked_format = NULL;
    }

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    using_stdin |= !strncmp(filename, "pipe:", 5) ||
                    !strcmp(filename, "/dev/stdin");

    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic) {
        print_error(filename, AVERROR(ENOMEM));
        exit_program(1);
    }
    if (audio_sample_rate) {
        snprintf(buf, sizeof(buf), "%d", audio_sample_rate);
        av_dict_set(&format_opts, "sample_rate", buf, 0);
    }
    if (audio_channels) {
        snprintf(buf, sizeof(buf), "%d", audio_channels);
        av_dict_set(&format_opts, "channels", buf, 0);
    }
    if (frame_rate.num) {
        snprintf(buf, sizeof(buf), "%d/%d", frame_rate.num, frame_rate.den);
        av_dict_set(&format_opts, "framerate", buf, 0);
    }
    if (frame_width && frame_height) {
        snprintf(buf, sizeof(buf), "%dx%d", frame_width, frame_height);
        av_dict_set(&format_opts, "video_size", buf, 0);
    }
    if (frame_pix_fmt != PIX_FMT_NONE)
        av_dict_set(&format_opts, "pixel_format", av_get_pix_fmt_name(frame_pix_fmt), 0);

    ic->flags |= AVFMT_FLAG_NONBLOCK;

    /* open the input file with generic libav function */
    err = avformat_open_input(&ic, filename, file_iformat, &format_opts);
    if (err < 0) {
        print_error(filename, err);
        exit_program(1);
    }
    assert_avoptions(format_opts);

    if(opt_programid) {
        int i, j;
        int found=0;
        for(i=0; i<ic->nb_streams; i++){
            ic->streams[i]->discard= AVDISCARD_ALL;
        }
        for(i=0; i<ic->nb_programs; i++){
            AVProgram *p= ic->programs[i];
            if(p->id != opt_programid){
                p->discard = AVDISCARD_ALL;
            }else{
                found=1;
                for(j=0; j<p->nb_stream_indexes; j++){
                    ic->streams[p->stream_index[j]]->discard= AVDISCARD_DEFAULT;
                }
            }
        }
        if(!found){
            fprintf(stderr, "Specified program id not found\n");
            exit_program(1);
        }
        opt_programid=0;
    }

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        choose_codec(ic, ic->streams[i], ic->streams[i]->codec->codec_type, codec_names);

    /* Set AVCodecContext options for avformat_find_stream_info */
    opts = setup_find_stream_info_opts(ic, codec_opts);
    orig_nb_streams = ic->nb_streams;

    /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
    ret = avformat_find_stream_info(ic, opts);
    if (ret < 0 && verbose >= 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", filename);
        av_close_input_file(ic);
        exit_program(1);
    }

    timestamp = start_time;
    /* add the stream start time */
    if (ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (start_time != 0) {
        ret = av_seek_frame(ic, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            fprintf(stderr, "%s: could not seek to position %0.3f\n",
                    filename, (double)timestamp / AV_TIME_BASE);
        }
        /* reset seek info */
        start_time = 0;
    }

    /* update the current parameters so that they match the one of the input stream */
    add_input_streams(ic);

    /* dump the file content */
    if (verbose >= 0)
        av_dump_format(ic, nb_input_files, filename, 0);

    input_files = grow_array(input_files, sizeof(*input_files), &nb_input_files, nb_input_files + 1);
    input_files[nb_input_files - 1].ctx        = ic;
    input_files[nb_input_files - 1].ist_index  = nb_input_streams - ic->nb_streams;
    input_files[nb_input_files - 1].ts_offset  = input_ts_offset - (copy_ts ? 0 : timestamp);
    input_files[nb_input_files - 1].nb_streams = ic->nb_streams;

    top_field_first = -1;
    frame_rate    = (AVRational){0, 0};
    frame_pix_fmt = PIX_FMT_NONE;
    frame_height = 0;
    frame_width  = 0;
    audio_sample_rate = 0;
    audio_channels    = 0;
    audio_sample_fmt  = AV_SAMPLE_FMT_NONE;
    av_dict_free(&ts_scale);
    input_ts_offset = 0;

    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);
    av_dict_free(&codec_names);
    uninit_opts();
    init_opts();
    return 0;
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
    ost->forced_kf_pts = av_malloc(sizeof(*ost->forced_kf_pts) * n);
    if (!ost->forced_kf_pts) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.\n");
        exit_program(1);
    }
    for (i = 0; i < n; i++) {
        p = i ? strchr(p, ',') + 1 : kf;
        t = parse_time_or_die("force_key_frames", p, 1);
        ost->forced_kf_pts[i] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);
    }
}

static OutputStream *new_output_stream(AVFormatContext *oc, enum AVMediaType type)
{
    OutputStream *ost;
    AVStream *st = av_new_stream(oc, oc->nb_streams < nb_streamid_map ? streamid_map[oc->nb_streams] : 0);
    int idx      = oc->nb_streams - 1;

    if (!st) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc stream.\n");
        exit_program(1);
    }

    output_streams = grow_array(output_streams, sizeof(*output_streams), &nb_output_streams,
                                nb_output_streams + 1);
    ost = &output_streams[nb_output_streams - 1];
    ost->file_index = nb_output_files;
    ost->index = idx;
    ost->st    = st;
    st->codec->codec_type = type;
    ost->enc = choose_codec(oc, st, type, codec_names);
    if (ost->enc) {
        ost->opts  = filter_codec_opts(codec_opts, ost->enc->id, oc, st);
    }

    avcodec_get_context_defaults3(st->codec, ost->enc);
    st->codec->codec_type = type; // XXX hack, avcodec_get_context_defaults2() sets type to unknown for stream copy

    ost->sws_flags = av_get_int(sws_opts, "sws_flags", NULL);
    return ost;
}

static OutputStream *new_video_stream(AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *video_enc;

    ost = new_output_stream(oc, AVMEDIA_TYPE_VIDEO);
    st  = ost->st;
    if (!st->stream_copy) {
        ost->frame_aspect_ratio = frame_aspect_ratio;
        frame_aspect_ratio = 0;
#if CONFIG_AVFILTER
        ost->avfilter = vfilters;
        vfilters = NULL;
#endif
    }

    ost->bitstream_filters = video_bitstream_filters;
    video_bitstream_filters= NULL;

    st->codec->thread_count= thread_count;

    video_enc = st->codec;

    if(video_codec_tag)
        video_enc->codec_tag= video_codec_tag;

    if(oc->oformat->flags & AVFMT_GLOBALHEADER) {
        video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    if (st->stream_copy) {
        video_enc->sample_aspect_ratio =
        st->sample_aspect_ratio = av_d2q(frame_aspect_ratio*frame_height/frame_width, 255);
    } else {
        const char *p;
        int i;

        if (frame_rate.num)
            ost->frame_rate = frame_rate;

        video_enc->width = frame_width;
        video_enc->height = frame_height;
        video_enc->pix_fmt = frame_pix_fmt;
        video_enc->bits_per_raw_sample = frame_bits_per_raw_sample;
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

        if (video_qscale || same_quant) {
            video_enc->flags |= CODEC_FLAG_QSCALE;
            video_enc->global_quality = FF_QP2LAMBDA * video_qscale;
        }

        if(intra_matrix)
            video_enc->intra_matrix = intra_matrix;
        if(inter_matrix)
            video_enc->inter_matrix = inter_matrix;

        p= video_rc_override_string;
        for(i=0; p; i++){
            int start, end, q;
            int e=sscanf(p, "%d,%d,%d", &start, &end, &q);
            if(e!=3){
                fprintf(stderr, "error parsing rc_override\n");
                exit_program(1);
            }
            video_enc->rc_override=
                av_realloc(video_enc->rc_override,
                           sizeof(RcOverride)*(i+1));
            video_enc->rc_override[i].start_frame= start;
            video_enc->rc_override[i].end_frame  = end;
            if(q>0){
                video_enc->rc_override[i].qscale= q;
                video_enc->rc_override[i].quality_factor= 1.0;
            }
            else{
                video_enc->rc_override[i].qscale= 0;
                video_enc->rc_override[i].quality_factor= -q/100.0;
            }
            p= strchr(p, '/');
            if(p) p++;
        }
        video_enc->rc_override_count=i;
        if (!video_enc->rc_initial_buffer_occupancy)
            video_enc->rc_initial_buffer_occupancy = video_enc->rc_buffer_size*3/4;
        video_enc->me_threshold= me_threshold;
        video_enc->intra_dc_precision= intra_dc_precision - 8;

        if (do_psnr)
            video_enc->flags|= CODEC_FLAG_PSNR;

        /* two pass mode */
        if (do_pass) {
            if (do_pass == 1) {
                video_enc->flags |= CODEC_FLAG_PASS1;
            } else {
                video_enc->flags |= CODEC_FLAG_PASS2;
            }
        }

        if (forced_key_frames)
            parse_forced_key_frames(forced_key_frames, ost, video_enc);
    }
    if (video_language) {
        av_dict_set(&st->metadata, "language", video_language, 0);
        av_freep(&video_language);
    }

    /* reset some key parameters */
    video_disable = 0;
    av_freep(&forced_key_frames);
    frame_pix_fmt = PIX_FMT_NONE;
    return ost;
}

static OutputStream *new_audio_stream(AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *audio_enc;

    ost = new_output_stream(oc, AVMEDIA_TYPE_AUDIO);
    st  = ost->st;

    ost->bitstream_filters = audio_bitstream_filters;
    audio_bitstream_filters= NULL;

    st->codec->thread_count= thread_count;

    audio_enc = st->codec;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    if(audio_codec_tag)
        audio_enc->codec_tag= audio_codec_tag;

    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    if (!st->stream_copy) {
        if (audio_qscale > QSCALE_NONE) {
            audio_enc->flags |= CODEC_FLAG_QSCALE;
            audio_enc->global_quality = FF_QP2LAMBDA * audio_qscale;
        }
        if (audio_channels)
            audio_enc->channels = audio_channels;
        if (audio_sample_fmt != AV_SAMPLE_FMT_NONE)
            audio_enc->sample_fmt = audio_sample_fmt;
        if (audio_sample_rate)
            audio_enc->sample_rate = audio_sample_rate;
    }
    if (audio_language) {
        av_dict_set(&st->metadata, "language", audio_language, 0);
        av_freep(&audio_language);
    }

    /* reset some key parameters */
    audio_disable = 0;

    return ost;
}

static OutputStream *new_data_stream(AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *data_enc;

    ost = new_output_stream(oc, AVMEDIA_TYPE_DATA);
    st  = ost->st;
    data_enc = st->codec;
    if (!st->stream_copy) {
        fprintf(stderr, "Data stream encoding not supported yet (only streamcopy)\n");
        exit_program(1);
    }

    if (data_codec_tag)
        data_enc->codec_tag= data_codec_tag;

    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        data_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    data_disable = 0;
    return ost;
}

static OutputStream *new_subtitle_stream(AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *subtitle_enc;

    ost = new_output_stream(oc, AVMEDIA_TYPE_SUBTITLE);
    st  = ost->st;
    subtitle_enc = st->codec;

    ost->bitstream_filters = subtitle_bitstream_filters;
    subtitle_bitstream_filters= NULL;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    if(subtitle_codec_tag)
        subtitle_enc->codec_tag= subtitle_codec_tag;

    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        subtitle_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    if (subtitle_language) {
        av_dict_set(&st->metadata, "language", subtitle_language, 0);
        av_freep(&subtitle_language);
    }

    subtitle_disable = 0;
    return ost;
}

/* arg format is "output-stream-index:streamid-value". */
static int opt_streamid(const char *opt, const char *arg)
{
    int idx;
    char *p;
    char idx_str[16];

    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        fprintf(stderr,
                "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
                arg, opt);
        exit_program(1);
    }
    *p++ = '\0';
    idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, MAX_STREAMS-1);
    streamid_map = grow_array(streamid_map, sizeof(*streamid_map), &nb_streamid_map, idx+1);
    streamid_map[idx] = parse_number_or_die(opt, p, OPT_INT, 0, INT_MAX);
    return 0;
}

static int copy_chapters(int infile, int outfile)
{
    AVFormatContext *is = input_files[infile].ctx;
    AVFormatContext *os = output_files[outfile].ctx;
    int i;

    for (i = 0; i < is->nb_chapters; i++) {
        AVChapter *in_ch = is->chapters[i], *out_ch;
        int64_t ts_off   = av_rescale_q(start_time - input_files[infile].ts_offset,
                                      AV_TIME_BASE_Q, in_ch->time_base);
        int64_t rt       = (recording_time == INT64_MAX) ? INT64_MAX :
                           av_rescale_q(recording_time, AV_TIME_BASE_Q, in_ch->time_base);


        if (in_ch->end < ts_off)
            continue;
        if (rt != INT64_MAX && in_ch->start > rt + ts_off)
            break;

        out_ch = av_mallocz(sizeof(AVChapter));
        if (!out_ch)
            return AVERROR(ENOMEM);

        out_ch->id        = in_ch->id;
        out_ch->time_base = in_ch->time_base;
        out_ch->start     = FFMAX(0,  in_ch->start - ts_off);
        out_ch->end       = FFMIN(rt, in_ch->end   - ts_off);

        if (metadata_chapters_autocopy)
            av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

        os->nb_chapters++;
        os->chapters = av_realloc(os->chapters, sizeof(AVChapter)*os->nb_chapters);
        if (!os->chapters)
            return AVERROR(ENOMEM);
        os->chapters[os->nb_chapters - 1] = out_ch;
    }
    return 0;
}

static int read_ffserver_streams(AVFormatContext *s, const char *filename)
{
    int i, err;
    AVFormatContext *ic = NULL;

    err = avformat_open_input(&ic, filename, NULL, NULL);
    if (err < 0)
        return err;
    /* copy stream format */
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st;
        OutputStream *ost;
        AVCodec *codec;

        codec = avcodec_find_encoder(ic->streams[i]->codec->codec_id);
        ost   = new_output_stream(s, codec->type);
        st    = ost->st;

        // FIXME: a more elegant solution is needed
        memcpy(st, ic->streams[i], sizeof(AVStream));
        st->info = av_malloc(sizeof(*st->info));
        memcpy(st->info, ic->streams[i]->info, sizeof(*st->info));
        avcodec_copy_context(st->codec, ic->streams[i]->codec);

        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO && !st->stream_copy)
            choose_sample_fmt(st, codec);
        else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && !st->stream_copy)
            choose_pixel_fmt(st, codec);
    }

    av_close_input_file(ic);
    return 0;
}

static int opt_output_file(const char *opt, const char *filename)
{
    AVFormatContext *oc;
    int i, err;
    AVOutputFormat *file_oformat;
    OutputStream *ost;
    InputStream  *ist;

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    err = avformat_alloc_output_context2(&oc, NULL, last_asked_format, filename);
    last_asked_format = NULL;
    if (!oc) {
        print_error(filename, err);
        exit_program(1);
    }

    file_oformat= oc->oformat;

    if (!strcmp(file_oformat->name, "ffm") &&
        av_strstart(filename, "http:", NULL)) {
        /* special case for files sent to ffserver: we get the stream
           parameters from ffserver */
        int err = read_ffserver_streams(oc, filename);
        if (err < 0) {
            print_error(filename, err);
            exit_program(1);
        }
    } else if (!nb_stream_maps) {
        /* pick the "best" stream of each type */
#define NEW_STREAM(type, index)\
        if (index >= 0) {\
            ost = new_ ## type ## _stream(oc);\
            ost->source_index = index;\
            ost->sync_ist     = &input_streams[index];\
            input_streams[index].discard = 0;\
        }

        /* video: highest resolution */
        if (!video_disable && oc->oformat->video_codec != CODEC_ID_NONE) {
            int area = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = &input_streams[i];
                if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                    ist->st->codec->width * ist->st->codec->height > area) {
                    area = ist->st->codec->width * ist->st->codec->height;
                    idx = i;
                }
            }
            NEW_STREAM(video, idx);
        }

        /* audio: most channels */
        if (!audio_disable && oc->oformat->audio_codec != CODEC_ID_NONE) {
            int channels = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = &input_streams[i];
                if (ist->st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                    ist->st->codec->channels > channels) {
                    channels = ist->st->codec->channels;
                    idx = i;
                }
            }
            NEW_STREAM(audio, idx);
        }

        /* subtitles: pick first */
        if (!subtitle_disable && oc->oformat->subtitle_codec != CODEC_ID_NONE) {
            for (i = 0; i < nb_input_streams; i++)
                if (input_streams[i].st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    NEW_STREAM(subtitle, i);
                    break;
                }
        }
        /* do something with data? */
    } else {
        for (i = 0; i < nb_stream_maps; i++) {
            StreamMap *map = &stream_maps[i];

            if (map->disabled)
                continue;

            ist = &input_streams[input_files[map->file_index].ist_index + map->stream_index];
            switch (ist->st->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:    ost = new_video_stream(oc);    break;
            case AVMEDIA_TYPE_AUDIO:    ost = new_audio_stream(oc);    break;
            case AVMEDIA_TYPE_SUBTITLE: ost = new_subtitle_stream(oc); break;
            case AVMEDIA_TYPE_DATA:     ost = new_data_stream(oc);     break;
            default:
                av_log(NULL, AV_LOG_ERROR, "Cannot map stream #%d.%d - unsupported type.\n",
                       map->file_index, map->stream_index);
                exit_program(1);
            }

            ost->source_index = input_files[map->file_index].ist_index + map->stream_index;
            ost->sync_ist = &input_streams[input_files[map->sync_file_index].ist_index +
                                           map->sync_stream_index];
            ist->discard = 0;
        }
    }

    av_dict_copy(&oc->metadata, metadata, 0);
    av_dict_free(&metadata);


    output_files = grow_array(output_files, sizeof(*output_files), &nb_output_files, nb_output_files + 1);
    output_files[nb_output_files - 1].ctx       = oc;
    output_files[nb_output_files - 1].ost_index = nb_output_streams - oc->nb_streams;
    output_files[nb_output_files - 1].recording_time = recording_time;
    output_files[nb_output_files - 1].start_time     = start_time;
    output_files[nb_output_files - 1].limit_filesize = limit_filesize;
    av_dict_copy(&output_files[nb_output_files - 1].opts, format_opts, 0);

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(oc->filename)) {
            print_error(oc->filename, AVERROR(EINVAL));
            exit_program(1);
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid loosing precious files */
        if (!file_overwrite &&
            (strchr(filename, ':') == NULL ||
             filename[1] == ':' ||
             av_strstart(filename, "file:", NULL))) {
            if (avio_check(filename, 0) == 0) {
                if (!using_stdin) {
                    fprintf(stderr,"File '%s' already exists. Overwrite ? [y/N] ", filename);
                    fflush(stderr);
                    if (!read_yesno()) {
                        fprintf(stderr, "Not overwriting - exiting\n");
                        exit_program(1);
                    }
                }
                else {
                    fprintf(stderr,"File '%s' already exists. Exiting.\n", filename);
                    exit_program(1);
                }
            }
        }

        /* open the file */
        if ((err = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE)) < 0) {
            print_error(filename, err);
            exit_program(1);
        }
    }

    oc->preload= (int)(mux_preload*AV_TIME_BASE);
    oc->max_delay= (int)(mux_max_delay*AV_TIME_BASE);

    /* copy chapters */
    if (chapters_input_file >= nb_input_files) {
        if (chapters_input_file == INT_MAX) {
            /* copy chapters from the first input file that has them*/
            chapters_input_file = -1;
            for (i = 0; i < nb_input_files; i++)
                if (input_files[i].ctx->nb_chapters) {
                    chapters_input_file = i;
                    break;
                }
        } else {
            av_log(NULL, AV_LOG_ERROR, "Invalid input file index %d in chapter mapping.\n",
                   chapters_input_file);
            exit_program(1);
        }
    }
    if (chapters_input_file >= 0)
        copy_chapters(chapters_input_file, nb_output_files - 1);

    /* copy metadata */
    for (i = 0; i < nb_meta_data_maps; i++) {
        AVFormatContext *files[2];
        AVDictionary    **meta[2];
        int j;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
        if ((index) < 0 || (index) >= (nb_elems)) {\
            av_log(NULL, AV_LOG_ERROR, "Invalid %s index %d while processing metadata maps\n",\
                     (desc), (index));\
            exit_program(1);\
        }

        int in_file_index = meta_data_maps[i][1].file;
        if (in_file_index < 0)
            continue;
        METADATA_CHECK_INDEX(in_file_index, nb_input_files, "input file")

        files[0] = oc;
        files[1] = input_files[in_file_index].ctx;

        for (j = 0; j < 2; j++) {
            MetadataMap *map = &meta_data_maps[i][j];

            switch (map->type) {
            case 'g':
                meta[j] = &files[j]->metadata;
                break;
            case 's':
                METADATA_CHECK_INDEX(map->index, files[j]->nb_streams, "stream")
                meta[j] = &files[j]->streams[map->index]->metadata;
                break;
            case 'c':
                METADATA_CHECK_INDEX(map->index, files[j]->nb_chapters, "chapter")
                meta[j] = &files[j]->chapters[map->index]->metadata;
                break;
            case 'p':
                METADATA_CHECK_INDEX(map->index, files[j]->nb_programs, "program")
                meta[j] = &files[j]->programs[map->index]->metadata;
                break;
            }
        }

        av_dict_copy(meta[0], *meta[1], AV_DICT_DONT_OVERWRITE);
    }

    /* copy global metadata by default */
    if (metadata_global_autocopy && nb_input_files)
        av_dict_copy(&oc->metadata, input_files[0].ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
    if (metadata_streams_autocopy)
        for (i = output_files[nb_output_files - 1].ost_index; i < nb_output_streams; i++) {
            InputStream *ist = &input_streams[output_streams[i].source_index];
            av_dict_copy(&output_streams[i].st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
        }

    frame_rate    = (AVRational){0, 0};
    frame_width   = 0;
    frame_height  = 0;
    audio_sample_rate = 0;
    audio_channels    = 0;
    audio_sample_fmt  = AV_SAMPLE_FMT_NONE;
    chapters_input_file = INT_MAX;
    recording_time = INT64_MAX;
    start_time     = 0;
    limit_filesize = UINT64_MAX;

    av_freep(&meta_data_maps);
    nb_meta_data_maps = 0;
    metadata_global_autocopy   = 1;
    metadata_streams_autocopy  = 1;
    metadata_chapters_autocopy = 1;
    av_freep(&stream_maps);
    nb_stream_maps = 0;
    av_freep(&streamid_map);
    nb_streamid_map = 0;

    av_dict_free(&codec_names);

    av_freep(&forced_key_frames);
    uninit_opts();
    init_opts();
    return 0;
}

/* same option as mencoder */
static int opt_pass(const char *opt, const char *arg)
{
    do_pass = parse_number_or_die(opt, arg, OPT_INT, 1, 2);
    return 0;
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

static void parse_matrix_coeffs(uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for(i = 0;; i++) {
        dest[i] = atoi(p);
        if(i == 63)
            break;
        p = strchr(p, ',');
        if(!p) {
            fprintf(stderr, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
            exit_program(1);
        }
        p++;
    }
}

static int opt_inter_matrix(const char *opt, const char *arg)
{
    inter_matrix = av_mallocz(sizeof(uint16_t) * 64);
    parse_matrix_coeffs(inter_matrix, arg);
    return 0;
}

static int opt_intra_matrix(const char *opt, const char *arg)
{
    intra_matrix = av_mallocz(sizeof(uint16_t) * 64);
    parse_matrix_coeffs(intra_matrix, arg);
    return 0;
}

static void show_usage(void)
{
    printf("Hyper fast Audio and Video encoder\n");
    printf("usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
    printf("\n");
}

static int opt_help(const char *opt, const char *arg)
{
    AVCodec *c;
    AVOutputFormat *oformat = NULL;
    AVInputFormat  *iformat = NULL;

    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE | OPT_GRAB, 0);
    show_help_options(options, "\nAdvanced options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE | OPT_GRAB,
                      OPT_EXPERT);
    show_help_options(options, "\nVideo options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
                      OPT_VIDEO);
    show_help_options(options, "\nAdvanced Video options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
                      OPT_VIDEO | OPT_EXPERT);
    show_help_options(options, "\nAudio options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
                      OPT_AUDIO);
    show_help_options(options, "\nAdvanced Audio options:\n",
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
                      OPT_AUDIO | OPT_EXPERT);
    show_help_options(options, "\nSubtitle options:\n",
                      OPT_SUBTITLE | OPT_GRAB,
                      OPT_SUBTITLE);
    show_help_options(options, "\nAudio/Video grab options:\n",
                      OPT_GRAB,
                      OPT_GRAB);
    printf("\n");
    av_opt_show2(avcodec_opts[0], NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    printf("\n");

    /* individual codec options */
    c = NULL;
    while ((c = av_codec_next(c))) {
        if (c->priv_class) {
            av_opt_show2(&c->priv_class, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
            printf("\n");
        }
    }

    av_opt_show2(avformat_opts, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    printf("\n");

    /* individual muxer options */
    while ((oformat = av_oformat_next(oformat))) {
        if (oformat->priv_class) {
            av_opt_show2(&oformat->priv_class, NULL, AV_OPT_FLAG_ENCODING_PARAM, 0);
            printf("\n");
        }
    }

    /* individual demuxer options */
    while ((iformat = av_iformat_next(iformat))) {
        if (iformat->priv_class) {
            av_opt_show2(&iformat->priv_class, NULL, AV_OPT_FLAG_DECODING_PARAM, 0);
            printf("\n");
        }
    }

    av_opt_show2(sws_opts, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    return 0;
}

static int opt_target(const char *opt, const char *arg)
{
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
    static const char *const frame_rates[] = {"25", "30000/1001", "24000/1001"};

    if(!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if(!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if(!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        int fr;
        /* Calculate FR via float to avoid int overflow */
        fr = (int)(frame_rate.num * 1000.0 / frame_rate.den);
        if(fr == 25000) {
            norm = PAL;
        } else if((fr == 29970) || (fr == 23976)) {
            norm = NTSC;
        } else {
            /* Try to determine PAL/NTSC by peeking in the input files */
            if(nb_input_files) {
                int i, j;
                for (j = 0; j < nb_input_files; j++) {
                    for (i = 0; i < input_files[j].ctx->nb_streams; i++) {
                        AVCodecContext *c = input_files[j].ctx->streams[i]->codec;
                        if(c->codec_type != AVMEDIA_TYPE_VIDEO)
                            continue;
                        fr = c->time_base.den * 1000 / c->time_base.num;
                        if(fr == 25000) {
                            norm = PAL;
                            break;
                        } else if((fr == 29970) || (fr == 23976)) {
                            norm = NTSC;
                            break;
                        }
                    }
                    if(norm != UNKNOWN)
                        break;
                }
            }
        }
        if(verbose > 0 && norm != UNKNOWN)
            fprintf(stderr, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if(norm == UNKNOWN) {
        fprintf(stderr, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        fprintf(stderr, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        fprintf(stderr, "or set a framerate with \"-r xxx\".\n");
        exit_program(1);
    }

    if(!strcmp(arg, "vcd")) {
        opt_codec("c:v", "mpeg1video");
        opt_codec("c:a", "mp2");
        opt_format("f", "vcd");

        opt_frame_size("s", norm == PAL ? "352x288" : "352x240");
        opt_frame_rate("r", frame_rates[norm]);
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b", "1150000");
        opt_default("maxrate", "1150000");
        opt_default("minrate", "1150000");
        opt_default("bufsize", "327680"); // 40*1024*8;

        opt_default("b:a", "224000");
        audio_sample_rate = 44100;
        audio_channels = 2;

        opt_default("packetsize", "2324");
        opt_default("muxrate", "1411200"); // 2352 * 75 * 8;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        mux_preload= (36000+3*1200) / 90000.0; //0.44
    } else if(!strcmp(arg, "svcd")) {

        opt_codec("c:v", "mpeg2video");
        opt_codec("c:a", "mp2");
        opt_format("f", "svcd");

        opt_frame_size("s", norm == PAL ? "480x576" : "480x480");
        opt_frame_rate("r", frame_rates[norm]);
        opt_frame_pix_fmt("pix_fmt", "yuv420p");
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b", "2040000");
        opt_default("maxrate", "2516000");
        opt_default("minrate", "0"); //1145000;
        opt_default("bufsize", "1835008"); //224*1024*8;
        opt_default("flags", "+scan_offset");


        opt_default("b:a", "224000");
        audio_sample_rate = 44100;

        opt_default("packetsize", "2324");

    } else if(!strcmp(arg, "dvd")) {

        opt_codec("c:v", "mpeg2video");
        opt_codec("c:a", "ac3");
        opt_format("f", "dvd");

        opt_frame_size("vcodec", norm == PAL ? "720x576" : "720x480");
        opt_frame_rate("r", frame_rates[norm]);
        opt_frame_pix_fmt("pix_fmt", "yuv420p");
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b", "6000000");
        opt_default("maxrate", "9000000");
        opt_default("minrate", "0"); //1500000;
        opt_default("bufsize", "1835008"); //224*1024*8;

        opt_default("packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        opt_default("muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

        opt_default("b:a", "448000");
        audio_sample_rate = 48000;

    } else if(!strncmp(arg, "dv", 2)) {

        opt_format("f", "dv");

        opt_frame_size("s", norm == PAL ? "720x576" : "720x480");
        opt_frame_pix_fmt("pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                          norm == PAL ? "yuv420p" : "yuv411p");
        opt_frame_rate("r", frame_rates[norm]);

        audio_sample_rate = 48000;
        audio_channels = 2;

    } else {
        fprintf(stderr, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_vstats_file(const char *opt, const char *arg)
{
    av_free (vstats_filename);
    vstats_filename=av_strdup (arg);
    return 0;
}

static int opt_vstats(const char *opt, const char *arg)
{
    char filename[40];
    time_t today2 = time(NULL);
    struct tm *today = localtime(&today2);

    snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
             today->tm_sec);
    return opt_vstats_file(opt, filename);
}

static int opt_bsf(const char *opt, const char *arg)
{
    AVBitStreamFilterContext *bsfc= av_bitstream_filter_init(arg); //FIXME split name and args for filter at '='
    AVBitStreamFilterContext **bsfp;

    if(!bsfc){
        fprintf(stderr, "Unknown bitstream filter %s\n", arg);
        exit_program(1);
    }

    bsfp= *opt == 'v' ? &video_bitstream_filters :
          *opt == 'a' ? &audio_bitstream_filters :
                        &subtitle_bitstream_filters;
    while(*bsfp)
        bsfp= &(*bsfp)->next;

    *bsfp= bsfc;

    return 0;
}

static void log_callback_null(void* ptr, int level, const char* fmt, va_list vl)
{
}

static int opt_passlogfile(const char *opt, const char *arg)
{
    pass_logfilename_prefix = arg;
#if CONFIG_LIBX264_ENCODER
    return opt_default("passlogfile", arg);
#else
    return 0;
#endif
}

static const OptionDef options[] = {
    /* main options */
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "i", HAS_ARG, {(void*)opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
    { "c", HAS_ARG, {(void*)opt_codec}, "codec name", "codec" },
    { "codec", HAS_ARG, {(void*)opt_codec}, "codec name", "codec" },
    { "map", HAS_ARG | OPT_EXPERT, {(void*)opt_map}, "set input stream mapping", "file.stream[:syncfile.syncstream]" },
    { "map_metadata", HAS_ARG | OPT_EXPERT, {(void*)opt_map_metadata}, "set metadata information of outfile from infile",
      "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",  OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&chapters_input_file},  "set chapters mapping", "input_file_index" },
    { "t", HAS_ARG, {(void*)opt_recording_time}, "record or transcode \"duration\" seconds of audio/video", "duration" },
    { "fs", HAS_ARG | OPT_INT64, {(void*)&limit_filesize}, "set the limit file size in bytes", "limit_size" }, //
    { "ss", HAS_ARG, {(void*)opt_start_time}, "set the start time offset", "time_off" },
    { "itsoffset", HAS_ARG, {(void*)opt_input_ts_offset}, "set the input ts offset", "time_off" },
    { "itsscale", HAS_ARG, {(void*)opt_input_ts_scale}, "set the input ts scale", "scale" },
    { "metadata", HAS_ARG, {(void*)opt_metadata}, "add metadata", "string=string" },
    { "dframes", OPT_INT | HAS_ARG, {(void*)&max_frames[AVMEDIA_TYPE_DATA]}, "set the number of data frames to record", "number" },
    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark},
      "add timings for benchmarking" },
    { "timelimit", HAS_ARG, {(void*)opt_timelimit}, "set max runtime in seconds", "limit" },
    { "dump", OPT_BOOL | OPT_EXPERT, {(void*)&do_pkt_dump},
      "dump each input packet" },
    { "hex", OPT_BOOL | OPT_EXPERT, {(void*)&do_hex_dump},
      "when dumping packets, also dump the payload" },
    { "re", OPT_BOOL | OPT_EXPERT, {(void*)&rate_emu}, "read input at native frame rate", "" },
    { "v", HAS_ARG, {(void*)opt_verbose}, "set the verbosity level", "number" },
    { "target", HAS_ARG, {(void*)opt_target}, "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\", \"dv50\", \"pal-vcd\", \"ntsc-svcd\", ...)", "type" },
    { "threads",  HAS_ARG | OPT_EXPERT, {(void*)opt_thread_count}, "thread count", "count" },
    { "vsync", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&video_sync_method}, "video sync method", "" },
    { "async", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&audio_sync_method}, "audio sync method", "" },
    { "adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&audio_drift_threshold}, "audio drift threshold", "threshold" },
    { "copyts", OPT_BOOL | OPT_EXPERT, {(void*)&copy_ts}, "copy timestamps" },
    { "copytb", OPT_BOOL | OPT_EXPERT, {(void*)&copy_tb}, "copy input stream time base when stream copying" },
    { "shortest", OPT_BOOL | OPT_EXPERT, {(void*)&opt_shortest}, "finish encoding within shortest input" }, //
    { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&dts_delta_threshold}, "timestamp discontinuity delta threshold", "threshold" },
    { "programid", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&opt_programid}, "desired program number", "" },
    { "xerror", OPT_BOOL, {(void*)&exit_on_error}, "exit on error", "error" },
    { "copyinkf", OPT_BOOL | OPT_EXPERT, {(void*)&copy_initial_nonkeyframes}, "copy initial non-keyframes" },

    /* video options */
    { "vframes", OPT_INT | HAS_ARG | OPT_VIDEO, {(void*)&max_frames[AVMEDIA_TYPE_VIDEO]}, "set the number of video frames to record", "number" },
    { "r", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_rate}, "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "aspect", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_aspect_ratio}, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_frame_pix_fmt}, "set pixel format, 'list' as argument shows all the pixel formats supported", "format" },
    { "bits_per_raw_sample", OPT_INT | HAS_ARG | OPT_VIDEO, {(void*)&frame_bits_per_raw_sample}, "set the number of bits per raw sample", "number" },
    { "croptop",  HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
    { "cropbottom", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
    { "cropleft", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
    { "cropright", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
    { "padtop", HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
    { "padbottom", HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
    { "padleft", HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
    { "padright", HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
    { "padcolor", HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "color" },
    { "vn", OPT_BOOL | OPT_VIDEO, {(void*)&video_disable}, "disable video" },
    { "vdt", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&video_discard}, "discard threshold", "n" },
    { "qscale", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qscale}, "use fixed video quantizer scale (VBR)", "q" },
    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_override_string}, "rate control override for specific intervals", "override" },
    { "vcodec", HAS_ARG | OPT_VIDEO, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
    { "me_threshold", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_me_threshold}, "motion estimation threshold",  "threshold" },
    { "same_quant", OPT_BOOL | OPT_VIDEO, {(void*)&same_quant},
      "use same quantizer as source (implies VBR)" },
    { "pass", HAS_ARG | OPT_VIDEO, {(void*)opt_pass}, "select the pass number (1 or 2)", "n" },
    { "passlogfile", HAS_ARG | OPT_VIDEO, {(void*)&opt_passlogfile}, "select two pass log file name prefix", "prefix" },
    { "deinterlace", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_deinterlace},
      "deinterlace pictures" },
    { "psnr", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
    { "vstats", OPT_EXPERT | OPT_VIDEO, {(void*)&opt_vstats}, "dump video coding statistics to file" },
    { "vstats_file", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_vstats_file}, "dump video coding statistics to file", "file" },
#if CONFIG_AVFILTER
    { "vf", OPT_STRING | HAS_ARG, {(void*)&vfilters}, "video filters", "filter list" },
#endif
    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_intra_matrix}, "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_inter_matrix}, "specify inter matrix coeffs", "matrix" },
    { "top", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_top_field_first}, "top=1/bottom=0/auto=-1 field first", "" },
    { "dc", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_dc_precision}, "intra_dc_precision", "precision" },
    { "vtag", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_codec_tag}, "force video tag/fourcc", "fourcc/tag" },
    { "vlang", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void *)&video_language}, "set the ISO 639 language code (3 letters) of the current video stream" , "code" },
    { "qphist", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, { (void *)&qp_hist }, "show QP histogram" },
    { "force_fps", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&force_fps}, "force the selected framerate, disable the best supported framerate selection" },
    { "streamid", HAS_ARG | OPT_EXPERT, {(void*)opt_streamid}, "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames", OPT_STRING | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void *)&forced_key_frames}, "force key frames at specified timestamps", "timestamps" },

    /* audio options */
    { "aframes", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&max_frames[AVMEDIA_TYPE_AUDIO]}, "set the number of audio frames to record", "number" },
    { "aq", OPT_FLOAT | HAS_ARG | OPT_AUDIO, {(void*)&audio_qscale}, "set audio quality (codec-specific)", "quality", },
    { "ar", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_rate}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_channels}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL | OPT_AUDIO, {(void*)&audio_disable}, "disable audio" },
    { "acodec", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_codec}, "force audio codec ('copy' to copy stream)", "codec" },
    { "atag", HAS_ARG | OPT_EXPERT | OPT_AUDIO, {(void*)opt_codec_tag}, "force audio tag/fourcc", "fourcc/tag" },
    { "vol", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&audio_volume}, "change audio volume (256=normal)" , "volume" }, //
    { "alang", HAS_ARG | OPT_STRING | OPT_AUDIO, {(void *)&audio_language}, "set the ISO 639 language code (3 letters) of the current audio stream" , "code" },
    { "sample_fmt", HAS_ARG | OPT_EXPERT | OPT_AUDIO, {(void*)opt_audio_sample_fmt}, "set sample format, 'list' as argument shows all the sample formats supported", "format" },

    /* subtitle options */
    { "sn", OPT_BOOL | OPT_SUBTITLE, {(void*)&subtitle_disable}, "disable subtitle" },
    { "scodec", HAS_ARG | OPT_SUBTITLE, {(void*)opt_subtitle_codec}, "force subtitle codec ('copy' to copy stream)", "codec" },
    { "slang", HAS_ARG | OPT_STRING | OPT_SUBTITLE, {(void *)&subtitle_language}, "set the ISO 639 language code (3 letters) of the current subtitle stream" , "code" },
    { "stag", HAS_ARG | OPT_EXPERT | OPT_SUBTITLE, {(void*)opt_codec_tag}, "force subtitle tag/fourcc", "fourcc/tag" },

    /* grab options */
    { "isync", OPT_BOOL | OPT_EXPERT | OPT_GRAB, {(void*)&input_sync}, "sync read on input", "" },

    /* muxer options */
    { "muxdelay", OPT_FLOAT | HAS_ARG | OPT_EXPERT, {(void*)&mux_max_delay}, "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT, {(void*)&mux_preload}, "set the initial demux-decode delay", "seconds" },

    { "absf", HAS_ARG | OPT_AUDIO | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },
    { "vbsf", HAS_ARG | OPT_VIDEO | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },
    { "sbsf", HAS_ARG | OPT_SUBTITLE | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },

    /* data codec support */
    { "dcodec", HAS_ARG | OPT_DATA, {(void*)opt_data_codec}, "force data codec ('copy' to copy stream)", "codec" },

    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { NULL, },
};

int main(int argc, char **argv)
{
    int64_t ti;

    av_log_set_flags(AV_LOG_SKIP_REPEATED);

    if(argc>1 && !strcmp(argv[1], "-d")){
        run_as_daemon=1;
        verbose=-1;
        av_log_set_callback(log_callback_null);
        argc--;
        argv++;
    }

    avcodec_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_register_all();

#if HAVE_ISATTY
    if(isatty(STDIN_FILENO))
        avio_set_interrupt_cb(decode_interrupt_cb);
#endif

    init_opts();

    if(verbose>=0)
        show_banner();

    /* parse options */
    parse_options(argc, argv, options, opt_output_file);

    if(nb_output_files <= 0 && nb_input_files == 0) {
        show_usage();
        fprintf(stderr, "Use -h to get full help or, even better, run 'man %s'\n", program_name);
        exit_program(1);
    }

    /* file converter / grab */
    if (nb_output_files <= 0) {
        fprintf(stderr, "At least one output file must be specified\n");
        exit_program(1);
    }

    if (nb_input_files == 0) {
        fprintf(stderr, "At least one input file must be specified\n");
        exit_program(1);
    }

    ti = getutime();
    if (transcode(output_files, nb_output_files, input_files, nb_input_files) < 0)
        exit_program(1);
    ti = getutime() - ti;
    if (do_benchmark) {
        int maxrss = getmaxrss() / 1024;
        printf("bench: utime=%0.3fs maxrss=%ikB\n", ti / 1000000.0, maxrss);
    }

    return exit_program(0);
}
