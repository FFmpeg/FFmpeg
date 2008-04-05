/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
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

#include "parser.h"
#include "aac_ac3_parser.h"
#include "bitstream.h"
#include "mpeg4audio.h"

#define AAC_HEADER_SIZE 7

static int aac_sync(uint64_t state, AACAC3ParseContext *hdr_info,
        int *need_next_header, int *new_frame_start)
{
    GetBitContext bits;
    int size, rdb, ch, sr;
    uint64_t tmp = be2me_64(state);

    init_get_bits(&bits, (uint8_t *)&tmp, AAC_HEADER_SIZE * 8);

    if(get_bits(&bits, 12) != 0xfff)
        return 0;

    skip_bits1(&bits);          /* id */
    skip_bits(&bits, 2);        /* layer */
    skip_bits1(&bits);          /* protection_absent */
    skip_bits(&bits, 2);        /* profile_objecttype */
    sr = get_bits(&bits, 4);    /* sample_frequency_index */
    if(!ff_mpeg4audio_sample_rates[sr])
        return 0;
    skip_bits1(&bits);          /* private_bit */
    ch = get_bits(&bits, 3);    /* channel_configuration */
    if(!ff_mpeg4audio_channels[ch])
        return 0;
    skip_bits1(&bits);          /* original/copy */
    skip_bits1(&bits);          /* home */

    /* adts_variable_header */
    skip_bits1(&bits);          /* copyright_identification_bit */
    skip_bits1(&bits);          /* copyright_identification_start */
    size = get_bits(&bits, 13); /* aac_frame_length */
    if(size < AAC_HEADER_SIZE)
        return 0;

    skip_bits(&bits, 11);       /* adts_buffer_fullness */
    rdb = get_bits(&bits, 2);   /* number_of_raw_data_blocks_in_frame */

    hdr_info->channels = ff_mpeg4audio_channels[ch];
    hdr_info->sample_rate = ff_mpeg4audio_sample_rates[sr];
    hdr_info->samples = (rdb + 1) * 1024;
    hdr_info->bit_rate = size * 8 * hdr_info->sample_rate / hdr_info->samples;

    *need_next_header=0;
    *new_frame_start=1;
    return size;
}

static av_cold int aac_parse_init(AVCodecParserContext *s1)
{
    AACAC3ParseContext *s = s1->priv_data;
    s->header_size = AAC_HEADER_SIZE;
    s->sync = aac_sync;
    return 0;
}


AVCodecParser aac_parser = {
    { CODEC_ID_AAC },
    sizeof(AACAC3ParseContext),
    aac_parse_init,
    ff_aac_ac3_parse,
    NULL,
};
