/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Mickael Raulet
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2012 - 2013 Wassim Hamidouche
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

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/internal.h"
#include "libavutil/md5.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timecode.h"

#include "aom_film_grain.h"
#include "bswapdsp.h"
#include "cabac_functions.h"
#include "codec_internal.h"
#include "container_fifo.h"
#include "decode.h"
#include "golomb.h"
#include "hevc.h"
#include "parse.h"
#include "hevcdec.h"
#include "hwaccel_internal.h"
#include "hwconfig.h"
#include "internal.h"
#include "profiles.h"
#include "progressframe.h"
#include "refstruct.h"
#include "thread.h"
#include "threadprogress.h"

static const uint8_t hevc_pel_weight[65] = { [2] = 0, [4] = 1, [6] = 2, [8] = 3, [12] = 4, [16] = 5, [24] = 6, [32] = 7, [48] = 8, [64] = 9 };

/**
 * NOTE: Each function hls_foo correspond to the function foo in the
 * specification (HLS stands for High Level Syntax).
 */

/**
 * Section 5.7
 */

/* free everything allocated  by pic_arrays_init() */
static void pic_arrays_free(HEVCLayerContext *l)
{
    av_freep(&l->sao);
    av_freep(&l->deblock);

    av_freep(&l->skip_flag);
    av_freep(&l->tab_ct_depth);

    av_freep(&l->tab_ipm);
    av_freep(&l->cbf_luma);
    av_freep(&l->is_pcm);

    av_freep(&l->qp_y_tab);
    av_freep(&l->tab_slice_address);
    av_freep(&l->filter_slice_edges);

    av_freep(&l->horizontal_bs);
    av_freep(&l->vertical_bs);

    for (int i = 0; i < 3; i++) {
        av_freep(&l->sao_pixel_buffer_h[i]);
        av_freep(&l->sao_pixel_buffer_v[i]);
    }

    ff_refstruct_pool_uninit(&l->tab_mvf_pool);
    ff_refstruct_pool_uninit(&l->rpl_tab_pool);
}

/* allocate arrays that depend on frame dimensions */
static int pic_arrays_init(HEVCLayerContext *l, const HEVCSPS *sps)
{
    int log2_min_cb_size = sps->log2_min_cb_size;
    int width            = sps->width;
    int height           = sps->height;
    int pic_size_in_ctb  = ((width  >> log2_min_cb_size) + 1) *
                           ((height >> log2_min_cb_size) + 1);
    int ctb_count        = sps->ctb_width * sps->ctb_height;
    int min_pu_size      = sps->min_pu_width * sps->min_pu_height;

    l->bs_width  = (width  >> 2) + 1;
    l->bs_height = (height >> 2) + 1;

    l->sao           = av_calloc(ctb_count, sizeof(*l->sao));
    l->deblock       = av_calloc(ctb_count, sizeof(*l->deblock));
    if (!l->sao || !l->deblock)
        goto fail;

    l->skip_flag    = av_malloc_array(sps->min_cb_height, sps->min_cb_width);
    l->tab_ct_depth = av_malloc_array(sps->min_cb_height, sps->min_cb_width);
    if (!l->skip_flag || !l->tab_ct_depth)
        goto fail;

    l->cbf_luma = av_malloc_array(sps->min_tb_width, sps->min_tb_height);
    l->tab_ipm  = av_mallocz(min_pu_size);
    l->is_pcm   = av_malloc_array(sps->min_pu_width + 1, sps->min_pu_height + 1);
    if (!l->tab_ipm || !l->cbf_luma || !l->is_pcm)
        goto fail;

    l->filter_slice_edges = av_mallocz(ctb_count);
    l->tab_slice_address  = av_malloc_array(pic_size_in_ctb,
                                      sizeof(*l->tab_slice_address));
    l->qp_y_tab           = av_calloc(pic_size_in_ctb,
                                      sizeof(*l->qp_y_tab));
    if (!l->qp_y_tab || !l->filter_slice_edges || !l->tab_slice_address)
        goto fail;

    l->horizontal_bs = av_calloc(l->bs_width, l->bs_height);
    l->vertical_bs   = av_calloc(l->bs_width, l->bs_height);
    if (!l->horizontal_bs || !l->vertical_bs)
        goto fail;

    l->tab_mvf_pool = ff_refstruct_pool_alloc(min_pu_size * sizeof(MvField), 0);
    l->rpl_tab_pool = ff_refstruct_pool_alloc(ctb_count   * sizeof(RefPicListTab), 0);
    if (!l->tab_mvf_pool || !l->rpl_tab_pool)
        goto fail;

    if (sps->sao_enabled) {
        int c_count = (sps->chroma_format_idc != 0) ? 3 : 1;

        for (int c_idx = 0; c_idx < c_count; c_idx++) {
            int w = sps->width >> sps->hshift[c_idx];
            int h = sps->height >> sps->vshift[c_idx];
            l->sao_pixel_buffer_h[c_idx] =
                av_mallocz((w * 2 * sps->ctb_height) <<
                          sps->pixel_shift);
            l->sao_pixel_buffer_v[c_idx] =
                av_mallocz((h * 2 * sps->ctb_width) <<
                          sps->pixel_shift);
            if (!l->sao_pixel_buffer_h[c_idx] ||
                !l->sao_pixel_buffer_v[c_idx])
                goto fail;
        }
    }

    return 0;

fail:
    pic_arrays_free(l);
    return AVERROR(ENOMEM);
}

static int pred_weight_table(SliceHeader *sh, void *logctx,
                             const HEVCSPS *sps, GetBitContext *gb)
{
    int i = 0;
    int j = 0;
    uint8_t luma_weight_l0_flag[16];
    uint8_t chroma_weight_l0_flag[16];
    uint8_t luma_weight_l1_flag[16];
    uint8_t chroma_weight_l1_flag[16];
    int luma_log2_weight_denom;

    luma_log2_weight_denom = get_ue_golomb_long(gb);
    if (luma_log2_weight_denom < 0 || luma_log2_weight_denom > 7) {
        av_log(logctx, AV_LOG_ERROR, "luma_log2_weight_denom %d is invalid\n", luma_log2_weight_denom);
        return AVERROR_INVALIDDATA;
    }
    sh->luma_log2_weight_denom = av_clip_uintp2(luma_log2_weight_denom, 3);
    if (sps->chroma_format_idc != 0) {
        int64_t chroma_log2_weight_denom = luma_log2_weight_denom + (int64_t)get_se_golomb(gb);
        if (chroma_log2_weight_denom < 0 || chroma_log2_weight_denom > 7) {
            av_log(logctx, AV_LOG_ERROR, "chroma_log2_weight_denom %"PRId64" is invalid\n", chroma_log2_weight_denom);
            return AVERROR_INVALIDDATA;
        }
        sh->chroma_log2_weight_denom = chroma_log2_weight_denom;
    }

    for (i = 0; i < sh->nb_refs[L0]; i++) {
        luma_weight_l0_flag[i] = get_bits1(gb);
        if (!luma_weight_l0_flag[i]) {
            sh->luma_weight_l0[i] = 1 << sh->luma_log2_weight_denom;
            sh->luma_offset_l0[i] = 0;
        }
    }
    if (sps->chroma_format_idc != 0) {
        for (i = 0; i < sh->nb_refs[L0]; i++)
            chroma_weight_l0_flag[i] = get_bits1(gb);
    } else {
        for (i = 0; i < sh->nb_refs[L0]; i++)
            chroma_weight_l0_flag[i] = 0;
    }
    for (i = 0; i < sh->nb_refs[L0]; i++) {
        if (luma_weight_l0_flag[i]) {
            int delta_luma_weight_l0 = get_se_golomb(gb);
            if ((int8_t)delta_luma_weight_l0 != delta_luma_weight_l0)
                return AVERROR_INVALIDDATA;
            sh->luma_weight_l0[i] = (1 << sh->luma_log2_weight_denom) + delta_luma_weight_l0;
            sh->luma_offset_l0[i] = get_se_golomb(gb);
        }
        if (chroma_weight_l0_flag[i]) {
            for (j = 0; j < 2; j++) {
                int delta_chroma_weight_l0 = get_se_golomb(gb);
                int delta_chroma_offset_l0 = get_se_golomb(gb);

                if (   (int8_t)delta_chroma_weight_l0 != delta_chroma_weight_l0
                    || delta_chroma_offset_l0 < -(1<<17) || delta_chroma_offset_l0 > (1<<17)) {
                    return AVERROR_INVALIDDATA;
                }

                sh->chroma_weight_l0[i][j] = (1 << sh->chroma_log2_weight_denom) + delta_chroma_weight_l0;
                sh->chroma_offset_l0[i][j] = av_clip((delta_chroma_offset_l0 - ((128 * sh->chroma_weight_l0[i][j])
                                                                                    >> sh->chroma_log2_weight_denom) + 128), -128, 127);
            }
        } else {
            sh->chroma_weight_l0[i][0] = 1 << sh->chroma_log2_weight_denom;
            sh->chroma_offset_l0[i][0] = 0;
            sh->chroma_weight_l0[i][1] = 1 << sh->chroma_log2_weight_denom;
            sh->chroma_offset_l0[i][1] = 0;
        }
    }
    if (sh->slice_type == HEVC_SLICE_B) {
        for (i = 0; i < sh->nb_refs[L1]; i++) {
            luma_weight_l1_flag[i] = get_bits1(gb);
            if (!luma_weight_l1_flag[i]) {
                sh->luma_weight_l1[i] = 1 << sh->luma_log2_weight_denom;
                sh->luma_offset_l1[i] = 0;
            }
        }
        if (sps->chroma_format_idc != 0) {
            for (i = 0; i < sh->nb_refs[L1]; i++)
                chroma_weight_l1_flag[i] = get_bits1(gb);
        } else {
            for (i = 0; i < sh->nb_refs[L1]; i++)
                chroma_weight_l1_flag[i] = 0;
        }
        for (i = 0; i < sh->nb_refs[L1]; i++) {
            if (luma_weight_l1_flag[i]) {
                int delta_luma_weight_l1 = get_se_golomb(gb);
                if ((int8_t)delta_luma_weight_l1 != delta_luma_weight_l1)
                    return AVERROR_INVALIDDATA;
                sh->luma_weight_l1[i] = (1 << sh->luma_log2_weight_denom) + delta_luma_weight_l1;
                sh->luma_offset_l1[i] = get_se_golomb(gb);
            }
            if (chroma_weight_l1_flag[i]) {
                for (j = 0; j < 2; j++) {
                    int delta_chroma_weight_l1 = get_se_golomb(gb);
                    int delta_chroma_offset_l1 = get_se_golomb(gb);

                    if (   (int8_t)delta_chroma_weight_l1 != delta_chroma_weight_l1
                        || delta_chroma_offset_l1 < -(1<<17) || delta_chroma_offset_l1 > (1<<17)) {
                        return AVERROR_INVALIDDATA;
                    }

                    sh->chroma_weight_l1[i][j] = (1 << sh->chroma_log2_weight_denom) + delta_chroma_weight_l1;
                    sh->chroma_offset_l1[i][j] = av_clip((delta_chroma_offset_l1 - ((128 * sh->chroma_weight_l1[i][j])
                                                                                        >> sh->chroma_log2_weight_denom) + 128), -128, 127);
                }
            } else {
                sh->chroma_weight_l1[i][0] = 1 << sh->chroma_log2_weight_denom;
                sh->chroma_offset_l1[i][0] = 0;
                sh->chroma_weight_l1[i][1] = 1 << sh->chroma_log2_weight_denom;
                sh->chroma_offset_l1[i][1] = 0;
            }
        }
    }
    return 0;
}

static int decode_lt_rps(const HEVCSPS *sps, LongTermRPS *rps,
                         GetBitContext *gb, int cur_poc, int poc_lsb)
{
    int max_poc_lsb    = 1 << sps->log2_max_poc_lsb;
    int prev_delta_msb = 0;
    unsigned int nb_sps = 0, nb_sh;
    int i;

    rps->nb_refs = 0;
    if (!sps->long_term_ref_pics_present)
        return 0;

    if (sps->num_long_term_ref_pics_sps > 0)
        nb_sps = get_ue_golomb_long(gb);
    nb_sh = get_ue_golomb_long(gb);

    if (nb_sps > sps->num_long_term_ref_pics_sps)
        return AVERROR_INVALIDDATA;
    if (nb_sh + (uint64_t)nb_sps > FF_ARRAY_ELEMS(rps->poc))
        return AVERROR_INVALIDDATA;

    rps->nb_refs = nb_sh + nb_sps;

    for (i = 0; i < rps->nb_refs; i++) {

        if (i < nb_sps) {
            uint8_t lt_idx_sps = 0;

            if (sps->num_long_term_ref_pics_sps > 1)
                lt_idx_sps = get_bits(gb, av_ceil_log2(sps->num_long_term_ref_pics_sps));

            rps->poc[i]  = sps->lt_ref_pic_poc_lsb_sps[lt_idx_sps];
            rps->used[i] = !!(sps->used_by_curr_pic_lt & (1U << lt_idx_sps));
        } else {
            rps->poc[i]  = get_bits(gb, sps->log2_max_poc_lsb);
            rps->used[i] = get_bits1(gb);
        }

        rps->poc_msb_present[i] = get_bits1(gb);
        if (rps->poc_msb_present[i]) {
            int64_t delta = get_ue_golomb_long(gb);
            int64_t poc;

            if (i && i != nb_sps)
                delta += prev_delta_msb;

            poc = rps->poc[i] + cur_poc - delta * max_poc_lsb - poc_lsb;
            if (poc != (int32_t)poc)
                return AVERROR_INVALIDDATA;
            rps->poc[i] = poc;
            prev_delta_msb = delta;
        }
    }

    return 0;
}

static void export_stream_params(HEVCContext *s, const HEVCSPS *sps)
{
    AVCodecContext *avctx = s->avctx;
    const HEVCVPS    *vps = sps->vps;
    const HEVCWindow *ow = &sps->output_window;
    unsigned int num = 0, den = 0;

    avctx->pix_fmt             = sps->pix_fmt;
    avctx->coded_width         = sps->width;
    avctx->coded_height        = sps->height;
    avctx->width               = sps->width  - ow->left_offset - ow->right_offset;
    avctx->height              = sps->height - ow->top_offset  - ow->bottom_offset;
    avctx->has_b_frames        = sps->temporal_layer[sps->max_sub_layers - 1].num_reorder_pics;
    avctx->profile             = sps->ptl.general_ptl.profile_idc;
    avctx->level               = sps->ptl.general_ptl.level_idc;

    ff_set_sar(avctx, sps->vui.common.sar);

    if (sps->vui.common.video_signal_type_present_flag)
        avctx->color_range = sps->vui.common.video_full_range_flag ? AVCOL_RANGE_JPEG
                                                                   : AVCOL_RANGE_MPEG;
    else
        avctx->color_range = AVCOL_RANGE_MPEG;

    if (sps->vui.common.colour_description_present_flag) {
        avctx->color_primaries = sps->vui.common.colour_primaries;
        avctx->color_trc       = sps->vui.common.transfer_characteristics;
        avctx->colorspace      = sps->vui.common.matrix_coeffs;
    } else {
        avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
        avctx->color_trc       = AVCOL_TRC_UNSPECIFIED;
        avctx->colorspace      = AVCOL_SPC_UNSPECIFIED;
    }

    avctx->chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED;
    if (sps->chroma_format_idc == 1) {
        if (sps->vui.common.chroma_loc_info_present_flag) {
            if (sps->vui.common.chroma_sample_loc_type_top_field <= 5)
                avctx->chroma_sample_location = sps->vui.common.chroma_sample_loc_type_top_field + 1;
        } else
            avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    }

    if (vps->vps_timing_info_present_flag) {
        num = vps->vps_num_units_in_tick;
        den = vps->vps_time_scale;
    } else if (sps->vui.vui_timing_info_present_flag) {
        num = sps->vui.vui_num_units_in_tick;
        den = sps->vui.vui_time_scale;
    }

    if (num > 0 && den > 0)
        av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                  num, den, 1 << 30);
}

static int export_stream_params_from_sei(HEVCContext *s)
{
    AVCodecContext *avctx = s->avctx;

    if (s->sei.common.a53_caption.buf_ref)
        s->avctx->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;

    if (s->sei.common.alternative_transfer.present &&
        av_color_transfer_name(s->sei.common.alternative_transfer.preferred_transfer_characteristics) &&
        s->sei.common.alternative_transfer.preferred_transfer_characteristics != AVCOL_TRC_UNSPECIFIED) {
        avctx->color_trc = s->sei.common.alternative_transfer.preferred_transfer_characteristics;
    }

    if ((s->sei.common.film_grain_characteristics && s->sei.common.film_grain_characteristics->present) ||
        s->sei.common.aom_film_grain.enable)
        avctx->properties |= FF_CODEC_PROPERTY_FILM_GRAIN;

    return 0;
}

static int export_multilayer(HEVCContext *s, const HEVCVPS *vps)
{
    const HEVCSEITDRDI *tdrdi = &s->sei.tdrdi;

    av_freep(&s->view_ids_available);
    s->nb_view_ids_available = 0;
    av_freep(&s->view_pos_available);
    s->nb_view_pos_available = 0;

    // don't export anything in the trivial case (1 layer, view id=0)
    if (vps->nb_layers < 2 && !vps->view_id[0])
        return 0;

    s->view_ids_available = av_calloc(vps->nb_layers, sizeof(*s->view_ids_available));
    if (!s->view_ids_available)
        return AVERROR(ENOMEM);

    if (tdrdi->num_ref_displays) {
        s->view_pos_available = av_calloc(vps->nb_layers, sizeof(*s->view_pos_available));
        if (!s->view_pos_available)
            return AVERROR(ENOMEM);
    }

    for (int i = 0; i < vps->nb_layers; i++) {
        s->view_ids_available[i] = vps->view_id[i];

        if (s->view_pos_available) {
            s->view_pos_available[i] = vps->view_id[i] == tdrdi->left_view_id[0]  ?
                                       AV_STEREO3D_VIEW_LEFT                      :
                                       vps->view_id[i] == tdrdi->right_view_id[0] ?
                                       AV_STEREO3D_VIEW_RIGHT : AV_STEREO3D_VIEW_UNSPEC;
        }
    }
    s->nb_view_ids_available = vps->nb_layers;
    s->nb_view_pos_available = s->view_pos_available ? vps->nb_layers : 0;

    return 0;
}

static int setup_multilayer(HEVCContext *s, const HEVCVPS *vps)
{
    unsigned layers_active_output = 0, highest_layer;

    s->layers_active_output = 1;
    s->layers_active_decode = 1;

    // nothing requested - decode base layer only
    if (!s->nb_view_ids)
        return 0;

    if (s->nb_view_ids == 1 && s->view_ids[0] == -1) {
        layers_active_output = (1 << vps->nb_layers) - 1;
    } else {
        for (int i = 0; i < s->nb_view_ids; i++) {
            int view_id   = s->view_ids[i];
            int layer_idx = -1;

            if (view_id < 0) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid view ID requested: %d\n", view_id);
                return AVERROR(EINVAL);
            }

            for (int j = 0; j < vps->nb_layers; j++) {
                if (vps->view_id[j] == view_id) {
                    layer_idx = j;
                    break;
                }
            }
            if (layer_idx < 0) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "View ID %d not present in VPS\n", view_id);
                return AVERROR(EINVAL);
            }
            layers_active_output |= 1 << layer_idx;
        }
    }

    if (!layers_active_output) {
        av_log(s->avctx, AV_LOG_ERROR, "No layers selected\n");
        return AVERROR_BUG;
    }

    highest_layer = ff_log2(layers_active_output);
    if (highest_layer >= FF_ARRAY_ELEMS(s->layers)) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Too many layers requested: %u\n", layers_active_output);
        return AVERROR(EINVAL);
    }

    /* Assume a higher layer depends on all the lower ones.
     * This is enforced in VPS parsing currently, this logic will need
     * to be changed if we want to support more complex dependency structures.
     */
    s->layers_active_decode = (1 << (highest_layer + 1)) - 1;
    s->layers_active_output = layers_active_output;

    av_log(s->avctx, AV_LOG_DEBUG, "decode/output layers: %x/%x\n",
           s->layers_active_decode, s->layers_active_output);

    return 0;
}

static enum AVPixelFormat get_format(HEVCContext *s, const HEVCSPS *sps)
{
#define HWACCEL_MAX (CONFIG_HEVC_DXVA2_HWACCEL + \
                     CONFIG_HEVC_D3D11VA_HWACCEL * 2 + \
                     CONFIG_HEVC_D3D12VA_HWACCEL + \
                     CONFIG_HEVC_NVDEC_HWACCEL + \
                     CONFIG_HEVC_VAAPI_HWACCEL + \
                     CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL + \
                     CONFIG_HEVC_VDPAU_HWACCEL + \
                     CONFIG_HEVC_VULKAN_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmt = pix_fmts;
    int ret;

