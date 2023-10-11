/*
 * Intel MediaSDK QSV based AV1 encoder
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


#include <stdint.h>
#include <sys/types.h>

#include "libavutil/common.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "bsf.h"
#include "qsv.h"
#include "qsvenc.h"

typedef struct QSVAV1EncContext {
    AVClass *class;
    AVBSFContext *extra_data_bsf;
    QSVEncContext qsv;
} QSVAV1EncContext;

static int qsv_av1_set_encode_ctrl(AVCodecContext *avctx,
                                   const AVFrame *frame, mfxEncodeCtrl *enc_ctrl)
{
    QSVAV1EncContext *q = avctx->priv_data;
    AVFrameSideData *sd;

    if (!frame || !QSV_RUNTIME_VERSION_ATLEAST(q->qsv.ver, 2, 11))
        return 0;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd) {
        AVMasteringDisplayMetadata *mdm = (AVMasteringDisplayMetadata *)sd->data;
        if (mdm->has_primaries && mdm->has_luminance) {
            const int chroma_den   = 1 << 16;
            const int max_luma_den = 1 << 8;
            const int min_luma_den = 1 << 14;
            mfxExtMasteringDisplayColourVolume *mdcv = av_mallocz(sizeof(*mdcv));
            if (!mdcv)
                return AVERROR(ENOMEM);

            mdcv->Header.BufferId = MFX_EXTBUFF_MASTERING_DISPLAY_COLOUR_VOLUME;
            mdcv->Header.BufferSz = sizeof(*mdcv);

            for (int i = 0; i < 3; i++) {
                mdcv->DisplayPrimariesX[i] =
                    av_rescale(mdm->display_primaries[i][0].num, chroma_den,
                               mdm->display_primaries[i][0].den);
                mdcv->DisplayPrimariesY[i] =
                    av_rescale(mdm->display_primaries[i][1].num, chroma_den,
                               mdm->display_primaries[i][1].den);
            }

            mdcv->WhitePointX =
                av_rescale(mdm->white_point[0].num, chroma_den,
                           mdm->white_point[0].den);
            mdcv->WhitePointY =
                av_rescale(mdm->white_point[1].num, chroma_den,
                           mdm->white_point[1].den);

            mdcv->MaxDisplayMasteringLuminance =
                av_rescale(mdm->max_luminance.num, max_luma_den,
                           mdm->max_luminance.den);
            mdcv->MinDisplayMasteringLuminance =
                av_rescale(mdm->min_luminance.num, min_luma_den,
                           mdm->min_luminance.den);

            enc_ctrl->ExtParam[enc_ctrl->NumExtParam++] = (mfxExtBuffer *)mdcv;
        }
    }

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
        mfxExtContentLightLevelInfo *clli = av_mallocz(sizeof(*clli));
        if (!clli)
            return AVERROR(ENOMEM);

        clli->Header.BufferId = MFX_EXTBUFF_CONTENT_LIGHT_LEVEL_INFO;
        clli->Header.BufferSz = sizeof(*clli);

        clli->MaxContentLightLevel          = clm->MaxCLL;
        clli->MaxPicAverageLightLevel       = clm->MaxFALL;

        enc_ctrl->ExtParam[enc_ctrl->NumExtParam++] = (mfxExtBuffer *)clli;
    }

    return 0;
}

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVAV1EncContext *q = avctx->priv_data;
    int ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        const AVBitStreamFilter *filter = av_bsf_get_by_name("extract_extradata");
        if (!filter) {
            av_log(avctx, AV_LOG_ERROR, "Cannot get extract_extradata bitstream filter\n");
            return AVERROR_BUG;
        }
        ret = av_bsf_alloc(filter, &q->extra_data_bsf);
        if (ret < 0)
            return ret;
        ret = avcodec_parameters_from_context(q->extra_data_bsf->par_in, avctx);
        if (ret < 0)
           return ret;
        ret = av_bsf_init(q->extra_data_bsf);
        if (ret < 0)
           return ret;
    }

    q->qsv.set_encode_ctrl_cb = qsv_av1_set_encode_ctrl;

    return ff_qsv_enc_init(avctx, &q->qsv);
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVAV1EncContext *q = avctx->priv_data;
    int ret;

    ret = ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
    if (ret < 0)
        return ret;

    if (*got_packet && avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        ret = av_bsf_send_packet(q->extra_data_bsf, pkt);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata filter "
                "failed to send input packet\n");
            return ret;
        }

        ret = av_bsf_receive_packet(q->extra_data_bsf, pkt);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata filter "
                "failed to receive output packet\n");
            return ret;
        }
    }

    return ret;
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVAV1EncContext *q = avctx->priv_data;

    av_bsf_free(&q->extra_data_bsf);

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVAV1EncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    QSV_COMMON_OPTS
    QSV_OPTION_B_STRATEGY
    QSV_OPTION_ADAPTIVE_I
    QSV_OPTION_ADAPTIVE_B
    QSV_OPTION_EXTBRC
    QSV_OPTION_LOW_DELAY_BRC
    QSV_OPTION_MAX_FRAME_SIZE
    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, .unit = "profile" },
        { "unknown" , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN      }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
        { "main"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AV1_MAIN     }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "tile_cols",  "Number of columns for tiled encoding",   OFFSET(qsv.tile_cols),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, VE },
    { "tile_rows",  "Number of rows for tiled encoding",      OFFSET(qsv.tile_rows),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, VE },
    { "look_ahead_depth", "Depth of look ahead in number frames, available when extbrc option is enabled", OFFSET(qsv.look_ahead_depth), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 100, VE },
    { NULL },
};

static const AVClass class = {
    .class_name = "av1_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault qsv_enc_defaults[] = {
    { "b",         "0"    },
    { "g",         "-1"   },
    { "bf",        "-1"   },
    { "refs",      "0"    },
    { NULL },
};

FFCodec ff_av1_qsv_encoder = {
    .p.name           = "av1_qsv",
    .p.long_name      = NULL_IF_CONFIG_SMALL("AV1 (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVAV1EncContext),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_AV1,
    .init             = qsv_enc_init,
    FF_CODEC_ENCODE_CB(qsv_enc_frame),
    .close            = qsv_enc_close,
    .p.capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .p.pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_P010,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .color_ranges     = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class     = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name   = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
};
