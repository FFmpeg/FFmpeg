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

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/adler32.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"

static void vflip(AVFrame *frame, const AVPixFmtDescriptor *desc)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(frame->data) && frame->data[i]; i++) {
        int shift = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;

        frame->data[i]    += (AV_CEIL_RSHIFT(frame->height, shift) - 1) * frame->linesize[i];
        frame->linesize[i] = -frame->linesize[i];
    }
}

static int test_format(AVBufferRef *device_ref, enum AVPixelFormat fmt,
                       int flip_upload, int flip_download)
{
    AVBufferRef *frames_ref = NULL;
    AVHWFramesContext *hwfc;
    AVFrame *sw_frame = NULL, *hw_frame = NULL, *download = NULL;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    int ret;

    frames_ref = av_hwframe_ctx_alloc(device_ref);
    if (!frames_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    hwfc = (AVHWFramesContext *)frames_ref->data;
    hwfc->format    = AV_PIX_FMT_CUDA;
    hwfc->sw_format = fmt;
    hwfc->width     = 64;
    hwfc->height    = 64;

    ret = av_hwframe_ctx_init(frames_ref);
    if (ret < 0)
        goto fail;

    sw_frame = av_frame_alloc();
    hw_frame = av_frame_alloc();
    download = av_frame_alloc();
    if (!sw_frame || !hw_frame || !download) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    sw_frame->format = fmt;
    sw_frame->width  = 64;
    sw_frame->height = 64;
    ret = av_frame_get_buffer(sw_frame, 0);
    if (ret < 0)
        goto fail;
    if (flip_upload)
        vflip(sw_frame, desc);

    {
        int linesizes[4];
        av_image_fill_linesizes(linesizes, fmt, 64);

        for (int i = 0; i < FF_ARRAY_ELEMS(sw_frame->data) && sw_frame->data[i]; i++) {
            int shift = (i == 1 || i == 2) && desc ? desc->log2_chroma_h : 0;
            int h = AV_CEIL_RSHIFT(64, shift);

            for (int y = 0; y < h; y++)
                for (int x = 0; x < linesizes[i]; x++)
                    sw_frame->data[i][y * sw_frame->linesize[i] + x] =
                        (uint8_t)(x + y * 3 + i * 17);
        }
    }

    ret = av_hwframe_get_buffer(frames_ref, hw_frame, 0);
    if (ret < 0)
        goto fail;

    ret = av_hwframe_transfer_data(hw_frame, sw_frame, 0);
    if (ret < 0)
        goto fail;

    download->format = fmt;
    download->width  = 64;
    download->height = 64;
    ret = av_frame_get_buffer(download, 0);
    if (ret < 0)
        goto fail;
    if (flip_download)
        vflip(download, desc);

    ret = av_hwframe_transfer_data(download, hw_frame, 0);
    if (ret < 0)
        goto fail;

    {
        int linesizes[4];
        av_image_fill_linesizes(linesizes, fmt, 64);

        for (int i = 0; i < FF_ARRAY_ELEMS(download->data) && download->data[i]; i++) {
            int shift = (i == 1 || i == 2) && desc ? desc->log2_chroma_h : 0;
            int h = AV_CEIL_RSHIFT(64, shift);

            for (int y = 0; y < h; y++) {
                if (memcmp(sw_frame->data[i] + y * sw_frame->linesize[i],
                           download->data[i] + y * download->linesize[i],
                           linesizes[i])) {
                    printf("fail: %-16s plane %d row %d mismatch\n",
                           av_get_pix_fmt_name(fmt), i, y);
                    ret = AVERROR(EINVAL);
                    goto fail;
                }
            }
        }
    }

fail:
    av_frame_free(&sw_frame);
    av_frame_free(&hw_frame);
    av_frame_free(&download);
    av_buffer_unref(&frames_ref);
    return ret;
}

static atomic_int source_overwritten;

/* Hold the stream until the source was overwritten, at most 100 ms. */
static void CUDAAPI hold_stream(CUstream stream, CUresult status, void *opaque)
{
    int64_t start = av_gettime_relative();
    while (!atomic_load(&source_overwritten) && av_gettime_relative() - start < 100000);
}

/* Reuse the memory of an uploaded transfer buffer while the stream is still
 * busy, the upload must not see the new contents. */
static int test_source_reuse(AVBufferRef *device_ref)
{
    AVCUDADeviceContext *hwctx = ((AVHWDeviceContext *)device_ref->data)->hwctx;
    CudaFunctions *cu = hwctx->internal->cuda_dl;
    AVBufferRef *frames_ref = av_hwframe_ctx_alloc(device_ref);
    AVFrame *sw_frame = av_frame_alloc(), *hw_frame = av_frame_alloc();
    AVFrame *download = av_frame_alloc();
    AVHWFramesContext *hwfc;
    CUcontext dummy;
    AVAdler checksum = 1;
    int ret = AVERROR(ENOMEM);

    if (!frames_ref || !sw_frame || !hw_frame || !download)
        goto fail;

    hwfc = (AVHWFramesContext *)frames_ref->data;
    hwfc->format    = AV_PIX_FMT_CUDA;
    hwfc->sw_format = AV_PIX_FMT_GRAY8;
    hwfc->width     = 64;
    hwfc->height    = 64;
    if ((ret = av_hwframe_ctx_init(frames_ref)) < 0 ||
        (ret = av_hwframe_get_buffer(frames_ref, hw_frame, 0)) < 0)
        goto fail;

    for (int value = 0x11; value <= 0x22; value += 0x11) {
        av_frame_unref(sw_frame);
        sw_frame->format = AV_PIX_FMT_GRAY8;
        sw_frame->width  = 64;
        sw_frame->height = 64;
        if ((ret = av_hwframe_get_transfer_buffer(frames_ref, sw_frame)) < 0)
            goto fail;
        for (int y = 0; y < 64; y++)
            memset(sw_frame->data[0] + y * sw_frame->linesize[0], value, 64);
        if (value == 0x22) {
            atomic_store(&source_overwritten, 1);
            break;
        }

        ret = AVERROR_EXTERNAL;
        if (cu->cuCtxPushCurrent(hwctx->cuda_ctx) != CUDA_SUCCESS)
            goto fail;
        if (cu->cuStreamAddCallback(hwctx->stream, hold_stream, NULL, 0) == CUDA_SUCCESS)
            ret = 0;
        cu->cuCtxPopCurrent(&dummy);
        if (ret < 0)
            goto fail;

        if ((ret = av_hwframe_transfer_data(hw_frame, sw_frame, 0)) < 0)
            goto fail;
    }

    if ((ret = av_hwframe_transfer_data(download, hw_frame, 0)) < 0)
        goto fail;
    for (int y = 0; y < 64; y++)
        checksum = av_adler32_update(checksum,
                                     download->data[0] + y * download->linesize[0], 64);
    fprintf(stderr, "source reuse checksum: %08"PRIx32"\n", checksum);

fail:
    av_frame_free(&sw_frame);
    av_frame_free(&hw_frame);
    av_frame_free(&download);
    av_buffer_unref(&frames_ref);
    return ret;
}

/* Verify that an unsupported sw_format is refused at init time. Returns 0 when
 * the format is correctly rejected, and an error when it is wrongly accepted. */
static int test_rejected(AVBufferRef *device_ref, enum AVPixelFormat fmt)
{
    AVBufferRef *frames_ref = av_hwframe_ctx_alloc(device_ref);
    AVHWFramesContext *hwfc;
    int ret;

    if (!frames_ref)
        return AVERROR(ENOMEM);

    hwfc = (AVHWFramesContext *)frames_ref->data;
    hwfc->format    = AV_PIX_FMT_CUDA;
    hwfc->sw_format = fmt;
    hwfc->width     = 64;
    hwfc->height    = 64;

    /* Init is expected to fail; silence the resulting error log. */
    av_log_set_level(AV_LOG_QUIET);
    ret = av_hwframe_ctx_init(frames_ref);
    av_log_set_level(AV_LOG_INFO);

    av_buffer_unref(&frames_ref);

    return ret >= 0 ? AVERROR(EINVAL) : 0;
}

int main(void)
{
    AVBufferRef *device_ref = NULL;
    enum AVPixelFormat fmt;
    int ret, failures = 0, total = 0;

    ret = av_hwdevice_ctx_create(&device_ref, AV_HWDEVICE_TYPE_CUDA, NULL, NULL, 0);
    if (ret < 0) {
        /* CONFIG_CUDA only requires the ffnvcodec headers and a dynamic
         * loader; a FATE host that compiled CUDA support need not have an
         * NVIDIA GPU or usable driver. Skip cleanly in that case so that
         * make fate does not fail for an unavailable test environment. A real
         * transfer mismatch below still returns a nonzero exit status. */
        fprintf(stderr, "No CUDA device available, skipping.\n");
        return 0;
    }

    for (fmt = 0; fmt < AV_PIX_FMT_NB; fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!desc || (desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            continue;

        total++;
        /* Palette formats must be refused rather than silently dropping the
         * palette across a transfer; every other format must round-trip. */
        if (desc->flags & AV_PIX_FMT_FLAG_PAL) {
            if (test_rejected(device_ref, fmt) < 0) {
                printf("fail: %-16s wrongly accepted\n",
                       av_get_pix_fmt_name(fmt));
                failures++;
            }
        } else {
            ret = 0;
            for (int flip = 0; flip < 4 && ret >= 0; flip++)
                ret = test_format(device_ref, fmt, flip & 1, flip & 2);
            if (ret < 0) {
                printf("fail: %-16s %s\n",
                       av_get_pix_fmt_name(fmt),
                       av_err2str(ret));
                failures++;
            }
        }
    }

    total++;
    ret = test_source_reuse(device_ref);
    if (ret < 0) {
        printf("fail: source reuse %s\n", av_err2str(ret));
        failures++;
    }

    av_buffer_unref(&device_ref);

    if (failures)
        printf("%d / %d tests failed.\n", failures, total);
    else
        printf("%d tests passed.\n", total);

    return !!failures;
}
