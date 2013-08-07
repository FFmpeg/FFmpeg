/*
 * ffmpeg filter configuration
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

#include "ffmpeg.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"

#include "libavresample/avresample.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"

enum AVPixelFormat choose_pixel_fmt(AVStream *st, AVCodec *codec, enum AVPixelFormat target)
{
    if (codec && codec->pix_fmts) {
        const enum AVPixelFormat *p = codec->pix_fmts;
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(target);
        int has_alpha = desc ? desc->nb_components % 2 == 0 : 0;
        enum AVPixelFormat best= AV_PIX_FMT_NONE;
        if (st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            if (st->codec->codec_id == AV_CODEC_ID_MJPEG) {
                p = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_NONE };
            } else if (st->codec->codec_id == AV_CODEC_ID_LJPEG) {
                p = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV420P,
                                                 AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE };
            }
        }
        for (; *p != AV_PIX_FMT_NONE; p++) {
            best= avcodec_find_best_pix_fmt_of_2(best, *p, target, has_alpha, NULL);
            if (*p == target)
                break;
        }
        if (*p == AV_PIX_FMT_NONE) {
            if (target != AV_PIX_FMT_NONE)
                av_log(NULL, AV_LOG_WARNING,
                       "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
                       av_get_pix_fmt_name(target),
                       codec->name,
                       av_get_pix_fmt_name(best));
            return best;
        }
    }
    return target;
}

void choose_sample_fmt(AVStream *st, AVCodec *codec)
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

static char *choose_pix_fmts(OutputStream *ost)
{
     if (ost->keep_pix_fmt) {
        if (ost->filter)
            avfilter_graph_set_auto_convert(ost->filter->graph->graph,
                                            AVFILTER_AUTO_CONVERT_NONE);
        if (ost->st->codec->pix_fmt == AV_PIX_FMT_NONE)
            return NULL;
        return av_strdup(av_get_pix_fmt_name(ost->st->codec->pix_fmt));
    }
    if (ost->st->codec->pix_fmt != AV_PIX_FMT_NONE) {
        return av_strdup(av_get_pix_fmt_name(choose_pixel_fmt(ost->st, ost->enc, ost->st->codec->pix_fmt)));
    } else if (ost->enc && ost->enc->pix_fmts) {
        const enum AVPixelFormat *p;
        AVIOContext *s = NULL;
        uint8_t *ret;
        int len;

        if (avio_open_dyn_buf(&s) < 0)
            exit_program(1);

        p = ost->enc->pix_fmts;
        if (ost->st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            if (ost->st->codec->codec_id == AV_CODEC_ID_MJPEG) {
                p = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_NONE };
            } else if (ost->st->codec->codec_id == AV_CODEC_ID_LJPEG) {
                p = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE };
            }
        }

        for (; *p != AV_PIX_FMT_NONE; p++) {
            const char *name = av_get_pix_fmt_name(*p);
            avio_printf(s, "%s|", name);
        }
        len = avio_close_dyn_buf(s, &ret);
        ret[len - 1] = 0;
        return ret;
    } else
        return NULL;
}

/* Define a function for building a string containing a list of
 * allowed formats. */
#define DEF_CHOOSE_FORMAT(type, var, supported_list, none, get_name)           \
static char *choose_ ## var ## s(OutputStream *ost)                            \
{                                                                              \
    if (ost->st->codec->var != none) {                                         \
        get_name(ost->st->codec->var);                                         \
        return av_strdup(name);                                                \
    } else if (ost->enc && ost->enc->supported_list) {                         \
        const type *p;                                                         \
        AVIOContext *s = NULL;                                                 \
        uint8_t *ret;                                                          \
        int len;                                                               \
                                                                               \
        if (avio_open_dyn_buf(&s) < 0)                                         \
            exit_program(1);                                                           \
                                                                               \
        for (p = ost->enc->supported_list; *p != none; p++) {                  \
            get_name(*p);                                                      \
            avio_printf(s, "%s|", name);                                       \
        }                                                                      \
        len = avio_close_dyn_buf(s, &ret);                                     \
        ret[len - 1] = 0;                                                      \
        return ret;                                                            \
    } else                                                                     \
        return NULL;                                                           \
}

