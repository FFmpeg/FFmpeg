/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
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
 * The simplest AC-3 encoder.
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "config_components.h"
#include "encode.h"
#include "me_cmp.h"
#include "put_bits.h"
#include "audiodsp.h"
#include "ac3dsp.h"
#include "ac3.h"
#include "ac3defs.h"
#include "ac3tab.h"
#include "ac3enc.h"
#include "eac3enc.h"

#define SAMPLETYPE_SIZE(ctx) (sizeof(float) == sizeof(int32_t) ? sizeof(float) : \
                                  (ctx)->fixed_point ? sizeof(int32_t) : sizeof(float))

typedef struct AC3Mant {
    int16_t *qmant1_ptr, *qmant2_ptr, *qmant4_ptr; ///< mantissa pointers for bap=1,2,4
    int mant1_cnt, mant2_cnt, mant4_cnt;    ///< mantissa counts for bap=1,2,4
} AC3Mant;

#define CMIXLEV_NUM_OPTIONS 3
static const float cmixlev_options[CMIXLEV_NUM_OPTIONS] = {
    LEVEL_MINUS_3DB, LEVEL_MINUS_4POINT5DB, LEVEL_MINUS_6DB
};

#define SURMIXLEV_NUM_OPTIONS 3
static const float surmixlev_options[SURMIXLEV_NUM_OPTIONS] = {
    LEVEL_MINUS_3DB, LEVEL_MINUS_6DB, LEVEL_ZERO
};

#define EXTMIXLEV_NUM_OPTIONS 8
static const float extmixlev_options[EXTMIXLEV_NUM_OPTIONS] = {
    LEVEL_PLUS_3DB,  LEVEL_PLUS_1POINT5DB,  LEVEL_ONE,       LEVEL_MINUS_1POINT5DB,
    LEVEL_MINUS_3DB, LEVEL_MINUS_4POINT5DB, LEVEL_MINUS_6DB, LEVEL_ZERO
};

/* The first two options apply only to the AC-3 encoders;
 * the rest is also valid for EAC-3. When modifying it,
 * it might be necessary to adapt said offset in eac3enc.c. */
#define OFFSET(param) offsetof(AC3EncodeContext, options.param)
#define AC3ENC_PARAM (AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
const AVOption ff_ac3_enc_options[] = {
/* AC-3 downmix levels */
{"center_mixlev", "Center Mix Level", OFFSET(center_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = LEVEL_MINUS_4POINT5DB }, 0.0, 1.0, AC3ENC_PARAM},
{"surround_mixlev", "Surround Mix Level", OFFSET(surround_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = LEVEL_MINUS_6DB }, 0.0, 1.0, AC3ENC_PARAM},
/* audio production information */
{"mixing_level", "Mixing Level", OFFSET(mixing_level), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, 111, AC3ENC_PARAM},
{"room_type", "Room Type", OFFSET(room_type), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_SMALL_ROOM, AC3ENC_PARAM, .unit = "room_type"},
    {"notindicated", "Not Indicated (default)", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_NOT_INDICATED }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "room_type"},
    {"large",        "Large Room",              0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_LARGE_ROOM    }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "room_type"},
    {"small",        "Small Room",              0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_SMALL_ROOM    }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "room_type"},
/* Metadata Options */
{"per_frame_metadata", "Allow Changing Metadata Per-Frame", OFFSET(allow_per_frame_metadata), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, AC3ENC_PARAM},
{"copyright", "Copyright Bit", OFFSET(copyright), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, 1, AC3ENC_PARAM},
{"dialnorm", "Dialogue Level (dB)", OFFSET(dialogue_level), AV_OPT_TYPE_INT, {.i64 = -31 }, -31, -1, AC3ENC_PARAM},
{"dsur_mode", "Dolby Surround Mode", OFFSET(dolby_surround_mode), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_MODE_ON, AC3ENC_PARAM, .unit = "dsur_mode"},
    {"notindicated", "Not Indicated (default)",    0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_NOT_INDICATED }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsur_mode"},
    {"on",           "Dolby Surround Encoded",     0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_ON       }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsur_mode"},
    {"off",          "Not Dolby Surround Encoded", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_OFF      }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsur_mode"},
{"original", "Original Bit Stream", OFFSET(original), AV_OPT_TYPE_INT,   {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, 1, AC3ENC_PARAM},
/* extended bitstream information */
{"dmix_mode", "Preferred Stereo Downmix Mode", OFFSET(preferred_stereo_downmix), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_DOWNMIX_DPLII, AC3ENC_PARAM, .unit = "dmix_mode"},
    {"notindicated", "Not Indicated (default)", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_NOT_INDICATED }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dmix_mode"},
    {"ltrt", "Lt/Rt Downmix Preferred",         0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_DOWNMIX_LTRT  }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dmix_mode"},
    {"loro", "Lo/Ro Downmix Preferred",         0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_DOWNMIX_LORO  }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dmix_mode"},
    {"dplii", "Dolby Pro Logic II Downmix Preferred", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_DOWNMIX_DPLII }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dmix_mode"},
{"ltrt_cmixlev", "Lt/Rt Center Mix Level", OFFSET(ltrt_center_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"ltrt_surmixlev", "Lt/Rt Surround Mix Level", OFFSET(ltrt_surround_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"loro_cmixlev", "Lo/Ro Center Mix Level", OFFSET(loro_center_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"loro_surmixlev", "Lo/Ro Surround Mix Level", OFFSET(loro_surround_mix_level), AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"dsurex_mode", "Dolby Surround EX Mode", OFFSET(dolby_surround_ex_mode), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_DSUREX_DPLIIZ, AC3ENC_PARAM, .unit = "dsurex_mode"},
    {"notindicated", "Not Indicated (default)",       0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_NOT_INDICATED }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsurex_mode"},
    {"on",           "Dolby Surround EX Encoded",     0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_ON       }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsurex_mode"},
    {"off",          "Not Dolby Surround EX Encoded", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_OFF      }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsurex_mode"},
    {"dpliiz",       "Dolby Pro Logic IIz-encoded",   0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_DSUREX_DPLIIZ }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dsurex_mode"},
{"dheadphone_mode", "Dolby Headphone Mode", OFFSET(dolby_headphone_mode), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_MODE_ON, AC3ENC_PARAM, .unit = "dheadphone_mode"},
    {"notindicated", "Not Indicated (default)",     0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_NOT_INDICATED }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dheadphone_mode"},
    {"on",           "Dolby Headphone Encoded",     0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_ON       }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dheadphone_mode"},
    {"off",          "Not Dolby Headphone Encoded", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_MODE_OFF      }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "dheadphone_mode"},
{"ad_conv_type", "A/D Converter Type", OFFSET(ad_converter_type), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_NONE }, AC3ENC_OPT_NONE, AC3ENC_OPT_ADCONV_HDCD, AC3ENC_PARAM, .unit = "ad_conv_type"},
    {"standard", "Standard (default)", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_ADCONV_STANDARD }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "ad_conv_type"},
    {"hdcd",     "HDCD",               0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_ADCONV_HDCD     }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "ad_conv_type"},
/* Other Encoding Options */
{"stereo_rematrixing", "Stereo Rematrixing", OFFSET(stereo_rematrixing), AV_OPT_TYPE_BOOL, {.i64 = 1 }, 0, 1, AC3ENC_PARAM},
{"channel_coupling",   "Channel Coupling",   OFFSET(channel_coupling),   AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_AUTO }, AC3ENC_OPT_AUTO, AC3ENC_OPT_ON, AC3ENC_PARAM, .unit = "channel_coupling"},
    {"auto", "Selected by the Encoder", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_AUTO }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "channel_coupling"},
{"cpl_start_band", "Coupling Start Band", OFFSET(cpl_start), AV_OPT_TYPE_INT, {.i64 = AC3ENC_OPT_AUTO }, AC3ENC_OPT_AUTO, 15, AC3ENC_PARAM, .unit = "cpl_start_band"},
    {"auto", "Selected by the Encoder", 0, AV_OPT_TYPE_CONST, {.i64 = AC3ENC_OPT_AUTO }, INT_MIN, INT_MAX, AC3ENC_PARAM, .unit = "cpl_start_band"},
{NULL}
};

const AVClass ff_ac3enc_class = {
    .class_name = "AC-3 Encoder",
    .item_name  = av_default_item_name,
    .option     = ff_ac3_enc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodecDefault ff_ac3_enc_defaults[] = {
    { "b",  "0" },
    { NULL }
};

/**
 * LUT for number of exponent groups.
 * exponent_group_tab[coupling][exponent strategy-1][number of coefficients]
 */
static uint8_t exponent_group_tab[2][3][256];


/**
 * List of supported channel layouts.
 */
const AVChannelLayout ff_ac3_ch_layouts[19] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_2_1,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_2_2,
    AV_CHANNEL_LAYOUT_QUAD,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LAYOUT_MONO | AV_CH_LOW_FREQUENCY,
    },
    {
        .nb_channels = 3,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LAYOUT_STEREO | AV_CH_LOW_FREQUENCY,
    },
    {
        .nb_channels = 4,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LAYOUT_2_1 | AV_CH_LOW_FREQUENCY,
    },
    {
        .nb_channels = 4,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LAYOUT_SURROUND | AV_CH_LOW_FREQUENCY,
    },
    {
        .nb_channels = 5,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LAYOUT_4POINT0 | AV_CH_LOW_FREQUENCY,
    },
    AV_CHANNEL_LAYOUT_5POINT1,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    { 0 },
};

/**
 * Table to remap channels from SMPTE order to AC-3 order.
 * [channel_mode][lfe][ch]
 */
static const uint8_t ac3_enc_channel_map[8][2][6] = {
    COMMON_CHANNEL_MAP
    { { 0, 1, 2, 3,    }, { 0, 1, 3, 4, 2,   } },
    { { 0, 2, 1, 3, 4, }, { 0, 2, 1, 4, 5, 3 } },
};

/**
 * LUT to select the bandwidth code based on the bit rate, sample rate, and
 * number of full-bandwidth channels.
 * bandwidth_tab[fbw_channels-1][sample rate code][bit rate code]
 */
static const uint8_t ac3_bandwidth_tab[5][3][19] = {
//      32  40  48  56  64  80  96 112 128 160 192 224 256 320 384 448 512 576 640

    { {  0,  0,  0, 12, 16, 32, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0, 16, 20, 36, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0, 32, 40, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0, 20, 24, 32, 48, 48, 48, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  4, 24, 28, 36, 56, 56, 56, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0, 20, 44, 52, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0, 16, 24, 32, 40, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  4, 20, 28, 36, 44, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0, 20, 40, 48, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 12, 24, 32, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 16, 28, 36, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 32, 48, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8, 20, 32, 40, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 12, 24, 36, 44, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 28, 44, 60, 60, 60, 60, 60, 60 } }
};


/**
 * LUT to select the coupling start band based on the bit rate, sample rate, and
 * number of full-bandwidth channels. -1 = coupling off
 * ac3_coupling_start_tab[channel_mode-2][sample rate code][bit rate code]
 *
 * TODO: more testing for optimal parameters.
 *       multi-channel tests at 44.1kHz and 32kHz.
 */