    switch (sps->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
#if CONFIG_HEVC_DXVA2_HWACCEL
        *fmt++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_HEVC_D3D11VA_HWACCEL
        *fmt++ = AV_PIX_FMT_D3D11VA_VLD;
        *fmt++ = AV_PIX_FMT_D3D11;
#endif
#if CONFIG_HEVC_D3D12VA_HWACCEL
        *fmt++ = AV_PIX_FMT_D3D12;
#endif
#if CONFIG_HEVC_VAAPI_HWACCEL
        *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VDPAU_HWACCEL
        *fmt++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_HEVC_NVDEC_HWACCEL
        *fmt++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
        *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV420P10:
#if CONFIG_HEVC_DXVA2_HWACCEL
        *fmt++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_HEVC_D3D11VA_HWACCEL
        *fmt++ = AV_PIX_FMT_D3D11VA_VLD;
        *fmt++ = AV_PIX_FMT_D3D11;
#endif
#if CONFIG_HEVC_D3D12VA_HWACCEL
        *fmt++ = AV_PIX_FMT_D3D12;
#endif
#if CONFIG_HEVC_VAAPI_HWACCEL
        *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
        *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
#if CONFIG_HEVC_VDPAU_HWACCEL
        *fmt++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_HEVC_NVDEC_HWACCEL
        *fmt++ = AV_PIX_FMT_CUDA;
#endif
        break;
    case AV_PIX_FMT_YUV444P:
#if CONFIG_HEVC_VAAPI_HWACCEL
        *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VDPAU_HWACCEL
        *fmt++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_HEVC_NVDEC_HWACCEL
        *fmt++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
        *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV422P10LE:
#if CONFIG_HEVC_VAAPI_HWACCEL
       *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
        *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV444P10:
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
        *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
    /* NOTE: fallthrough */
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV444P12:
#if CONFIG_HEVC_VAAPI_HWACCEL
       *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VDPAU_HWACCEL
        *fmt++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
#if CONFIG_HEVC_NVDEC_HWACCEL
        *fmt++ = AV_PIX_FMT_CUDA;
#endif
        break;
    case AV_PIX_FMT_YUV422P12:
#if CONFIG_HEVC_VAAPI_HWACCEL
       *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
        *fmt++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    }

    *fmt++ = sps->pix_fmt;
    *fmt = AV_PIX_FMT_NONE;

    // export multilayer information from active VPS to the caller,
    // so it is available in get_format()
    ret = export_multilayer(s, sps->vps);
    if (ret < 0)
        return ret;

    ret = ff_get_format(s->avctx, pix_fmts);
    if (ret < 0)
        return ret;
    s->avctx->pix_fmt = ret;

    // set up multilayer decoding, if requested by caller
    ret = setup_multilayer(s, sps->vps);
    if (ret < 0)
        return ret;

    return 0;
}

static int set_sps(HEVCContext *s, HEVCLayerContext *l, const HEVCSPS *sps)
{
    int ret;

    pic_arrays_free(l);
    ff_refstruct_unref(&l->sps);
    ff_refstruct_unref(&s->vps);

    if (!sps)
        return 0;

    ret = pic_arrays_init(l, sps);
    if (ret < 0)
        goto fail;

    ff_hevc_pred_init(&s->hpc,     sps->bit_depth);
    ff_hevc_dsp_init (&s->hevcdsp, sps->bit_depth);
    ff_videodsp_init (&s->vdsp,    sps->bit_depth);

    l->sps    = ff_refstruct_ref_c(sps);
    s->vps    = ff_refstruct_ref_c(sps->vps);

    return 0;

fail:
    pic_arrays_free(l);
    ff_refstruct_unref(&l->sps);
    return ret;
}

static int hls_slice_header(SliceHeader *sh, const HEVCContext *s, GetBitContext *gb)
{
    const HEVCPPS *pps;
    const HEVCSPS *sps;
    const HEVCVPS *vps;
    unsigned pps_id, layer_idx;
    int i, ret;

    // Coded parameters
    sh->first_slice_in_pic_flag = get_bits1(gb);

    sh->no_output_of_prior_pics_flag = 0;
    if (IS_IRAP(s))
        sh->no_output_of_prior_pics_flag = get_bits1(gb);

    pps_id = get_ue_golomb_long(gb);
    if (pps_id >= HEVC_MAX_PPS_COUNT || !s->ps.pps_list[pps_id]) {
        av_log(s->avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (!sh->first_slice_in_pic_flag && s->ps.pps_list[pps_id] != s->pps) {
        av_log(s->avctx, AV_LOG_ERROR, "PPS changed between slices.\n");
        return AVERROR_INVALIDDATA;
    }
    sh->pps_id = pps_id;

    pps = s->ps.pps_list[pps_id];
    sps = pps->sps;
    vps = sps->vps;
    layer_idx = vps->layer_idx[s->nuh_layer_id];

    if (s->nal_unit_type == HEVC_NAL_CRA_NUT && s->last_eos == 1)
        sh->no_output_of_prior_pics_flag = 1;

    sh->dependent_slice_segment_flag = 0;
    if (!sh->first_slice_in_pic_flag) {
        int slice_address_length;

        if (pps->dependent_slice_segments_enabled_flag)
            sh->dependent_slice_segment_flag = get_bits1(gb);
        if (sh->dependent_slice_segment_flag && !s->slice_initialized) {
            av_log(s->avctx, AV_LOG_ERROR, "Independent slice segment missing.\n");
            return AVERROR_INVALIDDATA;
        }

        slice_address_length = av_ceil_log2(sps->ctb_width *
                                            sps->ctb_height);
        sh->slice_segment_addr = get_bitsz(gb, slice_address_length);
        if (sh->slice_segment_addr >= sps->ctb_width * sps->ctb_height) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid slice segment address: %u.\n",
                   sh->slice_segment_addr);
            return AVERROR_INVALIDDATA;
        }

        if (!sh->dependent_slice_segment_flag) {
            sh->slice_addr = sh->slice_segment_addr;
        }
    } else {
        sh->slice_segment_addr = sh->slice_addr = 0;
    }

    if (!sh->dependent_slice_segment_flag) {
        for (i = 0; i < pps->num_extra_slice_header_bits; i++)
            skip_bits(gb, 1);  // slice_reserved_undetermined_flag[]

        sh->slice_type = get_ue_golomb_long(gb);
        if (!(sh->slice_type == HEVC_SLICE_I ||
              sh->slice_type == HEVC_SLICE_P ||
              sh->slice_type == HEVC_SLICE_B)) {
            av_log(s->avctx, AV_LOG_ERROR, "Unknown slice type: %d.\n",
                   sh->slice_type);
            return AVERROR_INVALIDDATA;
        }
        if (IS_IRAP(s) && sh->slice_type != HEVC_SLICE_I &&
            !pps->pps_curr_pic_ref_enabled_flag &&
            s->nuh_layer_id == 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Inter slices in an IRAP frame.\n");
            return AVERROR_INVALIDDATA;
        }

        // when flag is not present, picture is inferred to be output
        sh->pic_output_flag = 1;
        if (pps->output_flag_present_flag)
            sh->pic_output_flag = get_bits1(gb);

        if (sps->separate_colour_plane)
            sh->colour_plane_id = get_bits(gb, 2);

        if (!IS_IDR(s) ||
            (s->nuh_layer_id > 0 &&
             !(vps->poc_lsb_not_present & (1 << layer_idx)))) {
            int poc;

            sh->pic_order_cnt_lsb = get_bits(gb, sps->log2_max_poc_lsb);
            poc = ff_hevc_compute_poc(sps, s->poc_tid0, sh->pic_order_cnt_lsb, s->nal_unit_type);
            if (!sh->first_slice_in_pic_flag && poc != sh->poc) {
                av_log(s->avctx, AV_LOG_WARNING,
                       "Ignoring POC change between slices: %d -> %d\n", poc, sh->poc);
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
                poc = sh->poc;
            }
            sh->poc = poc;
        }

        if (!IS_IDR(s)) {
            int pos;

            sh->short_term_ref_pic_set_sps_flag = get_bits1(gb);
            pos = get_bits_left(gb);
            if (!sh->short_term_ref_pic_set_sps_flag) {
                ret = ff_hevc_decode_short_term_rps(gb, s->avctx, &sh->slice_rps, sps, 1);
                if (ret < 0)
                    return ret;

                sh->short_term_rps = &sh->slice_rps;
            } else {
                int numbits, rps_idx;

                if (!sps->nb_st_rps) {
                    av_log(s->avctx, AV_LOG_ERROR, "No ref lists in the SPS.\n");
                    return AVERROR_INVALIDDATA;
                }

                numbits = av_ceil_log2(sps->nb_st_rps);
                rps_idx = numbits > 0 ? get_bits(gb, numbits) : 0;
                sh->short_term_rps = &sps->st_rps[rps_idx];
            }
            sh->short_term_ref_pic_set_size = pos - get_bits_left(gb);

            pos = get_bits_left(gb);
            ret = decode_lt_rps(sps, &sh->long_term_rps, gb, sh->poc, sh->pic_order_cnt_lsb);
            if (ret < 0) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid long term RPS.\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }
            sh->long_term_ref_pic_set_size = pos - get_bits_left(gb);

            if (sps->temporal_mvp_enabled)
                sh->slice_temporal_mvp_enabled_flag = get_bits1(gb);
            else
                sh->slice_temporal_mvp_enabled_flag = 0;
        } else {
            sh->poc                             = 0;
            sh->pic_order_cnt_lsb               = 0;
            sh->short_term_ref_pic_set_sps_flag = 0;
            sh->short_term_ref_pic_set_size     = 0;
            sh->short_term_rps                  = NULL;
            sh->long_term_ref_pic_set_size      = 0;
            sh->slice_temporal_mvp_enabled_flag = 0;
        }

        sh->inter_layer_pred = 0;
        if (s->nuh_layer_id > 0) {
            int num_direct_ref_layers = vps->num_direct_ref_layers[layer_idx];

            if (vps->default_ref_layers_active)
                sh->inter_layer_pred = !!num_direct_ref_layers;
            else if (num_direct_ref_layers) {
                sh->inter_layer_pred = get_bits1(gb);

                if (sh->inter_layer_pred && num_direct_ref_layers > 1) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "NumDirectRefLayers>1 not supported\n");
                    return AVERROR_PATCHWELCOME;
                }
            }
        }

        if (sps->sao_enabled) {
            sh->slice_sample_adaptive_offset_flag[0] = get_bits1(gb);
            if (sps->chroma_format_idc) {
                sh->slice_sample_adaptive_offset_flag[1] =
                sh->slice_sample_adaptive_offset_flag[2] = get_bits1(gb);
            }
        } else {
            sh->slice_sample_adaptive_offset_flag[0] = 0;
            sh->slice_sample_adaptive_offset_flag[1] = 0;
            sh->slice_sample_adaptive_offset_flag[2] = 0;
        }

        sh->nb_refs[L0] = sh->nb_refs[L1] = 0;
        if (sh->slice_type == HEVC_SLICE_P || sh->slice_type == HEVC_SLICE_B) {
            int nb_refs;

            sh->nb_refs[L0] = pps->num_ref_idx_l0_default_active;
            if (sh->slice_type == HEVC_SLICE_B)
                sh->nb_refs[L1] = pps->num_ref_idx_l1_default_active;

            if (get_bits1(gb)) { // num_ref_idx_active_override_flag
                sh->nb_refs[L0] = get_ue_golomb_31(gb) + 1;
                if (sh->slice_type == HEVC_SLICE_B)
                    sh->nb_refs[L1] = get_ue_golomb_31(gb) + 1;
            }
            if (sh->nb_refs[L0] >= HEVC_MAX_REFS || sh->nb_refs[L1] >= HEVC_MAX_REFS) {
                av_log(s->avctx, AV_LOG_ERROR, "Too many refs: %d/%d.\n",
                       sh->nb_refs[L0], sh->nb_refs[L1]);
                return AVERROR_INVALIDDATA;
            }

            sh->rpl_modification_flag[0] = 0;
            sh->rpl_modification_flag[1] = 0;
            nb_refs = ff_hevc_frame_nb_refs(sh, pps, layer_idx);
            if (!nb_refs) {
                av_log(s->avctx, AV_LOG_ERROR, "Zero refs for a frame with P or B slices.\n");
                return AVERROR_INVALIDDATA;
            }

            if (pps->lists_modification_present_flag && nb_refs > 1) {
                sh->rpl_modification_flag[0] = get_bits1(gb);
                if (sh->rpl_modification_flag[0]) {
                    for (i = 0; i < sh->nb_refs[L0]; i++)
                        sh->list_entry_lx[0][i] = get_bits(gb, av_ceil_log2(nb_refs));
                }

                if (sh->slice_type == HEVC_SLICE_B) {
                    sh->rpl_modification_flag[1] = get_bits1(gb);
                    if (sh->rpl_modification_flag[1] == 1)
                        for (i = 0; i < sh->nb_refs[L1]; i++)
                            sh->list_entry_lx[1][i] = get_bits(gb, av_ceil_log2(nb_refs));
                }
            }

            if (sh->slice_type == HEVC_SLICE_B)
                sh->mvd_l1_zero_flag = get_bits1(gb);

            if (pps->cabac_init_present_flag)
                sh->cabac_init_flag = get_bits1(gb);
            else
                sh->cabac_init_flag = 0;

            sh->collocated_ref_idx = 0;
            if (sh->slice_temporal_mvp_enabled_flag) {
                sh->collocated_list = L0;
                if (sh->slice_type == HEVC_SLICE_B)
                    sh->collocated_list = !get_bits1(gb);

                if (sh->nb_refs[sh->collocated_list] > 1) {
                    sh->collocated_ref_idx = get_ue_golomb_long(gb);
                    if (sh->collocated_ref_idx >= sh->nb_refs[sh->collocated_list]) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Invalid collocated_ref_idx: %d.\n",
                               sh->collocated_ref_idx);
                        return AVERROR_INVALIDDATA;
                    }
                }
            }

            if ((pps->weighted_pred_flag   && sh->slice_type == HEVC_SLICE_P) ||
                (pps->weighted_bipred_flag && sh->slice_type == HEVC_SLICE_B)) {
                int ret = pred_weight_table(sh, s->avctx, sps, gb);
                if (ret < 0)
                    return ret;
            }

            sh->max_num_merge_cand = 5 - get_ue_golomb_long(gb);
            if (sh->max_num_merge_cand < 1 || sh->max_num_merge_cand > 5) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid number of merging MVP candidates: %d.\n",
                       sh->max_num_merge_cand);
                return AVERROR_INVALIDDATA;
            }

            // Syntax in 7.3.6.1
            if (sps->motion_vector_resolution_control_idc == 2)
                sh->use_integer_mv_flag = get_bits1(gb);
            else
                // Inferred to be equal to motion_vector_resolution_control_idc if not present
                sh->use_integer_mv_flag = sps->motion_vector_resolution_control_idc;

        }

        sh->slice_qp_delta = get_se_golomb(gb);

        if (pps->pic_slice_level_chroma_qp_offsets_present_flag) {
            sh->slice_cb_qp_offset = get_se_golomb(gb);
            sh->slice_cr_qp_offset = get_se_golomb(gb);
            if (sh->slice_cb_qp_offset < -12 || sh->slice_cb_qp_offset > 12 ||
                sh->slice_cr_qp_offset < -12 || sh->slice_cr_qp_offset > 12) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid slice cx qp offset.\n");
                return AVERROR_INVALIDDATA;
            }
        } else {
            sh->slice_cb_qp_offset = 0;
            sh->slice_cr_qp_offset = 0;
        }

        if (pps->pps_slice_act_qp_offsets_present_flag) {
            sh->slice_act_y_qp_offset  = get_se_golomb(gb);
            sh->slice_act_cb_qp_offset = get_se_golomb(gb);
            sh->slice_act_cr_qp_offset = get_se_golomb(gb);
        }

        if (pps->chroma_qp_offset_list_enabled_flag)
            sh->cu_chroma_qp_offset_enabled_flag = get_bits1(gb);
        else
            sh->cu_chroma_qp_offset_enabled_flag = 0;

        if (pps->deblocking_filter_control_present_flag) {
            int deblocking_filter_override_flag = 0;

            if (pps->deblocking_filter_override_enabled_flag)
                deblocking_filter_override_flag = get_bits1(gb);

            if (deblocking_filter_override_flag) {
                sh->disable_deblocking_filter_flag = get_bits1(gb);
                if (!sh->disable_deblocking_filter_flag) {
                    int beta_offset_div2 = get_se_golomb(gb);
                    int tc_offset_div2   = get_se_golomb(gb) ;
                    if (beta_offset_div2 < -6 || beta_offset_div2 > 6 ||
                        tc_offset_div2   < -6 || tc_offset_div2   > 6) {
                        av_log(s->avctx, AV_LOG_ERROR,
                            "Invalid deblock filter offsets: %d, %d\n",
                            beta_offset_div2, tc_offset_div2);
                        return AVERROR_INVALIDDATA;
                    }
                    sh->beta_offset = beta_offset_div2 * 2;
                    sh->tc_offset   =   tc_offset_div2 * 2;
                }
            } else {
                sh->disable_deblocking_filter_flag = pps->disable_dbf;
                sh->beta_offset                    = pps->beta_offset;
                sh->tc_offset                      = pps->tc_offset;
            }
        } else {
            sh->disable_deblocking_filter_flag = 0;
            sh->beta_offset                    = 0;
            sh->tc_offset                      = 0;
        }

        if (pps->seq_loop_filter_across_slices_enabled_flag &&
            (sh->slice_sample_adaptive_offset_flag[0] ||
             sh->slice_sample_adaptive_offset_flag[1] ||
             !sh->disable_deblocking_filter_flag)) {
            sh->slice_loop_filter_across_slices_enabled_flag = get_bits1(gb);
        } else {
            sh->slice_loop_filter_across_slices_enabled_flag = pps->seq_loop_filter_across_slices_enabled_flag;
        }
    }

    sh->num_entry_point_offsets = 0;
    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
        unsigned num_entry_point_offsets = get_ue_golomb_long(gb);
        // It would be possible to bound this tighter but this here is simpler
        if (num_entry_point_offsets > get_bits_left(gb) || num_entry_point_offsets > UINT16_MAX) {
            av_log(s->avctx, AV_LOG_ERROR, "num_entry_point_offsets %d is invalid\n", num_entry_point_offsets);
            return AVERROR_INVALIDDATA;
        }

        sh->num_entry_point_offsets = num_entry_point_offsets;
        if (sh->num_entry_point_offsets > 0) {
            int offset_len = get_ue_golomb_long(gb) + 1;

            if (offset_len < 1 || offset_len > 32) {
                sh->num_entry_point_offsets = 0;
                av_log(s->avctx, AV_LOG_ERROR, "offset_len %d is invalid\n", offset_len);
                return AVERROR_INVALIDDATA;
            }

            av_freep(&sh->entry_point_offset);
            av_freep(&sh->offset);
            av_freep(&sh->size);
            sh->entry_point_offset = av_malloc_array(sh->num_entry_point_offsets, sizeof(unsigned));
            sh->offset             = av_malloc_array(sh->num_entry_point_offsets + 1, sizeof(int));
            sh->size               = av_malloc_array(sh->num_entry_point_offsets + 1, sizeof(int));
            if (!sh->entry_point_offset || !sh->offset || !sh->size) {
                sh->num_entry_point_offsets = 0;
                av_log(s->avctx, AV_LOG_ERROR, "Failed to allocate memory\n");
                return AVERROR(ENOMEM);
            }
            for (i = 0; i < sh->num_entry_point_offsets; i++) {
                unsigned val = get_bits_long(gb, offset_len);
                sh->entry_point_offset[i] = val + 1; // +1; // +1 to get the size
            }
        }
    }

    if (pps->slice_header_extension_present_flag) {
        unsigned int length = get_ue_golomb_long(gb);
        if (length*8LL > get_bits_left(gb)) {
            av_log(s->avctx, AV_LOG_ERROR, "too many slice_header_extension_data_bytes\n");
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < length; i++)
            skip_bits(gb, 8);  // slice_header_extension_data_byte
    }

    ret = get_bits1(gb);
    if (!ret) {
        av_log(s->avctx, AV_LOG_ERROR, "alignment_bit_equal_to_one=0\n");
        return AVERROR_INVALIDDATA;
    }
    sh->data_offset = align_get_bits(gb) - gb->buffer;

    // Inferred parameters
    sh->slice_qp = 26U + pps->pic_init_qp_minus26 + sh->slice_qp_delta;
    if (sh->slice_qp > 51 ||
        sh->slice_qp < -sps->qp_bd_offset) {
        av_log(s->avctx, AV_LOG_ERROR,
               "The slice_qp %d is outside the valid range "
               "[%d, 51].\n",
               sh->slice_qp,
               -sps->qp_bd_offset);
        return AVERROR_INVALIDDATA;
    }

    sh->slice_ctb_addr_rs = sh->slice_segment_addr;

    if (sh->dependent_slice_segment_flag &&
        (!sh->slice_ctb_addr_rs || !pps->ctb_addr_rs_to_ts[sh->slice_ctb_addr_rs])) {
        av_log(s->avctx, AV_LOG_ERROR, "Impossible slice segment.\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Overread slice header by %d bits\n", -get_bits_left(gb));
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

#define CTB(tab, x, y) ((tab)[(y) * sps->ctb_width + (x)])

#define SET_SAO(elem, value)                            \
do {                                                    \
    if (!sao_merge_up_flag && !sao_merge_left_flag)     \
        sao->elem = value;                              \
    else if (sao_merge_left_flag)                       \
        sao->elem = CTB(l->sao, rx-1, ry).elem;         \
    else if (sao_merge_up_flag)                         \
        sao->elem = CTB(l->sao, rx, ry-1).elem;         \
    else                                                \
        sao->elem = 0;                                  \
} while (0)

static void hls_sao_param(HEVCLocalContext *lc, const HEVCLayerContext *l,
                          const HEVCPPS *pps, const HEVCSPS *sps,
                          int rx, int ry)
{
    const HEVCContext *const s = lc->parent;
    int sao_merge_left_flag = 0;
    int sao_merge_up_flag   = 0;
    SAOParams *sao          = &CTB(l->sao, rx, ry);
    int c_idx, i;

    if (s->sh.slice_sample_adaptive_offset_flag[0] ||
        s->sh.slice_sample_adaptive_offset_flag[1]) {
        if (rx > 0) {
            if (lc->ctb_left_flag)
                sao_merge_left_flag = ff_hevc_sao_merge_flag_decode(lc);
        }
        if (ry > 0 && !sao_merge_left_flag) {
            if (lc->ctb_up_flag)
                sao_merge_up_flag = ff_hevc_sao_merge_flag_decode(lc);
        }
    }

    for (c_idx = 0; c_idx < (sps->chroma_format_idc ? 3 : 1); c_idx++) {
        int log2_sao_offset_scale = c_idx == 0 ? pps->log2_sao_offset_scale_luma :
                                                 pps->log2_sao_offset_scale_chroma;

        if (!s->sh.slice_sample_adaptive_offset_flag[c_idx]) {
            sao->type_idx[c_idx] = SAO_NOT_APPLIED;
            continue;
        }

        if (c_idx == 2) {
            sao->type_idx[2] = sao->type_idx[1];
            sao->eo_class[2] = sao->eo_class[1];
        } else {
            SET_SAO(type_idx[c_idx], ff_hevc_sao_type_idx_decode(lc));
        }

        if (sao->type_idx[c_idx] == SAO_NOT_APPLIED)
            continue;

        for (i = 0; i < 4; i++)
            SET_SAO(offset_abs[c_idx][i], ff_hevc_sao_offset_abs_decode(lc, sps->bit_depth));

        if (sao->type_idx[c_idx] == SAO_BAND) {
            for (i = 0; i < 4; i++) {
                if (sao->offset_abs[c_idx][i]) {
                    SET_SAO(offset_sign[c_idx][i],
                            ff_hevc_sao_offset_sign_decode(lc));
                } else {
                    sao->offset_sign[c_idx][i] = 0;
                }
            }
            SET_SAO(band_position[c_idx], ff_hevc_sao_band_position_decode(lc));
        } else if (c_idx != 2) {
            SET_SAO(eo_class[c_idx], ff_hevc_sao_eo_class_decode(lc));
        }

        // Inferred parameters
        sao->offset_val[c_idx][0] = 0;
        for (i = 0; i < 4; i++) {
            sao->offset_val[c_idx][i + 1] = sao->offset_abs[c_idx][i];
            if (sao->type_idx[c_idx] == SAO_EDGE) {
                if (i > 1)
                    sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            } else if (sao->offset_sign[c_idx][i]) {
                sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            }
            sao->offset_val[c_idx][i + 1] *= 1 << log2_sao_offset_scale;
        }
    }
}

#undef SET_SAO
#undef CTB

static int hls_cross_component_pred(HEVCLocalContext *lc, int idx)
{
    int log2_res_scale_abs_plus1 = ff_hevc_log2_res_scale_abs(lc, idx);

    if (log2_res_scale_abs_plus1 !=  0) {
        int res_scale_sign_flag = ff_hevc_res_scale_sign_flag(lc, idx);
        lc->tu.res_scale_val = (1 << (log2_res_scale_abs_plus1 - 1)) *
                               (1 - 2 * res_scale_sign_flag);
    } else {
        lc->tu.res_scale_val = 0;
    }


    return 0;
}

static int hls_transform_unit(HEVCLocalContext *lc,
                              const HEVCLayerContext *l,
                              const HEVCPPS *pps, const HEVCSPS *sps,
                              int x0, int y0,
                              int xBase, int yBase, int cb_xBase, int cb_yBase,
                              int log2_cb_size, int log2_trafo_size,
                              int blk_idx, int cbf_luma, int *cbf_cb, int *cbf_cr)
{
    const HEVCContext *const s = lc->parent;
    const int log2_trafo_size_c = log2_trafo_size - sps->hshift[1];
    int i;

    if (lc->cu.pred_mode == MODE_INTRA) {
        int trafo_size = 1 << log2_trafo_size;
        ff_hevc_set_neighbour_available(lc, x0, y0, trafo_size, trafo_size, sps->log2_ctb_size);

        s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, x0, y0, 0);
    }

    if (cbf_luma || cbf_cb[0] || cbf_cr[0] ||
        (sps->chroma_format_idc == 2 && (cbf_cb[1] || cbf_cr[1]))) {
        int scan_idx   = SCAN_DIAG;
        int scan_idx_c = SCAN_DIAG;
        int cbf_chroma = cbf_cb[0] || cbf_cr[0] ||
                         (sps->chroma_format_idc == 2 &&
                         (cbf_cb[1] || cbf_cr[1]));

        if (pps->cu_qp_delta_enabled_flag && !lc->tu.is_cu_qp_delta_coded) {
            lc->tu.cu_qp_delta = ff_hevc_cu_qp_delta_abs(lc);
            if (lc->tu.cu_qp_delta != 0)
                if (ff_hevc_cu_qp_delta_sign_flag(lc) == 1)
                    lc->tu.cu_qp_delta = -lc->tu.cu_qp_delta;
            lc->tu.is_cu_qp_delta_coded = 1;

            if (lc->tu.cu_qp_delta < -(26 + sps->qp_bd_offset / 2) ||
                lc->tu.cu_qp_delta >  (25 + sps->qp_bd_offset / 2)) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "The cu_qp_delta %d is outside the valid range "
                       "[%d, %d].\n",
                       lc->tu.cu_qp_delta,
                       -(26 + sps->qp_bd_offset / 2),
                        (25 + sps->qp_bd_offset / 2));
                return AVERROR_INVALIDDATA;
            }

            ff_hevc_set_qPy(lc, l, pps, cb_xBase, cb_yBase, log2_cb_size);
        }

        if (s->sh.cu_chroma_qp_offset_enabled_flag && cbf_chroma &&
            !lc->cu.cu_transquant_bypass_flag  &&  !lc->tu.is_cu_chroma_qp_offset_coded) {
            int cu_chroma_qp_offset_flag = ff_hevc_cu_chroma_qp_offset_flag(lc);
            if (cu_chroma_qp_offset_flag) {
                int cu_chroma_qp_offset_idx  = 0;
                if (pps->chroma_qp_offset_list_len_minus1 > 0) {
                    cu_chroma_qp_offset_idx = ff_hevc_cu_chroma_qp_offset_idx(lc, pps->chroma_qp_offset_list_len_minus1);
                    av_log(s->avctx, AV_LOG_ERROR,
                        "cu_chroma_qp_offset_idx not yet tested.\n");
                }
                lc->tu.cu_qp_offset_cb = pps->cb_qp_offset_list[cu_chroma_qp_offset_idx];
                lc->tu.cu_qp_offset_cr = pps->cr_qp_offset_list[cu_chroma_qp_offset_idx];
            } else {
                lc->tu.cu_qp_offset_cb = 0;
                lc->tu.cu_qp_offset_cr = 0;
            }
            lc->tu.is_cu_chroma_qp_offset_coded = 1;
        }

        if (lc->cu.pred_mode == MODE_INTRA && log2_trafo_size < 4) {
            if (lc->tu.intra_pred_mode >= 6 &&
                lc->tu.intra_pred_mode <= 14) {
                scan_idx = SCAN_VERT;
            } else if (lc->tu.intra_pred_mode >= 22 &&
                       lc->tu.intra_pred_mode <= 30) {
                scan_idx = SCAN_HORIZ;
            }

            if (lc->tu.intra_pred_mode_c >=  6 &&
                lc->tu.intra_pred_mode_c <= 14) {
                scan_idx_c = SCAN_VERT;
            } else if (lc->tu.intra_pred_mode_c >= 22 &&
                       lc->tu.intra_pred_mode_c <= 30) {
                scan_idx_c = SCAN_HORIZ;
            }
        }

        lc->tu.cross_pf = 0;

        if (cbf_luma)
            ff_hevc_hls_residual_coding(lc, pps, x0, y0, log2_trafo_size, scan_idx, 0);
        if (sps->chroma_format_idc && (log2_trafo_size > 2 || sps->chroma_format_idc == 3)) {
            int trafo_size_h = 1 << (log2_trafo_size_c + sps->hshift[1]);
            int trafo_size_v = 1 << (log2_trafo_size_c + sps->vshift[1]);
            lc->tu.cross_pf  = (pps->cross_component_prediction_enabled_flag && cbf_luma &&
                                (lc->cu.pred_mode == MODE_INTER ||
                                 (lc->tu.chroma_mode_c ==  4)));

            if (lc->tu.cross_pf) {
                hls_cross_component_pred(lc, 0);
            }
            for (i = 0; i < (sps->chroma_format_idc == 2 ? 2 : 1); i++) {
                if (lc->cu.pred_mode == MODE_INTRA) {
                    ff_hevc_set_neighbour_available(lc, x0, y0 + (i << log2_trafo_size_c),
                                                    trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                    s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0 + (i << log2_trafo_size_c), 1);
                }
                if (cbf_cb[i])
                    ff_hevc_hls_residual_coding(lc, pps, x0, y0 + (i << log2_trafo_size_c),
                                                log2_trafo_size_c, scan_idx_c, 1);
                else
                    if (lc->tu.cross_pf) {
                        ptrdiff_t stride = s->cur_frame->f->linesize[1];
                        int hshift = sps->hshift[1];
                        int vshift = sps->vshift[1];
                        const int16_t *coeffs_y = (int16_t*)lc->edge_emu_buffer;
                        int16_t *coeffs   = (int16_t*)lc->edge_emu_buffer2;
                        int size = 1 << log2_trafo_size_c;

                        uint8_t *dst = &s->cur_frame->f->data[1][(y0 >> vshift) * stride +
                                                              ((x0 >> hshift) << sps->pixel_shift)];
                        for (i = 0; i < (size * size); i++) {
                            coeffs[i] = ((lc->tu.res_scale_val * coeffs_y[i]) >> 3);
                        }
                        s->hevcdsp.add_residual[log2_trafo_size_c-2](dst, coeffs, stride);
                    }
            }

            if (lc->tu.cross_pf) {
                hls_cross_component_pred(lc, 1);
            }
            for (i = 0; i < (sps->chroma_format_idc == 2 ? 2 : 1); i++) {
                if (lc->cu.pred_mode == MODE_INTRA) {
                    ff_hevc_set_neighbour_available(lc, x0, y0 + (i << log2_trafo_size_c),
                                                    trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                    s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0 + (i << log2_trafo_size_c), 2);
                }
                if (cbf_cr[i])
                    ff_hevc_hls_residual_coding(lc, pps, x0, y0 + (i << log2_trafo_size_c),
                                                log2_trafo_size_c, scan_idx_c, 2);
                else
                    if (lc->tu.cross_pf) {
                        ptrdiff_t stride = s->cur_frame->f->linesize[2];
                        int hshift = sps->hshift[2];
                        int vshift = sps->vshift[2];
                        const int16_t *coeffs_y = (int16_t*)lc->edge_emu_buffer;
                        int16_t *coeffs   = (int16_t*)lc->edge_emu_buffer2;
                        int size = 1 << log2_trafo_size_c;

                        uint8_t *dst = &s->cur_frame->f->data[2][(y0 >> vshift) * stride +
                                                          ((x0 >> hshift) << sps->pixel_shift)];
                        for (i = 0; i < (size * size); i++) {
                            coeffs[i] = ((lc->tu.res_scale_val * coeffs_y[i]) >> 3);
                        }
                        s->hevcdsp.add_residual[log2_trafo_size_c-2](dst, coeffs, stride);
                    }
            }
        } else if (sps->chroma_format_idc && blk_idx == 3) {
            int trafo_size_h = 1 << (log2_trafo_size + 1);
            int trafo_size_v = 1 << (log2_trafo_size + sps->vshift[1]);
            for (i = 0; i < (sps->chroma_format_idc == 2 ? 2 : 1); i++) {
                if (lc->cu.pred_mode == MODE_INTRA) {
                    ff_hevc_set_neighbour_available(lc, xBase, yBase + (i << log2_trafo_size),
                                                    trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                    s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase + (i << log2_trafo_size), 1);
                }
                if (cbf_cb[i])
                    ff_hevc_hls_residual_coding(lc, pps, xBase, yBase + (i << log2_trafo_size),
                                                log2_trafo_size, scan_idx_c, 1);
            }
            for (i = 0; i < (sps->chroma_format_idc == 2 ? 2 : 1); i++) {
                if (lc->cu.pred_mode == MODE_INTRA) {
                    ff_hevc_set_neighbour_available(lc, xBase, yBase + (i << log2_trafo_size),
                                                trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                    s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase + (i << log2_trafo_size), 2);
                }
                if (cbf_cr[i])
                    ff_hevc_hls_residual_coding(lc, pps, xBase, yBase + (i << log2_trafo_size),
                                                log2_trafo_size, scan_idx_c, 2);
            }
        }
    } else if (sps->chroma_format_idc && lc->cu.pred_mode == MODE_INTRA) {
        if (log2_trafo_size > 2 || sps->chroma_format_idc == 3) {
            int trafo_size_h = 1 << (log2_trafo_size_c + sps->hshift[1]);
            int trafo_size_v = 1 << (log2_trafo_size_c + sps->vshift[1]);
            ff_hevc_set_neighbour_available(lc, x0, y0, trafo_size_h, trafo_size_v,
                                            sps->log2_ctb_size);
            s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0, 1);
            s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0, 2);
            if (sps->chroma_format_idc == 2) {
                ff_hevc_set_neighbour_available(lc, x0, y0 + (1 << log2_trafo_size_c),
                                                trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0 + (1 << log2_trafo_size_c), 1);
                s->hpc.intra_pred[log2_trafo_size_c - 2](lc, pps, x0, y0 + (1 << log2_trafo_size_c), 2);
            }
        } else if (blk_idx == 3) {
            int trafo_size_h = 1 << (log2_trafo_size + 1);
            int trafo_size_v = 1 << (log2_trafo_size + sps->vshift[1]);
            ff_hevc_set_neighbour_available(lc, xBase, yBase,
                                            trafo_size_h, trafo_size_v, sps->log2_ctb_size);
            s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase, 1);
            s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase, 2);
            if (sps->chroma_format_idc == 2) {
                ff_hevc_set_neighbour_available(lc, xBase, yBase + (1 << log2_trafo_size),
                                                trafo_size_h, trafo_size_v, sps->log2_ctb_size);
                s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase + (1 << log2_trafo_size), 1);
                s->hpc.intra_pred[log2_trafo_size - 2](lc, pps, xBase, yBase + (1 << log2_trafo_size), 2);
            }
        }
    }

    return 0;
}

