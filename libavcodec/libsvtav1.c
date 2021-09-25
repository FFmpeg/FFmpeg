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

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

#include "internal.h"
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

    // User options.
    int hierarchical_level;
    int la_depth;
    int enc_mode;
    int rc_mode;
    int scd;
    int qp;

    int tier;

    int tile_columns;
    int tile_rows;
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
    const int    pack_mode_10bit =
        (config->encoder_bit_depth > 8) && (config->compressed_ten_bit_format == 0) ? 1 : 0;
    const size_t luma_size_8bit  =
        config->source_width * config->source_height * (1 << pack_mode_10bit);
    const size_t luma_size_10bit =
        (config->encoder_bit_depth > 8 && pack_mode_10bit == 0) ? luma_size_8bit : 0;

    EbSvtIOFormat *in_data;

    svt_enc->raw_size = (luma_size_8bit + luma_size_10bit) * 3 / 2;

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

static int config_enc_params(EbSvtAv1EncConfiguration *param,
                             AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;
    const AVPixFmtDescriptor *desc;

    param->source_width     = avctx->width;
    param->source_height    = avctx->height;

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
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

    if (avctx->profile != FF_PROFILE_UNKNOWN)
        param->profile = avctx->profile;

    if (avctx->level != FF_LEVEL_UNKNOWN)
        param->level = avctx->level;

    if ((param->encoder_color_format == EB_YUV422 || param->encoder_bit_depth > 10)
         && param->profile != FF_PROFILE_AV1_PROFESSIONAL ) {
        av_log(avctx, AV_LOG_WARNING, "Forcing Professional profile\n");
        param->profile = FF_PROFILE_AV1_PROFESSIONAL;
    } else if (param->encoder_color_format == EB_YUV444 && param->profile != FF_PROFILE_AV1_HIGH) {
        av_log(avctx, AV_LOG_WARNING, "Forcing High profile\n");
        param->profile = FF_PROFILE_AV1_HIGH;
    }

    // Update param from options
    param->hierarchical_levels      = svt_enc->hierarchical_level;
    param->enc_mode                 = svt_enc->enc_mode;
    param->tier                     = svt_enc->tier;
    param->rate_control_mode        = svt_enc->rc_mode;
    param->scene_change_detection   = svt_enc->scd;
    param->qp                       = svt_enc->qp;

    param->target_bit_rate          = avctx->bit_rate;

    if (avctx->gop_size > 0)
        param->intra_period_length  = avctx->gop_size - 1;

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        param->frame_rate_numerator   = avctx->framerate.num;
        param->frame_rate_denominator = avctx->framerate.den;
    } else {
        param->frame_rate_numerator   = avctx->time_base.den;
        param->frame_rate_denominator = avctx->time_base.num * avctx->ticks_per_frame;
    }

    param->enable_tpl_la = !!param->rate_control_mode;
    if (param->rate_control_mode) {
        param->max_qp_allowed       = avctx->qmax;
        param->min_qp_allowed       = avctx->qmin;
    }

    /* 2 = IDR, closed GOP, 1 = CRA, open GOP */
    param->intra_refresh_type = avctx->flags & AV_CODEC_FLAG_CLOSED_GOP ? 2 : 1;

    if (svt_enc->la_depth >= 0)
        param->look_ahead_distance  = svt_enc->la_depth;

    param->tile_columns = svt_enc->tile_columns;
    param->tile_rows    = svt_enc->tile_rows;

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

    return 0;
}

static av_cold int eb_enc_init(AVCodecContext *avctx)
{
    SvtContext   *svt_enc = avctx->priv_data;
    EbErrorType svt_ret;
    int ret;

    svt_enc->eos_flag = EOS_NOT_REACHED;

    svt_ret = svt_av1_enc_init_handle(&svt_enc->svt_handle, svt_enc, &svt_enc->enc_params);
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
    int ret;

    if (!frame) {
        EbBufferHeaderType headerPtrLast;

        if (svt_enc->eos_flag == EOS_SENT)
            return 0;

        headerPtrLast.n_alloc_len   = 0;
        headerPtrLast.n_filled_len  = 0;
        headerPtrLast.n_tick_count  = 0;
        headerPtrLast.p_app_private = NULL;
        headerPtrLast.p_buffer      = NULL;
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

    svt_av1_enc_send_picture(svt_enc->svt_handle, headerPtr);

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

    if (headerPtr->flags & EB_BUFFERFLAG_EOS)
        svt_enc->eos_flag = EOS_RECEIVED;

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
        av_freep(&svt_enc->in_buf);
    }

    av_buffer_pool_uninit(&svt_enc->pool);
    av_frame_free(&svt_enc->frame);

    return 0;
}

#define OFFSET(x) offsetof(SvtContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "hielevel", "Hierarchical prediction levels setting", OFFSET(hierarchical_level),
      AV_OPT_TYPE_INT, { .i64 = 4 }, 3, 4, VE , "hielevel"},
        { "3level", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 3 },  INT_MIN, INT_MAX, VE, "hielevel" },
        { "4level", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 4 },  INT_MIN, INT_MAX, VE, "hielevel" },

    { "la_depth", "Look ahead distance [0, 120]", OFFSET(la_depth),
      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 120, VE },

    { "preset", "Encoding preset [0, 8]",
      OFFSET(enc_mode), AV_OPT_TYPE_INT, { .i64 = MAX_ENC_PRESET }, 0, MAX_ENC_PRESET, VE },

    { "tier", "Set operating point tier", OFFSET(tier),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE, "tier" },
        { "main", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, VE, "tier" },
        { "high", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, VE, "tier" },

    FF_AV1_PROFILE_OPTS

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, VE, "avctx.level"
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

    { "rc", "Bit rate control mode", OFFSET(rc_mode),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, VE , "rc"},
        { "cqp", "Constant quantizer", 0, AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, "rc" },
        { "vbr", "Variable Bit Rate, use a target bitrate for the entire stream", 0, AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, "rc" },
        { "cvbr", "Constrained Variable Bit Rate, use a target bitrate for each GOP", 0, AV_OPT_TYPE_CONST,{ .i64 = 2 },  INT_MIN, INT_MAX, VE, "rc" },

    { "qp", "Quantizer to use with cqp rate control mode", OFFSET(qp),
      AV_OPT_TYPE_INT, { .i64 = 50 }, 0, 63, VE },

    { "sc_detection", "Scene change detection", OFFSET(scd),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "tile_columns", "Log2 of number of tile columns to use", OFFSET(tile_columns), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 4, VE},
    { "tile_rows", "Log2 of number of tile rows to use", OFFSET(tile_rows), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 6, VE},

    {NULL},
};

static const AVClass class = {
    .class_name = "libsvtav1",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault eb_enc_defaults[] = {
    { "b",         "7M"    },
    { "flags",     "+cgop" },
    { "g",         "-1"    },
    { "qmin",      "0"     },
    { "qmax",      "63"    },
    { NULL },
};

const AVCodec ff_libsvtav1_encoder = {
    .name           = "libsvtav1",
    .long_name      = NULL_IF_CONFIG_SMALL("SVT-AV1(Scalable Video Technology for AV1) encoder"),
    .priv_data_size = sizeof(SvtContext),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .init           = eb_enc_init,
    .receive_packet = eb_receive_packet,
    .close          = eb_enc_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_YUV420P10,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .defaults       = eb_enc_defaults,
    .wrapper_name   = "libsvtav1",
};
