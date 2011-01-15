/*
 * Copyright (C) 2006-2010 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
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
     *  0: bottom field first
     *  1: top field first
     * -1: auto-detection
     */
    int parity;

    int frame_pending;

    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    AVFilterBufferRef *prev;
    AVFilterBufferRef *out;
    void (*filter_line)(uint8_t *dst,
                        uint8_t *prev, uint8_t *cur, uint8_t *next,
                        int w, int refs, int parity, int mode);
} YADIFContext;

static void filter_line_c(uint8_t *dst,
                          uint8_t *prev, uint8_t *cur, uint8_t *next,
                          int w, int refs, int parity, int mode)
{
    int x;
    uint8_t *prev2 = parity ? prev : cur ;
    uint8_t *next2 = parity ? cur  : next;
    for (x = 0;  x < w; x++) {
        int c = cur[-refs];
        int d = (prev2[0] + next2[0])>>1;
        int e = cur[+refs];
        int temporal_diff0 = FFABS(prev2[0] - next2[0]);
        int temporal_diff1 =(FFABS(prev[-refs] - c) + FFABS(prev[+refs] - e) )>>1;
        int temporal_diff2 =(FFABS(next[-refs] - c) + FFABS(next[+refs] - e) )>>1;
        int diff = FFMAX3(temporal_diff0>>1, temporal_diff1, temporal_diff2);
        int spatial_pred = (c+e)>>1;
        int spatial_score = FFABS(cur[-refs-1] - cur[+refs-1]) + FFABS(c-e)
                          + FFABS(cur[-refs+1] - cur[+refs+1]) - 1;

#define CHECK(j)\
    {   int score = FFABS(cur[-refs-1+j] - cur[+refs-1-j])\
                  + FFABS(cur[-refs  +j] - cur[+refs  -j])\
                  + FFABS(cur[-refs+1+j] - cur[+refs+1-j]);\
        if (score < spatial_score) {\
            spatial_score= score;\
            spatial_pred= (cur[-refs  +j] + cur[+refs  -j])>>1;\

        CHECK(-1) CHECK(-2) }} }}
        CHECK( 1) CHECK( 2) }} }}

        if (mode < 2) {
            int b = (prev2[-2*refs] + next2[-2*refs])>>1;
            int f = (prev2[+2*refs] + next2[+2*refs])>>1;
#if 0
            int a = cur[-3*refs];
            int g = cur[+3*refs];
            int max = FFMAX3(d-e, d-c, FFMIN3(FFMAX(b-c,f-e),FFMAX(b-c,b-a),FFMAX(f-g,f-e)) );
            int min = FFMIN3(d-e, d-c, FFMAX3(FFMIN(b-c,f-e),FFMIN(b-c,b-a),FFMIN(f-g,f-e)) );
#else
            int max = FFMAX3(d-e, d-c, FFMIN(b-c, f-e));
            int min = FFMIN3(d-e, d-c, FFMAX(b-c, f-e));
#endif

            diff = FFMAX3(diff, min, -max);
        }

        if (spatial_pred > d + diff)
           spatial_pred = d + diff;
        else if (spatial_pred < d - diff)
           spatial_pred = d - diff;

        dst[0] = spatial_pred;

        dst++;
        cur++;
        prev++;
        next++;
        prev2++;
        next2++;
    }
}

static void filter(AVFilterContext *ctx, AVFilterBufferRef *dstpic,
                   int parity, int tff)
{
    YADIFContext *yadif = ctx->priv;
    int y, i;

    for (i = 0; i < 3; i++) {
        int is_chroma = !!i;
        int w = dstpic->video->w >> is_chroma;
        int h = dstpic->video->h >> is_chroma;
        int refs = yadif->cur->linesize[i];

        for (y = 0; y < h; y++) {
            if ((y ^ parity) & 1) {
                uint8_t *prev = &yadif->prev->data[i][y*refs];
                uint8_t *cur  = &yadif->cur ->data[i][y*refs];
                uint8_t *next = &yadif->next->data[i][y*refs];
                uint8_t *dst  = &dstpic->data[i][y*dstpic->linesize[i]];
                yadif->filter_line(dst, prev, cur, next, w, refs, parity ^ tff, yadif->mode);
            } else {
                memcpy(&dstpic->data[i][y*dstpic->linesize[i]],
                       &yadif->cur->data[i][y*refs], w);
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
    int height= FFALIGN(h+6, 32);
    int i;

    picref = avfilter_default_get_video_buffer(link, perms, width, height);

    picref->video->w = w;
    picref->video->h = h;

    for (i = 0; i < 3; i++)
        picref->data[i] += 3 * picref->linesize[i];

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

    if (is_second)
        yadif->out = avfilter_get_video_buffer(link, AV_PERM_WRITE | AV_PERM_PRESERVE |
                                               AV_PERM_REUSE, link->w, link->h);

    filter(ctx, yadif->out, tff ^ !is_second, tff);

    if (is_second) {
        if (yadif->next->pts != AV_NOPTS_VALUE &&
            yadif->cur->pts != AV_NOPTS_VALUE) {
            yadif->out->pts =
                (yadif->next->pts&yadif->cur->pts) +
                ((yadif->next->pts^yadif->cur->pts)>>1);
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

    if (val==1 && !yadif->next) { //FIXME change API to not requre this red tape
        if ((ret = avfilter_request_frame(link->src->inputs[0])) < 0)
            return ret;
        val = avfilter_poll_frame(link->src->inputs[0]);
    }
    assert(yadif->next);

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
        PIX_FMT_GRAY8,
        PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    YADIFContext *yadif = ctx->priv;
    av_unused int cpu_flags = av_get_cpu_flags();

    yadif->mode = 0;
    yadif->parity = -1;

    if (args) sscanf(args, "%d:%d", &yadif->mode, &yadif->parity);

    yadif->filter_line = filter_line_c;
    if (HAVE_SSSE3 && cpu_flags & AV_CPU_FLAG_SSSE3)
        yadif->filter_line = ff_yadif_filter_line_ssse3;
    else if (HAVE_SSE && cpu_flags & AV_CPU_FLAG_SSE2)
        yadif->filter_line = ff_yadif_filter_line_sse2;
    else if (HAVE_MMX && cpu_flags & AV_CPU_FLAG_MMX)
        yadif->filter_line = ff_yadif_filter_line_mmx;

    av_log(ctx, AV_LOG_INFO, "mode:%d parity:%d\n", yadif->mode, yadif->parity);

    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

AVFilter avfilter_vf_yadif = {
    .name          = "yadif",
    .description   = NULL_IF_CONFIG_SMALL("Deinterlace the input image"),

    .priv_size     = sizeof(YADIFContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .start_frame      = start_frame,
                                    .get_video_buffer = get_video_buffer,
                                    .draw_slice       = null_draw_slice,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .poll_frame       = poll_frame,
                                    .request_frame    = request_frame, },
                                  { .name = NULL}},
};
