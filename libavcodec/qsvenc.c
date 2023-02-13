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

#include "config_components.h"

#include <string.h>
#include <sys/types.h>
#include <mfxvideo.h>

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
#include "packet_internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

struct profile_names {
    mfxU16 profile;
    const char *name;
};

static const struct profile_names avc_profiles[] = {
    { MFX_PROFILE_AVC_BASELINE,                 "avc baseline"              },
    { MFX_PROFILE_AVC_MAIN,                     "avc main"                  },
    { MFX_PROFILE_AVC_EXTENDED,                 "avc extended"              },
    { MFX_PROFILE_AVC_HIGH,                     "avc high"                  },
    { MFX_PROFILE_AVC_HIGH_422,                 "avc high 422"              },
    { MFX_PROFILE_AVC_CONSTRAINED_BASELINE,     "avc constrained baseline"  },
    { MFX_PROFILE_AVC_CONSTRAINED_HIGH,         "avc constrained high"      },
    { MFX_PROFILE_AVC_PROGRESSIVE_HIGH,         "avc progressive high"      },
};

static const struct profile_names mpeg2_profiles[] = {
    { MFX_PROFILE_MPEG2_SIMPLE,                 "mpeg2 simple"                },
    { MFX_PROFILE_MPEG2_MAIN,                   "mpeg2 main"                  },
    { MFX_PROFILE_MPEG2_HIGH,                   "mpeg2 high"                  },
};

static const struct profile_names hevc_profiles[] = {
    { MFX_PROFILE_HEVC_MAIN,                    "hevc main"                  },
    { MFX_PROFILE_HEVC_MAIN10,                  "hevc main10"                },
    { MFX_PROFILE_HEVC_MAINSP,                  "hevc mainsp"                },
    { MFX_PROFILE_HEVC_REXT,                    "hevc rext"                  },
#if QSV_VERSION_ATLEAST(1, 32)
    { MFX_PROFILE_HEVC_SCC,                     "hevc scc"                   },
#endif
};

static const struct profile_names vp9_profiles[] = {
    { MFX_PROFILE_VP9_0,                        "vp9 0"                     },
    { MFX_PROFILE_VP9_1,                        "vp9 1"                     },
    { MFX_PROFILE_VP9_2,                        "vp9 2"                     },
    { MFX_PROFILE_VP9_3,                        "vp9 3"                     },
};

static const struct profile_names av1_profiles[] = {
#if QSV_VERSION_ATLEAST(1, 34)
    { MFX_PROFILE_AV1_MAIN,                     "av1 main"                  },
    { MFX_PROFILE_AV1_HIGH,                     "av1 high"                  },
    { MFX_PROFILE_AV1_PRO,                      "av1 professional"          },
#endif
};

typedef struct QSVPacket {
    AVPacket        pkt;
    mfxSyncPoint   *sync;
    mfxBitstream   *bs;
} QSVPacket;

static const char *print_profile(enum AVCodecID codec_id, mfxU16 profile)
{
    const struct profile_names *profiles;
    int i, num_profiles;

    switch (codec_id) {
    case AV_CODEC_ID_H264:
        profiles = avc_profiles;
        num_profiles = FF_ARRAY_ELEMS(avc_profiles);
        break;

    case AV_CODEC_ID_MPEG2VIDEO:
        profiles = mpeg2_profiles;
        num_profiles = FF_ARRAY_ELEMS(mpeg2_profiles);
        break;

    case AV_CODEC_ID_HEVC:
        profiles = hevc_profiles;
        num_profiles = FF_ARRAY_ELEMS(hevc_profiles);
        break;

    case AV_CODEC_ID_VP9:
        profiles = vp9_profiles;
        num_profiles = FF_ARRAY_ELEMS(vp9_profiles);
        break;

    case AV_CODEC_ID_AV1:
        profiles = av1_profiles;
        num_profiles = FF_ARRAY_ELEMS(av1_profiles);
        break;

    default:
        return "unknown";
    }

    for (i = 0; i < num_profiles; i++)
        if (profile == profiles[i].profile)
            return profiles[i].name;

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
    { MFX_RATECONTROL_LA,      "LA" },
    { MFX_RATECONTROL_ICQ,     "ICQ" },
    { MFX_RATECONTROL_LA_ICQ,  "LA_ICQ" },
#if QSV_HAVE_VCM
    { MFX_RATECONTROL_VCM,     "VCM" },
#endif
#if !QSV_ONEVPL
    { MFX_RATECONTROL_LA_EXT,  "LA_EXT" },
#endif
    { MFX_RATECONTROL_LA_HRD,  "LA_HRD" },
    { MFX_RATECONTROL_QVBR,    "QVBR" },
};

#define UPDATE_PARAM(a, b)  \
do {                        \
    if ((a) != (b)) {       \
        a = b;              \
        updated = 1;        \
    }                       \
} while (0)                 \

#define MFX_IMPL_VIA_MASK(impl) (0x0f00 & (impl))

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

    // co is always at index 1
    mfxExtCodingOption   *co = (mfxExtCodingOption*)coding_opts[1];
    mfxExtCodingOption2 *co2 = NULL;
    mfxExtCodingOption3 *co3 = NULL;
    mfxExtHEVCTiles *exthevctiles = NULL;
#if QSV_HAVE_HE
    mfxExtHyperModeParam *exthypermodeparam = NULL;
#endif

    const char *tmp_str = NULL;

    if (q->co2_idx > 0)
        co2 = (mfxExtCodingOption2*)coding_opts[q->co2_idx];

    if (q->co3_idx > 0)
        co3 = (mfxExtCodingOption3*)coding_opts[q->co3_idx];

    if (q->exthevctiles_idx > 0)
        exthevctiles = (mfxExtHEVCTiles *)coding_opts[q->exthevctiles_idx];

#if QSV_HAVE_HE
    if (q->exthypermodeparam_idx > 0)
        exthypermodeparam = (mfxExtHyperModeParam *)coding_opts[q->exthypermodeparam_idx];
#endif

    av_log(avctx, AV_LOG_VERBOSE, "profile: %s; level: %"PRIu16"\n",
           print_profile(avctx->codec_id, info->CodecProfile), info->CodecLevel);

    av_log(avctx, AV_LOG_VERBOSE,
           "GopPicSize: %"PRIu16"; GopRefDist: %"PRIu16"; GopOptFlag:%s%s; IdrInterval: %"PRIu16"\n",
           info->GopPicSize, info->GopRefDist,
           info->GopOptFlag & MFX_GOP_CLOSED ? " closed" : "",
           info->GopOptFlag & MFX_GOP_STRICT ? " strict" : "",
           info->IdrInterval);

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
    else if (info->RateControlMethod == MFX_RATECONTROL_LA
             || info->RateControlMethod == MFX_RATECONTROL_LA_HRD
             ) {
        av_log(avctx, AV_LOG_VERBOSE,
               "TargetKbps: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->TargetKbps, info->BRCParamMultiplier);
    } else if (info->RateControlMethod == MFX_RATECONTROL_ICQ ||
               info->RateControlMethod == MFX_RATECONTROL_LA_ICQ)
        av_log(avctx, AV_LOG_VERBOSE, "ICQQuality: %"PRIu16"\n", info->ICQQuality);
    av_log(avctx, AV_LOG_VERBOSE, "NumSlice: %"PRIu16"; NumRefFrame: %"PRIu16"\n",
           info->NumSlice, info->NumRefFrame);
    av_log(avctx, AV_LOG_VERBOSE, "RateDistortionOpt: %s\n",
           print_threestate(co->RateDistortionOpt));

    av_log(avctx, AV_LOG_VERBOSE, "RecoveryPointSEI: %s\n", print_threestate(co->RecoveryPointSEI));
    av_log(avctx, AV_LOG_VERBOSE, "VDENC: %s\n", print_threestate(info->LowPower));

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        av_log(avctx, AV_LOG_VERBOSE, "Entropy coding: %s; MaxDecFrameBuffering: %"PRIu16"\n",
               co->CAVLC == MFX_CODINGOPTION_ON ? "CAVLC" : "CABAC", co->MaxDecFrameBuffering);
        av_log(avctx, AV_LOG_VERBOSE,
               "NalHrdConformance: %s; SingleSeiNalUnit: %s; VuiVclHrdParameters: %s VuiNalHrdParameters: %s\n",
               print_threestate(co->NalHrdConformance), print_threestate(co->SingleSeiNalUnit),
               print_threestate(co->VuiVclHrdParameters), print_threestate(co->VuiNalHrdParameters));
    } else if ((avctx->codec_id == AV_CODEC_ID_HEVC) && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 28)) {
        av_log(avctx, AV_LOG_VERBOSE,
               "NalHrdConformance: %s; VuiNalHrdParameters: %s\n",
               print_threestate(co->NalHrdConformance), print_threestate(co->VuiNalHrdParameters));
    }

    av_log(avctx, AV_LOG_VERBOSE, "FrameRateExtD: %"PRIu32"; FrameRateExtN: %"PRIu32" \n",
           info->FrameInfo.FrameRateExtD, info->FrameInfo.FrameRateExtN);

    if (co2) {
        if ((info->RateControlMethod == MFX_RATECONTROL_VBR && q->extbrc && q->look_ahead_depth > 0) ||
            (info->RateControlMethod == MFX_RATECONTROL_LA) ||
            (info->RateControlMethod == MFX_RATECONTROL_LA_HRD) ||
            (info->RateControlMethod == MFX_RATECONTROL_LA_ICQ))
            av_log(avctx, AV_LOG_VERBOSE, "LookAheadDepth: %"PRIu16"\n", co2->LookAheadDepth);

        av_log(avctx, AV_LOG_VERBOSE, "IntRefType: %"PRIu16"; IntRefCycleSize: %"PRIu16"; IntRefQPDelta: %"PRId16"\n",
               co2->IntRefType, co2->IntRefCycleSize, co2->IntRefQPDelta);

        av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSize: %d; MaxSliceSize: %d\n",
               co2->MaxFrameSize, co2->MaxSliceSize);

        av_log(avctx, AV_LOG_VERBOSE,
               "BitrateLimit: %s; MBBRC: %s; ExtBRC: %s\n",
               print_threestate(co2->BitrateLimit), print_threestate(co2->MBBRC),
               print_threestate(co2->ExtBRC));

        if (co2->Trellis & MFX_TRELLIS_OFF) {
            av_log(avctx, AV_LOG_VERBOSE, "Trellis: off\n");
        } else if (!co2->Trellis) {
            av_log(avctx, AV_LOG_VERBOSE, "Trellis: auto\n");
        } else {
            char trellis_type[4];
            int i = 0;
            if (co2->Trellis & MFX_TRELLIS_I) trellis_type[i++] = 'I';
            if (co2->Trellis & MFX_TRELLIS_P) trellis_type[i++] = 'P';
            if (co2->Trellis & MFX_TRELLIS_B) trellis_type[i++] = 'B';
            trellis_type[i] = 0;
            av_log(avctx, AV_LOG_VERBOSE, "Trellis: %s\n", trellis_type);
        }

        switch (co2->LookAheadDS) {
        case MFX_LOOKAHEAD_DS_OFF: tmp_str = "off";     break;
        case MFX_LOOKAHEAD_DS_2x:  tmp_str = "2x";      break;
        case MFX_LOOKAHEAD_DS_4x:  tmp_str = "4x";      break;
        default:                   tmp_str = "unknown"; break;
        }
        av_log(avctx, AV_LOG_VERBOSE,
               "RepeatPPS: %s; NumMbPerSlice: %"PRIu16"; LookAheadDS: %s\n",
               print_threestate(co2->RepeatPPS), co2->NumMbPerSlice, tmp_str);

        switch (co2->BRefType) {
        case MFX_B_REF_OFF:     tmp_str = "off";       break;
        case MFX_B_REF_PYRAMID: tmp_str = "pyramid";   break;
        default:                tmp_str = "auto";      break;
        }
        av_log(avctx, AV_LOG_VERBOSE,
               "AdaptiveI: %s; AdaptiveB: %s; BRefType:%s\n",
               print_threestate(co2->AdaptiveI), print_threestate(co2->AdaptiveB), tmp_str);

        av_log(avctx, AV_LOG_VERBOSE,
               "MinQPI: %"PRIu8"; MaxQPI: %"PRIu8"; MinQPP: %"PRIu8"; MaxQPP: %"PRIu8"; MinQPB: %"PRIu8"; MaxQPB: %"PRIu8"\n",
               co2->MinQPI, co2->MaxQPI, co2->MinQPP, co2->MaxQPP, co2->MinQPB, co2->MaxQPB);
        av_log(avctx, AV_LOG_VERBOSE, "DisableDeblockingIdc: %"PRIu32" \n", co2->DisableDeblockingIdc);

        switch (co2->SkipFrame) {
        case MFX_SKIPFRAME_NO_SKIP:
            av_log(avctx, AV_LOG_VERBOSE, "SkipFrame: no_skip\n");
            break;
        case MFX_SKIPFRAME_INSERT_DUMMY:
            av_log(avctx, AV_LOG_VERBOSE, "SkipFrame: insert_dummy\n");
            break;
        case MFX_SKIPFRAME_INSERT_NOTHING:
            av_log(avctx, AV_LOG_VERBOSE, "SkipFrame: insert_nothing\n");
            break;
        case MFX_SKIPFRAME_BRC_ONLY:
            av_log(avctx, AV_LOG_VERBOSE, "SkipFrame: brc_only\n");
            break;
        default: break;
        }
    }

    if (co3) {
        if (info->RateControlMethod == MFX_RATECONTROL_QVBR)
            av_log(avctx, AV_LOG_VERBOSE, "QVBRQuality: %"PRIu16"\n", co3->QVBRQuality);

        switch (co3->PRefType) {
        case MFX_P_REF_DEFAULT: av_log(avctx, AV_LOG_VERBOSE, "PRefType: default\n");   break;
        case MFX_P_REF_SIMPLE:  av_log(avctx, AV_LOG_VERBOSE, "PRefType: simple\n");    break;
        case MFX_P_REF_PYRAMID: av_log(avctx, AV_LOG_VERBOSE, "PRefType: pyramid\n");   break;
        default:                av_log(avctx, AV_LOG_VERBOSE, "PRefType: unknown\n");   break;
        }

        if (avctx->codec_id == AV_CODEC_ID_HEVC)
            av_log(avctx, AV_LOG_VERBOSE,"GPB: %s\n", print_threestate(co3->GPB));

        av_log(avctx, AV_LOG_VERBOSE, "TransformSkip: %s \n", print_threestate(co3->TransformSkip));
        av_log(avctx, AV_LOG_VERBOSE, "IntRefCycleDist: %"PRId16"\n", co3->IntRefCycleDist);
        av_log(avctx, AV_LOG_VERBOSE, "LowDelayBRC: %s\n", print_threestate(co3->LowDelayBRC));
        av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSizeI: %d; ", co3->MaxFrameSizeI);
        av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSizeP: %d\n", co3->MaxFrameSizeP);
        av_log(avctx, AV_LOG_VERBOSE, "ScenarioInfo: %"PRId16"\n", co3->ScenarioInfo);
    }

    if (exthevctiles) {
        av_log(avctx, AV_LOG_VERBOSE, "NumTileColumns: %"PRIu16"; NumTileRows: %"PRIu16"\n",
               exthevctiles->NumTileColumns, exthevctiles->NumTileRows);
    }