static const int8_t ac3_coupling_start_tab[6][3][19] = {
//      32  40  48  56  64  80  96 112 128 160 192 224 256 320 384 448 512 576 640

    // 2/0
    { {  0,  0,  0,  0,  0,  0,  0,  1,  1,  7,  8, 11, 12, -1, -1, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  1,  3,  5,  7, 10, 12, 13, -1, -1, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  1,  2,  2,  9, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/0
    { {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 2/1 - untested
    { {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/1
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 2/2 - untested
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/2
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  6,  8, 11, 12, 12, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  6,  8, 11, 12, 12, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },
};


#define FLT_OPTION_THRESHOLD 0.01

static int validate_float_option(float v, const float *v_list, int v_list_size)
{
    int i;

    for (i = 0; i < v_list_size; i++) {
        if (v < (v_list[i] + FLT_OPTION_THRESHOLD) &&
            v > (v_list[i] - FLT_OPTION_THRESHOLD))
            break;
    }
    if (i == v_list_size)
        return AVERROR(EINVAL);

    return i;
}


static void validate_mix_level(void *log_ctx, const char *opt_name,
                               float *opt_param, const float *list,
                               int list_size, int default_value, int min_value,
                               int *ctx_param)
{
    int mixlev = validate_float_option(*opt_param, list, list_size);
    if (mixlev < min_value) {
        mixlev = default_value;
        if (*opt_param >= 0.0) {
            av_log(log_ctx, AV_LOG_WARNING, "requested %s is not valid. using "
                   "default value: %0.3f\n", opt_name, list[mixlev]);
        }
    }
    *opt_param = list[mixlev];
    *ctx_param = mixlev;
}


/**
 * Validate metadata options as set by AVOption system.
 * These values can optionally be changed per-frame.
 *
 * @param s  AC-3 encoder private context
 */
static int ac3_validate_metadata(AC3EncodeContext *s)
{
    AVCodecContext *avctx = s->avctx;
    AC3EncOptions *opt = &s->options;

    opt->audio_production_info = 0;
    opt->extended_bsi_1        = 0;
    opt->extended_bsi_2        = 0;
    opt->eac3_mixing_metadata  = 0;
    opt->eac3_info_metadata    = 0;

    /* determine mixing metadata / xbsi1 use */
    if (s->channel_mode > AC3_CHMODE_STEREO && opt->preferred_stereo_downmix != AC3ENC_OPT_NONE) {
        opt->extended_bsi_1       = 1;
        opt->eac3_mixing_metadata = 1;
    }
    if (s->has_center &&
        (opt->ltrt_center_mix_level >= 0 || opt->loro_center_mix_level >= 0)) {
        opt->extended_bsi_1       = 1;
        opt->eac3_mixing_metadata = 1;
    }
    if (s->has_surround &&
        (opt->ltrt_surround_mix_level >= 0 || opt->loro_surround_mix_level >= 0)) {
        opt->extended_bsi_1       = 1;
        opt->eac3_mixing_metadata = 1;
    }

    if (s->eac3) {
        /* determine info metadata use */
        if (avctx->audio_service_type != AV_AUDIO_SERVICE_TYPE_MAIN)
            opt->eac3_info_metadata = 1;
        if (opt->copyright != AC3ENC_OPT_NONE || opt->original != AC3ENC_OPT_NONE)
            opt->eac3_info_metadata = 1;
        if (s->channel_mode == AC3_CHMODE_STEREO &&
            (opt->dolby_headphone_mode != AC3ENC_OPT_NONE || opt->dolby_surround_mode != AC3ENC_OPT_NONE))
            opt->eac3_info_metadata = 1;
        if (s->channel_mode >= AC3_CHMODE_2F2R && opt->dolby_surround_ex_mode != AC3ENC_OPT_NONE)
            opt->eac3_info_metadata = 1;
        if (opt->mixing_level != AC3ENC_OPT_NONE || opt->room_type != AC3ENC_OPT_NONE ||
            opt->ad_converter_type != AC3ENC_OPT_NONE) {
            opt->audio_production_info = 1;
            opt->eac3_info_metadata    = 1;
        }
    } else {
        /* determine audio production info use */
        if (opt->mixing_level != AC3ENC_OPT_NONE || opt->room_type != AC3ENC_OPT_NONE)
            opt->audio_production_info = 1;

        /* determine xbsi2 use */
        if (s->channel_mode >= AC3_CHMODE_2F2R && opt->dolby_surround_ex_mode != AC3ENC_OPT_NONE)
            opt->extended_bsi_2 = 1;
        if (s->channel_mode == AC3_CHMODE_STEREO && opt->dolby_headphone_mode != AC3ENC_OPT_NONE)
            opt->extended_bsi_2 = 1;
        if (opt->ad_converter_type != AC3ENC_OPT_NONE)
            opt->extended_bsi_2 = 1;
    }

    /* validate AC-3 mixing levels */
    if (!s->eac3) {
        if (s->has_center) {
            validate_mix_level(avctx, "center_mix_level", &opt->center_mix_level,
                               cmixlev_options, CMIXLEV_NUM_OPTIONS, 1, 0,
                               &s->center_mix_level);
        }
        if (s->has_surround) {
            validate_mix_level(avctx, "surround_mix_level", &opt->surround_mix_level,
                               surmixlev_options, SURMIXLEV_NUM_OPTIONS, 1, 0,
                               &s->surround_mix_level);
        }
    }

    /* validate extended bsi 1 / mixing metadata */
    if (opt->extended_bsi_1 || opt->eac3_mixing_metadata) {
        /* default preferred stereo downmix */
        if (opt->preferred_stereo_downmix == AC3ENC_OPT_NONE)
            opt->preferred_stereo_downmix = AC3ENC_OPT_NOT_INDICATED;
        if (!s->eac3 || s->has_center) {
            /* validate Lt/Rt center mix level */
            validate_mix_level(avctx, "ltrt_center_mix_level",
                               &opt->ltrt_center_mix_level, extmixlev_options,
                               EXTMIXLEV_NUM_OPTIONS, 5, 0,
                               &s->ltrt_center_mix_level);
            /* validate Lo/Ro center mix level */
            validate_mix_level(avctx, "loro_center_mix_level",
                               &opt->loro_center_mix_level, extmixlev_options,
                               EXTMIXLEV_NUM_OPTIONS, 5, 0,
                               &s->loro_center_mix_level);
        }
        if (!s->eac3 || s->has_surround) {
            /* validate Lt/Rt surround mix level */
            validate_mix_level(avctx, "ltrt_surround_mix_level",
                               &opt->ltrt_surround_mix_level, extmixlev_options,
                               EXTMIXLEV_NUM_OPTIONS, 6, 3,
                               &s->ltrt_surround_mix_level);
            /* validate Lo/Ro surround mix level */
            validate_mix_level(avctx, "loro_surround_mix_level",
                               &opt->loro_surround_mix_level, extmixlev_options,
                               EXTMIXLEV_NUM_OPTIONS, 6, 3,
                               &s->loro_surround_mix_level);
        }
    }

    /* validate audio service type / channels combination */
    if ((avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_KARAOKE &&
         avctx->ch_layout.nb_channels == 1) ||
        ((avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_COMMENTARY ||
          avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_EMERGENCY  ||
          avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_VOICE_OVER)
         && avctx->ch_layout.nb_channels > 1)) {
        av_log(avctx, AV_LOG_ERROR, "invalid audio service type for the "
                                    "specified number of channels\n");
        return AVERROR(EINVAL);
    }

    /* validate extended bsi 2 / info metadata */
    if (opt->extended_bsi_2 || opt->eac3_info_metadata) {
        /* default dolby headphone mode */
        if (opt->dolby_headphone_mode == AC3ENC_OPT_NONE)
            opt->dolby_headphone_mode = AC3ENC_OPT_NOT_INDICATED;
        /* default dolby surround ex mode */
        if (opt->dolby_surround_ex_mode == AC3ENC_OPT_NONE)
            opt->dolby_surround_ex_mode = AC3ENC_OPT_NOT_INDICATED;
        /* default A/D converter type */
        if (opt->ad_converter_type == AC3ENC_OPT_NONE)
            opt->ad_converter_type = AC3ENC_OPT_ADCONV_STANDARD;
    }

    /* copyright & original defaults */
    if (!s->eac3 || opt->eac3_info_metadata) {
        /* default copyright */
        if (opt->copyright == AC3ENC_OPT_NONE)
            opt->copyright = AC3ENC_OPT_OFF;
        /* default original */
        if (opt->original == AC3ENC_OPT_NONE)
            opt->original = AC3ENC_OPT_ON;
    }

    /* dolby surround mode default */
    if (!s->eac3 || opt->eac3_info_metadata) {
        if (opt->dolby_surround_mode == AC3ENC_OPT_NONE)
            opt->dolby_surround_mode = AC3ENC_OPT_NOT_INDICATED;
    }

    /* validate audio production info */
    if (opt->audio_production_info) {
        if (opt->mixing_level == AC3ENC_OPT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "mixing_level must be set if "
                   "room_type is set\n");
            return AVERROR(EINVAL);
        }
        if (opt->mixing_level < 80) {
            av_log(avctx, AV_LOG_ERROR, "invalid mixing level. must be between "
                   "80dB and 111dB\n");
            return AVERROR(EINVAL);
        }
        /* default room type */
        if (opt->room_type == AC3ENC_OPT_NONE)
            opt->room_type = AC3ENC_OPT_NOT_INDICATED;
    }

    /* set bitstream id for alternate bitstream syntax */
    if (!s->eac3 && (opt->extended_bsi_1 || opt->extended_bsi_2))
        s->bitstream_id = 6;

    return 0;
}

/**
 * Adjust the frame size to make the average bit rate match the target bit rate.
 * This is only needed for 11025, 22050, and 44100 sample rates or any E-AC-3.
 *
 * @param s  AC-3 encoder private context
 */
static void ac3_adjust_frame_size(AC3EncodeContext *s)
{
    while (s->bits_written >= s->bit_rate && s->samples_written >= s->sample_rate) {
        s->bits_written    -= s->bit_rate;
        s->samples_written -= s->sample_rate;
    }
    s->frame_size = s->frame_size_min +
                    2 * (s->bits_written * s->sample_rate < s->samples_written * s->bit_rate);
    s->bits_written    += s->frame_size * 8;
    s->samples_written += AC3_BLOCK_SIZE * s->num_blocks;
}

/**
 * Set the initial coupling strategy parameters prior to coupling analysis.
 *
 * @param s  AC-3 encoder private context
 */
void ff_ac3_compute_coupling_strategy(AC3EncodeContext *s)
{
    int blk, ch;
    int got_cpl_snr;
    int num_cpl_blocks;

    /* set coupling use flags for each block/channel */
    /* TODO: turn coupling on/off and adjust start band based on bit usage */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->fbw_channels; ch++)
            block->channel_in_cpl[ch] = s->cpl_on;
    }

    /* enable coupling for each block if at least 2 channels have coupling
       enabled for that block */
    got_cpl_snr = 0;
    num_cpl_blocks = 0;
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        block->num_cpl_channels = 0;
        for (ch = 1; ch <= s->fbw_channels; ch++)
            block->num_cpl_channels += block->channel_in_cpl[ch];
        block->cpl_in_use = block->num_cpl_channels > 1;
        num_cpl_blocks += block->cpl_in_use;
        if (!block->cpl_in_use) {
            block->num_cpl_channels = 0;
            for (ch = 1; ch <= s->fbw_channels; ch++)
                block->channel_in_cpl[ch] = 0;
        }

        block->new_cpl_strategy = !blk;
        if (blk) {
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (block->channel_in_cpl[ch] != s->blocks[blk-1].channel_in_cpl[ch]) {
                    block->new_cpl_strategy = 1;
                    break;
                }
            }
        }
        block->new_cpl_leak = block->new_cpl_strategy;

        if (!blk || (block->cpl_in_use && !got_cpl_snr)) {
            block->new_snr_offsets = 1;
            if (block->cpl_in_use)
                got_cpl_snr = 1;
        } else {
            block->new_snr_offsets = 0;
        }
    }
    if (!num_cpl_blocks)
        s->cpl_on = 0;

    /* set bandwidth for each channel */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (block->channel_in_cpl[ch])
                block->end_freq[ch] = s->start_freq[CPL_CH];
            else
                block->end_freq[ch] = s->bandwidth_code * 3 + 73;
        }
    }
}


