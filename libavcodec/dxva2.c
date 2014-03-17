/*
 * DXVA2 HW acceleration.
 *
 * copyright (c) 2010 Laurent Aimar
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

#include <assert.h>
#include <string.h>

#include "libavutil/log.h"
#include "libavutil/time.h"

#include "avcodec.h"
#include "mpegvideo.h"
#include "dxva2_internal.h"

void *ff_dxva2_get_surface(const AVFrame *frame)
{
    return frame->data[3];
}

unsigned ff_dxva2_get_surface_index(const struct dxva_context *ctx,
                                    const AVFrame *frame)
{
    void *surface = ff_dxva2_get_surface(frame);
    unsigned i;

    for (i = 0; i < ctx->surface_count; i++)
        if (ctx->surface[i] == surface)
            return i;

    assert(0);
    return 0;
}

int ff_dxva2_commit_buffer(AVCodecContext *avctx,
                           struct dxva_context *ctx,
                           DXVA2_DecodeBufferDesc *dsc,
                           unsigned type, const void *data, unsigned size,
                           unsigned mb_count)
{
    void     *dxva_data;
    unsigned dxva_size;
    int      result;
    HRESULT hr;

    hr = IDirectXVideoDecoder_GetBuffer(ctx->decoder, type,
                                        &dxva_data, &dxva_size);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get a buffer for %u: 0x%lx\n",
               type, hr);
        return -1;
    }
    if (size <= dxva_size) {
        memcpy(dxva_data, data, size);

        memset(dsc, 0, sizeof(*dsc));
        dsc->CompressedBufferType = type;
        dsc->DataSize             = size;
        dsc->NumMBsInBuffer       = mb_count;

        result = 0;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Buffer for type %u was too small\n", type);
        result = -1;
    }

    hr = IDirectXVideoDecoder_ReleaseBuffer(ctx->decoder, type);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to release buffer type %u: 0x%lx\n",
               type, hr);
        result = -1;
    }
    return result;
}

int ff_dxva2_common_end_frame(AVCodecContext *avctx, AVFrame *frame,
                              const void *pp, unsigned pp_size,
                              const void *qm, unsigned qm_size,
                              int (*commit_bs_si)(AVCodecContext *,
                                                  DXVA2_DecodeBufferDesc *bs,
                                                  DXVA2_DecodeBufferDesc *slice))
{
    struct dxva_context *ctx = avctx->hwaccel_context;
    unsigned               buffer_count = 0;
    DXVA2_DecodeBufferDesc buffer[4];
    DXVA2_DecodeExecuteParams exec = { 0 };
    int result, runs = 0;
    HRESULT hr;

    do {
        hr = IDirectXVideoDecoder_BeginFrame(ctx->decoder,
                                             ff_dxva2_get_surface(frame),
                                             NULL);
        if (hr == E_PENDING)
            av_usleep(2000);
    } while (hr == E_PENDING && ++runs < 50);

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to begin frame: 0x%lx\n", hr);
        return -1;
    }

    result = ff_dxva2_commit_buffer(avctx, ctx, &buffer[buffer_count],
                                    DXVA2_PictureParametersBufferType,
                                    pp, pp_size, 0);
    if (result) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add picture parameter buffer\n");
        goto end;
    }
    buffer_count++;

    if (qm_size > 0) {
        result = ff_dxva2_commit_buffer(avctx, ctx, &buffer[buffer_count],
                                        DXVA2_InverseQuantizationMatrixBufferType,
                                        qm, qm_size, 0);
        if (result) {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to add inverse quantization matrix buffer\n");
            goto end;
        }
        buffer_count++;
    }

    result = commit_bs_si(avctx,
                          &buffer[buffer_count + 0],
                          &buffer[buffer_count + 1]);
    if (result) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add bitstream or slice control buffer\n");
        goto end;
    }
    buffer_count += 2;

    /* TODO Film Grain when possible */

    assert(buffer_count == 1 + (qm_size > 0) + 2);

    exec.NumCompBuffers      = buffer_count;
    exec.pCompressedBuffers  = buffer;
    exec.pExtensionData      = NULL;
    hr = IDirectXVideoDecoder_Execute(ctx->decoder, &exec);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to execute: 0x%lx\n", hr);
        result = -1;
    }

end:
    hr = IDirectXVideoDecoder_EndFrame(ctx->decoder, NULL);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end frame: 0x%lx\n", hr);
        result = -1;
    }

    return result;
}
