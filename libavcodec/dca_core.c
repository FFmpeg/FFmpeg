/*
 * Copyright (C) 2016 foo86
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

#include "libavutil/channel_layout.h"
#include "dcaadpcm.h"
#include "dcadec.h"
#include "dcadata.h"
#include "dcahuff.h"
#include "dcamath.h"
#include "dca_syncwords.h"
#include "internal.h"

#if ARCH_ARM
#include "arm/dca.h"
#endif

enum HeaderType {
    HEADER_CORE,
    HEADER_XCH,
    HEADER_XXCH
};

static const int8_t prm_ch_to_spkr_map[DCA_AMODE_COUNT][5] = {
    { DCA_SPEAKER_C,            -1,             -1,             -1,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R,             -1,             -1,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R,             -1,             -1,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R,             -1,             -1,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R,             -1,             -1,             -1 },
    { DCA_SPEAKER_C, DCA_SPEAKER_L, DCA_SPEAKER_R ,             -1,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R, DCA_SPEAKER_Cs,             -1,             -1 },
    { DCA_SPEAKER_C, DCA_SPEAKER_L, DCA_SPEAKER_R , DCA_SPEAKER_Cs,             -1 },
    { DCA_SPEAKER_L, DCA_SPEAKER_R, DCA_SPEAKER_Ls, DCA_SPEAKER_Rs,             -1 },
    { DCA_SPEAKER_C, DCA_SPEAKER_L, DCA_SPEAKER_R,  DCA_SPEAKER_Ls, DCA_SPEAKER_Rs }
};

static const uint8_t audio_mode_ch_mask[DCA_AMODE_COUNT] = {
    DCA_SPEAKER_LAYOUT_MONO,
    DCA_SPEAKER_LAYOUT_STEREO,
    DCA_SPEAKER_LAYOUT_STEREO,
    DCA_SPEAKER_LAYOUT_STEREO,
    DCA_SPEAKER_LAYOUT_STEREO,
    DCA_SPEAKER_LAYOUT_3_0,
    DCA_SPEAKER_LAYOUT_2_1,
    DCA_SPEAKER_LAYOUT_3_1,
    DCA_SPEAKER_LAYOUT_2_2,
    DCA_SPEAKER_LAYOUT_5POINT0
};

static const uint8_t block_code_nbits[7] = {
    7, 10, 12, 13, 15, 17, 19
};

static int dca_get_vlc(GetBitContext *s, DCAVLC *v, int i)
{
    return get_vlc2(s, v->vlc[i].table, v->vlc[i].bits, v->max_depth) + v->offset;
}

static void get_array(GetBitContext *s, int32_t *array, int size, int n)
{
    int i;

    for (i = 0; i < size; i++)
        array[i] = get_sbits(s, n);
}

// 5.3.1 - Bit stream header
static int parse_frame_header(DCACoreDecoder *s)
{
    DCACoreFrameHeader h = { 0 };
    int err = ff_dca_parse_core_frame_header(&h, &s->gb);

    if (err < 0) {
        switch (err) {
        case DCA_PARSE_ERROR_DEFICIT_SAMPLES:
            av_log(s->avctx, AV_LOG_ERROR, "Deficit samples are not supported\n");
            return h.normal_frame ? AVERROR_INVALIDDATA : AVERROR_PATCHWELCOME;

        case DCA_PARSE_ERROR_PCM_BLOCKS:
            av_log(s->avctx, AV_LOG_ERROR, "Unsupported number of PCM sample blocks (%d)\n", h.npcmblocks);
            return (h.npcmblocks < 6 || h.normal_frame) ? AVERROR_INVALIDDATA : AVERROR_PATCHWELCOME;

        case DCA_PARSE_ERROR_FRAME_SIZE:
            av_log(s->avctx, AV_LOG_ERROR, "Invalid core frame size (%d bytes)\n", h.frame_size);
            return AVERROR_INVALIDDATA;

        case DCA_PARSE_ERROR_AMODE:
            av_log(s->avctx, AV_LOG_ERROR, "Unsupported audio channel arrangement (%d)\n", h.audio_mode);
            return AVERROR_PATCHWELCOME;

        case DCA_PARSE_ERROR_SAMPLE_RATE:
            av_log(s->avctx, AV_LOG_ERROR, "Invalid core audio sampling frequency\n");
            return AVERROR_INVALIDDATA;

        case DCA_PARSE_ERROR_RESERVED_BIT:
            av_log(s->avctx, AV_LOG_ERROR, "Reserved bit set\n");
            return AVERROR_INVALIDDATA;

        case DCA_PARSE_ERROR_LFE_FLAG:
            av_log(s->avctx, AV_LOG_ERROR, "Invalid low frequency effects flag\n");
            return AVERROR_INVALIDDATA;

        case DCA_PARSE_ERROR_PCM_RES:
            av_log(s->avctx, AV_LOG_ERROR, "Invalid source PCM resolution\n");
            return AVERROR_INVALIDDATA;

        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unknown core frame header error\n");
            return AVERROR_INVALIDDATA;
        }
    }

    s->crc_present          = h.crc_present;
    s->npcmblocks           = h.npcmblocks;
    s->frame_size           = h.frame_size;
    s->audio_mode           = h.audio_mode;
    s->sample_rate          = ff_dca_sample_rates[h.sr_code];
    s->bit_rate             = ff_dca_bit_rates[h.br_code];
    s->drc_present          = h.drc_present;
    s->ts_present           = h.ts_present;
    s->aux_present          = h.aux_present;
    s->ext_audio_type       = h.ext_audio_type;
    s->ext_audio_present    = h.ext_audio_present;
    s->sync_ssf             = h.sync_ssf;
    s->lfe_present          = h.lfe_present;
    s->predictor_history    = h.predictor_history;
    s->filter_perfect       = h.filter_perfect;
    s->source_pcm_res       = ff_dca_bits_per_sample[h.pcmr_code];
    s->es_format            = h.pcmr_code & 1;
    s->sumdiff_front        = h.sumdiff_front;
    s->sumdiff_surround     = h.sumdiff_surround;

    return 0;
}

// 5.3.2 - Primary audio coding header
static int parse_coding_header(DCACoreDecoder *s, enum HeaderType header, int xch_base)
{
    int n, ch, nchannels, header_size = 0, header_pos = get_bits_count(&s->gb);
    unsigned int mask, index;

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    switch (header) {
    case HEADER_CORE:
        // Number of subframes
        s->nsubframes = get_bits(&s->gb, 4) + 1;

        // Number of primary audio channels
        s->nchannels = get_bits(&s->gb, 3) + 1;
        if (s->nchannels != ff_dca_channels[s->audio_mode]) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid number of primary audio channels (%d) for audio channel arrangement (%d)\n", s->nchannels, s->audio_mode);
            return AVERROR_INVALIDDATA;
        }
        av_assert1(s->nchannels <= DCA_CHANNELS - 2);

        s->ch_mask = audio_mode_ch_mask[s->audio_mode];

        // Add LFE channel if present
        if (s->lfe_present)
            s->ch_mask |= DCA_SPEAKER_MASK_LFE1;
        break;

    case HEADER_XCH:
        s->nchannels = ff_dca_channels[s->audio_mode] + 1;
        av_assert1(s->nchannels <= DCA_CHANNELS - 1);
        s->ch_mask |= DCA_SPEAKER_MASK_Cs;
        break;

    case HEADER_XXCH:
        // Channel set header length
        header_size = get_bits(&s->gb, 7) + 1;

        // Check CRC
        if (s->xxch_crc_present
            && ff_dca_check_crc(s->avctx, &s->gb, header_pos, header_pos + header_size * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH channel set header checksum\n");
            return AVERROR_INVALIDDATA;
        }

        // Number of channels in a channel set
        nchannels = get_bits(&s->gb, 3) + 1;
        if (nchannels > DCA_XXCH_CHANNELS_MAX) {
            avpriv_request_sample(s->avctx, "%d XXCH channels", nchannels);
            return AVERROR_PATCHWELCOME;
        }
        s->nchannels = ff_dca_channels[s->audio_mode] + nchannels;
        av_assert1(s->nchannels <= DCA_CHANNELS);

        // Loudspeaker layout mask
        mask = get_bits_long(&s->gb, s->xxch_mask_nbits - DCA_SPEAKER_Cs);
        s->xxch_spkr_mask = mask << DCA_SPEAKER_Cs;

        if (av_popcount(s->xxch_spkr_mask) != nchannels) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH speaker layout mask (%#x)\n", s->xxch_spkr_mask);
            return AVERROR_INVALIDDATA;
        }

        if (s->xxch_core_mask & s->xxch_spkr_mask) {
            av_log(s->avctx, AV_LOG_ERROR, "XXCH speaker layout mask (%#x) overlaps with core (%#x)\n", s->xxch_spkr_mask, s->xxch_core_mask);
            return AVERROR_INVALIDDATA;
        }

        // Combine core and XXCH masks together
        s->ch_mask = s->xxch_core_mask | s->xxch_spkr_mask;

        // Downmix coefficients present in stream
        if (get_bits1(&s->gb)) {
            int *coeff_ptr = s->xxch_dmix_coeff;

            // Downmix already performed by encoder
            s->xxch_dmix_embedded = get_bits1(&s->gb);

            // Downmix scale factor
            index = get_bits(&s->gb, 6) * 4 - FF_DCA_DMIXTABLE_OFFSET - 3;
            if (index >= FF_DCA_INV_DMIXTABLE_SIZE) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH downmix scale index (%d)\n", index);
                return AVERROR_INVALIDDATA;
            }
            s->xxch_dmix_scale_inv = ff_dca_inv_dmixtable[index];

            // Downmix channel mapping mask
            for (ch = 0; ch < nchannels; ch++) {
                mask = get_bits_long(&s->gb, s->xxch_mask_nbits);
                if ((mask & s->xxch_core_mask) != mask) {
                    av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH downmix channel mapping mask (%#x)\n", mask);
                    return AVERROR_INVALIDDATA;
                }
                s->xxch_dmix_mask[ch] = mask;
            }

            // Downmix coefficients
            for (ch = 0; ch < nchannels; ch++) {
                for (n = 0; n < s->xxch_mask_nbits; n++) {
                    if (s->xxch_dmix_mask[ch] & (1U << n)) {
                        int code = get_bits(&s->gb, 7);
                        int sign = (code >> 6) - 1;
                        if (code &= 63) {
                            index = code * 4 - 3;
                            if (index >= FF_DCA_DMIXTABLE_SIZE) {
                                av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH downmix coefficient index (%d)\n", index);
                                return AVERROR_INVALIDDATA;
                            }
                            *coeff_ptr++ = (ff_dca_dmixtable[index] ^ sign) - sign;
                        } else {
                            *coeff_ptr++ = 0;
                        }
                    }
                }
            }
        } else {
            s->xxch_dmix_embedded = 0;
        }

        break;
    }

    // Subband activity count
    for (ch = xch_base; ch < s->nchannels; ch++) {
        s->nsubbands[ch] = get_bits(&s->gb, 5) + 2;
        if (s->nsubbands[ch] > DCA_SUBBANDS) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid subband activity count\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // High frequency VQ start subband
    for (ch = xch_base; ch < s->nchannels; ch++)
        s->subband_vq_start[ch] = get_bits(&s->gb, 5) + 1;

    // Joint intensity coding index
    for (ch = xch_base; ch < s->nchannels; ch++) {
        if ((n = get_bits(&s->gb, 3)) && header == HEADER_XXCH)
            n += xch_base - 1;
        if (n > s->nchannels) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid joint intensity coding index\n");
            return AVERROR_INVALIDDATA;
        }
        s->joint_intensity_index[ch] = n;
    }

    // Transient mode code book
    for (ch = xch_base; ch < s->nchannels; ch++)
        s->transition_mode_sel[ch] = get_bits(&s->gb, 2);

    // Scale factor code book
    for (ch = xch_base; ch < s->nchannels; ch++) {
        s->scale_factor_sel[ch] = get_bits(&s->gb, 3);
        if (s->scale_factor_sel[ch] == 7) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid scale factor code book\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // Bit allocation quantizer select
    for (ch = xch_base; ch < s->nchannels; ch++) {
        s->bit_allocation_sel[ch] = get_bits(&s->gb, 3);
        if (s->bit_allocation_sel[ch] == 7) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid bit allocation quantizer select\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // Quantization index codebook select
    for (n = 0; n < DCA_CODE_BOOKS; n++)
        for (ch = xch_base; ch < s->nchannels; ch++)
            s->quant_index_sel[ch][n] = get_bits(&s->gb, ff_dca_quant_index_sel_nbits[n]);

    // Scale factor adjustment index
    for (n = 0; n < DCA_CODE_BOOKS; n++)
        for (ch = xch_base; ch < s->nchannels; ch++)
            if (s->quant_index_sel[ch][n] < ff_dca_quant_index_group_size[n])
                s->scale_factor_adj[ch][n] = ff_dca_scale_factor_adj[get_bits(&s->gb, 2)];

    if (header == HEADER_XXCH) {
        // Reserved
        // Byte align
        // CRC16 of channel set header
        if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Read past end of XXCH channel set header\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        // Audio header CRC check word
        if (s->crc_present)
            skip_bits(&s->gb, 16);
    }

    return 0;
}

static inline int parse_scale(DCACoreDecoder *s, int *scale_index, int sel)
{
    const uint32_t *scale_table;
    unsigned int scale_size;

    // Select the root square table
    if (sel > 5) {
        scale_table = ff_dca_scale_factor_quant7;
        scale_size = FF_ARRAY_ELEMS(ff_dca_scale_factor_quant7);
    } else {
        scale_table = ff_dca_scale_factor_quant6;
        scale_size = FF_ARRAY_ELEMS(ff_dca_scale_factor_quant6);
    }

    // If Huffman code was used, the difference of scales was encoded
    if (sel < 5)
        *scale_index += dca_get_vlc(&s->gb, &ff_dca_vlc_scale_factor, sel);
    else
        *scale_index = get_bits(&s->gb, sel + 1);

    // Look up scale factor from the root square table
    if ((unsigned int)*scale_index >= scale_size) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid scale factor index\n");
        return AVERROR_INVALIDDATA;
    }

    return scale_table[*scale_index];
}

static inline int parse_joint_scale(DCACoreDecoder *s, int sel)
{
    int scale_index;

    // Absolute value was encoded even when Huffman code was used
    if (sel < 5)
        scale_index = dca_get_vlc(&s->gb, &ff_dca_vlc_scale_factor, sel);
    else
        scale_index = get_bits(&s->gb, sel + 1);

    // Bias by 64
    scale_index += 64;

    // Look up joint scale factor
    if ((unsigned int)scale_index >= FF_ARRAY_ELEMS(ff_dca_joint_scale_factors)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid joint scale factor index\n");
        return AVERROR_INVALIDDATA;
    }

    return ff_dca_joint_scale_factors[scale_index];
}

// 5.4.1 - Primary audio coding side information
static int parse_subframe_header(DCACoreDecoder *s, int sf,
                                 enum HeaderType header, int xch_base)
{
    int ch, band, ret;

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    if (header == HEADER_CORE) {
        // Subsubframe count
        s->nsubsubframes[sf] = get_bits(&s->gb, 2) + 1;

        // Partial subsubframe sample count
        skip_bits(&s->gb, 3);
    }

    // Prediction mode
    for (ch = xch_base; ch < s->nchannels; ch++)
        for (band = 0; band < s->nsubbands[ch]; band++)
            s->prediction_mode[ch][band] = get_bits1(&s->gb);

    // Prediction coefficients VQ address
    for (ch = xch_base; ch < s->nchannels; ch++)
        for (band = 0; band < s->nsubbands[ch]; band++)
            if (s->prediction_mode[ch][band])
                s->prediction_vq_index[ch][band] = get_bits(&s->gb, 12);

    // Bit allocation index
    for (ch = xch_base; ch < s->nchannels; ch++) {
        int sel = s->bit_allocation_sel[ch];

        for (band = 0; band < s->subband_vq_start[ch]; band++) {
            int abits;

            if (sel < 5)
                abits = dca_get_vlc(&s->gb, &ff_dca_vlc_bit_allocation, sel);
            else
                abits = get_bits(&s->gb, sel - 1);

            if (abits > DCA_ABITS_MAX) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid bit allocation index\n");
                return AVERROR_INVALIDDATA;
            }

            s->bit_allocation[ch][band] = abits;
        }
    }

    // Transition mode
    for (ch = xch_base; ch < s->nchannels; ch++) {
        // Clear transition mode for all subbands
        memset(s->transition_mode[sf][ch], 0, sizeof(s->transition_mode[0][0]));

        // Transient possible only if more than one subsubframe
        if (s->nsubsubframes[sf] > 1) {
            int sel = s->transition_mode_sel[ch];
            for (band = 0; band < s->subband_vq_start[ch]; band++)
                if (s->bit_allocation[ch][band])
                    s->transition_mode[sf][ch][band] = dca_get_vlc(&s->gb, &ff_dca_vlc_transition_mode, sel);
        }
    }

    // Scale factors
    for (ch = xch_base; ch < s->nchannels; ch++) {
        int sel = s->scale_factor_sel[ch];
        int scale_index = 0;

        // Extract scales for subbands up to VQ
        for (band = 0; band < s->subband_vq_start[ch]; band++) {
            if (s->bit_allocation[ch][band]) {
                if ((ret = parse_scale(s, &scale_index, sel)) < 0)
                    return ret;
                s->scale_factors[ch][band][0] = ret;
                if (s->transition_mode[sf][ch][band]) {
                    if ((ret = parse_scale(s, &scale_index, sel)) < 0)
                        return ret;
                    s->scale_factors[ch][band][1] = ret;
                }
            } else {
                s->scale_factors[ch][band][0] = 0;
            }
        }

        // High frequency VQ subbands
        for (band = s->subband_vq_start[ch]; band < s->nsubbands[ch]; band++) {
            if ((ret = parse_scale(s, &scale_index, sel)) < 0)
                return ret;
            s->scale_factors[ch][band][0] = ret;
        }
    }

    // Joint subband codebook select
    for (ch = xch_base; ch < s->nchannels; ch++) {
        if (s->joint_intensity_index[ch]) {
            s->joint_scale_sel[ch] = get_bits(&s->gb, 3);
            if (s->joint_scale_sel[ch] == 7) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid joint scale factor code book\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    // Scale factors for joint subband coding
    for (ch = xch_base; ch < s->nchannels; ch++) {
        int src_ch = s->joint_intensity_index[ch] - 1;
        if (src_ch >= 0) {
            int sel = s->joint_scale_sel[ch];
            for (band = s->nsubbands[ch]; band < s->nsubbands[src_ch]; band++) {
                if ((ret = parse_joint_scale(s, sel)) < 0)
                    return ret;
                s->joint_scale_factors[ch][band] = ret;
            }
        }
    }

    // Dynamic range coefficient
    if (s->drc_present && header == HEADER_CORE)
        skip_bits(&s->gb, 8);

    // Side information CRC check word
    if (s->crc_present)
        skip_bits(&s->gb, 16);

    return 0;
}

#ifndef decode_blockcodes
static inline int decode_blockcodes(int code1, int code2, int levels, int32_t *audio)
{
    int offset = (levels - 1) / 2;
    int n, div;

    for (n = 0; n < DCA_SUBBAND_SAMPLES / 2; n++) {
        div = FASTDIV(code1, levels);
        audio[n] = code1 - div * levels - offset;
        code1 = div;
    }
    for (; n < DCA_SUBBAND_SAMPLES; n++) {
        div = FASTDIV(code2, levels);
        audio[n] = code2 - div * levels - offset;
        code2 = div;
    }

    return code1 | code2;
}
#endif

static inline int parse_block_codes(DCACoreDecoder *s, int32_t *audio, int abits)
{
    // Extract block code indices from the bit stream
    int code1 = get_bits(&s->gb, block_code_nbits[abits - 1]);
    int code2 = get_bits(&s->gb, block_code_nbits[abits - 1]);
    int levels = ff_dca_quant_levels[abits];

    // Look up samples from the block code book
    if (decode_blockcodes(code1, code2, levels, audio)) {
        av_log(s->avctx, AV_LOG_ERROR, "Failed to decode block code(s)\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static inline int parse_huffman_codes(DCACoreDecoder *s, int32_t *audio, int abits, int sel)
{
    int i;

    // Extract Huffman codes from the bit stream
    for (i = 0; i < DCA_SUBBAND_SAMPLES; i++)
        audio[i] = dca_get_vlc(&s->gb, &ff_dca_vlc_quant_index[abits - 1], sel);

    return 1;
}

static inline int extract_audio(DCACoreDecoder *s, int32_t *audio, int abits, int ch)
{
    av_assert1(abits >= 0 && abits <= DCA_ABITS_MAX);

    if (abits == 0) {
        // No bits allocated
        memset(audio, 0, DCA_SUBBAND_SAMPLES * sizeof(*audio));
        return 0;
    }

    if (abits <= DCA_CODE_BOOKS) {
        int sel = s->quant_index_sel[ch][abits - 1];
        if (sel < ff_dca_quant_index_group_size[abits - 1]) {
            // Huffman codes
            return parse_huffman_codes(s, audio, abits, sel);
        }
        if (abits <= 7) {
            // Block codes
            return parse_block_codes(s, audio, abits);
        }
    }

    // No further encoding
    get_array(&s->gb, audio, DCA_SUBBAND_SAMPLES, abits - 3);
    return 0;
}

static inline void inverse_adpcm(int32_t **subband_samples,
                                 const int16_t *vq_index,
                                 const int8_t *prediction_mode,
                                 int sb_start, int sb_end,
                                 int ofs, int len)
{
    int i, j;

    for (i = sb_start; i < sb_end; i++) {
        if (prediction_mode[i]) {
            const int pred_id = vq_index[i];
            int32_t *ptr = subband_samples[i] + ofs;
            for (j = 0; j < len; j++) {
                int32_t x = ff_dcaadpcm_predict(pred_id, ptr + j - DCA_ADPCM_COEFFS);
                ptr[j] = clip23(ptr[j] + x);
            }
        }
    }
}

// 5.5 - Primary audio data arrays
static int parse_subframe_audio(DCACoreDecoder *s, int sf, enum HeaderType header,
                                int xch_base, int *sub_pos, int *lfe_pos)
{
    int32_t audio[16], scale;
    int n, ssf, ofs, ch, band;

    // Check number of subband samples in this subframe
    int nsamples = s->nsubsubframes[sf] * DCA_SUBBAND_SAMPLES;
    if (*sub_pos + nsamples > s->npcmblocks) {
        av_log(s->avctx, AV_LOG_ERROR, "Subband sample buffer overflow\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    // VQ encoded subbands
    for (ch = xch_base; ch < s->nchannels; ch++) {
        int32_t vq_index[DCA_SUBBANDS];

        for (band = s->subband_vq_start[ch]; band < s->nsubbands[ch]; band++)
            // Extract the VQ address from the bit stream
            vq_index[band] = get_bits(&s->gb, 10);

        if (s->subband_vq_start[ch] < s->nsubbands[ch]) {
            s->dcadsp->decode_hf(s->subband_samples[ch], vq_index,
                                 ff_dca_high_freq_vq, s->scale_factors[ch],
                                 s->subband_vq_start[ch], s->nsubbands[ch],
                                 *sub_pos, nsamples);
        }
    }

    // Low frequency effect data
    if (s->lfe_present && header == HEADER_CORE) {
        unsigned int index;

        // Determine number of LFE samples in this subframe
        int nlfesamples = 2 * s->lfe_present * s->nsubsubframes[sf];
        av_assert1((unsigned int)nlfesamples <= FF_ARRAY_ELEMS(audio));

        // Extract LFE samples from the bit stream
        get_array(&s->gb, audio, nlfesamples, 8);

        // Extract scale factor index from the bit stream
        index = get_bits(&s->gb, 8);
        if (index >= FF_ARRAY_ELEMS(ff_dca_scale_factor_quant7)) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid LFE scale factor index\n");
            return AVERROR_INVALIDDATA;
        }

        // Look up the 7-bit root square quantization table
        scale = ff_dca_scale_factor_quant7[index];

        // Account for quantizer step size which is 0.035
        scale = mul23(4697620 /* 0.035 * (1 << 27) */, scale);

        // Scale and take the LFE samples
        for (n = 0, ofs = *lfe_pos; n < nlfesamples; n++, ofs++)
            s->lfe_samples[ofs] = clip23(audio[n] * scale >> 4);

        // Advance LFE sample pointer for the next subframe
        *lfe_pos = ofs;
    }

    // Audio data
    for (ssf = 0, ofs = *sub_pos; ssf < s->nsubsubframes[sf]; ssf++) {
        for (ch = xch_base; ch < s->nchannels; ch++) {
            if (get_bits_left(&s->gb) < 0)
                return AVERROR_INVALIDDATA;

            // Not high frequency VQ subbands
            for (band = 0; band < s->subband_vq_start[ch]; band++) {
                int ret, trans_ssf, abits = s->bit_allocation[ch][band];
                int32_t step_size;

                // Extract bits from the bit stream
                if ((ret = extract_audio(s, audio, abits, ch)) < 0)
                    return ret;

                // Select quantization step size table and look up
                // quantization step size
                if (s->bit_rate == 3)
                    step_size = ff_dca_lossless_quant[abits];
                else
                    step_size = ff_dca_lossy_quant[abits];

                // Identify transient location
                trans_ssf = s->transition_mode[sf][ch][band];

                // Determine proper scale factor
                if (trans_ssf == 0 || ssf < trans_ssf)
                    scale = s->scale_factors[ch][band][0];
                else
                    scale = s->scale_factors[ch][band][1];

                // Adjust scale factor when SEL indicates Huffman code
                if (ret > 0) {
                    int64_t adj = s->scale_factor_adj[ch][abits - 1];
                    scale = clip23(adj * scale >> 22);
                }

                ff_dca_core_dequantize(s->subband_samples[ch][band] + ofs,
                           audio, step_size, scale, 0, DCA_SUBBAND_SAMPLES);
            }
        }

        // DSYNC
        if ((ssf == s->nsubsubframes[sf] - 1 || s->sync_ssf) && get_bits(&s->gb, 16) != 0xffff) {
            av_log(s->avctx, AV_LOG_ERROR, "DSYNC check failed\n");
            return AVERROR_INVALIDDATA;
        }

        ofs += DCA_SUBBAND_SAMPLES;
    }

    // Inverse ADPCM
    for (ch = xch_base; ch < s->nchannels; ch++) {
        inverse_adpcm(s->subband_samples[ch], s->prediction_vq_index[ch],
                      s->prediction_mode[ch], 0, s->nsubbands[ch],
                      *sub_pos, nsamples);
    }

    // Joint subband coding
    for (ch = xch_base; ch < s->nchannels; ch++) {
        int src_ch = s->joint_intensity_index[ch] - 1;
        if (src_ch >= 0) {
            s->dcadsp->decode_joint(s->subband_samples[ch], s->subband_samples[src_ch],
                                    s->joint_scale_factors[ch], s->nsubbands[ch],
                                    s->nsubbands[src_ch], *sub_pos, nsamples);
        }
    }

    // Advance subband sample pointer for the next subframe
    *sub_pos = ofs;
    return 0;
}

