/*
 * Scalable Video Technology for AV1 encoder library plugin
 *
 * Copyright (c) 2018 Intel Corporation
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <EbSvtAv1ErrorCodes.h>
#include <EbSvtAv1Enc.h>
#include <EbSvtAv1Metadata.h>

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

#include "codec_internal.h"
#include "dovi_rpu.h"
#include "encode.h"
#include "packet_internal.h"
#include "avcodec.h"
#include "profiles.h"

typedef enum eos_status {
    EOS_NOT_REACHED = 0,
    EOS_SENT,
    EOS_RECEIVED
}EOS_STATUS;

typedef struct SvtContext {
    const AVClass *class;

    EbSvtAv1EncConfiguration    enc_params;
    EbComponentType            *svt_handle;

    EbBufferHeaderType         *in_buf;
    int                         raw_size;
    int                         max_tu_size;

    AVFrame *frame;

    AVBufferPool *pool;

    EOS_STATUS eos_flag;

    DOVIContext dovi;

    // User options.
    AVDictionary *svtav1_opts;
    int enc_mode;
    int crf;
    int qp;
} SvtContext;

static const struct {
    EbErrorType    eb_err;
    int            av_err;
    const char     *desc;
} svt_errors[] = {
    { EB_ErrorNone,                             0,              "success"                   },
    { EB_ErrorInsufficientResources,      AVERROR(ENOMEM),      "insufficient resources"    },
    { EB_ErrorUndefined,                  AVERROR(EINVAL),      "undefined error"           },
    { EB_ErrorInvalidComponent,           AVERROR(EINVAL),      "invalid component"         },
    { EB_ErrorBadParameter,               AVERROR(EINVAL),      "bad parameter"             },
    { EB_ErrorDestroyThreadFailed,        AVERROR_EXTERNAL,     "failed to destroy thread"  },
    { EB_ErrorSemaphoreUnresponsive,      AVERROR_EXTERNAL,     "semaphore unresponsive"    },
    { EB_ErrorDestroySemaphoreFailed,     AVERROR_EXTERNAL,     "failed to destroy semaphore"},
    { EB_ErrorCreateMutexFailed,          AVERROR_EXTERNAL,     "failed to create mutex"    },
    { EB_ErrorMutexUnresponsive,          AVERROR_EXTERNAL,     "mutex unresponsive"        },
    { EB_ErrorDestroyMutexFailed,         AVERROR_EXTERNAL,     "failed to destroy mutex"   },
    { EB_NoErrorEmptyQueue,               AVERROR(EAGAIN),      "empty queue"               },
};

static int svt_map_error(EbErrorType eb_err, const char **desc)
{
    int i;

    av_assert0(desc);
    for (i = 0; i < FF_ARRAY_ELEMS(svt_errors); i++) {
        if (svt_errors[i].eb_err == eb_err) {
            *desc = svt_errors[i].desc;
            return svt_errors[i].av_err;
        }
    }
    *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

static int svt_print_error(void *log_ctx, EbErrorType err,
                           const char *error_string)
{
    const char *desc;
    int ret = svt_map_error(err, &desc);

    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (0x%x)\n", error_string, desc, err);

    return ret;
}

static int alloc_buffer(EbSvtAv1EncConfiguration *config, SvtContext *svt_enc)
{
    const size_t luma_size = config->source_width * config->source_height *
        (config->encoder_bit_depth > 8 ? 2 : 1);

    EbSvtIOFormat *in_data;

    svt_enc->raw_size = luma_size * 3 / 2;

    // allocate buffer for in and out
    svt_enc->in_buf           = av_mallocz(sizeof(*svt_enc->in_buf));
    if (!svt_enc->in_buf)
        return AVERROR(ENOMEM);

    svt_enc->in_buf->p_buffer = av_mallocz(sizeof(*in_data));
    if (!svt_enc->in_buf->p_buffer)
        return AVERROR(ENOMEM);

    svt_enc->in_buf->size     = sizeof(*svt_enc->in_buf);

    return 0;

}

static void handle_mdcv(struct EbSvtAv1MasteringDisplayInfo *dst,
                        const AVMasteringDisplayMetadata *mdcv)
{
    if (mdcv->has_primaries) {
        const struct EbSvtAv1ChromaPoints *const points[] = {
            &dst->r,
            &dst->g,
            &dst->b,
        };

        for (int i = 0; i < 3; i++) {
            const struct EbSvtAv1ChromaPoints *dst = points[i];
            const AVRational *src = mdcv->display_primaries[i];

            AV_WB16(&dst->x,
                    av_rescale_q(1, src[0], (AVRational){ 1, (1 << 16) }));
            AV_WB16(&dst->y,
                    av_rescale_q(1, src[1], (AVRational){ 1, (1 << 16) }));
        }

        AV_WB16(&dst->white_point.x,
                av_rescale_q(1, mdcv->white_point[0],
                             (AVRational){ 1, (1 << 16) }));
        AV_WB16(&dst->white_point.y,
                av_rescale_q(1, mdcv->white_point[1],
                             (AVRational){ 1, (1 << 16) }));
    }

    if (mdcv->has_luminance) {
        AV_WB32(&dst->max_luma,
                av_rescale_q(1, mdcv->max_luminance,
                             (AVRational){ 1, (1 << 8) }));
        AV_WB32(&dst->min_luma,
                av_rescale_q(1, mdcv->min_luminance,
                             (AVRational){ 1, (1 << 14) }));
    }
}

static void handle_side_data(AVCodecContext *avctx,
                             EbSvtAv1EncConfiguration *param)
{
    const AVFrameSideData *cll_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    const AVFrameSideData *mdcv_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data,
            AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);

    if (cll_sd) {
        const AVContentLightMetadata *cll =
            (AVContentLightMetadata *)cll_sd->data;

        AV_WB16(&param->content_light_level.max_cll, cll->MaxCLL);
        AV_WB16(&param->content_light_level.max_fall, cll->MaxFALL);
    }

    if (mdcv_sd) {
        handle_mdcv(&param->mastering_display,
                    (AVMasteringDisplayMetadata *)mdcv_sd->data);
    }
}

static int config_enc_params(EbSvtAv1EncConfiguration *param,
                             AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;
    const AVPixFmtDescriptor *desc;
    const AVDictionaryEntry av_unused *en = NULL;

    // Update param from options
    if (svt_enc->enc_mode >= -1)
        param->enc_mode             = svt_enc->enc_mode;

    if (avctx->bit_rate) {
        param->target_bit_rate      = avctx->bit_rate;
        if (avctx->rc_max_rate != avctx->bit_rate)
            param->rate_control_mode = 1;
        else
            param->rate_control_mode = 2;

        param->max_qp_allowed       = avctx->qmax;
        param->min_qp_allowed       = avctx->qmin;
    }
    param->max_bit_rate             = avctx->rc_max_rate;
    if ((avctx->bit_rate > 0 || avctx->rc_max_rate > 0) && avctx->rc_buffer_size)
        param->maximum_buffer_size_ms =
            avctx->rc_buffer_size * 1000LL /
            FFMAX(avctx->bit_rate, avctx->rc_max_rate);

    if (svt_enc->crf > 0) {
        param->qp                   = svt_enc->crf;
        param->rate_control_mode    = 0;
    } else if (svt_enc->qp > 0) {
        param->qp                   = svt_enc->qp;
        param->rate_control_mode    = 0;
        param->enable_adaptive_quantization = 0;
    }

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    param->color_primaries          = avctx->color_primaries;
    param->matrix_coefficients      = (desc->flags & AV_PIX_FMT_FLAG_RGB) ?
                                      AVCOL_SPC_RGB : avctx->colorspace;
    param->transfer_characteristics = avctx->color_trc;

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED)
        param->color_range = avctx->color_range == AVCOL_RANGE_JPEG;
    else
        param->color_range = !!(desc->flags & AV_PIX_FMT_FLAG_RGB);

#if SVT_AV1_CHECK_VERSION(1, 0, 0)
    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        const char *name =
            av_chroma_location_name(avctx->chroma_sample_location);

        switch (avctx->chroma_sample_location) {
        case AVCHROMA_LOC_LEFT:
            param->chroma_sample_position = EB_CSP_VERTICAL;
            break;
        case AVCHROMA_LOC_TOPLEFT:
            param->chroma_sample_position = EB_CSP_COLOCATED;
            break;
        default:
            if (!name)
                break;

            av_log(avctx, AV_LOG_WARNING,
                   "Specified chroma sample location %s is unsupported "
                   "on the AV1 bit stream level. Usage of a container that "
                   "allows passing this information - such as Matroska - "
                   "is recommended.\n",
                   name);
            break;
        }
    }
#endif

    if (avctx->profile != AV_PROFILE_UNKNOWN)
        param->profile = avctx->profile;

    if (avctx->level != AV_LEVEL_UNKNOWN)
        param->level = avctx->level;

    // gop_size == 1 case is handled when encoding each frame by setting
    // pic_type to EB_AV1_KEY_PICTURE. For gop_size > 1, set the
    // intra_period_length. Even though setting intra_period_length to 0 should
    // work in this case, it does not.
    // See: https://gitlab.com/AOMediaCodec/SVT-AV1/-/issues/2076
    if (avctx->gop_size > 1)
        param->intra_period_length  = avctx->gop_size - 1;

#if SVT_AV1_CHECK_VERSION(1, 1, 0)
    // In order for SVT-AV1 to force keyframes by setting pic_type to
    // EB_AV1_KEY_PICTURE on any frame, force_key_frames has to be set. Note
    // that this does not force all frames to be keyframes (it only forces a
    // keyframe with pic_type is set to EB_AV1_KEY_PICTURE). As of now, SVT-AV1
    // does not support arbitrary keyframe requests by setting pic_type to
    // EB_AV1_KEY_PICTURE, so it is done only when gop_size == 1.
    // FIXME: When SVT-AV1 supports arbitrary keyframe requests, this code needs
    // to be updated to set force_key_frames accordingly.
    if (avctx->gop_size == 1)
        param->force_key_frames = 1;
#endif

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        param->frame_rate_numerator   = avctx->framerate.num;
        param->frame_rate_denominator = avctx->framerate.den;
    } else {
        param->frame_rate_numerator   = avctx->time_base.den;
        param->frame_rate_denominator = avctx->time_base.num;
    }

    /* 2 = IDR, closed GOP, 1 = CRA, open GOP */
    param->intra_refresh_type = avctx->flags & AV_CODEC_FLAG_CLOSED_GOP ? 2 : 1;

    handle_side_data(avctx, param);

