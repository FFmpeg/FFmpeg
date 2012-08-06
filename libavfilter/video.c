/*
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

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

#ifdef DEBUG
static char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms)
{
    snprintf(buf, buf_size, "%s%s%s%s%s%s",
             perms & AV_PERM_READ      ? "r" : "",
             perms & AV_PERM_WRITE     ? "w" : "",
             perms & AV_PERM_PRESERVE  ? "p" : "",
             perms & AV_PERM_REUSE     ? "u" : "",
             perms & AV_PERM_REUSE2    ? "U" : "",
             perms & AV_PERM_NEG_LINESIZES ? "n" : "");
    return buf;
}
#endif

static void ff_dlog_ref(void *ctx, AVFilterBufferRef *ref, int end)
{
    av_unused char buf[16];
    av_dlog(ctx,
            "ref[%p buf:%p refcount:%d perms:%s data:%p linesize[%d, %d, %d, %d] pts:%"PRId64" pos:%"PRId64,
            ref, ref->buf, ref->buf->refcount, ff_get_ref_perms_string(buf, sizeof(buf), ref->perms), ref->data[0],
            ref->linesize[0], ref->linesize[1], ref->linesize[2], ref->linesize[3],
            ref->pts, ref->pos);

    if (ref->video) {
        av_dlog(ctx, " a:%d/%d s:%dx%d i:%c iskey:%d type:%c",
                ref->video->pixel_aspect.num, ref->video->pixel_aspect.den,
                ref->video->w, ref->video->h,
                !ref->video->interlaced     ? 'P' :         /* Progressive  */
                ref->video->top_field_first ? 'T' : 'B',    /* Top / Bottom */
                ref->video->key_frame,
                av_get_picture_type_char(ref->video->pict_type));
    }
    if (ref->audio) {
        av_dlog(ctx, " cl:%"PRId64"d n:%d r:%d p:%d",
                ref->audio->channel_layout,
                ref->audio->nb_samples,
                ref->audio->sample_rate,
                ref->audio->planar);
    }

    av_dlog(ctx, "]%s", end ? "\n" : "");
}

AVFilterBufferRef *ff_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    return ff_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterBufferRef *ff_default_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    int linesize[4];
    uint8_t *data[4];
    AVFilterBufferRef *picref = NULL;

    // +2 is needed for swscaler, +16 to be SIMD-friendly
    if (av_image_alloc(data, linesize, w, h, link->format, 16) < 0)
        return NULL;

    picref = avfilter_get_video_buffer_ref_from_arrays(data, linesize,
                                                       perms, w, h, link->format);
    if (!picref) {
        av_free(data[0]);
        return NULL;
    }

    return picref;
}

AVFilterBufferRef *
avfilter_get_video_buffer_ref_from_arrays(uint8_t *data[4], int linesize[4], int perms,
                                          int w, int h, enum PixelFormat format)
{
    AVFilterBuffer *pic = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *picref = av_mallocz(sizeof(AVFilterBufferRef));

    if (!pic || !picref)
        goto fail;

    picref->buf = pic;
    picref->buf->free = ff_avfilter_default_free_buffer;
    if (!(picref->video = av_mallocz(sizeof(AVFilterBufferRefVideoProps))))
        goto fail;

    pic->w = picref->video->w = w;
    pic->h = picref->video->h = h;

    /* make sure the buffer gets read permission or it's useless for output */
    picref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    picref->type = AVMEDIA_TYPE_VIDEO;
    pic->format = picref->format = format;

    memcpy(pic->data,        data,          4*sizeof(data[0]));
    memcpy(pic->linesize,    linesize,      4*sizeof(linesize[0]));
    memcpy(picref->data,     pic->data,     sizeof(picref->data));
    memcpy(picref->linesize, pic->linesize, sizeof(picref->linesize));

    pic->   extended_data = pic->data;
    picref->extended_data = picref->data;

    picref->pts = AV_NOPTS_VALUE;

    return picref;

fail:
    if (picref && picref->video)
        av_free(picref->video);
    av_free(picref);
    av_free(pic);
    return NULL;
}

AVFilterBufferRef *ff_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    AVFilterBufferRef *ret = NULL;

    av_unused char buf[16];
    FF_DPRINTF_START(NULL, get_video_buffer); ff_dlog_link(NULL, link, 0);
    av_dlog(NULL, " perms:%s w:%d h:%d\n", ff_get_ref_perms_string(buf, sizeof(buf), perms), w, h);

    if (link->dstpad->get_video_buffer)
        ret = link->dstpad->get_video_buffer(link, perms, w, h);

    if (!ret)
        ret = ff_default_get_video_buffer(link, perms, w, h);

    if (ret)
        ret->type = AVMEDIA_TYPE_VIDEO;

    FF_DPRINTF_START(NULL, get_video_buffer); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " returning "); ff_dlog_ref(NULL, ret, 1);

    return ret;
}

int ff_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterBufferRef *buf_out = avfilter_ref_buffer(picref, ~0);
    if (!buf_out)
        return AVERROR(ENOMEM);
    return ff_start_frame(link->dst->outputs[0], buf_out);
}

