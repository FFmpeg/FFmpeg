/*
 * RockChip MPP Video Encoder
 *
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#include "config_components.h"

#include <assert.h>
#include <stdbool.h>

#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>

#include "libavutil/avassert.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_drm.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "hwconfig.h"

#define RKMPP_TIME_BASE AV_TIME_BASE_Q
#define RKMPP_ALIGN_SIZE 16

typedef struct RKMPPEncoderContext {
    const AVClass *av_class;

    MppCtx enc;
    MppApi *mpi;
    MppEncCfg cfg;
    AVFrame *frame;

    MppFrameFormat pix_fmt;
    int mpp_stride;
    int mpp_height;
    // When pix_fmt isn't hardware pixel format
    MppBufferGroup buf_group;
    MppBuffer frame_buf;

    MppEncRcMode rc_mode;
    bool eof_sent;
} RKMPPEncoderContext;

static const enum AVPixelFormat rkmpp_pix_fmts[] = {
    AV_PIX_FMT_DRM_PRIME,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static av_cold int rkmpp_close_encoder(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;

    if (ctx->enc) {
        ctx->mpi->reset(ctx->enc);
        mpp_destroy(ctx->enc);
        ctx->enc = NULL;
    }

    if (ctx->cfg) {
        mpp_enc_cfg_deinit(ctx->cfg);
        ctx->cfg = NULL;
    }

    if (ctx->frame_buf) {
        mpp_buffer_put(ctx->frame_buf);
        ctx->frame_buf = NULL;
    }

    if (ctx->buf_group) {
        mpp_buffer_group_put(ctx->buf_group);
        ctx->buf_group = NULL;
    }

    av_frame_free(&ctx->frame);

    return 0;
}

static int rkmpp_create_frame_buf(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    if (avctx->pix_fmt == AV_PIX_FMT_DRM_PRIME)
        return 0;

    int ret = mpp_buffer_group_get_internal(&ctx->buf_group,
                MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create buffer group, %d\n",
               ret);
        return AVERROR_EXTERNAL;
    }

    int n = av_image_get_buffer_size(avctx->pix_fmt, ctx->mpp_stride,
                                     ctx->mpp_height, 1);
    if (n < 0)
        return ret;
    ret = mpp_buffer_get(ctx->buf_group, &ctx->frame_buf, n);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get frame buffer, %d\n",
               ret);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int rkmpp_export_extradata(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    MppEncHeaderMode mode = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ?
            MPP_ENC_HEADER_MODE_DEFAULT : MPP_ENC_HEADER_MODE_EACH_IDR;

    int ret = ctx->mpi->control(ctx->enc, MPP_ENC_SET_HEADER_MODE, &mode);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set header mode: %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    if (!(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER))
        return 0;

    size_t size = 4096;
    avctx->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    MppPacket packet = NULL;
    mpp_packet_init(&packet, avctx->extradata, size);
    mpp_packet_set_length(packet, 0);
    ret = ctx->mpi->control(ctx->enc, MPP_ENC_GET_HDR_SYNC, packet);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get header: %d\n", ret);
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    avctx->extradata_size = mpp_packet_get_length(packet);
    if (avctx->extradata_size == 0 || avctx->extradata_size > size) {
        av_log(avctx, AV_LOG_ERROR, "Invalid extradata size %d\n",
               avctx->extradata_size);
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    ret = 0;
out:
    mpp_packet_deinit(&packet);

    return ret;
}

static av_cold int rkmpp_init_encoder(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    int ret;

    MppCodingType codectype;
    switch (avctx->codec_id) {
    case AV_CODEC_ID_H264:
        codectype = MPP_VIDEO_CodingAVC;
        break;
    case AV_CODEC_ID_HEVC:
        codectype = MPP_VIDEO_CodingHEVC;
        break;
    default:
        av_unreachable("Invalid codec_id");
    }

    ret = mpp_check_support_format(MPP_CTX_ENC, codectype);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "The device doesn't support %s\n",
                avcodec_get_name(avctx->codec_id));
        return AVERROR_EXTERNAL;
    }

    ret = mpp_create(&ctx->enc, &ctx->mpi);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create MPP context (%d).\n", ret);
        return AVERROR_EXTERNAL;
    }

    ret = mpp_init(ctx->enc, MPP_CTX_ENC, codectype);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize MPP context (%d).\n", ret);
        return AVERROR_EXTERNAL;
    }

    ret = mpp_enc_cfg_init(&ctx->cfg);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize config (%d).\n", ret);
        return AVERROR_EXTERNAL;
    }

    MppEncCfg cfg = ctx->cfg;
    ret = ctx->mpi->control(ctx->enc, MPP_ENC_GET_CFG, cfg);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get encoder config: %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    mpp_enc_cfg_set_s32(cfg, "prep:width", avctx->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", avctx->height);
    ctx->mpp_stride = FFALIGN(avctx->width, RKMPP_ALIGN_SIZE);
    ctx->mpp_height = FFALIGN(avctx->height, RKMPP_ALIGN_SIZE);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", ctx->mpp_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ctx->mpp_height);

    if (avctx->pix_fmt == AV_PIX_FMT_DRM_PRIME || avctx->pix_fmt == AV_PIX_FMT_NV12)
        ctx->pix_fmt = MPP_FMT_YUV420SP;
    else if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
        ctx->pix_fmt = MPP_FMT_YUV420P;
    else // Can only happen during development
        return AVERROR_BUG;
    mpp_enc_cfg_set_s32(cfg, "prep:format", ctx->pix_fmt);

    if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED)
        mpp_enc_cfg_set_s32(cfg, "prep:colorspace", avctx->colorspace);
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
        mpp_enc_cfg_set_s32(cfg, "prep:colorprim", avctx->color_primaries);
    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED)
        mpp_enc_cfg_set_s32(cfg, "prep:colortrc", avctx->color_trc);
    static_assert((int)AVCOL_RANGE_MPEG == (int)MPP_FRAME_RANGE_MPEG &&
          (int)AVCOL_RANGE_JPEG == (int)MPP_FRAME_RANGE_JPEG &&
          (int)AVCOL_RANGE_UNSPECIFIED == (int) MPP_FRAME_RANGE_UNSPECIFIED,
          "MppFrameColorRange not equal to AVColorRange");
    mpp_enc_cfg_set_s32(cfg, "prep:colorrange", avctx->color_range);

    /* These two options sound like variable frame rate from the doc, but they
     * are not. When they are false, bitrate control is based on frame numbers
     * and framerate. But when they are true, bitrate control is based on wall
     * clock time, not based on frame timestamps, which makes these options
     * almost useless, except in certain rare realtime case.
     */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    if (avctx->framerate.den > 0 && avctx->framerate.num > 0) {
        mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", avctx->framerate.num);
        mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", avctx->framerate.den);
        mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", avctx->framerate.num);
        mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", avctx->framerate.den);
    }

    if (avctx->gop_size >= 0)
        mpp_enc_cfg_set_s32(cfg, "rc:gop", avctx->gop_size);

    mpp_enc_cfg_set_u32(cfg, "rc:mode", ctx->rc_mode);
    if (avctx->bit_rate > 0) {
        mpp_enc_cfg_set_s32(cfg, "rc:bps_target", avctx->bit_rate);
        if (avctx->rc_buffer_size >= avctx->bit_rate) {
            int seconds = round((double)avctx->rc_buffer_size / avctx->bit_rate);
            // 60 is the upper bound from the doc
            seconds = FFMIN(seconds, 60);
            mpp_enc_cfg_set_s32(cfg, "rc:stats_time", seconds);
        }
    }
    if (avctx->rc_max_rate > 0)
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", avctx->rc_max_rate);
    if (avctx->rc_min_rate > 0)
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", avctx->rc_min_rate);

    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);

    ret = ctx->mpi->control(ctx->enc, MPP_ENC_SET_CFG, cfg);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set config: %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    ret = rkmpp_create_frame_buf(avctx);
    if (ret < 0)
        return ret;

    ret = rkmpp_export_extradata(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int rkmpp_output_pkt(AVCodecContext *avctx, AVPacket *pkt, MppPacket packet)
{
    if (mpp_packet_get_eos(packet)) {
        av_log(avctx, AV_LOG_INFO, "Receive eos packet\n");
        return AVERROR_EOF;
    }

    size_t size = mpp_packet_get_length(packet);
    void *data = mpp_packet_get_pos(packet);

    if (!size || !data) {
        av_log(avctx, AV_LOG_ERROR, "Encoder return empty packet\n");
        return AVERROR_EXTERNAL;
    }

    int ret = ff_get_encode_buffer(avctx, pkt, size, 0);
    if (ret < 0)
        return ret;
    memcpy(pkt->data, data, size);

    int64_t pts = mpp_packet_get_pts(packet);
    int64_t dts = mpp_packet_get_dts(packet);

    pkt->pts = av_rescale_q(pts, RKMPP_TIME_BASE, avctx->time_base);
    /* dts is always zero currently, since rkmpp copy dts from MppFrame to
     * MppPacket, and we don't set dts for MppFrame (it make no sense for
     * encoder). rkmpp encoder doesn't support reordering, so we can just
     * set dts as pts.
     *
     * TODO: remove this workaround once rkmpp fixed the issue.
     */
    if (dts)
        pkt->dts = av_rescale_q(dts, RKMPP_TIME_BASE, avctx->time_base);
    else
        pkt->dts = pkt->pts;

    MppMeta meta = mpp_packet_get_meta(packet);
    if (!meta) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get meta from mpp packet\n");
        return AVERROR_EXTERNAL;
    }

    int key_frame = 0;
    ret = mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &key_frame);
    if (ret != MPP_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get key frame info\n");
        return AVERROR_EXTERNAL;
    }

    if (key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;

    return 0;
}

