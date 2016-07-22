/*
 * Copyright (c) 2010, Google, Inc.
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
#include "libvpx.h"
#include "libavutil/base64.h"
#include "libavutil/common.h"
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
    unsigned long deadline; //i.e., RT/GOOD/BEST
    struct FrameListData *coded_frame_list;
    int cpu_used;
    int auto_alt_ref;
    int arnr_max_frames;
    int arnr_strength;
    int arnr_type;
    int lag_in_frames;
    int error_resilient;
    int crf;
    int static_thresh;
    int drop_threshold;
    int noise_sensitivity;
} VP8Context;

/** String mappings for enum vp8e_enc_control_id */
static const char *const ctlidstr[] = {
    [VP8E_SET_ARNR_MAXFRAMES]    = "VP8E_SET_ARNR_MAXFRAMES",
    [VP8E_SET_ARNR_STRENGTH]     = "VP8E_SET_ARNR_STRENGTH",
    [VP8E_SET_ARNR_TYPE]         = "VP8E_SET_ARNR_TYPE",
    [VP8E_SET_CPUUSED]           = "VP8E_SET_CPUUSED",
    [VP8E_SET_CQ_LEVEL]          = "VP8E_SET_CQ_LEVEL",
    [VP8E_SET_ENABLEAUTOALTREF]  = "VP8E_SET_ENABLEAUTOALTREF",
    [VP8E_SET_NOISE_SENSITIVITY] = "VP8E_SET_NOISE_SENSITIVITY",
    [VP8E_SET_STATIC_THRESHOLD]  = "VP8E_SET_STATIC_THRESHOLD",
    [VP8E_SET_TOKEN_PARTITIONS]  = "VP8E_SET_TOKEN_PARTITIONS",
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

    while (*p)
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
    av_freep(&avctx->stats_out);
    free_frame_list(ctx->coded_frame_list);
    return 0;
}

static av_cold int vpx_init(AVCodecContext *avctx,
                            const struct vpx_codec_iface *iface)
{
    VP8Context *ctx = avctx->priv_data;
    struct vpx_codec_enc_cfg enccfg = { 0 };
    AVCPBProperties *cpb_props;
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

    if (ctx->lag_in_frames >= 0)
        enccfg.g_lag_in_frames = ctx->lag_in_frames;

    if (avctx->flags & AV_CODEC_FLAG_PASS1)
        enccfg.g_pass = VPX_RC_FIRST_PASS;
    else if (avctx->flags & AV_CODEC_FLAG_PASS2)
        enccfg.g_pass = VPX_RC_LAST_PASS;
    else
        enccfg.g_pass = VPX_RC_ONE_PASS;

    if (!avctx->bit_rate)
        avctx->bit_rate = enccfg.rc_target_bitrate * 1000;
    else
        enccfg.rc_target_bitrate = av_rescale_rnd(avctx->bit_rate, 1, 1000,
                                              AV_ROUND_NEAR_INF);

    if (ctx->crf)
        enccfg.rc_end_usage = VPX_CQ;
    else if (avctx->rc_min_rate == avctx->rc_max_rate &&
             avctx->rc_min_rate == avctx->bit_rate)
        enccfg.rc_end_usage = VPX_CBR;

    if (avctx->qmin > 0)
        enccfg.rc_min_quantizer = avctx->qmin;
    if (avctx->qmax > 0)
        enccfg.rc_max_quantizer = avctx->qmax;

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->frame_skip_threshold)
        ctx->drop_threshold = avctx->frame_skip_threshold;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    enccfg.rc_dropframe_thresh = ctx->drop_threshold;

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

    //_enc_init() will balk if kf_min_dist differs from max w/VPX_KF_AUTO
    if (avctx->keyint_min >= 0 && avctx->keyint_min == avctx->gop_size)
        enccfg.kf_min_dist = avctx->keyint_min;
    if (avctx->gop_size >= 0)
        enccfg.kf_max_dist = avctx->gop_size;

    if (enccfg.g_pass == VPX_RC_FIRST_PASS)
        enccfg.g_lag_in_frames = 0;
    else if (enccfg.g_pass == VPX_RC_LAST_PASS) {
        int decode_size, ret;

        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No stats file for second pass\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->twopass_stats.sz  = strlen(avctx->stats_in) * 3 / 4;
        ret = av_reallocp(&ctx->twopass_stats.buf, ctx->twopass_stats.sz);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Stat buffer alloc (%zu bytes) failed\n",
                   ctx->twopass_stats.sz);
            return ret;
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
    else if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
        avctx->profile = enccfg.g_profile = FF_PROFILE_VP9_0;
    else
        avctx->profile = enccfg.g_profile = FF_PROFILE_VP9_1;

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

    if (CONFIG_LIBVPX_VP8_ENCODER && iface == &vpx_codec_vp8_cx_algo) {
#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
        if (avctx->noise_reduction)
            ctx->noise_sensitivity = avctx->noise_reduction;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        codecctl_int(avctx, VP8E_SET_NOISE_SENSITIVITY, ctx->noise_sensitivity);
        codecctl_int(avctx, VP8E_SET_TOKEN_PARTITIONS,  av_log2(avctx->slices));
    }
