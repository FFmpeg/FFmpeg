/*
 * DXVA2 HW acceleration.
 *
 * copyright (c) 2010 Laurent Aimar
 *
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

#include <assert.h>
#include <string.h>

#include "libavutil/log.h"
#include "libavutil/time.h"

#include "avcodec.h"
#include "dxva2_internal.h"

void *ff_dxva2_get_surface(const AVFrame *frame)
{
    return frame->data[3];
}

unsigned ff_dxva2_get_surface_index(const AVCodecContext *avctx,
                                    const AVDXVAContext *ctx,
                                    const AVFrame *frame)
{
    void *surface = ff_dxva2_get_surface(frame);
    unsigned i;

    for (i = 0; i < DXVA_CONTEXT_COUNT(avctx, ctx); i++)
        if (DXVA_CONTEXT_SURFACE(avctx, ctx, i) == surface)
            return i;

    assert(0);
    return 0;
}

int ff_dxva2_commit_buffer(AVCodecContext *avctx,
                           AVDXVAContext *ctx,
                           DECODER_BUFFER_DESC *dsc,
                           unsigned type, const void *data, unsigned size,
                           unsigned mb_count)
{
    void     *dxva_data;
    unsigned dxva_size;
    int      result;
    HRESULT hr;

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
        hr = ID3D11VideoContext_GetDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context,
                                                 D3D11VA_CONTEXT(ctx)->decoder,
                                                 type,
                                                 &dxva_size, &dxva_data);
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        hr = IDirectXVideoDecoder_GetBuffer(DXVA2_CONTEXT(ctx)->decoder, type,
                                            &dxva_data, &dxva_size);
#endif
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get a buffer for %u: 0x%lx\n",
               type, hr);
        return -1;
    }
    if (size <= dxva_size) {
        memcpy(dxva_data, data, size);

#if CONFIG_D3D11VA
        if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
            D3D11_VIDEO_DECODER_BUFFER_DESC *dsc11 = dsc;
            memset(dsc11, 0, sizeof(*dsc11));
            dsc11->BufferType           = type;
            dsc11->DataSize             = size;
            dsc11->NumMBsInBuffer       = mb_count;
        }
#endif
#if CONFIG_DXVA2
        if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
            DXVA2_DecodeBufferDesc *dsc2 = dsc;
            memset(dsc2, 0, sizeof(*dsc2));
            dsc2->CompressedBufferType = type;
            dsc2->DataSize             = size;
            dsc2->NumMBsInBuffer       = mb_count;
        }
#endif

        result = 0;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Buffer for type %u was too small\n", type);
        result = -1;
    }

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
        hr = ID3D11VideoContext_ReleaseDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder, type);
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        hr = IDirectXVideoDecoder_ReleaseBuffer(DXVA2_CONTEXT(ctx)->decoder, type);
#endif
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
                                                  DECODER_BUFFER_DESC *bs,
                                                  DECODER_BUFFER_DESC *slice))
{
    AVDXVAContext *ctx = avctx->hwaccel_context;
    unsigned               buffer_count = 0;
#if CONFIG_D3D11VA
    D3D11_VIDEO_DECODER_BUFFER_DESC buffer11[4];
#endif
#if CONFIG_DXVA2
    DXVA2_DecodeBufferDesc          buffer2[4];
#endif
    DECODER_BUFFER_DESC             *buffer,*buffer_slice;
    int result, runs = 0;
    HRESULT hr;
    unsigned type;

    do {
#if CONFIG_D3D11VA
        if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
            if (D3D11VA_CONTEXT(ctx)->context_mutex != INVALID_HANDLE_VALUE)
                WaitForSingleObjectEx(D3D11VA_CONTEXT(ctx)->context_mutex, INFINITE, FALSE);
            hr = ID3D11VideoContext_DecoderBeginFrame(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder,
                                                      ff_dxva2_get_surface(frame),
                                                      0, NULL);
        }
#endif
#if CONFIG_DXVA2
        if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
            hr = IDirectXVideoDecoder_BeginFrame(DXVA2_CONTEXT(ctx)->decoder,
                                                 ff_dxva2_get_surface(frame),
                                                 NULL);
#endif
        if (hr != E_PENDING || ++runs > 50)
            break;
#if CONFIG_D3D11VA
        if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
            if (D3D11VA_CONTEXT(ctx)->context_mutex != INVALID_HANDLE_VALUE)
                ReleaseMutex(D3D11VA_CONTEXT(ctx)->context_mutex);
#endif
        av_usleep(2000);
    } while(1);

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to begin frame: 0x%lx\n", hr);
#if CONFIG_D3D11VA
        if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
            if (D3D11VA_CONTEXT(ctx)->context_mutex != INVALID_HANDLE_VALUE)
                ReleaseMutex(D3D11VA_CONTEXT(ctx)->context_mutex);
#endif
        return -1;
    }

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        buffer = &buffer11[buffer_count];
        type = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        buffer = &buffer2[buffer_count];
        type = DXVA2_PictureParametersBufferType;
    }
#endif
    result = ff_dxva2_commit_buffer(avctx, ctx, buffer,
                                    type,
                                    pp, pp_size, 0);
    if (result) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add picture parameter buffer\n");
        goto end;
    }
    buffer_count++;

    if (qm_size > 0) {
#if CONFIG_D3D11VA
        if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
            buffer = &buffer11[buffer_count];
            type = D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
        }
#endif
#if CONFIG_DXVA2
        if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
            buffer = &buffer2[buffer_count];
            type = DXVA2_InverseQuantizationMatrixBufferType;
        }
#endif
        result = ff_dxva2_commit_buffer(avctx, ctx, buffer,
                                        type,
                                        qm, qm_size, 0);
        if (result) {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to add inverse quantization matrix buffer\n");
            goto end;
        }
        buffer_count++;
    }

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        buffer       = &buffer11[buffer_count + 0];
        buffer_slice = &buffer11[buffer_count + 1];
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        buffer       = &buffer2[buffer_count + 0];
        buffer_slice = &buffer2[buffer_count + 1];
    }
#endif

    result = commit_bs_si(avctx,
                          buffer,
                          buffer_slice);
    if (result) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to add bitstream or slice control buffer\n");
        goto end;
    }
    buffer_count += 2;

    /* TODO Film Grain when possible */

    assert(buffer_count == 1 + (qm_size > 0) + 2);

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
        hr = ID3D11VideoContext_SubmitDecoderBuffers(D3D11VA_CONTEXT(ctx)->video_context,
                                                     D3D11VA_CONTEXT(ctx)->decoder,
                                                     buffer_count, buffer11);
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        DXVA2_DecodeExecuteParams exec = {
            .NumCompBuffers     = buffer_count,
            .pCompressedBuffers = buffer2,
            .pExtensionData     = NULL,
        };
        hr = IDirectXVideoDecoder_Execute(DXVA2_CONTEXT(ctx)->decoder, &exec);
    }
#endif
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to execute: 0x%lx\n", hr);
        result = -1;
    }

end:
#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        hr = ID3D11VideoContext_DecoderEndFrame(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder);
        if (D3D11VA_CONTEXT(ctx)->context_mutex != INVALID_HANDLE_VALUE)
            ReleaseMutex(D3D11VA_CONTEXT(ctx)->context_mutex);
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        hr = IDirectXVideoDecoder_EndFrame(DXVA2_CONTEXT(ctx)->decoder, NULL);
#endif
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end frame: 0x%lx\n", hr);
        result = -1;
    }

    return result;
}