#if QSV_HAVE_HE
    if (exthypermodeparam) {
        av_log(avctx, AV_LOG_VERBOSE, "HyperEncode: ");

        if (exthypermodeparam->Mode == MFX_HYPERMODE_OFF)
            av_log(avctx, AV_LOG_VERBOSE, "OFF");
        if (exthypermodeparam->Mode == MFX_HYPERMODE_ON)
            av_log(avctx, AV_LOG_VERBOSE, "ON");
        if (exthypermodeparam->Mode == MFX_HYPERMODE_ADAPTIVE)
            av_log(avctx, AV_LOG_VERBOSE, "Adaptive");

        av_log(avctx, AV_LOG_VERBOSE, "\n");
    }
#endif
}

static void dump_video_vp9_param(AVCodecContext *avctx, QSVEncContext *q,
                                 mfxExtBuffer **coding_opts)
{
    mfxInfoMFX *info = &q->param.mfx;
    mfxExtVP9Param *vp9_param = NULL;
    mfxExtCodingOption2 *co2 = NULL;

    if (q->vp9_idx >= 0)
        vp9_param = (mfxExtVP9Param *)coding_opts[q->vp9_idx];

    if (q->co2_idx >= 0)
        co2 = (mfxExtCodingOption2*)coding_opts[q->co2_idx];

    av_log(avctx, AV_LOG_VERBOSE, "profile: %s \n",
           print_profile(avctx->codec_id, info->CodecProfile));

    av_log(avctx, AV_LOG_VERBOSE,
           "GopPicSize: %"PRIu16"; GopRefDist: %"PRIu16"; GopOptFlag:%s%s; IdrInterval: %"PRIu16"\n",
           info->GopPicSize, info->GopRefDist,
           info->GopOptFlag & MFX_GOP_CLOSED ? " closed" : "",
           info->GopOptFlag & MFX_GOP_STRICT ? " strict" : "",
           info->IdrInterval);

    av_log(avctx, AV_LOG_VERBOSE, "TargetUsage: %"PRIu16"; RateControlMethod: %s\n",
           info->TargetUsage, print_ratecontrol(info->RateControlMethod));

    if (info->RateControlMethod == MFX_RATECONTROL_CBR ||
        info->RateControlMethod == MFX_RATECONTROL_VBR) {
        av_log(avctx, AV_LOG_VERBOSE,
               "BufferSizeInKB: %"PRIu16"; InitialDelayInKB: %"PRIu16"; TargetKbps: %"PRIu16"; MaxKbps: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->BufferSizeInKB, info->InitialDelayInKB, info->TargetKbps, info->MaxKbps, info->BRCParamMultiplier);
    } else if (info->RateControlMethod == MFX_RATECONTROL_CQP) {
        av_log(avctx, AV_LOG_VERBOSE, "QPI: %"PRIu16"; QPP: %"PRIu16"; QPB: %"PRIu16"\n",
               info->QPI, info->QPP, info->QPB);
    }
    else if (info->RateControlMethod == MFX_RATECONTROL_ICQ) {
        av_log(avctx, AV_LOG_VERBOSE, "ICQQuality: %"PRIu16"\n", info->ICQQuality);
    }
    else {
        av_log(avctx, AV_LOG_VERBOSE, "Unsupported ratecontrol method: %d \n", info->RateControlMethod);
    }

    av_log(avctx, AV_LOG_VERBOSE, "NumRefFrame: %"PRIu16"\n", info->NumRefFrame);
    av_log(avctx, AV_LOG_VERBOSE, "FrameRateExtD: %"PRIu32"; FrameRateExtN: %"PRIu32" \n",
           info->FrameInfo.FrameRateExtD, info->FrameInfo.FrameRateExtN);

    if (co2) {
        av_log(avctx, AV_LOG_VERBOSE,
               "IntRefType: %"PRIu16"; IntRefCycleSize: %"PRIu16"; IntRefQPDelta: %"PRId16"\n",
               co2->IntRefType, co2->IntRefCycleSize, co2->IntRefQPDelta);

        av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSize: %d\n", co2->MaxFrameSize);

        av_log(avctx, AV_LOG_VERBOSE,
               "BitrateLimit: %s; MBBRC: %s; ExtBRC: %s\n",
               print_threestate(co2->BitrateLimit), print_threestate(co2->MBBRC),
               print_threestate(co2->ExtBRC));

        av_log(avctx, AV_LOG_VERBOSE, "VDENC: %s\n", print_threestate(info->LowPower));

        av_log(avctx, AV_LOG_VERBOSE,
               "MinQPI: %"PRIu8"; MaxQPI: %"PRIu8"; MinQPP: %"PRIu8"; MaxQPP: %"PRIu8"; MinQPB: %"PRIu8"; MaxQPB: %"PRIu8"\n",
               co2->MinQPI, co2->MaxQPI, co2->MinQPP, co2->MaxQPP, co2->MinQPB, co2->MaxQPB);
    }

    if (vp9_param) {
        av_log(avctx, AV_LOG_VERBOSE, "WriteIVFHeaders: %s \n",
               print_threestate(vp9_param->WriteIVFHeaders));
    }
}

static void dump_video_mjpeg_param(AVCodecContext *avctx, QSVEncContext *q)
{
    mfxInfoMFX *info = &q->param.mfx;

    av_log(avctx, AV_LOG_VERBOSE, "Interleaved: %"PRIu16" \n", info->Interleaved);
    av_log(avctx, AV_LOG_VERBOSE, "Quality: %"PRIu16" \n", info->Quality);
    av_log(avctx, AV_LOG_VERBOSE, "RestartInterval: %"PRIu16" \n", info->RestartInterval);

    av_log(avctx, AV_LOG_VERBOSE, "FrameRateExtD: %"PRIu32"; FrameRateExtN: %"PRIu32" \n",
           info->FrameInfo.FrameRateExtD, info->FrameInfo.FrameRateExtN);
}

#if QSV_HAVE_EXT_AV1_PARAM
static void dump_video_av1_param(AVCodecContext *avctx, QSVEncContext *q,
                                 mfxExtBuffer **coding_opts)
{
    mfxInfoMFX *info = &q->param.mfx;
    mfxExtAV1TileParam *av1_tile_param = (mfxExtAV1TileParam *)coding_opts[0];
    mfxExtAV1BitstreamParam *av1_bs_param = (mfxExtAV1BitstreamParam *)coding_opts[1];
    mfxExtCodingOption2 *co2 = (mfxExtCodingOption2*)coding_opts[2];
    mfxExtCodingOption3 *co3 = (mfxExtCodingOption3*)coding_opts[3];

    av_log(avctx, AV_LOG_VERBOSE, "profile: %s; level: %"PRIu16"\n",
           print_profile(avctx->codec_id, info->CodecProfile), info->CodecLevel);

    av_log(avctx, AV_LOG_VERBOSE,
           "GopPicSize: %"PRIu16"; GopRefDist: %"PRIu16"; GopOptFlag:%s%s; IdrInterval: %"PRIu16"\n",
           info->GopPicSize, info->GopRefDist,
           info->GopOptFlag & MFX_GOP_CLOSED ? " closed" : "",
           info->GopOptFlag & MFX_GOP_STRICT ? " strict" : "",
           info->IdrInterval);

    av_log(avctx, AV_LOG_VERBOSE, "TargetUsage: %"PRIu16"; RateControlMethod: %s\n",
           info->TargetUsage, print_ratecontrol(info->RateControlMethod));

    if (info->RateControlMethod == MFX_RATECONTROL_CBR ||
        info->RateControlMethod == MFX_RATECONTROL_VBR)
        av_log(avctx, AV_LOG_VERBOSE,
               "BufferSizeInKB: %"PRIu16"; InitialDelayInKB: %"PRIu16"; TargetKbps: %"PRIu16"; MaxKbps: %"PRIu16"; BRCParamMultiplier: %"PRIu16"\n",
               info->BufferSizeInKB, info->InitialDelayInKB, info->TargetKbps, info->MaxKbps, info->BRCParamMultiplier);
    else if (info->RateControlMethod == MFX_RATECONTROL_CQP)
        av_log(avctx, AV_LOG_VERBOSE, "QPI: %"PRIu16"; QPP: %"PRIu16"; QPB: %"PRIu16"\n",
               info->QPI, info->QPP, info->QPB);
    else if (info->RateControlMethod == MFX_RATECONTROL_ICQ)
        av_log(avctx, AV_LOG_VERBOSE, "ICQQuality: %"PRIu16"\n", info->ICQQuality);
    else
        av_log(avctx, AV_LOG_VERBOSE, "Unsupported ratecontrol method: %d \n", info->RateControlMethod);

    av_log(avctx, AV_LOG_VERBOSE, "NumRefFrame: %"PRIu16"\n", info->NumRefFrame);

    av_log(avctx, AV_LOG_VERBOSE,
           "IntRefType: %"PRIu16"; IntRefCycleSize: %"PRIu16
           "; IntRefQPDelta: %"PRId16"; IntRefCycleDist: %"PRId16"\n",
           co2->IntRefType, co2->IntRefCycleSize,
           co2->IntRefQPDelta, co3->IntRefCycleDist);

    av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSize: %d;\n", co2->MaxFrameSize);

    av_log(avctx, AV_LOG_VERBOSE,
           "BitrateLimit: %s; MBBRC: %s; ExtBRC: %s\n",
           print_threestate(co2->BitrateLimit), print_threestate(co2->MBBRC),
           print_threestate(co2->ExtBRC));

    av_log(avctx, AV_LOG_VERBOSE, "VDENC: %s\n", print_threestate(info->LowPower));

    switch (co2->BRefType) {
    case MFX_B_REF_OFF:     av_log(avctx, AV_LOG_VERBOSE, "BRefType: off\n");       break;
    case MFX_B_REF_PYRAMID: av_log(avctx, AV_LOG_VERBOSE, "BRefType: pyramid\n");   break;
    default:                av_log(avctx, AV_LOG_VERBOSE, "BRefType: auto\n");      break;
    }

    switch (co3->PRefType) {
    case MFX_P_REF_DEFAULT: av_log(avctx, AV_LOG_VERBOSE, "PRefType: default\n");   break;
    case MFX_P_REF_SIMPLE:  av_log(avctx, AV_LOG_VERBOSE, "PRefType: simple\n");    break;
    case MFX_P_REF_PYRAMID: av_log(avctx, AV_LOG_VERBOSE, "PRefType: pyramid\n");   break;
    default:                av_log(avctx, AV_LOG_VERBOSE, "PRefType: unknown\n");   break;
    }

    av_log(avctx, AV_LOG_VERBOSE,
           "MinQPI: %"PRIu8"; MaxQPI: %"PRIu8"; MinQPP: %"PRIu8"; MaxQPP: %"PRIu8"; MinQPB: %"PRIu8"; MaxQPB: %"PRIu8"\n",
           co2->MinQPI, co2->MaxQPI, co2->MinQPP, co2->MaxQPP, co2->MinQPB, co2->MaxQPB);

    av_log(avctx, AV_LOG_VERBOSE, "FrameRateExtD: %"PRIu32"; FrameRateExtN: %"PRIu32" \n",
           info->FrameInfo.FrameRateExtD, info->FrameInfo.FrameRateExtN);

    av_log(avctx, AV_LOG_VERBOSE,
           "NumTileRows: %"PRIu16"; NumTileColumns: %"PRIu16"; NumTileGroups: %"PRIu16"\n",
           av1_tile_param->NumTileRows, av1_tile_param->NumTileColumns, av1_tile_param->NumTileGroups);

    av_log(avctx, AV_LOG_VERBOSE, "WriteIVFHeaders: %s \n",
           print_threestate(av1_bs_param->WriteIVFHeaders));
    av_log(avctx, AV_LOG_VERBOSE, "LowDelayBRC: %s\n", print_threestate(co3->LowDelayBRC));
    av_log(avctx, AV_LOG_VERBOSE, "MaxFrameSize: %d;\n", co2->MaxFrameSize);
}
#endif

static int select_rc_mode(AVCodecContext *avctx, QSVEncContext *q)
{
    const char *rc_desc;
    mfxU16      rc_mode;

    int want_la     = q->look_ahead;
    int want_qscale = !!(avctx->flags & AV_CODEC_FLAG_QSCALE);
    int want_vcm    = q->vcm;

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
    else if (want_la) {
        rc_mode = MFX_RATECONTROL_LA;
        rc_desc = "VBR with lookahead (LA)";

        if (avctx->global_quality > 0) {
            rc_mode = MFX_RATECONTROL_LA_ICQ;
            rc_desc = "intelligent constant quality with lookahead (LA_ICQ)";
        }
    }
    else if (avctx->global_quality > 0 && !avctx->rc_max_rate) {
        rc_mode = MFX_RATECONTROL_ICQ;
        rc_desc = "intelligent constant quality (ICQ)";
    }
    else if (avctx->rc_max_rate == avctx->bit_rate) {
        rc_mode = MFX_RATECONTROL_CBR;
        rc_desc = "constant bitrate (CBR)";
    }
#if QSV_HAVE_AVBR
    else if (!avctx->rc_max_rate &&
             (avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_HEVC) &&
             q->avbr_accuracy &&
             q->avbr_convergence) {
        rc_mode = MFX_RATECONTROL_AVBR;
        rc_desc = "average variable bitrate (AVBR)";
    }
#endif
    else if (avctx->global_quality > 0) {
        rc_mode = MFX_RATECONTROL_QVBR;
        rc_desc = "constant quality with VBR algorithm (QVBR)";
    }
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

static int is_strict_gop(QSVEncContext *q) {
    if (q->adaptive_b == 0 && q->adaptive_i == 0)
        return 1;
    return 0;
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

    ret = ff_qsv_map_pixfmt(sw_format, &q->param.mfx.FrameInfo.FourCC, &q->param.mfx.FrameInfo.Shift);
    if (ret < 0)
        return AVERROR_BUG;

    q->param.mfx.FrameInfo.CropX          = 0;
    q->param.mfx.FrameInfo.CropY          = 0;
    q->param.mfx.FrameInfo.CropW          = avctx->width;
    q->param.mfx.FrameInfo.CropH          = avctx->height;
    q->param.mfx.FrameInfo.AspectRatioW   = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH   = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420 +
                                            !desc->log2_chroma_w + !desc->log2_chroma_h;
    q->param.mfx.FrameInfo.BitDepthLuma   = desc->comp[0].depth;
    q->param.mfx.FrameInfo.BitDepthChroma = desc->comp[0].depth;

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

    q->width_align = 16;
    q->height_align = 16;

    q->param.mfx.FrameInfo.Width = FFALIGN(avctx->width, q->width_align);
    q->param.mfx.FrameInfo.Height = FFALIGN(avctx->height, q->height_align);

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

    if (avctx->level > 0) {
        q->param.mfx.CodecLevel = avctx->level;
        if (avctx->codec_id == AV_CODEC_ID_HEVC && avctx->level >= MFX_LEVEL_HEVC_4)
            q->param.mfx.CodecLevel |= q->tier;
    }

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

    if (q->low_power == 1) {
        q->param.mfx.LowPower = MFX_CODINGOPTION_ON;
    } else if (q->low_power == -1)
        q->param.mfx.LowPower = MFX_CODINGOPTION_UNKNOWN;
    else
        q->param.mfx.LowPower = MFX_CODINGOPTION_OFF;

    q->param.mfx.CodecProfile       = q->profile;
    q->param.mfx.TargetUsage        = avctx->compression_level;
    q->param.mfx.GopPicSize         = FFMAX(0, avctx->gop_size);
    q->old_gop_size                 = avctx->gop_size;
    q->param.mfx.GopRefDist         = FFMAX(-1, avctx->max_b_frames) + 1;
    q->param.mfx.GopOptFlag         = avctx->flags & AV_CODEC_FLAG_CLOSED_GOP ?
                                      MFX_GOP_CLOSED : is_strict_gop(q) ?
                                      MFX_GOP_STRICT : 0;
    q->param.mfx.IdrInterval        = q->idr_interval;
    q->param.mfx.NumSlice           = avctx->slices;
    q->param.mfx.NumRefFrame        = FFMAX(0, avctx->refs);
    q->param.mfx.EncodedOrder       = 0;
    q->param.mfx.BufferSizeInKB     = 0;

    desc = av_pix_fmt_desc_get(sw_format);
    if (!desc)
        return AVERROR_BUG;

    ret = ff_qsv_map_pixfmt(sw_format, &q->param.mfx.FrameInfo.FourCC, &q->param.mfx.FrameInfo.Shift);
    if (ret < 0)
        return AVERROR_BUG;

    q->param.mfx.FrameInfo.CropX          = 0;
    q->param.mfx.FrameInfo.CropY          = 0;
    q->param.mfx.FrameInfo.CropW          = avctx->width;
    q->param.mfx.FrameInfo.CropH          = avctx->height;
    q->param.mfx.FrameInfo.AspectRatioW   = avctx->sample_aspect_ratio.num;
    q->param.mfx.FrameInfo.AspectRatioH   = avctx->sample_aspect_ratio.den;
    q->param.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420 +
                                            !desc->log2_chroma_w + !desc->log2_chroma_h;
    q->param.mfx.FrameInfo.BitDepthLuma   = desc->comp[0].depth;
    q->param.mfx.FrameInfo.BitDepthChroma = desc->comp[0].depth;

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
    q->old_framerate = avctx->framerate;

    ret = select_rc_mode(avctx, q);
    if (ret < 0)
        return ret;

    //libmfx BRC parameters are 16 bits thus maybe overflow, then BRCParamMultiplier is needed
    buffer_size_in_kilobytes   = avctx->rc_buffer_size / 8000;
    initial_delay_in_kilobytes = avctx->rc_initial_buffer_occupancy / 8000;
    target_bitrate_kbps        = avctx->bit_rate / 1000;
    max_bitrate_kbps           = avctx->rc_max_rate / 1000;
    brc_param_multiplier       = (FFMAX(FFMAX3(target_bitrate_kbps, max_bitrate_kbps, buffer_size_in_kilobytes),
                                  initial_delay_in_kilobytes) + 0x10000) / 0x10000;
    q->old_rc_buffer_size = avctx->rc_buffer_size;
    q->old_rc_initial_buffer_occupancy = avctx->rc_initial_buffer_occupancy;
    q->old_bit_rate = avctx->bit_rate;
    q->old_rc_max_rate = avctx->rc_max_rate;

    switch (q->param.mfx.RateControlMethod) {
    case MFX_RATECONTROL_CBR:
    case MFX_RATECONTROL_VBR:
        if (q->extbrc) {
            q->extco2.LookAheadDepth = q->look_ahead_depth;
        }
#if QSV_HAVE_VCM
    case MFX_RATECONTROL_VCM:
#endif
    case MFX_RATECONTROL_QVBR:
        q->param.mfx.BufferSizeInKB   = buffer_size_in_kilobytes / brc_param_multiplier;
        q->param.mfx.InitialDelayInKB = initial_delay_in_kilobytes / brc_param_multiplier;
        q->param.mfx.TargetKbps       = target_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.MaxKbps          = max_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
        if (q->param.mfx.RateControlMethod == MFX_RATECONTROL_QVBR)
            q->extco3.QVBRQuality = av_clip(avctx->global_quality, 0, 51);
        break;
    case MFX_RATECONTROL_CQP:
        quant = avctx->global_quality / FF_QP2LAMBDA;
        if (avctx->codec_id == AV_CODEC_ID_AV1) {
            q->param.mfx.QPI = av_clip_uintp2(quant * fabs(avctx->i_quant_factor) + avctx->i_quant_offset, 8);
            q->param.mfx.QPP = av_clip_uintp2(quant, 8);
            q->param.mfx.QPB = av_clip_uintp2(quant * fabs(avctx->b_quant_factor) + avctx->b_quant_offset, 8);
        } else {
            q->param.mfx.QPI = av_clip(quant * fabs(avctx->i_quant_factor) + avctx->i_quant_offset, 0, 51);
            q->param.mfx.QPP = av_clip(quant, 0, 51);
            q->param.mfx.QPB = av_clip(quant * fabs(avctx->b_quant_factor) + avctx->b_quant_offset, 0, 51);
        }
        q->old_global_quality = avctx->global_quality;
        q->old_i_quant_factor = avctx->i_quant_factor;
        q->old_i_quant_offset = avctx->i_quant_offset;
        q->old_b_quant_factor = avctx->b_quant_factor;
        q->old_b_quant_offset = avctx->b_quant_offset;

        break;
#if QSV_HAVE_AVBR
    case MFX_RATECONTROL_AVBR:
        q->param.mfx.TargetKbps  = target_bitrate_kbps / brc_param_multiplier;
        q->param.mfx.Convergence = q->avbr_convergence;
        q->param.mfx.Accuracy    = q->avbr_accuracy;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
        break;
#endif
    case MFX_RATECONTROL_LA:
        q->param.mfx.TargetKbps  = target_bitrate_kbps / brc_param_multiplier;
        q->extco2.LookAheadDepth = q->look_ahead_depth;
        q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
        break;
    case MFX_RATECONTROL_LA_ICQ:
        q->extco2.LookAheadDepth = q->look_ahead_depth;
    case MFX_RATECONTROL_ICQ:
        q->param.mfx.ICQQuality  = av_clip(avctx->global_quality, 1, 51);
        break;
    }

    // The HEVC encoder plugin currently fails with some old libmfx version if coding options
    // are provided. Can't find the extract libmfx version which fixed it, just enable it from
    // V1.28 in order to keep compatibility security.
    if (((avctx->codec_id != AV_CODEC_ID_HEVC) || QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 28))
        && (avctx->codec_id != AV_CODEC_ID_VP9)) {
        q->extco.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION;
        q->extco.Header.BufferSz      = sizeof(q->extco);

        q->extco.PicTimingSEI         = q->pic_timing_sei ?
                                        MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN;
        q->old_pic_timing_sei = q->pic_timing_sei;

        if (q->rdo >= 0)
            q->extco.RateDistortionOpt = q->rdo > 0 ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

        if (avctx->codec_id == AV_CODEC_ID_H264) {
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
        } else if (avctx->codec_id == AV_CODEC_ID_HEVC) {
            if (avctx->strict_std_compliance != FF_COMPLIANCE_NORMAL)
                q->extco.NalHrdConformance = avctx->strict_std_compliance > FF_COMPLIANCE_NORMAL ?
                                             MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

            if (q->recovery_point_sei >= 0)
                q->extco.RecoveryPointSEI = q->recovery_point_sei ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

            q->extco.AUDelimiter          = q->aud ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
        }

        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco;

        if (avctx->codec_id == AV_CODEC_ID_H264) {
            if (q->bitrate_limit >= 0)
                q->extco2.BitrateLimit = q->bitrate_limit ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

            if (avctx->trellis >= 0)
                q->extco2.Trellis = (avctx->trellis == 0) ? MFX_TRELLIS_OFF : (MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B);
            else
                q->extco2.Trellis = MFX_TRELLIS_UNKNOWN;

            q->extco2.LookAheadDS = q->look_ahead_downsampling;
            q->extco2.RepeatPPS   = q->repeat_pps ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
        }

        if (avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_HEVC) {
            if (q->extbrc >= 0)
                q->extco2.ExtBRC = q->extbrc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->max_frame_size >= 0)
                q->extco2.MaxFrameSize = q->max_frame_size;
            q->old_max_frame_size = q->max_frame_size;
            if (q->int_ref_type >= 0)
                q->extco2.IntRefType = q->int_ref_type;
            q->old_int_ref_type = q->int_ref_type;
            if (q->int_ref_cycle_size >= 0)
                q->extco2.IntRefCycleSize = q->int_ref_cycle_size;
            q->old_int_ref_cycle_size = q->int_ref_cycle_size;
            if (q->int_ref_qp_delta != INT16_MIN)
                q->extco2.IntRefQPDelta = q->int_ref_qp_delta;
            q->old_int_ref_qp_delta = q->int_ref_qp_delta;
            if (q->max_slice_size >= 0)
                q->extco2.MaxSliceSize = q->max_slice_size;
            q->extco2.DisableDeblockingIdc = q->dblk_idc;

            if (q->b_strategy >= 0)
                q->extco2.BRefType = q->b_strategy ? MFX_B_REF_PYRAMID : MFX_B_REF_OFF;
            if (q->adaptive_i >= 0)
                q->extco2.AdaptiveI = q->adaptive_i ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->adaptive_b >= 0)
                q->extco2.AdaptiveB = q->adaptive_b ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if ((avctx->qmin >= 0 && avctx->qmax >= 0 && avctx->qmin > avctx->qmax) ||
                (q->max_qp_i >= 0 && q->min_qp_i >= 0 && q->min_qp_i > q->max_qp_i) ||
                (q->max_qp_p >= 0 && q->min_qp_p >= 0 && q->min_qp_p > q->max_qp_p) ||
                (q->max_qp_b >= 0 && q->min_qp_b >= 0 && q->min_qp_b > q->max_qp_b)) {
                av_log(avctx, AV_LOG_ERROR,
                       "qmin and or qmax are set but invalid,"
                       " please make sure min <= max\n");
                return AVERROR(EINVAL);
            }
            if (avctx->qmin >= 0) {
                q->extco2.MinQPI = avctx->qmin > 51 ? 51 : avctx->qmin;
                q->extco2.MinQPP = q->extco2.MinQPB = q->extco2.MinQPI;
            }
            q->old_qmin = avctx->qmin;
            if (avctx->qmax >= 0) {
                q->extco2.MaxQPI = avctx->qmax > 51 ? 51 : avctx->qmax;
                q->extco2.MaxQPP = q->extco2.MaxQPB = q->extco2.MaxQPI;
            }
            q->old_qmax = avctx->qmax;
            if (q->min_qp_i >= 0)
                q->extco2.MinQPI = q->min_qp_i > 51 ? 51 : q->min_qp_i;
            q->old_min_qp_i = q->min_qp_i;
            if (q->max_qp_i >= 0)
                q->extco2.MaxQPI = q->max_qp_i > 51 ? 51 : q->max_qp_i;
            q->old_max_qp_i = q->max_qp_i;
            if (q->min_qp_p >= 0)
                q->extco2.MinQPP = q->min_qp_p > 51 ? 51 : q->min_qp_p;
            q->old_min_qp_p = q->min_qp_p;
            if (q->max_qp_p >= 0)
                q->extco2.MaxQPP = q->max_qp_p > 51 ? 51 : q->max_qp_p;
            q->old_max_qp_p = q->max_qp_p;
            if (q->min_qp_b >= 0)
                q->extco2.MinQPB = q->min_qp_b > 51 ? 51 : q->min_qp_b;
            q->old_min_qp_b = q->min_qp_b;
            if (q->max_qp_b >= 0)
                q->extco2.MaxQPB = q->max_qp_b > 51 ? 51 : q->max_qp_b;
            q->old_max_qp_b = q->max_qp_b;
            if (q->mbbrc >= 0)
                q->extco2.MBBRC = q->mbbrc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->skip_frame >= 0)
                q->extco2.SkipFrame = q->skip_frame;

            q->extco2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
            q->extco2.Header.BufferSz = sizeof(q->extco2);

            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco2;
        } else if (avctx->codec_id == AV_CODEC_ID_AV1) {
            if (q->extbrc >= 0)
                q->extco2.ExtBRC = q->extbrc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->b_strategy >= 0)
                q->extco2.BRefType = q->b_strategy ? MFX_B_REF_PYRAMID : MFX_B_REF_OFF;
            if (q->adaptive_i >= 0)
                q->extco2.AdaptiveI = q->adaptive_i ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->adaptive_b >= 0)
                q->extco2.AdaptiveB = q->adaptive_b ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            if (q->max_frame_size >= 0)
                q->extco2.MaxFrameSize = q->max_frame_size;

            q->extco2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
            q->extco2.Header.BufferSz = sizeof(q->extco2);

            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco2;
        }

        if (avctx->codec_id == AV_CODEC_ID_H264) {
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
        q->extco3.Header.BufferId      = MFX_EXTBUFF_CODING_OPTION3;
        q->extco3.Header.BufferSz      = sizeof(q->extco3);

        if (avctx->codec_id == AV_CODEC_ID_HEVC ||
            avctx->codec_id == AV_CODEC_ID_H264) {
            switch (q->p_strategy) {
            case 0:
                q->extco3.PRefType = MFX_P_REF_DEFAULT;
                break;
            case 1:
                q->extco3.PRefType = MFX_P_REF_SIMPLE;
                break;
            case 2:
                q->extco3.PRefType = MFX_P_REF_PYRAMID;
                break;
            default:
                q->extco3.PRefType = MFX_P_REF_DEFAULT;
                av_log(avctx, AV_LOG_WARNING,
                       "invalid p_strategy, set to default\n");
                break;
            }
            if (q->extco3.PRefType == MFX_P_REF_PYRAMID &&
                avctx->max_b_frames != 0) {
                av_log(avctx, AV_LOG_WARNING,
                       "Please set max_b_frames(-bf) to 0 to enable P-pyramid\n");
            }
            if (q->int_ref_cycle_dist >= 0)
                q->extco3.IntRefCycleDist = q->int_ref_cycle_dist;
            q->old_int_ref_cycle_dist = q->int_ref_cycle_dist;
            if (q->low_delay_brc >= 0)
                q->extco3.LowDelayBRC = q->low_delay_brc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
            q->old_low_delay_brc = q->low_delay_brc;
            if (q->max_frame_size_i >= 0)
                q->extco3.MaxFrameSizeI = q->max_frame_size_i;
            if (q->max_frame_size_p >= 0)
                q->extco3.MaxFrameSizeP = q->max_frame_size_p;

            q->extco3.ScenarioInfo = q->scenario;
        } else if (avctx->codec_id == AV_CODEC_ID_AV1) {
            if (q->low_delay_brc >= 0)
                q->extco3.LowDelayBRC = q->low_delay_brc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
        }

        if (avctx->codec_id == AV_CODEC_ID_HEVC) {
            if (q->transform_skip >= 0)
                q->extco3.TransformSkip = q->transform_skip ? MFX_CODINGOPTION_ON :
                                                              MFX_CODINGOPTION_OFF;
            else
                q->extco3.TransformSkip = MFX_CODINGOPTION_UNKNOWN;
            q->extco3.GPB              = q->gpb ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
        }
        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extco3;
    }

    if (avctx->codec_id == AV_CODEC_ID_VP9) {
        q->extvp9param.Header.BufferId = MFX_EXTBUFF_VP9_PARAM;
        q->extvp9param.Header.BufferSz = sizeof(q->extvp9param);
        q->extvp9param.WriteIVFHeaders = MFX_CODINGOPTION_OFF;
#if QSV_HAVE_EXT_VP9_TILES
        q->extvp9param.NumTileColumns  = q->tile_cols;
        q->extvp9param.NumTileRows     = q->tile_rows;
#endif
        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extvp9param;
    }

