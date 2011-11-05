/*
 * Copyright (c) 2010, Google, Inc.
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

/**
 * @file
 * VP8 encoder support via libvpx
 */

#define VPX_DISABLE_CTRL_TYPECHECKS 1
#define VPX_CODEC_DISABLE_COMPAT    1
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/base64.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

/**
 * Portion of struct vpx_codec_cx_pkt from vpx_encoder.h.
 * One encoded frame returned from the library.
 */
struct FrameListData {
    void *buf;                       /**< compressed data buffer */
    size_t sz;                       /**< length of compressed data */
    int64_t pts;                     /**< time stamp to show frame
                                          (in timebase units) */
    unsigned long duration;          /**< duration to show frame
                                          (in timebase units) */
    uint32_t flags;                  /**< flags for this frame */
    struct FrameListData *next;
};

typedef struct VP8EncoderContext {
    AVClass *class;
    struct vpx_codec_ctx encoder;
    struct vpx_image rawimg;
    struct vpx_fixed_buf twopass_stats;
    int deadline; //i.e., RT/GOOD/BEST
    struct FrameListData *coded_frame_list;

    int cpu_used;
    int auto_alt_ref;

    int arnr_max_frames;
    int arnr_strength;
    int arnr_type;

    int lag_in_frames;
    int error_resilient;
    int crf;
} VP8Context;

/** String mappings for enum vp8e_enc_control_id */
static const char *ctlidstr[] = {
    [VP8E_UPD_ENTROPY]           = "VP8E_UPD_ENTROPY",
    [VP8E_UPD_REFERENCE]         = "VP8E_UPD_REFERENCE",
    [VP8E_USE_REFERENCE]         = "VP8E_USE_REFERENCE",
    [VP8E_SET_ROI_MAP]           = "VP8E_SET_ROI_MAP",
    [VP8E_SET_ACTIVEMAP]         = "VP8E_SET_ACTIVEMAP",
    [VP8E_SET_SCALEMODE]         = "VP8E_SET_SCALEMODE",
    [VP8E_SET_CPUUSED]           = "VP8E_SET_CPUUSED",
    [VP8E_SET_ENABLEAUTOALTREF]  = "VP8E_SET_ENABLEAUTOALTREF",
    [VP8E_SET_NOISE_SENSITIVITY] = "VP8E_SET_NOISE_SENSITIVITY",
    [VP8E_SET_SHARPNESS]         = "VP8E_SET_SHARPNESS",
    [VP8E_SET_STATIC_THRESHOLD]  = "VP8E_SET_STATIC_THRESHOLD",
    [VP8E_SET_TOKEN_PARTITIONS]  = "VP8E_SET_TOKEN_PARTITIONS",
    [VP8E_GET_LAST_QUANTIZER]    = "VP8E_GET_LAST_QUANTIZER",
    [VP8E_SET_ARNR_MAXFRAMES]    = "VP8E_SET_ARNR_MAXFRAMES",
    [VP8E_SET_ARNR_STRENGTH]     = "VP8E_SET_ARNR_STRENGTH",
    [VP8E_SET_ARNR_TYPE]         = "VP8E_SET_ARNR_TYPE",
    [VP8E_SET_CQ_LEVEL]          = "VP8E_SET_CQ_LEVEL",
};

static av_cold void log_encoder_error(AVCodecContext *avctx, const char *desc)
{
    VP8Context *ctx = avctx->priv_data;
    const char *error  = vpx_codec_error(&ctx->encoder);
    const char *detail = vpx_codec_error_detail(&ctx->encoder);

    av_log(avctx, AV_LOG_ERROR, "%s: %s\n", desc, error);
    if (detail)
        av_log(avctx, AV_LOG_ERROR, "  Additional information: %s\n", detail);
}

