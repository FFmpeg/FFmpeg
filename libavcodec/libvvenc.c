/*
 * H.266 encoding using the VVenC library
 *
 * Copyright (C) 2022, Thomas Siedel
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

#include <vvenc/vvenc.h>
#include <vvenc/vvencCfg.h>
#include <vvenc/version.h>

#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"
#include "profiles.h"

#define VVENC_VERSION_INT  AV_VERSION_INT(VVENC_VERSION_MAJOR, \
                                          VVENC_VERSION_MINOR, \
                                          VVENC_VERSION_PATCH)

typedef struct VVenCContext {
    AVClass         *class;
    vvencEncoder    *encoder;
    vvencAccessUnit *au;
    bool             encode_done;
    int   preset;
    int   qp;
    int   qpa;
    int   intra_refresh_sec;
    char *level;
    int   tier;
    char *stats;
    AVDictionary *vvenc_opts;
} VVenCContext;

static void vvenc_log_callback(void *ctx, int level,
                               const char *fmt, va_list args)
{
    vvenc_config params;
    vvencEncoder *encoder = ctx;
    if (encoder) {
        vvenc_config_default(&params);
        vvenc_get_config(encoder, &params);
        if ((int)params.m_verbosity >= level)
            vfprintf(level == 1 ? stderr : stdout, fmt, args);
    }
}

static void vvenc_set_verbository(vvenc_config *params)
{
    int loglevel = av_log_get_level();
    params->m_verbosity = VVENC_SILENT;
    if (loglevel >= AV_LOG_DEBUG)
        params->m_verbosity = VVENC_DETAILS;
    else if (loglevel >= AV_LOG_VERBOSE)
        params->m_verbosity = VVENC_NOTICE;
    else if (loglevel >= AV_LOG_INFO)
        params->m_verbosity = VVENC_WARNING;
}

static void vvenc_set_pic_format(AVCodecContext *avctx, vvenc_config *params)
{
    params->m_internChromaFormat = VVENC_CHROMA_420;
    params->m_inputBitDepth[0]   = 10;
}

static void vvenc_set_color_format(AVCodecContext *avctx, vvenc_config *params)
{
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
        params->m_colourPrimaries = (int) avctx->color_primaries;
    if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED)
        params->m_matrixCoefficients = (int) avctx->colorspace;
    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED) {
        params->m_transferCharacteristics = (int) avctx->color_trc;

        if (avctx->color_trc == AVCOL_TRC_SMPTE2084)
            params->m_HdrMode = (avctx->color_primaries == AVCOL_PRI_BT2020) ?
                                VVENC_HDR_PQ_BT2020 : VVENC_HDR_PQ;
        else if (avctx->color_trc == AVCOL_TRC_BT2020_10 || avctx->color_trc == AVCOL_TRC_ARIB_STD_B67)
            params->m_HdrMode = (avctx->color_trc == AVCOL_TRC_BT2020_10 ||
                                 avctx->color_primaries == AVCOL_PRI_BT2020 ||
                                 avctx->colorspace == AVCOL_SPC_BT2020_NCL ||
                                 avctx->colorspace == AVCOL_SPC_BT2020_CL) ?
                                VVENC_HDR_HLG_BT2020 : VVENC_HDR_HLG;
    }

    if (params->m_HdrMode == VVENC_HDR_OFF &&
        (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED || avctx->colorspace != AVCOL_SPC_UNSPECIFIED)) {
        params->m_vuiParametersPresent = 1;
        params->m_colourDescriptionPresent = true;
    }
}

static void vvenc_set_framerate(AVCodecContext *avctx, vvenc_config *params)
{
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        params->m_FrameRate = avctx->framerate.num;
        params->m_FrameScale = avctx->framerate.den;
    } else {
        params->m_FrameRate = avctx->time_base.den;
        params->m_FrameScale = avctx->time_base.num;
    }

    params->m_TicksPerSecond = -1;   /* auto mode for ticks per frame = 1 */
}

