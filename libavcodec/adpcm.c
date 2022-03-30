/*
 * Copyright (c) 2001-2003 The FFmpeg project
 *
 * first version by Francois Revol (revol@free.fr)
 * fringe ADPCM codecs (e.g., DK3, DK4, Westwood)
 *   by Mike Melanson (melanson@pcisys.net)
 * CD-ROM XA ADPCM codec by BERO
 * EA ADPCM decoder by Robin Kay (komadori@myrealbox.com)
 * EA ADPCM R1/R2/R3 decoder by Peter Ross (pross@xvid.org)
 * EA IMA EACS decoder by Peter Ross (pross@xvid.org)
 * EA IMA SEAD decoder by Peter Ross (pross@xvid.org)
 * EA ADPCM XAS decoder by Peter Ross (pross@xvid.org)
 * MAXIS EA ADPCM decoder by Robert Marston (rmarston@gmail.com)
 * THP ADPCM decoder by Marco Gerards (mgerards@xs4all.nl)
 * Argonaut Games ADPCM decoder by Zane van Iperen (zane@zanevaniperen.com)
 * Simon & Schuster Interactive ADPCM decoder by Zane van Iperen (zane@zanevaniperen.com)
 * Ubisoft ADPCM decoder by Zane van Iperen (zane@zanevaniperen.com)
 * High Voltage Software ALP decoder by Zane van Iperen (zane@zanevaniperen.com)
 * Cunning Developments decoder by Zane van Iperen (zane@zanevaniperen.com)
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

#include "config_components.h"

#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "adpcm.h"
#include "adpcm_data.h"
#include "codec_internal.h"
#include "internal.h"

/**
 * @file
 * ADPCM decoders
 * Features and limitations:
 *
 * Reference documents:
 * http://wiki.multimedia.cx/index.php?title=Category:ADPCM_Audio_Codecs
 * http://www.pcisys.net/~melanson/codecs/simpleaudio.html [dead]
 * http://www.geocities.com/SiliconValley/8682/aud3.txt [dead]
 * http://openquicktime.sourceforge.net/
 * XAnim sources (xa_codec.c) http://xanim.polter.net/
 * http://www.cs.ucla.edu/~leec/mediabench/applications.html [dead]
 * SoX source code http://sox.sourceforge.net/
 *
 * CD-ROM XA:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/xaadpcm.html [dead]
 * vagpack & depack http://homepages.compuserve.de/bITmASTER32/psx-index.html [dead]
 * readstr http://www.geocities.co.jp/Playtown/2004/
 */

#define CASE_0(codec_id, ...)
#define CASE_1(codec_id, ...) \
    case codec_id:            \
    { __VA_ARGS__ }           \
    break;
#define CASE_2(enabled, codec_id, ...) \
        CASE_ ## enabled(codec_id, __VA_ARGS__)
#define CASE_3(config, codec_id, ...) \
        CASE_2(config, codec_id, __VA_ARGS__)
#define CASE(codec, ...) \
        CASE_3(CONFIG_ ## codec ## _DECODER, AV_CODEC_ID_ ## codec, __VA_ARGS__)

/* These are for CD-ROM XA ADPCM */
static const int8_t xa_adpcm_table[5][2] = {
    {   0,   0 },
    {  60,   0 },
    { 115, -52 },
    {  98, -55 },
    { 122, -60 }
};

static const int16_t afc_coeffs[2][16] = {
    { 0, 2048, 0, 1024, 4096, 3584, 3072, 4608, 4200, 4800, 5120, 2048, 1024, -1024, -1024, -2048 },
    { 0, 0, 2048, 1024, -2048, -1536, -1024, -2560, -2248, -2300, -3072, -2048, -1024, 1024, 0, 0 }
};

static const int16_t ea_adpcm_table[] = {
    0,  240,  460,  392,
    0,    0, -208, -220,
    0,    1,    3,    4,
    7,    8,   10,   11,
    0,   -1,   -3,   -4
};

/*
 * Dumped from the binaries:
 * - FantasticJourney.exe - 0x794D2, DGROUP:0x47A4D2
 * - BigRaceUSA.exe       - 0x9B8AA, DGROUP:0x49C4AA
 * - Timeshock!.exe       - 0x8506A, DGROUP:0x485C6A
 */
static const int8_t ima_cunning_index_table[9] = {
    -1, -1, -1, -1, 1, 2, 3, 4, -1
};

/*
 * Dumped from the binaries:
 * - FantasticJourney.exe - 0x79458, DGROUP:0x47A458
 * - BigRaceUSA.exe       - 0x9B830, DGROUP:0x49C430
 * - Timeshock!.exe       - 0x84FF0, DGROUP:0x485BF0
 */
static const int16_t ima_cunning_step_table[61] = {
       1,    1,   1,      1,     2,     2,     3,     3,    4,      5,
       6,    7,   8,     10,    12,    14,    16,    20,    24,    28,
      32,   40,  48,     56,    64,    80,    96,   112,   128,   160,
     192,  224,  256,   320,   384,   448,   512,   640,   768,   896,
    1024, 1280, 1536,  1792,  2048,  2560,  3072,  3584,  4096,  5120,
    6144, 7168, 8192, 10240, 12288, 14336, 16384, 20480, 24576, 28672, 0
};

static const int8_t adpcm_index_table2[4] = {
    -1,  2,
    -1,  2,
};

static const int8_t adpcm_index_table3[8] = {
    -1, -1,  1,  2,
    -1, -1,  1,  2,
};

static const int8_t adpcm_index_table5[32] = {
    -1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16,
    -1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16,
};

static const int8_t * const adpcm_index_tables[4] = {
    &adpcm_index_table2[0],
    &adpcm_index_table3[0],
    &ff_adpcm_index_table[0],
    &adpcm_index_table5[0],
};

static const int16_t mtaf_stepsize[32][16] = {
    {     1,     5,     9,    13,    16,    20,    24,    28,
         -1,    -5,    -9,   -13,   -16,   -20,   -24,   -28, },
    {     2,     6,    11,    15,    20,    24,    29,    33,
         -2,    -6,   -11,   -15,   -20,   -24,   -29,   -33, },
    {     2,     7,    13,    18,    23,    28,    34,    39,
         -2,    -7,   -13,   -18,   -23,   -28,   -34,   -39, },
    {     3,     9,    15,    21,    28,    34,    40,    46,
         -3,    -9,   -15,   -21,   -28,   -34,   -40,   -46, },
    {     3,    11,    18,    26,    33,    41,    48,    56,
         -3,   -11,   -18,   -26,   -33,   -41,   -48,   -56, },
    {     4,    13,    22,    31,    40,    49,    58,    67,
         -4,   -13,   -22,   -31,   -40,   -49,   -58,   -67, },
    {     5,    16,    26,    37,    48,    59,    69,    80,
         -5,   -16,   -26,   -37,   -48,   -59,   -69,   -80, },
    {     6,    19,    31,    44,    57,    70,    82,    95,
         -6,   -19,   -31,   -44,   -57,   -70,   -82,   -95, },
    {     7,    22,    38,    53,    68,    83,    99,   114,
         -7,   -22,   -38,   -53,   -68,   -83,   -99,  -114, },
    {     9,    27,    45,    63,    81,    99,   117,   135,
         -9,   -27,   -45,   -63,   -81,   -99,  -117,  -135, },
    {    10,    32,    53,    75,    96,   118,   139,   161,
        -10,   -32,   -53,   -75,   -96,  -118,  -139,  -161, },
    {    12,    38,    64,    90,   115,   141,   167,   193,
        -12,   -38,   -64,   -90,  -115,  -141,  -167,  -193, },
    {    15,    45,    76,   106,   137,   167,   198,   228,
        -15,   -45,   -76,  -106,  -137,  -167,  -198,  -228, },
    {    18,    54,    91,   127,   164,   200,   237,   273,
        -18,   -54,   -91,  -127,  -164,  -200,  -237,  -273, },
    {    21,    65,   108,   152,   195,   239,   282,   326,
        -21,   -65,  -108,  -152,  -195,  -239,  -282,  -326, },
    {    25,    77,   129,   181,   232,   284,   336,   388,
        -25,   -77,  -129,  -181,  -232,  -284,  -336,  -388, },
    {    30,    92,   153,   215,   276,   338,   399,   461,
        -30,   -92,  -153,  -215,  -276,  -338,  -399,  -461, },
    {    36,   109,   183,   256,   329,   402,   476,   549,
        -36,  -109,  -183,  -256,  -329,  -402,  -476,  -549, },
    {    43,   130,   218,   305,   392,   479,   567,   654,
        -43,  -130,  -218,  -305,  -392,  -479,  -567,  -654, },
    {    52,   156,   260,   364,   468,   572,   676,   780,
        -52,  -156,  -260,  -364,  -468,  -572,  -676,  -780, },
    {    62,   186,   310,   434,   558,   682,   806,   930,
        -62,  -186,  -310,  -434,  -558,  -682,  -806,  -930, },
    {    73,   221,   368,   516,   663,   811,   958,  1106,
        -73,  -221,  -368,  -516,  -663,  -811,  -958, -1106, },
    {    87,   263,   439,   615,   790,   966,  1142,  1318,
        -87,  -263,  -439,  -615,  -790,  -966, -1142, -1318, },
    {   104,   314,   523,   733,   942,  1152,  1361,  1571,
       -104,  -314,  -523,  -733,  -942, -1152, -1361, -1571, },
    {   124,   374,   623,   873,  1122,  1372,  1621,  1871,
       -124,  -374,  -623,  -873, -1122, -1372, -1621, -1871, },
    {   148,   445,   743,  1040,  1337,  1634,  1932,  2229,
       -148,  -445,  -743, -1040, -1337, -1634, -1932, -2229, },
    {   177,   531,   885,  1239,  1593,  1947,  2301,  2655,
       -177,  -531,  -885, -1239, -1593, -1947, -2301, -2655, },
    {   210,   632,  1053,  1475,  1896,  2318,  2739,  3161,
       -210,  -632, -1053, -1475, -1896, -2318, -2739, -3161, },
    {   251,   753,  1255,  1757,  2260,  2762,  3264,  3766,
       -251,  -753, -1255, -1757, -2260, -2762, -3264, -3766, },
    {   299,   897,  1495,  2093,  2692,  3290,  3888,  4486,
       -299,  -897, -1495, -2093, -2692, -3290, -3888, -4486, },
    {   356,  1068,  1781,  2493,  3206,  3918,  4631,  5343,
       -356, -1068, -1781, -2493, -3206, -3918, -4631, -5343, },
    {   424,  1273,  2121,  2970,  3819,  4668,  5516,  6365,
       -424, -1273, -2121, -2970, -3819, -4668, -5516, -6365, },
};

static const int16_t oki_step_table[49] = {
     16,  17,  19,  21,   23,   25,   28,   31,   34,  37,
     41,  45,  50,  55,   60,   66,   73,   80,   88,  97,
    107, 118, 130, 143,  157,  173,  190,  209,  230, 253,
    279, 307, 337, 371,  408,  449,  494,  544,  598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
};

// padded to zero where table size is less then 16
static const int8_t swf_index_tables[4][16] = {
    /*2*/ { -1, 2 },
    /*3*/ { -1, -1, 2, 4 },
    /*4*/ { -1, -1, -1, -1, 2, 4, 6, 8 },
    /*5*/ { -1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16 }
};

static const int8_t zork_index_table[8] = {
    -1, -1, -1, 1, 4, 7, 10, 12,
};

static const int8_t mtf_index_table[16] = {
     8,  6,  4,  2, -1, -1, -1, -1,
    -1, -1, -1, -1,  2,  4,  6,  8,
};

/* end of tables */

typedef struct ADPCMDecodeContext {
    ADPCMChannelStatus status[14];
    int vqa_version;                /**< VQA version. Used for ADPCM_IMA_WS */
    int has_status;                 /**< Status flag. Reset to 0 after a flush. */
} ADPCMDecodeContext;

static void adpcm_flush(AVCodecContext *avctx);

static av_cold int adpcm_decode_init(AVCodecContext * avctx)
{
    ADPCMDecodeContext *c = avctx->priv_data;
    unsigned int min_channels = 1;
    unsigned int max_channels = 2;

    adpcm_flush(avctx);

    switch(avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_IMA_AMV:
        max_channels = 1;
        break;
    case AV_CODEC_ID_ADPCM_DTK:
    case AV_CODEC_ID_ADPCM_EA:
        min_channels = 2;
        break;
    case AV_CODEC_ID_ADPCM_AFC:
    case AV_CODEC_ID_ADPCM_EA_R1:
    case AV_CODEC_ID_ADPCM_EA_R2:
    case AV_CODEC_ID_ADPCM_EA_R3:
    case AV_CODEC_ID_ADPCM_EA_XAS:
    case AV_CODEC_ID_ADPCM_MS:
        max_channels = 6;
        break;
    case AV_CODEC_ID_ADPCM_MTAF:
        min_channels = 2;
        max_channels = 8;
        if (avctx->ch_layout.nb_channels & 1) {
            avpriv_request_sample(avctx, "channel count %d", avctx->ch_layout.nb_channels);
            return AVERROR_PATCHWELCOME;
        }
        break;
    case AV_CODEC_ID_ADPCM_PSX:
        max_channels = 8;
        if (avctx->ch_layout.nb_channels <= 0 ||
            avctx->block_align % (16 * avctx->ch_layout.nb_channels))
            return AVERROR_INVALIDDATA;
        break;
    case AV_CODEC_ID_ADPCM_IMA_DAT4:
    case AV_CODEC_ID_ADPCM_THP:
    case AV_CODEC_ID_ADPCM_THP_LE:
        max_channels = 14;
        break;
    }
    if (avctx->ch_layout.nb_channels < min_channels ||
        avctx->ch_layout.nb_channels > max_channels) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of channels\n");
        return AVERROR(EINVAL);
    }

    switch(avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_IMA_WAV:
        if (avctx->bits_per_coded_sample < 2 || avctx->bits_per_coded_sample > 5)
            return AVERROR_INVALIDDATA;
        break;
    case AV_CODEC_ID_ADPCM_ARGO:
        if (avctx->bits_per_coded_sample != 4 ||
            avctx->block_align != 17 * avctx->ch_layout.nb_channels)
            return AVERROR_INVALIDDATA;
        break;
    case AV_CODEC_ID_ADPCM_ZORK:
        if (avctx->bits_per_coded_sample != 8)
            return AVERROR_INVALIDDATA;
        break;
    default:
        break;
    }

    switch (avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_AICA:
    case AV_CODEC_ID_ADPCM_IMA_CUNNING:
    case AV_CODEC_ID_ADPCM_IMA_DAT4:
    case AV_CODEC_ID_ADPCM_IMA_QT:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ADPCM_4XM:
    case AV_CODEC_ID_ADPCM_XA:
    case AV_CODEC_ID_ADPCM_EA_R1:
    case AV_CODEC_ID_ADPCM_EA_R2:
    case AV_CODEC_ID_ADPCM_EA_R3:
    case AV_CODEC_ID_ADPCM_EA_XAS:
    case AV_CODEC_ID_ADPCM_THP:
    case AV_CODEC_ID_ADPCM_THP_LE:
    case AV_CODEC_ID_ADPCM_AFC:
    case AV_CODEC_ID_ADPCM_DTK:
    case AV_CODEC_ID_ADPCM_PSX:
    case AV_CODEC_ID_ADPCM_MTAF:
    case AV_CODEC_ID_ADPCM_ARGO:
    case AV_CODEC_ID_ADPCM_IMA_MOFLEX:
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
        break;
    case AV_CODEC_ID_ADPCM_IMA_WS:
        avctx->sample_fmt = c->vqa_version == 3 ? AV_SAMPLE_FMT_S16P :
                                                  AV_SAMPLE_FMT_S16;
        break;
    case AV_CODEC_ID_ADPCM_MS:
        avctx->sample_fmt = avctx->ch_layout.nb_channels > 2 ? AV_SAMPLE_FMT_S16P :
                                                  AV_SAMPLE_FMT_S16;
        break;
    default:
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    }
    return 0;
}

