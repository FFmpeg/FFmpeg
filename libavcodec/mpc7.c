/*
 * Musepack SV7 decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 * @file libavcodec/mpc7.c Musepack SV7 decoder
 * MPEG Audio Layer 1/2 -like codec with frames of 1152 samples
 * divided into 32 subbands.
 */

#include "libavutil/random.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "mpegaudio.h"

#include "mpc.h"
#include "mpc7data.h"

#define BANDS            32
#define SAMPLES_PER_BAND 36
#define MPC_FRAME_SIZE   (BANDS * SAMPLES_PER_BAND)

static VLC scfi_vlc, dscf_vlc, hdr_vlc, quant_vlc[MPC7_QUANT_VLC_TABLES][2];

static av_cold int mpc7_decode_init(AVCodecContext * avctx)
{
    int i, j;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    uint8_t buf[16];
    static int vlc_initialized = 0;

    if(avctx->extradata_size < 16){
        av_log(avctx, AV_LOG_ERROR, "Too small extradata size (%i)!\n", avctx->extradata_size);
        return -1;
    }
    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    av_random_init(&c->rnd, 0xDEADBEEF);
    dsputil_init(&c->dsp, avctx);
    c->dsp.bswap_buf((uint32_t*)buf, (const uint32_t*)avctx->extradata, 4);
    ff_mpc_init();
    init_get_bits(&gb, buf, 128);

    c->IS = get_bits1(&gb);
    c->MSS = get_bits1(&gb);
    c->maxbands = get_bits(&gb, 6);
    if(c->maxbands >= BANDS){
        av_log(avctx, AV_LOG_ERROR, "Too many bands: %i\n", c->maxbands);
        return -1;
    }
    skip_bits(&gb, 88);
    c->gapless = get_bits1(&gb);
    c->lastframelen = get_bits(&gb, 11);
    av_log(avctx, AV_LOG_DEBUG, "IS: %d, MSS: %d, TG: %d, LFL: %d, bands: %d\n",
            c->IS, c->MSS, c->gapless, c->lastframelen, c->maxbands);
    c->frames_to_skip = 0;

    if(vlc_initialized) return 0;
    av_log(avctx, AV_LOG_DEBUG, "Initing VLC\n");
    if(init_vlc(&scfi_vlc, MPC7_SCFI_BITS, MPC7_SCFI_SIZE,
                &mpc7_scfi[1], 2, 1,
                &mpc7_scfi[0], 2, 1, INIT_VLC_USE_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init SCFI VLC\n");
        return -1;
    }
    if(init_vlc(&dscf_vlc, MPC7_DSCF_BITS, MPC7_DSCF_SIZE,
                &mpc7_dscf[1], 2, 1,
                &mpc7_dscf[0], 2, 1, INIT_VLC_USE_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init DSCF VLC\n");
        return -1;
    }
    if(init_vlc(&hdr_vlc, MPC7_HDR_BITS, MPC7_HDR_SIZE,
                &mpc7_hdr[1], 2, 1,
                &mpc7_hdr[0], 2, 1, INIT_VLC_USE_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init HDR VLC\n");
        return -1;
    }
    for(i = 0; i < MPC7_QUANT_VLC_TABLES; i++){
        for(j = 0; j < 2; j++){
            if(init_vlc(&quant_vlc[i][j], 9, mpc7_quant_vlc_sizes[i],
                        &mpc7_quant_vlc[i][j][1], 4, 2,
                        &mpc7_quant_vlc[i][j][0], 4, 2, INIT_VLC_USE_STATIC)){
                av_log(avctx, AV_LOG_ERROR, "Cannot init QUANT VLC %i,%i\n",i,j);
                return -1;
            }
        }
    }
    vlc_initialized = 1;
    avctx->sample_fmt = SAMPLE_FMT_S16;
    avctx->channel_layout = (avctx->channels==2) ? CH_LAYOUT_STEREO : CH_LAYOUT_MONO;
    return 0;
}

/**
 * Fill samples for given subband
 */
static inline void idx_to_quant(MPCContext *c, GetBitContext *gb, int idx, int *dst)
{
    int i, i1, t;
    switch(idx){
    case -1:
        for(i = 0; i < SAMPLES_PER_BAND; i++){
            *dst++ = (av_random(&c->rnd) & 0x3FC) - 510;
        }
        break;
    case 1:
        i1 = get_bits1(gb);
        for(i = 0; i < SAMPLES_PER_BAND/3; i++){
            t = get_vlc2(gb, quant_vlc[0][i1].table, 9, 2);
            *dst++ = mpc7_idx30[t];
            *dst++ = mpc7_idx31[t];
            *dst++ = mpc7_idx32[t];
        }
        break;
    case 2:
        i1 = get_bits1(gb);
        for(i = 0; i < SAMPLES_PER_BAND/2; i++){
            t = get_vlc2(gb, quant_vlc[1][i1].table, 9, 2);
            *dst++ = mpc7_idx50[t];
            *dst++ = mpc7_idx51[t];
        }
        break;
    case  3: case  4: case  5: case  6: case  7:
        i1 = get_bits1(gb);
        for(i = 0; i < SAMPLES_PER_BAND; i++)
            *dst++ = get_vlc2(gb, quant_vlc[idx-1][i1].table, 9, 2) - mpc7_quant_vlc_off[idx-1];
        break;
    case  8: case  9: case 10: case 11: case 12:
    case 13: case 14: case 15: case 16: case 17:
        t = (1 << (idx - 2)) - 1;
        for(i = 0; i < SAMPLES_PER_BAND; i++)
            *dst++ = get_bits(gb, idx - 1) - t;
        break;
    default: // case 0 and -2..-17
        return;
    }
}

static int mpc7_decode_frame(AVCodecContext * avctx,
                            void *data, int *data_size,
                            const uint8_t * buf, int buf_size)
{
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    uint8_t *bits;
    int i, ch, t;
    int mb = -1;
    Band *bands = c->bands;
    int off;
    int bits_used, bits_avail;

    memset(bands, 0, sizeof(bands));
    if(buf_size <= 4){
        av_log(avctx, AV_LOG_ERROR, "Too small buffer passed (%i bytes)\n", buf_size);
    }

    bits = av_malloc(((buf_size - 1) & ~3) + FF_INPUT_BUFFER_PADDING_SIZE);
    c->dsp.bswap_buf((uint32_t*)bits, (const uint32_t*)(buf + 4), (buf_size - 4) >> 2);
    init_get_bits(&gb, bits, (buf_size - 4)* 8);
    skip_bits(&gb, buf[0]);

    /* read subband indexes */
    for(i = 0; i <= c->maxbands; i++){
        for(ch = 0; ch < 2; ch++){
            if(i) t = get_vlc2(&gb, hdr_vlc.table, MPC7_HDR_BITS, 1) - 5;
            if(!i || (t == 4)) bands[i].res[ch] = get_bits(&gb, 4);
            else bands[i].res[ch] = bands[i-1].res[ch] + t;
        }

        if(bands[i].res[0] || bands[i].res[1]){
            mb = i;
            if(c->MSS) bands[i].msf = get_bits1(&gb);
        }
    }
    /* get scale indexes coding method */
    for(i = 0; i <= mb; i++)
        for(ch = 0; ch < 2; ch++)
            if(bands[i].res[ch]) bands[i].scfi[ch] = get_vlc2(&gb, scfi_vlc.table, MPC7_SCFI_BITS, 1);
    /* get scale indexes */
    for(i = 0; i <= mb; i++){
        for(ch = 0; ch < 2; ch++){
            if(bands[i].res[ch]){
                bands[i].scf_idx[ch][2] = c->oldDSCF[ch][i];
                t = get_vlc2(&gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
                bands[i].scf_idx[ch][0] = (t == 8) ? get_bits(&gb, 6) : (bands[i].scf_idx[ch][2] + t);
                switch(bands[i].scfi[ch]){
                case 0:
                    t = get_vlc2(&gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
                    bands[i].scf_idx[ch][1] = (t == 8) ? get_bits(&gb, 6) : (bands[i].scf_idx[ch][0] + t);
                    t = get_vlc2(&gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
                    bands[i].scf_idx[ch][2] = (t == 8) ? get_bits(&gb, 6) : (bands[i].scf_idx[ch][1] + t);
                    break;
                case 1:
                    t = get_vlc2(&gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
                    bands[i].scf_idx[ch][1] = (t == 8) ? get_bits(&gb, 6) : (bands[i].scf_idx[ch][0] + t);
                    bands[i].scf_idx[ch][2] = bands[i].scf_idx[ch][1];
                    break;
                case 2:
                    bands[i].scf_idx[ch][1] = bands[i].scf_idx[ch][0];
                    t = get_vlc2(&gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
                    bands[i].scf_idx[ch][2] = (t == 8) ? get_bits(&gb, 6) : (bands[i].scf_idx[ch][1] + t);
                    break;
                case 3:
                    bands[i].scf_idx[ch][2] = bands[i].scf_idx[ch][1] = bands[i].scf_idx[ch][0];
                    break;
                }
                c->oldDSCF[ch][i] = bands[i].scf_idx[ch][2];
            }
        }
    }
    /* get quantizers */
    memset(c->Q, 0, sizeof(c->Q));
    off = 0;
    for(i = 0; i < BANDS; i++, off += SAMPLES_PER_BAND)
        for(ch = 0; ch < 2; ch++)
            idx_to_quant(c, &gb, bands[i].res[ch], c->Q[ch] + off);

    ff_mpc_dequantize_and_synth(c, mb, data);

    av_free(bits);

    bits_used = get_bits_count(&gb);
    bits_avail = (buf_size - 4) * 8;
    if(!buf[1] && ((bits_avail < bits_used) || (bits_used + 32 <= bits_avail))){
        av_log(NULL,0, "Error decoding frame: used %i of %i bits\n", bits_used, bits_avail);
        return -1;
    }
    if(c->frames_to_skip){
        c->frames_to_skip--;
        *data_size = 0;
        return buf_size;
    }
    *data_size = (buf[1] ? c->lastframelen : MPC_FRAME_SIZE) * 4;

    return buf_size;
}

static void mpc7_decode_flush(AVCodecContext *avctx)
{
    MPCContext *c = avctx->priv_data;

    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    c->frames_to_skip = 32;
}

AVCodec mpc7_decoder = {
    "mpc7",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MUSEPACK7,
    sizeof(MPCContext),
    mpc7_decode_init,
    NULL,
    NULL,
    mpc7_decode_frame,
    .flush = mpc7_decode_flush,
    .long_name = NULL_IF_CONFIG_SMALL("Musepack SV7"),
};
