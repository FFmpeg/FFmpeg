/*
 * ADPCM codecs
 * Copyright (c) 2001-2003 The ffmpeg Project
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

/**
 * @file adpcm.c
 * ADPCM codecs.
 * First version by Francois Revol (revol@free.fr)
 * Fringe ADPCM codecs (e.g., DK3, DK4, Westwood)
 *   by Mike Melanson (melanson@pcisys.net)
 * CD-ROM XA ADPCM codec by BERO
 *
 * Features and limitations:
 *
 * Reference documents:
 * http://www.pcisys.net/~melanson/codecs/simpleaudio.html
 * http://www.geocities.com/SiliconValley/8682/aud3.txt
 * http://openquicktime.sourceforge.net/plugins.htm
 * XAnim sources (xa_codec.c) http://www.rasnaimaging.com/people/lapus/download.html
 * http://www.cs.ucla.edu/~leec/mediabench/applications.html
 * SoX source code http://home.sprynet.com/~cbagwell/sox.html
 *
 * CD-ROM XA:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/xaadpcm.html
 * vagpack & depack http://homepages.compuserve.de/bITmASTER32/psx-index.html
 * readstr http://www.geocities.co.jp/Playtown/2004/
 */

#define BLKSIZE 1024

#define CLAMP_TO_SHORT(value) \
if (value > 32767) \
    value = 32767; \
else if (value < -32768) \
    value = -32768; \

/* step_table[] and index_table[] are from the ADPCM reference source */
/* This is the index table: */
static const int index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

/** 
 * This is the step table. Note that many programs use slight deviations from
 * this table, but such deviations are negligible:
 */
static const int step_table[89] = {
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

/* These are for MS-ADPCM */
/* AdaptationTable[], AdaptCoeff1[], and AdaptCoeff2[] are from libsndfile */
static const int AdaptationTable[] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        768, 614, 512, 409, 307, 230, 230, 230
};

static const int AdaptCoeff1[] = {
        256, 512, 0, 192, 240, 460, 392
};

static const int AdaptCoeff2[] = {
        0, -256, 0, 64, 0, -208, -232
};

/* These are for CD-ROM XA ADPCM */
static const int xa_adpcm_table[5][2] = {
   {   0,   0 },
   {  60,   0 },
   { 115, -52 },
   {  98, -55 },
   { 122, -60 }
};

/* end of tables */

typedef struct ADPCMChannelStatus {
    int predictor;
    short int step_index;
    int step;
    /* for encoding */
    int prev_sample;

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

#ifdef CONFIG_ENCODERS
static int adpcm_encode_init(AVCodecContext *avctx)
{
    if (avctx->channels > 2)
        return -1; /* only stereo or mono =) */
    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_QT:
        av_log(avctx, AV_LOG_ERROR, "ADPCM: codec adpcm_ima_qt unsupported for encoding !\n");
        avctx->frame_size = 64; /* XXX: can multiple of avctx->channels * 64 (left and right blocks are interleaved) */
        return -1;
        break;
    case CODEC_ID_ADPCM_IMA_WAV:
        avctx->frame_size = (BLKSIZE - 4 * avctx->channels) * 8 / (4 * avctx->channels) + 1; /* each 16 bits sample gives one nibble */
                                                             /* and we have 4 bytes per channel overhead */
        avctx->block_align = BLKSIZE;
        /* seems frame_size isn't taken into account... have to buffer the samples :-( */
        break;
    case CODEC_ID_ADPCM_MS:
        av_log(avctx, AV_LOG_ERROR, "ADPCM: codec adpcm_ms unsupported for encoding !\n");
        return -1;
        break;
    default:
        return -1;
        break;
    }

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static int adpcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}