// DEF_CHOOSE_FORMAT(enum AVPixelFormat, pix_fmt, pix_fmts, AV_PIX_FMT_NONE,
//                   GET_PIX_FMT_NAME)

DEF_CHOOSE_FORMAT(enum AVSampleFormat, sample_fmt, sample_fmts,
                  AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME)

DEF_CHOOSE_FORMAT(int, sample_rate, supported_samplerates, 0,
                  GET_SAMPLE_RATE_NAME)

DEF_CHOOSE_FORMAT(uint64_t, channel_layout, channel_layouts, 0,
                  GET_CH_LAYOUT_NAME)

FilterGraph *init_simple_filtergraph(InputStream *ist, OutputStream *ost)
{
    FilterGraph *fg = av_mallocz(sizeof(*fg));

    if (!fg)
        exit_program(1);
    fg->index = nb_filtergraphs;

    GROW_ARRAY(fg->outputs, fg->nb_outputs);
    if (!(fg->outputs[0] = av_mallocz(sizeof(*fg->outputs[0]))))
        exit_program(1);
    fg->outputs[0]->ost   = ost;
    fg->outputs[0]->graph = fg;

    ost->filter = fg->outputs[0];

    GROW_ARRAY(fg->inputs, fg->nb_inputs);
    if (!(fg->inputs[0] = av_mallocz(sizeof(*fg->inputs[0]))))
        exit_program(1);
    fg->inputs[0]->ist   = ist;
    fg->inputs[0]->graph = fg;

    GROW_ARRAY(ist->filters, ist->nb_filters);
    ist->filters[ist->nb_filters - 1] = fg->inputs[0];

    GROW_ARRAY(filtergraphs, nb_filtergraphs);
    filtergraphs[nb_filtergraphs - 1] = fg;

    return fg;
}

static void init_input_filter(FilterGraph *fg, AVFilterInOut *in)
{
    InputStream *ist = NULL;
    enum AVMediaType type = avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx);
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
            enum AVMediaType stream_type = s->streams[i]->codec->codec_type;
            if (stream_type != type &&
                !(stream_type == AVMEDIA_TYPE_SUBTITLE &&
                  type == AVMEDIA_TYPE_VIDEO /* sub2video hack */))
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
                   "unlabeled input pad %d on filter %s\n", in->pad_idx,
                   in->filter_ctx->name);
            exit_program(1);
        }
    }
    av_assert0(ist);

    ist->discard         = 0;
    ist->decoding_needed++;
    ist->st->discard = AVDISCARD_NONE;

    GROW_ARRAY(fg->inputs, fg->nb_inputs);
    if (!(fg->inputs[fg->nb_inputs - 1] = av_mallocz(sizeof(*fg->inputs[0]))))
        exit_program(1);
    fg->inputs[fg->nb_inputs - 1]->ist   = ist;
    fg->inputs[fg->nb_inputs - 1]->graph = fg;

    GROW_ARRAY(ist->filters, ist->nb_filters);
    ist->filters[ist->nb_filters - 1] = fg->inputs[fg->nb_inputs - 1];
}

static int insert_trim(int64_t start_time, int64_t duration,
                       AVFilterContext **last_filter, int *pad_idx,
                       const char *filter_name)
{
    AVFilterGraph *graph = (*last_filter)->graph;
    AVFilterContext *ctx;
    const AVFilter *trim;
    enum AVMediaType type = avfilter_pad_get_type((*last_filter)->output_pads, *pad_idx);
    const char *name = (type == AVMEDIA_TYPE_VIDEO) ? "trim" : "atrim";
    int ret = 0;

    if (duration == INT64_MAX && start_time == AV_NOPTS_VALUE)
        return 0;

    trim = avfilter_get_by_name(name);
    if (!trim) {
        av_log(NULL, AV_LOG_ERROR, "%s filter not present, cannot limit "
               "recording time.\n", name);
        return AVERROR_FILTER_NOT_FOUND;
    }

    ctx = avfilter_graph_alloc_filter(graph, trim, filter_name);
    if (!ctx)
        return AVERROR(ENOMEM);

    if (duration != INT64_MAX) {
        ret = av_opt_set_double(ctx, "duration", (double)duration / 1e6,
                                AV_OPT_SEARCH_CHILDREN);
    }
    if (ret >= 0 && start_time != AV_NOPTS_VALUE) {
        ret = av_opt_set_double(ctx, "start", (double)start_time / 1e6,
                                AV_OPT_SEARCH_CHILDREN);
    }
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error configuring the %s filter", name);
        return ret;
    }

    ret = avfilter_init_str(ctx, NULL);
    if (ret < 0)
        return ret;

    ret = avfilter_link(*last_filter, *pad_idx, ctx, 0);
    if (ret < 0)
        return ret;

    *last_filter = ctx;
    *pad_idx     = 0;
    return 0;
}

