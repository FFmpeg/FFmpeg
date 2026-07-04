/*
 * Copyright (c) 2020
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

#include "dnn_io_proc.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libswscale/swscale.h"
#include "libavutil/avassert.h"
#include "libavutil/detection_bbox.h"

static int get_datatype_size(DNNDataType dt)
{
    switch (dt)
    {
    case DNN_FLOAT:
        return sizeof(float);
    case DNN_UINT8:
        return sizeof(uint8_t);
    default:
        av_assert0(!"not supported yet.");
        return 1;
    }
}

int ff_proc_from_dnn_to_frame(AVFrame *frame, DNNData *output, void *log_ctx)
{
    struct SwsContext *sws_ctx;
    int ret = 0;
    int linesize[4] = { 0 };
    void **dst_data = NULL;
    void *middle_data = NULL;
    uint8_t *planar_data[4] = { 0 };
    int plane_size = frame->width * frame->height * sizeof(uint8_t);
    enum AVPixelFormat src_fmt = AV_PIX_FMT_NONE;
    int src_datatype_size = get_datatype_size(output->dt);

    int bytewidth = av_image_get_linesize(frame->format, frame->width, 0);
    if (bytewidth < 0) {
        return AVERROR(EINVAL);
    }
    /* scale == 1 and mean == 0 and dt == UINT8: passthrough */
    if (fabsf(output->scale - 1) < 1e-6f && fabsf(output->mean) < 1e-6 && output->dt == DNN_UINT8)
        src_fmt = AV_PIX_FMT_GRAY8;
    /* (scale == 255 or scale == 0) and mean == 0 and dt == FLOAT: normalization */
    else if ((fabsf(output->scale - 255) < 1e-6f || fabsf(output->scale) < 1e-6f) &&
             fabsf(output->mean) < 1e-6 && output->dt == DNN_FLOAT)
        src_fmt = AV_PIX_FMT_GRAYF32;
    else {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_process output data doesn't type: UINT8 "
                                      "scale: %f, mean: %f\n", output->scale, output->mean);
        return AVERROR(ENOSYS);
    }

    dst_data = (void **)frame->data;
    linesize[0] = frame->linesize[0];
    if (output->layout == DL_NCHW) {
        middle_data = av_malloc(plane_size * output->dims[1]);
        if (!middle_data) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        dst_data = &middle_data;
        linesize[0] = frame->width * 3;
    }

    switch (frame->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        sws_ctx = sws_getContext(frame->width * 3,
                                 frame->height,
                                 src_fmt,
                                 frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(src_fmt), frame->width * 3, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),   frame->width * 3, frame->height);
            ret = AVERROR(EINVAL);
            goto err;
        }
        sws_scale(sws_ctx, (const uint8_t *[4]){(const uint8_t *)output->data, 0, 0, 0},
                           (const int[4]){frame->width * 3 * src_datatype_size, 0, 0, 0}, 0, frame->height,
                           (uint8_t * const*)dst_data, linesize);
        sws_freeContext(sws_ctx);
        // convert data from planar to packed
        if (output->layout == DL_NCHW) {
            sws_ctx = sws_getContext(frame->width,
                                     frame->height,
                                     AV_PIX_FMT_GBRP,
                                     frame->width,
                                     frame->height,
                                     frame->format,
                                     0, NULL, NULL, NULL);
            if (!sws_ctx) {
                av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                       "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                       av_get_pix_fmt_name(AV_PIX_FMT_GBRP), frame->width, frame->height,
                       av_get_pix_fmt_name(frame->format),frame->width, frame->height);
                ret = AVERROR(EINVAL);
                goto err;
            }
            if (frame->format == AV_PIX_FMT_RGB24) {
                planar_data[0] = (uint8_t *)middle_data + plane_size;
                planar_data[1] = (uint8_t *)middle_data + plane_size * 2;
                planar_data[2] = (uint8_t *)middle_data;
            } else if (frame->format == AV_PIX_FMT_BGR24) {
                planar_data[0] = (uint8_t *)middle_data + plane_size;
                planar_data[1] = (uint8_t *)middle_data;
                planar_data[2] = (uint8_t *)middle_data + plane_size * 2;
            }
            sws_scale(sws_ctx, (const uint8_t * const *)planar_data,
                      (const int [4]){frame->width * sizeof(uint8_t),
                                      frame->width * sizeof(uint8_t),
                                      frame->width * sizeof(uint8_t), 0},
                      0, frame->height, frame->data, frame->linesize);
            sws_freeContext(sws_ctx);
        }
        break;
    case AV_PIX_FMT_GRAYF32:
        av_image_copy_plane(frame->data[0], frame->linesize[0],
                            output->data, bytewidth,
                            bytewidth, frame->height);
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_NV12:
        sws_ctx = sws_getContext(frame->width,
                                 frame->height,
                                 AV_PIX_FMT_GRAYF32,
                                 frame->width,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(src_fmt), frame->width, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),   frame->width, frame->height);
            ret = AVERROR(EINVAL);
            goto err;
        }
        sws_scale(sws_ctx, (const uint8_t *[4]){(const uint8_t *)output->data, 0, 0, 0},
                           (const int[4]){frame->width * src_datatype_size, 0, 0, 0}, 0, frame->height,
                           (uint8_t * const*)frame->data, frame->linesize);
        sws_freeContext(sws_ctx);
        break;
    default:
        avpriv_report_missing_feature(log_ctx, "%s", av_get_pix_fmt_name(frame->format));
        ret = AVERROR(ENOSYS);
        goto err;
    }

