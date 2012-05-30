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
#include <unistd.h>
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavresample/avresample.h"
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
#include "libavformat/os_support.h"

# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/buffersrc.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/vsrc_buffer.h"

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

#include <time.h>

#include "cmdutils.h"

#include "libavutil/avassert.h"

#define VSYNC_AUTO       -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR         1
#define VSYNC_VFR         2

const char program_name[] = "avconv";
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

/**
 * select an input file for an output file
 */
typedef struct MetadataMap {
    int  file;      ///< file index
    char type;      ///< type of metadata to copy -- (g)lobal, (s)tream, (c)hapter or (p)rogram
    int  index;     ///< stream/chapter/program number
} MetadataMap;

static const OptionDef options[];

static int video_discard = 0;
static int same_quant = 0;
static int do_deinterlace = 0;
static int intra_dc_precision = 8;
static int qp_hist = 0;

static int file_overwrite = 0;
static int do_benchmark = 0;
static int do_hex_dump = 0;
static int do_pkt_dump = 0;
static int do_pass = 0;
static char *pass_logfilename_prefix = NULL;
static int video_sync_method = VSYNC_AUTO;
static int audio_sync_method = 0;
static float audio_drift_threshold = 0.1;
static int copy_ts = 0;
static int copy_tb = 1;
static int opt_shortest = 0;
static char *vstats_filename;
static FILE *vstats_file;

static int audio_volume = 256;

static int exit_on_error = 0;
static int using_stdin = 0;
static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;
static int input_sync;

static float dts_delta_threshold = 10;

static int print_stats = 1;

#define DEFAULT_PASS_LOGFILENAME_PREFIX "av2pass"

typedef struct InputFilter {
    AVFilterContext    *filter;
    struct InputStream *ist;
    struct FilterGraph *graph;
    uint8_t            *name;
} InputFilter;

typedef struct OutputFilter {
    AVFilterContext     *filter;
    struct OutputStream *ost;
    struct FilterGraph  *graph;
    uint8_t             *name;

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
    int64_t       last_dts;
    PtsCorrectionContext pts_ctx;
    double ts_scale;
    int is_start;            /* is 1 at the start and after a discontinuity */
    int showed_multi_packet_warning;
    AVDictionary *opts;
    AVRational framerate;               /* framerate forced with -r */

    int resample_height;
    int resample_width;
    int resample_pix_fmt;

    int      resample_sample_fmt;
    int      resample_sample_rate;
    int      resample_channels;
    uint64_t resample_channel_layout;

    /* a pool of free buffers for decoded data */
    FrameBuffer *buffer_pool;

    /* decoded data from this stream goes into all those filters
     * currently video and audio only */
    InputFilter **filters;
    int        nb_filters;
} InputStream;

typedef struct InputFile {
    AVFormatContext *ctx;
    int eof_reached;      /* true if eof reached */
    int ist_index;        /* index of first stream in ist_table */
    int buffer_size;      /* current total buffer size */
    int64_t ts_offset;
    int nb_streams;       /* number of stream that avconv is aware of; may be different
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
    // double sync_ipts;        /* dts from the AVPacket of the demuxer in second units */
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

    FILE *logfile;

    OutputFilter *filter;
    char *avfilter;

    int64_t sws_flags;
    AVDictionary *opts;
    int is_past_recording_time;
    int stream_copy;
    const char *attachment_filename;
    int copy_initial_nonkeyframes;

    enum PixelFormat pix_fmts[2];
} OutputStream;