static inline int16_t adpcm_agm_expand_nibble(ADPCMChannelStatus *c, int8_t nibble)
{
    int delta, pred, step, add;

    pred = c->predictor;
    delta = nibble & 7;
    step = c->step;
    add = (delta * 2 + 1) * step;
    if (add < 0)
        add = add + 7;

    if ((nibble & 8) == 0)
        pred = av_clip(pred + (add >> 3), -32767, 32767);
    else
        pred = av_clip(pred - (add >> 3), -32767, 32767);

    switch (delta) {
    case 7:
        step *= 0x99;
        break;
    case 6:
        c->step = av_clip(c->step * 2, 127, 24576);
        c->predictor = pred;
        return pred;
    case 5:
        step *= 0x66;
        break;
    case 4:
        step *= 0x4d;
        break;
    default:
        step *= 0x39;
        break;
    }

    if (step < 0)
        step += 0x3f;

    c->step = step >> 6;
    c->step = av_clip(c->step, 127, 24576);
    c->predictor = pred;
    return pred;
}

static inline int16_t adpcm_ima_expand_nibble(ADPCMChannelStatus *c, int8_t nibble, int shift)
{
    int step_index;
    int predictor;
    int sign, delta, diff, step;

    step = ff_adpcm_step_table[c->step_index];
    step_index = c->step_index + ff_adpcm_index_table[(unsigned)nibble];
    step_index = av_clip(step_index, 0, 88);

    sign = nibble & 8;
    delta = nibble & 7;
    /* perform direct multiplication instead of series of jumps proposed by
     * the reference ADPCM implementation since modern CPUs can do the mults
     * quickly enough */
    diff = ((2 * delta + 1) * step) >> shift;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return (int16_t)c->predictor;
}

static inline int16_t adpcm_ima_alp_expand_nibble(ADPCMChannelStatus *c, int8_t nibble, int shift)
{
    int step_index;
    int predictor;
    int sign, delta, diff, step;

    step = ff_adpcm_step_table[c->step_index];
    step_index = c->step_index + ff_adpcm_index_table[(unsigned)nibble];
    step_index = av_clip(step_index, 0, 88);

    sign = nibble & 8;
    delta = nibble & 7;
    diff = (delta * step) >> shift;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return (int16_t)c->predictor;
}

static inline int16_t adpcm_ima_mtf_expand_nibble(ADPCMChannelStatus *c, int nibble)
{
    int step_index, step, delta, predictor;

    step = ff_adpcm_step_table[c->step_index];

    delta = step * (2 * nibble - 15);
    predictor = c->predictor + delta;

    step_index = c->step_index + mtf_index_table[(unsigned)nibble];
    c->predictor = av_clip_int16(predictor >> 4);
    c->step_index = av_clip(step_index, 0, 88);

    return (int16_t)c->predictor;
}

static inline int16_t adpcm_ima_cunning_expand_nibble(ADPCMChannelStatus *c, int8_t nibble)
{
    int step_index;
    int predictor;
    int step;

    nibble = sign_extend(nibble & 0xF, 4);

    step = ima_cunning_step_table[c->step_index];
    step_index = c->step_index + ima_cunning_index_table[abs(nibble)];
    step_index = av_clip(step_index, 0, 60);

    predictor = c->predictor + step * nibble;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return c->predictor;
}

static inline int16_t adpcm_ima_wav_expand_nibble(ADPCMChannelStatus *c, GetBitContext *gb, int bps)
{
    int nibble, step_index, predictor, sign, delta, diff, step, shift;

    shift = bps - 1;
    nibble = get_bits_le(gb, bps),
    step = ff_adpcm_step_table[c->step_index];
    step_index = c->step_index + adpcm_index_tables[bps - 2][nibble];
    step_index = av_clip(step_index, 0, 88);

    sign = nibble & (1 << shift);
    delta = av_mod_uintp2(nibble, shift);
    diff = ((2 * delta + 1) * step) >> shift;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return (int16_t)c->predictor;
}

static inline int adpcm_ima_qt_expand_nibble(ADPCMChannelStatus *c, int nibble)
{
    int step_index;
    int predictor;
    int diff, step;

    step = ff_adpcm_step_table[c->step_index];
    step_index = c->step_index + ff_adpcm_index_table[nibble];
    step_index = av_clip(step_index, 0, 88);

    diff = step >> 3;
    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;

    if (nibble & 8)
        predictor = c->predictor - diff;
    else
        predictor = c->predictor + diff;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return c->predictor;
}

static inline int16_t adpcm_ms_expand_nibble(ADPCMChannelStatus *c, int nibble)
{
    int predictor;

    predictor = (((c->sample1) * (c->coeff1)) + ((c->sample2) * (c->coeff2))) / 64;
    predictor += ((nibble & 0x08)?(nibble - 0x10):(nibble)) * c->idelta;

    c->sample2 = c->sample1;
    c->sample1 = av_clip_int16(predictor);
    c->idelta = (ff_adpcm_AdaptationTable[(int)nibble] * c->idelta) >> 8;
    if (c->idelta < 16) c->idelta = 16;
    if (c->idelta > INT_MAX/768) {
        av_log(NULL, AV_LOG_WARNING, "idelta overflow\n");
        c->idelta = INT_MAX/768;
    }

    return c->sample1;
}

static inline int16_t adpcm_ima_oki_expand_nibble(ADPCMChannelStatus *c, int nibble)
{
    int step_index, predictor, sign, delta, diff, step;

    step = oki_step_table[c->step_index];
    step_index = c->step_index + ff_adpcm_index_table[(unsigned)nibble];
    step_index = av_clip(step_index, 0, 48);

    sign = nibble & 8;
    delta = nibble & 7;
    diff = ((2 * delta + 1) * step) >> 3;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    c->predictor = av_clip_intp2(predictor, 11);
    c->step_index = step_index;

    return c->predictor * 16;
}

static inline int16_t adpcm_ct_expand_nibble(ADPCMChannelStatus *c, int8_t nibble)
{
    int sign, delta, diff;
    int new_step;

    sign = nibble & 8;
    delta = nibble & 7;
    /* perform direct multiplication instead of series of jumps proposed by
     * the reference ADPCM implementation since modern CPUs can do the mults
     * quickly enough */
    diff = ((2 * delta + 1) * c->step) >> 3;
    /* predictor update is not so trivial: predictor is multiplied on 254/256 before updating */
    c->predictor = ((c->predictor * 254) >> 8) + (sign ? -diff : diff);
    c->predictor = av_clip_int16(c->predictor);
    /* calculate new step and clamp it to range 511..32767 */
    new_step = (ff_adpcm_AdaptationTable[nibble & 7] * c->step) >> 8;
    c->step = av_clip(new_step, 511, 32767);

    return (int16_t)c->predictor;
}

static inline int16_t adpcm_sbpro_expand_nibble(ADPCMChannelStatus *c, int8_t nibble, int size, int shift)
{
    int sign, delta, diff;

    sign = nibble & (1<<(size-1));
    delta = nibble & ((1<<(size-1))-1);
    diff = delta << (7 + c->step + shift);

    /* clamp result */
    c->predictor = av_clip(c->predictor + (sign ? -diff : diff), -16384,16256);

    /* calculate new step */
    if (delta >= (2*size - 3) && c->step < 3)
        c->step++;
    else if (delta == 0 && c->step > 0)
        c->step--;

    return (int16_t) c->predictor;
}