err:
    av_free(middle_data);
    return ret;
}

int ff_proc_from_frame_to_dnn(AVFrame *frame, DNNData *input, void *log_ctx)
{
    struct SwsContext *sws_ctx;
    int ret = 0;
    int linesize[4] = { 0 };
    void **src_data = NULL;
    void *middle_data = NULL;
    uint8_t *planar_data[4] = { 0 };
    int plane_size = frame->width * frame->height * sizeof(uint8_t);
    enum AVPixelFormat dst_fmt = AV_PIX_FMT_NONE;
    int dst_datatype_size = get_datatype_size(input->dt);
    int bytewidth = av_image_get_linesize(frame->format, frame->width, 0);
    if (bytewidth < 0) {
        return AVERROR(EINVAL);
    }
    /* scale == 1 and mean == 0 and dt == UINT8: passthrough */
    if (fabsf(input->scale - 1) < 1e-6f && fabsf(input->mean) < 1e-6 && input->dt == DNN_UINT8)
        dst_fmt = AV_PIX_FMT_GRAY8;
    /* (scale == 255 or scale == 0) and mean == 0 and dt == FLOAT: normalization */
    else if ((fabsf(input->scale - 255) < 1e-6f || fabsf(input->scale) < 1e-6f) &&
             fabsf(input->mean) < 1e-6 && input->dt == DNN_FLOAT)
        dst_fmt = AV_PIX_FMT_GRAYF32;
    else {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_process input data doesn't support type: UINT8 "
                                      "scale: %f, mean: %f\n", input->scale, input->mean);
        return AVERROR(ENOSYS);
    }

    src_data = (void **)frame->data;
    linesize[0] = frame->linesize[0];
    if (input->layout == DL_NCHW) {
        middle_data = av_malloc(plane_size * input->dims[1]);
        if (!middle_data) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        src_data = &middle_data;
        linesize[0] = frame->width * 3;
    }

    switch (frame->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        // convert data from planar to packed
        if (input->layout == DL_NCHW) {
            sws_ctx = sws_getContext(frame->width,
                                     frame->height,
                                     frame->format,
                                     frame->width,
                                     frame->height,
                                     AV_PIX_FMT_GBRP,
                                     0, NULL, NULL, NULL);
            if (!sws_ctx) {
                av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                       "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                       av_get_pix_fmt_name(frame->format), frame->width, frame->height,
                       av_get_pix_fmt_name(AV_PIX_FMT_GBRP),frame->width, frame->height);
                ret = AVERROR(EINVAL);
                goto err;
            }
            if (frame->format == AV_PIX_FMT_RGB24) {
                planar_data[0] = (uint8_t *)middle_data + plane_size;
                planar_data[1] = (uint8_t *)middle_data + plane_size * 2;
                planar_data[2] = (uint8_t *)middle_data;
            } else if (frame->format == AV_PIX_FMT_BGR24) {
                planar_data[0] = (uint8_t *)middle_data + plane_size;
                planar_data[1] = (uint8_t *)middle_data;
                planar_data[2] = (uint8_t *)middle_data + plane_size * 2;
            }
            sws_scale(sws_ctx, (const uint8_t * const *)frame->data,
                      frame->linesize, 0, frame->height, planar_data,
                      (const int [4]){frame->width * sizeof(uint8_t),
                                      frame->width * sizeof(uint8_t),
                                      frame->width * sizeof(uint8_t), 0});
            sws_freeContext(sws_ctx);
        }
        sws_ctx = sws_getContext(frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 frame->width * 3,
                                 frame->height,
                                 dst_fmt,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),  frame->width * 3, frame->height,
                av_get_pix_fmt_name(dst_fmt),frame->width * 3, frame->height);
            ret = AVERROR(EINVAL);
            goto err;
        }
        sws_scale(sws_ctx, (const uint8_t **)src_data,
                           linesize, 0, frame->height,
                           (uint8_t * const [4]){input->data, 0, 0, 0},
                           (const int [4]){frame->width * 3 * dst_datatype_size, 0, 0, 0});
        sws_freeContext(sws_ctx);
        break;
    case AV_PIX_FMT_GRAYF32:
        av_image_copy_plane(input->data, bytewidth,
                            frame->data[0], frame->linesize[0],
                            bytewidth, frame->height);
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_NV12:
        sws_ctx = sws_getContext(frame->width,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 frame->width,
                                 frame->height,
                                 dst_fmt,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),  frame->width, frame->height,
                av_get_pix_fmt_name(dst_fmt),frame->width, frame->height);
            ret = AVERROR(EINVAL);
            goto err;
        }
        sws_scale(sws_ctx, (const uint8_t **)frame->data,
                           frame->linesize, 0, frame->height,
                           (uint8_t * const [4]){input->data, 0, 0, 0},
                           (const int [4]){frame->width * dst_datatype_size, 0, 0, 0});
        sws_freeContext(sws_ctx);
        break;
    default:
        avpriv_report_missing_feature(log_ctx, "%s", av_get_pix_fmt_name(frame->format));
        ret = AVERROR(ENOSYS);
        goto err;
    }
