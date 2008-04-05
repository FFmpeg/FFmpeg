/*
 * AC3 parser
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
#include "ac3_parser.h"
#include "aac_ac3_parser.h"
#include "bitstream.h"


#define AC3_HEADER_SIZE 7


static const uint8_t eac3_blocks[4] = {
    1, 2, 3, 6
};


int ff_ac3_parse_header(const uint8_t buf[7], AC3HeaderInfo *hdr)
{
    GetBitContext gbc;
    int frame_size_code;
    int num_blocks;

    memset(hdr, 0, sizeof(*hdr));

    init_get_bits(&gbc, buf, 54);

    hdr->sync_word = get_bits(&gbc, 16);
    if(hdr->sync_word != 0x0B77)
        return AC3_PARSE_ERROR_SYNC;

    /* read ahead to bsid to distinguish between AC-3 and E-AC-3 */
    hdr->bitstream_id = show_bits_long(&gbc, 29) & 0x1F;
    if(hdr->bitstream_id > 16)
        return AC3_PARSE_ERROR_BSID;

    if(hdr->bitstream_id <= 10) {
        /* Normal AC-3 */
        hdr->crc1 = get_bits(&gbc, 16);
        hdr->sr_code = get_bits(&gbc, 2);
        if(hdr->sr_code == 3)
            return AC3_PARSE_ERROR_SAMPLE_RATE;

        frame_size_code = get_bits(&gbc, 6);
        if(frame_size_code > 37)
            return AC3_PARSE_ERROR_FRAME_SIZE;

        skip_bits(&gbc, 5); // skip bsid, already got it

        skip_bits(&gbc, 3); // skip bitstream mode
        hdr->channel_mode = get_bits(&gbc, 3);
        if((hdr->channel_mode & 1) && hdr->channel_mode != AC3_CHMODE_MONO) {
            skip_bits(&gbc, 2); // skip center mix level
        }
        if(hdr->channel_mode & 4) {
            skip_bits(&gbc, 2); // skip surround mix level
        }
        if(hdr->channel_mode == AC3_CHMODE_STEREO) {
            skip_bits(&gbc, 2); // skip dolby surround mode
        }
        hdr->lfe_on = get_bits1(&gbc);

        hdr->sr_shift = FFMAX(hdr->bitstream_id, 8) - 8;
        hdr->sample_rate = ff_ac3_sample_rate_tab[hdr->sr_code] >> hdr->sr_shift;
        hdr->bit_rate = (ff_ac3_bitrate_tab[frame_size_code>>1] * 1000) >> hdr->sr_shift;
        hdr->channels = ff_ac3_channels_tab[hdr->channel_mode] + hdr->lfe_on;
        hdr->frame_size = ff_ac3_frame_size_tab[frame_size_code][hdr->sr_code] * 2;
        hdr->frame_type = EAC3_FRAME_TYPE_INDEPENDENT;
    } else {
        /* Enhanced AC-3 */
        hdr->crc1 = 0;
        hdr->frame_type = get_bits(&gbc, 2);
        if(hdr->frame_type == EAC3_FRAME_TYPE_RESERVED)
            return AC3_PARSE_ERROR_FRAME_TYPE;

        skip_bits(&gbc, 3); // skip substream id

        hdr->frame_size = (get_bits(&gbc, 11) + 1) << 1;
        if(hdr->frame_size < AC3_HEADER_SIZE)
            return AC3_PARSE_ERROR_FRAME_SIZE;

        hdr->sr_code = get_bits(&gbc, 2);
        if (hdr->sr_code == 3) {
            int sr_code2 = get_bits(&gbc, 2);
            if(sr_code2 == 3)
                return AC3_PARSE_ERROR_SAMPLE_RATE;
            hdr->sample_rate = ff_ac3_sample_rate_tab[sr_code2] / 2;
            hdr->sr_shift = 1;
            num_blocks = 6;
        } else {
            num_blocks = eac3_blocks[get_bits(&gbc, 2)];
            hdr->sample_rate = ff_ac3_sample_rate_tab[hdr->sr_code];
            hdr->sr_shift = 0;
        }

        hdr->channel_mode = get_bits(&gbc, 3);
        hdr->lfe_on = get_bits1(&gbc);

        hdr->bit_rate = (uint32_t)(8.0 * hdr->frame_size * hdr->sample_rate /
                        (num_blocks * 256.0));
        hdr->channels = ff_ac3_channels_tab[hdr->channel_mode] + hdr->lfe_on;
    }

    return 0;
}

static int ac3_sync(uint64_t state, AACAC3ParseContext *hdr_info,
        int *need_next_header, int *new_frame_start)
{
    int err;
    uint64_t tmp = be2me_64(state);
    AC3HeaderInfo hdr;

    err = ff_ac3_parse_header((uint8_t *)&tmp, &hdr);

    if(err < 0)
        return 0;

    hdr_info->sample_rate = hdr.sample_rate;
    hdr_info->bit_rate = hdr.bit_rate;
    hdr_info->channels = hdr.channels;
    hdr_info->samples = AC3_FRAME_SIZE;

    *need_next_header = (hdr.frame_type != EAC3_FRAME_TYPE_AC3_CONVERT);
    *new_frame_start = (hdr.frame_type != EAC3_FRAME_TYPE_DEPENDENT);
    return hdr.frame_size;
}

static av_cold int ac3_parse_init(AVCodecParserContext *s1)
{
    AACAC3ParseContext *s = s1->priv_data;
    s->header_size = AC3_HEADER_SIZE;
    s->sync = ac3_sync;
    return 0;
}


AVCodecParser ac3_parser = {
    { CODEC_ID_AC3 },
    sizeof(AACAC3ParseContext),
    ac3_parse_init,
    ff_aac_ac3_parse,
    NULL,
};