static int configure_output_video_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    char *pix_fmts;
    OutputStream *ost = ofilter->ost;
    OutputFile    *of = output_files[ost->file_index];
    AVCodecContext *codec = ost->st->codec;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    int ret;
    char name[255];

    snprintf(name, sizeof(name), "output stream %d:%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("buffersink"),
                                       name, NULL, NULL, fg->graph);

    if (ret < 0)
        return ret;

    if (codec->width || codec->height) {
        char args[255];
        AVFilterContext *filter;

        snprintf(args, sizeof(args), "%d:%d:0x%X",
                 codec->width,
                 codec->height,
                 (unsigned)ost->sws_flags);
        snprintf(name, sizeof(name), "scaler for output stream %d:%d",
                 ost->file_index, ost->index);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                name, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    if ((pix_fmts = choose_pix_fmts(ost))) {
        AVFilterContext *filter;
        snprintf(name, sizeof(name), "pixel format for output stream %d:%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&filter,
                                                avfilter_get_by_name("format"),
                                                "format", pix_fmts, NULL,
                                                fg->graph);
        av_freep(&pix_fmts);
        if (ret < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx     = 0;
    }

    if (ost->frame_rate.num && 0) {
        AVFilterContext *fps;
        char args[255];

        snprintf(args, sizeof(args), "fps=%d/%d", ost->frame_rate.num,
                 ost->frame_rate.den);
        snprintf(name, sizeof(name), "fps for output stream %d:%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&fps, avfilter_get_by_name("fps"),
                                           name, args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, fps, 0);
        if (ret < 0)
            return ret;
        last_filter = fps;
        pad_idx = 0;
    }

    snprintf(name, sizeof(name), "trim for output stream %d:%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;


    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_audio_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputStream *ost = ofilter->ost;
    OutputFile    *of = output_files[ost->file_index];
    AVCodecContext *codec  = ost->st->codec;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    char *sample_fmts, *sample_rates, *channel_layouts;
    char name[255];
    int ret;

    snprintf(name, sizeof(name), "output stream %d:%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("abuffersink"),
                                       name, NULL, NULL, fg->graph);
    if (ret < 0)
        return ret;
    if ((ret = av_opt_set_int(ofilter->filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        return ret;

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
    ret = avfilter_link(last_filter, pad_idx, filt_ctx, 0);                 \
    if (ret < 0)                                                            \
        return ret;                                                         \
                                                                            \
    last_filter = filt_ctx;                                                 \
    pad_idx = 0;                                                            \
} while (0)
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

    if (codec->channels && !codec->channel_layout)
        codec->channel_layout = av_get_default_channel_layout(codec->channels);

    sample_fmts     = choose_sample_fmts(ost);
    sample_rates    = choose_sample_rates(ost);
    channel_layouts = choose_channel_layouts(ost);
    if (sample_fmts || sample_rates || channel_layouts) {
        AVFilterContext *format;
        char args[256];
        args[0] = 0;

        if (sample_fmts)
            av_strlcatf(args, sizeof(args), "sample_fmts=%s:",
                            sample_fmts);
        if (sample_rates)
            av_strlcatf(args, sizeof(args), "sample_rates=%s:",
                            sample_rates);
        if (channel_layouts)
            av_strlcatf(args, sizeof(args), "channel_layouts=%s:",
                            channel_layouts);

        av_freep(&sample_fmts);
        av_freep(&sample_rates);
        av_freep(&channel_layouts);

        snprintf(name, sizeof(name), "audio format for output stream %d:%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           name, args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, format, 0);
        if (ret < 0)
            return ret;

        last_filter = format;
        pad_idx = 0;
    }

    if (audio_volume != 256 && 0) {
        char args[256];

        snprintf(args, sizeof(args), "%f", audio_volume / 256.);
        AUTO_INSERT_FILTER("-vol", "volume", args);
    }

    if (ost->apad && of->shortest) {
        char args[256];
        int i;

        for (i=0; i<of->ctx->nb_streams; i++)
            if (of->ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                break;

        if (i<of->ctx->nb_streams) {
            snprintf(args, sizeof(args), "%s", ost->apad);
            AUTO_INSERT_FILTER("-apad", "apad", args);
        }
    }

    snprintf(name, sizeof(name), "trim for output stream %d:%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

#define DESCRIBE_FILTER_LINK(f, inout, in)                         \
{                                                                  \
    AVFilterContext *ctx = inout->filter_ctx;                      \
    AVFilterPad *pads = in ? ctx->input_pads  : ctx->output_pads;  \
    int       nb_pads = in ? ctx->nb_inputs   : ctx->nb_outputs;   \
    AVIOContext *pb;                                               \
                                                                   \
    if (avio_open_dyn_buf(&pb) < 0)                                \
        exit_program(1);                                           \
                                                                   \
    avio_printf(pb, "%s", ctx->filter->name);                      \
    if (nb_pads > 1)                                               \
        avio_printf(pb, ":%s", avfilter_pad_get_name(pads, inout->pad_idx));\
    avio_w8(pb, 0);                                                \
    avio_close_dyn_buf(pb, &f->name);                              \
}

int configure_output_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    av_freep(&ofilter->name);
    DESCRIBE_FILTER_LINK(ofilter, out, 0);

    switch (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx)) {
    case AVMEDIA_TYPE_VIDEO: return configure_output_video_filter(fg, ofilter, out);
    case AVMEDIA_TYPE_AUDIO: return configure_output_audio_filter(fg, ofilter, out);
    default: av_assert0(0);
    }
}

static int sub2video_prepare(InputStream *ist)
{
    AVFormatContext *avf = input_files[ist->file_index]->ctx;
    int i, w, h;

    /* Compute the size of the canvas for the subtitles stream.
       If the subtitles codec has set a size, use it. Otherwise use the
       maximum dimensions of the video streams in the same file. */
    w = ist->st->codec->width;
    h = ist->st->codec->height;
    if (!(w && h)) {
        for (i = 0; i < avf->nb_streams; i++) {
            if (avf->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                w = FFMAX(w, avf->streams[i]->codec->width);
                h = FFMAX(h, avf->streams[i]->codec->height);
            }
        }
        if (!(w && h)) {
            w = FFMAX(w, 720);
            h = FFMAX(h, 576);
        }
        av_log(avf, AV_LOG_INFO, "sub2video: using %dx%d canvas\n", w, h);
    }
    ist->sub2video.w = ist->st->codec->width  = ist->resample_width  = w;
    ist->sub2video.h = ist->st->codec->height = ist->resample_height = h;

    /* rectangles are AV_PIX_FMT_PAL8, but we have no guarantee that the
       palettes for all rectangles are identical or compatible */
    ist->resample_pix_fmt = ist->st->codec->pix_fmt = AV_PIX_FMT_RGB32;

    ist->sub2video.frame = av_frame_alloc();
    if (!ist->sub2video.frame)
        return AVERROR(ENOMEM);
    return 0;
}

static int configure_input_video_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *last_filter;
    const AVFilter *buffer_filt = avfilter_get_by_name("buffer");
    InputStream *ist = ifilter->ist;
    InputFile     *f = input_files[ist->file_index];
    AVRational tb = ist->framerate.num ? av_inv_q(ist->framerate) :
                                         ist->st->time_base;
    AVRational fr = ist->framerate;
    AVRational sar;
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;

    if (!fr.num)
        fr = av_guess_frame_rate(input_files[ist->file_index]->ctx, ist->st, NULL);

    if (ist->st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        ret = sub2video_prepare(ist);
        if (ret < 0)
            return ret;
    }

    sar = ist->st->sample_aspect_ratio.num ?
          ist->st->sample_aspect_ratio :
          ist->st->codec->sample_aspect_ratio;
    if(!sar.den)
        sar = (AVRational){0,1};
    av_bprint_init(&args, 0, 1);
    av_bprintf(&args,
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:"
             "pixel_aspect=%d/%d:sws_param=flags=%d", ist->resample_width,
             ist->resample_height, ist->resample_pix_fmt,
             tb.num, tb.den, sar.num, sar.den,
             SWS_BILINEAR + ((ist->st->codec->flags&CODEC_FLAG_BITEXACT) ? SWS_BITEXACT:0));
    if (fr.num && fr.den)
        av_bprintf(&args, ":frame_rate=%d/%d", fr.num, fr.den);
    snprintf(name, sizeof(name), "graph %d input from stream %d:%d", fg->index,
             ist->file_index, ist->st->index);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, buffer_filt, name,
                                            args.str, NULL, fg->graph)) < 0)
        return ret;
    last_filter = ifilter->filter;

    if (ist->framerate.num) {
        AVFilterContext *setpts;

        snprintf(name, sizeof(name), "force CFR for input from stream %d:%d",
                 ist->file_index, ist->st->index);
        if ((ret = avfilter_graph_create_filter(&setpts,
                                                avfilter_get_by_name("setpts"),
                                                name, "N", NULL,
                                                fg->graph)) < 0)
            return ret;

        if ((ret = avfilter_link(last_filter, 0, setpts, 0)) < 0)
            return ret;

        last_filter = setpts;
    }

    if (do_deinterlace) {
        AVFilterContext *yadif;

        snprintf(name, sizeof(name), "deinterlace input from stream %d:%d",
                 ist->file_index, ist->st->index);
        if ((ret = avfilter_graph_create_filter(&yadif,
                                                avfilter_get_by_name("yadif"),
                                                name, "", NULL,
                                                fg->graph)) < 0)
            return ret;

        if ((ret = avfilter_link(last_filter, 0, yadif, 0)) < 0)
            return ret;

        last_filter = yadif;
    }

    snprintf(name, sizeof(name), "trim for input stream %d:%d",
             ist->file_index, ist->st->index);
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ?
                      AV_NOPTS_VALUE : 0, f->recording_time, &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;
    return 0;
}

static int configure_input_audio_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *last_filter;
    const AVFilter *abuffer_filt = avfilter_get_by_name("abuffer");
    InputStream *ist = ifilter->ist;
    InputFile     *f = input_files[ist->file_index];
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;

    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args, "time_base=%d/%d:sample_rate=%d:sample_fmt=%s",
             1, ist->st->codec->sample_rate,
             ist->st->codec->sample_rate,
             av_get_sample_fmt_name(ist->st->codec->sample_fmt));
    if (ist->st->codec->channel_layout)
        av_bprintf(&args, ":channel_layout=0x%"PRIx64,
                   ist->st->codec->channel_layout);
    else
        av_bprintf(&args, ":channels=%d", ist->st->codec->channels);
    snprintf(name, sizeof(name), "graph %d input from stream %d:%d", fg->index,
             ist->file_index, ist->st->index);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, abuffer_filt,
                                            name, args.str, NULL,
                                            fg->graph)) < 0)
        return ret;
    last_filter = ifilter->filter;