#if QSV_HAVE_EXT_AV1_PARAM
    if (avctx->codec_id == AV_CODEC_ID_AV1) {
        if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 2, 5)) {
            q->extav1tileparam.Header.BufferId = MFX_EXTBUFF_AV1_TILE_PARAM;
            q->extav1tileparam.Header.BufferSz = sizeof(q->extav1tileparam);
            q->extav1tileparam.NumTileColumns  = q->tile_cols;
            q->extav1tileparam.NumTileRows     = q->tile_rows;
            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extav1tileparam;

            q->extav1bsparam.Header.BufferId = MFX_EXTBUFF_AV1_BITSTREAM_PARAM;
            q->extav1bsparam.Header.BufferSz = sizeof(q->extav1bsparam);
            q->extav1bsparam.WriteIVFHeaders = MFX_CODINGOPTION_OFF;
            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extav1bsparam;
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "This version of runtime doesn't support AV1 encoding\n");
            return AVERROR_UNKNOWN;
        }
    }
#endif

    if (avctx->codec_id == AV_CODEC_ID_HEVC) {
        q->exthevctiles.Header.BufferId = MFX_EXTBUFF_HEVC_TILES;
        q->exthevctiles.Header.BufferSz = sizeof(q->exthevctiles);
        q->exthevctiles.NumTileColumns  = q->tile_cols;
        q->exthevctiles.NumTileRows     = q->tile_rows;
        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->exthevctiles;
    }

    q->extvsi.VideoFullRange = (avctx->color_range == AVCOL_RANGE_JPEG);
    q->extvsi.ColourDescriptionPresent = 0;

    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace != AVCOL_SPC_UNSPECIFIED) {
        q->extvsi.ColourDescriptionPresent = 1;
        q->extvsi.ColourPrimaries = avctx->color_primaries;
        q->extvsi.TransferCharacteristics = avctx->color_trc;
        if (avctx->colorspace == AVCOL_SPC_RGB)
            // RGB will be converted to YUV, so RGB colorspace is not supported
            q->extvsi.MatrixCoefficients = AVCOL_SPC_UNSPECIFIED;
        else
            q->extvsi.MatrixCoefficients = avctx->colorspace;

    }

    if ((avctx->codec_id != AV_CODEC_ID_VP9) && (q->extvsi.VideoFullRange || q->extvsi.ColourDescriptionPresent)) {
        q->extvsi.Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
        q->extvsi.Header.BufferSz = sizeof(q->extvsi);
        q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->extvsi;
    }

