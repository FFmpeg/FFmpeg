/*
 * Copyright (C) 2007 Marco Gerards <marco@gnu.org>
 * Copyright (C) 2009 David Conrad
 * Copyright (C) 2011 Jordi Ortiz
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
 * @author Marco Gerards <marco@gnu.org>, David Conrad, Jordi Ortiz <nenjordi@gmail.com>
 */

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "dirac.h"
#include "golomb.h"
#include "internal.h"
#include "mpeg12data.h"

#if CONFIG_DIRAC_PARSE

typedef struct dirac_source_params {
    unsigned width;
    unsigned height;
    uint8_t chroma_format;          ///< 0: 444  1: 422  2: 420

    uint8_t interlaced;
    uint8_t top_field_first;

    uint8_t frame_rate_index;       ///< index into dirac_frame_rate[]
    uint8_t aspect_ratio_index;     ///< index into dirac_aspect_ratio[]

    uint16_t clean_width;
    uint16_t clean_height;
    uint16_t clean_left_offset;
    uint16_t clean_right_offset;

    uint8_t pixel_range_index;      ///< index into dirac_pixel_range_presets[]
    uint8_t color_spec_index;       ///< index into dirac_color_spec_presets[]
} dirac_source_params;

/* defaults for source parameters */
static const dirac_source_params dirac_source_parameters_defaults[] = {
    {  640,  480, 2, 0, 0,  1, 1,  640,  480, 0, 0, 1, 0 },
    {  176,  120, 2, 0, 0,  9, 2,  176,  120, 0, 0, 1, 1 },
    {  176,  144, 2, 0, 1, 10, 3,  176,  144, 0, 0, 1, 2 },
    {  352,  240, 2, 0, 0,  9, 2,  352,  240, 0, 0, 1, 1 },
    {  352,  288, 2, 0, 1, 10, 3,  352,  288, 0, 0, 1, 2 },
    {  704,  480, 2, 0, 0,  9, 2,  704,  480, 0, 0, 1, 1 },
    {  704,  576, 2, 0, 1, 10, 3,  704,  576, 0, 0, 1, 2 },
    {  720,  480, 1, 1, 0,  4, 2,  704,  480, 8, 0, 3, 1 },
    {  720,  576, 1, 1, 1,  3, 3,  704,  576, 8, 0, 3, 2 },

    { 1280,  720, 1, 0, 1,  7, 1, 1280,  720, 0, 0, 3, 3 },
    { 1280,  720, 1, 0, 1,  6, 1, 1280,  720, 0, 0, 3, 3 },
    { 1920, 1080, 1, 1, 1,  4, 1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 1, 1,  3, 1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 0, 1,  7, 1, 1920, 1080, 0, 0, 3, 3 },
    { 1920, 1080, 1, 0, 1,  6, 1, 1920, 1080, 0, 0, 3, 3 },
    { 2048, 1080, 0, 0, 1,  2, 1, 2048, 1080, 0, 0, 4, 4 },
    { 4096, 2160, 0, 0, 1,  2, 1, 4096, 2160, 0, 0, 4, 4 },

    { 3840, 2160, 1, 0, 1,  7, 1, 3840, 2160, 0, 0, 3, 3 },
    { 3840, 2160, 1, 0, 1,  6, 1, 3840, 2160, 0, 0, 3, 3 },
    { 7680, 4320, 1, 0, 1,  7, 1, 3840, 2160, 0, 0, 3, 3 },
    { 7680, 4320, 1, 0, 1,  6, 1, 3840, 2160, 0, 0, 3, 3 },
};

/* [DIRAC_STD] Table 10.4 - Available preset pixel aspect ratio values */
static const AVRational dirac_preset_aspect_ratios[] = {
    {  1,  1 },
    { 10, 11 },
    { 12, 11 },
    { 40, 33 },
    { 16, 11 },
    {  4,  3 },
};

/* [DIRAC_STD] Values 9,10 of 10.3.5 Frame Rate.
 * Table 10.3 Available preset frame rate values
 */
static const AVRational dirac_frame_rate[] = {
    { 15000, 1001 },
    {    25,    2 },
};

/* [DIRAC_STD] This should be equivalent to Table 10.5 Available signal
 * range presets */
