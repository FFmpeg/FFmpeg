/*
 * ADPCM codecs
 * Copyright (c) 2001 Fabrice Bellard.
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
#include "avcodec.h"

/*
 * First version by Francois Revol revol@free.fr
 *
 * Features and limitations:
 *
 * Reference documents:
 * http://www.pcisys.net/~melanson/codecs/adpcm.txt
 * http://www.geocities.com/SiliconValley/8682/aud3.txt
 * http://openquicktime.sourceforge.net/plugins.htm
 * XAnim sources (xa_codec.c) http://www.rasnaimaging.com/people/lapus/download.html
 */

#define BLKSIZE 1024

#define CLAMP_TO_SHORT(value) \
if (value > 32767) \
    value = 32767; \
else if (value < -32768) \
    value = -32768; \

/* step_table[] and index_table[] are from the ADPCM reference source */
/* This is the index table: */
static int index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

/* This is the step table. Note that many programs use slight deviations from
 * this table, but such deviations are negligible:
 */
static int step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

/* AdaptationTable[], AdaptCoeff1[], and AdaptCoeff2[] are from libsndfile */
static int AdaptationTable[] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        768, 614, 512, 409, 307, 230, 230, 230
};

static int AdaptCoeff1[] = {
        256, 512, 0, 192, 240, 460, 392
};

static int AdaptCoeff2[] = {
        0, -256, 0, 64, 0, -208, -232
};

/* end of tables */

typedef struct ADPCMChannelStatus {
    int predictor;
    short int step_index;
    int step;

    /* MS version */
    short sample1;
    short sample2;
    int coeff1;
    int coeff2;
    int idelta;
} ADPCMChannelStatus;

typedef struct ADPCMContext {
    int channel; /* for stereo MOVs, decode left, then decode right, then tell it's decoded */
    ADPCMChannelStatus status[2];
    short sample_buffer[32]; /* hold left samples while waiting for right samples */
} ADPCMContext;

/* XXX: implement encoding */

static int adpcm_encode_init(AVCodecContext *avctx)
{
    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_QT:
        avctx->frame_size = 64; /* XXX: ??? */
        break;
    case CODEC_ID_ADPCM_IMA_WAV:
        avctx->frame_size = 64; /* XXX: ??? */
        break;
    default:
        avctx->frame_size = 1;
        break;
    }
    return 0;
}

static int adpcm_encode_close(AVCodecContext *avctx)
{
    switch(avctx->codec->id) {
    default:
        /* nothing to free */
        break;
    }
    return 0;
}

static int adpcm_encode_frame(AVCodecContext *avctx,
			    unsigned char *frame, int buf_size, void *data)
{
    int n, sample_size, v;
    short *samples;
    unsigned char *dst;

    switch(avctx->codec->id) {
    default:
        return -1;
    }
    avctx->key_frame = 1;
    //avctx->frame_size = (dst - frame) / (sample_size * avctx->channels);

    return dst - frame;
}

static int adpcm_decode_init(AVCodecContext * avctx)
{
    ADPCMContext *c = avctx->priv_data;

    c->channel = 0;
    c->status[0].predictor = c->status[1].predictor = 0;
    c->status[0].step_index = c->status[1].step_index = 0;
    c->status[0].step = c->status[1].step = 0;

    switch(avctx->codec->id) {
    default:
        break;
    }
    return 0;
}

static inline short adpcm_ima_expand_nibble(ADPCMChannelStatus *c, char nibble)
{
    int step_index;
    int predictor;
    int sign, delta, diff, step;

    predictor = c->predictor;
    step_index = c->step_index + index_table[(unsigned)nibble];
    if (step_index < 0) step_index = 0;
    if (step_index > 88) step_index = 88;

    step = c->step;

/*
    diff = ((signed)((nibble & 0x08)?(nibble | 0xF0):(nibble)) + 0.5) * step / 4;
    predictor += diff;
*/
    sign = nibble & 8;
    delta = nibble & 7;
    diff = step >> 3;
    if (delta & 4) diff += step;
    if (delta & 2) diff += step >> 1;
    if (delta & 1) diff += step >> 2;
    if (sign) predictor -= diff;
    else predictor += diff;

    CLAMP_TO_SHORT(predictor);
    c->predictor = predictor;
    c->step_index = step_index;
    c->step = step_table[step_index];
    
    return (short)predictor;
}