static void set_deblocking_bypass(uint8_t *is_pcm, const HEVCSPS *sps,
                                  int x0, int y0, int log2_cb_size)
{
    int cb_size          = 1 << log2_cb_size;
    int log2_min_pu_size = sps->log2_min_pu_size;

    int min_pu_width     = sps->min_pu_width;
    int x_end = FFMIN(x0 + cb_size, sps->width);
    int y_end = FFMIN(y0 + cb_size, sps->height);
    int i, j;

    for (j = (y0 >> log2_min_pu_size); j < (y_end >> log2_min_pu_size); j++)
        for (i = (x0 >> log2_min_pu_size); i < (x_end >> log2_min_pu_size); i++)
            is_pcm[i + j * min_pu_width] = 2;
}

static int hls_transform_tree(HEVCLocalContext *lc,
                              const HEVCLayerContext *l,
                              const HEVCPPS *pps, const HEVCSPS *sps,
                              int x0, int y0,
                              int xBase, int yBase, int cb_xBase, int cb_yBase,
                              int log2_cb_size, int log2_trafo_size,
                              int trafo_depth, int blk_idx,
                              const int *base_cbf_cb, const int *base_cbf_cr)
{
    const HEVCContext *const s = lc->parent;
    uint8_t split_transform_flag;
    int cbf_cb[2];
    int cbf_cr[2];
    int ret;

    cbf_cb[0] = base_cbf_cb[0];
    cbf_cb[1] = base_cbf_cb[1];
    cbf_cr[0] = base_cbf_cr[0];
    cbf_cr[1] = base_cbf_cr[1];

    if (lc->cu.intra_split_flag) {
        if (trafo_depth == 1) {
            lc->tu.intra_pred_mode   = lc->pu.intra_pred_mode[blk_idx];
            if (sps->chroma_format_idc == 3) {
                lc->tu.intra_pred_mode_c = lc->pu.intra_pred_mode_c[blk_idx];
                lc->tu.chroma_mode_c     = lc->pu.chroma_mode_c[blk_idx];
            } else {
                lc->tu.intra_pred_mode_c = lc->pu.intra_pred_mode_c[0];
                lc->tu.chroma_mode_c     = lc->pu.chroma_mode_c[0];
            }
        }
    } else {
        lc->tu.intra_pred_mode   = lc->pu.intra_pred_mode[0];
        lc->tu.intra_pred_mode_c = lc->pu.intra_pred_mode_c[0];
        lc->tu.chroma_mode_c     = lc->pu.chroma_mode_c[0];
    }

    if (log2_trafo_size <= sps->log2_max_trafo_size &&
        log2_trafo_size >  sps->log2_min_tb_size    &&
        trafo_depth     < lc->cu.max_trafo_depth       &&
        !(lc->cu.intra_split_flag && trafo_depth == 0)) {
        split_transform_flag = ff_hevc_split_transform_flag_decode(lc, log2_trafo_size);
    } else {
        int inter_split = sps->max_transform_hierarchy_depth_inter == 0 &&
                          lc->cu.pred_mode == MODE_INTER &&
                          lc->cu.part_mode != PART_2Nx2N &&
                          trafo_depth == 0;

        split_transform_flag = log2_trafo_size > sps->log2_max_trafo_size ||
                               (lc->cu.intra_split_flag && trafo_depth == 0) ||
                               inter_split;
    }

    if (sps->chroma_format_idc && (log2_trafo_size > 2 || sps->chroma_format_idc == 3)) {
        if (trafo_depth == 0 || cbf_cb[0]) {
            cbf_cb[0] = ff_hevc_cbf_cb_cr_decode(lc, trafo_depth);
            if (sps->chroma_format_idc == 2 && (!split_transform_flag || log2_trafo_size == 3)) {
                cbf_cb[1] = ff_hevc_cbf_cb_cr_decode(lc, trafo_depth);
            }
        }

        if (trafo_depth == 0 || cbf_cr[0]) {
            cbf_cr[0] = ff_hevc_cbf_cb_cr_decode(lc, trafo_depth);
            if (sps->chroma_format_idc == 2 && (!split_transform_flag || log2_trafo_size == 3)) {
                cbf_cr[1] = ff_hevc_cbf_cb_cr_decode(lc, trafo_depth);
            }
        }
    }

    if (split_transform_flag) {
        const int trafo_size_split = 1 << (log2_trafo_size - 1);
        const int x1 = x0 + trafo_size_split;
        const int y1 = y0 + trafo_size_split;

#define SUBDIVIDE(x, y, idx)                                                    \
do {                                                                            \
    ret = hls_transform_tree(lc, l, pps, sps,                                   \
                             x, y, x0, y0, cb_xBase, cb_yBase, log2_cb_size,    \
                             log2_trafo_size - 1, trafo_depth + 1, idx,         \
                             cbf_cb, cbf_cr);                                   \
    if (ret < 0)                                                                \
        return ret;                                                             \
} while (0)

        SUBDIVIDE(x0, y0, 0);
        SUBDIVIDE(x1, y0, 1);
        SUBDIVIDE(x0, y1, 2);
        SUBDIVIDE(x1, y1, 3);

#undef SUBDIVIDE
    } else {
        int min_tu_size      = 1 << sps->log2_min_tb_size;
        int log2_min_tu_size = sps->log2_min_tb_size;
        int min_tu_width     = sps->min_tb_width;
        int cbf_luma         = 1;

        if (lc->cu.pred_mode == MODE_INTRA || trafo_depth != 0 ||
            cbf_cb[0] || cbf_cr[0] ||
            (sps->chroma_format_idc == 2 && (cbf_cb[1] || cbf_cr[1]))) {
            cbf_luma = ff_hevc_cbf_luma_decode(lc, trafo_depth);
        }

        ret = hls_transform_unit(lc, l, pps, sps,
                                 x0, y0, xBase, yBase, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size,
                                 blk_idx, cbf_luma, cbf_cb, cbf_cr);
        if (ret < 0)
            return ret;
        // TODO: store cbf_luma somewhere else
        if (cbf_luma) {
            int i, j;
            for (i = 0; i < (1 << log2_trafo_size); i += min_tu_size)
                for (j = 0; j < (1 << log2_trafo_size); j += min_tu_size) {
                    int x_tu = (x0 + j) >> log2_min_tu_size;
                    int y_tu = (y0 + i) >> log2_min_tu_size;
                    l->cbf_luma[y_tu * min_tu_width + x_tu] = 1;
                }
        }
        if (!s->sh.disable_deblocking_filter_flag) {
            ff_hevc_deblocking_boundary_strengths(lc, l, pps, x0, y0, log2_trafo_size);
            if (pps->transquant_bypass_enable_flag &&
                lc->cu.cu_transquant_bypass_flag)
                set_deblocking_bypass(l->is_pcm, sps, x0, y0, log2_trafo_size);
        }
    }
    return 0;
}

