/*
 * The default get_buffer2() implementation
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

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"
#include "libavutil/version.h"

#include "avcodec.h"
#include "internal.h"
#include "libavutil/refstruct.h"

typedef struct FramePool {
    /**
     * Pools for each data plane. For audio all the planes have the same size,
     * so only pools[0] is used.
     */
    AVBufferPool *pools[4];

    /*
     * Pool parameters
     */
    int format;
    int width, height;
    int stride_align[AV_NUM_DATA_POINTERS];
    int linesize[4];
    int planes;
    int channels;
    int samples;
} FramePool;

static void frame_pool_free(AVRefStructOpaque unused, void *obj)
{
    FramePool *pool = obj;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(pool->pools); i++)
        av_buffer_pool_uninit(&pool->pools[i]);
}

static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int i, ret;

    if (pool && pool->format == frame->format) {
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO &&
            pool->width == frame->width && pool->height == frame->height)
            return 0;
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO &&
            pool->channels == frame->ch_layout.nb_channels &&
            frame->nb_samples == pool->samples)
            return 0;
    }

    pool = av_refstruct_alloc_ext(sizeof(*pool), 0, NULL, frame_pool_free);
    if (!pool)
        return AVERROR(ENOMEM);

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
        int linesize[4];
        int w = frame->width;
        int h = frame->height;
        int unaligned;
        ptrdiff_t linesize1[4];
        size_t size[4];

        avcodec_align_dimensions2(avctx, &w, &h, pool->stride_align);

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            ret = av_image_fill_linesizes(linesize, avctx->pix_fmt, w);
            if (ret < 0)
                goto fail;
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w - 1);

            unaligned = 0;
            for (i = 0; i < 4; i++)
                unaligned |= linesize[i] % pool->stride_align[i];
        } while (unaligned);

        for (i = 0; i < 4; i++)
            linesize1[i] = linesize[i];
        ret = av_image_fill_plane_sizes(size, avctx->pix_fmt, h, linesize1);
        if (ret < 0)
            goto fail;

        for (i = 0; i < 4; i++) {
            pool->linesize[i] = linesize[i];
            if (size[i]) {
                if (size[i] > INT_MAX - (16 + STRIDE_ALIGN - 1)) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }
                pool->pools[i] = av_buffer_pool_init(size[i] + 16 + STRIDE_ALIGN - 1,
                                                     CONFIG_MEMORY_POISONING ?
                                                        NULL :
                                                        av_buffer_allocz);
                if (!pool->pools[i]) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }
        }
        pool->format = frame->format;
        pool->width  = frame->width;
        pool->height = frame->height;

        break;
        }
    case AVMEDIA_TYPE_AUDIO: {
        ret = av_samples_get_buffer_size(&pool->linesize[0],
                                         frame->ch_layout.nb_channels,
                                         frame->nb_samples, frame->format, 0);
        if (ret < 0)
            goto fail;

        pool->pools[0] = av_buffer_pool_init(pool->linesize[0],
                                             CONFIG_MEMORY_POISONING ?
                                                NULL :
                                                av_buffer_allocz);
        if (!pool->pools[0]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        pool->format     = frame->format;
        pool->channels   = frame->ch_layout.nb_channels;
        pool->samples = frame->nb_samples;
        pool->planes     = av_sample_fmt_is_planar(pool->format) ? pool->channels : 1;
        break;
        }
    default: av_assert0(0);
    }

    av_refstruct_unref(&avctx->internal->pool);
    avctx->internal->pool = pool;

    return 0;
fail:
    av_refstruct_unref(&pool);
    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int planes = pool->planes;
    int i;

    frame->linesize[0] = pool->linesize[0];

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_calloc(planes, sizeof(*frame->extended_data));
        frame->nb_extended_buf = planes - AV_NUM_DATA_POINTERS;
        frame->extended_buf  = av_calloc(frame->nb_extended_buf,
                                          sizeof(*frame->extended_buf));
        if (!frame->extended_data || !frame->extended_buf) {
            av_freep(&frame->extended_data);
            av_freep(&frame->extended_buf);
            return AVERROR(ENOMEM);
        }
    } else {
        frame->extended_data = frame->data;
        av_assert0(frame->nb_extended_buf == 0);
    }

    for (i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->buf[i])
            goto fail;
        frame->extended_data[i] = frame->data[i] = frame->buf[i]->data;
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        frame->extended_buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->extended_buf[i])
            goto fail;
        frame->extended_data[i + AV_NUM_DATA_POINTERS] = frame->extended_buf[i]->data;
    }

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "default_get_buffer called on frame %p", frame);

    return 0;
fail:
    av_frame_unref(frame);
    return AVERROR(ENOMEM);
}

static int video_get_buffer(AVCodecContext *s, AVFrame *pic)
{
    FramePool *pool = s->internal->pool;
    int i;

    if (pic->data[0] || pic->data[1] || pic->data[2] || pic->data[3]) {
        av_log(s, AV_LOG_ERROR, "pic->data[*]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }

    memset(pic->data, 0, sizeof(pic->data));
    pic->extended_data = pic->data;

    for (i = 0; i < 4 && pool->pools[i]; i++) {
        pic->linesize[i] = pool->linesize[i];

        pic->buf[i] = av_buffer_pool_get(pool->pools[i]);
        if (!pic->buf[i])
            goto fail;

        pic->data[i] = pic->buf[i]->data;
    }
    for (; i < AV_NUM_DATA_POINTERS; i++) {
        pic->data[i] = NULL;
        pic->linesize[i] = 0;
    }

    if (s->debug & FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p\n", pic);

    return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

int avcodec_default_get_buffer2(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret;

    if (avctx->hw_frames_ctx) {
        ret = av_hwframe_get_buffer(avctx->hw_frames_ctx, frame, 0);
        if (ret == AVERROR(ENOMEM)) {
            AVHWFramesContext *frames_ctx =
                (AVHWFramesContext*)avctx->hw_frames_ctx->data;
            if (frames_ctx->initial_pool_size > 0 &&
                !avctx->internal->warned_on_failed_allocation_from_fixed_pool) {
                av_log(avctx, AV_LOG_WARNING, "Failed to allocate a %s/%s "
                       "frame from a fixed pool of hardware frames.\n",
                       av_get_pix_fmt_name(frames_ctx->format),
                       av_get_pix_fmt_name(frames_ctx->sw_format));
                av_log(avctx, AV_LOG_WARNING, "Consider setting "
                       "extra_hw_frames to a larger value "
                       "(currently set to %d, giving a pool size of %d).\n",
                       avctx->extra_hw_frames, frames_ctx->initial_pool_size);
                avctx->internal->warned_on_failed_allocation_from_fixed_pool = 1;
            }
        }
        frame->width  = avctx->coded_width;
        frame->height = avctx->coded_height;
        return ret;
    }

    if ((ret = update_frame_pool(avctx, frame)) < 0)
        return ret;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return video_get_buffer(avctx, frame);
    case AVMEDIA_TYPE_AUDIO:
        return audio_get_buffer(avctx, frame);
    default:
        return -1;
    }
}