static inline unsigned char adpcm_ima_compress_sample(ADPCMChannelStatus *c, short sample)
{
    int step_index;
    unsigned char nibble;
    
    int sign = 0; /* sign bit of the nibble (MSB) */
    int delta, predicted_delta;

    delta = sample - c->prev_sample;

    if (delta < 0) {
        sign = 1;
        delta = -delta;
    }

    step_index = c->step_index;

    /* nibble = 4 * delta / step_table[step_index]; */
    nibble = (delta << 2) / step_table[step_index];

    if (nibble > 7)
        nibble = 7;

    step_index += index_table[nibble];
    if (step_index < 0)
        step_index = 0;
    if (step_index > 88)
        step_index = 88;

    /* what the decoder will find */
    predicted_delta = ((step_table[step_index] * nibble) / 4) + (step_table[step_index] / 8);

    if (sign)
        c->prev_sample -= predicted_delta;
    else
        c->prev_sample += predicted_delta;

    CLAMP_TO_SHORT(c->prev_sample);


    nibble += sign << 3; /* sign * 8 */   

    /* save back */
    c->step_index = step_index;

    return nibble;
}

static int adpcm_encode_frame(AVCodecContext *avctx,
			    unsigned char *frame, int buf_size, void *data)
{
    int n;
    short *samples;
    unsigned char *dst;
    ADPCMContext *c = avctx->priv_data;

    dst = frame;
    samples = (short *)data;
/*    n = (BLKSIZE - 4 * avctx->channels) / (2 * 8 * avctx->channels); */

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_QT: /* XXX: can't test until we get .mov writer */
        break;
    case CODEC_ID_ADPCM_IMA_WAV:
        n = avctx->frame_size / 8;
            c->status[0].prev_sample = (signed short)samples[0]; /* XXX */
/*            c->status[0].step_index = 0; *//* XXX: not sure how to init the state machine */
            *dst++ = (c->status[0].prev_sample) & 0xFF; /* little endian */
            *dst++ = (c->status[0].prev_sample >> 8) & 0xFF;
            *dst++ = (unsigned char)c->status[0].step_index;
            *dst++ = 0; /* unknown */
            samples++;
            if (avctx->channels == 2) {
                c->status[1].prev_sample = (signed short)samples[1];
/*                c->status[1].step_index = 0; */
                *dst++ = (c->status[1].prev_sample) & 0xFF;
                *dst++ = (c->status[1].prev_sample >> 8) & 0xFF;
                *dst++ = (unsigned char)c->status[1].step_index;
                *dst++ = 0;
                samples++;
            }
        
            /* stereo: 4 bytes (8 samples) for left, 4 bytes for right, 4 bytes left, ... */
            for (; n>0; n--) {
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[0]) & 0x0F;
                *dst |= (adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels]) << 4) & 0xF0;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 2]) & 0x0F;
                *dst |= (adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 3]) << 4) & 0xF0;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 4]) & 0x0F;
                *dst |= (adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 5]) << 4) & 0xF0;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 6]) & 0x0F;
                *dst |= (adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 7]) << 4) & 0xF0;
                dst++;
                /* right channel */
                if (avctx->channels == 2) {
                    *dst = adpcm_ima_compress_sample(&c->status[1], samples[1]);
                    *dst |= adpcm_ima_compress_sample(&c->status[1], samples[3]) << 4;
                    dst++;
                    *dst = adpcm_ima_compress_sample(&c->status[1], samples[5]);
                    *dst |= adpcm_ima_compress_sample(&c->status[1], samples[7]) << 4;
                    dst++;
                    *dst = adpcm_ima_compress_sample(&c->status[1], samples[9]);
                    *dst |= adpcm_ima_compress_sample(&c->status[1], samples[11]) << 4;
                    dst++;
                    *dst = adpcm_ima_compress_sample(&c->status[1], samples[13]);
                    *dst |= adpcm_ima_compress_sample(&c->status[1], samples[15]) << 4;
                    dst++;
                }
                samples += 8 * avctx->channels;
            }
        break;
    default:
        return -1;
    }
    return dst - frame;
}
#endif //CONFIG_ENCODERS

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

    step = step_table[c->step_index];
    step_index = c->step_index + index_table[(unsigned)nibble];
    if (step_index < 0) step_index = 0;
    else if (step_index > 88) step_index = 88;

    sign = nibble & 8;
    delta = nibble & 7;
    /* perform direct multiplication instead of series of jumps proposed by
     * the reference ADPCM implementation since modern CPUs can do the mults
     * quickly enough */
    diff = ((2 * delta + 1) * step) >> 3;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    CLAMP_TO_SHORT(predictor);
    c->predictor = predictor;
    c->step_index = step_index;

    return (short)predictor;
}

