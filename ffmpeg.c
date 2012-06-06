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
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
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
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libavutil/bprint.h"
#include "libavformat/os_support.h"

#include "libavformat/ffm.h" // not public API

# include "libavfilter/avcodec.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/buffersrc.h"
# include "libavfilter/buffersink.h"

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

#define VSYNC_AUTO       -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR         1
#define VSYNC_VFR         2
#define VSYNC_DROP        0xff

#define SINKA

const char program_name[] = "ffmpeg";
const int program_birth_year = 2000;

/* select an input stream for an output stream */
typedef struct StreamMap {
    int disabled;           /** 1 is this mapping is disabled by a negative map */
    int file_index;
    int stream_index;
    int sync_file_index;
    int sync_stream_index;
    char *linklabel;       /** name of an output link, for mapping lavfi outputs */
} StreamMap;

typedef struct {
    int  file_idx,  stream_idx,  channel_idx; // input
    int ofile_idx, ostream_idx;               // output
} AudioChannelMap;

static const OptionDef options[];

#define MAX_STREAMS 1024    /* arbitrary sanity check value */

static int frame_bits_per_raw_sample = 0;
static int video_discard = 0;
static int same_quant = 0;
static int do_deinterlace = 0;
static int intra_dc_precision = 8;
static int qp_hist = 0;
static int intra_only = 0;
static const char *video_codec_name    = NULL;
static const char *audio_codec_name    = NULL;
static const char *subtitle_codec_name = NULL;

static int file_overwrite = 0;
static int no_file_overwrite = 0;
static int do_benchmark = 0;
static int do_benchmark_all = 0;
static int do_hex_dump = 0;
static int do_pkt_dump = 0;
static int do_psnr = 0;
static int do_pass = 0;
static const char *pass_logfilename_prefix;
static int video_sync_method = VSYNC_AUTO;
static int audio_sync_method = 0;
static float audio_drift_threshold = 0.1;
static int copy_ts = 0;
static int copy_tb = -1;
static int opt_shortest = 0;
static char *vstats_filename;
static FILE *vstats_file;

static int audio_volume = 256;

static int exit_on_error = 0;
static int using_stdin = 0;
static int run_as_daemon  = 0;
static volatile int received_nb_signals = 0;
static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;
static int input_sync;

static float dts_delta_threshold = 10;
static float dts_error_threshold = 3600*30;

static int print_stats = 1;
static int debug_ts = 0;
static int current_time;

#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"

typedef struct InputFilter {
    AVFilterContext    *filter;
    struct InputStream *ist;
    struct FilterGraph *graph;
} InputFilter;

typedef struct OutputFilter {
    AVFilterContext     *filter;
    struct OutputStream *ost;
    struct FilterGraph  *graph;

    /* temporary storage until stream maps are processed */
    AVFilterInOut       *out_tmp;
} OutputFilter;

typedef struct FilterGraph {
    int            index;
    const char    *graph_desc;

    AVFilterGraph *graph;

    InputFilter   **inputs;
    int          nb_inputs;
    OutputFilter **outputs;
    int         nb_outputs;
} FilterGraph;

typedef struct FrameBuffer {
    uint8_t *base[4];
    uint8_t *data[4];
    int  linesize[4];

    int h, w;
    enum PixelFormat pix_fmt;

    int refcount;
    struct InputStream *ist;
    struct FrameBuffer *next;
} FrameBuffer;

typedef struct InputStream {
    int file_index;
    AVStream *st;
    int discard;             /* true if stream data should be discarded */
    int decoding_needed;     /* true if the packets must be decoded in 'raw_fifo' */
    AVCodec *dec;
    AVFrame *decoded_frame;

    int64_t       start;     /* time when read started */
    /* predicted dts of the next packet read for this stream or (when there are
     * several frames in a packet) of the next frame in current packet */
    int64_t       next_dts;
    /* dts of the last packet read for this stream */
    int64_t       dts;

    int64_t       next_pts;  /* synthetic pts for the next decode frame */
    int64_t       pts;       /* current pts of the decoded frame */
    double ts_scale;
    int is_start;            /* is 1 at the start and after a discontinuity */
    int saw_first_ts;
    int showed_multi_packet_warning;
    AVDictionary *opts;

    int resample_height;
    int resample_width;
    int resample_pix_fmt;

    int      resample_sample_fmt;
    int      resample_sample_rate;
    int      resample_channels;
    uint64_t resample_channel_layout;

    /* a pool of free buffers for decoded data */
    FrameBuffer *buffer_pool;
    int dr1;

    /* decoded data from this stream goes into all those filters
     * currently video and audio only */
    InputFilter **filters;
    int        nb_filters;
} InputStream;

typedef struct InputFile {
    AVFormatContext *ctx;
    int eof_reached;      /* true if eof reached */
    int ist_index;        /* index of first stream in input_streams */
    int buffer_size;      /* current total buffer size */
    int64_t ts_offset;
    int nb_streams;       /* number of stream that ffmpeg is aware of; may be different
                             from ctx.nb_streams if new streams appear during av_read_frame() */
    int rate_emu;
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
    struct InputStream *sync_ist; /* input stream to sync against */
    int64_t sync_opts;       /* output frame counter, could be changed to some true timestamp */ // FIXME look at frame_number
    /* pts of the first frame encoded for this stream, used for limiting
     * recording time */
    int64_t first_pts;
    AVBitStreamFilterContext *bitstream_filters;
    AVCodec *enc;
    int64_t max_frames;
    AVFrame *filtered_frame;

    /* video only */
    AVRational frame_rate;
    int force_fps;
    int top_field_first;

    float frame_aspect_ratio;
    float last_quality;

    /* forced key frames */
    int64_t *forced_kf_pts;
    int forced_kf_count;
    int forced_kf_index;

    /* audio only */
    int audio_channels_map[SWR_CH_MAX];  /* list of the channels id to pick from the source stream */
    int audio_channels_mapped;           /* number of channels in audio_channels_map */

    FILE *logfile;

    OutputFilter *filter;
    char *avfilter;

    int64_t sws_flags;
    int64_t swr_dither_method;
    double swr_dither_scale;
    AVDictionary *opts;
    int is_past_recording_time;
    int stream_copy;
    const char *attachment_filename;
    int copy_initial_nonkeyframes;

    int keep_pix_fmt;
} OutputStream;


#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int restore_tty;
#endif

typedef struct OutputFile {
    AVFormatContext *ctx;
    AVDictionary *opts;
    int ost_index;       /* index of the first stream in output_streams */
    int64_t recording_time; /* desired length of the resulting file in microseconds */
    int64_t start_time;     /* start time in microseconds */
    uint64_t limit_filesize; /* filesize limit expressed in bytes */
} OutputFile;

static InputStream **input_streams = NULL;
static int        nb_input_streams = 0;
static InputFile   **input_files   = NULL;
static int        nb_input_files   = 0;

static OutputStream **output_streams = NULL;
static int         nb_output_streams = 0;
static OutputFile   **output_files   = NULL;
static int         nb_output_files   = 0;

static FilterGraph **filtergraphs;
int               nb_filtergraphs;

typedef struct OptionsContext {
    /* input/output options */
    int64_t start_time;
    const char *format;

    SpecifierOpt *codec_names;
    int        nb_codec_names;
    SpecifierOpt *audio_channels;
    int        nb_audio_channels;
    SpecifierOpt *audio_sample_rate;
    int        nb_audio_sample_rate;
    SpecifierOpt *frame_rates;
    int        nb_frame_rates;
    SpecifierOpt *frame_sizes;
    int        nb_frame_sizes;
    SpecifierOpt *frame_pix_fmts;
    int        nb_frame_pix_fmts;

    /* input options */
    int64_t input_ts_offset;
    int rate_emu;

    SpecifierOpt *ts_scale;
    int        nb_ts_scale;
    SpecifierOpt *dump_attachment;
    int        nb_dump_attachment;

    /* output options */
    StreamMap *stream_maps;
    int     nb_stream_maps;
    AudioChannelMap *audio_channel_maps; /* one info entry per -map_channel */
    int           nb_audio_channel_maps; /* number of (valid) -map_channel settings */
    int metadata_global_manual;
    int metadata_streams_manual;
    int metadata_chapters_manual;
    const char **attachments;
    int       nb_attachments;

    int chapters_input_file;

    int64_t recording_time;
    uint64_t limit_filesize;
    float mux_preload;
    float mux_max_delay;

    int video_disable;
    int audio_disable;
    int subtitle_disable;
    int data_disable;

    /* indexed by output file stream index */
    int   *streamid_map;
    int nb_streamid_map;

    SpecifierOpt *metadata;
    int        nb_metadata;
    SpecifierOpt *max_frames;
    int        nb_max_frames;
    SpecifierOpt *bitstream_filters;
    int        nb_bitstream_filters;
    SpecifierOpt *codec_tags;
    int        nb_codec_tags;
    SpecifierOpt *sample_fmts;
    int        nb_sample_fmts;
    SpecifierOpt *qscale;
    int        nb_qscale;
    SpecifierOpt *forced_key_frames;
    int        nb_forced_key_frames;
    SpecifierOpt *force_fps;
    int        nb_force_fps;
    SpecifierOpt *frame_aspect_ratios;
    int        nb_frame_aspect_ratios;
    SpecifierOpt *rc_overrides;
    int        nb_rc_overrides;
    SpecifierOpt *intra_matrices;
    int        nb_intra_matrices;
    SpecifierOpt *inter_matrices;
    int        nb_inter_matrices;
    SpecifierOpt *top_field_first;
    int        nb_top_field_first;
    SpecifierOpt *metadata_map;
    int        nb_metadata_map;
    SpecifierOpt *presets;
    int        nb_presets;
    SpecifierOpt *copy_initial_nonkeyframes;
    int        nb_copy_initial_nonkeyframes;
    SpecifierOpt *filters;
    int        nb_filters;
} OptionsContext;

static void do_video_stats(AVFormatContext *os, OutputStream *ost, int frame_size);

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st)\
{\
    int i, ret;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if ((ret = check_stream_specifier(fmtctx, st, spec)) > 0)\
            outvar = o->name[i].u.type;\
        else if (ret < 0)\
            exit_program(1);\
    }\
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

static void update_benchmark(const char *fmt, ...)
{
    if (do_benchmark_all) {
        int64_t t = getutime();
        va_list va;
        char buf[1024];

        if (fmt) {
            va_start(va, fmt);
            vsnprintf(buf, sizeof(buf), fmt, va);
            va_end(va);
            printf("bench: %8"PRIu64" %s \n", t - current_time, buf);
        }
        current_time = t;
    }
}

static void reset_options(OptionsContext *o, int is_input)
{
    const OptionDef *po = options;
    OptionsContext bak= *o;
    int i;

    /* all OPT_SPEC and OPT_STRING can be freed in generic way */
    while (po->name) {
        void *dst = (uint8_t*)o + po->u.off;

        if (po->flags & OPT_SPEC) {
            SpecifierOpt **so = dst;
            int i, *count = (int*)(so + 1);
            for (i = 0; i < *count; i++) {
                av_freep(&(*so)[i].specifier);
                if (po->flags & OPT_STRING)
                    av_freep(&(*so)[i].u.str);
            }
            av_freep(so);
            *count = 0;
        } else if (po->flags & OPT_OFFSET && po->flags & OPT_STRING)
            av_freep(dst);
        po++;
    }

    for (i = 0; i < o->nb_stream_maps; i++)
        av_freep(&o->stream_maps[i].linklabel);
    av_freep(&o->stream_maps);
    av_freep(&o->audio_channel_maps);
    av_freep(&o->streamid_map);

    memset(o, 0, sizeof(*o));

    if(is_input) o->recording_time = bak.recording_time;
    else         o->recording_time = INT64_MAX;
    o->mux_max_delay  = 0.7;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;

    uninit_opts();
    init_opts();
}

static int alloc_buffer(InputStream *ist, AVCodecContext *s, FrameBuffer **pbuf)
{
    FrameBuffer  *buf = av_mallocz(sizeof(*buf));
    int i, ret;
    const int pixel_size = av_pix_fmt_descriptors[s->pix_fmt].comp[0].step_minus1+1;
    int h_chroma_shift, v_chroma_shift;
    int edge = 32; // XXX should be avcodec_get_edge_width(), but that fails on svq1
    int w = s->width, h = s->height;

    if (!buf)
        return AVERROR(ENOMEM);

    avcodec_align_dimensions(s, &w, &h);

    if (!(s->flags & CODEC_FLAG_EMU_EDGE)) {
        w += 2*edge;
        h += 2*edge;
    }

    if ((ret = av_image_alloc(buf->base, buf->linesize, w, h,
                              s->pix_fmt, 32)) < 0) {
        av_freep(&buf);
        return ret;
    }
    /* XXX this shouldn't be needed, but some tests break without this line
     * those decoders are buggy and need to be fixed.
     * the following tests fail:
     * cdgraphics, ansi, aasc, fraps-v1, qtrle-1bit
     */
    memset(buf->base[0], 128, ret);

    avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);
    for (i = 0; i < FF_ARRAY_ELEMS(buf->data); i++) {
        const int h_shift = i==0 ? 0 : h_chroma_shift;
        const int v_shift = i==0 ? 0 : v_chroma_shift;
        if ((s->flags & CODEC_FLAG_EMU_EDGE) || !buf->linesize[1] || !buf->base[i])
            buf->data[i] = buf->base[i];
        else
            buf->data[i] = buf->base[i] +
                           FFALIGN((buf->linesize[i]*edge >> v_shift) +
                                   (pixel_size*edge >> h_shift), 32);
    }
    buf->w       = s->width;
    buf->h       = s->height;
    buf->pix_fmt = s->pix_fmt;
    buf->ist     = ist;

    *pbuf = buf;
    return 0;
}

static void free_buffer_pool(InputStream *ist)
{
    FrameBuffer *buf = ist->buffer_pool;
    while (buf) {
        ist->buffer_pool = buf->next;
        av_freep(&buf->base[0]);
        av_free(buf);
        buf = ist->buffer_pool;
    }
}

static void unref_buffer(InputStream *ist, FrameBuffer *buf)
{
    av_assert0(buf->refcount > 0);
    buf->refcount--;
    if (!buf->refcount) {
        FrameBuffer *tmp;
        for(tmp= ist->buffer_pool; tmp; tmp= tmp->next)
            av_assert1(tmp != buf);
        buf->next = ist->buffer_pool;
        ist->buffer_pool = buf;
    }
}

static int codec_get_buffer(AVCodecContext *s, AVFrame *frame)
{
    InputStream *ist = s->opaque;
    FrameBuffer *buf;
    int ret, i;

    if(av_image_check_size(s->width, s->height, 0, s) || s->pix_fmt<0)
        return -1;

    if (!ist->buffer_pool && (ret = alloc_buffer(ist, s, &ist->buffer_pool)) < 0)
        return ret;

    buf              = ist->buffer_pool;
    ist->buffer_pool = buf->next;
    buf->next        = NULL;
    if (buf->w != s->width || buf->h != s->height || buf->pix_fmt != s->pix_fmt) {
        av_freep(&buf->base[0]);
        av_free(buf);
        if ((ret = alloc_buffer(ist, s, &buf)) < 0)
            return ret;
    }
    av_assert0(!buf->refcount);
    buf->refcount++;

    frame->opaque        = buf;
    frame->type          = FF_BUFFER_TYPE_USER;
    frame->extended_data = frame->data;
    frame->pkt_pts       = s->pkt ? s->pkt->pts : AV_NOPTS_VALUE;
    frame->width         = buf->w;
    frame->height        = buf->h;
    frame->format        = buf->pix_fmt;
    frame->sample_aspect_ratio = s->sample_aspect_ratio;

    for (i = 0; i < FF_ARRAY_ELEMS(buf->data); i++) {
        frame->base[i]     = buf->base[i];  // XXX h264.c uses base though it shouldn't
        frame->data[i]     = buf->data[i];
        frame->linesize[i] = buf->linesize[i];
    }

    return 0;
}

static void codec_release_buffer(AVCodecContext *s, AVFrame *frame)
{
    InputStream *ist = s->opaque;
    FrameBuffer *buf = frame->opaque;
    int i;

    if(frame->type!=FF_BUFFER_TYPE_USER)
        return avcodec_default_release_buffer(s, frame);

    for (i = 0; i < FF_ARRAY_ELEMS(frame->data); i++)
        frame->data[i] = NULL;

    unref_buffer(ist, buf);
}

static void filter_release_buffer(AVFilterBuffer *fb)
{
    FrameBuffer *buf = fb->priv;
    av_free(fb);
    unref_buffer(buf->ist, buf);
}

static enum PixelFormat choose_pixel_fmt(AVStream *st, AVCodec *codec, enum PixelFormat target)
{
    if (codec && codec->pix_fmts) {
        const enum PixelFormat *p = codec->pix_fmts;
        int has_alpha= av_pix_fmt_descriptors[target].nb_components % 2 == 0;
        enum PixelFormat best= PIX_FMT_NONE;
        if (st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            if (st->codec->codec_id == CODEC_ID_MJPEG) {
                p = (const enum PixelFormat[]) { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_NONE };
            } else if (st->codec->codec_id == CODEC_ID_LJPEG) {
                p = (const enum PixelFormat[]) { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUV420P,
                                                 PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_BGRA, PIX_FMT_NONE };
            }
        }
        for (; *p != PIX_FMT_NONE; p++) {
            best= avcodec_find_best_pix_fmt2(best, *p, target, has_alpha, NULL);
            if (*p == target)
                break;
        }
        if (*p == PIX_FMT_NONE) {
            if (target != PIX_FMT_NONE)
                av_log(NULL, AV_LOG_WARNING,
                       "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
                       av_pix_fmt_descriptors[target].name,
                       codec->name,
                       av_pix_fmt_descriptors[best].name);
            return best;
        }
    }
    return target;
}

static char *choose_pix_fmts(OutputStream *ost)
{
     if (ost->keep_pix_fmt) {
        if (ost->filter)
            avfilter_graph_set_auto_convert(ost->filter->graph->graph,
                                            AVFILTER_AUTO_CONVERT_NONE);
        if (ost->st->codec->pix_fmt == PIX_FMT_NONE)
            return NULL;
        return av_strdup(av_get_pix_fmt_name(ost->st->codec->pix_fmt));
    }
    if (ost->st->codec->pix_fmt != PIX_FMT_NONE) {
        return av_strdup(av_get_pix_fmt_name(choose_pixel_fmt(ost->st, ost->enc, ost->st->codec->pix_fmt)));
    } else if (ost->enc && ost->enc->pix_fmts) {
        const enum PixelFormat *p;
        AVIOContext *s = NULL;
        uint8_t *ret;
        int len;

        if (avio_open_dyn_buf(&s) < 0)
            exit_program(1);

        p = ost->enc->pix_fmts;
        if (ost->st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            if (ost->st->codec->codec_id == CODEC_ID_MJPEG) {
                p = (const enum PixelFormat[]) { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_NONE };
            } else if (ost->st->codec->codec_id == CODEC_ID_LJPEG) {
                p = (const enum PixelFormat[]) { PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUV420P,
                                                    PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_BGRA, PIX_FMT_NONE };
            }
        }

        for (; *p != PIX_FMT_NONE; p++) {
            const char *name = av_get_pix_fmt_name(*p);
            avio_printf(s, "%s:", name);
        }
        len = avio_close_dyn_buf(s, &ret);
        ret[len - 1] = 0;
        return ret;
    } else
        return NULL;
}

/**
 * Define a function for building a string containing a list of
 * allowed formats,
 */
