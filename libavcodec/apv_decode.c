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

#include <stdatomic.h>

#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/thread.h"

#include "apv.h"
#include "apv_decode.h"
#include "apv_dsp.h"
#include "avcodec.h"
#include "cbs.h"
#include "cbs_apv.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"
#include "thread.h"


typedef struct APVDecodeContext {
    CodedBitstreamContext *cbc;
    APVDSPContext dsp;

    CodedBitstreamFragment au;
    APVDerivedTileInfo tile_info;

    AVFrame *output_frame;
    atomic_int tile_errors;

    uint8_t warned_additional_frames;
    uint8_t warned_unknown_pbu_types;
} APVDecodeContext;

static const enum AVPixelFormat apv_format_table[5][5] = {
    { AV_PIX_FMT_GRAY8,    AV_PIX_FMT_GRAY10,     AV_PIX_FMT_GRAY12,     AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16 },
    { 0 }, // 4:2:0 is not valid.
    { AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV422P16 },
    { AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV444P10,  AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV444P16 },
    { AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUVA444P16 },
};

static APVVLCLUT decode_lut;

static int apv_decode_check_format(AVCodecContext *avctx,
                                   const APVRawFrameHeader *header)
{
    int err, bit_depth;

    avctx->profile = header->frame_info.profile_idc;
    avctx->level   = header->frame_info.level_idc;

    bit_depth = header->frame_info.bit_depth_minus8 + 8;
    if (bit_depth < 8 || bit_depth > 16 || bit_depth % 2) {
        avpriv_request_sample(avctx, "Bit depth %d", bit_depth);
        return AVERROR_PATCHWELCOME;
    }
    avctx->pix_fmt =
        apv_format_table[header->frame_info.chroma_format_idc][bit_depth - 4 >> 2];

    err = ff_set_dimensions(avctx,
                            FFALIGN(header->frame_info.frame_width,  16),
                            FFALIGN(header->frame_info.frame_height, 16));
    if (err < 0) {
        // Unsupported frame size.
        return err;
    }
    avctx->width  = header->frame_info.frame_width;
    avctx->height = header->frame_info.frame_height;

    avctx->sample_aspect_ratio = (AVRational){ 1, 1 };

    avctx->color_primaries = header->color_primaries;
    avctx->color_trc       = header->transfer_characteristics;
    avctx->colorspace      = header->matrix_coefficients;
    avctx->color_range     = header->full_range_flag ? AVCOL_RANGE_JPEG
                                                     : AVCOL_RANGE_MPEG;
    avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;

    avctx->refs = 0;
    avctx->has_b_frames = 0;

    return 0;
}

static const CodedBitstreamUnitType apv_decompose_unit_types[] = {
    APV_PBU_PRIMARY_FRAME,
    APV_PBU_METADATA,
};

static AVOnce apv_entropy_once = AV_ONCE_INIT;

static av_cold void apv_entropy_build_decode_lut(void)
{
    ff_apv_entropy_build_decode_lut(&decode_lut);
}

static av_cold int apv_decode_init(AVCodecContext *avctx)
{
    APVDecodeContext *apv = avctx->priv_data;
    int err;

    ff_thread_once(&apv_entropy_once, apv_entropy_build_decode_lut);

    err = ff_cbs_init(&apv->cbc, AV_CODEC_ID_APV, avctx);
    if (err < 0)
        return err;

    apv->cbc->decompose_unit_types =
        apv_decompose_unit_types;
    apv->cbc->nb_decompose_unit_types =
        FF_ARRAY_ELEMS(apv_decompose_unit_types);

    // Extradata could be set here, but is ignored by the decoder.

    ff_apv_dsp_init(&apv->dsp);

    atomic_init(&apv->tile_errors, 0);

    return 0;
}

static av_cold int apv_decode_close(AVCodecContext *avctx)
{
    APVDecodeContext *apv = avctx->priv_data;

    ff_cbs_fragment_free(&apv->au);
    ff_cbs_close(&apv->cbc);

    return 0;
}

static int apv_decode_block(AVCodecContext *avctx,
                            void *output,
                            ptrdiff_t pitch,
                            GetBitContext *gbc,
                            APVEntropyState *entropy_state,
                            int bit_depth,
                            int qp_shift,
                            const uint16_t *qmatrix)
{
    APVDecodeContext *apv = avctx->priv_data;
    int err;

    LOCAL_ALIGNED_32(int16_t, coeff, [64]);
    memset(coeff, 0, 64 * sizeof(int16_t));

    err = ff_apv_entropy_decode_block(coeff, gbc, entropy_state);
    if (err < 0)
        return err;

    apv->dsp.decode_transquant(output, pitch,
                               coeff, qmatrix,
                               bit_depth, qp_shift);

    return 0;
}

