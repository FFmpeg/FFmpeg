/*
 * Copyright (C) 2006-2010 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
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

#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "yadif.h"

#undef NDEBUG
#include <assert.h>

typedef struct ThreadData {
    AVFrame *frame;
    int plane;
    int w, h;
    int parity;
    int tff;
} ThreadData;

#define CHECK(j)\
    {   int score = FFABS(cur[mrefs - 1 + (j)] - cur[prefs - 1 - (j)])\
                  + FFABS(cur[mrefs  +(j)] - cur[prefs  -(j)])\
                  + FFABS(cur[mrefs + 1 + (j)] - cur[prefs + 1 - (j)]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[mrefs  +(j)] + cur[prefs  -(j)])>>1;\

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge) \
    for (x = start;  x < end; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0])>>1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) )>>1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
 \
        if (is_not_edge) {\
            int spatial_score = FFABS(cur[mrefs - 1] - cur[prefs - 1]) + FFABS(c-e) \
                              + FFABS(cur[mrefs + 1] - cur[prefs + 1]) - 1; \
            CHECK(-1) CHECK(-2) }} }} \
            CHECK( 1) CHECK( 2) }} }} \
        }\
 \
        if (mode < 2) { \
            int b = (prev2[2 * mrefs] + next2[2 * mrefs])>>1; \
            int f = (prev2[2 * prefs] + next2[2 * prefs])>>1; \
            int max = FFMAX3(d - e, d - c, FFMIN(b - c, f - e)); \
            int min = FFMIN3(d - e, d - c, FFMAX(b - c, f - e)); \
 \
            diff = FFMAX3(diff, min, -max); \
        } \
 \
        if (spatial_pred > d + diff) \
           spatial_pred = d + diff; \
        else if (spatial_pred < d - diff) \
           spatial_pred = d - diff; \
 \
        dst[0] = spatial_pred; \
 \
        dst++; \
        cur++; \
        prev++; \
        next++; \
        prev2++; \
        next2++; \
    }

static void filter_line_c(void *dst1,
                          void *prev1, void *cur1, void *next1,
                          int w, int prefs, int mrefs, int parity, int mode)
{
    uint8_t *dst  = dst1;
    uint8_t *prev = prev1;
    uint8_t *cur  = cur1;
    uint8_t *next = next1;
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    /* The function is called with the pointers already pointing to data[3] and
     * with 6 subtracted from the width.  This allows the FILTER macro to be
     * called so that it processes all the pixels normally.  A constant value of
     * true for is_not_edge lets the compiler ignore the if statement. */
    FILTER(0, w, 1)
}

static void filter_edges(void *dst1, void *prev1, void *cur1, void *next1,
                         int w, int prefs, int mrefs, int parity, int mode)
{
    uint8_t *dst  = dst1;
    uint8_t *prev = prev1;
    uint8_t *cur  = cur1;
    uint8_t *next = next1;
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    /* Only edge pixels need to be processed here.  A constant value of false
     * for is_not_edge should let the compiler ignore the whole branch. */
    FILTER(0, 3, 0)

    dst  = (uint8_t*)dst1  + w - 3;
    prev = (uint8_t*)prev1 + w - 3;
    cur  = (uint8_t*)cur1  + w - 3;
    next = (uint8_t*)next1 + w - 3;
    prev2 = (uint8_t*)(parity ? prev : cur);
    next2 = (uint8_t*)(parity ? cur  : next);

    FILTER(w - 3, w, 0)
}


static void filter_line_c_16bit(void *dst1,
                                void *prev1, void *cur1, void *next1,
                                int w, int prefs, int mrefs, int parity,
                                int mode)
{
    uint16_t *dst  = dst1;
    uint16_t *prev = prev1;
    uint16_t *cur  = cur1;
    uint16_t *next = next1;
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    mrefs /= 2;
    prefs /= 2;

    FILTER(0, w, 1)
}