static int vvenc_parse_vvenc_params(AVCodecContext *avctx, vvenc_config *params)
{
    VVenCContext *s = avctx->priv_data;
    const AVDictionaryEntry *en = NULL;
    int parse_ret;
    int ret = 0;

    while ((en = av_dict_iterate(s->vvenc_opts, en))) {
        av_log(avctx, AV_LOG_DEBUG, "vvenc_set_param: '%s:%s'\n", en->key,
               en->value);
        parse_ret = vvenc_set_param(params, en->key, en->value);
        switch (parse_ret) {
        case VVENC_PARAM_BAD_NAME:
            av_log(avctx, AV_LOG_ERROR, "Unknown vvenc option: %s.\n", en->key);
            ret = AVERROR(EINVAL);
            break;
        case VVENC_PARAM_BAD_VALUE:
            av_log(avctx, AV_LOG_ERROR, "Invalid vvenc value for %s: %s.\n", en->key, en->value);
            ret = AVERROR(EINVAL);
            break;
        default:
            break;
        }

        if (!av_strcasecmp(en->key, "rcstatsfile")) {
            av_log(avctx, AV_LOG_ERROR, "vvenc-params 2pass option 'rcstatsfile' "
                   "not available. Use option 'passlogfile'\n");
            ret = AVERROR(EINVAL);
        }
        if (!av_strcasecmp(en->key, "passes") || !av_strcasecmp(en->key, "pass")) {
            av_log(avctx, AV_LOG_ERROR, "vvenc-params 2pass option '%s' "
                   "not available. Use option 'pass'\n", en->key);
            ret = AVERROR(EINVAL);
        }
    }
    return ret;
}

static int vvenc_set_rc_mode(AVCodecContext *avctx, vvenc_config *params)
{
    params->m_RCNumPasses = 1;
    if ((avctx->flags & AV_CODEC_FLAG_PASS1 || avctx->flags & AV_CODEC_FLAG_PASS2)) {
        if (!avctx->bit_rate) {
            av_log(avctx, AV_LOG_ERROR, "A bitrate must be set to use two pass mode.\n");
            return AVERROR(EINVAL);
        }
        params->m_RCNumPasses = 2;
        if (avctx->flags & AV_CODEC_FLAG_PASS1)
            params->m_RCPass = 1;
        else
            params->m_RCPass = 2;
    }

    if (avctx->rc_max_rate) {
#if VVENC_VERSION_INT >= AV_VERSION_INT(1,8,0)
        params->m_RCMaxBitrate = avctx->rc_max_rate;
#endif

#if VVENC_VERSION_INT < AV_VERSION_INT(1,11,0)
        /* rc_max_rate without a bit_rate enables capped CQF mode.
        (QP + subj. optimization + max. bitrate) */
        if (!avctx->bit_rate) {
            av_log(avctx, AV_LOG_ERROR, "Capped Constant Quality Factor mode (capped CQF) "
                   "needs at least vvenc version >= 1.11.0 (current version %s)\n", vvenc_get_version());
            return AVERROR(EINVAL);
        }
#endif
    }
    return 0;
}

static int vvenc_init_extradata(AVCodecContext *avctx, VVenCContext *s)
{
    int ret;
    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        ret = vvenc_get_headers(s->encoder, s->au);
        if (0 != ret) {
            av_log(avctx, AV_LOG_ERROR, "cannot get (SPS,PPS) headers: %s\n",
                   vvenc_get_last_error(s->encoder));
            return AVERROR(EINVAL);
        }

        if (s->au->payloadUsedSize <= 0) {
            return AVERROR_INVALIDDATA;
        }

        avctx->extradata_size = s->au->payloadUsedSize;
        avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            return AVERROR(ENOMEM);
        }

        memcpy(avctx->extradata, s->au->payload, avctx->extradata_size);
    }
    return 0;
}

