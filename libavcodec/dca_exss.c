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

#include "dcadec.h"
#include "dcadata.h"

static int count_chs_for_mask(int mask)
{
    return av_popcount(mask) + av_popcount(mask & 0xae66);
}

static void parse_xll_parameters(DCAExssParser *s, DCAExssAsset *asset)
{
    // Size of XLL data in extension substream
    asset->xll_size = get_bits(&s->gb, s->exss_size_nbits) + 1;

    // XLL sync word present flag
    if (asset->xll_sync_present = get_bits1(&s->gb)) {
        int xll_delay_nbits;

        // Peak bit rate smoothing buffer size
        skip_bits(&s->gb, 4);

        // Number of bits for XLL decoding delay
        xll_delay_nbits = get_bits(&s->gb, 5) + 1;

        // Initial XLL decoding delay in frames
        asset->xll_delay_nframes = get_bits_long(&s->gb, xll_delay_nbits);

        // Number of bytes offset to XLL sync
        asset->xll_sync_offset = get_bits(&s->gb, s->exss_size_nbits);
    } else {
        asset->xll_delay_nframes = 0;
        asset->xll_sync_offset = 0;
    }
}

static void parse_lbr_parameters(DCAExssParser *s, DCAExssAsset *asset)
{
    // Size of LBR component in extension substream
    asset->lbr_size = get_bits(&s->gb, 14) + 1;

    // LBR sync word present flag
    if (get_bits1(&s->gb))
        // LBR sync distance
        skip_bits(&s->gb, 2);
}

