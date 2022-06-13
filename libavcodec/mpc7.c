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
 * @file
 * MPEG Audio Layer 1/2 -like codec with frames of 1152 samples
 * divided into 32 subbands.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "internal.h"
#include "mpegaudiodsp.h"

#include "mpc.h"
#include "mpc7data.h"

static VLC scfi_vlc, dscf_vlc, hdr_vlc, quant_vlc[MPC7_QUANT_VLC_TABLES][2];

static av_cold void mpc7_init_static(void)
{
    static VLCElem quant_tables[7224];
    const uint8_t *raw_quant_table = mpc7_quant_vlcs;

    INIT_VLC_STATIC_FROM_LENGTHS(&scfi_vlc, MPC7_SCFI_BITS, MPC7_SCFI_SIZE,
                                 &mpc7_scfi[1], 2,
                                 &mpc7_scfi[0], 2, 1, 0, 0, 1 << MPC7_SCFI_BITS);
    INIT_VLC_STATIC_FROM_LENGTHS(&dscf_vlc, MPC7_DSCF_BITS, MPC7_DSCF_SIZE,
                                 &mpc7_dscf[1], 2,
                                 &mpc7_dscf[0], 2, 1, -7, 0, 1 << MPC7_DSCF_BITS);
    INIT_VLC_STATIC_FROM_LENGTHS(&hdr_vlc, MPC7_HDR_BITS, MPC7_HDR_SIZE,
                                 &mpc7_hdr[1], 2,
                                 &mpc7_hdr[0], 2, 1, -5, 0, 1 << MPC7_HDR_BITS);
    for (unsigned i = 0, offset = 0; i < MPC7_QUANT_VLC_TABLES; i++){
        for (int j = 0; j < 2; j++) {
            quant_vlc[i][j].table           = &quant_tables[offset];
            quant_vlc[i][j].table_allocated = FF_ARRAY_ELEMS(quant_tables) - offset;
            ff_init_vlc_from_lengths(&quant_vlc[i][j], 9, mpc7_quant_vlc_sizes[i],
                                     &raw_quant_table[1], 2,
                                     &raw_quant_table[0], 2, 1,
                                     mpc7_quant_vlc_off[i],
                                     INIT_VLC_STATIC_OVERLONG, NULL);
            raw_quant_table += 2 * mpc7_quant_vlc_sizes[i];
            offset          += quant_vlc[i][j].table_size;
        }
    }
    ff_mpa_synth_init_fixed();
}

static av_cold int mpc7_decode_init(AVCodecContext * avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MPCContext *c = avctx->priv_data;
    GetBitContext gb;
    LOCAL_ALIGNED_16(uint8_t, buf, [16]);

    /* Musepack SV7 is always stereo */
    if (avctx->ch_layout.nb_channels != 2) {
        avpriv_request_sample(avctx, "%d channels", avctx->ch_layout.nb_channels);
        return AVERROR_PATCHWELCOME;
    }

    if(avctx->extradata_size < 16){
        av_log(avctx, AV_LOG_ERROR, "Too small extradata size (%i)!\n", avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }
    memset(c->oldDSCF, 0, sizeof(c->oldDSCF));
    av_lfg_init(&c->rnd, 0xDEADBEEF);
    ff_bswapdsp_init(&c->bdsp);
    ff_mpadsp_init(&c->mpadsp);
    c->bdsp.bswap_buf((uint32_t *) buf, (const uint32_t *) avctx->extradata, 4);
    init_get_bits(&gb, buf, 128);

    c->IS = get_bits1(&gb);
    c->MSS = get_bits1(&gb);
    c->maxbands = get_bits(&gb, 6);
    if(c->maxbands >= BANDS){
        av_log(avctx, AV_LOG_ERROR, "Too many bands: %i\n", c->maxbands);
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(&gb, 88);
    c->gapless = get_bits1(&gb);
    c->lastframelen = get_bits(&gb, 11);
    av_log(avctx, AV_LOG_DEBUG, "IS: %d, MSS: %d, TG: %d, LFL: %d, bands: %d\n",
            c->IS, c->MSS, c->gapless, c->lastframelen, c->maxbands);
    c->frames_to_skip = 0;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    ff_thread_once(&init_static_once, mpc7_init_static);

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
            *dst++ = get_vlc2(gb, quant_vlc[idx-1][i1].table, 9, 2);
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
    int t = get_vlc2(gb, dscf_vlc.table, MPC7_DSCF_BITS, 1);
    if (t == 8)
        return get_bits(gb, 6);
    return ref + t;
}

static int mpc7_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
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
    frame->nb_samples = MPC_FRAME_SIZE;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    av_fast_padded_malloc(&c->bits, &c->buf_size, buf_size);
    if (!c->bits)
        return AVERROR(ENOMEM);
    c->bdsp.bswap_buf((uint32_t *) c->bits, (const uint32_t *) buf,
                      buf_size >> 2);
    if ((ret = init_get_bits8(&gb, c->bits, buf_size)) < 0)
        return ret;
    skip_bits_long(&gb, skip);

    /* read subband indexes */
    for(i = 0; i <= c->maxbands; i++){
        for(ch = 0; ch < 2; ch++){
            int t = i ? get_vlc2(&gb, hdr_vlc.table, MPC7_HDR_BITS, 1) : 4;
            if(t == 4) bands[i].res[ch] = get_bits(&gb, 4);
            else bands[i].res[ch] = bands[i-1].res[ch] + t;
            if (bands[i].res[ch] < -1 || bands[i].res[ch] > 17) {
                av_log(avctx, AV_LOG_ERROR, "subband index invalid\n");
                return AVERROR_INVALIDDATA;
            }
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
    if(last_frame)
        frame->nb_samples = c->lastframelen;

    bits_used = get_bits_count(&gb);
    bits_avail = buf_size * 8;
    if (!last_frame && ((bits_avail < bits_used) || (bits_used + 32 <= bits_avail))) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame: used %i of %i bits\n", bits_used, bits_avail);
        return AVERROR_INVALIDDATA;
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

const FFCodec ff_mpc7_decoder = {
    .p.name         = "mpc7",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Musepack SV7"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MUSEPACK7,
    .priv_data_size = sizeof(MPCContext),
    .init           = mpc7_decode_init,
    .close          = mpc7_decode_close,
    FF_CODEC_DECODE_CB(mpc7_decode_frame),
    .flush          = mpc7_decode_flush,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