static void erase_adpcm_history(DCACoreDecoder *s)
{
    int ch, band;

    // Erase ADPCM history from previous frame if
    // predictor history switch was disabled
    for (ch = 0; ch < DCA_CHANNELS; ch++)
        for (band = 0; band < DCA_SUBBANDS; band++)
            AV_ZERO128(s->subband_samples[ch][band] - DCA_ADPCM_COEFFS);

    emms_c();
}

static int alloc_sample_buffer(DCACoreDecoder *s)
{
    int nchsamples = DCA_ADPCM_COEFFS + s->npcmblocks;
    int nframesamples = nchsamples * DCA_CHANNELS * DCA_SUBBANDS;
    int nlfesamples = DCA_LFE_HISTORY + s->npcmblocks / 2;
    unsigned int size = s->subband_size;
    int ch, band;

    // Reallocate subband sample buffer
    av_fast_mallocz(&s->subband_buffer, &s->subband_size,
                    (nframesamples + nlfesamples) * sizeof(int32_t));
    if (!s->subband_buffer)
        return AVERROR(ENOMEM);

    if (size != s->subband_size) {
        for (ch = 0; ch < DCA_CHANNELS; ch++)
            for (band = 0; band < DCA_SUBBANDS; band++)
                s->subband_samples[ch][band] = s->subband_buffer +
                    (ch * DCA_SUBBANDS + band) * nchsamples + DCA_ADPCM_COEFFS;
        s->lfe_samples = s->subband_buffer + nframesamples;
    }

    if (!s->predictor_history)
        erase_adpcm_history(s);

    return 0;
}