#if FF_API_MPV_OPT
    FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->mb_threshold) {
        av_log(avctx, AV_LOG_WARNING, "The mb_threshold option is deprecated, "
               "use the static-thresh private option instead.\n");
        ctx->static_thresh = avctx->mb_threshold;
    }
    FF_ENABLE_DEPRECATION_WARNINGS
#endif
    codecctl_int(avctx, VP8E_SET_STATIC_THRESHOLD,  ctx->static_thresh);
    codecctl_int(avctx, VP8E_SET_CQ_LEVEL,          ctx->crf);

    //provide dummy value to initialize wrapper, values will be updated each _encode()
    vpx_img_wrap(&ctx->rawimg, ff_vpx_pixfmt_to_imgfmt(avctx->pix_fmt),
                 avctx->width, avctx->height, 1, (unsigned char *)1);

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);

    if (enccfg.rc_end_usage == VPX_CBR ||
        enccfg.g_pass != VPX_RC_ONE_PASS) {
        cpb_props->max_bitrate = avctx->rc_max_rate;
        cpb_props->min_bitrate = avctx->rc_min_rate;
        cpb_props->avg_bitrate = avctx->bit_rate;
    }
    cpb_props->buffer_size = avctx->rc_buffer_size;

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
 * Store coded frame information in format suitable for return from encode2().
 *
 * Write information from @a cx_frame to @a pkt
 * @return packet data size on success
 * @return a negative AVERROR on error
 */
static int storeframe(AVCodecContext *avctx, struct FrameListData *cx_frame,
                      AVPacket *pkt)
{
    int ret = ff_alloc_packet(pkt, cx_frame->sz);
    if (ret >= 0) {
        memcpy(pkt->data, cx_frame->buf, pkt->size);
        pkt->pts = pkt->dts = cx_frame->pts;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->coded_frame->pts       = cx_frame->pts;
        avctx->coded_frame->key_frame = !!(cx_frame->flags & VPX_FRAME_IS_KEY);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (!!(cx_frame->flags & VPX_FRAME_IS_KEY)) {
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            pkt->flags |= AV_PKT_FLAG_KEY;
        } else {
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "Error getting output packet of size %zu.\n", cx_frame->sz);
        return ret;
    }
    return pkt->size;
}

/**
 * Queue multiple output frames from the encoder, returning the front-most.
 * In cases where vpx_codec_get_cx_data() returns more than 1 frame append
 * the frame queue. Return the head frame if available.
 * @return Stored frame size
 * @return AVERROR(EINVAL) on output size error
 * @return AVERROR(ENOMEM) on coded frame queue data allocation error
 */
