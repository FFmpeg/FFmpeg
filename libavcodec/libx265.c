/*
 * libx265 encoder
 *
 * Copyright (c) 2013-2014 Derek Buitenhuis
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

#if defined(_MSC_VER)
#define X265_API_IMPORTS 1
#endif

#include <x265.h>
#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/internal.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "dovi_rpu.h"
#include "encode.h"
#include "packet_internal.h"
#include "atsc_a53.h"
#include "sei.h"

typedef struct ReorderedData {
    int64_t duration;

    void        *frame_opaque;
    AVBufferRef *frame_opaque_ref;

    int in_use;
} ReorderedData;

typedef struct libx265Context {
    const AVClass *class;

    x265_encoder *encoder;
    x265_param   *params;
    const x265_api *api;

    float crf;
    int   cqp;
    int   forced_idr;
    char *preset;
    char *tune;
    char *profile;
    AVDictionary *x265_opts;

    void *sei_data;
    int sei_data_size;
    int udu_sei;
    int a53_cc;

    ReorderedData *rd;
    int         nb_rd;

    /**
     * If the encoder does not support ROI then warn the first time we
     * encounter a frame with ROI side data.
     */
    int roi_warned;

    DOVIContext dovi;
} libx265Context;

static int is_keyframe(NalUnitType naltype)
{
    switch (naltype) {
    case NAL_UNIT_CODED_SLICE_BLA_W_LP:
    case NAL_UNIT_CODED_SLICE_BLA_W_RADL:
    case NAL_UNIT_CODED_SLICE_BLA_N_LP:
    case NAL_UNIT_CODED_SLICE_IDR_W_RADL:
    case NAL_UNIT_CODED_SLICE_IDR_N_LP:
    case NAL_UNIT_CODED_SLICE_CRA:
        return 1;
    default:
        return 0;
    }
}

static int rd_get(libx265Context *ctx)
{
    const int add = 16;

    ReorderedData *tmp;
    int idx;

    for (int i = 0; i < ctx->nb_rd; i++)
        if (!ctx->rd[i].in_use) {
            ctx->rd[i].in_use = 1;
            return i;
        }

    tmp = av_realloc_array(ctx->rd, ctx->nb_rd + add, sizeof(*ctx->rd));
    if (!tmp)
        return AVERROR(ENOMEM);
    memset(tmp + ctx->nb_rd, 0, sizeof(*tmp) * add);

    ctx->rd     = tmp;
    ctx->nb_rd += add;

    idx                 = ctx->nb_rd - add;
    ctx->rd[idx].in_use = 1;

    return idx;
}

static void rd_release(libx265Context *ctx, int idx)
{
    av_assert0(idx >= 0 && idx < ctx->nb_rd);
    av_buffer_unref(&ctx->rd[idx].frame_opaque_ref);
    memset(&ctx->rd[idx], 0, sizeof(ctx->rd[idx]));
}

static av_cold int libx265_encode_close(AVCodecContext *avctx)
{
    libx265Context *ctx = avctx->priv_data;

    ctx->api->param_free(ctx->params);
    av_freep(&ctx->sei_data);

    for (int i = 0; i < ctx->nb_rd; i++)
        rd_release(ctx, i);
    av_freep(&ctx->rd);

    if (ctx->encoder)
        ctx->api->encoder_close(ctx->encoder);

    ff_dovi_ctx_unref(&ctx->dovi);

    return 0;
}

