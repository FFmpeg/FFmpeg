/*
 * VP9 HW decode acceleration through VDPAU
 *
 * Copyright (c) 2019 Manoj Gupta Bonda
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
 * License along with FFmpeg; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vdpau/vdpau.h>
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "internal.h"
#include "vp9data.h"
#include "vp9dec.h"
#include "hwaccel.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int vdpau_vp9_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    VP9Context *s = avctx->priv_data;
    VP9SharedContext *h = &(s->s);
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!pixdesc) {
        return AV_PIX_FMT_NONE;
    }

    VP9Frame pic = h->frames[CUR_FRAME];
    struct vdpau_picture_context *pic_ctx = pic.hwaccel_picture_private;
    int i;

    VdpPictureInfoVP9 *info = &pic_ctx->info.vp9;

    info->width = avctx->width;
    info->height = avctx->height;
    /*  fill LvPictureInfoVP9 struct */
    info->lastReference  = VDP_INVALID_HANDLE;
    info->goldenReference = VDP_INVALID_HANDLE;
    info->altReference = VDP_INVALID_HANDLE;

    if (h->refs[h->h.refidx[0]].f && h->refs[h->h.refidx[0]].f->private_ref) {
        info->lastReference               = ff_vdpau_get_surface_id(h->refs[h->h.refidx[0]].f);
    }
    if (h->refs[h->h.refidx[1]].f && h->refs[h->h.refidx[1]].f->private_ref) {
        info->goldenReference             = ff_vdpau_get_surface_id(h->refs[h->h.refidx[1]].f);
    }
    if (h->refs[h->h.refidx[2]].f && h->refs[h->h.refidx[2]].f->private_ref) {
        info->altReference                = ff_vdpau_get_surface_id(h->refs[h->h.refidx[2]].f);
    }

    info->profile                  = h->h.profile;
    info->frameContextIdx          = h->h.framectxid;
    info->keyFrame                 = h->h.keyframe;
    info->showFrame                = !h->h.invisible;
    info->errorResilient           = h->h.errorres;
    info->frameParallelDecoding    = h->h.parallelmode;

    info->subSamplingX             = pixdesc->log2_chroma_w;
    info->subSamplingY             = pixdesc->log2_chroma_h;

    info->intraOnly                = h->h.intraonly;
    info->allowHighPrecisionMv     = h->h.keyframe ? 0 : h->h.highprecisionmvs;
    info->refreshEntropyProbs      = h->h.refreshctx;

    info->bitDepthMinus8Luma       = pixdesc->comp[0].depth - 8;
    info->bitDepthMinus8Chroma     = pixdesc->comp[1].depth - 8;

    info->loopFilterLevel          = h->h.filter.level;
    info->loopFilterSharpness      = h->h.filter.sharpness;
    info->modeRefLfEnabled         = h->h.lf_delta.enabled;

    info->log2TileColumns          = h->h.tiling.log2_tile_cols;
    info->log2TileRows             = h->h.tiling.log2_tile_rows;

    info->segmentEnabled           = h->h.segmentation.enabled;
    info->segmentMapUpdate         = h->h.segmentation.update_map;
    info->segmentMapTemporalUpdate = h->h.segmentation.temporal;
    info->segmentFeatureMode       = h->h.segmentation.absolute_vals;

    info->qpYAc                    = h->h.yac_qi;
    info->qpYDc                    = h->h.ydc_qdelta;
    info->qpChDc                   = h->h.uvdc_qdelta;
    info->qpChAc                   = h->h.uvac_qdelta;

    info->resetFrameContext        = h->h.resetctx;
    info->mcompFilterType          = h->h.filtermode ^ (h->h.filtermode <= 1);
    info->uncompressedHeaderSize   = h->h.uncompressed_header_size;
    info->compressedHeaderSize     = h->h.compressed_header_size;
    info->refFrameSignBias[0]      = 0;


    for (i = 0; i < FF_ARRAY_ELEMS(info->mbModeLfDelta); i++)
        info->mbModeLfDelta[i] = h->h.lf_delta.mode[i];

    for (i = 0; i < FF_ARRAY_ELEMS(info->mbRefLfDelta); i++)
        info->mbRefLfDelta[i] = h->h.lf_delta.ref[i];

    for (i = 0; i < FF_ARRAY_ELEMS(info->mbSegmentTreeProbs); i++)
        info->mbSegmentTreeProbs[i] = h->h.segmentation.prob[i];

    for (i = 0; i < FF_ARRAY_ELEMS(info->activeRefIdx); i++) {
        info->activeRefIdx[i] = h->h.refidx[i];
        info->segmentPredProbs[i] = h->h.segmentation.pred_prob[i];
        info->refFrameSignBias[i + 1] = h->h.signbias[i];
    }

    for (i = 0; i < FF_ARRAY_ELEMS(info->segmentFeatureEnable); i++) {
        info->segmentFeatureEnable[i][0] = h->h.segmentation.feat[i].q_enabled;
        info->segmentFeatureEnable[i][1] = h->h.segmentation.feat[i].lf_enabled;
        info->segmentFeatureEnable[i][2] = h->h.segmentation.feat[i].ref_enabled;
        info->segmentFeatureEnable[i][3] = h->h.segmentation.feat[i].skip_enabled;

        info->segmentFeatureData[i][0] = h->h.segmentation.feat[i].q_val;
        info->segmentFeatureData[i][1] = h->h.segmentation.feat[i].lf_val;
        info->segmentFeatureData[i][2] = h->h.segmentation.feat[i].ref_val;
        info->segmentFeatureData[i][3] = 0;
    }

    switch (avctx->colorspace) {
    default:
    case AVCOL_SPC_UNSPECIFIED:
        info->colorSpace = 0;
        break;
    case AVCOL_SPC_BT470BG:
        info->colorSpace = 1;
        break;
    case AVCOL_SPC_BT709:
        info->colorSpace = 2;
        break;
    case AVCOL_SPC_SMPTE170M:
        info->colorSpace = 3;
        break;
    case AVCOL_SPC_SMPTE240M:
        info->colorSpace = 4;
        break;
    case AVCOL_SPC_BT2020_NCL:
        info->colorSpace = 5;
        break;
    case AVCOL_SPC_RESERVED:
        info->colorSpace = 6;
        break;
    case AVCOL_SPC_RGB:
        info->colorSpace = 7;
        break;
    }

    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);

}

