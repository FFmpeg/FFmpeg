/*
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

#include "channel_layout.h"
#include "avassert.h"
#include "buffer.h"
#include "dict.h"
#include "frame.h"
#include "imgutils.h"
#include "mem.h"
#include "refstruct.h"
#include "samplefmt.h"
#include "side_data.h"
#include "hwcontext.h"

static void get_frame_defaults(AVFrame *frame)
{
    memset(frame, 0, sizeof(*frame));

    frame->pts                   =
    frame->pkt_dts               = AV_NOPTS_VALUE;
    frame->best_effort_timestamp = AV_NOPTS_VALUE;
    frame->duration            = 0;
    frame->time_base           = (AVRational){ 0, 1 };
    frame->sample_aspect_ratio = (AVRational){ 0, 1 };
    frame->format              = -1; /* unknown */
    frame->extended_data       = frame->data;
    frame->color_primaries     = AVCOL_PRI_UNSPECIFIED;
    frame->color_trc           = AVCOL_TRC_UNSPECIFIED;
    frame->colorspace          = AVCOL_SPC_UNSPECIFIED;
    frame->color_range         = AVCOL_RANGE_UNSPECIFIED;
    frame->chroma_location     = AVCHROMA_LOC_UNSPECIFIED;
    frame->flags               = 0;
}

AVFrame *av_frame_alloc(void)
{
    AVFrame *frame = av_malloc(sizeof(*frame));

    if (!frame)
        return NULL;

    get_frame_defaults(frame);

    return frame;
}

void av_frame_free(AVFrame **frame)
{
    if (!frame || !*frame)
        return;

    av_frame_unref(*frame);
    av_freep(frame);
}

#define ALIGN (HAVE_SIMD_ALIGN_64 ? 64 : 32)

static int get_video_buffer(AVFrame *frame, int align)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int ret, padded_height;
    int plane_padding;
    ptrdiff_t linesizes[4];
    size_t total_size, sizes[4];

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(frame->width, frame->height, 0, NULL)) < 0)
        return ret;

    if (align <= 0)
        align = ALIGN;
    plane_padding = FFMAX(ALIGN, align);

    if (!frame->linesize[0]) {
        for (int i = 1; i <= align; i += i) {
            ret = av_image_fill_linesizes(frame->linesize, frame->format,
                                          FFALIGN(frame->width, i));
            if (ret < 0)
                return ret;
            if (!(frame->linesize[0] & (align-1)))
                break;
        }

        for (int i = 0; i < 4 && frame->linesize[i]; i++)
            frame->linesize[i] = FFALIGN(frame->linesize[i], align);
    }

    for (int i = 0; i < 4; i++)
        linesizes[i] = frame->linesize[i];

    padded_height = FFALIGN(frame->height, 32);
    if ((ret = av_image_fill_plane_sizes(sizes, frame->format,
                                         padded_height, linesizes)) < 0)
        return ret;

    total_size = 4 * plane_padding + 4 * align;
    for (int i = 0; i < 4; i++) {
        if (sizes[i] > SIZE_MAX - total_size)
            return AVERROR(EINVAL);
        total_size += sizes[i];
    }

    frame->buf[0] = av_buffer_alloc(total_size);
    if (!frame->buf[0]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = av_image_fill_pointers(frame->data, frame->format, padded_height,
                                      frame->buf[0]->data, frame->linesize)) < 0)
        goto fail;

    for (int i = 1; i < 4; i++) {
        if (frame->data[i])
            frame->data[i] += i * plane_padding;
        frame->data[i] = (uint8_t *)FFALIGN((uintptr_t)frame->data[i], align);
    }

    frame->extended_data = frame->data;

    return 0;
fail:
    av_frame_unref(frame);
    return ret;
}

