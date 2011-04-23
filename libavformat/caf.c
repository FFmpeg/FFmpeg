/*
 * CAF common code
 * Copyright (c) 2007  Justin Ruggles
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
 * CAF common code
 */

#include "avformat.h"
#include "internal.h"
#include "caf.h"

/**
 * Known codec tags for CAF
 */
const AVCodecTag ff_codec_caf_tags[] = {
    { CODEC_ID_AAC,             MKBETAG('a','a','c',' ') },
    { CODEC_ID_AC3,             MKBETAG('a','c','-','3') },
    { CODEC_ID_ALAC,            MKBETAG('a','l','a','c') },
  /* FIXME: use DV demuxer, as done in MOV */
  /*{ CODEC_ID_DVAUDIO,         MKBETAG('v','d','v','a') },*/
  /*{ CODEC_ID_DVAUDIO,         MKBETAG('d','v','c','a') },*/
    { CODEC_ID_ADPCM_IMA_QT,    MKBETAG('i','m','a','4') },
    { CODEC_ID_MACE3,           MKBETAG('M','A','C','3') },
    { CODEC_ID_MACE6,           MKBETAG('M','A','C','6') },
    { CODEC_ID_MP3,             MKBETAG('.','m','p','3') },
    { CODEC_ID_MP2,             MKBETAG('.','m','p','2') },
    { CODEC_ID_MP1,             MKBETAG('.','m','p','1') },
    { CODEC_ID_PCM_ALAW,        MKBETAG('a','l','a','w') },
    { CODEC_ID_PCM_MULAW,       MKBETAG('u','l','a','w') },
    { CODEC_ID_QCELP,           MKBETAG('Q','c','l','p') },
    { CODEC_ID_QDM2,            MKBETAG('Q','D','M','2') },
    { CODEC_ID_QDM2,            MKBETAG('Q','D','M','C') },
  /* currently unsupported codecs */
  /*{ AC-3 over S/PDIF          MKBETAG('c','a','c','3') },*/
  /*{ MPEG4CELP                 MKBETAG('c','e','l','p') },*/
  /*{ MPEG4HVXC                 MKBETAG('h','v','x','c') },*/
  /*{ MPEG4TwinVQ               MKBETAG('t','w','v','q') },*/
    { CODEC_ID_NONE,            0 },
};

typedef struct CafChannelLayout {
    int64_t  channel_layout;
    uint32_t layout_tag;
} CafChannelLayout;

static const CafChannelLayout caf_channel_layout[] = {
    { AV_CH_LAYOUT_MONO,                         (100<<16) | 1}, //< kCAFChannelLayoutTag_Mono
    { AV_CH_LAYOUT_STEREO,                       (101<<16) | 2}, //< kCAFChannelLayoutTag_Stereo
    { AV_CH_LAYOUT_STEREO,                       (102<<16) | 2}, //< kCAFChannelLayoutTag_StereoHeadphones
    { AV_CH_LAYOUT_2_1,                          (131<<16) | 3}, //< kCAFChannelLayoutTag_ITU_2_1
    { AV_CH_LAYOUT_2_2,                          (132<<16) | 4}, //< kCAFChannelLayoutTag_ITU_2_2
    { AV_CH_LAYOUT_QUAD,                         (108<<16) | 4}, //< kCAFChannelLayoutTag_Quadraphonic
    { AV_CH_LAYOUT_SURROUND,                     (113<<16) | 3}, //< kCAFChannelLayoutTag_MPEG_3_0_A
    { AV_CH_LAYOUT_4POINT0,                      (115<<16) | 4}, //< kCAFChannelLayoutTag_MPEG_4_0_A
    { AV_CH_LAYOUT_5POINT0_BACK,                 (117<<16) | 5}, //< kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT0,                      (117<<16) | 5}, //< kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT1_BACK,                 (121<<16) | 6}, //< kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_5POINT1,                      (121<<16) | 6}, //< kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_7POINT1,                      (128<<16) | 8}, //< kCAFChannelLayoutTag_MPEG_7_1_C
    { AV_CH_LAYOUT_7POINT1_WIDE,                 (126<<16) | 8}, //< kCAFChannelLayoutTag_MPEG_7_1_A
    { AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY,   (133<<16) | 3}, //< kCAFChannelLayoutTag_DVD_4
    { AV_CH_LAYOUT_2_1|AV_CH_LOW_FREQUENCY,      (134<<16) | 4}, //< kCAFChannelLayoutTag_DVD_5
    { AV_CH_LAYOUT_2_2|AV_CH_LOW_FREQUENCY,      (135<<16) | 4}, //< kCAFChannelLayoutTag_DVD_6
    { AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY, (136<<16) | 4}, //< kCAFChannelLayoutTag_DVD_10
    { AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY,  (137<<16) | 5}, //< kCAFChannelLayoutTag_DVD_11
    { 0, 0},
};

void ff_read_chan_chunk(AVFormatContext *s, int64_t size, AVCodecContext *codec)
{
    uint32_t layout_tag;
    AVIOContext *pb = s->pb;
    const CafChannelLayout *caf_layout = caf_channel_layout;
    if (size != 12) {
        // Channel descriptions not implemented
        av_log_ask_for_sample(s, "Unimplemented channel layout.\n");
        avio_skip(pb, size);
        return;
    }
    layout_tag = avio_rb32(pb);
    if (layout_tag == 0x10000) { //< kCAFChannelLayoutTag_UseChannelBitmap
        codec->channel_layout = avio_rb32(pb);
        avio_skip(pb, 4);
        return;
    }
    while (caf_layout->channel_layout) {
        if (layout_tag == caf_layout->layout_tag) {
            codec->channel_layout = caf_layout->channel_layout;
            break;
        }
        caf_layout++;
    }
    if (!codec->channel_layout)
        av_log(s, AV_LOG_WARNING, "Unknown channel layout.\n");
    avio_skip(pb, 8);
}

