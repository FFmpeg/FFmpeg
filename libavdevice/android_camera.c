/*
 * Android camera input device
 *
 * Copyright (C) 2017 Felix Matouschek
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

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/display.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixfmt.h"
#include "libavutil/threadmessage.h"
#include "libavutil/time.h"

/* This image format is available on all Android devices
 * supporting the Camera2 API */
#define IMAGE_FORMAT_ANDROID AIMAGE_FORMAT_YUV_420_888

#define MAX_BUF_COUNT 2
#define VIDEO_STREAM_INDEX 0
#define VIDEO_TIMEBASE_ANDROID 1000000000

#define RETURN_CASE(x) case x: return AV_STRINGIFY(x);
#define RETURN_DEFAULT(x) default: return AV_STRINGIFY(x);

typedef struct AndroidCameraCtx {
    const AVClass *class;

    int requested_width;
    int requested_height;
    AVRational framerate;
    int camera_index;
    int input_queue_size;

    uint8_t lens_facing;
    int32_t sensor_orientation;
    int width;
    int height;
    int32_t framerate_range[2];
    int image_format;

    ACameraManager *camera_mgr;
    char *camera_id;
    ACameraMetadata *camera_metadata;
    ACameraDevice *camera_dev;
    ACameraDevice_StateCallbacks camera_state_callbacks;
    AImageReader *image_reader;
    AImageReader_ImageListener image_listener;
    ANativeWindow *image_reader_window;
    ACaptureSessionOutputContainer *capture_session_output_container;
    ACaptureSessionOutput *capture_session_output;
    ACameraOutputTarget *camera_output_target;
    ACaptureRequest *capture_request;
    ACameraCaptureSession_stateCallbacks capture_session_state_callbacks;
    ACameraCaptureSession *capture_session;

    AVThreadMessageQueue *input_queue;
    atomic_int exit;
    atomic_int got_image_format;
} AndroidCameraCtx;

static const char *camera_status_string(camera_status_t val)
{
    switch(val) {
        RETURN_CASE(ACAMERA_OK)
        RETURN_CASE(ACAMERA_ERROR_UNKNOWN)
        RETURN_CASE(ACAMERA_ERROR_INVALID_PARAMETER)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DISCONNECTED)
        RETURN_CASE(ACAMERA_ERROR_NOT_ENOUGH_MEMORY)
        RETURN_CASE(ACAMERA_ERROR_METADATA_NOT_FOUND)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DEVICE)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_SERVICE)
        RETURN_CASE(ACAMERA_ERROR_SESSION_CLOSED)
        RETURN_CASE(ACAMERA_ERROR_INVALID_OPERATION)
        RETURN_CASE(ACAMERA_ERROR_STREAM_CONFIGURE_FAIL)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_IN_USE)
        RETURN_CASE(ACAMERA_ERROR_MAX_CAMERA_IN_USE)
        RETURN_CASE(ACAMERA_ERROR_CAMERA_DISABLED)
        RETURN_CASE(ACAMERA_ERROR_PERMISSION_DENIED)
        RETURN_DEFAULT(ACAMERA_ERROR_UNKNOWN)
    }
}

static const char *media_status_string(media_status_t val)
{
    switch(val) {
        RETURN_CASE(AMEDIA_OK)
        RETURN_CASE(AMEDIA_ERROR_UNKNOWN)
        RETURN_CASE(AMEDIA_ERROR_MALFORMED)
        RETURN_CASE(AMEDIA_ERROR_UNSUPPORTED)
        RETURN_CASE(AMEDIA_ERROR_INVALID_OBJECT)
        RETURN_CASE(AMEDIA_ERROR_INVALID_PARAMETER)
        RETURN_CASE(AMEDIA_ERROR_INVALID_OPERATION)
        RETURN_CASE(AMEDIA_DRM_NOT_PROVISIONED)
        RETURN_CASE(AMEDIA_DRM_RESOURCE_BUSY)
        RETURN_CASE(AMEDIA_DRM_DEVICE_REVOKED)
        RETURN_CASE(AMEDIA_DRM_SHORT_BUFFER)
        RETURN_CASE(AMEDIA_DRM_SESSION_NOT_OPENED)
        RETURN_CASE(AMEDIA_DRM_TAMPER_DETECTED)
        RETURN_CASE(AMEDIA_DRM_VERIFY_FAILED)
        RETURN_CASE(AMEDIA_DRM_NEED_KEY)
        RETURN_CASE(AMEDIA_DRM_LICENSE_EXPIRED)
        RETURN_CASE(AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)
        RETURN_CASE(AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED)
        RETURN_CASE(AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE)
        RETURN_CASE(AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE)
        RETURN_CASE(AMEDIA_IMGREADER_IMAGE_NOT_LOCKED)
        RETURN_DEFAULT(AMEDIA_ERROR_UNKNOWN)
    }
}

