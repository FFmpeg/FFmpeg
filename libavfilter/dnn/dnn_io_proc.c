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
#include "libswscale/swscale.h"

DNNReturnType proc_from_dnn_to_frame(AVFrame *frame, DNNData *output, void *log_ctx)
{
    struct SwsContext *sws_ctx;
    int bytewidth = av_image_get_linesize(frame->format, frame->width, 0);
    if (output->dt != DNN_FLOAT) {
        av_log(log_ctx, AV_LOG_ERROR, "do not support data type rather than DNN_FLOAT\n");
        return DNN_ERROR;
    }

    switch (frame->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        sws_ctx = sws_getContext(frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAYF32,
                                 frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(AV_PIX_FMT_GRAYF32), frame->width * 3, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),   frame->width * 3, frame->height);
            return DNN_ERROR;
        }
        sws_scale(sws_ctx, (const uint8_t *[4]){(const uint8_t *)output->data, 0, 0, 0},
                           (const int[4]){frame->width * 3 * sizeof(float), 0, 0, 0}, 0, frame->height,
                           (uint8_t * const*)frame->data, frame->linesize);
        sws_freeContext(sws_ctx);
        return DNN_SUCCESS;
    case AV_PIX_FMT_GRAYF32:
        av_image_copy_plane(frame->data[0], frame->linesize[0],
                            output->data, bytewidth,
                            bytewidth, frame->height);
        return DNN_SUCCESS;
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
                av_get_pix_fmt_name(AV_PIX_FMT_GRAYF32), frame->width, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),   frame->width, frame->height);
            return DNN_ERROR;
        }
        sws_scale(sws_ctx, (const uint8_t *[4]){(const uint8_t *)output->data, 0, 0, 0},
                           (const int[4]){frame->width * sizeof(float), 0, 0, 0}, 0, frame->height,
                           (uint8_t * const*)frame->data, frame->linesize);
        sws_freeContext(sws_ctx);
        return DNN_SUCCESS;
    default:
        av_log(log_ctx, AV_LOG_ERROR, "do not support frame format %s\n",
               av_get_pix_fmt_name(frame->format));
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}

DNNReturnType proc_from_frame_to_dnn(AVFrame *frame, DNNData *input, void *log_ctx)
{
    struct SwsContext *sws_ctx;
    int bytewidth = av_image_get_linesize(frame->format, frame->width, 0);
    if (input->dt != DNN_FLOAT) {
        av_log(log_ctx, AV_LOG_ERROR, "do not support data type rather than DNN_FLOAT\n");
        return DNN_ERROR;
    }

    switch (frame->format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        sws_ctx = sws_getContext(frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAY8,
                                 frame->width * 3,
                                 frame->height,
                                 AV_PIX_FMT_GRAYF32,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),  frame->width * 3, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAYF32),frame->width * 3, frame->height);
            return DNN_ERROR;
        }
        sws_scale(sws_ctx, (const uint8_t **)frame->data,
                           frame->linesize, 0, frame->height,
                           (uint8_t * const*)(&input->data),
                           (const int [4]){frame->width * 3 * sizeof(float), 0, 0, 0});
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
                                 AV_PIX_FMT_GRAYF32,
                                 0, NULL, NULL, NULL);
        if (!sws_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Impossible to create scale context for the conversion "
                "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                av_get_pix_fmt_name(AV_PIX_FMT_GRAY8),  frame->width, frame->height,
                av_get_pix_fmt_name(AV_PIX_FMT_GRAYF32),frame->width, frame->height);
            return DNN_ERROR;
        }
        sws_scale(sws_ctx, (const uint8_t **)frame->data,
                           frame->linesize, 0, frame->height,
                           (uint8_t * const*)(&input->data),
                           (const int [4]){frame->width * sizeof(float), 0, 0, 0});
        sws_freeContext(sws_ctx);
        break;
    default:
        av_log(log_ctx, AV_LOG_ERROR, "do not support frame format %s\n",
               av_get_pix_fmt_name(frame->format));
        return DNN_ERROR;
    }

    return DNN_SUCCESS;
}
