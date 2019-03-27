/*
 * Intel MediaSDK QSV encoder utility functions
 *
 * copyright (c) 2013 Yukinori Yamazoe
 * copyright (c) 2015 Anton Khirnov
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

#include <string.h>
#include <sys/types.h>
#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "libavcodec/bytestream.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

static const struct {
    mfxU16 profile;
    const char *name;
} profile_names[] = {
    { MFX_PROFILE_AVC_BASELINE,                 "baseline"              },
    { MFX_PROFILE_AVC_MAIN,                     "main"                  },
    { MFX_PROFILE_AVC_EXTENDED,                 "extended"              },
    { MFX_PROFILE_AVC_HIGH,                     "high"                  },
#if QSV_VERSION_ATLEAST(1, 15)
    { MFX_PROFILE_AVC_HIGH_422,                 "high 422"              },
#endif
#if QSV_VERSION_ATLEAST(1, 4)
    { MFX_PROFILE_AVC_CONSTRAINED_BASELINE,     "constrained baseline"  },
    { MFX_PROFILE_AVC_CONSTRAINED_HIGH,         "constrained high"      },
    { MFX_PROFILE_AVC_PROGRESSIVE_HIGH,         "progressive high"      },
#endif
    { MFX_PROFILE_MPEG2_SIMPLE,                 "simple"                },
    { MFX_PROFILE_MPEG2_MAIN,                   "main"                  },
    { MFX_PROFILE_MPEG2_HIGH,                   "high"                  },
    { MFX_PROFILE_VC1_SIMPLE,                   "simple"                },
    { MFX_PROFILE_VC1_MAIN,                     "main"                  },
    { MFX_PROFILE_VC1_ADVANCED,                 "advanced"              },
#if QSV_VERSION_ATLEAST(1, 8)
    { MFX_PROFILE_HEVC_MAIN,                    "main"                  },
    { MFX_PROFILE_HEVC_MAIN10,                  "main10"                },
    { MFX_PROFILE_HEVC_MAINSP,                  "mainsp"                },
#endif
};

static const char *print_profile(mfxU16 profile)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(profile_names); i++)
        if (profile == profile_names[i].profile)
            return profile_names[i].name;
    return "unknown";
}

static const struct {
    mfxU16      rc_mode;
    const char *name;
} rc_names[] = {
    { MFX_RATECONTROL_CBR,     "CBR" },
    { MFX_RATECONTROL_VBR,     "VBR" },
    { MFX_RATECONTROL_CQP,     "CQP" },
#if QSV_HAVE_AVBR
    { MFX_RATECONTROL_AVBR,    "AVBR" },
#endif
#if QSV_HAVE_LA
    { MFX_RATECONTROL_LA,      "LA" },
#endif
#if QSV_HAVE_ICQ
    { MFX_RATECONTROL_ICQ,     "ICQ" },
    { MFX_RATECONTROL_LA_ICQ,  "LA_ICQ" },
#endif
#if QSV_HAVE_VCM
    { MFX_RATECONTROL_VCM,     "VCM" },
#endif
#if QSV_VERSION_ATLEAST(1, 10)
    { MFX_RATECONTROL_LA_EXT,  "LA_EXT" },
#endif
#if QSV_HAVE_LA_HRD
    { MFX_RATECONTROL_LA_HRD,  "LA_HRD" },
#endif
#if QSV_HAVE_QVBR
    { MFX_RATECONTROL_QVBR,    "QVBR" },
#endif
};

static const char *print_ratecontrol(mfxU16 rc_mode)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(rc_names); i++)
        if (rc_mode == rc_names[i].rc_mode)
            return rc_names[i].name;
    return "unknown";
}

static const char *print_threestate(mfxU16 val)
{
    if (val == MFX_CODINGOPTION_ON)
        return "ON";
    else if (val == MFX_CODINGOPTION_OFF)
        return "OFF";
    return "unknown";
}

static void dump_video_param(AVCodecContext *avctx, QSVEncContext *q,
                             mfxExtBuffer **coding_opts)
{
    mfxInfoMFX *info = &q->param.mfx;

    mfxExtCodingOption   *co = (mfxExtCodingOption*)coding_opts[0];
#if QSV_HAVE_CO2
    mfxExtCodingOption2 *co2 = (mfxExtCodingOption2*)coding_opts[1];
#endif
#if QSV_HAVE_CO3
    mfxExtCodingOption3 *co3 = (mfxExtCodingOption3*)coding_opts[2];
#endif

    av_log(avctx, AV_LOG_VERBOSE, "profile: %s; level: %"PRIu16"\n",
           print_profile(info->CodecProfile), info->CodecLevel);

    av_log(avctx, AV_LOG_VERBOSE, "GopPicSize: %"PRIu16"; GopRefDist: %"PRIu16"; GopOptFlag: ",
           info->GopPicSize, info->GopRefDist);
    if (info->GopOptFlag & MFX_GOP_CLOSED)
        av_log(avctx, AV_LOG_VERBOSE, "closed ");
    if (info->GopOptFlag & MFX_GOP_STRICT)
        av_log(avctx, AV_LOG_VERBOSE, "strict ");
    av_log(avctx, AV_LOG_VERBOSE, "; IdrInterval: %"PRIu16"\n", info->IdrInterval);

    av_log(avctx, AV_LOG_VERBOSE, "TargetUsage: %"PRIu16"; RateControlMethod: %s\n",
           info->TargetUsage, print_ratecontrol(info->RateControlMethod));

    if (info->RateControlMethod == MFX_RATECONTROL_CBR ||
        info->RateControlMethod == MFX_RATECONTROL_VBR
#if QSV_HAVE_VCM
        || info->RateControlMethod == MFX_RATECONTROL_VCM
#endif
        ) {
        av_log(avctx, AV_LOG_VERBOSE,
               "BufferSizeInKB: %"PRIu16"; InitialDelayInKB: %"PRIu16"; TargetKbps: %"PRIu16"; MaxKbps: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->BufferSizeInKB, info->InitialDelayInKB, info->TargetKbps, info->MaxKbps, info->BRCParamMultiplier);
    } else if (info->RateControlMethod == MFX_RATECONTROL_CQP) {
        av_log(avctx, AV_LOG_VERBOSE, "QPI: %"PRIu16"; QPP: %"PRIu16"; QPB: %"PRIu16"\n",
               info->QPI, info->QPP, info->QPB);
    }
#if QSV_HAVE_AVBR
    else if (info->RateControlMethod == MFX_RATECONTROL_AVBR) {
        av_log(avctx, AV_LOG_VERBOSE,
               "TargetKbps: %"PRIu16"; Accuracy: %"PRIu16"; Convergence: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->TargetKbps, info->Accuracy, info->Convergence, info->BRCParamMultiplier);
    }
#endif
#if QSV_HAVE_LA
    else if (info->RateControlMethod == MFX_RATECONTROL_LA
#if QSV_HAVE_LA_HRD
             || info->RateControlMethod == MFX_RATECONTROL_LA_HRD
#endif
             ) {
        av_log(avctx, AV_LOG_VERBOSE,
               "TargetKbps: %"PRIu16"; LookAheadDepth: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->TargetKbps, co2->LookAheadDepth, info->BRCParamMultiplier);
    }
#endif
#if QSV_HAVE_ICQ
    else if (info->RateControlMethod == MFX_RATECONTROL_ICQ) {
        av_log(avctx, AV_LOG_VERBOSE, "ICQQuality: %"PRIu16"\n", info->ICQQuality);
    } else if (info->RateControlMethod == MFX_RATECONTROL_LA_ICQ) {
        av_log(avctx, AV_LOG_VERBOSE, "ICQQuality: %"PRIu16"; LookAheadDepth: %"PRIu16"\n",
               info->ICQQuality, co2->LookAheadDepth);
    }
#endif
#if QSV_HAVE_QVBR
    else if (info->RateControlMethod == MFX_RATECONTROL_QVBR) {
        av_log(avctx, AV_LOG_VERBOSE, "QVBRQuality: %"PRIu16"\n",
               co3->QVBRQuality);
    }
#endif
    av_log(avctx, AV_LOG_VERBOSE, "NumSlice: %"PRIu16"; NumRefFrame: %"PRIu16"\n",
           info->NumSlice, info->NumRefFrame);
    av_log(avctx, AV_LOG_VERBOSE, "RateDistortionOpt: %s\n",
           print_threestate(co->RateDistortionOpt));

#if QSV_HAVE_CO2
    av_log(avctx, AV_LOG_VERBOSE,
           "RecoveryPointSEI: %s IntRefType: %"PRIu16"; IntRefCycleSize: %"PRIu16"; IntRefQPDelta: %"PRId16"\n",
           print_threestate(co->RecoveryPointSEI), co2->IntRefType, co2->IntRefCycleSize, co2->IntRefQPDelta);

    av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSize: %"PRIu16"; ", co2->MaxFrameSize);
#if QSV_HAVE_MAX_SLICE_SIZE
    av_log(avctx, AV_LOG_VERBOSE, "MaxSliceSize: %"PRIu16"; ", co2->MaxSliceSize);
#endif
    av_log(avctx, AV_LOG_VERBOSE, "\n");

    av_log(avctx, AV_LOG_VERBOSE,
           "BitrateLimit: %s; MBBRC: %s; ExtBRC: %s\n",
           print_threestate(co2->BitrateLimit), print_threestate(co2->MBBRC),
           print_threestate(co2->ExtBRC));

#if QSV_HAVE_TRELLIS
    av_log(avctx, AV_LOG_VERBOSE, "Trellis: ");
    if (co2->Trellis & MFX_TRELLIS_OFF) {
        av_log(avctx, AV_LOG_VERBOSE, "off");
    } else if (!co2->Trellis) {
        av_log(avctx, AV_LOG_VERBOSE, "auto");
    } else {
        if (co2->Trellis & MFX_TRELLIS_I) av_log(avctx, AV_LOG_VERBOSE, "I");
        if (co2->Trellis & MFX_TRELLIS_P) av_log(avctx, AV_LOG_VERBOSE, "P");
        if (co2->Trellis & MFX_TRELLIS_B) av_log(avctx, AV_LOG_VERBOSE, "B");
    }
    av_log(avctx, AV_LOG_VERBOSE, "\n");
#endif

#if QSV_HAVE_VDENC
    av_log(avctx, AV_LOG_VERBOSE, "VDENC: %s\n", print_threestate(info->LowPower));
#endif

#if QSV_VERSION_ATLEAST(1, 8)
    av_log(avctx, AV_LOG_VERBOSE,
           "RepeatPPS: %s; NumMbPerSlice: %"PRIu16"; LookAheadDS: ",
           print_threestate(co2->RepeatPPS), co2->NumMbPerSlice);
    switch (co2->LookAheadDS) {
    case MFX_LOOKAHEAD_DS_OFF: av_log(avctx, AV_LOG_VERBOSE, "off");     break;
    case MFX_LOOKAHEAD_DS_2x:  av_log(avctx, AV_LOG_VERBOSE, "2x");      break;
    case MFX_LOOKAHEAD_DS_4x:  av_log(avctx, AV_LOG_VERBOSE, "4x");      break;
    default:                   av_log(avctx, AV_LOG_VERBOSE, "unknown"); break;
    }
    av_log(avctx, AV_LOG_VERBOSE, "\n");

    av_log(avctx, AV_LOG_VERBOSE, "AdaptiveI: %s; AdaptiveB: %s; BRefType: ",
           print_threestate(co2->AdaptiveI), print_threestate(co2->AdaptiveB));
    switch (co2->BRefType) {
    case MFX_B_REF_OFF:     av_log(avctx, AV_LOG_VERBOSE, "off");       break;
    case MFX_B_REF_PYRAMID: av_log(avctx, AV_LOG_VERBOSE, "pyramid");   break;
    default:                av_log(avctx, AV_LOG_VERBOSE, "auto");      break;
    }
    av_log(avctx, AV_LOG_VERBOSE, "\n");
#endif

#if QSV_VERSION_ATLEAST(1, 9)
    av_log(avctx, AV_LOG_VERBOSE,
           "MinQPI: %"PRIu8"; MaxQPI: %"PRIu8"; MinQPP: %"PRIu8"; MaxQPP: %"PRIu8"; MinQPB: %"PRIu8"; MaxQPB: %"PRIu8"\n",
           co2->MinQPI, co2->MaxQPI, co2->MinQPP, co2->MaxQPP, co2->MinQPB, co2->MaxQPB);
#endif
#endif

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        av_log(avctx, AV_LOG_VERBOSE, "Entropy coding: %s; MaxDecFrameBuffering: %"PRIu16"\n",
               co->CAVLC == MFX_CODINGOPTION_ON ? "CAVLC" : "CABAC", co->MaxDecFrameBuffering);
        av_log(avctx, AV_LOG_VERBOSE,
               "NalHrdConformance: %s; SingleSeiNalUnit: %s; VuiVclHrdParameters: %s VuiNalHrdParameters: %s\n",
               print_threestate(co->NalHrdConformance), print_threestate(co->SingleSeiNalUnit),
               print_threestate(co->VuiVclHrdParameters), print_threestate(co->VuiNalHrdParameters));
    }

    av_log(avctx, AV_LOG_VERBOSE, "FrameRateExtD: %"PRIu32"; FrameRateExtN: %"PRIu32" \n",
           info->FrameInfo.FrameRateExtD, info->FrameInfo.FrameRateExtN);

}

static int select_rc_mode(AVCodecContext *avctx, QSVEncContext *q)
{
    const char *rc_desc;
    mfxU16      rc_mode;

    int want_la     = q->look_ahead;
    int want_qscale = !!(avctx->flags & AV_CODEC_FLAG_QSCALE);
    int want_vcm    = q->vcm;

    if (want_la && !QSV_HAVE_LA) {
        av_log(avctx, AV_LOG_ERROR,
               "Lookahead ratecontrol mode requested, but is not supported by this SDK version\n");
        return AVERROR(ENOSYS);
    }
    if (want_vcm && !QSV_HAVE_VCM) {
        av_log(avctx, AV_LOG_ERROR,
               "VCM ratecontrol mode requested, but is not supported by this SDK version\n");
        return AVERROR(ENOSYS);
    }

    if (want_la + want_qscale + want_vcm > 1) {
        av_log(avctx, AV_LOG_ERROR,
               "More than one of: { constant qscale, lookahead, VCM } requested, "
               "only one of them can be used at a time.\n");
        return AVERROR(EINVAL);
    }

    if (!want_qscale && avctx->global_quality > 0 && !QSV_HAVE_ICQ){
        av_log(avctx, AV_LOG_ERROR,
               "ICQ ratecontrol mode requested, but is not supported by this SDK version\n");
        return AVERROR(ENOSYS);
    }

    if (want_qscale) {
        rc_mode = MFX_RATECONTROL_CQP;
        rc_desc = "constant quantization parameter (CQP)";
    }
#if QSV_HAVE_VCM
    else if (want_vcm) {
        rc_mode = MFX_RATECONTROL_VCM;
        rc_desc = "video conferencing mode (VCM)";
    }
#endif
#if QSV_HAVE_LA
    else if (want_la) {
        rc_mode = MFX_RATECONTROL_LA;
        rc_desc = "VBR with lookahead (LA)";

#if QSV_HAVE_ICQ
        if (avctx->global_quality > 0) {
            rc_mode = MFX_RATECONTROL_LA_ICQ;
            rc_desc = "intelligent constant quality with lookahead (LA_ICQ)";
        }
#endif
    }
#endif
#if QSV_HAVE_ICQ
    else if (avctx->global_quality > 0 && !avctx->rc_max_rate) {
        rc_mode = MFX_RATECONTROL_ICQ;
        rc_desc = "intelligent constant quality (ICQ)";
    }
#endif
    else if (avctx->rc_max_rate == avctx->bit_rate) {
        rc_mode = MFX_RATECONTROL_CBR;
        rc_desc = "constant bitrate (CBR)";
    }
#if QSV_HAVE_AVBR
    else if (!avctx->rc_max_rate) {
        rc_mode = MFX_RATECONTROL_AVBR;
        rc_desc = "average variable bitrate (AVBR)";
    }
#endif
#if QSV_HAVE_QVBR
    else if (avctx->global_quality > 0) {
        rc_mode = MFX_RATECONTROL_QVBR;
        rc_desc = "constant quality with VBR algorithm (QVBR)";
    }
#endif
    else {
        rc_mode = MFX_RATECONTROL_VBR;
        rc_desc = "variable bitrate (VBR)";
    }

    q->param.mfx.RateControlMethod = rc_mode;
    av_log(avctx, AV_LOG_VERBOSE, "Using the %s ratecontrol method\n", rc_desc);

    return 0;
}

static int check_enc_param(AVCodecContext *avctx, QSVEncContext *q)
{
    mfxVideoParam param_out = { .mfx.CodecId = q->param.mfx.CodecId };
    mfxStatus ret;

#define UNMATCH(x) (param_out.mfx.x != q->param.mfx.x)

    ret = MFXVideoENCODE_Query(q->session, &q->param, &param_out);

    if (ret < 0) {
        if (UNMATCH(CodecId))
            av_log(avctx, AV_LOG_ERROR, "Current codec type is unsupported\n");
        if (UNMATCH(CodecProfile))
            av_log(avctx, AV_LOG_ERROR, "Current profile is unsupported\n");
        if (UNMATCH(RateControlMethod))
            av_log(avctx, AV_LOG_ERROR, "Selected ratecontrol mode is unsupported\n");
        if (UNMATCH(LowPower))
              av_log(avctx, AV_LOG_ERROR, "Low power mode is unsupported\n");
        if (UNMATCH(FrameInfo.FrameRateExtN) || UNMATCH(FrameInfo.FrameRateExtD))
              av_log(avctx, AV_LOG_ERROR, "Current frame rate is unsupported\n");
        if (UNMATCH(FrameInfo.PicStruct))
              av_log(avctx, AV_LOG_ERROR, "Current picture structure is unsupported\n");
        if (UNMATCH(FrameInfo.Width) || UNMATCH(FrameInfo.Height))
              av_log(avctx, AV_LOG_ERROR, "Current resolution is unsupported\n");
        if (UNMATCH(FrameInfo.FourCC))
              av_log(avctx, AV_LOG_ERROR, "Current pixel format is unsupported\n");
        return 0;
    }
    return 1;
}

static int init_video_param_jpeg(AVCodecContext *avctx, QSVEncContext *q)
{
    enum AVPixelFormat sw_format = avctx->pix_fmt == AV_PIX_FMT_QSV ?
                                   avctx->sw_pix_fmt : avctx->pix_fmt;
    const AVPixFmtDescriptor *desc;
    int ret;

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0)
        return AVERROR_BUG;
    q->param.mfx.CodecId = ret;

    if (avctx->level > 0)
        q->param.mfx.CodecLevel = avctx->level;
    q->param.mfx.CodecProfile       = q->profile;

    desc = av_pix_fmt_desc_get(sw_format);
    if (!desc)
        return AVERROR_BUG;

    ff_qsv_map_pixfmt(sw_format, &q->param.mfx.FrameInfo.FourCC);

    q->param.mfx.FrameInfo.CropX          = 0;
    q->param.mfx.FrameInfo.CropY          = 0;
    q->param.mfx.FrameInfo.CropW          = avctx->width;
    q->param.mfx.FrameInfo.CropH          = avctx->height;
    q->param.mfx.FrameInfo.AspectRatioW   = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH   = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
    q->param.mfx.FrameInfo.BitDepthLuma   = desc->comp[0].depth;
    q->param.mfx.FrameInfo.BitDepthChroma = desc->comp[0].depth;
    q->param.mfx.FrameInfo.Shift          = desc->comp[0].depth > 8;

    q->param.mfx.FrameInfo.Width  = FFALIGN(avctx->width, 16);
    q->param.mfx.FrameInfo.Height = FFALIGN(avctx->height, 16);

    if (avctx->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx    = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;
        q->param.mfx.FrameInfo.Width  = frames_hwctx->surfaces[0].Info.Width;
        q->param.mfx.FrameInfo.Height = frames_hwctx->surfaces[0].Info.Height;
    }

    if (avctx->framerate.den > 0 && avctx->framerate.num > 0) {
        q->param.mfx.FrameInfo.FrameRateExtN = avctx->framerate.num;
        q->param.mfx.FrameInfo.FrameRateExtD = avctx->framerate.den;
    } else {
        q->param.mfx.FrameInfo.FrameRateExtN  = avctx->time_base.den;
        q->param.mfx.FrameInfo.FrameRateExtD  = avctx->time_base.num;
    }

    q->param.mfx.Interleaved          = 1;
    q->param.mfx.Quality              = av_clip(avctx->global_quality, 1, 100);
    q->param.mfx.RestartInterval      = 0;

    return 0;
}

static int init_video_param(AVCodecContext *avctx, QSVEncContext *q)
{
    enum AVPixelFormat sw_format = avctx->pix_fmt == AV_PIX_FMT_QSV ?
                                   avctx->sw_pix_fmt : avctx->pix_fmt;
    const AVPixFmtDescriptor *desc;
    float quant;
    int target_bitrate_kbps, max_bitrate_kbps, brc_param_multiplier;
    int buffer_size_in_kilobytes, initial_delay_in_kilobytes;
    int ret;

    ret = ff_qsv_codec_id_to_mfx(avctx->codec_id);
    if (ret < 0)
        return AVERROR_BUG;
    q->param.mfx.CodecId = ret;

    if (avctx->level > 0)
        q->param.mfx.CodecLevel = avctx->level;

    if (avctx->compression_level == FF_COMPRESSION_DEFAULT) {
        avctx->compression_level = q->preset;
    } else if (avctx->compression_level >= 0) {
        if (avctx->compression_level > MFX_TARGETUSAGE_BEST_SPEED) {
            av_log(avctx, AV_LOG_WARNING, "Invalid compression level: "
                    "valid range is 0-%d, using %d instead\n",
                    MFX_TARGETUSAGE_BEST_SPEED, MFX_TARGETUSAGE_BEST_SPEED);
            avctx->compression_level = MFX_TARGETUSAGE_BEST_SPEED;
        }
    }

#if QSV_HAVE_VDENC
    q->param.mfx.LowPower           = q->low_power ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
#endif
    q->param.mfx.CodecProfile       = q->profile;
    q->param.mfx.TargetUsage        = avctx->compression_level;
    q->param.mfx.GopPicSize         = FFMAX(0, avctx->gop_size);
    q->param.mfx.GopRefDist         = FFMAX(-1, avctx->max_b_frames) + 1;
    q->param.mfx.GopOptFlag         = avctx->flags & AV_CODEC_FLAG_CLOSED_GOP ?
                                      MFX_GOP_CLOSED : 0;
    q->param.mfx.IdrInterval        = q->idr_interval;
    q->param.mfx.NumSlice           = avctx->slices;
    q->param.mfx.NumRefFrame        = FFMAX(0, avctx->refs);
    q->param.mfx.EncodedOrder       = 0;
    q->param.mfx.BufferSizeInKB     = 0;

    desc = av_pix_fmt_desc_get(sw_format);
    if (!desc)
        return AVERROR_BUG;

    ff_qsv_map_pixfmt(sw_format, &q->param.mfx.FrameInfo.FourCC);

    q->param.mfx.FrameInfo.CropX          = 0;
    q->param.mfx.FrameInfo.CropY          = 0;
    q->param.mfx.FrameInfo.CropW          = avctx->width;
    q->param.mfx.FrameInfo.CropH          = avctx->height;
    q->param.mfx.FrameInfo.AspectRatioW   = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH   = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
    q->param.mfx.FrameInfo.BitDepthLuma   = desc->comp[0].depth;
    q->param.mfx.FrameInfo.BitDepthChroma = desc->comp[0].depth;
    q->param.mfx.FrameInfo.Shift          = desc->comp[0].depth > 8;

    // If the minor version is greater than or equal to 19,
    // then can use the same alignment settings as H.264 for HEVC
    q->width_align = (avctx->codec_id != AV_CODEC_ID_HEVC ||
                      QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 19)) ? 16 : 32;
    q->param.mfx.FrameInfo.Width = FFALIGN(avctx->width, q->width_align);

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        // it is important that PicStruct be setup correctly from the
        // start--otherwise, encoding doesn't work and results in a bunch
        // of incompatible video parameter errors
        q->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_FIELD_TFF;
        // height alignment always must be 32 for interlaced video
        q->height_align = 32;
    } else {
        q->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        // for progressive video, the height should be aligned to 16 for
        // H.264.  For HEVC, depending on the version of MFX, it should be
        // either 32 or 16.  The lower number is better if possible.
        q->height_align = avctx->codec_id == AV_CODEC_ID_HEVC ? 32 : 16;
    }
    q->param.mfx.FrameInfo.Height = FFALIGN(avctx->height, q->height_align);

    if (avctx->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;
        q->param.mfx.FrameInfo.Width  = frames_hwctx->surfaces[0].Info.Width;
        q->param.mfx.FrameInfo.Height = frames_hwctx->surfaces[0].Info.Height;
    }

    if (avctx->framerate.den > 0 && avctx->framerate.num > 0) {
        q->param.mfx.FrameInfo.FrameRateExtN = avctx->framerate.num;
        q->param.mfx.FrameInfo.FrameRateExtD = avctx->framerate.den;
    } else {
        q->param.mfx.FrameInfo.FrameRateExtN  = avctx->time_base.den;
        q->param.mfx.FrameInfo.FrameRateExtD  = avctx->time_base.num;
    }

    ret = select_rc_mode(avctx, q);
    if (ret < 0)
        return ret;

    //libmfx BRC parameters are 16 bits thus maybe overflow, then BRCParamMultiplier is needed
    buffer_size_in_kilobytes   = avctx->rc_buffer_size / 8000;
    initial_delay_in_kilobytes = avctx->rc_initial_buffer_occupancy / 1000;
    target_bitrate_kbps        = avctx->bit_rate / 1000;
    max_bitrate_kbps           = avctx->rc_max_rate / 1000;
    brc_param_multiplier       = (FFMAX(FFMAX3(target_bitrate_kbps, max_bitrate_kbps, buffer_size_in_kilobytes),
                                  initial_delay_in_kilobytes) + 0x10000) / 0x10000;

    switch (q->param.mfx.RateControlMethod) {
    case MFX_RATECONTROL_CBR:
    case MFX_RATECONTROL_VBR:
#if QSV_HAVE_VCM
    case MFX_RATECONTROL_VCM:
#endif
#if QSV_HAVE_QVBR
    case MFX_RATECONTROL_QVBR:
#endif
        q->param.mfx.BufferSizeInKB   = buffer_size_in_kilobytes / brc_param_multiplier;
        q->param.mfx.InitialDelayInKB = initial_delay_in_kilobytes / brc_param_multiplier;
        q->param.mfx.TargetKbps       = target_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.MaxKbps          = max_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
#if QSV_HAVE_QVBR
        if (q->param.mfx.RateControlMethod == MFX_RATECONTROL_QVBR)
            q->extco3.QVBRQuality = av_clip(avctx->global_quality, 0, 51);
#endif
        break;
    case MFX_RATECONTROL_CQP:
        quant = avctx->global_quality / FF_QP2LAMBDA;

        q->param.mfx.QPI = av_clip(quant * fabs(avctx->i_quant_factor) + avctx->i_quant_offset, 0, 51);
        q->param.mfx.QPP = av_clip(quant, 0, 51);
        q->param.mfx.QPB = av_clip(quant * fabs(avctx->b_quant_factor) + avctx->b_quant_offset, 0, 51);

        break;
#if QSV_HAVE_AVBR
    case MFX_RATECONTROL_AVBR:
        q->param.mfx.TargetKbps  = target_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.Convergence = q->avbr_convergence;
        q->param.mfx.Accuracy    = q->avbr_accuracy;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
        break;
#endif
#if QSV_HAVE_LA
    case MFX_RATECONTROL_LA:
        q->param.mfx.TargetKbps  = target_bitrate_kbps / brc_param_multiplier;
        q->extco2.LookAheadDepth = q->look_ahead_depth;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
        break;
#if QSV_HAVE_ICQ
    case MFX_RATECONTROL_LA_ICQ:
        q->extco2.LookAheadDepth = q->look_ahead_depth;
    case MFX_RATECONTROL_ICQ:
        q->param.mfx.ICQQuality  = avctx->global_quality;
        break;
#endif
#endif
    }

    // the HEVC encoder plugin currently fails if coding options
    // are provided
    if (avctx->codec_id != AV_CODEC_ID_HEVC) {
        q->extco.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION;
        q->extco.Header.BufferSz      = sizeof(q->extco);

        q->extco.PicTimingSEI         = q->pic_timing_sei ?
                                        MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN;

        if (q->rdo >= 0)
            q->extco.RateDistortionOpt = q->rdo > 0 ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

        if (avctx->codec_id == AV_CODEC_ID_H264) {
#if FF_API_CODER_TYPE
FF_DISABLE_DEPRECATION_WARNINGS
            if (avctx->coder_type >= 0)
                q->cavlc = avctx->coder_type == FF_CODER_TYPE_VLC;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            q->extco.CAVLC = q->cavlc ? MFX_CODINGOPTION_ON
                                      : MFX_CODINGOPTION_UNKNOWN;

            if (avctx->strict_std_compliance != FF_COMPLIANCE_NORMAL)
                q->extco.NalHrdConformance = avctx->strict_std_compliance > FF_COMPLIANCE_NORMAL ?
                                             MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

            if (q->single_sei_nal_unit >= 0)
                q->extco.SingleSeiNalUnit = q->single_sei_nal_unit ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->recovery_point_sei >= 0)
                q->extco.RecoveryPointSEI = q->recovery_point_sei ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            q->extco.MaxDecFrameBuffering = q->max_dec_frame_buffering;
            q->extco.AUDelimiter          = q->aud ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
        }

        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco;

        if (avctx->codec_id == AV_CODEC_ID_H264) {
#if QSV_HAVE_CO2
            q->extco2.Header.BufferId     = MFX_EXTBUFF_CODING_OPTION2;
            q->extco2.Header.BufferSz     = sizeof(q->extco2);

            if (q->int_ref_type >= 0)
                q->extco2.IntRefType = q->int_ref_type;
            if (q->int_ref_cycle_size >= 0)
                q->extco2.IntRefCycleSize = q->int_ref_cycle_size;
            if (q->int_ref_qp_delta != INT16_MIN)
                q->extco2.IntRefQPDelta = q->int_ref_qp_delta;

            if (q->bitrate_limit >= 0)
                q->extco2.BitrateLimit = q->bitrate_limit ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->mbbrc >= 0)
                q->extco2.MBBRC = q->mbbrc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->extbrc >= 0)
                q->extco2.ExtBRC = q->extbrc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

            if (q->max_frame_size >= 0)
                q->extco2.MaxFrameSize = q->max_frame_size;
#if QSV_HAVE_MAX_SLICE_SIZE
            if (q->max_slice_size >= 0)
                q->extco2.MaxSliceSize = q->max_slice_size;
#endif

#if QSV_HAVE_TRELLIS
            if (avctx->trellis >= 0)
                q->extco2.Trellis = (avctx->trellis == 0) ? MFX_TRELLIS_OFF : (MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B);
            else
                q->extco2.Trellis = MFX_TRELLIS_UNKNOWN;
#endif

#if QSV_VERSION_ATLEAST(1, 8)
            q->extco2.LookAheadDS = q->look_ahead_downsampling;
            q->extco2.RepeatPPS   = q->repeat_pps ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
            if (avctx->b_frame_strategy >= 0)
                q->b_strategy = avctx->b_frame_strategy;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            if (q->b_strategy >= 0)
                q->extco2.BRefType = q->b_strategy ? MFX_B_REF_PYRAMID : MFX_B_REF_OFF;
            if (q->adaptive_i >= 0)
                q->extco2.AdaptiveI = q->adaptive_i ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->adaptive_b >= 0)
                q->extco2.AdaptiveB = q->adaptive_b ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
#endif

#if QSV_VERSION_ATLEAST(1, 9)
            if (avctx->qmin >= 0 && avctx->qmax >= 0 && avctx->qmin > avctx->qmax) {
                av_log(avctx, AV_LOG_ERROR, "qmin and or qmax are set but invalid, please make sure min <= max\n");
                return AVERROR(EINVAL);
            }
            if (avctx->qmin >= 0) {
                q->extco2.MinQPI = avctx->qmin > 51 ? 51 : avctx->qmin;
                q->extco2.MinQPP = q->extco2.MinQPB = q->extco2.MinQPI;
            }
            if (avctx->qmax >= 0) {
                q->extco2.MaxQPI = avctx->qmax > 51 ? 51 : avctx->qmax;
                q->extco2.MaxQPP = q->extco2.MaxQPB = q->extco2.MaxQPI;
            }
#endif
            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco2;
#endif

#if QSV_HAVE_MF
            if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 25)) {
                q->extmfp.Header.BufferId     = MFX_EXTBUFF_MULTI_FRAME_PARAM;
                q->extmfp.Header.BufferSz     = sizeof(q->extmfp);

                q->extmfp.MFMode = q->mfmode;
                av_log(avctx,AV_LOG_VERBOSE,"MFMode:%d\n", q->extmfp.MFMode);
                q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extmfp;
            }
#endif
        }
#if QSV_HAVE_CO3
        q->extco3.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION3;
        q->extco3.Header.BufferSz      = sizeof(q->extco3);
        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco3;
#endif
    }

    if (!check_enc_param(avctx,q)) {
        av_log(avctx, AV_LOG_ERROR,
               "some encoding parameters are not supported by the QSV "
               "runtime. Please double check the input parameters.\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int qsv_retrieve_enc_jpeg_params(AVCodecContext *avctx, QSVEncContext *q)
{
    int ret = 0;

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error calling GetVideoParam");

    q->packet_size = q->param.mfx.BufferSizeInKB * q->param.mfx.BRCParamMultiplier * 1000;

    // for qsv mjpeg the return value maybe 0 so alloc the buffer
    if (q->packet_size == 0)
        q->packet_size = q->param.mfx.FrameInfo.Height * q->param.mfx.FrameInfo.Width * 4;

    return 0;
}

static int qsv_retrieve_enc_params(AVCodecContext *avctx, QSVEncContext *q)
{
    AVCPBProperties *cpb_props;

    uint8_t sps_buf[128];
    uint8_t pps_buf[128];

    mfxExtCodingOptionSPSPPS extradata = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS,
        .Header.BufferSz = sizeof(extradata),
        .SPSBuffer = sps_buf, .SPSBufSize = sizeof(sps_buf),
        .PPSBuffer = pps_buf, .PPSBufSize = sizeof(pps_buf)
    };

    mfxExtCodingOption co = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION,
        .Header.BufferSz = sizeof(co),
    };
#if QSV_HAVE_CO2
    mfxExtCodingOption2 co2 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION2,
        .Header.BufferSz = sizeof(co2),
    };
#endif
#if QSV_HAVE_CO3
    mfxExtCodingOption3 co3 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION3,
        .Header.BufferSz = sizeof(co3),
    };
#endif

    mfxExtBuffer *ext_buffers[] = {
        (mfxExtBuffer*)&extradata,
        (mfxExtBuffer*)&co,
#if QSV_HAVE_CO2
        (mfxExtBuffer*)&co2,
#endif
#if QSV_HAVE_CO3
        (mfxExtBuffer*)&co3,
#endif
    };

    int need_pps = avctx->codec_id != AV_CODEC_ID_MPEG2VIDEO;
    int ret;

    q->param.ExtParam    = ext_buffers;
    q->param.NumExtParam = FF_ARRAY_ELEMS(ext_buffers);

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error calling GetVideoParam");

    q->packet_size = q->param.mfx.BufferSizeInKB * q->param.mfx.BRCParamMultiplier * 1000;

    if (!extradata.SPSBufSize || (need_pps && !extradata.PPSBufSize)) {
        av_log(avctx, AV_LOG_ERROR, "No extradata returned from libmfx.\n");
        return AVERROR_UNKNOWN;
    }

    avctx->extradata = av_malloc(extradata.SPSBufSize + need_pps * extradata.PPSBufSize +
                                 AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    memcpy(avctx->extradata,                        sps_buf, extradata.SPSBufSize);
    if (need_pps)
        memcpy(avctx->extradata + extradata.SPSBufSize, pps_buf, extradata.PPSBufSize);
    avctx->extradata_size = extradata.SPSBufSize + need_pps * extradata.PPSBufSize;
    memset(avctx->extradata + avctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = avctx->rc_max_rate;
    cpb_props->min_bitrate = avctx->rc_min_rate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = avctx->rc_buffer_size;

    dump_video_param(avctx, q, ext_buffers + 1);

    return 0;
}

static int qsv_init_opaque_alloc(AVCodecContext *avctx, QSVEncContext *q)
{
    AVQSVContext *qsv = avctx->hwaccel_context;
    mfxFrameSurface1 *surfaces;
    int nb_surfaces, i;

    nb_surfaces = qsv->nb_opaque_surfaces + q->req.NumFrameSuggested;

    q->opaque_alloc_buf = av_buffer_allocz(sizeof(*surfaces) * nb_surfaces);
    if (!q->opaque_alloc_buf)
        return AVERROR(ENOMEM);

    q->opaque_surfaces = av_malloc_array(nb_surfaces, sizeof(*q->opaque_surfaces));
    if (!q->opaque_surfaces)
        return AVERROR(ENOMEM);

    surfaces = (mfxFrameSurface1*)q->opaque_alloc_buf->data;
    for (i = 0; i < nb_surfaces; i++) {
        surfaces[i].Info      = q->req.Info;
        q->opaque_surfaces[i] = surfaces + i;
    }

    q->opaque_alloc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    q->opaque_alloc.Header.BufferSz = sizeof(q->opaque_alloc);
    q->opaque_alloc.In.Surfaces     = q->opaque_surfaces;
    q->opaque_alloc.In.NumSurface   = nb_surfaces;
    q->opaque_alloc.In.Type         = q->req.Type;

    q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->opaque_alloc;

    qsv->nb_opaque_surfaces = nb_surfaces;
    qsv->opaque_surfaces    = q->opaque_alloc_buf;
    qsv->opaque_alloc_type  = q->req.Type;

    return 0;
}

static int qsvenc_init_session(AVCodecContext *avctx, QSVEncContext *q)
{
    int ret;

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;
        q->session = qsv->session;
    } else if (avctx->hw_frames_ctx) {
        q->frames_ctx.hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
        if (!q->frames_ctx.hw_frames_ctx)
            return AVERROR(ENOMEM);

        ret = ff_qsv_init_session_frames(avctx, &q->internal_session,
                                         &q->frames_ctx, q->load_plugins,
                                         q->param.IOPattern == MFX_IOPATTERN_IN_OPAQUE_MEMORY);
        if (ret < 0) {
            av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
            return ret;
        }

        q->session = q->internal_session;
    } else if (avctx->hw_device_ctx) {
        ret = ff_qsv_init_session_device(avctx, &q->internal_session,
                                         avctx->hw_device_ctx, q->load_plugins);
        if (ret < 0)
            return ret;

        q->session = q->internal_session;
    } else {
        ret = ff_qsv_init_internal_session(avctx, &q->internal_session,
                                           q->load_plugins);
        if (ret < 0)
            return ret;

        q->session = q->internal_session;
    }

    return 0;
}

static inline unsigned int qsv_fifo_item_size(void)
{
    return sizeof(AVPacket) + sizeof(mfxSyncPoint*) + sizeof(mfxBitstream*);
}

static inline unsigned int qsv_fifo_size(const AVFifoBuffer* fifo)
{
    return av_fifo_size(fifo)/qsv_fifo_item_size();
}

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q)
{
    int iopattern = 0;
    int opaque_alloc = 0;
    int ret;

    q->param.AsyncDepth = q->async_depth;

    q->async_fifo = av_fifo_alloc(q->async_depth * qsv_fifo_item_size());
    if (!q->async_fifo)
        return AVERROR(ENOMEM);

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;

        iopattern    = qsv->iopattern;
        opaque_alloc = qsv->opaque_alloc;
    }

    if (avctx->hw_frames_ctx) {
        AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;

        if (!iopattern) {
            if (frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME)
                iopattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
            else if (frames_hwctx->frame_type &
                     (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))
                iopattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
        }
    }

    if (!iopattern)
        iopattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    q->param.IOPattern = iopattern;

    ret = qsvenc_init_session(avctx, q);
    if (ret < 0)
        return ret;

    ret = MFXQueryVersion(q->session,&q->ver);
    if (ret < 0) {
        return ff_qsv_print_error(avctx, ret,
                                  "Error querying mfx version");
    }

    // in the mfxInfoMFX struct, JPEG is different from other codecs
    switch (avctx->codec_id) {
    case AV_CODEC_ID_MJPEG:
        ret = init_video_param_jpeg(avctx, q);
        break;
    default:
        ret = init_video_param(avctx, q);
        break;
    }
    if (ret < 0)
        return ret;

    ret = MFXVideoENCODE_Query(q->session, &q->param, &q->param);
    if (ret == MFX_WRN_PARTIAL_ACCELERATION) {
        av_log(avctx, AV_LOG_WARNING, "Encoder will work with partial HW acceleration\n");
    } else if (ret < 0) {
        return ff_qsv_print_error(avctx, ret,
                                  "Error querying encoder params");
    }

    ret = MFXVideoENCODE_QueryIOSurf(q->session, &q->param, &q->req);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error querying (IOSurf) the encoding parameters");

    if (opaque_alloc) {
        ret = qsv_init_opaque_alloc(avctx, q);
        if (ret < 0)
            return ret;
    }

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;
        int i, j;

        q->extparam = av_mallocz_array(qsv->nb_ext_buffers + q->nb_extparam_internal,
                                       sizeof(*q->extparam));
        if (!q->extparam)
            return AVERROR(ENOMEM);

        q->param.ExtParam = q->extparam;
        for (i = 0; i < qsv->nb_ext_buffers; i++)
            q->param.ExtParam[i] = qsv->ext_buffers[i];
        q->param.NumExtParam = qsv->nb_ext_buffers;

        for (i = 0; i < q->nb_extparam_internal; i++) {
            for (j = 0; j < qsv->nb_ext_buffers; j++) {
                if (qsv->ext_buffers[j]->BufferId == q->extparam_internal[i]->BufferId)
                    break;
            }
            if (j < qsv->nb_ext_buffers)
                continue;

            q->param.ExtParam[q->param.NumExtParam++] = q->extparam_internal[i];
        }
    } else {
        q->param.ExtParam    = q->extparam_internal;
        q->param.NumExtParam = q->nb_extparam_internal;
    }

    ret = MFXVideoENCODE_Init(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error initializing the encoder");
    else if (ret > 0)
        ff_qsv_print_warning(avctx, ret,
                             "Warning in encoder initialization");

    switch (avctx->codec_id) {
    case AV_CODEC_ID_MJPEG:
        ret = qsv_retrieve_enc_jpeg_params(avctx, q);
        break;
    default:
        ret = qsv_retrieve_enc_params(avctx, q);
        break;
    }
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error retrieving encoding parameters.\n");
        return ret;
    }

    q->avctx = avctx;

    return 0;
}

static void free_encoder_ctrl_payloads(mfxEncodeCtrl* enc_ctrl)
{
    if (enc_ctrl) {
        int i;
        for (i = 0; i < enc_ctrl->NumPayload && i < QSV_MAX_ENC_PAYLOAD; i++) {
            av_free(enc_ctrl->Payload[i]);
        }
        enc_ctrl->NumPayload = 0;
    }
}

static void clear_unused_frames(QSVEncContext *q)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (cur->used && !cur->surface.Data.Locked) {
            free_encoder_ctrl_payloads(&cur->enc_ctrl);
            if (cur->frame->format == AV_PIX_FMT_QSV) {
                av_frame_unref(cur->frame);
            }
            cur->used = 0;
        }
        cur = cur->next;
    }
}

static int get_free_frame(QSVEncContext *q, QSVFrame **f)
{
    QSVFrame *frame, **last;

    clear_unused_frames(q);

    frame = q->work_frames;
    last  = &q->work_frames;
    while (frame) {
        if (!frame->used) {
            *f = frame;
            frame->used = 1;
            return 0;
        }

        last  = &frame->next;
        frame = frame->next;
    }

    frame = av_mallocz(sizeof(*frame));
    if (!frame)
        return AVERROR(ENOMEM);
    frame->frame = av_frame_alloc();
    if (!frame->frame) {
        av_freep(&frame);
        return AVERROR(ENOMEM);
    }
    frame->enc_ctrl.Payload = av_mallocz(sizeof(mfxPayload*) * QSV_MAX_ENC_PAYLOAD);
    if (!frame->enc_ctrl.Payload) {
        av_freep(&frame);
        return AVERROR(ENOMEM);
    }
    *last = frame;

    *f = frame;
    frame->used = 1;

    return 0;
}

static int submit_frame(QSVEncContext *q, const AVFrame *frame,
                        QSVFrame **new_frame)
{
    QSVFrame *qf;
    int ret;

    ret = get_free_frame(q, &qf);
    if (ret < 0)
        return ret;

    if (frame->format == AV_PIX_FMT_QSV) {
        ret = av_frame_ref(qf->frame, frame);
        if (ret < 0)
            return ret;

        qf->surface = *(mfxFrameSurface1*)qf->frame->data[3];

        if (q->frames_ctx.mids) {
            ret = ff_qsv_find_surface_idx(&q->frames_ctx, qf);
            if (ret < 0)
                return ret;

            qf->surface.Data.MemId = &q->frames_ctx.mids[ret];
        }
    } else {
        /* make a copy if the input is not padded as libmfx requires */
        /* and to make allocation continious for data[0]/data[1] */
         if ((frame->height & 31 || frame->linesize[0] & (q->width_align - 1)) ||
            (frame->data[1] - frame->data[0] != frame->linesize[0] * FFALIGN(qf->frame->height, q->height_align))) {
            qf->frame->height = FFALIGN(frame->height, q->height_align);
            qf->frame->width  = FFALIGN(frame->width, q->width_align);

            qf->frame->format = frame->format;

            if (!qf->frame->data[0]) {
                ret = av_frame_get_buffer(qf->frame, q->width_align);
                if (ret < 0)
                    return ret;
            }

            qf->frame->height = frame->height;
            qf->frame->width  = frame->width;

            ret = av_frame_copy(qf->frame, frame);
            if (ret < 0) {
                av_frame_unref(qf->frame);
                return ret;
            }
        } else {
            ret = av_frame_ref(qf->frame, frame);
            if (ret < 0)
                return ret;
        }

        qf->surface.Info = q->param.mfx.FrameInfo;

        qf->surface.Info.PicStruct =
            !frame->interlaced_frame ? MFX_PICSTRUCT_PROGRESSIVE :
            frame->top_field_first   ? MFX_PICSTRUCT_FIELD_TFF :
                                       MFX_PICSTRUCT_FIELD_BFF;
        if (frame->repeat_pict == 1)
            qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FIELD_REPEATED;
        else if (frame->repeat_pict == 2)
            qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FRAME_DOUBLING;
        else if (frame->repeat_pict == 4)
            qf->surface.Info.PicStruct |= MFX_PICSTRUCT_FRAME_TRIPLING;

        qf->surface.Data.PitchLow  = qf->frame->linesize[0];
        qf->surface.Data.Y         = qf->frame->data[0];
        qf->surface.Data.UV        = qf->frame->data[1];
    }

    qf->surface.Data.TimeStamp = av_rescale_q(frame->pts, q->avctx->time_base, (AVRational){1, 90000});

    *new_frame = qf;

    return 0;
}

