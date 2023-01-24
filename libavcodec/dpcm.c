/*
 * Assorted DPCM codecs
 * Copyright (c) 2003 The FFmpeg project
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
 * Assorted DPCM (differential pulse code modulation) audio codecs
 * by Mike Melanson (melanson@pcisys.net)
 * Xan DPCM decoder by Mario Brito (mbrito@student.dei.uc.pt)
 * for more information on the specific data formats, visit:
 *   http://www.pcisys.net/~melanson/codecs/simpleaudio.html
 * SOL DPCMs implemented by Konstantin Shishkov
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
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "mathops.h"

typedef struct DPCMContext {
    int16_t array[256];
    int sample[2];                  ///< previous sample (for SOL_DPCM and WADY_DPCM)
    int scale;                      ///< scale for WADY_DPCM
    const int8_t *sol_table;        ///< delta table for SOL_DPCM
} DPCMContext;

static const int32_t derf_steps[96] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 16,
    17, 19, 21, 23, 25, 28, 31, 34,
    37, 41, 45, 50, 55, 60, 66, 73,
    80, 88, 97, 107, 118, 130, 143, 157,
    173, 190, 209, 230, 253, 279, 307, 337,
    371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int16_t interplay_delta_table[] = {
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

static const int8_t sol_table_old[16] = {
      0x0,  0x1,  0x2,  0x3,  0x6,  0xA,  0xF, 0x15,
    -0x15, -0xF, -0xA, -0x6, -0x3, -0x2, -0x1,  0x0
};

static const int8_t sol_table_new[16] = {
    0x0,  0x1,  0x2,  0x3,  0x6,  0xA,  0xF,  0x15,
    0x0, -0x1, -0x2, -0x3, -0x6, -0xA, -0xF, -0x15
};

static const int16_t sol_table_16[128] = {
    0x000, 0x008, 0x010, 0x020, 0x030, 0x040, 0x050, 0x060, 0x070, 0x080,
    0x090, 0x0A0, 0x0B0, 0x0C0, 0x0D0, 0x0E0, 0x0F0, 0x100, 0x110, 0x120,
    0x130, 0x140, 0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0, 0x1B0, 0x1C0,
    0x1D0, 0x1E0, 0x1F0, 0x200, 0x208, 0x210, 0x218, 0x220, 0x228, 0x230,
    0x238, 0x240, 0x248, 0x250, 0x258, 0x260, 0x268, 0x270, 0x278, 0x280,
    0x288, 0x290, 0x298, 0x2A0, 0x2A8, 0x2B0, 0x2B8, 0x2C0, 0x2C8, 0x2D0,
    0x2D8, 0x2E0, 0x2E8, 0x2F0, 0x2F8, 0x300, 0x308, 0x310, 0x318, 0x320,
    0x328, 0x330, 0x338, 0x340, 0x348, 0x350, 0x358, 0x360, 0x368, 0x370,
    0x378, 0x380, 0x388, 0x390, 0x398, 0x3A0, 0x3A8, 0x3B0, 0x3B8, 0x3C0,
    0x3C8, 0x3D0, 0x3D8, 0x3E0, 0x3E8, 0x3F0, 0x3F8, 0x400, 0x440, 0x480,
    0x4C0, 0x500, 0x540, 0x580, 0x5C0, 0x600, 0x640, 0x680, 0x6C0, 0x700,
    0x740, 0x780, 0x7C0, 0x800, 0x900, 0xA00, 0xB00, 0xC00, 0xD00, 0xE00,
    0xF00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000
};

static const int16_t wady_table[128] = {
    0,   2,   4,   6,   8,   10,  12,  15,
    18,  21,  24,  28,  32,  36,  40,  44,
    49,  54,  59,  64,  70,  76,  82,  88,
    95,  102, 109, 116, 124, 132, 140, 148,
    160, 170, 180, 190, 200, 210, 220, 230,
    240, 255, 270, 285, 300, 320, 340, 360,
    380, 400, 425, 450, 475, 500, 525, 550,
    580, 610, 650, 700, 750, 800, 900, 1000,
    -0,  -2,  -4,  -6,  -8,  -10, -12, -15,
    -18, -21, -24, -28, -32, -36, -40, -44,
    -49, -54, -59, -64, -70, -76, -82, -88,
    -95, -102,-109,-116,-124,-132,-140,-148,
    -160,-170,-180,-190,-200,-210,-220,-230,
    -240,-255,-270,-285,-300,-320,-340,-360,
    -380,-400,-425,-450,-475,-500,-525,-550,
    -580,-610,-650,-700,-750,-800,-900,-1000,
};

static av_cold int dpcm_decode_init(AVCodecContext *avctx)
{
    DPCMContext *s = avctx->priv_data;
    int i;

    if (avctx->ch_layout.nb_channels < 1 || avctx->ch_layout.nb_channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels\n");
        return AVERROR(EINVAL);
    }

    s->sample[0] = s->sample[1] = 0;

    switch (avctx->codec->id) {

    case AV_CODEC_ID_ROQ_DPCM:
        /* initialize square table */
        for (i = 0; i < 128; i++) {
            int16_t square = i * i;
            s->array[i      ] =  square;
            s->array[i + 128] = -square;
        }
        break;

    case AV_CODEC_ID_SOL_DPCM:
        switch(avctx->codec_tag){
        case 1:
            s->sol_table = sol_table_old;
            s->sample[0] = s->sample[1] = 0x80;
            break;
        case 2:
            s->sol_table = sol_table_new;
            s->sample[0] = s->sample[1] = 0x80;
            break;
        case 3:
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown SOL subcodec\n");
            return -1;
        }
        break;

    case AV_CODEC_ID_SDX2_DPCM:
        for (i = -128; i < 128; i++) {
            int16_t square = i * i * 2;
            s->array[i+128] = i < 0 ? -square: square;
        }
        break;

    case AV_CODEC_ID_CBD2_DPCM:
        for (i = -128; i < 128; i++) {
            int16_t cube = (i * i * i) / 64;
            s->array[i+128] = cube;
        }
        break;

    case AV_CODEC_ID_GREMLIN_DPCM: {
        int delta = 0;
        int code = 64;
        int step = 45;

        s->array[0] = 0;
        for (i = 0; i < 127; i++) {
            delta += (code >> 5);
            code  += step;
            step  += 2;

            s->array[i*2 + 1] =  delta;
            s->array[i*2 + 2] = -delta;
        }
        s->array[255] = delta + (code >> 5);
        }
        break;

    case AV_CODEC_ID_WADY_DPCM:
        s->scale = (avctx->extradata && avctx->extradata_size > 0) ? avctx->extradata[0] : 1;
        break;

    default:
        break;
    }

    if (avctx->codec->id == AV_CODEC_ID_SOL_DPCM && avctx->codec_tag != 3)
        avctx->sample_fmt = AV_SAMPLE_FMT_U8;
    else
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    return 0;
}