static av_cold void dump_enc_cfg(AVCodecContext *avctx,
                                 const struct vpx_codec_enc_cfg *cfg)
{
    int width = -30;
    int level = AV_LOG_DEBUG;

    av_log(avctx, level, "vpx_codec_enc_cfg\n");
    av_log(avctx, level, "generic settings\n"
           "  %*s%u\n  %*s%u\n  %*s%u\n  %*s%u\n  %*s%u\n"
           "  %*s{%u/%u}\n  %*s%u\n  %*s%d\n  %*s%u\n",
           width, "g_usage:",           cfg->g_usage,
           width, "g_threads:",         cfg->g_threads,
           width, "g_profile:",         cfg->g_profile,
           width, "g_w:",               cfg->g_w,
           width, "g_h:",               cfg->g_h,
           width, "g_timebase:",        cfg->g_timebase.num, cfg->g_timebase.den,
           width, "g_error_resilient:", cfg->g_error_resilient,
           width, "g_pass:",            cfg->g_pass,
           width, "g_lag_in_frames:",   cfg->g_lag_in_frames);
    av_log(avctx, level, "rate control settings\n"
           "  %*s%u\n  %*s%u\n  %*s%u\n  %*s%u\n"
           "  %*s%d\n  %*s%p(%zu)\n  %*s%u\n",
           width, "rc_dropframe_thresh:",   cfg->rc_dropframe_thresh,
           width, "rc_resize_allowed:",     cfg->rc_resize_allowed,
           width, "rc_resize_up_thresh:",   cfg->rc_resize_up_thresh,
           width, "rc_resize_down_thresh:", cfg->rc_resize_down_thresh,
           width, "rc_end_usage:",          cfg->rc_end_usage,
           width, "rc_twopass_stats_in:",   cfg->rc_twopass_stats_in.buf, cfg->rc_twopass_stats_in.sz,
           width, "rc_target_bitrate:",     cfg->rc_target_bitrate);
    av_log(avctx, level, "quantizer settings\n"
           "  %*s%u\n  %*s%u\n",
           width, "rc_min_quantizer:", cfg->rc_min_quantizer,
           width, "rc_max_quantizer:", cfg->rc_max_quantizer);
    av_log(avctx, level, "bitrate tolerance\n"
           "  %*s%u\n  %*s%u\n",
           width, "rc_undershoot_pct:", cfg->rc_undershoot_pct,
           width, "rc_overshoot_pct:",  cfg->rc_overshoot_pct);
    av_log(avctx, level, "decoder buffer model\n"
            "  %*s%u\n  %*s%u\n  %*s%u\n",
            width, "rc_buf_sz:",         cfg->rc_buf_sz,
            width, "rc_buf_initial_sz:", cfg->rc_buf_initial_sz,
            width, "rc_buf_optimal_sz:", cfg->rc_buf_optimal_sz);
    av_log(avctx, level, "2 pass rate control settings\n"
           "  %*s%u\n  %*s%u\n  %*s%u\n",
           width, "rc_2pass_vbr_bias_pct:",       cfg->rc_2pass_vbr_bias_pct,
           width, "rc_2pass_vbr_minsection_pct:", cfg->rc_2pass_vbr_minsection_pct,
           width, "rc_2pass_vbr_maxsection_pct:", cfg->rc_2pass_vbr_maxsection_pct);
    av_log(avctx, level, "keyframing settings\n"
           "  %*s%d\n  %*s%u\n  %*s%u\n",
           width, "kf_mode:",     cfg->kf_mode,
           width, "kf_min_dist:", cfg->kf_min_dist,
           width, "kf_max_dist:", cfg->kf_max_dist);
    av_log(avctx, level, "\n");
}

static void coded_frame_add(void *list, struct FrameListData *cx_frame)
{
    struct FrameListData **p = list;

    while (*p != NULL)
        p = &(*p)->next;
    *p = cx_frame;
    cx_frame->next = NULL;
}

static av_cold void free_coded_frame(struct FrameListData *cx_frame)
{
    av_freep(&cx_frame->buf);
    av_freep(&cx_frame);
}

static av_cold void free_frame_list(struct FrameListData *list)
{
    struct FrameListData *p = list;

    while (p) {
        list = list->next;
        free_coded_frame(p);
        p = list;
    }
}

static av_cold int codecctl_int(AVCodecContext *avctx,
                                enum vp8e_enc_control_id id, int val)
{
    VP8Context *ctx = avctx->priv_data;
    char buf[80];
    int width = -30;
    int res;

    snprintf(buf, sizeof(buf), "%s:", ctlidstr[id]);
    av_log(avctx, AV_LOG_DEBUG, "  %*s%d\n", width, buf, val);