static inline int16_t adpcm_yamaha_expand_nibble(ADPCMChannelStatus *c, uint8_t nibble)
{
    if(!c->step) {
        c->predictor = 0;
        c->step = 127;
    }

    c->predictor += (c->step * ff_adpcm_yamaha_difflookup[nibble]) / 8;
    c->predictor = av_clip_int16(c->predictor);
    c->step = (c->step * ff_adpcm_yamaha_indexscale[nibble]) >> 8;
    c->step = av_clip(c->step, 127, 24576);
    return c->predictor;
}

static inline int16_t adpcm_mtaf_expand_nibble(ADPCMChannelStatus *c, uint8_t nibble)
{
    c->predictor += mtaf_stepsize[c->step][nibble];
    c->predictor = av_clip_int16(c->predictor);
    c->step += ff_adpcm_index_table[nibble];
    c->step = av_clip_uintp2(c->step, 5);
    return c->predictor;
}

static inline int16_t adpcm_zork_expand_nibble(ADPCMChannelStatus *c, uint8_t nibble)
{
    int16_t index = c->step_index;
    uint32_t lookup_sample = ff_adpcm_step_table[index];
    int32_t sample = 0;

    if (nibble & 0x40)
        sample += lookup_sample;
    if (nibble & 0x20)
        sample += lookup_sample >> 1;
    if (nibble & 0x10)
        sample += lookup_sample >> 2;
    if (nibble & 0x08)
        sample += lookup_sample >> 3;
    if (nibble & 0x04)
        sample += lookup_sample >> 4;
    if (nibble & 0x02)
        sample += lookup_sample >> 5;
    if (nibble & 0x01)
        sample += lookup_sample >> 6;
    if (nibble & 0x80)
        sample = -sample;

    sample += c->predictor;
    sample = av_clip_int16(sample);

    index += zork_index_table[(nibble >> 4) & 7];
    index = av_clip(index, 0, 88);

    c->predictor = sample;
    c->step_index = index;

    return sample;
}

static int xa_decode(AVCodecContext *avctx, int16_t *out0, int16_t *out1,
                     const uint8_t *in, ADPCMChannelStatus *left,
                     ADPCMChannelStatus *right, int channels, int sample_offset)
{
    int i, j;
    int shift,filter,f0,f1;
    int s_1,s_2;
    int d,s,t;

    out0 += sample_offset;
    if (channels == 1)
        out1 = out0 + 28;
    else
        out1 += sample_offset;

    for(i=0;i<4;i++) {
        shift  = 12 - (in[4+i*2] & 15);
        filter = in[4+i*2] >> 4;
        if (filter >= FF_ARRAY_ELEMS(xa_adpcm_table)) {
            avpriv_request_sample(avctx, "unknown XA-ADPCM filter %d", filter);
            filter=0;
        }
        if (shift < 0) {
            avpriv_request_sample(avctx, "unknown XA-ADPCM shift %d", shift);
            shift = 0;
        }
        f0 = xa_adpcm_table[filter][0];
        f1 = xa_adpcm_table[filter][1];

        s_1 = left->sample1;
        s_2 = left->sample2;

        for(j=0;j<28;j++) {
            d = in[16+i+j*4];

            t = sign_extend(d, 4);
            s = t*(1<<shift) + ((s_1*f0 + s_2*f1+32)>>6);
            s_2 = s_1;
            s_1 = av_clip_int16(s);
            out0[j] = s_1;
        }

        if (channels == 2) {
            left->sample1 = s_1;
            left->sample2 = s_2;
            s_1 = right->sample1;
            s_2 = right->sample2;
        }

        shift  = 12 - (in[5+i*2] & 15);
        filter = in[5+i*2] >> 4;
        if (filter >= FF_ARRAY_ELEMS(xa_adpcm_table) || shift < 0) {
            avpriv_request_sample(avctx, "unknown XA-ADPCM filter %d", filter);
            filter=0;
        }
        if (shift < 0) {
            avpriv_request_sample(avctx, "unknown XA-ADPCM shift %d", shift);
            shift = 0;
        }

        f0 = xa_adpcm_table[filter][0];
        f1 = xa_adpcm_table[filter][1];

        for(j=0;j<28;j++) {
            d = in[16+i+j*4];

            t = sign_extend(d >> 4, 4);
            s = t*(1<<shift) + ((s_1*f0 + s_2*f1+32)>>6);
            s_2 = s_1;
            s_1 = av_clip_int16(s);
            out1[j] = s_1;
        }

        if (channels == 2) {
            right->sample1 = s_1;
            right->sample2 = s_2;
        } else {
            left->sample1 = s_1;
            left->sample2 = s_2;
        }

        out0 += 28 * (3 - channels);
        out1 += 28 * (3 - channels);
    }

    return 0;
}

static void adpcm_swf_decode(AVCodecContext *avctx, const uint8_t *buf, int buf_size, int16_t *samples)
{
    ADPCMDecodeContext *c = avctx->priv_data;
    GetBitContext gb;
    const int8_t *table;
    int channels = avctx->ch_layout.nb_channels;
    int k0, signmask, nb_bits, count;
    int size = buf_size*8;
    int i;

    init_get_bits(&gb, buf, size);

    //read bits & initial values
    nb_bits = get_bits(&gb, 2)+2;
    table = swf_index_tables[nb_bits-2];
    k0 = 1 << (nb_bits-2);
    signmask = 1 << (nb_bits-1);

    while (get_bits_count(&gb) <= size - 22 * channels) {
        for (i = 0; i < channels; i++) {
            *samples++ = c->status[i].predictor = get_sbits(&gb, 16);
            c->status[i].step_index = get_bits(&gb, 6);
        }

        for (count = 0; get_bits_count(&gb) <= size - nb_bits * channels && count < 4095; count++) {
            int i;

            for (i = 0; i < channels; i++) {
                // similar to IMA adpcm
                int delta = get_bits(&gb, nb_bits);
                int step = ff_adpcm_step_table[c->status[i].step_index];
                int vpdiff = 0; // vpdiff = (delta+0.5)*step/4
                int k = k0;

                do {
                    if (delta & k)
                        vpdiff += step;
                    step >>= 1;
                    k >>= 1;
                } while(k);
                vpdiff += step;

                if (delta & signmask)
                    c->status[i].predictor -= vpdiff;
                else
                    c->status[i].predictor += vpdiff;

                c->status[i].step_index += table[delta & (~signmask)];

                c->status[i].step_index = av_clip(c->status[i].step_index, 0, 88);
                c->status[i].predictor = av_clip_int16(c->status[i].predictor);

                *samples++ = c->status[i].predictor;
            }
        }
    }
}

int16_t ff_adpcm_argo_expand_nibble(ADPCMChannelStatus *cs, int nibble, int shift, int flag)
{
    int sample = sign_extend(nibble, 4) * (1 << shift);

    if (flag)
        sample += (8 * cs->sample1) - (4 * cs->sample2);
    else
        sample += 4 * cs->sample1;

    sample = av_clip_int16(sample >> 2);

    cs->sample2 = cs->sample1;
    cs->sample1 = sample;

    return sample;
}

/**
 * Get the number of samples (per channel) that will be decoded from the packet.
 * In one case, this is actually the maximum number of samples possible to
 * decode with the given buf_size.
 *
 * @param[out] coded_samples set to the number of samples as coded in the
 *                           packet, or 0 if the codec does not encode the
 *                           number of samples in each frame.
 * @param[out] approx_nb_samples set to non-zero if the number of samples
 *                               returned is an approximation.
 */