static int dpcm_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    int buf_size = avpkt->size;
    DPCMContext *s = avctx->priv_data;
    int out = 0, ret;
    int predictor[2];
    int ch = 0;
    int stereo = avctx->ch_layout.nb_channels - 1;
    int16_t *output_samples, *samples_end;
    GetByteContext gb;

    if (stereo && (buf_size & 1))
        buf_size--;
    bytestream2_init(&gb, avpkt->data, buf_size);

    /* calculate output size */
    switch(avctx->codec->id) {
    case AV_CODEC_ID_ROQ_DPCM:
        out = buf_size - 8;
        break;
    case AV_CODEC_ID_INTERPLAY_DPCM:
        out = buf_size - 6 - avctx->ch_layout.nb_channels;
        break;
    case AV_CODEC_ID_XAN_DPCM:
        out = buf_size - 2 * avctx->ch_layout.nb_channels;
        break;
    case AV_CODEC_ID_SOL_DPCM:
        if (avctx->codec_tag != 3)
            out = buf_size * 2;
        else
            out = buf_size;
        break;
    case AV_CODEC_ID_WADY_DPCM:
    case AV_CODEC_ID_DERF_DPCM:
    case AV_CODEC_ID_GREMLIN_DPCM:
    case AV_CODEC_ID_CBD2_DPCM:
    case AV_CODEC_ID_SDX2_DPCM:
        out = buf_size;
        break;
    }
    if (out <= 0) {
        av_log(avctx, AV_LOG_ERROR, "packet is too small\n");
        return AVERROR(EINVAL);
    }
    if (out % avctx->ch_layout.nb_channels) {
        av_log(avctx, AV_LOG_WARNING, "channels have differing number of samples\n");
    }

    /* get output buffer */
    frame->nb_samples = (out + avctx->ch_layout.nb_channels - 1) / avctx->ch_layout.nb_channels;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    output_samples = (int16_t *)frame->data[0];
    samples_end = output_samples + out;

    switch(avctx->codec->id) {

    case AV_CODEC_ID_ROQ_DPCM:
        bytestream2_skipu(&gb, 6);

        if (stereo) {
            predictor[1] = sign_extend(bytestream2_get_byteu(&gb) << 8, 16);
            predictor[0] = sign_extend(bytestream2_get_byteu(&gb) << 8, 16);
        } else {
            predictor[0] = sign_extend(bytestream2_get_le16u(&gb), 16);
        }

        /* decode the samples */
        while (output_samples < samples_end) {
            predictor[ch] += s->array[bytestream2_get_byteu(&gb)];
            predictor[ch]  = av_clip_int16(predictor[ch]);
            *output_samples++ = predictor[ch];

            /* toggle channel */
            ch ^= stereo;
        }
        break;

    case AV_CODEC_ID_INTERPLAY_DPCM:
        bytestream2_skipu(&gb, 6);  /* skip over the stream mask and stream length */

        for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
            predictor[ch] = sign_extend(bytestream2_get_le16u(&gb), 16);
            *output_samples++ = predictor[ch];
        }

        ch = 0;
        while (output_samples < samples_end) {
            predictor[ch] += interplay_delta_table[bytestream2_get_byteu(&gb)];
            predictor[ch]  = av_clip_int16(predictor[ch]);
            *output_samples++ = predictor[ch];

            /* toggle channel */
            ch ^= stereo;
        }
        break;

    case AV_CODEC_ID_XAN_DPCM:
    {
        int shift[2] = { 4, 4 };

        for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
            predictor[ch] = sign_extend(bytestream2_get_le16u(&gb), 16);

        ch = 0;
        while (output_samples < samples_end) {
            int diff = bytestream2_get_byteu(&gb);
            int n    = diff & 3;

            if (n == 3)
                shift[ch]++;
            else
                shift[ch] -= (2 * n);
            diff = sign_extend((diff &~ 3) << 8, 16);

            /* saturate the shifter to 0..31 */
            shift[ch] = av_clip_uintp2(shift[ch], 5);

            diff >>= shift[ch];
            predictor[ch] += diff;

            predictor[ch] = av_clip_int16(predictor[ch]);
            *output_samples++ = predictor[ch];

            /* toggle channel */
            ch ^= stereo;
        }
        break;
    }
    case AV_CODEC_ID_SOL_DPCM:
        if (avctx->codec_tag != 3) {
            uint8_t *output_samples_u8 = frame->data[0],
                    *samples_end_u8 = output_samples_u8 + out;
            while (output_samples_u8 < samples_end_u8) {
                int n = bytestream2_get_byteu(&gb);

                s->sample[0] += s->sol_table[n >> 4];
                s->sample[0]  = av_clip_uint8(s->sample[0]);
                *output_samples_u8++ = s->sample[0];

                s->sample[stereo] += s->sol_table[n & 0x0F];
                s->sample[stereo]  = av_clip_uint8(s->sample[stereo]);
                *output_samples_u8++ = s->sample[stereo];
            }
        } else {
            while (output_samples < samples_end) {
                int n = bytestream2_get_byteu(&gb);
                if (n & 0x80) s->sample[ch] -= sol_table_16[n & 0x7F];
                else          s->sample[ch] += sol_table_16[n & 0x7F];
                s->sample[ch] = av_clip_int16(s->sample[ch]);
                *output_samples++ = s->sample[ch];
                /* toggle channel */
                ch ^= stereo;
            }
        }
        break;

    case AV_CODEC_ID_CBD2_DPCM:
    case AV_CODEC_ID_SDX2_DPCM:
        while (output_samples < samples_end) {
            int8_t n = bytestream2_get_byteu(&gb);

            if (!(n & 1))
                s->sample[ch] = 0;
            s->sample[ch] += s->array[n + 128];
            s->sample[ch]  = av_clip_int16(s->sample[ch]);
            *output_samples++ = s->sample[ch];
            ch ^= stereo;
        }
        break;

    case AV_CODEC_ID_GREMLIN_DPCM: {
        int idx = 0;

        while (output_samples < samples_end) {
            uint8_t n = bytestream2_get_byteu(&gb);

            *output_samples++ = s->sample[idx] += (unsigned)s->array[n];
            idx ^= 1;
        }
        }
        break;

    case AV_CODEC_ID_DERF_DPCM: {
        int idx = 0;

        while (output_samples < samples_end) {
            uint8_t n = bytestream2_get_byteu(&gb);
            int index = FFMIN(n & 0x7f, 95);

            s->sample[idx] += (n & 0x80 ? -1: 1) * derf_steps[index];
            s->sample[idx]  = av_clip_int16(s->sample[idx]);
            *output_samples++ = s->sample[idx];
            idx ^= stereo;
        }
        }
        break;

    case AV_CODEC_ID_WADY_DPCM: {
        int idx = 0;

        while (output_samples < samples_end) {
            const uint8_t n = bytestream2_get_byteu(&gb);

            if (n & 0x80)
                s->sample[idx] = sign_extend((n & 0x7f) << 9, 16);
            else
                s->sample[idx] += s->scale * wady_table[n & 0x7f];
            *output_samples++ = av_clip_int16(s->sample[idx]);
            idx ^= stereo;
        }
        }
        break;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static void dpcm_flush(AVCodecContext *avctx)
{
    DPCMContext *s = avctx->priv_data;

    s->sample[0] = s->sample[1] = 0;
}