typedef struct OutputFile {
    AVFormatContext *ctx;
    AVDictionary *opts;
    int ost_index;       /* index of the first stream in output_streams */
    int64_t recording_time; /* desired length of the resulting file in microseconds */
    int64_t start_time;     /* start time in microseconds */
    uint64_t limit_filesize;
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
    /* first item specifies output metadata, second is input */
    MetadataMap (*meta_data_maps)[2];
    int nb_meta_data_maps;
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

static void reset_options(OptionsContext *o)
{
    const OptionDef *po = options;
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
    av_freep(&o->meta_data_maps);
    av_freep(&o->streamid_map);

    memset(o, 0, sizeof(*o));

    o->mux_max_delay  = 0.7;
    o->recording_time = INT64_MAX;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;

    uninit_opts();
    init_opts();
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

DEF_CHOOSE_FORMAT(enum PixelFormat, pix_fmt, pix_fmts, PIX_FMT_NONE,
                  GET_PIX_FMT_NAME, ":")

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

    ost->filter = fg->outputs[0];

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
    InputStream *ist;
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
            av_log(NULL, AV_LOG_FATAL, "Invalid file index %d in filtegraph description %s.\n",
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
    OutputStream *ost = ofilter->ost;
    AVCodecContext *codec = ost->st->codec;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    int ret;


    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("buffersink"),
                                       "out", NULL, pix_fmts, fg->graph);
    if (ret < 0)
        return ret;

    if (codec->width || codec->height) {
        char args[255];
        AVFilterContext *filter;

        snprintf(args, sizeof(args), "%d:%d:flags=0x%X",
                 codec->width,
                 codec->height,
                 (unsigned)ost->sws_flags);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                NULL, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    if ((pix_fmts = choose_pix_fmts(ost))) {
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

    if (ost->frame_rate.num) {
        AVFilterContext *fps;
        char args[255];

        snprintf(args, sizeof(args), "fps=%d/%d", ost->frame_rate.num,
                 ost->frame_rate.den);
        ret = avfilter_graph_create_filter(&fps, avfilter_get_by_name("fps"),
                                           "fps", args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, fps, 0);
        if (ret < 0)
            return ret;
        last_filter = fps;
        pad_idx = 0;
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

#define DESCRIBE_FILTER_LINK(f, inout, in)                         \
{                                                                  \
    AVFilterContext *ctx = inout->filter_ctx;                      \
    AVFilterPad *pads = in ? ctx->input_pads  : ctx->output_pads;  \
    int       nb_pads = in ? ctx->input_count : ctx->output_count; \
    AVIOContext *pb;                                               \
                                                                   \
    if (avio_open_dyn_buf(&pb) < 0)                                \
        exit_program(1);                                           \
                                                                   \
    avio_printf(pb, "%s", ctx->filter->name);                      \
    if (nb_pads > 1)                                               \
        avio_printf(pb, ":%s", pads[inout->pad_idx].name);         \
    avio_w8(pb, 0);                                                \
    avio_close_dyn_buf(pb, &f->name);                              \
}

static int configure_output_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    av_freep(&ofilter->name);
    DESCRIBE_FILTER_LINK(ofilter, out, 0);

    switch (out->filter_ctx->output_pads[out->pad_idx].type) {
    case AVMEDIA_TYPE_VIDEO: return configure_output_video_filter(fg, ofilter, out);
    case AVMEDIA_TYPE_AUDIO: return configure_output_audio_filter(fg, ofilter, out);
    default: av_assert0(0);
    }
}

static int configure_input_video_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *first_filter = in->filter_ctx;
    AVFilter *filter = avfilter_get_by_name("buffer");
    InputStream *ist = ifilter->ist;
    AVRational tb = ist->framerate.num ? (AVRational){ist->framerate.den,
                                                      ist->framerate.num} :
                                         ist->st->time_base;
    AVRational sar;
    char args[255];
    int pad_idx = in->pad_idx;
    int ret;

    sar = ist->st->sample_aspect_ratio.num ?
          ist->st->sample_aspect_ratio :
          ist->st->codec->sample_aspect_ratio;
    snprintf(args, sizeof(args), "%d:%d:%d:%d:%d:%d:%d", ist->st->codec->width,
             ist->st->codec->height, ist->st->codec->pix_fmt,
             tb.num, tb.den, sar.num, sar.den);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, filter, in->name,
                                            args, NULL, fg->graph)) < 0)
        return ret;

    if (ist->framerate.num) {
        AVFilterContext *setpts;

        if ((ret = avfilter_graph_create_filter(&setpts,
                                                avfilter_get_by_name("setpts"),
                                                "setpts", "N", NULL,
                                                fg->graph)) < 0)
            return ret;

        if ((ret = avfilter_link(setpts, 0, first_filter, pad_idx)) < 0)
            return ret;

        first_filter = setpts;
        pad_idx = 0;
    }

    if ((ret = avfilter_link(ifilter->filter, 0, first_filter, pad_idx)) < 0)
        return ret;
    return 0;
}

static int configure_input_audio_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *first_filter = in->filter_ctx;
    AVFilter *filter = avfilter_get_by_name("abuffer");
    InputStream *ist = ifilter->ist;
    int pad_idx = in->pad_idx;
    char args[255];
    int ret;

    snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s"
             ":channel_layout=0x%"PRIx64,
             ist->st->time_base.num, ist->st->time_base.den,
             ist->st->codec->sample_rate,
             av_get_sample_fmt_name(ist->st->codec->sample_fmt),
             ist->st->codec->channel_layout);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, filter,
                                            in->name, args, NULL,
                                            fg->graph)) < 0)
        return ret;

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

        ret = avfilter_link(async, 0, first_filter, pad_idx);
        if (ret < 0)
            return ret;

        first_filter = async;
        pad_idx = 0;
    }
    if ((ret = avfilter_link(ifilter->filter, 0, first_filter, pad_idx)) < 0)
        return ret;

    return 0;
}

static int configure_input_filter(FilterGraph *fg, InputFilter *ifilter,
                                  AVFilterInOut *in)
{
    av_freep(&ifilter->name);
    DESCRIBE_FILTER_LINK(ifilter, in, 1);

    switch (in->filter_ctx->input_pads[in->pad_idx].type) {
    case AVMEDIA_TYPE_VIDEO: return configure_input_video_filter(fg, ifilter, in);
    case AVMEDIA_TYPE_AUDIO: return configure_input_audio_filter(fg, ifilter, in);
    default: av_assert0(0);
    }
}