#if SVT_AV1_CHECK_VERSION(0, 9, 1)
    while ((en = av_dict_iterate(svt_enc->svtav1_opts, en))) {
        EbErrorType ret = svt_av1_enc_parse_parameter(param, en->key, en->value);
        if (ret != EB_ErrorNone) {
            int level = (avctx->err_recognition & AV_EF_EXPLODE) ? AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(avctx, level, "Error parsing option %s: %s.\n", en->key, en->value);
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR(EINVAL);
        }
    }
#else
    if (av_dict_count(svt_enc->svtav1_opts)) {
        int level = (avctx->err_recognition & AV_EF_EXPLODE) ? AV_LOG_ERROR : AV_LOG_WARNING;
        av_log(avctx, level, "svt-params needs libavcodec to be compiled with SVT-AV1 "
                             "headers >= 0.9.1.\n");
        if (avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR(ENOSYS);
    }
#endif

    param->source_width     = avctx->width;
    param->source_height    = avctx->height;

    param->encoder_bit_depth = desc->comp[0].depth;

    if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1)
        param->encoder_color_format   = EB_YUV420;
    else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 0)
        param->encoder_color_format   = EB_YUV422;
    else if (!desc->log2_chroma_w && !desc->log2_chroma_h)
        param->encoder_color_format   = EB_YUV444;
    else {
        av_log(avctx, AV_LOG_ERROR , "Unsupported pixel format\n");
        return AVERROR(EINVAL);
    }

    if ((param->encoder_color_format == EB_YUV422 || param->encoder_bit_depth > 10)
         && param->profile != AV_PROFILE_AV1_PROFESSIONAL ) {
        av_log(avctx, AV_LOG_WARNING, "Forcing Professional profile\n");
        param->profile = AV_PROFILE_AV1_PROFESSIONAL;
    } else if (param->encoder_color_format == EB_YUV444 && param->profile != AV_PROFILE_AV1_HIGH) {
        av_log(avctx, AV_LOG_WARNING, "Forcing High profile\n");
        param->profile = AV_PROFILE_AV1_HIGH;
    }

    avctx->bit_rate       = param->rate_control_mode > 0 ?
                            param->target_bit_rate : 0;
    avctx->rc_max_rate    = param->max_bit_rate;
    avctx->rc_buffer_size = param->maximum_buffer_size_ms *
                            FFMAX(avctx->bit_rate, avctx->rc_max_rate) / 1000LL;

    if (avctx->bit_rate || avctx->rc_max_rate || avctx->rc_buffer_size) {
        AVCPBProperties *cpb_props = ff_encode_add_cpb_side_data(avctx);
        if (!cpb_props)
            return AVERROR(ENOMEM);

        cpb_props->buffer_size = avctx->rc_buffer_size;
        cpb_props->max_bitrate = avctx->rc_max_rate;
        cpb_props->avg_bitrate = avctx->bit_rate;
    }

    return 0;
}