static av_cold int libx265_param_parse_float(AVCodecContext *avctx,
                                           const char *key, float value)
{
    libx265Context *ctx = avctx->priv_data;
    char buf[256];

    snprintf(buf, sizeof(buf), "%2.2f", value);
    if (ctx->api->param_parse(ctx->params, key, buf) == X265_PARAM_BAD_VALUE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value %2.2f for param \"%s\".\n", value, key);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int libx265_param_parse_int(AVCodecContext *avctx,
                                           const char *key, int value)
{
    libx265Context *ctx = avctx->priv_data;
    char buf[256];

    snprintf(buf, sizeof(buf), "%d", value);
    if (ctx->api->param_parse(ctx->params, key, buf) == X265_PARAM_BAD_VALUE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value %d for param \"%s\".\n", value, key);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int handle_mdcv(void *logctx, const x265_api *api,
                       x265_param *params,
                       const AVMasteringDisplayMetadata *mdcv)
{
    char buf[10 /* # of PRId64s */ * 20 /* max strlen for %PRId64 */ + sizeof("G(,)B(,)R(,)WP(,)L(,)")];

    // G(%hu,%hu)B(%hu,%hu)R(%hu,%hu)WP(%hu,%hu)L(%u,%u)
    snprintf(buf, sizeof(buf),
        "G(%"PRId64",%"PRId64")B(%"PRId64",%"PRId64")R(%"PRId64",%"PRId64")"
        "WP(%"PRId64",%"PRId64")L(%"PRId64",%"PRId64")",
        av_rescale_q(1, mdcv->display_primaries[1][0], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->display_primaries[1][1], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->display_primaries[2][0], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->display_primaries[2][1], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->display_primaries[0][0], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->display_primaries[0][1], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->white_point[0], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->white_point[1], (AVRational){ 1, 50000 }),
        av_rescale_q(1, mdcv->max_luminance,  (AVRational){ 1, 10000 }),
        av_rescale_q(1, mdcv->min_luminance,  (AVRational){ 1, 10000 }));

    if (api->param_parse(params, "master-display", buf) ==
            X265_PARAM_BAD_VALUE) {
        av_log(logctx, AV_LOG_ERROR,
               "Invalid value \"%s\" for param \"master-display\".\n",
               buf);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int handle_side_data(AVCodecContext *avctx, const x265_api *api,
                            x265_param *params)
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

        params->maxCLL  = cll->MaxCLL;
        params->maxFALL = cll->MaxFALL;
    }

    if (mdcv_sd) {
        int ret = handle_mdcv(
            avctx, api, params,
            (AVMasteringDisplayMetadata *)mdcv_sd->data);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static av_cold int libx265_encode_init(AVCodecContext *avctx)
{
    libx265Context *ctx = avctx->priv_data;
    AVCPBProperties *cpb_props = NULL;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    int ret;

    ctx->api = x265_api_get(desc->comp[0].depth);
    if (!ctx->api)
        ctx->api = x265_api_get(0);

    ctx->params = ctx->api->param_alloc();
    if (!ctx->params) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate x265 param structure.\n");
        return AVERROR(ENOMEM);
    }

    if (ctx->api->param_default_preset(ctx->params, ctx->preset, ctx->tune) < 0) {
        int i;

        av_log(avctx, AV_LOG_ERROR, "Error setting preset/tune %s/%s.\n", ctx->preset, ctx->tune);
        av_log(avctx, AV_LOG_INFO, "Possible presets:");
        for (i = 0; x265_preset_names[i]; i++)
            av_log(avctx, AV_LOG_INFO, " %s", x265_preset_names[i]);

        av_log(avctx, AV_LOG_INFO, "\n");
        av_log(avctx, AV_LOG_INFO, "Possible tunes:");
        for (i = 0; x265_tune_names[i]; i++)
            av_log(avctx, AV_LOG_INFO, " %s", x265_tune_names[i]);

        av_log(avctx, AV_LOG_INFO, "\n");

        return AVERROR(EINVAL);
    }

    ctx->params->frameNumThreads = avctx->thread_count;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        ctx->params->fpsNum      = avctx->framerate.num;
        ctx->params->fpsDenom    = avctx->framerate.den;
    } else {
        ctx->params->fpsNum      = avctx->time_base.den;
FF_DISABLE_DEPRECATION_WARNINGS
        ctx->params->fpsDenom    = avctx->time_base.num
#if FF_API_TICKS_PER_FRAME
                                   * avctx->ticks_per_frame
#endif
                                   ;
FF_ENABLE_DEPRECATION_WARNINGS
    }
    ctx->params->sourceWidth     = avctx->width;
    ctx->params->sourceHeight    = avctx->height;
    ctx->params->bEnablePsnr     = !!(avctx->flags & AV_CODEC_FLAG_PSNR);
    ctx->params->bOpenGOP        = !(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP);

    /* Tune the CTU size based on input resolution. */
    if (ctx->params->sourceWidth < 64 || ctx->params->sourceHeight < 64)
        ctx->params->maxCUSize = 32;
    if (ctx->params->sourceWidth < 32 || ctx->params->sourceHeight < 32)
        ctx->params->maxCUSize = 16;
    if (ctx->params->sourceWidth < 16 || ctx->params->sourceHeight < 16) {
        av_log(avctx, AV_LOG_ERROR, "Image size is too small (%dx%d).\n",
               ctx->params->sourceWidth, ctx->params->sourceHeight);
        return AVERROR(EINVAL);
    }


    ctx->params->vui.bEnableVideoSignalTypePresentFlag = 1;

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED)
        ctx->params->vui.bEnableVideoFullRangeFlag =
            avctx->color_range == AVCOL_RANGE_JPEG;
    else
        ctx->params->vui.bEnableVideoFullRangeFlag =
            (desc->flags & AV_PIX_FMT_FLAG_RGB) ||
            avctx->pix_fmt == AV_PIX_FMT_YUVJ420P ||
            avctx->pix_fmt == AV_PIX_FMT_YUVJ422P ||
            avctx->pix_fmt == AV_PIX_FMT_YUVJ444P;

    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED) {

        ctx->params->vui.bEnableColorDescriptionPresentFlag = 1;

        // x265 validates the parameters internally
        ctx->params->vui.colorPrimaries          = avctx->color_primaries;
        ctx->params->vui.transferCharacteristics = avctx->color_trc;
#if X265_BUILD >= 159
        if (avctx->color_trc == AVCOL_TRC_ARIB_STD_B67)
            ctx->params->preferredTransferCharacteristics = ctx->params->vui.transferCharacteristics;
#endif
        ctx->params->vui.matrixCoeffs            = avctx->colorspace;
    }

    // chroma sample location values are to be ignored in case of non-4:2:0
    // according to the specification, so we only write them out in case of
    // 4:2:0 (log2_chroma_{w,h} == 1).
    ctx->params->vui.bEnableChromaLocInfoPresentFlag =
        avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED &&
        desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1;

    if (ctx->params->vui.bEnableChromaLocInfoPresentFlag) {
        ctx->params->vui.chromaSampleLocTypeTopField =
        ctx->params->vui.chromaSampleLocTypeBottomField =
            avctx->chroma_sample_location - 1;
    }

    if (avctx->sample_aspect_ratio.num > 0 && avctx->sample_aspect_ratio.den > 0) {
        char sar[12];
        int sar_num, sar_den;

        av_reduce(&sar_num, &sar_den,
                  avctx->sample_aspect_ratio.num,
                  avctx->sample_aspect_ratio.den, 65535);
        snprintf(sar, sizeof(sar), "%d:%d", sar_num, sar_den);
        if (ctx->api->param_parse(ctx->params, "sar", sar) == X265_PARAM_BAD_VALUE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid SAR: %d:%d.\n", sar_num, sar_den);
            return AVERROR_INVALIDDATA;
        }
    }

    switch (desc->log2_chroma_w) {
    // 4:4:4, RGB. gray
    case 0:
        // gray
        if (desc->nb_components == 1) {
            if (ctx->api->api_build_number < 85) {
                av_log(avctx, AV_LOG_ERROR,
                       "libx265 version is %d, must be at least 85 for gray encoding.\n",
                       ctx->api->api_build_number);
                return AVERROR_INVALIDDATA;
            }
            ctx->params->internalCsp = X265_CSP_I400;
            break;
        }

        // set identity matrix for RGB
        if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
            ctx->params->vui.matrixCoeffs = AVCOL_SPC_RGB;
            ctx->params->vui.bEnableVideoSignalTypePresentFlag  = 1;
            ctx->params->vui.bEnableColorDescriptionPresentFlag = 1;
        }

        ctx->params->internalCsp = X265_CSP_I444;
        break;
    // 4:2:0, 4:2:2
    case 1:
        ctx->params->internalCsp = desc->log2_chroma_h == 1 ?
            X265_CSP_I420 : X265_CSP_I422;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Pixel format '%s' cannot be mapped to a libx265 CSP!\n",
               desc->name);
        return AVERROR_BUG;
    }

    ret = handle_side_data(avctx, ctx->api, ctx->params);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed handling side data! (%s)\n",
               av_err2str(ret));
        return ret;
    }

    if (ctx->crf >= 0) {
        char crf[6];

        snprintf(crf, sizeof(crf), "%2.2f", ctx->crf);
        if (ctx->api->param_parse(ctx->params, "crf", crf) == X265_PARAM_BAD_VALUE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid crf: %2.2f.\n", ctx->crf);
            return AVERROR(EINVAL);
        }
    } else if (avctx->bit_rate > 0) {
        ctx->params->rc.bitrate         = avctx->bit_rate / 1000;
        ctx->params->rc.rateControlMode = X265_RC_ABR;
    } else if (ctx->cqp >= 0) {
        ret = libx265_param_parse_int(avctx, "qp", ctx->cqp);
        if (ret < 0)
            return ret;
    }

    if (avctx->qmin >= 0) {
        ret = libx265_param_parse_int(avctx, "qpmin", avctx->qmin);
        if (ret < 0)
            return ret;
    }
    if (avctx->qmax >= 0) {
        ret = libx265_param_parse_int(avctx, "qpmax", avctx->qmax);
        if (ret < 0)
            return ret;
    }
    if (avctx->max_qdiff >= 0) {
        ret = libx265_param_parse_int(avctx, "qpstep", avctx->max_qdiff);
        if (ret < 0)
            return ret;
    }
    if (avctx->qblur >= 0) {
        ret = libx265_param_parse_float(avctx, "qblur", avctx->qblur);
        if (ret < 0)
            return ret;
    }
    if (avctx->qcompress >= 0) {
        ret = libx265_param_parse_float(avctx, "qcomp", avctx->qcompress);
        if (ret < 0)
            return ret;
    }
    if (avctx->i_quant_factor >= 0) {
        ret = libx265_param_parse_float(avctx, "ipratio", avctx->i_quant_factor);
        if (ret < 0)
            return ret;
    }
    if (avctx->b_quant_factor >= 0) {
        ret = libx265_param_parse_float(avctx, "pbratio", avctx->b_quant_factor);
        if (ret < 0)
            return ret;
    }

    ctx->params->rc.vbvBufferSize = avctx->rc_buffer_size / 1000;
    ctx->params->rc.vbvMaxBitrate = avctx->rc_max_rate    / 1000;

    cpb_props = ff_encode_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->buffer_size = ctx->params->rc.vbvBufferSize * 1000;
    cpb_props->max_bitrate = ctx->params->rc.vbvMaxBitrate * 1000LL;
    cpb_props->avg_bitrate = ctx->params->rc.bitrate       * 1000LL;

    if (!(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER))
        ctx->params->bRepeatHeaders = 1;

    if (avctx->gop_size >= 0) {
        ret = libx265_param_parse_int(avctx, "keyint", avctx->gop_size);
        if (ret < 0)
            return ret;
    }
    if (avctx->keyint_min > 0) {
        ret = libx265_param_parse_int(avctx, "min-keyint", avctx->keyint_min);
        if (ret < 0)
            return ret;
    }
    if (avctx->max_b_frames >= 0) {
        ret = libx265_param_parse_int(avctx, "bframes", avctx->max_b_frames);
        if (ret < 0)
            return ret;
    }
    if (avctx->refs >= 0) {
        ret = libx265_param_parse_int(avctx, "ref", avctx->refs);
        if (ret < 0)
            return ret;
    }

    {
        const AVDictionaryEntry *en = NULL;
        while ((en = av_dict_iterate(ctx->x265_opts, en))) {
            int parse_ret = ctx->api->param_parse(ctx->params, en->key, en->value);

            switch (parse_ret) {
            case X265_PARAM_BAD_NAME:
                av_log(avctx, AV_LOG_WARNING,
                      "Unknown option: %s.\n", en->key);
                break;
            case X265_PARAM_BAD_VALUE:
                av_log(avctx, AV_LOG_WARNING,
                      "Invalid value for %s: %s.\n", en->key, en->value);
                break;
            default:
                break;
            }
        }
    }

    if (ctx->params->rc.vbvBufferSize && avctx->rc_initial_buffer_occupancy > 1000 &&
        ctx->params->rc.vbvBufferInit == 0.9) {
        ctx->params->rc.vbvBufferInit = (float)avctx->rc_initial_buffer_occupancy / 1000;
    }

    if (ctx->profile) {
        if (ctx->api->param_apply_profile(ctx->params, ctx->profile) < 0) {
            int i;
            av_log(avctx, AV_LOG_ERROR, "Invalid or incompatible profile set: %s.\n", ctx->profile);
            av_log(avctx, AV_LOG_INFO, "Possible profiles:");
            for (i = 0; x265_profile_names[i]; i++)
                av_log(avctx, AV_LOG_INFO, " %s", x265_profile_names[i]);
            av_log(avctx, AV_LOG_INFO, "\n");
            return AVERROR(EINVAL);
        }
    }