#define AUTO_INSERT_FILTER_INPUT(opt_name, filter_name, arg) do {                 \
    AVFilterContext *filt_ctx;                                              \
                                                                            \
    av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi "            \
           "similarly to -af " filter_name "=%s.\n", arg);                  \
                                                                            \
    snprintf(name, sizeof(name), "graph %d %s for input stream %d:%d",      \
                fg->index, filter_name, ist->file_index, ist->st->index);   \
    ret = avfilter_graph_create_filter(&filt_ctx,                           \
                                       avfilter_get_by_name(filter_name),   \
                                       name, arg, NULL, fg->graph);         \
    if (ret < 0)                                                            \
        return ret;                                                         \
                                                                            \
    ret = avfilter_link(last_filter, 0, filt_ctx, 0);                       \
    if (ret < 0)                                                            \
        return ret;                                                         \
                                                                            \
    last_filter = filt_ctx;                                                 \
} while (0)

    if (audio_sync_method > 0) {
        char args[256] = {0};

        av_strlcatf(args, sizeof(args), "async=%d", audio_sync_method);
        if (audio_drift_threshold != 0.1)
            av_strlcatf(args, sizeof(args), ":min_hard_comp=%f", audio_drift_threshold);
        if (!fg->reconfiguration)
            av_strlcatf(args, sizeof(args), ":first_pts=0");
        AUTO_INSERT_FILTER_INPUT("-async", "aresample", args);
    }