static int get_audio_buffer(AVFrame *frame, int align)
{
    int planar   = av_sample_fmt_is_planar(frame->format);
    int channels, planes;
    size_t size;
    int ret;

    channels = frame->ch_layout.nb_channels;
    planes   = planar ? channels : 1;
    if (!frame->linesize[0]) {
        ret = av_samples_get_buffer_size(&frame->linesize[0], channels,
                                         frame->nb_samples, frame->format,
                                         align);
        if (ret < 0)
            return ret;
    }

    if (align <= 0)
        align = ALIGN;

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_calloc(planes,
                                          sizeof(*frame->extended_data));
        frame->extended_buf  = av_calloc(planes - AV_NUM_DATA_POINTERS,
                                          sizeof(*frame->extended_buf));
        if (!frame->extended_data || !frame->extended_buf) {
            av_freep(&frame->extended_data);
            av_freep(&frame->extended_buf);
            return AVERROR(ENOMEM);
        }
        frame->nb_extended_buf = planes - AV_NUM_DATA_POINTERS;
    } else
        frame->extended_data = frame->data;

    if (frame->linesize[0] > SIZE_MAX - align)
        return AVERROR(EINVAL);
    size = frame->linesize[0] + (size_t)align;

    for (int i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_alloc(size);
        if (!frame->buf[i]) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        frame->extended_data[i] = frame->data[i] =
            (uint8_t *)FFALIGN((uintptr_t)frame->buf[i]->data, align);
    }
    for (int i = 0; i < planes - AV_NUM_DATA_POINTERS; i++) {
        frame->extended_buf[i] = av_buffer_alloc(size);
        if (!frame->extended_buf[i]) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        frame->extended_data[i + AV_NUM_DATA_POINTERS] =
            (uint8_t *)FFALIGN((uintptr_t)frame->extended_buf[i]->data, align);
    }
    return 0;

}

int av_frame_get_buffer(AVFrame *frame, int align)
{
    if (frame->format < 0)
        return AVERROR(EINVAL);

    if (frame->width > 0 && frame->height > 0)
        return get_video_buffer(frame, align);
    else if (frame->nb_samples > 0 &&
             (av_channel_layout_check(&frame->ch_layout)))
        return get_audio_buffer(frame, align);

    return AVERROR(EINVAL);
}

static int frame_copy_props(AVFrame *dst, const AVFrame *src, int force_copy)
{
    dst->pict_type              = src->pict_type;
    dst->sample_aspect_ratio    = src->sample_aspect_ratio;
    dst->crop_top               = src->crop_top;
    dst->crop_bottom            = src->crop_bottom;
    dst->crop_left              = src->crop_left;
    dst->crop_right             = src->crop_right;
    dst->pts                    = src->pts;
    dst->duration               = src->duration;
    dst->repeat_pict            = src->repeat_pict;
    dst->sample_rate            = src->sample_rate;
    dst->opaque                 = src->opaque;
    dst->pkt_dts                = src->pkt_dts;
    dst->time_base              = src->time_base;
    dst->quality                = src->quality;
    dst->best_effort_timestamp  = src->best_effort_timestamp;
    dst->flags                  = src->flags;
    dst->decode_error_flags     = src->decode_error_flags;
    dst->color_primaries        = src->color_primaries;
    dst->color_trc              = src->color_trc;
    dst->colorspace             = src->colorspace;
    dst->color_range            = src->color_range;
    dst->chroma_location        = src->chroma_location;

    av_dict_copy(&dst->metadata, src->metadata, 0);

    for (int i = 0; i < src->nb_side_data; i++) {
        const AVFrameSideData *sd_src = src->side_data[i];
        AVFrameSideData *sd_dst;
        if (   sd_src->type == AV_FRAME_DATA_PANSCAN
            && (src->width != dst->width || src->height != dst->height))
            continue;
        if (force_copy) {
            sd_dst = av_frame_new_side_data(dst, sd_src->type,
                                            sd_src->size);
            if (!sd_dst) {
                av_frame_side_data_free(&dst->side_data, &dst->nb_side_data);
                return AVERROR(ENOMEM);
            }
            memcpy(sd_dst->data, sd_src->data, sd_src->size);
        } else {
            AVBufferRef *ref = av_buffer_ref(sd_src->buf);
            sd_dst = av_frame_new_side_data_from_buf(dst, sd_src->type, ref);
            if (!sd_dst) {
                av_buffer_unref(&ref);
                av_frame_side_data_free(&dst->side_data, &dst->nb_side_data);
                return AVERROR(ENOMEM);
            }
        }
        av_dict_copy(&sd_dst->metadata, sd_src->metadata, 0);
    }

    av_refstruct_replace(&dst->private_ref, src->private_ref);
    return av_buffer_replace(&dst->opaque_ref, src->opaque_ref);
}

