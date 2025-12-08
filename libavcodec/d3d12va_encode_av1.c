/*
 * Direct3D 12 HW acceleration video encoder
 *
 * Copyright (c) 2024 Intel Corporation
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

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "config_components.h"
#include "avcodec.h"
#include "cbs.h"
#include "cbs_av1.h"
#include "av1_levels.h"
#include "codec_internal.h"
#include "d3d12va_encode.h"
#include "encode.h"
#include "hw_base_encode.h"

#include <d3d12.h>
#include <d3d12video.h>

#ifndef D3D12_VIDEO_ENCODER_AV1_INVALID_DPB_RESOURCE_INDEX
#define	D3D12_VIDEO_ENCODER_AV1_INVALID_DPB_RESOURCE_INDEX	( 0xff )
#endif

typedef struct D3D12VAHWBaseEncodeAV1 {
    AV1RawOBU    raw_sequence_header;
    AV1RawOBU       raw_frame_header;
    AV1RawOBU         raw_tile_group;
} D3D12VAHWBaseEncodeAV1;

typedef struct D3D12VAHWBaseEncodeAV1Opts {
    int                 tier; // 0: Main tier, 1: High tier
    int                level; // AV1 level (2.0-7.3 map to 0-23)

    int          enable_cdef; // Constrained Directional Enhancement Filter
    int   enable_restoration; // loop restoration
    int      enable_superres; // super-resolution
    int enable_ref_frame_mvs;

    int            enable_jnt_comp;
    int  enable_128x128_superblock;

    int       enable_warped_motion;
    int   enable_intra_edge_filter;
    int enable_interintra_compound;
    int     enable_masked_compound;
    int        enable_filter_intra;

    int         enable_loop_filter;
    int   enable_loop_filter_delta;
    int         enable_dual_filter;

    int             enable_palette;
    int    enable_intra_block_copy;
} D3D12VAHWBaseEncodeAV1Opts;

typedef struct D3D12VAEncodeAV1Picture {
    uint8_t     temporal_id;
    uint8_t      spatial_id;
    uint8_t      show_frame;
    uint8_t      frame_type;
    uint16_t last_idr_frame;
    uint8_t            slot;
} D3D12VAEncodeAV1Picture;

typedef struct D3D12VAEncodeAV1Context {
    D3D12VAEncodeContext common;
    // User options.
    int      qp;
    int profile;
    int   level;
    int    tier;

    uint8_t q_idx_idr;
    uint8_t   q_idx_p;

    // Writer structures.
    D3D12VAHWBaseEncodeAV1         units;
    D3D12VAHWBaseEncodeAV1Opts unit_opts;

    CodedBitstreamContext              *cbc;
    CodedBitstreamFragment      current_obu;
    D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAGS post_encode_values_flag;
    AVFifo             *picture_header_list;
} D3D12VAEncodeAV1Context;

typedef struct D3D12VAEncodeAV1Level {
    uint8_t                              level;
    D3D12_VIDEO_ENCODER_AV1_LEVELS d3d12_level;
} D3D12VAEncodeAV1Level;


static const D3D12VAEncodeAV1Level av1_levels[] = {
    { 0,  D3D12_VIDEO_ENCODER_AV1_LEVELS_2_0 },
    { 1,  D3D12_VIDEO_ENCODER_AV1_LEVELS_2_1 },
    { 2,  D3D12_VIDEO_ENCODER_AV1_LEVELS_2_2 },
    { 3,  D3D12_VIDEO_ENCODER_AV1_LEVELS_2_3 },
    { 4,  D3D12_VIDEO_ENCODER_AV1_LEVELS_3_0 },
    { 5,  D3D12_VIDEO_ENCODER_AV1_LEVELS_3_1 },
    { 6,  D3D12_VIDEO_ENCODER_AV1_LEVELS_3_2 },
    { 7,  D3D12_VIDEO_ENCODER_AV1_LEVELS_3_3 },
    { 8,  D3D12_VIDEO_ENCODER_AV1_LEVELS_4_0 },
    { 9,  D3D12_VIDEO_ENCODER_AV1_LEVELS_4_1 },
    { 10, D3D12_VIDEO_ENCODER_AV1_LEVELS_4_2 },
    { 11, D3D12_VIDEO_ENCODER_AV1_LEVELS_4_3 },
    { 12, D3D12_VIDEO_ENCODER_AV1_LEVELS_5_0 },
    { 13, D3D12_VIDEO_ENCODER_AV1_LEVELS_5_1 },
    { 14, D3D12_VIDEO_ENCODER_AV1_LEVELS_5_2 },
    { 15, D3D12_VIDEO_ENCODER_AV1_LEVELS_5_3 },
    { 16, D3D12_VIDEO_ENCODER_AV1_LEVELS_6_0 },
    { 17, D3D12_VIDEO_ENCODER_AV1_LEVELS_6_1 },
    { 18, D3D12_VIDEO_ENCODER_AV1_LEVELS_6_2 },
    { 19, D3D12_VIDEO_ENCODER_AV1_LEVELS_6_3 },
    { 20, D3D12_VIDEO_ENCODER_AV1_LEVELS_7_0 },
    { 21, D3D12_VIDEO_ENCODER_AV1_LEVELS_7_1 },
    { 22, D3D12_VIDEO_ENCODER_AV1_LEVELS_7_2 },
    { 23, D3D12_VIDEO_ENCODER_AV1_LEVELS_7_3 },
};

static const D3D12_VIDEO_ENCODER_AV1_PROFILE         profile_main = D3D12_VIDEO_ENCODER_AV1_PROFILE_MAIN;
static const D3D12_VIDEO_ENCODER_AV1_PROFILE         profile_high = D3D12_VIDEO_ENCODER_AV1_PROFILE_HIGH;
static const D3D12_VIDEO_ENCODER_AV1_PROFILE profile_professional = D3D12_VIDEO_ENCODER_AV1_PROFILE_PROFESSIONAL;

#define D3D_PROFILE_DESC(name) \
    { sizeof(D3D12_VIDEO_ENCODER_AV1_PROFILE), { .pAV1Profile = (D3D12_VIDEO_ENCODER_AV1_PROFILE *)&profile_ ## name } }
static const D3D12VAEncodeProfile d3d12va_encode_av1_profiles[] = {
    { AV_PROFILE_AV1_MAIN,          8, 3, 1, 1, D3D_PROFILE_DESC(main)         },
    { AV_PROFILE_AV1_MAIN,         10, 3, 1, 1, D3D_PROFILE_DESC(main)         },
    { AV_PROFILE_AV1_HIGH,         10, 3, 1, 1, D3D_PROFILE_DESC(high)         },
    { AV_PROFILE_AV1_PROFESSIONAL,  8, 3, 1, 1, D3D_PROFILE_DESC(professional) },
    { AV_PROFILE_AV1_PROFESSIONAL, 10, 3, 1, 1, D3D_PROFILE_DESC(professional) },
    { AV_PROFILE_AV1_PROFESSIONAL, 12, 3, 1, 1, D3D_PROFILE_DESC(professional) },
    { AV_PROFILE_UNKNOWN },
};

static int d3d12va_encode_av1_write_obu(AVCodecContext *avctx,
                                        char *data, size_t *data_len,
                                        CodedBitstreamFragment *obu)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    int                       err = 0;

    err = ff_cbs_write_fragment_data(priv->cbc, obu);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed OBU data.\n");
        return err;
    }

    memcpy(data, obu->data, obu->data_size);
    *data_len = (8 * obu->data_size) - obu->data_bit_padding;

    return 0;
}

static int d3d12va_encode_av1_add_obu(AVCodecContext* avctx,
                                      CodedBitstreamFragment* au,
                                      CodedBitstreamUnitType obu_type,
                                      void* obu_unit)
{
    int err = 0;

    err = ff_cbs_insert_unit_content(au, -1, obu_type, obu_unit, NULL);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add OBU unit: "
            "type = %d.\n", obu_type);
        return err;
    }
    return 0;
}

static int d3d12va_encode_av1_write_sequence_header(AVCodecContext *avctx,
                                                    char *data, size_t *data_len)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    CodedBitstreamFragment   *obu = &priv->current_obu;
    int                       err = 0;

    priv->units.raw_sequence_header.header.obu_type = AV1_OBU_SEQUENCE_HEADER;
    err = d3d12va_encode_av1_add_obu(avctx, obu, AV1_OBU_SEQUENCE_HEADER, &priv->units.raw_sequence_header);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_av1_write_obu(avctx, data, data_len, obu);

fail:
    ff_cbs_fragment_reset(obu);
    return err;
}

static int d3d12va_encode_av1_update_current_frame_picture_header(AVCodecContext *avctx,
                                                                  D3D12VAEncodePicture *pic,
                                                                  AV1RawOBU *frameheader_obu)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    AV1RawFrameHeader         *fh = &frameheader_obu->obu.frame_header;
    uint8_t                 *data = NULL;
    HRESULT                    hr = S_OK;
    int                       err = 0;
    D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES *post_encode_values = NULL;

    // Update the frame header according to the picture post_encode_values
    hr = ID3D12Resource_Map(pic->resolved_metadata, 0, NULL, (void **)&data);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        return err;
    }
    post_encode_values = (D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES*) (data +
            sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA) +
            sizeof(D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA) +
            sizeof(D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_TILES));

    if (priv->post_encode_values_flag & D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAG_QUANTIZATION) {
        fh->base_q_idx = post_encode_values->Quantization.BaseQIndex;
        fh->delta_q_y_dc = post_encode_values->Quantization.YDCDeltaQ;
        fh->delta_q_u_dc = post_encode_values->Quantization.UDCDeltaQ;
        fh->delta_q_u_ac = post_encode_values->Quantization.UACDeltaQ;
        fh->delta_q_v_dc = post_encode_values->Quantization.VDCDeltaQ;
        fh->delta_q_v_ac = post_encode_values->Quantization.VACDeltaQ;
        fh->using_qmatrix = post_encode_values->Quantization.UsingQMatrix;
        fh->qm_y = post_encode_values->Quantization.QMY;
        fh->qm_u = post_encode_values->Quantization.QMU;
        fh->qm_v = post_encode_values->Quantization.QMV;
    }

    if (priv->post_encode_values_flag & D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAG_LOOP_FILTER) {
        fh->loop_filter_level[0] = post_encode_values->LoopFilter.LoopFilterLevel[0];
        fh->loop_filter_level[1] = post_encode_values->LoopFilter.LoopFilterLevel[1];
        fh->loop_filter_level[2] = post_encode_values->LoopFilter.LoopFilterLevelU;
        fh->loop_filter_level[3] = post_encode_values->LoopFilter.LoopFilterLevelV;
        fh->loop_filter_sharpness = post_encode_values->LoopFilter.LoopFilterSharpnessLevel;
        fh->loop_filter_delta_enabled = post_encode_values->LoopFilter.LoopFilterDeltaEnabled;
        if (fh->loop_filter_delta_enabled) {
            for (int i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++) {
                fh->loop_filter_ref_deltas[i] = post_encode_values->LoopFilter.RefDeltas[i];
                fh->update_ref_delta[i]       = post_encode_values->LoopFilter.RefDeltas[i];
            }
            for (int i = 0; i < 2; i++) {
                fh->loop_filter_mode_deltas[i] = post_encode_values->LoopFilter.ModeDeltas[i];
                fh->update_mode_delta[i]       = post_encode_values->LoopFilter.ModeDeltas[i];
            }
        }
    }
    if (priv->post_encode_values_flag & D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAG_CDEF_DATA) {
        fh->cdef_damping_minus_3 = post_encode_values->CDEF.CdefDampingMinus3;
        fh->cdef_bits = post_encode_values->CDEF.CdefBits;
        for (int i = 0; i < 8; i++) {
            fh->cdef_y_pri_strength[i]  = post_encode_values->CDEF.CdefYPriStrength[i];
            fh->cdef_y_sec_strength[i]  = post_encode_values->CDEF.CdefYSecStrength[i];
            fh->cdef_uv_pri_strength[i] = post_encode_values->CDEF.CdefUVPriStrength[i];
            fh->cdef_uv_sec_strength[i] = post_encode_values->CDEF.CdefUVSecStrength[i];
        }
    }
    if (priv->post_encode_values_flag & D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAG_QUANTIZATION_DELTA) {
        fh->delta_q_present = post_encode_values->QuantizationDelta.DeltaQPresent;
        fh->delta_q_res = post_encode_values->QuantizationDelta.DeltaQRes;
    }

    if (priv->post_encode_values_flag & D3D12_VIDEO_ENCODER_AV1_POST_ENCODE_VALUES_FLAG_REFERENCE_INDICES) {
        for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
            fh->ref_frame_idx[i] = post_encode_values->ReferenceIndices[i];
        }
    }

    ID3D12Resource_Unmap(pic->resolved_metadata, 0, NULL);
    return 0;
}

static int d3d12va_encode_av1_write_picture_header(AVCodecContext *avctx,
                                                   D3D12VAEncodePicture *pic,
                                                   char *data, size_t *data_len)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    CodedBitstreamFragment  *obu  = &priv->current_obu;
    AV1RawOBU    *frameheader_obu = av_mallocz(sizeof(AV1RawOBU));
    int                       err = 0;

    av_fifo_read(priv->picture_header_list, frameheader_obu, 1);
    err = d3d12va_encode_av1_update_current_frame_picture_header(avctx, pic,frameheader_obu);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to update current frame picture header: %d.\n", err);
        return err;
    }

    // Add the frame header OBU
    frameheader_obu->header.obu_has_size_field = 1;

    err = d3d12va_encode_av1_add_obu(avctx, obu, AV1_OBU_FRAME_HEADER, frameheader_obu);
    if (err < 0)
        goto fail;
    err = d3d12va_encode_av1_write_obu(avctx, data, data_len, obu);

fail:
    ff_cbs_fragment_reset(obu);
    av_freep(&frameheader_obu);
    return err;
}

static int d3d12va_encode_av1_write_tile_group(AVCodecContext *avctx,
                                               uint8_t* tile_group,
                                               uint32_t tile_group_size,
                                               char *data, size_t *data_len)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    CodedBitstreamFragment  *obu  = &priv->current_obu;
    AV1RawOBU     *tile_group_obu = &priv->units.raw_tile_group;
    AV1RawTileGroup           *tg = &tile_group_obu->obu.tile_group;
    int                       err = 0;

    tg->tile_data.data = tile_group;
    tg->tile_data.data_ref = NULL;
    tg->tile_data.data_size = tile_group_size;
    tile_group_obu->header.obu_has_size_field = 1;
    tile_group_obu->header.obu_type = AV1_OBU_TILE_GROUP;

    err = d3d12va_encode_av1_add_obu(avctx, obu, AV1_OBU_TILE_GROUP, tile_group_obu);
    if (err < 0)
        goto fail;
    err = d3d12va_encode_av1_write_obu(avctx, data, data_len, obu);

fail:
    ff_cbs_fragment_reset(obu);
    return err;
}

static int d3d12va_encode_av1_get_buffer_size(AVCodecContext *avctx,
                                              D3D12VAEncodePicture *pic, size_t *size)
{
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *subregion_meta = NULL;
    uint8_t                                                *data = NULL;
    HRESULT                                                   hr = S_OK;
    int                                                      err = 0;

    hr = ID3D12Resource_Map(pic->resolved_metadata, 0, NULL, (void **)&data);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        return err;
    }

    subregion_meta = (D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA*)(data + sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA));
    if (subregion_meta->bSize == 0) {
        av_log(avctx, AV_LOG_ERROR, "No subregion metadata found\n");
        err = AVERROR(EINVAL);
        return err;
    }
    *size = subregion_meta->bSize;

    ID3D12Resource_Unmap(pic->resolved_metadata, 0, NULL);

    return 0;
}

static int d3d12va_encode_av1_get_coded_data(AVCodecContext *avctx,
                                             D3D12VAEncodePicture *pic, AVPacket *pkt)
{
    int                   err = 0;
    uint8_t              *ptr = NULL;
    uint8_t      *mapped_data = NULL;
    size_t         total_size = 0;
    HRESULT                hr = S_OK;
    size_t    av1_pic_hd_size = 0;
    int tile_group_extra_size = 0;
    size_t            bit_len = 0;

    char pic_hd_data[MAX_PARAM_BUFFER_SIZE] = { 0 };

    err = d3d12va_encode_av1_get_buffer_size(avctx, pic, &total_size);
    if (err < 0)
        goto end;

    // Update the picture header and calculate the picture header size
    memset(pic_hd_data, 0, sizeof(pic_hd_data));
    err = d3d12va_encode_av1_write_picture_header(avctx, pic, pic_hd_data, &av1_pic_hd_size);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write picture header: %d.\n", err);
        return err;
    }
    av1_pic_hd_size /= 8;
    av_log(avctx, AV_LOG_DEBUG, "AV1 picture header size: %zu bytes.\n", av1_pic_hd_size);


    tile_group_extra_size = (av_log2(total_size) + 7) / 7 + 1; // 1 byte for obu header, rest for tile group LEB128 size
    av_log(avctx, AV_LOG_DEBUG, "Tile group extra size: %d bytes.\n", tile_group_extra_size);

    total_size += (pic->header_size + tile_group_extra_size + av1_pic_hd_size);
    av_log(avctx, AV_LOG_DEBUG, "Output buffer size %zu\n", total_size);

    hr = ID3D12Resource_Map(pic->output_buffer, 0, NULL, (void **)&mapped_data);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto end;
    }

    err = ff_get_encode_buffer(avctx, pkt, total_size, 0);
    if (err < 0)
        goto end;
    ptr = pkt->data;

    memcpy(ptr, mapped_data, pic->header_size);

    ptr += pic->header_size;
    mapped_data += pic->aligned_header_size;
    total_size -= pic->header_size;

    memcpy(ptr, pic_hd_data, av1_pic_hd_size);
    ptr += av1_pic_hd_size;
    total_size -= av1_pic_hd_size;
    av_log(avctx, AV_LOG_DEBUG, "AV1 total_size after write picture header: %zu.\n", total_size);

    total_size -= tile_group_extra_size;
    err = d3d12va_encode_av1_write_tile_group(avctx, mapped_data, total_size, ptr, &bit_len);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write tile group: %d.\n", err);
        goto end;
    }
    assert((total_size + tile_group_extra_size) * 8 == bit_len);

    ID3D12Resource_Unmap(pic->output_buffer, 0, NULL);

end:
    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = NULL;
    return err;
}

static int d3d12va_hw_base_encode_init_params_av1(FFHWBaseEncodeContext *base_ctx,
                                                  AVCodecContext *avctx,
                                                  D3D12VAHWBaseEncodeAV1 *common,
                                                  D3D12VAHWBaseEncodeAV1Opts *opts)
{
    AV1RawOBU      *seqheader_obu = &common->raw_sequence_header;
    AV1RawSequenceHeader     *seq = &seqheader_obu->obu.sequence_header;
    const AVPixFmtDescriptor *desc;

    seq->seq_profile = avctx->profile;
    if (!seq->seq_force_screen_content_tools)
        seq->seq_force_integer_mv = AV1_SELECT_INTEGER_MV;
    seq->seq_tier[0] = opts->tier;

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    seq->color_config = (AV1RawColorConfig){
        .high_bitdepth = desc->comp[0].depth == 8 ? 0 : 1,
        .color_primaries = avctx->color_primaries,
        .transfer_characteristics = avctx->color_trc,
        .matrix_coefficients = avctx->colorspace,
        .color_description_present_flag = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                           avctx->color_trc != AVCOL_TRC_UNSPECIFIED ||
                                           avctx->colorspace != AVCOL_SPC_UNSPECIFIED),
        .color_range = avctx->color_range == AVCOL_RANGE_JPEG,
        .subsampling_x = desc->log2_chroma_w,
        .subsampling_y = desc->log2_chroma_h,
    };

    switch (avctx->chroma_sample_location) {
    case AVCHROMA_LOC_LEFT:
        seq->color_config.chroma_sample_position = AV1_CSP_VERTICAL;
        break;
    case AVCHROMA_LOC_TOPLEFT:
        seq->color_config.chroma_sample_position = AV1_CSP_COLOCATED;
        break;
    default:
        seq->color_config.chroma_sample_position = AV1_CSP_UNKNOWN;
        break;
    }

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        seq->seq_level_idx[0] = avctx->level;
    }
    else {
        const AV1LevelDescriptor *level;
        float framerate;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = avctx->framerate.num / avctx->framerate.den;
        else
            framerate = 0;

        //currently only supporting 1 tile
        level = ff_av1_guess_level(avctx->bit_rate, opts->tier,
            base_ctx->surface_width, base_ctx->surface_height,
            /*priv->tile_rows*/1 * 1/*priv->tile_cols*/,
            /*priv->tile_cols*/1, framerate);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            seq->seq_level_idx[0] = level->level_idx;
        }
        else {
            av_log(avctx, AV_LOG_VERBOSE, "Stream will not conform to "
                "any normal level, using maximum parameters level by default.\n");
            seq->seq_level_idx[0] = 31;
            seq->seq_tier[0] = 1;
        }
    }

    // Still picture mode
    seq->still_picture = (base_ctx->gop_size == 1);
    seq->reduced_still_picture_header = seq->still_picture;

    // Feature flags
    seq->enable_filter_intra = opts->enable_filter_intra;
    seq->enable_intra_edge_filter = opts->enable_intra_edge_filter;
    seq->enable_interintra_compound = opts->enable_interintra_compound;
    seq->enable_masked_compound = opts->enable_masked_compound;
    seq->enable_warped_motion = opts->enable_warped_motion;
    seq->enable_dual_filter = opts->enable_dual_filter;
    seq->enable_order_hint = !seq->still_picture;
    if (seq->enable_order_hint) {
        seq->order_hint_bits_minus_1 = 7;
    }
    seq->enable_jnt_comp = opts->enable_jnt_comp && seq->enable_order_hint;
    seq->enable_ref_frame_mvs = opts->enable_ref_frame_mvs && seq->enable_order_hint;
    seq->enable_superres = opts->enable_superres;
    seq->enable_cdef = opts->enable_cdef;
    seq->enable_restoration = opts->enable_restoration;

    return 0;

}