//     if (ost->audio_channels_mapped) {
//         int i;
//         AVBPrint pan_buf;
//         av_bprint_init(&pan_buf, 256, 8192);
//         av_bprintf(&pan_buf, "0x%"PRIx64,
//                    av_get_default_channel_layout(ost->audio_channels_mapped));
//         for (i = 0; i < ost->audio_channels_mapped; i++)
//             if (ost->audio_channels_map[i] != -1)
//                 av_bprintf(&pan_buf, ":c%d=c%d", i, ost->audio_channels_map[i]);
//         AUTO_INSERT_FILTER_INPUT("-map_channel", "pan", pan_buf.str);
//         av_bprint_finalize(&pan_buf, NULL);
//     }

    if (audio_volume != 256) {
        char args[256];

        av_log(NULL, AV_LOG_WARNING, "-vol has been deprecated. Use the volume "
               "audio filter instead.\n");

        snprintf(args, sizeof(args), "%f", audio_volume / 256.);
        AUTO_INSERT_FILTER_INPUT("-vol", "volume", args);
    }

    snprintf(name, sizeof(name), "trim for input stream %d:%d",
             ist->file_index, ist->st->index);
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ?
                      AV_NOPTS_VALUE : 0, f->recording_time, &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;

    return 0;
}

static int configure_input_filter(FilterGraph *fg, InputFilter *ifilter,
                                  AVFilterInOut *in)
{
    av_freep(&ifilter->name);
    DESCRIBE_FILTER_LINK(ifilter, in, 1);

    switch (avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx)) {
    case AVMEDIA_TYPE_VIDEO: return configure_input_video_filter(fg, ifilter, in);
    case AVMEDIA_TYPE_AUDIO: return configure_input_audio_filter(fg, ifilter, in);
    default: av_assert0(0);
    }
}

