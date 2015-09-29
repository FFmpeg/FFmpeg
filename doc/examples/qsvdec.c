/*
 * Copyright (c) 2015 Anton Khirnov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Intel QSV-accelerated H.264 decoding example.
 *
 * @example qsvdec.c
 * This example shows how to do QSV-accelerated H.264 decoding with output
 * frames in the VA-API video surfaces.
 */

#include "config.h"

#include <stdio.h>

#include <mfx/mfxvideo.h>

#include <va/va.h>
#include <va/va_x11.h>
#include <X11/Xlib.h>

#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/qsv.h"

#include "libavutil/error.h"
#include "libavutil/mem.h"

typedef struct DecodeContext {
    mfxSession mfx_session;
    VADisplay va_dpy;

    VASurfaceID *surfaces;
    mfxMemId    *surface_ids;
    int         *surface_used;
    int       nb_surfaces;

    mfxFrameInfo frame_info;
} DecodeContext;

static mfxStatus frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                             mfxFrameAllocResponse *resp)
{
    DecodeContext *decode = pthis;
    int err, i;

    if (decode->surfaces) {
        fprintf(stderr, "Multiple allocation requests.\n");
        return MFX_ERR_MEMORY_ALLOC;
    }
    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)) {
        fprintf(stderr, "Unsupported surface type: %d\n", req->Type);
        return MFX_ERR_UNSUPPORTED;
    }
    if (req->Info.BitDepthLuma != 8 || req->Info.BitDepthChroma != 8 ||
        req->Info.Shift || req->Info.FourCC != MFX_FOURCC_NV12 ||
        req->Info.ChromaFormat != MFX_CHROMAFORMAT_YUV420) {
        fprintf(stderr, "Unsupported surface properties.\n");
        return MFX_ERR_UNSUPPORTED;
    }

    decode->surfaces     = av_malloc_array (req->NumFrameSuggested, sizeof(*decode->surfaces));
    decode->surface_ids  = av_malloc_array (req->NumFrameSuggested, sizeof(*decode->surface_ids));
    decode->surface_used = av_mallocz_array(req->NumFrameSuggested, sizeof(*decode->surface_used));
    if (!decode->surfaces || !decode->surface_ids || !decode->surface_used)
        goto fail;

    err = vaCreateSurfaces(decode->va_dpy, VA_RT_FORMAT_YUV420,
                           req->Info.Width, req->Info.Height,
                           decode->surfaces, req->NumFrameSuggested,
                           NULL, 0);
    if (err != VA_STATUS_SUCCESS) {
        fprintf(stderr, "Error allocating VA surfaces\n");
        goto fail;
    }
    decode->nb_surfaces = req->NumFrameSuggested;

    for (i = 0; i < decode->nb_surfaces; i++)
        decode->surface_ids[i] = &decode->surfaces[i];

    resp->mids           = decode->surface_ids;
    resp->NumFrameActual = decode->nb_surfaces;

    decode->frame_info = req->Info;

    return MFX_ERR_NONE;
fail:
    av_freep(&decode->surfaces);
    av_freep(&decode->surface_ids);
    av_freep(&decode->surface_used);

    return MFX_ERR_MEMORY_ALLOC;
}

static mfxStatus frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    return MFX_ERR_NONE;
}

static mfxStatus frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
    *hdl = mid;
    return MFX_ERR_NONE;
}

static void free_surfaces(DecodeContext *decode)
{
    if (decode->surfaces)
        vaDestroySurfaces(decode->va_dpy, decode->surfaces, decode->nb_surfaces);
    av_freep(&decode->surfaces);
    av_freep(&decode->surface_ids);
    av_freep(&decode->surface_used);
    decode->nb_surfaces = 0;
}

static void free_buffer(void *opaque, uint8_t *data)
{
    int *used = opaque;
    *used = 0;
    av_freep(&data);
}

static int get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    DecodeContext *decode = avctx->opaque;

    mfxFrameSurface1 *surf;
    AVBufferRef *surf_buf;
    int idx;

    for (idx = 0; idx < decode->nb_surfaces; idx++) {
        if (!decode->surface_used[idx])
            break;
    }
    if (idx == decode->nb_surfaces) {
        fprintf(stderr, "No free surfaces\n");
        return AVERROR(ENOMEM);
    }

    surf = av_mallocz(sizeof(*surf));
    if (!surf)
        return AVERROR(ENOMEM);
    surf_buf = av_buffer_create((uint8_t*)surf, sizeof(*surf), free_buffer,
                                &decode->surface_used[idx], AV_BUFFER_FLAG_READONLY);
    if (!surf_buf) {
        av_freep(&surf);
        return AVERROR(ENOMEM);
    }

    surf->Info       = decode->frame_info;
    surf->Data.MemId = &decode->surfaces[idx];

    frame->buf[0]  = surf_buf;
    frame->data[3] = (uint8_t*)surf;

    decode->surface_used[idx] = 1;

    return 0;
}