#define DPCM_DECODER(id_, name_, long_name_)                \
const FFCodec ff_ ## name_ ## _decoder = {                  \
    .p.name         = #name_,                               \
    CODEC_LONG_NAME(long_name_),                            \
    .p.type         = AVMEDIA_TYPE_AUDIO,                   \
    .p.id           = id_,                                  \
    .p.capabilities = AV_CODEC_CAP_DR1,                     \
    .priv_data_size = sizeof(DPCMContext),                  \
    .init           = dpcm_decode_init,                     \
    .flush          = dpcm_flush,                           \
    FF_CODEC_DECODE_CB(dpcm_decode_frame),                  \
}

DPCM_DECODER(AV_CODEC_ID_CBD2_DPCM,      cbd2_dpcm,      "DPCM Cuberoot-Delta-Exact");
DPCM_DECODER(AV_CODEC_ID_DERF_DPCM,      derf_dpcm,      "DPCM Xilam DERF");
DPCM_DECODER(AV_CODEC_ID_GREMLIN_DPCM,   gremlin_dpcm,   "DPCM Gremlin");
DPCM_DECODER(AV_CODEC_ID_INTERPLAY_DPCM, interplay_dpcm, "DPCM Interplay");
DPCM_DECODER(AV_CODEC_ID_ROQ_DPCM,       roq_dpcm,       "DPCM id RoQ");
DPCM_DECODER(AV_CODEC_ID_SDX2_DPCM,      sdx2_dpcm,      "DPCM Squareroot-Delta-Exact");
DPCM_DECODER(AV_CODEC_ID_SOL_DPCM,       sol_dpcm,       "DPCM Sol");
DPCM_DECODER(AV_CODEC_ID_XAN_DPCM,       xan_dpcm,       "DPCM Xan");
DPCM_DECODER(AV_CODEC_ID_WADY_DPCM,      wady_dpcm,      "DPCM Marble WADY");
