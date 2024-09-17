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

#include <assert.h>
#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavcodec/codec_id.h"
#include "mov_chan.h"

enum {
    c_L      = AV_CHAN_FRONT_LEFT,
    c_R      = AV_CHAN_FRONT_RIGHT,
    c_C      = AV_CHAN_FRONT_CENTER,
    c_LFE    = AV_CHAN_LOW_FREQUENCY,
    c_Rls    = AV_CHAN_BACK_LEFT,
    c_Rrs    = AV_CHAN_BACK_RIGHT,
    c_Lc     = AV_CHAN_FRONT_LEFT_OF_CENTER,
    c_Rc     = AV_CHAN_FRONT_RIGHT_OF_CENTER,
    c_Cs     = AV_CHAN_BACK_CENTER,
    c_Ls     = AV_CHAN_SIDE_LEFT,
    c_Rs     = AV_CHAN_SIDE_RIGHT,
    c_Ts     = AV_CHAN_TOP_CENTER,
    c_Vhl    = AV_CHAN_TOP_FRONT_LEFT,
    c_Vhc    = AV_CHAN_TOP_FRONT_CENTER,
    c_Vhr    = AV_CHAN_TOP_FRONT_RIGHT,
    c_Rlt    = AV_CHAN_TOP_BACK_LEFT,
    //       = AV_CHAN_TOP_BACK_CENTER,
    c_Rrt    = AV_CHAN_TOP_BACK_RIGHT,
    c_Lt     = AV_CHAN_STEREO_LEFT,
    c_Rt     = AV_CHAN_STEREO_RIGHT,
    c_Lw     = AV_CHAN_WIDE_LEFT,
    c_Rw     = AV_CHAN_WIDE_RIGHT,
    c_Lsd    = AV_CHAN_SURROUND_DIRECT_LEFT,
    c_Rsd    = AV_CHAN_SURROUND_DIRECT_RIGHT,
    c_LFE2   = AV_CHAN_LOW_FREQUENCY_2,
    //       = AV_CHAN_TOP_SIDE_LEFT,
    //       = AV_CHAN_TOP_SIDE_RIGHT,
    //       = AV_CHAN_BOTTOM_FRONT_CENTER,
    //       = AV_CHAN_BOTTOM_FRONT_LEFT,
    //       = AV_CHAN_BOTTOM_FRONT_RIGHT,
    c_W      = AV_CHAN_AMBISONIC_BASE,
    c_Y      = AV_CHAN_AMBISONIC_BASE + 1,
    c_Z      = AV_CHAN_AMBISONIC_BASE + 2,
    c_X      = AV_CHAN_AMBISONIC_BASE + 3,
    /* The following have no exact counterparts */
    c_LFE1   = AV_CHAN_LOW_FREQUENCY,
    c_Csd    = AV_CHAN_NONE,
    c_HI     = AV_CHAN_NONE,
    c_VI     = AV_CHAN_NONE,
    c_Haptic = AV_CHAN_NONE,
};

enum {
    iso_L    = AV_CHAN_FRONT_LEFT,
    iso_R    = AV_CHAN_FRONT_RIGHT,
    iso_C    = AV_CHAN_FRONT_CENTER,
    iso_LFE  = AV_CHAN_LOW_FREQUENCY,
    iso_Lsr  = AV_CHAN_BACK_LEFT,
    iso_Rsr  = AV_CHAN_BACK_RIGHT,
    iso_Lc   = AV_CHAN_FRONT_LEFT_OF_CENTER,
    iso_Rc   = AV_CHAN_FRONT_RIGHT_OF_CENTER,
    iso_Cs   = AV_CHAN_BACK_CENTER,
    iso_Ls   = AV_CHAN_SIDE_LEFT,
    iso_Rs   = AV_CHAN_SIDE_RIGHT,
    iso_Ts   = AV_CHAN_TOP_CENTER,
    iso_Lv   = AV_CHAN_TOP_FRONT_LEFT,
    iso_Cv   = AV_CHAN_TOP_FRONT_CENTER,
    iso_Rv   = AV_CHAN_TOP_FRONT_RIGHT,
    iso_Lvr  = AV_CHAN_TOP_BACK_LEFT,
    iso_Cvr  = AV_CHAN_TOP_BACK_CENTER,
    iso_Rvr  = AV_CHAN_TOP_BACK_RIGHT,
    //       = AV_CHAN_STEREO_LEFT,
    //       = AV_CHAN_STEREO_RIGHT,
    iso_Lw   = AV_CHAN_WIDE_LEFT,
    iso_Rw   = AV_CHAN_WIDE_RIGHT,
    iso_Lsd  = AV_CHAN_SURROUND_DIRECT_LEFT,
    iso_Rsd  = AV_CHAN_SURROUND_DIRECT_RIGHT,
    iso_LFE2 = AV_CHAN_LOW_FREQUENCY_2,
    iso_Lvss = AV_CHAN_TOP_SIDE_LEFT,
    iso_Rvss = AV_CHAN_TOP_SIDE_RIGHT,
    iso_Cb   = AV_CHAN_BOTTOM_FRONT_CENTER,
    iso_Lb   = AV_CHAN_BOTTOM_FRONT_LEFT,
    iso_Rb   = AV_CHAN_BOTTOM_FRONT_RIGHT,
    iso_Lss  = AV_CHAN_SIDE_SURROUND_LEFT,
    iso_Rss  = AV_CHAN_SIDE_SURROUND_RIGHT,
    /* The following have no exact counterparts */
    iso_Lvs  = AV_CHAN_NONE,
    iso_Rvs  = AV_CHAN_NONE,
};

