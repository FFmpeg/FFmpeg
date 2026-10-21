/*
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

/*
 * Feed hand made packet sequences to the MPEG-TS probe.
 *
 * analyze() only counts the sync bytes of null packets (PID 0x1FFF) and of
 * packets whose adaptation field control (afc) is not 0. Packets are given as
 * a string with one character each, see put_packet(). The score tells which
 * packets were counted: 100 if all of them, 0 if (almost) none.
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"

extern const FFInputFormat ff_mpegts_demuxer;

#define NB_PACKETS   12
#define MAX_PREFIX   8
#define MAX_STRIDE   204

/*
 * D: PID 0x100, adaptation field control 1 (payload only)
 * d: PID 0x100, adaptation field control 0 (reserved)
 * N: PID 0x1FFF, adaptation field control 1
 * n: PID 0x1FFF, adaptation field control 0
 * L: PID 0x1FF, adaptation field control 0 (low byte of the null PID)
 * H: PID 0x1F00, adaptation field control 0 (high byte of the null PID)
 */
static void put_packet(uint8_t *p, char type)
{
    int pid = 0x1FFF;
    int afc = (type == 'D' || type == 'N') ? 1 : 0;

    if (type == 'D' || type == 'd')
        pid = 0x100;
    else if (type == 'L')
        pid = 0x1FF;
    else if (type == 'H')
        pid = 0x1F00;

    p[0] = 0x47;
    p[1] = pid >> 8;
    p[2] = pid & 0xFF;
    p[3] = afc << 4;
}

static void test(const char *name, int prefix_size, int prefix_fill,
                 int stride, const char *packets)
{
    uint8_t *buf = av_mallocz(MAX_PREFIX + NB_PACKETS * MAX_STRIDE +
                              AVPROBE_PADDING_SIZE);
    AVProbeData pd = { .buf = buf };
    int i;

    if (!buf)
        return;

    /* Neither the filler nor the padding contains a sync byte. */
    memset(buf, prefix_fill, prefix_size);
    memset(buf + prefix_size, 0xFF, NB_PACKETS * stride);
    for (i = 0; i < NB_PACKETS; i++)
        put_packet(buf + prefix_size + i * stride, packets[i]);
    pd.buf_size = prefix_size + NB_PACKETS * stride;

    printf("%s: %d\n", name, ff_mpegts_demuxer.read_probe(&pd));

    av_free(buf);
}

int main(void)
{
    /* Packets at the start of the buffer, 188, 192 or 204 bytes apart. */
    test("payload",                             0, 0x00, 188, "DDDDDDDDDDDD");
    test("null, afc 0",                         0, 0x00, 188, "nnnnnnnnnnnn");
    test("payload, then null with afc 0",       0, 0x00, 188, "Dnnnnnnnnnnn");
    test("payload, then null with afc 0, 192",  0, 0x00, 192, "Dnnnnnnnnnnn");
    test("payload, then null with afc 0, 204",  0, 0x00, 204, "Dnnnnnnnnnnn");
    test("null, then payload with afc 0",       0, 0x00, 188, "Nddddddddddd");
    test("payload and null with afc 0, mixed",  0, 0x00, 188, "DnDnDnDnDnDn");
    test("payload, a few null with afc 0",      0, 0x00, 188, "DDnDDnDDnDDD");
    test("null, then PID 0x1FF with afc 0",     0, 0x00, 188, "nLLLLLLLLLLL");
    test("null, then PID 0x1F00 with afc 0",    0, 0x00, 188, "nHHHHHHHHHHH");

    /* The packets follow 5 bytes which are not part of the stream. */
    test("null with afc 0, after 0x00",         5, 0x00, 188, "nnnnnnnnnnnn");
    test("payload with afc 0, after 0xFF",      5, 0xFF, 188, "dddddddddddd");
    test("payload, after 0xFF",                 5, 0xFF, 188, "DDDDDDDDDDDD");

    return 0;
}