static int rkmpp_set_hw_frame(AVCodecContext *avctx, MppFrame frame)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    AVBufferRef *hw_ref = ctx->frame->hw_frames_ctx;
    int ret;

    if (!hw_ref)
        return AVERROR(EINVAL);

    AVHWFramesContext *hwframes = (AVHWFramesContext *)hw_ref->data;
    if (hwframes->sw_format != AV_PIX_FMT_NV12)
        return AVERROR(EINVAL);


    const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)ctx->frame->data[0];
    const AVDRMLayerDescriptor *layer = &desc->layers[0];

    int stride = layer->planes[0].pitch;
    int vertical = layer->planes[1].offset / stride;
    if (stride != ctx->mpp_stride || vertical != ctx->mpp_height) {
        // Update stride info
        ctx->mpp_stride = stride;
        ctx->mpp_height = vertical;
        mpp_enc_cfg_set_s32(ctx->cfg, "prep:hor_stride", ctx->mpp_stride);
        mpp_enc_cfg_set_s32(ctx->cfg, "prep:ver_stride", ctx->mpp_height);
        ret = ctx->mpi->control(ctx->enc, MPP_ENC_SET_CFG, ctx->cfg);
        if (ret != MPP_OK) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set config: %d\n", ret);
            return AVERROR_EXTERNAL;
        }
    }
    mpp_frame_set_hor_stride(frame, stride);
    mpp_frame_set_ver_stride(frame, vertical);

    MppBuffer buffer = {0};
    MppBufferInfo info = {
        .type = MPP_BUFFER_TYPE_DRM,
        .size = desc->objects[0].size,
        .fd = desc->objects[0].fd,
    };
    ret = mpp_buffer_import(&buffer, &info);
    if (ret != MPP_OK)
        return AVERROR_EXTERNAL;

    mpp_frame_set_buffer(frame, buffer);
    mpp_buffer_put(buffer);

    return 0;
}

