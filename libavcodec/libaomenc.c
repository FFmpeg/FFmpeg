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
 * AV1 encoder support via libaom
 */

#define AOM_DISABLE_CTRL_TYPECHECKS 1
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>

#include "libavutil/avassert.h"
#include "libavutil/base64.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "av1.h"
#include "avcodec.h"
#include "internal.h"
#include "profiles.h"

/*
 * Portion of struct aom_codec_cx_pkt from aom_encoder.h.
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
    uint64_t sse[4];
    int have_sse;                    /**< true if we have pending sse[] */
    uint64_t frame_number;
    struct FrameListData *next;
};

typedef struct AOMEncoderContext {
    AVClass *class;
    AVBSFContext *bsf;
    struct aom_codec_ctx encoder;
    struct aom_image rawimg;
    struct aom_fixed_buf twopass_stats;
    struct FrameListData *coded_frame_list;
    int cpu_used;
    int auto_alt_ref;
    int lag_in_frames;
    int error_resilient;
    int crf;
    int static_thresh;
    int drop_threshold;
    int noise_sensitivity;
    uint64_t sse[4];
    int have_sse; /**< true if we have pending sse[] */
    uint64_t frame_number;
    int tile_cols, tile_rows;
    int tile_cols_log2, tile_rows_log2;
    aom_superblock_size_t superblock_size;
    int uniform_tiles;
} AOMContext;

static const char *const ctlidstr[] = {
    [AOME_SET_CPUUSED]          = "AOME_SET_CPUUSED",
    [AOME_SET_CQ_LEVEL]         = "AOME_SET_CQ_LEVEL",
    [AOME_SET_ENABLEAUTOALTREF] = "AOME_SET_ENABLEAUTOALTREF",
    [AOME_SET_STATIC_THRESHOLD] = "AOME_SET_STATIC_THRESHOLD",
    [AV1E_SET_COLOR_RANGE]      = "AV1E_SET_COLOR_RANGE",
    [AV1E_SET_COLOR_PRIMARIES]  = "AV1E_SET_COLOR_PRIMARIES",
    [AV1E_SET_MATRIX_COEFFICIENTS] = "AV1E_SET_MATRIX_COEFFICIENTS",
    [AV1E_SET_TRANSFER_CHARACTERISTICS] = "AV1E_SET_TRANSFER_CHARACTERISTICS",
    [AV1E_SET_SUPERBLOCK_SIZE]  = "AV1E_SET_SUPERBLOCK_SIZE",
    [AV1E_SET_TILE_COLUMNS]     = "AV1E_SET_TILE_COLUMNS",
    [AV1E_SET_TILE_ROWS]        = "AV1E_SET_TILE_ROWS",
};

static av_cold void log_encoder_error(AVCodecContext *avctx, const char *desc)
{
    AOMContext *ctx    = avctx->priv_data;
    const char *error  = aom_codec_error(&ctx->encoder);
    const char *detail = aom_codec_error_detail(&ctx->encoder);

    av_log(avctx, AV_LOG_ERROR, "%s: %s\n", desc, error);
    if (detail)
        av_log(avctx, AV_LOG_ERROR, "  Additional information: %s\n", detail);
}

static av_cold void dump_enc_cfg(AVCodecContext *avctx,
                                 const struct aom_codec_enc_cfg *cfg)
{
    int width = -30;
    int level = AV_LOG_DEBUG;

    av_log(avctx, level, "aom_codec_enc_cfg\n");
    av_log(avctx, level, "generic settings\n"
                         "  %*s%u\n  %*s%u\n  %*s%u\n  %*s%u\n  %*s%u\n"
                         "  %*s%u\n  %*s%u\n"
                         "  %*s{%u/%u}\n  %*s%u\n  %*s%d\n  %*s%u\n",
           width, "g_usage:",           cfg->g_usage,
           width, "g_threads:",         cfg->g_threads,
           width, "g_profile:",         cfg->g_profile,
           width, "g_w:",               cfg->g_w,
           width, "g_h:",               cfg->g_h,
           width, "g_bit_depth:",       cfg->g_bit_depth,
           width, "g_input_bit_depth:", cfg->g_input_bit_depth,
           width, "g_timebase:",        cfg->g_timebase.num, cfg->g_timebase.den,
           width, "g_error_resilient:", cfg->g_error_resilient,
           width, "g_pass:",            cfg->g_pass,
           width, "g_lag_in_frames:",   cfg->g_lag_in_frames);
    av_log(avctx, level, "rate control settings\n"
                         "  %*s%u\n  %*s%d\n  %*s%p(%"SIZE_SPECIFIER")\n  %*s%u\n",
           width, "rc_dropframe_thresh:", cfg->rc_dropframe_thresh,
           width, "rc_end_usage:",        cfg->rc_end_usage,
           width, "rc_twopass_stats_in:", cfg->rc_twopass_stats_in.buf, cfg->rc_twopass_stats_in.sz,
           width, "rc_target_bitrate:",   cfg->rc_target_bitrate);
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
    av_log(avctx, level, "tile settings\n"
                         "  %*s%d\n  %*s%d\n",
           width, "tile_width_count:",  cfg->tile_width_count,
           width, "tile_height_count:", cfg->tile_height_count);
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
                                enum aome_enc_control_id id, int val)
{
    AOMContext *ctx = avctx->priv_data;
    char buf[80];
    int width = -30;
    int res;