    res = vpx_codec_control(&ctx->encoder, id, val);
    if (res != VPX_CODEC_OK) {
        snprintf(buf, sizeof(buf), "Failed to set %s codec control",
                 ctlidstr[id]);
        log_encoder_error(avctx, buf);
    }

    return res == VPX_CODEC_OK ? 0 : AVERROR(EINVAL);
}

static av_cold int vp8_free(AVCodecContext *avctx)
{
    VP8Context *ctx = avctx->priv_data;

    vpx_codec_destroy(&ctx->encoder);
    av_freep(&ctx->twopass_stats.buf);
    av_freep(&avctx->coded_frame);
    av_freep(&avctx->stats_out);
    free_frame_list(ctx->coded_frame_list);
    return 0;
}

static av_cold int vp8_init(AVCodecContext *avctx)
{
    VP8Context *ctx = avctx->priv_data;
    const struct vpx_codec_iface *iface = &vpx_codec_vp8_cx_algo;
    struct vpx_codec_enc_cfg enccfg;
    int res;

    av_log(avctx, AV_LOG_INFO, "%s\n", vpx_codec_version_str());
    av_log(avctx, AV_LOG_VERBOSE, "%s\n", vpx_codec_build_config());

    if ((res = vpx_codec_enc_config_default(iface, &enccfg, 0)) != VPX_CODEC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get config: %s\n",
               vpx_codec_err_to_string(res));
        return AVERROR(EINVAL);
    }
    dump_enc_cfg(avctx, &enccfg);

    enccfg.g_w            = avctx->width;
    enccfg.g_h            = avctx->height;
    enccfg.g_timebase.num = avctx->time_base.num;
    enccfg.g_timebase.den = avctx->time_base.den;
    enccfg.g_threads      = avctx->thread_count;
#if FF_API_X264_GLOBAL_OPTS
    if(avctx->rc_lookahead >= 0)
        enccfg.g_lag_in_frames= FFMIN(avctx->rc_lookahead, 25);  //0-25, avoids init failure
    if (ctx->lag_in_frames >= 0)
        enccfg.g_lag_in_frames = ctx->lag_in_frames;
#else
    enccfg.g_lag_in_frames= ctx->lag_in_frames;
#endif

    if (avctx->flags & CODEC_FLAG_PASS1)
        enccfg.g_pass = VPX_RC_FIRST_PASS;
    else if (avctx->flags & CODEC_FLAG_PASS2)
        enccfg.g_pass = VPX_RC_LAST_PASS;
    else
        enccfg.g_pass = VPX_RC_ONE_PASS;

    if (avctx->rc_min_rate == avctx->rc_max_rate &&
        avctx->rc_min_rate == avctx->bit_rate)
        enccfg.rc_end_usage = VPX_CBR;
#if FF_API_X264_GLOBAL_OPTS
    else if (avctx->crf || ctx->crf > 0)
#else
    else if (ctx->crf)