static int rkmpp_set_sw_frame(AVCodecContext *avctx, MppFrame frame)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    AVFrame *f = ctx->frame;

    mpp_buffer_sync_begin(ctx->frame_buf);
    void *buf = mpp_buffer_get_ptr(ctx->frame_buf);

    uint8_t *dst[4] = {NULL};
    int dst_linesizes[4] = {0};
    int ret = av_image_fill_linesizes(dst_linesizes, f->format, ctx->mpp_stride);
    if (ret < 0)
        goto out;
    ret = av_image_fill_pointers(dst, f->format, ctx->mpp_height, buf,
                                 dst_linesizes);
    if (ret < 0)
        goto out;

    av_image_copy2(dst, dst_linesizes, f->data, f->linesize,
                   f->format, f->width, f->height);
    mpp_frame_set_hor_stride(frame, ctx->mpp_stride);
    mpp_frame_set_ver_stride(frame, ctx->mpp_height);

    ret = 0;

out:
    mpp_buffer_sync_end(ctx->frame_buf);
    if (!ret)
        mpp_frame_set_buffer(frame, ctx->frame_buf);

    return ret;
}

static int rkmpp_send_frame(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    MppFrame frame = NULL;
    int ret = 0;

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        ret = AVERROR_EXTERNAL;
        goto out;
    }

    if (ctx->frame->buf[0]) {
        if (ctx->frame->format == AV_PIX_FMT_DRM_PRIME)
            ret = rkmpp_set_hw_frame(avctx, frame);
        else
            ret = rkmpp_set_sw_frame(avctx, frame);

        if (ret < 0)
            goto out;

        mpp_frame_set_fmt(frame, ctx->pix_fmt);
        mpp_frame_set_width(frame, ctx->frame->width);
        mpp_frame_set_height(frame, ctx->frame->height);
        mpp_frame_set_pts(frame, av_rescale_q(ctx->frame->pts,
                        avctx->time_base, RKMPP_TIME_BASE));
    } else {
        mpp_frame_set_buffer(frame, NULL);
        mpp_frame_set_eos(frame, 1);
    }

    ret = ctx->mpi->encode_put_frame(ctx->enc, frame);
    if (ret != MPP_OK)
        ret = AVERROR_EXTERNAL;