    snprintf(buf, sizeof(buf), "%s:", ctlidstr[id]);
    av_log(avctx, AV_LOG_DEBUG, "  %*s%d\n", width, buf, val);

    res = aom_codec_control(&ctx->encoder, id, val);
    if (res != AOM_CODEC_OK) {
        snprintf(buf, sizeof(buf), "Failed to set %s codec control",
                 ctlidstr[id]);
        log_encoder_error(avctx, buf);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int aom_free(AVCodecContext *avctx)
{
    AOMContext *ctx = avctx->priv_data;

    aom_codec_destroy(&ctx->encoder);
    av_freep(&ctx->twopass_stats.buf);
    av_freep(&avctx->stats_out);
    free_frame_list(ctx->coded_frame_list);
    av_bsf_free(&ctx->bsf);
    return 0;
}

static int set_pix_fmt(AVCodecContext *avctx, aom_codec_caps_t codec_caps,
                       struct aom_codec_enc_cfg *enccfg, aom_codec_flags_t *flags,
                       aom_img_fmt_t *img_fmt)
{
    AOMContext av_unused *ctx = avctx->priv_data;
    enccfg->g_bit_depth = enccfg->g_input_bit_depth = 8;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        enccfg->g_profile = FF_PROFILE_AV1_MAIN;
        *img_fmt = AOM_IMG_FMT_I420;
        return 0;
    case AV_PIX_FMT_YUV422P:
        enccfg->g_profile = FF_PROFILE_AV1_PROFESSIONAL;
        *img_fmt = AOM_IMG_FMT_I422;
        return 0;
    case AV_PIX_FMT_YUV444P:
        enccfg->g_profile = FF_PROFILE_AV1_HIGH;
        *img_fmt = AOM_IMG_FMT_I444;
        return 0;
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV420P12:
        if (codec_caps & AOM_CODEC_CAP_HIGHBITDEPTH) {
            enccfg->g_bit_depth = enccfg->g_input_bit_depth =
                avctx->pix_fmt == AV_PIX_FMT_YUV420P10 ? 10 : 12;
            enccfg->g_profile =
                enccfg->g_bit_depth == 10 ? FF_PROFILE_AV1_MAIN : FF_PROFILE_AV1_PROFESSIONAL;
            *img_fmt = AOM_IMG_FMT_I42016;
            *flags |= AOM_CODEC_USE_HIGHBITDEPTH;
            return 0;
        }
        break;
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV422P12:
        if (codec_caps & AOM_CODEC_CAP_HIGHBITDEPTH) {
            enccfg->g_bit_depth = enccfg->g_input_bit_depth =
                avctx->pix_fmt == AV_PIX_FMT_YUV422P10 ? 10 : 12;
            enccfg->g_profile = FF_PROFILE_AV1_PROFESSIONAL;
            *img_fmt = AOM_IMG_FMT_I42216;
            *flags |= AOM_CODEC_USE_HIGHBITDEPTH;
            return 0;
        }
        break;
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
        if (codec_caps & AOM_CODEC_CAP_HIGHBITDEPTH) {
            enccfg->g_bit_depth = enccfg->g_input_bit_depth =
                avctx->pix_fmt == AV_PIX_FMT_YUV444P10 ? 10 : 12;
            enccfg->g_profile =
                enccfg->g_bit_depth == 10 ? FF_PROFILE_AV1_HIGH : FF_PROFILE_AV1_PROFESSIONAL;
            *img_fmt = AOM_IMG_FMT_I44416;
            *flags |= AOM_CODEC_USE_HIGHBITDEPTH;
            return 0;
        }
        break;
    default:
        break;
    }
    av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
    return AVERROR_INVALIDDATA;
}

static void set_color_range(AVCodecContext *avctx)
{
    enum aom_color_range aom_cr;
    switch (avctx->color_range) {
    case AVCOL_RANGE_UNSPECIFIED:
    case AVCOL_RANGE_MPEG:       aom_cr = AOM_CR_STUDIO_RANGE; break;
    case AVCOL_RANGE_JPEG:       aom_cr = AOM_CR_FULL_RANGE;   break;
    default:
        av_log(avctx, AV_LOG_WARNING, "Unsupported color range (%d)\n",
               avctx->color_range);
        return;
    }

    codecctl_int(avctx, AV1E_SET_COLOR_RANGE, aom_cr);
}

static int count_uniform_tiling(int dim, int sb_size, int tiles_log2)
{
    int sb_dim   = (dim + sb_size - 1) / sb_size;
    int tile_dim = (sb_dim + (1 << tiles_log2) - 1) >> tiles_log2;
    av_assert0(tile_dim > 0);
    return (sb_dim + tile_dim - 1) / tile_dim;
}

static int choose_tiling(AVCodecContext *avctx,
                         struct aom_codec_enc_cfg *enccfg)
{
    AOMContext *ctx = avctx->priv_data;
    int sb_128x128_possible, sb_size, sb_width, sb_height;
    int uniform_rows, uniform_cols;
    int uniform_64x64_possible, uniform_128x128_possible;
    int tile_size, rounding, i;