#if X265_BUILD >= 167
    ctx->dovi.logctx = avctx;
    if ((ret = ff_dovi_configure(&ctx->dovi, avctx)) < 0)
        return ret;
    ctx->params->dolbyProfile = ctx->dovi.cfg.dv_profile * 10 +
                                ctx->dovi.cfg.dv_bl_signal_compatibility_id;
#endif

    ctx->encoder = ctx->api->encoder_open(ctx->params);
    if (!ctx->encoder) {
        av_log(avctx, AV_LOG_ERROR, "Cannot open libx265 encoder.\n");
        libx265_encode_close(avctx);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        x265_nal *nal;
        int nnal;

        avctx->extradata_size = ctx->api->encoder_headers(ctx->encoder, &nal, &nnal);
        if (avctx->extradata_size <= 0) {
            av_log(avctx, AV_LOG_ERROR, "Cannot encode headers.\n");
            libx265_encode_close(avctx);
            return AVERROR_INVALIDDATA;
        }

        avctx->extradata = av_malloc(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot allocate HEVC header of size %d.\n", avctx->extradata_size);
            libx265_encode_close(avctx);
            return AVERROR(ENOMEM);
        }

        memcpy(avctx->extradata, nal[0].payload, avctx->extradata_size);
        memset(avctx->extradata + avctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }

    return 0;
}

