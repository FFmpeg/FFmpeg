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

#include "dxva2_internal.h"

void *ff_dxva2_get_surface(const Picture *picture)
{
    return picture->f.data[3];
}

unsigned ff_dxva2_get_surface_index(const struct dxva_context *ctx,
                                    const Picture *picture)
{
    void *surface = ff_dxva2_get_surface(picture);
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

    if (FAILED(IDirectXVideoDecoder_GetBuffer(ctx->decoder, type,
                                              &dxva_data, &dxva_size))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get a buffer for %d\n", type);
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
        av_log(avctx, AV_LOG_ERROR, "Buffer for type %d was too small\n", type);
        result = -1;
    }
    if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(ctx->decoder, type))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to release buffer type %d\n", type);
        result = -1;
    }
    return result;
}

int ff_dxva2_common_end_frame(AVCodecContext *avctx, MpegEncContext *s,
                              const void *pp, unsigned pp_size,
                              const void *qm, unsigned qm_size,
                              int (*commit_bs_si)(AVCodecContext *,
                                                  DXVA2_DecodeBufferDesc *bs,
                                                  DXVA2_DecodeBufferDesc *slice))
{
    struct dxva_context *ctx = avctx->hwaccel_context;
    unsigned               buffer_count = 0;
    DXVA2_DecodeBufferDesc buffer[4];
    DXVA2_DecodeExecuteParams exec;
    int      result;

    if (FAILED(IDirectXVideoDecoder_BeginFrame(ctx->decoder,
                                               ff_dxva2_get_surface(s->current_picture_ptr),
                                               NULL))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to begin frame\n");
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

    memset(&exec, 0, sizeof(exec));
    exec.NumCompBuffers      = buffer_count;
    exec.pCompressedBuffers  = buffer;
    exec.pExtensionData      = NULL;
    if (FAILED(IDirectXVideoDecoder_Execute(ctx->decoder, &exec))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to execute\n");
        result = -1;
    }

end:
    if (FAILED(IDirectXVideoDecoder_EndFrame(ctx->decoder, NULL))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end frame\n");
        result = -1;
    }

    if (!result)
        ff_draw_horiz_band(s, 0, s->avctx->height);
    return result;
}
