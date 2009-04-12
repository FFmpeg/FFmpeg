/*
 * ADPCM codecs
 * Copyright (c) 2001-2003 The ffmpeg Project
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
#include "avcodec.h"
#include "bitstream.h"
#include "put_bits.h"
#include "bytestream.h"

/**
 * @file libavcodec/adpcm.c
 * ADPCM codecs.
 * First version by Francois Revol (revol@free.fr)
 * Fringe ADPCM codecs (e.g., DK3, DK4, Westwood)
 *   by Mike Melanson (melanson@pcisys.net)
 * CD-ROM XA ADPCM codec by BERO
 * EA ADPCM decoder by Robin Kay (komadori@myrealbox.com)
 * EA ADPCM R1/R2/R3 decoder by Peter Ross (pross@xvid.org)
 * EA IMA EACS decoder by Peter Ross (pross@xvid.org)
 * EA IMA SEAD decoder by Peter Ross (pross@xvid.org)
 * EA ADPCM XAS decoder by Peter Ross (pross@xvid.org)
 * MAXIS EA ADPCM decoder by Robert Marston (rmarston@gmail.com)
 * THP ADPCM decoder by Marco Gerards (mgerards@xs4all.nl)
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

static const uint8_t AdaptCoeff1[] = {
        64, 128, 0, 48, 60, 115, 98
};

static const int8_t AdaptCoeff2[] = {
        0, -64, 0, 16, 0, -52, -58
};

/* These are for CD-ROM XA ADPCM */
static const int xa_adpcm_table[5][2] = {
   {   0,   0 },
   {  60,   0 },
   { 115, -52 },
   {  98, -55 },
   { 122, -60 }
};

static const int ea_adpcm_table[] = {
    0, 240, 460, 392, 0, 0, -208, -220, 0, 1,
    3, 4, 7, 8, 10, 11, 0, -1, -3, -4
};

// padded to zero where table size is less then 16
static const int swf_index_tables[4][16] = {
    /*2*/ { -1, 2 },
    /*3*/ { -1, -1, 2, 4 },
    /*4*/ { -1, -1, -1, -1, 2, 4, 6, 8 },
    /*5*/ { -1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16 }
};

static const int yamaha_indexscale[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    230, 230, 230, 230, 307, 409, 512, 614
};

static const int yamaha_difflookup[] = {
    1, 3, 5, 7, 9, 11, 13, 15,
    -1, -3, -5, -7, -9, -11, -13, -15
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
    ADPCMChannelStatus status[6];
} ADPCMContext;

/* XXX: implement encoding */

#if CONFIG_ENCODERS
static av_cold int adpcm_encode_init(AVCodecContext *avctx)
{
    if (avctx->channels > 2)
        return -1; /* only stereo or mono =) */

    if(avctx->trellis && (unsigned)avctx->trellis > 16U){
        av_log(avctx, AV_LOG_ERROR, "invalid trellis size\n");
        return -1;
    }

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_WAV:
        avctx->frame_size = (BLKSIZE - 4 * avctx->channels) * 8 / (4 * avctx->channels) + 1; /* each 16 bits sample gives one nibble */
                                                             /* and we have 4 bytes per channel overhead */
        avctx->block_align = BLKSIZE;
        /* seems frame_size isn't taken into account... have to buffer the samples :-( */
        break;
    case CODEC_ID_ADPCM_IMA_QT:
        avctx->frame_size = 64;
        avctx->block_align = 34 * avctx->channels;
        break;
    case CODEC_ID_ADPCM_MS:
        avctx->frame_size = (BLKSIZE - 7 * avctx->channels) * 2 / avctx->channels + 2; /* each 16 bits sample gives one nibble */
                                                             /* and we have 7 bytes per channel overhead */
        avctx->block_align = BLKSIZE;
        break;
    case CODEC_ID_ADPCM_YAMAHA:
        avctx->frame_size = BLKSIZE * avctx->channels;
        avctx->block_align = BLKSIZE;
        break;
    case CODEC_ID_ADPCM_SWF:
        if (avctx->sample_rate != 11025 &&
            avctx->sample_rate != 22050 &&
            avctx->sample_rate != 44100) {
            av_log(avctx, AV_LOG_ERROR, "Sample rate must be 11025, 22050 or 44100\n");
            return -1;
        }
        avctx->frame_size = 512 * (avctx->sample_rate / 11025);
        break;
    default:
        return -1;
        break;
    }

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static av_cold int adpcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}


static inline unsigned char adpcm_ima_compress_sample(ADPCMChannelStatus *c, short sample)
{
    int delta = sample - c->prev_sample;
    int nibble = FFMIN(7, abs(delta)*4/step_table[c->step_index]) + (delta<0)*8;
    c->prev_sample += ((step_table[c->step_index] * yamaha_difflookup[nibble]) / 8);
    c->prev_sample = av_clip_int16(c->prev_sample);
    c->step_index = av_clip(c->step_index + index_table[nibble], 0, 88);
    return nibble;
}

static inline unsigned char adpcm_ms_compress_sample(ADPCMChannelStatus *c, short sample)
{
    int predictor, nibble, bias;

    predictor = (((c->sample1) * (c->coeff1)) + ((c->sample2) * (c->coeff2))) / 64;

    nibble= sample - predictor;
    if(nibble>=0) bias= c->idelta/2;
    else          bias=-c->idelta/2;

    nibble= (nibble + bias) / c->idelta;
    nibble= av_clip(nibble, -8, 7)&0x0F;

    predictor += (signed)((nibble & 0x08)?(nibble - 0x10):(nibble)) * c->idelta;

    c->sample2 = c->sample1;
    c->sample1 = av_clip_int16(predictor);

    c->idelta = (AdaptationTable[(int)nibble] * c->idelta) >> 8;
    if (c->idelta < 16) c->idelta = 16;

    return nibble;
}

static inline unsigned char adpcm_yamaha_compress_sample(ADPCMChannelStatus *c, short sample)
{
    int nibble, delta;

    if(!c->step) {
        c->predictor = 0;
        c->step = 127;
    }

    delta = sample - c->predictor;

    nibble = FFMIN(7, abs(delta)*4/c->step) + (delta<0)*8;

    c->predictor += ((c->step * yamaha_difflookup[nibble]) / 8);
    c->predictor = av_clip_int16(c->predictor);
    c->step = (c->step * yamaha_indexscale[nibble]) >> 8;
    c->step = av_clip(c->step, 127, 24567);

    return nibble;
}

typedef struct TrellisPath {
    int nibble;
    int prev;
} TrellisPath;

typedef struct TrellisNode {
    uint32_t ssd;
    int path;
    int sample1;
    int sample2;
    int step;
} TrellisNode;