static av_cold int libx265_encode_set_roi(libx265Context *ctx, const AVFrame *frame, x265_picture* pic)
{
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
    if (sd) {
        if (ctx->params->rc.aqMode == X265_AQ_NONE) {
            if (!ctx->roi_warned) {
                ctx->roi_warned = 1;
                av_log(ctx, AV_LOG_WARNING, "Adaptive quantization must be enabled to use ROI encoding, skipping ROI.\n");
            }
        } else {
            /* 8x8 block when qg-size is 8, 16*16 block otherwise. */
            int mb_size = (ctx->params->rc.qgSize == 8) ? 8 : 16;
            int mbx = (frame->width + mb_size - 1) / mb_size;
            int mby = (frame->height + mb_size - 1) / mb_size;
            int qp_range = 51 + 6 * (pic->bitDepth - 8);
            int nb_rois;
            const AVRegionOfInterest *roi;
            uint32_t roi_size;
            float *qoffsets;         /* will be freed after encode is called. */

            roi = (const AVRegionOfInterest*)sd->data;
            roi_size = roi->self_size;
            if (!roi_size || sd->size % roi_size != 0) {
                av_log(ctx, AV_LOG_ERROR, "Invalid AVRegionOfInterest.self_size.\n");
                return AVERROR(EINVAL);
            }
            nb_rois = sd->size / roi_size;

            qoffsets = av_calloc(mbx * mby, sizeof(*qoffsets));
            if (!qoffsets)
                return AVERROR(ENOMEM);

            // This list must be iterated in reverse because the first
            // region in the list applies when regions overlap.
            for (int i = nb_rois - 1; i >= 0; i--) {
                int startx, endx, starty, endy;
                float qoffset;

                roi = (const AVRegionOfInterest*)(sd->data + roi_size * i);

                starty = FFMIN(mby, roi->top / mb_size);
                endy   = FFMIN(mby, (roi->bottom + mb_size - 1)/ mb_size);
                startx = FFMIN(mbx, roi->left / mb_size);
                endx   = FFMIN(mbx, (roi->right + mb_size - 1)/ mb_size);

                if (roi->qoffset.den == 0) {
                    av_free(qoffsets);
                    av_log(ctx, AV_LOG_ERROR, "AVRegionOfInterest.qoffset.den must not be zero.\n");
                    return AVERROR(EINVAL);
                }
                qoffset = roi->qoffset.num * 1.0f / roi->qoffset.den;
                qoffset = av_clipf(qoffset * qp_range, -qp_range, +qp_range);

                for (int y = starty; y < endy; y++)
                    for (int x = startx; x < endx; x++)
                        qoffsets[x + y*mbx] = qoffset;
            }

            pic->quantOffsets = qoffsets;
        }
    }
    return 0;
}

