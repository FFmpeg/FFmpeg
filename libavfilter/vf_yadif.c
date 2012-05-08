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

#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "yadif.h"

#undef NDEBUG
#include <assert.h>

typedef struct {
    /**
     * 0: send 1 frame for each frame
     * 1: send 1 frame for each field
     * 2: like 0 but skips spatial interlacing check
     * 3: like 1 but skips spatial interlacing check
     */
    int mode;

    /**
     *  0: top field first
     *  1: bottom field first
     * -1: auto-detection
     */
    int parity;

    int frame_pending;

    /**
     *  0: deinterlace all frames
     *  1: only deinterlace frames marked as interlaced
     */
    int auto_enable;

    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    AVFilterBufferRef *prev;
    AVFilterBufferRef *out;
    void (*filter_line)(uint8_t *dst,
                        uint8_t *prev, uint8_t *cur, uint8_t *next,
                        int w, int prefs, int mrefs, int parity, int mode);

    const AVPixFmtDescriptor *csp;
} YADIFContext;

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
        int diff = FFMAX3(temporal_diff0>>1, temporal_diff1, temporal_diff2); \
        int spatial_pred = (c+e)>>1; \
        int spatial_score = FFABS(cur[mrefs-1] - cur[prefs-1]) + FFABS(c-e) \
                          + FFABS(cur[mrefs+1] - cur[prefs+1]) - 1; \
 \
        CHECK(-1) CHECK(-2) }} }} \
        CHECK( 1) CHECK( 2) }} }} \
 \
        if (mode < 2) { \
            int b = (prev2[2*mrefs] + next2[2*mrefs])>>1; \
            int f = (prev2[2*prefs] + next2[2*prefs])>>1; \
            int max = FFMAX3(d-e, d-c, FFMIN(b-c, f-e)); \
            int min = FFMIN3(d-e, d-c, FFMAX(b-c, f-e)); \
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
                                int w, int prefs, int mrefs, int parity, int mode)
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
                uint8_t *prev = &yadif->prev->data[i][y*refs];
                uint8_t *cur  = &yadif->cur ->data[i][y*refs];
                uint8_t *next = &yadif->next->data[i][y*refs];
                uint8_t *dst  = &dstpic->data[i][y*dstpic->linesize[i]];
                int     mode  = y==1 || y+2==h ? 2 : yadif->mode;
                yadif->filter_line(dst, prev, cur, next, w, y+1<h ? refs : -refs, y ? -refs : refs, parity ^ tff, mode);
            } else {
                memcpy(&dstpic->data[i][y*dstpic->linesize[i]],
                       &yadif->cur->data[i][y*refs], w*df);
            }
        }
    }
#if HAVE_MMX
    __asm__ volatile("emms \n\t" : : : "memory");
#endif
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    AVFilterBufferRef *picref;
    int width = FFALIGN(w, 32);
    int height= FFALIGN(h+2, 32);
    int i;

    picref = avfilter_default_get_video_buffer(link, perms, width, height);

    picref->video->w = w;
    picref->video->h = h;

    for (i = 0; i < 3; i++)
        picref->data[i] += picref->linesize[i];

    return picref;
}