#endif
        enccfg.rc_end_usage = VPX_CQ;
    enccfg.rc_target_bitrate = av_rescale_rnd(avctx->bit_rate, 1, 1000,
                                              AV_ROUND_NEAR_INF);
    if (avctx->qmin > 0)
        enccfg.rc_min_quantizer = avctx->qmin;
    if (avctx->qmax > 0)
        enccfg.rc_max_quantizer = avctx->qmax;
    enccfg.rc_dropframe_thresh = avctx->frame_skip_threshold;

    //0-100 (0 => CBR, 100 => VBR)
    enccfg.rc_2pass_vbr_bias_pct           = round(avctx->qcompress * 100);
    enccfg.rc_2pass_vbr_minsection_pct     =
        avctx->rc_min_rate * 100LL / avctx->bit_rate;
    if (avctx->rc_max_rate)
        enccfg.rc_2pass_vbr_maxsection_pct =
            avctx->rc_max_rate * 100LL / avctx->bit_rate;

    if (avctx->rc_buffer_size)
        enccfg.rc_buf_sz         =
            avctx->rc_buffer_size * 1000LL / avctx->bit_rate;
    if (avctx->rc_initial_buffer_occupancy)
        enccfg.rc_buf_initial_sz =
            avctx->rc_initial_buffer_occupancy * 1000LL / avctx->bit_rate;
    enccfg.rc_buf_optimal_sz     = enccfg.rc_buf_sz * 5 / 6;
    enccfg.rc_undershoot_pct     = round(avctx->rc_buffer_aggressivity * 100);

    //_enc_init() will balk if kf_min_dist differs from max w/VPX_KF_AUTO
    if (avctx->keyint_min >= 0 && avctx->keyint_min == avctx->gop_size)
        enccfg.kf_min_dist = avctx->keyint_min;
    if (avctx->gop_size >= 0)
        enccfg.kf_max_dist = avctx->gop_size;

    if (enccfg.g_pass == VPX_RC_FIRST_PASS)
        enccfg.g_lag_in_frames = 0;
    else if (enccfg.g_pass == VPX_RC_LAST_PASS) {
        int decode_size;

        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No stats file for second pass\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->twopass_stats.sz  = strlen(avctx->stats_in) * 3 / 4;
        ctx->twopass_stats.buf = av_malloc(ctx->twopass_stats.sz);
        if (!ctx->twopass_stats.buf) {
            av_log(avctx, AV_LOG_ERROR,
                   "Stat buffer alloc (%zu bytes) failed\n",
                   ctx->twopass_stats.sz);
            return AVERROR(ENOMEM);
        }
        decode_size = av_base64_decode(ctx->twopass_stats.buf, avctx->stats_in,
                                       ctx->twopass_stats.sz);
        if (decode_size < 0) {
            av_log(avctx, AV_LOG_ERROR, "Stat buffer decode failed\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->twopass_stats.sz      = decode_size;
        enccfg.rc_twopass_stats_in = ctx->twopass_stats;
    }

    /* 0-3: For non-zero values the encoder increasingly optimizes for reduced
       complexity playback on low powered devices at the expense of encode
       quality. */
   if (avctx->profile != FF_PROFILE_UNKNOWN)
       enccfg.g_profile = avctx->profile;

    enccfg.g_error_resilient = ctx->error_resilient;

    dump_enc_cfg(avctx, &enccfg);
    /* Construct Encoder Context */
    res = vpx_codec_enc_init(&ctx->encoder, iface, &enccfg, 0);
    if (res != VPX_CODEC_OK) {
        log_encoder_error(avctx, "Failed to initialize encoder");
        return AVERROR(EINVAL);
    }

    //codec control failures are currently treated only as warnings
    av_log(avctx, AV_LOG_DEBUG, "vpx_codec_control\n");
    if (ctx->cpu_used != INT_MIN)
        codecctl_int(avctx, VP8E_SET_CPUUSED,          ctx->cpu_used);
    if (ctx->auto_alt_ref >= 0)
        codecctl_int(avctx, VP8E_SET_ENABLEAUTOALTREF, ctx->auto_alt_ref);
    if (ctx->arnr_max_frames >= 0)
        codecctl_int(avctx, VP8E_SET_ARNR_MAXFRAMES,   ctx->arnr_max_frames);
    if (ctx->arnr_strength >= 0)
        codecctl_int(avctx, VP8E_SET_ARNR_STRENGTH,    ctx->arnr_strength);
    if (ctx->arnr_type >= 0)
        codecctl_int(avctx, VP8E_SET_ARNR_TYPE,        ctx->arnr_type);
    codecctl_int(avctx, VP8E_SET_NOISE_SENSITIVITY, avctx->noise_reduction);
    codecctl_int(avctx, VP8E_SET_TOKEN_PARTITIONS,  av_log2(avctx->slices));
    codecctl_int(avctx, VP8E_SET_STATIC_THRESHOLD,  avctx->mb_threshold);
#if FF_API_X264_GLOBAL_OPTS
    codecctl_int(avctx, VP8E_SET_CQ_LEVEL,          (int)avctx->crf);
    if (ctx->crf >= 0)
        codecctl_int(avctx, VP8E_SET_CQ_LEVEL,      ctx->crf);
#else
    codecctl_int(avctx, VP8E_SET_CQ_LEVEL,          ctx->crf);
#endif

    av_log(avctx, AV_LOG_DEBUG, "Using deadline: %d\n", ctx->deadline);

    //provide dummy value to initialize wrapper, values will be updated each _encode()
    vpx_img_wrap(&ctx->rawimg, VPX_IMG_FMT_I420, avctx->width, avctx->height, 1,
                 (unsigned char*)1);

    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating coded frame\n");
        vp8_free(avctx);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static inline void cx_pktcpy(struct FrameListData *dst,
                             const struct vpx_codec_cx_pkt *src)
{
    dst->pts      = src->data.frame.pts;
    dst->duration = src->data.frame.duration;
    dst->flags    = src->data.frame.flags;
    dst->sz       = src->data.frame.sz;
    dst->buf      = src->data.frame.buf;
}

/**
 * Store coded frame information in format suitable for return from encode().
 *
 * Write buffer information from @a cx_frame to @a buf & @a buf_size.
 * Timing/frame details to @a coded_frame.
 * @return Frame size written to @a buf on success
 * @return AVERROR(EINVAL) on error
 */
static int storeframe(AVCodecContext *avctx, struct FrameListData *cx_frame,
                      uint8_t *buf, int buf_size, AVFrame *coded_frame)
{
    if ((int) cx_frame->sz <= buf_size) {
        buf_size = cx_frame->sz;
        memcpy(buf, cx_frame->buf, buf_size);
        coded_frame->pts       = cx_frame->pts;
        coded_frame->key_frame = !!(cx_frame->flags & VPX_FRAME_IS_KEY);

        if (coded_frame->key_frame)
            coded_frame->pict_type = AV_PICTURE_TYPE_I;
        else
            coded_frame->pict_type = AV_PICTURE_TYPE_P;
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "Compressed frame larger than storage provided! (%zu/%d)\n",
               cx_frame->sz, buf_size);
        return AVERROR(EINVAL);
    }
    return buf_size;
}

/**
 * Queue multiple output frames from the encoder, returning the front-most.
 * In cases where vpx_codec_get_cx_data() returns more than 1 frame append
 * the frame queue. Return the head frame if available.
 * @return Stored frame size
 * @return AVERROR(EINVAL) on output size error
 * @return AVERROR(ENOMEM) on coded frame queue data allocation error
 */
static int queue_frames(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                        AVFrame *coded_frame)
{
    VP8Context *ctx = avctx->priv_data;
    const struct vpx_codec_cx_pkt *pkt;
    const void *iter = NULL;
    int size = 0;

    if (ctx->coded_frame_list) {
        struct FrameListData *cx_frame = ctx->coded_frame_list;
        /* return the leading frame if we've already begun queueing */
        size = storeframe(avctx, cx_frame, buf, buf_size, coded_frame);
        if (size < 0)
            return AVERROR(EINVAL);
        ctx->coded_frame_list = cx_frame->next;
        free_coded_frame(cx_frame);
    }

    /* consume all available output from the encoder before returning. buffers
       are only good through the next vpx_codec call */
    while ((pkt = vpx_codec_get_cx_data(&ctx->encoder, &iter))) {
        switch (pkt->kind) {
        case VPX_CODEC_CX_FRAME_PKT:
            if (!size) {
                struct FrameListData cx_frame;

                /* avoid storing the frame when the list is empty and we haven't yet
                   provided a frame for output */
                assert(!ctx->coded_frame_list);
                cx_pktcpy(&cx_frame, pkt);
                size = storeframe(avctx, &cx_frame, buf, buf_size, coded_frame);
                if (size < 0)
                    return AVERROR(EINVAL);
            } else {
                struct FrameListData *cx_frame =
                    av_malloc(sizeof(struct FrameListData));

                if (!cx_frame) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Frame queue element alloc failed\n");
                    return AVERROR(ENOMEM);
                }
                cx_pktcpy(cx_frame, pkt);
                cx_frame->buf = av_malloc(cx_frame->sz);

                if (!cx_frame->buf) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Data buffer alloc (%zu bytes) failed\n",
                           cx_frame->sz);
                    return AVERROR(ENOMEM);
                }
                memcpy(cx_frame->buf, pkt->data.frame.buf, pkt->data.frame.sz);
                coded_frame_add(&ctx->coded_frame_list, cx_frame);
            }
            break;
        case VPX_CODEC_STATS_PKT: {
            struct vpx_fixed_buf *stats = &ctx->twopass_stats;
            stats->buf = av_realloc_f(stats->buf, 1,
                                      stats->sz + pkt->data.twopass_stats.sz);
            if (!stats->buf) {
                av_log(avctx, AV_LOG_ERROR, "Stat buffer realloc failed\n");
                return AVERROR(ENOMEM);
            }
            memcpy((uint8_t*)stats->buf + stats->sz,
                   pkt->data.twopass_stats.buf, pkt->data.twopass_stats.sz);
            stats->sz += pkt->data.twopass_stats.sz;
            break;
        }
        case VPX_CODEC_PSNR_PKT: //FIXME add support for CODEC_FLAG_PSNR
        case VPX_CODEC_CUSTOM_PKT:
            //ignore unsupported/unrecognized packet types
            break;
        }
    }

    return size;
}