static int hls_pcm_sample(HEVCLocalContext *lc, const HEVCLayerContext *l,
                          const HEVCPPS *pps, int x0, int y0, int log2_cb_size)
{
    const HEVCContext *const s = lc->parent;
    const HEVCSPS   *const sps = pps->sps;
    GetBitContext gb;
    int cb_size   = 1 << log2_cb_size;
    ptrdiff_t stride0 = s->cur_frame->f->linesize[0];
    ptrdiff_t stride1 = s->cur_frame->f->linesize[1];
    ptrdiff_t stride2 = s->cur_frame->f->linesize[2];
    uint8_t *dst0 = &s->cur_frame->f->data[0][y0 * stride0 + (x0 << sps->pixel_shift)];
    uint8_t *dst1 = &s->cur_frame->f->data[1][(y0 >> sps->vshift[1]) * stride1 + ((x0 >> sps->hshift[1]) << sps->pixel_shift)];
    uint8_t *dst2 = &s->cur_frame->f->data[2][(y0 >> sps->vshift[2]) * stride2 + ((x0 >> sps->hshift[2]) << sps->pixel_shift)];

    int length         = cb_size * cb_size * sps->pcm.bit_depth +
                         (((cb_size >> sps->hshift[1]) * (cb_size >> sps->vshift[1])) +
                          ((cb_size >> sps->hshift[2]) * (cb_size >> sps->vshift[2]))) *
                          sps->pcm.bit_depth_chroma;
    const uint8_t *pcm = skip_bytes(&lc->cc, (length + 7) >> 3);
    int ret;

    if (!s->sh.disable_deblocking_filter_flag)
        ff_hevc_deblocking_boundary_strengths(lc, l, pps, x0, y0, log2_cb_size);

    ret = init_get_bits(&gb, pcm, length);
    if (ret < 0)
        return ret;

    s->hevcdsp.put_pcm(dst0, stride0, cb_size, cb_size,     &gb, sps->pcm.bit_depth);
    if (sps->chroma_format_idc) {
        s->hevcdsp.put_pcm(dst1, stride1,
                           cb_size >> sps->hshift[1],
                           cb_size >> sps->vshift[1],
                           &gb, sps->pcm.bit_depth_chroma);
        s->hevcdsp.put_pcm(dst2, stride2,
                           cb_size >> sps->hshift[2],
                           cb_size >> sps->vshift[2],
                           &gb, sps->pcm.bit_depth_chroma);
    }

    return 0;
}

/**
 * 8.5.3.2.2.1 Luma sample unidirectional interpolation process
 *
 * @param s HEVC decoding context
 * @param dst target buffer for block data at block position
 * @param dststride stride of the dst buffer
 * @param ref reference picture buffer at origin (0, 0)
 * @param mv motion vector (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 * @param luma_weight weighting factor applied to the luma prediction
 * @param luma_offset additive offset applied to the luma prediction value
 */

static void luma_mc_uni(HEVCLocalContext *lc,
                        const HEVCPPS *pps, const HEVCSPS *sps,
                        uint8_t *dst, ptrdiff_t dststride,
                        const AVFrame *ref, const Mv *mv, int x_off, int y_off,
                        int block_w, int block_h, int luma_weight, int luma_offset)
{
    const HEVCContext *const s = lc->parent;
    const uint8_t *src   = ref->data[0];
    ptrdiff_t srcstride  = ref->linesize[0];
    int pic_width        = sps->width;
    int pic_height       = sps->height;
    int mx               = mv->x & 3;
    int my               = mv->y & 3;
    int weight_flag      = (s->sh.slice_type == HEVC_SLICE_P && pps->weighted_pred_flag) ||
                           (s->sh.slice_type == HEVC_SLICE_B && pps->weighted_bipred_flag);
    int idx              = hevc_pel_weight[block_w];

    x_off += mv->x >> 2;
    y_off += mv->y >> 2;
    src   += y_off * srcstride + (x_off * (1 << sps->pixel_shift));

    if (x_off < QPEL_EXTRA_BEFORE || y_off < QPEL_EXTRA_AFTER ||
        x_off >= pic_width - block_w - QPEL_EXTRA_AFTER ||
        y_off >= pic_height - block_h - QPEL_EXTRA_AFTER ||
        ref == s->cur_frame->f) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset     = QPEL_EXTRA_BEFORE * srcstride       + (QPEL_EXTRA_BEFORE << sps->pixel_shift);
        int buf_offset = QPEL_EXTRA_BEFORE * edge_emu_stride + (QPEL_EXTRA_BEFORE << sps->pixel_shift);

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src - offset,
                                 edge_emu_stride, srcstride,
                                 block_w + QPEL_EXTRA,
                                 block_h + QPEL_EXTRA,
                                 x_off - QPEL_EXTRA_BEFORE, y_off - QPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);
        src = lc->edge_emu_buffer + buf_offset;
        srcstride = edge_emu_stride;
    }

    if (!weight_flag)
        s->hevcdsp.put_hevc_qpel_uni[idx][!!my][!!mx](dst, dststride, src, srcstride,
                                                      block_h, mx, my, block_w);
    else
        s->hevcdsp.put_hevc_qpel_uni_w[idx][!!my][!!mx](dst, dststride, src, srcstride,
                                                        block_h, s->sh.luma_log2_weight_denom,
                                                        luma_weight, luma_offset, mx, my, block_w);
}

/**
 * 8.5.3.2.2.1 Luma sample bidirectional interpolation process
 *
 * @param s HEVC decoding context
 * @param dst target buffer for block data at block position
 * @param dststride stride of the dst buffer
 * @param ref0 reference picture0 buffer at origin (0, 0)
 * @param mv0 motion vector0 (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 * @param ref1 reference picture1 buffer at origin (0, 0)
 * @param mv1 motion vector1 (relative to block position) to get pixel data from
 * @param current_mv current motion vector structure
 */
static void luma_mc_bi(HEVCLocalContext *lc,
                       const HEVCPPS *pps, const HEVCSPS *sps,
                       uint8_t *dst, ptrdiff_t dststride,
                        const AVFrame *ref0, const Mv *mv0, int x_off, int y_off,
                        int block_w, int block_h, const AVFrame *ref1,
                        const Mv *mv1, struct MvField *current_mv)
{
    const HEVCContext *const s = lc->parent;
    ptrdiff_t src0stride  = ref0->linesize[0];
    ptrdiff_t src1stride  = ref1->linesize[0];
    int pic_width        = sps->width;
    int pic_height       = sps->height;
    int mx0              = mv0->x & 3;
    int my0              = mv0->y & 3;
    int mx1              = mv1->x & 3;
    int my1              = mv1->y & 3;
    int weight_flag      = (s->sh.slice_type == HEVC_SLICE_P && pps->weighted_pred_flag) ||
                           (s->sh.slice_type == HEVC_SLICE_B && pps->weighted_bipred_flag);
    int x_off0           = x_off + (mv0->x >> 2);
    int y_off0           = y_off + (mv0->y >> 2);
    int x_off1           = x_off + (mv1->x >> 2);
    int y_off1           = y_off + (mv1->y >> 2);
    int idx              = hevc_pel_weight[block_w];

    const uint8_t *src0  = ref0->data[0] + y_off0 * src0stride + (int)((unsigned)x_off0 << sps->pixel_shift);
    const uint8_t *src1  = ref1->data[0] + y_off1 * src1stride + (int)((unsigned)x_off1 << sps->pixel_shift);

    if (x_off0 < QPEL_EXTRA_BEFORE || y_off0 < QPEL_EXTRA_AFTER ||
        x_off0 >= pic_width - block_w - QPEL_EXTRA_AFTER ||
        y_off0 >= pic_height - block_h - QPEL_EXTRA_AFTER) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset     = QPEL_EXTRA_BEFORE * src0stride      + (QPEL_EXTRA_BEFORE << sps->pixel_shift);
        int buf_offset = QPEL_EXTRA_BEFORE * edge_emu_stride + (QPEL_EXTRA_BEFORE << sps->pixel_shift);

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src0 - offset,
                                 edge_emu_stride, src0stride,
                                 block_w + QPEL_EXTRA,
                                 block_h + QPEL_EXTRA,
                                 x_off0 - QPEL_EXTRA_BEFORE, y_off0 - QPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);
        src0 = lc->edge_emu_buffer + buf_offset;
        src0stride = edge_emu_stride;
    }

    if (x_off1 < QPEL_EXTRA_BEFORE || y_off1 < QPEL_EXTRA_AFTER ||
        x_off1 >= pic_width - block_w - QPEL_EXTRA_AFTER ||
        y_off1 >= pic_height - block_h - QPEL_EXTRA_AFTER) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset     = QPEL_EXTRA_BEFORE * src1stride      + (QPEL_EXTRA_BEFORE << sps->pixel_shift);
        int buf_offset = QPEL_EXTRA_BEFORE * edge_emu_stride + (QPEL_EXTRA_BEFORE << sps->pixel_shift);

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer2, src1 - offset,
                                 edge_emu_stride, src1stride,
                                 block_w + QPEL_EXTRA,
                                 block_h + QPEL_EXTRA,
                                 x_off1 - QPEL_EXTRA_BEFORE, y_off1 - QPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);
        src1 = lc->edge_emu_buffer2 + buf_offset;
        src1stride = edge_emu_stride;
    }

    s->hevcdsp.put_hevc_qpel[idx][!!my0][!!mx0](lc->tmp, src0, src0stride,
                                                block_h, mx0, my0, block_w);
    if (!weight_flag)
        s->hevcdsp.put_hevc_qpel_bi[idx][!!my1][!!mx1](dst, dststride, src1, src1stride, lc->tmp,
                                                       block_h, mx1, my1, block_w);
    else
        s->hevcdsp.put_hevc_qpel_bi_w[idx][!!my1][!!mx1](dst, dststride, src1, src1stride, lc->tmp,
                                                         block_h, s->sh.luma_log2_weight_denom,
                                                         s->sh.luma_weight_l0[current_mv->ref_idx[0]],
                                                         s->sh.luma_weight_l1[current_mv->ref_idx[1]],
                                                         s->sh.luma_offset_l0[current_mv->ref_idx[0]],
                                                         s->sh.luma_offset_l1[current_mv->ref_idx[1]],
                                                         mx1, my1, block_w);

}

/**
 * 8.5.3.2.2.2 Chroma sample uniprediction interpolation process
 *
 * @param s HEVC decoding context
 * @param dst1 target buffer for block data at block position (U plane)
 * @param dst2 target buffer for block data at block position (V plane)
 * @param dststride stride of the dst1 and dst2 buffers
 * @param ref reference picture buffer at origin (0, 0)
 * @param mv motion vector (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 * @param chroma_weight weighting factor applied to the chroma prediction
 * @param chroma_offset additive offset applied to the chroma prediction value
 */

static void chroma_mc_uni(HEVCLocalContext *lc,
                          const HEVCPPS *pps, const HEVCSPS *sps,
                          uint8_t *dst0,
                          ptrdiff_t dststride, const uint8_t *src0, ptrdiff_t srcstride, int reflist,
                          int x_off, int y_off, int block_w, int block_h,
                          const struct MvField *current_mv, int chroma_weight, int chroma_offset)
{
    const HEVCContext *const s = lc->parent;
    int pic_width        = sps->width >> sps->hshift[1];
    int pic_height       = sps->height >> sps->vshift[1];
    const Mv *mv         = &current_mv->mv[reflist];
    int weight_flag      = (s->sh.slice_type == HEVC_SLICE_P && pps->weighted_pred_flag) ||
                           (s->sh.slice_type == HEVC_SLICE_B && pps->weighted_bipred_flag);
    int idx              = hevc_pel_weight[block_w];
    int hshift           = sps->hshift[1];
    int vshift           = sps->vshift[1];
    intptr_t mx          = av_zero_extend(mv->x, 2 + hshift);
    intptr_t my          = av_zero_extend(mv->y, 2 + vshift);
    intptr_t _mx         = mx << (1 - hshift);
    intptr_t _my         = my << (1 - vshift);
    int emu              = src0 == s->cur_frame->f->data[1] || src0 == s->cur_frame->f->data[2];

    x_off += mv->x >> (2 + hshift);
    y_off += mv->y >> (2 + vshift);
    src0  += y_off * srcstride + (x_off * (1 << sps->pixel_shift));

    if (x_off < EPEL_EXTRA_BEFORE || y_off < EPEL_EXTRA_AFTER ||
        x_off >= pic_width - block_w - EPEL_EXTRA_AFTER ||
        y_off >= pic_height - block_h - EPEL_EXTRA_AFTER ||
        emu) {
        const int edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset0 = EPEL_EXTRA_BEFORE * (srcstride + (1 << sps->pixel_shift));
        int buf_offset0 = EPEL_EXTRA_BEFORE *
                          (edge_emu_stride + (1 << sps->pixel_shift));
        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src0 - offset0,
                                 edge_emu_stride, srcstride,
                                 block_w + EPEL_EXTRA, block_h + EPEL_EXTRA,
                                 x_off - EPEL_EXTRA_BEFORE,
                                 y_off - EPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);

        src0 = lc->edge_emu_buffer + buf_offset0;
        srcstride = edge_emu_stride;
    }
    if (!weight_flag)
        s->hevcdsp.put_hevc_epel_uni[idx][!!my][!!mx](dst0, dststride, src0, srcstride,
                                                  block_h, _mx, _my, block_w);
    else
        s->hevcdsp.put_hevc_epel_uni_w[idx][!!my][!!mx](dst0, dststride, src0, srcstride,
                                                        block_h, s->sh.chroma_log2_weight_denom,
                                                        chroma_weight, chroma_offset, _mx, _my, block_w);
}

/**
 * 8.5.3.2.2.2 Chroma sample bidirectional interpolation process
 *
 * @param s HEVC decoding context
 * @param dst target buffer for block data at block position
 * @param dststride stride of the dst buffer
 * @param ref0 reference picture0 buffer at origin (0, 0)
 * @param mv0 motion vector0 (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 * @param ref1 reference picture1 buffer at origin (0, 0)
 * @param mv1 motion vector1 (relative to block position) to get pixel data from
 * @param current_mv current motion vector structure
 * @param cidx chroma component(cb, cr)
 */
static void chroma_mc_bi(HEVCLocalContext *lc,
                         const HEVCPPS *pps, const HEVCSPS *sps,
                         uint8_t *dst0, ptrdiff_t dststride,
                         const AVFrame *ref0, const AVFrame *ref1,
                         int x_off, int y_off, int block_w, int block_h, const MvField *current_mv, int cidx)
{
    const HEVCContext *const s = lc->parent;
    const uint8_t *src1  = ref0->data[cidx+1];
    const uint8_t *src2  = ref1->data[cidx+1];
    ptrdiff_t src1stride = ref0->linesize[cidx+1];
    ptrdiff_t src2stride = ref1->linesize[cidx+1];
    int weight_flag      = (s->sh.slice_type == HEVC_SLICE_P && pps->weighted_pred_flag) ||
                           (s->sh.slice_type == HEVC_SLICE_B && pps->weighted_bipred_flag);
    int pic_width        = sps->width >> sps->hshift[1];
    int pic_height       = sps->height >> sps->vshift[1];
    const Mv *const mv0  = &current_mv->mv[0];
    const Mv *const mv1  = &current_mv->mv[1];
    int hshift = sps->hshift[1];
    int vshift = sps->vshift[1];

    intptr_t mx0 = av_zero_extend(mv0->x, 2 + hshift);
    intptr_t my0 = av_zero_extend(mv0->y, 2 + vshift);
    intptr_t mx1 = av_zero_extend(mv1->x, 2 + hshift);
    intptr_t my1 = av_zero_extend(mv1->y, 2 + vshift);
    intptr_t _mx0 = mx0 << (1 - hshift);
    intptr_t _my0 = my0 << (1 - vshift);
    intptr_t _mx1 = mx1 << (1 - hshift);
    intptr_t _my1 = my1 << (1 - vshift);

    int x_off0 = x_off + (mv0->x >> (2 + hshift));
    int y_off0 = y_off + (mv0->y >> (2 + vshift));
    int x_off1 = x_off + (mv1->x >> (2 + hshift));
    int y_off1 = y_off + (mv1->y >> (2 + vshift));
    int idx = hevc_pel_weight[block_w];
    src1  += y_off0 * src1stride + (int)((unsigned)x_off0 << sps->pixel_shift);
    src2  += y_off1 * src2stride + (int)((unsigned)x_off1 << sps->pixel_shift);

    if (x_off0 < EPEL_EXTRA_BEFORE || y_off0 < EPEL_EXTRA_AFTER ||
        x_off0 >= pic_width - block_w - EPEL_EXTRA_AFTER ||
        y_off0 >= pic_height - block_h - EPEL_EXTRA_AFTER) {
        const int edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset1 = EPEL_EXTRA_BEFORE * (src1stride + (1 << sps->pixel_shift));
        int buf_offset1 = EPEL_EXTRA_BEFORE *
                          (edge_emu_stride + (1 << sps->pixel_shift));

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src1 - offset1,
                                 edge_emu_stride, src1stride,
                                 block_w + EPEL_EXTRA, block_h + EPEL_EXTRA,
                                 x_off0 - EPEL_EXTRA_BEFORE,
                                 y_off0 - EPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);

        src1 = lc->edge_emu_buffer + buf_offset1;
        src1stride = edge_emu_stride;
    }

    if (x_off1 < EPEL_EXTRA_BEFORE || y_off1 < EPEL_EXTRA_AFTER ||
        x_off1 >= pic_width - block_w - EPEL_EXTRA_AFTER ||
        y_off1 >= pic_height - block_h - EPEL_EXTRA_AFTER) {
        const int edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << sps->pixel_shift;
        int offset1 = EPEL_EXTRA_BEFORE * (src2stride + (1 << sps->pixel_shift));
        int buf_offset1 = EPEL_EXTRA_BEFORE *
                          (edge_emu_stride + (1 << sps->pixel_shift));

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer2, src2 - offset1,
                                 edge_emu_stride, src2stride,
                                 block_w + EPEL_EXTRA, block_h + EPEL_EXTRA,
                                 x_off1 - EPEL_EXTRA_BEFORE,
                                 y_off1 - EPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);

        src2 = lc->edge_emu_buffer2 + buf_offset1;
        src2stride = edge_emu_stride;
    }

    s->hevcdsp.put_hevc_epel[idx][!!my0][!!mx0](lc->tmp, src1, src1stride,
                                                block_h, _mx0, _my0, block_w);
    if (!weight_flag)
        s->hevcdsp.put_hevc_epel_bi[idx][!!my1][!!mx1](dst0, s->cur_frame->f->linesize[cidx+1],
                                                       src2, src2stride, lc->tmp,
                                                       block_h, _mx1, _my1, block_w);
    else
        s->hevcdsp.put_hevc_epel_bi_w[idx][!!my1][!!mx1](dst0, s->cur_frame->f->linesize[cidx+1],
                                                         src2, src2stride, lc->tmp,
                                                         block_h,
                                                         s->sh.chroma_log2_weight_denom,
                                                         s->sh.chroma_weight_l0[current_mv->ref_idx[0]][cidx],
                                                         s->sh.chroma_weight_l1[current_mv->ref_idx[1]][cidx],
                                                         s->sh.chroma_offset_l0[current_mv->ref_idx[0]][cidx],
                                                         s->sh.chroma_offset_l1[current_mv->ref_idx[1]][cidx],
                                                         _mx1, _my1, block_w);
}

static void hevc_await_progress(const HEVCContext *s, const HEVCFrame *ref,
                                const Mv *mv, int y0, int height)
{
    if (s->avctx->active_thread_type == FF_THREAD_FRAME ) {
        int y = FFMAX(0, (mv->y >> 2) + y0 + height + 9);

        ff_progress_frame_await(&ref->tf, y);
    }
}

static void hevc_luma_mv_mvp_mode(HEVCLocalContext *lc,
                                  const HEVCPPS *pps, const HEVCSPS *sps,
                                  int x0, int y0, int nPbW,
                                  int nPbH, int log2_cb_size, int part_idx,
                                  int merge_idx, MvField *mv)
{
    const HEVCContext *const s = lc->parent;
    enum InterPredIdc inter_pred_idc = PRED_L0;
    int mvp_flag;

    ff_hevc_set_neighbour_available(lc, x0, y0, nPbW, nPbH, sps->log2_ctb_size);
    mv->pred_flag = 0;
    if (s->sh.slice_type == HEVC_SLICE_B)
        inter_pred_idc = ff_hevc_inter_pred_idc_decode(lc, nPbW, nPbH);

    if (inter_pred_idc != PRED_L1) {
        if (s->sh.nb_refs[L0])
            mv->ref_idx[0]= ff_hevc_ref_idx_lx_decode(lc, s->sh.nb_refs[L0]);

        mv->pred_flag = PF_L0;
        ff_hevc_hls_mvd_coding(lc, x0, y0, 0);
        mvp_flag = ff_hevc_mvp_lx_flag_decode(lc);
        ff_hevc_luma_mv_mvp_mode(lc, pps, x0, y0, nPbW, nPbH, log2_cb_size,
                                 part_idx, merge_idx, mv, mvp_flag, 0);
        mv->mv[0].x += lc->pu.mvd.x;
        mv->mv[0].y += lc->pu.mvd.y;
    }

    if (inter_pred_idc != PRED_L0) {
        if (s->sh.nb_refs[L1])
            mv->ref_idx[1]= ff_hevc_ref_idx_lx_decode(lc, s->sh.nb_refs[L1]);

        if (s->sh.mvd_l1_zero_flag == 1 && inter_pred_idc == PRED_BI) {
            AV_ZERO32(&lc->pu.mvd);
        } else {
            ff_hevc_hls_mvd_coding(lc, x0, y0, 1);
        }

        mv->pred_flag += PF_L1;
        mvp_flag = ff_hevc_mvp_lx_flag_decode(lc);
        ff_hevc_luma_mv_mvp_mode(lc, pps, x0, y0, nPbW, nPbH, log2_cb_size,
                                 part_idx, merge_idx, mv, mvp_flag, 1);
        mv->mv[1].x += lc->pu.mvd.x;
        mv->mv[1].y += lc->pu.mvd.y;
    }
}

