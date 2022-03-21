/*
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

#include <inttypes.h>

#include "libavutil/log.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_h264.h"
#include "codec_id.h"
#include "h264.h"
#include "packet.h"

#define NEW_GLOBAL_PIC_INIT_QP 26

typedef struct H264RedundantPPSContext {
    CBSBSFContext common;
} H264RedundantPPSContext;


static int h264_redundant_pps_fixup_pps(H264RedundantPPSContext *ctx,
                                        CodedBitstreamUnit *unit)
{
    H264RawPPS *pps;
    int err;

    // The changes we are about to perform affect the parsing process,
    // so we must make sure that the PPS is writable, otherwise the
    // parsing of future slices will be incorrect and even raise errors.
    err = ff_cbs_make_unit_writable(ctx->common.input, unit);
    if (err < 0)
        return err;
    pps = unit->content;

    // Overwrite pic_init_qp with the global value.
    pps->pic_init_qp_minus26 = NEW_GLOBAL_PIC_INIT_QP - 26;

    // Some PPSs have this set, so it must be set in all of them.
    // (Slices which do not use such a PPS on input will still have
    // *_weight_l*flag as zero and therefore write equivalently.)
    pps->weighted_pred_flag = 1;

    return 0;
}

static int h264_redundant_pps_fixup_slice(H264RedundantPPSContext *ctx,
                                          H264RawSliceHeader *slice)
{
    const CodedBitstreamH264Context *const in = ctx->common.input->priv_data;
    const H264RawPPS *const pps = in->pps[slice->pic_parameter_set_id];

    // We modified the PPS's qp value, now offset this by applying
    // the negative offset to the slices.
    slice->slice_qp_delta += pps->pic_init_qp_minus26
                             - (NEW_GLOBAL_PIC_INIT_QP - 26);

    return 0;
}

static int h264_redundant_pps_update_fragment(AVBSFContext *bsf,
                                              AVPacket *pkt,
                                              CodedBitstreamFragment *au)
{
    H264RedundantPPSContext *ctx = bsf->priv_data;
    int au_has_sps;
    int err, i;

    au_has_sps = 0;
    for (i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *nal = &au->units[i];

        if (nal->type == H264_NAL_SPS)
            au_has_sps = 1;
        if (nal->type == H264_NAL_PPS) {
            err = h264_redundant_pps_fixup_pps(ctx, nal);
            if (err < 0)
                return err;
            if (!au_has_sps) {
                av_log(bsf, AV_LOG_VERBOSE, "Deleting redundant PPS "
                       "at %"PRId64".\n", pkt->pts);
                ff_cbs_delete_unit(au, i);
                i--;
                continue;
            }
        }
        if (nal->type == H264_NAL_SLICE ||
            nal->type == H264_NAL_IDR_SLICE) {
            H264RawSlice *slice = nal->content;
            h264_redundant_pps_fixup_slice(ctx, &slice->header);
        }
    }

    return 0;
}

static const CBSBSFType h264_redundant_pps_type = {
    .codec_id        = AV_CODEC_ID_H264,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &h264_redundant_pps_update_fragment,
};

static int h264_redundant_pps_init(AVBSFContext *bsf)
{
    return ff_cbs_bsf_generic_init(bsf, &h264_redundant_pps_type);
}

static const enum AVCodecID h264_redundant_pps_codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_h264_redundant_pps_bsf = {
    .p.name         = "h264_redundant_pps",
    .p.codec_ids    = h264_redundant_pps_codec_ids,
    .priv_data_size = sizeof(H264RedundantPPSContext),
    .init           = &h264_redundant_pps_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
