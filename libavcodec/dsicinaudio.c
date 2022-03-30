/*
 * Delphine Software International CIN audio decoder
 * Copyright (c) 2006 Gregory Montoir (cyx@users.sourceforge.net)
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
 * Delphine Software International CIN audio decoder
 */

#include "libavutil/channel_layout.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "internal.h"
#include "mathops.h"

typedef struct CinAudioContext {
    int initial_decode_frame;
    int delta;
} CinAudioContext;


/* table defining a geometric sequence with multiplier = 32767 ^ (1 / 128) */
static const int16_t cinaudio_delta16_table[256] = {
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0, -30210, -27853, -25680, -23677, -21829,
    -20126, -18556, -17108, -15774, -14543, -13408, -12362, -11398,
    -10508,  -9689,  -8933,  -8236,  -7593,  -7001,  -6455,  -5951,
     -5487,  -5059,  -4664,  -4300,  -3964,  -3655,  -3370,  -3107,
     -2865,  -2641,  -2435,  -2245,  -2070,  -1908,  -1759,  -1622,
     -1495,  -1379,  -1271,  -1172,  -1080,   -996,   -918,   -847,
      -781,   -720,   -663,   -612,   -564,   -520,   -479,   -442,
      -407,   -376,   -346,   -319,   -294,   -271,   -250,   -230,
      -212,   -196,   -181,   -166,   -153,   -141,   -130,   -120,
      -111,   -102,    -94,    -87,    -80,    -74,    -68,    -62,
       -58,    -53,    -49,    -45,    -41,    -38,    -35,    -32,
       -30,    -27,    -25,    -23,    -21,    -20,    -18,    -17,
       -15,    -14,    -13,    -12,    -11,    -10,     -9,     -8,
        -7,     -6,     -5,     -4,     -3,     -2,     -1,      0,
         0,      1,      2,      3,      4,      5,      6,      7,
         8,      9,     10,     11,     12,     13,     14,     15,
        17,     18,     20,     21,     23,     25,     27,     30,
        32,     35,     38,     41,     45,     49,     53,     58,
        62,     68,     74,     80,     87,     94,    102,    111,
       120,    130,    141,    153,    166,    181,    196,    212,
       230,    250,    271,    294,    319,    346,    376,    407,
       442,    479,    520,    564,    612,    663,    720,    781,
       847,    918,    996,   1080,   1172,   1271,   1379,   1495,
      1622,   1759,   1908,   2070,   2245,   2435,   2641,   2865,
      3107,   3370,   3655,   3964,   4300,   4664,   5059,   5487,
      5951,   6455,   7001,   7593,   8236,   8933,   9689,  10508,
     11398,  12362,  13408,  14543,  15774,  17108,  18556,  20126,
     21829,  23677,  25680,  27853,  30210,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0
};

static av_cold int cinaudio_decode_init(AVCodecContext *avctx)
{
    CinAudioContext *cin = avctx->priv_data;

    cin->initial_decode_frame = 1;
    cin->delta                = 0;
    avctx->sample_fmt         = AV_SAMPLE_FMT_S16;
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout          = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    return 0;
}

static int cinaudio_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf     = avpkt->data;
    CinAudioContext *cin   = avctx->priv_data;
    const uint8_t *buf_end = buf + avpkt->size;
    int16_t *samples;
    int delta, ret;

    /* get output buffer */
    frame->nb_samples = avpkt->size - cin->initial_decode_frame;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t *)frame->data[0];

    delta = cin->delta;
    if (cin->initial_decode_frame) {
        cin->initial_decode_frame = 0;
        delta                     = sign_extend(AV_RL16(buf), 16);
        buf                      += 2;
        *samples++                = delta;
    }
    while (buf < buf_end) {
        delta     += cinaudio_delta16_table[*buf++];
        delta      = av_clip_int16(delta);
        *samples++ = delta;
    }
    cin->delta = delta;

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_dsicinaudio_decoder = {
    .p.name         = "dsicinaudio",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Delphine Software International CIN audio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_DSICINAUDIO,
    .priv_data_size = sizeof(CinAudioContext),
    .init           = cinaudio_decode_init,
    FF_CODEC_DECODE_CB(cinaudio_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
