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
#include <stdio.h>

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
                                          int w, int h, enum AVPixelFormat format)
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
