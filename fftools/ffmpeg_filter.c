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

#include <stdint.h>

#include "ffmpeg.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

// FIXME private header, used for mid_pred()
#include "libavcodec/mathops.h"

typedef struct FilterGraphPriv {
    FilterGraph      fg;

    // name used for logging
    char             log_name[32];

    int              is_simple;
    // true when the filtergraph contains only meta filters
    // that do not modify the frame data
    int              is_meta;
    // source filters are present in the graph
    int              have_sources;
    int              disable_conversions;

    unsigned         nb_outputs_done;

    const char      *graph_desc;

    char            *nb_threads;

    // frame for temporarily holding output from the filtergraph
    AVFrame         *frame;
    // frame for sending output to the encoder
    AVFrame         *frame_enc;

    Scheduler       *sch;
    unsigned         sch_idx;
} FilterGraphPriv;

static FilterGraphPriv *fgp_from_fg(FilterGraph *fg)
{
    return (FilterGraphPriv*)fg;
}

static const FilterGraphPriv *cfgp_from_cfg(const FilterGraph *fg)
{
    return (const FilterGraphPriv*)fg;
}

// data that is local to the filter thread and not visible outside of it
typedef struct FilterGraphThread {
    AVFilterGraph   *graph;

    AVFrame         *frame;

    // Temporary buffer for output frames, since on filtergraph reset
    // we cannot send them to encoders immediately.
    // The output index is stored in frame opaque.
    AVFifo          *frame_queue_out;

    // index of the next input to request from the scheduler
    unsigned         next_in;
    // set to 1 after at least one frame passed through this output
    int              got_frame;

    // EOF status of each input/output, as received by the thread
    uint8_t         *eof_in;
    uint8_t         *eof_out;
} FilterGraphThread;

typedef struct InputFilterPriv {
    InputFilter         ifilter;

    InputFilterOptions  opts;

    int                 index;

    AVFilterContext    *filter;

    // used to hold submitted input
    AVFrame            *frame;

    /* for filters that are not yet bound to an input stream,
     * this stores the input linklabel, if any */
    uint8_t            *linklabel;

    // filter data type
    enum AVMediaType    type;
    // source data type: AVMEDIA_TYPE_SUBTITLE for sub2video,
    // same as type otherwise
    enum AVMediaType    type_src;

    int                 eof;
    int                 bound;

    // parameters configured for this input
    int                 format;

    int                 width, height;
    AVRational          sample_aspect_ratio;
    enum AVColorSpace   color_space;
    enum AVColorRange   color_range;

    int                 sample_rate;
    AVChannelLayout     ch_layout;

    AVRational          time_base;

    AVFifo             *frame_queue;

    AVBufferRef        *hw_frames_ctx;

    int                 displaymatrix_present;
    int                 displaymatrix_applied;
    int32_t             displaymatrix[9];

    struct {
        AVFrame *frame;

        int64_t last_pts;
        int64_t end_pts;

        ///< marks if sub2video_update should force an initialization
        unsigned int initialize;
    } sub2video;
} InputFilterPriv;

static InputFilterPriv *ifp_from_ifilter(InputFilter *ifilter)
{
    return (InputFilterPriv*)ifilter;
}

typedef struct FPSConvContext {
    AVFrame          *last_frame;
    /* number of frames emitted by the video-encoding sync code */
    int64_t           frame_number;
    /* history of nb_frames_prev, i.e. the number of times the
     * previous frame was duplicated by vsync code in recent
     * do_video_out() calls */
    int64_t           frames_prev_hist[3];

    uint64_t          dup_warning;

    int               last_dropped;
    int               dropped_keyframe;

    enum VideoSyncMethod vsync_method;

    AVRational        framerate;
    AVRational        framerate_max;
    const AVRational *framerate_supported;
    int               framerate_clip;
} FPSConvContext;

typedef struct OutputFilterPriv {
    OutputFilter            ofilter;

    int                     index;

    void                   *log_parent;
    char                    log_name[32];

    char                   *name;

    AVFilterContext        *filter;

    /* desired output stream properties */
    int                     format;
    int                     width, height;
    int                     sample_rate;
    AVChannelLayout         ch_layout;

    // time base in which the output is sent to our downstream
    // does not need to match the filtersink's timebase
    AVRational              tb_out;
    // at least one frame with the above timebase was sent
    // to our downstream, so it cannot change anymore
    int                     tb_out_locked;

    AVRational              sample_aspect_ratio;

    AVDictionary           *sws_opts;
    AVDictionary           *swr_opts;

    // those are only set if no format is specified and the encoder gives us multiple options
    // They point directly to the relevant lists of the encoder.
    const int              *formats;
    const AVChannelLayout  *ch_layouts;
    const int              *sample_rates;

    AVRational              enc_timebase;
    int64_t                 trim_start_us;
    int64_t                 trim_duration_us;
    // offset for output timestamps, in AV_TIME_BASE_Q
    int64_t                 ts_offset;
    int64_t                 next_pts;
    FPSConvContext          fps;

    unsigned                flags;
} OutputFilterPriv;

static OutputFilterPriv *ofp_from_ofilter(OutputFilter *ofilter)
{
    return (OutputFilterPriv*)ofilter;
}

typedef struct FilterCommand {
    char *target;
    char *command;
    char *arg;

    double time;
    int    all_filters;
} FilterCommand;

static void filter_command_free(void *opaque, uint8_t *data)
{
    FilterCommand *fc = (FilterCommand*)data;

    av_freep(&fc->target);
    av_freep(&fc->command);
    av_freep(&fc->arg);

    av_free(data);
}

static int sub2video_get_blank_frame(InputFilterPriv *ifp)
{
    AVFrame *frame = ifp->sub2video.frame;
    int ret;

    av_frame_unref(frame);

    frame->width  = ifp->width;
    frame->height = ifp->height;
    frame->format = ifp->format;
    frame->colorspace = ifp->color_space;
    frame->color_range = ifp->color_range;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0)
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

static void sub2video_push_ref(InputFilterPriv *ifp, int64_t pts)
{
    AVFrame *frame = ifp->sub2video.frame;
    int ret;

    av_assert1(frame->data[0]);
    ifp->sub2video.last_pts = frame->pts = pts;
    ret = av_buffersrc_add_frame_flags(ifp->filter, frame,
                                       AV_BUFFERSRC_FLAG_KEEP_REF |
                                       AV_BUFFERSRC_FLAG_PUSH);
    if (ret != AVERROR_EOF && ret < 0)
        av_log(NULL, AV_LOG_WARNING, "Error while add the frame to buffer source(%s).\n",
               av_err2str(ret));
}

static void sub2video_update(InputFilterPriv *ifp, int64_t heartbeat_pts,
                             const AVSubtitle *sub)
{
    AVFrame *frame = ifp->sub2video.frame;
    int8_t *dst;
    int     dst_linesize;
    int num_rects;
    int64_t pts, end_pts;

    if (sub) {
        pts       = av_rescale_q(sub->pts + sub->start_display_time * 1000LL,
                                 AV_TIME_BASE_Q, ifp->time_base);
        end_pts   = av_rescale_q(sub->pts + sub->end_display_time   * 1000LL,
                                 AV_TIME_BASE_Q, ifp->time_base);
        num_rects = sub->num_rects;
    } else {
        /* If we are initializing the system, utilize current heartbeat
           PTS as the start time, and show until the following subpicture
           is received. Otherwise, utilize the previous subpicture's end time
           as the fall-back value. */
        pts       = ifp->sub2video.initialize ?
                    heartbeat_pts : ifp->sub2video.end_pts;
        end_pts   = INT64_MAX;
        num_rects = 0;
    }
    if (sub2video_get_blank_frame(ifp) < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to get a blank canvas.\n");
        return;
    }
    dst          = frame->data    [0];
    dst_linesize = frame->linesize[0];
    for (int i = 0; i < num_rects; i++)
        sub2video_copy_rect(dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(ifp, pts);
    ifp->sub2video.end_pts = end_pts;
    ifp->sub2video.initialize = 0;
}

/* Define a function for appending a list of allowed formats
 * to an AVBPrint. If nonempty, the list will have a header. */
#define DEF_CHOOSE_FORMAT(name, type, var, supported_list, none, printf_format, get_name) \
static void choose_ ## name (OutputFilterPriv *ofp, AVBPrint *bprint)          \
{                                                                              \
    if (ofp->var == none && !ofp->supported_list)                              \
        return;                                                                \
    av_bprintf(bprint, #name "=");                                             \
    if (ofp->var != none) {                                                    \
        av_bprintf(bprint, printf_format, get_name(ofp->var));                 \
    } else {                                                                   \
        const type *p;                                                         \
                                                                               \
        for (p = ofp->supported_list; *p != none; p++) {                       \
            av_bprintf(bprint, printf_format "|", get_name(*p));               \
        }                                                                      \
        if (bprint->len > 0)                                                   \
            bprint->str[--bprint->len] = '\0';                                 \
    }                                                                          \
    av_bprint_chars(bprint, ':', 1);                                           \
}

DEF_CHOOSE_FORMAT(pix_fmts, enum AVPixelFormat, format, formats,
                  AV_PIX_FMT_NONE, "%s", av_get_pix_fmt_name)

DEF_CHOOSE_FORMAT(sample_fmts, enum AVSampleFormat, format, formats,
                  AV_SAMPLE_FMT_NONE, "%s", av_get_sample_fmt_name)

DEF_CHOOSE_FORMAT(sample_rates, int, sample_rate, sample_rates, 0,
                  "%d", )

static void choose_channel_layouts(OutputFilterPriv *ofp, AVBPrint *bprint)
{
    if (av_channel_layout_check(&ofp->ch_layout)) {
        av_bprintf(bprint, "channel_layouts=");
        av_channel_layout_describe_bprint(&ofp->ch_layout, bprint);
    } else if (ofp->ch_layouts) {
        const AVChannelLayout *p;

        av_bprintf(bprint, "channel_layouts=");
        for (p = ofp->ch_layouts; p->nb_channels; p++) {
            av_channel_layout_describe_bprint(p, bprint);
            av_bprintf(bprint, "|");
        }
        if (bprint->len > 0)
            bprint->str[--bprint->len] = '\0';
    } else
        return;
    av_bprint_chars(bprint, ':', 1);
}

static int read_binary(const char *path, uint8_t **data, int *len)
{
    AVIOContext *io = NULL;
    int64_t fsize;
    int ret;

    *data = NULL;
    *len  = 0;

    ret = avio_open2(&io, path, AVIO_FLAG_READ, &int_cb, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open file '%s': %s\n",
               path, av_err2str(ret));
        return ret;
    }

    fsize = avio_size(io);
    if (fsize < 0 || fsize > INT_MAX) {
        av_log(NULL, AV_LOG_ERROR, "Cannot obtain size of file %s\n", path);
        ret = AVERROR(EIO);
        goto fail;
    }

    *data = av_malloc(fsize);
    if (!*data) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avio_read(io, *data, fsize);
    if (ret != fsize) {
        av_log(NULL, AV_LOG_ERROR, "Error reading file %s\n", path);
        ret = ret < 0 ? ret : AVERROR(EIO);
        goto fail;
    }

    *len = fsize;

    ret = 0;
fail:
    avio_close(io);
    if (ret < 0) {
        av_freep(data);
        *len = 0;
    }
    return ret;
}

static int filter_opt_apply(AVFilterContext *f, const char *key, const char *val)
{
    const AVOption *o = NULL;
    int ret;

    ret = av_opt_set(f, key, val, AV_OPT_SEARCH_CHILDREN);
    if (ret >= 0)
        return 0;

    if (ret == AVERROR_OPTION_NOT_FOUND && key[0] == '/')
        o = av_opt_find(f, key + 1, NULL, 0, AV_OPT_SEARCH_CHILDREN);
    if (!o)
        goto err_apply;

    // key is a valid option name prefixed with '/'
    // interpret value as a path from which to load the actual option value
    key++;

    if (o->type == AV_OPT_TYPE_BINARY) {
        uint8_t *data;
        int      len;

        ret = read_binary(val, &data, &len);
        if (ret < 0)
            goto err_load;

        ret = av_opt_set_bin(f, key, data, len, AV_OPT_SEARCH_CHILDREN);
        av_freep(&data);
    } else {
        char *data = file_read(val);
        if (!data) {
            ret = AVERROR(EIO);
            goto err_load;
        }

        ret = av_opt_set(f, key, data, AV_OPT_SEARCH_CHILDREN);
        av_freep(&data);
    }
    if (ret < 0)
        goto err_apply;

    return 0;

err_apply:
    av_log(NULL, AV_LOG_ERROR,
           "Error applying option '%s' to filter '%s': %s\n",
           key, f->filter->name, av_err2str(ret));
    return ret;
err_load:
    av_log(NULL, AV_LOG_ERROR,
           "Error loading value for option '%s' from file '%s'\n",
           key, val);
    return ret;
}

static int graph_opts_apply(AVFilterGraphSegment *seg)
{
    for (size_t i = 0; i < seg->nb_chains; i++) {
        AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            AVFilterParams *p = ch->filters[j];
            const AVDictionaryEntry *e = NULL;

            av_assert0(p->filter);

            while ((e = av_dict_iterate(p->opts, e))) {
                int ret = filter_opt_apply(p->filter, e->key, e->value);
                if (ret < 0)
                    return ret;
            }

            av_dict_free(&p->opts);
        }
    }

    return 0;
}