static int read_in_data(EbSvtAv1EncConfiguration *param, const AVFrame *frame,
                         EbBufferHeaderType *header_ptr)
{
    EbSvtIOFormat *in_data = (EbSvtIOFormat *)header_ptr->p_buffer;
    ptrdiff_t linesizes[4];
    size_t sizes[4];
    int bytes_shift = param->encoder_bit_depth > 8 ? 1 : 0;
    int ret, frame_size;

    for (int i = 0; i < 4; i++)
        linesizes[i] = frame->linesize[i];

    ret = av_image_fill_plane_sizes(sizes, frame->format, frame->height,
                                    linesizes);
    if (ret < 0)
        return ret;

    frame_size = 0;
    for (int i = 0; i < 4; i++) {
        if (sizes[i] > INT_MAX - frame_size)
            return AVERROR(EINVAL);
        frame_size += sizes[i];
    }

    in_data->luma = frame->data[0];
    in_data->cb   = frame->data[1];
    in_data->cr   = frame->data[2];

    in_data->y_stride  = AV_CEIL_RSHIFT(frame->linesize[0], bytes_shift);
    in_data->cb_stride = AV_CEIL_RSHIFT(frame->linesize[1], bytes_shift);
    in_data->cr_stride = AV_CEIL_RSHIFT(frame->linesize[2], bytes_shift);

    header_ptr->n_filled_len = frame_size;
    svt_metadata_array_free(&header_ptr->metadata);

    return 0;
}

