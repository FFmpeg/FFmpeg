/*
 * Assorted DPCM codecs
 * Copyright (c) 2003 The ffmpeg Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file: dpcm.c
 * Assorted DPCM (differential pulse code modulation) audio codecs
 * by Mike Melanson (melanson@pcisys.net)
 * Xan DPCM decoder by Mario Brito (mbrito@student.dei.uc.pt)
 * for more information on the specific data formats, visit:
 *   http://www.pcisys.net/~melanson/codecs/simpleaudio.html
 *
 * Note about using the Xan DPCM decoder: Xan DPCM is used in AVI files
 * found in the Wing Commander IV computer game. These AVI files contain
 * WAVEFORMAT headers which report the audio format as 0x01: raw PCM.
 * Clearly incorrect. To detect Xan DPCM, you will probably have to
 * special-case your AVI demuxer to use Xan DPCM if the file uses 'Xxan'
 * (Xan video) for its video codec. Alternately, such AVI files also contain
 * the fourcc 'Axan' in the 'auds' chunk of the AVI header.
 */

#include "avcodec.h"

typedef struct DPCMContext {
    int channels;
    short roq_square_array[256];
} DPCMContext;

#define SATURATE_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;
#define SE_16BIT(x)  if (x & 0x8000) x -= 0x10000;

static int interplay_delta_table[] = {
         0,      1,      2,      3,      4,      5,      6,      7,
         8,      9,     10,     11,     12,     13,     14,     15,
        16,     17,     18,     19,     20,     21,     22,     23,
        24,     25,     26,     27,     28,     29,     30,     31,
        32,     33,     34,     35,     36,     37,     38,     39,
        40,     41,     42,     43,     47,     51,     56,     61,
        66,     72,     79,     86,     94,    102,    112,    122,
       133,    145,    158,    173,    189,    206,    225,    245,
       267,    292,    318,    348,    379,    414,    452,    493,
       538,    587,    640,    699,    763,    832,    908,    991,
      1081,   1180,   1288,   1405,   1534,   1673,   1826,   1993,
      2175,   2373,   2590,   2826,   3084,   3365,   3672,   4008,
      4373,   4772,   5208,   5683,   6202,   6767,   7385,   8059,
      8794,   9597,  10472,  11428,  12471,  13609,  14851,  16206,
     17685,  19298,  21060,  22981,  25078,  27367,  29864,  32589,
    -29973, -26728, -23186, -19322, -15105, -10503,  -5481,     -1,
         1,      1,   5481,  10503,  15105,  19322,  23186,  26728,
     29973, -32589, -29864, -27367, -25078, -22981, -21060, -19298,
    -17685, -16206, -14851, -13609, -12471, -11428, -10472,  -9597,
     -8794,  -8059,  -7385,  -6767,  -6202,  -5683,  -5208,  -4772,
     -4373,  -4008,  -3672,  -3365,  -3084,  -2826,  -2590,  -2373,
     -2175,  -1993,  -1826,  -1673,  -1534,  -1405,  -1288,  -1180,
     -1081,   -991,   -908,   -832,   -763,   -699,   -640,   -587,
      -538,   -493,   -452,   -414,   -379,   -348,   -318,   -292,
      -267,   -245,   -225,   -206,   -189,   -173,   -158,   -145,
      -133,   -122,   -112,   -102,    -94,    -86,    -79,    -72,
       -66,    -61,    -56,    -51,    -47,    -43,    -42,    -41,
       -40,    -39,    -38,    -37,    -36,    -35,    -34,    -33,
       -32,    -31,    -30,    -29,    -28,    -27,    -26,    -25,
       -24,    -23,    -22,    -21,    -20,    -19,    -18,    -17,
       -16,    -15,    -14,    -13,    -12,    -11,    -10,     -9,
        -8,     -7,     -6,     -5,     -4,     -3,     -2,     -1

};

