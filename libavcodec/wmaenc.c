/*
 * WMA compatible encoder
 * Copyright (c) 2007 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "internal.h"
#include "wma.h"

#undef NDEBUG
#include <assert.h>


static int encode_init(AVCodecContext * avctx){
    WMACodecContext *s = avctx->priv_data;
    int i, flags1, flags2;
    uint8_t *extradata;

    s->avctx = avctx;

    if(avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "too many channels: got %i, need %i or fewer",
               avctx->channels, MAX_CHANNELS);
        return AVERROR(EINVAL);
    }

    if (avctx->sample_rate > 48000) {
        av_log(avctx, AV_LOG_ERROR, "sample rate is too high: %d > 48kHz",
               avctx->sample_rate);
        return AVERROR(EINVAL);
    }

    if(avctx->bit_rate < 24*1000) {
        av_log(avctx, AV_LOG_ERROR, "bitrate too low: got %i, need 24000 or higher\n",
               avctx->bit_rate);
        return AVERROR(EINVAL);
    }

    /* extract flag infos */
    flags1 = 0;
    flags2 = 1;
    if (avctx->codec->id == CODEC_ID_WMAV1) {
        extradata= av_malloc(4);
        avctx->extradata_size= 4;
        AV_WL16(extradata, flags1);
        AV_WL16(extradata+2, flags2);
    } else if (avctx->codec->id == CODEC_ID_WMAV2) {
        extradata= av_mallocz(10);
        avctx->extradata_size= 10;
        AV_WL32(extradata, flags1);
        AV_WL16(extradata+4, flags2);
    }else
        assert(0);
    avctx->extradata= extradata;
    s->use_exp_vlc = flags2 & 0x0001;
    s->use_bit_reservoir = flags2 & 0x0002;
    s->use_variable_block_len = flags2 & 0x0004;
    if (avctx->channels == 2)
        s->ms_stereo = 1;

    ff_wma_init(avctx, flags2);

    /* init MDCT */
    for(i = 0; i < s->nb_block_sizes; i++)
        ff_mdct_init(&s->mdct_ctx[i], s->frame_len_bits - i + 1, 0, 1.0);

    s->block_align     = avctx->bit_rate * (int64_t)s->frame_len /
                         (avctx->sample_rate * 8);
    s->block_align     = FFMIN(s->block_align, MAX_CODED_SUPERFRAME_SIZE);
    avctx->block_align = s->block_align;
    avctx->bit_rate    = avctx->block_align * 8LL * avctx->sample_rate /
                         s->frame_len;
//av_log(NULL, AV_LOG_ERROR, "%d %d %d %d\n", s->block_align, avctx->bit_rate, s->frame_len, avctx->sample_rate);
    avctx->frame_size = avctx->delay = s->frame_len;

#if FF_API_OLD_ENCODE_AUDIO
    avctx->coded_frame = &s->frame;
    avcodec_get_frame_defaults(avctx->coded_frame);
#endif

    return 0;
}


static void apply_window_and_mdct(AVCodecContext * avctx, const signed short * audio, int len) {
    WMACodecContext *s = avctx->priv_data;
    int window_index= s->frame_len_bits - s->block_len_bits;
    FFTContext *mdct = &s->mdct_ctx[window_index];
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
        mdct->mdct_calc(mdct, s->coefs[channel], s->output);
    }
}

//FIXME use for decoding too
static void init_exp(WMACodecContext *s, int ch, const int *exp_param){
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
        put_bits(&s->pb, ff_aac_scalefactor_bits[code], ff_aac_scalefactor_code[code]);
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
        put_bits(&s->pb, 1, !!s->ms_stereo);
    }

    for(ch = 0; ch < s->nb_channels; ch++) {
        s->channel_coded[ch] = 1; //FIXME only set channel_coded when needed, instead of always
        if (s->channel_coded[ch]) {
            init_exp(s, ch, fixed_exp);
        }
    }

    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            WMACoef *coefs1;
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
            WMACoef *ptr, *eptr;
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
            avpriv_align_put_bits(&s->pb);
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

    avpriv_align_put_bits(&s->pb);

    return put_bits_count(&s->pb)/8 - s->block_align;
}

static int encode_superframe(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    WMACodecContext *s = avctx->priv_data;
    const int16_t *samples = (const int16_t *)frame->data[0];
    int i, total_gain, ret;

    s->block_len_bits= s->frame_len_bits; //required by non variable block len
    s->block_len = 1 << s->block_len_bits;

    apply_window_and_mdct(avctx, samples, frame->nb_samples);

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

    if ((ret = ff_alloc_packet(avpkt, 2 * MAX_CODED_SUPERFRAME_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }

#if 1
    total_gain= 128;
    for(i=64; i; i>>=1){
        int error = encode_frame(s, s->coefs, avpkt->data, avpkt->size,
                                 total_gain - i);
        if(error<0)
            total_gain-= i;
    }
#else
    total_gain= 90;
    best = encode_frame(s, s->coefs, avpkt->data, avpkt->size, total_gain);
    for(i=32; i; i>>=1){
        int scoreL = encode_frame(s, s->coefs, avpkt->data, avpkt->size, total_gain - i);
        int scoreR = encode_frame(s, s->coefs, avpkt->data, avpkt->size, total_gain + i);
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

    if ((i = encode_frame(s, s->coefs, avpkt->data, avpkt->size, total_gain)) >= 0) {
        av_log(avctx, AV_LOG_ERROR, "required frame size too large. please "
               "use a higher bit rate.\n");
        return AVERROR(EINVAL);
    }
    assert((put_bits_count(&s->pb) & 7) == 0);
    while (i++)
        put_bits(&s->pb, 8, 'N');

    flush_put_bits(&s->pb);

    if (frame->pts != AV_NOPTS_VALUE)
        avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->delay);

    avpkt->size = s->block_align;
    *got_packet_ptr = 1;
    return 0;
}

AVCodec ff_wmav1_encoder = {
    .name           = "wmav1",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_WMAV1,
    .priv_data_size = sizeof(WMACodecContext),
    .init           = encode_init,
    .encode2        = encode_superframe,
    .close          = ff_wma_end,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Audio 1"),
};

AVCodec ff_wmav2_encoder = {
    .name           = "wmav2",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_WMAV2,
    .priv_data_size = sizeof(WMACodecContext),
    .init           = encode_init,
    .encode2        = encode_superframe,
    .close          = ff_wma_end,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Audio 2"),
};
