/*
 * Copyright (c) 2011 Justin Ruggles
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
 * mov 'chan' tag reading/writing.
 * @author Justin Ruggles
 */

#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavcodec/codec_id.h"
#include "mov_chan.h"

struct MovChannelLayoutMap {
    uint32_t tag;
    uint64_t layout;
};

static const struct MovChannelLayoutMap mov_ch_layout_map_misc[] = {
    { MOV_CH_LAYOUT_USE_DESCRIPTIONS,   0 },
    { MOV_CH_LAYOUT_USE_BITMAP,         0 },
    { MOV_CH_LAYOUT_DISCRETEINORDER,    0 },
    { MOV_CH_LAYOUT_UNKNOWN,            0 },
    { MOV_CH_LAYOUT_TMH_10_2_STD,       0 }, // L,   R,  C,    Vhc, Lsd, Rsd,
                                             // Ls,  Rs, Vhl,  Vhr, Lw,  Rw,
                                             // Csd, Cs, LFE1, LFE2
    { MOV_CH_LAYOUT_TMH_10_2_FULL,      0 }, // L,   R,  C,    Vhc,  Lsd, Rsd,
                                             // Ls,  Rs, Vhl,  Vhr,  Lw,  Rw,
                                             // Csd, Cs, LFE1, LFE2, Lc,  Rc,
                                             // HI,  VI, Haptic
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_1ch[] = {
    { MOV_CH_LAYOUT_MONO,               AV_CH_LAYOUT_MONO }, // C
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_2ch[] = {
    { MOV_CH_LAYOUT_STEREO,             AV_CH_LAYOUT_STEREO         }, // L, R
    { MOV_CH_LAYOUT_STEREOHEADPHONES,   AV_CH_LAYOUT_STEREO         }, // L, R
    { MOV_CH_LAYOUT_BINAURAL,           AV_CH_LAYOUT_STEREO         }, // L, R
    { MOV_CH_LAYOUT_MIDSIDE,            AV_CH_LAYOUT_STEREO         }, // C, sides
    { MOV_CH_LAYOUT_XY,                 AV_CH_LAYOUT_STEREO         }, // X (left), Y (right)

    { MOV_CH_LAYOUT_MATRIXSTEREO,       AV_CH_LAYOUT_STEREO_DOWNMIX }, // Lt, Rt

    { MOV_CH_LAYOUT_AC3_1_0_1,          AV_CH_LAYOUT_MONO |            // C, LFE
                                        AV_CH_LOW_FREQUENCY         },
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_3ch[] = {
    { MOV_CH_LAYOUT_MPEG_3_0_A,         AV_CH_LAYOUT_SURROUND }, // L, R, C
    { MOV_CH_LAYOUT_MPEG_3_0_B,         AV_CH_LAYOUT_SURROUND }, // C, L, R
    { MOV_CH_LAYOUT_AC3_3_0,            AV_CH_LAYOUT_SURROUND }, // L, C, R

    { MOV_CH_LAYOUT_ITU_2_1,            AV_CH_LAYOUT_2_1      }, // L, R, Cs

    { MOV_CH_LAYOUT_DVD_4,              AV_CH_LAYOUT_2POINT1  }, // L, R, LFE
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_4ch[] = {
    { MOV_CH_LAYOUT_AMBISONIC_B_FORMAT, 0 },                    // W, X, Y, Z

    { MOV_CH_LAYOUT_QUADRAPHONIC,       AV_CH_LAYOUT_QUAD    }, // L, R, Rls, Rrs

    { MOV_CH_LAYOUT_MPEG_4_0_A,         AV_CH_LAYOUT_4POINT0 }, // L, R, C, Cs
    { MOV_CH_LAYOUT_MPEG_4_0_B,         AV_CH_LAYOUT_4POINT0 }, // C, L, R, Cs
    { MOV_CH_LAYOUT_AC3_3_1,            AV_CH_LAYOUT_4POINT0 }, // L, C, R, Cs

    { MOV_CH_LAYOUT_ITU_2_2,            AV_CH_LAYOUT_2_2     }, // L, R, Ls, Rs

    { MOV_CH_LAYOUT_DVD_5,              AV_CH_LAYOUT_2_1 |      // L, R, LFE, Cs
                                        AV_CH_LOW_FREQUENCY  },
    { MOV_CH_LAYOUT_AC3_2_1_1,          AV_CH_LAYOUT_2_1 |      // L, R, Cs, LFE
                                        AV_CH_LOW_FREQUENCY  },

    { MOV_CH_LAYOUT_DVD_10,             AV_CH_LAYOUT_3POINT1 }, // L, R, C, LFE
    { MOV_CH_LAYOUT_AC3_3_0_1,          AV_CH_LAYOUT_3POINT1 }, // L, C, R, LFE
    { MOV_CH_LAYOUT_DTS_3_1,            AV_CH_LAYOUT_3POINT1 }, // C, L, R, LFE
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_5ch[] = {
    { MOV_CH_LAYOUT_PENTAGONAL,         AV_CH_LAYOUT_5POINT0_BACK }, // L, R, Rls, Rrs, C

    { MOV_CH_LAYOUT_MPEG_5_0_A,         AV_CH_LAYOUT_5POINT0 },      // L, R, C,  Ls, Rs
    { MOV_CH_LAYOUT_MPEG_5_0_B,         AV_CH_LAYOUT_5POINT0 },      // L, R, Ls, Rs, C
    { MOV_CH_LAYOUT_MPEG_5_0_C,         AV_CH_LAYOUT_5POINT0 },      // L, C, R,  Ls, Rs
    { MOV_CH_LAYOUT_MPEG_5_0_D,         AV_CH_LAYOUT_5POINT0 },      // C, L, R,  Ls, Rs

    { MOV_CH_LAYOUT_DVD_6,              AV_CH_LAYOUT_2_2 |           // L, R, LFE, Ls, Rs
                                        AV_CH_LOW_FREQUENCY },
    { MOV_CH_LAYOUT_DVD_18,             AV_CH_LAYOUT_2_2 |           // L, R, Ls, Rs, LFE
                                        AV_CH_LOW_FREQUENCY },

    { MOV_CH_LAYOUT_DVD_11,             AV_CH_LAYOUT_4POINT1 },      // L, R, C, LFE, Cs
    { MOV_CH_LAYOUT_AC3_3_1_1,          AV_CH_LAYOUT_4POINT1 },      // L, C, R, Cs,  LFE
    { MOV_CH_LAYOUT_DTS_4_1,            AV_CH_LAYOUT_4POINT1 },      // C, L, R, Cs,  LFE
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_6ch[] = {
    { MOV_CH_LAYOUT_HEXAGONAL,          AV_CH_LAYOUT_HEXAGONAL },      // L, R,  Rls, Rrs, C,   Cs
    { MOV_CH_LAYOUT_DTS_6_0_C,          AV_CH_LAYOUT_HEXAGONAL },      // C, Cs, L,   R,   Rls, Rrs

    { MOV_CH_LAYOUT_MPEG_5_1_A,         AV_CH_LAYOUT_5POINT1 },        // L, R, C,  LFE, Ls, Rs
    { MOV_CH_LAYOUT_MPEG_5_1_B,         AV_CH_LAYOUT_5POINT1 },        // L, R, Ls, Rs,  C,  LFE
    { MOV_CH_LAYOUT_MPEG_5_1_C,         AV_CH_LAYOUT_5POINT1 },        // L, C, R,  Ls,  Rs, LFE
    { MOV_CH_LAYOUT_MPEG_5_1_D,         AV_CH_LAYOUT_5POINT1 },        // C, L, R,  Ls,  Rs, LFE

    { MOV_CH_LAYOUT_AUDIOUNIT_6_0,      AV_CH_LAYOUT_6POINT0 },        // L, R, Ls, Rs, C,  Cs
    { MOV_CH_LAYOUT_AAC_6_0,            AV_CH_LAYOUT_6POINT0 },        // C, L, R,  Ls, Rs, Cs
    { MOV_CH_LAYOUT_EAC3_6_0_A,         AV_CH_LAYOUT_6POINT0 },        // L, C, R,  Ls, Rs, Cs

    { MOV_CH_LAYOUT_DTS_6_0_A,          AV_CH_LAYOUT_6POINT0_FRONT },  // Lc, Rc, L, R, Ls, Rs

    { MOV_CH_LAYOUT_DTS_6_0_B,          AV_CH_LAYOUT_5POINT0_BACK |    // C, L, R, Rls, Rrs, Ts
                                        AV_CH_TOP_CENTER },
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_7ch[] = {
    { MOV_CH_LAYOUT_MPEG_6_1_A,          AV_CH_LAYOUT_6POINT1 },        // L, R, C, LFE, Ls, Rs,  Cs
    { MOV_CH_LAYOUT_AAC_6_1,             AV_CH_LAYOUT_6POINT1 },        // C, L, R, Ls,  Rs, Cs,  LFE
    { MOV_CH_LAYOUT_EAC3_6_1_A,          AV_CH_LAYOUT_6POINT1 },        // L, C, R, Ls,  Rs, LFE, Cs
    { MOV_CH_LAYOUT_DTS_6_1_D,           AV_CH_LAYOUT_6POINT1 },        // C, L, R, Ls,  Rs, LFE, Cs

    { MOV_CH_LAYOUT_AUDIOUNIT_7_0,       AV_CH_LAYOUT_7POINT0 },        // L, R, Ls, Rs, C,  Rls, Rrs
    { MOV_CH_LAYOUT_AAC_7_0,             AV_CH_LAYOUT_7POINT0 },        // C, L, R,  Ls, Rs, Rls, Rrs
    { MOV_CH_LAYOUT_EAC3_7_0_A,          AV_CH_LAYOUT_7POINT0 },        // L, C, R,  Ls, Rs, Rls, Rrs

    { MOV_CH_LAYOUT_AUDIOUNIT_7_0_FRONT, AV_CH_LAYOUT_7POINT0_FRONT },  // L,  R, Ls, Rs, C, Lc, Rc
    { MOV_CH_LAYOUT_DTS_7_0,             AV_CH_LAYOUT_7POINT0_FRONT },  // Lc, C, Rc, L,  R, Ls, Rs

    { MOV_CH_LAYOUT_EAC3_6_1_B,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Ts
                                         AV_CH_TOP_CENTER },

    { MOV_CH_LAYOUT_EAC3_6_1_C,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Vhc
                                         AV_CH_TOP_FRONT_CENTER },

    { MOV_CH_LAYOUT_DTS_6_1_A,           AV_CH_LAYOUT_6POINT1_FRONT },  // Lc, Rc, L, R, Ls, Rs, LFE

    { MOV_CH_LAYOUT_DTS_6_1_B,           AV_CH_LAYOUT_5POINT1_BACK |    // C, L, R, Rls, Rrs, Ts, LFE
                                         AV_CH_TOP_CENTER },

    { MOV_CH_LAYOUT_DTS_6_1_C,           AV_CH_LAYOUT_6POINT1_BACK },   // C, Cs, L, R, Rls, Rrs, LFE
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_8ch[] = {
    { MOV_CH_LAYOUT_OCTAGONAL,           AV_CH_LAYOUT_OCTAGONAL },      // L, R, Rls, Rrs, C,  Cs,  Ls,  Rs
    { MOV_CH_LAYOUT_AAC_OCTAGONAL,       AV_CH_LAYOUT_OCTAGONAL },      // C, L, R,   Ls,  Rs, Rls, Rrs, Cs

    { MOV_CH_LAYOUT_CUBE,                AV_CH_LAYOUT_CUBE },           // L, R, Rls, Rrs, Vhl, Vhr, Rlt, Rrt

    { MOV_CH_LAYOUT_MPEG_7_1_A,          AV_CH_LAYOUT_7POINT1_WIDE },   // L,  R,  C,  LFE, Ls, Rs,  Lc, Rc
    { MOV_CH_LAYOUT_MPEG_7_1_B,          AV_CH_LAYOUT_7POINT1_WIDE },   // C,  Lc, Rc, L,   R,  Ls,  Rs, LFE
    { MOV_CH_LAYOUT_EMAGIC_DEFAULT_7_1,  AV_CH_LAYOUT_7POINT1_WIDE },   // L,  R,  Ls, Rs,  C,  LFE, Lc, Rc
    { MOV_CH_LAYOUT_EAC3_7_1_B,          AV_CH_LAYOUT_7POINT1_WIDE },   // L,  C,  R,  Ls,  Rs, LFE, Lc, Rc
    { MOV_CH_LAYOUT_DTS_7_1,             AV_CH_LAYOUT_7POINT1_WIDE },   // Lc, C,  Rc, L,   R,  Ls,  Rs, LFE

    { MOV_CH_LAYOUT_MPEG_7_1_C,          AV_CH_LAYOUT_7POINT1 },        // L, R, C, LFE, Ls, Rs,  Rls, Rrs
    { MOV_CH_LAYOUT_EAC3_7_1_A,          AV_CH_LAYOUT_7POINT1 },        // L, C, R, Ls,  Rs, LFE, Rls, Rrs

    { MOV_CH_LAYOUT_SMPTE_DTV,           AV_CH_LAYOUT_5POINT1 |         // L, R, C, LFE, Ls, Rs, Lt, Rt
                                         AV_CH_LAYOUT_STEREO_DOWNMIX },

    { MOV_CH_LAYOUT_EAC3_7_1_C,          AV_CH_LAYOUT_5POINT1        |  // L, C, R, Ls, Rs, LFE, Lsd, Rsd
                                         AV_CH_SURROUND_DIRECT_LEFT  |
                                         AV_CH_SURROUND_DIRECT_RIGHT },

    { MOV_CH_LAYOUT_EAC3_7_1_D,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Lw, Rw
                                         AV_CH_WIDE_LEFT      |
                                         AV_CH_WIDE_RIGHT },

    { MOV_CH_LAYOUT_EAC3_7_1_E,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Vhl, Vhr
                                         AV_CH_TOP_FRONT_LEFT |
                                         AV_CH_TOP_FRONT_RIGHT },

    { MOV_CH_LAYOUT_EAC3_7_1_F,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Cs, Ts
                                         AV_CH_BACK_CENTER    |
                                         AV_CH_TOP_CENTER },

    { MOV_CH_LAYOUT_EAC3_7_1_G,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Cs, Vhc
                                         AV_CH_BACK_CENTER    |
                                         AV_CH_TOP_FRONT_CENTER },

    { MOV_CH_LAYOUT_EAC3_7_1_H,          AV_CH_LAYOUT_5POINT1 |         // L, C, R, Ls, Rs, LFE, Ts, Vhc
                                         AV_CH_TOP_CENTER     |
                                         AV_CH_TOP_FRONT_CENTER },

    { MOV_CH_LAYOUT_DTS_8_0_A,           AV_CH_LAYOUT_2_2           |   // Lc, Rc, L, R, Ls, Rs, Rls, Rrs
                                         AV_CH_BACK_LEFT            |
                                         AV_CH_BACK_RIGHT           |
                                         AV_CH_FRONT_LEFT_OF_CENTER |
                                         AV_CH_FRONT_RIGHT_OF_CENTER },

    { MOV_CH_LAYOUT_DTS_8_0_B,           AV_CH_LAYOUT_5POINT0        |  // Lc, C, Rc, L, R, Ls, Cs, Rs
                                         AV_CH_FRONT_LEFT_OF_CENTER  |
                                         AV_CH_FRONT_RIGHT_OF_CENTER |
                                         AV_CH_BACK_CENTER },
    { 0, 0 },
};

static const struct MovChannelLayoutMap mov_ch_layout_map_9ch[] = {
    { MOV_CH_LAYOUT_DTS_8_1_A,           AV_CH_LAYOUT_2_2            | // Lc, Rc, L, R, Ls, Rs, Rls, Rrs, LFE
                                         AV_CH_BACK_LEFT             |
                                         AV_CH_BACK_RIGHT            |
                                         AV_CH_FRONT_LEFT_OF_CENTER  |
                                         AV_CH_FRONT_RIGHT_OF_CENTER |
                                         AV_CH_LOW_FREQUENCY },

    { MOV_CH_LAYOUT_DTS_8_1_B,           AV_CH_LAYOUT_7POINT1_WIDE   | // Lc, C, Rc, L, R, Ls, Cs, Rs, LFE
                                         AV_CH_BACK_CENTER },
    { 0, 0 },
};

static const struct MovChannelLayoutMap * const mov_ch_layout_map[] = {
    mov_ch_layout_map_misc,
    mov_ch_layout_map_1ch,
    mov_ch_layout_map_2ch,
    mov_ch_layout_map_3ch,
    mov_ch_layout_map_4ch,
    mov_ch_layout_map_5ch,
    mov_ch_layout_map_6ch,
    mov_ch_layout_map_7ch,
    mov_ch_layout_map_8ch,
    mov_ch_layout_map_9ch,
};

static const enum MovChannelLayoutTag mov_ch_layouts_aac[] = {
    MOV_CH_LAYOUT_MONO,
    MOV_CH_LAYOUT_STEREO,
    MOV_CH_LAYOUT_AC3_1_0_1,
    MOV_CH_LAYOUT_MPEG_3_0_B,
    MOV_CH_LAYOUT_ITU_2_1,
    MOV_CH_LAYOUT_DVD_4,
    MOV_CH_LAYOUT_QUADRAPHONIC,
    MOV_CH_LAYOUT_MPEG_4_0_B,
    MOV_CH_LAYOUT_ITU_2_2,
    MOV_CH_LAYOUT_AC3_2_1_1,
    MOV_CH_LAYOUT_DTS_3_1,
    MOV_CH_LAYOUT_MPEG_5_0_D,
    MOV_CH_LAYOUT_DVD_18,
    MOV_CH_LAYOUT_DTS_4_1,
    MOV_CH_LAYOUT_MPEG_5_1_D,
    MOV_CH_LAYOUT_AAC_6_0,
    MOV_CH_LAYOUT_DTS_6_0_A,
    MOV_CH_LAYOUT_AAC_6_1,
    MOV_CH_LAYOUT_AAC_7_0,
    MOV_CH_LAYOUT_DTS_6_1_A,
    MOV_CH_LAYOUT_AAC_OCTAGONAL,
    MOV_CH_LAYOUT_MPEG_7_1_B,
    MOV_CH_LAYOUT_DTS_8_0_A,
    0,
};

static const enum MovChannelLayoutTag mov_ch_layouts_ac3[] = {
    MOV_CH_LAYOUT_MONO,
    MOV_CH_LAYOUT_STEREO,
    MOV_CH_LAYOUT_AC3_1_0_1,
    MOV_CH_LAYOUT_AC3_3_0,
    MOV_CH_LAYOUT_ITU_2_1,
    MOV_CH_LAYOUT_DVD_4,
    MOV_CH_LAYOUT_AC3_3_1,
    MOV_CH_LAYOUT_ITU_2_2,
    MOV_CH_LAYOUT_AC3_2_1_1,
    MOV_CH_LAYOUT_AC3_3_0_1,
    MOV_CH_LAYOUT_MPEG_5_0_C,
    MOV_CH_LAYOUT_DVD_18,
    MOV_CH_LAYOUT_AC3_3_1_1,
    MOV_CH_LAYOUT_MPEG_5_1_C,
    0,
};

static const enum MovChannelLayoutTag mov_ch_layouts_alac[] = {
    MOV_CH_LAYOUT_MONO,
    MOV_CH_LAYOUT_STEREO,
    MOV_CH_LAYOUT_MPEG_3_0_B,
    MOV_CH_LAYOUT_MPEG_4_0_B,
    MOV_CH_LAYOUT_MPEG_5_0_D,
    MOV_CH_LAYOUT_MPEG_5_1_D,
    MOV_CH_LAYOUT_AAC_6_1,
    MOV_CH_LAYOUT_MPEG_7_1_B,
    0,
};

static const enum MovChannelLayoutTag mov_ch_layouts_wav[] = {
    MOV_CH_LAYOUT_MONO,
    MOV_CH_LAYOUT_STEREO,
    MOV_CH_LAYOUT_MATRIXSTEREO,
    MOV_CH_LAYOUT_MPEG_3_0_A,
    MOV_CH_LAYOUT_QUADRAPHONIC,
    MOV_CH_LAYOUT_MPEG_5_0_A,
    MOV_CH_LAYOUT_MPEG_5_1_A,
    MOV_CH_LAYOUT_MPEG_6_1_A,
    MOV_CH_LAYOUT_MPEG_7_1_A,
    MOV_CH_LAYOUT_MPEG_7_1_C,
    MOV_CH_LAYOUT_SMPTE_DTV,
    0,
};

static const struct {
    enum AVCodecID codec_id;
    const enum MovChannelLayoutTag *layouts;
} mov_codec_ch_layouts[] = {
    { AV_CODEC_ID_AAC,     mov_ch_layouts_aac      },
    { AV_CODEC_ID_AC3,     mov_ch_layouts_ac3      },
    { AV_CODEC_ID_ALAC,    mov_ch_layouts_alac     },
    { AV_CODEC_ID_PCM_U8,    mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S8,    mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S16LE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S16BE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S24LE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S24BE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S32LE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_S32BE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_F32LE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_F32BE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_F64LE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_PCM_F64BE, mov_ch_layouts_wav    },
    { AV_CODEC_ID_NONE,    NULL                    },
};

/**
 * Get the channel layout for the specified channel layout tag.
 *
 * @param[in]  tag     channel layout tag
 * @param[out] bitmap  channel bitmap (only used if needed)
 * @return             channel layout
 */
static uint64_t mov_get_channel_layout(uint32_t tag, uint32_t bitmap)
{
    int i, channels;
    const struct MovChannelLayoutMap *layout_map;

    /* use ff_mov_get_channel_label() to build a layout instead */
    if (tag == MOV_CH_LAYOUT_USE_DESCRIPTIONS)
        return 0;

    /* handle the use of the channel bitmap */
    if (tag == MOV_CH_LAYOUT_USE_BITMAP)
        return bitmap < 0x40000 ? bitmap : 0;

    /* get the layout map based on the channel count for the specified layout tag */
    channels = tag & 0xFFFF;
    if (channels > 9)
        channels = 0;
    layout_map = mov_ch_layout_map[channels];

    /* find the channel layout for the specified layout tag */
    for (i = 0; layout_map[i].tag != 0; i++) {
        if (layout_map[i].tag == tag)
            break;
    }
    return layout_map[i].layout;
}

static uint64_t mov_get_channel_mask(uint32_t label)
{
    if (label == 0)
        return 0;
    if (label <= 18)
        return 1U << (label - 1);
    if (label == 35)
        return AV_CH_WIDE_LEFT;
    if (label == 36)
        return AV_CH_WIDE_RIGHT;
    if (label == 37)
        return AV_CH_LOW_FREQUENCY_2;
    if (label == 38)
        return AV_CH_STEREO_LEFT;
    if (label == 39)
        return AV_CH_STEREO_RIGHT;
    return 0;
}

static uint32_t mov_get_channel_label(enum AVChannel channel)
{
    if (channel < 0)
        return 0;
    if (channel <= AV_CHAN_TOP_BACK_RIGHT)
        return channel + 1;
    if (channel == AV_CHAN_WIDE_LEFT)
        return 35;
    if (channel == AV_CHAN_WIDE_RIGHT)
        return 36;
    if (channel == AV_CHAN_LOW_FREQUENCY_2)
        return 37;
    if (channel == AV_CHAN_STEREO_LEFT)
        return 38;
    if (channel == AV_CHAN_STEREO_RIGHT)
        return 39;
    return 0;
}

int ff_mov_get_channel_layout_tag(const AVCodecParameters *par,
                                  uint32_t *layout,
                                  uint32_t *bitmap,
                                  uint32_t **pchannel_desc)
{
    int i, j;
    uint32_t tag = 0;
    const enum MovChannelLayoutTag *layouts = NULL;

    /* find the layout list for the specified codec */
    for (i = 0; mov_codec_ch_layouts[i].codec_id != AV_CODEC_ID_NONE; i++) {
        if (mov_codec_ch_layouts[i].codec_id == par->codec_id)
            break;
    }
    if (mov_codec_ch_layouts[i].codec_id != AV_CODEC_ID_NONE)
        layouts = mov_codec_ch_layouts[i].layouts;

    if (layouts) {
        int channels;
        const struct MovChannelLayoutMap *layout_map;

        /* get the layout map based on the channel count */
        channels = par->ch_layout.nb_channels;
        if (channels > 9)
            channels = 0;
        layout_map = mov_ch_layout_map[channels];

        /* find the layout tag for the specified channel layout */
        for (i = 0; layouts[i] != 0; i++) {
            if ((layouts[i] & 0xFFFF) != channels)
                continue;
            for (j = 0; layout_map[j].tag != 0; j++) {
                if (layout_map[j].tag    == layouts[i] &&
                    (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                     layout_map[j].layout == par->ch_layout.u.mask))
                    break;
            }
            if (layout_map[j].tag)
                break;
        }
        tag = layouts[i];
    }

    *layout = tag;
    *bitmap = 0;
    *pchannel_desc = NULL;

    /* if no tag was found, use channel bitmap or description as a backup if possible */
    if (tag == 0) {
        uint32_t *channel_desc;
        if (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
            par->ch_layout.u.mask < 0x40000) {
            *layout = MOV_CH_LAYOUT_USE_BITMAP;
            *bitmap = (uint32_t)par->ch_layout.u.mask;
            return 0;
        } else if (par->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            return AVERROR(ENOSYS);

        channel_desc = av_malloc_array(par->ch_layout.nb_channels, sizeof(*channel_desc));
        if (!channel_desc)
            return AVERROR(ENOMEM);

        for (i = 0; i < par->ch_layout.nb_channels; i++) {
            channel_desc[i] =
                mov_get_channel_label(av_channel_layout_channel_from_index(&par->ch_layout, i));

            if (channel_desc[i] == 0) {
                av_free(channel_desc);
                return AVERROR(ENOSYS);
            }
        }

        *pchannel_desc = channel_desc;
    }

    return 0;
}

int ff_mov_read_chan(AVFormatContext *s, AVIOContext *pb, AVStream *st,
                     int64_t size)
{
    uint32_t layout_tag, bitmap, num_descr;
    uint64_t label_mask, mask = 0;
    int i;

    if (size < 12)
        return AVERROR_INVALIDDATA;

    layout_tag = avio_rb32(pb);
    bitmap     = avio_rb32(pb);
    num_descr  = avio_rb32(pb);

    av_log(s, AV_LOG_DEBUG, "chan: layout=%"PRIu32" "
           "bitmap=%"PRIu32" num_descr=%"PRIu32"\n",
           layout_tag, bitmap, num_descr);

    if (size < 12ULL + num_descr * 20ULL)
        return 0;

    label_mask = 0;
    for (i = 0; i < num_descr; i++) {
        uint32_t label;
        if (pb->eof_reached) {
            av_log(s, AV_LOG_ERROR,
                   "reached EOF while reading channel layout\n");
            return AVERROR_INVALIDDATA;
        }
        label     = avio_rb32(pb);          // mChannelLabel
        avio_rb32(pb);                      // mChannelFlags
        avio_rl32(pb);                      // mCoordinates[0]
        avio_rl32(pb);                      // mCoordinates[1]
        avio_rl32(pb);                      // mCoordinates[2]
        size -= 20;
        if (layout_tag == 0) {
            uint64_t mask_incr = mov_get_channel_mask(label);
            if (mask_incr == 0) {
                label_mask = 0;
                break;
            }
            label_mask |= mask_incr;
        }
    }
    if (layout_tag == 0) {
        if (label_mask)
            mask = label_mask;
    } else
        mask = mov_get_channel_layout(layout_tag, bitmap);

    if (mask) {
        av_channel_layout_uninit(&st->codecpar->ch_layout);
        av_channel_layout_from_mask(&st->codecpar->ch_layout, mask);
    }
    avio_skip(pb, size - 12);

    return 0;
}