/**
 * Apply stereo rematrixing to coefficients based on rematrixing flags.
 *
 * @param s  AC-3 encoder private context
 */
static void ac3_apply_rematrixing(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd, i;
    int start, end;
    uint8_t *flags = NULL;

    if (!s->rematrixing_enabled)
        return;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (block->new_rematrixing_strategy)
            flags = block->rematrixing_flags;
        nb_coefs = FFMIN(block->end_freq[1], block->end_freq[2]);
        for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++) {
            if (flags[bnd]) {
                start = ff_ac3_rematrix_band_tab[bnd];
                end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
                for (i = start; i < end; i++) {
                    int32_t lt = block->fixed_coef[1][i];
                    int32_t rt = block->fixed_coef[2][i];
                    block->fixed_coef[1][i] = (lt + rt) >> 1;
                    block->fixed_coef[2][i] = (lt - rt) >> 1;
                }
            }
        }
    }
}


/*
 * Initialize exponent tables.
 */
static av_cold void exponent_init(void)
{
    int expstr, i, grpsize;

    for (expstr = EXP_D15-1; expstr <= EXP_D45-1; expstr++) {
        grpsize = 3 << expstr;
        for (i = 12; i < 256; i++) {
            exponent_group_tab[0][expstr][i] = (i + grpsize - 4) / grpsize;
            exponent_group_tab[1][expstr][i] = (i              ) / grpsize;
        }
    }
    /* LFE */
    exponent_group_tab[0][0][7] = 2;
}


/*
 * Extract exponents from the MDCT coefficients.
 */
static void extract_exponents(AC3EncodeContext *s)
{
    int ch        = !s->cpl_on;
    int chan_size = AC3_MAX_COEFS * s->num_blocks * (s->channels - ch + 1);
    AC3Block *block = &s->blocks[0];

    s->ac3dsp.extract_exponents(block->exp[ch], block->fixed_coef[ch], chan_size);
}


/**
 * Exponent Difference Threshold.
 * New exponents are sent if their SAD exceed this number.
 */
#define EXP_DIFF_THRESHOLD 500

/**
 * Table used to select exponent strategy based on exponent reuse block interval.
 */
static const uint8_t exp_strategy_reuse_tab[4][6] = {
    { EXP_D15, EXP_D15, EXP_D15, EXP_D15, EXP_D15, EXP_D15 },
    { EXP_D15, EXP_D15, EXP_D15, EXP_D15, EXP_D15, EXP_D15 },
    { EXP_D25, EXP_D25, EXP_D15, EXP_D15, EXP_D15, EXP_D15 },
    { EXP_D45, EXP_D25, EXP_D25, EXP_D15, EXP_D15, EXP_D15 }
};

/*
 * Calculate exponent strategies for all channels.
 * Array arrangement is reversed to simplify the per-channel calculation.
 */
static void compute_exp_strategy(AC3EncodeContext *s)
{
    int ch, blk, blk1;

    for (ch = !s->cpl_on; ch <= s->fbw_channels; ch++) {
        uint8_t *exp_strategy = s->exp_strategy[ch];
        uint8_t *exp          = s->blocks[0].exp[ch];
        int exp_diff;

        /* estimate if the exponent variation & decide if they should be
           reused in the next frame */
        exp_strategy[0] = EXP_NEW;
        exp += AC3_MAX_COEFS;
        for (blk = 1; blk < s->num_blocks; blk++, exp += AC3_MAX_COEFS) {
            if (ch == CPL_CH) {
                if (!s->blocks[blk-1].cpl_in_use) {
                    exp_strategy[blk] = EXP_NEW;
                    continue;
                } else if (!s->blocks[blk].cpl_in_use) {
                    exp_strategy[blk] = EXP_REUSE;
                    continue;
                }
            } else if (s->blocks[blk].channel_in_cpl[ch] != s->blocks[blk-1].channel_in_cpl[ch]) {
                exp_strategy[blk] = EXP_NEW;
                continue;
            }
            exp_diff = s->mecc.sad[0](NULL, exp, exp - AC3_MAX_COEFS, 16, 16);
            exp_strategy[blk] = EXP_REUSE;
            if (ch == CPL_CH && exp_diff > (EXP_DIFF_THRESHOLD * (s->blocks[blk].end_freq[ch] - s->start_freq[ch]) / AC3_MAX_COEFS))
                exp_strategy[blk] = EXP_NEW;
            else if (ch > CPL_CH && exp_diff > EXP_DIFF_THRESHOLD)
                exp_strategy[blk] = EXP_NEW;
        }

        /* now select the encoding strategy type : if exponents are often
           recoded, we use a coarse encoding */
        blk = 0;
        while (blk < s->num_blocks) {
            blk1 = blk + 1;
            while (blk1 < s->num_blocks && exp_strategy[blk1] == EXP_REUSE)
                blk1++;
            exp_strategy[blk] = exp_strategy_reuse_tab[s->num_blks_code][blk1-blk-1];
            blk = blk1;
        }
    }
    if (s->lfe_on) {
        ch = s->lfe_channel;
        s->exp_strategy[ch][0] = EXP_D15;
        for (blk = 1; blk < s->num_blocks; blk++)
            s->exp_strategy[ch][blk] = EXP_REUSE;
    }

    /* for E-AC-3, determine frame exponent strategy */
    if (CONFIG_EAC3_ENCODER && s->eac3)
        ff_eac3_get_frame_exp_strategy(s);
}


/**
 * Update the exponents so that they are the ones the decoder will decode.
 *
 * @param[in,out] exp   array of exponents for 1 block in 1 channel
 * @param nb_exps       number of exponents in active bandwidth
 * @param exp_strategy  exponent strategy for the block
 * @param cpl           indicates if the block is in the coupling channel
 */
static void encode_exponents_blk_ch(uint8_t *exp, int nb_exps, int exp_strategy,
                                    int cpl)
{
    int nb_groups, i, k;

    nb_groups = exponent_group_tab[cpl][exp_strategy-1][nb_exps] * 3;

    /* for each group, compute the minimum exponent */
    switch(exp_strategy) {
    case EXP_D25:
        for (i = 1, k = 1-cpl; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            exp[i-cpl] = exp_min;
            k += 2;
        }
        break;
    case EXP_D45:
        for (i = 1, k = 1-cpl; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            if (exp[k+2] < exp_min)
                exp_min = exp[k+2];
            if (exp[k+3] < exp_min)
                exp_min = exp[k+3];
            exp[i-cpl] = exp_min;
            k += 4;
        }
        break;
    }

    /* constraint for DC exponent */
    if (!cpl && exp[0] > 15)
        exp[0] = 15;

    /* decrease the delta between each groups to within 2 so that they can be
       differentially encoded */
    for (i = 1; i <= nb_groups; i++)
        exp[i] = FFMIN(exp[i], exp[i-1] + 2);
    i--;
    while (--i >= 0)
        exp[i] = FFMIN(exp[i], exp[i+1] + 2);

    if (cpl)
        exp[-1] = exp[0] & ~1;

    /* now we have the exponent values the decoder will see */
    switch (exp_strategy) {
    case EXP_D25:
        for (i = nb_groups, k = (nb_groups * 2)-cpl; i > 0; i--) {
            uint8_t exp1 = exp[i-cpl];
            exp[k--] = exp1;
            exp[k--] = exp1;
        }
        break;
    case EXP_D45:
        for (i = nb_groups, k = (nb_groups * 4)-cpl; i > 0; i--) {
            exp[k] = exp[k-1] = exp[k-2] = exp[k-3] = exp[i-cpl];
            k -= 4;
        }
        break;
    }
}


/*
 * Encode exponents from original extracted form to what the decoder will see.
 * This copies and groups exponents based on exponent strategy and reduces
 * deltas between adjacent exponent groups so that they can be differentially
 * encoded.
 */
static void encode_exponents(AC3EncodeContext *s)
{
    int blk, blk1, ch, cpl;
    uint8_t *exp, *exp_strategy;
    int nb_coefs, num_reuse_blocks;

    for (ch = !s->cpl_on; ch <= s->channels; ch++) {
        exp          = s->blocks[0].exp[ch] + s->start_freq[ch];
        exp_strategy = s->exp_strategy[ch];

        cpl = (ch == CPL_CH);
        blk = 0;
        while (blk < s->num_blocks) {
            AC3Block *block = &s->blocks[blk];
            if (cpl && !block->cpl_in_use) {
                exp += AC3_MAX_COEFS;
                blk++;
                continue;
            }
            nb_coefs = block->end_freq[ch] - s->start_freq[ch];
            blk1 = blk + 1;

            /* count the number of EXP_REUSE blocks after the current block
               and set exponent reference block numbers */
            s->exp_ref_block[ch][blk] = blk;
            while (blk1 < s->num_blocks && exp_strategy[blk1] == EXP_REUSE) {
                s->exp_ref_block[ch][blk1] = blk;
                blk1++;
            }
            num_reuse_blocks = blk1 - blk - 1;

            /* for the EXP_REUSE case we select the min of the exponents */
            s->ac3dsp.ac3_exponent_min(exp-s->start_freq[ch], num_reuse_blocks,
                                       AC3_MAX_COEFS);

            encode_exponents_blk_ch(exp, nb_coefs, exp_strategy[blk], cpl);

            exp += AC3_MAX_COEFS * (num_reuse_blocks + 1);
            blk = blk1;
        }
    }

    /* reference block numbers have been changed, so reset ref_bap_set */
    s->ref_bap_set = 0;
}


/*
 * Count exponent bits based on bandwidth, coupling, and exponent strategies.
 */
static int count_exponent_bits(AC3EncodeContext *s)
{
    int blk, ch;
    int nb_groups, bit_count;

    bit_count = 0;
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            int exp_strategy = s->exp_strategy[ch][blk];
            int cpl          = (ch == CPL_CH);
            int nb_coefs     = block->end_freq[ch] - s->start_freq[ch];

            if (exp_strategy == EXP_REUSE)
                continue;

            nb_groups = exponent_group_tab[cpl][exp_strategy-1][nb_coefs];
            bit_count += 4 + (nb_groups * 7);
        }
    }

    return bit_count;
}


/**
 * Group exponents.
 * 3 delta-encoded exponents are in each 7-bit group. The number of groups
 * varies depending on exponent strategy and bandwidth.
 *
 * @param s  AC-3 encoder private context
 */