static void adpcm_compress_trellis(AVCodecContext *avctx, const short *samples,
                                   uint8_t *dst, ADPCMChannelStatus *c, int n)
{
#define FREEZE_INTERVAL 128
    //FIXME 6% faster if frontier is a compile-time constant
    const int frontier = 1 << avctx->trellis;
    const int stride = avctx->channels;
    const int version = avctx->codec->id;
    const int max_paths = frontier*FREEZE_INTERVAL;
    TrellisPath paths[max_paths], *p;
    TrellisNode node_buf[2][frontier];
    TrellisNode *nodep_buf[2][frontier];
    TrellisNode **nodes = nodep_buf[0]; // nodes[] is always sorted by .ssd
    TrellisNode **nodes_next = nodep_buf[1];
    int pathn = 0, froze = -1, i, j, k;

    assert(!(max_paths&(max_paths-1)));

    memset(nodep_buf, 0, sizeof(nodep_buf));
    nodes[0] = &node_buf[1][0];
    nodes[0]->ssd = 0;
    nodes[0]->path = 0;
    nodes[0]->step = c->step_index;
    nodes[0]->sample1 = c->sample1;
    nodes[0]->sample2 = c->sample2;
    if((version == CODEC_ID_ADPCM_IMA_WAV) || (version == CODEC_ID_ADPCM_IMA_QT) || (version == CODEC_ID_ADPCM_SWF))
        nodes[0]->sample1 = c->prev_sample;
    if(version == CODEC_ID_ADPCM_MS)
        nodes[0]->step = c->idelta;
    if(version == CODEC_ID_ADPCM_YAMAHA) {
        if(c->step == 0) {
            nodes[0]->step = 127;
            nodes[0]->sample1 = 0;
        } else {
            nodes[0]->step = c->step;
            nodes[0]->sample1 = c->predictor;
        }
    }

    for(i=0; i<n; i++) {
        TrellisNode *t = node_buf[i&1];
        TrellisNode **u;
        int sample = samples[i*stride];
        memset(nodes_next, 0, frontier*sizeof(TrellisNode*));
        for(j=0; j<frontier && nodes[j]; j++) {
            // higher j have higher ssd already, so they're unlikely to use a suboptimal next sample too
            const int range = (j < frontier/2) ? 1 : 0;
            const int step = nodes[j]->step;
            int nidx;
            if(version == CODEC_ID_ADPCM_MS) {
                const int predictor = ((nodes[j]->sample1 * c->coeff1) + (nodes[j]->sample2 * c->coeff2)) / 64;
                const int div = (sample - predictor) / step;
                const int nmin = av_clip(div-range, -8, 6);
                const int nmax = av_clip(div+range, -7, 7);
                for(nidx=nmin; nidx<=nmax; nidx++) {
                    const int nibble = nidx & 0xf;
                    int dec_sample = predictor + nidx * step;
#define STORE_NODE(NAME, STEP_INDEX)\
                    int d;\
                    uint32_t ssd;\
                    dec_sample = av_clip_int16(dec_sample);\
                    d = sample - dec_sample;\
                    ssd = nodes[j]->ssd + d*d;\
                    if(nodes_next[frontier-1] && ssd >= nodes_next[frontier-1]->ssd)\
                        continue;\
                    /* Collapse any two states with the same previous sample value. \
                     * One could also distinguish states by step and by 2nd to last
                     * sample, but the effects of that are negligible. */\
                    for(k=0; k<frontier && nodes_next[k]; k++) {\
                        if(dec_sample == nodes_next[k]->sample1) {\
                            assert(ssd >= nodes_next[k]->ssd);\
                            goto next_##NAME;\
                        }\
                    }\
                    for(k=0; k<frontier; k++) {\
                        if(!nodes_next[k] || ssd < nodes_next[k]->ssd) {\
                            TrellisNode *u = nodes_next[frontier-1];\
                            if(!u) {\
                                assert(pathn < max_paths);\
                                u = t++;\
                                u->path = pathn++;\
                            }\
                            u->ssd = ssd;\
                            u->step = STEP_INDEX;\
                            u->sample2 = nodes[j]->sample1;\
                            u->sample1 = dec_sample;\
                            paths[u->path].nibble = nibble;\
                            paths[u->path].prev = nodes[j]->path;\
                            memmove(&nodes_next[k+1], &nodes_next[k], (frontier-k-1)*sizeof(TrellisNode*));\
                            nodes_next[k] = u;\
                            break;\
                        }\
                    }\
                    next_##NAME:;
                    STORE_NODE(ms, FFMAX(16, (AdaptationTable[nibble] * step) >> 8));
                }
            } else if((version == CODEC_ID_ADPCM_IMA_WAV)|| (version == CODEC_ID_ADPCM_IMA_QT)|| (version == CODEC_ID_ADPCM_SWF)) {
#define LOOP_NODES(NAME, STEP_TABLE, STEP_INDEX)\
                const int predictor = nodes[j]->sample1;\
                const int div = (sample - predictor) * 4 / STEP_TABLE;\
                int nmin = av_clip(div-range, -7, 6);\
                int nmax = av_clip(div+range, -6, 7);\
                if(nmin<=0) nmin--; /* distinguish -0 from +0 */\
                if(nmax<0) nmax--;\
                for(nidx=nmin; nidx<=nmax; nidx++) {\
                    const int nibble = nidx<0 ? 7-nidx : nidx;\
                    int dec_sample = predictor + (STEP_TABLE * yamaha_difflookup[nibble]) / 8;\
                    STORE_NODE(NAME, STEP_INDEX);\
                }
                LOOP_NODES(ima, step_table[step], av_clip(step + index_table[nibble], 0, 88));
            } else { //CODEC_ID_ADPCM_YAMAHA
                LOOP_NODES(yamaha, step, av_clip((step * yamaha_indexscale[nibble]) >> 8, 127, 24567));
#undef LOOP_NODES
#undef STORE_NODE
            }
        }

        u = nodes;
        nodes = nodes_next;
        nodes_next = u;

        // prevent overflow
        if(nodes[0]->ssd > (1<<28)) {
            for(j=1; j<frontier && nodes[j]; j++)
                nodes[j]->ssd -= nodes[0]->ssd;
            nodes[0]->ssd = 0;
        }

        // merge old paths to save memory
        if(i == froze + FREEZE_INTERVAL) {
            p = &paths[nodes[0]->path];
            for(k=i; k>froze; k--) {
                dst[k] = p->nibble;
                p = &paths[p->prev];
            }
            froze = i;
            pathn = 0;
            // other nodes might use paths that don't coincide with the frozen one.
            // checking which nodes do so is too slow, so just kill them all.
            // this also slightly improves quality, but I don't know why.
            memset(nodes+1, 0, (frontier-1)*sizeof(TrellisNode*));
        }
    }

    p = &paths[nodes[0]->path];
    for(i=n-1; i>froze; i--) {
        dst[i] = p->nibble;
        p = &paths[p->prev];
    }

    c->predictor = nodes[0]->sample1;
    c->sample1 = nodes[0]->sample1;
    c->sample2 = nodes[0]->sample2;
    c->step_index = nodes[0]->step;
    c->step = nodes[0]->step;
    c->idelta = nodes[0]->step;
}