static int parse_frame_data(DCACoreDecoder *s, enum HeaderType header, int xch_base)
{
    int sf, ch, ret, band, sub_pos, lfe_pos;

    if ((ret = parse_coding_header(s, header, xch_base)) < 0)
        return ret;

    for (sf = 0, sub_pos = 0, lfe_pos = DCA_LFE_HISTORY; sf < s->nsubframes; sf++) {
        if ((ret = parse_subframe_header(s, sf, header, xch_base)) < 0)
            return ret;
        if ((ret = parse_subframe_audio(s, sf, header, xch_base, &sub_pos, &lfe_pos)) < 0)
            return ret;
    }

    for (ch = xch_base; ch < s->nchannels; ch++) {
        // Determine number of active subbands for this channel
        int nsubbands = s->nsubbands[ch];
        if (s->joint_intensity_index[ch])
            nsubbands = FFMAX(nsubbands, s->nsubbands[s->joint_intensity_index[ch] - 1]);

        // Update history for ADPCM
        for (band = 0; band < nsubbands; band++) {
            int32_t *samples = s->subband_samples[ch][band] - DCA_ADPCM_COEFFS;
            AV_COPY128(samples, samples + s->npcmblocks);
        }

        // Clear inactive subbands
        for (; band < DCA_SUBBANDS; band++) {
            int32_t *samples = s->subband_samples[ch][band] - DCA_ADPCM_COEFFS;
            memset(samples, 0, (DCA_ADPCM_COEFFS + s->npcmblocks) * sizeof(int32_t));
        }
    }

    emms_c();

    return 0;
}