static inline short adpcm_4xa_expand_nibble(ADPCMChannelStatus *c, char nibble)
{
    int step_index;
    int predictor;
    int sign, delta, diff, step;

    step = step_table[c->step_index];
    step_index = c->step_index + index_table[(unsigned)nibble];
    if (step_index < 0) step_index = 0;
    else if (step_index > 88) step_index = 88;

    sign = nibble & 8;
    delta = nibble & 7;
    
    diff = (delta*step + (step>>1))>>3; // difference to code above
    
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    CLAMP_TO_SHORT(predictor);
    c->predictor = predictor;
    c->step_index = step_index;

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

static void xa_decode(short *out, const unsigned char *in, 
    ADPCMChannelStatus *left, ADPCMChannelStatus *right, int inc)
{
    int i, j;
    int shift,filter,f0,f1;
    int s_1,s_2;
    int d,s,t;

    for(i=0;i<4;i++) {

        shift  = 12 - (in[4+i*2] & 15);
        filter = in[4+i*2] >> 4;
        f0 = xa_adpcm_table[filter][0];
        f1 = xa_adpcm_table[filter][1];

        s_1 = left->sample1;
        s_2 = left->sample2;

        for(j=0;j<28;j++) {
            d = in[16+i+j*4];

            t = (signed char)(d<<4)>>4;
            s = ( t<<shift ) + ((s_1*f0 + s_2*f1+32)>>6);
            CLAMP_TO_SHORT(s);
            *out = s;
            out += inc;
            s_2 = s_1;
            s_1 = s;
        }

        if (inc==2) { /* stereo */
            left->sample1 = s_1;
            left->sample2 = s_2;
            s_1 = right->sample1;
            s_2 = right->sample2;
            out = out + 1 - 28*2;
        }

        shift  = 12 - (in[5+i*2] & 15);
        filter = in[5+i*2] >> 4;

        f0 = xa_adpcm_table[filter][0];
        f1 = xa_adpcm_table[filter][1];

        for(j=0;j<28;j++) {
            d = in[16+i+j*4];

            t = (signed char)d >> 4;
            s = ( t<<shift ) + ((s_1*f0 + s_2*f1+32)>>6);
            CLAMP_TO_SHORT(s);
            *out = s;
            out += inc;
            s_2 = s_1;
            s_1 = s;
        }

        if (inc==2) { /* stereo */
            right->sample1 = s_1;
            right->sample2 = s_2;
            out -= 1;
        } else {
            left->sample1 = s_1;
            left->sample2 = s_2;
        }
    }
}


/* DK3 ADPCM support macro */
#define DK3_GET_NEXT_NIBBLE() \
    if (decode_top_nibble_next) \
    { \
        nibble = (last_byte >> 4) & 0x0F; \
        decode_top_nibble_next = 0; \
    } \
    else \
    { \
        last_byte = *src++; \
        if (src >= buf + buf_size) break; \
        nibble = last_byte & 0x0F; \
        decode_top_nibble_next = 1; \
    }

static int adpcm_decode_frame(AVCodecContext *avctx,
			    void *data, int *data_size,
			    uint8_t *buf, int buf_size)
{
    ADPCMContext *c = avctx->priv_data;
    ADPCMChannelStatus *cs;
    int n, m, channel, i;
    int block_predictor[2];
    short *samples;
    uint8_t *src;
    int st; /* stereo */

    /* DK3 ADPCM accounting variables */
    unsigned char last_byte = 0;
    unsigned char nibble;
    int decode_top_nibble_next = 0;
    int diff_channel;

    samples = data;
    src = buf;

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

        if (cs->step_index > 88) av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i\n", cs->step_index);
        if (cs->step_index > 88) cs->step_index = 88;

        cs->step = step_table[cs->step_index];

        if (st && channel)
            samples++;

        *samples++ = cs->predictor;
        samples += st;

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
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

	// XXX: do as per-channel loop
        cs = &(c->status[0]);
        cs->predictor = (*src++) & 0x0FF;
        cs->predictor |= ((*src++) << 8) & 0x0FF00;
        if(cs->predictor & 0x8000)
            cs->predictor -= 0x10000;
        CLAMP_TO_SHORT(cs->predictor);

	// XXX: is this correct ??: *samples++ = cs->predictor;

	cs->step_index = *src++;
        if (cs->step_index < 0) cs->step_index = 0;
        if (cs->step_index > 88) cs->step_index = 88;
        if (*src++) av_log(avctx, AV_LOG_ERROR, "unused byte should be null !!\n"); /* unused */

        if (st) {
            cs = &(c->status[1]);
            cs->predictor = (*src++) & 0x0FF;
            cs->predictor |= ((*src++) << 8) & 0x0FF00;
            if(cs->predictor & 0x8000)
                cs->predictor -= 0x10000;
            CLAMP_TO_SHORT(cs->predictor);

	    // XXX: is this correct ??: *samples++ = cs->predictor;

	    cs->step_index = *src++;
            if (cs->step_index < 0) cs->step_index = 0;
            if (cs->step_index > 88) cs->step_index = 88;
            src++; /* if != 0  -> out-of-sync */
        }

        for(m=4; src < (buf + buf_size);) {
	    *samples++ = adpcm_ima_expand_nibble(&c->status[0], src[0] & 0x0F);
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], src[4] & 0x0F);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], (src[0] >> 4) & 0x0F);
	    if (st) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], (src[4] >> 4) & 0x0F);
		if (!--m) {
		    m=4;
		    src+=4;
		}
	    }
	    src++;
	}
        break;
    case CODEC_ID_ADPCM_4XM:
        cs = &(c->status[0]);
        c->status[0].predictor= (int16_t)(src[0] + (src[1]<<8)); src+=2;
        if(st){
            c->status[1].predictor= (int16_t)(src[0] + (src[1]<<8)); src+=2;
        }
        c->status[0].step_index= (int16_t)(src[0] + (src[1]<<8)); src+=2;
        if(st){
            c->status[1].step_index= (int16_t)(src[0] + (src[1]<<8)); src+=2;
        }