static int d3d12va_encode_av1_init_sequence_params(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext      *ctx  = avctx->priv_data;
    D3D12VAEncodeAV1Context   *priv = avctx->priv_data;
    AVD3D12VAFramesContext   *hwctx = base_ctx->input_frames->hwctx;
    AV1RawOBU        *seqheader_obu = &priv->units.raw_sequence_header;
    AV1RawSequenceHeader       *seq = &priv->units.raw_sequence_header.obu.sequence_header;

    D3D12_VIDEO_ENCODER_AV1_PROFILE profile = D3D12_VIDEO_ENCODER_AV1_PROFILE_MAIN;
    D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS level = { 0 };
    HRESULT hr;
    int err;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1 support = {
        .NodeIndex                        = 0,
        .Codec                            = D3D12_VIDEO_ENCODER_CODEC_AV1,
        .InputFormat                      = hwctx->format,
        .RateControl                      = ctx->rc,
        .IntraRefresh                     = ctx->intra_refresh.Mode,
        .SubregionFrameEncoding           = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME,
        .ResolutionsListCount             = 1,
        .pResolutionList                  = &ctx->resolution,
        .CodecGopSequence                 = ctx->gop,
        .MaxReferenceFramesInDPB          = AV1_NUM_REF_FRAMES,
        .CodecConfiguration               = ctx->codec_conf,
        .SuggestedProfile.DataSize        = sizeof(D3D12_VIDEO_ENCODER_AV1_PROFILE),
        .SuggestedProfile.pAV1Profile     = &profile,
        .SuggestedLevel.DataSize          = sizeof(D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS),
        .SuggestedLevel.pAV1LevelSetting  = &level,
        .pResolutionDependentSupport      = &ctx->res_limits,
        .SubregionFrameEncodingData.pTilesPartition_AV1 = ctx->subregions_layout.pTilesPartition_AV1,
    };

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1,
                                                &support, sizeof(support));

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to check encoder support(%lx).\n", (long)hr);
        return AVERROR(EINVAL);
    }

    if (!(support.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK)) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support some request D3D12VA AV1 features. %#x\n",
               support.ValidationFlags);
        return AVERROR(EINVAL);
    }

    if (support.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) {
        ctx->is_texture_array = 1;
        av_log(avctx, AV_LOG_DEBUG, "D3D12 video encode on this device uses texture array mode.\n");
    }

    memset(seqheader_obu, 0, sizeof(*seqheader_obu));
    seq->seq_profile = profile;
    seq->seq_level_idx[0] = level.Level;
    seq->seq_tier[0] = level.Tier;

    seq->max_frame_width_minus_1 = ctx->resolution.Width - 1;
    seq->max_frame_height_minus_1 = ctx->resolution.Height - 1;
    seq->frame_width_bits_minus_1 = av_log2(ctx->resolution.Width);
    seq->frame_height_bits_minus_1 = av_log2(ctx->resolution.Height);

    seqheader_obu->header.obu_type = AV1_OBU_SEQUENCE_HEADER;

    err = d3d12va_hw_base_encode_init_params_av1(base_ctx, avctx,
                                                 &priv->units, &priv->unit_opts);
    if (err < 0)
        return err;

    if (avctx->level == AV_LEVEL_UNKNOWN)
        avctx->level = level.Level;

    return 0;
}