static int graph_parse(AVFilterGraph *graph, const char *desc,
                       AVFilterInOut **inputs, AVFilterInOut **outputs,
                       AVBufferRef *hw_device)
{
    AVFilterGraphSegment *seg;
    int ret;

    *inputs  = NULL;
    *outputs = NULL;

    ret = avfilter_graph_segment_parse(graph, desc, 0, &seg);
    if (ret < 0)
        return ret;

    ret = avfilter_graph_segment_create_filters(seg, 0);
    if (ret < 0)
        goto fail;

    if (hw_device) {
        for (int i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *f = graph->filters[i];

            if (!(f->filter->flags & AVFILTER_FLAG_HWDEVICE))
                continue;
            f->hw_device_ctx = av_buffer_ref(hw_device);
            if (!f->hw_device_ctx) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
    }

    ret = graph_opts_apply(seg);
    if (ret < 0)
        goto fail;

    ret = avfilter_graph_segment_apply(seg, 0, inputs, outputs);

fail:
    avfilter_graph_segment_free(&seg);
    return ret;
}

// Filters can be configured only if the formats of all inputs are known.
static int ifilter_has_all_input_formats(FilterGraph *fg)
{
    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        if (ifp->format < 0)
            return 0;
    }
    return 1;
}

static int filter_thread(void *arg);

static char *describe_filter_link(FilterGraph *fg, AVFilterInOut *inout, int in)
{
    AVFilterContext *ctx = inout->filter_ctx;
    AVFilterPad *pads = in ? ctx->input_pads  : ctx->output_pads;
    int       nb_pads = in ? ctx->nb_inputs   : ctx->nb_outputs;

    if (nb_pads > 1)
        return av_strdup(ctx->filter->name);
    return av_asprintf("%s:%s", ctx->filter->name,
                       avfilter_pad_get_name(pads, inout->pad_idx));
}

static const char *ofilter_item_name(void *obj)
{
    OutputFilterPriv *ofp = obj;
    return ofp->log_name;
}

static const AVClass ofilter_class = {
    .class_name                = "OutputFilter",
    .version                   = LIBAVUTIL_VERSION_INT,
    .item_name                 = ofilter_item_name,
    .parent_log_context_offset = offsetof(OutputFilterPriv, log_parent),
    .category                  = AV_CLASS_CATEGORY_FILTER,
};

static OutputFilter *ofilter_alloc(FilterGraph *fg, enum AVMediaType type)
{
    OutputFilterPriv *ofp;
    OutputFilter *ofilter;

    ofp = allocate_array_elem(&fg->outputs, sizeof(*ofp), &fg->nb_outputs);
    if (!ofp)
        return NULL;

    ofilter           = &ofp->ofilter;
    ofilter->class    = &ofilter_class;
    ofp->log_parent   = fg;
    ofilter->graph    = fg;
    ofilter->type     = type;
    ofp->format       = -1;
    ofp->index        = fg->nb_outputs - 1;

    snprintf(ofp->log_name, sizeof(ofp->log_name), "%co%d",
             av_get_media_type_string(type)[0], ofp->index);

    return ofilter;
}

static int ifilter_bind_ist(InputFilter *ifilter, InputStream *ist)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    FilterGraphPriv *fgp = fgp_from_fg(ifilter->graph);
    int ret, dec_idx;

    av_assert0(!ifp->bound);
    ifp->bound = 1;

    if (ifp->type != ist->par->codec_type &&
        !(ifp->type == AVMEDIA_TYPE_VIDEO && ist->par->codec_type == AVMEDIA_TYPE_SUBTITLE)) {
        av_log(fgp, AV_LOG_ERROR, "Tried to connect %s stream to %s filtergraph input\n",
               av_get_media_type_string(ist->par->codec_type), av_get_media_type_string(ifp->type));
        return AVERROR(EINVAL);
    }

    ifp->type_src        = ist->st->codecpar->codec_type;

    ifp->opts.fallback = av_frame_alloc();
    if (!ifp->opts.fallback)
        return AVERROR(ENOMEM);

    dec_idx = ist_filter_add(ist, ifilter, filtergraph_is_simple(ifilter->graph),
                             &ifp->opts);
    if (dec_idx < 0)
        return dec_idx;

    ret = sch_connect(fgp->sch, SCH_DEC(dec_idx),
                                SCH_FILTER_IN(fgp->sch_idx, ifp->index));
    if (ret < 0)
        return ret;

    if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE) {
        ifp->sub2video.frame = av_frame_alloc();
        if (!ifp->sub2video.frame)
            return AVERROR(ENOMEM);

        ifp->width  = ifp->opts.sub2video_width;
        ifp->height = ifp->opts.sub2video_height;

        /* rectangles are AV_PIX_FMT_PAL8, but we have no guarantee that the
           palettes for all rectangles are identical or compatible */
        ifp->format = AV_PIX_FMT_RGB32;

        ifp->time_base = AV_TIME_BASE_Q;

        av_log(fgp, AV_LOG_VERBOSE, "sub2video: using %dx%d canvas\n",
               ifp->width, ifp->height);
    }

    return 0;
}

static int ifilter_bind_dec(InputFilterPriv *ifp, Decoder *dec)
{
    FilterGraphPriv *fgp = fgp_from_fg(ifp->ifilter.graph);
    int ret, dec_idx;

    av_assert0(!ifp->bound);
    ifp->bound = 1;

    if (ifp->type != dec->type) {
        av_log(fgp, AV_LOG_ERROR, "Tried to connect %s decoder to %s filtergraph input\n",
               av_get_media_type_string(dec->type), av_get_media_type_string(ifp->type));
        return AVERROR(EINVAL);
    }

    ifp->type_src = ifp->type;

    dec_idx = dec_filter_add(dec, &ifp->ifilter, &ifp->opts);
    if (dec_idx < 0)
        return dec_idx;

    ret = sch_connect(fgp->sch, SCH_DEC(dec_idx),
                                SCH_FILTER_IN(fgp->sch_idx, ifp->index));
    if (ret < 0)
        return ret;

    return 0;
}

static int set_channel_layout(OutputFilterPriv *f, const AVChannelLayout *layouts_allowed,
                              const AVChannelLayout *layout_requested)
{
    int i, err;

    if (layout_requested->order != AV_CHANNEL_ORDER_UNSPEC) {
        /* Pass the layout through for all orders but UNSPEC */
        err = av_channel_layout_copy(&f->ch_layout, layout_requested);
        if (err < 0)
            return err;
        return 0;
    }

    /* Requested layout is of order UNSPEC */
    if (!layouts_allowed) {
        /* Use the default native layout for the requested amount of channels when the
           encoder doesn't have a list of supported layouts */
        av_channel_layout_default(&f->ch_layout, layout_requested->nb_channels);
        return 0;
    }
    /* Encoder has a list of supported layouts. Pick the first layout in it with the
       same amount of channels as the requested layout */
    for (i = 0; layouts_allowed[i].nb_channels; i++) {
        if (layouts_allowed[i].nb_channels == layout_requested->nb_channels)
            break;
    }
    if (layouts_allowed[i].nb_channels) {
        /* Use it if one is found */
        err = av_channel_layout_copy(&f->ch_layout, &layouts_allowed[i]);
        if (err < 0)
            return err;
        return 0;
    }
    /* If no layout for the amount of channels requested was found, use the default
       native layout for it. */
    av_channel_layout_default(&f->ch_layout, layout_requested->nb_channels);

    return 0;
}

int ofilter_bind_ost(OutputFilter *ofilter, OutputStream *ost,
                     unsigned sched_idx_enc,
                     const OutputFilterOptions *opts)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    FilterGraph  *fg = ofilter->graph;
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    int ret;

    av_assert0(!ofilter->bound);
    av_assert0(ofilter->type == ost->type);

    ofilter->bound = 1;
    av_freep(&ofilter->linklabel);

    ofp->flags        = opts->flags;
    ofp->ts_offset    = opts->ts_offset;
    ofp->enc_timebase = opts->output_tb;

    ofp->trim_start_us    = opts->trim_start_us;
    ofp->trim_duration_us = opts->trim_duration_us;

    ofp->name         = av_strdup(opts->name);
    if (!ofp->name)
        return AVERROR(EINVAL);

    ret = av_dict_copy(&ofp->sws_opts, opts->sws_opts, 0);
    if (ret < 0)
        return ret;

    ret = av_dict_copy(&ofp->swr_opts, opts->swr_opts, 0);
    if (ret < 0)
        return ret;

    if (opts->flags & OFILTER_FLAG_AUDIO_24BIT)
        av_dict_set(&ofp->swr_opts, "output_sample_bits", "24", 0);

    if (fgp->is_simple) {
        // for simple filtergraph there is just one output,
        // so use only graph-level information for logging
        ofp->log_parent = NULL;
        av_strlcpy(ofp->log_name, fgp->log_name, sizeof(ofp->log_name));
    } else
        av_strlcatf(ofp->log_name, sizeof(ofp->log_name), "->%s", ofp->name);

    switch (ofilter->type) {
    case AVMEDIA_TYPE_VIDEO:
        ofp->width      = opts->width;
        ofp->height     = opts->height;
        if (opts->format != AV_PIX_FMT_NONE) {
            ofp->format = opts->format;
        } else if (opts->pix_fmts)
            ofp->formats = opts->pix_fmts;
        else if (opts->enc &&
                 !(ofp->flags & OFILTER_FLAG_DISABLE_CONVERT))
            ofp->formats = opts->enc->pix_fmts;

        fgp->disable_conversions |= !!(ofp->flags & OFILTER_FLAG_DISABLE_CONVERT);

        ofp->fps.last_frame = av_frame_alloc();
        if (!ofp->fps.last_frame)
            return AVERROR(ENOMEM);

        ofp->fps.vsync_method        = opts->vsync_method;
        ofp->fps.framerate           = ost->frame_rate;
        ofp->fps.framerate_max       = ost->max_frame_rate;
        ofp->fps.framerate_supported = ost->force_fps || !opts->enc ?
                                       NULL : opts->enc->supported_framerates;

        // reduce frame rate for mpeg4 to be within the spec limits
        if (opts->enc && opts->enc->id == AV_CODEC_ID_MPEG4)
            ofp->fps.framerate_clip = 65535;

        ofp->fps.dup_warning         = 1000;

        break;
    case AVMEDIA_TYPE_AUDIO:
        if (opts->format != AV_SAMPLE_FMT_NONE) {
            ofp->format = opts->format;
        } else if (opts->enc) {
            ofp->formats = opts->enc->sample_fmts;
        }
        if (opts->sample_rate) {
            ofp->sample_rate = opts->sample_rate;
        } else if (opts->enc) {
            ofp->sample_rates = opts->enc->supported_samplerates;
        }
        if (opts->ch_layout.nb_channels) {
            int ret = set_channel_layout(ofp, opts->enc ? opts->enc->ch_layouts : NULL,
                                         &opts->ch_layout);
            if (ret < 0)
                return ret;
        } else if (opts->enc) {
            ofp->ch_layouts = opts->enc->ch_layouts;
        }
        break;
    }

    ret = sch_connect(fgp->sch, SCH_FILTER_OUT(fgp->sch_idx, ofp->index),
                                SCH_ENC(sched_idx_enc));
    if (ret < 0)
        return ret;

    return 0;
}

