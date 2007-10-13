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


#define AAC_HEADER_SIZE 7


static const int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static const int aac_channels[8] = {
    0, 1, 2, 3, 4, 5, 6, 8
};


static int aac_sync(const uint8_t *buf, int *channels, int *sample_rate,
                    int *bit_rate, int *samples)
{
    GetBitContext bits;
    int size, rdb, ch, sr;

    init_get_bits(&bits, buf, AAC_HEADER_SIZE * 8);

    if(get_bits(&bits, 12) != 0xfff)
        return 0;

    skip_bits1(&bits);          /* id */
    skip_bits(&bits, 2);        /* layer */
    skip_bits1(&bits);          /* protection_absent */
    skip_bits(&bits, 2);        /* profile_objecttype */
    sr = get_bits(&bits, 4);    /* sample_frequency_index */
    if(!aac_sample_rates[sr])
        return 0;
    skip_bits1(&bits);          /* private_bit */
    ch = get_bits(&bits, 3);    /* channel_configuration */
    if(!aac_channels[ch])
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

    *channels = aac_channels[ch];
    *sample_rate = aac_sample_rates[sr];
    *samples = (rdb + 1) * 1024;
    *bit_rate = size * 8 * *sample_rate / *samples;

    return size;
}

static int aac_parse_init(AVCodecParserContext *s1)
{
    AACAC3ParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
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
