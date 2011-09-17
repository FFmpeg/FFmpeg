/*
 * Copyright (C) 2007 Marco Gerards <marco@gnu.org>
 * Copyright (C) 2009 David Conrad
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

/**
 * @file
 * Dirac Decoder
 * @author Marco Gerards <marco@gnu.org>
 */

#include "libavutil/imgutils.h"
#include "dirac.h"
#include "avcodec.h"
#include "golomb.h"
#include "mpeg12data.h"

// defaults for source parameters
static const dirac_source_params dirac_source_parameters_defaults[] = {
    { 640,  480,  2, 0, 0, 1,  1, 640,  480,  0, 0, 1, 0 },
    { 176,  120,  2, 0, 0, 9,  2, 176,  120,  0, 0, 1, 1 },
    { 176,  144,  2, 0, 1, 10, 3, 176,  144,  0, 0, 1, 2 },
    { 352,  240,  2, 0, 0, 9,  2, 352,  240,  0, 0, 1, 1 },
    { 352,  288,  2, 0, 1, 10, 3, 352,  288,  0, 0, 1, 2 },
    { 704,  480,  2, 0, 0, 9,  2, 704,  480,  0, 0, 1, 1 },
    { 704,  576,  2, 0, 1, 10, 3, 704,  576,  0, 0, 1, 2 },
    { 720,  480,  1, 1, 0, 4,  2, 704,  480,  8, 0, 3, 1 },
    { 720,  576,  1, 1, 1, 3,  3, 704,  576,  8, 0, 3, 2 },

    { 1280, 720,  1, 0, 1, 7,  1, 1280, 720,  0, 0, 3, 3 },
    { 1280, 720,  1, 0, 1, 6,  1, 1280, 720,  0, 0, 3, 3 },
    { 1920, 1080, 1, 1, 1, 4,  1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 1, 1, 3,  1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 0, 1, 7,  1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 0, 1, 6,  1, 1920, 1080, 0, 0, 3, 3 },
    { 2048, 1080, 0, 0, 1, 2,  1, 2048, 1080, 0, 0, 4, 4 },
    { 4096, 2160, 0, 0, 1, 2,  1, 4096, 2160, 0, 0, 4, 4 },

    { 3840, 2160, 1, 0, 1, 7,  1, 3840, 2160, 0, 0, 3, 3 },
    { 3840, 2160, 1, 0, 1, 6,  1, 3840, 2160, 0, 0, 3, 3 },
    { 7680, 4320, 1, 0, 1, 7,  1, 3840, 2160, 0, 0, 3, 3 },
    { 7680, 4320, 1, 0, 1, 6,  1, 3840, 2160, 0, 0, 3, 3 },
};

static const AVRational dirac_preset_aspect_ratios[] = {
    {1, 1},
    {10, 11},
    {12, 11},
    {40, 33},
    {16, 11},
    {4, 3},
};

static const AVRational dirac_frame_rate[] = {
    {15000, 1001},
    {25, 2},
};

static const struct {
    uint8_t             bitdepth;
    enum AVColorRange   color_range;
} pixel_range_presets[] = {
    {8,  AVCOL_RANGE_JPEG},
    {8,  AVCOL_RANGE_MPEG},
    {10, AVCOL_RANGE_MPEG},
    {12, AVCOL_RANGE_MPEG},
};

static const enum AVColorPrimaries dirac_primaries[] = {
    AVCOL_PRI_BT709,
    AVCOL_PRI_SMPTE170M,
    AVCOL_PRI_BT470BG,
};

static const struct {
    enum AVColorPrimaries color_primaries;
    enum AVColorSpace colorspace;
    enum AVColorTransferCharacteristic color_trc;
} dirac_color_presets[] = {
    { AVCOL_PRI_BT709,     AVCOL_SPC_BT709,   AVCOL_TRC_BT709 },
    { AVCOL_PRI_SMPTE170M, AVCOL_SPC_BT470BG, AVCOL_TRC_BT709 },
    { AVCOL_PRI_BT470BG,   AVCOL_SPC_BT470BG, AVCOL_TRC_BT709 },
    { AVCOL_PRI_BT709,     AVCOL_SPC_BT709,   AVCOL_TRC_BT709 },
    { AVCOL_PRI_BT709,     AVCOL_SPC_BT709,   AVCOL_TRC_UNSPECIFIED /* DCinema */ },
};

static const enum PixelFormat dirac_pix_fmt[2][3] = {
    { PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P  },
    { PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P },
};