static void ac3_group_exponents(AC3EncodeContext *s)
{
    int blk, ch, i, cpl;
    int group_size, nb_groups;
    uint8_t *p;
    int delta0, delta1, delta2;
    int exp0, exp1;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            int exp_strategy = s->exp_strategy[ch][blk];
            if (exp_strategy == EXP_REUSE)
                continue;
            cpl = (ch == CPL_CH);
            group_size = exp_strategy + (exp_strategy == EXP_D45);
            nb_groups = exponent_group_tab[cpl][exp_strategy-1][block->end_freq[ch]-s->start_freq[ch]];
            p = block->exp[ch] + s->start_freq[ch] - cpl;

            /* DC exponent */
            exp1 = *p++;
            block->grouped_exp[ch][0] = exp1;

            /* remaining exponents are delta encoded */
            for (i = 1; i <= nb_groups; i++) {
                /* merge three delta in one code */
                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta0 = exp1 - exp0 + 2;
                av_assert2(delta0 >= 0 && delta0 <= 4);

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta1 = exp1 - exp0 + 2;
                av_assert2(delta1 >= 0 && delta1 <= 4);

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta2 = exp1 - exp0 + 2;
                av_assert2(delta2 >= 0 && delta2 <= 4);

                block->grouped_exp[ch][i] = ((delta0 * 5 + delta1) * 5) + delta2;
            }
        }
    }
}


/**
 * Calculate final exponents from the supplied MDCT coefficients and exponent shift.
 * Extract exponents from MDCT coefficients, calculate exponent strategies,
 * and encode final exponents.
 *
 * @param s  AC-3 encoder private context
 */
static void ac3_process_exponents(AC3EncodeContext *s)
{
    extract_exponents(s);

    compute_exp_strategy(s);

    encode_exponents(s);

    emms_c();
}


/*
 * Count frame bits that are based solely on fixed parameters.
 * This only has to be run once when the encoder is initialized.
 */
static void count_frame_bits_fixed(AC3EncodeContext *s)
{
    static const uint8_t frame_bits_inc[8] = { 0, 0, 2, 2, 2, 4, 2, 4 };
    int blk;
    int frame_bits;

    /* assumptions:
     *   no dynamic range codes
     *   bit allocation parameters do not change between blocks
     *   no delta bit allocation
     *   no skipped data
     *   no auxiliary data
     *   no E-AC-3 metadata
     */

    /* header */
    frame_bits = 16; /* sync info */
    if (s->eac3) {
        /* bitstream info header */
        frame_bits += 35;
        frame_bits += 1 + 1;
        if (s->num_blocks != 0x6)
            frame_bits++;
        frame_bits++;
        /* audio frame header */
        if (s->num_blocks == 6)
            frame_bits += 2;
        frame_bits += 10;
        /* exponent strategy */
        if (s->use_frame_exp_strategy)
            frame_bits += 5 * s->fbw_channels;
        else
            frame_bits += s->num_blocks * 2 * s->fbw_channels;
        if (s->lfe_on)
            frame_bits += s->num_blocks;
        /* converter exponent strategy */
        if (s->num_blks_code != 0x3)
            frame_bits++;
        else
            frame_bits += s->fbw_channels * 5;
        /* snr offsets */
        frame_bits += 10;
        /* block start info */
        if (s->num_blocks != 1)
            frame_bits++;
    } else {
        frame_bits += 49;
        frame_bits += frame_bits_inc[s->channel_mode];
    }

    /* audio blocks */
    for (blk = 0; blk < s->num_blocks; blk++) {
        if (!s->eac3) {
            /* block switch flags */
            frame_bits += s->fbw_channels;

            /* dither flags */
            frame_bits += s->fbw_channels;
        }

        /* dynamic range */
        frame_bits++;

        /* spectral extension */
        if (s->eac3)
            frame_bits++;

        /* coupling strategy exists: cplstre */
        if (!s->eac3)
            frame_bits++;

        if (!s->eac3) {
            /* exponent strategy */
            frame_bits += 2 * s->fbw_channels;
            if (s->lfe_on)
                frame_bits++;

            /* bit allocation params */
            frame_bits++;
            if (!blk)
                frame_bits += 2 + 2 + 2 + 2 + 3;
        }

        /* snroffste for AC-3, convsnroffste for E-AC-3 */
        frame_bits++;

        if (!s->eac3) {
            /* delta bit allocation */
            frame_bits++;

            /* skipped data */
            frame_bits++;
        }
    }

    /* auxiliary data */
    frame_bits++;

    /* CRC */
    frame_bits += 1 + 16;

    s->frame_bits_fixed = frame_bits;
}


/*
 * Initialize bit allocation.
 * Set default parameter codes and calculate parameter values.
 */
static av_cold void bit_alloc_init(AC3EncodeContext *s)
{
    int ch;

    /* init default parameters */
    s->slow_decay_code = 2;
    s->fast_decay_code = 1;
    s->slow_gain_code  = 1;
    s->db_per_bit_code = s->eac3 ? 2 : 3;
    s->floor_code      = 7;
    for (ch = 0; ch <= s->channels; ch++)
        s->fast_gain_code[ch] = 4;

    /* initial snr offset */
    s->coarse_snr_offset = 40;

    /* compute real values */
    /* currently none of these values change during encoding, so we can just
       set them once at initialization */
    s->bit_alloc.slow_decay = ff_ac3_slow_decay_tab[s->slow_decay_code];
    s->bit_alloc.fast_decay = ff_ac3_fast_decay_tab[s->fast_decay_code];
    s->bit_alloc.slow_gain  = ff_ac3_slow_gain_tab[s->slow_gain_code];
    s->bit_alloc.db_per_bit = ff_ac3_db_per_bit_tab[s->db_per_bit_code];
    s->bit_alloc.floor      = ff_ac3_floor_tab[s->floor_code];
    s->bit_alloc.cpl_fast_leak = 0;
    s->bit_alloc.cpl_slow_leak = 0;

    count_frame_bits_fixed(s);
}


/*
 * Count the bits used to encode the frame, minus exponents and mantissas.
 * Bits based on fixed parameters have already been counted, so now we just
 * have to add the bits based on parameters that change during encoding.
 */
static void count_frame_bits(AC3EncodeContext *s)
{
    AC3EncOptions *opt = &s->options;
    int blk, ch;
    int frame_bits = 0;

    /* header */
    if (s->eac3) {
        if (opt->eac3_mixing_metadata) {
            if (s->channel_mode > AC3_CHMODE_STEREO)
                frame_bits += 2;
            if (s->has_center)
                frame_bits += 6;
            if (s->has_surround)
                frame_bits += 6;
            frame_bits += s->lfe_on;
            frame_bits += 1 + 1 + 2;
            if (s->channel_mode < AC3_CHMODE_STEREO)
                frame_bits++;
            frame_bits++;
        }
        if (opt->eac3_info_metadata) {
            frame_bits += 3 + 1 + 1;
            if (s->channel_mode == AC3_CHMODE_STEREO)
                frame_bits += 2 + 2;
            if (s->channel_mode >= AC3_CHMODE_2F2R)
                frame_bits += 2;
            frame_bits++;
            if (opt->audio_production_info)
                frame_bits += 5 + 2 + 1;
            frame_bits++;
        }
        /* coupling */
        if (s->channel_mode > AC3_CHMODE_MONO) {
            frame_bits++;
            for (blk = 1; blk < s->num_blocks; blk++) {
                AC3Block *block = &s->blocks[blk];
                frame_bits++;
                if (block->new_cpl_strategy)
                    frame_bits++;
            }
        }
        /* coupling exponent strategy */
        if (s->cpl_on) {
            if (s->use_frame_exp_strategy) {
                frame_bits += 5;
            } else {
                for (blk = 0; blk < s->num_blocks; blk++)
                    frame_bits += 2 * s->blocks[blk].cpl_in_use;
            }
        }
    } else {
        if (opt->audio_production_info)
            frame_bits += 7;
        if (s->bitstream_id == 6) {
            if (opt->extended_bsi_1)
                frame_bits += 14;
            if (opt->extended_bsi_2)
                frame_bits += 14;
        }
    }

    /* audio blocks */
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];

        /* coupling strategy */
        if (block->new_cpl_strategy) {
            if (!s->eac3)
                frame_bits++;
            if (block->cpl_in_use) {
                if (s->eac3)
                    frame_bits++;
                if (!s->eac3 || s->channel_mode != AC3_CHMODE_STEREO)
                    frame_bits += s->fbw_channels;
                if (s->channel_mode == AC3_CHMODE_STEREO)
                    frame_bits++;
                frame_bits += 4 + 4;
                if (s->eac3)
                    frame_bits++;
                else
                    frame_bits += s->num_cpl_subbands - 1;
            }
        }

        /* coupling coordinates */
        if (block->cpl_in_use) {
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (block->channel_in_cpl[ch]) {
                    if (!s->eac3 || block->new_cpl_coords[ch] != 2)
                        frame_bits++;
                    if (block->new_cpl_coords[ch]) {
                        frame_bits += 2;
                        frame_bits += (4 + 4) * s->num_cpl_bands;
                    }
                }
            }
        }

        /* stereo rematrixing */
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            if (!s->eac3 || blk > 0)
                frame_bits++;
            if (s->blocks[blk].new_rematrixing_strategy)
                frame_bits += block->num_rematrixing_bands;
        }

        /* bandwidth codes & gain range */
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                if (!block->channel_in_cpl[ch])
                    frame_bits += 6;
                frame_bits += 2;
            }
        }

        /* coupling exponent strategy */
        if (!s->eac3 && block->cpl_in_use)
            frame_bits += 2;

        /* snr offsets and fast gain codes */
        if (!s->eac3) {
            if (block->new_snr_offsets)
                frame_bits += 6 + (s->channels + block->cpl_in_use) * (4 + 3);
        }

        /* coupling leak info */
        if (block->cpl_in_use) {
            if (!s->eac3 || block->new_cpl_leak != 2)
                frame_bits++;
            if (block->new_cpl_leak)
                frame_bits += 3 + 3;
        }
    }

    s->frame_bits = s->frame_bits_fixed + frame_bits;
}


/*
 * Calculate masking curve based on the final exponents.
 * Also calculate the power spectral densities to use in future calculations.
 */
static void bit_alloc_masking(AC3EncodeContext *s)
{
    int blk, ch;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            /* We only need psd and mask for calculating bap.
               Since we currently do not calculate bap when exponent
               strategy is EXP_REUSE we do not need to calculate psd or mask. */
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                ff_ac3_bit_alloc_calc_psd(block->exp[ch], s->start_freq[ch],
                                          block->end_freq[ch], block->psd[ch],
                                          block->band_psd[ch]);
                ff_ac3_bit_alloc_calc_mask(&s->bit_alloc, block->band_psd[ch],
                                           s->start_freq[ch], block->end_freq[ch],
                                           ff_ac3_fast_gain_tab[s->fast_gain_code[ch]],
                                           ch == s->lfe_channel,
                                           DBA_NONE, 0, NULL, NULL, NULL,
                                           block->mask[ch]);
            }
        }
    }
}


/*
 * Ensure that bap for each block and channel point to the current bap_buffer.
 * They may have been switched during the bit allocation search.
 */
