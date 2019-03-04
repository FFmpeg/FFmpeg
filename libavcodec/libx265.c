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

#include "libavutil/internal.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "internal.h"

typedef struct libx265Context {
    const AVClass *class;

    x265_encoder *encoder;
    x265_param   *params;
    const x265_api *api;

    float crf;
    int   forced_idr;
    char *preset;
    char *tune;
    char *profile;
    char *x265_opts;
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

static av_cold int libx265_encode_close(AVCodecContext *avctx)
{
    libx265Context *ctx = avctx->priv_data;

    ctx->api->param_free(ctx->params);

    if (ctx->encoder)
        ctx->api->encoder_close(ctx->encoder);

    return 0;
}

static av_cold int libx265_encode_init(AVCodecContext *avctx)
{
    libx265Context *ctx = avctx->priv_data;
    AVCPBProperties *cpb_props = NULL;

    ctx->api = x265_api_get(av_pix_fmt_desc_get(avctx->pix_fmt)->comp[0].depth);
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
    ctx->params->fpsNum          = avctx->time_base.den;
    ctx->params->fpsDenom        = avctx->time_base.num * avctx->ticks_per_frame;
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

    if ((avctx->color_primaries <= AVCOL_PRI_SMPTE432 &&
         avctx->color_primaries != AVCOL_PRI_UNSPECIFIED) ||
        (avctx->color_trc <= AVCOL_TRC_ARIB_STD_B67 &&
         avctx->color_trc != AVCOL_TRC_UNSPECIFIED) ||
        (avctx->colorspace <= AVCOL_SPC_ICTCP &&
         avctx->colorspace != AVCOL_SPC_UNSPECIFIED)) {

        ctx->params->vui.bEnableVideoSignalTypePresentFlag  = 1;
        ctx->params->vui.bEnableColorDescriptionPresentFlag = 1;

        // x265 validates the parameters internally
        ctx->params->vui.colorPrimaries          = avctx->color_primaries;
        ctx->params->vui.transferCharacteristics = avctx->color_trc;
        ctx->params->vui.matrixCoeffs            = avctx->colorspace;
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

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV420P12:
        ctx->params->internalCsp = X265_CSP_I420;
        break;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV422P12:
        ctx->params->internalCsp = X265_CSP_I422;
        break;
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
        ctx->params->vui.matrixCoeffs = AVCOL_SPC_RGB;
        ctx->params->vui.bEnableVideoSignalTypePresentFlag  = 1;
        ctx->params->vui.bEnableColorDescriptionPresentFlag = 1;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
        ctx->params->internalCsp = X265_CSP_I444;
        break;
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GRAY12:
        if (ctx->api->api_build_number < 85) {
            av_log(avctx, AV_LOG_ERROR,
                   "libx265 version is %d, must be at least 85 for gray encoding.\n",
                   ctx->api->api_build_number);
            return AVERROR_INVALIDDATA;
        }
        ctx->params->internalCsp = X265_CSP_I400;
        break;
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
    }

    ctx->params->rc.vbvBufferSize = avctx->rc_buffer_size / 1000;
    ctx->params->rc.vbvMaxBitrate = avctx->rc_max_rate    / 1000;

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->buffer_size = ctx->params->rc.vbvBufferSize * 1000;
    cpb_props->max_bitrate = ctx->params->rc.vbvMaxBitrate * 1000;
    cpb_props->avg_bitrate = ctx->params->rc.bitrate       * 1000;

    if (!(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER))
        ctx->params->bRepeatHeaders = 1;

    if (ctx->x265_opts) {
        AVDictionary *dict    = NULL;
        AVDictionaryEntry *en = NULL;

        if (!av_dict_parse_string(&dict, ctx->x265_opts, "=", ":", 0)) {
            while ((en = av_dict_get(dict, "", en, AV_DICT_IGNORE_SUFFIX))) {
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
            av_dict_free(&dict);
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
    }

    return 0;
}

static av_cold int libx265_encode_set_roi(libx265Context *ctx, const AVFrame *frame, x265_picture* pic)
{
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
    if (sd) {
        if (ctx->params->rc.aqMode == X265_AQ_NONE) {
            av_log(ctx, AV_LOG_WARNING, "Adaptive quantization must be enabled to use ROI encoding, skipping ROI.\n");
        } else {
            /* 8x8 block when qg-size is 8, 16*16 block otherwise. */
            int mb_size = (ctx->params->rc.qgSize == 8) ? 8 : 16;
            int mbx = (frame->width + mb_size - 1) / mb_size;
            int mby = (frame->height + mb_size - 1) / mb_size;
            int nb_rois;
            AVRegionOfInterest *roi;
            float *qoffsets;         /* will be freed after encode is called. */
            qoffsets = av_mallocz_array(mbx * mby, sizeof(*qoffsets));
            if (!qoffsets)
                return AVERROR(ENOMEM);

            nb_rois = sd->size / sizeof(AVRegionOfInterest);
            roi = (AVRegionOfInterest*)sd->data;
            for (int count = 0; count < nb_rois; count++) {
                int starty = FFMIN(mby, roi->top / mb_size);
                int endy   = FFMIN(mby, (roi->bottom + mb_size - 1)/ mb_size);
                int startx = FFMIN(mbx, roi->left / mb_size);
                int endx   = FFMIN(mbx, (roi->right + mb_size - 1)/ mb_size);
                float qoffset;

                if (roi->self_size == 0) {
                    av_free(qoffsets);
                    av_log(ctx, AV_LOG_ERROR, "AVRegionOfInterest.self_size must be set to sizeof(AVRegionOfInterest).\n");
                    return AVERROR(EINVAL);
                }

                if (roi->qoffset.den == 0) {
                    av_free(qoffsets);
                    av_log(ctx, AV_LOG_ERROR, "AVRegionOfInterest.qoffset.den must not be zero.\n");
                    return AVERROR(EINVAL);
                }
                qoffset = roi->qoffset.num * 1.0f / roi->qoffset.den;
                qoffset = av_clipf(qoffset, -1.0f, 1.0f);

                /* qp range of x265 is from 0 to 51, just choose 25 as the scale value,
                 * so the range of final qoffset is [-25.0, 25.0].
                 */
                qoffset = qoffset * 25;

                for (int y = starty; y < endy; y++)
                    for (int x = startx; x < endx; x++)
                        qoffsets[x + y*mbx] = qoffset;

                roi = (AVRegionOfInterest*)((char*)roi + roi->self_size);
            }

            pic->quantOffsets = qoffsets;
        }
    }
    return 0;
}

static int libx265_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    libx265Context *ctx = avctx->priv_data;
    x265_picture x265pic;
    x265_picture x265pic_out = { 0 };
    x265_nal *nal;
    uint8_t *dst;
    int payload = 0;
    int nnal;
    int ret;
    int i;

    ctx->api->picture_init(ctx->params, &x265pic);

    if (pic) {
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
    }

    ret = ctx->api->encoder_encode(ctx->encoder, &nal, &nnal,
                                   pic ? &x265pic : NULL, &x265pic_out);

    av_freep(&x265pic.quantOffsets);

    if (ret < 0)
        return AVERROR_EXTERNAL;

    if (!nnal)
        return 0;

    for (i = 0; i < nnal; i++)
        payload += nal[i].sizeBytes;

    ret = ff_alloc_packet2(avctx, pkt, payload, payload);
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

    pkt->pts = x265pic_out.pts;
    pkt->dts = x265pic_out.dts;

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    switch (x265pic_out.sliceType) {
    case X265_TYPE_IDR:
    case X265_TYPE_I:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case X265_TYPE_P:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case X265_TYPE_B:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;
        break;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

#if X265_BUILD >= 130
    if (x265pic_out.sliceType == X265_TYPE_B)
#else
    if (x265pic_out.frameData.sliceType == 'b')
#endif
        pkt->flags |= AV_PKT_FLAG_DISPOSABLE;

    *got_packet = 1;
    return 0;
}

static const enum AVPixelFormat x265_csp_eight[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat x265_csp_ten[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
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
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
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

static av_cold void libx265_encode_init_csp(AVCodec *codec)
{
    if (x265_api_get(12))
        codec->pix_fmts = x265_csp_twelve;
    else if (x265_api_get(10))
        codec->pix_fmts = x265_csp_ten;
    else if (x265_api_get(8))
        codec->pix_fmts = x265_csp_eight;
}

#define OFFSET(x) offsetof(libx265Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "crf",         "set the x265 crf",                                                            OFFSET(crf),       AV_OPT_TYPE_FLOAT,  { .dbl = -1 }, -1, FLT_MAX, VE },
    { "forced-idr",  "if forcing keyframes, force them as IDR frames",                              OFFSET(forced_idr),AV_OPT_TYPE_BOOL,   { .i64 =  0 },  0,       1, VE },
    { "preset",      "set the x265 preset",                                                         OFFSET(preset),    AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "tune",        "set the x265 tune parameter",                                                 OFFSET(tune),      AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "profile",     "set the x265 profile",                                                        OFFSET(profile),   AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { "x265-params", "set the x265 configuration using a :-separated list of key=value parameters", OFFSET(x265_opts), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },
    { NULL }
};

static const AVClass class = {
    .class_name = "libx265",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault x265_defaults[] = {
    { "b", "0" },
    { NULL },
};

AVCodec ff_libx265_encoder = {
    .name             = "libx265",
    .long_name        = NULL_IF_CONFIG_SMALL("libx265 H.265 / HEVC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .init             = libx265_encode_init,
    .init_static_data = libx265_encode_init_csp,
    .encode2          = libx265_encode_frame,
    .close            = libx265_encode_close,
    .priv_data_size   = sizeof(libx265Context),
    .priv_class       = &class,
    .defaults         = x265_defaults,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .wrapper_name     = "libx265",
};