err:
    av_free(middle_data);
    return ret;
}

static enum AVPixelFormat get_pixel_format(DNNData *data)
{
    if (data->dt == DNN_UINT8) {
        switch (data->order) {
        case DCO_BGR:
            return AV_PIX_FMT_BGR24;
        case DCO_RGB:
            return AV_PIX_FMT_RGB24;
        default:
            av_assert0(!"unsupported data pixel format.\n");
            return AV_PIX_FMT_BGR24;
        }
    }

    av_assert0(!"unsupported data type.\n");
    return AV_PIX_FMT_BGR24;
}

int ff_frame_to_dnn_classify(AVFrame *frame, DNNData *input, uint32_t bbox_index, void *log_ctx)
{
    const AVPixFmtDescriptor *desc;
    int offsetx[4], offsety[4];
    uint8_t *bbox_data[4];
    struct SwsContext *sws_ctx;
    int linesizes[4];
    int ret = 0;
    enum AVPixelFormat fmt;
    int left, top, width, height;
    int width_idx, height_idx;
    const AVDetectionBBoxHeader *header;
    const AVDetectionBBox *bbox;
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
    int max_step[4] = { 0 };
    av_assert0(sd);

    /* (scale != 1 and scale != 0) or mean != 0 */
    if ((fabsf(input->scale - 1) > 1e-6f && fabsf(input->scale) > 1e-6f) ||
        fabsf(input->mean) > 1e-6f) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_classify input data doesn't support "
                                      "scale: %f, mean: %f\n", input->scale, input->mean);
        return AVERROR(ENOSYS);
    }

    if (input->layout == DL_NCHW) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_classify input data doesn't support layout: NCHW\n");
        return AVERROR(ENOSYS);
    }

    width_idx = dnn_get_width_idx_by_layout(input->layout);
    height_idx = dnn_get_height_idx_by_layout(input->layout);

    header = (const AVDetectionBBoxHeader *)sd->data;
    bbox = av_get_detection_bbox(header, bbox_index);

    left = bbox->x;
    width = bbox->w;
    top = bbox->y;
    height = bbox->h;

    fmt = get_pixel_format(input);
    sws_ctx = sws_getContext(width, height, frame->format,
                             input->dims[width_idx],
                             input->dims[height_idx], fmt,
                             SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        av_log(log_ctx, AV_LOG_ERROR, "Failed to create scale context for the conversion "
               "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
               av_get_pix_fmt_name(frame->format), width, height,
               av_get_pix_fmt_name(fmt),
               input->dims[width_idx],
               input->dims[height_idx]);
        return AVERROR(EINVAL);
    }

    ret = av_image_fill_linesizes(linesizes, fmt, input->dims[width_idx]);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR, "unable to get linesizes with av_image_fill_linesizes");
        sws_freeContext(sws_ctx);
        return ret;
    }

    desc = av_pix_fmt_desc_get(frame->format);
    offsetx[1] = offsetx[2] = AV_CEIL_RSHIFT(left, desc->log2_chroma_w);
    offsetx[0] = offsetx[3] = left;

    offsety[1] = offsety[2] = AV_CEIL_RSHIFT(top, desc->log2_chroma_h);
    offsety[0] = offsety[3] = top;

    av_image_fill_max_pixsteps(max_step, NULL, desc);
    for (int k = 0; frame->data[k]; k++)
        bbox_data[k] = frame->data[k] + offsety[k] * frame->linesize[k] + offsetx[k] * max_step[k];

    sws_scale(sws_ctx, (const uint8_t *const *)&bbox_data, frame->linesize,
                       0, height,
                       (uint8_t *const [4]){input->data, 0, 0, 0}, linesizes);

    sws_freeContext(sws_ctx);

    return ret;
}

