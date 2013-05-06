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
#include "common.h"
#include "cpu.h"
#include "dict.h"
#include "frame.h"
#include "imgutils.h"
#include "mem.h"
#include "samplefmt.h"
#include "hwcontext.h"

#if FF_API_OLD_CHANNEL_LAYOUT
#define CHECK_CHANNELS_CONSISTENCY(frame) \
    av_assert2(!(frame)->channel_layout || \
               (frame)->channels == \
               av_get_channel_layout_nb_channels((frame)->channel_layout))
#endif

#if FF_API_COLORSPACE_NAME
const char *av_get_colorspace_name(enum AVColorSpace val)
{
    static const char * const name[] = {
        [AVCOL_SPC_RGB]       = "GBR",
        [AVCOL_SPC_BT709]     = "bt709",
        [AVCOL_SPC_FCC]       = "fcc",
        [AVCOL_SPC_BT470BG]   = "bt470bg",
        [AVCOL_SPC_SMPTE170M] = "smpte170m",
        [AVCOL_SPC_SMPTE240M] = "smpte240m",
        [AVCOL_SPC_YCOCG]     = "YCgCo",
    };
    if ((unsigned)val >= FF_ARRAY_ELEMS(name))
        return NULL;
    return name[val];
}
#endif
static void get_frame_defaults(AVFrame *frame)
{
    memset(frame, 0, sizeof(*frame));

    frame->pts                   =
    frame->pkt_dts               = AV_NOPTS_VALUE;
    frame->best_effort_timestamp = AV_NOPTS_VALUE;
    frame->pkt_duration        = 0;
    frame->pkt_pos             = -1;
    frame->pkt_size            = -1;
    frame->time_base           = (AVRational){ 0, 1 };
    frame->key_frame           = 1;
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

static void free_side_data(AVFrameSideData **ptr_sd)
{
    AVFrameSideData *sd = *ptr_sd;

    av_buffer_unref(&sd->buf);
    av_dict_free(&sd->metadata);
    av_freep(ptr_sd);
}

static void wipe_side_data(AVFrame *frame)
{
    int i;

    for (i = 0; i < frame->nb_side_data; i++) {
        free_side_data(&frame->side_data[i]);
    }
    frame->nb_side_data = 0;

    av_freep(&frame->side_data);
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

static int get_video_buffer(AVFrame *frame, int align)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int ret, i, padded_height, total_size;
    int plane_padding = FFMAX(16 + 16/*STRIDE_ALIGN*/, align);
    ptrdiff_t linesizes[4];
    size_t sizes[4];

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(frame->width, frame->height, 0, NULL)) < 0)
        return ret;

    if (!frame->linesize[0]) {
        if (align <= 0)
            align = 32; /* STRIDE_ALIGN. Should be av_cpu_max_align() */

        for(i=1; i<=align; i+=i) {
            ret = av_image_fill_linesizes(frame->linesize, frame->format,
                                          FFALIGN(frame->width, i));
            if (ret < 0)
                return ret;
            if (!(frame->linesize[0] & (align-1)))
                break;
        }

        for (i = 0; i < 4 && frame->linesize[i]; i++)
            frame->linesize[i] = FFALIGN(frame->linesize[i], align);
    }

    for (i = 0; i < 4; i++)
        linesizes[i] = frame->linesize[i];

    padded_height = FFALIGN(frame->height, 32);
    if ((ret = av_image_fill_plane_sizes(sizes, frame->format,
                                         padded_height, linesizes)) < 0)
        return ret;

    total_size = 4*plane_padding;
    for (i = 0; i < 4; i++) {
        if (sizes[i] > INT_MAX - total_size)
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

    for (i = 1; i < 4; i++) {
        if (frame->data[i])
            frame->data[i] += i * plane_padding;
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
    int ret, i;

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    if (!frame->ch_layout.nb_channels) {
        if (frame->channel_layout) {
            av_channel_layout_from_mask(&frame->ch_layout, frame->channel_layout);
        } else {
            frame->ch_layout.nb_channels = frame->channels;
            frame->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        }
    }
    frame->channels = frame->ch_layout.nb_channels;
    frame->channel_layout = frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE ?
                            frame->ch_layout.u.mask : 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    channels = frame->ch_layout.nb_channels;
    planes   = planar ? channels : 1;
    if (!frame->linesize[0]) {
        ret = av_samples_get_buffer_size(&frame->linesize[0], channels,
                                         frame->nb_samples, frame->format,
                                         align);
        if (ret < 0)
            return ret;
    }

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

    for (i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_alloc(frame->linesize[0]);
        if (!frame->buf[i]) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        frame->extended_data[i] = frame->data[i] = frame->buf[i]->data;
    }
    for (i = 0; i < planes - AV_NUM_DATA_POINTERS; i++) {
        frame->extended_buf[i] = av_buffer_alloc(frame->linesize[0]);
        if (!frame->extended_buf[i]) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        frame->extended_data[i + AV_NUM_DATA_POINTERS] = frame->extended_buf[i]->data;
    }
    return 0;

}

int av_frame_get_buffer(AVFrame *frame, int align)
{
    if (frame->format < 0)
        return AVERROR(EINVAL);

FF_DISABLE_DEPRECATION_WARNINGS
    if (frame->width > 0 && frame->height > 0)
        return get_video_buffer(frame, align);
    else if (frame->nb_samples > 0 &&
             (av_channel_layout_check(&frame->ch_layout)
#if FF_API_OLD_CHANNEL_LAYOUT
              || frame->channel_layout || frame->channels > 0
#endif
             ))
        return get_audio_buffer(frame, align);
FF_ENABLE_DEPRECATION_WARNINGS

    return AVERROR(EINVAL);
}

static int frame_copy_props(AVFrame *dst, const AVFrame *src, int force_copy)
{
    int ret, i;

    dst->key_frame              = src->key_frame;
    dst->pict_type              = src->pict_type;
    dst->sample_aspect_ratio    = src->sample_aspect_ratio;
    dst->crop_top               = src->crop_top;
    dst->crop_bottom            = src->crop_bottom;
    dst->crop_left              = src->crop_left;
    dst->crop_right             = src->crop_right;
    dst->pts                    = src->pts;
    dst->repeat_pict            = src->repeat_pict;
    dst->interlaced_frame       = src->interlaced_frame;
    dst->top_field_first        = src->top_field_first;
    dst->palette_has_changed    = src->palette_has_changed;
    dst->sample_rate            = src->sample_rate;
    dst->opaque                 = src->opaque;
    dst->pkt_dts                = src->pkt_dts;
    dst->pkt_pos                = src->pkt_pos;
    dst->pkt_size               = src->pkt_size;
    dst->pkt_duration           = src->pkt_duration;
    dst->time_base              = src->time_base;
    dst->reordered_opaque       = src->reordered_opaque;
    dst->quality                = src->quality;
    dst->best_effort_timestamp  = src->best_effort_timestamp;
    dst->coded_picture_number   = src->coded_picture_number;
    dst->display_picture_number = src->display_picture_number;
    dst->flags                  = src->flags;
    dst->decode_error_flags     = src->decode_error_flags;
    dst->color_primaries        = src->color_primaries;
    dst->color_trc              = src->color_trc;
    dst->colorspace             = src->colorspace;
    dst->color_range            = src->color_range;
    dst->chroma_location        = src->chroma_location;

    av_dict_copy(&dst->metadata, src->metadata, 0);

    for (i = 0; i < src->nb_side_data; i++) {
        const AVFrameSideData *sd_src = src->side_data[i];
        AVFrameSideData *sd_dst;
        if (   sd_src->type == AV_FRAME_DATA_PANSCAN
            && (src->width != dst->width || src->height != dst->height))
            continue;
        if (force_copy) {
            sd_dst = av_frame_new_side_data(dst, sd_src->type,
                                            sd_src->size);
            if (!sd_dst) {
                wipe_side_data(dst);
                return AVERROR(ENOMEM);
            }
            memcpy(sd_dst->data, sd_src->data, sd_src->size);
        } else {
            AVBufferRef *ref = av_buffer_ref(sd_src->buf);
            sd_dst = av_frame_new_side_data_from_buf(dst, sd_src->type, ref);
            if (!sd_dst) {
                av_buffer_unref(&ref);
                wipe_side_data(dst);
                return AVERROR(ENOMEM);
            }
        }
        av_dict_copy(&sd_dst->metadata, sd_src->metadata, 0);
    }

    ret = av_buffer_replace(&dst->opaque_ref, src->opaque_ref);
    ret |= av_buffer_replace(&dst->private_ref, src->private_ref);
    return ret;
}

int av_frame_ref(AVFrame *dst, const AVFrame *src)
{
    int i, ret = 0;

    av_assert1(dst->width == 0 && dst->height == 0);
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    av_assert1(dst->channels == 0);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    av_assert1(dst->ch_layout.nb_channels == 0 &&
               dst->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);

    dst->format         = src->format;
    dst->width          = src->width;
    dst->height         = src->height;
    dst->nb_samples     = src->nb_samples;
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    dst->channels       = src->channels;
    dst->channel_layout = src->channel_layout;
    if (!av_channel_layout_check(&src->ch_layout)) {
        if (src->channel_layout)
            av_channel_layout_from_mask(&dst->ch_layout, src->channel_layout);
        else {
            dst->ch_layout.nb_channels = src->channels;
            dst->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        }
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ret = frame_copy_props(dst, src, 0);
    if (ret < 0)
        goto fail;

    // this check is needed only until FF_API_OLD_CHANNEL_LAYOUT is out
    if (av_channel_layout_check(&src->ch_layout)) {
        ret = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
        if (ret < 0)
            goto fail;
    }

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
    for (i = 0; i < FF_ARRAY_ELEMS(src->buf); i++) {
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

        for (i = 0; i < src->nb_extended_buf; i++) {
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

        if (!ch) {
            ret = AVERROR(EINVAL);
            goto fail;
        }

        dst->extended_data = av_malloc_array(sizeof(*dst->extended_data), ch);
        if (!dst->extended_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        memcpy(dst->extended_data, src->extended_data, sizeof(*src->extended_data) * ch);
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
    int i;

    if (!frame)
        return;

    wipe_side_data(frame);

    for (i = 0; i < FF_ARRAY_ELEMS(frame->buf); i++)
        av_buffer_unref(&frame->buf[i]);
    for (i = 0; i < frame->nb_extended_buf; i++)
        av_buffer_unref(&frame->extended_buf[i]);
    av_freep(&frame->extended_buf);
    av_dict_free(&frame->metadata);

    av_buffer_unref(&frame->hw_frames_ctx);

    av_buffer_unref(&frame->opaque_ref);
    av_buffer_unref(&frame->private_ref);

    if (frame->extended_data != frame->data)
        av_freep(&frame->extended_data);

    av_channel_layout_uninit(&frame->ch_layout);

    get_frame_defaults(frame);
}

void av_frame_move_ref(AVFrame *dst, AVFrame *src)
{
    av_assert1(dst->width == 0 && dst->height == 0);
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    av_assert1(dst->channels == 0);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    av_assert1(dst->ch_layout.nb_channels == 0 &&
               dst->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC);

    *dst = *src;
    if (src->extended_data == src->data)
        dst->extended_data = dst->data;
    get_frame_defaults(src);
}

int av_frame_is_writable(AVFrame *frame)
{
    int i, ret = 1;

    /* assume non-refcounted frames are not writable */
    if (!frame->buf[0])
        return 0;

    for (i = 0; i < FF_ARRAY_ELEMS(frame->buf); i++)
        if (frame->buf[i])
            ret &= !!av_buffer_is_writable(frame->buf[i]);
    for (i = 0; i < frame->nb_extended_buf; i++)
        ret &= !!av_buffer_is_writable(frame->extended_buf[i]);

    return ret;
}

int av_frame_make_writable(AVFrame *frame)
{
    AVFrame tmp;
    int ret;

    if (!frame->buf[0])
        return AVERROR(EINVAL);

    if (av_frame_is_writable(frame))
        return 0;

    memset(&tmp, 0, sizeof(tmp));
    tmp.format         = frame->format;
    tmp.width          = frame->width;
    tmp.height         = frame->height;
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    tmp.channels       = frame->channels;
    tmp.channel_layout = frame->channel_layout;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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

AVBufferRef *av_frame_get_plane_buffer(AVFrame *frame, int plane)
{
    uint8_t *data;
    int planes, i;

    if (frame->nb_samples) {
        int channels = frame->ch_layout.nb_channels;

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
        if (!channels) {
            channels = frame->channels;
            CHECK_CHANNELS_CONSISTENCY(frame);
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        if (!channels)
            return NULL;
        planes = av_sample_fmt_is_planar(frame->format) ? channels : 1;
    } else
        planes = 4;

    if (plane < 0 || plane >= planes || !frame->extended_data[plane])
        return NULL;
    data = frame->extended_data[plane];

    for (i = 0; i < FF_ARRAY_ELEMS(frame->buf) && frame->buf[i]; i++) {
        AVBufferRef *buf = frame->buf[i];
        if (data >= buf->data && data < buf->data + buf->size)
            return buf;
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        AVBufferRef *buf = frame->extended_buf[i];
        if (data >= buf->data && data < buf->data + buf->size)
            return buf;
    }
    return NULL;
}

AVFrameSideData *av_frame_new_side_data_from_buf(AVFrame *frame,
                                                 enum AVFrameSideDataType type,
                                                 AVBufferRef *buf)
{
    AVFrameSideData *ret, **tmp;

    if (!buf)
        return NULL;

    if (frame->nb_side_data > INT_MAX / sizeof(*frame->side_data) - 1)
        return NULL;

    tmp = av_realloc(frame->side_data,
                     (frame->nb_side_data + 1) * sizeof(*frame->side_data));
    if (!tmp)
        return NULL;
    frame->side_data = tmp;

    ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->buf = buf;
    ret->data = ret->buf->data;
    ret->size = buf->size;
    ret->type = type;

    frame->side_data[frame->nb_side_data++] = ret;

    return ret;
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
    int i;

    for (i = 0; i < frame->nb_side_data; i++) {
        if (frame->side_data[i]->type == type)
            return frame->side_data[i];
    }
    return NULL;
}

static int frame_copy_video(AVFrame *dst, const AVFrame *src)
{
    const uint8_t *src_data[4];
    int i, planes;

    if (dst->width  < src->width ||
        dst->height < src->height)
        return AVERROR(EINVAL);

    if (src->hw_frames_ctx || dst->hw_frames_ctx)
        return av_hwframe_transfer_data(dst, src, 0);

    planes = av_pix_fmt_count_planes(dst->format);
    for (i = 0; i < planes; i++)
        if (!dst->data[i] || !src->data[i])
            return AVERROR(EINVAL);

    memcpy(src_data, src->data, sizeof(src_data));
    av_image_copy(dst->data, dst->linesize,
                  src_data, src->linesize,
                  dst->format, src->width, src->height);

    return 0;
}

static int frame_copy_audio(AVFrame *dst, const AVFrame *src)
{
    int planar   = av_sample_fmt_is_planar(dst->format);
    int channels = dst->ch_layout.nb_channels;
    int planes   = planar ? channels : 1;
    int i;

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    if (!channels) {
        if (dst->channels       != src->channels ||
            dst->channel_layout != src->channel_layout)
            return AVERROR(EINVAL);
        channels = dst->channels;
        planes = planar ? channels : 1;
        CHECK_CHANNELS_CONSISTENCY(src);
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (dst->nb_samples != src->nb_samples ||
        av_channel_layout_compare(&dst->ch_layout, &src->ch_layout))
        return AVERROR(EINVAL);

    for (i = 0; i < planes; i++)
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

FF_DISABLE_DEPRECATION_WARNINGS
    if (dst->width > 0 && dst->height > 0)
        return frame_copy_video(dst, src);
    else if (dst->nb_samples > 0 &&
             (av_channel_layout_check(&dst->ch_layout)
#if FF_API_OLD_CHANNEL_LAYOUT
              || dst->channel_layout || dst->channels
#endif
            ))
        return frame_copy_audio(dst, src);
FF_ENABLE_DEPRECATION_WARNINGS

    return AVERROR(EINVAL);
}

void av_frame_remove_side_data(AVFrame *frame, enum AVFrameSideDataType type)
{
    int i;

    for (i = frame->nb_side_data - 1; i >= 0; i--) {
        AVFrameSideData *sd = frame->side_data[i];
        if (sd->type == type) {
            free_side_data(&frame->side_data[i]);
            frame->side_data[i] = frame->side_data[frame->nb_side_data - 1];
            frame->nb_side_data--;
        }
    }
}

const char *av_frame_side_data_name(enum AVFrameSideDataType type)
{
    switch(type) {
    case AV_FRAME_DATA_PANSCAN:         return "AVPanScan";
    case AV_FRAME_DATA_A53_CC:          return "ATSC A53 Part 4 Closed Captions";
    case AV_FRAME_DATA_STEREO3D:        return "Stereo 3D";
    case AV_FRAME_DATA_MATRIXENCODING:  return "AVMatrixEncoding";
    case AV_FRAME_DATA_DOWNMIX_INFO:    return "Metadata relevant to a downmix procedure";
    case AV_FRAME_DATA_REPLAYGAIN:      return "AVReplayGain";
    case AV_FRAME_DATA_DISPLAYMATRIX:   return "3x3 displaymatrix";
    case AV_FRAME_DATA_AFD:             return "Active format description";
    case AV_FRAME_DATA_MOTION_VECTORS:  return "Motion vectors";
    case AV_FRAME_DATA_SKIP_SAMPLES:    return "Skip samples";
    case AV_FRAME_DATA_AUDIO_SERVICE_TYPE:          return "Audio service type";
    case AV_FRAME_DATA_MASTERING_DISPLAY_METADATA:  return "Mastering display metadata";
    case AV_FRAME_DATA_CONTENT_LIGHT_LEVEL:         return "Content light level metadata";
    case AV_FRAME_DATA_GOP_TIMECODE:                return "GOP timecode";
    case AV_FRAME_DATA_S12M_TIMECODE:               return "SMPTE 12-1 timecode";
    case AV_FRAME_DATA_SPHERICAL:                   return "Spherical Mapping";
    case AV_FRAME_DATA_ICC_PROFILE:                 return "ICC profile";
    case AV_FRAME_DATA_DYNAMIC_HDR_PLUS: return "HDR Dynamic Metadata SMPTE2094-40 (HDR10+)";
    case AV_FRAME_DATA_DYNAMIC_HDR_VIVID: return "HDR Dynamic Metadata CUVA 005.1 2021 (Vivid)";
    case AV_FRAME_DATA_REGIONS_OF_INTEREST: return "Regions Of Interest";
    case AV_FRAME_DATA_VIDEO_ENC_PARAMS:            return "Video encoding parameters";
    case AV_FRAME_DATA_SEI_UNREGISTERED:            return "H.26[45] User Data Unregistered SEI message";
    case AV_FRAME_DATA_FILM_GRAIN_PARAMS:           return "Film grain parameters";
    case AV_FRAME_DATA_DETECTION_BBOXES:            return "Bounding boxes for object detection and classification";
    case AV_FRAME_DATA_DOVI_RPU_BUFFER:             return "Dolby Vision RPU Data";
    case AV_FRAME_DATA_DOVI_METADATA:               return "Dolby Vision Metadata";
    }
    return NULL;
}

static int calc_cropping_offsets(size_t offsets[4], const AVFrame *frame,
                                 const AVPixFmtDescriptor *desc)
{
    int i, j;

    for (i = 0; frame->data[i]; i++) {
        const AVComponentDescriptor *comp = NULL;
        int shift_x = (i == 1 || i == 2) ? desc->log2_chroma_w : 0;
        int shift_y = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;

        if (desc->flags & AV_PIX_FMT_FLAG_PAL && i == 1) {
            offsets[i] = 0;
            break;
        }

        /* find any component descriptor for this plane */
        for (j = 0; j < desc->nb_components; j++) {
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
    int i;

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
    calc_cropping_offsets(offsets, frame, desc);

    /* adjust the offsets to avoid breaking alignment */
    if (!(flags & AV_FRAME_CROP_UNALIGNED)) {
        int log2_crop_align = frame->crop_left ? ff_ctz(frame->crop_left) : INT_MAX;
        int min_log2_align = INT_MAX;

        for (i = 0; frame->data[i]; i++) {
            int log2_align = offsets[i] ? ff_ctz(offsets[i]) : INT_MAX;
            min_log2_align = FFMIN(log2_align, min_log2_align);
        }

        /* we assume, and it should always be true, that the data alignment is
         * related to the cropping alignment by a constant power-of-2 factor */
        if (log2_crop_align < min_log2_align)
            return AVERROR_BUG;

        if (min_log2_align < 5) {
            frame->crop_left &= ~((1 << (5 + log2_crop_align - min_log2_align)) - 1);
            calc_cropping_offsets(offsets, frame, desc);
        }
    }

    for (i = 0; frame->data[i]; i++)
        frame->data[i] += offsets[i];

    frame->width      -= (frame->crop_left + frame->crop_right);
    frame->height     -= (frame->crop_top  + frame->crop_bottom);
    frame->crop_left   = 0;
    frame->crop_right  = 0;
    frame->crop_top    = 0;
    frame->crop_bottom = 0;

    return 0;
}