static int vp8_encode(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                      void *data)
{
    VP8Context *ctx = avctx->priv_data;
    AVFrame *frame = data;
    struct vpx_image *rawimg = NULL;
    int64_t timestamp = 0;
    int res, coded_size;

    if (frame) {
        rawimg                      = &ctx->rawimg;
        rawimg->planes[VPX_PLANE_Y] = frame->data[0];
        rawimg->planes[VPX_PLANE_U] = frame->data[1];
        rawimg->planes[VPX_PLANE_V] = frame->data[2];
        rawimg->stride[VPX_PLANE_Y] = frame->linesize[0];
        rawimg->stride[VPX_PLANE_U] = frame->linesize[1];
        rawimg->stride[VPX_PLANE_V] = frame->linesize[2];
        timestamp                   = frame->pts;
    }

    res = vpx_codec_encode(&ctx->encoder, rawimg, timestamp,
                           avctx->ticks_per_frame, 0, ctx->deadline);
    if (res != VPX_CODEC_OK) {
        log_encoder_error(avctx, "Error encoding frame");
        return AVERROR_INVALIDDATA;
    }
    coded_size = queue_frames(avctx, buf, buf_size, avctx->coded_frame);

    if (!frame && avctx->flags & CODEC_FLAG_PASS1) {
        unsigned int b64_size = AV_BASE64_SIZE(ctx->twopass_stats.sz);

        avctx->stats_out = av_malloc(b64_size);
        if (!avctx->stats_out) {
            av_log(avctx, AV_LOG_ERROR, "Stat buffer alloc (%d bytes) failed\n",
                   b64_size);
            return AVERROR(ENOMEM);
        }
        av_base64_encode(avctx->stats_out, b64_size, ctx->twopass_stats.buf,
                         ctx->twopass_stats.sz);
    }
    return coded_size;
}