static const char *error_state_callback_string(int val)
{
    switch(val) {
        RETURN_CASE(ERROR_CAMERA_IN_USE)
        RETURN_CASE(ERROR_MAX_CAMERAS_IN_USE)
        RETURN_CASE(ERROR_CAMERA_DISABLED)
        RETURN_CASE(ERROR_CAMERA_DEVICE)
        RETURN_CASE(ERROR_CAMERA_SERVICE)
        default:
            return "ERROR_CAMERA_UNKNOWN";
    }
}

static void camera_dev_disconnected(void *context, ACameraDevice *device)
{
    AVFormatContext *avctx = context;
    AndroidCameraCtx *ctx = avctx->priv_data;
    atomic_store(&ctx->exit, 1);
    av_log(avctx, AV_LOG_ERROR, "Camera with id %s disconnected.\n",
           ACameraDevice_getId(device));
}

static void camera_dev_error(void *context, ACameraDevice *device, int error)
{
    AVFormatContext *avctx = context;
    AndroidCameraCtx *ctx = avctx->priv_data;
    atomic_store(&ctx->exit, 1);
    av_log(avctx, AV_LOG_ERROR, "Error %s on camera with id %s.\n",
           error_state_callback_string(error), ACameraDevice_getId(device));
}

static int open_camera(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    camera_status_t ret;
    ACameraIdList *camera_ids;

    ret = ACameraManager_getCameraIdList(ctx->camera_mgr, &camera_ids);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get camera id list, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    if (ctx->camera_index < camera_ids->numCameras) {
        ctx->camera_id = av_strdup(camera_ids->cameraIds[ctx->camera_index]);
        if (!ctx->camera_id) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate memory for camera_id.\n");
            return AVERROR(ENOMEM);
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "No camera with index %d available.\n",
               ctx->camera_index);
        return AVERROR(ENXIO);
    }

    ACameraManager_deleteCameraIdList(camera_ids);

    ret = ACameraManager_getCameraCharacteristics(ctx->camera_mgr,
            ctx->camera_id, &ctx->camera_metadata);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get metadata for camera with id %s, error: %s.\n",
               ctx->camera_id, camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ctx->camera_state_callbacks.context = avctx;
    ctx->camera_state_callbacks.onDisconnected = camera_dev_disconnected;
    ctx->camera_state_callbacks.onError = camera_dev_error;

    ret = ACameraManager_openCamera(ctx->camera_mgr, ctx->camera_id,
                                    &ctx->camera_state_callbacks, &ctx->camera_dev);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open camera with id %s, error: %s.\n",
               ctx->camera_id, camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static void get_sensor_orientation(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    ACameraMetadata_const_entry lens_facing;
    ACameraMetadata_const_entry sensor_orientation;

    ACameraMetadata_getConstEntry(ctx->camera_metadata,
                                  ACAMERA_LENS_FACING, &lens_facing);
    ACameraMetadata_getConstEntry(ctx->camera_metadata,
                                  ACAMERA_SENSOR_ORIENTATION, &sensor_orientation);

    ctx->lens_facing = lens_facing.data.u8[0];
    ctx->sensor_orientation = sensor_orientation.data.i32[0];
}