struct MovChannelLayoutMap {
    union {
        uint32_t tag;
        enum AVChannel id;
    };
};

#define TAG(_tag, _cnt)                                    {.tag = _tag}
#define ID(_0)                                             {.id = c_##_0}
#define CHLIST01(_tag, _1)                                 CHLIST(_tag, 1, ID(_1))
#define CHLIST02(_tag, _1, _2)                             CHLIST(_tag, 2, ID(_1), ID(_2))
#define CHLIST03(_tag, _1, _2, _3)                         CHLIST(_tag, 3, ID(_1), ID(_2), ID(_3))
#define CHLIST04(_tag, _1, _2, _3, _4)                     CHLIST(_tag, 4, ID(_1), ID(_2), ID(_3), ID(_4))
#define CHLIST05(_tag, _1, _2, _3, _4, _5)                 CHLIST(_tag, 5, ID(_1), ID(_2), ID(_3), ID(_4), ID(_5))
#define CHLIST06(_tag, _1, _2, _3, _4, _5, _6)             CHLIST(_tag, 6, ID(_1), ID(_2), ID(_3), ID(_4), ID(_5), ID(_6))
#define CHLIST07(_tag, _1, _2, _3, _4, _5, _6, _7)         CHLIST(_tag, 7, ID(_1), ID(_2), ID(_3), ID(_4), ID(_5), ID(_6), ID(_7))
#define CHLIST08(_tag, _1, _2, _3, _4, _5, _6, _7, _8)     CHLIST(_tag, 8, ID(_1), ID(_2), ID(_3), ID(_4), ID(_5), ID(_6), ID(_7), ID(_8))
#define CHLIST09(_tag, _1, _2, _3, _4, _5, _6, _7, _8, _9) CHLIST(_tag, 9, ID(_1), ID(_2), ID(_3), ID(_4), ID(_5), ID(_6), ID(_7), ID(_8), ID(_9))
#define CHLIST16(_tag, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16) \
    CHLIST(_tag, 16, ID(_1),  ID(_2),  ID(_3),  ID(_4),  ID(_5),  ID(_6), ID(_7), ID(_8), ID(_9), ID(_10), \
                     ID(_11), ID(_12), ID(_13), ID(_14), ID(_15), ID(_16))
#define CHLIST21(_tag, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21) \
    CHLIST(_tag, 21, ID(_1),  ID(_2),  ID(_3),  ID(_4),  ID(_5),  ID(_6),  ID(_7),  ID(_8),  ID(_9),  ID(_10), \
                     ID(_11), ID(_12), ID(_13), ID(_14), ID(_15), ID(_16), ID(_17), ID(_18), ID(_19), ID(_20), ID(_21))
#define CHLIST24(_tag, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24) \
    CHLIST(_tag, 24, ID(_1),  ID(_2),  ID(_3),  ID(_4),  ID(_5),  ID(_6),  ID(_7),  ID(_8),  ID(_9),  ID(_10), \
                     ID(_11), ID(_12), ID(_13), ID(_14), ID(_15), ID(_16), ID(_17), ID(_18), ID(_19), ID(_20), \
                     ID(_21), ID(_22), ID(_23), ID(_24))

