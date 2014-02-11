/*
 * DCA ExSS extension
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

#include "libavutil/common.h"
#include "libavutil/log.h"

#include "dca.h"
#include "dca_syncwords.h"
#include "get_bits.h"

/* extensions that reside in core substream */
#define DCA_CORE_EXTS (DCA_EXT_XCH | DCA_EXT_XXCH | DCA_EXT_X96)

/* these are unconfirmed but should be mostly correct */
enum DCAExSSSpeakerMask {
    DCA_EXSS_FRONT_CENTER          = 0x0001,
    DCA_EXSS_FRONT_LEFT_RIGHT      = 0x0002,
    DCA_EXSS_SIDE_REAR_LEFT_RIGHT  = 0x0004,
    DCA_EXSS_LFE                   = 0x0008,
    DCA_EXSS_REAR_CENTER           = 0x0010,
    DCA_EXSS_FRONT_HIGH_LEFT_RIGHT = 0x0020,
    DCA_EXSS_REAR_LEFT_RIGHT       = 0x0040,
    DCA_EXSS_FRONT_HIGH_CENTER     = 0x0080,
    DCA_EXSS_OVERHEAD              = 0x0100,
    DCA_EXSS_CENTER_LEFT_RIGHT     = 0x0200,
    DCA_EXSS_WIDE_LEFT_RIGHT       = 0x0400,
    DCA_EXSS_SIDE_LEFT_RIGHT       = 0x0800,
    DCA_EXSS_LFE2                  = 0x1000,
    DCA_EXSS_SIDE_HIGH_LEFT_RIGHT  = 0x2000,
    DCA_EXSS_REAR_HIGH_CENTER      = 0x4000,
    DCA_EXSS_REAR_HIGH_LEFT_RIGHT  = 0x8000,
};

/**
 * Return the number of channels in an ExSS speaker mask (HD)
 */
static int dca_exss_mask2count(int mask)
{
    /* count bits that mean speaker pairs twice */
    return av_popcount(mask) +
           av_popcount(mask & (DCA_EXSS_CENTER_LEFT_RIGHT      |
                               DCA_EXSS_FRONT_LEFT_RIGHT       |
                               DCA_EXSS_FRONT_HIGH_LEFT_RIGHT  |
                               DCA_EXSS_WIDE_LEFT_RIGHT        |
                               DCA_EXSS_SIDE_LEFT_RIGHT        |
                               DCA_EXSS_SIDE_HIGH_LEFT_RIGHT   |
                               DCA_EXSS_SIDE_REAR_LEFT_RIGHT   |
                               DCA_EXSS_REAR_LEFT_RIGHT        |
                               DCA_EXSS_REAR_HIGH_LEFT_RIGHT));
}

/**
 * Skip mixing coefficients of a single mix out configuration (HD)
 */
static void dca_exss_skip_mix_coeffs(GetBitContext *gb, int channels, int out_ch)
{
    int i;

    for (i = 0; i < channels; i++) {
        int mix_map_mask = get_bits(gb, out_ch);
        int num_coeffs = av_popcount(mix_map_mask);
        skip_bits_long(gb, num_coeffs * 6);
    }
}

/**
 * Parse extension substream asset header (HD)
 */