// for filters that support (but don't require) outpic==inpic
int ff_inplace_start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *outpicref = NULL, *for_next_filter;
    int ret = 0;

    if ((inpicref->perms & AV_PERM_WRITE) && !(inpicref->perms & AV_PERM_PRESERVE)) {
        outpicref = avfilter_ref_buffer(inpicref, ~0);
        if (!outpicref)
            return AVERROR(ENOMEM);
    } else {
        outpicref = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!outpicref)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(outpicref, inpicref);
        outpicref->video->w = outlink->w;
        outpicref->video->h = outlink->h;
    }

    for_next_filter = avfilter_ref_buffer(outpicref, ~0);
    if (for_next_filter)
        ret = ff_start_frame(outlink, for_next_filter);
    else
        ret = AVERROR(ENOMEM);

    if (ret < 0) {
        avfilter_unref_bufferp(&outpicref);
        return ret;
    }

    outlink->out_buf = outpicref;
    return 0;
}

static int default_start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        AVFilterBufferRef *buf_out;
        outlink->out_buf = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!outlink->out_buf)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
        buf_out = avfilter_ref_buffer(outlink->out_buf, ~0);
        if (!buf_out)
            return AVERROR(ENOMEM);

        return ff_start_frame(outlink, buf_out);
    }
    return 0;
}

static void clear_link(AVFilterLink *link)
{
    avfilter_unref_bufferp(&link->cur_buf);
    avfilter_unref_bufferp(&link->src_buf);
    avfilter_unref_bufferp(&link->out_buf);
}

/* XXX: should we do the duplicating of the picture ref here, instead of
 * forcing the source filter to do it? */
int ff_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    int (*start_frame)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *dst = link->dstpad;
    int ret, perms = picref->perms;

    FF_DPRINTF_START(NULL, start_frame); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " "); ff_dlog_ref(NULL, picref, 1);

    if (!(start_frame = dst->start_frame))
        start_frame = default_start_frame;

    if (picref->linesize[0] < 0)
        perms |= AV_PERM_NEG_LINESIZES;
    /* prepare to copy the picture if it has insufficient permissions */
    if ((dst->min_perms & perms) != dst->min_perms || dst->rej_perms & perms) {
        av_log(link->dst, AV_LOG_DEBUG,
                "frame copy needed (have perms %x, need %x, reject %x)\n",
                picref->perms,
                link->dstpad->min_perms, link->dstpad->rej_perms);

        link->cur_buf = ff_get_video_buffer(link, dst->min_perms, link->w, link->h);
        if (!link->cur_buf) {
            avfilter_unref_bufferp(&picref);
            return AVERROR(ENOMEM);
        }

        link->src_buf = picref;
        avfilter_copy_buffer_ref_props(link->cur_buf, link->src_buf);
    }
    else
        link->cur_buf = picref;

    ret = start_frame(link, link->cur_buf);
    if (ret < 0)
        clear_link(link);

    return ret;
}

int ff_null_end_frame(AVFilterLink *link)
{
    return ff_end_frame(link->dst->outputs[0]);
}

static int default_end_frame(AVFilterLink *inlink)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        return ff_end_frame(outlink);
    }
    return 0;
}

int ff_end_frame(AVFilterLink *link)
{
    int (*end_frame)(AVFilterLink *);
    int ret;

    if (!(end_frame = link->dstpad->end_frame))
        end_frame = default_end_frame;

    ret = end_frame(link);

    clear_link(link);

    return ret;
}

int ff_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    return ff_draw_slice(link->dst->outputs[0], y, h, slice_dir);
}

static int default_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink)
        return ff_draw_slice(outlink, y, h, slice_dir);
    return 0;
}

int ff_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    uint8_t *src[4], *dst[4];
    int i, j, vsub, ret;
    int (*draw_slice)(AVFilterLink *, int, int, int);

    FF_DPRINTF_START(NULL, draw_slice); ff_dlog_link(NULL, link, 0); av_dlog(NULL, " y:%d h:%d dir:%d\n", y, h, slice_dir);

    /* copy the slice if needed for permission reasons */
    if (link->src_buf) {
        vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

        for (i = 0; i < 4; i++) {
            if (link->src_buf->data[i]) {
                src[i] = link->src_buf-> data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->src_buf-> linesize[i];
                dst[i] = link->cur_buf->data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->cur_buf->linesize[i];
            } else
                src[i] = dst[i] = NULL;
        }

        for (i = 0; i < 4; i++) {
            int planew =
                av_image_get_linesize(link->format, link->cur_buf->video->w, i);

            if (!src[i]) continue;

            for (j = 0; j < h >> (i==1 || i==2 ? vsub : 0); j++) {
                memcpy(dst[i], src[i], planew);
                src[i] += link->src_buf->linesize[i];
                dst[i] += link->cur_buf->linesize[i];
            }
        }
    }

    if (!(draw_slice = link->dstpad->draw_slice))
        draw_slice = default_draw_slice;
    ret = draw_slice(link, y, h, slice_dir);
    if (ret < 0)
        clear_link(link);
    return ret;
}