static int ofilter_bind_ifilter(OutputFilter *ofilter, InputFilterPriv *ifp,
                                const OutputFilterOptions *opts)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);

    av_assert0(!ofilter->bound);
    av_assert0(ofilter->type == ifp->type);

    ofilter->bound = 1;
    av_freep(&ofilter->linklabel);

    ofp->name = av_strdup(opts->name);
    if (!ofp->name)
        return AVERROR(EINVAL);

    av_strlcatf(ofp->log_name, sizeof(ofp->log_name), "->%s", ofp->name);

    return 0;
}

static int ifilter_bind_fg(InputFilterPriv *ifp, FilterGraph *fg_src, int out_idx)
{
    FilterGraphPriv      *fgp = fgp_from_fg(ifp->ifilter.graph);
    OutputFilter *ofilter_src = fg_src->outputs[out_idx];
    OutputFilterOptions opts;
    char name[32];
    int ret;

    av_assert0(!ifp->bound);
    ifp->bound = 1;

    if (ifp->type != ofilter_src->type) {
        av_log(fgp, AV_LOG_ERROR, "Tried to connect %s output to %s input\n",
               av_get_media_type_string(ofilter_src->type),
               av_get_media_type_string(ifp->type));
        return AVERROR(EINVAL);
    }

    ifp->type_src = ifp->type;

    memset(&opts, 0, sizeof(opts));

    snprintf(name, sizeof(name), "fg:%d:%d", fgp->fg.index, ifp->index);
    opts.name = name;

    ret = ofilter_bind_ifilter(ofilter_src, ifp, &opts);
    if (ret < 0)
        return ret;

    ret = sch_connect(fgp->sch, SCH_FILTER_OUT(fg_src->index, out_idx),
                                SCH_FILTER_IN(fgp->sch_idx, ifp->index));
    if (ret < 0)
        return ret;

    return 0;
}

static InputFilter *ifilter_alloc(FilterGraph *fg)
{
    InputFilterPriv *ifp;
    InputFilter *ifilter;

    ifp = allocate_array_elem(&fg->inputs, sizeof(*ifp), &fg->nb_inputs);
    if (!ifp)
        return NULL;

    ifilter         = &ifp->ifilter;
    ifilter->graph  = fg;

    ifp->frame = av_frame_alloc();
    if (!ifp->frame)
        return NULL;

    ifp->index           = fg->nb_inputs - 1;
    ifp->format          = -1;
    ifp->color_space     = AVCOL_SPC_UNSPECIFIED;
    ifp->color_range     = AVCOL_RANGE_UNSPECIFIED;

    ifp->frame_queue = av_fifo_alloc2(8, sizeof(AVFrame*), AV_FIFO_FLAG_AUTO_GROW);
    if (!ifp->frame_queue)
        return NULL;

    return ifilter;
}

void fg_free(FilterGraph **pfg)
{
    FilterGraph *fg = *pfg;
    FilterGraphPriv *fgp;

    if (!fg)
        return;
    fgp = fgp_from_fg(fg);

    for (int j = 0; j < fg->nb_inputs; j++) {
        InputFilter *ifilter = fg->inputs[j];
        InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

        if (ifp->frame_queue) {
            AVFrame *frame;
            while (av_fifo_read(ifp->frame_queue, &frame, 1) >= 0)
                av_frame_free(&frame);
            av_fifo_freep2(&ifp->frame_queue);
        }
        av_frame_free(&ifp->sub2video.frame);

        av_frame_free(&ifp->frame);
        av_frame_free(&ifp->opts.fallback);

        av_buffer_unref(&ifp->hw_frames_ctx);
        av_freep(&ifp->linklabel);
        av_freep(&ifp->opts.name);
        av_freep(&ifilter->name);
        av_freep(&fg->inputs[j]);
    }
    av_freep(&fg->inputs);
    for (int j = 0; j < fg->nb_outputs; j++) {
        OutputFilter *ofilter = fg->outputs[j];
        OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);

        av_frame_free(&ofp->fps.last_frame);
        av_dict_free(&ofp->sws_opts);
        av_dict_free(&ofp->swr_opts);

        av_freep(&ofilter->linklabel);
        av_freep(&ofilter->name);
        av_freep(&ofilter->apad);
        av_freep(&ofp->name);
        av_channel_layout_uninit(&ofp->ch_layout);
        av_freep(&fg->outputs[j]);
    }
    av_freep(&fg->outputs);
    av_freep(&fgp->graph_desc);
    av_freep(&fgp->nb_threads);

    av_frame_free(&fgp->frame);
    av_frame_free(&fgp->frame_enc);

    av_freep(pfg);
}

static const char *fg_item_name(void *obj)
{
    const FilterGraphPriv *fgp = obj;

    return fgp->log_name;
}