#define MOV_CH_LAYOUT_MAP \
    CHLIST01( MOV_CH_LAYOUT_MONO,                 C    )\
    CHLIST02( MOV_CH_LAYOUT_STEREO,               L,   R    )\
    CHLIST02( MOV_CH_LAYOUT_STEREOHEADPHONES,     L,   R    )\
    CHLIST02( MOV_CH_LAYOUT_BINAURAL,             L,   R    )\
    CHLIST02( MOV_CH_LAYOUT_MIDSIDE,              L,   R    )\
    CHLIST02( MOV_CH_LAYOUT_XY,                   L,   R    )\
    CHLIST02( MOV_CH_LAYOUT_MATRIXSTEREO,         Lt,  Rt   )\
    CHLIST02( MOV_CH_LAYOUT_AC3_1_0_1,            C,   LFE  )\
    CHLIST03( MOV_CH_LAYOUT_MPEG_3_0_A,           L,   R,   C     )\
    CHLIST03( MOV_CH_LAYOUT_MPEG_3_0_B,           C,   L,   R     )\
    CHLIST03( MOV_CH_LAYOUT_AC3_3_0,              L,   C,   R     )\
    CHLIST03( MOV_CH_LAYOUT_ITU_2_1,              L,   R,   Cs    )\
    CHLIST03( MOV_CH_LAYOUT_DVD_4,                L,   R,   LFE   )\
    CHLIST04( MOV_CH_LAYOUT_AMBISONIC_B_FORMAT,   W,   X,   Y,    Z     )\
    CHLIST04( MOV_CH_LAYOUT_QUADRAPHONIC,         L,   R,   Rls,  Rrs   )\
    CHLIST04( MOV_CH_LAYOUT_MPEG_4_0_A,           L,   R,   C,    Cs    )\
    CHLIST04( MOV_CH_LAYOUT_MPEG_4_0_B,           C,   L,   R,    Cs    )\
    CHLIST04( MOV_CH_LAYOUT_AC3_3_1,              L,   C,   R,    Cs    )\
    CHLIST04( MOV_CH_LAYOUT_ITU_2_2,              L,   R,   Ls,   Rs    )\
    CHLIST04( MOV_CH_LAYOUT_DVD_5,                L,   R,   LFE,  Cs    )\
    CHLIST04( MOV_CH_LAYOUT_AC3_2_1_1,            L,   R,   Cs,   LFE   )\
    CHLIST04( MOV_CH_LAYOUT_DVD_10,               L,   R,   C,    LFE   )\
    CHLIST04( MOV_CH_LAYOUT_AC3_3_0_1,            L,   C,   R,    LFE   )\
    CHLIST04( MOV_CH_LAYOUT_DTS_3_1,              C,   L,   R,    LFE   )\
    CHLIST05( MOV_CH_LAYOUT_PENTAGONAL,           L,   R,   Rls,  Rrs,  C     )\
    CHLIST05( MOV_CH_LAYOUT_MPEG_5_0_A,           L,   R,   C,    Ls,   Rs    )\
    CHLIST05( MOV_CH_LAYOUT_MPEG_5_0_B,           L,   R,   Ls,   Rs,   C     )\
    CHLIST05( MOV_CH_LAYOUT_MPEG_5_0_C,           L,   C,   R,    Ls,   Rs    )\
    CHLIST05( MOV_CH_LAYOUT_MPEG_5_0_D,           C,   L,   R,    Ls,   Rs    )\
    CHLIST05( MOV_CH_LAYOUT_DVD_6,                L,   R,   LFE,  Ls,   Rs    )\
    CHLIST05( MOV_CH_LAYOUT_DVD_18,               L,   R,   Ls,   Rs,   LFE   )\
    CHLIST05( MOV_CH_LAYOUT_DVD_11,               L,   R,   C,    LFE,  Cs    )\
    CHLIST05( MOV_CH_LAYOUT_AC3_3_1_1,            L,   C,   R,    Cs,   LFE   )\
    CHLIST05( MOV_CH_LAYOUT_DTS_4_1,              C,   L,   R,    Cs,   LFE   )\
    CHLIST06( MOV_CH_LAYOUT_HEXAGONAL,            L,   R,   Rls,  Rrs,  C,    Cs     )\
    CHLIST06( MOV_CH_LAYOUT_DTS_6_0_C,            C,   Cs,  L,    R,    Rls,  Rrs    )\
    CHLIST06( MOV_CH_LAYOUT_MPEG_5_1_A,           L,   R,   C,    LFE,  Ls,   Rs     )\
    CHLIST06( MOV_CH_LAYOUT_MPEG_5_1_B,           L,   R,   Ls,   Rs,   C,    LFE    )\
    CHLIST06( MOV_CH_LAYOUT_MPEG_5_1_C,           L,   C,   R,    Ls,   Rs,   LFE    )\
    CHLIST06( MOV_CH_LAYOUT_MPEG_5_1_D,           C,   L,   R,    Ls,   Rs,   LFE    )\
    CHLIST06( MOV_CH_LAYOUT_AUDIOUNIT_6_0,        L,   R,   Ls,   Rs,   C,    Cs     )\
    CHLIST06( MOV_CH_LAYOUT_AAC_6_0,              C,   L,   R,    Ls,   Rs,   Cs     )\
    CHLIST06( MOV_CH_LAYOUT_EAC3_6_0_A,           L,   C,   R,    Ls,   Rs,   Cs     )\
    CHLIST06( MOV_CH_LAYOUT_DTS_6_0_A,            Lc,  Rc,  L,    R,    Ls,   Rs     )\
    CHLIST06( MOV_CH_LAYOUT_DTS_6_0_B,            C,   L,   R,    Rls,  Rrs,  Ts     )\
    CHLIST07( MOV_CH_LAYOUT_MPEG_6_1_A,           L,   R,   C,    LFE,  Ls,   Rs,    Cs    )\
    CHLIST07( MOV_CH_LAYOUT_AAC_6_1,              C,   L,   R,    Ls,   Rs,   Cs,    LFE   )\
    CHLIST07( MOV_CH_LAYOUT_EAC3_6_1_A,           L,   C,   R,    Ls,   Rs,   LFE,   Cs    )\
    CHLIST07( MOV_CH_LAYOUT_DTS_6_1_D,            C,   L,   R,    Ls,   Rs,   LFE,   Cs    )\
    CHLIST07( MOV_CH_LAYOUT_AUDIOUNIT_7_0,        L,   R,   Ls,   Rs,   C,    Rls,   Rrs   )\
    CHLIST07( MOV_CH_LAYOUT_AAC_7_0,              C,   L,   R,    Ls,   Rs,   Rls,   Rrs   )\
    CHLIST07( MOV_CH_LAYOUT_EAC3_7_0_A,           L,   C,   R,    Ls,   Rs,   Rls,   Rrs   )\
    CHLIST07( MOV_CH_LAYOUT_AUDIOUNIT_7_0_FRONT,  L,   R,   Ls,   Rs,   C,    Lc,    Rc    )\
    CHLIST07( MOV_CH_LAYOUT_DTS_7_0,              Lc,  C,   Rc,   L,    R,    Ls,    Rs    )\
    CHLIST07( MOV_CH_LAYOUT_EAC3_6_1_B,           L,   C,   R,    Ls,   Rs,   LFE,   Ts    )\
    CHLIST07( MOV_CH_LAYOUT_EAC3_6_1_C,           L,   C,   R,    Ls,   Rs,   LFE,   Vhc   )\
    CHLIST07( MOV_CH_LAYOUT_DTS_6_1_A,            Lc,  Rc,  L,    R,    Ls,   Rs,    LFE   )\
    CHLIST07( MOV_CH_LAYOUT_DTS_6_1_B,            C,   L,   R,    Rls,  Rrs,  Ts,    LFE   )\
    CHLIST07( MOV_CH_LAYOUT_DTS_6_1_C,            C,   Cs,  L,    R,    Rls,  Rrs,   LFE   )\
    CHLIST08( MOV_CH_LAYOUT_OCTAGONAL,            L,   R,   Rls,  Rrs,  C,    Cs,    Ls,   Rs    )\
    CHLIST08( MOV_CH_LAYOUT_AAC_OCTAGONAL,        C,   L,   R,    Ls,   Rs,   Rls,   Rrs,  Cs    )\
    CHLIST08( MOV_CH_LAYOUT_CUBE,                 L,   R,   Rls,  Rrs,  Vhl,  Vhr,   Rlt,  Rrt   )\
    CHLIST08( MOV_CH_LAYOUT_MPEG_7_1_A,           L,   R,   C,    LFE,  Ls,   Rs,    Lc,   Rc    )\
    CHLIST08( MOV_CH_LAYOUT_MPEG_7_1_B,           C,   Lc,  Rc,   L,    R,    Ls,    Rs,   LFE   )\
    CHLIST08( MOV_CH_LAYOUT_EMAGIC_DEFAULT_7_1,   L,   R,   Ls,   Rs,   C,    LFE,   Lc,   Rc    )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_B,           L,   C,   R,    Ls,   Rs,   LFE,   Lc,   Rc    )\
    CHLIST08( MOV_CH_LAYOUT_DTS_7_1,              Lc,  C,   Rc,   L,    R,    Ls,    Rs,   LFE   )\
    CHLIST08( MOV_CH_LAYOUT_MPEG_7_1_C,           L,   R,   C,    LFE,  Ls,   Rs,    Rls,  Rrs   )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_A,           L,   C,   R,    Ls,   Rs,   LFE,   Rls,  Rrs   )\
    CHLIST08( MOV_CH_LAYOUT_SMPTE_DTV,            L,   R,   C,    LFE,  Ls,   Rs,    Lt,   Rt    )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_C,           L,   C,   R,    Ls,   Rs,   LFE,   Lsd,  Rsd   )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_D,           L,   C,   R,    Ls,   Rs,   LFE,   Lw,   Rw    )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_E,           L,   C,   R,    Ls,   Rs,   LFE,   Vhl,  Vhr   )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_F,           L,   C,   R,    Ls,   Rs,   LFE,   Cs,   Ts    )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_G,           L,   C,   R,    Ls,   Rs,   LFE,   Cs,   Vhc   )\
    CHLIST08( MOV_CH_LAYOUT_EAC3_7_1_H,           L,   C,   R,    Ls,   Rs,   LFE,   Ts,   Vhc   )\
    CHLIST08( MOV_CH_LAYOUT_DTS_8_0_A,            Lc,  Rc,  L,    R,    Ls,   Rs,    Rls,  Rrs   )\
    CHLIST08( MOV_CH_LAYOUT_DTS_8_0_B,            Lc,  C,   Rc,   L,    R,    Ls,    Cs,   Rs    )\
    CHLIST09( MOV_CH_LAYOUT_DTS_8_1_A,            Lc,  Rc,  L,    R,    Ls,   Rs,    Rls,  Rrs,  LFE   )\
    CHLIST09( MOV_CH_LAYOUT_DTS_8_1_B,            Lc,  C,   Rc,   L,    R,    Ls,    Cs,   Rs,   LFE   )\
    CHLIST16( MOV_CH_LAYOUT_TMH_10_2_STD,         L,   R,   C,    Vhc,  Lsd,  Rsd,   Ls,   Rs,   Vhl,  Vhr,  Lw,  Rw,  Csd,  Cs,  LFE1,  LFE2  )\
    CHLIST21( MOV_CH_LAYOUT_TMH_10_2_FULL,        L,   R,   C,    Vhc,  Lsd,  Rsd,   Ls,   Rs,   Vhl,  Vhr,  Lw,  Rw,  Csd,  Cs,  LFE1,  LFE2,  Lc,  Rc,  HI,  VI,  Haptic  )\