#if QSV_HAVE_HE
   if (q->dual_gfx) {
        if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 2, 4)) {
            mfxIMPL impl;
            MFXQueryIMPL(q->session, &impl);

            if (MFX_IMPL_VIA_MASK(impl) != MFX_IMPL_VIA_D3D11) {
                av_log(avctx, AV_LOG_ERROR, "Dual GFX mode requires D3D11VA \n");
                return AVERROR_UNKNOWN;
            }
            if (q->param.mfx.LowPower != MFX_CODINGOPTION_ON) {
                av_log(avctx, AV_LOG_ERROR, "Dual GFX mode supports only low-power encoding mode \n");
                return AVERROR_UNKNOWN;
            }
            if (q->param.mfx.CodecId != MFX_CODEC_AVC && q->param.mfx.CodecId != MFX_CODEC_HEVC) {
                av_log(avctx, AV_LOG_ERROR, "Not supported encoder for dual GFX mode. "
                                            "Supported: h264_qsv and hevc_qsv \n");
                return AVERROR_UNKNOWN;
            }
            if (q->param.mfx.RateControlMethod != MFX_RATECONTROL_VBR &&
                q->param.mfx.RateControlMethod != MFX_RATECONTROL_CQP &&
                q->param.mfx.RateControlMethod != MFX_RATECONTROL_ICQ) {
                av_log(avctx, AV_LOG_WARNING, "Not supported BRC for dual GFX mode. "
                                            "Supported: VBR, CQP and ICQ \n");
            }
            if ((q->param.mfx.CodecId == MFX_CODEC_AVC  && q->param.mfx.IdrInterval != 0) ||
                (q->param.mfx.CodecId == MFX_CODEC_HEVC && q->param.mfx.IdrInterval != 1)) {
                av_log(avctx, AV_LOG_WARNING, "Dual GFX mode requires closed GOP for AVC and strict GOP for HEVC, -idr_interval 0 \n");
            }
            if (q->param.mfx.GopPicSize < 30) {
                av_log(avctx, AV_LOG_WARNING, "For better performance in dual GFX mode GopPicSize must be >= 30 \n");
            }
            if (q->param.AsyncDepth < 30) {
                av_log(avctx, AV_LOG_WARNING, "For better performance in dual GFX mode AsyncDepth must be >= 30 \n");
            }

            q->exthypermodeparam.Header.BufferId = MFX_EXTBUFF_HYPER_MODE_PARAM;
            q->exthypermodeparam.Header.BufferSz = sizeof(q->exthypermodeparam);
            q->exthypermodeparam.Mode            = q->dual_gfx;
            q->extparam_internal[q->nb_extparam_internal++] = (mfxExtBuffer *)&q->exthypermodeparam;
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "This version of runtime doesn't support Hyper Encode\n");
            return AVERROR_UNKNOWN;
        }
    }
