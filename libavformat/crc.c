/* 
 * CRC decoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define DO1(buf)  {s1 += *buf++; s2 += s1;}
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);
#define DO16(buf) DO8(buf); DO8(buf);

unsigned long update_adler32(unsigned long adler, const uint8_t *buf, unsigned int len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;
    int k;

    if (buf == NULL) return 1L;

    while (len > 0) {
        k = len < NMAX ? len : NMAX;
        len -= k;
        while (k >= 16) {
            DO16(buf);
            k -= 16;
        }
        if (k != 0) do {
            DO1(buf);
        } while (--k);
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

typedef struct CRCState {
    uint32_t crcval;
} CRCState;

static int crc_write_header(struct AVFormatContext *s)
{
    CRCState *crc = s->priv_data;

    /* init CRC */
    crc->crcval = update_adler32(0, NULL, 0);

    return 0;
}

static int crc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    CRCState *crc = s->priv_data;
    crc->crcval = update_adler32(crc->crcval, pkt->data, pkt->size);
    return 0;
}

static int crc_write_trailer(struct AVFormatContext *s)
{
    CRCState *crc = s->priv_data;
    char buf[64];

    snprintf(buf, sizeof(buf), "CRC=%08x\n", crc->crcval);
    put_buffer(&s->pb, buf, strlen(buf));
    put_flush_packet(&s->pb);
    return 0;
}

static AVOutputFormat crc_format = {
    "crc",
    "crc testing format",
    NULL,
    "",
    sizeof(CRCState),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_RAWVIDEO,
    crc_write_header,
    crc_write_packet,
    crc_write_trailer,
};

int crc_init(void)
{
    av_register_output_format(&crc_format);
    return 0;
}