#define OFFSET(x) offsetof(VP8Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "cpu-used",        "Quality/Speed ratio modifier",           OFFSET(cpu_used),        AV_OPT_TYPE_INT, {INT_MIN}, INT_MIN, INT_MAX, VE},
    { "auto-alt-ref",    "Enable use of alternate reference "
                         "frames (2-pass only)",                   OFFSET(auto_alt_ref),    AV_OPT_TYPE_INT, {-1},      -1,      1,       VE},
    { "lag-in-frames",   "Number of frames to look ahead for "
                         "alternate reference frame selection",    OFFSET(lag_in_frames),   AV_OPT_TYPE_INT, {-1},      -1,      INT_MAX, VE},
    { "arnr-maxframes",  "altref noise reduction max frame count", OFFSET(arnr_max_frames), AV_OPT_TYPE_INT, {-1},      -1,      INT_MAX, VE},
    { "arnr-strength",   "altref noise reduction filter strength", OFFSET(arnr_strength),   AV_OPT_TYPE_INT, {-1},      -1,      INT_MAX, VE},
    { "arnr-type",       "altref noise reduction filter type",     OFFSET(arnr_type),       AV_OPT_TYPE_INT, {-1},      -1,      INT_MAX, VE, "arnr_type"},
    { "backward",        NULL, 0, AV_OPT_TYPE_CONST, {1}, 0, 0, VE, "arnr_type" },
    { "forward",         NULL, 0, AV_OPT_TYPE_CONST, {2}, 0, 0, VE, "arnr_type" },
    { "centered",        NULL, 0, AV_OPT_TYPE_CONST, {3}, 0, 0, VE, "arnr_type" },
    { "deadline",        "Time to spend encoding, in microseconds.", OFFSET(deadline),      AV_OPT_TYPE_INT, {VPX_DL_GOOD_QUALITY}, INT_MIN, INT_MAX, VE, "quality"},
    { "best",            NULL, 0, AV_OPT_TYPE_CONST, {VPX_DL_BEST_QUALITY}, 0, 0, VE, "quality"},
    { "good",            NULL, 0, AV_OPT_TYPE_CONST, {VPX_DL_GOOD_QUALITY}, 0, 0, VE, "quality"},
    { "realtime",        NULL, 0, AV_OPT_TYPE_CONST, {VPX_DL_REALTIME},     0, 0, VE, "quality"},
    { "error-resilient", "Error resilience configuration", OFFSET(error_resilient), AV_OPT_TYPE_FLAGS, {0}, INT_MIN, INT_MAX, VE, "er"},
