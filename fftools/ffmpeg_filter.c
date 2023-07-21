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
#include "libavutil/display.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"

typedef struct FilterGraphPriv {
    FilterGraph fg;

    // name used for logging
    char log_name[32];

    int is_simple;
    // true when the filtergraph contains only meta filters
    // that do not modify the frame data
    int is_meta;
    int disable_conversions;

    const char *graph_desc;

    // frame for temporarily holding output from the filtergraph
    AVFrame *frame;
} FilterGraphPriv;

static FilterGraphPriv *fgp_from_fg(FilterGraph *fg)
{
    return (FilterGraphPriv*)fg;
}

static const FilterGraphPriv *cfgp_from_cfg(const FilterGraph *fg)
{
    return (const FilterGraphPriv*)fg;
}

typedef struct InputFilterPriv {
    InputFilter ifilter;

    AVFilterContext *filter;

    InputStream *ist;

    // used to hold submitted input
    AVFrame *frame;

    /* for filters that are not yet bound to an input stream,
     * this stores the input linklabel, if any */
    uint8_t *linklabel;

    // filter data type
    enum AVMediaType type;
    // source data type: AVMEDIA_TYPE_SUBTITLE for sub2video,
    // same as type otherwise
    enum AVMediaType type_src;

    int eof;

    // parameters configured for this input
    int format;

    int width, height;
    AVRational sample_aspect_ratio;

    int sample_rate;
    AVChannelLayout ch_layout;

    AVRational time_base;

    AVFifo *frame_queue;

    AVBufferRef *hw_frames_ctx;

    int     displaymatrix_present;
    int32_t displaymatrix[9];

    // fallback parameters to use when no input is ever sent
    struct {
        int                 format;

        int                 width;
        int                 height;
        AVRational          sample_aspect_ratio;

        int                 sample_rate;
        AVChannelLayout     ch_layout;
    } fallback;

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

typedef struct OutputFilterPriv {
    OutputFilter        ofilter;

    AVFilterContext    *filter;

    /* desired output stream properties */
    int format;
    int width, height;
    int sample_rate;
    AVChannelLayout ch_layout;

    AVRational time_base;
    AVRational sample_aspect_ratio;

    // those are only set if no format is specified and the encoder gives us multiple options
    // They point directly to the relevant lists of the encoder.
    const int *formats;
    const AVChannelLayout *ch_layouts;
    const int *sample_rates;

    // set to 1 after at least one frame passed through this output
    int got_frame;
} OutputFilterPriv;

static OutputFilterPriv *ofp_from_ofilter(OutputFilter *ofilter)
{
    return (OutputFilterPriv*)ofilter;
}

static int configure_filtergraph(FilterGraph *fg);

static int sub2video_get_blank_frame(InputFilterPriv *ifp)
{
    AVFrame *frame = ifp->sub2video.frame;
    int ret;

    av_frame_unref(frame);

    frame->width  = ifp->width;
    frame->height = ifp->height;
    frame->format = ifp->format;

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
    int num_rects, i;
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
    for (i = 0; i < num_rects; i++)
        sub2video_copy_rect(dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(ifp, pts);
    ifp->sub2video.end_pts = end_pts;
    ifp->sub2video.initialize = 0;
}

/* *dst may return be set to NULL (no pixel format found), a static string or a
 * string backed by the bprint. Nothing has been written to the AVBPrint in case
 * NULL is returned. The AVBPrint provided should be clean. */
static int choose_pix_fmts(OutputFilter *ofilter, AVBPrint *bprint,
                           const char **dst)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    OutputStream *ost = ofilter->ost;

    *dst = NULL;

    if (ost->keep_pix_fmt || ofp->format != AV_PIX_FMT_NONE) {
        *dst = ofp->format == AV_PIX_FMT_NONE ? NULL :
               av_get_pix_fmt_name(ofp->format);
    } else if (ofp->formats) {
        const enum AVPixelFormat *p = ofp->formats;

        for (; *p != AV_PIX_FMT_NONE; p++) {
            const char *name = av_get_pix_fmt_name(*p);
            av_bprintf(bprint, "%s%c", name, p[1] == AV_PIX_FMT_NONE ? '\0' : '|');
        }
        if (!av_bprint_is_complete(bprint))
            return AVERROR(ENOMEM);

        *dst = bprint->str;
    }

    return 0;
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

//DEF_CHOOSE_FORMAT(pix_fmts, enum AVPixelFormat, format, formats, AV_PIX_FMT_NONE,
//                  GET_PIX_FMT_NAME)

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
    int i;
    for (i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        if (ifp->format < 0)
            return 0;
    }
    return 1;
}

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

static OutputFilter *ofilter_alloc(FilterGraph *fg)
{
    OutputFilterPriv *ofp;
    OutputFilter *ofilter;

    ofp = allocate_array_elem(&fg->outputs, sizeof(*ofp), &fg->nb_outputs);
    if (!ofp)
        return NULL;

    ofilter           = &ofp->ofilter;
    ofilter->graph    = fg;
    ofp->format       = -1;
    ofilter->last_pts = AV_NOPTS_VALUE;

    return ofilter;
}

static int ifilter_bind_ist(InputFilter *ifilter, InputStream *ist)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int ret;

    av_assert0(!ifp->ist);

    ifp->ist             = ist;
    ifp->type_src        = ist->st->codecpar->codec_type;

    ret = ist_filter_add(ist, ifilter, filtergraph_is_simple(ifilter->graph));
    if (ret < 0)
        return ret;

    if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE) {
        ifp->sub2video.frame = av_frame_alloc();
        if (!ifp->sub2video.frame)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int set_channel_layout(OutputFilterPriv *f, OutputStream *ost)
{
    const AVCodec *c = ost->enc_ctx->codec;
    int i, err;

    if (ost->enc_ctx->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
        /* Pass the layout through for all orders but UNSPEC */
        err = av_channel_layout_copy(&f->ch_layout, &ost->enc_ctx->ch_layout);
        if (err < 0)
            return err;
        return 0;
    }

    /* Requested layout is of order UNSPEC */
    if (!c->ch_layouts) {
        /* Use the default native layout for the requested amount of channels when the
           encoder doesn't have a list of supported layouts */
        av_channel_layout_default(&f->ch_layout, ost->enc_ctx->ch_layout.nb_channels);
        return 0;
    }
    /* Encoder has a list of supported layouts. Pick the first layout in it with the
       same amount of channels as the requested layout */
    for (i = 0; c->ch_layouts[i].nb_channels; i++) {
        if (c->ch_layouts[i].nb_channels == ost->enc_ctx->ch_layout.nb_channels)
            break;
    }
    if (c->ch_layouts[i].nb_channels) {
        /* Use it if one is found */
        err = av_channel_layout_copy(&f->ch_layout, &c->ch_layouts[i]);
        if (err < 0)
            return err;
        return 0;
    }
    /* If no layout for the amount of channels requested was found, use the default
       native layout for it. */
    av_channel_layout_default(&f->ch_layout, ost->enc_ctx->ch_layout.nb_channels);

    return 0;
}

int ofilter_bind_ost(OutputFilter *ofilter, OutputStream *ost)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    FilterGraph  *fg = ofilter->graph;
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    const AVCodec *c = ost->enc_ctx->codec;

    av_assert0(!ofilter->ost);

    ofilter->ost = ost;
    av_freep(&ofilter->linklabel);

    switch (ost->enc_ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        ofp->width      = ost->enc_ctx->width;
        ofp->height     = ost->enc_ctx->height;
        if (ost->enc_ctx->pix_fmt != AV_PIX_FMT_NONE) {
            ofp->format = ost->enc_ctx->pix_fmt;
        } else {
            ofp->formats = c->pix_fmts;

            // MJPEG encoder exports a full list of supported pixel formats,
            // but the full-range ones are experimental-only.
            // Restrict the auto-conversion list unless -strict experimental
            // has been specified.
            if (!strcmp(c->name, "mjpeg")) {
                // FIXME: YUV420P etc. are actually supported with full color range,
                // yet the latter information isn't available here.
                static const enum AVPixelFormat mjpeg_formats[] =
                    { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
                      AV_PIX_FMT_NONE };

                const AVDictionaryEntry *strict = av_dict_get(ost->encoder_opts, "strict", NULL, 0);
                int strict_val = ost->enc_ctx->strict_std_compliance;

                if (strict) {
                    const AVOption *o = av_opt_find(ost->enc_ctx, strict->key, NULL, 0, 0);
                    av_assert0(o);
                    av_opt_eval_int(ost->enc_ctx, o, strict->value, &strict_val);
                }

                if (strict_val > FF_COMPLIANCE_UNOFFICIAL)
                    ofp->formats = mjpeg_formats;
            }
        }

        fgp->disable_conversions |= ost->keep_pix_fmt;

        break;
    case AVMEDIA_TYPE_AUDIO:
        if (ost->enc_ctx->sample_fmt != AV_SAMPLE_FMT_NONE) {
            ofp->format = ost->enc_ctx->sample_fmt;
        } else {
            ofp->formats = c->sample_fmts;
        }
        if (ost->enc_ctx->sample_rate) {
            ofp->sample_rate = ost->enc_ctx->sample_rate;
        } else {
            ofp->sample_rates = c->supported_samplerates;
        }
        if (ost->enc_ctx->ch_layout.nb_channels) {
            int ret = set_channel_layout(ofp, ost);
            if (ret < 0)
                return ret;
        } else if (c->ch_layouts) {
            ofp->ch_layouts = c->ch_layouts;
        }
        break;
    }

    // if we have all input parameters and all outputs are bound,
    // the graph can now be configured
    if (ifilter_has_all_input_formats(fg)) {
        int ret;

        for (int i = 0; i < fg->nb_outputs; i++)
            if (!fg->outputs[i]->ost)
                return 0;

        ret = configure_filtergraph(fg);
        if (ret < 0) {
            av_log(fg, AV_LOG_ERROR, "Error configuring filter graph: %s\n",
                   av_err2str(ret));
            return ret;
        }
    }

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

    ifp->format          = -1;
    ifp->fallback.format = -1;

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

    avfilter_graph_free(&fg->graph);
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

        av_channel_layout_uninit(&ifp->fallback.ch_layout);

        av_frame_free(&ifp->frame);

        av_buffer_unref(&ifp->hw_frames_ctx);
        av_freep(&ifp->linklabel);
        av_freep(&ifilter->name);
        av_freep(&fg->inputs[j]);
    }
    av_freep(&fg->inputs);
    for (int j = 0; j < fg->nb_outputs; j++) {
        OutputFilter *ofilter = fg->outputs[j];
        OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);

        av_freep(&ofilter->linklabel);
        av_freep(&ofilter->name);
        av_channel_layout_uninit(&ofp->ch_layout);
        av_freep(&fg->outputs[j]);
    }
    av_freep(&fg->outputs);
    av_freep(&fgp->graph_desc);

    av_frame_free(&fgp->frame);

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