static av_cold int eb_enc_init(AVCodecContext *avctx)
{
    SvtContext   *svt_enc = avctx->priv_data;
    EbErrorType svt_ret;
    int ret;

    svt_enc->eos_flag = EOS_NOT_REACHED;

#if SVT_AV1_CHECK_VERSION(3, 0, 0)
    svt_ret = svt_av1_enc_init_handle(&svt_enc->svt_handle, &svt_enc->enc_params);
#else
    svt_ret = svt_av1_enc_init_handle(&svt_enc->svt_handle, svt_enc, &svt_enc->enc_params);
#endif
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error initializing encoder handle");
    }

    ret = config_enc_params(&svt_enc->enc_params, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error configuring encoder parameters\n");
        return ret;
    }

    svt_ret = svt_av1_enc_set_parameter(svt_enc->svt_handle, &svt_enc->enc_params);
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error setting encoder parameters");
    }

    svt_ret = svt_av1_enc_init(svt_enc->svt_handle);
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error initializing encoder");
    }

    svt_enc->dovi.logctx = avctx;
    ret = ff_dovi_configure(&svt_enc->dovi, avctx);
    if (ret < 0)
        return ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        EbBufferHeaderType *headerPtr = NULL;

        svt_ret = svt_av1_enc_stream_header(svt_enc->svt_handle, &headerPtr);
        if (svt_ret != EB_ErrorNone) {
            return svt_print_error(avctx, svt_ret, "Error building stream header");
        }

        avctx->extradata_size = headerPtr->n_filled_len;
        avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot allocate AV1 header of size %d.\n", avctx->extradata_size);
            return AVERROR(ENOMEM);
        }

        memcpy(avctx->extradata, headerPtr->p_buffer, avctx->extradata_size);

        svt_ret = svt_av1_enc_stream_header_release(headerPtr);
        if (svt_ret != EB_ErrorNone) {
            return svt_print_error(avctx, svt_ret, "Error freeing stream header");
        }
    }

    svt_enc->frame = av_frame_alloc();
    if (!svt_enc->frame)
        return AVERROR(ENOMEM);

    return alloc_buffer(&svt_enc->enc_params, svt_enc);
}