static void match_video_size(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    ACameraMetadata_const_entry available_configs;
    int found = 0;

    ACameraMetadata_getConstEntry(ctx->camera_metadata,
                                  ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                                  &available_configs);

    for (int i = 0; i < available_configs.count; i++) {
        int32_t input = available_configs.data.i32[i * 4 + 3];
        int32_t format = available_configs.data.i32[i * 4 + 0];

        if (input) {
            continue;
        }

        if (format == IMAGE_FORMAT_ANDROID) {
            int32_t width = available_configs.data.i32[i * 4 + 1];
            int32_t height = available_configs.data.i32[i * 4 + 2];

            //Same ratio
            if ((ctx->requested_width == width && ctx->requested_height == height) ||
                    (ctx->requested_width == height && ctx->requested_height == width)) {
                ctx->width = width;
                ctx->height = height;
                found = 1;
                break;
            }
        }
    }

    if (!found || ctx->width == 0 || ctx->height == 0) {
        ctx->width = available_configs.data.i32[1];
        ctx->height = available_configs.data.i32[2];

        av_log(avctx, AV_LOG_WARNING,
               "Requested video_size %dx%d not available, falling back to %dx%d\n",
               ctx->requested_width, ctx->requested_height, ctx->width, ctx->height);
    }

    return;
}

static void match_framerate(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    ACameraMetadata_const_entry available_framerates;
    int found = 0;
    int current_best_match = -1;
    int requested_framerate = av_q2d(ctx->framerate);

    ACameraMetadata_getConstEntry(ctx->camera_metadata,
                                  ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                                  &available_framerates);

    for (int i = 0; i < available_framerates.count; i++) {
        int32_t min = available_framerates.data.i32[i * 2 + 0];
        int32_t max = available_framerates.data.i32[i * 2 + 1];

        if (requested_framerate == max) {
            if (min == max) {
                ctx->framerate_range[0] = min;
                ctx->framerate_range[1] = max;
                found = 1;
                break;
            } else if (current_best_match >= 0) {
                int32_t current_best_match_min = available_framerates.data.i32[current_best_match * 2 + 0];
                if (min > current_best_match_min) {
                    current_best_match = i;
                }
            } else {
                current_best_match = i;
            }
        }
    }

    if (!found) {
        if (current_best_match >= 0) {
            ctx->framerate_range[0] = available_framerates.data.i32[current_best_match * 2 + 0];
            ctx->framerate_range[1] = available_framerates.data.i32[current_best_match * 2 + 1];

        } else {
            ctx->framerate_range[0] = available_framerates.data.i32[0];
            ctx->framerate_range[1] = available_framerates.data.i32[1];
        }

        av_log(avctx, AV_LOG_WARNING,
               "Requested framerate %d not available, falling back to min: %d and max: %d fps\n",
               requested_framerate, ctx->framerate_range[0], ctx->framerate_range[1]);
    }

    return;
}

static int get_image_format(AVFormatContext *avctx, AImage *image)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    int32_t image_pixelstrides[2];
    uint8_t *image_plane_data[2];
    int plane_data_length[2];

    for (int i = 0; i < 2; i++) {
        AImage_getPlanePixelStride(image, i + 1, &image_pixelstrides[i]);
        AImage_getPlaneData(image, i + 1, &image_plane_data[i], &plane_data_length[i]);
    }

    if (image_pixelstrides[0] != image_pixelstrides[1]) {
        av_log(avctx, AV_LOG_ERROR,
               "Pixel strides of U and V plane should have been the same.\n");
        return AVERROR_EXTERNAL;
    }

    switch (image_pixelstrides[0]) {
        case 1:
            ctx->image_format = AV_PIX_FMT_YUV420P;
            break;
        case 2:
            if (image_plane_data[0] < image_plane_data[1]) {
                ctx->image_format = AV_PIX_FMT_NV12;
            } else {
                ctx->image_format = AV_PIX_FMT_NV21;
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "Unknown pixel stride %d of U and V plane, cannot determine camera image format.\n",
                   image_pixelstrides[0]);
            return AVERROR(ENOSYS);
    }

    return 0;
}