#define DEF_CHOOSE_FORMAT(type, var, supported_list, none, get_name, separator) \
static char *choose_ ## var ## s(OutputStream *ost)                             \
{                                                                               \
    if (ost->st->codec->var != none) {                                          \
        get_name(ost->st->codec->var);                                          \
        return av_strdup(name);                                                 \
    } else if (ost->enc->supported_list) {                                      \
        const type *p;                                                          \
        AVIOContext *s = NULL;                                                  \
        uint8_t *ret;                                                           \
        int len;                                                                \
                                                                                \
        if (avio_open_dyn_buf(&s) < 0)                                          \
            exit_program(1);                                                    \
                                                                                \
        for (p = ost->enc->supported_list; *p != none; p++) {                   \
            get_name(*p);                                                       \
            avio_printf(s, "%s" separator, name);                               \
        }                                                                       \
        len = avio_close_dyn_buf(s, &ret);                                      \
        ret[len - 1] = 0;                                                       \
        return ret;                                                             \
    } else                                                                      \
        return NULL;                                                            \
}

#define GET_PIX_FMT_NAME(pix_fmt)\
    const char *name = av_get_pix_fmt_name(pix_fmt);

// DEF_CHOOSE_FORMAT(enum PixelFormat, pix_fmt, pix_fmts, PIX_FMT_NONE,
//                   GET_PIX_FMT_NAME, ":")

#define GET_SAMPLE_FMT_NAME(sample_fmt)\
    const char *name = av_get_sample_fmt_name(sample_fmt)

DEF_CHOOSE_FORMAT(enum AVSampleFormat, sample_fmt, sample_fmts,
                  AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME, ",")

#define GET_SAMPLE_RATE_NAME(rate)\
    char name[16];\
    snprintf(name, sizeof(name), "%d", rate);

DEF_CHOOSE_FORMAT(int, sample_rate, supported_samplerates, 0,
                  GET_SAMPLE_RATE_NAME, ",")

#define GET_CH_LAYOUT_NAME(ch_layout)\
    char name[16];\
    snprintf(name, sizeof(name), "0x%"PRIx64, ch_layout);

DEF_CHOOSE_FORMAT(uint64_t, channel_layout, channel_layouts, 0,
                  GET_CH_LAYOUT_NAME, ",")

static int configure_audio_filters(FilterGraph *fg, AVFilterContext **in_filter,
                                   AVFilterContext **out_filter)
{
    InputStream  *ist = fg->inputs[0]->ist;
    OutputStream *ost = fg->outputs[0]->ost;
    AVCodecContext *codec  = ost->st->codec;
    AVCodecContext *icodec = ist->st->codec;
    char *sample_fmts, *sample_rates, *channel_layouts;
    char args[256];
    int ret;

    avfilter_graph_free(&fg->graph);
    if (!(fg->graph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:"
             "channel_layout=0x%"PRIx64, ist->st->time_base.num,
             ist->st->time_base.den, icodec->sample_rate,
             av_get_sample_fmt_name(icodec->sample_fmt), icodec->channel_layout);
    ret = avfilter_graph_create_filter(&fg->inputs[0]->filter,
                                       avfilter_get_by_name("abuffer"),
                                       "src", args, NULL, fg->graph);
    if (ret < 0)
        return ret;

    ret = avfilter_graph_create_filter(&fg->outputs[0]->filter,
                                       avfilter_get_by_name("abuffersink_old"),
                                       "out", NULL, NULL, fg->graph);
    if (ret < 0)
        return ret;

    *in_filter  = fg->inputs[0]->filter;
    *out_filter = fg->outputs[0]->filter;

    if (codec->channels && !codec->channel_layout)
        codec->channel_layout = av_get_default_channel_layout(codec->channels);

    sample_fmts     = choose_sample_fmts(ost);
    sample_rates    = choose_sample_rates(ost);
    channel_layouts = choose_channel_layouts(ost);
    if (sample_fmts || sample_rates || channel_layouts) {
        AVFilterContext *format;
        char args[256];
        int len = 0;

        if (sample_fmts)
            len += snprintf(args + len, sizeof(args) - len, "sample_fmts=%s:",
                            sample_fmts);
        if (sample_rates)
            len += snprintf(args + len, sizeof(args) - len, "sample_rates=%s:",
                            sample_rates);
        if (channel_layouts)
            len += snprintf(args + len, sizeof(args) - len, "channel_layouts=%s:",
                            channel_layouts);
        args[len - 1] = 0;

        av_freep(&sample_fmts);
        av_freep(&sample_rates);
        av_freep(&channel_layouts);

        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           "aformat", args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(format, 0, fg->outputs[0]->filter, 0);
        if (ret < 0)
            return ret;

        *out_filter = format;
    }

#define AUTO_INSERT_FILTER(opt_name, filter_name, arg) do {                 \
    AVFilterContext *filt_ctx;                                              \
                                                                            \
    av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi "            \
           "similarly to -af " filter_name "=%s.\n", arg);                  \
                                                                            \
    ret = avfilter_graph_create_filter(&filt_ctx,                           \
                                       avfilter_get_by_name(filter_name),   \
                                       filter_name, arg, NULL, fg->graph);  \
    if (ret < 0)                                                            \
        return ret;                                                         \
                                                                            \
    ret = avfilter_link(*in_filter, 0, filt_ctx, 0);                        \
    if (ret < 0)                                                            \
        return ret;                                                         \
                                                                            \
    *in_filter = filt_ctx;                                                  \
} while (0)

    if (audio_sync_method > 0) {
        char args[256] = {0};

        av_strlcatf(args, sizeof(args), "min_comp=0.001:min_hard_comp=%f", audio_drift_threshold);
        if (audio_sync_method > 1)
            av_strlcatf(args, sizeof(args), ":max_soft_comp=%f", audio_sync_method/(double)icodec->sample_rate);
        AUTO_INSERT_FILTER("-async", "aresample", args);
    }

    if (ost->audio_channels_mapped) {
        int i;
        AVBPrint pan_buf;

        av_bprint_init(&pan_buf, 256, 8192);
        av_bprintf(&pan_buf, "0x%"PRIx64,
                   av_get_default_channel_layout(ost->audio_channels_mapped));
        for (i = 0; i < ost->audio_channels_mapped; i++)
            if (ost->audio_channels_map[i] != -1)
                av_bprintf(&pan_buf, ":c%d=c%d", i, ost->audio_channels_map[i]);

        AUTO_INSERT_FILTER("-map_channel", "pan", pan_buf.str);
        av_bprint_finalize(&pan_buf, NULL);
    }

    if (audio_volume != 256) {
        char args[256];

        snprintf(args, sizeof(args), "%lf", audio_volume / 256.);
        AUTO_INSERT_FILTER("-vol", "volume", args);
    }

    return 0;
}

static int configure_video_filters(FilterGraph *fg, AVFilterContext **in_filter,
                                   AVFilterContext **out_filter)
{
    InputStream  *ist = fg->inputs[0]->ist;
    OutputStream *ost = fg->outputs[0]->ost;
    AVFilterContext *filter;
    AVCodecContext *codec = ost->st->codec;
    AVBufferSinkParams *buffersink_params = av_buffersink_params_alloc();
    char *pix_fmts;
    AVRational sample_aspect_ratio;
    char args[255];
    int ret;

    if (ist->st->sample_aspect_ratio.num) {
        sample_aspect_ratio = ist->st->sample_aspect_ratio;
    } else
        sample_aspect_ratio = ist->st->codec->sample_aspect_ratio;

    snprintf(args, 255, "%d:%d:%d:%d:%d:%d:%d:flags=%d", ist->st->codec->width,
             ist->st->codec->height, ist->st->codec->pix_fmt,
             ist->st->time_base.num, ist->st->time_base.den,
             sample_aspect_ratio.num, sample_aspect_ratio.den,
             SWS_BILINEAR + ((ist->st->codec->flags&CODEC_FLAG_BITEXACT) ? SWS_BITEXACT:0));

    ret = avfilter_graph_create_filter(&fg->inputs[0]->filter,
                                       avfilter_get_by_name("buffer"),
                                       "src", args, NULL, fg->graph);
    if (ret < 0)
        return ret;

#if FF_API_OLD_VSINK_API
    ret = avfilter_graph_create_filter(&fg->outputs[0]->filter,
                                       avfilter_get_by_name("buffersink"),
                                       "out", NULL, NULL, fg->graph);
#else
    ret = avfilter_graph_create_filter(&fg->outputs[0]->filter,
                                       avfilter_get_by_name("buffersink"),
                                       "out", NULL, buffersink_params, fg->graph);
#endif
    av_freep(&buffersink_params);

    if (ret < 0)
        return ret;
    *in_filter  = fg->inputs[0]->filter;
    *out_filter = fg->outputs[0]->filter;

    if (codec->width || codec->height) {
        snprintf(args, 255, "%d:%d:flags=0x%X",
                 codec->width,
                 codec->height,
                 (unsigned)ost->sws_flags);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                NULL, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(*in_filter, 0, filter, 0)) < 0)
            return ret;
        *in_filter = filter;
    }

    if ((pix_fmts = choose_pix_fmts(ost))) {
        if ((ret = avfilter_graph_create_filter(&filter,
                                                avfilter_get_by_name("format"),
                                                "format", pix_fmts, NULL,
                                                fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(filter, 0, *out_filter, 0)) < 0)
            return ret;

        *out_filter = filter;
        av_freep(&pix_fmts);
    }

    snprintf(args, sizeof(args), "flags=0x%X", (unsigned)ost->sws_flags);
    fg->graph->scale_sws_opts = av_strdup(args);

    return 0;
}

static int configure_simple_filtergraph(FilterGraph *fg)
{
    OutputStream *ost = fg->outputs[0]->ost;
    AVFilterContext *in_filter, *out_filter;
    int ret;

    avfilter_graph_free(&fg->graph);
    fg->graph = avfilter_graph_alloc();
    if (!fg->graph)
        return AVERROR(ENOMEM);

    switch (ost->st->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        ret = configure_video_filters(fg, &in_filter, &out_filter);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ret = configure_audio_filters(fg, &in_filter, &out_filter);
        break;
    default: av_assert0(0);
    }
    if (ret < 0)
        return ret;

    if (ost->avfilter) {
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs  = avfilter_inout_alloc();

        outputs->name    = av_strdup("in");
        outputs->filter_ctx = in_filter;
        outputs->pad_idx = 0;
        outputs->next    = NULL;

        inputs->name    = av_strdup("out");
        inputs->filter_ctx = out_filter;
        inputs->pad_idx = 0;
        inputs->next    = NULL;

        if ((ret = avfilter_graph_parse(fg->graph, ost->avfilter, &inputs, &outputs, NULL)) < 0)
            return ret;
        av_freep(&ost->avfilter);
    } else {
        if ((ret = avfilter_link(in_filter, 0, out_filter, 0)) < 0)
            return ret;
    }

    if (ost->keep_pix_fmt)
        avfilter_graph_set_auto_convert(fg->graph,
                                        AVFILTER_AUTO_CONVERT_NONE);

    if ((ret = avfilter_graph_config(fg->graph, NULL)) < 0)
        return ret;

    ost->filter = fg->outputs[0];

    return 0;
}

static FilterGraph *init_simple_filtergraph(InputStream *ist, OutputStream *ost)
{
    FilterGraph *fg = av_mallocz(sizeof(*fg));

    if (!fg)
        exit_program(1);
    fg->index = nb_filtergraphs;

    fg->outputs = grow_array(fg->outputs, sizeof(*fg->outputs), &fg->nb_outputs,
                             fg->nb_outputs + 1);
    if (!(fg->outputs[0] = av_mallocz(sizeof(*fg->outputs[0]))))
        exit_program(1);
    fg->outputs[0]->ost   = ost;
    fg->outputs[0]->graph = fg;

    fg->inputs = grow_array(fg->inputs, sizeof(*fg->inputs), &fg->nb_inputs,
                            fg->nb_inputs + 1);
    if (!(fg->inputs[0] = av_mallocz(sizeof(*fg->inputs[0]))))
        exit_program(1);
    fg->inputs[0]->ist   = ist;
    fg->inputs[0]->graph = fg;

    ist->filters = grow_array(ist->filters, sizeof(*ist->filters),
                              &ist->nb_filters, ist->nb_filters + 1);
    ist->filters[ist->nb_filters - 1] = fg->inputs[0];

    filtergraphs = grow_array(filtergraphs, sizeof(*filtergraphs),
                              &nb_filtergraphs, nb_filtergraphs + 1);
    filtergraphs[nb_filtergraphs - 1] = fg;

    return fg;
}

static void init_input_filter(FilterGraph *fg, AVFilterInOut *in)
{
    InputStream *ist = NULL;
    enum AVMediaType type = in->filter_ctx->input_pads[in->pad_idx].type;
    int i;

    // TODO: support other filter types
    if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_FATAL, "Only video and audio filters supported "
               "currently.\n");
        exit_program(1);
    }

    if (in->name) {
        AVFormatContext *s;
        AVStream       *st = NULL;
        char *p;
        int file_idx = strtol(in->name, &p, 0);

        if (file_idx < 0 || file_idx >= nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "Invalid file index %d in filtergraph description %s.\n",
                   file_idx, fg->graph_desc);
            exit_program(1);
        }
        s = input_files[file_idx]->ctx;

        for (i = 0; i < s->nb_streams; i++) {
            if (s->streams[i]->codec->codec_type != type)
                continue;
            if (check_stream_specifier(s, s->streams[i], *p == ':' ? p + 1 : p) == 1) {
                st = s->streams[i];
                break;
            }
        }
        if (!st) {
            av_log(NULL, AV_LOG_FATAL, "Stream specifier '%s' in filtergraph description %s "
                   "matches no streams.\n", p, fg->graph_desc);
            exit_program(1);
        }
        ist = input_streams[input_files[file_idx]->ist_index + st->index];
    } else {
        /* find the first unused stream of corresponding type */
        for (i = 0; i < nb_input_streams; i++) {
            ist = input_streams[i];
            if (ist->st->codec->codec_type == type && ist->discard)
                break;
        }
        if (i == nb_input_streams) {
            av_log(NULL, AV_LOG_FATAL, "Cannot find a matching stream for "
                   "unlabeled input pad %d on filter %s", in->pad_idx,
                   in->filter_ctx->name);
            exit_program(1);
        }
    }
    ist->discard         = 0;
    ist->decoding_needed = 1;
    ist->st->discard = AVDISCARD_NONE;

    fg->inputs = grow_array(fg->inputs, sizeof(*fg->inputs),
                            &fg->nb_inputs, fg->nb_inputs + 1);
    if (!(fg->inputs[fg->nb_inputs - 1] = av_mallocz(sizeof(*fg->inputs[0]))))
        exit_program(1);
    fg->inputs[fg->nb_inputs - 1]->ist   = ist;
    fg->inputs[fg->nb_inputs - 1]->graph = fg;

    ist->filters = grow_array(ist->filters, sizeof(*ist->filters),
                              &ist->nb_filters, ist->nb_filters + 1);
    ist->filters[ist->nb_filters - 1] = fg->inputs[fg->nb_inputs - 1];
}

static int configure_output_video_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    char *pix_fmts;
    AVCodecContext *codec = ofilter->ost->st->codec;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    int ret;
    AVBufferSinkParams *buffersink_params = av_buffersink_params_alloc();

#if FF_API_OLD_VSINK_API
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("buffersink"),
                                       "out", NULL, NULL, fg->graph);
#else
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("buffersink"),
                                       "out", NULL, buffersink_params, fg->graph);