static av_cold int vvenc_init(AVCodecContext *avctx)
{
    int ret;
    int framerate;
    VVenCContext *s = avctx->priv_data;
    vvenc_config params;
    vvencPresetMode preset = (vvencPresetMode) s->preset;

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        av_log(avctx, AV_LOG_ERROR, "interlaced not supported\n");
        return AVERROR(EINVAL);
    }

    vvenc_config_default(&params);

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        framerate = avctx->framerate.num / avctx->framerate.den;
    else
        framerate = avctx->time_base.den / avctx->time_base.num;

    vvenc_init_default(&params, avctx->width, avctx->height, framerate,
                       avctx->bit_rate, s->qp, preset);

    vvenc_set_verbository(&params);

    if (avctx->thread_count > 0)
        params.m_numThreads = avctx->thread_count;

    /* GOP settings (IDR/CRA) */
    if (avctx->flags & AV_CODEC_FLAG_CLOSED_GOP)
        params.m_DecodingRefreshType = VVENC_DRT_IDR;

    if (avctx->gop_size == 1) {
        params.m_GOPSize = 1;
        params.m_IntraPeriod = 1;
    } else
        params.m_IntraPeriodSec = s->intra_refresh_sec;

    params.m_AccessUnitDelimiter = true;
    params.m_usePerceptQPA = s->qpa;
    params.m_levelTier     = (vvencTier) s->tier;

    if (avctx->level > 0)
        params.m_level = (vvencLevel)avctx->level;

    if (s->level) {
        if (VVENC_PARAM_BAD_VALUE == vvenc_set_param(&params, "level", s->level)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid level_idc: %s.\n", s->level);
            return AVERROR(EINVAL);
        }
    }

    vvenc_set_framerate(avctx, &params);

    vvenc_set_pic_format(avctx, &params);

    vvenc_set_color_format(avctx, &params);

    ret = vvenc_parse_vvenc_params(avctx, &params);
    if (ret != 0)
        return ret;

    ret = vvenc_set_rc_mode(avctx, &params);
    if (ret != 0)
        return ret;

    s->encoder = vvenc_encoder_create();
    if (!s->encoder) {
        av_log(avctx, AV_LOG_ERROR, "cannot create libvvenc encoder\n");
        return AVERROR(ENOMEM);
    }

    vvenc_set_msg_callback(&params, s->encoder, vvenc_log_callback);
    ret = vvenc_encoder_open(s->encoder, &params);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "cannot open libvvenc encoder: %s\n",
               vvenc_get_last_error(s->encoder));
        return AVERROR_EXTERNAL;
    }

    vvenc_get_config(s->encoder, &params);     /* get the adapted config */

    av_log(avctx, AV_LOG_INFO, "libvvenc version: %s\n", vvenc_get_version());
    if (av_log_get_level() >= AV_LOG_VERBOSE)
        av_log(avctx, AV_LOG_INFO, "%s\n", vvenc_get_config_as_string(&params, params.m_verbosity));

    if (params.m_RCNumPasses == 2) {
        ret = vvenc_init_pass(s->encoder, params.m_RCPass - 1, s->stats);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "cannot init pass %d: %s\n",  params.m_RCPass,
                   vvenc_get_last_error(s->encoder));
            return AVERROR_EXTERNAL;
        }
    }

    s->au = vvenc_accessUnit_alloc();
    if (!s->au) {
        av_log(avctx, AV_LOG_FATAL, "cannot allocate memory for AU payload\n");
        return AVERROR(ENOMEM);
    }
    vvenc_accessUnit_alloc_payload(s->au, avctx->width * avctx->height);
    if (!s->au->payload) {
        av_log(avctx, AV_LOG_FATAL, "cannot allocate payload memory of size %d\n",
               avctx->width * avctx->height);
        return AVERROR(ENOMEM);
    }

    ret = vvenc_init_extradata(avctx, s);
    if (ret != 0)
        return ret;

    s->encode_done = false;
    return 0;
}