    if (ctx->tile_cols_log2 >= 0)
        ctx->tile_cols = 1 << ctx->tile_cols_log2;
    if (ctx->tile_rows_log2 >= 0)
        ctx->tile_rows = 1 << ctx->tile_rows_log2;

    if (ctx->tile_cols == 0) {
        ctx->tile_cols = (avctx->width + AV1_MAX_TILE_WIDTH - 1) /
            AV1_MAX_TILE_WIDTH;
        if (ctx->tile_cols > 1) {
            av_log(avctx, AV_LOG_DEBUG, "Automatically using %d tile "
                   "columns to fill width.\n", ctx->tile_cols);
        }
    }
    av_assert0(ctx->tile_cols > 0);
    if (ctx->tile_rows == 0) {
        int max_tile_width =
            FFALIGN((FFALIGN(avctx->width, 128) +
                     ctx->tile_cols - 1) / ctx->tile_cols, 128);
        ctx->tile_rows =
            (max_tile_width * FFALIGN(avctx->height, 128) +
             AV1_MAX_TILE_AREA - 1) / AV1_MAX_TILE_AREA;
        if (ctx->tile_rows > 1) {
            av_log(avctx, AV_LOG_DEBUG, "Automatically using %d tile "
                   "rows to fill area.\n", ctx->tile_rows);
        }
    }
    av_assert0(ctx->tile_rows > 0);

