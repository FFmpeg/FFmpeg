/*
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "libavutil/avassert.h"
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

#define PERM_RWP AV_PERM_WRITE | AV_PERM_PRESERVE | AV_PERM_REUSE

#define CHECK(j)\
    {   int score = FFABS(cur[mrefs + off_left + (j)] - cur[prefs + off_left - (j)])\
                  + FFABS(cur[mrefs  +(j)] - cur[prefs  -(j)])\
                  + FFABS(cur[mrefs + off_right + (j)] - cur[prefs + off_right - (j)]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[mrefs  +(j)] + cur[prefs  -(j)])>>1;\

#define FILTER(start, end) \
    for (x = start;  x < end; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0])>>1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) )>>1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
        int off_right = (x < w - 1) ? 1 : -1;\
        int off_left  = x ? -1 : 1;\
        int spatial_score = FFABS(cur[mrefs + off_left]  - cur[prefs + off_left]) + FFABS(c-e) \
                          + FFABS(cur[mrefs + off_right] - cur[prefs + off_right]) - 1; \
 \
        if (x > 2 && x < w - 3) {\
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

    FILTER(0, w)
}

static void filter_edges(void *dst1, void *prev1, void *cur1, void *next1,
                         int w, int prefs, int mrefs, int parity, int mode,
                         int l_edge)
{
    uint8_t *dst  = dst1;
    uint8_t *prev = prev1;
    uint8_t *cur  = cur1;
    uint8_t *next = next1;
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    FILTER(0, l_edge)

    dst  = (uint8_t*)dst1  + w - 3;
    prev = (uint8_t*)prev1 + w - 3;
    cur  = (uint8_t*)cur1  + w - 3;
    next = (uint8_t*)next1 + w - 3;
    prev2 = (uint8_t*)(parity ? prev : cur);
    next2 = (uint8_t*)(parity ? cur  : next);

    FILTER(w - 3, w)
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

    FILTER(0, w)
}

static void filter_edges_16bit(void *dst1, void *prev1, void *cur1, void *next1,
                               int w, int prefs, int mrefs, int parity, int mode,
                               int l_edge)
{
    uint16_t *dst  = dst1;
    uint16_t *prev = prev1;
    uint16_t *cur  = cur1;
    uint16_t *next = next1;
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;

    FILTER(0, l_edge)

    dst   = (uint16_t*)dst1  + w - 3;
    prev  = (uint16_t*)prev1 + w - 3;
    cur   = (uint16_t*)cur1  + w - 3;
    next  = (uint16_t*)next1 + w - 3;
    prev2 = (uint16_t*)(parity ? prev : cur);
    next2 = (uint16_t*)(parity ? cur  : next);

    FILTER(w - 3, w)
}

static void filter(AVFilterContext *ctx, AVFilterBufferRef *dstpic,
                   int parity, int tff)
{
    YADIFContext *yadif = ctx->priv;
    int y, i;

    for (i = 0; i < yadif->csp->nb_components; i++) {
        int w = dstpic->video->w;
        int h = dstpic->video->h;
        int refs = yadif->cur->linesize[i];
        int df = (yadif->csp->comp[i].depth_minus1 + 8) / 8;
        int l_edge, l_edge_pix;

        if (i == 1 || i == 2) {
        /* Why is this not part of the per-plane description thing? */
            w >>= yadif->csp->log2_chroma_w;
            h >>= yadif->csp->log2_chroma_h;
        }

        /* filtering reads 3 pixels to the left/right; to avoid invalid reads,
         * we need to call the c variant which avoids this for border pixels
         */
        l_edge     = yadif->req_align;
        l_edge_pix = l_edge / df;

        for (y = 0; y < h; y++) {
            if ((y ^ parity) & 1) {
                uint8_t *prev = &yadif->prev->data[i][y * refs];
                uint8_t *cur  = &yadif->cur ->data[i][y * refs];
                uint8_t *next = &yadif->next->data[i][y * refs];
                uint8_t *dst  = &dstpic->data[i][y * dstpic->linesize[i]];
                int     mode  = y == 1 || y + 2 == h ? 2 : yadif->mode;
                if (yadif->req_align) {
                    yadif->filter_line(dst + l_edge, prev + l_edge, cur + l_edge,
                                       next + l_edge, w - l_edge_pix - 3,
                                       y + 1 < h ? refs : -refs,
                                       y ? -refs : refs,
                                       parity ^ tff, mode);
                    yadif->filter_edges(dst, prev, cur, next, w,
                                         y + 1 < h ? refs : -refs,
                                         y ? -refs : refs,
                                         parity ^ tff, mode, l_edge_pix);
                } else {
                    yadif->filter_line(dst, prev, cur, next + l_edge, w,
                                       y + 1 < h ? refs : -refs,
                                       y ? -refs : refs,
                                       parity ^ tff, mode);
                }
            } else {
                memcpy(&dstpic->data[i][y * dstpic->linesize[i]],
                       &yadif->cur->data[i][y * refs], w * df);
            }
        }
    }

    emms_c();
}

