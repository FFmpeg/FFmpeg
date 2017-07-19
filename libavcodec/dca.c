/*
 * DCA compatible decoder data
 * Copyright (C) 2004 Gildas Bazin
 * Copyright (C) 2004 Benjamin Zores
 * Copyright (C) 2006 Benjamin Larsson
 * Copyright (C) 2007 Konstantin Shishkov
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

#include <stdint.h>
#include <string.h>

#include "libavutil/error.h"

#include "dca.h"
#include "dca_core.h"
#include "dca_syncwords.h"
#include "get_bits.h"
#include "put_bits.h"

const uint32_t avpriv_dca_sample_rates[16] = {
    0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0,
    12000, 24000, 48000, 96000, 192000
};

const uint32_t ff_dca_sampling_freqs[16] = {
      8000,  16000, 32000, 64000, 128000, 22050,  44100,  88200,
    176400, 352800, 12000, 24000,  48000, 96000, 192000, 384000,
};

const uint8_t ff_dca_freq_ranges[16] = {
    0, 1, 2, 3, 4, 1, 2, 3, 4, 4, 0, 1, 2, 3, 4, 4
};

const uint8_t ff_dca_bits_per_sample[8] = {
    16, 16, 20, 20, 0, 24, 24, 0
};

int avpriv_dca_convert_bitstream(const uint8_t *src, int src_size, uint8_t *dst,
                             int max_size)
{
    uint32_t mrk;
    int i, tmp;
    PutBitContext pb;

    if ((unsigned) src_size > (unsigned) max_size)
        src_size = max_size;

    mrk = AV_RB32(src);
    switch (mrk) {
    case DCA_SYNCWORD_CORE_BE:
    case DCA_SYNCWORD_SUBSTREAM:
        memcpy(dst, src, src_size);
        return src_size;
    case DCA_SYNCWORD_CORE_LE:
        for (i = 0; i < (src_size + 1) >> 1; i++) {
            AV_WB16(dst, AV_RL16(src));
            src += 2;
            dst += 2;
        }
        return src_size;
    case DCA_SYNCWORD_CORE_14B_BE:
    case DCA_SYNCWORD_CORE_14B_LE:
        init_put_bits(&pb, dst, max_size);
        for (i = 0; i < (src_size + 1) >> 1; i++, src += 2) {
            tmp = ((mrk == DCA_SYNCWORD_CORE_14B_BE) ? AV_RB16(src) : AV_RL16(src)) & 0x3FFF;
            put_bits(&pb, 14, tmp);
        }
        flush_put_bits(&pb);
        return (put_bits_count(&pb) + 7) >> 3;
    default:
        return AVERROR_INVALIDDATA;
    }
}

int ff_dca_parse_core_frame_header(DCACoreFrameHeader *h, GetBitContext *gb)
{
    if (get_bits_long(gb, 32) != DCA_SYNCWORD_CORE_BE)
        return DCA_PARSE_ERROR_SYNC_WORD;

    h->normal_frame = get_bits1(gb);
    h->deficit_samples = get_bits(gb, 5) + 1;
    if (h->deficit_samples != DCA_PCMBLOCK_SAMPLES)
        return DCA_PARSE_ERROR_DEFICIT_SAMPLES;

    h->crc_present = get_bits1(gb);
    h->npcmblocks = get_bits(gb, 7) + 1;
    if (h->npcmblocks & (DCA_SUBBAND_SAMPLES - 1))
        return DCA_PARSE_ERROR_PCM_BLOCKS;

    h->frame_size = get_bits(gb, 14) + 1;
    if (h->frame_size < 96)
        return DCA_PARSE_ERROR_FRAME_SIZE;

    h->audio_mode = get_bits(gb, 6);
    if (h->audio_mode >= DCA_AMODE_COUNT)
        return DCA_PARSE_ERROR_AMODE;

    h->sr_code = get_bits(gb, 4);
    if (!avpriv_dca_sample_rates[h->sr_code])
        return DCA_PARSE_ERROR_SAMPLE_RATE;

    h->br_code = get_bits(gb, 5);
    if (get_bits1(gb))
        return DCA_PARSE_ERROR_RESERVED_BIT;

    h->drc_present = get_bits1(gb);
    h->ts_present = get_bits1(gb);
    h->aux_present = get_bits1(gb);
    h->hdcd_master = get_bits1(gb);
    h->ext_audio_type = get_bits(gb, 3);
    h->ext_audio_present = get_bits1(gb);
    h->sync_ssf = get_bits1(gb);
    h->lfe_present = get_bits(gb, 2);
    if (h->lfe_present == DCA_LFE_FLAG_INVALID)
        return DCA_PARSE_ERROR_LFE_FLAG;

    h->predictor_history = get_bits1(gb);
    if (h->crc_present)
        skip_bits(gb, 16);
    h->filter_perfect = get_bits1(gb);
    h->encoder_rev = get_bits(gb, 4);
    h->copy_hist = get_bits(gb, 2);
    h->pcmr_code = get_bits(gb, 3);
    if (!ff_dca_bits_per_sample[h->pcmr_code])
        return DCA_PARSE_ERROR_PCM_RES;

    h->sumdiff_front = get_bits1(gb);
    h->sumdiff_surround = get_bits1(gb);
    h->dn_code = get_bits(gb, 4);
    return 0;
}

int avpriv_dca_parse_core_frame_header(DCACoreFrameHeader *h, uint8_t *buf, int size)
{
    GetBitContext gb;

    if (init_get_bits8(&gb, buf, size) < 0)
        return DCA_PARSE_ERROR_INVALIDDATA;

    return ff_dca_parse_core_frame_header(h, &gb);
}