static int configure_filtergraph(FilterGraph *fg)
{
    AVFilterInOut *inputs, *outputs, *cur;
    int ret, i, init = !fg->graph, simple = !fg->graph_desc;
    const char *graph_desc = simple ? fg->outputs[0]->ost->avfilter :
                                      fg->graph_desc;

    avfilter_graph_free(&fg->graph);
    if (!(fg->graph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    if (simple) {
        OutputStream *ost = fg->outputs[0]->ost;
        char args[255];
        snprintf(args, sizeof(args), "flags=0x%X", (unsigned)ost->sws_flags);
        fg->graph->scale_sws_opts = av_strdup(args);
    }

    if ((ret = avfilter_graph_parse2(fg->graph, graph_desc, &inputs, &outputs)) < 0)
        return ret;

    if (simple && (!inputs || inputs->next || !outputs || outputs->next)) {
        av_log(NULL, AV_LOG_ERROR, "Simple filtergraph '%s' does not have "
               "exactly one input and output.\n", graph_desc);
        return AVERROR(EINVAL);
    }

    for (cur = inputs; !simple && init && cur; cur = cur->next)
        init_input_filter(fg, cur);

    for (cur = inputs, i = 0; cur; cur = cur->next, i++)
        if ((ret = configure_input_filter(fg, fg->inputs[i], cur)) < 0)
            return ret;
    avfilter_inout_free(&inputs);

    if (!init || simple) {
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
            (ret = configure_filtergraph(filtergraphs[i])) < 0)
            return ret;
    return 0;
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

static const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };

void exit_program(int ret)
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

        av_freep(&output_streams[i]->avfilter);
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
        free_buffer_pool(&input_streams[i]->buffer_pool);
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

/**
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
        int min_dec = -1, min_inc = -1;

        /* find a matching sample format in the encoder */
        for (p = dec_codec->sample_fmts; *p != AV_SAMPLE_FMT_NONE; p++) {
            if (*p == enc->sample_fmt) {
                dec->request_sample_fmt = *p;
                return;
            } else if (*p > enc->sample_fmt) {
                min_inc = FFMIN(min_inc, *p - enc->sample_fmt);
            } else
                min_dec = FFMIN(min_dec, enc->sample_fmt - *p);
        }

        /* if none match, provide the one that matches quality closest */
        dec->request_sample_fmt = min_inc > 0 ? enc->sample_fmt + min_inc :
                                  enc->sample_fmt - min_dec;
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
            new_pkt.destruct = av_destruct_packet;
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

    if (!check_recording_time(ost))
        return;

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
                         int *frame_size, float quality)
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

    if (!check_recording_time(ost) ||
        ost->frame_number >= ost->max_frames)
        return;

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
        ret = avcodec_encode_video2(enc, &pkt, &big_picture, &got_packet);
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
    int i, frame_size;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];
        OutputFile    *of = output_files[ost->file_index];
        int ret = 0;

        if (!ost->filter)
            continue;

        if (!ost->filtered_frame && !(ost->filtered_frame = avcodec_alloc_frame())) {
            return AVERROR(ENOMEM);
        } else
            avcodec_get_frame_defaults(ost->filtered_frame);
        filtered_frame = ost->filtered_frame;

        while (ret >= 0 && !ost->is_past_recording_time) {
            if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
                !(ost->enc->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE))
                ret = av_buffersink_read_samples(ost->filter->filter, &picref,
                                                 ost->st->codec->frame_size);
            else
                ret = av_buffersink_read(ost->filter->filter, &picref);

            if (ret < 0)
                break;

            avfilter_copy_buf_props(filtered_frame, picref);
            if (picref->pts != AV_NOPTS_VALUE)
                filtered_frame->pts = av_rescale_q(picref->pts,
                                                   ost->filter->filter->inputs[0]->time_base,
                                                   ost->st->codec->time_base) -
                                      av_rescale_q(of->start_time,
                                                   AV_TIME_BASE_Q,
                                                   ost->st->codec->time_base);

            if (of->start_time && filtered_frame->pts < of->start_time) {
                avfilter_unref_buffer(picref);
                continue;
            }

            switch (ost->filter->filter->inputs[0]->type) {
            case AVMEDIA_TYPE_VIDEO:
                if (!ost->frame_aspect_ratio)
                    ost->st->codec->sample_aspect_ratio = picref->video->pixel_aspect;

                do_video_out(of->ctx, ost, filtered_frame, &frame_size,
                             same_quant ? ost->last_quality :
                                          ost->st->codec->global_quality);
                if (vstats_filename && frame_size)
                    do_video_stats(of->ctx, ost, frame_size);
                break;
            case AVMEDIA_TYPE_AUDIO:
                do_audio_out(of->ctx, ost, filtered_frame);
                break;
            default:
                // TODO support subtitle filters
                av_assert0(0);
            }

            avfilter_unref_buffer(picref);
        }
    }
    return 0;
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
    if (total_size < 0) // FIXME improve avio_size() so it works with non seekable output too
        total_size = avio_tell(oc->pb);

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
        int64_t raw= audio_size + video_size + extra_size;
        av_log(NULL, AV_LOG_INFO, "\n");
        av_log(NULL, AV_LOG_INFO, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
               video_size / 1024.0,
               audio_size / 1024.0,
               extra_size / 1024.0,
               100.0 * (total_size - raw) / raw
        );
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

    if (of->start_time && ist->last_dts < of->start_time)
        return 0;

    return 1;
}

static void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
{
    OutputFile *of = output_files[ost->file_index];
    int64_t ost_tb_start_time = av_rescale_q(of->start_time, AV_TIME_BASE_Q, ost->st->time_base);
    AVPacket opkt;

    av_init_packet(&opkt);

    if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) &&
        !ost->copy_initial_nonkeyframes)
        return;

    if (of->recording_time != INT64_MAX &&
        ist->last_dts >= of->recording_time + of->start_time) {
        ost->is_past_recording_time = 1;
        return;
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

    write_frame(of->ctx, &opkt, ost);
    ost->st->codec->frame_number++;
    av_free_packet(&opkt);
}