#endif
    av_freep(&buffersink_params);

    if (ret < 0)
        return ret;

    if (codec->width || codec->height) {
        char args[255];
        AVFilterContext *filter;

        snprintf(args, sizeof(args), "%d:%d:flags=0x%X",
                 codec->width,
                 codec->height,
                 (unsigned)ofilter->ost->sws_flags);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                NULL, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    if ((pix_fmts = choose_pix_fmts(ofilter->ost))) {
        AVFilterContext *filter;
        if ((ret = avfilter_graph_create_filter(&filter,
                                                avfilter_get_by_name("format"),
                                                "format", pix_fmts, NULL,
                                                fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx     = 0;
        av_freep(&pix_fmts);
    }

    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_audio_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputStream *ost = ofilter->ost;
    AVCodecContext *codec  = ost->st->codec;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    char *sample_fmts, *sample_rates, *channel_layouts;
    int ret;

    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("abuffersink"),
                                       "out", NULL, NULL, fg->graph);
    if (ret < 0)
        return ret;

    if (codec->channels && !codec->channel_layout)
        codec->channel_layout = av_get_default_channel_layout(codec->channels);

    sample_fmts     = choose_sample_fmts(ost);
    sample_rates    = choose_sample_rates(ost);
    channel_layouts = choose_channel_layouts(ost);
    if (sample_fmts || sample_rates || channel_layouts) {
        AVFilterContext *format;
        char args[256];
        int len = 0;

        if (sample_fmts)
            len += snprintf(args + len, sizeof(args) - len, "sample_fmts=%s:",
                            sample_fmts);
        if (sample_rates)
            len += snprintf(args + len, sizeof(args) - len, "sample_rates=%s:",
                            sample_rates);
        if (channel_layouts)
            len += snprintf(args + len, sizeof(args) - len, "channel_layouts=%s:",
                            channel_layouts);
        args[len - 1] = 0;

        av_freep(&sample_fmts);
        av_freep(&sample_rates);
        av_freep(&channel_layouts);

        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           "aformat", args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, format, 0);
        if (ret < 0)
            return ret;

        last_filter = format;
        pad_idx = 0;
    }

    if (audio_sync_method > 0) {
        AVFilterContext *async;
        char args[256];
        int  len = 0;

        av_log(NULL, AV_LOG_WARNING, "-async has been deprecated. Used the "
               "asyncts audio filter instead.\n");

        if (audio_sync_method > 1)
            len += snprintf(args + len, sizeof(args) - len, "compensate=1:"
                            "max_comp=%d:", audio_sync_method);
        snprintf(args + len, sizeof(args) - len, "min_delta=%f",
                 audio_drift_threshold);

        ret = avfilter_graph_create_filter(&async,
                                           avfilter_get_by_name("asyncts"),
                                           "async", args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, async, 0);
        if (ret < 0)
            return ret;

        last_filter = async;
        pad_idx = 0;
    }

    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    switch (out->filter_ctx->output_pads[out->pad_idx].type) {
    case AVMEDIA_TYPE_VIDEO: return configure_output_video_filter(fg, ofilter, out);
    case AVMEDIA_TYPE_AUDIO: return configure_output_audio_filter(fg, ofilter, out);
    default: av_assert0(0);
    }
}

static int configure_complex_filter(FilterGraph *fg)
{
    AVFilterInOut *inputs, *outputs, *cur;
    int ret, i, init = !fg->graph;

    avfilter_graph_free(&fg->graph);
    if (!(fg->graph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    if ((ret = avfilter_graph_parse2(fg->graph, fg->graph_desc, &inputs, &outputs)) < 0)
        return ret;

    for (cur = inputs; init && cur; cur = cur->next)
        init_input_filter(fg, cur);

    for (cur = inputs, i = 0; cur; cur = cur->next, i++) {
        InputFilter *ifilter = fg->inputs[i];
        InputStream     *ist = ifilter->ist;
        AVRational       sar;
        AVFilter     *filter;
        char            args[255];

        switch (cur->filter_ctx->input_pads[cur->pad_idx].type) {
        case AVMEDIA_TYPE_VIDEO:
            sar = ist->st->sample_aspect_ratio.num ?
                  ist->st->sample_aspect_ratio :
                  ist->st->codec->sample_aspect_ratio;
            snprintf(args, sizeof(args), "%d:%d:%d:%d:%d:%d:%d", ist->st->codec->width,
                     ist->st->codec->height, ist->st->codec->pix_fmt,
                     ist->st->time_base.num, ist->st->time_base.den,
                     sar.num, sar.den);
            filter = avfilter_get_by_name("buffer");
            break;
        case AVMEDIA_TYPE_AUDIO:
            snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:"
                     "sample_fmt=%s:channel_layout=0x%"PRIx64,
                     ist->st->time_base.num, ist->st->time_base.den,
                     ist->st->codec->sample_rate,
                     av_get_sample_fmt_name(ist->st->codec->sample_fmt),
                     ist->st->codec->channel_layout);
            filter = avfilter_get_by_name("abuffer");
            break;
        default:
            av_assert0(0);
        }

        if ((ret = avfilter_graph_create_filter(&ifilter->filter,
                                                filter, cur->name,
                                                args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(ifilter->filter, 0,
                                 cur->filter_ctx, cur->pad_idx)) < 0)
            return ret;
    }
    avfilter_inout_free(&inputs);

    if (!init) {
        /* we already know the mappings between lavfi outputs and output streams,
         * so we can finish the setup */
        for (cur = outputs, i = 0; cur; cur = cur->next, i++)
            configure_output_filter(fg, fg->outputs[i], cur);
        avfilter_inout_free(&outputs);

        if ((ret = avfilter_graph_config(fg->graph, NULL)) < 0)
            return ret;
    } else {
        /* wait until output mappings are processed */
        for (cur = outputs; cur;) {
            fg->outputs = grow_array(fg->outputs, sizeof(*fg->outputs),
                                     &fg->nb_outputs, fg->nb_outputs + 1);
            if (!(fg->outputs[fg->nb_outputs - 1] = av_mallocz(sizeof(*fg->outputs[0]))))
                exit_program(1);
            fg->outputs[fg->nb_outputs - 1]->graph   = fg;
            fg->outputs[fg->nb_outputs - 1]->out_tmp = cur;
            cur = cur->next;
            fg->outputs[fg->nb_outputs - 1]->out_tmp->next = NULL;
        }
    }

    return 0;
}

static int configure_complex_filters(void)
{
    int i, ret = 0;

    for (i = 0; i < nb_filtergraphs; i++)
        if (!filtergraphs[i]->graph &&
            (ret = configure_complex_filter(filtergraphs[i])) < 0)
            return ret;
    return 0;
}

static int configure_filtergraph(FilterGraph *fg)
{
    return fg->graph_desc ? configure_complex_filter(fg) :
                            configure_simple_filtergraph(fg);
}

static int ist_in_filtergraph(FilterGraph *fg, InputStream *ist)
{
    int i;
    for (i = 0; i < fg->nb_inputs; i++)
        if (fg->inputs[i]->ist == ist)
            return 1;
    return 0;
}

static void term_exit(void)
{
    av_log(NULL, AV_LOG_QUIET, "%s", "");
#if HAVE_TERMIOS_H
    if(restore_tty)
        tcsetattr (0, TCSANOW, &oldtty);
#endif
}

static volatile int received_sigterm = 0;

static void sigterm_handler(int sig)
{
    received_sigterm = sig;
    received_nb_signals++;
    term_exit();
    if(received_nb_signals > 3)
        exit(123);
}

static void term_init(void)
{
#if HAVE_TERMIOS_H
    if(!run_as_daemon){
        struct termios tty;
        int istty = 1;
#if HAVE_ISATTY
        istty = isatty(0) && isatty(2);
#endif
        if (istty && tcgetattr (0, &tty) == 0) {
            oldtty = tty;
            restore_tty = 1;
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
        }
        signal(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
    }
#endif
    avformat_network_deinit();

    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
#ifdef SIGXCPU
    signal(SIGXCPU, sigterm_handler);
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

    if (stdin->_cnt > 0) {
        read(0, &ch, 1);
        return ch;
    }
    if (is_pipe) {
        /* When running under a GUI, you will end here. */
        if (!PeekNamedPipe(input_handle, NULL, 0, NULL, &nchars, NULL))
            return -1;
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
    return received_nb_signals > 1;
}

static const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };

void av_noreturn exit_program(int ret)
{
    int i, j;

    for (i = 0; i < nb_filtergraphs; i++) {
        avfilter_graph_free(&filtergraphs[i]->graph);
        for (j = 0; j < filtergraphs[i]->nb_inputs; j++)
            av_freep(&filtergraphs[i]->inputs[j]);
        av_freep(&filtergraphs[i]->inputs);
        for (j = 0; j < filtergraphs[i]->nb_outputs; j++)
            av_freep(&filtergraphs[i]->outputs[j]);
        av_freep(&filtergraphs[i]->outputs);
        av_freep(&filtergraphs[i]);
    }
    av_freep(&filtergraphs);

    /* close files */
    for (i = 0; i < nb_output_files; i++) {
        AVFormatContext *s = output_files[i]->ctx;
        if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
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

        av_freep(&output_streams[i]->filtered_frame);
        av_freep(&output_streams[i]);
    }
    for (i = 0; i < nb_input_files; i++) {
        avformat_close_input(&input_files[i]->ctx);
        av_freep(&input_files[i]);
    }
    for (i = 0; i < nb_input_streams; i++) {
        av_freep(&input_streams[i]->decoded_frame);
        av_dict_free(&input_streams[i]->opts);
        free_buffer_pool(input_streams[i]);
        av_freep(&input_streams[i]->filters);
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

    avfilter_uninit();
    avformat_network_deinit();

    if (received_sigterm) {
        av_log(NULL, AV_LOG_INFO, "Received signal %d: terminating.\n",
               (int) received_sigterm);
        exit (255);
    }

    exit(ret);
}

static void assert_avoptions(AVDictionary *m)
{
    AVDictionaryEntry *t;
    if ((t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_FATAL, "Option %s not found.\n", t->key);
        exit_program(1);
    }
}

static void assert_codec_experimental(AVCodecContext *c, int encoder)
{
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;
    if (c->codec->capabilities & CODEC_CAP_EXPERIMENTAL &&
        c->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(NULL, AV_LOG_FATAL, "%s '%s' is experimental and might produce bad "
                "results.\nAdd '-strict experimental' if you want to use it.\n",
                codec_string, c->codec->name);
        codec = encoder ? avcodec_find_encoder(c->codec->id) : avcodec_find_decoder(c->codec->id);
        if (!(codec->capabilities & CODEC_CAP_EXPERIMENTAL))
            av_log(NULL, AV_LOG_FATAL, "Or use the non experimental %s '%s'.\n",
                   codec_string, codec->name);
        exit_program(1);
    }
}

static void choose_sample_fmt(AVStream *st, AVCodec *codec)
{
    if (codec && codec->sample_fmts) {
        const enum AVSampleFormat *p = codec->sample_fmts;
        for (; *p != -1; p++) {
            if (*p == st->codec->sample_fmt)
                break;
        }
        if (*p == -1) {
            if((codec->capabilities & CODEC_CAP_LOSSLESS) && av_get_sample_fmt_name(st->codec->sample_fmt) > av_get_sample_fmt_name(codec->sample_fmts[0]))
                av_log(NULL, AV_LOG_ERROR, "Conversion will not be lossless.\n");
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

static void write_frame(AVFormatContext *s, AVPacket *pkt, OutputStream *ost)
{
    AVBitStreamFilterContext *bsfc = ost->bitstream_filters;
    AVCodecContext          *avctx = ost->st->codec;
    int ret;

    if ((avctx->codec_type == AVMEDIA_TYPE_VIDEO && video_sync_method == VSYNC_DROP) ||
        (avctx->codec_type == AVMEDIA_TYPE_AUDIO && audio_sync_method < 0))
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;

    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && pkt->dts != AV_NOPTS_VALUE) {
        int64_t max = ost->st->cur_dts + !(s->oformat->flags & AVFMT_TS_NONSTRICT);
        if (ost->st->cur_dts && ost->st->cur_dts != AV_NOPTS_VALUE &&  max > pkt->dts) {
            av_log(s, max - pkt->dts > 2 ? AV_LOG_WARNING : AV_LOG_DEBUG, "Audio timestamp %"PRId64" < %"PRId64" invalid, cliping\n", pkt->dts, max);
            pkt->pts = pkt->dts = max;
        }
    }

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
            new_pkt.destruct = av_destruct_packet;
        } else if (a < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open bitstream filter %s for stream %d with codec %s",
                   bsfc->filter->name, pkt->stream_index,
                   avctx->codec ? avctx->codec->name : "copy");
            print_error("", a);
            if (exit_on_error)
                exit_program(1);
        }
        *pkt = new_pkt;

        bsfc = bsfc->next;
    }

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
        ost->is_past_recording_time = 1;
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
#if 0
    if (!check_recording_time(ost))
        return;
#endif
    if (frame->pts == AV_NOPTS_VALUE || audio_sync_method < 0)
        frame->pts = ost->sync_opts;
    ost->sync_opts = frame->pts + frame->nb_samples;

    av_assert0(pkt.size || !pkt.data);
    update_benchmark(NULL);
    if (avcodec_encode_audio2(enc, &pkt, frame, &got_packet) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Audio encoding failed (avcodec_encode_audio2)\n");
        exit_program(1);
    }
    update_benchmark("encode_audio %d.%d", ost->file_index, ost->index);

    if (got_packet) {
        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts      = av_rescale_q(pkt.pts,      enc->time_base, ost->st->time_base);
        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts      = av_rescale_q(pkt.dts,      enc->time_base, ost->st->time_base);
        if (pkt.duration > 0)
            pkt.duration = av_rescale_q(pkt.duration, enc->time_base, ost->st->time_base);

        if (debug_ts) {
            av_log(NULL, AV_LOG_INFO, "encoder -> type:audio "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                   av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->st->time_base),
                   av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->st->time_base));
        }

        write_frame(s, &pkt, ost);

        audio_size += pkt.size;
        av_free_packet(&pkt);
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
        buf  = av_malloc(size);
        if (!buf)
            return;

        picture2 = &picture_tmp;
        avpicture_fill(picture2, buf, dec->pix_fmt, dec->width, dec->height);

        if (avpicture_deinterlace(picture2, picture,
                                 dec->pix_fmt, dec->width, dec->height) < 0) {
            /* if error, do not deinterlace */
            av_log(NULL, AV_LOG_WARNING, "Deinterlacing failed\n");
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
    if (enc->codec_id == CODEC_ID_DVB_SUBTITLE)
        nb = 2;
    else
        nb = 1;

    for (i = 0; i < nb; i++) {
        ost->sync_opts = av_rescale_q(pts, ist->st->time_base, enc->time_base);

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
        if (enc->codec_id == CODEC_ID_DVB_SUBTITLE) {
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
                         float quality)
{
    int ret, format_video_sync;
    AVPacket pkt;
    AVCodecContext *enc = ost->st->codec;
    int nb_frames, i;
    double sync_ipts, delta;
    double duration = 0;
    int frame_size = 0;
    InputStream *ist = NULL;

    if (ost->source_index >= 0)
        ist = input_streams[ost->source_index];

    if(ist && ist->st->start_time != AV_NOPTS_VALUE && ist->st->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
        duration = 1/(av_q2d(ost->frame_rate) * av_q2d(enc->time_base));

    sync_ipts = in_picture->pts;
    delta = sync_ipts - ost->sync_opts + duration;

    /* by default, we output a single frame */
    nb_frames = 1;

    format_video_sync = video_sync_method;
    if (format_video_sync == VSYNC_AUTO)
        format_video_sync = (s->oformat->flags & AVFMT_VARIABLE_FPS) ? ((s->oformat->flags & AVFMT_NOTIMESTAMPS) ? VSYNC_PASSTHROUGH : VSYNC_VFR) : 1;

    switch (format_video_sync) {
    case VSYNC_CFR:
        // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (delta < -1.1)
            nb_frames = 0;
        else if (delta > 1.1)
            nb_frames = lrintf(delta);
        break;
    case VSYNC_VFR:
        if (delta <= -0.6)
            nb_frames = 0;
        else if (delta > 0.6)
            ost->sync_opts = lrint(sync_ipts);
        break;
    case VSYNC_DROP:
    case VSYNC_PASSTHROUGH:
        ost->sync_opts = lrint(sync_ipts);
        break;
    default:
        av_assert0(0);
    }

    nb_frames = FFMIN(nb_frames, ost->max_frames - ost->frame_number);
    if (nb_frames == 0) {
        nb_frames_drop++;
        av_log(NULL, AV_LOG_VERBOSE, "*** drop!\n");
        return;
    } else if (nb_frames > 1) {
        nb_frames_dup += nb_frames - 1;
        av_log(NULL, AV_LOG_VERBOSE, "*** %d dup!\n", nb_frames - 1);
    }


duplicate_frame:
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    in_picture->pts = ost->sync_opts;

    if (s->oformat->flags & AVFMT_RAWPICTURE &&
        enc->codec->id == CODEC_ID_RAWVIDEO) {
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
        AVFrame big_picture;

        big_picture = *in_picture;
        /* better than nothing: use input picture interlaced
           settings */
        big_picture.interlaced_frame = in_picture->interlaced_frame;
        if (ost->st->codec->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME)) {
            if (ost->top_field_first == -1)
                big_picture.top_field_first = in_picture->top_field_first;
            else
                big_picture.top_field_first = !!ost->top_field_first;
        }

        /* handles same_quant here. This is not correct because it may
           not be a global option */
        big_picture.quality = quality;
        if (!enc->me_threshold)
            big_picture.pict_type = 0;
        if (ost->forced_kf_index < ost->forced_kf_count &&
            big_picture.pts >= ost->forced_kf_pts[ost->forced_kf_index]) {
            big_picture.pict_type = AV_PICTURE_TYPE_I;
            ost->forced_kf_index++;
        }
        update_benchmark(NULL);
        ret = avcodec_encode_video2(enc, &pkt, &big_picture, &got_packet);
        update_benchmark("encode_video %d.%d", ost->file_index, ost->index);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
            exit_program(1);
        }

        if (got_packet) {
            if (pkt.pts == AV_NOPTS_VALUE && !(enc->codec->capabilities & CODEC_CAP_DELAY))
                pkt.pts = ost->sync_opts;

            if (pkt.pts != AV_NOPTS_VALUE)
                pkt.pts = av_rescale_q(pkt.pts, enc->time_base, ost->st->time_base);
            if (pkt.dts != AV_NOPTS_VALUE)
                pkt.dts = av_rescale_q(pkt.dts, enc->time_base, ost->st->time_base);

            if (debug_ts) {
                av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                    "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                    av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->st->time_base),
                    av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->st->time_base));
            }

            write_frame(s, &pkt, ost);
            frame_size = pkt.size;
            video_size += pkt.size;
            av_free_packet(&pkt);

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

    if(--nb_frames)
        goto duplicate_frame;

    if (vstats_filename && frame_size)
        do_video_stats(output_files[ost->file_index]->ctx, ost, frame_size);
}

static double psnr(double d)
{
    return -10.0 * log(d) / log(10.0);
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

/* check for new output on any of the filtergraphs */
static int poll_filters(void)
{
    AVFilterBufferRef *picref;
    AVFrame *filtered_frame = NULL;
    int i, ret, ret_all;
    unsigned nb_success, nb_eof;
    int64_t frame_pts;

    while (1) {
        /* Reap all buffers present in the buffer sinks */
        for (i = 0; i < nb_output_streams; i++) {
            OutputStream *ost = output_streams[i];
            OutputFile    *of = output_files[ost->file_index];
            int ret = 0;

            if (!ost->filter || ost->is_past_recording_time)
                continue;

            if (!ost->filtered_frame && !(ost->filtered_frame = avcodec_alloc_frame())) {
                return AVERROR(ENOMEM);
            } else
                avcodec_get_frame_defaults(ost->filtered_frame);
            filtered_frame = ost->filtered_frame;

            while (1) {
                AVRational ist_pts_tb = ost->filter->filter->inputs[0]->time_base;
                if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
                    !(ost->enc->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE))
                    ret = av_buffersink_read_samples(ost->filter->filter, &picref,
                                                    ost->st->codec->frame_size);
                else
#ifdef SINKA
                    ret = av_buffersink_read(ost->filter->filter, &picref);
#else
                    ret = av_buffersink_get_buffer_ref(ost->filter->filter, &picref,
                                                       AV_BUFFERSINK_FLAG_NO_REQUEST);
#endif
                if (ret < 0) {
                    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                        char buf[256];
                        av_strerror(ret, buf, sizeof(buf));
                        av_log(NULL, AV_LOG_WARNING,
                               "Error in av_buffersink_get_buffer_ref(): %s\n", buf);
                    }
                    break;
                }
                frame_pts = AV_NOPTS_VALUE;
                if (picref->pts != AV_NOPTS_VALUE) {
                    filtered_frame->pts = frame_pts = av_rescale_q(picref->pts,
                                                    ost->filter->filter->inputs[0]->time_base,
                                                    ost->st->codec->time_base) -
                                        av_rescale_q(of->start_time,
                                                    AV_TIME_BASE_Q,
                                                    ost->st->codec->time_base);

                    if (of->start_time && filtered_frame->pts < 0) {
                        avfilter_unref_buffer(picref);
                        continue;
                    }
                }
                //if (ost->source_index >= 0)
                //    *filtered_frame= *input_streams[ost->source_index]->decoded_frame; //for me_threshold


                switch (ost->filter->filter->inputs[0]->type) {
                case AVMEDIA_TYPE_VIDEO:
                    avfilter_fill_frame_from_video_buffer_ref(filtered_frame, picref);
                    filtered_frame->pts = frame_pts;
                    if (!ost->frame_aspect_ratio)
                        ost->st->codec->sample_aspect_ratio = picref->video->sample_aspect_ratio;

                    do_video_out(of->ctx, ost, filtered_frame,
                                 same_quant ? ost->last_quality :
                                              ost->st->codec->global_quality);
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    avfilter_copy_buf_props(filtered_frame, picref);
                    filtered_frame->pts = frame_pts;
                    do_audio_out(of->ctx, ost, filtered_frame);
                    break;
                default:
                    // TODO support subtitle filters
                    av_assert0(0);
                }

                avfilter_unref_buffer(picref);
            }
        }
        /* Request frames through all the graphs */
        ret_all = nb_success = nb_eof = 0;
        for (i = 0; i < nb_filtergraphs; i++) {
            ret = avfilter_graph_request_oldest(filtergraphs[i]->graph);
            if (!ret) {
                nb_success++;
            } else if (ret == AVERROR_EOF) {
                nb_eof++;
            } else if (ret != AVERROR(EAGAIN)) {
                char buf[256];
                av_strerror(ret, buf, sizeof(buf));
                av_log(NULL, AV_LOG_WARNING,
                       "Error in request_frame(): %s\n", buf);
                ret_all = ret;
            }
        }
        if (!nb_success)
            break;
        /* Try again if anything succeeded */
    }
    return nb_eof == nb_filtergraphs ? AVERROR_EOF : ret_all;
}

static void print_report(int is_last_report, int64_t timer_start, int64_t cur_time)
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
    int hours, mins, secs, us;

    if (!print_stats && !is_last_report)
        return;

    if (!is_last_report) {
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
    if (total_size < 0) { // FIXME improve avio_size() so it works with non seekable output too
        total_size = avio_tell(oc->pb);
        if (total_size < 0)
            total_size = 0;
    }

    buf[0] = '\0';
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
            float fps, t = (cur_time-timer_start) / 1000000.0;

            frame_number = ost->frame_number;
            fps = t > 1 ? frame_number / t : 0;
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "frame=%5d fps=%3.*f q=%3.1f ",
                     frame_number, fps < 9.95, fps, q);
            if (is_last_report)
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "L");
            if (qp_hist) {
                int j;
                int qp = lrintf(q);
                if (qp >= 0 && qp < FF_ARRAY_ELEMS(qp_histogram))
                    qp_histogram[qp]++;
                for (j = 0; j < 32; j++)
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%X", (int)lrintf(log(qp_histogram[j] + 1) / log(2)));
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
        pts = FFMIN(pts, av_rescale_q(ost->st->pts.val,
                                      ost->st->time_base, AV_TIME_BASE_Q));
    }

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

    av_log(NULL, AV_LOG_INFO, "%s    \r", buf);

    fflush(stderr);

    if (is_last_report) {
        int64_t raw= audio_size + video_size + extra_size;
        av_log(NULL, AV_LOG_INFO, "\n");
        av_log(NULL, AV_LOG_INFO, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
               video_size / 1024.0,
               audio_size / 1024.0,
               extra_size / 1024.0,
               100.0 * (total_size - raw) / raw
        );
        if(video_size + audio_size + extra_size == 0){
            av_log(NULL, AV_LOG_WARNING, "Output file is empty, nothing was encoded (check -ss / -t / -frames parameters if used)\n");
        }
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
        if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && (os->oformat->flags & AVFMT_RAWPICTURE) && enc->codec->id == CODEC_ID_RAWVIDEO)
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

                update_benchmark(NULL);
                ret = encode(enc, &pkt, NULL, &got_packet);
                update_benchmark("flush %s %d.%d", desc, ost->file_index, ost->index);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_FATAL, "%s encoding failed\n", desc);
                    exit_program(1);
                }
                *size += pkt.size;
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

    if (of->start_time && ist->pts < of->start_time)
        return 0;

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ist->pts, AV_TIME_BASE_Q, of->recording_time + of->start_time,
                      (AVRational){ 1, 1000000 }) >= 0) {
        ost->is_past_recording_time = 1;
        return 0;
    }

    return 1;
}