/*
 * Write packed 24bit RGB/BGR samples into the tensor type/layout the model expects.
 */
static void detect_write_tensor(DNNData *input, const uint8_t *src,
                                int src_linesize, int w, int h)
{
    const size_t plane_size = (size_t)w * h;

    if (input->dt == DNN_FLOAT) {
        float *dst = input->data;
        if (input->layout == DL_NCHW) {
            /* FLOAT + NCHW: deinterleave into planes while widening uint8 -> float. */
            for (int c = 0; c < 3; c++)
                for (int y = 0; y < h; y++) {
                    const uint8_t *row = src + (ptrdiff_t)y * src_linesize;
                    for (int x = 0; x < w; x++)
                        dst[c * plane_size + (size_t)y * w + x] = row[x * 3 + c];
                }
        } else {
            /* FLOAT + NHWC : keep it packed, only widen uint8 -> float byte by byte. */
            for (int y = 0; y < h; y++) {
                const uint8_t *row = src + (ptrdiff_t)y * src_linesize;
                for (int x = 0; x < w * 3; x++)
                    dst[(size_t)y * w * 3 + x] = row[x];
            }
        }
    } else {
        /* UINT8 + NCHW: deinterleave into 3 uint8 planes. */
        uint8_t *dst = input->data;
        for (int c = 0; c < 3; c++)
            for (int y = 0; y < h; y++) {
                const uint8_t *row = src + (ptrdiff_t)y * src_linesize;
                for (int x = 0; x < w; x++)
                    dst[c * plane_size + (size_t)y * w + x] = row[x * 3 + c];
            }
    }
}

