/*
 * VP9 HW decode acceleration through NVDEC
 *
 * Copyright (c) 2016 Timo Rothenpieler
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

#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "vp9shared.h"

static int nvdec_vp9_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    VP9SharedContext *h = avctx->priv_data;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    CUVIDVP9PICPARAMS *ppc = &pp->CodecSpecific.vp9;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = h->frames[CUR_FRAME].tf.f;

    int ret, i;

    ret = ff_nvdec_start_frame(avctx, cur_frame);
    if (ret < 0)
        return ret;

    fdd = (FrameDecodeData*)cur_frame->private_ref->data;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = (cur_frame->width  + 15) / 16,
        .FrameHeightInMbs  = (cur_frame->height + 15) / 16,
        .CurrPicIdx        = cf->idx,

        .CodecSpecific.vp9 = {
            .width                    = cur_frame->width,
            .height                   = cur_frame->height,

            .LastRefIdx               = ff_nvdec_get_ref_idx(h->refs[h->h.refidx[0]].f),
            .GoldenRefIdx             = ff_nvdec_get_ref_idx(h->refs[h->h.refidx[1]].f),
            .AltRefIdx                = ff_nvdec_get_ref_idx(h->refs[h->h.refidx[2]].f),

            .profile                  = h->h.profile,
            .frameContextIdx          = h->h.framectxid,
            .frameType                = !h->h.keyframe,
            .showFrame                = !h->h.invisible,
            .errorResilient           = h->h.errorres,
            .frameParallelDecoding    = h->h.parallelmode,
            .subSamplingX             = pixdesc->log2_chroma_w,
            .subSamplingY             = pixdesc->log2_chroma_h,
            .intraOnly                = h->h.intraonly,
            .allow_high_precision_mv  = h->h.keyframe ? 0 : h->h.highprecisionmvs,
            .refreshEntropyProbs      = h->h.refreshctx,

            .bitDepthMinus8Luma       = pixdesc->comp[0].depth - 8,
            .bitDepthMinus8Chroma     = pixdesc->comp[1].depth - 8,

            .loopFilterLevel          = h->h.filter.level,
            .loopFilterSharpness      = h->h.filter.sharpness,
            .modeRefLfEnabled         = h->h.lf_delta.enabled,

            .log2_tile_columns        = h->h.tiling.log2_tile_cols,
            .log2_tile_rows           = h->h.tiling.log2_tile_rows,

            .segmentEnabled           = h->h.segmentation.enabled,
            .segmentMapUpdate         = h->h.segmentation.update_map,
            .segmentMapTemporalUpdate = h->h.segmentation.temporal,
            .segmentFeatureMode       = h->h.segmentation.absolute_vals,

            .qpYAc                    = h->h.yac_qi,
            .qpYDc                    = h->h.ydc_qdelta,
            .qpChDc                   = h->h.uvdc_qdelta,
            .qpChAc                   = h->h.uvac_qdelta,

            .resetFrameContext        = h->h.resetctx,
            .mcomp_filter_type        = h->h.filtermode ^ (h->h.filtermode <= 1),

            .frameTagSize             = h->h.uncompressed_header_size,
            .offsetToDctParts         = h->h.compressed_header_size,

            .refFrameSignBias[0]      = 0,
        }
    };

    for (i = 0; i < 2; i++)
        ppc->mbModeLfDelta[i] = h->h.lf_delta.mode[i];

    for (i = 0; i < 4; i++)
        ppc->mbRefLfDelta[i] = h->h.lf_delta.ref[i];

    for (i = 0; i < 7; i++)
        ppc->mb_segment_tree_probs[i] = h->h.segmentation.prob[i];

    for (i = 0; i < 3; i++) {
        ppc->activeRefIdx[i] = h->h.refidx[i];
        ppc->segment_pred_probs[i] = h->h.segmentation.pred_prob[i];
        ppc->refFrameSignBias[i + 1] = h->h.signbias[i];
    }

    for (i = 0; i < 8; i++) {
        ppc->segmentFeatureEnable[i][0] = h->h.segmentation.feat[i].q_enabled;
        ppc->segmentFeatureEnable[i][1] = h->h.segmentation.feat[i].lf_enabled;
        ppc->segmentFeatureEnable[i][2] = h->h.segmentation.feat[i].ref_enabled;
        ppc->segmentFeatureEnable[i][3] = h->h.segmentation.feat[i].skip_enabled;

        ppc->segmentFeatureData[i][0] = h->h.segmentation.feat[i].q_val;
        ppc->segmentFeatureData[i][1] = h->h.segmentation.feat[i].lf_val;
        ppc->segmentFeatureData[i][2] = h->h.segmentation.feat[i].ref_val;
        ppc->segmentFeatureData[i][3] = 0;
    }

    switch (avctx->colorspace) {
    default:
    case AVCOL_SPC_UNSPECIFIED:
        ppc->colorSpace = 0;
        break;
    case AVCOL_SPC_BT470BG:
        ppc->colorSpace = 1;
        break;
    case AVCOL_SPC_BT709:
        ppc->colorSpace = 2;
        break;
    case AVCOL_SPC_SMPTE170M:
        ppc->colorSpace = 3;
        break;
    case AVCOL_SPC_SMPTE240M:
        ppc->colorSpace = 4;
        break;
    case AVCOL_SPC_BT2020_NCL:
        ppc->colorSpace = 5;
        break;
    case AVCOL_SPC_RESERVED:
        ppc->colorSpace = 6;
        break;
    case AVCOL_SPC_RGB:
        ppc->colorSpace = 7;
        break;
    }

    return 0;
}

static int nvdec_vp9_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // VP9 uses a fixed size pool of 8 possible reference frames
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 8, 0);
}

const FFHWAccel ff_vp9_nvdec_hwaccel = {
    .p.name               = "vp9_nvdec",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_VP9,
    .p.pix_fmt            = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_vp9_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_vp9_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