static void hls_prediction_unit(HEVCLocalContext *lc,
                                const HEVCLayerContext *l,
                                const HEVCPPS *pps, const HEVCSPS *sps,
                                int x0, int y0, int nPbW, int nPbH,
                                int log2_cb_size, int partIdx, int idx)
{
#define POS(c_idx, x, y)                                                              \
    &s->cur_frame->f->data[c_idx][((y) >> sps->vshift[c_idx]) * linesize[c_idx] + \
                           (((x) >> sps->hshift[c_idx]) << sps->pixel_shift)]
    const HEVCContext *const s = lc->parent;
    int merge_idx = 0;
    struct MvField current_mv = {{{ 0 }}};

    int min_pu_width = sps->min_pu_width;

    MvField *tab_mvf = s->cur_frame->tab_mvf;
    const RefPicList *refPicList = s->cur_frame->refPicList;
    const HEVCFrame *ref0 = NULL, *ref1 = NULL;
    const int *linesize = s->cur_frame->f->linesize;
    uint8_t *dst0 = POS(0, x0, y0);
    uint8_t *dst1 = POS(1, x0, y0);
    uint8_t *dst2 = POS(2, x0, y0);
    int log2_min_cb_size = sps->log2_min_cb_size;
    int min_cb_width     = sps->min_cb_width;
    int x_cb             = x0 >> log2_min_cb_size;
    int y_cb             = y0 >> log2_min_cb_size;
    int x_pu, y_pu;
    int i, j;

    int skip_flag = SAMPLE_CTB(l->skip_flag, x_cb, y_cb);

    if (!skip_flag)
        lc->pu.merge_flag = ff_hevc_merge_flag_decode(lc);

    if (skip_flag || lc->pu.merge_flag) {
        if (s->sh.max_num_merge_cand > 1)
            merge_idx = ff_hevc_merge_idx_decode(lc);
        else
            merge_idx = 0;

        ff_hevc_luma_mv_merge_mode(lc, pps, x0, y0, nPbW, nPbH, log2_cb_size,
                                   partIdx, merge_idx, &current_mv);
    } else {
        hevc_luma_mv_mvp_mode(lc, pps, sps, x0, y0, nPbW, nPbH, log2_cb_size,
                              partIdx, merge_idx, &current_mv);
    }

    x_pu = x0 >> sps->log2_min_pu_size;
    y_pu = y0 >> sps->log2_min_pu_size;

    for (j = 0; j < nPbH >> sps->log2_min_pu_size; j++)
        for (i = 0; i < nPbW >> sps->log2_min_pu_size; i++)
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i] = current_mv;

    if (current_mv.pred_flag & PF_L0) {
        ref0 = refPicList[0].ref[current_mv.ref_idx[0]];
        if (!ref0 || !ref0->f)
            return;
        hevc_await_progress(s, ref0, &current_mv.mv[0], y0, nPbH);
    }
    if (current_mv.pred_flag & PF_L1) {
        ref1 = refPicList[1].ref[current_mv.ref_idx[1]];
        if (!ref1 || !ref1->f)
            return;
        hevc_await_progress(s, ref1, &current_mv.mv[1], y0, nPbH);
    }

    if (current_mv.pred_flag == PF_L0) {
        int x0_c = x0 >> sps->hshift[1];
        int y0_c = y0 >> sps->vshift[1];
        int nPbW_c = nPbW >> sps->hshift[1];
        int nPbH_c = nPbH >> sps->vshift[1];

        luma_mc_uni(lc, pps, sps, dst0, linesize[0], ref0->f,
                    &current_mv.mv[0], x0, y0, nPbW, nPbH,
                    s->sh.luma_weight_l0[current_mv.ref_idx[0]],
                    s->sh.luma_offset_l0[current_mv.ref_idx[0]]);

        if (sps->chroma_format_idc) {
            chroma_mc_uni(lc, pps, sps, dst1, linesize[1], ref0->f->data[1], ref0->f->linesize[1],
                          0, x0_c, y0_c, nPbW_c, nPbH_c, &current_mv,
                          s->sh.chroma_weight_l0[current_mv.ref_idx[0]][0], s->sh.chroma_offset_l0[current_mv.ref_idx[0]][0]);
            chroma_mc_uni(lc, pps, sps, dst2, linesize[2], ref0->f->data[2], ref0->f->linesize[2],
                          0, x0_c, y0_c, nPbW_c, nPbH_c, &current_mv,
                          s->sh.chroma_weight_l0[current_mv.ref_idx[0]][1], s->sh.chroma_offset_l0[current_mv.ref_idx[0]][1]);
        }
    } else if (current_mv.pred_flag == PF_L1) {
        int x0_c = x0 >> sps->hshift[1];
        int y0_c = y0 >> sps->vshift[1];
        int nPbW_c = nPbW >> sps->hshift[1];
        int nPbH_c = nPbH >> sps->vshift[1];

        luma_mc_uni(lc, pps, sps, dst0, linesize[0], ref1->f,
                    &current_mv.mv[1], x0, y0, nPbW, nPbH,
                    s->sh.luma_weight_l1[current_mv.ref_idx[1]],
                    s->sh.luma_offset_l1[current_mv.ref_idx[1]]);

        if (sps->chroma_format_idc) {
            chroma_mc_uni(lc, pps, sps, dst1, linesize[1], ref1->f->data[1], ref1->f->linesize[1],
                          1, x0_c, y0_c, nPbW_c, nPbH_c, &current_mv,
                          s->sh.chroma_weight_l1[current_mv.ref_idx[1]][0], s->sh.chroma_offset_l1[current_mv.ref_idx[1]][0]);

            chroma_mc_uni(lc, pps, sps, dst2, linesize[2], ref1->f->data[2], ref1->f->linesize[2],
                          1, x0_c, y0_c, nPbW_c, nPbH_c, &current_mv,
                          s->sh.chroma_weight_l1[current_mv.ref_idx[1]][1], s->sh.chroma_offset_l1[current_mv.ref_idx[1]][1]);
        }
    } else if (current_mv.pred_flag == PF_BI) {
        int x0_c = x0 >> sps->hshift[1];
        int y0_c = y0 >> sps->vshift[1];
        int nPbW_c = nPbW >> sps->hshift[1];
        int nPbH_c = nPbH >> sps->vshift[1];

        luma_mc_bi(lc, pps, sps, dst0, linesize[0], ref0->f,
                   &current_mv.mv[0], x0, y0, nPbW, nPbH,
                   ref1->f, &current_mv.mv[1], &current_mv);

        if (sps->chroma_format_idc) {
            chroma_mc_bi(lc, pps, sps, dst1, linesize[1], ref0->f, ref1->f,
                         x0_c, y0_c, nPbW_c, nPbH_c, &current_mv, 0);

            chroma_mc_bi(lc, pps, sps, dst2, linesize[2], ref0->f, ref1->f,
                         x0_c, y0_c, nPbW_c, nPbH_c, &current_mv, 1);
        }
    }
}

/**
 * 8.4.1
 */
static int luma_intra_pred_mode(HEVCLocalContext *lc, const HEVCLayerContext *l,
                                const HEVCSPS *sps,
                                int x0, int y0, int pu_size,
                                int prev_intra_luma_pred_flag)
{
    const HEVCContext *const s = lc->parent;
    int x_pu             = x0 >> sps->log2_min_pu_size;
    int y_pu             = y0 >> sps->log2_min_pu_size;
    int min_pu_width     = sps->min_pu_width;
    int size_in_pus      = pu_size >> sps->log2_min_pu_size;
    int x0b              = av_zero_extend(x0, sps->log2_ctb_size);
    int y0b              = av_zero_extend(y0, sps->log2_ctb_size);

    int cand_up   = (lc->ctb_up_flag || y0b) ?
                    l->tab_ipm[(y_pu - 1) * min_pu_width + x_pu] : INTRA_DC;
    int cand_left = (lc->ctb_left_flag || x0b) ?
                    l->tab_ipm[y_pu * min_pu_width + x_pu - 1]   : INTRA_DC;

    int y_ctb = (y0 >> (sps->log2_ctb_size)) << (sps->log2_ctb_size);

    MvField *tab_mvf = s->cur_frame->tab_mvf;
    int intra_pred_mode;
    int candidate[3];
    int i, j;

    // intra_pred_mode prediction does not cross vertical CTB boundaries
    if ((y0 - 1) < y_ctb)
        cand_up = INTRA_DC;

    if (cand_left == cand_up) {
        if (cand_left < 2) {
            candidate[0] = INTRA_PLANAR;
            candidate[1] = INTRA_DC;
            candidate[2] = INTRA_ANGULAR_26;
        } else {
            candidate[0] = cand_left;
            candidate[1] = 2 + ((cand_left - 2 - 1 + 32) & 31);
            candidate[2] = 2 + ((cand_left - 2 + 1) & 31);
        }
    } else {
        candidate[0] = cand_left;
        candidate[1] = cand_up;
        if (candidate[0] != INTRA_PLANAR && candidate[1] != INTRA_PLANAR) {
            candidate[2] = INTRA_PLANAR;
        } else if (candidate[0] != INTRA_DC && candidate[1] != INTRA_DC) {
            candidate[2] = INTRA_DC;
        } else {
            candidate[2] = INTRA_ANGULAR_26;
        }
    }

    if (prev_intra_luma_pred_flag) {
        intra_pred_mode = candidate[lc->pu.mpm_idx];
    } else {
        if (candidate[0] > candidate[1])
            FFSWAP(uint8_t, candidate[0], candidate[1]);
        if (candidate[0] > candidate[2])
            FFSWAP(uint8_t, candidate[0], candidate[2]);
        if (candidate[1] > candidate[2])
            FFSWAP(uint8_t, candidate[1], candidate[2]);

        intra_pred_mode = lc->pu.rem_intra_luma_pred_mode;
        for (i = 0; i < 3; i++)
            if (intra_pred_mode >= candidate[i])
                intra_pred_mode++;
    }

    /* write the intra prediction units into the mv array */
    if (!size_in_pus)
        size_in_pus = 1;
    for (i = 0; i < size_in_pus; i++) {
        memset(&l->tab_ipm[(y_pu + i) * min_pu_width + x_pu],
               intra_pred_mode, size_in_pus);

        for (j = 0; j < size_in_pus; j++) {
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].pred_flag = PF_INTRA;
        }
    }

    return intra_pred_mode;
}

static av_always_inline void set_ct_depth(const HEVCSPS *sps, uint8_t *tab_ct_depth,
                                          int x0, int y0,
                                          int log2_cb_size, int ct_depth)
{
    int length = (1 << log2_cb_size) >> sps->log2_min_cb_size;
    int x_cb   = x0 >> sps->log2_min_cb_size;
    int y_cb   = y0 >> sps->log2_min_cb_size;
    int y;

    for (y = 0; y < length; y++)
        memset(&tab_ct_depth[(y_cb + y) * sps->min_cb_width + x_cb],
               ct_depth, length);
}

static const uint8_t tab_mode_idx[] = {
     0,  1,  2,  2,  2,  2,  3,  5,  7,  8, 10, 12, 13, 15, 17, 18, 19, 20,
    21, 22, 23, 23, 24, 24, 25, 25, 26, 27, 27, 28, 28, 29, 29, 30, 31};

static void intra_prediction_unit(HEVCLocalContext *lc,
                                  const HEVCLayerContext *l, const HEVCSPS *sps,
                                  int x0, int y0,
                                  int log2_cb_size)
{
    static const uint8_t intra_chroma_table[4] = { 0, 26, 10, 1 };
    uint8_t prev_intra_luma_pred_flag[4];
    int split   = lc->cu.part_mode == PART_NxN;
    int pb_size = (1 << log2_cb_size) >> split;
    int side    = split + 1;
    int chroma_mode;
    int i, j;

    for (i = 0; i < side; i++)
        for (j = 0; j < side; j++)
            prev_intra_luma_pred_flag[2 * i + j] = ff_hevc_prev_intra_luma_pred_flag_decode(lc);

    for (i = 0; i < side; i++) {
        for (j = 0; j < side; j++) {
            if (prev_intra_luma_pred_flag[2 * i + j])
                lc->pu.mpm_idx = ff_hevc_mpm_idx_decode(lc);
            else
                lc->pu.rem_intra_luma_pred_mode = ff_hevc_rem_intra_luma_pred_mode_decode(lc);

            lc->pu.intra_pred_mode[2 * i + j] =
                luma_intra_pred_mode(lc, l, sps,
                                     x0 + pb_size * j, y0 + pb_size * i, pb_size,
                                     prev_intra_luma_pred_flag[2 * i + j]);
        }
    }

    if (sps->chroma_format_idc == 3) {
        for (i = 0; i < side; i++) {
            for (j = 0; j < side; j++) {
                lc->pu.chroma_mode_c[2 * i + j] = chroma_mode = ff_hevc_intra_chroma_pred_mode_decode(lc);
                if (chroma_mode != 4) {
                    if (lc->pu.intra_pred_mode[2 * i + j] == intra_chroma_table[chroma_mode])
                        lc->pu.intra_pred_mode_c[2 * i + j] = 34;
                    else
                        lc->pu.intra_pred_mode_c[2 * i + j] = intra_chroma_table[chroma_mode];
                } else {
                    lc->pu.intra_pred_mode_c[2 * i + j] = lc->pu.intra_pred_mode[2 * i + j];
                }
            }
        }
    } else if (sps->chroma_format_idc == 2) {
        int mode_idx;
        lc->pu.chroma_mode_c[0] = chroma_mode = ff_hevc_intra_chroma_pred_mode_decode(lc);
        if (chroma_mode != 4) {
            if (lc->pu.intra_pred_mode[0] == intra_chroma_table[chroma_mode])
                mode_idx = 34;
            else
                mode_idx = intra_chroma_table[chroma_mode];
        } else {
            mode_idx = lc->pu.intra_pred_mode[0];
        }
        lc->pu.intra_pred_mode_c[0] = tab_mode_idx[mode_idx];
    } else if (sps->chroma_format_idc != 0) {
        chroma_mode = ff_hevc_intra_chroma_pred_mode_decode(lc);
        if (chroma_mode != 4) {
            if (lc->pu.intra_pred_mode[0] == intra_chroma_table[chroma_mode])
                lc->pu.intra_pred_mode_c[0] = 34;
            else
                lc->pu.intra_pred_mode_c[0] = intra_chroma_table[chroma_mode];
        } else {
            lc->pu.intra_pred_mode_c[0] = lc->pu.intra_pred_mode[0];
        }
    }
}

static void intra_prediction_unit_default_value(HEVCLocalContext *lc,
                                                const HEVCLayerContext *l,
                                                const HEVCSPS *sps,
                                                int x0, int y0,
                                                int log2_cb_size)
{
    const HEVCContext *const s = lc->parent;
    int pb_size          = 1 << log2_cb_size;
    int size_in_pus      = pb_size >> sps->log2_min_pu_size;
    int min_pu_width     = sps->min_pu_width;
    MvField *tab_mvf     = s->cur_frame->tab_mvf;
    int x_pu             = x0 >> sps->log2_min_pu_size;
    int y_pu             = y0 >> sps->log2_min_pu_size;
    int j, k;

    if (size_in_pus == 0)
        size_in_pus = 1;
    for (j = 0; j < size_in_pus; j++)
        memset(&l->tab_ipm[(y_pu + j) * min_pu_width + x_pu], INTRA_DC, size_in_pus);
    if (lc->cu.pred_mode == MODE_INTRA)
        for (j = 0; j < size_in_pus; j++)
            for (k = 0; k < size_in_pus; k++)
                tab_mvf[(y_pu + j) * min_pu_width + x_pu + k].pred_flag = PF_INTRA;
}

static int hls_coding_unit(HEVCLocalContext *lc, const HEVCContext *s,
                           const HEVCLayerContext *l,
                           const HEVCPPS *pps, const HEVCSPS *sps,
                           int x0, int y0, int log2_cb_size)
{
    int cb_size          = 1 << log2_cb_size;
    int log2_min_cb_size = sps->log2_min_cb_size;
    int length           = cb_size >> log2_min_cb_size;
    int min_cb_width     = sps->min_cb_width;
    int x_cb             = x0 >> log2_min_cb_size;
    int y_cb             = y0 >> log2_min_cb_size;
    int idx              = log2_cb_size - 2;
    int qp_block_mask    = (1 << (sps->log2_ctb_size - pps->diff_cu_qp_delta_depth)) - 1;
    int x, y, ret;

    lc->cu.x                = x0;
    lc->cu.y                = y0;
    lc->cu.pred_mode        = MODE_INTRA;
    lc->cu.part_mode        = PART_2Nx2N;
    lc->cu.intra_split_flag = 0;

    SAMPLE_CTB(l->skip_flag, x_cb, y_cb) = 0;
    for (x = 0; x < 4; x++)
        lc->pu.intra_pred_mode[x] = 1;
    if (pps->transquant_bypass_enable_flag) {
        lc->cu.cu_transquant_bypass_flag = ff_hevc_cu_transquant_bypass_flag_decode(lc);
        if (lc->cu.cu_transquant_bypass_flag)
            set_deblocking_bypass(l->is_pcm, sps, x0, y0, log2_cb_size);
    } else
        lc->cu.cu_transquant_bypass_flag = 0;

    if (s->sh.slice_type != HEVC_SLICE_I) {
        const int x0b = av_zero_extend(x0, sps->log2_ctb_size);
        const int y0b = av_zero_extend(y0, sps->log2_ctb_size);
        uint8_t skip_flag = ff_hevc_skip_flag_decode(lc, l->skip_flag,
                                                     x0b, y0b, x_cb, y_cb,
                                                     min_cb_width);

        x = y_cb * min_cb_width + x_cb;
        for (y = 0; y < length; y++) {
            memset(&l->skip_flag[x], skip_flag, length);
            x += min_cb_width;
        }
        lc->cu.pred_mode = skip_flag ? MODE_SKIP : MODE_INTER;
    } else {
        x = y_cb * min_cb_width + x_cb;
        for (y = 0; y < length; y++) {
            memset(&l->skip_flag[x], 0, length);
            x += min_cb_width;
        }
    }

    if (SAMPLE_CTB(l->skip_flag, x_cb, y_cb)) {
        hls_prediction_unit(lc, l, pps, sps,
                            x0, y0, cb_size, cb_size, log2_cb_size, 0, idx);
        intra_prediction_unit_default_value(lc, l, sps, x0, y0, log2_cb_size);

        if (!s->sh.disable_deblocking_filter_flag)
            ff_hevc_deblocking_boundary_strengths(lc, l, pps, x0, y0, log2_cb_size);
    } else {
        int pcm_flag = 0;

        if (s->sh.slice_type != HEVC_SLICE_I)
            lc->cu.pred_mode = ff_hevc_pred_mode_decode(lc);
        if (lc->cu.pred_mode != MODE_INTRA ||
            log2_cb_size == sps->log2_min_cb_size) {
            lc->cu.part_mode        = ff_hevc_part_mode_decode(lc, sps, log2_cb_size);
            lc->cu.intra_split_flag = lc->cu.part_mode == PART_NxN &&
                                      lc->cu.pred_mode == MODE_INTRA;
        }

        if (lc->cu.pred_mode == MODE_INTRA) {
            if (lc->cu.part_mode == PART_2Nx2N && sps->pcm_enabled &&
                log2_cb_size >= sps->pcm.log2_min_pcm_cb_size &&
                log2_cb_size <= sps->pcm.log2_max_pcm_cb_size) {
                pcm_flag = ff_hevc_pcm_flag_decode(lc);
            }
            if (pcm_flag) {
                intra_prediction_unit_default_value(lc, l, sps, x0, y0, log2_cb_size);
                ret = hls_pcm_sample(lc, l, pps, x0, y0, log2_cb_size);
                if (sps->pcm_loop_filter_disabled)
                    set_deblocking_bypass(l->is_pcm, sps, x0, y0, log2_cb_size);

                if (ret < 0)
                    return ret;
            } else {
                intra_prediction_unit(lc, l, sps, x0, y0, log2_cb_size);
            }
        } else {
            intra_prediction_unit_default_value(lc, l, sps, x0, y0, log2_cb_size);
            switch (lc->cu.part_mode) {
            case PART_2Nx2N:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0, cb_size, cb_size, log2_cb_size, 0, idx);
                break;
            case PART_2NxN:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0,               cb_size, cb_size / 2, log2_cb_size, 0, idx);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0 + cb_size / 2, cb_size, cb_size / 2, log2_cb_size, 1, idx);
                break;
            case PART_Nx2N:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0,               y0, cb_size / 2, cb_size, log2_cb_size, 0, idx - 1);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0 + cb_size / 2, y0, cb_size / 2, cb_size, log2_cb_size, 1, idx - 1);
                break;
            case PART_2NxnU:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0,               cb_size, cb_size     / 4, log2_cb_size, 0, idx);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0 + cb_size / 4, cb_size, cb_size * 3 / 4, log2_cb_size, 1, idx);
                break;
            case PART_2NxnD:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0,                   cb_size, cb_size * 3 / 4, log2_cb_size, 0, idx);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0, y0 + cb_size * 3 / 4, cb_size, cb_size     / 4, log2_cb_size, 1, idx);
                break;
            case PART_nLx2N:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0,               y0, cb_size     / 4, cb_size, log2_cb_size, 0, idx - 2);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0 + cb_size / 4, y0, cb_size * 3 / 4, cb_size, log2_cb_size, 1, idx - 2);
                break;
            case PART_nRx2N:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0,                   y0, cb_size * 3 / 4, cb_size, log2_cb_size, 0, idx - 2);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0 + cb_size * 3 / 4, y0, cb_size     / 4, cb_size, log2_cb_size, 1, idx - 2);
                break;
            case PART_NxN:
                hls_prediction_unit(lc, l, pps, sps,
                                    x0,               y0,               cb_size / 2, cb_size / 2, log2_cb_size, 0, idx - 1);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0 + cb_size / 2, y0,               cb_size / 2, cb_size / 2, log2_cb_size, 1, idx - 1);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0,               y0 + cb_size / 2, cb_size / 2, cb_size / 2, log2_cb_size, 2, idx - 1);
                hls_prediction_unit(lc, l, pps, sps,
                                    x0 + cb_size / 2, y0 + cb_size / 2, cb_size / 2, cb_size / 2, log2_cb_size, 3, idx - 1);
                break;
            }
        }

        if (!pcm_flag) {
            int rqt_root_cbf = 1;

            if (lc->cu.pred_mode != MODE_INTRA &&
                !(lc->cu.part_mode == PART_2Nx2N && lc->pu.merge_flag)) {
                rqt_root_cbf = ff_hevc_no_residual_syntax_flag_decode(lc);
            }
            if (rqt_root_cbf) {
                const static int cbf[2] = { 0 };
                lc->cu.max_trafo_depth = lc->cu.pred_mode == MODE_INTRA ?
                                         sps->max_transform_hierarchy_depth_intra + lc->cu.intra_split_flag :
                                         sps->max_transform_hierarchy_depth_inter;
                ret = hls_transform_tree(lc, l, pps, sps, x0, y0, x0, y0, x0, y0,
                                         log2_cb_size,
                                         log2_cb_size, 0, 0, cbf, cbf);
                if (ret < 0)
                    return ret;
            } else {
                if (!s->sh.disable_deblocking_filter_flag)
                    ff_hevc_deblocking_boundary_strengths(lc, l, pps, x0, y0, log2_cb_size);
            }
        }
    }

    if (pps->cu_qp_delta_enabled_flag && lc->tu.is_cu_qp_delta_coded == 0)
        ff_hevc_set_qPy(lc, l, pps, x0, y0, log2_cb_size);

    x = y_cb * min_cb_width + x_cb;
    for (y = 0; y < length; y++) {
        memset(&l->qp_y_tab[x], lc->qp_y, length);
        x += min_cb_width;
    }

    if(((x0 + (1<<log2_cb_size)) & qp_block_mask) == 0 &&
       ((y0 + (1<<log2_cb_size)) & qp_block_mask) == 0) {
        lc->qPy_pred = lc->qp_y;
    }

    set_ct_depth(sps, l->tab_ct_depth, x0, y0, log2_cb_size, lc->ct_depth);

    return 0;
}

