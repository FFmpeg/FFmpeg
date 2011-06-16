/*
 * E-AC-3 encoder
 * Copyright (c) 2011 Justin Ruggles <justin.ruggles@gmail.com>
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
 * E-AC-3 encoder
 */

#define CONFIG_AC3ENC_FLOAT 1
#include "ac3enc.h"
#include "eac3enc.h"


#define AC3ENC_TYPE AC3ENC_TYPE_EAC3
#include "ac3enc_opts_template.c"
static AVClass eac3enc_class = { "E-AC-3 Encoder", av_default_item_name,
                                 eac3_options, LIBAVUTIL_VERSION_INT };


void ff_eac3_set_cpl_states(AC3EncodeContext *s)
{
    int ch, blk;
    int first_cpl_coords[AC3_MAX_CHANNELS];

    /* set first cpl coords */
    for (ch = 1; ch <= s->fbw_channels; ch++)
        first_cpl_coords[ch] = 1;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (block->channel_in_cpl[ch]) {
                if (first_cpl_coords[ch]) {
                    block->new_cpl_coords = 2;
                    first_cpl_coords[ch]  = 0;
                }
            } else {
                first_cpl_coords[ch] = 1;
            }
        }
    }

    /* set first cpl leak */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (block->cpl_in_use) {
            block->new_cpl_leak = 2;
            break;
        }
    }
}


void ff_eac3_output_frame_header(AC3EncodeContext *s)
{
    int blk, ch;
    AC3EncOptions *opt = &s->options;

    put_bits(&s->pb, 16, 0x0b77);                   /* sync word */

    /* BSI header */
    put_bits(&s->pb,  2, 0);                        /* stream type = independent */
    put_bits(&s->pb,  3, 0);                        /* substream id = 0 */
    put_bits(&s->pb, 11, (s->frame_size / 2) - 1);  /* frame size */
    if (s->bit_alloc.sr_shift) {
        put_bits(&s->pb, 2, 0x3);                   /* fscod2 */
        put_bits(&s->pb, 2, s->bit_alloc.sr_code);  /* sample rate code */
    } else {
        put_bits(&s->pb, 2, s->bit_alloc.sr_code);  /* sample rate code */
        put_bits(&s->pb, 2, 0x3);                   /* number of blocks = 6 */
    }
    put_bits(&s->pb, 3, s->channel_mode);           /* audio coding mode */
    put_bits(&s->pb, 1, s->lfe_on);                 /* LFE channel indicator */
    put_bits(&s->pb, 5, s->bitstream_id);           /* bitstream id (EAC3=16) */
    put_bits(&s->pb, 5, -opt->dialogue_level);      /* dialogue normalization level */
    put_bits(&s->pb, 1, 0);                         /* no compression gain */
    put_bits(&s->pb, 1, 0);                         /* no mixing metadata */
    /* TODO: mixing metadata */
    put_bits(&s->pb, 1, 0);                         /* no info metadata */
    /* TODO: info metadata */
    put_bits(&s->pb, 1, 0);                         /* no additional bit stream info */

    /* frame header */
    put_bits(&s->pb, 1, 1);                         /* exponent strategy syntax = each block */
    put_bits(&s->pb, 1, 0);                         /* aht enabled = no */
    put_bits(&s->pb, 2, 0);                         /* snr offset strategy = 1 */
    put_bits(&s->pb, 1, 0);                         /* transient pre-noise processing enabled = no */
    put_bits(&s->pb, 1, 0);                         /* block switch syntax enabled = no */
    put_bits(&s->pb, 1, 0);                         /* dither flag syntax enabled = no */
    put_bits(&s->pb, 1, 0);                         /* bit allocation model syntax enabled = no */
    put_bits(&s->pb, 1, 0);                         /* fast gain codes enabled = no */
    put_bits(&s->pb, 1, 0);                         /* dba syntax enabled = no */
    put_bits(&s->pb, 1, 0);                         /* skip field syntax enabled = no */
    put_bits(&s->pb, 1, 0);                         /* spx enabled = no */
    /* coupling strategy use flags */
    if (s->channel_mode > AC3_CHMODE_MONO) {
        put_bits(&s->pb, 1, s->blocks[0].cpl_in_use);
        for (blk = 1; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            put_bits(&s->pb, 1, block->new_cpl_strategy);
            if (block->new_cpl_strategy)
                put_bits(&s->pb, 1, block->cpl_in_use);
        }
    }
    /* exponent strategy */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
        for (ch = !s->blocks[blk].cpl_in_use; ch <= s->fbw_channels; ch++)
            put_bits(&s->pb, 2, s->exp_strategy[ch][blk]);
    if (s->lfe_on) {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
            put_bits(&s->pb, 1, s->exp_strategy[s->lfe_channel][blk]);
    }
    /* E-AC-3 to AC-3 converter exponent strategy (unfortunately not optional...) */
    for (ch = 1; ch <= s->fbw_channels; ch++)
        put_bits(&s->pb, 5, 0);
    /* snr offsets */
    put_bits(&s->pb, 6, s->coarse_snr_offset);
    put_bits(&s->pb, 4, s->fine_snr_offset[1]);
    /* block start info */
    put_bits(&s->pb, 1, 0);
}


#if CONFIG_EAC3_ENCODER
AVCodec ff_eac3_encoder = {
    .name            = "eac3",
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = CODEC_ID_EAC3,
    .priv_data_size  = sizeof(AC3EncodeContext),
    .init            = ff_ac3_encode_init,
    .encode          = ff_ac3_encode_frame,
    .close           = ff_ac3_encode_close,
    .sample_fmts     = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_FLT,AV_SAMPLE_FMT_NONE},
    .long_name       = NULL_IF_CONFIG_SMALL("ATSC A/52 E-AC-3"),
    .priv_class      = &eac3enc_class,
    .channel_layouts = ff_ac3_channel_layouts,
};
#endif