static void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
{
    OutputFile *of = output_files[ost->file_index];
    int64_t ost_tb_start_time = av_rescale_q(of->start_time, AV_TIME_BASE_Q, ost->st->time_base);
    AVPicture pict;
    AVPacket opkt;

    av_init_packet(&opkt);

    if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) &&
        !ost->copy_initial_nonkeyframes)
        return;

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
        opkt.dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ost->st->time_base);
    else
        opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->st->time_base);
    opkt.dts -= ost_tb_start_time;

    opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->st->time_base);
    opkt.flags    = pkt->flags;

    // FIXME remove the following 2 lines they shall be replaced by the bitstream filters
    if (  ost->st->codec->codec_id != CODEC_ID_H264
       && ost->st->codec->codec_id != CODEC_ID_MPEG1VIDEO
       && ost->st->codec->codec_id != CODEC_ID_MPEG2VIDEO
       && ost->st->codec->codec_id != CODEC_ID_VC1
       ) {
        if (av_parser_change(ist->st->parser, ost->st->codec, &opkt.data, &opkt.size, pkt->data, pkt->size, pkt->flags & AV_PKT_FLAG_KEY))
            opkt.destruct = av_destruct_packet;
    } else {
        opkt.data = pkt->data;
        opkt.size = pkt->size;
    }
    if (of->ctx->oformat->flags & AVFMT_RAWPICTURE) {
        /* store AVPicture in AVPacket, as expected by the output format */
        avpicture_fill(&pict, opkt.data, ost->st->codec->pix_fmt, ost->st->codec->width, ost->st->codec->height);
        opkt.data = (uint8_t *)&pict;
        opkt.size = sizeof(AVPicture);
        opkt.flags |= AV_PKT_FLAG_KEY;
    }

    write_frame(of->ctx, &opkt, ost);
    ost->st->codec->frame_number++;
    av_free_packet(&opkt);
}

static void rate_emu_sleep(InputStream *ist)
{
    if (input_files[ist->file_index]->rate_emu) {
        int64_t pts = av_rescale(ist->dts, 1000000, AV_TIME_BASE);
        int64_t now = av_gettime() - ist->start;
        if (pts > now)
            usleep(pts - now);
    }
}

static int guess_input_channel_layout(InputStream *ist)
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
    AVFrame *decoded_frame;
    AVCodecContext *avctx = ist->st->codec;
    int i, ret, resample_changed;

    if (!ist->decoded_frame && !(ist->decoded_frame = avcodec_alloc_frame()))
        return AVERROR(ENOMEM);
    else
        avcodec_get_frame_defaults(ist->decoded_frame);
    decoded_frame = ist->decoded_frame;

    update_benchmark(NULL);
    ret = avcodec_decode_audio4(avctx, decoded_frame, got_output, pkt);
    update_benchmark("decode_audio %d.%d", ist->file_index, ist->st->index);
    if (ret < 0) {
        return ret;
    }
    if (avctx->sample_rate <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d invalid\n", avctx->sample_rate);
        return AVERROR_INVALIDDATA;
    }

    if (!*got_output) {
        /* no audio frame */
        if (!pkt->size)
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_add_ref(ist->filters[i]->filter, NULL,
                                     AV_BUFFERSRC_FLAG_NO_COPY);
        return ret;
    }

    /* if the decoder provides a pts, use it instead of the last packet pts.
       the decoder could be delaying output by a packet or more. */
    if (decoded_frame->pts != AV_NOPTS_VALUE)
        ist->dts = ist->next_dts = ist->pts = ist->next_pts = decoded_frame->pts;
    else if (pkt->pts != AV_NOPTS_VALUE) {
        decoded_frame->pts = pkt->pts;
        pkt->pts           = AV_NOPTS_VALUE;
    }else
        decoded_frame->pts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ist->st->time_base);


#if 1
    /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
    ist->next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     avctx->sample_rate;
    ist->next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                     avctx->sample_rate;
#endif

    rate_emu_sleep(ist);

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

    for (i = 0; i < ist->nb_filters; i++)
        av_buffersrc_add_frame(ist->filters[i]->filter, decoded_frame, 0);

    return ret;
}

static int decode_video(InputStream *ist, AVPacket *pkt, int *got_output)
{
    AVFrame *decoded_frame;
    void *buffer_to_free = NULL;
    int i, ret = 0, resample_changed;
    int64_t best_effort_timestamp;
    AVRational *frame_sample_aspect;
    float quality;

    if (!ist->decoded_frame && !(ist->decoded_frame = avcodec_alloc_frame()))
        return AVERROR(ENOMEM);
    else
        avcodec_get_frame_defaults(ist->decoded_frame);
    decoded_frame = ist->decoded_frame;
    pkt->dts  = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ist->st->time_base);

    update_benchmark(NULL);
    ret = avcodec_decode_video2(ist->st->codec,
                                decoded_frame, got_output, pkt);
    update_benchmark("decode_video %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        return ret;

    quality = same_quant ? decoded_frame->quality : 0;
    if (!*got_output) {
        /* no picture yet */
        if (!pkt->size)
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_add_ref(ist->filters[i]->filter, NULL, AV_BUFFERSRC_FLAG_NO_COPY);
        return ret;
    }

    best_effort_timestamp= av_frame_get_best_effort_timestamp(decoded_frame);
    if(best_effort_timestamp != AV_NOPTS_VALUE)
        ist->next_pts = ist->pts = av_rescale_q(decoded_frame->pts = best_effort_timestamp, ist->st->time_base, AV_TIME_BASE_Q);

    pkt->size = 0;
    pre_process_video_frame(ist, (AVPicture *)decoded_frame, &buffer_to_free);

    rate_emu_sleep(ist);

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

    frame_sample_aspect= av_opt_ptr(avcodec_get_frame_class(), decoded_frame, "sample_aspect_ratio");
    for (i = 0; i < ist->nb_filters; i++) {
        int changed =      ist->st->codec->width   != ist->filters[i]->filter->outputs[0]->w
                        || ist->st->codec->height  != ist->filters[i]->filter->outputs[0]->h
                        || ist->st->codec->pix_fmt != ist->filters[i]->filter->outputs[0]->format;
        // XXX what an ugly hack
        if (ist->filters[i]->graph->nb_outputs == 1)
            ist->filters[i]->graph->outputs[0]->ost->last_quality = quality;

        if (!frame_sample_aspect->num)
            *frame_sample_aspect = ist->st->sample_aspect_ratio;
        if (ist->dr1 && decoded_frame->type==FF_BUFFER_TYPE_USER && !changed) {
            FrameBuffer      *buf = decoded_frame->opaque;
            AVFilterBufferRef *fb = avfilter_get_video_buffer_ref_from_arrays(
                                        decoded_frame->data, decoded_frame->linesize,
                                        AV_PERM_READ | AV_PERM_PRESERVE,
                                        ist->st->codec->width, ist->st->codec->height,
                                        ist->st->codec->pix_fmt);

            avfilter_copy_frame_props(fb, decoded_frame);
            fb->buf->priv           = buf;
            fb->buf->free           = filter_release_buffer;

            av_assert0(buf->refcount>0);
            buf->refcount++;
            av_buffersrc_add_ref(ist->filters[i]->filter, fb,
                                 AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT |
                                 AV_BUFFERSRC_FLAG_NO_COPY);
        } else
        if(av_buffersrc_add_frame(ist->filters[i]->filter, decoded_frame, 0)<0) {
            av_log(NULL, AV_LOG_FATAL, "Failed to inject frame into filter network\n");
            exit_program(1);
        }

    }

    av_free(buffer_to_free);
    return ret;
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

    rate_emu_sleep(ist);

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
    int ret = 0, i;
    int got_output;

    AVPacket avpkt;
    if (!ist->saw_first_ts) {
        ist->dts = ist->st->avg_frame_rate.num ? - ist->st->codec->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        ist->pts = 0;
        if (pkt != NULL && pkt->pts != AV_NOPTS_VALUE && !ist->decoding_needed) {
            ist->dts += av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
            ist->pts = ist->dts; //unused but better to set it to a value thats not totally wrong
        }
        ist->saw_first_ts = 1;
    }

    if (ist->next_dts == AV_NOPTS_VALUE)
        ist->next_dts = ist->dts;
    if (ist->next_pts == AV_NOPTS_VALUE)
        ist->next_pts = ist->pts;

    if (pkt == NULL) {
        /* EOF handling */
        av_init_packet(&avpkt);
        avpkt.data = NULL;
        avpkt.size = 0;
        goto handle_eof;
    } else {
        avpkt = *pkt;
    }

    if (pkt->dts != AV_NOPTS_VALUE) {
        ist->next_dts = ist->dts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
        if (ist->st->codec->codec_type != AVMEDIA_TYPE_VIDEO || !ist->decoding_needed)
            ist->next_pts = ist->pts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
    }

    // while we have more to decode or while the decoder did output something on EOF
    while (ist->decoding_needed && (avpkt.size > 0 || (!pkt && got_output))) {
        int duration;
    handle_eof:

        ist->pts = ist->next_pts;
        ist->dts = ist->next_dts;

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
            if (avpkt.duration) {
                duration = av_rescale_q(avpkt.duration, ist->st->time_base, AV_TIME_BASE_Q);
            } else if(ist->st->codec->time_base.num != 0 && ist->st->codec->time_base.den != 0) {
                int ticks= ist->st->parser ? ist->st->parser->repeat_pict+1 : ist->st->codec->ticks_per_frame;
                duration = ((int64_t)AV_TIME_BASE *
                                ist->st->codec->time_base.num * ticks) /
                                ist->st->codec->time_base.den;
            } else
                duration = 0;

            if(ist->dts != AV_NOPTS_VALUE && duration) {
                ist->next_dts += duration;
            }else
                ist->next_dts = AV_NOPTS_VALUE;

            if (got_output)
                ist->next_pts += duration; //FIXME the duration is not correct in some cases
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            ret = transcode_subtitles(ist, &avpkt, &got_output);
            break;
        default:
            return -1;
        }

        if (ret < 0)
            return ret;

        avpkt.dts=
        avpkt.pts= AV_NOPTS_VALUE;

        // touch data and size only if not EOF
        if (pkt) {
            if(ist->st->codec->codec_type != AVMEDIA_TYPE_AUDIO)
                ret = avpkt.size;
            avpkt.data += ret;
            avpkt.size -= ret;
        }
        if (!got_output) {
            continue;
        }
    }

    /* handle stream copy */
    if (!ist->decoding_needed) {
        rate_emu_sleep(ist);
        ist->dts = ist->next_dts;
        switch (ist->st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            ist->next_dts += ((int64_t)AV_TIME_BASE * ist->st->codec->frame_size) /
                             ist->st->codec->sample_rate;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (pkt->duration) {
                ist->next_dts += av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
            } else if(ist->st->codec->time_base.num != 0) {
                int ticks= ist->st->parser ? ist->st->parser->repeat_pict + 1 : ist->st->codec->ticks_per_frame;
                ist->next_dts += ((int64_t)AV_TIME_BASE *
                                  ist->st->codec->time_base.num * ticks) /
                                  ist->st->codec->time_base.den;
            }
            break;
        }
        ist->pts = ist->dts;
        ist->next_pts = ist->next_dts;
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
    char sdp[2048];
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

static int init_input_stream(int ist_index, char *error, int error_len)
{
    InputStream *ist = input_streams[ist_index];

    if (ist->decoding_needed) {
        AVCodec *codec = ist->dec;
        if (!codec) {
            snprintf(error, error_len, "Decoder (codec %s) not found for input stream #%d:%d",
                    avcodec_get_name(ist->st->codec->codec_id), ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        ist->dr1 = (codec->capabilities & CODEC_CAP_DR1) && !do_deinterlace;
        if (codec->type == AVMEDIA_TYPE_VIDEO && ist->dr1) {
            ist->st->codec->get_buffer     = codec_get_buffer;
            ist->st->codec->release_buffer = codec_release_buffer;
            ist->st->codec->opaque         = ist;
        }

        if (!av_dict_get(ist->opts, "threads", NULL, 0))
            av_dict_set(&ist->opts, "threads", "auto", 0);
        if (avcodec_open2(ist->st->codec, codec, &ist->opts) < 0) {
            snprintf(error, error_len, "Error while opening decoder for input stream #%d:%d",
                    ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }
        assert_codec_experimental(ist->st->codec, 0);
        assert_avoptions(ist->opts);
    }

    ist->next_pts = AV_NOPTS_VALUE;
    ist->next_dts = AV_NOPTS_VALUE;
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

static int transcode_init(void)
{
    int ret = 0, i, j, k;
    AVFormatContext *oc;
    AVCodecContext *codec, *icodec;
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
            codec->extradata_size= icodec->extradata_size;
            codec->bits_per_coded_sample  = icodec->bits_per_coded_sample;

            codec->time_base = ist->st->time_base;
            /*
             * Avi is a special case here because it supports variable fps but
             * having the fps and timebase differe significantly adds quite some
             * overhead
             */
            if(!strcmp(oc->oformat->name, "avi")) {
                if (   copy_tb<0 && av_q2d(icodec->time_base)*icodec->ticks_per_frame > 2*av_q2d(ist->st->time_base)
                                 && av_q2d(ist->st->time_base) < 1.0/500
                    || copy_tb==0){
                    codec->time_base = icodec->time_base;
                    codec->time_base.num *= icodec->ticks_per_frame;
                    codec->time_base.den *= 2;
                    codec->ticks_per_frame = 2;
                }
            } else if(!(oc->oformat->flags & AVFMT_VARIABLE_FPS)
                      && strcmp(oc->oformat->name, "mov") && strcmp(oc->oformat->name, "mp4") && strcmp(oc->oformat->name, "3gp")
                      && strcmp(oc->oformat->name, "3g2") && strcmp(oc->oformat->name, "psp") && strcmp(oc->oformat->name, "ipod")
            ) {
                if(   copy_tb<0 && av_q2d(icodec->time_base)*icodec->ticks_per_frame > av_q2d(ist->st->time_base)
                                && av_q2d(ist->st->time_base) < 1.0/500
                   || copy_tb==0){
                    codec->time_base = icodec->time_base;
                    codec->time_base.num *= icodec->ticks_per_frame;
                }
            }
            av_reduce(&codec->time_base.num, &codec->time_base.den,
                        codec->time_base.num, codec->time_base.den, INT_MAX);

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
                if(codec->block_align == 1 && codec->codec_id == CODEC_ID_MP3)
                    codec->block_align= 0;
                if(codec->codec_id == CODEC_ID_AC3)
                    codec->block_align= 0;
                break;
            case AVMEDIA_TYPE_VIDEO:
                codec->pix_fmt            = icodec->pix_fmt;
                codec->width              = icodec->width;
                codec->height             = icodec->height;
                codec->has_b_frames       = icodec->has_b_frames;
                if (!codec->sample_aspect_ratio.num) {
                    codec->sample_aspect_ratio   =
                    ost->st->sample_aspect_ratio =
                        ist->st->sample_aspect_ratio.num ? ist->st->sample_aspect_ratio :
                        ist->st->codec->sample_aspect_ratio.num ?
                        ist->st->codec->sample_aspect_ratio : (AVRational){0, 1};
                }
                ost->st->avg_frame_rate = ist->st->avg_frame_rate;
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
            if (!ost->enc)
                ost->enc = avcodec_find_encoder(codec->codec_id);
            if (!ost->enc) {
                /* should only happen when a default codec is not present. */
                snprintf(error, sizeof(error), "Encoder (codec %s) not found for output stream #%d:%d",
                         avcodec_get_name(ost->st->codec->codec_id), ost->file_index, ost->index);
                ret = AVERROR(EINVAL);
                goto dump_format;
            }

            if (ist)
                ist->decoding_needed = 1;
            ost->encoding_needed = 1;

            if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                if (ist && !ost->frame_rate.num)
                    ost->frame_rate = ist->st->r_frame_rate.num ? ist->st->r_frame_rate : (AVRational){25, 1};
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
                    if (configure_simple_filtergraph(fg)) {
                        av_log(NULL, AV_LOG_FATAL, "Error opening filters!\n");
                        exit(1);
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
                codec->time_base = (AVRational){ost->frame_rate.den, ost->frame_rate.num};
                if (   av_q2d(codec->time_base) < 0.001 && video_sync_method != VSYNC_PASSTHROUGH
                   && (video_sync_method == VSYNC_CFR || (video_sync_method == VSYNC_AUTO && !(oc->oformat->flags & AVFMT_VARIABLE_FPS)))){
                    av_log(oc, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                               "Please consider specifying a lower framerate, a different muxer or -vsync 2\n");
                }
                for (j = 0; j < ost->forced_kf_count; j++)
                    ost->forced_kf_pts[j] = av_rescale_q(ost->forced_kf_pts[j],
                                                         AV_TIME_BASE_Q,
                                                         codec->time_base);

                codec->width  = ost->filter->filter->inputs[0]->w;
                codec->height = ost->filter->filter->inputs[0]->h;
                codec->sample_aspect_ratio = ost->st->sample_aspect_ratio =
                    ost->frame_aspect_ratio ? // overridden by the -aspect cli option
                    av_d2q(ost->frame_aspect_ratio * codec->height/codec->width, 255) :
                    ost->filter->filter->inputs[0]->sample_aspect_ratio;
                codec->pix_fmt = ost->filter->filter->inputs[0]->format;

                if (codec->width   != icodec->width  ||
                    codec->height  != icodec->height ||
                    codec->pix_fmt != icodec->pix_fmt) {
                    codec->bits_per_raw_sample = frame_bits_per_raw_sample;
                }

                break;
            case AVMEDIA_TYPE_SUBTITLE:
                codec->time_base = (AVRational){1, 1000};
                break;
            default:
                abort();
                break;
            }
            /* two pass mode */
            if (codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2)) {
                char logfilename[1024];
                FILE *f;

                snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                         pass_logfilename_prefix ? pass_logfilename_prefix : DEFAULT_PASS_LOGFILENAME_PREFIX,
                         i);
                if (!strcmp(ost->enc->name, "libx264")) {
                    av_dict_set(&ost->opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
                } else {
                    if (codec->flags & CODEC_FLAG_PASS2) {
                        char  *logbuffer;
                        size_t logbuffer_size;
                        if (cmdutils_read_file(logfilename, &logbuffer, &logbuffer_size) < 0) {
                            av_log(NULL, AV_LOG_FATAL, "Error reading log file '%s' for pass-2 encoding\n",
                                   logfilename);
                            exit_program(1);
                        }
                        codec->stats_in = logbuffer;
                    }
                    if (codec->flags & CODEC_FLAG_PASS1) {
                        f = fopen(logfilename, "wb");
                        if (!f) {
                            av_log(NULL, AV_LOG_FATAL, "Cannot write log file '%s' for pass-1 encoding: %s\n",
                                logfilename, strerror(errno));
                            exit_program(1);
                        }
                        ost->logfile = f;
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
            if (avcodec_open2(ost->st->codec, codec, &ost->opts) < 0) {
                snprintf(error, sizeof(error), "Error while opening encoder for output stream #%d:%d - maybe incorrect parameters such as bit_rate, rate, width or height",
                        ost->file_index, ost->index);
                ret = AVERROR(EINVAL);
                goto dump_format;
            }
            assert_codec_experimental(ost->st->codec, 1);
            assert_avoptions(ost->opts);
            if (ost->st->codec->bit_rate && ost->st->codec->bit_rate < 1000)
                av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                             " It takes bits/s as argument, not kbits/s\n");
            extra_size += ost->st->codec->extradata_size;

            if (ost->st->codec->me_threshold)
                input_streams[ost->source_index]->st->codec->debug |= FF_DEBUG_MV;
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
        if (avformat_write_header(oc, &output_files[i]->opts) < 0) {
            snprintf(error, sizeof(error), "Could not write header for output file #%d (incorrect codec parameters ?)", i);
            ret = AVERROR(EINVAL);
            goto dump_format;
        }
//         assert_avoptions(output_files[i]->opts);
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
            AVFilterLink *link = ist->filters[j]->filter->outputs[0];
            if (ist->filters[j]->graph->graph_desc) {
                av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d (%s) -> %s",
                       ist->file_index, ist->st->index, ist->dec ? ist->dec->name : "?",
                       link->dst->filter->name);
                if (link->dst->input_count > 1)
                    av_log(NULL, AV_LOG_INFO, ":%s", link->dstpad->name);
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
            AVFilterLink *link = ost->filter->filter->inputs[0];
            av_log(NULL, AV_LOG_INFO, "  %s", link->src->filter->name);
            if (link->src->output_count > 1)
                av_log(NULL, AV_LOG_INFO, ":%s", link->srcpad->name);
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

/*
 * The following code is the main loop of the file converter
 */
static int transcode(void)
{
    int ret, i;
    AVFormatContext *is, *os;
    OutputStream *ost;
    InputStream *ist;
    uint8_t *no_packet;
    int no_packet_count = 0;
    int64_t timer_start;
    int key;

    if (!(no_packet = av_mallocz(nb_input_files)))
        exit_program(1);

    ret = transcode_init();
    if (ret < 0)
        goto fail;

    if (!using_stdin) {
        av_log(NULL, AV_LOG_INFO, "Press [q] to stop, [?] for help\n");
    }

    timer_start = av_gettime();

    for (; received_sigterm == 0;) {
        int file_index, ist_index, past_recording_time = 1;
        AVPacket pkt;
        int64_t ipts_min;
        int64_t cur_time= av_gettime();

        ipts_min = INT64_MAX;
        /* if 'q' pressed, exits */
        if (!using_stdin) {
            static int64_t last_time;
            if (received_nb_signals)
                break;
            /* read_key() returns 0 on EOF */
            if(cur_time - last_time >= 100000 && !run_as_daemon){
                key =  read_key();
                last_time = cur_time;
            }else
                key = -1;
            if (key == 'q')
                break;
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
                fprintf(stderr, "\nEnter command: <target> <time> <command>[ <argument>]\n");
                i = 0;
                while ((k = read_key()) != '\n' && k != '\r' && i < sizeof(buf)-1)
                    if (k > 0)
                        buf[i++] = k;
                buf[i] = 0;
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
                                fprintf(stderr, "Command reply for stream %d: ret:%d res:%s\n", i, ret, buf);
                            } else {
                                ret = avfilter_graph_queue_command(fg->graph, target, command, arg, 0, time);
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
                    debug = input_streams[0]->st->codec->debug<<1;
                    if(!debug) debug = 1;
                    while(debug & (FF_DEBUG_DCT_COEFF|FF_DEBUG_VIS_QP|FF_DEBUG_VIS_MB_TYPE)) //unsupported, would just crash
                        debug += debug;
                }else
                    if(scanf("%d", &debug)!=1)
                        fprintf(stderr,"error parsing debug value\n");
                for(i=0;i<nb_input_streams;i++) {
                    input_streams[i]->st->codec->debug = debug;
                }
                for(i=0;i<nb_output_streams;i++) {
                    ost = output_streams[i];
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
                                "c      Send command to filtergraph\n"
                                "D      cycle through available debug modes\n"
                                "h      dump packets/hex press to cycle through the 3 states\n"
                                "q      quit\n"
                                "s      Show QP histogram\n"
                );
            }
        }

        /* check if there's any stream where output is still needed */
        for (i = 0; i < nb_output_streams; i++) {
            OutputFile *of;
            ost = output_streams[i];
            of  = output_files[ost->file_index];
            os  = output_files[ost->file_index]->ctx;
            if (ost->is_past_recording_time ||
                (os->pb && avio_tell(os->pb) >= of->limit_filesize))
                continue;
            if (ost->frame_number >= ost->max_frames) {
                int j;
                for (j = 0; j < of->ctx->nb_streams; j++)
                    output_streams[of->ost_index + j]->is_past_recording_time = 1;
                continue;
            }
            past_recording_time = 0;
        }
        if (past_recording_time)
            break;

        /* select the stream that we must read now by looking at the
           smallest output pts */
        file_index = -1;
        for (i = 0; i < nb_input_streams; i++) {
            int64_t ipts;
            ist = input_streams[i];
            ipts = ist->pts;
            if (ist->discard || no_packet[ist->file_index])
                continue;
            if (!input_files[ist->file_index]->eof_reached) {
                if (ipts < ipts_min) {
                    ipts_min = ipts;
                    file_index = ist->file_index;
                }
            }
        }
        /* if none, if is finished */
        if (file_index < 0) {
            if (no_packet_count) {
                no_packet_count = 0;
                memset(no_packet, 0, nb_input_files);
                usleep(10000);
                continue;
            }
            break;
        }

        /* read a frame from it and output it in the fifo */
        is  = input_files[file_index]->ctx;
        ret = av_read_frame(is, &pkt);
        if (ret == AVERROR(EAGAIN)) {
            no_packet[file_index] = 1;
            no_packet_count++;
            continue;
        }
        if (ret < 0) {
            input_files[file_index]->eof_reached = 1;

            for (i = 0; i < input_files[file_index]->nb_streams; i++) {
                ist = input_streams[input_files[file_index]->ist_index + i];
                if (ist->decoding_needed)
                    output_packet(ist, NULL);
            }

            if (opt_shortest)
                break;
            else
                continue;
        }

        no_packet_count = 0;
        memset(no_packet, 0, nb_input_files);

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_DEBUG, &pkt, do_hex_dump,
                             is->streams[pkt.stream_index]);
        }
        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt.stream_index >= input_files[file_index]->nb_streams)
            goto discard_packet;
        ist_index = input_files[file_index]->ist_index + pkt.stream_index;
        ist = input_streams[ist_index];
        if (ist->discard)
            goto discard_packet;

        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts += av_rescale_q(input_files[ist->file_index]->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);
        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts += av_rescale_q(input_files[ist->file_index]->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);

        if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts *= ist->ts_scale;
        if (pkt.dts != AV_NOPTS_VALUE)
            pkt.dts *= ist->ts_scale;

        if (debug_ts) {
            av_log(NULL, AV_LOG_INFO, "demuxer -> ist_index:%d type:%s "
                    "next_dts:%s next_dts_time:%s next_pts:%s next_pts_time:%s  pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%"PRId64"\n",
                    ist_index, av_get_media_type_string(ist->st->codec->codec_type),
                    av_ts2str(ist->next_dts), av_ts2timestr(ist->next_dts, &ist->st->time_base),
                    av_ts2str(ist->next_pts), av_ts2timestr(ist->next_pts, &ist->st->time_base),
                    av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ist->st->time_base),
                    av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ist->st->time_base),
                    input_files[ist->file_index]->ts_offset);
        }

        if (pkt.dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE && !copy_ts) {
            int64_t pkt_dts = av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);
            int64_t delta   = pkt_dts - ist->next_dts;
            if (is->iformat->flags & AVFMT_TS_DISCONT) {
            if(delta < -1LL*dts_delta_threshold*AV_TIME_BASE ||
                (delta > 1LL*dts_delta_threshold*AV_TIME_BASE &&
                 ist->st->codec->codec_type != AVMEDIA_TYPE_SUBTITLE) ||
                pkt_dts+1<ist->pts){
                input_files[ist->file_index]->ts_offset -= delta;
                av_log(NULL, AV_LOG_DEBUG,
                       "timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                       delta, input_files[ist->file_index]->ts_offset);
                pkt.dts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if (pkt.pts != AV_NOPTS_VALUE)
                    pkt.pts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
            } else {
                if ( delta < -1LL*dts_error_threshold*AV_TIME_BASE ||
                    (delta > 1LL*dts_error_threshold*AV_TIME_BASE && ist->st->codec->codec_type != AVMEDIA_TYPE_SUBTITLE) ||
                     pkt_dts+1<ist->pts){
                    av_log(NULL, AV_LOG_WARNING, "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n", pkt.dts, ist->next_dts, pkt.stream_index);
                    pkt.dts = AV_NOPTS_VALUE;
                }
                if (pkt.pts != AV_NOPTS_VALUE){
                    int64_t pkt_pts = av_rescale_q(pkt.pts, ist->st->time_base, AV_TIME_BASE_Q);
                    delta   = pkt_pts - ist->next_dts;
                    if ( delta < -1LL*dts_error_threshold*AV_TIME_BASE ||
                        (delta > 1LL*dts_error_threshold*AV_TIME_BASE && ist->st->codec->codec_type != AVMEDIA_TYPE_SUBTITLE) ||
                        pkt_pts+1<ist->pts) {
                        av_log(NULL, AV_LOG_WARNING, "PTS %"PRId64", next:%"PRId64" invalid dropping st:%d\n", pkt.pts, ist->next_dts, pkt.stream_index);
                        pkt.pts = AV_NOPTS_VALUE;
                    }
                }
            }
        }

        // fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->st->index, pkt.size);
        if (output_packet(ist, &pkt) < 0 ||
            ((ret = poll_filters()) < 0 && ret != AVERROR_EOF)) {
            av_log(NULL, AV_LOG_ERROR, "Error while decoding stream #%d:%d\n",
                   ist->file_index, ist->st->index);
            if (exit_on_error)
                exit_program(1);
            av_free_packet(&pkt);
            continue;
        }

    discard_packet:
        av_free_packet(&pkt);

        /* dump report by using the output first video and audio streams */
        print_report(0, timer_start, cur_time);
    }

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
    print_report(1, timer_start, av_gettime());

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
        }
    }

    /* finished ! */
    ret = 0;

 fail:
    av_freep(&no_packet);

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
            }
        }
    }
    return ret;
}

