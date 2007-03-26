/*
 * WMA compatible encoder
 * Copyright (c) 2007 Michael Niedermayer
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
#include "wma.h"

#undef NDEBUG
#include <assert.h>


static int encode_init(AVCodecContext * avctx){
    WMACodecContext *s = avctx->priv_data;
    int i, flags1, flags2;
    uint8_t *extradata;

    s->avctx = avctx;

    if(avctx->channels > MAX_CHANNELS)
        return -1;

    if(avctx->bit_rate < 24*1000)
        return -1;

    /* extract flag infos */
    flags1 = 0;
    flags2 = 1;
    if (avctx->codec->id == CODEC_ID_WMAV1) {
        extradata= av_malloc(4);
        avctx->extradata_size= 4;
        extradata[0] = flags1;
        extradata[1] = flags1>>8;
        extradata[2] = flags2;
        extradata[3] = flags2>>8;
    } else if (avctx->codec->id == CODEC_ID_WMAV2) {
        extradata= av_mallocz(10);
        avctx->extradata_size= 10;
        extradata[0] = flags1;
        extradata[1] = flags1>>8;
        extradata[2] = flags1>>16;
        extradata[3] = flags1>>24;
        extradata[4] = flags2;
        extradata[5] = flags2>>8;
    }else
        assert(0);
    avctx->extradata= extradata;
    s->use_exp_vlc = flags2 & 0x0001;
    s->use_bit_reservoir = flags2 & 0x0002;
    s->use_variable_block_len = flags2 & 0x0004;

    ff_wma_init(avctx, flags2);

    /* init MDCT */
    for(i = 0; i < s->nb_block_sizes; i++)
        ff_mdct_init(&s->mdct_ctx[i], s->frame_len_bits - i + 1, 0);

    avctx->block_align=
    s->block_align= avctx->bit_rate*(int64_t)s->frame_len / (avctx->sample_rate*8);
//av_log(NULL, AV_LOG_ERROR, "%d %d %d %d\n", s->block_align, avctx->bit_rate, s->frame_len, avctx->sample_rate);
    avctx->frame_size= s->frame_len;

    return 0;
}


static void apply_window_and_mdct(AVCodecContext * avctx, signed short * audio, int len) {
    WMACodecContext *s = avctx->priv_data;
    int window_index= s->frame_len_bits - s->block_len_bits;
    int i, j, channel;
    const float * win = s->windows[window_index];
    int window_len = 1 << s->block_len_bits;
    float n = window_len/2;

    for (channel = 0; channel < avctx->channels; channel++) {
        memcpy(s->output, s->frame_out[channel], sizeof(float)*window_len);
        j = channel;
        for (i = 0; i < len; i++, j += avctx->channels){
            s->output[i+window_len]  = audio[j] / n * win[window_len - i - 1];
            s->frame_out[channel][i] = audio[j] / n * win[i];
        }
        ff_mdct_calc(&s->mdct_ctx[window_index], s->coefs[channel], s->output, s->mdct_tmp);
    }
}

//FIXME use for decoding too
static void init_exp(WMACodecContext *s, int ch, int *exp_param){
    int n;
    const uint16_t *ptr;
    float v, *q, max_scale, *q_end;

    ptr = s->exponent_bands[s->frame_len_bits - s->block_len_bits];
    q = s->exponents[ch];
    q_end = q + s->block_len;
    max_scale = 0;
    while (q < q_end) {
        /* XXX: use a table */
        v = pow(10, *exp_param++ * (1.0 / 16.0));
        max_scale= FFMAX(max_scale, v);
        n = *ptr++;
        do {
            *q++ = v;
        } while (--n);
    }
    s->max_exponent[ch] = max_scale;
}

static void encode_exp_vlc(WMACodecContext *s, int ch, const int *exp_param){
    int last_exp;
    const uint16_t *ptr;
    float *q, *q_end;

    ptr = s->exponent_bands[s->frame_len_bits - s->block_len_bits];
    q = s->exponents[ch];
    q_end = q + s->block_len;
    if (s->version == 1) {
        last_exp= *exp_param++;
        assert(last_exp-10 >= 0 && last_exp-10 < 32);
        put_bits(&s->pb, 5, last_exp - 10);
        q+= *ptr++;
    }else
        last_exp = 36;
    while (q < q_end) {
        int exp = *exp_param++;
        int code = exp - last_exp + 60;
        assert(code >= 0 && code < 120);
        put_bits(&s->pb, ff_wma_scale_huffbits[code], ff_wma_scale_huffcodes[code]);
        /* XXX: use a table */
        q+= *ptr++;
        last_exp= exp;
    }
}

