/*
 * Musepack SV7 decoder
 * Copyright (c) 2006 Konstantin Shishkov
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

/**
 * @file
 * MPEG Audio Layer 1/2 -like codec with frames of 1152 samples
 * divided into 32 subbands.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "mpegaudiodsp.h"

#include "mpc.h"
#include "mpc7data.h"

#define BANDS            32
#define SAMPLES_PER_BAND 36
#define MPC_FRAME_SIZE   (BANDS * SAMPLES_PER_BAND)

static VLC scfi_vlc, dscf_vlc, hdr_vlc, quant_vlc[MPC7_QUANT_VLC_TABLES][2];

static const uint16_t quant_offsets[MPC7_QUANT_VLC_TABLES*2 + 1] =
{
       0, 512, 1024, 1536, 2052, 2564, 3076, 3588, 4100, 4612, 5124,
       5636, 6164, 6676, 7224
};


static av_cold int mpc7_decode_init(AVCodecContext * avctx)
{
    int i, j;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    LOCAL_ALIGNED_16(uint8_t, buf, [16]);
    static int vlc_initialized = 0;

    static VLC_TYPE scfi_table[1 << MPC7_SCFI_BITS][2];
    static VLC_TYPE dscf_table[1 << MPC7_DSCF_BITS][2];
    static VLC_TYPE hdr_table[1 << MPC7_HDR_BITS][2];
    static VLC_TYPE quant_tables[7224][2];

    /* Musepack SV7 is always stereo */
    if (avctx->channels != 2) {
        avpriv_request_sample(avctx, "%d channels", avctx->channels);
        return AVERROR_PATCHWELCOME;
    }

    if(avctx->extradata_size < 16){
        av_log(avctx, AV_LOG_ERROR, "Too small extradata size (%i)!\n", avctx->extradata_size);
        return -1;
    }
    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    av_lfg_init(&c->rnd, 0xDEADBEEF);
    ff_bswapdsp_init(&c->bdsp);
    ff_mpadsp_init(&c->mpadsp);
    c->bdsp.bswap_buf((uint32_t *) buf, (const uint32_t *) avctx->extradata, 4);
    ff_mpc_init();
    init_get_bits(&gb, buf, 128);

    c->IS = get_bits1(&gb);
    c->MSS = get_bits1(&gb);
    c->maxbands = get_bits(&gb, 6);
    if(c->maxbands >= BANDS){
        av_log(avctx, AV_LOG_ERROR, "Too many bands: %i\n", c->maxbands);
        return -1;
    }
    skip_bits_long(&gb, 88);
    c->gapless = get_bits1(&gb);
    c->lastframelen = get_bits(&gb, 11);
    av_log(avctx, AV_LOG_DEBUG, "IS: %d, MSS: %d, TG: %d, LFL: %d, bands: %d\n",
            c->IS, c->MSS, c->gapless, c->lastframelen, c->maxbands);
    c->frames_to_skip = 0;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    avctx->channel_layout = AV_CH_LAYOUT_STEREO;

    if(vlc_initialized) return 0;
    av_log(avctx, AV_LOG_DEBUG, "Initing VLC\n");
    scfi_vlc.table = scfi_table;
    scfi_vlc.table_allocated = 1 << MPC7_SCFI_BITS;
    if(init_vlc(&scfi_vlc, MPC7_SCFI_BITS, MPC7_SCFI_SIZE,
                &mpc7_scfi[1], 2, 1,
                &mpc7_scfi[0], 2, 1, INIT_VLC_USE_NEW_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init SCFI VLC\n");
        return -1;
    }
    dscf_vlc.table = dscf_table;
    dscf_vlc.table_allocated = 1 << MPC7_DSCF_BITS;
    if(init_vlc(&dscf_vlc, MPC7_DSCF_BITS, MPC7_DSCF_SIZE,
                &mpc7_dscf[1], 2, 1,
                &mpc7_dscf[0], 2, 1, INIT_VLC_USE_NEW_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init DSCF VLC\n");
        return -1;
    }
    hdr_vlc.table = hdr_table;
    hdr_vlc.table_allocated = 1 << MPC7_HDR_BITS;
    if(init_vlc(&hdr_vlc, MPC7_HDR_BITS, MPC7_HDR_SIZE,
                &mpc7_hdr[1], 2, 1,
                &mpc7_hdr[0], 2, 1, INIT_VLC_USE_NEW_STATIC)){
        av_log(avctx, AV_LOG_ERROR, "Cannot init HDR VLC\n");
        return -1;
    }
    for(i = 0; i < MPC7_QUANT_VLC_TABLES; i++){
        for(j = 0; j < 2; j++){
            quant_vlc[i][j].table = &quant_tables[quant_offsets[i*2 + j]];
            quant_vlc[i][j].table_allocated = quant_offsets[i*2 + j + 1] - quant_offsets[i*2 + j];
            if(init_vlc(&quant_vlc[i][j], 9, mpc7_quant_vlc_sizes[i],
                        &mpc7_quant_vlc[i][j][1], 4, 2,
                        &mpc7_quant_vlc[i][j][0], 4, 2, INIT_VLC_USE_NEW_STATIC)){
                av_log(avctx, AV_LOG_ERROR, "Cannot init QUANT VLC %i,%i\n",i,j);
                return -1;
            }
        }
    }
    vlc_initialized = 1;

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
            *dst++ = (av_lfg_get(&c->rnd) & 0x3FC) - 510;
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

static int get_scale_idx(GetBitContext *gb, int ref)
{
    int t = get_vlc2(gb, dscf_vlc.table, MPC7_DSCF_BITS, 1) - 7;
    if (t == 8)
        return get_bits(gb, 6);
    return av_clip_uintp2(ref + t, 7);
}

static int mpc7_decode_frame(AVCodecContext * avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    int i, ch;
    int mb = -1;
    Band *bands = c->bands;
    int off, ret, last_frame, skip;
    int bits_used, bits_avail;

    memset(bands, 0, sizeof(*bands) * (c->maxbands + 1));

    buf_size = avpkt->size & ~3;
    if (buf_size <= 0) {
        av_log(avctx, AV_LOG_ERROR, "packet size is too small (%i bytes)\n",
               avpkt->size);
        return AVERROR_INVALIDDATA;
    }
    if (buf_size != avpkt->size) {
        av_log(avctx, AV_LOG_WARNING, "packet size is not a multiple of 4. "
               "extra bytes at the end will be skipped.\n");
    }

    skip       = buf[0];
    last_frame = buf[1];
    buf       += 4;
    buf_size  -= 4;

    /* get output buffer */
    frame->nb_samples = last_frame ? c->lastframelen : MPC_FRAME_SIZE;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    av_fast_padded_malloc(&c->bits, &c->buf_size, buf_size);
    if (!c->bits)
        return AVERROR(ENOMEM);
    c->bdsp.bswap_buf((uint32_t *) c->bits, (const uint32_t *) buf,
                      buf_size >> 2);
    init_get_bits(&gb, c->bits, buf_size * 8);
    skip_bits_long(&gb, skip);

    /* read subband indexes */
    for(i = 0; i <= c->maxbands; i++){
        for(ch = 0; ch < 2; ch++){
            int t = 4;
            if(i) t = get_vlc2(&gb, hdr_vlc.table, MPC7_HDR_BITS, 1) - 5;
            if(t == 4) bands[i].res[ch] = get_bits(&gb, 4);
            else bands[i].res[ch] = av_clip(bands[i-1].res[ch] + t, 0, 17);
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
                bands[i].scf_idx[ch][0] = get_scale_idx(&gb, bands[i].scf_idx[ch][2]);
                switch(bands[i].scfi[ch]){
                case 0:
                    bands[i].scf_idx[ch][1] = get_scale_idx(&gb, bands[i].scf_idx[ch][0]);
                    bands[i].scf_idx[ch][2] = get_scale_idx(&gb, bands[i].scf_idx[ch][1]);
                    break;
                case 1:
                    bands[i].scf_idx[ch][1] = get_scale_idx(&gb, bands[i].scf_idx[ch][0]);
                    bands[i].scf_idx[ch][2] = bands[i].scf_idx[ch][1];
                    break;
                case 2:
                    bands[i].scf_idx[ch][1] = bands[i].scf_idx[ch][0];
                    bands[i].scf_idx[ch][2] = get_scale_idx(&gb, bands[i].scf_idx[ch][1]);
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

    ff_mpc_dequantize_and_synth(c, mb, (int16_t **)frame->extended_data, 2);

    bits_used = get_bits_count(&gb);
    bits_avail = buf_size * 8;
    if (!last_frame && ((bits_avail < bits_used) || (bits_used + 32 <= bits_avail))) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame: used %i of %i bits\n", bits_used, bits_avail);
        return -1;
    }
    if(c->frames_to_skip){
        c->frames_to_skip--;
        *got_frame_ptr = 0;
        return avpkt->size;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static void mpc7_decode_flush(AVCodecContext *avctx)
{
    MPCContext *c = avctx->priv_data;

    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    c->frames_to_skip = 32;
}

static av_cold int mpc7_decode_close(AVCodecContext *avctx)
{
    MPCContext *c = avctx->priv_data;
    av_freep(&c->bits);
    c->buf_size = 0;
    return 0;
}

AVCodec ff_mpc7_decoder = {
    .name           = "mpc7",
    .long_name      = NULL_IF_CONFIG_SMALL("Musepack SV7"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MUSEPACK7,
    .priv_data_size = sizeof(MPCContext),
    .init           = mpc7_decode_init,
    .close          = mpc7_decode_close,
    .decode         = mpc7_decode_frame,
    .flush          = mpc7_decode_flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
};