static int d3d12va_encode_av1_get_encoder_caps(AVCodecContext *avctx)
{
    HRESULT                      hr = S_OK;
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    D3D12VAEncodeAV1Context   *priv = avctx->priv_data;

    D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION *config;
    D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION_SUPPORT av1_caps;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT codec_caps = {
        .NodeIndex                   = 0,
        .Codec                       = D3D12_VIDEO_ENCODER_CODEC_AV1,
        .Profile                     = ctx->profile->d3d12_profile,
        .CodecSupportLimits.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION_SUPPORT),
    };

    codec_caps.CodecSupportLimits.pAV1Support = &av1_caps;

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
                                                &codec_caps, sizeof(codec_caps));
    if (!(SUCCEEDED(hr) && codec_caps.IsSupported))
        return AVERROR(EINVAL);

    ctx->codec_conf.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION);
    ctx->codec_conf.pAV1Config = av_mallocz(ctx->codec_conf.DataSize);
    if (!ctx->codec_conf.pAV1Config)
        return AVERROR(ENOMEM);

    priv->post_encode_values_flag = av1_caps.PostEncodeValuesFlags;
    config = ctx->codec_conf.pAV1Config;

    config->FeatureFlags = D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_NONE;
    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_128x128_SUPERBLOCK) {
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_128x128_SUPERBLOCK;
        priv->unit_opts.enable_128x128_superblock = 1;
    }

    base_ctx->surface_width  = FFALIGN(avctx->width,  priv->unit_opts.enable_128x128_superblock ? 128 : 64);
    base_ctx->surface_height = FFALIGN(avctx->height, priv->unit_opts.enable_128x128_superblock ? 128 : 64);

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_LOOP_RESTORATION_FILTER) {
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_LOOP_RESTORATION_FILTER;
        priv->unit_opts.enable_loop_filter = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_PALETTE_ENCODING) {
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_PALETTE_ENCODING;
        priv->unit_opts.enable_palette = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_INTRA_BLOCK_COPY) {
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_INTRA_BLOCK_COPY;
        priv->unit_opts.enable_intra_block_copy = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_LOOP_FILTER_DELTAS) {
        // Loop filter deltas
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_LOOP_FILTER_DELTAS;
        priv->unit_opts.enable_loop_filter_delta = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_CDEF_FILTERING) {
        // CDEF (Constrained Directional Enhancement Filter)
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_CDEF_FILTERING;
        priv->unit_opts.enable_cdef = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_DUAL_FILTER) {
        // Dual filter
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_DUAL_FILTER;
        priv->unit_opts.enable_dual_filter = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_JNT_COMP) {
        // Joint compound prediction
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_JNT_COMP;
        priv->unit_opts.enable_jnt_comp = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_FRAME_REFERENCE_MOTION_VECTORS) {
        // Frame reference motion vectors
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_FRAME_REFERENCE_MOTION_VECTORS;
        priv->unit_opts.enable_ref_frame_mvs = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_SUPER_RESOLUTION) {
        // Super-resolution
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_SUPER_RESOLUTION;
        priv->unit_opts.enable_superres = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_WARPED_MOTION) {
        // Warped motion
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_WARPED_MOTION;
        priv->unit_opts.enable_warped_motion = 1;
    }

    if (av1_caps.SupportedFeatureFlags & D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_INTERINTRA_COMPOUND) {
        // Inter-intra compound prediction
        config->FeatureFlags |= D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAG_INTERINTRA_COMPOUND;
        priv->unit_opts.enable_interintra_compound = 1;
    }

    return 0;
}