static int queue_frames(AVCodecContext *avctx, AVPacket *pkt_out)
{
    VP8Context *ctx = avctx->priv_data;
    const struct vpx_codec_cx_pkt *pkt;
    const void *iter = NULL;
    int size = 0;

    if (ctx->coded_frame_list) {
        struct FrameListData *cx_frame = ctx->coded_frame_list;
        /* return the leading frame if we've already begun queueing */
        size = storeframe(avctx, cx_frame, pkt_out);
        if (size < 0)
            return size;
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
                size = storeframe(avctx, &cx_frame, pkt_out);
                if (size < 0)
                    return size;
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
                    av_freep(&cx_frame);
                    return AVERROR(ENOMEM);
                }
                memcpy(cx_frame->buf, pkt->data.frame.buf, pkt->data.frame.sz);
                coded_frame_add(&ctx->coded_frame_list, cx_frame);
            }
            break;
        case VPX_CODEC_STATS_PKT: {
            struct vpx_fixed_buf *stats = &ctx->twopass_stats;
            int err;
            if ((err = av_reallocp(&stats->buf,
                                   stats->sz +
                                   pkt->data.twopass_stats.sz)) < 0) {
                stats->sz = 0;
                av_log(avctx, AV_LOG_ERROR, "Stat buffer realloc failed\n");
                return err;
            }
            memcpy((uint8_t*)stats->buf + stats->sz,
                   pkt->data.twopass_stats.buf, pkt->data.twopass_stats.sz);
            stats->sz += pkt->data.twopass_stats.sz;
            break;
        }
        case VPX_CODEC_PSNR_PKT: //FIXME add support for AV_CODEC_FLAG_PSNR
        case VPX_CODEC_CUSTOM_PKT:
            //ignore unsupported/unrecognized packet types
            break;
        }
    }

    return size;
}

static int vp8_encode(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    VP8Context *ctx = avctx->priv_data;
    struct vpx_image *rawimg = NULL;
    int64_t timestamp = 0;
    int res, coded_size;
    vpx_enc_frame_flags_t flags = 0;

    if (frame) {
        rawimg                      = &ctx->rawimg;
        rawimg->planes[VPX_PLANE_Y] = frame->data[0];
        rawimg->planes[VPX_PLANE_U] = frame->data[1];
        rawimg->planes[VPX_PLANE_V] = frame->data[2];
        rawimg->stride[VPX_PLANE_Y] = frame->linesize[0];
        rawimg->stride[VPX_PLANE_U] = frame->linesize[1];
        rawimg->stride[VPX_PLANE_V] = frame->linesize[2];
        timestamp                   = frame->pts;
#if VPX_IMAGE_ABI_VERSION >= 4
        switch (frame->color_range) {
        case AVCOL_RANGE_MPEG:
            rawimg->range = VPX_CR_STUDIO_RANGE;
            break;
        case AVCOL_RANGE_JPEG:
            rawimg->range = VPX_CR_FULL_RANGE;
            break;
        }
#endif
        if (frame->pict_type == AV_PICTURE_TYPE_I)
            flags |= VPX_EFLAG_FORCE_KF;
    }

    res = vpx_codec_encode(&ctx->encoder, rawimg, timestamp,
                           avctx->ticks_per_frame, flags, ctx->deadline);
    if (res != VPX_CODEC_OK) {
        log_encoder_error(avctx, "Error encoding frame");
        return AVERROR_INVALIDDATA;
    }
    coded_size = queue_frames(avctx, pkt);

    if (!frame && avctx->flags & AV_CODEC_FLAG_PASS1) {
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

    *got_packet = !!coded_size;
    return 0;
}

#define OFFSET(x) offsetof(VP8Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "cpu-used",        "Quality/Speed ratio modifier",           OFFSET(cpu_used),        AV_OPT_TYPE_INT, {.i64 = 1}, INT_MIN, INT_MAX, VE},
    { "auto-alt-ref",    "Enable use of alternate reference "
                         "frames (2-pass only)",                   OFFSET(auto_alt_ref),    AV_OPT_TYPE_INT, {.i64 = -1},      -1,      1,       VE},
    { "lag-in-frames",   "Number of frames to look ahead for "
                         "alternate reference frame selection",    OFFSET(lag_in_frames),   AV_OPT_TYPE_INT, {.i64 = -1},      -1,      INT_MAX, VE},
    { "arnr-maxframes",  "altref noise reduction max frame count", OFFSET(arnr_max_frames), AV_OPT_TYPE_INT, {.i64 = -1},      -1,      INT_MAX, VE},
    { "arnr-strength",   "altref noise reduction filter strength", OFFSET(arnr_strength),   AV_OPT_TYPE_INT, {.i64 = -1},      -1,      INT_MAX, VE},
    { "arnr-type",       "altref noise reduction filter type",     OFFSET(arnr_type),       AV_OPT_TYPE_INT, {.i64 = -1},      -1,      INT_MAX, VE, "arnr_type"},
    { "backward",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, 0, 0, VE, "arnr_type" },
    { "forward",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2}, 0, 0, VE, "arnr_type" },
    { "centered",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 3}, 0, 0, VE, "arnr_type" },
    { "deadline",        "Time to spend encoding, in microseconds.", OFFSET(deadline),      AV_OPT_TYPE_INT, {.i64 = VPX_DL_GOOD_QUALITY}, INT_MIN, INT_MAX, VE, "quality"},
    { "best",            NULL, 0, AV_OPT_TYPE_CONST, {.i64 = VPX_DL_BEST_QUALITY}, 0, 0, VE, "quality"},
    { "good",            NULL, 0, AV_OPT_TYPE_CONST, {.i64 = VPX_DL_GOOD_QUALITY}, 0, 0, VE, "quality"},
    { "realtime",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = VPX_DL_REALTIME},     0, 0, VE, "quality"},
    { "error-resilient", "Error resilience configuration", OFFSET(error_resilient), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, VE, "er"},