static void print_interlace_msg(AVCodecContext *avctx, QSVEncContext *q)
{
    if (q->param.mfx.CodecId == MFX_CODEC_AVC) {
        if (q->param.mfx.CodecProfile == MFX_PROFILE_AVC_BASELINE ||
            q->param.mfx.CodecLevel < MFX_LEVEL_AVC_21 ||
            q->param.mfx.CodecLevel > MFX_LEVEL_AVC_41)
            av_log(avctx, AV_LOG_WARNING,
                   "Interlaced coding is supported"
                   " at Main/High Profile Level 2.2-4.0\n");
    }
}

static int encode_frame(AVCodecContext *avctx, QSVEncContext *q,
                        const AVFrame *frame)
{
    AVPacket new_pkt = { 0 };
    mfxBitstream *bs;
#if QSV_VERSION_ATLEAST(1, 26)
    mfxExtAVCEncodedFrameInfo *enc_info;
    mfxExtBuffer **enc_buf;
#endif

    mfxFrameSurface1 *surf = NULL;
    mfxSyncPoint *sync     = NULL;
    QSVFrame *qsv_frame = NULL;
    mfxEncodeCtrl* enc_ctrl = NULL;
    int ret;

    if (frame) {
        ret = submit_frame(q, frame, &qsv_frame);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error submitting the frame for encoding.\n");
            return ret;
        }
    }
    if (qsv_frame) {
        surf = &qsv_frame->surface;
        enc_ctrl = &qsv_frame->enc_ctrl;
        memset(enc_ctrl, 0, sizeof(mfxEncodeCtrl));

        if (frame->pict_type == AV_PICTURE_TYPE_I) {
            enc_ctrl->FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_REF;
            if (q->forced_idr)
                enc_ctrl->FrameType |= MFX_FRAMETYPE_IDR;
        }
    }

    ret = av_new_packet(&new_pkt, q->packet_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating the output packet\n");
        return ret;
    }

    bs = av_mallocz(sizeof(*bs));
    if (!bs) {
        av_packet_unref(&new_pkt);
        return AVERROR(ENOMEM);
    }
    bs->Data      = new_pkt.data;
    bs->MaxLength = new_pkt.size;