static int d3d12va_encode_av1_configure(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    D3D12VAEncodeAV1Context   *priv = avctx->priv_data;
    int                         err = 0;
    int fixed_qp_key, fixed_qp_inter;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_AV1, avctx);
    if (err < 0)
        return err;

    if (ctx->rc.Mode == D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP) {
        D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP *cqp_ctl;
        fixed_qp_inter = av_clip_uintp2(ctx->rc_quality, 8);

        if (avctx->i_quant_factor > 0.0)
            fixed_qp_key = av_clip_uintp2((avctx->i_quant_factor * fixed_qp_inter +
                                    avctx->i_quant_offset) + 0.5, 8);
        else
            fixed_qp_key = fixed_qp_inter;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d for Key / Inter frames.\n",
               fixed_qp_key, fixed_qp_inter);

        ctx->rc.ConfigParams.DataSize = sizeof(D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP);
        cqp_ctl = av_mallocz(ctx->rc.ConfigParams.DataSize);
        if (!cqp_ctl)
            return AVERROR(ENOMEM);

        cqp_ctl->ConstantQP_FullIntracodedFrame                  = fixed_qp_key;
        cqp_ctl->ConstantQP_InterPredictedFrame_PrevRefOnly      = fixed_qp_inter;
        cqp_ctl->ConstantQP_InterPredictedFrame_BiDirectionalRef = fixed_qp_inter;

        ctx->rc.ConfigParams.pConfiguration_CQP = cqp_ctl;

        priv->q_idx_idr = fixed_qp_key;
        priv->q_idx_p   = fixed_qp_inter;

    }

    // GOP configuration for AV1
    ctx->gop.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_SEQUENCE_STRUCTURE);
    ctx->gop.pAV1SequenceStructure = av_mallocz(ctx->gop.DataSize);
    if (!ctx->gop.pAV1SequenceStructure)
        return AVERROR(ENOMEM);

    ctx->gop.pAV1SequenceStructure->IntraDistance = base_ctx->gop_size;
    ctx->gop.pAV1SequenceStructure->InterFramePeriod = base_ctx->b_per_p + 1;

    return 0;
}