static int get_nb_samples(AVCodecContext *avctx, GetByteContext *gb,
                          int buf_size, int *coded_samples, int *approx_nb_samples)
{
    ADPCMDecodeContext *s = avctx->priv_data;
    int nb_samples        = 0;
    int ch                = avctx->ch_layout.nb_channels;
    int has_coded_samples = 0;
    int header_size;

    *coded_samples = 0;
    *approx_nb_samples = 0;

    if(ch <= 0)
        return 0;

    switch (avctx->codec->id) {
    /* constant, only check buf_size */
    case AV_CODEC_ID_ADPCM_EA_XAS:
        if (buf_size < 76 * ch)
            return 0;
        nb_samples = 128;
        break;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        if (buf_size < 34 * ch)
            return 0;
        nb_samples = 64;
        break;
    /* simple 4-bit adpcm */
    case AV_CODEC_ID_ADPCM_CT:
    case AV_CODEC_ID_ADPCM_IMA_APC:
    case AV_CODEC_ID_ADPCM_IMA_CUNNING:
    case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
    case AV_CODEC_ID_ADPCM_IMA_OKI:
    case AV_CODEC_ID_ADPCM_IMA_WS:
    case AV_CODEC_ID_ADPCM_YAMAHA:
    case AV_CODEC_ID_ADPCM_AICA:
    case AV_CODEC_ID_ADPCM_IMA_SSI:
    case AV_CODEC_ID_ADPCM_IMA_APM:
    case AV_CODEC_ID_ADPCM_IMA_ALP:
    case AV_CODEC_ID_ADPCM_IMA_MTF:
        nb_samples = buf_size * 2 / ch;
        break;
    }
    if (nb_samples)
        return nb_samples;

    /* simple 4-bit adpcm, with header */
    header_size = 0;
    switch (avctx->codec->id) {
        case AV_CODEC_ID_ADPCM_4XM:
        case AV_CODEC_ID_ADPCM_AGM:
        case AV_CODEC_ID_ADPCM_IMA_ACORN:
        case AV_CODEC_ID_ADPCM_IMA_DAT4:
        case AV_CODEC_ID_ADPCM_IMA_MOFLEX:
        case AV_CODEC_ID_ADPCM_IMA_ISS:     header_size = 4 * ch;      break;
        case AV_CODEC_ID_ADPCM_IMA_SMJPEG:  header_size = 4 * ch;      break;
    }
    if (header_size > 0)
        return (buf_size - header_size) * 2 / ch;

    /* more complex formats */
    switch (avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_IMA_AMV:
        bytestream2_skip(gb, 4);
        has_coded_samples  = 1;
        *coded_samples     = bytestream2_get_le32u(gb);
        nb_samples         = FFMIN((buf_size - 8) * 2, *coded_samples);
        bytestream2_seek(gb, -8, SEEK_CUR);
        break;
    case AV_CODEC_ID_ADPCM_EA:
        has_coded_samples = 1;
        *coded_samples  = bytestream2_get_le32(gb);
        *coded_samples -= *coded_samples % 28;
        nb_samples      = (buf_size - 12) / 30 * 28;
        break;
    case AV_CODEC_ID_ADPCM_IMA_EA_EACS:
        has_coded_samples = 1;
        *coded_samples = bytestream2_get_le32(gb);
        nb_samples     = (buf_size - (4 + 8 * ch)) * 2 / ch;
        break;
    case AV_CODEC_ID_ADPCM_EA_MAXIS_XA:
        nb_samples = (buf_size - ch) / ch * 2;
        break;
    case AV_CODEC_ID_ADPCM_EA_R1:
    case AV_CODEC_ID_ADPCM_EA_R2:
    case AV_CODEC_ID_ADPCM_EA_R3:
        /* maximum number of samples */
        /* has internal offsets and a per-frame switch to signal raw 16-bit */
        has_coded_samples = 1;
        switch (avctx->codec->id) {
        case AV_CODEC_ID_ADPCM_EA_R1:
            header_size    = 4 + 9 * ch;
            *coded_samples = bytestream2_get_le32(gb);
            break;
        case AV_CODEC_ID_ADPCM_EA_R2:
            header_size    = 4 + 5 * ch;
            *coded_samples = bytestream2_get_le32(gb);
            break;
        case AV_CODEC_ID_ADPCM_EA_R3:
            header_size    = 4 + 5 * ch;
            *coded_samples = bytestream2_get_be32(gb);
            break;
        }
        *coded_samples -= *coded_samples % 28;
        nb_samples      = (buf_size - header_size) * 2 / ch;
        nb_samples     -= nb_samples % 28;
        *approx_nb_samples = 1;
        break;
    case AV_CODEC_ID_ADPCM_IMA_DK3:
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        nb_samples = ((buf_size - 16) * 2 / 3 * 4) / ch;
        break;
    case AV_CODEC_ID_ADPCM_IMA_DK4:
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        if (buf_size < 4 * ch)
            return AVERROR_INVALIDDATA;
        nb_samples = 1 + (buf_size - 4 * ch) * 2 / ch;
        break;
    case AV_CODEC_ID_ADPCM_IMA_RAD:
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        nb_samples = (buf_size - 4 * ch) * 2 / ch;
        break;
    CASE(ADPCM_IMA_WAV,
        int bsize = ff_adpcm_ima_block_sizes[avctx->bits_per_coded_sample - 2];
        int bsamples = ff_adpcm_ima_block_samples[avctx->bits_per_coded_sample - 2];
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        if (buf_size < 4 * ch)
            return AVERROR_INVALIDDATA;
        nb_samples = 1 + (buf_size - 4 * ch) / (bsize * ch) * bsamples;
        ) /* End of CASE */
    case AV_CODEC_ID_ADPCM_MS:
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        nb_samples = (buf_size - 6 * ch) * 2 / ch;
        break;
    case AV_CODEC_ID_ADPCM_MTAF:
        if (avctx->block_align > 0)
            buf_size = FFMIN(buf_size, avctx->block_align);
        nb_samples = (buf_size - 16 * (ch / 2)) * 2 / ch;
        break;
    case AV_CODEC_ID_ADPCM_SBPRO_2:
    case AV_CODEC_ID_ADPCM_SBPRO_3:
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    {
        int samples_per_byte;
        switch (avctx->codec->id) {
        case AV_CODEC_ID_ADPCM_SBPRO_2: samples_per_byte = 4; break;
        case AV_CODEC_ID_ADPCM_SBPRO_3: samples_per_byte = 3; break;
        case AV_CODEC_ID_ADPCM_SBPRO_4: samples_per_byte = 2; break;
        }
        if (!s->status[0].step_index) {
            if (buf_size < ch)
                return AVERROR_INVALIDDATA;
            nb_samples++;
            buf_size -= ch;
        }
        nb_samples += buf_size * samples_per_byte / ch;
        break;
    }
    case AV_CODEC_ID_ADPCM_SWF:
    {
        int buf_bits       = buf_size * 8 - 2;
        int nbits          = (bytestream2_get_byte(gb) >> 6) + 2;
        int block_hdr_size = 22 * ch;
        int block_size     = block_hdr_size + nbits * ch * 4095;
        int nblocks        = buf_bits / block_size;
        int bits_left      = buf_bits - nblocks * block_size;
        nb_samples         = nblocks * 4096;
        if (bits_left >= block_hdr_size)
            nb_samples += 1 + (bits_left - block_hdr_size) / (nbits * ch);
        break;
    }
    case AV_CODEC_ID_ADPCM_THP:
    case AV_CODEC_ID_ADPCM_THP_LE:
        if (avctx->extradata) {
            nb_samples = buf_size * 14 / (8 * ch);
            break;
        }
        has_coded_samples = 1;
        bytestream2_skip(gb, 4); // channel size
        *coded_samples  = (avctx->codec->id == AV_CODEC_ID_ADPCM_THP_LE) ?
                          bytestream2_get_le32(gb) :
                          bytestream2_get_be32(gb);
        buf_size       -= 8 + 36 * ch;
        buf_size       /= ch;
        nb_samples      = buf_size / 8 * 14;
        if (buf_size % 8 > 1)
            nb_samples     += (buf_size % 8 - 1) * 2;
        *approx_nb_samples = 1;
        break;
    case AV_CODEC_ID_ADPCM_AFC:
        nb_samples = buf_size / (9 * ch) * 16;
        break;
    case AV_CODEC_ID_ADPCM_XA:
        nb_samples = (buf_size / 128) * 224 / ch;
        break;
    case AV_CODEC_ID_ADPCM_DTK:
    case AV_CODEC_ID_ADPCM_PSX:
        nb_samples = buf_size / (16 * ch) * 28;
        break;
    case AV_CODEC_ID_ADPCM_ARGO:
        nb_samples = buf_size / avctx->block_align * 32;
        break;
    case AV_CODEC_ID_ADPCM_ZORK:
        nb_samples = buf_size / ch;
        break;
    }

    /* validate coded sample count */
    if (has_coded_samples && (*coded_samples <= 0 || *coded_samples > nb_samples))
        return AVERROR_INVALIDDATA;

    return nb_samples;
}