static void reset_block_bap(AC3EncodeContext *s)
{
    int blk, ch;
    uint8_t *ref_bap;

    if (s->ref_bap[0][0] == s->bap_buffer && s->ref_bap_set)
        return;

    ref_bap = s->bap_buffer;
    for (ch = 0; ch <= s->channels; ch++) {
        for (blk = 0; blk < s->num_blocks; blk++)
            s->ref_bap[ch][blk] = ref_bap + AC3_MAX_COEFS * s->exp_ref_block[ch][blk];
        ref_bap += AC3_MAX_COEFS * s->num_blocks;
    }
    s->ref_bap_set = 1;
}


/**
 * Initialize mantissa counts.
 * These are set so that they are padded to the next whole group size when bits
 * are counted in compute_mantissa_size.
 *
 * @param[in,out] mant_cnt  running counts for each bap value for each block
 */
static void count_mantissa_bits_init(uint16_t mant_cnt[AC3_MAX_BLOCKS][16])
{
    int blk;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        memset(mant_cnt[blk], 0, sizeof(mant_cnt[blk]));
        mant_cnt[blk][1] = mant_cnt[blk][2] = 2;
        mant_cnt[blk][4] = 1;
    }
}


/**
 * Update mantissa bit counts for all blocks in 1 channel in a given bandwidth
 * range.
 *
 * @param s                 AC-3 encoder private context
 * @param ch                channel index
 * @param[in,out] mant_cnt  running counts for each bap value for each block
 * @param start             starting coefficient bin
 * @param end               ending coefficient bin
 */
static void count_mantissa_bits_update_ch(AC3EncodeContext *s, int ch,
                                          uint16_t mant_cnt[AC3_MAX_BLOCKS][16],
                                          int start, int end)
{
    int blk;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (ch == CPL_CH && !block->cpl_in_use)
            continue;
        s->ac3dsp.update_bap_counts(mant_cnt[blk],
                                    s->ref_bap[ch][blk] + start,
                                    FFMIN(end, block->end_freq[ch]) - start);
    }
}


/*
 * Count the number of mantissa bits in the frame based on the bap values.
 */
static int count_mantissa_bits(AC3EncodeContext *s)
{
    int ch, max_end_freq;
    LOCAL_ALIGNED_16(uint16_t, mant_cnt, [AC3_MAX_BLOCKS], [16]);

    count_mantissa_bits_init(mant_cnt);

    max_end_freq = s->bandwidth_code * 3 + 73;
    for (ch = !s->cpl_enabled; ch <= s->channels; ch++)
        count_mantissa_bits_update_ch(s, ch, mant_cnt, s->start_freq[ch],
                                      max_end_freq);

    return s->ac3dsp.compute_mantissa_size(mant_cnt);
}


/**
 * Run the bit allocation with a given SNR offset.
 * This calculates the bit allocation pointers that will be used to determine
 * the quantization of each mantissa.
 *
 * @param s           AC-3 encoder private context
 * @param snr_offset  SNR offset, 0 to 1023
 * @return the number of bits needed for mantissas if the given SNR offset is
 *         is used.
 */
static int bit_alloc(AC3EncodeContext *s, int snr_offset)
{
    int blk, ch;

    snr_offset = (snr_offset - 240) * 4;

    reset_block_bap(s);
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];

        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            /* Currently the only bit allocation parameters which vary across
               blocks within a frame are the exponent values.  We can take
               advantage of that by reusing the bit allocation pointers
               whenever we reuse exponents. */
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                s->ac3dsp.bit_alloc_calc_bap(block->mask[ch], block->psd[ch],
                                             s->start_freq[ch], block->end_freq[ch],
                                             snr_offset, s->bit_alloc.floor,
                                             ff_ac3_bap_tab, s->ref_bap[ch][blk]);
            }
        }
    }
    return count_mantissa_bits(s);
}


/*
 * Constant bitrate bit allocation search.
 * Find the largest SNR offset that will allow data to fit in the frame.
 */
static int cbr_bit_allocation(AC3EncodeContext *s)
{
    int ch;
    int bits_left;
    int snr_offset, snr_incr;

    bits_left = 8 * s->frame_size - (s->frame_bits + s->exponent_bits);
    if (bits_left < 0)
        return AVERROR(EINVAL);

    snr_offset = s->coarse_snr_offset << 4;

    /* if previous frame SNR offset was 1023, check if current frame can also
       use SNR offset of 1023. if so, skip the search. */
    if ((snr_offset | s->fine_snr_offset[1]) == 1023) {
        if (bit_alloc(s, 1023) <= bits_left)
            return 0;
    }

    while (snr_offset >= 0 &&
           bit_alloc(s, snr_offset) > bits_left) {
        snr_offset -= 64;
    }
    if (snr_offset < 0)
        return AVERROR(EINVAL);

    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    for (snr_incr = 64; snr_incr > 0; snr_incr >>= 2) {
        while (snr_offset + snr_incr <= 1023 &&
               bit_alloc(s, snr_offset + snr_incr) <= bits_left) {
            snr_offset += snr_incr;
            FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
        }
    }
    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    reset_block_bap(s);

    s->coarse_snr_offset = snr_offset >> 4;
    for (ch = !s->cpl_on; ch <= s->channels; ch++)
        s->fine_snr_offset[ch] = snr_offset & 0xF;

    return 0;
}


/*
 * Perform bit allocation search.
 * Finds the SNR offset value that maximizes quality and fits in the specified
 * frame size.  Output is the SNR offset and a set of bit allocation pointers
 * used to quantize the mantissas.
 */
static int ac3_compute_bit_allocation(AC3EncodeContext *s)
{
    count_frame_bits(s);

    s->exponent_bits = count_exponent_bits(s);

    bit_alloc_masking(s);

    return cbr_bit_allocation(s);
}


/**
 * Symmetric quantization on 'levels' levels.
 *
 * @param c       unquantized coefficient
 * @param e       exponent
 * @param levels  number of quantization levels
 * @return        quantized coefficient
 */
static inline int sym_quant(int c, int e, int levels)
{
    int v = (((levels * c) >> (24 - e)) + levels) >> 1;
    av_assert2(v >= 0 && v < levels);
    return v;
}


/**
 * Asymmetric quantization on 2^qbits levels.
 *
 * @param c      unquantized coefficient
 * @param e      exponent
 * @param qbits  number of quantization bits
 * @return       quantized coefficient
 */
static inline int asym_quant(int c, int e, int qbits)
{
    int m;

    c = (((c * (1<<e)) >> (24 - qbits)) + 1) >> 1;
    m = (1 << (qbits-1));
    if (c >= m)
        c = m - 1;
    av_assert2(c >= -m);
    return c;
}


/**
 * Quantize a set of mantissas for a single channel in a single block.
 *
 * @param s           Mantissa count context
 * @param fixed_coef  unquantized fixed-point coefficients
 * @param exp         exponents
 * @param bap         bit allocation pointer indices
 * @param[out] qmant  quantized coefficients
 * @param start_freq  starting coefficient bin
 * @param end_freq    ending coefficient bin
 */