static int encode_block(WMACodecContext *s, float (*src_coefs)[BLOCK_MAX_SIZE], int total_gain){
    int v, bsize, ch, coef_nb_bits, parse_exponents;
    float mdct_norm;
    int nb_coefs[MAX_CHANNELS];
    static const int fixed_exp[25]={20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20};

    //FIXME remove duplication relative to decoder
    if (s->use_variable_block_len) {
        assert(0); //FIXME not implemented
    }else{
        /* fixed block len */
        s->next_block_len_bits = s->frame_len_bits;
        s->prev_block_len_bits = s->frame_len_bits;
        s->block_len_bits = s->frame_len_bits;
    }

    s->block_len = 1 << s->block_len_bits;
//     assert((s->block_pos + s->block_len) <= s->frame_len);
    bsize = s->frame_len_bits - s->block_len_bits;

    //FIXME factor
    v = s->coefs_end[bsize] - s->coefs_start;
    for(ch = 0; ch < s->nb_channels; ch++)
        nb_coefs[ch] = v;
    {
        int n4 = s->block_len / 2;
        mdct_norm = 1.0 / (float)n4;
        if (s->version == 1) {
            mdct_norm *= sqrt(n4);
        }
    }

    if (s->nb_channels == 2) {
        put_bits(&s->pb, 1, s->ms_stereo= 1);
    }

    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]= 1) { //FIXME
            init_exp(s, ch, fixed_exp);
        }
    }

    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            int16_t *coefs1;
            float *coefs, *exponents, mult;
            int i, n;

            coefs1 = s->coefs1[ch];
            exponents = s->exponents[ch];
            mult = pow(10, total_gain * 0.05) / s->max_exponent[ch];
            mult *= mdct_norm;
            coefs = src_coefs[ch];
            if (s->use_noise_coding && 0) {
                assert(0); //FIXME not implemented
            } else {
                coefs += s->coefs_start;
                n = nb_coefs[ch];
                for(i = 0;i < n; i++){
                    double t= *coefs++ / (exponents[i] * mult);
                    if(t<-32768 || t>32767)
                        return -1;

                    coefs1[i] = lrint(t);
                }
            }
        }
    }

    v = 0;
    for(ch = 0; ch < s->nb_channels; ch++) {
        int a = s->channel_coded[ch];
        put_bits(&s->pb, 1, a);
        v |= a;
    }

    if (!v)
        return 1;

    for(v= total_gain-1; v>=127; v-= 127)
        put_bits(&s->pb, 7, 127);
    put_bits(&s->pb, 7, v);

    coef_nb_bits= ff_wma_total_gain_to_bits(total_gain);

    if (s->use_noise_coding) {
        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                int i, n;
                n = s->exponent_high_sizes[bsize];
                for(i=0;i<n;i++) {
                    put_bits(&s->pb, 1, s->high_band_coded[ch][i]= 0);
                    if (0)
                        nb_coefs[ch] -= s->exponent_high_bands[bsize][i];
                }
            }
        }
    }

    parse_exponents = 1;
    if (s->block_len_bits != s->frame_len_bits) {
        put_bits(&s->pb, 1, parse_exponents);
    }

    if (parse_exponents) {
        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                if (s->use_exp_vlc) {
                    encode_exp_vlc(s, ch, fixed_exp);
                } else {
                    assert(0); //FIXME not implemented
//                    encode_exp_lsp(s, ch);
                }
            }
        }
    } else {
        assert(0); //FIXME not implemented
    }

    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            int run, tindex;
            int16_t *ptr, *eptr;
            tindex = (ch == 1 && s->ms_stereo);
            ptr = &s->coefs1[ch][0];
            eptr = ptr + nb_coefs[ch];

            run=0;
            for(;ptr < eptr; ptr++){
                if(*ptr){
                    int level= *ptr;
                    int abs_level= FFABS(level);
                    int code= 0;
                    if(abs_level <= s->coef_vlcs[tindex]->max_level){
                        if(run < s->coef_vlcs[tindex]->levels[abs_level-1])
                            code= run + s->int_table[tindex][abs_level-1];
                    }

                    assert(code < s->coef_vlcs[tindex]->n);
                    put_bits(&s->pb, s->coef_vlcs[tindex]->huffbits[code], s->coef_vlcs[tindex]->huffcodes[code]);

                    if(code == 0){
                        if(1<<coef_nb_bits <= abs_level)
                            return -1;

                        put_bits(&s->pb, coef_nb_bits, abs_level);
                        put_bits(&s->pb, s->frame_len_bits, run);
                    }
                    put_bits(&s->pb, 1, level < 0); //FIXME the sign is fliped somewhere
                    run=0;
                }else{
                    run++;
                }
            }
            if(run)
                put_bits(&s->pb, s->coef_vlcs[tindex]->huffbits[1], s->coef_vlcs[tindex]->huffcodes[1]);
        }
        if (s->version == 1 && s->nb_channels >= 2) {
            align_put_bits(&s->pb);
        }
    }
    return 0;
}