static int parse_descriptor(DCAExssParser *s, DCAExssAsset *asset)
{
    int i, j, drc_present, descr_size, descr_pos = get_bits_count(&s->gb);

    // Size of audio asset descriptor in bytes
    descr_size = get_bits(&s->gb, 9) + 1;

    // Audio asset identifier
    asset->asset_index = get_bits(&s->gb, 3);

    //
    // Per stream static metadata
    //

    if (s->static_fields_present) {
        // Asset type descriptor presence
        if (get_bits1(&s->gb))
            // Asset type descriptor
            skip_bits(&s->gb, 4);

        // Language descriptor presence
        if (get_bits1(&s->gb))
            // Language descriptor
            skip_bits(&s->gb, 24);

        // Additional textual information presence
        if (get_bits1(&s->gb)) {
            // Byte size of additional text info
            int text_size = get_bits(&s->gb, 10) + 1;

            // Sanity check available size
            if (get_bits_left(&s->gb) < text_size * 8)
                return AVERROR_INVALIDDATA;

            // Additional textual information string
            skip_bits_long(&s->gb, text_size * 8);
        }

        // PCM bit resolution
        asset->pcm_bit_res = get_bits(&s->gb, 5) + 1;

        // Maximum sample rate
        asset->max_sample_rate = ff_dca_sampling_freqs[get_bits(&s->gb, 4)];

        // Total number of channels
        asset->nchannels_total = get_bits(&s->gb, 8) + 1;

        // One to one map channel to speakers
        if (asset->one_to_one_map_ch_to_spkr = get_bits1(&s->gb)) {
            int spkr_mask_nbits = 0;
            int spkr_remap_nsets;
            int nspeakers[8];

            // Embedded stereo flag
            if (asset->nchannels_total > 2)
                asset->embedded_stereo = get_bits1(&s->gb);

            // Embedded 6 channels flag
            if (asset->nchannels_total > 6)
                asset->embedded_6ch = get_bits1(&s->gb);

            // Speaker mask enabled flag
            if (asset->spkr_mask_enabled = get_bits1(&s->gb)) {
                // Number of bits for speaker activity mask
                spkr_mask_nbits = (get_bits(&s->gb, 2) + 1) << 2;

                // Loudspeaker activity mask
                asset->spkr_mask = get_bits(&s->gb, spkr_mask_nbits);
            }

            // Number of speaker remapping sets
            if ((spkr_remap_nsets = get_bits(&s->gb, 3)) && !spkr_mask_nbits) {
                av_log(s->avctx, AV_LOG_ERROR, "Speaker mask disabled yet there are remapping sets\n");
                return AVERROR_INVALIDDATA;
            }

            // Standard loudspeaker layout mask
            for (i = 0; i < spkr_remap_nsets; i++)
                nspeakers[i] = count_chs_for_mask(get_bits(&s->gb, spkr_mask_nbits));

            for (i = 0; i < spkr_remap_nsets; i++) {
                // Number of channels to be decoded for speaker remapping
                int nch_for_remaps = get_bits(&s->gb, 5) + 1;

                for (j = 0; j < nspeakers[i]; j++) {
                    // Decoded channels to output speaker mapping mask
                    int remap_ch_mask = get_bits_long(&s->gb, nch_for_remaps);

                    // Loudspeaker remapping codes
                    skip_bits_long(&s->gb, av_popcount(remap_ch_mask) * 5);
                }
            }
        } else {
            asset->embedded_stereo = 0;
            asset->embedded_6ch = 0;
            asset->spkr_mask_enabled = 0;
            asset->spkr_mask = 0;

            // Representation type
            asset->representation_type = get_bits(&s->gb, 3);
        }
    }

    //
    // DRC, DNC and mixing metadata
    //

    // Dynamic range coefficient presence flag
    drc_present = get_bits1(&s->gb);

    // Code for dynamic range coefficient
    if (drc_present)
        skip_bits(&s->gb, 8);

    // Dialog normalization presence flag
    if (get_bits1(&s->gb))
        // Dialog normalization code
        skip_bits(&s->gb, 5);

    // DRC for stereo downmix
    if (drc_present && asset->embedded_stereo)
        skip_bits(&s->gb, 8);

    // Mixing metadata presence flag
    if (s->mix_metadata_enabled && get_bits1(&s->gb)) {
        int nchannels_dmix;

        // External mixing flag
        skip_bits1(&s->gb);

        // Post mixing / replacement gain adjustment
        skip_bits(&s->gb, 6);

        // DRC prior to mixing
        if (get_bits(&s->gb, 2) == 3)
            // Custom code for mixing DRC
            skip_bits(&s->gb, 8);
        else
            // Limit for mixing DRC
            skip_bits(&s->gb, 3);

        // Scaling type for channels of main audio
        // Scaling parameters of main audio
        if (get_bits1(&s->gb))
            for (i = 0; i < s->nmixoutconfigs; i++)
                skip_bits_long(&s->gb, 6 * s->nmixoutchs[i]);
        else
            skip_bits_long(&s->gb, 6 * s->nmixoutconfigs);

        nchannels_dmix = asset->nchannels_total;
        if (asset->embedded_6ch)
            nchannels_dmix += 6;
        if (asset->embedded_stereo)
            nchannels_dmix += 2;

        for (i = 0; i < s->nmixoutconfigs; i++) {
            if (!s->nmixoutchs[i]) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid speaker layout mask for mixing configuration\n");
                return AVERROR_INVALIDDATA;
            }
            for (j = 0; j < nchannels_dmix; j++) {
                // Mix output mask
                int mix_map_mask = get_bits(&s->gb, s->nmixoutchs[i]);

                // Mixing coefficients
                skip_bits_long(&s->gb, av_popcount(mix_map_mask) * 6);
            }
        }
    }

    //
    // Decoder navigation data
    //

    // Coding mode for the asset
    asset->coding_mode = get_bits(&s->gb, 2);

    // Coding components used in asset
    switch (asset->coding_mode) {
    case 0: // Coding mode that may contain multiple coding components
        asset->extension_mask = get_bits(&s->gb, 12);

        if (asset->extension_mask & DCA_EXSS_CORE) {
            // Size of core component in extension substream
            asset->core_size = get_bits(&s->gb, 14) + 1;
            // Core sync word present flag
            if (get_bits1(&s->gb))
                // Core sync distance
                skip_bits(&s->gb, 2);
        }

        if (asset->extension_mask & DCA_EXSS_XBR)
            // Size of XBR extension in extension substream
            asset->xbr_size = get_bits(&s->gb, 14) + 1;

        if (asset->extension_mask & DCA_EXSS_XXCH)
            // Size of XXCH extension in extension substream
            asset->xxch_size = get_bits(&s->gb, 14) + 1;

        if (asset->extension_mask & DCA_EXSS_X96)
            // Size of X96 extension in extension substream
            asset->x96_size = get_bits(&s->gb, 12) + 1;

        if (asset->extension_mask & DCA_EXSS_LBR)
            parse_lbr_parameters(s, asset);

        if (asset->extension_mask & DCA_EXSS_XLL)
            parse_xll_parameters(s, asset);

        if (asset->extension_mask & DCA_EXSS_RSV1)
            skip_bits(&s->gb, 16);

        if (asset->extension_mask & DCA_EXSS_RSV2)
            skip_bits(&s->gb, 16);
        break;

    case 1: // Loss-less coding mode without CBR component
        asset->extension_mask = DCA_EXSS_XLL;
        parse_xll_parameters(s, asset);
        break;

    case 2: // Low bit rate mode
        asset->extension_mask = DCA_EXSS_LBR;
        parse_lbr_parameters(s, asset);
        break;

    case 3: // Auxiliary coding mode
        asset->extension_mask = 0;

        // Size of auxiliary coded data
        skip_bits(&s->gb, 14);

        // Auxiliary codec identification
        skip_bits(&s->gb, 8);

        // Aux sync word present flag
        if (get_bits1(&s->gb))
            // Aux sync distance
            skip_bits(&s->gb, 3);
        break;
    }

    if (asset->extension_mask & DCA_EXSS_XLL)
        // DTS-HD stream ID
        asset->hd_stream_id = get_bits(&s->gb, 3);

    // One to one mixing flag
    // Per channel main audio scaling flag
    // Main audio scaling codes
    // Decode asset in secondary decoder flag
    // Revision 2 DRC metadata
    // Reserved
    // Zero pad
    if (ff_dca_seek_bits(&s->gb, descr_pos + descr_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of EXSS asset descriptor\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int set_exss_offsets(DCAExssAsset *asset)
{
    int offs = asset->asset_offset;
    int size = asset->asset_size;

    if (asset->extension_mask & DCA_EXSS_CORE) {
        asset->core_offset = offs;
        if (asset->core_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->core_size;
        size -= asset->core_size;
    }

    if (asset->extension_mask & DCA_EXSS_XBR) {
        asset->xbr_offset = offs;
        if (asset->xbr_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->xbr_size;
        size -= asset->xbr_size;
    }

    if (asset->extension_mask & DCA_EXSS_XXCH) {
        asset->xxch_offset = offs;
        if (asset->xxch_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->xxch_size;
        size -= asset->xxch_size;
    }

    if (asset->extension_mask & DCA_EXSS_X96) {
        asset->x96_offset = offs;
        if (asset->x96_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->x96_size;
        size -= asset->x96_size;
    }

    if (asset->extension_mask & DCA_EXSS_LBR) {
        asset->lbr_offset = offs;
        if (asset->lbr_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->lbr_size;
        size -= asset->lbr_size;
    }

    if (asset->extension_mask & DCA_EXSS_XLL) {
        asset->xll_offset = offs;
        if (asset->xll_size > size)
            return AVERROR_INVALIDDATA;
        offs += asset->xll_size;
        size -= asset->xll_size;
    }

    return 0;
}

int ff_dca_exss_parse(DCAExssParser *s, uint8_t *data, int size)
{
    int i, ret, offset, wide_hdr, header_size;

    if ((ret = init_get_bits8(&s->gb, data, size)) < 0)
        return ret;

    // Extension substream sync word
    skip_bits_long(&s->gb, 32);

    // User defined bits
    skip_bits(&s->gb, 8);

    // Extension substream index
    s->exss_index = get_bits(&s->gb, 2);

    // Flag indicating short or long header size
    wide_hdr = get_bits1(&s->gb);

    // Extension substream header length
    header_size = get_bits(&s->gb, 8 + 4 * wide_hdr) + 1;

    // Check CRC
    if ((s->avctx->err_recognition & (AV_EF_CRCCHECK | AV_EF_CAREFUL))
        && ff_dca_check_crc(&s->gb, 32 + 8, header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid EXSS header checksum\n");
        return AVERROR_INVALIDDATA;
    }

    s->exss_size_nbits = 16 + 4 * wide_hdr;

    // Number of bytes of extension substream
    s->exss_size = get_bits(&s->gb, s->exss_size_nbits) + 1;
    if (s->exss_size > size) {
        av_log(s->avctx, AV_LOG_ERROR, "Packet too short for EXSS frame\n");
        return AVERROR_INVALIDDATA;
    }

    // Per stream static fields presence flag
    if (s->static_fields_present = get_bits1(&s->gb)) {
        int active_exss_mask[8];

        // Reference clock code
        skip_bits(&s->gb, 2);

        // Extension substream frame duration
        skip_bits(&s->gb, 3);

        // Timecode presence flag
        if (get_bits1(&s->gb))
            // Timecode data
            skip_bits_long(&s->gb, 36);

        // Number of defined audio presentations
        s->npresents = get_bits(&s->gb, 3) + 1;
        if (s->npresents > 1) {
            avpriv_request_sample(s->avctx, "%d audio presentations", s->npresents);
            return AVERROR_PATCHWELCOME;
        }

        // Number of audio assets in extension substream
        s->nassets = get_bits(&s->gb, 3) + 1;
        if (s->nassets > 1) {
            avpriv_request_sample(s->avctx, "%d audio assets", s->nassets);
            return AVERROR_PATCHWELCOME;
        }

        // Active extension substream mask for audio presentation
        for (i = 0; i < s->npresents; i++)
            active_exss_mask[i] = get_bits(&s->gb, s->exss_index + 1);

        // Active audio asset mask
        for (i = 0; i < s->npresents; i++)
            skip_bits_long(&s->gb, av_popcount(active_exss_mask[i]) * 8);

        // Mixing metadata enable flag
        if (s->mix_metadata_enabled = get_bits1(&s->gb)) {
            int spkr_mask_nbits;

            // Mixing metadata adjustment level
            skip_bits(&s->gb, 2);

            // Number of bits for mixer output speaker activity mask
            spkr_mask_nbits = (get_bits(&s->gb, 2) + 1) << 2;

            // Number of mixing configurations
            s->nmixoutconfigs = get_bits(&s->gb, 2) + 1;

            // Speaker layout mask for mixer output channels
            for (i = 0; i < s->nmixoutconfigs; i++)
                s->nmixoutchs[i] = count_chs_for_mask(get_bits(&s->gb, spkr_mask_nbits));
        }
    } else {
        s->npresents = 1;
        s->nassets = 1;
    }

    // Size of encoded asset data in bytes
    offset = header_size;
    for (i = 0; i < s->nassets; i++) {
        s->assets[i].asset_offset = offset;
        s->assets[i].asset_size = get_bits(&s->gb, s->exss_size_nbits) + 1;
        offset += s->assets[i].asset_size;
        if (offset > s->exss_size) {
            av_log(s->avctx, AV_LOG_ERROR, "EXSS asset out of bounds\n");
            return AVERROR_INVALIDDATA;
        }
    }

    // Audio asset descriptor
    for (i = 0; i < s->nassets; i++) {
        if ((ret = parse_descriptor(s, &s->assets[i])) < 0)
            return ret;
        if ((ret = set_exss_offsets(&s->assets[i])) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid extension size in EXSS asset descriptor\n");
            return ret;
        }
    }

    // Backward compatible core present
    // Backward compatible core substream index
    // Backward compatible core asset index
    // Reserved
    // Byte align
    // CRC16 of extension substream header
    if (ff_dca_seek_bits(&s->gb, header_size * 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of EXSS header\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}