static int d3d12va_encode_av1_set_level(AVCodecContext *avctx)
{
    D3D12VAEncodeContext     *ctx = avctx->priv_data;
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;
    int                         i = 0;

    ctx->level.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS);
    ctx->level.pAV1LevelSetting = av_mallocz(ctx->level.DataSize);
    if (!ctx->level.pAV1LevelSetting)
        return AVERROR(ENOMEM);

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        for (i = 0; i < FF_ARRAY_ELEMS(av1_levels); i++) {
            if (avctx->level == av1_levels[i].level) {
                ctx->level.pAV1LevelSetting->Level = av1_levels[i].d3d12_level;
                break;
            }
        }

        if (i == FF_ARRAY_ELEMS(av1_levels) ) {
            av_log(avctx, AV_LOG_ERROR, "Invalid AV1 level %d.\n", avctx->level);
            return AVERROR(EINVAL);
        }
    } else {
        ctx->level.pAV1LevelSetting->Level = D3D12_VIDEO_ENCODER_AV1_LEVELS_5_2;
        avctx->level = D3D12_VIDEO_ENCODER_AV1_LEVELS_5_2;
        av_log(avctx, AV_LOG_DEBUG, "Using default AV1 level 5.2\n");
    }

    if (priv->tier == 1 || avctx->bit_rate > 30000000) {
        ctx->level.pAV1LevelSetting->Tier = D3D12_VIDEO_ENCODER_AV1_TIER_HIGH;
        av_log(avctx, AV_LOG_DEBUG, "Using AV1 High tier\n");
    } else {
        ctx->level.pAV1LevelSetting->Tier = D3D12_VIDEO_ENCODER_AV1_TIER_MAIN;
        av_log(avctx, AV_LOG_DEBUG, "Using AV1 Main tier\n");
    }

    if (priv->tier >= 0) {
        ctx->level.pAV1LevelSetting->Tier = priv->tier == 0 ?
                                            D3D12_VIDEO_ENCODER_AV1_TIER_MAIN :
                                            D3D12_VIDEO_ENCODER_AV1_TIER_HIGH;
    }

    av_log(avctx, AV_LOG_DEBUG, "AV1 level set to %d, tier: %s\n",
           ctx->level.pAV1LevelSetting->Level,
           ctx->level.pAV1LevelSetting->Tier == D3D12_VIDEO_ENCODER_AV1_TIER_MAIN ? "Main" : "High");

    return 0;
}