static void quantize_mantissas_blk_ch(AC3Mant *s, int32_t *fixed_coef,
                                      uint8_t *exp, uint8_t *bap,
                                      int16_t *qmant, int start_freq,
                                      int end_freq)
{
    int i;

    for (i = start_freq; i < end_freq; i++) {
        int c = fixed_coef[i];
        int e = exp[i];
        int v = bap[i];
        switch (v) {
        case 0:
            break;
        case 1:
            v = sym_quant(c, e, 3);
            switch (s->mant1_cnt) {
            case 0:
                s->qmant1_ptr = &qmant[i];
                v = 9 * v;
                s->mant1_cnt = 1;
                break;
            case 1:
                *s->qmant1_ptr += 3 * v;
                s->mant1_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant1_ptr += v;
                s->mant1_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 2:
            v = sym_quant(c, e, 5);
            switch (s->mant2_cnt) {
            case 0:
                s->qmant2_ptr = &qmant[i];
                v = 25 * v;
                s->mant2_cnt = 1;
                break;
            case 1:
                *s->qmant2_ptr += 5 * v;
                s->mant2_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant2_ptr += v;
                s->mant2_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 3:
            v = sym_quant(c, e, 7);
            break;
        case 4:
            v = sym_quant(c, e, 11);
            switch (s->mant4_cnt) {
            case 0:
                s->qmant4_ptr = &qmant[i];
                v = 11 * v;
                s->mant4_cnt = 1;
                break;
            default:
                *s->qmant4_ptr += v;
                s->mant4_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 5:
            v = sym_quant(c, e, 15);
            break;
        case 14:
            v = asym_quant(c, e, 14);
            break;
        case 15:
            v = asym_quant(c, e, 16);
            break;
        default:
            v = asym_quant(c, e, v - 1);
            break;
        }
        qmant[i] = v;
    }
}


/**
 * Quantize mantissas using coefficients, exponents, and bit allocation pointers.
 *
 * @param s  AC-3 encoder private context
 */
static void ac3_quantize_mantissas(AC3EncodeContext *s)
{
    int blk, ch, ch0=0, got_cpl;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        AC3Mant m = { 0 };

        got_cpl = !block->cpl_in_use;
        for (ch = 1; ch <= s->channels; ch++) {
            if (!got_cpl && ch > 1 && block->channel_in_cpl[ch-1]) {
                ch0     = ch - 1;
                ch      = CPL_CH;
                got_cpl = 1;
            }
            quantize_mantissas_blk_ch(&m, block->fixed_coef[ch],
                                      s->blocks[s->exp_ref_block[ch][blk]].exp[ch],
                                      s->ref_bap[ch][blk], block->qmant[ch],
                                      s->start_freq[ch], block->end_freq[ch]);
            if (ch == CPL_CH)
                ch = ch0;
        }
    }
}


/*
 * Write the AC-3 frame header to the output bitstream.
 */
static void ac3_output_frame_header(AC3EncodeContext *s, PutBitContext *pb)
{
    AC3EncOptions *opt = &s->options;

    put_bits(pb, 16, 0x0b77);   /* frame header */
    put_bits(pb, 16, 0);        /* crc1: will be filled later */
    put_bits(pb, 2,  s->bit_alloc.sr_code);
    put_bits(pb, 6,  s->frame_size_code + (s->frame_size - s->frame_size_min) / 2);
    put_bits(pb, 5,  s->bitstream_id);
    put_bits(pb, 3,  s->bitstream_mode);
    put_bits(pb, 3,  s->channel_mode);
    if ((s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO)
        put_bits(pb, 2, s->center_mix_level);
    if (s->channel_mode & 0x04)
        put_bits(pb, 2, s->surround_mix_level);
    if (s->channel_mode == AC3_CHMODE_STEREO)
        put_bits(pb, 2, opt->dolby_surround_mode);
    put_bits(pb, 1, s->lfe_on); /* LFE */
    put_bits(pb, 5, -opt->dialogue_level);
    put_bits(pb, 1, 0);         /* no compression control word */
    put_bits(pb, 1, 0);         /* no lang code */
    put_bits(pb, 1, opt->audio_production_info);
    if (opt->audio_production_info) {
        put_bits(pb, 5, opt->mixing_level - 80);
        put_bits(pb, 2, opt->room_type);
    }
    put_bits(pb, 1, opt->copyright);
    put_bits(pb, 1, opt->original);
    if (s->bitstream_id == 6) {
        /* alternate bit stream syntax */
        put_bits(pb, 1, opt->extended_bsi_1);
        if (opt->extended_bsi_1) {
            put_bits(pb, 2, opt->preferred_stereo_downmix);
            put_bits(pb, 3, s->ltrt_center_mix_level);
            put_bits(pb, 3, s->ltrt_surround_mix_level);
            put_bits(pb, 3, s->loro_center_mix_level);
            put_bits(pb, 3, s->loro_surround_mix_level);
        }
        put_bits(pb, 1, opt->extended_bsi_2);
        if (opt->extended_bsi_2) {
            put_bits(pb, 2, opt->dolby_surround_ex_mode);
            put_bits(pb, 2, opt->dolby_headphone_mode);
            put_bits(pb, 1, opt->ad_converter_type);
            put_bits(pb, 9, 0);     /* xbsi2 and encinfo : reserved */
        }
    } else {
        put_bits(pb, 1, 0);     /* no time code 1 */
        put_bits(pb, 1, 0);     /* no time code 2 */
    }
    put_bits(pb, 1, 0);         /* no additional bit stream info */
}


/*
 * Write one audio block to the output bitstream.
 */
static void output_audio_block(AC3EncodeContext *s, PutBitContext *pb, int blk)
{
    int ch, i, baie, bnd, got_cpl, av_uninit(ch0);
    AC3Block *block = &s->blocks[blk];

    /* block switching */
    if (!s->eac3) {
        for (ch = 0; ch < s->fbw_channels; ch++)
            put_bits(pb, 1, 0);
    }

    /* dither flags */
    if (!s->eac3) {
        for (ch = 0; ch < s->fbw_channels; ch++)
            put_bits(pb, 1, 1);
    }

    /* dynamic range codes */
    put_bits(pb, 1, 0);

    /* spectral extension */
    if (s->eac3)
        put_bits(pb, 1, 0);

    /* channel coupling */
    if (!s->eac3)
        put_bits(pb, 1, block->new_cpl_strategy);
    if (block->new_cpl_strategy) {
        if (!s->eac3)
            put_bits(pb, 1, block->cpl_in_use);
        if (block->cpl_in_use) {
            int start_sub, end_sub;
            if (s->eac3)
                put_bits(pb, 1, 0); /* enhanced coupling */
            if (!s->eac3 || s->channel_mode != AC3_CHMODE_STEREO) {
                for (ch = 1; ch <= s->fbw_channels; ch++)
                    put_bits(pb, 1, block->channel_in_cpl[ch]);
            }
            if (s->channel_mode == AC3_CHMODE_STEREO)
                put_bits(pb, 1, 0); /* phase flags in use */
            start_sub = (s->start_freq[CPL_CH] - 37) / 12;
            end_sub   = (s->cpl_end_freq       - 37) / 12;
            put_bits(pb, 4, start_sub);
            put_bits(pb, 4, end_sub - 3);
            /* coupling band structure */
            if (s->eac3) {
                put_bits(pb, 1, 0); /* use default */
            } else {
                for (bnd = start_sub+1; bnd < end_sub; bnd++)
                    put_bits(pb, 1, ff_eac3_default_cpl_band_struct[bnd]);
            }
        }
    }

    /* coupling coordinates */
    if (block->cpl_in_use) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (block->channel_in_cpl[ch]) {
                if (!s->eac3 || block->new_cpl_coords[ch] != 2)
                    put_bits(pb, 1, block->new_cpl_coords[ch]);
                if (block->new_cpl_coords[ch]) {
                    put_bits(pb, 2, block->cpl_master_exp[ch]);
                    for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                        put_bits(pb, 4, block->cpl_coord_exp [ch][bnd]);
                        put_bits(pb, 4, block->cpl_coord_mant[ch][bnd]);
                    }
                }
            }
        }
    }

    /* stereo rematrixing */
    if (s->channel_mode == AC3_CHMODE_STEREO) {
        if (!s->eac3 || blk > 0)
            put_bits(pb, 1, block->new_rematrixing_strategy);
        if (block->new_rematrixing_strategy) {
            /* rematrixing flags */
            for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++)
                put_bits(pb, 1, block->rematrixing_flags[bnd]);
        }
    }

    /* exponent strategy */
    if (!s->eac3) {
        for (ch = !block->cpl_in_use; ch <= s->fbw_channels; ch++)
            put_bits(pb, 2, s->exp_strategy[ch][blk]);
        if (s->lfe_on)
            put_bits(pb, 1, s->exp_strategy[s->lfe_channel][blk]);
    }

    /* bandwidth */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
        if (s->exp_strategy[ch][blk] != EXP_REUSE && !block->channel_in_cpl[ch])
            put_bits(pb, 6, s->bandwidth_code);
    }

    /* exponents */
    for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
        int nb_groups;
        int cpl = (ch == CPL_CH);

        if (s->exp_strategy[ch][blk] == EXP_REUSE)
            continue;

        /* DC exponent */
        put_bits(pb, 4, block->grouped_exp[ch][0] >> cpl);

        /* exponent groups */
        nb_groups = exponent_group_tab[cpl][s->exp_strategy[ch][blk]-1][block->end_freq[ch]-s->start_freq[ch]];
        for (i = 1; i <= nb_groups; i++)
            put_bits(pb, 7, block->grouped_exp[ch][i]);

        /* gain range info */
        if (ch != s->lfe_channel && !cpl)
            put_bits(pb, 2, 0);
    }

    /* bit allocation info */
    if (!s->eac3) {
        baie = (blk == 0);
        put_bits(pb, 1, baie);
        if (baie) {
            put_bits(pb, 2, s->slow_decay_code);
            put_bits(pb, 2, s->fast_decay_code);
            put_bits(pb, 2, s->slow_gain_code);
            put_bits(pb, 2, s->db_per_bit_code);
            put_bits(pb, 3, s->floor_code);
        }
    }

    /* snr offset */
    if (!s->eac3) {
        put_bits(pb, 1, block->new_snr_offsets);
        if (block->new_snr_offsets) {
            put_bits(pb, 6, s->coarse_snr_offset);
            for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
                put_bits(pb, 4, s->fine_snr_offset[ch]);
                put_bits(pb, 3, s->fast_gain_code[ch]);
            }
        }
    } else {
        put_bits(pb, 1, 0); /* no converter snr offset */
    }

    /* coupling leak */
    if (block->cpl_in_use) {
        if (!s->eac3 || block->new_cpl_leak != 2)
            put_bits(pb, 1, block->new_cpl_leak);
        if (block->new_cpl_leak) {
            put_bits(pb, 3, s->bit_alloc.cpl_fast_leak);
            put_bits(pb, 3, s->bit_alloc.cpl_slow_leak);
        }
    }

    if (!s->eac3) {
        put_bits(pb, 1, 0); /* no delta bit allocation */
        put_bits(pb, 1, 0); /* no data to skip */
    }

    /* mantissas */
    got_cpl = !block->cpl_in_use;
    for (ch = 1; ch <= s->channels; ch++) {
        int b, q;

        if (!got_cpl && ch > 1 && block->channel_in_cpl[ch-1]) {
            ch0     = ch - 1;
            ch      = CPL_CH;
            got_cpl = 1;
        }
        for (i = s->start_freq[ch]; i < block->end_freq[ch]; i++) {
            q = block->qmant[ch][i];
            b = s->ref_bap[ch][blk][i];
            switch (b) {
            case 0:                                          break;
            case 1: if (q != 128) put_bits (pb,   5, q); break;
            case 2: if (q != 128) put_bits (pb,   7, q); break;
            case 3:               put_sbits(pb,   3, q); break;
            case 4: if (q != 128) put_bits (pb,   7, q); break;
            case 14:              put_sbits(pb,  14, q); break;
            case 15:              put_sbits(pb,  16, q); break;
            default:              put_sbits(pb, b-1, q); break;
            }
        }
        if (ch == CPL_CH)
            ch = ch0;
    }
}


/** CRC-16 Polynomial */
#define CRC16_POLY ((1 << 0) | (1 << 2) | (1 << 15) | (1 << 16))


static unsigned int mul_poly(unsigned int a, unsigned int b, unsigned int poly)
{
    unsigned int c;

    c = 0;
    while (a) {
        if (a & 1)
            c ^= b;
        a = a >> 1;
        b = b << 1;
        if (b & (1 << 16))
            b ^= poly;
    }
    return c;
}


static unsigned int pow_poly(unsigned int a, unsigned int n, unsigned int poly)
{
    unsigned int r;
    r = 1;
    while (n) {
        if (n & 1)
            r = mul_poly(r, a, poly);
        a = mul_poly(a, a, poly);
        n >>= 1;
    }
    return r;
}


/*
 * Fill the end of the frame with 0's and compute the two CRCs.
 */
static void output_frame_end(AC3EncodeContext *s, PutBitContext *pb)
{
    const AVCRC *crc_ctx = av_crc_get_table(AV_CRC_16_ANSI);
    int frame_size_58, pad_bytes, crc1, crc2, crc_inv;
    uint8_t *frame;

    frame_size_58 = ((s->frame_size >> 2) + (s->frame_size >> 4)) << 1;

    /* pad the remainder of the frame with zeros */
    av_assert2(s->frame_size * 8 - put_bits_count(pb) >= 18);
    flush_put_bits(pb);
    frame = pb->buf;
    pad_bytes = s->frame_size - (put_bits_ptr(pb) - frame) - 2;
    av_assert2(pad_bytes >= 0);
    if (pad_bytes > 0)
        memset(put_bits_ptr(pb), 0, pad_bytes);

    if (s->eac3) {
        /* compute crc2 */
        crc2 = av_crc(crc_ctx, 0, frame + 2, s->frame_size - 4);
    } else {
        /* compute crc1 */
        /* this is not so easy because it is at the beginning of the data... */
        crc1    = av_bswap16(av_crc(crc_ctx, 0, frame + 4, frame_size_58 - 4));
        crc_inv = s->crc_inv[s->frame_size > s->frame_size_min];
        crc1    = mul_poly(crc_inv, crc1, CRC16_POLY);
        AV_WB16(frame + 2, crc1);

        /* compute crc2 */
        crc2 = av_crc(crc_ctx, 0, frame + frame_size_58,
                      s->frame_size - frame_size_58 - 2);
    }
    crc2 = av_bswap16(crc2);
    /* ensure crc2 does not match sync word by flipping crcrsv bit if needed */
    if (crc2 == 0x0B77) {
        /* The CRC generator polynomial is x^16 + x^15 + x^2 + 1,
         * so xor'ing with 0x18005 does not affect the CRC. */
        frame[s->frame_size - 3] ^= 0x1;
        crc2                     ^= 0x8005;
    }
    AV_WB16(frame + s->frame_size - 2, crc2);
}


/**
 * Write the frame to the output bitstream.
 *
 * @param s      AC-3 encoder private context
 * @param frame  output data buffer
 */
static void ac3_output_frame(AC3EncodeContext *s, unsigned char *frame)
{
    PutBitContext pb;
    int blk;

    init_put_bits(&pb, frame, s->frame_size);

    s->output_frame_header(s, &pb);

    for (blk = 0; blk < s->num_blocks; blk++)
        output_audio_block(s, &pb, blk);

    output_frame_end(s, &pb);
}