#endif

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

    dump_video_mjpeg_param(avctx, q);

    return 0;
}

static int qsv_retrieve_enc_vp9_params(AVCodecContext *avctx, QSVEncContext *q)
{
    int ret = 0;
    mfxExtVP9Param vp9_extend_buf = {
         .Header.BufferId = MFX_EXTBUFF_VP9_PARAM,
         .Header.BufferSz = sizeof(vp9_extend_buf),
    };

    mfxExtCodingOption2 co2 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION2,
        .Header.BufferSz = sizeof(co2),
    };

    mfxExtCodingOption3 co3 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION3,
        .Header.BufferSz = sizeof(co3),
    };

    mfxExtBuffer *ext_buffers[3];
    int ext_buf_num = 0;

    q->co2_idx = q->co3_idx = q->vp9_idx = -1;

    // It is possible the runtime doesn't support the given ext buffer
    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 6)) {
        q->co2_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&co2;
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 11)) {
        q->co3_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&co3;
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 26)) {
        q->vp9_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&vp9_extend_buf;
    }

    q->param.ExtParam    = ext_buffers;
    q->param.NumExtParam = ext_buf_num;

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error calling GetVideoParam");

    q->packet_size = q->param.mfx.BufferSizeInKB * q->param.mfx.BRCParamMultiplier * 1000;

    dump_video_vp9_param(avctx, q, ext_buffers);

    return 0;
}

static int qsv_retrieve_enc_av1_params(AVCodecContext *avctx, QSVEncContext *q)
{
#if QSV_HAVE_EXT_AV1_PARAM
    int ret = 0;
    mfxExtAV1TileParam av1_extend_tile_buf = {
         .Header.BufferId = MFX_EXTBUFF_AV1_TILE_PARAM,
         .Header.BufferSz = sizeof(av1_extend_tile_buf),
    };
    mfxExtAV1BitstreamParam av1_bs_param = {
         .Header.BufferId = MFX_EXTBUFF_AV1_BITSTREAM_PARAM,
         .Header.BufferSz = sizeof(av1_bs_param),
    };

    mfxExtCodingOption2 co2 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION2,
        .Header.BufferSz = sizeof(co2),
    };

    mfxExtCodingOption3 co3 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION3,
        .Header.BufferSz = sizeof(co3),
    };

    mfxExtBuffer *ext_buffers[] = {
        (mfxExtBuffer*)&av1_extend_tile_buf,
        (mfxExtBuffer*)&av1_bs_param,
        (mfxExtBuffer*)&co2,
        (mfxExtBuffer*)&co3,
    };

    if (!QSV_RUNTIME_VERSION_ATLEAST(q->ver, 2, 5)) {
        av_log(avctx, AV_LOG_ERROR,
               "This version of runtime doesn't support AV1 encoding\n");
        return AVERROR_UNKNOWN;
    }

    q->param.ExtParam    = ext_buffers;
    q->param.NumExtParam = FF_ARRAY_ELEMS(ext_buffers);

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error calling GetVideoParam");

    q->packet_size = q->param.mfx.BufferSizeInKB * q->param.mfx.BRCParamMultiplier * 1000;
    dump_video_av1_param(avctx, q, ext_buffers);
#endif
    return 0;
}

static int qsv_retrieve_enc_params(AVCodecContext *avctx, QSVEncContext *q)
{
    AVCPBProperties *cpb_props;

    uint8_t sps_buf[512];
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
    mfxExtCodingOption2 co2 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION2,
        .Header.BufferSz = sizeof(co2),
    };
    mfxExtCodingOption3 co3 = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION3,
        .Header.BufferSz = sizeof(co3),
    };

    uint8_t vps_buf[128];
    mfxExtCodingOptionVPS extradata_vps = {
        .Header.BufferId = MFX_EXTBUFF_CODING_OPTION_VPS,
        .Header.BufferSz = sizeof(extradata_vps),
        .VPSBuffer       = vps_buf,
        .VPSBufSize      = sizeof(vps_buf),
    };

    mfxExtHEVCTiles hevc_tile_buf = {
         .Header.BufferId = MFX_EXTBUFF_HEVC_TILES,
         .Header.BufferSz = sizeof(hevc_tile_buf),
    };

#if QSV_HAVE_HE
    mfxExtHyperModeParam hyper_mode_param_buf = {
        .Header.BufferId = MFX_EXTBUFF_HYPER_MODE_PARAM,
        .Header.BufferSz = sizeof(hyper_mode_param_buf),
    };