    if ((avctx->width  + 63) / 64 < ctx->tile_cols ||
        (avctx->height + 63) / 64 < ctx->tile_rows) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile sizing: frame not "
               "large enough to fit specified tile arrangement.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->tile_cols > AV1_MAX_TILE_COLS ||
        ctx->tile_rows > AV1_MAX_TILE_ROWS) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile sizing: AV1 does "
               "not allow more than %dx%d tiles.\n",
               AV1_MAX_TILE_COLS, AV1_MAX_TILE_ROWS);
        return AVERROR(EINVAL);
    }
    if (avctx->width / ctx->tile_cols > AV1_MAX_TILE_WIDTH) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile sizing: AV1 does "
               "not allow tiles of width greater than %d.\n",
               AV1_MAX_TILE_WIDTH);
        return AVERROR(EINVAL);
    }

    ctx->superblock_size = AOM_SUPERBLOCK_SIZE_DYNAMIC;

    if (ctx->tile_cols == 1 && ctx->tile_rows == 1) {
        av_log(avctx, AV_LOG_DEBUG, "Using a single tile.\n");
        return 0;
    }

    sb_128x128_possible =
        (avctx->width  + 127) / 128 >= ctx->tile_cols &&
        (avctx->height + 127) / 128 >= ctx->tile_rows;

    ctx->tile_cols_log2 = ctx->tile_cols == 1 ? 0 :
        av_log2(ctx->tile_cols - 1) + 1;
    ctx->tile_rows_log2 = ctx->tile_rows == 1 ? 0 :
        av_log2(ctx->tile_rows - 1) + 1;

    uniform_cols = count_uniform_tiling(avctx->width,
                                        64, ctx->tile_cols_log2);
    uniform_rows = count_uniform_tiling(avctx->height,
                                        64, ctx->tile_rows_log2);
    av_log(avctx, AV_LOG_DEBUG, "Uniform with 64x64 superblocks "
           "-> %dx%d tiles.\n", uniform_cols, uniform_rows);
    uniform_64x64_possible = uniform_cols == ctx->tile_cols &&
                             uniform_rows == ctx->tile_rows;

    if (sb_128x128_possible) {
        uniform_cols = count_uniform_tiling(avctx->width,
                                            128, ctx->tile_cols_log2);
        uniform_rows = count_uniform_tiling(avctx->height,
                                            128, ctx->tile_rows_log2);
        av_log(avctx, AV_LOG_DEBUG, "Uniform with 128x128 superblocks "
               "-> %dx%d tiles.\n", uniform_cols, uniform_rows);
        uniform_128x128_possible = uniform_cols == ctx->tile_cols &&
                                   uniform_rows == ctx->tile_rows;
    } else {
        av_log(avctx, AV_LOG_DEBUG, "128x128 superblocks not possible.\n");
        uniform_128x128_possible = 0;
    }

    ctx->uniform_tiles = 1;
    if (uniform_64x64_possible && uniform_128x128_possible) {
        av_log(avctx, AV_LOG_DEBUG, "Using uniform tiling with dynamic "
               "superblocks (tile_cols_log2 = %d, tile_rows_log2 = %d).\n",
               ctx->tile_cols_log2, ctx->tile_rows_log2);
        return 0;
    }
    if (uniform_64x64_possible && !sb_128x128_possible) {
        av_log(avctx, AV_LOG_DEBUG, "Using uniform tiling with 64x64 "
               "superblocks (tile_cols_log2 = %d, tile_rows_log2 = %d).\n",
               ctx->tile_cols_log2, ctx->tile_rows_log2);
        ctx->superblock_size = AOM_SUPERBLOCK_SIZE_64X64;
        return 0;
    }
    if (uniform_128x128_possible) {
        av_log(avctx, AV_LOG_DEBUG, "Using uniform tiling with 128x128 "
               "superblocks (tile_cols_log2 = %d, tile_rows_log2 = %d).\n",
               ctx->tile_cols_log2, ctx->tile_rows_log2);
        ctx->superblock_size = AOM_SUPERBLOCK_SIZE_128X128;
        return 0;
    }
    ctx->uniform_tiles = 0;

    if (sb_128x128_possible) {
        sb_size = 128;
        ctx->superblock_size = AOM_SUPERBLOCK_SIZE_128X128;
    } else {
        sb_size = 64;
        ctx->superblock_size = AOM_SUPERBLOCK_SIZE_64X64;
    }
    av_log(avctx, AV_LOG_DEBUG, "Using fixed tiling with %dx%d "
           "superblocks (tile_cols = %d, tile_rows = %d).\n",
           sb_size, sb_size, ctx->tile_cols, ctx->tile_rows);

    enccfg->tile_width_count  = ctx->tile_cols;
    enccfg->tile_height_count = ctx->tile_rows;

    sb_width  = (avctx->width  + sb_size - 1) / sb_size;
    sb_height = (avctx->height + sb_size - 1) / sb_size;

    tile_size = sb_width / ctx->tile_cols;
    rounding  = sb_width % ctx->tile_cols;
    for (i = 0; i < ctx->tile_cols; i++) {
        enccfg->tile_widths[i] = tile_size +
            (i < rounding / 2 ||
             i > ctx->tile_cols - 1 - (rounding + 1) / 2);
    }

    tile_size = sb_height / ctx->tile_rows;
    rounding  = sb_height % ctx->tile_rows;
    for (i = 0; i < ctx->tile_rows; i++) {
        enccfg->tile_heights[i] = tile_size +
            (i < rounding / 2 ||
             i > ctx->tile_rows - 1 - (rounding + 1) / 2);
    }

    return 0;
}

