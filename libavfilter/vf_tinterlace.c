/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2003 Michael Zucchi <notzed@ximian.com>
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
 * with FFmpeg if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * temporal field interlace filter, ported from MPlayer/libmpcodecs
 */

#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "internal.h"

enum TInterlaceMode {
    MODE_MERGE = 0,
    MODE_DROP_EVEN,
    MODE_DROP_ODD,
    MODE_PAD,
    MODE_INTERLEAVE_TOP,
    MODE_INTERLEAVE_BOTTOM,
    MODE_INTERLACEX2,
};

static const char *tinterlace_mode_str[] = {
    "merge",
    "drop_even",
    "drop_odd",
    "pad",
    "interleave_top",
    "interleave_bottom",
    "interlacex2",
    NULL
};

typedef struct {
    enum TInterlaceMode mode;   ///< interlace mode selected
    int frame;                  ///< number of the output frame
    int vsub;                   ///< chroma vertical subsampling
    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    uint8_t *black_data[4];     ///< buffer used to fill padded lines
    int black_linesize[4];
} TInterlaceContext;

#define FULL_SCALE_YUVJ_FORMATS \
    PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUVJ440P

static enum PixelFormat full_scale_yuvj_pix_fmts[] = {
    FULL_SCALE_YUVJ_FORMATS, PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P,  PIX_FMT_YUV422P,  PIX_FMT_YUV444P,
        PIX_FMT_YUV444P,  PIX_FMT_YUV410P,  PIX_FMT_YUVA420P,
        PIX_FMT_GRAY8, FULL_SCALE_YUVJ_FORMATS,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    TInterlaceContext *tinterlace = ctx->priv;
    int i;
    char c;

    tinterlace->mode = MODE_MERGE;

    if (args) {
        if (sscanf(args, "%d%c", (int *)&tinterlace->mode, &c) == 1) {
            if (tinterlace->mode > 6) {
                av_log(ctx, AV_LOG_ERROR,
                       "Invalid mode '%s', use an integer between 0 and 6\n", args);
                return AVERROR(EINVAL);
            }

            av_log(ctx, AV_LOG_WARNING,
                   "Using numeric constant is deprecated, use symbolic values\n");
        } else {
            for (i = 0; tinterlace_mode_str[i]; i++) {
                if (!strcmp(tinterlace_mode_str[i], args)) {
                    tinterlace->mode = i;
                    break;
                }
            }
            if (!tinterlace_mode_str[i]) {
                av_log(ctx, AV_LOG_ERROR, "Invalid argument '%s'\n", args);
                return AVERROR(EINVAL);
            }
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TInterlaceContext *tinterlace = ctx->priv;

    if (tinterlace->cur ) avfilter_unref_bufferp(&tinterlace->cur );
    if (tinterlace->next) avfilter_unref_bufferp(&tinterlace->next);

    av_freep(&tinterlace->black_data[0]);
}

static int config_out_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[outlink->format];
    TInterlaceContext *tinterlace = ctx->priv;

    tinterlace->vsub = desc->log2_chroma_h;
    outlink->w = inlink->w;
    outlink->h = tinterlace->mode == MODE_MERGE || tinterlace->mode == MODE_PAD ?
        inlink->h*2 : inlink->h;

    if (tinterlace->mode == MODE_PAD) {
        uint8_t black[4] = { 16, 128, 128, 16 };
        int i, ret;
        if (ff_fmt_is_in(outlink->format, full_scale_yuvj_pix_fmts))
            black[0] = black[3] = 0;
        ret = av_image_alloc(tinterlace->black_data, tinterlace->black_linesize,
                             outlink->w, outlink->h, outlink->format, 1);
        if (ret < 0)
            return ret;

        /* fill black picture with black */
        for (i = 0; i < 4 && tinterlace->black_data[i]; i++) {
            int h = i == 1 || i == 2 ? outlink->h >> desc->log2_chroma_h : outlink->h;
            memset(tinterlace->black_data[i], black[i],
                   tinterlace->black_linesize[i] * h);
        }
    }
    av_log(ctx, AV_LOG_VERBOSE, "mode:%s h:%d -> h:%d\n",
           tinterlace_mode_str[tinterlace->mode], inlink->h, outlink->h);

    return 0;
}

#define FIELD_UPPER           0
#define FIELD_LOWER           1
#define FIELD_UPPER_AND_LOWER 2

/**
 * Copy picture field from src to dst.
 *
 * @param src_field copy from upper, lower field or both
 * @param interleave leave a padding line between each copied line
 * @param dst_field copy to upper or lower field,
 *        only meaningful when interleave is selected
 */
static inline
void copy_picture_field(uint8_t *dst[4], int dst_linesize[4],
                        const uint8_t *src[4], int src_linesize[4],
                        enum PixelFormat format, int w, int src_h,
                        int src_field, int interleave, int dst_field)
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[format];
    int plane, vsub = desc->log2_chroma_h;
    int k = src_field == FIELD_UPPER_AND_LOWER ? 1 : 2;