int configure_filtergraph(FilterGraph *fg)
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
        char args[512];
        AVDictionaryEntry *e = NULL;

        snprintf(args, sizeof(args), "flags=0x%X", (unsigned)ost->sws_flags);
        fg->graph->scale_sws_opts = av_strdup(args);

        args[0] = 0;
        while ((e = av_dict_get(ost->swr_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX))) {
            av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
        }
        if (strlen(args))
            args[strlen(args)-1] = 0;
        av_opt_set(fg->graph, "aresample_swr_opts", args, 0);

        args[0] = '\0';
        while ((e = av_dict_get(fg->outputs[0]->ost->resample_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX))) {
            av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
        }
        if (strlen(args))
            args[strlen(args) - 1] = '\0';
        fg->graph->resample_lavr_opts = av_strdup(args);
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
            GROW_ARRAY(fg->outputs, fg->nb_outputs);
            if (!(fg->outputs[fg->nb_outputs - 1] = av_mallocz(sizeof(*fg->outputs[0]))))
                exit_program(1);
            fg->outputs[fg->nb_outputs - 1]->graph   = fg;
            fg->outputs[fg->nb_outputs - 1]->out_tmp = cur;
            cur = cur->next;
            fg->outputs[fg->nb_outputs - 1]->out_tmp->next = NULL;
        }
    }

    fg->reconfiguration = 1;
    return 0;
}

int ist_in_filtergraph(FilterGraph *fg, InputStream *ist)
{
    int i;
    for (i = 0; i < fg->nb_inputs; i++)
        if (fg->inputs[i]->ist == ist)
            return 1;
    return 0;
}