#if QSV_VERSION_ATLEAST(1, 26)
    if (avctx->codec_id == AV_CODEC_ID_H264) {
        enc_info = av_mallocz(sizeof(*enc_info));
        if (!enc_info)
            return AVERROR(ENOMEM);

        enc_info->Header.BufferId = MFX_EXTBUFF_ENCODED_FRAME_INFO;
        enc_info->Header.BufferSz = sizeof (*enc_info);
        bs->NumExtParam = 1;
        enc_buf = av_mallocz(sizeof(mfxExtBuffer *));
        if (!enc_buf)
            return AVERROR(ENOMEM);
        enc_buf[0] = (mfxExtBuffer *)enc_info;

        bs->ExtParam = enc_buf;
    }
#endif

    if (q->set_encode_ctrl_cb) {
        q->set_encode_ctrl_cb(avctx, frame, &qsv_frame->enc_ctrl);
    }

    sync = av_mallocz(sizeof(*sync));
    if (!sync) {
        av_freep(&bs);
 #if QSV_VERSION_ATLEAST(1, 26)
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
 #endif
        av_packet_unref(&new_pkt);
        return AVERROR(ENOMEM);
    }

    do {
        ret = MFXVideoENCODE_EncodeFrameAsync(q->session, enc_ctrl, surf, bs, sync);
        if (ret == MFX_WRN_DEVICE_BUSY)
            av_usleep(500);
    } while (ret == MFX_WRN_DEVICE_BUSY || ret == MFX_WRN_IN_EXECUTION);

    if (ret > 0)
        ff_qsv_print_warning(avctx, ret, "Warning during encoding");

    if (ret < 0) {
        av_packet_unref(&new_pkt);
        av_freep(&bs);
#if QSV_VERSION_ATLEAST(1, 26)
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
#endif
        av_freep(&sync);
        return (ret == MFX_ERR_MORE_DATA) ?
               0 : ff_qsv_print_error(avctx, ret, "Error during encoding");
    }

    if (ret == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM && frame->interlaced_frame)
        print_interlace_msg(avctx, q);

    if (*sync) {
        av_fifo_generic_write(q->async_fifo, &new_pkt, sizeof(new_pkt), NULL);
        av_fifo_generic_write(q->async_fifo, &sync,    sizeof(sync),    NULL);
        av_fifo_generic_write(q->async_fifo, &bs,      sizeof(bs),    NULL);
    } else {
        av_freep(&sync);
        av_packet_unref(&new_pkt);
        av_freep(&bs);
#if QSV_VERSION_ATLEAST(1, 26)
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
#endif
    }

    return 0;
}

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    int ret;

    ret = encode_frame(avctx, q, frame);
    if (ret < 0)
        return ret;

    if ((qsv_fifo_size(q->async_fifo) >= q->async_depth) ||
        (!frame && av_fifo_size(q->async_fifo))) {
        AVPacket new_pkt;
        mfxBitstream *bs;
        mfxSyncPoint *sync;
#if QSV_VERSION_ATLEAST(1, 26)
        mfxExtAVCEncodedFrameInfo *enc_info;
        mfxExtBuffer **enc_buf;
#endif
        enum AVPictureType pict_type;

        av_fifo_generic_read(q->async_fifo, &new_pkt, sizeof(new_pkt), NULL);
        av_fifo_generic_read(q->async_fifo, &sync,    sizeof(sync),    NULL);
        av_fifo_generic_read(q->async_fifo, &bs,      sizeof(bs),      NULL);

        do {
            ret = MFXVideoCORE_SyncOperation(q->session, *sync, 1000);
        } while (ret == MFX_WRN_IN_EXECUTION);

        new_pkt.dts  = av_rescale_q(bs->DecodeTimeStamp, (AVRational){1, 90000}, avctx->time_base);
        new_pkt.pts  = av_rescale_q(bs->TimeStamp,       (AVRational){1, 90000}, avctx->time_base);
        new_pkt.size = bs->DataLength;

        if (bs->FrameType & MFX_FRAMETYPE_IDR || bs->FrameType & MFX_FRAMETYPE_xIDR) {
            new_pkt.flags |= AV_PKT_FLAG_KEY;
            pict_type = AV_PICTURE_TYPE_I;
        } else if (bs->FrameType & MFX_FRAMETYPE_I || bs->FrameType & MFX_FRAMETYPE_xI)
            pict_type = AV_PICTURE_TYPE_I;
        else if (bs->FrameType & MFX_FRAMETYPE_P || bs->FrameType & MFX_FRAMETYPE_xP)
            pict_type = AV_PICTURE_TYPE_P;
        else if (bs->FrameType & MFX_FRAMETYPE_B || bs->FrameType & MFX_FRAMETYPE_xB)
            pict_type = AV_PICTURE_TYPE_B;
        else if (bs->FrameType == MFX_FRAMETYPE_UNKNOWN) {
            pict_type = AV_PICTURE_TYPE_NONE;
            av_log(avctx, AV_LOG_WARNING, "Unknown FrameType, set pict_type to AV_PICTURE_TYPE_NONE.\n");
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid FrameType:%d.\n", bs->FrameType);
            return AVERROR_INVALIDDATA;
        }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

#if QSV_VERSION_ATLEAST(1, 26)
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            enc_buf = bs->ExtParam;
            enc_info = (mfxExtAVCEncodedFrameInfo *)(*bs->ExtParam);
            ff_side_data_set_encoder_stats(&new_pkt,
                enc_info->QP * FF_QP2LAMBDA, NULL, 0, pict_type);
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
#endif
        av_freep(&bs);
        av_freep(&sync);

        if (pkt->data) {
            if (pkt->size < new_pkt.size) {
                av_log(avctx, AV_LOG_ERROR, "Submitted buffer not large enough: %d < %d\n",
                       pkt->size, new_pkt.size);
                av_packet_unref(&new_pkt);
                return AVERROR(EINVAL);
            }

            memcpy(pkt->data, new_pkt.data, new_pkt.size);
            pkt->size = new_pkt.size;

            ret = av_packet_copy_props(pkt, &new_pkt);
            av_packet_unref(&new_pkt);
            if (ret < 0)
                return ret;
        } else
            *pkt = new_pkt;

        *got_packet = 1;
    }

    return 0;
}

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q)
{
    QSVFrame *cur;

    if (q->session)
        MFXVideoENCODE_Close(q->session);
    if (q->internal_session)
        MFXClose(q->internal_session);
    q->session          = NULL;
    q->internal_session = NULL;

    av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
    av_buffer_unref(&q->frames_ctx.mids_buf);

    cur = q->work_frames;
    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        av_free(cur->enc_ctrl.Payload);
        av_freep(&cur);
        cur = q->work_frames;
    }

    while (q->async_fifo && av_fifo_size(q->async_fifo)) {
        AVPacket pkt;
        mfxSyncPoint *sync;
        mfxBitstream *bs;

        av_fifo_generic_read(q->async_fifo, &pkt,  sizeof(pkt),  NULL);
        av_fifo_generic_read(q->async_fifo, &sync, sizeof(sync), NULL);
        av_fifo_generic_read(q->async_fifo, &bs,   sizeof(bs),   NULL);

        av_freep(&sync);
        av_freep(&bs);
        av_packet_unref(&pkt);
    }
    av_fifo_free(q->async_fifo);
    q->async_fifo = NULL;

    av_freep(&q->opaque_surfaces);
    av_buffer_unref(&q->opaque_alloc_buf);

    av_freep(&q->extparam);

    return 0;
}