    for (plane = 0; plane < desc->nb_components; plane++) {
        int lines = plane == 1 || plane == 2 ? src_h >> vsub : src_h;
        int linesize = av_image_get_linesize(format, w, plane);
        uint8_t *dstp = dst[plane];
        const uint8_t *srcp = src[plane];
        lines /= k;
        if (src_field == FIELD_LOWER)
            srcp += src_linesize[plane];
        if (interleave && dst_field == FIELD_LOWER)
            dstp += dst_linesize[plane];
        av_image_copy_plane(dstp, dst_linesize[plane] * (interleave ? 2 : 1),
                            srcp, src_linesize[plane]*k, linesize, lines);
    }
}

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = inlink->dst;
    TInterlaceContext *tinterlace = ctx->priv;

    avfilter_unref_buffer(tinterlace->cur);
    tinterlace->cur  = tinterlace->next;
    tinterlace->next = picref;
    inlink->cur_buf = NULL;
    return 0;
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    TInterlaceContext *tinterlace = ctx->priv;
    AVFilterBufferRef *cur  = tinterlace->cur;
    AVFilterBufferRef *next = tinterlace->next;
    AVFilterBufferRef *out  = NULL;
    int field, tff;

    /* we need at least two frames */
    if (!tinterlace->cur)
        return 0;

    switch (tinterlace->mode) {
    case MODE_MERGE: /* move the odd frame into the upper field of the new image, even into
             * the lower field, generating a double-height video at half framerate */
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(out, cur);
        out->video->h = outlink->h;
        out->video->interlaced = 1;
        out->video->top_field_first = 1;

        /* write odd frame lines into the upper field of the new frame */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)cur->data, cur->linesize,
                           inlink->format, inlink->w, inlink->h,
                           FIELD_UPPER_AND_LOWER, 1, FIELD_UPPER);
        /* write even frame lines into the lower field of the new frame */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)next->data, next->linesize,
                           inlink->format, inlink->w, inlink->h,
                           FIELD_UPPER_AND_LOWER, 1, FIELD_LOWER);
        avfilter_unref_bufferp(&tinterlace->next);
        break;

    case MODE_DROP_ODD:  /* only output even frames, odd  frames are dropped; height unchanged, half framerate */
    case MODE_DROP_EVEN: /* only output odd  frames, even frames are dropped; height unchanged, half framerate */
        out = avfilter_ref_buffer(tinterlace->mode == MODE_DROP_EVEN ? cur : next, AV_PERM_READ);
        avfilter_unref_bufferp(&tinterlace->next);
        break;

    case MODE_PAD: /* expand each frame to double height, but pad alternate
                    * lines with black; framerate unchanged */
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(out, cur);
        out->video->h = outlink->h;

        field = (1 + tinterlace->frame) & 1 ? FIELD_UPPER : FIELD_LOWER;
        /* copy upper and lower fields */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)cur->data, cur->linesize,
                           inlink->format, inlink->w, inlink->h,
                           FIELD_UPPER_AND_LOWER, 1, field);
        /* pad with black the other field */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)tinterlace->black_data, tinterlace->black_linesize,
                           inlink->format, inlink->w, inlink->h,
                           FIELD_UPPER_AND_LOWER, 1, !field);
        break;

        /* interleave upper/lower lines from odd frames with lower/upper lines from even frames,
         * halving the frame rate and preserving image height */
    case MODE_INTERLEAVE_TOP:    /* top    field first */
    case MODE_INTERLEAVE_BOTTOM: /* bottom field first */
        tff = tinterlace->mode == MODE_INTERLEAVE_TOP;
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(out, cur);
        out->video->interlaced = 1;
        out->video->top_field_first = tff;

        /* copy upper/lower field from cur */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)cur->data, cur->linesize,
                           inlink->format, inlink->w, inlink->h,
                           tff ? FIELD_UPPER : FIELD_LOWER, 1, tff ? FIELD_UPPER : FIELD_LOWER);
        /* copy lower/upper field from next */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)next->data, next->linesize,
                           inlink->format, inlink->w, inlink->h,
                           tff ? FIELD_LOWER : FIELD_UPPER, 1, tff ? FIELD_LOWER : FIELD_UPPER);
        avfilter_unref_bufferp(&tinterlace->next);
        break;
    case MODE_INTERLACEX2: /* re-interlace preserving image height, double frame rate */
        /* output current frame first */
        out = avfilter_ref_buffer(cur, ~AV_PERM_WRITE);
        out->video->interlaced = 1;

        ff_start_frame(outlink, out);
        ff_draw_slice(outlink, 0, outlink->h, 1);
        ff_end_frame(outlink);

        /* output mix of current and next frame */
        tff = next->video->top_field_first;
        out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(out, next);
        out->video->interlaced = 1;

        /* write current frame second field lines into the second field of the new frame */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)cur->data, cur->linesize,
                           inlink->format, inlink->w, inlink->h,
                           tff ? FIELD_LOWER : FIELD_UPPER, 1, tff ? FIELD_LOWER : FIELD_UPPER);
        /* write next frame first field lines into the first field of the new frame */
        copy_picture_field(out->data, out->linesize,
                           (const uint8_t **)next->data, next->linesize,
                           inlink->format, inlink->w, inlink->h,
                           tff ? FIELD_UPPER : FIELD_LOWER, 1, tff ? FIELD_UPPER : FIELD_LOWER);
        break;
    }

    ff_start_frame(outlink, out);
    ff_draw_slice(outlink, 0, outlink->h, 1);
    ff_end_frame(outlink);

    tinterlace->frame++;

    return 0;
}