static int adpcm_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    int n, i, st;
    short *samples;
    unsigned char *dst;
    ADPCMContext *c = avctx->priv_data;

    dst = frame;
    samples = (short *)data;
    st= avctx->channels == 2;
/*    n = (BLKSIZE - 4 * avctx->channels) / (2 * 8 * avctx->channels); */

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_WAV:
        n = avctx->frame_size / 8;
            c->status[0].prev_sample = (signed short)samples[0]; /* XXX */
/*            c->status[0].step_index = 0; *//* XXX: not sure how to init the state machine */
            bytestream_put_le16(&dst, c->status[0].prev_sample);
            *dst++ = (unsigned char)c->status[0].step_index;
            *dst++ = 0; /* unknown */
            samples++;
            if (avctx->channels == 2) {
                c->status[1].prev_sample = (signed short)samples[0];
/*                c->status[1].step_index = 0; */
                bytestream_put_le16(&dst, c->status[1].prev_sample);
                *dst++ = (unsigned char)c->status[1].step_index;
                *dst++ = 0;
                samples++;
            }

            /* stereo: 4 bytes (8 samples) for left, 4 bytes for right, 4 bytes left, ... */
            if(avctx->trellis > 0) {
                uint8_t buf[2][n*8];
                adpcm_compress_trellis(avctx, samples, buf[0], &c->status[0], n*8);
                if(avctx->channels == 2)
                    adpcm_compress_trellis(avctx, samples+1, buf[1], &c->status[1], n*8);
                for(i=0; i<n; i++) {
                    *dst++ = buf[0][8*i+0] | (buf[0][8*i+1] << 4);
                    *dst++ = buf[0][8*i+2] | (buf[0][8*i+3] << 4);
                    *dst++ = buf[0][8*i+4] | (buf[0][8*i+5] << 4);
                    *dst++ = buf[0][8*i+6] | (buf[0][8*i+7] << 4);
                    if (avctx->channels == 2) {
                        *dst++ = buf[1][8*i+0] | (buf[1][8*i+1] << 4);
                        *dst++ = buf[1][8*i+2] | (buf[1][8*i+3] << 4);
                        *dst++ = buf[1][8*i+4] | (buf[1][8*i+5] << 4);
                        *dst++ = buf[1][8*i+6] | (buf[1][8*i+7] << 4);
                    }
                }
            } else
            for (; n>0; n--) {
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[0]);
                *dst |= adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels]) << 4;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 2]);
                *dst |= adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 3]) << 4;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 4]);
                *dst |= adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 5]) << 4;
                dst++;
                *dst = adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 6]);
                *dst |= adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels * 7]) << 4;
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
    case CODEC_ID_ADPCM_IMA_QT:
    {
        int ch, i;
        PutBitContext pb;
        init_put_bits(&pb, dst, buf_size*8);

        for(ch=0; ch<avctx->channels; ch++){
            put_bits(&pb, 9, (c->status[ch].prev_sample + 0x10000) >> 7);
            put_bits(&pb, 7, c->status[ch].step_index);
            if(avctx->trellis > 0) {
                uint8_t buf[64];
                adpcm_compress_trellis(avctx, samples+ch, buf, &c->status[ch], 64);
                for(i=0; i<64; i++)
                    put_bits(&pb, 4, buf[i^1]);
                c->status[ch].prev_sample = c->status[ch].predictor & ~0x7F;
            } else {
                for (i=0; i<64; i+=2){
                    int t1, t2;
                    t1 = adpcm_ima_compress_sample(&c->status[ch], samples[avctx->channels*(i+0)+ch]);
                    t2 = adpcm_ima_compress_sample(&c->status[ch], samples[avctx->channels*(i+1)+ch]);
                    put_bits(&pb, 4, t2);
                    put_bits(&pb, 4, t1);
                }
                c->status[ch].prev_sample &= ~0x7F;
            }
        }

        dst += put_bits_count(&pb)>>3;
        break;
    }
    case CODEC_ID_ADPCM_SWF:
    {
        int i;
        PutBitContext pb;
        init_put_bits(&pb, dst, buf_size*8);

        n = avctx->frame_size-1;

        //Store AdpcmCodeSize
        put_bits(&pb, 2, 2);                //Set 4bits flash adpcm format

        //Init the encoder state
        for(i=0; i<avctx->channels; i++){
            c->status[i].step_index = av_clip(c->status[i].step_index, 0, 63); // clip step so it fits 6 bits
            put_sbits(&pb, 16, samples[i]);
            put_bits(&pb, 6, c->status[i].step_index);
            c->status[i].prev_sample = (signed short)samples[i];
        }

        if(avctx->trellis > 0) {
            uint8_t buf[2][n];
            adpcm_compress_trellis(avctx, samples+2, buf[0], &c->status[0], n);
            if (avctx->channels == 2)
                adpcm_compress_trellis(avctx, samples+3, buf[1], &c->status[1], n);
            for(i=0; i<n; i++) {
                put_bits(&pb, 4, buf[0][i]);
                if (avctx->channels == 2)
                    put_bits(&pb, 4, buf[1][i]);
            }
        } else {
            for (i=1; i<avctx->frame_size; i++) {
                put_bits(&pb, 4, adpcm_ima_compress_sample(&c->status[0], samples[avctx->channels*i]));
                if (avctx->channels == 2)
                    put_bits(&pb, 4, adpcm_ima_compress_sample(&c->status[1], samples[2*i+1]));
            }
        }
        flush_put_bits(&pb);
        dst += put_bits_count(&pb)>>3;
        break;
    }
    case CODEC_ID_ADPCM_MS:
        for(i=0; i<avctx->channels; i++){
            int predictor=0;

            *dst++ = predictor;
            c->status[i].coeff1 = AdaptCoeff1[predictor];
            c->status[i].coeff2 = AdaptCoeff2[predictor];
        }
        for(i=0; i<avctx->channels; i++){
            if (c->status[i].idelta < 16)
                c->status[i].idelta = 16;

            bytestream_put_le16(&dst, c->status[i].idelta);
        }
        for(i=0; i<avctx->channels; i++){
            c->status[i].sample2= *samples++;
        }
        for(i=0; i<avctx->channels; i++){
            c->status[i].sample1= *samples++;

            bytestream_put_le16(&dst, c->status[i].sample1);
        }
        for(i=0; i<avctx->channels; i++)
            bytestream_put_le16(&dst, c->status[i].sample2);

        if(avctx->trellis > 0) {
            int n = avctx->block_align - 7*avctx->channels;
            uint8_t buf[2][n];
            if(avctx->channels == 1) {
                n *= 2;
                adpcm_compress_trellis(avctx, samples, buf[0], &c->status[0], n);
                for(i=0; i<n; i+=2)
                    *dst++ = (buf[0][i] << 4) | buf[0][i+1];
            } else {
                adpcm_compress_trellis(avctx, samples, buf[0], &c->status[0], n);
                adpcm_compress_trellis(avctx, samples+1, buf[1], &c->status[1], n);
                for(i=0; i<n; i++)
                    *dst++ = (buf[0][i] << 4) | buf[1][i];
            }
        } else
        for(i=7*avctx->channels; i<avctx->block_align; i++) {
            int nibble;
            nibble = adpcm_ms_compress_sample(&c->status[ 0], *samples++)<<4;
            nibble|= adpcm_ms_compress_sample(&c->status[st], *samples++);
            *dst++ = nibble;
        }
        break;
    case CODEC_ID_ADPCM_YAMAHA:
        n = avctx->frame_size / 2;
        if(avctx->trellis > 0) {
            uint8_t buf[2][n*2];
            n *= 2;
            if(avctx->channels == 1) {
                adpcm_compress_trellis(avctx, samples, buf[0], &c->status[0], n);
                for(i=0; i<n; i+=2)
                    *dst++ = buf[0][i] | (buf[0][i+1] << 4);
            } else {
                adpcm_compress_trellis(avctx, samples, buf[0], &c->status[0], n);
                adpcm_compress_trellis(avctx, samples+1, buf[1], &c->status[1], n);
                for(i=0; i<n; i++)
                    *dst++ = buf[0][i] | (buf[1][i] << 4);
            }
        } else
        for (; n>0; n--) {
            for(i = 0; i < avctx->channels; i++) {
                int nibble;
                nibble  = adpcm_yamaha_compress_sample(&c->status[i], samples[i]);
                nibble |= adpcm_yamaha_compress_sample(&c->status[i], samples[i+avctx->channels]) << 4;
                *dst++ = nibble;
            }
            samples += 2 * avctx->channels;
        }
        break;
    default:
        return -1;
    }
    return dst - frame;
}
#endif //CONFIG_ENCODERS