#endif

    mfxExtBuffer *ext_buffers[6 + QSV_HAVE_HE];

    int need_pps = avctx->codec_id != AV_CODEC_ID_MPEG2VIDEO;
    int ret, ext_buf_num = 0, extradata_offset = 0;

    q->co2_idx = q->co3_idx = q->exthevctiles_idx = q->exthypermodeparam_idx = -1;
    ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&extradata;
    ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&co;

    // It is possible the runtime doesn't support the given ext buffer
    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 6)) {
        q->co2_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&co2;
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 11)) {
        q->co3_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&co3;
    }

    q->hevc_vps = ((avctx->codec_id == AV_CODEC_ID_HEVC) && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 17));
    if (q->hevc_vps)
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&extradata_vps;
    if (avctx->codec_id == AV_CODEC_ID_HEVC && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 13)) {
        q->exthevctiles_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&hevc_tile_buf;
    }
#if QSV_HAVE_HE
    if (q->dual_gfx && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 2, 4)) {
        q->exthypermodeparam_idx = ext_buf_num;
        ext_buffers[ext_buf_num++] = (mfxExtBuffer*)&hyper_mode_param_buf;
    }
#endif

    q->param.ExtParam    = ext_buffers;
    q->param.NumExtParam = ext_buf_num;

    ret = MFXVideoENCODE_GetVideoParam(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error calling GetVideoParam");

    q->packet_size = q->param.mfx.BufferSizeInKB * q->param.mfx.BRCParamMultiplier * 1000;

    if (!extradata.SPSBufSize || (need_pps && !extradata.PPSBufSize)
        || (q->hevc_vps && !extradata_vps.VPSBufSize)
    ) {
        av_log(avctx, AV_LOG_ERROR, "No extradata returned from libmfx.\n");
        return AVERROR_UNKNOWN;
    }

    avctx->extradata_size = extradata.SPSBufSize + need_pps * extradata.PPSBufSize;
    avctx->extradata_size += q->hevc_vps * extradata_vps.VPSBufSize;

    avctx->extradata = av_malloc(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    if (q->hevc_vps) {
        memcpy(avctx->extradata, vps_buf, extradata_vps.VPSBufSize);
        extradata_offset += extradata_vps.VPSBufSize;
    }

    memcpy(avctx->extradata + extradata_offset, sps_buf, extradata.SPSBufSize);
    extradata_offset += extradata.SPSBufSize;
    if (need_pps) {
        memcpy(avctx->extradata + extradata_offset, pps_buf, extradata.PPSBufSize);
        extradata_offset += extradata.PPSBufSize;
    }
    memset(avctx->extradata + avctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = avctx->rc_max_rate;
    cpb_props->min_bitrate = avctx->rc_min_rate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = avctx->rc_buffer_size;

    dump_video_param(avctx, q, ext_buffers);

    return 0;
}

#if QSV_HAVE_OPAQUE
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
#endif

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

        ret = ff_qsv_init_session_frames(avctx, &q->internal_qs.session,
                                         &q->frames_ctx, q->load_plugins,
#if QSV_HAVE_OPAQUE
                                         q->param.IOPattern == MFX_IOPATTERN_IN_OPAQUE_MEMORY,
#else
                                         0,
#endif
                                         MFX_GPUCOPY_OFF);
        if (ret < 0) {
            av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
            return ret;
        }

        q->session = q->internal_qs.session;
    } else if (avctx->hw_device_ctx) {
        ret = ff_qsv_init_session_device(avctx, &q->internal_qs.session,
                                         avctx->hw_device_ctx, q->load_plugins,
                                         MFX_GPUCOPY_OFF);
        if (ret < 0)
            return ret;

        q->session = q->internal_qs.session;
    } else {
        ret = ff_qsv_init_internal_session(avctx, &q->internal_qs,
                                           q->load_plugins, MFX_GPUCOPY_OFF);
        if (ret < 0)
            return ret;

        q->session = q->internal_qs.session;
    }

    return 0;
}

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q)
{
    int iopattern = 0;
    int opaque_alloc = 0;
    int ret;

    q->param.AsyncDepth = q->async_depth;

    q->async_fifo = av_fifo_alloc2(q->async_depth, sizeof(QSVPacket), AV_FIFO_FLAG_AUTO_GROW);
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
#if QSV_HAVE_OPAQUE
            if (frames_hwctx->frame_type & MFX_MEMTYPE_OPAQUE_FRAME)
                iopattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
            else if (frames_hwctx->frame_type &
                     (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))
                iopattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
#else
            if (frames_hwctx->frame_type &
                (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))
                iopattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
#endif
        }
    }

    if (!iopattern)
        iopattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    q->param.IOPattern = iopattern;
    ff_qsv_print_iopattern(avctx, iopattern, "Encoder");

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

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;
        int i, j;

        q->extparam = av_calloc(qsv->nb_ext_buffers + q->nb_extparam_internal,
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
#if QSV_HAVE_OPAQUE
        ret = qsv_init_opaque_alloc(avctx, q);
        if (ret < 0)
            return ret;
#else
        av_log(avctx, AV_LOG_ERROR, "User is requesting to allocate OPAQUE surface, "
               "however libmfx %d.%d doesn't support OPAQUE memory.\n",
               q->ver.Major, q->ver.Minor);
        return AVERROR_UNKNOWN;
#endif
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
    case AV_CODEC_ID_VP9:
        ret = qsv_retrieve_enc_vp9_params(avctx, q);
        break;
    case AV_CODEC_ID_AV1:
        ret = qsv_retrieve_enc_av1_params(avctx, q);
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

static void free_encoder_ctrl(mfxEncodeCtrl* enc_ctrl)
{
    if (enc_ctrl) {
        for (int i = 0; i < enc_ctrl->NumPayload && i < QSV_MAX_ENC_PAYLOAD; i++)
            av_freep(&enc_ctrl->Payload[i]);

        for (int i = 0; i < enc_ctrl->NumExtParam && i < QSV_MAX_ENC_EXTPARAM; i++)
            av_freep(&enc_ctrl->ExtParam[i]);

        enc_ctrl->NumPayload = 0;
        enc_ctrl->NumExtParam = 0;
    }
}

static void clear_unused_frames(QSVEncContext *q)
{
    QSVFrame *cur = q->work_frames;
    while (cur) {
        if (cur->used && !cur->surface.Data.Locked) {
            free_encoder_ctrl(&cur->enc_ctrl);
            //do not reuse enc_ctrl from previous frame
            memset(&cur->enc_ctrl, 0, sizeof(cur->enc_ctrl));
            cur->enc_ctrl.Payload = cur->payloads;
            cur->enc_ctrl.ExtParam = cur->extparam;
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
    frame->enc_ctrl.Payload = frame->payloads;
    frame->enc_ctrl.ExtParam = frame->extparam;
    *last = frame;

    *f = frame;
    frame->used = 1;

    return 0;
}

static int qsvenc_fill_padding_area(AVFrame *frame, int new_w, int new_h)
{
    const AVPixFmtDescriptor *desc;
    int max_step[4], filled[4] = { 0 };

    desc = av_pix_fmt_desc_get(frame->format);
    av_assert0(desc);
    av_image_fill_max_pixsteps(max_step, NULL, desc);

    for (int i = 0; i < desc->nb_components; i++) {
        const AVComponentDescriptor *comp = &desc->comp[i];
        int sheight, dheight, plane = comp->plane;
        ptrdiff_t swidth = av_image_get_linesize(frame->format,
                                                 frame->width,
                                                 plane);
        ptrdiff_t dwidth = av_image_get_linesize(frame->format,
                                                 new_w,
                                                 plane);

        if (swidth < 0 || dwidth < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_image_get_linesize failed\n");
            return AVERROR(EINVAL);
        }

        if (filled[plane])
            continue;

        sheight = frame->height;
        dheight = new_h;

        if (plane) {
            sheight = AV_CEIL_RSHIFT(frame->height, desc->log2_chroma_h);
            dheight = AV_CEIL_RSHIFT(new_h, desc->log2_chroma_h);
        }

        // Fill right padding
        if (new_w > frame->width) {
            for (int j = 0; j < sheight; j++) {
                void *line_ptr = frame->data[plane] + j * frame->linesize[plane] + swidth;

                av_memcpy_backptr(line_ptr,
                                  max_step[plane],
                                  new_w - frame->width);
            }
        }

        // Fill bottom padding
        for (int j = sheight; j < dheight; j++)
            memcpy(frame->data[plane] + j * frame->linesize[plane],
                   frame->data[plane] + (sheight - 1) * frame->linesize[plane],
                   dwidth);

        filled[plane] = 1;
    }

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
            int tmp_w, tmp_h;
            qf->frame->height = tmp_h = FFALIGN(frame->height, q->height_align);
            qf->frame->width  = tmp_w = FFALIGN(frame->width, q->width_align);

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

            ret = qsvenc_fill_padding_area(qf->frame, tmp_w, tmp_h);
            if (ret < 0) {
                av_frame_unref(qf->frame);
                return ret;
            }
        } else {
            av_frame_unref(qf->frame);
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

        ret = ff_qsv_map_frame_to_surface(qf->frame, &qf->surface);
        if (ret < 0) {
            av_log(q->avctx, AV_LOG_ERROR, "map frame to surface failed.\n");
            return ret;
        }
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

static int set_roi_encode_ctrl(AVCodecContext *avctx, const AVFrame *frame,
                               mfxEncodeCtrl *enc_ctrl)
{
    AVFrameSideData *sd = NULL;
    int mb_size;

    if (avctx->codec_id == AV_CODEC_ID_H264)
        mb_size = 16;
    else if (avctx->codec_id == AV_CODEC_ID_H265)
        mb_size = 32;
    else
        return 0;

    if (frame)
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);

    if (sd) {
        mfxExtEncoderROI *enc_roi = NULL;
        AVRegionOfInterest *roi;
        uint32_t roi_size;
        int nb_roi, i;

        roi = (AVRegionOfInterest *)sd->data;
        roi_size = roi->self_size;
        if (!roi_size || sd->size % roi_size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid ROI Data.\n");
            return AVERROR(EINVAL);
        }
        nb_roi = sd->size / roi_size;
        if (nb_roi > QSV_MAX_ROI_NUM) {
            av_log(avctx, AV_LOG_WARNING, "More ROIs set than "
                    "supported by driver (%d > %d).\n",
                    nb_roi, QSV_MAX_ROI_NUM);
            nb_roi = QSV_MAX_ROI_NUM;
        }

        enc_roi = av_mallocz(sizeof(*enc_roi));
        if (!enc_roi)
            return AVERROR(ENOMEM);
        enc_roi->Header.BufferId = MFX_EXTBUFF_ENCODER_ROI;
        enc_roi->Header.BufferSz = sizeof(*enc_roi);
        enc_roi->NumROI  = nb_roi;
        enc_roi->ROIMode = MFX_ROI_MODE_QP_DELTA;
        for (i = 0; i < nb_roi; i++) {
            roi = (AVRegionOfInterest *)(sd->data + roi_size * i);
            enc_roi->ROI[i].Top    = FFALIGN(roi->top, mb_size);
            enc_roi->ROI[i].Bottom = FFALIGN(roi->bottom, mb_size);
            enc_roi->ROI[i].Left   = FFALIGN(roi->left, mb_size);
            enc_roi->ROI[i].Right  = FFALIGN(roi->right, mb_size);
            enc_roi->ROI[i].DeltaQP =
                roi->qoffset.num * 51 / roi->qoffset.den;
            av_log(avctx, AV_LOG_DEBUG, "ROI: (%d,%d)-(%d,%d) -> %+d.\n",
                   roi->top, roi->left, roi->bottom, roi->right,
                   enc_roi->ROI[i].DeltaQP);
        }
        enc_ctrl->ExtParam[enc_ctrl->NumExtParam] = (mfxExtBuffer *)enc_roi;
        enc_ctrl->NumExtParam++;
    }
    return 0;
}

static void set_skip_frame_encode_ctrl(AVCodecContext *avctx, const AVFrame *frame,
                               mfxEncodeCtrl *enc_ctrl)
{
    AVDictionaryEntry* skip_frame_dict = NULL;
    if (!frame->metadata)
        return;
    skip_frame_dict = av_dict_get(frame->metadata, "qsv_skip_frame", NULL, 0);
    if (!skip_frame_dict)
        return;
    enc_ctrl->SkipFrame = strtol(skip_frame_dict->value, NULL, 10);
    return;
}

static int update_qp(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0, new_qp = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264 && avctx->codec_id != AV_CODEC_ID_HEVC)
        return 0;

    if (q->param.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
        UPDATE_PARAM(q->old_global_quality, avctx->global_quality);
        UPDATE_PARAM(q->old_i_quant_factor, avctx->i_quant_factor);
        UPDATE_PARAM(q->old_i_quant_offset, avctx->i_quant_offset);
        UPDATE_PARAM(q->old_b_quant_factor, avctx->b_quant_factor);
        UPDATE_PARAM(q->old_b_quant_offset, avctx->b_quant_offset);
        if (!updated)
            return 0;

        new_qp = avctx->global_quality / FF_QP2LAMBDA;
        q->param.mfx.QPI = av_clip(new_qp * fabs(avctx->i_quant_factor) +
                                    avctx->i_quant_offset, 0, 51);
        q->param.mfx.QPP = av_clip(new_qp, 0, 51);
        q->param.mfx.QPB = av_clip(new_qp * fabs(avctx->b_quant_factor) +
                                    avctx->b_quant_offset, 0, 51);
        av_log(avctx, AV_LOG_DEBUG,
               "Reset qp = %d/%d/%d for idr/p/b frames\n",
               q->param.mfx.QPI, q->param.mfx.QPP, q->param.mfx.QPB);
    }
    return updated;
}

static int update_max_frame_size(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264 && avctx->codec_id != AV_CODEC_ID_HEVC)
        return 0;

    UPDATE_PARAM(q->old_max_frame_size, q->max_frame_size);
    if (!updated)
        return 0;

    q->extco2.MaxFrameSize  = FFMAX(0, q->max_frame_size);
    av_log(avctx, AV_LOG_DEBUG,
           "Reset MaxFrameSize: %d;\n", q->extco2.MaxFrameSize);

    return updated;
}

static int update_gop_size(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;
    UPDATE_PARAM(q->old_gop_size, avctx->gop_size);
    if (!updated)
        return 0;

    q->param.mfx.GopPicSize = FFMAX(0, avctx->gop_size);
    av_log(avctx, AV_LOG_DEBUG, "reset GopPicSize to %d\n",
           q->param.mfx.GopPicSize);

    return updated;
}

static int update_rir(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264 && avctx->codec_id != AV_CODEC_ID_HEVC)
        return 0;

    UPDATE_PARAM(q->old_int_ref_type, q->int_ref_type);
    UPDATE_PARAM(q->old_int_ref_cycle_size, q->int_ref_cycle_size);
    UPDATE_PARAM(q->old_int_ref_qp_delta, q->int_ref_qp_delta);
    UPDATE_PARAM(q->old_int_ref_cycle_dist, q->int_ref_cycle_dist);
    if (!updated)
        return 0;

    q->extco2.IntRefType      = FFMAX(0, q->int_ref_type);
    q->extco2.IntRefCycleSize = FFMAX(0, q->int_ref_cycle_size);
    q->extco2.IntRefQPDelta   =
        q->int_ref_qp_delta != INT16_MIN ? q->int_ref_qp_delta : 0;
    q->extco3.IntRefCycleDist = FFMAX(0, q->int_ref_cycle_dist);
    av_log(avctx, AV_LOG_DEBUG,
           "Reset IntRefType: %d; IntRefCycleSize: %d; "
           "IntRefQPDelta: %d; IntRefCycleDist: %d\n",
           q->extco2.IntRefType, q->extco2.IntRefCycleSize,
           q->extco2.IntRefQPDelta, q->extco3.IntRefCycleDist);

    return updated;
}