static int eb_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    SvtContext           *svt_enc = avctx->priv_data;
    EbBufferHeaderType  *headerPtr = svt_enc->in_buf;
    AVFrameSideData *sd;
    EbErrorType svt_ret;
    int ret;

    if (!frame) {
        EbBufferHeaderType headerPtrLast;

        if (svt_enc->eos_flag == EOS_SENT)
            return 0;

        memset(&headerPtrLast, 0, sizeof(headerPtrLast));
        headerPtrLast.pic_type      = EB_AV1_INVALID_PICTURE;
        headerPtrLast.flags         = EB_BUFFERFLAG_EOS;

        svt_av1_enc_send_picture(svt_enc->svt_handle, &headerPtrLast);
        svt_enc->eos_flag = EOS_SENT;
        return 0;
    }

    ret = read_in_data(&svt_enc->enc_params, frame, headerPtr);
    if (ret < 0)
        return ret;

    headerPtr->flags         = 0;
    headerPtr->p_app_private = NULL;
    headerPtr->pts           = frame->pts;

    switch (frame->pict_type) {
    case AV_PICTURE_TYPE_I:
        headerPtr->pic_type = EB_AV1_KEY_PICTURE;
        break;
    default:
        // Actually means auto, or default.
        headerPtr->pic_type = EB_AV1_INVALID_PICTURE;
        break;
    }

    if (avctx->gop_size == 1)
        headerPtr->pic_type = EB_AV1_KEY_PICTURE;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
    if (svt_enc->dovi.cfg.dv_profile && sd) {
        const AVDOVIMetadata *metadata = (const AVDOVIMetadata *)sd->data;
        uint8_t *t35;
        int size;
        if ((ret = ff_dovi_rpu_generate(&svt_enc->dovi, metadata, FF_DOVI_WRAP_T35,
                                        &t35, &size)) < 0)
            return ret;
        ret = svt_add_metadata(headerPtr, EB_AV1_METADATA_TYPE_ITUT_T35, t35, size);
        av_free(t35);
        if (ret < 0)
            return AVERROR(ENOMEM);
    } else if (svt_enc->dovi.cfg.dv_profile) {
        av_log(avctx, AV_LOG_ERROR, "Dolby Vision enabled, but received frame "
               "without AV_FRAME_DATA_DOVI_METADATA\n");
        return AVERROR_INVALIDDATA;
    }


    svt_ret = svt_av1_enc_send_picture(svt_enc->svt_handle, headerPtr);
    if (svt_ret != EB_ErrorNone)
        return svt_print_error(avctx, svt_ret, "Error sending a frame to encoder");

    return 0;
}

