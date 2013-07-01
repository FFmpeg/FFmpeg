/*
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

#include "channel_layout.h"
#include "avassert.h"
#include "buffer.h"
#include "common.h"
#include "dict.h"
#include "frame.h"
#include "imgutils.h"
#include "mem.h"
#include "samplefmt.h"

#define MAKE_ACCESSORS(str, name, type, field) \
    type av_##name##_get_##field(const str *s) { return s->field; } \
    void av_##name##_set_##field(str *s, type v) { s->field = v; }

MAKE_ACCESSORS(AVFrame, frame, int64_t, best_effort_timestamp)
MAKE_ACCESSORS(AVFrame, frame, int64_t, pkt_duration)
MAKE_ACCESSORS(AVFrame, frame, int64_t, pkt_pos)
MAKE_ACCESSORS(AVFrame, frame, int64_t, channel_layout)
MAKE_ACCESSORS(AVFrame, frame, int,     channels)
MAKE_ACCESSORS(AVFrame, frame, int,     sample_rate)
MAKE_ACCESSORS(AVFrame, frame, AVDictionary *, metadata)
MAKE_ACCESSORS(AVFrame, frame, int,     decode_error_flags)
MAKE_ACCESSORS(AVFrame, frame, int,     pkt_size)

#define CHECK_CHANNELS_CONSISTENCY(frame) \
    av_assert2(!(frame)->channel_layout || \
               (frame)->channels == \
               av_get_channel_layout_nb_channels((frame)->channel_layout))

AVDictionary **avpriv_frame_get_metadatap(AVFrame *frame) {return &frame->metadata;};

int av_frame_set_qp_table(AVFrame *f, AVBufferRef *buf, int stride, int qp_type)
{
    av_buffer_unref(&f->qp_table_buf);

    f->qp_table_buf = buf;

    f->qscale_table = buf->data;
    f->qstride      = stride;
    f->qscale_type  = qp_type;

    return 0;
}

int8_t *av_frame_get_qp_table(AVFrame *f, int *stride, int *type)
{
    *stride = f->qstride;
    *type   = f->qscale_type;

    if (!f->qp_table_buf)
        return NULL;

    return f->qp_table_buf->data;
}

static void get_frame_defaults(AVFrame *frame)
{
    if (frame->extended_data != frame->data)
        av_freep(&frame->extended_data);

    memset(frame, 0, sizeof(*frame));

    frame->pts                   =
    frame->pkt_dts               =
    frame->pkt_pts               = AV_NOPTS_VALUE;
    av_frame_set_best_effort_timestamp(frame, AV_NOPTS_VALUE);
    av_frame_set_pkt_duration         (frame, 0);
    av_frame_set_pkt_pos              (frame, -1);
    av_frame_set_pkt_size             (frame, -1);
    frame->key_frame           = 1;
    frame->sample_aspect_ratio = (AVRational){ 0, 1 };
    frame->format              = -1; /* unknown */
    frame->extended_data       = frame->data;
}

AVFrame *av_frame_alloc(void)
{
    AVFrame *frame = av_mallocz(sizeof(*frame));

    if (!frame)
        return NULL;

    frame->extended_data = NULL;
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
    int ret, i;

    if (!desc)
        return AVERROR(EINVAL);

    if ((ret = av_image_check_size(frame->width, frame->height, 0, NULL)) < 0)
        return ret;

    if (!frame->linesize[0]) {
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

    for (i = 0; i < 4 && frame->linesize[i]; i++) {
        int h = FFALIGN(frame->height, 32);
        if (i == 1 || i == 2)
            h = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);

        frame->buf[i] = av_buffer_alloc(frame->linesize[i] * h + 16);
        if (!frame->buf[i])
            goto fail;

        frame->data[i] = frame->buf[i]->data;
    }
    if (desc->flags & AV_PIX_FMT_FLAG_PAL || desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) {
        av_buffer_unref(&frame->buf[1]);
        frame->buf[1] = av_buffer_alloc(1024);
        if (!frame->buf[1])
            goto fail;
        frame->data[1] = frame->buf[1]->data;
    }

    frame->extended_data = frame->data;

    return 0;
fail:
    av_frame_unref(frame);
    return AVERROR(ENOMEM);
}