static av_cold int aom_init(AVCodecContext *avctx,
                            const struct aom_codec_iface *iface)
{
    AOMContext *ctx = avctx->priv_data;
    struct aom_codec_enc_cfg enccfg = { 0 };
#ifdef AOM_FRAME_IS_INTRAONLY
    aom_codec_flags_t flags =
        (avctx->flags & AV_CODEC_FLAG_PSNR) ? AOM_CODEC_USE_PSNR : 0;
#else
    aom_codec_flags_t flags = 0;
#endif
    AVCPBProperties *cpb_props;
    int res;
    aom_img_fmt_t img_fmt;
    aom_codec_caps_t codec_caps = aom_codec_get_caps(iface);

    av_log(avctx, AV_LOG_INFO, "%s\n", aom_codec_version_str());
    av_log(avctx, AV_LOG_VERBOSE, "%s\n", aom_codec_build_config());

    if ((res = aom_codec_enc_config_default(iface, &enccfg, 0)) != AOM_CODEC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get config: %s\n",
               aom_codec_err_to_string(res));
        return AVERROR(EINVAL);
    }

    if (set_pix_fmt(avctx, codec_caps, &enccfg, &flags, &img_fmt))
        return AVERROR(EINVAL);

    if(!avctx->bit_rate)
        if(avctx->rc_max_rate || avctx->rc_buffer_size || avctx->rc_initial_buffer_occupancy) {
            av_log( avctx, AV_LOG_ERROR, "Rate control parameters set without a bitrate\n");
            return AVERROR(EINVAL);
        }

    dump_enc_cfg(avctx, &enccfg);

    enccfg.g_w            = avctx->width;
    enccfg.g_h            = avctx->height;
    enccfg.g_timebase.num = avctx->time_base.num;
    enccfg.g_timebase.den = avctx->time_base.den;
    enccfg.g_threads      = avctx->thread_count ? avctx->thread_count : av_cpu_count();

    if (ctx->lag_in_frames >= 0)
        enccfg.g_lag_in_frames = ctx->lag_in_frames;

    if (avctx->flags & AV_CODEC_FLAG_PASS1)
        enccfg.g_pass = AOM_RC_FIRST_PASS;
    else if (avctx->flags & AV_CODEC_FLAG_PASS2)
        enccfg.g_pass = AOM_RC_LAST_PASS;
    else
        enccfg.g_pass = AOM_RC_ONE_PASS;

    if (avctx->rc_min_rate == avctx->rc_max_rate &&
        avctx->rc_min_rate == avctx->bit_rate && avctx->bit_rate) {
        enccfg.rc_end_usage = AOM_CBR;
    } else if (ctx->crf >= 0) {
        enccfg.rc_end_usage = AOM_CQ;
        if (!avctx->bit_rate)
            enccfg.rc_end_usage = AOM_Q;
    }

    if (avctx->bit_rate) {
        enccfg.rc_target_bitrate = av_rescale_rnd(avctx->bit_rate, 1, 1000,
                                                  AV_ROUND_NEAR_INF);
    } else if (enccfg.rc_end_usage != AOM_Q) {
        if (enccfg.rc_end_usage == AOM_CQ) {
            enccfg.rc_target_bitrate = 1000000;
        } else {
            avctx->bit_rate = enccfg.rc_target_bitrate * 1000;
            av_log(avctx, AV_LOG_WARNING,
                   "Neither bitrate nor constrained quality specified, using default bitrate of %dkbit/sec\n",
                   enccfg.rc_target_bitrate);
        }
    }

    if (avctx->qmin >= 0)
        enccfg.rc_min_quantizer = avctx->qmin;
    if (avctx->qmax >= 0)
        enccfg.rc_max_quantizer = avctx->qmax;

    if (enccfg.rc_end_usage == AOM_CQ || enccfg.rc_end_usage == AOM_Q) {
        if (ctx->crf < enccfg.rc_min_quantizer || ctx->crf > enccfg.rc_max_quantizer) {
            av_log(avctx, AV_LOG_ERROR,
                   "CQ level %d must be between minimum and maximum quantizer value (%d-%d)\n",
                   ctx->crf, enccfg.rc_min_quantizer, enccfg.rc_max_quantizer);
            return AVERROR(EINVAL);
        }
    }

    enccfg.rc_dropframe_thresh = ctx->drop_threshold;

    // 0-100 (0 => CBR, 100 => VBR)
    enccfg.rc_2pass_vbr_bias_pct       = round(avctx->qcompress * 100);
    if (avctx->bit_rate)
        enccfg.rc_2pass_vbr_minsection_pct =
            avctx->rc_min_rate * 100LL / avctx->bit_rate;
    if (avctx->rc_max_rate)
        enccfg.rc_2pass_vbr_maxsection_pct =
            avctx->rc_max_rate * 100LL / avctx->bit_rate;

    if (avctx->rc_buffer_size)
        enccfg.rc_buf_sz =
            avctx->rc_buffer_size * 1000LL / avctx->bit_rate;
    if (avctx->rc_initial_buffer_occupancy)
        enccfg.rc_buf_initial_sz =
            avctx->rc_initial_buffer_occupancy * 1000LL / avctx->bit_rate;
    enccfg.rc_buf_optimal_sz = enccfg.rc_buf_sz * 5 / 6;

    // _enc_init() will balk if kf_min_dist differs from max w/AOM_KF_AUTO
    if (avctx->keyint_min >= 0 && avctx->keyint_min == avctx->gop_size)
        enccfg.kf_min_dist = avctx->keyint_min;
    if (avctx->gop_size >= 0)
        enccfg.kf_max_dist = avctx->gop_size;

    if (enccfg.g_pass == AOM_RC_FIRST_PASS)
        enccfg.g_lag_in_frames = 0;
    else if (enccfg.g_pass == AOM_RC_LAST_PASS) {
        int decode_size, ret;

        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No stats file for second pass\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->twopass_stats.sz = strlen(avctx->stats_in) * 3 / 4;
        ret                   = av_reallocp(&ctx->twopass_stats.buf, ctx->twopass_stats.sz);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Stat buffer alloc (%"SIZE_SPECIFIER" bytes) failed\n",
                   ctx->twopass_stats.sz);
            ctx->twopass_stats.sz = 0;
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
     * complexity playback on low powered devices at the expense of encode
     * quality. */
    if (avctx->profile != FF_PROFILE_UNKNOWN)
        enccfg.g_profile = avctx->profile;

    enccfg.g_error_resilient = ctx->error_resilient;

    res = choose_tiling(avctx, &enccfg);
    if (res < 0)
        return res;

    dump_enc_cfg(avctx, &enccfg);
    /* Construct Encoder Context */
    res = aom_codec_enc_init(&ctx->encoder, iface, &enccfg, flags);
    if (res != AOM_CODEC_OK) {
        log_encoder_error(avctx, "Failed to initialize encoder");
        return AVERROR(EINVAL);
    }

    // codec control failures are currently treated only as warnings
    av_log(avctx, AV_LOG_DEBUG, "aom_codec_control\n");
    codecctl_int(avctx, AOME_SET_CPUUSED, ctx->cpu_used);
    if (ctx->auto_alt_ref >= 0)
        codecctl_int(avctx, AOME_SET_ENABLEAUTOALTREF, ctx->auto_alt_ref);

    codecctl_int(avctx, AOME_SET_STATIC_THRESHOLD, ctx->static_thresh);
    if (ctx->crf >= 0)
        codecctl_int(avctx, AOME_SET_CQ_LEVEL,          ctx->crf);

    codecctl_int(avctx, AV1E_SET_COLOR_PRIMARIES, avctx->color_primaries);
    codecctl_int(avctx, AV1E_SET_MATRIX_COEFFICIENTS, avctx->colorspace);
    codecctl_int(avctx, AV1E_SET_TRANSFER_CHARACTERISTICS, avctx->color_trc);
    set_color_range(avctx);

    codecctl_int(avctx, AV1E_SET_SUPERBLOCK_SIZE, ctx->superblock_size);
    if (ctx->uniform_tiles) {
        codecctl_int(avctx, AV1E_SET_TILE_COLUMNS, ctx->tile_cols_log2);
        codecctl_int(avctx, AV1E_SET_TILE_ROWS,    ctx->tile_rows_log2);
    }

    // provide dummy value to initialize wrapper, values will be updated each _encode()
    aom_img_wrap(&ctx->rawimg, img_fmt, avctx->width, avctx->height, 1,
                 (unsigned char*)1);

    if (codec_caps & AOM_CODEC_CAP_HIGHBITDEPTH)
        ctx->rawimg.bit_depth = enccfg.g_bit_depth;

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        const AVBitStreamFilter *filter = av_bsf_get_by_name("extract_extradata");
        int ret;

        if (!filter) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata bitstream filter "
                   "not found. This is a bug, please report it.\n");
            return AVERROR_BUG;
        }
        ret = av_bsf_alloc(filter, &ctx->bsf);
        if (ret < 0)
            return ret;

        ret = avcodec_parameters_from_context(ctx->bsf->par_in, avctx);
        if (ret < 0)
           return ret;

        ret = av_bsf_init(ctx->bsf);
        if (ret < 0)
           return ret;
    }

    if (enccfg.rc_end_usage == AOM_CBR ||
        enccfg.g_pass != AOM_RC_ONE_PASS) {
        cpb_props->max_bitrate = avctx->rc_max_rate;
        cpb_props->min_bitrate = avctx->rc_min_rate;
        cpb_props->avg_bitrate = avctx->bit_rate;
    }
    cpb_props->buffer_size = avctx->rc_buffer_size;

    return 0;
}