static int hls_coding_quadtree(HEVCLocalContext *lc,
                               const HEVCLayerContext *l,
                               const HEVCPPS *pps, const HEVCSPS *sps,
                               int x0, int y0,
                               int log2_cb_size, int cb_depth)
{
    const HEVCContext *const s = lc->parent;
    const int cb_size    = 1 << log2_cb_size;
    int ret;
    int split_cu;

    lc->ct_depth = cb_depth;
    if (x0 + cb_size <= sps->width  &&
        y0 + cb_size <= sps->height &&
        log2_cb_size > sps->log2_min_cb_size) {
        split_cu = ff_hevc_split_coding_unit_flag_decode(lc, l->tab_ct_depth,
                                                         sps, cb_depth, x0, y0);
    } else {
        split_cu = (log2_cb_size > sps->log2_min_cb_size);
    }
    if (pps->cu_qp_delta_enabled_flag &&
        log2_cb_size >= sps->log2_ctb_size - pps->diff_cu_qp_delta_depth) {
        lc->tu.is_cu_qp_delta_coded = 0;
        lc->tu.cu_qp_delta          = 0;
    }

    if (s->sh.cu_chroma_qp_offset_enabled_flag &&
        log2_cb_size >= sps->log2_ctb_size - pps->diff_cu_chroma_qp_offset_depth) {
        lc->tu.is_cu_chroma_qp_offset_coded = 0;
    }

    if (split_cu) {
        int qp_block_mask = (1 << (sps->log2_ctb_size - pps->diff_cu_qp_delta_depth)) - 1;
        const int cb_size_split = cb_size >> 1;
        const int x1 = x0 + cb_size_split;
        const int y1 = y0 + cb_size_split;

        int more_data = 0;

        more_data = hls_coding_quadtree(lc, l, pps, sps,
                                        x0, y0, log2_cb_size - 1, cb_depth + 1);
        if (more_data < 0)
            return more_data;

        if (more_data && x1 < sps->width) {
            more_data = hls_coding_quadtree(lc, l, pps, sps,
                                            x1, y0, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }
        if (more_data && y1 < sps->height) {
            more_data = hls_coding_quadtree(lc, l, pps, sps,
                                            x0, y1, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }
        if (more_data && x1 < sps->width &&
            y1 < sps->height) {
            more_data = hls_coding_quadtree(lc, l, pps, sps,
                                            x1, y1, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }

        if(((x0 + (1<<log2_cb_size)) & qp_block_mask) == 0 &&
            ((y0 + (1<<log2_cb_size)) & qp_block_mask) == 0)
            lc->qPy_pred = lc->qp_y;

        if (more_data)
            return ((x1 + cb_size_split) < sps->width ||
                    (y1 + cb_size_split) < sps->height);
        else
            return 0;
    } else {
        ret = hls_coding_unit(lc, s, l, pps, sps, x0, y0, log2_cb_size);
        if (ret < 0)
            return ret;
        if ((!((x0 + cb_size) %
               (1 << (sps->log2_ctb_size))) ||
             (x0 + cb_size >= sps->width)) &&
            (!((y0 + cb_size) %
               (1 << (sps->log2_ctb_size))) ||
             (y0 + cb_size >= sps->height))) {
            int end_of_slice_flag = ff_hevc_end_of_slice_flag_decode(lc);
            return !end_of_slice_flag;
        } else {
            return 1;
        }
    }

    return 0;
}

static void hls_decode_neighbour(HEVCLocalContext *lc,
                                 const HEVCLayerContext *l,
                                 const HEVCPPS *pps, const HEVCSPS *sps,
                                 int x_ctb, int y_ctb, int ctb_addr_ts)
{
    const HEVCContext *const s = lc->parent;
    int ctb_size          = 1 << sps->log2_ctb_size;
    int ctb_addr_rs       = pps->ctb_addr_ts_to_rs[ctb_addr_ts];
    int ctb_addr_in_slice = ctb_addr_rs - s->sh.slice_addr;

    l->tab_slice_address[ctb_addr_rs] = s->sh.slice_addr;

    if (pps->entropy_coding_sync_enabled_flag) {
        if (x_ctb == 0 && (y_ctb & (ctb_size - 1)) == 0)
            lc->first_qp_group = 1;
        lc->end_of_tiles_x = sps->width;
    } else if (pps->tiles_enabled_flag) {
        if (ctb_addr_ts && pps->tile_id[ctb_addr_ts] != pps->tile_id[ctb_addr_ts - 1]) {
            int idxX = pps->col_idxX[x_ctb >> sps->log2_ctb_size];
            lc->end_of_tiles_x   = x_ctb + (pps->column_width[idxX] << sps->log2_ctb_size);
            lc->first_qp_group   = 1;
        }
    } else {
        lc->end_of_tiles_x = sps->width;
    }

    lc->end_of_tiles_y = FFMIN(y_ctb + ctb_size, sps->height);

    lc->boundary_flags = 0;
    if (pps->tiles_enabled_flag) {
        if (x_ctb > 0 && pps->tile_id[ctb_addr_ts] != pps->tile_id[pps->ctb_addr_rs_to_ts[ctb_addr_rs - 1]])
            lc->boundary_flags |= BOUNDARY_LEFT_TILE;
        if (x_ctb > 0 && l->tab_slice_address[ctb_addr_rs] != l->tab_slice_address[ctb_addr_rs - 1])
            lc->boundary_flags |= BOUNDARY_LEFT_SLICE;
        if (y_ctb > 0 && pps->tile_id[ctb_addr_ts] != pps->tile_id[pps->ctb_addr_rs_to_ts[ctb_addr_rs - sps->ctb_width]])
            lc->boundary_flags |= BOUNDARY_UPPER_TILE;
        if (y_ctb > 0 && l->tab_slice_address[ctb_addr_rs] != l->tab_slice_address[ctb_addr_rs - sps->ctb_width])
            lc->boundary_flags |= BOUNDARY_UPPER_SLICE;
    } else {
        if (ctb_addr_in_slice <= 0)
            lc->boundary_flags |= BOUNDARY_LEFT_SLICE;
        if (ctb_addr_in_slice < sps->ctb_width)
            lc->boundary_flags |= BOUNDARY_UPPER_SLICE;
    }

    lc->ctb_left_flag = ((x_ctb > 0) && (ctb_addr_in_slice > 0) && !(lc->boundary_flags & BOUNDARY_LEFT_TILE));
    lc->ctb_up_flag   = ((y_ctb > 0) && (ctb_addr_in_slice >= sps->ctb_width) && !(lc->boundary_flags & BOUNDARY_UPPER_TILE));
    lc->ctb_up_right_flag = ((y_ctb > 0)  && (ctb_addr_in_slice+1 >= sps->ctb_width) && (pps->tile_id[ctb_addr_ts] == pps->tile_id[pps->ctb_addr_rs_to_ts[ctb_addr_rs+1 - sps->ctb_width]]));
    lc->ctb_up_left_flag = ((x_ctb > 0) && (y_ctb > 0)  && (ctb_addr_in_slice-1 >= sps->ctb_width) && (pps->tile_id[ctb_addr_ts] == pps->tile_id[pps->ctb_addr_rs_to_ts[ctb_addr_rs-1 - sps->ctb_width]]));
}

static int hls_decode_entry(HEVCContext *s, GetBitContext *gb)
{
    HEVCLocalContext *const lc = &s->local_ctx[0];
    const HEVCLayerContext *const l = &s->layers[s->cur_layer];
    const HEVCPPS   *const pps = s->pps;
    const HEVCSPS   *const sps = pps->sps;
    const uint8_t *slice_data = gb->buffer + s->sh.data_offset;
    const size_t   slice_size = gb->buffer_end - gb->buffer - s->sh.data_offset;
    int ctb_size    = 1 << sps->log2_ctb_size;
    int more_data   = 1;
    int x_ctb       = 0;
    int y_ctb       = 0;
    int ctb_addr_ts = pps->ctb_addr_rs_to_ts[s->sh.slice_ctb_addr_rs];
    int ret;

    while (more_data && ctb_addr_ts < sps->ctb_size) {
        int ctb_addr_rs = pps->ctb_addr_ts_to_rs[ctb_addr_ts];

        x_ctb = (ctb_addr_rs % ((sps->width + ctb_size - 1) >> sps->log2_ctb_size)) << sps->log2_ctb_size;
        y_ctb = (ctb_addr_rs / ((sps->width + ctb_size - 1) >> sps->log2_ctb_size)) << sps->log2_ctb_size;
        hls_decode_neighbour(lc, l, pps, sps, x_ctb, y_ctb, ctb_addr_ts);

        ret = ff_hevc_cabac_init(lc, pps, ctb_addr_ts, slice_data, slice_size, 0);
        if (ret < 0) {
            l->tab_slice_address[ctb_addr_rs] = -1;
            return ret;
        }

        hls_sao_param(lc, l, pps, sps,
                      x_ctb >> sps->log2_ctb_size, y_ctb >> sps->log2_ctb_size);

        l->deblock[ctb_addr_rs].beta_offset = s->sh.beta_offset;
        l->deblock[ctb_addr_rs].tc_offset   = s->sh.tc_offset;
        l->filter_slice_edges[ctb_addr_rs]  = s->sh.slice_loop_filter_across_slices_enabled_flag;

        more_data = hls_coding_quadtree(lc, l, pps, sps, x_ctb, y_ctb, sps->log2_ctb_size, 0);
        if (more_data < 0) {
            l->tab_slice_address[ctb_addr_rs] = -1;
            return more_data;
        }


        ctb_addr_ts++;
        ff_hevc_save_states(lc, pps, ctb_addr_ts);
        ff_hevc_hls_filters(lc, l, pps, x_ctb, y_ctb, ctb_size);
    }

    if (x_ctb + ctb_size >= sps->width &&
        y_ctb + ctb_size >= sps->height)
        ff_hevc_hls_filter(lc, l, pps, x_ctb, y_ctb, ctb_size);

    return ctb_addr_ts;
}

static int hls_decode_entry_wpp(AVCodecContext *avctx, void *hevc_lclist,
                                int job, int thread)
{
    HEVCLocalContext *lc = &((HEVCLocalContext*)hevc_lclist)[thread];
    const HEVCContext *const s = lc->parent;
    const HEVCLayerContext *const l = &s->layers[s->cur_layer];
    const HEVCPPS   *const pps = s->pps;
    const HEVCSPS   *const sps = pps->sps;
    int ctb_size    = 1 << sps->log2_ctb_size;
    int more_data   = 1;
    int ctb_row = job;
    int ctb_addr_rs = s->sh.slice_ctb_addr_rs + ctb_row * ((sps->width + ctb_size - 1) >> sps->log2_ctb_size);
    int ctb_addr_ts = pps->ctb_addr_rs_to_ts[ctb_addr_rs];

    const uint8_t *data      = s->data + s->sh.offset[ctb_row];
    const size_t   data_size = s->sh.size[ctb_row];

    int progress = 0;

    int ret;

    if (ctb_row)
        ff_init_cabac_decoder(&lc->cc, data, data_size);

    while(more_data && ctb_addr_ts < sps->ctb_size) {
        int x_ctb = (ctb_addr_rs % sps->ctb_width) << sps->log2_ctb_size;
        int y_ctb = (ctb_addr_rs / sps->ctb_width) << sps->log2_ctb_size;

        hls_decode_neighbour(lc, l, pps, sps, x_ctb, y_ctb, ctb_addr_ts);

        if (ctb_row)
            ff_thread_progress_await(&s->wpp_progress[ctb_row - 1],
                                     progress + SHIFT_CTB_WPP + 1);

        /* atomic_load's prototype requires a pointer to non-const atomic variable
         * (due to implementations via mutexes, where reads involve writes).
         * Of course, casting const away here is nevertheless safe. */
        if (atomic_load((atomic_int*)&s->wpp_err)) {
            ff_thread_progress_report(&s->wpp_progress[ctb_row], INT_MAX);
            return 0;
        }

        ret = ff_hevc_cabac_init(lc, pps, ctb_addr_ts, data, data_size, 1);
        if (ret < 0)
            goto error;
        hls_sao_param(lc, l, pps, sps,
                      x_ctb >> sps->log2_ctb_size, y_ctb >> sps->log2_ctb_size);

        l->deblock[ctb_addr_rs].beta_offset = s->sh.beta_offset;
        l->deblock[ctb_addr_rs].tc_offset   = s->sh.tc_offset;
        l->filter_slice_edges[ctb_addr_rs]  = s->sh.slice_loop_filter_across_slices_enabled_flag;

        more_data = hls_coding_quadtree(lc, l, pps, sps, x_ctb, y_ctb, sps->log2_ctb_size, 0);

        if (more_data < 0) {
            ret = more_data;
            goto error;
        }

        ctb_addr_ts++;

        ff_hevc_save_states(lc, pps, ctb_addr_ts);
        ff_thread_progress_report(&s->wpp_progress[ctb_row], ++progress);
        ff_hevc_hls_filters(lc, l, pps, x_ctb, y_ctb, ctb_size);

        if (!more_data && (x_ctb+ctb_size) < sps->width && ctb_row != s->sh.num_entry_point_offsets) {
            /* Casting const away here is safe, because it is an atomic operation. */
            atomic_store((atomic_int*)&s->wpp_err, 1);
            ff_thread_progress_report(&s->wpp_progress[ctb_row], INT_MAX);
            return 0;
        }

        if ((x_ctb+ctb_size) >= sps->width && (y_ctb+ctb_size) >= sps->height ) {
            ff_hevc_hls_filter(lc, l, pps, x_ctb, y_ctb, ctb_size);
            ff_thread_progress_report(&s->wpp_progress[ctb_row], INT_MAX);
            return ctb_addr_ts;
        }
        ctb_addr_rs = pps->ctb_addr_ts_to_rs[ctb_addr_ts];
        x_ctb+=ctb_size;

        if(x_ctb >= sps->width) {
            break;
        }
    }
    ff_thread_progress_report(&s->wpp_progress[ctb_row], INT_MAX);

    return 0;
error:
    l->tab_slice_address[ctb_addr_rs] = -1;
    /* Casting const away here is safe, because it is an atomic operation. */
    atomic_store((atomic_int*)&s->wpp_err, 1);
    ff_thread_progress_report(&s->wpp_progress[ctb_row], INT_MAX);
    return ret;
}

static int wpp_progress_init(HEVCContext *s, unsigned count)
{
    if (s->nb_wpp_progress < count) {
        void *tmp = av_realloc_array(s->wpp_progress, count,
                                     sizeof(*s->wpp_progress));
        if (!tmp)
            return AVERROR(ENOMEM);

        s->wpp_progress = tmp;
        memset(s->wpp_progress + s->nb_wpp_progress, 0,
               (count - s->nb_wpp_progress) * sizeof(*s->wpp_progress));

        for (int i = s->nb_wpp_progress; i < count; i++) {
            int ret = ff_thread_progress_init(&s->wpp_progress[i], 1);
            if (ret < 0)
                return ret;
            s->nb_wpp_progress = i + 1;
        }
    }

    for (int i = 0; i < count; i++)
        ff_thread_progress_reset(&s->wpp_progress[i]);

    return 0;
}

static int hls_slice_data_wpp(HEVCContext *s, const H2645NAL *nal)
{
    const HEVCPPS *const pps = s->pps;
    const HEVCSPS *const sps = pps->sps;
    const uint8_t *data = nal->data;
    int length          = nal->size;
    int *ret;
    int64_t offset;
    int64_t startheader, cmpt = 0;
    int i, j, res = 0;

    if (s->sh.slice_ctb_addr_rs + s->sh.num_entry_point_offsets * sps->ctb_width >= sps->ctb_width * sps->ctb_height) {
        av_log(s->avctx, AV_LOG_ERROR, "WPP ctb addresses are wrong (%d %d %d %d)\n",
            s->sh.slice_ctb_addr_rs, s->sh.num_entry_point_offsets,
            sps->ctb_width, sps->ctb_height
        );
        return AVERROR_INVALIDDATA;
    }

    if (s->avctx->thread_count > s->nb_local_ctx) {
        HEVCLocalContext *tmp = av_malloc_array(s->avctx->thread_count, sizeof(*s->local_ctx));

        if (!tmp)
            return AVERROR(ENOMEM);

        memcpy(tmp, s->local_ctx, sizeof(*s->local_ctx) * s->nb_local_ctx);
        av_free(s->local_ctx);
        s->local_ctx = tmp;

        for (unsigned i = s->nb_local_ctx; i < s->avctx->thread_count; i++) {
            tmp = &s->local_ctx[i];

            memset(tmp, 0, sizeof(*tmp));

            tmp->logctx             = s->avctx;
            tmp->parent             = s;
            tmp->common_cabac_state = &s->cabac;
        }

        s->nb_local_ctx = s->avctx->thread_count;
    }

    offset = s->sh.data_offset;

    for (j = 0, cmpt = 0, startheader = offset + s->sh.entry_point_offset[0]; j < nal->skipped_bytes; j++) {
        if (nal->skipped_bytes_pos[j] >= offset && nal->skipped_bytes_pos[j] < startheader) {
            startheader--;
            cmpt++;
        }
    }

    for (i = 1; i < s->sh.num_entry_point_offsets; i++) {
        offset += (s->sh.entry_point_offset[i - 1] - cmpt);
        for (j = 0, cmpt = 0, startheader = offset
             + s->sh.entry_point_offset[i]; j < nal->skipped_bytes; j++) {
            if (nal->skipped_bytes_pos[j] >= offset && nal->skipped_bytes_pos[j] < startheader) {
                startheader--;
                cmpt++;
            }
        }
        s->sh.size[i]   = s->sh.entry_point_offset[i] - cmpt;
        s->sh.offset[i] = offset;

    }

    offset += s->sh.entry_point_offset[s->sh.num_entry_point_offsets - 1] - cmpt;
    if (length < offset) {
        av_log(s->avctx, AV_LOG_ERROR, "entry_point_offset table is corrupted\n");
        return AVERROR_INVALIDDATA;
    }
    s->sh.size  [s->sh.num_entry_point_offsets] = length - offset;
    s->sh.offset[s->sh.num_entry_point_offsets] = offset;

    s->sh.offset[0] = s->sh.data_offset;
    s->sh.size[0]   = s->sh.offset[1] - s->sh.offset[0];

    s->data = data;

    for (i = 1; i < s->nb_local_ctx; i++) {
        s->local_ctx[i].first_qp_group = 1;
        s->local_ctx[i].qp_y = s->local_ctx[0].qp_y;
    }

    atomic_store(&s->wpp_err, 0);
    res = wpp_progress_init(s, s->sh.num_entry_point_offsets + 1);
    if (res < 0)
        return res;

    ret = av_calloc(s->sh.num_entry_point_offsets + 1, sizeof(*ret));
    if (!ret)
        return AVERROR(ENOMEM);

    if (pps->entropy_coding_sync_enabled_flag)
        s->avctx->execute2(s->avctx, hls_decode_entry_wpp, s->local_ctx, ret, s->sh.num_entry_point_offsets + 1);

    for (i = 0; i <= s->sh.num_entry_point_offsets; i++)
        res += ret[i];

    av_free(ret);
    return res;
}

static int decode_slice_data(HEVCContext *s, const HEVCLayerContext *l,
                             const H2645NAL *nal, GetBitContext *gb)
{
    const HEVCPPS *pps = s->pps;
    int ret;

    if (!s->sh.first_slice_in_pic_flag)
        s->slice_idx += !s->sh.dependent_slice_segment_flag;

    if (!s->sh.dependent_slice_segment_flag && s->sh.slice_type != HEVC_SLICE_I) {
        ret = ff_hevc_slice_rpl(s);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "Error constructing the reference lists for the current slice.\n");
            return ret;
        }
    }

    s->slice_initialized = 1;

    if (s->avctx->hwaccel)
        return FF_HW_CALL(s->avctx, decode_slice, nal->raw_data, nal->raw_size);

    if (s->avctx->profile == AV_PROFILE_HEVC_SCC) {
        av_log(s->avctx, AV_LOG_ERROR,
               "SCC profile is not yet implemented in hevc native decoder.\n");
        return AVERROR_PATCHWELCOME;
    }

    if (s->sh.dependent_slice_segment_flag) {
        int ctb_addr_ts = pps->ctb_addr_rs_to_ts[s->sh.slice_ctb_addr_rs];
        int prev_rs = pps->ctb_addr_ts_to_rs[ctb_addr_ts - 1];
        if (l->tab_slice_address[prev_rs] != s->sh.slice_addr) {
            av_log(s->avctx, AV_LOG_ERROR, "Previous slice segment missing\n");
            return AVERROR_INVALIDDATA;
        }
    }

    s->local_ctx[0].first_qp_group = !s->sh.dependent_slice_segment_flag;

    if (!pps->cu_qp_delta_enabled_flag)
        s->local_ctx[0].qp_y = s->sh.slice_qp;

    s->local_ctx[0].tu.cu_qp_offset_cb = 0;
    s->local_ctx[0].tu.cu_qp_offset_cr = 0;

    if (s->avctx->active_thread_type == FF_THREAD_SLICE  &&
        s->sh.num_entry_point_offsets > 0                &&
        pps->num_tile_rows == 1 && pps->num_tile_columns == 1)
        return hls_slice_data_wpp(s, nal);

    return hls_decode_entry(s, gb);
}

static int set_side_data(HEVCContext *s)
{
    const HEVCSPS *sps = s->cur_frame->pps->sps;
    AVFrame *out = s->cur_frame->f;
    int ret;

    // Decrement the mastering display and content light level flag when IRAP
    // frame has no_rasl_output_flag=1 so the side data persists for the entire
    // coded video sequence.
    if (IS_IRAP(s) && s->no_rasl_output_flag) {
        if (s->sei.common.mastering_display.present > 0)
            s->sei.common.mastering_display.present--;

        if (s->sei.common.content_light.present > 0)
            s->sei.common.content_light.present--;
    }

    ret = ff_h2645_sei_to_frame(out, &s->sei.common, AV_CODEC_ID_HEVC, s->avctx,
                                &sps->vui.common,
                                sps->bit_depth, sps->bit_depth_chroma,
                                s->cur_frame->poc /* no poc_offset in HEVC */);
    if (ret < 0)
        return ret;

    if (s->sei.timecode.present) {
        uint32_t *tc_sd;
        char tcbuf[AV_TIMECODE_STR_SIZE];
        AVFrameSideData *tcside;
        ret = ff_frame_new_side_data(s->avctx, out, AV_FRAME_DATA_S12M_TIMECODE,
                                     sizeof(uint32_t) * 4, &tcside);
        if (ret < 0)
            return ret;

        if (tcside) {
            tc_sd = (uint32_t*)tcside->data;
            tc_sd[0] = s->sei.timecode.num_clock_ts;

            for (int i = 0; i < tc_sd[0]; i++) {
                int drop = s->sei.timecode.cnt_dropped_flag[i];
                int   hh = s->sei.timecode.hours_value[i];
                int   mm = s->sei.timecode.minutes_value[i];
                int   ss = s->sei.timecode.seconds_value[i];
                int   ff = s->sei.timecode.n_frames[i];

                tc_sd[i + 1] = av_timecode_get_smpte(s->avctx->framerate, drop, hh, mm, ss, ff);
                av_timecode_make_smpte_tc_string2(tcbuf, s->avctx->framerate, tc_sd[i + 1], 0, 0);
                av_dict_set(&out->metadata, "timecode", tcbuf, 0);
            }
        }

        s->sei.timecode.num_clock_ts = 0;
    }

    if (s->sei.common.dynamic_hdr_plus.info) {
        AVBufferRef *info_ref = av_buffer_ref(s->sei.common.dynamic_hdr_plus.info);
        if (!info_ref)
            return AVERROR(ENOMEM);

        ret = ff_frame_new_side_data_from_buf(s->avctx, out, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, &info_ref);
        if (ret < 0)
            return ret;
    }

    if (s->rpu_buf) {
        AVFrameSideData *rpu = av_frame_new_side_data_from_buf(out, AV_FRAME_DATA_DOVI_RPU_BUFFER, s->rpu_buf);
        if (!rpu)
            return AVERROR(ENOMEM);

        s->rpu_buf = NULL;
    }

    if ((ret = ff_dovi_attach_side_data(&s->dovi_ctx, out)) < 0)
        return ret;

    if (s->sei.common.dynamic_hdr_vivid.info) {
        AVBufferRef *info_ref = av_buffer_ref(s->sei.common.dynamic_hdr_vivid.info);
        if (!info_ref)
            return AVERROR(ENOMEM);

        if (!av_frame_new_side_data_from_buf(out, AV_FRAME_DATA_DYNAMIC_HDR_VIVID, info_ref)) {
            av_buffer_unref(&info_ref);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static int find_finish_setup_nal(const HEVCContext *s)
{
    int nal_idx = 0;

    for (int i = nal_idx; i < s->pkt.nb_nals; i++) {
        const H2645NAL *nal = &s->pkt.nals[i];
        const int  layer_id = nal->nuh_layer_id;
        GetBitContext    gb = nal->gb;

        if (layer_id > HEVC_MAX_NUH_LAYER_ID || s->vps->layer_idx[layer_id] < 0 ||
            !(s->layers_active_decode & (1 << s->vps->layer_idx[layer_id])))
            continue;

        switch (nal->type) {
        case HEVC_NAL_TRAIL_R:
        case HEVC_NAL_TRAIL_N:
        case HEVC_NAL_TSA_N:
        case HEVC_NAL_TSA_R:
        case HEVC_NAL_STSA_N:
        case HEVC_NAL_STSA_R:
        case HEVC_NAL_BLA_W_LP:
        case HEVC_NAL_BLA_W_RADL:
        case HEVC_NAL_BLA_N_LP:
        case HEVC_NAL_IDR_W_RADL:
        case HEVC_NAL_IDR_N_LP:
        case HEVC_NAL_CRA_NUT:
        case HEVC_NAL_RADL_N:
        case HEVC_NAL_RADL_R:
        case HEVC_NAL_RASL_N:
        case HEVC_NAL_RASL_R:
            if (!get_bits1(&gb)) // first_slice_segment_in_pic_flag
                continue;
        case HEVC_NAL_VPS:
        case HEVC_NAL_SPS:
        case HEVC_NAL_PPS:
            nal_idx = i;
            break;
        }
    }

    return nal_idx;
}

static int hevc_frame_start(HEVCContext *s, HEVCLayerContext *l,
                            unsigned nal_idx)
{
    const HEVCPPS *const pps = s->ps.pps_list[s->sh.pps_id];
    const HEVCSPS *const sps = pps->sps;
    int pic_size_in_ctb  = ((sps->width  >> sps->log2_min_cb_size) + 1) *
                           ((sps->height >> sps->log2_min_cb_size) + 1);
    int new_sequence = (l == &s->layers[0]) &&
                       (IS_IDR(s) || IS_BLA(s) || s->last_eos);
    int prev_layers_active_decode = s->layers_active_decode;
    int prev_layers_active_output = s->layers_active_output;
    int ret;

    if (sps->vps != s->vps && l != &s->layers[0]) {
        av_log(s->avctx, AV_LOG_ERROR, "VPS changed in a non-base layer\n");
        set_sps(s, l, NULL);
        return AVERROR_INVALIDDATA;
    }

    ff_refstruct_replace(&s->pps, pps);
    if (l->sps != sps) {
        const HEVCSPS *sps_base = s->layers[0].sps;
        enum AVPixelFormat pix_fmt = sps->pix_fmt;

        if (l != &s->layers[0]) {
            if (!sps_base) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Access unit starts with a non-base layer frame\n");
                return AVERROR_INVALIDDATA;
            }

            // Files produced by Vision Pro lack VPS extension VUI,
            // so the secondary layer has no range information.
            // This check avoids failing in such a case.
            if (sps_base->pix_fmt == AV_PIX_FMT_YUVJ420P &&
                sps->pix_fmt == AV_PIX_FMT_YUV420P       &&
                !sps->vui.common.video_signal_type_present_flag)
                pix_fmt = sps_base->pix_fmt;

            if (pix_fmt     != sps_base->pix_fmt ||
                sps->width  != sps_base->width   ||
                sps->height != sps_base->height) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Base/non-base layer SPS have unsupported parameter combination\n");
                return AVERROR(ENOSYS);
            }
        }

        ff_hevc_clear_refs(l);

        ret = set_sps(s, l, sps);
        if (ret < 0)
            return ret;

        if (l == &s->layers[0]) {
            export_stream_params(s, sps);

            ret = get_format(s, sps);
            if (ret < 0) {
                set_sps(s, l, NULL);
                return ret;
            }

            new_sequence = 1;
        }
    }

    memset(l->horizontal_bs, 0, l->bs_width * l->bs_height);
    memset(l->vertical_bs,   0, l->bs_width * l->bs_height);
    memset(l->cbf_luma,      0, sps->min_tb_width * sps->min_tb_height);
    memset(l->is_pcm,        0, (sps->min_pu_width + 1) * (sps->min_pu_height + 1));
    memset(l->tab_slice_address, -1, pic_size_in_ctb * sizeof(*l->tab_slice_address));

    if (IS_IDR(s))
        ff_hevc_clear_refs(l);

    s->slice_idx         = 0;
    s->first_nal_type    = s->nal_unit_type;
    s->poc               = s->sh.poc;

    if (IS_IRAP(s))
        s->no_rasl_output_flag = IS_IDR(s) || IS_BLA(s) ||
                                 (s->nal_unit_type == HEVC_NAL_CRA_NUT && s->last_eos);

    /* 8.3.1 */
    if (s->temporal_id == 0 &&
        s->nal_unit_type != HEVC_NAL_TRAIL_N &&
        s->nal_unit_type != HEVC_NAL_TSA_N   &&
        s->nal_unit_type != HEVC_NAL_STSA_N  &&
        s->nal_unit_type != HEVC_NAL_RADL_N  &&
        s->nal_unit_type != HEVC_NAL_RADL_R  &&
        s->nal_unit_type != HEVC_NAL_RASL_N  &&
        s->nal_unit_type != HEVC_NAL_RASL_R)
        s->poc_tid0 = s->poc;

    if (pps->tiles_enabled_flag)
        s->local_ctx[0].end_of_tiles_x = pps->column_width[0] << sps->log2_ctb_size;

    if (new_sequence) {
        ret = ff_hevc_output_frames(s, prev_layers_active_decode, prev_layers_active_output,
                                    0, 0, s->sh.no_output_of_prior_pics_flag);
        if (ret < 0)
            return ret;
    }

    ret = export_stream_params_from_sei(s);
    if (ret < 0)
        return ret;

    ret = ff_hevc_set_new_ref(s, l, s->poc);
    if (ret < 0)
        goto fail;

    ret = ff_hevc_frame_rps(s, l);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Error constructing the frame RPS.\n");
        goto fail;
    }

    if (IS_IRAP(s))
        s->cur_frame->f->flags |= AV_FRAME_FLAG_KEY;
    else
        s->cur_frame->f->flags &= ~AV_FRAME_FLAG_KEY;

    s->cur_frame->needs_fg = ((s->sei.common.film_grain_characteristics &&
                               s->sei.common.film_grain_characteristics->present) ||
                              s->sei.common.aom_film_grain.enable) &&
        !(s->avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) &&
        !s->avctx->hwaccel;

    ret = set_side_data(s);
    if (ret < 0)
        goto fail;

    if (s->cur_frame->needs_fg &&
        (s->sei.common.film_grain_characteristics && s->sei.common.film_grain_characteristics->present &&
         !ff_h274_film_grain_params_supported(s->sei.common.film_grain_characteristics->model_id,
                                              s->cur_frame->f->format) ||
         !av_film_grain_params_select(s->cur_frame->f))) {
        av_log_once(s->avctx, AV_LOG_WARNING, AV_LOG_DEBUG, &s->film_grain_warning_shown,
                    "Unsupported film grain parameters. Ignoring film grain.\n");
        s->cur_frame->needs_fg = 0;
    }

    if (s->cur_frame->needs_fg) {
        s->cur_frame->frame_grain->format = s->cur_frame->f->format;
        s->cur_frame->frame_grain->width  = s->cur_frame->f->width;
        s->cur_frame->frame_grain->height = s->cur_frame->f->height;
        if ((ret = ff_thread_get_buffer(s->avctx, s->cur_frame->frame_grain, 0)) < 0)
            goto fail;

        ret = av_frame_copy_props(s->cur_frame->frame_grain, s->cur_frame->f);
        if (ret < 0)
            goto fail;
    }

    s->cur_frame->f->pict_type = 3 - s->sh.slice_type;

    ret = ff_hevc_output_frames(s, s->layers_active_decode, s->layers_active_output,
                                sps->temporal_layer[sps->max_sub_layers - 1].num_reorder_pics,
                                sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering, 0);
    if (ret < 0)
        goto fail;

    if (s->avctx->hwaccel) {
        ret = FF_HW_CALL(s->avctx, start_frame, NULL, 0);
        if (ret < 0)
            goto fail;
    }

    // after starting the base-layer frame we know which layers will be decoded,
    // so we can now figure out which NALUs to wait for before we can call
    // ff_thread_finish_setup()
    if (l == &s->layers[0])
        s->finish_setup_nal_idx = find_finish_setup_nal(s);

    if (nal_idx >= s->finish_setup_nal_idx)
        ff_thread_finish_setup(s->avctx);

    return 0;

fail:
    if (l->cur_frame)
        ff_hevc_unref_frame(l->cur_frame, ~0);
    l->cur_frame = NULL;
    s->cur_frame = s->collocated_ref = NULL;
    s->slice_initialized = 0;
    return ret;
}

static int verify_md5(HEVCContext *s, AVFrame *frame)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    char msg_buf[4 * (50 + 2 * 2 * 16 /* MD5-size */)];
    int pixel_shift;
    int err = 0;
    int i, j;

    if (!desc)
        return AVERROR(EINVAL);

    pixel_shift = desc->comp[0].depth > 8;

    /* the checksums are LE, so we have to byteswap for >8bpp formats
     * on BE arches */
#if HAVE_BIGENDIAN
    if (pixel_shift && !s->checksum_buf) {
        av_fast_malloc(&s->checksum_buf, &s->checksum_buf_size,
                       FFMAX3(frame->linesize[0], frame->linesize[1],
                              frame->linesize[2]));
        if (!s->checksum_buf)
            return AVERROR(ENOMEM);
    }
#endif

    msg_buf[0] = '\0';
    for (i = 0; frame->data[i]; i++) {
        int width  = s->avctx->coded_width;
        int height = s->avctx->coded_height;
        int w = (i == 1 || i == 2) ? (width  >> desc->log2_chroma_w) : width;
        int h = (i == 1 || i == 2) ? (height >> desc->log2_chroma_h) : height;
        uint8_t md5[16];

        av_md5_init(s->md5_ctx);
        for (j = 0; j < h; j++) {
            const uint8_t *src = frame->data[i] + j * frame->linesize[i];
#if HAVE_BIGENDIAN
            if (pixel_shift) {
                s->bdsp.bswap16_buf((uint16_t *) s->checksum_buf,
                                    (const uint16_t *) src, w);
                src = s->checksum_buf;
            }
#endif
            av_md5_update(s->md5_ctx, src, w << pixel_shift);
        }
        av_md5_final(s->md5_ctx, md5);

#define MD5_PRI "%016" PRIx64 "%016" PRIx64
#define MD5_PRI_ARG(buf) AV_RB64(buf), AV_RB64((const uint8_t*)(buf) + 8)

        if (!memcmp(md5, s->sei.picture_hash.md5[i], 16)) {
            av_strlcatf(msg_buf, sizeof(msg_buf),
                        "plane %d - correct " MD5_PRI "; ",
                        i, MD5_PRI_ARG(md5));
        } else {
            av_strlcatf(msg_buf, sizeof(msg_buf),
                       "mismatching checksum of plane %d - " MD5_PRI " != " MD5_PRI "; ",
                        i, MD5_PRI_ARG(md5), MD5_PRI_ARG(s->sei.picture_hash.md5[i]));
            err = AVERROR_INVALIDDATA;
        }
    }

    av_log(s->avctx, err < 0 ? AV_LOG_ERROR : AV_LOG_DEBUG,
           "Verifying checksum for frame with POC %d: %s\n",
           s->poc, msg_buf);

    return err;
    }