static int update_min_max_qp(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264)
        return 0;

    UPDATE_PARAM(q->old_qmin, avctx->qmin);
    UPDATE_PARAM(q->old_qmax, avctx->qmax);
    UPDATE_PARAM(q->old_min_qp_i, q->min_qp_i);
    UPDATE_PARAM(q->old_max_qp_i, q->max_qp_i);
    UPDATE_PARAM(q->old_min_qp_p, q->min_qp_p);
    UPDATE_PARAM(q->old_max_qp_p, q->max_qp_p);
    UPDATE_PARAM(q->old_min_qp_b, q->min_qp_b);
    UPDATE_PARAM(q->old_max_qp_b, q->max_qp_b);
    if (!updated)
        return 0;

    if ((avctx->qmin >= 0 && avctx->qmax >= 0 && avctx->qmin > avctx->qmax) ||
        (q->max_qp_i >= 0 && q->min_qp_i >= 0 && q->min_qp_i > q->max_qp_i) ||
        (q->max_qp_p >= 0 && q->min_qp_p >= 0 && q->min_qp_p > q->max_qp_p) ||
        (q->max_qp_b >= 0 && q->min_qp_b >= 0 && q->min_qp_b > q->max_qp_b)) {
        av_log(avctx, AV_LOG_ERROR,
                "qmin and or qmax are set but invalid,"
                " please make sure min <= max\n");
        return AVERROR(EINVAL);
    }

    q->extco2.MinQPI = 0;
    q->extco2.MaxQPI = 0;
    q->extco2.MinQPP = 0;
    q->extco2.MaxQPP = 0;
    q->extco2.MinQPB = 0;
    q->extco2.MaxQPB = 0;
    if (avctx->qmin >= 0) {
        q->extco2.MinQPI = avctx->qmin > 51 ? 51 : avctx->qmin;
        q->extco2.MinQPB = q->extco2.MinQPP = q->extco2.MinQPI;
    }
    if (avctx->qmax >= 0) {
        q->extco2.MaxQPI = avctx->qmax > 51 ? 51 : avctx->qmax;
        q->extco2.MaxQPB = q->extco2.MaxQPP = q->extco2.MaxQPI;
    }
    if (q->min_qp_i >= 0)
        q->extco2.MinQPI = q->min_qp_i > 51 ? 51 : q->min_qp_i;
    if (q->max_qp_i >= 0)
        q->extco2.MaxQPI = q->max_qp_i > 51 ? 51 : q->max_qp_i;
    if (q->min_qp_p >= 0)
        q->extco2.MinQPP = q->min_qp_p > 51 ? 51 : q->min_qp_p;
    if (q->max_qp_p >= 0)
        q->extco2.MaxQPP = q->max_qp_p > 51 ? 51 : q->max_qp_p;
    if (q->min_qp_b >= 0)
        q->extco2.MinQPB = q->min_qp_b > 51 ? 51 : q->min_qp_b;
    if (q->max_qp_b >= 0)
        q->extco2.MaxQPB = q->max_qp_b > 51 ? 51 : q->max_qp_b;

    av_log(avctx, AV_LOG_VERBOSE, "Reset MinQPI: %d; MaxQPI: %d; "
        "MinQPP: %d; MaxQPP: %d; "
        "MinQPB: %d; MaxQPB: %d\n",
        q->extco2.MinQPI, q->extco2.MaxQPI,
        q->extco2.MinQPP, q->extco2.MaxQPP,
        q->extco2.MinQPB, q->extco2.MaxQPB);

    return updated;
}

static int update_low_delay_brc(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264 && avctx->codec_id != AV_CODEC_ID_HEVC)
        return 0;

    UPDATE_PARAM(q->old_low_delay_brc, q->low_delay_brc);
    if (!updated)
        return 0;

    q->extco3.LowDelayBRC = MFX_CODINGOPTION_UNKNOWN;
    if (q->low_delay_brc >= 0)
        q->extco3.LowDelayBRC = q->low_delay_brc ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
    av_log(avctx, AV_LOG_DEBUG, "Reset LowDelayBRC: %s\n",
           print_threestate(q->extco3.LowDelayBRC));

    return updated;
}

static int update_frame_rate(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    UPDATE_PARAM(q->old_framerate.num, avctx->framerate.num);
    UPDATE_PARAM(q->old_framerate.den, avctx->framerate.den);
    if (!updated)
        return 0;

    if (avctx->framerate.den > 0 && avctx->framerate.num > 0) {
        q->param.mfx.FrameInfo.FrameRateExtN = avctx->framerate.num;
        q->param.mfx.FrameInfo.FrameRateExtD = avctx->framerate.den;
    } else {
        q->param.mfx.FrameInfo.FrameRateExtN = avctx->time_base.den;
        q->param.mfx.FrameInfo.FrameRateExtD = avctx->time_base.num;
    }
    av_log(avctx, AV_LOG_DEBUG, "Reset framerate: %d/%d (%.2f fps).\n",
           q->param.mfx.FrameInfo.FrameRateExtN,
           q->param.mfx.FrameInfo.FrameRateExtD,
           (double)q->param.mfx.FrameInfo.FrameRateExtN / q->param.mfx.FrameInfo.FrameRateExtD);

    return updated;
}

static int update_bitrate(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;
    int target_bitrate_kbps, max_bitrate_kbps, brc_param_multiplier;
    int buffer_size_in_kilobytes, initial_delay_in_kilobytes;

    UPDATE_PARAM(q->old_rc_buffer_size, avctx->rc_buffer_size);
    UPDATE_PARAM(q->old_rc_initial_buffer_occupancy, avctx->rc_initial_buffer_occupancy);
    UPDATE_PARAM(q->old_bit_rate, avctx->bit_rate);
    UPDATE_PARAM(q->old_rc_max_rate, avctx->rc_max_rate);
    if (!updated)
        return 0;

    buffer_size_in_kilobytes   = avctx->rc_buffer_size / 8000;
    initial_delay_in_kilobytes = avctx->rc_initial_buffer_occupancy / 8000;
    target_bitrate_kbps        = avctx->bit_rate / 1000;
    max_bitrate_kbps           = avctx->rc_max_rate / 1000;
    brc_param_multiplier       = (FFMAX(FFMAX3(target_bitrate_kbps, max_bitrate_kbps, buffer_size_in_kilobytes),
                                    initial_delay_in_kilobytes) + 0x10000) / 0x10000;

    q->param.mfx.BufferSizeInKB = buffer_size_in_kilobytes / brc_param_multiplier;
    q->param.mfx.InitialDelayInKB = initial_delay_in_kilobytes / brc_param_multiplier;
    q->param.mfx.TargetKbps = target_bitrate_kbps / brc_param_multiplier;
    q->param.mfx.MaxKbps = max_bitrate_kbps / brc_param_multiplier;
    q->param.mfx.BRCParamMultiplier = brc_param_multiplier;
    av_log(avctx, AV_LOG_VERBOSE,
            "Reset BufferSizeInKB: %d; InitialDelayInKB: %d; "
            "TargetKbps: %d; MaxKbps: %d; BRCParamMultiplier: %d\n",
            q->param.mfx.BufferSizeInKB, q->param.mfx.InitialDelayInKB,
            q->param.mfx.TargetKbps, q->param.mfx.MaxKbps, q->param.mfx.BRCParamMultiplier);
    return updated;
}

