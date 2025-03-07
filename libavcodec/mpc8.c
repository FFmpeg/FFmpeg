/*
 * Musepack SV8 decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * MPEG Audio Layer 1/2 -like codec with frames of 1152 samples
 * divided into 32 subbands.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/lfg.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "mpegaudiodsp.h"

#include "mpc.h"
#include "mpc8data.h"
#include "mpc8huff.h"

static VLC band_vlc, scfi_vlc[2], dscf_vlc[2], res_vlc[2];
static VLC q1_vlc, q2_vlc[2], q3_vlc[2], quant_vlc[4][2], q9up_vlc;

static inline int mpc8_dec_base(GetBitContext *gb, int k, int n)
{
    int len = mpc8_cnk_len[k-1][n-1] - 1;
    int code = len ? get_bits_long(gb, len) : 0;

    if (code >= mpc8_cnk_lost[k-1][n-1])
        code = ((code << 1) | get_bits1(gb)) - mpc8_cnk_lost[k-1][n-1];

    return code;
}

static inline int mpc8_dec_enum(GetBitContext *gb, int k, int n)
{
    int bits = 0;
    const uint32_t * C = mpc8_cnk[k-1];
    int code = mpc8_dec_base(gb, k, n);

    do {
        n--;
        if (code >= C[n]) {
            bits |= 1U << n;
            code -= C[n];
            C -= 32;
            k--;
        }
    } while(k > 0);

    return bits;
}

static inline int mpc8_get_mod_golomb(GetBitContext *gb, int m)
{
    if(mpc8_cnk_len[0][m] < 1) return 0;
    return mpc8_dec_base(gb, 1, m+1);
}

static int mpc8_get_mask(GetBitContext *gb, int size, int t)
{
    int mask = 0;

    if(t && t != size)
         mask = mpc8_dec_enum(gb, FFMIN(t, size - t), size);
    if((t << 1) > size) mask = ~mask;

    return mask;
}

static av_cold void build_vlc(VLC *vlc, unsigned *buf_offset,
                              const uint8_t codes_counts[16],
                              const uint8_t **syms, int offset)
{
    static VLCElem vlc_buf[9296];
    uint8_t len[MPC8_MAX_VLC_SIZE];
    unsigned num = 0;

    vlc->table           = &vlc_buf[*buf_offset];
    vlc->table_allocated = FF_ARRAY_ELEMS(vlc_buf) - *buf_offset;

    for (int i = 16; i > 0; i--)
        for (unsigned tmp = num + codes_counts[i - 1]; num < tmp; num++)
            len[num] = i;

    ff_vlc_init_from_lengths(vlc, FFMIN(len[0], 9), num, len, 1,
                             *syms, 1, 1, offset, VLC_INIT_STATIC_OVERLONG, NULL);
    *buf_offset += vlc->table_size;
    *syms       += num;
}

static av_cold void mpc8_init_static(void)
{
    const uint8_t *q_syms   = mpc8_q_syms,  *bands_syms = mpc8_bands_syms;
    const uint8_t *res_syms = mpc8_res_syms, *scfi_syms = mpc8_scfi_syms;
    const uint8_t *dscf_syms = mpc8_dscf_syms;
    unsigned offset = 0;

    build_vlc(&band_vlc, &offset, mpc8_bands_len_counts, &bands_syms, 0);

    build_vlc(&q1_vlc,   &offset, mpc8_q1_len_counts,   &q_syms, 0);
    build_vlc(&q9up_vlc, &offset, mpc8_q9up_len_counts, &q_syms, 0);

    for (int i = 0; i < 2; i++){
        build_vlc(&scfi_vlc[i], &offset, mpc8_scfi_len_counts[i], &scfi_syms, 0);

        build_vlc(&dscf_vlc[i], &offset, mpc8_dscf_len_counts[i], &dscf_syms, 0);

        build_vlc(&res_vlc[i],  &offset, mpc8_res_len_counts[i],  &res_syms,  0);

        build_vlc(&q2_vlc[i], &offset, mpc8_q2_len_counts[i], &q_syms, 0);
        build_vlc(&q3_vlc[i], &offset, mpc8_q34_len_counts[i],
                  &q_syms, -48 - 16 * i);
        for (int j = 0; j < 4; j++)
            build_vlc(&quant_vlc[j][i], &offset, mpc8_q5_8_len_counts[i][j],
                      &q_syms, -((8 << j) - 1));
    }
    ff_mpa_synth_init_fixed();
}

static av_cold int mpc8_decode_init(AVCodecContext * avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    int channels;

    if(avctx->extradata_size < 2){
        av_log(avctx, AV_LOG_ERROR, "Too small extradata size (%i)!\n", avctx->extradata_size);
        return -1;
    }
    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    av_lfg_init(&c->rnd, 0xDEADBEEF);
    ff_mpadsp_init(&c->mpadsp);

    init_get_bits(&gb, avctx->extradata, 16);

    skip_bits(&gb, 3);//sample rate
    c->maxbands = get_bits(&gb, 5) + 1;
    if (c->maxbands >= BANDS) {
        av_log(avctx,AV_LOG_ERROR, "maxbands %d too high\n", c->maxbands);
        return AVERROR_INVALIDDATA;
    }
    channels = get_bits(&gb, 4) + 1;
    if (channels > 2) {
        avpriv_request_sample(avctx, "Multichannel MPC SV8");
        return AVERROR_PATCHWELCOME;
    }
    c->MSS = get_bits1(&gb);
    c->frames = 1 << (get_bits(&gb, 3) * 2);

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, channels);

    ff_thread_once(&init_static_once, mpc8_init_static);

    return 0;
}

static int mpc8_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb2, *gb = &gb2;
    int i, j, k, ch, cnt, res, t;
    Band *bands = c->bands;
    int off;
    int maxband, keyframe;
    int last[2];

    keyframe = c->cur_frame == 0;

    if(keyframe){
        memset(c->Q, 0, sizeof(c->Q));
        c->last_bits_used = 0;
    }
    if ((res = init_get_bits8(gb, buf, buf_size)) < 0)
        return res;

    skip_bits(gb, c->last_bits_used & 7);

    if(keyframe)
        maxband = mpc8_get_mod_golomb(gb, c->maxbands + 1);
    else{
        maxband = c->last_max_band + get_vlc2(gb, band_vlc.table, MPC8_BANDS_BITS, 2);
        if(maxband > 32) maxband -= 33;
    }

    if (get_bits_left(gb) < 0) {
        *got_frame_ptr = 0;
        return buf_size;
    }

    if(maxband > c->maxbands + 1) {
        av_log(avctx, AV_LOG_ERROR, "maxband %d too large\n",maxband);
        return AVERROR_INVALIDDATA;
    }
    c->last_max_band = maxband;

    /* read subband indexes */
    if(maxband){
        last[0] = last[1] = 0;
        for(i = maxband - 1; i >= 0; i--){
            for(ch = 0; ch < 2; ch++){
                last[ch] = get_vlc2(gb, res_vlc[last[ch] > 2].table, MPC8_RES_BITS, 2) + last[ch];
                if(last[ch] > 15) last[ch] -= 17;
                bands[i].res[ch] = last[ch];
            }
        }
        if(c->MSS){
            int mask;

            cnt = 0;
            for(i = 0; i < maxband; i++)
                if(bands[i].res[0] || bands[i].res[1])
                    cnt++;
            t = mpc8_get_mod_golomb(gb, cnt);
            mask = mpc8_get_mask(gb, cnt, t);
            for(i = maxband - 1; i >= 0; i--)
                if(bands[i].res[0] || bands[i].res[1]){
                    bands[i].msf = mask & 1;
                    mask >>= 1;
                }
        }
    }
    for(i = maxband; i < c->maxbands; i++)
        bands[i].res[0] = bands[i].res[1] = 0;

    if(keyframe){
        for(i = 0; i < 32; i++)
            c->oldDSCF[0][i] = c->oldDSCF[1][i] = 1;
    }

    for(i = 0; i < maxband; i++){
        if(bands[i].res[0] || bands[i].res[1]){
            cnt = !!bands[i].res[0] + !!bands[i].res[1] - 1;
            if(cnt >= 0){
                t = get_vlc2(gb, scfi_vlc[cnt].table, scfi_vlc[cnt].bits, 1);
                if(bands[i].res[0]) bands[i].scfi[0] = t >> (2 * cnt);
                if(bands[i].res[1]) bands[i].scfi[1] = t & 3;
            }
        }
    }

    for(i = 0; i < maxband; i++){
        for(ch = 0; ch < 2; ch++){
            if(!bands[i].res[ch]) continue;

            if(c->oldDSCF[ch][i]){
                bands[i].scf_idx[ch][0] = get_bits(gb, 7) - 6;
                c->oldDSCF[ch][i] = 0;
            }else{
                t = get_vlc2(gb, dscf_vlc[1].table, MPC8_DSCF1_BITS, 2);
                if(t == 64)
                    t += get_bits(gb, 6);
                bands[i].scf_idx[ch][0] = ((bands[i].scf_idx[ch][2] + t - 25) & 0x7F) - 6;
            }
            for(j = 0; j < 2; j++){
                if((bands[i].scfi[ch] << j) & 2)
                    bands[i].scf_idx[ch][j + 1] = bands[i].scf_idx[ch][j];
                else{
                    t = get_vlc2(gb, dscf_vlc[0].table, MPC8_DSCF0_BITS, 2);
                    if(t == 31)
                        t = 64 + get_bits(gb, 6);
                    bands[i].scf_idx[ch][j + 1] = ((bands[i].scf_idx[ch][j] + t - 25) & 0x7F) - 6;
                }
            }
        }
    }

    for(i = 0, off = 0; i < maxband; i++, off += SAMPLES_PER_BAND){
        for(ch = 0; ch < 2; ch++){
            res = bands[i].res[ch];
            switch(res){
            case -1:
                for(j = 0; j < SAMPLES_PER_BAND; j++)
                    c->Q[ch][off + j] = (av_lfg_get(&c->rnd) & 0x3FC) - 510;
                break;
            case 0:
                break;
            case 1:
                for(j = 0; j < SAMPLES_PER_BAND; j += SAMPLES_PER_BAND / 2){
                    cnt = get_vlc2(gb, q1_vlc.table, MPC8_Q1_BITS, 2);
                    t = mpc8_get_mask(gb, 18, cnt);
                    for(k = 0; k < SAMPLES_PER_BAND / 2; k++)
                        c->Q[ch][off + j + k] = t & (1 << (SAMPLES_PER_BAND / 2 - k - 1))
                                                ? (get_bits1(gb) << 1) - 1 : 0;
                }
                break;
            case 2:
                cnt = 6;//2*mpc8_thres[res]
                for(j = 0; j < SAMPLES_PER_BAND; j += 3){
                    t = get_vlc2(gb, q2_vlc[cnt > 3].table, MPC8_Q2_BITS, 2);
                    c->Q[ch][off + j + 0] = mpc8_idx50[t];
                    c->Q[ch][off + j + 1] = mpc8_idx51[t];
                    c->Q[ch][off + j + 2] = mpc8_idx52[t];
                    cnt = (cnt >> 1) + mpc8_huffq2[t];
                }
                break;
            case 3:
            case 4:
                for(j = 0; j < SAMPLES_PER_BAND; j += 2){
                    t = get_vlc2(gb, q3_vlc[res - 3].table, MPC8_Q3_BITS, 2);
                    c->Q[ch][off + j + 1] = t >> 4;
                    c->Q[ch][off + j + 0] = sign_extend(t, 4);
                }
                break;
            case 5:
            case 6:
            case 7:
            case 8:
                cnt = 2 * mpc8_thres[res];
                for(j = 0; j < SAMPLES_PER_BAND; j++){
                    const VLC *vlc = &quant_vlc[res - 5][cnt > mpc8_thres[res]];
                    c->Q[ch][off + j] = get_vlc2(gb, vlc->table, vlc->bits, 2);
                    cnt = (cnt >> 1) + FFABS(c->Q[ch][off + j]);
                }
                break;
            default:
                for(j = 0; j < SAMPLES_PER_BAND; j++){
                    c->Q[ch][off + j] = get_vlc2(gb, q9up_vlc.table, MPC8_Q9UP_BITS, 2);
                    if(res != 9){
                        c->Q[ch][off + j] <<= res - 9;
                        c->Q[ch][off + j] |= get_bits(gb, res - 9);
                    }
                    c->Q[ch][off + j] -= (1 << (res - 2)) - 1;
                }
            }
        }
    }

    frame->nb_samples = MPC_FRAME_SIZE;
    if ((res = ff_get_buffer(avctx, frame, 0)) < 0)
        return res;

    ff_mpc_dequantize_and_synth(c, maxband - 1,
                                (int16_t **)frame->extended_data,
                                avctx->ch_layout.nb_channels);

    c->cur_frame++;

    c->last_bits_used = get_bits_count(gb);
    if(c->cur_frame >= c->frames)
        c->cur_frame = 0;
    if (get_bits_left(gb) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Overread %d\n", -get_bits_left(gb));
        c->last_bits_used = buf_size << 3;
    } else if (c->cur_frame == 0 && get_bits_left(gb) < 8) {// we have only padding left
        c->last_bits_used = buf_size << 3;
    }

    *got_frame_ptr = 1;

    return c->cur_frame ? c->last_bits_used >> 3 : buf_size;
}

static av_cold void mpc8_decode_flush(AVCodecContext *avctx)
{
    MPCContext *c = avctx->priv_data;
    c->cur_frame = 0;
}

const FFCodec ff_mpc8_decoder = {
    .p.name         = "mpc8",
    CODEC_LONG_NAME("Musepack SV8"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MUSEPACK8,
    .priv_data_size = sizeof(MPCContext),
    .init           = mpc8_decode_init,
    FF_CODEC_DECODE_CB(mpc8_decode_frame),
    .flush          = mpc8_decode_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16P),
};