static inline short adpcm_ms_expand_nibble(ADPCMChannelStatus *c, char nibble)
{
    int predictor;

    predictor = (((c->sample1) * (c->coeff1)) + ((c->sample2) * (c->coeff2))) / 256;
    predictor += (signed)((nibble & 0x08)?(nibble - 0x10):(nibble)) * c->idelta;
    CLAMP_TO_SHORT(predictor);

    c->sample2 = c->sample1;
    c->sample1 = predictor;
    c->idelta = (AdaptationTable[(int)nibble] * c->idelta) / 256;
    if (c->idelta < 16) c->idelta = 16;

    return (short)predictor;
}

static int adpcm_decode_frame(AVCodecContext *avctx,
			    void *data, int *data_size,
			    UINT8 *buf, int buf_size)
{
    ADPCMContext *c = avctx->priv_data;
    ADPCMChannelStatus *cs;
    int n, m, channel;
    int block_predictor[2];
    short *samples;
    UINT8 *src;
    int st; /* stereo */

    samples = data;
    src = buf;

//printf("adpcm_decode_frame() buf_size=%i\n", buf_size);

    st = avctx->channels == 2;

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_QT:
        n = (buf_size - 2);/* >> 2*avctx->channels;*/
        channel = c->channel;
        cs = &(c->status[channel]);
        /* (pppppp) (piiiiiii) */

        /* Bits 15-7 are the _top_ 9 bits of the 16-bit initial predictor value */
        cs->predictor = (*src++) << 8;
        cs->predictor |= (*src & 0x80);
        cs->predictor &= 0xFF80;

        /* sign extension */
        if(cs->predictor & 0x8000)
            cs->predictor -= 0x10000;

        CLAMP_TO_SHORT(cs->predictor);

        cs->step_index = (*src++) & 0x7F;

if (cs->step_index > 88) printf("ERROR: step_index = %i\n", cs->step_index);
        if (cs->step_index > 88) cs->step_index = 88;

        cs->step = step_table[cs->step_index];

        if (st && channel)
            samples++;

        for(m=32; n>0 && m>0; n--, m--) { /* in QuickTime, IMA is encoded by chuncks of 34 bytes (=64 samples) */
            *samples = adpcm_ima_expand_nibble(cs, src[0] & 0x0F);
            samples += avctx->channels;
            *samples = adpcm_ima_expand_nibble(cs, (src[0] >> 4) & 0x0F);
            samples += avctx->channels;
            src ++;
        }

        if(st) { /* handle stereo interlacing */
            c->channel = (channel + 1) % 2; /* we get one packet for left, then one for right data */
            if(channel == 0) { /* wait for the other packet before outputing anything */
                *data_size = 0;
                return src - buf;
            }
        }
        break;
    case CODEC_ID_ADPCM_IMA_WAV:
        if (buf_size > BLKSIZE) {
            if (avctx->block_align != 0)
                buf_size = avctx->block_align;
            else
                buf_size = BLKSIZE;
        }
        n = buf_size - 4 * avctx->channels;
        cs = &(c->status[0]);
        cs->predictor = (*src++) & 0x0FF;
        cs->predictor |= ((*src++) << 8) & 0x0FF00;
        if(cs->predictor & 0x8000)
            cs->predictor -= 0x10000;
        CLAMP_TO_SHORT(cs->predictor);

        cs->step_index = *src++;
        if (cs->step_index < 0) cs->step_index = 0;
        if (cs->step_index > 88) cs->step_index = 88;
        if (*src++) puts("unused byte should be null !!"); /* unused */
        if (st) {
            cs = &(c->status[1]);
            cs->predictor = (*src++) & 0x0FF;
            cs->predictor |= ((*src++) << 8) & 0x0FF00;
            if(cs->predictor & 0x8000)
                cs->predictor -= 0x10000;
            CLAMP_TO_SHORT(cs->predictor);

            cs->step_index = *src++;
            if (cs->step_index < 0) cs->step_index = 0;
            if (cs->step_index > 88) cs->step_index = 88;
            src++; /* unused */
        }
        cs = &(c->status[0]);


        for(m=3; n>0; n--, m--) {
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], src[0] & 0x0F);
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], src[4] & 0x0F);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], (src[0] >> 4) & 0x0F);
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], (src[4] >> 4) & 0x0F);
            src ++;
            if (st && !m) {
                m=3;
                src+=4;
            }
        }
        break;
    case CODEC_ID_ADPCM_MS:

        if (buf_size > BLKSIZE) {
            if (avctx->block_align != 0)
                buf_size = avctx->block_align;
            else
                buf_size = BLKSIZE;
        }
        n = buf_size - 7 * avctx->channels;
        if (n < 0)
            return -1;
        block_predictor[0] = (*src++); /* should be bound */
        block_predictor[0] = (block_predictor[0] < 0)?(0):((block_predictor[0] > 7)?(7):(block_predictor[0]));
        block_predictor[1] = 0;
        if (st)
            block_predictor[1] = (*src++);
        block_predictor[1] = (block_predictor[1] < 0)?(0):((block_predictor[1] > 7)?(7):(block_predictor[1]));
        c->status[0].idelta = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        if (c->status[0].idelta & 0x08000)
            c->status[0].idelta -= 0x10000;
        src+=2;
        if (st)
            c->status[1].idelta = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        if (st && c->status[1].idelta & 0x08000)
            c->status[1].idelta |= 0xFFFF0000;
        if (st)
            src+=2;
        c->status[0].coeff1 = AdaptCoeff1[block_predictor[0]];
        c->status[0].coeff2 = AdaptCoeff2[block_predictor[0]];
        c->status[1].coeff1 = AdaptCoeff1[block_predictor[1]];
        c->status[1].coeff2 = AdaptCoeff2[block_predictor[1]];
        
        c->status[0].sample1 = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        src+=2;
        if (st) c->status[1].sample1 = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        if (st) src+=2;
        c->status[0].sample2 = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        src+=2;
        if (st) c->status[1].sample2 = ((*src & 0xFF) | ((src[1] << 8) & 0xFF00));
        if (st) src+=2;

        *samples++ = c->status[0].sample1;
        if (st) *samples++ = c->status[1].sample1;
        *samples++ = c->status[0].sample2;
        if (st) *samples++ = c->status[1].sample2;
        for(;n>0;n--) {
            *samples++ = adpcm_ms_expand_nibble(&c->status[0], (src[0] >> 4) & 0x0F);
            *samples++ = adpcm_ms_expand_nibble(&c->status[st], src[0] & 0x0F);
            src ++;
        }
        break;
    default:
        *data_size = 0;
        return -1;
    }
    *data_size = (UINT8 *)samples - (UINT8 *)data;
    return src - buf;
}

#define ADPCM_CODEC(id, name)                   \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(ADPCMContext),                       \
    adpcm_encode_init,                          \
    adpcm_encode_frame,                         \
    adpcm_encode_close,                         \
    NULL,                                       \
};                                              \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(ADPCMContext),                       \
    adpcm_decode_init,                          \
    NULL,                                       \
    NULL,                                       \
    adpcm_decode_frame,                         \
};

ADPCM_CODEC(CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt);
ADPCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
ADPCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);

#undef ADPCM_CODEC