static void image_available(void *context, AImageReader *reader)
{
    AVFormatContext *avctx = context;
    AndroidCameraCtx *ctx = avctx->priv_data;
    media_status_t media_status;
    int ret = 0;

    AImage *image;
    int64_t image_timestamp;
    int32_t image_linestrides[4];
    uint8_t *image_plane_data[4];
    int plane_data_length[4];

    AVPacket pkt;
    int pkt_buffer_size = 0;

    media_status = AImageReader_acquireLatestImage(reader, &image);
    if (media_status != AMEDIA_OK) {
        if (media_status == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
            av_log(avctx, AV_LOG_WARNING,
                   "An image reader frame was discarded");
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to acquire latest image from image reader, error: %s.\n",
                   media_status_string(media_status));
            ret = AVERROR_EXTERNAL;
        }
        goto error;
    }

    // Silently drop frames when exit is set
    if (atomic_load(&ctx->exit)) {
        goto error;
    }

    // Determine actual image format
    if (!atomic_load(&ctx->got_image_format)) {
        ret = get_image_format(avctx, image);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Could not get image format of camera.\n");
            goto error;
        } else {
            atomic_store(&ctx->got_image_format, 1);
        }
    }

    pkt_buffer_size = av_image_get_buffer_size(ctx->image_format, ctx->width, ctx->height, 32);
    AImage_getTimestamp(image, &image_timestamp);

    AImage_getPlaneRowStride(image, 0, &image_linestrides[0]);
    AImage_getPlaneData(image, 0, &image_plane_data[0], &plane_data_length[0]);

    switch (ctx->image_format) {
        case AV_PIX_FMT_YUV420P:
            AImage_getPlaneRowStride(image, 1, &image_linestrides[1]);
            AImage_getPlaneData(image, 1, &image_plane_data[1], &plane_data_length[1]);
            AImage_getPlaneRowStride(image, 2, &image_linestrides[2]);
            AImage_getPlaneData(image, 2, &image_plane_data[2], &plane_data_length[2]);
            break;
        case AV_PIX_FMT_NV12:
            AImage_getPlaneRowStride(image, 1, &image_linestrides[1]);
            AImage_getPlaneData(image, 1, &image_plane_data[1], &plane_data_length[1]);
            break;
        case AV_PIX_FMT_NV21:
            AImage_getPlaneRowStride(image, 2, &image_linestrides[1]);
            AImage_getPlaneData(image, 2, &image_plane_data[1], &plane_data_length[1]);
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported camera image format.\n");
            ret = AVERROR(ENOSYS);
            goto error;
    }

    ret = av_new_packet(&pkt, pkt_buffer_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create new av packet, error: %s.\n", av_err2str(ret));
        goto error;
    }

    pkt.stream_index = VIDEO_STREAM_INDEX;
    pkt.pts = image_timestamp;
    av_image_copy_to_buffer(pkt.data, pkt_buffer_size,
                            (const uint8_t * const *) image_plane_data,
                            image_linestrides, ctx->image_format,
                            ctx->width, ctx->height, 32);

    ret = av_thread_message_queue_send(ctx->input_queue, &pkt, AV_THREAD_MESSAGE_NONBLOCK);

error:
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error while processing new image, error: %s.\n", av_err2str(ret));
            av_thread_message_queue_set_err_recv(ctx->input_queue, ret);
            atomic_store(&ctx->exit, 1);
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "Input queue was full, dropping frame, consider raising the input_queue_size option (current value: %d)\n",
                   ctx->input_queue_size);
        }
        if (pkt_buffer_size) {
            av_packet_unref(&pkt);
        }
    }

    AImage_delete(image);

    return;
}