static int parse_xch_frame(DCACoreDecoder *s)
{
    int ret;

    if (s->ch_mask & DCA_SPEAKER_MASK_Cs) {
        av_log(s->avctx, AV_LOG_ERROR, "XCH with Cs speaker already present\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = parse_frame_data(s, HEADER_XCH, s->nchannels)) < 0)
        return ret;

    // Seek to the end of core frame, don't trust XCH frame size
    if (ff_dca_seek_bits(&s->gb, s->frame_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of XCH frame\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int parse_xxch_frame(DCACoreDecoder *s)
{
    int xxch_nchsets, xxch_frame_size;
    int ret, mask, header_size, header_pos = get_bits_count(&s->gb);

    // XXCH sync word
    if (get_bits_long(&s->gb, 32) != DCA_SYNCWORD_XXCH) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH sync word\n");
        return AVERROR_INVALIDDATA;
    }

    // XXCH frame header length
    header_size = get_bits(&s->gb, 6) + 1;

    // Check XXCH frame header CRC
    if (ff_dca_check_crc(s->avctx, &s->gb, header_pos + 32, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid XXCH frame header checksum\n");
        return AVERROR_INVALIDDATA;
    }

    // CRC presence flag for channel set header
    s->xxch_crc_present = get_bits1(&s->gb);

    // Number of bits for loudspeaker mask
    s->xxch_mask_nbits = get_bits(&s->gb, 5) + 1;
    if (s->xxch_mask_nbits <= DCA_SPEAKER_Cs) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid number of bits for XXCH speaker mask (%d)\n", s->xxch_mask_nbits);
        return AVERROR_INVALIDDATA;
    }

    // Number of channel sets
    xxch_nchsets = get_bits(&s->gb, 2) + 1;
    if (xxch_nchsets > 1) {
        avpriv_request_sample(s->avctx, "%d XXCH channel sets", xxch_nchsets);
        return AVERROR_PATCHWELCOME;
    }

    // Channel set 0 data byte size
    xxch_frame_size = get_bits(&s->gb, 14) + 1;

    // Core loudspeaker activity mask
    s->xxch_core_mask = get_bits_long(&s->gb, s->xxch_mask_nbits);

    // Validate the core mask
    mask = s->ch_mask;

    if ((mask & DCA_SPEAKER_MASK_Ls) && (s->xxch_core_mask & DCA_SPEAKER_MASK_Lss))
        mask = (mask & ~DCA_SPEAKER_MASK_Ls) | DCA_SPEAKER_MASK_Lss;

    if ((mask & DCA_SPEAKER_MASK_Rs) && (s->xxch_core_mask & DCA_SPEAKER_MASK_Rss))
        mask = (mask & ~DCA_SPEAKER_MASK_Rs) | DCA_SPEAKER_MASK_Rss;

    if (mask != s->xxch_core_mask) {
        av_log(s->avctx, AV_LOG_ERROR, "XXCH core speaker activity mask (%#x) disagrees with core (%#x)\n", s->xxch_core_mask, mask);
        return AVERROR_INVALIDDATA;
    }

    // Reserved
    // Byte align
    // CRC16 of XXCH frame header
    if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of XXCH frame header\n");
        return AVERROR_INVALIDDATA;
    }

    // Parse XXCH channel set 0
    if ((ret = parse_frame_data(s, HEADER_XXCH, s->nchannels)) < 0)
        return ret;

    if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8 + xxch_frame_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of XXCH channel set\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int parse_xbr_subframe(DCACoreDecoder *s, int xbr_base_ch, int xbr_nchannels,
                              int *xbr_nsubbands, int xbr_transition_mode, int sf, int *sub_pos)
{
    int     xbr_nabits[DCA_CHANNELS];
    int     xbr_bit_allocation[DCA_CHANNELS][DCA_SUBBANDS];
    int     xbr_scale_nbits[DCA_CHANNELS];
    int32_t xbr_scale_factors[DCA_CHANNELS][DCA_SUBBANDS][2];
    int     ssf, ch, band, ofs;

    // Check number of subband samples in this subframe
    if (*sub_pos + s->nsubsubframes[sf] * DCA_SUBBAND_SAMPLES > s->npcmblocks) {
        av_log(s->avctx, AV_LOG_ERROR, "Subband sample buffer overflow\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    // Number of bits for XBR bit allocation index
    for (ch = xbr_base_ch; ch < xbr_nchannels; ch++)
        xbr_nabits[ch] = get_bits(&s->gb, 2) + 2;

    // XBR bit allocation index
    for (ch = xbr_base_ch; ch < xbr_nchannels; ch++) {
        for (band = 0; band < xbr_nsubbands[ch]; band++) {
            xbr_bit_allocation[ch][band] = get_bits(&s->gb, xbr_nabits[ch]);
            if (xbr_bit_allocation[ch][band] > DCA_ABITS_MAX) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid XBR bit allocation index\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    // Number of bits for scale indices
    for (ch = xbr_base_ch; ch < xbr_nchannels; ch++) {
        xbr_scale_nbits[ch] = get_bits(&s->gb, 3);
        if (!xbr_scale_nbits[ch]) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid number of bits for XBR scale factor index\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // XBR scale factors
    for (ch = xbr_base_ch; ch < xbr_nchannels; ch++) {
        const uint32_t *scale_table;
        int scale_size;

        // Select the root square table
        if (s->scale_factor_sel[ch] > 5) {
            scale_table = ff_dca_scale_factor_quant7;
            scale_size = FF_ARRAY_ELEMS(ff_dca_scale_factor_quant7);
        } else {
            scale_table = ff_dca_scale_factor_quant6;
            scale_size = FF_ARRAY_ELEMS(ff_dca_scale_factor_quant6);
        }

        // Parse scale factor indices and look up scale factors from the root
        // square table
        for (band = 0; band < xbr_nsubbands[ch]; band++) {
            if (xbr_bit_allocation[ch][band]) {
                int scale_index = get_bits(&s->gb, xbr_scale_nbits[ch]);
                if (scale_index >= scale_size) {
                    av_log(s->avctx, AV_LOG_ERROR, "Invalid XBR scale factor index\n");
                    return AVERROR_INVALIDDATA;
                }
                xbr_scale_factors[ch][band][0] = scale_table[scale_index];
                if (xbr_transition_mode && s->transition_mode[sf][ch][band]) {
                    scale_index = get_bits(&s->gb, xbr_scale_nbits[ch]);
                    if (scale_index >= scale_size) {
                        av_log(s->avctx, AV_LOG_ERROR, "Invalid XBR scale factor index\n");
                        return AVERROR_INVALIDDATA;
                    }
                    xbr_scale_factors[ch][band][1] = scale_table[scale_index];
                }
            }
        }
    }

    // Audio data
    for (ssf = 0, ofs = *sub_pos; ssf < s->nsubsubframes[sf]; ssf++) {
        for (ch = xbr_base_ch; ch < xbr_nchannels; ch++) {
            if (get_bits_left(&s->gb) < 0)
                return AVERROR_INVALIDDATA;

            for (band = 0; band < xbr_nsubbands[ch]; band++) {
                int ret, trans_ssf, abits = xbr_bit_allocation[ch][band];
                int32_t audio[DCA_SUBBAND_SAMPLES], step_size, scale;

                // Extract bits from the bit stream
                if (abits > 7) {
                    // No further encoding
                    get_array(&s->gb, audio, DCA_SUBBAND_SAMPLES, abits - 3);
                } else if (abits > 0) {
                    // Block codes
                    if ((ret = parse_block_codes(s, audio, abits)) < 0)
                        return ret;
                } else {
                    // No bits allocated
                    continue;
                }

                // Look up quantization step size
                step_size = ff_dca_lossless_quant[abits];

                // Identify transient location
                if (xbr_transition_mode)
                    trans_ssf = s->transition_mode[sf][ch][band];
                else
                    trans_ssf = 0;

                // Determine proper scale factor
                if (trans_ssf == 0 || ssf < trans_ssf)
                    scale = xbr_scale_factors[ch][band][0];
                else
                    scale = xbr_scale_factors[ch][band][1];

                ff_dca_core_dequantize(s->subband_samples[ch][band] + ofs,
                           audio, step_size, scale, 1, DCA_SUBBAND_SAMPLES);
            }
        }

        // DSYNC
        if ((ssf == s->nsubsubframes[sf] - 1 || s->sync_ssf) && get_bits(&s->gb, 16) != 0xffff) {
            av_log(s->avctx, AV_LOG_ERROR, "XBR-DSYNC check failed\n");
            return AVERROR_INVALIDDATA;
        }

        ofs += DCA_SUBBAND_SAMPLES;
    }

    // Advance subband sample pointer for the next subframe
    *sub_pos = ofs;
    return 0;
}

static int parse_xbr_frame(DCACoreDecoder *s)
{
    int     xbr_frame_size[DCA_EXSS_CHSETS_MAX];
    int     xbr_nchannels[DCA_EXSS_CHSETS_MAX];
    int     xbr_nsubbands[DCA_EXSS_CHSETS_MAX * DCA_EXSS_CHANNELS_MAX];
    int     xbr_nchsets, xbr_transition_mode, xbr_band_nbits, xbr_base_ch;
    int     i, ch1, ch2, ret, header_size, header_pos = get_bits_count(&s->gb);

    // XBR sync word
    if (get_bits_long(&s->gb, 32) != DCA_SYNCWORD_XBR) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid XBR sync word\n");
        return AVERROR_INVALIDDATA;
    }

    // XBR frame header length
    header_size = get_bits(&s->gb, 6) + 1;

    // Check XBR frame header CRC
    if (ff_dca_check_crc(s->avctx, &s->gb, header_pos + 32, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid XBR frame header checksum\n");
        return AVERROR_INVALIDDATA;
    }

    // Number of channel sets
    xbr_nchsets = get_bits(&s->gb, 2) + 1;

    // Channel set data byte size
    for (i = 0; i < xbr_nchsets; i++)
        xbr_frame_size[i] = get_bits(&s->gb, 14) + 1;

    // Transition mode flag
    xbr_transition_mode = get_bits1(&s->gb);

    // Channel set headers
    for (i = 0, ch2 = 0; i < xbr_nchsets; i++) {
        xbr_nchannels[i] = get_bits(&s->gb, 3) + 1;
        xbr_band_nbits = get_bits(&s->gb, 2) + 5;
        for (ch1 = 0; ch1 < xbr_nchannels[i]; ch1++, ch2++) {
            xbr_nsubbands[ch2] = get_bits(&s->gb, xbr_band_nbits) + 1;
            if (xbr_nsubbands[ch2] > DCA_SUBBANDS) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid number of active XBR subbands (%d)\n", xbr_nsubbands[ch2]);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    // Reserved
    // Byte align
    // CRC16 of XBR frame header
    if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of XBR frame header\n");
        return AVERROR_INVALIDDATA;
    }

    // Channel set data
    for (i = 0, xbr_base_ch = 0; i < xbr_nchsets; i++) {
        header_pos = get_bits_count(&s->gb);

        if (xbr_base_ch + xbr_nchannels[i] <= s->nchannels) {
            int sf, sub_pos;

            for (sf = 0, sub_pos = 0; sf < s->nsubframes; sf++) {
                if ((ret = parse_xbr_subframe(s, xbr_base_ch,
                                              xbr_base_ch + xbr_nchannels[i],
                                              xbr_nsubbands, xbr_transition_mode,
                                              sf, &sub_pos)) < 0)
                    return ret;
            }
        }

        xbr_base_ch += xbr_nchannels[i];

        if (ff_dca_seek_bits(&s->gb, header_pos + xbr_frame_size[i] * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Read past end of XBR channel set\n");
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

// Modified ISO/IEC 9899 linear congruential generator
// Returns pseudorandom integer in range [-2^30, 2^30 - 1]
static int rand_x96(DCACoreDecoder *s)
{
    s->x96_rand = 1103515245U * s->x96_rand + 12345U;
    return (s->x96_rand & 0x7fffffff) - 0x40000000;
}

static int parse_x96_subframe_audio(DCACoreDecoder *s, int sf, int xch_base, int *sub_pos)
{
    int n, ssf, ch, band, ofs;

    // Check number of subband samples in this subframe
    int nsamples = s->nsubsubframes[sf] * DCA_SUBBAND_SAMPLES;
    if (*sub_pos + nsamples > s->npcmblocks) {
        av_log(s->avctx, AV_LOG_ERROR, "Subband sample buffer overflow\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    // VQ encoded or unallocated subbands
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++) {
            // Get the sample pointer and scale factor
            int32_t *samples = s->x96_subband_samples[ch][band] + *sub_pos;
            int32_t scale    = s->scale_factors[ch][band >> 1][band & 1];

            switch (s->bit_allocation[ch][band]) {
            case 0: // No bits allocated for subband
                if (scale <= 1)
                    memset(samples, 0, nsamples * sizeof(int32_t));
                else for (n = 0; n < nsamples; n++)
                    // Generate scaled random samples
                    samples[n] = mul31(rand_x96(s), scale);
                break;

            case 1: // VQ encoded subband
                for (ssf = 0; ssf < (s->nsubsubframes[sf] + 1) / 2; ssf++) {
                    // Extract the VQ address from the bit stream and look up
                    // the VQ code book for up to 16 subband samples
                    const int8_t *vq_samples = ff_dca_high_freq_vq[get_bits(&s->gb, 10)];
                    // Scale and take the samples
                    for (n = 0; n < FFMIN(nsamples - ssf * 16, 16); n++)
                        *samples++ = clip23(vq_samples[n] * scale + (1 << 3) >> 4);
                }
                break;
            }
        }
    }

    // Audio data
    for (ssf = 0, ofs = *sub_pos; ssf < s->nsubsubframes[sf]; ssf++) {
        for (ch = xch_base; ch < s->x96_nchannels; ch++) {
            if (get_bits_left(&s->gb) < 0)
                return AVERROR_INVALIDDATA;

            for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++) {
                int ret, abits = s->bit_allocation[ch][band] - 1;
                int32_t audio[DCA_SUBBAND_SAMPLES], step_size, scale;

                // Not VQ encoded or unallocated subbands
                if (abits < 1)
                    continue;

                // Extract bits from the bit stream
                if ((ret = extract_audio(s, audio, abits, ch)) < 0)
                    return ret;

                // Select quantization step size table and look up quantization
                // step size
                if (s->bit_rate == 3)
                    step_size = ff_dca_lossless_quant[abits];
                else
                    step_size = ff_dca_lossy_quant[abits];

                // Get the scale factor
                scale = s->scale_factors[ch][band >> 1][band & 1];

                ff_dca_core_dequantize(s->x96_subband_samples[ch][band] + ofs,
                           audio, step_size, scale, 0, DCA_SUBBAND_SAMPLES);
            }
        }

        // DSYNC
        if ((ssf == s->nsubsubframes[sf] - 1 || s->sync_ssf) && get_bits(&s->gb, 16) != 0xffff) {
            av_log(s->avctx, AV_LOG_ERROR, "X96-DSYNC check failed\n");
            return AVERROR_INVALIDDATA;
        }

        ofs += DCA_SUBBAND_SAMPLES;
    }

    // Inverse ADPCM
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        inverse_adpcm(s->x96_subband_samples[ch], s->prediction_vq_index[ch],
                      s->prediction_mode[ch], s->x96_subband_start, s->nsubbands[ch],
                      *sub_pos, nsamples);
    }

    // Joint subband coding
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        int src_ch = s->joint_intensity_index[ch] - 1;
        if (src_ch >= 0) {
            s->dcadsp->decode_joint(s->x96_subband_samples[ch], s->x96_subband_samples[src_ch],
                                    s->joint_scale_factors[ch], s->nsubbands[ch],
                                    s->nsubbands[src_ch], *sub_pos, nsamples);
        }
    }

    // Advance subband sample pointer for the next subframe
    *sub_pos = ofs;
    return 0;
}

static void erase_x96_adpcm_history(DCACoreDecoder *s)
{
    int ch, band;

    // Erase ADPCM history from previous frame if
    // predictor history switch was disabled
    for (ch = 0; ch < DCA_CHANNELS; ch++)
        for (band = 0; band < DCA_SUBBANDS_X96; band++)
            AV_ZERO128(s->x96_subband_samples[ch][band] - DCA_ADPCM_COEFFS);

    emms_c();
}

static int alloc_x96_sample_buffer(DCACoreDecoder *s)
{
    int nchsamples = DCA_ADPCM_COEFFS + s->npcmblocks;
    int nframesamples = nchsamples * DCA_CHANNELS * DCA_SUBBANDS_X96;
    unsigned int size = s->x96_subband_size;
    int ch, band;

    // Reallocate subband sample buffer
    av_fast_mallocz(&s->x96_subband_buffer, &s->x96_subband_size,
                    nframesamples * sizeof(int32_t));
    if (!s->x96_subband_buffer)
        return AVERROR(ENOMEM);

    if (size != s->x96_subband_size) {
        for (ch = 0; ch < DCA_CHANNELS; ch++)
            for (band = 0; band < DCA_SUBBANDS_X96; band++)
                s->x96_subband_samples[ch][band] = s->x96_subband_buffer +
                    (ch * DCA_SUBBANDS_X96 + band) * nchsamples + DCA_ADPCM_COEFFS;
    }

    if (!s->predictor_history)
        erase_x96_adpcm_history(s);

    return 0;
}

static int parse_x96_subframe_header(DCACoreDecoder *s, int xch_base)
{
    int ch, band, ret;

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    // Prediction mode
    for (ch = xch_base; ch < s->x96_nchannels; ch++)
        for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++)
            s->prediction_mode[ch][band] = get_bits1(&s->gb);

    // Prediction coefficients VQ address
    for (ch = xch_base; ch < s->x96_nchannels; ch++)
        for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++)
            if (s->prediction_mode[ch][band])
                s->prediction_vq_index[ch][band] = get_bits(&s->gb, 12);

    // Bit allocation index
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        int sel = s->bit_allocation_sel[ch];
        int abits = 0;

        for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++) {
            // If Huffman code was used, the difference of abits was encoded
            if (sel < 7)
                abits += dca_get_vlc(&s->gb, &ff_dca_vlc_quant_index[5 + 2 * s->x96_high_res], sel);
            else
                abits = get_bits(&s->gb, 3 + s->x96_high_res);

            if (abits < 0 || abits > 7 + 8 * s->x96_high_res) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 bit allocation index\n");
                return AVERROR_INVALIDDATA;
            }

            s->bit_allocation[ch][band] = abits;
        }
    }

    // Scale factors
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        int sel = s->scale_factor_sel[ch];
        int scale_index = 0;

        // Extract scales for subbands which are transmitted even for
        // unallocated subbands
        for (band = s->x96_subband_start; band < s->nsubbands[ch]; band++) {
            if ((ret = parse_scale(s, &scale_index, sel)) < 0)
                return ret;
            s->scale_factors[ch][band >> 1][band & 1] = ret;
        }
    }

    // Joint subband codebook select
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        if (s->joint_intensity_index[ch]) {
            s->joint_scale_sel[ch] = get_bits(&s->gb, 3);
            if (s->joint_scale_sel[ch] == 7) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 joint scale factor code book\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    // Scale factors for joint subband coding
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        int src_ch = s->joint_intensity_index[ch] - 1;
        if (src_ch >= 0) {
            int sel = s->joint_scale_sel[ch];
            for (band = s->nsubbands[ch]; band < s->nsubbands[src_ch]; band++) {
                if ((ret = parse_joint_scale(s, sel)) < 0)
                    return ret;
                s->joint_scale_factors[ch][band] = ret;
            }
        }
    }

    // Side information CRC check word
    if (s->crc_present)
        skip_bits(&s->gb, 16);

    return 0;
}

static int parse_x96_coding_header(DCACoreDecoder *s, int exss, int xch_base)
{
    int n, ch, header_size = 0, header_pos = get_bits_count(&s->gb);

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    if (exss) {
        // Channel set header length
        header_size = get_bits(&s->gb, 7) + 1;

        // Check CRC
        if (s->x96_crc_present
            && ff_dca_check_crc(s->avctx, &s->gb, header_pos, header_pos + header_size * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 channel set header checksum\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // High resolution flag
    s->x96_high_res = get_bits1(&s->gb);

    // First encoded subband
    if (s->x96_rev_no < 8) {
        s->x96_subband_start = get_bits(&s->gb, 5);
        if (s->x96_subband_start > 27) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 subband start index (%d)\n", s->x96_subband_start);
            return AVERROR_INVALIDDATA;
        }
    } else {
        s->x96_subband_start = DCA_SUBBANDS;
    }

    // Subband activity count
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        s->nsubbands[ch] = get_bits(&s->gb, 6) + 1;
        if (s->nsubbands[ch] < DCA_SUBBANDS) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 subband activity count (%d)\n", s->nsubbands[ch]);
            return AVERROR_INVALIDDATA;
        }
    }

    // Joint intensity coding index
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        if ((n = get_bits(&s->gb, 3)) && xch_base)
            n += xch_base - 1;
        if (n > s->x96_nchannels) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 joint intensity coding index\n");
            return AVERROR_INVALIDDATA;
        }
        s->joint_intensity_index[ch] = n;
    }

    // Scale factor code book
    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        s->scale_factor_sel[ch] = get_bits(&s->gb, 3);
        if (s->scale_factor_sel[ch] >= 6) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 scale factor code book\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // Bit allocation quantizer select
    for (ch = xch_base; ch < s->x96_nchannels; ch++)
        s->bit_allocation_sel[ch] = get_bits(&s->gb, 3);

    // Quantization index codebook select
    for (n = 0; n < 6 + 4 * s->x96_high_res; n++)
        for (ch = xch_base; ch < s->x96_nchannels; ch++)
            s->quant_index_sel[ch][n] = get_bits(&s->gb, ff_dca_quant_index_sel_nbits[n]);

    if (exss) {
        // Reserved
        // Byte align
        // CRC16 of channel set header
        if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Read past end of X96 channel set header\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (s->crc_present)
            skip_bits(&s->gb, 16);
    }

    return 0;
}

static int parse_x96_frame_data(DCACoreDecoder *s, int exss, int xch_base)
{
    int sf, ch, ret, band, sub_pos;

    if ((ret = parse_x96_coding_header(s, exss, xch_base)) < 0)
        return ret;

    for (sf = 0, sub_pos = 0; sf < s->nsubframes; sf++) {
        if ((ret = parse_x96_subframe_header(s, xch_base)) < 0)
            return ret;
        if ((ret = parse_x96_subframe_audio(s, sf, xch_base, &sub_pos)) < 0)
            return ret;
    }

    for (ch = xch_base; ch < s->x96_nchannels; ch++) {
        // Determine number of active subbands for this channel
        int nsubbands = s->nsubbands[ch];
        if (s->joint_intensity_index[ch])
            nsubbands = FFMAX(nsubbands, s->nsubbands[s->joint_intensity_index[ch] - 1]);

        // Update history for ADPCM and clear inactive subbands
        for (band = 0; band < DCA_SUBBANDS_X96; band++) {
            int32_t *samples = s->x96_subband_samples[ch][band] - DCA_ADPCM_COEFFS;
            if (band >= s->x96_subband_start && band < nsubbands)
                AV_COPY128(samples, samples + s->npcmblocks);
            else
                memset(samples, 0, (DCA_ADPCM_COEFFS + s->npcmblocks) * sizeof(int32_t));
        }
    }

    emms_c();

    return 0;
}

static int parse_x96_frame(DCACoreDecoder *s)
{
    int ret;

    // Revision number
    s->x96_rev_no = get_bits(&s->gb, 4);
    if (s->x96_rev_no < 1 || s->x96_rev_no > 8) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 revision (%d)\n", s->x96_rev_no);
        return AVERROR_INVALIDDATA;
    }

    s->x96_crc_present = 0;
    s->x96_nchannels = s->nchannels;

    if ((ret = alloc_x96_sample_buffer(s)) < 0)
        return ret;

    if ((ret = parse_x96_frame_data(s, 0, 0)) < 0)
        return ret;

    // Seek to the end of core frame
    if (ff_dca_seek_bits(&s->gb, s->frame_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of X96 frame\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int parse_x96_frame_exss(DCACoreDecoder *s)
{
    int     x96_frame_size[DCA_EXSS_CHSETS_MAX];
    int     x96_nchannels[DCA_EXSS_CHSETS_MAX];
    int     x96_nchsets, x96_base_ch;
    int     i, ret, header_size, header_pos = get_bits_count(&s->gb);

    // X96 sync word
    if (get_bits_long(&s->gb, 32) != DCA_SYNCWORD_X96) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 sync word\n");
        return AVERROR_INVALIDDATA;
    }

    // X96 frame header length
    header_size = get_bits(&s->gb, 6) + 1;

    // Check X96 frame header CRC
    if (ff_dca_check_crc(s->avctx, &s->gb, header_pos + 32, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 frame header checksum\n");
        return AVERROR_INVALIDDATA;
    }

    // Revision number
    s->x96_rev_no = get_bits(&s->gb, 4);
    if (s->x96_rev_no < 1 || s->x96_rev_no > 8) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid X96 revision (%d)\n", s->x96_rev_no);
        return AVERROR_INVALIDDATA;
    }

    // CRC presence flag for channel set header
    s->x96_crc_present = get_bits1(&s->gb);

    // Number of channel sets
    x96_nchsets = get_bits(&s->gb, 2) + 1;

    // Channel set data byte size
    for (i = 0; i < x96_nchsets; i++)
        x96_frame_size[i] = get_bits(&s->gb, 12) + 1;

    // Number of channels in channel set
    for (i = 0; i < x96_nchsets; i++)
        x96_nchannels[i] = get_bits(&s->gb, 3) + 1;

    // Reserved
    // Byte align
    // CRC16 of X96 frame header
    if (ff_dca_seek_bits(&s->gb, header_pos + header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of X96 frame header\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = alloc_x96_sample_buffer(s)) < 0)
        return ret;

    // Channel set data
    s->x96_nchannels = 0;
    for (i = 0, x96_base_ch = 0; i < x96_nchsets; i++) {
        header_pos = get_bits_count(&s->gb);

        if (x96_base_ch + x96_nchannels[i] <= s->nchannels) {
            s->x96_nchannels = x96_base_ch + x96_nchannels[i];
            if ((ret = parse_x96_frame_data(s, 1, x96_base_ch)) < 0)
                return ret;
        }

        x96_base_ch += x96_nchannels[i];

        if (ff_dca_seek_bits(&s->gb, header_pos + x96_frame_size[i] * 8)) {
            av_log(s->avctx, AV_LOG_ERROR, "Read past end of X96 channel set\n");
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int parse_aux_data(DCACoreDecoder *s)
{
    int aux_pos;

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    // Auxiliary data byte count (can't be trusted)
    skip_bits(&s->gb, 6);

    // 4-byte align
    skip_bits_long(&s->gb, -get_bits_count(&s->gb) & 31);

    // Auxiliary data sync word
    if (get_bits_long(&s->gb, 32) != DCA_SYNCWORD_REV1AUX) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid auxiliary data sync word\n");
        return AVERROR_INVALIDDATA;
    }

    aux_pos = get_bits_count(&s->gb);

    // Auxiliary decode time stamp flag
    if (get_bits1(&s->gb))
        skip_bits_long(&s->gb, 47);

    // Auxiliary dynamic downmix flag
    if (s->prim_dmix_embedded = get_bits1(&s->gb)) {
        int i, m, n;

        // Auxiliary primary channel downmix type
        s->prim_dmix_type = get_bits(&s->gb, 3);
        if (s->prim_dmix_type >= DCA_DMIX_TYPE_COUNT) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid primary channel set downmix type\n");
            return AVERROR_INVALIDDATA;
        }

        // Size of downmix coefficients matrix
        m = ff_dca_dmix_primary_nch[s->prim_dmix_type];
        n = ff_dca_channels[s->audio_mode] + !!s->lfe_present;

        // Dynamic downmix code coefficients
        for (i = 0; i < m * n; i++) {
            int code = get_bits(&s->gb, 9);
            int sign = (code >> 8) - 1;
            unsigned int index = code & 0xff;
            if (index >= FF_DCA_DMIXTABLE_SIZE) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid downmix coefficient index\n");
                return AVERROR_INVALIDDATA;
            }
            s->prim_dmix_coeff[i] = (ff_dca_dmixtable[index] ^ sign) - sign;
        }
    }

    // Byte align
    skip_bits(&s->gb, -get_bits_count(&s->gb) & 7);

    // CRC16 of auxiliary data
    skip_bits(&s->gb, 16);

    // Check CRC
    if (ff_dca_check_crc(s->avctx, &s->gb, aux_pos, get_bits_count(&s->gb))) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid auxiliary data checksum\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int parse_optional_info(DCACoreDecoder *s)
{
    DCAContext *dca = s->avctx->priv_data;
    int ret = -1;

    // Time code stamp
    if (s->ts_present)
        skip_bits_long(&s->gb, 32);

    // Auxiliary data
    if (s->aux_present && (ret = parse_aux_data(s)) < 0
        && (s->avctx->err_recognition & AV_EF_EXPLODE))
        return ret;

    if (ret < 0)
        s->prim_dmix_embedded = 0;

    // Core extensions
    if (s->ext_audio_present && !dca->core_only) {
        int sync_pos = FFMIN(s->frame_size / 4, s->gb.size_in_bits / 32) - 1;
        int last_pos = get_bits_count(&s->gb) / 32;
        int size, dist;
        uint32_t w1, w2 = 0;

        // Search for extension sync words aligned on 4-byte boundary. Search
        // must be done backwards from the end of core frame to work around
        // sync word aliasing issues.
        switch (s->ext_audio_type) {
        case DCA_EXT_AUDIO_XCH:
            if (dca->request_channel_layout)
                break;

            // The distance between XCH sync word and end of the core frame
            // must be equal to XCH frame size. Off by one error is allowed for
            // compatibility with legacy bitstreams. Minimum XCH frame size is
            // 96 bytes. AMODE and PCHS are further checked to reduce
            // probability of alias sync detection.
            for (; sync_pos >= last_pos; sync_pos--, w2 = w1) {
                w1 = AV_RB32(s->gb.buffer + sync_pos * 4);
                if (w1 == DCA_SYNCWORD_XCH) {
                    size = (w2 >> 22) + 1;
                    dist = s->frame_size - sync_pos * 4;
                    if (size >= 96
                        && (size == dist || size - 1 == dist)
                        && (w2 >> 15 & 0x7f) == 0x08) {
                        s->xch_pos = sync_pos * 32 + 49;
                        break;
                    }
                }
            }

            if (!s->xch_pos) {
                av_log(s->avctx, AV_LOG_ERROR, "XCH sync word not found\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }
            break;

        case DCA_EXT_AUDIO_X96:
            // The distance between X96 sync word and end of the core frame
            // must be equal to X96 frame size. Minimum X96 frame size is 96
            // bytes.
            for (; sync_pos >= last_pos; sync_pos--, w2 = w1) {
                w1 = AV_RB32(s->gb.buffer + sync_pos * 4);
                if (w1 == DCA_SYNCWORD_X96) {
                    size = (w2 >> 20) + 1;
                    dist = s->frame_size - sync_pos * 4;
                    if (size >= 96 && size == dist) {
                        s->x96_pos = sync_pos * 32 + 44;
                        break;
                    }
                }
            }

            if (!s->x96_pos) {
                av_log(s->avctx, AV_LOG_ERROR, "X96 sync word not found\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }
            break;

        case DCA_EXT_AUDIO_XXCH:
            if (dca->request_channel_layout)
                break;

            // XXCH frame header CRC must be valid. Minimum XXCH frame header
            // size is 11 bytes.
            for (; sync_pos >= last_pos; sync_pos--, w2 = w1) {
                w1 = AV_RB32(s->gb.buffer + sync_pos * 4);
                if (w1 == DCA_SYNCWORD_XXCH) {
                    size = (w2 >> 26) + 1;
                    dist = s->gb.size_in_bits / 8 - sync_pos * 4;
                    if (size >= 11 && size <= dist &&
                        !av_crc(dca->crctab, 0xffff, s->gb.buffer +
                                (sync_pos + 1) * 4, size - 4)) {
                        s->xxch_pos = sync_pos * 32;
                        break;
                    }
                }
            }

            if (!s->xxch_pos) {
                av_log(s->avctx, AV_LOG_ERROR, "XXCH sync word not found\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }
            break;
        }
    }

    return 0;
}

int ff_dca_core_parse(DCACoreDecoder *s, uint8_t *data, int size)
{
    int ret;

    s->ext_audio_mask = 0;
    s->xch_pos = s->xxch_pos = s->x96_pos = 0;

    if ((ret = init_get_bits8(&s->gb, data, size)) < 0)
        return ret;
    s->gb_in = s->gb;

    if ((ret = parse_frame_header(s)) < 0)
        return ret;
    if ((ret = alloc_sample_buffer(s)) < 0)
        return ret;
    if ((ret = parse_frame_data(s, HEADER_CORE, 0)) < 0)
        return ret;
    if ((ret = parse_optional_info(s)) < 0)
        return ret;

    // Workaround for DTS in WAV
    if (s->frame_size > size)
        s->frame_size = size;

    if (ff_dca_seek_bits(&s->gb, s->frame_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of core frame\n");
        if (s->avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

int ff_dca_core_parse_exss(DCACoreDecoder *s, uint8_t *data, DCAExssAsset *asset)
{
    AVCodecContext *avctx = s->avctx;
    DCAContext *dca = avctx->priv_data;
    int exss_mask = asset ? asset->extension_mask : 0;
    int ret = 0, ext = 0;

    // Parse (X)XCH unless downmixing
    if (!dca->request_channel_layout) {
        if (exss_mask & DCA_EXSS_XXCH) {
            if ((ret = init_get_bits8(&s->gb, data + asset->xxch_offset, asset->xxch_size)) < 0)
                return ret;
            ret = parse_xxch_frame(s);
            ext = DCA_EXSS_XXCH;
        } else if (s->xxch_pos) {
            s->gb = s->gb_in;
            skip_bits_long(&s->gb, s->xxch_pos);
            ret = parse_xxch_frame(s);
            ext = DCA_CSS_XXCH;
        } else if (s->xch_pos) {
            s->gb = s->gb_in;
            skip_bits_long(&s->gb, s->xch_pos);
            ret = parse_xch_frame(s);
            ext = DCA_CSS_XCH;
        }

        // Revert to primary channel set in case (X)XCH parsing fails
        if (ret < 0) {
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return ret;
            s->nchannels = ff_dca_channels[s->audio_mode];
            s->ch_mask = audio_mode_ch_mask[s->audio_mode];
            if (s->lfe_present)
                s->ch_mask |= DCA_SPEAKER_MASK_LFE1;
        } else {
            s->ext_audio_mask |= ext;
        }
    }

    // Parse XBR
    if (exss_mask & DCA_EXSS_XBR) {
        if ((ret = init_get_bits8(&s->gb, data + asset->xbr_offset, asset->xbr_size)) < 0)
            return ret;
        if ((ret = parse_xbr_frame(s)) < 0) {
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return ret;
        } else {
            s->ext_audio_mask |= DCA_EXSS_XBR;
        }
    }

    // Parse X96 unless decoding XLL
    if (!(dca->packet & DCA_PACKET_XLL)) {
        if (exss_mask & DCA_EXSS_X96) {
            if ((ret = init_get_bits8(&s->gb, data + asset->x96_offset, asset->x96_size)) < 0)
                return ret;
            if ((ret = parse_x96_frame_exss(s)) < 0) {
                if (ret == AVERROR(ENOMEM) || (avctx->err_recognition & AV_EF_EXPLODE))
                    return ret;
            } else {
                s->ext_audio_mask |= DCA_EXSS_X96;
            }
        } else if (s->x96_pos) {
            s->gb = s->gb_in;
            skip_bits_long(&s->gb, s->x96_pos);
            if ((ret = parse_x96_frame(s)) < 0) {
                if (ret == AVERROR(ENOMEM) || (avctx->err_recognition & AV_EF_EXPLODE))
                    return ret;
            } else {
                s->ext_audio_mask |= DCA_CSS_X96;
            }
        }
    }

    return 0;
}

static int map_prm_ch_to_spkr(DCACoreDecoder *s, int ch)
{
    int pos, spkr;

    // Try to map this channel to core first
    pos = ff_dca_channels[s->audio_mode];
    if (ch < pos) {
        spkr = prm_ch_to_spkr_map[s->audio_mode][ch];
        if (s->ext_audio_mask & (DCA_CSS_XXCH | DCA_EXSS_XXCH)) {
            if (s->xxch_core_mask & (1U << spkr))
                return spkr;
            if (spkr == DCA_SPEAKER_Ls && (s->xxch_core_mask & DCA_SPEAKER_MASK_Lss))
                return DCA_SPEAKER_Lss;
            if (spkr == DCA_SPEAKER_Rs && (s->xxch_core_mask & DCA_SPEAKER_MASK_Rss))
                return DCA_SPEAKER_Rss;
            return -1;
        }
        return spkr;
    }

    // Then XCH
    if ((s->ext_audio_mask & DCA_CSS_XCH) && ch == pos)
        return DCA_SPEAKER_Cs;

    // Then XXCH
    if (s->ext_audio_mask & (DCA_CSS_XXCH | DCA_EXSS_XXCH)) {
        for (spkr = DCA_SPEAKER_Cs; spkr < s->xxch_mask_nbits; spkr++)
            if (s->xxch_spkr_mask & (1U << spkr))
                if (pos++ == ch)
                    return spkr;
    }

    // No mapping
    return -1;
}

static void erase_dsp_history(DCACoreDecoder *s)
{
    memset(s->dcadsp_data, 0, sizeof(s->dcadsp_data));
    s->output_history_lfe_fixed = 0;
    s->output_history_lfe_float = 0;
}

static void set_filter_mode(DCACoreDecoder *s, int mode)
{
    if (s->filter_mode != mode) {
        erase_dsp_history(s);
        s->filter_mode = mode;
    }
}

int ff_dca_core_filter_fixed(DCACoreDecoder *s, int x96_synth)
{
    int n, ch, spkr, nsamples, x96_nchannels = 0;
    const int32_t *filter_coeff;
    int32_t *ptr;

    // Externally set x96_synth flag implies that X96 synthesis should be
    // enabled, yet actual X96 subband data should be discarded. This is a
    // special case for lossless residual decoder that ignores X96 data if
    // present.
    if (!x96_synth && (s->ext_audio_mask & (DCA_CSS_X96 | DCA_EXSS_X96))) {
        x96_nchannels = s->x96_nchannels;
        x96_synth = 1;
    }
    if (x96_synth < 0)
        x96_synth = 0;

    s->output_rate = s->sample_rate << x96_synth;
    s->npcmsamples = nsamples = (s->npcmblocks * DCA_PCMBLOCK_SAMPLES) << x96_synth;

    // Reallocate PCM output buffer
    av_fast_malloc(&s->output_buffer, &s->output_size,
                   nsamples * av_popcount(s->ch_mask) * sizeof(int32_t));
    if (!s->output_buffer)
        return AVERROR(ENOMEM);

    ptr = (int32_t *)s->output_buffer;
    for (spkr = 0; spkr < DCA_SPEAKER_COUNT; spkr++) {
        if (s->ch_mask & (1U << spkr)) {
            s->output_samples[spkr] = ptr;
            ptr += nsamples;
        } else {
            s->output_samples[spkr] = NULL;
        }
    }

    // Handle change of filtering mode
    set_filter_mode(s, x96_synth | DCA_FILTER_MODE_FIXED);

    // Select filter
    if (x96_synth)
        filter_coeff = ff_dca_fir_64bands_fixed;
    else if (s->filter_perfect)
        filter_coeff = ff_dca_fir_32bands_perfect_fixed;
    else
        filter_coeff = ff_dca_fir_32bands_nonperfect_fixed;

    // Filter primary channels
    for (ch = 0; ch < s->nchannels; ch++) {
        // Map this primary channel to speaker
        spkr = map_prm_ch_to_spkr(s, ch);
        if (spkr < 0)
            return AVERROR(EINVAL);

        // Filter bank reconstruction
        s->dcadsp->sub_qmf_fixed[x96_synth](
            &s->synth,
            &s->dcadct,
            s->output_samples[spkr],
            s->subband_samples[ch],
            ch < x96_nchannels ? s->x96_subband_samples[ch] : NULL,
            s->dcadsp_data[ch].u.fix.hist1,
            &s->dcadsp_data[ch].offset,
            s->dcadsp_data[ch].u.fix.hist2,
            filter_coeff,
            s->npcmblocks);
    }

    // Filter LFE channel
    if (s->lfe_present) {
        int32_t *samples = s->output_samples[DCA_SPEAKER_LFE1];
        int nlfesamples = s->npcmblocks >> 1;

        // Check LFF
        if (s->lfe_present == DCA_LFE_FLAG_128) {
            av_log(s->avctx, AV_LOG_ERROR, "Fixed point mode doesn't support LFF=1\n");
            return AVERROR(EINVAL);
        }

        // Offset intermediate buffer for X96
        if (x96_synth)
            samples += nsamples / 2;

        // Interpolate LFE channel
        s->dcadsp->lfe_fir_fixed(samples, s->lfe_samples + DCA_LFE_HISTORY,
                                 ff_dca_lfe_fir_64_fixed, s->npcmblocks);

        if (x96_synth) {
            // Filter 96 kHz oversampled LFE PCM to attenuate high frequency
            // (47.6 - 48.0 kHz) components of interpolation image
            s->dcadsp->lfe_x96_fixed(s->output_samples[DCA_SPEAKER_LFE1],
                                     samples, &s->output_history_lfe_fixed,
                                     nsamples / 2);

        }

        // Update LFE history
        for (n = DCA_LFE_HISTORY - 1; n >= 0; n--)
            s->lfe_samples[n] = s->lfe_samples[nlfesamples + n];
    }

    return 0;
}

static int filter_frame_fixed(DCACoreDecoder *s, AVFrame *frame)
{
    AVCodecContext *avctx = s->avctx;
    DCAContext *dca = avctx->priv_data;
    int i, n, ch, ret, spkr, nsamples;

    // Don't filter twice when falling back from XLL
    if (!(dca->packet & DCA_PACKET_XLL) && (ret = ff_dca_core_filter_fixed(s, 0)) < 0)
        return ret;

    avctx->sample_rate = s->output_rate;
    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
    avctx->bits_per_raw_sample = 24;

    frame->nb_samples = nsamples = s->npcmsamples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    // Undo embedded XCH downmix
    if (s->es_format && (s->ext_audio_mask & DCA_CSS_XCH)
        && s->audio_mode >= DCA_AMODE_2F2R) {
        s->dcadsp->dmix_sub_xch(s->output_samples[DCA_SPEAKER_Ls],
                                s->output_samples[DCA_SPEAKER_Rs],
                                s->output_samples[DCA_SPEAKER_Cs],
                                nsamples);

    }

    // Undo embedded XXCH downmix
    if ((s->ext_audio_mask & (DCA_CSS_XXCH | DCA_EXSS_XXCH))
        && s->xxch_dmix_embedded) {
        int scale_inv   = s->xxch_dmix_scale_inv;
        int *coeff_ptr  = s->xxch_dmix_coeff;
        int xch_base    = ff_dca_channels[s->audio_mode];
        av_assert1(s->nchannels - xch_base <= DCA_XXCH_CHANNELS_MAX);

        // Undo embedded core downmix pre-scaling
        for (spkr = 0; spkr < s->xxch_mask_nbits; spkr++) {
            if (s->xxch_core_mask & (1U << spkr)) {
                s->dcadsp->dmix_scale_inv(s->output_samples[spkr],
                                          scale_inv, nsamples);
            }
        }

        // Undo downmix
        for (ch = xch_base; ch < s->nchannels; ch++) {
            int src_spkr = map_prm_ch_to_spkr(s, ch);
            if (src_spkr < 0)
                return AVERROR(EINVAL);
            for (spkr = 0; spkr < s->xxch_mask_nbits; spkr++) {
                if (s->xxch_dmix_mask[ch - xch_base] & (1U << spkr)) {
                    int coeff = mul16(*coeff_ptr++, scale_inv);
                    if (coeff) {
                        s->dcadsp->dmix_sub(s->output_samples[spkr    ],
                                            s->output_samples[src_spkr],
                                            coeff, nsamples);
                    }
                }
            }
        }
    }

    if (!(s->ext_audio_mask & (DCA_CSS_XXCH | DCA_CSS_XCH | DCA_EXSS_XXCH))) {
        // Front sum/difference decoding
        if ((s->sumdiff_front && s->audio_mode > DCA_AMODE_MONO)
            || s->audio_mode == DCA_AMODE_STEREO_SUMDIFF) {
            s->fixed_dsp->butterflies_fixed(s->output_samples[DCA_SPEAKER_L],
                                            s->output_samples[DCA_SPEAKER_R],
                                            nsamples);
        }

        // Surround sum/difference decoding
        if (s->sumdiff_surround && s->audio_mode >= DCA_AMODE_2F2R) {
            s->fixed_dsp->butterflies_fixed(s->output_samples[DCA_SPEAKER_Ls],
                                            s->output_samples[DCA_SPEAKER_Rs],
                                            nsamples);
        }
    }

    // Downmix primary channel set to stereo
    if (s->request_mask != s->ch_mask) {
        ff_dca_downmix_to_stereo_fixed(s->dcadsp,
                                       s->output_samples,
                                       s->prim_dmix_coeff,
                                       nsamples, s->ch_mask);
    }

    for (i = 0; i < avctx->ch_layout.nb_channels; i++) {
        int32_t *samples = s->output_samples[s->ch_remap[i]];
        int32_t *plane = (int32_t *)frame->extended_data[i];
        for (n = 0; n < nsamples; n++)
            plane[n] = clip23(samples[n]) * (1 << 8);
    }

    return 0;
}

static int filter_frame_float(DCACoreDecoder *s, AVFrame *frame)
{
    AVCodecContext *avctx = s->avctx;
    int x96_nchannels = 0, x96_synth = 0;
    int i, n, ch, ret, spkr, nsamples, nchannels;
    float *output_samples[DCA_SPEAKER_COUNT] = { NULL }, *ptr;
    const float *filter_coeff;

    if (s->ext_audio_mask & (DCA_CSS_X96 | DCA_EXSS_X96)) {
        x96_nchannels = s->x96_nchannels;
        x96_synth = 1;
    }

    avctx->sample_rate = s->sample_rate << x96_synth;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avctx->bits_per_raw_sample = 0;

    frame->nb_samples = nsamples = (s->npcmblocks * DCA_PCMBLOCK_SAMPLES) << x96_synth;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    // Build reverse speaker to channel mapping
    for (i = 0; i < avctx->ch_layout.nb_channels; i++)
        output_samples[s->ch_remap[i]] = (float *)frame->extended_data[i];

    // Allocate space for extra channels
    nchannels = av_popcount(s->ch_mask) - avctx->ch_layout.nb_channels;
    if (nchannels > 0) {
        av_fast_malloc(&s->output_buffer, &s->output_size,
                       nsamples * nchannels * sizeof(float));
        if (!s->output_buffer)
            return AVERROR(ENOMEM);

        ptr = (float *)s->output_buffer;
        for (spkr = 0; spkr < DCA_SPEAKER_COUNT; spkr++) {
            if (!(s->ch_mask & (1U << spkr)))
                continue;
            if (output_samples[spkr])
                continue;
            output_samples[spkr] = ptr;
            ptr += nsamples;
        }
    }

    // Handle change of filtering mode
    set_filter_mode(s, x96_synth);

    // Select filter
    if (x96_synth)
        filter_coeff = ff_dca_fir_64bands;
    else if (s->filter_perfect)
        filter_coeff = ff_dca_fir_32bands_perfect;
    else
        filter_coeff = ff_dca_fir_32bands_nonperfect;

    // Filter primary channels
    for (ch = 0; ch < s->nchannels; ch++) {
        // Map this primary channel to speaker
        spkr = map_prm_ch_to_spkr(s, ch);
        if (spkr < 0)
            return AVERROR(EINVAL);

        // Filter bank reconstruction
        s->dcadsp->sub_qmf_float[x96_synth](
            &s->synth,
            &s->imdct[x96_synth],
            output_samples[spkr],
            s->subband_samples[ch],
            ch < x96_nchannels ? s->x96_subband_samples[ch] : NULL,
            s->dcadsp_data[ch].u.flt.hist1,
            &s->dcadsp_data[ch].offset,
            s->dcadsp_data[ch].u.flt.hist2,
            filter_coeff,
            s->npcmblocks,
            1.0f / (1 << (17 - x96_synth)));
    }

    // Filter LFE channel
    if (s->lfe_present) {
        int dec_select = (s->lfe_present == DCA_LFE_FLAG_128);
        float *samples = output_samples[DCA_SPEAKER_LFE1];
        int nlfesamples = s->npcmblocks >> (dec_select + 1);

        // Offset intermediate buffer for X96
        if (x96_synth)
            samples += nsamples / 2;

        // Select filter
        if (dec_select)
            filter_coeff = ff_dca_lfe_fir_128;
        else
            filter_coeff = ff_dca_lfe_fir_64;

        // Interpolate LFE channel
        s->dcadsp->lfe_fir_float[dec_select](
            samples, s->lfe_samples + DCA_LFE_HISTORY,
            filter_coeff, s->npcmblocks);

        if (x96_synth) {
            // Filter 96 kHz oversampled LFE PCM to attenuate high frequency
            // (47.6 - 48.0 kHz) components of interpolation image
            s->dcadsp->lfe_x96_float(output_samples[DCA_SPEAKER_LFE1],
                                     samples, &s->output_history_lfe_float,
                                     nsamples / 2);
        }

        // Update LFE history
        for (n = DCA_LFE_HISTORY - 1; n >= 0; n--)
            s->lfe_samples[n] = s->lfe_samples[nlfesamples + n];
    }

    // Undo embedded XCH downmix
    if (s->es_format && (s->ext_audio_mask & DCA_CSS_XCH)
        && s->audio_mode >= DCA_AMODE_2F2R) {
        s->float_dsp->vector_fmac_scalar(output_samples[DCA_SPEAKER_Ls],
                                         output_samples[DCA_SPEAKER_Cs],
                                         -M_SQRT1_2, nsamples);
        s->float_dsp->vector_fmac_scalar(output_samples[DCA_SPEAKER_Rs],
                                         output_samples[DCA_SPEAKER_Cs],
                                         -M_SQRT1_2, nsamples);
    }

    // Undo embedded XXCH downmix
    if ((s->ext_audio_mask & (DCA_CSS_XXCH | DCA_EXSS_XXCH))
        && s->xxch_dmix_embedded) {
        float scale_inv = s->xxch_dmix_scale_inv * (1.0f / (1 << 16));
        int *coeff_ptr  = s->xxch_dmix_coeff;
        int xch_base    = ff_dca_channels[s->audio_mode];
        av_assert1(s->nchannels - xch_base <= DCA_XXCH_CHANNELS_MAX);

        // Undo downmix
        for (ch = xch_base; ch < s->nchannels; ch++) {
            int src_spkr = map_prm_ch_to_spkr(s, ch);
            if (src_spkr < 0)
                return AVERROR(EINVAL);
            for (spkr = 0; spkr < s->xxch_mask_nbits; spkr++) {
                if (s->xxch_dmix_mask[ch - xch_base] & (1U << spkr)) {
                    int coeff = *coeff_ptr++;
                    if (coeff) {
                        s->float_dsp->vector_fmac_scalar(output_samples[    spkr],
                                                         output_samples[src_spkr],
                                                         coeff * (-1.0f / (1 << 15)),
                                                         nsamples);
                    }
                }
            }
        }

        // Undo embedded core downmix pre-scaling
        for (spkr = 0; spkr < s->xxch_mask_nbits; spkr++) {
            if (s->xxch_core_mask & (1U << spkr)) {
                s->float_dsp->vector_fmul_scalar(output_samples[spkr],
                                                 output_samples[spkr],
                                                 scale_inv, nsamples);
            }
        }
    }

    if (!(s->ext_audio_mask & (DCA_CSS_XXCH | DCA_CSS_XCH | DCA_EXSS_XXCH))) {
        // Front sum/difference decoding
        if ((s->sumdiff_front && s->audio_mode > DCA_AMODE_MONO)
            || s->audio_mode == DCA_AMODE_STEREO_SUMDIFF) {
            s->float_dsp->butterflies_float(output_samples[DCA_SPEAKER_L],
                                            output_samples[DCA_SPEAKER_R],
                                            nsamples);
        }

        // Surround sum/difference decoding
        if (s->sumdiff_surround && s->audio_mode >= DCA_AMODE_2F2R) {
            s->float_dsp->butterflies_float(output_samples[DCA_SPEAKER_Ls],
                                            output_samples[DCA_SPEAKER_Rs],
                                            nsamples);
        }
    }

    // Downmix primary channel set to stereo
    if (s->request_mask != s->ch_mask) {
        ff_dca_downmix_to_stereo_float(s->float_dsp, output_samples,
                                       s->prim_dmix_coeff,
                                       nsamples, s->ch_mask);
    }

    return 0;
}

int ff_dca_core_filter_frame(DCACoreDecoder *s, AVFrame *frame)
{
    AVCodecContext *avctx = s->avctx;
    DCAContext *dca = avctx->priv_data;
    DCAExssAsset *asset = &dca->exss.assets[0];
    enum AVMatrixEncoding matrix_encoding;
    int ret;

    // Handle downmixing to stereo request
    if (dca->request_channel_layout == DCA_SPEAKER_LAYOUT_STEREO
        && s->audio_mode > DCA_AMODE_MONO && s->prim_dmix_embedded
        && (s->prim_dmix_type == DCA_DMIX_TYPE_LoRo ||
            s->prim_dmix_type == DCA_DMIX_TYPE_LtRt))
        s->request_mask = DCA_SPEAKER_LAYOUT_STEREO;
    else
        s->request_mask = s->ch_mask;
    if (!ff_dca_set_channel_layout(avctx, s->ch_remap, s->request_mask))
        return AVERROR(EINVAL);

    // Force fixed point mode when falling back from XLL
    if ((avctx->flags & AV_CODEC_FLAG_BITEXACT) || ((dca->packet & DCA_PACKET_EXSS)
                                                    && (asset->extension_mask & DCA_EXSS_XLL)))
        ret = filter_frame_fixed(s, frame);
    else
        ret = filter_frame_float(s, frame);
    if (ret < 0)
        return ret;

    // Set profile, bit rate, etc
    if (s->ext_audio_mask & DCA_EXSS_MASK)
        avctx->profile = FF_PROFILE_DTS_HD_HRA;
    else if (s->ext_audio_mask & (DCA_CSS_XXCH | DCA_CSS_XCH))
        avctx->profile = FF_PROFILE_DTS_ES;
    else if (s->ext_audio_mask & DCA_CSS_X96)
        avctx->profile = FF_PROFILE_DTS_96_24;
    else
        avctx->profile = FF_PROFILE_DTS;

    if (s->bit_rate > 3 && !(s->ext_audio_mask & DCA_EXSS_MASK))
        avctx->bit_rate = s->bit_rate;
    else
        avctx->bit_rate = 0;

    if (s->audio_mode == DCA_AMODE_STEREO_TOTAL || (s->request_mask != s->ch_mask &&
                                                    s->prim_dmix_type == DCA_DMIX_TYPE_LtRt))
        matrix_encoding = AV_MATRIX_ENCODING_DOLBY;
    else
        matrix_encoding = AV_MATRIX_ENCODING_NONE;
    if ((ret = ff_side_data_update_matrix_encoding(frame, matrix_encoding)) < 0)
        return ret;

    return 0;
}

av_cold void ff_dca_core_flush(DCACoreDecoder *s)
{
    if (s->subband_buffer) {
        erase_adpcm_history(s);
        memset(s->lfe_samples, 0, DCA_LFE_HISTORY * sizeof(int32_t));
    }

    if (s->x96_subband_buffer)
        erase_x96_adpcm_history(s);

    erase_dsp_history(s);
}

av_cold int ff_dca_core_init(DCACoreDecoder *s)
{
    if (!(s->float_dsp = avpriv_float_dsp_alloc(0)))
        return -1;
    if (!(s->fixed_dsp = avpriv_alloc_fixed_dsp(0)))
        return -1;

    ff_dcadct_init(&s->dcadct);
    if (ff_mdct_init(&s->imdct[0], 6, 1, 1.0) < 0)
        return -1;
    if (ff_mdct_init(&s->imdct[1], 7, 1, 1.0) < 0)
        return -1;
    ff_synth_filter_init(&s->synth);

    s->x96_rand = 1;
    return 0;
}

av_cold void ff_dca_core_close(DCACoreDecoder *s)
{
    av_freep(&s->float_dsp);
    av_freep(&s->fixed_dsp);

    ff_mdct_end(&s->imdct[0]);
    ff_mdct_end(&s->imdct[1]);

    av_freep(&s->subband_buffer);
    s->subband_size = 0;

    av_freep(&s->x96_subband_buffer);
    s->x96_subband_size = 0;

    av_freep(&s->output_buffer);
    s->output_size = 0;
}