static void return_frame(AVFilterContext *ctx, int is_second)
{
    YADIFContext *yadif = ctx->priv;
    AVFilterLink *link= ctx->outputs[0];
    int tff;

    if (yadif->parity == -1) {
        tff = yadif->cur->video->interlaced ?
            yadif->cur->video->top_field_first : 1;
    } else {
        tff = yadif->parity^1;
    }

    if (is_second) {
        yadif->out = avfilter_get_video_buffer(link, AV_PERM_WRITE | AV_PERM_PRESERVE |
                                               AV_PERM_REUSE, link->w, link->h);
        avfilter_copy_buffer_ref_props(yadif->out, yadif->cur);
        yadif->out->video->interlaced = 0;
    }

    if (!yadif->csp)
        yadif->csp = &av_pix_fmt_descriptors[link->format];
    if (yadif->csp->comp[0].depth_minus1 / 8 == 1)
        yadif->filter_line = (void*)filter_line_c_16bit;

    filter(ctx, yadif->out, tff ^ !is_second, tff);

    if (is_second) {
        if (yadif->next->pts != AV_NOPTS_VALUE &&
            yadif->cur->pts != AV_NOPTS_VALUE) {
            uint64_t next_pts = yadif->next->pts;
            uint64_t cur_pts  = yadif->cur->pts;
            uint64_t prev_pts = yadif->prev->pts;

            uint64_t ft = FFMIN3( cur_pts-prev_pts,
                                  next_pts-cur_pts,
                                 (next_pts-prev_pts)/2);

            if(next_pts - cur_pts < 2*ft)
                yadif->out->pts =
                    (next_pts&cur_pts) +
                    ((next_pts^cur_pts)>>1);
            else
                yadif->out->pts = cur_pts + ft/2;
        } else {
            yadif->out->pts = AV_NOPTS_VALUE;
        }
        avfilter_start_frame(ctx->outputs[0], yadif->out);
    }
    avfilter_draw_slice(ctx->outputs[0], 0, link->h, 1);
    avfilter_end_frame(ctx->outputs[0]);

    yadif->frame_pending = (yadif->mode&1) && !is_second;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
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
        return;

    if (yadif->auto_enable && !yadif->cur->video->interlaced) {
        yadif->out  = avfilter_ref_buffer(yadif->cur, AV_PERM_READ);
        avfilter_unref_buffer(yadif->prev);
        yadif->prev = NULL;
        avfilter_start_frame(ctx->outputs[0], yadif->out);
        return;
    }

    if (!yadif->prev)
        yadif->prev = avfilter_ref_buffer(yadif->cur, AV_PERM_READ);

    yadif->out = avfilter_get_video_buffer(ctx->outputs[0], AV_PERM_WRITE | AV_PERM_PRESERVE |
                                       AV_PERM_REUSE, link->w, link->h);

    avfilter_copy_buffer_ref_props(yadif->out, yadif->cur);
    yadif->out->video->interlaced = 0;
    avfilter_start_frame(ctx->outputs[0], yadif->out);
}

static void end_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    YADIFContext *yadif = ctx->priv;

    if (!yadif->out)
        return;

    if (yadif->auto_enable && !yadif->cur->video->interlaced) {
        avfilter_draw_slice(ctx->outputs[0], 0, link->h, 1);
        avfilter_end_frame(ctx->outputs[0]);
        return;
    }

    return_frame(ctx, 0);
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

        if ((ret = avfilter_request_frame(link->src->inputs[0])))
            return ret;
    } while (!yadif->cur);

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    YADIFContext *yadif = link->src->priv;
    int ret, val;

    if (yadif->frame_pending)
        return 1;

    val = avfilter_poll_frame(link->src->inputs[0]);
    if (val <= 0)
        return val;

    if (val >= 1 && !yadif->next) { //FIXME change API to not requre this red tape
        if ((ret = avfilter_request_frame(link->src->inputs[0])) < 0)
            return ret;
        val = avfilter_poll_frame(link->src->inputs[0]);
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

    if (yadif->prev) avfilter_unref_buffer(yadif->prev);
    if (yadif->cur ) avfilter_unref_buffer(yadif->cur );
    if (yadif->next) avfilter_unref_buffer(yadif->next);
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

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    YADIFContext *yadif = ctx->priv;
    int cpu_flags = av_get_cpu_flags();

    yadif->mode = 0;
    yadif->parity = -1;
    yadif->auto_enable = 0;
    yadif->csp = NULL;

    if (args) sscanf(args, "%d:%d:%d", &yadif->mode, &yadif->parity, &yadif->auto_enable);

    yadif->filter_line = filter_line_c;
    if (HAVE_SSSE3 && cpu_flags & AV_CPU_FLAG_SSSE3)
        yadif->filter_line = ff_yadif_filter_line_ssse3;
    else if (HAVE_SSE && cpu_flags & AV_CPU_FLAG_SSE2)
        yadif->filter_line = ff_yadif_filter_line_sse2;
    else if (HAVE_MMX && cpu_flags & AV_CPU_FLAG_MMX)
        yadif->filter_line = ff_yadif_filter_line_mmx;

    av_log(ctx, AV_LOG_INFO, "mode:%d parity:%d auto_enable:%d\n", yadif->mode, yadif->parity, yadif->auto_enable);

    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

AVFilter avfilter_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image."),

    .priv_size     = sizeof(YADIFContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .start_frame      = start_frame,
                                    .get_video_buffer = get_video_buffer,
                                    .draw_slice       = null_draw_slice,
                                    .end_frame        = end_frame,
                                    .rej_perms        = AV_PERM_REUSE2, },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .poll_frame       = poll_frame,
                                    .request_frame    = request_frame, },
                                  { .name = NULL}},
};