static int opt_frame_crop(const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_FATAL, "Option '%s' has been removed, use the crop filter instead\n", opt);
    return AVERROR(EINVAL);
}

static int opt_pad(const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_FATAL, "Option '%s' has been removed, use the pad filter instead\n", opt);
    return -1;
}

static int opt_video_channel(const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "This option is deprecated, use -channel.\n");
    return opt_default("channel", arg);
}

static int opt_video_standard(const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "This option is deprecated, use -standard.\n");
    return opt_default("standard", arg);
}

static int opt_audio_codec(OptionsContext *o, const char *opt, const char *arg)
{
    audio_codec_name = arg;
    return parse_option(o, "codec:a", arg, options);
}

static int opt_video_codec(OptionsContext *o, const char *opt, const char *arg)
{
    video_codec_name = arg;
    return parse_option(o, "codec:v", arg, options);
}

static int opt_subtitle_codec(OptionsContext *o, const char *opt, const char *arg)
{
    subtitle_codec_name = arg;
    return parse_option(o, "codec:s", arg, options);
}

static int opt_data_codec(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "codec:d", arg, options);
}

static int opt_map(OptionsContext *o, const char *opt, const char *arg)
{
    StreamMap *m = NULL;
    int i, negative = 0, file_idx;
    int sync_file_idx = -1, sync_stream_idx = 0;
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
            av_log(NULL, AV_LOG_FATAL, "Invalid sync file index: %d.\n", sync_file_idx);
            exit_program(1);
        }
        if (*sync)
            sync++;
        for (i = 0; i < input_files[sync_file_idx]->nb_streams; i++)
            if (check_stream_specifier(input_files[sync_file_idx]->ctx,
                                       input_files[sync_file_idx]->ctx->streams[i], sync) == 1) {
                sync_stream_idx = i;
                break;
            }
        if (i == input_files[sync_file_idx]->nb_streams) {
            av_log(NULL, AV_LOG_FATAL, "Sync stream specification in map %s does not "
                                       "match any streams.\n", arg);
            exit_program(1);
        }
    }


    if (map[0] == '[') {
        /* this mapping refers to lavfi output */
        const char *c = map + 1;
        o->stream_maps = grow_array(o->stream_maps, sizeof(*o->stream_maps),
                                    &o->nb_stream_maps, o->nb_stream_maps + 1);
        m = &o->stream_maps[o->nb_stream_maps - 1];
        m->linklabel = av_get_token(&c, "]");
        if (!m->linklabel) {
            av_log(NULL, AV_LOG_ERROR, "Invalid output link label: %s.\n", map);
            exit_program(1);
        }
    } else {
        file_idx = strtol(map, &p, 0);
        if (file_idx >= nb_input_files || file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index: %d.\n", file_idx);
            exit_program(1);
        }
        if (negative)
            /* disable some already defined maps */
            for (i = 0; i < o->nb_stream_maps; i++) {
                m = &o->stream_maps[i];
                if (file_idx == m->file_index &&
                    check_stream_specifier(input_files[m->file_index]->ctx,
                                           input_files[m->file_index]->ctx->streams[m->stream_index],
                                           *p == ':' ? p + 1 : p) > 0)
                    m->disabled = 1;
            }
        else
            for (i = 0; i < input_files[file_idx]->nb_streams; i++) {
                if (check_stream_specifier(input_files[file_idx]->ctx, input_files[file_idx]->ctx->streams[i],
                            *p == ':' ? p + 1 : p) <= 0)
                    continue;
                o->stream_maps = grow_array(o->stream_maps, sizeof(*o->stream_maps),
                                            &o->nb_stream_maps, o->nb_stream_maps + 1);
                m = &o->stream_maps[o->nb_stream_maps - 1];

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
    }

    if (!m) {
        av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches no streams.\n", arg);
        exit_program(1);
    }

    av_freep(&map);
    return 0;
}

static int opt_attach(OptionsContext *o, const char *opt, const char *arg)
{
    o->attachments = grow_array(o->attachments, sizeof(*o->attachments),
                                &o->nb_attachments, o->nb_attachments + 1);
    o->attachments[o->nb_attachments - 1] = arg;
    return 0;
}

static int opt_map_channel(OptionsContext *o, const char *opt, const char *arg)
{
    int n;
    AVStream *st;
    AudioChannelMap *m;

    o->audio_channel_maps =
        grow_array(o->audio_channel_maps, sizeof(*o->audio_channel_maps),
                   &o->nb_audio_channel_maps, o->nb_audio_channel_maps + 1);
    m = &o->audio_channel_maps[o->nb_audio_channel_maps - 1];

    /* muted channel syntax */
    n = sscanf(arg, "%d:%d.%d", &m->channel_idx, &m->ofile_idx, &m->ostream_idx);
    if ((n == 1 || n == 3) && m->channel_idx == -1) {
        m->file_idx = m->stream_idx = -1;
        if (n == 1)
            m->ofile_idx = m->ostream_idx = -1;
        return 0;
    }

    /* normal syntax */
    n = sscanf(arg, "%d.%d.%d:%d.%d",
               &m->file_idx,  &m->stream_idx, &m->channel_idx,
               &m->ofile_idx, &m->ostream_idx);

    if (n != 3 && n != 5) {
        av_log(NULL, AV_LOG_FATAL, "Syntax error, mapchan usage: "
               "[file.stream.channel|-1][:syncfile:syncstream]\n");
        exit_program(1);
    }

    if (n != 5) // only file.stream.channel specified
        m->ofile_idx = m->ostream_idx = -1;

    /* check input */
    if (m->file_idx < 0 || m->file_idx >= nb_input_files) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file index: %d\n",
               m->file_idx);
        exit_program(1);
    }
    if (m->stream_idx < 0 ||
        m->stream_idx >= input_files[m->file_idx]->nb_streams) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file stream index #%d.%d\n",
               m->file_idx, m->stream_idx);
        exit_program(1);
    }
    st = input_files[m->file_idx]->ctx->streams[m->stream_idx];
    if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: stream #%d.%d is not an audio stream.\n",
               m->file_idx, m->stream_idx);
        exit_program(1);
    }
    if (m->channel_idx < 0 || m->channel_idx >= st->codec->channels) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid audio channel #%d.%d.%d\n",
               m->file_idx, m->stream_idx, m->channel_idx);
        exit_program(1);
    }
    return 0;
}

/**
 * Parse a metadata specifier in arg.
 * @param type metadata type is written here -- g(lobal)/s(tream)/c(hapter)/p(rogram)
 * @param index for type c/p, chapter/program index is written here
 * @param stream_spec for type s, the stream specifier is written here
 */
static void parse_meta_type(char *arg, char *type, int *index, const char **stream_spec)
{
    if (*arg) {
        *type = *arg;
        switch (*arg) {
        case 'g':
            break;
        case 's':
            if (*(++arg) && *arg != ':') {
                av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", arg);
                exit_program(1);
            }
            *stream_spec = *arg == ':' ? arg + 1 : "";
            break;
        case 'c':
        case 'p':
            if (*(++arg) == ':')
                *index = strtol(++arg, NULL, 0);
            break;
        default:
            av_log(NULL, AV_LOG_FATAL, "Invalid metadata type %c.\n", *arg);
            exit_program(1);
        }
    } else
        *type = 'g';
}

static int copy_metadata(char *outspec, char *inspec, AVFormatContext *oc, AVFormatContext *ic, OptionsContext *o)
{
    AVDictionary **meta_in = NULL;
    AVDictionary **meta_out = NULL;
    int i, ret = 0;
    char type_in, type_out;
    const char *istream_spec = NULL, *ostream_spec = NULL;
    int idx_in = 0, idx_out = 0;

    parse_meta_type(inspec,  &type_in,  &idx_in,  &istream_spec);
    parse_meta_type(outspec, &type_out, &idx_out, &ostream_spec);

    if (!ic) {
        if (type_out == 'g' || !*outspec)
            o->metadata_global_manual = 1;
        if (type_out == 's' || !*outspec)
            o->metadata_streams_manual = 1;
        if (type_out == 'c' || !*outspec)
            o->metadata_chapters_manual = 1;
        return 0;
    }

    if (type_in == 'g' || type_out == 'g')
        o->metadata_global_manual = 1;
    if (type_in == 's' || type_out == 's')
        o->metadata_streams_manual = 1;
    if (type_in == 'c' || type_out == 'c')
        o->metadata_chapters_manual = 1;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
    if ((index) < 0 || (index) >= (nb_elems)) {\
        av_log(NULL, AV_LOG_FATAL, "Invalid %s index %d while processing metadata maps.\n",\
                (desc), (index));\
        exit_program(1);\
    }

#define SET_DICT(type, meta, context, index)\
        switch (type) {\
        case 'g':\
            meta = &context->metadata;\
            break;\
        case 'c':\
            METADATA_CHECK_INDEX(index, context->nb_chapters, "chapter")\
            meta = &context->chapters[index]->metadata;\
            break;\
        case 'p':\
            METADATA_CHECK_INDEX(index, context->nb_programs, "program")\
            meta = &context->programs[index]->metadata;\
            break;\
        }\

    SET_DICT(type_in, meta_in, ic, idx_in);
    SET_DICT(type_out, meta_out, oc, idx_out);

    /* for input streams choose first matching stream */
    if (type_in == 's') {
        for (i = 0; i < ic->nb_streams; i++) {
            if ((ret = check_stream_specifier(ic, ic->streams[i], istream_spec)) > 0) {
                meta_in = &ic->streams[i]->metadata;
                break;
            } else if (ret < 0)
                exit_program(1);
        }
        if (!meta_in) {
            av_log(NULL, AV_LOG_FATAL, "Stream specifier %s does not match  any streams.\n", istream_spec);
            exit_program(1);
        }
    }

    if (type_out == 's') {
        for (i = 0; i < oc->nb_streams; i++) {
            if ((ret = check_stream_specifier(oc, oc->streams[i], ostream_spec)) > 0) {
                meta_out = &oc->streams[i]->metadata;
                av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);
            } else if (ret < 0)
                exit_program(1);
        }
    } else
        av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int opt_recording_timestamp(OptionsContext *o, const char *opt, const char *arg)
{
    char buf[128];
    int64_t recording_timestamp = parse_time_or_die(opt, arg, 0) / 1E6;
    struct tm time = *gmtime((time_t*)&recording_timestamp);
    strftime(buf, sizeof(buf), "creation_time=%FT%T%z", &time);
    parse_option(o, "metadata", buf, options);

    av_log(NULL, AV_LOG_WARNING, "%s is deprecated, set the 'creation_time' metadata "
                                 "tag instead.\n", opt);
    return 0;
}