static int create_image_reader(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    media_status_t ret;

    ret = AImageReader_new(ctx->width, ctx->height, IMAGE_FORMAT_ANDROID,
                           MAX_BUF_COUNT, &ctx->image_reader);
    if (ret != AMEDIA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create image reader, error: %s.\n", media_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ctx->image_listener.context = avctx;
    ctx->image_listener.onImageAvailable = image_available;

    ret = AImageReader_setImageListener(ctx->image_reader, &ctx->image_listener);
    if (ret != AMEDIA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to set image listener on image reader, error: %s.\n",
               media_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = AImageReader_getWindow(ctx->image_reader, &ctx->image_reader_window);
    if (ret != AMEDIA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not get image reader window, error: %s.\n",
               media_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static void capture_session_closed(void *context, ACameraCaptureSession *session)
{
    av_log(context, AV_LOG_INFO, "Android camera capture session was closed.\n");
}

static void capture_session_ready(void *context, ACameraCaptureSession *session)
{
    av_log(context, AV_LOG_INFO, "Android camera capture session is ready.\n");
}

static void capture_session_active(void *context, ACameraCaptureSession *session)
{
    av_log(context, AV_LOG_INFO, "Android camera capture session is active.\n");
}

static int create_capture_session(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    camera_status_t ret;

    ret = ACaptureSessionOutputContainer_create(&ctx->capture_session_output_container);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create capture session output container, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ANativeWindow_acquire(ctx->image_reader_window);

    ret = ACaptureSessionOutput_create(ctx->image_reader_window, &ctx->capture_session_output);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create capture session container, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACaptureSessionOutputContainer_add(ctx->capture_session_output_container,
                                             ctx->capture_session_output);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add output to output container, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACameraOutputTarget_create(ctx->image_reader_window, &ctx->camera_output_target);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create camera output target, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACameraDevice_createCaptureRequest(ctx->camera_dev, TEMPLATE_RECORD, &ctx->capture_request);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create capture request, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACaptureRequest_setEntry_i32(ctx->capture_request, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,
                                       2, ctx->framerate_range);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to set target fps range in capture request, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACaptureRequest_addTarget(ctx->capture_request, ctx->camera_output_target);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add capture request capture request, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ctx->capture_session_state_callbacks.context = avctx;
    ctx->capture_session_state_callbacks.onClosed = capture_session_closed;
    ctx->capture_session_state_callbacks.onReady = capture_session_ready;
    ctx->capture_session_state_callbacks.onActive = capture_session_active;

    ret = ACameraDevice_createCaptureSession(ctx->camera_dev, ctx->capture_session_output_container,
                                             &ctx->capture_session_state_callbacks, &ctx->capture_session);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create capture session, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    ret = ACameraCaptureSession_setRepeatingRequest(ctx->capture_session, NULL, 1, &ctx->capture_request, NULL);
    if (ret != ACAMERA_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to set repeating request on capture session, error: %s.\n",
               camera_status_string(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int wait_for_image_format(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;

    while (!atomic_load(&ctx->got_image_format) && !atomic_load(&ctx->exit)) {
        //Wait until first frame arrived and actual image format was determined
        usleep(1000);
    }

    return atomic_load(&ctx->got_image_format);
}

static int add_display_matrix(AVFormatContext *avctx, AVStream *st)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    AVPacketSideData *side_data;
    int32_t display_matrix[9];

    av_display_rotation_set(display_matrix, ctx->sensor_orientation);

    if (ctx->lens_facing == ACAMERA_LENS_FACING_FRONT) {
        av_display_matrix_flip(display_matrix, 1, 0);
    }

    side_data = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                        &st->codecpar->nb_coded_side_data,
                                        AV_PKT_DATA_DISPLAYMATRIX,
                                        sizeof(display_matrix), 0);

    if (!side_data) {
        return AVERROR(ENOMEM);
    }

    memcpy(side_data->data, display_matrix, sizeof(display_matrix));

    return 0;
}

static int add_video_stream(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    AVStream *st;
    AVCodecParameters *codecpar;

    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    st->id = VIDEO_STREAM_INDEX;
    st->avg_frame_rate = (AVRational) { ctx->framerate_range[1], 1 };
    st->r_frame_rate = (AVRational) { ctx->framerate_range[1], 1 };

    if (!wait_for_image_format(avctx)) {
        return AVERROR_EXTERNAL;
    }

    codecpar = st->codecpar;
    codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    codecpar->format = ctx->image_format;
    codecpar->width = ctx->width;
    codecpar->height = ctx->height;

    avpriv_set_pts_info(st, 64, 1, VIDEO_TIMEBASE_ANDROID);

    return add_display_matrix(avctx, st);
}

static int android_camera_read_close(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;

    atomic_store(&ctx->exit, 1);

    if (ctx->capture_session) {
        ACameraCaptureSession_stopRepeating(ctx->capture_session);
        // Following warning is emitted, after capture session closed callback is received:
        // ACameraCaptureSession: Device is closed but session 0 is not notified
        // Seems to be a bug in Android, we can ignore this
        ACameraCaptureSession_close(ctx->capture_session);
        ctx->capture_session = NULL;
    }

    if (ctx->capture_request) {
        ACaptureRequest_removeTarget(ctx->capture_request, ctx->camera_output_target);
        ACaptureRequest_free(ctx->capture_request);
        ctx->capture_request = NULL;
    }

    if (ctx->camera_output_target) {
        ACameraOutputTarget_free(ctx->camera_output_target);
        ctx->camera_output_target = NULL;
    }

    if (ctx->capture_session_output) {
        ACaptureSessionOutputContainer_remove(ctx->capture_session_output_container,
                ctx->capture_session_output);
        ACaptureSessionOutput_free(ctx->capture_session_output);
        ctx->capture_session_output = NULL;
    }

    if (ctx->image_reader_window) {
        ANativeWindow_release(ctx->image_reader_window);
        ctx->image_reader_window = NULL;
    }

    if (ctx->capture_session_output_container) {
        ACaptureSessionOutputContainer_free(ctx->capture_session_output_container);
        ctx->capture_session_output_container = NULL;
    }

    if (ctx->camera_dev) {
        ACameraDevice_close(ctx->camera_dev);
        ctx->camera_dev = NULL;
    }

    if (ctx->image_reader) {
        AImageReader_delete(ctx->image_reader);
        ctx->image_reader = NULL;
    }

    if (ctx->camera_metadata) {
        ACameraMetadata_free(ctx->camera_metadata);
        ctx->camera_metadata = NULL;
    }

    av_freep(&ctx->camera_id);

    if (ctx->camera_mgr) {
        ACameraManager_delete(ctx->camera_mgr);
        ctx->camera_mgr = NULL;
    }

    if (ctx->input_queue) {
        AVPacket pkt;
        av_thread_message_queue_set_err_send(ctx->input_queue, AVERROR_EOF);
        while (av_thread_message_queue_recv(ctx->input_queue, &pkt, AV_THREAD_MESSAGE_NONBLOCK) >= 0) {
            av_packet_unref(&pkt);
        }
        av_thread_message_queue_free(&ctx->input_queue);
    }

    return 0;
}

static int android_camera_read_header(AVFormatContext *avctx)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    int ret;

    atomic_init(&ctx->got_image_format, 0);
    atomic_init(&ctx->exit, 0);

    ret = av_thread_message_queue_alloc(&ctx->input_queue, ctx->input_queue_size, sizeof(AVPacket));
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate input queue, error: %s.\n", av_err2str(ret));
        goto error;
    }

    ctx->camera_mgr = ACameraManager_create();
    if (!ctx->camera_mgr) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create Android camera manager.\n");
        ret = AVERROR_EXTERNAL;
        goto error;
    }

    ret = open_camera(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open camera.\n");
        goto error;
    }

    get_sensor_orientation(avctx);
    match_video_size(avctx);
    match_framerate(avctx);

    ret = create_image_reader(avctx);
    if (ret < 0) {
        goto error;
    }

    ret = create_capture_session(avctx);
    if (ret < 0) {
        goto error;
    }

    ret = add_video_stream(avctx);

error:
    if (ret < 0) {
        android_camera_read_close(avctx);
        av_log(avctx, AV_LOG_ERROR, "Failed to open android_camera.\n");
    }

    return ret;
}

static int android_camera_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    AndroidCameraCtx *ctx = avctx->priv_data;
    int ret;

    if (!atomic_load(&ctx->exit)) {
        ret = av_thread_message_queue_recv(ctx->input_queue, pkt,
                avctx->flags & AVFMT_FLAG_NONBLOCK ? AV_THREAD_MESSAGE_NONBLOCK : 0);
    } else {
        ret = AVERROR_EOF;
    }

    if (ret < 0) {
        return ret;
    } else {
        return pkt->size;
    }
}

#define OFFSET(x) offsetof(AndroidCameraCtx, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "video_size", "set video size given as a string such as 640x480 or hd720", OFFSET(requested_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "framerate", "set video frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "30"}, 0, INT_MAX, DEC },
    { "camera_index", "set index of camera to use", OFFSET(camera_index), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "input_queue_size", "set maximum number of frames to buffer", OFFSET(input_queue_size), AV_OPT_TYPE_INT, {.i64 = 5}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass android_camera_class = {
    .class_name = "android_camera indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_android_camera_demuxer = {
    .p.name         = "android_camera",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Android camera input device"),
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &android_camera_class,
    .priv_data_size = sizeof(AndroidCameraCtx),
    .read_header    = android_camera_read_header,
    .read_packet    = android_camera_read_packet,
    .read_close     = android_camera_read_close,
};