static void filter_edges_16bit(void *dst1, void *prev1, void *cur1, void *next1,
                               int w, int prefs, int mrefs, int parity, int mode)
{
    uint16_t *dst  = dst1;
    uint16_t *prev = prev1;
    uint16_t *cur  = cur1;
    uint16_t *next = next1;
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    mrefs /= 2;
    prefs /= 2;

    FILTER(0, 3, 0)

    dst   = (uint16_t*)dst1  + w - 3;
    prev  = (uint16_t*)prev1 + w - 3;
    cur   = (uint16_t*)cur1  + w - 3;
    next  = (uint16_t*)next1 + w - 3;
    prev2 = (uint16_t*)(parity ? prev : cur);
    next2 = (uint16_t*)(parity ? cur  : next);

    FILTER(w - 3, w, 0)
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    YADIFContext *s = ctx->priv;
    ThreadData *td  = arg;
    int refs = s->cur->linesize[td->plane];
    int df = (s->csp->comp[td->plane].depth_minus1 + 8) / 8;
    int pix_3 = 3 * df;
    int slice_h = td->h / nb_jobs;
    int slice_start = jobnr * slice_h;
    int slice_end   = (jobnr == nb_jobs - 1) ? td->h : (jobnr + 1) * slice_h;
    int y;

    /* filtering reads 3 pixels to the left/right; to avoid invalid reads,
     * we need to call the c variant which avoids this for border pixels
     */
    for (y = slice_start; y < slice_end; y++) {
        if ((y ^ td->parity) & 1) {
            uint8_t *prev = &s->prev->data[td->plane][y * refs];
            uint8_t *cur  = &s->cur ->data[td->plane][y * refs];
            uint8_t *next = &s->next->data[td->plane][y * refs];
            uint8_t *dst  = &td->frame->data[td->plane][y * td->frame->linesize[td->plane]];
            int     mode  = y == 1 || y + 2 == td->h ? 2 : s->mode;
            s->filter_line(dst + pix_3, prev + pix_3, cur + pix_3,
                           next + pix_3, td->w - 6,
                           y + 1 < td->h ? refs : -refs,
                           y ? -refs : refs,
                           td->parity ^ td->tff, mode);
            s->filter_edges(dst, prev, cur, next, td->w,
                            y + 1 < td->h ? refs : -refs,
                            y ? -refs : refs,
                            td->parity ^ td->tff, mode);
        } else {
            memcpy(&td->frame->data[td->plane][y * td->frame->linesize[td->plane]],
                   &s->cur->data[td->plane][y * refs], td->w * df);
        }
    }
    return 0;
}

static void filter(AVFilterContext *ctx, AVFrame *dstpic,
                   int parity, int tff)
{
    YADIFContext *yadif = ctx->priv;
    ThreadData td = { .frame = dstpic, .parity = parity, .tff = tff };
    int i;

    for (i = 0; i < yadif->csp->nb_components; i++) {
        int w = dstpic->width;
        int h = dstpic->height;

        if (i == 1 || i == 2) {
            w >>= yadif->csp->log2_chroma_w;
            h >>= yadif->csp->log2_chroma_h;
        }


        td.w       = w;
        td.h       = h;
        td.plane   = i;

        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(h, ctx->graph->nb_threads));
    }

    emms_c();
}

static AVFrame *get_video_buffer(AVFilterLink *link, int w, int h)
{
    AVFrame *frame;
    int width  = FFALIGN(w, 32);
    int height = FFALIGN(h + 2, 32);
    int i;

    frame = ff_default_get_video_buffer(link, width, height);

    frame->width  = w;
    frame->height = h;

    for (i = 0; i < 3; i++)
        frame->data[i] += frame->linesize[i];

    return frame;
}

