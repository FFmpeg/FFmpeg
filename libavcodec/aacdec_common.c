/*
 * Common code and tables of the AAC fixed- and floating-point decoders
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * Common code and tables of the AAC fixed- and floating-point decoders
 */

#include "aac.h"
#include "aacdectab.h"
#include "aactab.h"
#include "vlc.h"

#include "libavutil/attributes.h"
#include "libavutil/thread.h"

const int8_t ff_tags_per_config[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 5, 5, 16, 5, 0 };

const uint8_t ff_aac_channel_layout_map[16][16][3] = {
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { 0, } },
    { { 0, } },
    { { 0, } },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_SCE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_CPE, 2, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE  }, },
    {
      { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, // SCE1 = FC,
      { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, // CPE1 = FLc and FRc,
      { TYPE_CPE, 1, AAC_CHANNEL_FRONT }, // CPE2 = FL and FR,
      { TYPE_CPE, 2, AAC_CHANNEL_BACK  }, // CPE3 = SiL and SiR,
      { TYPE_CPE, 3, AAC_CHANNEL_BACK  }, // CPE4 = BL and BR,
      { TYPE_SCE, 1, AAC_CHANNEL_BACK  }, // SCE2 = BC,
      { TYPE_LFE, 0, AAC_CHANNEL_LFE   }, // LFE1 = LFE1,
      { TYPE_LFE, 1, AAC_CHANNEL_LFE   }, // LFE2 = LFE2,
      { TYPE_SCE, 2, AAC_CHANNEL_FRONT }, // SCE3 = TpFC,
      { TYPE_CPE, 4, AAC_CHANNEL_FRONT }, // CPE5 = TpFL and TpFR,
      { TYPE_CPE, 5, AAC_CHANNEL_SIDE  }, // CPE6 = TpSiL and TpSiR,
      { TYPE_SCE, 3, AAC_CHANNEL_SIDE  }, // SCE4 = TpC,
      { TYPE_CPE, 6, AAC_CHANNEL_BACK  }, // CPE7 = TpBL and TpBR,
      { TYPE_SCE, 4, AAC_CHANNEL_BACK  }, // SCE5 = TpBC,
      { TYPE_SCE, 5, AAC_CHANNEL_FRONT }, // SCE6 = BtFC,
      { TYPE_CPE, 7, AAC_CHANNEL_FRONT }, // CPE8 = BtFL and BtFR
    },
    { { TYPE_SCE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 0, AAC_CHANNEL_FRONT }, { TYPE_CPE, 1, AAC_CHANNEL_BACK }, { TYPE_LFE, 0, AAC_CHANNEL_LFE }, { TYPE_CPE, 2, AAC_CHANNEL_FRONT  }, },
    { { 0, } },
};

const int16_t ff_aac_channel_map[3][4][6] = {
    {
      { AV_CHAN_FRONT_CENTER,        AV_CHAN_FRONT_LEFT_OF_CENTER, AV_CHAN_FRONT_RIGHT_OF_CENTER, AV_CHAN_FRONT_LEFT,        AV_CHAN_FRONT_RIGHT,        AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_SIDE_LEFT,            AV_CHAN_SIDE_RIGHT,            AV_CHAN_BACK_LEFT,         AV_CHAN_BACK_RIGHT,         AV_CHAN_BACK_CENTER },
      { AV_CHAN_LOW_FREQUENCY,       AV_CHAN_LOW_FREQUENCY_2,      AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
    {
      { AV_CHAN_TOP_FRONT_CENTER,    AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_FRONT_LEFT,    AV_CHAN_TOP_FRONT_RIGHT,    AV_CHAN_NONE },
      { AV_CHAN_UNUSED,              AV_CHAN_TOP_SIDE_LEFT,        AV_CHAN_TOP_SIDE_RIGHT,        AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_TOP_CENTER},
      { AV_CHAN_UNUSED,              AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_TOP_BACK_LEFT,     AV_CHAN_TOP_BACK_RIGHT,     AV_CHAN_TOP_BACK_CENTER},
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE},
    },
    {
      { AV_CHAN_BOTTOM_FRONT_CENTER, AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_BOTTOM_FRONT_LEFT, AV_CHAN_BOTTOM_FRONT_RIGHT, AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
      { AV_CHAN_NONE,                AV_CHAN_NONE,                 AV_CHAN_NONE,                  AV_CHAN_NONE,              AV_CHAN_NONE,               AV_CHAN_NONE },
    },
};

#if FF_API_OLD_CHANNEL_LAYOUT
const uint64_t ff_aac_channel_layout[] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_7POINT1_WIDE_BACK,
    AV_CH_LAYOUT_6POINT1_BACK,
    AV_CH_LAYOUT_7POINT1,
    AV_CH_LAYOUT_22POINT2,
    AV_CH_LAYOUT_7POINT1_TOP_BACK,
    0,
};
#endif

const AVChannelLayout ff_aac_ch_layout[] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1,
    AV_CHANNEL_LAYOUT_22POINT2,
    AV_CHANNEL_LAYOUT_7POINT1_TOP_BACK,
    { 0 },
};

VLCElem ff_vlc_scalefactors[352];
const VLCElem *ff_vlc_spectral[11];

static av_cold void aacdec_common_init(void)
{
    static VLCElem vlc_buf[304 + 270 + 550 + 300 + 328 +
                           294 + 306 + 268 + 510 + 366 + 462];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);

    for (unsigned i = 0; i < 11; i++) {
#define TAB_WRAP_SIZE(name) name[i], sizeof(name[i][0]), sizeof(name[i][0])
        ff_vlc_spectral[i] =
            ff_vlc_init_tables_sparse(&state, 8, ff_aac_spectral_sizes[i],
                                      TAB_WRAP_SIZE(ff_aac_spectral_bits),
                                      TAB_WRAP_SIZE(ff_aac_spectral_codes),
                                      TAB_WRAP_SIZE(ff_aac_codebook_vector_idx),
                                      0);
    }

    VLC_INIT_STATIC_TABLE(ff_vlc_scalefactors, 7,
                          FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
                          ff_aac_scalefactor_bits,
                          sizeof(ff_aac_scalefactor_bits[0]),
                          sizeof(ff_aac_scalefactor_bits[0]),
                          ff_aac_scalefactor_code,
                          sizeof(ff_aac_scalefactor_code[0]),
                          sizeof(ff_aac_scalefactor_code[0]), 0);
}

av_cold void ff_aacdec_common_init_once(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, aacdec_common_init);
}