static av_cold int vvenc_close(AVCodecContext *avctx)
{
    VVenCContext *s = avctx->priv_data;

    if (s->au)
        vvenc_accessUnit_free(s->au, true);

    if (s->encoder) {
        vvenc_print_summary(s->encoder);

        if (0 != vvenc_encoder_close(s->encoder))
            return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold int vvenc_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame,
                               int *got_packet)
{
    VVenCContext *s = avctx->priv_data;
    vvencYUVBuffer *pyuvbuf;
    vvencYUVBuffer yuvbuf;
    int ret;

    pyuvbuf = NULL;
    if (frame) {
        vvenc_YUVBuffer_default(&yuvbuf);
        yuvbuf.planes[0].ptr = (int16_t *) frame->data[0];
        yuvbuf.planes[1].ptr = (int16_t *) frame->data[1];
        yuvbuf.planes[2].ptr = (int16_t *) frame->data[2];

        yuvbuf.planes[0].width  = frame->width;
        yuvbuf.planes[0].height = frame->height;
        yuvbuf.planes[0].stride = frame->linesize[0] >> 1; /* stride is used in 16bit samples in vvenc */

        yuvbuf.planes[1].width  = frame->width >> 1;
        yuvbuf.planes[1].height = frame->height >> 1;
        yuvbuf.planes[1].stride = frame->linesize[1] >> 1;

        yuvbuf.planes[2].width  = frame->width >> 1;
        yuvbuf.planes[2].height = frame->height >> 1;
        yuvbuf.planes[2].stride = frame->linesize[2] >> 1;

        yuvbuf.cts = frame->pts;
        yuvbuf.ctsValid = true;
        pyuvbuf = &yuvbuf;
    }

    if (!s->encode_done) {
        if (vvenc_encode(s->encoder, pyuvbuf, s->au, &s->encode_done) != 0)
            return AVERROR_EXTERNAL;
    } else
        return 0;

    if (s->au->payloadUsedSize > 0) {
        ret = ff_get_encode_buffer(avctx, pkt, s->au->payloadUsedSize, 0);
        if (ret < 0)
            return ret;

        memcpy(pkt->data, s->au->payload, s->au->payloadUsedSize);

        if (s->au->ctsValid)
            pkt->pts = s->au->cts;
        if (s->au->dtsValid)
            pkt->dts = s->au->dts;
        pkt->flags |= AV_PKT_FLAG_KEY * s->au->rap;

        *got_packet = 1;
        return 0;
    }

    return 0;
}

static const enum AVPixelFormat pix_fmts_vvenc[] = {
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_NONE
};

#define OFFSET(x) offsetof(VVenCContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset",       "set encoding preset", OFFSET(preset), AV_OPT_TYPE_INT, {.i64 = 2}, 0, 4, VE, "preset"},
    { "faster",       "0", 0, AV_OPT_TYPE_CONST, {.i64 = VVENC_FASTER}, INT_MIN, INT_MAX, VE, "preset" },
    { "fast",         "1", 0, AV_OPT_TYPE_CONST, {.i64 = VVENC_FAST},   INT_MIN, INT_MAX, VE, "preset" },
    { "medium",       "2", 0, AV_OPT_TYPE_CONST, {.i64 = VVENC_MEDIUM}, INT_MIN, INT_MAX, VE, "preset" },
    { "slow",         "3", 0, AV_OPT_TYPE_CONST, {.i64 = VVENC_SLOW},   INT_MIN, INT_MAX, VE, "preset" },
    { "slower",       "4", 0, AV_OPT_TYPE_CONST, {.i64 = VVENC_SLOWER}, INT_MIN, INT_MAX, VE, "preset" },
    { "qp",           "set quantization",          OFFSET(qp), AV_OPT_TYPE_INT,  {.i64 = 32}, -1, 63, VE },
    { "qpa",          "set subjective (perceptually motivated) optimization", OFFSET(qpa), AV_OPT_TYPE_BOOL, {.i64 = 1},  0, 1, VE},
    { "passlogfile",  "Filename for 2 pass stats", OFFSET(stats), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, VE},
    { "stats",        "Filename for 2 pass stats", OFFSET(stats), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, VE},
    { "period",       "set (intra) refresh period in seconds", OFFSET(intra_refresh_sec), AV_OPT_TYPE_INT,  {.i64 = 1},  1, INT_MAX, VE },
    { "vvenc-params", "set the vvenc configuration using a :-separated list of key=value parameters", OFFSET(vvenc_opts), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { "level",        "Specify level (as defined by Annex A)", OFFSET(level), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, VE},
    { "tier",         "set vvc tier", OFFSET(tier), AV_OPT_TYPE_INT, {.i64 = 0},  0, 1, VE, "tier"},
    { "main",         "main", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, VE, "tier"},
    { "high",         "high", 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, VE, "tier"},
    {NULL}
};

static const AVClass class = {
    .class_name = "libvvenc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault vvenc_defaults[] = {
    { "b", "0" },
    { "g", "-1" },
    { NULL },
};

const FFCodec ff_libvvenc_encoder = {
    .p.name         = "libvvenc",
    CODEC_LONG_NAME("libvvenc H.266 / VVC"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VVC,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_OTHER_THREADS,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_vvc_profiles),
    .p.priv_class   = &class,
    .p.wrapper_name = "libvvenc",
    .priv_data_size = sizeof(VVenCContext),
    CODEC_PIXFMTS_ARRAY(pix_fmts_vvenc),
    .init           = vvenc_init,
    FF_CODEC_ENCODE_CB(vvenc_frame),
    .close          = vvenc_close,
    .defaults       = vvenc_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS
};