static int apv_decode_tile_component(AVCodecContext *avctx, void *data,
                                     int job, int thread)
{
    APVRawFrame                      *input = data;
    APVDecodeContext                   *apv = avctx->priv_data;
    const CodedBitstreamAPVContext *apv_cbc = apv->cbc->priv_data;
    const APVDerivedTileInfo     *tile_info = &apv_cbc->tile_info;

    int tile_index = job / apv_cbc->num_comp;
    int comp_index = job % apv_cbc->num_comp;

    const AVPixFmtDescriptor *pix_fmt_desc =
        av_pix_fmt_desc_get(avctx->pix_fmt);

    int sub_w_shift = comp_index == 0 ? 0 : pix_fmt_desc->log2_chroma_w;
    int sub_h_shift = comp_index == 0 ? 0 : pix_fmt_desc->log2_chroma_h;

    APVRawTile *tile = &input->tile[tile_index];

    int tile_y = tile_index / tile_info->tile_cols;
    int tile_x = tile_index % tile_info->tile_cols;

    int tile_start_x = tile_info->col_starts[tile_x];
    int tile_start_y = tile_info->row_starts[tile_y];

    int tile_width  = tile_info->col_starts[tile_x + 1] - tile_start_x;
    int tile_height = tile_info->row_starts[tile_y + 1] - tile_start_y;

    int tile_mb_width  = tile_width  / APV_MB_WIDTH;
    int tile_mb_height = tile_height / APV_MB_HEIGHT;

    int blk_mb_width  = 2 >> sub_w_shift;
    int blk_mb_height = 2 >> sub_h_shift;

    int bit_depth;
    int qp_shift;
    LOCAL_ALIGNED_32(uint16_t, qmatrix_scaled, [64]);

    GetBitContext gbc;

    APVEntropyState entropy_state = {
        .log_ctx           = avctx,
        .decode_lut        = &decode_lut,
        .prev_dc           = 0,
        .prev_k_dc         = 5,
        .prev_k_level      = 0,
    };

    int err;

    err = init_get_bits8(&gbc, tile->tile_data[comp_index],
                         tile->tile_header.tile_data_size[comp_index]);
    if (err < 0)
        goto fail;

    // Combine the bitstream quantisation matrix with the qp scaling
    // in advance.  (Including qp_shift as well would overflow 16 bits.)
    // Fix the row ordering at the same time.
    {
        static const uint8_t apv_level_scale[6] = { 40, 45, 51, 57, 64, 71 };
        int qp = tile->tile_header.tile_qp[comp_index];
        int level_scale = apv_level_scale[qp % 6];

        bit_depth = apv_cbc->bit_depth;
        qp_shift  = qp / 6;

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++)
                qmatrix_scaled[y * 8 + x] = level_scale *
                    input->frame_header.quantization_matrix.q_matrix[comp_index][x][y];
        }
    }

    for (int mb_y = 0; mb_y < tile_mb_height; mb_y++) {
        for (int mb_x = 0; mb_x < tile_mb_width; mb_x++) {
            for (int blk_y = 0; blk_y < blk_mb_height; blk_y++) {
                for (int blk_x = 0; blk_x < blk_mb_width; blk_x++) {
                    int frame_y = (tile_start_y +
                                   APV_MB_HEIGHT * mb_y +
                                   APV_TR_SIZE * blk_y) >> sub_h_shift;
                    int frame_x = (tile_start_x +
                                   APV_MB_WIDTH * mb_x +
                                   APV_TR_SIZE * blk_x) >> sub_w_shift;

                    ptrdiff_t frame_pitch = apv->output_frame->linesize[comp_index];
                    uint8_t  *block_start = apv->output_frame->data[comp_index] +
                                            frame_y * frame_pitch + 2 * frame_x;

                    err = apv_decode_block(avctx,
                                           block_start, frame_pitch,
                                           &gbc, &entropy_state,
                                           bit_depth,
                                           qp_shift,
                                           qmatrix_scaled);
                    if (err < 0) {
                        // Error in block decode means entropy desync,
                        // so this is not recoverable.
                        goto fail;
                    }
                }
            }
        }
    }

    av_log(avctx, AV_LOG_DEBUG,
           "Decoded tile %d component %d: %dx%d MBs starting at (%d,%d)\n",
           tile_index, comp_index, tile_mb_width, tile_mb_height,
           tile_start_x, tile_start_y);

    return 0;

fail:
    av_log(avctx, AV_LOG_VERBOSE,
           "Decode error in tile %d component %d.\n",
           tile_index, comp_index);
    atomic_fetch_add_explicit(&apv->tile_errors, 1, memory_order_relaxed);
    return err;
}