static int hevc_frame_end(HEVCContext *s, HEVCLayerContext *l)
{
    HEVCFrame *out = l->cur_frame;
    const AVFilmGrainParams *fgp;
    av_unused int ret;

    if (out->needs_fg) {
        av_assert0(out->frame_grain->buf[0]);
        fgp = av_film_grain_params_select(out->f);
        switch (fgp->type) {
        case AV_FILM_GRAIN_PARAMS_NONE:
            av_assert0(0);
            return AVERROR_BUG;
        case AV_FILM_GRAIN_PARAMS_H274:
            ret = ff_h274_apply_film_grain(out->frame_grain, out->f,
                                           &s->h274db, fgp);
            break;
        case AV_FILM_GRAIN_PARAMS_AV1:
            ret = ff_aom_apply_film_grain(out->frame_grain, out->f, fgp);
            break;
        }
        av_assert1(ret >= 0);
    }

    if (s->avctx->hwaccel) {
        ret = FF_HW_SIMPLE_CALL(s->avctx, end_frame);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "hardware accelerator failed to decode picture\n");
            return ret;
        }
    } else {
        if (s->avctx->err_recognition & AV_EF_CRCCHECK &&
            s->sei.picture_hash.is_md5) {
            ret = verify_md5(s, out->f);
            if (ret < 0 && s->avctx->err_recognition & AV_EF_EXPLODE)
                return ret;
        }
    }
    s->sei.picture_hash.is_md5 = 0;

    av_log(s->avctx, AV_LOG_DEBUG, "Decoded frame with POC %zu/%d.\n",
           l - s->layers, s->poc);

    return 0;
}