static void free_picture(libx265Context *ctx, x265_picture *pic)
{
    x265_sei *sei = &pic->userSEI;
    for (int i = 0; i < sei->numPayloads; i++)
        av_free(sei->payloads[i].payload);

#if X265_BUILD >= 167
    av_free(pic->rpu.payload);
#endif

    if (pic->userData) {
        int idx = (int)(intptr_t)pic->userData - 1;
        rd_release(ctx, idx);
        pic->userData = NULL;
    }

    av_freep(&pic->quantOffsets);
    sei->numPayloads = 0;
}

static int libx265_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    libx265Context *ctx = avctx->priv_data;
    x265_picture x265pic;
#if X265_BUILD >= 210
    x265_picture x265pic_layers_out[MAX_SCALABLE_LAYERS];
    x265_picture* x265pic_lyrptr_out[MAX_SCALABLE_LAYERS];
#else
    x265_picture x265pic_solo_out = { 0 };
#endif
    x265_picture* x265pic_out;
    x265_nal *nal;
    x265_sei *sei;
    uint8_t *dst;
    int pict_type;
    int payload = 0;
    int nnal;
    int ret;
    int i;

    ctx->api->picture_init(ctx->params, &x265pic);

    sei = &x265pic.userSEI;
    sei->numPayloads = 0;

    if (pic) {
        AVFrameSideData *sd;
        ReorderedData *rd;
        int rd_idx;

        for (i = 0; i < 3; i++) {
           x265pic.planes[i] = pic->data[i];
           x265pic.stride[i] = pic->linesize[i];
        }

        x265pic.pts      = pic->pts;
        x265pic.bitDepth = av_pix_fmt_desc_get(avctx->pix_fmt)->comp[0].depth;

        x265pic.sliceType = pic->pict_type == AV_PICTURE_TYPE_I ?
                                              (ctx->forced_idr ? X265_TYPE_IDR : X265_TYPE_I) :
                            pic->pict_type == AV_PICTURE_TYPE_P ? X265_TYPE_P :
                            pic->pict_type == AV_PICTURE_TYPE_B ? X265_TYPE_B :
                            X265_TYPE_AUTO;

        ret = libx265_encode_set_roi(ctx, pic, &x265pic);
        if (ret < 0)
            return ret;

        rd_idx = rd_get(ctx);
        if (rd_idx < 0) {
            free_picture(ctx, &x265pic);
            return rd_idx;
        }
        rd = &ctx->rd[rd_idx];

        rd->duration         = pic->duration;
        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            rd->frame_opaque = pic->opaque;
            ret = av_buffer_replace(&rd->frame_opaque_ref, pic->opaque_ref);
            if (ret < 0) {
                rd_release(ctx, rd_idx);
                free_picture(ctx, &x265pic);
                return ret;
            }
        }

        x265pic.userData = (void*)(intptr_t)(rd_idx + 1);

        if (ctx->a53_cc) {
            void *sei_data;
            size_t sei_size;

            ret = ff_alloc_a53_sei(pic, 0, &sei_data, &sei_size);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
            } else if (sei_data) {
                void *tmp;
                x265_sei_payload *sei_payload;

                tmp = av_fast_realloc(ctx->sei_data,
                        &ctx->sei_data_size,
                        (sei->numPayloads + 1) * sizeof(*sei_payload));
                if (!tmp) {
                    av_free(sei_data);
                    free_picture(ctx, &x265pic);
                    return AVERROR(ENOMEM);
                }
                ctx->sei_data = tmp;
                sei->payloads = ctx->sei_data;
                sei_payload = &sei->payloads[sei->numPayloads];
                sei_payload->payload = sei_data;
                sei_payload->payloadSize = sei_size;
                sei_payload->payloadType = SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35;
                sei->numPayloads++;
            }
        }

        if (ctx->udu_sei) {
            for (i = 0; i < pic->nb_side_data; i++) {
                AVFrameSideData *side_data = pic->side_data[i];
                void *tmp;
                x265_sei_payload *sei_payload;

                if (side_data->type != AV_FRAME_DATA_SEI_UNREGISTERED)
                    continue;

                tmp = av_fast_realloc(ctx->sei_data,
                        &ctx->sei_data_size,
                        (sei->numPayloads + 1) * sizeof(*sei_payload));
                if (!tmp) {
                    free_picture(ctx, &x265pic);
                    return AVERROR(ENOMEM);
                }
                ctx->sei_data = tmp;
                sei->payloads = ctx->sei_data;
                sei_payload = &sei->payloads[sei->numPayloads];
                sei_payload->payload = av_memdup(side_data->data, side_data->size);
                if (!sei_payload->payload) {
                    free_picture(ctx, &x265pic);
                    return AVERROR(ENOMEM);
                }
                sei_payload->payloadSize = side_data->size;
                /* Equal to libx265 USER_DATA_UNREGISTERED */
                sei_payload->payloadType = SEI_TYPE_USER_DATA_UNREGISTERED;
                sei->numPayloads++;
            }
        }

