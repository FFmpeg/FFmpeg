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

    memset(hdr, 0, sizeof(*hdr));

    init_get_bits(&gbc, buf, 54);

    hdr->sync_word = get_bits(&gbc, 16);
    if(hdr->sync_word != 0x0B77)
        return AC3_PARSE_ERROR_SYNC;

    /* read ahead to bsid to make sure this is AC-3, not E-AC-3 */
    hdr->bitstream_id = show_bits_long(&gbc, 29) & 0x1F;
    if(hdr->bitstream_id > 10)
        return AC3_PARSE_ERROR_BSID;

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

    return 0;
}

static int ac3_sync(const uint8_t *buf, int *channels, int *sample_rate,
                    int *bit_rate, int *samples)
{
    int err;
    unsigned int sr_code, channel_mode, bitstream_id, lfe_on;
    unsigned int stream_type, substream_id, frame_size, sr_code2, num_blocks_code;
    GetBitContext bits;
    AC3HeaderInfo hdr;

    err = ff_ac3_parse_header(buf, &hdr);

    if(err < 0 && err != -2)
        return 0;

    bitstream_id = hdr.bitstream_id;
    if(bitstream_id <= 10) {             /* Normal AC-3 */
        *sample_rate = hdr.sample_rate;
        *bit_rate = hdr.bit_rate;
        *channels = hdr.channels;
        *samples = AC3_FRAME_SIZE;
        return hdr.frame_size;
    } else if (bitstream_id > 10 && bitstream_id <= 16) { /* Enhanced AC-3 */
        init_get_bits(&bits, &buf[2], (AC3_HEADER_SIZE-2) * 8);
        stream_type = get_bits(&bits, 2);
        substream_id = get_bits(&bits, 3);

        if (stream_type != 0 || substream_id != 0)
            return 0;   /* Currently don't support additional streams */

        frame_size = get_bits(&bits, 11) + 1;
        if(frame_size*2 < AC3_HEADER_SIZE)
            return 0;

        sr_code = get_bits(&bits, 2);
        if (sr_code == 3) {
            sr_code2 = get_bits(&bits, 2);
            num_blocks_code = 3;

            if(sr_code2 == 3)
                return 0;

            *sample_rate = ff_ac3_sample_rate_tab[sr_code2] / 2;
        } else {
            num_blocks_code = get_bits(&bits, 2);

            *sample_rate = ff_ac3_sample_rate_tab[sr_code];
        }

        channel_mode = get_bits(&bits, 3);
        lfe_on = get_bits1(&bits);

        *samples = eac3_blocks[num_blocks_code] * 256;
        *bit_rate = frame_size * (*sample_rate) * 16 / (*samples);
        *channels = ff_ac3_channels_tab[channel_mode] + lfe_on;

        return frame_size * 2;
    }

    /* Unsupported bitstream version */
    return 0;
}

static int ac3_parse_init(AVCodecParserContext *s1)
{
    AACAC3ParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
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