int ff_ac3_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                        const AVFrame *frame, int *got_packet_ptr)
{
    AC3EncodeContext *const s = avctx->priv_data;
    int ret;

    if (s->options.allow_per_frame_metadata) {
        ret = ac3_validate_metadata(s);
        if (ret)
            return ret;
    }

    if (s->bit_alloc.sr_code == 1 || s->eac3)
        ac3_adjust_frame_size(s);

    s->encode_frame(s, frame->extended_data);

    ac3_apply_rematrixing(s);

    ac3_process_exponents(s);

    ret = ac3_compute_bit_allocation(s);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Bit allocation failed. Try increasing the bitrate.\n");
        return ret;
    }

    ac3_group_exponents(s);

    ac3_quantize_mantissas(s);

    ret = ff_get_encode_buffer(avctx, avpkt, s->frame_size, 0);
    if (ret < 0)
        return ret;
    ac3_output_frame(s, avpkt->data);

    if (frame->pts != AV_NOPTS_VALUE)
        avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->initial_padding);

    *got_packet_ptr = 1;
    return 0;
}

static void dprint_options(AC3EncodeContext *s)
{
#ifdef DEBUG
    AVCodecContext *avctx = s->avctx;
    AC3EncOptions *opt = &s->options;
    const char *msg;
    char strbuf[32];

    switch (s->bitstream_id) {
    case  6: msg = "AC-3 (alt syntax)";       break;
    case  8: msg = "AC-3 (standard)";         break;
    case 16: msg = "E-AC-3 (enhanced)";       break;
    default: msg = "ERROR";
    }
    ff_dlog(avctx, "bitstream_id: %s (%d)\n", msg, s->bitstream_id);
    ff_dlog(avctx, "sample_fmt: %s\n", av_get_sample_fmt_name(avctx->sample_fmt));
    av_channel_layout_describe(&avctx->ch_layout, strbuf, sizeof(strbuf));
    ff_dlog(avctx, "channel_layout: %s\n", strbuf);
    ff_dlog(avctx, "sample_rate: %d\n", s->sample_rate);
    ff_dlog(avctx, "bit_rate: %d\n", s->bit_rate);
    ff_dlog(avctx, "blocks/frame: %d (code=%d)\n", s->num_blocks, s->num_blks_code);
    if (s->cutoff)
        ff_dlog(avctx, "cutoff: %d\n", s->cutoff);

    ff_dlog(avctx, "per_frame_metadata: %s\n",
            opt->allow_per_frame_metadata?"on":"off");
    if (s->has_center)
        ff_dlog(avctx, "center_mixlev: %0.3f (%d)\n", opt->center_mix_level,
                s->center_mix_level);
    else
        ff_dlog(avctx, "center_mixlev: {not written}\n");
    if (s->has_surround)
        ff_dlog(avctx, "surround_mixlev: %0.3f (%d)\n", opt->surround_mix_level,
                s->surround_mix_level);
    else
        ff_dlog(avctx, "surround_mixlev: {not written}\n");
    if (opt->audio_production_info) {
        ff_dlog(avctx, "mixing_level: %ddB\n", opt->mixing_level);
        switch (opt->room_type) {
        case AC3ENC_OPT_NOT_INDICATED: msg = "notindicated"; break;
        case AC3ENC_OPT_LARGE_ROOM:    msg = "large";        break;
        case AC3ENC_OPT_SMALL_ROOM:    msg = "small";        break;
        default:
            snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->room_type);
            msg = strbuf;
        }
        ff_dlog(avctx, "room_type: %s\n", msg);
    } else {
        ff_dlog(avctx, "mixing_level: {not written}\n");
        ff_dlog(avctx, "room_type: {not written}\n");
    }
    ff_dlog(avctx, "copyright: %s\n", opt->copyright?"on":"off");
    ff_dlog(avctx, "dialnorm: %ddB\n", opt->dialogue_level);
    if (s->channel_mode == AC3_CHMODE_STEREO) {
        switch (opt->dolby_surround_mode) {
        case AC3ENC_OPT_NOT_INDICATED: msg = "notindicated"; break;
        case AC3ENC_OPT_MODE_ON:       msg = "on";           break;
        case AC3ENC_OPT_MODE_OFF:      msg = "off";          break;
        default:
            snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->dolby_surround_mode);
            msg = strbuf;
        }
        ff_dlog(avctx, "dsur_mode: %s\n", msg);
    } else {
        ff_dlog(avctx, "dsur_mode: {not written}\n");
    }
    ff_dlog(avctx, "original: %s\n", opt->original?"on":"off");

    if (s->bitstream_id == 6) {
        if (opt->extended_bsi_1) {
            switch (opt->preferred_stereo_downmix) {
            case AC3ENC_OPT_NOT_INDICATED: msg = "notindicated"; break;
            case AC3ENC_OPT_DOWNMIX_LTRT:  msg = "ltrt";         break;
            case AC3ENC_OPT_DOWNMIX_LORO:  msg = "loro";         break;
            default:
                snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->preferred_stereo_downmix);
                msg = strbuf;
            }
            ff_dlog(avctx, "dmix_mode: %s\n", msg);
            ff_dlog(avctx, "ltrt_cmixlev: %0.3f (%d)\n",
                    opt->ltrt_center_mix_level, s->ltrt_center_mix_level);
            ff_dlog(avctx, "ltrt_surmixlev: %0.3f (%d)\n",
                    opt->ltrt_surround_mix_level, s->ltrt_surround_mix_level);
            ff_dlog(avctx, "loro_cmixlev: %0.3f (%d)\n",
                    opt->loro_center_mix_level, s->loro_center_mix_level);
            ff_dlog(avctx, "loro_surmixlev: %0.3f (%d)\n",
                    opt->loro_surround_mix_level, s->loro_surround_mix_level);
        } else {
            ff_dlog(avctx, "extended bitstream info 1: {not written}\n");
        }
        if (opt->extended_bsi_2) {
            switch (opt->dolby_surround_ex_mode) {
            case AC3ENC_OPT_NOT_INDICATED: msg = "notindicated"; break;
            case AC3ENC_OPT_MODE_ON:       msg = "on";           break;
            case AC3ENC_OPT_MODE_OFF:      msg = "off";          break;
            default:
                snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->dolby_surround_ex_mode);
                msg = strbuf;
            }
            ff_dlog(avctx, "dsurex_mode: %s\n", msg);
            switch (opt->dolby_headphone_mode) {
            case AC3ENC_OPT_NOT_INDICATED: msg = "notindicated"; break;
            case AC3ENC_OPT_MODE_ON:       msg = "on";           break;
            case AC3ENC_OPT_MODE_OFF:      msg = "off";          break;
            default:
                snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->dolby_headphone_mode);
                msg = strbuf;
            }
            ff_dlog(avctx, "dheadphone_mode: %s\n", msg);

            switch (opt->ad_converter_type) {
            case AC3ENC_OPT_ADCONV_STANDARD: msg = "standard"; break;
            case AC3ENC_OPT_ADCONV_HDCD:     msg = "hdcd";     break;
            default:
                snprintf(strbuf, sizeof(strbuf), "ERROR (%d)", opt->ad_converter_type);
                msg = strbuf;
            }
            ff_dlog(avctx, "ad_conv_type: %s\n", msg);
        } else {
            ff_dlog(avctx, "extended bitstream info 2: {not written}\n");
        }
    }
#endif
}

/**
 * Finalize encoding and free any memory allocated by the encoder.
 *
 * @param avctx  Codec context
 */
av_cold int ff_ac3_encode_close(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;

    for (int ch = 0; ch < s->channels; ch++)
        av_freep(&s->planar_samples[ch]);
    av_freep(&s->bap_buffer);
    av_freep(&s->bap1_buffer);
    av_freep(&s->mdct_coef_buffer);
    av_freep(&s->fixed_coef_buffer);
    av_freep(&s->exp_buffer);
    av_freep(&s->grouped_exp_buffer);
    av_freep(&s->psd_buffer);
    av_freep(&s->band_psd_buffer);
    av_freep(&s->mask_buffer);
    av_freep(&s->qmant_buffer);
    av_freep(&s->cpl_coord_buffer);
    av_freep(&s->fdsp);

    av_tx_uninit(&s->tx);

    return 0;
}


/*
 * Set channel information during initialization.
 */