int fg_create(FilterGraph **pfg, char *graph_desc)
{
    FilterGraphPriv *fgp;
    FilterGraph      *fg;

    AVFilterInOut *inputs, *outputs;
    AVFilterGraph *graph;
    int ret = 0;

    fgp = allocate_array_elem(&filtergraphs, sizeof(*fgp), &nb_filtergraphs);
    if (!fgp)
        return AVERROR(ENOMEM);
    fg = &fgp->fg;

    if (pfg)
        *pfg = fg;

    fg->class       = &fg_class;
    fg->index      = nb_filtergraphs - 1;
    fgp->graph_desc = graph_desc;
    fgp->disable_conversions = !auto_conversion_filters;

    snprintf(fgp->log_name, sizeof(fgp->log_name), "fc#%d", fg->index);

    fgp->frame = av_frame_alloc();
    if (!fgp->frame)
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

    for (AVFilterInOut *cur = inputs; cur; cur = cur->next) {
        InputFilter *const ifilter = ifilter_alloc(fg);
        InputFilterPriv       *ifp = ifp_from_ifilter(ifilter);

        ifp->linklabel = cur->name;
        cur->name      = NULL;

        ifp->type      = avfilter_pad_get_type(cur->filter_ctx->input_pads,
                                               cur->pad_idx);
        ifilter->name  = describe_filter_link(fg, cur, 1);
        if (!ifilter->name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (AVFilterInOut *cur = outputs; cur; cur = cur->next) {
        OutputFilter *const ofilter = ofilter_alloc(fg);

        if (!ofilter)
            goto fail;

        ofilter->linklabel = cur->name;
        cur->name          = NULL;

        ofilter->type      = avfilter_pad_get_type(cur->filter_ctx->output_pads,
                                                   cur->pad_idx);
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

fail:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_free(&graph);

    if (ret < 0)
        return ret;

    return 0;
}

int init_simple_filtergraph(InputStream *ist, OutputStream *ost,
                            char *graph_desc)
{
    FilterGraph *fg;
    FilterGraphPriv *fgp;
    int ret;

    ret = fg_create(&fg, graph_desc);
    if (ret < 0)
        return ret;
    fgp = fgp_from_fg(fg);

    fgp->is_simple = 1;

    snprintf(fgp->log_name, sizeof(fgp->log_name), "%cf#%d:%d",
             av_get_media_type_string(ost->type)[0],
             ost->file_index, ost->index);

    if (fg->nb_inputs != 1 || fg->nb_outputs != 1) {
        av_log(fg, AV_LOG_ERROR, "Simple filtergraph '%s' was expected "
               "to have exactly 1 input and 1 output. "
               "However, it had %d input(s) and %d output(s). Please adjust, "
               "or use a complex filtergraph (-filter_complex) instead.\n",
               graph_desc, fg->nb_inputs, fg->nb_outputs);
        return AVERROR(EINVAL);
    }

    ost->filter = fg->outputs[0];

    ret = ifilter_bind_ist(fg->inputs[0], ist);
    if (ret < 0)
        return ret;

    ret = ofilter_bind_ost(fg->outputs[0], ost);
    if (ret < 0)
        return ret;

    return 0;
}

static int init_input_filter(FilterGraph *fg, InputFilter *ifilter)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    InputStream *ist = NULL;
    enum AVMediaType type = ifp->type;
    int i, ret;

    // TODO: support other filter types
    if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) {
        av_log(fg, AV_LOG_FATAL, "Only video and audio filters supported "
               "currently.\n");
        return AVERROR(ENOSYS);
    }

    if (ifp->linklabel) {
        AVFormatContext *s;
        AVStream       *st = NULL;
        char *p;
        int file_idx = strtol(ifp->linklabel, &p, 0);

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
    } else {
        ist = ist_find_unused(type);
        if (!ist) {
            av_log(fg, AV_LOG_FATAL, "Cannot find a matching stream for "
                   "unlabeled input pad %s\n", ifilter->name);
            return AVERROR(EINVAL);
        }
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

int init_complex_filtergraph(FilterGraph *fg)
{
    // bind filtergraph inputs to input streams
    for (int i = 0; i < fg->nb_inputs; i++) {
        int ret = init_input_filter(fg, fg->inputs[i]);
        if (ret < 0)
            return ret;
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

static int configure_output_video_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    OutputStream *ost = ofilter->ost;
    OutputFile    *of = output_files[ost->file_index];
    AVFilterContext *last_filter = out->filter_ctx;
    AVBPrint bprint;
    int pad_idx = out->pad_idx;
    int ret;
    const char *pix_fmts;
    char name[255];

    snprintf(name, sizeof(name), "out_%d_%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofp->filter,
                                       avfilter_get_by_name("buffersink"),
                                       name, NULL, NULL, fg->graph);

    if (ret < 0)
        return ret;

    if ((ofp->width || ofp->height) && ofilter->ost->autoscale) {
        char args[255];
        AVFilterContext *filter;
        const AVDictionaryEntry *e = NULL;

        snprintf(args, sizeof(args), "%d:%d",
                 ofp->width, ofp->height);

        while ((e = av_dict_iterate(ost->sws_dict, e))) {
            av_strlcatf(args, sizeof(args), ":%s=%s", e->key, e->value);
        }

        snprintf(name, sizeof(name), "scaler_out_%d_%d",
                 ost->file_index, ost->index);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                name, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    av_bprint_init(&bprint, 0, AV_BPRINT_SIZE_UNLIMITED);
    ret = choose_pix_fmts(ofilter, &bprint, &pix_fmts);
    if (ret < 0)
        return ret;

    if (pix_fmts) {
        AVFilterContext *filter;

        ret = avfilter_graph_create_filter(&filter,
                                           avfilter_get_by_name("format"),
                                           "format", pix_fmts, NULL, fg->graph);
        av_bprint_finalize(&bprint, NULL);
        if (ret < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx     = 0;
    }

    snprintf(name, sizeof(name), "trim_out_%d_%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;


    if ((ret = avfilter_link(last_filter, pad_idx, ofp->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_audio_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
    OutputStream *ost = ofilter->ost;
    OutputFile    *of = output_files[ost->file_index];
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    AVBPrint args;
    char name[255];
    int ret;

    snprintf(name, sizeof(name), "out_%d_%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofp->filter,
                                       avfilter_get_by_name("abuffersink"),
                                       name, NULL, NULL, fg->graph);
    if (ret < 0)
        return ret;
    if ((ret = av_opt_set_int(ofp->filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        return ret;

#define AUTO_INSERT_FILTER(opt_name, filter_name, arg) do {                 \
    AVFilterContext *filt_ctx;                                              \
                                                                            \
    av_log(fg, AV_LOG_INFO, opt_name " is forwarded to lavfi "              \
           "similarly to -af " filter_name "=%s.\n", arg);                  \
                                                                            \
    ret = avfilter_graph_create_filter(&filt_ctx,                           \
                                       avfilter_get_by_name(filter_name),   \
                                       filter_name, arg, NULL, fg->graph);  \
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
#if FFMPEG_OPT_MAP_CHANNEL
    if (ost->audio_channels_mapped) {
        AVChannelLayout mapped_layout = { 0 };
        int i;
        av_channel_layout_default(&mapped_layout, ost->audio_channels_mapped);
        av_channel_layout_describe_bprint(&mapped_layout, &args);
        for (i = 0; i < ost->audio_channels_mapped; i++)
            if (ost->audio_channels_map[i] != -1)
                av_bprintf(&args, "|c%d=c%d", i, ost->audio_channels_map[i]);

        AUTO_INSERT_FILTER("-map_channel", "pan", args.str);
        av_bprint_clear(&args);
    }
#endif

    choose_sample_fmts(ofp,     &args);
    choose_sample_rates(ofp,    &args);
    choose_channel_layouts(ofp, &args);
    if (!av_bprint_is_complete(&args)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if (args.len) {
        AVFilterContext *format;

        snprintf(name, sizeof(name), "format_out_%d_%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           name, args.str, NULL, fg->graph);
        if (ret < 0)
            goto fail;

        ret = avfilter_link(last_filter, pad_idx, format, 0);
        if (ret < 0)
            goto fail;

        last_filter = format;
        pad_idx = 0;
    }

    if (ost->apad && of->shortest) {
        int i;

        for (i = 0; i < of->nb_streams; i++)
            if (of->streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                break;

        if (i < of->nb_streams) {
            AUTO_INSERT_FILTER("-apad", "apad", ost->apad);
        }
    }

    snprintf(name, sizeof(name), "trim for output stream %d:%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        goto fail;

    if ((ret = avfilter_link(last_filter, pad_idx, ofp->filter, 0)) < 0)
        goto fail;
fail:
    av_bprint_finalize(&args, NULL);

    return ret;
}

static int configure_output_filter(FilterGraph *fg, OutputFilter *ofilter,
                                   AVFilterInOut *out)
{
    if (!ofilter->ost) {
        av_log(fg, AV_LOG_FATAL, "Filter %s has an unconnected output\n", ofilter->name);
        return AVERROR(EINVAL);
    }

    switch (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx)) {
    case AVMEDIA_TYPE_VIDEO: return configure_output_video_filter(fg, ofilter, out);
    case AVMEDIA_TYPE_AUDIO: return configure_output_audio_filter(fg, ofilter, out);
    default: av_assert0(0); return 0;
    }
}

int check_filter_outputs(void)
{
    int i;
    for (i = 0; i < nb_filtergraphs; i++) {
        int n;
        for (n = 0; n < filtergraphs[i]->nb_outputs; n++) {
            OutputFilter *output = filtergraphs[i]->outputs[n];
            if (!output->ost) {
                av_log(filtergraphs[i], AV_LOG_FATAL,
                       "Filter %s has an unconnected output\n", output->name);
                return AVERROR(EINVAL);
            }
        }
    }

    return 0;
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

static int configure_input_video_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

    AVFilterContext *last_filter;
    const AVFilter *buffer_filt = avfilter_get_by_name("buffer");
    const AVPixFmtDescriptor *desc;
    InputStream *ist = ifp->ist;
    InputFile     *f = input_files[ist->file_index];
    AVRational fr = ist->framerate;
    AVRational sar;
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;
    int64_t tsoffset = 0;
    AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();

    if (!par)
        return AVERROR(ENOMEM);
    memset(par, 0, sizeof(*par));
    par->format = AV_PIX_FMT_NONE;

    if (ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        av_log(fg, AV_LOG_ERROR, "Cannot connect video filter to audio input\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (!fr.num)
        fr = ist->framerate_guessed;

    if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE)
        sub2video_prepare(ifp);

    ifp->time_base =  ist->framerate.num ? av_inv_q(ist->framerate) :
                                           ist->st->time_base;

    sar = ifp->sample_aspect_ratio;
    if(!sar.den)
        sar = (AVRational){0,1};
    av_bprint_init(&args, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&args,
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:"
             "pixel_aspect=%d/%d",
             ifp->width, ifp->height, ifp->format,
             ifp->time_base.num, ifp->time_base.den, sar.num, sar.den);
    if (fr.num && fr.den)
        av_bprintf(&args, ":frame_rate=%d/%d", fr.num, fr.den);
    snprintf(name, sizeof(name), "graph %d input from stream %d:%d", fg->index,
             ist->file_index, ist->index);


    if ((ret = avfilter_graph_create_filter(&ifp->filter, buffer_filt, name,
                                            args.str, NULL, fg->graph)) < 0)
        goto fail;
    par->hw_frames_ctx = ifp->hw_frames_ctx;
    ret = av_buffersrc_parameters_set(ifp->filter, par);
    if (ret < 0)
        goto fail;
    av_freep(&par);
    last_filter = ifp->filter;

    desc = av_pix_fmt_desc_get(ifp->format);
    av_assert0(desc);

    // TODO: insert hwaccel enabled filters like transpose_vaapi into the graph
    if (ist->autorotate && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        int32_t *displaymatrix = ifp->displaymatrix;
        double theta;

        if (!ifp->displaymatrix_present)
            displaymatrix = (int32_t *)av_stream_get_side_data(ist->st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
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
    }

    snprintf(name, sizeof(name), "trim_in_%d_%d",
             ist->file_index, ist->index);
    if (copy_ts) {
        tsoffset = f->start_time == AV_NOPTS_VALUE ? 0 : f->start_time;
        if (!start_at_zero && f->ctx->start_time != AV_NOPTS_VALUE)
            tsoffset += f->ctx->start_time;
    }
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ?
                      AV_NOPTS_VALUE : tsoffset, f->recording_time,
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

static int configure_input_audio_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    AVFilterContext *last_filter;
    const AVFilter *abuffer_filt = avfilter_get_by_name("abuffer");
    InputStream *ist = ifp->ist;
    InputFile     *f = input_files[ist->file_index];
    AVBPrint args;
    char name[255];
    int ret, pad_idx = 0;
    int64_t tsoffset = 0;

    if (ist->dec_ctx->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(fg, AV_LOG_ERROR, "Cannot connect audio filter to non audio input\n");
        return AVERROR(EINVAL);
    }

    ifp->time_base = (AVRational){ 1, ifp->sample_rate };

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
    snprintf(name, sizeof(name), "graph_%d_in_%d_%d", fg->index,
             ist->file_index, ist->index);

    if ((ret = avfilter_graph_create_filter(&ifp->filter, abuffer_filt,
                                            name, args.str, NULL,
                                            fg->graph)) < 0)
        return ret;
    last_filter = ifp->filter;

    snprintf(name, sizeof(name), "trim for input stream %d:%d",
             ist->file_index, ist->index);
    if (copy_ts) {
        tsoffset = f->start_time == AV_NOPTS_VALUE ? 0 : f->start_time;
        if (!start_at_zero && f->ctx->start_time != AV_NOPTS_VALUE)
            tsoffset += f->ctx->start_time;
    }
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ?
                      AV_NOPTS_VALUE : tsoffset, f->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;

    return 0;
}

static int configure_input_filter(FilterGraph *fg, InputFilter *ifilter,
                                  AVFilterInOut *in)
{
    switch (ifp_from_ifilter(ifilter)->type) {
    case AVMEDIA_TYPE_VIDEO: return configure_input_video_filter(fg, ifilter, in);
    case AVMEDIA_TYPE_AUDIO: return configure_input_audio_filter(fg, ifilter, in);
    default: av_assert0(0); return 0;
    }
}

static void cleanup_filtergraph(FilterGraph *fg)
{
    int i;
    for (i = 0; i < fg->nb_outputs; i++)
        ofp_from_ofilter(fg->outputs[i])->filter = NULL;
    for (i = 0; i < fg->nb_inputs; i++)
        ifp_from_ifilter(fg->inputs[i])->filter = NULL;
    avfilter_graph_free(&fg->graph);
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

static int configure_filtergraph(FilterGraph *fg)
{
    FilterGraphPriv *fgp = fgp_from_fg(fg);
    AVBufferRef *hw_device;
    AVFilterInOut *inputs, *outputs, *cur;
    int ret, i, simple = filtergraph_is_simple(fg);
    const char *graph_desc = fgp->graph_desc;

    cleanup_filtergraph(fg);
    if (!(fg->graph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    if (simple) {
        OutputStream *ost = fg->outputs[0]->ost;

        if (filter_nbthreads) {
            ret = av_opt_set(fg->graph, "threads", filter_nbthreads, 0);
            if (ret < 0)
                goto fail;
        } else {
            const AVDictionaryEntry *e = NULL;
            e = av_dict_get(ost->encoder_opts, "threads", NULL, 0);
            if (e)
                av_opt_set(fg->graph, "threads", e->value, 0);
        }

        if (av_dict_count(ost->sws_dict)) {
            ret = av_dict_get_string(ost->sws_dict,
                                     &fg->graph->scale_sws_opts,
                                     '=', ':');
            if (ret < 0)
                goto fail;
        }

        if (av_dict_count(ost->swr_opts)) {
            char *args;
            ret = av_dict_get_string(ost->swr_opts, &args, '=', ':');
            if (ret < 0)
                goto fail;
            av_opt_set(fg->graph, "aresample_swr_opts", args, 0);
            av_free(args);
        }
    } else {
        fg->graph->nb_threads = filter_complex_nbthreads;
    }

    hw_device = hw_device_for_filter();

    if ((ret = graph_parse(fg->graph, graph_desc, &inputs, &outputs, hw_device)) < 0)
        goto fail;

    for (cur = inputs, i = 0; cur; cur = cur->next, i++)
        if ((ret = configure_input_filter(fg, fg->inputs[i], cur)) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            goto fail;
        }
    avfilter_inout_free(&inputs);

    for (cur = outputs, i = 0; cur; cur = cur->next, i++) {
        ret = configure_output_filter(fg, fg->outputs[i], cur);
        if (ret < 0) {
            avfilter_inout_free(&outputs);
            goto fail;
        }
    }
    avfilter_inout_free(&outputs);

    if (fgp->disable_conversions)
        avfilter_graph_set_auto_convert(fg->graph, AVFILTER_AUTO_CONVERT_NONE);
    if ((ret = avfilter_graph_config(fg->graph, NULL)) < 0)
        goto fail;

    fgp->is_meta = graph_is_meta(fg->graph);

    /* limit the lists of allowed formats to the ones selected, to
     * make sure they stay the same if the filtergraph is reconfigured later */
    for (i = 0; i < fg->nb_outputs; i++) {
        OutputFilter *ofilter = fg->outputs[i];
        OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);
        AVFilterContext *sink = ofp->filter;

        ofp->format = av_buffersink_get_format(sink);

        ofp->width  = av_buffersink_get_w(sink);
        ofp->height = av_buffersink_get_h(sink);

        ofp->time_base           = av_buffersink_get_time_base(sink);
        ofp->sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(sink);

        ofp->sample_rate    = av_buffersink_get_sample_rate(sink);
        av_channel_layout_uninit(&ofp->ch_layout);
        ret = av_buffersink_get_ch_layout(sink, &ofp->ch_layout);
        if (ret < 0)
            goto fail;
    }

    for (i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        AVFrame *tmp;
        while (av_fifo_read(ifp->frame_queue, &tmp, 1) >= 0) {
            if (ifp->type_src == AVMEDIA_TYPE_SUBTITLE) {
                sub2video_update(ifp, INT64_MIN, (const AVSubtitle*)tmp->buf[0]->data);
            } else {
                ret = av_buffersrc_add_frame(ifp->filter, tmp);
            }
            av_frame_free(&tmp);
            if (ret < 0)
                goto fail;
        }
    }

    /* send the EOFs for the finished inputs */
    for (i = 0; i < fg->nb_inputs; i++) {
        InputFilterPriv *ifp = ifp_from_ifilter(fg->inputs[i]);
        if (ifp->eof) {
            ret = av_buffersrc_add_frame(ifp->filter, NULL);
            if (ret < 0)
                goto fail;
        }
    }

    return 0;

fail:
    cleanup_filtergraph(fg);
    return ret;
}

int ifilter_parameters_from_dec(InputFilter *ifilter, const AVCodecContext *dec)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

    if (dec->codec_type == AVMEDIA_TYPE_VIDEO) {
        ifp->fallback.format                 = dec->pix_fmt;
        ifp->fallback.width                  = dec->width;
        ifp->fallback.height                 = dec->height;
        ifp->fallback.sample_aspect_ratio    = dec->sample_aspect_ratio;
    } else if (dec->codec_type == AVMEDIA_TYPE_AUDIO) {
        int ret;

        ifp->fallback.format                 = dec->sample_fmt;
        ifp->fallback.sample_rate            = dec->sample_rate;

        ret = av_channel_layout_copy(&ifp->fallback.ch_layout, &dec->ch_layout);
        if (ret < 0)
            return ret;
    } else {
        // for subtitles (i.e. sub2video) we set the actual parameters,
        // rather than just fallback
        ifp->width  = ifp->ist->sub2video.w;
        ifp->height = ifp->ist->sub2video.h;

        /* rectangles are AV_PIX_FMT_PAL8, but we have no guarantee that the
           palettes for all rectangles are identical or compatible */
        ifp->format = AV_PIX_FMT_RGB32;

        av_log(NULL, AV_LOG_VERBOSE, "sub2video: using %dx%d canvas\n", ifp->width, ifp->height);
    }

    return 0;
}

static int ifilter_parameters_from_frame(InputFilter *ifilter, const AVFrame *frame)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    AVFrameSideData *sd;
    int ret;

    ret = av_buffer_replace(&ifp->hw_frames_ctx, frame->hw_frames_ctx);
    if (ret < 0)
        return ret;

    ifp->format              = frame->format;

    ifp->width               = frame->width;
    ifp->height              = frame->height;
    ifp->sample_aspect_ratio = frame->sample_aspect_ratio;

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

void fg_send_command(FilterGraph *fg, double time, const char *target,
                     const char *command, const char *arg, int all_filters)
{
    int ret;

    if (!fg->graph)
        return;

    if (time < 0) {
        char response[4096];
        ret = avfilter_graph_send_command(fg->graph, target, command, arg,
                                          response, sizeof(response),
                                          all_filters ? 0 : AVFILTER_CMD_FLAG_ONE);
        fprintf(stderr, "Command reply for stream %d: ret:%d res:\n%s",
                fg->index, ret, response);
    } else if (!all_filters) {
        fprintf(stderr, "Queuing commands only on filters supporting the specific command is unsupported\n");
    } else {
        ret = avfilter_graph_queue_command(fg->graph, target, command, arg, 0, time);
        if (ret < 0)
            fprintf(stderr, "Queuing command failed with error %s\n", av_err2str(ret));
    }
}

static int fg_output_step(OutputFilterPriv *ofp, int flush)
{
    FilterGraphPriv    *fgp = fgp_from_fg(ofp->ofilter.graph);
    OutputStream       *ost = ofp->ofilter.ost;
    AVFrame          *frame = fgp->frame;
    AVFilterContext *filter = ofp->filter;
    FrameData *fd;
    int ret;

    ret = av_buffersink_get_frame_flags(filter, frame,
                                        AV_BUFFERSINK_FLAG_NO_REQUEST);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            av_log(fgp, AV_LOG_WARNING,
                   "Error in av_buffersink_get_frame_flags(): %s\n", av_err2str(ret));
        } else if (flush && ret == AVERROR_EOF && ofp->got_frame &&
                   av_buffersink_get_type(filter) == AVMEDIA_TYPE_VIDEO) {
            ret = enc_frame(ost, NULL);
            if (ret < 0)
                return ret;
        }

        return 1;
    }
    if (ost->finished) {
        av_frame_unref(frame);
        return 0;
    }

    if (frame->pts != AV_NOPTS_VALUE) {
        AVRational tb = av_buffersink_get_time_base(filter);
        ost->filter->last_pts = av_rescale_q(frame->pts, tb, AV_TIME_BASE_Q);
        frame->time_base = tb;

        if (debug_ts)
            av_log(fgp, AV_LOG_INFO, "filter_raw -> pts:%s pts_time:%s time_base:%d/%d\n",
                   av_ts2str(frame->pts), av_ts2timestr(frame->pts, &tb), tb.num, tb.den);
    }

    fd = frame_data(frame);
    if (!fd) {
        av_frame_unref(frame);
        return AVERROR(ENOMEM);
    }

    // only use bits_per_raw_sample passed through from the decoder
    // if the filtergraph did not touch the frame data
    if (!fgp->is_meta)
        fd->bits_per_raw_sample = 0;

    if (ost->type == AVMEDIA_TYPE_VIDEO) {
        AVRational fr = av_buffersink_get_frame_rate(filter);
        if (fr.num > 0 && fr.den > 0) {
            fd->frame_rate_filter = fr;

            if (!frame->duration)
                frame->duration = av_rescale_q(1, av_inv_q(fr), frame->time_base);
        }
    }

    ret = enc_frame(ost, frame);
    av_frame_unref(frame);
    if (ret < 0)
        return ret;

    ofp->got_frame = 1;

    return 0;
}

int reap_filters(FilterGraph *fg, int flush)
{
    if (!fg->graph)
        return 0;

    /* Reap all buffers present in the buffer sinks */
    for (int i = 0; i < fg->nb_outputs; i++) {
        OutputFilterPriv *ofp = ofp_from_ofilter(fg->outputs[i]);
        int ret = 0;

        while (!ret) {
            ret = fg_output_step(ofp, flush);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

void ifilter_sub2video_heartbeat(InputFilter *ifilter, int64_t pts, AVRational tb)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int64_t pts2;

    if (!ifilter->graph->graph)
        return;

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

    if (av_buffersrc_get_nb_failed_requests(ifp->filter))
        sub2video_push_ref(ifp, pts2);
}

int ifilter_sub2video(InputFilter *ifilter, const AVFrame *frame)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int ret;

    if (ifilter->graph->graph) {
        if (!frame) {
            if (ifp->sub2video.end_pts < INT64_MAX)
                sub2video_update(ifp, INT64_MAX, NULL);

            return av_buffersrc_add_frame(ifp->filter, NULL);
        }

        ifp->width  = frame->width  ? frame->width  : ifp->width;
        ifp->height = frame->height ? frame->height : ifp->height;

        sub2video_update(ifp, INT64_MIN, (const AVSubtitle*)frame->buf[0]->data);
    } else if (frame) {
        AVFrame *tmp = av_frame_clone(frame);

        if (!tmp)
            return AVERROR(ENOMEM);

        ret = av_fifo_write(ifp->frame_queue, &tmp, 1);
        if (ret < 0) {
            av_frame_free(&tmp);
            return ret;
        }
    }

    return 0;
}

int ifilter_send_eof(InputFilter *ifilter, int64_t pts, AVRational tb)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    int ret;

    ifp->eof = 1;

    if (ifp->filter) {
        pts = av_rescale_q_rnd(pts, tb, ifp->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

        ret = av_buffersrc_close(ifp->filter, pts, AV_BUFFERSRC_FLAG_PUSH);
        if (ret < 0)
            return ret;
    } else {
        if (ifp->format < 0) {
            // the filtergraph was never configured, use the fallback parameters
            ifp->format                 = ifp->fallback.format;
            ifp->sample_rate            = ifp->fallback.sample_rate;
            ifp->width                  = ifp->fallback.width;
            ifp->height                 = ifp->fallback.height;
            ifp->sample_aspect_ratio    = ifp->fallback.sample_aspect_ratio;

            ret = av_channel_layout_copy(&ifp->ch_layout,
                                         &ifp->fallback.ch_layout);
            if (ret < 0)
                return ret;

            if (ifilter_has_all_input_formats(ifilter->graph)) {
                ret = configure_filtergraph(ifilter->graph);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error initializing filters!\n");
                    return ret;
                }
            }
        }

        if (ifp->format < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot determine format of input stream %d:%d after EOF\n",
                   ifp->ist->file_index, ifp->ist->index);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame, int keep_reference)
{
    InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
    FilterGraph *fg = ifilter->graph;
    AVFrameSideData *sd;
    int need_reinit, ret;

    /* determine if the parameters for this input changed */
    need_reinit = ifp->format != frame->format;

    switch (ifp->type) {
    case AVMEDIA_TYPE_AUDIO:
        need_reinit |= ifp->sample_rate    != frame->sample_rate ||
                       av_channel_layout_compare(&ifp->ch_layout, &frame->ch_layout);
        break;
    case AVMEDIA_TYPE_VIDEO:
        need_reinit |= ifp->width  != frame->width ||
                       ifp->height != frame->height;
        break;
    }

    if (!ifp->ist->reinit_filters && fg->graph)
        need_reinit = 0;

    if (!!ifp->hw_frames_ctx != !!frame->hw_frames_ctx ||
        (ifp->hw_frames_ctx && ifp->hw_frames_ctx->data != frame->hw_frames_ctx->data))
        need_reinit = 1;

    if (sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        if (!ifp->displaymatrix_present ||
            memcmp(sd->data, ifp->displaymatrix, sizeof(ifp->displaymatrix)))
            need_reinit = 1;
    } else if (ifp->displaymatrix_present)
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

            ret = av_fifo_write(ifp->frame_queue, &tmp, 1);
            if (ret < 0)
                av_frame_free(&tmp);

            return ret;
        }

        ret = reap_filters(fg, 0);
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(fg, AV_LOG_ERROR, "Error while filtering: %s\n", av_err2str(ret));
            return ret;
        }

        ret = configure_filtergraph(fg);
        if (ret < 0) {
            av_log(fg, AV_LOG_ERROR, "Error reinitializing filters!\n");
            return ret;
        }
    }

    if (keep_reference) {
        ret = av_frame_ref(ifp->frame, frame);
        if (ret < 0)
            return ret;
    } else
        av_frame_move_ref(ifp->frame, frame);
    frame = ifp->frame;

    frame->pts       = av_rescale_q(frame->pts,      frame->time_base, ifp->time_base);
    frame->duration  = av_rescale_q(frame->duration, frame->time_base, ifp->time_base);
    frame->time_base = ifp->time_base;
#if LIBAVUTIL_VERSION_MAJOR < 59
    AV_NOWARN_DEPRECATED(
    frame->pkt_duration = frame->duration;
    )
#endif

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

int fg_transcode_step(FilterGraph *graph, InputStream **best_ist)
{
    FilterGraphPriv *fgp = fgp_from_fg(graph);
    int i, ret;
    int nb_requests, nb_requests_max = 0;
    InputStream *ist;

    if (!graph->graph) {
        for (int i = 0; i < graph->nb_inputs; i++) {
            InputFilter *ifilter = graph->inputs[i];
            InputFilterPriv *ifp = ifp_from_ifilter(ifilter);
            if (ifp->format < 0 && !ifp->eof) {
                *best_ist = ifp->ist;
                return 0;
            }
        }

        // graph not configured, but all inputs are either initialized or EOF
        for (int i = 0; i < graph->nb_outputs; i++)
            graph->outputs[i]->ost->inputs_done = 1;

        return 0;
    }

    *best_ist = NULL;
    ret = avfilter_graph_request_oldest(graph->graph);
    if (ret >= 0)
        return reap_filters(graph, 0);

    if (ret == AVERROR_EOF) {
        reap_filters(graph, 1);
        for (int i = 0; i < graph->nb_outputs; i++) {
            OutputFilter *ofilter = graph->outputs[i];
            OutputFilterPriv *ofp = ofp_from_ofilter(ofilter);

            // we are finished and no frames were ever seen at this output,
            // at least initialize the encoder with a dummy frame
            if (!ofp->got_frame) {
                AVFrame *frame = fgp->frame;

                frame->time_base   = ofp->time_base;
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

                av_assert0(!frame->buf[0]);

                av_log(ofilter->ost, AV_LOG_WARNING,
                       "No filtered frames for output stream, trying to "
                       "initialize anyway.\n");

                enc_open(ofilter->ost, frame);
                av_frame_unref(frame);
            }

            close_output_stream(ofilter->ost);
        }
        return 0;
    }
    if (ret != AVERROR(EAGAIN))
        return ret;

    for (i = 0; i < graph->nb_inputs; i++) {
        InputFilter *ifilter = graph->inputs[i];
        InputFilterPriv *ifp = ifp_from_ifilter(ifilter);

        ist = ifp->ist;
        if (input_files[ist->file_index]->eagain || ifp->eof)
            continue;
        nb_requests = av_buffersrc_get_nb_failed_requests(ifp->filter);
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