static AVCodec *find_codec_or_die(const char *name, enum AVMediaType type, int encoder)
{
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;

    codec = encoder ?
        avcodec_find_encoder_by_name(name) :
        avcodec_find_decoder_by_name(name);
    if (!codec) {
        av_log(NULL, AV_LOG_FATAL, "Unknown %s '%s'\n", codec_string, name);
        exit_program(1);
    }
    if (codec->type != type) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s type '%s'\n", codec_string, name);
        exit_program(1);
    }
    return codec;
}

static AVCodec *choose_decoder(OptionsContext *o, AVFormatContext *s, AVStream *st)
{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st);
    if (codec_name) {
        AVCodec *codec = find_codec_or_die(codec_name, st->codec->codec_type, 0);
        st->codec->codec_id = codec->id;
        return codec;
    } else
        return avcodec_find_decoder(st->codec->codec_id);
}

/**
 * Add all the streams from the given input file to the global
 * list of input streams.
 */
static void add_input_streams(OptionsContext *o, AVFormatContext *ic)
{
    int i;
    char *next, *codec_tag = NULL;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecContext *dec = st->codec;
        InputStream *ist = av_mallocz(sizeof(*ist));

        if (!ist)
            exit_program(1);

        input_streams = grow_array(input_streams, sizeof(*input_streams), &nb_input_streams, nb_input_streams + 1);
        input_streams[nb_input_streams - 1] = ist;

        ist->st = st;
        ist->file_index = nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->opts = filter_codec_opts(codec_opts, choose_decoder(o, ic, st), ic, st);

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st);

        MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st);
        if (codec_tag) {
            uint32_t tag = strtol(codec_tag, &next, 0);
            if (*next)
                tag = AV_RL32(codec_tag);
            st->codec->codec_tag = tag;
        }

        ist->dec = choose_decoder(o, ic, st);

        switch (dec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(dec->codec_id);
            if (dec->lowres) {
                dec->flags |= CODEC_FLAG_EMU_EDGE;
            }

            ist->resample_height  = dec->height;
            ist->resample_width   = dec->width;
            ist->resample_pix_fmt = dec->pix_fmt;

            break;
        case AVMEDIA_TYPE_AUDIO:
            guess_input_channel_layout(ist);

            ist->resample_sample_fmt     = dec->sample_fmt;
            ist->resample_sample_rate    = dec->sample_rate;
            ist->resample_channels       = dec->channels;
            ist->resample_channel_layout = dec->channel_layout;

            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_SUBTITLE:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(dec->codec_id);
            break;
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        default:
            abort();
        }
    }
}

static void assert_file_overwrite(const char *filename)
{
    if ((!file_overwrite || no_file_overwrite) &&
        (strchr(filename, ':') == NULL || filename[1] == ':' ||
         av_strstart(filename, "file:", NULL))) {
        if (avio_check(filename, 0) == 0) {
            if (!using_stdin && (!no_file_overwrite || file_overwrite)) {
                fprintf(stderr,"File '%s' already exists. Overwrite ? [y/N] ", filename);
                fflush(stderr);
                term_exit();
                signal(SIGINT, SIG_DFL);
                if (!read_yesno()) {
                    av_log(NULL, AV_LOG_FATAL, "Not overwriting - exiting\n");
                    exit_program(1);
                }
                term_init();
            }
            else {
                av_log(NULL, AV_LOG_FATAL, "File '%s' already exists. Exiting.\n", filename);
                exit_program(1);
            }
        }
    }
}

static void dump_attachment(AVStream *st, const char *filename)
{
    int ret;
    AVIOContext *out = NULL;
    AVDictionaryEntry *e;

    if (!st->codec->extradata_size) {
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

    avio_write(out, st->codec->extradata, st->codec->extradata_size);
    avio_flush(out);
    avio_close(out);
}

static int opt_input_file(OptionsContext *o, const char *opt, const char *filename)
{
    AVFormatContext *ic;
    AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    uint8_t buf[128];
    AVDictionary **opts;
    int orig_nb_streams;                     // number of streams before avformat_find_stream_info

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(NULL, AV_LOG_FATAL, "Unknown input format: '%s'\n", o->format);
            exit_program(1);
        }
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
    if (o->nb_audio_sample_rate) {
        snprintf(buf, sizeof(buf), "%d", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i);
        av_dict_set(&format_opts, "sample_rate", buf, 0);
    }
    if (o->nb_audio_channels) {
        /* because we set audio_channels based on both the "ac" and
         * "channel_layout" options, we need to check that the specified
         * demuxer actually has the "channels" option before setting it */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "channels", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            snprintf(buf, sizeof(buf), "%d",
                     o->audio_channels[o->nb_audio_channels - 1].u.i);
            av_dict_set(&format_opts, "channels", buf, 0);
        }
    }
    if (o->nb_frame_rates) {
        av_dict_set(&format_opts, "framerate", o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    ic->video_codec_id   = video_codec_name ?
        find_codec_or_die(video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0)->id : CODEC_ID_NONE;
    ic->audio_codec_id   = audio_codec_name ?
        find_codec_or_die(audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0)->id : CODEC_ID_NONE;
    ic->subtitle_codec_id= subtitle_codec_name ?
        find_codec_or_die(subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0)->id : CODEC_ID_NONE;
    ic->flags |= AVFMT_FLAG_NONBLOCK;
    ic->interrupt_callback = int_cb;

    /* open the input file with generic avformat function */
    err = avformat_open_input(&ic, filename, file_iformat, &format_opts);
    if (err < 0) {
        print_error(filename, err);
        exit_program(1);
    }
    assert_avoptions(format_opts);

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        choose_decoder(o, ic, ic->streams[i]);

    /* Set AVCodecContext options for avformat_find_stream_info */
    opts = setup_find_stream_info_opts(ic, codec_opts);
    orig_nb_streams = ic->nb_streams;

    /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
    ret = avformat_find_stream_info(ic, opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "%s: could not find codec parameters\n", filename);
        avformat_close_input(&ic);
        exit_program(1);
    }

    timestamp = o->start_time;
    /* add the stream start time */
    if (ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (o->start_time != 0) {
        ret = av_seek_frame(ic, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    /* update the current parameters so that they match the one of the input stream */
    add_input_streams(o, ic);

    /* dump the file content */
    av_dump_format(ic, nb_input_files, filename, 0);

    input_files = grow_array(input_files, sizeof(*input_files), &nb_input_files, nb_input_files + 1);
    if (!(input_files[nb_input_files - 1] = av_mallocz(sizeof(*input_files[0]))))
        exit_program(1);

    input_files[nb_input_files - 1]->ctx        = ic;
    input_files[nb_input_files - 1]->ist_index  = nb_input_streams - ic->nb_streams;
    input_files[nb_input_files - 1]->ts_offset  = o->input_ts_offset - (copy_ts ? 0 : timestamp);
    input_files[nb_input_files - 1]->nb_streams = ic->nb_streams;
    input_files[nb_input_files - 1]->rate_emu   = o->rate_emu;

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < ic->nb_streams; j++) {
            AVStream *st = ic->streams[j];

            if (check_stream_specifier(ic, st, o->dump_attachment[i].specifier) == 1)
                dump_attachment(st, o->dump_attachment[i].u.str);
        }
    }

    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);

    reset_options(o, 1);
    return 0;
}

static void parse_forced_key_frames(char *kf, OutputStream *ost)
{
    char *p;
    int n = 1, i;

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
        if (next) *next++ = 0;
        ost->forced_kf_pts[i] = parse_time_or_die("force_key_frames", p, 1);
        p = next;
    }
}

static uint8_t *get_line(AVIOContext *s)
{
    AVIOContext *line;
    uint8_t *buf;
    char c;

    if (avio_open_dyn_buf(&line) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloc buffer for reading preset.\n");
        exit_program(1);
    }

    while ((c = avio_r8(s)) && c != '\n')
        avio_w8(line, c);
    avio_w8(line, 0);
    avio_close_dyn_buf(line, &buf);

    return buf;
}

static int get_preset_file_2(const char *preset_name, const char *codec_name, AVIOContext **s)
{
    int i, ret = 1;
    char filename[1000];
    const char *base[3] = { getenv("AVCONV_DATADIR"),
                            getenv("HOME"),
                            AVCONV_DATADIR,
                            };

    for (i = 0; i < FF_ARRAY_ELEMS(base) && ret; i++) {
        if (!base[i])
            continue;
        if (codec_name) {
            snprintf(filename, sizeof(filename), "%s%s/%s-%s.avpreset", base[i],
                     i != 1 ? "" : "/.avconv", codec_name, preset_name);
            ret = avio_open2(s, filename, AVIO_FLAG_READ, &int_cb, NULL);
        }
        if (ret) {
            snprintf(filename, sizeof(filename), "%s%s/%s.avpreset", base[i],
                     i != 1 ? "" : "/.avconv", preset_name);
            ret = avio_open2(s, filename, AVIO_FLAG_READ, &int_cb, NULL);
        }
    }
    return ret;
}

static void choose_encoder(OptionsContext *o, AVFormatContext *s, OutputStream *ost)
{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, ost->st);
    if (!codec_name) {
        ost->st->codec->codec_id = av_guess_codec(s->oformat, NULL, s->filename,
                                                  NULL, ost->st->codec->codec_type);
        ost->enc = avcodec_find_encoder(ost->st->codec->codec_id);
    } else if (!strcmp(codec_name, "copy"))
        ost->stream_copy = 1;
    else {
        ost->enc = find_codec_or_die(codec_name, ost->st->codec->codec_type, 1);
        ost->st->codec->codec_id = ost->enc->id;
    }
}

static OutputStream *new_output_stream(OptionsContext *o, AVFormatContext *oc, enum AVMediaType type, int source_index)
{
    OutputStream *ost;
    AVStream *st = avformat_new_stream(oc, NULL);
    int idx      = oc->nb_streams - 1, ret = 0;
    char *bsf = NULL, *next, *codec_tag = NULL;
    AVBitStreamFilterContext *bsfc, *bsfc_prev = NULL;
    double qscale = -1;
    char *buf = NULL, *arg = NULL, *preset = NULL;
    AVIOContext *s = NULL;

    if (!st) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloc stream.\n");
        exit_program(1);
    }

    if (oc->nb_streams - 1 < o->nb_streamid_map)
        st->id = o->streamid_map[oc->nb_streams - 1];

    output_streams = grow_array(output_streams, sizeof(*output_streams), &nb_output_streams,
                                nb_output_streams + 1);
    if (!(ost = av_mallocz(sizeof(*ost))))
        exit_program(1);
    output_streams[nb_output_streams - 1] = ost;

    ost->file_index = nb_output_files;
    ost->index      = idx;
    ost->st         = st;
    st->codec->codec_type = type;
    choose_encoder(o, oc, ost);
    if (ost->enc) {
        ost->opts  = filter_codec_opts(codec_opts, ost->enc, oc, st);
    }

    avcodec_get_context_defaults3(st->codec, ost->enc);
    st->codec->codec_type = type; // XXX hack, avcodec_get_context_defaults2() sets type to unknown for stream copy

    MATCH_PER_STREAM_OPT(presets, str, preset, oc, st);
    if (preset && (!(ret = get_preset_file_2(preset, ost->enc->name, &s)))) {
        do  {
            buf = get_line(s);
            if (!buf[0] || buf[0] == '#') {
                av_free(buf);
                continue;
            }
            if (!(arg = strchr(buf, '='))) {
                av_log(NULL, AV_LOG_FATAL, "Invalid line found in the preset file.\n");
                exit_program(1);
            }
            *arg++ = 0;
            av_dict_set(&ost->opts, buf, arg, AV_DICT_DONT_OVERWRITE);
            av_free(buf);
        } while (!s->eof_reached);
        avio_close(s);
    }
    if (ret) {
        av_log(NULL, AV_LOG_FATAL,
               "Preset %s specified for stream %d:%d, but could not be opened.\n",
               preset, ost->file_index, ost->index);
        exit_program(1);
    }

    ost->max_frames = INT64_MAX;
    MATCH_PER_STREAM_OPT(max_frames, i64, ost->max_frames, oc, st);

    MATCH_PER_STREAM_OPT(bitstream_filters, str, bsf, oc, st);
    while (bsf) {
        if (next = strchr(bsf, ','))
            *next++ = 0;
        if (!(bsfc = av_bitstream_filter_init(bsf))) {
            av_log(NULL, AV_LOG_FATAL, "Unknown bitstream filter %s\n", bsf);
            exit_program(1);
        }
        if (bsfc_prev)
            bsfc_prev->next = bsfc;
        else
            ost->bitstream_filters = bsfc;

        bsfc_prev = bsfc;
        bsf       = next;
    }

    MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, oc, st);
    if (codec_tag) {
        uint32_t tag = strtol(codec_tag, &next, 0);
        if (*next)
            tag = AV_RL32(codec_tag);
        st->codec->codec_tag = tag;
    }

    MATCH_PER_STREAM_OPT(qscale, dbl, qscale, oc, st);
    if (qscale >= 0 || same_quant) {
        st->codec->flags |= CODEC_FLAG_QSCALE;
        st->codec->global_quality = FF_QP2LAMBDA * qscale;
    }

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        st->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

    av_opt_get_int(sws_opts, "sws_flags", 0, &ost->sws_flags);
    av_opt_get_int   (swr_opts, "dither_method", 0, &ost->swr_dither_method);
    av_opt_get_double(swr_opts, "dither_scale" , 0, &ost->swr_dither_scale);

    ost->source_index = source_index;
    if (source_index >= 0) {
        ost->sync_ist = input_streams[source_index];
        input_streams[source_index]->discard = 0;
        input_streams[source_index]->st->discard = AVDISCARD_NONE;
    }

    return ost;
}

static void parse_matrix_coeffs(uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for (i = 0;; i++) {
        dest[i] = atoi(p);
        if (i == 63)
            break;
        p = strchr(p, ',');
        if (!p) {
            av_log(NULL, AV_LOG_FATAL, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
            exit_program(1);
        }
        p++;
    }
}

static OutputStream *new_video_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *video_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_VIDEO, source_index);
    st  = ost->st;
    video_enc = st->codec;

    if (!ost->stream_copy) {
        const char *p = NULL;
        char *forced_key_frames = NULL, *frame_rate = NULL, *frame_size = NULL;
        char *frame_aspect_ratio = NULL, *frame_pix_fmt = NULL;
        char *intra_matrix = NULL, *inter_matrix = NULL, *filters = NULL;
        int i;

        MATCH_PER_STREAM_OPT(frame_rates, str, frame_rate, oc, st);
        if (frame_rate && av_parse_video_rate(&ost->frame_rate, frame_rate) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid framerate value: %s\n", frame_rate);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
        if (frame_size && av_parse_video_size(&video_enc->width, &video_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(frame_aspect_ratios, str, frame_aspect_ratio, oc, st);
        if (frame_aspect_ratio) {
            AVRational q;
            if (av_parse_ratio(&q, frame_aspect_ratio, 255, 0, NULL) < 0 ||
                q.num <= 0 || q.den <= 0) {
                av_log(NULL, AV_LOG_FATAL, "Invalid aspect ratio: %s\n", frame_aspect_ratio);
                exit_program(1);
            }
            ost->frame_aspect_ratio = av_q2d(q);
        }

        video_enc->bits_per_raw_sample = frame_bits_per_raw_sample;
        MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st);
        if (frame_pix_fmt && *frame_pix_fmt == '+') {
            ost->keep_pix_fmt = 1;
            if (!*++frame_pix_fmt)
                frame_pix_fmt = NULL;
        }
        if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == PIX_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Unknown pixel format requested: %s.\n", frame_pix_fmt);
            exit_program(1);
        }
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

        if (intra_only)
            video_enc->gop_size = 0;
        MATCH_PER_STREAM_OPT(intra_matrices, str, intra_matrix, oc, st);
        if (intra_matrix) {
            if (!(video_enc->intra_matrix = av_mallocz(sizeof(*video_enc->intra_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
                exit_program(1);
            }
            parse_matrix_coeffs(video_enc->intra_matrix, intra_matrix);
        }
        MATCH_PER_STREAM_OPT(inter_matrices, str, inter_matrix, oc, st);
        if (inter_matrix) {
            if (!(video_enc->inter_matrix = av_mallocz(sizeof(*video_enc->inter_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for inter matrix.\n");
                exit_program(1);
            }
            parse_matrix_coeffs(video_enc->inter_matrix, inter_matrix);
        }

        MATCH_PER_STREAM_OPT(rc_overrides, str, p, oc, st);
        for (i = 0; p; i++) {
            int start, end, q;
            int e = sscanf(p, "%d,%d,%d", &start, &end, &q);
            if (e != 3) {
                av_log(NULL, AV_LOG_FATAL, "error parsing rc_override\n");
                exit_program(1);
            }
            /* FIXME realloc failure */
            video_enc->rc_override =
                av_realloc(video_enc->rc_override,
                           sizeof(RcOverride) * (i + 1));
            video_enc->rc_override[i].start_frame = start;
            video_enc->rc_override[i].end_frame   = end;
            if (q > 0) {
                video_enc->rc_override[i].qscale         = q;
                video_enc->rc_override[i].quality_factor = 1.0;
            }
            else {
                video_enc->rc_override[i].qscale         = 0;
                video_enc->rc_override[i].quality_factor = -q/100.0;
            }
            p = strchr(p, '/');
            if (p) p++;
        }
        video_enc->rc_override_count = i;
        if (!video_enc->rc_initial_buffer_occupancy)
            video_enc->rc_initial_buffer_occupancy = video_enc->rc_buffer_size * 3 / 4;
        video_enc->intra_dc_precision = intra_dc_precision - 8;

        if (do_psnr)
            video_enc->flags|= CODEC_FLAG_PSNR;

        /* two pass mode */
        if (do_pass) {
            if (do_pass & 1) {
                video_enc->flags |= CODEC_FLAG_PASS1;
            }
            if (do_pass & 2) {
                video_enc->flags |= CODEC_FLAG_PASS2;
            }
        }

        MATCH_PER_STREAM_OPT(forced_key_frames, str, forced_key_frames, oc, st);
        if (forced_key_frames)
            parse_forced_key_frames(forced_key_frames, ost);

        MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st);

        ost->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st);

        MATCH_PER_STREAM_OPT(filters, str, filters, oc, st);
        if (filters)
            ost->avfilter = av_strdup(filters);
    } else {
        MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc ,st);
    }

    return ost;
}

static OutputStream *new_audio_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
    int n;
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *audio_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_AUDIO, source_index);
    st  = ost->st;

    audio_enc = st->codec;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    if (!ost->stream_copy) {
        char *sample_fmt = NULL, *filters = NULL;

        MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st);

        MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st);
        if (sample_fmt &&
            (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sample format '%s'\n", sample_fmt);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st);

        MATCH_PER_STREAM_OPT(filters, str, filters, oc, st);
        if (filters)
            ost->avfilter = av_strdup(filters);

        /* check for channel mapping for this audio stream */
        for (n = 0; n < o->nb_audio_channel_maps; n++) {
            AudioChannelMap *map = &o->audio_channel_maps[n];
            InputStream *ist = input_streams[ost->source_index];
            if ((map->channel_idx == -1 || (ist->file_index == map->file_idx && ist->st->index == map->stream_idx)) &&
                (map->ofile_idx   == -1 || ost->file_index == map->ofile_idx) &&
                (map->ostream_idx == -1 || ost->st->index  == map->ostream_idx)) {
                if (ost->audio_channels_mapped < FF_ARRAY_ELEMS(ost->audio_channels_map))
                    ost->audio_channels_map[ost->audio_channels_mapped++] = map->channel_idx;
                else
                    av_log(NULL, AV_LOG_FATAL, "Max channel mapping for output %d.%d reached\n",
                           ost->file_index, ost->st->index);
            }
        }
    }

    return ost;
}

static OutputStream *new_data_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
    OutputStream *ost;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_DATA, source_index);
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
        exit_program(1);
    }

    return ost;
}

