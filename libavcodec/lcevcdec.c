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

#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/refstruct.h"

#include "cbs.h"
#include "cbs_lcevc.h"
#include "decode.h"
#include "lcevc_parse.h"
#include "lcevcdec.h"
#include "lcevctab.h"

static LCEVC_ColorFormat map_format(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        return LCEVC_I420_8;
    case AV_PIX_FMT_YUV420P10:
        return LCEVC_I420_10_LE;
    case AV_PIX_FMT_YUV420P12:
        return LCEVC_I420_12_LE;
    case AV_PIX_FMT_YUV422P:
        return LCEVC_I422_8;
    case AV_PIX_FMT_YUV422P10:
        return LCEVC_I422_10_LE;
    case AV_PIX_FMT_YUV422P12:
        return LCEVC_I422_12_LE;
    case AV_PIX_FMT_YUV444P:
        return LCEVC_I444_8;
    case AV_PIX_FMT_YUV444P10:
        return LCEVC_I444_10_LE;
    case AV_PIX_FMT_YUV444P12:
        return LCEVC_I444_12_LE;
    case AV_PIX_FMT_NV12:
        return LCEVC_NV12_8;
    case AV_PIX_FMT_NV21:
        return LCEVC_NV21_8;
    case AV_PIX_FMT_GRAY8:
        return LCEVC_GRAY_8;
    case AV_PIX_FMT_GRAY10LE:
        return LCEVC_GRAY_10_LE;
    case AV_PIX_FMT_GRAY12LE:
        return LCEVC_GRAY_12_LE;
    }

    return LCEVC_ColorFormat_Unknown;
}