static AVBufferRef *get_output_ref(AVCodecContext *avctx, SvtContext *svt_enc, int filled_len)
{
    if (filled_len > svt_enc->max_tu_size) {
        const int max_frames = 8;
        int max_tu_size;

        if (filled_len > svt_enc->raw_size * max_frames) {
            av_log(avctx, AV_LOG_ERROR, "TU size > %d raw frame size.\n", max_frames);
            return NULL;
        }

        max_tu_size = 1 << av_ceil_log2(filled_len);
        av_buffer_pool_uninit(&svt_enc->pool);
        svt_enc->pool = av_buffer_pool_init(max_tu_size + AV_INPUT_BUFFER_PADDING_SIZE, NULL);
        if (!svt_enc->pool)
            return NULL;

        svt_enc->max_tu_size = max_tu_size;
    }
    av_assert0(svt_enc->pool);

    return av_buffer_pool_get(svt_enc->pool);
}

static int eb_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    SvtContext  *svt_enc = avctx->priv_data;
    EbBufferHeaderType *headerPtr;
    AVFrame *frame = svt_enc->frame;
    EbErrorType svt_ret;
    AVBufferRef *ref;
    int ret = 0, pict_type;

    if (svt_enc->eos_flag == EOS_RECEIVED)
        return AVERROR_EOF;

    ret = ff_encode_get_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        return ret;
    if (ret == AVERROR_EOF)
        frame = NULL;

    ret = eb_send_frame(avctx, frame);
    if (ret < 0)
        return ret;
    av_frame_unref(svt_enc->frame);

    svt_ret = svt_av1_enc_get_packet(svt_enc->svt_handle, &headerPtr, svt_enc->eos_flag);
    if (svt_ret == EB_NoErrorEmptyQueue)
        return AVERROR(EAGAIN);
    else if (svt_ret != EB_ErrorNone)
        return svt_print_error(avctx, svt_ret, "Error getting an output packet from encoder");

#if SVT_AV1_CHECK_VERSION(2, 0, 0)
    if (headerPtr->flags & EB_BUFFERFLAG_EOS) {
         svt_enc->eos_flag = EOS_RECEIVED;
         svt_av1_enc_release_out_buffer(&headerPtr);
         return AVERROR_EOF;
    }
#endif

    ref = get_output_ref(avctx, svt_enc, headerPtr->n_filled_len);
    if (!ref) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate output packet.\n");
        svt_av1_enc_release_out_buffer(&headerPtr);
        return AVERROR(ENOMEM);
    }
    pkt->buf = ref;
    pkt->data = ref->data;

    memcpy(pkt->data, headerPtr->p_buffer, headerPtr->n_filled_len);
    memset(pkt->data + headerPtr->n_filled_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    pkt->size = headerPtr->n_filled_len;
    pkt->pts  = headerPtr->pts;
    pkt->dts  = headerPtr->dts;

    switch (headerPtr->pic_type) {
    case EB_AV1_KEY_PICTURE:
        pkt->flags |= AV_PKT_FLAG_KEY;
        // fall-through
    case EB_AV1_INTRA_ONLY_PICTURE:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case EB_AV1_INVALID_PICTURE:
        pict_type = AV_PICTURE_TYPE_NONE;
        break;
    default:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    }

    if (headerPtr->pic_type == EB_AV1_NON_REF_PICTURE)
        pkt->flags |= AV_PKT_FLAG_DISPOSABLE;

#if !(SVT_AV1_CHECK_VERSION(2, 0, 0))
    if (headerPtr->flags & EB_BUFFERFLAG_EOS)
        svt_enc->eos_flag = EOS_RECEIVED;
#endif

    ff_side_data_set_encoder_stats(pkt, headerPtr->qp * FF_QP2LAMBDA, NULL, 0, pict_type);

    svt_av1_enc_release_out_buffer(&headerPtr);

    return 0;
}