static av_cold void set_channel_info(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    uint64_t mask = av_channel_layout_subset(&avctx->ch_layout, ~(uint64_t)0);
    int channels = avctx->ch_layout.nb_channels;

    s->lfe_on       = !!(mask & AV_CH_LOW_FREQUENCY);
    s->channels     = channels;
    s->fbw_channels = channels - s->lfe_on;
    s->lfe_channel  = s->lfe_on ? s->fbw_channels + 1 : -1;

    switch (mask & ~AV_CH_LOW_FREQUENCY) {
    case AV_CH_LAYOUT_MONO:           s->channel_mode = AC3_CHMODE_MONO;   break;
    case AV_CH_LAYOUT_STEREO:         s->channel_mode = AC3_CHMODE_STEREO; break;
    case AV_CH_LAYOUT_SURROUND:       s->channel_mode = AC3_CHMODE_3F;     break;
    case AV_CH_LAYOUT_2_1:            s->channel_mode = AC3_CHMODE_2F1R;   break;
    case AV_CH_LAYOUT_4POINT0:        s->channel_mode = AC3_CHMODE_3F1R;   break;
    case AV_CH_LAYOUT_QUAD:
    case AV_CH_LAYOUT_2_2:            s->channel_mode = AC3_CHMODE_2F2R;   break;
    case AV_CH_LAYOUT_5POINT0:
    case AV_CH_LAYOUT_5POINT0_BACK:   s->channel_mode = AC3_CHMODE_3F2R;   break;
    }
    s->has_center   = (s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO;
    s->has_surround =  s->channel_mode & 0x04;

    s->channel_map  = ac3_enc_channel_map[s->channel_mode][s->lfe_on];
}


static av_cold int validate_options(AC3EncodeContext *s)
{
    AVCodecContext *avctx = s->avctx;
    int ret;

    set_channel_info(avctx);

    for (int i = 0;; i++) {
        if (ff_ac3_sample_rate_tab[i] == avctx->sample_rate) {
            s->bit_alloc.sr_code = i;
            break;
        }
        av_assert1(ff_ac3_sample_rate_tab[i] != 0);
    }
    s->sample_rate        = avctx->sample_rate;
    s->bitstream_id       = s->eac3 ? 16 : 8;

    /* select a default bit rate if not set by the user */
    if (!avctx->bit_rate) {
        switch (s->fbw_channels) {
        case 1: avctx->bit_rate =  96000; break;
        case 2: avctx->bit_rate = 192000; break;
        case 3: avctx->bit_rate = 320000; break;
        case 4: avctx->bit_rate = 384000; break;
        case 5: avctx->bit_rate = 448000; break;
        }
    }

    /* validate bit rate */
    if (s->eac3) {
        int max_br, min_br, wpf, min_br_code;
        int num_blks_code, num_blocks, frame_samples;
        long long min_br_dist;

        /* calculate min/max bitrate */
        /* TODO: More testing with 3 and 2 blocks. All E-AC-3 samples I've
                 found use either 6 blocks or 1 block, even though 2 or 3 blocks
                 would work as far as the bit rate is concerned. */
        for (num_blks_code = 3; num_blks_code >= 0; num_blks_code--) {
            num_blocks = ((int[]){ 1, 2, 3, 6 })[num_blks_code];
            frame_samples  = AC3_BLOCK_SIZE * num_blocks;
            max_br = 2048 * s->sample_rate / frame_samples * 16;
            min_br = ((s->sample_rate + (frame_samples-1)) / frame_samples) * 16;
            if (avctx->bit_rate <= max_br)
                break;
        }
        if (avctx->bit_rate < min_br || avctx->bit_rate > max_br) {
            av_log(avctx, AV_LOG_ERROR, "invalid bit rate. must be %d to %d "
                   "for this sample rate\n", min_br, max_br);
            return AVERROR(EINVAL);
        }
        s->num_blks_code = num_blks_code;
        s->num_blocks    = num_blocks;

        /* calculate words-per-frame for the selected bitrate */
        wpf = (avctx->bit_rate / 16) * frame_samples / s->sample_rate;
        av_assert1(wpf > 0 && wpf <= 2048);

        /* find the closest AC-3 bitrate code to the selected bitrate.
           this is needed for lookup tables for bandwidth and coupling
           parameter selection */
        min_br_code = -1;
        min_br_dist = INT64_MAX;
        for (int i = 0; i < 19; i++) {
            long long br_dist = llabs(ff_ac3_bitrate_tab[i] * 1000 - avctx->bit_rate);
            if (br_dist < min_br_dist) {
                min_br_dist = br_dist;
                min_br_code = i;
            }
        }

        /* make sure the minimum frame size is below the average frame size */
        s->frame_size_code = min_br_code << 1;
        while (wpf > 1 && wpf * s->sample_rate / AC3_FRAME_SIZE * 16 > avctx->bit_rate)
            wpf--;
        s->frame_size_min = 2 * wpf;
    } else {
        int best_br = 0, best_code = 0;
        long long best_diff = INT64_MAX;
        for (int i = 0; i < 19; i++) {
            int br   = ff_ac3_bitrate_tab[i] * 1000;
            long long diff = llabs(br - avctx->bit_rate);
            if (diff < best_diff) {
                best_br   = br;
                best_code = i;
                best_diff = diff;
            }
            if (!best_diff)
                break;
        }
        avctx->bit_rate    = best_br;
        s->frame_size_code = best_code << 1;
        s->frame_size_min  = 2 * ff_ac3_frame_size_tab[s->frame_size_code][s->bit_alloc.sr_code];
        s->num_blks_code   = 0x3;
        s->num_blocks      = 6;
    }
    s->bit_rate   = avctx->bit_rate;
    s->frame_size = s->frame_size_min;

    /* validate cutoff */
    if (avctx->cutoff < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid cutoff frequency\n");
        return AVERROR(EINVAL);
    }
    s->cutoff = avctx->cutoff;
    if (s->cutoff > (s->sample_rate >> 1))
        s->cutoff = s->sample_rate >> 1;

    ret = ac3_validate_metadata(s);
    if (ret)
        return ret;

    s->rematrixing_enabled = s->options.stereo_rematrixing &&
                             (s->channel_mode == AC3_CHMODE_STEREO);

    s->cpl_enabled = s->options.channel_coupling &&
                     s->channel_mode >= AC3_CHMODE_STEREO;

    return 0;
}


/*
 * Set bandwidth for all channels.
 * The user can optionally supply a cutoff frequency. Otherwise an appropriate
 * default value will be used.
 */
static av_cold void set_bandwidth(AC3EncodeContext *s)
{
    int blk, ch, av_uninit(cpl_start);

    if (s->cutoff) {
        /* calculate bandwidth based on user-specified cutoff frequency */
        int fbw_coeffs;
        fbw_coeffs     = s->cutoff * 2 * AC3_MAX_COEFS / s->sample_rate;
        s->bandwidth_code = av_clip((fbw_coeffs - 73) / 3, 0, 60);
    } else {
        /* use default bandwidth setting */
        s->bandwidth_code = ac3_bandwidth_tab[s->fbw_channels-1][s->bit_alloc.sr_code][s->frame_size_code/2];
    }

    /* set number of coefficients for each channel */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
        s->start_freq[ch] = 0;
        for (blk = 0; blk < s->num_blocks; blk++)
            s->blocks[blk].end_freq[ch] = s->bandwidth_code * 3 + 73;
    }
    /* LFE channel always has 7 coefs */
    if (s->lfe_on) {
        s->start_freq[s->lfe_channel] = 0;
        for (blk = 0; blk < s->num_blocks; blk++)
            s->blocks[blk].end_freq[ch] = 7;
    }

    /* initialize coupling strategy */
    if (s->cpl_enabled) {
        if (s->options.cpl_start != AC3ENC_OPT_AUTO) {
            cpl_start = s->options.cpl_start;
        } else {
            cpl_start = ac3_coupling_start_tab[s->channel_mode-2][s->bit_alloc.sr_code][s->frame_size_code/2];
            if (cpl_start < 0) {
                if (s->options.channel_coupling == AC3ENC_OPT_AUTO)
                    s->cpl_enabled = 0;
                else
                    cpl_start = 15;
            }
        }
    }
    if (s->cpl_enabled) {
        int i, cpl_start_band, cpl_end_band;
        uint8_t *cpl_band_sizes = s->cpl_band_sizes;

        cpl_end_band   = s->bandwidth_code / 4 + 3;
        cpl_start_band = av_clip(cpl_start, 0, FFMIN(cpl_end_band-1, 15));

        s->num_cpl_subbands = cpl_end_band - cpl_start_band;

        s->num_cpl_bands = 1;
        *cpl_band_sizes  = 12;
        for (i = cpl_start_band + 1; i < cpl_end_band; i++) {
            if (ff_eac3_default_cpl_band_struct[i]) {
                *cpl_band_sizes += 12;
            } else {
                s->num_cpl_bands++;
                cpl_band_sizes++;
                *cpl_band_sizes = 12;
            }
        }

        s->start_freq[CPL_CH] = cpl_start_band * 12 + 37;
        s->cpl_end_freq       = cpl_end_band   * 12 + 37;
        for (blk = 0; blk < s->num_blocks; blk++)
            s->blocks[blk].end_freq[CPL_CH] = s->cpl_end_freq;
    }
}


static av_cold int allocate_buffers(AC3EncodeContext *s)
{
    int blk, ch;
    int channels = s->channels + 1; /* includes coupling channel */
    int channel_blocks = channels * s->num_blocks;
    int total_coefs    = AC3_MAX_COEFS * channel_blocks;
    uint8_t *cpl_coord_mant_buffer;
    const unsigned sampletype_size = SAMPLETYPE_SIZE(s);

    for (int ch = 0; ch < s->channels; ch++) {
        s->planar_samples[ch] = av_mallocz(AC3_BLOCK_SIZE * sampletype_size);
        if (!s->planar_samples[ch])
            return AVERROR(ENOMEM);
    }

    if (!FF_ALLOC_TYPED_ARRAY(s->bap_buffer,         total_coefs)          ||
        !FF_ALLOC_TYPED_ARRAY(s->bap1_buffer,        total_coefs)          ||
        !FF_ALLOCZ_TYPED_ARRAY(s->mdct_coef_buffer,  total_coefs)          ||
        !FF_ALLOC_TYPED_ARRAY(s->exp_buffer,         total_coefs)          ||
        !FF_ALLOC_TYPED_ARRAY(s->grouped_exp_buffer, channel_blocks * 128) ||
        !FF_ALLOC_TYPED_ARRAY(s->psd_buffer,         total_coefs)          ||
        !FF_ALLOC_TYPED_ARRAY(s->band_psd_buffer,    channel_blocks * 64)  ||
        !FF_ALLOC_TYPED_ARRAY(s->mask_buffer,        channel_blocks * 64)  ||
        !FF_ALLOC_TYPED_ARRAY(s->qmant_buffer,       total_coefs))
        return AVERROR(ENOMEM);

    if (!s->fixed_point) {
        if (!FF_ALLOCZ_TYPED_ARRAY(s->fixed_coef_buffer, total_coefs))
            return AVERROR(ENOMEM);
    }
    if (s->cpl_enabled) {
        if (!FF_ALLOC_TYPED_ARRAY(s->cpl_coord_buffer, channel_blocks * 32))
            return AVERROR(ENOMEM);
        cpl_coord_mant_buffer = s->cpl_coord_buffer + 16 * channel_blocks;
    }
    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];

        for (ch = 0; ch < channels; ch++) {
            /* arrangement: block, channel, coeff */
            block->grouped_exp[ch] = &s->grouped_exp_buffer[128           * (blk * channels + ch)];
            block->psd[ch]         = &s->psd_buffer        [AC3_MAX_COEFS * (blk * channels + ch)];
            block->band_psd[ch]    = &s->band_psd_buffer   [64            * (blk * channels + ch)];
            block->mask[ch]        = &s->mask_buffer       [64            * (blk * channels + ch)];
            block->qmant[ch]       = &s->qmant_buffer      [AC3_MAX_COEFS * (blk * channels + ch)];
            if (s->cpl_enabled) {
                block->cpl_coord_exp[ch]  = &s->cpl_coord_buffer [16  * (blk * channels + ch)];
                block->cpl_coord_mant[ch] = &cpl_coord_mant_buffer[16  * (blk * channels + ch)];
            }

            /* arrangement: channel, block, coeff */
            block->exp[ch]         = &s->exp_buffer        [AC3_MAX_COEFS * (s->num_blocks * ch + blk)];
            block->mdct_coef[ch]   = &s->mdct_coef_buffer  [AC3_MAX_COEFS * (s->num_blocks * ch + blk)];
            if (s->fixed_point)
                block->fixed_coef[ch] = (int32_t *)block->mdct_coef[ch];
            else
                block->fixed_coef[ch] = &s->fixed_coef_buffer[AC3_MAX_COEFS * (s->num_blocks * ch + blk)];
        }
    }

    return 0;
}


av_cold int ff_ac3_encode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    AC3EncodeContext *s = avctx->priv_data;
    int ret, frame_size_58;

    s->avctx = avctx;

    ret = validate_options(s);
    if (ret)
        return ret;

    avctx->frame_size = AC3_BLOCK_SIZE * s->num_blocks;
    avctx->initial_padding = AC3_BLOCK_SIZE;

    s->bitstream_mode = avctx->audio_service_type;
    if (s->bitstream_mode == AV_AUDIO_SERVICE_TYPE_KARAOKE)
        s->bitstream_mode = 0x7;

    s->bits_written    = 0;
    s->samples_written = 0;

    /* calculate crc_inv for both possible frame sizes */
    frame_size_58 = (( s->frame_size    >> 2) + ( s->frame_size    >> 4)) << 1;
    s->crc_inv[0] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    if (s->bit_alloc.sr_code == 1) {
        frame_size_58 = (((s->frame_size+2) >> 2) + ((s->frame_size+2) >> 4)) << 1;
        s->crc_inv[1] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    }

    if (!s->output_frame_header)
        s->output_frame_header = ac3_output_frame_header;

    set_bandwidth(s);

    bit_alloc_init(s);

    ret = allocate_buffers(s);
    if (ret)
        return ret;

    ff_audiodsp_init(&s->adsp);
    ff_me_cmp_init(&s->mecc, avctx);
    ff_ac3dsp_init(&s->ac3dsp);

    dprint_options(s);

    ff_thread_once(&init_static_once, exponent_init);

    return 0;
}