static int alloc_base_frame(void *logctx, FFLCEVCContext *lcevc,
                            const AVFrame *frame, LCEVC_PictureHandle *picture)
{
    LCEVC_PictureDesc desc;
    LCEVC_ColorFormat fmt = map_format(frame->format);
    LCEVC_PicturePlaneDesc planes[AV_VIDEO_MAX_PLANES] = { 0 };
    int width = frame->width - frame->crop_left - frame->crop_right;
    int height = frame->height - frame->crop_top - frame->crop_bottom;
    LCEVC_ReturnCode res;

    res = LCEVC_DefaultPictureDesc(&desc, fmt, width, height);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    desc.cropTop    = frame->crop_top;
    desc.cropBottom = frame->crop_bottom;
    desc.cropLeft   = frame->crop_left;
    desc.cropRight  = frame->crop_right;
    desc.sampleAspectRatioNum  = frame->sample_aspect_ratio.num;
    desc.sampleAspectRatioDen  = frame->sample_aspect_ratio.den;

    for (int i = 0; i < AV_VIDEO_MAX_PLANES; i++) {
        planes[i].firstSample = frame->data[i];
        planes[i].rowByteStride = frame->linesize[i];
    }

    /* Allocate LCEVC Picture */
    res = LCEVC_AllocPictureExternal(lcevc->decoder, &desc, NULL, planes, picture);
    if (res != LCEVC_Success) {
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int alloc_enhanced_frame(void *logctx, FFLCEVCFrame *frame_ctx,
                                LCEVC_PictureHandle *picture)
{
    FFLCEVCContext *lcevc = frame_ctx->lcevc;
    LCEVC_PictureDesc desc ;
    LCEVC_ColorFormat fmt = map_format(frame_ctx->frame->format);
    LCEVC_PicturePlaneDesc planes[4] = { 0 };
    LCEVC_ReturnCode res;

    res = LCEVC_DefaultPictureDesc(&desc, fmt, frame_ctx->frame->width, frame_ctx->frame->height);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    /* Set plane description */
    for (int i = 0; i < 4; i++) {
        planes[i].firstSample = frame_ctx->frame->data[i];
        planes[i].rowByteStride = frame_ctx->frame->linesize[i];
    }

    /* Allocate LCEVC Picture */
    res = LCEVC_AllocPictureExternal(lcevc->decoder, &desc, NULL, planes, picture);
    if (res != LCEVC_Success) {
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int lcevc_send_frame(void *logctx, FFLCEVCFrame *frame_ctx, const AVFrame *in)
{
    FFLCEVCContext *lcevc = frame_ctx->lcevc;
    LCEVC_ColorFormat fmt = map_format(in->format);
    const AVFrameSideData *sd = av_frame_get_side_data(in, AV_FRAME_DATA_LCEVC);
    AVFrame *opaque;
    LCEVC_PictureHandle picture;
    LCEVC_ReturnCode res;
    int ret = 0;

    if (!sd || fmt == LCEVC_ColorFormat_Unknown)
        return 1;

    res = LCEVC_SendDecoderEnhancementData(lcevc->decoder, in->pts, sd->data, sd->size);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    ret = alloc_base_frame(logctx, lcevc, in, &picture);
    if (ret < 0)
        return ret;

    opaque = av_frame_clone(in);
    if (!opaque) {
        LCEVC_FreePicture(lcevc->decoder, picture);
        return AVERROR(ENOMEM);
    }

    res = LCEVC_SetPictureUserData(lcevc->decoder, picture, opaque);
    if (res != LCEVC_Success) {
        LCEVC_FreePicture(lcevc->decoder, picture);
        av_frame_free(&opaque);
        return AVERROR_EXTERNAL;
    }

    res = LCEVC_SendDecoderBase(lcevc->decoder, in->pts, picture, -1, opaque);
    if (res != LCEVC_Success) {
        LCEVC_FreePicture(lcevc->decoder, picture);
        av_frame_free(&opaque);
        return AVERROR_EXTERNAL;
    }

    memset(&picture, 0, sizeof(picture));
    ret = alloc_enhanced_frame(logctx, frame_ctx, &picture);
    if (ret < 0)
        return ret;

    res = LCEVC_SendDecoderPicture(lcevc->decoder, picture);
    if (res != LCEVC_Success) {
        LCEVC_FreePicture(lcevc->decoder, picture);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int generate_output(void *logctx, FFLCEVCFrame *frame_ctx, AVFrame *out)
{
    FFLCEVCContext *lcevc = frame_ctx->lcevc;
    LCEVC_PictureDesc desc;
    LCEVC_DecodeInformation info;
    LCEVC_PictureHandle picture;
    LCEVC_ReturnCode res;

    res = LCEVC_ReceiveDecoderPicture(lcevc->decoder, &picture, &info);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    res = LCEVC_GetPictureDesc(lcevc->decoder, picture, &desc);
    if (res != LCEVC_Success) {
        LCEVC_FreePicture(lcevc->decoder, picture);
        return AVERROR_EXTERNAL;
    }

    av_frame_unref(out);
    av_frame_copy_props(frame_ctx->frame, (AVFrame *)info.baseUserData);
    av_frame_move_ref(out, frame_ctx->frame);

    out->crop_top = desc.cropTop;
    out->crop_bottom = desc.cropBottom;
    out->crop_left = desc.cropLeft;
    out->crop_right = desc.cropRight;
    out->sample_aspect_ratio.num = desc.sampleAspectRatioNum;
    out->sample_aspect_ratio.den = desc.sampleAspectRatioDen;
    out->width = desc.width + out->crop_left + out->crop_right;
    out->height = desc.height + out->crop_top + out->crop_bottom;

    av_log(logctx, AV_LOG_DEBUG, "out PTS %"PRId64", %dx%d, "
                                 "%zu/%zu/%zu/%zu, "
                                 "SAR %d:%d, "
                                 "hasEnhancement %d, enhanced %d\n",
           out->pts, out->width, out->height,
           out->crop_top, out->crop_bottom, out->crop_left, out->crop_right,
           out->sample_aspect_ratio.num, out->sample_aspect_ratio.den,
           info.hasEnhancement, info.enhanced);

    res = LCEVC_FreePicture(lcevc->decoder, picture);
    if (res != LCEVC_Success)
        return AVERROR_EXTERNAL;

    return 0;
}

static int lcevc_flush_pictures(FFLCEVCContext *lcevc)
{
    LCEVC_PictureHandle picture;
    LCEVC_ReturnCode res;

    while (1) {
        AVFrame *base = NULL;
        res = LCEVC_ReceiveDecoderBase (lcevc->decoder, &picture);
        if (res != LCEVC_Success && res != LCEVC_Again)
            return AVERROR_EXTERNAL;

        if (res == LCEVC_Again)
            break;

        LCEVC_GetPictureUserData(lcevc->decoder, picture, (void **)&base);
        av_frame_free(&base);

        res = LCEVC_FreePicture(lcevc->decoder, picture);
        if (res != LCEVC_Success)
            return AVERROR_EXTERNAL;
    }

    return 0;
}

static int lcevc_receive_frame(void *logctx, FFLCEVCFrame *frame_ctx, AVFrame *out)
{
    FFLCEVCContext *lcevc = frame_ctx->lcevc;
    int ret;

    ret = generate_output(logctx, frame_ctx, out);
    if (ret < 0)
        return ret;

    return lcevc_flush_pictures(lcevc);
}

static void event_callback(LCEVC_DecoderHandle dec, LCEVC_Event event,
    LCEVC_PictureHandle pic, const LCEVC_DecodeInformation *info,
    const uint8_t *data, uint32_t size, void *logctx)
{
    switch (event) {
    case LCEVC_Log:
        av_log(logctx, AV_LOG_INFO, "%s\n", data);
        break;
    default:
        break;
    }
}

static void lcevc_free(AVRefStructOpaque unused, void *obj)
{
    FFLCEVCContext *lcevc = obj;
    if (lcevc->initialized) {
        LCEVC_FlushDecoder(lcevc->decoder);
        lcevc_flush_pictures(lcevc);
        LCEVC_DestroyDecoder(lcevc->decoder);
    }
    if (lcevc->frag)
        ff_cbs_fragment_free(lcevc->frag);
    av_freep(&lcevc->frag);
    ff_cbs_close(&lcevc->cbc);
    memset(lcevc, 0, sizeof(*lcevc));
}

static int lcevc_init(FFLCEVCContext *lcevc, void *logctx)
{
    LCEVC_AccelContextHandle dummy = { 0 };
    const int32_t event = LCEVC_Log;

    if (LCEVC_CreateDecoder(&lcevc->decoder, dummy) != LCEVC_Success) {
        av_log(logctx, AV_LOG_ERROR, "Failed to create LCEVC decoder\n");
        return AVERROR_EXTERNAL;
    }

    LCEVC_ConfigureDecoderInt(lcevc->decoder, "log_level", 4);
    LCEVC_ConfigureDecoderIntArray(lcevc->decoder, "events", 1, &event);
    LCEVC_SetDecoderEventCallback(lcevc->decoder, event_callback, logctx);

    if (LCEVC_InitializeDecoder(lcevc->decoder) != LCEVC_Success) {
        av_log(logctx, AV_LOG_ERROR, "Failed to initialize LCEVC decoder\n");
        LCEVC_DestroyDecoder(lcevc->decoder);
        return AVERROR_EXTERNAL;
    }

    lcevc->initialized = 1;

    return 0;
}

int ff_lcevc_process(void *logctx, AVFrame *frame)
{
    FrameDecodeData  *fdd = frame->private_ref;
    FFLCEVCFrame *frame_ctx = fdd->post_process_opaque;
    FFLCEVCContext *lcevc = frame_ctx->lcevc;
    int ret;

    if (!lcevc->initialized) {
        ret = lcevc_init(lcevc, logctx);
        if (ret < 0)
            return ret;
    }

    av_assert0(frame_ctx->frame);


    ret = lcevc_send_frame(logctx, frame_ctx, frame);
    if (ret)
        return ret < 0 ? ret : 0;

    ret = lcevc_receive_frame(logctx, frame_ctx, frame);
    if (ret < 0)
        return ret;

    av_frame_remove_side_data(frame, AV_FRAME_DATA_LCEVC);

    return 0;
}

int ff_lcevc_parse_frame(FFLCEVCContext *lcevc, const AVFrame *frame,
                         int *width, int *height, void *logctx)
{
    LCEVCRawProcessBlock *block = NULL;
    LCEVCRawGlobalConfig *gc = NULL;
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_LCEVC);
    int ret;

    ret = ff_cbs_read(lcevc->cbc, lcevc->frag, sd->buf, sd->data, sd->size);
    if (ret < 0) {
        av_log(logctx, AV_LOG_ERROR, "Failed to parse Access Unit.\n");
        goto end;
    }

    ret = ff_cbs_lcevc_find_process_block(lcevc->cbc, lcevc->frag,
                                          LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG, &block);
    if (ret < 0) {
        ret = 0;
        goto end;
    }

    gc = block->payload;
    if (gc->resolution_type < 63) {
        *width  = ff_lcevc_resolution_type[gc->resolution_type].width;
        *height = ff_lcevc_resolution_type[gc->resolution_type].height;
    } else {
        *width  = gc->custom_resolution_width;
        *height = gc->custom_resolution_height;
    }

    ret = 0;
end:
    ff_cbs_fragment_reset(lcevc->frag);

    return ret;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    LCEVC_IDR_NUT,
    LCEVC_NON_IDR_NUT,
};

int ff_lcevc_alloc(FFLCEVCContext **plcevc, void *logctx)
{
    FFLCEVCContext *lcevc = NULL;
    int ret;

    lcevc = av_refstruct_alloc_ext(sizeof(*lcevc), 0, NULL, lcevc_free);
    if (!lcevc)
        return AVERROR(ENOMEM);

    lcevc->frag = av_mallocz(sizeof(*lcevc->frag));
    if (!lcevc->frag) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = ff_cbs_init(&lcevc->cbc, AV_CODEC_ID_LCEVC, logctx);
    if (ret < 0)
        goto fail;

    lcevc->cbc->decompose_unit_types    = decompose_unit_types;
    lcevc->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    *plcevc = lcevc;
    return 0;
fail:
    av_refstruct_unref(&lcevc);
    return ret;
}

void ff_lcevc_unref(void *opaque)
{
    FFLCEVCFrame *lcevc = opaque;
    av_refstruct_unref(&lcevc->lcevc);
    av_frame_free(&lcevc->frame);
    av_free(opaque);
}