#ifdef VPX_ERROR_RESILIENT_DEFAULT
    { "default",         "Improve resiliency against losses of whole frames", 0, AV_OPT_TYPE_CONST, {.i64 = VPX_ERROR_RESILIENT_DEFAULT}, 0, 0, VE, "er"},
    { "partitions",      "The frame partitions are independently decodable "
                         "by the bool decoder, meaning that partitions can be decoded even "
                         "though earlier partitions have been lost. Note that intra predicition"
                         " is still done over the partition boundary.",       0, AV_OPT_TYPE_CONST, {.i64 = VPX_ERROR_RESILIENT_PARTITIONS}, 0, 0, VE, "er"},
#endif
    { "crf",              "Select the quality for constant quality mode", offsetof(VP8Context, crf), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 63, VE },
    { "static-thresh",    "A change threshold on blocks below which they will be skipped by the encoder", OFFSET(static_thresh), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "drop-threshold",   "Frame drop threshold", offsetof(VP8Context, drop_threshold), AV_OPT_TYPE_INT, {.i64 = 0 }, INT_MIN, INT_MAX, VE },
    { "noise-sensitivity", "Noise sensitivity", OFFSET(noise_sensitivity), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 4, VE},
    { NULL }
};

static const AVCodecDefault defaults[] = {
    { "qmin",             "-1" },
    { "qmax",             "-1" },
    { "g",                "-1" },
    { "keyint_min",       "-1" },
    { NULL },
};

#if CONFIG_LIBVPX_VP8_ENCODER
static av_cold int vp8_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp8_cx_algo);
}

static const AVClass class_vp8 = {
    .class_name = "libvpx encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libvpx_vp8_encoder = {
    .name           = "libvpx",
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP8"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP8,
    .priv_data_size = sizeof(VP8Context),
    .init           = vp8_init,
    .encode2        = vp8_encode,
    .close          = vp8_free,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &class_vp8,
    .defaults       = defaults,
};
#endif /* CONFIG_LIBVPX_VP8_ENCODER */

#if CONFIG_LIBVPX_VP9_ENCODER
static av_cold int vp9_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp9_cx_algo);
}

static const AVClass class_vp9 = {
    .class_name = "libvpx encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVProfile profiles[] = {
    { FF_PROFILE_VP9_0, "Profile 0" },
    { FF_PROFILE_VP9_1, "Profile 1" },
    { FF_PROFILE_VP9_2, "Profile 2" },
    { FF_PROFILE_VP9_3, "Profile 3" },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_libvpx_vp9_encoder = {
    .name           = "libvpx-vp9",
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP9"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VP8Context),
    .init           = vp9_init,
    .encode2        = vp8_encode,
    .close          = vp8_free,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
#if VPX_IMAGE_ABI_VERSION >= 3
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV440P,
#endif
        AV_PIX_FMT_NONE,
    },
    .profiles       = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class     = &class_vp9,
    .defaults       = defaults,
};
#endif /* CONFIG_LIBVPX_VP9_ENCODER */