int av_frame_ref(AVFrame *dst, const AVFrame *src)
{
    int ret = 0;

    av_assert1(dst->width == 0 && dst->height == 0);
    av_assert1(dst->ch_layout.nb_channels == 0 &&
               dst->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);

    dst->format         = src->format;
    dst->width          = src->width;
    dst->height         = src->height;
    dst->nb_samples     = src->nb_samples;

    ret = frame_copy_props(dst, src, 0);
    if (ret < 0)
        goto fail;

    ret = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
    if (ret < 0)
        goto fail;

    /* duplicate the frame data if it's not refcounted */
    if (!src->buf[0]) {
        ret = av_frame_get_buffer(dst, 0);
        if (ret < 0)
            goto fail;

        ret = av_frame_copy(dst, src);
        if (ret < 0)
            goto fail;

        return 0;
    }

    /* ref the buffers */
    for (int i = 0; i < FF_ARRAY_ELEMS(src->buf); i++) {
        if (!src->buf[i])
            continue;
        dst->buf[i] = av_buffer_ref(src->buf[i]);
        if (!dst->buf[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (src->extended_buf) {
        dst->extended_buf = av_calloc(src->nb_extended_buf,
                                      sizeof(*dst->extended_buf));
        if (!dst->extended_buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst->nb_extended_buf = src->nb_extended_buf;

        for (int i = 0; i < src->nb_extended_buf; i++) {
            dst->extended_buf[i] = av_buffer_ref(src->extended_buf[i]);
            if (!dst->extended_buf[i]) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
    }

    if (src->hw_frames_ctx) {
        dst->hw_frames_ctx = av_buffer_ref(src->hw_frames_ctx);
        if (!dst->hw_frames_ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* duplicate extended data */
    if (src->extended_data != src->data) {
        int ch = dst->ch_layout.nb_channels;

        if (ch <= 0 || ch > SIZE_MAX / sizeof(*dst->extended_data)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }

        dst->extended_data = av_memdup(src->extended_data, sizeof(*dst->extended_data) * ch);
        if (!dst->extended_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else
        dst->extended_data = dst->data;

    memcpy(dst->data,     src->data,     sizeof(src->data));
    memcpy(dst->linesize, src->linesize, sizeof(src->linesize));

    return 0;

fail:
    av_frame_unref(dst);
    return ret;
}

int av_frame_replace(AVFrame *dst, const AVFrame *src)
{
    int ret = 0;

    if (dst == src)
        return AVERROR(EINVAL);

    if (!src->buf[0]) {
        av_frame_unref(dst);

        /* duplicate the frame data if it's not refcounted */
        if (   src->data[0] || src->data[1]
            || src->data[2] || src->data[3])
            return av_frame_ref(dst, src);

        ret = frame_copy_props(dst, src, 0);
        if (ret < 0)
            goto fail;
    }

    dst->format         = src->format;
    dst->width          = src->width;
    dst->height         = src->height;
    dst->nb_samples     = src->nb_samples;

    ret = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
    if (ret < 0)
        goto fail;

    av_frame_side_data_free(&dst->side_data, &dst->nb_side_data);
    av_dict_free(&dst->metadata);
    ret = frame_copy_props(dst, src, 0);
    if (ret < 0)
        goto fail;

    /* replace the buffers */
    for (int i = 0; i < FF_ARRAY_ELEMS(src->buf); i++) {
        ret = av_buffer_replace(&dst->buf[i], src->buf[i]);
        if (ret < 0)
            goto fail;
    }

    if (src->extended_buf) {
        if (dst->nb_extended_buf != src->nb_extended_buf) {
            int nb_extended_buf = FFMIN(dst->nb_extended_buf, src->nb_extended_buf);
            void *tmp;

            for (int i = nb_extended_buf; i < dst->nb_extended_buf; i++)
                av_buffer_unref(&dst->extended_buf[i]);

            tmp = av_realloc_array(dst->extended_buf, src->nb_extended_buf,
                                   sizeof(*dst->extended_buf));
            if (!tmp) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            dst->extended_buf = tmp;
            dst->nb_extended_buf = src->nb_extended_buf;

            memset(&dst->extended_buf[nb_extended_buf], 0,
                   (src->nb_extended_buf - nb_extended_buf) * sizeof(*dst->extended_buf));
        }

        for (int i = 0; i < src->nb_extended_buf; i++) {
            ret = av_buffer_replace(&dst->extended_buf[i], src->extended_buf[i]);
            if (ret < 0)
                goto fail;
        }
    } else if (dst->extended_buf) {
        for (int i = 0; i < dst->nb_extended_buf; i++)
            av_buffer_unref(&dst->extended_buf[i]);
        av_freep(&dst->extended_buf);
    }

    ret = av_buffer_replace(&dst->hw_frames_ctx, src->hw_frames_ctx);
    if (ret < 0)
        goto fail;

    if (dst->extended_data != dst->data)
        av_freep(&dst->extended_data);

    if (src->extended_data != src->data) {
        int ch = dst->ch_layout.nb_channels;

        if (ch <= 0 || ch > SIZE_MAX / sizeof(*dst->extended_data)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }

        dst->extended_data = av_memdup(src->extended_data, sizeof(*dst->extended_data) * ch);
        if (!dst->extended_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else
        dst->extended_data = dst->data;

    memcpy(dst->data,     src->data,     sizeof(src->data));
    memcpy(dst->linesize, src->linesize, sizeof(src->linesize));

    return 0;

fail:
    av_frame_unref(dst);
    return ret;
}

AVFrame *av_frame_clone(const AVFrame *src)
{
    AVFrame *ret = av_frame_alloc();

    if (!ret)
        return NULL;

    if (av_frame_ref(ret, src) < 0)
        av_frame_free(&ret);

    return ret;
}

void av_frame_unref(AVFrame *frame)
{
    if (!frame)
        return;

    av_frame_side_data_free(&frame->side_data, &frame->nb_side_data);

    for (int i = 0; i < FF_ARRAY_ELEMS(frame->buf); i++)
        av_buffer_unref(&frame->buf[i]);
    for (int i = 0; i < frame->nb_extended_buf; i++)
        av_buffer_unref(&frame->extended_buf[i]);
    av_freep(&frame->extended_buf);
    av_dict_free(&frame->metadata);

    av_buffer_unref(&frame->hw_frames_ctx);

    av_buffer_unref(&frame->opaque_ref);
    av_refstruct_unref(&frame->private_ref);

    if (frame->extended_data != frame->data)
        av_freep(&frame->extended_data);

    av_channel_layout_uninit(&frame->ch_layout);

    get_frame_defaults(frame);
}

void av_frame_move_ref(AVFrame *dst, AVFrame *src)
{
    av_assert1(dst->width == 0 && dst->height == 0);
    av_assert1(dst->ch_layout.nb_channels == 0 &&
               dst->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);

    *dst = *src;
    if (src->extended_data == src->data)
        dst->extended_data = dst->data;
    get_frame_defaults(src);
}

int av_frame_is_writable(AVFrame *frame)
{
    int ret = 1;

    /* assume non-refcounted frames are not writable */
    if (!frame->buf[0])
        return 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(frame->buf); i++)
        if (frame->buf[i])
            ret &= !!av_buffer_is_writable(frame->buf[i]);
    for (int i = 0; i < frame->nb_extended_buf; i++)
        ret &= !!av_buffer_is_writable(frame->extended_buf[i]);

    return ret;
}

int av_frame_make_writable(AVFrame *frame)
{
    AVFrame tmp;
    int ret;

    if (av_frame_is_writable(frame))
        return 0;

    memset(&tmp, 0, sizeof(tmp));
    tmp.format         = frame->format;
    tmp.width          = frame->width;
    tmp.height         = frame->height;
    tmp.nb_samples     = frame->nb_samples;
    ret = av_channel_layout_copy(&tmp.ch_layout, &frame->ch_layout);
    if (ret < 0) {
        av_frame_unref(&tmp);
        return ret;
    }

    if (frame->hw_frames_ctx)
        ret = av_hwframe_get_buffer(frame->hw_frames_ctx, &tmp, 0);
    else
        ret = av_frame_get_buffer(&tmp, 0);
    if (ret < 0)
        return ret;

    ret = av_frame_copy(&tmp, frame);
    if (ret < 0) {
        av_frame_unref(&tmp);
        return ret;
    }

    ret = av_frame_copy_props(&tmp, frame);
    if (ret < 0) {
        av_frame_unref(&tmp);
        return ret;
    }

    av_frame_unref(frame);

    *frame = tmp;
    if (tmp.data == tmp.extended_data)
        frame->extended_data = frame->data;

    return 0;
}

int av_frame_copy_props(AVFrame *dst, const AVFrame *src)
{
    return frame_copy_props(dst, src, 1);
}

AVBufferRef *av_frame_get_plane_buffer(const AVFrame *frame, int plane)
{
    uintptr_t data;
    int planes;

    if (frame->nb_samples) {
        int channels = frame->ch_layout.nb_channels;
        if (!channels)
            return NULL;
        planes = av_sample_fmt_is_planar(frame->format) ? channels : 1;
    } else
        planes = 4;

    if (plane < 0 || plane >= planes || !frame->extended_data[plane])
        return NULL;
    data = (uintptr_t)frame->extended_data[plane];

    for (int i = 0; i < FF_ARRAY_ELEMS(frame->buf) && frame->buf[i]; i++) {
        AVBufferRef *buf = frame->buf[i];
        uintptr_t buf_begin = (uintptr_t)buf->data;

        if (data >= buf_begin && data < buf_begin + buf->size)
            return buf;
    }
    for (int i = 0; i < frame->nb_extended_buf; i++) {
        AVBufferRef *buf = frame->extended_buf[i];
        uintptr_t buf_begin = (uintptr_t)buf->data;

        if (data >= buf_begin && data < buf_begin + buf->size)
            return buf;
    }
    return NULL;
}

AVFrameSideData *av_frame_new_side_data_from_buf(AVFrame *frame,
                                                 enum AVFrameSideDataType type,
                                                 AVBufferRef *buf)
{
    return
        ff_frame_side_data_add_from_buf(
            &frame->side_data, &frame->nb_side_data, type, buf);
}

AVFrameSideData *av_frame_new_side_data(AVFrame *frame,
                                        enum AVFrameSideDataType type,
                                        size_t size)
{
    AVFrameSideData *ret;
    AVBufferRef *buf = av_buffer_alloc(size);
    ret = av_frame_new_side_data_from_buf(frame, type, buf);
    if (!ret)
        av_buffer_unref(&buf);
    return ret;
}

AVFrameSideData *av_frame_get_side_data(const AVFrame *frame,
                                        enum AVFrameSideDataType type)
{
    return (AVFrameSideData *)av_frame_side_data_get(
        frame->side_data, frame->nb_side_data,
        type
    );
}

static int frame_copy_video(AVFrame *dst, const AVFrame *src)
{
    int planes;

    if (dst->width  < src->width ||
        dst->height < src->height)
        return AVERROR(EINVAL);

    if (src->hw_frames_ctx || dst->hw_frames_ctx)
        return av_hwframe_transfer_data(dst, src, 0);

    planes = av_pix_fmt_count_planes(dst->format);
    for (int i = 0; i < planes; i++)
        if (!dst->data[i] || !src->data[i])
            return AVERROR(EINVAL);

    av_image_copy2(dst->data, dst->linesize,
                   src->data, src->linesize,
                   dst->format, src->width, src->height);

    return 0;
}

static int frame_copy_audio(AVFrame *dst, const AVFrame *src)
{
    int planar   = av_sample_fmt_is_planar(dst->format);
    int channels = dst->ch_layout.nb_channels;
    int planes   = planar ? channels : 1;

    if (dst->nb_samples != src->nb_samples ||
        av_channel_layout_compare(&dst->ch_layout, &src->ch_layout))
        return AVERROR(EINVAL);

    for (int i = 0; i < planes; i++)
        if (!dst->extended_data[i] || !src->extended_data[i])
            return AVERROR(EINVAL);

    av_samples_copy(dst->extended_data, src->extended_data, 0, 0,
                    dst->nb_samples, channels, dst->format);

    return 0;
}

int av_frame_copy(AVFrame *dst, const AVFrame *src)
{
    if (dst->format != src->format || dst->format < 0)
        return AVERROR(EINVAL);

    if (dst->width > 0 && dst->height > 0)
        return frame_copy_video(dst, src);
    else if (dst->nb_samples > 0 &&
             (av_channel_layout_check(&dst->ch_layout)))
        return frame_copy_audio(dst, src);

    return AVERROR(EINVAL);
}

void av_frame_remove_side_data(AVFrame *frame, enum AVFrameSideDataType type)
{
    av_frame_side_data_remove(&frame->side_data, &frame->nb_side_data, type);
}

static int calc_cropping_offsets(size_t offsets[4], const AVFrame *frame,
                                 const AVPixFmtDescriptor *desc)
{
    for (int i = 0; frame->data[i]; i++) {
        const AVComponentDescriptor *comp = NULL;
        int shift_x = (i == 1 || i == 2) ? desc->log2_chroma_w : 0;
        int shift_y = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;

        if (desc->flags & AV_PIX_FMT_FLAG_PAL && i == 1) {
            offsets[i] = 0;
            break;
        }

        /* find any component descriptor for this plane */
        for (int j = 0; j < desc->nb_components; j++) {
            if (desc->comp[j].plane == i) {
                comp = &desc->comp[j];
                break;
            }
        }
        if (!comp)
            return AVERROR_BUG;

        offsets[i] = (frame->crop_top  >> shift_y) * frame->linesize[i] +
                     (frame->crop_left >> shift_x) * comp->step;
    }

    return 0;
}

int av_frame_apply_cropping(AVFrame *frame, int flags)
{
    const AVPixFmtDescriptor *desc;
    size_t offsets[4];
    int ret;

    if (!(frame->width > 0 && frame->height > 0))
        return AVERROR(EINVAL);

    if (frame->crop_left >= INT_MAX - frame->crop_right        ||
        frame->crop_top  >= INT_MAX - frame->crop_bottom       ||
        (frame->crop_left + frame->crop_right) >= frame->width ||
        (frame->crop_top + frame->crop_bottom) >= frame->height)
        return AVERROR(ERANGE);

    desc = av_pix_fmt_desc_get(frame->format);
    if (!desc)
        return AVERROR_BUG;

    /* Apply just the right/bottom cropping for hwaccel formats. Bitstream
     * formats cannot be easily handled here either (and corresponding decoders
     * should not export any cropping anyway), so do the same for those as well.
     * */
    if (desc->flags & (AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_HWACCEL)) {
        frame->width      -= frame->crop_right;
        frame->height     -= frame->crop_bottom;
        frame->crop_right  = 0;
        frame->crop_bottom = 0;
        return 0;
    }

    /* calculate the offsets for each plane */
    ret = calc_cropping_offsets(offsets, frame, desc);
    if (ret < 0)
        return ret;

    /* adjust the offsets to avoid breaking alignment */
    if (!(flags & AV_FRAME_CROP_UNALIGNED)) {
        int log2_crop_align = frame->crop_left ? ff_ctz(frame->crop_left) : INT_MAX;
        int min_log2_align = INT_MAX;

        for (int i = 0; frame->data[i]; i++) {
            int log2_align = offsets[i] ? ff_ctz(offsets[i]) : INT_MAX;
            min_log2_align = FFMIN(log2_align, min_log2_align);
        }

        /* we assume, and it should always be true, that the data alignment is
         * related to the cropping alignment by a constant power-of-2 factor */
        if (log2_crop_align < min_log2_align)
            return AVERROR_BUG;

        if (min_log2_align < 5 && log2_crop_align != INT_MAX) {
            frame->crop_left &= ~((1 << (5 + log2_crop_align - min_log2_align)) - 1);
            ret = calc_cropping_offsets(offsets, frame, desc);
            if (ret < 0)
                return ret;
        }
    }

    for (int i = 0; frame->data[i]; i++)
        frame->data[i] += offsets[i];

    frame->width      -= (frame->crop_left + frame->crop_right);
    frame->height     -= (frame->crop_top  + frame->crop_bottom);
    frame->crop_left   = 0;
    frame->crop_right  = 0;
    frame->crop_top    = 0;
    frame->crop_bottom = 0;

    return 0;
}