static int get_audio_buffer(AVFrame *frame, int align)
{
    int channels = frame->channels;
    int planar   = av_sample_fmt_is_planar(frame->format);
    int planes   = planar ? channels : 1;
    int ret, i;

    CHECK_CHANNELS_CONSISTENCY(frame);
    if (!frame->linesize[0]) {
        ret = av_samples_get_buffer_size(&frame->linesize[0], channels,
                                         frame->nb_samples, frame->format,
                                         align);
        if (ret < 0)
            return ret;
    }

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_mallocz(planes *
                                          sizeof(*frame->extended_data));
        frame->extended_buf  = av_mallocz((planes - AV_NUM_DATA_POINTERS) *
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

    if (frame->width > 0 && frame->height > 0)
        return get_video_buffer(frame, align);
    else if (frame->nb_samples > 0 && frame->channel_layout)
        return get_audio_buffer(frame, align);

    return AVERROR(EINVAL);
}

int av_frame_ref(AVFrame *dst, AVFrame *src)
{
    int i, ret = 0;

    dst->format         = src->format;
    dst->width          = src->width;
    dst->height         = src->height;
    dst->channels       = src->channels;
    dst->channel_layout = src->channel_layout;
    dst->nb_samples     = src->nb_samples;

    ret = av_frame_copy_props(dst, src);
    if (ret < 0)
        return ret;

    /* duplicate the frame data if it's not refcounted */
    if (!src->buf[0]) {
        ret = av_frame_get_buffer(dst, 32);
        if (ret < 0)
            return ret;

        if (src->nb_samples) {
            int ch = src->channels;
            CHECK_CHANNELS_CONSISTENCY(src);
            av_samples_copy(dst->extended_data, src->extended_data, 0, 0,
                            dst->nb_samples, ch, dst->format);
        } else {
            av_image_copy(dst->data, dst->linesize, src->data, src->linesize,
                          dst->format, dst->width, dst->height);
        }
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
        dst->extended_buf = av_mallocz(sizeof(*dst->extended_buf) *
                                       src->nb_extended_buf);
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

    /* duplicate extended data */
    if (src->extended_data != src->data) {
        int ch = src->channels;

        if (!ch) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
        CHECK_CHANNELS_CONSISTENCY(src);

        dst->extended_data = av_malloc(sizeof(*dst->extended_data) * ch);
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

AVFrame *av_frame_clone(AVFrame *src)
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

    for (i = 0; i < frame->nb_side_data; i++) {
        av_freep(&frame->side_data[i]->data);
        av_dict_free(&frame->side_data[i]->metadata);
        av_freep(&frame->side_data[i]);
    }
    av_freep(&frame->side_data);

    for (i = 0; i < FF_ARRAY_ELEMS(frame->buf); i++)
        av_buffer_unref(&frame->buf[i]);
    for (i = 0; i < frame->nb_extended_buf; i++)
        av_buffer_unref(&frame->extended_buf[i]);
    av_freep(&frame->extended_buf);
    av_dict_free(&frame->metadata);
    av_buffer_unref(&frame->qp_table_buf);

    get_frame_defaults(frame);
}

void av_frame_move_ref(AVFrame *dst, AVFrame *src)
{
    *dst = *src;
    if (src->extended_data == src->data)
        dst->extended_data = dst->data;
    memset(src, 0, sizeof(*src));
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
    tmp.channels       = frame->channels;
    tmp.channel_layout = frame->channel_layout;
    tmp.nb_samples     = frame->nb_samples;
    ret = av_frame_get_buffer(&tmp, 32);
    if (ret < 0)
        return ret;

    if (tmp.nb_samples) {
        int ch = tmp.channels;
        CHECK_CHANNELS_CONSISTENCY(&tmp);
        av_samples_copy(tmp.extended_data, frame->extended_data, 0, 0,
                        frame->nb_samples, ch, frame->format);
    } else {
        av_image_copy(tmp.data, tmp.linesize, frame->data, frame->linesize,
                      frame->format, frame->width, frame->height);
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
    int i;

    dst->key_frame           = src->key_frame;
    dst->pict_type           = src->pict_type;
    dst->sample_aspect_ratio = src->sample_aspect_ratio;
    dst->pts                 = src->pts;
    dst->repeat_pict         = src->repeat_pict;
    dst->interlaced_frame    = src->interlaced_frame;
    dst->top_field_first     = src->top_field_first;
    dst->palette_has_changed = src->palette_has_changed;
    dst->sample_rate         = src->sample_rate;
    dst->opaque              = src->opaque;
#if FF_API_AVFRAME_LAVC
    dst->type                = src->type;
#endif
    dst->pkt_pts             = src->pkt_pts;
    dst->pkt_dts             = src->pkt_dts;
    dst->pkt_pos             = src->pkt_pos;
    dst->pkt_size            = src->pkt_size;
    dst->pkt_duration        = src->pkt_duration;
    dst->reordered_opaque    = src->reordered_opaque;
    dst->quality             = src->quality;
    dst->best_effort_timestamp = src->best_effort_timestamp;
    dst->coded_picture_number = src->coded_picture_number;
    dst->display_picture_number = src->display_picture_number;
    dst->decode_error_flags  = src->decode_error_flags;

    av_dict_copy(&dst->metadata, src->metadata, 0);

    memcpy(dst->error, src->error, sizeof(dst->error));

    for (i = 0; i < src->nb_side_data; i++) {
        const AVFrameSideData *sd_src = src->side_data[i];
        AVFrameSideData *sd_dst = av_frame_new_side_data(dst, sd_src->type,
                                                         sd_src->size);
        if (!sd_dst) {
            for (i = 0; i < dst->nb_side_data; i++) {
                av_freep(&dst->side_data[i]->data);
                av_freep(&dst->side_data[i]);
                av_dict_free(&dst->side_data[i]->metadata);
            }
            av_freep(&dst->side_data);
            return AVERROR(ENOMEM);
        }
        memcpy(sd_dst->data, sd_src->data, sd_src->size);
        av_dict_copy(&sd_dst->metadata, sd_src->metadata, 0);
    }

    dst->qscale_table = NULL;
    dst->qstride      = 0;
    dst->qscale_type  = 0;
    if (src->qp_table_buf) {
        dst->qp_table_buf = av_buffer_ref(src->qp_table_buf);
        if (dst->qp_table_buf) {
            dst->qscale_table = dst->qp_table_buf->data;
            dst->qstride      = src->qstride;
            dst->qscale_type  = src->qscale_type;
        }
    }

    return 0;
}

AVBufferRef *av_frame_get_plane_buffer(AVFrame *frame, int plane)
{
    uint8_t *data;
    int planes, i;

    if (frame->nb_samples) {
        int channels = frame->channels;
        if (!channels)
            return NULL;
        CHECK_CHANNELS_CONSISTENCY(frame);
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

AVFrameSideData *av_frame_new_side_data(AVFrame *frame,
                                        enum AVFrameSideDataType type,
                                        int size)
{
    AVFrameSideData *ret, **tmp;

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

    ret->data = av_malloc(size);
    if (!ret->data) {
        av_freep(&ret);
        return NULL;
    }

    ret->size = size;
    ret->type = type;

    frame->side_data[frame->nb_side_data++] = ret;

    return ret;
}

AVFrameSideData *av_frame_get_side_data(AVFrame *frame,
                                        enum AVFrameSideDataType type)
{
    int i;

    for (i = 0; i < frame->nb_side_data; i++) {
        if (frame->side_data[i]->type == type)
            return frame->side_data[i];
    }
    return NULL;
}