#ifdef VPX_ERROR_RESILIENT_DEFAULT
    { "default",         "Improve resiliency against losses of whole frames", 0, AV_OPT_TYPE_CONST, {VPX_ERROR_RESILIENT_DEFAULT}, 0, 0, VE, "er"},
    { "partitions",      "The frame partitions are independently decodable "
                         "by the bool decoder, meaning that partitions can be decoded even "
                         "though earlier partitions have been lost. Note that intra predicition"
                         " is still done over the partition boundary.",       0, AV_OPT_TYPE_CONST, {VPX_ERROR_RESILIENT_PARTITIONS}, 0, 0, VE, "er"},
#endif
{"speed", "", offsetof(VP8Context, cpu_used), AV_OPT_TYPE_INT, {.dbl = 3}, -16, 16, VE},
{"quality", "", offsetof(VP8Context, deadline), AV_OPT_TYPE_INT, {.dbl = VPX_DL_GOOD_QUALITY}, INT_MIN, INT_MAX, VE, "quality"},
{"best", NULL, 0, AV_OPT_TYPE_CONST, {.dbl = VPX_DL_BEST_QUALITY}, INT_MIN, INT_MAX, VE, "quality"},
{"good", NULL, 0, AV_OPT_TYPE_CONST, {.dbl = VPX_DL_GOOD_QUALITY}, INT_MIN, INT_MAX, VE, "quality"},
{"realtime", NULL, 0, AV_OPT_TYPE_CONST, {.dbl = VPX_DL_REALTIME}, INT_MIN, INT_MAX, VE, "quality"},
{"arnr_max_frames", "altref noise reduction max frame count", offsetof(VP8Context, arnr_max_frames), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 15, VE},
{"arnr_strength", "altref noise reduction filter strength", offsetof(VP8Context, arnr_strength), AV_OPT_TYPE_INT, {.dbl = 3}, 0, 6, VE},
{"arnr_type", "altref noise reduction filter type", offsetof(VP8Context, arnr_type), AV_OPT_TYPE_INT, {.dbl = 3}, 1, 3, VE},
#if FF_API_X264_GLOBAL_OPTS
{"rc_lookahead", "Number of frames to look ahead for alternate reference frame selection", offsetof(VP8Context, lag_in_frames), AV_OPT_TYPE_INT, {.dbl = -1}, -1, 25, VE},
{"crf", "Select the quality for constant quality mode", offsetof(VP8Context, crf), AV_OPT_TYPE_INT, {.dbl = -1}, -1, 63, VE},
#else
{"rc_lookahead", "Number of frames to look ahead for alternate reference frame selection", offsetof(VP8Context, lag_in_frames), AV_OPT_TYPE_INT, {.dbl = 25}, 0, 25, VE},
{"crf", "Select the quality for constant quality mode", offsetof(VP8Context, crf), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 63, VE},
#endif
{NULL}
};

static const AVClass class = {
    .class_name = "libvpx encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "qmin",             "-1" },
    { "qmax",             "-1" },
    { "g",                "-1" },
    { "keyint_min",       "-1" },
    { NULL },
};

AVCodec ff_libvpx_encoder = {
    .name           = "libvpx",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VP8,
    .priv_data_size = sizeof(VP8Context),
    .init           = vp8_init,
    .encode         = vp8_encode,
    .close          = vp8_free,
    .capabilities   = CODEC_CAP_DELAY,
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libvpx VP8"),
    .priv_class = &class,
    .defaults       = defaults,
};