static av_cold int adpcm_decode_init(AVCodecContext * avctx)
{
    ADPCMContext *c = avctx->priv_data;
    unsigned int max_channels = 2;

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_EA_R1:
    case CODEC_ID_ADPCM_EA_R2:
    case CODEC_ID_ADPCM_EA_R3:
        max_channels = 6;
        break;
    }
    if(avctx->channels > max_channels){
        return -1;
    }

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_CT:
        c->status[0].step = c->status[1].step = 511;
        break;
    case CODEC_ID_ADPCM_IMA_WS:
        if (avctx->extradata && avctx->extradata_size == 2 * 4) {
            c->status[0].predictor = AV_RL32(avctx->extradata);
            c->status[1].predictor = AV_RL32(avctx->extradata + 4);
        }
        break;
    default:
        break;
    }
    avctx->sample_fmt = SAMPLE_FMT_S16;
    return 0;
}

static inline short adpcm_ima_expand_nibble(ADPCMChannelStatus *c, char nibble, int shift)
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
    diff = ((2 * delta + 1) * step) >> shift;
    predictor = c->predictor;
    if (sign) predictor -= diff;
    else predictor += diff;

    c->predictor = av_clip_int16(predictor);
    c->step_index = step_index;

    return (short)c->predictor;
}

static inline short adpcm_ms_expand_nibble(ADPCMChannelStatus *c, char nibble)
{
    int predictor;

    predictor = (((c->sample1) * (c->coeff1)) + ((c->sample2) * (c->coeff2))) / 64;
    predictor += (signed)((nibble & 0x08)?(nibble - 0x10):(nibble)) * c->idelta;

    c->sample2 = c->sample1;
    c->sample1 = av_clip_int16(predictor);
    c->idelta = (AdaptationTable[(int)nibble] * c->idelta) >> 8;
    if (c->idelta < 16) c->idelta = 16;

    return c->sample1;
}

static inline short adpcm_ct_expand_nibble(ADPCMChannelStatus *c, char nibble)
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
    new_step = (AdaptationTable[nibble & 7] * c->step) >> 8;
    c->step = av_clip(new_step, 511, 32767);

    return (short)c->predictor;
}

static inline short adpcm_sbpro_expand_nibble(ADPCMChannelStatus *c, char nibble, int size, int shift)
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

    return (short) c->predictor;
}