static int d3d12va_encode_av1_set_tile(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;

    ctx->subregions_layout.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_TILES);
    D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_TILES *tiles_layout = av_mallocz(ctx->subregions_layout.DataSize);
    ctx->subregions_layout.pTilesPartition_AV1 = tiles_layout;

    // Currently only support 1 tile
    tiles_layout->RowCount = 1;
    tiles_layout->ColCount = 1;

    return 0;
}

static void d3d12va_encode_av1_free_picture_params(D3D12VAEncodePicture *pic)
{
    if (!pic->pic_ctl.pAV1PicData)
        return;

    av_freep(&pic->pic_ctl.pAV1PicData);
}

static int d3d12va_encode_av1_init_picture_params(AVCodecContext *avctx,
                                                  FFHWBaseEncodePicture *pic)
{
    FFHWBaseEncodeContext             *base_ctx = avctx->priv_data;
    D3D12VAEncodeAV1Context               *priv = avctx->priv_data;
    D3D12VAEncodeContext                   *ctx = avctx->priv_data;
    D3D12VAEncodePicture           *d3d12va_pic = pic->priv;
    D3D12VAEncodeAV1Picture               *hpic = pic->codec_priv;
    CodedBitstreamAV1Context             *cbctx = priv->cbc->priv_data;
    AV1RawOBU                  *frameheader_obu = &priv->units.raw_frame_header;
    AV1RawFrameHeader                       *fh = &frameheader_obu->obu.frame_header;

    FFHWBaseEncodePicture *ref;
    D3D12VAEncodeAV1Picture *href;
    int i;

    static const int8_t default_loop_filter_ref_deltas[AV1_TOTAL_REFS_PER_FRAME] =
        { 1, 0, 0, 0, -1, 0, -1, -1 };

    memset(frameheader_obu, 0, sizeof(*frameheader_obu));

    frameheader_obu->header.obu_type = AV1_OBU_FRAME_HEADER;

    d3d12va_pic->pic_ctl.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_CODEC_DATA);
    d3d12va_pic->pic_ctl.pAV1PicData = av_mallocz(d3d12va_pic->pic_ctl.DataSize);
    if (!d3d12va_pic->pic_ctl.pAV1PicData)
        return AVERROR(ENOMEM);

    // Initialize frame type and reference frame management
    switch(pic->type) {
        case FF_HW_PICTURE_TYPE_IDR:
            fh->frame_type = AV1_FRAME_KEY;
            fh->refresh_frame_flags = 0xFF;
            fh->base_q_idx = priv->q_idx_idr;
            hpic->slot = 0;
            hpic->last_idr_frame = pic->display_order;
            fh->tx_mode = AV1_TX_MODE_LARGEST;
            break;

        case FF_HW_PICTURE_TYPE_P:
            fh->frame_type = AV1_FRAME_INTER;
            fh->base_q_idx = priv->q_idx_p;
            fh->tx_mode = AV1_TX_MODE_SELECT;

            ref = pic->refs[0][pic->nb_refs[0] - 1];
            href = ref->codec_priv;

            /**
             * The encoder uses a simple alternating reference frame strategy:
             * - For P-frames, it uses the last reconstructed frame as a reference.
             * - To simplify the reference model of the encoder, the encoder alternates between
             * two reference frame slots (typically slot 0 and slot 1) for storing reconstructed
             * images and providing prediction references for the next frame.
             */
            if (base_ctx->ref_l0 > 1) {
                hpic->slot = !href->slot;
            } else {
                hpic->slot = 0;
            }
            hpic->last_idr_frame = href->last_idr_frame;
            fh->refresh_frame_flags = 1 << hpic->slot;

            // Set the nearest frame in L0 as all reference frame.
            for (i = 0; i < AV1_REFS_PER_FRAME; i++)
                fh->ref_frame_idx[i] = href->slot;

            fh->primary_ref_frame = href->slot;
            fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;

            // Set the 2nd nearest frame in L0 as Golden frame.
            if (pic->nb_refs[0] > 1) {
                ref = pic->refs[0][pic->nb_refs[0] - 2];
                href = ref->codec_priv;
                // Reference frame index 3 is the GOLDEN_FRAME
                fh->ref_frame_idx[3] = href->slot;
                fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;
            } else if (base_ctx->ref_l0 == 1) {
                fh->ref_order_hint[!href->slot] = cbctx->ref[!href->slot].order_hint;
            }
            break;

        case FF_HW_PICTURE_TYPE_B:
            av_log(avctx, AV_LOG_ERROR, "D3D12 AV1 video encode on this device requires B-frame support, "
                "but it's not implemented.\n");
            return AVERROR_PATCHWELCOME;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported picture type %d.\n", pic->type);
    }


    cbctx->seen_frame_header = 0;

    fh->show_frame                = pic->display_order <= pic->encode_order;
    fh->showable_frame            = fh->frame_type != AV1_FRAME_KEY;
    fh->order_hint                = pic->display_order - hpic->last_idr_frame;
    fh->frame_width_minus_1       = ctx->resolution.Width - 1;
    fh->frame_height_minus_1      = ctx->resolution.Height - 1;
    fh->render_width_minus_1      = fh->frame_width_minus_1;
    fh->render_height_minus_1     = fh->frame_height_minus_1;
    fh->is_filter_switchable      = 1;
    fh->interpolation_filter      = AV1_INTERPOLATION_FILTER_SWITCHABLE;
    fh->uniform_tile_spacing_flag = 1;
    fh->width_in_sbs_minus_1[0]   = (ctx->resolution.Width  + 63 >> 6) -1; // 64x64 superblock size
    fh->height_in_sbs_minus_1[0]  = (ctx->resolution.Height + 63 >> 6) -1; // 64x64 superblock size

    memcpy(fh->loop_filter_ref_deltas, default_loop_filter_ref_deltas,
           AV1_TOTAL_REFS_PER_FRAME * sizeof(int8_t));

    if (fh->frame_type == AV1_FRAME_KEY && fh->show_frame)
        fh->error_resilient_mode = 1;

    if (fh->frame_type == AV1_FRAME_KEY || fh->error_resilient_mode)
        fh->primary_ref_frame = AV1_PRIMARY_REF_NONE;

    d3d12va_pic->pic_ctl.pAV1PicData->FrameType = fh->frame_type;
    d3d12va_pic->pic_ctl.pAV1PicData->TxMode = fh->tx_mode;
    d3d12va_pic->pic_ctl.pAV1PicData->RefreshFrameFlags = fh->refresh_frame_flags;
    d3d12va_pic->pic_ctl.pAV1PicData->TemporalLayerIndexPlus1 = hpic->temporal_id + 1;
    d3d12va_pic->pic_ctl.pAV1PicData->SpatialLayerIndexPlus1 = hpic->spatial_id + 1;
    d3d12va_pic->pic_ctl.pAV1PicData->PictureIndex = pic->display_order;
    d3d12va_pic->pic_ctl.pAV1PicData->InterpolationFilter = D3D12_VIDEO_ENCODER_AV1_INTERPOLATION_FILTERS_SWITCHABLE;
    d3d12va_pic->pic_ctl.pAV1PicData->PrimaryRefFrame = fh->primary_ref_frame;
    if (fh->error_resilient_mode)
        d3d12va_pic->pic_ctl.pAV1PicData->Flags |= D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_FLAG_ENABLE_ERROR_RESILIENT_MODE;

    if (pic->type == FF_HW_PICTURE_TYPE_IDR)
    {
        for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
            d3d12va_pic->pic_ctl.pAV1PicData->ReferenceFramesReconPictureDescriptors[i].ReconstructedPictureResourceIndex =
            D3D12_VIDEO_ENCODER_AV1_INVALID_DPB_RESOURCE_INDEX;
        }
    } else if (pic->type == FF_HW_PICTURE_TYPE_P) {
        for (i = 0; i < pic->nb_refs[0]; i++) {
            FFHWBaseEncodePicture *ref_pic = pic->refs[0][i];
            d3d12va_pic->pic_ctl.pAV1PicData->ReferenceFramesReconPictureDescriptors[i].ReconstructedPictureResourceIndex =
            ((D3D12VAEncodeAV1Picture*)ref_pic->codec_priv)->slot;
        }
    }
    // Set reference frame management
    memset(d3d12va_pic->pic_ctl.pAV1PicData->ReferenceIndices, 0, sizeof(UINT) * AV1_REFS_PER_FRAME);
    if (pic->type == FF_HW_PICTURE_TYPE_P) {
        for (i = 0; i < AV1_REFS_PER_FRAME; i++)
            d3d12va_pic->pic_ctl.pAV1PicData->ReferenceIndices[i] = fh->ref_frame_idx[i];
    }

    return av_fifo_write(priv->picture_header_list, &priv->units.raw_frame_header, 1);
}