static int adpcm_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ADPCMDecodeContext *c = avctx->priv_data;
    int channels = avctx->ch_layout.nb_channels;
    int16_t *samples;
    int16_t **samples_p;
    int st; /* stereo */
    int nb_samples, coded_samples, approx_nb_samples, ret;
    GetByteContext gb;

    bytestream2_init(&gb, buf, buf_size);
    nb_samples = get_nb_samples(avctx, &gb, buf_size, &coded_samples, &approx_nb_samples);
    if (nb_samples <= 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of samples in packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = nb_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t *)frame->data[0];
    samples_p = (int16_t **)frame->extended_data;

    /* use coded_samples when applicable */
    /* it is always <= nb_samples, so the output buffer will be large enough */
    if (coded_samples) {
        if (!approx_nb_samples && coded_samples != nb_samples)
            av_log(avctx, AV_LOG_WARNING, "mismatch in coded sample count\n");
        frame->nb_samples = nb_samples = coded_samples;
    }

    st = channels == 2 ? 1 : 0;

    switch(avctx->codec->id) {
    CASE(ADPCM_IMA_QT,
        /* In QuickTime, IMA is encoded by chunks of 34 bytes (=64 samples).
           Channel data is interleaved per-chunk. */
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            int predictor;
            int step_index;
            /* (pppppp) (piiiiiii) */

            /* Bits 15-7 are the _top_ 9 bits of the 16-bit initial predictor value */
            predictor = sign_extend(bytestream2_get_be16u(&gb), 16);
            step_index = predictor & 0x7F;
            predictor &= ~0x7F;

            if (cs->step_index == step_index) {
                int diff = predictor - cs->predictor;
                if (diff < 0)
                    diff = - diff;
                if (diff > 0x7f)
                    goto update;
            } else {
            update:
                cs->step_index = step_index;
                cs->predictor = predictor;
            }

            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }

            samples = samples_p[channel];

            for (int m = 0; m < 64; m += 2) {
                int byte = bytestream2_get_byteu(&gb);
                samples[m    ] = adpcm_ima_qt_expand_nibble(cs, byte & 0x0F);
                samples[m + 1] = adpcm_ima_qt_expand_nibble(cs, byte >> 4  );
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_WAV,
        for (int i = 0; i < channels; i++) {
            ADPCMChannelStatus *cs = &c->status[i];
            cs->predictor = samples_p[i][0] = sign_extend(bytestream2_get_le16u(&gb), 16);

            cs->step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       i, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }

        if (avctx->bits_per_coded_sample != 4) {
            int samples_per_block = ff_adpcm_ima_block_samples[avctx->bits_per_coded_sample - 2];
            int block_size = ff_adpcm_ima_block_sizes[avctx->bits_per_coded_sample - 2];
            uint8_t temp[20 + AV_INPUT_BUFFER_PADDING_SIZE] = { 0 };
            GetBitContext g;

            for (int n = 0; n < (nb_samples - 1) / samples_per_block; n++) {
                for (int i = 0; i < channels; i++) {
                    ADPCMChannelStatus *cs = &c->status[i];
                    samples = &samples_p[i][1 + n * samples_per_block];
                    for (int j = 0; j < block_size; j++) {
                        temp[j] = buf[4 * channels + block_size * n * channels +
                                        (j % 4) + (j / 4) * (channels * 4) + i * 4];
                    }
                    ret = init_get_bits8(&g, (const uint8_t *)&temp, block_size);
                    if (ret < 0)
                        return ret;
                    for (int m = 0; m < samples_per_block; m++) {
                        samples[m] = adpcm_ima_wav_expand_nibble(cs, &g,
                                          avctx->bits_per_coded_sample);
                    }
                }
            }
            bytestream2_skip(&gb, avctx->block_align - channels * 4);
        } else {
            for (int n = 0; n < (nb_samples - 1) / 8; n++) {
                for (int i = 0; i < channels; i++) {
                    ADPCMChannelStatus *cs = &c->status[i];
                    samples = &samples_p[i][1 + n * 8];
                    for (int m = 0; m < 8; m += 2) {
                        int v = bytestream2_get_byteu(&gb);
                        samples[m    ] = adpcm_ima_expand_nibble(cs, v & 0x0F, 3);
                        samples[m + 1] = adpcm_ima_expand_nibble(cs, v >> 4  , 3);
                    }
                }
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_4XM,
        for (int i = 0; i < channels; i++)
            c->status[i].predictor = sign_extend(bytestream2_get_le16u(&gb), 16);

        for (int i = 0; i < channels; i++) {
            c->status[i].step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (c->status[i].step_index > 88u) {
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       i, c->status[i].step_index);
                return AVERROR_INVALIDDATA;
            }
        }

        for (int i = 0; i < channels; i++) {
            ADPCMChannelStatus *cs = &c->status[i];
            samples = (int16_t *)frame->data[i];
            for (int n = nb_samples >> 1; n > 0; n--) {
                int v = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_ima_expand_nibble(cs, v & 0x0F, 4);
                *samples++ = adpcm_ima_expand_nibble(cs, v >> 4  , 4);
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_AGM,
        for (int i = 0; i < channels; i++)
            c->status[i].predictor = sign_extend(bytestream2_get_le16u(&gb), 16);
        for (int i = 0; i < channels; i++)
            c->status[i].step = sign_extend(bytestream2_get_le16u(&gb), 16);

        for (int n = 0; n < nb_samples >> (1 - st); n++) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_agm_expand_nibble(&c->status[0], v & 0xF);
            *samples++ = adpcm_agm_expand_nibble(&c->status[st], v >> 4 );
        }
        ) /* End of CASE */
    CASE(ADPCM_MS,
        int block_predictor;

        if (avctx->ch_layout.nb_channels > 2) {
            for (int channel = 0; channel < avctx->ch_layout.nb_channels; channel++) {
                samples = samples_p[channel];
                block_predictor = bytestream2_get_byteu(&gb);
                if (block_predictor > 6) {
                    av_log(avctx, AV_LOG_ERROR, "ERROR: block_predictor[%d] = %d\n",
                           channel, block_predictor);
                    return AVERROR_INVALIDDATA;
                }
                c->status[channel].coeff1 = ff_adpcm_AdaptCoeff1[block_predictor];
                c->status[channel].coeff2 = ff_adpcm_AdaptCoeff2[block_predictor];
                c->status[channel].idelta = sign_extend(bytestream2_get_le16u(&gb), 16);
                c->status[channel].sample1 = sign_extend(bytestream2_get_le16u(&gb), 16);
                c->status[channel].sample2 = sign_extend(bytestream2_get_le16u(&gb), 16);
                *samples++ = c->status[channel].sample2;
                *samples++ = c->status[channel].sample1;
                for (int n = (nb_samples - 2) >> 1; n > 0; n--) {
                    int byte = bytestream2_get_byteu(&gb);
                    *samples++ = adpcm_ms_expand_nibble(&c->status[channel], byte >> 4  );
                    *samples++ = adpcm_ms_expand_nibble(&c->status[channel], byte & 0x0F);
                }
            }
        } else {
            block_predictor = bytestream2_get_byteu(&gb);
            if (block_predictor > 6) {
                av_log(avctx, AV_LOG_ERROR, "ERROR: block_predictor[0] = %d\n",
                       block_predictor);
                return AVERROR_INVALIDDATA;
            }
            c->status[0].coeff1 = ff_adpcm_AdaptCoeff1[block_predictor];
            c->status[0].coeff2 = ff_adpcm_AdaptCoeff2[block_predictor];
            if (st) {
                block_predictor = bytestream2_get_byteu(&gb);
                if (block_predictor > 6) {
                    av_log(avctx, AV_LOG_ERROR, "ERROR: block_predictor[1] = %d\n",
                           block_predictor);
                    return AVERROR_INVALIDDATA;
                }
                c->status[1].coeff1 = ff_adpcm_AdaptCoeff1[block_predictor];
                c->status[1].coeff2 = ff_adpcm_AdaptCoeff2[block_predictor];
            }
            c->status[0].idelta = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (st){
                c->status[1].idelta = sign_extend(bytestream2_get_le16u(&gb), 16);
            }

            c->status[0].sample1 = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (st) c->status[1].sample1 = sign_extend(bytestream2_get_le16u(&gb), 16);
            c->status[0].sample2 = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (st) c->status[1].sample2 = sign_extend(bytestream2_get_le16u(&gb), 16);

            *samples++ = c->status[0].sample2;
            if (st) *samples++ = c->status[1].sample2;
            *samples++ = c->status[0].sample1;
            if (st) *samples++ = c->status[1].sample1;
            for (int n = (nb_samples - 2) >> (1 - st); n > 0; n--) {
                int byte = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_ms_expand_nibble(&c->status[0 ], byte >> 4  );
                *samples++ = adpcm_ms_expand_nibble(&c->status[st], byte & 0x0F);
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_MTAF,
        for (int channel = 0; channel < channels; channel += 2) {
            bytestream2_skipu(&gb, 4);
            c->status[channel    ].step      = bytestream2_get_le16u(&gb) & 0x1f;
            c->status[channel + 1].step      = bytestream2_get_le16u(&gb) & 0x1f;
            c->status[channel    ].predictor = sign_extend(bytestream2_get_le16u(&gb), 16);
            bytestream2_skipu(&gb, 2);
            c->status[channel + 1].predictor = sign_extend(bytestream2_get_le16u(&gb), 16);
            bytestream2_skipu(&gb, 2);
            for (int n = 0; n < nb_samples; n += 2) {
                int v = bytestream2_get_byteu(&gb);
                samples_p[channel][n    ] = adpcm_mtaf_expand_nibble(&c->status[channel], v & 0x0F);
                samples_p[channel][n + 1] = adpcm_mtaf_expand_nibble(&c->status[channel], v >> 4  );
            }
            for (int n = 0; n < nb_samples; n += 2) {
                int v = bytestream2_get_byteu(&gb);
                samples_p[channel + 1][n    ] = adpcm_mtaf_expand_nibble(&c->status[channel + 1], v & 0x0F);
                samples_p[channel + 1][n + 1] = adpcm_mtaf_expand_nibble(&c->status[channel + 1], v >> 4  );
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_DK4,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            cs->predictor  = *samples++ = sign_extend(bytestream2_get_le16u(&gb), 16);
            cs->step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }
        for (int n = (nb_samples - 1) >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0 ], v >> 4  , 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], v & 0x0F, 3);
        }
        ) /* End of CASE */

        /* DK3 ADPCM support macro */
#define DK3_GET_NEXT_NIBBLE() \
    if (decode_top_nibble_next) { \
        nibble = last_byte >> 4; \
        decode_top_nibble_next = 0; \
    } else { \
        last_byte = bytestream2_get_byteu(&gb); \
        nibble = last_byte & 0x0F; \
        decode_top_nibble_next = 1; \
    }
    CASE(ADPCM_IMA_DK3,
        int last_byte = 0;
        int nibble;
        int decode_top_nibble_next = 0;
        int diff_channel;
        const int16_t *samples_end = samples + channels * nb_samples;

        bytestream2_skipu(&gb, 10);
        c->status[0].predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
        c->status[1].predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
        c->status[0].step_index = bytestream2_get_byteu(&gb);
        c->status[1].step_index = bytestream2_get_byteu(&gb);
        if (c->status[0].step_index > 88u || c->status[1].step_index > 88u){
            av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i/%i\n",
                   c->status[0].step_index, c->status[1].step_index);
            return AVERROR_INVALIDDATA;
        }
        /* sign extend the predictors */
        diff_channel = c->status[1].predictor;

        while (samples < samples_end) {

            /* for this algorithm, c->status[0] is the sum channel and
             * c->status[1] is the diff channel */

            /* process the first predictor of the sum channel */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[0], nibble, 3);

            /* process the diff channel predictor */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[1], nibble, 3);

            /* process the first pair of stereo PCM samples */
            diff_channel = (diff_channel + c->status[1].predictor) / 2;
            *samples++ = c->status[0].predictor + c->status[1].predictor;
            *samples++ = c->status[0].predictor - c->status[1].predictor;

            /* process the second predictor of the sum channel */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[0], nibble, 3);

            /* process the second pair of stereo PCM samples */
            diff_channel = (diff_channel + c->status[1].predictor) / 2;
            *samples++ = c->status[0].predictor + c->status[1].predictor;
            *samples++ = c->status[0].predictor - c->status[1].predictor;
        }

        if ((bytestream2_tell(&gb) & 1))
            bytestream2_skip(&gb, 1);
        ) /* End of CASE */
    CASE(ADPCM_IMA_ISS,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            cs->predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
            cs->step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }

        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v1, v2;
            int v = bytestream2_get_byteu(&gb);
            /* nibbles are swapped for mono */
            if (st) {
                v1 = v >> 4;
                v2 = v & 0x0F;
            } else {
                v2 = v >> 4;
                v1 = v & 0x0F;
            }
            *samples++ = adpcm_ima_expand_nibble(&c->status[0 ], v1, 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], v2, 3);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_MOFLEX,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            cs->step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            cs->predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }

        for (int subframe = 0; subframe < nb_samples / 256; subframe++) {
            for (int channel = 0; channel < channels; channel++) {
                samples = samples_p[channel] + 256 * subframe;
                for (int n = 0; n < 256; n += 2) {
                    int v = bytestream2_get_byteu(&gb);
                    *samples++ = adpcm_ima_expand_nibble(&c->status[channel], v & 0x0F, 3);
                    *samples++ = adpcm_ima_expand_nibble(&c->status[channel], v >> 4  , 3);
                }
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_DAT4,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            samples = samples_p[channel];
            bytestream2_skip(&gb, 4);
            for (int n = 0; n < nb_samples; n += 2) {
                int v = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_ima_expand_nibble(cs, v >> 4  , 3);
                *samples++ = adpcm_ima_expand_nibble(cs, v & 0x0F, 3);
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_APC,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],  v >> 4  , 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], v & 0x0F, 3);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_SSI,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_qt_expand_nibble(&c->status[0],  v >> 4  );
            *samples++ = adpcm_ima_qt_expand_nibble(&c->status[st], v & 0x0F);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_APM,
        for (int n = nb_samples / 2; n > 0; n--) {
            for (int channel = 0; channel < channels; channel++) {
                int v = bytestream2_get_byteu(&gb);
                *samples++  = adpcm_ima_qt_expand_nibble(&c->status[channel], v >> 4  );
                samples[st] = adpcm_ima_qt_expand_nibble(&c->status[channel], v & 0x0F);
            }
            samples += channels;
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_ALP,
        for (int n = nb_samples / 2; n > 0; n--) {
            for (int channel = 0; channel < channels; channel++) {
                int v = bytestream2_get_byteu(&gb);
                *samples++  = adpcm_ima_alp_expand_nibble(&c->status[channel], v >> 4  , 2);
                samples[st] = adpcm_ima_alp_expand_nibble(&c->status[channel], v & 0x0F, 2);
            }
            samples += channels;
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_CUNNING,
        for (int channel = 0; channel < channels; channel++) {
            int16_t *smp = samples_p[channel];
            for (int n = 0; n < nb_samples / 2; n++) {
                int v = bytestream2_get_byteu(&gb);
                *smp++ = adpcm_ima_cunning_expand_nibble(&c->status[channel], v & 0x0F);
                *smp++ = adpcm_ima_cunning_expand_nibble(&c->status[channel], v >> 4);
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_OKI,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_oki_expand_nibble(&c->status[0],  v >> 4  );
            *samples++ = adpcm_ima_oki_expand_nibble(&c->status[st], v & 0x0F);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_RAD,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            cs->step_index = sign_extend(bytestream2_get_le16u(&gb), 16);
            cs->predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }
        for (int n = 0; n < nb_samples / 2; n++) {
            int byte[2];

            byte[0] = bytestream2_get_byteu(&gb);
            if (st)
                byte[1] = bytestream2_get_byteu(&gb);
            for (int channel = 0; channel < channels; channel++) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[channel], byte[channel] & 0x0F, 3);
            }
            for (int channel = 0; channel < channels; channel++) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[channel], byte[channel] >> 4  , 3);
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_WS,
        if (c->vqa_version == 3) {
            for (int channel = 0; channel < channels; channel++) {
                int16_t *smp = samples_p[channel];

                for (int n = nb_samples / 2; n > 0; n--) {
                    int v = bytestream2_get_byteu(&gb);
                    *smp++ = adpcm_ima_expand_nibble(&c->status[channel], v & 0x0F, 3);
                    *smp++ = adpcm_ima_expand_nibble(&c->status[channel], v >> 4  , 3);
                }
            }
        } else {
            for (int n = nb_samples / 2; n > 0; n--) {
                for (int channel = 0; channel < channels; channel++) {
                    int v = bytestream2_get_byteu(&gb);
                    *samples++  = adpcm_ima_expand_nibble(&c->status[channel], v & 0x0F, 3);
                    samples[st] = adpcm_ima_expand_nibble(&c->status[channel], v >> 4  , 3);
                }
                samples += channels;
            }
        }
        bytestream2_seek(&gb, 0, SEEK_END);
        ) /* End of CASE */
    CASE(ADPCM_XA,
        int16_t *out0 = samples_p[0];
        int16_t *out1 = samples_p[1];
        int samples_per_block = 28 * (3 - channels) * 4;
        int sample_offset = 0;
        int bytes_remaining;
        while (bytestream2_get_bytes_left(&gb) >= 128) {
            if ((ret = xa_decode(avctx, out0, out1, buf + bytestream2_tell(&gb),
                                 &c->status[0], &c->status[1],
                                 channels, sample_offset)) < 0)
                return ret;
            bytestream2_skipu(&gb, 128);
            sample_offset += samples_per_block;
        }
        /* Less than a full block of data left, e.g. when reading from
         * 2324 byte per sector XA; the remainder is padding */
        bytes_remaining = bytestream2_get_bytes_left(&gb);
        if (bytes_remaining > 0) {
            bytestream2_skip(&gb, bytes_remaining);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_EA_EACS,
        for (int i = 0; i <= st; i++) {
            c->status[i].step_index = bytestream2_get_le32u(&gb);
            if (c->status[i].step_index > 88u) {
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       i, c->status[i].step_index);
                return AVERROR_INVALIDDATA;
            }
        }
        for (int i = 0; i <= st; i++) {
            c->status[i].predictor  = bytestream2_get_le32u(&gb);
            if (FFABS((int64_t)c->status[i].predictor) > (1<<16))
                return AVERROR_INVALIDDATA;
        }

        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int byte   = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],  byte >> 4,   3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], byte & 0x0F, 3);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_EA_SEAD,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int byte = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],  byte >> 4,   6);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], byte & 0x0F, 6);
        }
        ) /* End of CASE */
    CASE(ADPCM_EA,
        int previous_left_sample, previous_right_sample;
        int current_left_sample, current_right_sample;
        int next_left_sample, next_right_sample;
        int coeff1l, coeff2l, coeff1r, coeff2r;
        int shift_left, shift_right;

        /* Each EA ADPCM frame has a 12-byte header followed by 30-byte pieces,
           each coding 28 stereo samples. */

        if (channels != 2)
            return AVERROR_INVALIDDATA;

        current_left_sample   = sign_extend(bytestream2_get_le16u(&gb), 16);
        previous_left_sample  = sign_extend(bytestream2_get_le16u(&gb), 16);
        current_right_sample  = sign_extend(bytestream2_get_le16u(&gb), 16);
        previous_right_sample = sign_extend(bytestream2_get_le16u(&gb), 16);

        for (int count1 = 0; count1 < nb_samples / 28; count1++) {
            int byte = bytestream2_get_byteu(&gb);
            coeff1l = ea_adpcm_table[ byte >> 4       ];
            coeff2l = ea_adpcm_table[(byte >> 4  ) + 4];
            coeff1r = ea_adpcm_table[ byte & 0x0F];
            coeff2r = ea_adpcm_table[(byte & 0x0F) + 4];

            byte = bytestream2_get_byteu(&gb);
            shift_left  = 20 - (byte >> 4);
            shift_right = 20 - (byte & 0x0F);

            for (int count2 = 0; count2 < 28; count2++) {
                byte = bytestream2_get_byteu(&gb);
                next_left_sample  = sign_extend(byte >> 4, 4) * (1 << shift_left);
                next_right_sample = sign_extend(byte,      4) * (1 << shift_right);

                next_left_sample = (next_left_sample +
                    (current_left_sample * coeff1l) +
                    (previous_left_sample * coeff2l) + 0x80) >> 8;
                next_right_sample = (next_right_sample +
                    (current_right_sample * coeff1r) +
                    (previous_right_sample * coeff2r) + 0x80) >> 8;

                previous_left_sample = current_left_sample;
                current_left_sample = av_clip_int16(next_left_sample);
                previous_right_sample = current_right_sample;
                current_right_sample = av_clip_int16(next_right_sample);
                *samples++ = current_left_sample;
                *samples++ = current_right_sample;
            }
        }

        bytestream2_skip(&gb, 2); // Skip terminating 0x0000
        ) /* End of CASE */
    CASE(ADPCM_EA_MAXIS_XA,
        int coeff[2][2], shift[2];

        for (int channel = 0; channel < channels; channel++) {
            int byte = bytestream2_get_byteu(&gb);
            for (int i = 0; i < 2; i++)
                coeff[channel][i] = ea_adpcm_table[(byte >> 4) + 4*i];
            shift[channel] = 20 - (byte & 0x0F);
        }
        for (int count1 = 0; count1 < nb_samples / 2; count1++) {
            int byte[2];

            byte[0] = bytestream2_get_byteu(&gb);
            if (st) byte[1] = bytestream2_get_byteu(&gb);
            for (int i = 4; i >= 0; i-=4) { /* Pairwise samples LL RR (st) or LL LL (mono) */
                for (int channel = 0; channel < channels; channel++) {
                    int sample = sign_extend(byte[channel] >> i, 4) * (1 << shift[channel]);
                    sample = (sample +
                             c->status[channel].sample1 * coeff[channel][0] +
                             c->status[channel].sample2 * coeff[channel][1] + 0x80) >> 8;
                    c->status[channel].sample2 = c->status[channel].sample1;
                    c->status[channel].sample1 = av_clip_int16(sample);
                    *samples++ = c->status[channel].sample1;
                }
            }
        }
        bytestream2_seek(&gb, 0, SEEK_END);
        ) /* End of CASE */