static int return_frame(AVFilterContext *ctx, int is_second)
{
    YADIFContext *yadif = ctx->priv;
    AVFilterLink *link  = ctx->outputs[0];
    int tff, ret;

    if (yadif->parity == -1) {
        tff = yadif->cur->video->interlaced ?
              yadif->cur->video->top_field_first : 1;
    } else {
        tff = yadif->parity ^ 1;
    }

    if (is_second) {
        yadif->out = ff_get_video_buffer(link, PERM_RWP, link->w, link->h);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(yadif->out, yadif->cur);
        yadif->out->video->interlaced = 0;
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

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    av_assert0(picref);

    if (yadif->frame_pending)
        return_frame(ctx, 1);

    if (yadif->prev)
        avfilter_unref_buffer(yadif->prev);
    yadif->prev = yadif->cur;
    yadif->cur  = yadif->next;
    yadif->next = picref;

    if (!yadif->cur)
        return 0;

    if (yadif->deint && !yadif->cur->video->interlaced) {
        yadif->out  = avfilter_ref_buffer(yadif->cur, ~AV_PERM_WRITE);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        avfilter_unref_bufferp(&yadif->prev);
        if (yadif->out->pts != AV_NOPTS_VALUE)
            yadif->out->pts *= 2;
        return ff_filter_frame(ctx->outputs[0], yadif->out);
    }

    if (!yadif->prev &&
        !(yadif->prev = avfilter_ref_buffer(yadif->cur, ~AV_PERM_WRITE)))
        return AVERROR(ENOMEM);

    yadif->out = ff_get_video_buffer(ctx->outputs[0], PERM_RWP,
                                     link->w, link->h);
    if (!yadif->out)
        return AVERROR(ENOMEM);

    avfilter_copy_buffer_ref_props(yadif->out, yadif->cur);
    yadif->out->video->interlaced = 0;

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

        if (ret == AVERROR_EOF && yadif->cur) {
            AVFilterBufferRef *next = avfilter_ref_buffer(yadif->next, ~AV_PERM_WRITE);

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

#define OFFSET(x) offsetof(YADIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, unit }

static const AVOption yadif_options[] = {
    { "mode",   "specify the interlacing mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=YADIF_MODE_SEND_FRAME}, 0, 3, FLAGS, "mode"},
    CONST("send_frame",           "send one frame for each frame",                                     YADIF_MODE_SEND_FRAME,           "mode"),
    CONST("send_field",           "send one frame for each field",                                     YADIF_MODE_SEND_FIELD,           "mode"),
    CONST("send_frame_nospatial", "send one frame for each frame, but skip spatial interlacing check", YADIF_MODE_SEND_FRAME_NOSPATIAL, "mode"),
    CONST("send_field_nospatial", "send one frame for each field, but skip spatial interlacing check", YADIF_MODE_SEND_FIELD_NOSPATIAL, "mode"),

    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=YADIF_PARITY_AUTO}, -1, 1, FLAGS, "parity" },
    CONST("tff",  "assume top field first",    YADIF_PARITY_TFF,  "parity"),
    CONST("bff",  "assume bottom field first", YADIF_PARITY_BFF,  "parity"),
    CONST("auto", "auto detect parity",        YADIF_PARITY_AUTO, "parity"),

    { "deint", "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=YADIF_DEINT_ALL}, 0, 1, FLAGS, "deint" },
    CONST("all",        "deinterlace all frames",                       YADIF_DEINT_ALL,         "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", YADIF_DEINT_INTERLACED,  "deint"),

    {NULL},
};

AVFILTER_DEFINE_CLASS(yadif);

static av_cold void uninit(AVFilterContext *ctx)
{
    YADIFContext *yadif = ctx->priv;

    avfilter_unref_bufferp(&yadif->prev);
    avfilter_unref_bufferp(&yadif->cur );
    avfilter_unref_bufferp(&yadif->next);
    av_opt_free(yadif);
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
        AV_NE( AV_PIX_FMT_YUV420P9BE,  AV_PIX_FMT_YUV420P9LE ),
        AV_NE( AV_PIX_FMT_YUV422P9BE,  AV_PIX_FMT_YUV422P9LE ),
        AV_NE( AV_PIX_FMT_YUV444P9BE,  AV_PIX_FMT_YUV444P9LE ),
        AV_NE( AV_PIX_FMT_YUV420P10BE, AV_PIX_FMT_YUV420P10LE ),
        AV_NE( AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10LE ),
        AV_NE( AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUV444P10LE ),
        AV_NE( AV_PIX_FMT_YUV420P12BE, AV_PIX_FMT_YUV420P12LE ),
        AV_NE( AV_PIX_FMT_YUV422P12BE, AV_PIX_FMT_YUV422P12LE ),
        AV_NE( AV_PIX_FMT_YUV444P12BE, AV_PIX_FMT_YUV444P12LE ),
        AV_NE( AV_PIX_FMT_YUV420P14BE, AV_PIX_FMT_YUV420P14LE ),
        AV_NE( AV_PIX_FMT_YUV422P14BE, AV_PIX_FMT_YUV422P14LE ),
        AV_NE( AV_PIX_FMT_YUV444P14BE, AV_PIX_FMT_YUV444P14LE ),
        AV_NE( AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_YUV420P16LE ),
        AV_NE( AV_PIX_FMT_YUV422P16BE, AV_PIX_FMT_YUV422P16LE ),
        AV_NE( AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16LE ),
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    YADIFContext *yadif = ctx->priv;
    static const char *shorthand[] = { "mode", "parity", "deint", NULL };
    int ret;

    yadif->class = &yadif_class;
    av_opt_set_defaults(yadif);

    if ((ret = av_opt_set_from_string(yadif, args, shorthand, "=", ":")) < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "mode:%d parity:%d deint:%d\n",
           yadif->mode, yadif->parity, yadif->deint);

    return 0;
}

static int config_props(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    YADIFContext *s = link->src->priv;

    link->time_base.num = link->src->inputs[0]->time_base.num;
    link->time_base.den = link->src->inputs[0]->time_base.den * 2;
    link->w             = link->src->inputs[0]->w;
    link->h             = link->src->inputs[0]->h;

    if(s->mode&1)
        link->frame_rate = av_mul_q(link->src->inputs[0]->frame_rate, (AVRational){2,1});

    if (link->w < 3 || link->h < 3) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 columns or lines is not supported\n");
        return AVERROR(EINVAL);
    }

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

static const AVFilterPad avfilter_vf_yadif_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
        .min_perms        = AV_PERM_PRESERVE,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_yadif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter avfilter_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image."),

    .priv_size     = sizeof(YADIFContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_yadif_inputs,
    .outputs   = avfilter_vf_yadif_outputs,

    .priv_class = &yadif_class,
};