static const AVClass fg_class = {
    .class_name = "FilterGraph",
    .version    = LIBAVUTIL_VERSION_INT,
    .item_name  = fg_item_name,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

int fg_create(FilterGraph **pfg, char *graph_desc, Scheduler *sch)
{
    FilterGraphPriv *fgp;
    FilterGraph      *fg;

    AVFilterInOut *inputs, *outputs;
    AVFilterGraph *graph;
    int ret = 0;

    fgp = av_mallocz(sizeof(*fgp));
    if (!fgp)
        return AVERROR(ENOMEM);
    fg = &fgp->fg;

    if (pfg) {
        *pfg = fg;
        fg->index = -1;
    } else {
        ret = av_dynarray_add_nofree(&filtergraphs, &nb_filtergraphs, fgp);
        if (ret < 0) {
            av_freep(&fgp);
            return ret;
        }

        fg->index = nb_filtergraphs - 1;
    }

    fg->class       = &fg_class;
    fgp->graph_desc = graph_desc;
    fgp->disable_conversions = !auto_conversion_filters;
    fgp->sch                 = sch;

    snprintf(fgp->log_name, sizeof(fgp->log_name), "fc#%d", fg->index);

    fgp->frame     = av_frame_alloc();
    fgp->frame_enc = av_frame_alloc();
    if (!fgp->frame || !fgp->frame_enc)
        return AVERROR(ENOMEM);

    /* this graph is only used for determining the kinds of inputs
     * and outputs we have, and is discarded on exit from this function */
    graph = avfilter_graph_alloc();
    if (!graph)
        return AVERROR(ENOMEM);;
    graph->nb_threads = 1;

    ret = graph_parse(graph, fgp->graph_desc, &inputs, &outputs, NULL);
    if (ret < 0)
        goto fail;

    for (unsigned i = 0; i < graph->nb_filters; i++) {
        const AVFilter *f = graph->filters[i]->filter;
        if (!avfilter_filter_pad_count(f, 0) &&
            !(f->flags & AVFILTER_FLAG_DYNAMIC_INPUTS)) {
            fgp->have_sources = 1;
            break;
        }
    }

    for (AVFilterInOut *cur = inputs; cur; cur = cur->next) {
        InputFilter *const ifilter = ifilter_alloc(fg);
        InputFilterPriv       *ifp;

        if (!ifilter) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ifp            = ifp_from_ifilter(ifilter);
        ifp->linklabel = cur->name;
        cur->name      = NULL;

        ifp->type      = avfilter_pad_get_type(cur->filter_ctx->input_pads,
                                               cur->pad_idx);

        if (ifp->type != AVMEDIA_TYPE_VIDEO && ifp->type != AVMEDIA_TYPE_AUDIO) {
            av_log(fg, AV_LOG_FATAL, "Only video and audio filters supported "
                   "currently.\n");
            ret = AVERROR(ENOSYS);
            goto fail;
        }

        ifilter->name  = describe_filter_link(fg, cur, 1);
        if (!ifilter->name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (AVFilterInOut *cur = outputs; cur; cur = cur->next) {
        const enum AVMediaType type = avfilter_pad_get_type(cur->filter_ctx->output_pads,
                                                            cur->pad_idx);
        OutputFilter *const ofilter = ofilter_alloc(fg, type);

        if (!ofilter) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ofilter->linklabel = cur->name;
        cur->name          = NULL;

        ofilter->name      = describe_filter_link(fg, cur, 0);
        if (!ofilter->name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!fg->nb_outputs) {
        av_log(fg, AV_LOG_FATAL, "A filtergraph has zero outputs, this is not supported\n");
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    ret = sch_add_filtergraph(sch, fg->nb_inputs, fg->nb_outputs,
                              filter_thread, fgp);
    if (ret < 0)
        goto fail;
    fgp->sch_idx = ret;

fail:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_free(&graph);

    if (ret < 0)
        return ret;

    return 0;
}

int init_simple_filtergraph(InputStream *ist, OutputStream *ost,
                            char *graph_desc,
                            Scheduler *sch, unsigned sched_idx_enc,
                            const OutputFilterOptions *opts)
{
    FilterGraph *fg;
    FilterGraphPriv *fgp;
    int ret;

    ret = fg_create(&ost->fg_simple, graph_desc, sch);
    if (ret < 0)
        return ret;
    fg  = ost->fg_simple;
    fgp = fgp_from_fg(fg);

    fgp->is_simple = 1;

    snprintf(fgp->log_name, sizeof(fgp->log_name), "%cf%s",
             av_get_media_type_string(ost->type)[0], opts->name);

    if (fg->nb_inputs != 1 || fg->nb_outputs != 1) {
        av_log(fg, AV_LOG_ERROR, "Simple filtergraph '%s' was expected "
               "to have exactly 1 input and 1 output. "
               "However, it had %d input(s) and %d output(s). Please adjust, "
               "or use a complex filtergraph (-filter_complex) instead.\n",
               graph_desc, fg->nb_inputs, fg->nb_outputs);
        return AVERROR(EINVAL);
    }
    if (fg->outputs[0]->type != ost->type) {
        av_log(fg, AV_LOG_ERROR, "Filtergraph has a %s output, cannot connect "
               "it to %s output stream\n",
               av_get_media_type_string(fg->outputs[0]->type),
               av_get_media_type_string(ost->type));
        return AVERROR(EINVAL);
    }

    ost->filter = fg->outputs[0];

    ret = ifilter_bind_ist(fg->inputs[0], ist);
    if (ret < 0)
        return ret;

    ret = ofilter_bind_ost(fg->outputs[0], ost, sched_idx_enc, opts);
    if (ret < 0)
        return ret;

    if (opts->nb_threads) {
        av_freep(&fgp->nb_threads);
        fgp->nb_threads = av_strdup(opts->nb_threads);
        if (!fgp->nb_threads)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int fg_complex_bind_input(FilterGraph *fg, InputFilter *ifilter)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    InputStream *ist = NULL;
    enum AVMediaType type = ifp->type;
    int i, ret;

    if (ifp->linklabel && !strncmp(ifp->linklabel, "dec:", 4)) {
        // bind to a standalone decoder
        int dec_idx;

        dec_idx = strtol(ifp->linklabel + 4, NULL, 0);
        if (dec_idx < 0 || dec_idx >= nb_decoders) {
            av_log(fg, AV_LOG_ERROR, "Invalid decoder index %d in filtergraph description %s\n",
                   dec_idx, fgp->graph_desc);
            return AVERROR(EINVAL);
        }

        ret = ifilter_bind_dec(ifp, decoders[dec_idx]);
        if (ret < 0)
            av_log(fg, AV_LOG_ERROR, "Error binding a decoder to filtergraph input %s\n",
                   ifilter->name);
        return ret;
    } else if (ifp->linklabel) {
        AVFormatContext *s;
        AVStream       *st = NULL;
        char *p;
        int file_idx;

        // try finding an unbound filtergraph output with this label
        for (int i = 0; i < nb_filtergraphs; i++) {
            FilterGraph *fg_src = filtergraphs[i];

            if (fg == fg_src)
                continue;

            for (int j = 0; j < fg_src->nb_outputs; j++) {
                OutputFilter *ofilter = fg_src->outputs[j];

                if (!ofilter->bound && ofilter->linklabel &&
                    !strcmp(ofilter->linklabel, ifp->linklabel)) {
                    av_log(fg, AV_LOG_VERBOSE,
                           "Binding input with label '%s' to filtergraph output %d:%d\n",
                           ifp->linklabel, i, j);

                    ret = ifilter_bind_fg(ifp, fg_src, j);
                    if (ret < 0)
                        av_log(fg, AV_LOG_ERROR, "Error binding filtergraph input %s\n",
                               ifp->linklabel);
                    return ret;
                }
            }
        }

        // bind to an explicitly specified demuxer stream
        file_idx = strtol(ifp->linklabel, &p, 0);
        if (file_idx < 0 || file_idx >= nb_input_files) {
            av_log(fg, AV_LOG_FATAL, "Invalid file index %d in filtergraph description %s.\n",
                   file_idx, fgp->graph_desc);
            return AVERROR(EINVAL);
        }
        s = input_files[file_idx]->ctx;

        for (i = 0; i < s->nb_streams; i++) {
            enum AVMediaType stream_type = s->streams[i]->codecpar->codec_type;
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
            av_log(fg, AV_LOG_FATAL, "Stream specifier '%s' in filtergraph description %s "
                   "matches no streams.\n", p, fgp->graph_desc);
            return AVERROR(EINVAL);
        }
        ist = input_files[file_idx]->streams[st->index];

        av_log(fg, AV_LOG_VERBOSE,
               "Binding input with label '%s' to input stream %d:%d\n",
               ifp->linklabel, ist->file->index, ist->index);
    } else {
        ist = ist_find_unused(type);
        if (!ist) {
            av_log(fg, AV_LOG_FATAL, "Cannot find a matching stream for "
                   "unlabeled input pad %s\n", ifilter->name);
            return AVERROR(EINVAL);
        }

        av_log(fg, AV_LOG_VERBOSE,
               "Binding unlabeled input %d to input stream %d:%d\n",
               ifp->index, ist->file->index, ist->index);
    }
    av_assert0(ist);

    ret = ifilter_bind_ist(ifilter, ist);
    if (ret < 0) {
        av_log(fg, AV_LOG_ERROR,
               "Error binding an input stream to complex filtergraph input %s.\n",
               ifilter->name);
        return ret;
    }

    return 0;
}

static int bind_inputs(FilterGraph *fg)
{
    // bind filtergraph inputs to input streams or other filtergraphs
    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        int ret;

        if (ifp->bound)
            continue;

        ret = fg_complex_bind_input(fg, &ifp->ifilter);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int fg_finalise_bindings(void)
{
    int ret;

    for (int i = 0; i < nb_filtergraphs; i++) {
        ret = bind_inputs(filtergraphs[i]);
        if (ret < 0)
            return ret;
    }

    // check that all outputs were bound
    for (int i = 0; i < nb_filtergraphs; i++) {
        FilterGraph *fg = filtergraphs[i];

        for (int j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *output = fg->outputs[j];
            if (!output->bound) {
                av_log(filtergraphs[j], AV_LOG_FATAL,
                       "Filter %s has an unconnected output\n", output->name);
                return AVERROR(EINVAL);
            }
        }
    }

    return 0;
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
        ret = av_opt_set_int(ctx, "durationi", duration,
                                AV_OPT_SEARCH_CHILDREN);
    }
    if (ret >= 0 && start_time != AV_NOPTS_VALUE) {
        ret = av_opt_set_int(ctx, "starti", start_time,
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

static int insert_filter(AVFilterContext **last_filter, int *pad_idx,
                         const char *filter_name, const char *args)
{
    AVFilterGraph *graph = (*last_filter)->graph;
    AVFilterContext *ctx;
    int ret;

    ret = avfilter_graph_create_filter(&ctx,
                                       avfilter_get_by_name(filter_name),
                                       filter_name, args, NULL, graph);
    if (ret < 0)
        return ret;

    ret = avfilter_link(*last_filter, *pad_idx, ctx, 0);
    if (ret < 0)
        return ret;

    *last_filter = ctx;
    *pad_idx     = 0;
    return 0;
}

static int configure_output_video_filter(FilterGraph *fg, AVFilterGraph *graph,
                                         OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    AVFilterContext *last_filter = out->filter_ctx;
    AVBPrint bprint;
    int pad_idx = out->pad_idx;
    int ret;
    char name[255];

    snprintf(name, sizeof(name), "out_%s", ofp->name);
    ret = avfilter_graph_create_filter(&ofp->filter,
                                       avfilter_get_by_name("buffersink"),
                                       name, NULL, NULL, graph);

    if (ret < 0)
        return ret;

    if ((ofp->width || ofp->height) && (ofp->flags & OFILTER_FLAG_AUTOSCALE)) {
        char args[255];
        AVFilterContext *filter;
        const AVDictionaryEntry *e = NULL;

        snprintf(args, sizeof(args), "%d:%d",
                 ofp->width, ofp->height);

        while ((e = av_dict_iterate(ofp->sws_opts, e))) {
            av_strlcatf(args, sizeof(args), ":%s=%s", e->key, e->value);
        }

        snprintf(name, sizeof(name), "scaler_out_%s", ofp->name);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                name, args, NULL, graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    av_assert0(!(ofp->flags & OFILTER_FLAG_DISABLE_CONVERT) ||
               ofp->format != AV_PIX_FMT_NONE || !ofp->formats);
    av_bprint_init(&bprint, 0, AV_BPRINT_SIZE_UNLIMITED);
    choose_pix_fmts(ofp, &bprint);
    if (!av_bprint_is_complete(&bprint))
        return AVERROR(ENOMEM);

    if (bprint.len) {
        AVFilterContext *filter;

        ret = avfilter_graph_create_filter(&filter,
                                           avfilter_get_by_name("format"),
                                           "format", bprint.str, NULL, graph);
        av_bprint_finalize(&bprint, NULL);
        if (ret < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx     = 0;
    }

    snprintf(name, sizeof(name), "trim_out_%s", ofp->name);
    ret = insert_trim(ofp->trim_start_us, ofp->trim_duration_us,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;


    if ((ret = avfilter_link(last_filter, pad_idx, ofp->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_audio_filter(FilterGraph *fg, AVFilterGraph *graph,
                                         OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    AVBPrint args;
    char name[255];
    int ret;

    snprintf(name, sizeof(name), "out_%s", ofp->name);
    ret = avfilter_graph_create_filter(&ofp->filter,
                                       avfilter_get_by_name("abuffersink"),
                                       name, NULL, NULL, graph);
    if (ret < 0)
        return ret;
    if ((ret = av_opt_set_int(ofp->filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        return ret;

#define AUTO_INSERT_FILTER(opt_name, filter_name, arg) do {                 \
    AVFilterContext *filt_ctx;                                              \
                                                                            \
    av_log(ofilter, AV_LOG_INFO, opt_name " is forwarded to lavfi "         \
           "similarly to -af " filter_name "=%s.\n", arg);                  \
                                                                            \
    ret = avfilter_graph_create_filter(&filt_ctx,                           \
                                       avfilter_get_by_name(filter_name),   \
                                       filter_name, arg, NULL, graph);      \
    if (ret < 0)                                                            \
        goto fail;                                                          \
                                                                            \
    ret = avfilter_link(last_filter, pad_idx, filt_ctx, 0);                 \
    if (ret < 0)                                                            \
        goto fail;                                                          \
                                                                            \
    last_filter = filt_ctx;                                                 \
    pad_idx = 0;                                                            \
} while (0)
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_UNLIMITED);

    choose_sample_fmts(ofp,     &args);
    choose_sample_rates(ofp,    &args);
    choose_channel_layouts(ofp, &args);
    if (!av_bprint_is_complete(&args)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if (args.len) {
        AVFilterContext *format;

        snprintf(name, sizeof(name), "format_out_%s", ofp->name);
        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           name, args.str, NULL, graph);
        if (ret < 0)
            goto fail;

        ret = avfilter_link(last_filter, pad_idx, format, 0);
        if (ret < 0)
            goto fail;

        last_filter = format;
        pad_idx = 0;
    }

    if (ofilter->apad)
        AUTO_INSERT_FILTER("-apad", "apad", ofilter->apad);

    snprintf(name, sizeof(name), "trim for output %s", ofp->name);
    ret = insert_trim(ofp->trim_start_us, ofp->trim_duration_us,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        goto fail;

    if ((ret = avfilter_link(last_filter, pad_idx, ofp->filter, 0)) < 0)
        goto fail;
fail:
    av_bprint_finalize(&args, NULL);

    return ret;
}

static int configure_output_filter(FilterGraph *fg, AVFilterGraph *graph,
                                   OutputFilter *ofilter, AVFilterInOut *out)
{
    switch (ofilter->type) {
    case AVMEDIA_TYPE_VIDEO: return configure_output_video_filter(fg, graph, ofilter, out);
    case AVMEDIA_TYPE_AUDIO: return configure_output_audio_filter(fg, graph, ofilter, out);
    default: av_assert0(0); return 0;
    }
}

static void sub2video_prepare(InputFilterPriv *ifp)
{
    ifp->sub2video.last_pts = INT64_MIN;
    ifp->sub2video.end_pts  = INT64_MIN;

    /* sub2video structure has been (re-)initialized.
       Mark it as such so that the system will be
       initialized with the first received heartbeat. */
    ifp->sub2video.initialize = 1;
}

static int configure_input_video_filter(FilterGraph *fg, AVFilterGraph *graph,
                                        InputFilter *ifilter, AVFilterInOut *in)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

    AVFilterContext *last_filter;
    const AVFilter *buffer_filt = avfilter_get_by_name("buffer");
    const AVPixFmtDescriptor *desc;
    AVRational fr = ifp->opts.framerate;
    AVRational sar;
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;
    AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
    if (!par)
        return AVERROR(ENOMEM);

    if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE)
        sub2video_prepare(ifp);

    sar = ifp->sample_aspect_ratio;
    if(!sar.den)
        sar = (AVRational){0,1};
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args,
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:"
             "pixel_aspect=%d/%d:colorspace=%d:range=%d",
             ifp->width, ifp->height, ifp->format,
             ifp->time_base.num, ifp->time_base.den, sar.num, sar.den,
             ifp->color_space, ifp->color_range);
    if (fr.num && fr.den)
        av_bprintf(&args, ":frame_rate=%d/%d", fr.num, fr.den);
    snprintf(name, sizeof(name), "graph %d input from stream %s", fg->index,
             ifp->opts.name);


    if ((ret = avfilter_graph_create_filter(&ifp->filter, buffer_filt, name,
                                            args.str, NULL, graph)) < 0)
        goto fail;
    par->hw_frames_ctx = ifp->hw_frames_ctx;
    ret = av_buffersrc_parameters_set(ifp->filter, par);
    if (ret < 0)
        goto fail;
    av_freep(&par);
    last_filter = ifp->filter;

    desc = av_pix_fmt_desc_get(ifp->format);
    av_assert0(desc);

    if ((ifp->opts.flags & IFILTER_FLAG_CROP)) {
        char crop_buf[64];
        snprintf(crop_buf, sizeof(crop_buf), "w=iw-%u-%u:h=ih-%u-%u:x=%u:y=%u",
                 ifp->opts.crop_left, ifp->opts.crop_right,
                 ifp->opts.crop_top, ifp->opts.crop_bottom,
                 ifp->opts.crop_left, ifp->opts.crop_top);
        ret = insert_filter(&last_filter, &pad_idx, "crop", crop_buf);
        if (ret < 0)
            return ret;
    }

    // TODO: insert hwaccel enabled filters like transpose_vaapi into the graph
    ifp->displaymatrix_applied = 0;
    if ((ifp->opts.flags & IFILTER_FLAG_AUTOROTATE) &&
        !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        int32_t *displaymatrix = ifp->displaymatrix;
        double theta;

        theta = get_rotation(displaymatrix);

        if (fabs(theta - 90) < 1.0) {
            ret = insert_filter(&last_filter, &pad_idx, "transpose",
                                displaymatrix[3] > 0 ? "cclock_flip" : "clock");
        } else if (fabs(theta - 180) < 1.0) {
            if (displaymatrix[0] < 0) {
                ret = insert_filter(&last_filter, &pad_idx, "hflip", NULL);
                if (ret < 0)
                    return ret;
            }
            if (displaymatrix[4] < 0) {
                ret = insert_filter(&last_filter, &pad_idx, "vflip", NULL);
            }
        } else if (fabs(theta - 270) < 1.0) {
            ret = insert_filter(&last_filter, &pad_idx, "transpose",
                                displaymatrix[3] < 0 ? "clock_flip" : "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            ret = insert_filter(&last_filter, &pad_idx, "rotate", rotate_buf);
        } else if (fabs(theta) < 1.0) {
            if (displaymatrix && displaymatrix[4] < 0) {
                ret = insert_filter(&last_filter, &pad_idx, "vflip", NULL);
            }
        }
        if (ret < 0)
            return ret;

        ifp->displaymatrix_applied = 1;
    }

    snprintf(name, sizeof(name), "trim_in_%s", ifp->opts.name);
    ret = insert_trim(ifp->opts.trim_start_us, ifp->opts.trim_end_us,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;
    return 0;
fail:
    av_freep(&par);

    return ret;
}

static int configure_input_audio_filter(FilterGraph *fg, AVFilterGraph *graph,
                                        InputFilter *ifilter, AVFilterInOut *in)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    AVFilterContext *last_filter;
    const AVFilter *abuffer_filt = avfilter_get_by_name("abuffer");
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;

    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args, "time_base=%d/%d:sample_rate=%d:sample_fmt=%s",
               ifp->time_base.num, ifp->time_base.den,
               ifp->sample_rate,
               av_get_sample_fmt_name(ifp->format));
    if (av_channel_layout_check(&ifp->ch_layout) &&
        ifp->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
        av_bprintf(&args, ":channel_layout=");
        av_channel_layout_describe_bprint(&ifp->ch_layout, &args);
    } else
        av_bprintf(&args, ":channels=%d", ifp->ch_layout.nb_channels);
    snprintf(name, sizeof(name), "graph_%d_in_%s", fg->index, ifp->opts.name);

    if ((ret = avfilter_graph_create_filter(&ifp->filter, abuffer_filt,
                                            name, args.str, NULL,
                                            graph)) < 0)
        return ret;
    last_filter = ifp->filter;

    snprintf(name, sizeof(name), "trim for input stream %s", ifp->opts.name);
    ret = insert_trim(ifp->opts.trim_start_us, ifp->opts.trim_end_us,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;

    return 0;
}

static int configure_input_filter(FilterGraph *fg, AVFilterGraph *graph,
                                  InputFilter *ifilter, AVFilterInOut *in)
{
    switch (ifp_from_ifilter(ifilter)->type) {
    case AVMEDIA_TYPE_VIDEO: return configure_input_video_filter(fg, graph, ifilter, in);
    case AVMEDIA_TYPE_AUDIO: return configure_input_audio_filter(fg, graph, ifilter, in);
    default: av_assert0(0); return 0;
    }
}

static void cleanup_filtergraph(FilterGraph *fg, FilterGraphThread *fgt)
{
    for (int i = 0; i < fg->nb_outputs; i++)
        ofp_from_ofilter(fg->outputs[i])->filter = NULL;
    for (int i = 0; i < fg->nb_inputs; i++)
        ifp_from_ifilter(fg->inputs[i])->filter = NULL;
    avfilter_graph_free(&fgt->graph);
}

static int filter_is_buffersrc(const AVFilterContext *f)
{
    return f->nb_inputs == 0 &&
           (!strcmp(f->filter->name, "buffer") ||
            !strcmp(f->filter->name, "abuffer"));
}

static int graph_is_meta(AVFilterGraph *graph)
{
    for (unsigned i = 0; i < graph->nb_filters; i++) {
        const AVFilterContext *f = graph->filters[i];

        /* in addition to filters flagged as meta, also
         * disregard sinks and buffersources (but not other sources,
         * since they introduce data we are not aware of)
         */
        if (!((f->filter->flags & AVFILTER_FLAG_METADATA_ONLY) ||
              f->nb_outputs == 0                               ||
              filter_is_buffersrc(f)))
            return 0;
    }
    return 1;
}

static int sub2video_frame(InputFilter *ifilter, AVFrame *frame, int buffer);

static int configure_filtergraph(FilterGraph *fg, FilterGraphThread *fgt)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    AVBufferRef *hw_device;
    AVFilterInOut *inputs, *outputs, *cur;
    int ret, i, simple = filtergraph_is_simple(fg);
    int have_input_eof = 0;
    const char *graph_desc = fgp->graph_desc;

    cleanup_filtergraph(fg, fgt);
    fgt->graph = avfilter_graph_alloc();
    if (!fgt->graph)
        return AVERROR(ENOMEM);

    if (simple) {
        OutputFilterPriv *ofp = ofp_from_ofilter(fg->outputs[0]);

        if (filter_nbthreads) {
            ret = av_opt_set(fgt->graph, "threads", filter_nbthreads, 0);
            if (ret < 0)
                goto fail;
        } else if (fgp->nb_threads) {
            ret = av_opt_set(fgt->graph, "threads", fgp->nb_threads, 0);
            if (ret < 0)
                return ret;
        }

        if (av_dict_count(ofp->sws_opts)) {
            ret = av_dict_get_string(ofp->sws_opts,
                                     &fgt->graph->scale_sws_opts,
                                     '=', ':');
            if (ret < 0)
                goto fail;
        }

        if (av_dict_count(ofp->swr_opts)) {
            char *args;
            ret = av_dict_get_string(ofp->swr_opts, &args, '=', ':');
            if (ret < 0)
                goto fail;
            av_opt_set(fgt->graph, "aresample_swr_opts", args, 0);
            av_free(args);
        }
    } else {
        fgt->graph->nb_threads = filter_complex_nbthreads;
    }

    hw_device = hw_device_for_filter();

    if ((ret = graph_parse(fgt->graph, graph_desc, &inputs, &outputs, hw_device)) < 0)
        goto fail;

    for (cur = inputs, i = 0; cur; cur = cur->next, i++)
        if ((ret = configure_input_filter(fg, fgt->graph, fg->inputs[i], cur)) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            goto fail;
        }
    avfilter_inout_free(&inputs);

    for (cur = outputs, i = 0; cur; cur = cur->next, i++) {
        ret = configure_output_filter(fg, fgt->graph, fg->outputs[i], cur);
        if (ret < 0) {
            avfilter_inout_free(&outputs);
            goto fail;
        }
    }
    avfilter_inout_free(&outputs);

    if (fgp->disable_conversions)
        avfilter_graph_set_auto_convert(fgt->graph, AVFILTER_AUTO_CONVERT_NONE);
    if ((ret = avfilter_graph_config(fgt->graph, NULL)) < 0)
        goto fail;

    fgp->is_meta = graph_is_meta(fgt->graph);

    /* limit the lists of allowed formats to the ones selected, to
     * make sure they stay the same if the filtergraph is reconfigured later */
    for (int i = 0; i < fg->nb_outputs; i++) {
        OutputFilter *ofilter = fg->outputs[i];
        OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
        AVFilterContext *sink = ofp->filter;

        ofp->format = av_buffersink_get_format(sink);

        ofp->width  = av_buffersink_get_w(sink);
        ofp->height = av_buffersink_get_h(sink);

        // If the timing parameters are not locked yet, get the tentative values
        // here but don't lock them. They will only be used if no output frames
        // are ever produced.
        if (!ofp->tb_out_locked) {
            AVRational fr = av_buffersink_get_frame_rate(sink);
            if (ofp->fps.framerate.num <= 0 && ofp->fps.framerate.den <= 0 &&
                fr.num > 0 && fr.den > 0)
                ofp->fps.framerate = fr;
            ofp->tb_out = av_buffersink_get_time_base(sink);
        }
        ofp->sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(sink);

        ofp->sample_rate    = av_buffersink_get_sample_rate(sink);
        av_channel_layout_uninit(&ofp->ch_layout);
        ret = av_buffersink_get_ch_layout(sink, &ofp->ch_layout);
        if (ret < 0)
            goto fail;
    }

    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        AVFrame *tmp;
        while (av_fifo_read(ifp->frame_queue, &tmp, 1) >= 0) {
            if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE) {
                sub2video_frame(&ifp->ifilter, tmp, !fgt->graph);
            } else {
                ret = av_buffersrc_add_frame(ifp->filter, tmp);
            }
            av_frame_free(&tmp);
            if (ret < 0)
                goto fail;
        }
    }

    /* send the EOFs for the finished inputs */
    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        if (fgt->eof_in[i]) {
            ret = av_buffersrc_add_frame(ifp->filter, NULL);
            if (ret < 0)
                goto fail;
            have_input_eof = 1;
        }
    }

    if (have_input_eof) {
        // make sure the EOF propagates to the end of the graph
        ret = avfilter_graph_request_oldest(fgt->graph);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            goto fail;
    }

    return 0;
fail:
    cleanup_filtergraph(fg, fgt);
    return ret;
}

static int ifilter_parameters_from_frame(InputFilter *ifilter, const AVFrame *frame)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    AVFrameSideData *sd;
    int ret;

    ret = av_buffer_replace(&ifp->hw_frames_ctx, frame->hw_frames_ctx);
    if (ret < 0)
        return ret;

    ifp->time_base = (ifp->type == AVMEDIA_TYPE_AUDIO)    ? (AVRational){ 1, frame->sample_rate } :
                     (ifp->opts.flags & IFILTER_FLAG_CFR) ? av_inv_q(ifp->opts.framerate)         :
                     frame->time_base;

    ifp->format              = frame->format;

    ifp->width               = frame->width;
    ifp->height              = frame->height;
    ifp->sample_aspect_ratio = frame->sample_aspect_ratio;
    ifp->color_space         = frame->colorspace;
    ifp->color_range         = frame->color_range;

    ifp->sample_rate         = frame->sample_rate;
    ret = av_channel_layout_copy(&ifp->ch_layout, &frame->ch_layout);
    if (ret < 0)
        return ret;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
    if (sd)
        memcpy(ifp->displaymatrix, sd->data, sizeof(ifp->displaymatrix));
    ifp->displaymatrix_present = !!sd;

    return 0;
}

int filtergraph_is_simple(const FilterGraph *fg)
{
    const FilterGraphPriv *fgp = cfgp_from_cfg(fg);
    return fgp->is_simple;
}

static void send_command(FilterGraph *fg, AVFilterGraph *graph,
                         double time, const char *target,
                         const char *command, const char *arg, int all_filters)
{
    int ret;

    if (!graph)
        return;

    if (time < 0) {
        char response[4096];
        ret = avfilter_graph_send_command(graph, target, command, arg,
                                          response, sizeof(response),
                                          all_filters ? 0 : AVFILTER_CMD_FLAG_ONE);
        fprintf(stderr, "Command reply for stream %d: ret:%d res:\n%s",
                fg->index, ret, response);
    } else if (!all_filters) {
        fprintf(stderr, "Queuing commands only on filters supporting the specific command is unsupported\n");
    } else {
        ret = avfilter_graph_queue_command(graph, target, command, arg, 0, time);
        if (ret < 0)
            fprintf(stderr, "Queuing command failed with error %s\n", av_err2str(ret));
    }
}

static int choose_input(const FilterGraph *fg, const FilterGraphThread *fgt)
{
    int nb_requests, nb_requests_max = -1;
    int best_input = -1;

    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilter *ifilter = fg->inputs[i];
        InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

        if (fgt->eof_in[i])
            continue;

        nb_requests = av_buffersrc_get_nb_failed_requests(ifp->filter);
        if (nb_requests > nb_requests_max) {
            nb_requests_max = nb_requests;
            best_input = i;
        }
    }

    av_assert0(best_input >= 0);

    return best_input;
}

static int choose_out_timebase(OutputFilterPriv *ofp, AVFrame *frame)
{
    OutputFilter *ofilter = &ofp->ofilter;
    FPSConvContext   *fps = &ofp->fps;
    AVRational        tb = (AVRational){ 0, 0 };
    AVRational fr;
    const FrameData *fd;

    fd = frame_data_c(frame);

    // apply -enc_time_base
    if (ofp->enc_timebase.num == ENC_TIME_BASE_DEMUX &&
        (fd->dec.tb.num <= 0 || fd->dec.tb.den <= 0)) {
        av_log(ofp, AV_LOG_ERROR,
               "Demuxing timebase not available - cannot use it for encoding\n");
        return AVERROR(EINVAL);
    }

    switch (ofp->enc_timebase.num) {
    case 0:                                            break;
    case ENC_TIME_BASE_DEMUX:  tb = fd->dec.tb;        break;
    case ENC_TIME_BASE_FILTER: tb = frame->time_base;  break;
    default:                   tb = ofp->enc_timebase; break;
    }

    if (ofilter->type == AVMEDIA_TYPE_AUDIO) {
        tb = tb.num ? tb : (AVRational){ 1, frame->sample_rate };
        goto finish;
    }

    fr = fps->framerate;
    if (!fr.num) {
        AVRational fr_sink = av_buffersink_get_frame_rate(ofp->filter);
        if (fr_sink.num > 0 && fr_sink.den > 0)
            fr = fr_sink;
    }

    if (fps->vsync_method == VSYNC_CFR || fps->vsync_method == VSYNC_VSCFR) {
        if (!fr.num && !fps->framerate_max.num) {
            fr = (AVRational){25, 1};
            av_log(ofp, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps. Use the -r option "
                   "if you want a different framerate.\n");
        }

        if (fps->framerate_max.num &&
            (av_q2d(fr) > av_q2d(fps->framerate_max) ||
            !fr.den))
            fr = fps->framerate_max;
    }

    if (fr.num > 0) {
        if (fps->framerate_supported) {
            int idx = av_find_nearest_q_idx(fr, fps->framerate_supported);
            fr = fps->framerate_supported[idx];
        }
        if (fps->framerate_clip) {
            av_reduce(&fr.num, &fr.den,
                      fr.num, fr.den, fps->framerate_clip);
        }
    }

    if (!(tb.num > 0 && tb.den > 0))
        tb = av_inv_q(fr);
    if (!(tb.num > 0 && tb.den > 0))
        tb = frame->time_base;

    fps->framerate     = fr;
finish:
    ofp->tb_out        = tb;
    ofp->tb_out_locked = 1;

    return 0;
}

static double adjust_frame_pts_to_encoder_tb(AVFrame *frame, AVRational tb_dst,
                                             int64_t start_time)
{
    double float_pts = AV_NOPTS_VALUE; // this is identical to frame.pts but with higher precision

    AVRational        tb = tb_dst;
    AVRational filter_tb = frame->time_base;
    const int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

    if (frame->pts == AV_NOPTS_VALUE)
        goto early_exit;

    tb.den <<= extra_bits;
    float_pts = av_rescale_q(frame->pts, filter_tb, tb) -
                av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
    float_pts /= 1 << extra_bits;
    // when float_pts is not exactly an integer,
    // avoid exact midpoints to reduce the chance of rounding differences, this
    // can be removed in case the fps code is changed to work with integers
    if (float_pts != llrint(float_pts))
        float_pts += FFSIGN(float_pts) * 1.0 / (1<<17);

    frame->pts = av_rescale_q(frame->pts, filter_tb, tb_dst) -
                 av_rescale_q(start_time, AV_TIME_BASE_Q, tb_dst);
    frame->time_base = tb_dst;

early_exit:

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
               frame ? av_ts2str(frame->pts) : "NULL",
               av_ts2timestr(frame->pts, &tb_dst),
               float_pts, tb_dst.num, tb_dst.den);
    }

    return float_pts;
}

/* Convert frame timestamps to the encoder timebase and decide how many times
 * should this (and possibly previous) frame be repeated in order to conform to
 * desired target framerate (if any).
 */
static void video_sync_process(OutputFilterPriv *ofp, AVFrame *frame,
                               int64_t *nb_frames, int64_t *nb_frames_prev)
{
    OutputFilter   *ofilter = &ofp->ofilter;
    FPSConvContext     *fps = &ofp->fps;
    double delta0, delta, sync_ipts, duration;

    if (!frame) {
        *nb_frames_prev = *nb_frames = mid_pred(fps->frames_prev_hist[0],
                                                fps->frames_prev_hist[1],
                                                fps->frames_prev_hist[2]);

        if (!*nb_frames && fps->last_dropped) {
            atomic_fetch_add(&ofilter->nb_frames_drop, 1);
            fps->last_dropped++;
        }

        goto finish;
    }

    duration = frame->duration * av_q2d(frame->time_base) / av_q2d(ofp->tb_out);

    sync_ipts = adjust_frame_pts_to_encoder_tb(frame, ofp->tb_out, ofp->ts_offset);
    /* delta0 is the "drift" between the input frame and
     * where it would fall in the output. */
    delta0 = sync_ipts - ofp->next_pts;
    delta  = delta0 + duration;

    // tracks the number of times the PREVIOUS frame should be duplicated,
    // mostly for variable framerate (VFR)
    *nb_frames_prev = 0;
    /* by default, we output a single frame */
    *nb_frames = 1;

    if (delta0 < 0 &&
        delta > 0 &&
        fps->vsync_method != VSYNC_PASSTHROUGH
#if FFMPEG_OPT_VSYNC_DROP
        && fps->vsync_method != VSYNC_DROP
#endif
        ) {
        if (delta0 < -0.6) {
            av_log(ofp, AV_LOG_VERBOSE, "Past duration %f too large\n", -delta0);
        } else
            av_log(ofp, AV_LOG_DEBUG, "Clipping frame in rate conversion by %f\n", -delta0);
        sync_ipts = ofp->next_pts;
        duration += delta0;
        delta0 = 0;
    }

    switch (fps->vsync_method) {
    case VSYNC_VSCFR:
        if (fps->frame_number == 0 && delta0 >= 0.5) {
            av_log(ofp, AV_LOG_DEBUG, "Not duplicating %d initial frames\n", (int)lrintf(delta0));
            delta = duration;
            delta0 = 0;
            ofp->next_pts = llrint(sync_ipts);
        }
    case VSYNC_CFR:
        // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (frame_drop_threshold && delta < frame_drop_threshold && fps->frame_number) {
            *nb_frames = 0;
        } else if (delta < -1.1)
            *nb_frames = 0;
        else if (delta > 1.1) {
            *nb_frames = llrintf(delta);
            if (delta0 > 1.1)
                *nb_frames_prev = llrintf(delta0 - 0.6);
        }
        frame->duration = 1;
        break;
    case VSYNC_VFR:
        if (delta <= -0.6)
            *nb_frames = 0;
        else if (delta > 0.6)
            ofp->next_pts = llrint(sync_ipts);
        frame->duration = llrint(duration);
        break;
#if FFMPEG_OPT_VSYNC_DROP
    case VSYNC_DROP:
#endif
    case VSYNC_PASSTHROUGH:
        ofp->next_pts = llrint(sync_ipts);
        frame->duration = llrint(duration);
        break;
    default:
        av_assert0(0);
    }

finish:
    memmove(fps->frames_prev_hist + 1,
            fps->frames_prev_hist,
            sizeof(fps->frames_prev_hist[0]) * (FF_ARRAY_ELEMS(fps->frames_prev_hist) - 1));
    fps->frames_prev_hist[0] = *nb_frames_prev;

    if (*nb_frames_prev == 0 && fps->last_dropped) {
        atomic_fetch_add(&ofilter->nb_frames_drop, 1);
        av_log(ofp, AV_LOG_VERBOSE,
               "*** dropping frame %"PRId64" at ts %"PRId64"\n",
               fps->frame_number, fps->last_frame->pts);
    }
    if (*nb_frames > (*nb_frames_prev && fps->last_dropped) + (*nb_frames > *nb_frames_prev)) {
        uint64_t nb_frames_dup;
        if (*nb_frames > dts_error_threshold * 30) {
            av_log(ofp, AV_LOG_ERROR, "%"PRId64" frame duplication too large, skipping\n", *nb_frames - 1);
            atomic_fetch_add(&ofilter->nb_frames_drop, 1);
            *nb_frames = 0;
            return;
        }
        nb_frames_dup = atomic_fetch_add(&ofilter->nb_frames_dup,
                                         *nb_frames - (*nb_frames_prev && fps->last_dropped) - (*nb_frames > *nb_frames_prev));
        av_log(ofp, AV_LOG_VERBOSE, "*** %"PRId64" dup!\n", *nb_frames - 1);
        if (nb_frames_dup > fps->dup_warning) {
            av_log(ofp, AV_LOG_WARNING, "More than %"PRIu64" frames duplicated\n", fps->dup_warning);
            fps->dup_warning *= 10;
        }
    }

    fps->last_dropped = *nb_frames == *nb_frames_prev && frame;
    fps->dropped_keyframe |= fps->last_dropped && (frame->flags & AV_FRAME_FLAG_KEY);
}

static int close_output(OutputFilterPriv *ofp, FilterGraphThread *fgt)
{
    FilterGraphPriv *fgp = fgp_from_fg(ofp->ofilter.graph);
    int ret;

    // we are finished and no frames were ever seen at this output,
    // at least initialize the encoder with a dummy frame
    if (!fgt->got_frame) {
        AVFrame *frame = fgt->frame;
        FrameData *fd;

        frame->time_base   = ofp->tb_out;
        frame->format      = ofp->format;

        frame->width               = ofp->width;
        frame->height              = ofp->height;
        frame->sample_aspect_ratio = ofp->sample_aspect_ratio;

        frame->sample_rate = ofp->sample_rate;
        if (ofp->ch_layout.nb_channels) {
            ret = av_channel_layout_copy(&frame->ch_layout, &ofp->ch_layout);
            if (ret < 0)
                return ret;
        }

        fd = frame_data(frame);
        if (!fd)
            return AVERROR(ENOMEM);

        fd->frame_rate_filter = ofp->fps.framerate;

        av_assert0(!frame->buf[0]);

        av_log(ofp, AV_LOG_WARNING,
               "No filtered frames for output stream, trying to "
               "initialize anyway.\n");

        ret = sch_filter_send(fgp->sch, fgp->sch_idx, ofp->index, frame);
        if (ret < 0) {
            av_frame_unref(frame);
            return ret;
        }
    }

    fgt->eof_out[ofp->index] = 1;

    ret = sch_filter_send(fgp->sch, fgp->sch_idx, ofp->index, NULL);
    return (ret == AVERROR_EOF) ? 0 : ret;
}

static int fg_output_frame(OutputFilterPriv *ofp, FilterGraphThread *fgt,
                           AVFrame *frame)
{
    FilterGraphPriv  *fgp = fgp_from_fg(ofp->ofilter.graph);
    AVFrame   *frame_prev = ofp->fps.last_frame;
    enum AVMediaType type = ofp->ofilter.type;

    int64_t nb_frames = !!frame, nb_frames_prev = 0;

    if (type == AVMEDIA_TYPE_VIDEO && (frame || fgt->got_frame))
        video_sync_process(ofp, frame, &nb_frames, &nb_frames_prev);

    for (int64_t i = 0; i < nb_frames; i++) {
        AVFrame *frame_out;
        int ret;

        if (type == AVMEDIA_TYPE_VIDEO) {
            AVFrame *frame_in = (i < nb_frames_prev && frame_prev->buf[0]) ?
                                frame_prev : frame;
            if (!frame_in)
                break;

            frame_out = fgp->frame_enc;
            ret = av_frame_ref(frame_out, frame_in);
            if (ret < 0)
                return ret;

            frame_out->pts = ofp->next_pts;

            if (ofp->fps.dropped_keyframe) {
                frame_out->flags |= AV_FRAME_FLAG_KEY;
                ofp->fps.dropped_keyframe = 0;
            }
        } else {
            frame->pts = (frame->pts == AV_NOPTS_VALUE) ? ofp->next_pts :
                av_rescale_q(frame->pts,   frame->time_base, ofp->tb_out) -
                av_rescale_q(ofp->ts_offset, AV_TIME_BASE_Q, ofp->tb_out);

            frame->time_base = ofp->tb_out;
            frame->duration  = av_rescale_q(frame->nb_samples,
                                            (AVRational){ 1, frame->sample_rate },
                                            ofp->tb_out);

            ofp->next_pts = frame->pts + frame->duration;

            frame_out = frame;
        }

        // send the frame to consumers
        ret = sch_filter_send(fgp->sch, fgp->sch_idx, ofp->index, frame_out);
        if (ret < 0) {
            av_frame_unref(frame_out);

            if (!fgt->eof_out[ofp->index]) {
                fgt->eof_out[ofp->index] = 1;
                fgp->nb_outputs_done++;
            }

            return ret == AVERROR_EOF ? 0 : ret;
        }

        if (type == AVMEDIA_TYPE_VIDEO) {
            ofp->fps.frame_number++;
            ofp->next_pts++;

            if (i == nb_frames_prev && frame)
                frame->flags &= ~AV_FRAME_FLAG_KEY;
        }

        fgt->got_frame = 1;
    }

    if (frame && frame_prev) {
        av_frame_unref(frame_prev);
        av_frame_move_ref(frame_prev, frame);
    }

    if (!frame)
        return close_output(ofp, fgt);

    return 0;
}

static int fg_output_step(OutputFilterPriv *ofp, FilterGraphThread *fgt,
                          AVFrame *frame)
{
    FilterGraphPriv    *fgp = fgp_from_fg(ofp->ofilter.graph);
    AVFilterContext *filter = ofp->filter;
    FrameData *fd;
    int ret;

    ret = av_buffersink_get_frame_flags(filter, frame,
                                        AV_BUFFERSINK_FLAG_NO_REQUEST);
    if (ret == AVERROR_EOF && !fgt->eof_out[ofp->index]) {
        ret = fg_output_frame(ofp, fgt, NULL);
        return (ret < 0) ? ret : 1;
    } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 1;
    } else if (ret < 0) {
        av_log(ofp, AV_LOG_WARNING,
               "Error in retrieving a frame from the filtergraph: %s\n",
               av_err2str(ret));
        return ret;
    }

    if (fgt->eof_out[ofp->index]) {
        av_frame_unref(frame);
        return 0;
    }

    frame->time_base = av_buffersink_get_time_base(filter);

    if (debug_ts)
        av_log(ofp, AV_LOG_INFO, "filter_raw -> pts:%s pts_time:%s time_base:%d/%d\n",
               av_ts2str(frame->pts), av_ts2timestr(frame->pts, &frame->time_base),
                         frame->time_base.num, frame->time_base.den);

    // Choose the output timebase the first time we get a frame.
    if (!ofp->tb_out_locked) {
        ret = choose_out_timebase(ofp, frame);
        if (ret < 0) {
            av_log(ofp, AV_LOG_ERROR, "Could not choose an output time base\n");
            av_frame_unref(frame);
            return ret;
        }
    }

    fd = frame_data(frame);
    if (!fd) {
        av_frame_unref(frame);
        return AVERROR(ENOMEM);
    }

    fd->wallclock[LATENCY_PROBE_FILTER_POST] = av_gettime_relative();

    // only use bits_per_raw_sample passed through from the decoder
    // if the filtergraph did not touch the frame data
    if (!fgp->is_meta)
        fd->bits_per_raw_sample = 0;

    if (ofp->ofilter.type == AVMEDIA_TYPE_VIDEO) {
        if (!frame->duration) {
            AVRational fr = av_buffersink_get_frame_rate(filter);
            if (fr.num > 0 && fr.den > 0)
                frame->duration = av_rescale_q(1, av_inv_q(fr), frame->time_base);
        }

        fd->frame_rate_filter = ofp->fps.framerate;
    }

    ret = fg_output_frame(ofp, fgt, frame);
    av_frame_unref(frame);
    if (ret < 0)
        return ret;

    return 0;
}

/* retrieve all frames available at filtergraph outputs
 * and send them to consumers */
static int read_frames(FilterGraph *fg, FilterGraphThread *fgt,
                       AVFrame *frame)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    int did_step = 0;

    // graph not configured, just select the input to request
    if (!fgt->graph) {
        for (int i = 0; i < fg->nb_inputs; i++) {
            InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
            if (ifp->format < 0 && !fgt->eof_in[i]) {
                fgt->next_in = i;
                return 0;
            }
        }

        // This state - graph is not configured, but all inputs are either
        // initialized or EOF - should be unreachable because sending EOF to a
        // filter without even a fallback format should fail
        av_assert0(0);
        return AVERROR_BUG;
    }

    while (fgp->nb_outputs_done < fg->nb_outputs) {
        int ret;

        ret = avfilter_graph_request_oldest(fgt->graph);
        if (ret == AVERROR(EAGAIN)) {
            fgt->next_in = choose_input(fg, fgt);
            break;
        } else if (ret < 0) {
            if (ret == AVERROR_EOF)
                av_log(fg, AV_LOG_VERBOSE, "Filtergraph returned EOF, finishing\n");
            else
                av_log(fg, AV_LOG_ERROR,
                       "Error requesting a frame from the filtergraph: %s\n",
                       av_err2str(ret));
            return ret;
        }
        fgt->next_in = fg->nb_inputs;

        // return after one iteration, so that scheduler can rate-control us
        if (did_step && fgp->have_sources)
            return 0;

        /* Reap all buffers present in the buffer sinks */
        for (int i = 0; i < fg->nb_outputs; i++) {
            OutputFilterPriv *ofp = ofp_from_ofilter(fg->outputs[i]);

            ret = 0;
            while (!ret) {
                ret = fg_output_step(ofp, fgt, frame);
                if (ret < 0)
                    return ret;
            }
        }
        did_step = 1;
    }

    return (fgp->nb_outputs_done == fg->nb_outputs) ? AVERROR_EOF : 0;
}

static void sub2video_heartbeat(InputFilter *ifilter, int64_t pts, AVRational tb)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int64_t pts2;

    /* subtitles seem to be usually muxed ahead of other streams;
       if not, subtracting a larger time here is necessary */
    pts2 = av_rescale_q(pts, tb, ifp->time_base) - 1;

    /* do not send the heartbeat frame if the subtitle is already ahead */
    if (pts2 <= ifp->sub2video.last_pts)
        return;

    if (pts2 >= ifp->sub2video.end_pts || ifp->sub2video.initialize)
        /* if we have hit the end of the current displayed subpicture,
           or if we need to initialize the system, update the
           overlayed subpicture and its start/end times */
        sub2video_update(ifp, pts2 + 1, NULL);
    else
        sub2video_push_ref(ifp, pts2);
}

static int sub2video_frame(InputFilter *ifilter, AVFrame *frame, int buffer)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int ret;

    if (buffer) {
        AVFrame *tmp;

        if (!frame)
            return 0;

        tmp = av_frame_alloc();
        if (!tmp)
            return AVERROR(ENOMEM);

        av_frame_move_ref(tmp, frame);

        ret = av_fifo_write(ifp->frame_queue, &tmp, 1);
        if (ret < 0) {
            av_frame_free(&tmp);
            return ret;
        }

        return 0;
    }

    // heartbeat frame
    if (frame && !frame->buf[0]) {
        sub2video_heartbeat(ifilter, frame->pts, frame->time_base);
        return 0;
    }

    if (!frame) {
        if (ifp->sub2video.end_pts < INT64_MAX)
            sub2video_update(ifp, INT64_MAX, NULL);

        return av_buffersrc_add_frame(ifp->filter, NULL);
    }

    ifp->width  = frame->width  ? frame->width  : ifp->width;
    ifp->height = frame->height ? frame->height : ifp->height;

    sub2video_update(ifp, INT64_MIN, (const AVSubtitle*)frame->buf[0]->data);

    return 0;
}

static int send_eof(FilterGraphThread *fgt, InputFilter *ifilter,
                    int64_t pts, AVRational tb)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int ret;

    if (fgt->eof_in[ifp->index])
       return 0;

    fgt->eof_in[ifp->index] = 1;

    if (ifp->filter) {
        pts = av_rescale_q_rnd(pts, tb, ifp->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

        ret = av_buffersrc_close(ifp->filter, pts, AV_BUFFERSRC_FLAG_PUSH);
        if (ret < 0)
            return ret;
    } else {
        if (ifp->format < 0) {
            // the filtergraph was never configured, use the fallback parameters
            ifp->format                 = ifp->opts.fallback->format;
            ifp->sample_rate            = ifp->opts.fallback->sample_rate;
            ifp->width                  = ifp->opts.fallback->width;
            ifp->height                 = ifp->opts.fallback->height;
            ifp->sample_aspect_ratio    = ifp->opts.fallback->sample_aspect_ratio;
            ifp->color_space            = ifp->opts.fallback->colorspace;
            ifp->color_range            = ifp->opts.fallback->color_range;
            ifp->time_base              = ifp->opts.fallback->time_base;

            ret = av_channel_layout_copy(&ifp->ch_layout,
                                         &ifp->opts.fallback->ch_layout);
            if (ret < 0)
                return ret;

            if (ifilter_has_all_input_formats(ifilter->graph)) {
                ret = configure_filtergraph(ifilter->graph, fgt);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error initializing filters!\n");
                    return ret;
                }
            }
        }

        if (ifp->format < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot determine format of input %s after EOF\n",
                   ifp->opts.name);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

enum ReinitReason {
    VIDEO_CHANGED   = (1 << 0),
    AUDIO_CHANGED   = (1 << 1),
    MATRIX_CHANGED  = (1 << 2),
    HWACCEL_CHANGED = (1 << 3)
};

static const char *unknown_if_null(const char *str)
{
    return str ? str : "unknown";
}

static int send_frame(FilterGraph *fg, FilterGraphThread *fgt,
                      InputFilter *ifilter, AVFrame *frame)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    FrameData       *fd;
    AVFrameSideData *sd;
    int need_reinit = 0, ret;

    /* determine if the parameters for this input changed */
    switch (ifp->type) {
    case AVMEDIA_TYPE_AUDIO:
        if (ifp->format      != frame->format ||
            ifp->sample_rate != frame->sample_rate ||
            av_channel_layout_compare(&ifp->ch_layout, &frame->ch_layout))
            need_reinit |= AUDIO_CHANGED;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (ifp->format != frame->format ||
            ifp->width  != frame->width ||
            ifp->height != frame->height ||
            ifp->color_space != frame->colorspace ||
            ifp->color_range != frame->color_range)
            need_reinit |= VIDEO_CHANGED;
        break;
    }

    if (sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        if (!ifp->displaymatrix_present ||
            memcmp(sd->data, ifp->displaymatrix, sizeof(ifp->displaymatrix)))
            need_reinit |= MATRIX_CHANGED;
    } else if (ifp->displaymatrix_present)
        need_reinit |= MATRIX_CHANGED;

    if (!(ifp->opts.flags & IFILTER_FLAG_REINIT) && fgt->graph)
        need_reinit = 0;

    if (!!ifp->hw_frames_ctx != !!frame->hw_frames_ctx ||
        (ifp->hw_frames_ctx && ifp->hw_frames_ctx->data != frame->hw_frames_ctx->data))
        need_reinit |= HWACCEL_CHANGED;

    if (need_reinit) {
        ret = ifilter_parameters_from_frame(ifilter, frame);
        if (ret < 0)
            return ret;
    }

    /* (re)init the graph if possible, otherwise buffer the frame and return */
    if (need_reinit || !fgt->graph) {
        AVFrame *tmp = av_frame_alloc();

        if (!tmp)
            return AVERROR(ENOMEM);

        if (!ifilter_has_all_input_formats(fg)) {
            av_frame_move_ref(tmp, frame);

            ret = av_fifo_write(ifp->frame_queue, &tmp, 1);
            if (ret < 0)
                av_frame_free(&tmp);

            return ret;
        }

        ret = fgt->graph ? read_frames(fg, fgt, tmp) : 0;
        av_frame_free(&tmp);
        if (ret < 0)
            return ret;

        if (fgt->graph) {
            AVBPrint reason;
            av_bprint_init(&reason, 0, AV_BPRINT_SIZE_AUTOMATIC);
            if (need_reinit & AUDIO_CHANGED) {
                const char *sample_format_name = av_get_sample_fmt_name(frame->format);
                av_bprintf(&reason, "audio parameters changed to %d Hz, ", frame->sample_rate);
                av_channel_layout_describe_bprint(&frame->ch_layout, &reason);
                av_bprintf(&reason, ", %s, ", unknown_if_null(sample_format_name));
            }
            if (need_reinit & VIDEO_CHANGED) {
                const char *pixel_format_name = av_get_pix_fmt_name(frame->format);
                const char *color_space_name = av_color_space_name(frame->colorspace);
                const char *color_range_name = av_color_range_name(frame->color_range);
                av_bprintf(&reason, "video parameters changed to %s(%s, %s), %dx%d, ",
                        unknown_if_null(pixel_format_name), unknown_if_null(color_range_name),
                        unknown_if_null(color_space_name), frame->width, frame->height);
            }
            if (need_reinit & MATRIX_CHANGED)
                av_bprintf(&reason, "display matrix changed, ");
            if (need_reinit & HWACCEL_CHANGED)
                av_bprintf(&reason, "hwaccel changed, ");
            if (reason.len > 1)
                reason.str[reason.len - 2] = '\0'; // remove last comma
            av_log(fg, AV_LOG_INFO, "Reconfiguring filter graph%s%s\n", reason.len ? " because " : "", reason.str);
        }

        ret = configure_filtergraph(fg, fgt);
        if (ret < 0) {
            av_log(fg, AV_LOG_ERROR, "Error reinitializing filters!\n");
            return ret;
        }
    }

    frame->pts       = av_rescale_q(frame->pts,      frame->time_base, ifp->time_base);
    frame->duration  = av_rescale_q(frame->duration, frame->time_base, ifp->time_base);
    frame->time_base = ifp->time_base;

    if (ifp->displaymatrix_applied)
        av_frame_remove_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);

    fd = frame_data(frame);
    if (!fd)
        return AVERROR(ENOMEM);
    fd->wallclock[LATENCY_PROBE_FILTER_PRE] = av_gettime_relative();

    ret = av_buffersrc_add_frame_flags(ifp->filter, frame,
                                       AV_BUFFERSRC_FLAG_PUSH);
    if (ret < 0) {
        av_frame_unref(frame);
        if (ret != AVERROR_EOF)
            av_log(fg, AV_LOG_ERROR, "Error while filtering: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

static void fg_thread_set_name(const FilterGraph *fg)
{
    char name[16];
    if (filtergraph_is_simple(fg)) {
        OutputFilterPriv *ofp = ofp_from_ofilter(fg->outputs[0]);
        snprintf(name, sizeof(name), "%cf%s",
                 av_get_media_type_string(ofp->ofilter.type)[0],
                 ofp->name);
    } else {
        snprintf(name, sizeof(name), "fc%d", fg->index);
    }

    ff_thread_setname(name);
}

static void fg_thread_uninit(FilterGraphThread *fgt)
{
    if (fgt->frame_queue_out) {
        AVFrame *frame;
        while (av_fifo_read(fgt->frame_queue_out, &frame, 1) >= 0)
            av_frame_free(&frame);
        av_fifo_freep2(&fgt->frame_queue_out);
    }

    av_frame_free(&fgt->frame);
    av_freep(&fgt->eof_in);
    av_freep(&fgt->eof_out);

    avfilter_graph_free(&fgt->graph);

    memset(fgt, 0, sizeof(*fgt));
}

static int fg_thread_init(FilterGraphThread *fgt, const FilterGraph *fg)
{
    memset(fgt, 0, sizeof(*fgt));

    fgt->frame = av_frame_alloc();
    if (!fgt->frame)
        goto fail;

    fgt->eof_in = av_calloc(fg->nb_inputs, sizeof(*fgt->eof_in));
    if (!fgt->eof_in)
        goto fail;

    fgt->eof_out = av_calloc(fg->nb_outputs, sizeof(*fgt->eof_out));
    if (!fgt->eof_out)
        goto fail;

    fgt->frame_queue_out = av_fifo_alloc2(1, sizeof(AVFrame*), AV_FIFO_FLAG_AUTO_GROW);
    if (!fgt->frame_queue_out)
        goto fail;

    return 0;

fail:
    fg_thread_uninit(fgt);
    return AVERROR(ENOMEM);
}

static int filter_thread(void *arg)
{
    FilterGraphPriv *fgp = arg;
    FilterGraph      *fg = &fgp->fg;

    FilterGraphThread fgt;
    int ret = 0, input_status = 0;

    ret = fg_thread_init(&fgt, fg);
    if (ret < 0)
        goto finish;

    fg_thread_set_name(fg);

    // if we have all input parameters the graph can now be configured
    if (ifilter_has_all_input_formats(fg)) {
        ret = configure_filtergraph(fg, &fgt);
        if (ret < 0) {
            av_log(fg, AV_LOG_ERROR, "Error configuring filter graph: %s\n",
                   av_err2str(ret));
            goto finish;
        }
    }

    while (1) {
        InputFilter *ifilter;
        InputFilterPriv *ifp;
        enum FrameOpaque o;
        unsigned input_idx = fgt.next_in;

        input_status = sch_filter_receive(fgp->sch, fgp->sch_idx,
                                          &input_idx, fgt.frame);
        if (input_status == AVERROR_EOF) {
            av_log(fg, AV_LOG_VERBOSE, "Filtering thread received EOF\n");
            break;
        } else if (input_status == AVERROR(EAGAIN)) {
            // should only happen when we didn't request any input
            av_assert0(input_idx == fg->nb_inputs);
            goto read_frames;
        }
        av_assert0(input_status >= 0);

        o = (intptr_t)fgt.frame->opaque;

        o = (intptr_t)fgt.frame->opaque;

        // message on the control stream
        if (input_idx == fg->nb_inputs) {
            FilterCommand *fc;

            av_assert0(o == FRAME_OPAQUE_SEND_COMMAND && fgt.frame->buf[0]);

            fc = (FilterCommand*)fgt.frame->buf[0]->data;
            send_command(fg, fgt.graph, fc->time, fc->target, fc->command, fc->arg,
                         fc->all_filters);
            av_frame_unref(fgt.frame);
            continue;
        }

        // we received an input frame or EOF
        ifilter   = fg->inputs[input_idx];
        ifp       = ifp_from_ifilter(ifilter);

        if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE) {
            int hb_frame = input_status >= 0 && o == FRAME_OPAQUE_SUB_HEARTBEAT;
            ret = sub2video_frame(ifilter, (fgt.frame->buf[0] || hb_frame) ? fgt.frame : NULL,
                                  !fgt.graph);
        } else if (fgt.frame->buf[0]) {
            ret = send_frame(fg, &fgt, ifilter, fgt.frame);
        } else {
            av_assert1(o == FRAME_OPAQUE_EOF);
            ret = send_eof(&fgt, ifilter, fgt.frame->pts, fgt.frame->time_base);
        }
        av_frame_unref(fgt.frame);
        if (ret == AVERROR_EOF) {
            av_log(fg, AV_LOG_VERBOSE, "Input %u no longer accepts new data\n",
                   input_idx);
            sch_filter_receive_finish(fgp->sch, fgp->sch_idx, input_idx);
            continue;
        }
        if (ret < 0)
            goto finish;

read_frames:
        // retrieve all newly avalable frames
        ret = read_frames(fg, &fgt, fgt.frame);
        if (ret == AVERROR_EOF) {
            av_log(fg, AV_LOG_VERBOSE, "All consumers returned EOF\n");
            break;
        } else if (ret < 0) {
            av_log(fg, AV_LOG_ERROR, "Error sending frames to consumers: %s\n",
                   av_err2str(ret));
            goto finish;
        }
    }

    for (unsigned i = 0; i < fg->nb_outputs; i++) {
        OutputFilterPriv *ofp = ofp_from_ofilter(fg->outputs[i]);

        if (fgt.eof_out[i] || !fgt.graph)
            continue;

        ret = fg_output_frame(ofp, &fgt, NULL);
        if (ret < 0)
            goto finish;
    }

finish:
    // EOF is normal termination
    if (ret == AVERROR_EOF)
        ret = 0;

    fg_thread_uninit(&fgt);

    return ret;
}

void fg_send_command(FilterGraph *fg, double time, const char *target,
                     const char *command, const char *arg, int all_filters)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    AVBufferRef *buf;
    FilterCommand *fc;

    fc = av_mallocz(sizeof(*fc));
    if (!fc)
        return;

    buf = av_buffer_create((uint8_t*)fc, sizeof(*fc), filter_command_free, NULL, 0);
    if (!buf) {
        av_freep(&fc);
        return;
    }

    fc->target  = av_strdup(target);
    fc->command = av_strdup(command);
    fc->arg     = av_strdup(arg);
    if (!fc->target || !fc->command || !fc->arg) {
        av_buffer_unref(&buf);
        return;
    }

    fc->time        = time;
    fc->all_filters = all_filters;

    fgp->frame->buf[0] = buf;
    fgp->frame->opaque = (void*)(intptr_t)FRAME_OPAQUE_SEND_COMMAND;

    sch_filter_command(fgp->sch, fgp->sch_idx, fgp->frame);
}