static OutputStream *new_attachment_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
    OutputStream *ost = new_output_stream(o, oc, AVMEDIA_TYPE_ATTACHMENT, source_index);
    ost->stream_copy = 1;
    return ost;
}

static OutputStream *new_subtitle_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *subtitle_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_SUBTITLE, source_index);
    st  = ost->st;
    subtitle_enc = st->codec;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc, st);

    return ost;
}

/* arg format is "output-stream-index:streamid-value". */
static int opt_streamid(OptionsContext *o, const char *opt, const char *arg)
{
    int idx;
    char *p;
    char idx_str[16];

    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        av_log(NULL, AV_LOG_FATAL,
               "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
               arg, opt);
        exit_program(1);
    }
    *p++ = '\0';
    idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, MAX_STREAMS-1);
    o->streamid_map = grow_array(o->streamid_map, sizeof(*o->streamid_map), &o->nb_streamid_map, idx+1);
    o->streamid_map[idx] = parse_number_or_die(opt, p, OPT_INT, 0, INT_MAX);
    return 0;
}

static int copy_chapters(InputFile *ifile, OutputFile *ofile, int copy_metadata)
{
    AVFormatContext *is = ifile->ctx;
    AVFormatContext *os = ofile->ctx;
    int i;

    for (i = 0; i < is->nb_chapters; i++) {
        AVChapter *in_ch = is->chapters[i], *out_ch;
        int64_t ts_off   = av_rescale_q(ofile->start_time - ifile->ts_offset,
                                       AV_TIME_BASE_Q, in_ch->time_base);
        int64_t rt       = (ofile->recording_time == INT64_MAX) ? INT64_MAX :
                           av_rescale_q(ofile->recording_time, AV_TIME_BASE_Q, in_ch->time_base);


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

        if (copy_metadata)
            av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

        os->nb_chapters++;
        os->chapters = av_realloc_f(os->chapters, os->nb_chapters, sizeof(AVChapter));
        if (!os->chapters)
            return AVERROR(ENOMEM);
        os->chapters[os->nb_chapters - 1] = out_ch;
    }
    return 0;
}

static int read_ffserver_streams(OptionsContext *o, AVFormatContext *s, const char *filename)
{
    int i, err;
    AVFormatContext *ic = avformat_alloc_context();

    ic->interrupt_callback = int_cb;
    err = avformat_open_input(&ic, filename, NULL, NULL);
    if (err < 0)
        return err;
    /* copy stream format */
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st;
        OutputStream *ost;
        AVCodec *codec;
        AVCodecContext *avctx;

        codec = avcodec_find_encoder(ic->streams[i]->codec->codec_id);
        ost   = new_output_stream(o, s, codec->type, -1);
        st    = ost->st;
        avctx = st->codec;
        ost->enc = codec;

        // FIXME: a more elegant solution is needed
        memcpy(st, ic->streams[i], sizeof(AVStream));
        st->cur_dts = 0;
        st->info = av_malloc(sizeof(*st->info));
        memcpy(st->info, ic->streams[i]->info, sizeof(*st->info));
        st->codec= avctx;
        avcodec_copy_context(st->codec, ic->streams[i]->codec);

        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO && !ost->stream_copy)
            choose_sample_fmt(st, codec);
        else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && !ost->stream_copy)
            choose_pixel_fmt(st, codec, st->codec->pix_fmt);
    }

    avformat_close_input(&ic);
    return 0;
}

static void init_output_filter(OutputFilter *ofilter, OptionsContext *o,
                               AVFormatContext *oc)
{
    OutputStream *ost;

    switch (ofilter->out_tmp->filter_ctx->output_pads[ofilter->out_tmp->pad_idx].type) {
    case AVMEDIA_TYPE_VIDEO: ost = new_video_stream(o, oc, -1); break;
    case AVMEDIA_TYPE_AUDIO: ost = new_audio_stream(o, oc, -1); break;
    default:
        av_log(NULL, AV_LOG_FATAL, "Only video and audio filters are supported "
               "currently.\n");
        exit_program(1);
    }

    ost->source_index = -1;
    ost->filter       = ofilter;

    ofilter->ost      = ost;

    if (ost->stream_copy) {
        av_log(NULL, AV_LOG_ERROR, "Streamcopy requested for output stream %d:%d, "
               "which is fed from a complex filtergraph. Filtering and streamcopy "
               "cannot be used together.\n", ost->file_index, ost->index);
        exit_program(1);
    }

    if (configure_output_filter(ofilter->graph, ofilter, ofilter->out_tmp) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error configuring filter.\n");
        exit_program(1);
    }
    avfilter_inout_free(&ofilter->out_tmp);
}