static int poll_frame(AVFilterLink *outlink)
{
    TInterlaceContext *tinterlace = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret, val;

    val = ff_poll_frame(inlink);

    if (val == 1 && !tinterlace->next) {
        if ((ret = ff_request_frame(inlink)) < 0)
            return ret;
        val = ff_poll_frame(inlink);
    }
    av_assert0(tinterlace->next);

    return val;
}

static int request_frame(AVFilterLink *outlink)
{
    TInterlaceContext *tinterlace = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];

    do {
        int ret;

        if ((ret = ff_request_frame(inlink)) < 0)
            return ret;
    } while (!tinterlace->cur);

    return 0;
}

static int null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { return 0; }

AVFilter avfilter_vf_tinterlace = {
    .name          = "tinterlace",
    .description   = NULL_IF_CONFIG_SMALL("Perform temporal field interlacing."),
    .priv_size     = sizeof(TInterlaceContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name          = "default",
          .type          = AVMEDIA_TYPE_VIDEO,
          .start_frame   = start_frame,
          .draw_slice    = null_draw_slice,
          .end_frame     = end_frame, },
        { .name = NULL}
    },
    .outputs = (const AVFilterPad[]) {
        { .name          = "default",
          .type          = AVMEDIA_TYPE_VIDEO,
          .config_props  = config_out_props,
          .poll_frame    = poll_frame,
          .request_frame = request_frame },
        { .name = NULL}
    },
};