static int parse_source_parameters(AVCodecContext *avctx, GetBitContext *gb,
                                   dirac_source_params *source)
{
    AVRational frame_rate = (AVRational){0,0};
    unsigned luma_depth = 8, luma_offset = 16;
    int idx;

    if (get_bits1(gb)) {
        source->width  = svq3_get_ue_golomb(gb);
        source->height = svq3_get_ue_golomb(gb);
    }

    // chroma subsampling
    if (get_bits1(gb))
        source->chroma_format = svq3_get_ue_golomb(gb);
    if (source->chroma_format > 2U) {
        av_log(avctx, AV_LOG_ERROR, "Unknown chroma format %d\n",
               source->chroma_format);
        return -1;
    }

    if (get_bits1(gb))
        source->interlaced = svq3_get_ue_golomb(gb);
    if (source->interlaced > 1U)
        return -1;

    // frame rate
    if (get_bits1(gb)) {
        source->frame_rate_index = svq3_get_ue_golomb(gb);

        if (source->frame_rate_index > 10U)
            return -1;

        if (!source->frame_rate_index) {
            frame_rate.num = svq3_get_ue_golomb(gb);
            frame_rate.den = svq3_get_ue_golomb(gb);
        }
    }
    if (source->frame_rate_index > 0) {
        if (source->frame_rate_index <= 8)
            frame_rate = ff_frame_rate_tab[source->frame_rate_index];
        else
            frame_rate = dirac_frame_rate[source->frame_rate_index-9];
    }
    av_reduce(&avctx->time_base.num, &avctx->time_base.den,
              frame_rate.den, frame_rate.num, 1<<30);

    // aspect ratio
    if (get_bits1(gb)) {
        source->aspect_ratio_index = svq3_get_ue_golomb(gb);

        if (source->aspect_ratio_index > 6U)
            return -1;

        if (!source->aspect_ratio_index) {
            avctx->sample_aspect_ratio.num = svq3_get_ue_golomb(gb);
            avctx->sample_aspect_ratio.den = svq3_get_ue_golomb(gb);
        }
    }
    if (source->aspect_ratio_index > 0)
        avctx->sample_aspect_ratio =
                dirac_preset_aspect_ratios[source->aspect_ratio_index-1];

    if (get_bits1(gb)) {
        source->clean_width        = svq3_get_ue_golomb(gb);
        source->clean_height       = svq3_get_ue_golomb(gb);
        source->clean_left_offset  = svq3_get_ue_golomb(gb);
        source->clean_right_offset = svq3_get_ue_golomb(gb);
    }

    // Override signal range.
    if (get_bits1(gb)) {
        source->pixel_range_index = svq3_get_ue_golomb(gb);

        if (source->pixel_range_index > 4U)
            return -1;

        // This assumes either fullrange or MPEG levels only
        if (!source->pixel_range_index) {
            luma_offset = svq3_get_ue_golomb(gb);
            luma_depth  = av_log2(svq3_get_ue_golomb(gb))+1;
            svq3_get_ue_golomb(gb); // chroma offset
            svq3_get_ue_golomb(gb); // chroma excursion

            avctx->color_range = luma_offset ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        }
    }
    if (source->pixel_range_index > 0) {
        idx                = source->pixel_range_index-1;
        luma_depth         = pixel_range_presets[idx].bitdepth;
        avctx->color_range = pixel_range_presets[idx].color_range;
    }

    if (luma_depth > 8)
        av_log(avctx, AV_LOG_WARNING, "Bitdepth greater than 8");

    avctx->pix_fmt = dirac_pix_fmt[!luma_offset][source->chroma_format];

    // color spec
    if (get_bits1(gb)) {
        idx = source->color_spec_index = svq3_get_ue_golomb(gb);

        if (source->color_spec_index > 4U)
            return -1;

        avctx->color_primaries = dirac_color_presets[idx].color_primaries;
        avctx->colorspace      = dirac_color_presets[idx].colorspace;
        avctx->color_trc       = dirac_color_presets[idx].color_trc;

        if (!source->color_spec_index) {
            if (get_bits1(gb)) {
                idx = svq3_get_ue_golomb(gb);
                if (idx < 3U)
                    avctx->color_primaries = dirac_primaries[idx];
            }

            if (get_bits1(gb)) {
                idx = svq3_get_ue_golomb(gb);
                if (!idx)
                    avctx->colorspace = AVCOL_SPC_BT709;
                else if (idx == 1)
                    avctx->colorspace = AVCOL_SPC_BT470BG;
            }

            if (get_bits1(gb) && !svq3_get_ue_golomb(gb))
                avctx->color_trc = AVCOL_TRC_BT709;
        }
    } else {
        idx = source->color_spec_index;
        avctx->color_primaries = dirac_color_presets[idx].color_primaries;
        avctx->colorspace      = dirac_color_presets[idx].colorspace;
        avctx->color_trc       = dirac_color_presets[idx].color_trc;
    }

    return 0;
}

int ff_dirac_parse_sequence_header(AVCodecContext *avctx, GetBitContext *gb,
                                   dirac_source_params *source)
{
    unsigned version_major;
    unsigned video_format, picture_coding_mode;

    version_major  = svq3_get_ue_golomb(gb);
    svq3_get_ue_golomb(gb); /* version_minor */
    avctx->profile = svq3_get_ue_golomb(gb);
    avctx->level   = svq3_get_ue_golomb(gb);
    video_format   = svq3_get_ue_golomb(gb);

    if (version_major < 2)
        av_log(avctx, AV_LOG_WARNING, "Stream is old and may not work\n");
    else if (version_major > 2)
        av_log(avctx, AV_LOG_WARNING, "Stream may have unhandled features\n");

    if (video_format > 20U)
        return -1;

    // Fill in defaults for the source parameters.
    *source = dirac_source_parameters_defaults[video_format];

    // Override the defaults.
    if (parse_source_parameters(avctx, gb, source))
        return -1;

    if (av_image_check_size(source->width, source->height, 0, avctx))
        return -1;

    avcodec_set_dimensions(avctx, source->width, source->height);

    // currently only used to signal field coding
    picture_coding_mode = svq3_get_ue_golomb(gb);
    if (picture_coding_mode != 0) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported picture coding mode %d",
               picture_coding_mode);
        return -1;
    }
    return 0;
}