static int decode_slice(HEVCContext *s, unsigned nal_idx, GetBitContext *gb)
{
    const int layer_idx = s->vps ? s->vps->layer_idx[s->nuh_layer_id] : 0;
    HEVCLayerContext *l;
    int ret;

    // skip layers not requested to be decoded
    // layers_active_decode can only change while decoding a base-layer frame,
    // so we can check it for non-base layers
    if (layer_idx < 0 ||
        (s->nuh_layer_id > 0 && !(s->layers_active_decode & (1 << layer_idx))))
        return 0;

    ret = hls_slice_header(&s->sh, s, gb);
    if (ret < 0) {
        // hls_slice_header() does not cleanup on failure thus the state now is inconsistant so we cannot use it on depandant slices
        s->slice_initialized = 0;
        return ret;
    }

    if ((s->avctx->skip_frame >= AVDISCARD_BIDIR && s->sh.slice_type == HEVC_SLICE_B) ||
        (s->avctx->skip_frame >= AVDISCARD_NONINTRA && s->sh.slice_type != HEVC_SLICE_I) ||
        (s->avctx->skip_frame >= AVDISCARD_NONKEY && !IS_IRAP(s)) ||
        ((s->nal_unit_type == HEVC_NAL_RASL_R || s->nal_unit_type == HEVC_NAL_RASL_N) &&
         s->no_rasl_output_flag)) {
        return 0;
    }

    // switching to a new layer, mark previous layer's frame (if any) as done
    if (s->cur_layer != layer_idx &&
        s->layers[s->cur_layer].cur_frame &&
        s->avctx->active_thread_type == FF_THREAD_FRAME)
        ff_progress_frame_report(&s->layers[s->cur_layer].cur_frame->tf, INT_MAX);

    s->cur_layer = layer_idx;
    l = &s->layers[s->cur_layer];

    if (s->sh.first_slice_in_pic_flag) {
        if (l->cur_frame) {
            av_log(s->avctx, AV_LOG_ERROR, "Two slices reporting being the first in the same frame.\n");
            return AVERROR_INVALIDDATA;
        }

        ret = hevc_frame_start(s, l, nal_idx);
        if (ret < 0)
            return ret;
    } else if (!l->cur_frame) {
        av_log(s->avctx, AV_LOG_ERROR, "First slice in a frame missing.\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->nal_unit_type != s->first_nal_type) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Non-matching NAL types of the VCL NALUs: %d %d\n",
               s->first_nal_type, s->nal_unit_type);
        return AVERROR_INVALIDDATA;
    }

    ret = decode_slice_data(s, l, &s->pkt.nals[nal_idx], gb);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_nal_unit(HEVCContext *s, unsigned nal_idx)
{
    H2645NAL *nal = &s->pkt.nals[nal_idx];
    GetBitContext     gb = nal->gb;
    int ret;

    s->nal_unit_type = nal->type;
    s->nuh_layer_id  = nal->nuh_layer_id;
    s->temporal_id   = nal->temporal_id;

    if (FF_HW_HAS_CB(s->avctx, decode_params) &&
        (s->nal_unit_type == HEVC_NAL_VPS ||
         s->nal_unit_type == HEVC_NAL_SPS ||
         s->nal_unit_type == HEVC_NAL_PPS ||
         s->nal_unit_type == HEVC_NAL_SEI_PREFIX ||
         s->nal_unit_type == HEVC_NAL_SEI_SUFFIX)) {
        ret = FF_HW_CALL(s->avctx, decode_params,
                         nal->type, nal->raw_data, nal->raw_size);
        if (ret < 0)
            goto fail;
    }

    switch (s->nal_unit_type) {
    case HEVC_NAL_VPS:
        ret = ff_hevc_decode_nal_vps(&gb, s->avctx, &s->ps);
        if (ret < 0)
            goto fail;
        break;
    case HEVC_NAL_SPS:
        ret = ff_hevc_decode_nal_sps(&gb, s->avctx, &s->ps,
                                     nal->nuh_layer_id, s->apply_defdispwin);
        if (ret < 0)
            goto fail;
        break;
    case HEVC_NAL_PPS:
        ret = ff_hevc_decode_nal_pps(&gb, s->avctx, &s->ps);
        if (ret < 0)
            goto fail;
        break;
    case HEVC_NAL_SEI_PREFIX:
    case HEVC_NAL_SEI_SUFFIX:
        ret = ff_hevc_decode_nal_sei(&gb, s->avctx, &s->sei, &s->ps, s->nal_unit_type);
        if (ret < 0)
            goto fail;
        break;
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
        ret = decode_slice(s, nal_idx, &gb);
        if (ret < 0)
            goto fail;
        break;
    case HEVC_NAL_EOS_NUT:
    case HEVC_NAL_EOB_NUT:
    case HEVC_NAL_AUD:
    case HEVC_NAL_FD_NUT:
    case HEVC_NAL_UNSPEC62:
        break;
    default:
        av_log(s->avctx, AV_LOG_INFO,
               "Skipping NAL unit %d\n", s->nal_unit_type);
    }

    return 0;
fail:
    if (ret == AVERROR_INVALIDDATA &&
        !(s->avctx->err_recognition & AV_EF_EXPLODE)) {
        av_log(s->avctx, AV_LOG_WARNING,
               "Skipping invalid undecodable NALU: %d\n", s->nal_unit_type);
        return 0;
    }
    return ret;
}

static int decode_nal_units(HEVCContext *s, const uint8_t *buf, int length)
{
    int i, ret = 0;
    int eos_at_start = 1;
    int flags = (H2645_FLAG_IS_NALFF * !!s->is_nalff) | H2645_FLAG_SMALL_PADDING;

    s->cur_frame = s->collocated_ref = NULL;
    s->last_eos = s->eos;
    s->eos = 0;
    s->slice_initialized = 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->layers); i++) {
        HEVCLayerContext *l = &s->layers[i];
        l->cur_frame = NULL;
    }

    /* split the input packet into NAL units, so we know the upper bound on the
     * number of slices in the frame */
    ret = ff_h2645_packet_split(&s->pkt, buf, length, s->avctx,
                                s->nal_length_size, s->avctx->codec_id, flags);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Error splitting the input into NAL units.\n");
        return ret;
    }

    for (i = 0; i < s->pkt.nb_nals; i++) {
        if (s->pkt.nals[i].type == HEVC_NAL_EOB_NUT ||
            s->pkt.nals[i].type == HEVC_NAL_EOS_NUT) {
            if (eos_at_start) {
                s->last_eos = 1;
            } else {
                s->eos = 1;
            }
        } else {
            eos_at_start = 0;
        }
    }

    /*
     * Check for RPU delimiter.
     *
     * Dolby Vision RPUs masquerade as unregistered NALs of type 62.
     *
     * We have to do this check here an create the rpu buffer, since RPUs are appended
     * to the end of an AU; they are the last non-EOB/EOS NAL in the AU.
     */
    if (s->pkt.nb_nals > 1 && s->pkt.nals[s->pkt.nb_nals - 1].type == HEVC_NAL_UNSPEC62 &&
        s->pkt.nals[s->pkt.nb_nals - 1].size > 2 && !s->pkt.nals[s->pkt.nb_nals - 1].nuh_layer_id
        && !s->pkt.nals[s->pkt.nb_nals - 1].temporal_id) {
        H2645NAL *nal = &s->pkt.nals[s->pkt.nb_nals - 1];
        if (s->rpu_buf) {
            av_buffer_unref(&s->rpu_buf);
            av_log(s->avctx, AV_LOG_WARNING, "Multiple Dolby Vision RPUs found in one AU. Skipping previous.\n");
        }

        s->rpu_buf = av_buffer_alloc(nal->raw_size - 2);
        if (!s->rpu_buf)
            return AVERROR(ENOMEM);
        memcpy(s->rpu_buf->data, nal->raw_data + 2, nal->raw_size - 2);

        ret = ff_dovi_rpu_parse(&s->dovi_ctx, nal->data + 2, nal->size - 2,
                                s->avctx->err_recognition);
        if (ret < 0) {
            av_buffer_unref(&s->rpu_buf);
            av_log(s->avctx, AV_LOG_WARNING, "Error parsing DOVI NAL unit.\n");
            /* ignore */
        }
    }

    /* decode the NAL units */
    for (i = 0; i < s->pkt.nb_nals; i++) {
        H2645NAL *nal = &s->pkt.nals[i];

        if (s->avctx->skip_frame >= AVDISCARD_ALL ||
            (s->avctx->skip_frame >= AVDISCARD_NONREF && ff_hevc_nal_is_nonref(nal->type)))
            continue;

        ret = decode_nal_unit(s, i);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "Error parsing NAL unit #%d.\n", i);
            goto fail;
        }
    }

fail:
    for (int i = 0; i < FF_ARRAY_ELEMS(s->layers); i++) {
        HEVCLayerContext *l = &s->layers[i];

        if (!l->cur_frame)
            continue;

        if (ret >= 0)
            ret = hevc_frame_end(s, l);

        if (s->avctx->active_thread_type == FF_THREAD_FRAME)
            ff_progress_frame_report(&l->cur_frame->tf, INT_MAX);
    }

    return ret;
}

static int hevc_decode_extradata(HEVCContext *s, uint8_t *buf, int length, int first)
{
    int ret, i;

    ret = ff_hevc_decode_extradata(buf, length, &s->ps, &s->sei, &s->is_nalff,
                                   &s->nal_length_size, s->avctx->err_recognition,
                                   s->apply_defdispwin, s->avctx);
    if (ret < 0)
        return ret;

    /* export stream parameters from the first SPS */
    for (i = 0; i < FF_ARRAY_ELEMS(s->ps.sps_list); i++) {
        if (first && s->ps.sps_list[i]) {
            const HEVCSPS *sps = s->ps.sps_list[i];
            export_stream_params(s, sps);

            ret = export_multilayer(s, sps->vps);
            if (ret < 0)
                return ret;

            break;
        }
    }

    /* export stream parameters from SEI */
    ret = export_stream_params_from_sei(s);
    if (ret < 0)
        return ret;

    return 0;
}

static int hevc_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    HEVCContext        *s = avctx->priv_data;
    AVCodecInternal *avci = avctx->internal;
    AVPacket       *avpkt = avci->in_pkt;

    int ret;
    uint8_t *sd;
    size_t sd_size;

    s->pkt_dts = AV_NOPTS_VALUE;

    if (ff_container_fifo_can_read(s->output_fifo))
        goto do_output;

    av_packet_unref(avpkt);
    ret = ff_decode_get_packet(avctx, avpkt);
    if (ret == AVERROR_EOF) {
        ret = ff_hevc_output_frames(s, s->layers_active_decode,
                                    s->layers_active_output, 0, 0, 0);
        if (ret < 0)
            return ret;
        goto do_output;
    } else if (ret < 0)
        return ret;

    s->pkt_dts = avpkt->dts;

    sd = av_packet_get_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA, &sd_size);
    if (sd && sd_size > 0) {
        ret = hevc_decode_extradata(s, sd, sd_size, 0);
        if (ret < 0)
            return ret;
    }

    sd = av_packet_get_side_data(avpkt, AV_PKT_DATA_DOVI_CONF, &sd_size);
    if (sd && sd_size >= sizeof(s->dovi_ctx.cfg)) {
        int old = s->dovi_ctx.cfg.dv_profile;
        s->dovi_ctx.cfg = *(AVDOVIDecoderConfigurationRecord *) sd;
        if (old)
            av_log(avctx, AV_LOG_DEBUG,
                   "New DOVI configuration record from input packet (profile %d -> %u).\n",
                   old, s->dovi_ctx.cfg.dv_profile);
    }

    ret    = decode_nal_units(s, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

do_output:
    if (ff_container_fifo_read(s->output_fifo, frame) >= 0) {
        if (!(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN))
            av_frame_remove_side_data(frame, AV_FRAME_DATA_FILM_GRAIN_PARAMS);

        return 0;
    }

    return avci->draining ? AVERROR_EOF : AVERROR(EAGAIN);
}

static int hevc_ref_frame(HEVCFrame *dst, const HEVCFrame *src)
{
    int ret;

    ff_progress_frame_ref(&dst->tf, &src->tf);

    if (src->needs_fg) {
        ret = av_frame_ref(dst->frame_grain, src->frame_grain);
        if (ret < 0) {
            ff_hevc_unref_frame(dst, ~0);
            return ret;
        }
        dst->needs_fg = 1;
    }

    dst->pps     = ff_refstruct_ref_c(src->pps);
    dst->tab_mvf = ff_refstruct_ref(src->tab_mvf);
    dst->rpl_tab = ff_refstruct_ref(src->rpl_tab);
    dst->rpl = ff_refstruct_ref(src->rpl);
    dst->nb_rpl_elems = src->nb_rpl_elems;

    dst->poc        = src->poc;
    dst->ctb_count  = src->ctb_count;
    dst->flags      = src->flags;

    dst->base_layer_frame = src->base_layer_frame;

    ff_refstruct_replace(&dst->hwaccel_picture_private,
                          src->hwaccel_picture_private);

    return 0;
}

static av_cold int hevc_decode_free(AVCodecContext *avctx)
{
    HEVCContext       *s = avctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->layers); i++) {
        pic_arrays_free(&s->layers[i]);
        ff_refstruct_unref(&s->layers[i].sps);
    }

    ff_refstruct_unref(&s->vps);
    ff_refstruct_unref(&s->pps);

    ff_dovi_ctx_unref(&s->dovi_ctx);
    av_buffer_unref(&s->rpu_buf);

    av_freep(&s->md5_ctx);

    ff_container_fifo_free(&s->output_fifo);

    for (int layer = 0; layer < FF_ARRAY_ELEMS(s->layers); layer++) {
        HEVCLayerContext *l = &s->layers[layer];
        for (int i = 0; i < FF_ARRAY_ELEMS(l->DPB); i++) {
            ff_hevc_unref_frame(&l->DPB[i], ~0);
            av_frame_free(&l->DPB[i].frame_grain);
        }
    }

    ff_hevc_ps_uninit(&s->ps);

    for (int i = 0; i < s->nb_wpp_progress; i++)
        ff_thread_progress_destroy(&s->wpp_progress[i]);
    av_freep(&s->wpp_progress);

    av_freep(&s->sh.entry_point_offset);
    av_freep(&s->sh.offset);
    av_freep(&s->sh.size);

    av_freep(&s->local_ctx);

    ff_h2645_packet_uninit(&s->pkt);

    ff_hevc_reset_sei(&s->sei);

    return 0;
}

static av_cold int hevc_init_context(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;

    s->avctx = avctx;

    s->local_ctx = av_mallocz(sizeof(*s->local_ctx));
    if (!s->local_ctx)
        return AVERROR(ENOMEM);
    s->nb_local_ctx = 1;

    s->local_ctx[0].parent = s;
    s->local_ctx[0].logctx = avctx;
    s->local_ctx[0].common_cabac_state = &s->cabac;

    s->output_fifo = ff_container_fifo_alloc_avframe(0);
    if (!s->output_fifo)
        return AVERROR(ENOMEM);

    for (int layer = 0; layer < FF_ARRAY_ELEMS(s->layers); layer++) {
        HEVCLayerContext *l = &s->layers[layer];
        for (int i = 0; i < FF_ARRAY_ELEMS(l->DPB); i++) {
            l->DPB[i].frame_grain = av_frame_alloc();
            if (!l->DPB[i].frame_grain)
                return AVERROR(ENOMEM);
        }
    }

    s->md5_ctx = av_md5_alloc();
    if (!s->md5_ctx)
        return AVERROR(ENOMEM);

    ff_bswapdsp_init(&s->bdsp);

    s->dovi_ctx.logctx = avctx;
    s->eos = 0;

    ff_hevc_reset_sei(&s->sei);

    return 0;
}

#if HAVE_THREADS
static int hevc_update_thread_context(AVCodecContext *dst,
                                      const AVCodecContext *src)
{
    HEVCContext *s  = dst->priv_data;
    HEVCContext *s0 = src->priv_data;
    int ret;

    for (int layer = 0; layer < FF_ARRAY_ELEMS(s->layers); layer++) {
        HEVCLayerContext        *l = &s->layers[layer];
        const HEVCLayerContext *l0 = &s0->layers[layer];
        for (int i = 0; i < FF_ARRAY_ELEMS(l->DPB); i++) {
            ff_hevc_unref_frame(&l->DPB[i], ~0);
            if (l0->DPB[i].f) {
                ret = hevc_ref_frame(&l->DPB[i], &l0->DPB[i]);
                if (ret < 0)
                    return ret;
            }
        }

        if (l->sps != l0->sps) {
            ret = set_sps(s, l, l0->sps);
            if (ret < 0)
                return ret;
        }
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ps.vps_list); i++)
        ff_refstruct_replace(&s->ps.vps_list[i], s0->ps.vps_list[i]);

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ps.sps_list); i++)
        ff_refstruct_replace(&s->ps.sps_list[i], s0->ps.sps_list[i]);

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ps.pps_list); i++)
        ff_refstruct_replace(&s->ps.pps_list[i], s0->ps.pps_list[i]);

    // PPS do not persist between frames
    ff_refstruct_unref(&s->pps);

    s->poc_tid0   = s0->poc_tid0;
    s->eos        = s0->eos;
    s->no_rasl_output_flag = s0->no_rasl_output_flag;

    s->is_nalff        = s0->is_nalff;
    s->nal_length_size = s0->nal_length_size;
    s->layers_active_decode = s0->layers_active_decode;
    s->layers_active_output = s0->layers_active_output;

    s->film_grain_warning_shown = s0->film_grain_warning_shown;

    if (s->nb_view_ids != s0->nb_view_ids ||
        memcmp(s->view_ids, s0->view_ids, sizeof(*s->view_ids) * s->nb_view_ids)) {
        av_freep(&s->view_ids);
        s->nb_view_ids = 0;

        if (s0->nb_view_ids) {
            s->view_ids = av_memdup(s0->view_ids, s0->nb_view_ids * sizeof(*s0->view_ids));
            if (!s->view_ids)
                return AVERROR(ENOMEM);
            s->nb_view_ids = s0->nb_view_ids;
        }
    }

    ret = ff_h2645_sei_ctx_replace(&s->sei.common, &s0->sei.common);
    if (ret < 0)
        return ret;

    ret = av_buffer_replace(&s->sei.common.dynamic_hdr_plus.info,
                            s0->sei.common.dynamic_hdr_plus.info);
    if (ret < 0)
        return ret;

    ret = av_buffer_replace(&s->rpu_buf, s0->rpu_buf);
    if (ret < 0)
        return ret;

    ff_dovi_ctx_replace(&s->dovi_ctx, &s0->dovi_ctx);

    ret = av_buffer_replace(&s->sei.common.dynamic_hdr_vivid.info,
                            s0->sei.common.dynamic_hdr_vivid.info);
    if (ret < 0)
        return ret;

    s->sei.common.frame_packing        = s0->sei.common.frame_packing;
    s->sei.common.display_orientation  = s0->sei.common.display_orientation;
    s->sei.common.alternative_transfer = s0->sei.common.alternative_transfer;
    s->sei.common.mastering_display    = s0->sei.common.mastering_display;
    s->sei.common.content_light        = s0->sei.common.content_light;
    s->sei.tdrdi                       = s0->sei.tdrdi;

    return 0;
}
#endif

static av_cold int hevc_decode_init(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    int ret;

    ret = hevc_init_context(avctx);
    if (ret < 0)
        return ret;

    s->sei.picture_timing.picture_struct = 0;
    s->eos = 1;

    atomic_init(&s->wpp_err, 0);

    if (!avctx->internal->is_copy) {
        const AVPacketSideData *sd;

        if (avctx->extradata_size > 0 && avctx->extradata) {
            ret = hevc_decode_extradata(s, avctx->extradata, avctx->extradata_size, 1);
            if (ret < 0) {
                return ret;
            }

            ret = ff_h2645_sei_to_context(avctx, &s->sei.common);
            if (ret < 0)
                return ret;
        }

        sd = ff_get_coded_side_data(avctx, AV_PKT_DATA_DOVI_CONF);
        if (sd && sd->size >= sizeof(s->dovi_ctx.cfg))
            s->dovi_ctx.cfg = *(AVDOVIDecoderConfigurationRecord *) sd->data;
    }

    return 0;
}

static void hevc_decode_flush(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    ff_hevc_flush_dpb(s);
    ff_hevc_reset_sei(&s->sei);
    ff_dovi_ctx_flush(&s->dovi_ctx);
    av_buffer_unref(&s->rpu_buf);
    s->eos = 1;

    if (FF_HW_HAS_CB(avctx, flush))
        FF_HW_SIMPLE_CALL(avctx, flush);
}

#define OFFSET(x) offsetof(HEVCContext, x)
#define PAR (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption options[] = {
    { "apply_defdispwin", "Apply default display window from VUI", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, PAR },
    { "strict-displaywin", "stricly apply default display window size", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, PAR },
    { "view_ids", "Array of view IDs that should be decoded and output; a single -1 to decode all views",
        .offset = OFFSET(view_ids), .type = AV_OPT_TYPE_INT | AV_OPT_TYPE_FLAG_ARRAY,
        .min = -1, .max = INT_MAX, .flags = PAR },
    { "view_ids_available", "Array of available view IDs is exported here",
        .offset = OFFSET(view_ids_available), .type = AV_OPT_TYPE_UINT | AV_OPT_TYPE_FLAG_ARRAY,
        .flags = PAR | AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY },
    { "view_pos_available", "Array of view positions for view_ids_available is exported here, as AVStereo3DView",
        .offset = OFFSET(view_pos_available), .type = AV_OPT_TYPE_UINT | AV_OPT_TYPE_FLAG_ARRAY,
        .flags = PAR | AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY, .unit = "view_pos" },
        { "unspecified", .type = AV_OPT_TYPE_CONST, .default_val = { .i64 = AV_STEREO3D_VIEW_UNSPEC }, .unit = "view_pos" },
        { "left",        .type = AV_OPT_TYPE_CONST, .default_val = { .i64 = AV_STEREO3D_VIEW_LEFT },   .unit = "view_pos" },
        { "right",       .type = AV_OPT_TYPE_CONST, .default_val = { .i64 = AV_STEREO3D_VIEW_RIGHT },  .unit = "view_pos" },

    { NULL },
};

static const AVClass hevc_decoder_class = {
    .class_name = "HEVC decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_hevc_decoder = {
    .p.name                = "hevc",
    CODEC_LONG_NAME("HEVC (High Efficiency Video Coding)"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_HEVC,
    .priv_data_size        = sizeof(HEVCContext),
    .p.priv_class          = &hevc_decoder_class,
    .init                  = hevc_decode_init,
    .close                 = hevc_decode_free,
    FF_CODEC_RECEIVE_FRAME_CB(hevc_receive_frame),
    .flush                 = hevc_decode_flush,
    UPDATE_THREAD_CONTEXT(hevc_update_thread_context),
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                             AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal         = FF_CODEC_CAP_EXPORTS_CROPPING |
                             FF_CODEC_CAP_USES_PROGRESSFRAMES |
                             FF_CODEC_CAP_INIT_CLEANUP,
    .p.profiles            = NULL_IF_CONFIG_SMALL(ff_hevc_profiles),
    .hw_configs            = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_HEVC_DXVA2_HWACCEL
                               HWACCEL_DXVA2(hevc),
#endif
#if CONFIG_HEVC_D3D11VA_HWACCEL
                               HWACCEL_D3D11VA(hevc),
#endif
#if CONFIG_HEVC_D3D11VA2_HWACCEL
                               HWACCEL_D3D11VA2(hevc),
#endif
#if CONFIG_HEVC_D3D12VA_HWACCEL
                               HWACCEL_D3D12VA(hevc),
#endif
#if CONFIG_HEVC_NVDEC_HWACCEL
                               HWACCEL_NVDEC(hevc),
#endif
#if CONFIG_HEVC_VAAPI_HWACCEL
                               HWACCEL_VAAPI(hevc),
#endif
#if CONFIG_HEVC_VDPAU_HWACCEL
                               HWACCEL_VDPAU(hevc),
#endif
#if CONFIG_HEVC_VIDEOTOOLBOX_HWACCEL
                               HWACCEL_VIDEOTOOLBOX(hevc),
#endif
#if CONFIG_HEVC_VULKAN_HWACCEL
                               HWACCEL_VULKAN(hevc),
#endif
                               NULL
                           },
};