static inline short adpcm_yamaha_expand_nibble(ADPCMChannelStatus *c, unsigned char nibble)
{
    if(!c->step) {
        c->predictor = 0;
        c->step = 127;
    }

    c->predictor += (c->step * yamaha_difflookup[nibble]) / 8;
    c->predictor = av_clip_int16(c->predictor);
    c->step = (c->step * yamaha_indexscale[nibble]) >> 8;
    c->step = av_clip(c->step, 127, 24567);
    return c->predictor;
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
            s_2 = s_1;
            s_1 = av_clip_int16(s);
            *out = s_1;
            out += inc;
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
            s_2 = s_1;
            s_1 = av_clip_int16(s);
            *out = s_1;
            out += inc;
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
        nibble = last_byte >> 4; \
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
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ADPCMContext *c = avctx->priv_data;
    ADPCMChannelStatus *cs;
    int n, m, channel, i;
    int block_predictor[2];
    short *samples;
    short *samples_end;
    const uint8_t *src;
    int st; /* stereo */

    /* DK3 ADPCM accounting variables */
    unsigned char last_byte = 0;
    unsigned char nibble;
    int decode_top_nibble_next = 0;
    int diff_channel;

    /* EA ADPCM state variables */
    uint32_t samples_in_chunk;
    int32_t previous_left_sample, previous_right_sample;
    int32_t current_left_sample, current_right_sample;
    int32_t next_left_sample, next_right_sample;
    int32_t coeff1l, coeff2l, coeff1r, coeff2r;
    uint8_t shift_left, shift_right;
    int count1, count2;
    int coeff[2][2], shift[2];//used in EA MAXIS ADPCM

    if (!buf_size)
        return 0;

    //should protect all 4bit ADPCM variants
    //8 is needed for CODEC_ID_ADPCM_IMA_WAV with 2 channels
    //
    if(*data_size/4 < buf_size + 8)
        return -1;

    samples = data;
    samples_end= samples + *data_size/2;
    *data_size= 0;
    src = buf;

    st = avctx->channels == 2 ? 1 : 0;

    switch(avctx->codec->id) {
    case CODEC_ID_ADPCM_IMA_QT:
        n = buf_size - 2*avctx->channels;
        for (channel = 0; channel < avctx->channels; channel++) {
            cs = &(c->status[channel]);
            /* (pppppp) (piiiiiii) */

            /* Bits 15-7 are the _top_ 9 bits of the 16-bit initial predictor value */
            cs->predictor = (*src++) << 8;
            cs->predictor |= (*src & 0x80);
            cs->predictor &= 0xFF80;

            /* sign extension */
            if(cs->predictor & 0x8000)
                cs->predictor -= 0x10000;

            cs->predictor = av_clip_int16(cs->predictor);

            cs->step_index = (*src++) & 0x7F;

            if (cs->step_index > 88){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i\n", cs->step_index);
                cs->step_index = 88;
            }

            cs->step = step_table[cs->step_index];

            samples = (short*)data + channel;

            for(m=32; n>0 && m>0; n--, m--) { /* in QuickTime, IMA is encoded by chuncks of 34 bytes (=64 samples) */
                *samples = adpcm_ima_expand_nibble(cs, src[0] & 0x0F, 3);
                samples += avctx->channels;
                *samples = adpcm_ima_expand_nibble(cs, src[0] >> 4  , 3);
                samples += avctx->channels;
                src ++;
            }
        }
        if (st)
            samples--;
        break;
    case CODEC_ID_ADPCM_IMA_WAV:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

//        samples_per_block= (block_align-4*chanels)*8 / (bits_per_sample * chanels) + 1;

        for(i=0; i<avctx->channels; i++){
            cs = &(c->status[i]);
            cs->predictor = *samples++ = (int16_t)bytestream_get_le16(&src);

            cs->step_index = *src++;
            if (cs->step_index > 88){
                av_log(avctx, AV_LOG_ERROR, "ERROR: step_index = %i\n", cs->step_index);
                cs->step_index = 88;
            }
            if (*src++) av_log(avctx, AV_LOG_ERROR, "unused byte should be null but is %d!!\n", src[-1]); /* unused */
        }

        while(src < buf + buf_size){
            for(m=0; m<4; m++){
                for(i=0; i<=st; i++)
                    *samples++ = adpcm_ima_expand_nibble(&c->status[i], src[4*i] & 0x0F, 3);
                for(i=0; i<=st; i++)
                    *samples++ = adpcm_ima_expand_nibble(&c->status[i], src[4*i] >> 4  , 3);
                src++;
            }
            src += 4*st;
        }
        break;
    case CODEC_ID_ADPCM_4XM:
        cs = &(c->status[0]);
        c->status[0].predictor= (int16_t)bytestream_get_le16(&src);
        if(st){
            c->status[1].predictor= (int16_t)bytestream_get_le16(&src);
        }
        c->status[0].step_index= (int16_t)bytestream_get_le16(&src);
        if(st){
            c->status[1].step_index= (int16_t)bytestream_get_le16(&src);
        }
        if (cs->step_index < 0) cs->step_index = 0;
        if (cs->step_index > 88) cs->step_index = 88;

        m= (buf_size - (src - buf))>>st;
        for(i=0; i<m; i++) {
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], src[i] & 0x0F, 4);
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], src[i+m] & 0x0F, 4);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], src[i] >> 4, 4);
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1], src[i+m] >> 4, 4);
        }

        src += m<<st;

        break;
    case CODEC_ID_ADPCM_MS:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;
        n = buf_size - 7 * avctx->channels;
        if (n < 0)
            return -1;
        block_predictor[0] = av_clip(*src++, 0, 6);
        block_predictor[1] = 0;
        if (st)
            block_predictor[1] = av_clip(*src++, 0, 6);
        c->status[0].idelta = (int16_t)bytestream_get_le16(&src);
        if (st){
            c->status[1].idelta = (int16_t)bytestream_get_le16(&src);
        }
        c->status[0].coeff1 = AdaptCoeff1[block_predictor[0]];
        c->status[0].coeff2 = AdaptCoeff2[block_predictor[0]];
        c->status[1].coeff1 = AdaptCoeff1[block_predictor[1]];
        c->status[1].coeff2 = AdaptCoeff2[block_predictor[1]];

        c->status[0].sample1 = bytestream_get_le16(&src);
        if (st) c->status[1].sample1 = bytestream_get_le16(&src);
        c->status[0].sample2 = bytestream_get_le16(&src);
        if (st) c->status[1].sample2 = bytestream_get_le16(&src);

        *samples++ = c->status[0].sample2;
        if (st) *samples++ = c->status[1].sample2;
        *samples++ = c->status[0].sample1;
        if (st) *samples++ = c->status[1].sample1;
        for(;n>0;n--) {
            *samples++ = adpcm_ms_expand_nibble(&c->status[0 ], src[0] >> 4  );
            *samples++ = adpcm_ms_expand_nibble(&c->status[st], src[0] & 0x0F);
            src ++;
        }
        break;
    case CODEC_ID_ADPCM_IMA_DK4:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

        c->status[0].predictor  = (int16_t)bytestream_get_le16(&src);
        c->status[0].step_index = *src++;
        src++;
        *samples++ = c->status[0].predictor;
        if (st) {
            c->status[1].predictor  = (int16_t)bytestream_get_le16(&src);
            c->status[1].step_index = *src++;
            src++;
            *samples++ = c->status[1].predictor;
        }
        while (src < buf + buf_size) {

            /* take care of the top nibble (always left or mono channel) */
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                src[0] >> 4, 3);

            /* take care of the bottom nibble, which is right sample for
             * stereo, or another mono sample */
            if (st)
                *samples++ = adpcm_ima_expand_nibble(&c->status[1],
                    src[0] & 0x0F, 3);
            else
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] & 0x0F, 3);

            src++;
        }
        break;
    case CODEC_ID_ADPCM_IMA_DK3:
        if (avctx->block_align != 0 && buf_size > avctx->block_align)
            buf_size = avctx->block_align;

        if(buf_size + 16 > (samples_end - samples)*3/8)
            return -1;

        c->status[0].predictor  = (int16_t)AV_RL16(src + 10);
        c->status[1].predictor  = (int16_t)AV_RL16(src + 12);
        c->status[0].step_index = src[14];
        c->status[1].step_index = src[15];
        /* sign extend the predictors */
        src += 16;
        diff_channel = c->status[1].predictor;

        /* the DK3_GET_NEXT_NIBBLE macro issues the break statement when
         * the buffer is consumed */
        while (1) {

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
        break;
    case CODEC_ID_ADPCM_IMA_ISS:
        c->status[0].predictor  = (int16_t)AV_RL16(src + 0);
        c->status[0].step_index = src[2];
        src += 4;
        if(st) {
            c->status[1].predictor  = (int16_t)AV_RL16(src + 0);
            c->status[1].step_index = src[2];
            src += 4;
        }

        while (src < buf + buf_size) {

            if (st) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] >> 4  , 3);
                *samples++ = adpcm_ima_expand_nibble(&c->status[1],
                    src[0] & 0x0F, 3);
            } else {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] & 0x0F, 3);
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] >> 4  , 3);
            }

            src++;
        }
        break;
    case CODEC_ID_ADPCM_IMA_WS:
        /* no per-block initialization; just start decoding the data */
        while (src < buf + buf_size) {

            if (st) {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] >> 4  , 3);
                *samples++ = adpcm_ima_expand_nibble(&c->status[1],
                    src[0] & 0x0F, 3);
            } else {
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] >> 4  , 3);
                *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                    src[0] & 0x0F, 3);
            }

            src++;
        }
        break;
    case CODEC_ID_ADPCM_XA:
        while (buf_size >= 128) {
            xa_decode(samples, src, &c->status[0], &c->status[1],
                avctx->channels);
            src += 128;
            samples += 28 * 8;
            buf_size -= 128;
        }
        break;
    case CODEC_ID_ADPCM_IMA_EA_EACS:
        samples_in_chunk = bytestream_get_le32(&src) >> (1-st);

        if (samples_in_chunk > buf_size-4-(8<<st)) {
            src += buf_size - 4;
            break;
        }

        for (i=0; i<=st; i++)
            c->status[i].step_index = bytestream_get_le32(&src);
        for (i=0; i<=st; i++)
            c->status[i].predictor  = bytestream_get_le32(&src);

        for (; samples_in_chunk; samples_in_chunk--, src++) {
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],  *src>>4,   3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st], *src&0x0F, 3);
        }
        break;
    case CODEC_ID_ADPCM_IMA_EA_SEAD:
        for (; src < buf+buf_size; src++) {
            *samples++ = adpcm_ima_expand_nibble(&c->status[0], src[0] >> 4, 6);
            *samples++ = adpcm_ima_expand_nibble(&c->status[st],src[0]&0x0F, 6);
        }
        break;
    case CODEC_ID_ADPCM_EA:
        samples_in_chunk = AV_RL32(src);
        if (samples_in_chunk >= ((buf_size - 12) * 2)) {
            src += buf_size;
            break;
        }
        src += 4;
        current_left_sample   = (int16_t)bytestream_get_le16(&src);
        previous_left_sample  = (int16_t)bytestream_get_le16(&src);
        current_right_sample  = (int16_t)bytestream_get_le16(&src);
        previous_right_sample = (int16_t)bytestream_get_le16(&src);

        for (count1 = 0; count1 < samples_in_chunk/28;count1++) {
            coeff1l = ea_adpcm_table[ *src >> 4       ];
            coeff2l = ea_adpcm_table[(*src >> 4  ) + 4];
            coeff1r = ea_adpcm_table[*src & 0x0F];
            coeff2r = ea_adpcm_table[(*src & 0x0F) + 4];
            src++;

            shift_left  = (*src >> 4  ) + 8;
            shift_right = (*src & 0x0F) + 8;
            src++;

            for (count2 = 0; count2 < 28; count2++) {
                next_left_sample  = (int32_t)((*src & 0xF0) << 24) >> shift_left;
                next_right_sample = (int32_t)((*src & 0x0F) << 28) >> shift_right;
                src++;

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
                *samples++ = (unsigned short)current_left_sample;
                *samples++ = (unsigned short)current_right_sample;
            }
        }
        break;
    case CODEC_ID_ADPCM_EA_MAXIS_XA:
        for(channel = 0; channel < avctx->channels; channel++) {
            for (i=0; i<2; i++)
                coeff[channel][i] = ea_adpcm_table[(*src >> 4) + 4*i];
            shift[channel] = (*src & 0x0F) + 8;
            src++;
        }
        for (count1 = 0; count1 < (buf_size - avctx->channels) / avctx->channels; count1++) {
            for(i = 4; i >= 0; i-=4) { /* Pairwise samples LL RR (st) or LL LL (mono) */
                for(channel = 0; channel < avctx->channels; channel++) {
                    int32_t sample = (int32_t)(((*(src+channel) >> i) & 0x0F) << 0x1C) >> shift[channel];
                    sample = (sample +
                             c->status[channel].sample1 * coeff[channel][0] +
                             c->status[channel].sample2 * coeff[channel][1] + 0x80) >> 8;
                    c->status[channel].sample2 = c->status[channel].sample1;
                    c->status[channel].sample1 = av_clip_int16(sample);
                    *samples++ = c->status[channel].sample1;
                }
            }
            src+=avctx->channels;
        }
        break;
    case CODEC_ID_ADPCM_EA_R1:
    case CODEC_ID_ADPCM_EA_R2:
    case CODEC_ID_ADPCM_EA_R3: {
        /* channel numbering
           2chan: 0=fl, 1=fr
           4chan: 0=fl, 1=rl, 2=fr, 3=rr
           6chan: 0=fl, 1=c,  2=fr, 3=rl,  4=rr, 5=sub */
        const int big_endian = avctx->codec->id == CODEC_ID_ADPCM_EA_R3;
        int32_t previous_sample, current_sample, next_sample;
        int32_t coeff1, coeff2;
        uint8_t shift;
        unsigned int channel;
        uint16_t *samplesC;
        const uint8_t *srcC;
        const uint8_t *src_end = buf + buf_size;

        samples_in_chunk = (big_endian ? bytestream_get_be32(&src)
                                       : bytestream_get_le32(&src)) / 28;
        if (samples_in_chunk > UINT32_MAX/(28*avctx->channels) ||
            28*samples_in_chunk*avctx->channels > samples_end-samples) {
            src += buf_size - 4;
            break;
        }

        for (channel=0; channel<avctx->channels; channel++) {
            int32_t offset = (big_endian ? bytestream_get_be32(&src)
                                         : bytestream_get_le32(&src))
                           + (avctx->channels-channel-1) * 4;

            if ((offset < 0) || (offset >= src_end - src - 4)) break;
            srcC  = src + offset;
            samplesC = samples + channel;

            if (avctx->codec->id == CODEC_ID_ADPCM_EA_R1) {
                current_sample  = (int16_t)bytestream_get_le16(&srcC);
                previous_sample = (int16_t)bytestream_get_le16(&srcC);
            } else {
                current_sample  = c->status[channel].predictor;
                previous_sample = c->status[channel].prev_sample;
            }

            for (count1=0; count1<samples_in_chunk; count1++) {
                if (*srcC == 0xEE) {  /* only seen in R2 and R3 */
                    srcC++;
                    if (srcC > src_end - 30*2) break;
                    current_sample  = (int16_t)bytestream_get_be16(&srcC);
                    previous_sample = (int16_t)bytestream_get_be16(&srcC);

                    for (count2=0; count2<28; count2++) {
                        *samplesC = (int16_t)bytestream_get_be16(&srcC);
                        samplesC += avctx->channels;
                    }
                } else {
                    coeff1 = ea_adpcm_table[ *srcC>>4     ];
                    coeff2 = ea_adpcm_table[(*srcC>>4) + 4];
                    shift = (*srcC++ & 0x0F) + 8;

                    if (srcC > src_end - 14) break;
                    for (count2=0; count2<28; count2++) {
                        if (count2 & 1)
                            next_sample = (int32_t)((*srcC++ & 0x0F) << 28) >> shift;
                        else
                            next_sample = (int32_t)((*srcC   & 0xF0) << 24) >> shift;

                        next_sample += (current_sample  * coeff1) +
                                       (previous_sample * coeff2);
                        next_sample = av_clip_int16(next_sample >> 8);

                        previous_sample = current_sample;
                        current_sample  = next_sample;
                        *samplesC = current_sample;
                        samplesC += avctx->channels;
                    }
                }
            }

            if (avctx->codec->id != CODEC_ID_ADPCM_EA_R1) {
                c->status[channel].predictor   = current_sample;
                c->status[channel].prev_sample = previous_sample;
            }
        }

        src = src + buf_size - (4 + 4*avctx->channels);
        samples += 28 * samples_in_chunk * avctx->channels;
        break;
    }
    case CODEC_ID_ADPCM_EA_XAS:
        if (samples_end-samples < 32*4*avctx->channels
            || buf_size < (4+15)*4*avctx->channels) {
            src += buf_size;
            break;
        }
        for (channel=0; channel<avctx->channels; channel++) {
            int coeff[2][4], shift[4];
            short *s2, *s = &samples[channel];
            for (n=0; n<4; n++, s+=32*avctx->channels) {
                for (i=0; i<2; i++)
                    coeff[i][n] = ea_adpcm_table[(src[0]&0x0F)+4*i];
                shift[n] = (src[2]&0x0F) + 8;
                for (s2=s, i=0; i<2; i++, src+=2, s2+=avctx->channels)
                    s2[0] = (src[0]&0xF0) + (src[1]<<8);
            }

            for (m=2; m<32; m+=2) {
                s = &samples[m*avctx->channels + channel];
                for (n=0; n<4; n++, src++, s+=32*avctx->channels) {
                    for (s2=s, i=0; i<8; i+=4, s2+=avctx->channels) {
                        int level = (int32_t)((*src & (0xF0>>i)) << (24+i)) >> shift[n];
                        int pred  = s2[-1*avctx->channels] * coeff[0][n]
                                  + s2[-2*avctx->channels] * coeff[1][n];
                        s2[0] = av_clip_int16((level + pred + 0x80) >> 8);
                    }
                }
            }
        }
        samples += 32*4*avctx->channels;
        break;
    case CODEC_ID_ADPCM_IMA_AMV:
    case CODEC_ID_ADPCM_IMA_SMJPEG:
        c->status[0].predictor = (int16_t)bytestream_get_le16(&src);
        c->status[0].step_index = bytestream_get_le16(&src);

        if (avctx->codec->id == CODEC_ID_ADPCM_IMA_AMV)
            src+=4;

        while (src < buf + buf_size) {
            char hi, lo;
            lo = *src & 0x0F;
            hi = *src >> 4;

            if (avctx->codec->id == CODEC_ID_ADPCM_IMA_AMV)
                FFSWAP(char, hi, lo);

            *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                lo, 3);
            *samples++ = adpcm_ima_expand_nibble(&c->status[0],
                hi, 3);
            src++;
        }
        break;
    case CODEC_ID_ADPCM_CT:
        while (src < buf + buf_size) {
            if (st) {
                *samples++ = adpcm_ct_expand_nibble(&c->status[0],
                    src[0] >> 4);
                *samples++ = adpcm_ct_expand_nibble(&c->status[1],
                    src[0] & 0x0F);
            } else {
                *samples++ = adpcm_ct_expand_nibble(&c->status[0],
                    src[0] >> 4);
                *samples++ = adpcm_ct_expand_nibble(&c->status[0],
                    src[0] & 0x0F);
            }
            src++;
        }
        break;
    case CODEC_ID_ADPCM_SBPRO_4:
    case CODEC_ID_ADPCM_SBPRO_3:
    case CODEC_ID_ADPCM_SBPRO_2:
        if (!c->status[0].step_index) {
            /* the first byte is a raw sample */
            *samples++ = 128 * (*src++ - 0x80);
            if (st)
              *samples++ = 128 * (*src++ - 0x80);
            c->status[0].step_index = 1;
        }
        if (avctx->codec->id == CODEC_ID_ADPCM_SBPRO_4) {
            while (src < buf + buf_size) {
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                    src[0] >> 4, 4, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                    src[0] & 0x0F, 4, 0);
                src++;
            }
        } else if (avctx->codec->id == CODEC_ID_ADPCM_SBPRO_3) {
            while (src < buf + buf_size && samples + 2 < samples_end) {
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                     src[0] >> 5        , 3, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                    (src[0] >> 2) & 0x07, 3, 0);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                    src[0] & 0x03, 2, 0);
                src++;
            }
        } else {
            while (src < buf + buf_size && samples + 3 < samples_end) {
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                     src[0] >> 6        , 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                    (src[0] >> 4) & 0x03, 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[0],
                    (src[0] >> 2) & 0x03, 2, 2);
                *samples++ = adpcm_sbpro_expand_nibble(&c->status[st],
                    src[0] & 0x03, 2, 2);
                src++;
            }
        }
        break;
    case CODEC_ID_ADPCM_SWF:
    {
        GetBitContext gb;
        const int *table;
        int k0, signmask, nb_bits, count;
        int size = buf_size*8;

        init_get_bits(&gb, buf, size);

        //read bits & initial values
        nb_bits = get_bits(&gb, 2)+2;
        //av_log(NULL,AV_LOG_INFO,"nb_bits: %d\n", nb_bits);
        table = swf_index_tables[nb_bits-2];
        k0 = 1 << (nb_bits-2);
        signmask = 1 << (nb_bits-1);

        while (get_bits_count(&gb) <= size - 22*avctx->channels) {
            for (i = 0; i < avctx->channels; i++) {
                *samples++ = c->status[i].predictor = get_sbits(&gb, 16);
                c->status[i].step_index = get_bits(&gb, 6);
            }

            for (count = 0; get_bits_count(&gb) <= size - nb_bits*avctx->channels && count < 4095; count++) {
                int i;

                for (i = 0; i < avctx->channels; i++) {
                    // similar to IMA adpcm
                    int delta = get_bits(&gb, nb_bits);
                    int step = step_table[c->status[i].step_index];
                    long vpdiff = 0; // vpdiff = (delta+0.5)*step/4
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
                    if (samples >= samples_end) {
                        av_log(avctx, AV_LOG_ERROR, "allocated output buffer is too small\n");
                        return -1;
                    }
                }
            }
        }
        src += buf_size;
        break;
    }
    case CODEC_ID_ADPCM_YAMAHA:
        while (src < buf + buf_size) {
            if (st) {
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[0],
                        src[0] & 0x0F);
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[1],
                        src[0] >> 4  );
            } else {
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[0],
                        src[0] & 0x0F);
                *samples++ = adpcm_yamaha_expand_nibble(&c->status[0],
                        src[0] >> 4  );
            }
            src++;
        }
        break;
    case CODEC_ID_ADPCM_THP:
    {
        int table[2][16];
        unsigned int samplecnt;
        int prev[2][2];
        int ch;

        if (buf_size < 80) {
            av_log(avctx, AV_LOG_ERROR, "frame too small\n");
            return -1;
        }

        src+=4;
        samplecnt = bytestream_get_be32(&src);

        for (i = 0; i < 32; i++)
            table[0][i] = (int16_t)bytestream_get_be16(&src);

        /* Initialize the previous sample.  */
        for (i = 0; i < 4; i++)
            prev[0][i] = (int16_t)bytestream_get_be16(&src);

        if (samplecnt >= (samples_end - samples) /  (st + 1)) {
            av_log(avctx, AV_LOG_ERROR, "allocated output buffer is too small\n");
            return -1;
        }

        for (ch = 0; ch <= st; ch++) {
            samples = (unsigned short *) data + ch;

            /* Read in every sample for this channel.  */
            for (i = 0; i < samplecnt / 14; i++) {
                int index = (*src >> 4) & 7;
                unsigned int exp = 28 - (*src++ & 15);
                int factor1 = table[ch][index * 2];
                int factor2 = table[ch][index * 2 + 1];

                /* Decode 14 samples.  */
                for (n = 0; n < 14; n++) {
                    int32_t sampledat;
                    if(n&1) sampledat=  *src++    <<28;
                    else    sampledat= (*src&0xF0)<<24;

                    sampledat = ((prev[ch][0]*factor1
                                + prev[ch][1]*factor2) >> 11) + (sampledat>>exp);
                    *samples = av_clip_int16(sampledat);
                    prev[ch][1] = prev[ch][0];
                    prev[ch][0] = *samples++;

                    /* In case of stereo, skip one sample, this sample
                       is for the other channel.  */
                    samples += st;
                }
            }
        }

        /* In the previous loop, in case stereo is used, samples is
           increased exactly one time too often.  */
        samples -= st;
        break;
    }

    default:
        return -1;
    }
    *data_size = (uint8_t *)samples - (uint8_t *)data;
    return src - buf;
}