#if CONFIG_ADPCM_EA_R1_DECODER || CONFIG_ADPCM_EA_R2_DECODER || CONFIG_ADPCM_EA_R3_DECODER
    case AV_CODEC_ID_ADPCM_EA_R1:
    case AV_CODEC_ID_ADPCM_EA_R2:
    case AV_CODEC_ID_ADPCM_EA_R3: {
        /* channel numbering
           2chan: 0=fl, 1=fr
           4chan: 0=fl, 1=rl, 2=fr, 3=rr
           6chan: 0=fl, 1=c,  2=fr, 3=rl,  4=rr, 5=sub */
        const int big_endian = avctx->codec->id == AV_CODEC_ID_ADPCM_EA_R3;
        int previous_sample, current_sample, next_sample;
        int coeff1, coeff2;
        int shift;
        uint16_t *samplesC;
        int count = 0;
        int offsets[6];

        for (unsigned channel = 0; channel < channels; channel++)
            offsets[channel] = (big_endian ? bytestream2_get_be32(&gb) :
                                             bytestream2_get_le32(&gb)) +
                               (channels + 1) * 4;

        for (unsigned channel = 0; channel < channels; channel++) {
            int count1;

            bytestream2_seek(&gb, offsets[channel], SEEK_SET);
            samplesC = samples_p[channel];

            if (avctx->codec->id == AV_CODEC_ID_ADPCM_EA_R1) {
                current_sample  = sign_extend(bytestream2_get_le16(&gb), 16);
                previous_sample = sign_extend(bytestream2_get_le16(&gb), 16);
            } else {
                current_sample  = c->status[channel].predictor;
                previous_sample = c->status[channel].prev_sample;
            }

            for (count1 = 0; count1 < nb_samples / 28; count1++) {
                int byte = bytestream2_get_byte(&gb);
                if (byte == 0xEE) {  /* only seen in R2 and R3 */
                    current_sample  = sign_extend(bytestream2_get_be16(&gb), 16);
                    previous_sample = sign_extend(bytestream2_get_be16(&gb), 16);

                    for (int count2 = 0; count2 < 28; count2++)
                        *samplesC++ = sign_extend(bytestream2_get_be16(&gb), 16);
                } else {
                    coeff1 = ea_adpcm_table[ byte >> 4     ];
                    coeff2 = ea_adpcm_table[(byte >> 4) + 4];
                    shift = 20 - (byte & 0x0F);

                    for (int count2 = 0; count2 < 28; count2++) {
                        if (count2 & 1)
                            next_sample = (unsigned)sign_extend(byte,    4) << shift;
                        else {
                            byte = bytestream2_get_byte(&gb);
                            next_sample = (unsigned)sign_extend(byte >> 4, 4) << shift;
                        }

                        next_sample += (current_sample  * coeff1) +
                                       (previous_sample * coeff2);
                        next_sample = av_clip_int16(next_sample >> 8);

                        previous_sample = current_sample;
                        current_sample  = next_sample;
                        *samplesC++ = current_sample;
                    }
                }
            }
            if (!count) {
                count = count1;
            } else if (count != count1) {
                av_log(avctx, AV_LOG_WARNING, "per-channel sample count mismatch\n");
                count = FFMAX(count, count1);
            }

            if (avctx->codec->id != AV_CODEC_ID_ADPCM_EA_R1) {
                c->status[channel].predictor   = current_sample;
                c->status[channel].prev_sample = previous_sample;
            }
        }

        frame->nb_samples = count * 28;
        bytestream2_seek(&gb, 0, SEEK_END);
        break;
    }
#endif /* CONFIG_ADPCM_EA_Rx_DECODER */
    CASE(ADPCM_EA_XAS,
        for (int channel=0; channel < channels; channel++) {
            int coeff[2][4], shift[4];
            int16_t *s = samples_p[channel];
            for (int n = 0; n < 4; n++, s += 32) {
                int val = sign_extend(bytestream2_get_le16u(&gb), 16);
                for (int i = 0; i < 2; i++)
                    coeff[i][n] = ea_adpcm_table[(val&0x0F)+4*i];
                s[0] = val & ~0x0F;

                val = sign_extend(bytestream2_get_le16u(&gb), 16);
                shift[n] = 20 - (val & 0x0F);
                s[1] = val & ~0x0F;
            }

            for (int m = 2; m < 32; m += 2) {
                s = &samples_p[channel][m];
                for (int n = 0; n < 4; n++, s += 32) {
                    int level, pred;
                    int byte = bytestream2_get_byteu(&gb);

                    level = sign_extend(byte >> 4, 4) * (1 << shift[n]);
                    pred  = s[-1] * coeff[0][n] + s[-2] * coeff[1][n];
                    s[0]  = av_clip_int16((level + pred + 0x80) >> 8);

                    level = sign_extend(byte, 4) * (1 << shift[n]);
                    pred  = s[0] * coeff[0][n] + s[-1] * coeff[1][n];
                    s[1]  = av_clip_int16((level + pred + 0x80) >> 8);
                }
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_ACORN,
        for (int channel = 0; channel < channels; channel++) {
            ADPCMChannelStatus *cs = &c->status[channel];
            cs->predictor  = sign_extend(bytestream2_get_le16u(&gb), 16);
            cs->step_index = bytestream2_get_le16u(&gb) & 0xFF;
            if (cs->step_index > 88u){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index[%d] = %i\n",
                       channel, cs->step_index);
                return AVERROR_INVALIDDATA;
            }
        }
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int byte = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],  byte & 0x0F, 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], byte >> 4,   3);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_AMV,
        av_assert0(channels == 1);

        /*
         * Header format:
         *   int16_t  predictor;
         *   uint8_t  step_index;
         *   uint8_t  reserved;
         *   uint32_t frame_size;
         *
         * Some implementations have step_index as 16-bits, but others
         * only use the lower 8 and store garbage in the upper 8.
         */
        c->status[0].predictor = sign_extend(bytestream2_get_le16u(&gb), 16);
        c->status[0].step_index = bytestream2_get_byteu(&gb);
        bytestream2_skipu(&gb, 5);
        if (c->status[0].step_index > 88u) {
            av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i\n",
                   c->status[0].step_index);
            return AVERROR_INVALIDDATA;
        }

        for (int n = nb_samples >> 1; n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);

            *samples++ = adpcm_ima_expand_nibble(&c->status[0], v >> 4, 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], v & 0xf, 3);
        }

        if (nb_samples & 1) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], v >> 4, 3);

            if (v & 0x0F) {
                /* Holds true on all the http://samples.mplayerhq.hu/amv samples. */
                av_log(avctx, AV_LOG_WARNING, "Last nibble set on packet with odd sample count.\n");
                av_log(avctx, AV_LOG_WARNING, "Sample will be skipped.\n");
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_SMJPEG,
        for (int i = 0; i < channels; i++) {
            c->status[i].predictor = sign_extend(bytestream2_get_be16u(&gb), 16);
            c->status[i].step_index = bytestream2_get_byteu(&gb);
            bytestream2_skipu(&gb, 1);
            if (c->status[i].step_index > 88u) {
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i\n",
                       c->status[i].step_index);
                return AVERROR_INVALIDDATA;
            }
        }

        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);

            *samples++ = adpcm_ima_qt_expand_nibble(&c->status[0 ], v >> 4 );
            *samples++ = adpcm_ima_qt_expand_nibble(&c->status[st], v & 0xf);
        }
        ) /* End of CASE */
    CASE(ADPCM_CT,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_ct_expand_nibble(&c->status[0 ], v >> 4  );
            *samples++ = adpcm_ct_expand_nibble(&c->status[st], v & 0x0F);
        }
        ) /* End of CASE */
