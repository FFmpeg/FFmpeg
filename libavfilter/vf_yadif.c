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
        int absrefs = FFABS(refs);
        int df = (yadif->csp->comp[i].depth_minus1 + 8) / 8;

        if (i == 1 || i == 2) {
        /* Why is this not part of the per-plane description thing? */
            w >>= yadif->csp->log2_chroma_w;
            h >>= yadif->csp->log2_chroma_h;
        }

        if(yadif->temp_line_size < absrefs) {
            av_free(yadif->temp_line);
            yadif->temp_line = av_mallocz(2*64 + 5*absrefs);
            yadif->temp_line_size = absrefs;
        }

        for (y = 0; y < h; y++) {
            if ((y ^ parity) & 1) {
                uint8_t *prev = &yadif->prev->data[i][y * refs];
                uint8_t *cur  = &yadif->cur ->data[i][y * refs];
                uint8_t *next = &yadif->next->data[i][y * refs];
                uint8_t *dst  = &dstpic->data[i][y * dstpic->linesize[i]];
                int     mode  = y == 1 || y + 2 == h ? 2 : yadif->mode;
                int     prefs = y+1<h ? refs : -refs;
                int     mrefs =     y ?-refs :  refs;

                if(y<=1 || y+2>=h) {
                    uint8_t *tmp = yadif->temp_line + 64 + 2*absrefs;
                    if(mode<2)
                        memcpy(tmp+2*mrefs, cur+2*mrefs, w*df);
                    memcpy(tmp+mrefs, cur+mrefs, w*df);
                    memcpy(tmp      , cur      , w*df);
                    if(prefs != mrefs) {
                        memcpy(tmp+prefs, cur+prefs, w*df);
                        if(mode<2)
                            memcpy(tmp+2*prefs, cur+2*prefs, w*df);
                    }
                    cur = tmp;
                }

                yadif->filter_line(dst, prev, cur, next, w,
                                   prefs, mrefs,
                                   parity ^ tff, mode);
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

    if (!yadif->csp)
        yadif->csp = &av_pix_fmt_descriptors[link->format];
    if (yadif->csp->comp[0].depth_minus1 / 8 == 1)
        yadif->filter_line = (void*)filter_line_c_16bit;

    filter(ctx, yadif->out, tff ^ !is_second, tff);

    if (is_second) {
        int64_t cur_pts  = yadif->cur->pts;
        int64_t next_pts = yadif->next->pts;

        if (next_pts != AV_NOPTS_VALUE && cur_pts != AV_NOPTS_VALUE) {
            yadif->out->pts = cur_pts + next_pts;
        } else {
            yadif->out->pts = AV_NOPTS_VALUE;
        }
        ret = ff_start_frame(ctx->outputs[0], yadif->out);
        if (ret < 0)
            return ret;
    }
    if ((ret = ff_draw_slice(ctx->outputs[0], 0, link->h, 1)) < 0 ||
        (ret = ff_end_frame(ctx->outputs[0])) < 0)
        return ret;

    yadif->frame_pending = (yadif->mode&1) && !is_second;
    return 0;
}

static int start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    av_assert0(picref);

    if (picref->video->h < 3 || picref->video->w < 3) {
        av_log(ctx, AV_LOG_ERROR, "Video of less than 3 columns or lines is not supported\n");
        return AVERROR(EINVAL);
    }

    if (yadif->frame_pending)
        return_frame(ctx, 1);

    if (yadif->prev)
        avfilter_unref_buffer(yadif->prev);
    yadif->prev = yadif->cur;
    yadif->cur  = yadif->next;
    yadif->next = picref;
    link->cur_buf = NULL;

    if (!yadif->cur)
        return 0;

    if (yadif->auto_enable && !yadif->cur->video->interlaced) {
        yadif->out  = avfilter_ref_buffer(yadif->cur, ~AV_PERM_WRITE);
        if (!yadif->out)
            return AVERROR(ENOMEM);

        avfilter_unref_bufferp(&yadif->prev);
        if (yadif->out->pts != AV_NOPTS_VALUE)
            yadif->out->pts *= 2;
        return ff_start_frame(ctx->outputs[0], yadif->out);
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

    return ff_start_frame(ctx->outputs[0], yadif->out);
}

static int end_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    if (!yadif->out)
        return 0;

    if (yadif->auto_enable && !yadif->cur->video->interlaced) {
        int ret = ff_draw_slice(ctx->outputs[0], 0, link->h, 1);
        if (ret >= 0)
            ret = ff_end_frame(ctx->outputs[0]);
        return ret;
    }

    return_frame(ctx, 0);
    return 0;
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

            start_frame(link->src->inputs[0], next);
            end_frame(link->src->inputs[0]);
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
    if (val >= 1 && !yadif->next) {
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
    av_freep(&yadif->temp_line); yadif->temp_line_size = 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P,
        PIX_FMT_YUV422P,
        PIX_FMT_YUV444P,
        PIX_FMT_YUV410P,
        PIX_FMT_YUV411P,
        PIX_FMT_GRAY8,
        PIX_FMT_YUVJ420P,
        PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ444P,
        AV_NE( PIX_FMT_GRAY16BE, PIX_FMT_GRAY16LE ),
        PIX_FMT_YUV440P,
        PIX_FMT_YUVJ440P,
        AV_NE( PIX_FMT_YUV420P10BE, PIX_FMT_YUV420P10LE ),
        AV_NE( PIX_FMT_YUV422P10BE, PIX_FMT_YUV422P10LE ),
        AV_NE( PIX_FMT_YUV444P10BE, PIX_FMT_YUV444P10LE ),
        AV_NE( PIX_FMT_YUV420P16BE, PIX_FMT_YUV420P16LE ),
        AV_NE( PIX_FMT_YUV422P16BE, PIX_FMT_YUV422P16LE ),
        AV_NE( PIX_FMT_YUV444P16BE, PIX_FMT_YUV444P16LE ),
        PIX_FMT_YUVA420P,
        PIX_FMT_YUVA422P,
        PIX_FMT_YUVA444P,
        PIX_FMT_NONE
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

    if (HAVE_MMX)
        ff_yadif_init_x86(yadif);

    av_log(ctx, AV_LOG_VERBOSE, "mode:%d parity:%d auto_enable:%d\n",
           yadif->mode, yadif->parity, yadif->auto_enable);

    return 0;
}

static int null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    return 0;
}

static int config_props(AVFilterLink *link)
{
    YADIFContext *yadif = link->src->priv;

    link->time_base.num = link->src->inputs[0]->time_base.num;
    link->time_base.den = link->src->inputs[0]->time_base.den * 2;
    link->w             = link->src->inputs[0]->w;
    link->h             = link->src->inputs[0]->h;

    if(yadif->mode&1)
        link->frame_rate = av_mul_q(link->src->inputs[0]->frame_rate, (AVRational){2,1});

    return 0;
}

AVFilter avfilter_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image."),

    .priv_size     = sizeof(YADIFContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .start_frame      = start_frame,
                                          .draw_slice       = null_draw_slice,
                                          .end_frame        = end_frame,
                                          .min_perms        = AV_PERM_PRESERVE, },
                                        { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .poll_frame       = poll_frame,
                                          .request_frame    = request_frame,
                                          .config_props     = config_props, },
                                        { .name = NULL}},
};
