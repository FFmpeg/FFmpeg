/*
 * Copyright (C) 2006-2010 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "libavutil/cpu.h"
#include "libavutil/common.h"
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
    {   int score = FFABS(cur[mrefs-1+(j)] - cur[prefs-1-(j)])\
                  + FFABS(cur[mrefs  +(j)] - cur[prefs  -(j)])\
                  + FFABS(cur[mrefs+1+(j)] - cur[prefs+1-(j)]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[mrefs  +(j)] + cur[prefs  -(j)])>>1;\

#define FILTER \
    for (x = 0;  x < w; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0])>>1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) )>>1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) )>>1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e) >> 1; \
        int spatial_score = FFABS(cur[mrefs - 1] - cur[prefs - 1]) + FFABS(c-e) \
                          + FFABS(cur[mrefs + 1] - cur[prefs + 1]) - 1; \
 \
        CHECK(-1) CHECK(-2) }} }} \
        CHECK( 1) CHECK( 2) }} }} \
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

static void filter_line_c(uint8_t *dst,
                          uint8_t *prev, uint8_t *cur, uint8_t *next,
                          int w, int prefs, int mrefs, int parity, int mode)
{
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;

    FILTER
}

static void filter_line_c_16bit(uint16_t *dst,
                                uint16_t *prev, uint16_t *cur, uint16_t *next,
                                int w, int prefs, int mrefs, int parity,
                                int mode)
{
    int x;
    uint16_t *prev2 = parity ? prev : cur ;
    uint16_t *next2 = parity ? cur  : next;
    mrefs /= 2;
    prefs /= 2;

    FILTER
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

        if (i == 1 || i == 2) {
        /* Why is this not part of the per-plane description thing? */
            w >>= yadif->csp->log2_chroma_w;
            h >>= yadif->csp->log2_chroma_h;
        }

        for (y = 0; y < h; y++) {
            if ((y ^ parity) & 1) {
                uint8_t *prev = &yadif->prev->data[i][y * refs];
                uint8_t *cur  = &yadif->cur ->data[i][y * refs];
                uint8_t *next = &yadif->next->data[i][y * refs];
                uint8_t *dst  = &dstpic->data[i][y * dstpic->linesize[i]];
                int     mode  = y == 1 || y + 2 == h ? 2 : yadif->mode;
                yadif->filter_line(dst, prev, cur, next, w,
                                   y + 1 < h ? refs : -refs,
                                   y ? -refs : refs,
                                   parity ^ tff, mode);
            } else {
                memcpy(&dstpic->data[i][y * dstpic->linesize[i]],
                       &yadif->cur->data[i][y * refs], w * df);
            }
        }
    }

    emms_c();
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms,
                                           int w, int h)
{
    AVFilterBufferRef *picref;
    int width  = FFALIGN(w, 32);
    int height = FFALIGN(h + 2, 32);
    int i;

    picref = ff_default_get_video_buffer(link, perms, width, height);

    picref->video->w = w;
    picref->video->h = h;

    for (i = 0; i < 3; i++)
        picref->data[i] += picref->linesize[i];

    return picref;
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

    if (!yadif->csp)
        yadif->csp = av_pix_fmt_desc_get(link->format);
    if (yadif->csp->comp[0].depth_minus1 / 8 == 1)
        yadif->filter_line = filter_line_c_16bit;

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

    if (yadif->frame_pending)
        return_frame(ctx, 1);

    if (yadif->prev)
        avfilter_unref_buffer(yadif->prev);
    yadif->prev = yadif->cur;
    yadif->cur  = yadif->next;
    yadif->next = picref;

    if (!yadif->cur)
        return 0;

    if (yadif->auto_enable && !yadif->cur->video->interlaced) {
        yadif->out  = avfilter_ref_buffer(yadif->cur, AV_PERM_READ);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        avfilter_unref_bufferp(&yadif->prev);
        if (yadif->out->pts != AV_NOPTS_VALUE)
            yadif->out->pts *= 2;
        return ff_filter_frame(ctx->outputs[0], yadif->out);
    }

    if (!yadif->prev &&
        !(yadif->prev = avfilter_ref_buffer(yadif->cur, AV_PERM_READ)))
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

        if (ret == AVERROR_EOF && yadif->next) {
            AVFilterBufferRef *next =
                avfilter_ref_buffer(yadif->next, AV_PERM_READ);

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

    if (yadif->auto_enable && yadif->next && !yadif->next->video->interlaced)
        return val;

    return val * ((yadif->mode&1)+1);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    YADIFContext *yadif = ctx->priv;

    if (yadif->prev) avfilter_unref_bufferp(&yadif->prev);
    if (yadif->cur ) avfilter_unref_bufferp(&yadif->cur );
    if (yadif->next) avfilter_unref_bufferp(&yadif->next);
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

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    YADIFContext *yadif = ctx->priv;

    yadif->mode = 0;
    yadif->parity = -1;
    yadif->auto_enable = 0;
    yadif->csp = NULL;

    if (args)
        sscanf(args, "%d:%d:%d",
               &yadif->mode, &yadif->parity, &yadif->auto_enable);

    yadif->filter_line = filter_line_c;

    if (ARCH_X86)
        ff_yadif_init_x86(yadif);

    av_log(ctx, AV_LOG_VERBOSE, "mode:%d parity:%d auto_enable:%d\n",
           yadif->mode, yadif->parity, yadif->auto_enable);

    return 0;
}

static int config_props(AVFilterLink *link)
{
    link->time_base.num = link->src->inputs[0]->time_base.num;
    link->time_base.den = link->src->inputs[0]->time_base.den * 2;
    link->w             = link->src->inputs[0]->w;
    link->h             = link->src->inputs[0]->h;

    return 0;
}

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

AVFilter avfilter_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image"),

    .priv_size     = sizeof(YADIFContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_yadif_inputs,

    .outputs   = avfilter_vf_yadif_outputs,
};