static int return_frame(AVFilterContext *ctx, int is_second)
{
    YADIFContext *yadif = ctx->priv;
    AVFilterLink *link  = ctx->outputs[0];
    int tff, ret;

    if (yadif->parity == -1) {
        tff = yadif->cur->interlaced_frame ?
              yadif->cur->top_field_first : 1;
    } else {
        tff = yadif->parity ^ 1;
    }

    if (is_second) {
        yadif->out = ff_get_video_buffer(link, link->w, link->h);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        av_frame_copy_props(yadif->out, yadif->cur);
        yadif->out->interlaced_frame = 0;
    }

    filter(ctx, yadif->out, tff ^ !is_second, tff);

    if (is_second) {
        int64_t cur_pts  = yadif->cur->pts;
        int64_t next_pts = yadif->next->pts;

        if (next_pts != AV_NOPTS_VALUE && cur_pts != AV_NOPTS_VALUE) {
            yadif->out->pts = cur_pts + next_pts;
        } else {
            yadif->out->pts = AV_NOPTS_VALUE;
        }
    }
    ret = ff_filter_frame(ctx->outputs[0], yadif->out);

    yadif->frame_pending = (yadif->mode&1) && !is_second;
    return ret;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    if (yadif->frame_pending)
        return_frame(ctx, 1);

    if (yadif->prev)
        av_frame_free(&yadif->prev);
    yadif->prev = yadif->cur;
    yadif->cur  = yadif->next;
    yadif->next = frame;

    if (!yadif->cur)
        return 0;

    if (yadif->auto_enable && !yadif->cur->interlaced_frame) {
        yadif->out  = av_frame_clone(yadif->cur);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        av_frame_free(&yadif->prev);
        if (yadif->out->pts != AV_NOPTS_VALUE)
            yadif->out->pts *= 2;
        return ff_filter_frame(ctx->outputs[0], yadif->out);
    }

    if (!yadif->prev &&
        !(yadif->prev = av_frame_clone(yadif->cur)))
        return AVERROR(ENOMEM);

    yadif->out = ff_get_video_buffer(ctx->outputs[0], link->w, link->h);
    if (!yadif->out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(yadif->out, yadif->cur);
    yadif->out->interlaced_frame = 0;

    if (yadif->out->pts != AV_NOPTS_VALUE)
        yadif->out->pts *= 2;

    return return_frame(ctx, 0);
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    YADIFContext *yadif = ctx->priv;

    if (yadif->frame_pending) {
        return_frame(ctx, 1);
        return 0;
    }

    do {
        int ret;

        if (yadif->eof)
            return AVERROR_EOF;

        ret  = ff_request_frame(link->src->inputs[0]);

        if (ret == AVERROR_EOF && yadif->next) {
            AVFrame *next = av_frame_clone(yadif->next);

            if (!next)
                return AVERROR(ENOMEM);

            next->pts = yadif->next->pts * 2 - yadif->cur->pts;

            filter_frame(link->src->inputs[0], next);
            yadif->eof = 1;
        } else if (ret < 0) {
            return ret;
        }
    } while (!yadif->cur);

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    YADIFContext *yadif = link->src->priv;
    int ret, val;

    if (yadif->frame_pending)
        return 1;

    val = ff_poll_frame(link->src->inputs[0]);
    if (val <= 0)
        return val;

    //FIXME change API to not requre this red tape
    if (val == 1 && !yadif->next) {
        if ((ret = ff_request_frame(link->src->inputs[0])) < 0)
            return ret;
        val = ff_poll_frame(link->src->inputs[0]);
        if (val <= 0)
            return val;
    }
    assert(yadif->next || !val);

    if (yadif->auto_enable && yadif->next && !yadif->next->interlaced_frame)
        return val;

    return val * ((yadif->mode&1)+1);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    YADIFContext *yadif = ctx->priv;

    if (yadif->prev) av_frame_free(&yadif->prev);
    if (yadif->cur ) av_frame_free(&yadif->cur );
    if (yadif->next) av_frame_free(&yadif->next);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P,
        AV_NE( AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_GRAY16LE ),
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ440P,
        AV_NE( AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV420P10LE ),
        AV_NE( AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10LE ),
        AV_NE( AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUV444P10LE ),
        AV_NE( AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUV420P16LE ),
        AV_NE( AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUV422P16LE ),
        AV_NE( AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16LE ),
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_props(AVFilterLink *link)
{
    YADIFContext *s = link->src->priv;

    link->time_base.num = link->src->inputs[0]->time_base.num;
    link->time_base.den = link->src->inputs[0]->time_base.den * 2;
    link->w             = link->src->inputs[0]->w;
    link->h             = link->src->inputs[0]->h;

    s->csp = av_pix_fmt_desc_get(link->format);
    if (s->csp->comp[0].depth_minus1 / 8 == 1) {
        s->filter_line  = filter_line_c_16bit;
        s->filter_edges = filter_edges_16bit;
    } else {
        s->filter_line  = filter_line_c;
        s->filter_edges = filter_edges;

        if (ARCH_X86)
            ff_yadif_init_x86(s);
    }

    return 0;
}

#define OFFSET(x) offsetof(YADIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "mode",   NULL, OFFSET(mode),        AV_OPT_TYPE_INT, { .i64 = 0  },  0, 3, FLAGS },
    { "parity", NULL, OFFSET(parity),      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, FLAGS, "parity" },
        { "auto", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, .unit = "parity" },
        { "tff",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0  }, .unit = "parity" },
        { "bff",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1  }, .unit = "parity" },
    { "auto",   NULL, OFFSET(auto_enable), AV_OPT_TYPE_INT, { .i64 = 0  },  0, 1, FLAGS },
    { NULL },
};

static const AVClass yadif_class = {
    .class_name = "yadif",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad avfilter_vf_yadif_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_yadif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .poll_frame    = poll_frame,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image"),

    .priv_size     = sizeof(YADIFContext),
    .priv_class    = &yadif_class,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_yadif_inputs,

    .outputs   = avfilter_vf_yadif_outputs,

    .flags     = AVFILTER_FLAG_SLICE_THREADS,
};