#define CHLIST(_tag, _cnt, ...)    static_assert((_tag & 0xffff) == _cnt, "Channel count of " #_tag " is not " #_cnt);
MOV_CH_LAYOUT_MAP
#undef CHLIST
#define CHLIST(_tag, _cnt, ...)    TAG(_tag, _cnt), __VA_ARGS__,
static const struct MovChannelLayoutMap mov_ch_layout_map[] = {
    MOV_CH_LAYOUT_MAP
    { {0} },
};

#undef ID
#undef TAG
#define ID(_0)            {.id = iso_##_0}
#define TAG(_tag, _cnt)   {.tag = (_tag << 16) | _cnt}

/* ISO/IEC 23001-8 */
static const struct MovChannelLayoutMap iso_ch_layout_map[] = {
    CHLIST01(  1,  C )
    CHLIST02(  2,  L,   R   )
    CHLIST03(  3,  C,   L,   R   )
    CHLIST04(  4,  C,   L,   R,    Cs  )
    CHLIST05(  5,  C,   L,   R,    Ls,   Rs  )
    CHLIST06(  6,  C,   L,   R,    Ls,   Rs,   LFE )
    CHLIST08(  7,  C,   Lc,  Rc,   L,    R,    Ls,   Rs,   LFE )
    CHLIST03(  9,  L,   R,   Cs  )
    CHLIST04( 10,  L,   R,   Ls,   Rs  )
    CHLIST07( 11,  C,   L,   R,    Ls,   Rs,   Cs,   LFE )
    CHLIST08( 12,  C,   L,   R,    Ls,   Rs,   Lsr,  Rsr,  LFE )
    CHLIST24( 13,  C,   Lc,  Rc,   L,    R,    Lss,  Rss,  Lsr,  Rsr,  Cs,  LFE,  LFE2,  Cv,  Lv,  Rv,  Lvss,  Rvss,  Ts,  Lvr,  Rvr,  Cvr,  Cb,  Lb,  Rb)
    CHLIST08( 14,  C,   L,   R,    Ls,   Rs,   LFE,  Lv,   Rv)
    { {0} },
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

static const struct MovChannelLayoutMap* find_layout_map(uint32_t tag, const struct MovChannelLayoutMap *map)
{
    for (int i = 0; map[i].tag & 0xffff; i += 1 + (map[i].tag & 0xffff))
        if (map[i].tag == tag)
            return &map[i + 1];
    return NULL;
}

/**
 * Get the channel layout for the specified non-special channel layout tag if
 * known.
 *
 * @param[in,out]  ch_layout  channel layout
 * @param[in]      tag        channel layout tag
 * @return                    <0 on error
 */
static int mov_get_channel_layout(AVChannelLayout *ch_layout, uint32_t tag, uint64_t omitted_channel_map, const struct MovChannelLayoutMap *map)
{
    const struct MovChannelLayoutMap *layout_map;

    /* find the channel layout for the specified layout tag */
    layout_map = find_layout_map(tag, map);
    if (layout_map) {
        int ret;
        int map_layout_nb_channels = tag & 0xFFFF;
        int nb_channels = ch_layout->nb_channels;

        /* Omitted channel bits must not exceed number of channels in map */
        if (omitted_channel_map >> map_layout_nb_channels)
            return AVERROR_INVALIDDATA;

        av_channel_layout_uninit(ch_layout);
        ret = av_channel_layout_custom_init(ch_layout, nb_channels);
        if (ret < 0)
            return ret;

        for (int i = 0, idx = 0; i < map_layout_nb_channels && idx < nb_channels; i++, omitted_channel_map >>= 1) {
            if (!(omitted_channel_map & 1)) {
                enum AVChannel id = layout_map[i].id;
                ch_layout->u.map[idx++].id = (id != AV_CHAN_NONE ? id : AV_CHAN_UNKNOWN);
            }
        }

        return av_channel_layout_retype(ch_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
    }
    return 0;
}

static enum AVChannel mov_get_channel_id(uint32_t label)
{
    if (label == 0)
        return AV_CHAN_UNUSED;
    if (label <= 18)
        return (label - 1);
    if (label == 35)
        return AV_CHAN_WIDE_LEFT;
    if (label == 36)
        return AV_CHAN_WIDE_RIGHT;
    if (label == 37)
        return AV_CHAN_LOW_FREQUENCY_2;
    if (label == 38)
        return AV_CHAN_STEREO_LEFT;
    if (label == 39)
        return AV_CHAN_STEREO_RIGHT;
    return AV_CHAN_UNKNOWN;
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

static int is_layout_valid_for_tag(const AVChannelLayout *ch_layout, uint32_t tag, const struct MovChannelLayoutMap *map)
{
    const struct MovChannelLayoutMap *layout_map;
    int channels = ch_layout->nb_channels;

    /* get the layout map based on the channel count */
    if ((tag & 0xFFFF) != channels)
        return 0;

    layout_map = find_layout_map(tag, map);
    if (layout_map) {
        int i;
        for (i = 0; i < channels; i++) {
            if (av_channel_layout_channel_from_index(ch_layout, i) != layout_map[i].id)
                break;
        }
        if (i == channels)
            return 1;
    }
    return 0;
}

int ff_mov_get_channel_layout_tag(const AVCodecParameters *par,
                                  uint32_t *layout,
                                  uint32_t *bitmap,
                                  uint32_t **pchannel_desc)
{
    int i;
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
        /* find the layout tag for the specified channel layout */
        for (i = 0; layouts[i] != 0; i++)
            if (is_layout_valid_for_tag(&par->ch_layout, layouts[i], mov_ch_layout_map))
                break;

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
    int ret;
    AVChannelLayout *ch_layout = &st->codecpar->ch_layout;

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

    if (layout_tag == MOV_CH_LAYOUT_USE_DESCRIPTIONS) {
        int nb_channels = ch_layout->nb_channels;

        if (!num_descr || num_descr < nb_channels) {
            av_log(s, AV_LOG_ERROR, "got %d channel descriptions when at least %d were needed\n",
                   num_descr, nb_channels);
            return AVERROR_INVALIDDATA;
        }

        if (num_descr > nb_channels) {
            int strict = s->strict_std_compliance >= FF_COMPLIANCE_STRICT;
            av_log(s, strict ? AV_LOG_ERROR : AV_LOG_WARNING,
                   "got %d channel descriptions when number of channels is %d\n",
                   num_descr, nb_channels);
            if (strict)
                return AVERROR_INVALIDDATA;
            av_log(s, AV_LOG_WARNING, "capping channel descriptions to the number of channels\n");
            num_descr = nb_channels;
        }

        av_channel_layout_uninit(ch_layout);
        ret = av_channel_layout_custom_init(ch_layout, nb_channels);
        if (ret < 0)
            goto out;

        for (int i = 0; i < num_descr; i++) {
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
            ch_layout->u.map[i].id = mov_get_channel_id(label);
        }

        ret = av_channel_layout_retype(ch_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
        if (ret < 0)
            goto out;
    } else if (layout_tag == MOV_CH_LAYOUT_USE_BITMAP) {
        if (!ch_layout->nb_channels || av_popcount(bitmap) == ch_layout->nb_channels) {
            if (bitmap < 0x40000) {
                av_channel_layout_uninit(ch_layout);
                av_channel_layout_from_mask(ch_layout, bitmap);
            }
        } else {
            av_log(s, AV_LOG_WARNING, "ignoring channel layout bitmap with %d channels because number of channels is %d\n",
                   av_popcount64(bitmap), ch_layout->nb_channels);
        }
    } else if (layout_tag & 0xFFFF) {
        int nb_channels = layout_tag & 0xFFFF;
        if (!ch_layout->nb_channels)
            ch_layout->nb_channels = nb_channels;
        if (nb_channels == ch_layout->nb_channels) {
            ret = mov_get_channel_layout(ch_layout, layout_tag, 0, mov_ch_layout_map);
            if (ret < 0)
                return ret;
        } else {
            av_log(s, AV_LOG_WARNING, "ignoring layout tag with %d channels because number of channels is %d\n",
                   nb_channels, ch_layout->nb_channels);
        }
    }
    ret = 0;

out:
    avio_skip(pb, size - 12);

    return ret;
}

/* ISO/IEC 23001-8, table 8 */
static const enum AVChannel iso_channel_position[] = {
     [0] = AV_CHAN_FRONT_LEFT,              // left front
     [1] = AV_CHAN_FRONT_RIGHT,             // right front
     [2] = AV_CHAN_FRONT_CENTER,            // centre front
     [3] = AV_CHAN_LOW_FREQUENCY,           // low frequency enhancement
     [4] = AV_CHAN_SIDE_LEFT,               // left surround
     [5] = AV_CHAN_SIDE_RIGHT,              // right surround
     [6] = AV_CHAN_FRONT_LEFT_OF_CENTER,    // left front centre
     [7] = AV_CHAN_FRONT_RIGHT_OF_CENTER,   // right front centre
     [8] = AV_CHAN_BACK_LEFT,               // rear surround left
     [9] = AV_CHAN_BACK_RIGHT,              // rear surround right
    [10] = AV_CHAN_BACK_CENTER,             // rear centre
    [11] = AV_CHAN_SURROUND_DIRECT_LEFT,    // left surround direct
    [12] = AV_CHAN_SURROUND_DIRECT_RIGHT,   // right surround direct
    [13] = AV_CHAN_SIDE_SURROUND_LEFT,      // left side surround
    [14] = AV_CHAN_SIDE_SURROUND_RIGHT,     // right side surround
    [15] = AV_CHAN_WIDE_LEFT,               // left wide front
    [16] = AV_CHAN_WIDE_RIGHT,              // right wide front
    [17] = AV_CHAN_TOP_FRONT_LEFT,          // left front vertical height
    [18] = AV_CHAN_TOP_FRONT_RIGHT,         // right front vertical height
    [19] = AV_CHAN_TOP_FRONT_CENTER,        // centre front vertical height
    [20] = AV_CHAN_TOP_BACK_LEFT,           // left surround vertical height rear
    [21] = AV_CHAN_TOP_BACK_RIGHT,          // right surround vertical height rear
    [22] = AV_CHAN_TOP_BACK_CENTER,         // centre vertical height rear
    [23] = AV_CHAN_TOP_SIDE_LEFT,           // left vertical height side surround
    [24] = AV_CHAN_TOP_SIDE_RIGHT,          // right vertical height side surround
    [25] = AV_CHAN_TOP_CENTER,              // top centre surround
    [26] = AV_CHAN_LOW_FREQUENCY_2,         // low frequency enhancement 2
    [27] = AV_CHAN_BOTTOM_FRONT_LEFT,       // left front vertical bottom
    [28] = AV_CHAN_BOTTOM_FRONT_RIGHT,      // right front vertical bottom
    [29] = AV_CHAN_BOTTOM_FRONT_CENTER,     // centre front vertical bottom
    [30] = AV_CHAN_TOP_SURROUND_LEFT,       // left vertical height surround
    [31] = AV_CHAN_TOP_SURROUND_RIGHT,      // right vertical height surround

    [32] = AV_CHAN_NONE,                    // reserved
    [33] = AV_CHAN_NONE,                    // reserved
    [34] = AV_CHAN_NONE,                    // reserved
    [35] = AV_CHAN_NONE,                    // reserved

    [36] = AV_CHAN_NONE,                    // low frequency enhancement 3
    [37] = AV_CHAN_NONE,                    // left edge of screen
    [38] = AV_CHAN_NONE,                    // right edge of screen
    [39] = AV_CHAN_NONE,                    // half-way between centre of screen and left edge of screen
    [40] = AV_CHAN_NONE,                    // half-way between centre of screen and right edge of screen
    [41] = AV_CHAN_NONE,                    // left back surround
    [42] = AV_CHAN_NONE,                    // right back surround

    // 43 - 125: reserved
    // 126: explicit position
    // 127: unknown /undefined
};

int ff_mov_get_channel_config_from_layout(const AVChannelLayout *layout, int *config)
{
    // Set default value which means any setup in 23001-8
    *config = 0;
    for (int i = 0; iso_ch_layout_map[i].tag & 0xffff; i += 1 + (iso_ch_layout_map[i].tag & 0xffff)) {
        if (is_layout_valid_for_tag(layout, iso_ch_layout_map[i].tag, &iso_ch_layout_map[i])) {
            *config = iso_ch_layout_map[i].tag >> 16;
            break;
        }
    }

    return 0;
}

int ff_mov_get_channel_layout_from_config(int config, AVChannelLayout *layout, uint64_t omitted_channel_map)
{
    if (config > 0) {
        uint32_t layout_tag;
        int nb_omitted_channels = av_popcount64(omitted_channel_map);

        if (layout->nb_channels <= 0 || layout->nb_channels > UINT16_MAX - nb_omitted_channels)
            return AVERROR_INVALIDDATA;

        layout_tag = (config << 16) | (layout->nb_channels + nb_omitted_channels);
        return mov_get_channel_layout(layout, layout_tag, omitted_channel_map, iso_ch_layout_map);
    }
    return 1;
}

int ff_mov_get_channel_positions_from_layout(const AVChannelLayout *layout,
                                             uint8_t *position, int position_num)
{
    enum AVChannel channel;

    if (position_num < layout->nb_channels)
        return AVERROR(EINVAL);

    for (int i = 0; i < layout->nb_channels; i++) {
        position[i] = 127;
        channel = av_channel_layout_channel_from_index(layout, i);
        if (channel == AV_CHAN_NONE)
            return AVERROR(EINVAL);

        for (int j = 0; j < FF_ARRAY_ELEMS(iso_channel_position); j++) {
            if (iso_channel_position[j] == channel) {
                position[i] = j;
                break;
            }
        }
        if (position[i] == 127)
            return AVERROR(EINVAL);
    }

    return 0;
}

int ff_mov_read_chnl(AVFormatContext *s, AVIOContext *pb, AVStream *st)
{
    int stream_structure = avio_r8(pb);
    int ret;

    // stream carries channels
    if (stream_structure & 1) {
        int layout = avio_r8(pb);

        av_log(s, AV_LOG_TRACE, "'chnl' layout %d\n", layout);
        if (!layout) {
            AVChannelLayout *ch_layout = &st->codecpar->ch_layout;
            int nb_channels = ch_layout->nb_channels;

            av_channel_layout_uninit(ch_layout);
            ret = av_channel_layout_custom_init(ch_layout, nb_channels);
            if (ret < 0)
                return ret;

            for (int i = 0; i < nb_channels; i++) {
                int speaker_pos = avio_r8(pb);
                enum AVChannel channel;

                if (speaker_pos == 126) // explicit position
                    avio_skip(pb, 3);   // azimuth, elevation

                if (speaker_pos >= FF_ARRAY_ELEMS(iso_channel_position))
                    channel = AV_CHAN_NONE;
                else
                    channel = iso_channel_position[speaker_pos];

                if (channel == AV_CHAN_NONE) {
                    av_log(s, AV_LOG_WARNING, "speaker position %d is not implemented\n", speaker_pos);
                    channel = AV_CHAN_UNKNOWN;
                }

                ch_layout->u.map[i].id = channel;
            }

            ret = av_channel_layout_retype(ch_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
            if (ret < 0)
                return ret;
        } else {
            uint64_t omitted_channel_map = avio_rb64(pb);
            ret = ff_mov_get_channel_layout_from_config(layout, &st->codecpar->ch_layout, omitted_channel_map);
            if (ret < 0)
                return ret;
        }
    }

    // stream carries objects
    if (stream_structure & 2) {
        int obj_count = avio_r8(pb);
        av_log(s, AV_LOG_TRACE, "'chnl' with object_count %d\n", obj_count);
    }

    return 0;
}