#if CONFIG_ADPCM_SBPRO_2_DECODER || CONFIG_ADPCM_SBPRO_3_DECODER || \
    CONFIG_ADPCM_SBPRO_4_DECODER
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    case AV_CODEC_ID_ADPCM_SBPRO_3:
    case AV_CODEC_ID_ADPCM_SBPRO_2:
        if (!c->status[0].step_index) {
            /* the first byte is a raw sample */
            *samples++ = 128 * (bytestream2_get_byteu(&gb) - 0x80);
            if (st)
                *samples++ = 128 * (bytestream2_get_byteu(&gb) - 0x80);
            c->status[0].step_index = 1;
            nb_samples--;
        }
        if (avctx->codec->id == AV_CODEC_ID_ADPCM_SBPRO_4) {
            for (int n = nb_samples >> (1 - st); n > 0; n--) {
                int byte = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                       byte >> 4,   4, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                                                       byte & 0x0F, 4, 0);
            }
        } else if (avctx->codec->id == AV_CODEC_ID_ADPCM_SBPRO_3) {
            for (int n = (nb_samples<<st) / 3; n > 0; n--) {
                int byte = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                        byte >> 5        , 3, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                       (byte >> 2) & 0x07, 3, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                        byte & 0x03,       2, 0);
            }
        } else {
            for (int n = nb_samples >> (2 - st); n > 0; n--) {
                int byte = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                        byte >> 6        , 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                                                       (byte >> 4) & 0x03, 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                                                       (byte >> 2) & 0x03, 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                                                        byte & 0x03,       2, 2);
            }
        }
        break;
#endif /* CONFIG_ADPCM_SBPRO_x_DECODER */
    CASE(ADPCM_SWF,
        adpcm_swf_decode(avctx, buf, buf_size, samples);
        bytestream2_seek(&gb, 0, SEEK_END);
        ) /* End of CASE */
    CASE(ADPCM_YAMAHA,
        for (int n = nb_samples >> (1 - st); n > 0; n--) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_yamaha_expand_nibble(&c->status[0 ], v & 0x0F);
            *samples++ = adpcm_yamaha_expand_nibble(&c->status[st], v >> 4  );
        }
        ) /* End of CASE */
    CASE(ADPCM_AICA,
        for (int channel = 0; channel < channels; channel++) {
            samples = samples_p[channel];
            for (int n = nb_samples >> 1; n > 0; n--) {
                int v = bytestream2_get_byteu(&gb);
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[channel], v & 0x0F);
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[channel], v >> 4  );
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_AFC,
        int samples_per_block;
        int blocks;

        if (avctx->extradata && avctx->extradata_size == 1 && avctx->extradata[0]) {
            samples_per_block = avctx->extradata[0] / 16;
            blocks = nb_samples / avctx->extradata[0];
        } else {
            samples_per_block = nb_samples / 16;
            blocks = 1;
        }

        for (int m = 0; m < blocks; m++) {
            for (int channel = 0; channel < channels; channel++) {
                int prev1 = c->status[channel].sample1;
                int prev2 = c->status[channel].sample2;

                samples = samples_p[channel] + m * 16;
                /* Read in every sample for this channel.  */
                for (int i = 0; i < samples_per_block; i++) {
                    int byte = bytestream2_get_byteu(&gb);
                    int scale = 1 << (byte >> 4);
                    int index = byte & 0xf;
                    int factor1 = afc_coeffs[0][index];
                    int factor2 = afc_coeffs[1][index];

                    /* Decode 16 samples.  */
                    for (int n = 0; n < 16; n++) {
                        int32_t sampledat;

                        if (n & 1) {
                            sampledat = sign_extend(byte, 4);
                        } else {
                            byte = bytestream2_get_byteu(&gb);
                            sampledat = sign_extend(byte >> 4, 4);
                        }

                        sampledat = ((prev1 * factor1 + prev2 * factor2) >> 11) +
                                    sampledat * scale;
                        *samples = av_clip_int16(sampledat);
                        prev2 = prev1;
                        prev1 = *samples++;
                    }
                }

                c->status[channel].sample1 = prev1;
                c->status[channel].sample2 = prev2;
            }
        }
        bytestream2_seek(&gb, 0, SEEK_END);
        ) /* End of CASE */
#if CONFIG_ADPCM_THP_DECODER || CONFIG_ADPCM_THP_LE_DECODER
    case AV_CODEC_ID_ADPCM_THP:
    case AV_CODEC_ID_ADPCM_THP_LE:
    {
        int table[14][16];

#define THP_GET16(g) \
    sign_extend( \
        avctx->codec->id == AV_CODEC_ID_ADPCM_THP_LE ? \
        bytestream2_get_le16u(&(g)) : \
        bytestream2_get_be16u(&(g)), 16)

        if (avctx->extradata) {
            GetByteContext tb;
            if (avctx->extradata_size < 32 * channels) {
                av_log(avctx, AV_LOG_ERROR, "Missing coeff table\n");
                return AVERROR_INVALIDDATA;
            }

            bytestream2_init(&tb, avctx->extradata, avctx->extradata_size);
            for (int i = 0; i < channels; i++)
                for (int n = 0; n < 16; n++)
                    table[i][n] = THP_GET16(tb);
        } else {
            for (int i = 0; i < channels; i++)
                for (int n = 0; n < 16; n++)
                    table[i][n] = THP_GET16(gb);

            if (!c->has_status) {
                /* Initialize the previous sample.  */
                for (int i = 0; i < channels; i++) {
                    c->status[i].sample1 = THP_GET16(gb);
                    c->status[i].sample2 = THP_GET16(gb);
                }
                c->has_status = 1;
            } else {
                bytestream2_skip(&gb, channels * 4);
            }
        }

        for (int ch = 0; ch < channels; ch++) {
            samples = samples_p[ch];

            /* Read in every sample for this channel.  */
            for (int i = 0; i < (nb_samples + 13) / 14; i++) {
                int byte = bytestream2_get_byteu(&gb);
                int index = (byte >> 4) & 7;
                unsigned int exp = byte & 0x0F;
                int64_t factor1 = table[ch][index * 2];
                int64_t factor2 = table[ch][index * 2 + 1];

                /* Decode 14 samples.  */
                for (int n = 0; n < 14 && (i * 14 + n < nb_samples); n++) {
                    int32_t sampledat;

                    if (n & 1) {
                        sampledat = sign_extend(byte, 4);
                    } else {
                        byte = bytestream2_get_byteu(&gb);
                        sampledat = sign_extend(byte >> 4, 4);
                    }

                    sampledat = ((c->status[ch].sample1 * factor1
                                + c->status[ch].sample2 * factor2) >> 11) + sampledat * (1 << exp);
                    *samples = av_clip_int16(sampledat);
                    c->status[ch].sample2 = c->status[ch].sample1;
                    c->status[ch].sample1 = *samples++;
                }
            }
        }
        break;
    }
#endif /* CONFIG_ADPCM_THP(_LE)_DECODER */
    CASE(ADPCM_DTK,
        for (int channel = 0; channel < channels; channel++) {
            samples = samples_p[channel];

            /* Read in every sample for this channel.  */
            for (int i = 0; i < nb_samples / 28; i++) {
                int byte, header;
                if (channel)
                    bytestream2_skipu(&gb, 1);
                header = bytestream2_get_byteu(&gb);
                bytestream2_skipu(&gb, 3 - channel);

                /* Decode 28 samples.  */
                for (int n = 0; n < 28; n++) {
                    int32_t sampledat, prev;

                    switch (header >> 4) {
                    case 1:
                        prev = (c->status[channel].sample1 * 0x3c);
                        break;
                    case 2:
                        prev = (c->status[channel].sample1 * 0x73) - (c->status[channel].sample2 * 0x34);
                        break;
                    case 3:
                        prev = (c->status[channel].sample1 * 0x62) - (c->status[channel].sample2 * 0x37);
                        break;
                    default:
                        prev = 0;
                    }

                    prev = av_clip_intp2((prev + 0x20) >> 6, 21);

                    byte = bytestream2_get_byteu(&gb);
                    if (!channel)
                        sampledat = sign_extend(byte, 4);
                    else
                        sampledat = sign_extend(byte >> 4, 4);

                    sampledat = ((sampledat * (1 << 12)) >> (header & 0xf)) * (1 << 6) + prev;
                    *samples++ = av_clip_int16(sampledat >> 6);
                    c->status[channel].sample2 = c->status[channel].sample1;
                    c->status[channel].sample1 = sampledat;
                }
            }
            if (!channel)
                bytestream2_seek(&gb, 0, SEEK_SET);
        }
        ) /* End of CASE */
    CASE(ADPCM_PSX,
        for (int block = 0; block < avpkt->size / FFMAX(avctx->block_align, 16 * channels); block++) {
            int nb_samples_per_block = 28 * FFMAX(avctx->block_align, 16 * channels) / (16 * channels);
            for (int channel = 0; channel < channels; channel++) {
                samples = samples_p[channel] + block * nb_samples_per_block;
                av_assert0((block + 1) * nb_samples_per_block <= nb_samples);

                /* Read in every sample for this channel.  */
                for (int i = 0; i < nb_samples_per_block / 28; i++) {
                    int filter, shift, flag, byte;

                    filter = bytestream2_get_byteu(&gb);
                    shift  = filter & 0xf;
                    filter = filter >> 4;
                    if (filter >= FF_ARRAY_ELEMS(xa_adpcm_table))
                        return AVERROR_INVALIDDATA;
                    flag   = bytestream2_get_byteu(&gb) & 0x7;

                    /* Decode 28 samples.  */
                    for (int n = 0; n < 28; n++) {
                        int sample = 0, scale;

                        if (n & 1) {
                            scale = sign_extend(byte >> 4, 4);
                        } else {
                            byte  = bytestream2_get_byteu(&gb);
                            scale = sign_extend(byte, 4);
                        }

                        if (flag < 0x07) {
                            scale  = scale * (1 << 12);
                            sample = (int)((scale >> shift) + (c->status[channel].sample1 * xa_adpcm_table[filter][0] + c->status[channel].sample2 * xa_adpcm_table[filter][1]) / 64);
                        }
                        *samples++ = av_clip_int16(sample);
                        c->status[channel].sample2 = c->status[channel].sample1;
                        c->status[channel].sample1 = sample;
                    }
                }
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_ARGO,
        /*
         * The format of each block:
         *   uint8_t left_control;
         *   uint4_t left_samples[nb_samples];
         *   ---- and if stereo ----
         *   uint8_t right_control;
         *   uint4_t right_samples[nb_samples];
         *
         * Format of the control byte:
         * MSB [SSSSRDRR] LSB
         *   S = (Shift Amount - 2)
         *   D = Decoder flag.
         *   R = Reserved
         *
         * Each block relies on the previous two samples of each channel.
         * They should be 0 initially.
         */
        for (int block = 0; block < avpkt->size / avctx->block_align; block++) {
            for (int channel = 0; channel < avctx->ch_layout.nb_channels; channel++) {
                ADPCMChannelStatus *cs = c->status + channel;
                int control, shift;

                samples = samples_p[channel] + block * 32;

                /* Get the control byte and decode the samples, 2 at a time. */
                control = bytestream2_get_byteu(&gb);
                shift = (control >> 4) + 2;

                for (int n = 0; n < 16; n++) {
                    int sample = bytestream2_get_byteu(&gb);
                    *samples++ = ff_adpcm_argo_expand_nibble(cs, sample >> 4, shift, control & 0x04);
                    *samples++ = ff_adpcm_argo_expand_nibble(cs, sample >> 0, shift, control & 0x04);
                }
            }
        }
        ) /* End of CASE */
    CASE(ADPCM_ZORK,
        for (int n = 0; n < nb_samples * channels; n++) {
            int v = bytestream2_get_byteu(&gb);
            *samples++ = adpcm_zork_expand_nibble(&c->status[n % channels], v);
        }
        ) /* End of CASE */
    CASE(ADPCM_IMA_MTF,
        for (int n = nb_samples / 2; n > 0; n--) {
            for (int channel = 0; channel < channels; channel++) {
                int v = bytestream2_get_byteu(&gb);
                *samples++  = adpcm_ima_mtf_expand_nibble(&c->status[channel], v >> 4);
                samples[st] = adpcm_ima_mtf_expand_nibble(&c->status[channel], v & 0x0F);
            }
            samples += channels;
        }
        ) /* End of CASE */
    default:
        av_assert0(0); // unsupported codec_id should not happen
    }

    if (avpkt->size && bytestream2_tell(&gb) == 0) {
        av_log(avctx, AV_LOG_ERROR, "Nothing consumed\n");
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    if (avpkt->size < bytestream2_tell(&gb)) {
        av_log(avctx, AV_LOG_ERROR, "Overread of %d < %d\n", avpkt->size, bytestream2_tell(&gb));
        return avpkt->size;
    }

    return bytestream2_tell(&gb);
}

static void adpcm_flush(AVCodecContext *avctx)
{
    ADPCMDecodeContext *c = avctx->priv_data;

    /* Just nuke the entire state and re-init. */
    memset(c, 0, sizeof(ADPCMDecodeContext));

    switch(avctx->codec_id) {
    case AV_CODEC_ID_ADPCM_CT:
        c->status[0].step = c->status[1].step = 511;
        break;

    case AV_CODEC_ID_ADPCM_IMA_APC:
        if (avctx->extradata && avctx->extradata_size >= 8) {
            c->status[0].predictor = av_clip_intp2(AV_RL32(avctx->extradata    ), 18);
            c->status[1].predictor = av_clip_intp2(AV_RL32(avctx->extradata + 4), 18);
        }
        break;

    case AV_CODEC_ID_ADPCM_IMA_APM:
        if (avctx->extradata && avctx->extradata_size >= 28) {
            c->status[0].predictor  = av_clip_intp2(AV_RL32(avctx->extradata + 16), 18);
            c->status[0].step_index = av_clip(AV_RL32(avctx->extradata + 20), 0, 88);
            c->status[1].predictor  = av_clip_intp2(AV_RL32(avctx->extradata + 4), 18);
            c->status[1].step_index = av_clip(AV_RL32(avctx->extradata + 8), 0, 88);
        }
        break;

    case AV_CODEC_ID_ADPCM_IMA_WS:
        if (avctx->extradata && avctx->extradata_size >= 2)
            c->vqa_version = AV_RL16(avctx->extradata);
        break;
    default:
        /* Other codecs may want to handle this during decoding. */
        c->has_status = 0;
        return;
    }

    c->has_status = 1;
}


static const enum AVSampleFormat sample_fmts_s16[]  = { AV_SAMPLE_FMT_S16,
                                                        AV_SAMPLE_FMT_NONE };
static const enum AVSampleFormat sample_fmts_s16p[] = { AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_NONE };
static const enum AVSampleFormat sample_fmts_both[] = { AV_SAMPLE_FMT_S16,
                                                        AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_NONE };

#define ADPCM_DECODER_0(id_, sample_fmts_, name_, long_name_)
#define ADPCM_DECODER_1(id_, sample_fmts_, name_, long_name_) \
const FFCodec ff_ ## name_ ## _decoder = {                  \
    .p.name         = #name_,                               \
    .p.long_name    = NULL_IF_CONFIG_SMALL(long_name_),     \
    .p.type         = AVMEDIA_TYPE_AUDIO,                   \
    .p.id           = id_,                                  \
    .p.capabilities = AV_CODEC_CAP_DR1,                     \
    .p.sample_fmts  = sample_fmts_,                         \
    .priv_data_size = sizeof(ADPCMDecodeContext),           \
    .init           = adpcm_decode_init,                    \
    FF_CODEC_DECODE_CB(adpcm_decode_frame),                 \
    .flush          = adpcm_flush,                          \
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,         \
};
#define ADPCM_DECODER_2(enabled, codec_id, name, sample_fmts, long_name) \
    ADPCM_DECODER_ ## enabled(codec_id, name, sample_fmts, long_name)
