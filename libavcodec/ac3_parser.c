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

    memset(hdr, 0, sizeof(*hdr));

    init_get_bits(&gbc, buf, 54);

    hdr->sync_word = get_bits(&gbc, 16);
    if(hdr->sync_word != 0x0B77)
        return -1;

    /* read ahead to bsid to make sure this is AC-3, not E-AC-3 */
    hdr->bsid = show_bits_long(&gbc, 29) & 0x1F;
    if(hdr->bsid > 10)
        return -2;

    hdr->crc1 = get_bits(&gbc, 16);
    hdr->fscod = get_bits(&gbc, 2);
    if(hdr->fscod == 3)
        return -3;

    hdr->frmsizecod = get_bits(&gbc, 6);
    if(hdr->frmsizecod > 37)
        return -4;

    skip_bits(&gbc, 5); // skip bsid, already got it

    hdr->bsmod = get_bits(&gbc, 3);
    hdr->acmod = get_bits(&gbc, 3);
    if((hdr->acmod & 1) && hdr->acmod != 1) {
        hdr->cmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod & 4) {
        hdr->surmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod == 2) {
        hdr->dsurmod = get_bits(&gbc, 2);
    }
    hdr->lfeon = get_bits1(&gbc);

    hdr->halfratecod = FFMAX(hdr->bsid, 8) - 8;
    hdr->sample_rate = ff_ac3_freqs[hdr->fscod] >> hdr->halfratecod;
    hdr->bit_rate = (ff_ac3_bitratetab[hdr->frmsizecod>>1] * 1000) >> hdr->halfratecod;
    hdr->channels = ff_ac3_channels[hdr->acmod] + hdr->lfeon;
    hdr->frame_size = ff_ac3_frame_sizes[hdr->frmsizecod][hdr->fscod] * 2;

    return 0;
}

static int ac3_sync(const uint8_t *buf, int *channels, int *sample_rate,
                    int *bit_rate, int *samples)
{
    int err;
    unsigned int fscod, acmod, bsid, lfeon;
    unsigned int strmtyp, substreamid, frmsiz, fscod2, numblkscod;
    GetBitContext bits;
    AC3HeaderInfo hdr;

    err = ff_ac3_parse_header(buf, &hdr);

    if(err < 0 && err != -2)
        return 0;

    bsid = hdr.bsid;
    if(bsid <= 10) {             /* Normal AC-3 */
        *sample_rate = hdr.sample_rate;
        *bit_rate = hdr.bit_rate;
        *channels = hdr.channels;
        *samples = AC3_FRAME_SIZE;
        return hdr.frame_size;
    } else if (bsid > 10 && bsid <= 16) { /* Enhanced AC-3 */
        init_get_bits(&bits, &buf[2], (AC3_HEADER_SIZE-2) * 8);
        strmtyp = get_bits(&bits, 2);
        substreamid = get_bits(&bits, 3);

        if (strmtyp != 0 || substreamid != 0)
            return 0;   /* Currently don't support additional streams */

        frmsiz = get_bits(&bits, 11) + 1;
        fscod = get_bits(&bits, 2);
        if (fscod == 3) {
            fscod2 = get_bits(&bits, 2);
            numblkscod = 3;

            if(fscod2 == 3)
                return 0;

            *sample_rate = ff_ac3_freqs[fscod2] / 2;
        } else {
            numblkscod = get_bits(&bits, 2);

            *sample_rate = ff_ac3_freqs[fscod];
        }

        acmod = get_bits(&bits, 3);
        lfeon = get_bits1(&bits);

        *samples = eac3_blocks[numblkscod] * 256;
        *bit_rate = frmsiz * (*sample_rate) * 16 / (*samples);
        *channels = ff_ac3_channels[acmod] + lfeon;

        return frmsiz * 2;
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