static const uint8_t start_code_prefix[3] = { 0x00, 0x00, 0x01 };

static int vdpau_vp9_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    VP9SharedContext *h = avctx->priv_data;
    VP9Frame pic = h->frames[CUR_FRAME];
    struct vdpau_picture_context *pic_ctx = pic.hwaccel_picture_private;

    int val;

    val = ff_vdpau_add_buffer(pic_ctx, start_code_prefix, 3);
    if (val)
        return val;

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val)
        return val;

    return 0;
}

static int vdpau_vp9_end_frame(AVCodecContext *avctx)
{
    VP9SharedContext *h = avctx->priv_data;
    VP9Frame pic = h->frames[CUR_FRAME];
    struct vdpau_picture_context *pic_ctx = pic.hwaccel_picture_private;

    int val;

    val = ff_vdpau_common_end_frame(avctx, pic.tf.f, pic_ctx);
    if (val < 0)
        return val;

    return 0;
}

static int vdpau_vp9_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;
    uint32_t level = avctx->level;

    switch (avctx->profile) {
    case FF_PROFILE_VP9_0:
        profile = VDP_DECODER_PROFILE_VP9_PROFILE_0;
        break;
    case FF_PROFILE_VP9_1:
        profile = VDP_DECODER_PROFILE_VP9_PROFILE_1;
        break;
    case FF_PROFILE_VP9_2:
        profile = VDP_DECODER_PROFILE_VP9_PROFILE_2;
        break;
    case FF_PROFILE_VP9_3:
        profile = VDP_DECODER_PROFILE_VP9_PROFILE_3;
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return ff_vdpau_common_init(avctx, profile, level);
}

const AVHWAccel ff_vp9_vdpau_hwaccel = {
    .name           = "vp9_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_vp9_start_frame,
    .end_frame      = vdpau_vp9_end_frame,
    .decode_slice   = vdpau_vp9_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_vp9_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