static void rate_emu_sleep(InputStream *ist)
{
    if (input_files[ist->file_index]->rate_emu) {
        int64_t pts = av_rescale(ist->last_dts, 1000000, AV_TIME_BASE);
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
    int bps = av_get_bytes_per_sample(ist->st->codec->sample_fmt);
    int i, ret, resample_changed;

    if (!ist->decoded_frame && !(ist->decoded_frame = avcodec_alloc_frame()))
        return AVERROR(ENOMEM);
    else
        avcodec_get_frame_defaults(ist->decoded_frame);
    decoded_frame = ist->decoded_frame;

    ret = avcodec_decode_audio4(avctx, decoded_frame, got_output, pkt);
    if (ret < 0) {
        return ret;
    }

    if (!*got_output) {
        /* no audio frame */
        if (!pkt->size)
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_buffer(ist->filters[i]->filter, NULL);
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

    // preprocess audio (volume)
    if (audio_volume != 256) {
        int decoded_data_size = decoded_frame->nb_samples * avctx->channels * bps;
        void *samples = decoded_frame->data[0];
        switch (avctx->sample_fmt) {
        case AV_SAMPLE_FMT_U8:
        {
            uint8_t *volp = samples;
            for (i = 0; i < (decoded_data_size / sizeof(*volp)); i++) {
                int v = (((*volp - 128) * audio_volume + 128) >> 8) + 128;
                *volp++ = av_clip_uint8(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_S16:
        {
            int16_t *volp = samples;
            for (i = 0; i < (decoded_data_size / sizeof(*volp)); i++) {
                int v = ((*volp) * audio_volume + 128) >> 8;
                *volp++ = av_clip_int16(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_S32:
        {
            int32_t *volp = samples;
            for (i = 0; i < (decoded_data_size / sizeof(*volp)); i++) {
                int64_t v = (((int64_t)*volp * audio_volume + 128) >> 8);
                *volp++ = av_clipl_int32(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_FLT:
        {
            float *volp = samples;
            float scale = audio_volume / 256.f;
            for (i = 0; i < (decoded_data_size / sizeof(*volp)); i++) {
                *volp++ *= scale;
            }
            break;
        }
        case AV_SAMPLE_FMT_DBL:
        {
            double *volp = samples;
            double scale = audio_volume / 256.;
            for (i = 0; i < (decoded_data_size / sizeof(*volp)); i++) {
                *volp++ *= scale;
            }
            break;
        }
        default:
            av_log(NULL, AV_LOG_FATAL,
                   "Audio volume adjustment on sample format %s is not supported.\n",
                   av_get_sample_fmt_name(ist->st->codec->sample_fmt));
            exit_program(1);
        }
    }

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
        av_buffersrc_write_frame(ist->filters[i]->filter, decoded_frame);

    return ret;
}

static int decode_video(InputStream *ist, AVPacket *pkt, int *got_output)
{
    AVFrame *decoded_frame;
    void *buffer_to_free = NULL;
    int i, ret = 0, resample_changed;
    float quality;

    if (!ist->decoded_frame && !(ist->decoded_frame = avcodec_alloc_frame()))
        return AVERROR(ENOMEM);
    else
        avcodec_get_frame_defaults(ist->decoded_frame);
    decoded_frame = ist->decoded_frame;

    ret = avcodec_decode_video2(ist->st->codec,
                                decoded_frame, got_output, pkt);
    if (ret < 0)
        return ret;

    quality = same_quant ? decoded_frame->quality : 0;
    if (!*got_output) {
        /* no picture yet */
        if (!pkt->size)
            for (i = 0; i < ist->nb_filters; i++)
                av_buffersrc_buffer(ist->filters[i]->filter, NULL);
        return ret;
    }
    decoded_frame->pts = guess_correct_pts(&ist->pts_ctx, decoded_frame->pkt_pts,
                                           decoded_frame->pkt_dts);
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

    for (i = 0; i < ist->nb_filters; i++) {
        // XXX what an ugly hack
        if (ist->filters[i]->graph->nb_outputs == 1)
            ist->filters[i]->graph->outputs[0]->ost->last_quality = quality;

        if (ist->st->codec->codec->capabilities & CODEC_CAP_DR1) {
            FrameBuffer      *buf = decoded_frame->opaque;
            AVFilterBufferRef *fb = avfilter_get_video_buffer_ref_from_arrays(
                                        decoded_frame->data, decoded_frame->linesize,
                                        AV_PERM_READ | AV_PERM_PRESERVE,
                                        ist->st->codec->width, ist->st->codec->height,
                                        ist->st->codec->pix_fmt);

            avfilter_copy_frame_props(fb, decoded_frame);
            fb->buf->priv           = buf;
            fb->buf->free           = filter_release_buffer;

            buf->refcount++;
            av_buffersrc_buffer(ist->filters[i]->filter, fb);
        } else
            av_buffersrc_write_frame(ist->filters[i]->filter, decoded_frame);
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
            else if (ist->st->r_frame_rate.num)
                ist->next_dts += av_rescale_q(1, (AVRational){ist->st->r_frame_rate.den,
                                                              ist->st->r_frame_rate.num},
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
        rate_emu_sleep(ist);
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
    int i;
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

        if (codec->type == AVMEDIA_TYPE_VIDEO && codec->capabilities & CODEC_CAP_DR1) {
            ist->st->codec->get_buffer     = codec_get_buffer;
            ist->st->codec->release_buffer = codec_release_buffer;
            ist->st->codec->opaque         = &ist->buffer_pool;
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
            codec->extradata_size = icodec->extradata_size;
            if (!copy_tb) {
                codec->time_base      = icodec->time_base;
                codec->time_base.num *= icodec->ticks_per_frame;
                av_reduce(&codec->time_base.num, &codec->time_base.den,
                          codec->time_base.num, codec->time_base.den, INT_MAX);
            } else
                codec->time_base = ist->st->time_base;

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
                if (!codec->sample_aspect_ratio.num) {
                    codec->sample_aspect_ratio   =
                    ost->st->sample_aspect_ratio =
                        ist->st->sample_aspect_ratio.num ? ist->st->sample_aspect_ratio :
                        ist->st->codec->sample_aspect_ratio.num ?
                        ist->st->codec->sample_aspect_ratio : (AVRational){0, 1};
                }
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
                    if (configure_filtergraph(fg)) {
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
                codec->time_base = ost->filter->filter->inputs[0]->time_base;

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
                    codec->bits_per_raw_sample = 0;
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
            if ((codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2))) {
                char logfilename[1024];
                FILE *f;

                snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                         pass_logfilename_prefix ? pass_logfilename_prefix : DEFAULT_PASS_LOGFILENAME_PREFIX,
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
                                             "It takes bits/s as argument, not kbits/s\n");
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
        if ((ret = avformat_write_header(oc, &output_files[i]->opts)) < 0) {
            char errbuf[128];
            const char *errbuf_ptr = errbuf;
            if (av_strerror(ret, errbuf, sizeof(errbuf)) < 0)
                errbuf_ptr = strerror(AVUNERROR(ret));
            snprintf(error, sizeof(error), "Could not write header for output file #%d (incorrect codec parameters ?): %s", i, errbuf_ptr);
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

/**
 * @return 1 if there are still streams where more output is wanted,
 *         0 otherwise
 */
static int need_output(void)
{
    int i;

    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost    = output_streams[i];
        OutputFile *of       = output_files[ost->file_index];
        AVFormatContext *os  = output_files[ost->file_index]->ctx;

        if (ost->is_past_recording_time ||
            (os->pb && avio_tell(os->pb) >= of->limit_filesize))
            continue;
        if (ost->frame_number >= ost->max_frames) {
            int j;
            for (j = 0; j < of->ctx->nb_streams; j++)
                output_streams[of->ost_index + j]->is_past_recording_time = 1;
            continue;
        }

        return 1;
    }

    return 0;
}

static int select_input_file(uint8_t *no_packet)
{
    int64_t ipts_min = INT64_MAX;
    int i, file_index = -1;

    for (i = 0; i < nb_input_streams; i++) {
        InputStream *ist = input_streams[i];
        int64_t ipts     = ist->last_dts;

        if (ist->discard || no_packet[ist->file_index])
            continue;
        if (!input_files[ist->file_index]->eof_reached) {
            if (ipts < ipts_min) {
                ipts_min = ipts;
                file_index = ist->file_index;
            }
        }
    }

    return file_index;
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

    if (!(no_packet = av_mallocz(nb_input_files)))
        exit_program(1);

    ret = transcode_init();
    if (ret < 0)
        goto fail;

    av_log(NULL, AV_LOG_INFO, "Press ctrl-c to stop encoding\n");
    term_init();

    timer_start = av_gettime();

    for (; received_sigterm == 0;) {
        int file_index, ist_index;
        AVPacket pkt;

        /* check if there's any stream where output is still needed */
        if (!need_output()) {
            av_log(NULL, AV_LOG_VERBOSE, "No more output streams to write to, finishing.\n");
            break;
        }

        /* select the stream that we must read now */
        file_index = select_input_file(no_packet);
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

        //fprintf(stderr, "next:%"PRId64" dts:%"PRId64" off:%"PRId64" %d\n",
        //        ist->next_dts,
        //        pkt.dts, input_files[ist->file_index].ts_offset,
        //        ist->st->codec->codec_type);
        if (pkt.dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE
            && (is->iformat->flags & AVFMT_TS_DISCONT)) {
            int64_t pkt_dts = av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);
            int64_t delta   = pkt_dts - ist->next_dts;
            if ((FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE || pkt_dts + 1 < ist->last_dts) && !copy_ts) {
                input_files[ist->file_index]->ts_offset -= delta;
                av_log(NULL, AV_LOG_DEBUG,
                       "timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                       delta, input_files[ist->file_index]->ts_offset);
                pkt.dts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if (pkt.pts != AV_NOPTS_VALUE)
                    pkt.pts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
        }

        // fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->st->index, pkt.size);
        if (output_packet(ist, &pkt) < 0 || poll_filters() < 0) {
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
        print_report(0, timer_start);
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

static double parse_frame_aspect_ratio(const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;
    char *end;

    p = strchr(arg, ':');
    if (p) {
        x = strtol(arg, &end, 10);
        if (end == p)
            y = strtol(end + 1, &end, 10);
        if (x > 0 && y > 0)
            ar = (double)x / (double)y;
    } else
        ar = strtod(arg, NULL);

    if (!ar) {
        av_log(NULL, AV_LOG_FATAL, "Incorrect aspect ratio specification.\n");
        exit_program(1);
    }
    return ar;
}

static int opt_audio_codec(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "codec:a", arg, options);
}

static int opt_video_codec(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "codec:v", arg, options);
}

static int opt_subtitle_codec(OptionsContext *o, const char *opt, const char *arg)
{
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
    AVDictionary **meta_out;
    int i, ret = 0;
    char type_in, type_out;
    const char *istream_spec = NULL, *ostream_spec = NULL;
    int idx_in = 0, idx_out = 0;

    parse_meta_type(inspec,  &type_in,  &idx_in,  &istream_spec);
    parse_meta_type(outspec, &type_out, &idx_out, &ostream_spec);

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

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecContext *dec = st->codec;
        InputStream *ist = av_mallocz(sizeof(*ist));
        char *framerate = NULL;

        if (!ist)
            exit_program(1);

        input_streams = grow_array(input_streams, sizeof(*input_streams), &nb_input_streams, nb_input_streams + 1);
        input_streams[nb_input_streams - 1] = ist;

        ist->st = st;
        ist->file_index = nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->opts = filter_codec_opts(codec_opts, ist->st->codec->codec_id, ic, st);

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st);

        ist->dec = choose_decoder(o, ic, st);

        switch (dec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            ist->resample_height  = dec->height;
            ist->resample_width   = dec->width;
            ist->resample_pix_fmt = dec->pix_fmt;

            MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st);
            if (framerate && av_parse_video_rate(&ist->framerate,
                                                 framerate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error parsing framerate %s.\n",
                       framerate);
                exit_program(1);
            }

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
    if (!file_overwrite &&
        (strchr(filename, ':') == NULL || filename[1] == ':' ||
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
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    ic->interrupt_callback = int_cb;

    /* open the input file with generic libav function */
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

    reset_options(o);
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
    ost->forced_kf_pts   = av_malloc(sizeof(*ost->forced_kf_pts) * n);
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

static OutputStream *new_output_stream(OptionsContext *o, AVFormatContext *oc, enum AVMediaType type)
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
        ost->opts  = filter_codec_opts(codec_opts, ost->enc->id, oc, st);
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

    ost->pix_fmts[0] = ost->pix_fmts[1] = PIX_FMT_NONE;

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

static OutputStream *new_video_stream(OptionsContext *o, AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *video_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_VIDEO);
    st  = ost->st;
    video_enc = st->codec;

    if (!ost->stream_copy) {
        const char *p = NULL;
        char *forced_key_frames = NULL, *frame_rate = NULL, *frame_size = NULL;
        char *frame_aspect_ratio = NULL, *frame_pix_fmt = NULL;
        char *intra_matrix = NULL, *inter_matrix = NULL;
        const char *filters = "null";
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
        if (frame_aspect_ratio)
            ost->frame_aspect_ratio = parse_frame_aspect_ratio(frame_aspect_ratio);

        MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st);
        if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == PIX_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Unknown pixel format requested: %s.\n", frame_pix_fmt);
            exit_program(1);
        }
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

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

        /* two pass mode */
        if (do_pass) {
            if (do_pass == 1) {
                video_enc->flags |= CODEC_FLAG_PASS1;
            } else {
                video_enc->flags |= CODEC_FLAG_PASS2;
            }
        }

        MATCH_PER_STREAM_OPT(forced_key_frames, str, forced_key_frames, oc, st);
        if (forced_key_frames)
            parse_forced_key_frames(forced_key_frames, ost, video_enc);

        MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st);

        ost->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st);

        MATCH_PER_STREAM_OPT(filters, str, filters, oc, st);
        ost->avfilter = av_strdup(filters);
    } else {
        MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc ,st);
    }

    return ost;
}

static OutputStream *new_audio_stream(OptionsContext *o, AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *audio_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_AUDIO);
    st  = ost->st;

    audio_enc = st->codec;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    if (!ost->stream_copy) {
        char *sample_fmt = NULL;
        const char *filters = "anull";

        MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st);

        MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st);
        if (sample_fmt &&
            (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sample format '%s'\n", sample_fmt);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st);

        MATCH_PER_STREAM_OPT(filters, str, filters, oc, st);
        ost->avfilter = av_strdup(filters);
    }

    return ost;
}

static OutputStream *new_data_stream(OptionsContext *o, AVFormatContext *oc)
{
    OutputStream *ost;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_DATA);
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
        exit_program(1);
    }

    return ost;
}

static OutputStream *new_attachment_stream(OptionsContext *o, AVFormatContext *oc)
{
    OutputStream *ost = new_output_stream(o, oc, AVMEDIA_TYPE_ATTACHMENT);
    ost->stream_copy = 1;
    return ost;
}

static OutputStream *new_subtitle_stream(OptionsContext *o, AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *subtitle_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_SUBTITLE);
    st  = ost->st;
    subtitle_enc = st->codec;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

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
    idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, INT_MAX);
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
        os->chapters = av_realloc(os->chapters, sizeof(AVChapter) * os->nb_chapters);
        if (!os->chapters)
            return AVERROR(ENOMEM);
        os->chapters[os->nb_chapters - 1] = out_ch;
    }
    return 0;
}

static void init_output_filter(OutputFilter *ofilter, OptionsContext *o,
                               AVFormatContext *oc)
{
    OutputStream *ost;

    switch (ofilter->out_tmp->filter_ctx->output_pads[ofilter->out_tmp->pad_idx].type) {
    case AVMEDIA_TYPE_VIDEO: ost = new_video_stream(o, oc); break;
    case AVMEDIA_TYPE_AUDIO: ost = new_audio_stream(o, oc); break;
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

    oc = avformat_alloc_context();
    if (!oc) {
        print_error(filename, AVERROR(ENOMEM));
        exit_program(1);
    }

    if (o->format) {
        file_oformat = av_guess_format(o->format, NULL, NULL);
        if (!file_oformat) {
            av_log(NULL, AV_LOG_FATAL, "Requested output format '%s' is not a suitable output format\n", o->format);
            exit_program(1);
        }
    } else {
        file_oformat = av_guess_format(NULL, filename, NULL);
        if (!file_oformat) {
            av_log(NULL, AV_LOG_FATAL, "Unable to find a suitable output format for '%s'\n",
                   filename);
            exit_program(1);
        }
    }

    oc->oformat = file_oformat;
    oc->interrupt_callback = int_cb;
    av_strlcpy(oc->filename, filename, sizeof(oc->filename));

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

    if (!o->nb_stream_maps) {
        /* pick the "best" stream of each type */
#define NEW_STREAM(type, index)\
        if (index >= 0) {\
            ost = new_ ## type ## _stream(o, oc);\
            ost->source_index = index;\
            ost->sync_ist     = input_streams[index];\
            input_streams[index]->discard = 0;\
            input_streams[index]->st->discard = AVDISCARD_NONE;\
        }

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
            NEW_STREAM(video, idx);
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
            NEW_STREAM(audio, idx);
        }

        /* subtitles: pick first */
        if (!o->subtitle_disable && oc->oformat->subtitle_codec != CODEC_ID_NONE) {
            for (i = 0; i < nb_input_streams; i++)
                if (input_streams[i]->st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    NEW_STREAM(subtitle, i);
                    break;
                }
        }
        /* do something with data? */
    } else {
        for (i = 0; i < o->nb_stream_maps; i++) {
            StreamMap *map = &o->stream_maps[i];

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
                switch (ist->st->codec->codec_type) {
                case AVMEDIA_TYPE_VIDEO:    ost = new_video_stream(o, oc);    break;
                case AVMEDIA_TYPE_AUDIO:    ost = new_audio_stream(o, oc);    break;
                case AVMEDIA_TYPE_SUBTITLE: ost = new_subtitle_stream(o, oc); break;
                case AVMEDIA_TYPE_DATA:     ost = new_data_stream(o, oc);     break;
                case AVMEDIA_TYPE_ATTACHMENT: ost = new_attachment_stream(o, oc); break;
                default:
                    av_log(NULL, AV_LOG_FATAL, "Cannot map stream #%d:%d - unsupported type.\n",
                           map->file_index, map->stream_index);
                    exit_program(1);
                }

                ost->source_index = input_files[map->file_index]->ist_index + map->stream_index;
                ost->sync_ist     = input_streams[input_files[map->sync_file_index]->ist_index +
                                               map->sync_stream_index];
                ist->discard = 0;
                ist->st->discard = AVDISCARD_NONE;
            }
        }
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

        ost = new_attachment_stream(o, oc);
        ost->stream_copy               = 0;
        ost->source_index              = -1;
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
    oc->flags |= AVFMT_FLAG_NONBLOCK;

    /* copy metadata */
    for (i = 0; i < o->nb_metadata_map; i++) {
        char *p;
        int in_file_index = strtol(o->metadata_map[i].u.str, &p, 0);

        if (in_file_index < 0)
            continue;
        if (in_file_index >= nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d while processing metadata maps\n", in_file_index);
            exit_program(1);
        }
        copy_metadata(o->metadata_map[i].specifier, *p ? p + 1 : p, oc, input_files[in_file_index]->ctx, o);
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
    if (!o->metadata_global_manual && nb_input_files)
        av_dict_copy(&oc->metadata, input_files[0]->ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
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
        int index = 0, j, ret;

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

    reset_options(o);
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

static int opt_audio_qscale(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "q:a", arg, options);
}

static void show_usage(void)
{
    printf("Hyper fast Audio and Video encoder\n");
    printf("usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
    printf("\n");
}

static void show_help(void)
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

        opt_default("b", "1150000");
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
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b", "2040000");
        opt_default("maxrate", "2516000");
        opt_default("minrate", "0"); // 1145000;
        opt_default("bufsize", "1835008"); // 224*1024*8;
        opt_default("flags", "+scan_offset");


        opt_default("b:a", "224000");
        parse_option(o, "ar", "44100", options);

        opt_default("packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "ac3");
        parse_option(o, "f", "dvd", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default("g", norm == PAL ? "15" : "18");

        opt_default("b", "6000000");
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

static int opt_video_tag(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "tag:v", arg, options);
}

static int opt_audio_tag(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "tag:a", arg, options);
}

static int opt_subtitle_tag(OptionsContext *o, const char *opt, const char *arg)
{
    return parse_option(o, "tag:s", arg, options);
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

static int opt_cpuflags(const char *opt, const char *arg)
{
    int flags = av_parse_cpu_flags(arg);

    if (flags < 0)
        return flags;

    av_set_cpu_flags_mask(flags);
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
    { "c", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(codec_names)}, "codec name", "codec" },
    { "codec", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(codec_names)}, "codec name", "codec" },
    { "pre", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(presets)}, "preset name", "preset" },
    { "map", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_map}, "set input stream mapping", "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
    { "map_metadata", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(metadata_map)}, "set metadata information of outfile from infile",
      "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",  OPT_INT | HAS_ARG | OPT_EXPERT | OPT_OFFSET, {.off = OFFSET(chapters_input_file)},  "set chapters mapping", "input_file_index" },
    { "t", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(recording_time)}, "record or transcode \"duration\" seconds of audio/video", "duration" },
    { "fs", HAS_ARG | OPT_INT64 | OPT_OFFSET, {.off = OFFSET(limit_filesize)}, "set the limit file size in bytes", "limit_size" }, //
    { "ss", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(start_time)}, "set the start time offset", "time_off" },
    { "itsoffset", HAS_ARG | OPT_TIME | OPT_OFFSET, {.off = OFFSET(input_ts_offset)}, "set the input ts offset", "time_off" },
    { "itsscale", HAS_ARG | OPT_DOUBLE | OPT_SPEC, {.off = OFFSET(ts_scale)}, "set the input ts scale", "scale" },
    { "metadata", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(metadata)}, "add metadata", "string=string" },
    { "dframes", HAS_ARG | OPT_FUNC2, {(void*)opt_data_frames}, "set the number of data frames to record", "number" },
    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark},
      "add timings for benchmarking" },
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
    { "copytb", OPT_BOOL | OPT_EXPERT, {(void*)&copy_tb}, "copy input stream time base when stream copying" },
    { "shortest", OPT_BOOL | OPT_EXPERT, {(void*)&opt_shortest}, "finish encoding within shortest input" }, //
    { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&dts_delta_threshold}, "timestamp discontinuity delta threshold", "threshold" },
    { "xerror", OPT_BOOL, {(void*)&exit_on_error}, "exit on error", "error" },
    { "copyinkf", OPT_BOOL | OPT_EXPERT | OPT_SPEC, {.off = OFFSET(copy_initial_nonkeyframes)}, "copy initial non-keyframes" },
    { "frames", OPT_INT64 | HAS_ARG | OPT_SPEC, {.off = OFFSET(max_frames)}, "set the number of frames to record", "number" },
    { "tag",   OPT_STRING | HAS_ARG | OPT_SPEC, {.off = OFFSET(codec_tags)}, "force codec tag/fourcc", "fourcc/tag" },
    { "q", HAS_ARG | OPT_EXPERT | OPT_DOUBLE | OPT_SPEC, {.off = OFFSET(qscale)}, "use fixed quality scale (VBR)", "q" },
    { "qscale", HAS_ARG | OPT_EXPERT | OPT_DOUBLE | OPT_SPEC, {.off = OFFSET(qscale)}, "use fixed quality scale (VBR)", "q" },
    { "filter", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(filters)}, "set stream filterchain", "filter_list" },
    { "filter_complex", HAS_ARG | OPT_EXPERT, {(void*)opt_filter_complex}, "create a complex filtergraph", "graph_description" },
    { "stats", OPT_BOOL, {&print_stats}, "print progress report during encoding", },
    { "attach", HAS_ARG | OPT_FUNC2, {(void*)opt_attach}, "add an attachment to the output file", "filename" },
    { "dump_attachment", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(dump_attachment)}, "extract an attachment into a file", "filename" },
    { "cpuflags", HAS_ARG | OPT_EXPERT, {(void*)opt_cpuflags}, "set CPU flags mask", "mask" },

    /* video options */
    { "vframes", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_frames}, "set the number of video frames to record", "number" },
    { "r", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_rates)}, "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_sizes)}, "set frame size (WxH or abbreviation)", "size" },
    { "aspect", HAS_ARG | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_aspect_ratios)}, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(frame_pix_fmts)}, "set pixel format", "format" },
    { "vn", OPT_BOOL | OPT_VIDEO | OPT_OFFSET, {.off = OFFSET(video_disable)}, "disable video" },
    { "vdt", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&video_discard}, "discard threshold", "n" },
    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(rc_overrides)}, "rate control override for specific intervals", "override" },
    { "vcodec", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
    { "same_quant", OPT_BOOL | OPT_VIDEO, {(void*)&same_quant},
      "use same quantizer as source (implies VBR)" },
    { "pass", HAS_ARG | OPT_VIDEO, {(void*)opt_pass}, "select the pass number (1 or 2)", "n" },
    { "passlogfile", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void*)&pass_logfilename_prefix}, "select two pass log file name prefix", "prefix" },
    { "deinterlace", OPT_EXPERT | OPT_VIDEO, {(void*)opt_deinterlace},
      "this option is deprecated, use the yadif filter instead" },
    { "vstats", OPT_EXPERT | OPT_VIDEO, {(void*)&opt_vstats}, "dump video coding statistics to file" },
    { "vstats_file", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_vstats_file}, "dump video coding statistics to file", "file" },
    { "vf", HAS_ARG | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_filters}, "video filters", "filter list" },
    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(intra_matrices)}, "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_STRING | OPT_SPEC, {.off = OFFSET(inter_matrices)}, "specify inter matrix coeffs", "matrix" },
    { "top", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_INT| OPT_SPEC, {.off = OFFSET(top_field_first)}, "top=1/bottom=0/auto=-1 field first", "" },
    { "dc", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_dc_precision}, "intra_dc_precision", "precision" },
    { "vtag", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_FUNC2, {(void*)opt_video_tag}, "force video tag/fourcc", "fourcc/tag" },
    { "qphist", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, { (void *)&qp_hist }, "show QP histogram" },
    { "force_fps", OPT_BOOL | OPT_EXPERT | OPT_VIDEO | OPT_SPEC, {.off = OFFSET(force_fps)}, "force the selected framerate, disable the best supported framerate selection" },
    { "streamid", HAS_ARG | OPT_EXPERT | OPT_FUNC2, {(void*)opt_streamid}, "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames", OPT_STRING | HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_SPEC, {.off = OFFSET(forced_key_frames)}, "force key frames at specified timestamps", "timestamps" },

    /* audio options */
    { "aframes", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_frames}, "set the number of audio frames to record", "number" },
    { "aq", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_qscale}, "set audio quality (codec-specific)", "quality", },
    { "ar", HAS_ARG | OPT_AUDIO | OPT_INT | OPT_SPEC, {.off = OFFSET(audio_sample_rate)}, "set audio sampling rate (in Hz)", "rate" },
    { "ac", HAS_ARG | OPT_AUDIO | OPT_INT | OPT_SPEC, {.off = OFFSET(audio_channels)}, "set number of audio channels", "channels" },
    { "an", OPT_BOOL | OPT_AUDIO | OPT_OFFSET, {.off = OFFSET(audio_disable)}, "disable audio" },
    { "acodec", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_codec}, "force audio codec ('copy' to copy stream)", "codec" },
    { "atag", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_tag}, "force audio tag/fourcc", "fourcc/tag" },
    { "vol", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&audio_volume}, "change audio volume (256=normal)" , "volume" }, //
    { "sample_fmt", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_SPEC | OPT_STRING, {.off = OFFSET(sample_fmts)}, "set sample format", "format" },
    { "channel_layout", HAS_ARG | OPT_EXPERT | OPT_AUDIO | OPT_FUNC2, {(void*)opt_channel_layout}, "set channel layout", "layout" },
    { "af", HAS_ARG | OPT_AUDIO | OPT_FUNC2, {(void*)opt_audio_filters}, "audio filters", "filter list" },

    /* subtitle options */
    { "sn", OPT_BOOL | OPT_SUBTITLE | OPT_OFFSET, {.off = OFFSET(subtitle_disable)}, "disable subtitle" },
    { "scodec", HAS_ARG | OPT_SUBTITLE | OPT_FUNC2, {(void*)opt_subtitle_codec}, "force subtitle codec ('copy' to copy stream)", "codec" },
    { "stag", HAS_ARG | OPT_EXPERT | OPT_SUBTITLE | OPT_FUNC2, {(void*)opt_subtitle_tag}, "force subtitle tag/fourcc", "fourcc/tag" },

    /* grab options */
    { "isync", OPT_BOOL | OPT_EXPERT | OPT_GRAB, {(void*)&input_sync}, "sync read on input", "" },

    /* muxer options */
    { "muxdelay", OPT_FLOAT | HAS_ARG | OPT_EXPERT   | OPT_OFFSET, {.off = OFFSET(mux_max_delay)}, "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET, {.off = OFFSET(mux_preload)},   "set the initial demux-decode delay", "seconds" },

    { "bsf", HAS_ARG | OPT_STRING | OPT_SPEC, {.off = OFFSET(bitstream_filters)}, "A comma-separated list of bitstream filters", "bitstream_filters" },

    /* data codec support */
    { "dcodec", HAS_ARG | OPT_DATA | OPT_FUNC2, {(void*)opt_data_codec}, "force data codec ('copy' to copy stream)", "codec" },

    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { NULL, },
};

int main(int argc, char **argv)
{
    OptionsContext o = { 0 };
    int64_t ti;

    reset_options(&o);

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
        fprintf(stderr, "At least one output file must be specified\n");
        exit_program(1);
    }

    if (nb_input_files == 0) {
        av_log(NULL, AV_LOG_FATAL, "At least one input file must be specified\n");
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