static int dca_exss_parse_asset_header(DCAContext *s)
{
    int header_pos = get_bits_count(&s->gb);
    int header_size;
    int channels = 0;
    int embedded_stereo = 0;
    int embedded_6ch    = 0;
    int drc_code_present;
    int extensions_mask = 0;
    int i, j;

    if (get_bits_left(&s->gb) < 16)
        return AVERROR_INVALIDDATA;

    /* We will parse just enough to get to the extensions bitmask with which
     * we can set the profile value. */

    header_size = get_bits(&s->gb, 9) + 1;
    skip_bits(&s->gb, 3); // asset index

    if (s->static_fields) {
        if (get_bits1(&s->gb))
            skip_bits(&s->gb, 4); // asset type descriptor
        if (get_bits1(&s->gb))
            skip_bits_long(&s->gb, 24); // language descriptor

        if (get_bits1(&s->gb)) {
            /* How can one fit 1024 bytes of text here if the maximum value
             * for the asset header size field above was 512 bytes? */
            int text_length = get_bits(&s->gb, 10) + 1;
            if (get_bits_left(&s->gb) < text_length * 8)
                return AVERROR_INVALIDDATA;
            skip_bits_long(&s->gb, text_length * 8); // info text
        }

        skip_bits(&s->gb, 5); // bit resolution - 1
        skip_bits(&s->gb, 4); // max sample rate code
        channels = get_bits(&s->gb, 8) + 1;

        s->one2one_map_chtospkr = get_bits1(&s->gb);
        if (s->one2one_map_chtospkr) {
            int spkr_remap_sets;
            int spkr_mask_size = 16;
            int num_spkrs[7];

            if (channels > 2)
                embedded_stereo = get_bits1(&s->gb);
            if (channels > 6)
                embedded_6ch = get_bits1(&s->gb);

            if (get_bits1(&s->gb)) {
                spkr_mask_size = (get_bits(&s->gb, 2) + 1) << 2;
                skip_bits(&s->gb, spkr_mask_size); // spkr activity mask
            }

            spkr_remap_sets = get_bits(&s->gb, 3);

            for (i = 0; i < spkr_remap_sets; i++) {
                /* std layout mask for each remap set */
                num_spkrs[i] = dca_exss_mask2count(get_bits(&s->gb, spkr_mask_size));
            }

            for (i = 0; i < spkr_remap_sets; i++) {
                int num_dec_ch_remaps = get_bits(&s->gb, 5) + 1;
                if (get_bits_left(&s->gb) < 0)
                    return AVERROR_INVALIDDATA;

                for (j = 0; j < num_spkrs[i]; j++) {
                    int remap_dec_ch_mask = get_bits_long(&s->gb, num_dec_ch_remaps);
                    int num_dec_ch = av_popcount(remap_dec_ch_mask);
                    skip_bits_long(&s->gb, num_dec_ch * 5); // remap codes
                }
            }
        } else {
            skip_bits(&s->gb, 3); // representation type
        }
    }

    drc_code_present = get_bits1(&s->gb);
    if (drc_code_present)
        get_bits(&s->gb, 8); // drc code

    if (get_bits1(&s->gb))
        skip_bits(&s->gb, 5); // dialog normalization code

    if (drc_code_present && embedded_stereo)
        get_bits(&s->gb, 8); // drc stereo code

    if (s->mix_metadata && get_bits1(&s->gb)) {
        skip_bits(&s->gb, 1); // external mix
        skip_bits(&s->gb, 6); // post mix gain code

        if (get_bits(&s->gb, 2) != 3) // mixer drc code
            skip_bits(&s->gb, 3); // drc limit
        else
            skip_bits(&s->gb, 8); // custom drc code

        if (get_bits1(&s->gb)) // channel specific scaling
            for (i = 0; i < s->num_mix_configs; i++)
                skip_bits_long(&s->gb, s->mix_config_num_ch[i] * 6); // scale codes
        else
            skip_bits_long(&s->gb, s->num_mix_configs * 6); // scale codes

        for (i = 0; i < s->num_mix_configs; i++) {
            if (get_bits_left(&s->gb) < 0)
                return AVERROR_INVALIDDATA;
            dca_exss_skip_mix_coeffs(&s->gb, channels, s->mix_config_num_ch[i]);
            if (embedded_6ch)
                dca_exss_skip_mix_coeffs(&s->gb, 6, s->mix_config_num_ch[i]);
            if (embedded_stereo)
                dca_exss_skip_mix_coeffs(&s->gb, 2, s->mix_config_num_ch[i]);
        }
    }

    switch (get_bits(&s->gb, 2)) {
    case 0:
        extensions_mask = get_bits(&s->gb, 12);
        break;
    case 1:
        extensions_mask = DCA_EXT_EXSS_XLL;
        break;
    case 2:
        extensions_mask = DCA_EXT_EXSS_LBR;
        break;
    case 3:
        extensions_mask = 0; /* aux coding */
        break;
    }

    /* not parsed further, we were only interested in the extensions mask */

    if (get_bits_left(&s->gb) < 0)
        return AVERROR_INVALIDDATA;

    if (get_bits_count(&s->gb) - header_pos > header_size * 8) {
        av_log(s->avctx, AV_LOG_WARNING, "Asset header size mismatch.\n");
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(&s->gb, header_pos + header_size * 8 - get_bits_count(&s->gb));

    if (extensions_mask & DCA_EXT_EXSS_XLL)
        s->profile = FF_PROFILE_DTS_HD_MA;
    else if (extensions_mask & (DCA_EXT_EXSS_XBR | DCA_EXT_EXSS_X96 |
                                DCA_EXT_EXSS_XXCH))
        s->profile = FF_PROFILE_DTS_HD_HRA;

    if (!(extensions_mask & DCA_EXT_CORE))
        av_log(s->avctx, AV_LOG_WARNING, "DTS core detection mismatch.\n");
    if ((extensions_mask & DCA_CORE_EXTS) != s->core_ext_mask)
        av_log(s->avctx, AV_LOG_WARNING,
               "DTS extensions detection mismatch (%d, %d)\n",
               extensions_mask & DCA_CORE_EXTS, s->core_ext_mask);

    return 0;
}

/**
 * Parse extension substream header (HD)
 */
void ff_dca_exss_parse_header(DCAContext *s)
{
    int asset_size[8];
    int ss_index;
    int blownup;
    int num_audiop = 1;
    int num_assets = 1;
    int active_ss_mask[8];
    int i, j;
    int start_pos;
    int hdrsize;
    uint32_t mkr;

    if (get_bits_left(&s->gb) < 52)
        return;

    start_pos = get_bits_count(&s->gb) - 32;

    skip_bits(&s->gb, 8); // user data
    ss_index = get_bits(&s->gb, 2);

    blownup = get_bits1(&s->gb);
    hdrsize = get_bits(&s->gb,  8 + 4 * blownup) + 1; // header_size
    skip_bits(&s->gb, 16 + 4 * blownup); // hd_size

    s->static_fields = get_bits1(&s->gb);
    if (s->static_fields) {
        skip_bits(&s->gb, 2); // reference clock code
        skip_bits(&s->gb, 3); // frame duration code

        if (get_bits1(&s->gb))
            skip_bits_long(&s->gb, 36); // timestamp

        /* a single stream can contain multiple audio assets that can be
         * combined to form multiple audio presentations */

        num_audiop = get_bits(&s->gb, 3) + 1;
        if (num_audiop > 1) {
            avpriv_request_sample(s->avctx,
                                  "Multiple DTS-HD audio presentations");
            /* ignore such streams for now */
            return;
        }

        num_assets = get_bits(&s->gb, 3) + 1;
        if (num_assets > 1) {
            avpriv_request_sample(s->avctx, "Multiple DTS-HD audio assets");
            /* ignore such streams for now */
            return;
        }

        for (i = 0; i < num_audiop; i++)
            active_ss_mask[i] = get_bits(&s->gb, ss_index + 1);

        for (i = 0; i < num_audiop; i++)
            for (j = 0; j <= ss_index; j++)
                if (active_ss_mask[i] & (1 << j))
                    skip_bits(&s->gb, 8); // active asset mask

        s->mix_metadata = get_bits1(&s->gb);
        if (s->mix_metadata) {
            int mix_out_mask_size;

            skip_bits(&s->gb, 2); // adjustment level
            mix_out_mask_size  = (get_bits(&s->gb, 2) + 1) << 2;
            s->num_mix_configs =  get_bits(&s->gb, 2) + 1;

            for (i = 0; i < s->num_mix_configs; i++) {
                int mix_out_mask        = get_bits(&s->gb, mix_out_mask_size);
                s->mix_config_num_ch[i] = dca_exss_mask2count(mix_out_mask);
            }
        }
    }

    for (i = 0; i < num_assets; i++)
        asset_size[i] = get_bits_long(&s->gb, 16 + 4 * blownup) + 1;

    for (i = 0; i < num_assets; i++) {
        if (dca_exss_parse_asset_header(s))
            return;
    }

    if (num_assets > 0) {
        j = get_bits_count(&s->gb);
        if (start_pos + hdrsize * 8 > j)
            skip_bits_long(&s->gb, start_pos + hdrsize * 8 - j);

        for (i = 0; i < num_assets; i++) {
            int end_pos;
            start_pos = get_bits_count(&s->gb);
            end_pos   = start_pos + asset_size[i] * 8;
            mkr       = get_bits_long(&s->gb, 32);

            /* parse extensions that we know about */
            switch (mkr) {
            case DCA_SYNCWORD_XLL:
                if (s->xll_disable) {
                    av_log(s->avctx, AV_LOG_DEBUG,
                           "DTS-XLL: ignoring XLL extension\n");
                    break;
                }
                av_log(s->avctx, AV_LOG_DEBUG,
                       "DTS-XLL: decoding XLL extension\n");
                if (ff_dca_xll_decode_header(s)        == 0 &&
                    ff_dca_xll_decode_navi(s, end_pos) == 0)
                    s->exss_ext_mask |= DCA_EXT_EXSS_XLL;
                break;
            case DCA_SYNCWORD_XBR:
            case DCA_SYNCWORD_XXCH:
            default:
                av_log(s->avctx, AV_LOG_VERBOSE,
                       "DTS-ExSS: unknown marker = 0x%08"PRIx32"\n", mkr);
            }

            /* skip to end of block */
            j = get_bits_count(&s->gb);
            if (j > end_pos)
                av_log(s->avctx, AV_LOG_ERROR,
                       "DTS-ExSS: Processed asset too long.\n");
            if (j < end_pos)
                skip_bits_long(&s->gb, end_pos - j);
        }
    }
}