#if X265_BUILD >= 167
        sd = av_frame_get_side_data(pic, AV_FRAME_DATA_DOVI_METADATA);
        if (ctx->dovi.cfg.dv_profile && sd) {
            const AVDOVIMetadata *metadata = (const AVDOVIMetadata *)sd->data;
            ret = ff_dovi_rpu_generate(&ctx->dovi, metadata, FF_DOVI_WRAP_NAL,
                                       &x265pic.rpu.payload,
                                       &x265pic.rpu.payloadSize);
            if (ret < 0) {
                free_picture(ctx, &x265pic);
                return ret;
            }
        } else if (ctx->dovi.cfg.dv_profile) {
            av_log(avctx, AV_LOG_ERROR, "Dolby Vision enabled, but received frame "
                   "without AV_FRAME_DATA_DOVI_METADATA");
            free_picture(ctx, &x265pic);
            return AVERROR_INVALIDDATA;
        }
#endif
    }

#if X265_BUILD >= 210
    for (i = 0; i < MAX_SCALABLE_LAYERS; i++)
        x265pic_lyrptr_out[i] = &x265pic_layers_out[i];

    ret = ctx->api->encoder_encode(ctx->encoder, &nal, &nnal,
                                   pic ? &x265pic : NULL, x265pic_lyrptr_out);