static const D3D12VAEncodeType d3d12va_encode_type_av1 = {
    .profiles               = d3d12va_encode_av1_profiles,

    .d3d12_codec            = D3D12_VIDEO_ENCODER_CODEC_AV1,

    .flags                  = FF_HW_FLAG_B_PICTURES |
                              FF_HW_FLAG_B_PICTURE_REFERENCES |
                              FF_HW_FLAG_NON_IDR_KEY_PICTURES,

    .default_quality        = 25,

    .get_encoder_caps       = &d3d12va_encode_av1_get_encoder_caps,

    .configure              = &d3d12va_encode_av1_configure,

    .set_level              = &d3d12va_encode_av1_set_level,

    .set_tile               = &d3d12va_encode_av1_set_tile,

    .picture_priv_data_size = sizeof(D3D12VAEncodeAV1Picture),

    .init_sequence_params   = &d3d12va_encode_av1_init_sequence_params,

    .init_picture_params    = &d3d12va_encode_av1_init_picture_params,

    .free_picture_params    = &d3d12va_encode_av1_free_picture_params,

    .write_sequence_header  = &d3d12va_encode_av1_write_sequence_header,

#ifdef CONFIG_AV1_D3D12VA_ENCODER
    .get_coded_data         = &d3d12va_encode_av1_get_coded_data,
#endif
};