static int get_format(AVCodecContext *avctx, const enum AVPixelFormat *pix_fmts)
{
    while (*pix_fmts != AV_PIX_FMT_NONE) {
        if (*pix_fmts == AV_PIX_FMT_QSV) {
            if (!avctx->hwaccel_context) {
                DecodeContext *decode = avctx->opaque;
                AVQSVContext *qsv = av_qsv_alloc_context();
                if (!qsv)
                    return AV_PIX_FMT_NONE;

                qsv->session   = decode->mfx_session;
                qsv->iopattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

                avctx->hwaccel_context = qsv;
            }

            return AV_PIX_FMT_QSV;
        }

        pix_fmts++;
    }

    fprintf(stderr, "The QSV pixel format not offered in get_format()\n");

    return AV_PIX_FMT_NONE;
}

static int decode_packet(DecodeContext *decode, AVCodecContext *decoder_ctx,
                         AVFrame *frame, AVPacket *pkt,
                         AVIOContext *output_ctx)
{
    int ret = 0;
    int got_frame = 1;

    while (pkt->size > 0 || (!pkt->data && got_frame)) {
        ret = avcodec_decode_video2(decoder_ctx, frame, &got_frame, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            return ret;
        }

        pkt->data += ret;
        pkt->size -= ret;

        /* A real program would do something useful with the decoded frame here.
         * We just retrieve the raw data and write it to a file, which is rather
         * useless but pedagogic. */
        if (got_frame) {
            mfxFrameSurface1 *surf = (mfxFrameSurface1*)frame->data[3];
            VASurfaceID    surface = *(VASurfaceID*)surf->Data.MemId;

            VAImageFormat img_fmt = {
                .fourcc         = VA_FOURCC_NV12,
                .byte_order     = VA_LSB_FIRST,
                .bits_per_pixel = 8,
                .depth          = 8,
            };

            VAImage img;

            VAStatus err;
            uint8_t *data;
            int i, j;

            img.buf      = VA_INVALID_ID;
            img.image_id = VA_INVALID_ID;

            err = vaCreateImage(decode->va_dpy, &img_fmt,
                                frame->width, frame->height, &img);
            if (err != VA_STATUS_SUCCESS) {
                fprintf(stderr, "Error creating an image: %s\n",
                        vaErrorStr(err));
                ret = AVERROR_UNKNOWN;
                goto fail;
            }

            err = vaGetImage(decode->va_dpy, surface, 0, 0,
                             frame->width, frame->height,
                             img.image_id);
            if (err != VA_STATUS_SUCCESS) {
                fprintf(stderr, "Error getting an image: %s\n",
                        vaErrorStr(err));
                ret = AVERROR_UNKNOWN;
                goto fail;
            }

            err = vaMapBuffer(decode->va_dpy, img.buf, (void**)&data);
            if (err != VA_STATUS_SUCCESS) {
                fprintf(stderr, "Error mapping the image buffer: %s\n",
                        vaErrorStr(err));
                ret = AVERROR_UNKNOWN;
                goto fail;
            }

            for (i = 0; i < img.num_planes; i++)
                for (j = 0; j < (img.height >> (i > 0)); j++)
                    avio_write(output_ctx, data + img.offsets[i] + j * img.pitches[i], img.width);

fail:
            if (img.buf != VA_INVALID_ID)
                vaUnmapBuffer(decode->va_dpy, img.buf);
            if (img.image_id != VA_INVALID_ID)
                vaDestroyImage(decode->va_dpy, img.image_id);
            av_frame_unref(frame);

            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    AVFormatContext *input_ctx = NULL;
    AVStream *video_st = NULL;
    AVCodecContext *decoder_ctx = NULL;
    const AVCodec *decoder;

    AVPacket pkt = { 0 };
    AVFrame *frame = NULL;

    DecodeContext decode = { NULL };

    Display *dpy = NULL;
    int va_ver_major, va_ver_minor;

    mfxIMPL mfx_impl = MFX_IMPL_AUTO_ANY;
    mfxVersion mfx_ver = { { 1, 1 } };

    mfxFrameAllocator frame_allocator = {
        .pthis = &decode,
        .Alloc = frame_alloc,
        .Lock  = frame_lock,
        .Unlock = frame_unlock,
        .GetHDL = frame_get_hdl,
        .Free   = frame_free,
    };

    AVIOContext *output_ctx = NULL;

    int ret, i, err;

    av_register_all();

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    /* open the input file */
    ret = avformat_open_input(&input_ctx, argv[1], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Cannot open input file '%s': ", argv[1]);
        goto finish;
    }

    /* find the first H.264 video stream */
    for (i = 0; i < input_ctx->nb_streams; i++) {
        AVStream *st = input_ctx->streams[i];

        if (st->codec->codec_id == AV_CODEC_ID_H264 && !video_st)
            video_st = st;
        else
            st->discard = AVDISCARD_ALL;
    }
    if (!video_st) {
        fprintf(stderr, "No H.264 video stream in the input file\n");
        goto finish;
    }

    /* initialize VA-API */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open the X display\n");
        goto finish;
    }
    decode.va_dpy = vaGetDisplay(dpy);
    if (!decode.va_dpy) {
        fprintf(stderr, "Cannot open the VA display\n");
        goto finish;
    }

    err = vaInitialize(decode.va_dpy, &va_ver_major, &va_ver_minor);
    if (err != VA_STATUS_SUCCESS) {
        fprintf(stderr, "Cannot initialize VA: %s\n", vaErrorStr(err));
        goto finish;
    }
    fprintf(stderr, "Initialized VA v%d.%d\n", va_ver_major, va_ver_minor);

    /* initialize an MFX session */
    err = MFXInit(mfx_impl, &mfx_ver, &decode.mfx_session);
    if (err != MFX_ERR_NONE) {
        fprintf(stderr, "Error initializing an MFX session\n");
        goto finish;
    }

    MFXVideoCORE_SetHandle(decode.mfx_session, MFX_HANDLE_VA_DISPLAY, decode.va_dpy);
    MFXVideoCORE_SetFrameAllocator(decode.mfx_session, &frame_allocator);

    /* initialize the decoder */
    decoder = avcodec_find_decoder_by_name("h264_qsv");
    if (!decoder) {
        fprintf(stderr, "The QSV decoder is not present in libavcodec\n");
        goto finish;
    }

    decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }
    decoder_ctx->codec_id = AV_CODEC_ID_H264;
    if (video_st->codec->extradata_size) {
        decoder_ctx->extradata = av_mallocz(video_st->codec->extradata_size +
                                            AV_INPUT_BUFFER_PADDING_SIZE);
        if (!decoder_ctx->extradata) {
            ret = AVERROR(ENOMEM);
            goto finish;
        }
        memcpy(decoder_ctx->extradata, video_st->codec->extradata,
               video_st->codec->extradata_size);
        decoder_ctx->extradata_size = video_st->codec->extradata_size;
    }
    decoder_ctx->refcounted_frames = 1;

    decoder_ctx->opaque      = &decode;
    decoder_ctx->get_buffer2 = get_buffer;
    decoder_ctx->get_format  = get_format;

    ret = avcodec_open2(decoder_ctx, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error opening the decoder: ");
        goto finish;
    }

    /* open the output stream */
    ret = avio_open(&output_ctx, argv[2], AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "Error opening the output context: ");
        goto finish;
    }

    frame = av_frame_alloc();
    if (!frame) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    /* actual decoding */
    while (ret >= 0) {
        ret = av_read_frame(input_ctx, &pkt);
        if (ret < 0)
            break;

        if (pkt.stream_index == video_st->index)
            ret = decode_packet(&decode, decoder_ctx, frame, &pkt, output_ctx);

        av_packet_unref(&pkt);
    }

    /* flush the decoder */
    pkt.data = NULL;
    pkt.size = 0;
    ret = decode_packet(&decode, decoder_ctx, frame, &pkt, output_ctx);

finish:
    if (ret < 0) {
        char buf[1024];
        av_strerror(ret, buf, sizeof(buf));
        fprintf(stderr, "%s\n", buf);
    }

    avformat_close_input(&input_ctx);

    av_frame_free(&frame);

    if (decoder_ctx)
        av_freep(&decoder_ctx->hwaccel_context);
    avcodec_free_context(&decoder_ctx);

    free_surfaces(&decode);

    if (decode.mfx_session)
        MFXClose(decode.mfx_session);
    if (decode.va_dpy)
        vaTerminate(decode.va_dpy);
    if (dpy)
        XCloseDisplay(dpy);

    avio_close(output_ctx);

    return ret;
}