out:
    if (frame)
        mpp_frame_deinit(&frame);

    return ret;
}

static int rkmpp_receive(AVCodecContext *avctx, AVPacket *pkt)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;

    while (true) {
        MppPacket packet = NULL;
        int ret = ctx->mpi->encode_get_packet(ctx->enc, &packet);

        if (ret == MPP_OK && packet) {
            ret = rkmpp_output_pkt(avctx, pkt, packet);
            mpp_packet_deinit(&packet);
            return ret;
        }

        if (ctx->eof_sent)
            continue;

        if (!ctx->frame->buf[0]) {
            ret = ff_encode_get_frame(avctx, ctx->frame);
            if (ret < 0 && ret != AVERROR_EOF)
                return ret;
        }

        ret = rkmpp_send_frame(avctx);
        if (ret < 0)
            return ret;

        if (!ctx->frame->buf[0])
            ctx->eof_sent = true;
        else
            av_frame_unref(ctx->frame);
    }
}

static av_cold void rkmpp_flush(AVCodecContext *avctx)
{
    RKMPPEncoderContext *ctx = avctx->priv_data;
    ctx->mpi->reset(ctx->enc);
    ctx->eof_sent = true;
}

static const AVCodecHWConfigInternal *const rkmpp_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(DRM_PRIME, DRM),
    NULL
};

#define OFFSET(x) offsetof(RKMPPEncoderContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption rkmpp_options[] = {
    {"rc", "rate-control mode",
        OFFSET(rc_mode), AV_OPT_TYPE_INT,  { .i64 = MPP_ENC_RC_MODE_VBR }, MPP_ENC_RC_MODE_VBR, INT_MAX, VE, .unit = "rc"},
        {"vbr", "Variable bitrate mode",
            0, AV_OPT_TYPE_CONST, {.i64 = MPP_ENC_RC_MODE_VBR}, 0, 0, VE, .unit = "rc"},
        {"cbr", "Constant bitrate mode",
            0, AV_OPT_TYPE_CONST, {.i64 = MPP_ENC_RC_MODE_CBR}, 0, 0, VE, .unit = "rc"},
        {"avbr", "Adaptive bit rate mode",
            0, AV_OPT_TYPE_CONST, {.i64 = MPP_ENC_RC_MODE_AVBR}, 0, 0, VE, .unit = "rc"},
    {NULL},
};

static const AVClass rkmpp_enc_class = {
    .class_name = "rkmpp_enc",
    .item_name = av_default_item_name,
    .version = LIBAVUTIL_VERSION_INT,
    .option = rkmpp_options,
};

#define RKMPP_ENC(NAME, ID) \
    const FFCodec ff_##NAME##_rkmpp_encoder = { \
        .p.name         = #NAME "_rkmpp", \
        CODEC_LONG_NAME(#NAME " (rkmpp)"), \
        .p.type         = AVMEDIA_TYPE_VIDEO, \
        .p.id           = ID, \
        .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | \
                          AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_ENCODER_FLUSH, \
        .priv_data_size = sizeof(RKMPPEncoderContext), \
        CODEC_PIXFMTS_ARRAY(rkmpp_pix_fmts), \
        .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG, \
        .init           = rkmpp_init_encoder, \
        FF_CODEC_RECEIVE_PACKET_CB(rkmpp_receive), \
        .close          = rkmpp_close_encoder, \
        .flush          = rkmpp_flush, \
        .p.priv_class   = &rkmpp_enc_class, \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP, \
        .p.wrapper_name = "rkmpp", \
        .hw_configs     = rkmpp_hw_configs, \
    };

#if CONFIG_H264_RKMPP_ENCODER
RKMPP_ENC(h264, AV_CODEC_ID_H264)
#endif

#if CONFIG_HEVC_RKMPP_ENCODER
RKMPP_ENC(hevc, AV_CODEC_ID_HEVC)
#endif
