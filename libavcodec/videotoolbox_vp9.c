/*
 * Videotoolbox hardware acceleration for VP9
 *
 * copyright (c) 2021 rcombs
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "videotoolbox.h"
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/mem.h"
#include "vt_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "vp9shared.h"

enum VPX_CHROMA_SUBSAMPLING
{
    VPX_SUBSAMPLING_420_VERTICAL = 0,
    VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA = 1,
    VPX_SUBSAMPLING_422 = 2,
    VPX_SUBSAMPLING_444 = 3,
};

static int get_vpx_chroma_subsampling(enum AVPixelFormat pixel_format,
                                      enum AVChromaLocation chroma_location)
{
    int chroma_w, chroma_h;
    if (av_pix_fmt_get_chroma_sub_sample(pixel_format, &chroma_w, &chroma_h) == 0) {
        if (chroma_w == 1 && chroma_h == 1) {
            return (chroma_location == AVCHROMA_LOC_LEFT)
                       ? VPX_SUBSAMPLING_420_VERTICAL
                       : VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA;
        } else if (chroma_w == 1 && chroma_h == 0) {
            return VPX_SUBSAMPLING_422;
        } else if (chroma_w == 0 && chroma_h == 0) {
            return VPX_SUBSAMPLING_444;
        }
    }
    return -1;
}

CFDataRef ff_videotoolbox_vpcc_extradata_create(AVCodecContext *avctx)
{
    const VP9SharedContext *h = avctx->priv_data;
    CFDataRef data = NULL;
    uint8_t *p;
    int vt_extradata_size;
    uint8_t *vt_extradata;
    int subsampling = get_vpx_chroma_subsampling(avctx->sw_pix_fmt, avctx->chroma_sample_location);

    vt_extradata_size = 1 + 3 + 6 + 2;
    vt_extradata = av_malloc(vt_extradata_size);

    if (subsampling < 0)
        return NULL;

    if (!vt_extradata)
        return NULL;

    p = vt_extradata;

    *p++ = 1; /* version */
    AV_WB24(p + 1, 0); /* flags */
    p += 3;

   *p++ = h->h.profile;
   *p++ = avctx->level;
   *p++ = (h->h.bpp << 4) | (subsampling << 1) | (avctx->color_range == AVCOL_RANGE_JPEG);
   *p++ = avctx->color_primaries;
   *p++ = avctx->color_trc;
   *p++ = avctx->colorspace;

    AV_WB16(p + 0, 0);
    p += 2;

    av_assert0(p - vt_extradata == vt_extradata_size);

    data = CFDataCreate(kCFAllocatorDefault, vt_extradata, vt_extradata_size);
    av_free(vt_extradata);
    return data;
}

static int videotoolbox_vp9_start_frame(AVCodecContext *avctx,
                                        const AVBufferRef *buffer_ref,
                                        const uint8_t *buffer,
                                        uint32_t size)
{
    return 0;
}

static int videotoolbox_vp9_decode_slice(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    return ff_videotoolbox_buffer_copy(vtctx, buffer, size);
}

static int videotoolbox_vp9_end_frame(AVCodecContext *avctx)
{
    const VP9SharedContext *h = avctx->priv_data;
    AVFrame *frame = h->frames[CUR_FRAME].tf.f;

    return ff_videotoolbox_common_end_frame(avctx, frame);
}

const FFHWAccel ff_vp9_videotoolbox_hwaccel = {
    .p.name         = "vp9_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VP9,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_vp9_start_frame,
    .decode_slice   = videotoolbox_vp9_decode_slice,
    .end_frame      = videotoolbox_vp9_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};