static int dpcm_decode_init(AVCodecContext *avctx)
{
    DPCMContext *s = avctx->priv_data;
    int i;
    short square;

    s->channels = avctx->channels;

    switch(avctx->codec->id) {

    case CODEC_ID_ROQ_DPCM:
        /* initialize square table */
        for (i = 0; i < 128; i++) {
            square = i * i;
            s->roq_square_array[i] = square;
            s->roq_square_array[i + 128] = -square;
        }
        break;

    default:
        break;
    }

    return 0;
}

static int dpcm_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    DPCMContext *s = avctx->priv_data;
    int in, out = 0;
    int predictor[2];
    int channel_number = 0;
    short *output_samples = data;
    int shift[2];
    unsigned char byte;
    short diff;

    if (!buf_size)
        return 0;

    switch(avctx->codec->id) {

    case CODEC_ID_ROQ_DPCM:
        if (s->channels == 1)
            predictor[0] = LE_16(&buf[6]);
        else {
            predictor[0] = buf[7] << 8;
            predictor[1] = buf[6] << 8;
        }
        SE_16BIT(predictor[0]);
        SE_16BIT(predictor[1]);

        /* decode the samples */
        for (in = 8, out = 0; in < buf_size; in++, out++) {
            predictor[channel_number] += s->roq_square_array[buf[in]];
            SATURATE_S16(predictor[channel_number]);
            output_samples[out] = predictor[channel_number];

            /* toggle channel */
            channel_number ^= s->channels - 1;
        }
        break;

    case CODEC_ID_INTERPLAY_DPCM:
        in = 6;  /* skip over the stream mask and stream length */
        predictor[0] = LE_16(&buf[in]);
        in += 2;
        SE_16BIT(predictor[0])
        output_samples[out++] = predictor[0];
        if (s->channels == 2) {
            predictor[1] = LE_16(&buf[in]);
            in += 2;
            SE_16BIT(predictor[1])
            output_samples[out++] = predictor[1];
        }

        while (in < buf_size) {
            predictor[channel_number] += interplay_delta_table[buf[in++]];
            SATURATE_S16(predictor[channel_number]);
            output_samples[out++] = predictor[channel_number];

            /* toggle channel */
            channel_number ^= s->channels - 1;
        }

        break;

    case CODEC_ID_XAN_DPCM:
        in = 0;
        shift[0] = shift[1] = 4;
        predictor[0] = LE_16(&buf[in]);
        in += 2;
        SE_16BIT(predictor[0]);
        if (s->channels == 2) {
            predictor[1] = LE_16(&buf[in]);
            in += 2;
            SE_16BIT(predictor[1]);
        }

        while (in < buf_size) {
            byte = buf[in++];
            diff = (byte & 0xFC) << 8;
            if ((byte & 0x03) == 3)
                shift[channel_number]++;
            else
                shift[channel_number] -= (2 * (byte & 3));
            /* saturate the shifter to a lower limit of 0 */
            if (shift[channel_number] < 0)
                shift[channel_number] = 0;

            diff >>= shift[channel_number];
            predictor[channel_number] += diff;

            SATURATE_S16(predictor[channel_number]);
            output_samples[out++] = predictor[channel_number];

            /* toggle channel */
            channel_number ^= s->channels - 1;
        }
        break;
    }

    *data_size = out * sizeof(short);
    return buf_size;
}

AVCodec roq_dpcm_decoder = {
    "roq_dpcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_ROQ_DPCM,
    sizeof(DPCMContext),
    dpcm_decode_init,
    NULL,
    NULL,
    dpcm_decode_frame,
};

AVCodec interplay_dpcm_decoder = {
    "interplay_dpcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_INTERPLAY_DPCM,
    sizeof(DPCMContext),
    dpcm_decode_init,
    NULL,
    NULL,
    dpcm_decode_frame,
};

AVCodec xan_dpcm_decoder = {
    "xan_dpcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_XAN_DPCM,
    sizeof(DPCMContext),
    dpcm_decode_init,
    NULL,
    NULL,
    dpcm_decode_frame,
};
