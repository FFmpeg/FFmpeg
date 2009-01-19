/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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
#include "aac_parser.h"
#include "bitstream.h"
#include "mpeg4audio.h"

#define AAC_HEADER_SIZE 7

int ff_aac_parse_header(GetBitContext *gbc, AACADTSHeaderInfo *hdr)
{
    int size, rdb, ch, sr;
    int aot, crc_abs;

    if(get_bits(gbc, 12) != 0xfff)
        return AAC_AC3_PARSE_ERROR_SYNC;

    skip_bits1(gbc);             /* id */
    skip_bits(gbc, 2);           /* layer */
    crc_abs = get_bits1(gbc);    /* protection_absent */
    aot     = get_bits(gbc, 2);  /* profile_objecttype */
    sr      = get_bits(gbc, 4);  /* sample_frequency_index */
    if(!ff_mpeg4audio_sample_rates[sr])
        return AAC_AC3_PARSE_ERROR_SAMPLE_RATE;
    skip_bits1(gbc);             /* private_bit */
    ch      = get_bits(gbc, 3);  /* channel_configuration */

    if(!ff_mpeg4audio_channels[ch])
        return AAC_AC3_PARSE_ERROR_CHANNEL_CFG;

    skip_bits1(gbc);             /* original/copy */
    skip_bits1(gbc);             /* home */

    /* adts_variable_header */
    skip_bits1(gbc);             /* copyright_identification_bit */
    skip_bits1(gbc);             /* copyright_identification_start */
    size    = get_bits(gbc, 13); /* aac_frame_length */
    if(size < AAC_HEADER_SIZE)
        return AAC_AC3_PARSE_ERROR_FRAME_SIZE;

    skip_bits(gbc, 11);          /* adts_buffer_fullness */
    rdb = get_bits(gbc, 2);      /* number_of_raw_data_blocks_in_frame */

    hdr->object_type    = aot;
    hdr->chan_config    = ch;
    hdr->crc_absent     = crc_abs;
    hdr->num_aac_frames = rdb + 1;
    hdr->sampling_index = sr;
    hdr->sample_rate    = ff_mpeg4audio_sample_rates[sr];
    hdr->samples        = (rdb + 1) * 1024;
    hdr->bit_rate       = size * 8 * hdr->sample_rate / hdr->samples;

    return size;
}

static int aac_sync(uint64_t state, AACAC3ParseContext *hdr_info,
        int *need_next_header, int *new_frame_start)
{
    GetBitContext bits;
    AACADTSHeaderInfo hdr;
    int size;
    union {
        uint64_t u64;
        uint8_t  u8[8];
    } tmp;

    tmp.u64 = be2me_64(state);
    init_get_bits(&bits, tmp.u8+8-AAC_HEADER_SIZE, AAC_HEADER_SIZE * 8);

    if ((size = ff_aac_parse_header(&bits, &hdr)) < 0)
        return 0;
    *need_next_header = 0;
    *new_frame_start  = 1;
    hdr_info->sample_rate = hdr.sample_rate;
    hdr_info->channels    = ff_mpeg4audio_channels[hdr.chan_config];
    hdr_info->samples     = hdr.samples;
    hdr_info->bit_rate    = hdr.bit_rate;
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