int ff_frame_to_dnn_detect(AVFrame *frame, DNNData *input, void *log_ctx)
{
    struct SwsContext *sws_ctx = NULL;
    uint8_t *tmp_buf = NULL;
    const uint8_t *src;
    int src_linesize;
    int linesizes[4];
    int ret = 0, width_idx, height_idx, channel_idx;
    int packed_u8, w, h;
    enum AVPixelFormat fmt;

    switch (input->order) {
        case DCO_BGR: fmt = AV_PIX_FMT_BGR24; break;
        case DCO_RGB: fmt = AV_PIX_FMT_RGB24; break;
        default:      fmt = AV_PIX_FMT_NONE;  break;
    }

    /* (scale != 1 and scale != 0) or mean != 0 */
    if ((fabsf(input->scale - 1) > 1e-6f && fabsf(input->scale) > 1e-6f) ||
        fabsf(input->mean) > 1e-6f) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_detect input data doesn't support "
                                      "scale: %f, mean: %f\n", input->scale, input->mean);
        return AVERROR(ENOSYS);
    }

    if (fmt == AV_PIX_FMT_NONE) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_detect input data doesn't support "
                                      "color order: %d\n", input->order);
        return AVERROR(ENOSYS);
    }

    if (input->dt != DNN_UINT8 && input->dt != DNN_FLOAT) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_detect input data doesn't support "
                                      "data type: %d\n", input->dt);
        return AVERROR(ENOSYS);
    }

    width_idx   = dnn_get_width_idx_by_layout(input->layout);
    height_idx  = dnn_get_height_idx_by_layout(input->layout);
    channel_idx = dnn_get_channel_idx_by_layout(input->layout);

    w = input->dims[width_idx];
    h = input->dims[height_idx];
    if (w <= 0 || h <= 0) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_detect input data has invalid "
                                      "dimensions: %dx%d\n", w, h);
        return AVERROR(EINVAL);
    }

    packed_u8 = (input->dt == DNN_UINT8) && (input->layout != DL_NCHW);
    if (!packed_u8 && input->dims[channel_idx] != 3) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_detect requires a 3-channel input, "
                                      "but the model has %d channels\n",
                                      input->dims[channel_idx]);
        return AVERROR(ENOSYS);
    }

    ret = av_image_fill_linesizes(linesizes, fmt, w);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR, "unable to get linesizes with av_image_fill_linesizes");
        return ret;
    }

    if (packed_u8) {
        /* UINT8 + NHWC: sws_scale() writes straight into input->data in one pass. */
        sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
                                 w, h, fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(frame->format), frame->width, frame->height,
                av_get_pix_fmt_name(fmt), w, h);
            return AVERROR(EINVAL);
        }

        sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height,
                           (uint8_t *const [4]){input->data, 0, 0, 0}, linesizes);
        sws_freeContext(sws_ctx);
        return 0;
    }

    if (frame->format == fmt && frame->width == w && frame->height == h) {
        src = frame->data[0];
        src_linesize = frame->linesize[0];
    } else {
        size_t tmp_size;

        if (av_size_mult((size_t)linesizes[0], (size_t)h, &tmp_size) < 0) {
            av_log(log_ctx, AV_LOG_ERROR, "dnn_detect temporary buffer size overflows\n");
            return AVERROR(EINVAL);
        }

        tmp_buf = av_malloc(tmp_size);
        if (!tmp_buf)
            return AVERROR(ENOMEM);

        sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
                                 w, h, fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(frame->format), frame->width, frame->height,
                av_get_pix_fmt_name(fmt), w, h);
            ret = AVERROR(EINVAL);
            goto end;
        }

        sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height,
                           (uint8_t *const [4]){tmp_buf, 0, 0, 0}, linesizes);
        sws_freeContext(sws_ctx);
        sws_ctx = NULL;

        src = tmp_buf;
        src_linesize = linesizes[0];
    }

    detect_write_tensor(input, src, src_linesize, w, h);

end:
    av_freep(&tmp_buf);
    return ret;
}