static inline void cx_pktcpy(AOMContext *ctx,
                             struct FrameListData *dst,
                             const struct aom_codec_cx_pkt *src)
{
    dst->pts      = src->data.frame.pts;
    dst->duration = src->data.frame.duration;
    dst->flags    = src->data.frame.flags;
    dst->sz       = src->data.frame.sz;
    dst->buf      = src->data.frame.buf;
#ifdef AOM_FRAME_IS_INTRAONLY
    dst->have_sse = 0;
    dst->frame_number = ++ctx->frame_number;
    dst->have_sse = ctx->have_sse;
    if (ctx->have_sse) {
        /* associate last-seen SSE to the frame. */
        /* Transfers ownership from ctx to dst. */
        memcpy(dst->sse, ctx->sse, sizeof(dst->sse));
        ctx->have_sse = 0;
    }
#endif
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
    AOMContext *ctx = avctx->priv_data;
    int pict_type;
    int ret = ff_alloc_packet2(avctx, pkt, cx_frame->sz, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Error getting output packet of size %"SIZE_SPECIFIER".\n", cx_frame->sz);
        return ret;
    }
    memcpy(pkt->data, cx_frame->buf, pkt->size);
    pkt->pts = pkt->dts = cx_frame->pts;

    if (!!(cx_frame->flags & AOM_FRAME_IS_KEY)) {
        pkt->flags |= AV_PKT_FLAG_KEY;
#ifdef AOM_FRAME_IS_INTRAONLY
        pict_type = AV_PICTURE_TYPE_I;
    } else if (cx_frame->flags & AOM_FRAME_IS_INTRAONLY) {
        pict_type = AV_PICTURE_TYPE_I;
    } else {
        pict_type = AV_PICTURE_TYPE_P;
    }

    ff_side_data_set_encoder_stats(pkt, 0, cx_frame->sse + 1,
                                   cx_frame->have_sse ? 3 : 0, pict_type);

    if (cx_frame->have_sse) {
        int i;
        for (i = 0; i < 3; ++i) {
            avctx->error[i] += cx_frame->sse[i + 1];
        }
        cx_frame->have_sse = 0;
#endif
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        ret = av_bsf_send_packet(ctx->bsf, pkt);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata filter "
                   "failed to send input packet\n");
            return ret;
        }
        ret = av_bsf_receive_packet(ctx->bsf, pkt);

        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata filter "
                   "failed to receive output packet\n");
            return ret;
        }
    }
    return pkt->size;
}