static int update_pic_timing_sei(AVCodecContext *avctx, QSVEncContext *q)
{
    int updated = 0;

    if (avctx->codec_id != AV_CODEC_ID_H264 && avctx->codec_id != AV_CODEC_ID_HEVC)
        return 0;

    UPDATE_PARAM(q->old_pic_timing_sei, q->pic_timing_sei);
    if (!updated)
        return 0;

    q->extco.PicTimingSEI = q->pic_timing_sei ?
                            MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN;
    av_log(avctx, AV_LOG_DEBUG, "Reset PicTimingSEI: %s\n",
           print_threestate(q->extco.PicTimingSEI));

    return updated;
}

static int encode_frame(AVCodecContext *avctx, QSVEncContext *q,
                        const AVFrame *frame)
{
    QSVPacket pkt = { { 0 } };
    mfxExtAVCEncodedFrameInfo *enc_info = NULL;
    mfxExtBuffer **enc_buf = NULL;

    mfxFrameSurface1 *surf = NULL;
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

        if (frame->pict_type == AV_PICTURE_TYPE_I) {
            enc_ctrl->FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_REF;
            if (q->forced_idr)
                enc_ctrl->FrameType |= MFX_FRAMETYPE_IDR;
        }
    }

    ret = av_new_packet(&pkt.pkt, q->packet_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating the output packet\n");
        return ret;
    }

    pkt.bs = av_mallocz(sizeof(*pkt.bs));
    if (!pkt.bs)
        goto nomem;
    pkt.bs->Data      = pkt.pkt.data;
    pkt.bs->MaxLength = pkt.pkt.size;

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        enc_info = av_mallocz(sizeof(*enc_info));
        if (!enc_info)
            goto nomem;

        enc_info->Header.BufferId = MFX_EXTBUFF_ENCODED_FRAME_INFO;
        enc_info->Header.BufferSz = sizeof (*enc_info);
        pkt.bs->NumExtParam = 1;
        enc_buf = av_mallocz(sizeof(mfxExtBuffer *));
        if (!enc_buf)
            goto nomem;
        enc_buf[0] = (mfxExtBuffer *)enc_info;

        pkt.bs->ExtParam = enc_buf;
    }

    if (q->set_encode_ctrl_cb && enc_ctrl) {
        q->set_encode_ctrl_cb(avctx, frame, enc_ctrl);
    }

    if ((avctx->codec_id == AV_CODEC_ID_H264 ||
         avctx->codec_id == AV_CODEC_ID_H265) &&
         enc_ctrl && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 8)) {
        ret = set_roi_encode_ctrl(avctx, frame, enc_ctrl);
        if (ret < 0)
            goto free;
    }
    if ((avctx->codec_id == AV_CODEC_ID_H264 ||
         avctx->codec_id == AV_CODEC_ID_H265) &&
         q->skip_frame != MFX_SKIPFRAME_NO_SKIP &&
         enc_ctrl && QSV_RUNTIME_VERSION_ATLEAST(q->ver, 1, 13))
        set_skip_frame_encode_ctrl(avctx, frame, enc_ctrl);

    pkt.sync = av_mallocz(sizeof(*pkt.sync));
    if (!pkt.sync)
        goto nomem;

    do {
        ret = MFXVideoENCODE_EncodeFrameAsync(q->session, enc_ctrl, surf, pkt.bs, pkt.sync);
        if (ret == MFX_WRN_DEVICE_BUSY)
            av_usleep(500);
    } while (ret == MFX_WRN_DEVICE_BUSY || ret == MFX_WRN_IN_EXECUTION);

    if (ret > 0)
        ff_qsv_print_warning(avctx, ret, "Warning during encoding");

    if (ret < 0) {
        ret = (ret == MFX_ERR_MORE_DATA) ?
               AVERROR(EAGAIN) : ff_qsv_print_error(avctx, ret, "Error during encoding");
        goto free;
    }

    if (ret == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM && frame && frame->interlaced_frame)
        print_interlace_msg(avctx, q);

    ret = 0;

    if (*pkt.sync) {
        ret = av_fifo_write(q->async_fifo, &pkt, 1);
        if (ret < 0)
            goto free;
    } else {
free:
        av_freep(&pkt.sync);
        av_packet_unref(&pkt.pkt);
        av_freep(&pkt.bs);
        if (avctx->codec_id == AV_CODEC_ID_H264) {
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
    }

    return ret;
nomem:
    ret = AVERROR(ENOMEM);
    goto free;
}

static int update_parameters(AVCodecContext *avctx, QSVEncContext *q,
                             const AVFrame *frame)
{
    int needReset = 0, ret = 0;

    if (!frame || avctx->codec_id == AV_CODEC_ID_MJPEG)
        return 0;

    needReset = update_qp(avctx, q);
    needReset |= update_max_frame_size(avctx, q);
    needReset |= update_gop_size(avctx, q);
    needReset |= update_rir(avctx, q);
    needReset |= update_low_delay_brc(avctx, q);
    needReset |= update_frame_rate(avctx, q);
    needReset |= update_bitrate(avctx, q);
    needReset |= update_pic_timing_sei(avctx, q);
    ret = update_min_max_qp(avctx, q);
    if (ret < 0)
        return ret;
    needReset |= ret;
    if (!needReset)
        return 0;

    if (avctx->hwaccel_context) {
        AVQSVContext *qsv = avctx->hwaccel_context;
        int i, j;
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

    // Flush codec before reset configuration.
    while (ret != AVERROR(EAGAIN)) {
        ret = encode_frame(avctx, q, NULL);
        if (ret < 0 && ret != AVERROR(EAGAIN))
            return ret;
    }

    av_log(avctx, AV_LOG_DEBUG, "Parameter change, call msdk reset.\n");
    ret = MFXVideoENCODE_Reset(q->session, &q->param);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret, "Error during resetting");

    return 0;
}

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    int ret;

    ret = update_parameters(avctx, q, frame);
    if (ret < 0)
        return ret;

    ret = encode_frame(avctx, q, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return ret;

    if ((av_fifo_can_read(q->async_fifo) >= q->async_depth) ||
        (!frame && av_fifo_can_read(q->async_fifo))) {
        QSVPacket qpkt;
        mfxExtAVCEncodedFrameInfo *enc_info;
        mfxExtBuffer **enc_buf;
        enum AVPictureType pict_type;

        av_fifo_read(q->async_fifo, &qpkt, 1);

        do {
            ret = MFXVideoCORE_SyncOperation(q->session, *qpkt.sync, 1000);
        } while (ret == MFX_WRN_IN_EXECUTION);

        qpkt.pkt.dts  = av_rescale_q(qpkt.bs->DecodeTimeStamp, (AVRational){1, 90000}, avctx->time_base);
        qpkt.pkt.pts  = av_rescale_q(qpkt.bs->TimeStamp,       (AVRational){1, 90000}, avctx->time_base);
        qpkt.pkt.size = qpkt.bs->DataLength;

        if (qpkt.bs->FrameType & MFX_FRAMETYPE_IDR || qpkt.bs->FrameType & MFX_FRAMETYPE_xIDR) {
            qpkt.pkt.flags |= AV_PKT_FLAG_KEY;
            pict_type = AV_PICTURE_TYPE_I;
        } else if (qpkt.bs->FrameType & MFX_FRAMETYPE_I || qpkt.bs->FrameType & MFX_FRAMETYPE_xI)
            pict_type = AV_PICTURE_TYPE_I;
        else if (qpkt.bs->FrameType & MFX_FRAMETYPE_P || qpkt.bs->FrameType & MFX_FRAMETYPE_xP)
            pict_type = AV_PICTURE_TYPE_P;
        else if (qpkt.bs->FrameType & MFX_FRAMETYPE_B || qpkt.bs->FrameType & MFX_FRAMETYPE_xB)
            pict_type = AV_PICTURE_TYPE_B;
        else if (qpkt.bs->FrameType == MFX_FRAMETYPE_UNKNOWN) {
            pict_type = AV_PICTURE_TYPE_NONE;
            av_log(avctx, AV_LOG_WARNING, "Unknown FrameType, set pict_type to AV_PICTURE_TYPE_NONE.\n");
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid FrameType:%d.\n", qpkt.bs->FrameType);
            return AVERROR_INVALIDDATA;
        }

        if (avctx->codec_id == AV_CODEC_ID_H264) {
            enc_buf = qpkt.bs->ExtParam;
            enc_info = (mfxExtAVCEncodedFrameInfo *)(*enc_buf);
            ff_side_data_set_encoder_stats(&qpkt.pkt,
                enc_info->QP * FF_QP2LAMBDA, NULL, 0, pict_type);
            av_freep(&enc_info);
            av_freep(&enc_buf);
        }
        av_freep(&qpkt.bs);
        av_freep(&qpkt.sync);

        av_packet_move_ref(pkt, &qpkt.pkt);

        *got_packet = 1;
    }

    return 0;
}

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q)
{
    QSVFrame *cur;

    if (q->session)
        MFXVideoENCODE_Close(q->session);

    q->session          = NULL;
    ff_qsv_close_internal_session(&q->internal_qs);

    av_buffer_unref(&q->frames_ctx.hw_frames_ctx);
    av_buffer_unref(&q->frames_ctx.mids_buf);

    cur = q->work_frames;
    while (cur) {
        q->work_frames = cur->next;
        av_frame_free(&cur->frame);
        free_encoder_ctrl(&cur->enc_ctrl);
        av_freep(&cur);
        cur = q->work_frames;
    }

    if (q->async_fifo) {
        QSVPacket pkt;
        while (av_fifo_read(q->async_fifo, &pkt, 1) >= 0) {
            if (avctx->codec_id == AV_CODEC_ID_H264) {
                mfxExtBuffer **enc_buf = pkt.bs->ExtParam;
                mfxExtAVCEncodedFrameInfo *enc_info = (mfxExtAVCEncodedFrameInfo *)(*enc_buf);
                av_freep(&enc_info);
                av_freep(&enc_buf);
            }
            av_freep(&pkt.sync);
            av_freep(&pkt.bs);
            av_packet_unref(&pkt.pkt);
        }
        av_fifo_freep2(&q->async_fifo);
    }

#if QSV_HAVE_OPAQUE
    av_freep(&q->opaque_surfaces);
    av_buffer_unref(&q->opaque_alloc_buf);
#endif

    av_freep(&q->extparam);

    return 0;
}

const AVCodecHWConfigInternal *const ff_qsv_enc_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(QSV,  QSV),
    HW_CONFIG_ENCODER_DEVICE(NV12, QSV),
    HW_CONFIG_ENCODER_DEVICE(P010, QSV),
    NULL,
};