static av_cold int eb_enc_close(AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;

    if (svt_enc->svt_handle) {
        svt_av1_enc_deinit(svt_enc->svt_handle);
        svt_av1_enc_deinit_handle(svt_enc->svt_handle);
    }
    if (svt_enc->in_buf) {
        av_free(svt_enc->in_buf->p_buffer);
        svt_metadata_array_free(&svt_enc->in_buf->metadata);
        av_freep(&svt_enc->in_buf);
    }

    av_buffer_pool_uninit(&svt_enc->pool);
    av_frame_free(&svt_enc->frame);
    ff_dovi_ctx_unref(&svt_enc->dovi);

    return 0;
}

#define OFFSET(x) offsetof(SvtContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset", "Encoding preset",
      OFFSET(enc_mode), AV_OPT_TYPE_INT, { .i64 = -2 }, -2, MAX_ENC_PRESET, VE },

    FF_AV1_PROFILE_OPTS

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, VE, .unit = "avctx.level"
        { LEVEL("2.0", 20) },
        { LEVEL("2.1", 21) },
        { LEVEL("2.2", 22) },
        { LEVEL("2.3", 23) },
        { LEVEL("3.0", 30) },
        { LEVEL("3.1", 31) },
        { LEVEL("3.2", 32) },
        { LEVEL("3.3", 33) },
        { LEVEL("4.0", 40) },
        { LEVEL("4.1", 41) },
        { LEVEL("4.2", 42) },
        { LEVEL("4.3", 43) },
        { LEVEL("5.0", 50) },
        { LEVEL("5.1", 51) },
        { LEVEL("5.2", 52) },
        { LEVEL("5.3", 53) },
        { LEVEL("6.0", 60) },
        { LEVEL("6.1", 61) },
        { LEVEL("6.2", 62) },
        { LEVEL("6.3", 63) },
        { LEVEL("7.0", 70) },
        { LEVEL("7.1", 71) },
        { LEVEL("7.2", 72) },
        { LEVEL("7.3", 73) },
#undef LEVEL

    { "crf", "Constant Rate Factor value", OFFSET(crf),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 63, VE },
    { "qp", "Initial Quantizer level value", OFFSET(qp),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 63, VE },
    { "svtav1-params", "Set the SVT-AV1 configuration using a :-separated list of key=value parameters", OFFSET(svtav1_opts), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },

    { "dolbyvision", "Enable Dolby Vision RPU coding", OFFSET(dovi.enable), AV_OPT_TYPE_BOOL, {.i64 = FF_DOVI_AUTOMATIC }, -1, 1, VE, .unit = "dovi" },
    {   "auto", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_DOVI_AUTOMATIC}, .flags = VE, .unit = "dovi" },

    {NULL},
};

static const AVClass class = {
    .class_name = "libsvtav1",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault eb_enc_defaults[] = {
    { "b",         "0"    },
    { "flags",     "+cgop" },
    { "g",         "-1"    },
    { "qmin",      "1"     },
    { "qmax",      "63"    },
    { NULL },
};

const FFCodec ff_libsvtav1_encoder = {
    .p.name         = "libsvtav1",
    CODEC_LONG_NAME("SVT-AV1(Scalable Video Technology for AV1) encoder"),
    .priv_data_size = sizeof(SvtContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .init           = eb_enc_init,
    FF_CODEC_RECEIVE_PACKET_CB(eb_receive_packet),
    .close          = eb_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10),
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class   = &class,
    .defaults       = eb_enc_defaults,
    .p.wrapper_name = "libsvtav1",
};