/**
 * Queue multiple output frames from the encoder, returning the front-most.
 * In cases where aom_codec_get_cx_data() returns more than 1 frame append
 * the frame queue. Return the head frame if available.
 * @return Stored frame size
 * @return AVERROR(EINVAL) on output size error
 * @return AVERROR(ENOMEM) on coded frame queue data allocation error
 */
static int queue_frames(AVCodecContext *avctx, AVPacket *pkt_out)
{
    AOMContext *ctx = avctx->priv_data;
    const struct aom_codec_cx_pkt *pkt;
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
     * are only good through the next aom_codec call */
    while ((pkt = aom_codec_get_cx_data(&ctx->encoder, &iter))) {
        switch (pkt->kind) {
        case AOM_CODEC_CX_FRAME_PKT:
            if (!size) {
                struct FrameListData cx_frame;

                /* avoid storing the frame when the list is empty and we haven't yet
                 * provided a frame for output */
                av_assert0(!ctx->coded_frame_list);
                cx_pktcpy(ctx, &cx_frame, pkt);
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
                cx_pktcpy(ctx, cx_frame, pkt);
                cx_frame->buf = av_malloc(cx_frame->sz);

                if (!cx_frame->buf) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Data buffer alloc (%"SIZE_SPECIFIER" bytes) failed\n",
                           cx_frame->sz);
                    av_freep(&cx_frame);
                    return AVERROR(ENOMEM);
                }
                memcpy(cx_frame->buf, pkt->data.frame.buf, pkt->data.frame.sz);
                coded_frame_add(&ctx->coded_frame_list, cx_frame);
            }
            break;
        case AOM_CODEC_STATS_PKT:
        {
            struct aom_fixed_buf *stats = &ctx->twopass_stats;
            int err;
            if ((err = av_reallocp(&stats->buf,
                                   stats->sz +
                                   pkt->data.twopass_stats.sz)) < 0) {
                stats->sz = 0;
                av_log(avctx, AV_LOG_ERROR, "Stat buffer realloc failed\n");
                return err;
            }
            memcpy((uint8_t *)stats->buf + stats->sz,
                   pkt->data.twopass_stats.buf, pkt->data.twopass_stats.sz);
            stats->sz += pkt->data.twopass_stats.sz;
            break;
        }
#ifdef AOM_FRAME_IS_INTRAONLY
        case AOM_CODEC_PSNR_PKT:
        {
            av_assert0(!ctx->have_sse);
            ctx->sse[0] = pkt->data.psnr.sse[0];
            ctx->sse[1] = pkt->data.psnr.sse[1];
            ctx->sse[2] = pkt->data.psnr.sse[2];
            ctx->sse[3] = pkt->data.psnr.sse[3];
            ctx->have_sse = 1;
            break;
        }
#endif
        case AOM_CODEC_CUSTOM_PKT:
            // ignore unsupported/unrecognized packet types
            break;
        }
    }

    return size;
}