#else
    ret = ctx->api->encoder_encode(ctx->encoder, &nal, &nnal,
                                   pic ? &x265pic : NULL, &x265pic_solo_out);
#endif

    for (i = 0; i < sei->numPayloads; i++)
        av_free(sei->payloads[i].payload);
    av_freep(&x265pic.quantOffsets);

    if (ret < 0)
        return AVERROR_EXTERNAL;

    if (!nnal)
        return 0;

    for (i = 0; i < nnal; i++)
        payload += nal[i].sizeBytes;

    ret = ff_get_encode_buffer(avctx, pkt, payload, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    }
    dst = pkt->data;

    for (i = 0; i < nnal; i++) {
        memcpy(dst, nal[i].payload, nal[i].sizeBytes);
        dst += nal[i].sizeBytes;

        if (is_keyframe(nal[i].type))
            pkt->flags |= AV_PKT_FLAG_KEY;
    }

#if X265_BUILD >= 210
    x265pic_out = x265pic_lyrptr_out[0];
#else
    x265pic_out = &x265pic_solo_out;
#endif

    pkt->pts = x265pic_out->pts;
    pkt->dts = x265pic_out->dts;

    switch (x265pic_out->sliceType) {
    case X265_TYPE_IDR:
    case X265_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case X265_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case X265_TYPE_B:
    case X265_TYPE_BREF:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown picture type encountered.\n");
        return AVERROR_EXTERNAL;
    }

#if X265_BUILD >= 130
    if (x265pic_out->sliceType == X265_TYPE_B)
#else
    if (x265pic_out->frameData.sliceType == 'b')