static void opt_output_file(void *optctx, const char *filename)
{
    OptionsContext *o = optctx;
    AVFormatContext *oc;
    int i, j, err;
    AVOutputFormat *file_oformat;
    OutputStream *ost;
    InputStream  *ist;

    if (configure_complex_filters() < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error configuring filters.\n");
        exit_program(1);
    }

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    err = avformat_alloc_output_context2(&oc, NULL, o->format, filename);
    if (!oc) {
        print_error(filename, err);
        exit_program(1);
    }
    file_oformat= oc->oformat;
    oc->interrupt_callback = int_cb;

    /* create streams for all unlabeled output pads */
    for (i = 0; i < nb_filtergraphs; i++) {
        FilterGraph *fg = filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *ofilter = fg->outputs[j];

            if (!ofilter->out_tmp || ofilter->out_tmp->name)
                continue;

            switch (ofilter->out_tmp->filter_ctx->output_pads[ofilter->out_tmp->pad_idx].type) {
            case AVMEDIA_TYPE_VIDEO:    o->video_disable    = 1; break;
            case AVMEDIA_TYPE_AUDIO:    o->audio_disable    = 1; break;
            case AVMEDIA_TYPE_SUBTITLE: o->subtitle_disable = 1; break;
            }
            init_output_filter(ofilter, o, oc);
        }
    }

    if (!strcmp(file_oformat->name, "ffm") &&
        av_strstart(filename, "http:", NULL)) {
        int j;
        /* special case for files sent to ffserver: we get the stream
           parameters from ffserver */
        int err = read_ffserver_streams(o, oc, filename);
        if (err < 0) {
            print_error(filename, err);
            exit_program(1);
        }
        for(j = nb_output_streams - oc->nb_streams; j < nb_output_streams; j++) {
            ost = output_streams[j];
            for (i = 0; i < nb_input_streams; i++) {
                ist = input_streams[i];
                if(ist->st->codec->codec_type == ost->st->codec->codec_type){
                    ost->sync_ist= ist;
                    ost->source_index= i;
                    ist->discard = 0;
                    ist->st->discard = AVDISCARD_NONE;
                    break;
                }
            }
            if(!ost->sync_ist){
                av_log(NULL, AV_LOG_FATAL, "Missing %s stream which is required by this ffm\n", av_get_media_type_string(ost->st->codec->codec_type));
                exit_program(1);
            }
        }
    } else if (!o->nb_stream_maps) {
        /* pick the "best" stream of each type */

        /* video: highest resolution */
        if (!o->video_disable && oc->oformat->video_codec != CODEC_ID_NONE) {
            int area = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = input_streams[i];
                if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                    ist->st->codec->width * ist->st->codec->height > area) {
                    area = ist->st->codec->width * ist->st->codec->height;
                    idx = i;
                }
            }
            if (idx >= 0)
                new_video_stream(o, oc, idx);
        }

        /* audio: most channels */
        if (!o->audio_disable && oc->oformat->audio_codec != CODEC_ID_NONE) {
            int channels = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = input_streams[i];
                if (ist->st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                    ist->st->codec->channels > channels) {
                    channels = ist->st->codec->channels;
                    idx = i;
                }
            }
            if (idx >= 0)
                new_audio_stream(o, oc, idx);
        }

        /* subtitles: pick first */
        if (!o->subtitle_disable && (oc->oformat->subtitle_codec != CODEC_ID_NONE || subtitle_codec_name)) {
            for (i = 0; i < nb_input_streams; i++)
                if (input_streams[i]->st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    new_subtitle_stream(o, oc, i);
                    break;
                }
        }
        /* do something with data? */
    } else {
        for (i = 0; i < o->nb_stream_maps; i++) {
            StreamMap *map = &o->stream_maps[i];
            int src_idx = input_files[map->file_index]->ist_index + map->stream_index;

            if (map->disabled)
                continue;

            if (map->linklabel) {
                FilterGraph *fg;
                OutputFilter *ofilter = NULL;
                int j, k;

                for (j = 0; j < nb_filtergraphs; j++) {
                    fg = filtergraphs[j];
                    for (k = 0; k < fg->nb_outputs; k++) {
                        AVFilterInOut *out = fg->outputs[k]->out_tmp;
                        if (out && !strcmp(out->name, map->linklabel)) {
                            ofilter = fg->outputs[k];
                            goto loop_end;
                        }
                    }
                }
loop_end:
                if (!ofilter) {
                    av_log(NULL, AV_LOG_FATAL, "Output with label '%s' does not exist "
                           "in any defined filter graph.\n", map->linklabel);
                    exit_program(1);
                }
                init_output_filter(ofilter, o, oc);
            } else {
                ist = input_streams[input_files[map->file_index]->ist_index + map->stream_index];
                if(o->subtitle_disable && ist->st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
                    continue;
                if(o->   audio_disable && ist->st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
                    continue;
                if(o->   video_disable && ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                    continue;
                if(o->    data_disable && ist->st->codec->codec_type == AVMEDIA_TYPE_DATA)
                    continue;

                switch (ist->st->codec->codec_type) {
                case AVMEDIA_TYPE_VIDEO:      ost = new_video_stream     (o, oc, src_idx); break;
                case AVMEDIA_TYPE_AUDIO:      ost = new_audio_stream     (o, oc, src_idx); break;
                case AVMEDIA_TYPE_SUBTITLE:   ost = new_subtitle_stream  (o, oc, src_idx); break;
                case AVMEDIA_TYPE_DATA:       ost = new_data_stream      (o, oc, src_idx); break;
                case AVMEDIA_TYPE_ATTACHMENT: ost = new_attachment_stream(o, oc, src_idx); break;
                default:
                    av_log(NULL, AV_LOG_FATAL, "Cannot map stream #%d:%d - unsupported type.\n",
                           map->file_index, map->stream_index);
                    exit_program(1);
                }
            }
        }
    }


    for (i = nb_output_streams - oc->nb_streams; i < nb_output_streams; i++) { //for all streams of this output file
        AVDictionaryEntry *e;
        ost = output_streams[i];

        if (   ost->stream_copy
            && (e = av_dict_get(codec_opts, "flags", NULL, AV_DICT_IGNORE_SUFFIX))
            && (!e->key[5] || check_stream_specifier(oc, ost->st, e->key+6)))
            if (av_opt_set(ost->st->codec, "flags", e->value, 0) < 0)
                exit_program(1);
    }

    /* handle attached files */
    for (i = 0; i < o->nb_attachments; i++) {
        AVIOContext *pb;
        uint8_t *attachment;
        const char *p;
        int64_t len;

        if ((err = avio_open2(&pb, o->attachments[i], AVIO_FLAG_READ, &int_cb, NULL)) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not open attachment file %s.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        if ((len = avio_size(pb)) <= 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not get size of the attachment %s.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        if (!(attachment = av_malloc(len))) {
            av_log(NULL, AV_LOG_FATAL, "Attachment %s too large to fit into memory.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        avio_read(pb, attachment, len);

        ost = new_attachment_stream(o, oc, -1);
        ost->stream_copy               = 0;
        ost->attachment_filename       = o->attachments[i];
        ost->st->codec->extradata      = attachment;
        ost->st->codec->extradata_size = len;

        p = strrchr(o->attachments[i], '/');
        av_dict_set(&ost->st->metadata, "filename", (p && *p) ? p + 1 : o->attachments[i], AV_DICT_DONT_OVERWRITE);
        avio_close(pb);
    }

    output_files = grow_array(output_files, sizeof(*output_files), &nb_output_files, nb_output_files + 1);
    if (!(output_files[nb_output_files - 1] = av_mallocz(sizeof(*output_files[0]))))
        exit_program(1);

    output_files[nb_output_files - 1]->ctx            = oc;
    output_files[nb_output_files - 1]->ost_index      = nb_output_streams - oc->nb_streams;
    output_files[nb_output_files - 1]->recording_time = o->recording_time;
    if (o->recording_time != INT64_MAX)
        oc->duration = o->recording_time;
    output_files[nb_output_files - 1]->start_time     = o->start_time;
    output_files[nb_output_files - 1]->limit_filesize = o->limit_filesize;
    av_dict_copy(&output_files[nb_output_files - 1]->opts, format_opts, 0);

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(oc->filename)) {
            print_error(oc->filename, AVERROR(EINVAL));
            exit_program(1);
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid losing precious files */
        assert_file_overwrite(filename);

        /* open the file */
        if ((err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                              &oc->interrupt_callback,
                              &output_files[nb_output_files - 1]->opts)) < 0) {
            print_error(filename, err);
            exit_program(1);
        }
    }

    if (o->mux_preload) {
        uint8_t buf[64];
        snprintf(buf, sizeof(buf), "%d", (int)(o->mux_preload*AV_TIME_BASE));
        av_dict_set(&output_files[nb_output_files - 1]->opts, "preload", buf, 0);
    }
    oc->max_delay = (int)(o->mux_max_delay * AV_TIME_BASE);

    /* copy metadata */
    for (i = 0; i < o->nb_metadata_map; i++) {
        char *p;
        int in_file_index = strtol(o->metadata_map[i].u.str, &p, 0);

        if (in_file_index >= nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d while processing metadata maps\n", in_file_index);
            exit_program(1);
        }
        copy_metadata(o->metadata_map[i].specifier, *p ? p + 1 : p, oc, in_file_index >= 0 ? input_files[in_file_index]->ctx : NULL, o);
    }

    /* copy chapters */
    if (o->chapters_input_file >= nb_input_files) {
        if (o->chapters_input_file == INT_MAX) {
            /* copy chapters from the first input file that has them*/
            o->chapters_input_file = -1;
            for (i = 0; i < nb_input_files; i++)
                if (input_files[i]->ctx->nb_chapters) {
                    o->chapters_input_file = i;
                    break;
                }
        } else {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d in chapter mapping.\n",
                   o->chapters_input_file);
            exit_program(1);
        }
    }
    if (o->chapters_input_file >= 0)
        copy_chapters(input_files[o->chapters_input_file], output_files[nb_output_files - 1],
                      !o->metadata_chapters_manual);

    /* copy global metadata by default */
    if (!o->metadata_global_manual && nb_input_files){
        av_dict_copy(&oc->metadata, input_files[0]->ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
        if(o->recording_time != INT64_MAX)
            av_dict_set(&oc->metadata, "duration", NULL, 0);
    }
    if (!o->metadata_streams_manual)
        for (i = output_files[nb_output_files - 1]->ost_index; i < nb_output_streams; i++) {
            InputStream *ist;
            if (output_streams[i]->source_index < 0)         /* this is true e.g. for attached files */
                continue;
            ist = input_streams[output_streams[i]->source_index];
            av_dict_copy(&output_streams[i]->st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
        }

    /* process manually set metadata */
    for (i = 0; i < o->nb_metadata; i++) {
        AVDictionary **m;
        char type, *val;
        const char *stream_spec;
        int index = 0, j, ret = 0;

        val = strchr(o->metadata[i].u.str, '=');
        if (!val) {
            av_log(NULL, AV_LOG_FATAL, "No '=' character in metadata string %s.\n",
                   o->metadata[i].u.str);
            exit_program(1);
        }
        *val++ = 0;

        parse_meta_type(o->metadata[i].specifier, &type, &index, &stream_spec);
        if (type == 's') {
            for (j = 0; j < oc->nb_streams; j++) {
                if ((ret = check_stream_specifier(oc, oc->streams[j], stream_spec)) > 0) {
                    av_dict_set(&oc->streams[j]->metadata, o->metadata[i].u.str, *val ? val : NULL, 0);
                } else if (ret < 0)
                    exit_program(1);
            }
            printf("ret %d, stream_spec %s\n", ret, stream_spec);
        }
        else {
            switch (type) {
            case 'g':
                m = &oc->metadata;
                break;
            case 'c':
                if (index < 0 || index >= oc->nb_chapters) {
                    av_log(NULL, AV_LOG_FATAL, "Invalid chapter index %d in metadata specifier.\n", index);
                    exit_program(1);
                }
                m = &oc->chapters[index]->metadata;
                break;
            default:
                av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", o->metadata[i].specifier);
                exit_program(1);
            }
            av_dict_set(m, o->metadata[i].u.str, *val ? val : NULL, 0);
        }
    }

    reset_options(o, 0);
}

/* same option as mencoder */
static int opt_pass(const char *opt, const char *arg)
{
    do_pass = parse_number_or_die(opt, arg, OPT_INT, 1, 3);
    return 0;
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

static int opt_audio_qscale(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "q:a", arg, options);
}

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Hyper fast Audio and Video encoder\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

static int opt_help(const char *opt, const char *arg)
{
    int flags = AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM;
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
    show_help_children(avcodec_get_class(), flags);
    show_help_children(avformat_get_class(), flags);
    show_help_children(sws_get_class(), flags);

    return 0;
}

static int opt_target(OptionsContext *o, const char *opt, const char *arg)
{
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
    static const char *const frame_rates[] = { "25", "30000/1001", "24000/1001" };

    if (!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if (!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if (!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        /* Try to determine PAL/NTSC by peeking in the input files */
        if (nb_input_files) {
            int i, j, fr;
            for (j = 0; j < nb_input_files; j++) {
                for (i = 0; i < input_files[j]->nb_streams; i++) {
                    AVCodecContext *c = input_files[j]->ctx->streams[i]->codec;
                    if (c->codec_type != AVMEDIA_TYPE_VIDEO)
                        continue;
                    fr = c->time_base.den * 1000 / c->time_base.num;
                    if (fr == 25000) {
                        norm = PAL;
                        break;
                    } else if ((fr == 29970) || (fr == 23976)) {
                        norm = NTSC;
                        break;
                    }
                }
                if (norm != UNKNOWN)
                    break;
            }
        }
        if (norm != UNKNOWN)
            av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if (norm == UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
        exit_program(1);
    }

    if (!strcmp(arg, "vcd")) {
        opt_video_codec(o, "c:v", "mpeg1video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "vcd", options);

        parse_option(o, "s", norm == PAL ? "352x288" : "352x240", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b:v", "1150000");
        opt_default("maxrate", "1150000");
        opt_default("minrate", "1150000");
        opt_default("bufsize", "327680"); // 40*1024*8;

        opt_default("b:a", "224000");
        parse_option(o, "ar", "44100", options);
        parse_option(o, "ac", "2", options);

        opt_default("packetsize", "2324");
        opt_default("muxrate", "1411200"); // 2352 * 75 * 8;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        o->mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
    } else if (!strcmp(arg, "svcd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "svcd", options);

        parse_option(o, "s", norm == PAL ? "480x576" : "480x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b:v", "2040000");
        opt_default("maxrate", "2516000");
        opt_default("minrate", "0"); // 1145000;
        opt_default("bufsize", "1835008"); // 224*1024*8;
        opt_default("scan_offset", "1");


        opt_default("b:a", "224000");
        parse_option(o, "ar", "44100", options);

        opt_default("packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "ac3");
        parse_option(o, "f", "dvd", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b:v", "6000000");
        opt_default("maxrate", "9000000");
        opt_default("minrate", "0"); // 1500000;
        opt_default("bufsize", "1835008"); // 224*1024*8;

        opt_default("packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        opt_default("muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

        opt_default("b:a", "448000");
        parse_option(o, "ar", "48000", options);

    } else if (!strncmp(arg, "dv", 2)) {

        parse_option(o, "f", "dv", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                          norm == PAL ? "yuv420p" : "yuv411p", options);
        parse_option(o, "r", frame_rates[norm], options);

        parse_option(o, "ar", "48000", options);
        parse_option(o, "ac", "2", options);

    } else {
        av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_vstats_file(const char *opt, const char *arg)
{
    av_free (vstats_filename);
    vstats_filename = av_strdup (arg);
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

static int opt_video_frames(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "frames:v", arg, options);
}

static int opt_audio_frames(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "frames:a", arg, options);
}

static int opt_data_frames(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "frames:d", arg, options);
}

static int opt_preset(OptionsContext *o, const char *opt, const char *arg)
{
    FILE *f=NULL;
    char filename[1000], line[1000], tmp_line[1000];
    const char *codec_name = *opt == 'v' ? video_codec_name :
                             *opt == 'a' ? audio_codec_name :
                                           subtitle_codec_name;

    if (!(f = get_preset_file(filename, sizeof(filename), arg, *opt == 'f', codec_name))) {
        if(!strncmp(arg, "libx264-lossless", strlen("libx264-lossless"))){
            av_log(NULL, AV_LOG_FATAL, "Please use -preset <speed> -qp 0\n");
        }else
            av_log(NULL, AV_LOG_FATAL, "File for preset '%s' not found\n", arg);
        exit_program(1);
    }

    while (fgets(line, sizeof(line), f)) {
        char *key = tmp_line, *value, *endptr;

        if (strcspn(line, "#\n\r") == 0)
            continue;
        strcpy(tmp_line, line);
        if (!av_strtok(key,   "=",    &value) ||
            !av_strtok(value, "\r\n", &endptr)) {
            av_log(NULL, AV_LOG_FATAL, "%s: Invalid syntax: '%s'\n", filename, line);
            exit_program(1);
        }
        av_log(NULL, AV_LOG_DEBUG, "ffpreset[%s]: set '%s' = '%s'\n", filename, key, value);

        if      (!strcmp(key, "acodec")) opt_audio_codec   (o, key, value);
        else if (!strcmp(key, "vcodec")) opt_video_codec   (o, key, value);
        else if (!strcmp(key, "scodec")) opt_subtitle_codec(o, key, value);
        else if (!strcmp(key, "dcodec")) opt_data_codec    (o, key, value);
        else if (opt_default(key, value) < 0) {
            av_log(NULL, AV_LOG_FATAL, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n",
                   filename, line, key, value);
            exit_program(1);
        }
    }

    fclose(f);

    return 0;
}

static void log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{
}

static int opt_passlogfile(const char *opt, const char *arg)
{
    pass_logfilename_prefix = arg;
#if CONFIG_LIBX264_ENCODER
    return opt_default(opt, arg);
#else
    return 0;
#endif
}

static int opt_old2new(OptionsContext *o, const char *opt, const char *arg)
{
    char *s = av_asprintf("%s:%c", opt + 1, *opt);
    int ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_bitrate(OptionsContext *o, const char *opt, const char *arg)
{
    if(!strcmp(opt, "b")){
        av_log(NULL, AV_LOG_WARNING, "Please use -b:a or -b:v, -b is ambiguous\n");
        return parse_option(o, "b:v", arg, options);
    }
    return opt_default(opt, arg);
}

static int opt_qscale(OptionsContext *o, const char *opt, const char *arg)
{
    char *s;
    int ret;
    if(!strcmp(opt, "qscale")){
        av_log(NULL, AV_LOG_WARNING, "Please use -q:a or -q:v, -qscale is ambiguous\n");
        return parse_option(o, "q:v", arg, options);
    }
    s = av_asprintf("q%s", opt + 6);
    ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_profile(OptionsContext *o, const char *opt, const char *arg)
{
    if(!strcmp(opt, "profile")){
        av_log(NULL, AV_LOG_WARNING, "Please use -profile:a or -profile:v, -profile is ambiguous\n");
        return parse_option(o, "profile:v", arg, options);
    }
    return opt_default(opt, arg);
}

static int opt_video_filters(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "filter:v", arg, options);
}

static int opt_audio_filters(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "filter:a", arg, options);
}

static int opt_vsync(const char *opt, const char *arg)
{
    if      (!av_strcasecmp(arg, "cfr"))         video_sync_method = VSYNC_CFR;
    else if (!av_strcasecmp(arg, "vfr"))         video_sync_method = VSYNC_VFR;
    else if (!av_strcasecmp(arg, "passthrough")) video_sync_method = VSYNC_PASSTHROUGH;
    else if (!av_strcasecmp(arg, "drop"))        video_sync_method = VSYNC_DROP;

    if (video_sync_method == VSYNC_AUTO)
        video_sync_method = parse_number_or_die("vsync", arg, OPT_INT, VSYNC_AUTO, VSYNC_VFR);
    return 0;
}

static int opt_deinterlace(const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "-%s is deprecated, use -filter:v yadif instead\n", opt);
    do_deinterlace = 1;
    return 0;
}

static void parse_cpuflags(int argc, char **argv, const OptionDef *options)
{
    int idx = locate_option(argc, argv, options, "cpuflags");
    if (idx && argv[idx + 1])
        opt_cpuflags("cpuflags", argv[idx + 1]);
}

static int opt_channel_layout(OptionsContext *o, const char *opt, const char *arg)
{
    char layout_str[32];
    char *stream_str;
    char *ac_str;
    int ret, channels, ac_str_size;
    uint64_t layout;

    layout = av_get_channel_layout(arg);
    if (!layout) {
        av_log(NULL, AV_LOG_ERROR, "Unknown channel layout: %s\n", arg);
        return AVERROR(EINVAL);
    }
    snprintf(layout_str, sizeof(layout_str), "%"PRIu64, layout);
    ret = opt_default(opt, layout_str);
    if (ret < 0)
        return ret;

    /* set 'ac' option based on channel layout */
    channels = av_get_channel_layout_nb_channels(layout);
    snprintf(layout_str, sizeof(layout_str), "%d", channels);
    stream_str = strchr(opt, ':');
    ac_str_size = 3 + (stream_str ? strlen(stream_str) : 0);
    ac_str = av_mallocz(ac_str_size);
    if (!ac_str)
        return AVERROR(ENOMEM);
    av_strlcpy(ac_str, "ac", 3);
    if (stream_str)
        av_strlcat(ac_str, stream_str, ac_str_size);
    ret = parse_option(o, ac_str, layout_str, options);
    av_free(ac_str);

    return ret;
}

static int opt_filter_complex(const char *opt, const char *arg)
{
    filtergraphs = grow_array(filtergraphs, sizeof(*filtergraphs),
                              &nb_filtergraphs, nb_filtergraphs + 1);
    if (!(filtergraphs[nb_filtergraphs - 1] = av_mallocz(sizeof(*filtergraphs[0]))))
        return AVERROR(ENOMEM);
    filtergraphs[nb_filtergraphs - 1]->index       = nb_filtergraphs - 1;
    filtergraphs[nb_filtergraphs - 1]->graph_desc = arg;
    return 0;
}

#define OFFSET(x) offsetof(OptionsContext, x)
static const OptionDef options[] = {
    /* main options */
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG | OPT_STRING | OPT_OFFSET, {.off = OFFSET(format)}, "force format", "fmt" },
    { "i", HAS_ARG | OPT_FUNC2, {(void*)opt_input_file}, "input file name", "filename" },
    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
    { "n", OPT_BOOL, {(void*)&no_file_overwrite}, "do not overwrite output files" },
    { "c", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(codec_names)}, "codec name", "codec" },
    { "codec", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(codec_names)}, "codec name", "codec" },
    { "pre", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(presets)}, "preset name", "preset" },
    { "map", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_map}, "set input stream mapping", "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
    { "map_channel", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_map_channel}, "map an audio channel from one stream to another", "file.stream.channel[:syncfile.syncstream]" },
    { "map_metadata", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(metadata_map)}, "set metadata information of outfile from infile",
      "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",  OPT_INT | HAS_ARG | OPT_EXPERT | OPT_OFFSET, {.off = OFFSET(chapters_input_file)},  "set chapters mapping", "input_file_index" },
    { "t", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(recording_time)}, "record or transcode \"duration\" seconds of audio/video", "duration" },
    { "fs", HAS_ARG | OPT_INT64 | OPT_OFFSET, {.off = OFFSET(limit_filesize)}, "set the limit file size in bytes", "limit_size" }, //
    { "ss", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(start_time)}, "set the start time offset", "time_off" },
    { "itsoffset", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(input_ts_offset)}, "set the input ts offset", "time_off" },
    { "itsscale", HAS_ARG | OPT_DOUBLE | OPT_SPEC, {.off = OFFSET(ts_scale)}, "set the input ts scale", "scale" },
    { "timestamp", HAS_ARG | OPT_FUNC2, {(void*)opt_recording_timestamp}, "set the recording timestamp ('now' to set the current time)", "time" },
    { "metadata", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(metadata)}, "add metadata", "string=string" },
    { "dframes", HAS_ARG | OPT_FUNC2, {(void*)opt_data_frames}, "set the number of data frames to record", "number" },
    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark},
      "add timings for benchmarking" },
    { "benchmark_all", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark_all},
      "add timings for each task" },
    { "timelimit", HAS_ARG, {(void*)opt_timelimit}, "set max runtime in seconds", "limit" },
    { "dump", OPT_BOOL | OPT_EXPERT, {(void*)&do_pkt_dump},
      "dump each input packet" },
    { "hex", OPT_BOOL | OPT_EXPERT, {(void*)&do_hex_dump},
      "when dumping packets, also dump the payload" },
    { "re", OPT_BOOL | OPT_EXPERT | OPT_OFFSET, {.off = OFFSET(rate_emu)}, "read input at native frame rate", "" },
    { "target", HAS_ARG | OPT_FUNC2, {(void*)opt_target}, "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\", \"dv50\", \"pal-vcd\", \"ntsc-svcd\", ...)", "type" },
    { "vsync", HAS_ARG | OPT_EXPERT, {(void*)opt_vsync}, "video sync method", "" },
    { "async", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&audio_sync_method}, "audio sync method", "" },
    { "adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&audio_drift_threshold}, "audio drift threshold", "threshold" },
    { "copyts", OPT_BOOL | OPT_EXPERT, {(void*)&copy_ts}, "copy timestamps" },
    { "copytb", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&copy_tb}, "copy input stream time base when stream copying", "mode" },
    { "shortest", OPT_BOOL | OPT_EXPERT, {(void*)&opt_shortest}, "finish encoding within shortest input" }, //
    { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&dts_delta_threshold}, "timestamp discontinuity delta threshold", "threshold" },
    { "dts_error_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&dts_error_threshold}, "timestamp error delta threshold", "threshold" },
    { "xerror", OPT_BOOL, {(void*)&exit_on_error}, "exit on error", "error" },
    { "copyinkf", OPT_BOOL | OPT_EXPERT | OPT_SPEC, {.off = OFFSET(copy_initial_nonkeyframes)}, "copy initial non-keyframes" },
    { "frames", OPT_INT64 | HAS_ARG | OPT_SPEC, {.off = OFFSET(max_frames)}, "set the number of frames to record", "number" },
    { "tag",   OPT_STRING | HAS_ARG | OPT_SPEC, {.off = OFFSET(codec_tags)}, "force codec tag/fourcc", "fourcc/tag" },
    { "q", HAS_ARG | OPT_EXPERT | OPT_DOUBLE | OPT_SPEC, {.off = OFFSET(qscale)}, "use fixed quality scale (VBR)", "q" },
    { "qscale", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_qscale}, "use fixed quality scale (VBR)", "q" },
    { "profile", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_profile}, "set profile", "profile" },
    { "filter", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(filters)}, "set stream filterchain", "filter_list" },
    { "filter_complex", HAS_ARG | OPT_EXPERT, {(void*)opt_filter_complex}, "create a complex filtergraph", "graph_description" },
    { "stats", OPT_BOOL, {&print_stats}, "print progress report during encoding", },
    { "attach", HAS_ARG | OPT_FUNC2, {(void*)opt_attach}, "add an attachment to the output file", "filename" },
    { "dump_attachment", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(dump_attachment)}, "extract an attachment into a file", "filename" },
    { "debug_ts", OPT_BOOL | OPT_EXPERT, {&debug_ts}, "print timestamp debugging info" },

    /* video options */
    { "vframes", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_frames}, "set the number of video frames to record", "number" },
    { "r", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_rates)}, "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_sizes)}, "set frame size (WxH or abbreviation)", "size" },
    { "aspect", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_aspect_ratios)}, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_pix_fmts)}, "set pixel format", "format" },
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
    { "intra", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_only}, "deprecated use -g 1"},
    { "vn", OPT_BOOL | OPT_VIDEO | OPT_OFFSET, {.off = OFFSET(video_disable)}, "disable video" },
    { "vdt", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&video_discard}, "discard threshold", "n" },
    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(rc_overrides)}, "rate control override for specific intervals", "override" },
    { "vcodec", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
    { "sameq", OPT_BOOL | OPT_VIDEO, {(void*)&same_quant}, "use same quantizer as source (implies VBR)" },
    { "same_quant", OPT_BOOL | OPT_VIDEO, {(void*)&same_quant},
      "use same quantizer as source (implies VBR)" },
    { "pass", HAS_ARG | OPT_VIDEO, {(void*)opt_pass}, "select the pass number (1 or 2)", "n" },
    { "passlogfile", HAS_ARG | OPT_VIDEO, {(void*)&opt_passlogfile}, "select two pass log file name prefix", "prefix" },
    { "deinterlace", OPT_EXPERT | OPT_VIDEO, {(void*)opt_deinterlace},
      "this option is deprecated, use the yadif filter instead" },
    { "psnr", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
    { "vstats", OPT_EXPERT | OPT_VIDEO, {(void*)&opt_vstats}, "dump video coding statistics to file" },
    { "vstats_file", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_vstats_file}, "dump video coding statistics to file", "file" },
    { "vf", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_filters}, "video filters", "filter list" },
    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(intra_matrices)}, "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(inter_matrices)}, "specify inter matrix coeffs", "matrix" },
    { "top", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_INT| OPT_SPEC, {.off = OFFSET(top_field_first)}, "top=1/bottom=0/auto=-1 field first", "" },
    { "dc", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_dc_precision}, "intra_dc_precision", "precision" },
    { "vtag", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_FUNC2, {(void*)opt_old2new}, "force video tag/fourcc", "fourcc/tag" },
    { "qphist", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, { (void *)&qp_hist }, "show QP histogram" },
    { "force_fps", OPT_BOOL | OPT_EXPERT | OPT_VIDEO | OPT_SPEC, {.off = OFFSET(force_fps)}, "force the selected framerate, disable the best supported framerate selection" },
    { "streamid", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_streamid}, "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames", OPT_STRING | HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_SPEC, {.off = OFFSET(forced_key_frames)}, "force key frames at specified timestamps", "timestamps" },
    { "b", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_bitrate}, "video bitrate (please use -b:v)", "bitrate" },

    /* audio options */
    { "aframes", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_frames}, "set the number of audio frames to record", "number" },
    { "aq", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_qscale}, "set audio quality (codec-specific)", "quality", },
    { "ar", HAS_ARG | OPT_AUDIO | OPT_INT | OPT_SPEC, {.off = OFFSET(audio_sample_rate)}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG | OPT_AUDIO | OPT_INT | OPT_SPEC, {.off = OFFSET(audio_channels)}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL | OPT_AUDIO | OPT_OFFSET, {.off = OFFSET(audio_disable)}, "disable audio" },
    { "acodec", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_codec}, "force audio codec ('copy' to copy stream)", "codec" },
    { "atag", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_FUNC2, {(void*)opt_old2new}, "force audio tag/fourcc", "fourcc/tag" },
    { "vol", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&audio_volume}, "change audio volume (256=normal)" , "volume" }, //
    { "sample_fmt", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_SPEC | OPT_STRING, {.off = OFFSET(sample_fmts)}, "set sample format", "format" },
    { "channel_layout", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_FUNC2, {(void*)opt_channel_layout}, "set channel layout", "layout" },
    { "af", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_filters}, "audio filters", "filter list" },

    /* subtitle options */
    { "sn", OPT_BOOL | OPT_SUBTITLE | OPT_OFFSET, {.off = OFFSET(subtitle_disable)}, "disable subtitle" },
    { "scodec", HAS_ARG | OPT_SUBTITLE | OPT_FUNC2, {(void*)opt_subtitle_codec}, "force subtitle codec ('copy' to copy stream)", "codec" },
    { "stag", HAS_ARG | OPT_EXPERT | OPT_SUBTITLE | OPT_FUNC2, {(void*)opt_old2new}, "force subtitle tag/fourcc", "fourcc/tag" },

    /* grab options */
    { "vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_channel}, "deprecated, use -channel", "channel" },
    { "tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_standard}, "deprecated, use -standard", "standard" },
    { "isync", OPT_BOOL | OPT_EXPERT | OPT_GRAB, {(void*)&input_sync}, "sync read on input", "" },

    /* muxer options */
    { "muxdelay", OPT_FLOAT | HAS_ARG | OPT_EXPERT   | OPT_OFFSET, {.off = OFFSET(mux_max_delay)}, "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET, {.off = OFFSET(mux_preload)},   "set the initial demux-decode delay", "seconds" },

    { "bsf", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(bitstream_filters)}, "A comma-separated list of bitstream filters", "bitstream_filters" },
    { "absf", HAS_ARG | OPT_AUDIO | OPT_EXPERT| OPT_FUNC2, {(void*)opt_old2new}, "deprecated", "audio bitstream_filters" },
    { "vbsf", HAS_ARG | OPT_VIDEO | OPT_EXPERT| OPT_FUNC2, {(void*)opt_old2new}, "deprecated", "video bitstream_filters" },

    { "apre", HAS_ARG | OPT_AUDIO | OPT_EXPERT| OPT_FUNC2, {(void*)opt_preset}, "set the audio options to the indicated preset", "preset" },
    { "vpre", HAS_ARG | OPT_VIDEO | OPT_EXPERT| OPT_FUNC2, {(void*)opt_preset}, "set the video options to the indicated preset", "preset" },
    { "spre", HAS_ARG | OPT_SUBTITLE | OPT_EXPERT| OPT_FUNC2, {(void*)opt_preset}, "set the subtitle options to the indicated preset", "preset" },
    { "fpre", HAS_ARG | OPT_EXPERT| OPT_FUNC2, {(void*)opt_preset}, "set options from indicated preset file", "filename" },
    /* data codec support */
    { "dcodec", HAS_ARG | OPT_DATA | OPT_FUNC2, {(void*)opt_data_codec}, "force data codec ('copy' to copy stream)", "codec" },
    { "dn", OPT_BOOL | OPT_VIDEO | OPT_OFFSET, {.off = OFFSET(data_disable)}, "disable data" },

    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { NULL, },
};

int main(int argc, char **argv)
{
    OptionsContext o = { 0 };
    int64_t ti;

    reset_options(&o, 0);

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    if(argc>1 && !strcmp(argv[1], "-d")){
        run_as_daemon=1;
        av_log_set_callback(log_callback_null);
        argc--;
        argv++;
    }

    avcodec_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avfilter_register_all();
    av_register_all();
    avformat_network_init();

    show_banner(argc, argv, options);

    term_init();

    parse_cpuflags(argc, argv, options);

    /* parse options */
    parse_options(&o, argc, argv, options, opt_output_file);

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

    if (nb_input_files == 0) {
        av_log(NULL, AV_LOG_FATAL, "At least one input file must be specified\n");
        exit_program(1);
    }

    current_time = ti = getutime();
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