static int aom_encode(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    AOMContext *ctx = avctx->priv_data;
    struct aom_image *rawimg = NULL;
    int64_t timestamp = 0;
    int res, coded_size;
    aom_enc_frame_flags_t flags = 0;

    if (frame) {
        rawimg                      = &ctx->rawimg;
        rawimg->planes[AOM_PLANE_Y] = frame->data[0];
        rawimg->planes[AOM_PLANE_U] = frame->data[1];
        rawimg->planes[AOM_PLANE_V] = frame->data[2];
        rawimg->stride[AOM_PLANE_Y] = frame->linesize[0];
        rawimg->stride[AOM_PLANE_U] = frame->linesize[1];
        rawimg->stride[AOM_PLANE_V] = frame->linesize[2];
        timestamp                   = frame->pts;
        switch (frame->color_range) {
        case AVCOL_RANGE_MPEG:
            rawimg->range = AOM_CR_STUDIO_RANGE;
            break;
        case AVCOL_RANGE_JPEG:
            rawimg->range = AOM_CR_FULL_RANGE;
            break;
        }

        if (frame->pict_type == AV_PICTURE_TYPE_I)
            flags |= AOM_EFLAG_FORCE_KF;
    }

    res = aom_codec_encode(&ctx->encoder, rawimg, timestamp,
                           avctx->ticks_per_frame, flags);
    if (res != AOM_CODEC_OK) {
        log_encoder_error(avctx, "Error encoding frame");
        return AVERROR_INVALIDDATA;
    }
    coded_size = queue_frames(avctx, pkt);

    if (!frame && avctx->flags & AV_CODEC_FLAG_PASS1) {
        size_t b64_size = AV_BASE64_SIZE(ctx->twopass_stats.sz);

        avctx->stats_out = av_malloc(b64_size);
        if (!avctx->stats_out) {
            av_log(avctx, AV_LOG_ERROR, "Stat buffer alloc (%"SIZE_SPECIFIER" bytes) failed\n",
                   b64_size);
            return AVERROR(ENOMEM);
        }
        av_base64_encode(avctx->stats_out, b64_size, ctx->twopass_stats.buf,
                         ctx->twopass_stats.sz);
    }

    *got_packet = !!coded_size;
    return 0;
}

static const enum AVPixelFormat av1_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat av1_pix_fmts_highbd[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

static av_cold void av1_init_static(AVCodec *codec)
{
    aom_codec_caps_t codec_caps = aom_codec_get_caps(aom_codec_av1_cx());
    if (codec_caps & AOM_CODEC_CAP_HIGHBITDEPTH)
        codec->pix_fmts = av1_pix_fmts_highbd;
    else
        codec->pix_fmts = av1_pix_fmts;
}

static av_cold int av1_init(AVCodecContext *avctx)
{
    return aom_init(avctx, aom_codec_av1_cx());
}

#define OFFSET(x) offsetof(AOMContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "cpu-used",        "Quality/Speed ratio modifier",           OFFSET(cpu_used),        AV_OPT_TYPE_INT, {.i64 = 1}, 0, 8, VE},
    { "auto-alt-ref",    "Enable use of alternate reference "
                         "frames (2-pass only)",                   OFFSET(auto_alt_ref),    AV_OPT_TYPE_INT, {.i64 = -1},      -1,      2,       VE},
    { "lag-in-frames",   "Number of frames to look ahead at for "
                         "alternate reference frame selection",    OFFSET(lag_in_frames),   AV_OPT_TYPE_INT, {.i64 = -1},      -1,      INT_MAX, VE},
    { "error-resilience", "Error resilience configuration", OFFSET(error_resilient), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, VE, "er"},
    { "default",         "Improve resiliency against losses of whole frames", 0, AV_OPT_TYPE_CONST, {.i64 = AOM_ERROR_RESILIENT_DEFAULT}, 0, 0, VE, "er"},
    { "crf",              "Select the quality for constant quality mode", offsetof(AOMContext, crf), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 63, VE },
    { "static-thresh",    "A change threshold on blocks below which they will be skipped by the encoder", OFFSET(static_thresh), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "drop-threshold",   "Frame drop threshold", offsetof(AOMContext, drop_threshold), AV_OPT_TYPE_INT, {.i64 = 0 }, INT_MIN, INT_MAX, VE },
    { "noise-sensitivity", "Noise sensitivity", OFFSET(noise_sensitivity), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 4, VE},
    { "tiles",            "Tile columns x rows", OFFSET(tile_cols), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, 0, VE },
    { "tile-columns",     "Log2 of number of tile columns to use", OFFSET(tile_cols_log2), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 6, VE},
    { "tile-rows",        "Log2 of number of tile rows to use",    OFFSET(tile_rows_log2), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 6, VE},
    { NULL }
};

static const AVCodecDefault defaults[] = {
    { "qmin",             "-1" },
    { "qmax",             "-1" },
    { "g",                "-1" },
    { "keyint_min",       "-1" },
    { NULL },
};

static const AVClass class_aom = {
    .class_name = "libaom-av1 encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libaom_av1_encoder = {
    .name           = "libaom-av1",
    .long_name      = NULL_IF_CONFIG_SMALL("libaom AV1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(AOMContext),
    .init           = av1_init,
    .encode2        = aom_encode,
    .close          = aom_free,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS | AV_CODEC_CAP_EXPERIMENTAL,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_av1_profiles),
    .priv_class     = &class_aom,
    .defaults       = defaults,
    .init_static_data = av1_init_static,
    .wrapper_name   = "libaom",
};