static int encode_frame(WMACodecContext *s, float (*src_coefs)[BLOCK_MAX_SIZE], uint8_t *buf, int buf_size, int total_gain){
    init_put_bits(&s->pb, buf, buf_size);

    if (s->use_bit_reservoir) {
        assert(0);//FIXME not implemented
    }else{
        if(encode_block(s, src_coefs, total_gain) < 0)
            return INT_MAX;
    }

    align_put_bits(&s->pb);

    return put_bits_count(&s->pb)/8 - s->block_align;
}

static int encode_superframe(AVCodecContext *avctx,
                            unsigned char *buf, int buf_size, void *data){
    WMACodecContext *s = avctx->priv_data;
    short *samples = data;
    int i, total_gain, best;

    s->block_len_bits= s->frame_len_bits; //required by non variable block len
    s->block_len = 1 << s->block_len_bits;

    apply_window_and_mdct(avctx, samples, avctx->frame_size);

    if (s->ms_stereo) {
        float a, b;
        int i;

        for(i = 0; i < s->block_len; i++) {
            a = s->coefs[0][i]*0.5;
            b = s->coefs[1][i]*0.5;
            s->coefs[0][i] = a + b;
            s->coefs[1][i] = a - b;
        }
    }

#if 1
    total_gain= 128;
    for(i=64; i; i>>=1){
        int error= encode_frame(s, s->coefs, buf, buf_size, total_gain-i);
        if(error<0)
            total_gain-= i;
    }
#else
    total_gain= 90;
    best= encode_frame(s, s->coefs, buf, buf_size, total_gain);
    for(i=32; i; i>>=1){
        int scoreL= encode_frame(s, s->coefs, buf, buf_size, total_gain-i);
        int scoreR= encode_frame(s, s->coefs, buf, buf_size, total_gain+i);
        av_log(NULL, AV_LOG_ERROR, "%d %d %d (%d)\n", scoreL, best, scoreR, total_gain);
        if(scoreL < FFMIN(best, scoreR)){
            best = scoreL;
            total_gain -= i;
        }else if(scoreR < best){
            best = scoreR;
            total_gain += i;
        }
    }
#endif

    encode_frame(s, s->coefs, buf, buf_size, total_gain);
    assert((put_bits_count(&s->pb) & 7) == 0);
    i= s->block_align - (put_bits_count(&s->pb)+7)/8;
    assert(i>=0);
    while(i--)
        put_bits(&s->pb, 8, 'N');

    flush_put_bits(&s->pb);
    return pbBufPtr(&s->pb) - s->pb.buf;
}

AVCodec wmav1_encoder =
{
    "wmav1",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WMAV1,
    sizeof(WMACodecContext),
    encode_init,
    encode_superframe,
    ff_wma_end,
};

AVCodec wmav2_encoder =
{
    "wmav2",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WMAV2,
    sizeof(WMACodecContext),
    encode_init,
    encode_superframe,
    ff_wma_end,
};