#define ADPCM_DECODER_3(config, codec_id, name, sample_fmts, long_name) \
    ADPCM_DECODER_2(config, codec_id, name, sample_fmts, long_name)
#define ADPCM_DECODER(codec, name, sample_fmts, long_name) \
    ADPCM_DECODER_3(CONFIG_ ## codec ## _DECODER, AV_CODEC_ID_ ## codec, \
                    name, sample_fmts, long_name)

/* Note: Do not forget to add new entries to the Makefile as well. */
ADPCM_DECODER(ADPCM_4XM,         sample_fmts_s16p, adpcm_4xm,         "ADPCM 4X Movie")
ADPCM_DECODER(ADPCM_AFC,         sample_fmts_s16p, adpcm_afc,         "ADPCM Nintendo Gamecube AFC")
ADPCM_DECODER(ADPCM_AGM,         sample_fmts_s16,  adpcm_agm,         "ADPCM AmuseGraphics Movie")
ADPCM_DECODER(ADPCM_AICA,        sample_fmts_s16p, adpcm_aica,        "ADPCM Yamaha AICA")
ADPCM_DECODER(ADPCM_ARGO,        sample_fmts_s16p, adpcm_argo,        "ADPCM Argonaut Games")
ADPCM_DECODER(ADPCM_CT,          sample_fmts_s16,  adpcm_ct,          "ADPCM Creative Technology")
ADPCM_DECODER(ADPCM_DTK,         sample_fmts_s16p, adpcm_dtk,         "ADPCM Nintendo Gamecube DTK")
ADPCM_DECODER(ADPCM_EA,          sample_fmts_s16,  adpcm_ea,          "ADPCM Electronic Arts")
ADPCM_DECODER(ADPCM_EA_MAXIS_XA, sample_fmts_s16,  adpcm_ea_maxis_xa, "ADPCM Electronic Arts Maxis CDROM XA")
ADPCM_DECODER(ADPCM_EA_R1,       sample_fmts_s16p, adpcm_ea_r1,       "ADPCM Electronic Arts R1")
ADPCM_DECODER(ADPCM_EA_R2,       sample_fmts_s16p, adpcm_ea_r2,       "ADPCM Electronic Arts R2")
ADPCM_DECODER(ADPCM_EA_R3,       sample_fmts_s16p, adpcm_ea_r3,       "ADPCM Electronic Arts R3")
ADPCM_DECODER(ADPCM_EA_XAS,      sample_fmts_s16p, adpcm_ea_xas,      "ADPCM Electronic Arts XAS")
ADPCM_DECODER(ADPCM_IMA_ACORN,   sample_fmts_s16,  adpcm_ima_acorn,   "ADPCM IMA Acorn Replay")
ADPCM_DECODER(ADPCM_IMA_AMV,     sample_fmts_s16,  adpcm_ima_amv,     "ADPCM IMA AMV")
ADPCM_DECODER(ADPCM_IMA_APC,     sample_fmts_s16,  adpcm_ima_apc,     "ADPCM IMA CRYO APC")
ADPCM_DECODER(ADPCM_IMA_APM,     sample_fmts_s16,  adpcm_ima_apm,     "ADPCM IMA Ubisoft APM")
ADPCM_DECODER(ADPCM_IMA_CUNNING, sample_fmts_s16p, adpcm_ima_cunning, "ADPCM IMA Cunning Developments")
ADPCM_DECODER(ADPCM_IMA_DAT4,    sample_fmts_s16,  adpcm_ima_dat4,    "ADPCM IMA Eurocom DAT4")
ADPCM_DECODER(ADPCM_IMA_DK3,     sample_fmts_s16,  adpcm_ima_dk3,     "ADPCM IMA Duck DK3")
ADPCM_DECODER(ADPCM_IMA_DK4,     sample_fmts_s16,  adpcm_ima_dk4,     "ADPCM IMA Duck DK4")
ADPCM_DECODER(ADPCM_IMA_EA_EACS, sample_fmts_s16,  adpcm_ima_ea_eacs, "ADPCM IMA Electronic Arts EACS")
ADPCM_DECODER(ADPCM_IMA_EA_SEAD, sample_fmts_s16,  adpcm_ima_ea_sead, "ADPCM IMA Electronic Arts SEAD")
ADPCM_DECODER(ADPCM_IMA_ISS,     sample_fmts_s16,  adpcm_ima_iss,     "ADPCM IMA Funcom ISS")
ADPCM_DECODER(ADPCM_IMA_MOFLEX,  sample_fmts_s16p, adpcm_ima_moflex,  "ADPCM IMA MobiClip MOFLEX")
ADPCM_DECODER(ADPCM_IMA_MTF,     sample_fmts_s16,  adpcm_ima_mtf,     "ADPCM IMA Capcom's MT Framework")
ADPCM_DECODER(ADPCM_IMA_OKI,     sample_fmts_s16,  adpcm_ima_oki,     "ADPCM IMA Dialogic OKI")
ADPCM_DECODER(ADPCM_IMA_QT,      sample_fmts_s16p, adpcm_ima_qt,      "ADPCM IMA QuickTime")
ADPCM_DECODER(ADPCM_IMA_RAD,     sample_fmts_s16,  adpcm_ima_rad,     "ADPCM IMA Radical")
ADPCM_DECODER(ADPCM_IMA_SSI,     sample_fmts_s16,  adpcm_ima_ssi,     "ADPCM IMA Simon & Schuster Interactive")
ADPCM_DECODER(ADPCM_IMA_SMJPEG,  sample_fmts_s16,  adpcm_ima_smjpeg,  "ADPCM IMA Loki SDL MJPEG")
ADPCM_DECODER(ADPCM_IMA_ALP,     sample_fmts_s16,  adpcm_ima_alp,     "ADPCM IMA High Voltage Software ALP")
ADPCM_DECODER(ADPCM_IMA_WAV,     sample_fmts_s16p, adpcm_ima_wav,     "ADPCM IMA WAV")
ADPCM_DECODER(ADPCM_IMA_WS,      sample_fmts_both, adpcm_ima_ws,      "ADPCM IMA Westwood")
ADPCM_DECODER(ADPCM_MS,          sample_fmts_both, adpcm_ms,          "ADPCM Microsoft")
ADPCM_DECODER(ADPCM_MTAF,        sample_fmts_s16p, adpcm_mtaf,        "ADPCM MTAF")
ADPCM_DECODER(ADPCM_PSX,         sample_fmts_s16p, adpcm_psx,         "ADPCM Playstation")
ADPCM_DECODER(ADPCM_SBPRO_2,     sample_fmts_s16,  adpcm_sbpro_2,     "ADPCM Sound Blaster Pro 2-bit")
ADPCM_DECODER(ADPCM_SBPRO_3,     sample_fmts_s16,  adpcm_sbpro_3,     "ADPCM Sound Blaster Pro 2.6-bit")
ADPCM_DECODER(ADPCM_SBPRO_4,     sample_fmts_s16,  adpcm_sbpro_4,     "ADPCM Sound Blaster Pro 4-bit")
ADPCM_DECODER(ADPCM_SWF,         sample_fmts_s16,  adpcm_swf,         "ADPCM Shockwave Flash")
ADPCM_DECODER(ADPCM_THP_LE,      sample_fmts_s16p, adpcm_thp_le,      "ADPCM Nintendo THP (little-endian)")
ADPCM_DECODER(ADPCM_THP,         sample_fmts_s16p, adpcm_thp,         "ADPCM Nintendo THP")
ADPCM_DECODER(ADPCM_XA,          sample_fmts_s16p, adpcm_xa,          "ADPCM CDROM XA")
ADPCM_DECODER(ADPCM_YAMAHA,      sample_fmts_s16,  adpcm_yamaha,      "ADPCM Yamaha")
ADPCM_DECODER(ADPCM_ZORK,        sample_fmts_s16,  adpcm_zork,        "ADPCM Zork")