static const struct {
    uint8_t             bitdepth;
    enum AVColorRange   color_range;
} pixel_range_presets[] = {
    {  8, AVCOL_RANGE_JPEG },
    {  8, AVCOL_RANGE_MPEG },
    { 10, AVCOL_RANGE_MPEG },
    { 12, AVCOL_RANGE_MPEG },
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

/* [DIRAC_STD] Table 10.2 Supported chroma sampling formats */
static const enum AVPixelFormat dirac_pix_fmt[][3] = {
    {AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12},
    {AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P12},
    {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV420P12},
};

/* [DIRAC_STD] 10.3 Parse Source Parameters.
 * source_parameters(base_video_format) */
static int parse_source_parameters(AVDiracSeqHeader *dsh, GetBitContext *gb,
                                   void *log_ctx)
{
    AVRational frame_rate = { 0, 0 };
    unsigned luma_depth = 8, luma_offset = 16;
    int idx;
    int chroma_x_shift, chroma_y_shift;

    /* [DIRAC_STD] 10.3.2 Frame size. frame_size(video_params) */
    /* [DIRAC_STD] custom_dimensions_flag */
    if (get_bits1(gb)) {
        dsh->width  = svq3_get_ue_golomb(gb); /* [DIRAC_STD] FRAME_WIDTH  */
        dsh->height = svq3_get_ue_golomb(gb); /* [DIRAC_STD] FRAME_HEIGHT */
    }

    /* [DIRAC_STD] 10.3.3 Chroma Sampling Format.
     *  chroma_sampling_format(video_params) */
    /* [DIRAC_STD] custom_chroma_format_flag */
    if (get_bits1(gb))
        /* [DIRAC_STD] CHROMA_FORMAT_INDEX */
        dsh->chroma_format = svq3_get_ue_golomb(gb);
    if (dsh->chroma_format > 2U) {
        if (log_ctx)
            av_log(log_ctx, AV_LOG_ERROR, "Unknown chroma format %d\n",
                   dsh->chroma_format);
        return AVERROR_INVALIDDATA;
    }

    /* [DIRAC_STD] 10.3.4 Scan Format. scan_format(video_params) */
    /* [DIRAC_STD] custom_scan_format_flag */
    if (get_bits1(gb))
        /* [DIRAC_STD] SOURCE_SAMPLING */
        dsh->interlaced = svq3_get_ue_golomb(gb);
    if (dsh->interlaced > 1U)
        return AVERROR_INVALIDDATA;

    /* [DIRAC_STD] 10.3.5 Frame Rate. frame_rate(video_params) */
    if (get_bits1(gb)) { /* [DIRAC_STD] custom_frame_rate_flag */
        dsh->frame_rate_index = svq3_get_ue_golomb(gb);

        if (dsh->frame_rate_index > 10U)
            return AVERROR_INVALIDDATA;

        if (!dsh->frame_rate_index) {
            /* [DIRAC_STD] FRAME_RATE_NUMER */
            frame_rate.num = svq3_get_ue_golomb(gb);
            /* [DIRAC_STD] FRAME_RATE_DENOM */
            frame_rate.den = svq3_get_ue_golomb(gb);
        }
    }
    /* [DIRAC_STD] preset_frame_rate(video_params, index) */
    if (dsh->frame_rate_index > 0) {
        if (dsh->frame_rate_index <= 8)
            frame_rate = ff_mpeg12_frame_rate_tab[dsh->frame_rate_index];
        else
            /* [DIRAC_STD] Table 10.3 values 9-10 */
            frame_rate = dirac_frame_rate[dsh->frame_rate_index - 9];
    }
    dsh->framerate = frame_rate;

    /* [DIRAC_STD] 10.3.6 Pixel Aspect Ratio.
     * pixel_aspect_ratio(video_params) */
    if (get_bits1(gb)) { /* [DIRAC_STD] custom_pixel_aspect_ratio_flag */
        /* [DIRAC_STD] index */
        dsh->aspect_ratio_index = svq3_get_ue_golomb(gb);

        if (dsh->aspect_ratio_index > 6U)
            return AVERROR_INVALIDDATA;

        if (!dsh->aspect_ratio_index) {
            dsh->sample_aspect_ratio.num = svq3_get_ue_golomb(gb);
            dsh->sample_aspect_ratio.den = svq3_get_ue_golomb(gb);
        }
    }
    /* [DIRAC_STD] Take value from Table 10.4 Available preset pixel
     *  aspect ratio values */
    if (dsh->aspect_ratio_index > 0)
        dsh->sample_aspect_ratio =
            dirac_preset_aspect_ratios[dsh->aspect_ratio_index - 1];

    /* [DIRAC_STD] 10.3.7 Clean area. clean_area(video_params) */
    if (get_bits1(gb)) { /* [DIRAC_STD] custom_clean_area_flag */
        /* [DIRAC_STD] CLEAN_WIDTH */
        dsh->clean_width = svq3_get_ue_golomb(gb);
        /* [DIRAC_STD] CLEAN_HEIGHT */
        dsh->clean_height = svq3_get_ue_golomb(gb);
        /* [DIRAC_STD] CLEAN_LEFT_OFFSET */
        dsh->clean_left_offset = svq3_get_ue_golomb(gb);
        /* [DIRAC_STD] CLEAN_RIGHT_OFFSET */
        dsh->clean_right_offset = svq3_get_ue_golomb(gb);
    }

    /* [DIRAC_STD] 10.3.8 Signal range. signal_range(video_params)
     * WARNING: Some adaptation seems to be done using the
     * AVCOL_RANGE_MPEG/JPEG values */
    if (get_bits1(gb)) { /* [DIRAC_STD] custom_signal_range_flag */
        /* [DIRAC_STD] index */
        dsh->pixel_range_index = svq3_get_ue_golomb(gb);

        if (dsh->pixel_range_index > 4U)
            return AVERROR_INVALIDDATA;

        /* This assumes either fullrange or MPEG levels only */
        if (!dsh->pixel_range_index) {
            luma_offset = svq3_get_ue_golomb(gb);
            luma_depth  = av_log2(svq3_get_ue_golomb(gb)) + 1;
            svq3_get_ue_golomb(gb); /* chroma offset    */
            svq3_get_ue_golomb(gb); /* chroma excursion */
            dsh->color_range = luma_offset ? AVCOL_RANGE_MPEG
                                           : AVCOL_RANGE_JPEG;
        }
    }
    /* [DIRAC_STD] Table 10.5
     * Available signal range presets <--> pixel_range_presets */
    if (dsh->pixel_range_index > 0) {
        idx                = dsh->pixel_range_index - 1;
        luma_depth         = pixel_range_presets[idx].bitdepth;
        dsh->color_range   = pixel_range_presets[idx].color_range;
    }

    dsh->bit_depth = luma_depth;

    /* Full range 8 bts uses the same pix_fmts as limited range 8 bits */
    dsh->pixel_range_index += dsh->pixel_range_index == 1;

    if (dsh->pixel_range_index < 2U)
        return AVERROR_INVALIDDATA;

    dsh->pix_fmt = dirac_pix_fmt[dsh->chroma_format][dsh->pixel_range_index-2];
    avcodec_get_chroma_sub_sample(dsh->pix_fmt, &chroma_x_shift, &chroma_y_shift);
    if ((dsh->width % (1<<chroma_x_shift)) || (dsh->height % (1<<chroma_y_shift))) {
        if (log_ctx)
            av_log(log_ctx, AV_LOG_ERROR, "Dimensions must be an integer multiple of the chroma subsampling\n");
        return AVERROR_INVALIDDATA;
    }

    /* [DIRAC_STD] 10.3.9 Colour specification. colour_spec(video_params) */
    if (get_bits1(gb)) { /* [DIRAC_STD] custom_colour_spec_flag */
        /* [DIRAC_STD] index */
        idx = dsh->color_spec_index = svq3_get_ue_golomb(gb);

        if (dsh->color_spec_index > 4U)
            return AVERROR_INVALIDDATA;

        dsh->color_primaries = dirac_color_presets[idx].color_primaries;
        dsh->colorspace      = dirac_color_presets[idx].colorspace;
        dsh->color_trc       = dirac_color_presets[idx].color_trc;

        if (!dsh->color_spec_index) {
            /* [DIRAC_STD] 10.3.9.1 Colour primaries */
            if (get_bits1(gb)) {
                idx = svq3_get_ue_golomb(gb);
                if (idx < 3U)
                    dsh->color_primaries = dirac_primaries[idx];
            }
            /* [DIRAC_STD] 10.3.9.2 Colour matrix */
            if (get_bits1(gb)) {
                idx = svq3_get_ue_golomb(gb);
                if (!idx)
                    dsh->colorspace = AVCOL_SPC_BT709;
                else if (idx == 1)
                    dsh->colorspace = AVCOL_SPC_BT470BG;
            }
            /* [DIRAC_STD] 10.3.9.3 Transfer function */
            if (get_bits1(gb) && !svq3_get_ue_golomb(gb))
                dsh->color_trc = AVCOL_TRC_BT709;
        }
    } else {
        idx                    = dsh->color_spec_index;
        dsh->color_primaries = dirac_color_presets[idx].color_primaries;
        dsh->colorspace      = dirac_color_presets[idx].colorspace;
        dsh->color_trc       = dirac_color_presets[idx].color_trc;
    }

    return 0;
}

/* [DIRAC_STD] 10. Sequence Header. sequence_header() */
int av_dirac_parse_sequence_header(AVDiracSeqHeader **pdsh,
                                   const uint8_t *buf, size_t buf_size,
                                   void *log_ctx)
{
    AVDiracSeqHeader *dsh;
    GetBitContext gb;
    unsigned video_format, picture_coding_mode;
    int ret;

    dsh = av_mallocz(sizeof(*dsh));
    if (!dsh)
        return AVERROR(ENOMEM);

    ret = init_get_bits8(&gb, buf, buf_size);
    if (ret < 0)
        goto fail;

    /* [DIRAC_SPEC] 10.1 Parse Parameters. parse_parameters() */
    dsh->version.major = svq3_get_ue_golomb(&gb);
    dsh->version.minor = svq3_get_ue_golomb(&gb);
    dsh->profile   = svq3_get_ue_golomb(&gb);
    dsh->level     = svq3_get_ue_golomb(&gb);
    /* [DIRAC_SPEC] sequence_header() -> base_video_format as defined in
     * 10.2 Base Video Format, table 10.1 Dirac predefined video formats */
    video_format   = svq3_get_ue_golomb(&gb);

    if (dsh->version.major < 2 && log_ctx)
        av_log(log_ctx, AV_LOG_WARNING, "Stream is old and may not work\n");
    else if (dsh->version.major > 2 && log_ctx)
        av_log(log_ctx, AV_LOG_WARNING, "Stream may have unhandled features\n");

    if (video_format > 20U) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    /* Fill in defaults for the source parameters. */
    dsh->width              = dirac_source_parameters_defaults[video_format].width;
    dsh->height             = dirac_source_parameters_defaults[video_format].height;
    dsh->chroma_format      = dirac_source_parameters_defaults[video_format].chroma_format;
    dsh->interlaced         = dirac_source_parameters_defaults[video_format].interlaced;
    dsh->top_field_first    = dirac_source_parameters_defaults[video_format].top_field_first;
    dsh->frame_rate_index   = dirac_source_parameters_defaults[video_format].frame_rate_index;
    dsh->aspect_ratio_index = dirac_source_parameters_defaults[video_format].aspect_ratio_index;
    dsh->clean_width        = dirac_source_parameters_defaults[video_format].clean_width;
    dsh->clean_height       = dirac_source_parameters_defaults[video_format].clean_height;
    dsh->clean_left_offset  = dirac_source_parameters_defaults[video_format].clean_left_offset;
    dsh->clean_right_offset = dirac_source_parameters_defaults[video_format].clean_right_offset;
    dsh->pixel_range_index  = dirac_source_parameters_defaults[video_format].pixel_range_index;
    dsh->color_spec_index   = dirac_source_parameters_defaults[video_format].color_spec_index;

    /* [DIRAC_STD] 10.3 Source Parameters
     * Override the defaults. */
    ret = parse_source_parameters(dsh, &gb, log_ctx);
    if (ret < 0)
        goto fail;

    /* [DIRAC_STD] picture_coding_mode shall be 0 for fields and 1 for frames
     * currently only used to signal field coding */
    picture_coding_mode = svq3_get_ue_golomb(&gb);
    if (picture_coding_mode != 0) {
        if (log_ctx) {
            av_log(log_ctx, AV_LOG_ERROR, "Unsupported picture coding mode %d",
                   picture_coding_mode);
        }
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    *pdsh = dsh;
    return 0;
fail:
    av_freep(&dsh);
    *pdsh = NULL;
    return ret;
}
#else
int av_dirac_parse_sequence_header(AVDiracSeqHeader **pdsh,
                                   const uint8_t *buf, size_t buf_size,
                                   void *log_ctx)
{
    return AVERROR(ENOSYS);
}
#endif
