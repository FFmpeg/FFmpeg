/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
 * Copyright (c) 2009 Alex Converse
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

#include "adts_header.h"
#include "adts_parser.h"
#include "get_bits.h"
#include "mpeg4audio.h"
#include "libavutil/avassert.h"

int ff_adts_header_parse(GetBitContext *gbc, AACADTSHeaderInfo *hdr)
{
    int size, rdb, ch, sr;
    int aot, crc_abs;

    memset(hdr, 0, sizeof(*hdr));

    if (get_bits(gbc, 12) != 0xfff)
        return AAC_PARSE_ERROR_SYNC;

    skip_bits1(gbc);             /* id */
    skip_bits(gbc, 2);           /* layer */
    crc_abs = get_bits1(gbc);    /* protection_absent */
    aot     = get_bits(gbc, 2);  /* profile_objecttype */
    sr      = get_bits(gbc, 4);  /* sample_frequency_index */
    if (!ff_mpeg4audio_sample_rates[sr])
        return AAC_PARSE_ERROR_SAMPLE_RATE;
    skip_bits1(gbc);             /* private_bit */
    ch = get_bits(gbc, 3);       /* channel_configuration */

    skip_bits1(gbc);             /* original/copy */
    skip_bits1(gbc);             /* home */

    /* adts_variable_header */
    skip_bits1(gbc);             /* copyright_identification_bit */
    skip_bits1(gbc);             /* copyright_identification_start */
    size = get_bits(gbc, 13);    /* aac_frame_length */
    if (size < AV_AAC_ADTS_HEADER_SIZE)
        return AAC_PARSE_ERROR_FRAME_SIZE;

    skip_bits(gbc, 11);          /* adts_buffer_fullness */
    rdb = get_bits(gbc, 2);      /* number_of_raw_data_blocks_in_frame */

    hdr->object_type    = aot + 1;
    hdr->chan_config    = ch;
    hdr->crc_absent     = crc_abs;
    hdr->num_aac_frames = rdb + 1;
    hdr->sampling_index = sr;
    hdr->sample_rate    = ff_mpeg4audio_sample_rates[sr];
    hdr->samples        = (rdb + 1) * 1024;
    hdr->bit_rate       = size * 8 * hdr->sample_rate / hdr->samples;
    hdr->frame_length   = size;

    return size;
}

int ff_adts_header_parse_buf(const uint8_t buf[AV_AAC_ADTS_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE],
                             AACADTSHeaderInfo *hdr)
{
    GetBitContext gb;
    av_unused int ret = init_get_bits8(&gb, buf, AV_AAC_ADTS_HEADER_SIZE);
    av_assert1(ret >= 0);
    return ff_adts_header_parse(&gb, hdr);
}