#if CONFIG_ENCODERS
#define ADPCM_ENCODER(id,name,long_name_)       \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(ADPCMContext),                       \
    adpcm_encode_init,                          \
    adpcm_encode_frame,                         \
    adpcm_encode_close,                         \
    NULL,                                       \
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE}, \
    .long_name = NULL_IF_CONFIG_SMALL(long_name_), \
};
#else
#define ADPCM_ENCODER(id,name,long_name_)
#endif

#if CONFIG_DECODERS
#define ADPCM_DECODER(id,name,long_name_)       \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(ADPCMContext),                       \
    adpcm_decode_init,                          \
    NULL,                                       \
    NULL,                                       \
    adpcm_decode_frame,                         \
    .long_name = NULL_IF_CONFIG_SMALL(long_name_), \
};
#else
#define ADPCM_DECODER(id,name,long_name_)
#endif

#define ADPCM_CODEC(id,name,long_name_)         \
    ADPCM_ENCODER(id,name,long_name_) ADPCM_DECODER(id,name,long_name_)

/* Note: Do not forget to add new entries to the Makefile as well. */
ADPCM_DECODER(CODEC_ID_ADPCM_4XM, adpcm_4xm, "ADPCM 4X Movie");
ADPCM_DECODER(CODEC_ID_ADPCM_CT, adpcm_ct, "ADPCM Creative Technology");
ADPCM_DECODER(CODEC_ID_ADPCM_EA, adpcm_ea, "ADPCM Electronic Arts");
ADPCM_DECODER(CODEC_ID_ADPCM_EA_MAXIS_XA, adpcm_ea_maxis_xa, "ADPCM Electronic Arts Maxis CDROM XA");
ADPCM_DECODER(CODEC_ID_ADPCM_EA_R1, adpcm_ea_r1, "ADPCM Electronic Arts R1");
ADPCM_DECODER(CODEC_ID_ADPCM_EA_R2, adpcm_ea_r2, "ADPCM Electronic Arts R2");
ADPCM_DECODER(CODEC_ID_ADPCM_EA_R3, adpcm_ea_r3, "ADPCM Electronic Arts R3");
ADPCM_DECODER(CODEC_ID_ADPCM_EA_XAS, adpcm_ea_xas, "ADPCM Electronic Arts XAS");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_AMV, adpcm_ima_amv, "ADPCM IMA AMV");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3, "ADPCM IMA Duck DK3");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4, "ADPCM IMA Duck DK4");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_EA_EACS, adpcm_ima_ea_eacs, "ADPCM IMA Electronic Arts EACS");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_EA_SEAD, adpcm_ima_ea_sead, "ADPCM IMA Electronic Arts SEAD");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_ISS, adpcm_ima_iss, "ADPCM IMA Funcom ISS");
ADPCM_CODEC  (CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt, "ADPCM IMA QuickTime");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_SMJPEG, adpcm_ima_smjpeg, "ADPCM IMA Loki SDL MJPEG");
ADPCM_CODEC  (CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav, "ADPCM IMA WAV");
ADPCM_DECODER(CODEC_ID_ADPCM_IMA_WS, adpcm_ima_ws, "ADPCM IMA Westwood");
ADPCM_CODEC  (CODEC_ID_ADPCM_MS, adpcm_ms, "ADPCM Microsoft");
ADPCM_DECODER(CODEC_ID_ADPCM_SBPRO_2, adpcm_sbpro_2, "ADPCM Sound Blaster Pro 2-bit");
ADPCM_DECODER(CODEC_ID_ADPCM_SBPRO_3, adpcm_sbpro_3, "ADPCM Sound Blaster Pro 2.6-bit");
ADPCM_DECODER(CODEC_ID_ADPCM_SBPRO_4, adpcm_sbpro_4, "ADPCM Sound Blaster Pro 4-bit");
ADPCM_CODEC  (CODEC_ID_ADPCM_SWF, adpcm_swf, "ADPCM Shockwave Flash");
ADPCM_DECODER(CODEC_ID_ADPCM_THP, adpcm_thp, "ADPCM Nintendo Gamecube THP");
ADPCM_DECODER(CODEC_ID_ADPCM_XA, adpcm_xa, "ADPCM CDROM XA");
ADPCM_CODEC  (CODEC_ID_ADPCM_YAMAHA, adpcm_yamaha, "ADPCM Yamaha");