static int apv_decode(AVCodecContext *avctx, AVFrame *output,
                      APVRawFrame *input)
{
    APVDecodeContext                   *apv = avctx->priv_data;
    const CodedBitstreamAPVContext *apv_cbc = apv->cbc->priv_data;
    const APVDerivedTileInfo     *tile_info = &apv_cbc->tile_info;
    int err, job_count;

    err = apv_decode_check_format(avctx, &input->frame_header);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported format parameters.\n");
        return err;
    }

    err = ff_thread_get_buffer(avctx, output, 0);
    if (err < 0)
        return err;

    apv->output_frame = output;
    atomic_store_explicit(&apv->tile_errors, 0, memory_order_relaxed);

    // Each component within a tile is independent of every other,
    // so we can decode all in parallel.
    job_count = tile_info->num_tiles * apv_cbc->num_comp;

    avctx->execute2(avctx, apv_decode_tile_component,
                    input, NULL, job_count);

    err = atomic_load_explicit(&apv->tile_errors, memory_order_relaxed);
    if (err > 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Decode errors in %d tile components.\n", err);
        if (avctx->flags & AV_CODEC_FLAG_OUTPUT_CORRUPT) {
            // Output the frame anyway.
            output->flags |= AV_FRAME_FLAG_CORRUPT;
        } else {
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int apv_decode_metadata(AVCodecContext *avctx, AVFrame *frame,
                               const APVRawMetadata *md)
{
    int err;

    for (int i = 0; i < md->metadata_count; i++) {
        const APVRawMetadataPayload *pl = &md->payloads[i];

        switch (pl->payload_type) {
        case APV_METADATA_MDCV:
            {
                const APVRawMetadataMDCV *mdcv = &pl->mdcv;
                AVMasteringDisplayMetadata *mdm;

                err = ff_decode_mastering_display_new(avctx, frame, &mdm);
                if (err < 0)
                    return err;

                if (mdm) {
                    for (int j = 0; j < 3; j++) {
                        mdm->display_primaries[j][0] =
                            av_make_q(mdcv->primary_chromaticity_x[j], 1 << 16);
                        mdm->display_primaries[j][1] =
                            av_make_q(mdcv->primary_chromaticity_y[j], 1 << 16);
                    }

                    mdm->white_point[0] =
                        av_make_q(mdcv->white_point_chromaticity_x, 1 << 16);
                    mdm->white_point[1] =
                        av_make_q(mdcv->white_point_chromaticity_y, 1 << 16);

                    mdm->max_luminance =
                        av_make_q(mdcv->max_mastering_luminance, 1 << 8);
                    mdm->min_luminance =
                        av_make_q(mdcv->min_mastering_luminance, 1 << 14);

                    mdm->has_primaries = 1;
                    mdm->has_luminance = 1;
                }
            }
            break;
        case APV_METADATA_CLL:
            {
                const APVRawMetadataCLL *cll = &pl->cll;
                AVContentLightMetadata *clm;

                err = ff_decode_content_light_new(avctx, frame, &clm);
                if (err < 0)
                    return err;

                if (clm) {
                    clm->MaxCLL  = cll->max_cll;
                    clm->MaxFALL = cll->max_fall;
                }
            }
            break;
        default:
            // Ignore other types of metadata.
            break;
        }
    }

    return 0;
}

static int apv_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *packet)
{
    APVDecodeContext      *apv = avctx->priv_data;
    CodedBitstreamFragment *au = &apv->au;
    int err;

    err = ff_cbs_read_packet(apv->cbc, au, packet);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to read packet.\n");
        goto fail;
    }

    for (int i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *pbu = &au->units[i];

        switch (pbu->type) {
        case APV_PBU_PRIMARY_FRAME:
            err = apv_decode(avctx, frame, pbu->content);
            if (err < 0)
                goto fail;
            *got_frame = 1;
            break;
        case APV_PBU_METADATA:
            apv_decode_metadata(avctx, frame, pbu->content);
            break;
        case APV_PBU_NON_PRIMARY_FRAME:
        case APV_PBU_PREVIEW_FRAME:
        case APV_PBU_DEPTH_FRAME:
        case APV_PBU_ALPHA_FRAME:
            if (!avctx->internal->is_copy &&
                !apv->warned_additional_frames) {
                av_log(avctx, AV_LOG_WARNING,
                       "Stream contains additional non-primary frames "
                       "which will be ignored by the decoder.\n");
                apv->warned_additional_frames = 1;
            }
            break;
        case APV_PBU_ACCESS_UNIT_INFORMATION:
        case APV_PBU_FILLER:
            // Not relevant to the decoder.
            break;
        default:
            if (!avctx->internal->is_copy &&
                !apv->warned_unknown_pbu_types) {
                av_log(avctx, AV_LOG_WARNING,
                       "Stream contains PBUs with unknown types "
                       "which will be ignored by the decoder.\n");
                apv->warned_unknown_pbu_types = 1;
            }
            break;
        }
    }

    err = packet->size;
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

const FFCodec ff_apv_decoder = {
    .p.name                = "apv",
    CODEC_LONG_NAME("Advanced Professional Video"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_APV,
    .priv_data_size        = sizeof(APVDecodeContext),
    .init                  = apv_decode_init,
    .close                 = apv_decode_close,
    FF_CODEC_DECODE_CB(apv_decode_frame),
    .p.capabilities        = AV_CODEC_CAP_DR1 |
                             AV_CODEC_CAP_SLICE_THREADS |
                             AV_CODEC_CAP_FRAME_THREADS,
};