#endif
        pkt->flags |= AV_PKT_FLAG_DISPOSABLE;

    ff_side_data_set_encoder_stats(pkt, x265pic_out->frameData.qp * FF_QP2LAMBDA, NULL, 0, pict_type);

    if (x265pic_out->userData) {
        int idx = (int)(intptr_t)x265pic_out->userData - 1;
        ReorderedData *rd = &ctx->rd[idx];

        pkt->duration           = rd->duration;

        if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            pkt->opaque          = rd->frame_opaque;
            pkt->opaque_ref      = rd->frame_opaque_ref;
            rd->frame_opaque_ref = NULL;
        }

        rd_release(ctx, idx);
    }

    *got_packet = 1;
    return 0;
}

static const enum AVPixelFormat x265_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat x265_csp_ten[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat x265_csp_twelve[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_NONE
};

static int libx265_get_supported_config(const AVCodecContext *avctx,
                                        const AVCodec *codec,
                                        enum AVCodecConfig config,
                                        unsigned flags, const void **out,
                                        int *out_num)
{
    if (config == AV_CODEC_CONFIG_PIX_FORMAT) {
        if (x265_api_get(12)) {
            *out = x265_csp_twelve;
            *out_num = FF_ARRAY_ELEMS(x265_csp_twelve) - 1;
        } else if (x265_api_get(10)) {
            *out = x265_csp_ten;
            *out_num = FF_ARRAY_ELEMS(x265_csp_ten) - 1;
        } else if (x265_api_get(8)) {
            *out = x265_csp_eight;
            *out_num = FF_ARRAY_ELEMS(x265_csp_eight) - 1;
        } else
            return AVERROR_EXTERNAL;
        return 0;
    }

    return ff_default_get_supported_config(avctx, codec, config, flags, out, out_num);
}

#define OFFSET(x) offsetof(libx265Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "crf",         "set the x265 crf",                                                            OFFSET(crf),       AV_OPT_TYPE_FLOAT,  { .dbl = -1 }, -1, FLT_MAX, VE },
    { "qp",          "set the x265 qp",                                                             OFFSET(cqp),       AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE },
    { "forced-idr",  "if forcing keyframes, force them as IDR frames",                              OFFSET(forced_idr),AV_OPT_TYPE_BOOL,   { .i64 =  0 },  0,       1, VE },
    { "preset",      "set the x265 preset",                                                         OFFSET(preset),    AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "tune",        "set the x265 tune parameter",                                                 OFFSET(tune),      AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "profile",     "set the x265 profile",                                                        OFFSET(profile),   AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "udu_sei",     "Use user data unregistered SEI if available",                                 OFFSET(udu_sei),   AV_OPT_TYPE_BOOL,   { .i64 = 0 }, 0, 1, VE },
    { "a53cc",       "Use A53 Closed Captions (if available)",                                      OFFSET(a53_cc),    AV_OPT_TYPE_BOOL,   { .i64 = 0 }, 0, 1, VE },
    { "x265-params", "set the x265 configuration using a :-separated list of key=value parameters", OFFSET(x265_opts), AV_OPT_TYPE_DICT,   { 0 }, 0, 0, VE },
#if X265_BUILD >= 167
    { "dolbyvision", "Enable Dolby Vision RPU coding", OFFSET(dovi.enable), AV_OPT_TYPE_BOOL, {.i64 = FF_DOVI_AUTOMATIC }, -1, 1, VE, .unit = "dovi" },
    {   "auto", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_DOVI_AUTOMATIC}, .flags = VE, .unit = "dovi" },
#endif
    { NULL }
};

static const AVClass class = {
    .class_name = "libx265",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault x265_defaults[] = {
    { "b", "0" },
    { "bf", "-1" },
    { "g", "-1" },
    { "keyint_min", "-1" },
    { "refs", "-1" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "i_qfactor", "-1" },
    { "b_qfactor", "-1" },
    { NULL },
};

FFCodec ff_libx265_encoder = {
    .p.name           = "libx265",
    CODEC_LONG_NAME("libx265 H.265 / HEVC"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_HEVC,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                        AV_CODEC_CAP_OTHER_THREADS |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .color_ranges     = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class     = &class,
    .p.wrapper_name   = "libx265",
    .init             = libx265_encode_init,
    .get_supported_config = libx265_get_supported_config,
    FF_CODEC_ENCODE_CB(libx265_encode_frame),
    .close            = libx265_encode_close,
    .priv_data_size   = sizeof(libx265Context),
    .defaults         = x265_defaults,
    .caps_internal    = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                        FF_CODEC_CAP_AUTO_THREADS,
};