static int d3d12va_encode_av1_init(AVCodecContext *avctx)
{
    D3D12VAEncodeContext     *ctx = avctx->priv_data;
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;

    ctx->codec = &d3d12va_encode_type_av1;

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = priv->profile;
    if (avctx->level == AV_LEVEL_UNKNOWN)
        avctx->level = priv->level;

    if (avctx->level != AV_LEVEL_UNKNOWN && avctx->level & ~0xff) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d: must fit "
               "in 8-bit unsigned integer.\n", avctx->level);
        return AVERROR(EINVAL);
    }

    if (priv->qp > 0)
        ctx->explicit_qp = priv->qp;

    priv->picture_header_list = av_fifo_alloc2(2, sizeof(AV1RawOBU), AV_FIFO_FLAG_AUTO_GROW);

    return ff_d3d12va_encode_init(avctx);
}

static int d3d12va_encode_av1_close(AVCodecContext *avctx)
{
    D3D12VAEncodeAV1Context *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_obu);
    ff_cbs_close(&priv->cbc);

    av_freep(&priv->common.codec_conf.pAV1Config);
    av_freep(&priv->common.gop.pAV1SequenceStructure);
    av_freep(&priv->common.level.pAV1LevelSetting);
    av_freep(&priv->common.subregions_layout.pTilesPartition_AV1);

    av_fifo_freep2(&priv->picture_header_list);

    return ff_d3d12va_encode_close(avctx);
}

#define OFFSET(x) offsetof(D3D12VAEncodeAV1Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption d3d12va_encode_av1_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    D3D12VA_ENCODE_COMMON_OPTIONS,
    D3D12VA_ENCODE_RC_OPTIONS,

    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 52, FLAGS },

    { "profile", "Set profile (general_profile_idc)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("main",             AV_PROFILE_AV1_MAIN) },
    { PROFILE("high",             AV_PROFILE_AV1_HIGH) },
    { PROFILE("professional",     AV_PROFILE_AV1_PROFESSIONAL) },
#undef PROFILE

    { "tier", "Set tier (general_tier_flag)",
      OFFSET(unit_opts.tier), AV_OPT_TYPE_INT,
      { .i64 = 0 }, 0, 1, FLAGS, "tier" },
    { "main", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 0 }, 0, 0, FLAGS, "tier" },
    { "high", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 1 }, 0, 0, FLAGS, "tier" },

    { "level", "Set level (general_level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("2.0",  0) },
    { LEVEL("2.1",  1) },
    { LEVEL("2.2",  2) },
    { LEVEL("2.3",  3) },
    { LEVEL("3.0",  4) },
    { LEVEL("3.1",  5) },
    { LEVEL("3.2",  6) },
    { LEVEL("3.3",  7) },
    { LEVEL("4.0",  8) },
    { LEVEL("4.1",  9) },
    { LEVEL("4.2",  10) },
    { LEVEL("4.3",  11) },
    { LEVEL("5.0",  12) },
    { LEVEL("5.1",  13) },
    { LEVEL("5.2",  14) },
    { LEVEL("5.3",  15) },
    { LEVEL("6.0",  16) },
    { LEVEL("6.1",  17) },
    { LEVEL("6.2",  18) },
    { LEVEL("6.3",  19) },
    { LEVEL("7.0",  20) },
    { LEVEL("7.1",  21) },
    { LEVEL("7.2",  22) },
    { LEVEL("7.3",  23) },
#undef LEVEL
    { NULL },
};

static const FFCodecDefault d3d12va_encode_av1_defaults[] = {
    { "b",              "0"   },
    { "bf",             "0"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "1"   },
    { "b_qoffset",      "0"   },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { "refs",           "0"   },
    { NULL },
};

static const AVClass d3d12va_encode_av1_class = {
    .class_name = "av1_d3d12va",
    .item_name  = av_default_item_name,
    .option     = d3d12va_encode_av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_d3d12va_encoder = {
    .p.name         = "av1_d3d12va",
    CODEC_LONG_NAME("D3D12VA av1 encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(D3D12VAEncodeAV1Context),
    .init           = &d3d12va_encode_av1_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_d3d12va_encode_receive_packet),
    .close          = &d3d12va_encode_av1_close,
    .p.priv_class   = &d3d12va_encode_av1_class,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = d3d12va_encode_av1_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_D3D12),
    .hw_configs     = ff_d3d12va_encode_hw_configs,
    .p.wrapper_name = "d3d12va",
};