//            if (cs->step_index < 0) cs->step_index = 0;
//            if (cs->step_index > 88) cs->step_index = 88;

        m= (buf_size - (src - buf))>>st;
//printf("%d %d %d %d\n", st, m, c->status[0].predictor, c->status[0].step_index);
        //FIXME / XXX decode chanels individual & interleave samples
        for(i=0; i<m; i++) {
	    *samples++ = adpcm_4xa_expand_nibble(&c->status[0], src[i] & 0x0F);
            if (st)
                *samples++ = adpcm_4xa_expand_nibble(&c->status[1], src[i+m] & 0x0F);
            *samples++ = adpcm_4xa_expand_nibble(&c->status[0], src[i] >> 4);
	    if (st)
                *samples++ = adpcm_4xa_expand_nibble(&c->status[1], src[i+m] >> 4);
	}

        src += m<<st;

        break;
    case CODEC_ID_ADPCM_MS:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;
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
    case CODEC_ID_ADPCM_IMA_DK4:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

        c->status[0].predictor = (src[0] | (src[1] << 8));
        c->status[0].step_index = src[2];
        src += 4;
        if(c->status[0].predictor & 0x8000)
            c->status[0].predictor -= 0x10000;
        *samples++ = c->status[0].predictor;
        if (st) {
            c->status[1].predictor = (src[0] | (src[1] << 8));
            c->status[1].step_index = src[2];
            src += 4;
            if(c->status[1].predictor & 0x8000)
                c->status[1].predictor -= 0x10000;
            *samples++ = c->status[1].predictor;
        }
        while (src < buf + buf_size) {

            /* take care of the top nibble (always left or mono channel) */
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], 
                (src[0] >> 4) & 0x0F);

            /* take care of the bottom nibble, which is right sample for
             * stereo, or another mono sample */
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], 
                    src[0] & 0x0F);
            else
                *samples++ = adpcm_ima_expand_nibble(&c->status[0], 
                    src[0] & 0x0F);

            src++;
        }
        break;
    case CODEC_ID_ADPCM_IMA_DK3:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

        c->status[0].predictor = (src[10] | (src[11] << 8));
        c->status[1].predictor = (src[12] | (src[13] << 8));
        c->status[0].step_index = src[14];
        c->status[1].step_index = src[15];
        /* sign extend the predictors */
        if(c->status[0].predictor & 0x8000)
            c->status[0].predictor -= 0x10000;
        if(c->status[1].predictor & 0x8000)
            c->status[1].predictor -= 0x10000;
        src += 16;
        diff_channel = c->status[1].predictor;

        /* the DK3_GET_NEXT_NIBBLE macro issues the break statement when
         * the buffer is consumed */
        while (1) {

            /* for this algorithm, c->status[0] is the sum channel and
             * c->status[1] is the diff channel */

            /* process the first predictor of the sum channel */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[0], nibble);

            /* process the diff channel predictor */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[1], nibble);

            /* process the first pair of stereo PCM samples */
            diff_channel = (diff_channel + c->status[1].predictor) / 2;
            *samples++ = c->status[0].predictor + c->status[1].predictor;
            *samples++ = c->status[0].predictor - c->status[1].predictor;

            /* process the second predictor of the sum channel */
            DK3_GET_NEXT_NIBBLE();
            adpcm_ima_expand_nibble(&c->status[0], nibble);

            /* process the second pair of stereo PCM samples */
            diff_channel = (diff_channel + c->status[1].predictor) / 2;
            *samples++ = c->status[0].predictor + c->status[1].predictor;
            *samples++ = c->status[0].predictor - c->status[1].predictor;
        }
        break;
    case CODEC_ID_ADPCM_IMA_WS:
        /* no per-block initialization; just start decoding the data */
        while (src < buf + buf_size) {

            if (st) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0], 
                    (src[0] >> 4) & 0x0F);
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], 
                    src[0] & 0x0F);
            } else {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0], 
                    (src[0] >> 4) & 0x0F);
                *samples++ = adpcm_ima_expand_nibble(&c->status[0], 
                    src[0] & 0x0F);
            }

            src++;
        }
        break;
    case CODEC_ID_ADPCM_XA:
        c->status[0].sample1 = c->status[0].sample2 = 
        c->status[1].sample1 = c->status[1].sample2 = 0;
        while (buf_size >= 128) {
            xa_decode(samples, src, &c->status[0], &c->status[1], 
                avctx->channels);
            src += 128;
            samples += 28 * 8;
            buf_size -= 128;
        }
        break;
    default:
        *data_size = 0;
        return -1;
    }
    *data_size = (uint8_t *)samples - (uint8_t *)data;
    return src - buf;
}



#ifdef CONFIG_ENCODERS
#define ADPCM_ENCODER(id,name)                  \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(ADPCMContext),                       \
    adpcm_encode_init,                          \
    adpcm_encode_frame,                         \
    adpcm_encode_close,                         \
    NULL,                                       \
};
#else
#define ADPCM_ENCODER(id,name)
#endif

#ifdef CONFIG_DECODERS
#define ADPCM_DECODER(id,name)                  \
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
#else
#define ADPCM_DECODER(id,name)
#endif

#define ADPCM_CODEC(id, name)                   \
ADPCM_ENCODER(id,name) ADPCM_DECODER(id,name)

ADPCM_CODEC(CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt);
ADPCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
ADPCM_CODEC(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3);
ADPCM_CODEC(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4);
ADPCM_CODEC(CODEC_ID_ADPCM_IMA_WS, adpcm_ima_ws);
ADPCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);
ADPCM_CODEC(CODEC_ID_ADPCM_4XM, adpcm_4xm);
ADPCM_CODEC(CODEC_ID_ADPCM_XA, adpcm_xa);
ADPCM_CODEC(CODEC_ID_ADPCM_ADX, adpcm_adx);

#undef ADPCM_CODEC
